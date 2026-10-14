# FRR frr_pthread 机制深入详解

> 基于 `frr-10.6.1` 源码（分支 `study/bgpd-annotated`）整理。
> 所有行号均为本仓库当前代码的实测量结果，改动代码后需复核。
> 用途：把 FRR「每线程一个事件循环」的并发模型讲透 —— 从**为什么要这样设计**，
> 到 `frr_pthread_new/run/wait_running/stop/destroy` 的**每一步时序**，
> 再到它和 **RCU / 信号 / zlog / 统计** 四套子系统的咬合关系，
> 最后给出**全树真实用法**和**踩坑清单**。

---

## 0. 全局总览（先记这张图）

### 0.1 一句话结论

**`frr_pthread` 不是「线程库」，而是「把 FRR 的事件循环搬进线程」的适配层。**

它的全部价值在于一句话：**让新建的 pthread 拥有自己的 `event_loop`，从而可以用
`event_add_event()` 这套已经写好的机制，向它投递任务。**

理解了这一点，后面所有设计（启动握手、毒丸停止、RCU 注册、信号屏蔽）都是自然的推论。

### 0.2 心智模型：FRR 的并发哲学

```
传统多线程思路                         FRR 思路（本仓库实际做法）
+---------------------+               +-------------------------------+
| 共享内存 + 大锁      |               | 每个线程一个独立事件循环       |
| 谁都能改任何数据     |               | 跨线程只投递「事件」，不直接调函数 |
+---------------------+               +-------------------------------+
+---------------------+               +-------------------------------+
| 加锁逻辑散落各处     |               | 共享数据尽量收敛到「生产者-消费者队列」|
+---------------------+               +-------------------------------+
```

对照官方文档 [doc/developer/process-architecture.rst:186-205](doc/developer/process-architecture.rst#L186-L205) 的原话：

> Each kernel thread behaves as a lightweight process within FRR, sharing the same
> process memory space. ... a natural choice is to implement the event system within
> each kernel thread. This allows us to leverage the event-driven execution model with
> the currently existing task and context primitives.
>
> 每个内核线程在FRR中表现为一个轻量级进程，共享相同的进程内存空间。……因此，自然的选择是在每个内核线程内部实现事件系统。这使我们能够利用现有的任务和上下文原语，采用事件驱动的执行模型。

翻译：**把内核线程当成「轻量级进程」用**——进程之间怎么通信（消息/事件），
线程之间就怎么通信。这样 FRR 里海量「假设单线程」的既有代码不需要重写。

### 0.3 全景图

```
        ┌──────────────────── 一个 FRR 守护进程 ──────────────────────┐
        │                                                            │
        │  主线程 (main)                    工作线程 (worker)         │
        │  ┌──────────────────┐            ┌──────────────────┐      │
        │  │  master (主 loop)│            │ master (子 loop) │      │
        │  │  struct event_loop│           │ struct event_loop│      │
        │  └────────┬─────────┘            └────────▲─────────┘      │
        │           │                               │                │
        │   frr_run(loop):                  fpt_run(loop):           │
        │   while(event_fetch)              while(fpt->running)      │
        │       event_call();                   event_call();        │
        │           │                               ▲                │
        │           │                               │                │
        │           │   event_add_event(worker_master, ...) ─────┐   │
        │           │   (跨线程投递 = 唯一的通信方式)              │   │
        │           │                                            │   │
        │           │   event_add_event(main_master, ...) ◄──────┘   │
        │           │                                                │
        │  ┌────────▼───────────────────────────────────────────┐    │
        │  │ struct frr_pthread  （把上面两块粘起来的那层胶水）    │    │
        │  │  thread / rcu_thread / master / attr / 两组条件变量 │    │
        │  └────────────────────────────────────────────────────┘    │
        └────────────────────────────────────────────────────────────┘
```

### 0.4 三层文件职责

| 层 | 文件 | 职责 |
|----|------|------|
| 线程外壳 | [lib/frr_pthread.c](lib/frr_pthread.c) / [lib/frr_pthread.h](lib/frr_pthread.h) | 线程创建/启动/停止/销毁；启动握手；信号屏蔽；默认事件循环 `fpt_run` |
| 事件循环 | [lib/event.c](lib/event.c) / [lib/frrevent.h](lib/frrevent.h) | `struct event_loop`、`event_fetch()`、`event_call()`、`event_add_*()`、跨线程唤醒管道 |
| 支撑 | [lib/frrcu.c](lib/frrcu.c) / [lib/sigevent.c](lib/sigevent.c) / [lib/zlog.c](lib/zlog.c) | RCU 读侧注册、信号掩码、线程局部日志缓冲 |

---

## 1. 为什么需要 frr_pthread？（设计动机）

### 1.1 纯事件循环的三个痛点

FRR 的传统模型是单线程事件循环，所有任务串行执行。这在协议实现上极方便（不用担心
数据竞争），但有三处硬伤：

| 痛点 | 场景 | 后果 |
|------|------|------|
| **阻塞即全局阻塞** | 一次 `read()` 卡住 / 一次大路由表遍历 | 整个协议栈停摆，keepalive 超时，邻居抖动 |
| **I/O 与协议逻辑抢 CPU** | 高吞吐 BGP 会话：收发报文 vs 状态机 | 收包延迟直接影响收敛速度 |
| **无法利用多核** | 多核服务器上的单守护进程 | 一个核心跑满，其余闲置 |

### 1.2 那为什么不直接 `pthread_create()`？

因为直接建线程会**同时引入两个问题**：

**问题一：没有事件循环，就没法用 FRR 现有的一切。**

FRR 的定时器、fd 事件、大半个 lib 都建立在 `struct event_loop` 之上。
一个裸线程拿到 `struct event_loop *` 也没用——它不知道是谁的、怎么注册自己、
退出时怎么清理。

**问题二：跨线程调用 = 数据竞争。**

如果允许主线程直接调用工作线程里的函数（比如 `bgp_write(peer)`），
那么 `peer` 结构体的每一个字段都变成需要加锁的共享数据——这是灾难性的改造量。

`frr_pthread` 的答案是：**给每个线程配一个自己的 `event_loop`**。
这样跨线程交互退化成「往对方的 loop 上投递一个事件」，也就是**借用现成的消息队列机制**。
正如官方文档所说 [doc/developer/process-architecture.rst:229-237](doc/developer/process-architecture.rst#L229-L237)：

> To schedule tasks on a particular pthread, simply use the regular `event.c` functions
> as usual and provide the `threadmaster` pointed to from the `frr_pthread`. ... This serves
> as the basis for inter-thread communication and boils down to a **slightly more complicated
> method of message passing**, where the messages are the regular task events.
>
> 要在特定的 pthread 上调度任务，只需像平常一样使用 event.c 中的常规函数，并提供由 frr_pthread 指向的 threadmaster 即可。……这构成了线程间通信的基础，其本质是一种稍显复杂的消息传递方式，其中消息即为普通的任务事件。

### 1.3 设计边界（官方明确划定的）

[doc/developer/process-architecture.rst:310-328](doc/developer/process-architecture.rst#L310-L328) 明确说明：

> there is not integrated support for other models of pthread use such as **divide and
> conquer**. Similarly, there is no explicit support for **thread pooling** ... The currently
> existing infrastructure is designed around the concept of **long-running worker threads
> responsible for specific jobs within each daemon**.
>
>目前没有集成支持其他 pthread 使用模式，例如分治法。同样，也没有对线程池提供明确的支持……现有的基础设施是围绕长期运行的工作者线程设计的，这些线程负责每个守护进程中特定的任务。

即：**`frr_pthread` 只服务「常驻专职工作线程」，不支持 fork-join 并行、线程池。**
这不是缺陷，是刻意的取舍——因为 FRR 的 99% 代码假设单线程。

---

## 2. 数据结构详解

### 2.1 `struct frr_pthread`：胶水对象

定义在 [lib/frr_pthread.h:73-141](lib/frr_pthread.h#L73-L141)。分成五组看：

```c
struct frr_pthread {
    /* ① 锁 */
    pthread_mutex_t mtx;          // 保护本结构；标注 "Requires: mtx" 的字段须持锁访问

    /* ② 线程标识 */
    pthread_t thread;             // 底层 pthread（stop 后会被 memset 清零）
    struct rcu_thread *rcu_thread;// 该线程在 RCU 框架里的注册信息

    /* ③ 事件循环 —— 核心 */
    struct event_loop *master;    // 本线程的事件循环；向他投递任务即在该线程执行

    /* ④ 行为定义 */
    struct frr_pthread_attr attr; // start / stop 回调

    /* ⑤ 同步原语 */
    pthread_cond_t startup_cond;      // 启动握手（内嵌）
    pthread_mutex_t startup_cond_mtx;
    atomic_bool started;

    pthread_cond_t *running_cond;     // 就绪通知（堆分配）
    pthread_mutex_t *running_cond_mtx;
    atomic_bool running;              // 线程是否活着；也是默认事件循环的退出开关

    void *data;                       // 线程私有数据（可当"伪 TLS"传参）
    char *name;                       // 人类可读名：如 "BGP I/O thread"
    char os_name[OS_THREAD_NAMELEN];  // 写入 OS 的名（16 字节含 '\0'）：如 "bgpd_io"
};
```

**几个容易被忽略的细节：**

| 字段 | 细节 |
|------|------|
| `master` | 由 `frr_pthread_new()` **立即创建**（[lib/frr_pthread.c:112](lib/frr_pthread.c#L112)），不是启动时创建。所以 `frr_pthread_new()` 之后就能往 `fpt->master` 上登记事件——zebra dplane 就靠这个在 `frr_pthread_run()` **之前**把首轮事件排好（见 §6.3） |
| `running` | 是 `atomic_bool`，但**语义被复用了两次**：对外表示"线程已就绪且活着"，对内是 `fpt_run` 主循环的继续条件。自定义 start 函数必须自己维护它（见 §6.2、§7 陷阱 6） |
| `data` | 注释说是 "Fake thread-specific storage"，实际用法见 pathd：`set_ctrl_state()` 把控制器状态塞进 `fpt->data`（[pathd/path_pcep_controller.c:989-1004](pathd/path_pcep_controller.c#L989-L1004)） |
| `startup_cond` vs `running_cond` | 前者是**内嵌**的，后者是**堆分配**的。这个不对称纯属历史原因，没有语义差别 |
| `name` vs `os_name` | `name` 出现在 `show event cpu` 的输出里；`os_name` 是 `pthread_setname_np()` 的 16 字节上限。给两个参数就是为了绕过这个长度限制 |

### 2.2 `struct frr_pthread_attr`：唯一的行为定制点

```c
struct frr_pthread_attr {           // lib/frr_pthread.h:53-56
    void *(*start)(void *);         // 线程入口，返回值即线程结果
    int (*stop)(struct frr_pthread *, void **);  // 请求停止，结果回填到 *result
};
```

默认值（[lib/frr_pthread.c:55-58](lib/frr_pthread.c#L55-L58)）：

```c
const struct frr_pthread_attr frr_pthread_attr_default = {
    .start = fpt_run,    // 跑一个基础事件循环
    .stop  = fpt_halt,   // 投毒丸 + pthread_join
};
```

> ⚠️ 顺带指出一处**源码注释与实现不符**：[lib/frr_pthread.h:143](lib/frr_pthread.h#L143) 的中文注释写
> "默认属性（默认 start / stop 为 NULL）"，但实际默认的 start/stop 是 `fpt_run`/`fpt_halt`，
> 不是 NULL。仅注释有误，代码是对的。

**`attr = NULL` 时自动用默认属性**（[lib/frr_pthread.c:106](lib/frr_pthread.c#L106)），
所以 `frr_pthread_new(NULL, name, os_name)` 是合法且常见的写法（zebra FPM 就这么写）。

### 2.3 三者关系图

```
   struct frr_pthread
   ┌─────────────────────────────┐
   │ pthread_t thread ───────────┼──► 内核线程实体
   │ struct rcu_thread *rcu_thread┼──► RCU 框架里的"我"（depth/epoch）
   │ struct event_loop *master ──┼──► 我专属的事件循环
   │ atomic_bool running         │    （跨线程投递的目标）
   └─────────────────────────────┘
```

官方对 `struct event_loop` 的描述（[lib/event.c:153-178](lib/event.c#L153-L178)）：
每个循环自带 poll/epoll 句柄、定时器最小堆、三条事件链表（`event`/`ready`/`unuse`）、
**自唤醒管道 `io_pipe[2]`**、以及 **`owner`（归属 pthread）**。
最后这两样是跨线程通信的关键，见 §4。

---

## 3. 生命周期全流程（核心章节）

### 3.0 六步速查

```c
/* 1. 模块初始化（进程启动时一次，libfrr.c 已做） */
frr_pthread_init();

/* 2. 创建对象（不启动线程；内部已建好 event_loop） */
struct frr_pthread_attr attr = { .start = ..., .stop = ... };
struct frr_pthread *fpt = frr_pthread_new(&attr, "My thread", "my_thr");

/* 3. 登记事件（可选：可以在启动前先把首轮事件排好） */
event_add_event(fpt->master, my_handler, NULL, 0, NULL);

/* 4. 启动线程 */
frr_pthread_run(fpt, NULL);

/* 5. 等待线程就绪（如果对端用了握手，必须等） */
frr_pthread_wait_running(fpt);

/* ... 运行期：通过 fpt->master 投递事件与它通信 ... */

/* 6. 停止 + 销毁 */
frr_pthread_stop(fpt, NULL);
frr_pthread_destroy(fpt);
```

### 3.1 `frr_pthread_new()` —— 只创建，不启动

实现：[lib/frr_pthread.c:101-139](lib/frr_pthread.c#L101-L139)

```c
attr = attr ? attr : &frr_pthread_attr_default;

fpt = XCALLOC(MTYPE_FRR_PTHREAD, sizeof(struct frr_pthread));
pthread_mutex_init(&fpt->mtx, NULL);
fpt->master = event_master_create(name);     // ← 关键：事件循环在此创建
fpt->attr = *attr;                            // 结构体浅拷贝
name = (name ? name : "Anonymous thread");
fpt->name = XSTRDUP(MTYPE_FRR_PTHREAD, name);
if (os_name) strlcpy(fpt->os_name, os_name, OS_THREAD_NAMELEN);
else         strlcpy(fpt->os_name, name,     OS_THREAD_NAMELEN);

fpt->running_cond_mtx = XCALLOC(MTYPE_PTHREAD_PRIM, sizeof(pthread_mutex_t));
fpt->running_cond     = XCALLOC(MTYPE_PTHREAD_PRIM, sizeof(pthread_cond_t));
pthread_mutex_init(fpt->running_cond_mtx, NULL);
pthread_cond_init(fpt->running_cond, NULL);

pthread_mutex_init(&fpt->startup_cond_mtx, NULL);
pthread_cond_init(&fpt->startup_cond, NULL);
fpt->started = false;

frr_with_mutex (&frr_pthread_list_mtx) {     // 登记进全局链表
    listnode_add(frr_pthread_list, fpt);
}
```

**要点：**

1. **`event_master_create(name)` 拿到的 name 可能是 NULL**。`event_master_create()` 内部
   把 NULL 替换成 `"default"`（[lib/event.c:892](lib/event.c#L892)），
   而 `fpt->name` 会被替换成 `"Anonymous thread"`。于是**同一个线程会出现两个不同的名字**：
   `show event cpu` 标题显示 `default`，而 `frrtrace`/日志显示 `Anonymous thread`。
   调试时别被绕晕——**永远显式传 name**。

2. **`fpt->master` 在此时就已存在**。这是「可以先投递事件再启动线程」的原因，
   zebra dplane / opaque / mlag 三个线程全部依赖这个性质。

3. **登记进全局链表** `frr_pthread_list`（用 `frr_pthread_list_mtx` 保护）。
   这张链表是 `frr_pthread_stop_all()` / `frr_pthread_finish()` 的遍历对象。

### 3.2 `frr_pthread_run()` —— 启动，含四道保险

实现：[lib/frr_pthread.c:233-266](lib/frr_pthread.c#L233-L266)

```c
assert(frr_is_after_fork || !"trying to start thread before fork()");

sigemptyset(&blocksigs);
frr_sigset_add_mainonly(&blocksigs);
pthread_sigmask(SIG_BLOCK, &blocksigs, &oldsigs);   // ① 屏蔽"主线程专属"信号

fpt->rcu_thread = rcu_thread_prepare();             // ② 预备 RCU 注册信息
ret = pthread_create(&fpt->thread, attr, frr_pthread_inner, fpt);  // ③ 建线程

pthread_sigmask(SIG_SETMASK, &oldsigs, NULL);       // ④ 恢复调用者的信号掩码
frr_pthread_notify_startup(fpt);                    // ⑤ 放行子线程

if (ret < 0) {                                      // ⑥ 失败清理（见下方注）
    rcu_thread_unprepare(fpt->rcu_thread);
    memset(&fpt->thread, 0x00, sizeof(fpt->thread));
}
return ret;
```

按序拆解每一道的意图：

#### ① 为什么要屏蔽信号？

新线程**继承**调用者的信号掩码。如果不屏蔽，那么当内核把 SIGHUP 投递给
「进程里任意一个没屏蔽它的线程」时，SIGHUP 可能落到工作线程上——
而 `frr_sigevent_process()` 只在主循环里跑，工作线程的循环明确设置了
`frr_event_loop_set_handle_sigs(fpt->master, false)`。

`frr_sigset_add_mainonly()`（[lib/sigevent.h:124-140](lib/sigevent.h#L124-L140)）把
**应用级信号**加入屏蔽集：

```c
/* signals we actively handle */
sigaddset(blocksigs, SIGHUP);  SIGINT;  SIGTERM;  SIGUSR1;
/* signals we don't actively use but that semantically belong */
SIGUSR2; SIGQUIT; SIGCHLD; SIGPIPE; SIGTSTP; SIGTTIN; SIGTTOU; SIGWINCH;
```

> ✅ **SIGSEGV / SIGABRT 等致命信号故意不在此列**——源码注释说得很清楚：
> "NB: `SIGSEGV`, `SIGABRT`, etc. must be allowed on all threads or we get no crashlogs."
> 即任意线程崩溃都要能生成 core / crashlog。

调用者自己的掩码随后用 `SIG_SETMASK` + 之前保存的 `oldsigs` 恢复——
注意 `pthread_sigmask()` 的第三参数在同一次调用里就存好了旧掩码，**不需要预先取一次**。

#### ② `rcu_thread_prepare()` —— RCU 注册

`rcu_thread_prepare()`（[lib/frrcu.c:259-272](lib/frrcu.c#L259-L272)）做三件事：

```c
rcu_assert_read_locked();        // 断言：调用者必须持有 RCU 读锁
if (!rcu_active) rcu_start();    // 首次调用时拉起 RCU 回收线程
cur = rcu_self();
return rcu_thread_new(cur);      // 基于父线程的 epoch 建新线程的 rcu_thread
```

**为什么调用者"总是"持有读锁？** 因为主线程在构造期就被设了 `depth = 1`：
`rcu_preinit()` 带 `__attribute__((constructor))`，进程启动时自动执行，
把 `rcu_thread_main.depth` 置 1 并永久持有（[lib/frrcu.c:203-224](lib/frrcu.c#L203-L224)）。
所以主线程里调 `frr_pthread_run()` 一定能过断言。

**为什么新线程要继承父线程的 epoch？** `rcu_thread_new()` 里有注释
（[lib/frrcu.c:234-250](lib/frrcu.c#L234-L250)）：

> new thread always starts with `rcu_read_lock` held at depth 1, and holding the
> **same epoch as the parent** (this makes it possible to use RCU for things passed
> into the thread through its arg)
>
>新线程总是以 rcu_read_lock 保持在深度 1 开始，并且持有与父线程相同的 epoch（这使得可以对通过参数传入线程的东西使用 RCU）

即：父线程在 `frr_pthread_new()` 之后、`frr_pthread_run()` 之前塞进 `fpt->data`
的 RCU 保护对象，可以安全地被新线程读取——因为两者共享同一个 epoch，宽限期还没推进。

#### ③ `pthread_create(frr_pthread_inner, fpt)`

**入口不是用户的 `start`，而是中间层 `frr_pthread_inner`**（见 §3.3）。

#### ⑤ `frr_pthread_notify_startup()` —— 为什么要有这一步？

这是整个机制里最容易被忽略、但最重要的一环。

**问题**：POSIX 不保证 `pthread_create()` 返回后，新线程和创建线程谁先跑。
如果新线程抢跑，它可能：

- 在 `frr_pthread_run()` **还没执行完**时就开始跑事件循环；
- 此时 `fpt->thread` 甚至还没赋值；
- 信号掩码还没恢复；
- 更糟：新线程可能在 `frr_pthread_new()` 之后的父线程操作（比如继续填 `fpt->data`）
  之前，就把 `fpt->data` 读走了。

**解法**（头文件注释 [lib/frr_pthread.h:92-101](lib/frr_pthread.h#L92-L101) 的原话）：

> Startup serialization: newly-started pthreads wait at a point very early in life
> so that there isn't a race with the starting pthread. The OS 'start' apis don't
> make any guarantees about which pthread runs first.
>
>启动序列化：新启动的 pthread 会在程序运行初期的某个点等待，以避免与启动 pthread 发生竞争。操作系统的 'start' 接口并不能保证哪个 pthread 会先运行。

于是新线程在入口最开头就**卡住**，等父线程放行：

```c
static void frr_pthread_wait_startup(struct frr_pthread *fpt) {   // c:193-200
    frr_with_mutex (&fpt->startup_cond_mtx) {
        while (!fpt->started)
            pthread_cond_wait(&fpt->startup_cond, &fpt->startup_cond_mtx);
    }
}
static void frr_pthread_notify_startup(struct frr_pthread *fpt) {  // c:204-210
    frr_with_mutex (&fpt->startup_cond_mtx) {
        fpt->started = true;
        pthread_cond_signal(&fpt->startup_cond);
    }
}
```

注意两点：

- 父线程在 `pthread_create()` **之后**（不管成功失败）都会调用 `notify_startup`。
  失败时没人等这个条件变量，`signal` 是空操作，无害。
- 用的是 `while (!started)` 而非 `if`——标准条件变量谓词循环写法，防虚假唤醒。

#### ⑥ 一处值得注意的**疑似残留代码**

```c
if (ret < 0) { ... }
```

`pthread_create(3)` 的返回值语义是：**成功返回 0，失败返回正的错误码**
（如 `EAGAIN` = 11）。它**不返回负数**。所以这个 `ret < 0` 分支在 Linux/glibc 上
**永远不会进入**，意味着：

- `rcu_thread_unprepare()` 不会被调用；
- `fpt->thread` 不会被清零。

而头文件 [lib/frr_pthread.h:230-232](lib/frr_pthread.h#L230-L232) 却承诺
> "If the value is less than zero, the provided frr_pthread is guaranteed to be a clean
> instance that may be subsequently passed to frr_pthread_run()"。
> 
> 如果值小于零，提供的 frr_pthread 就保证是一个干净的实例，可以随后传给 frr_pthread_run() 使用。

**结论**：`pthread_create` 失败（线程数超限等）时，当前的清理路径不可靠。
实际影响很小（FRR 每个守护进程只建个位数线程，几乎不可能失败），
但如果你要写「失败后重试 `frr_pthread_run`」的代码，别指望这个分支。

### 3.3 `frr_pthread_inner()` 与默认循环 `fpt_run()`

#### 中间层：`frr_pthread_inner`（[lib/frr_pthread.c:215-225](lib/frr_pthread.c#L215-L225)）

```c
static void *frr_pthread_inner(void *arg)
{
    struct frr_pthread *fpt = arg;

    frr_pthread_wait_startup(fpt);   // ① 等父线程放行（防竞争）
    rcu_thread_start(fpt->rcu_thread);// ② 把预建的 rcu_thread 绑到本线程 TLS
    return fpt->attr.start(fpt);      // ③ 调用真正的线程体
}
```

**为什么 `rcu_thread_prepare()` 必须在父线程、`rcu_thread_start()` 必须在本线程？**
因为 `rcu_thread` 要挂进全局链表并继承父线程 epoch（父线程做），
但 TLS 绑定只能由线程自己完成（子线程做）。这是标准的两段式初始化。

#### 默认线程体：`fpt_run`（[lib/frr_pthread.c:427-458](lib/frr_pthread.c#L427-L458)）

```c
static void *fpt_run(void *arg)
{
    struct frr_pthread *fpt = arg;

    frr_event_loop_set_pthread_owner(fpt->master, pthread_self());  // ① 认领循环

    zlog_tls_buffer_init();                                         // ② 线程局部日志缓冲

    int sleeper[2];
    pipe(sleeper);                                                  // ③ "sleeper 管道"
    event_add_read(fpt->master, &fpt_dummy, NULL, sleeper[0], NULL);

    frr_event_loop_set_handle_sigs(fpt->master, false);             // ④ 本循环不处理信号
    frr_pthread_set_name(fpt);                                      // ⑤ 设置 OS 线程名
    frr_pthread_notify_running(fpt);                                // ⑥ 宣告就绪

    struct event task;
    while (atomic_load_explicit(&fpt->running, memory_order_relaxed)) {
        pthread_testcancel();                                       // ⑦ 响应取消点
        if (event_fetch(fpt->master, &task))
            event_call(&task);
    }

    close(sleeper[1]);
    close(sleeper[0]);
    zlog_tls_buffer_fini();
    return NULL;
}
```

逐点说明：

**① `set_pthread_owner` —— 这是跨线程取消能正常工作的前提。**
`event_cancel()` 里有 `assert(master->owner == pthread_self())`
（[lib/event.c:2213](lib/event.c#L2213)）；`event_cancel_async()` 反过来
`assert(master->owner != pthread_self())`（[lib/event.c:2264](lib/event.c#L2264)）。
所以「谁是 owner」必须准确无误。自定义 start 函数**也必须**调用这一句——
`bgp_keepalives_start()` 第一行就是它。

**③ sleeper 管道：为什么要有它？**

这是最容易被误读的一段。`pipe(sleeper)` 的**写端 `sleeper[1]` 从来没有人写过**，
只在退出时被 `close()`。它的唯一作用是把读端挂成一个 I/O 事件源，让
`m->handler.pfdcount >= 1`。

为什么需要？看 `event_fetch()` 的「无事可做就死」分支
（[lib/event.c:2774-2779](lib/event.c#L2774-L2779)）：

```c
if (!tw && m->handler.pfdcount == 0) { /* die */
    pthread_mutex_unlock(&m->mtx);
    fetch = NULL;
    *broken = true;
    return;
}
```

如果循环里**既没有定时器、也没有任何 fd 事件**，`event_fetch()` 会返回 NULL。
`fpt_run` 里的 `if (event_fetch(...))` 于是跳过 `event_call`，
**但 `while (running)` 依然成立 → 立刻下一轮 → 空转 100% CPU**。

挂上 sleeper 管道后，poll 永远有东西可等，会**无限阻塞**直到有事件（或被 `io_pipe` 唤醒）。
所以：

> **sleeper 管道不是「睡眠」用的，是「防止忙等」用的占位 fd。**
> 真正的唤醒通道是事件循环自带的 `io_pipe`（见 §4.1），不是它。

> ⚠️ 另外注意：`pipe()` 的返回值没有检查，`event_add_read(..., sleeper[0], NULL)`
> 的 `tref` 也传了 NULL。这属于「unlikely 失败路径不处理」的常见 FRR 风格。

**④ `handle_sigs = false`** —— 工作线程不做 `frr_sigevent_process()`。
配合 §3.2 ① 的信号屏蔽，构成「信号只在主线程处理」的双保险。
（`event_fetch_inner_loop` 第一件事就是 `if (m->handle_signals) frr_sigevent_process();`
见 [lib/event.c:2710-2711](lib/event.c#L2710-L2711)。）

**⑥ `notify_running` —— 启动握手第二阶段。**

第一阶段的 `startup_cond` 解决的是「子线程别抢跑」；
第二阶段的 `running_cond` 解决的是**反方向**「父线程别抢跑」：

头文件注释 [lib/frr_pthread.h:103-118](lib/frr_pthread.h#L103-L118)：

> - `frr_pthread_wait_running()` — This function should be called by the **spawning thread**
>   after `frr_pthread_run()`. It safely waits until the spawned thread indicates that is
>   ready to do work.
>
> frr_pthread_wait_running() — 这个函数应该在调用 frr_pthread_run() 之后由创建线程的线程调用。它会安全地等待，直到被创建的线程表示已经准备好开始工作。
>
> - `frr_pthread_notify_running()` — This function should be called by the **spawned thread**
>   when it is ready to do work.
>
> frr_pthread_notify_running() — 当新建的线程准备好开始工作时，应调用此函数。
> 

**两道握手的对照表：**

| | 触发者 | 等待者 | 解决什么竞争 | 条件变量 |
|---|---|---|---|---|
| startup | 父线程 | 子线程 | 子线程抢在父线程完成 `run()` 之前跑 | `startup_cond` |
| running | 子线程 | 父线程 | 父线程在子线程自初始化完成前就开始投递/使用 | `running_cond` |

**`notify_running` 的位置很讲究**：它在「owner 已认领、日志缓冲已建、信号策略已设、
线程名已设」之后。所以父线程从 `wait_running()` 返回时，可以确信这些**都已生效**，
正如源码注释所说 [lib/frr_pthread.c:406-411](lib/frr_pthread.c#L406-L411)：

> The `frr_pthread->running_cond` condition variable is signalled to indicate that the
> previous actions have completed. **It is not safe to assume any of the above have occurred
> before receiving this signal.**
>
>frr_pthread->running_cond 条件变量会被触发，以表明之前的操作已经完成。在收到这个信号之前，不能安全地假设上面提到的任何操作已经发生。

**⑦ `pthread_testcancel()`** 让线程能被 `pthread_cancel()` 取消（FRR 极少用，但保留）。

**退出时不跑任何 cleanup 回调**，源码注释明说
（[lib/frr_pthread.c:424-425](lib/frr_pthread.c#L424-L425)）：
> the event loop exits, closes the pipes and dies without running any cleanup functions.
>
>事件循环退出，关闭管道，然后在没有运行任何清理函数的情况下结束。

所以**需要清理的资源必须挂到别的地方**（自定义 stop / `pthread_cleanup_push` /
注册一个在退出前跑的事件）。

### 3.4 停止：毒丸（poison pill）机制

默认 stop 函数 `fpt_halt`（[lib/frr_pthread.c:386-392](lib/frr_pthread.c#L386-L392)）：

```c
static int fpt_halt(struct frr_pthread *fpt, void **res)
{
    event_add_event(fpt->master, &fpt_finish, fpt, 0, NULL);  // 投毒丸
    pthread_join(fpt->thread, res);                            // 等它自己死
    return 0;
}
static void fpt_finish(struct event *event) {                  // c:375-380
    struct frr_pthread *fpt = EVENT_ARG(event);
    atomic_store_explicit(&fpt->running, false, memory_order_relaxed);
}
```

**为什么叫「毒丸」？** 因为 `fpt_run` 的 `while (running)` 是唯一的退出判据。
往目标 loop 投一个「把 running 置 false」的事件，主循环处理完它就自然退出。
这是 actor 模型里最经典的优雅停止手法：**不打断线程，只是告诉它"该收工了"**。

它相对「`pthread_cancel()` 强杀」的优势：

| | 毒丸 | pthread_cancel |
|---|---|---|
| 停在何处 | 停在事件边界，状态一致 | 停在任意点（有 cancellation point 才行） |
| 锁 | 天然安全 | 可能持锁而死 |
| 清理 | `running` 循环退出后走后续代码 | 需 `pthread_cleanup_push/pop` 配合 |

**调用链**：`frr_pthread_stop()`（[lib/frr_pthread.c:290-297](lib/frr_pthread.c#L290-L297)）：

```c
int frr_pthread_stop(struct frr_pthread *fpt, void **result)
{
    frrtrace(1, frr_libfrr, frr_pthread_stop, fpt->name);
    int ret = (*fpt->attr.stop)(fpt, result);       // 多态：默认 = fpt_halt
    memset(&fpt->thread, 0x00, sizeof(fpt->thread));// 标记"已无绑定的 pthread"
    return ret;
}
```

`frr_pthread_stop()` 本身**不做任何同步**——同步完全由 `attr.stop` 负责。
默认实现靠 `pthread_join`；自定义实现也必须保证「返回时线程已终止」。

### 3.5 销毁与全局收尾

```c
void frr_pthread_destroy(struct frr_pthread *fpt)          // c:162-169
{
    frr_with_mutex (&frr_pthread_list_mtx) { listnode_delete(frr_pthread_list, fpt); }
    frr_pthread_destroy_nolock(fpt);   // event_master_free + 销毁各同步原语 + 释放内存
}
```

**前置条件（必须记住）**：头文件 [lib/frr_pthread.h:203-208](lib/frr_pthread.h#L203-L208)：
> Assumes that the associated pthread, if any, **has already terminated**.

因为 `frr_pthread_destroy_nolock()` 会调用 `event_master_free(fpt->master)`
销毁事件循环——如果线程还在跑着 `event_fetch(fpt->master, ...)`，就是标准的
use-after-free。

**全局收尾**（[lib/frr_pthread.c:78-93](lib/frr_pthread.c#L78-L93)）：

```c
void frr_pthread_finish(void)
{
    frr_pthread_stop_all();                       // ① 先停
    frr_with_mutex (&frr_pthread_list_mtx) {
        for (ALL_LIST_ELEMENTS(frr_pthread_list, n, nn, fpt)) {
            listnode_delete(frr_pthread_list, fpt);
            frr_pthread_destroy_nolock(fpt);      // ② 再逐个销毁
        }
        list_delete(&frr_pthread_list);
    }
}
```

调用点：`frr_fini()` → [lib/libfrr.c:1427](lib/libfrr.c#L1427)。
**注意顺序**：`frr_pthread_finish()` 在 `event_master_free(master)`（主循环）
**之前**执行——工作线程先死，主循环后死，符合依赖方向。

`frr_pthread_stop_all()`（[lib/frr_pthread.c:300-311](lib/frr_pthread.c#L300-L311)）：

```c
frr_with_mutex (&frr_pthread_list_mtx) {
    for (ALL_LIST_ELEMENTS_RO(frr_pthread_list, n, fpt)) {
        if (atomic_load_explicit(&fpt->running, memory_order_relaxed))
            frr_pthread_stop(fpt, NULL);
    }
}
return;
```

**两个隐藏陷阱**：

1. **`running == false` 的线程会被静默跳过**。
   如果某个自定义 start 从来没调用 `frr_pthread_notify_running()`
   （比如 lib/northbound_grpc.cpp 的 gRPC 线程），它的 `running` 永远是 false，
   `stop_all` 就不会去 `pthread_join` 它——**线程会活到进程结束**。
   这正是 gRPC 把 `.stop` 设为 NULL 却不会崩的原因。
2. **`stop_all` 持着全局链表锁去 `join` 线程**。
   如果被停的线程在退出过程中试图 `frr_pthread_new()`（要拿同一把锁）→ 死锁。

### 3.6 完整生命周期时序图

```
主线程 (spawning)                          新线程 (spawned)
    │                                            │
    │ frr_pthread_new()                          │
    │   ├ XCALLOC fpt                            │
    │   ├ event_master_create()  ← loop 就绪     │
    │   └ listnode_add(全局链表)                 │
    │                                            │
    │ frr_pthread_run()                          │
    │   ├ assert(frr_is_after_fork)              │
    │   ├ 屏蔽 main-only 信号                    │
    │   ├ rcu_thread_prepare()                   │
    │   ├ pthread_create() ─────────────────────►│ 进入 frr_pthread_inner
    │   ├ 恢复信号掩码                           │
    │   └ notify_startup() ─────┐                │ frr_pthread_wait_startup()
    │                           │                │   …阻塞在 startup_cond…
    │                           └───────────────►│  醒来
    │                                            │ rcu_thread_start()
    │                                            │ fpt_run():
    │                                            │   set_pthread_owner()
    │                                            │   zlog_tls_buffer_init()
    │                                            │   pipe(sleeper) + add_read
    │                                            │   handle_sigs = false
    │                                            │   set_name()
    │ frr_pthread_wait_running() ◄───notify_running()───┘
    │   …阻塞在 running_cond…                    │
    │                                            │ ┌─► while (running):
    │ event_add_event(fpt->master, ...) ────────►│ │     pthread_testcancel()
    │                                            │ │     event_fetch() + event_call()
    │                                            │ └─┐
    │ frr_pthread_stop()                         │   │
    │   event_add_event(fpt->master,fpt_finish) ►│───┘  running := false
    │   pthread_join() ………………阻塞………………          │ 循环退出
    │   ………………………………………………………………◄│ close(sleeper); tls_buffer_fini()
    │   memset(fpt->thread, 0)                   │ return NULL
    │                                            ✗
    │ frr_pthread_destroy()                      │
    │   └ event_master_free() / 释放 fpt         │
    ▼                                            ▼
```

---

## 4. 跨线程通信：正道只有一条

### 4.1 `event_add_event()` —— 唯一的通用投递方式

`_event_add_event()`（[lib/event.c:1694-1722](lib/event.c#L1694-L1722)）：

```c
frr_with_mutex (&m->mtx) {                     // 拿目标循环的锁
    if (t_ptr && *t_ptr) break;                // 已经排过队了，不重复排
    event = event_get(m, EVENT_EVENT, func, arg, xref);
    frr_with_mutex (&event->mtx) {
        event->u.val = val;
        event_list_add_tail(&m->event, event);
    }
    if (t_ptr) { *t_ptr = event; event->ref = t_ptr; }
    AWAKEN(m);                                 // 叫醒可能在 poll 里睡觉的目标线程
}
```

`AWAKEN` 宏（[lib/event.c:246-251](lib/event.c#L246-L251)）：

```c
#define AWAKEN(m)                                        \
    do {                                                 \
        const unsigned char wakebyte = 0x01;             \
        write(m->io_pipe[1], &wakebyte, 1);              \
    } while (0)
```

**完整链路**：

```
线程 A                                   线程 B（owner of loop M）
  │                                          │
  │ event_add_event(M, fn, arg, 0, tref)     │
  │   ├ lock(M->mtx)                         │
  │   ├ 从 M->unuse 取/造一个 struct event    │
  │   ├ 挂到 M->event 链表尾部                │
  │   └ AWAKEN: write(M->io_pipe[1])         │
  │                                          │  epoll_wait/poll 因 io_pipe[0] 可读而返回
  │                                          │  fd_poll(): 读空 io_pipe[0]（清空唤醒字节）
  │                                          │  thread_process(&m->event) → 事件转入 ready
  │                                          │  event_fetch() 取出 → event_call(fn) 在这边执行
```

**关键性质：**

| 性质 | 说明 |
|------|------|
| **任意线程可调用** | 文档原话：safe to schedule events on a threadmaster belonging both to the calling thread as well as *any other pthread 在一个既属于调用线程又属于其他任何 pthread 的 threadmaster 上安排事件是安全的* |
| **投递是异步的** | 返回时任务只是"排上队"，**尚未执行** |
| **执行者是 owner** | 回调总在目标线程上下文里跑，所以回调内部无需加锁访问目标线程私有数据 |
| **可去重** | 传 `tref` 且 `*tref` 非空时直接跳过（`if (t_ptr && *t_ptr) break;`），避免重复排队 |

`io_pipe` 在 poll 版本里是临时塞进 `pollfd` 数组的最后一项
（[lib/event.c:1267-1270](lib/event.c#L1267-L1270)），在 epoll 版本里则早就在
`event_master_create()` 时 `EPOLL_CTL_ADD` 进去了
（[lib/event.c:952-961](lib/event.c#L952-L961)）。两条路径都会在 `fd_poll()`
开头把管道读空（[lib/event.c:1362](lib/event.c#L1362)）：
`while (read(m->io_pipe[0], &trash, sizeof(trash)) > 0)`。

### 4.2 取消：`event_cancel` vs `event_cancel_async`

这是**唯一**必须区分「同线程 / 跨线程」的 API：

```c
void event_cancel(struct event **event)             // lib/event.c:2200-2224
{
    ...
    assert(master->owner == pthread_self());        // ← 必须是自己拥有的 loop
    frr_with_mutex (&master->mtx) {
        struct cancel_req *cr = XCALLOC(MTYPE_TMP, sizeof(*cr));
        cr->event = *event;
        listnode_add(master->cancel_req, cr);
        do_event_cancel(master);                    // 就地取消，同步完成
        *event = NULL;
    }
}

void event_cancel_async(struct event_loop *master, struct event **thread, void *eventobj)
{
    assert(!(thread && eventobj) && (thread || eventobj));  // 两者互斥
    assert(master->owner != pthread_self());                // ← 必须是"别人的" loop
    frr_with_mutex (&master->mtx) {
        master->canceled = false;
        if (thread) { ...创建 cancel_req... }
        else if (eventobj) { ...按 arg 取消... }
        AWAKEN(master);
        while (!master->canceled)                            // ← 阻塞等对方处理完
            pthread_cond_wait(&master->cancel_cond, &master->mtx);
    }
    if (thread) *thread = NULL;
}
```

**为什么需要异步版本？** 两个原因（头文件与官方文档都提到）：

1. **数据竞争**：`struct event` 对象归目标 loop 所有。跨线程直接摘链会撕裂链表。
2. **语义保证**：调用者往往需要「确保那个事件**已经**不会再执行了」之后才能继续
   （比如释放 `event->arg` 指向的内存）。所以 `event_cancel_async` **阻塞等待**
   目标线程处理完取消请求才返回——注释原话：

   > This is necessary to avoid race conditions in the specific case where one pthread wants to guarantee that a task on another pthread is cancelled before proceeding.
   > 
   > 这是必要的，以避免在这种特定情况下出现竞争条件，即一个 pthread 想确保另一个 pthread 上的任务在继续之前被取消。

**实战写法**（pathd 给出的「自适应」范例，
[pathd/path_pcep_controller.c:400-404](pathd/path_pcep_controller.c#L400-L404)）：

```c
if (frr_event_loop_get_pthread_owner((*thread)->master) == pthread_self())
    event_cancel(thread);                              // 自己的 loop
else
    event_cancel_async((*thread)->master, thread, NULL); // 别人的 loop
```

bgpd 的用法则是无条件的异步取消
（[bgpd/bgp_io.c:155](bgpd/bgp_io.c#L155)、[bgpd/bgp_io.c:196](bgpd/bgp_io.c#L196)）：
主线程要关掉某连接的读/写事件时，直接
`event_cancel_async(bgp_pth_io->master, &connection->t_write, NULL);`。

### 4.3 共享数据的三种保护手段

FRR 代码里能看到的全部手法（按推荐度排序）：

| 手段 | 适用 | 实例 |
|------|------|------|
| **① 事件投递（不共享）** | 首选。数据所有权交给某个线程，其他线程只投事件 | bgp_io 把整包报文入队后 `event_add_event(bm->master, bgp_process_packet, ...)`（[bgpd/bgp_io.c:484](bgpd/bgp_io.c#L484)） |
| **② 互斥量 + `frr_with_mutex`** | 必须共享的容器（队列、哈希表） | bgpd：`connection->io_mtx` 护 obuf/ibuf；`bm->peer_connection_mtx` 护连接 FIFO。zebra dplane：`DPLANE_LOCK()` 护 `dg_update_list` |
| **③ 原子变量** | 单个标量状态/计数 | 各处 `atomic_load/store(&fpt->running, memory_order_relaxed)`；`dg_routes_queued`；`zo_info.run` |

**推荐的组合套路（bgpd bgp_io 的教科书式写法）**：

```c
/* 工作线程（I/O 线程）里 */
if (added_pkt) {
    frr_with_mutex (&bm->peer_connection_mtx) {            // ② 锁住共享队列
        if (!peer_connection_fifo_member(&bm->connection_fifo, connection))
            peer_connection_fifo_add_tail(&bm->connection_fifo, connection);
    }
    event_add_event(bm->master, bgp_process_packet, NULL, 0, &bm->e_process_packet); // ① 唤醒主线程
}
```

> 「**先加锁入队，再投事件通知**」是 FRR 跨线程协作的标准范式。
> 顺序不能反：先投事件的话，主线程可能醒来发现队列里还是空的。

### 4.4 锁辅助宏：`frr_with_mutex`

定义在 [lib/frr_pthread.h:297-357](lib/frr_pthread.h#L297-L357)，用 GCC 的
`__attribute__((cleanup))` 做 RAII 式自动解锁：

```c
frr_with_mutex (&mtx, &mtx2) {     // 支持一次锁多把（按声明顺序加锁）
    if (error)
        break;                     // break / return 都能正确解锁
    ...
}

frr_mutex_lock_autounlock(&mtx);   // 变体 2：适合长代码块，无额外缩进
...
```

底层就两行（[lib/frr_pthread.h:344-357](lib/frr_pthread.h#L344-L357)）：

```c
static inline pthread_mutex_t *_frr_mtx_lock(pthread_mutex_t *mutex)
{ pthread_mutex_lock(mutex); return mutex; }

static inline void _frr_mtx_unlock(pthread_mutex_t **mutex)
{ if (!*mutex) return; pthread_mutex_unlock(*mutex); *mutex = NULL; }
```

**注意**：用 `break` 跳出块是合法的（宏展开成 `for` 循环），但**不能**用 `continue`
或 `goto` 跳出外层——`break` 只会跳出这个伪循环。锁定多把锁时是**按参数顺序依次
加锁**，所以要么固定顺序，要么接受潜在的锁序反转风险。

---

## 5. 与其它子系统的咬合

### 5.1 信号

| 位置 | 行为 |
|------|------|
| `frr_pthread_run()` | 屏蔽 main-only 信号，子线程继承掩码 |
| `fpt_run()` | `frr_event_loop_set_handle_sigs(master, false)` |
| `event_fetch_inner_loop()` | `if (m->handle_signals) frr_sigevent_process();` — 只有主循环会跑 |
| `frr_sigset_add_mainonly()` | SIGHUP/SIGINT/SIGTERM/SIGUSR1/USR2/QUIT/CHLD/PIPE/TSTP/TTIN/TTOU/WINCH |

**设计意图**：信号处理是天然的进程级语义（SIGHUP=重载配置，SIGTERM=退出），
必须由单一线程串行处理。同时保证 SIGSEGV/SIGABRT 不被屏蔽，以便任意线程崩溃
都能产生 crashlog。

### 5.2 RCU —— 最容易被忽略的耦合

`sleeper` 管道之外，**RCU 是 `frr_pthread` 里第二容易被忽略的机制**。

**核心事实**：`rcu_thread_new()` 让新线程**初始就持有 RCU 读锁（depth = 1）**
（[lib/frrcu.c:234-250](lib/frrcu.c#L234-L250)）。

**默认事件循环怎么解掉它？** 在 `fd_poll()` 里——**准备睡进 poll 之前**：

```c
static int fd_poll(struct event_loop *m, const struct timeval *timer_wait, bool *eintr_p)
{
    ...
    zlog_tls_buffer_flush();          // 顺便把线程局部日志刷出去
    rcu_read_unlock();                // ① 释放读锁，RCU 宽限期才能推进
    rcu_assert_read_unlocked();
    ...
    num = epoll_wait(...) / poll(...); // ② 睡觉（此时不持 RCU 读锁）
    ...
    rcu_read_lock();                  // ③ 醒来重新持有
    return num;
}
```

（[lib/event.c:1263-1264](lib/event.c#L1263-L1264) 与 [lib/event.c:1391](lib/event.c#L1391)）

这解释了**为什么事件循环是 RCU 友好的**：它在阻塞期间不持有读锁，
RCU 后台回收线程（`rcu_main`）才能等到宽限期结束、回收内存。

> ⚠️ **对自定义 start 函数的直接后果**：
> 如果你**不用 `fpt_run`** 而是自己写循环（像 bgp keepalives 那样），
> 那么 **必须手动 `rcu_read_unlock()`**，否则 RCU 宽限期永远无法完成，
> 所有 `rcu_free()` 的内存都不会被回收。`bgp_keepalives_start()` 的第一件事
> 就是它（[bgpd/bgp_keepalives.c:219-224](bgpd/bgp_keepalives.c#L219-L224)），
> 源码注释写得很直白：
>
> > The RCU mechanism for each pthread is initialized in a "locked" state. That's ok for
> > pthreads using the frr_pthread, event_fetch event loop, because that event loop
> > unlocks regularly. For foreign pthreads, the lock needs to be unlocked so that the
> > background rcu pthread can run.

**生命周期对应关系：**

| frr_pthread 侧 | frrcu 侧 | 何时 |
|---|---|---|
| `frr_pthread_run()` | `rcu_thread_prepare()` | 父线程，创建前 |
| `frr_pthread_inner()` | `rcu_thread_start()` | 子线程，入口处绑 TLS |
| `pthread_create` 失败分支 | `rcu_thread_unprepare()` | （当前代码不可达，见 §3.2⑥） |
| 线程退出（TLS 析构） | `rcu_thread_end()` → `rcu_thread_unprepare()` | 由 `pthread_key_create` 的 destructor 自动触发 |

### 5.3 日志 —— 每个线程一块 mmap 缓冲

`fpt_run` 默认会调 `zlog_tls_buffer_init()`
（[lib/frr_pthread.c:432](lib/frr_pthread.c#L432)），退出时 `zlog_tls_buffer_fini()`
（[lib/frr_pthread.c:455](lib/frr_pthread.c#L455)）。

**它做了什么**：在 `/var/tmp/frr/<daemon>.<pid>/` 下 mmap 一个 8KB 文件
`logbuf.<tid>`，日志先写内存（**零 syscall**），
崩溃时内核会把 mmap 的内容回写磁盘，从而保住最后几条日志
（[lib/zlog.c:213-232](lib/zlog.c#L213-L232)）。

**注意**：这是**可选**的。自定义 start 函数若想要就得自己调——
`bgp_keepalives_start()` 就没调，所以 keepalives 线程没有自己的 logbuf 文件。

（更完整的日志机制见 [FRR_IPC_STUDY.md](FRR_IPC_STUDY.md) §7。）

### 5.4 线程名

```c
int frr_pthread_set_name(struct frr_pthread *fpt)   // lib/frr_pthread.c:174-189
{
#ifdef HAVE_PTHREAD_SETNAME_NP
# ifdef GNU_LINUX
    ret = pthread_setname_np(fpt->thread, fpt->os_name);      // Linux：2 参数，最长 15 字符
# elif defined(__NetBSD__)
    ret = pthread_setname_np(fpt->thread, fpt->os_name, NULL);// NetBSD：3 参数
# endif
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
    pthread_set_name_np(fpt->thread, fpt->os_name);           // BSD
#endif
    return ret;
}
```

设置后 `ps -T` / `top -H` / `gdb info threads` 就能看到 `bgpd_io`、`zebra_dplane`
这样的名字——**排查多线程问题时的第一手信息**。

### 5.5 统计与调试

**`show event cpu`** 会**按 loop 分组**输出，因为它是遍历全局 `masters` 链表
（每个 `event_master_create()` 都会把新 loop 挂进去，
[lib/event.c:977-983](lib/event.c#L977-L983)；打印见
[lib/event.c:405-437](lib/event.c#L405-L437)）：

```
frr# show event cpu

Showing statistics for pthread main
------------------------------------
...
Showing statistics for pthread BGP I/O thread
-----------------------------------------------
      Active   Runtime(ms)   Invoked Avg uSec Max uSecs ...
       0       1611.000      9296      173     13000   R  bgp_process_reads
       0       2995.000     11753      254     26000   W  bgp_process_writes

Showing statistics for pthread BGP Keepalives thread
------------------------------------------------------
No data to display yet.        ← 因为它不用事件循环，没有事件可统计
```

（这段输出摘自官方文档 [doc/developer/process-architecture.rst:246-291](doc/developer/process-architecture.rst#L246-L291)）

**这带来一个实用的诊断技巧**：

> 某个工作线程在 `show event cpu` 里「No data to display yet」，
> 说明它**没有走事件循环**（自定义 start），它的卡顿不会体现在事件统计里。
> 反过来，如果一个本该是默认循环的线程没有统计，说明它根本没在跑事件。

其他相关命令（均在 [lib/event.c:521-800](lib/event.c#L521-L800)）：
`show event timers`、`show event poll`、`clear event cpu`、
`service cputime-stats`、`service cputime-warning`。

代码里还有 `frrtrace` 探针（[lib/libfrr_trace.h:148-168](lib/libfrr_trace.h#L148-L168)）：
`frr_pthread_run` / `frr_pthread_stop` 各带一个 `frr_pthread_name` 字段，
可以配合 `tracing.rst` 里的 USDT 机制做 tracepoint。

### 5.6 非受控线程接入：`frr_pthread_non_controlled_startup()`

**场景**：某个线程不是 FRR 建的，而是**第三方库建的**（rtrlib / pceplib / gRPC），
但它**会回调进 FRR**（调用 `zlog_*`、访问 RCU 保护的数据）。

此时该线程的 `rcu_thread_key` TLS 是空的，`rcu_self()` 返回 NULL，
`rcu_read_lock()` 里的 `assert(rt)` 直接崩。

**解法**（[lib/frr_pthread.c:330-354](lib/frr_pthread.c#L330-L354)）：

```c
int frr_pthread_non_controlled_startup(pthread_t thread, const char *name, const char *os_name)
{
    struct rcu_thread *rcu_thread = rcu_thread_new(NULL);
    rcu_thread_start(rcu_thread);

    struct frr_pthread_attr attr = {
        .start = frr_pthread_attr_non_controlled_start,     // 只置 running=true
        .stop  = frr_pthread_attr_default.stop,
    };
    struct frr_pthread *fpt = frr_pthread_new(&attr, name, os_name);
    if (!fpt) return -1;

    fpt->thread  = thread;
    fpt->rcu_thread = rcu_thread;
    fpt->started = true;          // ← 预置为 true，让下面的 wait_startup 直接通过

    frr_pthread_inner(fpt);       // ← 在【当前(外部)线程】上直接跑一遍入口
    return 0;
}
```

**精妙之处**：它把 `started` 预置为 true，于是 `frr_pthread_inner()` 里的
`wait_startup` 不会阻塞；然后 `rcu_thread_start()`（重复绑定，无害）
+ 调用 `frr_pthread_attr_non_controlled_start`（把 `running` 置真）就返回了。
**相当于"手动走一遍子线程的启动流程"**，只为把 RCU TLS 和线程名装上。

**全树唯一调用点**：[bgpd/bgp_rpki.c:1607-1610](bgpd/bgp_rpki.c#L1607-L1610)。
并且带着一段著名的大写警告：

```c
/*
 * IF YOU PUT A ZLOG_XXXX prior to the call into
 * frr_pthread_non_controlled_startup in this function
 * BGP WILL CRASH. You have been warned.
 */
```

**为什么？** 因为在调用它之前，这个线程还没有 RCU 上下文，
任何 `zlog_*` 都可能触碰到未注册的 RCU/线程局部状态而断言失败。
官方文档 [doc/developer/rcu.rst:235-242](doc/developer/rcu.rst#L235-L242) 也收录了这一条。

---

## 6. 真实案例剖析

> 下面全部行号来自本仓库；`bgpd` 相关文件已带中文注释，可直接对照阅读。

### 6.1 案例 A：bgpd 的 I/O 线程（用默认事件循环）

**创建**（[bgpd/bgpd.c:10424-10439](bgpd/bgpd.c#L10424-L10439)）：

```c
static void bgp_pthreads_init(void)
{
    assert(!bgp_pth_io);
    assert(!bgp_pth_ka);

    struct frr_pthread_attr io = {
        .start = frr_pthread_attr_default.start,   // ← 用默认 fpt_run
        .stop  = frr_pthread_attr_default.stop,    // ← 用默认 fpt_halt（毒丸）
    };
    struct frr_pthread_attr ka = {
        .start = bgp_keepalives_start,             // ← 自定义（见 6.2）
        .stop  = bgp_keepalives_stop,
    };
    bgp_pth_io = frr_pthread_new(&io, "BGP I/O thread",      "bgpd_io");
    bgp_pth_ka = frr_pthread_new(&ka, "BGP Keepalives thread", "bgpd_ka");
}

void bgp_pthreads_run(void)      // bgpd/bgp_main.c:633 调用
{
    frr_pthread_run(bgp_pth_io, NULL);
    frr_pthread_run(bgp_pth_ka, NULL);
    frr_pthread_wait_running(bgp_pth_io);   // ← 等双方就绪
    frr_pthread_wait_running(bgp_pth_ka);
}
```

**主线程 ↔ I/O 线程的全部交互**，都在 [bgpd/bgp_io.c](bgpd/bgp_io.c)：

| 方向 | 动作 | 代码 |
|------|------|------|
| 主 → IO | 登记"可写" | `event_add_write(fpt->master, bgp_process_writes, connection, connection->fd, &connection->t_write)` ([:122](bgpd/bgp_io.c#L122)) |
| 主 → IO | 取消"可写" | `event_cancel_async(bgp_pth_io->master, &connection->t_write, NULL)` ([:155](bgpd/bgp_io.c#L155)) |
| 主 → IO | 登记"可读" | `event_add_read(fpt->master, bgp_process_reads, ...)` ([:179](bgpd/bgp_io.c#L179)) |
| 主 → IO | 取消"可读" | `event_cancel_async(fpt->master, &connection->t_read, NULL)` ([:196](bgpd/bgp_io.c#L196)) |
| IO → 主 | 报文收齐，通知消费 | `event_add_event(bm->master, bgp_process_packet, NULL, 0, &bm->e_process_packet)` ([:484](bgpd/bgp_io.c#L484)) |

**该文件头部把并发规则写成了三条**（[bgpd/bgp_io.c:38-46](bgpd/bgp_io.c#L38-L46)）：

1. `connection->io_mtx` 保护 obuf / ibuf_work；
2. 出错不在 I/O 线程改状态机，而是用 `bgp_enqueue_conn_err` / `BGP_EVENT_ADD`
   把事件**投给主线程**；
3. 唤醒主线程一律用 `event_add_event`，**不跨线程直调**。

> 💡 顺带纠正一个常见误解：[bgpd/bgp_io.h:56-78](bgpd/bgp_io.h#L56-L78) 里声明的
> `bgp_io_start` / `bgp_io_stop` 在 10.6.1 **已经废弃不用**（头文件自己也注明了），
> I/O 线程用的是默认 attr。别照着它们去理解代码。

### 6.2 案例 B：bgpd 的 Keepalives 线程（**不用**事件循环）

这是**最有教学价值**的一个：它证明 `frr_pthread` 的壳和事件循环的内核是**可以拆开用**的。

**动机**（官方文档 [doc/developer/process-architecture.rst:293-308](doc/developer/process-architecture.rst#L293-L308) 的原文）：

> This thread ... opts **not** to use the embedded `threadmaster` facilities. Instead it
> replaces the `start` and `stop` functions with custom functions. This was done because
> the `threadmaster` facilities introduce **a small but significant amount of overhead**
> relative to the pthread's task. In this case since the pthread does not need the
> event-driven model and does not need to receive tasks from other pthreads, it is
> simpler and more efficient to implement it outside of the provided event facilities.

翻译：**"不需要收发事件"的纯计算/定时线程，不值得养一个事件循环。**

**自定义 start**（[bgpd/bgp_keepalives.c:209-290](bgpd/bgp_keepalives.c#L209-L290)，节选）：

```c
void *bgp_keepalives_start(void *arg)
{
    struct frr_pthread *fpt = arg;
    frr_event_loop_set_pthread_owner(fpt->master, pthread_self());  // 仍要认领 owner

    rcu_read_unlock();            // ★ 关键：不用事件循环就必须手动释放 RCU 读锁

    peerhash_mtx  = XCALLOC(...); // 自己的同步原语
    peerhash_cond = XCALLOC(...);
    pthread_mutex_init(peerhash_mtx, NULL);
    pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC);   // 用单调时钟防时间跳变
    pthread_cond_init(peerhash_cond, &attrs);

    frr_pthread_set_name(fpt);    // 手工设置线程名（fpt_run 里这句是自动的）
    peerhash = hash_create_size(2048, peer_hash_key, peer_hash_cmp, NULL);

    pthread_mutex_lock(peerhash_mtx);
    pthread_cleanup_push(&bgp_keepalives_finish, NULL);   // 退出清理靠自己挂

    frr_pthread_notify_running(fpt);      // ★ 必须手动通知，否则 wait_running 永久阻塞

    while (atomic_load_explicit(&fpt->running, memory_order_relaxed)) {
        if (peerhash->count > 0)
            pthread_cond_timedwait(peerhash_cond, peerhash_mtx, &next_update_ts);
        else
            while (peerhash->count == 0 &&
                   atomic_load_explicit(&fpt->running, memory_order_relaxed))
                pthread_cond_wait(peerhash_cond, peerhash_mtx);
        ...
        hash_iterate(peerhash, peer_process, &next_update);
    }
    pthread_cleanup_pop(1);
    return NULL;
}
```

**自定义 stop**（[bgpd/bgp_keepalives.c:363-376](bgpd/bgp_keepalives.c#L363-L376)）：

```c
int bgp_keepalives_stop(struct frr_pthread *fpt, void **result)
{
    assert(fpt->running);

    frr_with_mutex (peerhash_mtx) {
        atomic_store_explicit(&fpt->running, false, memory_order_relaxed);
        pthread_cond_signal(peerhash_cond);     // 叫醒 sleep 中的线程
    }
    pthread_join(fpt->thread, result);          // ★ 必须自己 join
    return 0;
}
```

**从默认实现「抄作业」的四件事**（写自定义 start/stop 时的检查表）：

| # | 动作 | 默认实现位置 |
|---|------|--------------|
| 1 | `frr_event_loop_set_pthread_owner(master, pthread_self())` | [lib/frr_pthread.c:430](lib/frr_pthread.c#L430) |
| 2 | 发布 `frr_pthread_notify_running(fpt)` | [lib/frr_pthread.c:442](lib/frr_pthread.c#L442) |
| 3 | 循环条件用 `fpt->running` | [lib/frr_pthread.c:445](lib/frr_pthread.c#L445) |
| 4 | stop 里 `pthread_join(fpt->thread, ...)` | [lib/frr_pthread.c:389](lib/frr_pthread.c#L389) |
| ➕ | 额外：不用事件循环 → 手动 `rcu_read_unlock()` | 对应 [lib/event.c:1263](lib/event.c#L1263) 的自动动作 |
| ➕ | 额外：想要 per-thread logbuf → 手动 `zlog_tls_buffer_init/fini()` | [lib/frr_pthread.c:432/455](lib/frr_pthread.c#L432) |

**主线程怎么跟它交互**？不走事件，走**自己的锁 + 条件变量**
（[bgpd/bgp_keepalives.c:299-360](bgpd/bgp_keepalives.c#L299-L360)）：
`bgp_keepalives_on/off` 在 `peerhash_mtx` 保护下增删 peer，然后
`pthread_cond_signal(peerhash_cond)` 叫醒它。

### 6.3 案例 C：zebra 的五个工作线程

zebra 是**最大用户**，且全部用**默认属性**（没有自定义 start/stop）：

| 线程 | 用途 | os_name | 关键代码 |
|------|------|---------|----------|
| dplane | 内核数据面 | `zebra_dplane` | [zebra/zebra_dplane.c:8510-8607](zebra/zebra_dplane.c#L8510-L8607) |
| opaque | 不透明消息透传 | `zebra_opaque` | [zebra/zebra_opaque.c:141-166](zebra/zebra_opaque.c#L141-L166) |
| mlag | 与 MLAG 守护进程通信 | `Zebra MLAG` | [zebra/zebra_mlag.c:352-370](zebra/zebra_mlag.c#L352-L370) |
| zserv | **每个 ZAPI 客户端一个线程** | `zebra_apic` | [zebra/zserv.c:836-851](zebra/zserv.c#L836-L851) |
| FPM | FPM 插件 | 动态 | [zebra/dplane_fpm_nl.c:1657-1678](zebra/dplane_fpm_nl.c#L1657-L1678) |

**dplane 的启动顺序值得单独看**（[zebra/zebra_dplane.c:8510-8607](zebra/zebra_dplane.c#L8510-L8607)）：

```c
zdplane_info.dg_pthread = frr_pthread_new(&pattr, "Zebra dplane thread", "zebra_dplane");
zdplane_info.dg_master  = zdplane_info.dg_pthread->master;   // ← new 之后 master 就可用了
zdplane_info.dg_run = true;
event_add_event(zdplane_info.dg_master, dplane_thread_loop, NULL, 0,
                &zdplane_info.dg_t_update);                  // ← 启动前先排好首轮事件
... event_add_read(zdplane_info.dg_master, dplane_incoming_read, zi, zi->info.sock, &zi->t_read);
... prov->dp_start(prov);
frr_pthread_run(zdplane_info.dg_pthread, NULL);              // ← 最后才启动
```

**它刻意没用 `frr_pthread_wait_running()`**，而是用自己的 `dg_run` 标志 +
「启动前排事件」来保证就绪。这是**另一种合法的模式**：

| 模式 | 谁用 | 就绪判据 |
|------|------|----------|
| 握手式 | bgpd、pathd | `frr_pthread_wait_running()` 返回 |
| 排异式（先排事件再启动） | zebra dplane / opaque / mlag / zserv、FPM | 自己的 run flag（`dg_run`、`zo_info.run`） |

**跨线程投递示例**（[zebra/zebra_dplane.c:7102-7116](zebra/zebra_dplane.c#L7102-L7116)）：

```c
int dplane_provider_work_ready(void)
{
    /* Note that during zebra startup, we may be offered work before
     * the dataplane pthread (and thread-master) are ready. ... */
    if (zdplane_info.dg_run) {
        event_add_event(zdplane_info.dg_master, dplane_thread_loop, NULL, 0,
                        &zdplane_info.dg_t_update);
    }
    return AOK;
}
```

注意这个 `if (dg_run)` **不是可有可无的**：线程启动前 `dg_run` 为假，
避免向一个「事件循环还没人跑」的 master 上投递（虽然投递本身合法，
但那意味着 zebra 退出时这些事件没人处理）。

**zebra_mlag 里有一段关于"免锁"的注释**（[zebra/zebra_mlag.c:230-244](zebra/zebra_mlag.c#L230-L244)）值得一读：

```c
/*
 * This api will be called from Both main & MLAG Threads.
 * main thread writes, "zrouter.mlag_info.th_master" only
 * during Zebra Init/after MLAG thread is destroyed.
 * so it is safe to use without any locking
 */
event_add_event(zrouter.mlag_info.th_master, zebra_mlag_client_msg_handler, NULL, 0,
                &zrouter.mlag_info.t_write);
```

这是 FRR 里典型的**「靠时序约定免锁」**写法：`th_master` 只在 init 阶段和线程销毁后
被主线程改写，其余时间只读，所以不需要锁。
**代价是脆弱**——任何人在运行期插一句改 `th_master` 就会引入难查的 bug。

**zserv：每客户端一线程**（[zebra/zserv.c:836-851](zebra/zserv.c#L836-L851)）。
这是全树唯一「按连接数动态建线程」的地方：每个连上来的协议守护进程都得到一个
`zebra_apic` 线程和自己的 `ibuf_mtx`/`obuf_mtx`/`stats_mtx`，
关闭时 `frr_pthread_stop()` + `frr_pthread_destroy()`
（[zebra/zserv.c:731-774](zebra/zserv.c#L731-L774)）——
这也说明 **`frr_pthread` 是可以反复创建/销毁的**，不是只能建一次。

### 6.4 案例 D：pathd —— 自定义 stop + 把状态塞进 `fpt->data`

**创建**（[pathd/path_pcep_controller.c:155-201](pathd/path_pcep_controller.c#L155-L201)）：

```c
struct frr_pthread_attr attr = {
    .start = frr_pthread_attr_default.start,   // 用默认事件循环
    .stop  = pcep_ctrl_halt_cb,                // 自定义 stop
};
*fpt = frr_pthread_new(&attr, "PCEP thread", "pcep_controller");
ret = frr_pthread_run(*fpt, NULL);
...
frr_pthread_wait_running(*fpt);
ctrl_state = XCALLOC(MTYPE_PCEP, sizeof(*ctrl_state));
ctrl_state->main = main_thread;          // 记下主线程的 master
ctrl_state->self = (*fpt)->master;       // 记下自己的 master
set_ctrl_state(*fpt, ctrl_state);        // ← 存进 fpt->data
```

**自定义 stop 其实只是抄了默认实现的作业**（[pathd/path_pcep_controller.c:320-327](pathd/path_pcep_controller.c#L320-L327)）：

```c
int pcep_ctrl_halt_cb(struct frr_pthread *fpt, void **res)
{
    event_add_event(fpt->master, pcep_thread_finish_event_handler, (void *)fpt, 0, NULL);
    pthread_join(fpt->thread, res);
    return 0;
}
```

区别只在于：毒丸换成了**一个会做清理工作的处理函数**
`pcep_thread_finish_event_handler`（[:496-516](pathd/path_pcep_controller.c#L496-L516)），
它最后才 `atomic_store_explicit(&fpt->running, false, ...)`。

> 📌 **这是「需要清理时该怎么停线程」的标准答案**：
> 不要用 `fpt_halt`，而是自定义 stop，投递一个"先清理、后置 running=false"的事件。
> 因为默认循环退出后**不跑任何 cleanup**（§3.3）。

**双向通信**：

| 方向 | 函数 | 目标 master |
|------|------|-------------|
| 主 → PCEP | `send_to_thread_with_cb()` [:692-708](pathd/path_pcep_controller.c#L692-L708) | `ctrl_state->self` |
| PCEP → 主 | `send_to_main()` [:956-970](pathd/path_pcep_controller.c#L956-L970) | `ctrl_state->main` |

**pceplib 适配层**（[pathd/path_pcep_lib.c:272-310](pathd/path_pcep_lib.c#L272-L310)）：
pceplib 要求「给我一个 `pthread_create` 的回调」，FRR 就在回调里
`frr_pthread_new()` + `frr_pthread_run()`，并用 `fpt->data` 把参数透传进去——
这就是 `data` 字段注释里说的 "Can be used to pass argument to pthread entry function"。

### 6.5 全树用法对照表

**A. 谁创建了 `frr_pthread`**

| 模块 | 线程 | 事件循环 | 就绪方式 |
|------|------|----------|----------|
| bgpd | BGP I/O thread | ✅ 默认 | `wait_running` |
| bgpd | BGP Keepalives thread | ❌ 自定义 | `wait_running` |
| zebra | dplane / opaque / mlag / zserv(每客户端) | ✅ 默认 | 自己排事件 + run flag |
| zebra | FPM (`dplane_fpm_nl`) | ✅ 默认（attr=NULL） | `assert(frr_pthread_run(...) == 0)` |
| pathd | PCEP controller | ✅ 默认 | `wait_running` |
| pathd | pceplib 线程（包装外部库） | ✅ 默认 | 透传 |
| sharpd | logpump | ❌ 自定义 | 无 |
| lib | gRPC northbound（northbound_grpc.cpp） | ❌ 自定义 | 无（`running` 恒 false） |
| mgmtd | **无** | — | 只在退出时 `frr_pthread_stop_all()` |
| ldpd | **无**（只有注释提到） | — | — |

**B. 全部自定义 attr（这是全树的"特殊用法"清单）**

| 文件:行 | start | stop |
|---|---|---|
| [lib/frr_pthread.c:55-58](lib/frr_pthread.c#L55-L58) | `fpt_run`（默认） | `fpt_halt`（默认） |
| [lib/frr_pthread.c:337-340](lib/frr_pthread.c#L337-L340) | `frr_pthread_attr_non_controlled_start` | 默认 |
| [bgpd/bgpd.c:10429-10438](bgpd/bgpd.c#L10429-L10438) | io=默认；**ka=`bgp_keepalives_start`** | io=默认；**ka=`bgp_keepalives_stop`** |
| [pathd/path_pcep_controller.c:163-166](pathd/path_pcep_controller.c#L163-L166) | 默认 | **`pcep_ctrl_halt_cb`** |
| [pathd/path_pcep_lib.c:284-286](pathd/path_pcep_lib.c#L284-L286) | **`pcep_lib_pthread_start_passthrough`** | **`pcep_lib_pthread_stop_cb`** |
| [sharpd/sharp_logpump.c:117-121](sharpd/sharp_logpump.c#L117-L121) | **`logpump_run`** | **`logpump_halt`**（只 `return 0`） |
| [lib/northbound_grpc.cpp:1368-1371](lib/northbound_grpc.cpp#L1368-L1371) | **`grpc_pthread_start`** | **`NULL`**（危险，见下） |
| [tests/lib/test_grpc.cpp:505-508](tests/lib/test_grpc.cpp#L505-L508) | `grpc_client_test_start` | `grpc_client_test_stop` |

> ⚠️ **`northbound_grpc.cpp` 的 `.stop = NULL` 之所以不崩，纯属巧合性的自洽**：
> `grpc_pthread_start` 从不调用 `frr_pthread_notify_running()`，
> 因此 `fpt->running` 恒为 false，`frr_pthread_stop_all()` 的
> `if (atomic_load(&fpt->running))` 判断会**跳过它**，于是永远不会解引用 NULL。
> 它自己在 `frr_grpc_finish()` 里做了 `pthread_join` + `frr_pthread_destroy`。
> **不要把这种写法当模板**——它踩中了 §3.5 陷阱 1 的两面性。

**C. `frr_pthread_init/finish/stop_all` 调用点**

| 函数 | 调用点 |
|------|--------|
| `frr_pthread_init()` | [lib/libfrr.c:931](lib/libfrr.c#L931)（`frr_init()` 内）；各测试程序 |
| `frr_pthread_finish()` | [lib/libfrr.c:1427](lib/libfrr.c#L1427)（`frr_fini()` 内，内含 `stop_all`） |
| `frr_pthread_stop_all()` | [bgpd/bgpd.c:10455](bgpd/bgpd.c#L10455)（`bgp_pthreads_finish`）、[mgmtd/mgmt_main.c:101](mgmtd/mgmt_main.c#L101) |

`bgpd` 与 `mgmtd` **各调一次 `stop_all`**，随后 `frr_fini()` 里再调一次——
第二次是幂等的（已停的线程 `running` 为 false，会被跳过）。

---

## 7. 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|------|------|----------|
| 1 | 在 `frr_pthread_run()` 之前调 `frr_pthread_run()` 之外的线程操作 | — | 其实 **`fpt->master` 在 `new` 之后就可用了**，可以提前排事件（zebra 就这么干） |
| 2 | 线程还在跑就调 `frr_pthread_destroy()` | `event_master_free()` 后线程访问已释放的 loop → 崩溃 | **先 `stop`（会 join）、再 `destroy`** |
| 3 | 从线程**自己**里调 `frr_pthread_stop()` | 对自己 `pthread_join` → 失败/未定义；毒丸没人消费 | 停止只能由**别的线程**发起 |
| 4 | 自定义 start 忘了 `frr_pthread_notify_running()` | 父线程 `wait_running()` **永久阻塞**；且 `running` 恒 false → `stop_all` 永远跳过它 | 初始化完成后必须通知（见 §6.2 检查表） |
| 5 | 自定义 start 忘了 `frr_event_loop_set_pthread_owner()` | `event_cancel()` 的 `assert(owner == self)` 失败 | 入口第一句就设（见 §6.2 检查表） |
| 6 | 自定义 start 忘了维护 `fpt->running` | 循环无法退出 / `stop_all` 行为异常 | 循环条件用 `atomic_load(&fpt->running)`，stop 里置 false（见 §6.2） |
| 7 | 自定义 start 不用事件循环，却忘了 `rcu_read_unlock()` | RCU 宽限期永不结束 → **所有 `rcu_free()` 的内存泄漏** | 入口处手动 `rcu_read_unlock()`（[bgp_keepalives.c:219-224](bgpd/bgp_keepalives.c#L219-L224)） |
| 8 | 自定义 stop 忘了 `pthread_join()` | `frr_pthread_stop()` 返回时线程还活着 → 后续 `destroy` 悬空 | stop 必须**同步**到线程终止（默认 `fpt_halt` 已含 join） |
| 9 | 需要清理却用默认 stop | 默认循环退出**不跑任何 cleanup** | 自定义 stop，投递"先清理后置 false"的事件（pathd 范例，§6.4） |
| 10 | 跨线程用 `event_cancel()` 而非 `event_cancel_async()` | `assert(master->owner == pthread_self())` 失败 | 别人的 loop 一律用 `event_cancel_async`（或按 owner 自适应，§4.2） |
| 11 | 以为 `event_add_event()` 是同步调用 | 回调还没执行就继续往下走了 | 它是**异步**的；要等结果就再投一个回程事件 |
| 12 | 跨线程直接调用对方的函数改对方数据 | 数据竞争，难查的偶发崩溃 | 一律改成「投事件」；必须共享的用锁（§4.3） |
| 13 | 在 `frr_pthread_new()` 前调 `frr_pthread_init()` 之外的操作 | 全局链表为空 → 段错误 | 模块初始化顺序由 `frr_init()` 保证，**测试程序要自己调 `frr_pthread_init()`** |
| 14 | 进程 `fork()` 之前建线程 | `assert(frr_is_after_fork ...)` 失败 | 线程只能在 `frr_config_fork()` 之后创建；ldpd 因此显式设 `frr_is_after_fork = true`（[ldpd/lde.c:131-132](ldpd/lde.c#L131-L132)） |
| 15 | 外部库线程回调进 FRR 却没接入 RCU | `zlog_*` 或 `rcu_read_lock()` 里 `assert(rt)` 崩溃 | 回调入口调 `frr_pthread_non_controlled_startup()`，且**它之前不能打日志**（§5.6） |
| 16 | 以为 `running == true` 就等于线程在跑事件循环 | 「排异式」线程可能自己有别的判据 | `show event cpu` 里「No data to display」= 该线程没用事件循环 |

---

## 8. 一句话记住的设计精华

| 设计 | 一句话 |
|------|--------|
| **每线程一个事件循环** | 把「线程间通信」降级成「跨线程投递事件」，复用整套事件 API，代码几乎不用改 |
| **`startup_cond`** | 防止新线程抢在父线程完成 `run()` 之前跑起来 |
| **`running_cond`** | 防止父线程抢在新线程自初始化完成之前用它 |
| **毒丸（poison pill）** | 不打断线程，只告诉它收工——停在线程边界，状态永远一致 |
| **sleeper 管道** | 不是用来睡的，是防止「无事可做 → 忙等 100% CPU」的占位 fd |
| **`handle_sigs = false`** | 信号只在主线程处理；工作线程连掩码都继承屏蔽 |
| **`rcu_thread_prepare/start`** | 父线程预建（继承 epoch）→ 子线程绑 TLS；保证 `fpt->data` 里的 RCU 对象可安全交接 |
| **`fd_poll()` 里 `rcu_read_unlock()`** | 事件循环在阻塞等待期间**不持读锁**，RCU 宽限期才能推进 |
| **`io_pipe`** | 向写端写 1 字节 = 把正在 epoll_wait 睡觉的目标线程叫醒 |
| **`event_cancel_async`** | 唯一必须区分同线程/跨线程的 API，且**阻塞等待**取消真正完成 |

---

## 9. 自测题（复习用）

1. `frr_pthread_new()` 之后、`frr_pthread_run()` 之前，能不能往 `fpt->master` 上投事件？为什么？
2. 为什么需要**两个**条件变量（`startup_cond` 和 `running_cond`）？各自防的是什么竞争？
3. `fpt_run()` 里的 `sleeper` 管道，写端从来没人写，那它是干什么的？去掉它会怎样？
4. 主线程调 `frr_pthread_run()` 时，`rcu_thread_prepare()` 里的 `rcu_assert_read_locked()` 为什么不会失败？
5. 自定义 start 函数必须做哪五件事？（提示：owner / 通知 / 循环条件 / RCU / 日志）
6. 为什么 `event_cancel()` 和 `event_cancel_async()` 要分成两个函数？各有一个什么断言？
7. bgpd 的 Keepalives 线程为什么不用事件循环？它靠什么同步？这带来哪些必须手动补的动作？
8. `frr_pthread_stop_all()` 会跳过哪一类线程？为什么 gRPC 的 `.stop = NULL` 没有导致崩溃？
9. 如果某个线程在 `show event cpu` 里显示 "No data to display yet"，你能得出什么结论？
10. `frr_pthread_non_controlled_startup()` 内部为什么要把 `fpt->started` 预置为 true？
11. 默认事件循环退出后为什么不跑 cleanup？需要清理时该怎么办？
12. 事件循环在 `epoll_wait()` 睡觉时，持有 RCU 读锁吗？这个设计是为了什么？

---

## 10. 代码索引（按阅读顺序）

**必读（核心实现，共约 820 行）**

1. [lib/frr_pthread.h](lib/frr_pthread.h) — 结构体定义、API 文档、`frr_with_mutex` 宏
2. [lib/frr_pthread.c](lib/frr_pthread.c) — 全部实现，建议顺序：
   `frr_pthread_new` → `frr_pthread_run` → `frr_pthread_inner` → `fpt_run` → `fpt_halt`
3. [lib/frrevent.h](lib/frrevent.h) — `struct event` / 事件类型 / `event_add_*` 宏
4. [lib/event.c](lib/event.c) — 重点看：
   - `struct event_loop` [:153-178](lib/event.c#L153-L178)
   - `event_master_create` [:875-986](lib/event.c#L875-L986)（`io_pipe` 初始化）
   - `AWAKEN` 宏 [:246-251](lib/event.c#L246-L251)
   - `fd_poll` [:1250-1396](lib/event.c#L1250-L1396)（RCU unlock + poll + RCU lock）
   - `_event_add_event` [:1694-1722](lib/event.c#L1694-L1722)
   - `event_cancel` / `event_cancel_async` [:2200-2300](lib/event.c#L2200-L2300)
   - `event_fetch_inner_loop` / `event_fetch` [:2698-2858](lib/event.c#L2698-L2858)
   - `event_call` [:2988-3117](lib/event.c#L2988-L3117)

**选读（理解耦合）**

5. [lib/frrcu.c:192-330](lib/frrcu.c#L192-L330) — RCU 线程生命周期
6. [lib/sigevent.h:110-145](lib/sigevent.h#L110-L145) — `frr_sigset_add_mainonly`
7. [lib/zlog.c:357-470](lib/zlog.c#L357-L470) — 线程局部日志缓冲
8. [lib/libfrr.c:1343-1395](lib/libfrr.c#L1343-L1395) — 主线程的 `frr_run()` 循环

**实战（三个层次各一例）**

9. [bgpd/bgp_io.c](bgpd/bgp_io.c) + [bgpd/bgp_keepalives.c](bgpd/bgp_keepalives.c)
   — 默认循环 vs 自定义循环，对照着看效果最好
10. [zebra/zebra_dplane.c:8510-8607](zebra/zebra_dplane.c#L8510-L8607) — 「先排事件再启动」模式
11. [pathd/path_pcep_controller.c](pathd/path_pcep_controller.c) — 自定义 stop + `fpt->data`

**官方文档**

12. [doc/developer/process-architecture.rst](doc/developer/process-architecture.rst) — 事件循环与内核线程的架构说明（§"Kernel Thread Architecture"）
13. [doc/developer/rcu.rst](doc/developer/rcu.rst) — RCU 与 `frr_pthread` 的接口约定
14. [doc/developer/locking.rst](doc/developer/locking.rst) — `frr_with_mutex` / `frr_mutex_lock_autounlock`
15. [doc/developer/logging.rst](doc/developer/logging.rst) — 每线程日志缓冲

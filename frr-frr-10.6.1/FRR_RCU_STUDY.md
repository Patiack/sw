# FRR frrcu 机制深入剖析（用户态 RCU 实现）

> 基于 `frr-10.6.1` 源码（分支 `study/bgpd-annotated`）整理。
> 所有行号均为本仓库当前代码的**实测结果**（仓库内 `lib/` 下部分文件带有中文导读注释，
> 行号以当前树为准），改动代码后需复核。
> 用途：把 FRR「自己实现的一套用户态 RCU」讲透 —— 从**为什么不用锁**，
> 到 `rcu_read_lock/unlock` → `rcu_bump` → `rcu_main` 清扫线程的**每一步时序**，
> 再到它与 **frr_pthread / 事件循环 / zlog** 的咬合关系，
> 最后给出**全树真实用法清单**与**踩坑速查表**。
>
> 配套阅读：[doc/developer/rcu.rst](doc/developer/rcu.rst)（官方规范）、
> [FRR_PTHREAD_STUDY.md](FRR_PTHREAD_STUDY.md)（线程与事件循环）。

---

## 0. 全局总览（先记这张图）

### 0.1 一句话结论

**FRR 的 RCU 不是内核 RCU 的移植，而是一套「纪元（epoch）+ 序列锁 + 原子链表」拼出来的
极简延迟回收器**，它只解决一个问题：

> **一块内存（或一个 fd）被"删除"之后，怎么知道别的线程都已经不再看它了？**

答案是：**不回答这个问题，而是让调用方用一个全局序号（epoch）来"预订"自己正在看的数据
属于哪一代；清扫线程等所有线程都离开这一代之后，才真正执行 free/close。**

[lib/frrcu.c:44-56](lib/frrcu.c#L44-L56) 的 implementation notes 就是这句话的原文。

### 0.2 心智模型：为什么需要 RCU

```
传统加锁读                           FRR 的 RCU 读
+--------------------------+        +--------------------------+
| 读之前加锁               |        | 读之前只自增一个本地计数   |
| 写者也要加同一把锁       |        | 写者完全不碰读者的锁       |
| 读写互相阻塞             |        | 读写不互斥                |
| 释放 = 当场 free         |        | 释放 = 挂队列，宽限期后再 free |
+--------------------------+        +--------------------------+
        代价：读侧开销大                     代价：内存延迟回收 + 读者规则更严
```

FRR 的日志目标链、线程链表这类数据的特点是：**读极其频繁（每条日志都要遍历目标），
写极少（只有配置变更时）。** 用锁去保护它们，只会让每条日志都付出一次加解锁的代价。
[doc/developer/rcu.rst:58-60](doc/developer/rcu.rst#L58-L60) 明确写了这条取舍。

但代价也是实打实的，且 FRR 的版本比内核更"苛刻"——见 §1.3。

### 0.3 全景图

```
                          ┌──────────────────────────────────────────┐
                          │      全局纪元 rcu_seq (struct seqlock)    │
                          │      取值 1, 5, 9, 13 ... (每次 +4)       │
                          └──────────────────────────────────────────┘
                                  ▲                        │
                     seqlock_bump │                        │ seqlock_wait / timedwait
                                  │                        ▼
  ┌───────────────┐        ┌──────┴───────┐        ┌───────────────────────────┐
  │  工作线程 A    │        │  rcu_bump()  │        │   RCU 清扫线程 rcu_main()  │
  │  rcu_thread A │        │ 入队2项:      │        │   1) 等 rcu_seq 前进       │
  │  ├ rt->rcu=5  │        │  RCUA_NEXT   │        │   2) 等所有线程离开旧纪元  │
  │  └ depth=1    │        │  RCUA_FREE   │        │      (100ms 超时→watchdog) │
  └──────┬────────┘        └──────┬───────┘        │   3) 弹队列直到 RCUA_NEXT  │
         │ rcu_enqueue            │                │      → rcu_do() 真正释放   │
         │ (memtype, offset)      ▼                └───────────┬───────────────┘
         └──────────────► ┌─────────────────────┐               │
                          │ rcu_heads 原子链表    │◄──────────────┘
                          │ [act][NEXT][FREE][..]│   （此链表永不为空！）
                          └─────────────────────┘
   rcu_threads 原子链表：所有线程的 rcu_thread，清扫线程靠它"逐线程点名"
```

### 0.4 文件职责分层

| 文件 | 行数 | 职责 | 关键符号 |
|---|---|---|---|
| [lib/frrcu.h](lib/frrcu.h) | 264 | 对外 API + 数据结构（头部有 RCU 速览） | `rcu_free` `rcu_call` `rcu_head` `rcu_action` |
| [lib/frrcu.c](lib/frrcu.c) | 657 | 全部实现：纪元推进、清扫线程、线程注册 | `rcu_bump` `rcu_main` `rcu_enqueue` |
| [lib/seqlock.h](lib/seqlock.h) | 262 | 纪元原语（带 WAITERS 位的序列计数器） | `seqlock_acquire` `SEQLOCK_INCR` |
| [lib/seqlock.c](lib/seqlock.c) | 487 | 平台相关等待（Linux futex / 通用 condvar） | `seqlock_wait` `seqlock_bump` |
| [lib/atomlist.h](lib/atomlist.h) | — | 无锁单链表，RCU 队列的载体 | `rcu_heads_add_tail` `rcu_heads_pop` |
| [doc/developer/rcu.rst](doc/developer/rcu.rst) | 278 | 官方规范（**使用前必读**） | "When (not) to use RCU" @56 |

**注意：`lib/seqlock.h/.c` 在 10.6.1 中只有 frrcu 一个使用者。** 全树 grep 确认没有别的
模块直接 include seqlock.h。

### 0.5 关键常量与不变量（背下来）

| 名称 | 值 | 位置 | 含义 |
|---|---|---|---|
| `SEQLOCK_STARTVAL` | `1U` | [lib/seqlock.h:116](lib/seqlock.h#L116) | 初始纪元值（bit0 = HELD，表示"持有中"） |
| `SEQLOCK_INCR` | `4U` | [lib/seqlock.h:117](lib/seqlock.h#L117) | 每次 bump 步长，跳过 bit0/bit1 |
| `SEQLOCK_HELD` | `1U<<0` | [lib/seqlock.h:113](lib/seqlock.h#L113) | bit0：该锁已持有；`0` = 未持有 |
| `SEQLOCK_WAITERS` | `1U<<1` | [lib/seqlock.h:114](lib/seqlock.h#L114) | bit1：有线程在等，仅 HELD 时可置 |
| `SEQLOCK_VAL(n)` | `n & ~WAITERS` | [lib/seqlock.h:115](lib/seqlock.h#L115) | 取出纯序列号 |
| 看门狗超时 | `100ms` | [lib/frrcu.c:556](lib/frrcu.c#L556) | 单个线程拖住宽限期的告警阈值 |

四条硬性不变量：

1. **读锁必须"连续"持有**，任何一处 unlock 都会让 RCU 保护的指针立即失效
   （[doc/developer/rcu.rst:127-134](doc/developer/rcu.rst#L127-L134)）。
2. **`rcu_heads` 队列永不为空**——这是 atomlist `last` 指针存活的必要条件
   （[lib/frrcu.c:350-361](lib/frrcu.c#L350-L361)）。
3. **`rcu_bump()` 必须在持有读锁时调用**，否则清扫线程可能看到链表的"中断点"
   （[lib/frrcu.c:326-346](lib/frrcu.c#L326-L346)）。
4. **`rcu_head` 不可复用**：一旦传给 `rcu_*`，它就归 RCU 所有
   （[doc/developer/rcu.rst:148-151](doc/developer/rcu.rst#L148-L151)）。

---

## 1. 设计动机与适用边界

### 1.1 锁 vs RCU：代价落在哪一侧

| 维度 | 互斥锁 / rwlock | FRR RCU |
|---|---|---|
| 读侧开销 | 至少一次原子操作 + 可能的睡眠 | 一次原子 load/store（本地 depth 甚至不是原子） |
| 读侧可否睡眠 | 可以 | **理论可以，但会拖死宽限期**（见 §7） |
| 写侧开销 | 一次加锁 | 一次分配（新副本）+ 入队 + bump |
| 删除安全性 | 由锁保证互斥 | 由宽限期保证"没人再看" |
| 内存占用 | 1 份 | 短暂 2 份（新旧副本并存） |
| 实时性 | 立即可见 | 可见性取决于纪元推进时机 |
| 复杂度 | 低 | **高**，规则多且违反后是UAF而不是死锁 |

注意最后一行：**违反 RCU 规则的后果通常是 use-after-free（静默数据损坏），
而不是死锁（立刻卡住）。** 这是 RCU 比锁更难用的根本原因。

### 1.2 什么时候用、什么时候别用

官方在 [doc/developer/rcu.rst:56](doc/developer/rcu.rst#L56) 专门写了 "When (not) to use RCU"：

> RCU is designed for read-heavy workloads where objects are updated relatively
> rarely, but frequently accessed. Do *not* indiscriminately replace locking by
> RCU patterns.
>
>RCU 适用于读多写少的工作负载，也就是说对象更新相对较少，但访问频繁。不要随意用 RCU 模式替代锁机制。

即：**读多写少**才值得。同时它提醒了两个副作用：新旧副本并存导致的内存占用增加，
以及清扫线程批量释放时**成簇的 malloc/free 竞争**。

[doc/developer/rcu.rst:70](doc/developer/rcu.rst#L70) 的 "Other useful patterns" 给了两个
"缩减版" RCU 用法，这两条在 FRR 里都有真实案例：

| 缩减用法 | 含义 | FRR 实例 |
|---|---|---|
| **原子更新单个字段** | 对象不变，只把某个 int/fd 原子换掉 | [lib/zlog_targets.c:407](lib/zlog_targets.c#L407) 用 `atomic_exchange_explicit` 换 fd，再 `rcu_close` 旧 fd |
| **只读/不可变对象** | 对象建好就不改，但**删除仍会与读竞争**，所以仍须 `rcu_free` | [lib/zlog.h:371](lib/zlog.h#L371) 的 `struct zlog_target` |

### 1.3 FRR 相对内核 RCU 的三处"阉割"（必须知道）

[lib/frrcu.h:74-79](lib/frrcu.h#L74-L79) 列了两条，实际上有三条：

**① 没有 `rcu_synchronize()`。**

没有办法"同步等待一个宽限期结束"。真需要延迟动作，只能用 `rcu_call()`，
而它跑在清扫线程上**并会阻塞清扫线程**（[lib/frrcu.h:237](lib/frrcu.h#L237) 原话
"use this sparingly"）。全树 grep 确认 **10.6.1 中没有任何消费者使用 `rcu_call`**。

**② 没有 `rcu_dereference()` / `rcu_assign_pointer()`。**

必须改用 `atomic_load_explicit` / `atomic_store_explicit`，或者干脆用 ATOMLIST 系列
（[lib/frrcu.h:77-78](lib/frrcu.h#L77-L78)）。这也意味着 FRR 里**没有内存屏障语义的封装**，
把正确性责任交给调用方。

**③ 最反直觉的一条：即使持有读锁，调用线程也不能访问刚 `rcu_free` 的对象。**

> Note that even if a thread is in rcu_read_lock(), it is invalid for that
> thread to access bits after rcu_free() & co on them.
>
> 请注意，即使一个线程处于 rcu_read_lock() 中，该线程在 rcu_free() 及相关操作执行后访问那些位也是无效的。
> —— [lib/frrcu.h:69-72](lib/frrcu.h#L69-L72)

[doc/developer/rcu.rst:177-182](doc/developer/rcu.rst#L177-L182) 再次强调：

> In some other RCU implementations, the pointer remains valid to the
> *calling* thread if it is holding the RCU read-side lock. This is not the
> case in FRR, particularly when running single-threaded.
>
> 在其他一些 RCU 实现中，如果调用线程持有 RCU 读侧锁，指针会保持有效。但在 FRR 中情况并非如此，尤其是在单线程运行时。

**为什么要这么"自虐"？** 答案在紧接的一句注释里：**为了让单线程运行时整套 RCU 能退化成
一个空操作（no-op）**。如果线程自己的读锁能保护自己免于 free，那么单线程路径就必须
保留真实队列，做不到零成本。这个设计取舍直接决定了 §5.4 的 `rcu_active == false` 快速路径。

### 1.4 单线程退化：整个机制的"隐藏形态"

看 [lib/frrcu.c:642-648](lib/frrcu.c#L642-L648)：

```c
	if (!rcu_active) {
		rcu_do(rh);          /* 直接同步执行，不入队 */
		return;
	}
	rcu_heads_add_tail(&rcu_heads, rh);
	atomic_store_explicit(&rcu_dirty, seqlock_cur(&rcu_seq), memory_order_relaxed);
```

`rcu_active` 只在线程真正被创建时才为 `true`（[lib/frrcu.c:470](lib/frrcu.c#L470)，
`rcu_start()` 里）。所以：

```
任何用到 RCU 的守护进程              → rcu_active == true  → 完整 RCU 语义
单线程工具（vtysh、watchfrr? 等）    → rcu_active == false → rcu_free 立刻生效
```

这就解释了"读锁内也不能访问已 free 对象"这条规则——**因为在这条路径上，对象真的已经不在了。**

---

## 2. 数据结构

### 2.1 `struct rcu_thread`：每个线程的 RCU 身份

[lib/frrcu.c:102-111](lib/frrcu.c#L102-L111)：

```c
struct rcu_thread {
	struct rcu_threads_item head;   /* 挂入 rcu_threads 链表 */
	struct rcu_head rcu_head;       /* 本结构体自己被 RCU 释放时用 */
	struct seqlock rcu;             /* 本线程"钉住"的纪元值 */
	unsigned depth;                 /* 读锁嵌套深度 */
};
```

| 字段 | 谁来写 | 说明 |
|---|---|---|
| `head` | 任何线程（原子链表） | 清扫线程靠遍历这条链找到所有需要等待的线程 |
| `rcu_head` | 仅本线程退出时 | 线程退出后 `rcu_thread` 自身也要延迟释放（[lib/frrcu.c:293](lib/frrcu.c#L293)） |
| `rcu` | 仅本线程（+ 清扫线程等待） | `pos` 是本线程当前钉住的纪元 |
| `depth` | **仅本线程**，非原子 | 注释明说 "only accessed by thread itself, not atomic" |

**`depth` 不是原子变量**这一点很关键：FRR 的读锁**完全不可跨线程共享**，
它就是"本线程的一个计数器"。所以 RCU 读锁不能当锁用、不能由 A 线程 lock 由 B 线程 unlock。

### 2.2 队列与动作描述

```
struct rcu_head          (16~24 字节，会被内嵌到成千上万个对象里)
+--------------------------+
| struct rcu_heads_item head | ──► 原子链表挂链节点
| const struct rcu_action *action | ──► 指向一个 static const 的动作描述
+--------------------------+
                │
                ▼
struct rcu_action        (一个动作描述，复用同一份 static const 实例)
+---------------------+
| enum rcu_action_type |
| union {              |
|   free  { mt, offset }   ← 用 offset 反推容器地址，用 mt 做内存统计
|   call  { fptr, offset } ← 回调跑在清扫线程上
| }                    |
+---------------------+
```

定义见 [lib/frrcu.h:166-180](lib/frrcu.h#L166-L180)；注释解释了为什么拆成两个结构体：

> since rcu_head is intended to be embedded into structs which may exist
> with lots of copies, rcu_head is shrunk down to its absolute minimum -
> the atomlist pointer + a pointer to this action struct.
>
>由于 rcu_head 是用来嵌入到可能有很多副本的结构体中的，rcu_head 被缩小到其绝对最小——atomlist 指针，一个指向这个 action 结构体的指针。


**这就是 `rcu_free(mtype, ptr, field)` 里 `field` 参数的用途**：把 `offsetof(ptr, field)`
存进 `rcu_action`，释放时用 `p = (char *)rh - offset` 反推出对象的真实地址
（[lib/frrcu.c:497](lib/frrcu.c#L497)、[lib/frrcu.c:509](lib/frrcu.c#L509)）。
**代价：`field` 必须是固定的编译期常量，且 `rcu_action` 是 `static const` 的
——每个调用点只有一份，因此 `mtype` 也必须是常量。**

动作类型枚举（[lib/frrcu.h:146-155](lib/frrcu.h#L146-L155)）：

| 类型 | 用途 | 谁产生 |
|---|---|---|
| `RCUA_INVALID` | 占位初值 | — |
| `RCUA_NEXT` | **纪元边界哨兵** | `rcu_bump()` 内部 |
| `RCUA_END` | **整体结束** | `rcu_shutdown()` |
| `RCUA_FREE` | 释放内存 | `rcu_free()` 宏 |
| `RCUA_CLOSE` | 关闭 fd | `rcu_close()` |
| `RCUA_CALL` | 调用回调 | `rcu_call()` 宏（全树无人使用） |

三个常量实例是**共享**的（[lib/frrcu.c:120-122](lib/frrcu.c#L120-L122)），
因为它们不含任何个体信息。

`struct rcu_head_close` 则是"多带一个 fd"的特化版（[lib/frrcu.h:195-198](lib/frrcu.h#L195-L198)），
清扫时用 `container_of` 反推（[lib/frrcu.c:504-506](lib/frrcu.c#L504-L506)）：

```c
	case RCUA_CLOSE:
		rhc = container_of(rh, struct rcu_head_close, rcu_head);
		close(rhc->fd);
		break;
```

### 2.3 `struct rcu_next`：一个"为了队列不空"而存在的结构

[lib/frrcu.c:128-131](lib/frrcu.c#L128-L131)：

```c
struct rcu_next {
	struct rcu_head head_free;   /* 稍后释放本结构体自己 */
	struct rcu_head head_next;   /* RCUA_NEXT 哨兵 */
};
```

每做一次 `rcu_bump()`，就分配一个 `rcu_next`，**往队列里塞两项**（NEXT 和 FREE），
而不是把"释放自己"合并进 NEXT 动作里。为什么？

> This is INTENTIONALLY not built into the RCUA_NEXT action. This ensures that
> after the action above is popped off the queue, there is still at least 1
> item on the RCU queue. ... If we were to "run dry" on the RCU queue, add_tail
> may run into the "last item is being deleted - start over" case, and then we
> may end up accessing old RCU queue items that are already free'd.
>
> 这故意没有内置到 RCUA_NEXT 动作中。这确保了在上面的动作从队列中弹出之后，RCU 队列上至少还有 1 个项目。……如果我们的 RCU 队列“用完了”，add_tail 可能会碰到“最后一个项目正在被删除——重新开始”的情况，然后我们可能会访问已经被释放的旧 RCU 队列项目。
> —— [lib/frrcu.c:350-361](lib/frrcu.c#L350-L361)

**这段注释是理解 FRR RCU 的关键之一**：它把 atomlist 的实现细节（`last` 指针）
暴露成了 RCU 的硬性不变量。详见 §3.6。

### 2.4 全局变量一览

| 变量 | 定义 | 说明 |
|---|---|---|
| `rcu_seq` | [lib/frrcu.c:155](lib/frrcu.c#L155) | **全局纪元计数器**，唯一的同步真相来源 |
| `rcu_dirty` | [lib/frrcu.c:159](lib/frrcu.c#L159) | `_Atomic seqlock_val_t`；入队时被设为当时的 `rcu_seq` |
| `rcu_threads` | [lib/frrcu.c:162](lib/frrcu.c#L162) | 所有线程的 `rcu_thread` 链表 |
| `rcu_heads` | [lib/frrcu.c:163](lib/frrcu.c#L163) | 待处理动作队列 |
| `rcu_thread_main` | [lib/frrcu.c:180](lib/frrcu.c#L180) | 主线程状态（主线程不是 pthread_create 出来的） |
| `rcu_thread_rcu` | [lib/frrcu.c:181](lib/frrcu.c#L181) | 清扫线程状态，**故意不加入 `rcu_threads`** |
| `rcu_pthread` | [lib/frrcu.c:187](lib/frrcu.c#L187) | 清扫线程句柄 |
| `rcu_thread_key` | [lib/frrcu.c:188](lib/frrcu.c#L188) | TLS key，`rcu_self()` 靠它取回本线程的 `rcu_thread` |
| `rcu_active` | [lib/frrcu.c:189](lib/frrcu.c#L189) | 是否处于"真多线程"模式 |

`rcu_thread_rcu` 不加入链表的原因写在 [lib/frrcu.c:176-178](lib/frrcu.c#L176-L178)：
清扫线程自己也可能调用用到 RCU 的函数，需要一个"不算数"的身份。
（真加进去的话，清扫线程就会等自己，直接死锁。）

### 2.5 `struct seqlock`：纪元值的载体

[lib/seqlock.h:135-141](lib/seqlock.h#L135-L141)：

```c
struct seqlock {
/* always used */
	seqlock_ctr_t pos;          /* _Atomic uint32_t，唯一的同步真相 */
/* used when futexes not available: (i.e. non-linux) */
	pthread_mutex_t lock;
	pthread_cond_t  wake;
};
```

**它名字叫 seqlock，但和 Linux 内核的 seqlock 不是一回事。** 它本质是一个
"带状态位的原子计数器"，`pos` 的位布局：

```
 31                                2   1        0
+------------------------------------+---------+--------+
|           序列号（每次 +4）          | WAITERS |  HELD  |
+------------------------------------+---------+--------+
                                     bit1      bit0
  pos == 0  →  未持有（没有任何线程钉住这个纪元）
  pos == 1  →  持有初始纪元
  pos == 5  →  持有第 2 代
  ...
```

等待者要判断"目标是否已经越过我等的值"，用的是**无符号环绕算术**
（[lib/seqlock.c:271](lib/seqlock.c#L271)）：

```c
	while (cur & SEQLOCK_HELD) {
		cal = SEQLOCK_VAL(cur) - val - 1;
		assert(cal < 0x40000000 || cal > 0xc0000000);
		if (cal < 0x80000000)
			break;        /* 已经越过 val，退出等待 */
		...
	}
```

逐条翻译：

| 条件 | `cal` | 含义 |
|---|---|---|
| `cur == val` | `0xFFFFFFFF` | 还停在我等的这一代 → 继续等 |
| `cur == val + 4` | `3` | 已经前进一代 → 退出 |
| `cur == 0`（未持有） | 循环条件 `cur & HELD` 为假 | 直接退出 |
| 差距过大（≥2^30 代） | 断言失败 | 说明有线程卡了 10 亿次 bump |

最后那个 `assert` 是 32 位回绕的**安全阀**：`SEQLOCK_INCR=4`，32 位空间共 2^30 代，
只要"等待期间的代差"小于 2^30，环绕比较就不会误判。

**平台实现**：Linux 用 futex（`FUTEX_WAIT` / `FUTEX_WAIT_BITSET` / `FUTEX_WAKE`），
OpenBSD 用 `futex()`，FreeBSD 用 `_umtx_op()`，其余平台回退到 pthread 条件变量；
**编译 TSAN 时强制使用 condvar 回退**（[lib/seqlock.c:64](lib/seqlock.c#L64) 附近注释）。

---

## 3. 宽限期机制（核心）

### 3.1 纪元模型：一句话

```
rcu_seq 是"当前是第几代数据"。
线程 rcu_read_lock()   → 把自己的 rt->rcu 设成 rcu_seq 的当前值（钉住这一代）
线程 rcu_read_unlock() → 把 rt->rcu 归零（我不再看任何一代了）
rcu_bump()            → rcu_seq +4，并在队列上打一个 RCUA_NEXT 哨兵
清扫线程               → 等所有线程都离开"哨兵之前那一代"，就执行到哨兵为止的所有动作
```

原话在 [lib/frrcu.c:44-47](lib/frrcu.c#L44-L47)：

> this is an epoch-based RCU implementation. rcu_seq (global variable) counts
> the current epoch. Threads hold a specific epoch in rcu_read_lock(). This is
> the oldest epoch a thread might be accessing data from.

注意最后半句：**线程钉住的纪元 = 它可能访问到的最老数据。** 宽限期要等的就是"最老"的那个。

**epoch 只在 `rcu_read_lock()` / `rcu_read_unlock()` 两处推进**，
理由写在 [lib/frrcu.c:49-54](lib/frrcu.c#L49-L54)：

> - on rcu_read_lock, we don't want to hold an old epoch pointlessly
> - on rcu_read_unlock, we want to make sure we're not stuck on an old epoch
>   when heading into a long idle period where no thread holds RCU

即：**入读锁时顺手把队列里的待办推出去；出读锁时顺手把纪元推高，免得自己闲着却卡住别人的回收。**

### 3.2 `rcu_read_lock()` / `rcu_read_unlock()`

[lib/frrcu.c:396-409](lib/frrcu.c#L396-L409)：

```c
void rcu_read_lock(void)
{
	struct rcu_thread *rt = rcu_self();

	assert(rt);
	if (rt->depth++ > 0)
		return;                          /* 嵌套：只加计数 */

	seqlock_acquire(&rt->rcu, &rcu_seq);  /* 先钉住当前纪元 */
	/* need to hold RCU for bump ... */
	rcu_bump_maybe();                     /* 必要时推纪元 */
	/* ... but no point in holding the old epoch if we just bumped */
	seqlock_acquire(&rt->rcu, &rcu_seq);  /* 重新钉到最新纪元 */
}
```

**为什么要 acquire 两次？** 因为 `rcu_bump()` 必须先持有读锁（§3.3 的竞争条件），
但 bump 之后自己就"落后一代"了，再 acquire 一次把自己推进到新纪元，才不会无谓地
拖住别人。这个细节在 [lib/frrcu.c:405-407](lib/frrcu.c#L405-L407) 有注释。

[lib/frrcu.c:413-422](lib/frrcu.c#L413-L422)：

```c
void rcu_read_unlock(void)
{
	struct rcu_thread *rt = rcu_self();

	assert(rt && rt->depth);
	if (--rt->depth > 0)
		return;
	rcu_bump_maybe();                     /* 别卡在旧纪元上 */
	seqlock_release(&rt->rcu);            /* pos = 0：我退出 RCU 了 */
}
```

两个断言函数（[lib/frrcu.c:425-436](lib/frrcu.c#L425-L436)）验证的是另一件事：

```c
void rcu_assert_read_locked(void)
{
	struct rcu_thread *rt = rcu_self();
	assert(rt && rt->depth && seqlock_held(&rt->rcu));
}
```

**注意它同时检查 `depth` 和 `seqlock_held`**——因为 `rcu_thread_unprepare()` 期间
会出现 `depth == 1` 但锁已释放的中间态。

开销表（单线程、无竞争时）：

| 调用 | 最外层 | 嵌套层 |
|---|---|---|
| `rcu_read_lock` | 2 次原子 exchange + 1 次原子 load | **1 次非原子自增** |
| `rcu_read_unlock` | 1 次原子 exchange + 1 次原子 load | **1 次非原子自减** |

这就是 RCU 读侧便宜的原因：**嵌套时连原子操作都没有。**

### 3.3 `rcu_dirty` / `rcu_bump_maybe()` / `rcu_bump()`

**`rcu_bump_maybe()` 是"是否需要推进"的廉价判断**（[lib/frrcu.c:380-391](lib/frrcu.c#L380-L391)）：

```c
static void rcu_bump_maybe(void)
{
	seqlock_val_t dirty;

	dirty = atomic_load_explicit(&rcu_dirty, memory_order_relaxed);
	/* no problem if we race here and multiple threads bump rcu_seq;
	 * bumping too much causes no issues while not bumping enough will
	 * result in delayed cleanup
	 */
	if (dirty == seqlock_cur(&rcu_seq))
		rcu_bump();
}
```

`rcu_dirty` 的语义：**"最近一次入队发生在哪一代"**。如果它等于当前纪元，
说明"这一代有脏数据，需要推进一代才能让清扫线程看到并回收"。

**这是典型的"乐观无锁 + 允许重复劳动"设计**：竞争导致多 bump 完全无害
（只是纪元跑得快些），少 bump 只会让回收延迟。**没有 CAS、没有锁。**

`rcu_bump()` 是真正的推进（[lib/frrcu.c:315-376](lib/frrcu.c#L315-L376)），三步：

```c
	rn = XMALLOC(MTYPE_RCU_NEXT, sizeof(*rn));

	rn->head_next.action = &rcua_next;
	rcu_heads_add_tail(&rcu_heads, &rn->head_next);   /* ① 入队 RCUA_NEXT 哨兵 */

	rcu_free_internal(MTYPE_RCU_NEXT, rn, head_free); /* ② 入队"释放 rn 自己" */

	seqlock_bump(&rcu_seq);                           /* ③ 推纪元，唤醒清扫线程 */
```

代码里有一段很长的竞争条件说明（[lib/frrcu.c:326-346](lib/frrcu.c#L326-L346)），
值得完整理解：

```
时刻 T:  线程 A 正在 rcu_heads_add_tail(&rcu_heads, &rn->head_next) 的中途
         —— atomlist 的 last 指针已经指向 rn->head_next，
            但 rn->head_next 还没真正挂上链！

此时若线程 B 也做 rcu_bump()：
   B 的 add_tail 会挂到"还没上链的 A 的项"后面
   → 队列在 A 的项那里是"断的"
   → 若此刻清扫线程来弹队列，会在断点处误判为"队列结束"
   → 只弹出前半段（甚至什么都没弹出就停了）

FRR 的解法：要求调用 rcu_bump() 时必须持有 RCU 读锁。
  只要 A 还钉着当前纪元，清扫线程就不会处理这一代，自然不会踩到断点。
   等到 A 走到 rcu_read_unlock() 时，add_tail 必然已经完成。
```

**这条约束的直接后果**：`rcu_bump_maybe()`（进而 `rcu_read_lock/unlock`）
是**唯一**能安全触发 bump 的地方，`rcu_bump()` 是 `static` 的，
外部代码根本无法直接调用。类型系统把它封死了。

另有一个"无害的窗口"注释（[lib/frrcu.c:364-373](lib/frrcu.c#L364-L373)）：

> If another thread enqueues some RCU action in the intermediate window here,
> nothing bad happens - the queued action is associated with a larger seq# than
> strictly necessary. Thus, it might get executed a bit later, but that's not a
> problem.

即**入队与 bump 之间没有原子性**，导致动作可能被归到"更新"的纪元里，
回收稍晚一点点。这是刻意的取舍：**宁晚不早，晚了只是延迟，早了就是 UAF。**

### 3.4 `rcu_main()`：清扫线程

[lib/frrcu.c:540-592](lib/frrcu.c#L540-L592)：

```c
static void *rcu_main(void *arg)
{
	struct rcu_thread *rt;
	struct rcu_head *rh = NULL;
	bool end = false;
	struct timespec maxwait;

	seqlock_val_t rcuval = SEQLOCK_STARTVAL;

	pthread_setspecific(rcu_thread_key, &rcu_thread_rcu);

	while (!end) {
		seqlock_wait(&rcu_seq, rcuval);          /* ① 等纪元推进 */

		/* RCU watchdog timeout, TODO: configurable value */
		clock_gettime(CLOCK_MONOTONIC, &maxwait);
		maxwait.tv_nsec += 100 * 1000 * 1000;    /* ② 100ms 超时点 */
		if (maxwait.tv_nsec >= 1000000000) {
			maxwait.tv_sec++;
			maxwait.tv_nsec -= 1000000000;
		}

		frr_each (rcu_threads, &rcu_threads, rt) /* ③ 逐线程等待离开旧纪元 */
			if (!seqlock_timedwait(&rt->rcu, rcuval, &maxwait)) {
				rcu_watchdog(rt);
				seqlock_wait(&rt->rcu, rcuval);
			}

		while ((rh = rcu_heads_pop(&rcu_heads))) { /* ④ 弹队列执行 */
			if (rh->action->type == RCUA_NEXT)
				break;
			else if (rh->action->type == RCUA_END)
				end = true;
			else
				rcu_do(rh);
		}

		rcuval += SEQLOCK_INCR;                  /* ⑤ 我自己也前进一代 */
	}

	assert(!rcu_heads_first(&rcu_heads));
	return NULL;
}
```

逐步拆解：

| 步 | 做什么 | 关键点 |
|---|---|---|
| ① | `seqlock_wait(&rcu_seq, rcuval)` | 阻塞到"全局纪元 > 我等的这一代"。**rcuval 初值是 STARTVAL(1)**，所以第一次唤醒来自第一次 `rcu_bump()` |
| ② | 计算 100ms 后的绝对时刻 | 注意是 `CLOCK_MONOTONIC` 绝对时间，因为 `seqlock_timedwait` 收的是绝对时刻 |
| ③ | 对**每个**线程 `seqlock_timedwait(&rt->rcu, rcuval, &maxwait)` | 等它离开第 `rcuval` 代。**超时则调看门狗，然后无限等下去**——宁可卡住也不提前回收 |
| ④ | 弹队列直到 `RCUA_NEXT` | 执行的是"哨兵之前"的所有动作；`RCUA_END` 则整个循环结束 |
| ⑤ | `rcuval += SEQLOCK_INCR` | 清扫线程自己的节奏与全局纪元同步前进 |

**第③步是宽限期的真正定义**：不是"等 100ms"，而是"等所有线程离开这一代"。
100ms 只是**报警阈值**，超时后照样继续等（`seqlock_wait` 无超时版）。
`rcu_watchdog()` 目前是空的（[lib/frrcu.c:523-535](lib/frrcu.c#L523-L535)，
`#if 0` 包着），注释说未来计划给"拖住 RCU 的线程"打印回溯栈。

**第④步的队列消费顺序为什么正确？**
因为 `RCUA_NEXT` 是 bump 时插在队尾的，所以"哨兵之前的动作"恰好就是
"在上一代结束前入队的动作"。配合第③步"所有线程已离开上一代"，
就得到了完整的保证。

循环尾部那个断言（[lib/frrcu.c:584-590](lib/frrcu.c#L584-L590)）也很说明问题：

```c
	/* rcu_shutdown can only be called singlethreaded, and it does a
	 * pthread_join, so it should be impossible that anything ended up
	 * on the queue after RCUA_END
	 */
#if 1
	assert(!rcu_heads_first(&rcu_heads));
#else
	while ((rh = rcu_heads_pop(&rcu_heads)))
		if (rh->action->type >= RCUA_FREE)
			rcu_do(rh);
#endif
```

上面是"断言版"（debug），下面是"宽容版"（防守），当前代码**选择了断言**——
即 FRR 认为"RCUA_END 之后队列非空"是程序错误，不是可以容忍的情况。

### 3.5 一次完整宽限期的时序图

```
 t   工作线程 A                      rcu_seq   A.rt->rcu   rcu_heads 队列        清扫线程
──────────────────────────────────────────────────────────────────────────────────────────
 T0  rcu_read_lock()
       depth 0→1
       seqlock_acquire(rt->rcu,&rcu_seq)           = 1
       rcu_bump_maybe(): dirty(0)≠cur(1) → 不 bump
       seqlock_acquire(rt->rcu,&rcu_seq)           = 1
 T1  遍历目标链 / 读对象字段……（无锁，无原子操作）
 T2  rcu_free(MTYPE_X, obj, rcu_head)
       rcu_enqueue(): 断言持读锁
         rcu_heads_add_tail(obj)                        [obj]
         rcu_dirty = 1
 T3  rcu_read_unlock()
       depth 1→0
       rcu_bump_maybe(): dirty(1)==cur(1) → rcu_bump()
         add_tail(RCUA_NEXT)                            [obj][NEXT]
         rcu_free_internal(rn)                          [obj][NEXT][FREE]
         seqlock_bump(&rcu_seq) → 5
       seqlock_release(rt->rcu) → 0
                                    ▼ 5
 T4                                                    seqlock_wait(rcu_seq,1) 返回
                                                       逐线程等待 rt->rcu 离开第 1 代
                                                          A.rt->rcu=0 → 立即返回
                                                          （其它线程若还钉着 1，则阻塞在此）
                                                       弹队列：
                                                          [obj]  → rcu_do() 真正 free()
                                                          [NEXT] → break（本轮结束）
                                                          （FREE(rn) 留给下一轮）
                                                       rcuval += 4 → 5
 T5                                                  seqlock_wait(rcu_seq,5) 阻塞…
```

**注意 T4 里 `[FREE]` 那一项没有在本轮被消费**——它躺在队列里，等待下一次 bump。
这就是"队列永不为空"的实现方式（§3.6）。

### 3.6 为什么队列永不为空：atomlist 的 `last` 指针

这是 FRR RCU 里最"工程化"、最容易被忽略的一环。

[lib/frrcu.c:58-65](lib/frrcu.c#L58-L65) 原文：

> rcu_head structures are the most iffy; normally for an ATOMLIST we would need
> to make sure we use rcu_free or pthread_rwlock to deallocate old items to
> prevent ABA or use-after-free problems. However, our ATOMLIST code guarantees
> that if the list remains non-empty in all cases, we only need the "last"
> pointer to do an "add_tail()", i.e. we can't run into ABA/UAF issues - but we
> do need to keep at least 1 item on the list.

翻译：**"删除队列尾项"这件事本身也需要 RCU 保护，否则就是自举（bootstrap）死循环。
FRR 的解法是——干脆永远不删最后一项。"**

理由是 atomlist 的实现：`add_tail` 会读取 `last` 指针并做 CAS，
如果 `last` 指向的项**正在被删除**，它有一条 "last item is being deleted - start over"
的退避路径。如果队列被清空、`last` 指针指向已释放内存，`add_tail` 就会访问到
**已经被 RCU 回收的旧队列项**——完美的 use-after-free。

具体到 [lib/frrcu.c:350-361](lib/frrcu.c#L350-L361) 的注释：如果 `RCUA_FREE(rn)`
和 `RCUA_NEXT` 是同一个动作，那么 `RCUA_NEXT` 被弹出并执行时，
**整个队列可能刚好变空**，下一轮才 pop 的项就成了"被删掉的 last"。

于是 FRR 选择：**每次 bump 往队列里塞 2 项**（NEXT 哨兵 + 释放 `rn` 自己），
保证"弹出 NEXT 之后队列里至少还有 FREE 那项"。队列永远不空，
`last` 指针永远有效，`add_tail` 永远走快路径。

**这个设计还有个额外好处**：哨兵与"释放自己"解耦后，`RCUA_NEXT` 是共享的
`static const`（[lib/frrcu.c:120](lib/frrcu.c#L120)），不带任何个体信息，
所以清扫线程**不需要知道哪个 NEXT 对应哪个纪元**
（[lib/frrcu.c:321-324](lib/frrcu.c#L321-L324)）。

### 3.7 `rcu_do()`：真正执行回收

[lib/frrcu.c:490-519](lib/frrcu.c#L490-L519)：

```c
static void rcu_do(struct rcu_head *rh)
{
	struct rcu_head_close *rhc;
	void *p;

	switch (rh->action->type) {
	case RCUA_FREE:
		p = (char *)rh - rh->action->u.free.offset;
		if (rh->action->u.free.mt)
			qfree(rh->action->u.free.mt, p);
		else
			free(p);
		break;
	case RCUA_CLOSE:
		rhc = container_of(rh, struct rcu_head_close, rcu_head);
		close(rhc->fd);
		break;
	case RCUA_CALL:
		p = (char *)rh - rh->action->u.call.offset;
		rh->action->u.call.fptr(p);
		break;

	case RCUA_INVALID:
	case RCUA_NEXT:
	case RCUA_END:
	default:
		assert(0);
	}
}
```

要点：

1. **指针反推**：`p = (char *)rh - offset`，这是 `rcu_free` 里 `offsetof` 的用武之地。
2. **`mt == NULL` 走裸 `free()`**（[lib/frrcu.h:217](lib/frrcu.h#L217) 明示
   "may be called with NULL mt to do non-MTYPE free()"）。
3. **内部类型到这一步就是 bug**：`assert(0)`。因为 `RCUA_NEXT`/`RCUA_END`
   在 `rcu_main` 的循环里就被拦下了（[lib/frrcu.c:569-572](lib/frrcu.c#L569-L572)），
   根本传不到这里。
4. `RCUA_CALL` 的 `fptr` 是 `void (*)(void *)`，参数是"容器对象地址"，
   **回调在清扫线程上下文执行**——绝对不能在里面做需要等宽限期的事（自死锁）。

### 3.8 `rcu_start()`：清扫线程的诞生

[lib/frrcu.c:448-485](lib/frrcu.c#L448-L485)：

```c
	sigemptyset(&blocksigs);
	frr_sigset_add_mainonly(&blocksigs);
	pthread_sigmask(SIG_BLOCK, &blocksigs, &oldsigs);   /* 子线程继承掩码 */

	rcu_active = true;

	assert(!pthread_create(&rcu_pthread, NULL, rcu_main, NULL));

	pthread_sigmask(SIG_SETMASK, &oldsigs, NULL);
	...
	pthread_setname_np(rcu_pthread, "RCU sweeper");
```

| 细节 | 原因 |
|---|---|
| 先屏蔽 `frr_sigset_add_mainonly()` 的信号，再创建线程 | **子线程继承信号掩码**，这样清扫线程永远不会被信号打断 |
| `rcu_active = true` 在 `pthread_create` **之前** | 保证清扫线程一启动就能看到正确状态 |
| 线程名 `"RCU sweeper"` | `ps -L` / `top -H` 里可直接看到 |

[lib/frrcu.c:455-464](lib/frrcu.c#L455-L464) 有一段少见的坦诚注释：

> technically, the RCU thread is very poorly suited to run even just a crashlog
> handler, since zlog_sigsafe() could deadlock on transiently invalid (due to
> RCU) logging data structures
> but given that when we try to write a crashlog, we're already in b0rked
> territory anyway - give the crashlog handler a chance.

即：**清扫线程跑崩溃日志处理器理论上会死锁**（日志目标链正在被 RCU 改），
但既然都要写 crashes 了，就赌一把。这是有意为之的"最后手段"。

**清扫线程何时启动？** 不在 `rcu_preinit()`（构造函数）里，而在
`rcu_thread_prepare()`（[lib/frrcu.c:259-272](lib/frrcu.c#L259-L272)）：

```c
	rcu_assert_read_locked();
	if (!rcu_active)
		rcu_start();
```

**即"第一次要创建真线程时才启动清扫线程"**——单线程程序（如 vtysh）永远不会启动它，
`rcu_active` 一直是 false，RCU 全程退化为直接释放（§1.4）。

### 3.9 线程注册与注销

| 时机 | 函数 | 做什么 |
|---|---|---|
| 程序启动（构造函数） | `rcu_preinit()` [lib/frrcu.c:203](lib/frrcu.c#L203) | 主线程 `depth=1`、`rcu_seq=STARTVAL`、建 TLS key、把自己挂上 `rcu_threads` |
| 创建新线程前（父线程） | `rcu_thread_prepare()` [lib/frrcu.c:259](lib/frrcu.c#L259) | 断言持读锁 → 必要时 `rcu_start()` → `rcu_thread_new(rcu_self())` |
| 新线程入口内 | `rcu_thread_start(rt)` [lib/frrcu.c:275](lib/frrcu.c#L275) | `pthread_setspecific()` 绑定 TLS |
| 线程退出（TLS 析构） | `rcu_thread_end()` → `rcu_thread_unprepare()` [lib/frrcu.c:282](lib/frrcu.c#L282) | 见下 |

`rcu_thread_new()`（[lib/frrcu.c:234-252](lib/frrcu.c#L234-L252)）有一条重要语义：

```c
	/* new thread always starts with rcu_read_lock held at depth 1, and
	 * holding the same epoch as the parent (this makes it possible to
	 * use RCU for things passed into the thread through its arg)
	 */
	rt = XCALLOC(MTYPE_RCU_THREAD, sizeof(*rt));
	rt->depth = 1;

	seqlock_init(&rt->rcu);
	if (cur)
		seqlock_acquire(&rt->rcu, &cur->rcu);

	rcu_threads_add_tail(&rcu_threads, rt);
```

**新线程诞生时就"已经持锁"（depth=1），并继承父线程的纪元。**
这解决了一个经典启动难题：父线程想通过 `arg` 把 RCU 保护的数据传进新线程，
但新线程"还没来得及调 rcu_read_lock"——继承纪元就补上了这个空窗。

**副作用**：新线程如果跑的不是标准事件循环，就永远停在 depth=1，
卡住所有宽限期（§5.3 的 `bgp_keepalives` 就是这么处理的）。

`rcu_thread_unprepare()`（[lib/frrcu.c:282-297](lib/frrcu.c#L282-L297)）：

```c
	if (rt == &rcu_thread_rcu)
		return;                                  /* 清扫线程自己：什么都不做 */
	rt->depth = 1;
	seqlock_acquire(&rt->rcu, &rcu_seq);         /* 重新钉到当前纪元 */
	rcu_bump();                                  /* 手动推一代，让"我退出"这件事可见 */
	if (rt != &rcu_thread_main)
		/* this free() happens after seqlock_release() below */
		rcu_free_internal(MTYPE_RCU_THREAD, rt, rcu_head);
	rcu_threads_del(&rcu_threads, rt);
	seqlock_release(&rt->rcu);
```

妙处在于：**`rcu_thread` 结构体自己也用 RCU 释放**。线程退出后，清扫线程可能
正好在遍历 `rcu_threads` 链表上拿到它的指针，所以不能当场 `free`。
`rcu_free_internal` 把它挂上 `rcu_heads`，下一次宽限期后才真正释放。

主线程的 `rcu_thread_main` 是静态变量，不释放。

### 3.10 `rcu_shutdown()`：有序收场

[lib/frrcu.c:596-624](lib/frrcu.c#L596-L624)：

```c
void rcu_shutdown(void)
{
	static struct rcu_head rcu_head_end;
	struct rcu_thread *rt = rcu_self();
	void *retval;

	if (!rcu_active)
		return;                                  /* 单线程：无事可做 */

	rcu_assert_read_locked();
	assert(rcu_threads_count(&rcu_threads) == 1); /* 必须只剩主线程 */

	rcu_enqueue(&rcu_head_end, &rcua_end);

	rt->depth = 0;
	seqlock_release(&rt->rcu);
	seqlock_release(&rcu_seq);
	rcu_active = false;

	/* clearing rcu_active is before pthread_join in case we hang in
	 * pthread_join & get a SIGTERM or something - in that case, just
	 * ignore the maybe-still-running RCU thread
	 */
	if (pthread_join(rcu_pthread, &retval) == 0) {
		seqlock_acquire_val(&rcu_seq, SEQLOCK_STARTVAL);
		seqlock_acquire_val(&rt->rcu, SEQLOCK_STARTVAL);
		rt->depth = 1;
	}
}
```

四个值得琢磨的点：

1. **`assert(rcu_threads_count(&rcu_threads) == 1)`**——这就是"必须单线程调用"的强制检查。
   调用点在 [lib/libfrr.c:1449](lib/libfrr.c#L1449) 的 `frr_fini()`，位于
   `frr_pthread_finish()`（[lib/libfrr.c:1427](lib/libfrr.c#L1427)）之后，
   此时所有工作线程都已 join 完毕。
2. **`rcu_active = false` 放在 `pthread_join` 之前**——注释原文解释得很清楚：
   万一 join 卡住又收到 SIGTERM，此时 `rcu_active` 已是 false，
   任何还在跑的 `rcu_enqueue` 会走同步快速路径，不会往"没人消费的队列"里塞垃圾导致卡死退出。
3. **join 成功才重置 seqlock**——失败（`pthread_join` 返回非 0）说明清扫线程状态未知，
   就不复位了。
4. **重置的意义**：把 `rcu_seq` 和 `rt->rcu` 拨回 `SEQLOCK_STARTVAL(1)`、`depth = 1`，
   让 RCU 回到"刚初始化完"的状态，理论上可再次启用。

最后 `rcu_head_end` 是**函数内 static**，因为它的生命周期必须延续到清扫线程消费它为止
（栈变量会立刻失效）。

---

## 4. API 参考

### 4.1 写侧（"删除"一方）

| API | 位置 | 语义 |
|---|---|---|
| `rcu_free(mtype, ptr, field)` | [lib/frrcu.h:218](lib/frrcu.h#L218) | 延迟释放 `ptr`；`field` 是内嵌的 `rcu_head` 字段名 |
| `rcu_close(head, fd)` | [lib/frrcu.h:256](lib/frrcu.h#L256) | 延迟关闭 fd |
| `rcu_call(func, ptr, field)` | [lib/frrcu.h:238](lib/frrcu.h#L238) | 延迟调用 `func(ptr)`；**跑在清扫线程上，全树无人使用** |
| `rcu_enqueue(head, action)` | [lib/frrcu.h:203](lib/frrcu.h#L203) | 底层接口，一般用宏而不直接调 |

`rcu_free` 宏展开（[lib/frrcu.h:218-232](lib/frrcu.h#L218-L232)）：

```c
#define rcu_free(mtype, ptr, field)                                            \
	do {                                                                   \
		typeof(ptr) _ptr = (ptr);                                      \
		if (!_ptr)                                                     \
			break;                                                 \
		struct rcu_head *_rcu_head = &_ptr->field;                     \
		static const struct rcu_action _rcu_action = {                 \
			.type = RCUA_FREE,                                     \
			.u.free = {                                            \
				.mt = mtype,                                   \
				.offset = offsetof(typeof(*_ptr), field),      \
			},                                                     \
		};                                                             \
		rcu_enqueue(_rcu_head, &_rcu_action);                          \
	} while (0)
```

**四个必须理解的宏特性**：

| 特性 | 后果 |
|---|---|
| `if (!_ptr) break;` | **传 NULL 是安全的**，不必自己判空 |
| `static const struct rcu_action` | 每个调用点一份，**共享**——所以 `mtype` 必须是编译期常量 |
| `offsetof(typeof(*_ptr), field)` | 编译器算出偏移，**零运行时开销**；`field` 必须固定 |
| `typeof(ptr)` | 有类型检查，写错字段会编译报错 |

`rcu_call` 多一行 `(void)(_fptype = func);`（[lib/frrcu.h:250](lib/frrcu.h#L250)），
这是一次**编译期类型检查**：如果 `func` 的参数类型与 `ptr` 不匹配就编译失败。

`rcu_enqueue()` 的两条路径（[lib/frrcu.c:633-649](lib/frrcu.c#L633-L649)）：

```c
	rcu_assert_read_locked();        /* 调用者必须持有读锁！ */
	rh->action = action;

	if (!rcu_active) {
		rcu_do(rh);              /* 单线程：同步立即执行 */
		return;
	}
	rcu_heads_add_tail(&rcu_heads, rh);
	atomic_store_explicit(&rcu_dirty, seqlock_cur(&rcu_seq), memory_order_relaxed);
```

**"必须持有读锁"是硬性要求**（原因见 §3.3 的 add_tail 中断点问题），
由断言强制执行；release 构建下断言消失，违反的后果是**静默的队列损坏**。

### 4.2 读侧

| API | 位置 | 说明 |
|---|---|---|
| `rcu_read_lock()` | [lib/frrcu.h:133](lib/frrcu.h#L133) | 可嵌套；最外层才动原子量 |
| `rcu_read_unlock()` | [lib/frrcu.h:134](lib/frrcu.h#L134) | 必须连续持有，中间不能有别的 unlock |
| `rcu_assert_read_locked()` | [lib/frrcu.h:139](lib/frrcu.h#L139) | 调试断言 |
| `rcu_assert_read_unlocked()` | [lib/frrcu.h:140](lib/frrcu.h#L140) | 调试断言 |

### 4.3 线程管理

| API | 位置 | 谁调用 |
|---|---|---|
| `rcu_thread_prepare()` | [lib/frrcu.h:103](lib/frrcu.h#L103) | 父线程，`pthread_create` 之前 |
| `rcu_thread_start(rt)` | [lib/frrcu.h:117](lib/frrcu.h#L117) | 新线程入口最开头 |
| `rcu_thread_new(arg)` | [lib/frrcu.h:92](lib/frrcu.h#L92) | 非受控线程自建 |
| `rcu_thread_unprepare(rt)` | [lib/frrcu.h:108](lib/frrcu.h#L108) | `pthread_create` 失败时；正常退出由 TLS 析构自动完成 |
| `rcu_shutdown()` | [lib/frrcu.h:128](lib/frrcu.h#L128) | `frr_fini()`，单线程时 |

> thread exit is handled through pthread_key_create's destructor function
> —— [lib/frrcu.h:119](lib/frrcu.h#L119)

**正常退出不需要手动调 `rcu_thread_unprepare()`**，TLS 析构函数
（[lib/frrcu.c:300-304](lib/frrcu.c#L300-L304)）会自动调用。
手动调只有一个场景：`pthread_create` 失败，线程从未诞生。

---

## 5. 与其它子系统的集成

### 5.1 `frr_pthread` 的三个钩子

| 位置 | 代码 | 作用 |
|---|---|---|
| [lib/frr_pthread.c:247](lib/frr_pthread.c#L247) | `fpt->rcu_thread = rcu_thread_prepare();` | 创建线程**之前**，在父线程上下文里预建状态 |
| [lib/frr_pthread.c:261](lib/frr_pthread.c#L261) | `rcu_thread_unprepare(fpt->rcu_thread);` | `pthread_create` 失败的清理 |
| [lib/frr_pthread.c:222](lib/frr_pthread.c#L222) | `rcu_thread_start(fpt->rcu_thread);` | 新线程入口 `frr_pthread_inner` 里绑定 TLS |

`frr_pthread_run()` 里的完整序列（含信号屏蔽）：

```c
	assert(frr_is_after_fork || !"trying to start thread before fork()");
	sigemptyset(&blocksigs);
	frr_sigset_add_mainonly(&blocksigs);
	pthread_sigmask(SIG_BLOCK, &blocksigs, &oldsigs);
	fpt->rcu_thread = rcu_thread_prepare();      /* ← RCU 注册 */
	ret = pthread_create(&fpt->thread, attr, frr_pthread_inner, fpt);
	pthread_sigmask(SIG_SETMASK, &oldsigs, NULL);
```

**注意 `rcu_thread_prepare()` 是在信号被屏蔽的窗口里执行的**——
这保证了 RCU 注册与线程创建之间不会被信号插队。

**非受控线程**（不由 `frr_pthread_new` 管理）走另一条路
（[lib/frr_pthread.c:333-348](lib/frr_pthread.c#L333-L348)）：

```c
	struct rcu_thread *rcu_thread = rcu_thread_new(NULL);
	rcu_thread_start(rcu_thread);
	...
	fpt->rcu_thread = rcu_thread;
```

区别是 `rcu_thread_new(NULL)` 传 NULL 作为"父线程"，所以**不继承任何纪元**
（[lib/frrcu.c:246-247](lib/frrcu.c#L246-L247) 的 `if (cur)`）。

### 5.2 事件循环：`fd_poll()` 里主动放下读锁

这是整个 FRR 里 RCU 与事件循环咬合最深的一处
（[lib/event.c:1250](lib/event.c#L1250) 起）：

```c
static int fd_poll(struct event_loop *m, const struct timeval *timer_wait,
		   bool *eintr_p)
{
	...
	zlog_tls_buffer_flush();
	rcu_read_unlock();               /* ← 进 poll() 之前放掉 */
	rcu_assert_read_unlocked();
	...
	/* epoll_wait / ppoll / poll 阻塞在这里 */
	...
	rcu_read_lock();                 /* ← 回来立刻重新拿上 */
	return num;
}
```

对应的规范原文 [doc/developer/rcu.rst:120-125](doc/developer/rcu.rst#L120-L125)：

> The ``thread_master`` code currently always holds RCU everywhere, except
> while doing the actual ``poll()`` syscall. This is both an optimization as
> well as an "easement" into getting RCU going. The current implementation
> contract is that any ``struct event *`` callback is called with a RCU
> holding depth of 1, and that this is owned by the thread so it may (should)
> drop and reacquire it when doing some longer-running work.

三句话拆开看：

1. **事件循环线程默认全程持有 RCU 读锁**（depth=1）。这是"默认打开"的设计——
   几乎没人会忘。
2. **唯一放开的地方是阻塞在 `poll()` 时**。否则一个空闲守护进程会永远卡在旧纪元上，
   宽限期永远结束不了，RCU 队列无限堆积。
3. **事件回调都是在 depth=1 下被调用的**，而且这个锁"属于线程"，
   回调里做长耗时工作**可以（也应该）主动 `rcu_read_unlock()` / `rcu_read_lock()`**。

**这正是 `bgp_keepalives` 那段注释所依赖的契约**（见 §5.3）。

顺序细节：`zlog_tls_buffer_flush()` 在 `rcu_read_unlock()` **之前**——
先把线程局部日志缓冲冲出去（此时它还需要 RCU），再放锁去睡。

### 5.3 非受控线程：`bgp_keepalives` 的手动解锁

全树 bgpd 里**唯一**的 RCU 调用是这一行
（[bgpd/bgp_keepalives.c:226](bgpd/bgp_keepalives.c#L226)），注释写得极好：

```c
	/*
	 * The RCU mechanism for each pthread is initialized in a "locked"
	 * state. That's ok for pthreads using the frr_pthread,
	 * event_fetch event loop, because that event loop unlocks regularly.
	 * For foreign pthreads, the lock needs to be unlocked so that the
	 * background rcu pthread can run.
	 */
	rcu_read_unlock();
```

翻译：**每个新线程出生时 `depth == 1`（§3.9）。标准 `frr_pthread` 事件循环会
在 `fd_poll()` 里规律地 unlock/lock，所以没关系；但"野线程"不会，
它必须自己解锁一次，否则清扫线程永远等不到它离开纪元，整个 RCU 停摆。**

这是一个**极易踩的坑**：新建线程如果不跑 FRR 事件循环，就必须手动
`rcu_read_unlock()`（并且注意 `depth` 变成 0 之后，任何 `rcu_free`/`rcu_enqueue`
都会触发 `rcu_assert_read_locked()` 失败——所以该线程要么不再碰 RCU，
要么自己在临界区内外成对 lock/unlock）。

### 5.4 启动与关闭时序

```
程序启动
  └─ (constructor) rcu_preinit()                [frrcu.c:203]
       ├─ 主线程 rcu_thread_main: depth=1, rcu=STARTVAL
       ├─ pthread_key_create(rcu_thread_key, rcu_thread_end)
       ├─ rcu_threads_add_tail(主线程)
       ├─ 清扫线程的 rcu_thread_rcu: depth=1（不入链表）
       └─ rcu_seq = SEQLOCK_STARTVAL
     （此时 rcu_active 仍为 false —— 单线程，RCU 是空操作）

首次创建线程
  └─ frr_pthread_run()                          [frr_pthread.c:247]
       └─ rcu_thread_prepare()
            ├─ rcu_assert_read_locked()
            ├─ rcu_start()  → rcu_active = true, pthread_create(rcu_main)
            │                   信号屏蔽继承，线程名 "RCU sweeper"
            └─ rcu_thread_new(父线程) → 继承父纪元, depth=1, 挂 rcu_threads

运行期
  └─ 每个事件循环线程：fd_poll() 前后 unlock/lock  [event.c:1263/1391]

退出
  └─ frr_fini()                                 [libfrr.c:1427-1449]
       ├─ frr_pthread_finish();        /* 各工作线程被 join 掉，rcu_thread 随之注销 */
       ├─ event_master_free(master);
       ├─ zlog_tls_buffer_fini();
       └─ rcu_shutdown();              /* 此时 rcu_threads 只剩主线程 → 断言通过 */
            ├─ assert(rcu_threads_count == 1)
            ├─ rcu_enqueue(RCUA_END)
            ├─ depth=0, release 两个 seqlock, rcu_active = false
            └─ pthread_join(rcu_pthread)
```

**顺序不能颠倒**：`rcu_shutdown()` 必须在所有工作线程都停掉之后调用，
否则 `assert(rcu_threads_count(&rcu_threads) == 1)` 会失败。

### 5.5 日志子系统：写侧/读侧分离的标杆

zlog 是 FRR 中 RCU 的最大用户，它把 RCU 的"读写分离"发挥得很典型：

```
写侧（配置变更，极少）                     读侧（每条日志，极频繁）
lib/zlog_targets.c                         lib/zlog.c
lib/zlog_5424.c                            ├─ zlog_tls_buffer_flush()  [zlog.c:520]
lib/zlog_live.c                            │    rcu_read_lock()  @530
  ├─ zlog_target_clone()  造新副本          │    frr_each_safe(zlog_targets) 遍历
  ├─ zlog_target_replace() 原子换链         │    rcu_read_unlock() @537
  └─ rcu_close() + rcu_free() 延迟回收旧副本 └─ vzlog_notls()  [zlog.c:565]
                                                早期丢弃扫描  [zlog.c:599]
                                                sigsafe 路径  [zlog.c:737]
```

**契约**（[lib/zlog.h:383-397](lib/zlog.h#L383-L397)、
[doc/developer/logging.rst:856-865](doc/developer/logging.rst#L856-L865)）：

> The target remains possibly in use by other threads until the RCU cycle ends.
> This implies you cannot release resources (e.g. memory, file descriptors)
> immediately. ... ``oldzt`` must remain **functional** until the RCU cycle ends.

并且诚实标注了一个**已知非原子窗口**：

> The replace operation is not atomic; for a brief period it is possible that
> messages are delivered on both ``oldzt`` and ``newzt``.

即**短暂重复投递日志是可接受的**——这是 RCU 换链的必然结果，也是对使用者的明确期望管理。

`zlog_target_free()` 宏就是一行包装（[lib/zlog.h:403-404](lib/zlog.h#L403-L404)）：

```c
#define zlog_target_free(mt, zt) \
	rcu_free(mt, zt, rcu_head)
```

---

## 6. 真实案例研究

### 6.1 `zlog_targets.c`：延迟 close + 延迟 free 的标准两连

目标结构内嵌一个"延迟关 fd"的头（[lib/zlog_targets.c:60-70](lib/zlog_targets.c#L60-L70)）：

```c
struct zlt_fd {
	struct zlog_target zt;      /* 基类，内含 struct rcu_head rcu_head */

	atomic_uint_fast32_t fd;

	char ts_subsec;
	bool record_priority;
	bool record_severity;

	struct rcu_head_close head_close;   /* 专门用来延迟 close(fd) */
};
```

释放函数（[lib/zlog_targets.c:244-251](lib/zlog_targets.c#L244-L251)）：

```c
static void zlog_file_target_free(struct zlt_fd *zlt)
{
	if (!zlt)
		return;

	rcu_close(&zlt->head_close, zlt->fd);
	rcu_free(MTYPE_LOG_FD, zlt, zt.rcu_head);
}
```

**两个 head 的分工必须分清**：

| 头 | 内嵌在 | 动作 | 为什么不能合并 |
|---|---|---|---|
| `zt.rcu_head` | `struct zlog_target`（基类） | `RCUA_FREE` 释放整个 `zlt_fd` | 用基类字段的 offset，可释放任意派生类型 |
| `head_close` | `struct zlt_fd`（派生类） | `RCUA_CLOSE` 关闭 `fd` | 关 fd 与释放内存是**两个不同时刻**的动作 |

注意 `rcu_free(MTYPE_LOG_FD, zlt, zt.rcu_head)`：`ptr` 是 `zlt`，
`field` 是 `zt.rcu_head`——**字段可以跨一层**，`offsetof` 会算出正确的整体偏移。

**文件轮转（rotate）的经典写法**（[lib/zlog_targets.c:381-417](lib/zlog_targets.c#L381-L417)）：

```c
struct rcu_close_rotate {
	struct rcu_head_close head_close;
	struct rcu_head head_self;
};

bool zlog_file_rotate(struct zlog_cfg_file *zcf)
{
	struct rcu_close_rotate *rcr;
	int fd;

	frr_with_mutex (&zcf->cfg_mtx) {
		if (!zcf->active || !zcf->filename)
			return true;

		fd = open(zcf->filename,
			  O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOCTTY,
			  LOGFILE_MASK);
		if (fd < 0)
			return false;

		fd = atomic_exchange_explicit(&zcf->active->fd,
					      (uint_fast32_t)fd,
					      memory_order_relaxed);
	}

	rcr = XCALLOC(MTYPE_LOG_FD_ROTATE, sizeof(*rcr));
	rcu_close(&rcr->head_close, fd);
	rcu_free(MTYPE_LOG_FD_ROTATE, rcr, head_self);
}
```

**这个函数是"缩减版 RCU"（§1.2）的教科书示例**：

1. **不复制目标对象**——用 `atomic_exchange_explicit` 原地把 fd 换掉。
2. **旧 fd 不能立即 close**——其它线程可能正在往里写（日志写是 lockless 的）。
3. **需要一个临时对象 `rcu_close_rotate`** 来承载"关这个 fd"这个动作，
   而它有**两个 head**：一个用于关 fd，一个用于释放自己。
4. 为什么必须两个？因为 `rcu_close` 的动作是 `static const` 共享的
   `rcua_close`（[lib/frrcu.c:122](lib/frrcu.c#L122)），
   **无法同时表达"关 fd"和"释放容器"**——一个 head 只能挂一次队（且不可复用）。
5. 读侧完全无锁：`atomic_load_explicit(&zte->fd, memory_order_relaxed)`
   （[lib/zlog_targets.c:117](lib/zlog_targets.c#L117)、
   [lib/zlog_targets.c:218](lib/zlog_targets.c#L218)）。

**完整替换路径**（[lib/zlog_targets.c:305-324](lib/zlog_targets.c#L305-L324)）：

```c
		zt = zlog_target_clone(MTYPE_LOG_FD, ..., sizeof(*zlt));
		zlt = container_of(zt, struct zlt_fd, zt);
	...
	old = zlog_target_replace(zcf->active ? &zcf->active->zt : NULL,
				  zlt ? &zlt->zt : NULL);
	zcf->active = zlt;

	zlog_file_target_free(container_of_null(old, struct zlt_fd, zt));
```

顺序是 **clone → replace（原子换链）→ 释放旧副本（延迟）**。
`container_of_null` 用于处理 `old == NULL` 的首次配置场景。

### 6.2 `zlog_5424.c`：一个值得学习的细节——fd 没变就不关

[lib/zlog_5424.c:902-911](lib/zlog_5424.c#L902-L911)：

```c
	old = zcf->active ? &zcf->active->zt : NULL;
	old = zlog_target_replace(old, zlt ? &zlt->zt : NULL);
	zcf->active = zlt;

	/* oldt->fd == fd happens for zlog_5424_apply_meta() */
	oldt = container_of(old, struct zlt_5424, zt);
	if (oldt && oldt->fd != (unsigned int)fd)
		rcu_close(&oldt->head_close, oldt->fd);
	rcu_free(MTYPE_LOG_5424, oldt, zt.rcu_head);
```

**`if (oldt->fd != fd)` 这一句是判断"fd 是否被复用/未变"**。
如果只是改了元数据（`zlog_5424_apply_meta()`），新旧目标的 fd 是同一个，
此时若照样 `rcu_close()`，就会**把正在使用的新 fd 关掉**——一个非常隐蔽的自杀式 bug。
这个判空 + 判等是必查项。

另外[lib/zlog_5424.c:913-918](lib/zlog_5424.c#L913-L918) 的注释解释了为什么
"重启连接"不能简单 `close()` 后重开：

> 于是重新 zlog_5424_open 打开新连接，加锁后用 zlog_5424_cycle 替换目标。

**因为别的线程可能正拿着旧 fd 在写**，所以必须"开新的 → 换链 → RCU 延迟关旧的"。

### 6.3 `zlog_live.c`：双所有者交接（RCU 之外的另一层技巧）

[lib/zlog_live.c:64-73](lib/zlog_live.c#L64-L73)：

```c
struct zlt_live {
	struct zlog_target zt;

	atomic_uint_fast32_t fd;
	struct rcu_head_close head_close;
	struct rcu_head head_self;

	atomic_uint_fast32_t state;
	atomic_uint_fast32_t lost_msgs;
};
```

**唯一同时内嵌 `head_close` 和 `head_self` 的目标类型**，因为 clean 路径
（`zlog_live_close`）和异常路径（写失败）**都可能触发清理**，
而 `rcu_free` **只能调用一次**（head 不可复用，§0.5 不变量 4）。

`state` 字段就是为此存在的两方交接协议：

| 状态 | 含义 | 谁负责 `rcu_free` |
|---|---|---|
| `STATE_NORMAL` | 正常使用中 | — |
| `STATE_FD_DEAD` | 写失败已判死 | **后到者**（见 [lib/zlog_live.c:341-346](lib/zlog_live.c#L341-L346)） |
| `STATE_DISOWNED` | 已被 close/disown | **后到者**（见 [lib/zlog_live.c:189-197](lib/zlog_live.c#L189-L197)） |

两个方向都指向同一句：

```c
	if (state == STATE_FD_DEAD)
		rcu_free(MTYPE_LOG_LIVE, zte, head_self);
	...
	if (state == STATE_DISOWNED)
		rcu_free(MTYPE_LOG_LIVE, zte, head_self);
```

**核心思路：谁把 state 推进到"终态"，谁就拿到了唯一的 `rcu_free` 权利。**
用原子状态机（而非锁）保证"恰好释放一次"。这是在 RCU 之上再叠一层无锁协议的范例。

### 6.4 全树 RCU 消费者清单（10.6.1 实测）

| 文件:行 | 调用 | 角色 |
|---|---|---|
| [lib/zlog.c:530](lib/zlog.c#L530) / [537](lib/zlog.c#L537) | `rcu_read_lock/unlock` | 刷新 TLS 日志缓冲时遍历目标链 |
| [lib/zlog.c:565](lib/zlog.c#L565) / [574](lib/zlog.c#L574) | 同上 | `vzlog_notls()` |
| [lib/zlog.c:599](lib/zlog.c#L599) / [606](lib/zlog.c#L606) | 同上 | 早期丢弃扫描 |
| [lib/zlog.c:737](lib/zlog.c#L737) / [746](lib/zlog.c#L746) | 同上 | sigsafe / 消息路径 |
| [lib/event.c:1263](lib/event.c#L1263) | `rcu_read_unlock` | `fd_poll()` 阻塞前放下 |
| [lib/event.c:1391](lib/event.c#L1391) | `rcu_read_lock` | `fd_poll()` 返回后拿回 |
| [lib/event.c:1264](lib/event.c#L1264) | `rcu_assert_read_unlocked` | 断言确实放下了 |
| [bgpd/bgp_keepalives.c:226](bgpd/bgp_keepalives.c#L226) | `rcu_read_unlock` | 野线程手动解锁 |
| [lib/zlog_targets.c:249](lib/zlog_targets.c#L249) / [250](lib/zlog_targets.c#L250) | `rcu_close` / `rcu_free` | 文件目标释放 |
| [lib/zlog_targets.c:413](lib/zlog_targets.c#L413) / [414](lib/zlog_targets.c#L414) | 同上 | 轮转辅助对象释放 |
| [lib/zlog_5424.c:820](lib/zlog_5424.c#L820) / [821](lib/zlog_5424.c#L821) | 同上 | 5424 目标释放 |
| [lib/zlog_5424.c:909](lib/zlog_5424.c#L909) / [910](lib/zlog_5424.c#L910) | 同上 | 目标替换 |
| [lib/zlog_5424.c:1317](lib/zlog_5424.c#L1317) / [1318](lib/zlog_5424.c#L1318) | 同上 | 轮转辅助对象释放 |
| [lib/zlog_live.c:189](lib/zlog_live.c#L189) / [197](lib/zlog_live.c#L197) | 同上 | 错误路径 |
| [lib/zlog_live.c:319](lib/zlog_live.c#L319) / [322](lib/zlog_live.c#L322) | 同上 | `zlog_live_close()` |
| [lib/zlog_live.c:346](lib/zlog_live.c#L346) | `rcu_free` | `zlog_live_disown()` |
| [lib/libfrr.c:1449](lib/libfrr.c#L1449) | `rcu_shutdown` | 进程退出 |
| [lib/frr_pthread.c:222](lib/frr_pthread.c#L222) / [247](lib/frr_pthread.c#L247) / [261](lib/frr_pthread.c#L261) / [333-335](lib/frr_pthread.c#L333-L335) | `rcu_thread_*` | 线程注册 |

**结论：FRR 的 RCU 使用面非常窄——只有 libfrr 核心 + lib/zlog + bgpd 的保活线程。**

明确**不使用** RCU 的：`isisd/`、`ospfd/`、`ospf6d/`、`zebra/`、`bfdd/`、`ldpd/`、
`pimd/`、`ripd/`、`staticd/`、`mgmtd/`、`pathd/`、`tests/`（全树 grep 零命中）。
**`struct rcu_head` 成员在全树只出现在 5 个文件里**（`zlog.h`、`zlog_targets.c`、
`zlog_5424.c`、`zlog_live.c` + `frrcu.c` 自身）。

### 6.5 反例：`zlog` 的 TLS 缓冲**不用** RCU

一个很有教育意义的对比。[lib/zlog.c](lib/zlog.c) 里有一个每线程的 mmap 日志缓冲
`struct zlog_tls`，它的销毁是**直接释放**的（[lib/zlog.c:504-513](lib/zlog.c#L504-L513)）：

```c
static inline void zlog_tls_free(void *arg)
{
	struct zlog_tls *zlog_tls = arg;

	if (!zlog_tls)
		return;

	munmap(zlog_tls->mmbuf, TLS_LOG_BUF_SIZE);
	XFREE(MTYPE_LOG_TLSBUF, zlog_tls);
}
```

**为什么不用 RCU？** 因为它**只被本线程访问**（TLS），
生命周期由 pthread key 析构函数管理，没有跨线程可见性——
**没有读者，就不需要宽限期。**

> 判断要不要用 RCU 的第一问应该是：**"这块数据是否可能被其它线程读到？"**
> 如果答案是否定的，任何 RCU 机制都是纯开销。

---

## 7. 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | `rcu_free`/`rcu_close`/`rcu_enqueue` 时**没有持有读锁** | release 构建下无断言 → 队列结构被破坏、静默 UAF | 调用前确保 `rcu_read_lock()` 已生效（debug 构建有 `rcu_assert_read_locked()` 兜底） |
| 2 | 持有读锁期间**断开**读锁（哪怕只是短暂 unlock 再 lock） | 期间对象可能已被回收 → UAF | 读锁必须**连续**持有（[rcu.rst:127-134](doc/developer/rcu.rst#L127-L134)） |
| 3 | 认为"我持着读锁，所以我刚 free 的对象还能用" | **FRR 明确不是这样**，尤其单线程时对象真的没了 | 记住 [frrcu.h:69-72](lib/frrcu.h#L69-L72) 的设计取舍 |
| 4 | **复用**一个已经传给 `rcu_*` 的 `rcu_head` | 同一节点被挂两次 → 链表损坏 | 一个 head 只能入队一次；需要多次就用多个 head（见 `zlog_live` 的 `head_close`+`head_self`） |
| 5 | 在事件回调里做耗时/阻塞工作而不放锁 | 该线程钉住旧纪元 → 宽限期无限延长 → RCU 队列堆积、内存不释放 | 长操作前后成对 `rcu_read_unlock()` / `rcu_read_lock()` |
| 6 | 新建"野线程"（不跑 `frr_pthread` 事件循环）后不处理 RCU | 线程永远停在 depth=1 → **整个 RCU 停摆** | 入口处 `rcu_read_unlock()`（[bgp_keepalives.c:219-226](bgpd/bgp_keepalives.c#L219-L226)） |
| 7 | 用 `rcu_call()` 图省事 | 回调**阻塞清扫线程**，所有延迟释放被拖住；回调内再调 RCU 会自死锁 | 全树无人使用——优先用 `rcu_free`/`rcu_close` 表达意图 |
| 8 | 期待有 `rcu_synchronize()` | 编译不过（不存在） | FRR 没有同步等宽限期的 API，用 `rcu_call()` 或改用其它同步手段 |
| 9 | 用 `rcu_dereference` / `rcu_assign_pointer` | 编译不过（不存在） | 用 `atomic_load_explicit`/`atomic_store_explicit`，或 ATOMLIST/ATOMLIST 系列 |
| 10 | 自定义"释放动作"时想把"释放容器"和"关 fd"塞进同一个 head | 做不到（`rcu_action` 是单个 `static const`） | 用两个 head（`zlog_targets` 的 `rcu_close_rotate` 就是为此而生） |
| 11 | 忘记"新 fd 可能与旧 fd 相同" | `rcu_close` 关掉正在使用的新 fd，日志彻底断掉 | 仿照 [zlog_5424.c:909](lib/zlog_5424.c#L909) 的 `if (oldt->fd != fd)` |
| 12 | 在多线程已启动后调用 `rcu_shutdown()` | `assert(rcu_threads_count == 1)` 失败 | 只能在所有工作线程 join 之后调用（`frr_fini()` 的固定顺序） |
| 13 | 试图清空 `rcu_heads` 队列 | 破坏 atomlist `last` 指针不变量 → **访问已释放的队列项** | 队列必须永不为空，这是 [frrcu.c:350-361](lib/frrcu.c#L350-L361) 的硬约束 |
| 14 | 直接调用 `rcu_bump()` | 编译不过（`static`） | 只能通过 `rcu_read_lock/unlock` 间接触发——这是**有意的封装** |
| 15 | 把 `rcu_thread` 的 `depth` 当成原子量跨线程改 | 竞态 + 语义错误 | `depth` 只属于本线程，RCU 读锁**不能跨线程**配对 |
| 16 | 在长期不退出循环的线程里不做任何 RCU 操作 | 它继承的初始纪元（depth=1）一直挂着，宽限期永远等它 | 见 #6：要么手动解锁，要么规律地 lock/unlock |
| 17 | 单线程程序里依赖"延迟释放"语义 | `rcu_active == false` → **立即同步释放** | 记住 [frrcu.c:642-645](lib/frrcu.c#L642-L645) 的快速路径 |
| 18 | 把"读多写少"之外的场景也换成 RCU | 内存双倍占用 + 批量 free 的 malloc 竞争，得不偿失 | 先读 [rcu.rst:56-68](doc/developer/rcu.rst#L56-L68) 的 "When (not) to use RCU" |

---

## 8. 设计精华（读完之后值得回味的几处）

### 8.1 用"队列永不为空"绕开 RCU 自举问题

RCU 要解决的问题是"安全地删除"，但 RCU 自己的队列也在被并发删除
——**这是一个自举（bootstrap）难题**。内核 RCU 用 per-CPU 链表 + 回调批次解决；
FRR 选择了一条极其朴素的路：**结构上保证永不删除最后一项**（§3.6）。

代价是每次 bump 多一次 `XMALLOC`（`struct rcu_next`，[frrcu.c:319](lib/frrcu.c#L319)），
收益是**队列本身不需要任何锁，也不需要递归的 RCU**。对一个 bump 频率低（只在
read_lock/unlock 且 dirty 时）的守护进程来说，这笔交易很划算。

### 8.2 用一个 `seqlock` 表示"我钉住哪一代"

传统 RCU 用 per-CPU 计数器或全局的阶段位图。FRR 因为**没有内核态、线程数少**，
直接给每个线程一个 `struct seqlock`（4 字节 `pos` + 一对 pthread 原语）。

`pos` 的位布局（HELD / WAITERS）让"持有中"和"有人在等"这两个状态**共用一次原子操作**
来表达，`seqlock_acquire_val` / `seqlock_release` 都只是一次
`atomic_exchange_explicit`（[lib/seqlock.c:414-443](lib/seqlock.c#L414-L443)）。
这是把状态机压进一个 32 位整数的漂亮做法。

### 8.3 "多 bump 无害、少 bump 才有害"

`rcu_bump_maybe()`（[frrcu.c:380-391](lib/frrcu.c#L380-L391)）**完全没有 CAS**：

```c
	dirty = atomic_load_explicit(&rcu_dirty, memory_order_relaxed);
	if (dirty == seqlock_cur(&rcu_seq))
		rcu_bump();
```

因为 bump 的本质是"推迟回收"（**保守方向**），所以：
- 多 bump → 纪元跑得快些，回收时机偏晚，**语义仍正确**（甚至更安全）
- 少 bump → 回收延迟，**只是性能问题**

**"让所有竞争都倒在安全的一侧"是并发设计的通用原则**，FRR 在这里用得很彻底。
同样的思路也出现在 `rcu_bump()` 的"入队与 bump 之间没有原子性"注释里
（[frrcu.c:364-373](lib/frrcu.c#L364-L373)）：动作可能被归到更新的纪元，回收晚一点，无害。

### 8.4 "让单线程退化为空操作"如何反向塑造 API

为了让 `rcu_active == false` 时 RCU 彻底消失（§1.4），FRR 必须接受
"读锁内也不能访问已 free 对象"这条**反直觉规则**（§1.3 ③）。

**这是一条"性能换易用性"的典型曲线**：牺牲了开发者的直觉安全感，
换来了 vtysh 这类单线程工具完全不为并发付任何代价。
理解这条因果链，才能真正理解 FRR RCU 的规则为什么长这样。

### 8.5 `fd_poll()` 的一对 unlock/lock：把"空闲"变成"可回收"

[event.c:1263/1391](lib/event.c#L1263) 这一放一收，是整个机制**能运转起来的关键**：

一个典型的守护进程 99% 的时间阻塞在 `poll()` 上。如果不在这里放锁，
**主线程永远是宽限期里最慢的那个**，RCU 队列永远不回收。

> **一旦用 RCU 取代锁，必须成为"好公民"**：让空转的线程释放它对宽限期的压力。

---

## 9. 自测题

1. `rcu_read_lock()` 里为什么要调用两次 `seqlock_acquire(&rt->rcu, &rcu_seq)`？
2. `rcu_bump()` 为什么必须在持有读锁时调用？不持有时会发生什么？
3. `rcu_bump()` 为什么要往队列里塞两项（`RCUA_NEXT` 和 `RCUA_FREE`）？合并成一项会怎样？
4. `rcu_dirty` 和 `rcu_seq` 的关系是什么？`rcu_bump_maybe()` 判断的依据是什么？
5. `rcu_main()` 的 `rcuval` 从 `SEQLOCK_STARTVAL` 开始，每次 `+= SEQLOCK_INCR`。
   它等待 `rt->rcu` 时，"越过"是怎么判断的？`cal < 0x80000000` 代表什么？
6. 清扫线程 100ms 超时后做了什么？为什么超时后还要 `seqlock_wait()` 而不是放弃？
7. `rcu_thread_rcu` 为什么不加入 `rcu_threads` 链表？加进去会怎样？
8. 新线程为什么初生就 `depth == 1` 且继承父线程纪元？带来什么副作用？
9. `bgp_keepalives` 为什么必须手动 `rcu_read_unlock()`？不这么做会出现什么现象？
10. `rcu_free` 宏里的 `static const struct rcu_action _rcu_action` 有什么含义？
    为什么 `field` 必须是编译期常量？
11. 为什么 `zlog_file_rotate()` 需要一个 `struct rcu_close_rotate` 临时对象？
    为什么它需要**两个** head？
12. `zlog_5424_cycle()` 里 `if (oldt->fd != fd)` 不判会出什么事？
13. `rcu_shutdown()` 为什么把 `rcu_active = false` 放在 `pthread_join()` **之前**？
14. 为什么 `struct zlog_tls`（每线程日志缓冲）不需要 RCU？（对照 §6.5）
15. 为什么 FRR 规定"即使持有读锁，访问已 `rcu_free` 的对象也是无效的"？
    去掉这条规定会失去什么优化？
16. 单线程程序（如 vtysh）里 `rcu_free()` 的实际行为是什么？在哪一行代码决定的？

---

## 10. 代码索引（按推荐阅读顺序）

| 顺序 | 位置 | 读什么 |
|---|---|---|
| 1 | [lib/frrcu.h:59-79](lib/frrcu.h#L59-L79) | RCU 速览 + 与内核的三点差异（**先读这个**） |
| 2 | [lib/frrcu.h:142-198](lib/frrcu.h#L142-L198) | 动作类型、`rcu_action`、`rcu_head`、`rcu_head_close` |
| 3 | [lib/frrcu.h:214-256](lib/frrcu.h#L214-L256) | `rcu_free` / `rcu_call` 宏逐字读 |
| 4 | [lib/frrcu.c:44-66](lib/frrcu.c#L44-L66) | implementation notes：纪元模型 + 队列永不为空的由来 |
| 5 | [lib/frrcu.c:96-131](lib/frrcu.c#L96-L131) | `rcu_thread` / `rcu_next` 数据结构 |
| 6 | [lib/frrcu.c:151-189](lib/frrcu.c#L151-L189) | 全局变量 |
| 7 | [lib/frrcu.c:203-223](lib/frrcu.c#L203-L223) | `rcu_preinit()` 启动状态 |
| 8 | [lib/frrcu.c:234-304](lib/frrcu.c#L234-L304) | 线程注册 / 注销全流程 |
| 9 | [lib/frrcu.c:315-391](lib/frrcu.c#L315-L391) | `rcu_bump()` / `rcu_bump_maybe()`（**核心**） |
| 10 | [lib/frrcu.c:396-436](lib/frrcu.c#L396-L436) | 读锁读写 + 断言 |
| 11 | [lib/frrcu.c:448-485](lib/frrcu.c#L448-L485) | `rcu_start()` 清扫线程诞生 |
| 12 | [lib/frrcu.c:490-592](lib/frrcu.c#L490-L592) | `rcu_do()` / `rcu_main()`（**核心**） |
| 13 | [lib/frrcu.c:596-657](lib/frrcu.c#L596-L657) | `rcu_shutdown()` / `rcu_enqueue()` / `rcu_close()` |
| 14 | [lib/seqlock.h:105-141](lib/seqlock.h#L105-L141) | 位定义 + `struct seqlock` |
| 15 | [lib/seqlock.c:261-300](lib/seqlock.c#L261-L300) | `seqlock_wait()` 的环绕比较 |
| 16 | [lib/seqlock.c:414-480](lib/seqlock.c#L414-L480) | `acquire_val` / `release` / `bump` |
| 17 | [doc/developer/rcu.rst:56-88](doc/developer/rcu.rst#L56-L88) | When (not) to use RCU |
| 18 | [doc/developer/rcu.rst:120-134](doc/developer/rcu.rst#L120-L134) | thread_master 的 RCU 持有契约 |
| 19 | [doc/developer/rcu.rst:171-201](doc/developer/rcu.rst#L171-L201) | `rcu_free` 规范（含两条 warning） |
| 20 | [doc/developer/rcu.rst:209-251](doc/developer/rcu.rst#L209-L251) | Internals：线程注册要求 |
| 21 | [lib/event.c:1250-1264](lib/event.c#L1250-L1264) | `fd_poll()` 放下读锁 |
| 22 | [lib/frr_pthread.c:233-266](lib/frr_pthread.c#L233-L266) | 线程创建时的 RCU 注册 |
| 23 | [lib/zlog_targets.c:244-251](lib/zlog_targets.c#L244-L251) | 标准"延迟 close + 延迟 free" |
| 24 | [lib/zlog_targets.c:381-417](lib/zlog_targets.c#L381-L417) | 轮转：双 head 经典写法 |
| 25 | [lib/zlog_5424.c:902-911](lib/zlog_5424.c#L902-L911) | "fd 没变就不关"的必查判断 |
| 26 | [lib/zlog_live.c:64-73](lib/zlog_live.c#L64-L73) + [:316-346](lib/zlog_live.c#L316-L346) | 双所有者交接协议 |
| 27 | [bgpd/bgp_keepalives.c:219-226](bgpd/bgp_keepalives.c#L219-L226) | 野线程必须手动解锁 |
| 28 | [lib/libfrr.c:1427-1449](lib/libfrr.c#L1427-L1449) | `frr_fini()` 里的 `rcu_shutdown()` 时序 |

### 与官方文档的对应关系

| 本文 | 官方文档 |
|---|---|
| §1.2 何时（不）用 RCU | [doc/developer/rcu.rst:56](doc/developer/rcu.rst#L56) "When (not) to use RCU" |
| §1.2 两种缩减用法 | [doc/developer/rcu.rst:70](doc/developer/rcu.rst#L70) "Other useful patterns" |
| §1.3 无 `synchronize_rcu`/`rcu_dereference` | [doc/developer/rcu.rst:253](doc/developer/rcu.rst#L253) "FRR specifics" |
| §3.2 读锁必须连续持有 | [doc/developer/rcu.rst:127-134](doc/developer/rcu.rst#L127-L134) |
| §5.2 事件循环的 RCU 契约 | [doc/developer/rcu.rst:120-125](doc/developer/rcu.rst#L120-L125) |
| §5.3 线程注册要求 | [doc/developer/rcu.rst:209-251](doc/developer/rcu.rst#L209-L251) "Internals" |
| §5.5 日志目标链的 RCU 契约 | [doc/developer/logging.rst:795-869](doc/developer/logging.rst#L795-L869) |
| §2.2 `rcu_head` 不可复用 | [doc/developer/rcu.rst:148-151](doc/developer/rcu.rst#L148-L151) |
| §6.4 原子链表为何需要 RCU | [doc/developer/lists.rst:714-728](doc/developer/lists.rst#L714-L728) |

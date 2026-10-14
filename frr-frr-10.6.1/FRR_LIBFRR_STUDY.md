# FRR libfrr 机制深入剖析

> 本文基于 **frr-10.6.1** 源码（当前分支 `study/bgpd-annotated`）逐行实测写作。
> 文中所有行号都用 `grep -n` / `sed -n` 在当前工作树上直接确认过，链接形如
> `[lib/libfrr.c:395](lib/libfrr.c#L395)`，路径相对仓库根目录。
> 目的是把「每个 FRR 守护进程都长一个样」这件事讲透：**libfrr 就是那副骨架**。
> 建议配合 `FRR_PTHREAD_STUDY.md`（线程）、`FRR_RCU_STUDY.md`（内存回收）、
> `FRR_IPC_STUDY.md`（进程间通信）一起读——libfrr 是它们共同的启动舞台。

---

## §0 全局总览

### 0.1 一句话结论

**libfrr 是 FRR 全部守护进程共用的「进程操作系统」**：它把 `main()` 从
「解析参数 → 建事件循环 → 读配置 → 后台化 → 进循环」这一串几乎人人相同的动作，
抽象成 `frr_preinit() → frr_opt_add()/frr_getopt() → frr_init() → frr_config_fork() → frr_run()`
五步主干；每个守护进程（bgpd / zebra / ospfd / mgmtd / …）只负责：

1. 用 `FRR_DAEMON_INFO` 声明「我是谁」（名字、vty 端口、信号表、能力、YANG 模块）；
2. 在 `frr_init()` 前后插入自己特有的初始化（比如 `bgp_master_init()`）；
3. 通过 `hook_register()` 在六个生命周期钩子上挂自己的回调。

### 0.2 心智模型：libfrr 是「守护进程的操作系统」

| 操作系统概念 | libfrr 对应物 | 位置 |
|---|---|---|
| 进程描述符 | `struct frr_daemon_info` | [lib/libfrr.h:157](lib/libfrr.h#L157) |
| 进程入口 | 五阶段主干 `frr_preinit/…/frr_run` | [lib/libfrr.c:395](lib/libfrr.c#L395) |
| 命令行解析 | `optspec` 组 + `comb_optstr` 聚合串 | [lib/libfrr.c:118](lib/libfrr.c#L118) |
| 驱动/回调注册 | `DEFINE_HOOK` / `hook_register` 六钩子 | [lib/hook.h:293](lib/hook.h#L293) |
| 系统调用接口 | `zprivs` / `frr_with_privs`（能力） | [lib/privs.h:234](lib/privs.h#L234) |
| 可加载内核模块 | `FRR_MODULE_SETUP` + `dlopen` | [lib/module.h:147](lib/module.h#L147) |
| 内存分配器统计 | MTYPE 注册表（构造函数自注册） | [lib/memory.h:138](lib/memory.h#L138) |
| 错误码表 | `struct log_ref` + `END_FERR` 哨兵 | [lib/ferr.h:184](lib/ferr.h#L184) |
| 信号处理 | `struct frr_signal_t` + 事件循环轮询 | [lib/sigevent.h:63](lib/sigevent.h#L63) |
| 守护进程化 | `frr_daemonize()` + 父子握手协议 | [lib/libfrr.c:1081](lib/libfrr.c#L1081) |

一句话：**看懂 libfrr.c 的 1774 行，就看懂了 FRR 所有 `*_main.c` 的 90%**。

### 0.3 启动主干 ASCII 全景图

```
                        main()  (以 bgpd/bgp_main.c:471 为例)
                           │
                           │  static struct frr_daemon_info bgpd_di;
                           │  FRR_DAEMON_INFO(bgpd, BGP, ...)   ← 声明"我是谁"
                           ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ① frr_preinit(&bgpd_di, argc, argv)          libfrr.c:395            │
  │    · di = daemon;  frr_is_after_fork = false                          │
  │    · progname = basename(argv[0]);  umask(0027)                       │
  │    · log_args_init()   —— 收集早期日志目标                            │
  │    · opt_extend(&os_always / os_cfg / os_fullcli / os_pid /           │
  │                 os_zclient / os_vty / os_user)  ← 按 FRR_NO_* 裁剪   │
  │    · 计算 config_default / pidfile_default / frr_zclientpath 默认路径 │
  │    · 扫描 fd 0..63 记入 startup_fds 位图                              │
  └──────────────────────────────────────────────────────────────────────┘
                           ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ② frr_opt_add(守护进程自有的短选项, longopts, help)   libfrr.c:478    │
  │    frr_getopt(argc, argv, 0) 循环                     libfrr.c:744    │
  │      └─ getopt_long(comb_optstr, comb_lo) → frr_opt() libfrr.c:519    │
  └──────────────────────────────────────────────────────────────────────┘
                           ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ③ frr_init()  →  返回 struct event_loop *master       libfrr.c:829    │
  │    路径重算 → zprivs_preinit → zlog_init → 消费早期日志               │
  │    → 解析 zclient 地址 → 按需 mkdir → 加载 -M 模块                    │
  │    → zprivs_init → event_master_create → signal_init                  │
  │    → hook_call(frr_early_init, master)   ★外部模块第一次进场          │
  │    → cmd_init/vty_init/debug_init/frr_pthread_init                    │
  │    → log_ref_init/nb_init/frr_memory_init                             │
  └──────────────────────────────────────────────────────────────────────┘
                           ▼
        bgp_master_init(frr_init(), ...)  ← 守护进程塞进自己的世界
                           ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ④ frr_config_fork()                                   libfrr.c:1172   │
  │    hook_call(frr_late_init)                                           │
  │    非 SPLIT_CONFIG:  dryrun? → 立即读配置并 exit(0)                   │
  │                     否则 event_add_event(frr_config_read_in)          │
  │    daemon_mode/terminal? → frr_daemonize()  (fork + setsid + 握手)    │
  │    frr_is_after_fork = true                                           │
  │    pid_output(di->pid_file)   ← fcntl F_SETLK 强制单实例              │
  │    zlog_tls_buffer_init()                                             │
  └──────────────────────────────────────────────────────────────────────┘
                           ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │ ⑤ frr_run(bm->master)          ← 永不返回             libfrr.c:1343   │
  │    frr_vty_serv_start() → zlog_notice("... starting")                 │
  │    terminal? vty_stdio + daemon_ctl 监听                              │
  │    daemon_mode? tty 重定向 /dev/null + 关闭控制 socket                │
  │    while (event_fetch(loop, &thread)) event_call(&thread);            │
  └──────────────────────────────────────────────────────────────────────┘
                           ▼
              （收到 SIGINT/SIGTERM → 守护进程 exit 回调）
                           ▼
         bgp_exit():  frr_early_fini()  ──►  frr_fini()   bgp_main.c:222/299
```

事件的插曲（`frr_config_read_in`）在 ④ 与 ⑤ 之间由事件循环触发：

```
frr_config_read_in()  libfrr.c:1128
   hook_call(frr_config_pre,  master)
   vty_read_config(candidate, di->config_file, config_default)
       失败且有 backup_config_file → 再读 backup 并恢复 host_config
   事务模式(FRR_CLI_TRANSACTIONAL) → nb_candidate_commit(...)
   hook_call(frr_config_post, master)
```

### 0.4 文件职责分层表

| 层次 | 文件 | 职责 | 与 libfrr.c 的关系 |
|---|---|---|---|
| **入口层** | [lib/libfrr.c](lib/libfrr.c) | 五阶段主干、选项聚合、守护化、钩子触发 | 本文主角 |
| | [lib/libfrr.h](lib/libfrr.h) | `struct frr_daemon_info`、`FRR_DAEMON_INFO` 宏、`FRR_NO_*` | 接口契约 |
| **框架层** | [lib/hook.c](lib/hook.c) / [lib/hook.h](lib/hook.h) | 生命周期钩子注册与调用 | `frr_early_init` 等 6 个钩子在此定义 |
| | [lib/module.c](lib/module.c) / [lib/module.h](lib/module.h) | 可加载模块（`.so`） | `frrmod_init/frrmod_load/frrmod_terminate` |
| | [lib/privs.c](lib/privs.c) / [lib/privs.h](lib/privs.h) | 权限与 Linux capability | `zprivs_preinit/init/terminate` |
| | [lib/sigevent.c](lib/sigevent.c) / [lib/sigevent.h](lib/sigevent.h) | 信号捕获与轮询分发 | `signal_init(master, …)` |
| | [lib/pid_output.c](lib/pid_output.c) | pid 文件 + 记录锁单实例 | `pid_output()` |
| **注册表层** | [lib/memory.c](lib/memory.c) / [lib/memory.h](lib/memory.h) | MTYPE/memgroup 自注册 | `frr_memory_init/fini` |
| | [lib/ferr.c](lib/ferr.c) / [lib/ferr.h](lib/ferr.h) | 错误码引用表 | `log_ref_init/fini/vty_init` |
| **消费层** | 各 `*/​*_main.c` | 声明自身、插初始化、挂钩子 | 调用方 |

### 0.5 关键全局变量一览（lib/libfrr.c）

| 变量 | 行号 | 含义 |
|---|---|---|
| `frr_sysconfdir[]` | [:74](lib/libfrr.c#L74) | 配置目录（构建期 `SYSCONFDIR`，常量） |
| `frr_runstatedir[256]` | [:75](lib/libfrr.c#L75) | 易失运行目录（**含 `-N` 命名空间后缀**） |
| `frr_libstatedir[256]` | [:76](lib/libfrr.c#L76) | 持久状态目录（含命名空间后缀） |
| `frr_moduledir[]` / `frr_scriptdir[]` | [:77-78](lib/libfrr.c#L77) | 模块目录 / Lua 脚本目录 |
| `frr_protoname[256]` / `frr_protonameinst[256]` | [:81-82](lib/libfrr.c#L81) | 协议名 / 带实例名的协议名 |
| `config_default[512]` | [:86](lib/libfrr.c#L86) | 计算出的默认配置文件路径 |
| `frr_zclientpath[512]` | [:87](lib/libfrr.c#L87) | zclient（zserv）socket 路径 |
| `pidfile_default[1024]` | [:88](lib/libfrr.c#L88) | 默认 pid 文件路径 |
| `vtypath_default[512]` | [:92](lib/libfrr.c#L92) | 默认 vty socket 路径 |
| `bool frr_is_after_fork` | [:99](lib/libfrr.c#L99) | 「本该 fork 的位置」已过？初值 true |
| `bool debug_memstats_at_exit` | [:100](lib/libfrr.c#L100) | 退出时是否打印内存统计 |
| `nodetach_term/nodetach_daemon` | [:101](lib/libfrr.c#L101) | `-t`/`-d` 保持附着的模式标记 |
| `startup_fds` (uint64_t) | [:102](lib/libfrr.c#L102) | 启动时已打开 fd 的位图 |
| `comb_optstr[256]` / `comb_lo[64]` / `comb_helpstr[4096]` | [:107-110](lib/libfrr.c#L107) | 聚合后的短选项串 / 长选项表 / 帮助 |
| `static struct frr_daemon_info *di` | [:387](lib/libfrr.c#L387) | **全局单例**，一切配置的来源 |
| `static struct event_loop *master` | [:822](lib/libfrr.c#L822) | 主事件循环（`frr_init` 写入） |
| `daemon_ctl_sock` | [:1077](lib/libfrr.c#L1077) | 守护化握手 socket（子进程侧） |
| `rcvd_signal` | [:971](lib/libfrr.c#L971) | 父进程握手线程收到的信号号 |
| `modules` / `errors` | [:511-512](lib/libfrr.c#L511) | `-M` 模块链表 / 选项错误计数 |

### 0.6 启动主干 API 速查

| 阶段 | 函数 | 行号 | 一句话 |
|---|---|---|---|
| ① | `frr_preinit(di, argc, argv)` | [:395](lib/libfrr.c#L395) | 记录 di、定默认路径、按 flags 组装选项组 |
| ②a | `frr_opt_add(optstr, longopts, help)` | [:478](lib/libfrr.c#L478) | 追加守护进程自有选项 |
| ②b | `frr_getopt(argc, argv, &lidx)` | [:744](lib/libfrr.c#L744) | 循环 `getopt_long` + `frr_opt()` 分发 |
| ③ | `frr_init()` → `master` | [:829](lib/libfrr.c#L829) | 初始化全部子系统，返回主循环 |
| ④ | `frr_config_fork()` | [:1172](lib/libfrr.c#L1172) | 读配置、守护化、写 pid |
| ⑤ | `frr_run(loop)` | [:1343](lib/libfrr.c#L1343) | 启动 VTY、进入事件循环（永不返回） |
| 收尾① | `frr_early_fini()` | [:1399](lib/libfrr.c#L1399) | 触发 `frr_early_fini` 逆序钩子 |
| 收尾② | `frr_fini()` | [:1409](lib/libfrr.c#L1409) | 拆掉所有子系统（`rcu_shutdown` 在最后） |

---

## §1 设计动机与契约

### 1.1 为什么要有 libfrr

FRR 有十几个守护进程：`zebra`、`bgpd`、`ospfd`、`ospf6d`、`ripd`、`ripngd`、
`isisd`、`pimd`、`ldpd`、`nhrpd`、`eigrpd`、`babeld`、`pbrd`、`bfdd`、`fabricd`、
`staticd`、`pathd`、`mgmtd`、`watchfrr` ……它们的 `main()` 若各写各的，会有几个后果：

- **选项解析不一致**：`-N` 命名空间、`-I` 实例号、`-d` 守护化这些通用选项，靠每个
  daemon 各自实现，必然有偏差，运维脚本就没法统一。
- **初始化顺序不一致**：`zlog_init()` 必须在降权前、`zprivs_init()` 必须在事件循环前、
  RCU 必须在最外层……这类顺序约束散落各处极易写错。
- **关闭路径不一致**：资源释放顺序错了就是内存泄漏或段错误。

所以 libfrr 把这套「进程脚手架」固化下来，形成**守护进程与库之间的契约**。

### 1.2 `FRR_DAEMON_INFO` 契约

守护进程用一个宏把「我是谁」告诉 libfrr。定义在
[lib/libfrr.h:227](lib/libfrr.h#L227)：

```c
#define FRR_DAEMON_INFO(execname, constname, ...)                              \
	static struct frr_daemon_info execname##_di = {.name = #execname,      \
						       .logname = #constname,  \
						       .module = THIS_MODULE,  \
						       __VA_ARGS__};           \
	FRR_COREMOD_SETUP(.name = #execname,                                   \
			  .description = #execname " daemon",                  \
			  .version = FRR_VERSION, );                           \
	MACRO_REQUIRE_SEMICOLON() /* end */
```

它做三件事：

1. 生成静态 `struct frr_daemon_info execname##_di`，先由宏**自动填三个字段**
   ——`.name = #execname`、`.logname = #constname`、`.module = THIS_MODULE`；
   其余字段由可变参数补（调用者若重复指定这三个会被编译器报重复初始化）。
2. 展开 `FRR_COREMOD_SETUP(...)`，把**本文件注册成一个「编译进主程序」的 FRR 模块**
   （见 §8），这样 `THIS_MODULE` 在守护进程文件里也能用。
3. `MACRO_REQUIRE_SEMICOLON()` 保证宏调用后必须跟分号，语法上更像一个声明。

bgpd 的实际用法（[bgpd/bgp_main.c:450](bgpd/bgp_main.c#L450)）：

```c
FRR_DAEMON_INFO(bgpd, BGP,
	.vty_port = BGP_VTY_PORT,
	.proghelp = "Implementation of the BGP routing protocol.",

	.signals = bgp_signals,
	.n_signals = array_size(bgp_signals),

	.privs = &bgpd_privs,

	.yang_modules = bgpd_yang_modules,
	.n_yang_modules = array_size(bgpd_yang_modules),
);
```

（完整版见 [bgpd/bgp_main.c:450-461](bgpd/bgp_main.c#L450)；
`.name` / `.logname` / `.module` 由宏自己填，不在这里写。）

### 1.3 `struct frr_daemon_info` 字段全表

定义在 [lib/libfrr.h:157](lib/libfrr.h#L157)，共 40 余个字段。按用途分组：

| 分组 | 字段（行号） | 说明 |
|---|---|---|
| **身份** | `flags` [:158](lib/libfrr.h#L158)、`progname` [:160](lib/libfrr.h#L160)、`name` [:161](lib/libfrr.h#L161)、`logname` [:162](lib/libfrr.h#L162)、`instance` [:163](lib/libfrr.h#L163)、`module` [:164](lib/libfrr.h#L164) | `progname` 由 `argv[0]` 推出；`name` 决定路径与 pid 文件名 |
| **VTY** | `vty_addr` [:166](lib/libfrr.h#L166)、`vty_port` [:167](lib/libfrr.h#L167)、`vty_sock_path` [:168](lib/libfrr.h#L168)、`vty_path` [:183](lib/libfrr.h#L183) | 端口 0 表示关 TCP；Unix socket 默认在 runstatedir |
| **运行模式** | `dryrun` [:169](lib/libfrr.h#L169)、`daemon_mode` [:170](lib/libfrr.h#L170)、`terminal` [:171](lib/libfrr.h#L171) | `-C`/`-d`/`-t` 的落地位置 |
| **GR** | `graceful_restart` [:172](lib/libfrr.h#L172)、`gr_cleanup_time` [:173](lib/libfrr.h#L173) | 优雅重启参数 |
| **CLI** | `cli_mode` [:174](lib/libfrr.h#L174) | `enum frr_cli_mode`（[:147](lib/libfrr.h#L147)）：CLASSIC / TRANSACTIONAL |
| **配置** | `read_in` [:176](lib/libfrr.h#L176)、`config_file` [:177](lib/libfrr.h#L177)、`backup_config_file` [:178](lib/libfrr.h#L178) | `read_in` 由 libfrr 写入事件句柄 |
| **路径** | `pid_file` [:179](lib/libfrr.h#L179)、`db_file` [:181](lib/libfrr.h#L181)、`module_path` [:184](lib/libfrr.h#L184)、`script_path` [:185](lib/libfrr.h#L185)、`state_paths` [:186](lib/libfrr.h#L186)、`pathspace` [:188](lib/libfrr.h#L188)、`zpathspace` [:189](lib/libfrr.h#L189) | `-N` 写 `pathspace` |
| **日志** | `early_logging` [:191](lib/libfrr.h#L191)、`early_loglevel` [:192](lib/libfrr.h#L192)、`log_always` [:207](lib/libfrr.h#L207) | 早期日志在 `zlog_init` 前收集 |
| **展示** | `proghelp` [:194](lib/libfrr.h#L194)、`printhelp` [:195](lib/libfrr.h#L195)、`copyright` [:196](lib/libfrr.h#L196)、`startinfo` [:197](lib/libfrr.h#L197) | `-h` 输出 |
| **信号** | `signals` [:199](lib/libfrr.h#L199)、`n_signals` [:200](lib/libfrr.h#L200) | 传给 `signal_init()` |
| **权限** | `privs` [:202](lib/libfrr.h#L202) | `struct zebra_privs_t *` |
| **北向** | `yang_modules` [:204](lib/libfrr.h#L204)、`n_yang_modules` [:205](lib/libfrr.h#L205) | 传给 `nb_init()` |
| **资源** | `limit_fds` [:210](lib/libfrr.h#L210) | `--limit-fds` |

> **注意**：`progname`、`cli_mode`、`graceful_restart`、`gr_cleanup_time` 会在
> `frr_preinit()` 中被**无条件覆盖**（[lib/libfrr.c:402](lib/libfrr.c#L402)、
> [:442-444](lib/libfrr.c#L442)），所以在 `FRR_DAEMON_INFO` 里写这几个字段是无效的。

### 1.4 与「标准 Unix 守护进程」的差异

| 传统做法 | FRR 做法 | 原因 |
|---|---|---|
| 直接 `fork()` 后父进程 `_exit(0)` | `socketpair` + `fork`，父进程 `frr_daemon_wait()` 握手后才退出 | 让 `bgpd -d` 返回时子进程**已经**在主循环里，脚本能立刻 `vtysh -d bgpd` |
| `setsid()` 后 `chdir("/")` | 只 `setsid()`，不 `chdir` | 路径都基于绝对路径，不需要 chdir；保留 cwd 便于 core dump |
| 写 pid 文件即完 | 写 pid 文件前先 `fcntl(F_SETLK)` 抢锁 | 单实例强制（见 §6.3） |
| 父进程 `exit()` 就完事 | 父进程负责把 `SIGTSTP`/`SIGINT`/`SIGQUIT` 转发给子进程 | `bgpd -d` 在终端里 `Ctrl-C` 能真正杀掉后台进程 |

---

## §2 启动主干五阶段

### 2.1 阶段职责对照

| 阶段 | 关键动作 | 此时进程状态 |
|---|---|---|
| ① `frr_preinit` | 记 di、定路径、组选项 | 完全特权（root），日志未就绪 |
| ② `frr_opt_add`+`frr_getopt` | 解析全部命令行 | 同上 |
| ③ `frr_init` | 全部子系统初始化 | 末尾 `zprivs_init()` **降权** |
| ④ `frr_config_fork` | 读配置、fork、写 pid | 已降权；守护化后是新进程 |
| ⑤ `frr_run` | 事件循环 | 稳定运行 |

### 2.2 `frr_preinit()` 逐行（[lib/libfrr.c:395](lib/libfrr.c#L395)）

```c
void frr_preinit(struct frr_daemon_info *daemon, int argc, char **argv)
{
	di = daemon;                       /* :397  全局单例绑定 */
	frr_is_after_fork = false;         /* :398  清除"已过 fork 点" */
	char *p = strrchr(argv[0], '/');
	di->progname = p ? p + 1 : argv[0];/* :402  basename(argv[0]) */
	if (!getenv("GCOV_PREFIX")) umask(0027); else umask(0002);  /* :404-409 */
	log_args_init(daemon->early_logging);   /* :411  收集早期日志目标 */
	opt_extend(&os_always);                 /* :413 */
	if (!(di->flags & FRR_NO_SPLIT_CONFIG)) opt_extend(&os_cfg);
	if (!(di->flags & FRR_LIMITED_CLI))     opt_extend(&os_fullcli);
	if (!(di->flags & FRR_NO_PID))          opt_extend(&os_pid);
	if (!(di->flags & FRR_NO_PRIVSEP))      opt_extend(&os_user);
	if (!(di->flags & FRR_NO_ZCLIENT))      opt_extend(&os_zclient);
	if (!(di->flags & FRR_NO_TCPVTY))       opt_extend(&os_vty);
	if (di->flags & FRR_DETACH_LATER) nodetach_daemon = true;  /* :426 */
	snprintf(config_default, …, "%s/%s.conf", frr_sysconfdir, di->name);  /* :429 */
	snprintf(pidfile_default, …, "%s/%s.pid", frr_runstatedir, di->name); /* :431 */
	snprintf(frr_zclientpath, …, ZAPI_SOCK_NAME);                         /* :433 */
	strlcpy(frr_protoname, di->logname, sizeof(frr_protoname));           /* :439 */
	di->cli_mode = FRR_CLI_CLASSIC;                                       /* :442 */
	di->graceful_restart = false;  di->gr_cleanup_time = 0;               /* :443-444 */
	startup_fds = 0;
	for (int i = 0; i < 64; i++) { … startup_fds |= 1 << i; }             /* :451-461 */
	systemd_init_env();                                                   /* :466 */
}
```

三个要点：

1. **umask 的选择**：普通运行 `0027`（新文件不给 other/group 任何权限），
   但带 `GCOV_PREFIX`（覆盖率）时放宽到 `0002`，否则 gcov 写不出数据。
   [:404-409](lib/libfrr.c#L404)
2. **`startup_fds` 位图**：扫描 fd 0..63，把「已存在且既不是目录也不是块设备」的
   fd 记下来（[:451-461](lib/libfrr.c#L451)）。用途见
   `frr_is_startup_fd()`（[:471](lib/libfrr.c#L471)）——例如明确区分「我这个 fd
   是继承来的」和「我这个 fd 是本进程自己建的」，避免用 BGP 会话 fd 去写日志。
   注释在 [:446-450](lib/libfrr.c#L446) 写得很直白。
3. **`systemd_init_env()`** 只「抓状态」不做动作，所以放这么早没问题
   （[:463-466](lib/libfrr.c#L463)）。

### 2.3 `frr_opt_add()` / `frr_getopt()`

`frr_opt_add()` 极其简单，就是构造一个临时 `optspec` 再交给 `opt_extend()`
（[lib/libfrr.c:478](lib/libfrr.c#L478)）。它**必须**在 `frr_getopt()` 之前调用，否则
守护进程自定义选项会「unknown option」。

`frr_getopt()`（[:744](lib/libfrr.c#L744)）：

```c
int frr_getopt(int argc, char *const argv[], int *longindex)
{
	comb_next_lo->name = NULL;             /* :749  给长选项数组打终止哨兵 */
	do {
		opt = getopt_long(argc, argv, comb_optstr, comb_lo, &lidx);
		if (frr_opt(opt)) break;       /* :753  frr_opt 返回 1 = 该选项被禁用 */
	} while (opt != -1);
	if (opt == -1 && errors) frr_help_exit(1);   /* :757 */
	if (longindex) *longindex = lidx;
	return opt;
}
```

守护进程的典型循环（[bgpd/bgp_main.c:508](bgpd/bgp_main.c#L508)）：

```c
while (1) {
	opt = frr_getopt(argc, argv, 0);
	if (opt == EOF) break;
	switch (opt) {
	case 'n': …          /* bgpd 自有选项 */
	case 'Z': …
	default: frr_help_exit(1);
	}
}
```

> **陷阱**：`frr_getopt` 返回 1 不会让 `getopt_long` 停下来——是 `frr_opt()` 内部
> 返回 1 时 `frr_getopt` 主动 `break`。所以 `default:` 分支里必须自己
> `frr_help_exit(1)`，否则会被 `frr_opt` 的 `default` 吞掉（见 §3.5）。

### 2.4 `frr_init()` 完整顺序（[lib/libfrr.c:829](lib/libfrr.c#L829)）

这是全书最重要的一段，**顺序即约束**：

| 序 | 行号 | 动作 | 为什么在这个位置 |
|---|---|---|---|
| 1 | [:840](lib/libfrr.c#L840) | `srandom(time(NULL))` | 越早越好 |
| 2 | [:841](lib/libfrr.c#L841) | `frr_defaults_apply()` | 应用 profile 覆盖的默认路径 |
| 3 | [:843-859](lib/libfrr.c#L843) | 按 `instance` + `pathspace` 重算 config/pid/db 路径 | 必须在 mkdir 之前 |
| 4 | [:861](lib/libfrr.c#L861) | `zprivs_preinit(di->privs)` | **降权前**先算好目标 uid/gid |
| 5 | [:862](lib/libfrr.c#L862) | `zprivs_get_ids(&ids)` | 取到普通用户 id，给日志文件 chown |
| 6 | [:864](lib/libfrr.c#L864) | `zlog_init(progname, logname, instance, uid, gid)` | **仍特权**，才能 chown `/var/tmp/frr` |
| 7 | [:867-876](lib/libfrr.c#L867) | 消费早期日志目标（`log_args_pop`） | zlog 就绪后才能设目标 |
| 8 | [:878](lib/libfrr.c#L878) | `frr_zclient_addr()` | 解析 zserv socket 地址 |
| 9 | [:886-894](lib/libfrr.c#L886) | `frr_mkdir(...)` 建 runstatedir 等 | **特权下**建目录并 chown |
| 10 | [:896](lib/libfrr.c#L896) | `frrmod_init(di->module)` | 注册守护进程自身模块 |
| 11 | [:897-903](lib/libfrr.c#L897) | 逐个 `frrmod_load()` 加载 `-M` | 模块可能要特权 |
| 12 | [:905](lib/libfrr.c#L905) | `zprivs_init(di->privs)` | ★ **真正降权点** |
| 13 | [:907](lib/libfrr.c#L907) | `event_master_create(NULL)` | 建主事件循环 |
| 14 | [:908](lib/libfrr.c#L908) | `signal_init(master, n_signals, signals)` | 信号处理依赖事件循环 |
| 15 | [:909](lib/libfrr.c#L909) | `hook_call(frr_early_init, master)` | ★ **外部第一次进场** |
| 16 | [:911-915](lib/libfrr.c#L911) | 可选 sqlite `db_init()` | |
| 17 | [:917-925](lib/libfrr.c#L917) | `cmd_init(1)` 或 `cmd_init(-1)`(LIMITED_CLI) | 命令注册 |
| 18 | [:927](lib/libfrr.c#L927) | `vty_init(master, di->log_always)` | |
| 19 | [:928-929](lib/libfrr.c#L928) | `lib_cmd_init()` / `debug_init()` | |
| 20 | [:931](lib/libfrr.c#L931) | `frr_pthread_init()` | 线程框架 |
| 21 | [:933](lib/libfrr.c#L933) | `frrscript_init(script_path)`（HAVE_SCRIPTING） | |
| 22 | [:936-938](lib/libfrr.c#L936) | `log_ref_init` / `log_ref_vty_init` / `lib_error_init` | 错误码表 |
| 23 | [:940-945](lib/libfrr.c#L940) | `nb_init(...)` + `nb_db_init()` | 北向接口 |
| 24 | [:947](lib/libfrr.c#L947) | `frr_memory_init(master)` | 内存统计定时器 |

> **要点 1**：`zprivs_preinit` 在 `zlog_init` 之前、`zprivs_init` 在 `zlog_init` 之后
> ——即 `zlog_init` **运行在特权态**。这是刻意的：日志文件/临时目录需要
> `chown` 给降权后的用户（见 §7.3）。
>
> **要点 2**：`hook_call(frr_early_init)` 放在 [:909](lib/libfrr.c#L909)，
> 此时事件循环、信号、CLI 都就绪，但**配置尚未读取**。这是外部模块做「注册命令、
> 注册 YANG 回调」的标准位置。
>
> **要点 3**：多处直接 `exit(1)` 而非返回错误（例如 [:882](lib/libfrr.c#L882)、
> [:901](lib/libfrr.c#L901)）。启动期出错没有「优雅回退」的语义，直接退更简单。

### 2.5 `frr_config_fork()`（[lib/libfrr.c:1172](lib/libfrr.c#L1172)）

```c
void frr_config_fork(void)
{
	hook_call(frr_late_init, master);          /* :1174 */
	if (!(di->flags & FRR_NO_SPLIT_CONFIG)) {
		if (di->dryrun) {                  /* :1178  -C */
			frr_config_read_in(NULL);
			exit(0);
		}
		event_add_event(master, frr_config_read_in, NULL, 0, &di->read_in);
	}                                          /* :1183 */
	if (di->daemon_mode || di->terminal) frr_daemonize();   /* :1187 */
	frr_is_after_fork = true;                  /* :1190 */
	if (!di->pid_file) di->pid_file = pidfile_default;
	pid_output(di->pid_file);                  /* :1194 */
	zlog_tls_buffer_init();                    /* :1195 */
}
```

四个关键点：

- **`-C`（dryrun）**：在 [:1179](lib/libfrr.c#L1179) **同步**读一遍配置然后 `exit(0)`，
  用来「只检查配置语法」。
- **`read_in` 是事件句柄**：libfrr 把 `&di->read_in` 存进 `di`，守护进程可以引用它
  （一般不用）。
- **`frr_is_after_fork = true` 的位置**：在 `frr_daemonize()` **之后**、**无论是否真的
  fork**。它的语义是「本该 fork 的位置已经过了」。所以即使用了 `-t`（不 fork），
  这个标志同样是 true。下游用它判断「现在再建线程是安全的」——
  见 [lib/frr_pthread.c:238](lib/frr_pthread.c#L238) 的断言。
- **`pid_output()` 在 fork 之后**：写进去的 pid 是**子进程**（真正在跑的守护进程）
  的 pid。

### 2.6 `frr_run()`（[lib/libfrr.c:1343](lib/libfrr.c#L1343)）

```c
void frr_run(struct event_loop *loop)
{
	if (!(di->flags & FRR_MANUAL_VTY_START))
		frr_vty_serv_start(false);                       /* :1348-1349 */
	zlog_notice("%s %s starting: %svty@%d%s", di->name, FRR_VERSION,
		    instanceinfo, di->vty_port, di->startinfo);  /* :1355 */
	if (di->terminal) {                                      /* :1358 */
		nodetach_term = true;
		vty_stdio(frr_terminal_close);
		if (daemon_ctl_sock != -1) {                     /* :1362 */
			set_nonblocking(daemon_ctl_sock);
			event_add_read(loop, frr_daemon_ctl, NULL,
				       daemon_ctl_sock, &daemon_ctl_thread);
		}
	} else if (di->daemon_mode) {                            /* :1367 */
		/* 把 tty 上的 stdin/stdout/stderr 重定向到 /dev/null，
		   但 stdout 被 --log stdout 占用时不重定向 */          /* :1380-1383 */
		if (!(di->flags & FRR_MANUAL_VTY_START))
			frr_check_detach();                      /* :1388 */
	}
	zlog_startup_end();              /* :1392 结束"固定 stderr 启动日志"阶段 */
	while (event_fetch(loop, &thread))                       /* :1394 */
		event_call(&thread);
}
```

- `-t`（terminal）模式与 `-d`（daemon）模式的差别：前者**保留 tty**，并把
  `daemon_ctl_sock` 挂到事件循环上持续接收父进程转发来的信号（[:1362-1366](lib/libfrr.c#L1362)）；
  后者**断开 tty** 并关掉控制 socket（[:1387-1388](lib/libfrr.c#L1387)）。
- `zlog_startup_end()` 之后，日志格式从「固定 stderr」切成正常 zlog 输出。
- `while (event_fetch(...)) event_call(...)` —— 进程的生命就在这里，
  直到某个信号的 handler 调 `exit()`。

### 2.7 收尾：`frr_early_fini()` 与 `frr_fini()`

守护进程退出时**必须**按顺序调这两个（[bgpd/bgp_main.c:222](bgpd/bgp_main.c#L222) 与
[:299](bgpd/bgp_main.c#L299)）：

```c
void frr_early_fini(void) { hook_call(frr_early_fini); }        /* :1399-1402 */

void frr_fini(void)                                             /* :1409 */
{
	hook_call(frr_fini);            /* :1411  逆序钩子 */
	frr_memory_fini();              /* :1413 */
	vty_terminate();                /* :1415 */
	cmd_terminate();                /* :1416 */
	nb_terminate();                 /* :1417 */
	yang_terminate();               /* :1418 */
	db_close();                     /* :1420  HAVE_SQLITE3 */
	log_ref_fini();                 /* :1422 */
	frrscript_fini();               /* :1425  HAVE_SCRIPTING */
	frr_pthread_finish();           /* :1427  线程必须在 RCU 之前停 */
	zprivs_terminate(di->privs);    /* :1428 */
	event_master_free(master);      /* :1430 */
	master = NULL;                  /* :1431 */
	zlog_tls_buffer_fini();         /* :1432 */
	if (0) zlog_fini();             /* :1434-1446  ★故意禁用 */
	rcu_shutdown();                 /* :1449  RCU 清扫线程最后停 */
	frrmod_terminate();             /* :1451 */
	log_memstats(di->name, debug_memstats_at_exit);  /* :1453 */
	zlog_tmpdir_fini();             /* :1454 */
}
```

**`if (0) zlog_fini()` 是整个文件最值得记住的一行**（[:1434-1446](lib/libfrr.c#L1434)）。
注释说得很清楚：zlog 一直活到 `exit()`，这样**关闭流程最后一步的日志也能打出来**。
代价是内存泄漏，但这些泄漏被 `DEFINE_MGROUP_ACTIVEATEXIT`（[lib/memory.h:128](lib/memory.h#L128)）
显式标记，退出统计时会排除掉。这是一次**「日志完整性 > 无泄漏」的自觉取舍**。

---

## §3 命令行体系

### 3.1 `optspec` / `opt_extend` / `comb_*`

```c
struct optspec {                                    /* :118-122 */
	const char *optstr;          /* 短选项串，如 "dM:F:" */
	const char *helpstr;         /* -h 里显示的一段帮助 */
	const struct option *longopts;  /* 长选项数组 */
};

static void opt_extend(const struct optspec *os)     /* :126-134 */
{
	strlcat(comb_optstr, os->optstr, sizeof(comb_optstr));
	strlcat(comb_helpstr, os->helpstr, sizeof(comb_helpstr));
	for (lo = os->longopts; lo->name; lo++)
		memcpy(comb_next_lo++, lo, sizeof(*lo));
}
```

设计很朴素但很有效：**所有选项组最终拼接成三块全局内存**
（`comb_optstr` / `comb_helpstr` / `comb_lo`），
然后一次性丢给 `getopt_long`（[:753](lib/libfrr.c#L753)）。
守护进程的 `frr_opt_add()` 也走同一条路，所以「库的选项」和「daemon 的选项」
在机制上完全平等。

> **陷阱**：`comb_optstr[256]` / `comb_lo[64]` 是**定长**的
> （[:107-108](lib/libfrr.c#L107)），溢出会被 `strlcat` / 越界 `memcpy` 静默吞掉。
> 加选项组时要留意预算。

### 3.2 选项组表

| 组 | 行号 | 短选项串 | 内容 | 由哪个 `FRR_NO_*` 关掉 |
|---|---|---|---|---|
| `os_always` | [:172](lib/libfrr.c#L172) | `"w"`\* + `"hvdM:F:N:o:K::"` | 帮助/版本/守护化(`-d`)/模块(`-M`)/配置文件(`-F`)/命名空间(`-N`)/`-o`/`-K`，以及 `--log`、`--log-level`、`--vty_socket`、`--moduledir`、`--scriptdir`、`--limit-fds` 等长选项 | 永不关 |
| `os_cfg` | [:206](lib/libfrr.c#L206) | `"f:C"` | `-f` 配置文件、**`-C` dryrun** | `FRR_NO_SPLIT_CONFIG` |
| `os_fullcli` | [:222](lib/libfrr.c#L222) | `"t"` | **`-t` terminal**、`--tcli` | `FRR_LIMITED_CLI` |
| `os_pid` | [:234](lib/libfrr.c#L234) | `"i:"` | `-i` pid 文件 | `FRR_NO_PID` |
| `os_zclient` | [:244](lib/libfrr.c#L244) | `"z:"` | `-z` zserv socket | `FRR_NO_ZCLIENT` |
| `os_vty` | [:253](lib/libfrr.c#L253) | `"A:P:"` | `-A` vty 地址、`-P` vty 端口 | `FRR_NO_TCPVTY` |
| `os_user` | [:264](lib/libfrr.c#L264) | `"u:g:"` | `-u` 用户、`-g` 组 | `FRR_NO_PRIVSEP` |

\* `"w"` 仅在 `HAVE_NETLINK` 时加进 `os_always`（[lib/libfrr.c:173-175](lib/libfrr.c#L173)）。

对应长选项数组分别在同组的前一段（`lo_always` [:151](lib/libfrr.c#L151)、
`lo_cfg` [:202](lib/libfrr.c#L202)、`lo_fullcli` [:215](lib/libfrr.c#L215)、
`lo_pid` [:231](lib/libfrr.c#L231)、`lo_zclient` [:241](lib/libfrr.c#L241)、
`lo_vty` [:249](lib/libfrr.c#L249)、`lo_user` [:261](lib/libfrr.c#L261)）。

> **注意分组语义**：`-C`（dryrun）属于**配置组**，`-t`（terminal）属于 **CLI 组**
> ——所以设了 `FRR_NO_SPLIT_CONFIG` 就没有 `-C`，设了 `FRR_LIMITED_CLI` 就没有 `-t`。
> 这两个短选项**不在** `os_always` 里。

**哪一组被启用，完全由 `di->flags` 决定**（[:413-425](lib/libfrr.c#L413)）。
比如 `staticd` 不需要 zclient，就设 `FRR_NO_ZCLIENT`，于是 `-z` 直接不存在。

> **表里没有 `-l`**：本版本的日志级别是长选项 `--log-level`
> （`OPTION_LOGLEVEL`，[lib/libfrr.c:167](lib/libfrr.c#L167)），
> 不存在 `-l` 短选项。

### 3.3 `FRR_NO_*` 开关全表（[lib/libfrr.h:58](lib/libfrr.h#L58)）

| 宏 | 值 | 效果 |
|---|---|---|
| `FRR_NO_PRIVSEP` | `1<<0` | 不启用 `os_user` 组（`-u`/`-g`），不建 runstatedir |
| `FRR_NO_TCPVTY` | `1<<1` | 不启用 `os_vty` 组（无 `-A`/`-P`） |
| `FRR_LIMITED_CLI` | `1<<2` | 不启用 `os_fullcli`；`cmd_init(-1)`；不装 host/log CLI |
| `FRR_NO_SPLIT_CONFIG` | `1<<3` | 不启用 `os_cfg`；`frr_config_fork` 不排配置读取事件 |
| `FRR_NO_PID` | `1<<4` | 不启用 `os_pid`；写 pid 文件仍会做（用默认名） |
| `FRR_NO_CFG_PID_DRY` | 组合 [:63](lib/libfrr.h#L63) | `NO_PID \| NO_SPLIT_CONFIG` |
| `FRR_NO_ZCLIENT` | `1<<5` | 不启用 `os_zclient` |
| `FRR_DETACH_LATER` | `1<<6` | 守护化后**父进程继续等**，直到 `frr_detach()` |
| `FRR_MANUAL_VTY_START` | `1<<7` | `frr_run()` 不自动起 VTY 监听 |
| `FRR_LOAD_YANG_LIBRARY` | `1<<8` | 让 libyang 实现 ietf-yang-library（一般只有 mgmtd） |
| `FRR_MGMTD_BACKEND` | `1<<9` | 标记本进程是 mgmtd 后端，影响 CLI 初始化 |

> **`FRR_NO_PID` 的语义**：它只关掉 `-i` **选项**，`frr_config_fork()` 里
> `pid_output(di->pid_file)`（[:1194](lib/libfrr.c#L1194)）仍会执行，只是用
> `pidfile_default`。所以 `FRR_NO_PID` 不等于「不写 pid 文件」。

> **`FRR_DETACH_LATER` 的用途**（注释在 [lib/libfrr.h:66-73](lib/libfrr.h#L66)）：
> 正常的 `bgpd -d` 在到达主循环时就返回；有些守护进程还有额外的启动工作
> （比如从磁盘恢复状态），希望**这些做完后再让父进程返回**。于是它在
> `frr_preinit` 里置 `nodetach_daemon = true`（[:426-427](lib/libfrr.c#L426)），
> 干完后调 `frr_detach()`（[:1332](lib/libfrr.c#L1332)）释放控制 socket。

### 3.4 伪短选项 `OPTION_*`（[lib/libfrr.c:139](lib/libfrr.c#L139)）

只有长选项、没有单字符短选项的项，用 ≥1000 的整数当「伪短选项」：

| 宏 | 值 | 长选项 |
|---|---|---|
| `OPTION_VTYSOCK` | 1000 | `--vty_socket` |
| `OPTION_MODULEDIR` | 1002 | `--moduledir` |
| `OPTION_LOG` | 1003 | `--log` |
| `OPTION_LOGLEVEL` | 1004 | `--log-level` |
| `OPTION_TCLI` | 1005 | `--tcli` |
| `OPTION_DB_FILE` | 1006 | `--db_file` |
| `OPTION_LOGGING` | 1007 | `--log` 的旧写法 |
| `OPTION_LIMIT_FDS` | 1008 | `--limit-fds` |
| `OPTION_SCRIPTDIR` | 1009 | `--scriptdir` |

在 `frr_opt()` 的大型 `switch` 里，这些和普通字符 case 混在一起处理
（如 [:689](lib/libfrr.c#L689) `OPTION_MODULEDIR`、[:698](lib/libfrr.c#L698)
`OPTION_SCRIPTDIR`、[:731](lib/libfrr.c#L731) `OPTION_LIMIT_FDS`）。

### 3.5 选项分发 `frr_opt()`（[lib/libfrr.c:519](lib/libfrr.c#L519)）

返回值语义：

- `0` —— 继续处理下一个选项；
- `1` —— 该选项**被 flags 禁用**，`frr_getopt` 应停止循环（[:753](lib/libfrr.c#L753)），
  交由守护进程的 `default:` 处理。

关键分支：

| 选项 | 行号 | 行为 |
|---|---|---|
| `-d` | [:535](lib/libfrr.c#L535) | `di->daemon_mode = true` |
| `-M` | [:538](lib/libfrr.c#L538) | 把 `optarg` 挂到 `modules` 链表 |
| `-i` | [:571](lib/libfrr.c#L571) | `di->pid_file = optarg` |
| `-f` | [:576](lib/libfrr.c#L576) | `di->config_file = optarg` |
| `-N` | [:581](lib/libfrr.c#L581) | 设 `di->pathspace`，**重算 runstatedir / libstatedir**（[:601-603](lib/libfrr.c#L601)） |
| `-C` | [:630](lib/libfrr.c#L630) | `di->dryrun = true` |
| `-t` | [:635](lib/libfrr.c#L635) | 终端模式 |

### 3.6 `-N` / `-I` / `-i` / `-C` / `-t` / `-d` 语义对照

| 选项 | 名称 | 效果 | 落地位置 |
|---|---|---|---|
| `-N` | pathspace | 给配置/socket/pid 路径插前缀，实现「一套主机跑多份 FRR」 | [:581-609](lib/libfrr.c#L581) |
| `-I` | 实例号 | 每个 daemon 自己处理（如 bgpd 的 `bgp_instance`） | 非 libfrr 核心 |
| `-i` | pid 文件 | 覆盖 `di->pid_file` | [:571](lib/libfrr.c#L571) |
| `-C` | dryrun | 读配置后立刻 `exit(0)`，不守护化、不进循环（属 `os_cfg` 组） | [:1178-1181](lib/libfrr.c#L1178) |
| `-t` | terminal | 保留 tty，前台交互式运行（属 `os_fullcli` 组） | [:635](lib/libfrr.c#L635) |
| `-d` | daemon | fork + setsid，父进程握手后退出 | [:535](lib/libfrr.c#L535) |

`-N` 的细节（[:581-609](lib/libfrr.c#L581)）值得单独看：

- 重复指定报错：`"-N/--pathspace option specified more than once!"`（[:584](lib/libfrr.c#L584)）；
- 禁止 `-N` 里含 `/` 或 `.`（[:594](lib/libfrr.c#L594)）——防止路径穿越；
- 直接改写 `frr_runstatedir` / `frr_libstatedir` 为 `FRR_RUNSTATE_PATH "/" pathspace`
  （[:601-603](lib/libfrr.c#L601)）；
- `frr_sysconfdir` **是常量数组**，不加 pathspace 后缀；配置文件的命名空间
  是在 `frr_init()` 里用 `p_pathspace` 拼出来的（[:848-853](lib/libfrr.c#L848)）。

所以最终配置文件路径是
`<sysconfdir>/<pathspace>/<name>[-<instance>].conf`
（[:852-853](lib/libfrr.c#L852)），pid 是
`<runstatedir>/<name>[-<instance>].pid`（[:854-855](lib/libfrr.c#L854)）。

### 3.7 帮助输出 `frr_help_exit()`（[lib/libfrr.c:487](lib/libfrr.c#L487)）

```c
void frr_help_exit(int status)
{
	FILE *target = status ? stderr : stdout;
	if (status != 0) fprintf(stderr, "Invalid options.\n\n");
	if (di->printhelp) di->printhelp(target);
	else fprintf(target, "Usage: %s [OPTION...]\n\n%s%s%s\n\n%s", …);
	fprintf(target, "\nReport bugs to %s\n", FRR_BUG_ADDRESS);
	exit(status);
}
```

两种输出策略：守护进程可以提供自己的 `printhelp`（如 zebra 打印一段很长的
自定义用法），否则 libfrr 用 `proghelp` + `copyright` + `comb_helpstr` 拼装。

---

## §4 生命周期钩子

### 4.1 钩子的声明与调用

钩子框架在 [lib/hook.h](lib/hook.h) / [lib/hook.c](lib/hook.c)。
libfrr 定义了**六个**钩子（[lib/libfrr.c:65-70](lib/libfrr.c#L65)）：

```c
DEFINE_HOOK(frr_early_init, (struct event_loop * tm), (tm));   /* :65 */
DEFINE_HOOK(frr_late_init,  (struct event_loop * tm), (tm));   /* :66 */
DEFINE_HOOK(frr_config_pre, (struct event_loop * tm), (tm));   /* :67 */
DEFINE_HOOK(frr_config_post,(struct event_loop * tm), (tm));   /* :68 */
DEFINE_KOOH(frr_early_fini, (), ());                            /* :69 */
DEFINE_KOOH(frr_fini,       (), ());                            /* :70 */
```

- `DECLARE_HOOK(...)` 在 [lib/hook.h:263](lib/hook.h#L263) 声明一个钩子类型；
- `DEFINE_HOOK(...)` 在 [lib/hook.h:355](lib/hook.h#L355) 定义它；
- `DECLARE_KOOH` [:295](lib/hook.h#L295) / `DEFINE_KOOH` 是「无参数」版本，
  内部走 `DEFINE_HOOK_INT` 的变体。`KOOH` 就是 `HOOK` 倒过来写，
  表示**这是关闭方向**的钩子（对应 `frr_fini` 的逆序语义）。

`DEFINE_HOOK_INT`（[lib/hook.h:324](lib/hook.h#L324)）只生成**两个**东西：

```c
#define DEFINE_HOOK_INT(hookname, arglist, passlist, rev)                      \
	struct hook _hook_##hookname = {                                       \
		.name = #hookname, .entries = NULL, .reverse = rev,            \
	};                                                                     \
	static int hook_call_##hookname HOOK_VOIDIFY arglist                   \
	{                                                                      \
		int hooksum = 0;                                               \
		struct hookent *he = _hook_##hookname.entries;                 \
		… union { void *voidptr; int(*fptr)…; int(*farg)…; } hookp;    \
		for (; he; he = he->next) {                                    \
			hookp.voidptr = he->hookfn;                            \
			if (!he->has_arg) hooksum += hookp.fptr passlist;      \
			else              hooksum += hookp.farg HOOK_ADDARG passlist; \
		}                                                              \
		return hooksum;                                                \
	}
```

1. 一个**全局** `struct hook _hook_##hookname`（存订阅者链表）；
2. 一个**static** `hook_call_##hookname(...)` —— `hook_call` 宏（[lib/hook.h:257](lib/hook.h#L257)）
   就展开成它。

> **关键**：`hook_call_##hookname` 是 `static`，**只有包含 `DEFINE_HOOK` 的那个
> 编译单元**能调用它。所以 `hook_call(frr_early_init, master)` 只可能写在
> libfrr.c 里（那里 `DEFINE_HOOK` 了它，见 [lib/libfrr.c:909](lib/libfrr.c#L909)）；
> 别处想触发这个钩子做不到。这是刻意的——**触发点唯一且可见**。

注册走另一条路（[lib/hook.h:214](lib/hook.h#L214)）：

```c
#define hook_register(hookname, func)                                          \
	_hook_reg_svar(&_hook_##hookname, _hook_typecheck_##hookname(func),    \
		       NULL, false, THIS_MODULE, #func, HOOK_DEFAULT_PRIORITY)
```

`_hook_reg_svar`（[lib/hook.h:198](lib/hook.h#L198)）的手法很讲究：在调用处声明一个
`static struct hookent stack_hookent = {}`——**放进数据段、零初始化、进程内唯一**，
所以不需要 malloc，也不需要模块自己管理生命周期。注释 [:189-191](lib/hook.h#L189)
说明了理由：绝大多数 `hook_register` 不在循环里，用静态表项最省。

`_hook_typecheck_##hookname(func)` 是 `DECLARE_HOOK`（[lib/hook.h:263](lib/hook.h#L263)）
生成的**编译期内联类型检查函数**：签名不匹配直接编译报错。这是这套框架「类型安全」
的地方。另外还派生出一族宏：

| 宏 | 行号 | 用途 |
|---|---|---|
| `hook_register` | [:214](lib/hook.h#L214) | 无参、默认优先级 |
| `hook_register_arg` | [:217](lib/hook.h#L217) | 带 `arg` 附加参数、默认优先级 |
| `hook_register_prio` | [:221](lib/hook.h#L221) | 指定优先级 |
| `hook_register_arg_prio` | [:224](lib/hook.h#L224) | 带参数 + 指定优先级 |
| `hook_unregister` / `_arg` | [:236](lib/hook.h#L236) | 注销（按 `(funcptr,arg,has_arg)` 三元组匹配） |
| `hook_have_hooks` | [:247](lib/hook.h#L247) | 判断有没有订阅者（可做提前退出优化） |

返回值语义：`hook_call_*` 把所有回调的返回值**累加**后返回（`hooksum`）。
libfrr 的六个钩子回调都返回 `int`，但调用点基本忽略这个和。

### 4.2 排序与 `reverse` 的实现位置

`struct hookent`（[lib/hook.h:141](lib/hook.h#L141)）里有：

```c
struct frrmod_runtime *module;   /* 谁注册的（模块） */
int priority;                    /* 排序键 */
```

`_hook_register()`（[lib/hook.c:72](lib/hook.c#L72)）用**插入排序**把新项插到正确位置，
比较式在 [lib/hook.c:91-94](lib/hook.c#L91)：

```c
for (pos = &hook->entries; *pos; pos = &(*pos)->next)
	if (hook->reverse ? (*pos)->priority < priority
			  : (*pos)->priority >= priority)
		break;
he->next = *pos;  *pos = he;
```

顺带一个细节：`_hook_register` 先看 `stackent->ent_used`
（[lib/hook.c:78](lib/hook.c#L78)）——如果这个静态表项已经被用过
（同一处注册了第二次），就退回 `XCALLOC(MTYPE_HOOK_ENTRY, …)` 从堆上分配
（[:80-83](lib/hook.c#L80)）。所以「同一行代码注册多次」不会覆盖，只是多花一次分配。

**这是本框架最容易看错的地方**：`reverse` 是在**注册时**通过比较式实现的，

- `reverse = false`：按 priority **升序**排列，`hook_call_*` 顺序执行；
- `reverse = true`：按 priority **降序**排列，于是 `hook_call_*` 顺序执行时就等价于
  「优先级高的先跑」——即**逆序**。

而 `hook_call_*` 本身**永远是从头到尾正向遍历**（生成于
[lib/hook.h:324](lib/hook.h#L324) 的宏）。所以「逆序」不是遍历方向，而是**排列顺序**。

### 4.3 六大钩子与调用点

| 钩子 | 参数 | 调用点 | 时机 | 方向 |
|---|---|---|---|---|
| `frr_early_init` | `master` | [libfrr.c:909](lib/libfrr.c#L909) | 事件循环/信号/CLI 就绪后，**配置前** | 正序 |
| `frr_late_init` | `master` | [libfrr.c:1174](lib/libfrr.c#L1174) | `frr_config_fork` 开头，**守护化前** | 正序 |
| `frr_config_pre` | `master` | [libfrr.c:1130](lib/libfrr.c#L1130) | 读配置文件**之前** | 正序 |
| `frr_config_post` | `master` | [libfrr.c:1165](lib/libfrr.c#L1165) | 读配置文件并提交**之后** | 正序 |
| `frr_early_fini` | 无 | [libfrr.c:1401](lib/libfrr.c#L1401) | 守护进程退出，**拆子系统前** | 逆序 |
| `frr_fini` | 无 | [libfrr.c:1411](lib/libfrr.c#L1411) | `frr_fini()` 最开头 | 逆序 |

`frr_early_fini` / `frr_fini` 用的是 `DEFINE_KOOH`，注册时 `reverse = true`，
所以**后注册的先跑**——正好是「资源释放逆序」的经典需求。

### 4.4 优先级约定（[lib/hook.h:102-112](lib/hook.h#L102)）

```
 -999 ...     0 ...  999   - main executable / daemon, or library
-1999 ... -1000            - modules registering calls that should run
                             before the daemon's bits
           1000 ... 1999   - modules calls that should run after daemon's
```

- 默认值 `HOOK_DEFAULT_PRIORITY = 1000`（[lib/hook.h:169](lib/hook.h#L169)）；
- 注释解释了为什么默认是 1000：**大多数 `hook_register()` 来自可加载模块**，
  默认让它们排在 daemon 自己注册的（0..999）之后；
- 想让模块**抢跑**，显式指定负数（-1999..-1000）。

模块注册的钩子还带 `module` 字段（[lib/hook.h:141](lib/hook.h#L141)），
所以卸载/调试时能查到「这个回调是哪来的」。

### 4.5 真实用法示例

守护进程与模块注册钩子的典型形态：

```c
/* 守护进程：配置读完后补自己的东西 */
hook_register(frr_config_post, my_config_post);

/* 模块：想在 daemon 之前跑 */
hook_register_prio(frr_early_init, -1500, my_very_early_init);
```

libfrr 自己在 [lib/libfrr.c:1765](lib/libfrr.c#L1765) 就注册了一个：

```c
hook_register(frr_config_post, frr_tcmalloc_lib_init);
```

把 tcmalloc 的内存释放定时器挂在「配置读完后」，因为释放速率由配置决定。

---

## §5 子系统初始化/收尾顺序对照

`frr_init()` 与 `frr_fini()` 的顺序**基本互为镜像**，但有刻意的差异：

| 子系统 | 初始化（frr_init） | 收尾（frr_fini） | 备注 |
|---|---|---|---|
| 路径/默认值 | [:840-859](lib/libfrr.c#L840) | — | 无资源 |
| privs 预初始化 | `zprivs_preinit` [:861](lib/libfrr.c#L861) | — | 与 `zprivs_init` 不是一回事 |
| 日志 | `zlog_init` [:864](lib/libfrr.c#L864) | `if (0) zlog_fini()` [:1434](lib/libfrr.c#L1434) | ★ 故意不释放 |
| 模块加载 | [:896-903](lib/libfrr.c#L896) | `frrmod_terminate` [:1451](lib/libfrr.c#L1451) | |
| privs 正式 | `zprivs_init` [:905](lib/libfrr.c#L905) | `zprivs_terminate` [:1428](lib/libfrr.c#L1428) | |
| 事件循环 | `event_master_create` [:907](lib/libfrr.c#L907) | `event_master_free` [:1430](lib/libfrr.c#L1430) | |
| 信号 | `signal_init` [:908](lib/libfrr.c#L908) | 无 | 注释 [:1429](lib/libfrr.c#L1429)：nothing needed |
| CLI | `cmd_init` [:917](lib/libfrr.c#L917) | `cmd_terminate` [:1416](lib/libfrr.c#L1416) | |
| VTY | `vty_init` [:927](lib/libfrr.c#L927) | `vty_terminate` [:1415](lib/libfrr.c#L1415) | |
| 线程 | `frr_pthread_init` [:931](lib/libfrr.c#L931) | `frr_pthread_finish` [:1427](lib/libfrr.c#L1427) | 必须在 RCU 之前 |
| 脚本 | `frrscript_init` [:933](lib/libfrr.c#L933) | `frrscript_fini` [:1425](lib/libfrr.c#L1425) | |
| 错误码 | `log_ref_init` [:936](lib/libfrr.c#L936) | `log_ref_fini` [:1422](lib/libfrr.c#L1422) | |
| 北向 | `nb_init` [:940](lib/libfrr.c#L940) | `nb_terminate` [:1417](lib/libfrr.c#L1417) | |
| YANG | （在 nb_init 内） | `yang_terminate` [:1418](lib/libfrr.c#L1418) | |
| 内存统计 | `frr_memory_init` [:947](lib/libfrr.c#L947) | `frr_memory_fini` [:1413](lib/libfrr.c#L1413) | |
| RCU | （构造函数 `rcu_preinit`） | `rcu_shutdown` [:1449](lib/libfrr.c#L1449) | **最后关** |
| 内存统计打印 | — | `log_memstats` [:1453](lib/libfrr.c#L1453) | 在 zlog 还活着时 |
| zlog 临时目录 | — | `zlog_tmpdir_fini` [:1454](lib/libfrr.c#L1454) | 最末尾 |

**两处顺序约束值得单独记**：

1. `frr_pthread_finish()` 必须在 `rcu_shutdown()` 之前（[:1427](lib/libfrr.c#L1427) vs
   [:1449](lib/libfrr.c#L1449)）。RCU 要等所有读线程退出才安全。
2. `log_memstats()` 在 `zlog_fini` **之后**（当然，因为 `zlog_fini` 被禁用了）
   但在 `zlog_tmpdir_fini` **之前**——内存统计报告本身要写日志。

---

## §6 守护化与进程身份

### 6.1 `frr_daemonize()`（[lib/libfrr.c:1081](lib/libfrr.c#L1081)）

```c
static void frr_daemonize(void)
{
	int fds[2]; pid_t pid;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) { perror(…); exit(1); }
	set_cloexec(fds[0]);  set_cloexec(fds[1]);         /* :1090-1091 */
	pid = fork();
	if (pid < 0) { perror("fork()"); exit(1); }
	if (pid == 0) {                    /* 子进程 = 真正的守护进程 */
		close(fds[0]);
		if (setsid() < 0) { perror("setsid()"); exit(1); }
		daemon_ctl_sock = fds[1];      /* :1106  留着给 -t 用 */
		return;                        /* :1107  回到 frr_config_fork 继续 */
	}
	close(fds[1]);
	frr_daemon_wait(fds[0]);           /* 父进程：握手后退出（NORETURN） */
}
```

要点：

- **不做 double-fork**。只 fork 一次 + `setsid()`。因为配置文件、日志这些都
  已经打开好了，不需要再脱离一次。
- **不 `chdir("/")`**，理由见 §1.4。
- `daemon_ctl_sock` 是**子进程侧**保存的 socket 端，供后续 `-t` 模式的
  `frr_daemon_ctl()` 接收父进程信号转发（[:1362-1366](lib/libfrr.c#L1362)）。

### 6.2 父子握手协议 `frr_daemon_wait()`（[lib/libfrr.c:988](lib/libfrr.c#L988)）

父进程的等待逻辑（`FRR_NORETURN`，必然 `exit`）：

```c
static FRR_NORETURN void frr_daemon_wait(int fd)
{
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGTSTP); sigaddset(&sigs, SIGQUIT); sigaddset(&sigs, SIGINT);
	sigprocmask(SIG_BLOCK, &sigs, &prevsigs);           /* :1000 */
	struct sigaction sa = {.sa_handler = rcv_signal, .sa_flags = SA_RESETHAND};
	sigaction(SIGTSTP, &sa, NULL); /* SIGQUIT, SIGINT 同理 */  /* :1006-1008 */
	do {
		pfd[0].fd = fd; pfd[0].events = POLLIN;
		rcvd_signal = 0;
#if defined(HAVE_PPOLL)
		ret = ppoll(pfd, 1, NULL, &prevsigs);            /* :1020 */
#elif defined(HAVE_POLLTS)
		ret = pollts(pfd, 1, NULL, &prevsigs);
#else
		/* racy -- only used on FreeBSD 9 */              /* :1024 */
		ret = poll(pfd, 1, -1);
#endif
		switch (rcvd_signal) {
		case SIGTSTP: send(fd, "S", 1, 0); recv(…); raise(SIGTSTP);
		              sigaction(SIGTSTP, &sa, NULL); send(fd, "R", 1, 0); break;
		case SIGINT:  send(fd, "I", 1, 0); break;
		case SIGQUIT: send(fd, "Q", 1, 0); break;
		}
	} while (ret <= 0);
	exitpid = waitpid(-1, &exitstat, WNOHANG);           /* :1055 */
	if (exitpid == 0) exit(0);        /* 子进程已进主循环并关掉 socket */
	/* 否则打印子进程失败原因并 exit(1) */                 /* :1061-1073 */
}
```

**协议字符**（父 → 子）：

| 字节 | 含义 | 子进程处理（[libfrr.c:1306](lib/libfrr.c#L1306)） |
|---|---|---|
| `'S'` | 挂起 | `vty_stdio_suspend()`，回 `'s'` 确认 |
| `'R'` | 恢复 | `vty_stdio_resume()` |
| `'I'` | SIGINT | `di->daemon_mode = false; raise(SIGINT)` |
| `'Q'` | SIGQUIT | `di->daemon_mode = true; vty_stdio_close()` |
| （关闭） | 启动完成 | 子进程 closes socket → 父进程 `poll` 返回可读 → `exit(0)` |

**父进程如何判断成功**：`poll` 返回且 `waitpid(WNOHANG)` 得到 0 ——
说明子进程还没退出（还在跑），那就 `exit(0)`（[:1056-1058](lib/libfrr.c#L1056)）。
如果 `waitpid` 拿到了子进程且状态不好，就打印 `"%s failed to start, exited %d"`
之类并 `exit(1)`（[:1061-1073](lib/libfrr.c#L1061)）。子进程失败的原因靠
**exit status / signal 号**传递，不靠 socket 传字符串。

### 6.3 pid 文件与单实例强制（[lib/pid_output.c:54](lib/pid_output.c#L54)）

```c
pid_t pid_output(const char *path)
{
	fd = open(path, O_RDWR | O_CREAT, PIDFILE_MASK);  /* :66  PIDFILE_MASK=0644 */
	set_cloexec(fd);                              /* :79  不泄漏给子进程 */
	lock.l_type = F_WRLCK;                        /* :81 */
	lock.l_whence = SEEK_SET; lock.l_start = 0; lock.l_len = 0;
	if (fcntl(fd, F_SETLK, &lock) < 0) { ... }    /* :84  ★单实例判定 */
	… write(fd, buf, pidsize);
	ftruncate(fd, pidsize);                       /* :103 ★截断旧内容 */
}
```

关键设计（文件头注释 [lib/pid_output.c:20-52](lib/pid_output.c#L20) 讲得很清楚）：

- **用 POSIX 记录锁而不是「文件存在性」判断**。因为 pid 文件可能因为
  `kill -9` 而残留，靠存在性判断会误杀。
- **用 `F_SETLK`（非阻塞）而不是 `F_SETLKW`（阻塞）**：拿不到锁立刻失败退出，
  不挂起。这保证 `bgpd -d` 在「已经有一个在跑」时**立即**报错返回。
- **必须 `ftruncate`**：如果新 pid 比旧 pid 短（比如 `12345` → `999`），
  不截断就会读出 `99945` 这种垃圾。注释 [:51-53](lib/pid_output.c#L51) 举了这个例子。
- `set_cloexec` 保证锁 fd 不会被子进程继承，否则 fork 出去的孩子也会「持有」
  这个锁，单实例判断就废了。

调用点在 [lib/libfrr.c:1194](lib/libfrr.c#L1194)，位于 `frr_config_fork()`
**守护化之后**，所以写进去的是真正的守护进程 pid。

### 6.4 `frr_is_after_fork` 的语义

| 位置 | 值 | 行号 |
|---|---|---|
| 定义初值 | `true` | [lib/libfrr.c:99](lib/libfrr.c#L99) |
| `frr_preinit()` | `false` | [lib/libfrr.c:398](lib/libfrr.c#L398) |
| `frr_config_fork()` | `true` | [lib/libfrr.c:1190](lib/libfrr.c#L1190) |

用途：判断「现在是不是那个**本该 fork** 的时刻已经过去了」。下游用它来断言
「不能在这个阶段创建线程」——参见 [lib/frr_pthread.c:238](lib/frr_pthread.c#L238)
的 `assert(frr_is_after_fork)`。注意它**不表示真的 fork 了**：
`-t` 模式不 fork，但标志一样是 true。

---

## §7 权限与能力

### 7.1 三段式：`preinit` → `init` → `terminate`

| 函数 | 行号 | 时机 | 做什么 |
|---|---|---|---|
| `zprivs_preinit(privs)` | [lib/privs.c:674](lib/privs.c#L674) | `frr_init` 早期 [:861](lib/libfrr.c#L861) | **不降权**，只做查询与准备：确定目标 uid/gid、为 capability 路径做铺垫 |
| `zprivs_get_ids(&ids)` | [lib/privs.c:905](lib/privs.c#L905) | [:862](lib/libfrr.c#L862) | 取普通用户 id，供 `zlog_init` chown |
| `zprivs_init(privs)` | [lib/privs.c:747](lib/libfrr.c#L747) | [:905](lib/libfrr.c#L905) | ★ **真正降权**：切 uid/gid 或保留 capability，并设置 `raise`/`lower` 回调 |
| `zprivs_terminate(privs)` | [lib/privs.c:864](lib/privs.c#L864) | `frr_fini` [:1428](lib/libfrr.c#L1428) | 收尾 |

**为什么三段**：`zlog_init()` 需要以**特权身份**运行（要 `chown` 日志文件与
`/var/tmp/frr` 目录给降权后的用户），而 `zprivs_init()` 会丢权限。
所以顺序必须是 preinit（准备）→ zlog_init（特权干活）→ init（降权）。

### 7.2 抽象能力 `zebra_capabilities_t`（[lib/privs.h:76](lib/privs.h#L76)）

```c
typedef enum {
	ZCAP_SETID, ZCAP_BIND, ZCAP_NET_ADMIN, ZCAP_SYS_ADMIN, ZCAP_NET_RAW,
	ZCAP_CHROOT, ZCAP_NICE, ZCAP_PTRACE, ZCAP_DAC_OVERRIDE,
	ZCAP_READ_SEARCH, ZCAP_FOWNER, ZCAP_IPC_LOCK, ZCAP_SYS_RAWIO,
	ZCAP_MAX
} zebra_capabilities_t;
```

`ZCAP_MAX` 是**哨兵**，不是真能力（[lib/privs.h:90](lib/privs.h#L90)）。
映射表 `cap_map[ZCAP_MAX]` 在 [lib/privs.c:183](lib/privs.c#L183)：

```c
static struct {
	int num;
	pvalue_t *system_caps;
} cap_map[ZCAP_MAX] = {
	[ZCAP_SETID]     = { 2, (pvalue_t[]){CAP_SETGID, CAP_SETUID} },
	[ZCAP_BIND]      = { 1, (pvalue_t[]){CAP_NET_BIND_SERVICE} },
	[ZCAP_NET_ADMIN] = { 1, (pvalue_t[]){CAP_NET_ADMIN} },
	…
};
```

注意是**一对多**：`ZCAP_SETID` 对应两个系统能力。
所以遍历时是先累加 `num`（[:259](lib/privs.c#L259)）再展平（[:276-278](lib/privs.c#L276)）。

bgpd 需要的能力（[bgpd/bgp_main.c:142](bgpd/bgp_main.c#L142)）：

```c
static zebra_capabilities_t _caps_p[] = {ZCAP_BIND, ZCAP_NET_RAW, …};
struct zebra_privs_t bgpd_privs = { …, .caps_p = _caps_p,
                                       .cap_num_p = array_size(_caps_p), … };
```

### 7.3 能力模式初始化（[lib/privs.c:380](lib/privs.c#L380)）

`zprivs_caps_init()` 的核心步骤（注释在 [:371-379](lib/privs.c#L371)）：

```c
static void zprivs_caps_init(struct zebra_privs_t *zprivs)
{
	…
	if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1) { … }   /* :389 */
	…
}
```

`PR_SET_KEEPCAPS` 告诉内核「**换 uid 时保留能力**」。没有它，`setuid()` 到
普通用户会丢掉全部 capability，之后就没法再 `bind()` 特权端口了。
这是 Linux 能力模型里最容易踩的坑。

### 7.4 `frr_with_privs`（[lib/privs.h:234](lib/privs.h#L234)）

```c
#define frr_with_privs(privs)                                                  \
	for (struct zebra_privs_t *_once = NULL,                               \
	     *_privs __attribute__((unused, cleanup(_zprivs_lower))) =         \
		     _zprivs_raise(privs, __func__);                           \
	     _once == NULL; _once = (void *)1)
```

用法：

```c
frr_with_privs(&bgpd_privs) {
	/* 需要特权的代码：bind / setsockopt / ioctl */
	if (error) break;      /* 用 break 跳出也安全 */
}
```

三个设计要点（注释 [:223-233](lib/privs.h#L223)）：

1. **`cleanup(_zprivs_lower)`**：变量离开作用域自动降权。所以
   `return` / `goto` / `break` 都不会漏掉降权——这解决了「提权后中途 return
   忘记降权」这类经典 bug。
2. **代码块一定会执行**：即使 `_zprivs_raise` 失败、即使传 `NULL`（表示维持当前
   权限）。理由：某些部署（外部 RBAC、容器 capability 已足）本来就不需要提权，
   代码应当在无特权下也能跑通。
3. **参数可为 `NULL`**：`frr_with_privs(cond ? &privs : NULL)` 实现按条件提权
   （[:199-201](lib/privs.h#L199)）。

`_zprivs_raise`（[lib/privs.c:605](lib/privs.c#L605)）与
`_zprivs_lower`（[:639](lib/privs.c#L639)）内部用
`get_privs_refs()`（[:571](lib/privs.c#L571)）做**引用计数**，
所以嵌套 `frr_with_privs` 是安全的：只有最外层降权时才真的降。

---

## §8 模块系统

### 8.1 两种模块声明

| 宏 | 行号 | 用于 | 导出符号 |
|---|---|---|---|
| `FRR_COREMOD_SETUP(...)` | [lib/module.h:134](lib/module.h#L134) | **编译进主程序**的模块（每个 daemon 自己的 `*_main.c`） | 无（`DSO_LOCAL`） |
| `FRR_MODULE_SETUP(...)` | [lib/module.h:147](lib/module.h#L147) | **可动态加载**的 `.so` 模块 | `frr_module`（`DSO_SELF`） |

```c
#define FRR_COREMOD_SETUP(...)                                                 \
	static const struct frrmod_info _frrmod_info = {__VA_ARGS__};          \
	DSO_LOCAL union _frrmod_runtime_u _frrmod_this_module = {{             \
		NULL, &_frrmod_info,                                           \
	}};                                                                    \
	XREF_SETUP();                                                          \
	MACRO_REQUIRE_SEMICOLON()

#define FRR_MODULE_SETUP(...)                                                  \
	FRR_COREMOD_SETUP(__VA_ARGS__);                                        \
	DSO_SELF struct frrmod_runtime *frr_module = &_frrmod_this_module.r;   \
	MACRO_REQUIRE_SEMICOLON()
```

差别只有两行：`FRR_MODULE_SETUP` 额外导出一个 `frr_module` 指针供 `dlsym` 找。

### 8.2 数据结构

`struct frrmod_info`（静态元信息，编译期常量，[lib/module.h:57](lib/module.h#L57)）：

```c
struct frrmod_info {
	const char *name;          /* 单行、少词 */
	const char *version;       /* 人类可读，不含空格 */
	const char *description;   /* 一段话 */
	int (*init)(void);         /* 可选；返回非 0 表示失败 */
};
```

`struct frrmod_runtime`（运行时信息，[lib/module.h:87](lib/module.h#L87)）：

```c
struct frrmod_runtime {
	struct frrmod_runtime *next;
	const struct frrmod_info *info;
	void *dl_handle;           /* dlopen 句柄（仅可加载模块） */
	bool finished_loading;
	char *load_name;           /* 加载名 */
	char *load_args;           /* -M name:args 的 args */
};
```

**ABI 预留**是这里最精巧的设计（[lib/module.h:104](lib/module.h#L104)）：

```c
struct _frrmod_runtime_size {
	struct frrmod_runtime r;
	uint8_t space[1024 - sizeof(struct frrmod_runtime)];  /* 补齐到 1024 */
};
union _frrmod_runtime_u {
	struct frrmod_runtime r;
	struct _frrmod_runtime_size s;
};
```

`frrmod_runtime` 一旦超过 1024 字节，`space[]` 变负数就会**编译报错**。
这样将来往结构里加字段时，旧 `.so` 和新 libfrr 之间的内存布局仍是兼容的
（`.so` 按 1024 字节预留，libfrr 只读它认识的前缀）。

`THIS_MODULE`（[lib/module.h:126](lib/module.h#L126)）取当前模块的运行时指针：

```c
#define THIS_MODULE (&_frrmod_this_module.r)
```

### 8.3 全局链表与 libfrr 自身

[lib/module.c:60-103](lib/module.c#L60)：

```c
static struct frrmod_info frrmod_default_info = {
	.name = "libfrr", .version = FRR_VERSION,
	.description = "libfrr core module",
};                                                          /* :60 */
union _frrmod_runtime_u frrmod_default = {
	.r = { .info = &frrmod_default_info, .finished_loading = 1 },
};                                                          /* :69 */
XREF_SETUP();
#pragma weak _frrmod_this_module = frrmod_default        /* :91 */
struct frrmod_runtime *frrmod_list = &frrmod_default.r;    /* :101 */
static struct frrmod_runtime **frrmod_last = &frrmod_default.r.next;  /* :102 */
static const char *execname = NULL;                        /* :103 */
```

- **libfrr 自己就是链表的第一个模块**，`finished_loading = 1`（永远视为已加载）。
- `#pragma weak _frrmod_this_module = frrmod_default`（[:91](lib/module.c#L91)）：
  没有 `FRR_COREMOD_SETUP` 的编译单元里用 `THIS_MODULE`，会回退到 libfrr 自身。

`frrmod_init()`（[lib/module.c:110](lib/module.c#L110)）：

```c
void frrmod_init(struct frrmod_runtime *modinfo)
{
	modinfo->finished_loading = true;
	*frrmod_last = modinfo;
	frrmod_last = &modinfo->next;
	execname = modinfo->info->name;    /* :117 ★ 决定模块文件名的前缀 */
}
```

`execname` 被设为模块名（如 `"bgpd"`），用于拼出 `bgpd_<mod>.so` 的查找路径。

### 8.4 `frrmod_load()` 查找顺序（[lib/module.c:143](lib/module.c#L143)）

参数 `spec` 形如 `name` 或 `name:args`：

```c
snprintf(name, sizeof(name), "%s", spec);
args = strchr(name, ':');
if (args) *args++ = '\0';                      /* :193-195 拆出参数 */
if (!strchr(name, '/')) {                      /* :197  名字里没有 / 才搜索 */
	if (execname) {
		snprintf(fullpath, …, "%s/%s_%s.so", dir, execname, name);
		handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);       /* :210 */
	}
	if (!handle) {
		snprintf(fullpath, …, "%s/%s.so", dir, name);
		handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);       /* :218 */
	}
}
if (!handle) {
	snprintf(fullpath, …, "%s", name);         /* :226  当成原始路径 */
	handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
}
```

**三级查找**：

1. `<dir>/<execname>_<name>.so` —— 例如 `bgpd -M bmp` 找 `/usr/lib/frr/modules/bgpd_bmp.so`
2. `<dir>/<name>.so` —— 例如 `-M bmp` 找 `.../bmp.so`
3. 原始 `name` —— 例如 `-M /path/to/foo.so`（名字含 `/` 时直接跳到这一步）

`dir` 来自 [lib/libfrr.c:838](lib/libfrr.c#L838)：
`di->module_path ? di->module_path : frr_moduledir`。

然后（[lib/module.c:240](lib/module.c#L240)）：

```c
rtinfop = dlsym(handle, "frr_module");     /* :240 ★固定符号名 */
if (!rtinfop) { dlclose(handle); … return NULL; }
rtinfo = *rtinfop;
rtinfo->load_name = XSTRDUP(MTYPE_MODULE_LOADNAME, name);
rtinfo->dl_handle = handle;
if (args) rtinfo->load_args = XSTRDUP(MTYPE_MODULE_LOADARGS, args);
if (rtinfo->finished_loading) { … "already loaded" …; goto out_fail; }
if (info->init && info->init()) { … "initialisation failed" …; goto out_fail; }
rtinfo->finished_loading = true;
*frrmod_last = rtinfo;  frrmod_last = &rtinfo->next;
```

要点：

- `RTLD_NOW | RTLD_GLOBAL`（[:210](lib/module.c#L210) 等）：立即解析全部符号，
  且模块符号对后续加载的模块可见。
- **重复加载被拒绝**（`finished_loading` 检查）。
- `info->init()` 返回非 0 视为失败，会 `dlclose` 并回滚。
- 参数通过 `THIS_MODULE->load_args` 读取——这是模块拿到 `-M name:args` 里
  `args` 的唯一途径。
- **错误上报有上限**：`FRRMOD_LOAD_N_ERRSTR = 10`（[lib/module.c:152](lib/module.c#L152)），
  超过 10 条丢弃。错误先攒在 `aErr[]` 里，最后统一通过 `pFerrlog` 回调发出
  ——libfrr 传的回调就是 `_err_print()`（[lib/libfrr.c:814](lib/libfrr.c#L814)），
  它把 `"前缀: 错误信息"` 打到 stderr。

### 8.5 没有卸载

[lib/module.c:295](lib/module.c#L295) 是 `frrmod_terminate()`，
而 `frrmod_unload()` 在 `#if 0` 块里（`#if 0` 在 [lib/module.c:285](lib/module.c#L285)，
函数体在 [:286](lib/module.c#L286)）。

`frrmod_terminate()` 只做资源释放（`load_name` / `load_args`），
**不 `dlclose`**。这是刻意的：FRR 里模块注册了大量钩子、命令、YANG 回调，
运行期卸载几乎不可能安全；进程退出时反正要死，直接不卸更简单。

### 8.6 树内真实模块清单

`grep -rn FRR_MODULE_SETUP` 的结果（去掉 `lib/module.h` 定义本身）：

| 模块名 | 文件 | 备注 |
|---|---|---|
| `bgpd_bmp` | [bgpd/bgp_bmp.c:4043](bgpd/bgp_bmp.c#L4043) | BMP 上报 |
| `bgpd_rpki` | [bgpd/bgp_rpki.c:3162](bgpd/bgp_rpki.c#L3162) | RPKI 校验（版本 0.3.6） |
| `bgpd_snmp` | [bgpd/bgp_snmp.c:219](bgpd/bgp_snmp.c#L219) | 需 `--enable-snmp` |
| `zebra_fpm` | [zebra/zebra_fpm.c:2073](zebra/zebra_fpm.c#L2073) | FPM 上报 |
| `dplane_fpm_nl` | [zebra/dplane_fpm_nl.c:1873](zebra/dplane_fpm_nl.c#L1873) | 数据面 FPM |
| `dplane_dpdk` | [zebra/dpdk/zebra_dplane_dpdk.c:610](zebra/dpdk/zebra_dplane_dpdk.c#L610) | DPDK 数据面（版本 0.0.1） |
| `zebra_snmp` | [zebra/zebra_snmp.c:555](zebra/zebra_snmp.c#L555) | |
| `frr_sysrepo` | [lib/northbound_sysrepo.c:775](lib/northbound_sysrepo.c#L775) | Sysrepo 北向 |
| `frr_pathd_pcep` | [pathd/path_pcep.c:376](pathd/path_pcep.c#L376) | PCEP |
| `ospfd_snmp` / `ospf6d_snmp` / `ripd_snmp` / `isisd_snmp` | [ospfd/ospf_snmp.c:2549](ospfd/ospf_snmp.c#L2549) 等 | SNMP 各协议 |
| `sample_plugin` | [zebra/sample_plugin.c:115](zebra/sample_plugin.c#L115) | 示例插件 |

可以看到一条清晰的分工线：**核心协议逻辑编译进主程序；旁路功能（SNMP、
BMP、RPKI、FPM、Sysrepo、PCEP）做成可加载模块**。

---

## §9 MTYPE 与错误码注册表

libfrr 用**构造函数属性自注册**解决了「一堆注册表条目要在 `main()` 之前就位」
的问题。两个注册表都靠这一招。

### 9.1 MTYPE：内存类型注册表

`struct memtype`（[lib/memory.h:45](lib/memory.h#L45)）与
`struct memgroup`（[lib/memory.h:57](lib/memory.h#L57)）；链表头
`mg_first`（[lib/memory.c:54](lib/memory.c#L54)，`static`）与尾部插入用的二级指针
`mg_insert`（[lib/memory.c:55](lib/memory.c#L55)，**非 static**）——后者是
整个 memory.c 里**唯一的非 static 可写全局变量**，
因为别的编译单元里由 `_DEFINE_MGROUP` 生成的构造函数要往这个链表里追加。

自注册宏：

| 宏 | 定义行号 | 构造函数属性所在行 | 作用 |
|---|---|---|---|
| `DECLARE_MGROUP` | [lib/memory.h:96](lib/memory.h#L96) | — | 声明一个 memgroup |
| `_DEFINE_MGROUP` | [lib/memory.h:99](lib/memory.h#L99) | `_CONSTRUCTOR(1000)` [:108](lib/memory.h#L108) | 定义并注册一个 memgroup |
| `DEFINE_MGROUP` | [lib/memory.h:126](lib/memory.h#L126) | 同上 | 对外名字 |
| `DEFINE_MGROUP_ACTIVEATEXIT` | [lib/memory.h:128](lib/memory.h#L128) | 同上 | ★ 退出时**不**报泄漏（zlog 用） |
| `DECLARE_MTYPE` | [lib/memory.h:132](lib/memory.h#L132) | — | 声明一个 memtype |
| `DEFINE_MTYPE_ATTR` | [lib/memory.h:138](lib/memory.h#L138) | `_CONSTRUCTOR(1001)` [:146](lib/memory.h#L146) | 定义并注册一个 memtype（可带属性） |
| `DEFINE_MTYPE` | [lib/memory.h:165](lib/memory.h#L165) | 同上 | 最常用 |
| `DEFINE_MTYPE_STATIC` | [lib/memory.h:170](lib/memory.h#L170) | 同上 | 文件内可见 |

**优先级 1000 vs 1001 很关键**：memgroup 必须先于它下面的 memtype 注册
（memtype 注册时要挂到 group 上），所以 group 用 1000、type 用 1001。
构造函数按优先级升序执行。

数据段（`_DATA_SECTION`）也参与：group 进 `"mgroups"` 段、type 进 `"mtypes"` 段，
这样构建时能把这些条目聚在一起，便于工具扫描。

分配与统计（[lib/memory.c](lib/memory.c)）：

| 函数 | 行号 | 作用 |
|---|---|---|
| `mt_count_alloc` | [:68](lib/memory.c#L68) | 记活跃次数、峰值、总字节 |
| `mt_count_free` | [:109](lib/memory.c#L109) | 递减 |
| `qmalloc` | [:142](lib/memory.c#L142) | |
| `qcalloc` | [:148](lib/memory.c#L148) | |
| `qfree` | [:178](lib/memory.c#L178) | |
| `log_memstats` | [:305](lib/memory.c#L305) | 退出统计入口（`frr_fini` [:1453](lib/libfrr.c#L1453) 调用） |

`log_memstats()` 的遍历回调在 [:237](lib/memory.c#L237)，
对**每个仍有活跃分配**的 MTYPE 做汇报。

上层宏 `XMALLOC`/`XFREE` 等在 [lib/memory.h:201-206](lib/memory.h#L201)。
**`XFREE` 会把指针置 NULL**（用 do-while 实现，且用了
`__attribute__((nonnull))` 之类约束），所以 `XFREE(MTYPE_TMP, p)` 之后
`p == NULL`，能防 double-free。

### 9.2 ferr：错误码引用表

`ferr_r` 是错误码类型（[lib/ferr.h:61](lib/ferr.h#L61)）：

```c
typedef int ferr_r;
```

每个守护进程分配一段错误码区间：

| 宏 | 行号 | 值 |
|---|---|---|
| `LIB_FERR_START` | [lib/ferr.h:151](lib/ferr.h#L151) | libfrr 区间起点 |
| `BGP_FERR_START` | [lib/ferr.h:143](lib/ferr.h#L143) | bgpd 区间起点 |
| `END_FERR` | [lib/ferr.h:177](lib/ferr.h#L177) | `0xFFFFFFFF` **哨兵** |

`struct log_ref`（[lib/ferr.h:184](lib/ferr.h#L184)）是表项：

```c
struct log_ref {
	enum log_ref_type type;
	uint32_t code;        /* 完整错误码 */
	const char *title;
	const char *description;
	const char *suggestion;
};
```

注册走哈希表：[lib/ferr.c:104](lib/ferr.c#L104) 定义 `struct hash *refs`，
`log_ref_add()`（[:133](lib/ferr.c#L133)）把条目插进去。整个注册表也靠构造函数
初始化（[lib/ferr.c:79](lib/ferr.c#L79) 的 `_CONSTRUCTOR(500)`）。

生命周期：`log_ref_init` / `log_ref_fini` / `log_ref_vty_init`
（[lib/ferr.h:222-224](lib/ferr.h#L222) 声明；[lib/libfrr.c:936-937](lib/libfrr.c#L936) 调用）。
`END_FERR` 哨兵让「遍历所有错误码」有终止条件——每个守护进程的错误表以
`{END_FERR, NULL, NULL, NULL}` 结尾。

> **常见误解**：结构体叫 `struct log_ref`，**不叫** `struct ferr_ref`；
> 初始化函数叫 `log_ref_init`，**不叫** `ferr_init`。FRR 树里没有
> `ferr_init` / `ferr_str` / `frr_error_codes` 这些名字。

---

## §10 信号体系

### 10.1 数据结构

`struct frr_signal_t`（[lib/sigevent.h:63](lib/sigevent.h#L63)）：

```c
struct frr_signal_t {
	int signal;                              /* 信号编号 */
	void (*handler)(void);                   /* 回调，无参无返回 */
	volatile sig_atomic_t caught;            /* :67 私有：信号到达时置 1 */
};
```

`caught` **必须是 `volatile sig_atomic_t`**（注释 [:35](lib/sigevent.h#L35)），
否则编译器可能把它优化到寄存器里，信号处理函数改了主循环看不见。

全局 `sigmaster`（[lib/sigevent.c:70](lib/sigevent.c#L70)）里有一个
`volatile sig_atomic_t caught`——**所有被跟踪信号的总旗标**。

### 10.2 两段式处理：信号处理函数只置位

`frr_signal_handler()`（[lib/sigevent.c:81](lib/sigevent.c#L81)）做且只做一件事：

```c
static void frr_signal_handler(int signo)
{
	… 遍历 sigmaster.signals，匹配 signo 的表项 → sig->caught = 1;   /* :90 */
	sigmaster.caught = 1;                                              /* :93 */
}
```

**不做任何 I/O、不分配内存、不打日志**（注释 [:78-79](lib/sigevent.c#L78)）。
这是信号处理函数的铁律：async-signal-safe。

真正的分发在主事件循环里，由 `frr_signal_timer()`
（[lib/sigevent.c:194](lib/sigevent.c#L194)）周期性检查：

```c
if (sigmaster.caught > 0) {            /* :162 */
	sigmaster.caught = 0;          /* :163 */
	/* must not read or set sigmaster.caught after here,          /* :164-165 */
	 * race condition with per-sig caught flags if one does */
	for (…) if (sig->caught > 0) {  /* :171 */
		sig->caught = 0;       /* :172 */
		sig->handler();
	}
}
```

> **关键竞态注释**（[:164-165](lib/sigevent.c#L164)）：一旦清零
> `sigmaster.caught`，就**不得再读写它**——否则会和 per-sig 的 `caught` 标志
> 产生竞态（可能在两者之间又来了一个新信号）。

### 10.3 `signal_init()`（[lib/sigevent.c:463](lib/sigevent.c#L463)）

```c
void signal_init(struct event_loop *m, int sigc, struct frr_signal_t signals[])
{
	trap_default_signals();                        /* :471 先装默认的处理 */
	while (i < sigc) {
		if (signal_set(signals[i].signal) < 0) exit(-1);   /* :475-476 */
		i++;
	}
	sigmaster.sigc = sigc;  sigmaster.signals = signals;      /* :480-481 */
#ifdef SIGEVENT_SCHEDULE_THREAD                        /* :483 */
	sigmaster.t = NULL;
	event_add_timer(m, frr_signal_timer, &sigmaster,
			FRR_SIGNAL_TIMER_INTERVAL, &sigmaster.t);     /* :485 */
#endif
}
```

- `trap_default_signals()`（[:371](lib/sigevent.c#L371)）先装**默认**处理：
  核心信号（SIGQUIT/SIGILL/SIGABRT/SIGFPE/SIGBUS/SIGSEGV/SIGSYS/…，见
  [:373-380](lib/sigevent.c#L373)）→ `core_handler`，退出信号 → `exit_handler`，
  `SIGPIPE` 等（[:402](lib/sigevent.c#L402)）→ `SIG_IGN`。
  **这些可以被守护进程自己的信号表覆盖**（注释 [:468-469](lib/sigevent.c#L468)）。
- 每个信号用 `signal_set()`（[:214](lib/sigevent.c#L214)）安装，它用
  `sigaction` 而非 `signal()`（注释 [:210](lib/sigevent.c#L210)）。
- 最后把周期性定时器挂到事件循环上——**信号处理因此不是「中断驱动」而是
  「定时轮询驱动」**。这避免了在信号上下文里做复杂操作。

### 10.4 多线程与信号（[lib/sigevent.h:124](lib/sigevent.h#L124)）

```c
static inline void frr_sigset_add_mainonly(sigset_t *blocksigs)
{
	sigaddset(blocksigs, SIGHUP);
	sigaddset(blocksigs, SIGINT);
	sigaddset(blocksigs, SIGTERM);
	sigaddset(blocksigs, SIGUSR1);
	…
}
```

创建线程时调用它，把「应用级信号」加入屏蔽集合，让它们**只在主线程被处理**。
但 SIGSEGV / SIGABRT 等致命信号**不在此列**——它们必须允许在任何线程发生，
否则崩在子线程里就没有 core dump / crashlog 了
（注释 [:112-118](lib/sigevent.h#L112)）。

### 10.5 守护进程的信号表

bgpd 的例子（[bgpd/bgp_main.c:118-135](bgpd/bgp_main.c#L118)）：

| 信号 | 回调 | 行为 |
|---|---|---|
| `SIGHUP` | `sighup()` [:162](bgpd/bgp_main.c#L162) | 只记日志并忽略（BGP 不支持 SIGHUP 重载配置） |
| `SIGUSR1` | `sigusr1()` [:197](bgpd/bgp_main.c#L197) | 轮转日志 |
| `SIGINT` / `SIGTERM` | `sigint()` | 优雅退出，最终走到 `bgp_exit()` |

这张表通过 `FRR_DAEMON_INFO` 的 `.signals` / `.n_signals`
（[bgpd/bgp_main.c:454-455](bgpd/bgp_main.c#L454)）传给 libfrr，
再由 `signal_init()`（[lib/libfrr.c:908](lib/libfrr.c#L908)）安装。

---

## §11 真实案例：bgpd 的 `main()`

把前面所有机制串起来看一遍。`main()` 起始于
[bgpd/bgp_main.c:471](bgpd/bgp_main.c#L471)。

### 11.1 声明阶段（文件作用域）

```c
static struct frr_daemon_info bgpd_di;                    /* :158 前向声明 */

static struct frr_signal_t bgp_signals[] = {              /* :118-135 */
	{ .signal = SIGHUP,  .handler = &sighup  },
	{ .signal = SIGUSR1, .handler = &sigusr1 },
	{ .signal = SIGINT,  .handler = &sigint  },
	{ .signal = SIGTERM, .handler = &sigint  },
};

static zebra_capabilities_t _caps_p[] = {ZCAP_BIND, ZCAP_NET_RAW, …};  /* :142 */

struct zebra_privs_t bgpd_privs = {                        /* :145-156 */
	.user = …, .group = …, .vty_group = …,
	.caps_p = _caps_p, .cap_num_p = array_size(_caps_p),
};

FRR_DAEMON_INFO(bgpd, BGP,                                  /* :450-461 */
	.vty_port = BGP_VTY_PORT,
	.proghelp = "Implementation of the BGP routing protocol.",
	.signals = bgp_signals, .n_signals = array_size(bgp_signals),
	.privs = &bgpd_privs,
	.yang_modules = bgpd_yang_modules,
	.n_yang_modules = array_size(bgpd_yang_modules),
);
```

注意 `FRR_DAEMON_INFO` 展开出的静态变量名字是 `bgpd_di`
（宏里是 `execname##_di`），所以前面 [:158](bgpd/bgp_main.c#L158) 的
`static struct frr_daemon_info bgpd_di;` 是**前置声明**，不是重复定义。

### 11.2 main() 逐段

```c
int main(int argc, char **argv)
{
	…
	frr_preinit(&bgpd_di, argc, argv);                    /* :490  ① */
	frr_opt_add("p:l:SnZe:I:s:x" DEPRECATED_OPTIONS,
		    longopts, "");                            /* :493  ②a */
	while (1) {
		opt = frr_getopt(argc, argv, 0);              /* :508  ②b */
		if (opt == EOF) break;
		switch (opt) { … case 'n': … default: frr_help_exit(1); }
	}
	…
	bgp_master_init(frr_init(), buffer_size, addresses);  /* :581  ③ */
	…
	bgp_error_init();
	…
	frr_config_fork();                                    /* :628  ④ */
	…
	frr_pthreads_run();                                   /* :633 */
	frr_run(bm->master);                                  /* :635  ⑤ */
}
```

中间那段（`bgp_master_init` 前后，[:581-628](bgpd/bgp_main.c#L581)）是 bgpd
的「私有初始化」：设置监听端口、是否连 zebra、是否装 FIB、GR 参数、
各子系统初始化、拼装启动信息串。

### 11.3 退出路径

```c
void bgp_exit(int status)                                 /* :214 */
{
	frr_early_fini();                                 /* :222 */
	…
	/* reverse bgp_master_init */                     /* :229 */
	…
	frr_fini();                                       /* :299 */
	exit(status);
}
```

即：

```
SIGINT/SIGTERM → sigint() → bgp_exit()
   frr_early_fini()   ← 触发 frr_early_fini 逆序钩子
   （bgpd 自己的资源释放，按 bgp_master_init 的逆序）
   frr_fini()         ← 触发 frr_fini 逆序钩子，拆掉全部子系统
   exit(status)
```

### 11.4 各守护进程的差异对照

| 守护进程 | `FRR_NO_*` 特点 | 备注 |
|---|---|---|
| `bgpd` | 无（全套） | 需要 zclient、需要能力 `ZCAP_BIND`/`ZCAP_NET_RAW` |
| `zebra` | 无 | `FRR_DAEMON_INFO` [:305](zebra/main.c#L305)，`frr_init` [:579](zebra/main.c#L579)、`frr_run` [:657](zebra/main.c#L657) |
| `mgmtd` | 可能 `FRR_LOAD_YANG_LIBRARY` | `FRR_DAEMON_INFO` [:191](mgmtd/mgmt_main.c#L191)，`frr_run` [:272](mgmtd/mgmt_main.c#L272) |
| `staticd` | `FRR_NO_ZCLIENT`（不直连 zebra） | 走别的进程间通道 |
| `watchfrr` | 大量 `FRR_NO_*` | 它不是网络守护进程 |

> **阅读建议**：随便挑一个 `*/​*_main.c`，按 §11.2 的标号找到这五步，
> 中间夹着的就是该协议的私有初始化——这就是 libfrr 抽象带来的**结构同构性**。

---

## §12 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | `frr_opt_add()` 在 `frr_getopt()` 之后调用 | 自定义选项「unknown option」 | 必须在 `frr_getopt` 之前 |
| 2 | 在 `FRR_DAEMON_INFO` 里写 `progname`/`cli_mode`/`graceful_restart` | 无效，会被 `frr_preinit` 覆盖 | 这些不用写 |
| 3 | `frr_getopt` 返回 1 时 `default:` 不处理 | 被禁用的选项被静默忽略 | `default: frr_help_exit(1)` |
| 4 | 忘记 `comb_optstr[256]` / `comb_lo[64]` 的容量 | 选项静默丢失/越界 | 加选项组时算预算 |
| 5 | `optspec.helpstr` 超长 | `comb_helpstr[4096]` 截断 | 精简帮助文本 |
| 6 | 以为 `FRR_NO_PID` 会「不写 pid 文件」 | 实际上仍写，只是用默认路径 | 见 §3.3 |
| 7 | 以为 `frr_is_after_fork` 表示「真的 fork 了」 | `-t` 模式不 fork 但标志仍为 true | 语义是「本该 fork 的时刻已过」 |
| 8 | 手动 `fork()` 而不走 `frr_daemonize()` | 父进程立刻退出，脚本拿不到「已就绪」保证 | 用 `frr_daemonize()` |
| 9 | 在 `frr_init()` 之后还想以 root 身份建目录 | 此时已 `zprivs_init()` 降权，失败 | 目录创建放在 [:886-894](lib/libfrr.c#L886) |
| 10 | 把 `zlog_init()` 放到 `zprivs_init()` 之后 | 无法 chown 日志文件/临时目录 | 顺序见 §7.1 |
| 11 | `frr_pthread_finish()` 放在 `rcu_shutdown()` 之后 | RCU 清扫线程可能在读线程还活着时就停 | 线程先停，RCU 后关 |
| 12 | 在退出路径上调用 `zlog_fini()` | 关闭期的最后几条日志丢失 | 别调，libfrr 已故意禁用 |
| 13 | 模块里忘了 `FRR_MODULE_SETUP` | `dlsym(handle,"frr_module")` 失败，加载报「不是 FRR 模块」 | 可加载模块必须用它 |
| 14 | 模块名里带 `/` | 跳过 `<dir>` 搜索，直接当路径 | 用文件名前缀形式 |
| 15 | 指望能卸载模块 | `frrmod_unload` 是 `#if 0`，不存在 | 设计上不支持 |
| 16 | 重复 `-M` 同一个模块 | `finished_loading` 检查拒绝，启动失败 | 别重复 |
| 17 | 自己写 pid 文件锁 | 与 `pid_output` 的 `F_SETLK` 冲突或漏掉 `ftruncate` | 用 `pid_output()` |
| 18 | pid 文件不 `ftruncate` | 短 pid 后面残留旧字符（`99945`） | `pid_output` 已处理；自己写要注意 |
| 19 | 用文件「存在性」判断单实例 | 残留文件导致误判 | 用记录锁 |
| 20 | 把 `cleanup(_zprivs_lower)` 换掉 | 中途 `return` 忘记降权 | 用 `frr_with_privs` |
| 21 | 在 `frr_with_privs` 块里期待「一定提权成功」 | 提权可能失败，块仍会执行 | 回调要能在无特权下工作 |
| 22 | 忘记 `prctl(PR_SET_KEEPCAPS, 1)` | `setuid` 后能力全丢，`bind()` 特权端口失败 | 由 `zprivs_caps_init` 处理；自己实现要记得 |
| 23 | 在信号处理函数里打日志/分配内存 | 死锁或崩溃（非 async-signal-safe） | 只置 `caught` 标志 |
| 24 | `caught` 不是 `volatile sig_atomic_t` | 主循环看不到信号 | 保持原类型 |
| 25 | 清零 `sigmaster.caught` 后再读它 | 与 per-sig 标志竞态 | 见 [lib/sigevent.c:164](lib/sigevent.c#L164) |
| 26 | 把致命信号也加入 `frr_sigset_add_mainonly` | 子线程崩溃无 core/crashlog | 只加应用级信号 |
| 27 | MTYPE 构造函数优先级搞反（type 用 1000） | type 注册时 group 还没建，挂不上 | group=1000、type=1001 |
| 28 | 用 `free()` 释放 `XMALLOC` 的内存 | MTYPE 统计错乱 | 一律 `XFREE` |
| 29 | 忘记 `XFREE` 会置 NULL，做了二次判断 | 多余的 NULL 检查 | 知道就好 |
| 30 | 错误表忘了 `END_FERR` 哨兵 | 遍历越界 | 每张表以 `{END_FERR, NULL,…}` 结尾 |
| 31 | 找 `struct ferr_ref` / `ferr_init` | 树里不存在 | 是 `struct log_ref` / `log_ref_init` |
| 32 | 钩子里以为 `reverse` 改变遍历方向 | 其实是改变**排列顺序** | 见 §4.2 |
| 33 | 模块注册钩子不写 priority，期待「早跑」 | 默认 1000，排在 daemon 之后 | 要抢跑就写负数 |
| 34 | `-N` 里带 `/` 或 `.` | 被拒绝（[:594](lib/libfrr.c#L594)） | 用纯名字 |
| 35 | 以为 `frr_sysconfdir` 会带 `-N` 后缀 | 它是常量；后缀在拼路径时加 | 见 §3.6 |
| 36 | 在 `frr_init()` 之后才 `frrmod_init` | 模块加载在 [:896](lib/libfrr.c#L896)，晚了 | 顺序固定 |
| 37 | `-C` 模式下期待进程继续跑 | `exit(0)` 直接退 | `-C` 是「只验配置」 |
| 38 | 以为 `frr_run()` 会返回 | 它进 `while(event_fetch)` 循环 | 退出靠 `exit()` |
| 39 | 守护化后还去访问父进程的变量 | 是两个进程，内存独立 | 别依赖 |
| 40 | 忘了 `frr_early_fini()` 就直接 `frr_fini()` | 守护进程自己的清理钩子不执行 | 两个都要调，顺序固定 |
| 41 | 想在守护进程里调 `hook_call(frr_config_post, …)` | `hook_call_##hookname` 是 **static**，只有 `DEFINE_HOOK` 所在文件可见，链接不过 | 触发点由 libfrr 独占；自己定义的钩子才能自己触发 |
| 42 | 以为 `hook_register` 需要 malloc 管理生命周期 | 它用的是数据段里的 `static struct hookent` | 不需要 free；`hook_unregister` 只是摘链 |
| 43 | 回调签名写错，期待运行时报错 | `_hook_typecheck_##hookname` 在**编译期**就报错 | 签名照 `DECLARE_HOOK` 写 |

---

## §13 自测题

1. `frr_preinit` / `frr_init` / `frr_config_fork` / `frr_run` 各自在哪一行？
   哪个阶段会「永不返回」？
2. `frr_is_after_fork` 的三次赋值分别在哪一行、值是什么？为什么 `-t` 模式下
   它也是 `true`？
3. `zprivs_preinit`、`zlog_init`、`zprivs_init` 三者的顺序是什么？如果调换
   `zlog_init` 与 `zprivs_init` 会发生什么？
4. `hook_call(frr_early_init, master)` 在 `frr_init()` 的哪一行？
   此时「配置读了吗」「CLI 就绪了吗」？
5. `hook->reverse` 在注册时如何影响排列？为什么 `hook_call_*` 本身不需要逆序遍历？
6. `HOOK_DEFAULT_PRIORITY` 是多少？注释给出的三个优先级区间分别给谁用？
7. `FRR_COREMOD_SETUP` 与 `FRR_MODULE_SETUP` 展开后的唯一差别是哪一行？
8. `frrmod_load()` 的三级查找路径分别是什么（按顺序）？
   `-M bmp` 在 bgpd 下会先找哪个文件？
9. `_frrmod_runtime_size` 里那个 `space[1024 - sizeof(...)]` 解决什么问题？
   如果 `frrmod_runtime` 超过 1024 字节会怎样？
10. `PR_SET_KEEPCAPS` 是干什么的？不加会怎样？
11. `frr_with_privs` 宏里 `cleanup(_zprivs_lower)` 的作用是什么？
    为什么说它「防止了一类经典 bug」？
12. `pid_output()` 为什么必须 `ftruncate()`？注释给的例子是什么？
13. 为什么 pid 文件用 `fcntl(F_SETLK)` 而不是 `F_SETLKW`？
14. `frr_daemon_wait()` 里父进程怎么判断「子进程启动成功了」？
15. `frr_daemonize()` 为什么不做 double-fork、不 `chdir("/")`？
16. `zlog_fini()` 被写在 `if (0)` 里的理由是什么？泄漏怎么处理？
17. `frr_pthread_finish()` 为什么必须排在 `rcu_shutdown()` 之前？
18. MTYPE 构造函数优先级为什么是 group=1000、type=1001？
19. `struct log_ref` 与 `END_FERR` 的关系是什么？
20. 信号处理函数 `frr_signal_handler()` 为什么只置标志？真正的分发在哪？
21. `frr_sigset_add_mainonly()` 为什么**不**包括 SIGSEGV/SIGABRT？
22. `sigmaster.caught = 0` 之后为什么不允许再读它？
23. `FRR_NO_PID` 到底禁用了什么？pid 文件还会写吗？
24. `-N` 的 pathspace 会加到 `frr_sysconfdir` 上吗？不会的话，配置文件路径
    里的命名空间是怎么来的？
25. bgpd 的 `main()` 五步分别在哪些行？`bgp_exit()` 里两个 `frr_*_fini` 的顺序？

---

## §14 代码索引与官方文档对应

### 14.1 按阅读顺序的代码索引

**第一遍：主干（约 400 行）**

| 顺序 | 位置 | 内容 |
|---|---|---|
| 1 | [lib/libfrr.h:157](lib/libfrr.h#L157) | `struct frr_daemon_info` 全字段 |
| 2 | [lib/libfrr.h:227](lib/libfrr.h#L227) | `FRR_DAEMON_INFO` 宏 |
| 3 | [lib/libfrr.c:65-70](lib/libfrr.c#L65) | 六个生命周期钩子 |
| 4 | [lib/libfrr.c:74-110](lib/libfrr.c#L74) | 目录常量、路径缓冲、全局标志、选项聚合缓冲 |
| 5 | [lib/libfrr.c:118-134](lib/libfrr.c#L118) | `struct optspec` + `opt_extend()` |
| 6 | [lib/libfrr.c:395-467](lib/libfrr.c#L395) | `frr_preinit()` |
| 7 | [lib/libfrr.c:478-502](lib/libfrr.c#L478) | `frr_opt_add()` / `frr_help_exit()` |
| 8 | [lib/libfrr.c:519-742](lib/libfrr.c#L519) | `frr_opt()` 分发 switch |
| 9 | [lib/libfrr.c:744-762](lib/libfrr.c#L744) | `frr_getopt()` |
| 10 | [lib/libfrr.c:829-950](lib/libfrr.c#L829) | `frr_init()` ★核心 |
| 11 | [lib/libfrr.c:988-1112](lib/libfrr.c#L988) | `frr_daemon_wait()` + `frr_daemonize()` |
| 12 | [lib/libfrr.c:1128-1196](lib/libfrr.c#L1128) | `frr_config_read_in()` + `frr_config_fork()` |
| 13 | [lib/libfrr.c:1343-1396](lib/libfrr.c#L1343) | `frr_run()` |
| 14 | [lib/libfrr.c:1399-1455](lib/libfrr.c#L1399) | `frr_early_fini()` + `frr_fini()` |

**第二遍：叶子机制**

| 位置 | 内容 |
|---|---|
| [lib/hook.c:72-125](lib/hook.c#L72) | `_hook_register` / `_hook_unregister`（插入排序 + reverse） |
| [lib/hook.h:141-169](lib/hook.h#L141) | `struct hookent` / `struct hook` / `HOOK_DEFAULT_PRIORITY` |
| [lib/hook.h:293-358](lib/hook.h#L293) | `DECLARE_HOOK` / `DEFINE_HOOK` / `DECLARE_KOOH` / `DEFINE_HOOK_INT` |
| [lib/module.h:57-171](lib/module.h#L57) | 模块两结构 + 两个 SETUP 宏 + ABI 预留 |
| [lib/module.c:60-117](lib/module.c#L60) | libfrr 自身模块 + `frrmod_init` |
| [lib/module.c:143-278](lib/module.c#L143) | `frrmod_load` 三级查找 + `dlsym` + 错误攒批 |
| [lib/privs.h:76-91](lib/privs.h#L76) | `zebra_capabilities_t` 枚举 |
| [lib/privs.h:217-241](lib/privs.h#L217) | `_zprivs_raise/lower` + `frr_with_privs` |
| [lib/privs.c:183-320](lib/privs.c#L183) | `cap_map[]` + `zprivs_change_caps` |
| [lib/privs.c:380-490](lib/privs.c#L380) | `zprivs_caps_init`（`PR_SET_KEEPCAPS`） |
| [lib/privs.c:571-660](lib/privs.c#L571) | 引用计数 + `_zprivs_raise` / `_zprivs_lower` |
| [lib/privs.c:674-905](lib/privs.c#L674) | `zprivs_preinit` / `zprivs_init` / `zprivs_terminate` |
| [lib/sigevent.h:63-130](lib/sigevent.h#L63) | `struct frr_signal_t` + `frr_sigset_add_mainonly` |
| [lib/sigevent.c:81-200](lib/sigevent.c#L81) | 信号处理函数 + 轮询分发 |
| [lib/sigevent.c:371-460](lib/sigevent.c#L371) | `trap_default_signals` |
| [lib/sigevent.c:463-488](lib/sigevent.c#L463) | `signal_init` |
| [lib/pid_output.c:54-111](lib/pid_output.c#L54) | `pid_output`（记录锁 + ftruncate） |
| [lib/memory.h:45-206](lib/memory.h#L45) | MTYPE/MGROUP 宏与构造函数优先级 |
| [lib/memory.c:54-320](lib/memory.c#L54) | 全局链表、分配统计、`log_memstats` |
| [lib/ferr.h:61-224](lib/ferr.h#L61) | `ferr_r` / `END_FERR` / `struct log_ref` |
| [lib/ferr.c:79-280](lib/ferr.c#L79) | 错误码哈希表与 `log_ref_add` |

**第三遍：真实守护进程**

| 位置 | 内容 |
|---|---|
| [bgpd/bgp_main.c:118-158](bgpd/bgp_main.c#L118) | 信号表、能力、privs、`bgpd_di` 前置声明 |
| [bgpd/bgp_main.c:450-461](bgpd/bgp_main.c#L450) | `FRR_DAEMON_INFO` |
| [bgpd/bgp_main.c:471-639](bgpd/bgp_main.c#L471) | `main()` 全流程 |
| [bgpd/bgp_main.c:214-301](bgpd/bgp_main.c#L214) | `bgp_exit()` |
| [zebra/main.c:305](zebra/main.c#L305) | zebra 的 `FRR_DAEMON_INFO` |
| [mgmtd/mgmt_main.c:191](mgmtd/mgmt_main.c#L191) | mgmtd 的 `FRR_DAEMON_INFO` |

### 14.2 与官方 `doc/developer/*.rst` 的对应

| 本文主题 | 官方文档 |
|---|---|
| §1 守护进程契约 | `doc/developer/daemons.rst` |
| §3 命令行 / `FRR_NO_*` | `doc/developer/daemons.rst`（Daemon Options） |
| §4 生命周期钩子 | `doc/developer/hooks.rst` |
| §5 初始化顺序 | `doc/developer/daemons.rst`（Startup / Shutdown） |
| §7 权限与能力 | `doc/developer/privileges.rst` |
| §8 模块系统 | `doc/developer/modules.rst` |
| §9 MTYPE | `doc/developer/memtypes.rst` |
| §9 ferr 错误码 | `doc/developer/ferr.rst` |
| §10 信号 | `doc/developer/signals.rst`（或 `daemons.rst` 内） |
| §11 守护进程实现 | `doc/developer/daemons.rst`（Adding a new daemon） |

### 14.3 与同系列文档的交叉引用

| 主题 | 本文位置 | 详见 |
|---|---|---|
| 事件循环 `event_fetch`/`event_call` | §2.6 | `FRR_IPC_STUDY.md` |
| `frr_pthread_init` / `frr_pthread_finish` | §5 | `FRR_PTHREAD_STUDY.md` |
| `rcu_shutdown()` 为何最后 | §5、§12 | `FRR_RCU_STUDY.md` |
| `frr_is_after_fork` 的断言 | §6.4 | `FRR_PTHREAD_STUDY.md` |
| zclient / zserv socket | §2.4 | `FRR_IPC_STUDY.md` |
| `vty_read_config` 与 candidate config | §2.5 | `doc/developer/` 配置管理相关 |

---

*本文所有行号均在 frr-10.6.1（分支 `study/bgpd-annotated`）工作树上实测确认。
若上游代码变动，请以 `grep -n` 的实际结果为准。*

# FRR 日志子系统深入剖析 —— `log` 与 `zlog` 两代实现的合流

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 涉及文件共 18 个、9713 行，全部在阅读范围内。
>
> 写作目的：回答两个问题 ——
> 1. **FRR 为什么同时存在 `log` 与 `zlog` 两套日志代码？**
> 2. 一次 `zlog_err(...)` 调用，最终是怎么落到文件 / syslog / journald / vtysh 终端的？
>
> 本文不修改任何源码。若你在阅读中发现某处注释与代码不符，文末「陷阱清单」与「勘误」两节会专门列出。

---

## §0 全局总览

### 0.1 先给结论

> **`log` 与 `zlog` 不是「新旧两套日志系统」，而是同一套日志系统在三个不同维度上留下的层叠结构。**

把这句话拆开，是理解本办法最关键的一点。三个维度分别是：

| 维度 | 表面现象 | 实质 |
|---|---|---|
| **① 命名维度** | 文件名一半叫 `log_*`、一半叫 `zlog_*` | `zlog_` 这个**符号前缀被两代代码共用**，所以「文件名的 log/zlog 之分」**不等于**「代码代际之分」 |
| **② API 维度** | `log.h` 里大量声明看起来像老接口 | `lib/log.h:39-40` 直接 `#include` 了 `zlog.h` 与 `zlog_targets.h` —— 它已退化为**新核心的兼容伞头**，老名字是薄封装 |
| **③ 配置维度** | `log_vty.c` 和 `log_cli.c`/`log_nb.c` 都能配置日志 | **这才是真正并存的两套配置栈**：老的「VTY 直接改全局变量」与新的「YANG 北向回调」，二者靠 `frr_logging_merge_cli_to_nb_info()` 缝合 |

第 ③ 点才是「为什么有两套」的**真正答案**，也是本文件中最值得花时间理解的部分。详见 §1 与 §11。

### 0.2 「两套」的确切边界：符号前缀 ≠ 代码代际

证据是最硬的。下面这 16 个 **以 `zlog_` 开头、但定义在 `log_*.c` 文件里** 的公开符号，是命名混乱的直接证据：

| 符号 | 定义位置 | 声明位置 | 归属代际 |
|---|---|---|---|
| `zlog_signal` | [log.c:160](lib/log.c#L160) | [log.h:139](lib/log.h#L139) | 老（崩溃诊断） |
| `zlog_backtrace_sigsafe` | [log.c:208](lib/log.c#L208) | [log.h:158](lib/log.h#L158) | 老（崩溃诊断） |
| `zlog_backtrace` | [log.c:278](lib/log.c#L278) | [log.h:147](lib/log.h#L147) | 老（崩溃诊断） |
| `zlog_thread_info` | [log.c:345](lib/log.c#L345) | [log.h:84](lib/log.h#L84) | 老（崩溃诊断） |
| `zlog_hexdump` | [log.c:736](lib/log.c#L736) | [log.h:181](lib/log.h#L181) | 老（文本工具） |
| `zlog_sanitize` | [log.c:805](lib/log.c#L805) | [log.h:182](lib/log.h#L182) | 老（文本工具） |
| `zlog_filter_clear` | [log_filter.c:80](lib/log_filter.c#L80) | [log.h:116](lib/log.h#L116) | 老（文本过滤） |
| `zlog_filter_add` | [log_filter.c:90](lib/log_filter.c#L90) | [log.h:117](lib/log.h#L117) | 老（文本过滤） |
| `zlog_filter_del` | [log_filter.c:117](lib/log_filter.c#L117) | [log.h:118](lib/log.h#L118) | 老（文本过滤） |
| `zlog_filter_dump` | [log_filter.c:140](lib/log_filter.c#L140) | [log.h:119](lib/log.h#L119) | 老（文本过滤） |
| `zlog_filterfile_init` | [log_filter.c:209](lib/log_filter.c#L209) | [log.h:107](lib/log.h#L107) | 跨代（老名字 + 新类型） |
| `zlog_filterfile_fini` | [log_filter.c:217](lib/log_filter.c#L217) | [log.h:108](lib/log.h#L108) | 跨代 |
| `zlog_priority_str` | [log_vty.c:164](lib/log_vty.c#L164) | [zlog.h:466](lib/zlog.h#L466) | 跨代 |
| `zlog_rotate` | [log_vty.c:234](lib/log_vty.c#L234) | [log.h:67](lib/log.h#L67) | 跨代 |
| `zlog_progname` | [log_vty.c:108](lib/log_vty.c#L108) | [zlog.h:69](lib/zlog.h#L69) | 跨代（数据在 old，声明在 new） |
| `zlog_priority[]` | [log_vty.c:150](lib/log_vty.c#L150) | 无 extern 声明 | 老（文件内自用） |

**读法**：老一代 FRR（zebra 时代）的日志子系统自己就用了 `zlog_` 前缀。2015–19 年 David Lamparter 重写核心时，新核心顺理成章地继续叫 `zlog`（见 [zlog.h:4](lib/zlog.h#L4) 的版权行 `Copyright (c) 2015-19 David Lamparter, for NetDEF, Inc.`）。于是 `zlog_` 前缀变成两代共用的名字空间 —— 一边是新的「消息对象 + 目标链」引擎，一边是老配置/诊断散件。**这就是文件名分不清代际的根本原因。**

### 0.3 文件职责分层表（按代际标注）

| 文件 | 行数 | 代际 | 一句话职责 |
|---|---|---|---|
| [lib/zlog.h](lib/zlog.h) | 478 | **新核心 API** | 消息对象、目标结构、`zlog_*`/`flog_*` 宏、时间戳标志 |
| [lib/zlog.c](lib/zlog.c) | 1426 | **新核心实现** | 引擎：TLS 缓冲、目标链遍历、延迟格式化、崩溃路径、回注 |
| [lib/zlog_targets.h](lib/zlog_targets.h) | 110 | 新核心 | `struct zlog_cfg_file` 与 fd/syslog 目标配置 API |
| [lib/zlog_targets.c](lib/zlog_targets.c) | 784 | 新核心 | 内置目标：fd/文件、syslog、crashlog、startup-stderr、aux-stdout |
| [lib/zlog_5424.h](lib/zlog_5424.h) | 222 | 新（可插拔目标） | RFC 5424 / 3164 / local / journald 目标的配置结构 |
| [lib/zlog_5424.c](lib/zlog_5424.c) | 1321 | 新（可插拔目标） | 报文格式化、目的地打开（FD/FILE/FIFO/UNIX）、重连退避 |
| [lib/zlog_5424_cli.c](lib/zlog_5424_cli.c) | 1157 | 新（CLI） | `log extended-syslog NAME` 块的全部子命令与生命周期钩子 |
| [lib/zlog_live.h](lib/zlog_live.h) | 114 | 新（跨进程） | 实时日志流的**二进制线格式** `struct zlog_live_hdr` 与 API |
| [lib/zlog_live.c](lib/zlog_live.c) | 347 | 新（跨进程） | 实时流目标：`sendmmsg` 批量投递 + 三态生命周期 |
| [lib/zlog_recirculate.h](lib/zlog_recirculate.h) | 26 | 新（跨进程） | 日志回注 API（只有 1 个函数声明） |
| [lib/zlog_recirculate.c](lib/zlog_recirculate.c) | 109 | 新（跨进程） | 回注的 event-loop 半边：收包 → 补 `\n\0` → 交回核心 |
| [lib/log.h](lib/log.h) | 316 | **跨代伞头** | 对外兼容入口：`#include` zlog.h + zlog_targets.h，并保留大量老声明 |
| [lib/log.c](lib/log.c) | 831 | 老（工具） | 崩溃/回溯、时间戳、错误串兜底、名字查表、hexdump/sanitize |
| [lib/log_filter.c](lib/log_filter.c) | 220 | 老（过滤） | 文本子串过滤器 + 「过滤文件」目标 |
| [lib/log_vty.c](lib/log_vty.c) | 519 | **跨代（配置中枢）** | 全局日志状态、老 VTY 命令、两个钩子、所有目标配置对象 |
| [lib/log_vty.h](lib/log_vty.h) | 103 | 跨代 | 上述中枢的声明 |
| [lib/log_cli.c](lib/log_cli.c) | 705 | **新（北向 CLI）** | `log ...` 命令 → YANG change 入队；`cli_show` 回显 |
| [lib/log_nb.c](lib/log_nb.c) | 925 | **新（北向回调）** | `frr-logging` 模块的 create/destroy/modify/rpc 回调 |

构建成员关系（[lib/subdir.am](lib/subdir.am)）：老代 5 个 .c 在 [subdir.am:66-70](lib/subdir.am#L66)，新代 6 个 .c 在 [subdir.am:135-140](lib/subdir.am#L135)；三个带 CLI 的文件同时进 `clippy_scan`（[subdir.am:185](lib/subdir.am#L185)、[186](lib/subdir.am#L186)、[196](lib/subdir.am#L196)）。**全部 18 个文件都在编译，没有任何一个被 `#if 0` 关掉。**

### 0.4 全景图：一次 `zlog_err()` 的完整旅程

```
        调用点                          静态元数据（编译期生成）              运行时（每条消息）
 ┌────────────────────┐              ┌──────────────────────┐          ┌────────────────────────┐
 │ zlog_err("...%d",x)│──展开──▶     │ static xrefdata_logmsg│          │ struct zlog_msg         │
 │ flog_err(EC, ...)  │  _zlog_ecref │ static xref_logmsg   │          │  .ts .prio .args .xref  │
 └────────────────────┘   (zlog.h:161)│  XREF_LINK 注册进段  │          │  .text/.textlen(懒渲染) │
        │                             └──────────────────────┘          └────────────────────────┘
        │  vzlogx(xref, prio, fmt, ap)   zlog.h:116 / zlog.c:855
        ▼
 ┌──────────────────────────────────────────────────────────────────────────┐
 │ ① LTTng tracepoint：trace_zlog_message(...)   zlog.c:855 内按 prio 分支   │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ ② 有 TLS 缓冲？                                                            │
 │    ├─ 否 → vzlog_notls()  zlog.c:549：栈上建 msg，栈缓冲 512B，立刻遍历   │
 │    └─ 是 → vzlog_tls()    zlog.c:589：把 msg 塞进本线程 mmap 缓冲，        │
 │            必要时才 flush                                                 │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ ③ 可选：zlog_backtrace_msg()  zlog.c:755（消息带 fl_print_bt 标志时）      │
 └──────────────────────────────────────────────────────────────────────────┘
        │  flush：rcu_read_lock() + frr_each_safe(zlog_targets, ...)  zlog.c:520
        ▼
 ┌───────────────────────── 目标链（RCU 无锁）─────────────────────────┐
 │ zlt_fd(文件) │ zlt_syslog │ zlog_crashlog │ zlt_5424 │ zlt_live │ ... │
 │ 每个目标：prio > zt->prio_min 则跳过，否则 logfn(zt, msgs[], n)      │
 └──────────────────────────────────────────────────────────────────────┘
        │
        ├─ zlog_fd()          zlog_targets.c:106   writev 聚合「时间戳+级别+前缀+正文」
        ├─ zlog_syslog()      zlog_targets.c:601   syslog(prio|facility, "%.*s")
        ├─ zlog_5424()        zlog_5424.c:511      RFC5424/3164/local/journald 报文
        ├─ zlog_live()        zlog_live.c:87       sendmmsg 二进制头 + 正文 → vtysh
        └─ zlog_filterfile_fd() log_filter.c:185   先按子串过滤再转 zlog_fd
```

崩溃路径（异步信号安全）走另一条窄通道：`zlog_sigsafe(text, len)` [zlog.c:910](lib/zlog.c#L910) 按 `\n` 切行，只投给 `prio_min >= LOG_CRIT` 且 `logfn_sigsafe != NULL` 的目标。

### 0.5 两条调用链对照：老名字 vs 新名字

| | 老风格（仍可用） | 新风格 |
|---|---|---|
| 无错误码 | `zlog_err / zlog_warn / zlog_info / zlog_notice / zlog_debug` | 同左（其实这是同一批宏，见下） |
| 带错误码 | `flog_err(EC_xxx, fmt, ...)` | 同左 |
| 通用入口 | `zlog(prio, fmt, ...)` [zlog.h:131](lib/zlog.h#L131) | `zlog_ref(xref, fmt, ...)` [zlog.h:144](lib/zlog.h#L144) |
| 变参转发 | `vzlog(prio, fmt, ap)` [zlog.h:118](lib/zlog.h#L118) | `vzlogx(xref, prio, fmt, ap)` [zlog.h:116](lib/zlog.h#L116) |
| 不带 xref | `ezlog(prio, fmt, ...)` [zlog.c:840](lib/zlog.c#L840) | —— |
| 崩溃路径 | `zlog_sigsafe(text, len)` [zlog.h:213](lib/zlog.h#L213) | 同 |

**关键观察**：这张表里 `zlog_err`/`flog_err` 其实**已经属于新核心**（定义在 [zlog.h:189-207](lib/zlog.h#L189)），不是老系统残留。真正「老」的是 `log.h` 里那批**功能性散件**（§12 详列）。所以「`log` vs `zlog`」在 API 维度上并不构成两套实现 —— 这正是 §0.1 表格第 ② 行的意思。

### 0.6 zlog 全局变量清单（全部实测）

| 变量 | 定义 | 类型/初值 | 语义 |
|---|---|---|---|
| `zlog_prefix[128]` | [zlog.c:108](lib/zlog.c#L108) | `char[]` | 每条日志前缀，`"bgpd[1234]: "` 或 `"bgpd: "` |
| `zlog_prefixsz` | [zlog.c:109](lib/zlog.c#L109) | `size_t` | 前缀长度（含尾随 `": "`） |
| `zlog_tmpdirfd` | [zlog.c:110](lib/zlog.c#L110) | `int = -1` | 本进程临时目录 fd，crashlog 与 TLS 缓冲用 |
| `zlog_instance` | [zlog.c:111](lib/zlog.c#L111) | `int = -1` | 实例号（`bgpd -i 2`） |
| `zlog_ec` / `zlog_xid` | [zlog.c:117](lib/zlog.c#L117) | `atomic_bool = true` | 前缀是否带 `[EC n]` / `[xid]` |
| `zlog_uid` / `zlog_gid` | [zlog.c:128](lib/zlog.c#L128)、[129](lib/zlog.c#L129) | `uid_t/gid_t = -1` | 临时目录属主 |
| `zlog_targets` | [zlog.c:136](lib/zlog.c#L136) | `struct zlog_targets_head` | **目标链头**（atomlist，RCU 保护） |
| `zlog_default_immediate` | [zlog.c:145](lib/zlog.c#L145) | `bool` | 默认 immediate 模式 |
| `zlog_tmpdir[MAXPATHLEN]` | [zlog.c:1318](lib/zlog.c#L1318) | `char[]` | `/var/tmp/frr/<progname>[-<inst>].<pid>` |
| `zlog_progname` | [log_vty.c:108](lib/log_vty.c#L108) | `const char *` | 进程名（syslog ident）—— **定义在老文件，声明在新头** |
| `logmsgs_with_persist_bt` | [log_vty.c:52](lib/log_vty.c#L52) | `uint` | 带持久 backtrace 的消息计数 |

### 0.7 关键 API 速查

| 目的 | 接口 | 位置 |
|---|---|---|
| 写一条日志（带错误码，推荐） | `flog_err / flog_warn / flog_err_sys` | [zlog.h:201-207](lib/zlog.h#L201) |
| 写一条日志（无错误码） | `zlog_err / zlog_warn / zlog_info / zlog_notice / zlog_debug` | [zlog.h:189-193](lib/zlog.h#L189) |
| 任意优先级 | `zlog(prio, fmt, ...)` | [zlog.h:131](lib/zlog.h#L131) |
| 崩溃路径（AS-safe） | `zlog_sigsafe(text, len)` | [zlog.h:213](lib/zlog.h#L213) |
| 取消息正文（懒渲染） | `zlog_msg_text(msg, &len)` | [zlog.h:267](lib/zlog.h#L267) |
| 取消息优先级 | `zlog_msg_prio(msg)` | [zlog.h:252](lib/zlog.h#L252) |
| 取调用点 xref（含 EC） | `zlog_msg_xref(msg)` | [zlog.h:253](lib/zlog.h#L253) |
| 渲染时间戳 | `zlog_msg_ts / zlog_msg_ts_3164 / zlog_msg_tsraw` | [zlog.h:304](lib/zlog.h#L304)、[313](lib/zlog.h#L313)、[306](lib/zlog.h#L306) |
| 注册/替换目标 | `zlog_target_clone / _replace / _free` | [zlog.h:379](lib/zlog.h#L379)、[396](lib/zlog.h#L396)、[403](lib/zlog.h#L403) |
| 初始化 / 收尾 | `zlog_init` / `zlog_fini` / `zlog_aux_init` | [zlog.h:410](lib/zlog.h#L410)、[419](lib/zlog.h#L419)、[438](lib/zlog.h#L438) |
| 声明启动期结束 | `zlog_startup_end()` | [zlog.h:445](lib/zlog.h#L445) |
| 文件轮转 | `zlog_rotate()` | [log.h:67](lib/log.h#L67) / [log_vty.c:234](lib/log_vty.c#L234) |

---

## §1 为什么会形成「两套」——设计动因逐条拆解

### 1.1 动因一：老系统的技术债（为什么必须重写）

老 FRR 日志系统（zebra 血统，[log.h:4](lib/log.h#L4) `Copyright (C) 1997, 1998, 1999 Kunihiro Ishiguro`）的核心问题是**「格式化」与「投递」被绑死**：

1. **一调用即格式化**。`zlog()` 一进去就把 `va_list` 渲染成完整字符串，之后无论有多少个目标、无论目标是否需要，都无法复用。目标多了就是重复渲染。
2. **目标不可插拔**。所有出口（文件、syslog、stdout、终端）都是硬编码的 `if` 分支 + 一组全局变量（`log_config_stdout_lvl`、`log_config_syslog_lvl`……见 [log_vty.c:54-57](lib/log_vty.c#L54)）。加一个出口要动核心。
3. **配置即全局状态**。VTY 命令直接改全局变量，配置与运行时状态不分家，无法回滚、无法事务化、无法被 NETCONF/RESTCONF 复用。
4. **崩溃时基本写不出东西**。崩溃处理器不能 `malloc`、不能 `printf`，而老路径处处依赖动态内存与 `stdio` 缓冲。
5. **无结构化元数据**。日志行是纯文本，没有错误码、没有唯一 ID、没有可供程序消费的字段。

### 1.2 动因二：新核心的设计目标（对应上面每一条）

| 老问题 | 新核心的解法 | 落点 |
|---|---|---|
| 一调用即格式化 | **延迟格式化**：只创建 `struct zlog_msg`，`text` 字段留到 `zlog_msg_text()` 被调用时才渲染 | [zlog.h:263-267](lib/zlog.h#L263)、[zlog.c:995](lib/zlog.c#L995) |
| 目标不可插拔 | **目标链**：`DECLARE_ATOMLIST(zlog_targets, struct zlog_target, head)`，任何结构内嵌 `struct zlog_target` 即可成为目标 | [zlog.c:135](lib/zlog.c#L135)、[zlog.h:355](lib/zlog.h#L355) |
| 配置即全局状态 | 配置留在目标自己的 `zlog_cfg_*` 里，目标只持有「执行日志所需的（副本）数据」 | [zlog_targets.h:45](lib/zlog_targets.h#L45)、官方文档 [logging.rst:797-805](doc/developer/logging.rst#L797) |
| 崩溃写不出 | **TLS mmap 缓冲** + `logfn_sigsafe` 窄接口 | [zlog.c:238](lib/zlog.c#L238)、[zlog.h:368](lib/zlog.h#L368) |
| 无结构化元数据 | **xref**（静态调用点表）+ **uid** + **EC 错误码** + `argpos` 参数位置表 | [zlog.h:77](lib/zlog.h#L77)、[zlog.h:99](lib/zlog.h#L99)、[zlog.h:273](lib/zlog.h#L273) |
| 目标替换时的并发 | **RCU**：`zlog_target_replace` 先加后删，旧目标须存活到 RCU 周期结束 | [zlog.c:1283](lib/zlog.c#L1283)、[zlog.h:388-389](lib/zlog.h#L388) |

### 1.3 动因三：兼容成本 —— 349 个 `#include "log.h"` 的引力

这是「为什么老文件没被删掉」的现实原因，量级很清楚：

```
$ grep -rl '#include "log.h"' --include=*.c --include=*.h . | wc -l
347
$ grep -rn '#include "zlog' --include=*.c --include=*.h . | grep -v '^./lib/zlog'
lib/frr_pthread.c:34
lib/vty.h:41          （zlog_live.h）
ldpd/lde.c:31         （zlog_live.h）
ldpd/ldpe.c:26        （zlog_live.h）
ldpd/ldpd.c:38        （zlog_recirculate.h）
tests/lib/fuzz_zlog.c:9
watchfrr/watchfrr.c:22（zlog_targets.h）
```

**347 : 7**。如果重写时强制全树改 include 与符号名，这个改动量是不可接受的。于是采取的策略是：

> **保留 `log.h` 作为对外伞头，让它把新头拉进来，实现「老代码一行不改，用的却是新引擎」。**

具体机制就是三行代码：

```c
/* lib/log.h:38-40 */
#include "lib/hook.h"
#include "lib/zlog.h"          ← 新核心
#include "lib/zlog_targets.h"  ← 新目标配置
```

而 `lib/libfrr.h:35` 又 `#include` 了 `log.h`，所以任何引 `libfrr.h` 的守护进程都自动拿到新核心。这是**用头文件做兼容层**的经典手法：ABI 层面一切照旧，实现层面全部换新。

### 1.4 动因四（真正意义上的「两套」）：配置栈的双轨

到这里才是「两套」的实质。日志配置有**两条并行入口**，至今同时生效：

| | 老轨（VTY 直改状态） | 新轨（YANG 北向） |
|---|---|---|
| 代码 | [lib/log_vty.c](lib/log_vty.c) | [lib/log_cli.c](lib/log_cli.c) + [lib/log_nb.c](lib/log_nb.c) |
| 手法 | `DEFPY`/`DEFUN` 直接改全局变量与 `zt_*` 对象 | `DEFPY_YANG` 入队 XPath change → `nb_cli_apply_changes()` |
| 命令 | `show logging`、`send log`、`show logging filter-text`、`debug unique-id ... backtrace` | `log stdout/syslog/file/filtered-file/daemon-file/facility/...` |
| 配置落点 | 全局 `int log_config_stdout_lvl` 等 | `/frr-logging:logging/...` YANG 树 |
| 配置回显 | 手写 `vty_out` | `cli_show` 回调 |
| 能否被 NETCONF/mgmtd 复用 | **否** | **是** |

**为什么需要新轨？** 因为 FRR 在向「所有配置统一走 YANG 北向」演进：同一份配置要能被 CLI、NETCONF、RESTCONF、mgmtd、配置数据库共同读写。老的「VTY 直接改全局变量」无法满足事务语义与多前端需求。

**为什么老轨不能删？** 因为：

1. 有些能力**只存在于老轨**：`show logging`（诊断视图）、`send log`（现场发日志）、`show logging filter-text`、`debug unique-id ... backtrace`（[log_vty.c:389](lib/log_vty.c#L389)，注意它与 [log_cli.c:116](lib/log_cli.c#L116) 是**两个不同节点上的同名命令**）。这些是「操作/诊断」而非「配置」，本就不适合塞进 YANG。
2. 老轨还承担**命令行早期配置**：进程还没建 YANG 树时就得架起日志出口（`--log` 参数），见 [log_nb.c:123](lib/log_nb.c#L123) 的 `command_setup_early_logging`。
3. 老轨保存着**全部目标的运行时对象**（`zt_file`、`zt_filterfile`、`zt_file_cmdline`、`zt_stdout_file`、`zt_stdout_journald`，见 [log_vty.c:67-103](lib/log_vty.c#L67)），新轨的 NB 回调只是去操作这些对象。

**两条轨怎么缝合？** 靠一个函数：`frr_logging_merge_cli_to_nb_info()` [log_cli.c:691](lib/log_cli.c#L691)。

它的逻辑是：让 `frr_logging_nb_info`（NB 表，提供 create/destroy/modify 回调）与 `frr_logging_cli_info`（CLI 表，只提供 `cli_show`，且 [log_cli.c:665](lib/log_cli.c#L665) 明确设了 `.ignore_cfg_cbs = true`）**按 XPath 字符串配对**，把后者每个节点的 `cli_show` 回调**复制进**前者的同 XPath 节点：

```c
/* lib/log_cli.c:695-698 */
for (i = 0; frr_logging_cli_info.nodes[i].xpath != NULL; i++) {
        for (j = 0; frr_logging_nb_info.nodes[j].xpath != NULL; j++)
                if (!strcmp(frr_logging_cli_info.nodes[i].xpath,
                            frr_logging_nb_info.nodes[j].xpath)) {
                        frr_logging_nb_info.nodes[j].cbs.cli_show =
                                frr_logging_cli_info.nodes[i].cbs.cli_show;
```

于是：
- **配置写入** 走 NB 表（`log_nb.c` 的回调）；
- **配置回显** 走 CLI 表（`log_cli.c` 的 `cli_show`）；
- 一个模块信息表同时具备两种能力，`show running-config` 得以正常工作。

> **设计意图总结**：这不是「没清理干净的老代码」，而是**刻意的分工** —— NB 负责「事务化的配置」，VTY 负责「诊断与运行时操作」，两者在数据层面共用同一批 `zlog_cfg_*` 对象。理解这一点，§11 的整章内容才能读通。

### 1.5 分层的官方表述

官方文档对这条边界说得比代码注释更直白，[logging.rst:792-805](doc/developer/logging.rst#L792)：

> The actual logging subsystem (in `lib/zlog.c`) is heavily separated from the actual log writers. It uses an atomic linked-list (`zlog_targets`) with RCU to maintain the log targets to be called. This list is intended to function as "backend" only, **it is not used for configuration**.
>
> Logging targets provide their configuration layer on top of this and maintain their own capability to enumerate and store their configuration. ... There is another layer boundary here between these and the VTY configuration that they use.

也就是官方承认存在**三段分层**：

```
   ┌──────────────────────────────────────────────────┐
   │ 配置层：log_cli.c(北向 CLI) / log_vty.c(VTY+命令)  │
   ├──────────────────────────────────────────────────┤
   │ 目标配置层：zlog_cfg_file / zlog_cfg_5424 / ...    │   ← 每个目标自带配置
   ├──────────────────────────────────────────────────┤
   │ 目标执行层：zlog_target + logfn  zlog_targets.c /  │   ← 只做日志，不存配置
   │            zlog_5424.c / zlog_live.c              │
   ├──────────────────────────────────────────────────┤
   │ 核心引擎：zlog.c（消息对象、TLS 缓冲、RCU 遍历）    │   ← 不做配置，不做格式化输出
   └──────────────────────────────────────────────────┘
```

---

## §2 zlog 核心：调用入口与 xref

### 2.1 宏家族：`_zlog_ecref` 是总入口

`zlog.h` 里所有便捷宏都汇总到一个宏，[zlog.h:161](lib/zlog.h#L161)：

```c
#define _zlog_ecref(ec_, prio, msg, ...)                                       \
        do {                                                                   \
                static struct xrefdata_logmsg _xrefdata = {                    \
                        .xrefdata = { .xref = &_xref, .uid = {0} },            \
                        .fmtstring = msg, .priority = prio, .ec = ec_,         \
                };                                                             \
                static const struct xref_logmsg _xref __attribute__(           \
                        (section("xref_array"), used, aligned(8))) = { ... };  \
                XREF_LINK(_xref.xref);                                         \
                zlog_ref(&_xref, __VA_ARGS__);                                 \
        } while (0)
```

它做三件事（[zlog.h:155-188](lib/zlog.h#L155)）：

1. 在**函数作用域**内声明两个 `static` 对象（`_xrefdata` 与 `_xref`）—— 函数级 `static` 保证生命周期覆盖整个进程，且名字可复用不冲突；
2. 把 `_xref` 放进 `xref_array` 段并调 `XREF_LINK` 注册 —— 这是 FRR 的 **cross-reference 框架**，让「这条日志消息定义在哪里」在运行时可通过 uid 反查；
3. 调用 `zlog_ref(&_xref, ...)` [zlog.h:144](lib/zlog.h#L144) 真正落日志。

因此：

| 宏 | EC | 优先级 | 位置 |
|---|---|---|---|
| `zlog_err` | 0 | `LOG_ERR` | [zlog.h:189](lib/zlog.h#L189) |
| `zlog_warn` | 0 | `LOG_WARNING` | [zlog.h:190](lib/zlog.h#L190) |
| `zlog_info` | 0 | `LOG_INFO` | [zlog.h:191](lib/zlog.h#L191) |
| `zlog_notice` | 0 | `LOG_NOTICE` | [zlog.h:192](lib/zlog.h#L192) |
| `zlog_debug` | 0 | `LOG_DEBUG` | [zlog.h:193](lib/zlog.h#L193) |
| `flog_err(id, ...)` | `id` | `LOG_ERR` | [zlog.h:201](lib/zlog.h#L201) |
| `flog_warn(id, ...)` | `id` | `LOG_WARNING` | [zlog.h:204](lib/zlog.h#L204) |
| `flog_err_sys(id, ...)` | `id` | `LOG_ERR` | [zlog.h:206](lib/zlog.h#L206) |

> **注意**：`zlog_err` 那一排**不带 EC**（`ec = 0`），所以只产生 uid，不产生 `[EC n]` 前缀。要错误码必须用 `flog_*`。

### 2.2 xref：静态元数据与运行时数据的分离

`struct xref_logmsg` [zlog.h:77](lib/zlog.h#L77) 是**编译期常量**，放在 `xref_array` 段：

| 字段 | 语义 |
|---|---|
| `xref` | 通用交叉引用模块（文件、行、函数名） |
| `fmtstring` | 格式串字面量 |
| `priority` | 该消息固定的优先级 |
| `ec` | 错误码（`flog_*` 才有） |
| `args` | 参数名列表，供 `argpos` 使用 |

`struct xrefdata_logmsg` [zlog.h:99](lib/zlog.h#L99) 是**运行时数据**，挂在 xref 上：

| 字段 | 语义 |
|---|---|
| `xrefdata` | 内嵌通用运行时数据（含 `uid`：`"XXXXX-XXXXX"`，12 字节） |
| `fl_print_bt` | 是否给这条消息加 backtrace |

两个 `LOGMSG_FLAG_*` 位（[zlog.h:92-93](lib/zlog.h#L92)）：

- `LOGMSG_FLAG_EPHEMERAL (1<<0)` —— 在 **enable 模式**（运行期）打开的 backtrace
- `LOGMSG_FLAG_PERSISTENT (1<<1)` —— 在 **config 模式**（持久配置）打开的 backtrace

它们由 CLI 命令 `[no] debug unique-id UID backtrace` 切换，见 [log_vty.c:389](lib/log_vty.c#L389)（ENABLE 节点，直接改标志）与 [log_cli.c:116](lib/log_cli.c#L116)（CONFIG 节点，走北向）。两处命令同名但节点不同，实现路径也完全不同 —— 这正是 §1.4 所说双轨并存的典型样例。

### 2.3 入口分流：`vzlogx` 与 TLS

[vzlogx](lib/zlog.c#L855) 做了两件事，然后分流：

```
vzlogx(xref, prio, fmt, ap)
  ├─ LTTng tracepoint：按 prio 落到 trace_zlog_{emerg,...,debug} 之一
  └─ zlog_get_tls() 有值 ? vzlog_tls() : vzlog_notls()
```

`vzlog_notls` [zlog.c:549](lib/zlog.c#L549)：栈上建 `struct zlog_msg`、栈上 512 字节文本缓冲，**立刻**遍历目标链投递。

`vzlog_tls` [zlog.c:589](lib/zlog.c#L589)：把消息登记进本线程的 mmap 缓冲，**延后**投递。这是有代价的 —— 见 §5。

两者都有同一条过滤逻辑，例如 [zlog.c:567](lib/zlog.c#L567) / [zlog.c:601](lib/zlog.c#L601)：

```c
if (prio > zt->prio_min)
        continue;
```

注意是 **`>`**（数值大 = 更不重要）。目标链遍历本身不预过滤，核心层做的是「有没有任何一个目标感兴趣」的快速探针。

---

## §3 消息对象 `struct zlog_msg`

### 3.1 结构全貌

定义在 [zlog.c:168-210](lib/zlog.c#L168)，是**不透明**类型（头文件只给 `struct zlog_msg;` 前向声明，[zlog.h:246](lib/zlog.h#L246)）。字段分五组：

| 组 | 字段 | 说明 |
|---|---|---|
| **输入** | `ts`、`prio`、`fmt`、`args`、`xref` | 调用时给定；`prio` 存的是 `prio & LOG_PRIMASK`（[zlog.c:622](lib/zlog.c#L622)） |
| **缓冲** | `stackbuf`、`stackbufsz`、`text`、`textlen`、`hdrlen` | 首渲染优先用调用者栈缓冲，不够才 `XMALLOC` |
| **转发** | `pid`、`tid` | **仅用于被转发的消息**（[zlog.c:182-183](lib/zlog.c#L182) 的原注释：`for relayed log messages ONLY`） |
| **时间戳缓存** | `ts_str[32]`、`ts_dot`、`ts_zonetail[8]`、`ts_flags`、`ts_3164_flags`、`ts_3164_str[16]` | 避免同一秒内重复 `strftime` |
| **参数位置** | `struct fmt_outpos argpos[24]`、`n_argpos` | 最多记 24 个格式化参数的位置 |

### 3.2 延迟格式化：`zlog_msg_text`

[zlog.c:995](lib/zlog.c#L995) 是延迟格式化的核心。它做三件事：

1. **先拼前缀**（如果开启了 EC 或 xid），把 `[xid][EC n] ` 写进文本头部并记录 `hdrlen` —— 也就是「正文从哪个字节开始」；
2. 用 `vbprintfrr` 渲染用户格式串；
3. 若首渲染用的栈缓冲不够，`XMALLOC` 更大缓冲并**重渲染**。

**契约**（[zlog.h:263-267](lib/zlog.h#L263)）：

> 返回的文本**不以 `'\0'` 结尾**；换行符在 `text[textlen]` 处，**不计入 textlen**。

这条契约在三个地方被显式依赖：

- `zlog_fd` 写出时用 `iov_len = textlen + 1` 把那个换行一起写出（[zlog_targets.c:163](lib/zlog_targets.c#L163)）；
- `zlog_live` 组包时用 `iov_len = textlen`，不带头部前缀与换行（[zlog_live.c:132-134](lib/zlog_live.c#L132)）；
- 回注校验时**要求** `text[textlen] == '\n' && text[textlen+1] == '\0'`（[zlog.c:701-703](lib/zlog.c#L701)）。

### 3.3 参数位置表 `zlog_msg_args`

[zlog.c:1072](lib/zlog.c#L1072) 吐出 `hdrlen`、`n_argpos` 与 `argpos[]`。用途是让**消费者自己重新格式化**：

- **vtysh 侧**：按 `argpos` 把日志正文里的参数高亮（不同参数用不同颜色），见 `vtysh_log_print` [vtysh/vtysh.c:4392](vtysh/vtysh.c#L4392)；
- **RFC 5424 侧**：把参数抽成结构化数据项 `[args@50145 argN="..."]`，见 [zlog_5424.c:289](lib/zlog_5424.c#L289)。

这就是延迟格式化的**真正收益**：不止省了一次渲染，还能把「参数边界」这一信息交给下游 —— 这是老系统完全做不到的。

### 3.4 时间戳三函数

| 函数 | 输出样式 | 谁在用 |
|---|---|---|
| `zlog_msg_ts` [zlog.c:1101](lib/zlog.c#L1101) | ISO8601（`2026-10-14T10:05:00.123456Z`）或 LEGACY（`2026/10/14 10:05:00`），加 `.NNNNNNNNN` 亚秒 | `zlog_fd`（LEGACY）、`zlog_5424`（ISO8601） |
| `zlog_msg_ts_3164` [zlog.c:1193](lib/zlog.c#L1193) | `"Oct 14 10:05:00"`（月份名硬编码表在 [zlog.c:1199](lib/zlog.c#L1199)） | RFC3164 syslog 目的地 |
| `zlog_msg_tsraw` [zlog.c:1225](lib/zlog.c#L1225) | 原样吐出 `struct timespec` | 需要原始时间戳的消费者 |

标志位（[zlog.h:286-296](lib/zlog.h#L286)）：

| 标志 | 值 | 语义 |
|---|---|---|
| `ZLOG_TS_PREC` | `0xfU` | 低 4 位掩码：亚秒位数 0–9 |
| `ZLOG_TS_ISO8601` | `1<<8` | ISO8601 样式 |
| `ZLOG_TS_LEGACY` | `1<<9` | 老式 `YYYY/MM/DD HH:MM:SS` |
| `ZLOG_TS_UTC` | `1<<10` | 用 UTC，否则本地时区 |

内部掩码 `ZLOG_TS_FORMAT (ZLOG_TS_ISO8601 | ZLOG_TS_LEGACY)` 在 [zlog.c:1090](lib/zlog.c#L1090)，`ZLOG_TS_FLAGS ~ZLOG_TS_PREC` 在 [zlog.c:1091](lib/zlog.c#L1091)。

---

## §4 目标链与 RCU 契约

### 4.1 `struct zlog_target`：必须内嵌且必须是首字段

[zlog.h:355-372](lib/zlog.h#L355)：

| 字段 | 语义 |
|---|---|
| `head` | atomlist 链节点（`DECLARE_ATOMLIST(zlog_targets, struct zlog_target, head)` [zlog.c:135](lib/zlog.c#L135)） |
| `prio_min` | 本目标的最低优先级（数值大于它的消息被丢弃） |
| `logfn(zt, msgs[], nmsgs)` | 常规投递回调 |
| `logfn_sigsafe(zt, text, len)` | 崩溃路径回调，**不能分配内存**；无法支持时置 `NULL` |
| `rcu_head` | RCU 回收头 |

**「必须是首字段」** 的原因：`container_of(zt, struct zlt_xxx, zt)` 要从基类指针反查回具体目标结构。看看内置目标的实际写法（[zlog_targets.c:60-70](lib/zlog_targets.c#L60)、[zlog_targets.c:591-595](lib/zlog_targets.c#L591)、[zlog_live.c:64-73](lib/zlog_live.c#L64)、[zlog_5424.c:81-124](lib/zlog_5424.c#L81)），无一例外。

### 4.2 RCU 替换契约：先加后删，旧目标须存活到周期结束

[zlog_target_replace](lib/zlog.c#L1283) 的行为（官方文档 [logging.rst:858-878](doc/developer/logging.rst#L858) 说得最清楚）：

1. 返回被替换下来的旧目标，**调用方负责释放**；
2. 替换**不是原子的** —— 存在一个短暂窗口，同一条消息可能**同时**投给旧目标和新目标（可能重复输出！）；
3. **旧目标必须保持功能可用**，直到当前 RCU 周期结束 —— 也就是说，**不能立刻 close(fd)、不能立刻 free()**；
4. 配套的释放必须走 RCU：`zlog_target_free(mt, zt)` 实际就是 `rcu_free(...)` [zlog.h:403](lib/zlog.h#L403)。

正确的用法模式（以 syslog 目标为例，[zlog_targets.c:714-723](lib/zlog_targets.c#L714)）：

```c
newztc = zlog_target_clone(MTYPE_LOG_SYSL, &zlt_syslog->zt, sizeof(*newzt));
newzt  = container_of(newztc, struct zlt_syslog, zt);
newzt->syslog_facility = syslog_facility;

zlog_target_free(MTYPE_LOG_SYSL,
                 zlog_target_replace(&zlt_syslog->zt, &newzt->zt));
zlt_syslog = newzt;
```

即 **clone → 改副本 → replace → free 旧**。注意 `zlog_target_clone` [zlog.c:1263](lib/zlog.c#L1263) 只复制 `struct zlog_target` 里的通用字段，**不复制目标特有字段** —— 所以 clone 后必须自己补齐（这里是 `syslog_facility`）。

### 4.3 关闭 fd 的正确姿势：`rcu_close`

文件类目标有 fd 要关，但 fd 不能在替换时立刻关（旧目标可能正在 `writev`）。解法是 `struct rcu_head_close` + `rcu_close()`：

- [zlog_targets.c:69](lib/zlog_targets.c#L69)：`struct zlt_fd` 里带一个 `head_close`；
- [zlog_targets.c:249](lib/zlog_targets.c#L249)：释放目标时 `rcu_close(&zlt->head_close, zlt->fd)` —— 延后到 RCU 周期结束后才 `close(fd)`；
- 轮转路径同理，用一个两步延迟结构 `struct rcu_close_rotate`（[zlog_targets.c:381-384](lib/zlog_targets.c#L381)）同时延迟「关 fd」与「释放辅助结构」。

### 4.4 目标能力矩阵

| 目标 | 结构 | 常规 `logfn` | `logfn_sigsafe` | 多实例 | 有 fd 需 rcu_close |
|---|---|---|---|---|---|
| fd / 文件 | `struct zlt_fd` [zlog_targets.c:60](lib/zlog_targets.c#L60) | `zlog_fd` [:106](lib/zlog_targets.c#L106) | `zlog_fd_sigsafe` [:192](lib/zlog_targets.c#L192) | ✅（`zlog_cfg_file` 可多份） | ✅ |
| syslog(3) | `struct zlt_syslog` [:591](lib/zlog_targets.c#L591) | `zlog_syslog` [:601](lib/zlog_targets.c#L601) | `zlog_syslog_sigsafe` [:632](lib/zlog_targets.c#L632) | ❌（单例，全局变量存配置） | 自持 static fd |
| crashlog | `struct zlt_fd`（全局单例 `zlog_crashlog`）[:425](lib/zlog_targets.c#L425) | `zlog_crashlog_plain` [:460](lib/zlog_targets.c#L460) | `zlog_crashlog_sigsafe` [:433](lib/zlog_targets.c#L433) | ❌ | 自持 static fd |
| startup stderr | `zlog_startup_stderr` [:542](lib/zlog_targets.c#L542) | `zlog_fd` | `zlog_fd_sigsafe` | ❌ | 无（用 STDERR_FILENO） |
| aux stdout | `zlog_aux_stdout` [:495](lib/zlog_targets.c#L495) | `zlog_fd` | `zlog_fd_sigsafe` | ❌ | 无（用 STDOUT_FILENO） |
| 过滤文件 | `struct zlog_cfg_filterfile` | `zlog_filterfile_fd` [log_filter.c:185](lib/log_filter.c#L185) | 复用 `zlog_fd_sigsafe` | ✅ | ✅（内嵌 `zlog_cfg_file`） |
| RFC 5424 | `struct zlt_5424` [zlog_5424.c:81](lib/zlog_5424.c#L81) | `zlog_5424` [:511](lib/zlog_5424.c#L511) | `zlog_5424_sigsafe` [:721](lib/zlog_5424.c#L721) | ✅ | ✅ |
| 实时流 | `struct zlt_live` [zlog_live.c:64](lib/zlog_live.c#L64) | `zlog_live` [:87](lib/zlog_live.c#L87) | `zlog_live_sigsafe` [:206](lib/zlog_live.c#L206) | ✅（每会话一份） | ✅ |

**`logfn_sigsafe` 为 `NULL` 是允许的**（[zlog.h:368](lib/zlog.h#L368) 注释），只是崩溃时该目标收不到消息。

---

## §5 TLS 缓冲：崩溃时尽量不丢日志

### 5.1 设计动机：一套「两个都想要」的方案

需求矛盾：**平时**批量写日志性能好（攒一批再 writev），**崩溃时**又要求最近的日志已经在磁盘上。攒批意味着日志在内存里，崩溃就没了。

FRR 的解法（[zlog.c:212-227](lib/zlog.c#L212) 的注释）：**把线程日志缓冲建在 `MAP_SHARED` 的 mmap 上，文件位于 `/var/tmp`。这样崩溃时内核会把页缓存内容同步回文件，未写出的日志仍然留在磁盘上。** 消息的元数据（`struct zlog_msg` 数组）不在 mmap 区内，崩溃时仍会丢 —— 注释里明确承认了这一点。

### 5.2 结构

`struct zlog_tls` [zlog.c:248-256](lib/zlog.c#L248)：

| 字段 | 语义 |
|---|---|
| `mmbuf` | mmap 缓冲基址 |
| `bufpos` | 当前写入偏移 |
| `do_unlink` | 退出时是否删除临时文件 |
| `nmsgs` | 待投递消息数 |
| `msgs[64]` / `msgp[64]` | 消息对象数组与指针数组 |

常量：

| 常量 | 值 | 位置 |
|---|---|---|
| `TLS_LOG_BUF_SIZE` | `8192` | [zlog.c:238](lib/zlog.c#L238) |
| `TLS_LOG_MAXMSG` | `64` | [zlog.c:239](lib/zlog.c#L239) |
| `CAN_DO_TLS` | 需 `HAVE_OPENAT && HAVE_UNLINKAT` | [zlog.c:229-231](lib/zlog.c#L229) |

TLS 存储实现有两条路（[zlog.c:264-313](lib/zlog.c#L264)）：OpenBSD 走 `pthread_key`，其余平台走 ELF `thread_local` 并标 `tls_model("initial-exec")`。

### 5.3 初始化与收尾

| 函数 | 位置 | 做什么 |
|---|---|---|
| `zlog_tls_buffer_init` | [zlog.c:362](lib/zlog.c#L362) | `openat(zlog_tmpdirfd, "logbuf.<tid>")` → `posix_fallocate`（失败退 `ftruncate`）→ `mmap(MAP_SHARED)`；mmap 失败再退普通匿名映射 |
| `zlog_tls_buffer_flush` | [zlog.c:520](lib/zlog.c#L520) | RCU 读锁 + `frr_each_safe` 遍历目标链投递，然后复位 `bufpos`/`nmsgs` |
| `zlog_tls_buffer_fini` | [zlog.c:438](lib/zlog.c#L438) | 先 flush，再 `munmap`、释放结构 |
| `zlog_tls_free` | [zlog.c:504](lib/zlog.c#L504) | `munmap` + 释放 |
| 非 TLS 平台桩 | [zlog.c:458](lib/zlog.c#L458)、[462](lib/zlog.c#L462) | 空实现 |

`zlog_gettid()` [zlog.c:321](lib/zlog.c#L321) 在 Linux 上用 `syscall(__NR_gettid)`，结果缓存在 thread-local 里。

### 5.4 何时强制 flush —— 四个条件

`vzlog_tls` [zlog.c:589](lib/zlog.c#L589) 因为 `va_list` 无法长期保存，**在写入 TLS 缓冲前就必须先渲染文本**（这是延迟格式化在这里的折损点）。渲染之后，满足以下任一条件即刻 flush：

| 条件 | 代码位置 | 理由 |
|---|---|---|
| 待投递消息数达上限 64 | [zlog.c:651](lib/zlog.c#L651) | 缓冲容量 |
| 文本用了 `malloc`（栈缓冲不够） | [zlog.c:589](lib/zlog.c#L589) 段内判断 | 内存归属需及时归还 |
| mmap 缓冲剩余 < 256 字节 | [zlog.c:645](lib/zlog.c#L645) | 空间不足 |
| `prio < LOG_INFO`（即 WARNING 及以上） | [zlog.c:589](lib/zlog.c#L589) 段内判断 | **重要消息不冒险** |

最后一条尤其值得注意：**告警及以上级别一律立即 flush**。这就是「TLS 缓冲会延迟日志」这个主要副作用在实际中很少被观察到的原因 —— 重要的日志从不延迟。

### 5.5 崩溃恢复：那个额外的 `\0`

写完一条消息后，`vzlog_tls` 会在 `bufpos` 处再写一个 `'\0'`，用它标示「**尚未写出的日志到此为止**」。崩溃后解析 mmap 文件时，靠这个标记判断有效内容边界。

这也解释了 §3.2 的契约为什么是「换行在 `text[textlen]`，`\0` 在 `text[textlen+1]`」。

---

## §6 崩溃路径与异步信号安全

### 6.1 `zlog_sigsafe`：唯一允许在信号处理器里用的入口

[zlog.c:910](lib/zlog.c#L910)：

```c
void zlog_sigsafe(const char *text, size_t len);
```

它的约束和常规路径**完全不同**：

- 输入是**已经渲染好的纯文本**，不做任何格式化；
- 按 `\n` 手动切行（不能用 `printf` 家族）；
- 只投给 `LOG_CRIT <= zt->prio_min` 且 `logfn_sigsafe != NULL` 的目标 —— 也就是**只有崩溃日志目标与 fd 目标**；
- 完全不走 TLS 缓冲、不走 RCU 遍历之外的东西。

### 6.2 崩溃目标链的搭设：`zlog_targets.c` 的三个构造器

`zlog_targets.c` 用构造函数（`__attribute__((_CONSTRUCTOR(450)))`）在**库加载早期**就把 startup 目标挂好：

```c
/* lib/zlog_targets.c:549 */
__attribute__((_CONSTRUCTOR(450))) static void zlog_startup_init(void)
{
        zlog_startup_stderr.zt.prio_min = LOG_WARNING;
        zlog_startup_stderr.zt.logfn = zlog_fd;
        zlog_startup_stderr.zt.logfn_sigsafe = zlog_fd_sigsafe;
        zlog_startup_stderr.fd = STDERR_FILENO;
        zlog_target_replace(NULL, &zlog_startup_stderr.zt);

        hook_register(zlog_aux_init, zlt_aux_init);
        hook_register(zlog_init, zlt_init);
        hook_register(zlog_fini, zlt_fini);
}
```

即：**「配置加载完成之前，告警及以上一律写 stderr」**。

`zlog_startup_end()` [zlog_targets.c:568](lib/zlog_targets.c#L568) 宣布启动期结束：摘掉 stderr 目标；若不是辅助程序（`zlog_is_aux` 为假），则初始化 crashlog 目标 —— 此后崩溃日志写文件而非 stderr：

```c
zlog_target_replace(&zlog_startup_stderr.zt, NULL);
if (zlog_is_aux)
        return;
/* until here, crashlogs go to stderr */
zlog_crashlog_init();
```

`zlog_crashlog_init()` [zlog_targets.c:479](lib/zlog_targets.c#L479) 把 `zlog_crashlog` 以 `prio_min = LOG_CRIT` 挂入目标链，写函数是 `zlog_crashlog_plain`（普通路径，转发给 sigsafe 版）与 `zlog_crashlog_sigsafe` [zlog_targets.c:433](lib/zlog_targets.c#L433)。后者首次调用时用 `openat(zlog_tmpdirfd, "crashlog", ...)` 打开文件，失败记 `-2` 并彻底放弃。

> **官方文档提到的一处 TODO**（[logging.rst:907-912](doc/developer/logging.rst#L907)）：`zlog_startup_end()` 本应「显式切换」从 startup stderr 到已配置日志，但目前配置日志在各自 setup 执行时**并行**开始，导致**一段重叠期会有重复日志**。

### 6.3 崩溃诊断三件套（在 `log.c`）

| 函数 | 位置 | 输出 |
|---|---|---|
| `zlog_signal` | [log.c:160](lib/log.c#L160) | `Received signal %d at %lld (si_addr 0x.., PC 0x..); aborting...`，然后回调栈与线程信息 |
| `zlog_backtrace_sigsafe` | [log.c:208](lib/log.c#L208) | 只用 `libunwind` 或 glibc `backtrace`；**注意** 允许写 zlog 的文件状态 |
| `zlog_thread_info` | [log.c:345](lib/log.c#L345) | `Current thread function ..., scheduled from file ..., line ... in ...()` |
| `zlog_backtrace` | [log.c:278](lib/log.c#L278) | 非 AS-safe 版本，**允许 malloc**，用于 assert 等场景 |

`zlog_signal` 里读取线程信息用的是 `pthread_getspecific(thread_current)`（[log.c:188](lib/log.c#L188)），拿不到就打印 `no thread information available`。

### 6.4 assert 失败路径

`_zlog_assert_failed` [zlog.c:941](lib/zlog.c#L941) 用一个 `static bool assert_in_assert` 做**重入保护**（assert 里再 assert 会递归），最终 `abort()`。

---

## §7 内置目标：`zlog_targets.c` 与 `log_filter.c`

### 7.1 `zlog_fd` —— 一次调用如何变成一次 `writev`

[zlog_targets.c:106](lib/zlog_targets.c#L106) 是「批量聚合写出」的样板。它的手法是：**先把所有消息的时间戳、级别名、前缀、正文各自压进 iovec，最后一把 `writev`**。

关键实现细节：

```c
/* 时间戳缓冲：每条消息预留 TS_LEN 40 字节 */
#define TS_LEN 40
char ts_buf[TS_LEN * nmsgs], *ts_pos = ts_buf;

/* iovec 数量上界：每条最多 4 段（时间戳/严重级/优先级名/前缀）+正文，再加 1 */
size_t niov = MIN(4 * nmsgs + 1, IOV_MAX);
struct iovec iov[niov];
```

每条消息压入的段（顺序即输出顺序）：

| 顺序 | 段 | 条件 | 位置 |
|---|---|---|---|
| 1 | 时间戳 + 一个空格（`ZLOG_TS_LEGACY \| ts_subsec`） | 总是 | [zlog_targets.c:130-137](lib/zlog_targets.c#L130) |
| 2 | 严重级名（`"ERROR: "` 等，来自 `severity_names[]`） | `record_severity` | [:141-146](lib/zlog_targets.c#L141) |
| 3 | 优先级名（`"errors: "` 等，来自 `prionames[]`） | `record_priority` | [:148-154](lib/zlog_targets.c#L148) |
| 4 | 全局前缀 `zlog_prefix` | 总是 | [:156-159](lib/zlog_targets.c#L156) |
| 5 | 正文（**长度 `textlen + 1`，带上那个换行**） | 总是 | [:161-165](lib/zlog_targets.c#L161) |

两张名字表在 [zlog_targets.c:76](lib/zlog_targets.c#L76)（`severity_names`）与 [zlog_targets.c:86](lib/zlog_targets.c#L86)（`prionames`），都以 `LOG_*` 值作下标。

写出触发条件（[zlog_targets.c:173-175](lib/zlog_targets.c#L173)）三者之一：

1. 时间戳缓冲余量不足 `TS_LEN`；
2. 已是本批最后一条；
3. 剩余 iovec 不足 5 个。

函数末尾 `assert(iovpos == 0)` 保证不留半批（[zlog_targets.c:183](lib/zlog_targets.c#L183)）。

> **注意 `prio <= zt->prio_min`**（[:123](lib/zlog_targets.c#L123)）—— 这里比较用的是 `<=`，而核心层与 syslog 目标用的是 `>` 取反，语义等价但写法不统一，读的时候要留意。

### 7.2 文件目标的配置对象与「换出」模式

`struct zlog_cfg_file` [zlog_targets.h:45-62](lib/zlog_targets.h#L45)：

| 字段 | 语义 |
|---|---|
| `struct zlt_fd *active` | 当前活跃的目标实例 |
| `pthread_mutex_t cfg_mtx` | 配置修改锁 |
| `int prio_min` | 级别 |
| `char ts_subsec` | 亚秒位数 |
| `bool record_priority` / `record_severity` | 是否输出优先级名 / 严重级名 |
| `char *filename` / `int fd` | 输出位置（二选一） |
| `void (*zlog_wrap)(...)` | 可替换 `logfn` 的包装钩子（过滤目标用） |

**核心模式：任何配置变化都整体重跑一次 `zlog_file_cycle()`**（[:279](lib/zlog_targets.c#L279)）「换出」一个新目标：

```
zlog_file_cycle(zcf)
  ├─ prio_min == ZLOG_DISABLED ? 直接退出（不挂目标）
  ├─ fd != -1 ? dup(fd) : filename ? open(filename, O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOCTTY, LOGFILE_MASK)
  ├─ zlog_target_clone(...) 基于旧 active 克隆
  ├─ 填 fd / record_* / ts_subsec / prio_min / logfn(=zlog_wrap ?: zlog_fd) / logfn_sigsafe
  ├─ old = zlog_target_replace(active, new)；zcf->active = new
  └─ zlog_file_target_free(old)   ← 内部走 rcu_close + rcu_free
```

对外的四个入口都只是「改字段 + 调 cycle」：

| 入口 | 位置 | 改什么 |
|---|---|---|
| `zlog_file_init` | [:231](lib/zlog_targets.c#L231) | 清零，`prio_min = ZLOG_DISABLED`，`fd = -1` |
| `zlog_file_fini` | [:257](lib/zlog_targets.c#L257) | 摘目标、释放文件名、销毁锁 |
| `zlog_file_set_other` | [:333](lib/zlog_targets.c#L333) | 应用 prio_min/记录选项 → cycle |
| `zlog_file_set_filename` | [:345](lib/zlog_targets.c#L345) | 换文件名、`fd = -1` → cycle |
| `zlog_file_set_fd` | [:362](lib/zlog_targets.c#L362) | 换 fd、清文件名 → cycle |
| `zlog_file_rotate` | [:392](lib/zlog_targets.c#L392) | 重开 `filename`，用 `atomic_exchange` 替换 `active->fd`，旧 fd 交 RCU 关闭 |

> **设计要点**：`ZLOG_DISABLED`（[zlog.h:227](lib/zlog.h#L227)，值 `LOG_EMERG - 1`）作为「停用」哨兵非常巧妙 —— 它比任何真实优先级都小，所以 `prio <= prio_min` 恒不成立，目标自然静默。`zlog_file_init` 默认就设成它，意味着**未配置的文件目标不产生任何输出**。

> **陷阱**：`zlog_file_set_filename` 与 `zlog_file_set_fd` 结尾各有一句 `assert(0); return false;`（[:354](lib/zlog_targets.c#L354)、[:373](lib/zlog_targets.c#L373)）。这不是死代码 bug，而是为了满足 `frr_with_mutex` 宏块「必须 return」的编译要求而加的兜底断言。同理见 `zlog_syslog_get_facility` [:736](lib/zlog_targets.c#L736)。

### 7.3 过滤文件目标（`log_filter.c`）

「过滤文件」是**子串过滤**，不是正则：

| 函数 | 位置 | 匹配方式 |
|---|---|---|
| `zlog_filter_lookup` | [log_filter.c:69](lib/log_filter.c#L69) | `strncmp(lookup, zlog_filters[i], ...)` —— 前缀比较 |
| `search_buf` | [log_filter.c:162](lib/log_filter.c#L162) | `memmem(buf, len, filter, strlen(filter))` —— **二进制安全子串包含** |

数据结构：

```c
/* lib/log_filter.c:55-60 */
static pthread_mutex_t logfilterlock = PTHREAD_MUTEX_INITIALIZER;
static char zlog_filters[ZLOG_FILTERS_MAX][ZLOG_FILTER_LENGTH_MAX + 1];  /* [100][81] */
static uint8_t zlog_filter_count;
```

上限来自 [log.h:91-92](lib/log.h#L91)：`ZLOG_FILTERS_MAX 100`、`ZLOG_FILTER_LENGTH_MAX 80`。

返回码约定：`zlog_filter_add` → `0` 成功 / `1` 表满 / `-1` 重复或空（[:90](lib/log_filter.c#L90)）；`zlog_filter_del` → `-1` 未找到，否则 `memmove` 位移后 `0`（[:117](lib/log_filter.c#L117)）；`zlog_filter_dump` → 写入字节数或 `-1` 溢出（[:140](lib/log_filter.c#L140)）。

它挂进目标链的方式是复用 fd 目标并**替换写函数**：

```c
/* lib/log_filter.c:209-212 */
void zlog_filterfile_init(struct zlog_cfg_filterfile *zcf)
{
        zlog_file_init(&zcf->parent);
        zcf->parent.zlog_wrap = zlog_filterfile_fd;
}
```

`zlog_cfg_filterfile` 本身只有一个字段（[log.h:99-101](lib/log.h#L99)）：`struct zlog_cfg_file parent` —— 它**不是新类型**，而是「换了 logfn 的文件目标」。

`zlog_filterfile_fd` [log_filter.c:185](lib/log_filter.c#L185) 的过滤规则：**非 debug 消息无条件放行**；debug 消息（`zlog_msg_prio(...) >= LOG_DEBUG`，[:194](lib/log_filter.c#L194)）必须 `search_buf` 命中才放行。幸存者收集进 `msgfilt[]` 后转交 `zlog_fd`（[:203](lib/log_filter.c#L203)）。

### 7.4 syslog(3) 目标 —— 单例 + 全局变量存配置

与文件目标相反，syslog 目标**天生单例**：配置直接放在文件级 static 变量里（[:692-695](lib/zlog_targets.c#L692)）：

```c
static pthread_mutex_t syslog_cfg_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct zlt_syslog *zlt_syslog;
static int syslog_facility = LOG_DAEMON;
static int syslog_prio_min = ZLOG_DISABLED;
```

对外只有四个存取器（[zlog_targets.h:95-104](lib/zlog_targets.h#L95)）：`zlog_syslog_set/get_facility`、`zlog_syslog_set/get_prio_min`。**没有 `struct zlog_cfg_syslog` 这个类型** —— 这一点容易误记。

`zlog_syslog` [zlog_targets.c:601](lib/zlog_targets.c#L601) 的投递就是一行：`syslog(prio | facility, "%.*s", (int)text_len, text)`。

`zlog_syslog_sigsafe` [zlog_targets.c:632](lib/zlog_targets.c#L632) 完全绕开 libc 的 syslog 缓冲，自己连 `/dev/log`：

1. 先试 `socket(AF_UNIX, SOCK_DGRAM)` + `connect`；
2. 失败则退化为 `open(_PATH_LOG, O_WRONLY|O_NOCTTY)`（因为 `/dev/log` 可能是 FIFO 而非 socket，注释 [:658](lib/zlog_targets.c#L658)）；
3. 再失败记 `-2` 放弃；
4. 手工拼 `<PRI>前缀[pid]: ` 头部（[:671](lib/zlog_targets.c#L671)，注意要**砍掉 `zlog_prefix` 尾部 2 个字符**，因为前缀已含 `": "`），`writev` 写出。

### 7.5 辅助程序目标

| 目标 | 触发 | 位置 |
|---|---|---|
| `zlog_aux_stdout`（写 stdout） | `zlog_aux_init(prefix, prio_min)` 钩子 → `zlt_aux_init`，之后调 `zlog_startup_end()` | [zlog_targets.c:502](lib/zlog_targets.c#L502) |
| `zlt_init`（`openlog`） | `zlog_init` 钩子 | [zlog_targets.c:520](lib/zlog_targets.c#L520) |
| `zlt_fini`（`closelog`） | `zlog_fini` 钩子 | [zlog_targets.c:530](lib/zlog_targets.c#L530) |

`zlog_aux_init` 由工具/测试程序调用（对照 daemon 用 `zlog_init`），[zlog.c:1324](lib/zlog.c#L1324)。

---

## §8 RFC 5424 目标：一套可插拔的多目的地输出

### 8.1 目的地类型（只有 5 种）

`enum zlog_5424_dst` [zlog_5424.h:49](lib/zlog_5424.h#L49)：

| 枚举 | 值 | 打开方式 | 位置 |
|---|---|---|---|
| `ZLOG_5424_DST_NONE` | 0 | 返回 `-1`（禁用） | [zlog_5424.c:1026](lib/zlog_5424.c#L1026) |
| `ZLOG_5424_DST_FD` | 1 | `dup(fd)` + `getsockopt(SO_TYPE)` 探测 | [:1029](lib/zlog_5424.c#L1029) |
| `ZLOG_5424_DST_FILE` | 2 | `open()`，可带 `O_CREAT\|O_EXCL`；失败重试 | [:1065](lib/zlog_5424.c#L1065) |
| `ZLOG_5424_DST_FIFO` | 3 | `mkfifo()` 后按文件打开（`O_NONBLOCK`） | [:1044](lib/zlog_5424.c#L1044) |
| `ZLOG_5424_DST_UNIX` | 4 | `zlog_5424_unix()` 连接 UNIX 域套接字 | [:1109](lib/zlog_5424.c#L1109) |

**没有 TCP / UDP / 远端主机目的地**（[zlog_5424_cli.c:548-559](lib/zlog_5424_cli.c#L548) 有注释说明这是被否决的方案）。journald 与 syslog **不是独立目的地**，而是「UNIX 套接字 + 特定格式」的组合：

| 名义目的地 | 实际 | 位置 |
|---|---|---|
| journald | UNIX → `/run/systemd/journal/socket`，`SPECIAL_JOURNALD` | [zlog_5424_cli.c:517-518](lib/zlog_5424_cli.c#L517) |
| syslog | UNIX → `/dev/log`，`SPECIAL_SYSLOG` | [zlog_5424_cli.c:542-545](lib/zlog_5424_cli.c#L542) |

UNIX 套接字类型自动探测顺序（[zlog_5424.c:1015-1019](lib/zlog_5424.c#L1015)）：`SOCK_STREAM → SOCK_SEQPACKET → SOCK_DGRAM`；成功后把 `SO_SNDBUF` 往 1 MiB 提（[:961-992](lib/zlog_5424.c#L961)）。

### 8.2 报文格式：一个格式化器，三种方言

分派函数 `zlog_one()` [zlog_5424.c:459](lib/zlog_5424.c#L459)：

| 格式 | 枚举 | 格式化函数 | 位置 |
|---|---|---|---|
| RFC 5424 | `ZLOG_FMT_5424` | `zlog_5424_one` | [:230](lib/zlog_5424.c#L230) |
| RFC 3164 | `ZLOG_FMT_3164` | `zlog_3164_one` | [:323](lib/zlog_5424.c#L323) |
| local syslogd | `ZLOG_FMT_LOCAL` | 复用 3164 逻辑 | —— |
| journald | `ZLOG_FMT_JOURNALD` | `zlog_journald_one` | [:369](lib/zlog_5424.c#L369) |

RFC 5424 报文的字段构造顺序（[zlog_5424.c:230-313](lib/zlog_5424.c#L230)）：

| 字段 | 代码 | 行 |
|---|---|---|
| `PRI` = `prio \| facility`，加版本 `"1 "` | `bprintfrr(fbuf, "<%d>1 ", prio \| zte->facility)` | [:242](lib/zlog_5424.c#L242) |
| TIMESTAMP | `zlog_msg_ts(msg, fbuf, zte->ts_flags)` | [:243](lib/zlog_5424.c#L243) |
| HOSTNAME / APP-NAME / PROCID / MSGID | `" %s %s %jd %.*s "`，MSGID 取 `zlog_prefix` 去掉尾部 `": "` | [:244-246](lib/zlog_5424.c#L244) |
| SD `[origin enterpriseId="50145" software="FRRouting" swVersion="..."]` | 仅 `kw_version` | [:248-252](lib/zlog_5424.c#L248) |
| SD `[location@50145 tid="..."]` 开头 | 总是 | [:257](lib/zlog_5424.c#L257) |
| 可选 `instance=` / `id=`(uid) / `ec=` / `file=` `line=` `func=` | 按 `kw_*` 开关 | [:258-272](lib/zlog_5424.c#L258) |
| SD 结束 `]` | | [:274](lib/zlog_5424.c#L274) |
| SD `[args@50145 argN="..."]` | `kw_args && n_argpos` | [:283-294](lib/zlog_5424.c#L283) |
| MSG 正文（第二个 iovec，**跳过 `hdrlen` 前缀**） | `(char*)text + hdrlen`，长度 `textlen - hdrlen + use_nl` | [:312-313](lib/zlog_5424.c#L312) |

**企业号固定为 `50145`**；SD-ID 是 `origin`、`location@50145`、`args@50145`。

### 8.3 结构化数据的转义

SD-PARAM 的**值**通过 printfrr 的自定义格式符转义，不是手写转义：

| 格式符 | 语义 | 声明 | 用途 |
|---|---|---|---|
| `%pSQ` | 加双引号并转义内嵌引号 | [printfrr.h:398](lib/printfrr.h#L398) | RFC 5424 的 `argN="..."`（[zlog_5424.c:289](lib/zlog_5424.c#L289)） |
| `%pSE` | 转义不可打印字符 | [printfrr.h:397](lib/printfrr.h#L397) | journald 的 `FRR_ARGn=`（[:428](lib/zlog_5424.c#L428)） |

SD-PARAM 的**名字**是硬编码的（`argN`/`tid`/`id`/`ec`/`file`/`line`/`func`/`instance`），无用户输入，故不做转义。

### 8.4 断链重连与退避

只有 UNIX 套接字与 FD-socket 会重连（FILE/FIFO 一次打开，无重试）。

| 环节 | 位置 | 行为 |
|---|---|---|
| 判定需要重连 | [:1040](lib/zlog_5424.c#L1040)（FD 非 DGRAM）、[:1149](lib/zlog_5424.c#L1149)（UNIX 连接成功且非 DGRAM）、[:1142](lib/zlog_5424.c#L1142)（UNIX 连接失败） | 置 `need_reconnect` |
| 挂读事件（监听对端关闭） | [:1186](lib/zlog_5424.c#L1186) `event_add_read(...)` | 成功后复位退避 |
| 挂定时器（连接失败重试） | [:1191](lib/zlog_5424.c#L1191) `event_add_timer_msec(...)` | **只有 `_msec` 变体，没有 `event_add_timer`** |
| 退避增长 | [:1195-1196](lib/zlog_5424.c#L1195) `cur += cur / 2`（×1.5），上限 `reconn_backoff_max` | |
| 重连回调 | `zlog_5424_reconnect` [:920](lib/zlog_5424.c#L920) | `read()` 判定对端关闭后重开；**故意不 close 旧 fd**（注释 [:944](lib/zlog_5424.c#L944)） |
| 定时器取消 | [:1215](lib/zlog_5424.c#L1215)、[:1295](lib/zlog_5424.c#L1295) | `event_cancel(&zcf->t_reconnect)` |
| 默认值 | 25ms / 25ms / 10000ms（仅 CLI 设置） | [zlog_5424_cli.c:484-486](lib/zlog_5424_cli.c#L484) |

### 8.5 CLI：`log extended-syslog NAME` 块

配置块在 `EXTLOG_NODE`（[zlog_5424_cli.c:1043](lib/zlog_5424_cli.c#L1043)），入口命令 `log extended-syslog EXTLOGNAME`（[:222](lib/zlog_5424_cli.c#L222)）。**没有任何 `zlog ...` 形式的命令**。

| 命令 | 处理函数 | 行 |
|---|---|---|
| `log extended-syslog EXTLOGNAME` / `no ...` | `log_5424_target` / `no_log_5424_target` | [220](lib/zlog_5424_cli.c#L220) / [241](lib/zlog_5424_cli.c#L241) |
| `[no] destination file PATH [...] [format ...]` | `log_5424_destination_file` | [302](lib/zlog_5424_cli.c#L302) |
| `[no] destination fifo PATH [...]` | `ALIAS_HIDDEN` → 同上 | [437](lib/zlog_5424_cli.c#L437) |
| `[no] destination unix PATH [format ...]` | `log_5424_destination_unix` | [492](lib/zlog_5424_cli.c#L492) |
| `[no] destination journald` | `log_5424_destination_journald` | [510](lib/zlog_5424_cli.c#L510) |
| `[no] destination syslog [supports-rfc5424]` | `log_5424_destination_syslog` | [534](lib/zlog_5424_cli.c#L534) |
| `[no] destination fd\|envvar\|stdout\|stderr [format ...]` | `log_5424_destination_fd` | [568](lib/zlog_5424_cli.c#L568) |
| `[no] priority LEVEL` | `log_5424_prio` | [657](lib/zlog_5424_cli.c#L657) |
| `[no] facility FAC` | `log_5424_facility` | [676](lib/zlog_5424_cli.c#L676) |
| `[no] structured-data <code-location\|version\|unique-id\|error-category\|format-args>` | `log_5424_meta` | [697](lib/zlog_5424_cli.c#L697) |
| `[no] timestamp precision (0-9)` | `log_5424_ts_prec` | [728](lib/zlog_5424_cli.c#L728) |
| `[no] timestamp local-time` | `log_5424_ts_local` | [755](lib/zlog_5424_cli.c#L755) |

默认值（[zlog_5424_cli.c:86-88](lib/zlog_5424_cli.c#L86)）：`DFLT_TS_FLAGS (6 | ZLOG_TS_UTC)`、`DFLT_FACILITY LOG_DAEMON`、`DFLT_PRIO_MIN LOG_DEBUG`。

生命周期钩子在一个独立构造器里注册（[zlog_5424_cli.c:1115](lib/zlog_5424_cli.c#L1115)，`_CONSTRUCTOR(475)`）：

```c
hook_register(frr_early_init, log_5424_early_init);   /* :1117 */
hook_register(zlog_rotate,    log_5424_rotate);       /* :1118 */
hook_register(frr_fini,       log_5424_fini);         /* :1119 */
```

注意：**`zlog_5424.c` 自己不注册任何钩子**，全部生命周期由 CLI 文件负责。

### 8.6 平台差异

`zlog_5424.c` / `zlog_5424_cli.c` 里**没有任何 `HAVE_*` 条件编译**，journald 支持无需 libsystemd。唯一的 `#if` 是 `/dev/log` 默认格式的平台判断（[zlog_5424_cli.c:523-529](lib/zlog_5424_cli.c#L523)）：FreeBSD ≥ 1200061 与 NetBSD ≥ 5.0 用 RFC5424，其余（含 Linux）用 `ZLOG_FMT_LOCAL`。

---

## §9 实时日志流 `zlog_live`：daemon → vtysh 的日志通道

### 9.1 用途

`terminal monitor` 让用户在 vtysh 里实时看到守护进程日志。实现方式是：daemon 侧把每条日志**编码成二进制记录**，通过一个套接字 fd 交给 vtysh。fd 的传递用 `SCM_RIGHTS`。

### 9.2 线格式：`struct zlog_live_hdr`

[zlog_live.h:37-80](lib/zlog_live.h#L37)。**这是原生内存布局的直传，没有任何编码器**—— 一组 `iovec`（头部 + 可选 `argpos` + 正文）直接 `sendmmsg`。

| 字段 | 类型 | 行 | 语义 |
|---|---|---|---|
| `ts_sec` | `uint64_t` | [:39](lib/zlog_live.h#L39) | `CLOCK_REALTIME` 秒 |
| `ts_nsec` | `uint32_t` | [:40](lib/zlog_live.h#L40) | 纳秒 |
| `hdrlen` | `uint32_t` | [:45](lib/zlog_live.h#L45) | 头部总长（含变长部分）= 正文起始偏移 |
| `pid` / `tid` | `int64_t` | [:48](lib/zlog_live.h#L48) / [:49](lib/zlog_live.h#L49) | 进程 / 线程号 |
| `lost_msgs` | `uint32_t` | [:52](lib/zlog_live.h#L52) | **累计丢弃计数**（非阻塞模式） |
| `prio` | `uint32_t` | [:54](lib/zlog_live.h#L54) | syslog 优先级 |
| `flags` | `uint32_t` | [:56](lib/zlog_live.h#L56) | **当前未使用** |
| `textlen` | `uint32_t` | [:60](lib/zlog_live.h#L60) | 正文长度 |
| `texthdrlen` | `uint32_t` | [:66](lib/zlog_live.h#L66) | `"[XXXXX-XXXXX][EC 0] "` 前缀长度 |
| `uid[12]` | `char[]` | [:69](lib/zlog_live.h#L69) | xref 唯一 ID |
| `ec` | `uint32_t` | [:71](lib/zlog_live.h#L71) | 错误码 |
| `ts_subsec` | `uint8_t` | [:74](lib/zlog_live.h#L74) | 亚秒位数 |
| `_pad[3]` | `uint8_t[]` | [:75](lib/zlog_live.h#L75) | 对齐填充 |
| `n_argpos` | `uint32_t` | [:78](lib/zlog_live.h#L78) | 参数个数 |
| `argpos[0]` | `struct fmt_outpos[]` | [:79](lib/zlog_live.h#L79) | 变长参数位置数组 |

**三个重要「没有」**：

1. **没有 magic / version 字段** —— 线格式不版本化。唯一的兼容性保护是一句 `static_assert`，校验 `argpos` 元素的内存大小与线格式大小一致（[zlog.c:705-706](lib/zlog.c#L705)）。
2. **没有 origin / daemon 字段** —— 记录里不说自己来自哪个守护进程。**daemon 身份靠「从哪个 socket 收到」带外表达**（每个 `struct vtysh_client` 有自己的 `name` 与 `log_fd`，见 [vtysh/vtysh.h:175-177](vtysh/vtysh.h#L175)）。
3. **不发 JSON** —— vtysh 直接 `recv` 二进制结构（[vtysh/vtysh.c:4467](vtysh/vtysh.c#L4467)）。

### 9.3 投递路径

`zlog_live` [zlog_live.c:87](lib/zlog_live.c#L87) 为每条消息构造 1–3 个 iovec：

| iovec | 内容 | 行 |
|---|---|---|
| 1 | 头部 `sizeof(*hdr)` | [:122-124](lib/zlog_live.c#L122) |
| 2 | `argpos` 数组（`sizeof(*argpos) * n_argpos`） | [:126-130](lib/zlog_live.c#L126) |
| 3 | 正文（**不含前缀、不含换行**） | [:132-134](lib/zlog_live.c#L132) |

`hdr->hdrlen = sizeof(*hdr) + sizeof(*argpos) * n_argpos` 在 [:160](lib/zlog_live.c#L160) 填好，然后 `sendmmsg()` 批量发送（[:171](lib/zlog_live.c#L171)）。

**非阻塞、尽力而为**：发送失败且 `errno` 是 `EAGAIN`/`EWOULDBLOCK` 时，未发完的消息计入 `lost_msgs` 后**直接放弃**（[:173-178](lib/zlog_live.c#L173)）——**日志线程绝不阻塞**。vtysh 侧按差值打印「N log messages lost」提示。

信号安全版 `zlog_live_sigsafe` [zlog_live.c:206](lib/zlog_live.c#L206) 只有 2 个 iovec（头部 + 正文），不带 `argpos`，只用 `clock_gettime` + `writev`。

### 9.4 三态生命周期机

这是本文件最容易读错的部分。**没有引用计数**，只有一个原子 `state` 加 CAS 仲裁（枚举在 [zlog_live.c:49-53](lib/zlog_live.c#L49)）：

| 状态 | 含义 |
|---|---|
| `STATE_NORMAL` | 正常工作 |
| `STATE_FD_DEAD` | 写失败已判定，目标待回收 |
| `STATE_DISOWNED` | 调用方已 disown，所有权已交出 |

三条互斥的清理路径：

| 路径 | 函数 | 位置 | CAS |
|---|---|---|---|
| 显式关闭 | `zlog_live_close` | [:305](lib/zlog_live.c#L305) | 无（直接摘除 + 释放） |
| 调用方放弃 | `zlog_live_disown` | [:330](lib/zlog_live.c#L330) | `NORMAL → DISOWNED`；若观察到 `FD_DEAD` 则由**本次调用**负责释放（[:341-346](lib/zlog_live.c#L341)） |
| 写失败 | `out_err:` 标签 | [:184](lib/zlog_live.c#L184) | `NORMAL → FD_DEAD`；若观察到 `DISOWNED` 则由**本次调用**负责释放（[:192-197](lib/zlog_live.c#L192)） |

**仲裁逻辑**：两条路径都拿 `state` 做 CAS，且都把「我看到的旧值」留在同一个变量里。谁把状态推进到了终态，谁就负责释放；如果对方已经推进到终态，则由自己兜底释放。**保证恰有一方释放对象**。

创建只有一个入口：`zlog_live_open_fd` [zlog_live.c:280](lib/zlog_live.c#L280)，用 `zlog_target_clone(MTYPE_LOG_LIVE, NULL, sizeof(*zte))`（[:288](lib/zlog_live.c#L288)）+ `set_nonblocking(fd)`（[:292](lib/zlog_live.c#L292)）+ `zlog_target_replace(NULL, zt)`（[:298](lib/zlog_live.c#L298)）。

`zlog_live_open` [zlog_live.c:245](lib/zlog_live.c#L245) 是便利包装：建 `socketpair()`，**优先 `SOCK_SEQPACKET`**（[:262](lib/zlog_live.c#L262)），否则退 `SOCK_DGRAM`（[:263](lib/zlog_live.c#L263)），把读端经 `*other_fd` 交回调用方。

> **为什么要 SEQPACKET 优先？** [zlog_live.c:256-261](lib/zlog_live.c#L256) 的注释解释：`SOCK_SEQPACKET` 能收到「对端关闭」通知，避免 fd 泄漏；`SOCK_DGRAM` 收不到（vtysh 侧 [vtysh/vtysh.c:4614-4616](vtysh/vtysh.c#L4614) 也承认这一点）。

`zlog_live_cfg` [zlog_live.h:89](lib/zlog_live.h#L89) 简单到只有一个字段 `struct zlt_live *target`，`zlog_live_is_null()` [zlog_live.h:104](lib/zlog_live.h#L104) 就是判空。

### 9.5 全链路：`terminal monitor` 从 daemon 到终端

```
用户: vtysh 里敲 "terminal monitor"
   │
   ├─ vtysh 侧  vtysh_terminal_monitor  vtysh/vtysh.c:4532
   │    对每个 vclient 发一行 "terminal monitor"（:4539 定义 line[]）
   │    收 fd，存 vclient->log_fd（:4561）
   │    注册每客户端的读事件 vtysh_log_read（:4562-4563）
   │
   ├─ daemon 侧  VTY 命令 terminal monitor [detach]  lib/vty.c:3541
   │    zlog_live_open(&vty->live_log, LOG_DEBUG, &fd_ret)     （:3561）
   │    vty_pass_fd(vty, fd_ret)                               （:3568）
   │      └─ vty_pass_fd  lib/vty.c:2504：把 fd 记为 vty->pass_fd
   │      └─ 发送时用 SCM_RIGHTS  sendmsg   lib/vty.c:2439 / 2443
   │
   ├─ vtysh 收 fd  vtysh_client_receive  vtysh/vtysh.c:174
   │    cmsg_type = SCM_RIGHTS（:197）→ recvmsg（:202）→ 取出 fd（:207-217）
   │
   └─ 之后每产生一条日志：
        daemon: zlog_live()  zlog_live.c:87  → sendmmsg（二进制头 + argpos + 正文）
        vtysh : vtysh_log_read  vtysh/vtysh.c:4454
                  recv（:4467）
                  按 buf.text + sizeof(argpos[0]) * n_argpos 定位正文（:4518）
                  vtysh_log_print  vtysh/vtysh.c:4392
                    用 hdr->prio 选色，用 argpos 高亮参数，用 vclient->name 标来源
```

关闭路径：

| 触发 | 位置 |
|---|---|
| VTY 会话结束 | `vty_close` → `zlog_live_close(&vty->live_log)` [lib/vty.c:2744](lib/vty.c#L2744) |
| 用户 `no terminal monitor` | [lib/vty.c:3580](lib/vty.c#L3580) + vtysh 侧 [vtysh.c:4617-4621](vtysh/vtysh.c#L4617) |
| daemon 写失败 | `out_err` [zlog_live.c:184](lib/zlog_live.c#L184) |
| vtysh 读到 EOF / 错误 | [vtysh/vtysh.c:4481-4497](vtysh/vtysh.c#L4481) |

`terminal monitor detach`（[lib/vty.c:3556-3559](lib/vty.c#L3556)）打开一个独立 cfg 后**立即 disown**，让日志流在会话结束后继续存在。

`who` 命令会给有 live log 的会话标注 `, live log`（[lib/vty.c:3325](lib/vty.c#L3325)）。

---

## §10 日志回注 `zlog_recirculate`：把子进程的日志并回父进程

### 10.1 它解决的问题

ldpd 会 `fork()` 出 `lde` / `ldpe` 子进程。如果子进程各自直连后端（syslog、日志文件），输出会交错混乱且失去归属。所以：**子进程把日志编码后经 `socketpair` 发给父进程，父进程把它当自己的日志重新注入 zlog** —— 这就是 "recirculate"。

文件被有意拆成两半（[zlog_recirculate.c:8-33](lib/zlog_recirculate.c#L8) 的注释）：

- `zlog_recirculate.c`：**event-loop 半边**（收包、补终止符）；
- `zlog_recirculate_live_msg()` 在 `zlog.c`：**zlog 内部半边**（解码、重建消息、投递）。

适用范围注释写得很直白：**"runs in ldpd in the parent process ... not used anywhere else (yet?)"**（[zlog_recirculate.c:43-45](lib/zlog_recirculate.c#L43)）。

### 10.2 完整链路

```
ldpd 父进程                                    lde / ldpe 子进程
─────────────                                  ──────────────────
socketpair(AF_UNIX, SOCK_DGRAM)                （继承 LDPD_FD_LOG = 5）
  pipe_lde_log / pipe_ldpe_log
  ldpd/ldpd.c:324 / :334
        │
        ├─ 非阻塞 + cloexec（:344-359）
        │
        ├─ start_child(..., pipe_*_log[1])
        │    ldpd/ldpd.c:362-365
        │       └─ dup2(fd_log, LDPD_FD_LOG)   ldpd/ldpd.c:548
        │                                        （LDPD_FD_LOG 定义 ldpd/ldpd.h:33）
        │                                        ┌──────────────────────────────┐
        └────────────────────────────────────────▶ zlog_live_open_fd(&child_log, │
                                                    LOG_DEBUG, LDPD_FD_LOG)    │
                                                   ldpd/lde.c:129, ldpe.c:105   │
                                                 └──────────────────────────────┘
        │
zlog_recirculate_subscribe(master, pipe_*_log[0])
  ldpd/ldpd.c:372-373
        │
        ▼
zlog_recirculate_recv   lib/zlog_recirculate.c:65
  read(fd, rxbuf, sizeof(rxbuf) - 2)                    （:72）
  n_rd == 0  → close，不重新挂事件（:73-78）
  真错误     → zlog_warn + close（:79-85）
  否则         event_add_read(...) 重新挂（:87）
  rxbuf[n_rd] = '\n'; rxbuf[n_rd+1] = '\0';             （:92-93）
        │
        ▼
zlog_recirculate_live_msg(rxbuf, n_rd)   lib/zlog_recirculate.c:94
        │
        ▼
zlog_recirculate_live_msg   lib/zlog.c:670
  ├─ 校验 len / hdrlen / textlen（:677-693）
  ├─ 用 hdr->ts/pid/tid/prio 建栈上 struct zlog_msg（:686-690）
  ├─ 要求 text[textlen]=='\n' && text[textlen+1]=='\0'（:701-703）
  ├─ memcpy argpos（:707-708）
  ├─ uid → xref 反查：xrefdata_uid_find(&xrefdata_uid, &ref)（:723-724）
  │    找到且类型是 XREFT_LOGMSG → 取其格式串（:726-731）
  │    找不到 → "fake out format string"：msg->fmt = msg->text + hdr->texthdrlen（:732-735）
  └─ 投递：rcu_read_lock + frr_each_safe + zt->logfn(zt, &msg, 1)（:737-744）
```

**结果**：父进程本地**所有**目标（日志文件、syslog、以及挂在父进程上的 `terminal monitor`）都能看到子进程的日志。所以在 vtysh 里对 ldpd 做 `terminal monitor`，能同时看到 lde / ldpe 的输出。

### 10.3 边界与陷阱

| 陷阱 | 说明 | 位置 |
|---|---|---|
| uid → xref 反查**只在同一份二进制里有效** | 子父进程是同一程序所以能查到；换程序就查不到，此时 uid 丢失但消息仍能正确输出 | [zlog.c:710-722](lib/zlog.c#L710) 的注释 |
| `subscribe` 每个 fd 只能调用一次 | 调两次会注册两个读事件并发生竞争 | [zlog_recirculate.c:103-104](lib/zlog_recirculate.c#L103) |
| 读取长度必须 `sizeof(rxbuf) - 2` | 否则后面补 `\n\0` 会**栈溢出** | [zlog_recirculate.c:59-61](lib/zlog_recirculate.c#L59) |
| 头文件与实现里参数名不一致 | 声明用 `tm`（[zlog_recirculate.h:24](lib/zlog_recirculate.h#L24)），定义用 `el`（[zlog_recirculate.c:106](lib/zlog_recirculate.c#L106)） | —— |

---

## §11 配置面：VTY / 北向 / 命令行三个入口

### 11.1 全局状态中枢：`log_vty.c`

`log_vty.c` 是「日志子系统的全局状态与控制中枢」。它定义了**全部目标运行时对象**（[log_vty.c:67-103](lib/log_vty.c#L67)）：

| 对象 | 类型 | 用途 | 行 |
|---|---|---|---|
| `zt_file_cmdline` | `struct zlog_cfg_file` | 命令行 `--log file:` 指定的日志文件 | [:67](lib/log_vty.c#L67) |
| `zt_file` | `struct zlog_cfg_file` | 配置的日志文件（`log file`） | [:71](lib/log_vty.c#L71) |
| `zt_filterfile` | `struct zlog_cfg_filterfile` | 过滤日志文件（`log filtered-file`） | [:75](lib/log_vty.c#L75) |
| `zt_stdout_file` | `struct zlog_cfg_file` | 普通 stdout 目标 | [:83](lib/log_vty.c#L83) |
| `zt_stdout_journald` | `struct zlog_cfg_5424` | stdout 被 journald 接管时替代上面那个 | [:87](lib/log_vty.c#L87) |

以及四个级别变量（[log_vty.c:54-57](lib/log_vty.c#L54)），初值都是 `ZLOG_DISABLED`：

| 变量 | 来源 | 语义 |
|---|---|---|
| `log_config_stdout_lvl` | 配置文件 | stdout 级别 |
| `log_config_syslog_lvl` | 配置文件 | syslog 级别 |
| `log_cmdline_stdout_lvl` | 命令行 | stdout 级别 |
| `log_cmdline_syslog_lvl` | 命令行 | syslog 级别 |

**「配置文件与命令行取较松者」** 是这一层的核心不变式，由 `ZLOG_MAXLVL(a, b) = MAX(a, b)`（[log_vty.c:42](lib/log_vty.c#L42)、[log_nb.c:38](lib/log_nb.c#L38)）实现，出现在 [log_vty.c:272](lib/log_vty.c#L272)、[log_nb.c:53](lib/log_nb.c#L53)、[log_nb.c:156](lib/log_nb.c#L156)。

两个钩子的实例化也在这里（[log_vty.c:46-47](lib/log_vty.c#L46)）：

```c
DEFINE_HOOK(zlog_rotate, (), ());
DEFINE_HOOK(zlog_cli_show, (struct vty * vty), (vty));
```

`zlog_rotate()` [log_vty.c:234](lib/log_vty.c#L234) 的轮转顺序值得记住 —— **先转三个内置文件目标，再触发钩子让其他目标（如 5424）自己转**：

```c
zlog_file_rotate(&zt_file);
zlog_file_rotate(&zt_filterfile.parent);
zlog_file_rotate(&zt_file_cmdline);
hook_call(zlog_rotate);
```

名称表与转换函数：

| 表 / 函数 | 位置 | 说明 |
|---|---|---|
| `syslog_facilities[]` | [log_vty.c:119](lib/log_vty.c#L119) | `{facility, name, match}` 三元组；`match` 是**最短前缀匹配长度**，用来区分 `auth` / `authpriv`、`local0`..`local7` |
| `zlog_priority[]` | [log_vty.c:150](lib/log_vty.c#L150) | 级别值 → 名字（`"emergencies"`…`"debugging"`）；**无 extern 声明，仅文件内使用** |
| `syslog_severity[]` | [log_vty.c:156](lib/log_vty.c#L156) | 级别值 → syslog severity 名 |
| `zlog_priority_str` | [log_vty.c:164](lib/log_vty.c#L164) | 越界返回 `"invalid"`，负值返回 `"disabled"` |
| `facility_name` | [log_vty.c:177](lib/log_vty.c#L177) | 数值 → 名字 |
| `facility_match` | [log_vty.c:191](lib/log_vty.c#L191) | 名字 → 数值 |
| `log_level_match` | [log_vty.c:205](lib/log_vty.c#L205) | **只比前 2 个字符**（`strncmp(..., 2)`），所以 `"de"` 就能匹配 `debugging` |
| `log_lev2sev` | [log_vty.c:219](lib/log_vty.c#L219) | 级别名 → severity 名，识别失败回退 `"emergency"` |

> **`log_level_match` 只比 2 字符**是个值得注意的设计：它让 CLI 里级别既能写优先级名（`errors`）也能写 severity 名（`error`）—— 首两字符相同。`send log` 命令依赖这一点（[log_vty.c:298-299](lib/log_vty.c#L298) 的注释直言 "this checks using first 2 chars so works for both priority and severity"）。

### 11.2 老轨 VTY 命令（`log_vty.c` 定义，[log_cmd_init](lib/log_vty.c#L511) 注册）

| 命令 | 处理函数 | 节点 | 位置 |
|---|---|---|---|
| `send log [level LEVEL] LINE...` | `send_log` | VIEW | [log_vty.c:285](lib/log_vty.c#L285) |
| `show logging` | `show_logging` | VIEW | [log_vty.c:324](lib/log_vty.c#L324) |
| `show logging filter-text` | `show_log_filter` | VIEW | [log_vty.c:431](lib/log_vty.c#L431) |
| `[no] debug unique-id UID backtrace` | `debug_uid_backtrace` | ENABLE | [log_vty.c:389](lib/log_vty.c#L389) |

`show logging` 是诊断入口，汇总输出 syslog / stdout / 文件 / 过滤文件 / 命令行来源 / 协议名 / record 选项 / 时间戳精度，最后 `hook_call(zlog_cli_show, vty)` 让各目标（含 5424）追加自己的配置（[log_vty.c:378](lib/log_vty.c#L378)）。

`send log` 的实现有个细节：为拿到 `LINE` 的原文，需要**在 `vty->buf` 里跳过前面的关键字与空白**定位到 `LINE` 的位置（[log_vty.c:303-311](lib/log_vty.c#L303)），然后整体 `zlog(level, "%s", buf)`。

`debug_uid_backtrace`（VTY 版）直接改 xref 标志位 [log_vty.c:402-422](lib/log_vty.c#L402)：按 `vty->node == CONFIG_NODE` 决定是 `PERSISTENT` 还是 `EPHEMERAL`，并相应增减 `logmsgs_with_persist_bt` 计数。`no` 前缀表示关闭。

### 11.3 新轨 CLI（`log_cli.c`）

`log_cli.c` 里的**每个**处理函数都只做两件事：把用户输入翻译成 XPath change **入队**，然后 `nb_cli_apply_changes()`。**它从不直接碰 zlog 状态。**

| 命令 | 处理函数 | 行 |
|---|---|---|
| `[no] log stdout [LEVEL...]` | `config_log_stdout` | [log_cli.c:75](lib/log_cli.c#L75) |
| `[no] log monitor [...]` | `config_log_monitor`（**DEPRECATED 空实现**） | [:100](lib/log_cli.c#L100) |
| `[no] debug unique-id UID backtrace` | `debug_uid_backtrace`（CONFIG 节点版） | [:116](lib/log_cli.c#L116) |
| `log daemon DAEMON file FILENAME [LEVEL]` / `no ...` | `config_log_dmn_file` / `no_config_log_dmn_file` | [:138](lib/log_cli.c#L138) / [:165](lib/log_cli.c#L165) |
| `[no] log file ![FILENAME [LEVEL]]` | `config_log_file` | [:187](lib/log_cli.c#L187) |
| `log syslog [LEVEL...]` / `no ...` | `config_log_syslog` / `no_config_log_syslog` | [:213](lib/log_cli.c#L213) / [:232](lib/log_cli.c#L232) |
| `[no] log facility <...>` | `config_log_facility` / `no_config_log_facility` | [:247](lib/log_cli.c#L247) / [:263](lib/log_cli.c#L263) |
| `[no] log record-priority` | `config_log_record_priority` | [:279](lib/log_cli.c#L279) |
| `[no] log record-severity` | `config_log_record_severity` | [:293](lib/log_cli.c#L293) |
| `[no] log timestamp precision (0-6)` | `config_log_timestamp_precision` / `no_...` | [:308](lib/log_cli.c#L308) / [:325](lib/log_cli.c#L325) |
| `[no] log error-category` | `config_log_ec` | [:341](lib/log_cli.c#L341) |
| `[no] log unique-id` | `config_log_xid` | [:356](lib/log_cli.c#L356) |
| `[no] log filtered-file FILENAME [LEVEL...]` | `config_log_filterfile` / `no_...` | [:372](lib/log_cli.c#L372) / [:394](lib/log_cli.c#L394) |
| `[no] log filter-text WORD` | `log_filter` | [:411](lib/log_cli.c#L411) |
| `clear log filter-text` | `log_filter_clear` | [:428](lib/log_cli.c#L428) |
| `[no] log immediate-mode` | `log_immediate_mode` | [:445](lib/log_cli.c#L445) |
| `clear log cmdline-targets`（RPC） | `clear_log_cmdline` | [:461](lib/log_cli.c#L461) |

安装：`log_cli_init` [log_cli.c:475](lib/log_cli.c#L475)，配置命令进 `CONFIG_NODE`，RPC 进 `ENABLE_NODE`（[:501](lib/log_cli.c#L501)）。

### 11.4 新轨 NB 回调（`log_nb.c`）

`frr_logging_nb_info` [log_nb.c:785](lib/log_nb.c#L785) 把每个 XPath 映射到 create/destroy/modify/rpc 回调。**绝大多数回调在非 `NB_EV_APPLY` 事件下直接返回 `NB_OK`**（例如 [log_nb.c:220](lib/log_nb.c#L220)），只有 APPLY 才改运行状态 —— 这是北向的事务语义。

全部 XPath（共 21 条）：

| XPath | 回调 |
|---|---|
| `/frr-logging:clear-cmdline-targets` | rpc [:789](lib/log_nb.c#L789) |
| `/frr-logging:logging/stdout` | create [:795](lib/log_nb.c#L795) / destroy |
| `/frr-logging:logging/stdout/level` | modify [:802](lib/log_nb.c#L802) |
| `/frr-logging:logging/syslog` | create [:808](lib/log_nb.c#L808) / destroy |
| `/frr-logging:logging/syslog/level` | modify [:815](lib/log_nb.c#L815) |
| `/frr-logging:logging/file/filename` | modify [:821](lib/log_nb.c#L821) / destroy |
| `/frr-logging:logging/file/level` | modify [:828](lib/log_nb.c#L828) |
| `/frr-logging:logging/filtered-file/filename` | modify [:834](lib/log_nb.c#L834) / destroy |
| `/frr-logging:logging/filtered-file/level` | modify [:841](lib/log_nb.c#L841) |
| `/frr-logging:logging/filter-text` | create [:847](lib/log_nb.c#L847) / destroy |
| `/frr-logging:logging/daemon-file` | create [:854](lib/log_nb.c#L854) / destroy |
| `/frr-logging:logging/daemon-file/filename` | modify [:861](lib/log_nb.c#L861) |
| `/frr-logging:logging/daemon-file/level` | modify [:867](lib/log_nb.c#L867) |
| `/frr-logging:logging/facility` | modify [:873](lib/log_nb.c#L873) |
| `/frr-logging:logging/record-priority` | modify [:879](lib/log_nb.c#L879) |
| `/frr-logging:logging/record-severity` | modify [:885](lib/log_nb.c#L885) |
| `/frr-logging:logging/timestamp-precision` | modify [:891](lib/log_nb.c#L891) |
| `/frr-logging:logging/error-category` | modify [:897](lib/log_nb.c#L897) |
| `/frr-logging:logging/unique-id` | modify [:903](lib/log_nb.c#L903) |
| `/frr-logging:logging/immediate-mode` | modify [:909](lib/log_nb.c#L909) |
| `/frr-logging:logging/uid-backtrace` | create [:915](lib/log_nb.c#L915) / destroy |

两个例外值得记住：

- `logging_uid_backtrace_create`（[:703](lib/log_nb.c#L703)）**也处理 `NB_EV_VALIDATE`**（[:709](lib/log_nb.c#L709)），在 VALIDATE 阶段校验 UID 确实对应一条日志消息（[:717-724](lib/log_nb.c#L717)）——这是事务式配置才有的能力。
- 其余全部只认 `NB_EV_APPLY`。

两个基础设施：

| 函数 | 位置 | 作用 |
|---|---|---|
| `log_stdout_apply_level` | [log_nb.c:49](lib/log_nb.c#L49) | 统一 stdout 级别：取配置与命令行的 MAX；journald 接管时改 `zt_stdout_journald.prio_min` + `zlog_5424_apply_meta`，否则改 `zt_stdout_file.prio_min` + `zlog_file_set_other` |
| `set_log_file` | [log_nb.c:74](lib/log_nb.c#L74) | 把相对文件名拼成 `cwd/name` 绝对路径（带长度校验），再 `zlog_file_set_filename` |
| `_get_level_value` | [log_nb.c:191](lib/log_nb.c#L191) | 从 YANG 节点取 level 字符串；字段不存在（还是 YANG 默认值）时返回 `log_default_lvl` |

`log_default_lvl` 定义在 [log_nb.c:42](lib/log_nb.c#L42)（`= LOG_DEBUG`），声明在 [log_vty.h:64](lib/log_vty.h#L64)。

### 11.5 双轨缝合：`frr_logging_merge_cli_to_nb_info`

已在 §1.4 详述。补充两点：

- `frr_logging_cli_info` [log_cli.c:663](lib/log_cli.c#L663) 设 `.ignore_cfg_cbs = true`（[:665](lib/log_cli.c#L665)）—— 明确宣告「这本表只提供回显，不提供配置回调」。
- 该表**故意省略了 `filter-text`**（在 [log_cli.c:672](lib/log_cli.c#L672) 处被注释掉，原注释 `not saved, but may be the future`）—— 过滤器文本当前不写入 running-config。

### 11.6 命令行早期路径：`command_setup_early_logging`

[log_nb.c:123](lib/log_nb.c#L123)。为什么需要它？因为 `--log` 参数在**读配置之前**就得生效，而此刻 YANG 树、事件循环、VTY 都还不存在。所以只能特事特办，直接操作底层对象。

解析 `TYPE[:arg]`，`TYPE` 取值：

| TYPE | 行为 | 位置 |
|---|---|---|
| `stdout` | 设 `log_cmdline_stdout_lvl` + `log_stdout_apply_level()` | [:148](lib/log_nb.c#L148) |
| `syslog` | 设 `log_cmdline_syslog_lvl` + `zlog_syslog_set_prio_min(MAX(...))` | [:153](lib/log_nb.c#L153) |
| `file:<path>` | `set_log_file(&zt_file_cmdline, NULL, path, nlevel)` | [:159](lib/log_nb.c#L159) |
| `monitor:<fd>` | `zlog_live_open_fd(&cfg, nlevel, fd)` 然后 `zlog_live_disown(&cfg)` | [:165-179](lib/log_nb.c#L165) |

**级别非法或 TYPE 非法会直接 `exit(1)`**（[:134](lib/log_nb.c#L134)、[:183](lib/log_nb.c#L183)）。

`monitor:<fd>` 这一支很有意思：它正是 §9 里 `terminal monitor` 的**非交互变体** —— 启动方（例如 fuzzing harness 或测试框架）自己把一个 fd 传进来（常用 `--log monitor:1`），进程把日志流写进去后 `disown`，自己不管理生命周期。

`clear_cmdline_targets()` [log_vty.c:266](lib/log_vty.c#L266) 是其对称操作：配置读完后调用，把命令行来的目标全部关掉，让配置文件接管。

### 11.7 配置时序图

```
启动早期                                     配置加载后
────────                                     ──────────
构造器 _CONSTRUCTOR(450)  zlog_targets.c:549
  └─ 挂 startup-stderr 目标（LOG_WARNING+）
  └─ 注册 zlog_init / zlog_fini / zlog_aux_init 钩子

构造器 _CONSTRUCTOR(475)  log_vty.c:501
  └─ hook_register(zlog_init, log_vty_init)

构造器 _CONSTRUCTOR(475)  zlog_5424_cli.c:1115
  └─ 注册 frr_early_init / zlog_rotate / frr_fini 钩子

frr_init()（libfrr.c:864）
  └─ zlog_init(progname, protoname, instance, uid, gid)   zlog.c:1339
       ├─ 拼 zlog_prefix / zlog_tmpdir
       ├─ mkdir /var/tmp/frr 与 per-pid 子目录
       ├─ open zlog_tmpdirfd（O_PATH|O_RDONLY|O_CLOEXEC）
       └─ hook_call(zlog_init, ...)
            ├─ log_vty_init   log_vty.c:475
            │    ├─ 记 progname/protoname
            │    ├─ 注册 log_vty_fini
            │    ├─ zlog_set_prefix_ec(true) / zlog_set_prefix_xid(true)
            │    ├─ zlog_filterfile_init(&zt_filterfile)
            │    └─ sd_stdout_is_journal ?
            │         ├─ 是 → zlog_5424_init + zlog_5424_apply_dst(zt_stdout_journald)
            │         └─ 否 → zlog_file_set_fd(&zt_stdout_file, STDOUT_FILENO)
            └─ zlt_init   zlog_targets.c:520  → openlog(LOG_DAEMON)

命令行 --log 处理                                ↓
  └─ command_setup_early_logging  log_nb.c:123   │
       ├─ 设 log_cmdline_*_lvl / zt_file_cmdline │
       └─ monitor:<fd> → zlog_live_open_fd+disown│
                                                 ▼
                                     读配置文件（NB / VTY）
                                       → log_config_*_lvl / zt_file 等
                                                 │
                                                 ▼
                                     clear_cmdline_targets()  log_vty.c:266
                                       （命令行目标退场，配置接管）
                                                 │
                                                 ▼
                                     zlog_startup_end()  zlog_targets.c:568
                                       ├─ 摘掉 startup-stderr 目标
                                       └─ zlog_crashlog_init()（非 aux 程序）

退出
  └─ zlog_fini()  zlog.c:1405 → hook_call(zlog_fini)
       ├─ log_vty_fini  log_vty.c:458（关 zt_file_cmdline / zt_file）
       └─ zlt_fini      zlog_targets.c:530（closelog）
  └─ zlog_tmpdir_fini()  zlog.c:1416（关 fd + rmdir）
```

---

## §12 `log.c` 里还剩什么：老系统遗物清单

`log.c` 是「留在这里的名字最容易骗人」的文件 —— **它一个引擎都没有**，全是周边工具。完整清单（21 个函数 + 4 张表）：

| 类目 | 符号 | 位置 |
|---|---|---|
| **消息表查名** | `lookup_msg` | [log.c:68](lib/log.c#L68) |
| **时间戳** | `frr_timestamp`（带整秒缓存的 `strftime`） | [log.c:89](lib/log.c#L89) |
| **崩溃诊断** | `zlog_signal` / `zlog_backtrace_sigsafe` / `zlog_backtrace` / `zlog_thread_info` | [160](lib/log.c#L160) / [208](lib/log.c#L208) / [278](lib/log.c#L278) / [345](lib/log.c#L345) |
| **OOM** | `memory_oom`（打印后 `abort()`，**不返回**） | [log.c:365](lib/log.c#L365) |
| **错误串兜底** | `safe_strerror` | [log.c:380](lib/log.c#L380) |
| **zserv 命令表** | `command_types[]`（在 [log.c:541](lib/log.c#L541) 结束）+ `unknown` 兜底项 [:543](lib/log.c#L543) + `zroute_lookup` [:551](lib/log.c#L551) | |
| **路由类型查名** | `zebra_route_string` / `zebra_route_char` | [578](lib/log.c#L578) / [586](lib/log.c#L586) |
| **zserv 命令查名** | `zserv_command_string` | [595](lib/log.c#L595) |
| **GR 能力表** | `gr_client_cap_types[]` [:610](lib/log.c#L610) + `zserv_gr_client_cap_string` [:623](lib/log.c#L623) | |
| **协议名互换** | `proto_name2num` / `proto_redistnum` | [637](lib/log.c#L637) / [653](lib/log.c#L653) |
| **十六进制转储** | `zlog_hexdump`（每行 8 字节 + ASCII） | [736](lib/log.c#L736) |
| **时间截断** | `frr_time_t_to_uint32_t`（饱和转换到 `UINT32_MAX`） | [792](lib/log.c#L792) |
| **文本清洗** | `zlog_sanitize`（控制字符 / `"` / ≥127 转 `\xHH`） | [805](lib/log.c#L805) |

两个要点：

1. **`route_types[]` 不在 `log.c` 里**。它由 [lib/route_types.pl](lib/route_types.pl) 生成，靠 [log.c:28](lib/log.c#L28) 的 `#define FRR_DEFINE_DESC_TABLE` 开关打开。`log.c` 里的 `DESC_ENTRY(T)` 是**本地重定义的三成员版本**（[log.c:392](lib/log.c#L392) 与 [:609](lib/log.c#L609)，各配一次 `#undef`），与 `route_types.pl` 里的三参数版本是同名不同物。
2. **`zlog_hexdump` 依赖 `zlog_debug`**（[log.c:743](lib/log.c#L743)、[:775](lib/log.c#L775)）—— 所以这些"老"辅助其实**已经在用新核心**。这正是 §0.1「API 维度不是两套」的字面证据。

---

## §13 陷阱清单（速查）

### 13.1 概念层面

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 以为「文件名 `log_*` = 老系统，`zlog_*` = 新系统」 | 读代码时把 `log_vty.c` 当成可删的遗留物，理解不了配置栈 | 按 §0.2 的符号前缀检索判断代际；`log_vty.c` 是**当前活跃的配置中枢** |
| 2 | 以为 `log.h` 是老接口头，可以不理 | 漏掉它是新核心的伞头（[log.h:39-40](lib/log.h#L39)），也漏掉它里面的活声明 | `log.h` = 老声明 + 新 `#include` 的混合体，逐条看 |
| 3 | 以为 `log_cli.c` 是新版 `log_vty.c`（替换关系） | 找不到 `show logging` 就以为功能丢了 | 二者**并存且分工**：CLI 管配置、VTY 管诊断与运行时操作，见 §11 |
| 4 | 找 `struct zlog_cfg_syslog` | 编译不过 —— 该类型**不存在** | syslog 配置是标量 + `zlog_syslog_*` 存取器（[zlog_targets.h:95](lib/zlog_targets.h#L95)） |
| 5 | 以为 `log_filter.c` 用正则 | 按正则语义写过滤规则，永远不匹配 | 是**字面子串匹配**：`memmem`（[log_filter.c:168](lib/log_filter.c#L168)）与 `strncmp`（[:72](lib/log_filter.c#L72)） |
| 6 | 找 `struct zlog_target_5424` / 各目的地子结构 | 找不到 | 只有 `struct zlt_5424`（[zlog_5424.c:81](lib/zlog_5424.c#L81)）与 `struct zlog_cfg_5424`（[zlog_5424.h:97](lib/zlog_5424.h#L97)），**无子结构** |
| 7 | 找 `zlog ...` 形式的 CLI 命令 | 找不到 | 全部是 `log ...`；5424 子命令在 `log extended-syslog NAME` 块内 |
| 8 | 以为 5424 目标支持 TCP / UDP / 远端主机 | 配置不生效 | **不支持**（被否决，[zlog_5424_cli.c:548](lib/zlog_5424_cli.c#L548)） |
| 9 | 以为 journald 需要 libsystemd | 白找依赖 | 只是往 `/run/systemd/journal/socket` 写，**无 `HAVE_*` 条件编译** |

### 13.2 消息对象层面

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 10 | 把 `zlog_msg_text()` 的返回值当 C 字符串 | 越界读到下一条数据 | 返回值**不以 `\0` 结尾**；换行在 `text[textlen]`，`\0` 在 `text[textlen+1]`（[zlog.h:263](lib/zlog.h#L263)） |
| 11 | 自己写目标时忘了做 `prio_min` 过滤 | 输出比配置更啰嗦 | 核心层**不按目标过滤数组**，`logfn` 必须逐个检查（官方文档 [logging.rst:878-883](doc/developer/logging.rst#L878)）；写法参考 [zlog_targets.c:123](lib/zlog_targets.c#L123) 的 `<=` |
| 12 | 假设 `zlog_err` 会带错误码前缀 | 输出里没有 `[EC n]` | 只有 `flog_*` 才传 EC（[zlog.h:201](lib/zlog.h#L201)） |
| 13 | 以为 TLS 缓冲会推迟所有日志 | 崩溃时找不到最后几行 WARNING | 重要消息不延迟：`prio < LOG_INFO` 立即 flush（§5.4） |

### 13.3 目标与并发层面

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 14 | `zlog_target_replace` 后立刻 `free` / `close` 旧目标 | **use-after-free** 或给已关 fd 写入 | 旧目标须存活到 RCU 周期结束；用 `zlog_target_free`（rcu_free）+ `rcu_close`（[zlog.h:388-389](lib/zlog.h#L388)） |
| 15 | 以为 `zlog_target_replace` 是原子的 | 短暂窗口内同一条消息**重复输出** | 这是**已知且文档化**的行为（[logging.rst:865-867](doc/developer/logging.rst#L865)），目标实现须能容忍重复 |
| 16 | 以为 `zlog_target_clone` 会连目标特有字段一起复制 | 新目标缺字段（如 `syslog_facility`） | **只复制 `struct zlog_target` 通用字段**，特有字段必须自己补（[zlog_targets.c:717](lib/zlog_targets.c#L717)） |
| 17 | 把 `struct zlog_target` 放在容器结构**非首个**字段 | `container_of` 反查错位，内存踩踏 | 必须首字段（[zlog.h:340-343](lib/zlog.h#L340)） |
| 18 | 在 `logfn_sigsafe` 里 `malloc` / `printf` | 崩溃现场死锁或二次崩溃 | 只用 AS-safe 调用；不支持就置 `NULL`（[zlog.h:368](lib/zlog.h#L368)） |
| 19 | 误以为 `ZLOG_DISABLED` 会输出所有消息 | 目标静默无输出 | 它比 `LOG_EMERG` 还小，`prio <= prio_min` 恒假 ⇒ **停用**（[zlog.h:227](lib/zlog.h#L227)） |

### 13.4 `zlog_live` / 回注层面

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 20 | 从 `zlog_live_hdr` 里找 daemon 名 / magic / version | 找不到 | 三者都不存在；daemon 身份靠 socket 带外传递（[vtysh/vtysh.h:175](vtysh/vtysh.h#L175)） |
| 21 | 以为 live 流会阻塞等消费 | —— | 满/`EAGAIN` 时**丢消息并累加 `lost_msgs`**，日志线程绝不阻塞（[zlog_live.c:173](lib/zlog_live.c#L173)） |
| 22 | `zlog_live_disown` 与写失败路径同时发生 | 双重释放 或 泄漏 | 靠 `state` 的 CAS 仲裁，恰一方释放（[zlog_live.c:341](lib/zlog_live.c#L341) / [:192](lib/zlog_live.c#L192)） |
| 23 | 回注时 `read` 用整个缓冲区长度 | 后面补 `\n\0` 时**栈溢出** | 必须 `sizeof(rxbuf) - 2`（[zlog_recirculate.c:72](lib/zlog_recirculate.c#L72)） |
| 24 | 对同一个 fd 调两次 `zlog_recirculate_subscribe` | 注册两个读事件，竞争 | 每个 fd 只调一次（[zlog_recirculate.c:103](lib/zlog_recirculate.c#L103)） |
| 25 | 期望回注能恢复 uid（跨程序） | uid 丢失 | `uid→xref` 反查只在**同一份二进制**内有效；查不到时退化为「伪造格式串」（[zlog.c:734](lib/zlog.c#L734)） |

### 13.5 配置层面

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 26 | 改了 `log_config_stdout_lvl` 就直接调 `zlog_file_set_other` | 命令行级别被忽略 | 统一走 `log_stdout_apply_level()`，它取二者 MAX（[log_nb.c:49](lib/log_nb.c#L49)） |
| 27 | 以为 5424 目标自己注册生命周期钩子 | 在 `zlog_5424.c` 里找不到 | 钩子在 CLI 文件里注册（[zlog_5424_cli.c:1115](lib/zlog_5424_cli.c#L1115)） |
| 28 | 以为 `log_vty.c` 里的 `zlog_file_set_*` 系列末尾 `assert(0)` 是死代码 | 试图删掉它 | 是**为满足 `frr_with_mutex` 宏块必须 return 的编译要求**（[zlog_targets.c:354](lib/zlog_targets.c#L354)） |
| 29 | 以为 `log_level_match` 做完整匹配 | 用 `"deb"` / `"error"` 之类的输入，行为不符预期 | 只比**前 2 字符**（[log_vty.c:210](lib/log_vty.c#L210)）—— 这是刻意的，为了同时接受优先级名与 severity 名 |
| 30 | 在 5424 配置里改 stdout-journald 目标的 facility / 关键字开关 | 改不动 | [log_vty.c:94-96](lib/log_vty.c#L94) 的注释说明：它替代「普通」stdout 目标，这些字段**不可经配置修改** |

---

## §14 勘误：本仓库现有注释中与代码不符之处

本文档写作过程中对照代码复核，发现现有中文注释有以下不准确之处（**均未改动源码**，仅在此登记）：

| 位置 | 注释原文 | 实际情况 |
|---|---|---|
| [log.h:16-17](lib/log.h#L16) | 「老的过滤器接口：…**按正则过滤日志行**」 | **不是正则**，是字面子串匹配：`strncmp`（[log_filter.c:72](lib/log_filter.c#L72)）+ `memmem`（[log_filter.c:168](lib/log_filter.c#L168)）。全文件无 `regcomp`/`regexec`/`regex.h` |
| [log.h:113](lib/log.h#L113) | 「非 0 表示失败（如超限或**正则非法**）」 | 同上，不存在「正则非法」这一失败原因。实际失败原因是**表满**（返回 1）或**重复/空**（返回 -1） |
| [log_vty.c:22-25](lib/log_vty.c#L22) | 「应用调用 `zlog_*` 宏 -> **lib/log.c 的 zlog 核心**统一格式化」 | 核心在 [lib/zlog.c](lib/zlog.c)，`log.c` 只有周边辅助（§12）；`log.c` 里没有任何格式化逻辑 |
| [log_vty.h:16-17](lib/log_vty.h#L16) | 提到 `log_config_write` | 该函数**全树只有声明**（[log_vty.h:39](lib/log_vty.h#L39)），无定义，是遗留死声明 |

> 以上四处均为注释文本问题，**不涉及任何代码行为**。已在文档中按正确语义书写；是否修正源注释留待你决定。

---

## §15 自测题（复习用）

1. 「文件名带 `log_` 就是老代码」这句话错在哪里？给出至少 3 个反例。
2. `zlog_` 前缀被两代代码共用的历史原因是什么？这造成了什么实际困扰？
3. FRR 官方文档说 `zlog_targets` 链「**is not used for configuration**」，那么日志配置到底存在哪里？
4. 画出「一次 `flog_err(EC_LIB_SYSTEM_CALL, ...)` 从调用点到写进日志文件」的完整链路，标出至少 6 个关键函数。
5. `log.h` 与 `zlog.h` 是什么关系？为什么 347 个文件 include `log.h` 而只有 7 个直接 include `zlog_*`？
6. 「延迟格式化」延迟的是什么？在哪个函数里真正发生？为什么它在 TLS 缓冲路径上会打折？
7. `struct zlog_msg` 的 `text` 字段为什么不能当 C 字符串用？证据在哪两处代码里？
8. 为什么 `struct zlog_target` 必须内嵌为容器结构的**首个**字段？
9. `zlog_target_replace` 的「先加后删」会导致什么可观察现象？为什么这是可接受的？
10. `zlog_target_clone` 不复制目标特有字段 —— 请举出一个具体例子里必须手动补哪个字段。
11. TLS 缓冲解决了什么矛盾？为什么它把消息元数据排除在 mmap 之外？代价是什么？
12. 崩溃路径 `zlog_sigsafe` 为什么不能复刻常规路径？它把消息投给了哪些目标？
13. `ZLOG_DISABLED` 为什么能表示「停用」？它的数值有什么特点？
14. `zlog_fd` 一条消息最多产生几个 iovec？为什么正文长度是 `textlen + 1`？
15. 过滤日志文件与普通日志文件的实现关系是什么？「过滤」发生在链条的哪一步？
16. syslog 目标为什么是单例？它的信号安全版本为什么不复用 libc 的 `syslog()`？
17. RFC 5424 与 RFC 3164 报文的格式化函数分别在哪里？`PRI` 字段是怎么算出来的？
18. 5424 目标支持哪些目的地？为什么没有 TCP/UDP？journald 与 syslog 各对应什么组合？
19. `zlog_live_hdr` 里有哪三个「应该存在却不存在」的字段？daemon 身份靠什么传递？
20. `struct zlt_live` 的三个状态是什么？两条清理路径如何保证「恰有一方释放」？
21. 为什么 `zlog_live_open` 优先用 `SOCK_SEQPACKET` 而非 `SOCK_DGRAM`？
22. `zlog_recirculate` 解决什么问题？为什么它必须拆成两个文件实现？
23. 回注时为什么 `read` 要少读 2 字节？不这么做会怎样？
24. 「配置文件与命令行取较松者」这个不变式体现在哪几个函数里？为什么需要它？
25. 新老两条配置轨各负责什么？`frr_logging_merge_cli_to_nb_info()` 缝合了两张表的哪两个字段？
26. `log_level_match` 为什么只比较前 2 个字符？这个设计让 CLI 获得了什么便利？
27. `command_setup_early_logging` 为什么不能走北向框架？它支持哪四种目标类型？
28. `log.c` 里有没有日志引擎？列出它实际包含的三类内容，并说明 `route_types[]` 为什么不在这里。
29. `log_vty.c` 里为什么会出现 `assert(0); return false;` 这样的「死代码」？
30. 从「一次 `terminal monitor`」到「终端上出现一行日志」，依次经过哪几个关键函数与进程？

---

## §16 代码索引（按建议阅读顺序）

### 第 1 步：接口与模型（先看声明，建立心智模型）

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/zlog.h](lib/zlog.h) | 全文 478 行 | `struct xref_logmsg` [:77](lib/zlog.h#L77)、`struct zlog_target` [:355](lib/zlog.h#L355)、宏家族 [:189-207](lib/zlog.h#L189)、时间戳标志 [:286-296](lib/zlog.h#L286) |
| [lib/log.h](lib/log.h) | 全文 316 行 | `#include` 关系 [:38-40](lib/log.h#L38)、过滤上限 [:91-92](lib/log.h#L91)、`zlog_cfg_filterfile` [:99](lib/log.h#L99)、`struct timestamp_control` [:252](lib/log.h#L252) |

### 第 2 步：核心引擎

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/zlog.c](lib/zlog.c) | 文件头 [:6-46](lib/zlog.c#L6) | 全局变量 [:108-145](lib/zlog.c#L108) |
| | 读顺序建议 | `struct zlog_msg` [:168](lib/zlog.c#L168) → TLS 结构 [:248](lib/zlog.c#L248) → `zlog_tls_buffer_flush` [:520](lib/zlog.c#L520) → `vzlog_notls` [:549](lib/zlog.c#L549) → `vzlog_tls` [:589](lib/zlog.c#L589) → `vzlogx` [:855](lib/zlog.c#L855) → `zlog_msg_text` [:995](lib/zlog.c#L995) → `zlog_target_replace` [:1283](lib/zlog.c#L1283) → `zlog_init` [:1339](lib/zlog.c#L1339) |
| | 崩溃/回注两条支线 | `zlog_sigsafe` [:910](lib/zlog.c#L910)、`zlog_recirculate_live_msg` [:670](lib/zlog.c#L670) |

### 第 3 步：内置目标

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/zlog_targets.h](lib/zlog_targets.h) | [:45-104](lib/zlog_targets.h#L45) | `struct zlog_cfg_file` 与配置 API |
| [lib/zlog_targets.c](lib/zlog_targets.c) | 文件头 [:6-28](lib/zlog_targets.c#L6) | `zlog_fd` [:106](lib/zlog_targets.c#L106) → `zlog_file_cycle` [:279](lib/zlog_targets.c#L279) → `zlog_crashlog_sigsafe` [:433](lib/zlog_targets.c#L433) → `zlog_startup_init` [:549](lib/zlog_targets.c#L549) → `zlog_syslog` [:601](lib/zlog_targets.c#L601) |
| [lib/log_filter.c](lib/log_filter.c) | 文件头 [:8-44](lib/log_filter.c#L8) | `zlog_filter_add/del/dump` [:90](lib/log_filter.c#L90) → `search_buf` [:162](lib/log_filter.c#L162) → `zlog_filterfile_fd` [:185](lib/log_filter.c#L185) |

### 第 4 步：配置中枢

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/log_vty.c](lib/log_vty.c) | 全文 519 行 | 全局目标对象 [:67-103](lib/log_vty.c#L67) → 名字表 [:119-158](lib/log_vty.c#L119) → `zlog_rotate` [:234](lib/log_vty.c#L234) → `show logging` [:324](lib/log_vty.c#L324) → `log_vty_init` [:475](lib/log_vty.c#L475) |
| [lib/log_vty.h](lib/log_vty.h) | 全文 103 行 | 钩子声明 [:55-60](lib/log_vty.h#L55)、全局状态 extern [:79-89](lib/log_vty.h#L79) |
| [lib/log_nb.c](lib/log_nb.c) | 文件头 [:8-26](lib/log_nb.c#L8) | `log_stdout_apply_level` [:49](lib/log_nb.c#L49) → `set_log_file` [:74](lib/log_nb.c#L74) → `command_setup_early_logging` [:123](lib/log_nb.c#L123) → NB 回调 [:218](lib/log_nb.c#L218) 起 → `frr_logging_nb_info` [:785](lib/log_nb.c#L785) |
| [lib/log_cli.c](lib/log_cli.c) | 文件头 [:7-46](lib/log_cli.c#L7) | 命令处理函数 [:75](lib/log_cli.c#L75) 起 → `frr_logging_cli_info` [:663](lib/log_cli.c#L663) → `frr_logging_merge_cli_to_nb_info` [:691](lib/log_cli.c#L691) |

### 第 5 步：可插拔目标

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/zlog_5424.h](lib/zlog_5424.h) | [:49-141](lib/zlog_5424.h#L49) | 两个枚举 + 两个配置结构 |
| [lib/zlog_5424.c](lib/zlog_5424.c) | `struct zlt_5424` [:81](lib/zlog_5424.c#L81) | `zlog_5424_one` [:230](lib/zlog_5424.c#L230) → `zlog_3164_one` [:323](lib/zlog_5424.c#L323) → `zlog_journald_one` [:369](lib/zlog_5424.c#L369) → `zlog_5424` logfn [:511](lib/zlog_5424.c#L511) → `zlog_5424_sigsafe` [:721](lib/zlog_5424.c#L721) → `zlog_5424_cycle` [:851](lib/zlog_5424.c#L851) → `zlog_5424_open` [:1007](lib/zlog_5424.c#L1007) |
| [lib/zlog_5424_cli.c](lib/zlog_5424_cli.c) | `log_opts[]` [:75](lib/zlog_5424_cli.c#L75) | 各 destination 命令 [:302](lib/zlog_5424_cli.c#L302) 起 → `EXTLOG_NODE` [:1043](lib/zlog_5424_cli.c#L1043) → `log_5424_cmd_init` [:1076](lib/zlog_5424_cli.c#L1076) → 构造器 [:1115](lib/zlog_5424_cli.c#L1115) |

### 第 6 步：跨进程

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/zlog_live.h](lib/zlog_live.h) | [:37-112](lib/zlog_live.h#L37) | 线格式 + API |
| [lib/zlog_live.c](lib/zlog_live.c) | 文件头 [:6-30](lib/zlog_live.c#L6) | 状态机 [:49](lib/zlog_live.c#L49) → `zlog_live` [:87](lib/zlog_live.c#L87) → `out_err` [:184](lib/zlog_live.c#L184) → `zlog_live_open` [:245](lib/zlog_live.c#L245) → `close` [:305](lib/zlog_live.c#L305) → `disown` [:330](lib/zlog_live.c#L330) |
| [lib/zlog_recirculate.c](lib/zlog_recirculate.c) | 文件头 [:8-46](lib/zlog_recirculate.c#L8) | `zlog_recirculate_recv` [:65](lib/zlog_recirculate.c#L65) → `zlog_recirculate_subscribe` [:106](lib/zlog_recirculate.c#L106) |
| [lib/vty.c](lib/vty.c) | `vty_pass_fd` [:2504](lib/vty.c#L2504) | `terminal monitor` [:3541](lib/vty.c#L3541)、`no terminal monitor` [:3575](lib/vty.c#L3575)、`vty_close` 里的 `zlog_live_close` [:2744](lib/vty.c#L2744) |
| [vtysh/vtysh.c](vtysh/vtysh.c) | `vtysh_log_print` [:4392](vtysh/vtysh.c#L4392) | `vtysh_log_read` [:4454](vtysh/vtysh.c#L4454)、`vtysh_terminal_monitor` [:4532](vtysh/vtysh.c#L4532) |
| [ldpd/ldpd.c](ldpd/ldpd.c) | socketpair [:324](ldpd/ldpd.c#L324) / [:334](ldpd/ldpd.c#L334) | `start_child` [:362](ldpd/ldpd.c#L362)、`zlog_recirculate_subscribe` 调用点 [:372-373](ldpd/ldpd.c#L372) |
| [ldpd/lde.c](ldpd/lde.c) | [:129](ldpd/lde.c#L129) | `zlog_live_open_fd(&child_log, LOG_DEBUG, LDPD_FD_LOG)` |
| [ldpd/ldpe.c](ldpd/ldpe.c) | [:105](ldpd/ldpe.c#L105) | 同上 |

### 第 7 步：老系统辅助

| 文件 | 关键位置 | 看什么 |
|---|---|---|
| [lib/log.c](lib/log.c) | 文件头 [:7-27](lib/log.c#L7) | `lookup_msg` [:68](lib/log.c#L68) → `frr_timestamp` [:89](lib/log.c#L89) → `zlog_signal` [:160](lib/log.c#L160) → `zlog_hexdump` [:736](lib/log.c#L736) → `zlog_sanitize` [:805](lib/log.c#L805) |

### 附：构建与外部用例

| 位置 | 内容 |
|---|---|
| [lib/subdir.am:66-70](lib/subdir.am#L66) | 老代 5 个 .c |
| [lib/subdir.am:135-140](lib/subdir.am#L135) | 新代 6 个 .c |
| [lib/subdir.am:185-196](lib/subdir.am#L185) | 三个 CLI 文件的 clippy 扫描 |
| [lib/subdir.am:256-330](lib/subdir.am#L256) | 对外安装的头文件 |
| [tests/lib/fuzz_zlog.c](tests/lib/fuzz_zlog.c) | `zlog_5424.h` 的 fuzz 用例 |
| [watchfrr/watchfrr.c:22](watchfrr/watchfrr.c#L22) | 唯一直接 include `zlog_targets.h` 的非 lib 使用者 |

---

## §17 与官方 `doc/developer/*.rst` 的对应

官方 [`doc/developer/logging.rst`](doc/developer/logging.rst)（912 行）是本文最重要的对照物。章节映射：

| 本文 | logging.rst 章节 | 说明 |
|---|---|---|
| §0.4 全景图、§3 | `printfrr()`（[:13](doc/developer/logging.rst#L13)）与各格式符小节（[:100](doc/developer/logging.rst#L100) 起） | 官方从 `printfrr` 讲起，本文从调用链讲起，互补 |
| §6.1 | `AS-Safety`（[:89](doc/developer/logging.rst#L89)） | 本文补充了 `zlog_sigsafe` 的投递过滤条件 |
| §2.1、§3 | `Log levels`（[:610](doc/developer/logging.rst#L610)）与 `Errors and warnings`（[:613](doc/developer/logging.rst#L613)） | 官方强调 `flog_err` 必须带 EC，本文补充了「不带 EC 的 `zlog_*` 不产生前缀」 |
| §5 | `Thread-local buffering`（[:737](doc/developer/logging.rst#L737)） | 本文补充了四个强制 flush 条件与崩溃恢复的 `\0` 标记 |
| §4、§7、§8、§9 | `Log targets` / `Basic internals` / `Standard targets`（[:792](doc/developer/logging.rst#L792)、[:807](doc/developer/logging.rst#L807)、[:891](doc/developer/logging.rst#L891)） | 官方给出「**is not used for configuration**」这条关键边界；本文 §1.5 据此画出四段分层图 |
| §4.2 | `logfn()` 的 `note`（[:878](doc/developer/logging.rst#L878)） | 「数组**不**按目标预过滤」这条陷阱的官方出处 |
| §4.2 | `zlog_target_replace` 的 `warning`（[:865](doc/developer/logging.rst#L865)） | 「先加后删、可能重复」的官方出处 |
| §6.2 | `Standard targets` 末尾的 `todo`（[:907](doc/developer/logging.rst#L907)） | 启动期与配置期日志重叠的已知问题 |
| §1.4、§11 | 官方**未覆盖** | 双轨配置与 `frr_logging_merge_cli_to_nb_info` 在 `logging.rst` 里没有描述 —— 这是本文补上的部分 |

其他相关文档：

| 文档 | 关系 |
|---|---|
| [`doc/developer/rcu.rst`](doc/developer/rcu.rst)（278 行） | 目标链替换所依赖的 RCU 机制；本文只讲约定，原理见该文 |
| [`doc/developer/hooks.rst`](doc/developer/hooks.rst) | `zlog_init` / `zlog_fini` / `zlog_rotate` / `zlog_cli_show` 四个钩子 |
| [`doc/developer/memtypes.rst`](doc/developer/memtypes.rst) | `DEFINE_MTYPE_STATIC(LOG, ...)` 各类型 |
| [`doc/developer/library.rst`](doc/developer/library.rst)（21 行） | `libfrr` 总览，指向 logging.rst |
| [`doc/user/logging.rst`](doc/user/logging.rst) | 用户视角的日志配置说明（命令语义） |

---

## 结语：一句话回答「设计这两套的原因」

> **不是「设计了两套」，而是「一次未完成的重写留下了三层叠影」**：
>
> - **第一层（命名）**：`zlog_` 前缀被老系统占用过，新核心又沿用了这个名字，于是符号前缀不再能区分代际 —— 这是**历史包袱**；
> - **第二层（API）**：为了不动 347 个 `#include "log.h"` 的调用点，`log.h` 被改造成「老声明 + `#include` 新核心」的**兼容伞头**，老名字成为新引擎的薄封装 —— 这是**刻意的兼容策略**；
> - **第三层（配置）**：为了把日志配置纳入 YANG 北向（可被 NETCONF/mgmtd/配置库复用），新增了 `log_cli.c` + `log_nb.c`，但老的 VTY 诊断命令与运行时对象无法也不该被 YANG 化，于是**双轨并存**，靠 `frr_logging_merge_cli_to_nb_info()` 把「配置回调」与「回显回调」缝在同一张表上 —— 这是**演进中被保留的设计**。
>
> 三层里只有第二层是真正意义上的「两套实现」（老入口 vs 新内核），而它同样不是两套 —— 老的只是壳。真正并行的两套，只有**配置轨**。

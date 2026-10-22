# FRR 守护进程间通信（IPC）学习笔记

> 基于 `frr-10.6.1` 源码（分支 `study/bgpd-annotated`）整理。
> 所有行号均为本仓库当前代码的实测量结果，改动代码后需复核。
> 用途：归纳复习 FRR 各守护进程之间到底用什么 socket 通信、谁连谁、协议长什么样；
> 另附两节讲清**易被误当成通信通道**的运行期文件与启动期机制：
> 日志子系统 `/var/tmp/frr/<daemon>.<pid>/logbuf.<tid>`（§7）、
> 模块加载 `-M` / `frrmod_load()`（§8）。

---

## 0. 全局总览

### 0.1 三张"总线"表 —— 先记这个

FRR 10.x 里守护进程之间一共有 **三条互不相干** 的 socket 通道：

| # | 通道名 | 默认路径 | 内容格式 | 谁 listen | 谁 connect |
|---|--------|----------|----------|-----------|------------|
| 1 | **ZAPI / zserv** | `<runstatedir>/zserv.api` | 二进制 TLV（`stream`） | zebra | 各协议守护进程 |
| 2 | **VTY / vtysh** | `<runstatedir>/<daemon>.vty` | 明文命令 + `\0` + 4 字节返回码 | **每个守护进程自己** | vtysh |
| 3 | **mgmtd** | `mgmtd_fe.sock` / `mgmtd_be.sock` | 二进制 TLV | mgmtd（两条都 listen） | 各守护进程连 FE/BE |

> `<runstatedir>` 默认 `/var/run/frr`（编译期 `FRR_RUNSTATE_PATH`，见 [lib/libfrr.c:75](lib/libfrr.c#L75)）。
>
> 📌 别把 `/var/run/frr`（socket，**通道**）和 `/var/tmp/frr`（日志缓冲与 crashlog，**不是通道**）搞混 —— 后者见 **§7**。

### 0.2 全景图

```
                        ┌──────────────────────────────────────┐
                        │              vtysh                   │
                        │  (唯一的"用户键入命令"入口)          │
                        └───┬───────┬───────┬───────┬──────────┘
            通道2: *.vty    │       │       │       │
        ┌───────────────────┘       │       │       └──────────────┐
        ▼                           ▼       ▼                      ▼
  ┌───────────┐              ┌──────────┐ ┌──────────┐      ┌───────────┐
  │  bgpd     │              │  zebra   │ │  ospfd   │ .... │  mgmtd    │
  │ (listen   │              │ (listen  │ │          │      │ (listen   │
  │ bgpd.vty) │              │zebra.vty)│ │          │      │ mgmtd.vty)│
  └─┬───────┬─┘              └─┬──────┬─┘ └─┬──────┬─┘      └─┬───────┬─┘
    │       │                  │      │     │      │          │       │
    │       │  通道1: zserv.api │      │     │      │          │       │
    │       └──────────────────┼──────┘     │      │          │       │
    │    (bgpd → zebra ZAPI)   │            │      │          │       │
    │                          │◄───────────┘      │          │       │
    │                          │  (ospfd → zebra)  │          │       │
    │                                         ... │          │       │
    │      通道3: mgmtd_be.sock / mgmtd_fe.sock  │          │       │
    └──────────────────────────┼─────────────────┼──────────┘       │
                               └─────────────────┴──────────────────┘
                                  (配置平面：所有守护进程 ↔ mgmtd)
```

**记忆要点**：zebra 是 **通道 1 的服务端**；每个守护进程自己是 **通道 2 的服务端**；mgmtd 是 **通道 3 的服务端**。同一对进程（如 vtysh↔bgpd）走的是通道 2，跟 zebra 没有任何关系。

### 0.3 代码分层（谁负责哪块）

| 文件 | 职责 |
|------|------|
| [lib/libfrr.c](lib/libfrr.c) | 通用启动流程：命令行走参数 → 拼路径 → 加载 `-M` 模块 → `frr_run()` 起 VTY 服务 |
| [lib/module.c](lib/module.c) | 模块机制：`frrmod_init()` 登记自身、`frrmod_load()` dlopen `.so`（§8） |
| [lib/vty.c](lib/vty.c) | VTY 会话/服务端：**创建 `.vty` socket**、协议收发 |
| [lib/zclient.c](lib/zclient.c) | ZAPI **客户端**：连接 zebra、收发 ZAPI 消息 |
| [zebra/zserv.c](zebra/zserv.c) | ZAPI **服务端**：`zserv.api` 的 bind/listen/accept |
| [vtysh/vtysh.c](vtysh/vtysh.c) | vtysh 侧的客户端表与协议实现 |
| [lib/mgmt_be_client.c](lib/mgmt_be_client.c) / [lib/mgmt_fe_client.c](lib/mgmt_fe_client.c) | mgmtd 两条通道的客户端 |

---

## 1. Q1：FRR 下其它模块都使用 `zserv.api` 与 zebra 建立 unix socket 连接？

### 1.1 结论

**是的，但严格说是"想跟 zebra 打交道的守护进程"都通过 ZAPI（`zclient`）走同一条 Unix 域 socket**，而不是"所有模块"。zebra 是服务端，其余是客户端。

### 1.2 谁连、谁不连

| 进程 | 是否连 zserv.api | 说明 |
|------|------------------|------|
| **zebra** | ❌ | 它自己是服务端，[flags 含 `FRR_NO_ZCLIENT`](zebra/main.c#L310) |
| **watchfrr** | ❌ | [同样 `FRR_NO_ZCLIENT`](watchfrr/watchfrr.c#L1360)，只做进程守护 |
| **bgpd** | ✅ | [bgp_zebra.c:4842](bgpd/bgp_zebra.c#L4842) `zclient_init(bgp_zclient, ZEBRA_ROUTE_BGP, 0, &bgpd_privs)` |
| **ospfd / ospf6d** | ✅ | [ospf_zebra.c:2162](ospfd/ospf_zebra.c#L2162)、[ospf6_zebra.c:687](ospf6d/ospf6_zebra.c#L687) |
| **isisd / ripd / ripngd / eigrpd / babeld** | ✅ | 各自的 `*_zebra.c` |
| **pimd / ldpd / nhrpd / bfdd / pbrd / pathd / staticd / vrrpd / sharpd** | ✅ | bfdd 经 [ptm_adapter.c:873](bfdd/ptm_adapter.c#L873) |
| **mgmtd** | ❌ | 不 `zclient_init`，它用 `mgmtd_fe.sock` / `mgmtd_be.sock` |
| **vtysh** | ❌ | 只连各守护进程的 `.vty` |
| **ldpd 内部的 lde↔ldp** | ❌ | 走自己的 `imsg` socket，属于进程内通信，不是 ZAPI |

> `FRR_NO_ZCLIENT` 的实际作用只是 [不注册 / 拒绝 `-z` 选项](lib/libfrr.c#L639-L645)，不影响 `frr_init()` 里那句无条件的路径解析。

### 1.3 服务端侧（zebra 怎么 listen）

| 步骤 | 函数 | 位置 | 关键动作 |
|------|------|------|----------|
| 1 | [`zserv_open()`](zebra/zserv.c#L974-L1019) | `zebra/zserv.c:974` | `umask(0077)` → `socket(AF_UNIX, SOCK_STREAM)` → `unlink` 旧文件 → `bind()` |
| 2 | [`zserv_start()`](zebra/zserv.c#L1024-L1052) | `zebra/zserv.c:1024` | `listen(zsock, 5)`，`started_p` 保证多 netns 只做一次 |
| 3 | [`zserv_accept()`](zebra/zserv.c#L919) | `zebra/zserv.c:919` | `set_nonblocking` → `zserv_client_create()` |

调用点：[zebra/main.c:590](zebra/main.c#L590) `zserv_open(zserv_path)`、[zebra/main.c:455](zebra/main.c#L455) `zserv_start(zserv_path)`。

### 1.4 客户端侧（守护进程怎么 connect）

```
zclient_init()               lib/zclient.c:1113   ← daemon 的 *_zebra.c 里调用
   └─ zclient_event(ZCLIENT_SCHEDULE)
        └─ zclient_connect()  lib/zclient.c:1153  ← 事件回调（也用于重连）
             └─ zclient_start()          lib/zclient.c:1058
                  ├─ zclient_socket_connect()   lib/zclient.c:436   socket()+connect()
                  ├─ set_nonblocking()
                  ├─ zclient_send_hello()       通报自己的路由类型/实例
                  ├─ zebra_message_send(ZEBRA_INTERFACE_ADD)  拉初始接口列表
                  └─ zclient->zebra_connected() 回调
```

连接失败的重连节奏（[zclient.c:5767-5791](lib/zclient.c#L5767-L5791)）：

| 常量 | 值 | 含义 |
|------|----|------|
| `ZCLIENT_QUICK_RECONNECT` | 1 秒 | 失败次数 < 30 时的重连间隔 |
| `ZCLIENT_SWITCH_TO_SLOW` | 30 次 | 切换阈值 |
| `ZCLIENT_SLOW_RECONNECT` | 5 秒 | 失败次数 ≥ 30 后降速 |

### 1.5 路径是怎么定下来的

| 来源 | 结果 |
|------|------|
| 默认 | [libfrr.c:433](lib/libfrr.c#L433) `snprintf(frr_zclientpath, ZAPI_SOCK_NAME)`，宏见 [libfrr.h:46](lib/libfrr.h#L46) = `"%s/zserv.api", frr_runstatedir` |
| `-z` / `--socket` | 覆盖 `frr_zclientpath`，见 [lo_zclient / case 'z'](lib/libfrr.c#L241-L245)。**所有守护进程必须取值一致** |
| `-N <pathspace>` | `frr_runstatedir` 变成 `/var/run/frr/<pathspace>` → `<pathspace>/zserv.api`（[libfrr.c:600-608](lib/libfrr.c#L600-L608)） |
| `@tcp...` | TCP 形式的 zapi（端口 `ZEBRA_TCP_PORT` = 2600），**代码存在但被 `#if` 强制禁用**，见 [`frr_zclient_addr()`](lib/libfrr.c#L275-L283) |

路径解析统一走 [`frr_zclient_addr()`](lib/libfrr.c#L275)：`AF_UNIX` 则填 `sockaddr_un.sun_path`；`@tcp` 分支即使解析成功也 `return false`（安全考虑，仅留作调试）。

---

## 2. Q2：BGP 哪里创建了 VTY unix socket？vtysh 又是怎么跟它通信的？

### 2.1 关键结论

**bgpd 里没有任何一行 socket 创建代码**——`.vty` socket 是 libfrr / libvty 在公共启动流程里建的，文件名取自 `FRR_DAEMON_INFO` 宏里的 `execname`。

### 2.2 创建链路（bgpd 视角）

```
bgp_main.c:581   bgp_master_init(frr_init(), ...)
                    └─ lib/libfrr.c:927   vty_init(master, ...)        ← 只初始化 vty 子系统
bgp_main.c:635   frr_run(bm->master)
                    └─ lib/libfrr.c:1345  frr_vty_serv_start(false)
                         └─ lib/libfrr.c:1212  frr_vty_serv_start()    ← 在这里拼路径
                              └─ lib/vty.c:2669  vty_serv_start(addr, port, path)
                                   ├─ port != 0 → vty_serv_sock_addrinfo()   TCP VTY
                                   └─ #ifdef VTYSH → vty_serv_un(path)       ★ 就是这条
                                        └─ lib/vty.c:2260  vty_serv_un()
                                             unlink → umask(0007) → socket(AF_UNIX)
                                             → bind → listen(5) → chown(gid_vty)
                                             → 登记 vtyserv->vtysh = true
                                             → 挂 VTYSH_SERV 事件(vtysh_accept)
```

### 2.3 路径推导规则

路径拼装逻辑在 [libfrr.c:1216-1232](lib/libfrr.c#L1216-L1232)：

```c
if (di->instance)  snprintf(vtypath_default, ..., "%s/%s-%d.vty", dir, di->name, di->instance);
else               snprintf(vtypath_default, ..., "%s/%s.vty",    dir, di->name);
```

| 变量 | 取值来源 | bgpd 实际值 |
|------|----------|-------------|
| `di->name` | [`FRR_DAEMON_INFO(bgpd, BGP, ...)`](bgpd/bgp_main.c#L450) 宏展开 `.name = #execname`（[libfrr.h:227-235](lib/libfrr.h#L227-L235)） | `"bgpd"` |
| `dir` | `di->vty_sock_path`（`--vty_socket`）或 `frr_runstatedir` | `/var/run/frr` |
| `di->instance` | 由各 daemon 自己赋值；**只有 ospfd 用**（[ospf_main.c:219](ospfd/ospf_main.c#L219)） | 0（bgpd 无 `-n`，`-n` 是 `--no_kernel`） |

| 启动方式 | socket 路径 |
|----------|-------------|
| 默认 | `/var/run/frr/bgpd.vty` |
| `-N foo` | `/var/run/frr/foo/bgpd.vty` |
| `--vty_socket /tmp/x` | `/tmp/x/bgpd.vty` |
| （ospfd 专用）`-n 1` | `/var/run/frr/ospfd-1.vty` |

### 2.4 socket 的权限模型（为什么进 vtysh 不用密码）

[vty_serv_un()](lib/vty.c#L2260-L2334)：`umask(0007)` 建文件 → 权限 `0770`；随后 [`chown(path, -1, ids.gid_vty)`](lib/vty.c#L2314-L2326) 把属组改成 **`frrvty` 组**。

> 所以"能不能进 vtysh"由 **文件权限 + frrvty 组成员身份** 决定，不是密码。运行 vtysh 的用户必须在 frrvty 组里。

### 2.5 连接建立后：会话变成 `VTY_SHELL_SERV`

[`vtysh_accept()`](lib/vty.c#L2342-L2410)：

```
accept() → set_nonblocking() → 放大发送缓冲(VTY_SEND_BUF_MAX)
        → vty->fd = vty->wfd = sock
        → vty->type  = VTY_SHELL_SERV     ★ 这就是"我是 vtysh 会话"的标记
        → vty->node  = VIEW_NODE
        → 加入 vtysh_sessions 链表，挂 VTYSH_READ 事件
```

**TCP VTY 用的是 `vty_accept()`（`lib/vty.c:2117`）**，两条路径完全独立：读(`vtysh_read:2518` vs `vty_read`)、写(`vtysh_write:2656` vs `vty_flush:1832`)、flush(`vtysh_flush:2470`) 全部不同函数。

### 2.6 vtysh 侧：连哪个 socket

1. **静态客户端表** [vtysh/vtysh.c:118-142](vtysh/vtysh.c#L118-L142)：

   ```c
   struct vtysh_client vtysh_client[] = {
       {.name = "mgmtd",  .flag = VTYSH_MGMTD},
       {.name = "zebra",  .flag = VTYSH_ZEBRA},
       ...
       {.name = "bgpd",   .flag = VTYSH_BGPD},   /* path 为空 */
       ...
   };
   ```

2. **现拼路径**（[vtysh.c:4853-4855](vtysh/vtysh.c#L4853-L4855)）：

   ```c
   if (!vclient->path[0])
       snprintf(vclient->path, ..., "%s/%s.vty", vtydir, vclient->name);
   ```

3. `vtydir` 默认 = `frr_runstatedir`（[vtysh_main.c:446](vtysh/vtysh_main.c#L446)），可用：
   - `--vty_socket <dir>` 覆盖整个目录
   - `-N <pathspace>` → `vtydir += "/" + pathspace`（[vtysh_main.c:565-566](vtysh/vtysh_main.c#L565-L566)）
   - `-d <daemon>` 只连指定守护进程（[vtysh_main.c:495](vtysh/vtysh_main.c#L495)）

4. 启动时 `vtysh_connect_all()` 批量连；连不上的 fd 置 `VTYSH_WAS_ACTIVE`，下次用到该 daemon 时 [vtysh_reconnect()](vtysh/vtysh.c#L4921-L4935) 重连（重连后先补发 `enable` / `end`）。

5. **多实例特例**：只有 ospfd，vtysh 会 `opendir(vtydir)` 扫 `ospfd-*.vty` 动态插入客户端，上限 10 个（[vtysh_update_all_instances](vtysh/vtysh.c#L4967-L5005)）。

### 2.7 通信协议：明文命令 + 4 字节收尾

```
      vtysh                                            bgpd
        │                                               │
        │ ① write(fd, "show ip bgp summary\0", len+1)    │
        ├──────────────────────────────────────────────►│
        │                                               │ ② vtysh_read(): 逐字节累积
        │                                               │    遇 '\0' → vty_execute(vty)
        │                                               │ ③ 输出进 vty->obuf
        │                                               │ ④ buffer_put(header, 4)
        │                                               │    header = {0,0,0, RET}
        │ ⑤ "....文本....\n" + 00 00 00 + RET            │
        │◄──────────────────────────────────────────────┤    vtysh_flush()
        │  找 '\0' 判结束；末 4 字节取 ret               │
        │                                               │
```

| 方向 | 代码 | 说明 |
|------|------|------|
| 请求 | [vtysh.c:264](vtysh/vtysh.c#L264) `write(vclient->fd, line, strlen(line)+1)` | 命令行**明文**，末尾补一个 `\0` 作为分隔 |
| 收命令 | [vtysh_read()](lib/vty.c#L2518-L2653) | 逐字节累积到 `vty->buf`，遇 `\0` 即 `vty_execute()` |
| 发结果 | [lib/vty.c:2634-2635](lib/vty.c#L2634-L2635) `header[3]=ret; buffer_put(vty->obuf, header, 4)` | **3 个 NUL = 结束标记，第 4 字节 = 返回码** |
| 收结果 | [vtysh_client_run()](vtysh/vtysh.c#L243-L409)，判定在 [393-397 行](vtysh/vtysh.c#L393-L397) | `assert(!memcmp(buf, terminator, 3)); ret = buf[3];` |
| 输出分流 | `vtysh_client_execute()` vs [`vtysh_client_config()`](vtysh/vtysh.c#L488-L500) | 后者把每行喂给 `vtysh_config_parse_line`，用于汇总 `show running-config` |

**返回值 `RET`** 就是 CLI 命令的返回码：`CMD_SUCCESS` / `CMD_WARNING` / `CMD_ERR_*`。

### 2.8 两个必须记住的坑

| 坑 | 内容 | 依据 |
|----|------|------|
| **严格一问一答** | 一次 `read` 只承载一条命令。因为有 `pass_fd`、`CMD_SUSPEND`、等待 mgmtd 响应三条提前 `return` 的路径，**未被处理的字节会被直接丢弃**，所以 vtysh 不能流水线发多条再统一收 | [lib/vty.c:2525-2541](lib/vty.c#L2525-L2541) 上方英文长注释 |
| **返回码格式被硬编码** | watchfrr 硬编码了这 4 字节结果格式，改协议必须一起改 watchfrr | [lib/vty.c:2632](lib/vty.c#L2632) 注释 `warning: watchfrr hardcodes this result write` |

**彩蛋**：`terminal monitor` 的日志 fd 是经**同一条 socket 用 `SCM_RIGHTS` 传过去的**（[vty_pass_fd](lib/vty.c#L2504) → [vtysh_do_pass_fd](lib/vty.c#L2416-L2464)），传 fd 期间会暂停读——这也是"一问一答"的另一个原因。

### 2.9 与 mgmtd 的关系（FRR 10 常见误解）

vtysh 的**传输通道仍然是 `*.vty`**，没有变。变化在下游：

```
配置生效路径： vtysh → bgpd.vty → 命令 handler → northbound → mgmt_be_client → mgmtd_be.sock → mgmtd
```

- bgpd 命令经 YANG/northbound 提交到 mgmtd（[bgpd/bgp_nb.c](bgpd/bgp_nb.c)）。
- vtysh 表里第一项就是 mgmtd；`show running-config` 由 vtysh 从**各 daemon** 的 `.vty` **加** `mgmtd.vty` 收集后合并。
- mgmtd 因带 `FRR_MANUAL_VTY_START`，不由 `frr_run()` 自动起 VTY，而是自己调 [`frr_vty_serv_start(true)`](mgmtd/mgmt_vty_frontend.c#L831)。

---

## 3. 三条通道最终对照表

| 维度 | 通道 1：ZAPI | 通道 2：VTY | 通道 3：mgmtd |
|------|--------------|-------------|---------------|
| 路径 | `<runs>/zserv.api` | `<runs>/<daemon>.vty` | `<runs>/mgmtd_fe.sock`、`mgmtd_be.sock` |
| 创建者 | zebra（[zserv_open](zebra/zserv.c#L974)） | 每个守护进程（[vty_serv_un](lib/vty.c#L2260)） | mgmtd |
| 连接者 | 各协议守护进程（zclient） | vtysh | 各守护进程（mgmt_be/fe_client） |
| 载荷 | 二进制 ZAPI TLV | 明文 CLI + `\0` + 4 字节 RET | 二进制 TLV |
| 权限 | `umask(0077)` → 文件 0700 | `umask(0007)` → 0770 + chown `frrvty` 组 | — |
| 头文件/常量 | [libfrr.h:46](lib/libfrr.h#L46) `ZAPI_SOCK_NAME` | [libfrr.c:1226-1229](lib/libfrr.c#L1226-L1229) | [mgmt_defines.h:37-38](lib/mgmt_defines.h#L37-L38) |
| 客户端代码 | [lib/zclient.c](lib/zclient.c) | [vtysh/vtysh.c](vtysh/vtysh.c) | [lib/mgmt_be_client.c](lib/mgmt_be_client.c) / [lib/mgmt_fe_client.c](lib/mgmt_fe_client.c) |
| 服务端代码 | [zebra/zserv.c](zebra/zserv.c) | [lib/vty.c](lib/vty.c) | [mgmtd/mgmt_be_adapter.c](mgmtd/mgmt_be_adapter.c) / [mgmtd/mgmt_fe_adapter.c](mgmtd/mgmt_fe_adapter.c) |

---

## 4. 易踩坑清单（速查）

| # | 坑 | 结论 |
|---|----|------|
| 1 | 以为"所有守护进程都连 zebra" | zebra 自己、watchfrr、mgmtd、vtysh 都不连 |
| 2 | 以为 mgmtd 取代了 zserv.api | 两码事：ZAPI 是路由面，mgmtd 是配置面，并存 |
| 3 | 以为 `bgpd.vty` 是 bgpd 自己建的 | 在 `frr_run()` 里由 libfrr 统一建，文件名来自 `FRR_DAEMON_INFO` 的 execname |
| 4 | 改了 `-z` 只改了一个 daemon | `-z` 必须**所有守护进程取值一致**，否则互相看不见 |
| 5 | 给 `*.vty` 写多条命令 | 一次一条，多余的会被丢（见 2.8） |
| 6 | 想改 `{0,0,0,RET}` 格式 | watchfrr 硬编码，改了要一起改 |
| 7 | 找不到 socket | 检查 `<runs>` 是否因 `-N pathspace` 或 `--vty_socket` 变了；`ls /var/run/frr/*.vty` |
| 8 | 以普通用户跑 vtysh 报权限错 | socket 是 0770 + `frrvty` 组，用户需在组内 |
| 9 | 以为 `@tcp:2600` 能用 | 代码在，但被强制禁用（`frr_zclient_addr` 返回 false） |
| 10 | 找 ospfd 的 `.vty` 只有一个 | 多实例下是 `ospfd-<n>.vty`，vtysh 会扫目录 |

---

## 5. 代码索引（按调用顺序）

| 环节 | 文件:行 |
|------|---------|
| 通用启动、拼 zapi 路径 | [lib/libfrr.c:433](lib/libfrr.c#L433)、[:275](lib/libfrr.c#L275) |
| 通用启动、拼 vty 路径 | [lib/libfrr.c:1212-1234](lib/libfrr.c#L1212-L1234) |
| `frr_run()` 起 VTY 服务 | [lib/libfrr.c:1345](lib/libfrr.c#L1345) |
| VTY 服务总入口 | [lib/vty.c:2669](lib/vty.c#L2669) `vty_serv_start()` |
| 建 `.vty` socket | [lib/vty.c:2260](lib/vty.c#L2260) `vty_serv_un()` |
| accept vtysh 连接 | [lib/vty.c:2342](lib/vty.c#L2342) `vtysh_accept()` |
| 读命令 | [lib/vty.c:2518](lib/vty.c#L2518) `vtysh_read()` |
| 写结果（4 字节头） | [lib/vty.c:2634](lib/vty.c#L2634) |
| zebra 建 zserv socket | [zebra/zserv.c:974](zebra/zserv.c#L974)、[:1024](zebra/zserv.c#L1024) |
| zclient 连接 | [lib/zclient.c:436](lib/zclient.c#L436)、[:1058](lib/zclient.c#L1058)、[:1113](lib/zclient.c#L1113) |
| vtysh 客户端表 | [vtysh/vtysh.c:118](vtysh/vtysh.c#L118) |
| vtysh 建连接 | [vtysh/vtysh.c:4844](vtysh/vtysh.c#L4844) |
| vtysh 发命令 / 收结果 | [vtysh/vtysh.c:264](vtysh/vtysh.c#L264)、[:393](vtysh/vtysh.c#L393) |
| vtysh 解析 running-config | [vtysh/vtysh.c:488](vtysh/vtysh.c#L488) |
| bgpd 启动 | [bgpd/bgp_main.c:581](bgpd/bgp_main.c#L581)、[:635](bgpd/bgp_main.c#L635) |
| bgpd 连 zebra | [bgpd/bgp_zebra.c:4842](bgpd/bgp_zebra.c#L4842) |

---

## 6. 自测题（复习用）

<details>
<summary>点开看答案</summary>

1. **zebra 自己会去连 `zserv.api` 吗？为什么？**
   > 不会。zebra 是服务端，[zebra/main.c:310](zebra/main.c#L310) 带 `FRR_NO_ZCLIENT`。

2. **`bgpd.vty` 的"bgpd"这个字符串来自哪里？**
   > [`FRR_DAEMON_INFO(bgpd, BGP, ...)`](bgpd/bgp_main.c#L450) 宏展开的 `.name = #execname`，即第一个参数。

3. **vtysh 往 `bgpd.vty` 里写的是什么格式？**
   > 命令行明文 + 一个结尾 `\0`（[vtysh.c:264](vtysh/vtysh.c#L264)）。

4. **bgpd 怎么告诉 vtysh"这条命令执行完了"？**
   > 输出后追加 4 字节 `{0,0,0,RET}`（[lib/vty.c:2634](lib/vty.c#L2634)），前 3 个 NUL 是结束标记。

5. **为什么 vtysh 不能一口气发 10 条命令再统一收结果？**
   > `vtysh_read()` 在 `pass_fd` / `CMD_SUSPEND` / 等 mgmtd 响应时会提前 return，剩下的字节被丢弃（见 2.8）。

6. **运行 vtysh 需要什么权限？**
   > `.vty` socket 权限 0770 且属组被 chown 成 `frrvty`（[lib/vty.c:2314](lib/vty.c#L2314)），用户需在 frrvty 组内。

7. **FRR 10 里配置提交走哪条通道？**
   > `mgmtd_fe.sock` / `mgmtd_be.sock`（通道 3）。vtysh 只是把命令送进 daemon 的 `.vty`，daemon 再经 northbound 交给 mgmtd。

8. **`-N foo` 会影响哪些 socket 路径？**
   > `frr_runstatedir` 变成 `/var/run/frr/foo`，于是 `zserv.api` 和所有 `*.vty` 都移进该子目录（[libfrr.c:600-608](lib/libfrr.c#L600-L608)）。

9. **`/var/tmp/frr/bgpd.1703/logbuf.1704` 里的 1703 和 1704 分别是什么？**
   > 目录名里的 1703 是**进程 pid**，文件名里的 1704 是**线程 tid**（Linux 主线程 tid==pid，所以 1704 说明它是 bgpd 的工作线程之一）。

10. **为什么这个文件要用 mmap 而不是普通内存？**
    > 因为用 `MAP_SHARED` 映射到磁盘文件，进程崩溃时内核会把脏页回写，从而保住"还没 flush 出去的日志"（[lib/zlog.c:212-227](lib/zlog.c#L212-L227) 的注释）。

11. **正常退出后这个文件还在吗？**
    > 不在。线程退出时 `unlinkat` 删文件、进程退出时 `rmdir` 删目录（[lib/zlog.c:438-452](lib/zlog.c#L438-L452)、[:1416-1426](lib/zlog.c#L1416-L1426)）。看到残留说明进程非正常终止。

12. **这套缓冲和 syslog 是"两套并列的日志"吗？**
    > 不是。它是**缓冲层**（每线程一份、零锁、崩溃可回放），syslog 属于**目标层**（多个目标之一）。分层见 §7.1。

13. **日志目录和 `logbuf` 文件是同一时刻建的吗？**
    > 不是。目录在 `frr_init()` → `zlog_init()`（[libfrr.c:864](lib/libfrr.c#L864)）；主线程缓冲要等到 `frr_config_fork()` 末尾（[libfrr.c:1195](lib/libfrr.c#L1195)）。见 §7.3。

14. **配置了 `log syslog`，为什么启动最初几行没进 syslog？**
    > 因为读配置是**排进事件循环的事件**（[libfrr.c:1183](lib/libfrr.c#L1183)），要等 `frr_run()` 进入循环后才执行；在那之前目标里只有 stderr 和 stdout。

15. **为什么 `logbuf` 文件名用 tid 而不是线程名？**
    > [`fpt_run()`](lib/frr_pthread.c#L426-L440) 里建缓冲在 `frr_pthread_set_name()` **之前**，那时线程名还不存在（bgpd 的两个线程名是 `bgpd_io` / `bgpd_ka`，只出现在日志前缀和 ps 里）。

16. **`frrmod_init(di->module)` 是在加载模块吗？**
    > 不是。它在**登记"本 daemon 自己"**进 `frrmod_list`，并把 `execname` 设为 daemon 名（[lib/module.c:110-117](lib/module.c#L110-L117)）。加载 `-M` 指定模块的是紧随其后的 `frrmod_load()`。见 §8.4。

17. **`bgpd -M bmp` 凭什么能找到 `bgpd_bmp.so`？**
    > `frrmod_init()` 把 `execname` 设成 `"bgpd"`，`frrmod_load()` 先试 `<dir>/<execname>_<名字>.so` 再试 `<dir>/<名字>.so`（[lib/module.c:206-222](lib/module.c#L206-L222)）。见 §8.6。

18. **模块的 `init()` 里能 `install_element()` 吗？**
    > 不能。模块在 `cmd_init()`（[libfrr.c:920](lib/libfrr.c#L920)）**之前**加载，那时命令表还没建；CLI 安装必须放到 `frr_late_init` 钩子里（`bgpd_bmp` 就是范例）。见 §8.7。

</details>

---

## 7. FRR 日志子系统：从 `zlog_init()` 到 `logbuf.<tid>`（日志类问题集中归纳）

> ⚠️ 先划清界限：`/var/tmp/frr/<daemon>.<pid>/logbuf.<tid>` **不是 IPC 通道**，它是**线程级 mmap 日志缓冲**的物理载体。
> 之所以放进这份笔记，是因为它和 IPC socket 同属"守护进程运行期状态"，目录名 `frr` 又容易让人以为是同一条通道。

### 7.0 本节问题索引（速查）

| 问题 | 一句话答案 | 详见 |
|------|-----------|------|
| `logbuf.<tid>` 是一种日志文件吗？ | **不是**。它是缓冲层的内存载体，没有任何 target 往里写 | §7.1 |
| 路径各段分别是什么？ | `/var/tmp/frr`（硬编码）+ `<daemon>[-<inst>].<pid>` + `logbuf.<tid>` | §7.2 |
| 什么时候创建、每个阶段做什么？ | 库加载挂钩子 → `frr_init()` 建目录与目标 → `frr_config_fork()` 建主线程缓冲 → 工作线程各自建 → `frr_run()` 进主循环 → `frr_fini()` 收尾 | §7.3 |
| `hook_call(zlog_init, ...)` 调的是谁？ | ELF 构造器注册的 `zlt_init`（openlog）和 `log_vty_init`（stdout/filterfile 目标） | §7.4 / §7.11 |
| 日志怎么写进去、怎么出去？ | 正文渲染进 mmap 区 → 满足条件即 flush → 扇出给各 target | §7.5 |
| 崩溃后怎么读？ | `tr '\0' '\n' < logbuf.<tid>`，**空行之后是旧数据残留** | §7.6 |
| 正常退出后文件还在吗？ | 不在：线程退出 unlink 文件、进程退出 rmdir 目录 | §7.7 |
| 已经有 syslog 了，为什么还要这套？ | 二者不在同一层：这是**缓冲层**（保性能 + 崩溃黑匣子），syslog 是**目标层**（保持久化） | §7.1 |
| 为什么不用一个全局缓冲 + 锁？ | 多线程热点争抢；且崩溃时无法分辨消息来自哪个线程 | §7.1 |
| 文件名为什么用 tid 而不是线程名？ | 建缓冲时（`fpt_run` 开头）线程名还没被设置 | §7.1 |

### 7.1 设计总览：为什么非要这么复杂

#### ① 它不是"两套日志"，而是两层

```
┌─────────────────────── 缓冲层（每线程一份，无锁）───────────────────────┐
zlog() → vzlogx()  lib/zlog.c:855
   ├─ 有 TLS 缓冲 → vzlog_tls()   正文直接渲染进 mmap 区（8KB）
   │                               └─ mmap 背后是文件 /var/tmp/frr/<daemon>.<pid>/logbuf.<tid>
   └─ 无 TLS 缓冲 → vzlog_notls() 直接遍历目标逐个输出（每条一次 syscall）
                                    │
        flush 时机：64 条满 / 格式化走了 malloc / prio < LOG_INFO / 空间将尽
                                    ▼
┌─────────────────────────── 目标层（多路扇出）───────────────────────────┐
 syslog(journald) │ log file │ stdout │ 5424 socket │ zlog_live(terminal monitor) │ crashlog
```

**syslog 只是目标层里的一个**（而且它默认还是 disabled，见 §7.11）；`logbuf.<tid>` 属于缓冲层。两者不在同一层级，不构成竞争关系。

#### ② 缓冲层是为了什么（不能每条日志直接 `write()`/`syslog()` 吗？）

| 诉求 | 说明 |
|------|------|
| **省系统调用** | 每条日志一次 `write()`/`syslog()`；开 debug 后 FRR 每秒可能刷几千条 |
| **零锁** | bgpd 有 I/O + Keepalives 线程、zebra 有 dplane 线程。共用全局缓冲就得加锁，热点上争抢严重；**每线程一份 = 零锁** |
| **避免 malloc** | 格式化要一块内存写文本；固定 8KB mmap 区省掉每条消息的 malloc/free 与堆竞争 |
| **批处理** | 一次 flush 把多条消息交给目标，目标可一次 `writev` |
| **按优先级区别对待** | `prio < LOG_INFO`（WARNING/ERR/CRIT）**立即 flush**，INFO/DEBUG 才批处理 |

#### ③ 批处理的风险，正好被 mmap 化解

缓冲意味着"日志还没落地"。正常退出没事（`zlog_tls_buffer_fini` 会 flush），但**崩溃（SIGSEGV / assert / 被信号打死）会丢掉最后几条——恰恰是最想知道的那几条**。

于是把这块缓冲放到 `MAP_SHARED` 映射的文件上：

- 平时它就是普通内存，**几乎零额外成本**（反正都要 `mmap`，只是 flag 从 `MAP_ANONYMOUS` 换成 `MAP_SHARED`）
- 崩溃时内核自动回写脏页，人工 `tr '\0' '\n'` 就能读到
- 缓冲里那个多余的 `\0` 就是给这种情况准备的"有效数据到此为止"标记（[zlog.c:638-642](lib/zlog.c#L638-L642)）

> 一句话：**它是一块"顺便白捡了崩溃持久性"的内存，不是日志文件。**

#### ④ 反过来，为什么不能只用 `logbuf` 当日志？

| 维度 | syslog / log file | `logbuf.<tid>` |
|------|-------------------|----------------|
| 容量 | 无上限、可轮转、可归档 | **固定 8KB / 64 条** |
| 元数据 | 时间戳、pid/tid、优先级、xref 齐全 | **元数据不在 mmap 区**，崩溃时全丢，只剩正文（[zlog.c:224-226](lib/zlog.c#L224-L226) 明说 "we can live with that"） |
| 全局顺序 | 有 | **每线程一个文件**，跨线程无顺序 |
| 生命周期 | 持久 | 正常退出就 `unlink` + `rmdir` |
| 内容干净度 | 轮转，干净 | 只重置指针**不擦内容**，混杂更早的旧数据碎片 |
| 谁能读 | journald / rsyslog / 远程 syslog / 告警系统 | **只能人工事后分析**，树内没有一行读取代码 |
| 定位 | 运维、告警、审计（"日记"） | 崩溃黑匣子（"遗言"） |

#### ⑤ 另外两个"为什么这么设计"

| 问题 | 答案 |
|------|------|
| 为什么不用一个全局缓冲 + 锁？ | ① 多线程热点争抢；② 崩溃时无法分辨消息来自哪个线程——而 `logbuf.<tid>` 一文件一线程，天然带上了出处 |
| 文件名为什么用 `tid` 而不是线程名？ | [fpt_run()](lib/frr_pthread.c#L426-L440) 里 `zlog_tls_buffer_init()` 在 `frr_pthread_set_name()` **之前**调用——建缓冲时线程名还不存在；`gettid()` 是当时唯一可靠且便宜的标识（还有 thread_local 缓存避免重复 syscall） |

### 7.2 路径逐段拆解

```
/var/tmp/frr  /  bgpd.1703  /  logbuf.1704
└────┬────┘     └───┬───┘     └────┬────┘
   TMPBASEDIR    进程目录        线程缓冲文件
  (硬编码根)   <progname>[-<inst>].<pid>    logbuf.<tid>
```

| 段 | 来源 | 代码 | 例子中的值 |
|----|------|------|-----------|
| `/var/tmp/frr` | zlog.c 里**硬编码**的 `TMPBASEDIR` | [lib/zlog.c:1316](lib/zlog.c#L1316) | — |
| `bgpd.1703` | `zlog_init()` 按 `progname[-instance].pid` 拼 | [lib/zlog.c:1346-1354](lib/zlog.c#L1346-L1354) | `1703` = **bgpd 进程 pid** |
| `logbuf.1704` | `zlog_tls_buffer_init()` 按 `logbuf.<tid>` 拼 | [lib/zlog.c:378](lib/zlog.c#L378) | `1704` = **内核 TID（线程号）** |

**关键推断**：Linux 上**主线程的 tid == pid**，所以主线程的缓冲文件本该是 `logbuf.1703`。出现 `logbuf.1704` 说明这是 bgpd 起的**工作线程**——bgpd 有两条：

| 线程 | 创建位置 | 线程名 |
|------|----------|--------|
| BGP I/O thread | [bgpd/bgpd.c:10437](bgpd/bgpd.c#L10437) | `bgpd_io` |
| BGP Keepalives thread | [bgpd/bgpd.c:10438](bgpd/bgpd.c#L10438) | `bgpd_ka` |

> 所以**一个正常运行的 bgpd 通常有 3 个 `logbuf.*`**：`logbuf.<pid>`（主线程）+ 两个工作线程的。

### 7.3 初始化全过程：每个阶段谁做了什么

整个日志子系统是**分阶段、按需**长出来的，不是一次性配置好的。下面这张表就是"从进程被 exec 到进入事件循环，日志子系统每一步做了什么"。

| 阶段 | 触发点 | 代码 | 日志子系统做了什么 |
|------|--------|------|-------------------|
| **0. 库加载**（`main` 之前） | ELF 构造器 `.init_array` | [zlog_targets.c:549](lib/zlog_targets.c#L549) `_CONSTRUCTOR(450)`；[log_vty.c:501](lib/log_vty.c#L501) `_CONSTRUCTOR(475)`；[zlog_5424_cli.c:1115](lib/zlog_5424_cli.c#L1115) | 挂上 **stderr 目标**（只放行 ≥WARNING，fd=2）；`hook_register` 三组钩子：`zlog_init→zlt_init`、`zlog_fini→zlt_fini`、`zlog_init→log_vty_init`。**此时还没有任何目录、任何文件** |
| **1. 解析命令行** | `frr_preinit()` / `frr_getopt()` | [libfrr.c:395](lib/libfrr.c#L395)、[:744](lib/libfrr.c#L744) | 确定 `progname` / `instance` / `pathspace` / `pid_file` / `daemon_mode`。**日志目录仍未创建**——所以这个阶段的任何输出只能走 stderr |
| **2. 日志核心初始化** | `frr_init()` | [libfrr.c:829](lib/libfrr.c#L829)，核心调用在 [:864](lib/libfrr.c#L864) | `zlog_init()`：拼出 `zlog_tmpdir` → `mkdir /var/tmp/frr`(0700)+chown → `mkdir /var/tmp/frr/bgpd.<pid>`(0700) → `open(O_PATH)` 拿 `zlog_tmpdirfd` → **`hook_call(zlog_init,…)`** ⇒ `zlt_init`(`openlog`) + `log_vty_init`(stdout / filterfile 目标)。**目录在这里出现，但还没有 logbuf 文件** |
| **3. 启动切换** | `frr_config_fork()` | [libfrr.c:1172](lib/libfrr.c#L1172) | `hook_call(frr_late_init)` → **把"读配置"注册成事件**（[:1183](lib/libfrr.c#L1183)，不是立刻执行）→ `frr_daemonize()`（[:1188](lib/libfrr.c#L1188)）→ 写 pid 文件（[:1194](lib/libfrr.c#L1194)）→ **`zlog_tls_buffer_init()`（[:1195](lib/libfrr.c#L1195)）⇒ 主线程的 `logbuf.<tid>` 在这里诞生** |
| **4. 读取配置** | `frr_config_read_in()`（事件回调） | [libfrr.c:1128](lib/libfrr.c#L1128) | `log file` / `log syslog` / `log stdout` / `log monitor` 等命令在这里才**挂上真正的输出目标**。注意：它是被 `event_add_event` 排进事件循环的，**要等 `frr_run()` 进入循环后才跑**（[libfrr.c:1120-1127](lib/libfrr.c#L1120-L1127) 专门解释了为什么）；`--dryrun` 时内联执行后 `exit(0)` |
| **5. 工作线程启动** | `frr_pthread_run()` → `fpt_run()` | [lib/frr_pthread.c:426-440](lib/frr_pthread.c#L426-L440) | 每个线程开头 `zlog_tls_buffer_init()` ⇒ **又出现一个 `logbuf.<tid>`**；紧接着才 `frr_pthread_set_name()`。退出时 [:455](lib/frr_pthread.c#L455) `zlog_tls_buffer_fini()`（flush → munmap → unlink） |
| **6. 进入主循环** | `frr_run()` | [libfrr.c:1343](lib/libfrr.c#L1343) | 起 VTY socket（[:1349](lib/libfrr.c#L1349)，就是 §2 那个）→ `zlog_notice("… starting …")`（[:1355](lib/libfrr.c#L1355)）→ 守护模式下把 tty 重定向到 `/dev/null`（[:1380-1383](lib/libfrr.c#L1380-L1383)）→ **`zlog_startup_end()`（[:1392](lib/libfrr.c#L1392)）摘掉 stderr 目标、装 crashlog 目标** → 事件循环（[:1394](lib/libfrr.c#L1394)，每轮顺带 flush，[event.c:1262](lib/event.c#L1262)） |
| **7. 退出收尾** | `frr_fini()` | [libfrr.c:1409](lib/libfrr.c#L1409) | [:1432](lib/libfrr.c#L1432) `zlog_tls_buffer_fini()`（本线程缓冲 flush + 删文件）→ [:1454](lib/libfrr.c#L1454) `zlog_tmpdir_fini()`（关 dirfd + rmdir 目录）。**崩溃路径**改走 [sigevent.c:350](lib/sigevent.c#L350)：先 flush → 打 backtrace → 写 crashlog |

#### bgpd 的实际时间线（把上表落到具体函数）

```
exec bgpd
 ├─ 库构造器: 钩子挂上, stderr 目标可用
 ├─ bgp_main(): frr_preinit / frr_getopt                    ← 阶段 1
 ├─ bgp_master_init( frr_init(), ... )                      ← 阶段 2
 │     └─ libfrr.c:864  zlog_init()  ⇒ /var/tmp/frr/bgpd.<pid>/ 目录 + dirfd
 ├─ bgp_error_init / libagentx_init / bgp_vrf_init / bgp_init / bgp_if_init
 ├─ frr_config_fork()                                       ← 阶段 3
 │     └─ libfrr.c:1195 zlog_tls_buffer_init() ⇒ logbuf.<主线程 tid>
 ├─ bgp_gr_apply_running_config()
 ├─ bgp_pthreads_run()                                      ← 阶段 5
 │     ├─ fpt_run 线程 "BGP I/O thread"        ⇒ logbuf.<tid1>
 │     └─ fpt_run 线程 "BGP Keepalives thread" ⇒ logbuf.<tid2>
 └─ frr_run()                                               ← 阶段 6
       ├─ frr_vty_serv_start()  ⇒ /var/run/frr/bgpd.vty
       ├─ zlog_notice("BGP … starting")
       ├─ zlog_startup_end()    ⇒ 摘 stderr、装 crashlog
       ├─ 事件循环  ⟶ 此时才跑 frr_config_read_in       ← 阶段 4
       │              ⇒ log file / log syslog 目标在这里挂上
       └─ 退出 → frr_fini() → 缓冲 fini → 目录 rmdir
```

#### 从这张表能看出三件事

| # | 结论 | 依据 |
|---|------|------|
| 1 | **日志目标是一层层加的，不是一次配齐**：stderr（加载期）→ openlog/stdout（阶段 2）→ 配置指定的 file/syslog（阶段 4，事件循环内）→ crashlog（阶段 6） | 上表阶段 0/2/4/6 |
| 2 | **配置里写的 `log syslog` 生效得很晚**——要等事件循环第一次跑到 `frr_config_read_in`。所以"刚启动那几行"不会进 syslog／log file | [libfrr.c:1183](lib/libfrr.c#L1183) 是 `event_add_event` 而非直接调用 |
| 3 | **`logbuf.<tid>` 出现得比目录晚一个阶段**：目录在 `frr_init()`，主线程缓冲要等到 `frr_config_fork()` | 阶段 2 与阶段 3 是两次独立的调用 |

> ⚠️ 另一个由顺序推出的坑：`zlog_init()` 里的目录名用的是**当时的 `getpid()`**（[zlog.c:1353-1354](lib/zlog.c#L1353-L1354)），而 `frr_daemonize()` 的 `fork()` 发生在**之后**（[libfrr.c:1188](lib/libfrr.c#L1188)）。所以用 `-d` 守护化启动时，目录名里留的是**父进程的 pid**；用 systemd / watchfrr 前台方式启动（现代 FRR 的常规方式，不 fork）时，目录名才等于运行中的 daemon pid。

### 7.4 注册：进程级 + 线程级，两步

```
── 进程级（一次）────────────────────────────────────────────────
frr_init() → ... → zlog_init(di->progname, ...)        lib/libfrr.c:864
    ├─ mkdir /var/tmp/frr            (0700) + chown
    ├─ mkdir /var/tmp/frr/bgpd.<pid> (0700) + chown
    └─ zlog_tmpdirfd = open(tmpdir, O_PATH|O_RDONLY|O_CLOEXEC)   lib/zlog.c:1376
       失败 → 仅告警 "crashlog and per-thread log buffering unavailable!"

── 线程级（每个线程一次，幂等）──────────────────────────────────
主线程：  lib/libfrr.c:1195   ← frr_config_fork() 里，daemonize + 写 pid 文件之后
工作线程：lib/frr_pthread.c:432 ← fpt_run() 线程主函数开头（配套 fini 在 :455）
    └─ zlog_tls_buffer_init()                          lib/zlog.c:362
         ├─ openat(zlog_tmpdirfd, "logbuf.<tid>",
         │         O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, 0600)
         ├─ fchown(fd, zlog_uid, zlog_gid)
         ├─ posix_fallocate/ftruncate → 8 KB (TLS_LOG_BUF_SIZE)
         ├─ mmap(..., MAP_SHARED, fd, 0)   ★ 共享映射 → 崩溃时内核可回写
         └─ 失败则退化为 MAP_ANONYMOUS|MAP_PRIVATE（功能照常，只是崩溃后什么都没了）
```

| 参数 | 值 | 说明 |
|------|----|------|
| `TLS_LOG_BUF_SIZE` | 8192 | 每线程 8 KB（[lib/zlog.c:238](lib/zlog.c#L238)） |
| `TLS_LOG_MAXMSG` | 64 | 每线程最多暂存 64 条消息 |
| 文件权限 | 0600 + fchown 到运行身份 | 只有 frr 用户/组可读 |
| 启用条件 | `HAVE_OPENAT && HAVE_UNLINKAT` | 否则 `zlog_tls_buffer_init()` 是空函数（[lib/zlog.c:457-465](lib/zlog.c#L457-L465)） |

> `zlog_init()` 结尾还有一句 `hook_call(zlog_init, ...)`（[lib/zlog.c:1394](lib/zlog.c#L1394)）——**钩子是谁注册的、会做什么**见 **§7.11**。

### 7.5 使用：写 → 刷出

```
zlog_info(...) 等日志调用
   │
   ▼
vzlog_tls()                                   lib/zlog.c:589
   ├─ 快速探测：没有任何 target 想要这条消息 → 直接 return（零成本）
   ├─ 正文**直接渲染进 mmap 区**：msg->stackbuf = mmbuf + bufpos
   └─ 末尾额外写一个 '\0' 标记"当前有效结尾"
        （注释明说：崩溃时用这个双 \0 标示未写出日志的结束位置，lib/zlog.c:638-642）
   │
   ├── 立即 flush 的触发条件 ───────────────────────────────
   │     ① 缓冲已满 64 条
   │     ② 格式化时走了 malloc（无法长期持有可变参数引用）
   │     ③ prio < LOG_INFO（即 WARNING/ERR/CRIT，数值越小越严重）
   │     ④ 剩余空间 < 256 字节
   ▼
zlog_tls_buffer_flush()                       lib/zlog.c:520
   RCU 读锁内遍历 zlog_targets，逐个调用 zt->logfn()
        ├─ 文件 / syslog / journald / stdout 等常规目标
        ├─ crashlog 目标（zlog_targets.c:433，信号安全写）
        └─ zlog_live（terminal monitor 的广播）
   最后 bufpos = 0 / nmsgs = 0   ← 只重置指针，**不擦除内容**
```

其他会顺带 flush 的点：[lib/event.c:1262](lib/event.c#L1262)（事件循环每轮）、[lib/libfrr.c:1669](lib/libfrr.c#L1669)（收尾）、[lib/sigevent.c:350](lib/sigevent.c#L350)（**崩溃信号处理里先 flush 再打 backtrace**）。

**崩溃恢复的原理**：文件是 `MAP_SHARED`，进程被信号打死时内核会把脏页同步回磁盘，于是文件里保留了最后几条"还没来得及 flush 出去"的消息。

### 7.6 怎么读（官方口径）

树内**没有任何代码读取**这些文件——它纯粹是给人事后分析的。读法见 [doc/user/setup.rst:17-23](doc/user/setup.rst#L17-L23)：

```bash
tr '\0' '\n' < /var/tmp/frr/bgpd.1703/logbuf.1704
```

> 空行标记着"有效未写出数据"的结束；空行之后一般是乱码/旧日志，因为缓冲**不清零**。

### 7.7 生命周期与清理

| 时机 | 动作 | 代码 |
|------|------|------|
| 线程退出 | flush → munmap → **unlinkat 删掉 logbuf.<tid>** | [lib/zlog.c:438-452](lib/zlog.c#L438-L452) |
| 进程退出 | close(zlog_tmpdirfd) → **rmdir 整个目录** | [lib/zlog.c:1416-1426](lib/zlog.c#L1416-L1426) |

**所以：目录还在 = 进程还在跑，或者崩溃后没来得及清理。**

### 7.8 别混淆：同目录/同族的几个东西

| 路径 | 是什么 | 谁读 |
|------|--------|------|
| `/var/run/frr/bgpd.vty` | IPC：vtysh 通道（§2） | vtysh |
| `/var/run/frr/zserv.api` | IPC：ZAPI 通道（§1） | zclient |
| `/var/tmp/frr/<daemon>.<pid>/logbuf.<tid>` | **线程日志缓冲（非 IPC）** | 事后人工分析 |
| `/var/tmp/frr/<daemon>.<pid>/crashlog` | 崩溃 backtrace，懒创建 | 事后人工分析 |
| `terminal monitor` | `zlog_live` + `SCM_RIGHTS` 传 fd，**与 logbuf 无关** | vtysh 每次连接 |

### 7.9 坑清单

| # | 坑 | 结论 |
|---|----|------|
| 1 | 以为 `logbuf.1704` 的 1704 是 pid | 是 **tid**；pid 在目录名里（1703） |
| 2 | 以为 `/var/tmp/frr` 就是 `frr_libstatedir` | 默认值碰巧一样，但 zlog 的 `TMPBASEDIR` 是 [zlog.c:1316](lib/zlog.c#L1316) **硬编码**；`-N <pathspace>` 只改 `frr_libstatedir`（[libfrr.c:602](lib/libfrr.c#L602)），**日志目录不跟着变** |
| 3 | 直接 `cat logbuf.*` | 内容是 `\0` 分隔的，要 `tr '\0' '\n'`；空行之后是旧数据残留 |
| 4 | 以为"打了日志就一定在 syslog 里" | prio ≥ LOG_INFO 的消息可能长时间停在 8 KB 缓冲里，等 flush 时机 |
| 5 | 只找一个 logbuf 文件 | 主线程 + 每个 `frr_pthread` 各一个；bgpd 通常 3 个 |
| 6 | 想靠它做持久化日志 | 它只在崩溃时才有价值，`log file` / syslog 才是持久化手段 |
| 7 | 以为它受 `log immediate` 影响 | 会（`zlog_set_immediate` → 所有消息立即 flush），[lib/zlog.c:596](lib/zlog.c#L596) |
| 8 | 以为配置里的 `log syslog` / `log file` 一启动就生效 | **不**。它要等事件循环跑到 `frr_config_read_in`（[libfrr.c:1183](lib/libfrr.c#L1183) 是排事件，不是直接调用）；启动最初几行只可能到 stderr |
| 9 | 拿目录名当运行中 daemon 的 pid | 用 `-d` 守护化启动时，`fork()` 晚于 `zlog_init()`，目录名留的是**父进程** pid；systemd / watchfrr 前台启动才一致 |
| 10 | 以为 `logbuf` 在 `frr_init()` 就建好了 | 目录在 `frr_init()`（[:864](lib/libfrr.c#L864)），**缓冲文件**要等到 `frr_config_fork()`（[:1195](lib/libfrr.c#L1195)） |

### 7.10 代码索引

**按初始化阶段排（配合 §7.3 的表格读）**

| 阶段 | 环节 | 文件:行 |
|------|------|---------|
| 0 | stderr 目标 + 钩子注册（构造器 450） | [lib/zlog_targets.c:549-561](lib/zlog_targets.c#L549-L561) |
| 0 | vty 目标钩子注册（构造器 475） | [lib/log_vty.c:501-503](lib/log_vty.c#L501-L503) |
| 2 | `frr_init()` 里调用 `zlog_init()` | [lib/libfrr.c:864](lib/libfrr.c#L864) |
| 2 | 进程目录创建 + 打开 dirfd + `hook_call` | [lib/zlog.c:1339-1400](lib/zlog.c#L1339-L1400) |
| 2 | 临时目录根（硬编码 `TMPBASEDIR`） | [lib/zlog.c:1316](lib/zlog.c#L1316) |
| 2 | 钩子定义 | [lib/zlog.c:95-97](lib/zlog.c#L95-L97)（`DEFINE_HOOK`）/ [lib/zlog.h:412-414](lib/zlog.h#L412-L414)（`DECLARE_HOOK`） |
| 3 | 主线程缓冲注册点 | [lib/libfrr.c:1195](lib/libfrr.c#L1195) |
| 4 | 配置读取（日志目标此时才挂） | [lib/libfrr.c:1128](lib/libfrr.c#L1128)，排事件在 [:1183](lib/libfrr.c#L1183) |
| 5 | 工作线程注册 / 注销点 | [lib/frr_pthread.c:432](lib/frr_pthread.c#L432)、[:455](lib/frr_pthread.c#L455) |
| 6 | `zlog_startup_end()`（摘 stderr、装 crashlog） | [lib/zlog_targets.c:568-583](lib/zlog_targets.c#L568-L583) |
| 7 | 缓冲 fini / 目录 rmdir | [lib/libfrr.c:1432](lib/libfrr.c#L1432)、[:1454](lib/libfrr.c#L1454) |

**缓冲与目标本身**

| 环节 | 文件:行 |
|------|---------|
| 线程缓冲注册 | [lib/zlog.c:362-432](lib/zlog.c#L362-L432) |
| 线程缓冲注销 | [lib/zlog.c:438-452](lib/zlog.c#L438-L452) |
| 写入（渲染进 mmap） | [lib/zlog.c:589-656](lib/zlog.c#L589-L656) |
| 刷出（遍历 targets） | [lib/zlog.c:520-541](lib/zlog.c#L520-L541) |
| 崩溃前 flush | [lib/sigevent.c:350](lib/sigevent.c#L350) |
| crashlog 目标 | [lib/zlog_targets.c:433-473](lib/zlog_targets.c#L433-L473) |
| 用户文档（读法） | [doc/user/setup.rst:9-23](doc/user/setup.rst#L9-L23) |
| bgpd 的两个线程 | [bgpd/bgpd.c:10437-10445](bgpd/bgpd.c#L10437-L10445) |

### 7.11 附录：`hook_call(zlog_init, ...)` 到底调了谁（FRR hook 框架速览）

问题来源：[lib/zlog.c:1394](lib/zlog.c#L1394) 那句

```c
hook_call(zlog_init, progname, protoname, instance, uid, gid);
```

#### ① 钩子本身定义在哪

| 角色 | 宏 | 位置 |
|------|----|------|
| 声明（头文件） | `DECLARE_HOOK(zlog_init, ...)` | [lib/zlog.h:412-414](lib/zlog.h#L412-L414) |
| 定义（源文件） | `DEFINE_HOOK(zlog_init, ...)` | [lib/zlog.c:95-97](lib/zlog.c#L95-L97) |

`DEFINE_HOOK` 展开后生成两样东西（[lib/hook.h:324-348](lib/hook.h#L324-L348)）：

```c
struct hook _hook_zlog_init = {.name = "zlog_init", .entries = NULL, .reverse = false};
static int hook_call_zlog_init(const char *progname, ...)   /* 仅本 .c 文件可见 */
{
    int hooksum = 0;
    for (he = _hook_zlog_init.entries; he; he = he->next)
        hooksum += ((int(*)(const char*,...))he->hookfn)(progname, ...);  /* 依次调用 */
    return hooksum;                                          /* 返回值是各回调之和 */
}
```

`DECLARE_HOOK` 则额外生成两个静态内联的**类型检查**函数（`_hook_typecheck_zlog_init`），让 `hook_register` 在**编译期**就能发现回调签名写错。

#### ② 订阅者只有两个（全树 grep 确认）

| 订阅者 | 注册处 | 注册时机 | 回调干什么 |
|--------|--------|----------|-----------|
| `zlt_init` | [lib/zlog_targets.c:559](lib/zlog_targets.c#L559) | `_CONSTRUCTOR(450)` 的 `zlog_startup_init()`，**库加载时** | 就一行：[`openlog(progname, LOG_CONS\|LOG_NDELAY\|LOG_PID, LOG_DAEMON)`](lib/zlog_targets.c#L520-L525)——**只是打开 syslog 通道句柄（设好 ident）**，并不等于把日志写进 syslog，见下方注 |
| `log_vty_init` | [lib/log_vty.c:503](lib/log_vty.c#L503) | `_CONSTRUCTOR(475)` 的 `log_vty_preinit()`，**库加载时** | [记录 progname/protoname、**再注册 `zlog_fini → log_vty_fini`**、打开前缀 EC/XID、初始化 filterfile 目标、按 stdout 是否 journald 决定 stdout 是 5424 目标还是普通文件目标](lib/log_vty.c#L478-L508) |

> **注：`openlog()` ≠ 日志会写进 syslog。**真正决定"写不写 syslog"的是一个独立目标里的 `syslog_prio_min`，它默认是 `ZLOG_DISABLED`（[lib/zlog_targets.c:695](lib/zlog_targets.c#L695)），只有配置里出现 `log syslog [level]` 才会被设成有效值从而让该目标工作（[`zlog_syslog_set_prio_min()`，:745-769](lib/zlog_targets.c#L745-L769)）。
> 也就是说：**代码层面的默认是"不进 syslog"，是发行包自带配置 [tools/etc/frr/frr.conf:10](tools/etc/frr/frr.conf#L10) 里那句 `log syslog informational` 把它打开的**。`zlt_init` 只是把通道句柄准备好，省掉首次写日志时的 `openlog` 开销。
>
> `_CONSTRUCTOR(n)` 是 GCC 的库加载期构造器：**不需要任何人显式调用**，libfrr 被加载时这两个函数就跑了，于是钩子被挂上。

#### ③ 调用顺序与触发点

```
库加载期（构造器，按优先级 450 → 475 执行）
  zlog_startup_init()  ── hook_register(zlog_init, zlt_init)
  log_vty_preinit()    ── hook_register(zlog_init, log_vty_init)
                              │
                              ▼
            _hook_zlog_init.entries = [ zlt_init ] → [ log_vty_init ]
                              │
                              ▼
守护进程运行期： frr_init() → … → zlog_init()                lib/zlog.c:1339
     └─ 成功路径 hook_call(zlog_init, …)   lib/zlog.c:1394  ──┐
     └─ 失败路径 out_warn 也会 hook_call   lib/zlog.c:1399  ──┤
                                                             ▼
                       hook_call_zlog_init(...)
                         ├─ zlt_init()      → openlog()      （syslog 句柄就绪）
                         └─ log_vty_init()  → stdout/filterfile 目标就绪
```

两个关键结论：

1. **顺序**：`hook_register` 默认优先级是 `HOOK_DEFAULT_PRIORITY` = 1000（[lib/hook.h:169](lib/hook.h#L169)），而 `_hook_register` 找插入点用 `priority >= priority`（[lib/hook.c:90-95](lib/hook.c#L90-L95)），所以**同优先级保持注册先后**；构造器 450 < 475 ⇒ `zlt_init` 先跑。
2. **失败路径也会调**（[lib/zlog.c:1397-1399](lib/zlog.c#L1397-L1399)）：tmpdir 建失败只打印 "crashlog and per-thread log buffering unavailable!"，但**日志目标照挂**——这正是它用 `goto out_warn` 而不是 `return` 的原因。

#### ④ 为什么要绕这么一圈（设计意图）

`lib/zlog.c` 只提供"日志核心"，具体的输出目标分散在 `zlog_targets.c`（syslog/fd）、`log_vty.c`（stdout/filterfile）、`zlog_5424*.c`（journald/socket）里。若让 zlog.c 直接调它们，就会形成**反向依赖**（核心依赖插件）。用 hook + 构造器自注册后：

- `zlog.c` 不认识任何一个目标；
- 目标文件自己"报名"；
- 加新目标不用改 `zlog.c`。

> 顺带两个同类钩子：`zlog_fini` 用 `DEFINE_KOOH`（`reverse = true`，[lib/hook.h:357](lib/hook.h#L357)）——注册时按优先级**降序**插入，于是**后注册的先清理**；`zlog_aux_init` 给 `zlog_aux_init()` 这类辅助程序（工具/测试）用，参数是 `prefix/prio_min`。

#### ⑤ hook 框架速查（记住这四件套）

| 名字 | 放哪 | 作用 |
|------|------|------|
| `DECLARE_HOOK(name, arglist, passlist)` | 头文件 | 声明 `_hook_name` + 生成类型检查函数 |
| `DEFINE_HOOK(name, …)` | .c | 定义 `_hook_name` + 生成 `hook_call_name()` |
| `hook_register(name, func[, arg][, prio])` | 任意 .c | 把回调按优先级插入订阅链表 |
| `hook_call(name, …)` | **仅定义该钩子的 .c** | 遍历链表依次调用，返回各回调返回值之和 |

`hook_call` 生成的函数是 **static**，所以只能在定义钩子的那个 `.c` 里触发——想从别处触发就得再加一个包装函数。

---

## 8. 模块加载（`-M`）：`frrmod_init()` + `frrmod_load()` 详解

引子 —— `frr_init()` 里的这两行（[lib/libfrr.c:896-903](lib/libfrr.c#L896-L903)）：

```c
frrmod_init(di->module);
while (modules) {
	modules = (oc = modules)->next;
	module = frrmod_load(oc->arg, dir, _err_print, __func__);
	if (!module)
		exit(1);
	XFREE(MTYPE_TMP, oc);
}
```

> ⚠️ 最容易看错的一点：**第一行不是"加载模块"**，而是把 **daemon 自己**登记进模块链表；真正加载 `-M` 指定的 `.so` 是第二行的循环。

### 8.0 本节问题索引（速查）

| 问题 | 一句话答案 | 详见 |
|------|-----------|------|
| FRR 为什么要有 `-M` / dlopen 模块机制？ | 把 SNMP/FPM/RPKI 这类**重第三方依赖**从编译期耦合降级成运行期按需加载 | §8.1 |
| `frrmod_init(di->module)` 是加载模块吗？ | **不是**。它把 daemon 自己挂进 `frrmod_list` 并设置 `execname`（= daemon 名） | §8.4 |
| `di->module` / `THIS_MODULE` 从哪来？ | `FRR_DAEMON_INFO` 宏里的 `FRR_COREMOD_SETUP`，靠弱符号回退到 `frrmod_default` | §8.3 |
| `-M bmp` 是怎么找到 `bgpd_bmp.so` 的？ | `execname` 前缀拼接：先试 `<dir>/<daemon>_<名字>.so`，再试 `<dir>/<名字>.so`，最后当路径 | §8.6 |
| `-M` 参数存在哪里？什么时候消费？ | 解析命令行时攒进 `modules` 链表，`frr_init()` 里**消费即销毁** | §8.5 |
| 为什么模块的 `init()` 里不能装 CLI？ | 那时 `cmd_init()` 还没跑，`cmdvec` 还不存在；CLI 要推迟到 `frr_late_init` 钩子 | §8.7 |
| 加载失败会怎样？ | `exit(1)`，不做降级——`-M` 是用户显式意图 | §8.6 |
| 运行期能卸载模块吗？ | **不能**。`frrmod_unload()` 至今 `#if 0`，`frrmod_terminate()` 只释放字符串 | §8.9 |
| 怎么确认某个模块加载了？ | `show modules`（打印名字/版本/描述/来源路径） | §8.9 |
| 本版本有哪些可加载模块？ | 见 §8.12 的清单表（含"模块名 ≠ 文件名"的坑） | §8.12 |

### 8.1 设计目的：为解决什么而设计

FRR 有一批可选功能必须依赖第三方库：SNMP（net-snmp）、FPM（protobuf）、RPKI（rtrlib）、PCEP、sysrepo、gRPC、DPDK……

若把它们静态链接进 daemon，后果是**每个发行版为了编译 `bgpd` 都得先装 net-snmp + protobuf**，包依赖被无谓放大。

`-M` + `dlopen` 做的就是：**把"可选依赖"从编译期耦合降级为运行期按需加载**。

| 诉求 | 机制怎么满足 | 代价（官方文档明列） |
|------|-------------|--------------------|
| 可选依赖不污染主包 | 模块编成独立 `.so`，可单独打包（Debian 的 `frr-snmp` / `frr-fpm` 即如此） | **没有稳定 API/ABI**，模块必须与 FRR 版本严格匹配 |
| 用户按需开功能 | `bgpd -M rpki`；不加就完全不加载 | **只能启动期加载**，运行期不能 load/unload（[doc/developer/modules.rst:12-16](doc/developer/modules.rst#L12-L16)） |
| 一套代码、一个 daemon 二进制 | 模块用 `FRR_MODULE_SETUP` 自描述，daemon 侧零改动 | 模块崩 = daemon 崩；**不构成许可证边界**（仍受 GPL 约束） |

> 一句话：**`-M` 是 FRR 的"插件入口"，不是"热更新入口"。** 这个定位直接决定了后面那些别扭的时序约定。

官方手册对 `-M` 的权威描述在 [doc/manpages/common-options.rst:148-162](doc/manpages/common-options.rst#L148-L162)，实践例子：`-M rpki`（[doc/user/rpki.rst:73-76](doc/user/rpki.rst#L73-L76)）、`-M bmp`（[doc/user/bmp.rst:66](doc/user/bmp.rst#L66)）、`-M grpc:PORT`（[doc/user/grpc.rst:57-58](doc/user/grpc.rst#L57-L58)）、`-M dplane_fpm_nl`（[doc/user/zebra.rst:1343](doc/user/zebra.rst#L1343)）。

### 8.2 这两行在启动流程里的位置（四重时序约束）

```c
829  struct event_loop *frr_init(void)
...
838      dir = di->module_path ? di->module_path : frr_moduledir;   /* --moduledir 或编译期 MODULE_PATH */
...
896      frrmod_init(di->module);        /* ① daemon 自己入链 + 设 execname        */
897      while (modules) { ... }         /* ② 逐个 dlopen -M 指定的模块             */
905      zprivs_init(di->privs);         /* ← 模块必须已经加载完（还在特权身份下）   */
907      master = event_master_create(NULL);
909      hook_call(frr_early_init, master);   /* ← 模块注册的该钩子在这里触发         */
920      cmd_init(1);                    /* ← 命令表(cmdvec)在这里才建起来          */
927      vty_init(master, di->log_always);
940      nb_init(master, di->yang_modules, di->n_yang_modules, true, ...);
```

| # | 约束 | 为什么 | 依据 |
|---|------|--------|------|
| 1 | 必须在 `zprivs_init()`（降权）**之前** | 模块目录可能只有 root 可读；降权后 dlopen 可能失败。FRR 的一贯风格是"需要权限的事都在降权前做完" | [:905](lib/libfrr.c#L905) |
| 2 | 必须在 `hook_call(frr_early_init)` **之前** | 否则模块注册的 `frr_early_init` 回调赶不上这一次调用 | [:909](lib/libfrr.c#L909) |
| 3 | 必须在 `cmd_init()` **之前**加载，但**之后**才能装 CLI | `cmdvec` 在 `cmd_init()` 里才建；模块 `init()` 里不能 `install_element()` → 必须推迟到 `frr_late_init` | [:920](lib/libfrr.c#L920) / §8.7 |
| 4 | 必须在 `vty_init()` / `nb_init()` 之前 | 模块要注册 CLI 节点与 YANG 模块 | [:927](lib/libfrr.c#L927)、[:940](lib/libfrr.c#L940) |

### 8.3 `di->module` / `THIS_MODULE` / `FRR_COREMOD_SETUP` 的链接器关系

```
FRR_DAEMON_INFO(bgpd, BGP, ...)                       lib/libfrr.h:227-235
   ├─ static struct frr_daemon_info bgpd_di = { .name = "bgpd",
   │                                            .module = THIS_MODULE, ... };
   └─ FRR_COREMOD_SETUP(.name = "bgpd", ...)          ← 强定义 _frrmod_this_module
                                                        （名字取自 #execname）
若某编译单元没有 FRR_COREMOD_SETUP：
   lib/module.c:91   #pragma weak _frrmod_this_module = frrmod_default
   → THIS_MODULE 退化为 libfrr 自己的 frrmod_default
```

| 符号 | 定义 | 含义 |
|------|------|------|
| `_frrmod_info` | [lib/module.h:135](lib/module.h#L135) `FRR_COREMOD_SETUP` | 静态元信息：name / version / description / init |
| `_frrmod_this_module` | 同上（`DSO_LOCAL`，仅本链接单元可见） | 本单元的运行时结构 |
| `THIS_MODULE` | [lib/module.h:126](lib/module.h#L126) | `&_frrmod_this_module.r` |
| `frrmod_default` | [lib/module.c:69-75](lib/module.c#L69-L75) | libfrr 自身的模块，`finished_loading = 1` |
| `frr_module` | [lib/module.h:149](lib/module.h#L149) `FRR_MODULE_SETUP` | **可加载模块**额外导出的符号，供 `dlsym` 查找 |

所以"daemon 也是一个模块"这件事在 FRR 里是**字面成立**的：`show modules` 的第一行永远能看到 `libfrr`，第二行是 daemon 自己。

### 8.4 `frrmod_init()`：把 daemon 自己登记成链表节点

[lib/module.c:110-117](lib/module.c#L110-L117)，只有三件事：

```c
void frrmod_init(struct frrmod_runtime *modinfo)
{
	modinfo->finished_loading = true;   /* ① 自己人，标记已加载，防 -M 重复加载 */
	*frrmod_last = modinfo;             /* ② 尾插：frrmod_list = libfrr → bgpd  */
	frrmod_last = &modinfo->next;
	execname = modinfo->info->name;     /* ③ ★ "bgpd"，后面拼模块文件名要用      */
}
```

- ① 是**幂等保护**：`frrmod_load()` 检查 `finished_loading`（[:255](lib/module.c#L255)），所以 `-M bgpd` 这类自加载会被拒绝。
- ② 用 `frrmod_last`（[lib/module.c:102](lib/module.c#L102)）做尾插，O(1)；链表头是 `frrmod_default`。
- ③ **这是 `-M bmp` 能解析成 `bgpd_bmp.so` 的唯一原因**。若没有这一行，`execname` 为 NULL，就只剩"按名字找"和"当路径用"两种方式了。

### 8.5 `while (modules)`：`-M` 参数怎么攒、怎么消费

**攒**：`frr_opt()` 的 `case 'M'`（[lib/libfrr.c:538-544](lib/libfrr.c#L538-L544)），短选项串是 `"hvdM:F:N:o:K::"`（[lib/libfrr.c:176](lib/libfrr.c#L176)，`M:` 表示带参数）：

```c
case 'M':
	oc = XMALLOC(MTYPE_TMP, sizeof(*oc));
	oc->arg = optarg;      /* ← 不拷贝！直接指向 argv 里的字符串 */
	oc->next = NULL;
	*modnext = oc;         /* 尾插到 modules 链表（libfrr.c:511 定义） */
	modnext = &oc->next;
	break;
```

**消费**：`modules = (oc = modules)->next;` —— 一边前进一边把链表头指针推走，走完一趟 `modules` 就成 NULL。

| 细节 | 说明 |
|------|------|
| 支持多个 `-M` | 按命令行顺序加载（手册：*Modules are loaded in the order as listed on the command line*） |
| 内存所有权 | `oc->arg` 借用 `argv`，只有节点本身要 `XFREE(MTYPE_TMP, oc)` —— 无泄漏、无重复释放 |
| 消费即销毁 | 走完链表头即 NULL，`frr_init()` 天然不可重入；与同文件的 `log_args_pop()`（[:867](lib/libfrr.c#L867)）同一风格 |
| 存的是原始字符串 | `"bmp"` 或 `"grpc:50051"`，解析（拆冒号）在 `frrmod_load()` 里做 |

### 8.6 `frrmod_load()` 逐步详解

入口 [lib/module.c:143](lib/module.c#L143)：

```c
struct frrmod_runtime *frrmod_load(const char *spec, const char *dir,
                                   void (*pFerrlog)(const void *, const char *),
                                   const void *pErrlogCookie);
```

调用处传的是 `dir`（`--moduledir` 或 `frr_moduledir`，[:838](lib/libfrr.c#L838)）与错误回调 `_err_print`（[lib/libfrr.c:814-819](lib/libfrr.c#L814-L819)，把错误打成 `frr_init: <msg>` 到 stderr）。

#### ① 解析 `name:args`（[:201-204](lib/module.c#L201-L204)）

```c
snprintf(name, sizeof(name), "%s", spec);
args = strchr(name, ':');
if (args) *args++ = '\0';
```

冒号后的内容存进 `THIS_MODULE->load_args`（[:252](lib/module.c#L252)）供模块自解析。手册：*The meaning of options is specific to the module being loaded. Most modules currently ignore it.*（实际用例：`-M grpc:50051`）

#### ② 三次 `dlopen` 尝试（`-M` 名字能"简写"的根源）

| 顺序 | 拼接路径 | 条件 | 例：`bgpd -M bmp`（execname=`bgpd`，dir=`/usr/lib/frr/modules`） |
|------|---------|------|------------------------------------------------------------------|
| 1 | `<dir>/<execname>_<name>.so` | `execname` 已设 **且** spec 不含 `/` | `/usr/lib/frr/modules/bgpd_bmp.so` ✅ **命中** |
| 2 | `<dir>/<name>.so` | 上一步失败 | （写 `-M bgpd_bmp` 时这一步命中） |
| 3 | `<name>` 原样当路径 | 前两步都失败 | `-M .libs/bgpd_bmp.so`（**开发期 / snapcraft 常用**） |

代码在 [:206-230](lib/module.c#L206-L230)。手册原文（[common-options.rst:152](doc/manpages/common-options.rst#L152)）：

> If there is no **/** character, the module directory is searched first for a module named **"<daemon>_<module>.so"**, then for **"<module>.so"**. This allows for a module to exist in variations appropriate for particular daemons, e.g. `zebra_snmp` and `bgp_snmp`, with the correct one selected by `-M snmp`.

两个细节：
- spec 含 `/` → 直接跳到第 3 步，不做前缀拼接；
- `RTLD_NOW | RTLD_GLOBAL`（[:210](lib/module.c#L210)）：`NOW` = 立刻解析全部符号（**加载期就暴露缺失符号**，而不是第一次调用时才炸）；`GLOBAL` = 本模块符号对后续模块可见。

#### ③ 取模块入口 `frr_module`（[:240-248](lib/module.c#L240-L248)）

```c
rtinfop = dlsym(handle, "frr_module");   /* FRR_MODULE_SETUP 导出的唯一约定符号 */
if (!rtinfop) { dlclose(handle); ... return NULL; }   /* 不是 FRR 模块即拒收 */
rtinfo = *rtinfop;
rtinfo->load_name = XSTRDUP(MTYPE_MODULE_LOADNAME, name);   /* show modules 用 */
rtinfo->dl_handle = handle;
```

#### ④ 查重 → 调 `init()` → 尾插入链（[:255-272](lib/module.c#L255-L272)）

```c
if (rtinfo->finished_loading) { ... "already loaded" ...; goto out_fail; }
if (info->init && info->init()) { ... "initialisation failed"; goto out_fail; }
rtinfo->finished_loading = true;
*frrmod_last = rtinfo;  frrmod_last = &rtinfo->next;
```

注意 **`init()` 是加载时同步调用的**，且处于 `frr_init()` 上下文 —— 所以模块 `init()` 里能做的事非常有限（见 §8.7）。

#### ⑤ 错误处理三段式

| 宏 | 作用 | 位置 |
|----|------|------|
| `ERR_RECORD(...)` | 攒一条错误（上限 `FRRMOD_LOAD_N_ERRSTR` = 10 条） | [:155-171](lib/module.c#L155-L171) |
| `ERR_REPORT` | 把已记录的逐条交给 `pFerrlog` 回调 | [:176-185](lib/module.c#L176-L185) |
| `ERR_FREE` | 释放错误字符串、清零计数 | [:190-199](lib/module.c#L190-L199) |

契约写在注释里（[:119-131](lib/module.c#L119-L131)）：**`pFerrlog` 只在函数返回失败时才被调用，且错误串在返回前会被释放** —— 回调要保留必须自己拷贝。

### 8.7 最关键的时序坑：模块只能挂钩子，CLI 必须推迟到 `frr_late_init`

看真实模块 `bgpd/bgp_bmp.c`：

```c
static int bgp_bmp_module_init(void)      /* ← 加载时调用（libfrr.c:899）        */
{
	hook_register(bgp_packet_dump,  bmp_mirror_packet);
	hook_register(bgp_route_update, bmp_route_update);
	hook_register(frr_late_init,    bgp_bmp_init);   /* ★ CLI 安装推迟到这一刻 */
	hook_register(frr_early_fini,   bgp_bmp_early_fini);
	return 0;
}

static int bgp_bmp_init(struct event_loop *tm)   /* ← frr_late_init 钩子回调 */
{
	install_node(&bmp_node);
	install_element(BGP_NODE, &bmp_targets_cmd);   /* 真正的 CLI 装在这里 */
	...
}

FRR_MODULE_SETUP(.name = "bgpd_bmp", .version = FRR_VERSION,
		 .description = "bgpd BMP module", .init = bgp_bmp_module_init);
```

（[bgpd/bgp_bmp.c:3716-3743](bgpd/bgp_bmp.c#L3716-L3743)、[:4023-4046](bgpd/bgp_bmp.c#L4023-L4046)）

原因就是 §8.2 的约束 3：模块加载发生在 `cmd_init()` **之前**，那会儿 `cmdvec` 还没建，`install_element()` 无从谈起。而 `frr_late_init` 钩子在 `frr_config_fork()` 里触发（[lib/libfrr.c:1174](lib/libfrr.c#L1174)），已远在 `cmd_init()` 之后、主循环之前 —— 正是官方推荐的位置：

> The `frr_late_init` hook will be called after the daemon has finished its other startup and is about to enter the main event loop; this is the best place for most initialisation. — [doc/developer/modules.rst:81-83](doc/developer/modules.rst#L81-L83)

**所以三段式是硬性约定：`-M` 加载 → `init()` 只 `hook_register` → `frr_late_init` 里装 CLI。**

> 顺带一提：模块挂钩子时，钩子框架会记录"这个回调属于哪个模块"（`he->module`，[lib/hook.c:88](lib/hook.c#L88)）——这是为将来实现"卸载时批量摘掉该模块所有钩子"留的地基（[doc/developer/modules.rst:106-108](doc/developer/modules.rst#L106-L108)）。

### 8.8 完整时序：`bgpd -M bmp -M rpki`

```
frr_getopt()   -M bmp  ┐
               -M rpki ┘→ modules 链: [bmp] → [rpki]        (oc->arg 借 argv)
                          │
frr_init()  :838  dir = frr_moduledir（或 --moduledir）
            :864  zlog_init()                              → 日志目录就绪（§7.3 阶段 2）
            :896  frrmod_init(di->module)
                    execname = "bgpd"
                    frrmod_list: libfrr → bgpd
            :897  frrmod_load("bmp")   → /usr/lib/frr/modules/bgpd_bmp.so
                    └─ bgp_bmp_module_init()               只注册 hook（含 frr_late_init）
            :899  frrmod_load("rpki")  → /usr/lib/frr/modules/bgpd_rpki.so
            :905  zprivs_init()                            ← 此后不再有特权
            :909  hook_call(frr_early_init)
            :920  cmd_init()                               ← cmdvec 建好
            :927  vty_init()
frr_config_fork() :1174  hook_call(frr_late_init)              ← 模块在这里装 CLI（§7.3 阶段 3）
frr_run()                主循环（§7.3 阶段 6）
frr_fini()   :1451  frrmod_terminate()                         ← 只释放字符串，不 dlclose
```

### 8.9 可观测与收尾

| 关注点 | 手段 | 位置 |
|--------|------|------|
| 哪些模块加载了 | `show modules`：遍历 `frrmod_list` 打印名字/版本/描述 | [lib/lib_vty.c:187-228](lib/lib_vty.c#L187-L228) |
| 模块**从哪加载**的 | 仅对 `dl_handle` 非空者，用 `dlinfo(RTLD_DI_ORIGIN / RTLD_DI_LINKMAP)` 打印来源路径 | 同上 [:202-221](lib/lib_vty.c#L202-L221) |
| 加载失败 | `_err_print` → `frr_init: <err>` 到 stderr，然后 `exit(1)` | [:814](lib/libfrr.c#L814)、[:900](lib/libfrr.c#L900) |
| 退出清理 | `frrmod_terminate()` 只 `XFREE` 各节点 `load_name` / `load_args`，**不 dlclose** | [lib/module.c:295-305](lib/module.c#L295-L305)、[lib/libfrr.c:1451](lib/libfrr.c#L1451) |
| 卸载 | **未实现**：`frrmod_unload()` 是 `#if 0` | [lib/module.c:285-289](lib/module.c#L285-L289) |

### 8.10 坑清单

| # | 坑 | 结论 |
|---|----|------|
| 1 | 以为 `frrmod_init(di->module)` 在加载模块 | 它在**登记 daemon 自己**并设置 `execname`；加载是紧随其后的 `frrmod_load()` |
| 2 | 在模块 `init()` 里 `install_element()` | 段错误/无效。那时 `cmd_init()` 还没跑，必须改用 `frr_late_init` 钩子（§8.7） |
| 3 | 以为 `-M` 的名字必须等于 `FRR_MODULE_SETUP(.name=)` | 不。它按**文件名**规则找：`<daemon>_<名字>.so` → `<名字>.so`。`dplane_sample`（文件 `dplane_sample_plugin.so`）、`frr_sysrepo`（文件 `sysrepo.so`）、`frr_grpc`（文件 `grpc.so`）都会因此找不到，要按文件名写（`-M sysrepo`、`-M grpc`） |
| 4 | 想运行期 enable/disable 模块 | 不支持，只能启动期 `-M`；BMP 文档明说"started without `-M bmp` 就无法再启用"（[doc/user/bmp.rst:66-68](doc/user/bmp.rst#L66-L68)） |
| 5 | 以为 `--moduledir` 会被 `-N` 影响 | 不会。手册特别注明 *This path is not affected by the -N option*（[common-options.rst:160](doc/manpages/common-options.rst#L160)）——与 §7.9 坑 #2 的 `TMPBASEDIR` 是同一种"不随 pathspace 变" |
| 6 | 开发期用 `-M zebra_snmp` | libtool 产物在 `.libs/` 下，开发期得 `./zebra -M .libs/zebra_snmp.so`（[modules.rst:42-45](doc/developer/modules.rst#L42-L45)） |
| 7 | 想用模块隔离故障/许可证 | 都不成立：模块崩 = daemon 崩；模块仍受 GPL 约束（[modules.rst:14-23](doc/developer/modules.rst#L14-L23)） |
| 8 | 忘了默认配置里没有 `-M` | [tools/etc/frr/daemons](tools/etc/frr/daemons) 的 `*_options` 默认都不带 `-M`，要自己加，如 `bgpd_options=" -A 127.0.0.1 -M rpki"` |

### 8.11 代码索引

| 环节 | 文件:行 |
|------|---------|
| `-M` 选项解析（攒链表） | [lib/libfrr.c:538-544](lib/libfrr.c#L538-L544) |
| 短选项串 / 帮助文本 | [lib/libfrr.c:176](lib/libfrr.c#L176)、[:180](lib/libfrr.c#L180) |
| `--moduledir` 选项 | [lib/libfrr.c:689-697](lib/libfrr.c#L689-L697) |
| `modules` 链表全局量 | [lib/libfrr.c:505-511](lib/libfrr.c#L505-L511) |
| 模块加载两行 | [lib/libfrr.c:896-903](lib/libfrr.c#L896-L903) |
| 默认模块目录 `frr_moduledir` | [lib/libfrr.c:77](lib/libfrr.c#L77)（`MODULE_PATH`，来自 `--with-moduledir`） |
| 错误输出回调 `_err_print` | [lib/libfrr.c:814-819](lib/libfrr.c#L814-L819) |
| `frrmod_init()` | [lib/module.c:110-117](lib/module.c#L110-L117) |
| `frrmod_load()` 全文 | [lib/module.c:143-280](lib/module.c#L143-L280) |
| 三次 dlopen 候选路径 | [lib/module.c:206-230](lib/module.c#L206-L230) |
| `frrmod_terminate()` | [lib/module.c:295-305](lib/module.c#L295-L305) |
| 模块宏 / 结构体 | [lib/module.h:57-96](lib/module.h#L57-L96)、[:126-150](lib/module.h#L126-L150) |
| `FRR_DAEMON_INFO` 中的模块设置 | [lib/libfrr.h:227-235](lib/libfrr.h#L227-L235) |
| `show modules` | [lib/lib_vty.c:187-228](lib/lib_vty.c#L187-L228) |
| 官方文档（设计/限制/写法） | [doc/developer/modules.rst](doc/developer/modules.rst) |
| 官方手册（`-M` / `--moduledir`） | [doc/manpages/common-options.rst:148-162](doc/manpages/common-options.rst#L148-L162) |

### 8.12 FRR 10.6.1 里实际存在的可加载模块

| 模块名（`.name=`） | 源文件 | 编译产物 | 推荐写法 | 用途 |
|-------------------|--------|---------|---------|------|
| `zebra_snmp` | [zebra/zebra_snmp.c:555](zebra/zebra_snmp.c#L555) | `zebra_snmp.so` | `-M snmp` | zebra AgentX SNMP |
| `bgpd_snmp` | [bgpd/bgp_snmp.c:219](bgpd/bgp_snmp.c#L219) | `bgpd_snmp.so` | `-M snmp` | bgpd SNMP |
| `ospfd_snmp` / `ospf6d_snmp` / `ripd_snmp` / `isisd_snmp`（`.name=isis_snmp`）/ `ldpd_snmp`（`.name=ldp_snmp`） | ospfd/ospf_snmp.c:2549、ospf6d/ospf6_snmp.c:1440、ripd/rip_snmp.c:574、isisd/isis_snmp.c:3460、ldpd/ldp_snmp.c:1219 | 各自 `*d_snmp.so` | `-M snmp` | 各协议的 SNMP 代理 |
| `zebra_fpm` | [zebra/zebra_fpm.c:2073](zebra/zebra_fpm.c#L2073) | `zebra_fpm.so` | `-M fpm` | 老版 FPM（protobuf） |
| `dplane_fpm_nl` | [zebra/dplane_fpm_nl.c:1873](zebra/dplane_fpm_nl.c#L1873) | `dplane_fpm_nl.so` | `-M dplane_fpm_nl` | netlink 版 FPM |
| `dplane_dpdk` | [zebra/dpdk/zebra_dplane_dpdk.c:610](zebra/dpdk/zebra_dplane_dpdk.c#L610) | `zebra_dplane_dpdk.so` | `-M dplane_dpdk` | DPDK 数据面 |
| `zebra_cumulus_mlag` | [zebra/zebra_mlag_private.c:280](zebra/zebra_mlag_private.c#L280) | `zebra_cumulus_mlag.so` | `-M cumulus_mlag` | MLAG |
| `bgpd_bmp` | [bgpd/bgp_bmp.c:4043](bgpd/bgp_bmp.c#L4043) | `bgpd_bmp.so` | `-M bmp` | BMP 监控 |
| `bgpd_rpki` | [bgpd/bgp_rpki.c:3162](bgpd/bgp_rpki.c#L3162) | `bgpd_rpki.so` | `-M rpki` | RPKI 起源校验 |
| `frr_pathd_pcep` | [pathd/path_pcep.c:376](pathd/path_pcep.c#L376) | `pathd_pcep.so` ⚠️ | `-M pcep` | PCEP |
| `frr_sysrepo` | [lib/northbound_sysrepo.c:775](lib/northbound_sysrepo.c#L775) | `sysrepo.so` ⚠️ | `-M sysrepo` | sysrepo 北向 |
| `frr_grpc` | [lib/northbound_grpc.cpp](lib/northbound_grpc.cpp) | `grpc.so` ⚠️ | `-M grpc[:PORT]` | gRPC 北向 |
| `dplane_sample` | [zebra/sample_plugin.c:115](zebra/sample_plugin.c#L115) | `dplane_sample_plugin.so` ⚠️ | 需给全路径 | 开发示例插件 |

> ⚠️ 那几行标了警示的，就是坑 #3 的实例：**模块名与文件名不一致时，`-M` 按文件名规则找，只能拼出文件名才命得中**。

---

## 9. 后续可继续深挖的问题

- [ ] ZAPI 消息头的具体格式（`zclient_create_header` / `stream_putw` 长度字段）与 `zserv` 侧的解析
- [ ] `ZEBRA_HELLO` 握手包里到底带了哪些字段（proto / instance / session_id）
- [ ] northbound 从 CLI 到 mgmtd 的完整提交链路（事务、lock、rollback）
- [ ] `terminal monitor` 的 `SCM_RIGHTS` fd 传递在 vtysh 侧如何恢复成日志输出
- [ ] bfdd 的 ptm_adapter 与 BFD 数据面（dplane）的通信路径
- [ ] `zlog_targets` 各目标（syslog / file / journald / live / crashlog）的注册与过滤逻辑
- [ ] 崩溃时 backtrace 是如何生成并写进 `crashlog` 的（`sigevent.c` + `zlog_signal`）
- [ ] `zlog_live` 的线格式（`zlog_live_hdr`）与 ldpd 父进程代理子进程日志的 `zlog_recirculate`
- [ ] 模块卸载（`frrmod_unload()` 目前是 `#if 0`）将来如何配合 `hookent.module` 与 MTYPE 构造/析构做批量清理
- [ ] `frrmod_load()` 用 `RTLD_GLOBAL` 加载后，多个模块之间的符号可见性与命名冲突风险
- [ ] `show modules` 里 `dlinfo(RTLD_DI_ORIGIN / RTLD_DI_LINKMAP)` 在非 glibc 平台（musl）上的退化行为

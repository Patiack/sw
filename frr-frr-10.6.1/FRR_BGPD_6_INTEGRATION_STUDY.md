# FRR bgpd 深入剖析 · 卷6 —— 外部集成、配置面与可观测性

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 本卷覆盖文件 25 个、共 47640 行（工作树行数，含中文注释）。
>
> 写作目的：
> 1. **zebra 联动**：`bgp_zebra.c`（约 5520 行）与 zebra 之间到底互相发哪些 `ZEBRA_*` 消息（给全表）；
>    `bgp_zebra_announce/withdraw` 怎样把“选出来的最优路由”下发到内核 FIB；
>    为什么要有“有序通告”（`zebra_announce_head` 队列），它解决了什么问题。
> 2. **下一跳相关的两个小文件**：`bgp_nhc.c`（158 行）与 `bgp_nhg.c`（147 行）分别解决什么？
>    （它们和下一跳缓存、下一跳组、`ZEBRA_NHG_*` 是什么关系。）
> 3. **BMP**（`bgp_bmp.c` 约 4046 行，RFC 7854）：Route Monitoring / Peer Down / Peer Up /
>    Stats Report / Initiation / Termination 六类报文的线格式，以及它挂在 BGP 会话的哪些钩子点上。
> 4. **RPKI**（`bgp_rpki.c` 约 3165 行，RFC 6810/8210）：RPKI-RTR 客户端、与 rtrlib 库的关系、
>    校验三态（VALID/INVALID/NOTFOUND）怎样影响选路。
> 5. **SNMP**（`bgp_snmp.c` / `bgp_snmp_bgp4.c` / `bgp_snmp_bgp4v2.c`）：AgentX 子代理、bgp4 MIB
>    （RFC 4273）与 bgp4v2 MIB（RFC 4382）的区别、`BGP_OPT_TRAPS_*` 开关控制什么。
> 6. **脚本钩子**：`bgp_script.c`（--enable-scripting / `HAVE_SCRIPTING`）下，哪些事件会触发外部脚本。
> 7. **MRT 导出与转换**：`bgp_dump.c` 怎么写出 MRT 文件，`bgp_btoa.c` 又怎么把它读成文本。
> 8. **配置面**：`bgp_vty.c`（约 24654 行，全模块最大文件）内部怎么分区（给分区地图）；
>    它和北向接口（YANG / northbound）是什么关系。
> 9. **调试面**：`bgp_debug.c` 的 debug 开关层级，以及它和 `terminal monitor` 的关系。
>
> 本卷是 `FRR_BGPD_STUDY.md`（总览篇）之下的分册。总览篇给出 bgpd 的整体架构、三线程模型、
> 核心数据结构与 131 文件全景地图；本卷只深入「外部集成、配置面与可观测性」这一层，其余层次
> 请在对应分册中查阅。本文不修改任何源码。

---

## 本卷文件覆盖清单（自检用）

| # | 文件 | 行数 | 一句话职责 | 本卷小节 |
|---|------|------|-----------|---------|
| 1 | `bgp_zebra.c` | 5520 | bgpd↔zebra 的 zclient 客户端：收发表、FIB 下发、EVPN/SRv6/PBR 联动 | §1 |
| 2 | `bgp_zebra.h` | 229 | 上者对外的宏与接口（`BGP_ORIGINAL_UPDATE`、`bgp_zebra_announce_eligible`） | §1 |
| 3 | `bgp_nhc.c` | 158 | Next-Hop Capability (NHC) TLV 链表的增删查 | §2 |
| 4 | `bgp_nhc.h` | 131 | NHC 结构与 TLV 码（NNHN / BGPID） | §2 |
| 5 | `bgp_nhg.c` | 147 | 给 L3 下一跳组（NHG）向 zebra 申请/释放 ID 段 | §2 |
| 6 | `bgp_nhg.h` | 45 | NHG 初始化/分配器原型 | §2 |
| 7 | `bgp_bmp.c` | 4046 | BMP 生产者/队列/发送线程：六类报文的封装与钩子 | §3 |
| 8 | `bgp_bmp.h` | 449 | BMP 线格式常量、queue/mirror/targets 结构 | §3 |
| 9 | `bgp_rpki.c` | 3165 | RPKI-RTR 客户端（基于 rtrlib）：cache 管理、同步、校验、CLI | §4 |
| 10 | `bgp_rpki.h` | 51 | RPKI 三态枚举（`enum rpki_states`） | §4 |
| 11 | `bgp_snmp.c` | 222 | SNMP 模块入口：trap 开关、trap 发送、装载/初始化 | §5 |
| 12 | `bgp_snmp.h` | 57 | ASN.1 类型别名与 trap 入口声明 | §5 |
| 13 | `bgp_snmp_bgp4.c` | 908 | BGP4-MIB（RFC 4273）变量表与 trap 实现 | §5 |
| 14 | `bgp_snmp_bgp4.h` | 142 | BGP4-MIB OID 前缀与表列定义 | §5 |
| 15 | `bgp_snmp_bgp4v2.c` | 1731 | BGP4V2-MIB（RFC 4382）变量表与 trap 实现 | §5 |
| 16 | `bgp_snmp_bgp4v2.h` | 141 | BGP4V2-MIB OID 前缀与表列定义 | §5 |
| 17 | `bgp_script.c` | 250 | Lua 脚本桥：peer/attr 的编解码器 | §6 |
| 18 | `bgp_script.h` | 82 | 上者对外的编解码原型（`HAVE_SCRIPTING` 内） | §6 |
| 19 | `bgp_dump.c` | 1079 | MRT 转储：文件打开、TABLE_DUMP_V2/BGP4MP 记录写出 | §7 |
| 20 | `bgp_dump.h` | 102 | MRT 类型/子类型常量与接口 | §7 |
| 21 | `bgp_btoa.c` | 323 | 独立工具：把 MRT 二进制转成可读文本 | §7 |
| 22 | `bgp_vty.c` | 24654 | bgpd 的 CLI 层（最大文件）：命令定义 + 注册 + config-write | §8 |
| 23 | `bgp_vty.h` | 319 | 命令字符串宏、默认值档案、GR 同步宏、解析辅助原型 | §8 |
| 24 | `bgp_debug.c` | 3384 | debug 开关命令族 + 信息打印 + config-write | §9 |
| 25 | `bgp_debug.h` | 305 | 双态 debug 位的宏与结构（conf/term） | §9 |

> **覆盖率自检**：上表与本卷分配到的 25 个文件逐一对齐，一个不漏、一个不多。

---

## §0 本卷总览

### 0.1 先给结论（本卷最重要的 5 条判断）

> **结论 1：bgpd 的所有“对外联动的重活”，几乎都收敛到两类机制——`zclient`（与 zebra 进程）和
> `hook`（与编译进来的 .so 模块）。** 前者是 bgpd 唯一的“编程式内核接口”通道（zebra 再往下走
> netlink），后者是 BMP / RPKI / SNMP 这些**可加载模块**接入 bgpd 的方式。看懂这两条主线，
> 本卷一大半文件就串起来了。

> **结论 2：FIB 下发是“异步两段式 + 队列化”的。** `bgp_zebra_route_install()` 只把 `dest` 挂进
> `bm->zebra_announce_head` 队列并排一个事件，真正编码 ZAPI 的是
> `bgp_handle_route_announcements_to_zebra()` → `bgp_zebra_announce_actual()`。这样做的直接收益是
> **合并抖动、限流**（单次事件最多处理 `ZEBRA_ANNOUNCEMENTS_LIMIT = 1000` 条），并让“选路结果”
> 与“内核下发”解耦。

> **结论 3：`bgp_nhc.c` 不是“下一跳缓存”，`bgp_nhg.c` 也不是“下一跳网关对象管理”。**
> 前者是 **Next-Hop Capability (NHC) TLV**（“下一跳的下一跳节点”扩展，draft 系列）的链表工具；
> 后者只是给 L3 **下一跳组（Nexthop Group, NHG）** 向 zebra 申请一段 ID 号段。真正的“下一跳缓存/
> 跟踪（NHT）”实现在 **`bgp_nexthop.c`**（属其它卷），本卷不展开。

> **结论 4：BMP / RPKI / SNMP 三个“外部集成”都是同一个套路——编译成独立 `.so`，靠
> `FRR_MODULE_SETUP` 声明、靠 `hook_register` 把回调挂进 bgpd 主流程。** 它们在主进程里几乎不改
> 数据结构，只在“钩子被调用时”读取或旁路发送。因此三者对协议主流程是**只读旁路**（RPKI 稍特殊，
> 见 §4，它反过来给选路提供判据）。

> **结论 5：`bgp_vty.c` 里“命令定义（DEFUN/DEFPY）”与“命令注册（install_element）”是分开的，
> 而且这个文件与北向 YANG/NB 的关系是“各走各的”。** `bgp_vty.c` 是**传统 VTY-only** 的配置入口，
> 绝大多数 cmd_node 带 `no_xpath = true`；真正基于 YANG 的 northbound 配置在 `bgp_nb*.c` 里
> （属其它卷）。二者最终写的是**同一批配置字段**，但入口与事务机制不同。

### 0.2 文件职责分层表

| 层 | 文件 | 外部联动对端 | 通信机制 |
|----|------|-------------|---------|
| 内核路由层 | `bgp_zebra.c/.h` | **zebra** 进程 | `zclient`（ZAPI，Unix socket / TCP） |
| 下一跳扩展 | `bgp_nhc.c/.h`、`bgp_nhg.c/.h` | （NHC 无对端；NHG 对 **zebra**） | 结构工具；`zclient_get_nhg_start` |
| 监控旁路 | `bgp_bmp.c/.h` | 外部 **BMP 采集器** | BMP over TCP（主动连出 / 被动监听） |
| 安全校验 | `bgp_rpki.c/.h` | **RPKI Cache**（RTR 服务器） | RPKI-RTR over TCP（rtrlib） |
| 管理面 | `bgp_snmp*.c/.h` | **snmpd**（AgentX master） | AgentX over Unix socket（smux） |
| 脚本扩展 | `bgp_script.c/.h` | 内嵌 **Lua** 解释器 | 进程内 Lua C API（frrscript） |
| 数据导出 | `bgp_dump.c/.h`、`bgp_btoa.c` | 文件 / 离线工具 | 写 MRT 文件；btoa 读文件 |
| 配置面 | `bgp_vty.c/.h` | 用户 / vtysh /（间接）northbound | VTY/CLI |
| 调试面 | `bgp_debug.c/.h` | 用户 / vtysh；日志走 lib 的 live-log | VTY/CLI + zlog |

### 0.3 全景图（本卷内部的数据流 / 调用流）

```
                          +-------------------- bgpd 主进程 ---------------------+
                          |                                                    |
 用户的 CLI 输入 ---> bgp_vty.c (DEFUN/DEFPY 处理) ---> 改 bgp/peer/... 配置字段  |
                          |        |                                           |
                          |        +--> bgp_debug.c (debug 位: conf_/term_)    |
                          |                |                                   |
                          |                +-- term_* 位 + terminal monitor -->  lib/vty.c live-log
                          |                                                    |
  选路(bgp_route/bgp_bestpath) 产出“最优路径”                                    |
                          |                                                    |
   bgp_zebra_route_install(dest, info)  --(挂队列)-->  bm->zebra_announce_head   |
                          |                                     |              |
                          |   (事件) bgp_handle_route_announcements_to_zebra()    |
                          |                 |                                   |
                          |                 +--> bgp_zebra_announce_actual() -- zclient--> zebra
                          |                 +--> bgp_zebra_withdraw_actual()                  |
                          |                                                    |
   zebra --ZAPI--> bgp_handlers[] (bgp_zebra.c) --> 更新 bgp 状态 / FIB_INSTALLED |
                          |                                                    |
   bgpd 主流程各处 hook_call(...)  ---------------------------+                  |
                          |                                   |                  |
   .so 模块:  hook_register 的回调被调用（只读旁路）：          |                  |
     - bgpd_bmp.so  :  bmp_process / bmp_mirror_packet / bmp_peer_status_changed --> BMP 采集器
     - bgpd_rpki.so :  rpki_validate_prefix (由选路侧 hook_call 拉取判据)        --> RTR 服务器
     - bgpd_snmp.so :  bgpTrapEstablished / bgpTrapBackwardTransition            --> snmpd (AgentX)
     - (主库) bgp_script.c : Lua 编解码器，供脚本框架调用
                          |                                                    |
   bgp_dump.c: bgp_packet_dump hook --> 写 MRT 文件 --> 事后 bgp_btoa 读文件      |
                          +----------------------------------------------------+
```

### 0.4 与相邻卷的边界

- 本卷讲 zebra 联动的**接口面**（消息表、下发流程、钩子）；`bgp_nht`（下一跳跟踪/缓存的算法）
  与 `bgp_nexthop.c` 的**下一跳类型与可达性判定**属其它卷（见总览篇文件地图）。
- 本卷讲 `bgp_vty.c` 的**分区与注册结构**；具体某条命令的**语义**（如 `aggregate-address` 的算法）
  多属路由/策略卷。
- 本卷讲 `bgp_debug.c` 的**开关层级**；被打印的**内容语义**（UPDATE 编解码等）属消息卷。
- `bgp_dump.c` 写出的 MRT **内容**（路由表快照）依赖 `bgp_route.c`，本卷只讲**格式与写出路径**。

---

## §1 zebra 联动：`bgp_zebra.c` / `bgp_zebra.h`

### 1.1 定位：bgpd 的“唯一内核接口”

bgpd 自己**不直接**碰内核路由表。它把“要装哪些前缀、下一跳是谁”交给 **zebra** 进程，zebra 再统一
经 netlink/其它后端写内核。bgpd↔zebra 之间的协议实现就是 `bgp_zebra.c`。两个方向：

- **出向（bgpd → zebra）**：用 `zclient`/`zclient_sync` 两个 zclient 句柄发 ZAPI 消息。
- **入向（zebra → bgpd）**：全部经 `bgp_handlers[]` 分派（见 §1.4）。

两个句柄定义在：
- 主 zclient：`[bgp_zebra.c:113](bgpd/bgp_zebra.c#L113)` `struct zclient *bgp_zclient = NULL;`
- 同步 zclient（标签管理器、需要同步回应的场景）：
  `[bgp_zebra.c:117](bgpd/bgp_zebra.c#L117)` `struct zclient *bgp_zclient_sync;`

初始化总入口 `bgp_zebra_init()`：
`[bgp_zebra.c:4830](bgpd/bgp_zebra.c#L4830)`。它 `zclient_new(..., bgp_handlers, array_size(...))` 用于挂
入向分派表，`zclient_init(bgp_zclient, ZEBRA_ROUTE_BGP, 0, &bgpd_privs)` 注册自身路由类型：

```
[bgp_zebra.c:4840] bgp_zclient = zclient_new(master, &zclient_options_default, bgp_handlers, array_size(bgp_handlers));
[bgp_zebra.c:4842] zclient_init(bgp_zclient, ZEBRA_ROUTE_BGP, 0, &bgpd_privs);
```

（见 `[bgp_zebra.c:4840](bgpd/bgp_zebra.c#L4840)`、`[bgp_zebra.c:4842](bgpd/bgp_zebra.c#L4842)`。）
另有 `bgp_zclient_sync = zclient_new(master, &zclient_options_sync, NULL, 0);`
（`[bgp_zebra.c:4850](bgpd/bgp_zebra.c#L4850)`）。连上 zebra 后的回调挂在
`bgp_zclient->zebra_connected = bgp_zebra_connected;`
（`[bgp_zebra.c:4844](bgpd/bgp_zebra.c#L4844)`，函数体在 `[bgp_zebra.c:3702](bgpd/bgp_zebra.c#L3702)`）。

> **读代码提示**：本卷**没有**名为 `zclient_send_message` 的“集中发送函数”——它其实是 lib 里
> zclient 的通用发送原语，`bgp_zebra.c` 各处直接调用它，或调用更高层的
> `zclient_route_send()`（发路由）、`zebra_redistribute_send()`（登记重分发）、
> `bfd_client_sendmsg()`（BFD 登记）。本文表格里“发送点”给的是**调用这些原语的那一行**。

### 1.2 出向消息表（bgpd → zebra）

下表按“谁在什么场景发”归拢。**发送点行号**为该 send 调用所在行。

| ZEBRA_* 消息 | 发送函数（定义行） | 发送点 | 场景 |
|---|---|---|---|
| `ZEBRA_ROUTE_ADD` | `bgp_zebra_announce_actual` `[1911](bgpd/bgp_zebra.c#L1911)` | `zclient_route_send(ZEBRA_ROUTE_ADD, ...)` `[2049](bgpd/bgp_zebra.c#L2049)` | 把 best path 装进 FIB |
| `ZEBRA_ROUTE_DELETE` | `bgp_zebra_withdraw_actual` `[2124](bgpd/bgp_zebra.c#L2124)` | `zclient_route_send(ZEBRA_ROUTE_DELETE, ...)` `[2155](bgpd/bgp_zebra.c#L2155)` | 从前缀 FIB 撤下 |
| `ZEBRA_ROUTE_ADD/DELETE`（radv 通告用） | `bgp_zebra_announce_default` 组 | `zclient_route_send(announce ? ADD : DELETE, ...)` `[5148](bgpd/bgp_zebra.c#L5148)` | ICMP RA 通告默认路由场景 |
| `ZEBRA_REDISTRIBUTE_ADD` | `zebra_redistribute_send(...)` | `[2609](bgpd/bgp_zebra.c#L2609)`、`[2634](bgpd/bgp_zebra.c#L2634)` | `redistribute <proto>`：请求 zebra 回送该源的路由 |
| `ZEBRA_REDISTRIBUTE_DELETE` | `bgp_zebra_redistribute_unset` 路径 | 同段（约 `[2749](bgpd/bgp_zebra.c#L2749)`） | 取消重分发 |
| `ZEBRA_ADVERTISE_SUBNET` | `bgp_zebra_advertise_subnet` `[3004](bgpd/bgp_zebra.c#L3004)` | `zclient_send_message` `[3030](bgpd/bgp_zebra.c#L3030)` | EVPN：通告子网 |
| `ZEBRA_ADVERTISE_SVI_MACIP` | `bgp_zebra_advertise_svi_macip` `[3035](bgpd/bgp_zebra.c#L3035)` | `[3056](bgpd/bgp_zebra.c#L3056)` | EVPN：SVI MAC/IP 通告 |
| `ZEBRA_ADVERTISE_DEFAULT_GW` | `bgp_zebra_advertise_gw_macip` `[3061](bgpd/bgp_zebra.c#L3061)` | `[3087](bgpd/bgp_zebra.c#L3087)` | EVPN：默认网关 MAC/IP |
| `ZEBRA_VXLAN_FLOOD_CONTROL` | `bgp_zebra_vxlan_flood_control` `[3092](bgpd/bgp_zebra.c#L3092)` | `[3126](bgpd/bgp_zebra.c#L3126)` | EVPN：洪泛控制 |
| `ZEBRA_ADVERTISE_ALL_VNI` | `bgp_zebra_advertise_all_vni` `[3131](bgpd/bgp_zebra.c#L3131)` | `[3155](bgpd/bgp_zebra.c#L3155)` | EVPN：通告所有 VNI |
| `ZEBRA_DUPLICATE_ADDR_DETECTION` | `bgp_zebra_dup_addr_detection` `[3160](bgpd/bgp_zebra.c#L3160)` | `[3194](bgpd/bgp_zebra.c#L3194)` | EVPN：重复地址检测（DAD） |
| `ZEBRA_BFD_CLIENT_REGISTER` | `bfd_client_sendmsg(...)` | `[3709](bgpd/bgp_zebra.c#L3709)` | 连上 zebra 后登记 BFD 客户端 |
| `ZEBRA_RULE_ADD/DELETE` | `bgp_send_pbr_rule_action`（组） | `[4909](bgpd/bgp_zebra.c#L4909)` | PBR：策略路由规则 |
| `ZEBRA_IPSET_CREATE/DESTROY` | `bgp_send_pbr_ipset_match` `[4923](bgpd/bgp_zebra.c#L4923)` | `[4938](bgpd/bgp_zebra.c#L4938)` | PBR：ipset 匹配集 |
| `ZEBRA_IPSET_ENTRY_ADD/DELETE` | `bgp_send_pbr_ipset_entry` 路径 | 约 `[4952](bgpd/bgp_zebra.c#L4952)` | PBR：ipset 表项 |
| `ZEBRA_IPTABLE_ADD/DELETE` | `bgp_send_pbr_iptable` `[5025](bgpd/bgp_zebra.c#L5025)` | `[5045](bgpd/bgp_zebra.c#L5045)` | PBR：iptables 转发规则 |
| `ZEBRA_MPLS_LABELS_ADD/DELETE` | `bgp_zebra_send_nexthop_label` `[5433](bgpd/bgp_zebra.c#L5433)` | `zebra_send_mpls_labels(...)` `[5469](bgpd/bgp_zebra.c#L5469)` | MPLS：给某下一跳装/删标签 |
| `ZEBRA_LABEL_MANAGER_CONNECT` + 标签段请求 | `bgp_zebra_request_label_range` `[5475](bgpd/bgp_zebra.c#L5475)` | 内部 `zclient_send_message` | 标签管理器取号段 |
| `ZEBRA_RELEASE_LABEL_RANGE` | `bgp_zebra_release_label_range` `[5510](bgpd/bgp_zebra.c#L5510)` | 内部 | 释放号段 |
| SRv6 相关（locator chunk 等） | `bgp_zebra_*_srv6*` 组（约 5331–5428） | 多处 | SRv6：向 zebra 请求 locator chunk |

> **注意 PBR 与 label 目标句柄不同**：PBR/EVPN 类走主 `bgp_zclient`；标签管理器走
> **同步** zclient `bgp_zclient_sync`（因为要同步取回号段）。见
> `bgp_zebra_label_manager_ready()` `[4757](bgpd/bgp_zebra.c#L4757)` 判断 `bgp_zclient_sync->sock`。

### 1.3 核心流程：最优路由怎样下发内核（异步两段式）

**入口**——选路结束或路由变动时，调用：
`bgp_zebra_route_install(struct bgp_dest *dest, struct bgp_path_info *info, ...)`
`[bgp_zebra.c:2351](bgpd/bgp_zebra.c#L2351)`。它做的**不是立刻发送**，而是：

1. 判断能否下发：`bgp_install_info_to_zebra(bgp)`（`[136](bgpd/bgp_zebra.c#L136)`），
   以及 `bgp_zebra_announce_eligible(pi)`（内联，见 `bgp_zebra.h`）。
2. 若开了 `suppress-fib-pending` 且该 dest 从未装过（`BGP_NODE_FIB_INSTALLED` 未置），
   先置 `BGP_NODE_FIB_INSTALL_PENDING`（`[2377](bgpd/bgp_zebra.c#L2377)`）。
3. 把 dest 挂进队列：`zebra_announce_add_tail(&bm->zebra_announce_head, dest)`
   （`[2397](bgpd/bgp_zebra.c#L2397)`），保存 `za_bgp_pi`（带 lock）。
4. 排事件：`event_add_event(bm->master, bgp_handle_route_announcements_to_zebra, NULL, ...)`
   （`[2430](bgpd/bgp_zebra.c#L2430)`）。

**消费**——`bgp_handle_route_announcements_to_zebra()` `[2187](bgpd/bgp_zebra.c#L2187)`：

- 循环 `zebra_announce_pop(&bm->zebra_announce_head)`，最多 `ZEBRA_ANNOUNCEMENTS_LIMIT` 条；
- 按 dest 的 `BGP_NODE_SCHEDULE_FOR_INSTALL` 分流到
  `bgp_zebra_announce_actual()` `[2230](bgpd/bgp_zebra.c#L2230)` 或
  `bgp_zebra_withdraw_actual()` `[2242](bgpd/bgp_zebra.c#L2242)`；
- 若队列还有剩，重新排事件（限流）。

**编码与发送**：

- `bgp_zebra_announce_actual()` `[1911](bgpd/bgp_zebra.c#L1911)`：算出下一跳、metric、tag、
  SRv6/MPLS 标签等，构造 `struct zapi_route api`，最后
  `zclient_route_send(ZEBRA_ROUTE_ADD, bgp_zclient, &api)`（`[2049](bgpd/bgp_zebra.c#L2049)`）。
  这是**异步**发送（`enum zclient_send_status`；缓冲满会返回 `ZCLIENT_SEND_BUFFERED`）。
- `bgp_zebra_withdraw_actual()` `[2124](bgpd/bgp_zebra.c#L2124)`：只需 prefix/tableid，
  `zclient_route_send(ZEBRA_ROUTE_DELETE, ...)`（`[2155](bgpd/bgp_zebra.c#L2155)`）。

**缓冲回填**——若上一步返回“缓冲”，由
`bgp_zebra_buffer_write_ready()` `[2284](bgpd/bgp_zebra.c#L2284)` 在套接字可写时再次驱动队列
（它内部直接调 `bgp_handle_route_announcements_to_zebra(NULL)`，见 `[2286](bgpd/bgp_zebra.c#L2286)`）。

**闭环回执**——zebra 装完后会回 `ZEBRA_ROUTE_NOTIFY_OWNER`，由
`bgp_zebra_route_notify_owner()` `[3443](bgpd/bgp_zebra.c#L3443)` 处理：
`ZAPI_ROUTE_INSTALLED` 时清 PENDING、置 `BGP_NODE_FIB_INSTALLED`
（`[3486](bgpd/bgp_zebra.c#L3486)`、`[3487](bgpd/bgp_zebra.c#L3487)`），并**立即触发一次选路/通告**
（`group_announce_route(...)`，`[3499](bgpd/bgp_zebra.c#L3499)`）。

**批量入口**（配置变动时用，把整表重下发）：

- `bgp_zebra_announce_table()` `[2058](bgpd/bgp_zebra.c#L2058)`
- `bgp_zebra_announce_table_all_subtypes()` `[2099](bgpd/bgp_zebra.c#L2099)`
- `bgp_zebra_withdraw_table_all_subtypes()` `[2438](bgpd/bgp_zebra.c#L2438)`

### 1.4 “有序通告”队列解决了什么

`bm->zebra_announce_head` 是一个 **`zebra_announce_head` 单链表 + 事件驱动**：

- **抖动合并 / 去重**：同一 dest 在“未处理完”前再次 `route_install`，只会更新它在队列里的标志，
  不会重复排多条 ZAPI。
- **限流**：单次事件最多 1000 条（`ZEBRA_ANNOUNCEMENTS_LIMIT`，
  `[2186](bgpd/bgp_zebra.c#L2186)`），避免“一次收敛把 zebra 打爆”。
- **解耦**：选路线程只管“我要装/撤”，实际编码发送延后到主事件循环，避免在选路临界区里做
  socket I/O。
- **可重入安全**：`bgp_zebra_buffer_write_ready()` 复用同一个消费函数，套接字“可写”事件与
  “有新通告”事件走同一路径。

### 1.5 入向消息表（zebra → bgpd）：`bgp_handlers[]`

分派表定义在 `[bgp_zebra.c:4695](bgpd/bgp_zebra.c#L4695)`，由 `zclient_new()` 接收。
**这是入向的“总路由”**——每类 `ZEBRA_*` 通知都经此表。完整条目（行号即表内条目行）：

| 下标（ZEBRA_* 消息） | 处理函数 | 行 |
|---|---|---|
| `ZEBRA_ROUTER_ID_UPDATE` | `bgp_router_id_update` | `[4696](bgpd/bgp_zebra.c#L4696)` |
| `ZEBRA_INTERFACE_ADDRESS_ADD` | `bgp_interface_address_add` | `[4697](bgpd/bgp_zebra.c#L4697)` |
| `ZEBRA_INTERFACE_ADDRESS_DELETE` | `bgp_interface_address_delete` | `[4698](bgpd/bgp_zebra.c#L4698)` |
| `ZEBRA_INTERFACE_NBR_ADDRESS_ADD` | `bgp_interface_nbr_address_add` | `[4699](bgpd/bgp_zebra.c#L4699)` |
| `ZEBRA_INTERFACE_NBR_ADDRESS_DELETE` | `bgp_interface_nbr_address_delete` | `[4700](bgpd/bgp_zebra.c#L4700)` |
| `ZEBRA_REDISTRIBUTE_ROUTE_ADD` | `zebra_read_route` | `[4701](bgpd/bgp_zebra.c#L4701)` |
| `ZEBRA_REDISTRIBUTE_ROUTE_DEL` | `zebra_read_route` | `[4702](bgpd/bgp_zebra.c#L4702)` |
| `ZEBRA_FEC_UPDATE` | `bgp_read_fec_update` | `[4703](bgpd/bgp_zebra.c#L4703)` |
| `ZEBRA_LOCAL_ES_ADD` | `bgp_zebra_process_local_es_add` `[3750](bgpd/bgp_zebra.c#L3750)` | `[4704](bgpd/bgp_zebra.c#L4704)` |
| `ZEBRA_LOCAL_ES_DEL` | `bgp_zebra_process_local_es_del` `[3792](bgpd/bgp_zebra.c#L3792)` | `[4705](bgpd/bgp_zebra.c#L4705)` |
| `ZEBRA_VNI_ADD` | `bgp_zebra_process_local_vni` `[3909](bgpd/bgp_zebra.c#L3909)` | `[4706](bgpd/bgp_zebra.c#L4706)` |
| `ZEBRA_LOCAL_ES_EVI_ADD` | `bgp_zebra_process_local_es_evi` `[3820](bgpd/bgp_zebra.c#L3820)` | `[4707](bgpd/bgp_zebra.c#L4707)` |
| `ZEBRA_LOCAL_ES_EVI_DEL` | `bgp_zebra_process_local_es_evi` | `[4708](bgpd/bgp_zebra.c#L4708)` |
| `ZEBRA_VNI_DEL` | `bgp_zebra_process_local_vni` | `[4709](bgpd/bgp_zebra.c#L4709)` |
| `ZEBRA_MACIP_ADD` | `bgp_zebra_process_local_macip` `[3966](bgpd/bgp_zebra.c#L3966)` | `[4710](bgpd/bgp_zebra.c#L4710)` |
| `ZEBRA_MACIP_DEL` | `bgp_zebra_process_local_macip` | `[4711](bgpd/bgp_zebra.c#L4711)` |
| `ZEBRA_L3VNI_ADD` | `bgp_zebra_process_local_l3vni` `[3858](bgpd/bgp_zebra.c#L3858)` | `[4712](bgpd/bgp_zebra.c#L4712)` |
| `ZEBRA_L3VNI_DEL` | `bgp_zebra_process_local_l3vni` | `[4713](bgpd/bgp_zebra.c#L4713)` |
| `ZEBRA_IP_PREFIX_ROUTE_ADD` | `bgp_zebra_process_local_ip_prefix` `[4036](bgpd/bgp_zebra.c#L4036)` | `[4714](bgpd/bgp_zebra.c#L4714)` |
| `ZEBRA_IP_PREFIX_ROUTE_DEL` | `bgp_zebra_process_local_ip_prefix` | `[4715](bgpd/bgp_zebra.c#L4715)` |
| `ZEBRA_RULE_NOTIFY_OWNER` | `rule_notify_owner` | `[4716](bgpd/bgp_zebra.c#L4716)` |
| `ZEBRA_IPSET_NOTIFY_OWNER` | `ipset_notify_owner` | `[4717](bgpd/bgp_zebra.c#L4717)` |
| `ZEBRA_IPSET_ENTRY_NOTIFY_OWNER` | `ipset_entry_notify_owner` | `[4718](bgpd/bgp_zebra.c#L4718)` |
| `ZEBRA_IPTABLE_NOTIFY_OWNER` | `iptable_notify_owner` | `[4719](bgpd/bgp_zebra.c#L4719)` |
| `ZEBRA_ROUTE_NOTIFY_OWNER` | `bgp_zebra_route_notify_owner` `[3443](bgpd/bgp_zebra.c#L3443)` | `[4720](bgpd/bgp_zebra.c#L4720)` |
| `ZEBRA_SRV6_LOCATOR_ADD` | `bgp_zebra_process_srv6_locator_add` `[4514](bgpd/bgp_zebra.c#L4514)` | `[4721](bgpd/bgp_zebra.c#L4721)` |
| `ZEBRA_SRV6_LOCATOR_DELETE` | `bgp_zebra_process_srv6_locator_delete` | `[4722](bgpd/bgp_zebra.c#L4722)` |
| `ZEBRA_SRV6_MANAGER_GET_LOCATOR_CHUNK` | `bgp_zebra_process_srv6_locator_chunk` | `[4723](bgpd/bgp_zebra.c#L4723)` |
| `ZEBRA_SRV6_SID_NOTIFY` | `bgp_zebra_srv6_sid_notify` `[4208](bgpd/bgp_zebra.c#L4208)` | `[4725](bgpd/bgp_zebra.c#L4725)` |

> **入向消息族归纳**：① 配置/环境类（router-id、接口地址、FEC）；② 重分发路由（`zebra_read_route`，
> 定义 `[689](bgpd/bgp_zebra.c#L689)`）；③ EVPN/L3VPN 类（ES、VNI、MACIP、L3VNI、IP-Prefix）；
> ④ PBR 回执类（rule/ipset/iptable notify owner）；⑤ SRv6 类；⑥ 内核安装回执
> （`ZEBRA_ROUTE_NOTIFY_OWNER`）。

### 1.6 `bgp_zebra.h` 的几个要点

- `BGP_ORIGINAL_UPDATE` 宏（把“本进程产生的更新”标记进去，见 `bgp_zebra.h` 第 43 行附近）：
  用于区分“我发给别人的”和“别人发给我的”。
- `BGP_ZEBRA_DEFAULT_NHOP_WEIGHT`（ECMP 权重默认值）。
- 内联 `bgp_zebra_announce_eligible(struct bgp_path_info *pi)`：判定一条路径是否**够格**下发 FIB
  （被选为 best、有合法下一跳、非仅内部等）。

### 1.7 与相邻模块的联动

| 谁 | 通过什么 | 到哪 |
|---|---|---|
| 选路模块 `bgp_route.c` | 调 `bgp_zebra_route_install/withdraw` | 进本文件队列 |
| 配置 `bgp_vty.c` | 调 `bgp_zebra_send_capabilities`（GR）、`bgp_zebra_*_subnet` 等 | 发 ZAPI |
| 下一跳组 `bgp_nhg.c` | `zclient_get_nhg_start(ZEBRA_ROUTE_BGP)` | 取 NHG ID 段 |
| zebra | ZAPI 入向 | `bgp_handlers[]` 分派 |

---

## §2 下一跳相关的两个小文件：`bgp_nhc.c` 与 `bgp_nhg.c`

> **先纠一个常见误解**：这两个“N H”开头的文件**都不是**“下一跳缓存 / 下一跳跟踪（NHT）”。
> NHT 的实现在 `bgp_nexthop.c`（属其它卷）。本卷这两个文件是：
> **NHC = Next-Hop Capability TLV 工具**；**NHG = 给 L3 下一跳组申请 ID 段**。

### 2.1 `bgp_nhc.c` / `.h`：Next-Hop Capability (NHC) TLV

**解决什么**：BGP 可以携带一类“下一跳能力”扩展（“下一跳的下一跳节点”Next Next Hop Node，
简称 NNHN；以及 BGP ID 等），把这些 TLV 组织成一个链表挂在解析/构造上下文里。文件提供**纯链表
工具**：加、查、新、释放、计数。

**数据结构**（`bgp_nhc.h`）：
- `struct bgp_nhc_tlv`（`[bgp_nhc.h:52](bgpd/bgp_nhc.h#L52)`）：一个 TLV（`code` / `length` / `value`）。
- `struct bgp_nhc`（`[bgp_nhc.h:68](bgpd/bgp_nhc.h#L68)`）：TLV 链表的头。

**TLV 码**：`BGP_ATTR_NHC_TLV_NNHN = 2`、`BGP_ATTR_NHC_TLV_BGPID = 3`（见 `bgp_nhc.h`）。
长度宏：`BGP_NHC_TLV_MIN_LEN`（`[bgp_nhc.h:85](bgpd/bgp_nhc.h#L85)`）、
`BGP_NHC_MIN_LEN`（`[bgp_nhc.h:97](bgpd/bgp_nhc.h#L97)`）、`BGP_NHC_MIN_IPV6_LEN`（`98`）。

**函数**（`bgp_nhc.c`）：

| 函数 | 行 | 作用 |
|---|---|---|
| `bgp_nhc_tlv_add` | `[30](bgpd/bgp_nhc.c#L30)` | 把 TLV 追加到链表尾 |
| `bgp_nhc_tlv_new` | `[54](bgpd/bgp_nhc.c#L54)` | 分配并填充一个 TLV |
| `bgp_nhc_tlv_find` | `[73](bgpd/bgp_nhc.c#L73)` | 按 `code` 查 TLV |
| `bgp_nhc_tlv_free` | `[92](bgpd/bgp_nhc.c#L92)` | 释放单个 TLV |
| `bgp_nhc_tlvs_free` | `[107](bgpd/bgp_nhc.c#L107)` | 释放整条链表 |
| `bgp_nhc_free` | `[122](bgpd/bgp_nhc.c#L122)` | 释放 `struct bgp_nhc` 容器 |
| `bgp_nhc_nnhn_count` | `[138](bgpd/bgp_nhc.c#L138)` | 统计 NNHN TLV 个数 |

> **与 `ZEBRA_NHG_*` 无关**：NHC 是 BGP 报文属性层的东西，跟 zebra 不发生任何消息交换。

### 2.2 `bgp_nhg.c` / `.h`：L3 下一跳组 ID 分配器

**解决什么**：FRR 的 L3 nexthop-group（**NHG**）需要全局唯一的 ID（`nhg_id`）。zebra 为每个
路由客户端协议预留一段 ID 号段（由 `ZEBRA_NHG_PROTO_SPACING` 决定步长）。bgpd 启动时向 zebra
要一段，之后自己按段内偏移分配、释放，避免每条 NHG 都去 zebra 往返。

**关键函数**（`bgp_nhg.c`）：

| 函数 | 行 | 作用 |
|---|---|---|
| `bgp_nhg_zebra_init` | `[76](bgpd/bgp_nhg.c#L76)` | 调 `zclient_get_nhg_start(ZEBRA_ROUTE_BGP)` 取号段起点（带 `bgp_nhg_zebra_inited` 幂等保护） |
| `bgp_nhg_init` | `[96](bgpd/bgp_nhg.c#L96)` | 初始化分配器（装配 nexthop-group 增删回调） |
| `bgp_nhg_finish` | `[112](bgpd/bgp_nhg.c#L112)` | 收尾 |
| `bgp_nhg_id_alloc` | `[122](bgpd/bgp_nhg.c#L122)` | 从段内取一个未用 ID（首次调用触发 `bgp_nhg_zebra_init`） |
| `bgp_nhg_id_free` | `[139](bgpd/bgp_nhg.c#L139)` | 归还一个 ID |

**回调族**：`bgp_nhg_add_cb` `[49](bgpd/bgp_nhg.c#L49)`、`bgp_nhg_modify_cb` `[53](bgpd/bgp_nhg.c#L53)`、
`bgp_nhg_add_nexthop_cb` `[57](bgpd/bgp_nhg.c#L57)`、`bgp_nhg_del_nexthop_cb` `[62](bgpd/bgp_nhg.c#L62)`、
`bgp_nhg_del_cb` `[67](bgpd/bgp_nhg.c#L67)` —— 这些把 nexthop-group 的**命令行配置**与**内部 ID 分配**
打通（配一个 nexthop-group，就分配一个 NHG ID）。

**联动对端**：zebra（`ZEBRA_ROUTE_BGP` 的 NHG 号段）。**通信机制**：zclient 同步取号。

---

## §3 BMP：`bgp_bmp.c` / `bgp_bmp.h`（RFC 7854）

### 3.1 定位与编译

BMP（BGP Monitoring Protocol）把 bgpd 收到的/发出的 BGP 报文与路由状态，旁路送给外部采集器。
本文件编译成**独立模块** `bgpd_bmp.so`（`bgpd/subdir.am:210`，受 configure 开关 `BGP_BMP` 控制，
依赖 c-ares 以便主动外连时解析域名）。

**联动对端**：外部 BMP 采集器（如 OpenBMP 等）。
**通信机制**：BMP over TCP，两种连接角色并存——
- **`bmp_active`**（`[bgp_bmp.h:241](bgpd/bgp_bmp.h#L241)`）：bgpd **主动外连**采集器；
- **`bmp_listener`**（`[bgp_bmp.h:266](bgpd/bgp_bmp.h#L266)`）：bgpd **被动监听**，采集器连进来。
两者都归属一个 `bmp_targets`（`[bgp_bmp.h:296](bgpd/bgp_bmp.h#L296)`），即配置里的
`bmp targets <NAME>`。

### 3.2 生产者 / 队列 / 发送线程 三段式

`bgp_bmp.c` 的头部导读（同源注释）概括得清楚，本卷据此归纳：

1. **生产者（主线程）**：BGP 主线程在若干 hook 点被回调，产生待发数据。钩子注册在
   `bgp_bmp_module_init()` `[4023](bgpd/bgp_bmp.c#L4023)`（清单见 §3.4）。
2. **队列（两类）**：
   - Route Monitoring 走 `bmp_queue_entry`（`[bgp_bmp.h:123](bgpd/bgp_bmp.h#L123)`），**按键去重**
     （同前缀合并），由 `bmp_process()` `[2043](bgpd/bgp_bmp.c#L2043)` 产生。
   - Route Mirroring 走 `bmp_mirrorq`（`[bgp_bmp.h:153](bgpd/bgp_bmp.h#L153)`），**原样入队、不去重**，
     由 `bmp_mirror_packet()` `[924](bgpd/bgp_bmp.c#L924)` 产生。
3. **发送线程**：由 `bmp_wrfill()` 及其调用的 `bmp_wrmirror / bmp_wrqueue / bmp_wrqueue_locrib`
   把队列内容编码进 `struct stream`，再经 socket 写出（见 `bmp_send_bt` `[788](bgpd/bgp_bmp.c#L788)` /
   `bmp_send_bt_safe` `[797](bgpd/bgp_bmp.c#L797)`）。

**队列结构**：`struct bmp_mirrorq` `[153](bgpd/bgp_bmp.h#L153)`、`struct bmp`
`[182](bgpd/bgp_bmp.h#L182)`（每个 BMP 连接一个）。每连接发送缓冲上限由
`bmp_mirror_limit` 配置命令控制。

### 3.3 六类报文的线格式与封装点

BMP 公共头 = `bmp_common_hdr()` `[395](bgpd/bgp_bmp.c#L395)`（版本 + 长度 + 报文类型）；
per-peer 头 = `bmp_per_peer_hdr()` `[407](bgpd/bgp_bmp.c#L407)`（peer 类型、flags、distinguisher、
地址、AS、BGP ID、时间戳）。消息类型常量（`bgp_bmp.h`）：

| BMP 报文 | 类型值 | 常量 | 封装实现 |
|---|---|---|---|
| Route Monitoring | 0 | `BMP_TYPE_ROUTE_MONITORING` | `bmp_monitor()`（route monitoring 组装）、`bmp_wrqueue*` |
| Statistics Report | 1 | `BMP_TYPE_STATISTICS_REPORT` | `bmp_stats_peer()` `[2140](bgpd/bgp_bmp.c#L2140)`、`bmp_stats_cfg` 命令 |
| Peer Down Notification | 2 | `BMP_TYPE_PEER_DOWN_NOTIFICATION` | `bmp_peerstate(peer, /*down=*/true)` `[571](bgpd/bgp_bmp.c#L571)` |
| Peer Up Notification | 3 | `BMP_TYPE_PEER_UP_NOTIFICATION` | `bmp_send_peerup()` `[731](bgpd/bgp_bmp.c#L731)`（内部用 `bmp_peerstate(peer,false)`） |
| Initiation | 4 | `BMP_TYPE_INITIATION` | `bmp_send_initiation()` `[515](bgpd/bgp_bmp.c#L515)` |
| Termination | 5 | `BMP_TYPE_TERMINATION` | 关闭连接时发送 |
| （Route Mirroring） | 6 | `BMP_TYPE_ROUTE_MIRRORING` | `bmp_mirror_packet()` `[924](bgpd/bgp_bmp.c#L924)` |

**Peer Up / Down** 都用 `bmp_peerstate()`（`[571](bgpd/bgp_bmp.c#L571)`）产出，Peer Up 额外附带
本端发送的 OPEN 承载能力、以及收发的两份报文（`bmp_send_peerup` 里把本端/对端 OPEN 拼上）。
Peer Down 的原因码见 `bgp_bmp.h`（`BMP_PEER_DOWN_*`，约 415–424 行）。

**监控内容选择**（`bmp_targets->afimon[afi][safi]` 的位掩码，`bgp_bmp.h`）：
`BMP_MON_PREPOLICY`（`[316](bgpd/bgp_bmp.h#L316)`，入向策略前）、`BMP_MON_POSTPOLICY`
（`[317](bgpd/bgp_bmp.h#L317)`，入向策略后）、`BMP_MON_LOC_RIB`（`[318](bgpd/bgp_bmp.h#L318)`，本端 RIB）。
另有 `bmp_targets->mirror` 开关决定是否做 route mirroring。

**统计计数器码**（`bgp_bmp.h:430` 起）：`BMP_STATS_PFX_REJECTED=0`、`BMP_STATS_PFX_DUP_ADV=1`、
…… 直到 FRR 私有扩展 `BMP_STATS_FRR_NH_INVALID=65531`（`[bgp_bmp.h:444](bgpd/bgp_bmp.h#L444)`）。
FRR 用私有码补充 RFC 未定义项，采集器需兼容。

### 3.4 BGP 会话钩子点（`bgp_bmp_module_init`）

钩子注册清单（`[bgp_bmp.c:4023](bgpd/bgp_bmp.c#L4023)` 起）：

| 钩子 | 回调 | 作用 |
|---|---|---|
| `bgp_packet_dump` | `bmp_mirror_packet` `[924](bgpd/bgp_bmp.c#L924)` | 收/发每个 BGP 报文 → route mirroring |
| `bgp_packet_send` | `bmp_outgoing_packet` | 发报文时旁路 |
| `peer_status_changed` | `bmp_peer_status_changed` `[1117](bgpd/bgp_bmp.c#L1117)` | 邻居状态变化 → Peer Up/Down |
| `peer_backward_transition` | `bmp_peer_backward` | 状态回退（离开 Established） |
| `bgp_process` | `bmp_process` `[2043](bgpd/bgp_bmp.c#L2043)` | 路由处理 → route monitoring 入队 |
| `bgp_nht_path_update` | `bmp_nht_path_valid` | 下一跳可达性变化 |
| `bgp_inst_config_write` | `bmp_config_write` | running-config 回写 BMP 配置 |
| `bgp_inst_delete` | `bmp_bgp_del` | 实例删除清理 |
| `frr_late_init` | `bgp_bmp_init` `[3716](bgpd/bgp_bmp.c#L3716)` | 注册命令树、初始化 resolver |
| `bgp_route_update` | `bmp_route_update` | 路由更新 |
| `bgp_instance_state` | `bmp_vrf_state_changed` | 实例状态 |
| `bgp_vrf_status_changed` | `bmp_vrf_itf_state_changed` | VRF/接口状态 |
| `bgp_routerid_update` | `bmp_routerid_update` | router-id 变化 |
| `bgp_route_distinguisher_update` | `bmp_route_distinguisher_update` | RD 变化 |
| `frr_early_fini` | `bmp_bmp_early_fini` | 收尾 |

模块声明：`FRR_MODULE_SETUP(.name="bgpd_bmp", ..., .init = bgp_bmp_module_init)`
（`[bgp_bmp.c:4045](bgpd/bgp_bmp.c#L4045)` 附近）。

### 3.5 配置命令（DEFPY）

`bmp targets <NAME>` `[3102](bgpd/bgp_bmp.c#L3102)`、`no bmp targets`、
`bmp import-vrf` `[3143](bgpd/bgp_bmp.c#L3143)`、`bmp listener <ip> port <n>`
`[3201](bgpd/bgp_bmp.c#L3201)`、`bmp connect <ip> port <n>` `[3249](bgpd/bgp_bmp.c#L3249)`、
`bmp acl` `[3305](bgpd/bgp_bmp.c#L3305)`、`bmp stats` `[3332](bgpd/bgp_bmp.c#L3332)`、
`bmp stats send-experimental` `[3358](bgpd/bgp_bmp.c#L3358)`、
`bmp monitor <afi> <safi> <pre-policy|post-policy|loc-rib>` `[3380](bgpd/bgp_bmp.c#L3380)`、
`bmp mirror` `[3423](bgpd/bgp_bmp.c#L3423)`、`bmp mirror limit` `[3451](bgpd/bgp_bmp.c#L3451)`、
`show bmp` `[3491](bgpd/bgp_bmp.c#L3491)`。

---

## §4 RPKI：`bgp_rpki.c` / `bgp_rpki.h`（RFC 6810 / RFC 8210）

### 4.1 定位与编译

RPKI（资源公钥基础设施）用于**源认证**：判断某前缀“能不能由某 AS 起源”。bgpd 作为
**RPKI-RTR 客户端**，从上游 RPKI Cache 拉取 ROA（Route Origin Authorization）数据，
对本端路由做校验。

本文件编译成独立模块 `bgpd_rpki.so`（`bgpd/subdir.am:205`，configure 开关 `RPKI`，
依赖 **rtrlib** 库：`RTRLIB_CFLAGS` / `RTRLIB_LIBS`）。

**联动对端**：RPKI Cache（RTR 服务器）。**通信机制**：RPKI-RTR over TCP（也支持 SSH 传输），
协议与连接状态机**由 rtrlib 维护**（bgpd 只管配置 cache、启动/停止、接收同步数据）。

### 4.2 与 rtrlib 的关系：bgpd 负责“壳”，rtrlib 负责“协议”

- 每个 cache 节点用 `struct cache`（`[bgp_rpki.c:129](bgpd/bgp_rpki.c#L129)` 的导读定位，
  成员含 `tr_socket` `[147](bgpd/bgp_rpki.c#L147)`、`tr_config`（TCP/SSH 联合体）、
  `rtr_socket` `[152](bgpd/bgp_rpki.c#L152)`、`preference`）。分工是：
  - `tr_socket` = rtrlib 的**传输层**（建连、收发字节）；
  - `rtr_socket` = rtrlib 的**RTR 协议层**（版本协商、序列号、状态机）。
- 每个 VRF 一个 `struct rpki_vrf`（`[bgp_rpki.c:188](bgpd/bgp_rpki.c#L188)`），持有
  `rtr_mgr_config` `[189](bgpd/bgp_rpki.c#L189)`（rtrlib 会话管理器配置）与三个状态位
  `rtr_is_running` / `rtr_is_stopping` / `rtr_is_synced`（`[191–193](bgpd/bgp_rpki.c#L191)`）。
- RPKI 连接生命周期由 `rtr_is_*` 判定；**单个 cache 的 RTR 会话状态**另看
  `rtr_socket->state == RTR_ESTABLISHED`。
- rtrlib 的内存分配被嫁接到 FRR 统计：模块 init 时
  `lrtr_set_alloc_functions(malloc_wrapper, realloc_wrapper, free_wrapper)`
  （`[bgp_rpki.c:1118](bgpd/bgp_rpki.c#L1118)`）。

### 4.3 生命周期与数据同步

- `start()` `[218](bgpd/bgp_rpki.c#L218)` / `stop()` `[219](bgpd/bgp_rpki.c#L219)` /
  `reset()` `[220](bgpd/bgp_rpki.c#L220)`：初始化/停止/重启 rtr_mgr。
- 同步：rtrlib 回调 `rpki_update_cb_sync_rtr`（导读里提到）把变更的 `pfx_record` 写进
  **socketpair**；bgpd 侧的 `bgpd_sync_callback()` `[849](bgpd/bgp_rpki.c#L849)` 从 socketpair
  读出，转成 prefix 后触发重新校验。若 socketpair 写满（EAGAIN），置原子标志
  `rtr_update_overflow`（`[194](bgpd/bgp_rpki.c#L194)`）提示 bgpd 侧。
- 初次同步完成由定时器 `sync_expired` 轮询 `rtr_mgr_conf_in_sync` 判定，置 `rtr_is_synced`；
  **只有 `rtr_is_synced` 为真时校验结果才可信**（否则一律按 NOTFOUND/未知处理，避免误杀）。
- 初始化：`bgp_rpki_init()` `[1074](bgpd/bgp_rpki.c#L1074)` 注册到 `frr_late_init`
  （注册在 `bgp_rpki_module_init`）。同步套接字由 `rpki_init_sync_socket()` 建立。

### 4.4 校验与“怎样影响选路”（关键，纠一处常见误解）

**校验实现**：`rpki_validate_prefix()` `[1412](bgpd/bgp_rpki.c#L1412)`
（原型声明 `[343](bgpd/bgp_rpki.c#L343)`）。它对 (peer, attr, prefix) 走 RTR 数据源，返回三态
（`enum rpki_states`，`[bgp_rpki.h:44](bgpd/bgp_rpki.h#L44)`）：`RPKI_VALID` / `RPKI_INVALID` /
`RPKI_NOTFOUND`（另有表示“RPKI 未启用”“会话未同步”等内部值）。

**怎样被用到——不是“写回 bgp_path_info”**：

> **纠正**：RPKI 状态**不是**在路由落库时算一次、存进 `bgp_path_info` 的字段。
> 它是**按需（on demand）重算**的，通过一个 hook 暴露给选路与策略侧。

机制是：`bgp_rpki.c` 用
`hook_register(bgp_rpki_prefix_status, rpki_validate_prefix)`
（`[bgp_rpki.c:1120](bgpd/bgp_rpki.c#L1120)`）把自己注册为该校验钩子的实现；调用方在需要时
`hook_call(bgp_rpki_prefix_status, peer, attr, prefix)` 现算现用。调用点在 `bgp_route.c`：

- `[bgp_route.c:3433](bgpd/bgp_route.c#L3433)`（入向处理路径取 RPKI 状态）；
- `[bgp_route.c:11731](bgpd/bgp_route.c#L11731)`（`show` 输出去取状态，用于显示 `RPKI` 列）；
- `[bgp_route.c:14321](bgpd/bgp_route.c#L14321)`、`[bgp_route.c:15129](bgpd/bgp_route.c#L15129)`。

钩子本身用 `DECLARE_HOOK` 声明在 `bgpd.h`（`[bgpd.h:3458](bgpd/bgpd.h#L3458)`），
`DEFINE_HOOK` 在 `bgp_route.c`（`[bgp_route.c:158](bgpd/bgp_route.c#L158)`）。
功能未启用时 `rpki_validate_prefix` 返回 `RPKI_NOT_BEING_USED`。

**影响选路的两条路径**：
1. **route-map `match rpki <valid|invalid|notfound>`**：`route_match()` `[371](bgpd/bgp_rpki.c#L371)`
   是比对函数，`route_match_compile()` `[253](bgpd/bgp_rpki.c#L253)` 编译参数，
   `route_match_rpki_cmd`（`[291](bgpd/bgp_rpki.c#L291)`）把 "rpki" 关键字挂到 route-map 框架，
   `route_match_free()` `[248](bgpd/bgp_rpki.c#L248)` 释放。这样管理员能写策略**丢弃 INVALID**。
2. **选路/显示**：`bgp_route.c` 在做 bestpath 或 `show` 时，用 hook 拉取三态参与判定/展示
   （`[bgp_route.c:11731](bgpd/bgp_route.c#L11731)` 即在 route 显示路径上）。

**连接状态钩子**：`hook_register(bgp_rpki_connection_status, bgp_rpki_is_connected)`
（`[bgp_rpki.c:1121](bgpd/bgp_rpki.c#L1121)`），供别处查询某 VRF 是否已连上 RTR。

### 4.5 模块装载与 CLI

`bgp_rpki_module_init()` `[1114](bgpd/bgp_rpki.c#L1114)` 里注册的钩子：
`bgp_rpki_prefix_status`→`rpki_validate_prefix`（`[1120](bgpd/bgp_rpki.c#L1120)`）、
`bgp_rpki_connection_status`（`[1121](bgpd/bgp_rpki.c#L1121)`）、
`frr_late_init`→`bgp_rpki_init`（`[1122](bgpd/bgp_rpki.c#L1122)`）、
`frr_early_fini`→`bgp_rpki_fini`（`[1123](bgpd/bgp_rpki.c#L1123)`），以及配置回写钩子
（`bgp_hook_config_write_debug` `[1124](bgpd/bgp_rpki.c#L1124)`、`bgp_hook_vrf_update`
`[1125](bgpd/bgp_rpki.c#L1125)`、`bgp_hook_config_write_vrf` `[1126](bgpd/bgp_rpki.c#L1126)`）。
模块声明 `FRR_MODULE_SETUP(.name="bgpd_rpki", .version="0.3.6", ..., .init=bgp_rpki_module_init)`
（`[bgp_rpki.c:3162](bgpd/bgp_rpki.c#L3162)`）。

**主要 CLI（DEFPY/DEFUN）**：`rpki`（进入 RPKI 配置视图）`[1991](bgpd/bgp_rpki.c#L1991)`、
`no rpki` `[2028](bgpd/bgp_rpki.c#L2028)`、`rpki start` `[2060](bgpd/bgp_rpki.c#L2060)`、
`rpki stop` `[2090](bgpd/bgp_rpki.c#L2090)`、`rpki polling-period` `[2109](bgpd/bgp_rpki.c#L2109)`、
`rpki expire-interval` `[2155](bgpd/bgp_rpki.c#L2155)`、`rpki retry-interval` `[2206](bgpd/bgp_rpki.c#L2206)`、
`rpki cache tcp ...` `[2253](bgpd/bgp_rpki.c#L2253)`、`rpki cache ssh ...` `[2311](bgpd/bgp_rpki.c#L2311)`、
`no rpki cache ...` `[2379](bgpd/bgp_rpki.c#L2379)`、`show rpki prefix-table` `[2437](bgpd/bgp_rpki.c#L2437)`、
`show rpki prefix` `[2525](bgpd/bgp_rpki.c#L2525)`、`show rpki as-number` `[2479](bgpd/bgp_rpki.c#L2479)`、
`show rpki cache-server` `[2622](bgpd/bgp_rpki.c#L2622)`、`show rpki cache-connection` `[2751](bgpd/bgp_rpki.c#L2751)`、
`show rpki configuration` `[2891](bgpd/bgp_rpki.c#L2891)`、`rpki reset` `[2967](bgpd/bgp_rpki.c#L2967)`、
`debug rpki` `[3007](bgpd/bgp_rpki.c#L3007)`。

---

## §5 SNMP：`bgp_snmp*.c` / `.h`

### 5.1 定位与编译（也是 .so 模块）

SNMP 支持编译成独立模块 `bgpd_snmp.so`（`bgpd/subdir.am:200`：`bgpd_snmp_bgp4.c`
`bgpd_snmp_bgp4v2.c` `bgp_snmp.c` `bgp_mplsvpn_snmp.c`），configure 开关 `SNMP`。
bgpd 作为 SNMP **子代理（sub-agent）**，经 **AgentX** 协议向 **snmpd**（master agent）注册 MIB。

**联动对端**：`snmpd`（AgentX master）。**通信机制**：AgentX over Unix socket，
由 lib 的 `smux` 层封装——`smux_init(tm)` 建立通道并挂到事件循环
（`[bgp_snmp.c:197](bgpd/bgp_snmp.c#L197)`）。

### 5.2 三份 MIB 与文件对应

| MIB | RFC | OID 前缀 | 实现文件 |
|---|---|---|---|
| BGP4-MIB | RFC 4273 | `1.3.6.1.2.1.15`（mib-2.bgp） | `bgp_snmp_bgp4.c/.h` |
| BGP4V2-MIB | RFC 4382 | `1.3.6.1.3.5.1` | `bgp_snmp_bgp4v2.c/.h` |
| （MPLS VPN 相关） | — | — | `bgp_mplsvpn_snmp.c`（不在本卷） |

- BGP4-MIB OID：`#define BGP4MIB 1,3,6,1,2,1,15`（`[bgp_snmp_bgp4.h:45](bgpd/bgp_snmp_bgp4.h#L45)`）。
  定义 `bgpPeerTable`（列 1–23）与 `bgp4PathAttrTable`（列 1–14）。
- BGP4V2-MIB OID：`#define BGP4V2MIB 1,3,6,1,3,5,1`（`[bgp_snmp_bgp4v2.h:35](bgpd/bgp_snmp_bgp4v2.h#L35)`）。
  这是 RFC 4382 的**新版**，用**统一表**（peer 表 13 列、错误表、事件时间表、NLRI 表 25 列）
  同时覆盖 IPv4/IPv6，取代 bgp4 的“v4 专用 + 地址长度硬编码”。

**二者区别**：bgp4（4273）是老式、IPv4 中心（peer 表按 IP 索引）；bgp4v2（4382）是
**地址族无关**的新表结构，索引里带 AFI，因此能把 IPv4/IPv6 邻居放进同一张表。FRR 两个都实现，
由 trap 开关分别控制。

### 5.3 trap 开关：`BGP_OPT_TRAPS_*`

配置命令（`bgp_snmp.c`）：

| 命令 | 行 | 控制位 |
|---|---|---|
| `bgp snmp traps rfc4273` | `[78](bgpd/bgp_snmp.c#L78)` | `BGP_OPT_TRAPS_RFC4273` |
| `bgp snmp traps rfc4382` | `[95](bgpd/bgp_snmp.c#L95)` | `BGP_OPT_TRAPS_RFC4382` |
| `bgp snmp traps bgp4-mibv2` | `[112](bgpd/bgp_snmp.c#L112)` | `BGP_OPT_TRAPS_BGP4MIBV2` |

这些位来自 `bm->options`（进程级位集）。**注意是“开关位”，不是“立即发 trap”**——具体发送由
发送侧的 trap 函数按位判断（如 `[bgp_snmp.c:170](bgpd/bgp_snmp.c#L170)`、`[183](bgpd/bgp_snmp.c#L183)`）。
`bgp_snmp_init()` `[195](bgpd/bgp_snmp.c#L195)` 在 `frr_late_init` 阶段把 `RFC4273` 与 `RFC4382`
默认打开（`[136](bgpd/bgp_snmp.c#L136)`、`[139](bgpd/bgp_snmp.c#L139)`）；bgp4v2 默认关闭。

### 5.4 初始化与钩子、两个 trap

- `bgp_snmp_module_init()` `[207](bgpd/bgp_snmp.c#L207)` 注册：
  `hook_register(peer_status_changed, bgpTrapEstablished)`（`[209](bgpd/bgp_snmp.c#L209)`）、
  `hook_register(peer_backward_transition, bgpTrapBackwardTransition)`（`[210](bgpd/bgp_snmp.c#L210)`）、
  `hook_register(frr_late_init, bgp_snmp_init)`（`[211](bgpd/bgp_snmp.c#L211)`）、
  `hook_register(bgp_snmp_traps_config_write, ...)`（`[212](bgpd/bgp_snmp.c#L212)`）。
  `FRR_MODULE_SETUP(.name="bgpd_snmp", ...)` `[219](bgpd/bgp_snmp.c#L219)`。
- 两个 trap 的**外层分发**：`bgpTrapEstablished()`（`[168](bgpd/bgp_snmp.c#L168)` 附近）与
  `bgpTrapBackwardTransition()`（`[181](bgpd/bgp_snmp.c#L181)` 附近），它们按开关注位后转调各 MIB。
- **bgp4 侧**：变量表 `bgp_variables[]` `[678](bgpd/bgp_snmp_bgp4.c#L678)`；
  `bgp4TrapEstablished` `[856](bgpd/bgp_snmp_bgp4.c#L856)`、
  `bgp4TrapBackwardTransition` `[883](bgpd/bgp_snmp_bgp4.c#L883)`；
  注册 `REGISTER_MIB("mibII/bgp", bgp_variables, variable, bgp_oid)`
  `[bgp_snmp_bgp4.c:906](bgpd/bgp_snmp_bgp4.c#L906)`。
- **bgp4v2 侧**：变量表 `bgpv2_variables[]` `[1030](bgpd/bgp_snmp_bgp4v2.c#L1030)`；
  `bgpv2TrapEstablished` `[1652](bgpd/bgp_snmp_bgp4v2.c#L1652)`（先查 `BGP_OPT_TRAPS_BGP4MIBV2`，
  `[1657](bgpd/bgp_snmp_bgp4v2.c#L1657)`）、`bgpv2TrapBackwardTransition` `[1692](bgpd/bgp_snmp_bgp4v2.c#L1692)`；
  注册 `REGISTER_MIB("mibII/bgpv2", ...)` `[bgp_snmp_bgp4v2.c:1729](bgpd/bgp_snmp_bgp4v2.c#L1729)`。
- `bgp_snmp.h` 定义 ASN.1 类型别名（`ASN_INTEGER` / `ASN_COUNTER32` / `ASN_GAUGE32` /
  `ASN_TIMETICKS` / `ASN_IPADDRESS` / `ASN_OCTET_STR` 等），供变量表声明列类型。

> **只读旁路**：SNMP 模块不改协议状态，只在 hook 被调用时发 trap、在查询时读 bgp 状态。

---

## §6 脚本钩子：`bgp_script.c` / `bgp_script.h`

### 6.1 定位与编译开关

FRR 支持用 **Lua** 写脚本钩子（`--enable-scripting`）。本文件是 **bgpd 侧的类型编解码桥**：
把 bgpd 的对象（`struct peer`、`struct attr`）在 C 与 Lua 之间转换，交给 `frrscript` 框架执行脚本。

**编译开关**：整个文件体在 `#ifdef HAVE_SCRIPTING` 内（`[bgp_script.c:43](bgpd/bgp_script.c#L43)`）；
未开脚本时为空。头文件同理（`bgp_script.h` 也整体包在 `HAVE_SCRIPTING` 内）。注意
`bgp_script.c` 属主库 `libbgp.a`（不是单独 .so），但**代码块被宏裁掉**。

**联动对端**：无外部进程——**进程内**内嵌 Lua 解释器。**通信机制**：Lua C API（frrscript 封装）。

### 6.2 编解码器

| 函数 | 行 | 方向 | 作用 |
|---|---|---|---|
| `lua_pushpeer` | `[64](bgpd/bgp_script.c#L64)` | C→Lua | 把 peer 字段打包成 Lua 表（只读） |
| `lua_pushattr` | `[180](bgpd/bgp_script.c#L180)` | C→Lua | 把 attr 打包成 Lua 表 |
| `lua_decode_attr` | `[200](bgpd/bgp_script.c#L200)` | Lua→C | 把栈上 Lua 表**回填**到已有 attr（不分配） |
| `lua_toattr` | `[222](bgpd/bgp_script.c#L222)` | Lua→C | 用栈上 Lua 表**新建**一个 attr |

**注册表** `frrscript_codecs_bgpd[]` `[234](bgpd/bgp_script.c#L234)`：
把类型名映射到 encoder/decoder。注意当前表里 ——
- `"peer"` 只有 **encoder**（脚本能读 peer，不能改）；
- `"attr"` 有 **encoder + decoder**（脚本能读**也能改**属性，改完回传给流程）。

**注册入口** `bgp_script_init()` `[245](bgpd/bgp_script.c#L245)`：
`frrscript_register_type_codecs(frrscript_codecs_bgpd)`。

### 6.3 哪些事件触发外部脚本

> **本仓库中未找到“事件→脚本”的直接映射表**。本文件只提供**类型编解码**；真正的
> “哪个事件触发哪个脚本”由 **frrscript 框架 + 各调用点**决定（脚本钩子的注册与调用
> 分散在消息/路由处理路径，属其它卷）。因此本卷的准确结论是：
> **`bgp_script.c` 不定义触发点，它定义的是“脚本能看见/能改什么”**（peer 只读、attr 可读写）。
> 属性编解码使得脚本可对 inbound/outbound 路由属性做变换，这是 Lua 钩子最典型的用法。

---

## §7 MRT 导出与转换：`bgp_dump.c` / `.h` 与 `bgp_btoa.c`

### 7.1 `bgp_dump.c`：MRT 文件写出（RFC 6396）

**定位**：把 bgpd 的**路由表快照**（TABLE_DUMP_V2）与**BGP 报文/状态变化**（BGP4MP）按 MRT 格式
写文件，供事后分析（`bgpd/bgp_btoa` 或 `bgpdump` 读取）。它是主库的一部分（`bgp_dump.c` 在
`libbgp.a` 里，`subdir.am:24` 段）。

**数据结构**：
- dump 类别枚举 `enum bgp_dump_type` `[75](bgpd/bgp_dump.c#L75)`（CLI 关键字层，**不是** MRT type）。
- 关键字↔枚举对照 `bgp_dump_type_map[]` `[88](bgpd/bgp_dump.c#L88)`。
- 运行期句柄 `struct bgp_dump` `[126](bgpd/bgp_dump.c#L126)`（`type` `[127](bgpd/bgp_dump.c#L127)`、
  文件句柄、interval 等）。

**写记录的关键函数**：

| 函数 | 行 | 作用 |
|---|---|---|
| `bgp_dump_open_file` | `[171](bgpd/bgp_dump.c#L171)` | 打开/滚动文件 |
| `bgp_dump_interval_add` | `[227](bgpd/bgp_dump.c#L227)` | 定时转储（interval 场景） |
| `bgp_dump_header` | `[267](bgpd/bgp_dump.c#L267)` | 写 MRT 公共头（时间戳/type/subtype/len） |
| `bgp_dump_set_size` | `[298](bgpd/bgp_dump.c#L298)` | 回填长度字段 |
| `bgp_dump_routes_index_table` | `[319](bgpd/bgp_dump.c#L319)` | TABLE_DUMP_V2 的 PEER_INDEX_TABLE |
| `bgp_dump_route_node_record` | `[421](bgpd/bgp_dump.c#L421)` | 写一条 RIB 记录 |
| `bgp_dump_routes_func` | `[533](bgpd/bgp_dump.c#L533)` | 遍历整表写快照 |
| `bgp_dump_interval_func` | `[580](bgpd/bgp_dump.c#L580)` | 定时回调 |
| `bgp_dump_common` | `[611](bgpd/bgp_dump.c#L611)` | 公共封装 |
| `bgp_dump_state` | `[660](bgpd/bgp_dump.c#L660)` | 邻居状态变化记录 |
| `bgp_dump_packet_func` | `[695](bgpd/bgp_dump.c#L695)` | 单条报文记录 |
| `bgp_dump_packet` | `[748](bgpd/bgp_dump.c#L748)` | `bgp_packet_dump` hook 回调入口 |

**生命周期与配置**：
- `bgp_dump_init()` `[1049](bgpd/bgp_dump.c#L1049)`：注册
  `hook_register(bgp_packet_dump, bgp_dump_packet)` `[1063](bgpd/bgp_dump.c#L1063)` 与
  `hook_register(peer_status_changed, bgp_dump_state)` `[1064](bgpd/bgp_dump.c#L1064)`。
- `bgp_dump_finish()` `[1069](bgpd/bgp_dump.c#L1069)`：收尾落盘。
- 命令：`dump bgp all|all-et|updates|updates-et|routes-mrt ...`（`dump_bgp_all`
  `[897](bgpd/bgp_dump.c#L897)`）、`no dump bgp ...` `[947](bgpd/bgp_dump.c#L947)`、
  视图节点 `bgp_dump_node` `[992](bgpd/bgp_dump.c#L992)`、回写 `config_write_bgp_dump`。

**格式常量**（`bgp_dump.h`）：MRT type `MSG_PROTOCOL_BGP4MP = 16` `[46](bgpd/bgp_dump.h#L46)`、
`MSG_PROTOCOL_BGP4MP_ET = 17` `[47](bgpd/bgp_dump.h#L47)`；BGP4MP 子类型 `BGP4MP_MESSAGE=1`
等（`[53](bgpd/bgp_dump.h#L53)` 起，含 AS4/ADDPATH 变体）；记录头长
`BGP_DUMP_HEADER_SIZE = 12` `[65](bgpd/bgp_dump.h#L65)`、
`BGP_DUMP_MSG_HEADER = 40` `[66](bgpd/bgp_dump.h#L66)`；
TABLE_DUMP_V2 子类型 `TABLE_DUMP_V2_PEER_INDEX_TABLE=1` / `...RIB_IPV4_UNICAST=2` …
（`[72](bgpd/bgp_dump.h#L72)` 起）。

### 7.2 `bgp_btoa.c`：MRT → 文本的独立工具

**定位**：**不是** bgpd 常驻组件，而是独立命令行工具（`subdir.am:8`：`noinst_PROGRAMS += bgpd/bgp_btoa`），
binary→ASCII，把 `bgp_dump.c` 写出的 MRT 文件读成可读文本。

**关键点**：
- `main()` `[153](bgpd/bgp_btoa.c#L153)`：主循环先读 **12 字节**记录头
  （`stream_read(s, fd, 12)` `[189](bgpd/bgp_btoa.c#L189)`），解析时间戳/type/subtype/len，
  再按 len 读记录体（`[253](bgpd/bgp_btoa.c#L253)`）。
- `attr_parse()` `[91](bgpd/bgp_btoa.c#L91)`：解析记录体里的 BGP 路径属性段，只对
  `ORIGIN` / `AS_PATH` / `NEXT_HOP` 做解析并打印，其余按长度跳过
  （`stream_getw_from`，`[139](bgpd/bgp_btoa.c#L139)`）；AS_PATH 复用 `aspath_parse`
  （`[124](bgpd/bgp_btoa.c#L124)`）配合 `bgp_get_asnotation(NULL)`。
- 它**假定输入格式正确、不做健壮性校验**（导读明确提示畸形文件可能越界）——只能用于
  可信来源的 dump 文件。

**两工具的关系**：`bgp_dump.c` 写（生产），`bgp_btoa.c` 读（消费）。二者共用
`bgp_dump.h` 的类型/子类型常量与 `bgp_attr.h` 的属性类型常量。

---

## §8 配置面：`bgp_vty.c` / `bgp_vty.h`（约 24654 行，全模块最大文件）

### 8.1 两段式：命令定义 vs 命令注册

FRR 的 CLI 是**两段式**（`bgp_vty.c` 头部导读）：

1. **命令定义**：源码里用 `DEFUN` / `DEFPY` / `DEFUN_NOSH` 宏声明“语法字符串 + 处理函数”。
   **这些宏不产生运行期命令表**——它们在 build 期被 python 脚本扫描，生成 `command.c` 里的
   命令结构。
2. **命令注册**：运行期由 `bgp_vty_init()` 用 `install_element(node, &cmd)` 把命令挂到某个
   “节点”（视图）。node 决定该命令在哪个视图可见。

**本文件规模统计（实测）**：
- 命令定义宏：`DEFUN` 288 处、`DEFUN_NOSH` 8 处、`DEFUN_HIDDEN` 4 处、`DEFPY` 108 处。
- `install_element(...)` 调用 **1173** 处（因为同一条命令常挂到多个 AF 子节点）。
- 文件总行数 **24654**。

> **读本文件要始终带着两问**：这条命令属于**哪个 node**？它改完字段**是否触发了 apply**
> （如 `bgp_peer_activate` / `peer_group2peer_config_copy` / `bgp_zebra_send_capabilities`）？
> 漏 apply 的典型症状是“配置看着生效了，但会话/路由没变”。

### 8.2 内部结构地图（按文件从上到下）

下表行号为实测：

| 区段 | 行 | 内容 |
|---|---|---|
| 顶部默认值 | 66 起 | 一堆 `FRR_CFG_DEFAULT_BOOL`（按 profile/版本覆盖的默认值） |
| CLI 辅助函数 | ~150–950 | `get_afi_safi_str`、argv 解析、peer 查找、`bgp_vty_return`、`bgp_get_vty` |
| `bgp_get_vty` | `[806](bgpd/bgp_vty.c#L806)` | 按 vty 输入取得/创建 bgp 实例 |
| `bgp_vty_find_and_parse_afi_safi_bgp` | `[898](bgpd/bgp_vty.c#L898)` | 一次性解析 afi+safi+bgp |
| `peer_lookup_vty` | `[1016](bgpd/bgp_vty.c#L1016)` | 按字符串查邻居（先 peer 后 group） |
| clear 实现 | ~1000–1400 | `bgp_clear_*`、soft-in/soft-out（`bgp_clear_soft_in` `[1651](bgpd/bgp_vty.c#L1651)`） |
| peer flag set/unset | ~1400–1900 | `peer_flag_modify_vty`、是否需监听 |
| 海量 DEFUN/DEFPY | ~1900–12000 | bgp 全局、neighbor、address-family、redistribute、GR、vpn import/export 等 |
| show 命令族 | ~12000–18400 | summary/neighbors/peer-group/update-groups/route-leak/martian 打印 |
| config-write 族 | ~18700–20900 | `bgp_config_write_*` 与各 cmd_node |
| `bgp_config_write` | `[20659](bgpd/bgp_vty.c#L20659)` | running-config 总回写（递归各 AF） |
| cmd_node 定义 | `[21266](bgpd/bgp_vty.c#L21266)` 起 | 14 个 bgp 相关视图节点 |
| `bgp_vty_init` | `[21714](bgpd/bgp_vty.c#L21714)` | **所有 install_element 的总入口**（~2600 行） |
| community-list 族 | `[23417](bgpd/bgp_vty.c#L23417)` | community / large-community / extended-community-list |

### 8.3 `bgp_vty_init()` 的分段地图（“命令→节点”映射清单）

`bgp_vty_init()` 从 `[21714](bgpd/bgp_vty.c#L21714)` 开始，函数体极长，可按内嵌的
`【段：...】` 注释切成几大块（行号为段注释所在行）：

| 段 | 行 | 内容 |
|---|---|---|
| 变量补全 / 配置事务回调 | `[21716](bgpd/bgp_vty.c#L21716)` | `cmd_variable_handler_register(bgp_var_neighbor/peergroup)`、`cmd_init_config_callbacks(bgp_config_start, bgp_config_end)` |
| 注册各视图节点 | `[21721](bgpd/bgp_vty.c#L21721)` | `install_node(&bgp_node)` 等 14 个节点 + `install_default` |
| CONFIG_NODE 全局 bgp 命令 | `[21755](bgpd/bgp_vty.c#L21755)` | inq/outq-limit、local-mac、suppress-fib-pending、GR 等**进程级**参数 |
| router bgp 实例命令 | `[21814](bgpd/bgp_vty.c#L21814)` | 注册到 BGP_NODE / 各 AF 子节点，只作用某实例 |
| neighbor 命令族 | `[22106](bgpd/bgp_vty.c#L22106)` | **最长的一段**，neighbor 相关命令 |
| address-family 与按 AF 注册 | `[23201](bgpd/bgp_vty.c#L23201)` | `address_family_*` 命令 + 各 AF 子节点注册 |
| clear 命令 | `[23230](bgpd/bgp_vty.c#L23230)` | 注册到 ENABLE_NODE |
| show 命令 | `[23240](bgpd/bgp_vty.c#L23240)` | 注册到 VIEW_NODE |
| redistribute 与收尾注册 | `[23270](bgpd/bgp_vty.c#L23270)` | redistribute 命令 + 收尾 |
| SID / SRv6 导出 | `[23389](bgpd/bgp_vty.c#L23389)` | RD/SID 导出为某 AF 路由 |
| 接口命令收尾 | `[23408](bgpd/bgp_vty.c#L23408)` | interface 视图、mpls bgp forwarding |
| 团体属性列表 | `[23417](bgpd/bgp_vty.c#L23417)` | community-list / large-community-list 等 |
| large-community | `[23787](bgpd/bgp_vty.c#L23787)` | RFC 8092 |
| extended-community | `[24178](bgpd/bgp_vty.c#L24178)` | RT/Soo 等 |

### 8.4 cmd_node 层级（14 个视图）

cmd_node 描述一个 CLI 视图：`name` / `node`（枚举）/ `parent_node`（决定如何进入）/ `prompt` /
可选 `config_write` 回调。定义从 `[bgp_vty.c:21266](bgpd/bgp_vty.c#L21266)` 起：

| cmd_node | node | 行 |
|---|---|---|
| `bgp_node` | BGP_NODE（提示符 `(config-router)#`） | `[21266](bgpd/bgp_vty.c#L21266)` |
| `bgp_ipv4_unicast_node` | BGP_IPV4_NODE | `[21274](bgpd/bgp_vty.c#L21274)` |
| `bgp_ipv4_multicast_node` | BGP_IPV4M_NODE | `[21282](bgpd/bgp_vty.c#L21282)` |
| `bgp_ipv4_labeled_unicast_node` | BGP_IPV4L_NODE | `[21290](bgpd/bgp_vty.c#L21290)` |
| `bgp_ipv6_unicast_node` | BGP_IPV6_NODE | `[21298](bgpd/bgp_vty.c#L21298)` |
| `bgp_ipv6_multicast_node` | BGP_IPV6M_NODE | `[21306](bgpd/bgp_vty.c#L21306)` |
| `bgp_ipv6_labeled_unicast_node` | BGP_IPV6L_NODE | `[21314](bgpd/bgp_vty.c#L21314)` |
| `bgp_vpnv4_node` | BGP_VPNV4_NODE | `[21322](bgpd/bgp_vty.c#L21322)` |
| `bgp_vpnv6_node` | BGP_VPNV6_NODE | `[21330](bgpd/bgp_vty.c#L21330)` |
| `bgp_evpn_node` | BGP_EVPN_NODE | `[21338](bgpd/bgp_vty.c#L21338)` |
| `bgp_evpn_vni_node` | BGP_EVPN_VNI_NODE | `[21346](bgpd/bgp_vty.c#L21346)` |
| `bgp_flowspecv4_node` | BGP_FLOWSPECV4_NODE | `[21353](bgpd/bgp_vty.c#L21353)` |
| `bgp_flowspecv6_node` | BGP_FLOWSPECV6_NODE | `[21361](bgpd/bgp_vty.c#L21361)` |
| `bgp_srv6_node` | BGP_SRV6_NODE | `[21369](bgpd/bgp_vty.c#L21369)` |

另有 `community_list_node` `[24580](bgpd/bgp_vty.c#L24580)` 与
`community_alias_node` `[24637](bgpd/bgp_vty.c#L24637)`。

**关键易错点**：**只有顶层 `bgp_node` 挂了 `config_write = bgp_config_write`**
（`[21271](bgpd/bgp_vty.c#L21271)`）；各 AF 子节点**不再各自挂回调**，由 `bgp_config_write()`
内部递归输出。所以改“配置写回”要改 `bgp_config_write`，而不是改这些 node。

### 8.5 与北向接口（YANG / northbound）的关系

**结论：各走各的入口，最终写同一批字段。**

- `bgp_vty.c` 是**传统 VTY-only** 配置入口。绝大多数 AF 子节点带 `no_xpath = true`
  （如 `[21279](bgpd/bgp_vty.c#L21279)`、`[21287](bgpd/bgp_vty.c#L21287)` ……），
  意指“该节点不参与基于 XPath 的 northbound 配置”。全文件里 **`northbound` / `yang` 相关匹配
  仅出现在注释**（`[21262](bgpd/bgp_vty.c#L21262)` 一处说明 `no_xpath` 的含义），**代码本身不引入
  northbound**。
- 真正基于 YANG 的 northbound 配置实现在 **`bgp_nb*.c`**（生成自 `yang/frr-bgp*.yang`），
  属其它卷（见总览篇文件地图与 `bgpd/subdir.am` 的 yang 段）。
- 两者**共享同一份配置字段**（bgp/peer/af 结构体的成员）：VTY 命令与 northbound 回调最终都是
  这些字段的读写者。区别在**事务机制**：VTY 走 `bgp_config_start/end`
  （`cmd_init_config_callbacks`，`[21719](bgpd/bgp_vty.c#L21719)`），northbound 走两阶段
  apply（`nb_cb_*`）。因此**同一命令可能有两个入口**，改动字段时要同时顾及。

### 8.6 `bgp_vty.h` 要点

- **命令字符串宏**：`BGP_INSTANCE_HELP_STR` `[70](bgpd/bgp_vty.h#L70)`、
  `BGP_AFI_CMD_STR = "<ipv4|ipv6>"` `[80](bgpd/bgp_vty.h#L80)`、
  `BGP_SAFI_CMD_STR` `[82](bgpd/bgp_vty.h#L82)`、`BGP_AFI_SAFI_CMD_STR` `[85](bgpd/bgp_vty.h#L85)`、
  `BGP_SAFI_WITH_LABEL_CMD_STR` `[88](bgpd/bgp_vty.h#L88)`。改这些宏会**同时**改所有引用命令的
  语法与 help，务必谨慎。
- **默认值档案**：`FRR_CFG_DEFAULT_ULONG(BGP_KEEPALIVE, ...)` `[54](bgpd/bgp_vty.h#L54)`、
  `BGP_HOLDTIME` `[58](bgpd/bgp_vty.h#L58)`、`BGP_CONNECT_RETRY` `[62](bgpd/bgp_vty.h#L62)`
  ——按 profile 覆盖（如 datacenter 档案 keepalive=3）。
- **GR 同步宏族**（`VTY_BGP_GR_*`）：`VTY_BGP_GR_ROUTER_DETECT` `[146](bgpd/bgp_vty.h#L146)`、
  `VTY_SEND_BGP_GR_CAPABILITY_TO_ZEBRA` `[156](bgpd/bgp_vty.h#L156)`、
  `VTY_BGP_GR_ROUTER_DETECT_AND_SEND_CAPABILITY_TO_ZEBRA` `[170](bgpd/bgp_vty.h#L170)`。
  **这些宏有副作用**（真会调 `bgp_zebra_send_capabilities()`），不是纯字符串宏。
- 解析辅助函数原型：`bgp_vty_return`、`peer_and_group_lookup_vty`、`argv_find_and_parse_afi/safi`、
  `bgp_vty_find_and_parse_afi_safi_bgp`、`bgp_vty_find_and_parse_bgp`、
  `bgp_show_summary_vty`、`peergroup_flag_check` 等（见 `bgp_vty.h` 后半段）。

---

## §9 调试面：`bgp_debug.c` / `bgp_debug.h`

### 9.1 debug 开关的“双态”模型

`bgp_debug.h` 定义了两套并存的 debug 状态位——这是理解本文件的关键：

- `conf_bgp_debug_*`：**配置态**（写进 running-config、持久化）。
- `term_bgp_debug_*`：**终端态**（`terminal monitor` 会话级、临时）。

宏（`bgp_debug.h`）：`DEBUG_ON(conf, term, ...)` / `DEBUG_OFF(...)`（约 249–258 行）、
`TERM_DEBUG_ON`（约 242 行）、`CONF_DEBUG_ON`（约 236 行）、以及判断宏
`BGP_DEBUG(...)` / `CONF_BGP_DEBUG(...)`（约 265–266 行）。
`struct bgp_debug_filter`（约 186 行）支持“按 peer / 按前缀”的过滤列表。`DUMP_*` 是报文 dump
的开关位（约 65–74 行）。

### 9.2 命令层级（debug 族）

`bgp_debug.c` 里 debug 命令覆盖多个维度，从 `[889](bgpd/bgp_debug.c#L889)` 起：
`debug bgp as4` `[889](bgpd/bgp_debug.c#L889)`、`as4-segment` `[929](bgpd/bgp_debug.c#L929)`、
`neighbor-events` `[972](bgpd/bgp_debug.c#L972)`（含 detail/peer 变体 `[994](bgpd/bgp_debug.c#L994)`、
`[1024](bgpd/bgp_debug.c#L1024)`）、`nht` `[1134](bgpd/bgp_debug.c#L1134)`、
`keepalive` `[1175](bgpd/bgp_debug.c#L1175)`、`bestpath` `[1299](bgpd/bgp_debug.c#L1299)`、
`update` `[1402](bgpd/bgp_debug.c#L1402)`（detail `[1427](bgpd/bgp_debug.c#L1427)`、direct
`[1456](bgpd/bgp_debug.c#L1456)`、prefix `[1865](bgpd/bgp_debug.c#L1865)`…）、
`zebra` `[1980](bgpd/bgp_debug.c#L1980)`、`graceful-restart` `[1999](bgpd/bgp_debug.c#L1999)`、
`aggregate` `[2138](bgpd/bgp_debug.c#L2138)`、`update-groups` `[2251](bgpd/bgp_debug.c#L2251)`、
`vpn` `[2291](bgpd/bgp_debug.c#L2291)`、`pbr` `[2387](bgpd/bgp_debug.c#L2387)`、
`evpn-mh` `[2451](bgpd/bgp_debug.c#L2451)`、`labelpool` `[2505](bgpd/bgp_debug.c#L2505)`、
`bfd` `[2551](bgpd/bgp_debug.c#L2551)`、`conditional-advertisement` `[2584](bgpd/bgp_debug.c#L2584)`，
以及总开关 `no debug bgp` `[2617](bgpd/bgp_debug.c#L2617)`。

**展示**：`show debugging bgp` 是 `DEFUN_NOSH`（`[2674](bgpd/bgp_debug.c#L2674)`），
**注册**：`bgp_debug_init()` `[2950](bgpd/bgp_debug.c#L2950)` 把命令挂到
`ENABLE_NODE` 与 `CONFIG_NODE`（如 `[2954](bgpd/bgp_debug.c#L2954)`、`[2957](bgpd/bgp_debug.c#L2957)`）。
**回写**：`bgp_config_write_debug()` `[2781](bgpd/bgp_debug.c#L2781)`
（前置声明在 `[2933](bgpd/bgp_debug.c#L2933)`）。
**入口**：`bgp_debug_init()` 由主程序调用（`bgpd.c:10589`）。

### 9.3 与 `terminal monitor` 的关系（重要，别混淆）

> **`terminal monitor` 不是 bgp_debug.c 实现的。** 它在 **lib/vty.c**：
> `DEFUN_NOSH(terminal_monitor, ...)` `lib/vty.c:3541`（"terminal monitor [detach]"），
> `no terminal monitor` `lib/vty.c:3573`，别名 `terminal no monitor` `lib/vty.c:3584`，
> 注册到 `ENABLE_NODE`（`lib/vty.c:3896`）。

关系是**“开关”与“管道”的分工**：

- `bgp_debug.c` 决定**哪些事件产生日志**（通过 conf/term 两套 debug 位、以及 `BGP_DEBUG` 判断）；
- lib 的 `terminal monitor` 决定**日志往哪去**：它把当前 VTY 会话登记为 `live_log` 目标，
  于是经 zlog 的日志会**实时复制**到这个终端（`lib/vty.c` 的 live-log 机制，
  见 `lib/vty.h` 的 `live_log` 字段说明）。

**典型症状对应**：开了 `debug bgp update` 但没开 `terminal monitor` → 日志只进 syslog/文件，
终端上看不到；反之开了 `terminal monitor` 但没开任何 debug → 终端只有常规日志。两者要**同时**
满足才“终端刷 debug”。

---

## §10 源码注释与代码不符清单

按写作体例要求，专设一节列出“注释说 X、代码实际是 Y”之处（均为实测）。本卷范围内发现的：

1. **`bgp_nhc.c` 不是“下一跳缓存（Next Hop Cache）”。** 若按文件名/任务提示望文生义会误解。
   注释与代码一致地表明它是 **Next-Hop Capability TLV** 工具（`bgp_nhc_tlv_add` 等，
   `[bgp_nhc.c:30](bgpd/bgp_nhc.c#L30)`）。真正“下一跳缓存/跟踪”在 `bgp_nexthop.c`。

2. **RPKI 状态不写回 `bgp_path_info`。** 任务提示与常见说法是“校验状态写到 path_info”，
   但代码是**按需重算**：`hook_register(bgp_rpki_prefix_status, rpki_validate_prefix)`
   （`[bgp_rpki.c:1120](bgpd/bgp_rpki.c#L1120)`）+ 调用点 `hook_call(...)`
   （`[bgp_route.c:11731](bgpd/bgp_route.c#L11731)` 等）。`bgp_path_info` 上**没有**存 RPKI 三态的字段。

3. **`bgp_dump.c` 的 `enum bgp_dump_type` 不是 MRT 的 type 字段。** 注释显式提醒
   （`[bgp_dump.c:71](bgpd/bgp_dump.c#L71)`）：它是 **CLI 层“dump 类别”** 枚举，与
   `MSG_PROTOCOL_BGP4MP` 等 MRT 线格式类型是两码事。

4. **`BGP_SHOW_PEER_GR_CAPABILITY` 宏第 5 个调用用形参 `use_json` 而非参数 `json`**
   （`bgp_vty.h` 注释自己把它标为“疑点/待确认”，`[bgp_vty.h:120](bgpd/bgp_vty.h#L120)` 起）。
   这是**源码注释主动指出的疑点**，未做修改（本分支只加注释）。属于“注释与自身代码风格不齐”，
   非功能错误。

5. **任务提示“`bgp_nhg.c` 是下一跳组对象（ZEBRA_NHG_*）管理”并不准确。** 代码里
   `bgp_nhg.c` 只做 **ID 号段的申请/分配/释放**（`bgp_nhg_id_alloc` `[122](bgpd/bgp_nhg.c#L122)`、
   起点来自 `zclient_get_nhg_start(ZEBRA_ROUTE_BGP)` `[76](bgpd/bgp_nhg.c#L76)`）；
   **没有**在本文件里找到 `ZEBRA_NHG_ADD/DEL` 之类的消息构造（那类逻辑在别处）。

6. **`bgp_script.c` 不定义“事件→脚本”映射。** 若期待本文件里有一张“事件表”会发现没有——
   它只提供 peer/attr 编解码器（`[bgp_script.c:234](bgpd/bgp_script.c#L234)`）。触发点在框架侧。

---

## §11 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 以为 `bgp_zebra_route_install` 会同步发 ZAPI | 在临界区做 socket I/O，或误解时序 | 它只**入队**；实际发送在 `bgp_handle_route_announcements_to_zebra` → `..._actual` |
| 2 | 忽略 `ZEBRA_ANNOUNCEMENTS_LIMIT` | 以为一次事件能处理完整表 | 单次最多 1000 条，剩余靠再次排事件 |
| 3 | 忘记处理 `ZCLIENT_SEND_BUFFERED` | 缓冲满时通告丢失 | 靠 `bgp_zebra_buffer_write_ready` 在可写时重驱 |
| 4 | 把 `bgp_nhc.c` 当下一跳缓存 | 找错文件、误判数据结构 | NHC 是 Next-Hop Capability TLV；NHT 在 `bgp_nexthop.c` |
| 5 | 把 RPKI 状态当 `bgp_path_info` 的持久字段 | 读不到该字段、误解何时算 | 它是**按需**经 hook 重算（`bgp_rpki_prefix_status`） |
| 6 | RPKI 未同步就采信校验结果 | 误杀路由 | 只有 `rtr_is_synced` 为真才可信 |
| 7 | SNMP 把 `BGP_OPT_TRAPS_*` 当“立即发 trap” | 误解命令语义 | 它只是**开关位**；发送侧另判位 |
| 8 | 以为 bgp4v2（RFC 4382）能替代 bgp4（4273） | MIB 对不上采集器 | 两套并存，由不同开关位分别控制 |
| 9 | 用不可信 MRT 文件喂 `bgp_btoa` | 越界读取/崩溃 | 它**不做校验**，仅用于可信 dump |
| 10 | 在 `bgp_vty.c` 里改 AF 子节点的 `config_write` | 白改 | 只有 `bgp_node` 挂 `bgp_config_write`，改它 |
| 11 | 改了 VTY 命令字段却没调 apply 函数 | 配置“生效了”但会话/路由没变 | 补 `bgp_peer_activate` / `peer_group2peer_config_copy` 等 |
| 12 | 以为 `bgp_vty.c` 用 northbound | 找错文件 | 它是 VTY-only（`no_xpath=true`）；NB 在 `bgp_nb*.c` |
| 13 | 开 `debug bgp xxx` 却没开 `terminal monitor` | 终端看不到日志 | 两者是“开关”与“管道”，需同时满足 |
| 14 | 以为 `terminal monitor` 在 bgp_debug.c | 找错文件 | 它在 `lib/vty.c` |
| 15 | 混淆 `bgp_zclient` 与 `bgp_zclient_sync` | 标签管理走错句柄 | 标签管理器用 **sync** 句柄 |

---

## §12 自测题（复习用）

<details><summary>1. bgpd 与 zebra 之间的消息分派表叫什么？定义在哪一行？</summary>
`bgp_handlers[]`，定义在 `[bgp_zebra.c:4695](bgpd/bgp_zebra.c#L4695)`，由 `zclient_new()` 接收。
</details>

<details><summary>2. 出向发路由的两个 ZAPI 消息是什么？分别在哪个函数、哪一行发送？</summary>
`ZEBRA_ROUTE_ADD`（`bgp_zebra_announce_actual` `[1911](bgpd/bgp_zebra.c#L1911)`，发送 `[2049](bgpd/bgp_zebra.c#L2049)`）
与 `ZEBRA_ROUTE_DELETE`（`bgp_zebra_withdraw_actual` `[2124](bgpd/bgp_zebra.c#L2124)`，发送 `[2155](bgpd/bgp_zebra.c#L2155)`）。
</details>

<details><summary>3. 有序通告队列叫什么？为什么要它？</summary>
`bm->zebra_announce_head`。作用：抖动合并/去重、限流（单次 ≤ 1000 条）、让选路与内核下发解耦、
套接字可写与有新通告走同一消费路径。
</details>

<details><summary>4. 内核安装完成怎么回填到 bgp_dest？</summary>
zebra 回 `ZEBRA_ROUTE_NOTIFY_OWNER` → `bgp_zebra_route_notify_owner` `[3443](bgpd/bgp_zebra.c#L3443)`，
`ZAPI_ROUTE_INSTALLED` 时清 PENDING、置 `BGP_NODE_FIB_INSTALLED`（`[3487](bgpd/bgp_zebra.c#L3487)`），
并立即 `group_announce_route`。
</details>

<details><summary>5. `bgp_nhc.c` 解决什么？和下一个问题里的 `bgp_nhg.c` 有何不同？</summary>
`bgp_nhc.c` 是 Next-Hop Capability TLV 链表工具（`bgp_nhc_tlv_add` `[30](bgpd/bgp_nhc.c#L30)`）；
`bgp_nhg.c` 是 L3 下一跳组 ID 分配器（`bgp_nhg_id_alloc` `[122](bgpd/bgp_nhg.c#L122)`）。前者与 zebra 无关，后者向 zebra 取号段。
</details>

<details><summary>6. NHG 的 ID 从哪里来？</summary>
`zclient_get_nhg_start(ZEBRA_ROUTE_BGP)`（`bgp_nhg_zebra_init` `[76](bgpd/bgp_nhg.c#L76)`），步长 `ZEBRA_NHG_PROTO_SPACING`。
</details>

<details><summary>7. BMP 六类报文的类型值分别是？</summary>
Route Monitoring=0、Statistics Report=1、Peer Down=2、Peer Up=3、Initiation=4、Termination=5
（另有 Route Mirroring=6），见 `[bgp_bmp.h:73](bgpd/bgp_bmp.h#L73)` 起。
</details>

<details><summary>8. BMP 的 Peer Up 报文由哪个函数产出？</summary>
`bmp_send_peerup` `[731](bgpd/bgp_bmp.c#L731)`，内部用 `bmp_peerstate(peer,false)` `[571](bgpd/bgp_bmp.c#L571)`。
</details>

<details><summary>9. BMP 挂了哪些 BGP 钩子？举三个。</summary>
`bgp_packet_dump`→`bmp_mirror_packet`、`peer_status_changed`→`bmp_peer_status_changed`、
`bgp_process`→`bmp_process`（均在 `[bgp_bmp.c:4023](bgpd/bgp_bmp.c#L4023)` 起注册）。
</details>

<details><summary>10. BMP 的三种监控视图是什么？</summary>
`BMP_MON_PREPOLICY` `[316](bgpd/bgp_bmp.h#L316)`、`BMP_MON_POSTPOLICY` `[317](bgpd/bgp_bmp.h#L317)`、`BMP_MON_LOC_RIB` `[318](bgpd/bgp_bmp.h#L318)`。
</details>

<details><summary>11. RPKI 用的是哪个第三方库？bgpd 与它怎么分工？</summary>
rtrlib。`tr_socket` 负责传输、`rtr_socket` 负责 RTR 协议层；bgpd 管 cache 配置与 start/stop、
接收同步数据（`bgpd_sync_callback` `[849](bgpd/bgp_rpki.c#L849)`）。
</details>

<details><summary>12. RPKI 校验三态是什么？在哪个函数算？</summary>
VALID/INVALID/NOTFOUND（列在 `[bgp_rpki.h:44](bgpd/bgp_rpki.h#L44)`），由 `rpki_validate_prefix` `[1412](bgpd/bgp_rpki.c#L1412)` 计算。
</details>

<details><summary>13. RPKI 校验结果怎样影响选路？</summary>
经 hook `bgp_rpki_prefix_status` **按需**暴露；调用点在 `bgp_route.c`（`[11731](bgpd/bgp_route.c#L11731)` 等）；
route-map 用 `match rpki`（`route_match` `[371](bgpd/bgp_rpki.c#L371)`）据此过滤。**不写入 path_info**。
</details>

<details><summary>14. 为什么只有 `rtr_is_synced` 为真时校验结果才可信？</summary>
初次同步前 cache 数据不完整，会把合法前缀误判为 NOTFOUND/未知，可能误杀。
</details>

<details><summary>15. bgp4 MIB 与 bgp4v2 MIB 的 OID 前缀和 RFC 各是什么？</summary>
bgp4：`1.3.6.1.2.1.15`，RFC 4273；bgp4v2：`1.3.6.1.3.5.1`，RFC 4382。
</details>

<details><summary>16. `BGP_OPT_TRAPS_*` 三个位分别由哪条命令控制？</summary>
`bgp snmp traps rfc4273`（`[78](bgpd/bgp_snmp.c#L78)`）、`... rfc4382`（`[95](bgpd/bgp_snmp.c#L95)`）、
`... bgp4-mibv2`（`[112](bgpd/bgp_snmp.c#L112)`）。
</details>

<details><summary>17. SNMP 子代理与 snmpd 之间用什么协议/接口？</summary>
AgentX，经 lib 的 smux 层（`smux_init(tm)` `[197](bgpd/bgp_snmp.c#L197)`）。
</details>

<details><summary>18. 脚本桥里 peer 和 attr 的可读写性有何不同？</summary>
`frrscript_codecs_bgpd[]` `[234](bgpd/bgp_script.c#L234)`：peer **只有 encoder**（只读）、
attr **有 encoder+decoder**（可读可写）。
</details>

<details><summary>19. MRT 文件的记录头多长？MRT BGP4MP 的 type 值？</summary>
头 12 字节（`BGP_DUMP_HEADER_SIZE` `[65](bgpd/bgp_dump.h#L65)`）；`MSG_PROTOCOL_BGP4MP=16` `[46](bgpd/bgp_dump.h#L46)`。
</details>

<details><summary>20. `bgp_btoa` 的主循环先读多少字节？</summary>
先读 12 字节记录头（`[bgp_btoa.c:189](bgpd/bgp_btoa.c#L189)`），再按 len 读记录体（`[253](bgpd/bgp_btoa.c#L253)`）。
</details>

<details><summary>21. `bgp_vty.c` 里 DEFUN/DEFPY/install_element 各多少处？</summary>
DEFUN 288、DEFUN_NOSH 8、DEFUN_HIDDEN 4、DEFPY 108、install_element 1173（实测）。
</details>

<details><summary>22. `bgp_vty_init()` 在哪个文件哪一行？它做什么？</summary>
`[bgp_vty.c:21714](bgpd/bgp_vty.c#L21714)`，是所有 `install_element` 的注册总入口（约 2600 行）。
</details>

<details><summary>23. BGP 的 CLI 视图节点里，哪个挂了 config_write？</summary>
只有顶层 `bgp_node`（`[21266](bgpd/bgp_vty.c#L21266)`，`config_write=bgp_config_write` `[21271](bgpd/bgp_vty.c#L21271)`）；
各 AF 子节点由 `bgp_config_write` 递归输出。
</details>

<details><summary>24. `bgp_vty.c` 和 northbound 是什么关系？</summary>
各走各的入口、写同一批字段。`bgp_vty.c` 是 VTY-only（`no_xpath=true`）；NB 在 `bgp_nb*.c`。
</details>

<details><summary>25. `terminal monitor` 在哪实现？和 bgp_debug.c 什么关系？</summary>
在 `lib/vty.c`（`DEFUN_NOSH(terminal_monitor, ...)` `lib/vty.c:3541`；注册 `lib/vty.c:3896`）。
bgp_debug.c 决定“哪些事件产生日志”，terminal monitor 决定“日志往终端去”；两者需同时满足。
</details>

---

## §13 代码索引（按建议阅读顺序）

**第 0 步：先建立“外部集成 = zclient + hook”两条主线**
- `bgp_zebra.c` 头部导读（约 6–53）：确认唯一内核接口。
  → 认知：bgpd 不碰内核，全交 zebra。

**第 1 步：zebra 出向与入向**
- `[bgp_zebra.c:4695](bgpd/bgp_zebra.c#L4695)` 的 `bgp_handlers[]`：入向总表。
- `[bgp_zebra.c:2351](bgpd/bgp_zebra.c#L2351)` → `[2187](bgpd/bgp_zebra.c#L2187)` → `[1911](bgpd/bgp_zebra.c#L1911)`：
  跟一遍“install → 队列 → 消费 → 编码发送”。
  → 认知：两段式异步 + 队列限流。

**第 2 步：闭环**
- `[bgp_zebra.c:3443](bgpd/bgp_zebra.c#L3443)`：`ZEBRA_ROUTE_NOTIFY_OWNER` 回填 FIB_INSTALLED。
  → 认知：suppress-fib-pending 的闭环。

**第 3 步：两个“N H”文件**
- `bgp_nhc.c`（全读 158 行）与 `bgp_nhg.c`（全读 147 行）。
  → 认知：NHC=TLV 工具、NHG=ID 分配器；都不是 NHT。

**第 4 步：BMP**
- `[bgp_bmp.h:73](bgpd/bgp_bmp.h#L73)` 消息类型宏 → `[bgp_bmp.c:4023](bgpd/bgp_bmp.c#L4023)` 钩子清单 →
  `[395](bgpd/bgp_bmp.c#L395)`/`[407](bgpd/bgp_bmp.c#L407)` 头封装 → `[571](bgpd/bgp_bmp.c#L571)` Peer Up/Down。
  → 认知：生产者/队列/发送线程三段式 + 六类报文。

**第 5 步：RPKI**
- `[bgp_rpki.c:188](bgpd/bgp_rpki.c#L188)` `struct rpki_vrf` → `[1114](bgpd/bgp_rpki.c#L1114)` 模块 init →
  `[849](bgpd/bgp_rpki.c#L849)` 同步回调 → `[1412](bgpd/bgp_rpki.c#L1412)` 校验 →
  `bgp_route.c:11731` hook 调用点。
  → 认知：按需校验、经 hook 影响选路。

**第 6 步：SNMP**
- `[bgp_snmp.c:78](bgpd/bgp_snmp.c#L78)` 三个 trap 开关 → `[207](bgpd/bgp_snmp.c#L207)` 模块 init →
  `bgp_snmp_bgp4.h:45` / `bgp_snmp_bgp4v2.h:35` 两份 OID → `[bgp_snmp_bgp4.c:906](bgpd/bgp_snmp_bgp4.c#L906)` /
  `[bgp_snmp_bgp4v2.c:1729](bgpd/bgp_snmp_bgp4v2.c#L1729)` 注册。
  → 认知：两套 MIB + AgentX。

**第 7 步：脚本 + MRT**
- `[bgp_script.c:43](bgpd/bgp_script.c#L43)` → `[234](bgpd/bgp_script.c#L234)`：编解码器。
- `[bgp_dump.c:1049](bgpd/bgp_dump.c#L1049)` 注册钩子 → `[748](bgpd/bgp_dump.c#L748)` 报文记录 →
  `bgp_btoa.c:153` 读回。
  → 认知：Lua 桥只做类型转换；MRT 生产/消费成对。

**第 8 步：配置面**
- `[bgp_vty.c:21714](bgpd/bgp_vty.c#L21714)` 的 `bgp_vty_init`，按 `【段】` 注释浏览 →
  `[21266](bgpd/bgp_vty.c#L21266)` 的 cmd_node → `[20659](bgpd/bgp_vty.c#L20659)` 的 config_write。
  → 认知：命令定义 vs 注册两段式；config_write 只在顶层节点。

**第 9 步：调试面**
- `[bgp_debug.h:236](bgpd/bgp_debug.h#L236)`/`[242](bgpd/bgp_debug.h#L242)`/`[265](bgpd/bgp_debug.h#L265)`：
  conf/term 双态 → `[bgp_debug.c:2950](bgpd/bgp_debug.c#L2950)` init →
  `lib/vty.c:3541` terminal monitor。
  → 认知：开关（bgp_debug）与管道（lib live-log）分离。

---

## §14 与官方 `doc/developer/*.rst` 的对应

| 本卷主题 | 官方文档 |
|---|---|
| zebra/zclient 协议 | `doc/developer/zebra.rst`（ZAPI、zclient 客户端类型、路由下发） |
| 钩子机制（BMP/RPKI/SNMP 接入方式） | `doc/developer/hooks.rst`（`DECLARE_HOOK`/`DEFINE_HOOK`/`hook_register`） |
| CLI 命令宏与注册 | `doc/developer/cli.rst`（DEFUN/DEFPY/install_element/node） |
| northbound / YANG | `doc/developer/northbound*.rst`（本卷不展开，属其它卷） |
| 可加载模块 `.so` | `doc/developer/modules.rst`（`FRR_MODULE_SETUP`，本卷 BMP/RPKI/SNMP 三模块） |
| VTY 与日志（terminal monitor） | `doc/user/basic.rst`（terminal monitor、日志） |
| 各功能的用户手册 | `doc/user/bgp.rst`（BMP/RPKI/SNMP/脚本的用户视角配置） |

> 说明：以上 `.rst` 文件名以本仓库 `doc/` 目录为准；本文引用的**行号**均来自本仓库源码，
> 与官方手册的章节号不是一一对应关系（手册讲“怎么用”，本文讲“怎么实现”）。

---

> **本卷完。** 全文行号均已在本仓库 `study/bgpd-annotated` 分支实测；共覆盖 25 个文件、47640 行。

# FRR bgpd 深入剖析 · 总览篇 —— 架构、三线程模型与 131 文件全景地图

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> bgpd 目录共 **131 个 `.c`/`.h` 文件、201,610 行**（其中约 24,816 行是同分支上补写的中文注释，
> 只加注释、不改代码），本套文档全部覆盖。另有 `bgpd/rfapi/` 34 个文件、33,213 行，
> 属 `--enable-bgp-vnc` 条件编译、默认不构建，见文末「附录 A」。
>
> 写作目的：回答三个问题 ——
> 1. **bgpd 到底长什么样？** 报文从网卡进来、到内核 FIB 出去，中间经过了哪些对象、哪些线程、哪些文件？
> 2. **131 个文件各自负责什么？** 改代码时怎么一步定位到「该动哪个文件的哪个函数」？
> 3. **这些功能之间怎么联动？** 谁调用谁、谁通过什么机制通知谁？
>
> 本文是**总览篇**，给出架构骨架、核心数据结构、主链路时序与完整文件地图；
> 每个子系统的机制细节在 6 篇分册中展开（见 §10 分册导航）。
> 本文不修改任何源码。若发现注释与代码不符，§11「陷阱清单」与各分册的「勘误」节会列出。

---

## §0 全局总览

### 0.1 先给结论

> **bgpd 不是「一个 BGP 协议实现」，而是「一个单进程内的多实例路由引擎 + 一个三线程的报文流水线 + 一套面向 6 个外部系统的集成适配层」。**
>
> 读 bgpd 最容易迷路的地方，是把这三件事当成一件事。把它们分开，131 个文件立刻各归其位。

三件事分别对应三层：

| 层 | 你要建立的心智模型 | 主要文件 | 分册 |
|---|---|---|---|
| **① 协议引擎** | 一棵 RIB 树 + 一个 FSM + 一套属性编解码 | `bgpd.h` `bgp_route.c` `bgp_table.c` `bgp_attr.c` `bgp_fsm.c` `bgp_packet.c` | 卷1、卷2、卷3 |
| **② 报文流水线** | 三个线程、两条队列、一次跨线程交接 | `bgp_io.c` `bgp_keepalives.c` `bgp_packet.c` `bgp_updgrp*.c` | 卷1 |
| **③ 集成适配层** | 6 个对端：zebra / BMP / RPKI / SNMP / 脚本 / vtysh | `bgp_zebra.c` `bgp_bmp.c` `bgp_rpki.c` `bgp_snmp*.c` `bgp_vty.c` | 卷6 |

另外，还有两条**横切**的命名线，第一次读 bgpd 时最容易误判：

| 容易误判的现象 | 实际 |
|---|---|
| `bgp_evpn.c`、`bgp_evpn_mh.c`、`bgp_mplsvpn.c` 看起来像「另一个协议」 | 它们是**同一个 bgpd 进程内的地址族扩展**，共用 RIB、FSM、属性引擎，只是 NLRI 编解码不同。EVPN 甚至共用 `struct bgp` 实例（MAC-VRF / IP-VRF）。 |
| `bgp_vty.c` 有 24,654 行，比 `bgp_route.c` 还大 | `bgpd.c`/`bgp_route.c` 是**算法**，`bgp_vty.c` 是**命令与 show 输出**。文件大小反映的是 CLI 表面积，不是复杂度。全 bgpd 共 815 个 `DEFUN`/`DEFPY`，其中 `bgp_vty.c` 占 408 个。 |

### 0.2 心智模型：从「一个进程」到「一棵 RIB 树」

bgpd 最反直觉的一点：**一个 bgpd 进程里可以有多个 BGP 实例**。这来自 FRR 的对象模型分层 ——
「进程」不是「实例」，「实例」也不是「邻居」。

```
┌─ bgpd 进程（1 个）────────────────────────────────────────────┐
│                                                               │
│  bm  = struct bgp_master        ← 进程级单例，全局唯一          │
│   ├─ master     : event_loop    ← 主线程事件循环（libfrr）      │
│   ├─ bgp        : list          ← 【实例链表】default/VRF/view  │
│   ├─ connection_fifo            ← 【跨线程队列】见 §0.3         │
│   ├─ peer_connection_mtx        ← 保护上面这条队列的互斥锁       │
│   ├─ listen_sockets / addresses ← 监听套接字                    │
│   └─ labelpool / mh_info / ...                                  │
│                                                               │
│   └─ bgp (实例 1)   ← struct bgp，可以是 default / VRF / view   │
│       ├─ rib[AFI_MAX][SAFI_MAX]  ← 【RIB 树】每 AFI/SAFI 一棵   │
│       │    └─ bgp_table                                         │
│       │        └─ bgp_dest (前缀节点)                           │
│       │            └─ bgp_path_info (一条来自某邻居的路径)      │
│       ├─ peer / peer_group  (list / hash)                       │
│       ├─ update_groups                                          │
│       └─ vpn_policy / evpn 信息 / ...                           │
│                                                               │
│   └─ bgp (实例 2)   ← 例如一个 VRF 实例                         │
│       └─ rib[...] ...                                           │
└───────────────────────────────────────────────────────────────┘
```

三条贯穿全模块的主线，建议边读边记（`bgpd.h` 的「学习导读」块也是这么分的）：

| 线 | 路径 | 起点 |
|---|---|---|
| **实例线** | `bm` → `bgp` → `rib[afi][safi]` → `bgp_dest` → `bgp_path_info` | [bgpd.h:168](bgpd/bgpd.h#L168) |
| **邻居线** | `bgp` → `peer_group` → `peer` → `peer_af` → `update_subgroup` | [bgpd.h:657](bgpd/bgpd.h#L657) |
| **连接线** | `peer` → `peer_connection` → `ibuf`/`obuf` → FSM 状态 | [bgpd.h:1580](bgpd/bgpd.h#L1580) |

### 0.3 三线程模型全景图

**这是 bgpd 与大多数 FRR 守护进程最不一样的地方。** 大多数守护进程是单线程事件循环；
bgpd 用 `frr_pthread` 额外起了**两个**专用线程（见 [bgpd.c:10424](bgpd/bgpd.c#L10424) `bgp_pthreads_init`）：

```c
bgp_pth_io = frr_pthread_new(&io, "BGP I/O thread", "bgpd_io");   // bgpd.c:10437
bgp_pth_ka = frr_pthread_new(&ka, "BGP Keepalives thread", "bgpd_ka"); // bgpd.c:10438
```

| 线程 | 全局句柄 | 职责 | 入口 |
|---|---|---|---|
| **主线程** | `bm->master` | 事件循环、FSM 驱动、RIB 增删改、选路、策略、CLI、zebra 通信 | `frr_run(bm->master)` |
| **I/O 线程** | `bgp_pth_io` [bgpd.c:10416](bgpd/bgpd.c#L10416) | 套接字读/写、切成整包、压入 `ibuf` | 用 `frr_pthread_attr_default.start` |
| **Keepalive 线程** | `bgp_pth_ka` [bgpd.c:10418](bgpd/bgpd.c#L10418) | 独立生产 KEEPALIVE，避免主线程忙时漏发 | `bgp_keepalives_start` [bgp_keepalives.c:209](bgpd/bgp_keepalives.c#L209) |

启动与收尾在 [bgpd.c:10442](bgpd/bgpd.c#L10442) `bgp_pthreads_run()` / [bgpd.c:10453](bgpd/bgpd.c#L10453) `bgp_pthreads_finish()`。

**跨线程交接是本模型的核心，务必吃透**（这是全文档最值得记的一张图）：

```
   I/O 线程                                   主线程
   ─────────                                  ────────

  socket 可读
      │
      ▼
  bgp_process_reads()                    （bgp_io.c:386）
      │  ① 读字节、按 header 长度切整包
      ▼
  read_ibuf_work()                       （bgp_io.c:292）
      │  ② 整包 append 到 connection->ibuf
      ▼
  把 connection 挂到 bm->connection_fifo      ← 加锁 bm->peer_connection_mtx
      │      （bgp_io.c:479-481）
      ▼
  event_add_event(bm->master,                  ┐
      bgp_process_packet, ...)                 │ 跨线程唤醒
      │      （bgp_io.c:484）                   ┘
      │                                            │
      └───────────── 线程边界 ─────────────────────┘
                                                   ▼
                                        bgp_process_packet()   （bgp_packet.c:4316）
                                                   │  加锁 pop 出 connection
                                                   │      （bgp_packet.c:4329-4330）
                                                   ▼
                                        按 type 分派（bgp_packet.c:4391 起）
                                                   │
      ┌────────────────┬───────────────┬───────────┴────┬──────────────────┐
      ▼                ▼               ▼                ▼                  ▼
 bgp_open_receive  bgp_update_receive bgp_notify_receive bgp_keepalive_receive bgp_capability_receive
  (4395)             (4407)            (4418)             (4430)               (4453)
```

反向（发送）方向是**对称但不同步**的：主线程把要发的报文编码后 push 到 `connection->obuf`，
I/O 线程的 `bgp_process_writes` [bgp_io.c:226](bgpd/bgp_io.c#L226) 负责实际 `writev`。
主线程**不直接调** `write()`，而是通过 `bgp_writes_on/off`（[bgp_io.c:104](bgpd/bgp_io.c#L104) / [:133](bgpd/bgp_io.c#L133)）
去登记/注销写事件。

> **为什么要两个线程？** 一个邻居发来 100 万条路由时，读 socket + 解析报文是纯 CPU 活。
> 放在主线程会让所有邻居的 keepalive 定时器、路由收敛全部卡住。把 I/O 切开，
> 主线程只做「把已经切好的整包喂给 RIB」这一步。keepalive 单独一个线程则是为了
> 让主线程即使在做全表选路，保活报文也不会迟到而被对端断会话。
>
> 详见卷1 §1、§4。

### 0.4 六个子系统 —— 本套文档的分卷依据

131 个文件按「改代码时会一起动」聚成 6 组，每组一篇分册：

| 卷 | 分册文件 | 主题 | 文件数 | 源码行数 |
|---|---|---|---|---|
| **卷1** | `FRR_BGPD_1_SESSION_STUDY.md` | 会话与协议机：进程、实例、邻居、连接、FSM、报文编解码、I/O、更新组 | 19 | 36,671 |
| **卷2** | `FRR_BGPD_2_RIB_ATTR_STUDY.md` | 路由信息库与路径属性：RIB 树、前缀表、AS_PATH、团体/扩展团体/大团体、RD、过滤器 | 22 | ~34,000 |
| **卷3** | `FRR_BGPD_3_BESTPATH_STUDY.md` | 属性仲裁、选路、通告与收敛：属性编解码、bestpath、多路径、addpath、下一跳跟踪、抖动抑制 | 23 | ~19,400 |
| **卷4** | `FRR_BGPD_4_EVPN_MPLS_STUDY.md` | EVPN 与 MPLS L3VPN：EVPN 各路由类型、MAC-VRF、多归属、VPNv4/v6、标签池、SRv6、封装 TLV | 28 | ~41,600 |
| **卷5** | `FRR_BGPD_5_POLICY_STUDY.md` | 路由策略与流规范：route-map、北向配置、PBR、flowspec、BFD 联动 | 14 | ~22,300 |
| **卷6** | `FRR_BGPD_6_INTEGRATION_STUDY.md` | 外部集成、配置面与可观测性：zebra、BMP、RPKI、SNMP、脚本、MRT、CLI、debug | 25 | ~66,900 |

> 卷6 文件数不是最多，但源码量接近全模块 1/3 —— 因为 `bgp_vty.c`（24,654 行）与
> `bgp_zebra.c`（5,520 行）都在这里。分卷按「主题内聚」而非「行数均分」。

### 0.5 完整文件地图（131 个 `.c`/`.h`）

下表是全模块的「功能 → 文件」反查表。**改代码时先在这里定位文件，再去对应分册看机制。**

- 行数列为**当前工作树行数**（含中文注释），不是上游原始行数。
- 「卷」列指该文件在哪篇分册里有独立小节。

| 文件 | 行数 | 一句话职责 | 卷 |
|---|---|---|---|
| `bgp_addpath.c` | 615 | BGP Addpath 发送侧（TX）ID 选择与相关工具 | V3 |
| `bgp_addpath.h` | 147 | Add-Path(发送侧) 的对外接口 | V3 |
| `bgp_addpath_types.h` | 106 | Add-Path(RFC7911) **发送侧**的数据结构 | V3 |
| `bgp_advertise.c` | 369 | 出/入向通告记录(adj_out / adj_in)的增删与属性聚合 | V3 |
| `bgp_advertise.h` | 294 | 出向通告(adj_out)与入向记录(adj_in)的数据模型 | V3 |
| `bgp_aspath.c` | 3094 | AS_PATH / AS4_PATH 属性的内存表示、解析、编码、比较（选路）与聚合 | V2 |
| `bgp_aspath.h` | 400 | AS_PATH 属性（BGP 的“路径签名”） | V2 |
| `bgp_attr.c` | 6525 | BGP 路径属性的“解析(解码) / 编码 / 共享”中枢 | V3 |
| `bgp_attr.h` | 851 | BGP 路径属性（Path Attribute）的数据模型 | V3 |
| `bgp_attr_evpn.c` | 392 | EVPN（以太网 VPN）相关属性的解析与辅助工具 | V4 |
| `bgp_attr_evpn.h` | 119 | EVPN type-5 路由的“overlay index”承载结构 | V4 |
| `bgp_attr_srv6.h` | 75 | SRv6 Prefix-SID 属性(RFC 9252) 的内部表示 | V4 |
| `bgp_bfd.c` | 982 | BGP 与 BFD（双向转发检测）的集成 | V5 |
| `bgp_bfd.h` | 122 | BGP 与 BFD 联动（把会话/链路故障探测从秒级降到毫秒级） | V5 |
| `bgp_bmp.c` | 4046 | BMP（BGP Monitoring Protocol）实现 | V6 |
| `bgp_bmp.h` | 449 | BGP Monitoring Protocol（RFC 7854 / RFC 9069）头文件 | V6 |
| `bgp_btoa.c` | 323 | BGP 路由转储（MRT dump）到 ASCII 的转换工具 | V6 |
| `bgp_clist.c` | 1742 | community-list（团体过滤器）的实现 | V2 |
| `bgp_clist.h` | 322 | community-list（团体过滤器） | V2 |
| `bgp_community.c` | 1192 | 标准 Community 属性的实现 | V2 |
| `bgp_community.h` | 187 | BGP 标准 Community 属性 | V2 |
| `bgp_community_alias.c` | 291 | 团体(community)别名机制的实现 | V2 |
| `bgp_community_alias.h` | 106 | 团体(community)的“别名”机制 | V2 |
| `bgp_conditional_adv.c` | 596 | 条件通告（conditional advertisement） | V3 |
| `bgp_conditional_adv.h` | 108 | 条件通告（conditional advertisement） | V3 |
| `bgp_damp.c` | 1091 | BGP 路由抖动抑制（flap dampening）的实现 | V3 |
| `bgp_damp.h` | 235 | BGP 路由抖动抑制（flap dampening） | V3 |
| `bgp_debug.c` | 3384 | BGP 调试 / 日志开关的落地实现 | V6 |
| `bgp_debug.h` | 305 | BGP 调试 / 日志开关体系的对外接口 | V6 |
| `bgp_dump.c` | 1079 | 把 BGP 报文事件流与 RIB 快照编码成 MRT 文件 | V6 |
| `bgp_dump.h` | 102 | 把 BGP 报文与路由表导出成 MRT 格式文件 | V6 |
| `bgp_ecommunity.c` | 2345 | Extended Community 属性的实现 | V2 |
| `bgp_ecommunity.h` | 590 | BGP Extended Community 属性 | V2 |
| `bgp_encap_tlv.c` | 1163 | 隧道封装属性的 TLV 编解码实现 | V4 |
| `bgp_encap_tlv.h` | 192 | 隧道封装属性的 TLV 编解码接口 | V4 |
| `bgp_encap_types.h` | 385 | 隧道封装（Tunnel Encapsulation）的数据定义 | V4 |
| `bgp_errors.c` | 624 | bgpd 结构化错误码的“消息注册表” | V3 |
| `bgp_errors.h` | 147 | bgpd 的“结构化错误码” | V3 |
| `bgp_evpn.c` | 10546 | EVPN 子系统的主体实现(路由编解码/导入导出/本地学习/下发内核) | V4 |
| `bgp_evpn.h` | 417 | EVPN 对内核/其它模块暴露的公开接口(含少量 inline 工具) | V4 |
| `bgp_evpn_mh.c` | 5934 | EVPN 多归属(EVPN Multihoming, EVPN-MH)的核心实现 | V4 |
| `bgp_evpn_mh.h` | 755 | EVPN 多归属(EVPN Multihoming, EVPN-MH)的数据结构总表 | V4 |
| `bgp_evpn_private.h` | 1171 | EVPN 子系统的**内部**数据结构、宏与静态工具 | V4 |
| `bgp_evpn_vty.c` | 8584 | EVPN 的 CLI（VTY）层：命令解析、show 输出、配置读写 | V4 |
| `bgp_evpn_vty.h` | 91 | EVPN 的 CLI 层对外接口声明与帮助字符串常量 | V4 |
| `bgp_filter.c` | 950 | as-path access-list（AS 路径过滤器）的实现 | V2 |
| `bgp_filter.h` | 127 | as-path access-list（AS 路径过滤器） | V2 |
| `bgp_flowspec.c` | 256 | FlowSpec NLRI 的接收入口与合法性校验 | V5 |
| `bgp_flowspec.h` | 121 | BGP FlowSpec 的解析与展示接口 | V5 |
| `bgp_flowspec_private.h` | 69 | FlowSpec 的“匹配分量”类型表 | V5 |
| `bgp_flowspec_util.c` | 868 | FlowSpec 分量的通用解码器(本模块核心) | V5 |
| `bgp_flowspec_util.h` | 121 | FlowSpec 分量的“通用解码器” | V5 |
| `bgp_flowspec_vty.c` | 744 | BGP FlowSpec 的 vty / show / JSON 输出 | V5 |
| `bgp_fsm.c` | 4061 | BGP 邻居状态机（FSM）与连接/定时器/GR 的核心实现 | V1 |
| `bgp_fsm.h` | 296 | 连接状态机的对外接口与一批辅助宏 | V1 |
| `bgp_io.c` | 880 | 把 BGP 套接字读写放进独立的 I/O 线程 | V1 |
| `bgp_io.h` | 147 | BGP 套接字读写的对外接口 | V1 |
| `bgp_keepalives.c` | 376 | 统一的 keepalive 生产者线程 | V1 |
| `bgp_keepalives.h` | 101 | BGP keepalive 生产者线程的对外接口 | V1 |
| `bgp_label.c` | 844 | BGP 标签的编解码、共享(intern)、申请/注册与 NLRI 解析 | V4 |
| `bgp_label.h` | 253 | BGP 携带的 MPLS 标签：编码/内部标记/共享/注册 | V4 |
| `bgp_labelpool.c` | 1994 | 标签池：向 zebra 申请 label chunk 并按需分配 | V4 |
| `bgp_labelpool.h` | 186 | BGP 标签池的对外接口与核心数据结构 | V4 |
| `bgp_lcommunity.c` | 761 | Large Community 属性的实现 | V2 |
| `bgp_lcommunity.h` | 165 | BGP Large Community 属性 | V2 |
| `bgp_mac.c` | 542 | BGP 本机 MAC 哈希表（self mac hash） | V4 |
| `bgp_mac.h` | 67 | EVPN 场景下的 MAC 表（本机学到的 MAC 的汇总视图） | V4 |
| `bgp_main.c` | 639 | bgpd 守护进程的入口与生命周期 | V1 |
| `bgp_memory.c` | 195 | bgpd 内存类型(MTYPE)的定义与注册 | V3 |
| `bgp_memory.h` | 216 | bgpd 全体内存类型(MTYPE)的集中声明 | V3 |
| `bgp_mpath.c` | 863 | BGP 多路径（Multipath）选择与聚合属性 | V3 |
| `bgp_mpath.h` | 172 | 多路径(ECMP)与带权多路径(WECMP) | V3 |
| `bgp_mplsvpn.c` | 5103 | BGP 的 MPLS VPN / BGP-LU 数据面：VPN NLRI 解析、VRF<->VPN 双向路由泄漏(leak)、SRv6/MPLS 标签与 SID 处理 | V4 |
| `bgp_mplsvpn.h` | 609 | MPLS-VPN 子系统的对外接口与一组内联“状态判定”辅助 | V4 |
| `bgp_mplsvpn_snmp.c` | 1810 | MPLS / BGP L3VPN MIB（RFC4382）取值实现 | V4 |
| `bgp_mplsvpn_snmp.h` | 44 | L3VPN 的 SNMP MIB 支撑 | V4 |
| `bgp_network.c` | 1360 | BGP 邻居 TCP 建连(主动连接 / 被动监听)与 socket 选项 | V1 |
| `bgp_network.h` | 90 | BGP 邻居 TCP 连接建立的对外接口 | V1 |
| `bgp_nexthop.c` | 1605 | 下一跳缓存(bnc)的数据结构层：增删查 + 地址/直连/自环 | V3 |
| `bgp_nexthop.h` | 261 | 下一跳缓存(bnc)的数据结构 + 地址/直连/自环判断 | V3 |
| `bgp_nhc.c` | 158 | BGP 下一跳能力（Next Hop Capability, NHC）TLV 链表工具 | V6 |
| `bgp_nhc.h` | 131 | NHC(下一跳相关特性)属性的内部表示 | V6 |
| `bgp_nhg.c` | 147 | BGP 下一跳组（Nexthop Group, NHG）ID 分配器 | V6 |
| `bgp_nhg.h` | 45 | BGP 的 L3 下一跳组(nexthop group) ID 分配 | V6 |
| `bgp_nht.c` | 2086 | NHT(下一跳跟踪)逻辑层的实现 | V3 |
| `bgp_nht.h` | 172 | NHT(Next Hop Tracking，下一跳跟踪)的对外接口 | V3 |
| `bgp_open.c` | 2409 | BGP OPEN 报文的构造与解析，以及“能力协商”的核心实现 | V1 |
| `bgp_open.h` | 221 | OPEN 报文与能力协商的常量、结构、接口 | V1 |
| `bgp_packet.c` | 4569 | BGP 报文“字节流收发层”：把消息对象编码成线格式，或把线格式解析回消息对象 | V1 |
| `bgp_packet.h` | 171 | 报文收发层的对外接口（bgp_packet.c 的头文件） | V1 |
| `bgp_pbr.c` | 3331 | BGP PBR / FlowSpec 的落地实现 | V5 |
| `bgp_pbr.h` | 504 | BGP PBR (Policy Based Routing) 的接口与内部抽象定义 | V5 |
| `bgp_rd.c` | 340 | RD(Route Distinguisher) 的编解码与字符串互转 | V2 |
| `bgp_rd.h` | 142 | RD(Route Distinguisher，路由区分符) | V2 |
| `bgp_regex.c` | 123 | AS_PATH 正则匹配的实现（POSIX regex 封装 + '_' 魔法） | V2 |
| `bgp_regex.h` | 43 | AS_PATH 正则匹配的三个接口 | V2 |
| `bgp_route.c` | 19317 | bgpd 的“路由信息库中枢”：路由入向落地、最佳路径选择、向 zebra 下发、聚合与 show 命令 | V2 |
| `bgp_route.h` | 1198 | RIB 的“路径”模型与路由处理队列 | V2 |
| `bgp_routemap.c` | 9682 | BGP 专属的 route-map match / set 规则实现 | V5 |
| `bgp_routemap_nb.c` | 564 | BGP route-map 的 NB(YANG) 节点与回调映射表 | V5 |
| `bgp_routemap_nb.h` | 276 | BGP route-map 的 northbound(NB) 配置回调原型声明 | V5 |
| `bgp_routemap_nb_config.c` | 4154 | route-map 的 northbound(YANG) 配置回调实现 | V5 |
| `bgp_rpki.c` | 3165 | RPKI 源验证与 FRR 缓存服务器的对接 | V6 |
| `bgp_rpki.h` | 51 | RPKI 源验证的状态定义 | V6 |
| `bgp_script.c` | 250 | BGP 对象与 Lua 脚本之间的桥接（编解码器注册） | V6 |
| `bgp_script.h` | 82 | 用 Lua 脚本扩展 BGP 的桥接头 | V6 |
| `bgp_snmp.c` | 222 | bgpd 的 SNMP 模块总入口 | V6 |
| `bgp_snmp.h` | 57 | SNMP 实现的公共头（类型别名 + 两个告警入口） | V6 |
| `bgp_snmp_bgp4.c` | 908 | BGP4-MIB（RFC4273）取值实现 | V6 |
| `bgp_snmp_bgp4.h` | 142 | BGP4-MIB（RFC4273）的 OID 列号契约 | V6 |
| `bgp_snmp_bgp4v2.c` | 1731 | BGP4V2-MIB 取值实现 | V6 |
| `bgp_snmp_bgp4v2.h` | 141 | BGP4V2-MIB 的 OID 列号契约 | V6 |
| `bgp_srv6.c` | 424 | 本端 IPv6 单播前缀的 SRv6 SID 分配与通告 | V4 |
| `bgp_srv6.h` | 84 | SRv6 单播(v6 unicast)场景下的 SID 管理接口 | V4 |
| `bgp_table.c` | 346 | BGP RIB 的建表/落表/查表与节点生命周期 | V2 |
| `bgp_table.h` | 540 | BGP RIB 的数据模型（表 / 节点 / 路径） | V2 |
| `bgp_trace.c` | 28 | bgpd 追踪点(tracepoint)的“唯一定义编译单元” | V3 |
| `bgp_trace.h` | 1753 | bgpd 的 LTTng 追踪点(tracepoint)定义头 | V3 |
| `bgp_updgrp.c` | 2791 | update-group 主控：分组的建立/查找/分裂/合并/迁移 | V1 |
| `bgp_updgrp.h` | 738 | Update Group：向邻居发路由的“报文共享”体系 | V1 |
| `bgp_updgrp_adv.c` | 1436 | 出向通告状态机：把“选路结果”变成“待发内容” | V1 |
| `bgp_updgrp_packet.c` | 1565 | bpacket：报文共享缓冲与“逐 peer 改写”的实现 | V1 |
| `bgp_vnc_types.h` | 40 | VNC(Virtual Network Control) 的 Sub-TLV 类型号 | V4 |
| `bgp_vpn.c` | 263 | `show ... vpn` 类命令的 VPN 路由输出实现 | V4 |
| `bgp_vpn.h` | 38 | VPN 相关 CLI 的对外声明（仅一个函数） | V4 |
| `bgp_vty.c` | 24654 | bgpd 的命令行接口(CLI)实现：bgp_vty.h 声明的落地 | V6 |
| `bgp_vty.h` | 319 | bgpd CLI(VTY)层的对外声明：命令字符串宏 + 辅助函数原型 | V6 |
| `bgp_zebra.c` | 5520 | bgpd 与 zebra 的“南向接口”：把 BGP 决策结果真正 | V6 |
| `bgp_zebra.h` | 229 | bgpd 与 zebra 之间的对外接口声明 | V6 |
| `bgpd.c` | 11268 | bgpd 的“主控与对象生命周期中心”：管理 BGP 实例（default/ | V1 |
| `bgpd.h` | 3553 | 整个 bgpd 模块的“数据模型总纲” | V1 |

### 0.6 核心数据结构总览（全部实测行号）

| 结构 | 定义位置 | 一句话职责 |
|---|---|---|
| `struct bgp_master` | [bgpd.h:168](bgpd/bgpd.h#L168) | 进程级单例 `bm`：实例链表、事件循环、监听套接字、跨线程队列、标签池 |
| `struct bgp` | [bgpd.h:657](bgpd/bgpd.h#L657) | **一个 BGP 实例**（default / VRF / view）：持 `rib[AFI][SAFI]`、peer 集合、update-group |
| `struct peer_group` | [bgpd.h:1247](bgpd/bgpd.h#L1247) | 邻居组：一批 peer 共享的配置模板 |
| `struct peer_af` | [bgpd.h:1389](bgpd/bgpd.h#L1389) | **(peer × AFI/SAFI)** 粒度的运行状态：该邻居该地址族的 update-subgroup、过滤链、同步状态 |
| `struct peer_connection` | [bgpd.h:1580](bgpd/bgpd.h#L1580) | **一次具体的 TCP 连接**：ibuf/obuf、FSM 状态、连接碰撞的 doppelganger |
| `struct peer` | [bgpd.h:1684](bgpd/bgpd.h#L1684) | 一个邻居的逻辑身份（与连接解耦，见 §3.5） |
| `enum bgp_fsm_events` | [bgpd.h:1493](bgpd/bgpd.h#L1493) | FSM 事件码 |
| `enum bgp_fsm_status` | [bgpd.h:1519](bgpd/bgpd.h#L1519) | FSM 状态（RFC 4271 §8.2.2 的 7 态 + `Clearing`/`Deleted`） |
| `struct bgp_table` | [bgp_table.h:55](bgpd/bgp_table.h#L55) | 一个 AFI/SAFI 的 RIB 树（radix） |
| `struct bgp_dest` | [bgp_table.h:136](bgpd/bgp_table.h#L136) | 树上一个**前缀节点**，挂多条路径 + `adj_out`/`adj_in` 账本 |
| `struct bgp_path_info` | [bgp_route.h:343](bgpd/bgp_route.h#L343) | **一条路径**（来自某邻居的一条 NLRI + 其属性） |
| `struct bgp_path_info_extra` | [bgp_route.h:265](bgpd/bgp_route.h#L265) | 路径的「大块附属数据」，按需分配（见 §4.2） |
| `struct update_group` | [bgp_updgrp.h:159](bgpd/bgp_updgrp.h#L159) | 一批**出向属性完全相同**的 peer_af |
| `struct update_subgroup` | [bgp_updgrp.h:224](bgpd/bgp_updgrp.h#L224) | 更新组内**可共享同一份报文**的子集，持有同步点与待发 FIFO |

### 0.7 关键 API 速查

| 想做什么 | 入口函数 | 位置 |
|---|---|---|
| 进程入口 | `main` | [bgp_main.c:471](bgpd/bgp_main.c#L471) |
| 守护进程元信息（vty 端口、帮助串） | `FRR_DAEMON_INFO(bgpd, BGP, ...)` | [bgp_main.c:450](bgpd/bgp_main.c#L450) |
| 信号注册 | `bgp_signals[]` | [bgp_main.c:118](bgpd/bgp_main.c#L118) |
| 模块初始化 | `bgp_init(instance)` | [bgpd.c:10566](bgpd/bgpd.c#L10566) |
| 模块收尾 | `bgp_terminate()` | [bgpd.c:10637](bgpd/bgpd.c#L10637) |
| 建邻居 / 删邻居 | `peer_create` / `peer_delete` | [bgpd.c:2642](bgpd/bgpd.c#L2642) / [:3493](bgpd/bgpd.c#L3493) |
| 路由表初始化 | `bgp_route_init()` | [bgp_route.c:19159](bgpd/bgp_route.c#L19159) |
| zebra 通道初始化 | `bgp_zebra_init(master, instance)` | [bgp_zebra.c:4830](bgpd/bgp_zebra.c#L4830) |
| FSM 事件注入 | `bgp_event(event)` | [bgp_fsm.c:136](bgpd/bgp_fsm.c#L136)（声明） |
| FSM 状态落库 | `bgp_fsm_change_status(conn, status)` | [bgp_fsm.c:2107](bgpd/bgp_fsm.c#L2107) |
| 入向落库 | `bgp_update` / `bgp_withdraw` | [bgp_route.c:6210](bgpd/bgp_route.c#L6210) / [:7131](bgpd/bgp_route.c#L7131) |
| 选路 | `bgp_best_selection` | [bgp_route.c:3592](bgpd/bgp_route.c#L3592) |
| 出向准备 | `subgroup_process_announce_selected` | [bgp_route.c:4074](bgpd/bgp_route.c#L4074) |
| 出向策略与属性加工 | `subgroup_announce_check` | [bgp_route.c:2665](bgpd/bgp_route.c#L2665) |
| 更新组报文编码 | `subgroup_update_packet` | [bgp_updgrp_packet.c:798](bgpd/bgp_updgrp_packet.c#L798) |
| 下发 zebra | `bgp_zebra_announce_table` | [bgp_zebra.c:2058](bgpd/bgp_zebra.c#L2058) |

---

## §1 背景：bgpd 要解决什么问题

### 1.1 协议层要解决的问题

BGP（RFC 4271）是**路径向量**协议。它要解决的核心问题是：在一个由**互不信任的自治系统**构成的
互联网里，如何让每个 AS 知道「到某个前缀走哪条路」，同时保证**无环**、**可按策略择优**、**可扩展**。

与 OSPF/IS-IS 之类的链路状态协议相比，BGP 的三个本质差异直接决定了代码形态：

| 差异 | 后果（在代码里的体现） |
|---|---|
| **路径向量而非链路状态**：不交换拓扑，只交换「到某前缀的完整 AS 路径」 | 每条路径要完整保存 `AS_PATH` 用于防环 —— 这是 `bgp_aspath.c` 3100 行存在的理由 |
| **策略驱动而非代价驱动**：选路结果由运营者配置的 route-map / 团体 / local-pref 决定，不是最短路径 | 选路要跑十几个 tie-break 步骤（`bgp_best_selection`），且**入向、出向都要跑策略** |
| **只做增量通告、且要压缩**：同一策略的邻居应收到字节相同的 UPDATE | 这是 `bgp_updgrp.c` / `bgp_updgrp_packet.c` 存在的原因 —— 编码一次，发 N 次 |

### 1.2 工程层要解决的问题（协议之外的）

这部分才是 bgpd 体积膨胀的真正原因。bgpd 除了「实现 BGP」，还要：

| 问题 | 解决手段 | 代表文件 | 分册 |
|---|---|---|---|
| 内核 FIB 只有一份，但 BGP 实例可以有多个 | 多实例 + 每实例一棵 RIB，选完最优再按优先级仲裁下发 | `bgpd.c` `bgp_zebra.c` | 卷1/卷6 |
| 邻居可能在多 AFI/SAFI 上分别协商、分别过滤 | 把 (peer × AFI/SAFI) 抽成 `peer_af` | `bgpd.h` | 卷1 |
| 连接可能双向碰撞（双方同时发起 TCP） | 把连接抽成 `peer_connection`，允许 doppelganger | `bgpd.h` `bgp_fsm.c` | 卷1 |
| 100 万路由 × 1000 邻居 = 收发都是 CPU 瓶颈 | 三线程 + update-group 报文共享 | `bgp_io.c` `bgp_updgrp*.c` | 卷1 |
| 属性在大量路径间高度重复 | 属性 interning（哈希去重、引用计数） | `bgp_attr.c` | 卷3 |
| 需要为每个 VRF 维护独立路由表并互相泄漏 | `struct vpn_policy`：RD/RT、import/export RT 列表 | `bgpd.h` `bgp_mplsvpn.c` | 卷4 |
| 需要与 6 个外部系统对接 | 每个一个适配文件 | `bgp_zebra/bmp/rpki/snmp*/script*.c` | 卷5/卷6 |
| CLI 表面积巨大（815 条命令） | 全部集中到 `bgp_vty.c` / `*_vty.c` | `bgp_vty.c` | 卷6 |

---

## §2 进程启动与三线程搭建

### 2.1 `main()` 走的是 libfrr 通用骨架

`bgp_main.c` 只有 639 行，因为启动主干被 `libfrr` 接管了（见 `FRR_LIBFRR_STUDY.md`）。
bgpd 侧只需要提供三样东西：

```c
FRR_DAEMON_INFO(bgpd, BGP, ...)      // bgp_main.c:450  —— vty 端口、帮助串、yang 模块表
static struct frr_signal_t bgp_signals[]  // bgp_main.c:118 —— 信号处理表
int main(int argc, char **argv)      // bgp_main.c:471  —— 解析 BGP 专有参数后交给框架
```

`main()` 里 `frr_preinit` → `frr_opt_add`（注册 `-p/-n/-Z/-S/-e/-I/-s/-x` 等 BGP 专有选项）
→ 解析循环 → 建 `bm` → `frr_config_fork` → `frr_run`。这部分与其它守护进程同构，
细节见 `FRR_LIBFRR_STUDY.md` §2。

### 2.2 两个专用 pthread 的创建时机

关键点：**线程是「先创建句柄、后启动」两段式的**。

| 阶段 | 函数 | 做什么 |
|---|---|---|
| 创建（不启动） | `bgp_pthreads_init()` [bgpd.c:10424](bgpd/bgpd.c#L10424) | `frr_pthread_new` 建两个句柄，注册 start/stop 钩子 |
| 启动 | `bgp_pthreads_run()` [bgpd.c:10442](bgpd/bgpd.c#L10442) | `frr_pthread_run` + `frr_pthread_wait_running`（**等线程真正跑起来**） |
| 收尾 | `bgp_pthreads_finish()` [bgpd.c:10453](bgpd/bgpd.c#L10453) | `frr_pthread_stop_all()` |

I/O 线程用的是 `frr_pthread_attr_default`（即 libfrr 提供的通用事件循环线程体），
Keepalive 线程用自己的 `bgp_keepalives_start` [bgp_keepalives.c:209](bgpd/bgp_keepalives.c#L209) ——
因为它要维护自己的 peer 哈希表与定时器（`pkat`，[bgp_keepalives.c:77](bgpd/bgp_keepalives.c#L77) `pkat_new`）。

> **陷阱**：`bgp_io.h` 里声明了 `bgp_io_start` [:66](bgpd/bgp_io.h#L66) 与 `bgp_io_stop` [:78](bgpd/bgp_io.h#L78)，
> 但**全树没有任何定义**。I/O 线程根本不用它们（用的是 libfrr 默认线程体）。
> 想找 I/O 线程的入口却顺着这两个符号找，会一无所获。详见 §11 第 8 条。

### 2.3 跨线程队列的初始化

在 `bgp_init` 路径上（[bgpd.c:10290](bgpd/bgpd.c#L10290)）：

```c
peer_connection_fifo_init(&bm->connection_fifo);        // bgpd.c:10290
pthread_mutex_init(&bm->peer_connection_mtx, NULL);     // bgpd.c:10291
```

这两个对象是 §0.3 那张跨线程交接图的全部基础设施 —— 一条队列 + 一把锁。
**理解了「队列 + 锁 + `event_add_event` 唤醒」这三件套，就理解了 bgpd 的并发模型**：
没有共享内存的细粒度锁，只有一条单向队列把「连接」这个所有权单位从 I/O 线程移交给主线程。

---

## §3 对象模型：实例 / 邻居 / 连接 / peer_af

### 3.1 四条线总览

```
bm (bgp_master)                          进程级，全局唯一
 │
 ├── bgp  (实例：default / VRF / view)    ← 一条 RIB 树的拥有者
 │    │
 │    ├── peer_group  (邻居组：配置模板)
 │    │     │
 │    │     └── peer  (邻居的逻辑身份)     ← 配置、统计、定时器都在这里
 │    │           │
 │    │           ├── peer_af[AFI][SAFI]  ← 每地址族的状态/过滤/update-subgroup
 │    │           │
 │    │           └── peer_connection     ← 实际 TCP 连接（可能有多条：碰撞）
 │    │                 ├── ibuf / obuf   ← 跨线程 FIFO
 │    │                 └── FSM 状态
 │    │
 │    └── rib[AFI][SAFI] → bgp_table → bgp_dest → bgp_path_info
 │
 └── update_groups (按出向属性分组)
```

### 3.2 为什么「实例」要独立于「进程」

`struct bgp` [bgpd.h:657](bgpd/bgpd.h#L657) 的三重身份（注释里写得很清楚）：

| 用途 | 是什么 | 例子 |
|---|---|---|
| **default 实例** | 全局 BGP 路由表 | 普通 eBGP/iBGP 部署 |
| **VRF 实例** | 绑定一个 VRF 的 BGP 表，与 default 表隔离 | L3VPN 的 CE-PE 侧、EVPN 的 IP-VRF |
| **view 实例** | 只读视图，用于 `show` 比对 | 排障 |

`bm->bgp` 是一个 `list`，所以「多实例」是设计内的常态而非特例。
`bgp_evpn` 实例（MAC-VRF 的汇聚点）也挂在这个链表里（见 `bm->bgp_evpn`，[bgpd.h:220](bgpd/bgpd.h#L220)）。

### 3.3 为什么「邻居」要独立于「连接」

`struct peer` [bgpd.h:1684](bgpd/bgpd.h#L1684) 存的是**逻辑身份**：邻居地址、AS 号、各种
`Config` 标志、统计计数器、HoldTimer 等。它**在连接建立之前就存在**（配置驱动），
**在连接断开之后仍然存在**（等重连）。

### 3.4 为什么「(peer × AFI/SAFI)」还要再抽一层 `peer_af`

一个邻居可以在 IPv4 unicast 上up、在 IPv6 unicast 上被策略拒绝。
每地址族有自己的一套：出向过滤链、`update_subgroup` 归属、soft-reconfig 状态、ORF 状态。

`struct peer_af` [bgpd.h:1389](bgpd/bgpd.h#L1389) 就是这个粒度。
**读 `bgp_updgrp.c` 时把 `peer_af` 当成 update-group 的成员单位**，而不是 `peer`，这一点搞错会全程读不懂。

### 3.5 为什么「连接」要独立成 `peer_connection`

这是 bgpd 里最容易忽略、但最影响理解 FSM 的一个设计。

BGP 允许双方**同时**发起 TCP 连接（RFC 4271 §6.8 的「连接碰撞」）。两个连接都建起来后，
要靠 BGP ID（router-id）比较来决出保留哪条。因此在「决出胜负」之前，
**一个 `peer` 可能同时挂着两条 `peer_connection`**：一条主动、一条被动。

- 胜者成为正式连接，败者叫 **doppelganger**（替身），要被主动拆掉。
- 交接动作在 `bgp_establish` [bgp_fsm.c:3073](bgpd/bgp_fsm.c#L3073) 里，通过 `peer_xfer_conn`
  把连接「换心」；返回值 `BGP_FSM_SUCCESS_STATE_TRANSFER` 就是告诉上层「发生了连接转移」。
- `bgp_establish` 里紧接着调 `bgp_fsm_change_status(connection, Established)`
  [:3128](bgpd/bgp_fsm.c#L3128) 把状态真正落库。

> 如果按「一个 peer 一个连接」去读 `bgp_fsm.c`，会在 `bgp_stop` / `bgp_establish`
> 的连接转移逻辑处彻底迷失。**先把 `peer_connection` 当成独立生命周期对象，再读 FSM。**

---

## §4 RIB 数据模型

### 4.1 三层：table / dest / path_info

```
struct bgp_table            (bgp_table.h:55)   ← 一个 AFI/SAFI 一棵 radix 树
   │  路由树：前缀 → 节点
   ▼
struct bgp_dest             (bgp_table.h:136)  ← 一个前缀节点
   │  ├─ 前缀、父表指针
   │  ├─ info : bgp_path_info 链表  ← 「这个前缀我有哪些路径」
   │  ├─ adj_out : 出向通告账本     ← 「我把哪些路径通告给了哪些 peer」
   │  └─ adj_in  : 入向原始记录     ← 「我从哪些 peer 收到过，原始属性是什么」
   ▼
struct bgp_path_info        (bgp_route.h:343)  ← 一条路径
      ├─ peer    : 谁告诉我的          ├─ attr : 属性（intern 过的）
      ├─ flags   : BGP_PATH_* 标志      ├─ uptime / 各类计数器
      └─ extra   : bgp_path_info_extra  ← 大块附属数据，惰性分配
```

**`bgp_dest` 上的两个账本（`adj_out` / `adj_in`）是理解「出向通告」的钥匙**：

- `adj_in`：存**原始**收到的属性，用于 soft-reconfig（策略变了要重跑一遍，但不必让邻居重发）。
  入口见 [bgp_route.c:7369](bgpd/bgp_route.c#L7369) 附近的软重配逻辑，它重新调 `bgp_update` [:7391](bgpd/bgp_route.c#L7391)。
- `adj_out`：存**已发出**的记录，避免重复发送。查询入口 `bgp_adj_out_lookup`
  [bgp_advertise.c:231](bgpd/bgp_advertise.c#L231)。

### 4.2 为什么 `bgp_path_info_extra` 要和 `bgp_path_info` 拆开

`struct bgp_path_info_extra` [bgp_route.h:265](bgpd/bgp_route.h#L265) 装的是
**只有少数路径才需要的大块数据**（如聚合信息、EVPN 的 overlay index、标签等）。

拆开的理由是**内存**：一个满载路由器可能有几百万条 `bgp_path_info`，
而需要 extra 的可能不到 10%。把小而通用的字段留在 `bgp_path_info` 里、
把大而罕见的放进惰性分配的 `extra`，可以显著降低常驻内存。

> 这是 bgpd 里反复出现的模式（`update_subgroup` 里的 `bpacket` 池也是同理）。
> 看到 `->extra` 就要意识到「**可能为 NULL**」，所有访问都应有判空。

### 4.3 `bgp_route.c` 的内部地图

`bgp_route.c` 有 19,317 行，是「算法密集」的最大文件。它内部用 `/* ========== N. ... ========== */`
自带了分区标记，可以直接当目录用：

| 节 | 标记行 | 内容 |
|---|---|---|
| 0 | [bgp_route.c:124](bgpd/bgp_route.c#L124) | 文件级辅助设施与基础工具 |
| 1 | [bgp_route.c:307](bgpd/bgp_route.c#L307) | RIB 路径对象与节点：创建、引用计数与标志管理 |
| 2 | [bgp_route.c:982](bgpd/bgp_route.c#L982) | 最佳路径比较（comparator）：`bgp_path_info_cmp` |
| 3 | [bgp_route.c:2113](bgpd/bgp_route.c#L2113) | 入向/出向策略过滤与属性修饰 |
| 4 | [bgp_route.c:2648](bgpd/bgp_route.c#L2648) | 出向通告检查与属性加工：`subgroup_announce_check` |
| 5 | [bgp_route.c:3578](bgpd/bgp_route.c#L3578) | 最佳路径选择与多路径：`bgp_best_selection` |
| 6 | [bgp_route.c:5168](bgpd/bgp_route.c#L5168) | 路由处理队列 `meta_queue` 与调度 |
| 7 | [bgp_route.c:8896](bgpd/bgp_route.c#L8896) | 静态路由（`network` 命令）与聚合 |
| 8 | [bgp_route.c:9839](bgpd/bgp_route.c#L9839) | 路由聚合（`aggregate-address`） |
| 9 | [bgp_route.c:11292](bgpd/bgp_route.c#L11292) | 重分发（redistribute）：从其它协议注入 BGP |
| 10 | [bgp_route.c:11591](bgpd/bgp_route.c#L11591) | `show` 命令输出：文本/JSON 格式化 |
| 11 | [bgp_route.c:18452](bgpd/bgp_route.c#L18452) | 管理距离（distance）与抖动抑制（dampening） |
| 12 | [bgp_route.c:19159](bgpd/bgp_route.c#L19159) | 初始化与收尾：`bgp_route_init` / `bgp_route_finish` |

> 规律：**bgpd 的大文件都用 `/* ========== N. ========== */` 自建分区**。
> 读任何大文件（`bgpd.c` 16 节、`bgp_attr.c` 16 节、`bgp_zebra.c` 12 节）都先 `grep '^/\* =\+ [0-9]'`。

---

## §5 会话生命周期（FSM）

### 5.1 状态与事件

| 枚举 | 位置 | 内容 |
|---|---|---|
| 事件 | [bgpd.h:1493](bgpd/bgpd.h#L1493) `enum bgp_fsm_events` | `BGP_Start` / `BGP_Stop` / `BGP_Open` / `BGP_Keepalive` 等 |
| 状态 | [bgpd.h:1519](bgpd/bgpd.h#L1519) `enum bgp_fsm_status` | `Idle=1` → `Connect` → `Active` → `OpenSent` → `OpenConfirm` → `Established`，外加 `Clearing`、`Deleted` |

**比 RFC 4271 §8.2.2 多两个状态**，这两个是 FRR 的实现细节，不是协议状态：

- `Clearing`：非 RFC 状态，表示**正在清理该 peer 的路由**（`peer_clear` 过程中）。
- `Deleted`：终态，peer 即将释放。

### 5.2 驱动引擎是「状态 × 事件」查表

FSM 不是一大串 `if/else`，而是**表驱动**：`bgp_event()` [bgp_fsm.c:136](bgpd/bgp_fsm.c#L136) 注入事件，
内部按「当前状态 × 事件」查出对应的动作函数，动作函数返回「下一个状态」。

所有状态迁移的**唯一落库点**是 `bgp_fsm_change_status` [bgp_fsm.c:2107](bgpd/bgp_fsm.c#L2107) ——
统计更新、清路由、触发 hook 都在这里。**读 FSM 时只要盯住这一个函数，就不会漏掉副作用。**

### 5.3 建立路径（简图）

```
   Idle
     │ BGP_Start
     ▼
  Connect ──TCP 就绪──► OpenSent ──收到 OPEN──► OpenConfirm ──收到 KEEPALIVE──► Established
     │                     │                        │                              │
     │                     │ bgp_start()            │ bgp_fsm_open()               │ bgp_establish()
     │                     │ bgp_fsm.c:2757         │ bgp_fsm.c:2914               │ bgp_fsm.c:3073
     │                     │                        │                              │  └─ bgp_fsm_change_status()
     └───── 任何一方发 NOTIFY / 超时 / 被清 ────────► bgp_stop()                （bgp_fsm.c:3128）
                            bgp_fsm.c:2247  ──► Idle / Clearing / Deleted
```

`bgp_establish` 是信息量最大的一个函数，步骤见其注释（[bgp_fsm.c:3063](bgpd/bgp_fsm.c#L3063) 起）：
换心（连接碰撞）→ 落状态 → ADJCHANGE 日志 → 分配 update-group/subgroup → 停 BFD hold →
处理 GR → 开 keepalive → 发 ORF route-refresh → 按需延迟首个更新 → 启动 routeadv → 拆替身连接。

---

## §6 入向主干：一条 UPDATE 从网卡到 RIB

```
 ①网卡 ──► socket
             │  I/O 线程
             ▼
 ② bgp_read()                    bgp_io.c:736
             │  校验 header（validate_header bgp_io.c:810）
             ▼
 ③ read_ibuf_work()              bgp_io.c:292       ← 切成整包，追加到 connection->ibuf
             ▼
 ④ 入队 + 唤醒主线程              bgp_io.c:479-484
             │  ═══ 线程边界（队列 bm->connection_fifo + 锁 bm->peer_connection_mtx）═══
             ▼
 ⑤ bgp_process_packet()          bgp_packet.c:4316   ← 主线程，pop 队列并分派
             │  case BGP_MSG_UPDATE: bgp_packet.c:4402
             ▼
 ⑥ bgp_update_receive()          bgp_packet.c:2520
             ├─⑦ bgp_attr_parse()          bgp_packet.c:2652   ← 属性 TLV → struct attr（并 intern）
             └─⑧ bgp_nlri_parse()          bgp_packet.c:395 / 2739  ← 按 SAFI 分派 NLRI 解析器
                     ├─ bgp_nlri_parse_ip()   bgp_route.c:8701   （IPv4/IPv6 unicast）
                     ├─ bgp_nlri_parse_vpn()  （VPNv4/v6，卷4）
                     └─ bgp_nlri_parse_label()（标签，卷4）
                             │  逐条 NLRI
                             ▼
 ⑨ bgp_update() / bgp_withdraw()   bgp_route.c:6210 / :7131
             │   写入 bgp_dest 的 path_info，并触发选路调度
             ▼
 ⑩ bgp_process()                 bgp_route.c:5607   ← 路由处理队列（meta_queue）
             ▼
 ⑪ bgp_best_selection()          bgp_route.c:3592
```

**值得注意的两点**：

1. **属性只解析一次、并全局共享**。`bgp_attr_parse` 出来的 `struct attr` 会走 intern
   （哈希去重 + 引用计数），所以 RIB 里大量路径可以指向同一份属性内存。这是卷3 的重点。
2. **`bgp_nlri_parse` 是「前端分派器」**（[bgp_packet.c:386](bgpd/bgp_packet.c#L386) 的注释），
   按 SAFI 把字节流交给各 AF 专用解析器 —— 这是「协议引擎」为多种地址族复用的关键接缝。

---

## §7 出向主干：从选路结果到网线上的字节

出向链路比入向长得多，**要经过「账本 → 策略 → 编码 → 共享 → 发送」五跳**：

```
 ⑪ bgp_best_selection()              bgp_route.c:3592   ← 选出最优路径，打标志
        │
        ▼
 ⑫ bgp_process()                     bgp_route.c:5607   ← 按 dest 调度
        │
        ▼
 ⑬ subgroup_process_announce_selected()   bgp_route.c:4074
        │
        ├─⑭ subgroup_announce_check()        bgp_route.c:2665
        │     ← 【出向策略唯一落点】出向 route-map / 团体过滤 / SoO 防环 / 下一跳改写
        │       全在这里。返回 false 就不发。
        │
        └─⑮ bgp_adj_out_set_subgroup()        bgp_route.c:4117 调用点
              ← 写入 bgp_dest 的 adj_out 账本（「我打算发给这个 subgroup」）
                    │
                    ▼
 ⑯ subgroup_update_packet()          bgp_updgrp_packet.c:798
        ← 【报文共享】把 subgroup 的待发 FIFO 编码成**一条填满的** UPDATE；
          subgroup 内所有 peer 的报文主体字节相同
                    │
                    ▼
 ⑰ bpacket 逐 peer 改写
        ← 每个 peer 只在少数字段上不同（如 next-hop），做「打补丁」而不是重新编码
                    │
                    ▼
 ⑱ push 到 connection->obuf  ──► I/O 线程 bgp_process_writes()  bgp_io.c:226 ──► writev
```

### update-group 的本质

**问题**：1000 个邻居、出向策略相同、要发同样的 100 万条路由。逐邻居编码 = 10 亿次编码。

**解法**：把「出向属性完全相同的 `peer_af`」聚成一个 `update_group`
[bgp_updgrp.h:159](bgpd/bgp_updgrp.h#L159)，组内再按「能共享同一份待发报文」切成
`update_subgroup` [bgp_updgrp.h:224](bgpd/bgp_updgrp.h#L224)。

| 动作 | 函数 | 位置 |
|---|---|---|
| 找组 | `update_group_find` | [bgp_updgrp.c:1216](bgpd/bgp_updgrp.c#L1216) |
| 建组 | `update_group_create` | [bgp_updgrp.c:1246](bgpd/bgp_updgrp.c#L1246) |
| 删组 | `update_group_delete` | [bgp_updgrp.c:1281](bgpd/bgp_updgrp.c#L1281) |
| 建子组 | `update_subgroup_create` | [bgp_updgrp.c:1344](bgpd/bgp_updgrp.c#L1344) |
| peer 入组 | `update_group_adjust_peer` | [bgp_updgrp.c:2409](bgpd/bgp_updgrp.c#L2409) |

组会随策略变化**分裂**，随策略趋同**合并**（`update_subgroup_check_merge` 等）。
「子组」比「组」小一层的原因：同一组内不同子组可能因为**同步点**（该子组的路由还没发完）
而不能共用同一条报文。

---

## §8 与 zebra 的联动

bgpd **从不直接写内核**。所有 FIB 操作都通过 `zclient` 送给 `zebra` 进程，由 zebra 调 dplane 下发。

```
 bgpd                                        zebra
 ────                                        ─────
  bgp_best_selection 选出最优
        │
        ▼
  按优先级仲裁（多实例/多协议竞争同一条 FIB）
        │
        ▼
  bgp_zebra_announce_table()   bgp_zebra.c:2058
  / all_subtypes()             bgp_zebra.c:2099
        │  排入 bm->zebra_announce_head
        ▼
  bgp_handle_route_announcements_to_zebra()   bgp_zebra.c:2187
        │  ← 【ordered announcement】保证跨 VRF 的下发顺序
        ▼
  zclient_send_message()  ───── ZEBRA_ROUTE_ADD / DELETE ─────►  zebra ──► dplane ──► 内核 FIB
        ▲
        │       ZEBRA_INTERFACE_* / ZEBRA_NEXTHOP_* / ZEBRA_REDISTRIBUTE_*
        └──────────────────────────────────────────────────────────────
                          回灌拓扑、下一跳、接口、重分发路由
```

| 方向 | 关键函数 | 位置 |
|---|---|---|
| 通告整表 | `bgp_zebra_announce_table` | [bgp_zebra.c:2058](bgpd/bgp_zebra.c#L2058) |
| 通告（全子类型） | `bgp_zebra_announce_table_all_subtypes` | [bgp_zebra.c:2099](bgpd/bgp_zebra.c#L2099) |
| 通告默认路由 | `bgp_zebra_announce_default` | [bgp_zebra.c:5073](bgpd/bgp_zebra.c#L5073) |
| 异步排空队列 | `bgp_handle_route_announcements_to_zebra` | [bgp_zebra.c:2187](bgpd/bgp_zebra.c#L2187) |
| 撤销 | `bgp_zebra_withdraw_table_all_subtypes` | [bgp_zebra.c:2438](bgpd/bgp_zebra.c#L2438) |
| 通道初始化 | `bgp_zebra_init` | [bgp_zebra.c:4830](bgpd/bgp_zebra.c#L4830) |
| 下一跳可达性变化 | `bgp_nht_interface_events` | [bgp_nht.c:1155](bgpd/bgp_nht.c#L1155) |
| 注册下一跳跟踪 | `bgp_nht_register_nexthops` | [bgp_nht.c:1964](bgpd/bgp_nht.c#L1964) |

**下一跳跟踪（NHT）是双向联动的典型**：bgpd 把用到的下一跳注册给 zebra 跟踪；
下一跳不可达时 zebra 通知 bgpd，bgpd 让相关路径失效并重跑选路。
接口 up/down 的入口分别是 `bgp_nht_ifp_up` [bgp_nht.c:1107](bgpd/bgp_nht.c#L1107) /
`bgp_nht_ifp_down` [bgp_nht.c:1112](bgpd/bgp_nht.c#L1112)。

---

## §9 配置面与外部集成面

bgpd 对外的 6 个接口，各自一个（或一组）文件：

| 对端 | 通信机制 | 文件 | 分册 |
|---|---|---|---|
| **zebra** | `zclient`（unix socket + 自定义消息） | `bgp_zebra.c` `bgp_nhc.c` `bgp_nhg.c` | 卷6 |
| **vtysh / 用户** | VTY socket + `DEFUN`/`DEFPY` | `bgp_vty.c` `bgp_evpn_vty.c` `bgp_flowspec_vty.c` `bgp_debug.c` | 卷6 |
| **BMP 服务器** | TCP（RFC 7854），单向遥测 | `bgp_bmp.c` | 卷6 |
| **RPKI 缓存** | RPKI-RTR over TCP（`rtrlib`） | `bgp_rpki.c` | 卷6 |
| **SNMP 管理器** | AgentX（`snmpd`） | `bgp_snmp*.c` `bgp_mplsvpn_snmp.c` | 卷6 |
| **外部脚本** | Lua（`--enable-scripting`） | `bgp_script.c` | 卷6 |
| （离线） | MRT 文件 | `bgp_dump.c`（写）/ `bgp_btoa.c`（转 ASCII） | 卷6 |

配置面有一个**容易误判**的结构特征值得先知道：**bgpd 的配置栈是「按子系统分治」的，
不是「同一份配置双轨并存」**。两个方向都实测过：

| 子系统 | 命令宏 | 配置路径 |
|---|---|---|
| **route-map**（`bgp_routemap.c`，124 条命令） | 全是 `DEFUN_YANG`(87) / `DEFPY_YANG`(37) | **已 100% 北向化**：CLI 只是 YANG 北向的前端，语义由 `bgp_routemap_nb*.c` 的回调落地 |
| **其余绝大多数命令**（`bgp_vty.c`，408 条命令：`DEFUN` 288 + `DEFUN_NOSH` 8 + `DEFUN_HIDDEN` 4 + `DEFPY` 108；`bgp_evpn_vty.c` 等） | 全是 `DEFUN` / `DEFPY`（无 `_YANG` 变体） | **老式 VTY**：命令体直接读写 `struct bgp`/`struct peer` 的字段，不经北向 |

```bash
# 实测方法（可复现）
grep -o '^DEFUN_YANG\|^DEFPY_YANG\|^DEFUN(\|^DEFPY(' bgp_routemap.c | sort | uniq -c   # → 全为 _YANG
grep -o '^DEFUN_YANG\|^DEFPY_YANG\|^DEFUN(\|^DEFPY(' bgp_vty.c      | sort | uniq -c   # → 全非 _YANG
```

**为什么这个区别重要**：改 route-map 相关配置必须走北向（改 YANG 模型 + NB 回调），
改其它配置则直接改 `bgp_vty.c` 的命令体。**先确认你要动的那块属于哪一类，否则会改错地方。**
（`lib` 的日志子系统才是真正的「双轨并存」，见 `FRR_ZLOG_STUDY.md` §1.4 —— 两者不要类比。）

---

## §10 分册导航

| 卷 | 文件 | 回答什么问题 | 何时该翻它 |
|---|---|---|---|
| 1 | `FRR_BGPD_1_SESSION_STUDY.md` | 三线程怎么交接？peer/connection 生命周期？FSM 怎么驱动？update-group 怎么压缩报文？ | 改 FSM、报文收发、连接管理、邻居配置、性能调优 |
| 2 | `FRR_BGPD_2_RIB_ATTR_STUDY.md` | RIB 三层结构？一条路径怎么存？各属性怎么表示与去重？各种过滤器在哪？ | 改 RIB 增删查、属性容器、前缀/团体过滤 |
| 3 | `FRR_BGPD_3_BESTPATH_STUDY.md` | 属性编解码与错误处理？选路十几步 tie-break？ECMP / ADD-PATH？下一跳跟踪？ | 改选路策略、属性解析、多路径、抖动抑制、条件通告 |
| 4 | `FRR_BGPD_4_EVPN_MPLS_STUDY.md` | EVPN 五种路由类型？MAC-VRF/IP-VRF？多归属 DF 选举？VPNv4 导入导出？标签池？封装 TLV？ | 改 EVPN / L3VPN / SRv6 / 标签分配 / 隧道封装 |
| 5 | `FRR_BGPD_5_POLICY_STUDY.md` | route-map 与 lib 引擎的关系？flowspec NLRI 怎么编解码？PBR 怎么下发？BFD 怎么联动？ | 改路由策略、流规范、策略路由、BFD |
| 6 | `FRR_BGPD_6_INTEGRATION_STUDY.md` | zclient 消息全表？BMP 挂钩点？RPKI 验证回流？SNMP MIB？815 条 CLI 怎么分区？ | 改 zebra 交互、遥测、CLI、SNMP、脚本 |

---

## §11 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 以为 bgpd 是单线程 | 读 `bgp_io.c` 时看不懂为什么「读完不直接解析」 | 记住：**I/O 线程只切包，主线程才解析**（§0.3） |
| 2 | 以为 `bm->master` 是所有线程的事件循环 | 在 I/O 线程里找 `bm->master` 上的定时器，找不到 | `bm->master` 是**主线程**的；I/O/KA 线程各有自己的 loop |
| 3 | 按「一个 peer 一个连接」读 FSM | `bgp_stop`/`bgp_establish` 的连接转移逻辑完全读不通 | 先接受 `peer_connection` 的独立生命周期与 doppelganger（§3.5） |
| 4 | 把 `peer` 当 update-group 的成员单位 | `bgp_updgrp.c` 全程错位 | 成员单位是 **`peer_af`**（§3.4） |
| 5 | 直接读 `->extra` 不解引用判空 | 段错误（多数路径 extra 为 NULL） | `bgp_path_info_extra` 是**惰性分配**的（§4.2） |
| 6 | 以为 `bgp_vty.c` 是核心 | 花大量时间读 24,654 行 CLI，却没建立架构认知 | 先读卷1/卷2 建骨架，再按需查 CLI 实现 |
| 7 | 只看出向或只看入向 | 改策略时只改了入向，出向漏改 | 入向在 `bgp_update` 路径（§6），出向**唯一落点**是 `subgroup_announce_check` [bgp_route.c:2665](bgpd/bgp_route.c#L2665)（§7） |
| 8 | 顺着 `bgp_io_start` 找 I/O 线程入口 | 全树无定义，找不到 | I/O 线程用 `frr_pthread_attr_default.start`（§2.2）；`bgp_io.h:66/78` 是历史遗留声明 |
| 9 | 直接对内核操作来「快速验证」 | bgpd 不碰内核，改了没效果 | 一律走 zclient → zebra（§8） |
| 10 | 认为属性是每条路径一份 | 低估 interning 的作用，写出 O(N) 内存的代码 | `struct attr` 是**全局共享 + 引用计数**的（§6、卷3） |
| 11 | 在大文件里线性搜索 | 19,000 行的文件找到崩溃 | 先 `grep '^/\* =\+ [0-9]'` 拿分区地图（§4.3） |
| 12 | 用会话摘要/二手材料里的行号 | 行号系统性偏移，链接点错地方 | 行号一律 `grep -n` 实测（本套文档即因此反复核对） |

---

## §12 自测题（复习用）

> 建议先自己答，再展开对照。

<details><summary>1. bgpd 进程里有几个线程？各自职责与入口？</summary>

三个：主线程（`bm->master` 事件循环）、I/O 线程（`bgp_pth_io`，用 `frr_pthread_attr_default.start`）、
Keepalive 线程（`bgp_pth_ka`，入口 `bgp_keepalives_start` [bgp_keepalives.c:209](bgpd/bgp_keepalives.c#L209)）。
创建在 [bgpd.c:10424](bgpd/bgpd.c#L10424)，启动在 [bgpd.c:10442](bgpd/bgpd.c#L10442)。
</details>

<details><summary>2. I/O 线程收到报文后，怎么把连接交给主线程？</summary>

把连接挂到 `bm->connection_fifo`（加锁 `bm->peer_connection_mtx`，[bgp_io.c:479-481](bgpd/bgp_io.c#L479)），
再 `event_add_event(bm->master, bgp_process_packet, ...)`（[bgp_io.c:484](bgpd/bgp_io.c#L484)）唤醒主线程。
主线程在 [bgp_packet.c:4316](bgpd/bgp_packet.c#L4316) 加锁 pop 并分派。
</details>

<details><summary>3. 为什么 `peer_connection` 要从 `peer` 里独立出来？</summary>

为处理 RFC 4271 §6.8 的连接碰撞：决出胜负前一个 peer 可能同时有两条连接，
败者成为 doppelganger，在 `bgp_establish`（[bgp_fsm.c:3073](bgpd/bgp_fsm.c#L3073)）里经 `peer_xfer_conn` 换心后拆除。
</details>

<details><summary>4. RIB 的三层结构？`bgp_dest` 上两个账本各存什么？</summary>

`bgp_table`（AFI/SAFI 一棵树）→ `bgp_dest`（前缀节点）→ `bgp_path_info`（一条路径）。
`adj_in` 存原始入向属性（供 soft-reconfig），`adj_out` 存已发出记录（避免重复发送）。
</details>

<details><summary>5. 一条 UPDATE 从 socket 到 RIB 的关键函数链？</summary>

`bgp_read`（[bgp_io.c:736](bgpd/bgp_io.c#L736)）→ `read_ibuf_work`（[:292](bgpd/bgp_io.c#L292)）
→ 入队 + 唤醒 → `bgp_process_packet`（[bgp_packet.c:4316](bgpd/bgp_packet.c#L4316)）
→ `bgp_update_receive`（[:2520](bgpd/bgp_packet.c#L2520)）→ `bgp_attr_parse`（[:2652](bgpd/bgp_packet.c#L2652)）
+ `bgp_nlri_parse`（[:395](bgpd/bgp_packet.c#L395)）→ `bgp_update`（[bgp_route.c:6210](bgpd/bgp_route.c#L6210)）
→ `bgp_process`（[:5607](bgpd/bgp_route.c#L5607)）→ `bgp_best_selection`（[:3592](bgpd/bgp_route.c#L3592)）。
</details>

<details><summary>6. 出向策略在哪里应用？只有一处吗？</summary>

对每个「前缀 × subgroup」只有一处：`subgroup_announce_check`（[bgp_route.c:2665](bgpd/bgp_route.c#L2665)），
由 `subgroup_process_announce_selected`（[:4074](bgpd/bgp_route.c#L4074)）调用。出向 route-map、
团体过滤、SoO 防环、下一跳改写都在这里。
</details>

<details><summary>7. update-group 解决什么问题？「组」和「子组」为什么分两层？</summary>

解决「同一策略的 N 个邻居要发同样路由 → 编码 N 次」。组 = 出向属性相同的 `peer_af` 集合；
子组 = 组内可共享同一份待发报文的子集（受发送同步点影响）。
关键函数见 [bgp_updgrp.c:1216](bgpd/bgp_updgrp.c#L1216) 起的 `update_group_*`。
</details>

<details><summary>8. bgpd 怎么把路由装进内核？</summary>

从不直接写内核。`bgp_zebra_announce_table`（[bgp_zebra.c:2058](bgpd/bgp_zebra.c#L2058)）排入
`bm->zebra_announce_head`，由 `bgp_handle_route_announcements_to_zebra`（[:2187](bgpd/bgp_zebra.c#L2187)）
按序经 zclient 发给 zebra。
</details>

<details><summary>9. FSM 有几个状态？比 RFC 4271 多了什么？</summary>

[bgpd.h:1519](bgpd/bgpd.h#L1519)：`Idle/Connect/Active/OpenSent/OpenConfirm/Established` 六个 RFC 状态，
外加非 RFC 的 `Clearing`（正在清路由）与 `Deleted`（终态）。
</details>

<details><summary>10. 所有状态迁移的唯一落库点是哪个函数？</summary>

`bgp_fsm_change_status`（[bgp_fsm.c:2107](bgpd/bgp_fsm.c#L2107)）。
</details>

<details><summary>11. `bgp_path_info_extra` 为什么单独分配？</summary>

内存优化：`bgp_path_info` 可能有几百万条，而需要 extra 的不到 10%。
代价是访问前必须判空。
</details>

<details><summary>12. `bgp_route.c` 有 19,000 行，怎么快速定位？</summary>

它有 13 个 `/* ========== N. ========== */` 分区标记，从 [bgp_route.c:124](bgpd/bgp_route.c#L124) 起，
可直接当目录（见 §4.3 表）。
</details>

<details><summary>13. `bgp_io.h` 里 `bgp_io_start`/`bgp_io_stop` 是干什么的？</summary>

**没有实现**。全树只有声明（[bgp_io.h:66](bgpd/bgp_io.h#L66)、[:78](bgpd/bgp_io.h#L78)），
属历史遗留；I/O 线程实际用 `frr_pthread_attr_default.start`。
</details>

<details><summary>14. 「实例」和「进程」是什么关系？</summary>

一个进程（`bm`）可含多个实例（`bm->bgp` 是 list）：default、若干 VRF、若干 view。
每个实例独立持有一棵 `rib[AFI][SAFI]`。
</details>

<details><summary>15. 属性 intern 是什么意思？为什么重要？</summary>

收到的属性经哈希去重后全局共享 + 引用计数，大量路径指向同一份内存。
不知道这点会误判内存开销、也可能写出重复释放或漏释放的代码。
</details>

---

## §13 代码索引（按建议阅读顺序）

| 步 | 读什么 | 目标认知 |
|---|---|---|
| 1 | [bgpd.h](bgpd/bgpd.h)（重点 168/657/1247/1389/1493/1519/1580/1684） | 建立四条线的对象模型（§0.2） |
| 2 | [bgp_main.c](bgpd/bgp_main.c) 全文（639 行） | 守护进程入口、信号、参数 |
| 3 | [bgpd.c:10416-10456](bgpd/bgpd.c#L10416) | 三线程搭建与收尾 |
| 4 | [bgp_io.c](bgpd/bgp_io.c)（104/190/226/292/386/512/736/810） | 跨线程交接全貌（§0.3） |
| 5 | [bgp_packet.c:4286-4460](bgpd/bgp_packet.c#L4286) | 主线程分派表 |
| 6 | [bgp_fsm.c](bgpd/bgp_fsm.c)（136/2107/2247/2757/2914/3073） | FSM 表驱动与建立路径 |
| 7 | [bgp_table.h:55/136](bgpd/bgp_table.h#L55) + [bgp_route.h:265/343](bgpd/bgp_route.h#L265) | RIB 三层模型 |
| 8 | [bgp_route.c](bgpd/bgp_route.c) 按 §4.3 的 13 个分区跳读 | 路由入向/选路/出向/聚合/show |
| 9 | [bgp_updgrp.h:159/224](bgpd/bgp_updgrp.h#L159) + [bgp_updgrp.c:1216-1400](bgpd/bgp_updgrp.c#L1216) | update-group 分层 |
| 10 | [bgp_updgrp_packet.c:798](bgpd/bgp_updgrp_packet.c#L798) | 报文共享编码 |
| 11 | [bgp_zebra.c](bgpd/bgp_zebra.c)（2058/2099/2187/2438/4830） | 南向下发与有序通告 |
| 12 | 按需进入卷2~卷6 对应分册 | 子系统细节 |

---

## §14 与官方 `doc/developer/*.rst` 的对应

| 官方文档 | 本套文档对应 |
|---|---|
| `doc/developer/bgpd.rst` | 全篇；尤以 §3 对象模型、§5 FSM、§7 update-group |
| `doc/developer/bgp-typecodes.rst` | 卷3（属性类型码）、卷2（属性容器） |
| `doc/developer/bmp.rst` | 卷6 BMP 节 |
| `doc/developer/hooks.rst` | 卷6（bgpd 的进程级 hook） |
| `doc/developer/tracing.rst` | 卷3（`bgp_trace.h` / LTTng tracepoint） |
| `doc/developer/northbound*.rst` | 卷5（route-map 北向）、卷6（配置面双轨） |
| `doc/user/bgp.rst` | 各分册的 CLI 小节（用户手册视角） |
| `FRR_LIBFRR_STUDY.md` | §2 启动主干、§9 MTYPE（`bgp_memory.c`）、§10 信号 |
| `FRR_PTHREAD_STUDY.md` | §0.3 三线程模型的底层机制 |
| `FRR_IPC_STUDY.md` | §8 zclient、§9 VTY / vtysh |

---

## 附录 A：`rfapi/` 子系统（默认不编译）

`bgpd/rfapi/` 下有 **34 个文件、33,213 行**，由 `--enable-bgp-vnc` 控制
（见 `bgpd/subdir.am:85` 的 `if ENABLE_BGP_VNC`）。**默认构建不包含它**，
因此本套文档的主体不覆盖其实现细节，此处只给出生效条件与文件分组，便于需要时定位。

RFAPI（RF API，前身叫 VNC = Virtual Network Control）是一套基于 BGP 的
「虚拟网络地址映射（NVE ↔ NVE）」机制，用于数据中心 overlay 的控制面。

| 分组 | 文件 | 职责 |
|---|---|---|
| 配置与主体 | `bgp_rfapi_cfg.c` `rfapi.c` | VNC/RFAPI 配置、注册与总控 |
| RIB 与导入 | `rfapi_import.c` `rfapi_rib.c` `rfapi_ap.c` | 导入/导出路由、Application Prefix |
| NVE 与监控 | `rfapi_nve_addr.c` `rfapi_monitor.c` | NVE 地址解析、注册监控 |
| 封装与描述符 | `rfapi_encap_tlv.c` `rfapi_descriptor_rfp_utils.c` | VNC 封装 TLV、RFP 描述符工具 |
| 与 BGP/zebra 对接 | `vnc_export_bgp.c` `vnc_import_bgp.c` `vnc_export_table.c` `vnc_zebra.c` | 双向导出导入、zebra 交互 |
| CLI 与调试 | `rfapi_vty.c` `vnc_debug.c` | 命令与调试开关 |

> 与主模块的接缝：`bgpd.c` 的 `bgp_terminate` 等处在 `#if ENABLE_BGP_VNC` 下调 `rfapi_*`。
> `bgp_vnc_types.h`（[bgpd/bgp_vnc_types.h](bgpd/bgp_vnc_types.h)，40 行）在主目录下，
> 属条件编译但仍计入 131 文件之列。

---

## 结语

bgpd 的复杂度不在任何单个函数，而在**三个维度同时叠加**：
「多实例 × 多地址族 × 多线程」，再乘上「6 个外部系统的集成面」。
本文给出的骨架 —— 四条对象线、三线程一条队列、RIB 三层、出向五跳 ——
是解开这个乘积的钥匙：任何一处代码，先问「它在哪条线上、在哪个线程里、对应哪个 AFI/SAFI」，
再问「它上下游是谁」，路径就会自己显形。

各子系统的机制细节，请进入 6 篇分册。

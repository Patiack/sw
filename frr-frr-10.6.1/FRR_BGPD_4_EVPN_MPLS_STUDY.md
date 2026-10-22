# FRR bgpd 深入剖析 · 卷4 —— EVPN 与 MPLS L3VPN

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 本卷覆盖文件 28 个、共 42125 行（工作树行数，含中文注释）。
>
> 写作目的：
> 1. **EVPN 的五种路由类型**（Type-1 EAD、Type-2 MAC/IP、Type-3 IMET、Type-4 ES、Type-5 IP-Prefix）
>    在代码里分别怎么表示？对应哪些 NLRI 编解码函数？
> 2. EVPN 的「**MAC-VRF**」与「**IP-VRF**」两种实例模型在 `struct bgp` 上怎么落地？
>    `bgp_evpn_private.h` 里的 `bgpevpn` / `bgp_evpn_info` 各存什么？
> 3. 「**本地 MAC 学习 → 生成 Type-2 通告 → 远端收到 → 装入 MAC 表**」的完整链路。
> 4. EVPN 多归属（MH，RFC 7432 §8 以太网段/DF 选举）在 `bgp_evpn_mh.c` 里怎么实现？DF 选举算法在哪？
> 5. `bgp_mac.c` 的 MAC 表（哈希表 `bm->self_mac_hash`）与 EVPN 的关系？
> 6. MPLS L3VPN（VPNv4/VPNv6, RFC 4364）：`bgp_mplsvpn.c` 的 VRF-to-VPN 导入导出（import/export RT）、
>    RD/RT 编解码、以及与 `struct vpn_policy` 的关系。
> 7. MPLS 标签：`bgp_label.c`（标签管理，与 zebra 的 LDE 交互）与 `bgp_labelpool.c`（动态标签池）
>    分别解决什么问题？两者边界在哪？
> 8. SRv6（RFC 8986 及 draft-ietf-bess-srv6-services）：`bgp_srv6.c` 与 `bgp_attr_srv6.h` 的 SID 结构与 TLV。
> 9. `bgp_encap_tlv.c` / `bgp_encap_types.h` 是**封装扩展团体**（RFC 9012 及各种隧道类型 TLV）的编解码器，
>    它被谁共用？通用性体现在哪里？
>
> 本卷是 `FRR_BGPD_STUDY.md`（总览篇）之下的分册。总览篇给出 bgpd 的整体架构、三线程模型、
> 核心数据结构与 131 文件全景地图；本卷只深入「EVPN 与 MPLS L3VPN 数据面」这一层，其余层次请在
> 对应分册中查阅。本文不修改任何源码。

---

## 本卷文件覆盖清单（自检用）

| # | 文件 | 行数 | 一句话职责 | 本卷小节 |
|---|------|------|-----------|---------|
| 1 | `bgp_evpn.c` | 10546 | EVPN 中枢实现：NLRI 编解码、导入导出、本地学习、下发 zebra | §1、§3 |
| 2 | `bgp_evpn.h` | 417 | EVPN 对外 API 契约与少量 inline 工具 | §2.1 |
| 3 | `bgp_evpn_vty.c` | 8584 | EVPN 的 CLI：命令解析、show 输出、配置读写 | §5 |
| 4 | `bgp_evpn_vty.h` | 91 | CLI 层对外声明与帮助字符串常量 | §5 |
| 5 | `bgp_evpn_mh.c` | 5934 | EVPN 多归属：ES/ES-EVI、Type-1/Type-4、DF 参数 | §4 |
| 6 | `bgp_evpn_mh.h` | 755 | 多归属的数据结构总表与接口 | §4.1 |
| 7 | `bgp_evpn_private.h` | 1171 | EVPN 内部数据结构、inline 工具、扩展团体编码器 | §2 |
| 8 | `bgp_attr_evpn.c` | 392 | EVPN 扩展团体字段的只读访问器与前缀构造 | §1.6 |
| 9 | `bgp_attr_evpn.h` | 119 | type-5 的 overlay index 承载结构 | §1.6 |
| 10 | `bgp_mac.c` | 542 | 本机 MAC 哈希表（self mac hash） | §3.6 |
| 11 | `bgp_mac.h` | 67 | 上述 MAC 表的接口 | §3.6 |
| 12 | `bgp_vpn.c` | 263 | `show ... vpn` 的 VPN 路由输出实现 | §6.5 |
| 13 | `bgp_vpn.h` | 38 | 上述 CLI 的单函数声明 | §6.5 |
| 14 | `bgp_vnc_types.h` | 40 | VNC Sub-TLV 类型号（`ENABLE_BGP_VNC` 下） | §6.6 |
| 15 | `bgp_mplsvpn.c` | 5103 | MPLS VPN / BGP-LU 数据面：NLRI 解析、VRF↔VPN 泄漏、标签/SID | §6 |
| 16 | `bgp_mplsvpn.h` | 609 | 上述接口与内联状态判定 | §6 |
| 17 | `bgp_mplsvpn_snmp.c` | 1810 | L3VPN MIB（RFC4382）取值实现 | §6.6 |
| 18 | `bgp_mplsvpn_snmp.h` | 44 | MIB 常量与初始化入口 | §6.6 |
| 19 | `bgp_label.c` | 844 | BGP 标签编解码、intern 共享、FEC 注册 | §7 |
| 20 | `bgp_label.h` | 253 | 标签内部表示与位操作 | §7 |
| 21 | `bgp_labelpool.c` | 1994 | 向 zebra 异步申请 label chunk 的标签池 | §7 |
| 22 | `bgp_labelpool.h` | 186 | 标签池接口与数据结构 | §7 |
| 23 | `bgp_srv6.c` | 424 | 本端 IPv6 前缀的 SRv6 SID 分配与通告 | §8 |
| 24 | `bgp_srv6.h` | 84 | 上述接口 | §8 |
| 25 | `bgp_encap_tlv.c` | 1163 | Tunnel Encap 属性 TLV 编解码（RFC 9012） | §9 |
| 26 | `bgp_encap_tlv.h` | 192 | 上述编解码接口 | §9 |
| 27 | `bgp_encap_types.h` | 385 | 隧道类型号、sub-TLV 类型与结构体定义 | §9 |
| 28 | `bgp_attr_srv6.h` | 75 | SRv6 Prefix-SID 属性的内部表示 | §8.3 |

> **覆盖率自检**：上表与本卷分配到的文件清单逐一对齐，一个不漏、一个不多（28 个）。

---

## §0 本卷总览

### 0.1 先给结论

> **EVPN 与 MPLS L3VPN 在 bgpd 里是「两条共存的多协议承载路径」：EVPN 把 MAC/IP 当 NLRI，L3VPN 把带 RD 的 IPv4/IPv6 前缀当 NLRI；两者共用同一批底层设施——RD/RT、MPLS 标签、导入导出（leak）框架、以及 Tunnel Encap 扩展团体。**

把这句话拆开，是本卷最值得记住的几条判断：

| # | 判断 | 关键证据 |
|---|------|---------|
| ① | **五种 EVPN 路由类型不是五个独立模块，而是同一个 `struct evpn_addr` union 的五个成员**，靠 `route_type` 字节选择 | [lib/prefix.h:192](lib/prefix.h#L192) 的 union；[lib/prefix.h:69-73](lib/prefix.h#L69) 的类型枚举 |
| ② | **MAC-VRF（二层）与 IP-VRF（三层）用两个不同的对象承载**：二层是 `struct bgpevpn`，三层直接复用 `struct bgp`（VRF 实例） | [bgp_evpn_private.h:118](bgpd/bgp_evpn_private.h#L118) 的 `bgpevpn`；[bgpd.h:1019](bgpd/bgpd.h#L1019) 起的一堆 EVPN 哈希字段 |
| ③ | **EVPN 的「导入」是把全局表（`rib[AFI_L2VPN][SAFI_EVPN]`）里的路由按 RT 落到各 VNI 的 `mac_table`/`ip_table` 或各 VRF**，与 L3VPN 的 leak 是同一套哲学 | [bgp_evpn.c:8347](bgpd/bgp_evpn.c#L8347) → [bgp_evpn.c:5439](bgpd/bgp_evpn.c#L5439) |
| ④ | **DF（指定转发者）选举算法实际运行在 zebra，不在 bgpd**；`bgp_evpn_mh.c` 只负责控制面插桩（ES 对象、ESR 生成、DF 偏好值随扩展团体传播） | election 在 [zebra/zebra_evpn_mh.c:1756](zebra/zebra_evpn_mh.c#L1756)；bgpd 侧只在 [bgp_evpn_mh.c:1790](bgpd/bgp_evpn_mh.c#L1790) 学参数 |
| ⑤ | **「标签」在 bgpd 里有两条完全不同的供给链**：`bgp_label.c` 管「BGP 报文里怎么带标签、向 FEC 注册」；`bgp_labelpool.c` 管「本机出标签从哪个 chunk 里切」 | [bgp_label.c:456](bgpd/bgp_label.c#L456) vs [bgp_labelpool.c:476](bgpd/bgp_labelpool.c#L476) |
| ⑥ | **Tunnel Encap 的 TLV 编解码器 `bgp_encap_tlv.c` 在生产路径上只有 EVPN 一个真实用户**（VNC/rfapi 是可选编译的另一用户），并非「EVPN 与 flowspec 共用」 | [bgp_route.c:9014](bgpd/bgp_route.c#L9014)、[rfapi/rfapi_encap_tlv.c:74](bgpd/rfapi/rfapi_encap_tlv.c#L74)；flowspec 目录里无引用（见 §11 勘误） |

### 0.2 文件职责分层表

| 层 | 文件 | 说明 |
|---|------|------|
| **公共定义** | [bgp_encap_types.h](bgpd/bgp_encap_types.h)、[bgp_vnc_types.h](bgpd/bgp_vnc_types.h) | 纯数据/枚举定义，被编解码实现引用 |
| **EVPN 核心** | [bgp_evpn.c](bgpd/bgp_evpn.c)、[bgp_evpn_private.h](bgpd/bgp_evpn_private.h)、[bgp_evpn.h](bgpd/bgp_evpn.h)、[bgp_attr_evpn.c](bgpd/bgp_attr_evpn.c) | 表结构、NLRI、导入导出、本地事件 |
| **EVPN 多归属** | [bgp_evpn_mh.c](bgpd/bgp_evpn_mh.c)、[bgp_evpn_mh.h](bgpd/bgp_evpn_mh.h) | ES / ES-EVI / Type-1 / Type-4 / DF 参数 |
| **EVPN CLI** | [bgp_evpn_vty.c](bgpd/bgp_evpn_vty.c)、[bgp_evpn_vty.h](bgpd/bgp_evpn_vty.h) | 命令与 show（不碰协议） |
| **MAC 索引** | [bgp_mac.c](bgpd/bgp_mac.c)、[bgp_mac.h](bgpd/bgp_mac.h) | 本机 MAC 哈希表 |
| **L3VPN 核心** | [bgp_mplsvpn.c](bgpd/bgp_mplsvpn.c)、[bgp_mplsvpn.h](bgpd/bgp_mplsvpn.h) | VPN NLRI、leak 框架、标签/SID 分配 |
| **L3VPN 展示/网管** | [bgp_vpn.c](bgpd/bgp_vpn.c)、[bgp_vpn.h](bgpd/bgp_vpn.h)、[bgp_mplsvpn_snmp.c](bgpd/bgp_mplsvpn_snmp.c)、[bgp_mplsvpn_snmp.h](bgpd/bgp_mplsvpn_snmp.h) | VPN 路由 show 与 SNMP MIB |
| **标签** | [bgp_label.c](bgpd/bgp_label.c)、[bgp_label.h](bgpd/bgp_label.h)、[bgp_labelpool.c](bgpd/bgp_labelpool.c)、[bgp_labelpool.h](bgpd/bgp_labelpool.h) | 标签编解码/共享 + 标签池 |
| **SRv6** | [bgp_srv6.c](bgpd/bgp_srv6.c)、[bgp_srv6.h](bgpd/bgp_srv6.h)、[bgp_attr_srv6.h](bgpd/bgp_attr_srv6.h) | 本端 SID 分配 + 对端属性表示 |
| **封装 TLV** | [bgp_encap_tlv.c](bgpd/bgp_encap_tlv.c)、[bgp_encap_tlv.h](bgpd/bgp_encap_tlv.h)、[bgp_encap_types.h](bgpd/bgp_encap_types.h) | Tunnel Encap 属性编解码 |

### 0.3 全景图：EVPN 与 L3VPN 两条数据面

```
                         ┌─────────────────── bgpd 公共设施 ───────────────────┐
                         │  RD/RT(扩展团体)   MPLS 标签(bgp_label*)   Encap TLV │
                         │  bgp_attr intern   导入导出框架(leak)                │
                         └─────────┬──────────────────────────┬────────────────┘
                                   │                          │
        ┌─────────────────── EVPN ──┴──────┐      ┌──── MPLS L3VPN (VPNv4/v6) ────┐
        │ 全局表 rib[AFI_L2VPN][SAFI_EVPN] │      │ 全局表 rib[AFI_IP/6][SAFI_    │
        │   (默认实例 bgp_get_evpn())       │      │        MPLS_VPN]  (每个 RD 一层)│
        │        │ 按 RT 导入               │      │        │ 按 RT 导入(ecommunity_ │
        │        ▼                          │      │        ▼   include)             │
        │  L2 VNI(struct bgpevpn)          │      │  VRF 实例(struct bgp)          │
        │   mac_table / ip_table            │      │   → vpn_policy[afi]            │
        │  L3 VNI → bgp_vrf(struct bgp)     │      │                                │
        └───────┬───────────────┬───────────┘      └───────┬───────────────┬────────┘
                │               │                          │               │
       zebra 本地事件      下发给 zebra              zebra 标签回调   下发给 zebra
   bgp_evpn_local_*    evpn_zebra_install    bgp_reg_for_label_cb  bgp_zebra_route_install
                │               │                          │               │
                └────── bgp_zebra.c (zclient) ─────────────┴───────────────┘
                                   │
                              zebra 进程
                (FDB/NHG、L2/L3 VNI、DF 选举、标签管理器/LDE)
```

### 0.4 与相邻卷的边界

| 问题 | 去哪卷看 |
|------|---------|
| `bgp_update` / `bgp_withdraw` / 路由选择的通用实现 | 路由篇（`bgp_route.c`），非本卷文件 |
| `bgp_attr_intern`、`bgp_attr.c` 的属性解析框架 | 属性篇（`bgp_attr.c`）。本卷只引用其解析入口 `bgp_attr_encap` |
| `struct ecommunity` / RT 集合的构造与比较 | 属性篇（`bgp_ecommunity.c`）。本卷只引用 `ecommunity_include` |
| RD 编解码的实现 | 属性篇（`bgp_rd.c`）。本卷只在 §6.5 说明「谁在用它」 |
| VTY/CLI 框架、JSON 输出机制 | CLI 篇（`bgp_vty.c`、`lib/json.h`） |
| zclient 消息机制 | zebra 交互篇（`bgp_zebra.c`） |

> 一句话边界：**本卷只负责「EVPN / L3VPN 这两个 SAFI 的值语义与状态机」**；它们落到 BGP 报文、属性、表、CLI、zebra 的通用机制都在别的卷。

---

## §1 EVPN 五种路由类型与 NLRI 编解码

### 1.1 类型枚举与地址结构

EVPN 的路由类型与地址结构**定义在 `lib/prefix.h`，不在 bgpd 目录**——因为整个 FRR 路由栈的前缀比较/哈希都要认识 EVPN 前缀。这一点先记住，否则在 bgpd 里找不到 `BGP_EVPN_MAC_IP_ROUTE` 的定义。

五种类型是 [lib/prefix.h:69-73](lib/prefix.h#L69)：

```c
typedef enum {
        BGP_EVPN_AD_ROUTE = 1,    /* Type-1  Ethernet Auto-Discovery */
        BGP_EVPN_MAC_IP_ROUTE,    /* Type-2  MAC/IP Advertisement  */
        BGP_EVPN_IMET_ROUTE,      /* Type-3  Inclusive Multicast Ethernet Tag */
        BGP_EVPN_ES_ROUTE,        /* Type-4  Ethernet Segment */
        BGP_EVPN_IP_PREFIX_ROUTE, /* Type-5  IP Prefix */
} bgp_evpn_route_type;
```

五类的**统一前缀**是 `struct evpn_addr`（[lib/prefix.h:192](lib/prefix.h#L192)）：一个 `route_type` 字节 + 一个 union，union 的五个成员分别对应五类：

| 类型 | 枚举值 | union 成员 | 成员结构体定义 | 关键字段 |
|------|--------|-----------|---------------|---------|
| Type-1 EAD | `BGP_EVPN_AD_ROUTE=1` | `ead_addr` | [lib/prefix.h:154](lib/prefix.h#L154) | ESI + eth_tag + 对端 IP + frag_id |
| Type-2 MAC/IP | `BGP_EVPN_MAC_IP_ROUTE=2` | `macip_addr` | [lib/prefix.h:161](lib/prefix.h#L161) | eth_tag + ip_prefix_length + mac + ip |
| Type-3 IMET | `BGP_EVPN_IMET_ROUTE=3` | `imet_addr` | [lib/prefix.h:168](lib/prefix.h#L168) | eth_tag + ip_prefix_length + ip（源 IP） |
| Type-4 ES | `BGP_EVPN_ES_ROUTE=4` | `es_addr` | [lib/prefix.h:174](lib/prefix.h#L174) | esi + ip_prefix_length + ip |
| Type-5 IP-Prefix | `BGP_EVPN_IP_PREFIX_ROUTE=5` | `prefix_addr` | [lib/prefix.h:180](lib/prefix.h#L180) | eth_tag + ip_prefix_length + ip 前缀 |

union 成员有简写别名（[lib/prefix.h:198-202](lib/prefix.h#L198)）：`evpn->macip_addr` 等价于 `evpn->u._macip_addr`。**统一前缀长度**取结构体字节数 ×8，见 [bgp_evpn_private.h:61](bgpd/bgp_evpn_private.h#L61) 的 `EVPN_ROUTE_PREFIXLEN`。

### 1.2 五类型的「键」与编解码函数对照

EVPN 每条路由的表键 = （RD, 类型相关字段），而标签槽里装的其实是 VNI。下表把「类型 → 前缀构造 → 编码 → 解析」串起来：

| 类型 | 构造前缀的 inline | 编码入口 | 解析分派到的函数 | 解析函数定义 |
|------|------------------|---------|-----------------|-------------|
| Type-1 EAD | [bgp_evpn_private.h:782](bgpd/bgp_evpn_private.h#L782) `build_evpn_type1_prefix` | `bgp_evpn_encode_prefix` 的 `case BGP_EVPN_AD_ROUTE` | `bgp_evpn_type1_route_process` | [bgp_evpn_mh.c:1482](bgpd/bgp_evpn_mh.c#L1482) |
| Type-2 MAC/IP | [bgp_evpn_private.h:700](bgpd/bgp_evpn_private.h#L700) `build_evpn_type2_prefix` | 同上 `case BGP_EVPN_MAC_IP_ROUTE` | `process_type2_route` | [bgp_evpn.c:5915](bgpd/bgp_evpn.c#L5915) |
| Type-3 IMET | [bgp_evpn_private.h:743](bgpd/bgp_evpn_private.h#L743) `build_evpn_type3_prefix` | 同上 `case BGP_EVPN_IMET_ROUTE` | `process_type3_route` | [bgp_evpn.c:6061](bgpd/bgp_evpn.c#L6061) |
| Type-4 ES | [bgp_evpn_private.h:760](bgpd/bgp_evpn_private.h#L760) `build_evpn_type4_prefix` | 同上 `case BGP_EVPN_ES_ROUTE` | `bgp_evpn_type4_route_process` | [bgp_evpn_mh.c:948](bgpd/bgp_evpn_mh.c#L948) |
| Type-5 IP-Prefix | [bgp_evpn_private.h:716](bgpd/bgp_evpn_private.h#L716) `build_type5_prefix_from_ip_prefix` | 委托 `evpn_mpattr_encode_type5` | `process_type5_route` | [bgp_evpn.c:6164](bgpd/bgp_evpn.c#L6164) |

> **关键分工**：Type-1 与 Type-4 的**解析**在 `bgp_evpn_mh.c`（它们本质是「以太网段」语义，归多归属模块），但**编码**统一在 `bgp_evpn.c` 的 `bgp_evpn_encode_prefix` 里——编码只是按结构体写字节，不需要 MH 状态。

### 1.3 编码路径 `bgp_evpn_encode_prefix`

定义在 [bgp_evpn.c:7727](bgpd/bgp_evpn.c#L7727)。签名见 [bgp_evpn.h:246](bgpd/bgp_evpn.h#L246)。它把一条 EVPN 前缀写进 MP_REACH 流，逻辑是一个 `switch (evp->prefix.route_type)`：

- **先写 addpath_tx_id**（若 `addpath_capable`），再写 route_type 字节。
- **Type-2**（[bgp_evpn.c:7746](bgpd/bgp_evpn.c#L7746)）：`len = 8(RD)+10(ESI)+4(EthTag)+1+6(MAC)+1+ipa_len(IP)+3(VNI)`；若带 IP 且 `num_labels>1` 再 `+3`（第二个标签 = L3VNI）。**这是双标签的由来**（`VNI_FLAG_USE_TWO_LABELS`）。
- **Type-3**（[bgp_evpn.c:7782](bgpd/bgp_evpn.c#L7782)）：固定 13 字节 + 源 IP 4/16。
- **Type-4**（[bgp_evpn.c:7806](bgpd/bgp_evpn.c#L7806)）：固定 19 字节 + 源 IP；注释明确「RFC-7432 ES NLRI 19」。
- **Type-1**（[bgp_evpn.c:7830](bgpd/bgp_evpn.c#L7830)）：`RD + ESI + EthTag + 1 VNI`。
- **Type-5**：委托 [bgp_evpn.c:6317](bgpd/bgp_evpn.c#L6317) `evpn_mpattr_encode_type5`（见下）。

**Type-5 是唯一特殊的**：它不写「MAC」，而是写「IP 前缀 + 可选网关 IP」，且 ESI/网关按属性里的 overlay index 决定是否真的写（[bgp_evpn.c:6345](bgpd/bgp_evpn.c#L6345) 起）：

```c
if (attr && bre && bre->type == OVERLAY_INDEX_ESI)
        stream_put(s, &attr->esi, sizeof(esi_t));
else
        stream_put(s, 0, sizeof(esi_t));
...
if (attr && bre && bre->type == OVERLAY_INDEX_GATEWAY_IP) { ... }
```

这正是 [bgp_attr_evpn.h](bgpd/bgp_attr_evpn.h) 那个 overlay index 结构的用途（§1.6）。

### 1.4 解码路径 `bgp_nlri_parse_evpn`

定义在 [bgp_evpn.c:7853](bgpd/bgp_evpn.c#L7853)。骨架非常规整（[bgp_evpn.c:7859](bgpd/bgp_evpn.c#L7859) 起）：循环读 `type + length`，做溢出检查 `pnt + psize > lim`，再 `switch (rtype)` 分派。**withdraw 为真时把 attr 置 NULL** 交给子函数（子函数据 attr 是否为 NULL 决定 `bgp_update` 还是 `bgp_withdraw`）。

- 唯一可能的多重分派细节：addpath 时每条 NLRI 前有 4 字节 path-id。
- 收尾校验 `pnt != lim` 即报 `BGP_NLRI_PARSE_ERROR_PACKET_LENGTH`。
- 各错误码见 `BGP_NLRI_PARSE_ERROR_EVPN_TYPE{1..5}_SIZE`。

**Type-2 解析**（[bgp_evpn.c:5915](bgpd/bgp_evpn.c#L5915)）值得单独看：它先校验长度只能是 **33/37/49** 或带第二标签的 **36/40/52**，再把 `RD→ESI→EthTag→MAC len→MAC→IP len→IP→VNI[+VNI]` 依次解出，最后调 `bgp_update`/`bgp_withdraw`（[bgp_evpn.c:6028](bgpd/bgp_evpn.c#L6028)）。

### 1.5 NLRI 编解码的「方向陷阱」

- 编码函数返回 `void`、不报错；解析函数返回 `int` 错误码。**这是 EVPN 与 encap TLV 共有的命名/方向约定**，读代码时不要以为编码一定成功。
- `process_type2_route` 里 `attr` 既是**输出**（把 `attr->esi` 填上）又是**输入**（后续 `bgp_update` 用它）——它就地修改传入的 attr。撤销路径传 NULL 时用 `STREAM_FORWARD_GETP` 跳过 ESI 字段。

### 1.6 `bgp_attr_evpn.c` / `bgp_attr_evpn.h`：扩展团体字段的只读访问器

这两个文件不是 NLRI 编解码器，而是「**从 attr 的扩展团体里查出某个 EVPN 子字段**」的只读工具集合：

| 函数 | 定义 | 作用 |
|------|------|------|
| `bgp_route_evpn_same` | [bgp_attr_evpn.c:51](bgpd/bgp_attr_evpn.c#L51) | 比较两条 EVPN overlay 信息（type/ESI/gw_ip）是否等价；允许 NULL 参数 |
| `bgp_add_routermac_ecom` | [bgp_attr_evpn.c:71](bgpd/bgp_attr_evpn.c#L71) | 加 Router MAC 扩展团体 |
| `str2esi` | [bgp_attr_evpn.c:98](bgpd/bgp_attr_evpn.c#L98) | ESI 字符串 → 结构体 |
| `ecom_mac2str` | [bgp_attr_evpn.c:124](bgpd/bgp_attr_evpn.c#L124) | 扩展团体里的 MAC → 字符串 |
| `bgp_attr_rmac` | [bgp_attr_evpn.c:141](bgpd/bgp_attr_evpn.c#L141) | 取 Router MAC 扩展团体 |
| `bgp_attr_default_gw` | [bgp_attr_evpn.c:178](bgpd/bgp_attr_evpn.c#L178) | 默认网关扩展团体 |
| `bgp_attr_df_pref_from_ec` | [bgp_attr_evpn.c:217](bgpd/bgp_attr_evpn.c#L217) | 从 DF Election 扩展团体取 `df_pref`/`df_alg` |
| `bgp_attr_mac_mobility_seqnum` | [bgp_attr_evpn.c:261](bgpd/bgp_attr_evpn.c#L261) | MAC Mobility 序号（sticky 判定用） |
| `bgp_attr_evpn_na_flag` | [bgp_attr_evpn.c:314](bgpd/bgp_attr_evpn.c#L314) | EVPN ND 标志（router/proxy） |
| `bgp_build_evpn_prefix` | [bgp_attr_evpn.c:358](bgpd/bgp_attr_evpn.c#L358) | 把普通 IPv4/IPv6 前缀「伪造」成 type-5 EVPN 前缀 |

`bgp_attr_evpn.h` 的核心是 **overlay index 的四态**枚举（[bgp_attr_evpn.h](bgpd/bgp_attr_evpn.h) 中的 `enum overlay_index_type`）与 `struct bgp_route_evpn` 结构。注意头文件导读强调的坑：**枚举有四态，但结构体只装两种数据**——`OVERLAY_INDEX_MAC` 的 MAC 不在本结构里，而在 Router MAC 扩展团体中，本字段只起「去 RMAC 找」的指示作用。

---

## §2 MAC-VRF / IP-VRF 实例模型

### 2.1 `struct bgp` 上的 EVPN 字段

EVPN 不是「另起一个全局对象」，而是**往 `struct bgp` 里挂了一批哈希表与标志**（[bgpd.h:1019](bgpd/bgpd.h#L1019) 起）：

| 字段 | 定义 | 语义 |
|------|------|------|
| `vnihash` | [bgpd.h:1019](bgpd/bgpd.h#L1019) | **EVI 哈希表**：键 = VNI 号，值 = `struct bgpevpn *`。这是二层 MAC-VRF 的主索引 |
| `vni_svi_hash` | [bgpd.h:1027](bgpd/bgpd.h#L1027) | 以 **SVI ifindex** 为键，用于网关 IP overlay index 的递归查表（[bgp_evpn_private.h:206](bgpd/bgp_evpn_private.h#L206) 的 `remote_ip_hash` 与之配合） |
| `advertise_gw_macip` | [bgpd.h:1030](bgpd/bgpd.h#L1030) | 是否通告网关 MAC/IP |
| `advertise_all_vni` | [bgpd.h:1033](bgpd/bgpd.h#L1033) | **`EVPN_ENABLED(bgp)` 判定的就是这个位**（[bgp_evpn.h:62](bgpd/bgp_evpn.h#L62)） |
| `evpn_info` | [bgpd.h:1040](bgpd/bgpd.h#L1040) | 指向 `struct bgp_evpn_info`（默认实例的全局参数） |
| `import_rt_hash` | [bgpd.h:1051](bgpd/bgpd.h#L1051) | Import RT → EVI 的映射（`struct irt_node`） |
| `vrf_import_rt_hash` | [bgpd.h:1054](bgpd/bgpd.h#L1054) | VRF Import RT → VRF 的映射（`struct vrf_irt_node`） |
| `l3vni` | [bgpd.h:1057](bgpd/bgpd.h#L1057) | 本实例作 **IP-VRF** 时对应的 L3 VNI |
| `vrf_import_rtl` / `vrf_export_rtl` | [bgpd.h:1108](bgpd/bgpd.h#L1108)、[1111](bgpd/bgpd.h#L1111) | VRF 上的 RT 列表（元素 `struct vrf_route_target`） |
| `l2vnis` | [bgpd.h:1114](bgpd/bgpd.h#L1114) | 本 IP-VRF 关联的 L2 VNI（`struct bgpevpn *`）列表 |

> **两种实例模型的落地方式**：
> - **MAC-VRF（二层 EVI）** 是**独立堆对象** `struct bgpevpn`，挂在默认 EVPN 实例的 `vnihash` 上；
> - **IP-VRF（三层）** 就是**一个普通的 `struct bgp` VRF 实例**，只是多了 `l3vni` / `vrf_import_rtl` / `l2vnis` 等 EVPN 字段。
>
> 换句话说：「L3 VNI」不另建对象，而是给 VRF 实例「打标」。

### 2.2 `struct bgpevpn`（MAC-VRF）

定义在 [bgp_evpn_private.h:118](bgpd/bgp_evpn_private.h#L118)。它是 EVPN 的「中枢」，字段可归为四组：

**① 标识与关联**
- `vni`（[bgp_evpn_private.h:120](bgpd/bgp_evpn_private.h#L120)）：VNI 号，同时是 `vnihash` 的键。
- `tenant_vrf_id`（[bgp_evpn_private.h:122](bgpd/bgp_evpn_private.h#L122)）：所属租户 VRF。
- `bgp_vrf`（[bgp_evpn_private.h:149](bgpd/bgp_evpn_private.h#L149)）：回指三层 VRF 实例（持引用）。

**② 状态标志**：`flags`（[bgp_evpn_private.h:126](bgpd/bgp_evpn_private.h#L126)），位定义见 [bgp_evpn_private.h:136-146](bgpd/bgp_evpn_private.h#L136)：

| 标志 | 值 | 含义 |
|------|----|------|
| `VNI_FLAG_CFGD` | 0x1 | 用户显式配置 |
| `VNI_FLAG_LIVE` | 0x2 | 已上线（可学习/转发） |
| `VNI_FLAG_RD_CFGD` | 0x4 | RD 手工配置 |
| `VNI_FLAG_IMPRT_CFGD` | 0x8 | Import RT 手工配置 |
| `VNI_FLAG_EXPRT_CFGD` | 0x10 | Export RT 手工配置 |
| `VNI_FLAG_USE_TWO_LABELS` | 0x20 | Type-2 同时带 L2VNI + L3VNI |
| `VNI_FLAG_ADD` | 0x40 | L2VNI 添加流程中 |

**③ 表**（本卷问题 2 的关键）：
- `ip_table`（[bgp_evpn_private.h:212](bgpd/bgp_evpn_private.h#L212)）与 `mac_table`（[bgp_evpn_private.h:213](bgpd/bgp_evpn_private.h#L213)）：
  - `mac_table`：以 **MAC** 为键存 Type-2（纯 MAC 或 MAC+IP）；
  - `ip_table`：存带 IP 的 Type-2、Type-5 等 IP 相关条目。
- `remote_ip_hash`（[bgp_evpn_private.h:206](bgpd/bgp_evpn_private.h#L206)）：网关 IP → 一组 MAC/IP 路径，用于 overlay index 递归解析。

**④ 通告控制**：`advertise_gw_macip`、`advertise_subnet`、`advertise_svi_macip`、RD/RT 列表、ES-EVI 红黑树等。

生命周期由 `bgp_evpn_new` / `bgp_evpn_free` 管理（声明见 [bgp_evpn_private.h:1069](bgpd/bgp_evpn_private.h#L1069)、[1075](bgpd/bgp_evpn_private.h#L1075)），存于 `bgp->vnihash`。

### 2.3 `struct bgp_evpn_info`（默认实例全局参数）

定义在 [bgp_evpn_private.h:296](bgpd/bgp_evpn_private.h#L296)。挂在**默认 EVPN 实例**的 `bgp->evpn_info`（[bgpd.h:1040](bgpd/bgpd.h#L1040)）上，存的是**全局策略**而非某 VNI 的数据：

| 字段组 | 字段 | 语义 |
|--------|------|------|
| DAD | `dup_addr_detect`、`dad_time`、`dad_max_moves`、`dad_freeze`、`dad_freeze_time` | 重复地址检测（MAC 移动去重）参数 |
| 通告开关 | `advertise_svi_macip` | 全局是否通告 SVI MAC/IP |
| 防环 | `soo`（[bgp_evpn_private.h:328](bgpd/bgp_evpn_private.h#L328)） | **MAC-VRF Site-of-Origin** 扩展团体，多归属防环的关键 |
| PIP | `advertise_pip`、`pip_ip`、`pip_rmac` 系列、`is_anycast_mac` | Proxy IP / 任播网关 |

`soo` 的注释（[bgp_evpn_private.h:319-325](bgpd/bgp_evpn_private.h#L319)）写清了它的三条规则：从 L2VNI 导出的所有路由都带它；带匹配 SoO 的 Type-2/3 不导入 L2VNI；带匹配 SoO 的 Type-2/5 不导入 L3VNI。

### 2.4 `irt_node` / `vrf_irt_node`：RT 索引

EVPN 的「按 RT 导入」需要两个方向的索引，分别用两个结构体：

- `struct irt_node`（[bgp_evpn_private.h:248](bgpd/bgp_evpn_private.h#L248)）：**一个 Import RT → 多个 VNI**（二层）。含 RT 值和引用该 RT 的 `struct bgpevpn *` 列表（[bgp_evpn_private.h:253](bgpd/bgp_evpn_private.h#L253)）。
- `struct vrf_irt_node`（[bgp_evpn_private.h:266](bgpd/bgp_evpn_private.h#L266)）：**一个 Import RT → 多个 VRF**（三层）。

外加 `struct vrf_route_target`（[bgp_evpn_private.h:364](bgpd/bgp_evpn_private.h#L364)）：VRF 上一条 RT 的包装（带自动/通配标志），是 `vrf_import_rtl` / `vrf_export_rtl` 的元素。

这三者的关系：**收到一条 EVPN 路由 → 看它携带的 RT → 用 `irt_node`/`vrf_irt_node` 查出所有该导入的 VNI/VRF → 逐个安装**。映射在 [bgp_evpn.c:8031](bgpd/bgp_evpn.c#L8031) `bgp_evpn_map_vni_to_its_rts` 与 [bgp_evpn.c:7963](bgpd/bgp_evpn.c#L7963) `bgp_evpn_map_vrf_to_its_rts` 建立。

### 2.5 `bgp_evpn_private.h` 的 inline 工具与扩展团体编码器

这个头文件（1171 行）有大量 inline 小函数，分三类：

**① 标志位查询**：`is_vni_configured`（[bgp_evpn_private.h:488](bgpd/bgp_evpn_private.h#L488)）、`is_vni_live`（[:494](bgpd/bgp_evpn_private.h#L494)）、`is_l3vni_live`（[:501](bgpd/bgp_evpn_private.h#L501)）、`is_rd_configured`（[:507](bgpd/bgp_evpn_private.h#L507)）、`is_import_rt_configured`（[:521](bgpd/bgp_evpn_private.h#L521)）、`is_export_rt_configured`（[:527](bgpd/bgp_evpn_private.h#L527)）等。

**② 扩展团体「手搓」编码器**（直接按字节写协议格式，字段偏移是 RFC 规定）：

| 编码器 | 定义 | 编码内容 |
|--------|------|---------|
| `encode_es_rt_extcomm` | [bgp_evpn_private.h:538](bgpd/bgp_evpn_private.h#L538) | ES-Import Route Target |
| `encode_df_elect_extcomm` | [bgp_evpn_private.h:550](bgpd/bgp_evpn_private.h#L550) | DF Election 扩展团体（携带 `df_pref`） |
| `encode_esi_label_extcomm` | [bgp_evpn_private.h:564](bgpd/bgp_evpn_private.h#L564) | ESI Label（A-A 别名标签） |
| `encode_rmac_extcomm` | [bgp_evpn_private.h:577](bgpd/bgp_evpn_private.h#L577) | Router MAC（RFC9135） |
| `encode_default_gw_extcomm` | [bgp_evpn_private.h:588](bgpd/bgp_evpn_private.h#L588) | 默认网关（类型为 OPAQUE） |
| `encode_mac_mobility_extcomm` | [bgp_evpn_private.h:598](bgpd/bgp_evpn_private.h#L598) | MAC Mobility（RFC7432） |
| `encode_na_flag_extcomm` | [bgp_evpn_private.h:615](bgpd/bgp_evpn_private.h#L615) | ND 标志 |

**③ 前缀构造/拆解**：`build_evpn_type{1,2,3,4}_prefix`、`build_type5_prefix_from_ip_prefix`（[:716](bgpd/bgp_evpn_private.h#L716)）、`ip_prefix_from_type5_prefix`（[:630](bgpd/bgp_evpn_private.h#L630)）、`ip_prefix_from_type2_prefix`（[:667](bgpd/bgp_evpn_private.h#L667)）、`evpn_type1_prefix_global_copy`（[:809](bgpd/bgp_evpn_private.h#L809)）、`evpn_type2_prefix_global_copy`（[:869](bgpd/bgp_evpn_private.h#L869)）等。这些 `*_copy` 负责在「VNI 表前缀（MAC 或 IP）↔ 全局表前缀（MAC+IP）」之间补全字段——**全局表的前缀与 VNI 表的前缀不同构**，这是读 §3 前必须建立的认识。

`bgp_evpn.h`（对外契约）另有一批判定：`EVPN_ENABLED`（[bgp_evpn.h:62](bgpd/bgp_evpn.h#L62)）、`is_evpn_enabled`、`is_route_parent_evpn`（[bgp_evpn.h:158](bgpd/bgp_evpn.h#L158)，判定「根父路由是否来自 L2VPN/EVPN 表」）、`is_route_injectable_into_evpn`。文件末尾是成组的 extern 声明：导入导出（[bgp_evpn.h:216-281](bgpd/bgp_evpn.h#L216)）、本地事件（[bgp_evpn.h:294-330](bgpd/bgp_evpn.h#L294)）、zebra 下发（[bgp_evpn.h:391-409](bgpd/bgp_evpn.h#L391)）、查询（[bgp_evpn.h:342-380](bgpd/bgp_evpn.h#L342)）。

---

## §3 本地 MAC 学习 → Type-2 通告 → 远端装入 MAC 表

### 3.1 三段链路总览

```
[本端] zebra 从内核 FDB 学到 MAC
   │  zclient 事件
   ▼
bgp_evpn_local_macip_add()            bgp_evpn.c:8836
   │  build_evpn_type2_prefix()        bgp_evpn_private.h:700
   ▼
update_evpn_route()                   bgp_evpn.c:2679
   │  bgp_update()（进入本地 EVPN 表）
   ▼
路由选择 → bgp_evpn_encode_prefix()   bgp_evpn.c:7727 ──► 发往对端

[远端] 收到 UPDATE
   ▼
bgp_nlri_parse_evpn()                 bgp_evpn.c:7853
   │  process_type2_route()            bgp_evpn.c:5915
   ▼
bgp_update() → 全局表 rib[AFI_L2VPN][SAFI_EVPN]
   ▼
bgp_evpn_import_route()               bgp_evpn.c:8347
   │  install_uninstall_evpn_route()   bgp_evpn.c:5439
   ▼
install_evpn_route_entry_in_vni_mac() bgp_evpn.c:4196 ──► vpn->mac_table
   ▼
evpn_zebra_install()                  bgp_evpn.c:1480 ──► 下发 zebra 装 FDB
```

### 3.2 本地学习：`bgp_evpn_local_macip_add`

定义在 [bgp_evpn.c:8836](bgpd/bgp_evpn.c#L8836)。来自 zebra 的本地 MAC/IP 事件入口，逻辑很短：

1. `bgp_evpn_lookup_vni(bgp, vni)` 查 VNI 对象，不存在或 `!is_vni_live` 则告警返回（[bgp_evpn.c:8842](bgpd/bgp_evpn.c#L8842)）；
2. `build_evpn_type2_prefix(&p, mac, ip)` 构造 Type-2 前缀；
3. `update_evpn_route(bgp, vpn, &p, flags, seq, esi)` 建/更新路由。

对应删除入口是 `bgp_evpn_local_macip_del`（[bgp_evpn.c:8792](bgpd/bgp_evpn.c#L8792)）。**sticky 标志**（`ZEBRA_MACIP_TYPE_STICKY`）只影响日志措辞，路由创建统一走 `update_evpn_route`（见 [bgp_evpn.c:8831](bgpd/bgp_evpn.c#L8831) 注释）。

### 3.3 生成与通告：`update_evpn_route`

定义在 [bgp_evpn.c:2679](bgpd/bgp_evpn.c#L2679)（`static`）。它给指定 VNI 新建/更新一条本地 EVPN 路由并调度处理。真正「把本地路由变成可通告属性」的细节在 [bgp_evpn.c:2409](bgpd/bgp_evpn.c#L2409) 的 `update_evpn_route_entry` 与 [bgp_evpn.c:3022](bgpd/bgp_evpn.c#L3022) 的 `bgp_evpn_update_type2_route_entry`：

- `bgp_evpn_update_type2_route_entry`（[bgp_evpn.c:3022](bgpd/bgp_evpn.c#L3022)）把 **VNI 表前缀补全为全局前缀**（`evpn_type2_prefix_global_copy`，[bgp_evpn.c:3050](bgpd/bgp_evpn.c#L3050)），再按 `local_pi` 重建属性（默认属性 + originator_ip 下一跳、继承 `evpn_flags`/`es_flags`、按「本地且非 bypass」重设 `ATTR_ES_IS_LOCAL`），最后 `bgp_process`。
- 通告属性里的扩展团体由 [bgp_evpn.c:1281](bgpd/bgp_evpn.c#L1281) `build_evpn_route_extcomm` 组装：**所有路由都加 Encap（VXLAN）+ L2VNI 的 Export RT**，`add_l3_ecomm` 为真时再加 L3VNI 的 Export RT 与 Router MAC，另有 MAC Mobility（sticky）、默认网关、ND 标志等按 `evpn_flags` 视情况添加。

### 3.4 远端接收：`process_type2_route`

定义在 [bgp_evpn.c:5915](bgpd/bgp_evpn.c#L5915)。解析后直接 `bgp_update`（[bgp_evpn.c:6028](bgpd/bgp_evpn.c#L6028)）或 `bgp_withdraw`。这一步只把路由落到**全局 EVPN 表**；`bgp_update` 内部在选路/导入阶段会触发 EVPN 的导入回调。

### 3.5 导入到 VNI/VRF：`install_evpn_route_entry`

- 入口 `bgp_evpn_import_route`（[bgp_evpn.c:8347](bgpd/bgp_evpn.c#L8347)）→ `install_uninstall_evpn_route(bgp, ..., 1)`（[bgp_evpn.c:5439](bgpd/bgp_evpn.c#L5439)）。
- 按类型落到不同安装函数：

| 目标 | 函数 | 定义 |
|------|------|------|
| 装入 VRF（IP-VRF） | `install_evpn_route_entry_in_vrf` | [bgp_evpn.c:3773](bgpd/bgp_evpn.c#L3773) |
| 装入 VNI（IP 表） | `install_evpn_route_entry_in_vni_ip` | [bgp_evpn.c:4161](bgpd/bgp_evpn.c#L4161) |
| 装入 VNI（MAC 表） | `install_evpn_route_entry_in_vni_mac` | [bgp_evpn.c:4196](bgpd/bgp_evpn.c#L4196) |
| 通用入口 | `install_evpn_route_entry` / `uninstall_evpn_route_entry` | [bgp_evpn.c:4405](bgpd/bgp_evpn.c#L4405) / [:4460](bgpd/bgp_evpn.c#L4460) |
| 底层公共实现 | `install_evpn_route_entry_in_vni_common` | [bgp_evpn.c:3978](bgpd/bgp_evpn.c#L3978) |

> **这就是「远端装入 MAC 表」的落点**：`install_evpn_route_entry_in_vni_mac` 把对端 Type-2 装进该 VNI 的 `mac_table`。导入判定会**同时看 RT 匹配、SoO 防环、martian 过滤**（顺序与条件相互影响，见 [bgp_evpn.c:41-43](bgpd/bgp_evpn.c#L41) 导读）。

### 3.6 下发 zebra：`evpn_zebra_install` 与 `bgp_mac.c` 的关系

- 选路结果下发内核走 `evpn_zebra_install`（[bgp_evpn.c:1480](bgpd/bgp_evpn.c#L1480)）/ `evpn_zebra_uninstall`（[bgp_evpn.c:1610](bgpd/bgp_evpn.c#L1610)），由选路函数 `evpn_route_select_install`（[bgp_evpn.c:1746](bgpd/bgp_evpn.c#L1746)）调用。
- **`bgp_mac.c` 的 MAC 表是「本机 MAC 的存在性索引」，不是转发表**。它的哈希 `bm->self_mac_hash`（`struct bgp_master` 的字段，[bgpd.h:190](bgpd/bgpd.h#L190)）键为 MAC、值为 `struct bgp_self_mac`（[bgp_mac.c:62](bgpd/bgp_mac.c#L62)，含 MAC 与拥有它的接口名列表）。

| 接口 | 定义 | 作用 |
|------|------|------|
| `bgp_mac_init` | [bgp_mac.c:94](bgpd/bgp_mac.c#L94) | 建表（[bgp_mac.c:96](bgpd/bgp_mac.c#L96) `hash_create`） |
| `bgp_mac_finish` | [bgp_mac.c:118](bgpd/bgp_mac.c#L118) | 销毁表 |
| `bgp_mac_add_mac_entry` | [bgp_mac.c:394](bgpd/bgp_mac.c#L394) | 接口 MAC 新增（来自接口管理） |
| `bgp_mac_del_mac_entry` | [bgp_mac.c:448](bgpd/bgp_mac.c#L448) | 接口 MAC 删除 |
| `bgp_mac_exist` | [bgp_mac.c:475](bgpd/bgp_mac.c#L475) | 某 MAC 是否为本机所有 |
| `bgp_mac_entry_exists` | [bgp_mac.c:501](bgpd/bgp_mac.c#L501) | 由 EVPN 前缀（取 `macip_addr.mac`）判存在性 |
| `bgp_mac_dump_table` | [bgp_mac.c:539](bgpd/bgp_mac.c#L539) | show 用 |

**关系**：来自对端的 Type-2 若 MAC 属于本机（或 router-mac 指向本机），就需重新处理/通告——`bgp_mac_entry_exists` 正是 EVPN 路由处理流程用来快速判定的查询接口。当本机 MAC 变化（学到新 MAC、迁移、接口消失）时会触发对全部 EVPN 表的重新扫描并重发受影响路由。

> **注意设计取舍**：MAC 不是 bgpd 嗅探的，而是 **zebra 从内核 FDB 同步过来**的（[bgp_mac.h](bgpd/bgp_mac.h) 导读）。所以 `bgp_mac.c` 是「本端视角的 MAC 存在性索引」，具体到某条 EVPN 路由的 NLRI 组装/替换/去重在 `bgp_evpn.c`。两者配合完成「本端口 MAC → 通告出去」「对端 MAC → 装表」。

---

## §4 EVPN 多归属（MH）与 DF 选举

### 4.1 数据结构（`bgp_evpn_mh.h`）

`bgp_evpn_mh.h` 是 EVPN-MH 的骨架定义。自底向上：

| 结构 | 定义 | 代表什么 |
|------|------|---------|
| `struct bgp_evpn_es_frag` | [bgp_evpn_mh.h:103](bgpd/bgp_evpn_mh.h#L103) | **ES 分片**：一组共用同一 RD 的 ES-EVI 集合（一个 ES 的 EVI 按 `BGP_EVPN_MAX_EVI_PER_ES_FRAG` 切分，每片一个 RD，产生一条 EAD-per-ES） |
| `struct bgp_evpn_es` | [bgp_evpn_mh.h:169](bgpd/bgp_evpn_mh.h#L169) | **核心对象**：一个 ES。既可能本地（zebra 配）、也可能远端（被远端 ES-EVI 引用而隐式创建），甚至两者并存。ES 级 Type-1/Type-4 存在它的 `route_table` 里 |
| `struct bgp_evpn_es_vtep` | [bgp_evpn_mh.h:277](bgpd/bgp_evpn_mh.h#L277) | 挂在 ES 上的对端 PE（VTEP）；DF 选举信息 `df_alg`/`df_pref` 随 ESR 学来存这里 |
| `struct bgp_evpn_es_vrf` | [bgp_evpn_mh.h:318](bgpd/bgp_evpn_mh.h#L318) | ES-per-VRF，为对称 IRB 的 L3 NHG 服务 |
| `struct bgp_evpn_es_evi` | [bgp_evpn_mh.h:371](bgpd/bgp_evpn_mh.h#L371) | **ES-per-EVI**：某 ES 在某 L2-VNI 上的视角 |
| `struct bgp_evpn_es_evi_vtep` | [bgp_evpn_mh.h:422](bgpd/bgp_evpn_mh.h#L422) | 更细一层：某 PE 在该 EVI 上是否发了 EAD-per-ES / EAD-per-EVI |
| `struct bgp_evpn_nh` | [bgp_evpn_mh.h:457](bgpd/bgp_evpn_mh.h#L457) | 由 Type-2 导入 VRF 时按下一跳（VTEP）建立的下一跳对象 |
| `struct bgp_evpn_mh_info` | [bgp_evpn_mh.h:497](bgpd/bgp_evpn_mh.h#L497) | MH 子系统的「总控块」，存 `bm->mh_info`，宏 `bgp_mh_info` 访问（[bgp_evpn_mh.h:496](bgpd/bgp_evpn_mh.h#L496)） |

BGPD 全局用一棵 RB 树 `es_rb_tree`（键 = ESI）索引所有 ES。**本地/远端两套标志位分别记在 `es->flags`（`BGP_EVPNES_` 前缀）与 `attr->es_flags`（`ATTR_ES_` 前缀）上，含义不同**（[bgp_evpn_mh.h](bgpd/bgp_evpn_mh.h) 导读）。`zero_esi`（全 0）表示无效/不属于任何 ES。

### 4.2 本地 ES 生命周期

- `bgp_evpn_local_es_add`（[bgp_evpn_mh.c:2814](bgpd/bgp_evpn_mh.c#L2814)）：zebra 通知「设备与 ES 关联」。找不到则 `bgp_evpn_es_new`（[bgp_evpn_mh.c:2281](bgpd/bgp_evpn_mh.c#L2281)）新建；记录/更新 originator IP、oper_up、`df_pref`、bypass（`df_pref` 变化需重生成 ESR）。
- `bgp_evpn_local_es_del`（[bgp_evpn_mh.c:2791](bgpd/bgp_evpn_mh.c#L2791)）：zebra 通知删除本地 ES，找不到则告警。
- 释放族：`bgp_evpn_es_free`（[bgp_evpn_mh.c:2332](bgpd/bgp_evpn_mh.c#L2332)）、`bgp_evpn_es_frag_free`（[bgp_evpn_mh.c:2084](bgpd/bgp_evpn_mh.c#L2084)）、`bgp_evpn_es_frag_free_all`（[bgp_evpn_mh.c:2116](bgpd/bgp_evpn_mh.c#L2116)）。

本地 ES 的改动会连锁触发路由重生成：OS 类型见 [bgp_evpn_mh.c:2615](bgpd/bgp_evpn_mh.c#L2615)（删 Type-4 与 EAD-per-ES）与 [bgp_evpn_mh.c:2664](bgpd/bgp_evpn_mh.c#L2664)（建 Type-4 与 EAD-per-ES）。

### 4.3 Type-4（ESR）生成与 DF 扩展团体

- `bgp_evpn_type4_route_update`（[bgp_evpn_mh.c:861](bgpd/bgp_evpn_mh.c#L861)）：创建/更新本地 Type-4（ESR），**DF 选举参数在此随扩展团体发出**。
- DF Election 扩展团体由 [bgp_evpn_mh.c:847](bgpd/bgp_evpn_mh.c#L847) 的 `encode_df_elect_extcomm(&eval_df, es->df_pref)` 编码。
- `bgp_evpn_type4_route_delete`（[bgp_evpn_mh.c:933](bgpd/bgp_evpn_mh.c#L933)）：无 `es_base_frag` 时删除。

### 4.4 Type-1 EAD：per-ES 与 per-EVI

- `bgp_evpn_ead_es_route_update`（[bgp_evpn_mh.c:1323](bgpd/bgp_evpn_mh.c#L1323)）：为 ES 的每个分片生成/刷新 **EAD-per-ES** 路由（含 ESI Label，用于水平分割）。
- `bgp_evpn_ead_evi_route_update`（[bgp_evpn_mh.c:1347](bgpd/bgp_evpn_mh.c#L1347)）：生成/刷新一条 **EAD-per-EVI** 路由。
- 删除侧：`bgp_evpn_ead_es_route_delete`（[bgp_evpn_mh.c:1390](bgpd/bgp_evpn_mh.c#L1390)）、`bgp_evpn_ead_evi_route_delete`（[bgp_evpn_mh.c:1407](bgpd/bgp_evpn_mh.c#L1407)）。
- `delete_global_ead_evi_routes`（[bgp_evpn_mh.c:756](bgpd/bgp_evpn_mh.c#L756)）：VNI 的 RD 变化时删除该 VNI 在全局表中的 EAD-per-EVI。

### 4.5 远端 Type-1/Type-4 处理

- `bgp_evpn_type4_route_process`（[bgp_evpn_mh.c:948](bgpd/bgp_evpn_mh.c#L948)）：处理收到的远端 Type-4。
- `bgp_evpn_type1_route_process`（[bgp_evpn_mh.c:1482](bgpd/bgp_evpn_mh.c#L1482)）：处理收到的远端 Type-1。
- 远端 DF 参数落点 `bgp_evpn_es_vtep_add`（[bgp_evpn_mh.c:1790](bgpd/bgp_evpn_mh.c#L1790)）：`esr=true` 表示因收到 ESR 而添加——置 ESR 位并更新 `df_alg`/`df_pref`（参数变化时置位）。
- ES 路由安装/卸载：`bgp_evpn_es_route_install`（[bgp_evpn_mh.c:347](bgpd/bgp_evpn_mh.c#L347)）、`bgp_evpn_es_route_uninstall`（[bgp_evpn_mh.c:420](bgpd/bgp_evpn_mh.c#L420)），统一入口 `bgp_evpn_es_route_install_uninstall`（[bgp_evpn_mh.c:464](bgpd/bgp_evpn_mh.c#L464)）。

### 4.6 DF 选举到底在哪 —— bgpd 只做插桩

这是本卷最容易被误导的一点，必须讲清：**DF 选举算法本身运行在 zebra，不在 `bgp_evpn_mh.c`**。

- 真正的选举函数是 [zebra/zebra_evpn_mh.c:1756](zebra/zebra_evpn_mh.c#L1756) `zebra_evpn_es_run_df_election`。其判据（[zebra/zebra_evpn_mh.c:1800-1819](zebra/zebra_evpn_mh.c#L1800)）：
  - 只有「已通告 ESR」的 VTEP 参与选举；
  - 若对端 `df_alg != EVPN_MH_DF_ALG_PREF`，退化为停止转发 BUM（不支持 service-carving）；
  - 对端**偏好值更高者胜**；偏好相同则 **IP 更低者胜**；
  - 结果经 `zebra_evpn_es_df_change`（[zebra/zebra_evpn_mh.c:1730](zebra/zebra_evpn_mh.c#L1730)）落到数据面（non-DF 阻塞过滤器）。
- bgpd 侧只做三件事：
  1. **接收**本地 ES 的 `df_pref`（从 zebra 经 `ZEBRA_LOCAL_ES_ADD`，处理在 [bgp_zebra.c:3748](bgpd/bgp_zebra.c#L3748) 起），存入 `es->df_pref`（[bgp_evpn_mh.c:2835](bgpd/bgp_evpn_mh.c#L2835)）；
  2. **发出**：把 `es->df_pref` 编进 DF Election 扩展团体，随 ESR（Type-4）通告（[bgp_evpn_mh.c:847](bgpd/bgp_evpn_mh.c#L847)）；
  3. **学习**对端的 `df_alg`/`df_pref`（[bgp_evpn_mh.c:1790](bgpd/bgp_evpn_mh.c#L1790)）。

> 所以问「DF 选举算法在哪」的标准答案：**在 zebra 的 `zebra_evpn_es_run_df_election`**；bgpd 的 `bgp_evpn_mh.c` 只提供控制面插桩（ES 对象、ESR、DF 偏好值传播）。本仓库并未实现 RFC7432 的 service-carving，用的是 draft-ietf-bess-evpn-pref-df 的偏好值选举（[bgp_evpn_mh.c:23](bgpd/bgp_evpn_mh.c#L23)）。

### 4.7 ES-EVI、L3 NHG 与一致性检查

- ES 与 L2-VNI 的关联：`bgp_evpn_es_evi_vrf_ref`（[bgp_evpn_mh.c:3674](bgpd/bgp_evpn_mh.c#L3674)）/ `_vrf_deref`（[bgp_evpn_mh.c:3662](bgpd/bgp_evpn_mh.c#L3662)）。
- 非 DF 阻塞与 L3 NHG 通过 [bgp_evpn_mh.c:1688](bgpd/bgp_evpn_mh.c#L1688) 的 `bgp_evpn_es_vtep` 相关编程完成（附带标志「是否收到 ESR」与 DF 参数）。
- **一致性检查是异步的**，靠 `pend_es_list` 与定时器驱动：入口 `bgp_evpn_run_consistency_checks`（[bgp_evpn_mh.c:193](bgpd/bgp_evpn_mh.c#L193) 前置声明）。
- 初始化：`bgp_evpn_mh_init`（[bgp_evpn_mh.c:5760](bgpd/bgp_evpn_mh.c#L5760)）。

---

## §5 `bgp_evpn_vty.c` 与 `bgp_evpn.c` 的分工

`bgp_evpn_vty.c`（8584 行）是 EVPN 的**命令行前端**，不含 EVPN 核心协议逻辑；`bgp_evpn.c`（10546 行）是核心。二者分工明确：

| 维度 | `bgp_evpn.c` | `bgp_evpn_vty.c` |
|------|-------------|-----------------|
| 职责 | 表结构、NLRI、导入导出、本地事件、下发 zebra | show 输出、配置命令、配置回写 |
| 是否编码报文 | 是（`bgp_evpn_encode_prefix`） | 否 |
| 是否改协议状态 | 是（建/删路由、改 VNI 状态） | 通常只「读表 + 格式化」「改配置 + 触发 apply」 |
| 关键入口 | `bgp_evpn_init`（[bgp_evpn.c:9569](bgpd/bgp_evpn.c#L9569)） | `bgp_ethernetvpn_init`（[bgp_evpn_vty.c:8448](bgpd/bgp_evpn_vty.c#L8448)） |

**`bgp_evpn_vty.c` 的三块内容**（[bgp_evpn_vty.c](bgpd/bgp_evpn_vty.c) 导读）：

1. **`show` 命令族**：`show bgp l2vpn evpn ...`（route / vni / es / next-hop / all / summary 等）。核心遍历函数是 `evpn_show_all_routes`（[bgp_evpn_vty.c:3519](bgpd/bgp_evpn_vty.c#L3519)），表头由 `bgp_evpn_show_route_header`（[bgp_evpn_vty.c:431](bgpd/bgp_evpn_vty.c#L431)）打印。它遍历 `bgp_evpn.c` 维护的全局表 / VNI 表 / import-rt 表与 `bgp_evpn_mh.c` 的 ES 表，**把表格式化成文本或 JSON**。
2. **配置命令**：`advertise-all-vni`、`vni <n>` 下的 `flood`/`rd`/`route-target`/`import`/`export`/`advertise-svi-ip` 等，改 EVPN 配置结构并触发 apply/重建。
3. **配置回写**：`bgp_config_write_evpn_info`（[bgp_evpn_vty.c:8199](bgpd/bgp_evpn_vty.c#L8199)），把 EVPN 配置写进 running-config，由 `bgp_vty.c` 的 `bgp_config_write` 类入口调用。

**参数解析复用**：`argv_find_and_parse_oly_idx`（[bgp_evpn_vty.c:118](bgpd/bgp_evpn_vty.c#L118)）、`bgp_evpn_cli_parse_type`（[bgp_evpn_vty.c:5518](bgpd/bgp_evpn_vty.c#L5518)）、`bgp_evpn_cli_parse_type_cmp`（[bgp_evpn_vty.c:5497](bgpd/bgp_evpn_vty.c#L5497)）被其他模块（如 EVPN MH、`bgp_vty.c`）复用，避免各写一套解析逻辑。`bgp_evpn_vty.h`（91 行）只声明这几个入口 + 一批 HELP_STR 常量。

**两个易错点**（[bgp_evpn_vty.c](bgpd/bgp_evpn_vty.c) 导读）：
- show 的文本输出与 JSON 输出是**两套并行代码**（同一函数里 `if (json) {...} else {...}`），改一处别忘另一处；
- 配置命令往往「先校验（VNI 是否存在、是否与 L3 VNI 冲突）→ 改结构 → 按需 apply」，注释会标注「改了哪个字段 / 是否触发重建」。

> 本卷不逐条罗列命令（101 个 DEFUN/DEFPY），只讲清分工。

---

## §6 MPLS L3VPN（VPNv4 / VPNv6, RFC 4364）

### 6.1 NLRI 结构与解析 `bgp_nlri_parse_vpn`

VPN NLRI 的线格式是 `长度 | 标签栈 | RD | 前缀`（[bgp_mplsvpn.c](bgpd/bgp_mplsvpn.c) 导读）。解析入口 `bgp_nlri_parse_vpn`（[bgp_mplsvpn.c:135](bgpd/bgp_mplsvpn.c#L135)）：

- 固定约定：**除 IP 前缀外固定含 3 字节标签 + 8 字节 RD**，故「前缀长度」字段最小值 = `(3+8)*8 = 88 bit`（宏 `VPN_PREFIXLEN_MIN_BYTES` 在 [bgp_mplsvpn.c:171](bgpd/bgp_mplsvpn.c#L171)）；
- 循环读 `prefixlen`，用 `bgp_nlri_get_labels` 解标签栈，`decode_rd_type` + `decode_rd_*` 解 RD（RD 容器 `struct prefix_rd`，`prefixlen` 固定 64）。
- 结果（前缀, RD, 标签）交给 `bgp_update` / `bgp_withdraw`。

`argv_find_and_parse_vpnvx`（[bgp_mplsvpn.c:105](bgpd/bgp_mplsvpn.c#L105)）解析命令行 `vpnv4`/`vpnv6` 关键字。

### 6.2 `struct vpn_policy` 与方向常量

导入导出策略是**每 AF 一份**的 `struct vpn_policy`（定义在 [bgpd.h:342](bgpd/bgpd.h#L342)，字段见 [bgpd.h:342-352](bgpd/bgpd.h#L342)）：

| 字段 | 语义 |
|------|------|
| `rtlist[BGP_VPN_POLICY_DIR_MAX]` | 按方向的 RT 集合（`struct ecommunity *`） |
| `rmap_name` / `rmap` | 按方向的 route-map 名与句柄 |
| `tovpn_rd` | 导出到 VPN 时使用的 RD |

方向常量 [bgpd.h:313-315](bgpd/bgpd.h#L313)：

```c
BGP_VPN_POLICY_DIR_FROMVPN = 0,   /* 导入方向：VPN → VRF */
BGP_VPN_POLICY_DIR_TOVPN   = 1,   /* 导出方向：VRF → VPN */
BGP_VPN_POLICY_DIR_MAX     = 2
```

**读任何 leak 代码前先分清方向**：`DIR_TOVPN` 用 `rtlist[TOVPN]` 写 RT 到导出的路由上；`DIR_FROMVPN` 用 `rtlist[FROMVPN]` 做 `ecommunity_include` 交集判定。

### 6.3 VRF→VPN 导出：`vpn_leak_from_vrf_update`

定义在 [bgp_mplsvpn.c:2108](bgpd/bgp_mplsvpn.c#L2108)。流程（顺序重要，见 [bgp_mplsvpn.c:2096](bgpd/bgp_mplsvpn.c#L2096) 注释）：

1. 取 afi，检查是否可导出（`is_route_injectable_into_vpn`）、VPN 导出是否激活；
2. 聚合抑制检查；shallow copy 属性；合并/剥离 RT（VRF-2-VRF 时先剥 RT）；
3. 跑 `tovpn` route-map（可能 DENY）；校验必须含 RT；
4. 下一跳策略；取出口标签（per-nexthop 或 per-VRF；`MPLS_LABEL_NONE` 时用 implicit-null）；
5. 置 originator-id 为自身；分别按 MPLS/SRv6 调用 `_vpn_leak_from_vrf_update_leak_attr`（[bgp_mplsvpn.c:2064](bgpd/bgp_mplsvpn.c#L2064)）落库。

**落库核心是 `leak_update`**（[bgp_mplsvpn.c:1414](bgpd/bgp_mplsvpn.c#L1414)，返回新 `bgp_path_info *`）：它把源路径泄漏到目标表，并维护 `bgp_path_info->extra->vrfleak` 的 `parent` 指针（把「VPN 表中的导入项」与「源 VRF 中的原始项」关联）。

- 撤回：`vpn_leak_from_vrf_withdraw`（[bgp_mplsvpn.c:2375](bgpd/bgp_mplsvpn.c#L2375)）、批量 `_all`（[bgp_mplsvpn.c:2454](bgpd/bgp_mplsvpn.c#L2454)）、重导出 `_update_all`（[bgp_mplsvpn.c:2517](bgpd/bgp_mplsvpn.c#L2517)）。
- **per-nexthop 标签是异步的**：申请中（`MPLS_INVALID_LABEL`）时不导出，等回调再次触发（[bgp_mplsvpn.c](bgpd/bgp_mplsvpn.c) 导读）。

### 6.4 VPN→VRF 导入：`vpn_leak_to_vrf_update_onevrf`

定义在 [bgp_mplsvpn.c:2594](bgpd/bgp_mplsvpn.c#L2594)。这是「按 RT 决定该 VPN 路由能否进入某 VRF」的核心：

1. **RT 交集判定**（[bgp_mplsvpn.c:2650](bgpd/bgp_mplsvpn.c#L2650)）：

```c
if (!ecommunity_include(
            to_bgp->vpn_policy[afi].rtlist[BGP_VPN_POLICY_DIR_FROMVPN],
            bgp_attr_get_ecommunity(path_vpn->attr)))
        return;   /* 无交集直接跳过 */
```

2. **不接受回来的自身路由**（[bgp_mplsvpn.c:2668](bgpd/bgp_mplsvpn.c#L2668)）：若路径带 `BGP_PATH_ACCEPT_OWN` 且 RD 与目标 VRF 的 `tovpn_rd` 相同，跳过。
3. **AS 环检查**（[bgp_mplsvpn.c:2688](bgpd/bgp_mplsvpn.c#L2688)）：`aspath_loop_check`，按 `allowas-in` 容忍计数。
4. **MPLS/SRv6 互不覆盖**（[bgp_mplsvpn.c:2694-2706](bgpd/bgp_mplsvpn.c#L2694)）：SRv6 路径不能覆盖 MPLS 路径，反之亦然（示例见该循环里的 `continue`）。
5. VRF-2-VRF 泄漏时**剥离 RT**（[bgp_mplsvpn.c:2727](bgpd/bgp_mplsvpn.c#L2727)）；最终 `leak_update` 落库。

外部入口是 `vpn_leak_to_vrf_update`（[bgp_mplsvpn.c:2987](bgpd/bgp_mplsvpn.c#L2987)，遍历所有 VRF 逐个调用 `_onevrf`）。撤回与全量：`vpn_leak_to_vrf_withdraw`（[bgp_mplsvpn.c:3011](bgpd/bgp_mplsvpn.c#L3011)）、`_all`（[bgp_mplsvpn.c:3098](bgpd/bgp_mplsvpn.c#L3098)）、`_update_all`（[bgp_mplsvpn.c:3181](bgpd/bgp_mplsvpn.c#L3181)）。

初始化 / CLI：`bgp_mplsvpn_init`（[bgp_mplsvpn.c:4337](bgpd/bgp_mplsvpn.c#L4337)）注册 network/show 命令；`bgp_show_mpls_vpn` 是 `show bgp vpn` 的公共实现。

### 6.5 `bgp_vpn.c` 与 RD/RT 编解码的实际归属

**先纠正一个常见误解**：`bgp_vpn.c` **不是** RD/RT 的编解码器，它只有**一个函数** `show_adj_route_vpn`（[bgp_vpn.c:56](bgpd/bgp_vpn.c#L56)），负责 `show bgp <afi> <safi> neighbors X advertised-routes/routes` 的 **VPN 版输出**。

为何要单独一个 VPN 版：VPN 路由是**两层结构**（外层 RD 表套内层前缀表），普通 IPv4/IPv6 表的 `show_adj_route()` 无法复用。内层遍历用 `bgp_dest_get_bgp_table_info` 取子表（[bgp_vpn.c:94](bgpd/bgp_vpn.c#L94)），在 `adj_out` 红黑树里用 `SUBGRP_FOREACH_PEER` 找目标 peer 的 attr（[bgp_vpn.c:115](bgpd/bgp_vpn.c#L115)），再交 `route_vty_out_tmp` 打印。

**它确实用到了 RD 编解码，但只是「为了显示」**——按 RD 类型解出字符串（[bgp_vpn.c:170](bgpd/bgp_vpn.c#L170) 起）：

```c
type = decode_rd_type(pnt);                 /* pnt = 外层前缀前 8 字节 */
if (type == RD_TYPE_AS)       decode_rd_as(pnt + 2, &rd_as);
else if (type == RD_TYPE_AS4) decode_rd_as4(pnt + 2, &rd_as);
else if (type == RD_TYPE_IP)  decode_rd_ip(pnt + 2, &rd_ip);
```

- 注意 `pnt + 2`：**RD 前 8 字节里，前 2 字节是类型，后 6 字节是值**，所以解码值要跳过类型。
- 这些 `decode_rd_*` 与 `RD_TYPE_*` 宏**定义在 `bgp_rd.h`（不在本卷）**；RD 的**编码**（`encode_rd_*`）同样在 `bgp_rd.c`。RT 的编解码在 `bgp_ecommunity.c`（也不在本卷）。

> 所以本卷问题 6 里说的「`bgp_vpn.c` 的 RD/RT 编解码」应理解为「`bgp_vpn.c` 调用 RD 解码用于展示」；真正的 RD/RT 编解码在 `bgp_rd.c` / `bgp_ecommunity.c`，属属性篇。

### 6.6 `bgp_mplsvpn_snmp.c` / `bgp_mplsvpn_snmp.h` 与 `bgp_vnc_types.h`

- **`bgp_mplsvpn_snmp.c`（1810 行）**：MPLS/BGP L3VPN MIB（RFC4382，OID `1.3.6.1.2.1.10.166.11`，[bgp_mplsvpn_snmp.c:65](bgpd/bgp_mplsvpn_snmp.c#L65)）的取值实现，与前两个 BGP MIB 文件同构：回调签名一致，先 `smux_header_generic` 再填 `struct variable`。**表大多以「VRF 名字符串」为索引**。初始化 hook 在文件末尾 `bgp_mpls_l3vpn_module_init`（声明见 [bgp_mplsvpn_snmp.h](bgpd/bgp_mplsvpn_snmp.h)）。编译开关 `HAVE_SNMP`。
- **`bgp_mplsvpn_snmp.h`（44 行）**：一个初始化入口 + 两组**MIB 契约常量**。注意导读的强调：
  - `MPLSL3VPNVRFRTECIDRTYPE*`（OTHER=1/REJECT=2/LOCAL=3/REMOTE=4/BLACKHOLE=5）描述条目来源；
  - `MPLSVPNVRFRTTYPE*`（IMPORT=1/EXPORT=2/BOTH=3）中 **BOTH=3 是独立取值**，不是 `IMPORT|EXPORT` 的位或结果（数值巧合相同）。
  - **改动这些数值等于改协议**。
- **`bgp_vnc_types.h`（40 行）**：只有 VNC（LabN 的虚拟网络编排扩展，非 IETF 标准）的 Sub-TLV 类型号 `LIFETIME=1`、`RFPOPTION=2`（已废弃），整体被 `#ifdef ENABLE_BGP_VNC` 包住。**默认编译不含这些符号**。

---

## §7 MPLS 标签：`bgp_label.c` 与 `bgp_labelpool.c`

### 7.1 边界：一句话分清

> **`bgp_label.c` 管「BGP 报文里怎么带标签、以及本机标签与 FEC 的绑定/注册」；`bgp_labelpool.c` 管「本机出标签从哪个 chunk 里切」。**

| 维度 | `bgp_label.c`（844 行） | `bgp_labelpool.c`（1994 行） |
|------|------------------------|------------------------------|
| 解决的问题 | 标签的**编解码**、**intern 共享**、**向 zebra 注册 FEC**、收包解标签栈 | 向 zebra 申请 **label chunk** 并在本地**按需分配** |
| 面向的 SAFI | BGP-LU、MPLS-VPN、EVPN（都随前缀带标签） | 为 L3VPN 出标签、BGP-LU、每下一跳共享标签服务 |
| 与 zebra 交互 | `ZEBRA_FEC_REGISTER`（FEC 注册/注销）、`bgp_parse_fec_update` | `bgp_zebra_request_label_range`（申请 chunk）、`bgp_lp_event_chunk`（收 chunk） |
| 关键状态 | `bgp_labels` 共享哈希 | `labelpool`（ledger/inuse/chunks） |
| 上层调用 | `bgp_reg_dereg_for_label` 被路由处理调用 | `bgp_lp_get` 被 `bgp_reg_for_label_callback` 等调用 |

**两者的接点**：当标签由本机标签池分配时，`bgp_reg_for_label_callback`（[bgp_label.c:398](bgpd/bgp_label.c#L398)）在标签分配完成后被 `bgp_labelpool` 回调——即「池子」把标签给「标签管理」，后者再去向 zebra 注册 FEC。

### 7.2 `bgp_label.c`：编解码、intern、FEC 注册

**① 线上编码（所有位操作的基础）**：单个 MPLS 标签在报文里是 **3 字节（24 bit）= [20 bit Label | 3 bit TC | 1 bit BOS]**，见 [bgp_label.h](bgpd/bgp_label.h) 导读。FRR **借用 TC 区的 `0x02` 位**当「本端已确认标签有效」的内部标记：

| inline | 定义 | 作用 |
|--------|------|------|
| `bgp_is_valid_label` | [bgp_label.h:188](bgpd/bgp_label.h#L188) | 判 `0x02` 位（内部标记，非协议位） |
| `bgp_set_valid_label` | [bgp_label.h:200](bgpd/bgp_label.h#L200) | 置该位 |
| `bgp_is_withdraw_label` | [bgp_label.h:173](bgpd/bgp_label.h#L173) | **同时接受 `t[2]==0x00` 与 `0x02`**（因为收到的报文可能已带该位） |

`BGP_MAX_LABELS=10`（[bgp_label.h:64](bgpd/bgp_label.h#L64)）的来历：NLRI 前缀长度字段 1 字节且单位是 bit，`floor(256/24)=10`。

**② intern 共享**（与 `bgp_attr` 同构）：`bgp_labels_intern`（[bgp_label.c:135](bgpd/bgp_label.c#L135)）、`bgp_labels_unintern`（[bgp_label.c:155](bgpd/bgp_label.c#L155)）、`bgp_labels_cmp`（[bgp_label.c:178](bgpd/bgp_label.c#L178)）。内容相同的标签栈全局只留一份，靠 `refcnt` 复用。

**③ 出标签的申请/注册链路**：

```
bgp_reg_dereg_for_label()          bgp_label.c:456   ← 路由需注册 FEC/要标签
   ├─ 由 zebra 标签管理器按 FEC 分配 ──► bgp_parse_fec_update()  bgp_label.c:212
   └─ 由本机 labelpool 分配（异步）────► [bgp_lp_get] → bgp_reg_for_label_callback()  bgp_label.c:398
                                            └─ bgp_send_fec_register_label_msg()  bgp_label.c:319
```

- `bgp_adv_label`（[bgp_label.c:274](bgpd/bgp_label.c#L274)）：决定「向对端 to 通告时，这条路由携带哪个标签」。
- SR 场景还可用 attr 的 `label_index` 让 zebra 推导标签。

**④ 收包解析**：`bgp_nlri_get_labels`（[bgp_label.c:536](bgpd/bgp_label.c#L536)，按 3 字节一步解出标签栈）、`bgp_nlri_parse_label`（[bgp_label.c:599](bgpd/bgp_label.c#L599)，BGP-LU/MPLS-VPN 带标签 NLRI 的收包入口）、`decode_label`（[bgp_label.c:748](bgpd/bgp_label.c#L748)）、`encode_label`（[bgp_label.c:779](bgpd/bgp_label.c#L779)）、`encode_label_bos`（[bgp_label.c:763](bgpd/bgp_label.c#L763)）、`mpls_labels2str`（[bgp_label.c:819](bgpd/bgp_label.c#L819)）。

### 7.3 `bgp_labelpool.c`：chunk 异步分配状态机

**三层数据结构**（[bgp_labelpool.h](bgpd/bgp_labelpool.h) 导读）：

| 结构/表 | 键 | 值 | 作用 |
|---------|----|----|------|
| `ledger`（跳表） | `labelid`（请求者唯一标识） | `struct lp_lcb` | 已受理的请求账本（即使标签未分配也先登记） |
| `inuse`（跳表） | 标签值 | `labelid` | 当前已分配出去的标签，用于反查/释放校验 |
| `chunks`（链表） | —— | `struct lp_chunk` | zebra 授予的连续标签段，每段带 `allocated_map` 位图与 `nfree` |

模块总状态是 `struct labelpool`（[bgp_labelpool.h:70](bgpd/bgp_labelpool.h#L70)），全局单例 `static lp`（[bgp_labelpool.c:92](bgpd/bgp_labelpool.c#L92)）。初始化 `bgp_lp_init`（[bgp_labelpool.c:260](bgpd/bgp_labelpool.c#L260)）。

**分配主入口 `bgp_lp_get`**（[bgp_labelpool.c:476](bgpd/bgp_labelpool.c#L476)）：
- 先查 `ledger`（重复请求则复用），否则 `lcb_alloc` 新建（[bgp_labelpool.c:422](bgpd/bgp_labelpool.c#L422)，其中立刻尝试 `get_label_from_pool`，[bgp_labelpool.c:357](bgpd/bgp_labelpool.c#L357)）；
- **快路径**：`lcb->label != MPLS_LABEL_NONE` → 把回调投到 `callback_q` 工作队列（不直接调用，避免重入）；
- **慢路径**：本地池没货 → 把请求挂到 `requests` FIFO，按需 `bgp_zebra_request_label_range` 申请 chunk（块大小翻倍、封顶），挂定时器 1 秒后 `bgp_sync_label_manager`。

**状态机**：
- `bgp_sync_label_manager`（[bgp_labelpool.c:698](bgpd/bgp_labelpool.c#L698)）：定时器驱动，把 FIFO 里的请求尽量用本地池满足。
- `bgp_lp_event_chunk`（[bgp_labelpool.c:781](bgpd/bgp_labelpool.c#L781)）：zebra 授予一段 `[first,last]`，做成 `lp_chunk`。
- `bgp_lp_event_zebra_down`（[bgp_labelpool.c:816](bgpd/bgp_labelpool.c#L816)）：**空实现**——断连时允许继续用旧标签。
- `bgp_lp_event_zebra_up`（[bgp_labelpool.c:835](bgpd/bgp_labelpool.c#L835)）：重连时把旧标签全部作废，通过回调（`allocated=false`）通知请求者并重新分配，**请求者无需自己重发申请**。

**释放与类型**：`bgp_lp_release`（[bgp_labelpool.c:581](bgpd/bgp_labelpool.c#L581)）带多重校验（labelid / type 匹配）防止误释放。四个类型常量（[bgp_labelpool.h](bgpd/bgp_labelpool.h)）：`LP_TYPE_VRF=1`、`LP_TYPE_BGP_LU=2`、`LP_TYPE_NEXTHOP=3`、`LP_TYPE_BGP_L3VPN_BIND=4`。对应释放包装：

| 包装 | 定义 | 释放后动作 |
|------|------|-----------|
| `bgp_lu_lp_release` | [bgp_labelpool.c:658](bgpd/bgp_labelpool.c#L658) | 清 dest 的 local_label 有效位 |
| `bgp_vpn_lp_release` | [bgp_labelpool.c:676](bgpd/bgp_labelpool.c#L676) | 把 `policy->tovpn_label` 置 NONE |
| `bgp_vpn_nh_lp_release` | [bgp_labelpool.c:685](bgpd/bgp_labelpool.c#L685) | L3VPN 每下一跳绑定标签 |

**每下一跳共享标签**：`struct bgp_label_per_nexthop_cache`（[bgp_labelpool.h:134](bgpd/bgp_labelpool.h#L134)）是以下一跳为键的 RB-tree，用于 L3VPN 出标签按下一跳聚合，多条路径共享同一标签。

### 7.4 一句话协作链

```
L3VPN/BGP-LU 需要出标签
        │
   bgp_lp_get(type, labelid, …)                bgp_labelpool.c:476
        │   本地池有空 → 快路径回调；否则申请 chunk
        ▼
   bgp_reg_for_label_callback(new_label, …)     bgp_label.c:398
        │   绑定标签到 dest / path
        ▼
   bgp_send_fec_register_label_msg(dest, reg)   bgp_label.c:319
        │   ZEBRA_FEC_REGISTER
        ▼
      zebra 标签管理器 / LDE
```

---

## §8 SRv6（`bgp_srv6.c` / `bgp_attr_srv6.h`）

### 8.1 本端 SID：`bgp_srv6.c`

`bgp_srv6.c`（424 行）负责「本端 IPv6 单播前缀的 SRv6 Prefix-SID 分配与通告」，与 `bgp_attr_srv6.h`（管「收到对端 SID 怎么存」）正好相对（[bgp_srv6.c](bgpd/bgp_srv6.c) 导读）。

**三个 SID 来源**（由 `is_srv6_unicast_enabled` 的三判据体现，[bgp_srv6.h](bgpd/bgp_srv6.h)）：`auto`（从 locator 自动切分）、`index`（只给 `sid_index`，用 `srv6_sid_compose` 推导）、`explicit`（直接给完整 SID）。`ensure_afi_sid` 会检查三种模式不能同时选。

**两层「安装」**：
- **数据面**：SID 在内核注册为 local SID endpoint（End.DT4/6），由 `bgp_srv6_unicast_sid_endpoint`（[bgp_srv6.c:132](bgpd/bgp_srv6.c#L132)）经 `zclient_send_localsid` 完成；
- **通告面**：SID 作为 Prefix-SID 属性随路由通告，由 `bgp_srv6_unicast_register_route`（[bgp_srv6.c:289](bgpd/bgp_srv6.c#L289)）把 SID 挂到 `bgp_dest->srv6_unicast` 上，`announce`（[bgp_srv6.c:367](bgpd/bgp_srv6.c#L367)）再触发重发。

**与 route-map 交互**：`register_route` 在给前缀挂 SID 前会应用 `srv6_unicast[afi].rmap_name` 指定的 route-map；被 deny 的前缀不挂 SID。

函数清单：

| 函数 | 定义 | 作用 |
|------|------|------|
| `ensure_afi_sid` | [bgp_srv6.c:65](bgpd/bgp_srv6.c#L65) | 确保该 AFI 的 SID 已就绪 |
| `sid_endpoint` | [bgp_srv6.c:132](bgpd/bgp_srv6.c#L132) | 数据面装/卸（内核 local SID） |
| `sid_withdraw` | [bgp_srv6.c:174](bgpd/bgp_srv6.c#L174) | 撤销该 AFI 下所有 SID 通告 |
| `delete` | [bgp_srv6.c:207](bgpd/bgp_srv6.c#L207) | 删除该 AFI 全部 SID 状态 |
| `sid_update` | [bgp_srv6.c:249](bgpd/bgp_srv6.c#L249) | SID 内容变化，需重绑并重发 |
| `unregister_route` | [bgp_srv6.c:270](bgpd/bgp_srv6.c#L270) | 路由撤销时归还 SID |
| `register_route` | [bgp_srv6.c:289](bgpd/bgp_srv6.c#L289) | 某前缀需 SID 的入口 |
| `announce` / `withdraw` | [bgp_srv6.c:367](bgpd/bgp_srv6.c#L367) / [403](bgpd/bgp_srv6.c#L403) | 面向邻居的通告/撤销 |

配置保存在 `bgp->srv6_unicast[afi]`（`struct bgp_srv6_unicast`），含 SID、locator、模式标志、route-map 名、`zebra_sid_last_sent`（用于精确撤销）。

### 8.2 SID 组合与 transposition（在 `bgp_mplsvpn.c`）

**注意**：L3VPN 场景下的 SID 组合/转置**不在 `bgp_srv6.c`，而在 `bgp_mplsvpn.c`**（从 RFC9252 transposition 方案移植）：

| 函数 | 定义 | 作用 |
|------|------|------|
| `srv6_sid_compose` | [bgp_mplsvpn.c:854](bgpd/bgp_mplsvpn.c#L854) | 由 LOCATOR 前缀 + FUNCTION 组合出完整 SID；供分配前做冲突检查 |
| `transpose_sid` | [bgp_mplsvpn.c:1285](bgpd/bgp_mplsvpn.c#L1285) | 把 label 的高 len 位写入 SID 中从 offset 起的位置（RFC8986 transposition） |
| `ensure_vrf_tovpn_sid_per_af` | [bgp_mplsvpn.c:954](bgpd/bgp_mplsvpn.c#L954) | per-AFI 场景为 VRF 申请 SID |
| `ensure_vrf_tovpn_sid_per_vrf` | [bgp_mplsvpn.c:1047](bgpd/bgp_mplsvpn.c#L1047) | per-VRF 场景为整个 VRF 申请一个 SID |
| `ensure_vrf_tovpn_sid` | [bgp_mplsvpn.c:1138](bgpd/bgp_mplsvpn.c#L1138) | 分发：优先 per-AFI，否则 per-VRF |

函数位宽常量（[bgp_mplsvpn.h](bgpd/bgp_mplsvpn.h)）：`BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL=20`（可塞进 20bit 标签最高位）、`FOR_BGP=32`（属性直接承载 FUNC 的上限）。

### 8.3 对端 Prefix-SID 属性：`bgp_attr_srv6.h`

`bgp_attr_srv6.h`（75 行）只定义一个结构：`struct bgp_attr_srv6_l3service`（[bgp_attr_srv6.h:63](bgpd/bgp_attr_srv6.h#L63)），对应 RFC9252 的「SRv6 SID Information Sub-TLV」：

| 字段 | 语义 |
|------|------|
| `refcnt` | interned 共享计数（>1 时视为只读） |
| `sid_flags` | SID 标志位 |
| `endpoint_behavior` | 16 bit **Endpoint Behavior**（动作码，如 End.DT4/DT6/End.X，编码见 IANA 注册表） |
| `sid` | SID 本体，128 bit（`struct in6_addr`） |
| `loc_block_len` / `loc_node_len` | Locator 的 Block / Node 长度 |
| `func_len` / `arg_len` | Function / Arguments 长度 |
| `transposition_len` / `transposition_offset` | transposition 长度与偏移（RFC8986） |

**结构定位**：Prefix-SID attribute 是 `[TLV 类型 | 长度 | 值]`；类型 5 = SRv6 L3 Service TLV，其值里再嵌 SRv6 Service Data Sub-TLV，子 TLV 类型 1 = SRv6 SID Information Sub-TLV（本结构体）。**一个前缀可能有多个**。

> 导读特别强调：那几个 `*_len` 描述的是**SID 在本域内的切分方式**，不是任何字段自身的字节数（SID 恒为 16 字节）。

**收发包的实现不在本卷文件**：解析/intern/unintern 在 `bgp_attr.c`（`bgp_attr_srv6_l3service_parse` / `_intern` / `_unintern`）。`bgp_attr_srv6.h` 只给内部表示。**编译开关**：SRv6 相关代码默认编入，但功能靠配置开启。

---

## §9 封装扩展团体 TLV（`bgp_encap_tlv.c` / `bgp_encap_types.h` / `bgp_encap_tlv.h`）

> 本节是用户正在读的 `bgp_encap_tlv.c`（1163 行），写得格外扎实。

### 9.1 先辨析两套「encap」机制（最大的阅读陷阱）

`bgp_encap_types.h` 里**并存两套名字都带 encap、都带类型号的机制**，线格式、携带位置、表达能力完全不同：

| | ① **Tunnel Encapsulation 属性**（RFC 9012） | ② **Encapsulation 扩展团体** |
|---|---|---|
| 本领 | 内容丰富的那个：一串「隧道类型」条目，每条又由若干 **sub-TLV** 组成 | 只有 8 字节，本质就是**一个隧道类型号**，无参数 |
| 数据 | `bgp_tea_subtlv_*`、`bgp_encap_type_*` 结构 | 无结构，直接是个类型号 |
| 实现 | `bgp_encap_tlv.c` 的编解码函数 | [bgp_encap_types.h:351](bgpd/bgp_encap_types.h#L351) `encode_encap_extcomm` / [bgp_encap_types.h:370](bgpd/bgp_encap_types.h#L370) `ecommunity_tunnel_type` |
| 用途 | 描述「用哪种封装、端点在哪儿、VNI/KEY 多少」 | 给路由「贴一个隧道类型标签」（SD-WAN/color 场景） |

**两者共用 `bgp_encap_types` 这个枚举**，所以看到它被两处引用时尤其容易看走眼。

### 9.2 隧道类型号枚举表

`enum bgp_encap_types`（[bgp_encap_types.h:55](bgpd/bgp_encap_types.h#L55) 起，取值来自 IANA 注册表，**不能自造**）：

| 值 | 名字 | 备注 |
|----|------|------|
| 0 | `RESERVED` | 保留，不是「默认 GRE」 |
| 1 | `L2TPV3_OVER_IP` | |
| 2 | `GRE` | |
| 3 | `TRANSMIT_TUNNEL_ENDPOINT` | |
| 4 | `IPSEC_IN_TUNNEL_MODE` | |
| 5 | `IP_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE` | |
| 6 | `MPLS_IN_IP_TUNNEL_WITH_IPSEC_TRANSPORT_MODE` | |
| 7 | `IP_IN_IP` | |
| 8 | `VXLAN` | **EVPN 实际使用的类型** |
| 9 | `NVGRE` | |
| 10 | `MPLS` | 注释：「Encap SAFI&Attribute not used」 |
| 11 | `MPLS_IN_GRE` | |
| 12 | `VXLAN_GPE` | |
| 13 | `MPLS_IN_UDP` | |
| 14 | `PBB` | **没有写显式数值**，靠前一个值自增得到（维护风险：在前面插条目会静默改变它） |
| 255 | `SRV6` | 「For internal usage only」，FRR 内部私用值，**绝不能当标准值发给对端** |

### 9.3 sub-TLV 类型与 `valid_subtlvs`

`enum bgp_encap_subtlv_types`（[bgp_encap_types.h:84](bgpd/bgp_encap_types.h#L84) 起）：

| 值 | 名字 | 承载 |
|----|------|------|
| 1 | `ENCAPSULATION` | 该隧道自己的封装参数（GRE key、L2TPv3 session、PBB 等） |
| 2 | `PROTO_TYPE` | 承载的**以太类型**（EtherType，如 0x0800）——注意不是 IP 协议号 |
| 3 | `IPSEC_TA` | IPsec 隧道认证串 |
| 4 | `COLOR` | color（SD-WAN 染色） |
| 6 | `REMOTE_ENDPOINT` | 远端隧道端点 + AS（**跳过 5**，不是笔误） |

对应的 sub-TLV 结构体（原生数据）：

| 结构 | 定义 | 字段 |
|------|------|------|
| `bgp_tea_subtlv_encap_l2tpv3_over_ip` | [bgp_encap_types.h:101](bgpd/bgp_encap_types.h#L101) | sessionid / cookie_length / cookie[8] |
| `bgp_tea_subtlv_encap_gre_key` | [bgp_encap_types.h:109](bgpd/bgp_encap_types.h#L109) | gre_key（主机序） |
| `bgp_tea_subtlv_encap_pbb` | [bgp_encap_types.h:124](bgpd/bgp_encap_types.h#L124) | 位域 flag_isid/flag_vid/isid/vid + macaddr |
| `bgp_tea_subtlv_proto_type` | [bgp_encap_types.h:134](bgpd/bgp_encap_types.h#L134) | proto（EtherType） |
| `bgp_tea_subtlv_color` | [bgp_encap_types.h:140](bgpd/bgp_encap_types.h#L140) | color |
| `bgp_tea_subtlv_remote_endpoint` | [bgp_encap_types.h:154](bgpd/bgp_encap_types.h#L154) | family + union{v4,v6} + **as4（always 4 bytes）** |
| `bgp_tea_subtlv_ipsec_ta` | [bgp_encap_types.h:177](bgpd/bgp_encap_types.h#L177) | authenticator_type/length/value[20] |

**valid_subtlvs 位图**（[bgp_encap_types.h:194](bgpd/bgp_encap_types.h#L194) 起）是「读懂那堆结构体的钥匙」：每个 `bgp_encap_type_xxx` 结构体**固定包含该类型可能用到的全部 sub-TLV 字段**，用位图标记「哪些字段真收到了」。

| 位 | 值 |
|----|----|
| `BGP_TEA_SUBTLV_ENCAP` | 0x1 |
| `BGP_TEA_SUBTLV_PROTO_TYPE` | 0x2 |
| `BGP_TEA_SUBTLV_COLOR` | 0x4 |
| `BGP_TEA_SUBTLV_IPSEC_TA` | 0x8 |
| `BGP_TEA_SUBTLV_REMOTE_ENDPOINT` | 0x10 |

三个操作宏在 [bgp_encap_types.h:203-205](bgpd/bgp_encap_types.h#L203)：`CHECK_SUBTLV_FLAG` / `SET_SUBTLV_FLAG` / `UNSET_SUBTLV_FLAG`。**读取任何 `st_xxx` 字段前必须先 `CHECK_SUBTLV_FLAG`**，否则会把未初始化的零值当真实参数用。

**PBB 结构用了位域**——导读明确警告：位域内存布局是「实现定义」，**不能直接当线格式缓冲区读写**，必须先解析成字段再序列化。

### 9.4 编码半段（内存 → 报文）

`bgp_encap_tlv.c` 的布局是**上下两大对称半段**（[bgp_encap_tlv.c](bgpd/bgp_encap_tlv.c) 导读）：

**① 单个 sub-TLV 编码器**（`subtlv_encode_xxx`，返回**新分配**的 `bgp_attr_encap_subtlv` 节点）：

| 函数 | 定义 |
|------|------|
| `subtlv_encode_encap_l2tpv3_over_ip` | [bgp_encap_tlv.c:68](bgpd/bgp_encap_tlv.c#L68) |
| `subtlv_encode_encap_gre` | [bgp_encap_tlv.c:99](bgpd/bgp_encap_tlv.c#L99) |
| `subtlv_encode_encap_pbb` | [bgp_encap_tlv.c:129](bgpd/bgp_encap_tlv.c#L129) |
| `subtlv_encode_proto_type` | [bgp_encap_tlv.c:164](bgpd/bgp_encap_tlv.c#L164) |
| `subtlv_encode_color` | [bgp_encap_tlv.c:190](bgpd/bgp_encap_tlv.c#L190) |
| `subtlv_encode_ipsec_ta` | [bgp_encap_tlv.c:224](bgpd/bgp_encap_tlv.c#L224) |
| `subtlv_encode_remote_endpoint` | [bgp_encap_tlv.c:254](bgpd/bgp_encap_tlv.c#L254) |

**② 隧道类型级编码器**（`bgp_encap_type_xxx_to_tlv`，返回 void，按 `valid_subtlvs` 位图把各有效 sub-TLV **追加**进 `attr->encap_subtlvs` 链表）。它们共用一个帮手宏 `ENC_SUBTLV(flag, function, field)`（[bgp_encap_tlv.c:298](bgpd/bgp_encap_tlv.c#L298)），该宏要求调用方在作用域里定义了 `last`（当前链表尾）与 `attr`：

```c
#define ENC_SUBTLV(flag, function, field)                                  \
        do {                                                               \
                if (CHECK_FLAG(bet->valid_subtlvs, (flag))) {              \
                        new = function(&bet->field);                       \
                        if (last) last->next = new; else attr->encap_subtlvs = new; \
                        last = new;                                        \
                }                                                          \
        } while (0)
```

15 个编码函数：

| 函数 | 定义 | 编码的 sub-TLV |
|------|------|---------------|
| `bgp_encap_type_l2tpv3overip_to_tlv` | [bgp_encap_tlv.c:316](bgpd/bgp_encap_tlv.c#L316) | ENCAP(断言必有) + PROTO + COLOR + REMOTE_ENDPOINT |
| `bgp_encap_type_gre_to_tlv` | [bgp_encap_tlv.c:341](bgpd/bgp_encap_tlv.c#L341) | ENCAP(GRE key) + PROTO + COLOR + REMOTE_ENDPOINT |
| `bgp_encap_type_ip_in_ip_to_tlv` | [bgp_encap_tlv.c:363](bgpd/bgp_encap_tlv.c#L363) | PROTO + COLOR + REMOTE_ENDPOINT（无封装参数） |
| `bgp_encap_type_transmit_tunnel_endpoint` | [bgp_encap_tlv.c:384](bgpd/bgp_encap_tlv.c#L384) | 无 sub-TLV（只置类型号） |
| `bgp_encap_type_ipsec_in_tunnel_mode_to_tlv` | [bgp_encap_tlv.c:402](bgpd/bgp_encap_tlv.c#L402) | 只 IPSEC_TA（**不编 REMOTE_ENDPOINT**） |
| `bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode_to_tlv` | [bgp_encap_tlv.c:420](bgpd/bgp_encap_tlv.c#L420) | IPSEC_TA |
| `bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode_to_tlv` | [bgp_encap_tlv.c:440](bgpd/bgp_encap_tlv.c#L440) | IPSEC_TA |
| `bgp_encap_type_pbb_to_tlv` | [bgp_encap_tlv.c:460](bgpd/bgp_encap_tlv.c#L460) | ENCAP(PBB) + PROTO + COLOR + REMOTE_ENDPOINT |
| `bgp_encap_type_vxlan_to_tlv` | [bgp_encap_tlv.c:483](bgpd/bgp_encap_tlv.c#L483) | **唯一例外**：XFREE 掉现有链表后只写一个 12 字节 sub-TLV（VNID+MAC），并置 `BGP_ENCAP_TYPE_VXLAN` |
| `bgp_encap_type_nvgre_to_tlv` | [bgp_encap_tlv.c:512](bgpd/bgp_encap_tlv.c#L512) | 只置类型号 |
| `bgp_encap_type_mpls_to_tlv` | [bgp_encap_tlv.c:521](bgpd/bgp_encap_tlv.c#L521) | **直接 return**（MPLS 不用这个封装属性） |
| `bgp_encap_type_mpls_in_gre_to_tlv` | [bgp_encap_tlv.c:529](bgpd/bgp_encap_tlv.c#L529) | 只置类型号 |
| `bgp_encap_type_vxlan_gpe_to_tlv` | [bgp_encap_tlv.c:537](bgpd/bgp_encap_tlv.c#L537) | 只置类型号 |
| `bgp_encap_type_mpls_in_udp_to_tlv` | [bgp_encap_tlv.c:546](bgpd/bgp_encap_tlv.c#L546) | 只置类型号 |
| `bgp_encap_type_pbb_to_tlv` 见上 | | |

> VXLAN 编码器值得单独记：它 `XFREE(MTYPE_ENCAP_TLV, attr->encap_subtlvs)` 后**覆盖式**写一个 sub-TLV（type=1，length=12：4 字节 `vnid|VXLAN_ENCAP_MASK_VNID_VALID` + 6 字节可选 MAC），并用 `attr->encap_tunneltype = BGP_ENCAP_TYPE_VXLAN`。这是唯一不「追加」而是「重置」的编码器。

### 9.5 解码半段（报文 → 内存）

**① 单个 sub-TLV 解码器**（`subtlv_decode_xxx`，返回 int 错误码，0 成功）：

| 函数 | 定义 |
|------|------|
| `subtlv_decode_encap_l2tpv3_over_ip` | [bgp_encap_tlv.c:563](bgpd/bgp_encap_tlv.c#L563) |
| `subtlv_decode_encap_gre` | [bgp_encap_tlv.c:587](bgpd/bgp_encap_tlv.c#L587) |
| `subtlv_decode_encap_pbb` | [bgp_encap_tlv.c:603](bgpd/bgp_encap_tlv.c#L603) |
| `subtlv_decode_proto_type` | [bgp_encap_tlv.c:627](bgpd/bgp_encap_tlv.c#L627) |
| `subtlv_decode_color` | [bgp_encap_tlv.c:644](bgpd/bgp_encap_tlv.c#L644) |
| `subtlv_decode_ipsec_ta` | [bgp_encap_tlv.c:668](bgpd/bgp_encap_tlv.c#L668) |
| `subtlv_decode_remote_endpoint` | [bgp_encap_tlv.c:690](bgpd/bgp_encap_tlv.c#L690) |

**② 隧道类型级解码器**（`tlv_to_bgp_encap_type_xxx`，遍历 sub-TLV 链按 type 分发，返回累计错误码）。**方向与编码相反**：`stlv` 是只读输入链表，`bet` 由**调用方预分配**（[bgp_encap_tlv.c](bgpd/bgp_encap_tlv.c) 导读的「caller-allocated」）。示例见 [bgp_encap_tlv.c:765](bgpd/bgp_encap_tlv.c#L765) `tlv_to_bgp_encap_type_gre`：

```c
for (st = stlv; st; st = st->next) {
        switch (st->type) {
        case BGP_ENCAP_SUBTLV_TYPE_ENCAPSULATION:
                rc |= subtlv_decode_encap_gre(st, &bet->st_encap);
                SET_SUBTLV_FLAG(bet, BGP_TEA_SUBTLV_ENCAP);
                break;
        ...
        default:
                zlog_debug("%s: unexpected subtlv type %d", __func__, st->type);
                rc |= -1;   /* 记错误但继续 */
                break;
        }
}
```

15 个解码函数（及定义行）：

| 函数 | 定义 |
|------|------|
| `tlv_to_bgp_encap_type_l2tpv3overip` | [bgp_encap_tlv.c:721](bgpd/bgp_encap_tlv.c#L721) |
| `tlv_to_bgp_encap_type_gre` | [bgp_encap_tlv.c:765](bgpd/bgp_encap_tlv.c#L765) |
| `tlv_to_bgp_encap_type_ip_in_ip` | [bgp_encap_tlv.c:808](bgpd/bgp_encap_tlv.c#L808) |
| `tlv_to_bgp_encap_type_transmit_tunnel_endpoint` | [bgp_encap_tlv.c:846](bgpd/bgp_encap_tlv.c#L846) |
| `tlv_to_bgp_encap_type_ipsec_in_tunnel_mode` | [bgp_encap_tlv.c:875](bgpd/bgp_encap_tlv.c#L875) |
| `tlv_to_bgp_encap_type_ip_in_ip_tunnel_with_ipsec_transport_mode` | [bgp_encap_tlv.c:908](bgpd/bgp_encap_tlv.c#L908) |
| `tlv_to_bgp_encap_type_mpls_in_ip_tunnel_with_ipsec_transport_mode` | [bgp_encap_tlv.c:941](bgpd/bgp_encap_tlv.c#L941) |
| `tlv_to_bgp_encap_type_vxlan` | [bgp_encap_tlv.c:974](bgpd/bgp_encap_tlv.c#L974) |
| `tlv_to_bgp_encap_type_nvgre` | [bgp_encap_tlv.c:1000](bgpd/bgp_encap_tlv.c#L1000) |
| `tlv_to_bgp_encap_type_mpls` | [bgp_encap_tlv.c:1026](bgpd/bgp_encap_tlv.c#L1026) |
| `tlv_to_bgp_encap_type_mpls_in_gre` | [bgp_encap_tlv.c:1053](bgpd/bgp_encap_tlv.c#L1053) |
| `tlv_to_bgp_encap_type_vxlan_gpe` | [bgp_encap_tlv.c:1080](bgpd/bgp_encap_tlv.c#L1080) |
| `tlv_to_bgp_encap_type_mpls_in_udp` | [bgp_encap_tlv.c:1107](bgpd/bgp_encap_tlv.c#L1107) |
| `tlv_to_bgp_encap_type_pbb` | [bgp_encap_tlv.c:1135](bgpd/bgp_encap_tlv.c#L1135) |

**返回 0 成功、非 0 失败**；解码侧遇到不认识的 sub-TLV 会 `rc |= -1` 但仍走完整条链（向后兼容）。

### 9.6 谁在调用它 —— 通用性的真相

这是本节最关键的实证。`bgp_encap_tlv.c` 的编解码函数**在生产路径上只有两类调用者**：

| 调用者 | 位置 | 用哪些函数 | 编译开关 |
|--------|------|-----------|---------|
| **EVPN 静态路由** | [bgp_route.c:9014](bgpd/bgp_route.c#L9014) | 仅 `bgp_encap_type_vxlan_to_tlv`（当 `bgp_static->encap_tunneltype == BGP_ENCAP_TYPE_VXLAN`） | 无（永远编译） |
| **VNC / RFAPI** | [rfapi/rfapi_encap_tlv.c:74](bgpd/rfapi/rfapi_encap_tlv.c#L74) 起 | **全部 15 个**编码函数 + 全部解码函数 | `ENABLE_BGP_VNC`（默认不编译） |

**此外**，EVPN 在生成 Type-2/Type-3/Type-5 路由属性时用的是**机制 ②（Encapsulation 扩展团体）**，而非本文件的 TLV：见 [bgp_evpn.c:1219](bgpd/bgp_evpn.c#L1219)（`build_evpn_type5_route_extcomm`）与 [bgp_evpn.c:1281](bgpd/bgp_evpn.c#L1281)（`build_evpn_route_extcomm`）——它们都调 `encode_encap_extcomm(BGP_ENCAP_TYPE_VXLAN, &eval)`。

**通用属性框架**（在 `bgp_attr.c`，不属本卷但必须一提）：
- `bgp_attr_encap`（[bgp_attr.c:3427](bgpd/bgp_attr.c#L3427)）：从**线格式**解析 Tunnel Encap 属性成 `encap_subtlvs` 链表（这层解析与具体隧道类型无关，只按 TLV 结构切）；
- `encap_intern` / `encap_unintern` / `encap_tlv_dup` / `encap_same`（[bgp_attr.c:414](bgpd/bgp_attr.c#L414) 起）：子 TLV 链的 interned 共享；
- `bgp_packet_mpattr_tea`（[bgp_attr.c:5403](bgpd/bgp_attr.c#L5403)）：把 `encap_subtlvs` 编回报文。

> **通用性结论**：`bgp_encap_tlv.c` 提供的是一套「**与具体隧道类型无关、15 种类型平行**」的编解码器；谁都可以用它的某个类型（EVPN 只用 VXLAN 编码；VNC 用全部 15 种）。它**不是**「EVPN 与 flowspec 共用的编解码器」——flowspec 目录里没有任何对 `bgp_encap_type_*` / `bgp_attr_encap_subtlv` 的引用（见 §11 勘误）。

### 9.7 `bgp_encap_tlv.h` 的一个结构性问题

`bgp_encap_tlv.h`（192 行）把「编码半段」与「解码半段」的 extern 声明分列。**注意**：[bgp_encap_tlv.h:165](bgpd/bgp_encap_tlv.h#L165) 与 [bgp_encap_tlv.h:169](bgpd/bgp_encap_tlv.h#L169) 把 `tlv_to_bgp_encap_type_mpls` **连续声明了两次**（内容完全相同）。文件自身的注释（[bgp_encap_tlv.h:115](bgpd/bgp_encap_tlv.h#L115)）已指出这是重复声明。这不是 bug（重复声明合法），但读接口清单时别把它当成两个不同的函数。

**命名不一致**：编码侧 `bgp_encap_type_l2tpv3overip_to_tlv` 里是 `l2tpv3overip`（无下划线），而结构体名与解码侧都是 `l2tpv3_over_ip`（有下划线）——[bgp_encap_tlv.h](bgpd/bgp_encap_tlv.h) 导读已注明这是历史遗留拼写差异，不是两个类型。

---

## §10 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|------|------|---------|
| 1 | 在 bgpd 目录找 `BGP_EVPN_MAC_IP_ROUTE` 等类型常量 | 找不到 | 它们在 [lib/prefix.h:69](lib/prefix.h#L69) |
| 2 | 把 `EVPN_ENABLED(bgp)` 当成「本 VRF 开了 EVPN」 | 逻辑判断错误 | 它只表示传入实例的 `advertise_all_vni`（[bgp_evpn.h:62](bgpd/bgp_evpn.h#L62)） |
| 3 | 把 L2 VNI（`bgpevpn`）与 L3 VNI 混为一谈 | 张冠李戴 | 二层是独立对象 `bgpevpn`；三层是普通 `struct bgp` VRF + `l3vni` 字段 |
| 4 | 以为 VNI 表前缀与全局表前缀同构 | MAC/IP 字段缺失 | 用 `evpn_type2_prefix_global_copy` 补全（[bgp_evpn_private.h:869](bgpd/bgp_evpn_private.h#L869)） |
| 5 | 在 `bgp_evpn_mh.c` 里找 DF 选举算法 | 找不到 | 算法在 [zebra/zebra_evpn_mh.c:1756](zebra/zebra_evpn_mh.c#L1756)；bgpd 只传播 `df_pref` |
| 6 | 把 `es->flags`（`BGP_EVPNES_`）与 `attr->es_flags`（`ATTR_ES_`）当同一套 | 语义混淆 | 两者含义不同（本地 ES 状态 vs 属性标志） |
| 7 | 把 `bgp_mac.c` 的表当成「转发表」 | 误判 | 它是「本机 MAC 存在性索引」，键为 MAC，值为接口名列表 |
| 8 | 以为 `bm->self_mac_hash` 存 `struct bgpevpn` | 类型错误 | 存 `struct bgp_self_mac`（[bgp_mac.c:62](bgpd/bgp_mac.c#L62)） |
| 9 | 把 `bgp_vpn.c` 当成 RD/RT 编解码器 | 找不到编码函数 | 它只有 `show_adj_route_vpn`；RD 编解码在 `bgp_rd.c`，RT 在 `bgp_ecommunity.c` |
| 10 | RD 解码时不跳过前 2 字节类型 | RD 值错位 | `decode_rd_*(pnt + 2, ...)`（[bgp_vpn.c:176](bgpd/bgp_vpn.c#L176)） |
| 11 | 混淆 `BGP_VPN_POLICY_DIR_TOVPN` / `DIR_FROMVPN` | RT 匹配方向反了 | 导出用 `TOVPN`，导入用 `FROMVPN`（[bgpd.h:313](bgpd/bgpd.h#L313)） |
| 12 | 以为 VPN 导入只看 RT | 漏判 | 还要看 ACCEPT_OWN、SoO、AS 环、MPLS/SRv6 互不覆盖（[bgp_mplsvpn.c:2594](bgpd/bgp_mplsvpn.c#L2594) 起） |
| 13 | 把 `bgp_label.c` 与 `bgp_labelpool.c` 当同一模块 | 找错函数 | 前者管报文标签/共享/FEC；后者管 chunk 分配（§7.1） |
| 14 | 以为收到的 MPLS 标签 TC 位一定是 0 | `bgp_is_withdraw_label` 误判 | 它**同时接受 0x00 与 0x02**（[bgp_label.h:173](bgpd/bgp_label.h#L173)） |
| 15 | 以为标签池断连时不能再用标签 | 逻辑错误 | 断连是空实现，允许继续用旧标签；重连才作废（[bgp_labelpool.c:816](bgpd/bgp_labelpool.c#L816)） |
| 16 | 读 `bgp_tea_subtlv_*` 字段不查 `valid_subtlvs` | 把零值当真实参数 | 读前先 `CHECK_SUBTLV_FLAG` |
| 17 | 把 Tunnel Encap 属性与 Encap 扩展团体当同一机制 | 完全错位 | 前者 8 字节仅类型号（[bgp_encap_types.h:351](bgpd/bgp_encap_types.h#L351)），后者是带 sub-TLV 的属性 |
| 18 | 直接用 `bgp_tea_subtlv_encap_pbb` 的位域做序列化 | 内存布局实现定义，不可移植 | 先解析成字段再序列化 |
| 19 | 在 `bgp_encap_types` 里 PBB 前插入新条目 | PBB 的数值被静默改变 | PBB 无显式数值，靠自增 |
| 20 | 把 `BGP_ENCAP_TYPE_SRV6=255` 当标准类型号发给对端 | 互通失败 | 它是「internal usage only」 |
| 21 | 以为 Type-5 编码会写 MAC | 字段错位 | Type-5 写「IP 前缀 + 网关 IP」，ESI/网关按 overlay index 决定是否真写 |
| 22 | 把 `bgp_vnc_types.h` 的类型号当标准 | 不存在 | 整体在 `ENABLE_BGP_VNC` 下，默认编译不含 |
| 23 | 改 `MPLSVPNVRFRTTYPE*` 的数值 | 破坏 SNMP MIB 契约 | 这些数值必须与 MIB 一致 |
| 24 | 以为 Type-1/Type-4 的解析也在 `bgp_evpn.c` | 找不到 | 解析在 `bgp_evpn_mh.c`；编码统一在 `bgp_evpn.c` |

---

## §11 勘误：注释/导读与代码或事实不符之处

写作过程对照代码复核，发现以下不准确之处（**均未改动源码**）：

| # | 位置 | 说法 | 实际情况 |
|---|------|------|---------|
| 1 | 多份导读（[bgp_evpn_private.h:119](bgpd/bgp_evpn_private.h#L119)、[bgp_evpn.c:23](bgpd/bgp_evpn.c#L23) 等） | 反复称 VNI 表为 `bgp->vni_hash` | `struct bgp` 里**没有 `vni_hash` 字段**，真名是 [bgpd.h:1019](bgpd/bgpd.h#L1019) 的 `vnihash`。`bgp_evpn.c` 里 `vnihash` 出现 23 次、`vni_hash` 仅出现在注释中 |
| 2 | 任务书 / 常见说法 | `bgp_encap_tlv.c` 被 **EVPN 和 flowspec 共用** | flowspec 目录（`bgp_flowspec*.c` / `bgp_flowspec*.h`）**没有任何**对 `bgp_encap_type_*` / `bgp_attr_encap_subtlv` 的引用。真实调用者是**EVPN 静态路由**（[bgp_route.c:9014](bgpd/bgp_route.c#L9014)，仅 VXLAN）与 **VNC/rfapi**（[rfapi/rfapi_encap_tlv.c](bgpd/rfapi/rfapi_encap_tlv.c)，`ENABLE_BGP_VNC`）。见 §9.6 |
| 3 | 任务书 | 「`bgp_vpn.c` 的 RD/RT 编解码」 | `bgp_vpn.c` 只有 `show_adj_route_vpn` 一个函数；RD 解码 `decode_rd_*` 定义在 `bgp_rd.c`，RT 编解码在 `bgp_ecommunity.c`。`bgp_vpn.c` 仅是**调用** RD 解码用于显示 |
| 4 | [bgp_encap_tlv.h:165](bgpd/bgp_encap_tlv.h#L165) 与 [169](bgpd/bgp_encap_tlv.h#L169) | 接口清单 | `tlv_to_bgp_encap_type_mpls` **连续声明两次**（重复声明，非两个函数）；文件注释 [bgp_encap_tlv.h:115](bgpd/bgp_encap_tlv.h#L115) 已指出 |
| 5 | `bgp_evpn_private.h` 导读（[bgp_evpn_private.h:22](bgpd/bgp_evpn_private.h#L22)）与字段（[bgp_evpn_private.h:206](bgpd/bgp_evpn_private.h#L206)） | `remote_ip_hash` 被描述为「bgp 的」哈希 | 它是 **`struct bgpevpn` 的字段**（定义在 [bgp_evpn_private.h:206](bgpd/bgp_evpn_private.h#L206)），不是 `struct bgp` 上的；每个 VNI 各有一份 |

> 以上除 #4 是「重复声明」外，其余均为**注释文本或任务描述与代码/事实不符**，不涉及代码行为。

---

## §12 自测题（复习用）

1. EVPN 五种路由类型的枚举值分别是多少？它们定义在哪个文件（不在 bgpd 目录）？
2. 五类 EVPN 地址结构在内存里怎么统一表示？写出承载它们的 union 与「统一前缀长度」宏。
3. `bgp_evpn_encode_prefix` 与 `bgp_nlri_parse_evpn` 的返回值类型分别是什么？为什么方向相反？
4. Type-2 的路由键由哪几段构成？带「两个标签」的条件是什么？
5. Type-5 的下一跳为什么需要 overlay index？四种 overlay index 里哪一种的数据不在 `struct bgp_route_evpn` 里？
6. MAC-VRF 与 IP-VRF 分别用什么对象承载？它们各自存在哪里？
7. `struct bgpevpn` 的 `mac_table` 与 `ip_table` 各存什么？
8. `VNI_FLAG_USE_TWO_LABELS` 置位意味着什么？
9. `struct bgp_evpn_info` 里 `soo` 的三条规则是什么？
10. `irt_node` 与 `vrf_irt_node` 的区别是什么？它们服务于哪个方向？
11. 从「zebra 学到本地 MAC」到「发往对端」，依次经过哪几个关键函数？
12. 远端 Type-2 最终落到 `mac_table` 的是哪个函数？导入判定会看哪些条件？
13. `bgp_mac.c` 的哈希表键值分别是什么？它是转发表吗？
14. EVPN-MH 里 `es->flags` 与 `attr->es_flags` 有何区别？
15. DF 选举算法实际在哪个进程的哪个函数里？bgpd 侧负责哪三件事？
16. DF 选举的判据是什么（偏好值、平局怎么破）？
17. VPN NLRI 的最小前缀长度是多少？为什么？
18. VRF→VPN 导出时，`tovpn` route-map 被 DENY 会怎样？为什么必须校验含 RT？
19. VPN→VRF 导入时，RT 交集用什么函数判定？除了 RT 还要看哪几件事？
20. `bgp_vpn.c` 的 `show_adj_route_vpn` 为什么不能复用普通 `show_adj_route`？
21. `bgp_label.c` 与 `bgp_labelpool.c` 的职责边界是什么？它们的接点函数是哪个？
22. FRR 为什么把标签 TC 区的 `0x02` 位当「有效」标记？`bgp_is_withdraw_label` 为什么要接受两种取值？
23. 标签池的三层数据结构分别是什么？断连后重连时旧标签怎么处理？
24. `BGP_PREFIX_SID_SRV6_MAX_FUNCTION_LENGTH_FOR_LABEL` 为什么是 20？
25. Tunnel Encap 属性与 Encap 扩展团体有什么本质区别？
26. `valid_subtlvs` 位图解决什么问题？不查它会怎样？
27. `bgp_encap_type_vxlan_to_tlv` 为什么是编码半段里唯一的「例外」？
28. 解码半段为什么要求调用方预分配 `bet`？
29. `bgp_encap_tlv.c` 的 15 个隧道类型里，哪几个「只置类型号、无 sub-TLV」？
30. 生产路径上谁在真正调用 `bgp_encap_tlv.c` 的编解码函数？

<details><summary>参考答案（点击展开）</summary>

1. Type-1~5 分别为 1..5；枚举 `bgp_evpn_route_type` 定义在 [lib/prefix.h:68](lib/prefix.h#L68)。
2. 用 `struct evpn_addr`（route_type 字节 + union，[lib/prefix.h:192](lib/prefix.h#L192)）；统一前缀长度宏 `EVPN_ROUTE_PREFIXLEN`（[bgp_evpn_private.h:61](bgpd/bgp_evpn_private.h#L61)）。
3. 编码返回 `void`，解析返回 `int`（错误码）。方向相反源于「编码不报错、解码要报错」的约定（[bgp_evpn_tlv 系列共同约定]）。
4. 键 =（RD, ESI, EthTag, MAC, IP[len], VNI）；当带 IP 且 `num_labels>1` 时写第二个标签（L3VNI）。
5. 因为 type-5 的下一跳可能是网关 IP / ESI / MAC 逻辑实体；`OVERLAY_INDEX_MAC` 的 MAC 不在结构里，而在 Router MAC 扩展团体。
6. MAC-VRF 是独立堆对象 `struct bgpevpn`（存默认实例的 `vnihash`）；IP-VRF 就是一个普通 `struct bgp` VRF 实例（多带 `l3vni` 等字段）。
7. `mac_table` 以 MAC 为键存 Type-2；`ip_table` 存带 IP 的 Type-2、Type-5 等 IP 相关条目。
8. Type-2 需同时携带 L2VNI 与 L3VNI 两个标签。
9. 所有从 L2VNI 导出的路由都带它；带匹配 SoO 的 Type-2/3 不导入 L2VNI；带匹配 SoO 的 Type-2/5 不导入 L3VNI（防环）。
10. `irt_node` 是 RT→多个 VNI（二层），`vrf_irt_node` 是 RT→多个 VRF（三层）；服务于「按 RT 导入」。
11. `bgp_evpn_local_macip_add` → `build_evpn_type2_prefix` → `update_evpn_route` → 选路 → `bgp_evpn_encode_prefix`。
12. `install_evpn_route_entry_in_vni_mac`（[bgp_evpn.c:4196](bgpd/bgp_evpn.c#L4196)）；导入判定看 RT 匹配、SoO 防环、martian 过滤。
13. 键 = MAC，值 = `struct bgp_self_mac`（MAC + 接口名列表）。不是转发表，是本机 MAC 存在性索引。
14. `es->flags` 是 ES 对象自身的状态（本地/远端等）；`attr->es_flags` 是路由属性上的标志。
15. 在 zebra 的 `zebra_evpn_es_run_df_election`（[zebra/zebra_evpn_mh.c:1756](zebra/zebra_evpn_mh.c#L1756)）。bgpd 负责：接收本地 df_pref、把 df_pref 编进 ESR、学习对端 df_alg/df_pref。
16. 只有发过 ESR 的 VTEP 参选；偏好值更高者胜，偏好相同则 IP 更低者胜；对方算法不是 PREF 则停转 BUM。
17. 最小 `(3+8)*8 = 88` bit（标签 3 + RD 8）；由 `VPN_PREFIXLEN_MIN_BYTES`（[bgp_mplsvpn.c:171](bgpd/bgp_mplsvpn.c#L171)）定义。
18. DENY 则不导出；必须含 RT 是因为对端靠 RT 决定导入，无 RT 的路由无人接收。
19. `ecommunity_include`（[bgp_mplsvpn.c:2650](bgpd/bgp_mplsvpn.c#L2650)）；还要看 ACCEPT_OWN、AS 环、MPLS/SRv6 互不覆盖。
20. VPN 路由是两层结构（RD 表套前缀表），遍历方式与普通表不同。
21. 前者管报文标签/共享/FEC 注册，后者管本机 chunk 分配；接点 = `bgp_reg_for_label_callback`（[bgp_label.c:398](bgpd/bgp_label.c#L398)）。
22. FRR 不用 TC 字段，借 0x02 位当内部有效标记；收到的报文该位可能已被置，故 `bgp_is_withdraw_label` 接受 0x00 与 0x02。
23. `ledger`（labelid→lcb）、`inuse`（标签→labelid）、`chunks`（连续段 + 位图）；重连时旧标签全部作废并用回调 `allocated=false` 通知请求者重新分配。
24. 因为 20 bit 是 MPLS 标签的位宽，SID 的 Function 只有能塞进标签最高 20 位才能用标签承载。
25. Tunnel Encap 属性内容丰富、带 sub-TLV；Encap 扩展团体只有 8 字节、本质是一个类型号。
26. 解决「结构体固定含全部字段、但报文未必都有」的问题；不查会把未初始化的零值当真实参数。
27. 它 `XFREE` 现有链表后**覆盖式**只写一个 12 字节 sub-TLV，而不是按位图「追加」。
28. 因为调用方是按类型号分发的，只有它知道该用哪个结构体，由它分配最自然，且避免 `void *` 转换风险。
29. `transmit_tunnel_endpoint`、`nvgre`、`mpls_in_gre`、`vxlan_gpe`、`mpls_in_udp`（以及 `mpls` 直接 return）。
30. 生产路径只有 EVPN 静态路由（[bgp_route.c:9014](bgpd/bgp_route.c#L9014)，仅 VXLAN 编码）与 VNC/rfapi（全部，`ENABLE_BGP_VNC`）。

</details>

---

## §13 代码索引（按建议阅读顺序）

### 第 1 步：EVPN 类型与 NLRI（先建模型）

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [lib/prefix.h](lib/prefix.h) | [68](lib/prefix.h#L68)、[192](lib/prefix.h#L192) | 五类型枚举与 `struct evpn_addr` union（跨卷引用） |
| [bgpd/bgp_evpn_private.h](bgpd/bgp_evpn_private.h) | [118](bgpd/bgp_evpn_private.h#L118) | `bgpevpn` → `bgp_evpn_info`([296](bgpd/bgp_evpn_private.h#L296)) → `irt_node`([248](bgpd/bgp_evpn_private.h#L248)) → 扩展团体编码器([536](bgpd/bgp_evpn_private.h#L536) 起) → 前缀构造([700](bgpd/bgp_evpn_private.h#L700) 起) |
| [bgpd/bgp_evpn.c](bgpd/bgp_evpn.c) | [7727](bgpd/bgp_evpn.c#L7727)、[7853](bgpd/bgp_evpn.c#L7853) | `bgp_evpn_encode_prefix` → `bgp_nlri_parse_evpn` |
| [bgpd/bgp_attr_evpn.c](bgpd/bgp_attr_evpn.c) | [51](bgpd/bgp_attr_evpn.c#L51) 起 | 扩展团体字段访问器 |

### 第 2 步：本地学习与导入

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_evpn.c](bgpd/bgp_evpn.c) | [8836](bgpd/bgp_evpn.c#L8836) | `bgp_evpn_local_macip_add` |
| | [2679](bgpd/bgp_evpn.c#L2679) | `update_evpn_route` |
| | [3022](bgpd/bgp_evpn.c#L3022) | `bgp_evpn_update_type2_route_entry` |
| | [1281](bgpd/bgp_evpn.c#L1281) | `build_evpn_route_extcomm`（Encap + RT + RMAC） |
| | [5915](bgpd/bgp_evpn.c#L5915) | `process_type2_route` |
| | [8347](bgpd/bgp_evpn.c#L8347) → [5439](bgpd/bgp_evpn.c#L5439) | 导入入口 → 全量安装 |
| | [4196](bgpd/bgp_evpn.c#L4196)、[3773](bgpd/bgp_evpn.c#L3773) | 装入 VNI MAC 表 / VRF 表 |
| | [1480](bgpd/bgp_evpn.c#L1480) | `evpn_zebra_install` |

### 第 3 步：多归属

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_evpn_mh.h](bgpd/bgp_evpn_mh.h) | [169](bgpd/bgp_evpn_mh.h#L169)、[371](bgpd/bgp_evpn_mh.h#L371) | `bgp_evpn_es`、`bgp_evpn_es_evi` |
| [bgpd/bgp_evpn_mh.c](bgpd/bgp_evpn_mh.c) | [2814](bgpd/bgp_evpn_mh.c#L2814) | `bgp_evpn_local_es_add` |
| | [861](bgpd/bgp_evpn_mh.c#L861)、[847](bgpd/bgp_evpn_mh.c#L847) | Type-4 生成 / DF 扩展团体 |
| | [1323](bgpd/bgp_evpn_mh.c#L1323)、[1347](bgpd/bgp_evpn_mh.c#L1347) | EAD-per-ES / EAD-per-EVI |
| | [948](bgpd/bgp_evpn_mh.c#L948)、[1482](bgpd/bgp_evpn_mh.c#L1482)、[1790](bgpd/bgp_evpn_mh.c#L1790) | 远端 Type-4/Type-1 / es_vtep_add |
| [zebra/zebra_evpn_mh.c](zebra/zebra_evpn_mh.c) | [1756](zebra/zebra_evpn_mh.c#L1756) | **DF 选举算法真正所在**（跨卷引用） |

### 第 4 步：L3VPN

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_mplsvpn.c](bgpd/bgp_mplsvpn.c) | [135](bgpd/bgp_mplsvpn.c#L135) | `bgp_nlri_parse_vpn` |
| | [2108](bgpd/bgp_mplsvpn.c#L2108) | `vpn_leak_from_vrf_update`（导出） |
| | [1414](bgpd/bgp_mplsvpn.c#L1414) | `leak_update`（落库核心） |
| | [2594](bgpd/bgp_mplsvpn.c#L2594) | `vpn_leak_to_vrf_update_onevrf`（导入 + RT 交集） |
| | [4337](bgpd/bgp_mplsvpn.c#L4337) | `bgp_mplsvpn_init` |
| [bgpd/bgp_mplsvpn.h](bgpd/bgp_mplsvpn.h) | 头部 | 方向常量与内联状态判定 |
| [bgpd/bgp_vpn.c](bgpd/bgp_vpn.c) | [56](bgpd/bgp_vpn.c#L56) | `show_adj_route_vpn` |
| [bgpd/bgp_mplsvpn_snmp.c](bgpd/bgp_mplsvpn_snmp.c) | [65](bgpd/bgp_mplsvpn_snmp.c#L65) | MIB OID 与取值实现 |

### 第 5 步：标签

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_label.h](bgpd/bgp_label.h) | [64](bgpd/bgp_label.h#L64)、[173](bgpd/bgp_label.h#L173) | `BGP_MAX_LABELS`、`bgp_is_withdraw_label` |
| [bgpd/bgp_label.c](bgpd/bgp_label.c) | [135](bgpd/bgp_label.c#L135)、[274](bgpd/bgp_label.c#L274)、[456](bgpd/bgp_label.c#L456)、[398](bgpd/bgp_label.c#L398)、[599](bgpd/bgp_label.c#L599)、[748](bgpd/bgp_label.c#L748) | intern → adv_label → 注册 → 回调 → 收包解析 → decode |
| [bgpd/bgp_labelpool.h](bgpd/bgp_labelpool.h) | [70](bgpd/bgp_labelpool.h#L70)、[134](bgpd/bgp_labelpool.h#L134) | `labelpool`、每下一跳缓存 |
| [bgpd/bgp_labelpool.c](bgpd/bgp_labelpool.c) | [260](bgpd/bgp_labelpool.c#L260)、[476](bgpd/bgp_labelpool.c#L476)、[698](bgpd/bgp_labelpool.c#L698)、[781](bgpd/bgp_labelpool.c#L781)、[835](bgpd/bgp_labelpool.c#L835) | init → get → sync → event_chunk → zebra_up |

### 第 6 步：SRv6 与封装 TLV

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_srv6.c](bgpd/bgp_srv6.c) | [65](bgpd/bgp_srv6.c#L65)、[289](bgpd/bgp_srv6.c#L289)、[367](bgpd/bgp_srv6.c#L367) | ensure_sid → register_route → announce |
| [bgpd/bgp_attr_srv6.h](bgpd/bgp_attr_srv6.h) | [63](bgpd/bgp_attr_srv6.h#L63) | `bgp_attr_srv6_l3service` |
| [bgpd/bgp_mplsvpn.c](bgpd/bgp_mplsvpn.c) | [854](bgpd/bgp_mplsvpn.c#L854)、[1285](bgpd/bgp_mplsvpn.c#L1285) | `srv6_sid_compose`、`transpose_sid` |
| [bgpd/bgp_encap_types.h](bgpd/bgp_encap_types.h) | [55](bgpd/bgp_encap_types.h#L55)、[84](bgpd/bgp_encap_types.h#L84)、[194](bgpd/bgp_encap_types.h#L194)、[351](bgpd/bgp_encap_types.h#L351) | 类型枚举 → sub-TLV 枚举 → 位图 → Encap 扩展团体 |
| [bgpd/bgp_encap_tlv.c](bgpd/bgp_encap_tlv.c) | [68](bgpd/bgp_encap_tlv.c#L68)、[298](bgpd/bgp_encap_tlv.c#L298)、[316](bgpd/bgp_encap_tlv.c#L316)、[483](bgpd/bgp_encap_tlv.c#L483)、[563](bgpd/bgp_encap_tlv.c#L563)、[765](bgpd/bgp_encap_tlv.c#L765) | sub-TLV 编码 → ENC_SUBTLV 宏 → 隧道级编码 → VXLAN → sub-TLV 解码 → 隧道级解码 |
| [bgpd/bgp_route.c](bgpd/bgp_route.c) | [9014](bgpd/bgp_route.c#L9014) | 生产路径唯一 EVPN 调用点 |

### 附：EVPN CLI

| 文件 | 关键位置 | 看什么 |
|------|---------|--------|
| [bgpd/bgp_evpn_vty.c](bgpd/bgp_evpn_vty.c) | [3519](bgpd/bgp_evpn_vty.c#L3519)、[431](bgpd/bgp_evpn_vty.c#L431)、[8199](bgpd/bgp_evpn_vty.c#L8199)、[8448](bgpd/bgp_evpn_vty.c#L8448) | `evpn_show_all_routes` → 表头 → 配置回写 → 命令注册 |

---

## §14 与官方 `doc/developer/*.rst` / `doc/user/*.rst` 的对应

官方文档里没有专门的「EVPN 内部实现」开发者文档，相关说明散落在用户手册与路由文档中：

| 本文 | 官方文档 | 说明 |
|------|---------|------|
| §1 EVPN 类型 | [`doc/user/evpn.rst`](doc/user/evpn.rst)（若存在） | 官方从「怎么配」讲起，本文从「代码里怎么表示」讲起，互补 |
| §4 多归属 / DF | EVPN-MH 相关章节 | 官方说明 `es-df-pref` 等配置；本文补出「选举算法在 zebra」这一关键边界 |
| §6 L3VPN | [`doc/user/bgp.rst`](doc/user/bgp.rst) 的 L3VPN / VRF 章节 | 官方讲 `vrf`、`import vrf`、`export`；本文对齐到 `vpn_policy` 与 leak 函数 |
| §7 MPLS 标签 | BGP-LU / label 相关章节 | 官方讲 `label vpn export` 等；本文补出 labelpool 的 chunk 状态机 |
| §8 SRv6 | SRv6 章节（`segment-routing srv6`） | 官方讲 locator/sid 配置；本文对齐到 `bgp_srv6.c` 与 `bgp_attr_srv6.h` |
| §9 Encap TLV | RFC 9012 / draft-rosen-idr-tunnel-encaps | 官方无专文；本文直接对照 RFC 与 `bgp_encap_types.h` |

其他相关规范（本文引用，均注明章节）：

| 规范 | 关系 |
|------|------|
| RFC 7432 | EVPN 基础（五类型、ES、DF） |
| RFC 8365 | EVPN 自动 RT 派生（`advertise_autort_rfc8365`） |
| RFC 9135 / RFC 9136 | Router MAC 扩展团体、IP-Prefix 路由 |
| RFC 4364 | MPLS L3VPN（VPNv4，RD/RT） |
| RFC 3107 / RFC 8277 | BGP-LU（标签承载、10 标签上限） |
| RFC 9252 | SRv6 BGP Overlay（Prefix-SID 属性、transposition） |
| RFC 8986 | SRv6 网络编程（Endpoint Behavior、transposition） |
| RFC 9012 | Tunnel Encapsulation 属性（本文 §9） |
| RFC 4382 | MPLS L3VPN MIB（本文 §6.6） |
| draft-ietf-bess-evpn-pref-df | 偏好值 DF 选举（本文 §4.6） |

---

## 结语：一句话串起本卷

> **EVPN 与 MPLS L3VPN 是 bgpd 里两条「多协议承载路径」，共用同一批底层设施（RD/RT、MPLS 标签、导入导出泄漏框架、Tunnel Encap 扩展团体），但把「值语义」分别定义在两套 NLRI 上：EVPN 把 MAC/IP 装进 `struct evpn_addr` 的五个 union 成员，L3VPN 把「标签 + RD + 前缀」装进两层 RD 表。**
>
> 读本卷只要抓住三条主线即可：
> 1. **EVPN 的类型/实例**：五类型 → 三种 RT 映射 → MAC-VRF（`bgpevpn`）与 IP-VRF（`struct bgp` VRF）→ `mac_table`/`ip_table`；
> 2. **两种「导入」**：EVPN 的「全局表按 RT 落到 VNI/VRF」与 L3VPN 的「VPN 表按 RT 交集落到 VRF」是同一哲学的两处实现；
> 3. **两条标签供给链**：`bgp_label.c`（报文标签/FEC）与 `bgp_labelpool.c`（本机 chunk），靠 `bgp_reg_for_label_callback` 缝合。
>
> 而 EVPN 多归属的 DF 选举、以及 Tunnel Encap 的 TLV 编解码，一个「算法在 zebra」、一个「生产用户只有 EVPN+VNC」，是本卷最容易踩错的两处边界。

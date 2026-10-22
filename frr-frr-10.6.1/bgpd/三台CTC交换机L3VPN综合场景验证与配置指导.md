# 三台 CTC 交换机 L3VPN over MPLS 综合场景验证与配置指导

| 项 | 内容 |
|---|---|
| 文档版本 | v1.0 |
| 编写日期 | 2026-10-19 |
| 适用软件 | FRR 10.6.1 + CTC SDK 5.5.3 + HSL（hw_mpls.c / hw_l3.c / hsl_mpls_vrf.c）+ ctc_dplane.c |
| 适用硬件 | 三台 CTC 交换芯片设备（物理口二层 + vlanif 三层），可扩展至四台 |
| 验证范围 | L3VPN over MPLS（RFC 4364）控制面、数据面、CTC 硬件落表、故障收敛、资源生命周期 |
| 关联文档 | 《三台 CTC 交换机 LDP 场景验证测试清单》v1.5（基础 LDP/PHP/SWAP 部分） |

**修订记录**

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-10-19 | 首版：基础 RIP 场景 + PE-CE eBGP + 静态路由 + 多 VRF + Hub-Spoke + OSPF underlay + explicit-null + 双归属/ECMP/IPv6/跨域等扩展场景 |

---

## 0. 阅读指南

### 0.1 本文档解决什么问题

本文档面向 **测试工程师、配置工程师、现场交付工程师**，回答三个问题：

1. **怎么配**：从零开始，按什么顺序把三台 CTC 交换机配成一条可硬件转发的 L3VPN；
2. **怎么验**：每个场景在控制面、数据面、芯片表项上分别看什么、期望值是什么；
3. **怎么判**：出现问题时按什么顺序定位，什么结果算 PASS，什么结果算 FAIL。

### 0.2 场景分级与执行顺序

| 级别 | 含义 | 建议 |
|---|---|---|
| P0 | 必测。不通过则 L3VPN 不能交付 | 按 S1 → S2 → S4 → S7 顺序执行 |
| P1 | 应测。覆盖常见客户接入方式与运维场景 | P0 全绿后执行 |
| P2 | 扩展/条件项。依赖设备数量、版本特性或客户需求 | 按项目需要选做 |

**执行纪律**：每个场景都必须先过"控制面四段接力"检查，再谈数据面；控制面没通就抓包或查芯片，等于在错误的前提上找证据。

### 0.3 术语与约定

| 术语 | 含义 |
|---|---|
| PE / P / CE | Provider Edge / Provider / Customer Edge |
| VRF | Virtual Routing and Forwarding，本文统一用 CUST1、CUST2 |
| RD | Route Distinguisher，管 VPNv4 表内前缀唯一性，两端必须不同 |
| RT | Route Target，管 VPN 归属，两端必须相同 |
| VPN 标签 / VC 标签 | BGP 为 VRF 路由分配的**内层**标签，egress PE 用它定位 VRF |
| transport 标签 / 外层标签 | LDP 为对端 lo/32 分配的**外层**标签，P 用它逐跳转发 |
| ILM | Incoming Label Map，入标签映射表 |
| IPUC | 芯片的 IP 单播路由表（LPM/ALPM） |
| `vrf_id` | FRR 内部 VRF ID（如 8），**不是** Linux table ID（如 100） |
| `table N` | Linux VRF 路由表号（`ip link add CUST1 type vrf table 100` 里的 100） |

> ⚠️ **`vrf_id` 与 `table` 不是一回事。** `show vrf` 输出里的 `id` 才是 FRR VRF ID，也是下发给芯片的 `vrfId`；`table` 只是 Linux 路由表号。硬件验证时如果拿 `table 100` 去查 `show ipuc 100 ...`，一定查不到。本文示例中 CUST1 的 FRR `vrf_id` 以设备实际输出为准（实验室曾为 **8**）。

---

## 1. 验证对象、架构与数据通路

### 1.1 软件栈分层

```
┌────────────────────────────────────────────────────────────────────┐
│ 控制面                                                             │
│  bgpd（VPNv4 / VRF 实例）                                          │
│      │  ZAPI                                                       │
│  zebra（VRF RIB、LSP 表、nexthop 解析）                            │
│      │  dplane provider                                            │
│  ctc_dplane.c（把 ZAPI 对象翻译成 CTC HAL 消息）                   │
│      │  HAL_MODULE_ROUTE / HAL_MODULE_MPLS                         │
│  hsl（hsl_rt_vrf.c / hsl_mpls_vrf.c / hsl_l3.c / hsl_mpls.c）      │
│      │                                                             │
│  hw_l3.c（VRF 路由：IPUC + push nexthop）                          │
│  hw_mpls.c（ILM、MPLS nexthop、tunnel/push 两步模型）              │
│      │  ctcs_* API                                                 │
│  CTC SDK 5.5.3                                                     │
│      │                                                             │
│  CTC 交换芯片                                                       │
└────────────────────────────────────────────────────────────────────┘
```

### 1.2 关键表项映射

L3VPN 在芯片里不是一个"VPN 表"，而是由四类普通表项组合出来的。每个场景的硬件验证，本质上就是把这四类表项逐一核对。

| # | 表项 | 触发来源 | 关键字段 | 芯片侧表现 |
|---|---|---|---|---|
| T1 | **Underlay ILM**（NORMAL） | LDP 为 lo/32、互联段分配标签 | `in_label`、`out_label`、`nhid`、`pop/decap` | `DsMpls` NORMAL 项 + 出向 nexthop |
| T2 | **Ingress VPN 路由**（IPUC + push NH） | zebra 把带标签的 VRF 路由下发给 dplane | `vrf_id`、prefix、`label_stack[outer, inner]`、`nhid` | `DsIpDa/IPUC` 路由 + tunnel NH（外层）+ push NH（内层） |
| T3 | **Egress VPN 标签 ILM**（L3VPN） | `ZEBRA_VRF_LABEL` / BGP 每 VRF 出口标签 | `(vrf_id, in_label)`、`innerPacketLookup` | `DsMpls` L3VPN 项：`fidSelect=1`、`fidType=0`、`fid=vrf_id` |
| T4 | **Egress 内层 IPv4 路由** | VRF RIB 里的 `B` 路由 / CE 侧路由 | `vrf_id`、prefix、出接口/ARP NH | `DsIpDa` 路由 + ARP nexthop |

> **一句话记住**：T2 负责"压两层标签出去"，T1 负责"在核心逐跳换外层标签"，T3 负责"在 egress PE 弹掉 VPN 标签并进入 VRF"，T4 负责"在 VRF 里查内层 IP 并送到 CE"。

### 1.3 端到端转发流程

以 PC1（10.1.1.10）→ PC2（10.2.2.10）为例，默认 implicit-null 传输标签：

```
PC1 ──(无标签 IP)──> CE1 ──(无标签 IP)──> PE1
PE1：查 CUST1 RIB，10.2.2.0/24 是 BGP VPN 路由
      下一跳 = 3.3.3.3，标签栈 = [transport L_t][VPN L_v]
      芯片：IPUC(vrf_id) 命中 → tunnel NH 压 L_t → push NH 压 L_v
      ──([L_t][L_v][IP])──> P
P  ：外层 L_t 命中 LDP ILM
      默认：P 是倒数第二跳，implicit-null → PHP：弹掉 L_t，原样转发
      ──([L_v][IP]，S=1)──> PE2
PE2：L_v 命中 L3VPN ILM（type=L3VPN, id_type=VRF, vrf_id=8）
      芯片弹掉 L_v → 用 vrf_id=8 查内层 IP → 命中 10.2.2.0/24
      ──(无标签 IP)──> CE2 ──> PC2
```

反向流量 PE2→PE1 完全对称：PE2 压 [transport L_t2][VPN L_v2]，P 对 PE1 的 lo/32 做 PHP，PE1 用 L_v2 进入 CUST1 并把包送到 CE1。

> **PHP 的关键点**：P 收到的可能是 `[L_t][L_v][IP]`，也可能在 P 之前就已经是 `[L_v][IP]`。当前 hw_mpls.c 的 PHP 修正要求：P 不能把外层标签当成 IP 头去查表，也不能把整个标签栈一起丢掉；正确行为是**只消耗外层 transport 标签，保留内层 VPN 标签和 IP**，再按 FEC 下一跳转发。实验室已验证 P 向 PE2 发出的是 `[144][IP]`（144 为 VPN 标签），S=1。

### 1.4 硬件验证的六个证据点（E1–E6）

| 证据 | 位置 | 看什么 | 期望 |
|---|---|---|---|
| **E1** | PE1 | `show ipuc <vrf_id> 10.2.2.0 24 detail` | 路由存在于 CUST1 的 `vrf_id` 下，nexthop 指向 push NH |
| **E2** | PE1 出方向抓包 | Ethernet | `[L_t][L_v][IP]` 两层标签，外层 S=0、内层 S=1 |
| **E3** | P 出方向抓包 / ILM | `show mpls ilm 0 label <L_t> detail` | 默认 implicit-null 时出方向只剩 `[L_v][IP]`；explicit-null 时出方向为 `[0][L_v][IP]` |
| **E4** | PE2 | `show mpls ilm 0 label <L_v> detail` | `type=L3VPN`、`id_type=VRF`、`vrf_id` 正确、`innerPacketLookup=1` |
| **E5** | PE2 | `show ipuc <vrf_id> 10.2.2.0 24 detail` | 前缀在 CUST1 `vrf_id` 下，下一跳指向 CE2 |
| **E6** | PE2→CE2 抓包 / PC2 | 抓包 + ping | PE2 发出的是无标签 IP；PC2 100% 收到 |

> 这六个点里，**E1/E4/E5 是芯片表项证据，E2/E3/E6 是报文证据**。只做 ping 不做表项核对，无法区分"硬件转发"和"CPU 代转/内核转发"；只做表项核对不做抓包，无法证明标签栈真的按预期压/弹。

### 1.5 当前实现的能力边界（先看这一节再选场景）

| 能力 | 状态 | 说明 |
|---|---|---|
| IPv4 VPNv4 | ✅ 已验证 | S1/S2 为基础场景 |
| 多 VRF / 多客户 | ✅ 代码支持 | `(vrf_id, label)` 双键缓存，见 S4 |
| VRF 路由带两层标签 PUSH | ✅ 代码支持 | `hw_l3_route_add_vrf` 最多 3 层，L3VPN 典型 2 层 |
| Egress L3VPN ILM（内层查 VRF） | ✅ 已验证 | `hw_mpls_lsp_add_vrf`，`innerPacketLookup=1` |
| PHP（implicit-null） | ✅ 已验证 | hw_mpls.c 的 POP/PHP 出向 nexthop |
| explicit-null（label 0） | ⚠️ 需按 S7 验证 | 外层标签为 0 时，PE2 需先处理 label 0 再进 VPN ILM |
| PE-CE RIPv2 | ✅ | S1 |
| PE-CE eBGP | ✅ | S2 |
| PE-CE 静态路由 | ✅（需 staticd 运行） | S3 |
| Underlay OSPF | ✅（LDP 与 IGP 解耦） | S6 |
| IPv6 VPNv6 | ⚠️ 条件项 | dplane/hw_l3 有 v6 分支，需先做 S10 的预检 |
| Inter-AS Option A | ⚠️ 扩展项 | 需 4 台设备/背靠背 VRF |
| Inter-AS Option B/C | ⚠️ 条件项 | 依赖 FRR VPNv4 重分配标签 / BGP-LU 特性，需先确认 |
| EVPN / VXLAN | ❌ 不在本文范围 | `vni` 与 MPLS L3VPN 无关，不要混配 |

---

## 2. 通用实验环境

### 2.1 基础拓扑

```
  CUST1 站点1                                                                          CUST1 站点2
 ┌──────┐  ┌──────┐ vlanif100 ┌─────────┐ vlanif10 ┌───────┐ vlanif20 ┌─────────┐ vlanif200 ┌──────┐  ┌──────┐
 │ PC1  │──┤ CE1  │═══════════┤   PE1   │──────────┤   P   │──────────┤   PE2   │═══════════┤ CE2  ├──┤ PC2  │
 └──────┘  └──────┘192.168.100│  (CTC)  │192.168.12│ (CTC) │192.168.23│  (CTC)  │192.168.200└──────┘  └──────┘
 10.1.1.0/24        .0/24  lo 1.1.1.1/32  .0/24  lo 2.2.2.2/32  .0/24  lo 3.3.3.3/32  .0/24       10.2.2.0/24

  ═══  PE-CE 客户域：只在各 PE 的 CUST1 VRF 里跑 RIPv2，客户路由不进核心
  ───  Underlay 核心域：RIPv2 只通告 P2P 网段 + lo/32；LDP 只在 vlanif10 / vlanif20 上跑
  MP-BGP VPNv4（eBGP 65001 ↔ 65002）叠加在 PE1 ↔ PE2 之上，承载 CUST1 客户路由，RT = 65001:100
```

### 2.2 地址与角色规划

| 设备 | 角色 | 接口 | 地址 | VRF | 所属域 |
|---|---|---|---|---|---|
| PC1 | 客户主机 | 网卡 | 10.1.1.10/24，GW 10.1.1.5 | — | CUST1 |
| CE1 | 客户边缘（Linux/FRR） | vlanif101 | 10.1.1.5/24 | default | CUST1 |
| | | vlanif100 | 192.168.100.5/24 | default | PE-CE |
| **PE1** | PE（CTC） | lo | 1.1.1.1/32 | default | underlay |
| | | vlanif10 | 192.168.12.1/24 | default | underlay |
| | | vlanif100 | 192.168.100.1/24 | **CUST1** | PE-CE |
| | | vlanif1 | 192.168.254.1/24 | default | 管理 |
| **P** | P（CTC，被测转发节点） | lo | 2.2.2.2/32 | default | underlay |
| | | vlanif10 | 192.168.12.2/24 | default | underlay |
| | | vlanif20 | 192.168.23.2/24 | default | underlay |
| **PE2** | PE（CTC） | lo | 3.3.3.3/32 | default | underlay |
| | | vlanif20 | 192.168.23.3/24 | default | underlay |
| | | vlanif200 | 192.168.200.3/24 | **CUST1** | PE-CE |
| | | vlanif1 | 192.168.254.3/24 | default | 管理 |
| CE2 | 客户边缘（Linux/FRR） | vlanif200 | 192.168.200.5/24 | default | PE-CE |
| | | vlanif201 | 10.2.2.5/24 | default | CUST1 |
| PC2 | 客户主机 | 网卡 | 10.2.2.10/24，GW 10.2.2.5 | — | CUST1 |

> CE 地址以现场实际为准；本文按用户给定配置使用 CE1 `.5`、CE2 `.5`。若 CE 侧是 `.2`，全文替换即可。

### 2.3 VLAN / 端口规划

| 链路 | VLAN | 两端接口 | 说明 |
|---|---|---|---|
| PC1—CE1 | 101 | CE1 access / PC1 网卡 | 客户站点 1 |
| CE1—PE1 | 100 | CE1 vlanif100 / PE1 vlanif100 | PE-CE 互联，**只进 CUST1，不进核心** |
| PE1—P | 10 | PE1 vlanif10 / P vlanif10 | underlay + LDP |
| P—PE2 | 20 | P vlanif20 / PE2 vlanif20 | underlay + LDP |
| PE2—CE2 | 200 | PE2 vlanif200 / CE2 vlanif200 | PE-CE 互联，**只进 CUST1，不进核心** |
| CE2—PC2 | 201 | CE2 vlanif201 / PC2 网卡 | 客户站点 2 |

**接入方式**：PC 发 untagged 帧，CE 侧对应 VLAN 的三层口终结；PE 侧客户口进 VRF，核心口留在 default VRF。

### 2.4 协议域纪律（L3VPN 最重要的一张表）

| 域 | 跑什么 | 生效范围 | 绝不能出现什么 |
|---|---|---|---|
| Underlay IGP | RIPv2（或 OSPF） | 只有 P2P 互联段 + 三个 lo/32 | 任何客户网段（10.1.1.0/24、10.2.2.0/24、192.168.100.0/24、192.168.200.0/24） |
| Underlay LSP | LDP | 只有 PE-P、P-PE2 互联口 | 客户口（vlanif100 / vlanif200） |
| Overlay | MP-BGP VPNv4 | 只有 PE1 ↔ PE2（P 不参与） | 客户侧接口 |
| PE-CE | RIPv2 / eBGP / 静态 | 各 PE 的 CUST1 VRF 实例 | 核心网段 |
| VPN 标识 | RD 各端不同；RT 两端相同 | 只有客户网段 | — |

> **判断是否配错的最快方法**：在 P 上执行 `show ip route` 和 `show mpls table`。如果出现任何 10.x 或 192.168.100.x / 192.168.200.x，说明客户路由泄进了核心，L3VPN 隔离已被破坏，先修配置再谈转发。

### 2.5 设备复用关系

| 场景 | PE1 | P | PE2 | 说明 |
|---|---|---|---|---|
| S1/S2/S3 | CTC #1 | CTC #2 | CTC #3 | 基础三角/直线 |
| S4 多 VRF | CTC #1 | CTC #2 | CTC #3 | 每台加 CUST2 的 vlanif/VRF |
| S5 Hub-Spoke | CTC #1（Hub，1.1.1.1） | CTC #2（原 P，升级为 PE3/Spoke2，2.2.2.2） | CTC #3（Spoke1，3.3.3.3） | P 升级为 PE，需加 VRF + BGP；Spoke 间不互通 |
| S6 OSPF | CTC #1 | CTC #2 | CTC #3 | 只换 underlay IGP |
| S8 双归属 | CTC #1 | CTC #2（第二个 PE） | CTC #3 | P 同时承担 PE 角色 |
| S9 ECMP | CTC #1 | CTC #2 | CTC #3 | 增加一条核心链路 |

### 2.6 环境搭建检查清单

- [ ] 三台 CTC 设备芯片/SDK 正常，`CTC_CLI` 可用；`show mpls status` 正常
- [ ] 三台设备 FRR 版本一致（`show version`），HSL 模块已加载
- [ ] vlanif10/20/100/200 已创建且 up（系统层/平台工具），zebra 已感知
- [ ] PE1 的 vlanif100、PE2 的 vlanif200 已挂到 CUST1（`ip link set ... master CUST1`，或 netns 后端用 `interface IFNAME vrf VRF`）
- [ ] CE1/CE2 的 vlanif 已创建，`ip forwarding` 已开
- [ ] PC1/PC2 静态 IP + 网关配置完成
- [ ] 抓包工具：PE1 出方向、P 两个方向、PE2 入/出方向各一路 tcpdump；PC 侧可抓包
- [ ] 每台设备先 ping 通直连对端，确保 ARP 已解析
- [ ] 保存三台设备的 `show version`、`show vrf`、`show running-config` 基线

---

## 3. 配置总则：L3VPN 四段接力

### 3.1 四段接力模型

一条客户路由从 CE1 到 CE2，必须经过四段"接力"，缺一段端到端就不通：

```
① PE-CE 本地 → BGP
   CE1 --RIP/eBGP/static--> PE1 CUST1 RIB
   PE1: router bgp 65001 vrf CUST1 / redistribute rip（或 neighbor ... activate）

② VRF BGP → VPNv4（本端导出）
   PE1: export vpn
        + rd vpn export 65001:100
        + route-target vpn export 65001:100
        + label vpn export auto
   → VPNv4 表: [65001:100] 10.1.1.0/24, RT:65001:100, Label L1

③ VPNv4 → VRF BGP（对端导入）
   PE2: import vpn
        + route-target vpn import 65001:100
   → PE2 CUST1 RIB: 10.1.1.0/24

④ BGP → PE-CE 本地
   PE2: router rip vrf CUST1 / redistribute bgp metric 2
        （或 eBGP 邻居直接通告；或 redistribute static）
   → CE2 学到 10.1.1.0/24
```

> **换 PE-CE 协议时，只动第 ① 段和第 ④ 段。** 第 ②③ 段（`export/import vpn`、RD、RT、`label vpn export`）一个字符都不该变。如果换了 PE-CE 协议后第 ②③ 段要跟着改，说明对模型的理解还不到位。

### 3.2 VRF 绑定：先确认后端，再选写法

FRR 10.6.1 有两种 VRF 后端，绑定写法不同：

| 后端 | 判断方法 | 绑定写法 |
|---|---|---|
| **netns-based** | `show vrf` 第一行有 `netns-based vrfs` | `interface vlanif100 vrf CUST1` |
| **Linux VRF / vrf-lite**（默认） | `show vrf` 第一行没有上述字样 | 平台侧 `ip link set vlanif100 master CUST1`；CLI 里仍写 `interface vlanif100` |

> **最常见的致命错误**：在 vrf-lite 后端下，在接口子命令里写 `vrf CUST1`。FRR 10.6.1 没有"接口下配 vrf"这条命令，配置不会报错但接口仍在 default VRF，结果是 `show ip route vrf CUST1` 完全为空（连 `C` 都没有）。正确写法见上表。

**VRF 绑定验收（必须第一步做）**

| 检查 | 命令 | 期望 |
|---|---|---|
| VRF 存在 | `show vrf` | `vrf CUST1 id <n> table 100 (configured)`，不是 `inactive` |
| 接口归属 | `show interface vrf CUST1` | `vlanif100`（PE1）/ `vlanif200`（PE2）出现在列表里 |
| 直连路由 | `show ip route vrf CUST1` | 至少有 `C 192.168.100.0/24`、`L 192.168.100.1/32` |
| VRF ID | `show vrf` 的 `id` 列 | 记下这个数字，后面查芯片要用它，**不要用 table 100** |

### 3.3 RD / RT / label 速查

| 命令 | 方向 | 作用 | 关键约束 |
|---|---|---|---|
| `redistribute rip`（VRF 实例内） | 本地入 | 把 VRF RIB 的 RIP 路由灌进 BGP | 没有它 BGP 手里没货 |
| `label vpn export auto` | 本端出 | 为本 VRF 自动分配 VPN 标签 | FRR 默认是**每 VRF 一个标签**（`tovpn_label`），同 VRF 多前缀共用一个标签 |
| `rd vpn export 65001:100` | 本端出 | 导出前缀加 RD | 两端必须不同 |
| `route-target vpn export 65001:100` | 本端出 | 导出路由打 RT | 两端必须相同 |
| `route-target vpn import 65001:100` | 本端入 | 只允许匹配 RT 的路由进入本 VRF | 必须等于对端 export |
| `export vpn` | 总开关出 | 打通 VRF → VPNv4 | 缺它本地路由永远出不去 |
| `import vpn` | 总开关入 | 打通 VPNv4 → VRF | 缺它对端路由永远进不来 |

**为什么 PE1/PE2 的 RD 不同、RT 相同：**

- RD 是"身份证号"：两端站点可能用相同地址空间（例如都是 10.1.1.0/24），RD 不同才能让两条路由在 VPNv4 表里成为两条独立 NLRI，而不是被 BGP 当成同一条路由的主备。
- RT 是"群号"：两端都用 `65001:100`，才表示属于同一个 VPN。RT 里的 `65001` 只是约定编号，不代表只有 PE1 能用。

### 3.4 RFC 8212：eBGP 默认需要策略

FRR 10.6.1 默认 `bgp ebgp-requires-policy = true`。任何 eBGP 会话（PE1↔PE2、PE↔CE）如果没有 route-map / prefix-list / filter-list / distribute-list，**出方向不发、入方向丢弃**，日志会刷：

```
EBGP outbound policy not properly setup
```

**必须显式关闭：**

```
router bgp 65001
 no bgp ebgp-requires-policy
```

> ⚠️ 这是 **per-BGP-instance** 的标志。在全局 `router bgp 65001` 下配过，**不等于** `router bgp 65001 vrf CUST1` 下也配了。PE-CE 用 eBGP 时，VRF 实例下要**再配一遍**。

### 3.5 Underlay / Overlay / PE-CE 的配置骨架

```
! ========== zebra / VRF ==========
ip link add CUST1 type vrf table 100          ! 系统层
ip link set CUST1 up                          ! 系统层
ip link set vlanif100 master CUST1            ! vrf-lite 后端
! netns 后端改为：interface vlanif100 vrf CUST1

vrf CUST1
 exit-vrf
!
interface lo
 ip address 1.1.1.1/32
!
interface vlanif10
 ip address 192.168.12.1/24
!
interface vlanif100
 ip address 192.168.100.1/24
!
! ========== underlay RIPv2 ==========
router rip
 version 2
 network 1.1.1.1/32
 network 192.168.12.0/24
!
! ========== PE-CE RIPv2（CUST1 独立实例）==========
router rip vrf CUST1
 version 2
 network 192.168.100.0/24
 redistribute bgp metric 2
!
! ========== LDP：只在核心口 ==========
mpls ldp
 router-id 1.1.1.1
 address-family ipv4
  discovery transport-address 1.1.1.1
  interface vlanif10
  exit-address-family
 exit
!
! ========== MP-BGP：全局实例（VPNv4）==========
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 3.3.3.3 remote-as 65002
 neighbor 3.3.3.3 update-source lo
 neighbor 3.3.3.3 ebgp-multihop 3
 !
 address-family ipv4 vpn
  neighbor 3.3.3.3 activate
  neighbor 3.3.3.3 next-hop-self
 exit-address-family
!
! ========== MP-BGP：VRF 实例（VPN 侧）==========
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
!
```

### 3.6 配置落地顺序

| 步骤 | 做什么 | 检查点 | 不通过怎么办 |
|---|---|---|---|
| 1 | 系统层建 VRF、vlanif、绑接口 | `show interface vrf CUST1` 有接口 | 查后端类型/绑定方式 |
| 2 | 配 underlay 接口地址 + RIP | `show ip route` 有对端 lo/32 | 查 RIP network、接口 up |
| 3 | 配 LDP | `show mpls ldp neighbor` OPERATIONAL | 查 transport-address、`interface` 是否在 AF 下 |
| 4 | 配 MP-BGP 全局 + VPNv4 | `show bgp ipv4 vpn summary` Established | 查 ebgp-requires-policy、multihop、update-source |
| 5 | 配 VRF BGP / PE-CE | `show bgp vrf CUST1 ipv4 unicast` 有路由 | 查四段接力 |
| 6 | 配 PE-CE IGP | PE/CE 双向学到路由 | 查 VRF 绑定、redistribute |
| 7 | 数据面验证 | ping + 抓包 + 芯片表项 | 按第 18 章分层定位 |

---

## 4. 综合场景矩阵

| 场景 | 名称 | 核心变化 | 设备数 | 优先级 | 关键验证 |
|---|---|---|---|---|---|
| **S1** | 基础场景：PE-CE RIPv2 | 无（基线） | 3 CTC + 2 CE + 2 PC | **P0** | 端到端 + 两层标签 + L3VPN ILM |
| **S2** | PE-CE eBGP | 第①④段换 eBGP | 3 CTC + 2 CE + 2 PC | **P0** | VRF 实例 eBGP + 第②③段不变 |
| **S3** | PE-CE 静态路由 | 第①④段换静态 | 3 CTC + 2 CE + 2 PC | P1 | staticd + redistribute static |
| **S4** | 多 VRF / 多客户 / 地址重叠 | 增加 CUST2 | 3 CTC + 4 CE/PC | **P0** | `(vrf_id,label)` 双键、标签隔离、无泄漏 |
| **S5** | Hub-and-Spoke（RT 控制） | P 升级为 PE3；RT 分 Hub/Spoke | 3 CTC + 3 CE | P1 | RT 导入导出策略、Spoke 间不互通 |
| **S6** | Underlay OSPF 变体 | RIP → OSPF | 3 CTC + 2 CE | P1 | L3VPN 与 IGP 解耦 |
| **S7** | explicit-null 传输标签 | PE2 通告 label 0 | 3 CTC + 2 CE | **P0** | `[0][VPN][IP]` 处理 |
| **S8** | CE 双归属 / PE 冗余 | P 兼任第二个 PE | 3 CTC + 2 CE | P2 | 主备切换、标签/路由收敛 |
| **S9** | 核心 ECMP | 增加一条核心链路 | 3 CTC + 2 CE | P2 | IPUC ECMP + 多 push NH |
| **S10** | IPv6 VPNv6 | VPNv4 → VPNv6 | 3 CTC + 2 CE | P2（条件） | v6 VRF 路由 + v6 ILM |
| **S11** | 跨域 Option A/B/C | 多 AS | ≥4 CTC | P2（条件） | 见第 15 章前置确认 |
| **S12** | 路由反射器 | P 作 RR，PE 同 AS | 3 CTC + 2 CE | P2 | RR 反射 VPNv4 |

> **建议执行顺序**：S1 → S2 → S4 → S7 → S6 → S3 → S5 → S8 → S9 → S10 → S11 → S12。P0 全绿后再做 P1/P2。

---

## 5. 场景 S1：基础场景（PE-CE RIPv2）

### 5.1 目的与拓扑

验证最基础的 L3VPN over MPLS：underlay RIP + LDP，PE-CE RIP，PE1↔PE2 eBGP VPNv4，PC1↔PC2 端到端走 CTC 硬件转发。本场景是后续所有场景的基线。

拓扑见 2.1，地址见 2.2。

### 5.2 完整配置

#### PE1

```
hostname PE1
!
! ========== 系统层（在 shell 执行，非 FRR CLI）==========
! ip link add CUST1 type vrf table 100
! ip link set CUST1 up
! ip link set vlanif100 master CUST1
!
! ========== zebra ==========
vrf CUST1
 exit-vrf
!
interface lo
 ip address 1.1.1.1/32
exit
!
interface vlanif1
 ip address 192.168.254.1/24
exit
!
interface vlanif10
 ip address 192.168.12.1/24
exit
!
interface vlanif100
 ip address 192.168.100.1/24
exit
!
! ========== ripd：underlay ==========
! 只通告 PE-P 互联 + loopback；客户网段不在这里
router rip
 version 2
 network 1.1.1.1/32
 network 192.168.12.0/24
exit
!
! ========== ripd：PE-CE（CUST1 独立实例）==========
router rip vrf CUST1
 version 2
 network 192.168.100.0/24
 redistribute bgp metric 2
exit
!
! ========== ldpd：只在互联口 ==========
mpls ldp
 router-id 1.1.1.1
 address-family ipv4
  discovery transport-address 1.1.1.1
  interface vlanif10
  exit-address-family
 exit
!
! ========== bgpd：全局实例（MP-BGP VPNv4）==========
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 3.3.3.3 remote-as 65002
 neighbor 3.3.3.3 update-source lo
 neighbor 3.3.3.3 ebgp-multihop 3
 !
 address-family ipv4 vpn
  neighbor 3.3.3.3 activate
  neighbor 3.3.3.3 next-hop-self
 exit-address-family
exit
!
! ========== bgpd：VRF 实例（VPN 侧）==========
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
end
```

> `redistribute connected` 会把 PE-CE 互联段 192.168.100.0/24 也导出到 VPN。它不是 L3VPN 转发的必要条件；保留便于跨站点 ping PE-CE 接口排查，不保留也不影响 10.x 站点互通。

#### P

```
hostname P
!
interface lo
 ip address 2.2.2.2/32
exit
!
interface vlanif10
 ip address 192.168.12.2/24
exit
!
interface vlanif20
 ip address 192.168.23.2/24
exit
!
router rip
 version 2
 network 192.168.12.0/24
 network 192.168.23.0/24
 network 2.2.2.2/32
exit
!
mpls ldp
 router-id 2.2.2.2
 address-family ipv4
  discovery transport-address 2.2.2.2
  interface vlanif10
  interface vlanif20
  exit-address-family
 exit
!
end
```

> P **不需要** VRF、BGP、VPNv4。这正是 L3VPN 的价值：核心只看到 lo/32 和互联段，客户路由在 P 上完全不可见。

#### PE2

```
hostname PE2
!
! ========== 系统层（在 shell 执行，非 FRR CLI）==========
! ip link add CUST1 type vrf table 100
! ip link set CUST1 up
! ip link set vlanif200 master CUST1
!
! ========== zebra ==========
vrf CUST1
 exit-vrf
!
interface lo
 ip address 3.3.3.3/32
exit
!
interface vlanif1
 ip address 192.168.254.3/24
exit
!
interface vlanif20
 ip address 192.168.23.3/24
exit
!
interface vlanif200
 ip address 192.168.200.3/24
exit
!
! ========== ripd：underlay ==========
router rip
 version 2
 network 3.3.3.3/32
 network 192.168.23.0/24
exit
!
! ========== ripd：PE-CE（CUST1 独立实例）==========
router rip vrf CUST1
 version 2
 network 192.168.200.0/24
 redistribute bgp metric 2
exit
!
! ========== ldpd ==========
mpls ldp
 router-id 3.3.3.3
 address-family ipv4
  discovery transport-address 3.3.3.3
  interface vlanif20
  exit-address-family
 exit
!
! ========== bgpd：全局实例（MP-BGP VPNv4）==========
router bgp 65002
 bgp router-id 3.3.3.3
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 1.1.1.1 remote-as 65001
 neighbor 1.1.1.1 update-source lo
 neighbor 1.1.1.1 ebgp-multihop 3
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
  neighbor 1.1.1.1 next-hop-self
 exit-address-family
exit
!
! ========== bgpd：VRF 实例（VPN 侧）==========
! RD 用 65002:100（与 PE1 的 65001:100 不同）
! RT 两端都用 65001:100（相同 → 同一个 VPN）
router bgp 65002 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute rip
  label vpn export auto
  rd vpn export 65002:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
end
```

#### CE1

```
hostname CE1
!
interface vlanif100
 ip address 192.168.100.5/24
exit
!
interface vlanif101
 ip address 10.1.1.5/24
exit
!
router rip
 version 2
 network 192.168.100.0/24
 network 10.1.1.0/24
exit
!
line vty
!
end
```

#### CE2

```
hostname CE2
!
interface vlanif200
 ip address 192.168.200.5/24
exit
!
interface vlanif201
 ip address 10.2.2.5/24
exit
!
router rip
 version 2
 network 192.168.200.0/24
 network 10.2.2.0/24
exit
!
line vty
!
end
```

> **与原配置的差异说明**
>
> 1. 原 CE1/CE2 上的 `ip route 10.2.2.0/24 192.168.100.1`、`ip route 192.168.200.0/24 ...` 两条静态路由在 S1 中移除。RIP 正常后，CE 会通过 PE 的 `redistribute bgp` 学到对端站点；保留静态路由会掩盖 RIP 故障，且管理距离不同可能造成选路与预期不一致。若现场必须保留，请在报告中注明。
> 2. 原 PE2 的 `label local advertise explicit-null` 在 S1 基线中移除，使用 LDP 默认的 implicit-null。explicit-null 是独立路径，放在 S7 单独验证。
> 3. PE1 的 `redistribute connected` 会把 PE-CE 互联段（192.168.100.0/24）也导出到 VPN 并最终进入 CE2 的 RIP 表；PE2 的 `redistribute rip` 也可能把 192.168.200.0/24 导出到 CE1。**这些是客户侧互联段，不是核心网段**；核心网段（192.168.12.0/24、192.168.23.0/24、lo/32）仍然不能出现在 CE 表中。若客户要求 CE 只能看到站点网段，删除 `redistribute connected`，并确保 CE 不把互联段再通告回来。

#### PC1 / PC2

| PC | IP | 掩码 | 默认网关 |
|---|---|---|---|
| PC1 | 10.1.1.10 | 255.255.255.0 | 10.1.1.5 |
| PC2 | 10.2.2.10 | 255.255.255.0 | 10.2.2.5 |

### 5.3 分阶段生效检查

按顺序做，前一步不全绿不要往下走。

#### 阶段 A：VRF 与接口

| 检查 | 命令 | 期望 |
|---|---|---|
| VRF | `PE1# show vrf` | `vrf CUST1 id 8 table 100 (configured)`（id 以实际为准） |
| 接口归属 | `PE1# show interface vrf CUST1` | 有 `vlanif100` |
| 直连 | `PE1# show ip route vrf CUST1` | `C 192.168.100.0/24`、`L 192.168.100.1/32` |
| RIP 实例 | `PE1# show ip rip vrf CUST1 status` | Interface 表含 vlanif100，Send/Recv 2 |

PE2 侧对 `vlanif200` / `192.168.200.0/24` 做同样检查。

#### 阶段 B：Underlay（RIP + LDP）

| 检查 | 命令 | 期望 |
|---|---|---|
| underlay 路由 | `PE1# show ip route` | 有 `R 2.2.2.2/32`、`R 3.3.3.3/32`、`R 192.168.23.0/24` |
| LDP 会话 | `PE1# show mpls ldp neighbor` | 1 个邻居 2.2.2.2，Transport 为 lo，State OPERATIONAL |
| LDP 绑定 | `PE1# show mpls ldp binding` | 有 2.2.2.2/32、3.3.3.3/32 的 label mapping |
| MPLS 入表 | `PE1# show mpls table` | 有 FEC 3.3.3.3/32 的 ILM，出标签非 0，下一跳 192.168.12.2 |
| P 的标签 | `P# show mpls ldp binding` | 同时有 1.1.1.1/32、3.3.3.3/32 的 mapping |

#### 阶段 C：Overlay（VPNv4）

| 检查 | 命令 | 期望 |
|---|---|---|
| VPNv4 邻居 | `PE1# show bgp ipv4 vpn summary` | neighbor 3.3.3.3，State/PfxRcd 是数字且 ≥1 |
| 无策略告警 | `PE1# show bgp ipv4 vpn neighbors 3.3.3.3` | 无 `EBGP outbound policy not properly setup` |
| 导出 | `PE1# show bgp ipv4 vpn neighbors 3.3.3.3 advertised-routes` | ≥1 条（10.1.1.0/24） |

#### 阶段 D：VPN 路由交换（最关键）

| 检查 | 命令 | 期望 |
|---|---|---|
| PE1 CUST1 RIB | `PE1# show ip route vrf CUST1` | 同时有 `C 192.168.100.0/24`、`R 10.1.1.0/24`、**`B 10.2.2.0/24`** |
| PE1 VRF BGP | `PE1# show bgp vrf CUST1 ipv4 unicast` | `10.1.1.0/24`（本地）+ `10.2.2.0/24`（导入） |
| VPNv4 表 | `PE1# show bgp ipv4 vpn` | 两条前缀：`RD 65001:100 / 10.1.1.0/24`、`RD 65002:100 / 10.2.2.0/24`，各带 MPLS Label |
| PE2 CUST1 RIB | `PE2# show ip route vrf CUST1` | 对称：`R 10.2.2.0/24` + `B 10.1.1.0/24` |
| CE1 路由 | `CE1# show ip rip` | 有 `10.2.2.0/24`，**没有** 192.168.12.0/24 / 192.168.23.0/24；可能额外看到对端 PE-CE 互联段（如 192.168.200.0/24），见 5.2 差异说明 3 |
| CE2 路由 | `CE2# show ip rip` | 有 `10.1.1.0/24`，**没有**核心网段；可能额外看到 192.168.100.0/24，见 5.2 差异说明 3 |

### 5.4 测试用例

#### S1-TC01：端到端连通与路径

| 项 | 内容 |
|---|---|
| 目的 | 验证 PC1↔PC2 双向业务通，路径符合 L3VPN 设计 |
| 前置 | 阶段 A–D 全绿；各设备直连 ARP 已解析 |
| 步骤 | ① `PC1> ping 10.2.2.10 -n 100 -l 1400`；② `PC2> ping 10.1.1.10 -n 100 -l 1400`；③ `PC1> tracert -d 10.2.2.10` |
| 预期 | 双向 ping 100% 通；tracert 显示 PC1→CE1→PE1→P→PE2→CE2→PC2；MPLS 域内 TTL 行为按标签模式（PHP 时 P 可能不显示） |
| 判定 | 双向 100% 通 = PASS；单向通 = 查反向标签栈/RT；不通 = 回阶段 D |
| 证据 | ping 统计、tracert 输出、PC 侧抓包 |

#### S1-TC02：PE1 出方向两层标签

| 项 | 内容 |
|---|---|
| 目的 | 证明 ingress PE 按 BGP 路由的标签栈压了两层标签 |
| 前置 | S1-TC01 通过 |
| 步骤 | ① `PE1# show ip route vrf CUST1 10.2.2.0/24 detail` 记录 nexthop 和 label；② PE1 vlanif10 出方向 tcpdump；③ `PC1> ping 10.2.2.10 -n 20` |
| 预期 | 抓包为 `[L_t][L_v][IP]`：外层 L_t = LDP 为 3.3.3.3/32 分配的标签；内层 L_v = PE2 的 VPN 标签；外层 S=0、内层 S=1；TTL 按实现递减 |
| 判定 | 两层标签且顺序正确 = PASS；只有一层 = 查 dplane `label_stack_depth`/BGP nexthop 标签；外层内层颠倒 = 查 `hw_mpls_nh_create_key` 的 labels 顺序 |
| 证据 | pcap + `show ip route ... detail` + `show bgp vrf CUST1 ipv4 unicast 10.2.2.0/24` |

> 实验室参考值：外层 16、内层 144（实际值以设备为准）。`[16][144][IP]` 是 P 入方向应看到的报文。

#### S1-TC03：P 的 PHP 行为（默认 implicit-null）

| 项 | 内容 |
|---|---|
| 目的 | 验证 P 作为倒数第二跳只弹外层 transport 标签，保留 VPN 标签和 IP |
| 前置 | S1-TC01 通过；PE2 未配 `label local advertise explicit-null` |
| 步骤 | ① P 的 vlanif20 出方向 tcpdump；② `P# show mpls table` 查 3.3.3.3/32；③ `P# show mpls ilm 0 label <L_t> detail` |
| 预期 | P 出方向为 `[L_v][IP]`，只有一层标签，S=1；`show mpls table` 中 3.3.3.3/32 出标签为 **implicit-null**；ILM 行为为 POP/PHP，不是 decap=1 |
| 判定 | 出方向一层 VPN 标签 + implicit-null = PASS；仍带两层 = PE2 通告了真实标签（改回 implicit-null）；出方向无标签 IP = PHP 把 VPN 标签也弹了（严重 bug，查 hw_mpls.c PHP NH） |
| 证据 | pcap + `show mpls table` + ILM detail + dmesg |

> 该用例是 hw_mpls.c PHP 修正的核心回归点。重点看 `[16][144][IP]` → `[144][IP]`，不是 `[IP]`，也不是 `[16][144][IP]` 原样转发。

#### S1-TC04：PE1 ingress 硬件落表（T2）

| 项 | 内容 |
|---|---|
| 目的 | 证明 VPN 路由在 PE1 以 IPUC + push nexthop 形式落到芯片，而不是只在内核/CPU |
| 前置 | S1-TC01 通过 |
| 步骤 | ① `PE1# show vrf` 记录 CUST1 的 `id`（如 8）；② `PE1# show ipuc 8 10.2.2.0 24 detail`；③ 从 detail 取 nexthop id；④ `show nexthop <nhid> detail`；⑤ `show nexthop mpls-tunnel <tunnel-id> detail` |
| 预期 | ① IPUC 命中 10.2.2.0/24，`vrfId=8`；② nexthop 为 MPLS push 类型；③ tunnel label = 外层 L_t；④ push label = 内层 L_v；⑤ tunnel 的 oif/VLAN/MAC 指向 vlanif10 和 P 的 MAC |
| 判定 | IPUC + push NH 都存在且标签值正确 = PASS；IPUC 查不到 = 查 dplane `HAL_MSG_RT_VRF_ADD`/hw_l3；只有内核路由 = dplane 未下发或 skip_kernel 逻辑异常 |
| 证据 | `show ipuc`、`show nexthop`、`show nexthop mpls-tunnel` 输出 |

#### S1-TC05：P 的外层标签 SWAP/PHP 硬件表项（T1）

| 项 | 内容 |
|---|---|
| 目的 | 证明 P 的 LDP ILM 与出向 nexthop 正确落表 |
| 步骤 | ① `P# show mpls ilm 0 label <L_t> detail`；② `P# show nexthop <nhid> detail`；③ 对照 PE2 的 LDP 通告 |
| 预期 | `<L_t>` ILM 存在，类型 NORMAL；默认 implicit-null 时出向为 PHP（pop 外层、继续转发）；`show nexthop` 的 oif/MAC 指向 vlanif20 / PE2 |
| 判定 | ILM + nexthop 完整 = PASS；ILM 缺失 = LDP 未建 FEC；nexthop 为 TOCPU = ARP 未解析（先 ping 对端） |
| 证据 | ILM/nexthop detail + pcap |

#### S1-TC06：PE2 egress VPN 标签 ILM（T3，核心证据）

| 项 | 内容 |
|---|---|
| 目的 | 证明 egress PE 把 VPN 标签装成 L3VPN 型 ILM，弹标签后按 `vrf_id` 查内层 IP |
| 步骤 | ① `PE2# show vrf` 记录 CUST1 `id`；② `PE2# show mpls table` 找到 VPN 标签 `<L_v>`；③ `PE2# show mpls ilm 0 label <L_v> detail`；④ 如有 `read` 诊断命令，读 `DsMpls` 对应表项；⑤ `show diag pkt-trace detail`（单条 ping 触发） |
| 预期 | ILM 类型 **L3VPN**；`id_type=VRF`；`vrf_id` 等于 `show vrf` 记录的 CUST1 id（实验室为 8）；`innerPacketLookup=1`（或 `inner_pkt_type=IP`）；`fidSelect=1`、`fidType=0`（fid 是 VRF）；nh_id 为 TOCPU 保留值（L3VPN ILM 的正常终态，不是 ARP 占位失败） |
| 判定 | L3VPN ILM + vrf_id 正确 = PASS；ILM 是 NORMAL = dplane 未识别 VRF LSP（查 `ctc_dplane_lsp_is_vrf`）；vrf_id=0 = VRF 设备口识别失败 |
| 证据 | ILM detail + `read DsMpls` raw word + pkt-trace |

> **重要**：L3VPN ILM 的 `nh_id=TOCPU` 是**正常终态**。芯片弹掉 VPN 标签后由 `innerPacketLookup` 查 VRF 路由表，出口由 T4 决定，ILM 本身不需要真实 nexthop。不要把它误判成"ARP 未解析"。

#### S1-TC07：PE2 内层 IP 查表（T4）

| 项 | 内容 |
|---|---|
| 目的 | 证明弹掉 VPN 标签后，内层 IPv4 在 CUST1 的 `vrf_id` 下命中路由，而不是落到默认丢弃 |
| 步骤 | ① `PE2# show ipuc 8 10.2.2.0 24 detail`；② `show ip route vrf CUST1 10.2.2.0/24`；③ pkt-trace 查内层 lookup 阶段；④ PE2 vlanif200 出方向 tcpdump |
| 预期 | IPUC 命中 10.2.2.0/24，`vrfId=8`，nexthop 指向 CE2（192.168.200.5）或直连；PE2 发出无标签 IP；PC2 收到 |
| 判定 | 内层路由命中且出包无标签 = PASS；pkt-trace 显示 `DsIpDa 1` / `IPE_DISCARD_DS_FWD_DESTID_DIS` = 内层查表落到了默认丢弃，检查 `vrfId` 与 `intf-vrfid` 是否一致、TCAM 里该 VRF 的前缀是否真的存在 |
| 证据 | `show ipuc`、`show ip route vrf`、pkt-trace、pcap |

> 若内层查表异常，优先确认三件事：① `show vrf` 的 id 与 ILM 的 `vrf_id` 一致；② `show ipuc <vrf_id> ...` 能查到前缀；③ `show diag pkt-trace detail` 里 `intf-vrfid` 与 VPN ILM 的 `fid` 一致。不要先怀疑标签，先怀疑"内层查表用了哪个 VRF"。

#### S1-TC08：VRF 隔离

| 项 | 内容 |
|---|---|
| 目的 | 验证客户路由不进核心、不同域互不可见 |
| 步骤 | ① `P# show ip route`、`P# show mpls table`；② `P# show bgp summary`（应无 BGP）；③ CE1/CE2 `show ip rip`；④ 在 P 上 `ping 10.1.1.10`（应不通） |
| 预期 | P 只有 lo/32 与互联段，无 10.x/192.168.100.x/192.168.200.x；P 无 BGP；CE 只看到本站点 + 对端站点网段；P ping 客户地址不通 |
| 判定 | 全部符合 = PASS；P 出现客户网段 = underlay RIP/LDP 漏配客户口 |
| 证据 | 各表输出 |

#### S1-TC09：ARP 占位与升级

| 项 | 内容 |
|---|---|
| 目的 | 验证 VRF 路由 / ILM 在 ARP 未解析时的占位与解析后升级 |
| 前置 | S1-TC01 通过 |
| 步骤 | ① `PE1# ip neigh del 192.168.12.2 dev vlanif10`（或不 ping 对端）；② 触发路由重建：`clear ip bgp *` 或 `clear mpls ldp neighbor 3.3.3.3`；③ 观察 `show ipuc 8 10.2.2.0 24 detail` 的 nexthop；④ `PE1# ping 192.168.12.2 -c 3`；⑤ 再查 nexthop |
| 预期 | 占位阶段：**IPUC 路由的 nexthop** 指向 TOCPU 占位项（注意：这是 ARP 未解析占位，与 S1-TC06 中 L3VPN ILM 的正常 TOCPU 终态不是一回事），报文上送 CPU 不丢弃；ARP 解析后自动升级为真实 nexthop，PC1→PC2 恢复 |
| 判定 | 占位→解析→升级恢复 = PASS；**IPUC 的** nexthop 一直停在 TOCPU = ARP 回调链路异常（查 `hw_l3_arp_resolved` / `hw_mpls_arp_resolved`） |
| 证据 | dmesg（占位/升级日志）、nexthop 前后对比、ping 恢复 |

#### S1-TC10：路由撤销与收敛

| 项 | 内容 |
|---|---|
| 目的 | 验证客户路由/标签撤销时表项正确删除，恢复后自动重建 |
| 步骤 | ① `CE2# interface vlanif200 shutdown`（或断 CE2-PE2 链路）；② 观察 PE1/PE2 的 `show ip route vrf CUST1`、`show mpls table`、SDK IPUC/ILM；③ PC1 ping PC2（应中断）；④ 恢复接口；⑤ 观察自动重建 |
| 预期 | PE2 撤销 10.1.1.0/24 的 VPN 通告 → PE1 删除 B 路由和 IPUC；PE2 删除本地路由；恢复后双向重建；dmesg 无 `stats detach failed ... ret=-31993` |
| 判定 | 撤销干净 + 恢复自动 = PASS；残留 stale IPUC/ILM = 查 DELETE 路径；-31993 刷屏 = stats 生命周期 bug |
| 证据 | 前后表项 dump、dmesg、ping 结果 |

#### S1-TC11：资源生命周期与泄漏

| 项 | 内容 |
|---|---|
| 目的 | 验证反复建立/拆除不泄漏芯片表项和 stats id |
| 步骤 | ① 记录基线：`show ipuc count`、`show mpls status`、`show nexthop status`、stats 数量；② 循环 `no router bgp 65001 vrf CUST1` / 重建 5 次，或 `no mpls ldp` / `mpls ldp` 5 次；③ 每次记录表项数量；④ 恢复业务并 ping |
| 预期 | 表项数量回到基线，不持续增长；无 stale L3VPN ILM；业务恢复 |
| 判定 | 无增长 + 业务恢复 = PASS；增长 = 泄漏，查 `hw_mpls_vrf_ilm_map_del` / `hw_l3_vrf_route` 的删除路径 |
| 证据 | 每轮计数表、dmesg |

#### S1-TC12：MTU 与大包

| 项 | 内容 |
|---|---|
| 目的 | 验证两层标签下的 MTU 行为 |
| 步骤 | ① 确认 PE/CE/PC 的 MTU；② `PC1> ping 10.2.2.10 -n 100 -l 1400`；③ 逐步增大到 `-l 1472` / `-l 1500`；④ 观察是否分片/丢弃 |
| 预期 | 载荷 1400 稳定通；载荷 1472 时，若 MTU=1500，加 8 字节标签后超限，预期不通或分片（取决于 DF 位）；MTU 调到 1526+ 后 1472 通 |
| 判定 | 1400 通、1472 行为可解释 = PASS；1400 也大量丢 = 查 MTU/分片配置或硬件 MTU |
| 证据 | ping 统计、接口 MTU、抓包 |

### 5.5 S1 通过标准

| 用例 | 判定条件 |
|---|---|
| S1-TC01 | 双向 ping 100% 通，tracert 路径合理 |
| S1-TC02 | PE1 出方向 `[L_t][L_v][IP]` 两层标签 |
| S1-TC03 | P 出方向 `[L_v][IP]` 一层标签（默认 implicit-null） |
| S1-TC04 | PE1 IPUC + push NH 落表，标签值正确 |
| S1-TC05 | P ILM + 出向 nexthop 正确 |
| S1-TC06 | PE2 L3VPN ILM：type=L3VPN、vrf_id 正确、innerPacketLookup=1 |
| S1-TC07 | PE2 内层 IP 在 CUST1 vrf_id 下命中，出包无标签 |
| S1-TC08 | 核心/客户隔离完整 |
| S1-TC09 | ARP 占位→升级→业务恢复 |
| S1-TC10 | 撤销干净、恢复自动、无 -31993 |
| S1-TC11 | 表项无泄漏 |
| S1-TC12 | MTU 行为可解释 |

> S1 全绿 = 基础 L3VPN 控制面到转发面、CTC 硬件转发验证完成。后续场景都以 S1 为基线做增量。

---

## 6. 场景 S2：PE-CE 改用 eBGP

### 6.1 目的与差异

**适用场景**：客户希望用 BGP 接入，或需要 route-map / community / MED / 多宿主选路等策略能力；也用于证明"四段接力"模型的第 ②③ 段与 PE-CE 协议无关。

| 接力段 | S1（RIP 版） | S2（eBGP 版） |
|---|---|---|
| ① PE-CE 本地 → BGP | `router rip vrf CUST1` + `redistribute rip` | VRF 实例下的 `neighbor ... remote-as` + `activate` |
| ② VRF BGP → VPNv4 | `export vpn` + RD + RT + `label vpn export auto` | **完全不变** |
| ③ VPNv4 → VRF BGP | `import vpn` + RT import | **完全不变** |
| ④ BGP → PE-CE 本地 | `router rip vrf CUST1` + `redistribute bgp metric 2` | 不需要 redistribute，eBGP 邻居直接通告 |

### 6.2 配置改动

#### CE1（删除 `router rip`，新增 `router bgp`）

```
hostname CE1
!
interface vlanif100
 ip address 192.168.100.5/24
exit
!
interface vlanif101
 ip address 10.1.1.5/24
exit
!
! 原 router rip 整段删除
router bgp 65101
 bgp router-id 10.1.1.5
 no bgp ebgp-requires-policy
 neighbor 192.168.100.1 remote-as 65001
 !
 address-family ipv4 unicast
  neighbor 192.168.100.1 activate
  network 192.168.100.0/24
  network 10.1.1.0/24
 exit-address-family
exit
!
end
```

#### CE2

```
hostname CE2
!
interface vlanif200
 ip address 192.168.200.5/24
exit
!
interface vlanif201
 ip address 10.2.2.5/24
exit
!
router bgp 65102
 bgp router-id 10.2.2.5
 no bgp ebgp-requires-policy
 neighbor 192.168.200.3 remote-as 65002
 !
 address-family ipv4 unicast
  neighbor 192.168.200.3 activate
  network 192.168.200.0/24
  network 10.2.2.0/24
 exit-address-family
exit
!
end
```

#### PE1（删除 `router rip vrf CUST1`，VRF BGP 换成 eBGP 邻居）

```
! ========== ripd：underlay 保留不动 ==========
router rip
 version 2
 network 1.1.1.1/32
 network 192.168.12.0/24
exit
!
! ========== bgpd：VRF 实例（PE-CE 侧，eBGP）==========
router bgp 65001 vrf CUST1
 bgp router-id 1.1.1.1
 ! 必须单独配：ebgp-requires-policy 是 per-BGP-instance 的标志
 no bgp ebgp-requires-policy
 neighbor 192.168.100.5 remote-as 65101
 !
 address-family ipv4 unicast
  neighbor 192.168.100.5 activate
  ! ↓↓↓ 以下 6 行与 S1 完全一致，一个字都不用改 ↓↓↓
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
```

> 不再需要 `redistribute rip`。如果 CUST1 里还有直连网段想导出，可保留 `redistribute connected`；但 eBGP 已经把 10.1.1.0/24 送进来了，通常不需要。
>
> `next-hop-self` **不需要**：PE-CE 是直连 eBGP，从 CE 学来的路由下一跳就是 CE 接口地址，可用。只有 PE-CE 跨多跳 / 配了 `ebgp-multihop` 时才需要。

#### PE2

```
router bgp 65002 vrf CUST1
 bgp router-id 3.3.3.3
 no bgp ebgp-requires-policy
 neighbor 192.168.200.5 remote-as 65102
 !
 address-family ipv4 unicast
  neighbor 192.168.200.5 activate
  label vpn export auto
  rd vpn export 65002:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
```

**Underlay 三件套（`router rip` + `mpls ldp` + 全局 `router bgp 65001/65002`）一律不动。**

### 6.3 测试用例

| 用例 | 目的 | 步骤 | 预期 / 判定 |
|---|---|---|---|
| S2-TC01 | PE-CE 邻居建立 | `PE1# show bgp vrf CUST1 ipv4 unicast summary`；`CE1# show bgp ipv4 unicast summary` | 两端 neighbor 192.168.100.x，State/PfxRcd 为数字且 ≥1 |
| S2-TC02 | 客户路由双向交换 | `PE1# show bgp vrf CUST1 ipv4 unicast` | 有 `10.1.1.0/24`（本地 eBGP）+ `10.2.2.0/24`（VPNv4 导入） |
| S2-TC03 | VPNv4 与 S1 一致 | `PE1# show bgp ipv4 vpn` | 两条前缀、RD/RT/标签与 S1 相同（标签值可不同） |
| S2-TC04 | 端到端 | `PC1> ping 10.2.2.10 -n 100 -l 1400` | 100% 通 |
| S2-TC05 | 硬件四证据 | 重做 S1-TC04/05/06/07 | E1–E6 全部成立；第 ②③ 段硬件表项与 S1 无差别 |
| S2-TC06 | CE 侧路由 | `CE1# show bgp ipv4 unicast` | `10.2.2.0/24`；本端 `192.168.100.0/24`（CE1 自己 `network` 的）；对端互联段 `192.168.200.0/24`（CE2 `network` 后经 VPN 传来）；**无核心网段**（192.168.12.0/24、192.168.23.0/24、lo/32） |
| S2-TC07 | 策略能力（可选） | 在 PE 的 VRF 邻居上配 prefix-list / route-map，过滤一个前缀 | 过滤前有、过滤后消失；另一方向不受影响 |
| S2-TC08 | 会话故障收敛 | `CE1# clear bgp 192.168.100.1` 或 shutdown CE1-PE1 链路 | PE1 撤销 CE1 路由 → PE2 撤销 → PC2 侧路由消失；恢复后自动重建 |

### 6.4 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| PE-CE 邻居卡 Idle/Active，`show bgp vrf CUST1 ... summary` 无输出 | VRF 实例没建起来：接口没真正进 CUST1，直连路由不在 CUST1 RIB | 回 3.2 做 VRF 绑定验收 |
| 邻居 Established 但 PfxRcd=0，日志刷 `EBGP outbound policy not properly setup` | VRF 实例漏了 `no bgp ebgp-requires-policy`（全局实例下配过不算） | 在 `router bgp <AS> vrf CUST1` 下再配一遍 |
| CE 侧 `show bgp ipv4 unicast` 为空 | CE 漏 `no bgp ebgp-requires-policy`，或 `network` 的前缀不在 CE 自己的 RIB 里 | CE 是普通 BGP 实例，同样受 RFC 8212 约束 |
| `show bgp vrf CUST1` 有路由，`show bgp ipv4 vpn` 没有 | 不是 PE-CE 侧问题，是第 ② 段 | 查 `export vpn`、`rd vpn export`、`label vpn export auto` |
| PE 学到了路由但 CE 学不到对端站点 | 第 ④ 段：eBGP 邻居没 activate，或方向反了 | `address-family ipv4 unicast` 下确认 `neighbor ... activate` |

### 6.5 S2 通过标准

- PE-CE eBGP 双向 Established；
- 客户路由双向交换；
- VPNv4 表与 S1 语义一致（RD/RT 不变）；
- 端到端 100% 通；
- E1–E6 硬件证据全部成立。

---

## 7. 场景 S3：PE-CE 静态路由

### 7.1 目的与适用

客户侧设备不支持动态协议，或只需要少量固定网段。PE-CE 不跑任何路由协议，用静态路由 + `redistribute static` 完成第 ① 段，用静态/直连完成第 ④ 段。第 ②③ 段仍不变。

### 7.2 配置改动

#### CE1（删除 RIP，写默认路由）

```
hostname CE1
!
interface vlanif100
 ip address 192.168.100.5/24
exit
!
interface vlanif101
 ip address 10.1.1.5/24
exit
!
ip route 0.0.0.0/0 192.168.100.1
!
ip forwarding
!
end
```

#### PE1（删除 `router rip vrf CUST1`，加静态路由）

```
! 静态路由进 CUST1 RIB（staticd 必须运行）
ip route 10.1.1.0/24 192.168.100.5 vrf CUST1
!
router bgp 65001 vrf CUST1
 bgp router-id 1.1.1.1
 !
 address-family ipv4 unicast
  redistribute static
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### PE2（对称）

```
ip route 10.2.2.0/24 192.168.200.5 vrf CUST1
!
router bgp 65002 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute static
  label vpn export auto
  rd vpn export 65002:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
```

> **方向不要配反**：
> - PE 上的静态路由指向 **CE**（为了把客户站点网段送进 VPN）；
> - CE 上的默认路由指向 **PE**（为了把去往远端站点的流量交给 PE）。
>
> PE2 学到 10.1.1.0/24 后，第 ④ 段由 `import vpn` 进入 CUST1 RIB；CE2 的默认路由把它送出去。PE2 不需要再写一条 `ip route 10.1.1.0/24 ...`，否则会与 BGP 路由冲突。

### 7.3 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S3-TC01 | `PE1# show ip route vrf CUST1 10.1.1.0/24` | `S` 路由，下一跳 192.168.100.5 |
| S3-TC02 | `PE1# show bgp vrf CUST1 ipv4 unicast` | 10.1.1.0/24 来自 static（`?` 或 `S` 标记） |
| S3-TC03 | `PE1# show bgp ipv4 vpn` | 有 RD 65001:100 / 10.1.1.0/24，带标签 |
| S3-TC04 | `PC1> ping 10.2.2.10 -n 100 -l 1400` | 100% 通 |
| S3-TC05 | 重做 S1-TC04/05/06/07 | 硬件四证据成立；标签栈仍为两层 |
| S3-TC06 | `no ip route 10.1.1.0/24 192.168.100.5 vrf CUST1` | PE1 撤销静态路由 → VPNv4 撤销 → PC2 侧路由消失；恢复后重建 |

### 7.4 注意

- **staticd 必须运行**，否则 `ip route ... vrf CUST1` 不会进 zebra 的 CUST1 RIB。
- 静态路由的 VRF 关键字可以放在末尾：`ip route 10.1.1.0/24 192.168.100.5 vrf CUST1`；不要写成接口子命令。
- 如果客户站点有多个网段，逐条写；大规模场景建议改用动态协议。
- 第 ②③ 段仍然一个字符不改。

---

## 8. 场景 S4：多 VRF / 多客户 / 地址重叠

### 8.1 目的

验证同一台 PE 上承载多个 VPN，且不同客户可以使用**完全相同的地址空间**而不互相泄漏。这是 L3VPN 的核心价值，也是芯片 `(vrf_id, label)` 双键设计的关键验证。

### 8.2 拓扑扩展

在 S1 基础上，每台 PE 增加一个 CUST2：

```
CE3 --(192.168.110.0/24)--> PE1 CUST2 vlanif110
CE4 --(192.168.210.0/24)--> PE2 CUST2 vlanif210
客户地址：CUST2 站点1 = 10.1.1.0/24（与 CUST1 站点1 相同）
          CUST2 站点2 = 10.2.2.0/24（与 CUST1 站点2 相同）
```

| 设备 | 接口 | 地址 | VRF |
|---|---|---|---|
| PE1 | vlanif110 | 192.168.110.1/24 | **CUST2** |
| PE2 | vlanif210 | 192.168.210.3/24 | **CUST2** |
| CE3 | vlanif110 | 192.168.110.5/24 | default |
| CE3 | vlanif111 | 10.1.1.5/24（与 CE1 同网段） | default |
| CE4 | vlanif210 | 192.168.210.5/24 | default |
| CE4 | vlanif211 | 10.2.2.5/24（与 CE2 同网段） | default |
| PC3 | 网卡 | 10.1.1.10/24，GW 10.1.1.5 | — |
| PC4 | 网卡 | 10.2.2.10/24，GW 10.2.2.5 | — |

> PC1 与 PC3 地址相同、PC2 与 PC4 地址相同，用于证明 VPN 隔离。

### 8.3 配置要点

#### PE1 新增 CUST2

```
! 系统层
! ip link add CUST2 type vrf table 200
! ip link set CUST2 up
! ip link set vlanif110 master CUST2
!
vrf CUST2
 exit-vrf
!
interface vlanif110
 ip address 192.168.110.1/24
exit
!
router rip vrf CUST2
 version 2
 network 192.168.110.0/24
 redistribute bgp metric 2
exit
!
router bgp 65001 vrf CUST2
 bgp router-id 1.1.1.1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:200          ! RD 与 CUST1 不同
  route-target vpn import 65001:200 ! RT 与 CUST1 不同
  route-target vpn export 65001:200
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### PE2 新增 CUST2

```
! ip link add CUST2 type vrf table 200
! ip link set CUST2 up
! ip link set vlanif210 master CUST2
!
vrf CUST2
 exit-vrf
!
interface vlanif210
 ip address 192.168.210.3/24
exit
!
router rip vrf CUST2
 version 2
 network 192.168.210.0/24
 redistribute bgp metric 2
exit
!
router bgp 65002 vrf CUST2
 bgp router-id 3.3.3.3
 !
 address-family ipv4 unicast
  redistribute rip
  label vpn export auto
  rd vpn export 65002:200          ! RD 与 CUST1 不同
  route-target vpn import 65001:200
  route-target vpn export 65001:200
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### CE3 / CE4

与 CE1 / CE2 对称，只是接口和网段换成 CUST2 的。

### 8.4 关键验证点

| 证据 | 命令 | 期望 |
|---|---|---|
| 两个 VRF 存在 | `PE1# show vrf` | CUST1 id 8 table 100；CUST2 id 9 table 200（id 以实际为准） |
| 两个 VRF 都有同前缀 | `PE1# show ip route vrf CUST1 10.2.2.0/24`；`show ip route vrf CUST2 10.2.2.0/24` | 两张表各有一条 `B` 路由，互不覆盖 |
| 芯片 IPUC 双 VRF | `PE1# show ipuc 8 10.2.2.0 24 detail`；`show ipuc 9 10.2.2.0 24 detail` | 两条都命中，`vrfId` 分别为 8/9 |
| 每 VRF 独立 VPN 标签 | `PE2# show mpls table`；`show mpls ilm 0 label <L_v1> detail`；`... <L_v2> detail` | CUST1/CUST2 各一个标签；两个 ILM 的 `vrf_id` 不同 |
| 标签不串 | 抓包 PE2 入方向 | CUST1 流量用 L_v1，CUST2 用 L_v2；不能出现同一标签对应两个 VRF |
| 业务隔离 | PC1↔PC2 通；PC3↔PC4 通；PC1→PC4 **不通**；PC3→PC2 **不通** | 隔离完整 |

### 8.5 判定

- 两个 VPN 各自端到端通，且地址重叠不冲突 = PASS；
- PC1→PC4 能通 = VPN 隔离失败（RT/RD 配错或 VRF 绑定错误）；
- 两个 VRF 用了同一个 VPN 标签 = 标签分配/ILM 双键异常；
- `show ipuc` 只能在默认 VRF 查到前缀 = VRF 路由下发失败。

> **实现依据**：FRR 的 `label vpn export auto` 默认是**每 VRF 一个标签**（`tovpn_label`），同一 VRF 内多个前缀共用一个 VPN 标签。因此多 VRF 场景下，不同 VRF 必须拿到不同标签；芯片 L3VPN ILM 以 `(vrf_id, in_label)` 为键，正是为了支持这种复用。

---

## 9. 场景 S5：Hub-and-Spoke（RT 控制）

### 9.1 目的与拓扑

验证用 RT 导入/导出策略实现 Hub-and-Spoke：Spoke 只能访问 Hub，Spoke 之间不能直接互通。本场景把原来的 P（2.2.2.2）升级为 PE3（Spoke2），三台 CTC 都承担 PE 角色；原 PE2（3.3.3.3）保持 Spoke1，CE2 继续挂在 PE2 上。

```
        PE1（Hub）──────── PE3（Spoke2）──────── PE2（Spoke1）
         |  1.1.1.1           |  2.2.2.2            |  3.3.3.3
        CE1                  CE3                   CE2
     10.1.1.0/24         10.3.3.0/24           10.2.2.0/24
```

- Underlay：三台之间 RIP + LDP，lo/32 互通；
- Overlay：三台同属 AS 65001，iBGP full mesh（或 PE1 作 RR）；
- CUST1：三台各一个 VRF。
- 物理上 PE3（原 P）在中间，PE1 到 PE2 的流量经 PE3 转发；PE3 同时承担"P 的外层标签转发"和"Spoke2 的 PE 角色"。

### 9.2 RT 设计

| 设备 | 角色 | 导出 RT | 导入 RT |
|---|---|---|---|
| PE1 | Hub | `65001:100`（Hub） | `65001:200`（所有 Spoke） |
| PE2 | Spoke1 | `65001:200`（Spoke） | `65001:100`（只导入 Hub） |
| PE3 | Spoke2 | `65001:200`（Spoke） | `65001:100`（只导入 Hub） |

效果：

- Hub 导出的路由带 `65001:100`，两个 Spoke 都导入 → Spoke 能看到 Hub；
- Spoke 导出的路由带 `65001:200`，Hub 导入 → Hub 能看到所有 Spoke；
- Spoke 之间不导入 `65001:200` → Spoke1 看不到 Spoke2，反之亦然；
- 若需要 Spoke 间互通，把 Spoke 的 import 也加上 `65001:200`，但这会退化成 full mesh，按客户需求选择。

### 9.3 配置要点

#### PE1（Hub）

```
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 2.2.2.2 remote-as 65001
 neighbor 2.2.2.2 update-source lo
 neighbor 3.3.3.3 remote-as 65001
 neighbor 3.3.3.3 update-source lo
 !
 address-family ipv4 vpn
  neighbor 2.2.2.2 activate
  neighbor 3.3.3.3 activate
 exit-address-family
exit
!
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn export 65001:100   ! Hub 导出
  route-target vpn import 65001:200   ! Hub 导入所有 Spoke
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### PE2（Spoke1，3.3.3.3）

```
router bgp 65001
 bgp router-id 3.3.3.3
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 1.1.1.1 remote-as 65001
 neighbor 1.1.1.1 update-source lo
 neighbor 2.2.2.2 remote-as 65001
 neighbor 2.2.2.2 update-source lo
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
  neighbor 2.2.2.2 activate
 exit-address-family
exit
!
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:200
  route-target vpn export 65001:200   ! Spoke 导出
  route-target vpn import 65001:100   ! 只导入 Hub
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### PE3（Spoke2，2.2.2.2，原 P）

```
router bgp 65001
 bgp router-id 2.2.2.2
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 1.1.1.1 remote-as 65001
 neighbor 1.1.1.1 update-source lo
 neighbor 3.3.3.3 remote-as 65001
 neighbor 3.3.3.3 update-source lo
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
  neighbor 3.3.3.3 activate
 exit-address-family
exit
!
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute connected
  redistribute rip
  label vpn export auto
  rd vpn export 65001:300
  route-target vpn export 65001:200   ! Spoke 导出
  route-target vpn import 65001:100   ! 只导入 Hub
  export vpn
  import vpn
 exit-address-family
exit
!
```

#### PE3 新增接口与 CE3（S5 补充）

| 设备 | 接口 | 地址 | VRF |
|---|---|---|---|
| PE3 | vlanif130（新增，接 CE3） | 192.168.130.2/24 | **CUST1** |
| CE3 | vlanif130 | 192.168.130.5/24 | default |
| CE3 | vlanif131 | 10.3.3.5/24 | default |
| PC3 | 网卡 | 10.3.3.10/24，GW 10.3.3.5 | — |

PE3 的 PE-CE RIP 实例（与 underlay 的 `router rip` 是两个独立实例）：

```
router rip vrf CUST1
 version 2
 network 192.168.130.0/24
exit
```

CE3：

```
hostname CE3
!
interface vlanif130
 ip address 192.168.130.5/24
exit
!
interface vlanif131
 ip address 10.3.3.5/24
exit
!
router rip
 version 2
 network 192.168.130.0/24
 network 10.3.3.0/24
exit
!
line vty
!
end
```

> PE3 的 vlanif130 必须挂到 CUST1（netns 后端用 `interface vlanif130 vrf CUST1`，vrf-lite 用 `ip link set vlanif130 master CUST1`）；underlay 的 vlanif10/vlanif20 仍留在 default VRF。

> PE3 原先是 P，需要新增 CUST1 VRF、CE3 接入接口、PE-CE RIP 实例；同时保留原有的 `router rip`（underlay）和 `mpls ldp` 配置。

> 同 AS 的 iBGP 需要 loopback 互通；`update-source lo` 必须配。若用 eBGP 也可以，但每对 PE 需要不同的 AS 或 `ebgp-multihop`，配置更复杂；本场景推荐 iBGP。

### 9.4 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S5-TC01 | 三台 `show bgp ipv4 vpn summary` | 两两 Established |
| S5-TC02 | `PE1# show ip route vrf CUST1` | 有 CE1、CE2、CE3 三个站点的路由 |
| S5-TC03 | `PE2# show ip route vrf CUST1` | 有 CE2 + CE1（Hub），**没有** CE3 |
| S5-TC04 | `PE3# show ip route vrf CUST1` | 有 CE3 + CE1（Hub），**没有** CE2 |
| S5-TC05 | PC1↔PC2、PC1↔PC3 ping | 都通（经 Hub） |
| S5-TC06 | PC2→PC3 ping | **不通**（Spoke 间隔离） |
| S5-TC07 | `PE1# show bgp ipv4 vpn` | 三条前缀，RT 分别为 65001:100/200/200 |
| S5-TC08 | 硬件四证据 | 三个 PE 各自作为 ingress/egress 时，E1–E6 成立 |

> **注意**：Spoke 间流量如果要经过 Hub 转发，Hub 必须同时导入/导出对应 RT，且 Hub 的 VRF 路由要能把流量送到另一个 Spoke；本设计的"Spoke 间不通"是**路由层面不通**，不是硬件丢包。若客户要求 Spoke 间经 Hub 互通，应把 Hub 作为中转点，Spoke 只导入 Hub 的 RT，Hub 导入所有 Spoke 的 RT——此时 Hub 会重新通告吗？标准 BGP 不会把从一个 Spoke 学到的 VPN 路由再导出给另一个 Spoke，除非配置额外的 VRF 泄漏/路由策略；这一点在需求确认时要明确。

---

## 10. 场景 S6：Underlay 改用 OSPF

### 10.1 目的

证明 L3VPN 与 underlay IGP 解耦：LDP 只依赖路由可达，不依赖具体 IGP。把 RIP 换成 OSPF 后，L3VPN 控制面/数据面行为应完全不变。

### 10.2 配置改动（只改 underlay）

#### PE1

```
! 删除 router rip（underlay），改为：
router ospf
 ospf router-id 1.1.1.1
 network 1.1.1.1/32 area 0
 network 192.168.12.0/24 area 0
exit
!
! PE-CE 的 router rip vrf CUST1 保留不动
! mpls ldp 保留不动
! bgp 保留不动
```

#### P

```
router ospf
 ospf router-id 2.2.2.2
 network 2.2.2.2/32 area 0
 network 192.168.12.0/24 area 0
 network 192.168.23.0/24 area 0
exit
!
! mpls ldp 保留不动
```

#### PE2

```
router ospf
 ospf router-id 3.3.3.3
 network 3.3.3.3/32 area 0
 network 192.168.23.0/24 area 0
exit
```

> PE-CE 仍可用 RIP/eBGP/静态；本场景只替换 underlay。客户口仍然不能进 OSPF。

### 10.3 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S6-TC01 | `PE1# show ip ospf neighbor` | 与 P 建立 FULL |
| S6-TC02 | `PE1# show ip route` | 有 `O 2.2.2.2/32`、`O 3.3.3.3/32`、`O 192.168.23.0/24` |
| S6-TC03 | `PE1# show mpls ldp neighbor` | 与 P OPERATIONAL；LDP 绑定覆盖 2.2.2.2/32、3.3.3.3/32 |
| S6-TC04 | `PE1# show mpls table` | 3.3.3.3/32 ILM 存在，下一跳 192.168.12.2 |
| S6-TC05 | 重做 S1 阶段 C/D + TC01–TC07 | L3VPN 与 S1 行为一致 |
| S6-TC06 | OSPF 收敛 | P 与 PE2 断链后，PE1 的 OSPF 路由撤销 → LDP 标签撤销 → L3VPN 路由撤销；恢复后重建 |

### 10.4 判定

- OSPF 邻居 FULL + LDP 会话 OPERATIONAL + L3VPN 端到端通 = PASS；
- 只需改 underlay，PE-CE/BGP/LDP 配置不动，说明 L3VPN 与 IGP 解耦成立。

---

## 11. 场景 S7：传输标签 explicit-null 变体

### 11.1 目的

验证 PE2 对直连 FEC 通告 **explicit-null（label 0）** 时，P 的倒数第二跳行为与 PE2 对 `[0][VPN][IP]` 的处理。该场景与 S1 的 implicit-null 是两条不同的 PHP 路径，必须分别验证。

### 11.2 配置改动

只在 PE2 的 LDP 下增加：

```
mpls ldp
 router-id 3.3.3.3
 address-family ipv4
  discovery transport-address 3.3.3.3
  label local advertise explicit-null
  interface vlanif20
  exit-address-family
 exit
!
```

> 测完恢复：`no label local advertise explicit-null`。

### 11.3 预期行为

| 检查 | 预期 |
|---|---|
| `P# show mpls table` | 3.3.3.3/32 出标签为 **IPv4 Explicit Null（0）**，不是 implicit-null |
| P vlanif20 出方向抓包 | `[0][L_v][IP]`：外层 label 0，内层 VPN 标签，内层 S=1 |
| PE2 入方向抓包 | 同样 `[0][L_v][IP]` |
| PE2 芯片 | 先处理 label 0（弹掉/跳过），再命中 `<L_v>` 的 L3VPN ILM；内层 IP 在 CUST1 vrf_id 下查表 |
| `PE2# show mpls ilm 0 label <L_v> detail` | L3VPN ILM 不变（vrf_id/innerPacketLookup 正确） |
| PC1↔PC2 | 100% 通 |
| tracert | 报文在 PE2 弹掉 label 0 后按 IPv4 转发；MPLS 域内 TTL 行为以抓包为准（P 是否出现在 tracert 中取决于 label 0 的 TTL 处理），不应出现环路或 TTL 耗尽 |
| dmesg | 无 label 0 相关 error |

### 11.4 判定与回退

- `[0][L_v][IP]` 端到端通 + PE2 内层查表正确 = PASS；
- PE2 丢弃或内层查表 miss = 先确认 label 0 的处理路径（是芯片自动 pop，还是需要显式 ILM），记录为限制；**L3VPN 生产建议优先使用 implicit-null**（S1），除非客户明确要求 explicit-null；
- 测完必须恢复 implicit-null，避免影响后续场景基线。

> **与 S1 的区别**：S1 的 P 出方向是 `[L_v][IP]`（标签 3 被 PHP 消耗，不压上线）；S7 的 P 出方向是 `[0][L_v][IP]`（label 0 真实出现在线上）。两者都要求 PE2 最终按 VPN 标签进入 VRF，但入口标签栈不同。

---

## 12. 场景 S8：CE 双归属 / PE 冗余（扩展）

### 12.1 目的

验证 CE 双上行到两台 PE 时的冗余与收敛：主链路故障后流量切到备 PE，VPN 路由/标签随之更新，硬件表项无残留。

### 12.2 拓扑设计（3 台 CTC 可行方案）

把 P 升级为第二个 PE（PE1'），CE1 同时接 PE1 和 P：

```
            ┌──────── PE1（AS 65001，CUST1）────────┐
CE1 ────────┤                                       ├──── PE2（AS 65002，CUST1）──── CE2
            └──────── P/PE1'（AS 65001，CUST1）─────┘
```

- CE1 与 PE1、P 分别建立 eBGP（CE AS 65101）；
- PE1 与 P 同属 AS 65001，iBGP 交换 VPNv4（或都 eBGP 到 PE2）；
- PE2 作为远端 PE，学到 CE1 的两条路径（AS path 相同/不同按设计）；
- 通过 BGP best-path / MED / local-pref 决定主备。

### 12.3 配置要点

| 设备 | 关键配置 |
|---|---|
| CE1 | 两个 eBGP 邻居（PE1、P）；`network 10.1.1.0/24`；可用 local-pref 控制主备 |
| PE1 | CUST1 VRF + eBGP 邻居 CE1；`export/import vpn`；与 P/PE2 交换 VPNv4 |
| P/PE1' | 新增 CUST1 VRF + vlanif 接 CE1；eBGP 邻居 CE1；与 PE1/PE2 交换 VPNv4；**同时保留原 P 的 LDP 转发能力** |
| PE2 | 不变；学到 CE1 的两条 VPN 路径 |

### 12.4 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S8-TC01 | 双邻居建立 | CE1 与两台 PE 的 eBGP 都 Established；PE2 的 VPNv4 表有两条 CE1 路由（不同 next-hop） |
| S8-TC02 | 主路径转发 | PC1→PC2 走主 PE；抓包确认标签栈正确 |
| S8-TC03 | 主链路故障 | shutdown CE1-PE1 链路 → BGP 撤销 → PE2 切到备路径；ping 中断时间 < 收敛目标（如 3s）；硬件表项更新 |
| S8-TC04 | 主链路恢复 | 路由重新优选；流量回切（或保持，取决于 BGP 策略）；无环路 |
| S8-TC05 | 硬件一致性 | 两台 PE 作为 ingress 时 E1/E2 成立；PE2 作为 egress 时 E4/E5 成立 |
| S8-TC06 | 资源 | 反复切换 10 次，无表项泄漏、无 -31993 |

### 12.5 判定

- 双邻居 Established + 主备切换业务恢复 + 硬件表项更新正确 = PASS；
- 切换后 PE2 仍指向失效 next-hop = BGP/nexthop 跟踪异常；
- 切换后出现标签错配/环路 = 重点查两台 PE 的 VPN 标签与 RD/RT 是否一致。

> 本场景为 P2 扩展项，需确认 P/PE1' 同时承担"核心 P"和"PE"两个角色时，LDP 外层标签与 VPN 内层标签不冲突。建议单独用一台设备做 PE1' 更干净；三台设备方案仅用于功能摸底。

---

## 13. 场景 S9：核心 ECMP（扩展）

### 13.1 目的

验证 underlay 等开销多路径时，LDP 为同一 FEC 安装多个 NHLFE，ingress PE 的 IPUC 路由构建 ECMP 组，流量在两条核心链路上分担；单链路故障后自动收敛。

### 13.2 拓扑扩展

在 S1 基础上增加一条 P↔PE2 链路：

```
PE1 ──(vlanif10)── P ──(vlanif20)── PE2
                     └──(vlanif30)──┘
```

| 设备 | 新接口 | 地址 |
|---|---|---|
| P | vlanif30 | 192.168.30.2/24 |
| PE2 | vlanif30 | 192.168.30.3/24 |

### 13.3 配置改动

```
! P
interface vlanif30
 ip address 192.168.30.2/24
exit
router rip
 network 192.168.30.0/24
exit
mpls ldp
 address-family ipv4
  interface vlanif30
 exit-address-family
exit
!
! PE2
interface vlanif30
 ip address 192.168.30.3/24
exit
router rip
 network 192.168.30.0/24
exit
mpls ldp
 address-family ipv4
  interface vlanif30
 exit-address-family
exit
```

> 确保两条路径的 RIP 开销相同（默认跳数相同）。若开销不同，只有一条进入 RIB，ECMP 不成立。

### 13.4 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S9-TC01 | `PE1# show ip route 3.3.3.3/32` | 两个 nexthop（经 P 的 vlanif20 和 vlanif30） |
| S9-TC02 | `PE1# show mpls table` | 3.3.3.3/32 有两条 NHLFE |
| S9-TC03 | `PE1# show ipuc <vrf_id> 10.2.2.0 24 detail` | nexthop 为 ECMP 组，成员为两个 push NH |
| S9-TC04 | `PE1# show nexthop <nhid> detail` | ECMP 组含两个成员，每个成员的 tunnel/push 标签正确 |
| S9-TC05 | PC1→PC2 持续流量 + 两条链路抓包 | 两条链路都出现标签流量（逐包哈希，比例大致均衡） |
| S9-TC06 | 断开一条链路 | 流量切到剩余链路，业务不中断或短暂中断后恢复；ECMP 组更新，无残留成员 |
| S9-TC07 | 反复切换 10 次 | 无表项泄漏、无 `has >N ECMP nexthops; only first N sent` 告警 |

### 13.5 判定

- 双路径 RIB + 双 NHLFE + ECMP 组 + 双链路流量 = PASS；
- 只有一条链路有流量 = ECMP 哈希/组构建失败；
- 日志出现 `CTC-DPLANE: VRF route ... has >N ECMP nexthops; only first N sent to HSL` = 成员数超过 `CTC_HAL_MSG_MAX_NH_VRF`，硬件组缺臂，必须减少成员或扩大消息容量。

---

## 14. 场景 S10：IPv6 VPNv6（条件项）

### 14.1 前置确认（不通过就不要往下做）

| 检查 | 命令 | 要求 |
|---|---|---|
| 内核 IPv6 | `sysctl net.ipv6.conf.all.forwarding` | 1 |
| 接口 IPv6 | `show interface vlanif...` | 有 v6 地址 |
| 芯片 IPv6 路由 | `show ipuc ipv6 route` / `show ipuc lpm ipv6 ...` | 可用 |
| FRR VPNv6 | `show bgp ipv6 vpn summary` 命令存在 | 支持 |
| hw_l3 v6 | 代码 `hw_l3_route_add_vrf` 支持 `AF_INET6` | 已支持 |
| L3VPN ILM v6 | `ilm.inner_pkt_type = CTC_MPLS_INNER_IP` | 代码只指定"内层是 IP"，v4/v6 由芯片按内层版本解析；必须用 `show ipuc ipv6` + pkt-trace 实测确认 |

### 14.2 配置差异

```
! 接口
interface vlanif101
 ipv6 address 2001:db8:1::5/64
exit
!
! PE-CE（可用 eBGP 或 RIPng；下面是 CE1 的 eBGP 片段）
router bgp 65101
 no bgp ebgp-requires-policy
 neighbor 2001:db8:100::1 remote-as 65001
 address-family ipv6 unicast
  neighbor 2001:db8:100::1 activate
  network 2001:db8:1::/64
 exit-address-family
exit
!
! PE 全局
router bgp 65001
 address-family ipv6 vpn
  neighbor 3.3.3.3 activate
  neighbor 3.3.3.3 next-hop-self
 exit-address-family
exit
!
! PE VRF
router bgp 65001 vrf CUST1
 address-family ipv6 unicast
  redistribute connected
  redistribute static
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
exit
```

### 14.3 验证

| 检查 | 命令 | 期望 |
|---|---|---|
| VPNv6 会话 | `show bgp ipv6 vpn summary` | Established，PfxRcd ≥1 |
| VPNv6 表 | `show bgp ipv6 vpn` | 前缀带标签 |
| VRF v6 路由 | `show ip route vrf CUST1` | `B` v6 路由 |
| 芯片 v6 IPUC | `show ipuc ipv6 <vrf_id> 2001:db8:2:: 64 detail` | 命中，vrfId 正确 |
| ILM | `show mpls ilm 0 label <L_v6> detail` | L3VPN ILM，vrf_id 正确，inner_pkt_type=IP |
| 端到端 | `ping6` | 通 |

### 14.4 判定

- 任何一层不支持（芯片 v6 LPM、FRR VPNv6、hw_l3 v6）→ 记录为**条件不支持**，不要强行判定 L3VPN 失败；
- 全链路 v6 通 + 硬件证据成立 = PASS，可作为 v4/v6 双栈交付依据。

---

## 15. 场景 S11：跨域 Option A / B / C（扩展/条件项）

### 15.1 先做特性确认

| 方案 | 关键能力 | 当前实现判断 | 建议 |
|---|---|---|---|
| **Option A** | ASBR 背靠背 VRF，VRF 间 eBGP | FRR 标准能力；不依赖 VPNv4 跨 AS | 最稳妥，推荐先做 |
| **Option B** | ASBR 交换 VPNv4，并为 VPN 路由重新分配本地标签 | 依赖 FRR 对 VPNv4 重广告的标签分配/next-hop 处理；需实测 | 实验室先做 2 台 ASBR 的最小验证 |
| **Option C** | BGP-LU 承载 PE loopback，VPNv4 只在 PE 间 | hw_mpls.c 有 BGP-LU 单标签 push（`PUSH_OP_ROUTE`），但完整 Option C 需 labeled-unicast + 多跳 | 确认 `address-family ipv4 labeled-unicast` 与硬件单标签 push 后再做 |

### 15.2 Option A 配置骨架

```
! ASBR1（AS 65001）与 ASBR2（AS 65002）背靠背
! ASBR1
vrf CUST1
 exit-vrf
!
interface vlanifX
 ip address 192.168.99.1/24
exit
!
router bgp 65001 vrf CUST1
 neighbor 192.168.99.2 remote-as 65002
 !
 address-family ipv4 unicast
  neighbor 192.168.99.2 activate
  redistribute connected
 exit-address-family
exit
!
! ASBR2 对称：router bgp 65002 vrf CUST1，neighbor 192.168.99.1 remote-as 65001
```

**验证重点**：

- ASBR 之间是 **VRF 内 IP 转发**，不携带 VPN 标签穿过 AS 边界；
- 每个 AS 内部仍是 PE-ASBR 的 L3VPN（VPNv4 + LDP）；
- 两端 ASBR 的 CUST1 路由表能互相学到站点网段；
- 硬件：ASBR 作为 egress/ingress 时 L3VPN ILM 和 IPUC 正常；ASBR 之间抓包为无标签 IP。

### 15.3 Option B / C 的验证清单（先小规模）

| 检查 | Option B | Option C |
|---|---|---|
| ASBR 是否重分配 VPN 标签 | `show bgp ipv4 vpn` 出方向标签是否变化 | PE 间 VPNv4 直连/反射 |
| next-hop 处理 | `next-hop-self` / `next-hop-unchanged` | BGP-LU 标签到 PE loopback |
| 硬件 | ASBR 只换外层 transport 标签，VPN 标签透传 | PE 压 [transport-LU][VPN]，P 只换外层 |
| 失败特征 | 标签不重分配导致对端 VRF 错乱/丢弃 | BGP-LU 标签未落表导致 PE loopback 不可达 |

> 本场景不作为三台设备基础验收项；没有明确跨域需求时，建议只做 S1–S7。

---

## 16. 场景 S12：路由反射器（扩展）

### 16.1 目的

PE 数量较多时，用 RR 替代 full mesh iBGP。RR 只反射 VPNv4 控制面，数据面仍由 LDP 提供外层标签。

### 16.2 拓扑与配置

三台同属 AS 65001，P 作 RR：

```
PE1 ── P（RR） ── PE2
```

```
! P（RR）
router bgp 65001
 bgp router-id 2.2.2.2
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 1.1.1.1 remote-as 65001
 neighbor 1.1.1.1 update-source lo
 neighbor 3.3.3.3 remote-as 65001
 neighbor 3.3.3.3 update-source lo
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
  neighbor 1.1.1.1 route-reflector-client
  neighbor 3.3.3.3 activate
  neighbor 3.3.3.3 route-reflector-client
 exit-address-family
exit
!
```

```
! PE1（PE2 对称：router-id 3.3.3.3，neighbor 2.2.2.2）
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp default ipv4-unicast
 neighbor 2.2.2.2 remote-as 65001
 neighbor 2.2.2.2 update-source lo
 !
 address-family ipv4 vpn
  neighbor 2.2.2.2 activate
 exit-address-family
exit
!
```

PE1/PE2 只与 P 建 iBGP，不再互建；VRF 配置与 S1 相同（AS 号改为 65001，RD 可保持各端不同，如 65001:100 / 65001:200）。Underlay 的 IGP 与 LDP 仍按 S1 配置，P 继续转发外层标签。

### 16.3 测试用例

| 用例 | 步骤 | 预期 |
|---|---|---|
| S12-TC01 | `PE1# show bgp ipv4 vpn summary` | 只有 P 一个邻居，Established，PfxRcd ≥1 |
| S12-TC02 | `P# show bgp ipv4 vpn` | 能看到 PE1/PE2 的 VPN 路由；P 本身无 VRF |
| S12-TC03 | `PE1# show ip route vrf CUST1` | 有对端站点 `B` 路由 |
| S12-TC04 | 端到端 ping | 100% 通 |
| S12-TC05 | 硬件 | E1–E6 成立；P 只做外层标签转发，不参与 VPN 内层查表 |
| S12-TC06 | RR 故障 | P 的 BGP 进程重启后，PE1/PE2 会话重建，路由恢复 |

> RR 场景下 P 同时是 RR 和 LDP 转发节点；注意 P 不配置 CUST1 VRF，避免误把客户路由引入核心。

---

## 17. 硬件落表验证手册

本章是所有场景共用的"证据采集方法"。每个场景只需要把本章的命令套进去，把期望值换成该场景的标签/VRF/前缀。

### 17.1 验证层次与命令总表

| 层 | 命令 | 看什么 | 典型期望 |
|---|---|---|---|
| 控制面 VRF | `show vrf` | VRF 名、`id`、`table` | `CUST1 id 8 table 100` |
| 控制面 VRF | `show interface vrf CUST1` | 接口归属 | 客户口在列表里 |
| 控制面 RIB | `show ip route vrf CUST1` | `C`/`R`/`B` 路由 | 本站点 + 对端站点 |
| 控制面 BGP | `show bgp vrf CUST1 ipv4 unicast` | VRF BGP 表 | 本地 + 导入 |
| 控制面 BGP | `show bgp ipv4 vpn` | VPNv4 表 | RD/RT/Label |
| 控制面 LDP | `show mpls ldp neighbor` / `binding` | 会话/标签映射 | OPERATIONAL |
| 控制面 LSP | `show mpls table` | ILM 类型/出标签 | LDP / L3VPN |
| 芯片 IPUC | `show ipuc <vrf_id> <prefix> <len> detail` | 路由、nexthop、vrfId | 命中 |
| 芯片 IPUC | `show ipuc <vrf_id> <prefix> <len> hit` | 命中计数 | 随流量增长 |
| 芯片 IPUC | `show ipuc lpm <vrf_id> all detail` | VRF 内 LPM | 前缀存在 |
| 芯片 IPUC v6 | `show ipuc ipv6 <vrf_id> <prefix> <len>` | v6 路由 | 条件项 |
| 芯片 ILM | `show mpls ilm 0 label <label> detail` | 类型/出标签/nh | L3VPN 或 NORMAL |
| 芯片 nexthop | `show nexthop <nhid> detail` | 类型、标签、oif、MAC | push/tunnel 正确 |
| 芯片 tunnel | `show nexthop mpls-tunnel <id> detail` | 外层标签、VLAN、L3IFID、MAC | 指向核心口 |
| 芯片 ECMP | `show nexthop all ecmp` | ECMP 组 | 成员数正确 |
| 芯片资源 | `show nexthop status` / `show mpls status` | 表项数量/水位 | 无异常增长 |
| 芯片诊断 | `show diag pkt-trace detail` | 命中/丢弃阶段 | 见 17.4 |
| 芯片丢弃 | `show diag drop-info` | 丢弃原因 | 无意外丢弃 |
| 芯片内存 | `show diag mem-usage sram ip-lpm` / `... mpls` | 表项水位 | 未打满 |
| 驱动日志 | `dmesg` / `journalctl -k` | `hw_mpls` / `hw_l3` / `ctc_dplane` | 无 error |

> CTC CLI 的具体命令集随 SDK 版本/平台可能略有差异；以上为 5.5.3 通用命令。若某条命令不存在，用 `?` 查同族命令，不要跳过该证据层。

### 17.2 T2（ingress VPN 路由）判定细则

以 PE1、CUST1 `vrf_id=8`、10.2.2.0/24、标签栈 `[16][144]` 为例：

```
PE1# show ipuc 8 10.2.2.0 24 detail
```

| 字段 | 期望 | 说明 |
|---|---|---|
| `vrfId` | 8 | 必须是 CUST1 的 FRR VRF ID，不是 table 100 |
| `prefix` / `masklen` | 10.2.2.0 / 24 | 与 BGP 前缀一致 |
| nexthop 指针 | 指向 push NH | 不是 TOCPU、不是默认丢弃 |
| 路由类型 | 带标签路由 | 由 dplane 的 VRF route 下发 |

```
PE1# show nexthop <nhid> detail
```

| 字段 | 期望 | 说明 |
|---|---|---|
| 类型 | MPLS push / tunnel + push | L3VPN 双层栈 |
| tunnel label | 16（外层 transport） | LDP 为 3.3.3.3/32 分配 |
| push label | 144（内层 VPN） | PE2 为 CUST1 分配的 VPN 标签 |
| oif / VLAN | vlanif10 / VLAN 10 | 指向 P |
| DMAC | P 的接口 MAC | 来自 ARP 解析 |
| TTL/EXP | 按实现 | 见 22 章限制 |

```
PE1# show nexthop mpls-tunnel <tunnel_id> detail
```

| 字段 | 期望 |
|---|---|
| `REF_CN` | ≥1（被 push NH 引用） |
| `VLAN` | 10 |
| `L3IFID` | vlanif10 对应的 l3if |
| `MACDA` | P 的 MAC |
| `LSP` / tunnel label | 外层标签 16 |

### 17.3 T3（egress VPN 标签 ILM）判定细则

以 PE2、VPN 标签 144、`vrf_id=8` 为例：

```
PE2# show mpls ilm 0 label 144 detail
```

| 字段 | 期望 | 说明 |
|---|---|---|
| `type` | **L3VPN** | 不是 NORMAL |
| `id_type` | **VRF** | 不是 L2 FID / VSI |
| `vrf_id` | 8 | 与 `show vrf` 的 id 一致 |
| `innerPacketLookup` | 1 | 弹标签后查内层 IP |
| `fidSelect` | 1 | fid 有效 |
| `fidType` | 0 | 0 = fid 是 VRF；1 = L2 FID/VSI |
| `fid` | 8 | 与 vrf_id 一致 |
| `secondFidEn` | 0 | L3VPN 不需要第二 FID |
| `nh_id` | TOCPU（2） | **正常终态**，不是错误 |
| `sBitCheckEn` | 1 | 校验 BOS |

> 如果 `show mpls ilm` 的字段名与上表不同，用 `show diag pkt-trace detail` 和 `read DsMpls <index>` 交叉验证。`read DsMpls` 的 raw word 解码：`fidSelect` bit16、`fidType` bit15、`fid` bits1-14、`innerPacketLookup` 对应位、`secondFidEn` 对应位。具体位序以芯片手册为准，本文只给出语义。

### 17.4 T4（egress 内层 IP）判定细则

```
PE2# show ipuc 8 10.2.2.0 24 detail
```

| 字段 | 期望 |
|---|---|
| `vrfId` | 8 |
| 路由 | 命中 10.2.2.0/24 |
| nexthop | 指向 CE2（192.168.200.5）或直连接口 |
| ARP | 已解析（非 TOCPU） |

```
PE2# show diag pkt-trace detail
```

正常路径的关键阶段：

```
ILM hit: label 144
  -> pop
  -> HIT-LPM-TCAM (inner IPv4)
  -> DsIpDa route (10.2.2.0/24)
  -> egress vlanif200
```

异常路径的典型签名：

| 签名 | 含义 | 排查 |
|---|---|---|
| `HIT-LPM-TCAM` 后 `DsIpDa 1` + `IPE_DISCARD_DS_FWD_DESTID_DIS` | 内层查表落到 CTC 默认丢弃路由 | 查 `vrf_id` 是否一致；查 `show ipuc <vrf_id>` 是否有前缀；查 `intf-vrfid` |
| `tcam-ad-index` 指向 vrf 0 的行 | 内层 lookup 用了默认 VRF | 查 L3VPN ILM 的 `fid`/`vrf_id`；查 VRF 设备口/L3IF 的 vrf 配置 |
| ILM 未命中 | VPN 标签未落表 / 标签值不符 | 查 `show mpls table`、`show mpls ilm`、dplane 日志 |
| 命中但 `pop=0` | ILM 类型/op 错误 | 查 `ctc_dplane_process_lsp_vrf` 是否走了 VRF LSP 分支 |

> **`intf-vrfid` 是"配置态"**：它来自入接口的 `DsSrcInterface[interfaceId].vrfId`，不是报文运行时推导的。若它一直是 0，而 ILM 的 fid 是 8，就会出现"ILM 说查 vrf 8、实际查 vrf 0"的错配。排查时把 `intf-vrfid`、ILM `fid`、IPUC `vrfId` 三者放在一起看。

### 17.5 证据命名与保存

建议每个用例保存以下五类证据：

| 类别 | 内容 | 命名示例 |
|---|---|---|
| 控制面 | `show` 输出文本 | `S1-TC01_pe1_show-ip-route-vrf.txt` |
| 芯片 | IPUC/ILM/NH/tunnel 输出 | `S1-TC06_pe2_ilm-144.txt` |
| 报文 | pcap | `S1-TC02_pe1-egress.pcap` |
| 诊断 | pkt-trace / drop-info | `S1-TC07_pe2-pkt-trace.txt` |
| 日志 | dmesg / zebra log | `S1-TC10_dmesg.txt` |

---

## 18. 故障定位手册

### 18.1 三分钟分层定位法

按下面的顺序走，每步只回答一个问题，不要跳步：

```
第 1 分钟：业务层
  PC1 ping PC2 通不通？单向还是双向？
  CE1/CE2 有没有到对端站点的路由？
      ↓
第 2 分钟：控制面
  PE1/PE2 的 show ip route vrf CUST1 有没有 B 路由？
  show bgp ipv4 vpn 有没有对端前缀？
  show mpls ldp neighbor / show mpls table 是否覆盖对端 lo/32？
      ↓
第 3 分钟：数据面/芯片
  PE1 出方向有没有两层标签？
  P 出方向是 [L_v][IP] 还是 [L_t][L_v][IP] 原样？
  PE2 的 L3VPN ILM 是否存在、vrf_id 是否正确？
  PE2 内层 IPUC 是否命中？
```

### 18.2 按现象定位

| 现象 | 最可能的原因 | 检查/处理 |
|---|---|---|
| `show ip route vrf CUST1` 完全为空（连 `C` 都没有） | VRF 绑定失败（vrf-lite 后端写了接口子命令 `vrf`） | 查 `show vrf` 第一行判断后端；`show interface vrf CUST1` 是否有接口；vrf-lite 用 `ip link set ... master CUST1` |
| 有 `C` 但没有 `R` | 漏 `router rip vrf CUST1`，或 RIP 实例看不到接口 | `show ip rip vrf CUST1 status` 的 Interface 表 |
| 有 `R` 但没有 `B`（对端站点） | 第 ②③ 段：`export vpn`/`import vpn`/RD/RT 缺失 | `show bgp ipv4 vpn`、`advertised-routes` |
| `show bgp ipv4 vpn` 全空 + 日志 `EBGP outbound policy not properly setup` | RFC 8212：漏 `no bgp ebgp-requires-policy` | 全局实例和 VRF 实例各配一遍 |
| VPNv4 有路由但 PE 的 CUST1 RIB 没有 `B` | 对端 RT import 不匹配，或本端 `import vpn` 缺失 | 对照两端 RT；`show bgp vrf CUST1 ipv4 unicast` |
| PE 有 `B` 路由但 CE 学不到 | 第 ④ 段：漏 `redistribute bgp metric 2`（RIP）或邻居未 activate（eBGP） | `show ip rip vrf CUST1` / `show bgp vrf ... summary` |
| PC ping 不通，但 PE 的 VRF RIB 正常 | underlay 断：LDP 未覆盖对端 lo/32，或 LSP 未落表 | `show mpls ldp neighbor`、`show mpls table`、PE1 抓包 |
| PE1 出方向只有一层标签 | BGP 路由的 nexthop 标签栈不完整，或 dplane `label_stack_depth` 为 1 | `show bgp vrf CUST1 ipv4 unicast <prefix>`、zebra `CTC-DPLANE: VRF route ... labels=2` |
| P 出方向仍是 `[L_t][L_v][IP]` | PE2 通告了真实标签而不是 implicit-null | `PE2# show mpls ldp binding`；恢复默认行为 |
| P 出方向变成 `[IP]`（VPN 标签也被弹掉） | PHP 把整个标签栈弹了 | 查 `hw_mpls_nh_create_pop` / PHP NH；严重回归 |
| PE2 收到 `[L_v][IP]` 但业务不通 | L3VPN ILM 缺失/类型错误 | `show mpls ilm 0 label <L_v> detail`；查 dplane 是否走 `HAL_MSG_MPLS_LSP_VRF_ADD` |
| PE2 ILM 正确但内层查表 miss | 内层 `vrf_id` 与 IPUC 不一致，或前缀不在该 VRF | 对照 `show vrf` id、ILM fid、`show ipuc <vrf_id>` |
| pkt-trace 出现 `DsIpDa 1` / `IPE_DISCARD_DS_FWD_DESTID_DIS` | 内层查表落到默认丢弃 | 查 `intf-vrfid` 与 ILM fid；查 TCAM 里该 VRF 的前缀 |
| ping 通但大包丢 | MTU：两层标签 +8 字节 | 接口/PC MTU 调到 1526+，或载荷 ≤1400 |
| 抓包看到 label 0，业务不通 | explicit-null 变体未验证通过 | 回 S7；生产建议 implicit-null |
| 反复重建后表项增长 | 删除路径泄漏（stale ILM/IPUC/stats） | 查 `hw_mpls_vrf_ilm_map_del`、`hw_l3_vrf_route`、stats detach |
| dmesg 刷 `stats detach failed ... ret=-31993` | stats 已随 ILM 删除释放，删除时又删一次 | 属容错路径；若刷屏且表项泄漏，检查 stats 生命周期 |
| 日志 `has >N ECMP nexthops; only first N sent to HSL` | ECMP 成员超过 HAL 消息容量（8） | 减少成员或扩大 `CTC_HAL_MSG_MAX_NH_VRF` |
| VRF 删除失败/残留 | 接口还挂在 VRF 下就删 VRF | 先 `ip link set vlanifX nomaster`，再删 VRF |

### 18.3 常见配置陷阱清单

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 客户口写进 underlay RIP / LDP | 客户路由泄进核心，隔离破坏 | 客户口只进 VRF，不进 underlay |
| 2 | 接口子命令写 `vrf CUST1`（vrf-lite 后端） | 接口仍在 default VRF，VRF RIB 为空 | `ip link set ... master CUST1` |
| 3 | 漏 `export vpn` / `import vpn` | VPN 路由不进出 | VRF 实例 AF 下补齐 |
| 4 | eBGP 漏 `no bgp ebgp-requires-policy` | 路由不发/不收 | 每个 BGP 实例各配一遍 |
| 5 | 两端 RD 相同且地址重叠 | 两条路由被当成同一条 | RD 两端不同 |
| 6 | 两端 RT 不同 | 路由进不了对端 VRF | RT 两端相同 |
| 7 | PE-CE RIP 没在 VRF 实例里 | PE 学不到客户路由 | `router rip vrf CUST1` |
| 8 | RIP 里漏 `redistribute bgp` | CE 学不到对端站点 | VRF RIP 实例加 `redistribute bgp metric 2` |
| 9 | `label vpn export auto` 缺失 | VPNv4 路由不带标签 | VRF AF 下补齐 |
| 10 | 配了 `vni` | EVPN L3VNI 命令，与 MPLS L3VPN 无关 | 删除 |
| 11 | PE-CE eBGP 误用 `next-hop-self` 解决跨 VPN 下一跳 | 直连场景本就不需要；它不会修正 VPNv4 的下一跳语义 | 直连 PE-CE 不配；VPNv4 的下一跳问题查第 ②③ 段与 `next-hop-self`（全局 VPNv4 邻居） |
| 12 | CE 的 `network` 前缀不在自己 RIB 里 | BGP 不通告 | 先保证直连/静态路由存在 |
| 13 | 静态路由方向写反 | PE 学到了错误下一跳 | PE 指向 CE，CE 默认指向 PE |
| 14 | 删 VRF 前没解绑接口 | 删除失败/残留 | 先 nomaster |

---

## 19. 稳定性、性能与资源验收

### 19.1 容量基线（当前实现）

| 资源 | 上限 | 代码位置 | 说明 |
|---|---|---|---|
| VRF 数量（软件缓存） | 256 | `HW_L3_VRF_MAX` | `hw_l3.c` |
| VRF 路由（软件缓存） | 1024 | `HW_L3_VRF_ROUTE_MAX` | `hw_l3.c`；超出后新路由可能失败 |
| VRF ID | 4095 | `HW_L3_VRF_ID_MAX` | `hw_l3.c` |
| 每 VRF 路由 nexthop | 8 | `HW_L3_VRF_NH_MAX` / `CTC_HAL_MSG_MAX_NH_VRF` | 超出截断并告警 |
| VRF ILM | 512 | `HW_MPLS_VRF_ILM_MAX` | `hw_mpls.c` |
| ECMP 成员 | 8 | `HW_MPLS_ECMP_MEMBER_MAX` | `hw_mpls.c` |
| LSP nexthop（普通） | 4 | `CTC_HAL_MSG_MAX_LSP_NH` | `ctc_dplane.c`/`hsl` |
| 标签栈深度 | 3 | `ctc_dplane.c` / `hw_l3.c` | L3VPN 典型 2 层 |

> 这些是**软件缓存/消息容量**，不是芯片理论上限。规模测试应在上表范围内做；超过时先确认是否需要扩大常量，并做回归。

### 19.2 性能测试

| 项 | 方法 | 记录 |
|---|---|---|
| 吞吐 | iperf3 TCP/UDP，64/512/1400/1500 字节 | 单向/双向带宽 |
| 时延 | ping / iperf3 | 平均/最大时延 |
| 丢包 | 长时间 ping / iperf3 | 丢包率（目标 0） |
| 标签开销 | 对比 CE 侧与 PE 侧抓包 | 每包 +8 字节（两层标签） |
| 大包/分片 | 1400/1472/1500 载荷 | 是否分片、是否通 |
| CPU | `top` / `mpstat` | zebra/bgpd/ldpd/hsl 占用 |

### 19.3 稳定性测试

| 用例 | 方法 | 判定 |
|---|---|---|
| 长稳 | 端到端流量 24h | 无中断、无表项增长、无内存泄漏 |
| 控制面 flap | BGP/LDP/RIP 会话反复 up/down 50 次 | 每次恢复，表项回基线 |
| 链路 flap | 核心/客户链路反复 shutdown/no shutdown 50 次 | 收敛正常，无 stale |
| 设备重启 | PE/P/CE 分别重启 | 重启后自动恢复 |
| 批量前缀 | 1000 条客户前缀（在 1024 上限内） | 全部落表，转发正常 |
| 多 VRF | 10/50/100 个 VRF（在 256 上限内） | 隔离正确，资源不泄漏 |

### 19.4 验收阈值建议

| 指标 | 建议阈值 |
|---|---|
| 端到端丢包 | 0%（稳定期） |
| 故障收敛 | ≤ 3s（BGP+RIP+LDP 联动，按现场要求调整） |
| 表项泄漏 | 50 次 flap 后表项数 = 基线 |
| 长稳 | 24h 无中断、无 error 日志 |
| 大包 | MTU 调整后 1472 字节载荷 100% 通 |

> 收敛阈值与客户 SLA 有关；本文只给建议值，最终以项目验收标准为准。

---

## 20. 通过标准汇总

| 场景 | 优先级 | 通过标准 |
|---|---|---|
| S1 基础 RIP | P0 | S1-TC01–TC12 全绿；E1–E6 成立 |
| S2 PE-CE eBGP | P0 | 双向 Established；客户路由交换；E1–E6 成立；第 ②③ 段不变 |
| S3 PE-CE 静态 | P1 | 静态路由进出 VPN；端到端通；E1–E6 成立 |
| S4 多 VRF | P0 | 两个 VPN 各自通；同地址不冲突；`show ipuc` 双 VRF；ILM 双键正确；无泄漏 |
| S5 Hub-Spoke | P1 | Hub↔Spoke 通；Spoke 间不通；RT 表正确；硬件成立 |
| S6 OSPF underlay | P1 | OSPF FULL + LDP + L3VPN 端到端；只改 underlay |
| S7 explicit-null | P0 | `[0][L_v][IP]` 端到端通；PE2 内层查表正确；或明确记录为限制并回退 implicit-null |
| S8 双归属 | P2 | 双邻居；主备切换收敛；硬件更新；无泄漏 |
| S9 ECMP | P2 | 双路径 RIB/NHLFE/ECMP 组；双链路分流；单链路故障收敛 |
| S10 VPNv6 | P2（条件） | 各层 v6 支持；端到端 ping6；硬件 v6 证据 |
| S11 跨域 | P2（条件） | 按选定 Option 的最小验证通过 |
| S12 RR | P2 | RR 反射 VPNv4；端到端通；硬件成立 |

**整体结论判定**：

- **可交付（IPv4 单域）**：S1 + S2 + S4 + S7 全绿；
- **可交付（含运维场景）**：再加 S3/S5/S6；
- **规模/双栈/跨域**：按项目需求追加 S8–S12，并单独出具结论。

---

## 21. 证据收集与报告模板

### 21.1 目录结构

```
evidence/
└── 2026-10-19_L3VPN_综合验证/
    ├── 00_版本与基线/
    │   ├── pe1_show-version.txt
    │   ├── pe1_show-running-config.txt
    │   ├── pe1_show-vrf.txt
    │   ├── p_show-version.txt
    │   └── pe2_show-version.txt
    ├── 01_控制面/
    │   ├── pe1_show-bgp-ipv4-vpn.txt
    │   ├── pe1_show-ip-route-vrf-CUST1.txt
    │   ├── pe2_show-bgp-vrf-CUST1.txt
    │   └── p_show-mpls-ldp-binding.txt
    ├── 02_数据面/
    │   ├── S1-TC02_pe1-egress.pcap
    │   ├── S1-TC03_p-egress.pcap
    │   └── S1-TC07_pe2-egress.pcap
    ├── 03_芯片/
    │   ├── S1-TC04_pe1_ipuc.txt
    │   ├── S1-TC04_pe1_nexthop.txt
    │   ├── S1-TC06_pe2_ilm.txt
    │   └── S1-TC07_pe2_pkt-trace.txt
    ├── 04_日志/
    │   ├── pe1_dmesg.txt
    │   ├── pe2_dmesg.txt
    │   └── zebra.log
    └── 05_报告/
        └── L3VPN_综合验证报告.md
```

### 21.2 单用例记录模板

| 字段 | 内容 |
|---|---|
| 用例编号 | S1-TC06 |
| 执行日期/时间 | |
| 执行人 | |
| 设备/版本 | PE2 / FRR x / SDK x |
| 前置条件 | S1-TC01 通过 |
| 步骤 | 1… 2… |
| 实际结果 | |
| 期望结果 | L3VPN ILM、vrf_id=8、innerPacketLookup=1 |
| 判定 | PASS / FAIL / BLOCKED |
| 证据文件 | `03_芯片/S1-TC06_pe2_ilm.txt` |
| 备注 | |

### 21.3 报告结论模板

```
1. 测试范围与版本
2. 拓扑与地址
3. 场景执行结果汇总表（S1–S12）
4. P0 场景详细结果
5. 硬件落表证据摘要（E1–E6）
6. 发现的问题与限制
7. 结论：是否满足 L3VPN 硬件转发交付条件
8. 附件清单
```

---

## 22. 已知限制与注意事项

1. **`vrf_id` ≠ `table`**：`show vrf` 的 `id` 才是芯片 `vrfId`；`table 100` 只是 Linux 路由表号。硬件验证用错 ID 会得出"表项不存在"的错误结论。
2. **VRF 后端差异**：`show vrf` 第一行有 `netns-based vrfs` 才能用 `interface IFNAME vrf VRF`；否则用平台侧 `ip link set ... master`。`show running-config` 在 vrf-lite 后端不显示 vrf 限定词，属正常，不要据此判断绑定失败。
3. **RFC 8212 默认开启**：所有 eBGP（PE-PE、PE-CE）都必须显式 `no bgp ebgp-requires-policy`，且是 per-instance。
4. **`export vpn` / `import vpn` 是总开关**：RD/RT/label 配得再对，缺这两个开关也不通。
5. **RD 两端必须不同、RT 两端必须相同**：地址重叠时 RD 相同会把两条路由合并。
6. **客户网段绝不能进 underlay**：RIP/OSPF 的 `network` 和 LDP 的 `interface` 都只覆盖核心口。
7. **每 VRF 一个 VPN 标签**：FRR 的 `label vpn export auto` 默认按 VRF 分配（`tovpn_label`），同 VRF 多前缀共用一个标签；不同 VRF 标签不同。不要用"一条前缀一个标签"的预期去核对。
8. **L3VPN ILM 的 `nh_id=TOCPU` 是正常终态**：内层查表由芯片 `innerPacketLookup` 完成，不是"等待 ARP"。把它当失败会误判。
9. **标签栈深度上限 3**：dplane `label_stack_depth` 截断到 3，hw_l3 也按 3 层缓存；L3VPN 典型 2 层，超过 3 层的方案需另行确认。
10. **软件容量上限**：VRF 256、VRF 路由 1024、VRF ILM 512、ECMP 成员 8、每路由 nexthop 8（详见 19.1）。这些是当前实现缓存，不是芯片理论上限。
11. **ECMP 截断告警**：成员超过 `CTC_HAL_MSG_MAX_NH_VRF`（8）时，dplane 只发前 8 条并告警；硬件组会缺臂。
12. **外层 transport 标签 TTL**：当前 ingress L3VPN 双层栈的外层 tunnel label 使用固定 TTL 64（内层 VPN 标签映射报文 TTL）。这不影响转发，但 MPLS 域内逐跳 traceroute 的诊断能力有限；若客户要求 uniform TTL，需要改 `hw_mpls_nh_create_key` 的 tunnel TTL 映射并回归。
13. **explicit-null**：S7 必须单独验证；未通过时生产使用 implicit-null（默认）。
14. **MTU**：两层标签 +8 字节。PC/CE/PE 的 MTU 建议 1526+，或 ICMP 载荷 ≤1400。大包测试前先确认 MTU。
15. **带标签路由跳过内核安装**：dplane 对带真实标签的 VRF 路由设置 `skip_kernel`，因此 PE 上 `ip route` 可能看不到这些路由，属正常；以 FRR `show ip route vrf` 和芯片 IPUC 为准。
16. **stats 删除容错**：`stats detach failed ret=-31993` 是 NOT_EXIST 容错；偶发可接受，持续刷屏且表项泄漏才需排查。
17. **`vni` 与 MPLS L3VPN 无关**：`vrf CUST1 / vni 100` 是 EVPN L3VNI 命令，纯 LDP L3VPN 不要配。
18. **CE 侧 `network vlanifX` 写法**：不同 FRR/平台版本对 `network IFNAME` 支持不同；最稳妥是写网段 `network A.B.C.D/M`。
19. **IPv6 / 跨域是条件项**：先做 14.1 / 15.1 的特性确认，任何一层不支持都记录为"条件不支持"，不要直接判 L3VPN 失败。
20. **修改代码后重跑**：本文的期望值基于当前 `hw_mpls.c` / `hw_l3.c` / `ctc_dplane.c` 行为；PHP、ILM 构造、VRF 映射任一改动后，至少重跑 S1-TC03/TC06/TC07 和 S4。

---

## 附录 A：配置回滚与清理顺序

拆除 L3VPN 时按下面顺序，避免残留：

```
! 1. 停业务/撤客户路由
CE1(config)# no router rip
CE2(config)# no router rip

! 2. 删 VRF BGP
PE1(config)# no router bgp 65001 vrf CUST1
PE2(config)# no router bgp 65002 vrf CUST1

! 3. 删全局 VPNv4 邻居/实例（谨慎，确认无其他 VPN）
PE1(config)# no router bgp 65001
PE2(config)# no router bgp 65002

! 4. 删 PE-CE 路由实例
PE1(config)# no router rip vrf CUST1
PE2(config)# no router rip vrf CUST1

! 5. 删 LDP（确认无其他 LSP 依赖）
PE1(config)# no mpls ldp
P(config)# no mpls ldp
PE2(config)# no mpls ldp

! 6. 解绑接口
ip link set vlanif100 nomaster
ip link set vlanif200 nomaster

! 7. 删 VRF
ip link del CUST1

! 8. 清理静态/策略
PE1(config)# no ip route 10.1.1.0/24 192.168.100.5 vrf CUST1
```

> 删除 VRF 前必须确保没有接口还挂在它下面；否则内核会拒绝删除，zebra 侧也可能留下 stale 表项。

---

## 附录 B：命令速查

### B.1 FRR

| 目的 | 命令 |
|---|---|
| VRF 列表/ID | `show vrf` |
| VRF 接口 | `show interface vrf CUST1` |
| VRF 路由 | `show ip route vrf CUST1 [prefix]` |
| VRF BGP | `show bgp vrf CUST1 ipv4 unicast [prefix]` |
| VPNv4 表 | `show bgp ipv4 vpn [prefix]` |
| VPNv4 邻居 | `show bgp ipv4 vpn summary` |
| 导出路由 | `show bgp ipv4 vpn neighbors <ip> advertised-routes` |
| LDP 邻居 | `show mpls ldp neighbor` |
| LDP 绑定 | `show mpls ldp binding [prefix]` |
| LDP 接口 | `show mpls ldp ipv4 interface` |
| MPLS 表 | `show mpls table [label]` |
| RIP | `show ip rip [vrf CUST1]` |
| RIP 状态 | `show ip rip vrf CUST1 status` |
| OSPF | `show ip ospf neighbor` / `show ip ospf database` |
| 接口 | `show interface vlanifX` |

### B.2 CTC SDK

| 目的 | 命令 |
|---|---|
| MPLS 总览 | `show mpls status` |
| ILM 详情 | `show mpls ilm 0 label <label> detail` |
| IPUC 路由 | `show ipuc <vrf_id> <prefix> <len> detail` |
| IPUC 命中 | `show ipuc <vrf_id> <prefix> <len> hit` |
| IPUC LPM | `show ipuc lpm <vrf_id> all detail` |
| IPv6 IPUC | `show ipuc ipv6 <vrf_id> <prefix> <len> detail` |
| nexthop | `show nexthop <nhid> detail` |
| MPLS tunnel | `show nexthop mpls-tunnel <id> detail` |
| ECMP 组 | `show nexthop all ecmp` |
| nexthop 资源 | `show nexthop status` |
| 丢弃原因 | `show diag drop-info` |
| 报文追踪 | `show diag pkt-trace detail` |
| 内存水位 | `show diag mem-usage sram ip-lpm` / `... mpls` |
| L3 接口 | `show l3if type vlan-if vlan <vid>` |

### B.3 抓包建议

```bash
# PE1 出方向（核心口）
tcpdump -i vlanif10 -nn -e -s 0 -w pe1-egress.pcap 'mpls or icmp'

# P 入/出方向
tcpdump -i vlanif10 -nn -e -s 0 -w p-ingress.pcap 'mpls or icmp'
tcpdump -i vlanif20 -nn -e -s 0 -w p-egress.pcap 'mpls or icmp'

# PE2 入/出方向
tcpdump -i vlanif20 -nn -e -s 0 -w pe2-ingress.pcap 'mpls or icmp'
tcpdump -i vlanif200 -nn -e -s 0 -w pe2-egress.pcap 'mpls or icmp'
```

> vlanif 抓包是否能看到标签取决于平台抓包点；如果 vlanif 抓不到，改在物理口抓（access 口可能去 tag，注意区分）。

---

## 附录 C：术语表

| 缩写 | 全称 | 说明 |
|---|---|---|
| PE | Provider Edge | 运营商边缘设备，L3VPN 的 VRF 所在 |
| P | Provider | 核心设备，只做标签转发，不感知 VPN |
| CE | Customer Edge | 客户边缘设备 |
| VRF | Virtual Routing and Forwarding | 虚拟路由转发实例 |
| RD | Route Distinguisher | 路由区分符，VPNv4 表内唯一性 |
| RT | Route Target | 路由目标，VPN 归属匹配 |
| VPNv4 | BGP VPN IPv4 address family | 承载 VPN 路由的 BGP 地址族 |
| ILM | Incoming Label Map | 入标签映射 |
| NHLFE | Next Hop Label Forwarding Entry | 下一跳标签转发项 |
| FTN | FEC-to-NHLFE | FEC 到 NHLFE 的映射（ingress 压标签） |
| PHP | Penultimate Hop Popping | 倒数第二跳弹标签 |
| LDP | Label Distribution Protocol | 标签分发协议 |
| IPUC | IP Unicast | 芯片 IP 单播路由表 |
| LPM | Longest Prefix Match | 最长前缀匹配 |
| ALPM | Algorithmic LPM | 算法化最长前缀匹配 |
| BOS | Bottom of Stack | 标签栈底标志 |
| DsMpls | — | 芯片 MPLS 表 |
| DsIpDa | — | 芯片 IP 目的地址表 |
| DsLpmTcamIpv4HalfKey | — | 芯片 IPv4 LPM TCAM 半键表 |

---

## 附录 D：快速自检卡（打印版）

```
【配置完先查这 8 条】
1. show vrf                      → CUST1 id=___ table=___（记下 id）
2. show interface vrf CUST1      → 客户口在不在
3. show ip route vrf CUST1       → 有没有 C / R / B
4. show bgp ipv4 vpn             → 有没有对端前缀 + Label
5. show mpls ldp neighbor        → OPERATIONAL
6. show mpls table               → 对端 lo/32 ILM 在不在
7. show ipuc <id> <prefix> <len> detail → 命中 + push NH
8. show mpls ilm 0 label <L_v> detail  → L3VPN + vrf_id 正确

【抓包四看】
PE1 出： [L_t][L_v][IP]     两层
P   出： [L_v][IP]          一层（implicit-null）
PE2 入： [L_v][IP]          一层
PE2 出： [IP]               无标签

【三个不要】
不要用 table 号当 vrf_id
不要把客户网段写进 underlay
不要把 L3VPN ILM 的 TOCPU 当失败
```

---

**文档结束**

> 本文档的期望值基于当前代码实现与实验室验证结果。若后续修改了 `hw_mpls.c`、`hw_l3.c`、`ctc_dplane.c` 的 PHP/ILM/VRF 映射逻辑，请至少重跑 S1、S4、S7，并更新第 17、22 章。

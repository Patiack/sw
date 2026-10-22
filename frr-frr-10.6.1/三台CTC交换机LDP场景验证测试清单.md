# 三台 CTC 交换机 LDP 基础 LSP 场景验证测试清单

- 版本：v1.5（新增 11.5 RD / RT 语义速查 —— 逐条解释 VPN 侧 BGP 配置块并说明两端 RD 为何不同、RT 为何相同；新增 11.8 PE-CE 改用 eBGP 的变体配置；原 11.5 / 11.6 顺延为 11.6 / 11.7，并修复 11.7 表格被引用块截断的排版问题）
- 版本：v1.4（修正接口进 VRF 的写法：`interface IFNAME vrf VRF` 而非接口下 `vrf NAME`，新增缺陷 7 与 11.4 ⓪ VRF 绑定前置检查）
- 版本：v1.3（第十一节 L3VPN 配置重写：补齐 `export vpn`/`import vpn`、`no bgp ebgp-requires-policy`、VRF 内 RIP 实例与 `redistribute bgp`；修正客户网段误入 underlay、移除无关 `vni`）
- 版本：v1.2（新增 PC1/PC2 端到端数据面；修正 ldpd `interface` 命令缺失）
- 被测对象：三台 CTC 交换芯片设备（物理口二层 + vlanif 三层）+ 两台 PC，RIP 路由 + LDP 标签分发，基础 LSP（PUSH/SWAP/PHP/POP）全链路验证
- 链路：ldpd（mpls ldp）→ zebra（zebra_mpls.c）→ ctc_dplane.c（CTC-HW provider）→ hsl_mpls.c → hw_mpls.c → ctcs_mpls_* → 芯片
- 验证目标：
  1. LDP 动态标签分发链路全通（三台设备均硬件落表）
  2. PC1↔PC2 端到端数据面：R1 PUSH → R2 PHP（pop-and-continue，`pop=1, decap=0`）→ R3 egress 收 IP 后 L3 转发
  3. explicit-null 配置下 R2 为 SWAP（压 label 0）
  4. SWAP 两步模型落表证据（TUNNEL_ID=NHID、REF_CN=1、VC_LABEL 硬件回读）
  5. ARP 占位/升级、stats 生命周期（含 -31993 删除容错）、故障收敛、PC 流量统计联动

---

## 一、拓扑与地址规划

```
        VLAN 10 / 192.168.12.0/24       VLAN 20 / 192.168.23.0/24
 PC1 ──── R1(CTC,ingress) ────────── R2(CTC,transit,被测) ────────── R3(CTC,egress) ──── PC2
 VLAN100/192.168.100.0/24            eth0: vlan10 / eth1: vlan20    VLAN200/192.168.200.0/24
```

| 设备 | 角色 | 三层接口 | 说明 |
|---|---|---|---|
| PC1 | 流量源（接入侧） | 192.168.100.10/24，GW 192.168.100.1 | 普通 PC，不跑 RIP/LDP |
| R1 | ingress / PUSH | vlanif10: 192.168.12.1/24、vlanif100: 192.168.100.1/24、lo 1.1.1.1/32 | PC1 网关；对 PC2 网段压标签（FTN） |
| R2 | transit（被测） | vlanif10: 192.168.12.2/24、vlanif20: 192.168.23.2/24、lo 2.2.2.2/32 | PHP/SWAP/POP/ECMP 全部被测动作 |
| R3 | egress | vlanif20: 192.168.23.3/24、vlanif200: 192.168.200.3/24、lo 3.3.3.3/32 | PC2 网关；通告标签（默认 implicit-null） |
| PC2 | 流量宿（接入侧） | 192.168.200.10/24，GW 192.168.200.3 | 普通 PC，不跑 RIP/LDP |

**协议分工**：RIP 保证全网路由（lo 与 PC 网段全部通告，LDP transport 依赖），LDP 自动分发标签；PC 只做静态 IP + 默认网关，不参与路由协议。

**PC 接入方式**：PC 发 untagged 帧，接入 R1/R3 的 access 口（vlan 100/200），R1/R3 侧三层在 vlanif100/200 上终结。

## 二、前置条件

- [ ] 三台均为 CTC 交换芯片设备，芯片/SDK 正常（CTC_CLI 可用）
- [ ] vlanif10/20/100/200 已创建且 up（系统层或平台工具），zebra 已感知（`show interface vlanifX` 可见）
- [ ] 物理口已划入对应 vlan（access）：R1 eth0→vlan10、eth1→vlan100；R2 eth0→vlan10、eth1→vlan20；R3 eth0→vlan20、eth1→vlan200；R1↔R2、R2↔R3、PC↔R1/R3 二层互通
- [ ] zebra MPLS 已启用：`show mpls status` 显示 yes（无此命令则跳过，定制环境启动自动启用）
- [ ] 三台 RIP 收敛：`show ip route` 可见全部 lo 与 PC 网段（如 R1 有 192.168.200.0/24 via 192.168.12.2、R2 有 192.168.100.0/24 via 192.168.12.1）
- [ ] LDP 会话建立：三台 `show mpls ldp neighbor` 均见直连邻居（transport 为 lo）
- [ ] PC1/PC2 网关可达：`PC1> ping 192.168.100.1`、`PC2> ping 192.168.200.3`
- [ ] 抓包工具：R2 eth0 / eth1 各开 tcpdump；R1 eth0（出方向看 PUSH 标签）、R3 vlanif200（看收尾）；PC 可装 wireshark
- [ ] 对端 ARP 已解析（各设备先 ping 通直连对端，避免首配占位干扰基线）

## 三、系统层准备（非 FRR CLI）

```bash
# R1
ip link add link eth0 name vlanif10 type vlan id 10
ip link add link eth1 name vlanif100 type vlan id 100
# R2
ip link add link eth0 name vlanif10 type vlan id 10
ip link add link eth1 name vlanif20 type vlan id 20
# R3
ip link add link eth0 name vlanif20 type vlan id 20
ip link add link eth1 name vlanif200 type vlan id 200
# 物理口划 vlan（按平台实际方式，zebra 经 netlink 感知）
ip link set eth0 master <bridge> && bridge vlan add vid 10 dev eth0
# ...（每台设备的接入/互联口按 vlan 表逐一配置）
```

> FRR 10.6 无 `switchport access vlan` / `vlan 10` 创建 CLI（VLAN 配置命令已移除）；vlanif 由系统/平台预建，zebra 通过 netlink 感知（`if_netlink.c`：kind="vlan" → ZEBRA_IF_VLAN）。ctc_dplane 的 ifindex 换算按 `vlanifX` 接口名解析 vid（`0x06000000 | vid` = PORT_TYPE_VLANIF）。

## 四、设备配置（FRR 标准 CLI）

### R1

```
! zebra
interface vlanif10
 ip address 192.168.12.1/24
!
interface vlanif100
 ip address 192.168.100.1/24
!
interface lo
 ip address 1.1.1.1/32
!
! ripd
router rip
 version 2
 network 192.168.12.0/24
 network 192.168.100.0/24
 network 1.1.1.1/32
!
! ldpd（入口命令 = mpls ldp，非 router ldp）
mpls ldp
 router-id 1.1.1.1
 address-family ipv4
  discovery transport-address 1.1.1.1
  interface vlanif10
  interface vlanif100
  exit-address-family
 exit
!
```

### R2（被测）

```
interface vlanif10
 ip address 192.168.12.2/24
!
interface vlanif20
 ip address 192.168.23.2/24
!
interface lo
 ip address 2.2.2.2/32
!
router rip
 version 2
 network 192.168.12.0/24
 network 192.168.23.0/24
 network 2.2.2.2/32
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
```

### R3

```
interface vlanif20
 ip address 192.168.23.3/24
!
interface vlanif200
 ip address 192.168.200.3/24
!
interface lo
 ip address 3.3.3.3/32
!
router rip
 version 2
 network 192.168.23.0/24
 network 192.168.200.0/24
 network 3.3.3.3/32
!
mpls ldp
 router-id 3.3.3.3
 address-family ipv4
  discovery transport-address 3.3.3.3
  interface vlanif20
  interface vlanif200
  exit-address-family
 exit
!
```

### PC1 / PC2（静态配置，不跑路由协议）

```
PC1: IP 192.168.100.10/24    GW 192.168.100.1    （接 R1 eth1，access vlan 100）
PC2: IP 192.168.200.10/24    GW 192.168.200.3    （接 R3 eth1，access vlan 200）
```

> 说明：
> - zebra 无 `mpls enable` 命令（YANG/NB 命令在定制环境不可用）；zebra MPLS 启动时自动启用（`zebra_mpls_init` → `mpls_kernel_init` 成功 → `zebra_mpls_turned_on`）
> - `interface IFNAME` 必须在 `address-family ipv4` 下显式配置，否则 `enabled = 0`，该接口不发 Hello、不建邻居（`no interface IFNAME` 才是排除）。PC 侧 vlanif100/200 也配置 `interface` 后 ldpd 会发 Hello（对端无回应不影响），PC 网段作为直连 FEC 正常通告
> - `router-id` 在 LDP 节点下；`discovery transport-address` 在 address-family ipv4 下
> - RIP 通告 PC 网段后，LDP 会把 192.168.100.0/24、192.168.200.0/24 作为 FEC 分发标签（见第五章）

## 五、LDP 默认标签行为（决定测试预期）

ldpd 标签通告规则（lde.c `lde_update_label`）：**直连 FEC 通告 implicit-null（label 3），非直连 FEC 分配真实标签**。

| FEC | 通告方 → 接收方 | 标签 | 接收方动作 |
|---|---|---|---|
| 3.3.3.3/32 | R3（直连）→ R2 | implicit-null | R2 = **PHP** |
| 3.3.3.3/32 | R2（非直连）→ R1 | 真实标签 L | R1 = **PUSH** |
| **192.168.200.0/24（PC2 网段）** | R3（直连）→ R2 | implicit-null | R2 = **PHP**（端到端主路径） |
| **192.168.200.0/24** | R2（非直连）→ R1 | 真实标签 L | R1 = **PUSH**（端到端主路径） |
| 192.168.100.0/24（PC1 网段） | R1（直连）→ R2 | implicit-null | R2 = **PHP**（反向流量） |
| 192.168.100.0/24 | R2（非直连）→ R3 | 真实标签 | R3 = **PUSH**（反向流量） |

**默认端到端路径（PC1 → PC2）**：
```
PC1 ──(无标签 IP)──> R1 [FTN PUSH 压 L] ──(L)──> R2 [PHP: pop=1,decap=0 弹标签]
   ──(无标签 IP)──> R3 [查路由 → vlanif200] ──(无标签 IP)──> PC2
```
**默认 R2 是 PHP；SWAP 需 R3 配 explicit-null（TC-03）或加 R4（TC-04）**。R2 弹标签后内层目的（192.168.200.10）非本机，pop-and-continue 按 FEC nexthop 转发给 R3，R3 做普通 L3 转发——这正是 PHP 语义在真实 PC 流量上的完整验证。

## 六、测试用例

### TC-01：端到端 PHP（核心，PC1 → PC2）

| 项 | 内容 |
|---|---|
| 目的 | 验证 LDP 全链路 + PHP：PC1 → R1 PUSH → R2 PHP（pop-and-continue）→ R3 egress → PC2 |
| 前置 | 前置条件全过，ARP 已解析 |

**步骤**：
1. PC1 持续发包：`PC1> ping 192.168.200.10 -n 100`（或持续流量/iperf）
2. R2 eth0 / eth1、R1 eth0、R3 vlanif200 同时抓包
3. 检查 R2 `show mpls table` 与 SDK dump

**预期结果**：

| 检查点 | 预期 |
|---|---|
| R1 eth0 出 | 带 MPLS 标签（FTN PUSH，R2 通告的真实标签 L） |
| R2 eth0 入 / eth1 出 | 入带标签 L；出**无标签 IP**（PHP 生效，implicit-null 标准语义） |
| R2 `show mpls table` | 192.168.200.0/24 → LDP，出标签 **implicit-null** |
| R2 `show ip route 192.168.200.0/24` | via 192.168.23.3（vlanif20，RIP 1 跳） |
| SDK `show mpls ilm` | 192.168.200.0/24 对应 ILM：`pop=1, decap=0`（非 decap=1） |
| R3 vlanif200 出 | 无标签 IP，目的 192.168.200.10 → PC2 |
| PC2 | ping 100% 通，ICMP 应答回程对称（反向同为 PHP，见 TC-02） |

**判定**：PC1↔PC2 全通 + R2 eth1 出无标签 + ILM `pop=1, decap=0` = PASS；eth1 仍带标签 = R3 通告了真实标签（配置被改）；ILM `decap=1` = pop-and-continue 修正未生效；R1 出无标签 = FTN 未硬件化（见注意事项 1）。

### TC-02：反向流量（PC2 → PC1，对称验证）

| 项 | 内容 |
|---|---|
| 步骤 | `PC2> ping 192.168.100.10 -n 100`，R2 eth0/eth1 抓包 |
| 预期 | R2 上 192.168.100.0/24 ILM 出标签 implicit-null（R1 直连通告）→ R2→R1 方向无标签 IP，R1 转发 PC1 |
| 判定 | 双向 PHP 对称 = PASS；单方向无标签 = FEC nexthop 方向性错误 |

### TC-03：SWAP（R3 通告 explicit-null）

| 项 | 内容 |
|---|---|
| 目的 | 默认配置下 R2 对 192.168.200.0/24 收不到真实标签（R3 直连恒通告 null）；配 explicit-null 后 R2 变 SWAP（压 label 0） |
| 前置 | TC-01 通过 |

**步骤**：
```
R3(config)# mpls ldp
R3(config-ldp)# address-family ipv4
R3(config-ldp-af)# label local advertise explicit-null
```

**预期结果**：

| 检查点 | 预期 |
|---|---|
| R2 `show mpls table` | 192.168.200.0/24 出标签 **IPv4 Explicit Null**（zebra 判定 ≠ implicit-null → SWAP） |
| R2 eth1 抓包 | PC1→PC2 流量带 **label 0**（S=1，EXP 保留）的 MPLS 报文 |
| SDK `show nexthop <nhid>` | VC_LABEL=**0**（芯片硬件回读出标签），TUNNEL_ID=NHID（两步模型绑定） |
| SDK `show nexthop mpls-tunnel <id>` | REF_CN=**1**、VLAN=20、L3IFID 对应 vlanif20、MACDA=R3 的 MAC、LSP=0 |
| R3 | 收到 label 0 → pop → 查路由 → PC2（流量仍通） |
| dmesg | 无 `SWAP nh create failed`（-31993/-31995 回归） |
| PC2 tracert | `PC2> tracert -d 192.168.100.10`：R2 不出现（explicit-null 时 TTL 不递减，RFC 3032） |

**判定**：R2 eth1 出 label 0 + tunnel REF_CN=1 + VC_LABEL=0 回读 = PASS（SWAP 两步模型 + vlanif 出口完整落表）。**测完恢复 `no label local advertise explicit-null`**。

### TC-04：真实标签 SWAP（加 R4，可选）

| 项 | 内容 |
|---|---|
| 目的 | explicit-null 出标签为 0，非真实标签；加 R4 后 R3 对 4.4.4.4 通告真实标签，R2 为真实标签 SWAP |
| 前置 | TC-03 通过；R4 接入 |

**拓扑扩展**：R4 接 R3（如 vlan 40 / 192.168.40.0/24），lo 4.4.4.4/32 通告 RIP+LDP。

**预期**：
- R3 对 4.4.4.4（非直连）分配真实标签
- R2 的 4.4.4.4 ILM：出标签 = R3 通告的真实标签（SWAP）
- PC1 ping 4.4.4.4：完整 PUSH → R2 SWAP → R3 SWAP/POP → R4，全链路带标签
- SDK `show nexthop <nhid>`：VC_LABEL = 真实标签值（非 0）
- TTL 递减：SWAP 时 TTL-1（tracert 可见 R2 一跳；对比 TC-01/TC-03 中 R2 不出现）

**判定**：R2 eth1 出真实标签 + 端到端可达 = 真实标签 SWAP 全链路 PASS。

### TC-05：LDP 会话与绑定核对

| 检查 | 命令 | 预期 |
|---|---|---|
| 会话 | `R2# show mpls ldp neighbor` | R1 与 R3 两个邻居（TCP 基于 lo transport） |
| 绑定 | `R2# show mpls ldp binding 192.168.200.0/24` | 本地标签 + R3 通告 implicit-null + R1 通告真实标签 |
| 接口 | `R2# show mpls ldp ipv4 interface` | vlanif10/vlanif20 均 enabled |
| ILM | `R2# show mpls table` | 192.168.200.0/24 LDP，nexthop 指向 R3（RIP 最佳路径） |

**判定**：绑定表与 ILM 一致 = PASS；nexthop 指向 R1（备路径）= RIP 选路异常。

### TC-06：ARP 占位 → 升级

| 项 | 内容 |
|---|---|
| 目的 | 验证 vlanif 场景 ARP 占位（TOCPU）与 `hw_mpls_arp_resolved` 自动升级 |
| 前置 | TC-01 通过 |

**步骤**：
1. `R2# ip neigh del 192.168.23.3 dev vlanif20`（**不 ping 对端**）
2. 重建 LSP 触发重新下发：`R2# clear mpls ldp neighbor 3.3.3.3`（或 `no mpls ldp` 再配）
3. zebra 日志确认占位：`CTC-DPLANE: MPLS LSP installed in ARP placeholder mode`，SDK ILM nh_id=TOCPU
4. `R2# ping 192.168.23.3 -c 3` 触发 ARP（在 vlanif20 上解析）
5. HSL 日志确认 `hw_mpls_arp_resolved` 升级，SDK ILM nh_id 变为真实值；PC1→PC2 流量恢复

**判定**：占位 → ping → 自动升级 = PASS；升级失败 = ARP 回调链路异常。

### TC-07：故障收敛（LSP 生命周期）

| 项 | 内容 |
|---|---|
| 前置 | TC-01 通过 |

**步骤**：
1. `R3# interface vlanif20 shutdown`（或拔线）
2. 观察 R2：RIP 路由消失 → LDP **label withdraw** → 192.168.200.0/24 ILM 删除（`show mpls table` 消失）
3. PC1 ping PC2 中断（预期），R1 收到 withdraw 后删除自己的 LSP
4. dmesg 确认 **无 `stats detach failed ... ret=-31993`**（NOT_EXIST 容错生效，stats 已随 ILM 删除释放）
5. 恢复 vlanif20 → RIP/LDP 重建 → LSP 自动恢复 → PC1 ping PC2 自动恢复

**判定**：撤销 → 释放（无 -31993、stats 无泄漏）→ 恢复自动重建 = PASS。

### TC-08：删除与释放（stats 泄漏检查）

**步骤**：
1. `R2# no mpls ldp` → 所有 LDP ILM 删除，SDK nexthop/tunnel 释放
2. 循环 `mpls ldp` / `no mpls ldp` 5 次 → SDK stats 表数量不增长（无 id 泄漏）
3. 恢复 `mpls ldp` 后 LSP 自动重建，PC1 ping PC2 恢复

**判定**：资源全释放 + stats 无泄漏 = PASS。

### TC-09：ECMP（可选，需双链路）

| 项 | 内容 |
|---|---|
| 拓扑扩展 | R2 加 eth2 access vlan 30 + vlanif30（如 192.168.30.2/24）；R3 加 eth1 192.168.30.3/24；两网段均进 RIP/LDP |
| 预期 | RIP 等开销装两条路由（FRR 默认 multipath）→ LDP 为 192.168.200.0/24 装两个 NHLFE → zebra `show mpls table` 两条路径 → HSL 建 **ECMP 组** → PC1→PC2 流量经两条链路（逐包哈希），两条链路均抓到标签流量 |
| 判定 | 双链路分流 + SDK ECMP 组存在 = PASS；单链路 = ECMP 构建失败 |

### TC-10：PC 流量 stats 联动（新增）

| 项 | 内容 |
|---|---|
| 目的 | 验证 ILM stats_id 随 PC 真实流量计数，与 `-31993` 删除容错、USER 统计模式兼容 |
| 前置 | TC-01 通过 |

**步骤**：
1. 记录 R2 SDK stats 基线（ILM 192.168.200.0/24 的 stats_id 计数 = 0）
2. `PC1> ping 192.168.200.10 -n 1000 -l 1400`（或 iperf 60s）
3. 观察 R2 SDK stats：计数随流量持续增长，与发送包数一致（入方向统计含标签帧）
4. 停止流量 → 计数冻结；再启 → 继续累计
5. `R3# interface vlanif20 shutdown` 再恢复 → stats 随 ILM 删除/重建，无 -31993、计数清零重建

**判定**：计数 = 实发包数、停流冻结、故障重建无泄漏 = PASS。

---

## 七、验证命令汇总

| 层 | 命令 | 关注点 |
|---|---|---|
| PC | `ping -n 100 -l 1400` / `tracert -d` | 端到端连通、路径（R2 是否出现）、大包（配合 MTU 调整） |
| PC | wireshark / tcpdump（PC 网卡） | 收包无标签、TTL、往返时延 |
| ldpd | `show mpls ldp neighbor / binding / discovery / ipv4 interface` | 会话、标签映射（含 PC 网段 FEC）、接口启用 |
| zebra | `show mpls table` | ILM 类型（LDP）、出标签（implicit-null / 0 / 真实值） |
| zebra | `show ip route` | RIP 路径（含 192.168.100.0/24、192.168.200.0/24）与 LSP 下一跳一致性 |
| zebra | `show mpls status` | MPLS 启用状态（无此命令则跳过） |
| HSL | dmesg / printk | `LSP_ADD ret 0`、占位升级、无 -31993/-31995 |
| SDK | `show mpls ilm` | pop / decap / nh_id / stats_id |
| SDK | `show nexthop <nhid>` | 两步模型：TUNNEL_ID=NHID、VC_LABEL 硬件回读 |
| SDK | `show nexthop mpls-tunnel <id>` | REF_CN、VLAN、L3IFID、MACDA、LSP |
| 抓包 | R2 eth0/eth1、R1 eth0、R3 vlanif200 tcpdump | TC-01 出无标签、TC-03 出 label 0、TC-04 出真实标签、TTL 递减 |

## 八、已知限制与注意事项

1. **FTN（ingress PUSH）**：PC1→PC2 的压标签走 labeled route（FTN）路径——若 R1 抓包发现发出的报文无标签（未硬件化），说明 FTN 通路未接入 ctc_dplane，改用 scapy 在 R1 注入 `[label] + IP` 测 R2（与静态 LSP 验证方式一致），并另行跟踪 FTN。
2. **MTU**：MPLS 压标签 +4 字节（帧 +4），PC 侧默认 MTU 1500 时 `ping -l 1472`（Linux `-s 1472`）会因 1504 超限被丢弃。建议：vlanif/PC 网卡 MTU 统一调至 1526（或 1600），或 ICMP 载荷控制在 1400 以内（`-l 1400`）。**大包测试前必须先确认 MTU**。
3. **tracert 与 TTL 语义（PHP/SWAP 判别辅助）**：出标签为 implicit-null / explicit-null 时 R2 不递减 TTL（RFC 3032），tracert 不显示 R2；真实标签 SWAP 时 TTL-1，tracert 可见 R2。PC 侧 tracert 结果本身就是 PHP/SWAP 的数据面证据。
4. **vlanif 与 VPWS 的差异**：VPWS 的 AC 口必须二层口（HSL 拒绝 PORT_TYPE_VLANIF），但基础 LSP 的 nexthop 走 vlanif 完全正常——两场景不冲突。
5. **pop-and-continue 行为**：HSL 的 PHP ILM 弹标签后按 FEC nexthop 转发（continuing 查表 miss 走 FEC），不查本地 IP 路由；egress 收尾用 PHP（TC-01）最干净，POP ILM 收尾会绕回上游一次。
6. **静态 LSP 回归**：本清单验证后建议回归静态 LSP 场景（`mpls lsp 100 via <gw> 200`，参考《基础LSP下发逻辑梳理》），确认动态/静态路径互不影响。
7. **RIP 与 LDP 的收敛耦合**：RIP 切换路径（如 R3 断开后走备路径）时 LDP 跟随 IGP 重新选下一跳——直线拓扑无备路径，收敛即撤销；三角形拓扑注意可能形成转发环路（属拓扑固有）。
8. **抓包注意**：access 口出方向去 tag，R1/R3/PC 侧抓到的是 untagged 帧；芯片内部按 vlanif 携带 vid 处理，SDK `show nexthop mpls-tunnel` 的 VLAN 字段是落表证据。
9. **PC 网段进入 LDP FEC**：RIP 通告 PC 网段后，LDP 会为 192.168.100.0/24、192.168.200.0/24 建立 FEC（直连方通告 implicit-null）——这是 TC-01 端到端 PHP 的基础；若 RIP 未通告（如 PC 网段用静态路由替代），LDP 仍会为直连网段分配 FEC，但非直连侧无路由不建 ILM，行为不同。

---

## 九、通过标准汇总

| 用例 | 判定条件 | 结果（√/×/NA） |
|---|---|---|
| TC-01 | PC1↔PC2 ping 100% 通；R1 出带标签、R2 eth1 出无标签 IP；ILM `pop=1, decap=0` | |
| TC-02 | 反向流量同样 PHP，双向对称 | |
| TC-03 | R2 出 label 0；tunnel REF_CN=1；VC_LABEL=0 回读；tracert 不见 R2 | |
| TC-04 | 真实标签 SWAP 全链路（四台 ICMP 通）；tracert 见 R2（TTL 递减） | |
| TC-05 | LDP 会话/绑定（含 PC 网段）/ILM 三者一致 | |
| TC-06 | vlanif ARP 占位 → 自动升级 → 流量恢复 | |
| TC-07 | 收敛撤销无 -31993；恢复自动重建；PC 流量恢复 | |
| TC-08 | 资源释放、stats 无泄漏（循环 5 次） | |
| TC-09 | ECMP 双链路分流 + SDK ECMP 组 | |
| TC-10 | PC 流量 stats 计数一致、停流冻结、故障重建无泄漏 | |

**整体结论**：
- TC-01/TC-02 = LDP 全链路 + PHP（pop-and-continue）端到端数据面验证完成（PC 真实流量）
- TC-03/TC-04 = SWAP 两步模型（tunnel + push）与 vlanif 出口落表验证完成
- TC-10 = 真实流量 stats 联动验证完成
- TC-06/TC-07/TC-08 = ARP 占位升级、故障收敛、stats 生命周期回归完成
- TC-09 = ECMP 多路径（视拓扑可选）

---

## 十、日志与证据收集

- PC：ping 统计（丢包/时延）、tracert 输出、PC 侧抓包（标注测试编号）
- ldpd：`show mpls ldp neighbor / binding` 输出（含 PC 网段 FEC）
- zebra：`show mpls table`、`show ip route`、`show logging`（CTC-DPLANE 日志）
- HSL：dmesg（`LSP_ADD ret 0`、占位/升级、`stats attach/detach`）
- SDK：`show mpls ilm`、`show nexthop <nhid>`、`show nexthop mpls-tunnel <id>`、stats dump（TC-10 前后基线）
- 抓包：R2 eth0/eth1 pcap（标注测试编号）；TC-03 记录 label 0、TC-04 记录真实标签值与 TTL
- 修改前后各保存一份基线（代码 diff + 行为差异）


## 十一、L3VPN over MPLS 测试场景（RFC 4364，LDP underlay）

### 11.1 结论：原配置**不能**完成 L3VPN —— 已定位 4 个致命缺陷 + 2 处必须清理项 + 1 处静默失效的语法陷阱

原配置的 RD / RT / label **语法本身是对的**（`rd vpn export`、`route-target vpn import|export`、`label vpn export auto` 在 FRR 10.6.1 均有效），但缺了让 VRF 路由真正"进出 VPN"的开关，加上 PE-CE 侧路由根本不通，端到端一定失败。

**致命缺陷（4 个）：**

| # | 位置 | 问题 | 代码证据 | 后果 |
|---|---|---|---|---|
| 1 | `router bgp 65001 vrf CUST1`（PE1 / PE2 同） | 缺 `export vpn` / `import vpn` | `bgpd/bgp_mplsvpn.h:114` `vpn_leak_to_vpn_active()` 要求 `BGP_CONFIG_VRF_TO_MPLSVPN_EXPORT` 置位，否则返回 0（原因串 `"export not set"`）；`:203` `vpn_leak_from_vpn_active()` 要求 `BGP_CONFIG_MPLSVPN_TO_VRF_IMPORT`（`"import not set"`）。这两个 flag **只**由 `bgp_vty.c:11206` 的 `DEFPY(bgp_imexport_vpn, ..., "[no] <import\|export>$direction_str vpn")` 设置 | **最致命**：VRF 路由永不导出到 VPNv4，对端永不导入 → 一条 L3VPN 路由都没有 |
| 2 | `router bgp 65001`（全局实例） | PE1↔PE2 是 eBGP，但没配任何策略 | `bgp_vty.c:98` `FRR_CFG_DEFAULT_BOOL(BGP_EBGP_REQUIRES_POLICY, ...{ .val_bool = true })` → FRR 10.6.1 **默认开启** RFC 8212；`bgp_route.c:2586`（出方向 `subgroup_announce_check`）与 `:5816`（入方向 `bgp_update_receive`）在 eBGP 且无 route-map / prefix-list / filter-list / distribute-list 时分别 `return false`（不发）与 `goto filtered`（丢弃） | **致命**：VPNv4 路由发不出去也收不进来；日志刷 `EBGP outbound policy not properly setup` 告警 |
| 3 | PE1 / PE2 | `redistribute rip` 指向的 RIP 进程不在 CUST1 里 | `ripd/rip_interface.c:874` `FOR_ALL_INTERFACES(rip->vrf, ifp)` —— ripd 实例只看到**本 VRF** 的接口；vlanif100 / vlanif200 属于 CUST1（前提是缺陷 7 已修好），默认 VRF 的 `router rip` 根本看不见它们 | **致命**：CUST1 内没有 RIP，PE 学不到 CE 网段，`redistribute rip` 无源可分发 |
| 4 | PE1 / PE2 | VRF 的 RIP 里没有 `redistribute bgp` | `ripd/rip_cli.c:471` `[no] redistribute <...\|bgp\|...> [{metric (0-16)\|route-map RMAP_NAME}]`；RIP 只通告自己学到的路由，**不会**自动带出 BGP 导入的 VPN 路由 | **致命**：反方向不通 —— CE 永远学不到对端站点 |

**必须清理（2 处，不致命但语义错误）：**

| # | 位置 | 问题 | 说明 |
|---|---|---|---|
| 5 | PE1 的 `router rip / network 192.168.100.0/24`；PE1 的 `mpls ldp / interface vlanif100`（PE2 的 200 同理） | 客户口被塞进了 underlay RIP / LDP | 这两条在默认 VRF 实例里都是**无效语句**：ldpd 通过 `ldpd/ldp_vty_conf.c:762` `if_lookup_name(vty_conf, ifname)` 只在**本实例所属 VRF** 的接口表里查，查不到就 `if_new()` 建一个永远跑不起来的占位。L3VPN 设计上客户口本来就不该跑 IGP/LDP，直接删除 |
| 6 | PE1 / PE2 的 `vrf CUST1 / vni 100 / exit-vrf` | `vni` 与 MPLS L3VPN 无关 | `vni` 是 zebra 的 **EVPN L3VNI** 命令（`zebra/zebra_cli.c:2449` `DEFPY_YANG(vni_mapping, ..., "[no] vni ![...]")`，写 yang 的 `l3vni-id`），给 VXLAN/EVPN L3 网关用；纯 LDP L3VPN 不需要 |

**语法陷阱（1 处，静默失效 —— 前 4 个致命缺陷全改对了，也会因为这一条完全不通）：**

| # | 位置 | 问题 | 代码证据 | 后果 |
|---|---|---|---|---|
| 7 | PE1 的 `interface vlanif100` / PE2 的 `interface vlanif200` 下面的 `vrf CUST1` | **FRR 10.6.1 里没有"接口下配 vrf"这条命令。** VRF 是**接口选择器的一部分**，正确写法是 `interface vlanif100 vrf CUST1` | `doc/user/zebra.rst:157` `interface IFNAME vrf VRF`；`:515-517` "The VRF interfaces can be configured by entering in interface configuration mode ``interface IFNAME vrf VRF``"；`lib/if.c:1271` `DEFUN` 的 cmd 串是 `"interface IFNAME [vrf NAME$vrf_name]"`，netns 后端下 xpath 为 `/frr-interface:lib/interface[name='VRFNAME:IFNAME']`（`:1293-1295`）；**决定性证据**：`lib/if.c:1861-1865` 中 `/frr-interface:lib/interface/vrf` 的 northbound 回调**只有 `.get`**，没有 `.modify` / `.create` —— 整个 FRR 里没有任何一条 CLI 能去写这个叶子 | **最隐蔽**：vlanif100/200 实际留在 default VRF，zebra 的 CUST1 RIB 里连 `connected` 都建不出来。现象是 `show ip route vrf CUST1` **完全为空**、`show ip rip vrf CUST1` 完全为空、`show ip rip vrf CUST1 status` 的 `Interface` 表为空且 `Routing Information Sources` 为空（`ripd/ripd.c:3201-3227` 遍历 `FOR_ALL_INTERFACES(rip->vrf, ifp)` 后按 `!ri->running` 全部跳过） |

> **缺陷 7 与 VRF backend 强相关。** `show vrf` 的输出第一行若为 `netns-based vrfs`（`zebra/zebra_vty.c:2741-2742`），则 zebra 以 netns 后端运行（`-n` / `--vrfwnetns`），用 `interface IFNAME vrf VRF` 即完成绑定（Netlink 会把接口建到该 netns 并挂进 VRF）。
> 否则是 **Linux VRF（vrf-lite）** 后端（默认值，见 `doc/user/zebra.rst:519-525`），此时接口归属由**内核**决定、zebra 经 netlink 学习（`zebra/interface.c:810-820` `zebra_interface_vrf_update_del/add`），CLI 里的 `vrf` 限定词在非 netns 分支里**没有被使用**（`lib/if.c:1296-1299` 的 xpath 只有 `name='ifname'`；`lib_interface_create()` 也仅在 netns 分支才 `netns_ifname_split()` 拆出 VRF，见 `lib/if.c:1550-1586`）。这种后端下必须先在平台侧把接口挂到 CUST1 这个 VRF master 设备下（等价于 `ip link set vlanif200 master CUST1`）。

> **路由"接力"必须四段齐备，缺一段端到端就不通：**
> ① PE-CE 本地 → BGP：`router bgp ... vrf CUST1 / redistribute rip`
> ② VRF BGP → VPNv4（本端）：`export vpn` + `rd vpn export` + `route-target vpn export`
> ③ VPNv4 → VRF BGP（对端）：`import vpn` + `route-target vpn import`
> ④ BGP → PE-CE 本地：`router rip vrf CUST1 / redistribute bgp metric N`

### 11.2 拓扑与地址规划

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

| 设备 | 角色 | 接口 | 地址 | VRF | 所属域 |
|---|---|---|---|---|---|
| PC1 | 客户主机 | 网卡 | 10.1.1.10/24，GW 10.1.1.1 | — | CUST1 |
| CE1 | 客户边缘 | vlanif101 | 10.1.1.1/24 | default | CUST1 |
| | | vlanif100 | 192.168.100.2/24 | default | PE-CE |
| **PE1** | PE（CTC，R1 复用） | lo | 1.1.1.1/32 | default | underlay |
| | | vlanif10 | 192.168.12.1/24 | default | underlay |
| | | vlanif100 | 192.168.100.1/24 | **CUST1** | PE-CE |
| **P** | P（CTC，R2 复用，被测） | lo | 2.2.2.2/32 | default | underlay |
| | | vlanif10 / vlanif20 | 192.168.12.2/24 / 192.168.23.2/24 | default | underlay |
| **PE2** | PE（CTC，R3 复用） | lo | 3.3.3.3/32 | default | underlay |
| | | vlanif20 | 192.168.23.3/24 | default | underlay |
| | | vlanif200 | 192.168.200.3/24 | **CUST1** | PE-CE |
| CE2 | 客户边缘 | vlanif200 | 192.168.200.2/24 | default | PE-CE |
| | | vlanif201 | 10.2.2.1/24 | default | CUST1 |
| PC2 | 客户主机 | 网卡 | 10.2.2.10/24，GW 10.2.2.1 | — | CUST1 |

**三层分工（L3VPN 的核心纪律，务必别搞混）：**

| 域 | 跑什么 | 生效范围 |
|---|---|---|
| Underlay IGP | RIPv2 | **只有** P2P 互联网段 + 三个 lo/32 |
| Underlay LSP | LDP | **只有** PE-P 互联口；客户口一律不跑 |
| Overlay | MP-BGP VPNv4（eBGP 65001↔65002） | 只有 PE1↔PE2；P **不参与** |
| PE-CE | RIPv2（各自 VRF 内的独立实例） | 只有 PE1↔CE1、PE2↔CE2 |
| VPN 标识 | RD `ASN:100`（两端不同）、RT `65001:100`（两端相同） | 只有客户网段 |

> ⚠️ **客户网段（192.168.100.0/24、192.168.200.0/24、10.1.1.0/24、10.2.2.0/24）绝不能出现在 underlay 的 RIP `network` 里，也绝不能出现在 LDP `interface` 里。** 原配置正是把 192.168.100.0/24 / 192.168.200.0/24 写进了 underlay —— 那会把客户路由泄进公网 IGP，是 L3VPN 的设计禁忌，也是本次修改的重点之一。

### 11.3 完整配置

#### PE1（复用 R1）
```
ip link add CUST1 type vrf table 100
ip link set CUST1 up
ip link set vlanif100 master CUST1

!
debug zebra events
debug zebra kernel msgdump
debug zebra mpls detailed
debug zebra dplane detailed
debug zebra nexthop detail
!
debug vrf
!
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
router rip vrf CUST1
 network 192.168.100.0/24
 redistribute bgp metric 2
 version 2
exit
!
router rip
 network 1.1.1.1/32
 network 192.168.12.0/24
 network 192.168.100.0/24
 version 2
exit
!
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp ebgp-requires-policy
 no bgp default ipv4-unicast
 neighbor 3.3.3.3 remote-as 65002
 neighbor 3.3.3.3 ebgp-multihop 3
 neighbor 3.3.3.3 update-source lo
 !
 address-family ipv4 vpn
  neighbor 3.3.3.3 activate
  neighbor 3.3.3.3 next-hop-self
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
  rt vpn both 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
mpls ldp
 !
 address-family ipv4
  discovery transport-address 1.1.1.1
  !
  interface vlanif10
  exit
  !
 exit-address-family
 !
exit
!
end
```
```
hostname PE1
!
! ========== zebra ==========
! 纯 LDP L3VPN 不需要 vni（vni 是 EVPN L3VNI，见 11.1 缺陷 6）
vrf CUST1
 exit-vrf
!
interface vlanif10
 ip address 192.168.12.1/24
!
! VRF 是接口选择器的一部分，不能写成接口子命令（见 11.1 缺陷 7）
! netns 后端：这一条即完成绑定；vrf-lite 后端：还需在平台侧把接口挂到 CUST1 master 下
interface vlanif100 vrf CUST1
 ip address 192.168.100.1/24
!
interface lo
 ip address 1.1.1.1/32
!
! ========== ripd：underlay ==========
! 只通告 PE-P 互联 + loopback；客户网段不在这里（缺陷 5）
router rip
 version 2
 network 192.168.12.0/24
 network 1.1.1.1/32
!
! ========== ripd：PE-CE（CUST1 内独立实例）==========
! 原配置缺这一整段（缺陷 3）+ 缺 redistribute bgp（缺陷 4）
router rip vrf CUST1
 version 2
 network 192.168.100.0/24
 redistribute bgp metric 2
!
! ========== ldpd：只在互联口跑 ==========
! 客户口 vlanif100 不跑 LDP（缺陷 5）
mpls ldp
 router-id 1.1.1.1
 address-family ipv4
  discovery transport-address 1.1.1.1
  interface vlanif10
  exit-address-family
 exit
!
! ========== bgpd：全局实例（MP-BGP VPNv4）==========
! no bgp ebgp-requires-policy 是 eBGP VPNv4 能通的前提（缺陷 2）
router bgp 65001
 bgp router-id 1.1.1.1
 no bgp default ipv4-unicast
 no bgp ebgp-requires-policy
 neighbor 3.3.3.3 remote-as 65002
 neighbor 3.3.3.3 update-source lo
 neighbor 3.3.3.3 ebgp-multihop 2
 !
 address-family ipv4 vpn
  neighbor 3.3.3.3 activate
 exit-address-family
!
! ========== bgpd：VRF 实例（VPN 侧）==========
! export vpn / import vpn 是缺一不可的开关（缺陷 1）
router bgp 65001 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute rip
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
!
line vty
!
```

#### P（复用 R2，被测）

```
hostname P
!
interface vlanif10
 ip address 192.168.12.2/24
!
interface vlanif20
 ip address 192.168.23.2/24
!
interface lo
 ip address 2.2.2.2/32
!
router rip
 version 2
 network 192.168.12.0/24
 network 192.168.23.0/24
 network 2.2.2.2/32
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
line vty
!
```

> P **不需要** VRF、不需要 BGP、不需要 VPNv4——这正是 L3VPN 的价值：核心只做 lo/32 的标签 SWAP/PHP，客户路由（10.x）在 P 上完全不可见。原 P 配置无需改动，仅按"不能有客户网段"的纪律复核了一遍。

#### PE2（复用 R3）
```
ip link add CUST1 type vrf table 100
ip link set CUST1 up
ip link set vlanif200 master CUST1

!
debug zebra events
debug zebra packet detail
debug zebra kernel msgdump
debug zebra rib detailed
debug zebra mpls
debug zebra dplane detailed
debug zebra nexthop detail
debug zebra neigh
debug rip events
debug rip packet
debug rip zebra
!
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
interface vlanif200
 ip address 192.168.200.3/24
exit
!
router rip vrf CUST1
 network 192.168.200.0/24
 redistribute bgp metric 2
 version 2
exit
!
router rip
 network 3.3.3.3/32
 network 192.168.23.0/24
 version 2
exit
!
router bgp 65002
 bgp router-id 3.3.3.3
 no bgp ebgp-requires-policy
 no bgp default ipv4-unicast
 neighbor 1.1.1.1 remote-as 65001
 neighbor 1.1.1.1 ebgp-multihop 3
 neighbor 1.1.1.1 update-source lo
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
  neighbor 1.1.1.1 next-hop-self
 exit-address-family
exit
!
router bgp 65002 vrf CUST1
 !
 address-family ipv4 unicast
  redistribute rip
  label vpn export auto
  rd vpn export 65002:100
  rt vpn both 65001:100
  export vpn
  import vpn
 exit-address-family
exit
!
mpls ldp
 router-id 3.3.3.3
 !
 address-family ipv4
  discovery transport-address 3.3.3.3
  label local advertise explicit-null
  !
  interface vlanif20
  exit
  !
 exit-address-family
 !
exit
!
```
```
hostname PE2
!
! ========== zebra ==========
vrf CUST1
 exit-vrf
!
interface vlanif20
 ip address 192.168.23.3/24
!
interface vlanif200 vrf CUST1
 ip address 192.168.200.3/24
!
interface lo
 ip address 3.3.3.3/32
!
! ========== ripd：underlay ==========
router rip
 version 2
 network 192.168.23.0/24
 network 3.3.3.3/32
!
! ========== ripd：PE-CE（CUST1 内独立实例）==========
router rip vrf CUST1
 version 2
 network 192.168.200.0/24
 redistribute bgp metric 2
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
 neighbor 1.1.1.1 ebgp-multihop 2
 !
 address-family ipv4 vpn
  neighbor 1.1.1.1 activate
 exit-address-family
!
! ========== bgpd：VRF 实例（VPN 侧）==========
! RD 用 65002:100（与 PE1 的 65001:100 不同，保证全局唯一）
! RT 两端都用 65001:100（相同 → 属于同一个 VPN）
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
!
line vty
!
```

#### CE1
```
!
interface vlanif100
 ip address 192.168.100.5/24
!
interface vlanif101
 ip address 10.1.1.5/24
!
router rip
 network 10.1.1.0/24
 network 192.168.100.0/24
 network vlanif100
 network vlanif101
!
ip route 10.2.2.0/24 192.168.100.1
ip route 192.168.200.0/24 192.168.100.1
!
ip forwarding
ipv6 forwarding
!
```
```
hostname CE1
!
interface vlanif100
 ip address 192.168.100.2/24
!
interface vlanif101
 ip address 10.1.1.1/24
!
router rip
 version 2
 network 192.168.100.0/24
 network 10.1.1.0/24
!
line vty
!
```

> CE 只跑 RIP：与 PE 之间交换本站点路由（192.168.100.0/24、10.1.1.0/24）。CE 上的 `show ip rip` 应当**只**看到 192.168.100.0/24 和 10.2.2.0/24，**不应**出现 192.168.12.0/24 / 192.168.23.0/24 / 192.168.200.0/24 —— 若出现说明 VPN 隔离被破坏。

#### CE2

```
!
interface vlanif200
 ip address 192.168.200.2/24
!
interface vlanif201
 ip address 10.2.2.2/24
!
router rip
 network 10.2.2.0/24
 network 192.168.200.0/24
 network vlanif200
 network vlanif201
!
ip route 10.1.1.0/24 192.168.200.3
ip route 192.168.100.0/24 192.168.200.3
!
ip forwarding
ipv6 forwarding
!
```
```
hostname CE2
!
interface vlanif200
 ip address 192.168.200.2/24
!
interface vlanif201
 ip address 10.2.2.1/24
!
router rip
 version 2
 network 192.168.200.0/24
 network 10.2.2.0/24
!
line vty
!
```

#### PC1 / PC2（普通主机，不跑路由协议）

| PC | IP | 掩码 | 默认网关 |
|---|---|---|---|
| PC1 | 10.1.1.10 | 255.255.255.0 | 10.1.1.1 |
| PC2 | 10.2.2.10 | 255.255.255.0 | 10.2.2.1 |

### 11.4 前置检查与生效验证（按顺序做，前一步不全绿不要往下走）

**⓪ VRF 绑定（必须是第一步 —— 缺陷 7 会让后面每一步都查不出结果）**

| 检查 | 命令 | 期望 |
|---|---|---|
| backend 类型 | `PE1# show vrf` | 第一行是否有 `netns-based vrfs`：有 = netns 后端，无 = Linux VRF/vrf-lite。决定 `interface IFNAME vrf VRF` 能否单独完成绑定 |
| CUST1 存在 | `PE1# show vrf` | `vrf CUST1 id <n> table <t> (configured)`，不是 `inactive` |
| **接口归属（分水岭）** | `PE1# show interface vrf CUST1` | vlanif100 必须出现在列表里。**空 → 后面全部免谈** |
| 直连路由 | `PE1# show ip route vrf CUST1` | 至少有 `C 192.168.100.0/24`、`L 192.168.100.1/32`。空 → 回缺陷 7 |
| 配置是否落下 | `PE1# show running-config` | **netns 后端**：应显示 `interface vlanif100 vrf CUST1`。**vrf-lite 后端：只显示 `interface vlanif100`、没有 vrf 限定词，这是正常的** —— 绑定来自内核（netlink），不是 CLI 配置；`lib/if.c:1386-1400` 的 `cli_show_interface()` 只在 `vrf_is_backend_netns()` 时才 `vty_out(" vrf %s")`，`:1420-1429` 的 `if_vty_config_start()` 同理。**别拿这一条判断归属**，改用上面的 `show interface vrf CUST1` |
| RIP 实例看到接口 | `PE1# show ip rip vrf CUST1 status` | `Interface` 表里出现 vlanif100，Send/Recv 均为 2 |

> PE2 侧同样做一遍（vlanif200 / 192.168.200.0/24）；CE1/CE2 不涉及 VRF，跳过本步。

**① Underlay（RIP + LDP）**

| 检查 | 命令 | 期望 |
|---|---|---|
| underlay 路由 | `PE1# show ip route` | 有 `R 2.2.2.2/32`、`R 3.3.3.3/32`、`R 192.168.23.0/24`（LDP transport 的前提） |
| LDP 会话 | `PE1# show mpls ldp neighbor` | 1 个邻居（2.2.2.2），Transport 为 lo，State = OPERATIONAL |
| LDP 绑定 | `PE1# show mpls ldp binding` | 有 2.2.2.2/32、3.3.3.3/32 的 label mapping |
| MPLS 入表 | `PE1# show mpls table` | 有 FEC 3.3.3.3/32 的 ILM，出标签非 0，下一跳 192.168.12.2 |

**② Overlay（VPNv4 会话）**

| 检查 | 命令 | 期望 |
|---|---|---|
| VPNv4 邻居 | `PE1# show bgp ipv4 vpn summary` | neighbor 3.3.3.3，State/PfxRcd 是**数字**（Established）且 ≥1 |
| 收发计数 | `PE1# show bgp ipv4 vpn neighbors 3.3.3.3` | 无 `EBGP outbound policy not properly setup` 告警 |

**③ VPN 路由交换（最关键，直接验证缺陷 1/2）**

| 检查 | 命令 | 期望 |
|---|---|---|
| CUST1 路由表 | `PE1# show ip route vrf CUST1` | 同时出现：`C 192.168.100.0/24`、`R 10.1.1.0/24`（来自 CE1）、**`B 10.2.2.0/24`（从 VPNv4 导入）** |
| VPNv4 全局表 | `PE1# show bgp ipv4 vpn` | 两条前缀：`RD 65001:100 / 10.1.1.0/24` 与 `RD 65002:100 / 10.2.2.0/24`，各带 MPLS Label |
| 导出侧 | `PE1# show bgp ipv4 vpn neighbors 3.3.3.3 advertised-routes` | ≥1 条（空 → 回去查缺陷 2） |
| VRF BGP 表 | `PE2# show bgp vrf CUST1 ipv4 unicast` | `10.2.2.0/24`（本地导出）+ `10.1.1.0/24`（对端导入） |

**④ 数据面**

| 检查 | 命令/动作 | 期望 |
|---|---|---|
| 端到端 | `PC1> ping 10.2.2.10 -n 100 -l 1400` | 100% 通（MTU 见第八节注意事项 2） |
| 路径 | `PC1> tracert -d 10.2.2.10` | PC1→CE1→PE1→P→PE2→CE2→PC2（PHP 时 P 可能不显示，属正常） |
| 标签栈 | P 的 vlanif20 出方向 tcpdump | **两层标签**：外层为 LDP 交换标签、内层为 BGP VPN 标签，内层 S=1 |
| 核心隔离 | `P# show mpls table` | **只有** 1.1.1.1/32、3.3.3.3/32 及互联段，**不应有 10.x / 192.168.100.x / 192.168.200.x** |
| 硬件落表 | dmesg / CTC-DPLANE 日志 | VPN 路由与 VPN 标签下发无 error |

### 11.5 RD / RT 语义速查（VPN 侧 BGP 配置块逐条解释）

**先建立这张图：`address-family ipv4 vpn`（也就是 `export vpn` / `import vpn` 背后那张表）不是本机的表，而是逻辑上所有 PE 共享的一张表。** PE1 从 CE1 学来的路由、PE2 从 CE2 学来的路由，全混在里面。一"混着放"，就立刻冒出两个必须解决的问题：

| 问题 | 由谁解决 |
|---|---|
| 两台 PE 上都可能出现 `10.1.1.0/24`（客户自己的地址规划，运营商管不着），在同一张表里怎么区分开？ | **RD**（Route Distinguisher） |
| 表里这么多前缀，PE2 怎么判断哪条属于"我这个客户"、哪条是隔壁客户的？ | **RT**（Route Target） |

**一句话：RD 管唯一性，RT 管归属。**

#### 11.5.1 逐条命令

| 命令 | 方向 | 作用 | 关键约束 / 代码依据 |
|---|---|---|---|
| `redistribute rip` | 本地 入 | 把 CUST1 zebra RIB 里的 RIP 路由灌进本 VRF 的 BGP 实例 —— 这是"接力"的第 ① 段 | 没有它，BGP 手里没货可导 |
| `label vpn export auto` | 本端 出 | 从 zebra 标签池**自动分配内层 MPLS 标签（VPN 标签）**。对端 PE 靠这个标签判断"收到带标签的包该送进哪个 VRF" | `bgp_vty.c:10482` `"[no] label vpn export <(0-1048575)\|auto>"`。它与 LDP 分配的**外层传输标签**是两层，正是 11.4 ④ 要抓的东西。缺它则 `show bgp ipv4 vpn` 里那条前缀不带 Label，数据面转发不了 |
| `rd vpn export 65002:100` | 本端 出 | 把本 VRF 要导出的前缀改写成 `RD:前缀`，即 `65002:100:10.2.2.0/24` | `bgp_vty.c:10345` `"[no] rd vpn export ASN:NN_OR_IP-ADDRESS:NN"`。**只在导出侧生效，不参与"收不收"的判断** |
| `route-target vpn export 65001:100` | 本端 出 | 给导出的路由挂上 BGP Extended Community `RT:65001:100`，这个标记跟着路由穿过 VPNv4 表飘到对端 | CLI help 原文（`bgp_vty.c:10890`）："For routes leaked from current address-family to vpn: **set**" → export = **打标** |
| `route-target vpn import 65001:100` | 本端 入 | **只有**携带 `RT:65001:100` 的路由，才允许从 VPNv4 表漏进本 VRF | CLI help 原文（`bgp_vty.c:10889`）："For routes leaked from vpn to current address-family: **match any**" → import = **匹配** |
| `export vpn` | 总开关（出） | 打通「本 VRF → VPNv4 表」 | 缺它 = 缺陷 1：本地路由永远出不了本机，rd / rt / label 全部白配 |
| `import vpn` | 总开关（入） | 打通「VPNv4 表 → 本 VRF」 | 缺它 = 缺陷 1：对端路由永远进不来 |

> `rt` 与 `route-target` 是**同义写法**，`import / export / both` 三个方向都支持：`bgp_vty.c:10884` `"[no] <rt\|route-target> vpn <import\|export\|both>$direction_str RTLIST..."`。`show running-config` 的导出侧回显可能写成 `rt vpn both 65001:100`（`:19177`），与手敲的 `route-target vpn import` + `route-target vpn export` **完全等价**，别被写法差异误导。
>
> `import|export vpn` 有 CLI 限制：**只能用在本 VRF 实例或 default 实例上**，且**只能在 `ipv4/ipv6 unicast` 下**，否则直接报 `%% import|export vpn valid only for bgp vrf or default instance`（`bgp_vty.c:11232-11242`）。这条限制本身就点破了这个配置块的定位 —— **它就是"VRF 与 VPNv4 之间的那座桥"**。

#### 11.5.2 为什么 PE1 / PE2 的 RD 不同，RT 却相同

| 命令 | 作用侧 | PE1 | PE2 | 两端关系 | 原因 |
|---|---|---|---|---|---|
| `rd vpn export` | 本端**出** | `65001:100` | `65002:100` | **必须不同** | 若两端都用 `65001:100`，而两个站点恰好都用了 `10.1.1.0/24`，两条导出路由会变成**字面完全相同的一条 NLRI**，BGP 把它们当成"到达同一目的地的主/备路径"，best-path 只留一条 —— 客户两个站点的网络被错误合并。RD 不同，它们才是两条互不相干的独立路由 |
| `route-target vpn export` | 本端**出** | `65001:100` | `65001:100` | **必须相同** | RT 就是"这个 VPN 的名字"，名字不同就不是同一个 VPN |
| `route-target vpn import` | 本端**入** | `65001:100` | `65001:100` | **必须相同**，且 = 对端 export | 这是一对锁和钥匙：PE2 出口盖 `65001:100` 的章，PE1 入口验 `65001:100` 的章 |
| `label vpn export auto` | 本端**出** | auto | auto | 各自独立，无需一致 | 标签是本地资源，两端各分各的 |
| `export vpn` / `import vpn` | 总开关 | 都要有 | 都要有 | 两端都要有 | 见 `bgp_mplsvpn.h:114` / `:203` |
| `redistribute rip` | 本地灌料 | 都要有 | 都要有 | 两端都要有 | 接力第 ① 段的原料 |

**数值上为什么这么巧：** `65001` / `65002` 就是两台 PE 各自的 AS 号（`rd vpn export` 支持 `ASN:NN` 与 `IP:NN` 两种格式，用 AS 号天然唯一、最省事）。RT 两边都写 `65001:100`，意思是"这两个 VRF 属于同一个 VPN 65001:100"。**RT 里出现 `65001` 不代表它是 PE1 专属** —— 它只是一个约定好的 VPN 编号，写成 `1:1`、`100:200` 效果完全一样，唯一的要求是**两端一致**。

> RD 第二段（`:100`）两端凑巧都写 100，纯粹为了配置好看，**没有任何约束**。真正有约束的只有两条：**① RD 在同一个 VPNv4 表里不能撞车；② RT 两端必须一致。**

#### 11.5.3 端到端走一遍（用 11.2 的真实地址）

```
【PE1 侧 · 出】
CE1 --RIP--> PE1 CUST1 RIB: 10.1.1.0/24
   --redistribute rip-->                 PE1 BGP VRF CUST1
   --export vpn
      + rd vpn export 65001:100
      + route-target vpn export 65001:100
      + label vpn export auto-->
   VPNv4 表:  [65001:100] 10.1.1.0/24   RT:65001:100   Label L1
                          └─ RD 保证唯一 ─┘ └─ RT 是通行证 ─┘

【PE2 侧 · 入】
   VPNv4: [65001:100] 10.1.1.0/24   RT:65001:100
      --import vpn + route-target vpn import 65001:100   ← 章对上了 ✓
      --> PE2 BGP VRF CUST1: 10.1.1.0/24
      --redistribute bgp metric 2--> PE2 CUST1 RIP: 10.1.1.0/24 metric 2
      --RIP--> CE2 ✓
```

反方向 PE2 → PE1 完全对称。

> **一句话记住：**
> **RD 是身份证号** —— 出门时贴在路由上，保证它在全球 VPNv4 表里不重号，**两端必须不同**。
> **RT 是群号** —— 出门时贴在路由上、进门时验，验过了才放进来，**两端必须相同**。
> **`export/import vpn` 是门禁的电源开关，rd / rt / label 只是门禁上的参数。**

### 11.6 通过判定

| 项 | 判定条件 |
|---|---|
| VPN 路由双向交换 | PE1 的 `show ip route vrf CUST1` 同时有 `R 10.1.1.0/24` 与 `B 10.2.2.0/24`，PE2 对称 |
| VPNv4 表 | 两端 `show bgp ipv4 vpn` 各 2 条前缀，RD = 65001:100 / 65002:100，RT = 65001:100，带 MPLS Label |
| 核心隔离 | P 上 `show mpls table` 只有 lo/32 与互联段，无任何客户网段 |
| 客户隔离 | CE1 `show ip rip` 只有 192.168.100.0/24 + 10.2.2.0/24（无核心网段） |
| 数据面 | PC1↔PC2 双向 ping 100% 通，tracert 完整，抓包两层标签 |
| 硬件 | dmesg / CTC-DPLANE 无 error，ILM（LDP）+ VPN 标签均落表 |

### 11.7 常见故障定位（对照 11.1 缺陷表）

| 现象 | 最可能的原因 |
|---|---|
| `show bgp ipv4 vpn` 全空 + 日志有 `EBGP outbound policy not properly setup` | 缺陷 2：漏 `no bgp ebgp-requires-policy` |
| `show bgp vrf CUST1 ipv4 unicast` 有路由，但 `show bgp ipv4 vpn` 没有 | 缺陷 1：漏 `export vpn` / `import vpn` |
| PE 的 CUST1 路由表有 `B` 条目，但本地 CE 学不到对端网段 | 缺陷 4：漏 `redistribute bgp metric 2` |
| `show ip route vrf CUST1` **完全为空**（连 `C` 都没有），`show ip rip vrf CUST1 status` 的 `Interface` 表为空、`Routing Information Sources` 为空 | 缺陷 7：接口没真正进 CUST1。先查 `show vrf` 第一行判断 backend，再查 `show interface vrf CUST1` 里有没有 vlanif100/200；对照 `show running-config` 看 `interface vlanif100 vrf CUST1` 是否真的落下去了 |
| `show ip route vrf CUST1` 有 `C` 但没有 `R` 条目 | 缺陷 3：漏 `router rip vrf CUST1` |
| PE 有 `R`/`B` 条目，但 PC ping 不通 | underlay 断：查 `show mpls ldp neighbor` 与 `show mpls table` 是否覆盖 **对端 PE 的 lo/32**（3.3.3.3/32），LDP 只跑互联口是否漏配 `interface vlanif10/20` |
| Ping 通但大包丢 | MTU：MPLS 压两层标签 = 8 字节，PC/vlanif MTU 需 ≥1508（建议统一 1526 或 ICMP 载荷 ≤1400） |

> **若 `show ip rip [vrf CUST1]` 一路全是空，先证明"报文到底有没有到"**：在 PE 上开 `debug rip events`，然后看日志。若出现下面两行，说明 CE 的更新**确实到了这台 PE**，只是被 `ripd/ripd.c:1866-1872` 的 `if (!ri->running && !rip_neighbor_lookup(rip, &from))` 挡住了 —— 也就是收到的接口在本 RIP 实例里没被 `network`/`neighbor` 启用：
>
> ```
> RECV packet from 192.168.200.2 port 520 on vlanif200 (VRF default)
> RIP is not enabled on interface vlanif200.
> ```
>
> 注意两行都是 `IS_RIP_DEBUG_EVENT` 级别，而 `debug rip events` 才打开它；`debug zebra *` 打开不了，别被"日志里什么也没有"误导。
>
> 极简证伪：临时在**默认** `router rip` 里加 `network 192.168.200.0/24`，`show ip rip` 应立刻出现 CE 的路由；确认后立即撤销。若连这样都收不到，问题在数据面（报文压根没到 PE），与 RIP 实例无关。

### 11.8 变体：PE-CE 改用 eBGP（把 CUST1 里的 RIP 换成 BGP）

**适用场景**：客户不想跑 RIP、要用 BGP 接入，或需要更细的策略控制（route-map / community / MED / 多宿主选路）。这条路线值得单独跑一遍的另一个理由 —— **它能证明"四段接力"这个拆法是干净的**：

> **改动只发生在第 ① 段和第 ④ 段，第 ② ③ 段（`export vpn` / `import vpn` / RD / RT / `label vpn export`）一个字符都不用动。** 若换掉 PE-CE 协议后第 ② ③ 段需要跟着改，说明理解还不到位。

#### 段位对照

| 接力段 | RIP 版（11.3） | eBGP 版 |
|---|---|---|
| ① PE-CE 本地 → BGP | `router rip vrf CUST1` + `redistribute rip` | VRF 实例里的 `neighbor 192.168.100.2 remote-as 65101` + `activate` |
| ② VRF BGP → VPNv4 | `export vpn` + `rd vpn export` + `route-target vpn export` + `label vpn export auto` | **完全不变** |
| ③ VPNv4 → VRF BGP | `import vpn` + `route-target vpn import` | **完全不变** |
| ④ BGP → PE-CE 本地 | `router rip vrf CUST1` + `redistribute bgp metric 2` | 无需任何 redistribute —— eBGP 邻居之间本来就互相通告 |

#### 配置改动

**CE1**（新增 `router bgp`，删掉整个 `router rip`）：

```
hostname CE1
!
interface vlanif100
 ip address 192.168.100.2/24
!
interface vlanif101
 ip address 10.1.1.1/24
!
! 原 router rip 整段删除
router bgp 65101
 bgp router-id 10.1.1.1
 no bgp ebgp-requires-policy
 neighbor 192.168.100.1 remote-as 65001
 !
 address-family ipv4 unicast
  neighbor 192.168.100.1 activate
  network 192.168.100.0/24
  network 10.1.1.0/24
 exit-address-family
!
line vty
!
```

**CE2** 对称：AS `65102`，`bgp router-id 10.2.2.1`，`neighbor 192.168.200.3 remote-as 65002`，`network 192.168.200.0/24` + `network 10.2.2.0/24`。

**PE1** 的 CUST1 配置块（把 `router rip vrf CUST1` 整段删掉，换成 VRF 实例下的 eBGP 邻居）：

```
! ========== ripd ==========
! router rip vrf CUST1 整段删除；underlay 的 router rip 保留不动
!
! ========== bgpd：VRF 实例（PE-CE 侧，eBGP）==========
router bgp 65001 vrf CUST1
 bgp router-id 1.1.1.1
 ! 必须单独配：ebgp-requires-policy 是 per-BGP-instance 的标志，
 ! 全局 router bgp 65001 下那条管不到这个 VRF 实例（见"坑 2"）
 no bgp ebgp-requires-policy
 neighbor 192.168.100.2 remote-as 65101
 !
 address-family ipv4 unicast
  neighbor 192.168.100.2 activate
  ! ↓↓↓ 以下 6 行与 RIP 版完全一致，一个字都不用改 ↓↓↓
  label vpn export auto
  rd vpn export 65001:100
  route-target vpn import 65001:100
  route-target vpn export 65001:100
  export vpn
  import vpn
 exit-address-family
!
```

**PE2** 对称：`router bgp 65002 vrf CUST1`，`bgp router-id 3.3.3.3`，`neighbor 192.168.200.2 remote-as 65102`，RD 仍是 `65002:100`，RT 仍是 `65001:100`。

**Underlay 三件套（`router rip` + `mpls ldp` + 全局 `router bgp 65001`）一律不动。**

#### 验证增补（在 11.4 ③ 的基础上加这 4 项）

| 检查 | 命令 | 期望 |
|---|---|---|
| PE-CE 邻居 | `PE1# show bgp vrf CUST1 ipv4 unicast summary` | neighbor 192.168.100.2，State/PfxRcd 是**数字**（Established）且 ≥1 |
| CE 侧邻居 | `CE1# show bgp ipv4 unicast summary` | neighbor 192.168.100.1，Established |
| PE 学到客户网段 | `PE1# show bgp vrf CUST1 ipv4 unicast` | `10.1.1.0/24`（本地 eBGP 学来）+ `10.2.2.0/24`（从 VPNv4 导入） |
| CE 学到对端网段 | `CE1# show bgp ipv4 unicast` | `192.168.100.0/24` + `10.2.2.0/24` |

#### 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| PE-CE 邻居卡在 `Idle` / `Active`，`show bgp vrf CUST1 ipv4 unicast summary` 干脆没输出 | VRF 实例建不起来 —— 与缺陷 7 同源：接口没真正进 CUST1，`neighbor 192.168.100.2` 的直连路由不在 CUST1 的 RIB 里，BGP 的 TCP 建不起来 | 先回 **11.4 ⓪** 把 VRF 绑定做对 |
| 邻居 `Established` 但 `PfxRcd` 一直是 0，日志刷 `EBGP outbound policy not properly setup` | **坑 1**：漏了 VRF 实例下自己的 `no bgp ebgp-requires-policy`（详见坑 2） | 在 `router bgp <AS> vrf CUST1` 下**再配一遍** |
| CE 侧 `show bgp ipv4 unicast` 空 | CE 忘了 `no bgp ebgp-requires-policy`，或 `network` 指的前缀不在 CE 自己的 RIB 里 | CE 是普通 BGP 实例，同样受 RFC 8212 约束 |
| `show bgp vrf CUST1 ipv4 unicast` 有路由，但 `show bgp ipv4 vpn` 没有 | **不是** PE-CE 侧的问题，是第 ② 段 | 回缺陷 1：查 `export vpn` / `rd vpn export`（`bgp_mplsvpn.h:114` 要求 `rtlist` 与 RD 都非空） |

> **坑 2（最容易踩）：`no bgp ebgp-requires-policy` 要在两个地方各配一遍 —— 全局实例 + VRF 实例。**
> 它是 **per-BGP-instance** 的标志，不是全局状态。`bgp_vty.c:98` 的 `FRR_CFG_DEFAULT_BOOL(BGP_EBGP_REQUIRES_POLICY, ...{ .val_bool = true })` 让 FRR 10.6.1 默认开启 RFC 8212，而 `bgp_vty.c:678-679` 在 `bgp_get_vty()` 里**对每一个新建实例**都按这个默认值 `SET_FLAG((*bgp)->flags, BGP_FLAG_EBGP_REQUIRES_POLICY)`。所以在 `router bgp 65001` 下配过，**不等于** `router bgp 65001 vrf CUST1` 下也配了。这正是 11.1 缺陷 2 的同一个坑，只是位置从 PE1↔PE2 挪到了 PE1↔CE1。

> **不需要 `next-hop-self`。** PE-CE 是**直连** eBGP，从 CE 学来的路由下一跳就是 CE 的接口地址，直接可用。只有 PE 与 CE 之间跨了多跳设备（配了 `ebgp-multihop`）时才需要。这与 PE1↔PE2 之间那条走 loopback 的 VPNv4 eBGP 不同 —— 后者必须 `update-source lo` + `ebgp-multihop 2`。

> **不想跑任何动态协议？** 也可以纯静态：PE1 的 CUST1 里配 `ip route 10.1.1.0/24 192.168.100.2 vrf CUST1`（`staticd/static_vty.c:518` 起，`vrf NAME` 是可选 token，也支持 `table N`），VRF 实例里把 `redistribute rip` 换成 `redistribute static`，CE 侧写默认路由。第 ② ③ 段依然不动。注意 **staticd 必须运行**，否则路由进不了 zebra 的 CUST1 RIB。


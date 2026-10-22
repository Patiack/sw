# FRR bgpd 深入剖析 · 卷5 —— 路由策略、策略路由与流规范

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 本卷覆盖文件 **14** 个、共 **21794** 行（工作树行数，含中文注释）。
>
> 写作目的：
> 1. **route-map 与通用引擎的关系**：bgpd 的 route-map 实现与 `lib/routemap.c` 的通用引擎是什么关系？
>    bgpd 提供哪些「匹配 / 设置动作」回调？它们的注册表在哪里？各 `match_*` / `set_*` 回调的定义行在哪？
> 2. **北向接口的并存**：`bgp_routemap_nb.c` / `bgp_routemap_nb_config.c` 与老的 VTY 命令怎么并存？
>    两者是不是同一份配置的两条路径？交叉引用点在哪？
> 3. **策略路由（PBR）**：`bgp_pbr.c`（约 3300 行）做了什么？它与 flowspec 是什么关系？
> 4. **流规范（flowspec，RFC 8955/8956）**：NLRI 的二进制编码（按 component type 逐项）在
>    `bgp_flowspec.c` + `bgp_flowspec_util.c` 里怎么做？`bgp_flowspec_private.h` 定义了什么？
>    解析出的 flowspec 规则最终怎么变成 PBR 下发给 zebra？
> 5. **展示层**：`bgp_flowspec_vty.c` 的 CLI 与展示层职责。
> 6. **BFD**：BGP 如何用 BFD 加速邻居故障检测？怎么与 `bfdd` 进程交互（zclient 的
>    `ZEBRA_BFD_DEST_*` 消息）？哪些条件会触发 BFD 会话建立 / 拆除？
>
> 本卷是 `FRR_BGPD_STUDY.md`（总览篇）之下的分册。总览篇给出 bgpd 的整体架构、三线程模型、
> 核心数据结构与 131 文件全景地图；本卷只深入「路由策略、PBR 与流规范」这一层，其余层次请在
> 对应分册中查阅（如 `bgp_route.c` 的选路、`bgp_attr.c` 的属性解析属于别的卷）。本文不修改任何源码。

---

## 本卷文件覆盖清单（自检用）

| # | 文件 | 行数 | 一句话职责 | 本卷小节 |
|---|------|------|-----------|---------|
| 1 | `bgp_routemap.c` | 9682 | BGP 专属 route-map match/set 回调实现 + YANG 命令（124 条）+ 注册总表 | §1 §2 |
| 2 | `bgp_routemap_nb.c` | 564 | NB(YANG) 节点 → 回调映射表 `frr_bgp_route_map_info` | §2 |
| 3 | `bgp_routemap_nb.h` | 276 | 上述回调的原型声明（134 个函数原型） | §2 |
| 4 | `bgp_routemap_nb_config.c` | 4154 | route-map 的 NB 回调实现（134 个回调） | §2 |
| 5 | `bgp_pbr.c` | 3331 | flowspec / ip rule → zebra PBR 对象的翻译、下发、回收 | §3 §5 |
| 6 | `bgp_pbr.h` | 504 | PBR 全部内部数据结构与对外原型 | §3 |
| 7 | `bgp_flowspec.c` | 256 | flowspec NLRI 接收入口 + 逐分量格式校验 | §4 |
| 8 | `bgp_flowspec.h` | 121 | flowspec 解析/展示对外接口 + 显示模式常量 | §4 §6 |
| 9 | `bgp_flowspec_private.h` | 69 | 分量类型号字典 + 内部限值 + traffic-action 位 | §4 |
| 10 | `bgp_flowspec_util.c` | 868 | 分量通用解码器（op/prefix/bitmask）+ 转内部 PBR 语义 | §4 |
| 11 | `bgp_flowspec_util.h` | 121 | 上述解码器的接口 + 解码模式枚举 | §4 |
| 12 | `bgp_flowspec_vty.c` | 744 | flowspec 的 show / JSON / debug / local-install 展示层 | §6 |
| 13 | `bgp_bfd.c` | 982 | BGP 与 BFD 联动：会话建立/更新/拆除 + 状态回调 + 命令 | §7 |
| 14 | `bgp_bfd.h` | 122 | 上述接口 + `PEER_IS_MULTIHOP` 宏 | §7 |

> **覆盖率自检**：上表共 14 行，与本卷分配清单逐一对齐，一个不漏、一个不多。
> 各文件行数用 `wc -l` 实测（工作树行数，含中文注释）。

---

## §0 本卷总览

### 0.1 先给结论

> **这 14 个文件其实是「三条互不相干的策略管线」被同一个卷号捆在一起**：
> ① route-map（**路由**策略，作用于路径属性，纯控制面）；
> ② PBR/ipset/ip rule（**策略路由**，作用于数据面，通过 zebra 下发内核）；
> ③ BFD（**快速故障检测**，通过 bfdd 加速邻居收敛）。
> flowspec 是把 ①②串起来的那根线：flowspec 的 NLRI 是**匹配条件**、它的 ecommunity 是**动作**，
> 一起被翻译成 PBR，再经 zebra 变成 ipset + iptables + ip rule。

把这句判断拆开，是本卷最需要建立的认知：

| 判断 | 证据 | 展开 |
|---|---|---|
| **① route-map 的「引擎」不在 bgpd** | `struct route_map_rule_cmd` 定义在 [routemap.h:186](lib/routemap.h#L186)；条目/序号/permit-deny/求值循环在 `lib/routemap.c` | bgpd 只向引擎**注册插件**：41 个 match 类别 + 38 个 set 类别（[bgp_routemap.c:9448](bgpd/bgp_routemap.c#L9448) 起）。详见 §1 |
| **② route-map 的 CLI 在 10.6.1 已**全部**北向化** | `bgp_routemap.c` 里 **124 条命令全是 `DEFUN_YANG` / `DEFPY_YANG`**（实测 `grep -oE '^DEF[A-Z_]*'` 只有这两类） | 命令体统一是 `nb_cli_enqueue_change()` + `nb_cli_apply_changes()`（[bgp_routemap.c:6060](bgpd/bgp_routemap.c#L6060)），真正落地在 `bgp_routemap_nb_config.c` 的回调里。所以「老 VTY vs 新 NB」在本卷**不是两条并存路径，而是一条**：CLI 只是 NB 的前端。详见 §2 |
| **③ PBR 与 flowspec：前者是后者的「执行侧」** | `bgp_pbr_update_entry()`（[bgp_pbr.c:3260](bgpd/bgp_pbr.c#L3260)）由路由变化驱动，flowspec 表变化时被 `bgp_zebra.c` 调起 | flowspec 的 NLRI 解析成 `bgp_pbr_entry_main`，再展开成 `bgp_pbr_match`(ipset)、`bgp_pbr_rule`(ip rule)、`bgp_pbr_action`(动作)。详见 §3 §5 |
| **④ flowspec NLRI = 「TLV 式匹配分量」串**，不是前缀 | 分量类型号表在 [bgp_flowspec_private.h:55-67](bgpd/bgp_flowspec_private.h#L55)；三种解码器见 [bgp_flowspec_util.c:228](bgpd/bgp_flowspec_util.c#L228)、[331](bgpd/bgp_flowspec_util.c#L331)、[481](bgpd/bgp_flowspec_util.c#L481) | 每个分量 = 1 字节类型 + 该类型编码。分量之间是与(AND)。详见 §4 |
| **⑤ BGP 不直接跟 bfdd 说话** | `bgp_bfd_init` 调 `bfd_protocol_integration_init(bgp_zclient, tm)`（[lib/bfd.c:1293](lib/bfd.c#L1293)） | 走 `bgp_bfd.c → libbfd(bfd_sess_*) → zclient → zebra → bfdd`；消息是 `ZEBRA_BFD_DEST_REGISTER/UPDATE/DEREGISTER`。详见 §7 |

### 0.2 文件职责分层表（按「引擎 / 插件 / 适配 / 展示」四层）

| 文件 | 层 | 引擎侧依赖 | 一句话 |
|---|---|---|---|
| [bgp_routemap.c](bgpd/bgp_routemap.c) | **插件** | `lib/routemap.c`（引擎）、`lib/routemap_cli.c`（条目命令） | 41 match + 38 set 插件 + 124 条 YANG 命令 |
| [bgp_routemap_nb.c](bgpd/bgp_routemap_nb.c) | **适配** | `lib/northbound.h` | xpath → 回调映射表（唯一实质内容） |
| [bgp_routemap_nb.h](bgpd/bgp_routemap_nb.h) | 适配 | — | 134 个 NB 回调原型 |
| [bgp_routemap_nb_config.c](bgpd/bgp_routemap_nb_config.c) | **适配** | `lib/routemap.c` 的 `route_map_add_match/set` | 把 YANG 叶子翻译成引擎规则 |
| [bgp_pbr.c](bgpd/bgp_pbr.c) | **执行** | `lib/pbr.h`、`bgp_zebra.c` | flowspec→PBR 翻译、去重、下发、回收 |
| [bgp_pbr.h](bgpd/bgp_pbr.h) | 执行 | — | PBR 内部数据结构 |
| [bgp_flowspec.c](bgpd/bgp_flowspec.c) | **解析入口** | `bgp_route.c` 的 NLRI 分发 | 收包 + 逐分量校验 + 交给 `bgp_update` |
| [bgp_flowspec_util.c](bgpd/bgp_flowspec_util.c) | **解析核心** | `bgp_pbr.h` | 分量解码 + 转内部语义 |
| [bgp_flowspec_util.h](bgpd/bgp_flowspec_util.h) | 解析核心 | `zclient.h` | 解码器接口 + 模式枚举 |
| [bgp_flowspec_private.h](bgpd/bgp_flowspec_private.h) | 解析核心 | — | 类型号字典 |
| [bgp_flowspec.h](bgpd/bgp_flowspec.h) | 解析核心 | — | 对外接口 + 显示模式 |
| [bgp_flowspec_vty.c](bgpd/bgp_flowspec_vty.c) | **展示/配置** | `bgp_pbr.h` | show / JSON / debug / local-install |
| [bgp_bfd.c](bgpd/bgp_bfd.c) | **集成** | `lib/bfd.c`、`bgp_fsm.c` | BGP↔BFD 会话生命周期 |
| [bgp_bfd.h](bgpd/bgp_bfd.h) | 集成 | — | 接口 + 单跳/多跳判据 |

### 0.3 全景图（本卷内部的数据流与调用流）

```
                         ┌────────────────────────── 控制面：route-map ──────────────────────────┐
  用户 CLI                                                        lib/routemap.c（引擎／无 BGP 知识）
  ┌──────────────┐  nb_cli_enqueue_change    ┌────────────────┐   route_map_add_match/set
  │ bgp_routemap │ ───────────────────────▶  │ northbound 框架 │ ─────────────────────────┐
  │  .c  (124 条  │  nb_cli_apply_changes     │ bgp_routemap_nb │                          ▼
  │  DEFUN_YANG) │                           │  _config.c(134) │                 struct route_map_rule_cmd
  └──────┬───────┘                           └────────────────┘                 （bgp_routemap.c 注册 79 个）
         │ 注册插件                                                                    ▲
         └── route_map_install_match/set ───────────────────────────────────────────────┘
                                                                                 求值：route_map_apply()
                                                                                 → bpi->attr 被改写 → 选路/下发

                         ┌─────────────────────── 数据面：flowspec → PBR ───────────────────────┐
  对端 MP_REACH/UNREACH
  ┌───────────────────┐  bgp_nlri_parse_   ┌────────────────────┐  match_rules_fill
  │ bgp_flowspec.c    │  flowspec          │ bgp_flowspec_util  │ ─────────────┐
  │ bgp_fs_nlri_      │ ─────────────────▶ │  .c（op/prefix/    │              ▼
  │  validate(逐分量) │                    │   bitmask 三类解码）│   struct bgp_pbr_entry_main
  └───────────────────┘                    └────────────────────┘   （bgp_pbr.h:186，match+actions）
         │ 组装 AF_FLOWSPEC 前缀                                                    │
         └── bgp_update/bgp_withdraw ─▶ RIB(SAFI_FLOWSPEC)                          │ bgp_pbr_build_and_validate_entry
                                                                                    ▼  (bgp_pbr.c:911)
  路由变化 ── bgp_zebra_announce_actual/withdraw_actual（bgp_zebra.c:1928/2135）─▶ bgp_pbr_update_entry
                                                                                    │  (bgp_pbr.c:3260)
                                                                    bgp_pbr_handle_entry（bgp_pbr.c:2998）
                                                                    ├─ ACTION_REDIRECT / REDIRECT_IP → nexthop
                                                                    ├─ ACTION_TRAFFICRATE rate==0 → BLACKHOLE
                                                                    └─ OR 维度展开（recursive, bgp_pbr.c:2906）
                                                                                    ▼
                                                          bgp_pbr_policyroute_add_to_zebra_unit（:2580）
                                                          ├─ bgp_pbr_action（共享动作，可 refcnt）
                                                          ├─ bgp_pbr_match / _entry（ipset + 条目）
                                                          └─ bgp_pbr_rule（ip rule）
                                                                                    ▼
                                                          bgp_zebra.c: bgp_send_pbr_ipset_match(:4924)
                                                                       bgp_send_pbr_ipset_entry_match(:4953)
                                                                       bgp_send_pbr_iptable(:5027)
                                                                       bgp_send_pbr_rule_action(:4887)
                                                                                    ▼ zclient → zebra → 内核

                         ┌────────────────────────── 快速故障检测：BFD ──────────────────────────┐
  bgp_fsm.c 状态机 / bgpd.c 配置 / NHT ──▶ bgp_peer_config_apply（bgp_bfd.c:165）
                                            └─ libbfd bfd_sess_*（lib/bfd.c）
                                                  └─ _bfd_sess_send（lib/bfd.c:628）
                                                       └─ ZEBRA_BFD_DEST_REGISTER/UPDATE/DEREGISTER
                                                            └─ zclient(bgp_zclient) → zebra → bfdd
  bfdd ─▶ zebra ─▶ zclient 回调 ─▶ bfd_session_status_update（bgp_bfd.c:100）─▶ 重置/重建 BGP 会话
```

### 0.4 与相邻卷的边界

| 想问的问题 | 去本卷的哪节 | 或去别的卷 |
|---|---|---|
| route-map 的 permit/deny 求值顺序、`on-match goto` 语义 | 只在此点到引擎位置 §1.1 | **lib 公共库卷**（`lib/routemap.c`） |
| `bgp_update` / `bgp_withdraw` / RIB 选路 / NLRI 分发 | 只作交叉引用 | **bgp_route 卷**（`bgp_route.c` 的 NLRI dispatch 在 [bgp_route.c:12028](bgpd/bgp_route.c#L12028)） |
| flowspec 的 ecommunity（traffic-rate / redirect / mark）编解码 | §3.4、§5.3 引用其类型名 | **属性/ecommunity 卷**（`bgp_ecommunity.c` / `bgp_attr.c`） |
| `bgp_zebra_announce_actual` 的完整下发逻辑、zapi_route 编码 | §5.3 只看 flowspec 分流点 | **zebra 接口卷**（`bgp_zebra.c`） |
| `struct peer` / `bfd_session_config` 的完整定义 | §7 只引用相关成员 | **核心数据结构卷**（`bgpd.h` / `bgp_fsm.c`） |

---

## §1 route-map：bgpd 只提供「插件」，引擎在 lib

### 1.1 判断先行：这是插件-宿主关系

route-map 是 FRR 的**通用路由策略引擎**。它自己完全不懂 BGP 属性，只懂「一个条目 = permit/deny + 一串 match 子句 + 一串 set 子句，逐条求值」。BGP、OSPF、zebra 等每个协议各自向它**注册自己认识的动作**。证据是 `struct route_map_rule_cmd`：

```c
/* lib/routemap.h:186 */
struct route_map_rule_cmd {
	const char *str;                    /* 规则名，如 "ip address"、"metric" */
	enum route_map_cmd_result_t (*func_apply)(void *rule, const struct prefix *, void *object);
	void *(*func_compile)(const char *);   /* 字符串参数 → 不透明规则对象 */
	void (*func_free)(void *);
	void *(*func_get_rmap_rule_key)(void *val);
};
```

- 三个关键字段就是「样板三件套」：`func_compile`（编译参数）、`func_free`（释放）、`func_apply`（求值）。
- `func_apply` 的返回值是 [routemap.h:120](lib/routemap.h#L120) 的 `enum route_map_cmd_result_t`：match 规则返回 `RMAP_MATCH` / `RMAP_NOMATCH` / `RMAP_NOOP`，set 规则返回 `RMAP_OKAY` / `RMAP_ERROR`。
  - **`RMAP_NOOP` 是坑**：它表示「本规则对当前这条路由不适用」，既不是命中也不是未命中，读求值逻辑时不能当成真。注释见 [routemap.h:105-108](lib/routemap.h#L105)。
- `func_apply` 的 `object` 在常规路由场景下就是 `struct bgp_path_info *`（正在被策略评估的那条 BGP 路径），`func_compile` 的返回值是「规则对象」。

引擎侧 `route_map_install_match` / `route_map_install_set` 实际是宏（[routemap.h:666](lib/routemap.h#L666)、[677](lib/routemap.h#L677)），包一层 `struct route_map_rule_cmd_proxy`（[routemap.h:651](lib/routemap.h#L651)）后调 `_route_map_install_match()`（[routemap.h:686](lib/routemap.h#L686)）把插件加进引擎的全局匹配命令哈希表。之所以套一层 proxy：注释说 `route_map_rule_cmd` 保持 `const`（[routemap.h:641](lib/routemap.h#L641)），避免有可写副本。

一个「插件三件套」的最小样例（match peer）：

| 组成 | 定义行 | 说明 |
|---|---|---|
| `route_match_peer_compile` | [bgp_routemap.c:447](bgpd/bgp_routemap.c#L447) | 把 "local" 特判成 "0.0.0.0" 后 `str2sockunion`；失败则当接口/组名存进 `interface` |
| `route_match_peer_free` | [bgp_routemap.c:467](bgpd/bgp_routemap.c#L467) | 释放 `interface` 与结构体 |
| `route_match_peer`（apply） | [bgp_routemap.c:441](bgpd/bgp_routemap.c#L441) 上方注释；函数体在 479 之前 | 比较 `bpi->peer` 的地址或名字 |
| `route_match_peer_cmd` | [bgp_routemap.c:479](bgpd/bgp_routemap.c#L479) | 把规则名 `"peer"` 与上述三回调绑定 |

`route_match_src_peer_cmd`（[bgp_routemap.c:546](bgpd/bgp_routemap.c#L546)）是「复用 compile/free，仅换 apply」的典型：`src-peer` 与 `peer` 参数格式完全相同，只是 apply 比较的是 `bpi->from`（来源对端）而非 `bpi->peer`。

### 1.2 注册表在哪里：`bgp_route_map_init()`

**所有 BGP 插件都在一个函数里注册**：[bgp_route_map_init:9386](bgpd/bgp_routemap.c#L9386)。它做四件事：

1. `route_map_init()`（[9388](bgpd/bgp_routemap.c#L9388)）：初始化引擎。
2. 注册三个生命周期钩子（[9390-9392](bgpd/bgp_routemap.c#L9390)）：`bgp_route_map_add`、`bgp_route_map_delete`、`bgp_route_map_event` —— 当 route-map 被增删改时，引擎回调它们去重算路由。
3. 注册通用 match/set 钩子（[9394-9446](bgpd/bgp_routemap.c#L9394)）：把引擎内置的通用参数（interface、ip address、prefix-list、next-hop 类型、weight、metric、tag 等）绑定到 bgpd 的 `generic_match_add` / `generic_set_add`（定义在 [lib/routemap.c:686](lib/routemap.c#L686) / [775](lib/routemap.c#L775)）。
4. `route_map_install_match` / `route_map_install_set` 逐个装插件：41 处 match（[9448](bgpd/bgp_routemap.c#L9448)～[9653](bgpd/bgp_routemap.c#L9653)）与 38 处 set（[9483](bgpd/bgp_routemap.c#L9483)～[9651](bgpd/bgp_routemap.c#L9651)）**交错排列**（不是先 match 后 set）；最后 `install_element(RMAP_NODE, ...)` 把命令挂到 route-map 节点。

`bgp_route_map_init` 的调用点：`bgpd.c:10593`（在 `bgp_route_init` 之后、`bgp_flowspec_vty_init` 之前）。
终止入口 `bgp_route_map_terminate`（[bgp_routemap.c:9678](bgpd/bgp_routemap.c#L9678)）只调 `route_map_finish()`。

### 1.3 match / set 插件清单（含定义行）

**共 79 个 `struct route_map_rule_cmd` 定义**（实测 `grep -c 'struct route_map_rule_cmd' = 79`），其中 match 安装 41 处、set 安装 38 处（`grep -c route_map_install_match/set`）。

**match 类插件（规则名 → `route_map_rule_cmd` 定义行）**：

| 规则名 | cmd 定义 | 规则名 | cmd 定义 |
|---|---|---|---|
| `peer` | [479](bgpd/bgp_routemap.c#L479) | `lcommunity` | [2373](bgpd/bgp_routemap.c#L2373) |
| `src-peer` | [546](bgpd/bgp_routemap.c#L546) | `extcommunity` | [2460](bgpd/bgp_routemap.c#L2460) |
| `script` | [680](bgpd/bgp_routemap.c#L680) | `origin` | [2518](bgpd/bgp_routemap.c#L2518) |
| `ip address` | [749](bgpd/bgp_routemap.c#L749) | `probability` | [2583](bgpd/bgp_routemap.c#L2583) |
| `ip next-hop` | [813](bgpd/bgp_routemap.c#L813) | `interface` | [2634](bgpd/bgp_routemap.c#L2634) |
| `ip route-source` | [882](bgpd/bgp_routemap.c#L882) | `tag` | [2666](bgpd/bgp_routemap.c#L2666) |
| `ip address prefix-list` | [1006](bgpd/bgp_routemap.c#L1006) | `ipv6 address` | [4679](bgpd/bgp_routemap.c#L4679) |
| `ip next-hop prefix-list` | [1065](bgpd/bgp_routemap.c#L1065) | `ipv6 next-hop` | [4742](bgpd/bgp_routemap.c#L4742) |
| `ipv6 next-hop prefix-list` | [1130](bgpd/bgp_routemap.c#L1130) | `ipv6 next-hop address` | [4799](bgpd/bgp_routemap.c#L4799) |
| `ip next-hop type` | [1181](bgpd/bgp_routemap.c#L1181) | `ipv4 next-hop` | [4853](bgpd/bgp_routemap.c#L4853) |
| `source-protocol` | [1230](bgpd/bgp_routemap.c#L1230) | `ipv6 address prefix-list` | [4885](bgpd/bgp_routemap.c#L4885) |
| `ip route-source prefix-list` | [1295](bgpd/bgp_routemap.c#L1295) | `ipv6 next-hop type` | [4944](bgpd/bgp_routemap.c#L4944) |
| `evpn default-route` | [1323](bgpd/bgp_routemap.c#L1323) | `rpki` extcommunity | [5497](bgpd/bgp_routemap.c#L5497) |
| `mac address` | [1388](bgpd/bgp_routemap.c#L1388) | `vpn dataplane` | [5567](bgpd/bgp_routemap.c#L5567) |
| `evpn vni` | [1480](bgpd/bgp_routemap.c#L1480) | `vrf source-vrf` | [1882](bgpd/bgp_routemap.c#L1882) |
| `evpn route-type` | [1541](bgpd/bgp_routemap.c#L1541) | `alias` | [1954](bgpd/bgp_routemap.c#L1954) |
| `evpn route-distinguisher` | [1608](bgpd/bgp_routemap.c#L1608) | `local-preference`(match) | [2012](bgpd/bgp_routemap.c#L2012) |
| `community limit` | [1673](bgpd/bgp_routemap.c#L1673) | `metric`(match) | [2041](bgpd/bgp_routemap.c#L2041) |
| `extcommunity limit` | [1741](bgpd/bgp_routemap.c#L1741) | `as-path` | [2091](bgpd/bgp_routemap.c#L2091) |
| `as-path count` | [2151](bgpd/bgp_routemap.c#L2151) | `community` | [2282](bgpd/bgp_routemap.c#L2282) |

**set 类插件（规则名 → cmd 定义行）**：

| 规则名 | cmd 定义 | 规则名 | cmd 定义 |
|---|---|---|---|
| `evpn gateway-ip ipv4/ipv6` | [1819](bgpd/bgp_routemap.c#L1819) / [1826](bgpd/bgp_routemap.c#L1826) | `community`(set) | [3548](bgpd/bgp_routemap.c#L3548) |
| `sr-te color` | [2711](bgpd/bgp_routemap.c#L2711) | `large-community` | [3672](bgpd/bgp_routemap.c#L3672) |
| `ip next-hop`(set) | [2838](bgpd/bgp_routemap.c#L2838) | `large-community delete` | [3770](bgpd/bgp_routemap.c#L3770) |
| `l3vpn next-hop encapsulation` | [2908](bgpd/bgp_routemap.c#L2908) | `community delete` | [3865](bgpd/bgp_routemap.c#L3865) |
| `local-preference`(set) | [2948](bgpd/bgp_routemap.c#L2948) | `extcommunity delete` | [3949](bgpd/bgp_routemap.c#L3949) |
| `weight` | [2982](bgpd/bgp_routemap.c#L2982) | `extcommunity none` | [4069](bgpd/bgp_routemap.c#L4069) |
| `distance` | [3009](bgpd/bgp_routemap.c#L3009) | `extcommunity rt` | [4078](bgpd/bgp_routemap.c#L4078) |
| `metric`(set) | [3046](bgpd/bgp_routemap.c#L3046) | `extcommunity soo` | [4108](bgpd/bgp_routemap.c#L4108) |
| `table` | [3080](bgpd/bgp_routemap.c#L3080) | `extcommunity nt` | [4133](bgpd/bgp_routemap.c#L4133) |
| `as-path prepend` | [3155](bgpd/bgp_routemap.c#L3155) | `extcommunity color` | [4338](bgpd/bgp_routemap.c#L4338) |
| `as-path exclude` | [3270](bgpd/bgp_routemap.c#L3270) | `extcommunity lb` | [4354](bgpd/bgp_routemap.c#L4354) |
| `as-path replace` | [3418](bgpd/bgp_routemap.c#L3418) | `origin`(set) | [4409](bgpd/bgp_routemap.c#L4409) |
| `atomic-aggregate` | [4451](bgpd/bgp_routemap.c#L4451) | `aigp-metric` | [4495](bgpd/bgp_routemap.c#L4495) |
| `aggregator as` | [4560](bgpd/bgp_routemap.c#L4560) | `tag`(set) | [4587](bgpd/bgp_routemap.c#L4587) |
| `label-index` | [4620](bgpd/bgp_routemap.c#L4620) | `ipv6 next-hop global` | [5012](bgpd/bgp_routemap.c#L5012) |
| `ipv6 next-hop prefer-global` | [5071](bgpd/bgp_routemap.c#L5071) | `ipv6 next-hop local` | [5164](bgpd/bgp_routemap.c#L5164) |
| `ipv6 next-hop peer-address` | [5252](bgpd/bgp_routemap.c#L5252) | `vpnv4/vpnv6 nexthop` | [5354](bgpd/bgp_routemap.c#L5354) / [5363](bgpd/bgp_routemap.c#L5363) |
| `originator-id` | [5420](bgpd/bgp_routemap.c#L5420) | | |

> **读法**：想找「`set metric` 到底怎么改属性」，直接去 [bgp_routemap.c:3046](bgpd/bgp_routemap.c#L3046) 的 `route_set_metric_cmd` 看它绑定的 apply 函数；想找「`match community` 怎么比较」，去 [bgp_routemap.c:2282](bgpd/bgp_routemap.c#L2282)。这套「一个规则名对应一个 cmd 结构、三个回调」的规律，79 个插件无一例外。

### 1.4 求值函数的统一签名

match 类 apply 的签名统一是：

```c
static enum route_map_cmd_result_t route_match_xxx(void *rule,
                                                   const struct prefix *prefix,
                                                   void *object);
```

`object` 通常是 `struct bgp_path_info *`（见 [bgp_routemap.c:491](bgpd/bgp_routemap.c#L491) 的 `route_match_src_peer`，`bpi = object`）。`rule` 是 `func_compile` 产出的弱类型指针，apply 里强转回来（如 [bgp_routemap.c:501](bgpd/bgp_routemap.c#L501) 转成 `struct bgp_match_peer_compiled *`）。

关于依赖通知：被引用的对象（prefix-list、community-list、as-path-list…）变化时，引擎通过 `route_map_notify_dependencies()` 让依赖它的 route-map 重算。bgpd 侧的接收入口是三个生命周期钩子 `bgp_route_map_add`（[5996](bgpd/bgp_routemap.c#L5996)）、`bgp_route_map_delete`（[6006](bgpd/bgp_routemap.c#L6006)）、`bgp_route_map_event`（[6037](bgpd/bgp_routemap.c#L6037)），它们最终调 `bgp_route_map_mark_update`（[5962](bgpd/bgp_routemap.c#L5962)）安排一次 BGP 路由重算。

### 1.5 通用钩子：bgpd 借引擎内置的同名规则

除了上面 79 个「独立插件」，还有一类**半通用**规则：规则名字与引擎内置的通用参数**同名**，但 bgpd 通过「钩子」把「新增 / 删除该规则」的动作**重定向**到自己的实现。这段代码就在 `bgp_route_map_init` 里紧接 `route_map_init()` 之后（[bgp_routemap.c:9394-9446](bgpd/bgp_routemap.c#L9394)）。它不是注册 `route_map_rule_cmd`，而是注册 `route_map_*_hook(callback)`——钩子是「当 CLI/NB 要增删这条规则时调哪个函数」。

| 钩子族（规则名） | 行号 | 重定向到 |
|---|---|---|
| `interface` | [9394](bgpd/bgp_routemap.c#L9394) | `generic_match_add` / `generic_match_delete` |
| `ip address` | [9397](bgpd/bgp_routemap.c#L9397) | 同上 |
| `ip address prefix-list` | [9400](bgpd/bgp_routemap.c#L9400) | 同上 |
| `ip next-hop` | [9403](bgpd/bgp_routemap.c#L9403) | 同上 |
| `ipv6 next-hop` | [9406](bgpd/bgp_routemap.c#L9406) | 同上 |
| `ip next-hop prefix-list` | [9409](bgpd/bgp_routemap.c#L9409) | 同上 |
| `ip next-hop type` | [9412](bgpd/bgp_routemap.c#L9412) | 同上 |
| `ipv6 address` | [9415](bgpd/bgp_routemap.c#L9415) | 同上 |
| `ipv6 address prefix-list` | [9418](bgpd/bgp_routemap.c#L9418) | 同上 |
| `ipv6 next-hop type` | [9421](bgpd/bgp_routemap.c#L9421) | 同上 |
| `ipv6 next-hop prefix-list` | [9424](bgpd/bgp_routemap.c#L9424) | 同上 |
| `metric`(match) | [9427](bgpd/bgp_routemap.c#L9427) | 同上 |
| `tag`(match) | [9430](bgpd/bgp_routemap.c#L9430) | 同上 |
| `sr-te color`(set) | [9433](bgpd/bgp_routemap.c#L9433) | `generic_set_add` / `generic_set_delete` |
| `ip next-hop`(set) | [9436](bgpd/bgp_routemap.c#L9436) | 同上 |
| `ipv6 next-hop local`(set) | [9439](bgpd/bgp_routemap.c#L9439) | 同上 |
| `metric`(set) | [9442](bgpd/bgp_routemap.c#L9442) | 同上 |
| `tag`(set) | [9445](bgpd/bgp_routemap.c#L9445) | 同上 |

**为什么有这一层**：以 `ip address` 为例，它的参数是「前缀 + exact/ge/le」标准语法，与其它协议完全一致，引擎里已有通用实现 `generic_match_add`（[lib/routemap.c:686](lib/routemap.c#L686)）和通用规则对象 `route_map_rule_cmd`（注册名 `"ip address"`，由 `lib/routemap.c` 内置）。bgpd 不想重复造轮子，于是把这些规则的处理「接」到通用实现上。差异在于：bgpd 的钩子回调在转调 `generic_match_add` **之前**会先做一点 BGP 专属动作（比如登记依赖），并且在 `func_apply` 层面**复用引擎的同一份实现**。

**阅读要点**：看到「规则名 + `_hook`」就知道这是「转调通用实现」；看到「规则名 + `_cmd`」且带三个回调，就是「BGP 自研插件」（§1.3 的表）。两类规则的**命令**在 `bgp_routemap.c` 里各有一套 `DEFUN_YANG`，但**落地路径不同**：自研插件走 `bgp_routemap_nb_config.c` 里对应的 `lib_route_map_entry_*` 回调；通用钩子走 `lib_route_map_entry_*_generic_*` 回调，最终都落到 `generic_match_add` / `generic_set_add`。

### 1.6 一座「两层注册」的全景小结

把 §1.2～§1.5 收拢成一张表，这是理解「bgpd route-map 到底注册了什么」的最终答案：

| 注册动作 | 数量 | 落点 | 结果 |
|---|---|---|---|
| `route_map_init()` | 1 | [9388](bgpd/bgp_routemap.c#L9388) | 初始化引擎骨架 |
| 生命周期钩子（add/delete/event） | 3 | [9390-9392](bgpd/bgp_routemap.c#L9390) | BGP 侧重算入口 |
| 半通用钩子（match/set × add/delete） | 18 组 | [9394-9446](bgpd/bgp_routemap.c#L9394) | 转调引擎通用实现 |
| 自研 match 插件 `route_map_install_match` | 41 | [9448](bgpd/bgp_routemap.c#L9448)～[9653](bgpd/bgp_routemap.c#L9653) | 注册 BGP 专属匹配 |
| 自研 set 插件 `route_map_install_set` | 38 | [9483](bgpd/bgp_routemap.c#L9483)～[9651](bgpd/bgp_routemap.c#L9651) | 注册 BGP 专属设置 |
| `install_element(RMAP_NODE, …)` | 124 | [9518](bgpd/bgp_routemap.c#L9518)～[9670](bgpd/bgp_routemap.c#L9670) | 把命令挂到 route-map 节点 |

> **一句话**：bgpd 对 route-map 引擎的全部「接口面」就是这六类注册；改 BGP 策略行为，改的一定是这张表里的某个入口。

---

## §2 route-map 的北向接口：CLI 已是 NB 的前端

### 2.1 判断先行：10.6.1 里不存在「两条并存路径」

任务里假设「老的 VTY 命令」与「新的 NB」并存。实测结论是：**在 FRR 10.6.1，BGP route-map 的命令行已 100% 迁移到 northbound**。

证据：`bgp_routemap.c` 里所有 124 条命令的宏形式只有两种 —— `DEFUN_YANG`（87 条）与 `DEFPY_YANG`（37 条）；非 YANG 的 `DEFUN`/`DEFPY` **一条都没有**（实测 `grep -oE '^DEF[A-Z_]*' bgp_routemap.c | sort | uniq -c`）。而 `DEFUN_YANG` 只是给命令加了 `CMD_ATTR_YANG` 属性（[lib/command.h:414](lib/command.h#L414)）。

所以这里的真实结构是**一条三段式管线**，不是两条并存路径：

```
用户输入 "route-map X permit 10 / match ip address pl"
      │
      ▼  bgp_routemap.c 命令体（DEFUN_YANG）
  nb_cli_enqueue_change(vty, xpath, NB_OP_MODIFY/CREATE/DESTROY, value);
  return nb_cli_apply_changes(vty, NULL);
      │  enqueue 227 处、apply 129 处（实测）
      ▼  northbound 框架：按 YANG schema 校验 → 找到回调 → NB_EV_APPLY 调用
  bgp_routemap_nb_config.c 的 lib_route_map_entry_* 回调
      │  组装 struct routemap_hook_context，调 bgp_route_match_add / bgp_route_match_delete
      ▼  lib/routemap.c（引擎）
  route_map_add_match / route_map_delete_match / route_map_add_set / route_map_delete_set
      │
      ▼
  引擎里的规则对象（func_compile 编译）→ 真正生效时 route_map_apply 调 apply
```

一个完整的命令样例对照（`match mac address NAME`，[bgp_routemap.c:6048](bgpd/bgp_routemap.c#L6048)）：

```c
DEFUN_YANG (match_mac_address, ...)
{
	const char *xpath =
		"./match-condition[condition='frr-bgp-route-map:mac-address-list']";
	char xpath_value[XPATH_MAXLEN];

	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	snprintf(xpath_value, sizeof(xpath_value),
		 "%s/rmap-match-condition/frr-bgp-route-map:list-name", xpath);
	nb_cli_enqueue_change(vty, xpath_value, NB_OP_MODIFY, argv[3]->arg);
	return nb_cli_apply_changes(vty, NULL);
}
```

即：命令体自己**不做任何 route-map 操作**，只把「对一个 xpath 的一次 change」入队，然后交给 NB 框架。

### 2.2 NB 三层结构

| 文件 | 作用 | 关键落点 |
|---|---|---|
| `bgp_routemap_nb.c` | **映射表**：xpath → 回调。整个文件只有一个实质内容 `frr_bgp_route_map_info` | [bgp_routemap_nb.c:54](bgpd/bgp_routemap_nb.c#L54) |
| `bgp_routemap_nb.h` | **原型**：134 个回调声明 + `extern const struct frr_yang_module_info frr_bgp_route_map_info` | [bgp_routemap_nb.h:45](bgpd/bgp_routemap_nb.h#L45) |
| `bgp_routemap_nb_config.c` | **实现**：134 个回调函数体 | 首个回调 [bgp_routemap_nb_config.c:195](bgpd/bgp_routemap_nb_config.c#L195) |

模块信息在启动时注册进 bgpd 的 YANG 模块表 `bgpd_yang_modules`（[bgp_main.c:439](bgpd/bgp_main.c#L439)），数组里同时有 `frr_route_map_info`（lib 的骨架模型，[bgp_main.c:442](bgpd/bgp_main.c#L442)）与 `frr_bgp_route_map_info`（BGP 的 augment，[bgp_main.c:444](bgpd/bgp_main.c#L444)）。

**xpath 的两段式命名**是本模块最好记的规律：

```
/frr-route-map:lib/route-map/entry/
    <match-condition|set-action>/rmap-<m|s>atch-condition/
        frr-bgp-route-map:<参数名>
```

前缀 `frr-route-map` 是 **lib 定义的通用骨架**（entry、match-condition、set-action 这些容器），`frr-bgp-route-map` 是 **BGP 用 augment 挂上去的参数**（vpn-dataplane、local-preference、origin…）。同一份模型被所有协议的 route-map 共享骨架，各协议只 augment 自己的参数。

回调命名直接编码了这条路径（这也是函数名长得离谱的原因）：

```
lib_route_map_entry_<match_condition|set_action>_rmap_<match_condition|set_action>_<参数名>_<动作>
```

动作后缀 = NB 回调类型：`_create`（新增实例）/ `_modify`（改值）/ `_destroy`（删除）/ `_finish`（apply_finish，整棵子树应用完汇总）。

**易错点（注释已自曝）**：表里元素的**书写顺序不等于分类顺序**。例如 `extcommunity-limit` 的 xpath 属于 match-condition，却被排在若干 set-action 条目之间（见 [bgp_routemap_nb.c:296](bgpd/bgp_routemap_nb.c#L296)），阅读时**以 xpath 为准，不要靠相邻位置判断**。

### 2.3 交叉引用点：CLI ↔ NB 是同一份配置

| 交叉点 | 位置 | 说明 |
|---|---|---|
| 命令体 → NB 入队 | [bgp_routemap.c:6060](bgpd/bgp_routemap.c#L6060) 等 227 处 | `nb_cli_enqueue_change` |
| 命令体 → NB 应用 | [bgp_routemap.c:6065](bgpd/bgp_routemap.c#L6065) 等 129 处 | `nb_cli_apply_changes` |
| NB 回调 → 引擎 | [bgp_routemap_nb_config.c:97](bgpd/bgp_routemap_nb_config.c#L97) | `route_map_add_match(...)` |
| NB 回调 → 引擎（通用） | [bgp_routemap_nb_config.c:2042](bgpd/bgp_routemap_nb_config.c#L2042) | `generic_set_add(rhc->rhc_rmi, "distance", ...)` |
| 引擎 → bgpd 重算钩子 | [bgp_routemap.c:9390-9392](bgpd/bgp_routemap.c#L9390) | `route_map_add/delete/event_hook` |
| YANG 骨架模型 | [lib/routemap_northbound.c:1596](lib/routemap_northbound.c#L1596) | `frr_route_map_info`（.name = `frr-route-map`） |
| YANG 骨架 CLI | [lib/routemap_cli.c](lib/routemap_cli.c) | 45 条 `DEFPY_YANG`/`DEFUN_YANG`（entry 级命令，如 `route-map X permit 10`） |

**关键点**：CLI（`bgp_routemap.c`）与 NB 回调（`bgp_routemap_nb_config.c`）**不是两份配置**，而是「同一份配置的前端与后端」。CLI 命令**不直接**改 route-map；它把改动作为 YANG change 入队，NB 框架应用时才调用 `bgp_routemap_nb_config.c` 的回调去动 route-map。所以：

- 走 CLI 配置 → 走 NB 回调 → 走引擎 —— **一条路径**（这是当前唯一的路径）；
- 走 NETCONF / RESTCONF / mgmtd 配置 → 同样落到 NB 回调 → 走引擎 —— **另一条前端，同一个后端**。

这也解释了为什么同一个模块能同时被 CLI 与 NETCONF 读写：**配置模型是 YANG，回调是一份**。

### 2.4 `routemap_hook_context`：回调与引擎之间的桥

NB 回调不直接操作引擎，而是先组装一个上下文结构 `struct routemap_hook_context`（简称 rhc），记录「本次要做什么」（[bgp_routemap_nb_config.c:18-24](bgpd/bgp_routemap_nb_config.c#L18)）：

| 字段 | 含义 |
|---|---|
| `rhc_rmi` | 目标 `route_map_index`（条目：序号 + permit/deny） |
| `rhc_rule` | 规则名（如 `"vpn dataplane"`、`"local-preference"`） |
| `rhc_event` | 事件类型（`route_map_event_t`，如 `RMAP_EVENT_MATCH_ADDED`） |
| `rhc_mhook` | 销毁钩子（通常是 `bgp_route_match_delete` / `bgp_route_set_delete`） |

两个通用助手把 rhc 与引擎缝合：

- `bgp_route_match_add`（[bgp_routemap_nb_config.c:89](bgpd/bgp_routemap_nb_config.c#L89)）→ 调 `route_map_add_match`（[97](bgpd/bgp_routemap_nb_config.c#L97)），把 `rmap_compile_rets` 翻译成 CLI 风格返回码。
- `bgp_route_match_delete`（[bgp_routemap_nb_config.c:130](bgpd/bgp_routemap_nb_config.c#L130)）→ 调 `route_map_delete_match`（[154](bgpd/bgp_routemap_nb_config.c#L154)）。**坑**：删除前会把 `rmap_name` / `dep_name` 用 `XSTRDUP` 复制一份（[147](bgpd/bgp_routemap_nb_config.c#L147)、[151](bgpd/bgp_routemap_nb_config.c#L151)），因为删除会触发依赖者回调、原始字符串可能已被释放；末尾再 `XFREE`。

**多数 `_modify` 回调结构几乎一模一样**，只有三处不同：规则字符串、取值的 YANG 类型函数、返回的错误码（`NB_ERR_INCONSISTENCY` 或 `NB_ERR_VALIDATION`，历史遗留不统一）。少数复杂节点（`comm-list`、`aggregator`、`extcommunity-lb`）带 list 与 `_finish`，逻辑较重，是阅读重点。

---

## §3 策略路由（PBR）：`bgp_pbr.c` 做什么

### 3.1 判断先行：PBR 是 flowspec 的「执行侧」

`bgp_flowspec.c` 只把收到的 NLRI 收成一条 `AF_FLOWSPEC` 前缀丢进 RIB（[bgp_flowspec.c:202-207](bgpd/bgp_flowspec.c#L202)）。真正决定「这条规则在本机变成什么策略」的是 `bgp_pbr.c`。二者关系用一句话概括：

> **flowspec = 描述（匹配条件 NLRI + 动作 ecommunity）；PBR = 执行（ipset + iptables + ip rule）。`bgp_pbr.c` 负责从前者生成后者并下发 zebra。**

`bgp_pbr.c` 的产物有三类（[bgp_pbr.c:12-16](bgpd/bgp_pbr.c#L12)）：

| 产物结构 | 内核对应物 | 定义 |
|---|---|---|
| `struct bgp_pbr_match` / `bgp_pbr_match_entry` | ipset 集合 / 集合内条目 | [bgp_pbr.h:315](bgpd/bgp_pbr.h#L315) / [366](bgpd/bgp_pbr.h#L366) |
| `struct bgp_pbr_rule` | ip rule（`ip rule` / `ip route`） | [bgp_pbr.h:284](bgpd/bgp_pbr.h#L284) |
| `struct bgp_pbr_action` | 动作（fwmark / table_id / nexthop / 丢弃） | [bgp_pbr.h:400](bgpd/bgp_pbr.h#L400) |

`bgp_pbr.h` 的文件导读（[bgp_pbr.h:7-34](bgpd/bgp_pbr.h#L7)）点出了两条设计主线：**匹配与动作分离**、**去重与共享（refcnt + unique 编号）**。

### 3.2 三类哈希与去重

每个 BGP 实例维护三套哈希（[bgp_pbr.c:21-25](bgpd/bgp_pbr.c#L21)、初始化 [bgp_pbr_init:1662](bgpd/bgp_pbr.c#L1662)、拆除 [bgp_pbr_cleanup:1641](bgpd/bgp_pbr.c#L1641)）：

| 哈希 | 键 | key/equal 回调 |
|---|---|---|
| `pbr_match_hash` | 匹配特征 + vrf + family | [bgp_pbr_match_hash_key:1339](bgpd/bgp_pbr.c#L1339) / [equal:1362](bgpd/bgp_pbr.c#L1362) |
| `pbr_rule_hash` | IP rule 条目 | [bgp_pbr_rule_hash_key:1411](bgpd/bgp_pbr.c#L1411) / [equal:1426](bgpd/bgp_pbr.c#L1426) |
| `pbr_action_hash` | normalized action（table_id / fwmark / afi） | [bgp_pbr_action_hash_key:1511](bgpd/bgp_pbr.c#L1511) / [equal:1527](bgpd/bgp_pbr.c#L1527) |

**为什么要哈希**：让内容相同的对象天然去重、可被共享。注释说得直白：把「匹配特征」参与哈希，是为了「内容相同的对象能天然去重并被共享（[bgp_pbr.h:456-459](bgpd/bgp_pbr.h#L456)）」。

`unique` 是**进程级全局递增计数器**（[bgp_pbr.c:107-110](bgpd/bgp_pbr.c#L107)），一旦分配就随下发请求发给 zebra，回执时靠它找回对象。`installed` / `install_in_progress` 两个标志防重复下发与竞态。

`bgp_pbr_action` 带 `refcnt`（[bgp_pbr.h:422](bgpd/bgp_pbr.h#L422)）：多条 rule / match 可共享同一 action，只有 refcnt 归零且已安装时才真正发 delete。引用计数增减在 `bgp_pbr_bpa_add`（[bgp_pbr.c:1291](bgpd/bgp_pbr.c#L1291)）/ `bgp_pbr_bpa_remove`（[bgp_pbr.c:1277](bgpd/bgp_pbr.c#L1277)）；安装侧对 action 的 refcnt++ 实际发生在 `bgp_send_pbr_iptable`（[bgp_zebra.c:5058](bgpd/bgp_zebra.c#L5058)）。

### 3.3 从 NLRI 到 `bgp_pbr_entry_main`

中间层是 `struct bgp_pbr_entry_main`（[bgp_pbr.h:186](bgpd/bgp_pbr.h#L186)）：它把一条规则的 match 部分与 action 数组装在一起，是「外部输入 → 内部结构」的过渡视图。填充由 `bgp_pbr_build_and_validate_entry`（[bgp_pbr.c:911](bgpd/bgp_pbr.c#L911)）完成，四步（[bgp_pbr.c:896-907](bgpd/bgp_pbr.c#L896)）：

1. `bgp_flowspec_match_rules_fill()` 把 NLRI 里的 match 分量填进 `api`（[928](bgpd/bgp_pbr.c#L928)）。
2. 遍历路径上的 ecommunity 把 **set 动作**解析进 `api->actions`（[934](bgpd/bgp_pbr.c#L934) 起）：
   - `ECOMMUNITY_REDIRECT_VRF` → `ACTION_REDIRECT`（[952](bgpd/bgp_pbr.c#L952)）；
   - `REDIRECT_IP_NH` / `FLOWSPEC_REDIRECT_IPV4` → `ACTION_REDIRECT_IP`；
   - 其它扩展团体交给 `ecommunity_fill_pbr_action`（限速 / 打标 / 流量动作）。
3. 若发现「限速为 0」（即丢弃）动作，把动作数组裁剪成只看这一个动作。
4. `bgp_pbr_validate_policy_route()`（[bgp_pbr.c:649](bgpd/bgp_pbr.c#L649)）做支持性校验；最后检查 src/dst 前缀地址族一致。

`struct bgp_pbr_entry_main` 里 `type` 的三态（[bgp_pbr.h:187-189](bgpd/bgp_pbr.h#L187)）区分后续走哪条下游：

| type | 含义 | 下游路径 |
|---|---|---|
| `BGP_PBR_IPSET`(1) | flowspec 生成 | ipset + iptables |
| `BGP_PBR_IPRULE`(2) | 本机 ip rule 策略 | `bgp_pbr_rule` / ip rule |
| `BGP_PBR_UNDEFINED`(0) | 无法归类 | 直接丢弃 |

`type` 的判定在 `bgp_flowspec_match_rules_fill` 末尾（[bgp_flowspec_util.c:812-827](bgpd/bgp_flowspec_util.c#L812)）：只要任何一个 `match_*_num` 非零或 `match_bitmask` 非零 → IPSET；否则若 `match_bitmask_iprule` 里 src/dst 位被置 → IPRULE；否则 UNDEFINED。

### 3.4 支持矩阵：`bgp_pbr_validate_policy_route`

这是 PBR 的「能力边界」所在（[bgp_pbr.c:649](bgpd/bgp_pbr.c#L649)）。注释先给出支持集（[bgp_pbr.c:661-665](bgpd/bgp_pbr.c#L661)）：

```
- src/dst 前缀 => redirect nexthop [ + rate]
- src/dst 前缀 => redirect VRF     [ + rate]
- src/dst 前缀 => drop
- srcport  + @IP
```

逐步拒绝逻辑（返回 0 = 不支持，整条忽略）：

| 检查 | 行 | 判据 |
|---|---|---|
| 协议只能一个 | [667](bgpd/bgp_pbr.c#L667) | `match_protocol_num > 1` → 拒绝 |
| 前缀偏移不支持 | [673](bgpd/bgp_pbr.c#L673) | `src/dst_prefix_offset > 0` → 拒绝 |
| 协议必须是 UDP/ICMP/ICMPV6/TCP | [680](bgpd/bgp_pbr.c#L680) | 其它协议号 → 拒绝 |
| src/dst/tcpflags/icmp 匹配可归并 | [690](bgpd/bgp_pbr.c#L690) 起 | 走 `bgp_pbr_extract` / `_enumerate` 归并失败 → 拒绝 |
| flow-label | [766](bgpd/bgp_pbr.c#L766) | v4 直接拒绝；v6 **也拒绝**（当前实现不支持） |
| fragment | [787](bgpd/bgp_pbr.c#L787) | 值必须 ∈ {1,2,4,8}；IPv6 一律拒绝 |
| 端口维度不可叠加 | [838](bgpd/bgp_pbr.c#L838) | src+dst+port 总数 > 3 → 拒绝 |
| 端口与 icmp 不可混用 | [844](bgpd/bgp_pbr.c#L844) | 两者同时出现 → 拒绝 |
| iprule 只支持 redirect IP | [856](bgpd/bgp_pbr.c#L856) | drop / DSCP / redirect VRF 只告警不生效 |
| 非 iprule 必须匹配 src 或 dst | [885](bgpd/bgp_pbr.c#L885) | 都没有 → 拒绝 |

### 3.5 主调度：`bgp_pbr_handle_entry`

`bgp_pbr_handle_entry`（[bgp_pbr.c:2998](bgpd/bgp_pbr.c#L2998)）是关键分发点。它先把 `api` 的 match 部分压进 `struct bgp_pbr_filter`（[bgp_pbr.c:337](bgpd/bgp_pbr.c#L337)），并把「或关系」维度收集进 `struct bgp_pbr_or_filter`（[bgp_pbr.c:364](bgpd/bgp_pbr.c#L364)）。然后：

- `add == false`（撤销）：走 `bgp_pbr_policyroute_remove_from_zebra`（[3149](bgpd/bgp_pbr.c#L3149)）；
- `add == true`（安装）：遍历 `api->actions` 逐个安装（循环 [3158](bgpd/bgp_pbr.c#L3158)）：

| action | 处理 |
|---|---|
| `ACTION_TRAFFICRATE` rate==0 | 构造 `NEXTHOP_TYPE_BLACKHOLE` 表达 drop（[3163](bgpd/bgp_pbr.c#L3163)） |
| `ACTION_TRAFFICRATE` rate!=0 | 记下 rate（重入），但**只告警**「ignoring Set action rate」（[3175](bgpd/bgp_pbr.c#L3175)） |
| `ACTION_TRAFFIC_ACTION` | sample 只告警「Sample action Ignored」（[3184](bgpd/bgp_pbr.c#L3184)）；terminate 继续跑别的过滤 |
| `ACTION_REDIRECT_IP` | 用 `u.zr` 的 v4/v6 地址构造 nexthop，安装后 `continue_loop = 0`（重定向动作不与后续叠加） |
| `ACTION_REDIRECT` | 用 `u.redirect_vrf` 构造 nexthop，同样 `continue_loop = 0` |
| `ACTION_MARKING` | 只告警「Set DSCP/FlowLabel … Ignored」（[3224](bgpd/bgp_pbr.c#L3224)） |

**「或」维度的展开**是本文件最烧脑的部分：OR 语义的匹配（tcpflags / dscp / 分片 / icmp / pkt_len）无法用单个 ipset 表达，于是 `bgp_pbr_policyroute_add_to_zebra_recursive`（[bgp_pbr.c:2906](bgpd/bgp_pbr.c#L2906)）从 `type_entry` 维度起，逐个候选值写入 `bpf` 后递归下一维，走完所有维度就落到 `_unit` 安装。维度推进由 `bgp_pbr_next_type_entry`（[bgp_pbr.c:2280](bgpd/bgp_pbr.c#L2280)）负责；ICMP 维度特殊，交给 `bgp_pbr_icmp_action`（[bgp_pbr.c:2303](bgpd/bgp_pbr.c#L2303)）。

### 3.6 对象构造与下发：`bgp_pbr_policyroute_add_to_zebra_unit`

`bgp_pbr_policyroute_add_to_zebra_unit`（[bgp_pbr.c:2580](bgpd/bgp_pbr.c#L2580)）真正构造三类对象并下发：

1. **action**（[2613](bgpd/bgp_pbr.c#L2613) 起）：`hash_get(pbr_action_hash, ..., alloc_intern)`。若 `fwmark == 0`（新 action）：
   - blackhole → `table_id = 0`、直接标 `installed`（丢弃由 iptable 处理）；
   - 否则分配 fwmark 作 table_id；若目标是另一 VRF 且非 backend netns，则**借用该 VRF 的表 id**（[2642](bgpd/bgp_pbr.c#L2642)）。
2. **ip rule**（`type == BGP_PBR_IPRULE` 分支，[2654](bgpd/bgp_pbr.c#L2654)）：填 `MATCH_IP_SRC_SET`/`MATCH_IP_DST_SET`，priority 硬编码 20，`hash_get(pbr_rule_hash, ...)`，`bgp_send_pbr_rule_action(bpa, bpr, true)` 下发。
3. **ipset + 条目 + iptables**（[2717](bgpd/bgp_pbr.c#L2717) 起）：由 `flags` 决定 ipset 类型 —— `IPSET_NET_NET` / `IPSET_NET_PORT_NET` / `IPSET_NET_PORT` / `IPSET_NET`（枚举在 [zclient.h:1030](lib/zclient.h#L1030)）。分配 `ipset_name = "match%p"`（[2795](bgpd/bgp_pbr.c#L2795)）、`unique`/`unique2`，构好 `bgp_pbr_match_entry` 后按固定顺序下发（[2856-2879](bgpd/bgp_pbr.c#L2856)）：

```
   ip rule add → ipset create → ipset add → iptables
```

注释解释了为什么顺序是「先装新的再清旧的」：策略不是路由条目，没有 route replace 语义，所以先安装新策略、再按需清理旧策略，以保证最终一致（[bgp_pbr.c:2851-2864](bgpd/bgp_pbr.c#L2851)）。

### 3.7 PBR 数据结构速查（`bgp_pbr.h`）

`bgp_pbr.h`（504 行）几乎是纯数据类型定义。下表按「从输入到下发」的顺序排列，是改 PBR 代码时的定位图：

| 结构 / 枚举 | 定义行 | 角色 |
|---|---|---|
| `enum bgp_pbr_action_enum` | [63](bgpd/bgp_pbr.h#L63) | action 的四态：NONE / REDIRECT / REDIRECT_IP / TRAFFICRATE |
| `BGP_PBR_UNDEFINED/IPSET/IPRULE` | [187-189](bgpd/bgp_pbr.h#L187) | entry 的 type（见 §3.3） |
| `TRAFFIC_ACTION_*` | [77-79](bgpd/bgp_pbr.h#L77) | 丢弃 / 限速 |
| `OPERATOR_COMPARE_*` | [89-92](bgpd/bgp_pbr.h#L89) | 比较符（==、>、<、真值） |
| `OPERATOR_UNARY_*` | [100-101](bgpd/bgp_pbr.h#L100) | 组合符（AND / OR） |
| `BGP_PBR_MATCH_VAL_MAX` | [114](bgpd/bgp_pbr.h#L114) | 一个匹配分量最多 5 个值 |
| `struct bgp_pbr_match_val` | [116](bgpd/bgp_pbr.h#L116) | 单个「值 + 比较符 + 组合符」 |
| `FRAGMENT_*` | [129-132](bgpd/bgp_pbr.h#L129) | 分片位（DONT/IS/LAST/FIRST） |
| `struct bgp_pbr_entry_action` | [145](bgpd/bgp_pbr.h#L145) | 一条 action（含 `u` 联合：redirect vrf / rate / dscp…） |
| `struct bgp_pbr_entry_main` | [186](bgpd/bgp_pbr.h#L186) | 一条完整 flowspec 规则的内部表示（match + actions[]） |
| `struct bgp_pbr_interface` | [248](bgpd/bgp_pbr.h#L248) | PBR 下发到的接口（含 refcnt） |
| `struct bgp_pbr_config` | [265](bgpd/bgp_pbr.h#L265) | 每个 BGP 实例的 PBR 开关/策略桶 |
| `struct bgp_pbr_rule` | [284](bgpd/bgp_pbr.h#L284) | 一条 ip rule |
| `struct bgp_pbr_match` | [315](bgpd/bgp_pbr.h#L315) | 一个 ipset 集合 |
| `struct bgp_pbr_match_entry` | [366](bgpd/bgp_pbr.h#L366) | ipset 集合内的一条条目 |
| `struct bgp_pbr_action` | [400](bgpd/bgp_pbr.h#L400) | 归一化动作（refcnt 在 [422](bgpd/bgp_pbr.h#L422)） |

三类哈希的查找回调原型（[bgp_pbr.h:436-449](bgpd/bgp_pbr.h#L436)）与 hash 回调（[460-471](bgpd/bgp_pbr.h#L460)）也都在此头文件里声明。对外只有两个入口原型：`bgp_pbr_update_entry`（[481](bgpd/bgp_pbr.h#L481)）与 `bgp_pbr_build_and_validate_entry`（[501](bgpd/bgp_pbr.h#L501)）——**其余全在本文件内部静态实现**，这就是为什么 `bgp_pbr.c` 那么大却几乎不对外暴露。

### 3.8 `bgp_pbr_filter`：下发前的「指令集」

`bgp_pbr_handle_entry` 把 `bgp_pbr_entry_main` 的 match 部分压成 `struct bgp_pbr_filter`（[bgp_pbr.c:337](bgpd/bgp_pbr.c#L337)）。它比 entry 更「贴近内核」：`src` / `dst` 是指针（指向 entry 里的前缀），`src_port_min/max`、`dst_port_min/max`、`protocol`、`packet_len`、`dscp_value`、`fragment`、`tcp_flags`、`icmp_type`、`icmp_code`、`flow_label` 各一，外加一个位图 `flags`（用 `MATCH_*` 位标注哪些字段有效）。

「或」维度则收集进 `struct bgp_pbr_or_filter`（[bgp_pbr.c:364](bgpd/bgp_pbr.c#L364)）：`tcpflags`、`dscp`、`fragment`、`icmp`、`pkt_len` 各是一个二维数组 `[值维度][候选数]`。`_recursive`（[2906](bgpd/bgp_pbr.c#L2906)）就是在这几个维度上做**笛卡尔展开**——维度之间是 AND，维度内的候选值互相是 OR，于是「tcpflags ∈ {syn,ack} 且 dscp ∈ {10,20}」要被拆成 4 条 ipset 规则。这就是 PBR 代码里最绕、也最容易出效能问题的地方。

---

## §4 flowspec：NLRI 的二进制编码与解码

### 4.1 判断先行：NLRI 是一串 TLV 式的匹配分量

flowspec 的 AFI/SAFI = 1/133（IPv4，RFC 8955）、2/133（IPv6，RFC 8956）。它的 NLRI **不是前缀**，而是：

```
一条 NLRI = [长度][分量1][分量2]...[分量N]
每个分量 = [类型号(1 字节)][该类型的编码内容]
分量之间 = 与(AND)：所有条件都满足才命中
```

`bgp_flowspec_private.h` 就是这 13 种分量类型号的字典（[bgp_flowspec_private.h:55-67](bgpd/bgp_flowspec_private.h#L55)），外加内部限值 `FLOWSPEC_NLRI_SIZELIMIT`（[42](bgpd/bgp_flowspec_private.h#L42)）与 traffic-action 位定义（[47-49](bgpd/bgp_flowspec_private.h#L47)）。

### 4.2 component type 全表

| 类型号 | 宏 | 含义 | 编解码器 | 约束 |
|---|---|---|---|---|
| 1 | `FLOWSPEC_DEST_PREFIX` | 目的前缀 | `bgp_flowspec_ip_address` | 特殊前缀编码 |
| 2 | `FLOWSPEC_SRC_PREFIX` | 源前缀 | `bgp_flowspec_ip_address` | 同上 |
| 3 | `FLOWSPEC_IP_PROTOCOL` | IP 协议号 | `bgp_flowspec_op_decode` | 数值 |
| 4 | `FLOWSPEC_PORT` | 端口（源或目的） | `bgp_flowspec_op_decode` | 需协议 TCP/UDP |
| 5 | `FLOWSPEC_DEST_PORT` | 目的端口 | `bgp_flowspec_op_decode` | 需协议 TCP/UDP |
| 6 | `FLOWSPEC_SRC_PORT` | 源端口 | `bgp_flowspec_op_decode` | 需协议 TCP/UDP |
| 7 | `FLOWSPEC_ICMP_TYPE` | ICMP 类型 | `bgp_flowspec_op_decode` | |
| 8 | `FLOWSPEC_ICMP_CODE` | ICMP 码 | `bgp_flowspec_op_decode` | |
| 9 | `FLOWSPEC_TCP_FLAGS` | TCP flags | `bgp_flowspec_bitmask_decode` | 位掩码 |
| 10 | `FLOWSPEC_PKT_LEN` | 报文长度 | `bgp_flowspec_op_decode` | 数值 |
| 11 | `FLOWSPEC_DSCP` | DSCP | `bgp_flowspec_op_decode` | 数值 |
| 12 | `FLOWSPEC_FRAGMENT` | 分片位 | `bgp_flowspec_bitmask_decode` | 位掩码 |
| 13 | `FLOWSPEC_FLOW_LABEL` | flow-label | `bgp_flowspec_op_decode` | **仅 IPv6** |

> **注意差一坑**：`NUM_OF_FLOWSPEC_MATCH_TYPES` 定义为 12（[bgp_flowspec_private.h:54](bgpd/bgp_flowspec_private.h#L54)），**不含** 13 号 flow-label。实测这个宏在**全树从未被使用**（`grep -rn` 只在 private.h 命中定义与注释），所以目前是死宏；但若有人拿它给数组下标上界，会漏掉 flow-label 这一项。

分发点有两处，且完全对称：**校验时**在 `bgp_fs_nlri_validate` 的 switch（[bgp_flowspec.c:69-115](bgpd/bgp_flowspec.c#L69)），**转语义时**在 `bgp_flowspec_match_rules_fill` 的 switch（[bgp_flowspec_util.c:639-810](bgpd/bgp_flowspec_util.c#L639)）。

### 4.3 flowspec NLRI 字节布局（ASCII）

```
 一条 NLRI（在 MP_REACH 的 NLRI 字段里连续排列多条）：

 ┌────────────┬─────────────────────┬─────────────────────┬─────
 │  length    │   component #1      │   component #2      │ ...
 └────────────┴─────────────────────┴─────────────────────┴─────
      │              │                       │
      │              └── 类型号(1B) + 该类型编码
      │
      └── length 编码：首字节 < 240 时它即长度；
                       首字节 >= 240 时用 2 字节：
                       (b0 & 0x0f)<<8 | b1   （高 4 位为保留，忽略）
          见 bgp_flowspec.c:168-177

 ├─ 前缀型分量（type 1/2）──────────────────────────────────────┤
 │ ┌───────────┬──────────────────────┬────────────────────────┐ │
 │ │ prefixlen │ [IPv6 才有 offset 1B]│ addr = ceil(len/8) 字节 │ │
 │ └───────────┴──────────────────────┴────────────────────────┘ │
 │   地址不补零到整字节（紧凑排列），v6 还多一个 offset 字节       │
 │   见 bgp_flowspec_util.c:251-286                              │
 ├─ 数值型分量（type 3/4/5/6/7/8/10/11/13）─────────────────────┤
 │ ┌───────┬─────────────┐  （一个分量内可连续多组，直到 E 位置位）│
 │ │  op   │ value(N B)  │ ┌───────┬─────────────┐ ...           │
 │ └───────┴─────────────┘ └───────┴─────────────┘              │
 │   op 字节位（op[0]=bit7 … op[7]=bit0，见 hex2bin）：          │
 │     bit7 (op[0]) = E  结束位（0=本分量还有下一组）             │
 │     bit6 (op[1]) = A  与前一组的组合（0=OR, 1=AND）           │
 │     bit5..4 (op[2],op[3]) = 长度: 00→1B 01→2B 10→4B 11→8B    │
 │     bit2 (op[5]) = LT 小于                                    │
 │     bit1 (op[6]) = GT 大于                                    │
 │     bit0 (op[7]) = EQ 等于                                    │
 │   见 bgp_flowspec_util.c:365-381, 454                         │
 ├─ 位掩码型分量（type 9 TCP flags / 12 fragment）──────────────┤
 │   编码结构与数值型相同（同样的 op 字节布局），语义按位运算：   │
 │     bit6(op[1]) 组合; bit7(op[7])=精确匹配; bit6(op[6])=取反   │
 │   见 bgp_flowspec_util.c:509-522, 579-597                     │
 └──────────────────────────────────────────────────────────────┘
```

### 4.4 三种解码器

三个解码器共用同一套「解码函数 + mode 参数」设计（[bgp_flowspec_util.h:42-54](bgpd/bgp_flowspec_util.h#L42)）：

```c
enum bgp_flowspec_util_nlri_t {
	BGP_FLOWSPEC_VALIDATE_ONLY         = 0,  /* 只校验，不产出 */
	BGP_FLOWSPEC_RETURN_STRING         = 1,  /* result → char 缓冲 */
	BGP_FLOWSPEC_CONVERT_TO_NON_OPAQUE = 2,  /* result → 内部结构 */
	BGP_FLOWSPEC_RETURN_JSON           = 3,  /* result → json 对象 */
};
```

**同一段编码、同一个解码函数，靠 mode 切换「校验 / 显示 / 转语义 / JSON」四件事**。这是本模块最需要理解的设计，也带来两个阅读难点：

- `result` 是 `void *`：调用方必须按自己传的 mode 提供对应类型对象，**类型不匹配编译器不报错**（[bgp_flowspec_util.h:15-19](bgpd/bgp_flowspec_util.h#L15)）。
- 这里的 mode 与 `bgp_flowspec.h` 的 `NLRI_STRING_FORMAT_*` 是**两套不同的常量**：后者决定「字符串要多详细」，前者决定「解码结果往哪去」——名字相近，切勿混用。

**① `bgp_flowspec_op_decode`（数值型，[bgp_flowspec_util.c:331](bgpd/bgp_flowspec_util.c#L331)）**

- 用 `hex2bin`（[60](bgpd/bgp_flowspec_util.c#L60)）把 op 字节展开成 8 个 bit，`len = 2*op[2]+op[3]`，`value_size = 1 << len`（[367-368](bgpd/bgp_flowspec_util.c#L367)）。
- 用 `hexstr2num`（[76](bgpd/bgp_flowspec_util.c#L76)）把变长字节还原成大端 uint64。
- 校验：不能同时 `<` 和 `>`（[377](bgpd/bgp_flowspec_util.c#L377)）；首个元素不能置 AND 位（[380](bgpd/bgp_flowspec_util.c#L380)）。
- CONVERT 模式：`value > UINT16_MAX` 报 -1；把比较符写进 `compare_operator`、把 AND/OR 写进 `unary_operator`（[427-444](bgpd/bgp_flowspec_util.c#L427)）。值最多 `BGP_PBR_MATCH_VAL_MAX`（=5，[bgp_pbr.h:114](bgpd/bgp_pbr.h#L114)）条，超了置 `*error = -2` 停止转换（[355](bgpd/bgp_flowspec_util.c#L355)）。
- **`*error` 双重身份**：出错时是负错误码；成功时被复用为「解出多少条」的 loop 计数（[458-459](bgpd/bgp_flowspec_util.c#L458)）。读 `*error` 前必须先确认它非负。

**② `bgp_flowspec_ip_address`（前缀型，[bgp_flowspec_util.c:228](bgpd/bgp_flowspec_util.c#L228)）**

- 先读 1 字节 `prefixlen`，IPv6 再读 1 字节 offset（[251-266](bgpd/bgp_flowspec_util.c#L251)）。
- 三重防御：prefixlen 不能超过 `prefix_blen*8`（[268](bgpd/bgp_flowspec_util.c#L268)）；`psize+offset` 不能超 `max_len`（[273](bgpd/bgp_flowspec_util.c#L273)）；`psize` 必须能塞进 `struct prefix.u`（[280](bgpd/bgp_flowspec_util.c#L280)）。
- RETURN_STRING：IPv6 用 `%pFX/off` 格式带上 offset（[292](bgpd/bgp_flowspec_util.c#L292)）；CONVERT：`prefix_copy`。

**③ `bgp_flowspec_bitmask_decode`（位掩码型，[bgp_flowspec_util.c:481](bgpd/bgp_flowspec_util.c#L481)）**

- 结构同 op_decode（同样的 op 位布局）。
- 字符串模式用 `,&` / `,|` 表达组合、用 `∋`（包含）/ `!`（取反）表达掩码语义（[524-568](bgpd/bgp_flowspec_util.c#L524)）。
- CONVERT 模式：`op[6]`（GT 位，实际语义是「不同」）会**同时**置 `LESS_THAN` 与 `GREATER_THAN` 两个比较标志（[579-587](bgpd/bgp_flowspec_util.c#L579)）；`op[7]` 置 `EXACT_MATCH`。

### 4.5 转内部语义：`bgp_flowspec_match_rules_fill`

`bgp_flowspec_match_rules_fill`（[bgp_flowspec_util.c:624](bgpd/bgp_flowspec_util.c#L624)）是「从 BGP 收到 → 转成可下发策略」的关键一跳：

- 逐分量解析，前缀分量写进 `bpem->dst_prefix`/`src_prefix` 并置 `match_bitmask`（[640-680](bgpd/bgp_flowspec_util.c#L640)）。
- **特例**：若前缀是 `0.0.0.0` 或全零 IPv6，则**不**置 `match_bitmask` 而置 `match_bitmask_iprule`（表示「任意到任意」，可能无需 ipset 过滤，[666-677](bgpd/bgp_flowspec_util.c#L666)）。
- flow-label 在 v4 下恒判错（[681-685](bgpd/bgp_flowspec_util.c#L681)）。
- 数值分量走 `bgp_flowspec_call_non_opaque_decode`（[93](bgpd/bgp_flowspec_util.c#L93)）这一薄封装（固定 CONVERT 模式，把条目数回填 `match_num`）。
- 末尾按各 `match_*_num` 是否非零决定 `type`（[812-827](bgpd/bgp_flowspec_util.c#L812)，见 §3.3）。

### 4.6 `bgp_flowspec.c`：接收入口与校验

- `bgp_fs_nlri_validate`（[bgp_flowspec.c:51](bgpd/bgp_flowspec.c#L51)）：逐分量走一遍 `VALIDATE_ONLY`，任何分量解不通就整条判 BAD_FORMAT。**从严策略**：遇到不认识的分量类型号直接报错（[112-114](bgpd/bgp_flowspec.c#L112)），而 RFC 对未知分量的建议是忽略——这是实现选择差异，读代码时留意。
- `bgp_nlri_parse_flowspec`（[bgp_flowspec.c:129](bgpd/bgp_flowspec.c#L129)）：收包主循环。
  - `attr` 为空 → 按 RFC 当隐式撤销（[153](bgpd/bgp_flowspec.c#L153)）；
  - 长度编码两种（[168-177](bgpd/bgp_flowspec.c#L168)）；
  - 校验通过后组装 `AF_FLOWSPEC` 前缀，**零拷贝**把 `ptr` 指向报文缓冲（[207](bgpd/bgp_flowspec.c#L207)，生命周期依赖报文缓冲）；
  - 最后 `bgp_update` / `bgp_withdraw`（[247](bgpd/bgp_flowspec.c#L247)/[251](bgpd/bgp_flowspec.c#L251)）。
- 本文件被 `bgp_route.c:12028` 的 NLRI 分发按 SAFI 调起（cross-ref：路由卷）。

### 4.7 分量处理速查：两处 switch 的行号对照

同一个分量类型在**校验**与**转语义**两处各有一个 `case`。下表把两处行号并列，方便顺着一个类型号读完整条链路：

| 类型号 | 宏 | 校验 switch（[bgp_flowspec.c](bgpd/bgp_flowspec.c)） | 转语义 switch（[bgp_flowspec_util.c](bgpd/bgp_flowspec_util.c)） |
|---|---|---|---|
| 1 | `FLOWSPEC_DEST_PREFIX` | [70](bgpd/bgp_flowspec.c#L70) | [640](bgpd/bgp_flowspec_util.c#L640) |
| 2 | `FLOWSPEC_SRC_PREFIX` | [71](bgpd/bgp_flowspec.c#L71) | [641](bgpd/bgp_flowspec_util.c#L641) |
| 3 | `FLOWSPEC_IP_PROTOCOL` | [86](bgpd/bgp_flowspec.c#L86) | [695](bgpd/bgp_flowspec_util.c#L695) |
| 4 | `FLOWSPEC_PORT` | [87](bgpd/bgp_flowspec.c#L87) | [705](bgpd/bgp_flowspec_util.c#L705) |
| 5 | `FLOWSPEC_DEST_PORT` | [88](bgpd/bgp_flowspec.c#L88) | [715](bgpd/bgp_flowspec_util.c#L715) |
| 6 | `FLOWSPEC_SRC_PORT` | [89](bgpd/bgp_flowspec.c#L89) | [725](bgpd/bgp_flowspec_util.c#L725) |
| 7 | `FLOWSPEC_ICMP_TYPE` | [90](bgpd/bgp_flowspec.c#L90) | [735](bgpd/bgp_flowspec_util.c#L735) |
| 8 | `FLOWSPEC_ICMP_CODE` | [91](bgpd/bgp_flowspec.c#L91) | [745](bgpd/bgp_flowspec_util.c#L745) |
| 9 | `FLOWSPEC_TCP_FLAGS` | [96](bgpd/bgp_flowspec.c#L96) | [776](bgpd/bgp_flowspec_util.c#L776) |
| 10 | `FLOWSPEC_PKT_LEN` | [103](bgpd/bgp_flowspec.c#L103) | [755](bgpd/bgp_flowspec_util.c#L755) |
| 11 | `FLOWSPEC_DSCP` | [104](bgpd/bgp_flowspec.c#L104) | [766](bgpd/bgp_flowspec_util.c#L766) |
| 12 | `FLOWSPEC_FRAGMENT` | [97](bgpd/bgp_flowspec.c#L97) | [792](bgpd/bgp_flowspec_util.c#L792) |
| 13 | `FLOWSPEC_FLOW_LABEL` | [78](bgpd/bgp_flowspec.c#L78) | [681](bgpd/bgp_flowspec_util.c#L681) |

> **注意两处 switch 的 case 排列顺序并不一致**（校验侧按「前缀→flow-label→数值→位掩码」，转语义侧按类型号递增但 flow-label 被前移到 681）。对照上表时不要靠「第几个 case」推断，认类型号。

### 4.8 一个 NLRI 的解码走查（worked example）

设收到一条 IPv4 flowspec NLRI（AFI/SAFI = 1/133），字节串如下（十六进制，去掉最前面的长度字节后 12 字节）：

```
 01 18 0A 00 00 00          03 06 81 06             05 81 BB
 └─分量1─┘                  └──分量2──┘             └─分量3─┘
```

逐步走一遍（调用关系对应 §4.4 的三个解码器）：

**分量 1 —— 类型 0x01（DEST_PREFIX）**，进入 `bgp_flowspec_ip_address`（[bgp_flowspec_util.c:228](bgpd/bgp_flowspec_util.c#L228)）：
- 读 1 字节 `prefixlen = 0x18 = 24`；
- IPv4 无 offset 字节；
- `psize = ceil(24/8) = 3`，读走 3 字节 `0A 00 00`（= 10.0.0）；
- 结果：`10.0.0.0/24`。转语义模式下写进 `bpem->dst_prefix` 并置 `MATCH_IP_DST` 位（[bgp_flowspec_util.c:640](bgpd/bgp_flowspec_util.c#L640)）。

**分量 2 —— 类型 0x03（IP_PROTOCOL）**，进入 `bgp_flowspec_op_decode`（[bgp_flowspec_util.c:331](bgpd/bgp_flowspec_util.c#L331)）：
- 读 1 字节 `op = 0x81 = 1000 0001b`；
- 按位拆：`op[0]=1` → **E=1（结束）**；`op[1]=0` → A=0；`op[2]op[3]=00` → 长度编码 00 → **1 字节**；
- `op[5]=0 / op[6]=0 / op[7]=1`（`op[7]` 即 bit0 位置）→ **EQ（等于）**；
- 读 1 字节 `value = 0x06` → 协议号 6 = TCP；
- 结果：`protocol == 6`。等价于 `match tcp`。

**分量 3 —— 类型 0x05（DEST_PORT）**，进入 `bgp_flowspec_op_decode`：
- `op = 0x81` 同上：E=1、长度 1 字节、EQ；
- 读 `value = 0xBB = 187`；
- 结果：`dst-port == 187`。

**合并**：三个分量之间是 AND，得到一条规则：

```
目的 10.0.0.0/24 且 协议 TCP 且 目的端口 187
```

进入 §3 阶段：`bgp_flowspec_match_rules_fill` 末尾判定 `match_*_num` 非零 → `type = BGP_PBR_IPSET`（[bgp_flowspec_util.c:812](bgpd/bgp_flowspec_util.c#L812)）；再由 `bgp_pbr_validate_policy_route`（协议只有 TCP 一个、无 offset、TCP 属允许集）通过；最终在 `bgp_pbr_policyroute_add_to_zebra_unit` 里因为同时有 dst 前缀与 dst 端口，`temp.type` 取 `IPSET_NET_PORT`（[bgp_pbr.c:2741](bgpd/bgp_pbr.c#L2741)），下发为 `ipset create … hash:ip,port` + iptables。

> **走查要点**：① 长度字节在 NLRI 最前，每条 NLRI 一个；② 分量的「值个数」由 op 的 E 位决定，一个分量里可以塞多组；③ 前缀分量不含 op 字节，结构完全不同，这是初次阅读最容易混淆之处。

---

## §5 flowspec → PBR → zebra：跨文件、跨进程链路

### 5.1 时序图（ASCII）

```
 对端            bgpd(接收)                    bgpd(RIB→PBR)                 zebra            内核/bfdd
  │                  │                              │                          │                │
  │  MP_REACH(2/133) │                              │                          │                │
  │─────────────────▶│ bgp_nlri_parse_flowspec      │                          │                │
  │                  │  ├ bgp_fs_nlri_validate      │                          │                │
  │                  │  └ 组装 AF_FLOWSPEC 前缀       │                          │                │
  │                  │── bgp_update ─▶ RIB(FLOWSPEC) │                          │                │
  │                  │                              │                          │                │
  │                  │        路径变化 ─▶ bgp_zebra_announce_actual(bgp_zebra.c:1911)             │
  │                  │                     └ table->safi==SAFI_FLOWSPEC          │                │
  │                  │                        bgp_pbr_update_entry(bgp_pbr.c:3260)│                │
  │                  │                              │                          │                │
  │                  │      bgp_pbr_build_and_validate_entry(bgp_pbr.c:911)     │                │
  │                  │        ├ bgp_flowspec_match_rules_fill (NLRI→match)      │                │
  │                  │        └ 解析 ecommunity → actions                        │                │
  │                  │      bgp_pbr_validate_policy_route(bgp_pbr.c:649)        │                │
  │                  │      bgp_pbr_handle_entry(bgp_pbr.c:2998)                │                │
  │                  │        └ bgp_pbr_policyroute_add_to_zebra[_recursive]     │                │
  │                  │             └ ..._unit(bgp_pbr.c:2580)                    │                │
  │                  │                  ├ hash_get(action/match/rule)           │                │
  │                  │                  ├ bgp_send_pbr_ipset_match(bgp_zebra.c:4924)             │
  │                  │                  │   └ ZEBRA_IPSET_CREATE ───────────────▶│ ipset create   │
  │                  │                  ├ bgp_send_pbr_ipset_entry_match(:4953) │                │
  │                  │                  │   └ ZEBRA_IPSET_ENTRY_ADD ────────────▶│ ipset add      │
  │                  │                  ├ bgp_send_pbr_iptable(:5027)           │                │
  │                  │                  │   └ ZEBRA_IPTABLE_ADD ────────────────▶│ iptables       │
  │                  │                  └ bgp_send_pbr_rule_action(:4887)       │                │
  │                  │                      └ ZEBRA_RULE_ADD ──────────────────▶│ ip rule        │
  │                  │                              │                          │                │
  │                  │  ◀── 回执(ZAPI_IPSET_*_NOTIFY 等，带 unique) ────────────│                │
  │                  │     按 unique 找回对象、置 installed=false/true           │                │
```

### 5.2 `lib/pbr.h` 相关的数据结构落点

`bgp_pbr.c` 通过 `#include "pbr.h"`（[bgp_pbr.c:59](bgpd/bgp_pbr.c#L59)）复用 lib 侧定义：

| 落点 | 位置 | 含义 |
|---|---|---|
| `struct pbr_rule` | [lib/pbr.h:166](lib/pbr.h#L166) | 一条完整规则的编码形态（zebra 侧 `zapi_pbr_rule_encode`） |
| `MATCH_IP_SRC_SET` | [lib/pbr.h:199](lib/pbr.h#L199) | src 参与匹配 |
| `MATCH_IP_DST_SET` | [lib/pbr.h:200](lib/pbr.h#L200) | dst 参与匹配 |
| `MATCH_PORT_SRC/DST_SET`、`_RANGE_SET` | [lib/pbr.h:201-204](lib/pbr.h#L201) | 端口单值 / 区间 |
| `MATCH_DSCP_SET` / `_INVERSE_SET` | [lib/pbr.h:205-206](lib/pbr.h#L205) | DSCP 及其取反 |
| `MATCH_PKT_LEN_INVERSE_SET` | [lib/pbr.h:207](lib/pbr.h#L207) | 报文长度取反 |
| `MATCH_FRAGMENT_INVERSE_SET` | [lib/pbr.h:208](lib/pbr.h#L208) | 分片取反 |
| `MATCH_ICMP_SET` | [lib/pbr.h:209](lib/pbr.h#L209) | icmp |
| `MATCH_PROTOCOL_SET` | [lib/pbr.h:210](lib/pbr.h#L210) | 协议 |
| `MATCH_FLOW_LABEL_SET` / `_INVERSE_SET` | [lib/pbr.h:211-212](lib/pbr.h#L211) | flow-label 及其取反 |

这些位被 `bgp_pbr_policyroute_add_to_zebra_unit` 用来决定 ipset 类型与哪些字段有效（[bgp_pbr.c:2717-2786](bgpd/bgp_pbr.c#L2717)）。ips 类型的枚举 `enum ipset_type` 在 [zclient.h:1030](lib/zclient.h#L1030)，ipset 名长度 `ZEBRA_IPSET_NAME_SIZE`=32 在 [zclient.h:101](lib/zclient.h#L101)。

### 5.3 zebra 侧的发送函数（在 `bgp_zebra.c`，cross-ref 到接口卷）

| 函数 | 定义 | 发出的 zclient 消息 |
|---|---|---|
| `bgp_send_pbr_rule_action` | [bgp_zebra.c:4887](bgpd/bgp_zebra.c#L4887) | `ZEBRA_RULE_ADD` / `ZEBRA_RULE_DELETE`（[4908](bgpd/bgp_zebra.c#L4908)） |
| `bgp_send_pbr_ipset_match` | [bgp_zebra.c:4924](bgpd/bgp_zebra.c#L4924) | `ZEBRA_IPSET_CREATE` / `ZEBRA_IPSET_DESTROY`（[4937](bgpd/bgp_zebra.c#L4937)） |
| `bgp_send_pbr_ipset_entry_match` | [bgp_zebra.c:4953](bgpd/bgp_zebra.c#L4953) | `ZEBRA_IPSET_ENTRY_ADD` / `_DELETE`（[4967](bgpd/bgp_zebra.c#L4967)） |
| `bgp_send_pbr_iptable` | [bgp_zebra.c:5027](bgpd/bgp_zebra.c#L5027) | `ZEBRA_IPTABLE_ADD` / `_DELETE`（[5044](bgpd/bgp_zebra.c#L5044)，并带接口列表） |

编码函数在 `bgp_zebra.c`：`bgp_encode_pbr_rule_action`（[3565](bgpd/bgp_zebra.c#L3565)）、`bgp_encode_pbr_ipset_match`（[3625](bgpd/bgp_zebra.c#L3625)）、`bgp_encode_pbr_ipset_entry_match`（[3637](bgpd/bgp_zebra.c#L3637)）、`bgp_encode_pbr_iptable_match`（[3662](bgpd/bgp_zebra.c#L3662)）。

**flowspec 表在路由侧的分流点**：`bgp_zebra_announce_actual` 在 [bgp_zebra.c:1927](bgpd/bgp_zebra.c#L1927) 判断 `table->safi == SAFI_FLOWSPEC` 后直接转 PBR（不下发路由）；撤销侧在 `bgp_zebra_withdraw_actual` [bgp_zebra.c:2133](bgpd/bgp_zebra.c#L2133)。

---

## §6 `bgp_flowspec_vty.c`：展示与配置层

### 6.1 职责边界

本文件**不解析 NLRI、不下发策略**，它只做三件事：① 把二进制 NLRI 渲染成人读文本 / JSON；② 提供 flowspec 的 debug 与 local-install 配置；③ 「某 IP 命中哪些规则」的反查（[bgp_flowspec_vty.c:9-12](bgpd/bgp_flowspec_vty.c#L9)）。

### 6.2 两张分量标签表

分量类型号 → 英文标签有两张表，按显示模式选用：

| 表 | 定义 | 风格 |
|---|---|---|
| `bgp_flowspec_display_large` | [bgp_flowspec_vty.c:56](bgpd/bgp_flowspec_vty.c#L56) | 完整名（`Destination Address`、`IP Protocol`、`TCP Flags`…） |
| `bgp_flowspec_display_min` | [bgp_flowspec_vty.c:76](bgpd/bgp_flowspec_vty.c#L76) | 缩写（`to`、`from`、`proto`、`port`、`dstp`、`srcp`、`flwlbl`…） |

用 lib 的 `lookup_msg` 按类型号取标签。`FS_STRING_UPDATE` 宏（[bgp_flowspec_vty.c:102](bgpd/bgp_flowspec_vty.c#L102)）在追加下一个分量前按模式补分隔符（DEBUG→`, `、MIN→空格、LARGE/JSON→不加）。

### 6.3 核心函数

| 函数 | 定义 | 职责 |
|---|---|---|
| `bgp_fs_nlri_get_string` | [bgp_flowspec_vty.c:141](bgpd/bgp_flowspec_vty.c#L141) | 整条 NLRI → 字符串/JSON（最核心）；按类型分派到三个解码器（RETURN_STRING 模式） |
| `route_vty_out_flowspec` | [bgp_flowspec_vty.c:328](bgpd/bgp_flowspec_vty.c#L328) | 单条条目输出：NLRI + ecommunity 动作 + 接收时长 + （LARGE 下）本机 PBR 安装情况 |
| `bgp_show_table_flowspec` | [bgp_flowspec_vty.c:500](bgpd/bgp_flowspec_vty.c#L500) | 遍历整张表逐条输出（`show bgp ... flowspec` 的实现） |
| `bgp_fs_config_write_pbr` | [bgp_flowspec_vty.c:584](bgpd/bgp_flowspec_vty.c#L584) | 把 PBR 接口配置写回 `show running-config`（`local-install <name>`），返回「是否需要声明配置节」 |
| `bgp_fs_local_install_interface` | [bgp_flowspec_vty.c:621](bgpd/bgp_flowspec_vty.c#L621) | `[no] local-install INTERFACE` 的实际逻辑：维护接口红黑树与 `any` 标志（互斥） |
| `bgp_flowspec_display_match_per_ip` | [bgp_flowspec_vty.c:705](bgpd/bgp_flowspec_vty.c#L705) | 反查「某 IP 命中哪些规则」，用 `bgp_flowspec_contains_prefix`（[bgp_flowspec_util.c:119](bgpd/bgp_flowspec_util.c#L119)）判断 |
| `bgp_flowspec_vty_init` | [bgp_flowspec_vty.c:736](bgpd/bgp_flowspec_vty.c#L736) | 注册命令：debug 挂 ENABLE/CONFIG 节点，local-install 挂 `BGP_FLOWSPECV4_NODE`/`V6` |

`route_vty_out_flowspec` 的 LARGE 分支会遍历 `extra->flowspec->bgp_fs_pbr` / `bgp_fs_iprule` 列出本机安装的 ipset / rule 名（[bgp_flowspec_vty.c:434-488](bgpd/bgp_flowspec_vty.c#L434)），并用一个临时 `list_bpm` 去重——这是「展示层读 PBR 状态」的唯一耦合点。

`bgp_flowspec_vty_init` 由 `bgpd.c:10600` 调用（在 `bgp_route_map_init` 之后）。

---

## §7 BFD：用 BFD 把邻居故障检测降到毫秒级

### 7.1 判断先行：BGP 只是 BFD 的使用者

BFD（RFC 5880）用高频双向探测在毫秒级发现故障。真正的状态机与探测在 **`bfdd` 守护进程**里；bgpd 侧（`bgp_bfd.c`）只负责「哪个 neighbor 要开 BFD、单跳还是多跳、源地址变了怎么办」（[bgp_bfd.h:8-15](bgpd/bgp_bfd.h#L8)）。

### 7.2 单跳 vs 多跳：`PEER_IS_MULTIHOP`

BFD 会话按 (目的, 源, 出接口, VRF) 区分，单跳与多跳走**不同的端口与传输方式**（多跳走 UDP 4784、TTL=255）。判据宏在 [bgp_bfd.h:46](bgpd/bgp_bfd.h#L46)：

```c
#define PEER_IS_MULTIHOP(peer)                                                 \
	((((peer)->sort == BGP_PEER_IBGP) && !(peer)->shared_network)          \
	 || is_ebgp_multihop_configured((peer)))
```

即：**iBGP 且非共享网段**，或**显式配了 ebgp-multihop** → 多跳；其余（eBGP 直连单跳）→ 单跳。判错会导致 BFD 会话根本建不起来。

### 7.3 会话生命周期与触发条件

`struct peer` 上的 BFD 配置是 `struct bfd_session_config *bfd_config`（[bgpd.h:2327](bgpd/bgpd.h#L2327) 定义，成员在 [2358](bgpd/bgpd.h#L2358)），含 `session`（libbfd 句柄）、定时器、profile、`hold_time`、`t_hold_timer`、`manual` 标志等。

| 生命周期函数 | 定义 | 作用 |
|---|---|---|
| `bgp_peer_configure_bfd` | [bgp_bfd.c:405](bgpd/bgp_bfd.c#L405) | 为 peer 建会话（幂等）。`bfd_sess_new(bfd_session_status_update, p)` |
| `bgp_peer_config_apply` | [bgp_bfd.c:165](bgpd/bgp_bfd.c#L165) | **真正应用**（含组继承）。组场景递归到成员 |
| `bgp_peer_bfd_update_source` | [bgp_bfd.c:250](bgpd/bgp_bfd.c#L250) | 处理 `update-source` 改动（源地址变了必须重建会话） |
| `bgp_peer_remove_bfd_config` | [bgp_bfd.c:539](bgpd/bgp_bfd.c#L539) | 摘掉配置并注销会话（组↔单邻居分派） |
| `bgp_group_configure_bfd` | [bgp_bfd.c:482](bgpd/bgp_bfd.c#L482) | 组级配置，成员继承 |
| `bgp_bfd_init` | [bgp_bfd.c:965](bgpd/bgp_bfd.c#L965) | 进程初始化：注册命令 + `bfd_protocol_integration_init` |

**触发建立 / 应用 / 拆除的调用点**（实测）：
- **建立/应用**：`neighbor X bfd`（[608](bgpd/bgp_bfd.c#L608)）、组 bfd、profile（[897](bgpd/bgp_bfd.c#L897)）、strict hold-time（[756](bgpd/bgp_bfd.c#L756)）、cbit（[798](bgpd/bgp_bfd.c#L798)）；以及 **peer 从组继承**时 `bgpd.c:3880-3881` 调 `configure_bfd + config_apply`。
- **源地址刷新**：状态机在 OpenSent/连接变化时调 `bgp_fsm.c:2633`、[2698](bgpd/bgp_fsm.c#L2698)、[3231](bgpd/bgp_fsm.c#L3231)；NHT 变化时 `bgp_nexthop.c:555`、`bgp_zebra.c:1176`；`update-source` 配置改动时 `bgpd.c` 多处（[6913](bgpd/bgpd.c#L6913) 起）。
- **拆除**：删邻居 / 换组时 `bgpd.c:1808`、[3537](bgpd/bgpd.c#L3537)、组删 [4053](bgpd/bgpd.c#L4053)。

### 7.4 与 bfdd 的交互：`ZEBRA_BFD_DEST_*`

BGP **不直接**跟 bfdd 说话，链路是：

```
bgp_bfd.c ── libbfd(bfd_sess_*) ── zclient(bgp_zclient) ── zebra ── bfdd
```

- `bgp_bfd_init` 调 `bfd_protocol_integration_init(bgp_zclient, tm)`（[lib/bfd.c:1293](lib/bfd.c#L1293)）：设置 `bsglobal.zc = bgp_zclient`、`zc->bfd_integration = true`，并发出 `ZEBRA_BFD_CLIENT_REGISTER`（[lib/bfd.c:1307](lib/bfd.c#L1307)）。
- 安装/卸载是**延迟生效**的（batched）：`bfd_sess_install`（[lib/bfd.c:933](lib/bfd.c#L933)）安排一次性事件，事件体 `_bfd_sess_send`（[lib/bfd.c:628](lib/bfd.c#L628)）按状态决定命令：
  - `BSE_INSTALL` 且未装 → `ZEBRA_BFD_DEST_REGISTER`（[lib/bfd.c:639](lib/bfd.c#L639)）；
  - `BSE_INSTALL` 且已装 → `ZEBRA_BFD_DEST_UPDATE`（[638](lib/bfd.c#L638)）；
  - 否则 → `ZEBRA_BFD_DEST_DEREGISTER`（[641](lib/bfd.c#L641)）；
  - 最后 `zclient_bfd_command(bsglobal.zc, &bsp->args)`（[647](lib/bfd.c#L647)）。
- 命令枚举定义在 [zclient.h:179-182](lib/zclient.h#L179)（REGISTER/DEREGISTER/UPDATE/REPLAY）。
- 回执方向：bfdd → zebra → zclient 回调（`bfd_session_status_update`，注册于 `bfd_sess_new`）→ `bgp_bfd.c` 的 `bfd_session_status_update`（[bgp_bfd.c:100](bgpd/bgp_bfd.c#L100)）。

### 7.5 状态回调：Down 与 Up 的处理

`bfd_session_status_update`（[bgp_bfd.c:100](bgpd/bgp_bfd.c#L100)）只关注两件：

**Down（UP→DOWN，[113](bgpd/bgp_bfd.c#L113)）**：
1. 若在做 NFM（优雅重启）且对端 C 位已清 → 忽略这次 Down（[114](bgpd/bgp_bfd.c#L114)）；
2. 若 Down 事件晚于本会话建立时间（`bss->last_event > peer->uptime`），则：
   - 配了**严格 holdtime**（`PEER_FLAG_TIMER` 且 holdtime 为 0，或默认 holdtime 为 0）→ 起 `t_hold_timer` 延迟拆除（[133](bgpd/bgp_bfd.c#L133)），到期由 `bgp_bfd_strict_holdtime_expire`（[73](bgpd/bgp_bfd.c#L73)）拆会话；
   - 否则立即标记 BFD Down、发 `BGP_NOTIFY_CEASE_BFD_DOWN` 通知、投递 `BGP_Stop`（[137-143](bgpd/bgp_bfd.c#L137)，注释标注 RFC 9384）。

**Up（非 UP→UP，BGP 尚未 Established，[148](bgpd/bgp_bfd.c#L148)）**：若未抑制启动，刷新下一跳跟踪并投递 `BGP_Start` 主动重建邻居。

### 7.6 BFD 会话配置结构（`struct bfd_session_config`）

每个 peer（以及邻居组）挂一份 `bfd_config`（定义 [bgpd.h:2327](bgpd/bgpd.h#L2327)，成员指针在 [2358](bgpd/bgpd.h#L2358)）：

| 字段 | 行 | 含义 |
|---|---|---|
| `manual` | [2335](bgpd/bgpd.h#L2335) | 用户是否显式为该 peer 配了 BFD（只对实 peer 有意义，组无此位） |
| `cbit` | [2337](bgpd/bgpd.h#L2337) | Control Plane Independent（`neighbor X bfd check-control-plane-failure`） |
| `detection_multiplier` | [2339](bgpd/bgpd.h#L2339) | 检测倍数（无 bfdd 时的本地参数） |
| `min_rx` / `min_tx` | [2341](bgpd/bgpd.h#L2341) / [2343](bgpd/bgpd.h#L2343) | 最小收发间隔（微秒，无 bfdd 时用） |
| `profile` | [2345](bgpd/bgpd.h#L2345) | bfdd profile 名（有 bfdd 时用） |
| `session` | [2347](bgpd/bgpd.h#L2347) | libbfd 会话句柄（`struct bfd_session_params *`） |
| `hold_time` | [2352](bgpd/bgpd.h#L2352) | 严格 holdtime 值（默认 30 秒） |
| `t_hold_timer` | [2357](bgpd/bgpd.h#L2357) | 严格 holdtime 定时器 |

**有 bfdd 与无 bfdd 的配置路径不同**（这是 `HAVE_BFDD` 分支的实质）：
- **有 bfdd**：只写 `profile`，间隔/倍数交给 bfdd 端 profile 决定 → `bfd_sess_set_profile`（[bgp_bfd.c:190](bgpd/bgp_bfd.c#L190)、[218](bgpd/bgp_bfd.c#L218)、[221](bgpd/bgp_bfd.c#L221)）；
- **无 bfdd**：bgpd 自己把倍数/间隔写进会话 → `bfd_sess_set_timers`（[bgp_bfd.c:185](bgpd/bgp_bfd.c#L185)、[228](bgpd/bgp_bfd.c#L228)、[233](bgpd/bgp_bfd.c#L233)）。

### 7.7 严格 holdtime（strict hold-time）机制

当邻居的 BGP holdtime 被协商为 **0**（永不断开）时，若其 BFD 会话是 Down，BGP 就会永远挂着。严格 holdtime 就是为了兜底这个场景：BFD Down 时启动一个本地定时器 `t_hold_timer`（默认 30 秒，[bgpd.h:2352](bgpd/bgpd.h#L2352)），到期仍未 Up 就强制拆会话。

流程（[bgp_bfd.c:129-136](bgpd/bgp_bfd.c#L129)）：

```
BFD Down 且 holdtime==0（或未配 hold-time）
      │  event_add_timer(bm->master, bgp_bfd_strict_holdtime_expire, peer, hold_time, &t_hold_timer)
      ▼  （bgp_bfd.c:133）
定时器到期 → bgp_bfd_strict_holdtime_expire（bgp_bfd.c:73）
      │  若 peer 仍 Up 则标记 Down 并投递 BGP_Stop
      ▼
BFD Up / 会话拆除 → event_cancel(&t_hold_timer)（bgp_bfd.c:778）撤掉定时器
```

配置入口：`neighbor X bfd strict-hold-time HOLD-TIME`（`neighbor_bfd_strict`，[bgp_bfd.c:729](bgpd/bgp_bfd.c#L729)）与 `strict_hold_time`（[756](bgpd/bgp_bfd.c#L756)）；`check-control-plane-failure`（[798](bgpd/bgp_bfd.c#L798)）则控制 `cbit`。**注意**：严格 holdtime 只有在 `PEER_FLAG_TIMER` 置位且 holdtime==0，或默认 holdtime==0 时才生效（[bgp_bfd.c:131](bgpd/bgp_bfd.c#L131)）——配了非零 holdtime 的邻居不需要它。

---

## §8 条件编译开关

| 文件 | 开关 | 影响 |
|---|---|---|
| `bgp_bfd.c` | `HAVE_BFDD`（>0 / ==0） | 有 bfdd：命令只写 `bfd`、有 profile 命令、`neighbor bfd (参数)` 命令被隐藏（[bgp_bfd.c:683](bgpd/bgp_bfd.c#L683)）；无 bfdd：命令写探测倍数/间隔（[568](bgpd/bgp_bfd.c#L568)），`neighbor bfd (2-255)(50-60000)(50-60000)` 可见（[637](bgpd/bgp_bfd.c#L637)） |
| `bgp_bfd.c` | `VTYSH_EXTRACT_PL` | 排除 clippy 生成文件（[57](bgpd/bgp_bfd.c#L57)） |
| `bgp_routemap.c` | `ENABLE_BGP_VNC` | 引入 `rfapi` 头并调 `vnc_routemap_update`（[112](bgpd/bgp_routemap.c#L112)、[5943](bgpd/bgp_routemap.c#L5943)、[5985](bgpd/bgp_routemap.c#L5985)） |
| `bgp_routemap.c` | `HAVE_SCRIPTING` | Lua 脚本 match（`route_match_script_cmd`，[561-687](bgpd/bgp_routemap.c#L561)）；装插件/命令各一处（[9452](bgpd/bgp_routemap.c#L9452)、[9671](bgpd/bgp_routemap.c#L9671)） |
| `bgp_routemap.c` | `KEEP_OLD_VPN_COMMANDS` | 老式 `set vpn nexthop`（[9631](bgpd/bgp_routemap.c#L9631)） |
| **flowspec 四个文件** | **无任何 `HAVE_*` 条件编译** | 全部无条件编译（实测 flowspec 的 .c/.h 里没有 `#if`/`#ifdef` 功能开关） |
| `bgp_pbr.c` / `bgp_pbr.h` / `bgp_flowspec*.h` | 无 | 同上 |

> **注**：flowspec 支持是 bgpd 的固定编译特性，没有单独的 `HAVE_FLOWSPEC` 开关；能否用取决于对端是否激活 `address-family ipv4/ipv6 flowspec` 邻居。

---

## §9 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 把 `bgp_routemap.c` 的命令当成「老的 VTY 直改」路径 | 误以为存在两套并存配置，找不到真正的落地代码 | 10.6.1 已 100% 北向化，命令体只 `nb_cli_enqueue_change` + `apply_changes`；落地看 `bgp_routemap_nb_config.c` |
| 2 | match 规则返回 `RMAP_NOOP` 当成「真」 | 求值语义错 | `RMAP_NOOP` 表示「不适用」，不是命中；见 [routemap.h:105](lib/routemap.h#L105) |
| 3 | `bgp_flowspec_op_decode` 的 `*error` 当纯错误码读 | 成功时它其实是「条目数」 | 先确认 `*error >= 0` 再用；见 [bgp_flowspec_util.c:458](bgpd/bgp_flowspec_util.c#L458) |
| 4 | 误用 `NUM_OF_FLOWSPEC_MATCH_TYPES`(=12) 给含 flow-label 的数组设上界 | 漏掉 13 号 | 该宏当前全树未用；含 flow-label 的表要按 13 算 |
| 5 | 把 `NLRI_STRING_FORMAT_*` 与 `enum bgp_flowspec_util_nlri_t` 混用 | 一个决定「字符串多详细」、一个决定「结果往哪去」，传错 mode 编译不报错但结果错 | 明确二者用途，见 [bgp_flowspec_util.h:20-21](bgpd/bgp_flowspec_util.h#L20) |
| 6 | 以为 `bgp_pbr_action` 是每条规则独有 | 重复下发 / 提前释放 | 它可被多条 rule/match 共享，靠 `refcnt` 管理；归零才 delete |
| 7 | 以为 PBR 先删旧再装新 | 一致性窗口 | 顺序是「先装新的，再按需清旧的」（[bgp_pbr.c:2851](bgpd/bgp_pbr.c#L2851)） |
| 8 | BFD 判成单跳/多跳错 | 会话建不起来 | 核对 `PEER_IS_MULTIHOP`：iBGP 非共享网段 = 多跳 |
| 9 | 改了 BFD 配置忘了 `bgp_peer_config_apply` | 配置看起来生效了其实没生效 | `configure_bfd` 之后**必须** `config_apply`（[bgp_bfd.h:95-96](bgpd/bgp_bfd.h#L95)） |
| 10 | 以为 BGP 直接给 bfdd 发消息 | 找错文件 | 经 libbfd + zclient + zebra 中转，`ZEBRA_BFD_DEST_*` 定义在 [zclient.h:179](lib/zclient.h#L179) |
| 11 | flowspec 前缀 `ptr` 零拷贝的生命周期 | 悬垂指针 | `p.u.prefix_flowspec.ptr` 指向报文缓冲，生命周期依赖报文缓冲 |
| 12 | 拿 `bgp_flowspec_private.h` 的 240 当「非协议值」 | 误以为可随意改 | 240 正是 RFC 8955 §4.1 里「1 字节 vs 2 字节长度」的分界；见 §11 勘误 |
| 13 | 以为 flow-label 在 v6 一定支持 | 白忙 | `bgp_pbr_validate_policy_route` 对 flow-label **v4/v6 都拒绝**（[bgp_pbr.c:785](bgpd/bgp_pbr.c#L785)） |
| 14 | `bgp_routemap_nb.c` 表里元素按位置判 match/set | 分类错 | 以 xpath 为准（如 `extcommunity-limit` 位置在 set 之间但属 match） |

### 9.1 勘误：注释与代码不符之处

按体例专设本节（格式：`文件:行号 —— 注释说 X，代码实际是 Y`）：

1. **`bgp_flowspec_private.h:41` —— 注释说** `FLOWSPEC_NLRI_SIZELIMIT`(=240)「不是协议规定值，是 FRR 的实现约束……不要当成『标准里写的 240』」，**代码实际是**：该值正是 RFC 8955 §4.1 规定的长度字段编码分界（首字节 <240 为 1 字节长度，>=240 时用 2 字节），`bgp_flowspec.c:169` 拿它做的正是这个协议判定。注释把「协议分界值」误述为「实现自定上限」。（宏名 `SIZELIMIT` 也呼应了注释的「上限」错觉。）

2. **`bgp_flowspec.c:110` —— 注释说**「(0x19=25 之外的)分量按 RFC 应忽略」，**代码实际是**：default 分支对所有未知类型一律 `error = -1`（从严报错），并不区分「0x19」。该注释里的「0x19」在 RFC 8955/8956 的分量类型表里没有对应含义，疑似从别处转录残留；不影响代码行为，但会误导读者。

3. **`bgp_flowspec_private.h:48-49` —— 注释说**三个 traffic-action 宏「不是互斥枚举，是各自的位」，**代码实际是**：`FLOWSPEC_TRAFFIC_ACTION_TERMINAL`=1 与 `FLOWSPEC_TRAFFIC_ACTION_DISTRIBUTE`=1 **取值相同**，且 `DISTRIBUTE` 在全树**从未被使用**（`bgp_ecommunity.c` 只用 `1 << TERMINAL` 与 `1 << SAMPLE`，实测）。注释的方向正确（确实是位），但 `DISTRIBUTE` 是冗余死宏，易被误读为第二个独立位。

---

## §10 自测题（复习用）

<details><summary>1. route-map 的引擎在哪个文件？bgpd 提供什么？</summary>

引擎在 `lib/routemap.c`（条目、序号、permit/deny、求值循环）。bgpd 通过 `struct route_map_rule_cmd`（`lib/routemap.h:186`）注册 79 个插件（41 match + 38 set），注册总表在 `bgp_route_map_init`（`bgp_routemap.c:9386`）。
</details>

<details><summary>2. 一个 route_map_rule_cmd 有哪几个回调字段？</summary>

`str`（规则名）、`func_apply`（求值）、`func_compile`（编译参数）、`func_free`（释放）、`func_get_rmap_rule_key`（取去重键）。见 `lib/routemap.h:186`。
</details>

<details><summary>3. `RMAP_NOOP` 是什么意思？</summary>

「本规则对当前这条路由不适用」，既非命中亦非未命中。见 `lib/routemap.h:105`。
</details>

<details><summary>4. FRR 10.6.1 的 BGP route-map CLI 是走 VTY 直改还是北向？</summary>

北向。124 条命令全是 `DEFUN_YANG`/`DEFPY_YANG`，命令体只 `nb_cli_enqueue_change` + `nb_cli_apply_changes`，落地在 `bgp_routemap_nb_config.c`。
</details>

<details><summary>5. NB 的三层文件各做什么？</summary>

`bgp_routemap_nb.c` = xpath→回调映射表（`frr_bgp_route_map_info`，:54）；`bgp_routemap_nb.h` = 134 个回调原型；`bgp_routemap_nb_config.c` = 134 个回调实现（首个 :195）。
</details>

<details><summary>6. xpath 里 `frr-route-map` 与 `frr-bgp-route-map` 的分工？</summary>

`frr-route-map` 是 lib 的通用骨架（entry、match-condition、set-action）；`frr-bgp-route-map` 是 BGP 用 augment 挂的参数（vpn-dataplane…）。
</details>

<details><summary>7. `struct routemap_hook_context` 记录哪四样？</summary>

`rhc_rmi`（目标条目）、`rhc_rule`（规则名）、`rhc_event`（事件类型）、`rhc_mhook`（销毁钩子）。见 `bgp_routemap_nb_config.c:18`。
</details>

<details><summary>8. PBR 的三类产物结构是什么？</summary>

`bgp_pbr_match`/`bgp_pbr_match_entry`（ipset + 条目）、`bgp_pbr_rule`（ip rule）、`bgp_pbr_action`（动作）。
</details>

<details><summary>9. `bgp_pbr_entry_main.type` 三态含义？</summary>

`BGP_PBR_IPSET`(flowspec→ipset/iptables)、`BGP_PBR_IPRULE`(本机 ip rule)、`BGP_PBR_UNDEFINED`。判定在 `bgp_flowspec_util.c:812`。
</details>

<details><summary>10. 为什么 action 需要 refcnt？</summary>

多条 rule/match 可共享同一 action（归一化动作），只有 refcnt 归零且已安装时才真正发 delete。
</details>

<details><summary>11. PBR 下发的四步顺序？</summary>

`ip rule add → ipset create → ipset add → iptables`（`bgp_pbr.c:2856`）。先装新的、再清旧的。
</details>

<details><summary>12. flowspec NLRI 的结构是什么？</summary>

`[长度][分量]…`；分量 = 1 字节类型号 + 该类型编码；分量之间是 AND。
</details>

<details><summary>13. flowspec 的长度编码规则？</summary>

首字节 <240 即长度；>=240 时用 2 字节 `(b0&0x0f)<<8 | b1`。见 `bgp_flowspec.c:168`。
</details>

<details><summary>14. 13 种分量类型分别用什么解码器？</summary>

前缀(1/2)→`bgp_flowspec_ip_address`；数值(3/4/5/6/7/8/10/11/13)→`bgp_flowspec_op_decode`；位掩码(9/12)→`bgp_flowspec_bitmask_decode`。
</details>

<details><summary>15. 数值分量的 op 字节位怎么解读？</summary>

bit7=E(结束)、bit6=A(AND/OR)、bit5-4=长度(1/2/4/8B)、bit2=LT、bit1=GT、bit0=EQ。
</details>

<details><summary>16. `*error` 在 op_decode 里的双重身份是什么？</summary>

出错时是负错误码；成功时被复用为「解出条目数」的 loop 计数。
</details>

<details><summary>17. `enum bgp_flowspec_util_nlri_t` 四个 mode 的含义？</summary>

VALIDATE_ONLY(0)、RETURN_STRING(1)、CONVERT_TO_NON_OPAQUE(2)、RETURN_JSON(3)。注意 0 是「只校验」。
</details>

<details><summary>18. flowspec 前缀是 0.0.0.0 时怎么处理？</summary>

不置 `match_bitmask` 而置 `match_bitmask_iprule`（表示任意到任意，可能无需 ipset）。见 `bgp_flowspec_util.c:666`。
</details>

<details><summary>19. flowspec 表变化时怎样进入 PBR？</summary>

`bgp_zebra_announce_actual`/`withdraw_actual` 判断 `safi == SAFI_FLOWSPEC` 后直接调 `bgp_pbr_update_entry`，不下发路由。见 `bgp_zebra.c:1928`/`2135`。
</details>

<details><summary>20. `bgp_flowspec_vty.c` 的两张标签表分别用于什么模式？</summary>

`_large`（完整名）用于 LARGE/DEBUG；`_min`（缩写）用于 MIN。JSON 用 `lookup_msg` 拿名字当键。
</details>

<details><summary>21. BFD 单跳/多跳判据是什么？</summary>

`PEER_IS_MULTIHOP`：iBGP 且非共享网段，或配了 ebgp-multihop → 多跳；否则单跳。见 `bgp_bfd.h:46`。
</details>

<details><summary>22. BGP 怎样把 BFD 会话请求发给 bfdd？</summary>

`bgp_bfd.c → libbfd(bfd_sess_*) → _bfd_sess_send → zclient_bfd_command → zebra → bfdd`，消息是 `ZEBRA_BFD_DEST_REGISTER/UPDATE/DEREGISTER`。
</details>

<details><summary>23. BFD Down 时 BGP 何时立即拆、何时延迟拆？</summary>

配了严格 holdtime（`PEER_FLAG_TIMER` 且 holdtime=0，或默认 holdtime=0）→ 起 `t_hold_timer` 延迟；否则立即发 CEASE + 投 `BGP_Stop`。见 `bgp_bfd.c:130`。
</details>

<details><summary>24. BFD Up（BGP 尚未 Established）时做什么？</summary>

刷新 NHT 并投递 `BGP_Start` 主动重建邻居。见 `bgp_bfd.c:148`。
</details>

<details><summary>25. 哪些条件会触发 BFD 会话拆除？</summary>

`no neighbor X bfd`、从手动配置切回组配置、删邻居/换组（`bgp_peer_remove_bfd_config`，调用点 `bgpd.c:1808/3537/4053`）。
</details>

---

## §11 代码索引（按建议阅读顺序）

**第一站：route-map 与引擎的边界（约 30 分钟）**
1. `lib/routemap.h:186`（`struct route_map_rule_cmd`）—— 建立「插件接口」认知。
2. `lib/routemap.h:120`（`enum route_map_cmd_result_t`）—— 建立「match 返回三态、set 返回二态、NOOP 特殊」认知。
3. `bgp_routemap.c:479`（`route_match_peer_cmd`）与 `:447`（compile）—— 看一个完整插件三件套。
4. `bgp_routemap.c:9386`（`bgp_route_map_init`）—— 看 79 个插件的注册总表。

**第二站：北向接口（约 40 分钟）**
5. `lib/command.h:414`（`DEFUN_YANG`）—— 理解命令属性。
6. `bgp_routemap.c:6048`（`match_mac_address`）—— 看一条命令如何只做 enqueue+apply。
7. `bgp_routemap_nb.c:54`（`frr_bgp_route_map_info`）—— 看 xpath→回调映射表与两段式命名。
8. `bgp_routemap_nb_config.c:89`（`bgp_route_match_add`）与 `:195`（首个回调）—— 看 NB 回调如何落到引擎。

**第三站：flowspec 编码（约 60 分钟）**
9. `bgp_flowspec_private.h:55`（分量类型号表）—— 建立「13 种分量」认知。
10. `bgp_flowspec.c:51`（`bgp_fs_nlri_validate`）—— 看逐分量校验与分发。
11. `bgp_flowspec_util.c:60` / `:76`（hex2bin / hexstr2num）—— 基础工具。
12. `bgp_flowspec_util.c:331`（`op_decode`）—— 数值分量与 op 位语义。
13. `bgp_flowspec_util.c:228`（`ip_address`）与 `:481`（`bitmask_decode`）—— 另两类解码器。
14. `bgp_flowspec_util.c:624`（`match_rules_fill`）—— 转内部 PBR 语义、决定 type 三态。

**第四站：PBR 翻译与下发（约 60 分钟）**
15. `bgp_pbr.h:186`（`bgp_pbr_entry_main`）—— 中间层结构。
16. `bgp_pbr.h:337`（`bgp_pbr_filter`）与 `:364`（`bgp_pbr_or_filter`）—— 下发前指令集与 OR 集合。
17. `bgp_pbr.c:911`（`bgp_pbr_build_and_validate_entry`）与 `:649`（`validate_policy_route`）—— 构建与支持矩阵。
18. `bgp_pbr.c:2998`（`handle_entry`）与 `:2580`（`_unit`）—— 动作分发与对象构造。
19. `bgp_zebra.c:4887/4924/4953/5027` —— 四个 `bgp_send_pbr_*`，看 zclient 消息边界。
20. `bgp_pbr.c:3260`（`bgp_pbr_update_entry`）→ `bgp_zebra.c:1928` —— 谁把 flowspec 变化导进 PBR。

**第五站：展示层（约 20 分钟）**
21. `bgp_flowspec_vty.c:141`（`bgp_fs_nlri_get_string`）—— 整条 NLRI 渲染。
22. `bgp_flowspec_vty.c:328`（`route_vty_out_flowspec`）—— 单条条目输出（含本机 PBR 安装情况）。

**第六站：BFD（约 40 分钟）**
23. `bgp_bfd.h:46`（`PEER_IS_MULTIHOP`）—— 单跳/多跳判据。
24. `bgp_bfd.c:100`（`bfd_session_status_update`）—— Down/Up 处理。
25. `bgp_bfd.c:405` / `:165` / `:539` —— 建会话 / 应用 / 拆除。
26. `lib/bfd.c:628`（`_bfd_sess_send`）与 `lib/bfd.c:1293`（`bfd_protocol_integration_init`）—— 与 bfdd 的消息边界。

---

## §12 与官方 doc/developer/*.rst 的对应

| 本卷主题 | 官方文档落点（在 `doc/developer/`） | 说明 |
|---|---|---|
| route-map 框架与插件 | `routemap.rst` | 讲 `route_map_rule_cmd`、install、依赖通知 |
| 北向 / YANG 配置 | `northbound.rst`（或 `northbound/` 目录） | NB 回调动作、`nb_cli_enqueue_change` / `apply_changes` |
| flowspec | `flowspec.rst`（若存在）/ `bgp.rst` | RFC 8955/8956 分量与动作 |
| BFD | `bfd.rst`（若存在）/ `topotests.h` | libbfd 客户端集成、`ZEBRA_BFD_DEST_*` |
| zebra 接口（PBR 消息） | `zebra.rst` | ZAPI 消息集，含 IPSET/IPTABLE/RULE |

> 说明：本仓库中 `doc/developer/` 的具体文件名与章节号未在本卷逐条实测，上表仅给「主题 → 文档名」的对应方向，阅读时应以仓库实际的 `.rst` 文件为准（避免给出未经核实的行号）。

# FRR bgpd 深入剖析 · 卷3 —— 属性仲裁、选路、通告与收敛

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 本卷覆盖文件 23 个、共 19059 行（工作树行数，含中文注释）。
>
> 写作目的：
> 1. **属性编解码引擎**：`bgp_attr.c`（约 6500 行）如何把一条 UPDATE 的属性 TLV 解析进 `struct attr`，
>    出向又如何编码回去？属性去重/复用（`bgp_attr_intern` + 哈希表）是怎么做的？
> 2. **属性解析的错误处理**：RFC 4271 / RFC 7606 的「会话重置 vs 全部丢弃（treat-as-withdraw）vs
>    属性丢弃（attribute discard）」三种策略在代码里如何落地？
> 3. **选路决策**：RFC 4271 §9.1.2 与 FRR 扩展的每一步各在哪个函数？`bgp_best_selection`（卷2 文件）
>    与 `bgp_mpath.c` 如何分工？
> 4. **多路径（ECMP）与 `MULTIPATH_NUM`**：怎么从「一条最优」变成「N 条等价」？
> 5. **ADD-PATH（RFC 7911）** 的代码落点：`bgp_addpath.c` 的策略表与三个层次的数据结构。
> 6. **下一跳跟踪**：`bgp_nexthop.c` 与 `bgp_nht.c` 的分工？「下一跳不可达 → 路径不可用」这条链路怎么走？
> 7. **路由抖动抑制** `bgp_damp.c`（RFC 2439）的惩罚值算法实现。
> 8. **条件通告** `bgp_conditional_adv.c` 解决什么场景？
> 9. `bgp_memory.c` / `bgp_errors.c` / `bgp_trace.c` 这三个「小基础设施」各自提供什么？
>
> 本卷是 `FRR_BGPD_STUDY.md`（总览篇）之下的分册。总览篇给出 bgpd 的整体架构、三线程模型、
> 核心数据结构与 131 文件全景地图；本卷只深入「属性仲裁、选路、通告与收敛」这一层，其余层次请在
> 对应分册中查阅。本文不修改任何源码。

---

## 本卷文件覆盖清单（自检用）

| # | 文件 | 行数 | 一句话职责 | 本卷小节 |
|---|------|------|-----------|---------|
| 1 | `bgp_attr.c` | 6525 | 属性解析/编码/共享中枢（6500 行巨型模块） | §2 §3 §4 §5 |
| 2 | `bgp_attr.h` | 851 | 属性数据模型 `struct attr` + 解析返回码 + getter/setter | §1 |
| 3 | `bgp_mpath.c` | 863 | 多路径（ECMP/WECMP）选择与聚合属性 | §6 |
| 4 | `bgp_mpath.h` | 172 | 多路径数据结构与 WECMP 行为枚举 | §6 |
| 5 | `bgp_addpath.c` | 615 | Add-Path 发送侧 TX ID 分配与策略 | §7 |
| 6 | `bgp_addpath.h` | 147 | Add-Path 对外接口 + 线上能力格式 | §7 |
| 7 | `bgp_addpath_types.h` | 106 | Add-Path 三层数据结构（bgp/node/info） | §7 |
| 8 | `bgp_advertise.c` | 369 | 出向/入向账本增删与属性聚合（adj_out/adj_in） | §11 |
| 9 | `bgp_advertise.h` | 294 | 通告账本数据模型（adj_out/adj_in/advertise_attr） | §11 |
| 10 | `bgp_nexthop.c` | 1605 | 下一跳缓存（bnc）数据结构层：增删查、地址/直连/自环 | §8 |
| 11 | `bgp_nexthop.h` | 261 | bnc 结构 + flags/change_flags + 自环/多邻居判据 | §8 |
| 12 | `bgp_nht.c` | 2086 | NHT 逻辑层：登记、zebra 回调、扇出重算 | §8 |
| 13 | `bgp_nht.h` | 172 | NHT 对外接口 + `bgp_nht_path_update` hook | §8 |
| 14 | `bgp_damp.c` | 1091 | 抖动抑制实现（惩罚值、复用链表、定时器） | §9 |
| 15 | `bgp_damp.h` | 235 | 抖动抑制数据结构 + 默认参数 | §9 |
| 16 | `bgp_conditional_adv.c` | 596 | 条件通告：轮询判定与发/撤路由 | §10 |
| 17 | `bgp_conditional_adv.h` | 108 | 条件通告配置形态与轮询周期 | §10 |
| 18 | `bgp_memory.c` | 195 | bgpd 内存类型（MTYPE）定义与注册 | §12.1 |
| 19 | `bgp_memory.h` | 216 | bgpd 全体内存类型集中声明 | §12.1 |
| 20 | `bgp_errors.c` | 624 | bgpd 结构化错误码消息注册表 | §12.2 |
| 21 | `bgp_errors.h` | 147 | bgpd 错误码枚举（从 `BGP_FERR_START` 起） | §12.2 |
| 22 | `bgp_trace.c` | 28 | 追踪点（tracepoint）定义编译单元 | §12.3 |
| 23 | `bgp_trace.h` | 1753 | LTTng 追踪点事件定义头（104 个 `TRACEPOINT_EVENT`） | §12.3 |

> **覆盖率自检**：上表与本卷分配到的 23 个文件逐一对齐，一个不漏、一个不多。合计 19059 行。

---

## §0 本卷总览

### 0.1 先给结论

> **bgpd 的「仲裁」主干可浓缩成一条流水线：zebra/邻居送来字节 → `bgp_attr.c` 解码成 `struct attr` 并去重 → NHT 判定下一跳可达性 → `bgp_route.c` 选路 + `bgp_mpath.c` 挑等价 → `bgp_addpath.c` 编号 → `bgp_advertise.c` 记账 → 更新组编码下发。本卷的 23 个文件正是这条流水线上「除选路主函数之外」的全部零件。**

拆成几条可操作的判断：

| 判断 | 一句话 | 证据 |
|---|---|---|
| **① 属性层是「解码 + 去重」两件事** | 解析（`bgp_attr_parse`）与共享（`bgp_attr_intern`）是两条独立的主线，后者靠 `attrhash` 全局哈希表把内容相同的 `attr` 折叠成一份，靠 `refcnt` 管生命周期 | [bgp_attr.c:4561](bgpd/bgp_attr.c#L4561)、[bgp_attr.c:1437](bgpd/bgp_attr.c#L1437)、[bgp_attr.c:1235](bgpd/bgp_attr.c#L1235) |
| **② 失败有「三档」而非「一档」** | 返回码 `enum bgp_attr_parse_ret` 有 6 个取值，把 RFC 4271 的「仅会话重置」扩展成 RFC 7606 的 treat-as-withdraw / attribute-discard；统一决策点在 `bgp_attr_malformed` | [bgp_attr.h:432](bgpd/bgp_attr.h#L432)、[bgp_attr.c:1943](bgpd/bgp_attr.c#L1943) |
| **③ 选路在卷2，本卷给的是「补集」** | `bgp_best_selection`（[bgp_route.c:3592](bgpd/bgp_route.c#L3592)）选出 1 条最优 + 候选集；`bgp_mpath.c` 把候选集变成 N 条等价；`bgp_addpath.c` 给这 N 条编号 | [bgp_route.c:4048](bgpd/bgp_route.c#L4048) 连调三者 |
| **④ 多路径的「宿主是最优路径」** | `struct bgp_path_info_mpath` 挂在 best path 的 `bgp_dest` 上（惰性分配），`mp_count`/`mp_attr`/`cum_bw` 都是 best path 的属性 | [bgp_mpath.h:74](bgpd/bgp_mpath.h#L74)、[bgp_mpath.c:556](bgpd/bgp_mpath.c#L556) |
| **⑤ NHT 是「扇出」结构** | 一个 `bnc`（下一跳缓存）挂一串路径；zebra 更新一次 → `evaluate_paths` 把 `BGP_PATH_VALID` 的翻转扇出给所有引用者并触发重算 | [bgp_nht.c:1650](bgpd/bgp_nht.c#L1650)、[bgp_nht.c:1936](bgpd/bgp_nht.c#L1936) |

### 0.2 文件职责分层表

按「上层逻辑 / 数据结构 / 基础设施」三层看本卷 23 个文件：

| 层 | 文件 | 角色 |
|---|---|---|
| **属性层** | `bgp_attr.c` / `bgp_attr.h` | UPDATE 属性 TLV ⇄ `struct attr` 双向编解码；全局去重 |
| **选路补充层** | `bgp_mpath.c` / `bgp_mpath.h` | 在 best path 之后挑等价多路径；聚合通告属性 |
| | `bgp_addpath.c` / `bgp_addpath.h` / `bgp_addpath_types.h` | 给 N 条路径分配 TX ID |
| **通告账本层** | `bgp_advertise.c` / `bgp_advertise.h` | adj_out（我发过什么）/ adj_in（我收到什么） |
| **可达性层** | `bgp_nexthop.c` / `bgp_nexthop.h` | 「结构」：bnc 增删查、地址表、直连表、自环判断 |
| | `bgp_nht.c` / `bgp_nht.h` | 「流程」：登记 zebra、回调、扇出重算 |
| **收敛层** | `bgp_damp.c` / `bgp_damp.h` | 抖动抑制：惩罚值 + 复用链表 + 定时器 |
| | `bgp_conditional_adv.c` / `bgp_conditional_adv.h` | 条件通告：轮询「整表有无 B 路由」决定「发不发 A 路由」 |
| **基础设施层** | `bgp_memory.c` / `bgp_memory.h` | 内存标签（MTYPE）声明与定义 |
| | `bgp_errors.c` / `bgp_errors.h` | 结构化错误码消息注册表 |
| | `bgp_trace.c` / `bgp_trace.h` | LTTng 静态追踪点 |

### 0.3 全景图：一条路由从「收到」到「可用」再到「发出」

```
  邻居 UPDATE 报文
        │  (bgp_packet.c: bgp_update_receive)
        ▼
 ┌──────────────────────────────────────────────────────────┐
 │ §2/§3  bgp_attr.c : bgp_attr_parse()   逐属性 TLV 解码      │
 │   while (未读完): flag/type/length → 分派 bgp_attr_*()      │
 │   出错 → bgp_attr_malformed()                              │
 │        ├ PROCEED     忽略该属性，继续                        │
 │        ├ WITHDRAW    treat-as-withdraw（丢整条 UPDATE）      │
 │        └ ERROR       发 NOTIFY，会话重置/AFI 禁用            │
 └───────────────┬──────────────────────────────────────────┘
                 ▼ §4 bgp_attr_intern()
 ┌──────────────────────────────────────────────────────────┐
 │ attrhash 全局哈希表（attrhash_key_make + attrhash_cmp）     │
 │   命中 → 复用旧 attr，refcnt++                              │
 │   未命中 → bgp_attr_hash_alloc 新建，refcnt=1               │
 │   ⇒ 被 intern 的 attr 只读；要改先复制                      │
 └───────────────┬──────────────────────────────────────────┘
                 ▼ §8 bgp_nht.c : bgp_find_or_add_nexthop()
 ┌──────────────────────────────────────────────────────────┐
 │ 登记下一跳到 zebra (zclient_send_rnh)                      │
 │  zclient ← ZEBRA_NEXTHOP_UPDATE → bgp_nexthop_update()     │
 │    → bgp_process_nexthop_update()：改 bnc.flags/metric      │
 │    → evaluate_paths(bnc)：扇出到 bnc->paths                │
 │         path 有效/无效翻转 → 置/清 BGP_PATH_VALID          │
 │         → bgp_process() 触发重算                            │
 └───────────────┬──────────────────────────────────────────┘
                 ▼ 卷2 bgp_route.c（本卷仅交叉引用）
 ┌──────────────────────────────────────────────────────────┐
 │ bgp_best_selection() → bgp_path_info_cmp() 选 1 条最优     │
 │   ↓ (bgp_route.c:4048)                                     │
 │ §6 bgp_path_info_mpath_update()      挑等价 → BGP_PATH_MULTIPATH
 │ §6 bgp_path_info_mpath_aggregate_update()  聚合通告属性      │
 │ §7 bgp_addpath_update_ids()          给路径分配 TX ID       │
 └───────────────┬──────────────────────────────────────────┘
                 ▼ §11 bgp_advertise.c
 ┌──────────────────────────────────────────────────────────┐
 │ bgp_adj_out_set()  → bgp_advertise_attr_intern()           │
 │                    → 挂到 subgroup FIFO                     │
 │ bgp_adj_in_set()   → 记录「peer 通告过该前缀」（soft-recfg） │
 └───────────────┬──────────────────────────────────────────┘
                 ▼ 卷1 更新组（bgp_updgrp*）编码 + 发送（详见卷1）
```

旁路收敛机制：抖动抑制（§9 `bgp_damp.c`）在路由反复 up/down 时加分抑制；
条件通告（§10 `bgp_conditional_adv.c`）按 60s 轮询「整表有无条件路由」决定发/撤。

### 0.4 与相邻卷的边界

| 本卷负责 | 去别的卷看 |
|---|---|
| 属性 TLV ⇄ `struct attr` 的编解码、错误处理、去重 | UPDATE 报文的读写线程与收包入口（`bgp_packet.c` 的 `bgp_update_receive` / `bgp_update_packet`）→ 卷1 |
| 多路径的挑选与聚合属性 | 选路主函数 `bgp_best_selection` / `bgp_path_info_cmp` 的逐条比较规则 → 卷2 |
| Add-Path TX ID 分配与策略 | Add-Path 能力协商（OPEN 报文）与**接收侧** ID 处理 → 卷1/卷2 |
| 出向账本 `bgp_adj_out` / `bgp_advertise` 的数据结构 | update-group / update-subgroup 的分组、打包、`bpacket` 编码与发送 → 卷1 |
| NHT 登记与扇出 | zebra 侧下一跳解析（zebra 进程）、`bgp_zebra.c` 的消息转发 → 卷4（zebra 集成） |
| 抖动抑制、条件通告 | VTY 配置命令与 `bgp_vty.c` 的解析入口 → 卷5（CLI/配置） |
| 内存标签、错误码、追踪点机制 | `lib/memory.[ch]`、`lib/ferr.h`、`lib/trace.h` 的框架实现 → lib 分册 |

---

## §1 属性数据模型：`struct attr` 与 flag 位图（`bgp_attr.h`）

### 1.1 一句话定位

`struct attr`（[bgp_attr.h:183](bgpd/bgp_attr.h#L183)）是「**一条路径的全部属性**」的聚合结构，
被 `bgp_path_info->attr` 引用；`bgp_attr.c` 的一切解析/编码都围绕它展开。理解本卷，先要接受两个概念：

1. **`attr->flag` 是权威**：一个属性「在不在」不看指针是否非空，而看 `flag` 里对应的位。
2. **被 intern 的 `attr` 是只读的**：共享表里的对象一旦被多处引用就不能就地改。

### 1.2 `flag` 位图：`ATTR_FLAG_BIT(id)`

属性类型码是 1..64 的小整数（`BGP_ATTR_ORIGIN=1`、`BGP_ATTR_AS_PATH=2`……见
[bgpd.h:2484](bgpd/bgpd.h#L2484) 起）。`attr->flag` 是 `uint64_t`，把类型号映射到位：

```c
/* bgp_attr.h:412 */
#define ATTR_FLAG_BIT(X) ...
```

该宏带一个**编译期断言**：用 `__builtin_choose_expr` 保证 `X` 落在 1..64，越界会让表达式退化成
`(void)0` 从而编译报错（[bgp_attr.h:412](bgpd/bgp_attr.h#L412) 附近的注释说明）。

配套的「属性是否存在」判据只有一个：

```c
/* bgp_attr.h:415 */
#define bgp_attr_exists(attr, id) CHECK_FLAG((attr)->flag, ATTR_FLAG_BIT(id))
```

> **陷阱**：源码注释反复强调——**判断属性存在必须用 `bgp_attr_exists()`，不要只看指针非空**。
> 例如 `attr->ecommunity` 为 NULL 不代表「没有扩展团体」，可能是「这个属性本就没出现」。两者语义不同。

### 1.3 为什么要有 getter/setter 对

`struct attr` 里很多字段是「指针 + flag 必须同步」的组合。为避免直接赋值导致 flag 与内容不一致，
头文件提供了一组 `static inline` 存取器，**setter 同时维护指针与 flag**：

| 属性 | getter | setter |
|---|---|---|
| 扩展团体 | [bgp_attr.h:626](bgpd/bgp_attr.h#L626) | [bgp_attr.h:634](bgpd/bgp_attr.h#L634) |
| 大团体 | [bgp_attr.h:646](bgpd/bgp_attr.h#L646) | [bgp_attr.h:653](bgpd/bgp_attr.h#L653) |
| 团体 | [bgp_attr.h:666](bgpd/bgp_attr.h#L666) | [bgp_attr.h:672](bgpd/bgp_attr.h#L672) |
| IPv6 扩展团体 | [bgp_attr.h:684](bgpd/bgp_attr.h#L684) | [bgp_attr.h:692](bgpd/bgp_attr.h#L692) |
| 未知传递属性 | [bgp_attr.h:706](bgpd/bgp_attr.h#L706) | [bgp_attr.h:712](bgpd/bgp_attr.h#L712) |
| 下一跳特征 NHC | [bgp_attr.h:719](bgpd/bgp_attr.h#L719) | [bgp_attr.h:725](bgpd/bgp_attr.h#L725) |
| AIGP 度量 | [bgp_attr.h:743](bgpd/bgp_attr.h#L743) | [bgp_attr.h:749](bgpd/bgp_attr.h#L749) |
| 路由反射 Cluster | [bgp_attr.h:776](bgpd/bgp_attr.h#L776) | [bgp_attr.h:782](bgpd/bgp_attr.h#L782) |

> **开发定位**：要给 `struct attr` 增加一个新属性，光加字段不够，必须同步改三处——
> ① `attr` 结构；② 一组 getter/setter（若是指针+flag）；③ **`attrhash_key_make` 与 `attrhash_cmp`**
> （见 §4.3），否则新属性不同但其余相同的两条 attr 会被错误地去重成同一份。这是最隐蔽的坑。

### 1.4 `struct attr` 字段分组

`struct attr`（[bgp_attr.h:183](bgpd/bgp_attr.h#L183)）字段极多，按来源可粗分五组：

| 组 | 代表字段 | 说明 |
|---|---|---|
| **核心标量** | `origin`、`nexthop`（IPv4）、`med`、`local_pref`、`weight`（本机私有不入线） | RFC 4271 的基础属性 |
| **指针子结构** | `aspath`、`community`、`ecommunity`、`lcommunity`、`cluster1`、`transit` | **interned**，只读 |
| **MP-BGP 下一跳** | `mp_nexthop_global`、`mp_nexthop_local`、`mp_nexthop_len`、`mp_nexthop_global_in` | 多协议场景（IPv6/VPN/EVPN） |
| **EVPN/ES** | `esi`、`es_flags`、`rmac`、`mm_seqnum`、`evpn_overlay`、`df_alg`/`df_pref` | EVPN 专用（本卷仅提示，详见 EVPN 卷） |
| **缓存字段** | `attr_intern_reuse`（[bgp_attr.h:246](bgpd/bgp_attr.h#L246) 起） | §4.4 的「单次 UPDATE 内去重缓存」 |

`nh_flags` 的三个位（[bgp_attr.h:203](bgpd/bgp_attr.h#L203) 起）：
`BGP_ATTR_NH_VALID`、`BGP_ATTR_NH_IF_OPERSTATE`、`BGP_ATTR_NH_MP_PREFER_GLOBAL`。

`rmap_change_flags` 记录「route-map 改过哪些属性」（[bgp_attr.h:266](bgpd/bgp_attr.h#L266) 字段、
[bgp_attr.h:381](bgpd/bgp_attr.h#L381) 起的 `BATTR_RMAP_*` 位），供出向判断是否需要重算。

### 1.5 解析返回码：`enum bgp_attr_parse_ret`

这是本卷最需要记住的枚举（[bgp_attr.h:432](bgpd/bgp_attr.h#L432)）：

| 值 | 名称 | 语义 | 位置 |
|---|---|---|---|
| 0 | `BGP_ATTR_PARSE_PROCEED` | 正常，继续 | [bgp_attr.h:433](bgpd/bgp_attr.h#L433) |
| -1 | `BGP_ATTR_PARSE_ERROR` | 发 NOTIFY，由上层重置会话/禁用 AFI-SAFI | [bgp_attr.h:434](bgpd/bgp_attr.h#L434) |
| -2 | `BGP_ATTR_PARSE_WITHDRAW` | treat-as-withdraw（把整条 UPDATE 当作撤销），不断会话 | [bgp_attr.h:435](bgpd/bgp_attr.h#L435) |
| -3 | `BGP_ATTR_PARSE_WITHDRAW_IGNORE` | 用户配置的 treat-as-withdraw（被动） | [bgp_attr.h:440](bgpd/bgp_attr.h#L440) |
| -4 | `BGP_ATTR_PARSE_ERROR_NOTIFYPLS` | 内部用：发 NOTIFY 后转 `ERROR` | [bgp_attr.h:444](bgpd/bgp_attr.h#L444) |
| -5 | `BGP_ATTR_PARSE_MISSING_MANDATORY` | 缺必备 well-known 属性 | [bgp_attr.h:445](bgpd/bgp_attr.h#L445) |

> 注意 -2 与 -3 的差别：`WITHDRAW` 是**协议自动**触发的 treat-as-withdraw（RFC 7606 的要求），
> `WITHDRAW_IGNORE` 是**运维配置**的（`neighbor ... path-attribute treat-as-withdraw`），
> 二者在 `bgp_attr_parse` 尾部对流的处理相同，但计数与日志不同（见 §3.4）。

### 1.6 解析参数载体：`struct bgp_attr_parser_args`

每个单属性解析函数都收同一个结构（[bgp_attr.h:506](bgpd/bgp_attr.h#L506)），它把「本次属性」的
一切上下文打包：

| 字段 | 语义 |
|---|---|
| `connection` | 连接对象（`peer->connection`） |
| `length` | 属性**数据**长度（不含头） |
| `total` | 属性**总**长度（含头） |
| `attr` | 输出：正在填充的 `struct attr *` |
| `type` / `flags` | 属性类型码与 4 位标志 |
| `startp` | 属性在输入流中的起始指针（回填 PARTIAL 位、随 NOTIFY 发数据用） |

它由主循环在 [bgp_attr.c:4725](bgpd/bgp_attr.c#L4725) 构造，一路传给解析函数与错误处理。

---

## §2 属性解析引擎：`bgp_attr_parse` 主循环（`bgp_attr.c`）

### 2.1 一句话定位

`bgp_attr_parse`（[bgp_attr.c:4561](bgpd/bgp_attr.c#L4561)）是**接收侧总入口**，由
`bgp_packet.c` 的 `bgp_update_receive()` 调用。它把一个「属性区字节流」逐属性解析进
调用方给的 `struct attr *attr`，并把 MP_REACH/MP_UNREACH 里的 NLRI 通过出参 `mp_update` /
`mp_withdraw` 回传。

### 2.2 主循环骨架

```
bgp_attr_parse(peer, attr, size, mp_update, mp_withdraw)
  ① endp = 输入指针 + size;         // 属性区结束位置
  ② while (输入指针 < endp):        // ← 逐属性循环
        a. 剩余长度 < BGP_ATTR_MIN_LEN(3)?  → 报错并按方向定 ERROR/WITHDRAW
        b. flag = 0xF0 & stream_getc();  type = stream_getc();
        c. EXTLEN 位?  length = 2 字节 : 1 字节
        d. attr_endp = 输入指针 + length;  越界检查
        e. seen 位图重复检测（MP_REACH/MP_UNREACH 重复 → 错误）
        f. SET_BITMAP(seen, type)
        g. bgp_attr_flag_invalid() 校验 O/T/P 位
        h. switch(type) → 分派 bgp_attr_xxx()
        i. 按返回码统一处理（NOTIFY / 继续 / withdraw）
        j. 读取长度与 attr_endp 一致性检查
  ③ 循环后：必备属性校验 bgp_attr_check()、NEXT_HOP 语法校验、
       AS4 合成 bgp_attr_munge_as4_attrs()、AS_PATH 语义检查 bgp_attr_aspath_check()
  ④ 返回 enum bgp_attr_parse_ret
```

关键常量：`BGP_ATTR_MIN_LEN 3`（[bgp_attr.h:67](bgpd/bgp_attr.h#L67)），即「1 字节 flag + 1 字节
type + 至少 1 字节 length」。

### 2.3 属性头解析的两个「先验检查」

**检查一：剩余长度是否够放一个最小属性头**（[bgp_attr.c:4607](bgpd/bgp_attr.c#L4607) 附近）。
若 `(endp - startp) < BGP_ATTR_MIN_LEN`，先 `flog_warn(EC_BGP_ATTRIBUTE_TOO_SMALL)`，
再**按邻居类型分叉**：

```c
if (peer->sort != BGP_PEER_EBGP) {           /* iBGP：严格 */
        bgp_notify_send(..., BGP_NOTIFY_UPDATE_ATTR_LENG_ERR);
        ret = BGP_ATTR_PARSE_ERROR;
} else {                                     /* eBGP：宽松 */
        ret = BGP_ATTR_PARSE_WITHDRAW;
}
```

这段是 RFC 7606 的核心取舍之一：**对 eBGP 邻居采用 treat-as-withdraw，对 iBGP 采用会话重置**。
理由见 §3。

**检查二：EXTLEN 位与 2 字节长度的一致性**（[bgp_attr.c:4636](bgpd/bgp_attr.c#L4636) 附近）。
若置了 `BGP_ATTR_FLAG_EXTLEN` 但剩余不足 4 字节，同样报 `EC_BGP_EXT_ATTRIBUTE_TOO_SMALL`。

**检查三：属性长度溢出**（[bgp_attr.c:4661](bgpd/bgp_attr.c#L4661) 附近）。
`attr_endp > endp` 时记 `EC_BGP_ATTRIBUTE_TOO_LARGE` 并记 `EC_BGP_ATTRIBUTE_PARSE_WITHDRAW`，
**强制 WITHDRAW**，并把读指针前移到 `endp`（这样 NLRI 定位仍靠 Total Attribute Length）。这正是
RFC 7606 §4 说的「属性长度与总长冲突时必须 treat-as-withdraw」。

### 2.4 重复属性检测（RFC 7606 §3g）

用局部位图 `seen[BGP_ATTR_BITMAP_SIZE]` 记录已出现的类型。重复时（[bgp_attr.c:4691](bgpd/bgp_attr.c#L4691) 附近）：

```c
if (CHECK_BITMAP(seen, type)) {
        if (peer->sort != BGP_PEER_EBGP ||
            type == BGP_ATTR_MP_REACH_NLRI || type == BGP_ATTR_MP_UNREACH_NLRI) {
                bgp_notify_send(..., BGP_NOTIFY_UPDATE_MAL_ATTR);   /* 重置 */
                ret = BGP_ATTR_PARSE_ERROR;  goto done;
        } else {
                /* eBGP 且非 MP 属性：丢弃「后出现的那一个」 */
                stream_set_getp(...); continue;
        }
}
```

规则：**MP_REACH/MP_UNREACH 重复 = Malformed Attribute List（致命）；其它属性重复则静默丢弃后者**
（仅 eBGP 宽松）。注意丢弃时把读指针直接跳到 `attr_endp`，跳过整段属性数据。

### 2.5 标志位校验 `bgp_attr_flag_invalid`

每个属性「按 RFC 应有」的 O/T/P 标志集中在 `attr_flags_values[]`（[bgp_attr.c:2096](bgpd/bgp_attr.c#L2096)），
例如 `[BGP_ATTR_ORIGIN] = BGP_ATTR_FLAG_TRANS`、`[BGP_ATTR_MULTI_EXIT_DISC] = BGP_ATTR_FLAG_OPTIONAL`。
`bgp_attr_flag_invalid`（[bgp_attr.c:2130](bgpd/bgp_attr.c#L2130)）比较实际标志与应有标志，
不一致即返回 true，由主循环调 `bgp_attr_malformed` 发 `Attribute Flags Error`。

诊断函数 `bgp_attr_flags_diagnose`（[bgp_attr.c:2061](bgpd/bgp_attr.c#L2061)）把 O/T/P 逐位比对并打印
差异（只诊断不修改），供日志排查。

> **注意掩码**：比较前会 `UNSET_FLAG(desired_flags, BGP_ATTR_FLAG_EXTLEN)`（EXTLEN 不参与），
> 对「可选+传递」属性还会忽略 PARTIAL（因为中转设备可能置过 PARTIAL）。

### 2.6 分派表

主循环的 `switch (type)`（[bgp_attr.c:4756](bgpd/bgp_attr.c#L4756)）把类型码映射到解析函数：

| 类型码 | 值 | 解析函数 | 位置 |
|---|---|---|---|
| `BGP_ATTR_ORIGIN` | 1 | `bgp_attr_origin` | [bgp_attr.c:2208](bgpd/bgp_attr.c#L2208) |
| `BGP_ATTR_AS_PATH` | 2 | `bgp_attr_aspath` | [bgp_attr.c:2252](bgpd/bgp_attr.c#L2252) |
| `BGP_ATTR_NEXT_HOP` | 3 | `bgp_attr_nexthop` | [bgp_attr.c:2450](bgpd/bgp_attr.c#L2450) |
| `BGP_ATTR_MULTI_EXIT_DISC` | 4 | `bgp_attr_med` | [bgp_attr.c:2474](bgpd/bgp_attr.c#L2474) |
| `BGP_ATTR_LOCAL_PREF` | 5 | `bgp_attr_local_pref` | [bgp_attr.c:2499](bgpd/bgp_attr.c#L2499) |
| `BGP_ATTR_ATOMIC_AGGREGATE` | 6 | `bgp_attr_atomic` | [bgp_attr.c:2543](bgpd/bgp_attr.c#L2543) |
| `BGP_ATTR_AGGREGATOR` | 7 | `bgp_attr_aggregator` | [bgp_attr.c:2576](bgpd/bgp_attr.c#L2576) |
| `BGP_ATTR_COMMUNITIES` | 8 | `bgp_attr_community` | [bgp_attr.c:2804](bgpd/bgp_attr.c#L2804) |
| `BGP_ATTR_ORIGINATOR_ID` | 9 | `bgp_attr_originator_id` | [bgp_attr.c:2845](bgpd/bgp_attr.c#L2845) |
| `BGP_ATTR_CLUSTER_LIST` | 10 | `bgp_attr_cluster_list` | [bgp_attr.c:2894](bgpd/bgp_attr.c#L2894) |
| `BGP_ATTR_MP_REACH_NLRI` | 14 | `bgp_mp_reach_parse` | [bgp_attr.c:2958](bgpd/bgp_attr.c#L2958) |
| `BGP_ATTR_MP_UNREACH_NLRI` | 15 | `bgp_mp_unreach_parse` | [bgp_attr.c:3208](bgpd/bgp_attr.c#L3208) |
| `BGP_ATTR_EXT_COMMUNITIES` | 16 | `bgp_attr_ext_communities` | [bgp_attr.c:3308](bgpd/bgp_attr.c#L3308) |
| `BGP_ATTR_AS4_PATH` | 17 | `bgp_attr_as4_path` | [bgp_attr.c:2372](bgpd/bgp_attr.c#L2372) |
| `BGP_ATTR_AS4_AGGREGATOR` | 18 | `bgp_attr_as4_aggregator` | [bgp_attr.c:2639](bgpd/bgp_attr.c#L2639) |
| `BGP_ATTR_PMSI_TUNNEL` | 22 | `bgp_attr_pmsi_tunnel` | [bgp_attr.c:4059](bgpd/bgp_attr.c#L4059) |
| `BGP_ATTR_ENCAP` | 23 | `bgp_attr_encap` | [bgp_attr.c:3427](bgpd/bgp_attr.c#L3427) |
| `BGP_ATTR_IPV6_EXT_COMMUNITIES` | 25 | `bgp_attr_ipv6_ext_communities` | [bgp_attr.c:3383](bgpd/bgp_attr.c#L3383) |
| `BGP_ATTR_AIGP` | 26 | `bgp_attr_aigp` | [bgp_attr.c:4118](bgpd/bgp_attr.c#L4118) |
| `BGP_ATTR_LARGE_COMMUNITIES` | 32 | `bgp_attr_large_community` | [bgp_attr.c:3267](bgpd/bgp_attr.c#L3267) |
| `BGP_ATTR_OTC` | 35 | `bgp_attr_otc` | [bgp_attr.c:4384](bgpd/bgp_attr.c#L4384) |
| `BGP_ATTR_NHC` | 39 | `bgp_attr_nhc` | [bgp_attr.c:4166](bgpd/bgp_attr.c#L4166) |
| `BGP_ATTR_PREFIX_SID` | 40 | `bgp_attr_prefix_sid` | [bgp_attr.c:3988](bgpd/bgp_attr.c#L3988) |
| 其它 | | `bgp_attr_unknown` | [bgp_attr.c:4426](bgpd/bgp_attr.c#L4426) |

类型码定义在 [bgpd.h:2484](bgpd/bgpd.h#L2484) 起。每个解析函数内**出错时普遍调
`bgp_attr_malformed`**（§3），成功则把值写进 `args->attr` 并用 setter 置 flag。

### 2.7 循环结束后的四道收尾检查

主循环退出后（[bgp_attr.c:4878](bgpd/bgp_attr.c#L4878) 起）依次做：

1. **Prefix-SID 适用性**：非 Labeled-Unicast 时把 `attr->label_index` 置无效。
2. **读指针与 endp 一致性**：不等则 `EC_BGP_ATTRIBUTES_MISMATCH` + `ATTR_LENG_ERR` NOTIFY。
3. **NEXT_HOP 语法校验**：有 NEXT_HOP 且无 MP_REACH 时调 `bgp_attr_nexthop_valid`
   （[bgp_attr.c:2422](bgpd/bgp_attr.c#L2422)）；语义错误（如 martian 下一跳）此处只记录。
4. **必备属性校验 `bgp_attr_check`**（[bgp_attr.c:4494](bgpd/bgp_attr.c#L4494)），见下。
5. **AS4 合成 `bgp_attr_munge_as4_attrs`**（[bgp_attr.c:2697](bgpd/bgp_attr.c#L2697)）：
   16 位 AS 邻居场景下用 AS4_PATH/AS4_AGGREGATOR 合成真正的 AS_PATH/AGGREGATOR（RFC 6793）。
6. **AS_PATH 语义检查 `bgp_attr_aspath_check`**（[bgp_attr.c:2305](bgpd/bgp_attr.c#L2305)）：
   如首 AS 是否为对端、是否含本机 AS（环路检测）。

### 2.8 必备属性校验 `bgp_attr_check`

`bgp_attr_check`（[bgp_attr.c:4494](bgpd/bgp_attr.c#L4494)）的规则：

| 条件 | 缺失时的处理 |
|---|---|
| `BGP_ATTR_ORIGIN` 必须存在 | 记 `type`，最终 `BGP_ATTR_PARSE_WITHDRAW` |
| `BGP_ATTR_AS_PATH` 必须存在 | 同上 |
| `BGP_ATTR_NEXT_HOP` 必须存在（无 MP_REACH_NLRI 时） | 同上 |
| iBGP 时 `BGP_ATTR_LOCAL_PREF` 必须存在 | 同上 |

三个特例：
- **仅含 MP_UNREACH_NLRI 的 UPDATE**（纯撤销）不要求其它属性 → 直接 PROCEED。
- **GR End-of-RIB**（`PEER_CAP_RESTART_RCV` 且有 flag 为空、length 为 0 的空 UPDATE）→
  treat-as-withdraw，**防止被当成缺必备属性的普通 UPDATE**（[bgp_attr.c:4504](bgpd/bgp_attr.c#L4504)）。
- 缺必备属性时记 `EC_BGP_MISSING_ATTRIBUTE` 并返回 `WITHDRAW`（RFC 7606 要求 treat-as-withdraw）。

---

## §3 属性解析的错误处理：三档策略的落地

### 3.1 一句话定位

RFC 4271 只给了「错就发 NOTIFY 重置会话」一条路，太脆弱（一条坏属性就断整条会话）。RFC 7606
把错误处理细化为三档，FRR 在 `enum bgp_attr_parse_ret`（§1.5）与一个**统一决策点**
`bgp_attr_malformed`（[bgp_attr.c:1943](bgpd/bgp_attr.c#L1943)）里落地。

### 3.2 三档策略对照

| 档 | 返回码 | 行为 | 何时用 |
|---|---|---|---|
| **会话重置** | `BGP_ATTR_PARSE_ERROR` | 发 NOTIFY，由上层放回/关闭会话或禁用该 AFI-SAFI | 无法可靠定位 NLRI 的错误（如 MP_REACH/MP_UNREACH 长度错）；iBGP 的严重语法错误 |
| **全部丢弃**（treat-as-withdraw） | `BGP_ATTR_PARSE_WITHDRAW` | 把**整条 UPDATE** 当作撤销处理，不断会话 | 影响选路的核心属性出错；eBGP 的长度/语法错 |
| **属性丢弃**（attribute discard） | `BGP_ATTR_PARSE_PROCEED` | **忽略出错的单个属性**，其余照常处理 | 不影响选路的边缘属性；运维显式配置的 discard |
| 用户配置的 treat-as-withdraw | `BGP_ATTR_PARSE_WITHDRAW_IGNORE` | 同「全部丢弃」，但由配置触发、单独计数 | `neighbor ... path-attribute treat-as-withdraw` |

### 3.3 `bgp_attr_malformed` 的决策树

`bgp_attr_malformed(args, subcode, length)`（[bgp_attr.c:1943](bgpd/bgp_attr.c#L1943)）逻辑：

```
① 若开 debug/trace：dump 属性并 frrtrace(upd_malformed_attr)
② if (type == MP_REACH_NLRI || type == MP_UNREACH_NLRI):
       bgp_notify_send_with_data(...); return ERROR;      ← 无法定位 NLRI，必须重置
③ 把读指针前移到本属性末尾
④ if (flag 同时含 TRANS|OPTIONAL|PARTIAL):
       return WITHDRAW        ← draft-scudder：中转引入的问题，不重置会话
⑤ switch (type):
     无关紧要的属性  (AS4_AGGREGATOR/AGGREGATOR/ATOMIC_AGGREGATE/PREFIX_SID/NHC)
                     → return PROCEED     ← 属性丢弃
     核心属性        (ORIGIN/AS_PATH/AS4_PATH/NEXT_HOP/MED/LOCAL_PREF/团体类/
                      ORIGINATOR_ID/CLUSTER_LIST/PMSI_TUNNEL/ENCAP/OTC)
                     → return WITHDRAW    ← 全部丢弃
     default: flog_err(EC_BGP_ATTR_FLAG, "...treating as withdraw"); → return WITHDRAW
```

三条判据的次序很重要：

1. **MP 属性优先**：不管 flag 如何，MP_REACH/MP_UNREACH 长度错一律 ERROR（因为下一跳在 NLRI 之前，
   错位后无法定位后续 NLRI，treat-as-withdraw 也做不到——只能重置会话或禁用 AFI-SAFI）。
2. **PARTIAL 宽松**：`TRANS + OPTIONAL + PARTIAL` 三者齐备说明属性是「中转设备引入且未识别」，
   多半不是直连邻居的错，于是只做 treat-as-withdraw（draft-scudder-idr-optional-transitive）。
3. **按属性重要性二选一**：把属性硬编码成两组——「不影响选路的」丢弃、「影响选路的」整条撤。

> **设计动因**：treat-as-withdraw 比会话重置「便宜」——一次撤销只影响一条前缀，而重置会话要重学
> 整张表。所以 RFC 7606 把 treat-as-withdraw 定为**首选**，会话重置只在「无法定位 NLRI」时保留。

### 3.4 三个「忽略」入口的辨析

| 入口 | 位置 | 触发方式 | 返回 |
|---|---|---|---|
| `bgp_attr_malformed` | [bgp_attr.c:1943](bgpd/bgp_attr.c#L1943) | 解析函数内部检测到错误时调用 | ERROR / WITHDRAW / PROCEED |
| `bgp_attr_ignore` | [bgp_attr.c:6490](bgpd/bgp_attr.c#L6490) | 被解析器的错误分支调用，按**运维配置**决定 | `WITHDRAW_IGNORE` 或 PROCEED |
| `bgp_attr_unknown` | [bgp_attr.c:4426](bgpd/bgp_attr.c#L4426) | 未知属性类型的处理分支 | 见下 |

`bgp_attr_unknown`（[bgp_attr.c:4426](bgpd/bgp_attr.c#L4426)）对未知属性的处理遵循 RFC 4271 §6.3：

- 若该类型在 `peer->discard_attrs[]` 或 `peer->withdraw_attrs[]` 里 → 交给 `bgp_attr_ignore`。
- **不是** optional（即公认但本机不识别）→ `bgp_attr_malformed(..., UNREC_ATTR, ...)`（错误）。
- optional 但非 transitive → 静默忽略（PROCEED）。
- optional + transitive → **保存进 `attr->transit` 并置 PARTIAL 位后原样转发**给其它邻居
  （[bgp_attr.c:4468](bgpd/bgp_attr.c#L4468) 附近）。

`bgp_attr_ignore`（[bgp_attr.c:6490](bgpd/bgp_attr.c#L6490)）是「运维配置」的落点：
`withdraw` → 返回 `WITHDRAW_IGNORE`；否则 `peer->stat_pfx_discard++` 后返回 PROCEED。
注意 `stat_pfx_withdraw` 的累加**不在这里**（源码注释明确：在 `bgp_update_receive()` 里做，避免重复计数）。

### 3.5 主循环对返回码的统一处理

回到 `bgp_attr_parse` 的分派后处理（[bgp_attr.c:4836](bgpd/bgp_attr.c#L4836) 附近）：

```c
if (ret == BGP_ATTR_PARSE_ERROR_NOTIFYPLS) {
        bgp_notify_send(..., MAL_ATTR); ret = BGP_ATTR_PARSE_ERROR; goto done;
}
if (ret == BGP_ATTR_PARSE_ERROR) { flog_warn(EC_BGP_ATTRIBUTE_PARSE_ERROR, ...); goto done; }
if (ret == WITHDRAW || ret == WITHDRAW_IGNORE) {
        if (ret == WITHDRAW) flog_warn(EC_BGP_ATTRIBUTE_PARSE_WITHDRAW, ...);
        stream_forward_getp(..., endp - 输入指针); goto done;   /* 跳到属性区末尾 */
}
```

以及 `done:` 标签处对 WITHDRAW 的收尾（[bgp_attr.c:4953](bgpd/bgp_attr.c#L4953) 附近）：把读指针复位到
「属性区之后」的已知位置（RFC 7606 §5.1 要求用 Total Attribute Length 定位 NLRI），而不是依赖调用方。

### 3.6 错误码 → 日志

所有 `flog_warn(EC_BGP_*)` 用的错误码在 §12.2 有专门说明。本卷涉及的典型错误码（`bgp_errors.h`）：
`EC_BGP_ATTRIBUTE_TOO_SMALL`、`EC_BGP_ATTRIBUTE_TOO_LARGE`、`EC_BGP_EXT_ATTRIBUTE_TOO_SMALL`、
`EC_BGP_ATTRIBUTE_REPEATED`、`EC_BGP_ATTRIBUTE_PARSE_ERROR`、`EC_BGP_ATTRIBUTE_PARSE_WITHDRAW`、
`EC_BGP_ATTRIBUTE_FETCH_ERROR`、`EC_BGP_ATTRIBUTES_MISMATCH`、`EC_BGP_ATTR_FLAG`、
`EC_BGP_MISSING_ATTRIBUTE`。

---

## §4 属性共享（interning）：`attrhash` 去重

### 4.1 一句话定位

一条 BGP 表里成千上万条路由，属性组合高度重复（同一邻居学来的一批前缀，AS_PATH/NEXT_HOP
往往只差一点）。若不共享，内存会爆炸。**interning** 就是「内容相同的 `attr` 全局只留一份」，
多个 `bgp_path_info` 共享同一指针，靠 `refcnt` 管生命周期。入口是
`bgp_attr_intern`（[bgp_attr.c:1437](bgpd/bgp_attr.c#L1437)）/ `bgp_attr_unintern`
（[bgp_attr.c:1823](bgpd/bgp_attr.c#L1823)）。

### 4.2 全局属性主表 `attrhash`

```c
/* bgp_attr.c:1154 */
static struct hash *attrhash;
```

在 `bgp_attr_init`（[bgp_attr.c:6116](bgpd/bgp_attr.c#L6116)）里建表：

```c
/* bgp_attr.c:1288 */
attrhash = hash_create(attrhash_key_make, attrhash_cmp, "BGP Attributes");
```

统计与遍历：`attr_count`（[bgp_attr.c:1158](bgpd/bgp_attr.c#L1158)）返回条目数，
`attr_show_all_iterator`（[bgp_attr.c:1309](bgpd/bgp_attr.c#L1309)）供 `show bgp ... summary` 用。

> **特殊之处**：`attrhash_finish`（[bgp_attr.c:1304](bgpd/bgp_attr.c#L1304)）用
> `hash_clean_and_free(&attrhash, attr_vfree)`；`attr_vfree`（[bgp_attr.c:1298](bgpd/bgp_attr.c#L1298)）
> 直接 `XFREE(MTYPE_ATTR, attr)`，**不触发各子结构的 unintern**。所以在它之前必须保证子结构引用
> 已全部释放，否则泄的是 aspath/团体等子对象，不是 attr 本身。这是退出路径的微妙点。

### 4.3 去重的两个关键函数：`key_make` 与 `cmp`

**`attrhash_key_make`**（[bgp_attr.c:1173](bgpd/bgp_attr.c#L1173)）：算哈希键。它用
`MIX`/`MIX3` 宏（`jhash_1word`/`jhash_3words`）把几乎所有字段混入，既有**定长标量**（origin/med/
local_pref…），也有**子结构的哈希**（`aspath_key_make`、`community_hash_make`、
`ecommunity_hash_make`、`lcommunity_hash_make`、`cluster_hash_key_make`、`transit_hash_key_make`、
`encap_hash_key_make`、`srv6_*_hash_key_make`、`evpn_overlay_hash_key_make`、`bgp_nhc_hash_key_make`…）。
IPv6 下一跳与 `rmac` 直接用 `jhash(..., sizeof, key)` 混入。

**`attrhash_cmp`**（[bgp_attr.c:1235](bgpd/bgp_attr.c#L1235)）：命中后的精确比较。核心思想是
**「子结构已 intern，故比指针即可」**：

```c
if (attr1->flag == attr2->flag && attr1->origin == attr2->origin &&
    attr1->nexthop.s_addr == attr2->nexthop.s_addr &&
    attr1->aspath == attr2->aspath &&                      /* 指针比较！ */
    bgp_attr_get_community(attr1) == bgp_attr_get_community(attr2) && ...
```

但仍有部分字段需要**逐字节/逐字段**比较：`encap_subtlvs`（`encap_same`）、SNET 的
`overlay_index_same`、`esi`/`rmac`/若干 EVPN 字段、`srv6_l3service_same`、`srv6_vpn_same`、
`bgp_nhc_same` 等。这一长串是一个大 `&&` 表达式（[bgp_attr.c:1240](bgpd/bgp_attr.c#L1240) 起），
**任何一处漏比较都会导致「把不同的 attr 当成相同」——两条本该不同的路由被折叠成一份**。

> **开发定位**：给 `struct attr` 加字段，**必须**同步改这两个函数（尤其是 `cmp`），
> 否则会引入「路由属性串台」这类不编译报错、极难复现的 bug。

### 4.4 `bgp_attr_intern` 的流程

```
bgp_attr_intern(attr)
  ① 对每个子结构"先 intern 子结构":
       aspath      → aspath_intern() / refcnt++
       community   → community_intern() / refcnt++
       ecommunity / ipv6_ecommunity / lcommunity / evpn_overlay / encap ...
  ② 查/建主表:
       reuse 缓存命中(见下) → find = 缓存的 interned; find->refcnt++
       否则                  → find = hash_get(attrhash, attr, bgp_attr_hash_alloc); find->refcnt++
  ③ 返回 find                     // 调用方拿到的是"表里那一份"的新引用
```

`bgp_attr_hash_alloc`（[bgp_attr.c:1403](bgpd/bgp_attr.c#L1403)）在未命中时新建一份「堆上的
attr 副本」——即把调用方传进来的 attr 拷贝进表，作为共享本体。

**关键约定：被 intern 的 attr 只读**。要改，必须先复制（route-map 作用时、或 `bgp_attr_flush`
后重设），这是 bgpd 里最重要的一条纪律。

### 4.5 单次 UPDATE 内的去重缓存 `attr_intern_reuse`

一个 UPDATE 里多条前缀共用同一段属性，FRR 用一个**缓存**避免重复查表：调用方（`bgp_route.c`
的 UPDATE 处理）在解析后把「解析得到的 attr」记为 `parsed_attr` 锚点，之后每条前缀 intern
时若与锚点完全相同（说明没被改过），直接复用上次的 interned 引用：

```c
/* bgp_attr.c:1572 起 */
if (reuse_anchor && ... && attrhash_cmp(attr, reuse_anchor->attr_intern_reuse.parsed_attr)) {
        find = reuse_anchor->attr_intern_reuse.interned;
        find->refcnt++;
} else {
        find = (struct attr *)hash_get(attrhash, attr, bgp_attr_hash_alloc);
        find->refcnt++;
        /* 仅当 attr 未被改动时填充缓存 */
        ...
}
```

这就是 `struct attr` 里 `attr_intern_reuse` 三段（`valid` / `interned` / `parsed_attr`）的用途。
配套函数 `bgp_attr_unintern_clear_reuse`（[bgp_attr.c:1809](bgpd/bgp_attr.c#L1809)）用于清缓存。

### 4.6 注销与释放

| 函数 | 位置 | 做什么 |
|---|---|---|
| `bgp_attr_unintern_sub` | [bgp_attr.c:1744](bgpd/bgp_attr.c#L1744) | 释放 attr 内部各子结构引用（aspath/团体…的 unintern） |
| `bgp_attr_unintern` | [bgp_attr.c:1823](bgpd/bgp_attr.c#L1823) | `refcnt--`；归零则 `hash_release(attrhash, attr)` 并从表里移除后释放本体 |
| `bgp_attr_flush` | [bgp_attr.c:1848](bgpd/bgp_attr.c#L1848) | 释放临时（未 intern）attr 的子结构，供解析中途丢弃用 |
| `bgp_attr_flush_encap` | [bgp_attr.c:326](bgpd/bgp_attr.c#L326) | 释放封装 TLV 链表 |

`bgp_attr_unintern` 收的是 `struct attr **`（二级指针），因为归零时要把调用方的指针置 NULL——
这是防止悬垂的惯用法。

### 4.7 与其它「同类哈希表」的关系（一张对照表）

bgpd 里「按 interned attr 去重」的模式出现了多次，别混淆：

| 表 | 位置 | 键 | 范围 | 用途 |
|---|---|---|---|---|
| `attrhash`（全局属性表） | [bgp_attr.c:1154](bgpd/bgp_attr.c#L1154) | `attrhash_key_make`/`cmp` | 全进程 | 属性总量去重 |
| `subgrp->hash`（本卷 §11） | `bgp_advertise.c` | `bgp_advertise_attr_hash_key`/`cmp` | **每个 update_subgroup 一份** | 待通告属性的批量编码复用 |
| `transit_hash` | [bgp_attr.c:1158](bgpd/bgp_attr.c#L1158) 注释处 | 未知传递属性 | 全进程 | 未知传递属性去重 |

> **易误解点**：`bgp_advertise_attr_hash_key`（[bgp_advertise.c:104](bgpd/bgp_advertise.c#L104)）与
> `_cmp`（[bgp_advertise.c:114](bgpd/bgp_advertise.c#L114)）**直接借用 `attrhash_key_make` /
> `attrhash_cmp`**（它俩的键就是 `baa->attr`）。所以「属性复用」在这两处用的是同一套等价判据，
> 只是**表的范围不同**（全局 vs 子组）。

---

## §5 属性编码：`bgp_packet_attribute` 与 MP_REACH

### 5.1 一句话定位

出向总入口是 `bgp_packet_attribute`（[bgp_attr.c:5550](bgpd/bgp_attr.c#L5550)）。它把
`struct attr` 按对端能力编码成字节流，返回值是「本属性段总字节数」（供上层回填 UPDATE 长度）。
它由 `bgp_packet.c` / 更新组打包调用（详见卷1）。

### 5.2 编码顺序与分支

函数依次编码（[bgp_attr.c:5550](bgpd/bgp_attr.c#L5550) 起）：

| 顺序 | 属性 | 备注 |
|---|---|---|
| 0 | MP_REACH_NLRI（若适用） | 先用 `bgp_packet_mpattr_start` 开段、`_prefix` 写 NLRI、`_end` 回填长度 |
| 1 | ORIGIN | 固定 1 字节 |
| 2 | AS_PATH | eBGP 时可能前置 local-as；按 `use32bit` 决定 4 字节 ASN（RFC 6793）；必要时补 AS4_PATH |
| 3 | NEXT_HOP / MP nexthop | 由 `mpattr` 段处理，或传统 IPv4 NEXT_HOP |
| 4 | MED / LOCAL_PREF | |
| 5 | ATOMIC_AGGREGATE / AGGREGATOR | |
| 6 | COMMUNITY / 大团体 / 扩展团体 | 逐个走对应 `*_attr_put`/编码子函数 |
| 7 | ORIGINATOR_ID / CLUSTER_LIST | RR 场景 |
| 8 | 其它（AIGP/OTC/NHC…） | |

`use32bit` 由对端能力决定：

```c
/* bgp_attr.c:5562 */
bool use32bit = CHECK_FLAG(peer->cap, PEER_CAP_AS4_RCV) &&
                CHECK_FLAG(peer->cap, PEER_CAP_AS4_ADV);
```

### 5.3 MP_REACH 编码三步

`bgp_packet_mpattr_start`（[bgp_attr.c:5059](bgpd/bgp_attr.c#L5059)）开段（写属性头、AFI/SAFI、
下一跳），`bgp_packet_mpattr_prefix`（[bgp_attr.c:5302](bgpd/bgp_attr.c#L5302)）写一条 NLRI，
`bgp_packet_mpattr_end`（[bgp_attr.c:5497](bgpd/bgp_attr.c#L5497)）回填属性长度。因为一条
UPDATE 可以带多个前缀，所以是「开段 → 多次写前缀 → 回填结束」。

对应的撤段（MP_UNREACH）：`bgp_packet_mpunreach_start`（[bgp_attr.c:6058](bgpd/bgp_attr.c#L6058)）、
`bgp_packet_mpunreach_prefix`（[bgp_attr.c:6082](bgpd/bgp_attr.c#L6082)）、
`bgp_packet_mpunreach_end`（[bgp_attr.c:6107](bgpd/bgp_attr.c#L6107)）。MP_UNREACH 复用
MP_REACH 的「回填长度」逻辑（[bgp_attr.c:6102](bgpd/bgp_attr.c#L6102) 注释说明）。

### 5.4 其它编码辅助

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_attr_stream_put_labeled_prefix` | [bgp_attr.c:615](bgpd/bgp_attr.c#L615) | 标签化前缀（RFC 8277）编码 |
| `bgp_attr_stream_put_prefix_addpath` | [bgp_attr.h:845](bgpd/bgp_attr.h#L845) | 带 Add-Path ID 的前缀编码 |
| `bgp_packet_mpattr_prefix_size` | [bgp_attr.c:5354](bgpd/bgp_attr.c#L5354) | 预估 NLRI 字节数（预留空间） |
| `bgp_packet_ecommunity_attribute` | [bgp_attr.c:5525](bgpd/bgp_attr.c#L5525) | 扩展团体属性编码 |
| `bgp_packet_nhc` | [bgp_attr.c:5203](bgpd/bgp_attr.c#L5203) | 下一跳特征（NHC）编码 |
| `bgp_dump_routes_attr` | [bgp_attr.c:6152](bgpd/bgp_attr.c#L6152) | 导出属性供 dump/MRT（不依赖对端能力） |

### 5.5 init / finish

`bgp_attr_init`（[bgp_attr.c:6116](bgpd/bgp_attr.c#L6116)）建 `attrhash`；
`bgp_attr_finish`（[bgp_attr.c:6132](bgpd/bgp_attr.c#L6132)）销毁它。二者在 bgpd 启动/退出路径各调一次。

---

## §6 选路决策与多路径分工：`bgp_mpath.c`

### 6.1 一句话定位

**选路主函数不本卷**。`bgp_best_selection`（[bgp_route.c:3592](bgpd/bgp_route.c#L3592)）连同逐条比较器
`bgp_path_info_cmp` 属于卷2。本卷的 `bgp_mpath.c` 负责的是选路的**下游**：在「已选出 1 条最优」
之后，把与最优等价的其它路径挑出来（ECMP），并为对外通告生成一条聚合属性。

### 6.2 两者的分工（对照表）

| 维度 | `bgp_best_selection`（卷2） | `bgp_mpath.c`（本卷） |
|---|---|---|
| 输入 | 一个 `bgp_dest` 的全部 `bgp_path_info` | 已排序的路径链 + `new_select`/`old_select` |
| 输出 | 1 条 best + 候选集标记 | `dest->mpath`（N 条等价）+ `mp_attr`（聚合属性） |
| 打标 | `BGP_PATH_MULTIPATH_NEW`（候选） | `BGP_PATH_MULTIPATH`（最终入选） |
| 判据 | RFC 4271 §9.1.2 全套 + FRR 扩展 | 只看「下一跳是否等价」(`bgp_path_info_nexthop_cmp`) |
| 上限 | 无 | `maxpaths`（受 `multipath_num` 约束） |

调用链（[bgp_route.c:4048](bgpd/bgp_route.c#L4048) 起，紧接在 best selection 之后）：

```c
bgp_path_info_mpath_update(bgp, dest, new_select, old_select, num_candidates, mpath_cfg);
bgp_path_info_mpath_aggregate_update(new_select, old_select);
bgp_addpath_update_ids(bgp, dest, afi, safi);      /* §7 */
```

### 6.3 配置：`maximum-paths` 落到哪里

配置命令 `maximum-paths [ibgp] N [equal-cluster-length]` 由 `bgp_vty.c` 解析（[bgp_vty.c:3012](bgpd/bgp_vty.c#L3012) 起），
最终写进每个 `bgp` 实例的 `bgp->maxpaths[afi][safi]`（`struct bgp_maxpaths_cfg`，
[bgpd.h:992](bgpd/bgpd.h#L992)）：

| 字段 | 语义 |
|---|---|
| `maxpaths_ebgp` | eBGP 邻居允许的最大并行路径数 |
| `maxpaths_ibgp` | iBGP 邻居允许的最大并行路径数 |
| `same_clusterlen` | RR 场景下是否把 cluster-list 长度纳入等价比较 |

set/unset 在 `bgp_maximum_paths_set`（[bgp_mpath.c:68](bgpd/bgp_mpath.c#L68)）/
`bgp_maximum_paths_unset`（[bgp_mpath.c:98](bgpd/bgp_mpath.c#L98)）。unset 会把值恢复成
**全局 `multipath_num`**（[bgp_mpath.c:106](bgpd/bgp_mpath.c#L106)、[bgp_mpath.c:110](bgpd/bgp_mpath.c#L110)）。

### 6.4 `MULTIPATH_NUM`：编译期上限

`MULTIPATH_NUM` 不是源码里的字面常量，而是**由 configure 生成的**（[configure.ac:1084](configure.ac#L1084)）：
`--enable-multipath=ARG`（[configure.ac:834](configure.ac#L834)）取 `ARG`。
**缺省值为 16**（`MPATH_NUM=16`，[configure.ac:1068](configure.ac#L1068)）；仅当显式
`--enable-multipath=0` 时才放大到 **64**（[configure.ac:1070](configure.ac#L1070)）；
指定 1~999 则取该值（[configure.ac:1075](configure.ac#L1075)）；空串保持 16；非法值报错
（[configure.ac:1079](configure.ac#L1079)）。它定义在 `config.h`，被 lib/bgpd 多处当数组维度用
（如 `lib/zclient.h` 里的 `zapi_nexthop nexthops[MULTIPATH_NUM]`）。

运行时全局变量是 `unsigned int multipath_num = MULTIPATH_NUM`（[bgpd.c:202](bgpd/bgpd.c#L202)），
可由命令行 `-e` 覆盖（[bgp_main.c:530](bgpd/bgp_main.c#L530) 的 `case 'e'`，最终写入
`multipath_num`（[bgp_main.c:542](bgpd/bgp_main.c#L542)），超过 `MULTIPATH_NUM` 或为 0 即报错）。
`bgp_path_info_mpath_update` 的**默认上限**就是它（[bgp_mpath.c:570](bgpd/bgp_mpath.c#L570)）：

```c
maxpaths = multipath_num;   /* 缺省 */
if (new_best)               /* 实际按邻居类型取实例级配置 */
        maxpaths = (new_best->peer->sort == BGP_PEER_IBGP)
                   ? mpath_cfg->maxpaths_ibgp : mpath_cfg->maxpaths_ebgp;
```

### 6.5 数据结构：宿主是最优路径

`struct bgp_path_info_mpath`（[bgp_mpath.h:74](bgpd/bgp_mpath.h#L74)）**挂在 best path 的
`bgp_dest` 上**（`dest->mpath`），惰性分配（`bgp_path_info_mpath_get`，[bgp_mpath.c:293](bgpd/bgp_mpath.c#L293)）：

| 字段 | 语义 |
|---|---|
| `mp_dest` | 反指宿主 `bgp_dest` |
| `mp_count` | 已选中的多路径条数 |
| `mp_flags` | `BGP_MP_LB_PRESENT` / `BGP_MP_LB_ALL`（链路带宽） |
| `mp_attr` | 对外通告用的**聚合**属性 |
| `cum_bw` | 所有多路径的累计带宽（WECMP 用） |

**设计取舍**：只有最优路径才需要回答「我和谁一起分担」，所以不给每条路径都挂 `mpath`——
省内存，但代价是代码里到处判 NULL。

遍历多路径的两条入口：`bgp_path_info_mpath_first`（[bgp_mpath.c:340](bgpd/bgp_mpath.c#L340)）与
`bgp_path_info_mpath_next`（[bgp_mpath.c:317](bgpd/bgp_mpath.c#L317)），它们从 best 出发沿
`BGP_PATH_MULTIPATH` 链走。

### 6.6 等价判据：`bgp_path_info_nexthop_cmp`

「N 条是否等价」的核心是下一跳比较 `bgp_path_info_nexthop_cmp`（[bgp_mpath.c:159](bgpd/bgp_mpath.c#L159)）。
它先比 IPv4 `nexthop`；相等再按 `mp_nexthop_len` 分支：

| `mp_nexthop_len` | 比较对象 |
|---|---|
| `BGP_ATTR_NHLEN_IPV4` / `_VPNV4` | `mp_nexthop_global_in`（IPv4） |
| `BGP_ATTR_NHLEN_IPV6_GLOBAL` / `_VPNV6_GLOBAL` | `mp_nexthop_global` |
| `BGP_ATTR_NHLEN_IPV6_GLOBAL_AND_LL` | 按 `BGP_ATTR_NH_MP_PREFER_GLOBAL` 选 global 或 local；两者都不 prefer-global 时还要比 `peer->ifp`（`bgp_interface_same`，[bgp_mpath.c:129](bgpd/bgp_mpath.c#L129)） |

此外还比「是否属于同一 VRF」（`vrfleak->bgp_orig->vrf_id`），黑下一跳特殊处理。
**注意这只是「下一跳等价」**；「是否够格做多路径」的其它条件（AS_PATH 长度相同、MED 相同、
来自同一邻居 AS 等）由卷2 的选路在打 `BGP_PATH_MULTIPATH_NEW` 时判定。

### 6.7 `bgp_path_info_mpath_update`：从候选到 N 条

`bgp_path_info_mpath_update`（[bgp_mpath.c:556](bgpd/bgp_mpath.c#L556)）流程：

```
① 若 old_best：记录旧 mp_count/cum_bw，清 MULTIPATH 标、释放旧 mpath
② 取 maxpaths（按邻居类型）
③ 从 new_best 起「有序并行遍历」：
     对每条 cur_iterator:
        old_mpath = 有 BGP_PATH_MULTIPATH 吗
        new_mpath = 有 BGP_PATH_MULTIPATH_NEW 吗
        清 NEW 标
        if (mpath_count >= maxpaths) 把剩余的 MULTIPATH/NEW 标全清并 break
        ...（逐条决定是否入选、累计带宽、置/清 BGP_PATH_MULTIPATH）
④ 汇总 mp_count / cum_bw / mp_flags 写回 dest->mpath
```

关键设计：**有序并行遍历**——因为候选链与旧多路径链都已按同一顺序排列，同时推进可让
「本来就是多路径、现在还是」的路径在两条链里同时出现，只需 O(N) 即可算出增量。`maxpaths`
上限一到就停，剩下的路径只清标不入选。

### 6.8 `bgp_path_info_mpath_aggregate_update`：聚合属性

`bgp_path_info_mpath_aggregate_update`（[bgp_mpath.c:736](bgpd/bgp_mpath.c#L736)）为多路径**生成一条对外通告
的聚合属性**（写入 `dest->mpath->mp_attr`），这样向邻居只需通告一条前缀而带上综合后的属性：

- `mp_count == 1` 时**不需要**聚合（只有一条），释放旧 `mp_attr` 并返回。
- 当 `BGP_FLAG_MULTIPATH_RELAX_AS_SET` 生效时，对 AS_PATH 做 `aspath_aggregate`（把不同的
  AS_SET 合并）、origin 取「最差」（数值最大）、团体/扩展团体/大团体做 `*_merge + *_uniq_sort`。

聚合结果是一个新的 `struct attr`（基于 `*new_best->attr` 拷贝后修改），随后 intern。

### 6.9 WECMP：加权多路径

加权依据由 `enum bgp_wecmp_behavior`（[bgp_mpath.h:47](bgpd/bgp_mpath.h#L47)）描述：
`NONE(0)` / `LINK_BW(1)` / `NNHN_COUNT(2)` / `USE_RECURSIVE_VALUE(3)`。
- 注意 `NONE=0`，所以「未配置」与「显式关闭」同值，判断启用只需 `!= NONE`。
- 当前依据用 `bgp_path_info_mpath_chkwtd`（[bgp_mpath.c:464](bgpd/bgp_mpath.c#L464)）算出；
- 累计带宽用 `bgp_path_info_mpath_cumbw`（[bgp_mpath.c:511](bgpd/bgp_mpath.c#L511)）；
- 带宽标志用 `bgp_path_info_mpath_lb_update`（[bgp_mpath.c:400](bgpd/bgp_mpath.c#L400)）维护；
- 聚合属性取用 `bgp_path_info_mpath_attr`（[bgp_mpath.c:443](bgpd/bgp_mpath.c#L443)）。

> **相邻模块**：链路带宽值来自扩展团体，加权 ECMP 的下一跳集合下发依赖 `bgp_nhc.c` 的
> 「下一跳个数（NNHN）」计数（属于 NHC 卷，本卷仅提示）。

### 6.10 分配/释放

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_path_info_mpath_new` | [bgp_mpath.c:252](bgpd/bgp_mpath.c#L252) | 分配一个 mpath（`mp_count=1`） |
| `bgp_path_info_mpath_free` | [bgp_mpath.c:272](bgpd/bgp_mpath.c#L272) | 释放 |
| `bgp_path_info_mpath_get` | [bgp_mpath.c:293](bgpd/bgp_mpath.c#L293) | 惰性获取（无则建） |
| `bgp_path_info_mpath_count` | [bgp_mpath.c:354](bgpd/bgp_mpath.c#L354) | 读 `mp_count` |

---

## §7 ADD-PATH（RFC 7911）：发送侧 ID 选择

### 7.1 一句话定位

标准 BGP 一条前缀只通告一条最佳路径；Add-Path 允许**同时通告多条**，每条用 4 字节 Path-ID 区分。
本卷的三个文件只负责**发送侧**（TX）：给「要发出去的每条路径」分配/回收一个 TX ID，并在路径
选择变化时复用它，避免多做一次 withdraw+update。

### 7.2 三层数据结构

`__attribute__((packed))` 的线上能力格式 `struct bgp_paths_limit_capability`（[bgp_addpath.h:55](bgpd/bgp_addpath.h#L55)）
是 `(afi, safi, paths_limit)` 三元组序列—— **packed 是必需的**，否则编译器插的填充字节会让对端解析不了。

三层运行态（`bgp_addpath_types.h`）：

| 结构 | 挂在哪 | 字段 | 位置 |
|---|---|---|---|
| `bgp_addpath_bgp_data` | `struct bgp`（每实例一份） | `peercount[afi][safi][策略]`、`total_peercount`、`id_allocators[afi][safi][策略]` | [bgp_addpath_types.h:69](bgpd/bgp_addpath_types.h#L69) |
| `bgp_addpath_node_data` | `bgp_dest`（每前缀一份） | `free_ids[策略]`（该前缀空闲 ID 池） | [bgp_addpath_types.h:81](bgpd/bgp_addpath_types.h#L81) |
| `bgp_addpath_info_data` | `bgp_path_info`（每路径一份） | `addpath_tx_id[策略]` | [bgp_addpath_types.h:89](bgpd/bgp_addpath_types.h#L89) |

> **为什么每前缀一个池**：TX ID 的语义是「该前缀的第几条路径」，同一 ID **在不同前缀下可重复**。
> 若只用全局分配器，ID 会被无谓耗尽、且删前缀时收不回来。这是本模块最容易看错的一点。

### 7.3 策略表

`enum bgp_addpath_strat`（[bgp_addpath_types.h:50](bgpd/bgp_addpath_types.h#L50)）：

| 值 | 名称 | 语义 |
|---|---|---|
| 0 | `BGP_ADDPATH_ALL` | 全部路径都带 addpath 发（`addpath-tx-all-paths`） |
| 1 | `BGP_ADDPATH_BEST_PER_AS` | 每个 AS 的最佳路径发（依赖 deterministic-med） |
| 2 | `BGP_ADDPATH_BEST_SELECTED` | 发被选中的最佳 N 条 |
| 3 | `BGP_ADDPATH_MAX` | **不是策略**，是数组维度（3 种真实策略） |
| 4 | `BGP_ADDPATH_NONE` | 不发 |

> **易错点**：`MAX(=3)` 排在 `NONE(=4)` **之前**，所以 `xxx[BGP_ADDPATH_MAX]` 只能索引三条真实策略，
> **不能用它索引 NONE**。读数组声明时务必分清「维度」与「状态」。

每种策略的 5 种叫法集中在 `strat_names[BGP_ADDPATH_MAX]`（[bgp_addpath.c:47](bgpd/bgp_addpath.c#L47)）：
`config_name`（配置解析）/ `human_name` / `human_description`（展示）/ `type_json_name` /
`id_json_name`（JSON，改动即接口变更）。

### 7.4 核心判据与 ID 取用

| 问题 | 函数 | 位置 |
|---|---|---|
| 某 (peer, afi, safi) 用不用 Add-Path？ | `bgp_addpath_is_addpath_used` | [bgp_addpath.c:106](bgpd/bgp_addpath.c#L106) |
| 给定策略，这条路径该不该发？ | `bgp_addpath_tx_path` | [bgp_addpath.c:238](bgpd/bgp_addpath.c#L238) |
| 发送时用哪个 TX ID？ | `bgp_addpath_id_for_peer` | [bgp_addpath.c:164](bgpd/bgp_addpath.c#L164) |
| 策略是否需要 deterministic-med？ | `bgp_addpath_dmed_required` | [bgp_addpath.c:224](bgpd/bgp_addpath.c#L224) |

`bgp_addpath_tx_path`（[bgp_addpath.c:238](bgpd/bgp_addpath.c#L238)）恒等判据：

```c
case BGP_ADDPATH_ALL:            return true;
case BGP_ADDPATH_BEST_PER_AS:    return CHECK_FLAG(pi->flags, BGP_PATH_DMED_SELECTED);
case BGP_ADDPATH_BEST_SELECTED:  return true;   /* 上游已挑好最佳 N 条 */
case BGP_ADDPATH_NONE:
case BGP_ADDPATH_MAX:            return false;
```

`bgp_addpath_id_for_peer`（[bgp_addpath.c:164](bgpd/bgp_addpath.c#L164)）：先把 `SAFI_LABELED_UNICAST`
归一化为 `SAFI_UNICAST`，再按 `peer->addpath_type[afi][safi]` 取对应的 `addpath_tx_id[策略]`；
策略非法（≥ `MAX`）时返回 `IDALLOC_INVALID`。

> 默认路由（`network`/`default-originate` 产生的那条）固定用 `BGP_ADDPATH_TX_ID_FOR_DEFAULT_ORIGINATE`（值 1，
> [bgp_addpath.h:48](bgpd/bgp_addpath.h#L48)），因为它永远只有一条、不参与 ID 竞争，固定值便于排障。

### 7.5 `bgp_addpath_update_ids`：ID 复用

在每次选路之后被调用（[bgp_route.c:4052](bgpd/bgp_route.c#L4052)），对每种策略：

```
① 归还：遍历该前缀所有路径，凡「有 ID 但 bgp_addpath_tx_path 为假」的 → idalloc_free_to_pool 归还池
② 分配：遍历该前缀所有路径，凡「无 ID 但 bgp_addpath_tx_path 为真」的 → idalloc_allocate_prefer_pool 取 ID
③ 兜底：把池里剩下的 ID 全交还主分配器 idalloc_drain_pool
```

（[bgp_addpath.c:573](bgpd/bgp_addpath.c#L573)）。「先归还再分配」保证「换一条路径发送」时优先
复用刚归还的 ID，从而**避免多做一次 withdraw+update**——这正是 Add-Path 收敛优化的关键。

### 7.6 生命周期辅助

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_addpath_init_bgp_data` | [bgp_addpath.c:120](bgpd/bgp_addpath.c#L120) | 初始化实例级数据结构 |
| `bgp_addpath_free_info_data` | [bgp_addpath.c:143](bgpd/bgp_addpath.c#L143) | 释放单路径 ID |
| `bgp_addpath_info_has_ids` | [bgp_addpath.c:185](bgpd/bgp_addpath.c#L185) | 该路径是否有任一策略 ID（判据是 `!=0`） |
| `bgp_addpath_free_node_data` | [bgp_addpath.c:204](bgpd/bgp_addpath.c#L204) | 释放前缀 ID 池 |
| `bgp_addpath_flush_type` / `_rn` | [bgp_addpath.c:304](bgpd/bgp_addpath.c#L304) / [266](bgpd/bgp_addpath.c#L266) | 清某策略的全部 ID |
| `bgp_addpath_populate_path` / `_type` | [bgp_addpath.c:342](bgpd/bgp_addpath.c#L342) / [366](bgpd/bgp_addpath.c#L366) | 建分配器时填充已有路径 |
| `bgp_addpath_type_changed` | [bgp_addpath.c:428](bgpd/bgp_addpath.c#L428) | 邻居/邻居组 addpath 策略变化时增删分配器 |
| `bgp_addpath_set_peer_type` | [bgp_addpath.c:490](bgpd/bgp_addpath.c#L490) | 设置某 peer 某 AFI/SAFI 的策略 |

> **与更新组的关系（交叉引用卷1）**：Add-Path 让「同一条前缀要多条路径」，直接影响
> `bgp_updgrp_adv` / `bgp_updgrp_packet` 里「一个 dest 对应多少个待发条目」的逻辑；且
> **同一子组内 peer 必须策略一致**才能共用一段 UPDATE——这是更新组分组键要考虑 Add-Path 的原因。
> 能力协商（OPEN 报文）与接收侧 ID 处理不在本卷。

---

## §8 下一跳跟踪：`bgp_nexthop.c` + `bgp_nht.c`

### 8.1 一句话定位与两文件分工

「下一跳不可达 → 路径不可用 → 不参与选路」这条链，是 BGP 收敛的支点。它由两个文件协作：

| 文件 | 角色 | 关键内容 |
|---|---|---|
| `bgp_nexthop.c` | **本地信息与判定** | bnc 结构/树、直连地址表、`nexthop_self`（是不是我自己）、多路访问判定、scan 生命周期 |
| `bgp_nht.c` | **与 zebra 的 NHT 交互** | 向 zebra 注册/注销下一跳、接收 `ZEBRA_NEXTHOP_UPDATE`、把结果扇出到路径 (`evaluate_paths`) |

一句话：**`bgp_nexthop.c` 管「bnc 长什么样、怎么判」，`bgp_nht.c` 管「向 zebra 问、把答案传下去」**。

### 8.2 `bgp_nexthop_cache`（bnc）

`struct bgp_nexthop_cache`（[bgp_nexthop.h:76](bgpd/bgp_nexthop.h#L76)）是「一个被跟踪的下一跳」：

| 字段 | 行 | 语义 |
|---|---|---|
| `afi` | [77](bgpd/bgp_nexthop.h#L77) | 地址族 |
| `ifindex_ipv6_ll` | [80](bgpd/bgp_nexthop.h#L80) | **仅** IPv6 链路本地时有效，用于区分「同一 `fe80::` 在不同接口」——这也是键含 ifindex 的原因 |
| `metric` | [86](bgpd/bgp_nexthop.h#L86) | IGP 度量 |
| `nexthop_num` / `nexthop` | [89](bgpd/bgp_nexthop.h#L89)/[101](bgpd/bgp_nexthop.h#L101) | zebra 返回的下一跳个数与链表 |
| `is_evpn_gwip_nexthop` | [94](bgpd/bgp_nexthop.h#L94) | EVPN 网关 IP 下一跳，有效性判据特殊 |
| `change_flags` | [96](bgpd/bgp_nexthop.h#L96) | 「**这次相对上次变了什么**」 |
| `flags` | [103](bgpd/bgp_nexthop.h#L103) | 「**现在是什么状态**」 |
| `srte_color` | [151](bgpd/bgp_nexthop.h#L151) | SR-TE color，**参与 bnc 的键**（同前缀不同 color 是不同 bnc） |
| `prefix` / `resolved_prefix` | [156](bgpd/bgp_nexthop.h#L156)/[157](bgpd/bgp_nexthop.h#L157) | 原始/递归解析后的前缀 |
| `paths` / `path_count` | [159](bgpd/bgp_nexthop.h#L159)/[160](bgpd/bgp_nexthop.h#L160) | 反向引用：哪些路径挂在这条 bnc 上 |

**`flags` vs `change_flags` 是最容易混的一对**：`flags` 是快照（`BGP_NEXTHOP_VALID` 等，
[bgp_nexthop.h:120](bgpd/bgp_nexthop.h#L120)），`change_flags` 是事件（`BGP_NEXTHOP_CHANGED`/`_METRIC_CHANGED`/
`_MACIP_CHANGED`，[bgp_nexthop.h:97](bgpd/bgp_nexthop.h#L97)）。前者回答「能不能用」，后者回答「要不要通知别人」。

### 8.3 bnc 的 flags 位

| 位 | 行 | 含义 |
|---|---|---|
| `VALID` | [120](bgpd/bgp_nexthop.h#L120) | 可用（被 IGP 解析/直连/EVPN 场景 L3+L2 齐备）。**只有 VALID 的路径才可能被选为最佳并下发** |
| `REGISTERED` | [121](bgpd/bgp_nexthop.h#L121) | 已向 zebra 登记，正接收其变化通知 |
| `CONNECTED` | [122](bgpd/bgp_nexthop.h#L122) | 直连（eBGP 单跳的严格要求） |
| `PEER_NOTIFIED` | [123](bgpd/bgp_nexthop.h#L123) | 已通知相关 peer/路径（防重复通知） |
| `BGP_STATIC_ROUTE` / `_EXACT_MATCH` | [124](bgpd/bgp_nexthop.h#L124)/[125](bgpd/bgp_nexthop.h#L125) | 来自静态路由；后者要求精确匹配 |
| `LABELED_VALID` | [126](bgpd/bgp_nexthop.h#L126) | 带标签(MPLS)下一跳有效（VPN/标签单播单独判据） |
| `ULTIMATE` | [127](bgpd/bgp_nexthop.h#L127) | 递归解析到了最终下一跳 |
| `EVPN_INCOMPLETE` | [149](bgpd/bgp_nexthop.h#L149) | EVPN：L3 可达但 MAC/IP 未解析 |

> EVPN 场景有两个位 (`VALID` 与 `EVPN_INCOMPLETE`)，组合出的三种「不可用原因」见
> [bgp_nexthop.h:129-148](bgpd/bgp_nexthop.h#L129) 的对照表。正常情况（L3 可达且 MAC/IP 已解析）不单设位，
> 靠 `VALID=1 && INCOMPLETE=0` 唯一确定——省位但读代码要联合两个位判读。

### 8.4 bnc 的树与查找

bnc 存在一棵**无重复键红黑树**里（`DECLARE_RBTREE_UNIQ(bgp_nexthop_cache, ...)`，
[bgp_nexthop.h:166](bgpd/bgp_nexthop.h#L166)），比较器 `bgp_nexthop_cache_compare`（[bgp_nexthop.c:75](bgpd/bgp_nexthop.c#L75)）：

| 函数 | 位置 | 用途 |
|---|---|---|
| `bnc_new` | [bgp_nexthop.c:104](bgpd/bgp_nexthop.c#L104) | 建一个 bnc 并插入树 |
| `bnc_free` | [bgp_nexthop.c:142](bgpd/bgp_nexthop.c#L142) | 释放 |
| `bnc_find` | [bgp_nexthop.c:152](bgpd/bgp_nexthop.c#L152) | 查找 |
| `bgp_nexthop_cache_reset` | [bgp_nexthop.c:172](bgpd/bgp_nexthop.c#L172) | 清空整棵树 |

### 8.5 本机地址与直连表

`bgp_nexthop.c` 还维护几张「本机视角」的表，用于回答「这个下一跳是不是我自己」：

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_address_init`/`_add`/`_del` | [389](bgpd/bgp_nexthop.c#L389)/[405](bgpd/bgp_nexthop.c#L405)/[434](bgpd/bgp_nexthop.c#L434) | 本机地址表（`bgp->address_hash`） |
| `bgp_connected_add`/`_delete` | [490](bgpd/bgp_nexthop.c#L490)/[571](bgpd/bgp_nexthop.c#L571) | 直连网段表（zebra 通告的 connected） |
| `bgp_nexthop_self` | [644](bgpd/bgp_nexthop.c#L644) | 下一跳 ≠ 本机（决定是否要更新源地址） |
| `bgp_hostroute_self` | [730](bgpd/bgp_nexthop.c#L730) | 本机主机路由 |
| `bgp_multiaccess_check_v4`/`_v6` | [745](bgpd/bgp_nexthop.c#L745)/[781](bgpd/bgp_nexthop.c#L781) | 下一跳是否与 peer 同在共享子网（多路访问） |
| `bgp_subgrp_multiaccess_check_v4` | [863](bgpd/bgp_nexthop.c#L863) | 更新子组级判定 |
| `bgp_scan_init`/`_finish` | [1523](bgpd/bgp_nexthop.c#L1523)/[1545](bgpd/bgp_nexthop.c#L1545) | 扫描相关表初始化/销毁 |
| `bgp_nexthop_dump_bnc_flags` | [1563](bgpd/bgp_nexthop.c#L1563) | 把 flags 转字符串（show/日志，缓冲 ≥ `BNC_FLAG_DUMP_SIZE`=180，[bgp_nexthop.h:185](bgpd/bgp_nexthop.h#L185)） |

`NEXTHOP_FAMILY(nexthop_len)`（[bgp_nexthop.h:52](bgpd/bgp_nexthop.h#L52)）把「下一跳长度」映射到地址族：
4/12→IPv4，16/24/32/48→IPv6，**其它长度→`AF_UNSPEC`**——面对意外长度时**不猜**而是给错误信号，是刻意设计。
（各长度含义见 [bgp_nexthop.h:43-49](bgpd/bgp_nexthop.h#L43) 的对照注释。）

### 8.6 `bgp_nht.c`：向 zebra 问，把答案传下去

#### (1) 有效性判定（三套判据）

`bgp_isvalid_nexthop`（[bgp_nht.c:89](bgpd/bgp_nht.c#L89)）是总入口，按场景分派：

| 场景 | 函数 | 位置 |
|---|---|---|
| 通用 | `bgp_isvalid_nexthop` | [89](bgpd/bgp_nht.c#L89) |
| eBGP | `bgp_isvalid_nexthop_for_ebgp` | [108](bgpd/bgp_nht.c#L108) |
| L3VPN | `bgp_isvalid_nexthop_for_l3vpn` | [205](bgpd/bgp_nht.c#L205) |

> **为什么三套**：eBGP 单跳要求下一跳直连（`CONNECTED`），VPNv4/VPNv6 则要求
> `LABELED_VALID`（带标签解析），iBGP 递归又不同。用同一套「VALID」判所有场景会把
> 不该可用的路径判成可用——收敛时出现黑洞路由。

#### (2) 登记与查找

`bgp_find_or_add_nexthop`（[bgp_nht.c:448](bgpd/bgp_nht.c#L448)）是路径与 bnc 建立关联的总入口：给定
(bgp_route, bgp_nexthop, afi, safi, peer, path)，找到或新建 bnc，并把 `path` 挂上去
（内部调 `path_nh_map`）。**路径↔bnc 是多对多**：一条路径可挂到多个 bnc（IPv6 全局+链路本地，
外加 AIGP 等），一个 bnc 也服务多条路径。

#### (3) 与路径解绑

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_unlink_nexthop_check` | [bgp_nht.c:241](bgpd/bgp_nht.c#L241) | 该 bnc 上已无路径则释放 |
| `bgp_unlink_nexthop` | [bgp_nht.c:263](bgpd/bgp_nht.c#L263) | 路径删除时解绑 |
| `bgp_delete_connected_nexthop` | [bgp_nht.c:718](bgpd/bgp_nht.c#L718) | 直连 bnc 清理 |
| `bgp_cleanup_nexthops` | [bgp_nht.c:1294](bgpd/bgp_nht.c#L1294) | 实例退出时清空 |

#### (4) `path_nh_map`：双向映射的总闸

`path_nh_map`（[bgp_nht.c:1936](bgpd/bgp_nht.c#L1936)，声明 [bgp_nht.h:125](bgpd/bgp_nht.h#L125)）同时维护两个方向：
`path->nexthop` 列表与 `bnc->paths` 列表。`path_map` 为真时建立，为假时解除。
`bgp_nht_register_nexthops`（[bgp_nht.c:1964](bgpd/bgp_nht.c#L1964)）在实例启动时批量登记。

#### (5) 接收 zebra 的更新并扇出

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_nexthop_update` | [bgp_nht.c:1199](bgpd/bgp_nht.c#L1199) | zclient 回调入口（`ZEBRA_NEXTHOP_UPDATE`） |
| `bgp_process_nexthop_update` | [bgp_nht.c:832](bgpd/bgp_nht.c#L832) | 更新 bnc 的 flags/metric/下一跳 |
| `bgp_bnc_mark_nht_important` | [bgp_nht.c:781](bgpd/bgp_nht.c#L781) | 标记递归下一跳 |
| `make_prefix` | [bgp_nht.c:1328](bgpd/bgp_nht.c#L1328) | 从路径构造要注册的前缀 |
| `sendmsg_zebra_rnh` | [bgp_nht.c:1488](bgpd/bgp_nht.c#L1488) | 发 `ZEBRA_NEXTHOP_REGISTER/UNREGISTER` |
| `register_zebra_rnh` | [bgp_nht.c:1559](bgpd/bgp_nht.c#L1559) | 登记封装 |

#### (6) `evaluate_paths`：扇出到选路

`evaluate_paths`（[bgp_nht.c:1650](bgpd/bgp_nht.c#L1650)，声明 [bgp_nht.h:155](bgpd/bgp_nht.h#L155)）是「下一跳状态变了 → 重新选路」
的枢纽：遍历 `bnc->paths` 上的每条路径，重判 `bgp_isvalid_nexthop`，若 `VALID` 位发生变化
（或带 `BGP_PATH_IGP_CHANGED`）就调 `bgp_process`（卷2，重新跑选路+下发 RIB）。

它还触发一个**可挂载钩子** `bgp_nht_path_update`（`DECLARE_HOOK`，[bgp_nht.h:169](bgpd/bgp_nht.h#L169)）——
给 EVPN/VNC 等模块在「下一跳有效性变化」时插队处理的机会。

### 8.7 接口事件

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_nht_ifp_up` | [bgp_nht.c:1107](bgpd/bgp_nht.c#L1107) | 接口 up |
| `bgp_nht_ifp_down` | [bgp_nht.c:1112](bgpd/bgp_nht.c#L1112) | 接口 down |
| `bgp_nht_interface_events` | [bgp_nht.c:1155](bgpd/bgp_nht.c#L1155) | 按 peer 触发接口相关重算 |

> **链路本地下一跳的关键**：IPv6 LL 下一跳的 bnc 键含 ifindex，接口 down 会让整条 bnc 失效，
> 所以这里必须逐接口处理，而不能只按地址查。

### 8.8 「不可达 → 不可用」完整链（ASCII）

```
zebra (NHT)                                   bgpd
   │                                            │
   │  ZEBRA_NEXTHOP_UPDATE                      │
   ├──────────────────────────────────────────▶ bgp_nexthop_update (nht.c:1199)
   │                                            │
   │                                     bgp_process_nexthop_update (nht.c:832)
   │                                            │ 更新 bnc->flags / change_flags
   │                                            ▼
   │                                     evaluate_paths (nht.c:1650)
   │                                            │ 遍历 bnc->paths
   │                                            │ 重算 bgp_isvalid_nexthop (nht.c:89)
   │                                            ▼
   │                                     bgp_process (卷2) 重跑选路
   │                                            │
   │                     BGP_PATH_MULTIPATH/最佳重算，重新下发 RIB
   ▼
 (bgpd 侧) 路径若不再 VALID → 从最佳路径剔除 → withdraw / 换下一跳
```

---

## §9 抖动抑制：`bgp_damp.c`（RFC 2439）

### 9.1 一句话定位

一条前缀的路径反复 up/down（flap）会拖垮全网收敛。dampening 用「**惩罚值累积 + 衰减**」模型：
每条抖动加分，随时间按半衰期指数衰减；分数超过 `suppress` 就抑制，降到 `reuse` 以下才恢复。

算法出处：**RFC 2439**（BGP Route Flap Damping）。

### 9.2 核心数据结构

`struct bgp_damp_config`（[bgp_damp.h:110](bgpd/bgp_damp.h#L110)）分两组：**用户配置值**与**预计算表**。

用户配置（单位/含义）：

| 字段 | 缺省 | 行 |
|---|---|---|
| `reuse`（恢复阈值） | `DEFAULT_REUSE` | [bgp_damp.h:176](bgpd/bgp_damp.h#L176) |
| `suppress`（抑制阈值） | `DEFAULT_SUPPRESS` | [bgp_damp.h:177](bgpd/bgp_damp.h#L177) |
| `half_life`（半衰期） | `DEFAULT_HALF_LIFE` | [bgp_damp.h:175](bgpd/bgp_damp.h#L175) |
| `max_penalty` | `2*DEFAULT_SUPPRESS` | — |
| `t`（衰减步长） | `DELTA_T` | [bgp_damp.h:169](bgpd/bgp_damp.h#L169) |

预计算表：
- `decay_array` / `decay_array_fixed`：按 step 索引的衰减系数。
- **`reuse_list[REUSE_LIST_SIZE]`**：按惩罚值分桶的列表，桶里挂该分数段的 `bgp_damp_info`。
  这是性能关键——**恢复检查不用遍历所有路由，只需看对应桶**。
- `reuse_offset`：桶索引基准。

| 宏 | 行 | 值/含义 |
|---|---|---|
| `BGP_DAMP_NONE` | [160](bgpd/bgp_damp.h#L160) | 未启用 |
| `DELTA_REUSE` | [166](bgpd/bgp_damp.h#L166) | 每个桶覆盖的分数跨度 |
| `DELTA_T` | [169](bgpd/bgp_damp.h#L169) | 衰减步长（秒） |
| `DEFAULT_PENALTY` | [173](bgpd/bgp_damp.h#L173) | 每次事件加的分（1000） |
| `REUSE_LIST_SIZE` | [179](bgpd/bgp_damp.h#L179) | 桶数 |
| `REUSE_ARRAY_SIZE` | [180](bgpd/bgp_damp.h#L180) | 衰减数组大小 |
| `BGP_DAMP_NO_REUSE_LIST_INDEX` | [bgp_damp.h:80](bgpd/bgp_damp.h#L80) | 「不在任何桶里」（已恢复/未抑制） |

`struct bgp_damp_info`（[bgp_damp.h:52](bgpd/bgp_damp.h#L52)）挂在**每条被抑制的路径**上：
记录 penalty、suppress 时间、上次衰减时间、复用索引、所属 `bgp_damp_config`。

### 9.3 惩罚值衰减：`damp_decay`

`bgp_damp_decay`（[bgp_damp.c:201](bgpd/bgp_damp.c#L201)）核心公式（每经过 `t` 秒）：

```
penalty = penalty / 2^(t / half_life)
```

为避免每步都算 `pow()`，实现用**预计算的 `decay_array`**：把「一个 step 的衰减比例」
预先算进数组，衰减时查表相乘。这样每条路由的衰减是 O(1) 次乘法。

### 9.4 桶的增删（`reuselist_*`）

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_reuse_index` | [bgp_damp.c:141](bgpd/bgp_damp.c#L141) | 由 penalty 算桶索引 |
| `bgp_reuse_list_add` | [bgp_damp.c:166](bgpd/bgp_damp.c#L166) | 加进桶（越界即 `NO_REUSE_LIST_INDEX`） |
| `bgp_no_reuse_list_add` | [bgp_damp.c:183](bgpd/bgp_damp.c#L183) | 标记「不在桶里」 |
| `bgp_reuselist_add`/`_del`/`_switch` | [58](bgpd/bgp_damp.c#L58)/[65](bgpd/bgp_damp.c#L65)/[73](bgpd/bgp_damp.c#L73) | 底层链表操作 |

`bgp_reuse_timer`（[bgp_damp.c:227](bgpd/bgp_damp.c#L227)）周期跑：先 `bgp_damp_decay` 全表衰减，再看
「当前桶」里有没有分数降到 `reuse` 以下的路由，有就 `damp_withdraw` 恢复。

### 9.5 事件入口

| 事件 | 函数 | 位置 |
|---|---|---|
| 收到 update | `bgp_damp_update` | [bgp_damp.c:412](bgpd/bgp_damp.c#L412) |
| 收到 withdraw | `bgp_damp_withdraw` | [bgp_damp.c:313](bgpd/bgp_damp.c#L313) |
| 释放 info | `bgp_damp_info_free` | [bgp_damp.c:458](bgpd/bgp_damp.c#L458) |
| 清理整条路由 | `bgp_damp_info_clean` | [bgp_damp.c:590](bgpd/bgp_damp.c#L590) |

`bgp_damp_withdraw` 每次触发就给 penalty 加 `DEFAULT_PENALTY`（1000）；
`bgp_damp_update` 在收到 update 时若仍在抑制则保持抑制。

### 9.6 生命周期与 per-peer

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_damp_parameter_set` | [bgp_damp.c:492](bgpd/bgp_damp.c#L492) | 由配置算衰减数组/桶边界 |
| `bgp_damp_enable` | [bgp_damp.c:560](bgpd/bgp_damp.c#L560) | 启用 |
| `bgp_damp_disable` | [bgp_damp.c:639](bgpd/bgp_damp.c#L639) | 关闭（含清表、恢复被抑制路由） |
| `bgp_peer_damp_enable` | [bgp_damp.c:956](bgpd/bgp_damp.c#L956) | per-peer 配置 |
| `bgp_damp_info_vty`（显示单个） | [bgp_damp.c:761](bgpd/bgp_damp.c#L761) | show 命令 |
| `bgp_damp_reuse_time_vty` | [bgp_damp.c:821](bgpd/bgp_damp.c#L821) | 预测 `reuse` 时间 |

`bgp_damp_config` 挂在 `bgp->damp`（实例级），也可 per-peer（`peer->damp`）。
`get_active_bdc_from_pi`（[bgp_damp.c:119](bgpd/bgp_damp.c#L119)，声明 [bgp_damp.h:196](bgpd/bgp_damp.h#L196)）按
「peer 有配置用 peer 的，否则用实例的」选出当前生效的配置——**这是 peer 级覆盖实例级的落点**。

`BGP_RECORD_UPDATE`（[bgp_damp.h:86](bgpd/bgp_damp.h#L86)）宏控制是否记录「哪次 update 造成的抑制」，
用于展示。

> **陷阱**：penalty 单位不是秒，是**无量纲分数**。`half_life` 才是时间。把
> `reuse` 当「秒」读、把 `DELTA_T` 当「分数跨度」读，是最常见误读。

<!-- 代码索引与官方文档对应见本卷末尾 §15 / §16 -->


---

## §10 条件通告：`bgp_conditional_adv.c`

### 10.1 场景

条件通告（conditional advertisement）解决：**「只有当我的路由表里存在/不存在某些路由时，
我才向某个邻居通告 A 表」**。典型用例：多宿主双出口，只有备份链路「活着」（其路由表能看到
某条 sentinel 路由）时才对外通告，否则通告可能是黑洞。

三条 map：

| 名字 | 语义 |
|---|---|
| `advertise-map` | 满足条件时要通告的表（`route-map` 选出的前缀） |
| `exist-map` | 「必须**存在**」的路由（来自指定 peer） |
| `non-exist-map` | 「必须**不存在**」的路由 |

判定式：`exist-map 命中` **且** `non-exist-map 未命中` → 才放行 `advertise-map`。

### 10.2 数据结构与标志

**条件通告的配置并不是独立的结构体**，而是 `struct bgp_filter`（[bgpd.h:1306](bgpd/bgpd.h#L1306)）里一个
匿名成员 `advmap`（[bgpd.h:1343-1350](bgpd/bgpd.h#L1343)）：含 `aname/amap`（advertise-map）、
`condition`（bool）、`cname/cmap`（exist/non-exist 的依据 map）、`update_type`。
`condition` 与 `enum update_type`（[bgpd.h:1302](bgpd/bgpd.h#L1302)，`UPDATE_TYPE_WITHDRAW` /
`UPDATE_TYPE_ADVERTISE`）分别表示「用 exist 还是 non-exist」和「当前应当撤还是发」。
两个 `#define`：`CONDITION_NON_EXIST`（[bgpd.h:1299](bgpd/bgpd.h#L1299)）与
`CONDITION_EXIST`（[bgpd.h:1300](bgpd/bgpd.h#L1300)）。
**实例级**变化标志是 `bgp->advmap_config_change[AFI_MAX][SAFI_MAX]` 与 `advmap_table_change`
（[bgpd.h:2368](bgpd/bgpd.h#L2368)、[2369](bgpd/bgpd.h#L2369)）。

### 10.3 判定核心

`bgp_check_rmap_prefixes_in_bgp_table`（[bgp_conditional_adv.c:49](bgpd/bgp_conditional_adv.c#L49)）：遍历被
监视 peer 的 RIB，判断 exist-map/non-exist-map 里的前缀是否命中。

`bgp_conditional_adv_routes`（[bgp_conditional_adv.c:102](bgpd/bgp_conditional_adv.c#L102)）：对每个邻居、每组 map
调用上面的检查，根据结果调用「开始/停止通告」路径。

### 10.4 定时轮询机制（关键设计点）

**条件不会主动通知**——`exist-map` 依赖目标路由是否存在，而它可能出现/消失于任意时刻。FRR 用
**周期性轮询**实现，而非事件驱动：

| 项 | 位置 | 值/说明 |
|---|---|---|
| `bgp_conditional_adv_timer` | [bgp_conditional_adv.c:211](bgpd/bgp_conditional_adv.c#L211) | 轮询回调 |
| `DEFAULT_CONDITIONAL_ROUTES_POLL_TIME` | [bgp_conditional_adv.h:71](bgpd/bgp_conditional_adv.h#L71) | 缺省轮询秒数 |
| 调试位 `bgp_cond_adv_debug` | [bgp_conditional_adv.h:61](bgpd/bgp_conditional_adv.h#L61) | `debug bgp conditional-advertisement` |

轮询意味着**条件满足到实际通告之间有最多一个轮询周期的延迟**。想降低延迟只能调小周期，
代价是 CPU。这是设计与实现间最重要的取舍，读源码时必须知道。

### 10.5 配置注册

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_conditional_adv_enable` | [bgp_conditional_adv.c:369](bgpd/bgp_conditional_adv.c#L369) | 启用（peer 级），登记 map、启动 timer |
| `bgp_conditional_adv_disable` | [bgp_conditional_adv.c:400](bgpd/bgp_conditional_adv.c#L400) | 关闭 |
| `peer_advertise_map_filter_update` | [bgp_conditional_adv.c:429](bgpd/bgp_conditional_adv.c#L429) | advertise-map 变化时重评估 |
| vty `peer_advertise_map_set` | [bgp_conditional_adv.c:490](bgpd/bgp_conditional_adv.c#L490) | 配置命令（`neighbor x advertise-map ...`） |
| vty `peer_advertise_map_unset` | [bgp_conditional_adv.c:536](bgpd/bgp_conditional_adv.c#L536) | 取消 |

> **与更新组的关系（交叉引用卷1）**：条件通告决定的是「这个 peer 的某表要不要进入通告」，
> 而具体怎么编码、怎么分组成 UPDATE 由更新组负责——详见卷1。

---

## §11 通告账本：`bgp_advertise.c` + `bgp_advertise.h`

### 11.1 一句话定位（本卷与卷1的分界）

`bgp_advertise.c` 是**通告账本**：它记录「**每条前缀 × 每个邻居** 我到底通告了什么、收邀了什么」，
是**上层账目**。至于「账目差异怎么打包成一段 UPDATE、怎么按更新组批量发送」，属于卷1
（更新组/编码/发送）。本卷只讲账本的数据结构与增删改。

### 11.2 两个方向的两张账

| 账本 | 结构 | 方向 | 谁维护 |
|---|---|---|---|
| `adj_out` | `struct bgp_adj_out` | **我发出去的**（outbound） | `dest->adj_out` 列表 |
| `adj_in` | `struct bgp_adj_in` | **我从邻居收到的**（inbound，用于 soft-reconfig） | `dest->adj_in` 列表 |

| 结构 | 位置 | 关键字段 |
|---|---|---|
| `struct bgp_adj_out` | [bgp_advertise.h:144](bgpd/bgp_advertise.h#L144) | `subgroup`、`adv` 链、前缀信息 |
| `struct bgp_adj_in` | [bgp_advertise.h:193](bgpd/bgp_advertise.h#L193) | `peer`、`attr`、`uptime`、`nexthop` |

`adj_out` 按 **update-subgroup** 汇总（不是按 peer）——**这是「账本」与「更新组」的分界**：
一条前缀对同一子组的多个 peer，账上只记一条 `adj_out`，因为它们的 UPDATE 一样。

### 11.3 通告属性 `bgp_advertise_attr`

`struct bgp_advertise_attr`（[bgp_advertise.h:110](bgpd/bgp_advertise.h#L110)）是**「一条要发出的属性」的账目**，
内含 intern 后的 `struct attr *attr` 与 `advertise` 链表。它自身也 intern（`hash` 表 + refcnt），
目的是让「同一条 attr 发给多个邻居」只存一份。

| 结构/宏 | 位置 |
|---|---|
| `struct bgp_advertise`（单条通告条目） | [bgp_advertise.h:72](bgpd/bgp_advertise.h#L72) |
| `DECLARE_DLIST`（adv 双链表） | [bgp_advertise.h:92](bgpd/bgp_advertise.h#L92) |
| `RB_HEAD`（按前缀索引的 adj_in 树） | [bgp_advertise.h:169](bgpd/bgp_advertise.h#L169) |
| `bgp_synchronize`（同步标记） | [bgp_advertise.h:218](bgpd/bgp_advertise.h#L218) |
| `BGP_PATH_INFO_ADD` | [bgp_advertise.h:231](bgpd/bgp_advertise.h#L231) |

### 11.4 `bgp_advertise_attr` 的哈希与 intern

`bgp_advertise_attr` 走自己的一套 hash（与 §4 的 `attrhash` 不同）：

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_advertise_attr_hash_alloc` | [bgp_advertise.c:88](bgpd/bgp_advertise.c#L88) | 哈希表分配 |
| `bgp_advertise_attr_hash_key` | [bgp_advertise.c:104](bgpd/bgp_advertise.c#L104) | 键 = `attr` 指针 |
| `bgp_advertise_attr_hash_cmp` | [bgp_advertise.c:114](bgpd/bgp_advertise.c#L114) | 比较键 |
| `bgp_advertise_attr_intern` | [bgp_advertise.c:169](bgpd/bgp_advertise.c#L169) | 取/建 `baa` 并 refcnt++ |
| `bgp_advertise_attr_unintern` | [bgp_advertise.c:194](bgpd/bgp_advertise.c#L194) | refcnt--，归零则释放 |

`bgp_advertise_attr_new`/`_free`（[65](bgpd/bgp_advertise.c#L65)/[75](bgpd/bgp_advertise.c#L75)）是底层分配。

> **存疑点（见 §15 勘误）**：`bgp_advertise_attr_unintern`（[bgp_advertise.c:194](bgpd/bgp_advertise.c#L194)）在
> refcnt 归零分支里，先 `hash_release` 再立即 `bgp_attr_unintern(&baa->attr)`——这与 §4 的
> 「intern 结构最后才 unintern」惯例一致，但 `baa` 上的 `attr` 在被多处 `bgp_advertise` 引用时，
> 释放时机依赖调用者严格配对 refcnt。阅读时须核对调用处的 refcnt 配对。

### 11.5 `adj_out` / `adj_in` 的增删

| 函数 | 位置 | 用途 |
|---|---|---|
| `bgp_advertise_new` / `_free` | [bgp_advertise.c:128](bgpd/bgp_advertise.c#L128)/[137](bgpd/bgp_advertise.c#L137) | 单条 adv 条目 |
| `bgp_advertise_add` | [bgp_advertise.c:147](bgpd/bgp_advertise.c#L147) | 加入链表 |
| `bgp_advertise_delete` | [bgp_advertise.c:155](bgpd/bgp_advertise.c#L155) | 从链表摘除 |
| `bgp_adj_out_lookup` | [bgp_advertise.c:231](bgpd/bgp_advertise.c#L231) | 查某子组对某前缀的账 |
| `bgp_adj_in_set` | [bgp_advertise.c:282](bgpd/bgp_advertise.c#L282) | 记一笔收到的 |
| `bgp_adj_in_remove` | [bgp_advertise.c:320](bgpd/bgp_advertise.c#L320) | 删一笔收到的 |
| `bgp_adj_in_unset` | [bgp_advertise.c:346](bgpd/bgp_advertise.c#L346) | 清某 peer 的入账 |

### 11.6 与更新组的衔接（交叉引用卷1）

```
RIB 变化
  └─ bgp_process (卷2) ─▶ 更新组 (卷1)
                              │ 依据 adj_out 账本判断：
                              │   - 对哪些子组需要发/撤
                              │   - 与上次发过的 attr 是否相同（相同则跳过）
                              └─ 编码/打包/发送  ← 详见卷1
```

**要点**：账本只回答「发了什么」，不回答「怎么发」。`adj_out` 里那句 `adv->attr` 与
`bgp_advertise_attr` 的 refcnt 配对，是避免「重复发同样的 UPDATE」的依据。

---

## §12 小基础设施：内存标签 / 错误号 / 追踪点

三个文件体量都不大，但都是**全模块横切**的基础设施，改 bgpd 任何地方都可能碰到。

### 12.1 `bgp_memory.c` / `.h`：内存类型标签

FRR 的内存记账把每次分配归类到一个 **MTYPE**（memory type），用于
`show memory` 分类统计与泄漏定位。bgpd 的标签集中在这两个文件：

| 文件 | 宏 | 行 | 作用 |
|---|---|---|---|
| `bgp_memory.h` | `DECLARE_MGROUP(BGPD)` | [52](bgpd/bgp_memory.h#L52) | 声明「组」BGPD |
| `bgp_memory.h` | `DECLARE_MTYPE(...)` | [55](bgpd/bgp_memory.h#L55) 起，共 108 个 | 声明各 MTYPE |
| `bgp_memory.c` | `DEFINE_MGROUP(BGPD, "bgpd")` | [45](bgpd/bgp_memory.c#L45) | 定义组 |
| `bgp_memory.c` | `DEFINE_MTYPE(...)` | [48](bgpd/bgp_memory.c#L48) 起，共 107 个 | 定义各 MTYPE |

**为什么声明与定义分居 `.h`/`.c`**：`DECLARE_MTYPE` 展开成 `extern struct memtype *…`（只声明），
`DEFINE_MTYPE` 展开成真正的变量定义——和普通全局变量一样，**必须只定义一次**，否则重复定义。
所以别的 `.c` 只要 `#include "bgp_memory.h"` 就能用到 `MTYPE_BGP` 等符号。

数量：`.h` 里 108 个 `DECLARE_MTYPE`、`.c` 里 107 个 `DEFINE_MTYPE`（差额是文件本身的
`MTYPE_BGP_MAX` 之类的边界项，或声明多一个哨兵——阅读时以文件为准）。

使用方式：`XMALLOC(MTYPE_BGP_PEER, sizeof(struct peer))`。**改代码时新分配务必选一个语义
贴切的 MTYPE**，全用 `MTYPE_BGP` 会让 `show memory` 失去分类价值。

> 深入机制看 `lib/memory.h`（`struct memtype`、`qmalloc` 的原子计数），不在本卷。

### 12.2 `bgp_errors.c` / `.h`：错误号与日志引用

FRR 的 `ferr` 框架把「错误码 → 一行人类可读文本」集中注册，便于日志与排障。bgpd 的错误号
在**全局错误号空间里切一段**：起点是 `BGP_FERR_START = 0x02000001`（[lib/ferr.h:143](lib/ferr.h#L143)）。

| 文件 | 内容 | 行 |
|---|---|---|
| `bgp_errors.h` | `enum bgp_log_refs` 起 | [45](bgpd/bgp_errors.h#L45) |
| `bgp_errors.h` | 首项 `EC_BGP_ATTR_FLAG = BGP_FERR_START` | [47](bgpd/bgp_errors.h#L47) |
| `bgp_errors.h` | 共 **99** 个 `EC_BGP_*` 枚举项 | [45](bgpd/bgp_errors.h#L45) 起 |
| `bgp_errors.c` | `ferr_bgp_warn[]`（warn 级表） | [51](bgpd/bgp_errors.c#L51) |
| `bgp_errors.c` | `ferr_bgp_err[]`（err 级表） | [222](bgpd/bgp_errors.c#L222) |
| `bgp_errors.c` | `bgp_error_init()` | [620](bgpd/bgp_errors.c#L620) |

注册表是 `struct log_ref` 数组，每项 `.code = EC_BGP_xxx`、`.title`/`.descr`（人类可读）。
`.c` 里共 **92** 个 `.code = EC_BGP_*` 项（两个表合计，另有哨兵项终止）。
`bgp_error_init`（[620](bgpd/bgp_errors.c#L620)）只做两件事：`log_ref_add(ferr_bgp_err)` 与
`log_ref_add(ferr_bgp_warn)`——把两张表挂进全局；**它必须在实例起来前调用一次**（在 `bgp_main.c` 的
初始化路径）。

> **为什么从 `BGP_FERR_START` 开始**：各守护进程各占一段，错误号全局唯一，
> `0x02xxxxxx` 段即属于 bgpd。新增错误时**只能在段内追加、不能在中间插**——插值会移动
> 后面所有枚举的数值，破坏跨版本/跨工具的一致性。

### 12.3 `bgp_trace.c` / `.h`：LTTng 追踪点（生成机制）

`bgp_trace.h` 有 **1753 行**，看起来吓人，但**它是半自动生成风格、几乎全是宏**，不是手写逻辑。
理解它的**生成机制**比逐条读事件重要得多。

#### (1) 三个文件的角色

| 文件 | 行数 | 角色 |
|---|---|---|
| `lib/trace.h` | — | 定义**调用侧**宏 `frrtrace()` 等，按后端展开 |
| `bgp_trace.h` | 1753 | **声明**全部追踪点（被多处 `#include`） |
| `bgp_trace.c` | 28 | **唯一**把追踪点定义落地一次的编译单元 |

#### (2) LTTng 的「声明/定义分离」约定

LTTng-UST 要求：追踪点的**探针符号只能在恰好一个** `.c` 里定义一次，否则链接冲突。
于是拆成两半，用两个宏切换：

`bgp_trace.c`（这是它存在的全部理由）：

```c
#define TRACEPOINT_CREATE_PROBES   /* :19 让头展开出探针“代码” */
#define TRACEPOINT_DEFINE          /* :22 声明本 TU 负责“定义”这些追踪点 */
#include <zebra.h>                 /* :25 */
#include "bgp_trace.h"             /* :28 由此生成全部探针定义 */
```

而其它 `.c` 直接 `#include "bgp_trace.h"` 时这两个宏都**未**定义，头只展开成**声明**。
—— 这就是为什么 `bgp_trace.c` 没有函数、只有宏与 `#include`。

#### (3) `bgp_trace.h` 的骨架

| 位置 | 内容 |
|---|---|
| [70](bgpd/bgp_trace.h#L70) | 头保护：`#if !defined(_BGP_TRACE_H) || defined(TRACEPOINT_HEADER_MULTI_READ)`（**多读模式**，LTTng 要求头可被重复展开） |
| [79](bgpd/bgp_trace.h#L79) | `#ifdef HAVE_LTTNG`（整个文件的开关） |
| [83](bgpd/bgp_trace.h#L83) | `#undef TRACEPOINT_PROVIDER` → 定义为 `frr_bgp` |
| [87](bgpd/bgp_trace.h#L87) | `#undef TRACEPOINT_INCLUDE` → 指回本文件 |
| [91](bgpd/bgp_trace.h#L91) | `#include <lttng/tracepoint.h>` |
| [102](bgpd/bgp_trace.h#L102) 起 | 按功能**分段**：报文收发/过滤、属性、选路、更新组、EVPN、NHT…（9 段） |
| [1747](bgpd/bgp_trace.h#L1747) | `#include <lttng/tracepoint-event.h>`（真正展开事件） |
| [1750](bgpd/bgp_trace.h#L1750) | `#endif /* HAVE_LTTNG */` |
| [1753](bgpd/bgp_trace.h#L1753) | `#endif /* _BGP_TRACE_H */` |

内容统计（实测）：约 **104** 个 `TRACEPOINT_EVENT`、**2** 处 `TRACEPOINT_EVENT_CLASS` 定义、
**3** 处 `TRACEPOINT_EVENT_INSTANCE`。
`EVENT_CLASS` + `INSTANCE` 是 LTTng 的**复用**机制：公共字段先定义成一个「类」，
再用 `INSTANCE` 按名字实例化出多个事件——避免每个事件重复声明相同字段。
（导读注释见 [bgp_trace.h:38](bgpd/bgp_trace.h#L38)。）

#### (4) 调用侧

在 `bgp_*.c` 里触发事件统一写 `frrtrace(n, frr_bgp, <事件名>, ...)`（`n` 是参数个数，须与声明一致）。
`frrtrace()` 的定义在 `lib/trace.h`：**按编译后端三选一**——
`HAVE_LTTNG`（真实 LTTng，[lib/trace.h:67](lib/trace.h#L67)）、`HAVE_USDT`（SystemTap USDT，
[80](lib/trace.h#L80)）、否则展开成 `(void)0`（[88](lib/trace.h#L88)）——**关掉后端时零开销**。

全 bgpd 目录共 **151** 处 `frrtrace(` 调用（实测 `grep -rn` 计数）。

#### (5) 编译开关

| 开关 | 位置 | 作用 |
|---|---|---|
| `--enable-lttng` | [configure.ac:821](configure.ac#L821) | 请求 LTTng-UST 支持 |
| `HAVE_LTTNG` | [configure.ac:2188](configure.ac#L2188) 区 | 由上面探测结果定义；未探测到 lttng-ust 则报错（[configure.ac:2191](configure.ac#L2191)） |
| `#ifdef HAVE_LTTNG` | [bgp_trace.h:79](bgpd/bgp_trace.h#L79) | 未定义时整个头无内容 |

> **实践提示**：默认构建**不开** LTTng，`bgp_trace.h` 会被预处理成空——所以
> 「找不到某个 tracepoint」通常是**没开 `--enable-lttng`**，而不是代码里没有。

### 12.4 勘误：源码注释与代码/事实不符

按体例要求，专列一节。本卷在 23 个文件中发现的**实质性**不符如下（「注释里的行号已失效」是
已知现象，不计入）：

| # | 位置 | 注释说 | 实际是 |
|---|---|---|---|
| 1 | [bgp_nexthop.h:47](bgpd/bgp_nexthop.h#L47) | `24 : IPv6 全局 + 链路本地（…此处为 16+8 VPN）`——**自相矛盾**，前半句把 24 说成「IPv6 全局+链路本地」 | 24 = `BGP_ATTR_NHLEN_VPNV6_GLOBAL` = IPv6 全局(16) + **VPN 标签(8)**，即 **VPNv6**（[bgp_attr.h:75](bgpd/bgp_attr.h#L75)）。「IPv6 全局+链路本地」是 **32**（下一个条目才对）。`NEXTHOP_FAMILY` 把 24 归 `AF_INET6` 本身没错，但注释给的语义名称是错的 |

其余说明：

- **`bgp_advertise_attr_unintern` 的 refcnt 不对称**（[bgp_advertise.c:194](bgpd/bgp_advertise.c#L194)）：
  该文件的中文注释**自己就把它标为「存疑点」**，并说明「一个 baa 被多个 adv 共享时，第一次摘除
  就把属性引用释放了」，与 `bgp_advertise_attr_intern` 的约定不对称。**这里注释与代码是一致的**
  （代码确实是 refcnt>0 分支仍调 `bgp_attr_unintern`），故不算「注释说 X 代码 Y」；但作为
  **阅读时的风险点**，本卷在 §11.4 / §13 陷阱 15 反复提示，改这块代码前务必实测 refcnt。
- 本卷其余引用行号均在写作时用 `sed -n 'Np'` 实测；源码注释中的行号（如「见 1234 行」）因
  注释插入而整体后移，**一律未采信**。

---

## §13 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | 把 `struct attr` 当可写 | intern 后属性在全局共享，改一处污染全表 | 要改先 `bgp_attr_dup`/`bgp_attr_flush` 复制，或用专用 modify 接口 |
| 2 | `ATTR_FLAG_BIT(id)` 传 0 或 >64 | 编译期 `static_assert` 失败 | id 必须 1..64；看 `BGP_ATTR_*` 定义 |
| 3 | 误把 `MISSING_MANDATORY` 当致命 | 实际是提示缺 mandatory 属性，按 RFC 7606 走 treat-as-withdraw | 看 `enum bgp_attr_parse_ret` 语义，别一律 session reset |
| 4 | `mp_nexthop_len` 用魔法数 | 判错地址族 | 用 `NEXTHOP_FAMILY()`（[bgp_nexthop.h:52](bgpd/bgp_nexthop.h#L52)），非法长度得到 `AF_UNSPEC` 是**信号不是 bug** |
| 5 | 混淆 `bnc->flags` 与 `bnc->change_flags` | 拿「状态位」当「变更位」用 | flags 答「能不能用」，change_flags 答「要不要通知」 |
| 6 | 以为 Add-Path ID 全局唯一 | 多前缀下 ID 会重复，误判为冲突 | TX ID **每前缀一份池**（[bgp_addpath_types.h:81](bgpd/bgp_addpath_types.h#L81)） |
| 7 | 用 `BGP_ADDPATH_MAX` 索引 `NONE` | 数组越界 | `MAX=3` 是维度，`NONE=4` 是状态，二者不通用 |
| 8 | 把 dampening 的 `penalty` 当秒 | 时间计算全错 | penalty 无量纲，`half_life` 才是时间 |
| 9 | 以为条件通告会立即生效 | 实际最多等一个轮询周期 | 调 `DEFAULT_CONDITIONAL_ROUTES_POLL_TIME`（60s，[bgp_conditional_adv.h:71](bgpd/bgp_conditional_adv.h#L71)） |
| 10 | 在 `bgp_trace.h` 里找函数 | 全是宏，找逻辑找不到 | 那是声明头；落地在 [bgp_trace.c:19](bgpd/bgp_trace.c#L19) |
| 11 | 找不到 tracepoint 现象时报 bug | 其实没编译进去 | 确认 `--enable-lttng` / `HAVE_LTTNG` |
| 12 | 以为 `MULTIPATH_NUM` 缺省 64 | 配置校验按错数 | 缺省 **16**（[configure.ac:1068](configure.ac#L1068)），仅 `=0` 时 64 |
| 13 | 新增 ferr 错误插在枚举中间 | 后续错误号整体位移 | 只在 `EC_BGP_*` 段尾追加 |
| 14 | `adj_out` 按 peer 记账 | 与更新组分组矛盾 | `adj_out` **按 update-subgroup** 记（[bgp_advertise.h:144](bgpd/bgp_advertise.h#L144)） |
| 15 | `bgp_advertise_attr` 少 unintern 一次 | refcnt 泄漏 / 提前释放 | intern/unintern 严格配对（§11.4 存疑点） |
| 16 | 用 `evaluate_paths` 直接改 RIB | 绕过选路与下发 | 它只重算 VALID 并调 `bgp_process`，由卷2 走完整流程 |
| 17 | 忘 `bgp_error_init` | 错误号查不到文本 | 启动路径调用一次（[bgp_errors.c:620](bgpd/bgp_errors.c#L620)） |

---

## §14 自测题（复习用）

> 折叠答案：点开 `<details>` 查看。

1. **`bgp_attr.c` 在架构中的双重身份是什么？**
   <details><summary>答案</summary>一是 UPDATE 的属性**编解码引擎**（parse/encode 全部属性）；二是**属性去重中心**（interning：`attrhash` + refcnt，让相同属性全表只存一份，intern 后只读）。</details>

2. **RFC 4271 与 RFC 7606 的错误处理三种策略分别是什么？落到哪三种返回值？**
   <details><summary>答案</summary>session reset（最重）、treat-as-withdraw（把该路由当撤销，保留会话）、attribute-discard（丢弃该属性继续）。对应 `ERROR`/`WITHDRAW`（及 `WITHDRAW_IGNORE`）/`PROCEED`（或 `bgp_attr_ignore`）。</details>

3. **`bgp_attr_parse_ret` 各枚举值语义？**
   <details><summary>答案</summary>PROCEED=0 正常；ERROR=-1 致命；WITHDRAW=-2 当撤销；WITHDRAW_IGNORE=-3 当撤销且忽略该属性；ERROR_NOTIFYPLS=-4 需发 Notification；MISSING_MANDATORY=-5 缺必选属性。</details>

4. **属性重复（同一 type 出现两次）在解析循环里怎么判？**
   <details><summary>答案</summary>用 `attr->flag & ATTR_FLAG_BIT(type)` 检测，已置位即重复（[bgp_attr.c:4691](bgpd/bgp_attr.c#L4691)），触发 malformed 处理。</details>

5. **`bgp_best_selection` 与 `bgp_mpath.c` 的职责边界？**
   <details><summary>答案</summary>前者（卷2）按 RFC 4271 全套判据选出 1 条 best 并给等价候选打 `BGP_PATH_MULTIPATH_NEW`；后者把候选落实成最多 N 条 `BGP_PATH_MULTIPATH` 并生成聚合属性。</details>

6. **`MULTIPATH_NUM` 的缺省值是多少？怎么改？**
   <details><summary>答案</summary>缺省 16；`./configure --enable-multipath=N`（1..999）改；`=0` 变 64。运行时可由 `bgpd -e` 覆盖 `multipath_num`。</details>

7. **WECMP 的 `enum bgp_wecmp_behavior` 里为什么 `NONE=0` 很关键？**
   <details><summary>答案</summary>因为「未配置」与「显式关闭」同值，判断启用只需 `!= NONE`，无需额外布尔位。</details>

8. **Add-Path 的 TX ID 为什么按前缀分配而不是全局？**
   <details><summary>答案</summary>ID 语义是「该前缀的第几条路径」，不同前缀可复用同一 ID；全局分配会无谓耗尽且删前缀时收不回。</details>

9. **`bgp_addpath_update_ids` 为什么「先归还再分配」？**
   <details><summary>答案</summary>让「换一条路径发送」时优先复用刚归还的 ID，从而避免多做一次 withdraw+update，加速收敛。</details>

10. **`bgp_addpath_strat` 里 `MAX` 与 `NONE` 各是什么？能混用吗？**
    <details><summary>答案</summary>`MAX=3` 是数组维度（3 个真实策略），`NONE=4` 是状态「不发」。不能用 `MAX` 索引 `NONE`。</details>

11. **`bgp_nexthop.c` 与 `bgp_nht.c` 怎么分工？**
    <details><summary>答案</summary>前者管「bnc 长什么样、本地怎么判」（结构、直连表、`nexthop_self`、多路访问、scan）；后者管「向 zebra 注册/接收 NHT 更新、把结果扇出到路径」。</details>

12. **「下一跳不可达 → 路径不可用」的调用链？**
    <details><summary>答案</summary>zebra `ZEBRA_NEXTHOP_UPDATE` → `bgp_nexthop_update` → `bgp_process_nexthop_update`（改 bnc flags）→ `evaluate_paths`（遍历 bnc->paths 重算 VALID）→ `bgp_process`（卷2 重跑选路）。</details>

13. **bnc 的键为什么要含 ifindex？**
    <details><summary>答案</summary>因为 IPv6 链路本地地址 `fe80::` 可出现在多个接口上，只按地址无法区分；`ifindex_ipv6_ll` + 键内 ifindex 解决歧义。</details>

14. **`BGP_NEXTHOP_VALID` 与 `BGP_NEXTHOP_EVPN_INCOMPLETE` 组合的含义？**
    <details><summary>答案</summary>VALID=1 且 INCOMPLETE=0：L3 可达且 MAC/IP 已解析（可用）；VALID=0 且 INCOMPLETE=1：L3 可达但 MAC/IP 未解析；两者都 0：L3 不可达。</details>

15. **dampening 的惩罚值衰减公式？实现怎么加速？**
    <details><summary>答案</summary>每 `t` 秒 `penalty /= 2^(t/half_life)`；实现用预计算 `decay_array` 查表相乘，O(1)。</details>

16. **dampening 为什么用 `reuse_list` 分桶？**
    <details><summary>答案</summary>恢复检查只需看当前分数对应的桶，避免遍历全部被抑制路由——性能关键。</details>

17. **条件通告的判定式与实现机制？**
    <details><summary>答案</summary>`exist-map 命中 且 non-exist-map 未命中` 才放行 advertise-map；实现靠**周期轮询**（缺省 60s），不是事件驱动。</details>

18. **`adj_out` 与 `adj_in` 分别记什么？按什么粒度？**
    <details><summary>答案</summary>`adj_out` 记「我发出去的」，按 **update-subgroup** 粒度（同组多 peer 合并一条）；`adj_in` 记「我收到的」（soft-reconfig 用），按 peer。</details>

19. **`bgp_advertise_attr` 为什么要自己 intern 一套 hash？**
    <details><summary>答案</summary>让「同一条属性发给多个邻居」只存一份，配合 refcnt 决定何时释放；与 §4 的 `attrhash` 是两套不同的去重。</details>

20. **`MTYPE`/`DECLARE_MTYPE`/`DEFINE_MTYPE` 的关系？**
    <details><summary>答案</summary>`DEFINE_MTYPE` 是定义（只能一处，放 `.c`），`DECLARE_MTYPE` 是声明（放 `.h` 供他处 include）。类比普通全局变量的一次定义/多处声明。</details>

21. **ferr 错误号为什么从 `BGP_FERR_START` 开始、不能中间插？**
    <details><summary>答案</summary>各守护进程各占全局错误号空间的一段（bgpd 是 `0x02xxxxxx`），中间插入会移动后续枚举数值，破坏跨版本/工具一致性；只能在段尾追加。</details>

22. **`bgp_trace.h` 1753 行都是什么？为什么只在一个 `.c` 里落地？**
    <details><summary>答案</summary>全是 `TRACEPOINT_EVENT` 等宏声明（约 104 个事件）；LTTng 要求探针符号只定义一次，故 `bgp_trace.c` 定义 `TRACEPOINT_CREATE_PROBES`+`TRACEPOINT_DEFINE` 后 include 头，把定义落地一次。</details>

23. **`frrtrace()` 在未启用追踪的后端展开成什么？**
    <details><summary>答案</summary>`lib/trace.h` 按后端三选一；无 LTTng/USDT 时展开成 `(void)0`，即零开销。</details>

24. **`bgp_nexthop_dump_bnc_flags` 的调用方要注意什么？**
    <details><summary>答案</summary>缓冲区至少要 `BNC_FLAG_DUMP_SIZE`（180）字节（[bgp_nexthop.h:185](bgpd/bgp_nexthop.h#L185)）。</details>

25. **想降低条件通告生效延迟，代价是什么？**
    <details><summary>答案</summary>只能调小轮询周期（`DEFAULT_CONDITIONAL_ROUTES_POLL_TIME`），代价是轮询 CPU 上升。</details>

---

## §15 代码索引（按建议阅读顺序）

> 每一步给出**文件 + 起点行号 + 这一步要建立的认知**。建议按顺序读，前一步是后一步的前置。

**第 0 步：建立属性数据模型**
- [struct attr](bgpd/bgp_attr.h#L183)、[struct bgp_attr_parser_args](bgpd/bgp_attr.h#L506)、
  [ATTR_FLAG_BIT](bgpd/bgp_attr.h#L412)、[enum bgp_attr_parse_ret](bgpd/bgp_attr.h#L432)。
  建立认知：**属性是有 flag 的 TLV，聚合在一个大结构里，intern 后只读**。

**第 1 步：解析引擎**
- [bgp_attr_parse](bgpd/bgp_attr.c#L4561) → 主循环 → [bgp_attr_check](bgpd/bgp_attr.c#L4494)。
  建立认知：**解析是「逐 TLV + 收尾校验」两段式，错误按 RFC 7606 分三级**。

**第 2 步：错误处理决策树**
- [bgp_attr_malformed](bgpd/bgp_attr.c#L1943) / [bgp_attr_unknown](bgpd/bgp_attr.c#L4427) /
  [bgp_attr_ignore](bgpd/bgp_attr.c#L6490)。
  建立认知：**同一处 malformed，返回 WITHDRAW / ERROR / PROCEED 完全取决于属性与 flag**。

**第 3 步：interning**
- [bgp_attr_intern](bgpd/bgp_attr.c#L1437) → `attrhash` → [bgp_attr_unintern](bgpd/bgp_attr.c#L1823)。
  建立认知：**全表属性去重靠 refcnt，intern 结果不可改**。

**第 4 步：编码**
- [bgp_packet_attribute](bgpd/bgp_attr.c#L5550) 及其 MP_REACH/MP_UNREACH 三段。
  建立认知：**编码是解析的逆过程，且要按对端能力取舍属性**。（发送细节详见卷1。）

**第 5 步：选路下游**
- [bgp_mpath.c:bgp_path_info_nexthop_cmp](bgpd/bgp_mpath.c#L159) → [bgp_path_info_mpath_update:556](bgpd/bgp_mpath.c#L556)
  → [bgp_path_info_mpath_aggregate_update:736](bgpd/bgp_mpath.c#L736)。
  建立认知：**选路主函数在卷2；本文件负责「等价 Path 的落实」与聚合属性**。

**第 6 步：Add-Path**
- [bgp_addpath_types.h:三层数据](bgpd/bgp_addpath_types.h#L69) → [bgp_addpath_tx_path:238](bgpd/bgp_addpath.c#L238)
  → [bgp_addpath_update_ids:573](bgpd/bgp_addpath.c#L573)。
  建立认知：**TX ID 每前缀一份池，「先归还再分配」只为省一次 UPDATE**。

**第 7 步：下一跳跟踪**
- [bgp_nexthop.h:bnc 结构](bgpd/bgp_nexthop.h#L76) → [bgp_nht.c:bgp_find_or_add_nexthop:448](bgpd/bgp_nht.c#L448)
  → [evaluate_paths:1650](bgpd/bgp_nht.c#L1650)。
  建立认知：**「下一跳不可达 → 路径不可用」这条链的支点是 evaluate_paths**。

**第 8 步：抖动抑制**
- [bgp_damp.h:配置结构](bgpd/bgp_damp.h#L110) → [bgp_damp_decay:201](bgpd/bgp_damp.c#L201)
  → [bgp_reuse_timer:227](bgpd/bgp_damp.c#L227)。建立认知：**分桶 + 预计算衰减表 = O(1) 收敛判定**。

**第 9 步：条件通告**
- [bgp_conditional_adv.c:bgp_conditional_adv_timer:211](bgpd/bgp_conditional_adv.c#L211)
  → [bgp_check_rmap_prefixes_in_bgp_table:49](bgpd/bgp_conditional_adv.c#L49)。
  建立认知：**条件靠轮询，不是事件**。

**第 10 步：通告账本**
- [bgp_advertise.h:adj_out / adj_in](bgpd/bgp_advertise.h#L144)
  → [bgp_advertise_attr_intern:169](bgpd/bgp_advertise.c#L169)
  → [bgp_advertise_attr_unintern:194](bgpd/bgp_advertise.c#L194)。
  建立认知：**账本按 subgroup 记，是「更新组」的上层账目**（发送详见卷1）。

**第 11 步：横切基础设施**
- [bgp_memory.h:52](bgpd/bgp_memory.h#L52) → [bgp_errors.h:47](bgpd/bgp_errors.h#L47)
  → [bgp_trace.c:19](bgpd/bgp_trace.c#L19)。建立认知：**内存标签 / 错误号段 / 追踪点声明-定义分离**。

---

## §16 与官方 `doc/developer/*.rst` 的对应

| 本卷小节 | 主题 | 官方文档 |
|---|---|---|
| §1–§5 | 属性模型 / 解析 / 错误处理 / intern / 编码 | [doc/developer/bgp-typecodes.rst](doc/developer/bgp-typecodes.rst)（BGP typecodes 生成，25 行） |
| §6 | 多路径 / ECMP / `maximum-paths` | [doc/user/bgp.rst:457](doc/user/bgp.rst#L457)（`maximum-paths`）、[:399](doc/user/bgp.rst#L399)（`multipath-relax`） |
| §7 | Add-Path（RFC 7911） | [doc/user/bgp.rst:1897](doc/user/bgp.rst#L1897)（`addpath-tx-*` / `disable-addpath-rx` / `addpath-rx-paths-limit`） |
| §8 | 下一跳跟踪（NHT） | [doc/developer/next-hop-tracking.rst](doc/developer/next-hop-tracking.rst)（350 行，NHT 设计与接口） |
| §9 | 抖动抑制（RFC 2439） | [doc/user/bgp.rst:618](doc/user/bgp.rst#L618)（route-flap dampening，`:623` `bgp dampening` / `:628` `neighbor ... dampening`） |
| §10 | 条件通告 | [doc/user/bgp.rst:4041](doc/user/bgp.rst#L4041)（conditional advertisement，`neighbor advertise-map`） |
| §11 | 通告账本 | 无专文；发送/更新组见卷1 与 [doc/developer/hooks.rst](doc/developer/hooks.rst) 的更新组钩子 |
| §12.1 | 内存标签 MTYPE | [doc/developer/memtypes.rst](doc/developer/memtypes.rst)（140 行，`DEFINE_MTYPE`/`DECLARE_MTYPE` 规范） |
| §12.2 | 错误号 / 日志引用 | [doc/developer/logging.rst](doc/developer/logging.rst)（912 行，ferr 框架） |
| §12.3 | LTTng 追踪点 | [doc/developer/tracing.rst](doc/developer/tracing.rst)（411 行，tracepoint 生成与 `--enable-lttng`） |

**两处文档/代码口径不一致（提示）**：

1. [doc/user/bgp.rst:457](doc/user/bgp.rst#L457) 的 `maximum-paths (1-128)` 写的是 **1-128**，
   但源码里命令范围是 `CMD_RANGE_STR(1, MULTIPATH_NUM)`（[bgp_vty.c:3012](bgpd/bgp_vty.c#L3012)），
   `MULTIPATH_NUM` **缺省 16**——即**默认构建下上限是 16，不是 128**；只有 `--enable-multipath=128`
   （或更大）才与文档一致。看代码校验时以 `MULTIPATH_NUM` 为准。
2. [doc/user/bgp.rst:1897](doc/user/bgp.rst#L1897) 的 `addpath-tx-best-selected (1-6)` 范围是 **1-6**，
   而源码里 `BGP_ADDPATH_BEST_SELECTED` 的路径上限与此一致（Add-Path best-selected 的上限常量），
   读配置时要留意该上限不是 `MULTIPATH_NUM`。

> 本卷未逐条展开官方文档内容，只做**指向**；深入请直接读对应 `.rst`。





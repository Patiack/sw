# FRR bgpd 深入剖析 · 卷2 —— 路由信息库与路径属性

> 本文基于仓库当前工作树（分支 `study/bgpd-annotated`，基线版本 **FRR 10.6.1**）逐文件阅读源码写成。
> 文中所有行号均为在本仓库实测（`grep -n` / `sed -n` 逐条核对），不是凭记忆或从别处转录。
> 本卷覆盖文件 22 个、共 34321 行（工作树行数，含中文注释）。
>
> 写作目的：回答以下问题 ——
> 1. bgpd 的 RIB 为什么是「radix 树套链表」（`bgp_table` → `bgp_dest` → `bgp_path_info`）？三层各自职责是什么？
> 2. 同一前缀的多条路径（来自不同邻居）在内存里怎么共存？`bgp_path_info` 与 `bgp_path_info_extra` 为什么拆开？
> 3. 各类路径属性的**内存表示**与**线上编解码**分别是怎样的？（AS_PATH 段、团体/扩展团体/大团体哈希去重、RD 8 字节）
> 4. 属性里的「关键/非关键」「传递/非传递」标志位在代码里怎么体现？
> 5. `bgp_clist.c`（团体列表）与 `bgp_filter.c`（AS_PATH 访问列表）与 route-map 的边界在哪？
> 6. `bgp_community_alias.c` 解决什么问题？
> 7. `bgp_route.c`（19317 行）内部是怎么分区的？
>
> 本卷是 `FRR_BGPD_STUDY.md`（总览篇）之下的分册。总览篇给出 bgpd 的整体架构、三线程模型、
> 核心数据结构与 131 文件全景地图；本卷只深入「路由信息库与路径属性」这一层，其余层次请在
> 对应分册中查阅。**属性编解码的校验/收发主流程在 `bgp_attr.c`，归卷3**；本卷只讲属性的
> **数据结构与容器**。本文不修改任何源码。

---

## 本卷文件覆盖清单（自检用）

| # | 文件 | 行数 | 一句话职责 | 本卷小节 |
|---|------|------|-----------|---------|
| 1 | `bgp_route.c` | 19317 | RIB 中枢：入向落地、选路、下发、聚合、show 全在此 | §1 §2 §4 §5 §6 §7 §8 |
| 2 | `bgp_route.h` | 1198 | 路径模型 `bgp_path_info`／`extra`、静态/聚合配置、meta_queue | §1 §2 §4 §7 |
| 3 | `bgp_table.c` | 346 | 通用前缀树（lib `route_table`）的 BGP 包壳与节点生命周期 | §3 |
| 4 | `bgp_table.h` | 540 | RIB 三层数据模型、路径选择理由枚举、inline 容器工具 | §1 §3 |
| 5 | `bgp_aspath.c` | 3094 | AS_PATH 的段表示、线格式编解码、intern、正则/防环 | §9 |
| 6 | `bgp_aspath.h` | 400 | `struct assegment`/`struct aspath`/`aspath_exclude` 与常量 | §9 |
| 7 | `bgp_community.c` | 1192 | 标准团体：解析/打印/intern/集合运算/聚合 | §10 |
| 8 | `bgp_community.h` | 187 | `struct community`、well-known 常量、内联剔除 | §10 §12 |
| 9 | `bgp_ecommunity.c` | 2345 | 扩展团体（8/20 字节）：编码器、RT/SoO 打印、intern | §10 §12 |
| 10 | `bgp_ecommunity.h` | 590 | `struct ecommunity`、类型/子类型常量、编码内联函数 | §10 §12 |
| 11 | `bgp_lcommunity.c` | 761 | 大团体（12 字节）：解析/打印/intern | §10 |
| 12 | `bgp_lcommunity.h` | 165 | `struct lcommunity`、`lcommunity_val` 与接口 | §10 |
| 13 | `bgp_rd.c` | 340 | RD 的三种编码解码、字符串互转、`rd auto` | §11 |
| 14 | `bgp_rd.h` | 142 | RD 类型码、`struct rd_as/rd_ip`、`%pRD` 格式化宏 | §11 |
| 15 | `bgp_community_alias.c` | 291 | 团体「数值 ↔ 别名」双向映射的两张哈希表 | §14 |
| 16 | `bgp_community_alias.h` | 106 | `struct community_alias` 与转换接口 | §14 |
| 17 | `bgp_clist.c` | 1742 | community/extcommunity/large-community list 三套过滤器 | §13 |
| 18 | `bgp_clist.h` | 322 | 三套 master/entry 结构、六种 style 常量 | §13 |
| 19 | `bgp_filter.c` | 950 | as-path access-list：正则过滤器与 seq 编排 | §13 |
| 20 | `bgp_filter.h` | 127 | `struct as_list`/`as_filter`、`as_list_apply` | §13 |
| 21 | `bgp_regex.c` | 123 | AS_PATH 正则的 `_`→边界翻译与 POSIX 编译 | §13 |
| 22 | `bgp_regex.h` | 43 | `bgp_regcomp/bgp_regexec/bgp_regex_free` 三个接口 | §13 |

> **覆盖率自检**：本表共 22 行，与本卷分配到的 22 个文件**逐一对齐，一个不漏、一个不多**。
> 行数之和 19317+1198+346+540+3094+400+1192+187+2345+590+761+165+340+142+291+106+1742+322+950+127+123+43 = 34321。

---

## §0 本卷总览

### 0.1 先给结论

> **bgpd 的 RIB 是三层结构，且三层是「通用容器（表）→ 多态外壳（节点）→ 具体业务对象（路径）」的层层下放：**
> - `bgp_table`（[bgp_table.h:55](bgpd/bgp_table.h#L55)）只是**给 lib 的通用前缀树 `route_table` 套了一层 BGP 语义的壳**——它不会自己实现 radix 树，而是把一棵 `struct route_table *`（[bgp_table.h:75](bgpd/bgp_table.h#L75)）包进来复用最长匹配/迭代能力。
> - `bgp_dest`（[bgp_table.h:136](bgpd/bgp_table.h#L136)）是**树上某个前缀节点的多态外壳**，它的 `void *info`（[bgp_table.h:139](bgpd/bgp_table.h#L139)）指向什么取决于这张表的用途：RIB 里指向 `bgp_path_info` 链表，聚合表里指向 `bgp_aggregate`，静态表里指向 `bgp_static`。
> - `bgp_path_info`（[bgp_route.h:343](bgpd/bgp_route.h#L343)）才是「一条路由」，**同一前缀下多条路径用 `next/prev` 串成单链表**（[bgp_route.h:345](bgpd/bgp_route.h#L345)）。

> **同一前缀的多条路径的确靠「链表」共存，但链表不是唯一的索引**：为了 O(1) 按 `(peer,type,sub_type,addpath_id)` 定位，`bgp_table` 里还额外挂了一张跨前缀的类型安全哈希 `pi_hash`（[bgp_table.h:66](bgpd/bgp_table.h#L66)、[bgp_table.h:50](bgpd/bgp_table.h#L50)）。

> **`bgp_path_info_extra` 是「懒分配的附属仓」**：抖动抑制、标签栈、EVPN/VRF 泄漏/FlowSpec 数据这些**绝大多数路径用不到**的字段集中到一个独立结构（[bgp_route.h:265](bgpd/bgp_route.h#L265)），需要时 `bgp_path_info_extra_get()` 才分配，是省内存的关键设计。

> **团体三兄弟（community/ecommunity/lcommunity）共享同一套「全局哈希驻留 + 引用计数」范式**，但键的原语不同：标准团体按 `jhash2` 的 4 字节值数组、扩展团体按 8/20 字节单元、大团体按 12 字节单元——所以去重与比较函数各自成对（[bgp_community.c:638](bgpd/bgp_community.c#L638) / [bgp_ecommunity.c:409](bgpd/bgp_ecommunity.c#L409) / [bgp_lcommunity.c:365](bgpd/bgp_lcommunity.c#L365)）。

### 0.2 文件职责分层表

| 层次 | 文件 | 角色 |
|---|---|---|
| **容器层** | `bgp_table.c/.h` | 给 lib `route_table` 套 BGP 壳；定义 `bgp_dest` 多态外壳与锁定/版本语义 |
| **模型层** | `bgp_route.h` | 定义「路径」`bgp_path_info`、附属仓 `extra`、静态/聚合配置、`meta_queue` |
| **主控层** | `bgp_route.c` | 入向落地→选路→下发→聚合→show 的全流程实现（19317 行） |
| **属性容器层** | `bgp_aspath.*` / `bgp_community.*` / `bgp_ecommunity.*` / `bgp_lcommunity.*` / `bgp_rd.*` | 五类属性的内存表示、编解码、驻留、字符串互转 |
| **策略过滤层** | `bgp_clist.*` / `bgp_filter.*` | 团体列表（3 类）与 as-path 访问列表——route-map 匹配条件的底座 |
| **基础设施层** | `bgp_regex.*` / `bgp_community_alias.*` | 正则编译（`_` 翻译）与团体别名双向映射 |

### 0.3 全景图：一条 UPDATE 的完整旅程

```
  bgp_packet.c 解析 UPDATE（本卷不展开，见卷1/卷3）
      │  NLRI + attr
      ▼
  bgp_update()  bgp_route.c:6210   ──┐  入向落地
      │  ①bto_afi_node_get 取/建 dest（VPN 走 RD 子表两层）  bgp_route.c:315
      │  ②pi_hash 查旧路径                                    bgp_table.h:66
      │  ③AS 环/入向过滤/route-map 校验                       bgp_route.c:2119/2348
      │  ④attr intern + info_make 挂入 dest 的 pi 链表        bgp_route.c:5895
      │  ⑤NHT 可达性 → BGP_PATH_VALID                        bgp_route.c:6100
      └─⑥bgp_process() 排入处理队列                           bgp_route.c:5607
              │
              ▼   （主事件循环稍后）
  meta_queue（3 子队列）  bgp_route.h:695
      │  bgp_process_main_one()  bgp_route.c:4454
      ▼
  bgp_best_selection()  bgp_route.c:3592
      │  逐条调用 bgp_path_info_cmp()（RFC4271 §9.1.2.2）     bgp_route.c:1063
      │  产出 best + ECMP（BGP_PATH_MULTIPATH 标记）
      ▼
  ┌───────────── 新旧最优 diff ─────────────┐
  │ 变了 → bgp_zebra_announce/withdraw（下发内核）          （见卷……zebra 侧）
  │      → subgroup_process_announce_selected 触发重通告    bgp_route.c:4074
  │      → subgroup_announce_check 出向属性加工             bgp_route.c:2665
  └──────────────────────────────────────────┘

  属性容器（被上面各步反复读写）：
    AS_PATH    bgp_aspath.c  段链 + str 缓存（正则/显示都用它）
    COMMUNITY  bgp_community.c  全局哈希 comhash 驻留
    ECOMM     bgp_ecommunity.c 全局哈希 ecomhash 驻留（8 或 20 字节单元）
    LCOMM     bgp_lcommunity.c 全局哈希 lcomhash 驻留（12 字节单元）
    RD         bgp_rd.c       8 字节，按 type 分派解码
```

### 0.4 与相邻卷的边界

| 问题 | 去哪看 |
|---|---|
| UPDATE 报文的解析、属性 TLV 的校验/收发（`bgp_attr.c`、`bgp_packet.c`） | **卷3**（路径属性与报文） |
| 邻居状态机、capability 协商（`bgp_fsm.c`、`bgp_open.c`） | 卷1（会话与邻居） |
| update-group 的实时分组与编码发包（`bgp_updgrp.c`、`bgp_packet.c`） | 卷3/卷4 |
| 向 zebra 下发、NHT（`bgp_zebra.c`、`bgp_nht.c`） | 卷5（外部集成） |
| `bgp_path_info` 在 `bgp_attr.c` 里如何被赋 attr | 卷3；本卷只讲 `attr` 指针与 intern 约定 |
| route-map 引擎本体（`lib/routemap.c`） | 卷6（策略框架） |

本卷只负责「属性的**容器**与 **RIB 结构**」。凡涉及属性取值细节，一律一行交叉引用「详见卷3」。

---

## §1 三层 RIB：表 / 节点 / 路径

### 1.1 结论：「radix 树套链表」不是 BGP 自造，而是**通用树 + BGP 外壳**

`bgp_table` 并不自己实现前缀树。看它的成员（[bgp_table.h:55-77](bgpd/bgp_table.h#L55)）：

- `struct route_table *route_table;`（[bgp_table.h:75](bgpd/bgp_table.h#L75)）——**真正的 radix 树**，来自 lib `table.c`，提供前缀最长匹配、遍历、锁计数等通用能力。
- `struct bgp *bgp;`＋`afi_t afi;`＋`safi_t safi;`（[bgp_table.h:57-61](bgpd/bgp_table.h#L57)）——BGP 语义：属于哪个实例、哪个地址族。
- `struct bgp_pi_hash_head pi_hash;`（[bgp_table.h:66](bgpd/bgp_table.h#L66)）——**跨前缀的路径哈希**（本节 1.4 详述）。
- `uint64_t version;`（[bgp_table.h:76](bgpd/bgp_table.h#L76)）——整表版本号，用于增量同步判断。

创建入口 `bgp_table_init()`（[bgp_table.c:216](bgpd/bgp_table.c#L216)）做四件事：建底层 `route_table`（用 BGP 定制的 delegate）、把 `bgp_table` 反挂回 `route_table->info`（供 `bgp_dest_table()` 反查，[bgp_table.h:230](bgpd/bgp_table.h#L230)）、记录 `bgp/afi/safi`、`bgp_pi_hash_init`。**注意初始 `lock=1`**（[bgp_table.c:233](bgpd/bgp_table.c#L233)），销毁时 `bgp_table_finish()`（[bgp_table.c:82](bgpd/bgp_table.c#L82)）要对应解锁。

### 1.2 第二层：`bgp_dest` —— 一个前缀节点的「多态外壳」

`bgp_dest` 的核心是两个指针：

| 成员 | 行号 | 语义 |
|---|---|---|
| `struct route_node *rn` | [bgp_table.h:137](bgpd/bgp_table.h#L137) | 反向指回底层节点；**前缀本身存在 `rn->p`** |
| `void *info` | [bgp_table.h:139](bgpd/bgp_table.h#L139) | **多态**指向上层业务对象（见下表） |

`info` 的「一指针多义」是本层最需要理解的点。`bgp_table.h` 后半段成片的 `bgp_dest_get_*/set_*`（[bgp_table.h:406-523](bgpd/bgp_table.h#L406)）**不做任何转换**，只是给同一个 `info` 加个类型解释。它指向什么，取决于所属表：

| 表 | `dest->info` 实际类型 | 取/设函数 |
|---|---|---|
| `rib[afi][safi]` | `struct bgp_path_info *`（路径链表头） | [bgp_table.h:480](bgpd/bgp_table.h#L480) / [486](bgpd/bgp_table.h#L486) |
| `aggregate[afi][safi]` | `struct bgp_aggregate *` | [bgp_table.h:407](bgpd/bgp_table.h#L407) |
| `static_routes[afi][safi]` | `struct bgp_static *` | [bgp_table.h:446](bgpd/bgp_table.h#L446) |
| 下一跳表 | `struct bgp_nexthop_cache *` | [bgp_table.h:468](bgpd/bgp_table.h#L468) |
| EVPN/EOIU 临时 | `struct bgp_eoiu_info *` | [bgp_table.h:498](bgpd/bgp_table.h#L498) |

**读代码时的护航规则**：看到 `info` 被 set 成别的类型，先确认当前在操作哪张表——同一 `dest` 同时有两种用途就是 bug（但实际每张表只承载一种用途，所以安全）。

除 `info` 外，`bgp_dest` 还挂着一堆「按前缀聚合的状态」：

- `adj_out`（[bgp_table.h:141](bgpd/bgp_table.h#L141)）出向通告状态红黑树、`adj_in`（[bgp_table.h:143](bgpd/bgp_table.h#L143)）入向记录（soft-reconfig 用）。
- `pdest`（[bgp_table.h:145](bgpd/bgp_table.h#L145)）父前缀（aggregate 用）；对 VPN 表，它指回 RD 外层节点（见 §3.3）。
- `mpath`（[bgp_table.h:180](bgpd/bgp_table.h#L180)）等价多路径信息。
- `flags`（[bgp_table.h:160](bgpd/bgp_table.h#L160)）`BGP_NODE_*` 共 13 位（[bgp_table.h:161-173](bgpd/bgp_table.h#L161)），最重要的两类：`BGP_NODE_PROCESS_SCHEDULED`（是否已排入 bestpath 队列）与 `BGP_NODE_FIB_INSTALL_PENDING/INSTALLED`（与 zebra 的同步状态）。

### 1.3 第三层：`bgp_path_info` —— 一条路由

`struct bgp_path_info`（[bgp_route.h:343](bgpd/bgp_route.h#L343)）表示「某前缀下的一条路径」。关键成员：

| 成员 | 行号 | 语义 |
|---|---|---|
| `next/prev` | [bgp_route.h:345](bgpd/bgp_route.h#L345) | 同前缀下的路径**单链表**（多条路径共存就靠它） |
| `pi_hash_link` | [bgp_route.h:349](bgpd/bgp_route.h#L349) | 挂进 `bgp_table.pi_hash` 的哈希链（见 1.4） |
| `nh_thread` | [bgp_route.h:352](bgpd/bgp_route.h#L352) | **另一个链表**：挂到「使用该下一跳的路径」表上（NHT 变更时快速定位受影响路径）。与 `next/prev` 不是同一个链表 |
| `net` | [bgp_route.h:355](bgpd/bgp_route.h#L355) | 反向指回所属 `bgp_dest` |
| `peer` / `from` | [bgp_route.h:361](bgpd/bgp_route.h#L361) / [364](bgpd/bgp_route.h#L364) | 谁通告的 / 实际来源（RR、导入时二者可不同） |
| `attr` | [bgp_route.h:367](bgpd/bgp_route.h#L367) | 属性指针（`struct attr`，定义在 `bgp_attr.h`，详见卷3） |
| `extra` | [bgp_route.h:370](bgpd/bgp_route.h#L370) | **懒分配**的附属仓（§2） |
| `flags` | [bgp_route.h:379](bgpd/bgp_route.h#L379) | `BGP_PATH_*` 状态位（21 位，[bgp_route.h:380-413](bgpd/bgp_route.h#L380)） |
| `type/sub_type` | [bgp_route.h:416](bgpd/bgp_route.h#L416) / [420](bgpd/bgp_route.h#L420) | 路由来源（BGP/static/aggregate/redistribute/imported）；`sub_type` 取值 [bgp_route.h:421-428](bgpd/bgp_route.h#L421) |
| `reason` | [bgp_route.h:432](bgpd/bgp_route.h#L432) | 作为最优时「因何胜出」（枚举见 [bgp_table.h:85](bgpd/bgp_table.h#L85)） |
| `addpath_rx_id` / `tx_addpath` | [bgp_route.h:435](bgpd/bgp_route.h#L435) / [436](bgpd/bgp_route.h#L436) | Add-Path(RFC7911) 收发标识 |
| `lock` | [bgp_route.h:376](bgpd/bgp_route.h#L376) | 引用计数 |

**flags 里最重要的几个**（排障必看）：

- `BGP_PATH_VALID`（[bgp_route.h:384](bgpd/bgp_route.h#L384)）——可参与选路（如下一跳可达）。
- `BGP_PATH_SELECTED`（[bgp_route.h:383](bgpd/bgp_route.h#L383)）——被选为最佳。
- `BGP_PATH_MULTIPATH`（[bgp_route.h:396](bgpd/bgp_route.h#L396)）——**ECMP 成员，且不设在最优路径上**（原注释特意强调）。
- `BGP_PATH_HISTORY` / `DAMPED` / `REMOVED`（[bgp_route.h:382-389](bgpd/bgp_route.h#L382)）——不可用状态。判「能不能用」的宏是 `BGP_PATH_HOLDDOWN`（[bgp_route.h:621](bgpd/bgp_route.h#L621)）与 `BGP_PATH_UNUSEABLE`（[bgp_route.h:617](bgpd/bgp_route.h#L617)）。

### 1.4 链表之外：为什么还要 `pi_hash`

链表足够表达「同前缀多条路径」，但**入向更新时需要按 `(peer,type,sub_type,addpath_id)` 快速找到「是不是已有这条路径」**，遍历链表是 O(N)。于是 `bgp_table` 里额外挂了一张类型安全的哈希 `pi_hash`：

- 声明 `PREDECL_HASH(bgp_pi_hash)`（[bgp_table.h:50](bgpd/bgp_table.h#L50)），表内成员 `struct bgp_pi_hash_head pi_hash`（[bgp_table.h:66](bgpd/bgp_table.h#L66)）。
- 键函数 `bgp_pi_hash_hashfn`（[bgp_route.c:232](bgpd/bgp_route.c#L232)）、比较函数 `bgp_pi_hash_cmp`（[bgp_route.c:202](bgpd/bgp_route.c#L202)）。
- 挂入/摘除：`bgp_path_info_add_with_caller` 里 `bgp_pi_hash_add`（[bgp_route.c:733](bgpd/bgp_route.c#L733)），摘除见 [bgp_route.c:769](bgpd/bgp_route.c#L769)、[793](bgpd/bgp_route.c#L793)。
- 入向查找旧路径：`bgp_pi_hash_find(&rib_table->pi_hash, &pi_lookup)`（[bgp_route.c:6292](bgpd/bgp_route.c#L6292)）。

所以三层结构其实是「**树（前缀）→ 链表（多路径）→ 哈希（快速定位某一路径）**」的复合索引。

---

### 1.5 RIB 三层与外部文件的联动表

RIB 不是只被 `bgp_route.c` 使用。下面这些「取节点 / 排队 / 选路」的入口被多个文件调用（实测 grep），
体现「RIB 是公共基础设施」：

| 被调函数 | 定义 | 主要调用方（文件:行，均为实测） |
|---|---|---|
| `bgp_afi_node_get` | [bgp_route.c:315](bgpd/bgp_route.c#L315) | `bgp_route.c:6253`（`bgp_update`）、`bgp_route.c:7151`（`bgp_withdraw`）、`bgp_nht.c:801/807/1241`、`bgp_mplsvpn.c:2076/2677/2746/3073`、`bgp_evpn.c:873` |
| `bgp_best_selection` | [bgp_route.c:3592](bgpd/bgp_route.c#L3592) | `bgp_route.c:4518`（`bgp_process_main_one`）、`bgp_evpn.c:1782`、`bgp_evpn_mh.c:253` |
| `bgp_process` | [bgp_route.c:5607](bgpd/bgp_route.c#L5607) | `bgp_damp.c:279/477/607`、`bgp_evpn.c:1712/2157/2847/2931/…`、`bgp_route.c`（各入向/撤销点） |
| `subgroup_announce_check` | [bgp_route.c:2665](bgpd/bgp_route.c#L2665) | `bgp_route.c:4110`（`subgroup_process_announce_selected`）、`bgp_updgrp_adv.c:1286`、`bgp_conditional_adv.c:167` |

**结论**：`bgp_afi_node_get` 是「**所有想往 RIB 里放/取路由的模块**」的统一入口——EVPN、VPN 泄漏（`bgp_mplsvpn.c`）、
下一跳跟踪（`bgp_nht.c`）都经它落地；这些文件分属别的卷，本卷只标注它们是 RIB 的**联动对端**。
排障时若发现某路由「没进 RIB」，先确认它是否真的调了 `bgp_afi_node_get`。

---

## §2 多路径共存与 `bgp_path_info_extra` 懒分配

### 2.1 结论：`extra` 存在的唯一理由是「省内存」

一条路径真正高频用到的字段（`peer`、`attr`、`flags`、`uptime`）就在 `bgp_path_info` 本体里；而**只有极少数路径才需要**的东西——抖动抑制状态、MPLS 标签栈、EVPN 附加信息、VRF 泄漏信息、FlowSpec 数据——全部挪进 `struct bgp_path_info_extra`（[bgp_route.h:265](bgpd/bgp_route.h#L265)）：

```
damp_info          ← 抖动抑制状态（bgp_damp.c）
aggr_suppressors   ← 抑制本路径的聚合项列表
igpmetric          ← 解析下一跳得到的 IGP 度量
labels             ← MPLS 标签/VNI 栈
evpn / vnc / flowspec / vrfleak  ← 各特性专属数据（按需）
```

### 2.2 分配时机与判空模式

- 新建：`bgp_path_info_extra_new()`（[bgp_route.c:383](bgpd/bgp_route.c#L383)）只 `XCALLOC` 零填充，不建子结构。
- 取用（核心）：`bgp_path_info_extra_get()`（[bgp_route.c:474](bgpd/bgp_route.c#L474)）——有则返回，无则新建并挂上。**读代码时看到「总要 `if (pi->extra)` 再取字段」，就是这套懒分配造成的**。
- 释放：`bgp_path_info_extra_free()`（[bgp_route.c:397](bgpd/bgp_route.c#L397)）把各子结构按需释放。
- EVPN 专用入口：`bgp_evpn_path_info_extra_get()`（[bgp_route.c:490](bgpd/bgp_route.c#L490)）。

### 2.3 路径的生命周期（引用计数）

| 动作 | 函数 | 行号 |
|---|---|---|
| 构造 | `info_make()` | [bgp_route.c:5895](bgpd/bgp_route.c#L5895) |
| 挂入 dest（含 pi_hash） | `bgp_path_info_add_with_caller()` | [bgp_route.c:715](bgpd/bgp_route.c#L715) |
| 加/解锁 | `bgp_path_info_lock/unlock` | [bgp_route.c:548](bgpd/bgp_route.c#L548) / [556](bgpd/bgp_route.c#L556) |
| 标记待删 | `bgp_path_info_mark_for_delete()` | [bgp_route.c:806](bgpd/bgp_route.c#L806) |
| 真正摘除（回收节点锁） | `bgp_path_info_reap()` | [bgp_route.c:751](bgpd/bgp_route.c#L751) |
| 恢复（撤销删除标记） | `bgp_path_info_restore()` | [bgp_route.c:819](bgpd/bgp_route.c#L819) |

`bgp_path_info_lock/unlock` 是**路径级的引用计数**（`pi->lock`），与 `bgp_dest` 的锁（在 `rn->lock`，[bgp_table.h:531](bgpd/bgp_table.h#L531)）是两套独立计数，别混。

---

### 2.4 `extra` 的字段明细（实测行号）

| 字段 | 行号 | 何时分配 | 归属子系统 |
|---|---|---|---|
| `damp_info` | [bgp_route.h:267](bgpd/bgp_route.h#L267) | 该路径启用 dampening 时 | `bgp_damp.c` |
| `aggr_suppressors` | [bgp_route.h:270](bgpd/bgp_route.h#L270) | 被聚合项抑制时 | `bgp_route.c` 分区 8 |
| `igpmetric` | [bgp_route.h:273](bgpd/bgp_route.h#L273) | 解析下一跳得到 IGP metric 时 | NHT |
| `labels` | [bgp_route.h:276](bgpd/bgp_route.h#L276) | MPLS/VNI 标签栈 | MPLS/EVPN |
| `bgp_rib_uptime` | [bgp_route.h:279](bgpd/bgp_route.h#L279) | 路径安装进 RIB 的时刻 | 通用 |
| `evpn` | [bgp_route.h:282](bgpd/bgp_route.h#L282) | EVPN 路由 | `bgp_evpn.c` |
| `vnc` | [bgp_route.h:285](bgpd/bgp_route.h#L285) | **仅 `ENABLE_BGP_VNC` 下存在** | VNC |
| `flowspec` | [bgp_route.h:289](bgpd/bgp_route.h#L289) | FlowSpec 路由 | `bgp_flowspec.c` |
| `vrfleak` | [bgp_route.h:292](bgpd/bgp_route.h#L292) | VRF 泄漏 | `bgp_mplsvpn.c` |

**算法直觉**：把上表 8 个字段从 `bgp_path_info` 里挪出去，等于让「一条普通 IPv4 路由」少背 8 个指针
（64 位下 8×8=64 字节）。一张百万前缀表、每前缀平均 2~3 条路径时，省下的是几十 MB 量级——
**这就是 `extra` 存在的全部意义**。

---

## §3 `bgp_table.c`：通用前缀树的包壳

### 3.1 结论：`bgp_table.c` 只做三件事

346 行全部围绕「包壳 + 生命周期」：

1. **锁定/解锁**：`bgp_table_lock/unlock`（[bgp_table.c:55](bgpd/bgp_table.c#L55) / [62](bgpd/bgp_table.c#L62)）。
2. **节点与表的销毁**：`bgp_dest_unlock_node`（[bgp_table.c:132](bgpd/bgp_table.c#L132)）、`bgp_node_destroy`（[bgp_table.c:170](bgpd/bgp_table.c#L170)）。
3. **两个反查工具**：`bgp_dest_get_prefix_str`（[bgp_table.c:109](bgpd/bgp_table.c#L109)）与 `bgp_table_subtree_lookup`（[bgp_table.c:282](bgpd/bgp_table.c#L282)）。

### 3.2 节点锁与销毁：为什么释放发生在「`rn->lock == 1` 这一拍」

`bgp_dest_unlock_node()`（[bgp_table.c:132](bgpd/bgp_table.c#L132)）的逻辑分两段：

```
if (rn->lock == 1) {                 ← 本次解锁后引用计数归零
    bgp_addpath_free_node_data(...)  ← 释放 addpath 节点数据
    bgp_path_info_mpath_free(...)    ← 释放 mpath
    XFREE(MTYPE_BGP_NODE, dest)      ← 释放 bgp_dest 本体
    rn->info = NULL
}
route_unlock_node(rn)                ← 再递减底层 route_node 锁
```

**这是一个「先判后减」的典型**：`rn->lock == 1` 说明「我这一份是最后一份引用」。`bgp_dest` 本体与 `route_node` 本体是两块独立内存（`bgp_dest` 由 BGP 分配，`route_node` 由 lib 分配），所以释放也是两步。

`bgp_node_destroy()`（[bgp_table.c:170](bgpd/bgp_table.c#L170)）是**整表销毁**时由 lib `table.c` 逐节点回调的 `destroy_node`——与 `unlock_node` 的区别在于触发者不同（一个是「引用计数归零」，一个是「表析构」）。两者都在最后释放 `route_node` 本体。

节点回调装配在 `bgp_table_delegate`（[bgp_table.c:210](bgpd/bgp_table.c#L210)）：`create_node = route_node_create`（默认）、`destroy_node = bgp_node_destroy`。

### 3.3 VPN/EVPN 的「两层表」

VPN/ENCAP/EVPN 的 NLRI 是「RD + 前缀」。实现方式不是给前缀加宽，而是**在 RD 外层节点上再挂一棵子表**：

- `bgp_afi_node_get()`（[bgp_route.c:315](bgpd/bgp_route.c#L315)）：对 `SAFI_MPLS_VPN/ENCAP/EVPN`，先按 `prd` 取外层节点 `pdest`，若其 `info` 尚无子表则 `bgp_table_init` 建一棵挂上去（[bgp_route.c:330](bgpd/bgp_route.c#L330)），再在子表里取真正的前缀 `p`，最后 `dest->pdest = pdest`（[bgp_route.c:346](bgpd/bgp_route.c#L346)）。
- 只查不建的版本：`bgp_safi_node_lookup()`（[bgp_route.c:350](bgpd/bgp_route.c#L350)）。

这里 `info` 的多态被用到了极致：**RD 外层节点的 `info` 指向一棵 `bgp_table`**（通过 `bgp_dest_get_bgp_table_info`，[bgp_table.h:508](bgpd/bgp_table.h#L508)）。

### 3.4 inline 容器工具（都在头文件里，逐条实测行号）

| 工具 | 行号 | 作用 |
|---|---|---|
| `bgp_dest_from_rnode` / `to_rnode` | [bgp_table.h:210](bgpd/bgp_table.h#L210) / [220](bgpd/bgp_table.h#L220) | 节点↔外壳互转 |
| `bgp_dest_table` | [bgp_table.h:230](bgpd/bgp_table.h#L230) | 反查所属表 |
| `bgp_dest_parent_nolock` | [bgp_table.h:240](bgpd/bgp_table.h#L240) | 向上找第一个有 info 的祖先 |
| `bgp_table_top` | [bgp_table.h:277](bgpd/bgp_table.h#L277) | 遍历起点 |
| `bgp_route_next` | [bgp_table.h:289](bgpd/bgp_table.h#L289) | 前序/中序遍历下一个（释放当前节点锁） |
| `bgp_route_next_until` | [bgp_table.h:299](bgpd/bgp_table.h#L299) | 限定上界遍历 |
| `bgp_node_get` / `lookup` / `match` | [bgp_table.h:324](bgpd/bgp_table.h#L324) / [344](bgpd/bgp_table.h#L344) / [354](bgpd/bgp_table.h#L354) | 取/查/最长匹配 |
| `bgp_table_count` | [bgp_table.h:362](bgpd/bgp_table.h#L362) | 前缀数 |
| `bgp_table_version` / `next_version` | [bgp_table.h:387](bgpd/bgp_table.h#L387) / [382](bgpd/bgp_table.h#L382) | 版本读写（`bgp_bump_version` 用，[bgp_route.h:723](bgpd/bgp_route.h#L723)） |

`bgp_table_top_nolock`（[bgp_table.h:258](bgpd/bgp_table.h#L258)）与 `bgp_table_top` 的区别只是「是否顺手加锁」，注释里写得很清楚。

---

## §4 `bgp_route.c` 内部地图（本卷亮点）

### 4.1 结论：19317 行被**显式的章节注释块**切成 13 段

源码作者在文件里埋了 13 个 `/* ========== N. … ========== */` 的分区注释（实测行号如下）。这套分区是理解这个巨型文件最快的入口——**先看分区头，再进函数**。

| 分区 | 注释行 | 标题 | 关键函数（实测行） |
|---|---|---|---|
| 0 | [bgp_route.c:124](bgpd/bgp_route.c#L124) | 文件级辅助设施与基础工具 | `bgp_attr_nexthop_same` 131；`bgp_pi_hash_cmp/hashfn` 202/232；`bgp_path_suppressed` 299 |
| 1 | [bgp_route.c:307](bgpd/bgp_route.c#L307) | RIB 路径对象与节点：创建、引用计数、标志 | `bgp_afi_node_get` 315；`extra_get` 474；`info_make` 5895 |
| 2 | [bgp_route.c:982](bgpd/bgp_route.c#L982) | 最佳路径比较 `bgp_path_info_cmp` | `bgp_path_info_cmp_distance` 999；`bgp_path_info_cmp` 1063；`_compatible` 2090 |
| 3 | [bgp_route.c:2113](bgpd/bgp_route.c#L2113) | 入向/出向策略过滤与属性修饰 | `bgp_input_filter` 2119；`bgp_output_filter` 2180；`bgp_input_modifier` 2348；`bgp_output_modifier` 2406 |
| 4 | [bgp_route.c:2648](bgpd/bgp_route.c#L2648) | 出向通告检查与属性加工 | `subgroup_announce_check` 2665 |
| 5 | [bgp_route.c:3578](bgpd/bgp_route.c#L3578) | 最佳路径选择与多路径 | `bgp_best_selection` 3592；`subgroup_process_announce_selected` 4074；`bgp_process_main_one` 4454 |
| 6 | [bgp_route.c:5168](bgpd/bgp_route.c#L5168) | 路由处理队列 meta_queue 与调度 | `meta_queue_process` 5293；`bgp_process` 5607；`bgp_rib_remove` 5804 |
| （入向） | [bgp_route.c:6193](bgpd/bgp_route.c#L6193) | 入向 UPDATE 落地总入口 | `bgp_update` 6210 |
| （撤销） | [bgp_route.c:7124](bgpd/bgp_route.c#L7124) | 入向 UPDATE 撤销入口 | `bgp_withdraw` 7131；`bgp_soft_reconfig_in` 7548；`bgp_clear_route` 7840 |
| 7 | [bgp_route.c:8896](bgpd/bgp_route.c#L8896) | 静态路由（network 命令）与聚合入口 | `bgp_static_update` 8918；`bgp_static_set` 9269 |
| 8 | [bgp_route.c:9839](bgpd/bgp_route.c#L9839) | 路由聚合（aggregate-address） | `bgp_aggregate_route` 10265；`bgp_aggregate_increment` 10855 |
| 9 | [bgp_route.c:11292](bgpd/bgp_route.c#L11292) | 重分发（redistribute） | `bgp_redistribute_add` 11299；`_delete` 11509 |
| 10 | [bgp_route.c:11591](bgpd/bgp_route.c#L11591) | show 命令输出：文本/JSON | `route_vty_out` 11848；`route_vty_out_detail` 12969；`bgp_show_table` 14172 |
| 11 | [bgp_route.c:18452](bgpd/bgp_route.c#L18452) | 管理距离与抖动抑制 | `bgp_distance_apply` 18349；`bgp_damp_set` 18638 |
| 12 | [bgp_route.c:19154](bgpd/bgp_route.c#L19154) | 初始化与收尾 | `bgp_route_init` 19159；`bgp_route_finish` 19308 |

> 注意分区 6 到 7 之间插入的两个「入向/撤销」大注释块（[6193](bgpd/bgp_route.c#L6193)、[7124](bgpd/bgp_route.c#L7124)）用的是 `/* ====…【标题】…==== */` 格式（不是编号格式），所以 `grep '^/\* ==========' ` 抓不到它们——这本身就是一个「注释分区不完全统一」的小坑（见 §19）。

### 4.2 分区之间的**实际调用关系**（不是文件顺序）

文件顺序大致是「结构→选路→过滤→出向→队列→入向→静态→聚合→重分发→show→distance」，但**运行时的调用链与文件顺序相反**：

```
入口 bgp_update(6210) ─⑥→ bgp_process(5607) ─→ [主循环] meta_queue_process(5293)
                                                      │
                                                      ▼
                                          bgp_process_main_one(4454)
                                                      │  bgp_best_selection(3592)
                                                      │      └ bgp_path_info_cmp(1063)
                                                      ▼
                                    subgroup_process_announce_selected(4074)
                                                      │  subgroup_announce_check(2665)
                                                      │      └ bgp_output_filter(2180) / bgp_output_modifier(2406)
                                                      ▼
                                              bgp_zebra_announce/withdraw（卷……外部）
```

- **入向过滤在分区 3**（`bgp_input_filter` 2119、`bgp_input_modifier` 2348），被 `bgp_update` 调用。
- **出向过滤在分区 3+4**（`bgp_output_filter` 2180、`bgp_output_modifier` 2406），被 `subgroup_announce_check` 调用。
- **选路在分区 2+5**（比较器 + 选择器），由 `bgp_process_main_one` 驱动。
- **聚合在分区 8**，通过 `bgp_aggregate_increment/decrement` 被入向/撤销路径调用。

### 4.3 条件编译开关（本文件）

| 开关 | 出现位置 | 说明 |
|---|---|---|
| `ENABLE_BGP_VNC` | [bgp_route.c:108](bgpd/bgp_route.c#L108) 起共 25 处 | VNC（虚拟网络控制）L2VPN 相关，默认不开 |
| `KEEP_OLD_VPN_COMMANDS` | [bgp_route.c:17021](bgpd/bgp_route.c#L17021)、[19208](bgpd/bgp_route.c#L19208)、[19225](bgpd/bgp_route.c#L19225) | 保留旧版 VPN show 命令 |

---

### 4.4 分区 10（show）的内部结构 —— 排障必读

`show` 输出不是一个大函数，而是一组**分工明确的 vty 打印器**（均为实测）：

| 函数 | 行号 | 打印什么 |
|---|---|---|
| `bgp_path_selection_reason2str` | [bgp_route.c:11657](bgpd/bgp_route.c#L11657) | 把 `enum bgp_path_selection_reason` 变成「为何选它」的文字 |
| `route_vty_short_status_out` | [bgp_route.c:11723](bgpd/bgp_route.c#L11723) | 每行行首的状态列（`*>`、`i`、`x`、`d`…） |
| `bgp_nexthop_hostname` | [bgp_route.c:11835](bgpd/bgp_route.c#L11835) | 下一跳反查主机名（较昂贵，谨慎调用） |
| `route_vty_out` | [bgp_route.c:11848](bgpd/bgp_route.c#L11848) | 单条路由的一行（文本或 JSON） |
| `route_vty_out_tmp` | [bgp_route.c:12349](bgpd/bgp_route.c#L12349) | 临时路径（未入库）的打印 |
| `route_vty_out_detail` | [bgp_route.c:12969](bgpd/bgp_route.c#L12969) | `show bgp ... <prefix>` 的**详细块**（含各条路径属性） |
| `route_vty_out_detail_header` | [bgp_route.c:14852](bgpd/bgp_route.c#L14852) | 详细块的表头 |
| `bgp_show_table` | [bgp_route.c:14172](bgpd/bgp_route.c#L14172) | 遍历整表输出（普通 RIB） |
| `bgp_show_table_rd` | [bgp_route.c:14711](bgpd/bgp_route.c#L14711) | 遍历 VPN 外层 RD、再下钻子表 |
| `bgp_route_find_prd_match` | [bgp_route.c:14698](bgpd/bgp_route.c#L14698) | 在 VPN 表里定位 RD |
| `bgp_show` | [bgp_route.c:14761](bgpd/bgp_route.c#L14761) | `show bgp` 总入口（分发到上面） |

**理解要点**：`route_vty_out`（一行）与 `route_vty_out_detail`（一块）是两套代码，前者给 `show bgp`、
后者给 `show bgp <prefix>`——所以同一路由在两种视图下显示内容不一致是**设计如此**，不是 bug。
`bgp_show_table_rd`（[14711](bgpd/bgp_route.c#L14711)）体现了 §3.3 的「两层表」：它先遍历外层 RD，
对每个 RD 再遍历内层前缀。

### 4.5 分区 7/8/9 的入口函数（配置驱动）

| 分区 | 入口函数 | 行号 | 触发场景 |
|---|---|---|---|
| 7 静态/network | `bgp_static_update` | [bgp_route.c:8918](bgpd/bgp_route.c#L8918) | `network` 命令生效/重扫 |
| 7 | `bgp_static_withdraw` | [bgp_route.c:9214](bgpd/bgp_route.c#L9214) | 撤销 network |
| 7 | `bgp_static_set` | [bgp_route.c:9269](bgpd/bgp_route.c#L9269) | `network A.B.C.D/M …` 命令解析 |
| 8 聚合 | `bgp_aggregate_route` | [bgp_route.c:10265](bgpd/bgp_route.c#L10265) | 某前缀的最优路径变化时，尝试生成聚合 |
| 8 | `bgp_add_route_to_aggregate` | [bgp_route.c:10605](bgpd/bgp_route.c#L10605) | 把一条路由并进聚合体的 as_set/团体集合 |
| 8 | `bgp_remove_route_from_aggregate` | [bgp_route.c:10728](bgpd/bgp_route.c#L10728) | 从聚合体移除 |
| 8 | `bgp_aggregate_increment` | [bgp_route.c:10855](bgpd/bgp_route.c#L10855) | 入向计数（被 `bgp_update` 调用） |
| 8 | `bgp_aggregate_decrement` | [bgp_route.c:10898](bgpd/bgp_route.c#L10898) | 撤销计数 |
| 9 重分发 | `bgp_redistribute_add` | [bgp_route.c:11299](bgpd/bgp_route.c#L11299) | zebra 报来一条内核/其它协议路由 |
| 9 | `bgp_redistribute_delete` / `_withdraw` | [11509](bgpd/bgp_route.c#L11509) / [11549](bgpd/bgp_route.c#L11549) | 撤销重分发 |

**结论**：分区 7/8/9 都是「**配置/外部事件驱动的写入口**」，最终都落到 §5 的 `bgp_update` 同类路径
（经 `info_make` 建路径、`bgp_process` 排队）。它们与分区 5/6 是「生产者 vs 处理者」的关系。

---

## §5 入向：`bgp_update` / `bgp_withdraw` / `bgp_rib_remove`

### 5.1 `bgp_update`（[bgp_route.c:6210](bgpd/bgp_route.c#L6210)）

这是把一条 NLRI+属性写进 RIB 的总入口，函数头注释（[bgp_route.c:6193](bgpd/bgp_route.c#L6193)）列了 7 步。要点：

1. **BGP-LU 归一**：`SAFI_LABELED_UNICAST` 归一为 `UNICAST` 处理。
2. **取/建前缀节点**：`bgp_afi_node_get`（[bgp_route.c:315](bgpd/bgp_route.c#L315)），VPN 走 RD 子表（§3.3）。
3. **查旧路径**：`bgp_pi_hash_find(&rib_table->pi_hash, &pi_lookup)`（[bgp_route.c:6292](bgpd/bgp_route.c#L6292)），键含 `(prefix,peer,type,sub_type,addpath_id)`。
4. **一串校验**：AS 环、入向过滤（`bgp_input_filter` 2119）、入向修饰（`bgp_input_modifier` 2348）、无导出判定等，不通过 `goto filtered` 丢弃。
5. **属性 intern + same_attr 判定**。
6. **命中旧路径则更新或撤销重建；未命中则 `info_make`（5895）新建并 `bgp_path_info_add_with_caller`（715）挂入**。
7. **NHT 可达性 → `bgp_update_check_valid_flags`（[bgp_route.c:6100](bgpd/bgp_route.c#L6100)）打 `BGP_PATH_VALID`**，聚合计数增减，最后 `bgp_process` 排入队列。

### 5.2 `bgp_withdraw`（[bgp_route.c:7131](bgpd/bgp_route.c#L7131)）

撤销入口（注释块在 [bgp_route.c:7124](bgpd/bgp_route.c#L7124)）：定位前缀节点 → soft-reconfig 下清 `adj_in` 记录 → 按 `(peer,type,sub_type,addpath)` 找到 pi → 若不在 HISTORY（damping 历史）态，调用 `bgp_rib_withdraw`（[bgp_route.c:5840](bgpd/bgp_route.c#L5840)）真正撤销。

### 5.3 `bgp_rib_remove`（[bgp_route.c:5804](bgpd/bgp_route.c#L5804)）与 `bgp_rib_withdraw`（[bgp_route.c:5840](bgpd/bgp_route.c#L5840)）

- `bgp_rib_withdraw` 是**带 damping 判断**的通用撤销：eBGP 来源若被抑制则保留历史、不做移除；再做 VNC/EVPN 的 un-import/un-export，最后调 `bgp_rib_remove`。
- `bgp_rib_remove` 是**无条件移除**（会话断开等场景）：聚合计数递减 → `bgp_path_info_mark_for_delete`（806）打 `BGP_PATH_REMOVED` 并清 VALID → 若被移除的是当前最优，清 `BGP_NODE_SELECT_DEFER` 并调整 GR 延迟计数 → `bgp_process` 排入队列（**真正摘除延后到处理时**）。

> **设计要点**：`bgp_rib_remove` 只**标记**，不立刻从链表摘。真正回收靠随后的 `bgp_path_info_reap`（[bgp_route.c:751](bgpd/bgp_route.c#L751)）。这是为了避免「处理队列里已排队的 dest/pi 被提前释放」的悬空。

### 5.4 清表与软重配

| 功能 | 函数 | 行号 |
|---|---|---|
| 清某邻居某 afi/safi 的路由 | `bgp_clear_route` | [bgp_route.c:7840](bgpd/bgp_route.c#L7840) |
| 路由节点级清理工作队列 | `bgp_clear_route_node` | [bgp_route.c:7636](bgpd/bgp_route.c#L7636) |
| 批量清（分片，避免长时间阻塞） | `bgp_clear_route_batch` | [bgp_route.c:8320](bgpd/bgp_route.c#L8320) |
| 软重配入口 | `bgp_soft_reconfig_in` | [bgp_route.c:7548](bgpd/bgp_route.c#L7548) |
| 清 adj_in | `bgp_clear_adj_in` | [bgp_route.c:8374](bgpd/bgp_route.c#L8374) |
| 设置/清除 stale（GR 用） | `bgp_set_stale_route` / `bgp_clear_stale_route` | [bgp_route.c:8504](bgpd/bgp_route.c#L8504) / [8410](bgpd/bgp_route.c#L8410) |

`bgp_clear_route_batch`（8320）是**分片清理**：大表一次清完会长时间占用主线程，于是拆成多个 work item 分批（`bgp_clearing_info`，`clear_batch_rib_helper` [bgp_route.c:8186](bgpd/bgp_route.c#L8186)）。

---

### 5.5 入向落地用到的辅助函数（都在分区 1，实测）

`bgp_update` 与 `bgp_withdraw` 内部依赖的分区 1 工具：

| 辅助函数 | 行号 | 作用 |
|---|---|---|
| `bgp_path_info_set_flag` / `unset_flag` | [bgp_route.c:874](bgpd/bgp_route.c#L874) / [890](bgpd/bgp_route.c#L890) | 改路径标志位，并同步调整 dest 级计数（如 `VALID` 变化时改 `bgp->rib` 的计数器），**不要直接写 pi->flags** |
| `bgp_pcount_adjust` | [bgp_route.c:831](bgpd/bgp_route.c#L831) | 维护「dest 下有几条路径」计数 |
| `bgp_path_info_nexthop_changed` | [bgp_route.c:575](bgpd/bgp_route.c#L575) | 判断更新前后下一跳是否变化 |
| `bgp_dest_set_defer_flag` | [bgp_route.c:604](bgpd/bgp_route.c#L604) | 打/清「选路延后」标志（GR） |
| `bgp_label_index_differs` | [bgp_route.c:862](bgpd/bgp_route.c#L862) | MPLS 标签索引是否变化（决定要不要重通告） |
| `bgp_path_info_path_with_addpath_rx_str` | [bgp_route.c:936](bgpd/bgp_route.c#L936) | 打印带 Add-Path Rx id 的路径串（`show` 用） |
| `bgp_get_imported_bpi_ultimate` | [bgp_route.c:966](bgpd/bgp_route.c#L966) | 追到「最终被导入的路径」（VRF 泄漏链） |

### 5.6 与相邻文件的联动（入向阶段）

- **属性解析**：NLRI/属性 TLV 的解码在 `bgp_attr.c`（卷3），`bgp_update` 拿到的是已解析的 `struct attr *`。
- **可达性**：`bgp_update` 末尾调 `bgp_find_or_add_nexthop`（`bgp_nht.c`，卷5）建立/查询下一跳缓存；
  缓存状态变化回调里再 `bgp_process` 重算——这就是「先入库、后判可达、可达后重选路」的两段式。
- **抖动抑制**：被抑制的路径由 `bgp_damp.c` 在抑制期结束时回调 `bgp_process`（`bgp_damp.c:477`）重新参与选路。

---

## §6 选路：`bgp_path_info_cmp` 与 `bgp_best_selection`

### 6.1 `bgp_path_info_cmp`（[bgp_route.c:1063](bgpd/bgp_route.c#L1063)）

这是 RFC4271 §9.1.2.2 的落地，返回 1 表示 `new` 更优。函数头注释（[bgp_route.c:1046](bgpd/bgp_route.c#L1046) 起）列出的比较顺序与 `enum bgp_path_selection_reason`（[bgp_table.h:85](bgpd/bgp_table.h#L85)）**逐项对应**——每步胜出就把 `reason` 设为对应枚举，`show` 时「为何选它」就是读这个字段。

比较顺序（注释原文口径）：

```
weight(大者优) → local-pref(大) → 本地起源相关 → AS_PATH 长度(短) → origin(IGP<EGP<INCOMPLETE)
→ MED(小) → eBGP 优于 iBGP → IGP metric(小) → router-id(小) → cluster-list(短) → 邻居地址(小)
→ multipath 等价判定(*paths_eq=1)
```

**易误解点**（注释明说）：很多「胜出」靠 `!exist` 提前返回；`weight`/`local-pref` 是最高优先级，一旦分出胜负直接跳过后续所有规则。

有个独立的小比较器 `bgp_path_info_cmp_distance`（[bgp_route.c:999](bgpd/bgp_route.c#L999)）：当一条是重分发来的、另一条可安装时，比管理距离，**目的是让 BGP 与 zebra 对「本地路由 vs 其它路由谁被安装」的判断不随到达顺序而变**。

### 6.2 `bgp_best_selection`（[bgp_route.c:3592](bgpd/bgp_route.c#L3592)）

对一个前缀的所有路径排序并选出 best + 多路径。三种模式（注释 [bgp_route.c:3581](bgpd/bgp_route.c#L3581)）：

1. **deterministic-med**：先按「来源 AS 相同」分组，组内选代表再跨组比，使 MED 结果与到达顺序无关。
2. **常规**：两两用 `bgp_path_info_cmp` 排序，维护 `new_select`。
3. **multipath**：等价路径打 `BGP_PATH_MULTIPATH` 并登记到 `mpath` 数组。

复杂度 **O(N²)**（N 为该前缀路径数）。排序完成后路径链表变为有序（清 `BGP_PATH_UNSORTED`）。结果写入 `result->old/new`（`struct bgp_path_info_pair`，[bgp_route.h:445](bgpd/bgp_route.h#L445)），供 `bgp_process_main_one` 做 diff。

### 6.3 `bgp_process_main_one`（[bgp_route.c:4454](bgpd/bgp_route.c#L4454)）

处理队列的单节点主逻辑：对 `dest` 跑选路 → 比较新旧最优 → 变化则：

- 经 `bgp_zebra_has_route_changed`（[bgp_route.c:4187](bgpd/bgp_route.c#L4187)）判断是否要下发；
- `subgroup_process_announce_selected`（[bgp_route.c:4074](bgpd/bgp_route.c#L4074)）触发对邻居的通告。

GR 期间选路会延后：`bgp_do_deferred_path_selection`（[bgp_route.c:5039](bgpd/bgp_route.c#L5039)）、定时器 `bgp_route_select_timer_expire`（[bgp_route.c:3551](bgpd/bgp_route.c#L3551)）。

---

## §7 处理队列 `meta_queue`：延迟批处理

### 7.1 结构（[bgp_route.h:695](bgpd/bgp_route.h#L695)）

```
struct meta_queue {
    STAILQ_HEAD(bgp_dest_queue, bgp_dest) *subq[MQ_SIZE];   ← 3 个子队列
    uint32_t size;
};
```

`MQ_SIZE`＝3（[bgp_route.h:675](bgpd/bgp_route.h#L675)），子队列由 `enum meta_queue_indexes`（[bgp_route.h:720](bgpd/bgp_route.h#L720)）索引：

| 子队列 | 语义 |
|---|---|
| `META_QUEUE_EARLY_ROUTE` | 需优先处理的（SOO/VRF 泄漏相关） |
| `META_QUEUE_OTHER_ROUTE` | 普通路由 |
| `META_QUEUE_EOIU_MARKER` | update-delay 到期的 EOIU 标记 |

**去重机制**：每个 `bgp_dest` 用 flags 里的 MQ 位记录「已排入哪个子队列」，`MQ_BIT_MASK`（[bgp_route.h:678](bgpd/bgp_route.h#L678)）一次性检查/清除，避免同一 dest 重复入队。

### 7.2 `bgp_process` 与去重

`bgp_process()`（[bgp_route.c:5607](bgpd/bgp_route.c#L5607)）转调 `bgp_process_internal()`（[bgp_route.c:5544](bgpd/bgp_route.c#L5544)）：

1. 若传入了 pi，先把它**挪到链表头**并打 `BGP_PATH_UNSORTED`（[bgp_route.c:5553](bgpd/bgp_route.c#L5553)）——因为新路径可能改变排序。
2. 若 dest 已 `BGP_NODE_PROCESS_SCHEDULED` → 直接返回（计数 `node_already_on_queue`）。
3. 若 `BGP_NODE_SELECT_DEFER`（GR 延后）→ 直接返回（计数 `node_deferred_on_queue`）。
4. 否则锁表、置 `BGP_NODE_PROCESS_SCHEDULED`、`bgp_dest_lock_node`、按 early/other 入队。

`bgp_process_early()`（[bgp_route.c:5614](bgpd/bgp_route.c#L5614)）是「本地产生路由」的早处理路径。

### 7.3 EOIU 标记与 `info` 的又一次多态

`bgp_add_eoiu_mark()`（[bgp_route.c:5623](bgpd/bgp_route.c#L5623)）：因为 `meta_queue` 期望所有元素都是 `bgp_dest*`，于是**造一个哑元 dest**承载 `bgp_eoiu_info`，把 `struct bgp *` 存进 `dest->info`（`struct bgp_eoiu_info` 定义在 [bgp_route.h:713](bgpd/bgp_route.h#L713)）。这是 `bgp_dest->info` 多态的又一个实例。

---

## §8 出向：`subgroup_announce_check` 与过滤器边界

### 8.1 `subgroup_announce_check`（[bgp_route.c:2665](bgpd/bgp_route.c#L2665)）

update-group 在把一条路径重新编码发给某个子组前调用：允许通告则填充最终属性到 `attr`、返回 true；否则返回 false 过滤掉。关卡顺序（注释 [bgp_route.c:2650](bgpd/bgp_route.c#L2650) 起）：

```
出向 max-prefix 超限 → 路径须 VALID 且非 HISTORY/REMOVED（非 bestpath 需 addpath 需求）
→ 聚合抑制且无 unsuppress-map → LU/VPN 标签有效性 → 不回发给来源(onlypeer)
→ default-originate 时不重发缺省 → 进入 transparent/下一跳改写/AS_PATH 处理/route-map
```

这是理解出向行为的核心函数，排障对照 `show ip bgp neighbors ... advertised`。

### 8.2 三道出向「过滤器」的分工

| 机制 | 函数/文件 | 匹配对象 | 与 route-map 的关系 |
|---|---|---|---|
| prefix-list / distribute-list / filter-list / route-map 总入口 | `bgp_output_filter` [bgp_route.c:2180](bgpd/bgp_route.c#L2180) | 前缀/AS_PATH | **被 route-map 引用**（`match ip address / match as-path`） |
| 出向属性修饰 | `bgp_output_modifier` [bgp_route.c:2406](bgpd/bgp_route.c#L2406) | 属性 | 出向 route-map 的 `set` 段 |
| 入向对应 | `bgp_input_filter` [bgp_route.c:2119](bgpd/bgp_route.c#L2119) / `bgp_input_modifier` [bgp_route.c:2348](bgpd/bgp_route.c#L2348) | 同上 | 入向 route-map |

**边界规则**（回答本卷核心问题 5）：

- **`bgp_filter.c`（as-path access-list）与 `bgp_clist.c`（community-list）只是「匹配条件的底座」**——它们是 route-map `match` 子句、以及 `neighbor ... filter-list`／`distribute-list` 的实现依赖。
- **route-map 才是「策略编排器」**：决定「匹配哪些条件、按什么顺序、满足后 set 什么/ permit/deny」。access-list / community-list / prefix-list 都只是它的**原子条件**。
- 因此改「AS 路径匹配规则」→ 动 `bgp_filter.c`/`bgp_regex.c`；改「AS 路径过滤在整个策略里放在哪一步」→ 动 route-map（卷6）。

---

## §9 AS_PATH：段表示与线格式编解码

### 9.1 结论：AS_PATH 不是一维数组，而是「段的链表」

`struct aspath`（[bgp_aspath.h:126](bgpd/bgp_aspath.h#L126)）持有一串 `struct assegment`（[bgp_aspath.h:106](bgpd/bgp_aspath.h#L106)）：

```
struct assegment { struct assegment *next; as_t *as; unsigned short length; uint8_t type; };
struct aspath    { refcnt; segments; json; str; str_len; count; asnotation; };
```

- **段类型**（[bgp_aspath.h:57-60](bgpd/bgp_aspath.h#L57)）：`AS_SET`(1) / `AS_SEQUENCE`(2) / `AS_CONFED_SEQUENCE`(3) / `AS_CONFED_SET`(4)。**3/4 是联盟内部段，对外不通告**——所以有 `aspath_count_hops`（[bgp_aspath.c:540](bgpd/bgp_aspath.c#L540)，排除联盟）与 `aspath_count_confeds`（[bgp_aspath.c:521](bgpd/bgp_aspath.c#L521)）并存。`AS_SET` 语义上「算 1 跳」，因为集合不表达顺序。
- **AS 宽度**：内存里统一用 `as_t`（4 字节）存；线上原本 2 字节（RFC4271），RFC6793 扩到 4 字节，过渡期用 `BGP_AS_TRANS`(23456) 占位（[bgp_aspath.h:87](bgpd/bgp_aspath.h#L87)），真值放 `AS4_PATH`，由 `aspath_reconcile_as4`（[bgp_aspath.c:2335](bgpd/bgp_aspath.c#L2335)）对账。
- **边界值坑**（[bgp_aspath.h:83-87](bgpd/bgp_aspath.h#L83)）：`BGP_AS_MAX` **仍是 2 字节上界 65535**（有意保留旧语义），`BGP_AS4_MAX` 才是 4 字节上界；私有 AS 范围 2 字节 64512~65535（[bgp_aspath.h:65](bgpd/bgp_aspath.h#L65)）、4 字节 4200000000~4294967294（[bgp_aspath.h:71](bgpd/bgp_aspath.h#L71)，上界是 …294 不是全 1）。

### 9.2 线格式编解码

| 方向 | 函数 | 行号 | 要点 |
|---|---|---|---|
| 线 → 内存 | `assegments_parse` | [bgp_aspath.c:956](bgpd/bgp_aspath.c#L956) | 重复 `[type(1B) length(1B) as[length]*(2B/4B)]`；`use32bit` 决定 AS 宽度 |
| 线 → 内存（入口） | `aspath_parse` | [bgp_aspath.c:1075](bgpd/bgp_aspath.c#L1075) | 长度须偶数；栈上建骨架 → `assegments_parse` → `hash_get` 命中则释放临时内存 |
| 内存 → 线 | `aspath_put` | [bgp_aspath.c:1212](bgpd/bgp_aspath.c#L1212) | 段内 >255 拆分（`AS_SEGMENT_MAX`=255，[bgp_aspath.c:91](bgpd/bgp_aspath.c#L91)）；相邻可打包段合并 |
| SNMP 专用 | `aspath_snmp_pathseg` | [bgp_aspath.c:1299](bgpd/bgp_aspath.c#L1299) | 用静态 1024 字节 stream，**始终 2 字节 AS** |

段宏：`ASSEGMENT_SIZE`/`ASSEGMENT_LEN`/`ASSEGMENTS_PACKABLE`（[bgp_aspath.c:110](bgpd/bgp_aspath.c#L110) / [114](bgpd/bgp_aspath.c#L114) / [123](bgpd/bgp_aspath.c#L123)）。解析后调 `assegment_normalise`（[bgp_aspath.c:339](bgpd/bgp_aspath.c#L339)）统一内部表示，供哈希去重。

### 9.3 intern 与哈希：AS_PATH 也是共享对象

全局哈希 `ashash`（[bgp_aspath.c:105](bgpd/bgp_aspath.c#L105)），在 `aspath_init`（[bgp_aspath.c:2811](bgpd/bgp_aspath.c#L2811)）用 `hash_create_size(32768, aspath_key_make, aspath_cmp, "BGP AS Path")` 建立。

| 函数 | 行号 | 说明 |
|---|---|---|
| `aspath_key_make` | [bgp_aspath.c:2762](bgpd/bgp_aspath.c#L2762) | **键 = `jhash(str, str_len)`**——注意哈希基于**字符串**，不是段结构 |
| `aspath_cmp` | [bgp_aspath.c:2780](bgpd/bgp_aspath.c#L2780) | 逐段逐 AS 比较；**`asnotation` 不同即视为不等** |
| `aspath_intern` | [bgp_aspath.c:865](bgpd/bgp_aspath.c#L865) | 断言 `refcnt==0 && str!=NULL`，命中则释放入参 |
| `aspath_hash_alloc` | [bgp_aspath.c:922](bgpd/bgp_aspath.c#L922) | 未命中回调，**不做深拷贝**，直接「窃取」临时对象的 `segments/str/json` |
| `aspath_dup` | [bgp_aspath.c:889](bgpd/bgp_aspath.c#L889) | 深拷贝，新对象未 intern、`refcnt=0`——**要改内容就 dup 再改** |
| `aspath_unintern` | [bgp_aspath.c:444](bgpd/bgp_aspath.c#L444) | refcnt 减一，归零才释放 |

**关键一致性约束**：`str` 是缓存，既是显示、又是 **as-path 正则匹配的输入**（[bgp_filter.c:453](bgpd/bgp_filter.c#L453) 把 `aspath` 喂给 `bgp_regexec`，后者读 `aspath->str`）。**凡改动段内容必须调 `aspath_str_update`（[bgp_aspath.c:847](bgpd/bgp_aspath.c#L847)）刷新 `str`**，否则「策略匹配与显示不符」。生成函数是 `aspath_make_str_count`（[bgp_aspath.c:687](bgpd/bgp_aspath.c#L687)）。

### 9.4 主要运算

| 运算 | 函数 | 行号 |
|---|---|---|
| 聚合（公共前缀 → AS_SET） | `aspath_aggregate` | [bgp_aspath.c:1334](bgpd/bgp_aspath.c#L1334) |
| 前置（`set as-path prepend` 底座） | `aspath_prepend` | [bgp_aspath.c:1861](bgpd/bgp_aspath.c#L1861) |
| 追加 N 个（一趟完成多跳） | `aspath_add_seq_n` | [bgp_aspath.c:2253](bgpd/bgp_aspath.c#L2253) |
| 添加联盟序列段 | `aspath_add_confed_seq` | [bgp_aspath.c:2527](bgpd/bgp_aspath.c#L2527) |
| 防环 | `aspath_loop_check` / `_confed` | [bgp_aspath.c:1488](bgpd/bgp_aspath.c#L1488) / [1515](bgpd/bgp_aspath.c#L1515) |
| 私有 AS 检查/移除 | `aspath_private_as_check` / `aspath_remove_private_asns` | [bgp_aspath.c:1542](bgpd/bgp_aspath.c#L1542) / [1704](bgpd/bgp_aspath.c#L1704) |
| 剔除指定 AS（`set as-path exclude`） | `aspath_filter_exclude` | [bgp_aspath.c:2012](bgpd/bgp_aspath.c#L2012) |
| 字符串 → 对象（CLI 入口） | `aspath_str2aspath` | [bgp_aspath.c:2691](bgpd/bgp_aspath.c#L2691) |
| AS4 对账 | `aspath_reconcile_as4` | [bgp_aspath.c:2335](bgpd/bgp_aspath.c#L2335) |

`struct aspath_exclude`（[bgp_aspath.h:163](bgpd/bgp_aspath.h#L163)）支撑 `set as-path exclude`：`exclude_all` 与 `exclude_aspath_acl` 是两条不同代码路径；孤儿机制 `as_exclude_set_orphan`（[bgp_aspath.c:1963](bgpd/bgp_aspath.c#L1963)）处理「宿主 as-list 已被删」的悬空配置。

---

### 9.5 `str` / `json` 缓存与 `asnotation`（实测）

`aspath_make_str_count`（[bgp_aspath.c:687](bgpd/bgp_aspath.c#L687)）同时负责生成：
- `str`（纯文本）与 `json`（JSON 数组），由 `make_json` 参数决定是否顺手建 JSON；
- `str_len`、`count`（AS 个数）。

**`asnotation` 的坑**：`asnotation` 是 `enum asnotation_mode`，决定 4 字节 AS 在 `str` 里写成
「点分」（`1.2`）还是「点分加」（`1.2.3`）还是纯数字。它对 `str` 内容有直接影响，所以：
- `aspath_cmp`（[bgp_aspath.c:2780](bgpd/bgp_aspath.c#L2780)）**把 asnotation 纳入相等性判断**；
- 同一 AS_PATH 若两台设备 asnotation 不同，字符串不同 → 正则匹配结果可不同。

段类型的人类可读名在 `aspath_segment_type_str[]`（[bgp_aspath.c:173](bgpd/bgp_aspath.c#L173)），
供 `show` 与 JSON 输出。

其它与 str/count 相关的工具（实测）：`aspath_size`（[bgp_aspath.c:611](bgpd/bgp_aspath.c#L611)）、
`aspath_highest`（[626](bgpd/bgp_aspath.c#L626)）、`aspath_leftmost`（[647](bgpd/bgp_aspath.c#L647)）、
`aspath_has_as4`（[661](bgpd/bgp_aspath.c#L661)）、`aspath_check_as_sets`（[569](bgpd/bgp_aspath.c#L569)）、
`aspath_check_as_zero`（[587](bgpd/bgp_aspath.c#L587)）。

### 9.6 段的内存所有权

- `assegment_data_new`（[bgp_aspath.c:159](bgpd/bgp_aspath.c#L159)）分配 `as_t` 数组；`assegment_free`
  （[205](bgpd/bgp_aspath.c#L205)）释放单个段；`assegment_free_all`（[219](bgpd/bgp_aspath.c#L219)）释放整链。
- `aspath_free`（[403](bgpd/bgp_aspath.c#L403)）只释放 `aspath` 本体的 `str/json/segments`；
  **它不会去 unintern**——因为 intern 过的对象生命周期由哈希表管。
- `aspath_unintern`（[444](bgpd/bgp_aspath.c#L444)）签名是 `void aspath_unintern(struct aspath **aspath)`——
  **传二级指针**，释放后把调用方的指针置 NULL，正是为了防「悬空继续用」（呼应 §15 陷阱 15）。

---

## §10 团体三兄弟：内存表示与驻留

### 10.1 统一范式：全局哈希 + 引用计数

三者的驻留逻辑**同构**（这是本卷最该记的「一个模式看三遍」）：

```
新对象(refcnt=0) → community_uniq_sort 去重排序 → hash_get(全局表)
   命中 → 释放入参，返回表内对象，refcnt++
   未命中 → 表内新建，refcnt=1
unintern → refcnt--，归零则 hash_release + free，指针置 NULL
```

| 维度 | 标准团体 | 扩展团体 | 大团体 |
|---|---|---|---|
| 结构 | `struct community` [bgp_community.h:51](bgpd/bgp_community.h#L51) | `struct ecommunity` [bgp_ecommunity.h:209](bgpd/bgp_ecommunity.h#L209) | `struct lcommunity` [bgp_lcommunity.h:55](bgpd/bgp_lcommunity.h#L55) |
| 单元 | 4 字节（`COMMUNITY_SIZE`，[bgp_community.h:94](bgpd/bgp_community.h#L94)） | 8 或 20 字节（`unit_size` 字段区分，[bgp_ecommunity.h:179](bgpd/bgp_ecommunity.h#L179)/[180](bgpd/bgp_ecommunity.h#L180)） | 12 字节（`LCOMMUNITY_SIZE`，[bgp_lcommunity.h:48](bgpd/bgp_lcommunity.h#L48)） |
| `size` 语义 | 值个数（字节=size×4） | 单元个数（字节=size×unit_size） | 值个数（字节=size×12） |
| 全局表 | `comhash` [bgp_community.c:58](bgpd/bgp_community.c#L58) | `ecomhash` [bgp_ecommunity.c:76](bgpd/bgp_ecommunity.c#L76) | `lcomhash` [bgp_lcommunity.c:50](bgpd/bgp_lcommunity.c#L50) |
| 哈希函数 | `community_hash_make` [bgp_community.c:638](bgpd/bgp_community.c#L638) | `ecommunity_hash_make` [bgp_ecommunity.c:409](bgpd/bgp_ecommunity.c#L409) | `lcommunity_hash_make` [bgp_lcommunity.c:365](bgpd/bgp_lcommunity.c#L365) |
| 比较 | `community_cmp` [bgp_community.c:678](bgpd/bgp_community.c#L678) | `ecommunity_cmp` [bgp_ecommunity.c:419](bgpd/bgp_ecommunity.c#L419) | `lcommunity_cmp` [bgp_lcommunity.c:375](bgpd/bgp_lcommunity.c#L375) |
| intern / unintern | [525](bgpd/bgp_community.c#L525) / [554](bgpd/bgp_community.c#L554) | [367](bgpd/bgp_ecommunity.c#L367) / [387](bgpd/bgp_ecommunity.c#L387) | [303](bgpd/bgp_lcommunity.c#L303) / [324](bgpd/bgp_lcommunity.c#L324) |
| init / finish | [1001](bgpd/bgp_community.c#L1001) / [1018](bgpd/bgp_community.c#L1018) | [465](bgpd/bgp_ecommunity.c#L465) / [472](bgpd/bgp_ecommunity.c#L472) | [399](bgpd/bgp_lcommunity.c#L399) / [406](bgpd/bgp_lcommunity.c#L406) |

哈希种子**各自不同**（community 用 `jhash2`、ecommunity `0x564321ab`、lcommunity `0xab125423`），且**跨进程/跨版本不可比**，不要持久化。

### 10.2 标准团体（RFC1997，4 字节 "AS:VALUE"）

- 结构（[bgp_community.h:51](bgpd/bgp_community.h#L51)）：`val` 是网络字节序 4 字节数组，**升序去重排序**（`community_uniq_sort`，[bgp_community.c:212](bgpd/bgp_community.c#L212)），因此可直接 `memcmp` 比较。
- well-known 常量（[bgp_community.h:78-92](bgpd/bgp_community.h#L78)）：0xFFFF0000 段（graceful-shutdown/accept-own…）与 0xFFFFFFxx 段（no-export/no-advertise/local-AS）。**坑**：`COMMUNITY_LOCAL_AS` 与 `COMMUNITY_NO_EXPORT_SUBCONFED` 同为 `0xFFFFFF03`（[bgp_community.h:90](bgpd/bgp_community.h#L90)），同一线值在不同语境打印成不同名。
- 字符串互转：`community_gettoken`（[bgp_community.c:767](bgpd/bgp_community.c#L767)，字母开头匹配 well-known 关键字，数字开头解析 `AS:VAL`）、`community_str2com`（[bgp_community.c:940](bgpd/bgp_community.c#L940)）、`set_community_string`（[bgp_community.c:262](bgpd/bgp_community.c#L262)，先估长度再一次性分配，`translate_alias` 为真时翻译别名）。
- 内联 `community_strip_accept_own`（[bgp_community.h:164](bgpd/bgp_community.h#L164)）：向 VRF 传播时剔除 ACCEPT_OWN——注意 `community_del_val` 需**网络字节序**（先 `htonl`）。

### 10.3 扩展团体（RFC4360 等，8 字节 = 2B type + 6B value）

- type 高序字节决定编码空间（[bgp_ecommunity.h:78-93](bgpd/bgp_ecommunity.h#L78)）：`0x00` AS / `0x01` IPv4 / `0x02` AS4 / `0x03` opaque / `0x06` EVPN / `0x08` FlowSpec redirect；**`0x40` 位是非传递标志**（或进高序字节）。
- 低序字节是 subtype（[bgp_ecommunity.h:104-120](bgpd/bgp_ecommunity.h#L104)）：`0x02` RT、`0x03` SoO、`0x04` link-bandwidth、`0x0b` color…**判断「是不是 RT」看 subtype，判断「怎么取 AS/IP」才看高序字节**。
- 编码器是头文件里的 inline 函数（避免到处手写字节）；实测：

| 编码器 | 行号 | 产出 |
|---|---|---|
| `encode_route_target_as` | [bgp_ecommunity.h:274](bgpd/bgp_ecommunity.h#L274) | RT 2B AS:值（type 0x00） |
| `encode_route_target_ip` | [bgp_ecommunity.h:294](bgpd/bgp_ecommunity.h#L294) | RT IP:值（type 0x01） |
| `encode_route_target_as4` | [bgp_ecommunity.h:311](bgpd/bgp_ecommunity.h#L311) | RT 4B AS:值（type 0x02） |
| `encode_lb_extcomm` | [bgp_ecommunity.h:349](bgpd/bgp_ecommunity.h#L349) | link-bandwidth（可 IEEE-754 或整数） |
| `encode_lb_extended_extcomm` | [bgp_ecommunity.h:377](bgpd/bgp_ecommunity.h#L377) | extended-link-bandwidth（20 字节） |

- 解析：`ecommunity_gettoken`（[bgp_ecommunity.c:669](bgpd/bgp_ecommunity.c#L669)）、`ecommunity_str2com`（[bgp_ecommunity.c:1044](bgpd/bgp_ecommunity.c#L1044)，`_ipv6` 版 [1052](bgpd/bgp_ecommunity.c#L1052)）、`ecommunity_encode_internal`（[bgp_ecommunity.c:596](bgpd/bgp_ecommunity.c#L596)）。
- 打印：`_ecommunity_ecom2str`（[bgp_ecommunity.c:1294](bgpd/bgp_ecommunity.c#L1294)）——**同一份数据有 3 种格式**（ROUTE_MAP / COMMUNITY_LIST / DISPLAY，[bgp_ecommunity.h:171-173](bgpd/bgp_ecommunity.h#L171)），这是历史遗留、也是 `show` 与 rt 字符串不一致的根源。返回约定：`ecommunity_str` 返回内部缓存不可 free；`ecommunity_ecom2str` 返回新串需 `ecommunity_strfree`（[bgp_ecommunity.c:91](bgpd/bgp_ecommunity.c#L91)）。
- **传递性操作**（直接对应问题 4）：`ecommunity_non_transitive`（[bgp_ecommunity.c:1820](bgpd/bgp_ecommunity.c#L1820)）判定一个 type 字节是否非传递；`ecommunity_strip_non_transitive`（[bgp_ecommunity.c:1829](bgpd/bgp_ecommunity.c#L1829)）删除所有非传递扩展团体——用于「向 eBGP 邻居发送前剥掉非传递属性」的场景。

### 10.4 大团体（RFC8092，12 字节 = 3×4B "Global:Local1:Local2"）

- 结构（[bgp_lcommunity.h:55](bgpd/bgp_lcommunity.h#L55)）：`val` 是连续 12 字节值数组；`struct lcommunity_val`（[bgp_lcommunity.h:74](bgpd/bgp_lcommunity.h#L74)）是裸 12 字节容器。
- 三个字段都是完整 32 位——**这正是大团体存在的理由**：标准团体的 2 字节 AS 放不下 4 字节 ASN。
- 解析：`lcommunity_gettoken`（[bgp_lcommunity.c:419](bgpd/bgp_lcommunity.c#L419)，手工按 `:` 切三个 32 位，**断言恰好两个分隔符**）、`lcommunity_str2com`（[bgp_lcommunity.c:498](bgpd/bgp_lcommunity.c#L498)，先 `lcommunity_list_valid` 校验）。
- 插入排序：`lcommunity_add_val`（[bgp_lcommunity.c:89](bgpd/bgp_lcommunity.c#L89)）边比边插、遇重复返回 false，实现「有序去重」。
- 哈希用 `jhash`（非 `jhash2`），比较用 `memcmp`（大端天然按数值排序）。

### 10.5 聚合共享部分

三者的 `bgp_compute_aggregate_*` / `bgp_remove_*_from_aggregate` 系列服务于 `aggregate-address` 的 as_set/团体汇总（`struct bgp_aggregate` 的 `community_hash`/`ecommunity_hash`/`lcommunity_hash` 字段，[bgp_route.h:549](bgpd/bgp_route.h#L549) 起）。汇总逻辑本体在 §4 分区 8。

---

### 10.6 标准团体的完整操作面（实测行号）

以标准团体为例列出全部操作，便于对照扩展/大团体的同名函数：

| 操作 | 函数 | 行号 |
|---|---|---|
| 新建 / 释放 | `community_new` / `community_free` | [64](bgpd/bgp_community.c#L64) / [73](bgpd/bgp_community.c#L73) |
| 增 / 删一个值 | `community_add_val` / `community_del_val` | [93](bgpd/bgp_community.c#L93) / [107](bgpd/bgp_community.c#L107) |
| 集合差（删除） | `community_delete` | [141](bgpd/bgp_community.c#L141) |
| 排序比较子 | `community_compare` | [158](bgpd/bgp_community.c#L158) |
| 包含判定 | `community_include` / `community_val_get` | [178](bgpd/bgp_community.c#L178) / [193](bgpd/bgp_community.c#L193) |
| 去重排序 | `community_uniq_sort` | [212](bgpd/bgp_community.c#L212) |
| 从线数据解析 | `community_parse` | [579](bgpd/bgp_community.c#L579) |
| 深拷贝（未 intern） | `community_dup` | [600](bgpd/bgp_community.c#L600) |
| 转字符串 | `community_str` | [620](bgpd/bgp_community.c#L620) |
| 相等 / 匹配 | `community_match` / `community_cmp` | [648](bgpd/bgp_community.c#L648) / [678](bgpd/bgp_community.c#L678) |
| 合并 | `community_merge` | [696](bgpd/bgp_community.c#L696) |
| 全局计数 / 取表 | `community_count` / `community_hash` | [987](bgpd/bgp_community.c#L987) / [994](bgpd/bgp_community.c#L994) |
| 聚合支持 | `bgp_compute_aggregate_community*` | [1067](bgpd/bgp_community.c#L1067) / [1078](bgpd/bgp_community.c#L1078) / [1110](bgpd/bgp_community.c#L1110) |
| 聚合移除 | `bgp_remove_community_from_aggregate` | [1138](bgpd/bgp_community.c#L1138) |

**联动对端**：`bgp_clist.c`（匹配）、`bgp_route.c` 分区 8（聚合汇总）、`bgp_route.c` 分区 10（show）、
`bgp_community_alias.c`（别名翻译）。

---

## §11 RD：8 字节的前缀区分符

### 11.1 结论：RD 只答「这是哪条路由」，不答「谁能收」

`bgp_rd.h` 文件头（[bgp_rd.h:9-14](bgpd/bgp_rd.h#L9)）把这一点说得很重：RD 回答**身份**，RT（Route Target，扩展团体）回答**策略**。VPN 里不同 VRF 可能有完全相同的前缀（都是 10.0.0.0/24），靠前缀前拼 8 字节 RD 形成全局唯一标识。

### 11.2 三种编码格式（[bgp_rd.h:16-24](bgpd/bgp_rd.h#L16)）

8 字节 RD 前 2 字节是 type：

| type | 宏 | 布局 | 长度权衡 |
|---|---|---|---|
| 0 | `RD_TYPE_AS`（[bgp_rd.h:47](bgpd/bgp_rd.h#L47)） | 2B AS + 4B 编号 | AS 只有 2 字节 |
| 1 | `RD_TYPE_IP`（[bgp_rd.h:48](bgpd/bgp_rd.h#L48)） | 4B IPv4 + 2B 编号 | 用 router-id 天然避重，最推荐 |
| 2 | `RD_TYPE_AS4`（[bgp_rd.h:49](bgpd/bgp_rd.h#L49)） | 4B AS + 2B 编号 | 编号被压到 2 字节 |

`RD_TYPE_UNDEFINED`(-1)（[bgp_rd.h:46](bgpd/bgp_rd.h#L46)）是进程内「未设置」哨兵，不在线上。`RD_BYTES`=8、`RD_ADDRSTRLEN`=28（[bgp_rd.h:60-61](bgpd/bgp_rd.h#L60)）。

**为什么三个解码函数并存**：三者长度都 8 字节但字段宽度不同，必须按 type 分派（[bgp_rd.h:23-24](bgpd/bgp_rd.h#L23)）——**别拿错**：`decode_rd_as`（[bgp_rd.c:89](bgpd/bgp_rd.c#L89)）／`decode_rd_as4`（[bgp_rd.c:101](bgpd/bgp_rd.c#L101)）／`decode_rd_ip`（[bgp_rd.c:111](bgpd/bgp_rd.c#L111)）。类型读写单独提供：`decode_rd_type`（[bgp_rd.c:58](bgpd/bgp_rd.c#L58)）／`encode_rd_type`（[bgp_rd.c:77](bgpd/bgp_rd.c#L77)）。

### 11.3 字符串互转与自动生成

- `str2prefix_rd`（[bgp_rd.c:153](bgpd/bgp_rd.c#L153)）：解析 `"65001:100"`、`"10.0.0.1:5"`。**返回值约定坑**：成功返回 **1**，失败返回 0——不是「0 成功」（见 §19）。允许第二个 `:` 指定显式 type。
- `prefix_rd2str`（[bgp_rd.c:234](bgpd/bgp_rd.c#L234)）：按 type 分派打印；type 0/2 用 `ASN_FORMAT(asnotation)`（跟随用户的点分/点分加/纯数字写法），type 1 用 `%pI4:val`。
- `form_auto_rd`（[bgp_rd.c:286](bgpd/bgp_rd.c#L286)）：实现 `rd auto`，把 router-id 与本地编号拼成 type 1 RD，**忽略 `str2prefix_rd` 返回值**（按输入一定合法假设）。
- `%pRD` 家族（[bgp_rd.h:67-75](bgpd/bgp_rd.h#L67)）：`%pRDD`(点分) / `%pRDE`(点分加) / `%pRDP`(纯数字)，由 printfrr 扩展实现（注册见 [bgp_rd.c:319-336](bgpd/bgp_rd.c#L319)）。`_SPACE` 版本多一个左对齐宽度 21（表格对齐）。
- 条件编译：`RD_TYPE_VNC_ETH`(0xff00) 与 `decode_rd_vnc_eth`（[bgp_rd.c:125](bgpd/bgp_rd.c#L125)）**仅在 `ENABLE_BGP_VNC` 下存在**（[bgp_rd.h:53](bgpd/bgp_rd.h#L53)、[bgp_rd.c:122](bgpd/bgp_rd.c#L122)）。

---

### 11.4 解码结果结构（实测字段行号）

`decode_rd_as/as4/ip` 不分配内存，只填调用方给的结构体（[bgp_rd.c:119-121](bgpd/bgp_rd.c#L119)）：

| 结构 | 字段 | 行号 |
|---|---|---|
| `struct rd_as`（type 0/2 共用） | `type` / `as` / `val` | [bgp_rd.h:81](bgpd/bgp_rd.h#L81) / [82](bgpd/bgp_rd.h#L82) / [83](bgpd/bgp_rd.h#L83) |
| `struct rd_ip`（type 1） | `type` / `val` / `ip` | [bgp_rd.h:89](bgpd/bgp_rd.h#L89) / [90](bgpd/bgp_rd.h#L90) / [91](bgpd/bgp_rd.h#L91) |
| `struct rd_vnc_eth`（`ENABLE_BGP_VNC`） | `type` / `local_nve_id` / `macaddr` | [bgp_rd.h:99](bgpd/bgp_rd.h#L99) / [100](bgpd/bgp_rd.h#L100) / [101](bgpd/bgp_rd.h#L101) |

**坑**：`struct rd_as` 被 type 0（`as` 低 16 位 + 4 字节 `val`）与 type 2（4 字节 `as` + 2 字节 `val`）**共用**，
字段位宽由函数按类型自行解释——**调用方不要拿 `rd_as.val` 的位宽硬套**（[bgp_rd.h:115-116](bgpd/bgp_rd.h#L115) 注释明说）。
`rd_vnc_eth` 虽也 8 字节，但布局是 `2B type + 1B NVE + 6B MAC`，与标准三种完全不同，必须专门解码。

**`str2prefix_rd` 的已知解析反常**（源码注释 [bgp_rd.c:147-152](bgpd/bgp_rd.c#L147) 主动标注）：走「两个 `:`」的
分支时 `p = p2 + 1` 后 `all_digit(p + 1)`/`atol(p + 1)` 又各多前进一位，等于**从值字段的第 2 个字符起取值**
（例 `"0:65001:100"` 会把 `"00"` 当编号）。这是源码内自陈的实现疑点，详见 §19。

---

## §12 属性标志位：关键/非关键、传递/非传递

### 12.1 结论：「关键/传递」是**线上 TLV 的类型位**，在代码里体现为两条独立线索

RFC4271 把所有路径属性按两个维度分类：**well-known/optional（关键/可选）** 与 **transitive/non-transitive（传递/非传递）**。在 FRR 里这**不是一个字段**，而是分两处体现：

1. **线上 TLV 的 flags 字节**（定义在 `bgp_attr.h`，**详见卷3**，实测锚点：`BGP_ATTR_FLAG_OPTIONAL` 0x80 [bgp_attr.h:61](bgpd/bgp_attr.h#L61)、`BGP_ATTR_FLAG_TRANS` 0x40 [bgp_attr.h:62](bgpd/bgp_attr.h#L62)、`BGP_ATTR_FLAG_PARTIAL` 0x20 [bgp_attr.h:63](bgpd/bgp_attr.h#L63)、`BGP_ATTR_FLAG_EXTLEN` 0x10 [bgp_attr.h:64](bgpd/bgp_attr.h#L64)）。解析/编码时据此决定「未知属性是否丢弃」「是否原样转发」。

2. **属性值自身的类型编码**——这是本卷范围内最直接的体现。以扩展团体为例：`0x40` 位就是**非传递标志**，且某些取值（`0x41`、`0x43`）本身已含该位：

   - 常量 `ECOMMUNITY_FLAG_NON_TRANSITIVE`＝`0x40`（[bgp_ecommunity.h:194](bgpd/bgp_ecommunity.h#L194)）；
   - `ECOMMUNITY_ENCODE_IP_NON_TRANS`＝`0x41`、`ECOMMUNITY_ENCODE_OPAQUE_NON_TRANS`＝`0x43`（[bgp_ecommunity.h:92-93](bgpd/bgp_ecommunity.h#L92)）；
   - 判定函数 `ecommunity_non_transitive()`（[bgp_ecommunity.c:1820](bgpd/bgp_ecommunity.c#L1820)）**先 `CHECK_FLAG(type, 0x40)` 再看是否等于 0x41/0x43**；
   - 批量剥除 `ecommunity_strip_non_transitive()`（[bgp_ecommunity.c:1829](bgpd/bgp_ecommunity.c#L1829)）——这是「向 eBGP 发之前剥非传递属性」的落地；
   - 编码时设置该位：`encode_route_target_as/ip/as4`（[bgp_ecommunity.h:274](bgpd/bgp_ecommunity.h#L274) 等）在 `trans == false` 时 `val[0] |= ECOMMUNITY_FLAG_NON_TRANSITIVE`。

### 12.2 well-known（关键）团体值是「有名字的保留值」

标准团体的 well-known 值不是靠 flags 区分，而是靠**高 16 位为 0xFFFF 的保留区间**——见 §10.2 的常量表。`set_community_string`（[bgp_community.c:262](bgpd/bgp_community.c#L262)）对这批值打印固定关键字（`no-export`/`no-advertise`/`graceful-shutdown`…），其余打印 `AS:VAL`。这就是「关键属性值的人类可读映射」。

### 12.3 `attr->flag`：另一张「本路径携带了哪些属性」的位图

注意区分：`bgp_attr.h` 里还有一个**进程内的**位图 `attr->flag`，用 `ATTR_FLAG_BIT(BGP_ATTR_xxx)` 标记「本路径携带了哪些属性」（宏定义 [bgp_attr.h:412](bgpd/bgp_attr.h#L412)，判断宏 `bgp_attr_exists` [bgp_attr.h:415](bgpd/bgp_attr.h#L415)）。它**不是**线上 TLV 的 flags 字节，而是「已解析出哪些属性」的备忘——详见卷3。

> **一句话总结问题 4**：线上「关键/传递」是 TLV 的 flags 字节（`bgp_attr.h`，卷3）；属性值内部的「非传递」是扩展团体自身的 type `0x40` 位（`bgp_ecommunity.*`，本卷）；而「本路径有哪些属性」是 `attr->flag` 位图（`bgp_attr.h`，卷3）。三张不同的「位图/字节」，不要混。

---

## §13 过滤器家族：`bgp_clist` / `bgp_filter` / `bgp_regex`

三者是一条链：**route-map 想按团体/AS 路径匹配 → 调用 community-list / as-path access-list → 后者用正则（`bgp_regex`）或集合比较**。

### 13.1 `bgp_regex.c`：AS_PATH 正则的「`_` → 边界」翻译层

- **它解决的坑**：配置里 `_` 是特殊符号，代表 AS 边界（`^`、`$` 或 `[,{}() ]`）。`bgp_regcomp`（[bgp_regex.c:64](bgpd/bgp_regex.c#L64)）先数 `_` 的个数，再把每个 `_` 替换成 `"(^|[,{}() ]|$)"`（[bgp_regex.c:71](bgpd/bgp_regex.c#L71)），然后 `regcomp(..., REG_EXTENDED | REG_NOSUB)`。
- 匹配对象是 `aspath->str`（[bgp_regex.c:114](bgpd/bgp_regex.c#L114)）——**所以改了 aspath 的 str 就改了正则匹配结果**（呼应 §9.3）。
- **★ 反直觉约定**：`bgp_regexec` 返回值沿用 `regexec` 原始语义：**0 = 匹配成功，非 0 = 不匹配**（[bgp_regex.c:112](bgpd/bgp_regex.c#L112)、[bgp_regex.h:36](bgpd/bgp_regex.h#L36)）。读 `if (bgp_regexec(...))` 必须按「非 0 是不命中」理解。
- 释放 `bgp_regex_free`（[bgp_regex.c:119](bgpd/bgp_regex.c#L119)）与 `bgp_regcomp` 成对，否则泄漏。

### 13.2 `bgp_filter.c`：as-path access-list

- **数据结构**：`struct as_list`（[bgp_filter.h:84](bgpd/bgp_filter.h#L84)）＝一条命名访问列表；`struct as_filter`（[bgp_filter.h:59](bgpd/bgp_filter.h#L59)）＝一条规则（`seq` + `type` + `reg`/`reg_str`）。**头文件里的 `exclude_rule`（[bgp_filter.h:94](bgpd/bgp_filter.h#L94)）用 DLIST，普通规则用 head/tail 手工链表，两者分开存放，别混遍历**。
- **只有字符串名，没有数字名**——这与 community-list 不同（文件头导读明说，[bgp_filter.c:20-24](bgpd/bgp_filter.c#L20)）。全局单例 `as_list_master`（[bgp_filter.c:68](bgpd/bgp_filter.c#L68)）只维护 `str` 链表 + 两个 hook。
- **匹配主循环** `as_list_apply`（[bgp_filter.c:453](bgpd/bgp_filter.c#L453)）：按 seq 序遍历、逐条 `as_filter_match`（[bgp_filter.c:442](bgpd/bgp_filter.c#L442)，内部 `bgp_regexec != REG_NOMATCH`）；**首条命中即返回其 type**；`aslist==NULL` 或全不命中 → `AS_FILTER_DENY`（**未命中默认拒绝**）。
- seq 自动分配 `bgp_alist_new_seq_get`（[bgp_filter.c:89](bgpd/bgp_filter.c#L89)）：取当前最大 seq 向上取整到 5 的倍数再加 5（5,10,15…）。哨兵 `ASPATH_SEQ_NUMBER_AUTO`＝-1（[bgp_filter.h:42](bgpd/bgp_filter.h#L42)），所以 seq 类型是 `int64_t`，不能用无符号。
- **配置前预校验** `config_bgp_aspath_validate`（[bgp_filter.c:505](bgpd/bgp_filter.c#L505)）：**字符白名单粗筛**（`"1234567890_^|[,{}() ]$*+.?-\\"`），不是语法校验；真正合法性由 `bgp_regcomp` 能否编译决定。
- 命令 `bgp_as_path_cmd`（[bgp_filter.c:523](bgpd/bgp_filter.c#L523)）／`no_bgp_as_path_cmd`（[bgp_filter.c:618](bgpd/bgp_filter.c#L618)）；生命周期 `bgp_filter_init`（[bgp_filter.c:919](bgpd/bgp_filter.c#L919)）／`bgp_filter_reset`（[bgp_filter.c:938](bgpd/bgp_filter.c#L938)）。

### 13.3 `bgp_clist.c`：三套 community-list

- **两个正交维度**（[bgp_clist.h:20-29](bgpd/bgp_clist.h#L20)）：种类（`COMMUNITY_LIST_MASTER`/`EXTCOMMUNITY_LIST_MASTER`/`LARGE_COMMUNITY_LIST_MASTER`，[bgp_clist.h:54-56](bgpd/bgp_clist.h#L54)）× 写法（standard/expanded）。相乘得**六种 style 扁平编号 0~5**（[bgp_clist.h:87-92](bgpd/bgp_clist.h#L87)）——**0 是有效值**，用 0 判空会出错。
- **每个 master**（[bgp_clist.h:179](bgpd/bgp_clist.h#L179)）= `num` 链表 + `str` 链表 + `hash` 索引三件套；总句柄 `community_list_handler`（[bgp_clist.h:191](bgpd/bgp_clist.h#L191)）把三个 master 打包，全局单例 `bgp_clist`（[bgp_clist.h:213](bgpd/bgp_clist.h#L213)）。
- **条目** `community_entry`（[bgp_clist.h:135](bgpd/bgp_clist.h#L135)）：`direct`(permit=1/deny=0)、`style`、`seq`、`u`(三合一 union)、`config`(回显原文)、`reg`(expanded 正则)。**注意 `COMMUNITY_DENY`＝0**（[bgp_clist.h:62](bgpd/bgp_clist.h#L62)），即「清空后忘了设 direct」会默认 deny（安全默认，但也是坑）。
- **六种匹配语义**（[bgp_clist.h:267-307](bgpd/bgp_clist.h#L267)）——**最需要分清的地方**：

| 语义 | 函数（community 例） | 行号 | 含义 |
|---|---|---|---|
| 常规 match | `community_list_match` | [bgp_clist.c:815](bgpd/bgp_clist.c#L815) | 整体命中规则，返回该条目 permit 结论 |
| exact_match | `community_list_exact_match` | [bgp_clist.c:916](bgpd/bgp_clist.c#L916) | **全等**（多一个少一个都不算） |
| any_match | `community_list_any_match` | [bgp_clist.c:937](bgpd/bgp_clist.c#L937) | 值级：任一值命中即返回 |
| match_delete | `community_list_match_delete` | [bgp_clist.c:963](bgpd/bgp_clist.c#L963) | **变换**：返回删掉命中 permit 值后的集合（`set comm-list ... delete`） |

三套（community/ecommunity/lcommunity）同名同形，只是集合类型不同（ecommunity 版 [856](bgpd/bgp_clist.c#L856)/[896](bgpd/bgp_clist.c#L896)/[1187](bgpd/bgp_clist.c#L1187)/[1265](bgpd/bgp_clist.c#L1265)，lcommunity 版 [834](bgpd/bgp_clist.c#L834)/[877](bgpd/bgp_clist.c#L877)/[1162](bgpd/bgp_clist.c#L1162)/[1212](bgpd/bgp_clist.c#L1212)）。
- **返回约定坑**（[bgp_clist.h:37-41](bgpd/bgp_clist.h#L37)）：`*_match` 返回 true **不等于 permit**——匹配上一条 deny 条目也返回 true（实际返回的是 `entry->direct == COMMUNITY_PERMIT`，所以对 `community_list_match` 是本条目结论，但语义容易与 exact/any 混淆）。
- 配置/查找：`community_list_set`（[bgp_clist.c:1057](bgpd/bgp_clist.c#L1057)）／`extcommunity_list_set`（[1490](bgpd/bgp_clist.c#L1490)）／`lcommunity_list_set`（[1370](bgpd/bgp_clist.c#L1370)）；`community_list_insert`（[215](bgpd/bgp_clist.c#L215)）按名字排序插入（数字名按数值比、字符串名按字典序）；`community_list_lookup`（[309](bgpd/bgp_clist.c#L309)）**要传对 `name_hash`**，传错会查不到（【[bgp_clist.h:257](bgpd/bgp_clist.h#L257)】）。生命周期 `community_list_init`（[1611](bgpd/bgp_clist.c#L1611)）／`terminate`（[1639](bgpd/bgp_clist.c#L1639)）。

### 13.4 与 route-map 的边界（回答核心问题 5）

| 层 | 谁 | 职责 | 归属 |
|---|---|---|---|
| 原子条件 | `bgp_filter.c` / `bgp_clist.c` / prefix-list | 「这条路由的 AS_PATH/团体是否匹配某规则」 | 本卷 |
| 正则引擎 | `bgp_regex.c` | 「用编译后的正则去匹配 aspath 字符串」 | 本卷 |
| 策略编排 | route-map（`lib/routemap.c`） | 「按序号挑条件、按方向/类型决定 permit/deny、执行 set」 | 卷6 |

**一句话**：`bgp_filter.c`/`bgp_clist.c` 是「**匹配器**」，route-map 是「**编排器+执行器**」。`neighbor ... filter-list`/`distribute-list` 是「把匹配器直接挂到邻居/方向上」的快捷方式，本质仍是同一批匹配函数。改「AS 路径匹配规则」动 `bgp_filter.c`/`bgp_regex.c`；改「过滤在策略里的次序」动 route-map。

---

### 13.5 `bgp_clist.c` 的内部结构（实测）

一条 community-list 由「列表（list）」与「条目（entry）」两层构成，两个维度（种类 × 写法）决定它落在哪个
master 的 `num`/`str` 链表中：

| 内部件 | 函数 | 行号 | 要点 |
|---|---|---|---|
| 条目新建/释放 | `community_entry_new` / `_free` | [151](bgpd/bgp_clist.c#L151) / [162](bgpd/bgp_clist.c#L162) | 释放时按 style 释放 union 中的正则或团体对象 |
| 列表新建/释放 | `community_list_new` / `_free` | [195](bgpd/bgp_clist.c#L195) / [203](bgpd/bgp_clist.c#L203) | |
| 插入（按名排序） | `community_list_insert` | [214](bgpd/bgp_clist.c#L214) | 数字名比数值、字符串名比字典序 |
| 查找 | `community_list_lookup` | [309](bgpd/bgp_clist.c#L309) | 需正确的 `name_hash` |
| 删除 | `community_list_delete` | [347](bgpd/bgp_clist.c#L347) | |
| 空判定 | `community_list_empty_p` | [376](bgpd/bgp_clist.c#L376) | |
| 条目删除/替换/添加 | `_entry_delete` / `_entry_replace` / `_entry_add` | [384](bgpd/bgp_clist.c#L384) / [411](bgpd/bgp_clist.c#L411) / [440](bgpd/bgp_clist.c#L440) | seq 相同的条目走 replace |
| **seq 自动分配** | `bgp_clist_new_seq_get` | [66](bgpd/bgp_clist.c#L66) | 取当前最大 seq 上取整到 5 的倍数 +5（同 as-path 思路） |
| seq 冲突检查 | `bgp_clist_seq_check` | [87](bgpd/bgp_clist.c#L87) | |
| 重复条目检查 | `community_list_dup_check` | [1014](bgpd/bgp_clist.c#L1014) | 防「配置完全相同」的冗余条目 |
| 取第 i 个值转串 | `community_str_get` | [536](bgpd/bgp_clist.c#L536) | `community`/`ecommunity`/`lcommunity` 各一份 |
| 正则包含判定 | `community_regexp_include` | [607](bgpd/bgp_clist.c#L607) | |
| 正则整体匹配 | `community_regexp_match` | [633](bgpd/bgp_clist.c#L633) | |
| 大团体合法性预检 | `lcommunity_list_valid` | [1310](bgpd/bgp_clist.c#L1310) | 解析前校验格式 |

> **为什么 community_str_get 有三份**（[536](bgpd/bgp_clist.c#L536) / [667](bgpd/bgp_clist.c#L667) / [675](bgpd/bgp_clist.c#L675)）：
> 因为「取第 i 个元素并转成可读串」这一步对三种团体各自要调不同的 `*_ecom2str`/`*_str`，
> 无法用同一个函数表达。expanded 列表的正则匹配就是拿这个串去 `bgp_regexec`。

---

## §14 团体别名：`bgp_community_alias.c`

### 14.1 结论：把 `65001:100` 映射成可读名字，靠**两张哈希表**实现双向查

BGP 团体是裸数字，运维记不住。FRR 允许给常用团体起别名（如 `65001:100` → `BLACKHOLE`），配置里可用别名代替数字。

实现是**两张哈希表**（[bgp_community_alias.c:50-51](bgpd/bgp_community_alias.c#L50)）：

| 表 | 键 | 可见性 | 用途 |
|---|---|---|---|
| `bgp_ca_alias_hash` | `ca->alias` | **对外可见**（extern，[bgp_community_alias.h:64](bgpd/bgp_community_alias.h#L64)） | 按别名查团体（配置解析） |
| `bgp_ca_community_hash` | `ca->community` | 文件内 static | 按团体查别名（回显/日志） |

**同一个 `community_alias` 结构体被两张表共用**（同一份数据插入两次，键不同）——**不要为两个方向各建一份对象**（[bgp_community_alias.h:44-46](bgpd/bgp_community_alias.h#L44)）。映射是**一对一**，把同一别名映射到两个团体（或反之）属配置冲突，实现里会查重。

### 14.2 结构与常量

```c
struct community_alias {
    char community[LCOMMUNITY_SIZE * 3];  /* 可读团体串（容标准/扩展/大团体最长写法） */
    char alias[BUFSIZ];
};
```
（[bgp_community_alias.h:52](bgpd/bgp_community_alias.h#L52)）。注意 `community` 存的是**字符串**不是二进制团体值，比较走字符串比较（`bgp_ca_community_hash_cmp`，[bgp_community_alias.c:62](bgpd/bgp_community_alias.c#L62)）。

### 14.3 成对的 insert/delete 与三个转换

| 函数 | 行号 | 说明 |
|---|---|---|
| `bgp_ca_community_insert` / `bgp_ca_alias_insert` | [150](bgpd/bgp_community_alias.c#L150) / [156](bgpd/bgp_community_alias.c#L156) | **必须成对**，只插一张会导致「正向能查、反向查不到」 |
| `bgp_ca_community_delete` / `bgp_ca_alias_delete` | [163](bgpd/bgp_community_alias.c#L163) / [172](bgpd/bgp_community_alias.c#L172) | 成对删除 |
| `bgp_ca_lookup`（按 alias）/ `bgp_ca_community_lookup`（按 community） | [186](bgpd/bgp_community_alias.c#L186) / [180](bgpd/bgp_community_alias.c#L180) | 命名坑：函数名后缀指**用哪个字段当键** |
| `bgp_community2alias` | [194](bgpd/bgp_community_alias.c#L194) | 数值串 → 别名（**显示用**），查不到原样返回入参 |
| `bgp_alias2community` | [211](bgpd/bgp_community_alias.c#L211) | 别名 → 数值串（**配置解析用**），查不到原样返回入参 |
| `bgp_alias2community_str` | [236](bgpd/bgp_community_alias.c#L236) | 一整串逐个还原，**返回新分配字符串**（`frrstr_join`），调用方负责释放 |

`bgp_alias2community_str` 的**联动对端**是 `bgp_clist.c`：expanded community-list 的正则匹配前需要把别名还原成数字（[bgp_community_alias.c:230-235](bgpd/bgp_community_alias.c#L230) 注释明说）。

回显与补全：`bgp_community_alias_write`（[bgp_community_alias.c:139](bgpd/bgp_community_alias.c#L139)）写 `bgp community alias` 命令；`bgp_community_alias_command_completion_setup`（[bgp_community_alias.c:288](bgpd/bgp_community_alias.c#L288)）注册 CLI 补全。

> `bgp_community_alias_alloc`（[bgp_community_alias.c:91](bgpd/bgp_community_alias.c#L91)）里局部变量类型写成了 `struct communtiy_alias`（源码原样拼写失误），因仅作 `void*` 中转不被解引用，照常编译——见 §19。

---

## §15 陷阱清单（速查）

| # | 陷阱 | 后果 | 正确做法 |
|---|---|---|---|
| 1 | `bgp_regexec` 返回值按「true=命中」理解 | 过滤逻辑完全反掉 | **0 = 命中**，非 0 = 不命中（[bgp_regex.c:112](bgpd/bgp_regex.c#L112)） |
| 2 | 改了 `aspath` 段内容却不调 `aspath_str_update` | `str` 与实际不符 → **as-path 正则匹配结果与显示不符** | 任何改段后必刷 `str`（[bgp_aspath.c:847](bgpd/bgp_aspath.c#L847)） |
| 3 | 用 `bgp_path_info.next/prev` 当下一跳链表遍历 | 遍历出错 | 下一跳链表是 `nh_thread`（[bgp_route.h:352](bgpd/bgp_route.h#L352)），两套链表不同 |
| 4 | 忘记 `BGP_PATH_MULTIPATH` **不设在最优路径上** | 数 ECMP 成员时多/少数一条 | 见 [bgp_route.h:391](bgpd/bgp_route.h#L391) 注释 |
| 5 | `COMMUNITY_DENY == 0` | memset 后忘设 direct 的规则变 deny | 明确赋值 `direct`（[bgp_clist.h:62](bgpd/bgp_clist.h#L62)） |
| 6 | `COMMUNITY_LIST_STANDARD == 0` 当成「未设置」判空 | 把合法 style 当空 | 0 是有效值（[bgp_clist.h:87](bgpd/bgp_clist.h#L87)） |
| 7 | `ASPATH_SEQ_NUMBER_AUTO == -1` 当真实序号比大小 | 排序错乱 | seq 是 `int64_t`，先判 AUTO（[bgp_filter.h:42](bgpd/bgp_filter.h#L42)） |
| 8 | `community_list_lookup` 传错 `name_hash` | 查不到（哈希按值找桶） | 用 `bgp_clist_hash_key` 算的键，或传 0 让它重建（[bgp_clist.c:309](bgpd/bgp_clist.c#L309)） |
| 9 | `bgp_clist` 的 `*_match` 返回 true 当成 permit | 把 deny 当放行 | 返回的是「该条目结论」，结合条目 direct 理解（[bgp_clist.h:37](bgpd/bgp_clist.h#L37)） |
| 10 | `str2prefix_rd` 返回 0 当成成功 | 校验反了 | **成功返回 1，失败返回 0**（[bgp_rd.c:153](bgpd/bgp_rd.c#L153) 实现，`ret = 1` 在 [bgp_rd.c:219](bgpd/bgp_rd.c#L219)） |
| 11 | 把 `BGP_AS_MAX` 当 4 字节 AS 上界 | 逻辑错误 | 它**仍是 65535**（2 字节上界），4 字节用 `BGP_AS4_MAX`（[bgp_aspath.h:84](bgpd/bgp_aspath.h#L84)） |
| 12 | 混用 `aspath_count_hops` 与 `aspath_count_confeds` | 跳数算错 | 前者排除联盟段，后者只算联盟（[bgp_aspath.c:540](bgpd/bgp_aspath.c#L540)） |
| 13 | `bgp_dest->info` 当成单一类型 | 读到别种表的内存布局 | 按「当前在操作哪张表」解释（[bgp_table.h:17-28](bgpd/bgp_table.h#L17)） |
| 14 | `community_intern` 前对象已 refcnt>0 | 断言失败 | intern 只接受 refcnt==0 的新对象（[bgp_community.c:525](bgpd/bgp_community.c#L525)） |
| 15 | `aspath_get*`/`*_intern` 返回后继续用原指针 | 悬空（intern 可能释放入参） | **拿到返回值就重新赋值**（[bgp_aspath.h:264-269](bgpd/bgp_aspath.h#L264)） |
| 16 | 两个方向只插一张 alias 表 | 正向能查、反向查不到 | insert/delete **成对**（[bgp_community_alias.h:80](bgpd/bgp_community_alias.h#L80)） |
| 17 | `community_del_val` 传主机序值 | 删不掉 | 标准团体值要 `htonl`（[bgp_community.h:172](bgpd/bgp_community.h#L172)） |
| 18 | `bgp_rib_remove` 后立刻假设 pi 已释放 | 访问已标记未回收对象 | 只标记，真正回收在 `bgp_path_info_reap`（[bgp_route.c:751](bgpd/bgp_route.c#L751)） |

---

## §16 自测题（复习用）

<details><summary>1. RIB 三层结构分别是什么？各层职责？</summary>

`bgp_table`（[bgp_table.h:55](bgpd/bgp_table.h#L55)）＝一棵 (bgp,afi,safi) 的表，内包通用前缀树；`bgp_dest`（[bgp_table.h:136](bgpd/bgp_table.h#L136)）＝树上一个前缀节点的多态外壳；`bgp_path_info`（[bgp_route.h:343](bgpd/bgp_route.h#L343)）＝该前缀下的一条路径。多层是靠 `bgp_dest->info` 多态串起来的。
</details>

<details><summary>2. 为什么同一前缀的多条路径既能用链表又能 O(1) 查找？</summary>

链表 `next/prev` 表达多路径共存；`bgp_table.pi_hash`（[bgp_table.h:66](bgpd/bgp_table.h#L66)）按 `(peer,type,sub_type,addpath_id)` 建哈希，入向用 `bgp_pi_hash_find`（[bgp_route.c:6292](bgpd/bgp_route.c#L6292)）。
</details>

<details><summary>3. `bgp_path_info_extra` 为什么拆出去？</summary>

只有少数路径需要抖动抑制/标签/EVPN/VRF 泄漏数据，集中到懒分配结构省内存（[bgp_route.h:265](bgpd/bgp_route.h#L265)，`bgp_path_info_extra_get` [bgp_route.c:474](bgpd/bgp_route.c#L474)）。
</details>

<details><summary>4. `bgp_table.c` 到底做了什么？</summary>

只做包壳与生命周期：锁、节点/表销毁（[bgp_table.c:132](bgpd/bgp_table.c#L132)/[170](bgpd/bgp_table.c#L170)）、两个反查工具（[109](bgpd/bgp_table.c#L109)/[282](bgpd/bgp_table.c#L282)）。前缀树能力来自 lib `route_table`。
</details>

<details><summary>5. VPN 的「RD + 前缀」在内存里怎么表示？</summary>

RD 外层建一个 `bgp_dest`，其 `info` 指向一棵子 `bgp_table`，真前缀落在子表里（`bgp_afi_node_get` [bgp_route.c:315](bgpd/bgp_route.c#L315)，`dest->pdest` 指回外层）。
</details>

<details><summary>6. `bgp_route.c` 有几个分区？最大的亮点是什么？</summary>

13 个编号分区注释（§4.1 表），加两个「入向/撤销」非编号块。亮点是这套注释分区本身就是最佳导航。
</details>

<details><summary>7. `bgp_update` 的 7 步是什么？</summary>

见 §5.1：LU 归一 → 取/建 dest → pi_hash 查旧 → 校验 → intern/same_attr → 更新或新建 → NHT+process。
</details>

<details><summary>8. `bgp_path_info_cmp` 的比较顺序？</summary>

weight→local-pref→…→AS_PATH 长度→origin→MED→eBGP/iBGP→IGP metric→router-id→cluster-list（[bgp_route.c:1063](bgpd/bgp_route.c#L1063)）。
</details>

<details><summary>9. `bgp_best_selection` 的复杂度？三种模式？</summary>

O(N²)；deterministic-med / 常规 / multipath 三种（[bgp_route.c:3592](bgpd/bgp_route.c#L3592)）。
</details>

<details><summary>10. meta_queue 有几个子队列？怎么防重复入队？</summary>

3 个（EARLY/OTHER/EOIU_MARKER，[bgp_route.h:720](bgpd/bgp_route.h#L720)）；用 dest flags 的 MQ 位 + `MQ_BIT_MASK`（[bgp_route.h:678](bgpd/bgp_route.h#L678)）。
</details>

<details><summary>11. EOIU 标记为什么把 bgp 指针塞进 dest->info？</summary>

因为 meta_queue 只接受 `bgp_dest*`，而假 dest 不天然带实例指针（[bgp_route.h:706-712](bgpd/bgp_route.h#L706)、[bgp_route.c:5623](bgpd/bgp_route.c#L5623)）。
</details>

<details><summary>12. AS_PATH 为什么是「段的链表」而不是数组？</summary>

线格式本身就是 `[type,length,AS…]` 的段序列；AS_SET/AS_CONFED_* 与 AS_SEQUENCE 语义不同，必须分段（[bgp_aspath.h:13-22](bgpd/bgp_aspath.h#L13)、[bgp_aspath.h:106](bgpd/bgp_aspath.h#L106)）。
</details>

<details><summary>13. AS_PATH 的哈希键基于什么？</summary>

基于 `str`（`jhash(str, str_len)`，[bgp_aspath.c:2762](bgpd/bgp_aspath.c#L2762)），不是段结构——所以 asnotation 变化会影响相等性（[bgp_aspath.c:2780](bgpd/bgp_aspath.c#L2780)）。
</details>

<details><summary>14. 三种团体的驻留机制有何异同？</summary>

同构：uniq_sort→hash_get→命中释放入参、refcnt++。异：键原语与种子不同，ecommunity 还有 `unit_size`（8/20 字节）。见 §10.1 表。
</details>

<details><summary>15. 扩展团体的高序字节 vs 低序字节各管什么？</summary>

高序字节=编码空间（怎么取 AS/IP），低序字节=subtype（RT/SoO/BW/color…）（[bgp_ecommunity.h:66-76](bgpd/bgp_ecommunity.h#L66)）。
</details>

<details><summary>16. `ecommunity_strip_non_transitive` 干什么？</summary>

删除所有非传递扩展团体（type 0x40 位或 0x41/0x43），典型用于向 eBGP 发送前（[bgp_ecommunity.c:1829](bgpd/bgp_ecommunity.c#L1829)）。
</details>

<details><summary>17. RD 三种 type 的字段布局？</summary>

0: 2B AS+4B；1: 4B IP+2B；2: 4B AS+2B（[bgp_rd.h:16-24](bgpd/bgp_rd.h#L16)）。
</details>

<details><summary>18. `str2prefix_rd` 成功返回几？</summary>

1（不是 0）（[bgp_rd.c:153](bgpd/bgp_rd.c#L153)）。
</details>

<details><summary>19. `_` 在 as-path 正则里是什么？</summary>

AS 边界，编译前被替换成 `(^|[,{}() ]|$)`（[bgp_regex.c:71](bgpd/bgp_regex.c#L71)）。
</details>

<details><summary>20. `as_list_apply` 全不命中返回什么？</summary>

`AS_FILTER_DENY`（未命中默认拒绝，[bgp_filter.c:453](bgpd/bgp_filter.c#L453)）。
</details>

<details><summary>21. community-list 的六种 style 是哪两个维度的乘积？</summary>

种类（community/ext/large）× 写法（standard/expanded）（[bgp_clist.h:87-92](bgpd/bgp_clist.h#L87)）。
</details>

<details><summary>22. `community_list_match` 与 `any_match`、`exact_match`、`match_delete` 的区别？</summary>

见 §13.3 表：整体命中 / 值级任一 / 全等 / 删除命中项。
</details>

<details><summary>23. 团体别名为什么要两张表？</summary>

要支持双向查：alias→community（配置解析）与 community→alias（回显）。同一结构体插两次，键不同（[bgp_community_alias.h:44-46](bgpd/bgp_community_alias.h#L44)）。
</details>

<details><summary>24. `bgp_filter.c`/`bgp_clist.c` 与 route-map 的边界？</summary>

前者是匹配器（原子条件），route-map 是编排器+执行器（卷6）。见 §13.4。
</details>

<details><summary>25. `attr->flag` 与线上 TLV flags 字节是一回事吗？</summary>

不是。`attr->flag` 是「本路径有哪些属性」的进程内位图（[bgp_attr.h:412](bgpd/bgp_attr.h#L412)）；线上 flags 是 TLV 的可选/传递/部分位（[bgp_attr.h:61-64](bgpd/bgp_attr.h#L61)）。见 §12。
</details>

---

## §17 代码索引（按建议阅读顺序）

**第 1 步：建立 RIB 三层认知**
- `bgp_table.h` 文件头（[1-35](bgpd/bgp_table.h#L1)）→ `struct bgp_table`（[55](bgpd/bgp_table.h#L55)）→ `enum bgp_path_selection_reason`（[85](bgpd/bgp_table.h#L85)）→ `struct bgp_dest`（[136](bgpd/bgp_table.h#L136)）。**要建立**：「表/节点/路径」三层与 `info` 多态。

**第 2 步：路径模型与懒分配**
- `bgp_route.h` 文件头（[1-24](bgpd/bgp_route.h#L1)）→ `struct bgp_path_info_extra`（[265](bgpd/bgp_route.h#L265)）→ `struct bgp_path_info`（[343](bgpd/bgp_route.h#L343)）→ flags（[380-413](bgpd/bgp_route.h#L380)）。**要建立**：一条路由由哪些字段构成、哪些状态位决定可用性。

**第 3 步：容器生命周期**
- `bgp_table.c`：`bgp_table_init`（[216](bgpd/bgp_table.c#L216)）→ `bgp_dest_unlock_node`（[132](bgpd/bgp_table.c#L132)）→ `bgp_node_destroy`（[170](bgpd/bgp_table.c#L170)）。**要建立**：节点销毁的「先判后减」模式。

**第 4 步：bgp_route.c 分区地图**
- 用 `grep -n '^/\* ========== '` 抓 13 个分区头（§4.1）→ 先读分区 1（[307](bgpd/bgp_route.c#L307)）→ 分区 6（[5168](bgpd/bgp_route.c#L5168)）→ 入向（[6193](bgpd/bgp_route.c#L6193)）。**要建立**：文件不是按调用顺序排的。

**第 5 步：主链**
- `bgp_update`（[6210](bgpd/bgp_route.c#L6210)）→ `bgp_process`（[5607](bgpd/bgp_route.c#L5607)）→ `bgp_process_main_one`（[4454](bgpd/bgp_route.c#L4454)）→ `bgp_best_selection`（[3592](bgpd/bgp_route.c#L3592)）→ `bgp_path_info_cmp`（[1063](bgpd/bgp_route.c#L1063)）→ `subgroup_announce_check`（[2665](bgpd/bgp_route.c#L2665)）。**要建立**：入向→决策→出向的完整闭环。

**第 6 步：属性容器**
- AS_PATH：`bgp_aspath.h` 头（[5-42](bgpd/bgp_aspath.h#L5)）→ `aspath_parse`（[bgp_aspath.c:1075](bgpd/bgp_aspath.c#L1075)）→ `aspath_put`（[1212](bgpd/bgp_aspath.c#L1212)）→ `aspath_intern`（[865](bgpd/bgp_aspath.c#L865)）。
- 团体：`bgp_community.c` 的 `community_intern`（[525](bgpd/bgp_community.c#L525)）→ 对比 `ecommunity_intern`（[bgp_ecommunity.c:367](bgpd/bgp_ecommunity.c#L367)）→ `lcommunity_intern`（[bgp_lcommunity.c:303](bgpd/bgp_lcommunity.c#L303)）。**要建立**：一个模式看三遍。
- RD：`bgp_rd.c` 的三种 decode（[89](bgpd/bgp_rd.c#L89)/[101](bgpd/bgp_rd.c#L101)/[111](bgpd/bgp_rd.c#L111)）→ `str2prefix_rd`（[153](bgpd/bgp_rd.c#L153)）→ `prefix_rd2str`（[234](bgpd/bgp_rd.c#L234)）。

**第 7 步：过滤器**
- `bgp_regex.c` 全文（123 行）→ `bgp_filter.c` 的 `as_list_apply`（[453](bgpd/bgp_filter.c#L453)）→ `bgp_clist.c` 的 `community_list_match`（[815](bgpd/bgp_clist.c#L815)）与 `community_list_match_delete`（[963](bgpd/bgp_clist.c#L963)）。**要建立**：匹配器与编排器的分界。

**第 8 步：别名**
- `bgp_community_alias.c` 全文（291 行）。**要建立**：两张表 ↔ 双向映射。

---

## §18 与官方 `doc/developer/*.rst` 的对应

| 本卷主题 | 官方开发者文档 | 说明 |
|---|---|---|
| 路径选择 `bgp_path_info_cmp` | `doc/developer/bgp.rst` 中 best-path 章节（若存在） | 代码是 RFC4271 §9.1.2.2 的直接落地 |
| `meta_queue` / work_queue | `doc/developer/workqueue.rst` | 解释 work_queue 的调度与「不阻塞主线程」设计 |
| 前缀树 `route_table` | `doc/developer/tables.rst` | `bgp_table` 复用其能力，本卷 §1.1 |
| 引用计数 intern 模式 | `doc/developer/memtypes.rst` / `doc/developer/hashing.rst` | 与 `hash_get` 的 `hash_alloc_intern` 回调配合 |
| `struct bgp_path_info` 语义 | `doc/developer/bgp.rst`（数据结构概览） | 官方文档较简，实践以本卷 + 代码为准 |

> 说明：FRR 的 `doc/developer/` 对 bgpd 内部数据结构的覆盖较薄（多在 head-comment 与注释里），本卷的分区地图与行号锚点可作为官方文档的补充定位表。

---

## §19 勘误：注释与代码不符 / 可疑之处

以下均为**实测**发现的、注释（含英文原注释与中文注解）与代码不完全一致或需强调之处：

1. **`bgp_route.c:11` —— 注释说「约 1.8 万行」，实际 19317 行**。
   文件头导读写「这是 bgpd 体量最大、最核心的文件（约 1.8 万行）」（该「约 1.8 万行」为源码注释原文），
   而 `wc -l` 实测为 **19317 行**。属估算偏差，不影响理解。

2. **`bgp_ecommunity.h:257-261` —— 英文原注释写 "IPv6 Extended community value is eight octet"，实际 20 字节**。
   ```c
   /* IPv6 Extended community value is eight octet. */
   struct ecommunity_val_ipv6 {
       uint8_t val[IPV6_ECOMMUNITY_SIZE];   /* IPV6_ECOMMUNITY_SIZE = 20 */
   };
   ```
   中文注释已指出「疑点：原英文注释写作 eight octet 但实际是 20 字节」。代码正确、英文注释有误。

3. **`bgp_community.h:90-91` —— `COMMUNITY_LOCAL_AS` 与 `COMMUNITY_NO_EXPORT_SUBCONFED` 数值相同（均为 `0xFFFFFF03`）**。
   这是**有意的历史兼容**（同一线值在 `set_community_string` 里打印为 `local-AS`，在文档 RFC1997 语境叫 no-export-subconfed）。注释已标注为「疑点」，非 bug。

4. **`bgp_community_alias.c:94` —— 局部变量类型拼写 `struct communtiy_alias`（应为 `community_alias`）**。
   源码原样保留；因仅作 `void *` 中转、不被解引用，编译器不报错。属笔误，非功能缺陷。

5. **`bgp_route.c` 分区注释格式不统一** —— 13 个分区用 `/* ========== N. … ========== */`，但「入向 `bgp_update`」（[6193](bgpd/bgp_route.c#L6193)）与「入向撤销 `bgp_withdraw`」（[7124](bgpd/bgp_route.c#L7124)）两个大块用 `/* ====…【标题】…==== */`。因此 `grep '^/\* ==========' ` **抓不全**这些块——按分区导航时要两种模式都搜。

6. **`bgp_filter.c:20-24` 与 `bgp_filter.h:72-74` 的「同名异义」`as_list_list`** —— 同一个类型名在两处指不同东西：头文件里的是 `exclude_rule` 用的 DLIST 头，`.c` 里的是私有 head/tail 对。中文注释已显式提示，但**按名字搜索会串**。

7. **`bgp_aspath.h:84` `BGP_AS_MAX` 语义陷阱** —— 英文原注释 `we leave BGP_AS_MAX as the 16bit AS MAX number` 已说明它**故意**保留为 65535，但名字极易被误读为「AS 号最大值」。中文注释已强调。

8. **`bgp_clist.h:72-75` 关于数字名范围的注释** —— 原文说「numbered community-list ... have a range between 1-500」并注解「下界是 1 而不是 0」，但代码中 `COMMUNITY_LIST_STRING == 0`/`COMMUNITY_LIST_NUMBER == 1`（[bgp_clist.h:67-68](bgpd/bgp_clist.h#L67)）是**命名方式**的取值，`COMMUNITY_LIST_NUMBER_MAX == 500`（[bgp_clist.h:75](bgpd/bgp_clist.h#L75)）是**名字数值**上限——两个「0」含义不同，注解把二者并置时略有混淆。读时以代码为准。

9. **`bgp_rd.h:128-130` —— 头文件注释说「返回 0 表示成功，失败时返回非 0」，代码实际是「成功返回 1、失败返回 0」**。
   两处注释**互相矛盾**：头文件声明处（[bgp_rd.h:129](bgpd/bgp_rd.h#L129)）写「返回 0 表示成功」，
   而函数定义处的中文注释（[bgp_rd.c:144](bgpd/bgp_rd.c#L144)）写「★ 返回值：1 表示成功，0 表示失败」。
   查代码：`int ret = 0`（[bgp_rd.c:155](bgpd/bgp_rd.c#L155)）→ 成功路径 `ret = 1`（[bgp_rd.c:219](bgpd/bgp_rd.c#L219)）。
   **代码与 `.c` 注释一致，头文件注释有误**。

10. **`bgp_rd.c:147-152` —— 源码自陈 `str2prefix_rd` 双冒号分支存在「多前进一位」的解析反常**。
   走两个 `:` 的分支时 `p = p2 + 1`，而后续 `all_digit(p + 1)` 与 `atol(p + 1)` 又各多前进一位，
   等于**从值字段第 2 个字符起取值**（`"0:65001:100"` 会把 `"00"` 当编号、`"1:10.0.0.1:5"` 取到空串）；
   对照单冒号分支（`p` 停在第一个 `:`、`p + 1` 正好是值）可见 if 分支偏移多一位。
   注释已明确「仅标注这一实现差异、不改代码；是否为历史遗留 bug，请以实际需求/上游版本为准」。
   本卷如实转述该疑点，不复现/不判定。

> 分类说明：第 1、2、4、5、9 项属**注释与代码/注释与注释不一致**（其中 2、9 是会直接误导读代码的，
> 已分别在第 §10.3、§11.4、§15 交叉标注）；第 3、6、7、8 项属「**注释作者已主动标注的疑点或易混点**」
> （数值相同的历史兼容、同名异义类型、语义易误读的宏、命名方式的 0/1 取值），非隐蔽错误；
> 第 10 项是源码自陈的实现反常，本卷如实转述、不判定。除上述外，本卷逐条核对的 `#L数字` 锚点与源码一致。

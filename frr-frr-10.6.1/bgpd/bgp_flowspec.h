// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Flowspec header for packet handling
 * Copyright (C) 2018 6WIND
 */
/* ============================================================================
 * 【文件导读】bgp_flowspec.h —— BGP FlowSpec 的解析与展示接口
 * ============================================================================
 * 【FlowSpec 是什么】RFC 8955(IPv4) / RFC 8956(IPv6) 定义的 AFI/SAFI = 1/133、
 * 2/133：它的 NLRI 描述的不是“一条路由”，而是**一束匹配规则的集合**——例如
 * “目的前缀 10.0.0.0/24 且 协议=TCP 且 端口=80/443 且 DSCP=46”，再配上流量动作
 * (traffic-rate、traffic-action、redirect 等扩展团体)表达“这类流量该怎么处理”。
 * 典型用途是 **DDoS 清洗**：被攻击方通过 BGP 把“丢弃/限速/重定向某类报文”的
 * 规则发给上游，上游据此在本机下发过滤或 PBR 策略。
 *
 * 【与普通路由处理的最大不同】普通 NLRI 解码成前缀后进 RIB 选路即可；FlowSpec 的
 * NLRI 是**一串 TLV 式的匹配分量(component)**，解码后要转成内部的
 * bgp_pbr_entry_main 结构，进而下发到 zebra 变成 PBR 规则。因此它走的是
 * 「解析 → 转内部语义 → 下发 zebra」这条路（见 bgp_pbr.c / bgp_flowspec_util.c），
 * 而不是「进 RIB 选路」。
 *
 * 【本文件里的 format 常量】FlowSpec 的 NLRI 是二进制分量，给运维看必须转成
 * 字符串；不同场合需要的详细程度不同，于是有下面几种渲染模式（LARGE / DEBUG /
 * MIN / JSON / JSON_SIMPLE）。它们只影响**显示**，不影响解析与下发。
 *
 * 【阅读建议】bgp_nlri_parse_flowspec(入口) → bgp_flowspec_util.c 里的分量解码
 *            → bgp_pbr.c 里的策略生成；显示相关看本文件后半段的 vty 函数。
 * ============================================================================
 */

#ifndef _FRR_BGP_FLOWSPEC_H
#define _FRR_BGP_FLOWSPEC_H

/* 【显示模式】把一份 FlowSpec NLRI 渲染成文本时的详细程度：
 *   LARGE       : 最完整（包含所有分量与动作的原始语义），用于 `show ... detail`。
 *   DEBUG       : 带调试信息的渲染，用于日志排障。
 *   MIN         : 最精简，只体现关键匹配项，用于列表类输出避免刷屏。
 *   JSON        : 结构化输出（完整字段），供程序消费。
 *   JSON_SIMPLE : 结构化但字段精简的 JSON。
 * 取值为 0~4 —— 注意 0 是 LARGE 而不是“未设置”，若某处用 0 当默认值，默认就是
 * “最啰嗦的模式”，这与一般“0 表示无”的直觉相反。 */
#define NLRI_STRING_FORMAT_LARGE        0
#define NLRI_STRING_FORMAT_DEBUG        1
#define NLRI_STRING_FORMAT_MIN          2
#define NLRI_STRING_FORMAT_JSON         3
#define NLRI_STRING_FORMAT_JSON_SIMPLE  4

/* 【缓冲长度】渲染一条 NLRI 所需字符串缓冲的上限。FlowSpec 分量多、每个分量又
 * 可能展开成一串数值（如端口列表），所以给到 512；调用方**必须**按此大小准备
 * 缓冲，否则会被截断（甚至溢出，取决于实现是否做长度检查）。 */
#define BGP_FLOWSPEC_NLRI_STRING_MAX 512

/* 【函数】NLRI 解析入口，由公共 NLRI 分发逻辑按 AFI/SAFI 调到这里：
 *   withdraw = true 表示这是撤销，只需把对应规则摘除；
 *   peer / attr 提供来源上下文（策略、下一跳等）。
 *   返回值为解析结果码（负值通常表示格式错误，具体约定读实现）。
 * 注意这不是“把前缀塞进 RIB”，而是把规则转成内部 PBR 条目。 */
extern int bgp_nlri_parse_flowspec(struct peer *peer, struct attr *attr,
				   struct bgp_nlri *packet, bool withdraw);

/* 【函数】注册与 FlowSpec 相关的 show 命令（bgpd 启动时调用一次）。 */
extern void bgp_flowspec_vty_init(void);

/* 【函数】bgp_show_table_flowspec —— 遍历 FlowSpec 表并逐条输出（`show bgp ...
 * flowspec` 的实现）。参数较多，分组理解：
 *   输出目的地   : vty / use_json（文本还是 JSON）。
 *   数据来源     : bgp、afi、table（要遍历的表）。
 *   筛选与形态   : type(enum bgp_show_type)、output_arg（随 type 变化的附加参数，
 *                  如按前缀/社区过滤的条件，void * 是因为形态很多）。
 *   分页/累计    : is_last 表示是否本批次最后一块，output_cum / total_cum 是
 *                  **累计计数器**（已输出条数 / 总条数）——分页输出时跨多次调用
 *                  累加，所以是指针而非值，调用方要负责保持它们存活。
 * 这个签名是从通用 show 框架复用来的，因此参数风格偏“万能”，不必逐个深究。 */
extern int bgp_show_table_flowspec(struct vty *vty, struct bgp *bgp, afi_t afi,
				   struct bgp_table *table,
				   enum bgp_show_type type, void *output_arg,
				   bool use_json, int is_last,
				   unsigned long *output_cum,
				   unsigned long *total_cum);

/* 【函数】bgp_fs_nlri_get_string —— 把一段**二进制 NLRI 内容**渲染成字符串。
 *   nlri_content / len : 原始分量字节与长度（长度很关键，分量是 TLV 式解析，
 *                        没有长度无法界定边界）。
 *   return_string      : 调用方提供的输出缓冲，至少 BGP_FLOWSPEC_NLRI_STRING_MAX。
 *   format             : 上面那组 NLRI_STRING_FORMAT_* 之一。
 *   json_path          : 当 format 是 JSON 系列时，结果写进这个 json 对象；
 *                        文本模式下该参数不参与（可传 NULL）。
 *   afi                : 决定按 IPv4 还是 IPv6 解释分量（同一分量号在 v4/v6 下
 *                        含义/长度可能不同，例如 IPv6 独有 flow-label）。
 * 无返回值——结果直接写入入参缓冲，所以**调用方必须先准备好缓冲**。 */
extern void bgp_fs_nlri_get_string(unsigned char *nlri_content, size_t len,
				   char *return_string, int format,
				   json_object *json_path,
				   afi_t afi);

/* 【函数】单条 FlowSpec 路由的 vty 输出（供上面的表遍历与分页逻辑调用）。
 *   p / path    : 前缀(承载 NLRI 键)与路径。
 *   display     : 显示详略控制（与 NLRI_STRING_FORMAT_* 不是同一套常量，注意区分）。
 *   json_paths  : JSON 输出目标，非 JSON 模式下可传 NULL。 */
extern void route_vty_out_flowspec(struct vty *vty, const struct prefix *p,
				   struct bgp_path_info *path, int display,
				   json_object *json_paths);
/* 【函数】bgp_fs_config_write_pbr —— 把内部 PBR 条目反向写成**可再解析的配置行**，
 * 供 `show running-config` 使用。返回写入的行数/状态码（读实现确认约定）。
 * 它只处理 FlowSpec 对应的 PBR 部分，名字里的 fs 即 flowspec。 */
extern int bgp_fs_config_write_pbr(struct vty *vty, struct bgp *bgp,
				   afi_t afi, safi_t safi);

/* 【函数】bgp_flowspec_display_match_per_ip —— 反查：“某个 IP（或前缀）匹配到了
 * 哪些 FlowSpec 规则”。用于 `show bgp ... flowspec <ip>` 这类排查：想知道某流量
 * 为什么被丢弃 / 限速时，用它列出命中的规则。
 *   match / prefix_check : 目标地址；prefix_check 为真时按**前缀包含**判断，
 *                          否则按具体地址匹配。
 *   rib                  : 在里面找规则的表。
 * 返回命中的条目数（具体约定读实现）。 */
extern int bgp_flowspec_display_match_per_ip(afi_t afi, struct bgp_table *rib,
					     struct prefix *match,
					     int prefix_check, struct vty *vty,
					     bool use_json,
					     json_object *json_paths);

#endif /* _FRR_BGP_FLOWSPEC_H */

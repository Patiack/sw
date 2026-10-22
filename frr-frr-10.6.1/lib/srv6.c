// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRv6 definitions
 * Copyright (C) 2020  Hiroki Shirokura, LINE Corporation
 */

/*
 * ============================================================================
 * 文件导读：lib/srv6.c —— SRv6 公共工具实现
 * ============================================================================
 * 本文件实现 lib/srv6.h 中声明的非内联函数，是各守护进程共享的 SRv6 工具集。
 * 内容大致分四类：
 *   1) 行为 / 上下文转字符串与 JSON：
 *      seg6local_action2str、seg6local_context2str、seg6local_context2json、
 *      srv6_sid_structure2json、seg6local_flavors2str 等，供 VTY 与 JSON 展示；
 *   2) SID 序列格式化：snprintf_seg6_segs；
 *   3) locator / chunk 的生命周期管理与 JSON 导出：
 *      srv6_locator_alloc、srv6_locator_free、srv6_locator_copy、
 *      srv6_locator_json 与 detailed_json、chunk 的 JSON 导出等；
 *   4) SID format 与 SID context 的分配与释放。
 *
 * 核心数据：
 *   struct srv6_locator / srv6_locator_chunk / srv6_sid_format / srv6_sid_ctx
 *   （定义见 srv6.h）。本文件负责它们的构造、析构与对外表示。
 *
 * 与其他模块的关系：
 *   - 文件顶部用 DEFINE_QOBJ_TYPE / DEFINE_MTYPE_STATIC 注册对象类型与内存
 *     类型，便于 zebra 做内存统计与泄漏排查；
 *   - ietf_srv6_types_info 是给 YANG 使用的模块信息（仅类型定义，无回调）。
 *
 * 建议阅读顺序：
 *   1) 顶部内存类型注册；
 *   2) action / context 的转字符串与 JSON 函数；
 *   3) locator 与 chunk 的管理函数；
 *   4) sid_format / sid_ctx 的分配释放。
 */
#include "zebra.h"

#include "srv6.h"
#include "log.h"
#include "lib/json.h"

/*
 * 注册 QOBJ 对象类型（srv6_locator / srv6_sid_format）以及本文件使用的内存
 * 类型（MTYPE_*）。前者供对象登记与调试计数，后者供内存统计与泄漏检测。
 */
DEFINE_QOBJ_TYPE(srv6_locator);
DEFINE_QOBJ_TYPE(srv6_sid_format);
DEFINE_MTYPE_STATIC(LIB, SRV6_LOCATOR, "SRV6 locator");
DEFINE_MTYPE_STATIC(LIB, SRV6_LOCATOR_CHUNK, "SRV6 locator chunk");
DEFINE_MTYPE_STATIC(LIB, SRV6_SID_FORMAT, "SRv6 SID format");
DEFINE_MTYPE_STATIC(LIB, SRV6_SID_CTX, "SRv6 SID context");

/*
 * seg6local_action2str_with_next_csid：把本地 Endpoint 行为动作转为字符串。
 * 参数：action 为 ZEBRA_SEG6_LOCAL_ACTION_*；has_next_csid 控制是否使用 uSID
 * 短名（uN / uA / uDT6 等）。
 * 返回静态字符串，未知动作返回 "unknown"，未指定返回 "unspec"。
 * 说明：此处是唯一的动作到字符串映射表，所有展示路径都汇集到这里。
 */
const char *seg6local_action2str_with_next_csid(uint32_t action, bool has_next_csid)
{
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		return has_next_csid ? "uN" : "End";
	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
		return has_next_csid ? "uA" : "End.X";
	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
		return has_next_csid ? "uDT" : "End.T";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
		return has_next_csid ? "uDX2" : "End.DX2";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		return has_next_csid ? "uDX6" : "End.DX6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		return has_next_csid ? "uDX4" : "End.DX4";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
		return has_next_csid ? "uDT6" : "End.DT6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
		return has_next_csid ? "uDT4" : "End.DT4";
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
		return has_next_csid ? "uB6" : "End.B6";
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		return has_next_csid ? "uB6.Encap" : "End.B6.Encap";
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
		return has_next_csid ? "uBM" : "End.BM";
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
		return "End.S";
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
		return "End.AS";
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
		return "End.AM";
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		return has_next_csid ? "uDT46" : "End.DT46";
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
		return "unspec";
	default:
		return "unknown";
	}
}

/*
 * seg6local_action2str：动作转字符串的简单封装（不使用 uSID 短名）。
 * 返回静态字符串。
 */
const char *seg6local_action2str(uint32_t action)
{
	return seg6local_action2str_with_next_csid(action, false);
}

/*
 * snprintf_seg6_segs：把 SID 序列格式化为逗号分隔的地址串。
 * 参数：str / size 输出缓冲区；segs 待格式化的 SID 序列。
 * 返回写入的字符串长度（strlen）。
 * 注意：会先把 str[0] 置 NUL，因此缓冲区始终以空串起算。
 */
int snprintf_seg6_segs(char *str,
		size_t size, const struct seg6_segs *segs)
{
	str[0] = '\0';
	for (size_t i = 0; i < segs->num_segs; i++) {
		char addr[INET6_ADDRSTRLEN];
		bool not_last = (i + 1) < segs->num_segs;

		inet_ntop(AF_INET6, &segs->segs[i], addr, sizeof(addr));
		strlcat(str, addr, size);
		strlcat(str, not_last ? "," : "", size);
	}
	return strlen(str);
}

/*
 * seg6local_flavors2json：把 flavor 信息以字符串数组形式写入 JSON。
 * 参数：json 目标对象；flv_info flavor 信息。
 * 副作用：在 json 上新增 "flavors" 数组，含 psp / usp / usd 中已置位者。
 */
static void seg6local_flavors2json(json_object *json, const struct seg6local_flavors_info *flv_info)
{
	json_object *json_flavors;

	json_flavors = json_object_new_array();
	json_object_object_add(json, "flavors", json_flavors);

	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP))
		json_array_string_add(json_flavors, "psp");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USP))
		json_array_string_add(json_flavors, "usp");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		json_array_string_add(json_flavors, "usd");
}

/*
 * srv6_sid_structure2json：把 SID 结构位长写入 JSON。
 * 参数：ctx 上下文（提供 block/node/function/argument 位长）；json 目标对象。
 * 副作用：在 json 上新增 blockLen / nodeLen / funcLen / argLen 四个整数。
 */
void srv6_sid_structure2json(const struct seg6local_context *ctx, json_object *json)
{
	json_object_int_add(json, "blockLen", ctx->block_len);
	json_object_int_add(json, "nodeLen", ctx->node_len);
	json_object_int_add(json, "funcLen", ctx->function_len);
	json_object_int_add(json, "argLen", ctx->argument_len);
}

/*
 * seg6local_context2json：把 seg6local 上下文按行为写入 JSON。
 * 参数：ctx 上下文；action 行为动作；json 目标对象。
 * 副作用：先写 flavors 数组，再按 action 写 nh6 / nh4 / table / interfaceName 或
 * 标记 unknown 等字段。不同 action 输出的字段集合不同。
 */
void seg6local_context2json(const struct seg6local_context *ctx,
			    uint32_t action, json_object *json)
{
	seg6local_flavors2json(json, &ctx->flv);
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
		json_object_string_addf(json, "nh6", "%pI6", &ctx->nh6);
		json_object_string_add(json, "interfaceName",
				       ifindex2ifname(ctx->ifindex, VRF_DEFAULT));
		json_object_int_add(json, "interfaceIndex", ctx->ifindex);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		json_object_string_addf(json, "nh6", "%pI6", &ctx->nh6);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		json_object_string_addf(json, "nh4", "%pI4", &ctx->nh4);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		json_object_int_add(json, "table", ctx->table);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
		json_object_boolean_add(json, "none", true);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		json_object_string_addf(json, "nh6", "%pI6", &ctx->nh6);
		return;
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
	default:
		json_object_boolean_add(json, "unknown", true);
		return;
	}
}

/*
 * seg6local_flavors2str：把 flavor 信息格式化为 "(PSP/USP/USD)" 形式的字符串。
 * 参数：str / size 输出缓冲区；flv_info flavor 信息。
 * 返回 str。若未设置任何已知 flavor 则原样返回（不写括号）。
 * 注意：内部用 first 标志控制分隔符，顺序为 PSP、USP、USD。
 */
static char *seg6local_flavors2str(char *str, size_t size,
				   const struct seg6local_flavors_info *flv_info)
{
	size_t len = 0;
	bool first = true;

	if (!CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP |
							  ZEBRA_SEG6_LOCAL_FLV_OP_USP |
							  ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		return str;

	len += snprintf(str + len, size - len, "(");
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_PSP)) {
		/*
		 * First is never null for this one.  If you reorder ensure
		 * that we properly touch first here in the snprintf
		 */
		len += snprintf(str + len, size - len, "%sPSP", "");
		first = false;
	}
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USP)) {
		len += snprintf(str + len, size - len, "%sUSP", first ? "" : "/");
		first = false;
	}
	if (CHECK_SRV6_FLV_OP(flv_info->flv_ops, ZEBRA_SEG6_LOCAL_FLV_OP_USD))
		len += snprintf(str + len, size - len, "%sUSD", first ? "" : "/");

	snprintf(str + len, size - len, ")");

	return str;
}
/*
 * seg6local_context2str：把 seg6local 上下文按行为格式化为可读字符串。
 * 参数：str / size 输出缓冲区；ctx 上下文；action 行为动作。
 * 返回 str。先格式化 flavor 后缀，再按 action 拼接 nh6 / nh4 / table / oif 等。
 * 注意：END_DX2 会用 if_lookup_by_index 查接口名，查不到显示 "<unknown>"。
 */
const char *seg6local_context2str(char *str, size_t size,
				  const struct seg6local_context *ctx,
				  uint32_t action)
{
	char flavor[SRV6_FLAVORS_STRLEN], *p_flavor;
	struct interface *ifp;

	flavor[0] = '\0';
	p_flavor = seg6local_flavors2str(flavor, sizeof(flavor), &ctx->flv);
	switch (action) {
	case ZEBRA_SEG6_LOCAL_ACTION_END:
		snprintf(str, size, "%s", p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_X:
		snprintfrr(str, size, "nh6 %pI6%s, %s", &ctx->nh6, p_flavor,
			   ifindex2ifname(ctx->ifindex, VRF_DEFAULT));
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_DX6:
		snprintfrr(str, size, "nh6 %pI6%s", &ctx->nh6, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_DX4:
		snprintfrr(str, size, "nh4 %pI4%s", &ctx->nh4, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_T:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT4:
	case ZEBRA_SEG6_LOCAL_ACTION_END_DT46:
		snprintf(str, size, "table %u%s", ctx->table, p_flavor);
		return str;

	case ZEBRA_SEG6_LOCAL_ACTION_END_B6:
	case ZEBRA_SEG6_LOCAL_ACTION_END_B6_ENCAP:
		snprintfrr(str, size, "nh6 %pI6%s", &ctx->nh6, p_flavor);
		return str;
	case ZEBRA_SEG6_LOCAL_ACTION_END_DX2:
		ifp = if_lookup_by_index(ctx->ifindex, VRF_DEFAULT);
		snprintfrr(str, size, "oif %s", ifp ? ifp->name : "<unknown>");
		return str;
	case ZEBRA_SEG6_LOCAL_ACTION_END_BM:
	case ZEBRA_SEG6_LOCAL_ACTION_END_S:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AS:
	case ZEBRA_SEG6_LOCAL_ACTION_END_AM:
	case ZEBRA_SEG6_LOCAL_ACTION_UNSPEC:
	default:
		snprintf(str, size, "unknown(%s)", __func__);
		return str;
	}
}

/*
 * srv6_locator_chunk_list_free：locator->chunks 列表的删除回调。
 * 参数：data 为 list 元素（struct srv6_locator_chunk *），以 void * 传入。
 * 转调 srv6_locator_chunk_free 释放。
 */
void srv6_locator_chunk_list_free(void *data)
{
	struct srv6_locator_chunk *chunk = data;

	srv6_locator_chunk_free(&chunk);
}

/*
 * srv6_locator_alloc：分配并初始化一个 srv6_locator。
 * 参数：name locator 名（截断拷贝到 name 缓冲区）。
 * 返回新分配的 locator 指针（内部已建 chunks 空列表并登记 QOBJ）。
 * 失败时会断言退出（XCALLOC 语义）。
 */
struct srv6_locator *srv6_locator_alloc(const char *name)
{
	struct srv6_locator *locator = NULL;

	locator = XCALLOC(MTYPE_SRV6_LOCATOR, sizeof(struct srv6_locator));
	strlcpy(locator->name, name, sizeof(locator->name));
	locator->chunks = list_new();
	locator->chunks->del = srv6_locator_chunk_list_free;

	QOBJ_REG(locator, srv6_locator);
	return locator;
}

/*
 * srv6_locator_chunk_alloc：分配一个零初始化的 locator chunk。
 * 返回新分配的 chunk 指针（内容全零，需调用者填充字段）。
 */
struct srv6_locator_chunk *srv6_locator_chunk_alloc(void)
{
	struct srv6_locator_chunk *chunk = NULL;

	chunk = XCALLOC(MTYPE_SRV6_LOCATOR_CHUNK,
			sizeof(struct srv6_locator_chunk));
	return chunk;
}

/*
 * srv6_locator_copy：把 locator 的各字段逐项拷贝到 copy。
 * 参数：copy 目标（已分配）；locator 源。
 * 注意：只拷贝标量字段（名字、前缀、位长、算法号、current、status_up、flags），
 * 不拷贝 chunks 列表与 sid_format / sid_block 指针。
 */
void srv6_locator_copy(struct srv6_locator *copy,
		       const struct srv6_locator *locator)
{
	strlcpy(copy->name, locator->name, sizeof(locator->name));
	copy->prefix = locator->prefix;
	copy->block_bits_length = locator->block_bits_length;
	copy->node_bits_length = locator->node_bits_length;
	copy->function_bits_length = locator->function_bits_length;
	copy->argument_bits_length = locator->argument_bits_length;
	copy->algonum = locator->algonum;
	copy->current = locator->current;
	copy->status_up = locator->status_up;
	copy->flags = locator->flags;
}

/*
 * srv6_locator_free：释放 locator。
 * 参数：locator 可为 NULL（此时直接返回）。
 * 副作用：注销 QOBJ，删除 chunks 列表（会触发 chunk 释放回调），再释放本体。
 */
void srv6_locator_free(struct srv6_locator *locator)
{
	if (locator) {
		QOBJ_UNREG(locator);
		list_delete(&locator->chunks);

		XFREE(MTYPE_SRV6_LOCATOR, locator);
	}
}

/*
 * srv6_locator_chunk_free：释放 chunk 本体并把调用者指针置空。
 * 参数：chunk 指向 chunk 指针的指针（二级指针）。
 */
void srv6_locator_chunk_free(struct srv6_locator_chunk **chunk)
{
	XFREE(MTYPE_SRV6_LOCATOR_CHUNK, *chunk);
}

/*
 * srv6_sid_format_alloc：分配并初始化一个 srv6_sid_format。
 * 参数：name 格式名（截断拷贝）。
 * 返回新分配的 format 指针（已登记 QOBJ）。
 */
struct srv6_sid_format *srv6_sid_format_alloc(const char *name)
{
	struct srv6_sid_format *format = NULL;

	format = XCALLOC(MTYPE_SRV6_SID_FORMAT, sizeof(struct srv6_sid_format));
	strlcpy(format->name, name, sizeof(format->name));

	QOBJ_REG(format, srv6_sid_format);
	return format;
}

/*
 * srv6_sid_format_free：释放 srv6_sid_format。
 * 参数：format 可为 NULL（此时直接返回）。
 * 副作用：注销 QOBJ 并释放本体。
 */
void srv6_sid_format_free(struct srv6_sid_format *format)
{
	if (!format)
		return;

	QOBJ_UNREG(format);
	XFREE(MTYPE_SRV6_SID_FORMAT, format);
}

/**
 * Free an SRv6 SID format.
 *
 * @param val SRv6 SID format to be freed
 */
/*
 * delete_srv6_sid_format：list 删除回调，把 void * 参数转回 srv6_sid_format
 * 并调用 srv6_sid_format_free 释放。
 */
void delete_srv6_sid_format(void *val)
{
	srv6_sid_format_free((struct srv6_sid_format *)val);
}

/*
 * srv6_sid_ctx_alloc：分配并填充一个 srv6_sid_ctx。
 * 参数：behavior 本地行为；nh4 / nh6 下一跳（可为 NULL，为 NULL 则保持零值）；
 *       vrf_id 所属 VRF（为 0 则保持零值）。
 * 返回新分配的 ctx 指针。
 */
struct srv6_sid_ctx *srv6_sid_ctx_alloc(enum seg6local_action_t behavior,
					struct in_addr *nh4,
					struct in6_addr *nh6, vrf_id_t vrf_id)
{
	struct srv6_sid_ctx *ctx = NULL;

	ctx = XCALLOC(MTYPE_SRV6_SID_CTX, sizeof(struct srv6_sid_ctx));
	ctx->behavior = behavior;
	if (nh4)
		ctx->nh4 = *nh4;
	if (nh6)
		ctx->nh6 = *nh6;
	if (vrf_id)
		ctx->vrf_id = vrf_id;

	return ctx;
}

/*
 * srv6_sid_ctx_free：释放 srv6_sid_ctx。
 * 参数：ctx 待释放对象（内部 XFREE 允许 NULL）。
 */
void srv6_sid_ctx_free(struct srv6_sid_ctx *ctx)
{
	XFREE(MTYPE_SRV6_SID_CTX, ctx);
}

/*
 * srv6_locator_chunk_json：把 chunk 导出为精简 JSON。
 * 参数：chunk 待导出对象。
 * 返回新分配的 JSON 对象（含 prefix 与 proto）。
 */
json_object *srv6_locator_chunk_json(const struct srv6_locator_chunk *chunk)
{
	json_object *jo_root = NULL;

	jo_root = json_object_new_object();
	json_object_string_addf(jo_root, "prefix", "%pFX", &chunk->prefix);
	json_object_string_add(jo_root, "proto",
			       zebra_route_string(chunk->proto));

	return jo_root;
}

/*
 * srv6_locator_chunk_detailed_json：把 chunk 导出为详细 JSON。
 * 参数：chunk 待导出对象。
 * 返回新分配的 JSON 对象，含 prefix、四个 bits length、keep、proto、
 * instance、sessionId 等字段。
 */
json_object *
srv6_locator_chunk_detailed_json(const struct srv6_locator_chunk *chunk)
{
	json_object *jo_root = NULL;

	jo_root = json_object_new_object();

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &chunk->prefix);

	/* set block_bits_length */
	json_object_int_add(jo_root, "blockBitsLength",
			    chunk->block_bits_length);

	/* set node_bits_length */
	json_object_int_add(jo_root, "nodeBitsLength", chunk->node_bits_length);

	/* set function_bits_length */
	json_object_int_add(jo_root, "functionBitsLength",
			    chunk->function_bits_length);

	/* set argument_bits_length */
	json_object_int_add(jo_root, "argumentBitsLength",
			    chunk->argument_bits_length);

	/* set keep */
	json_object_int_add(jo_root, "keep", chunk->keep);

	/* set proto */
	json_object_string_add(jo_root, "proto",
			       zebra_route_string(chunk->proto));

	/* set instance */
	json_object_int_add(jo_root, "instance", chunk->instance);

	/* set session_id */
	json_object_int_add(jo_root, "sessionId", chunk->session_id);

	return jo_root;
}

/*
 * srv6_locator_json：把 locator 导出为精简 JSON。
 * 参数：loc 待导出对象。
 * 返回新分配的 JSON 对象，含 name、prefix、四个 bits length、behavior（当为
 * uSID locator 时）、statusUp 以及 chunks 数组。
 * 注意：bits length 优先取自 sid_format，否则取 locator 自身字段。
 */
json_object *srv6_locator_json(const struct srv6_locator *loc)
{
	struct listnode *node;
	struct srv6_locator_chunk *chunk;
	json_object *jo_root = NULL;
	json_object *jo_chunk = NULL;
	json_object *jo_chunks = NULL;

	jo_root = json_object_new_object();

	/* set name */
	json_object_string_add(jo_root, "name", loc->name);

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &loc->prefix);

	if (loc->sid_format) {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->sid_format->block_len);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->sid_format->node_len);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->sid_format->function_len);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->sid_format->argument_len);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (loc->sid_format->type == SRV6_SID_FORMAT_TYPE_USID)
			json_object_string_add(jo_root, "behavior", "usid");
	} else {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->block_bits_length);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->node_bits_length);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->function_bits_length);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->argument_bits_length);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (CHECK_FLAG(loc->flags, SRV6_LOCATOR_USID))
			json_object_string_add(jo_root, "behavior", "usid");
	}

	/* set status_up */
	json_object_boolean_add(jo_root, "statusUp",
				loc->status_up);

	/* set chunks */
	jo_chunks = json_object_new_array();
	json_object_object_add(jo_root, "chunks", jo_chunks);
	for (ALL_LIST_ELEMENTS_RO((struct list *)loc->chunks, node, chunk)) {
		jo_chunk = srv6_locator_chunk_json(chunk);
		json_object_array_add(jo_chunks, jo_chunk);
	}

	return jo_root;
}

/*
 * srv6_locator_detailed_json：把 locator 导出为详细 JSON。
 * 参数：loc 待导出对象。
 * 返回新分配的 JSON 对象，在精简版基础上多出 algoNum，
 * 且 chunks 使用 detailed 版本（含更多字段）。
 */
json_object *srv6_locator_detailed_json(const struct srv6_locator *loc)
{
	struct listnode *node;
	struct srv6_locator_chunk *chunk;
	json_object *jo_root = NULL;
	json_object *jo_chunk = NULL;
	json_object *jo_chunks = NULL;

	jo_root = json_object_new_object();

	/* set name */
	json_object_string_add(jo_root, "name", loc->name);

	/* set prefix */
	json_object_string_addf(jo_root, "prefix", "%pFX", &loc->prefix);

	if (loc->sid_format) {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->sid_format->block_len);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->sid_format->node_len);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->sid_format->function_len);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->sid_format->argument_len);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (loc->sid_format->type == SRV6_SID_FORMAT_TYPE_USID)
			json_object_string_add(jo_root, "behavior", "usid");
	} else {
		/* set block_bits_length */
		json_object_int_add(jo_root, "blockBitsLength",
				    loc->block_bits_length);

		/* set node_bits_length */
		json_object_int_add(jo_root, "nodeBitsLength",
				    loc->node_bits_length);

		/* set function_bits_length */
		json_object_int_add(jo_root, "functionBitsLength",
				    loc->function_bits_length);

		/* set argument_bits_length */
		json_object_int_add(jo_root, "argumentBitsLength",
				    loc->argument_bits_length);

		/* set true if the locator is a Micro-segment (uSID) locator */
		if (CHECK_FLAG(loc->flags, SRV6_LOCATOR_USID))
			json_object_string_add(jo_root, "behavior", "usid");
	}

	/* set algonum */
	json_object_int_add(jo_root, "algoNum", loc->algonum);

	/* set status_up */
	json_object_boolean_add(jo_root, "statusUp", loc->status_up);

	/* set chunks */
	jo_chunks = json_object_new_array();
	json_object_object_add(jo_root, "chunks", jo_chunks);
	for (ALL_LIST_ELEMENTS_RO((struct list *)loc->chunks, node, chunk)) {
		jo_chunk = srv6_locator_chunk_detailed_json(chunk);
		json_object_array_add(jo_chunks, jo_chunk);
	}

	return jo_root;
}

/* clang-format off */
/*
 * ietf_srv6_types_info：ietf-srv6-types YANG 模块信息。该模块只提供类型定义，
 * 无配置节点（xpath 为空）并忽略配置回调。供需要引用其中 typedef 的模块使用。
 */
const struct frr_yang_module_info ietf_srv6_types_info = {
	.name = "ietf-srv6-types",
	.ignore_cfg_cbs = true,
	.nodes = { { .xpath = NULL } },
};
/* clang-format on */

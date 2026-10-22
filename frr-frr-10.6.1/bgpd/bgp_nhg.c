// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP Nexthop Group Support
 * Copyright (C) 2023 NVIDIA Corporation
 * Copyright (C) 2023 6WIND
 */

/*
 * 文件导读：BGP 下一跳组（Nexthop Group, NHG）ID 分配器
 *
 * 本模块为 BGP 使用的 L3 下一跳组（L3 NHG）提供 ID 的申请与释放接口。
 * L3 NHG 用于数据面（dplane）中对远端下一跳做快速故障切换；bgpd 这里只
 * 负责维护一段 ID 位图的分配，NHG 自身的增删改由 Zebra 侧的应用托管。
 * 目前唯一的使用者是 EVPN 主机路由（host route），用于远端 ES 链路的
 * 快速失败切换。
 *
 * 与相邻模块的配合：
 *   - 通过 zclient_get_nhg_start 取得 Zebra 为本路由类型（BGP）预留的 NHG
 *     ID 起始段，再叠加本地位图内的偏移，得到全局唯一的 NHG ID。
 *   - 通过 nexthop_group_init 注册一组回调，供 nexthop-group 配置命令框架
 *     在命令行增删改时通知 bgpd；当前这些回调均为空实现。
 *
 * 读的顺序：先看两个静态变量与位图区间，再看 init / finish 的生命周期，
 * 最后看 id_alloc / id_free 的分配与释放逻辑。
 */

#include <zebra.h>

#include <bgpd/bgpd.h>
#include <bgpd/bgp_debug.h>
#include <bgpd/bgp_nhg.h>


/****************************************************************************
 * L3 NHGs are used for fast failover of nexthops in the dplane. These are
 * the APIs for allocating L3 NHG ids. Management of the L3 NHG itself is
 * left to the application using it.
 * PS: Currently EVPN host routes is the only app using L3 NHG for fast
 * failover of remote ES links.
 ***************************************************************************/
/*
 * 全局的 L3 NHG ID 位图：bit N 置位表示本地偏移 N 对应的 NHG ID 已被占用。
 * 真正的全局 ID 等于 bgp_nhg_start 加上这里的偏移值。
 */
static bitfield_t bgp_nh_id_bitmap;
/* Zebra 为 BGP 路由类型预留的 NHG ID 起始值，由 zclient 汇报。 */
static uint32_t bgp_nhg_start;

/* XXX - currently we do nothing on the callbacks */
static void bgp_nhg_add_cb(const char *name)
{
}

static void bgp_nhg_modify_cb(const struct nexthop_group_cmd *nhgc)
{
}

static void bgp_nhg_add_nexthop_cb(const struct nexthop_group_cmd *nhgc,
				   const struct nexthop *nhop)
{
}

static void bgp_nhg_del_nexthop_cb(const struct nexthop_group_cmd *nhgc,
				   const struct nexthop *nhop)
{
}

static void bgp_nhg_del_cb(const char *name)
{
}

/*
 * 惰性初始化与 Zebra 的联动：仅执行一次。取得 Zebra 为本路由类型预留的
 * NHG ID 起始编号，并把下面这组空回调注册进 nexthop-group 命令框架。
 * 之所以做成惰性（首用才初始化），是因为分配 ID 时 Zebra 连接可能尚未就绪。
 */
static void bgp_nhg_zebra_init(void)
{
	static bool bgp_nhg_zebra_inited;

	if (bgp_nhg_zebra_inited)
		return;

	bgp_nhg_zebra_inited = true;
	bgp_nhg_start = zclient_get_nhg_start(ZEBRA_ROUTE_BGP);
	nexthop_group_init(bgp_nhg_add_cb, bgp_nhg_modify_cb,
			   bgp_nhg_add_nexthop_cb, bgp_nhg_del_nexthop_cb,
			   bgp_nhg_del_cb);
}

/*
 * 初始化 NHG ID 位图。容量取 Zebra 每协议预留间距减一与 16K 的较小值，
 * 随后把索引 0 标记为已占用作为哨兵，避免分配出 0 这个无效 ID。
 * 之所以要留出哨兵，是因为 0 在返回值里表示“分配失败 / 无 ID”。
 * 当开启相关调试题时打印本次可用的 ID 区间，便于排查。
 */
void bgp_nhg_init(void)
{
	uint32_t id_max;

	id_max = MIN(ZEBRA_NHG_PROTO_SPACING - 1, 16 * 1024);
	bf_init(bgp_nh_id_bitmap, id_max);
	bf_assign_zero_index(bgp_nh_id_bitmap);

	if (BGP_DEBUG(nht, NHT) || BGP_DEBUG(evpn_mh, EVPN_MH_ES))
		zlog_debug("bgp nhg range %u - %u", bgp_nhg_start + 1,
			   bgp_nhg_start + id_max);
}

/*
 * 释放 NHG ID 位图占用的内存，在 bgpd 退出流程中调用一次。
 */
void bgp_nhg_finish(void)
{
	bf_free(bgp_nh_id_bitmap);
}

/*
 * 申请一个全局唯一的 L3 NHG ID。先做惰性 Zebra 联动初始化，再从位图取一个
 * 空闲本地偏移，加上 bgp_nhg_start 得到全局 ID。返回 0 表示分配失败或未成功
 * 初始化（调用方需把 0 当作“无 ID”处理，切勿把 0 当有效 ID 使用）。
 */
uint32_t bgp_nhg_id_alloc(void)
{
	uint32_t nhg_id = 0;

	bgp_nhg_zebra_init();
	bf_assign_index(bgp_nh_id_bitmap, nhg_id);
	if (nhg_id)
		nhg_id += bgp_nhg_start;

	return nhg_id;
}

/*
 * 归还一个此前由 bgp_nhg_id_alloc 分配的 NHG ID。对 0 或落在起始值之前
 * （即不在本模块区间内）的 ID 直接忽略，保证重复释放或传入外部 ID 时安全。
 * 内部先把全局 ID 换算回本地偏移，再释放位图对应位。
 */
void bgp_nhg_id_free(uint32_t nhg_id)
{
	if (!nhg_id || (nhg_id <= bgp_nhg_start))
		return;

	nhg_id -= bgp_nhg_start;

	bf_release_index(bgp_nh_id_bitmap, nhg_id);
}

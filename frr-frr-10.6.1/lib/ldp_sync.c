// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ldp_sync.c: LDP-SYNC handling routines
 * Copyright (C) 2020 Volta Networks, Inc.
 */

/*
 * ============================================================================
 * 文件导读：lib/ldp_sync.c —— LDP-IGP 同步的公共实现
 * ============================================================================
 * 本文件实现 ldp_sync.h 中声明的少量通用接口：创建 / 释放同步信息、判断接口
 * 是否处于“已启用且未就绪”状态、以及停止某接口的同步。更完整的同步状态机
 * （ZAPI 收发、度量调整）位于各 IGP 守护进程（ospfd / ospf6d / isisd）一侧，
 * 本文件只提供跨协议复用的最小公共部分。
 *
 * 要点：
 *   - ldp_sync_info_create 用零值加默认常量初始化；
 *   - ldp_sync_if_is_enabled 关注 enabled 与 state 两个字段的组合；
 *   - ldp_sync_if_down 会取消 holddown 定时器并把状态回退。
 *
 * 与其他模块的关系：内存类型 LDP_SYNC_INFO 在此注册；结构体定义见 ldp_sync.h，
 * 并被 ZAPI 复用。
 *
 * 建议阅读顺序：自顶向下（create、free、if_is_enabled、if_down）。
 */
#include <zebra.h>

#include "command.h"
#include "memory.h"
#include "prefix.h"
#include "log.h"
#include "frrevent.h"
#include "stream.h"
#include "zclient.h"
#include "table.h"
#include "vty.h"
#include "ldp_sync.h"

/* Library code */
/*
 * 注册本文件使用的内存类型 LDP_SYNC_INFO（ldp_sync_info 的分配 / 释放）。
 */
DEFINE_MTYPE_STATIC(LIB, LDP_SYNC_INFO, "LDP SYNC info");

/*
 * ldp_sync_info_create - Allocate the LDP_SYNC information
 */
/*
 * ldp_sync_info_create：分配并初始化一个 ldp_sync_info。
 * 返回新对象：flags=0、enabled=LDP_IGP_SYNC_DEFAULT、
 * state=NOT_REQUIRED、holddown=缺省、t_holddown=NULL。
 * 内存不足时 XCALLOC 会断言退出。
 */
struct ldp_sync_info *ldp_sync_info_create(void)
{
	struct ldp_sync_info *ldp_sync_info;

	ldp_sync_info = XCALLOC(MTYPE_LDP_SYNC_INFO,
				sizeof(struct ldp_sync_info));
	assert(ldp_sync_info);

	ldp_sync_info->flags = 0;
	ldp_sync_info->enabled = LDP_IGP_SYNC_DEFAULT;
	ldp_sync_info->state = LDP_IGP_SYNC_STATE_NOT_REQUIRED;
	ldp_sync_info->holddown = LDP_IGP_SYNC_HOLDDOWN_DEFAULT;
	ldp_sync_info->t_holddown = NULL;
	return ldp_sync_info;
}

/*
 * ldp_sync_info_free - Free the LDP_SYNC information.
 */
/*
 * ldp_sync_info_free：释放 ldp_sync_info 并置空调用者指针。
 * 参数：ldp_sync_info 指向对象指针的指针；若指向 NULL 则无操作。
 */
void ldp_sync_info_free(struct ldp_sync_info **ldp_sync_info)
{
	if (*ldp_sync_info)
		XFREE(MTYPE_LDP_SYNC_INFO, *ldp_sync_info);
}

/*
 * ldp_sync_if_is_enabled：判断接口当前是否处于 LDP-SYNC 抑制状态。
 * 返回 true 当且仅当：ldp_sync_info 非空、enabled 为已启用、且 state 为
 * REQUIRED_NOT_UP（即需要同步但尚未就绪）。
 */
bool ldp_sync_if_is_enabled(struct ldp_sync_info *ldp_sync_info)
{
	/* return true if LDP-SYNC is configured on this interface */
	if (ldp_sync_info &&
	    ldp_sync_info->enabled == LDP_IGP_SYNC_ENABLED &&
	    ldp_sync_info->state == LDP_IGP_SYNC_STATE_REQUIRED_NOT_UP)
		return true;

	return false;
}

/*
 * ldp_sync_if_down：停止该接口的 LDP-SYNC。
 * 参数：ldp_sync_info 同步信息（可为 NULL）。
 * 返回 true 表示执行了停止（原本已启用），否则 false。
 * 副作用：取消 holddown 定时器；若状态为 REQUIRED_UP 则回退为 REQUIRED_NOT_UP。
 */
bool ldp_sync_if_down(struct ldp_sync_info *ldp_sync_info)
{
	/* Stop LDP-SYNC on this interface:
	 *   if holddown timer is running stop it
	 *   update state
	 */
	if (ldp_sync_info && ldp_sync_info->enabled == LDP_IGP_SYNC_ENABLED) {
		event_cancel(&ldp_sync_info->t_holddown);

		if (ldp_sync_info->state == LDP_IGP_SYNC_STATE_REQUIRED_UP)
			ldp_sync_info->state =
				LDP_IGP_SYNC_STATE_REQUIRED_NOT_UP;
		return true;
	}

	return false;
}

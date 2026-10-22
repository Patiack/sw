// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VRF functions.
 * Copyright (C) 2014 6WIND S.A.
 */

/* ============================================================================
 * 【文件导读】vrf.c —— VRF 实例的注册、查找、生命周期与 netns 后端切换
 * ============================================================================
 * 【这个文件干什么】实现 vrf.h 声明的接口：维护两张全局索引(vrfs_by_id 与
 * vrfs_by_name)，负责 VRF 的取得/创建/更新/删除，驱动 VRF 的启用与停用，并
 * 提供以 VRF 为作用域建 socket、bind、getaddrinfo、ioctl 的封装，以及一套
 * 北向(YANG)回调把 /frr-vrf:lib/vrf 配置映射到 struct vrf。
 *
 * 【两种后端】vrf_backend 决定语义：
 *   VRF_BACKEND_VRF_LITE —— 用内核 VRF 设备与 table_id，socket 靠 SO_BINDTODEVICE；
 *   VRF_BACKEND_NETNS    —— 每个 VRF 一个网络命名空间，靠 setns 切换。
 * 切换类函数在非 netns 后端下多为 no-op。
 *
 * 【事件分发】vrf_master 保存四个钩子(create/enable/disable/delete)，由
 * vrf_init() 从各协议模块传入；VRF 状态变化时回调它们通知协议模块。
 *
 * 【注意点】vrf_get 既查又建；vrf_delete 是“有条件释放”(有用户配置或有接口
 * 时只解除 ID 映射)；状态变化经 vrf_update_state 走北向通知。
 *
 * 【阅读顺序】全局变量 → 比较/查找函数 → vrf_get / vrf_update / vrf_delete →
 * enable/disable/iterate → 位图 → CLI(DEFUN)→ 网络操作 → 北向回调表。
 * ============================================================================
 */
#include <zebra.h>
#include <sys/ioctl.h>

#include "if.h"
#include "vrf.h"
#include "vrf_int.h"
#include "prefix.h"
#include "table.h"
#include "log.h"
#include "memory.h"
#include "command.h"
#include "ns.h"
#include "privs.h"
#include "nexthop_group.h"
#include "lib_errors.h"
#include "northbound.h"
#include "northbound_cli.h"

/* 【全局开关】VRF 运行状态变更是否要发北向通知，由属主(zebra)设置。 */
/* Set by the owner (zebra). */
bool vrf_notify_oper_changes;

/* 【宏】非 netns 后端下，默认 VRF 使用的内部名字。 */
/* default VRF name value used when VRF backend is not NETNS */
#define VRF_DEFAULT_NAME_INTERNAL "default"

/* 【内存类型注册】为 VRF 结构与其位图注册内存记账类型，便于内存统计与排查。 */
DEFINE_MTYPE_STATIC(LIB, VRF, "VRF");
DEFINE_MTYPE_STATIC(LIB, VRF_BITMAP, "VRF bit-map");

/* 为本编译单元展开 vrf 的 QOBJ 类型定义(与头文件 DECLARE_QOBJ_TYPE 配对)。 */
DEFINE_QOBJ_TYPE(vrf);

/* 【前向声明】两棵树的比较函数在本文件后面定义，先行声明供 RB_GENERATE 使用。 */
static __inline int vrf_id_compare(const struct vrf *, const struct vrf *);
static __inline int vrf_name_compare(const struct vrf *, const struct vrf *);

/* 【红黑树生成】为按 ID、按名字的两棵树生成实现(依赖上面的比较函数)。 */
RB_GENERATE(vrf_id_head, vrf, id_entry, vrf_id_compare);
RB_GENERATE(vrf_name_head, vrf, name_entry, vrf_name_compare);

/* 【全局树实例】所有 VRF 的 ID 索引树与名字索引树。 */
struct vrf_id_head vrfs_by_id = RB_INITIALIZER(&vrfs_by_id);
struct vrf_name_head vrfs_by_name = RB_INITIALIZER(&vrfs_by_name);

/* 【全局状态】当前 VRF 后端类型，默认 VRF-lite，可由 vrf_configure_backend 更改。 */
static enum vrf_backend_type vrf_backend = VRF_BACKEND_VRF_LITE;
/* 【全局状态】默认 VRF 名字，默认 "default"，可经 vrf_set_default_name 修改。 */
static char vrf_default_name[VRF_NAMSIZ] = VRF_DEFAULT_NAME_INTERNAL;

/* 【全局状态】VRF 调试日志开关(由 debug vrf 命令控制)。 */
/*
 * Turn on/off debug code
 * for vrf.
 */
static int debug_vrf = 0;

/* 【事件钩子表】保存上层(各协议模块)注册的四个回调，VRF 生命周期变化时调用；
 * 由 vrf_init() 填充。 */
/* Holding VRF hooks  */
static struct vrf_master {
	int (*vrf_new_hook)(struct vrf *);
	int (*vrf_delete_hook)(struct vrf *);
	int (*vrf_enable_hook)(struct vrf *);
	int (*vrf_disable_hook)(struct vrf *);
} vrf_master = {
	0,
};

/* 【前向声明】VRF 是否启用的判定函数(本文件后面定义)。 */
static int vrf_is_enabled(struct vrf *vrf);

/* 【查找】按名字在名字树里查找 VRF，返回指针或 NULL。
 * 做法：在栈上放一个只填了 name 的临时 struct vrf 当键，交给 RB_FIND。 */
/* VRF list existance check by name. */
struct vrf *vrf_lookup_by_name(const char *name)
{
	struct vrf vrf;
	strlcpy(vrf.name, name, sizeof(vrf.name));
	return (RB_FIND(vrf_name_head, &vrfs_by_name, &vrf));
}

/* 【比较】ID 树的排序函数：返回 a->vrf_id 减 b->vrf_id 的差值，供红黑树使用。 */
static __inline int vrf_id_compare(const struct vrf *a, const struct vrf *b)
{
	return (a->vrf_id - b->vrf_id);
}

/* 【比较】名字树的排序函数：按名字字符串 strcmp，供红黑树使用。 */
static int vrf_name_compare(const struct vrf *a, const struct vrf *b)
{
	return strcmp(a->name, b->name);
}

/* 【切换】切入 vrf_id 对应 VRF 的网络命名空间。
 * 返回：1 表示是默认 VRF(无需切换)；2 表示该 VRF 无 netns 后端(无需切换)；
 * 否则返回 ns_switch_to_netns 的结果(0 成功、负值失败)。
 * 【易踩的坑】返回值语义分段，1 与 2 都代表“无需切换”，上层判断勿只比 0。 */
int vrf_switch_to_netns(vrf_id_t vrf_id)
{
	char *name;
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);

	/* VRF is default VRF. silently ignore */
	if (!vrf || vrf->vrf_id == VRF_DEFAULT)
		return 1;	/* 1 = default */
	/* VRF has no NETNS backend. silently ignore */
	if (vrf->data.l.netns_name[0] == '\0')
		return 2;	/* 2 = no netns */
	name = ns_netns_pathname(NULL, vrf->data.l.netns_name);
	if (debug_vrf)
		zlog_debug("VRF_SWITCH: %s(%u)", name, vrf->vrf_id);
	return ns_switch_to_netns(name);
}

/* 【切换】切回初始(默认)命名空间。返回 0 成功、负值失败。 */
int vrf_switchback_to_initial(void)
{
	int ret = ns_switchback_to_initial();

	if (ret == 0 && debug_vrf)
		zlog_debug("VRF_SWITCHBACK");
	return ret;
}

/* 【通知】若开启北向通知且 VRF 有名字，则发一条
 * /frr-vrf:lib/vrf[name=...]/state 的状态变更通知。
 * 未开通知或无名字时静默返回。 */
static void vrf_update_state(struct vrf *vrf)
{
	if (!vrf_notify_oper_changes || vrf->name[0] == '\0')
		return;

	nb_notif_addf("/frr-vrf:lib/vrf[name=\"%s\"]/state", vrf->name);
}

/* 【取得或创建】按 vrf_id 与 name 取得 VRF，不存在则创建。
 *   名称与 ID 均可为 NULL 或 VRF_UNKNOWN；但两者都缺失时返回 NULL。
 *   若按名字找到的 VRF 与传入 ID 冲突(名字已存在但 ID 不同)，返回 NULL 拒绝。
 * 【要点】会在缺 ID / 名字时补齐：补 ID 时插入 ID 树，补或改名字时维护名字树，
 *   并在状态变化时发北向通知；新建成功后再调用 vrf_master 的 new 钩子。
 * 【易踩的坑】“已存在且名字发生变化”会先发删除通知，改名字后再发添加通知。 */
/* Get a VRF. If not found, create one.
 * Arg:
 *   name   - The name of the vrf.  May be NULL if unknown.
 *   vrf_id - The vrf_id of the vrf.  May be VRF_UNKNOWN if unknown
 * Description: Please note that this routine can be called with just the name
 * and 0 vrf-id
 */
struct vrf *vrf_get(vrf_id_t vrf_id, const char *name)
{
	struct vrf *vrf = NULL;
	int new = 0;

	/* Nothing to see, move along here */
	if (!name && vrf_id == VRF_UNKNOWN)
		return NULL;

	/* attempt to find already available VRF
	 */
	if (name)
		vrf = vrf_lookup_by_name(name);
	if (vrf && vrf_id != VRF_UNKNOWN
	    && vrf->vrf_id != VRF_UNKNOWN
	    && vrf->vrf_id != vrf_id) {
		zlog_debug("VRF_GET: avoid %s creation(%u), same name exists (%u)",
			   name, vrf_id, vrf->vrf_id);
		return NULL;
	}
	/* Try to find VRF both by ID and name */
	if (!vrf && vrf_id != VRF_UNKNOWN)
		vrf = vrf_lookup_by_id(vrf_id);

	if (vrf == NULL) {
		vrf = XCALLOC(MTYPE_VRF, sizeof(struct vrf));
		vrf->vrf_id = VRF_UNKNOWN;
		QOBJ_REG(vrf, vrf);
		new = 1;

		if (debug_vrf)
			zlog_debug("VRF(%u) %s is created.", vrf_id,
				   (name) ? name : "(NULL)");
	}

	/* Set identifier */
	if (vrf_id != VRF_UNKNOWN && vrf->vrf_id == VRF_UNKNOWN) {
		vrf->vrf_id = vrf_id;
		RB_INSERT(vrf_id_head, &vrfs_by_id, vrf);
	}

	/* Set name */
	if (name && vrf->name[0] != '\0' && strcmp(name, vrf->name)) {
		/* vrf name has changed */
		if (vrf_notify_oper_changes)
			nb_notif_deletef("/frr-vrf:lib/vrf[name=\"%s\"]", vrf->name);
		RB_REMOVE(vrf_name_head, &vrfs_by_name, vrf);
		strlcpy(vrf->data.l.netns_name, name, NS_NAMSIZ);
		strlcpy(vrf->name, name, sizeof(vrf->name));
		RB_INSERT(vrf_name_head, &vrfs_by_name, vrf);
		/* New state with new name */
		if (vrf_notify_oper_changes)
			nb_notif_addf("/frr-vrf:lib/vrf[name=\"%s\"]", vrf->name);
	} else if (name && vrf->name[0] == '\0') {
		strlcpy(vrf->name, name, sizeof(vrf->name));
		RB_INSERT(vrf_name_head, &vrfs_by_name, vrf);

		/* We have a name now so we can have state */
		if (vrf_notify_oper_changes)
			nb_notif_addf("/frr-vrf:lib/vrf[name=\"%s\"]", vrf->name);
	} else if (vrf_notify_oper_changes)
		vrf_update_state(vrf);

	if (new &&vrf_master.vrf_new_hook)
		(*vrf_master.vrf_new_hook)(vrf);

	return vrf;
}

/* 【更新或创建】先按名字找到 VRF：若其现有 ID 与新 ID 不同，则先 disable 该 VRF
 * (模拟隐式删除以清掉陈旧路由)，再改 ID、重插 ID 树并发状态通知；否则退化为
 * vrf_get(new_vrf_id, name)。返回得到的 VRF。
 * 【易踩的坑】更新 ID 前不真正删除对象，只是禁用再换 ID，依赖后续 enable 恢复。 */
/* Update a VRF. If not found, create one.
 * Arg:
 *   name   - The name of the vrf.
 *   vrf_id - The vrf_id of the vrf.
 * Description: This function first finds the vrf using its name. If the vrf is
 * found and the vrf-id of the existing vrf does not match the new vrf id, it
 * will disable the existing vrf and update it with new vrf-id. If the vrf is
 * not found, it will create the vrf with given name and the new vrf id.
 */
struct vrf *vrf_update(vrf_id_t new_vrf_id, const char *name)
{
	struct vrf *vrf = NULL;

	/*Treat VRF add for existing vrf as update
	 * Update VRF ID and also update in VRF ID table
	 */
	if (name)
		vrf = vrf_lookup_by_name(name);
	if (vrf && new_vrf_id != VRF_UNKNOWN && vrf->vrf_id != VRF_UNKNOWN
	    && vrf->vrf_id != new_vrf_id) {
		if (debug_vrf) {
			zlog_debug(
				"Vrf Update event: %s old id: %u, new id: %u",
				name, vrf->vrf_id, new_vrf_id);
		}

		/*Disable the vrf to simulate implicit delete
		 * so that all stale routes are deleted
		 * This vrf will be enabled down the line
		 */
		vrf_disable(vrf);


		RB_REMOVE(vrf_id_head, &vrfs_by_id, vrf);
		vrf->vrf_id = new_vrf_id;
		RB_INSERT(vrf_id_head, &vrfs_by_id, vrf);

		vrf_update_state(vrf);
	} else {

		/*
		 * vrf_get is implied creation if it does not exist
		 */
		vrf = vrf_get(new_vrf_id, name);
	}
	return vrf;
}

/* 【删除】当底层 VRF 消失、预配置被删或进程关闭时调用。
 *   流程：若已启用则先 disable；从 ID 树移除并把 vrf_id 置为 VRF_UNKNOWN。
 * 【重要】以下情况只解除映射、不真正释放：该 VRF 有用户配置(VRF_CONFIGURED)，
 *   或仍有接口归属——二者都会提前 return，对象保留。
 *   真正释放时：调用 delete 钩子、注销 QOBJ、从名字树移除、发北向删除通知并释放内存。 */
/* Delete a VRF. This is called when the underlying VRF goes away, a
 * pre-configured VRF is deleted or when shutting down (vrf_terminate()).
 */
void vrf_delete(struct vrf *vrf)
{
	if (debug_vrf)
		zlog_debug("VRF %s(%u) is to be deleted.", vrf->name,
			   vrf->vrf_id);

	if (vrf_is_enabled(vrf))
		vrf_disable(vrf);

	if (vrf->vrf_id != VRF_UNKNOWN) {
		RB_REMOVE(vrf_id_head, &vrfs_by_id, vrf);
		vrf->vrf_id = VRF_UNKNOWN;
	}

	/* If the VRF is user configured, it'll stick around, just remove
	 * the ID mapping. Interfaces assigned to this VRF should've been
	 * removed already as part of the VRF going down.
	 */
	if (vrf_is_user_cfged(vrf))
		return;

	/* Do not delete the VRF if it has interfaces configured in it. */
	if (!RB_EMPTY(if_name_head, &vrf->ifaces_by_name))
		return;

	if (vrf_master.vrf_delete_hook)
		(*vrf_master.vrf_delete_hook)(vrf);

	QOBJ_UNREG(vrf);

	if (vrf->name[0] != '\0')
		RB_REMOVE(vrf_name_head, &vrfs_by_name, vrf);

	if (vrf_notify_oper_changes && vrf->name[0] != '\0')
		nb_notif_deletef("/frr-vrf:lib/vrf[name=\"%s\"]", vrf->name);

	XFREE(MTYPE_VRF, vrf);
}

/* 【查找】按 ID 在 ID 树中查找 VRF，返回指针或 NULL(用栈上临时键做 RB_FIND)。 */
/* Look up a VRF by identifier. */
struct vrf *vrf_lookup_by_id(vrf_id_t vrf_id)
{
	struct vrf vrf;
	vrf.vrf_id = vrf_id;
	return (RB_FIND(vrf_id_head, &vrfs_by_id, &vrf));
}

/* 【启用】把 VRF 标记为可用：置 VRF_ACTIVE 位、发状态通知、调用 enable 钩子，
 * 并唤醒此前因该 VRF 未就绪而挂起的 nexthop group。
 * 返回 1(成功)；已启用时同样返回 1。 */
/*
 * Enable a VRF - that is, let the VRF be ready to use.
 * The VRF_ENABLE_HOOK callback will be called to inform
 * that they can allocate resources in this VRF.
 *
 * RETURN: 1 - enabled successfully; otherwise, 0.
 */
int vrf_enable(struct vrf *vrf)
{
	if (vrf_is_enabled(vrf))
		return 1;

	if (debug_vrf)
		zlog_debug("VRF %s(%u) is enabled.", vrf->name, vrf->vrf_id);

	SET_FLAG(vrf->status, VRF_ACTIVE);

	vrf_update_state(vrf);

	if (vrf_master.vrf_enable_hook)
		(*vrf_master.vrf_enable_hook)(vrf);

	/*
	 * If we have any nexthop group entries that
	 * are awaiting vrf initialization then
	 * let's let people know about it
	 */
	nexthop_group_enable_vrf(vrf);

	return 1;
}

/* 【停用】把 VRF 标记为不可用：清 VRF_ACTIVE 位、发状态通知、处理挂起的
 * nexthop group，最后调用 disable 钩子。未启用时直接返回。 */
/*
 * Disable a VRF - that is, let the VRF be unusable.
 * The VRF_DELETE_HOOK callback will be called to inform
 * that they must release the resources in the VRF.
 */
void vrf_disable(struct vrf *vrf)
{
	if (!vrf_is_enabled(vrf))
		return;

	UNSET_FLAG(vrf->status, VRF_ACTIVE);

	vrf_update_state(vrf);

	if (debug_vrf)
		zlog_debug("VRF %s(%u) is to be disabled.", vrf->name,
			   vrf->vrf_id);

	/* Till now, nothing to be done for the default VRF. */
	// Pending: see why this statement.


	/*
	 * When the vrf is disabled let's
	 * handle all nexthop-groups associated
	 * with this vrf
	 */
	nexthop_group_disable_vrf(vrf);

	if (vrf_master.vrf_disable_hook)
		(*vrf_master.vrf_disable_hook)(vrf);
}

/* 【遍历】先遍历 ID 树、再遍历名字树中的所有非默认 VRF，最后单独处理默认 VRF，
 * 对每个调用 fnc。用 SAFE 版本以免遍历中删除节点出错。
 * 【易踩的坑】默认 VRF 会被两棵树的循环各自跳过，而在末尾单独补处理一次。 */
void vrf_iterate(vrf_iter_func fnc)
{
	struct vrf *vrf, *tmp;

	if (debug_vrf)
		zlog_debug("%s:  vrf subsystem iteration", __func__);

	RB_FOREACH_SAFE (vrf, vrf_id_head, &vrfs_by_id, tmp) {
		if (vrf->vrf_id == VRF_DEFAULT)
			continue;

		fnc(vrf);
	}

	RB_FOREACH_SAFE (vrf, vrf_name_head, &vrfs_by_name, tmp) {
		if (vrf->vrf_id == VRF_DEFAULT)
			continue;

		fnc(vrf);
	}

	/* Finally process default VRF */
	vrf = vrf_lookup_by_id(VRF_DEFAULT);
	if (vrf)
		fnc(vrf);
}

/* 【转换】把 VRF ID 转成可读名字：默认 VRF 返回默认名，否则查名字
 * (不存在时经 VRF_LOGNAME 回退为 "Unknown")。 */
const char *vrf_id_to_name(vrf_id_t vrf_id)
{
	struct vrf *vrf;

	if (vrf_id == VRF_DEFAULT)
		return VRF_DEFAULT_NAME;

	vrf = vrf_lookup_by_id(vrf_id);
	return VRF_LOGNAME(vrf);
}

/* 【查数据】返回指定 VRF 的 info 用户数据指针；VRF 不存在时返回 NULL。 */
/* Look up the data pointer of the specified VRF. */
void *vrf_info_lookup(vrf_id_t vrf_id)
{
	struct vrf *vrf = vrf_lookup_by_id(vrf_id);
	return vrf ? vrf->info : NULL;
}

/* 【结构】位图哈希表中的一个条目：VRF 的 id 与其“是否置位”的布尔值。
 * 之所以不用真正的 bitmap，是因为 VRF id 可能稀疏且取值很大。 */
/*
 * VRF hash for storing set or not.
 */
struct vrf_bit_set {
	vrf_id_t vrf_id;
	bool set;
};

/* 【哈希键】以 vrf_id 作为哈希键。 */
static unsigned int vrf_hash_bitmap_key(const void *data)
{
	const struct vrf_bit_set *bit = data;

	return bit->vrf_id;
}

/* 【哈希比较】两个条目相等当且仅当 vrf_id 相同。 */
static bool vrf_hash_bitmap_cmp(const void *a, const void *b)
{
	const struct vrf_bit_set *bit1 = a;
	const struct vrf_bit_set *bit2 = b;

	return bit1->vrf_id == bit2->vrf_id;
}

/* 【哈希分配回调】hash_get 需要新建条目时调用：复制 key(先只填 vrf_id)，
 * 返回新分配的 vrf_bit_set。 */
static void *vrf_hash_bitmap_alloc(void *data)
{
	struct vrf_bit_set *copy = data;
	struct vrf_bit_set *bit;

	bit = XMALLOC(MTYPE_VRF_BITMAP, sizeof(*bit));
	bit->vrf_id = copy->vrf_id;

	return bit;
}

/* 【哈希释放回调】释放一个 vrf_bit_set 条目。 */
static void vrf_hash_bitmap_free(void *data)
{
	struct vrf_bit_set *bit = data;

	XFREE(MTYPE_VRF_BITMAP, bit);
}

/* 【位图】把位图指针初始化为 NULL(空位图，哈希表按需惰性创建)。 */
void vrf_bitmap_init(vrf_bitmap_t *pbmap)
{
	*pbmap = NULL;
}

/* 【位图】释放整张位图(含所有条目)；空位图为 no-op。 */
void vrf_bitmap_free(vrf_bitmap_t *pbmap)
{
	struct hash *vrf_hash;

	if (!*pbmap)
		return;

	vrf_hash = *pbmap;

	hash_clean_and_free(&vrf_hash, vrf_hash_bitmap_free);
}

/* 【位图】把 vrf_id 对应的位置位(必要时先惰性创建哈希表)。
 * VRF_UNKNOWN 直接忽略；分配不到条目时静默返回。 */
void vrf_bitmap_set(vrf_bitmap_t *pbmap, vrf_id_t vrf_id)
{
	struct vrf_bit_set lookup = { .vrf_id = vrf_id };
	struct hash *vrf_hash;
	struct vrf_bit_set *bit;

	if (vrf_id == VRF_UNKNOWN)
		return;

	if (!*pbmap)
		*pbmap = vrf_hash =
			hash_create_size(2, vrf_hash_bitmap_key,
					 vrf_hash_bitmap_cmp, "VRF BIT HASH");
	else
		vrf_hash = *pbmap;

	bit = hash_get(vrf_hash, &lookup, vrf_hash_bitmap_alloc);
	if (!bit)
		return;
	bit->set = true;
}

/* 【位图】清除 vrf_id 对应的位。
 * 【易踩的坑】位图不存在、或该 id 不在表中时都无需处理，直接返回。 */
void vrf_bitmap_unset(vrf_bitmap_t *pbmap, vrf_id_t vrf_id)
{
	struct vrf_bit_set lookup = { .vrf_id = vrf_id };
	struct hash *vrf_hash;
	struct vrf_bit_set *bit;

	if (vrf_id == VRF_UNKNOWN)
		return;

	/*
	 * If the hash is not created then unsetting is unnecessary
	 */
	if (!*pbmap)
		return;

	vrf_hash = *pbmap;

	/*
	 * If we can't look it up, no need to unset it!
	 */
	bit = hash_lookup(vrf_hash, &lookup);
	if (!bit)
		return;

	bit->set = false;
}

/* 【位图】查询 vrf_id 是否置位，返回 1 或 0。
 * 空位图或 VRF_UNKNOWN 返回 0；条目不存在也返回 0。 */
int vrf_bitmap_check(vrf_bitmap_t *pbmap, vrf_id_t vrf_id)
{
	struct vrf_bit_set lookup = { .vrf_id = vrf_id };
	struct hash *vrf_hash;
	struct vrf_bit_set *bit;

	if (!*pbmap || vrf_id == VRF_UNKNOWN)
		return 0;

	vrf_hash = *pbmap;
	bit = hash_lookup(vrf_hash, &lookup);
	if (bit)
		return bit->set;

	return 0;
}

/* 【补全】CLI 变量补全回调：把每个 VRF 的名字加入候选 comps(名字需 XSTRDUP
 * 复制，供补全框架持有)。 */
static void vrf_autocomplete(vector comps, struct cmd_token *token)
{
	struct vrf *vrf = NULL;

	RB_FOREACH (vrf, vrf_name_head, &vrfs_by_name)
		vector_set(comps, XSTRDUP(MTYPE_COMPLETION, vrf->name));
}

/* 【补全注册】声明 vrf / vrf_name / nexthop_vrf 三个 CLI 变量都使用上面的补全
 * 函数；末尾以 {NULL} 哨兵结束。 */
static const struct cmd_variable_handler vrf_var_handlers[] = {
	{
		.varname = "vrf",
		.completions = vrf_autocomplete,
	},
	{
		.varname = "vrf_name",
		.completions = vrf_autocomplete,
	},
	{
		.varname = "nexthop_vrf",
		.completions = vrf_autocomplete,
	},
	{.completions = NULL},
};

/* 【初始化】初始化 VRF 子系统。
 *   步骤：ns_init() 初始化命名空间；登记四个生命周期钩子；创建并启用默认 VRF
 *   (任意一步失败即致命退出 exit(1))；最后注册 CLI 变量补全。
 *   参数 create/enable/disable/destroy 是四个回调，由调用者(如 zebra)提供。 */
/* Initialize VRF module. */
void vrf_init(int (*create)(struct vrf *), int (*enable)(struct vrf *),
	      int (*disable)(struct vrf *), int (*destroy)(struct vrf *))
{
	struct vrf *default_vrf;

	/* initialise NS, in case VRF backend if NETNS */
	ns_init();
	if (debug_vrf)
		zlog_debug("%s: Initializing VRF subsystem", __func__);

	vrf_master.vrf_new_hook = create;
	vrf_master.vrf_enable_hook = enable;
	vrf_master.vrf_disable_hook = disable;
	vrf_master.vrf_delete_hook = destroy;

	/* The default VRF always exists. */
	default_vrf = vrf_get(VRF_DEFAULT, VRF_DEFAULT_NAME);
	if (!default_vrf) {
		flog_err(EC_LIB_VRF_START,
			 "vrf_init: failed to create the default VRF!");
		exit(1);
	}

	/* Enable the default VRF. */
	if (!vrf_enable(default_vrf)) {
		flog_err(EC_LIB_VRF_START,
			 "vrf_init: failed to enable the default VRF!");
		exit(1);
	}

	cmd_variable_handler_register(vrf_var_handlers);
}

/* 【终止单个】清掉配置标志、停用、终止其接口并删除该 VRF。由 vrf_iterate 逐个调用。 */
static void vrf_terminate_single(struct vrf *vrf)
{
	/* Clear configured flag and invoke delete. */
	vrf_disable(vrf);
	UNSET_FLAG(vrf->status, VRF_CONFIGURED);
	if_terminate(vrf);
	vrf_delete(vrf);
}

/* 【终止】遍历并清理所有 VRF(对每个调用 vrf_terminate_single)。协议关闭时调用。 */
void vrf_terminate(void)
{
	if (debug_vrf)
		zlog_debug("%s: Shutting down vrf subsystem", __func__);

	vrf_iterate(vrf_terminate_single);
}

/* 【建 socket】在指定 VRF 内创建 socket。
 *   若为 netns 后端，先切入该 netns 建 socket 再切回；随后按 interfacename
 *   经 vrf_bind 绑定到 VRF 设备或接口。
 *   返回：新建的 fd；若在 bind 阶段失败会 close 并返回错误码，底层 socket 失败
 *   则返回其结果(负值)。
 * 【易踩的坑】切回后会恢复保存的 errno，以保留 socket 阶段的真实错误。 */
int vrf_socket(int domain, int type, int protocol, vrf_id_t vrf_id,
	       const char *interfacename)
{
	int ret, save_errno, ret2;

	ret = vrf_switch_to_netns(vrf_id);
	if (ret < 0)
		flog_err_sys(EC_LIB_SOCKET, "%s: Can't switch to VRF %u (%s)",
			     __func__, vrf_id, safe_strerror(errno));

	ret = socket(domain, type, protocol);
	save_errno = errno;
	ret2 = vrf_switchback_to_initial();
	if (ret2 < 0)
		flog_err_sys(EC_LIB_SOCKET,
			     "%s: Can't switchback from VRF %u (%s)", __func__,
			     vrf_id, safe_strerror(errno));
	errno = save_errno;
	if (ret <= 0)
		return ret;
	ret2 = vrf_bind(vrf_id, ret, interfacename);
	if (ret2 < 0) {
		close(ret);
		ret = ret2;
	}
	return ret;
}

/* 【后端】当前 VRF 后端是否为 netns。 */
bool vrf_is_backend_netns(void)
{
	return (vrf_backend == VRF_BACKEND_NETNS);
}

/* 【后端】返回当前 VRF 后端类型。 */
enum vrf_backend_type vrf_get_backend(void)
{
	return vrf_backend;
}

/* 【后端】设置 VRF 后端类型(VRF-lite 或 netns)。 */
void vrf_configure_backend(enum vrf_backend_type backend)
{
	vrf_backend = backend;
}

/* 【CLI】VRF 相关命令定义。 */
/* vrf CLI commands */
DEFUN_YANG_NOSH (vrf_exit,
           vrf_exit_cmd,
	   "exit-vrf",
	   "Exit current mode and down to previous mode\n")
{
	cmd_exit(vty);
	return CMD_SUCCESS;
}

/* 【命令】vrf NAME：进入指定 VRF 的配置视图。
 *   先校验名字长度，再通过北向把该 VRF 创建进运行配置，成功后压入 VRF 节点的
 *   XPath 与上下文。返回命令执行结果。
 * 【易踩的坑】创建下达成功但查不到 vrf 时仍返回 SUCCESS，只是不压上下文。 */
DEFUN_YANG_NOSH (vrf,
       vrf_cmd,
       "vrf NAME",
       "Select a VRF to configure\n"
       "VRF's name\n")
{
	int idx_name = 1;
	const char *vrfname = argv[idx_name]->arg;
	char xpath_list[XPATH_MAXLEN];
	struct vrf *vrf;
	int ret;

	if (strlen(vrfname) > VRF_NAMSIZ) {
		vty_out(vty,
			"%% VRF name %s invalid: length exceeds %d bytes\n",
			vrfname, VRF_NAMSIZ);
		return CMD_WARNING_CONFIG_FAILED;
	}

	snprintf(xpath_list, sizeof(xpath_list), FRR_VRF_KEY_XPATH, vrfname);

	nb_cli_enqueue_change(vty, xpath_list, NB_OP_CREATE, NULL);
	ret = nb_cli_apply_changes_clear_pending(vty, "%s", xpath_list);
	if (ret == CMD_SUCCESS) {
		VTY_PUSH_XPATH(VRF_NODE, xpath_list);
		vrf = vrf_lookup_by_name(vrfname);
		if (vrf)
			VTY_PUSH_CONTEXT(VRF_NODE, vrf);
	}

	return ret;
}

/* 【命令】no vrf NAME：删除一个 VRF 的配置。
 *   在 VRF-lite 后端下还会一并删除对应的接口配置(/frr-interface)，再删除 VRF
 *   本身，最后统一提交北向变更。 */
DEFUN_YANG (no_vrf,
       no_vrf_cmd,
       "no vrf NAME",
       NO_STR
       "Delete a pseudo VRF's configuration\n"
       "VRF's name\n")
{
	const char *vrfname = argv[2]->arg;
	char xpath_list[XPATH_MAXLEN];

	if (vrf_get_backend() == VRF_BACKEND_VRF_LITE) {
		/*
		 * Remove the VRF interface config when removing the VRF.
		 */
		snprintf(xpath_list, sizeof(xpath_list),
			 "/frr-interface:lib/interface[name='%s']", vrfname);
		nb_cli_enqueue_change(vty, xpath_list, NB_OP_DESTROY, NULL);
	}

	snprintf(xpath_list, sizeof(xpath_list), FRR_VRF_KEY_XPATH, vrfname);

	nb_cli_enqueue_change(vty, xpath_list, NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}


/* 【节点】VRF 配置视图节点定义(提示符 config-vrf 井号)。 */
static struct cmd_node vrf_node = {
	.name = "vrf",
	.node = VRF_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-vrf)# ",
};

/*
 * Debug CLI for vrf's
 */
/* 【命令】debug vrf：打开 VRF 调试日志。 */
DEFUN (vrf_debug,
      vrf_debug_cmd,
      "debug vrf",
      DEBUG_STR
      "VRF Debugging\n")
{
	debug_vrf = 1;

	return CMD_SUCCESS;
}

/* 【命令】no debug vrf：关闭 VRF 调试日志。 */
DEFUN (no_vrf_debug,
      no_vrf_debug_cmd,
      "no debug vrf",
      NO_STR
      DEBUG_STR
      "VRF Debugging\n")
{
	debug_vrf = 0;

	return CMD_SUCCESS;
}

/* 【配置回写】把当前 VRF 调试状态写入运行配置(调试开启时输出 debug vrf)。返回 1。 */
static int vrf_write_host(struct vty *vty)
{
	if (debug_vrf)
		vty_out(vty, "debug vrf\n");

	return 1;
}

static int vrf_write_host(struct vty *vty);
/* 【节点】VRF 调试节点(无提示符，仅用于把 debug_vrf 状态写进运行配置)。 */
static struct cmd_node vrf_debug_node = {
	.name = "vrf debug",
	.node = VRF_DEBUG_NODE,
	.prompt = "",
	.config_write = vrf_write_host,
};

/* 【CLI 初始化】安装 VRF 调试节点与 debug / no debug vrf 命令。 */
void vrf_install_commands(void)
{
	install_node(&vrf_debug_node);

	install_element(CONFIG_NODE, &vrf_debug_cmd);
	install_element(ENABLE_NODE, &vrf_debug_cmd);
	install_element(CONFIG_NODE, &no_vrf_debug_cmd);
	install_element(ENABLE_NODE, &no_vrf_debug_cmd);
}

/* 【CLI 初始化】安装 vrf / no vrf 命令与 VRF 节点，设置其配置回写函数为
 * writefunc，并安装 exit-vrf 命令。 */
void vrf_cmd_init(int (*writefunc)(struct vty *vty))
{
	install_element(CONFIG_NODE, &vrf_cmd);
	install_element(CONFIG_NODE, &no_vrf_cmd);
	vrf_node.config_write = writefunc;
	install_node(&vrf_node);
	install_default(VRF_NODE);
	install_element(VRF_NODE, &vrf_exit_cmd);
}

/* 【默认名】设置默认 VRF 的名字(截断到 VRF_NAMSIZ)。应仅在启动期调用。 */
void vrf_set_default_name(const char *default_name)
{
	snprintf(vrf_default_name, VRF_NAMSIZ, "%s", default_name);
}

/* 【默认名】返回当前默认 VRF 名字。 */
const char *vrf_get_default_name(void)
{
	return vrf_default_name;
}

/* 【绑定】把 fd 绑定到 VRF 内的接口或 VRF 设备。
 *   ifname 非空且不等于 VRF 名时绑到该接口；否则绑到 VRF 设备。
 *   返回 0 成功、-1 失败。
 * 【易踩的坑】fd 无效、VRF_UNKNOWN、VRF 未启用、目标接口不存在，都会返回 -1；
 *   netns 后端与默认 VRF 无需绑定，直接返回 0。 */
int vrf_bind(vrf_id_t vrf_id, int fd, const char *ifname)
{
	int ret = 0;
	struct interface *ifp;
	struct vrf *vrf;

	if (fd < 0)
		return -1;

	if (vrf_id == VRF_UNKNOWN)
		return -1;

	/* can't bind to a VRF that doesn't exist */
	vrf = vrf_lookup_by_id(vrf_id);
	if (!vrf_is_enabled(vrf))
		return -1;

	if (ifname && strcmp(ifname, vrf->name)) {
		/* binding to a regular interface */

		/* can't bind to an interface that doesn't exist */
		ifp = if_lookup_by_name(ifname, vrf_id);
		if (!ifp)
			return -1;
	} else {
		/* binding to a VRF device */

		/* nothing to do for netns */
		if (vrf_is_backend_netns())
			return 0;

		/* nothing to do for default vrf */
		if (vrf_id == VRF_DEFAULT)
			return 0;
#ifdef SO_BINDTODEVICE
		ifname = vrf->name;
#endif
	}

#ifdef SO_BINDTODEVICE
	ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, ifname,
			 strlen(ifname) + 1);
	if (ret < 0)
		zlog_err("bind to interface %s failed, errno=%d", ifname,
			 errno);
#endif /* SO_BINDTODEVICE */
	return ret;
}
/* 【解析】在指定 VRF 的命名空间内执行 getaddrinfo(必要时先切入再切回)。
 * 返回 getaddrinfo 的结果；切回后恢复保存的 errno。 */
int vrf_getaddrinfo(const char *node, const char *service,
		    const struct addrinfo *hints, struct addrinfo **res,
		    vrf_id_t vrf_id)
{
	int ret, ret2, save_errno;

	ret = vrf_switch_to_netns(vrf_id);
	if (ret < 0)
		flog_err_sys(EC_LIB_SOCKET, "%s: Can't switch to VRF %u (%s)",
			     __func__, vrf_id, safe_strerror(errno));
	ret = getaddrinfo(node, service, hints, res);
	save_errno = errno;
	ret2 = vrf_switchback_to_initial();
	if (ret2 < 0)
		flog_err_sys(EC_LIB_SOCKET,
			     "%s: Can't switchback from VRF %u (%s)", __func__,
			     vrf_id, safe_strerror(errno));
	errno = save_errno;
	return ret;
}

/* 【ioctl】在指定 VRF 的命名空间内执行 ioctl(必要时先切入再切回)。
 * 返回 ioctl 的结果；VFR 切换失败时返回 0。 */
int vrf_ioctl(vrf_id_t vrf_id, int d, unsigned long request, char *params)
{
	int ret, saved_errno, rc;

	ret = vrf_switch_to_netns(vrf_id);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "%s: Can't switch to VRF %u (%s)",
			     __func__, vrf_id, safe_strerror(errno));
		return 0;
	}
	rc = ioctl(d, request, params);
	saved_errno = errno;
	ret = vrf_switchback_to_initial();
	if (ret < 0)
		flog_err_sys(EC_LIB_SOCKET,
			     "%s: Can't switchback from VRF %u (%s)", __func__,
			     vrf_id, safe_strerror(errno));
	errno = saved_errno;
	return rc;
}

/* 【建 socket】按 sockunion 的地址族在指定 VRF 内创建 socket，流程同 vrf_socket
 * (切入 netns 建 socket、切回，再按 interfacename 绑定)。 */
int vrf_sockunion_socket(const union sockunion *su, vrf_id_t vrf_id,
			 const char *interfacename)
{
	int ret, save_errno, ret2;

	ret = vrf_switch_to_netns(vrf_id);
	if (ret < 0)
		flog_err_sys(EC_LIB_SOCKET, "%s: Can't switch to VRF %u (%s)",
			     __func__, vrf_id, safe_strerror(errno));
	ret = sockunion_socket(su);
	save_errno = errno;
	ret2 = vrf_switchback_to_initial();
	if (ret2 < 0)
		flog_err_sys(EC_LIB_SOCKET,
			     "%s: Can't switchback from VRF %u (%s)", __func__,
			     vrf_id, safe_strerror(errno));
	errno = save_errno;

	if (ret <= 0)
		return ret;
	ret2 = vrf_bind(vrf_id, ret, interfacename);
	if (ret2 < 0) {
		close(ret);
		ret = ret2;
	}
	return ret;
}

/* ------- 北向(Northbound)回调实现: 把 YANG 配置映射到 struct vrf ------- */
/* ------- Northbound callbacks ------- */

/* 【北向 create】/frr-vrf:lib/vrf 的创建回调：取名字，创建(或取得)VRF 并置
 * VRF_CONFIGURED 位，再把 vrfp 登记为运行态条目。仅 APPLY 事件真正动作。
 *   args->dnode 为 YANG 数据节点，其余事件直接返回 NB_OK。 */
/*
 * XPath: /frr-vrf:lib/vrf
 */
static int lib_vrf_create(struct nb_cb_create_args *args)
{
	const char *vrfname;
	struct vrf *vrfp;

	vrfname = yang_dnode_get_string(args->dnode, "name");

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	vrfp = vrf_get(VRF_UNKNOWN, vrfname);

	SET_FLAG(vrfp->status, VRF_CONFIGURED);
	nb_running_set_entry(args->dnode, vrfp);

	return NB_OK;
}

/* 【北向 destroy】VRF 的销毁回调。
 *   VALIDATE 阶段：仅当 VRF 处于非激活状态才允许删除，否则写错误信息并返回
 *   校验失败(NB_ERR_VALIDATION)。
 *   APPLY 阶段：取回条目、清 VRF_CONFIGURED 位并调用 vrf_delete。 */
static int lib_vrf_destroy(struct nb_cb_destroy_args *args)
{
	struct vrf *vrfp;

	switch (args->event) {
	case NB_EV_VALIDATE:
		vrfp = nb_running_get_entry(args->dnode, NULL, true);
		if (CHECK_FLAG(vrfp->status, VRF_ACTIVE)) {
			snprintf(args->errmsg, args->errmsg_len,
				 "Only inactive VRFs can be deleted");
			return NB_ERR_VALIDATION;
		}
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		vrfp = nb_running_unset_entry(args->dnode);

		/* Clear configured flag and invoke delete. */
		UNSET_FLAG(vrfp->status, VRF_CONFIGURED);
		vrf_delete(vrfp);
		break;
	}

	return NB_OK;
}

/* 【CLI 输出】把 VRF 写进运行配置：非默认 VRF 时输出 "!" 与 "vrf NAME" 头。 */
static void lib_vrf_cli_write(struct vty *vty, const struct lyd_node *dnode,
			      bool show_defaults)
{
	const char *name = yang_dnode_get_string(dnode, "name");

	if (strcmp(name, VRF_DEFAULT_NAME)) {
		vty_out(vty, "!\n");
		vty_out(vty, "vrf %s\n", name);
	}
}

/* 【CLI 输出】VRF 配置块结尾：非默认 VRF 时输出 "exit-vrf"。 */
static void lib_vrf_cli_write_end(struct vty *vty, const struct lyd_node *dnode)
{
	const char *name = yang_dnode_get_string(dnode, "name");

	if (strcmp(name, VRF_DEFAULT_NAME))
		vty_out(vty, "exit-vrf\n");
}

/* 【北向 get_next】按名字树顺序返回下一个 VRF 条目(list_entry 为 NULL 时取最小的)。 */
static const void *lib_vrf_get_next(struct nb_cb_get_next_args *args)
{
	struct vrf *vrfp = (struct vrf *)args->list_entry;

	if (args->list_entry == NULL) {
		vrfp = RB_MIN(vrf_name_head, &vrfs_by_name);
	} else {
		vrfp = RB_NEXT(vrf_name_head, vrfp);
	}

	return vrfp;
}

/* 【北向 get_keys】返回该 VRF 条目的键(name)。 */
static int lib_vrf_get_keys(struct nb_cb_get_keys_args *args)
{
	struct vrf *vrfp = (struct vrf *)args->list_entry;

	args->keys->num = 1;
	strlcpy(args->keys->key[0], vrfp->name, sizeof(args->keys->key[0]));

	return NB_OK;
}

/* 【北向 lookup_entry】按键(name)精确查找 VRF 条目。 */
static const void *lib_vrf_lookup_entry(struct nb_cb_lookup_entry_args *args)
{
	const char *vrfname = args->keys->key[0];

	struct vrf *vrf = vrf_lookup_by_name(vrfname);

	return vrf;
}

/* 【北向 lookup_next】返回名字严格大于给定键的下一个 VRF 条目。 */
static const void *lib_vrf_lookup_next(struct nb_cb_lookup_entry_args *args)
{
	const char *vrfname = args->keys->key[0];
	struct vrf vrfkey, *vrf;

	strlcpy(vrfkey.name, vrfname, sizeof(vrfkey.name));
	vrf = RB_FIND(vrf_name_head, &vrfs_by_name, &vrfkey);
	if (!vrf)
		return NULL;
	if (!strcmp(vrf->name, vrfname))
		vrf = RB_NEXT(vrf_name_head, vrf);

	return vrf;
}

/* 【北向 state】返回该 VRF 的 ID，输出到 /frr-vrf:lib/vrf/state/id。 */
/*
 * XPath: /frr-vrf:lib/vrf/state/id
 */
static struct yang_data *
lib_vrf_state_id_get_elem(struct nb_cb_get_elem_args *args)
{
	struct vrf *vrfp = (struct vrf *)args->list_entry;

	return yang_data_new_uint32(args->xpath, vrfp->vrf_id);
}

/* 【北向 state】返回该 VRF 是否激活，输出到 /frr-vrf:lib/vrf/state/active。 */
/*
 * XPath: /frr-vrf:lib/vrf/state/active
 */
static struct yang_data *
lib_vrf_state_active_get_elem(struct nb_cb_get_elem_args *args)
{
	struct vrf *vrfp = (struct vrf *)args->list_entry;

	return yang_data_new_bool(args->xpath, CHECK_FLAG(vrfp->status, VRF_ACTIVE) ? true : false);
}

/* clang-format off */

/* cli_show callbacks are kept here for daemons not yet converted to mgmtd */
/* 【YANG 模块表】frr-vrf 模块的回调表：把 /frr-vrf:lib/vrf 及其 state 节点绑定
 * 到上面的北向回调。priority 略低于默认值，使其它模块可先于 vrf 处理。 */
const struct frr_yang_module_info frr_vrf_info = {
	.name = "frr-vrf",
	.nodes = {
		{
			.xpath = "/frr-vrf:lib/vrf",
			.cbs = {
				.create = lib_vrf_create,
				.destroy = lib_vrf_destroy,
				.cli_show = lib_vrf_cli_write,
				.cli_show_end = lib_vrf_cli_write_end,
				.get_next = lib_vrf_get_next,
				.get_keys = lib_vrf_get_keys,
				.lookup_entry = lib_vrf_lookup_entry,
				.lookup_next = lib_vrf_lookup_next,
			},
			.priority = NB_DFLT_PRIORITY - 2,
		},
		{
			.xpath = "/frr-vrf:lib/vrf/state/id",
			.cbs = {
				.get_elem = lib_vrf_state_id_get_elem,
			}
		},
		{
			.xpath = "/frr-vrf:lib/vrf/state/active",
			.cbs = {
				.get_elem = lib_vrf_state_active_get_elem,
			}
		},
		{
			.xpath = NULL,
		},
	}
};

/* 【YANG 模块表】frr-vrf 的 CLI 展示专用表：ignore_cfg_cbs 为真，只提供
 * cli_show / cli_show_end，供尚未迁移到 mgmtd 的守护进程写运行配置用。 */
const struct frr_yang_module_info frr_vrf_cli_info = {
	.name = "frr-vrf",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/frr-vrf:lib/vrf",
			.cbs = {
				.cli_show = lib_vrf_cli_write,
				.cli_show_end = lib_vrf_cli_write_end,
			},
		},
		{
			.xpath = NULL,
		},
	}
};

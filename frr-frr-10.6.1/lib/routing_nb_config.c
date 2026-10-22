// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018        Vmware
 *                           Vishal Dhingra
 */

#include <zebra.h>

#include "northbound.h"
#include "libfrr.h"
#include "vrf.h"
#include "lib_errors.h"
#include "routing_nb.h"


/* ============================================================================
 * 【文件导读】routing_nb_config.c —— control-plane-protocol 节点的回调实现
 * ============================================================================
 * 【本文件解决什么问题】在 FRR 里"配置某协议的某 VRF 实例"被统一表示成 YANG
 * 路径 `/frr-routing:routing/control-plane-protocols/control-plane-protocol`，
 * 实例由 (type, name, vrf) 三元组唯一确定。本文件提供该节点的 create / destroy
 * 回调，并在其中调用三个 hook，让具体协议守护进程能插进自己的初始化逻辑。
 *
 * 【核心机制：为什么要有 hook】框架(本文件)只懂 YANG 事务与 VRF，不懂 ISIS、
 * OSPF、BGP。于是它把"协议相关的那一段"留成 hook：daemon 启动时用
 * hook_register 注册自己的处理函数，配置事务走到这里时统一 hook_call 触发。
 * 这就是"通用框架 + 协议插件"的解耦点。
 *
 * 【核心机制：dnode 里藏 VRF 指针】YANG 数据节点(dnode)本来只存文本。若某协议
 * 需要频繁拿到对应的 `struct vrf *`，每次按名字查表太慢；于是 northbound 提供
 * "依赖(dependency)"机制：注册依赖后，create 时把 VRF 指针缓存进 dnode，
 * destroy 时清掉。本文件用 nb_node_has_dependency() 判断本节点是否登记过这种
 * 依赖，**只有登记过才走缓存路径**(见下方注释块与 routing_control_plane_
 * protocols_register_vrf_dependency 的实现)。
 *
 * 【阅读顺序】DEFINE_HOOK 三连(先知道有哪三个扩展点) → create 回调(按事务
 * 阶段分派) → destroy 回调 → 两个 xpath 转换器 → 依赖注册函数(把转换器装上)。
 * ============================================================================
 */
/* 【Hook 定义】三个钩子的实体(声明在 routing_nb.h)。
 *   routing_conf_event : 校验阶段钩子，由 create 回调在 NB_EV_VALIDATE 时调用；
 *                        任一注册者返回非 0 即视为校验失败。
 *   routing_create     : 实例创建钩子。
 *   routing_destroy    : 实例销毁钩子(DECLARE_KOOH 声明，提示调用方逆序遍历)。
 * 注意 DEFINE_HOOK / DEFINE_KOOH 也会在文件里生成全局变量，所以本文件必须
 * 恰好出现一次这三个 DEFINE。 */
DEFINE_HOOK(routing_conf_event, (struct nb_cb_create_args *args), (args));
DEFINE_HOOK(routing_create, (struct nb_cb_create_args *args), (args));
DEFINE_KOOH(routing_destroy, (struct nb_cb_destroy_args *args), (args));

/*
 * XPath: /frr-routing:routing/control-plane-protocols/control-plane-protocol
 */

/* 【回调】创建一条 control-plane-protocol(即某协议在某 VRF 下的实例)。
 *   args: args->event 是当前事务阶段，args->dnode 是被创建的 YANG 节点。
 *   返回: NB_OK，或 NB_ERR_VALIDATION(校验钩子拒绝时)。
 * 【事务阶段怎么走，看这个 switch 最清楚】
 *   NB_EV_VALIDATE: 只调用校验钩子；钩子返回非 0 → 立刻返回 NB_ERR_VALIDATION，
 *                   整个事务回滚。这是"在改内存之前先说不行"的时机。
 *   NB_EV_PREPARE / NB_EV_ABORT: 本节点不需要做什么，空分支(注意**没有
 *                   break 之外的动作**)。ABORT 表示事务被中止，要回收 PREPARE
 *                   时申请的资源——本节点没申请，所以空着。
 *   NB_EV_APPLY    : 真正的落地阶段，才允许副作用。先按需缓存 VRF 指针，
 *                   再调用 routing_create 钩子让协议去建实例。
 * 【易踩的坑】① 只有 APPLY 里才有真正的"做事"，其余阶段提前 return 是正常的；
 *   ② VRF 指针的缓存是**条件性**的，靠 nb_node_has_dependency() 判断——
 *   若某 daemon 忘了调用 routing_control_plane_protocols_register_vrf_dependency()，
 *   这里会跳过缓存，后续代码若假定 dnode 里一定有 VRF 指针就会取到 NULL；
 *   ③ assert(vrf) 表明 vrf_get() 在依赖已注册的前提下被认为必然能拿到 VRF，
 *   名字对不上时这里是**断言崩溃**而不是返回错误码。 */
int routing_control_plane_protocols_control_plane_protocol_create(
	struct nb_cb_create_args *args)
{
	struct vrf *vrf;
	const char *vrfname;

	switch (args->event) {
	case NB_EV_VALIDATE:
		if (hook_call(routing_conf_event, args))
			return NB_ERR_VALIDATION;
		break;
	case NB_EV_PREPARE:
	case NB_EV_ABORT:
		break;
	case NB_EV_APPLY:
		/*
		 * If the daemon relies on the VRF pointer stored in this
		 * dnode, then it should register the dependency between this
		 * module and the VRF module using
		 * routing_control_plane_protocols_register_vrf_dependency.
		 * If such dependency is not registered, then nothing is
		 * stored in the dnode. If the dependency is registered,
		 * find the vrf and store the pointer.
		 */
		if (nb_node_has_dependency(args->dnode->schema->priv)) {
			vrfname = yang_dnode_get_string(args->dnode, "vrf");
			vrf = vrf_get(VRF_UNKNOWN, vrfname);
			assert(vrf);
			nb_running_set_entry(args->dnode, vrf);
		}
		hook_call(routing_create, args);
		break;
	};

	return NB_OK;
}

/* 【回调】销毁一条 control-plane-protocol。
 *   返回: 恒为 NB_OK(本回调不会失败)。
 * 【要点】与 create 不同，这里**只关心 APPLY 阶段**：事务的校验/准备阶段对
 * "删除"没有额外工作要做，所以非 APPLY 直接返回成功。真正的清理有两步——
 *   ① hook_call(routing_destroy) 让协议本体释放自己的实例；
 *   ② 若登记过 VRF 依赖，则把 dnode 里缓存的 VRF 指针清掉
 *      (nb_running_unset_entry)。这一步必须做，否则 dnode 会留下**悬垂指针**
 *      (VRF 可能随后被删)，是典型的一类内存安全问题。
 * 【易踩的坑】nb_running_unset_entry 的调用条件与 create 里 nb_running_set_entry
 * 的条件必须**成对**：都用 nb_node_has_dependency() 判断，否则会出现"设了没清"
 * 或"清了没设(清空指针导致 assert)"。 */
int routing_control_plane_protocols_control_plane_protocol_destroy(
	struct nb_cb_destroy_args *args)
{
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	hook_call(routing_destroy, args);

	/*
	 * If dependency on VRF module is registered, then VRF
	 * pointer was stored and must be cleared.
	 */
	if (nb_node_has_dependency(args->dnode->schema->priv))
		nb_running_unset_entry(args->dnode);

	return NB_OK;
}

/* 【xpath 转换器 A】给定一个 **VRF 节点**，算出与之关联的 control-plane-protocol
 * 节点的 xpath。
 *   dnode: frr-vrf 模型里的 VRF 节点(注意不是 routing 节点!)。
 *   xpath: 输出缓冲，由调用方提供，长度须为 XPATH_MAXLEN。
 * 【用法】northbound 的依赖机制需要"双向映射"：改了这个对象，要能反查出依赖它的
 * 对象并重算。本函数给出的是"VRF → protocol"方向，取 VRF 的 name 键拼成
 * FRR_ROUTING_KEY_XPATH_VRF(只带 vrf 键的那条路径)。
 * 【易踩的坑】这里读的是子节点 "name"，而另一个方向读的是 "vrf"——同一个 VRF
 * 在两个模型里字段名不同，写反了会取到 NULL 或触发 yang 库报错。 */
static void vrf_to_control_plane_protocol(const struct lyd_node *dnode,
					  char *xpath)
{
	const char *vrf;

	vrf = yang_dnode_get_string(dnode, "name");

	snprintf(xpath, XPATH_MAXLEN, FRR_ROUTING_KEY_XPATH_VRF, vrf);
}

/* 【xpath 转换器 B】给定一个 **control-plane-protocol 节点**，算出它所属 VRF
 * 节点的 xpath。与上面那个函数方向相反，两者配对构成双向映射。
 *   dnode: frr-routing 里的 protocol 节点，取其 "vrf" 叶子。
 *   xpath: 输出缓冲，长度须为 XPATH_MAXLEN。
 * 拼出的是 FRR_VRF_KEY_XPATH(定义在 vrf.h / frr-vrf 侧)。 */
static void control_plane_protocol_to_vrf(const struct lyd_node *dnode,
					  char *xpath)
{
	const char *vrf;

	vrf = yang_dnode_get_string(dnode, "vrf");

	snprintf(xpath, XPATH_MAXLEN, FRR_VRF_KEY_XPATH, vrf);
}

/* 【函数】把"control-plane-protocol ↔ VRF"的双向依赖注册进 northbound。
 * 【为什么需要它】注册之后框架才知道这两个模型有关联，于是：
 *   ① create 回调里才会执行 nb_running_set_entry() 缓存 VRF 指针(见本文件开头);
 *   ② 当某个 VRF 被改动/删除时，框架能顺藤摸瓜找到依赖它的 protocol 节点；
 *   ③ 事务中两者的顺序/一致性由框架保证。
 * 【实现细节】struct nb_dependency_callbacks 里两个成员就是上面那对互逆的 xpath
 * 转换器；nb_node_set_dependency_cbs() 的前两个参数是 (依赖方, 被依赖方)——
 * 这里传的是 (FRR_VRF_XPATH, FRR_ROUTING_XPATH)，即 protocol 依赖 VRF。
 * 【调用时机】由各协议 daemon 在启动/注册模块时**主动调用一次**；不调用则整个
 * VRF 依赖机制对本协议不生效(create 里会跳过缓存，功能上少一层便利但不崩)。 */
void routing_control_plane_protocols_register_vrf_dependency(void)
{
	struct nb_dependency_callbacks cbs;

	cbs.get_dependant_xpath = vrf_to_control_plane_protocol;
	cbs.get_dependency_xpath = control_plane_protocol_to_vrf;

	nb_node_set_dependency_cbs(FRR_VRF_XPATH, FRR_ROUTING_XPATH, &cbs);
}

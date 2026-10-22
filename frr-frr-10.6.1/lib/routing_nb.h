// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef _FRR_ROUTING_NB_H_
#define _FRR_ROUTING_NB_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 【文件导读】routing_nb.h —— frr-routing 模块的对外接口
 * ============================================================================
 * 【本文件提供三样东西】
 *   ① 两份模块注册表的 extern 声明(定义在 routing_nb.c)；
 *   ② control-plane-protocol 节点的 create / destroy 回调原型
 *      (实现位于 routing_nb_config.c，被注册表引用)；
 *   ③ 一组 XPath 宏 + 三个 hook 声明 + 一个依赖注册函数。
 *
 * 【背景：control-plane-protocol 到底代表什么】它是"某协议在某 VRF 下的一个
 * 实例"的标准 YANG 表示，三元组 (type, name, vrf) 唯一确定。各协议 daemon 不再
 * 自造一套配置根，而是统一挂在这条路径下——这也是 FRR 走向"YANG 一切配置"的
 * 基础设计。XPath 宏的拼写必须与 yang/frr-routing.yang 严格一致。
 * ============================================================================
 */
/* 【外部声明】两份注册表：带回调 / 不带回调(用途见 routing_nb.c 文件导读)。 */
extern const struct frr_yang_module_info frr_routing_info;
extern const struct frr_yang_module_info frr_routing_cli_info;

/* Mandatory callbacks. */
/* 【回调原型】control-plane-protocol 的创建与销毁，**必须**实现(注释原文
 * "Mandatory" 即指注册表里已绑定，不实现会链接失败)。
 *   create : 见 routing_nb_config.c，内部按事务阶段分派并调用 routing_create 钩子。
 *   destroy: 只处理 APPLY 阶段，清理 VRF 依赖并调用 routing_destroy 钩子。
 * 注意两个参数类型不同：create 用 nb_cb_create_args，destroy 用
 * nb_cb_destroy_args，不要混用。 */
int routing_control_plane_protocols_control_plane_protocol_create(
	struct nb_cb_create_args *args);
int routing_control_plane_protocols_control_plane_protocol_destroy(
	struct nb_cb_destroy_args *args);

/* 【XPath 宏】control-plane-protocol 列表节点的路径。
 * 三个宏的区别只在"键"的完整程度，用于不同的定位场景： */
/* 不带任何键：定位整条列表(遍历所有实例时用)。 */
#define FRR_ROUTING_XPATH                                                      \
	"/frr-routing:routing/control-plane-protocols/control-plane-protocol"

/* 带全部三个键 (type, name, vrf)：精确定位某一个协议实例。
 * 是 printf 风格格式串，调用方按顺序传三个 %s。 */
#define FRR_ROUTING_KEY_XPATH                                                  \
	"/frr-routing:routing/control-plane-protocols/"                        \
	"control-plane-protocol[type='%s'][name='%s'][vrf='%s']"

/* 只带 vrf 键：定位"某 VRF 下的全部协议实例"，用于 VRF 级批量操作。 */
#define FRR_ROUTING_KEY_XPATH_VRF                                              \
	"/frr-routing:routing/control-plane-protocols/"                        \
	"control-plane-protocol[vrf='%s']"

/*
 * callbacks for routing to handle configuration events
 * based on the control plane protocol
 */
/* 【Hook 声明】三个钩子把"通用框架"与"具体协议"解耦：框架在配置事务里回调
 * 这些钩子，各协议 daemon 启动时 hook_register 自己的处理函数，于是同一套
 * northbound 代码能服务所有协议。
 *   routing_conf_event : 在 VALIDATE 阶段调用，用于参数校验(返回非 0 即拒绝)。
 *                        DECLARE 与 DEFINE 分别在 routing_nb.h / routing_nb_config.c。
 *   routing_create     : 实例创建成功(APPLY 阶段)后调用。
 *   routing_destroy    : 实例销毁时调用。
 * 【关于 DECLARE_KOOH】hook.h 里明确写了 "DECLARE_KOOH is actually the same
 * thing as DECLARE_HOOK"。用 KOOH 只是给读者一个醒目提示：这个钩子的调用方会
 * **逆序**遍历(通常用于销毁路径)，语义上等价，宏展开无差别。 */
DECLARE_HOOK(routing_conf_event, (struct nb_cb_create_args *args), (args));
DECLARE_HOOK(routing_create, (struct nb_cb_create_args *args), (args));
DECLARE_KOOH(routing_destroy, (struct nb_cb_destroy_args *args), (args));

/* 【函数】向 northbound 注册"control-plane-protocol 与 VRF 之间的依赖"。
 * 注册后，框架才知道某条 protocol 实例对应哪个 VRF 对象，并能在 create 回调里
 * 把 VRF 指针缓存进 dnode(见 routing_nb_config.c 的 NB_EV_APPLY 分支)。
 * 不注册也能跑，只是拿不到 VRF 指针。 */
void routing_control_plane_protocols_register_vrf_dependency(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_ROUTING_NB_H_ */

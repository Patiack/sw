// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

#ifndef _FRR_NORTHBOUND_H_
#define _FRR_NORTHBOUND_H_

/*
 * ============================================================================
 * 文件导读：FRR 北向接口（northbound）核心头文件
 * ============================================================================
 *
 * 本文件定义了 FRR 各守护进程如何把内部数据结构“暴露”给 YANG 模型，以及
 * 北向（北向 = 配置管理层，如 CLI、Sysrepo、NETCONF、RESTCONF）如何驱动
 * 守护进程读写配置。核心思想：把每个 YANG 节点映射到一个 struct nb_node，
 * 节点上挂一组回调（nb_callbacks），北向框架在配置事务的不同阶段回调这些
 * 函数，由守护进程自己完成真正的增删改查。
 *
 * 关键概念：
 *   - struct nb_node      一个 YANG 节点及其回调集合、xpath 等元数据
 *   - struct nb_callbacks 回调族：create / modify / destroy / move /
 *                         pre_validate / apply_finish，以及只读的
 *                         get_elem / get_next / get_keys / lookup_entry，
 *                         还有 rpc / notify 等
 *   - enum nb_event       事务阶段：VALIDATE 到 PREPARE 到 APPLY，任一步
 *                         失败则进入 ABORT。回调需按阶段区分行为
 *   - candidate 与 running 配置事务模型：candidate 是候选配置，提交成功
 *                         后才写入 running（真正生效的配置）
 *   - enum nb_error       回调返回值约定（NB_OK / NB_ERR_* / NB_YIELD）
 *
 * 阅读顺序建议：先看 enum nb_event 与 struct nb_node 理解事务阶段与数据
 * 模型，再看 struct nb_callbacks 了解回调族，最后看文件末尾的 nb_* 函数
 * 声明了解北向框架对外提供的入口（如 nb_candidate_commit 等）。
 * ============================================================================
 */

#include "frrevent.h"
#include "hook.h"
#include "linklist.h"
#include "openbsd-tree.h"
#include "yang.h"
#include "yang_translator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration(s). */
struct vty;
struct debug;
struct nb_node;

/*
 * XPath 中一个 tag（节点）的标识：ns 为命名空间哈希值，id 为节点名哈希值。
 * 用哈希而非字符串是为了在后面做 xpath 比较时更快、更省内存。
 */
struct nb_yang_xpath_tag {
	uint32_t ns;
	uint32_t id;
};

/*
 * 一个 YANG 值的封装：底层保存 libyang 的 lyd_value，另记录其数据类型
 * value_type 和标志位 value_flags，便于在不同阶段复用同一个值对象。
 */
struct nb_yang_value {
	struct lyd_value value;
	LY_DATA_TYPE value_type;
	uint8_t value_flags;
};

/*
 * XPath 的一个元素 = 一个 tag 加上该节点携带的键值（对 list 的 key 而言）。
 */
struct nb_yang_xpath_elem {
	struct nb_yang_xpath_tag tag;
	struct nb_yang_value val;
};

/*
 * XPath 中各层级（tag）最多允许的 key 数量上限；用 UINT8_MAX 表示不设限，
 * 实际取值由各处的数组尺寸决定。
 */
#define NB_MAX_NUM_KEYS UINT8_MAX
#define NB_MAX_NUM_XPATH_TAGS UINT8_MAX

/*
 * 解析后的 xpath 表示：由 length 个 tag 组成，每个 tag 自带若干 key。
 * 相比保存原始字符串，这种结构便于按层级快速比较和定位节点。
 */
struct nb_yang_xpath {
	uint8_t length;
	struct {
		uint8_t num_keys;
		struct nb_yang_xpath_elem keys[NB_MAX_NUM_KEYS];
	} tags[NB_MAX_NUM_XPATH_TAGS];
};

/*
 * 安全地取出 xpath 中第 _indx1 个 tag 的第 _indx2 个 key 元素。
 * 会先做两层边界检查（tag 存在、key 存在），越界时返回 NULL，
 * 调用方必须判空后再使用。注意这里读的是 xpath->num_tags，而结构体中
 * 实际字段名为 length，使用时请以结构体定义为准。
 */
#define NB_YANG_XPATH_KEY(_xpath, _indx1, _indx2)                                                  \
	(((_xpath)->num_tags > (_indx1)) && ((_xpath)->tags[(_indx1)].num_keys > (_indx2))         \
		 ? &(_xpath)->tags[(_indx1)].keys[(_indx2)]                                        \
		 : NULL)

/*
 * 配置事务的四个阶段。北向框架在每个阶段调用同一批配置回调（create /
 * modify / destroy 等），回调需根据 args->event 判断当前处于哪个阶段，
 * 从而决定是校验、申请资源、回滚还是真正生效。阶段顺序：
 *   NB_EV_VALIDATE -> NB_EV_PREPARE -> NB_EV_APPLY，中途失败转 NB_EV_ABORT。
 */
/* Northbound events. */
enum nb_event {
	/*
	 * The configuration callback is supposed to verify that the changes are
	 * valid and can be applied.
	 */
	NB_EV_VALIDATE,

	/*
	 * The configuration callback is supposed to prepare all resources
	 * required to apply the changes.
	 */
	NB_EV_PREPARE,

	/*
	 * Transaction has failed, the configuration callback needs to release
	 * all resources previously allocated.
	 */
	NB_EV_ABORT,

	/*
	 * The configuration changes need to be applied. The changes can't be
	 * rejected at this point (errors are logged and ignored).
	 */
	NB_EV_APPLY,
};

/*
 * Northbound callback operations.
 *
 * Refer to the documentation comments of nb_callbacks for more details.
 */
/*
 * 回调操作的种类枚举。用于标识当前触发的是哪一种回调操作，便于在通用
 * 逻辑里（如日志、错误处理、通用 get 回调分发）区分具体动作。
 */
enum nb_cb_operation {
	NB_CB_CREATE,
	NB_CB_MODIFY,
	NB_CB_DESTROY,
	NB_CB_MOVE,
	NB_CB_PRE_VALIDATE,
	NB_CB_APPLY_FINISH,
	NB_CB_GET_ELEM,
	NB_CB_GET_NEXT,
	NB_CB_GET_KEYS,
	NB_CB_LIST_ENTRY_DONE,
	NB_CB_LOOKUP_ENTRY,
	NB_CB_RPC,
	NB_CB_NOTIFY,
};

/*
 * 北向错误码/返回码。NB_OK 表示成功；NB_ERR_* 表示各类错误；NB_YIELD 是
 * 一个特殊返回值，用于让当前回调“让出”控制权以便框架继续处理其他工作
 * （例如在长事务中分批执行），调用方需要理解其非错误语义。
 */
/* Northbound error codes. */
enum nb_error {
	NB_OK = 0,
	NB_ERR,
	NB_ERR_NO_CHANGES,
	NB_ERR_NOT_FOUND,
	NB_ERR_EXISTS,
	NB_ERR_LOCKED,
	NB_ERR_VALIDATION,
	NB_ERR_RESOURCE,
	NB_ERR_INCONSISTENCY,
	NB_YIELD,
};

/*
 * 回调在 NB_EV_PREPARE 阶段申请的资源句柄的联合体：既可能是文件描述符
 * fd，也可能是一个内存指针 ptr。同一资源会在后续 PREPARE（保存）、
 * ABORT（释放）或 APPLY（使用）阶段通过同一个指针访问，因此不可随意
 * 覆盖其内容。VALIDATE 阶段该指针为 NULL。
 */
union nb_resource {
	int fd;
	void *ptr;
};

/*
 * Northbound callbacks parameters.
 */

/*
 * create 回调入参。event 指明事务阶段；dnode 为正在创建的 libyang 节点；
 * resource 在 PREPARE 阶段用于保存所申请的资源，在 ABORT（释放）与 APPLY
 * （使用）阶段取用，VALIDATE 阶段为 NULL；errmsg 用于回填错误信息。
 */
struct nb_cb_create_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/*
	 * The transaction phase. Refer to the documentation comments of
	 * nb_event for more details.
	 */
	enum nb_event event;

	/* libyang data node that is being created. */
	const struct lyd_node *dnode;

	/*
	 * Pointer to store resource(s) allocated during the NB_EV_PREPARE
	 * phase. The same pointer can be used during the NB_EV_ABORT and
	 * NB_EV_APPLY phases to either release or make use of the allocated
	 * resource(s). It's set to NULL when the event is NB_EV_VALIDATE.
	 */
	union nb_resource *resource;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * modify 回调入参。字段含义与 nb_cb_create_args 相同，区别在于 dnode 表示
 * 值被修改的叶子节点。list 的 key 被修改时不会走 modify，而是被当作
 * “删除旧表项 + 新建表项”处理。
 */
struct nb_cb_modify_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/*
	 * The transaction phase. Refer to the documentation comments of
	 * nb_event for more details.
	 */
	enum nb_event event;

	/* libyang data node that is being modified. */
	const struct lyd_node *dnode;

	/*
	 * Pointer to store resource(s) allocated during the NB_EV_PREPARE
	 * phase. The same pointer can be used during the NB_EV_ABORT and
	 * NB_EV_APPLY phases to either release or make use of the allocated
	 * resource(s). It's set to NULL when the event is NB_EV_VALIDATE.
	 */
	union nb_resource *resource;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * destroy 回调入参。dnode 表示被删除的节点；这里没有 resource 字段，因为
 * 删除通常不需要跨阶段保存资源。注意删除可能涉及整棵子树（父节点删除时
 * 子节点回调也会被触发），回调需要自行处理级联删除。
 */
struct nb_cb_destroy_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/*
	 * The transaction phase. Refer to the documentation comments of
	 * nb_event for more details.
	 */
	enum nb_event event;

	/* libyang data node that is being deleted. */
	const struct lyd_node *dnode;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * move 回调入参。仅在 YANG 使用 ordered-by user 语句（用户自定义顺序）时
 * 才会触发，表示表项顺序发生变化。dnode 是发生移动的节点。
 */
struct nb_cb_move_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/*
	 * The transaction phase. Refer to the documentation comments of
	 * nb_event for more details.
	 */
	enum nb_event event;

	/* libyang data node that is being moved. */
	const struct lyd_node *dnode;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * pre_validate 回调入参。该回调在候选配置校验阶段（prepare 之前）触发，
 * 用于做跨节点的一致性检查（如某个选项依赖另一个节点是否已配置）。
 * 没有 event 字段，因为语义上只在校验阶段调用。
 */
struct nb_cb_pre_validate_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/* libyang data node associated with the 'pre_validate' callback. */
	const struct lyd_node *dnode;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * apply_finish 回调入参。在全部配置已 apply 完成之后触发，用于做收尾
 * 工作（如根据最终配置向内核下发路由、重新生成并下发聚合结果等）。
 * 同样无 event 字段，因为只在 apply 结束阶段调用。此阶段不能拒绝变更
 * （错误只会被记录并忽略）。
 */
struct nb_cb_apply_finish_args {
	/* Context of the configuration transaction. */
	struct nb_context *context;

	/* libyang data node associated with the 'apply_finish' callback. */
	const struct lyd_node *dnode;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * get_elem 回调入参。xpath 指明要读取的数据路径；若目标位于某个已有
 * 表项内部，list_entry 指向该表项（否则为 NULL）。用于把内部数据结构
 * 转换为配置树供北向读取。
 */
struct nb_cb_get_elem_args {
	/* YANG data path of the data we want to get. */
	const char *xpath;

	/* Pointer to list entry (might be NULL). */
	const void *list_entry;
};

/*
 * get_next 回调入参。用于遍历一个 list 或 leaf-list 的所有表项：
 * parent_list_entry 指向父表项，list_entry 为当前表项。回调需返回下一个
 * 表项指针，遍历结束时返回 NULL。配合 get_keys 完成整表枚举。
 */
struct nb_cb_get_next_args {
	/* Pointer to parent list entry. */
	const void *parent_list_entry;

	/* Pointer to (leaf-)list entry. */
	const void *list_entry;
};

/*
 * get_keys 回调入参。给定一个表项 list_entry，回调需将其关键字段填写到
 * keys 指向的 yang_list_keys 结构中（list 的 key 值）。这是把内部表项
 * 映射为 YANG 列表项、参与 xpath 定位的关键一步。
 */
struct nb_cb_get_keys_args {
	/* Pointer to list entry. */
	const void *list_entry;

	/*
	 * Structure to be filled based on the attributes of the provided list
	 * entry.
	 */
	struct yang_list_keys *keys;
};

/*
 * lookup_entry 回调入参。给定父表项 parent_list_entry 和一组 key 值 keys，
 * 回调需在内部数据结构中查找并返回匹配的表项指针，找不到返回 NULL。
 * 与 get_next/get_keys 相比，这是按 key 直接定位（随机访问）的方式。
 */
struct nb_cb_lookup_entry_args {
	/* Pointer to parent list entry. */
	const void *parent_list_entry;

	/* Structure containing the keys of the list entry. */
	const struct yang_list_keys *keys;
};

/*
 * rpc 回调入参。处理 YANG RPC 或 action：xpath 为 RPC/action 路径，input
 * 为只读的输入树，output 为需要回调填充的输出树，errmsg 用于回填错误。
 * 注意 output 由回调负责创建/填充，不要直接修改 input。
 */
struct nb_cb_rpc_args {
	/* XPath of the YANG RPC or action. */
	const char *xpath;

	/* Read-only "input" tree of the RPC/action. */
	const struct lyd_node *input;

	/* The "output" tree of the RPC/action to be populated by the callback. */
	struct lyd_node *output;

	/* Buffer to store human-readable error message in case of error. */
	char *errmsg;

	/* Size of errmsg. */
	size_t errmsg_len;
};

/*
 * notify 回调入参。用于生成 YANG notification（通知）：xpath 为通知路径，
 * op 为操作类型标识，dnode 为承载通知数据的 libyang 节点（若通知非顶层，
 * 该节点仍指向通知本身，但已挂在完整数据树中）。
 */
struct nb_cb_notify_args {
	/* XPath of the notification. */
	const char *xpath;
	uint8_t op;

	/*
	 * libyang data node representing the notification. If the notification
	 * is not top-level, it still points to the notification node, but it's
	 * part of the full data tree with all its parents.
	 */
	struct lyd_node *dnode;
};

/*
 * 一个 YANG 节点可挂载的回调集合。北向框架在配置事务各阶段回调这些函数，
 * 由守护进程完成真正的数据操作。回调分为三类：
 *   1) 配置类：create / modify / destroy / move / pre_validate / apply_finish，
 *      在每个事务阶段都会被调用（通过 args->event 区分阶段）；
 *   2) 运维数据类（只读）：get / get_elem / get_next / get_keys /
 *      lookup_entry / list_entry_done / lookup_next，把内部数据映射成配置树；
 *   3) 其它：rpc / notify 以及 CLI 展示用的 cli_show / cli_show_end / cli_cmp。
 * 未实现的回调留空（NULL）即可，框架会跳过。
 */
/*
 * Set of configuration callbacks that can be associated to a northbound node.
 */
struct nb_callbacks {
	/*
	 * Configuration callback.
	 *
	 * A presence container, list entry, leaf-list entry or leaf of type
	 * empty has been created.
	 *
	 * For presence-containers and list entries, the callback is supposed to
	 * initialize the default values of its children (if any) from the YANG
	 * models.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_create_args for
	 *    details.
	 *
	 * Returns:
	 *    - NB_OK on success.
	 *    - NB_ERR_VALIDATION when a validation error occurred.
	 *    - NB_ERR_RESOURCE when the callback failed to allocate a resource.
	 *    - NB_ERR_INCONSISTENCY when an inconsistency was detected.
	 *    - NB_ERR for other errors.
	 */
	/*
	 * create 回调：存在性容器 / 列表项 / leaf-list 项 / 空类型叶子被创建
	 * 时调用。对存在性容器和列表项，回调还需按 YANG 模型初始化子节点的
	 * 默认值。
	 */
	int (*create)(struct nb_cb_create_args *args);

	/*
	 * Configuration callback.
	 *
	 * The value of a leaf has been modified.
	 *
	 * List keys don't need to implement this callback. When a list key is
	 * modified, the northbound treats this as if the list was deleted and a
	 * new one created with the updated key value.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_modify_args for
	 *    details.
	 *
	 * Returns:
	 *    - NB_OK on success.
	 *    - NB_ERR_VALIDATION when a validation error occurred.
	 *    - NB_ERR_RESOURCE when the callback failed to allocate a resource.
	 *    - NB_ERR_INCONSISTENCY when an inconsistency was detected.
	 *    - NB_ERR for other errors.
	 */
	/*
	 * modify 回调：叶子值被修改时调用。list 的 key 不走此回调——key 的
	 * 变化会被当作“删除旧表项 + 以新 key 新建表项”处理。
	 */
	int (*modify)(struct nb_cb_modify_args *args);

	/*
	 * Configuration callback.
	 *
	 * A presence container, list entry, leaf-list entry or optional leaf
	 * has been deleted.
	 *
	 * The callback is supposed to delete the entire configuration object,
	 * including its children when they exist.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_destroy_args for
	 *    details.
	 *
	 * Returns:
	 *    - NB_OK on success.
	 *    - NB_ERR_VALIDATION when a validation error occurred.
	 *    - NB_ERR_INCONSISTENCY when an inconsistency was detected.
	 *    - NB_ERR for other errors.
	 */
	/*
	 * destroy 回调：存在性容器 / 列表项 / leaf-list 项 / 可选叶子被删除
	 * 时调用。回调需删除整个配置对象，包括其子对象（若存在）。
	 */
	int (*destroy)(struct nb_cb_destroy_args *args);

	/*
	 * Flags to control the how northbound callbacks are invoked.
	 */
	/*
	 * 控制回调调用行为的标志位。现有定义见 F_NB_CB_DESTROY_RECURSE：
	 * 置位后删除该节点时会由框架递归删除其子节点。
	 */
	uint flags;

	/*
	 * Configuration callback.
	 *
	 * A list entry or leaf-list entry has been moved. Only applicable when
	 * the "ordered-by user" statement is present.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_move_args for
	 *    details.
	 *
	 * Returns:
	 *    - NB_OK on success.
	 *    - NB_ERR_VALIDATION when a validation error occurred.
	 *    - NB_ERR_INCONSISTENCY when an inconsistency was detected.
	 *    - NB_ERR for other errors.
	 */
	/*
	 * move 回调：列表项 / leaf-list 项顺序发生变化时调用，仅当 YANG 使用
	 * ordered-by user 语句时才可能触发。
	 */
	int (*move)(struct nb_cb_move_args *args);

	/*
	 * Optional configuration callback.
	 *
	 * This callback can be used to validate subsections of the
	 * configuration being committed before validating the configuration
	 * changes themselves. It's useful to perform more complex validations
	 * that depend on the relationship between multiple nodes.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_pre_validate_args for
	 *    details.
	 *
	 * Returns:
	 *    - NB_OK on success.
	 *    - NB_ERR_VALIDATION when a validation error occurred.
	 */
	/*
	 * 可选回调：在提交前、对配置变更本身做校验之前，先对配置的各个子
	 * 部分进行校验。适用于依赖多个节点之间关系的复杂校验。
	 */
	int (*pre_validate)(struct nb_cb_pre_validate_args *args);

	/*
	 * Optional configuration callback.
	 *
	 * The 'apply_finish' callbacks are called after all other callbacks
	 * during the apply phase (NB_EV_APPLY). These callbacks are called only
	 * under one of the following two cases:
	 * - The data node has been created or modified (but not deleted);
	 * - Any change was made within the descendants of the data node (e.g. a
	 *   child leaf was modified, created or deleted).
	 *
	 * In the second case above, the 'apply_finish' callback is called only
	 * once even if multiple changes occurred within the descendants of the
	 * data node.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_apply_finish_args for
	 *    details.
	 */
	/*
	 * 可选回调：apply 阶段（NB_EV_APPLY）最后一个被调用的回调。仅在两种
	 * 情况触发：1) 该数据节点被创建或修改（删除不触发）；2) 其子孙节点
	 * 发生任何变化。第二种情况下即使子孙多次变化也只调用一次。常用于把
	 * 最终结果汇总下发（如重算并下发聚合路由）。
	 */
	void (*apply_finish)(struct nb_cb_apply_finish_args *args);

	/*
	 * Operational data callback (new direct tree add method).
	 *
	 * The callback function should create a new lyd_node (leaf) or
	 * lyd_node's (leaf list) for the value and attach to parent.
	 *
	 * nb_node
	 *    The node representing the leaf or leaf list
	 * list_entry
	 *    List entry from get_next (or NULL).
	 * parent
	 *    The parent lyd_node to attach the leaf data to.
	 *
	 * Returns:
	 *    Returns an nb_error if the data could not be added to the tree.
	 */
	/*
	 * 运维数据回调（新的直接建树方式）：为叶子 / leaf-list 构造并创建
	 * lyd_node，然后挂到 parent 上。list_entry 来自 get_next（或为 NULL）。
	 * 与 get_elem 相比更直接，返回 nb_error 表示数据是否成功加入配置树。
	 */
	enum nb_error (*get)(const struct nb_node *nb_node, const void *list_entry,
			     struct lyd_node *parent);

	/*
	 * Operational data callback.
	 *
	 * The callback function should return the value of a specific leaf,
	 * leaf-list entry or inform if a typeless value (presence containers or
	 * leafs of type empty) exists or not.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_get_elem_args for
	 *    details.
	 *
	 * Returns:
	 *    Pointer to newly created yang_data structure, or NULL to indicate
	 *    the absence of data.
	 */
	/*
	 * 运维数据回调：返回某个叶子 / leaf-list 项的值，或说明无类型值
	 * （存在性容器、空类型叶子）是否存在。返回新建的 yang_data，无数据
	 * 时返回 NULL。
	 */
	struct yang_data *(*get_elem)(struct nb_cb_get_elem_args *args);

	/*
	 * Operational data callback for YANG lists and leaf-lists.
	 *
	 * The callback function should return the next entry in the list or
	 * leaf-list. The 'list_entry' parameter will be NULL on the first
	 * invocation.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_get_next_args for
	 *    details.
	 *
	 * Returns:
	 *    Pointer to the next entry in the (leaf-)list, or NULL to signal
	 *    that the end of the (leaf-)list was reached.
	 */
	/*
	 * 运维数据回调（用于 list 与 leaf-list）：返回下一个表项，首次调用时
	 * list_entry 为 NULL。返回 NULL 表示已走到 (leaf-)list 末尾。
	 */
	const void *(*get_next)(struct nb_cb_get_next_args *args);

	/*
	 * Operational data callback for YANG lists.
	 *
	 * The callback function should fill the 'keys' parameter based on the
	 * given list_entry. Keyless lists don't need to implement this
	 * callback.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_get_keys_args for
	 *    details.
	 *
	 * Returns:
	 *    NB_OK on success, NB_ERR otherwise.
	 */
	/*
	 * 运维数据回调（用于 list）：根据 list_entry 填充 keys 参数。无 key 的
	 * list 无需实现。返回 NB_OK 成功，NB_ERR 失败。
	 */
	int (*get_keys)(struct nb_cb_get_keys_args *args);

	/*
	 * Operational data callback for YANG lists.
	 *
	 * This callback function is called to cleanup any resources that may be
	 * held by a backend opaque `list_entry` value (e.g., a lock). It is
	 * called when the northbound code is done using a `list_entry` value it
	 * obtained using the lookup_entry() callback. It is also called on the
	 * `list_entry` returned from the get_next() or lookup_next() callbacks
	 * if the iteration aborts before walking to the end of the list. The
	 * intention is to allow any resources (e.g., a lock) to now be
	 * released.
	 *
	 * args
	 *    parent_list_entry - pointer to the parent list entry
	 *    list_entry - value returned previously from `lookup_entry()`
	 */
	/*
	 * 运维数据回调（用于 list）：释放后端不透明 list_entry 所持有的资源
	 * （例如锁）。当北向用完一个由 lookup_entry() 取得的 list_entry 时会
	 * 调用；若遍历在走到列表末尾前中止，也会对 get_next / lookup_next
	 * 返回的 list_entry 调用。目的是让资源（如锁）及时释放。
	 */
	void (*list_entry_done)(const void *parent_list_entry, const void *list_entry);

	/*
	 * Operational data callback for YANG lists.
	 *
	 * The callback function should return a list entry based on the list
	 * keys given as a parameter. Keyless lists don't need to implement this
	 * callback.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_lookup_entry_args for
	 *    details.
	 *
	 * Returns:
	 *    Pointer to the list entry if found, or NULL if not found.
	 */
	/*
	 * 运维数据回调（用于 list）：根据给定 key 返回表项，找不到返回 NULL。
	 * 无 key 的 list 无需实现。
	 */
	const void *(*lookup_entry)(struct nb_cb_lookup_entry_args *args);

	/*
	 * Operational data callback for YANG lists.
	 *
	 * The callback function should return the next list entry that would
	 * follow a list entry with the keys given as a parameter. Keyless
	 * lists don't need to implement this  callback.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_lookup_entry_args for
	 *    details.
	 *
	 * Returns:
	 *    Pointer to the list entry if found, or NULL if not found.
	 */
	/*
	 * 运维数据回调（用于 list）：返回“紧跟在给定 key 的表项之后”的表项。
	 * 无 key 的 list 无需实现。返回表项指针，找不到返回 NULL。
	 */
	const void *(*lookup_next)(struct nb_cb_lookup_entry_args *args);

	/*
	 * RPC and action callback.
	 *
	 * Both 'input' and 'output' are lists of 'yang_data' structures. The
	 * callback should fetch all the input parameters from the 'input' list,
	 * and add output parameters to the 'output' list if necessary.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_rpc_args for details.
	 *
	 * Returns:
	 *    NB_OK on success, NB_ERR otherwise.
	 */
	/*
	 * RPC 与 action 回调：input 为只读的输入参数，output 为需回调填充的
	 * 输出参数。回调从 input 读取参数，必要时向 output 添加输出项。
	 */
	int (*rpc)(struct nb_cb_rpc_args *args);

	/*
	 * Notification callback.
	 *
	 * The callback is called when a YANG notification is received.
	 *
	 * args
	 *    Refer to the documentation comments of nb_cb_notify_args for
	 *    details.
	 */
	/*
	 * notification 回调：收到 YANG 通知（notification）时被调用。
	 */
	void (*notify)(struct nb_cb_notify_args *args);

	/*
	 * Optional callback to compare the data nodes when printing
	 * the CLI commands associated with them.
	 *
	 * dnode1
	 *    The first data node to compare.
	 *
	 * dnode2
	 *    The second data node to compare.
	 *
	 * Returns:
	 *    <0 when the CLI command for the dnode1 should be printed first
	 *    >0 when the CLI command for the dnode2 should be printed first
	 *     0 when there is no difference
	 */
	/*
	 * 可选回调：把与数据节点关联的 CLI 命令输出时，用于比较两个节点的
	 * 打印先后。返回负值表示 dnode1 的命令先打印，正值表示 dnode2 先
	 * 打印，0 表示无差别。
	 */
	int (*cli_cmp)(const struct lyd_node *dnode1,
		       const struct lyd_node *dnode2);

	/*
	 * Optional callback to show the CLI command associated to the given
	 * YANG data node.
	 *
	 * vty
	 *    The vty terminal to dump the configuration to.
	 *
	 * dnode
	 *    libyang data node that should be shown in the form of a CLI
	 *    command.
	 *
	 * show_defaults
	 *    Specify whether to display default configuration values or not.
	 *    This parameter can be ignored most of the time since the
	 *    northbound doesn't call this callback for default leaves or
	 *    non-presence containers that contain only default child nodes.
	 *    The exception are commands associated to multiple configuration
	 *    nodes, in which case it might be desirable to hide one or more
	 *    parts of the command when this parameter is set to false.
	 */
	/*
	 * 可选回调：把给定 YANG 数据节点以 CLI 命令形式输出到 vty。
	 * show_defaults 表示是否显示默认配置值；多数情况下可忽略，因为框架
	 * 不会对默认叶子、或只含默认子节点的非存在性容器调用此回调。例外是
	 * 一条命令关联多个配置节点时，可能需要据此隐藏命令的某部分。
	 */
	void (*cli_show)(struct vty *vty, const struct lyd_node *dnode,
			 bool show_defaults);

	/*
	 * Optional callback to show the CLI node end for lists or containers.
	 *
	 * vty
	 *    The vty terminal to dump the configuration to.
	 *
	 * dnode
	 *    libyang data node that should be shown in the form of a CLI
	 *    command.
	 */
	/*
	 * 可选回调：为 list 或容器输出 CLI 节点的结束部分（如闭合大括号
	 * 对应的收尾命令）。
	 */
	void (*cli_show_end)(struct vty *vty, const struct lyd_node *dnode);
};

/*
 * Flag indicating the northbound should recurse destroy the children of this
 * node when it is destroyed.
 */
/*
 * 标志位：置位后，删除该节点时北向会递归删除其所有子节点（由框架代劳，
 * 回调无需自己遍历删除子对象）。
 */
#define F_NB_CB_DESTROY_RECURSE 0x01

/*
 * 依赖关系回调，用于描述配置节点之间的依赖。给定一个数据节点 dnode，
 * get_dependant_xpath 返回“依赖它”的节点 xpath，get_dependency_xpath
 * 返回“它依赖”的节点 xpath。北向借此在节点变化时找到需要一并校验 /
 * 重算的相关节点。
 */
struct nb_dependency_callbacks {
	void (*get_dependant_xpath)(const struct lyd_node *dnode, char *xpath);
	void (*get_dependency_xpath)(const struct lyd_node *dnode, char *xpath);
};

/*
 * 每个原生 YANG 模块的 schema 节点都会分配一个 struct nb_node。它是整个
 * 北向接口的核心数据结构：把 libyang 的 schema 节点（snode）、数据路径
 * （xpath）、优先级、回调集合（cbs）、父子关系等串在一起。北向在处理配置
 * 时，先由 snode 找到对应的 nb_node，再调用其 cbs 中的回调。
 */
/*
 * Northbound-specific data that is allocated for each schema node of the native
 * YANG modules.
 */
struct nb_node {
	/* Back pointer to the libyang schema node. */
	/*
	 * 指回对应的 libyang schema 节点：北向据此知道本 nb_node 描述的是
	 * 哪个 YANG 定义（类型、名字、取值范围等都由这里提供）。
	 */
	const struct lysc_node *snode;

	/* Data path of this YANG node. */
	/*
	 * 本 YANG 节点的数据路径（xpath），用于在配置树中定位节点，
	 * 也是注册 / 查找 nb_node 时的关键标识。
	 */
	char xpath[XPATH_MAXLEN];

	/* Priority - lower priorities are processed first. */
	/*
	 * 处理优先级，数值越小越先被处理。用于控制同一批次中多个节点
	 * 回调的执行先后，避免依赖顺序出错。
	 */
	uint32_t priority;

	/*
	 * 本节点的依赖关系回调（描述它依赖谁、谁依赖它），供北向在节点
	 * 变化时找到需要一并校验 / 重算的相关节点。
	 */
	struct nb_dependency_callbacks dep_cbs;

	/* Callbacks implemented for this node. */
	/*
	 * 为本节点实现的回调集合（create / modify / get_elem 等），
	 * 北向处理该节点时就从这里调用具体实现。
	 */
	struct nb_callbacks cbs;

	/*
	 * Pointer to the parent node (disconsidering non-presence containers).
	 */
	/*
	 * 指向父 nb_node，但会“跳过”非存在性容器（non-presence container），
	 * 因此父节点通常是最近的 list 或存在性容器。
	 */
	struct nb_node *parent;

	/* Pointer to the nearest parent list, if any. */
	/*
	 * 指向最近的上一层 list 节点（若有），便于在只读回调中从表项
	 * 回溯到其所属列表。
	 */
	struct nb_node *parent_list;

	/* Flags. */
	/*
	 * 节点级标志位，取值见下面的 F_NB_NODE_* 宏。
	 */
	uint8_t flags;
};
/*
 * struct nb_node 的 flags 取值：
 *   F_NB_NODE_CONFIG_ONLY    该容器 / list 只包含配置数据
 *   F_NB_NODE_KEYLESS_LIST   该 list 没有 key 叶子
 *   F_NB_NODE_IGNORE_CFG_CBS 忽略该节点的配置类回调
 *   F_NB_NODE_HAS_GET_TREE   该节点使用“整树”读取方式（get_tree_locked）
 */
/* The YANG container or list contains only config data. */
#define F_NB_NODE_CONFIG_ONLY 0x01
/* The YANG list doesn't contain key leafs. */
#define F_NB_NODE_KEYLESS_LIST 0x02
/* Ignore config callbacks for this node */
#define F_NB_NODE_IGNORE_CFG_CBS 0x04
/* Ignore state callbacks for this node */
#define F_NB_NODE_HAS_GET_TREE 0x08

/*
 * 为规避旧版 gcc（< 5.x）在共享库中处理 C99 柔性数组的 bug，为节点数组
 * 设定一个固定上限；较新的编译器则使用柔性数组（见下面 nodes[]）。该值
 * 表示单个模块最多能注册多少个节点。
 */
/*
 * HACK: old gcc versions (< 5.x) have a bug that prevents C99 flexible arrays
 * from working properly on shared libraries. For those compilers, use a fixed
 * size array to work around the problem.
 */
#define YANG_MODULE_MAX_NODES 2000

/*
 * 描述一个 YANG 模块的北向注册信息：模块名、是否忽略配置回调、支持的特性
 * 列表、可选的“整树”读取回调，以及该模块中每个节点的 xpath 与回调集合
 * （最后的 nodes 数组）。守护进程用一组这样的结构（以 name 为 NULL 结尾）
 * 调用注册接口，一次性完成整模块的北向信息登记。
 */
struct frr_yang_module_info {
	/* YANG module name. */
	const char *name;

	/*
	 * Ignore configuration callbacks for this module. Set this to true to
	 * load module with only CLI-related callbacks. This is useful for
	 * modules loaded in mgmtd.
	 */
	bool ignore_cfg_cbs;

	/*
	 * The NULL-terminated list of supported features.
	 * Features are defined with "feature" statements in the YANG model.
	 * Use ["*", NULL] to enable all features.
	 * Use NULL to disable all features.
	 */
	const char **features;

	/*
	 * If the module keeps its oper-state in a libyang tree
	 * this function should return that tree (locked if multi-threading).
	 * If this function is provided then the state callback functions
	 * (get_elem, get_keys, get_next, lookup_entry) need not be set for a
	 * module. The unlock_tree function if non-NULL will be called with
	 * the returned tree and the *user_lock value.
	 */
	/*
	 * 若模块把运维状态保存在一棵 libyang 树中，则由该函数返回这棵树
	 * （多线程场景下需加锁，锁句柄通过 user_lock 回传）。一旦提供它，
	 * 模块就无需再实现 get_elem / get_keys / get_next / lookup_entry 这些
	 * 逐节点读取的回调。返回的树由配套的 unlock_tree 解锁。
	 */
	const struct lyd_node *(*get_tree_locked)(const char *xpath, void **user_lock);

	/*
	 * This function will be called following a call to get_tree_locked() in
	 * order to unlock the tree if locking was required.
	 */
	/*
	 * 与 get_tree_locked 配对：在取用完整树之后调用，用于解除此前施加的
	 * 锁。tree 为之前返回的树，user_lock 为取树时回传的锁句柄。
	 */
	void (*unlock_tree)(const struct lyd_node *tree, void *user_lock);

	/* Northbound callbacks. */
	/*
	 * 该模块所有节点的注册表：每个条目给出一个 YANG 节点的数据路径（xpath）
	 * 及其回调集合与优先级。数组以 xpath 为 NULL 的条目作为结束标记。
	 */
	struct {
		/* Data path of this YANG node. */
		/*
		 * 该节点的数据路径，注册表中以此定位节点。
		 */
		const char *xpath;

		/* Callbacks implemented for this node. */
		/*
		 * 为该节点实现的回调集合。
		 */
		struct nb_callbacks cbs;

		/* Priority - lower priorities are processed first. */
		/*
		 * 处理优先级，数值越小越先处理。
		 */
		uint32_t priority;
#if defined(__GNUC__) && ((__GNUC__ - 0) < 5) && !defined(__clang__)
	} nodes[YANG_MODULE_MAX_NODES + 1];
#else
	} nodes[];
#endif
};

/*
 * 默认优先级，取 UINT32_MAX 的一半：既给“更低优先级（更早执行）”预留空间，
 * 也给“更高优先级（更晚执行）”预留空间。
 */
/* Default priority. */
#define NB_DFLT_PRIORITY (UINT32_MAX / 2)

/*
 * 默认最多保存多少份配置回滚点。超过该数量后会丢弃最旧的记录。
 */
/* Default maximum of configuration rollbacks to store. */
#define NB_DLFT_MAX_CONFIG_ROLLBACKS 20

/*
 * 北向客户端类型：标识本次配置操作来自哪个前端，不同客户端在权限、
 * 行为（如是否直接写 running、是否触发通知）上可能不同。
 * CLI=命令行；SYSREPO=sysrepo；GRPC=gRPC；PCEP=PCEP 控制器；
 * MGMTD_SERVER / MGMTD_BE=统一管理框架 mgmtd 的前端 / 后端。
 */
/* Northbound clients. */
enum nb_client {
	NB_CLIENT_NONE = 0,
	NB_CLIENT_CLI,
	NB_CLIENT_SYSREPO,
	NB_CLIENT_GRPC,
	NB_CLIENT_PCEP,
	NB_CLIENT_MGMTD_SERVER,
	NB_CLIENT_MGMTD_BE,
};

/*
 * 北向上下文：记录本次操作来自哪个客户端（client）以及用户标识（user，
 * 可为 NULL）。它随事务在各阶段传递，供回调与日志使用。
 */
/* Northbound context. */
struct nb_context {
	/* Northbound client. */
	enum nb_client client;

	/* Northbound user (can be NULL). */
	const void *user;
};

/*
 * 记录一次配置变更所对应的回调：操作类型 operation、序号 seq（用于在同一
 * 过程中定序）、目标 nb_node 以及发生变化的 libyang 节点 dnode。它们是
 * “变更集”的基本元素，被组织进下面的红黑树 nb_config_cbs 中。
 */
/* Northbound configuration callback. */
struct nb_config_cb {
	RB_ENTRY(nb_config_cb) entry;
	enum nb_cb_operation operation;
	uint32_t seq;
	const struct nb_node *nb_node;
	const struct lyd_node *dnode;
};
/* 以 nb_config_cb 为节点类型、按 nb_config_cb_compare 排序的红黑树。 */
RB_HEAD(nb_config_cbs, nb_config_cb);
RB_PROTOTYPE(nb_config_cbs, nb_config_cb, entry, nb_config_cb_compare);

/*
 * 在 nb_config_cb 基础上扩展：额外保存该变更在 PREPARE 阶段申请的资源
 * （resource）以及 prepare_ok 标志，用于判断该变更是否已成功准备，从而
 * 在 APPLY 或 ABORT 阶段正确处理。
 */
/* Northbound configuration change. */
struct nb_config_change {
	struct nb_config_cb cb;
	union nb_resource resource;
	bool prepare_ok;
};

/*
 * 一次配置事务的完整描述：上下文 context、注释 comment、目标配置 config
 * （通常是 candidate）以及本次事务涉及的变更集合 changes。事务的校验、
 * 准备、应用、回滚都围绕这份变更集展开。
 */
/* Northbound configuration transaction. */
struct nb_transaction {
	struct nb_context context;
	char comment[80];
	struct nb_config *config;
	struct nb_config_cbs changes;
};

/*
 * 一份配置（配置树）：dnode 为数据树根节点，version 为版本号，用于比较
 * 两份配置是否相同、判断是否真的发生了变化。
 */
/* Northbound configuration. */
struct nb_config {
	struct lyd_node *dnode;
	uint32_t version;
};

/*
 * 配置操作类型，语义遵循 RFC 8072 第 2.5 节（YANG Patch 的 operation）：
 * CREATE_EXCL=排他创建（已存在则报错）；CREATE=创建 / 合并（兼容保留）；
 * MODIFY=合并修改；DESTROY=移除子树（父节点可保持不变）；DELETE=删除；
 * REPLACE=替换；MOVE=移动顺序。
 */
/*
 * Northbound operations. The semantics of operations is explained in RFC 8072,
 * section 2.5: https://datatracker.ietf.org/doc/html/rfc8072#section-2.5.
 */
enum nb_operation {
	NB_OP_CREATE_EXCL,	/* "create" */
	NB_OP_CREATE,		/* "merge" - kept for backward compatibility */
	NB_OP_MODIFY,		/* "merge" */
	NB_OP_DESTROY,		/* "remove" */
	NB_OP_DELETE,		/* "delete" */
	NB_OP_REPLACE,		/* "replace" */
	NB_OP_MOVE,		/* "move" */
};

/*
 * 一条来自客户端的配置变更请求：目标路径 xpath、操作类型 operation，以及
 * 可选的字符串值 value（例如取值设定时的内容）。
 */
struct nb_cfg_change {
	char xpath[XPATH_MAXLEN];
	enum nb_operation operation;
	const char *value;
};

/*
 * nb_oper_data_iterate() 使用的回调：遍历运维数据树时对每个数据节点调用，
 * 由调用方决定如何处理（snode 为 schema 节点，translator 为可选的模型
 * 翻译器，data 为该节点的数据，arg 为用户参数）。
 */
/* Callback function used by nb_oper_data_iterate(). */
typedef int (*nb_oper_data_cb)(const struct lysc_node *snode,
			       struct yang_translator *translator,
			       struct yang_data *data, void *arg);

/**
 * nb_oper_data_finish_cb() - finish a portion or all of a oper data walk.
 * @tree: r/o copy of the tree created during this portion of the walk.
 * @arg: finish arg passed to nb_op_iterate_yielding.
 * @ret: NB_OK if done with walk, NB_YIELD if done with portion, otherwise an
 *        error.
 *
 * If nb_op_iterate_yielding() was passed with @should_batch set then this
 * callback will be invoked during each portion (batch) of the walk with @ret
 * set to NB_YIELD.
 *
 * The @tree is read-only and should not be modified or freed.
 *
 * When @ret is NB_YIELD and this function returns anything but NB_OK then the
 * walk will be terminated, and this function *will* be called again with @ret
 * set the non-NB_OK return value it just returned. This allows the callback
 * have a single bit of code to send an error message and do any cleanup for any
 * type of failure, whether that failure was from itself or from the infra code.
 *
 * Return: NB_OK or an error during handling of @ret == NB_YIELD otherwise the
 * value is ignored.
 */
/*
 * nb_oper_data_iter_yielding() 使用的收尾回调：当运维数据遍历分批（batch）
 * 完成一段或整体结束时调用。ret 为 NB_YIELD 表示只是分段完成；返回非
 * NB_OK 会终止整个遍历。
 */
/* Callback function used by nb_oper_data_iter_yielding(). */
typedef enum nb_error (*nb_oper_data_finish_cb)(const struct lyd_node *tree,
						void *arg, enum nb_error ret);

/*
 * 遍历标志：只遍历直接子节点，不递归进入更深层级。
 */
/* Iterate over direct child nodes only. */
#define NB_OPER_DATA_ITER_NORECURSE 0x0001

/*
 * 通知发送钩子：nb_notification_send 以参数列表形式发送通知，
 * nb_notification_tree_send 以整棵数据树形式发送。各模块可注册钩子实现来
 * 接收这些通知事件。
 */
/* Hooks. */
DECLARE_HOOK(nb_notification_send, (const char *xpath, struct list *arguments),
	     (xpath, arguments));
DECLARE_HOOK(nb_notification_tree_send,
	     (const char *xpath, const struct lyd_node *tree), (xpath, tree));

/*
 * 北向相关的调试开关（由 debug 命令控制），分别对应配置回调、状态回调、
 * RPC 回调、通知回调、通知本身、事件处理，以及 libyang 层面的调试输出。
 */
/* Northbound debugging records */
extern struct debug nb_dbg_cbs_config;
extern struct debug nb_dbg_cbs_state;
extern struct debug nb_dbg_cbs_rpc;
extern struct debug nb_dbg_cbs_notify;
extern struct debug nb_dbg_notif;
extern struct debug nb_dbg_events;
extern struct debug nb_dbg_libyang;

/*
 * 全局 running 配置（真正生效的配置树）指针，整个进程共享；candidate 提交
 * 成功后即写入它。
 */
/* Global running configuration. */
extern struct nb_config *running_config;

/*
 * 全局通知过滤器：只允许其中列出的 xpath 的通知被发送；为 NULL 表示不过滤。
 */
/* Global notification filters */
extern const char **nb_notif_filters;

/*
 * 北向回调的包装函数（对外公开）：内部会先做空指针检查与调试处理，再调用
 * 相应 nb_node 上的回调实现，供其它模块安全地间接触发回调。
 */
/* Wrappers for the northbound callbacks. */
/*
 * 回调包装：查询 / 触发该节点新的运维数据读取路径，返回取得的 yang_data，
 * 无数据返回 NULL。内部会做空指针与调试处理。
 */
extern struct yang_data *nb_callback_has_new_get_elem(const struct nb_node *nb_node);

/*
 * 回调包装：按 xpath 调用节点的 get_elem 回调取得运维数据，list_entry 为
 * 可选表项（无则 NULL）。返回 NULL 表示无数据。
 */
extern struct yang_data *nb_callback_get_elem(const struct nb_node *nb_node, const char *xpath,
					      const void *list_entry);
/*
 * 回调包装：调用节点的 get_next 回调返回下一个表项（首次 list_entry 为
 * NULL）；返回 NULL 表示遍历结束。
 */
extern const void *nb_callback_get_next(const struct nb_node *nb_node,
					const void *parent_list_entry,
					const void *list_entry);
/*
 * 回调包装：调用节点的 get_keys 回调，根据表项填充 keys，返回 NB_OK / NB_ERR。
 */
extern int nb_callback_get_keys(const struct nb_node *nb_node,
				const void *list_entry,
				struct yang_list_keys *keys);
/*
 * 回调包装：调用节点的 lookup_entry 回调，按父表项与 keys 查找表项指针，
 * 找不到返回 NULL。
 */
extern const void *nb_callback_lookup_entry(const struct nb_node *nb_node,
					    const void *parent_list_entry,
					    const struct yang_list_keys *keys);
/*
 * 回调包装：调用节点的 list_entry_done 回调，通知其释放表项持有的资源。
 */
extern void nb_callback_list_entry_done(const struct nb_node *nb_node,
					const void *parent_list_entry, const void *list_entry);
/*
 * 回调包装：根据一个 libyang 节点与父表项，查找其对应的内部表项指针。
 */
extern const void *nb_callback_lookup_node_entry(struct lyd_node *node,
						 const void *parent_list_entry);
/*
 * 回调包装：调用节点的 lookup_next 回调，返回紧随指定 keys 之后的表项。
 */
extern const void *nb_callback_lookup_next(const struct nb_node *nb_node,
					   const void *parent_list_entry,
					   const struct yang_list_keys *keys);
/*
 * 回调包装：调用节点的 rpc 回调处理 YANG RPC / action。errmsg / errmsg_len
 * 用于回填错误信息。
 */
extern int nb_callback_rpc(const struct nb_node *nb_node, const char *xpath,
			   const struct lyd_node *input, struct lyd_node *output,
			   char *errmsg, size_t errmsg_len);
/*
 * 回调包装：调用节点的 notify 回调处理 YANG 通知（op 为操作类型）。
 */
extern void nb_callback_notify(const struct nb_node *nb_node, uint8_t op, const char *xpath,
			       struct lyd_node *dnode);

/*
 * 为所有已加载 YANG 模块的 schema 节点创建 nb_node：分配结构并按模块注册
 * 信息填充 xpath、回调、优先级等。通常在模块加载后调用一次。
 */
/*
 * Create a northbound node for all YANG schema nodes.
 */
void nb_nodes_create(void);

/*
 * 删除（释放）此前为所有 schema 节点创建的 nb_node。通常在模块卸载时调用。
 */
/*
 * Delete all northbound nodes from all YANG schema nodes.
 */
void nb_nodes_delete(void);

/*
 * 按数据路径 xpath 查找对应的 nb_node（xpath 可带或不带谓词）。找到返回
 * 指针，否则返回 NULL。
 */
/*
 * Find the northbound node corresponding to a YANG data path.
 *
 * xpath
 *    XPath to search for (with or without predicates).
 *
 * Returns:
 *    Pointer to northbound node if found, NULL otherwise.
 */
extern struct nb_node *nb_node_find(const char *xpath);

/*
 * 按复杂 xpath 查找对应的多个 nb_node，返回动态数组（darr，元素为
 * struct nb_node *）。使用完后需自行释放该数组。
 */
/**
 * nb_nodes_find() - find the NB nodes corresponding to complex xpath.
 * @xpath: XPath to search for (with or without predicates).
 *
 * Return: a dynamic array (darr) of `struct nb_node *`s.
 */
extern struct nb_node **nb_nodes_find(const char *xpath);

/*
 * 注册节点之间的依赖关系回调：dependency_xpath 为“被依赖”的节点路径，
 * dependant_xpath 为“依赖它”的节点路径，cbs 为依附在这对关系上的回调。
 */
extern void nb_node_set_dependency_cbs(const char *dependency_xpath,
				       const char *dependant_xpath,
				       struct nb_dependency_callbacks *cbs);

/*
 * 判断某 nb_node 是否配置了依赖关系回调；有则返回 true。
 */
bool nb_node_has_dependency(struct nb_node *node);

/*
 * 新建一份北向配置。dnode 为承载配置数据的 libyang 节点；若为 NULL 则创建
 * 一份空配置。返回新建的 nb_config。
 */
/*
 * Create a new northbound configuration.
 *
 * dnode
 *    Pointer to a libyang data node containing the configuration data. If NULL
 *    is given, an empty configuration will be created.
 *
 * Returns:
 *    Pointer to newly created northbound configuration.
 */
extern struct nb_config *nb_config_new(struct lyd_node *dnode);

/*
 * 删除并释放一份北向配置（包括其数据树）。
 */
/*
 * Delete a northbound configuration.
 *
 * config
 *    Pointer to the config that is going to be deleted.
 */
extern void nb_config_free(struct nb_config *config);

/*
 * 复制一份北向配置，返回其副本（深拷贝，副本与原配置相互独立）。
 */
/*
 * Duplicate a northbound configuration.
 *
 * config
 *    Northbound configuration to duplicate.
 *
 * Returns:
 *    Pointer to duplicated configuration.
 */
extern struct nb_config *nb_config_dup(const struct nb_config *config);

/*
 * 把 config_src 合并进 config_dst。preserve_source 为 true 时保留源配置，
 * 否则合并后销毁源配置。返回 NB_OK 成功，NB_ERR 失败。
 */
/*
 * Merge one configuration into another.
 *
 * config_dst
 *    Configuration to merge to.
 *
 * config_src
 *    Configuration to merge config_dst with.
 *
 * preserve_source
 *    Specify whether config_src should be deleted or not after the merge
 *    operation.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
extern int nb_config_merge(struct nb_config *config_dst,
			   struct nb_config *config_src, bool preserve_source);

/*
 * 用 config_src 替换 config_dst。preserve_source 决定替换后是否保留源配置。
 */
/*
 * Replace one configuration by another.
 *
 * config_dst
 *    Configuration to be replaced.
 *
 * config_src
 *    Configuration to replace config_dst.
 *
 * preserve_source
 *    Specify whether config_src should be deleted or not after the replace
 *    operation.
 */
extern void nb_config_replace(struct nb_config *config_dst,
			      struct nb_config *config_src,
			      bool preserve_source);

/*
 * 返回某个配置操作类型对应的可读字符串名（如 "create"、"delete"）。
 */
/*
 * Return a human-readable string representing a northbound operation.
 *
 * operation
 *    Northbound operation.
 *
 * Returns:
 *    String representation of the given northbound operation.
 */
extern const char *nb_operation_name(enum nb_operation operation);

/*
 * 判断对给定节点是否允许执行某种配置操作（依据节点的回调与标志位）。允许
 * 返回 true，否则返回 false。
 */
/*
 * Validate if the northbound operation is allowed for the given node.
 *
 * nb_node
 *    Northbound node.
 *
 * operation
 *    Operation we want to check.
 *
 * Returns:
 *    true if the operation is allowed, false otherwise.
 */
extern bool nb_is_operation_allowed(struct nb_node *nb_node,
				    enum nb_operation oper);

/*
 * 编辑候选配置（candidate）：按 operation 与 xpath 在 candidate 上施加一次
 * 变更，value 为可选取值。返回 NB_OK 成功，NB_ERR 失败。
 */
/*
 * Edit a candidate configuration.
 *
 * candidate
 *    Candidate configuration to edit.
 *
 * nb_node
 *    Northbound node associated to the configuration being edited.
 *
 * operation
 *    Operation to apply.
 *
 * xpath
 *    XPath of the configuration node being edited.
 *
 * value
 *    New value of the configuration node.
 *
 * Returns:
 *    - NB_OK on success.
 *    - NB_ERR for other errors.
 */
extern int nb_candidate_edit(struct nb_config *candidate, const struct nb_node *nb_node,
			     enum nb_operation operation, const char *xpath, const char *value);

/*
 * 编辑候选配置，取值以 JSON/XML 形式给出。format 指定数据格式，data 为新的
 * 数据树文本；created 为输出参数，指示节点是被新建还是仅更新；
 * xpath_created 在操作为 create 时返回新建节点的路径。
 */
/*
 * Edit a candidate configuration. Value is given as JSON/XML.
 *
 * candidate
 *    Candidate configuration to edit.
 *
 * operation
 *    Operation to apply.
 *
 * format
 *    LYD_FORMAT of the value.
 *
 * xpath
 *    XPath of the configuration node being edited.
 *    For create, it must be the parent.
 *
 * data
 *    New data tree for the node.
 *
 * created
 *    OUT param set accordingly if a node was created or just updated
 *
 * xpath_created
 *    XPath of the created node if operation is "create".
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    - NB_OK on success.
 *    - NB_ERR for other errors.
 */
extern int nb_candidate_edit_tree(struct nb_config *candidate,
				  enum nb_operation operation, LYD_FORMAT format,
				  const char *xpath, const char *data,
				  bool *created, char *xpath_created,
				  char *errmsg, size_t errmsg_len);

/*
 * 为一份配置计算增量：给定 libyang 数据节点 dnode，生成对应的变更记录并追加
 * 到 changes，同时通过 seq 返回分配给该变更的序号。
 */
/*
 * Create diff for configuration.
 *
 * dnode
 *    Pointer to a libyang data node containing the configuration data. If NULL
 *    is given, an empty configuration will be created.
 *
 * seq
 *    Returns sequence number assigned to the specific change.
 *
 * changes
 *    Northbound config callback head.
 */
extern void nb_config_diff_created(const struct lyd_node *dnode, uint32_t *seq,
				   struct nb_config_cbs *changes);

/*
 * 判断候选配置是否已过期、需要基于最新 running 重新更新；过期返回 true。
 */
/*
 * Check if a candidate configuration is outdated and needs to be updated.
 *
 * candidate
 *    Candidate configuration to check.
 *
 * Returns:
 *    true if the candidate is outdated, false otherwise.
 */
extern bool nb_candidate_needs_update(const struct nb_config *candidate);


/*
 * 单条配置变更的编辑结果：
 *   NB_CHANGE_OK       成功
 *   NB_CHANGE_ERR      失败（应中止后续变更处理）
 *   NB_CHANGE_ERR_CONT 失败，但可继续处理后续变更
 */
enum nb_change_result {
	NB_CHANGE_OK,
	NB_CHANGE_ERR,
	NB_CHANGE_ERR_CONT,
};

/*
 * 以“单条变更”形式编辑候选配置：给定 operation、绝对 xpath 与 value。
 * 若 in_backend 为 true，则忽略因 schema 非存在性导致的错误。返回
 * nb_change_result 表示结果。
 */
/*
 * Edit candidate configuration given single change.
 *
 * candidate_config
 *    Candidate configuration to edit.
 *
 * operation
 *    The type of change.
 *
 * xpath
 *     Absolute xpath of the configuration node being edited.
 *
 * value
 *    New value of the configuration node.
 *
 * in_backend
 *    If true then ignore errors due to schema non-presence.
 */
extern enum nb_change_result nb_candidate_edit_config_change(struct nb_config *candidate_config,
							     enum nb_operation operation,
							     const char *xpath, const char *value,
							     bool in_backend);

/*
 * 批量编辑候选配置的多个变更：cfg_changes 为变更数组，num_cfg_changes 为
 * 数量，xpath_base 为基准路径，in_backend 表示是否在后端应用。出错时经
 * err_buf / err_bufsize 回填信息，error 输出是否发生错误。
 */
/*
 * Edit candidate configuration changes.
 *
 * candidate_config
 *    Candidate configuration to edit.
 *
 * cfg_changes
 *    Northbound config changes.
 *
 * num_cfg_changes
 *    Number of config changes.
 *
 * xpath_base
 *    Base xpath for config.
 *
 * in_backend
 *    Specify whether the changes are being applied in the backend or not.
 *
 * err_buf
 *    Buffer to store human-readable error message in case of error.
 *
 * err_bufsize
 *    Size of err_buf.
 *
 * error
 *    TRUE on error, FALSE on success
 */
extern void nb_candidate_edit_config_changes(struct nb_config *candidate_config,
					     struct nb_cfg_change cfg_changes[],
					     size_t num_cfg_changes,
					     const char *xpath_base,
					     bool in_backend, char *err_buf,
					     int err_bufsize, bool *error);


/*
 * 向变更集 changes 追加一条变更记录（操作 operation 与节点 dnode），并通过
 * seq 递增分配序号。
 */
extern void nb_config_diff_add_change(struct nb_config_cbs *changes,
				      enum nb_cb_operation operation,
				      uint32_t *seq,
				      const struct lyd_node *dnode);
/*
 * 删除（释放）一个变更集 changes 中的所有变更记录。
 */
/*
 * Delete candidate configuration changes.
 *
 * changes
 *    Northbound config changes.
 */
extern void nb_config_diff_del_changes(struct nb_config_cbs *changes);

/*
 * 生成候选配置的差异并对其进行 YANG 层面的校验。context 为事务上下文，
 * changes 为变更集；出错时经 errmsg 回填信息。返回 NB_OK 或 NB_ERR_VALIDATION。
 */
/*
 * Create candidate diff and validate on yang tree
 *
 * context
 *    Context of the northbound transaction.
 *
 * candidate
 *    Candidate DB configuration.
 *
 * changes
 *    Northbound config changes.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    NB_OK on success, NB_ERR_VALIDATION otherwise
 */
extern int nb_candidate_diff_and_validate_yang(struct nb_context *context,
					       struct nb_config *candidate,
					       struct nb_config_cbs *changes,
					       char *errmsg, size_t errmsg_len);

/*
 * 计算两份配置之间的差异：以 reference（如 running）为基准，计算 incremental
 * （如 candidate）相对它的变化，最终结果写入 changes。
 */
/*
 * Calculate the delta between two different configurations.
 *
 * reference
 *    Running DB config changes to be compared against.
 *
 * incremental
 *    Candidate DB config changes that will be compared against reference.
 *
 * changes
 *    Will hold the final diff generated.
 *
 */
extern void nb_config_diff(const struct nb_config *reference,
			   const struct nb_config *incremental,
			   struct nb_config_cbs *changes);

/*
 * 执行 YANG 的语法与语义校验。注意：lyd_validate() 在校验过程中可能修改配置
 * （例如补充默认值），这是预期行为。返回 NB_OK 或 NB_ERR_VALIDATION。
 */
/*
 * Perform YANG syntactic and semantic validation.
 *
 * WARNING: lyd_validate() can change the configuration as part of the
 * validation process.
 *
 * candidate
 *    Candidate DB configuration.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    NB_OK on success, NB_ERR_VALIDATION otherwise
 */
extern int nb_candidate_validate_yang(struct nb_config *candidate,
				      bool no_state, char *errmsg,
				      size_t errmsg_len);

/*
 * 使用北向回调执行“代码级”校验：遍历 changes，调用各节点的校验逻辑。返回
 * NB_OK 或 NB_ERR_VALIDATION。
 */
/*
 * Perform code-level validation using the northbound callbacks.
 *
 * context
 *    Context of the northbound transaction.
 *
 * candidate
 *    Candidate DB configuration.
 *
 * changes
 *    Northbound config changes.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    NB_OK on success, NB_ERR_VALIDATION otherwise
 */
extern int nb_candidate_validate_code(struct nb_context *context,
				      struct nb_config *candidate,
				      struct nb_config_cbs *changes,
				      char *errmsg, size_t errmsg_len);

/*
 * 更新候选配置：把候选中的变更 rebase 到最新的 running 之上。发生冲突时优先
 * 保留候选配置中的修改。返回 NB_OK 或 NB_ERR。
 */
/*
 * Update a candidate configuration by rebasing the changes on top of the latest
 * running configuration. Resolve conflicts automatically by giving preference
 * to the changes done in the candidate configuration.
 *
 * candidate
 *    Candidate configuration to update.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
extern int nb_candidate_update(struct nb_config *candidate);

/*
 * 校验候选配置：同时执行 YANG 语法 / 语义校验与基于北向回调的代码级校验。
 * 注意校验过程可能修改候选配置（例如补充默认节点）。返回 NB_OK 或
 * NB_ERR_VALIDATION。
 */
/*
 * Validate a candidate configuration. Perform both YANG syntactic/semantic
 * validation and code-level validation using the northbound callbacks.
 *
 * WARNING: the candidate can be modified as part of the validation process
 * (e.g. add default nodes).
 *
 * context
 *    Context of the northbound transaction.
 *
 * candidate
 *    Candidate configuration to validate.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    NB_OK on success, NB_ERR_VALIDATION otherwise.
 */
extern int nb_candidate_validate(struct nb_context *context,
				 struct nb_config *candidate, char *errmsg,
				 size_t errmsg_len);

/*
 * 创建配置事务但暂不提交：只对候选做校验，并准备应用这些变更所需的全部资源
 * （两阶段提交的第一阶段，即 prepare）。成功时通过 transaction 返回新建事务，
 * 之后必须调用 nb_candidate_commit_abort() 或 nb_candidate_commit_apply() 收尾。
 * skip_validate 为 true 时跳过提交校验；ignore_zero_change 为 true 时允许无
 * 变更。可能返回 NB_ERR_NO_CHANGES / NB_ERR_LOCKED / NB_ERR_VALIDATION /
 * NB_ERR_RESOURCE 等错误码。
 */
/*
 * Create a new configuration transaction but do not commit it yet. Only
 * validate the candidate and prepare all resources required to apply the
 * configuration changes.
 *
 * context
 *    Context of the northbound transaction.
 *
 * candidate
 *    Candidate configuration to commit.
 *
 * comment
 *    Optional comment describing the commit.
 *
 * transaction
 *    Output parameter providing the created transaction when one is created
 *    successfully. In this case, it must be either aborted using
 *    nb_candidate_commit_abort() or committed using
 *    nb_candidate_commit_apply().
 *
 * skip_validate
 *    TRUE to skip commit validation, FALSE otherwise.
 *
 * ignore_zero_change
 *    TRUE to ignore if zero changes, FALSE otherwise.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    - NB_OK on success.
 *    - NB_ERR_NO_CHANGES when the candidate is identical to the running
 *      configuration.
 *    - NB_ERR_LOCKED when there's already another transaction in progress.
 *    - NB_ERR_VALIDATION when the candidate fails the validation checks.
 *    - NB_ERR_RESOURCE when the system fails to allocate resources to apply
 *      the candidate configuration.
 *    - NB_ERR for other errors.
 */
extern int nb_candidate_commit_prepare(struct nb_context context,
				       struct nb_config *candidate,
				       const char *comment,
				       struct nb_transaction **transaction,
				       bool skip_validate,
				       bool ignore_zero_change, char *errmsg,
				       size_t errmsg_len);


/*
 * 供 mgmtd 外部使用：调用方已自行算好变更（changes），此函数直接为这批变更
 * 走准备流程，而不去与自身的候选配置交互（因为该候选里已包含所有后端客户端
 * 的变更）。成功时经 transaction 返回事务。
 */
/*
 * This function is used externally by mgmtd which has already calculated
 * changes and just needs to run the process for itself without interacting with
 * it's own candidate config which has all the backend client changes in it.
 */
extern int nb_changes_commit_prepare(struct nb_context context, struct nb_config_cbs changes,
				     const char *comment, struct nb_config *candidate_config,
				     struct nb_transaction **transaction, char *errmsg,
				     size_t errmsg_len);

/*
 * 中止一个先前创建的配置事务：释放 prepare 阶段申请的所有资源（即回滚）。
 * 注意 transaction 会被本函数消耗，调用后不可再访问。
 */
/*
 * Abort a previously created configuration transaction, releasing all resources
 * allocated during the preparation phase.
 *
 * transaction
 *    Candidate configuration to abort. It's consumed by this function.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 */
extern void nb_candidate_commit_abort(struct nb_transaction *transaction,
				      char *errmsg, size_t errmsg_len);

/*
 * 提交一个先前创建（且已 prepare）的配置事务：真正应用变更并写入 running。
 * save_transaction 决定是否把该事务记入事务日志；transaction_id 可选返回提交
 * 事务的 ID。transaction 会被本函数消耗。
 */
/*
 * Commit a previously created configuration transaction.
 *
 * transaction
 *    Configuration transaction to commit. It's consumed by this function.
 *
 * save_transaction
 *    Specify whether the transaction should be recorded in the transactions log
 *    or not.
 *
 * transaction_id
 *    Optional output parameter providing the ID of the committed transaction.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 */
extern void nb_candidate_commit_apply(struct nb_transaction *transaction,
				      bool save_transaction,
				      uint32_t *transaction_id, char *errmsg,
				      size_t errmsg_len);

/*
 * 提交候选配置的便捷函数：内部透明地完成“两阶段提交”（prepare 加 apply）。
 * 代价是灵活性降低——跨设备 / 多守护进程事务需要网络管理器自行协调多个被管
 * 实体的 prepare 结果。candidate 无论提交成功与否都会被保留。返回码含义与
 * nb_candidate_commit_prepare 相同。
 */
/*
 * Create a new transaction to commit a candidate configuration. This is a
 * convenience function that performs the two-phase commit protocol
 * transparently to the user. The cost is reduced flexibility, since
 * network-wide and multi-daemon transactions require the network manager to
 * take into account the results of the preparation phase of multiple managed
 * entities.
 *
 * context
 *    Context of the northbound transaction.
 *
 * candidate
 *    Candidate configuration to commit. It's preserved regardless if the commit
 *    operation fails or not.
 *
 * save_transaction
 *    Specify whether the transaction should be recorded in the transactions log
 *    or not.
 *
 * comment
 *    Optional comment describing the commit.
 *
 * transaction_id
 *    Optional output parameter providing the ID of the committed transaction.
 *
 * errmsg
 *    Buffer to store human-readable error message in case of error.
 *
 * errmsg_len
 *    Size of errmsg.
 *
 * Returns:
 *    - NB_OK on success.
 *    - NB_ERR_NO_CHANGES when the candidate is identical to the running
 *      configuration.
 *    - NB_ERR_LOCKED when there's already another transaction in progress.
 *    - NB_ERR_VALIDATION when the candidate fails the validation checks.
 *    - NB_ERR_RESOURCE when the system fails to allocate resources to apply
 *      the candidate configuration.
 *    - NB_ERR for other errors.
 */
extern int nb_candidate_commit(struct nb_context context,
			       struct nb_config *candidate,
			       bool save_transaction, const char *comment,
			       uint32_t *transaction_id, char *errmsg,
			       size_t errmsg_len);

/*
 * 锁定 running 配置。成功返回 0；若已被锁定则返回 -1。
 */
/*
 * Lock the running configuration.
 *
 * client
 *    Northbound client.
 *
 * user
 *    Northbound user (can be NULL).
 *
 * Returns:
 *    0 on success, -1 when the running configuration is already locked.
 */
extern int nb_running_lock(enum nb_client client, const void *user);

/*
 * 解锁 running 配置。成功返回 0；若本就未锁定、或锁被其它客户端 / 用户持有，
 * 返回 -1。
 */
/*
 * Unlock the running configuration.
 *
 * client
 *    Northbound client.
 *
 * user
 *    Northbound user (can be NULL).
 *
 * Returns:
 *    0 on success, -1 when the running configuration is already unlocked or
 *    locked by another client/user.
 */
extern int nb_running_unlock(enum nb_client client, const void *user);

/*
 * 检查给定客户端 / 用户是否持有 running 配置的锁：未锁定、或正是该客户端 /
 * 用户持锁时返回 0，否则返回 -1。
 */
/*
 * Check if the running configuration is locked or not for the given
 * client/user.
 *
 * client
 *    Northbound client.
 *
 * user
 *    Northbound user (can be NULL).
 *
 * Returns:
 *    0 if the running configuration is unlocked or if the client/user owns the
 *    lock, -1 otherwise.
 */
extern int nb_running_lock_check(enum nb_client client, const void *user);

/*
 * 遍历运维数据（已废弃的 legacy 接口）：按 xpath 遍历，对每个数据节点调用
 * cb。flags 控制遍历方式（见 NB_OPER_DATA_ITER_ 标志）；tree 若非 NULL 则
 * 用于返回由遍历构建的数据树。返回 NB_OK 或 NB_ERR。
 */
/*
 * Iterate over operational data -- deprecated.
 *
 * xpath
 *    Data path of the YANG data we want to iterate over.
 *
 * translator
 *    YANG module translator (might be NULL).
 *
 * flags
 *    NB_OPER_DATA_ITER_ flags to control how the iteration is performed.
 *
 * should_batch
 *    Should call finish cb with partial results (i.e., creating batches)
 *
 * cb
 *    Function to call with each data node.
 *
 * arg
 *    Arbitrary argument passed as the fourth parameter in each call to 'cb'.
 *
 * tree
 *    If non-NULL will contain the data tree built from the walk.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
extern enum nb_error nb_oper_iterate_legacy(const char *xpath,
					    struct yang_translator *translator,
					    uint32_t flags, nb_oper_data_cb cb,
					    void *arg, struct lyd_node **tree);

/*
 * 遍历 schema 构建运维状态树（支持分批 / 让出）。cb 对每个非 list、非容器
 * 节点调用；finish 在一批或全部完成时调用，便于分批处理大表。返回一个 walk
 * 句柄（cookie），可用于取消遍历。
 */
/**
 * nb_oper_walk() - walk the schema building operational state.
 * @xpath: data path of the YANG data we want to iterate over.
 * @translator: YANG module translator (might be NULL).
 * @flags: NB_OPER_DATA_ITER_ flags to control how the iteration is performed.
 * @should_batch: should allow yielding and processing portions of the tree.
 * @cb: callback invoked for each non-list, non-container node.
 * @arg: arg to pass to @cb.
 * @finish: function to call when done with portion or all of walk.
 * @finish_arg: arg to pass to @finish.
 * @tree: if non-NULL will be used to copy state data from during the walk.
 *
 * Return: walk - a cookie that can be used to cancel the walk.
 */
extern void *nb_oper_walk(const char *xpath, struct yang_translator *translator,
			  uint32_t flags, bool should_batch, nb_oper_data_cb cb,
			  void *arg, nb_oper_data_finish_cb finish,
			  void *finish_arg);

/*
 * 取消一个正在进行的遍历。只能对进行中的 walk 调用；对已结束的 walk 调用是
 * 非法的。取消后该 walk 的 finish 回调不会再被调用。
 */
/**
 * nb_oper_cancel_walk() - cancel the in progress walk.
 * @walk: value returned from nb_op_iterate_yielding()
 *
 * Should only be called on an in-progress walk. It is invalid to cancel and
 * already finished walk. The walks `finish` callback will not be called.
 */
extern void nb_oper_cancel_walk(void *walk);

/*
 * 取消所有正在进行的遍历。
 */
/**
 * nb_op_cancel_all_walks() - cancel all in progress walks.
 */
extern void nb_oper_cancel_all_walks(void);

/*
 * 返回该 walk 的 finish 回调参数。
 */
/**
 * nb_oper_walk_finish_arg() - return the finish arg for this walk
 */
extern void *nb_oper_walk_finish_arg(void *walk);
/*
 * 返回该 walk 的回调参数。
 */
/**
 * nb_oper_walk_cb_arg() - return the callback arg for this walk
 */
extern void *nb_oper_walk_cb_arg(void *walk);

/*
 * 通用取值函数：把内部数据分别以 uint32 / uint64 形式挂到 parent 数据树上，
 * 返回 nb_error 表示结果。常用于实现节点的 get 回调。
 */
/* Generic getter functions */
extern enum nb_error nb_oper_uint32_get(const struct nb_node *nb_node,
					const void *parent_list_entry, struct lyd_node *parent);

extern enum nb_error nb_oper_uint64_get(const struct nb_node *nb_node,
					const void *parent_list_entry, struct lyd_node *parent);

/*
 * 判断某种回调操作对给定 schema 节点是否有效（例如只有 list 才有 get_keys）。
 * 有效返回 true，否则返回 false。
 */
/*
 * Validate if the northbound callback operation is valid for the given node.
 *
 * operation
 *    Operation we want to check.
 *
 * snode
 *    libyang schema node we want to check.
 *
 * Returns:
 *    true if the operation is valid, false otherwise.
 */
extern bool nb_cb_operation_is_valid(enum nb_cb_operation operation,
				     const struct lysc_node *snode);

/*
 * 发送 YANG 通知（已废弃，建议改用基于树的 nb_notification_tree_send）。仅当
 * 有北向插件注册了 nb_notification_send 钩子时才真正生效，否则为空操作。
 * arguments 为通知参数链表，使用后会被释放。返回 NB_OK 或 NB_ERR。
 */
/*
 * DEPRECATED: This call and infra should no longer be used. Instead,
 * the mgmtd supported tree based call `nb_notification_tree_send` should be
 * used instead
 *
 * Send a YANG notification. This is a no-op unless the 'nb_notification_send'
 * hook was registered by a northbound plugin.
 *
 * xpath
 *    XPath of the YANG notification.
 *
 * arguments
 *    Linked list containing the arguments that should be sent. This list is
 *    deleted after being used.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
extern int nb_notification_send(const char *xpath, struct list *arguments);

/*
 * 以数据树形式发送 YANG 通知：仅当有北向插件注册了 nb_notification_tree_send
 * 钩子时才真正生效，否则为空操作。返回 NB_OK 或 NB_ERR。
 */
/*
 * Send a YANG notification from a backend . This is a no-op unless th
 * 'nb_notification_tree_send' hook was registered by a northbound plugin.
 *
 * xpath
 *    XPath of the YANG notification.
 *
 * tree
 *    The libyang tree for the notification.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
extern int nb_notification_tree_send(const char *xpath,
				     const struct lyd_node *tree);

/*
 * 把一个用户指针关联到某个配置节点。只应在北向 create 回调的 NB_EV_APPLY
 * 阶段调用。dnode 仅用到其 XPath；entry 为用户自定义指针。建立此关联后，后续
 * 可通过 nb_running_get_entry 从配置节点反查内部结构。
 */
/*
 * Associate a user pointer to a configuration node.
 *
 * This should be called by northbound 'create' callbacks in the NB_EV_APPLY
 * phase only.
 *
 * dnode
 *    libyang data node - only its XPath is used.
 *
 * entry
 *    Arbitrary user-specified pointer.
 */
extern void nb_running_set_entry(const struct lyd_node *dnode, void *entry);

/*
 * 移动整棵带用户指针的子树：例如把 xpath A/B/C/D 中的 B 移到 Z 之下（变为
 * Z/B/C/D）时，由于用户指针以其绝对路径为索引，需要把 B 及其下所有用户指针
 * 重新挂到新路径上，本函数负责此事。xpath_from 为原基路径，xpath_to 为新基路径。
 */
/*
 * Move an entire tree of user pointer nodes.
 *
 * Suppose we have xpath A/B/C/D, with user pointers associated to C and D. We
 * need to move B to be under Z, so the new xpath is Z/B/C/D. Because user
 * pointers are indexed with their absolute path, We need to move all user
 * pointers at and below B to their new absolute paths; this function does
 * that.
 *
 * xpath_from
 *    base xpath of tree to move (A/B)
 *
 * xpath_to
 *    base xpath of new location of tree (Z/B)
 */
extern void nb_running_move_tree(const char *xpath_from, const char *xpath_to);

/*
 * 解除某配置节点上关联的用户指针，并返回被解除的指针。只应在北向 destroy
 * 回调的 NB_EV_APPLY 阶段调用。
 */
/*
 * Unset the user pointer associated to a configuration node.
 *
 * This should be called by northbound 'destroy' callbacks in the NB_EV_APPLY
 * phase only.
 *
 * dnode
 *    libyang data node - only its XPath is used.
 *
 * Returns:
 *    The user pointer that was unset.
 */
extern void *nb_running_unset_entry(const struct lyd_node *dnode);

/*
 * 查找某配置节点关联的用户指针。可通过 dnode 或 xpath 指定节点；若未找到，
 * 会沿 running 配置的父节点逐级向上查找，直到找到或到达根节点。
 * abort_if_not_found 为 true 时，找不到就直接中止程序（适用于 apply 阶段的
 * 各配置回调、以及所有阶段的 delete 回调这类“此处必然应有用户指针，否则说明
 * 代码有 bug”的场景）；其它场景应传 false 并由调用方判空。找到返回指针，
 * 否则返回 NULL。
 */
/*
 * Find the user pointer (if any) associated to a configuration node.
 *
 * The XPath associated to the configuration node can be provided directly or
 * indirectly through a libyang data node.
 *
 * If an user point is not found, this function follows the parent nodes in the
 * running configuration until an user pointer is found or until the root node
 * is reached.
 *
 * dnode
 *    libyang data node - only its XPath is used (can be NULL if 'xpath' is
 *    provided).
 *
 * xpath
 *    XPath of the configuration node (can be NULL if 'dnode' is provided).
 *
 * abort_if_not_found
 *    When set to true, abort the program if no user pointer is found.
 *
 *    As a rule of thumb, this parameter should be set to true in the following
 *    scenarios:
 *    - Calling this function from any northbound configuration callback during
 *      the NB_EV_APPLY phase.
 *    - Calling this function from a 'delete' northbound configuration callback
 *      during any phase.
 *
 *    In both the above cases, the given configuration node should contain an
 *    user pointer except when there's a bug in the code, in which case it's
 *    better to abort the program right away and eliminate the need for
 *    unnecessary NULL checks.
 *
 *    In all other cases, this parameter should be set to false and the caller
 *    should check if the function returned NULL or not.
 *
 * Returns:
 *    User pointer if found, NULL otherwise.
 */
extern void *nb_running_get_entry(const struct lyd_node *dnode,
				  const char *xpath, bool abort_if_not_found);

/*
 * 与 nb_running_get_entry 相同，但当未找到用户指针时不再递归向上层父节点查找。
 */
/*
 * Same as 'nb_running_get_entry', but doesn't search within parent nodes
 * recursively if an user point is not found.
 */
extern void *nb_running_get_entry_non_rec(const struct lyd_node *dnode,
					  const char *xpath,
					  bool abort_if_not_found);

/*
 * 返回某个事务阶段（nb_event）对应的可读字符串名。
 */
/*
 * Return a human-readable string representing a northbound event.
 *
 * event
 *    Northbound event.
 *
 * Returns:
 *    String representation of the given northbound event.
 */
extern const char *nb_event_name(enum nb_event event);

/*
 * 返回某个回调操作（nb_cb_operation）对应的可读字符串名。
 */
/*
 * Return a human-readable string representing a northbound callback operation.
 *
 * operation
 *    Northbound callback operation.
 *
 * Returns:
 *    String representation of the given northbound callback operation.
 */
extern const char *nb_cb_operation_name(enum nb_cb_operation operation);

/*
 * 返回某个北向错误码（nb_error）对应的可读字符串名。
 */
/*
 * Return a human-readable string representing a northbound error.
 *
 * error
 *    Northbound error.
 *
 * Returns:
 *    String representation of the given northbound error.
 */
extern const char *nb_err_name(enum nb_error error);

/*
 * 返回某个北向客户端（nb_client）对应的可读字符串名。
 */
/*
 * Return a human-readable string representing a northbound client.
 *
 * client
 *    Northbound client.
 *
 * Returns:
 *    String representation of the given northbound client.
 */
extern const char *nb_client_name(enum nb_client client);

/*
 * 校验所有北向回调。像“缺少必需回调”“优先级非法”这类错误是致命的、无法恢复；
 * 像“多余的回调”则只记录日志，不影响运行。注意：每当启动后又加载某个 YANG
 * 模块时，需要校验的是“所有”回调，而不仅是新加载模块的那些——因为 augment
 * 可能改变被增强模块的性质，从而要求实现额外的回调。
 */
/*
 * Validate all northbound callbacks.
 *
 * Some errors, like missing callbacks or invalid priorities, are fatal and
 * can't be recovered from. Other errors, like unneeded callbacks, are logged
 * but otherwise ignored.
 *
 * Whenever a YANG module is loaded after startup, *all* northbound callbacks
 * need to be validated and not only the callbacks from the newly loaded module.
 * This is because augmentations can change the properties of the augmented
 * module, making mandatory the implementation of additional callbacks.
 */
void nb_validate_callbacks(void);

/*
 * 初始化北向层。只应在守护进程初始化过程中调用一次。modules 为要解析并初始化
 * 的 YANG 模块数组，nmodules 为其个数；db_enabled 为 true 时把事务记入事务
 * 日志；load_library 为 true 时让 libyang 加载 / 实现 ietf-yang-library。
 */
/*
 * Initialize the northbound layer. Should be called only once during the
 * daemon initialization process.
 *
 * modules
 *    Array of YANG modules to parse and initialize.
 *
 * nmodules
 *    Size of the modules array.
 *
 * db_enabled
 *    Set this to record the transactions in the transaction log.
 *
 * load_library
 *    Set this to have libyang to load/implement the ietf-yang-library.
 */
extern void nb_init(struct event_loop *tm, const struct frr_yang_module_info *const modules[],
		    size_t nmodules, bool db_enabled, bool load_library);

/*
 * 优雅地结束北向层，仅在守护进程退出时调用。
 */
/*
 * Finish the northbound layer gracefully. Should be called only when the daemon
 * is exiting.
 */
extern void nb_terminate(void);

/*
 * 运维数据（operational data）子系统的初始化 / 结束，以及判断某个 xpath 是否
 * 属于 ietf-yang-library 的查询。
 */
extern void nb_oper_init(struct event_loop *loop);
extern void nb_oper_terminate(void);
extern bool nb_oper_is_yang_lib_query(const char *xpath);


/*
 * 创建 / 更新运维状态数据：在相对或绝对路径 path 处设置值为 value，返回新建
 * 的 libyang 节点。tree 为 path 相对的子树（为 NULL 时 path 必须为绝对路径）。
 */
/**
 * nb_op_update() - Create new state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to create.
 * @value: The canonical value of the state.
 *
 * Return: The new libyang node.
 */
extern struct lyd_node *nb_op_update(struct lyd_node *tree, const char *path, const char *value);

/*
 * 删除运维状态数据：删除 path 处的状态节点；若 path 为 NULL，则删除整棵 tree
 * 子树。
 */
/**
 * nb_op_update_delete() - Delete state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to delete, or NULL if @tree should just be
 *	  deleted.
 */
extern void nb_op_update_delete(struct lyd_node *tree, const char *path);

/*
 * 与 nb_op_update 相同，但路径用格式化字符串 path_fmt（配合可变参数）给出。
 * path_fmt 为绝对路径或相对 tree 的路径。返回新建的 libyang 节点。
 */
/**
 * nb_op_update_pathf() - Create new state data.
 * @tree: subtree @path_fmt is relative to or NULL in which case @path_fmt must
 *        be absolute.
 * @path_fmt: The path format string of the state node to create.
 * @value: The canonical value of the state.
 * @...: The values to substitute into @path_fmt.
 *
 * Return: The new libyang node.
 */
extern struct lyd_node *nb_op_update_pathf(struct lyd_node *tree, const char *path_fmt,
					   const char *value, ...) PRINTFRR(2, 4);
/*
 * 与 nb_op_update_pathf 相同，但以 va_list 形式接收可变参数（供上层已调用
 * va_start 的封装函数使用）。
 */
extern struct lyd_node *nb_op_update_vpathf(struct lyd_node *tree, const char *path_fmt,
					    const char *value, va_list ap);
/*
 * 删除运维状态数据，路径用格式化字符串 path_fmt 与可变参数给出。
 */
/**
 * nb_op_update_delete_pathf() - Delete state data.
 * @tree: subtree @path_fmt is relative to or NULL in which case @path_fmt must
 *	  be absolute.
 * @path: The path of the state node to delete.
 * @...: The values to substitute into @path_fmt.
 */
extern void nb_op_update_delete_pathf(struct lyd_node *tree, const char *path_fmt, ...)
	PRINTFRR(2, 3);
/*
 * 与 nb_op_update_delete_pathf 相同，但以 va_list 接收可变参数。
 */
extern void nb_op_update_delete_vpathf(struct lyd_node *tree, const char *path_fmt, va_list ap);

/*
 * 创建 / 更新运维状态数据，值为格式化字符串 val_fmt（配合可变参数）的结果。
 * 返回新建的 libyang 节点。
 */
/**
 * nb_op_updatef() - Create new state data.
 * @tree: subtree @path is relative to or NULL in which case @path must be
 *	  absolute.
 * @path: The path of the state node to create.
 * @val_fmt: The value format string to set the canonical value of the state.
 * @...: The values to substitute into @val_fmt.
 *
 * Return: The new libyang node.
 */
extern struct lyd_node *nb_op_updatef(struct lyd_node *tree, const char *path, const char *val_fmt,
				      ...) PRINTFRR(3, 4);

/*
 * 与 nb_op_updatef 相同，但以 va_list 接收可变参数。
 */
extern struct lyd_node *nb_op_vupdatef(struct lyd_node *tree, const char *path, const char *val_fmt,
				       va_list ap);
/*
 * 通告某运维状态路径上的值发生了变化（新增或更新）。
 */
/**
 * nb_notif_add() - Notice that the value at `path` has changed.
 * @path: Absolute path in the state tree that has changed (either added or
 *	  updated).
 */
void nb_notif_add(const char *path);

/*
 * 与 nb_notif_add 相同，但路径为格式化字符串（配合可变参数）。
 */
/**
 * nb_notif_addf() - Notice that the value at `path` has changed.
 * @path: Absolute path format string in the state tree that has changed
 *	  (either added or updated).
 * @...: The values to substitute into `path`.
 */
void nb_notif_addf(const char *path, ...) PRINTFRR(1, 2);

/*
 * 通告某运维状态路径上的值已被删除。
 */
/**
 * nb_notif_delete() - Notice that the value at `path` has been deleted.
 * @path: Absolute path in the state tree that has been deleted.
 */
void nb_notif_delete(const char *path);

/*
 * 与 nb_notif_delete 相同，但路径为格式化字符串（配合可变参数）。
 */
/**
 * nb_notif_deletef() - Notice that the value at `path` has been deleted.
 * @path: Absolute path format string in the state tree that has been deleted.
 * @...: The values to substitute into `path`.
 */
void nb_notif_deletef(const char *path, ...) PRINTFRR(1, 2);

/*
 * 新增或替换通知过滤器。selectors 为过滤器（selector）xpath 字符串的动态
 * 数组（darr），当 replace 为 true 时可为 NULL；本函数接管该数组及其内部
 * 字符串的所有权。replace 为 true 时替换已有过滤器，否则追加。
 */
/**
 * nb_notif_set_filters() - add or replace notification filters
 * @selectors: darr array of selector (filter) xpath strings, can be NULL if
 *	       @replace is true. nb_notif_set_filters takes ownership of this
 *	       array and the contained darr strings.
 * @replace: true to replace existing set otherwise append.
 */
extern void nb_notif_set_filters(const char **selectors, bool replace);


/*
 * 请求把运维状态转储发送给 mgmtd：selectors 为要执行 get 的 xpath 动态数组，
 * 结果作为通知数据返回；refer_id 会填在返回通知数据的 refer_id 字段中。
 */
/**
 * nb_notif_get_state() - request state dump be sent to mgmtd
 * @selectors: darr array of xpath strings to do get operations on returning
 *		the state as notification data.
 * @refer_id: send in the refer_id field of the returned notification data.
 */
extern void nb_notif_get_state(const char **selectors, uint64_t refer_id);

/*
 * 允许多线程使用 nb_notif：若将从多个线程调用 nb_notif_XXX 系列函数，则需要
 * 加锁。请在使用该 API 之前调用本函数以启用多线程支持。
 */
/**
 * nb_notif_enable_multi_thread() - enable use of multiple threads with nb_notif
 *
 * If the nb_notif_XXX calls will be made from multiple threads then locking is
 * required. Call this function to enable that functionality, prior to using the
 * nb_notif_XXX API.
 */
extern void nb_notif_enable_multi_thread(void);

/*
 * 通知（notification）子系统的初始化 / 结束。
 */
extern void nb_notif_init(struct event_loop *loop);
extern void nb_notif_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_NORTHBOUND_H_ */

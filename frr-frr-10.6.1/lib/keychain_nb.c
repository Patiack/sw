// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * February 22 2024, Christian Hopps <chopps@labn.net>
 *
 * Copyright (C) 2024 LabN Consulting, L.L.C.
 */
/*
 * 文件导读：keychain_nb.c —— frr-keychain 的 YANG 北向（northbound）回调
 *
 * 1. 本文件在 FRR 里干什么：
 *    实现 ietf-key-chain 模块的配置回调与状态查询回调，把 YANG 数据树
 *    （配置与状态）与 keychain.c 里的 struct keychain / struct key 关联
 *    起来。命令行（keychain_cli.c）与 NETCONF / RESTCONF 等北向客户端的
 *    修改最终都经这些回调落到内存对象上。
 *
 * 2. 回调类型：
 *    - create / destroy：配置节点（容器、叶子）的新增与删除；
 *    - modify：已有叶子的取值修改；
 *    - get_next / get_keys / lookup_entry：列表（key chain、key）的遍历
 *      与按 key 定位，供框架枚举与按索引查找；
 *    - get_elem：状态叶子（last-modified-timestamp、lifetime-active 等）
 *      的取值回调，从内存对象实时计算。
 *
 * 3. 关键辅助函数：
 *    - keychain_touch：打时间戳（更新 last_touch，用于 last-modified 状态）；
 *    - __dnode_get_key2 / __dnode_get_key3：由数据节点定位到对应 key
 *      （key3 用于多一层路径的 always 节点）；
 *    - __lifetime_create / __lifetime_start_date_time_modify /
 *      __lifetime_no_end_time_create / __lifetime_duration_modify /
 *      __lifetime_end_date_time_modify：时间窗口的公共处理，通过
 *      send / accept 两个布尔参数区分作用于发送还是接收方向。
 *
 * 4. 阅读顺序建议：
 *    先看 keychain_touch 与 key chain 级回调，再看 key 级回调，然后看
 *    lifetime 公共函数及其大量薄封装，接着是 crypto-algorithm 与
 *    key-string 回调，最后是状态叶子回调与模块信息 ietf_key_chain_info。
 *
 * 5. 易踩的坑：
 *    - 几乎所有回调都要先判断 args->event 是否为 NB_EV_APPLY，只有
 *      VALIDATE / APPLY 阶段才真正落地；
 *    - 相对路径 ../ 的层数取决于节点深度，always 节点比普通 lifetime
 *      节点多一层，故有 __dnode_get_key2 与 __dnode_get_key3 之分。
 */
#include <zebra.h>
#include "lib_errors.h"
#include "northbound.h"
#include "keychain.h"

/*
 * 更新密钥链的 last_touch 为当前时间。
 * 任何配置修改后都会调用，供 last-modified-timestamp 状态叶子读取。
 */
static void keychain_touch(struct keychain *keychain)
{
	keychain->last_touch = time(NULL);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain
 */
/*
 * 回调：创建 key-chain 列表项（按名称）。
 * 仅在 NB_EV_APPLY 阶段动作：按 name 获取（不存在则新建）密钥链并打时间戳。
 * 返回值：NB_OK。
 */
static int key_chains_key_chain_create(struct nb_cb_create_args *args)
{
	const char *name;
	struct keychain *keychain;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "name");
	keychain = keychain_get(name);
	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：删除 key-chain 列表项。
 * 按 name 查出密钥链并调用 keychain_delete 释放。
 * 返回值：NB_OK。
 */
static int key_chains_key_chain_destroy(struct nb_cb_destroy_args *args)
{
	const char *name;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "name");
	keychain_delete(keychain_lookup(name));
	return NB_OK;
}

/*
 * 回调：遍历 key-chain 列表的下一项。
 * 首次调用（prev 为空）返回链表头，否则返回当前节点的 next。
 * 返回值：下一个 listnode，末尾返回 NULL。
 */
static const void *key_chains_key_chain_get_next(struct nb_cb_get_next_args *args)
{
	const struct listnode *prev = args->list_entry;

	return prev ? prev->next : keychain_list->head;
}

/*
 * 回调：取出 key-chain 列表项的 key（仅 name 一个）。
 * 返回值：NB_OK；keys->num 置 1，key[0] 填密钥链名称。
 */
static int key_chains_key_chain_get_keys(struct nb_cb_get_keys_args *args)
{
	const struct listnode *node = args->list_entry;
	const struct keychain *keychain = node->data;

	args->keys->num = 1;
	strlcpy(args->keys->key[0], keychain->name, sizeof(args->keys->key[0]));
	return NB_OK;
}

/*
 * 回调：按 key（name）在全局链表定位 key-chain 对应的 listnode。
 * 返回值：命中的 listnode；未找到返回 NULL。
 */
static const void *key_chains_key_chain_lookup_entry(struct nb_cb_lookup_entry_args *args)
{
	const char *name = args->keys->key[0];
	struct keychain *keychain;
	struct listnode *node;

	for (ALL_LIST_ELEMENTS_RO(keychain_list, node, keychain)) {
		if (strcmp(keychain->name, name) == 0)
			return node;
	}
	return NULL;
}


/*
 * 空操作的销毁回调。
 * 某些 lifetime 子节点的清理要么由兄弟节点触发、要么随父容器一起销毁，
 * 因此这里无需额外动作，直接返回 NB_OK。
 */
static int __destroy_nop(struct nb_cb_destroy_args *args)
{
	/* modified by sibling or cleaned up by container destroy */
	return NB_OK;
}

/*
 * 由数据节点定位到对应的 key（相对路径深两层调用者用此版本）。
 * 参数：dnode 当前节点；touch 为 true 时同时更新所属密钥链的时间戳。
 * 路径：../../../name 取链名，../../key-id 取编号。
 * 返回值：命中的 struct key（未找到返回 NULL）。
 */
static struct key *__dnode_get_key2(const struct lyd_node *dnode, bool touch)
{
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;

	name = yang_dnode_get_string(dnode, "../../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(dnode, "../../key-id");
	key = key_lookup(keychain, index);
	if (touch)
		keychain_touch(keychain);
	return key;
}

/*
 * 由数据节点定位到对应的 key（相对路径深三层的 always 节点用此版本）。
 * 参数：dnode 当前节点；touch 为 true 时同时更新所属密钥链的时间戳。
 * 路径：../../../../name 取链名，../../../key-id 取编号。
 * 返回值：命中的 struct key（未找到返回 NULL）。
 */
static struct key *__dnode_get_key3(const struct lyd_node *dnode, bool touch)
{
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;

	name = yang_dnode_get_string(dnode, "../../../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(dnode, "../../../key-id");
	key = key_lookup(keychain, index);
	if (touch)
		keychain_touch(keychain);
	return key;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/description
 */
/*
 * 回调：修改 key-chain 的 description。
 * 释放旧描述字符串后用 YANG 节点值重建（MTYPE_KEYCHAIN_DESC），并打时间戳。
 * 返回值：NB_OK。
 */
static int key_chains_key_chain_description_modify(struct nb_cb_modify_args *args)
{
	struct keychain *keychain;
	const char *name;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../name");
	keychain = keychain_lookup(name);
	XFREE(MTYPE_KEYCHAIN_DESC, keychain->desc);
	keychain->desc = XSTRDUP(MTYPE_KEYCHAIN_DESC,
				 yang_dnode_get_string(args->dnode, NULL));

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：删除 key-chain 的 description。
 * 释放描述字符串并打时间戳。返回值：NB_OK。
 */
static int key_chains_key_chain_description_destroy(struct nb_cb_destroy_args *args)
{
	struct keychain *keychain;
	const char *name;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../name");
	keychain = keychain_lookup(name);
	XFREE(MTYPE_KEYCHAIN_DESC, keychain->desc);

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/last-modified-timestamp
 */
/*
 * 状态回调：读取 key-chain 的 last-modified-timestamp。
 * 返回 date-and-time 类型的状态数据，取自密钥链的 last_touch 字段。
 */
static struct yang_data *key_chains_key_chain_last_modified_timestamp_get_elem(struct nb_cb_get_elem_args *args)
{
	const struct listnode *kcnode = args->list_entry;
	const struct keychain *keychain = kcnode->data;

	return yang_data_new_date_and_time(args->xpath, keychain->last_touch,
					   false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key
 */
/*
 * 回调：创建 key 列表项（按 key-id）。
 * VALIDATE 阶段校验 key-id 不超过 32 位（超限则报错并拒绝）；
 * APPLY 阶段按 name 找到密钥链，用 key_get 取（不存在则建）对应编号的 key。
 * 返回值：NB_OK 或校验失败时 NB_EV_VALIDATE。
 */
static int key_chains_key_chain_key_create(struct nb_cb_create_args *args)
{
	struct keychain *keychain;
	struct key *key;
	const char *name;
	uint64_t keyid;

	if (args->event != NB_EV_VALIDATE && args->event != NB_EV_APPLY)
		return NB_OK;

	keyid = yang_dnode_get_uint64(args->dnode, "key-id");
	if (args->event == NB_EV_VALIDATE) {
		if (keyid > UINT32_MAX) {
			/* Warn most protocols can't use this value */
			flog_err(EC_LIB_NB_CB_CONFIG_VALIDATE,
				 "Protocols do not accept > 32-bit key-id values");
			return NB_EV_VALIDATE;
		}
		return NB_OK;
	}
	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../name");
	keychain = keychain_lookup(name);
	assert(keyid <= UINT32_MAX);
	key = key_get(keychain, (uint32_t)keyid);
	assert(key);

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：删除 key 列表项。
 * key-id 超过 32 位返回 NB_ERR_NOT_FOUND；否则按 name 与编号定位 key 并删除。
 * 返回值：NB_OK 或 NB_ERR_NOT_FOUND。
 */
static int key_chains_key_chain_key_destroy(struct nb_cb_destroy_args *args)
{
	struct keychain *keychain;
	struct key *key;
	const char *name;
	uint64_t keyid;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	keyid = yang_dnode_get_uint64(args->dnode, "key-id");
	if (keyid > UINT32_MAX)
		return NB_ERR_NOT_FOUND;
	name = yang_dnode_get_string(args->dnode, "../name");
	keychain = keychain_lookup(name);
	key = key_lookup(keychain, (uint32_t)keyid);
	key_delete(keychain, key);

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：遍历某密钥链下 key 列表的下一项。
 * 由父列表项（parent_list_entry）取出密钥链对象，首次返回其 key 链表头。
 * 返回值：下一个 listnode，末尾返回 NULL。
 */
static const void *key_chains_key_chain_key_get_next(struct nb_cb_get_next_args *args)
{
	const struct listnode *kcnode = args->parent_list_entry;
	const struct keychain *keychain = kcnode->data;
	const struct listnode *prev = args->list_entry;

	return prev ? prev->next : keychain->key->head;
}

/*
 * 回调：取出 key 列表项的 key（仅 key-id 一个，十进制字符串）。
 * 返回值：NB_OK；keys->num 置 1，key[0] 填编号。
 */
static int key_chains_key_chain_key_get_keys(struct nb_cb_get_keys_args *args)
{
	const struct listnode *node = args->list_entry;
	const struct key *key = node->data;

	args->keys->num = 1;
	snprintf(args->keys->key[0], sizeof(args->keys->key[0]), "%" PRIu32,
		 key->index);

	return NB_OK;
}

/*
 * 回调：在某密钥链内按 key（key-id）定位 key 对应的 listnode。
 * 把字符串 key-id 转成整数后与各 key 的 index 比较。
 * 返回值：命中的 listnode；未找到返回 NULL。
 */
static const void *key_chains_key_chain_key_lookup_entry(struct nb_cb_lookup_entry_args *args)
{
	const struct listnode *kcnode = args->parent_list_entry;
	const struct keychain *keychain = kcnode->data;
	struct listnode *node;
	struct key *key;
	uint32_t index;

	index = strtoul(args->keys->key[0], NULL, 0);
	for (ALL_LIST_ELEMENTS_RO(keychain->key, node, key))
		if (key->index == index)
			return node;
	return NULL;
}

/*
 * lifetime 公共处理：初始化时间窗口为默认“常生效”。
 * 参数：send / accept 指明作用于哪个方向；always 为 true 时节点位于
 *       .../always（路径更深一层，用 __dnode_get_key3 定位 key）。
 * 行为：把对应方向的 start 置 0（无起始限制）、end 置 -1（永不过期）、
 *       duration 置 0。
 * 返回值：NB_OK。
 */
static int __lifetime_create(struct nb_cb_create_args *args, bool send,
			     bool accept, bool always)
{
	struct key *key;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	if (always)
		key = __dnode_get_key3(args->dnode, true);
	else
		key = __dnode_get_key2(args->dnode, true);
	if (send) {
		key->send.start = 0;
		key->send.end = -1;
		key->send.duration = 0;
	}
	if (accept) {
		key->accept.start = 0;
		key->accept.end = -1;
		key->accept.duration = 0;
	}
	return NB_OK;
}

/*
 * lifetime 公共处理：设置起始时间（start-date-time）。
 * 参数：send / accept 指明作用方向；时间从节点解析后写入 start。
 * 返回值：NB_OK。
 */
static int __lifetime_start_date_time_modify(struct nb_cb_modify_args *args,
					     bool send, bool accept)
{
	struct key *key;
	time_t time;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	key = __dnode_get_key3(args->dnode, true);
	time = yang_dnode_get_date_and_time(args->dnode, NULL);

	if (send)
		key->send.start = time;
	if (accept)
		key->accept.start = time;

	return NB_OK;
}

/*
 * lifetime 公共处理：设置永不过期（no-end-time）。
 * 参数：send / accept 指明作用方向；把对应方向的 end 置 -1。
 * 返回值：NB_OK。
 */
static int __lifetime_no_end_time_create(struct nb_cb_create_args *args,
					 bool send, bool accept)
{
	struct key *key;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	key = __dnode_get_key3(args->dnode, true);
	if (send)
		key->send.end = -1;
	if (accept)
		key->accept.end = -1;
	return NB_OK;
}

/*
 * lifetime 公共处理：设置持续时长（duration）。
 * 参数：send / accept 指明作用方向。
 * 行为：end = start-date-time + duration。
 * 返回值：NB_OK。
 */
static int __lifetime_duration_modify(struct nb_cb_modify_args *args, bool send,
				      bool accept)
{
	struct key *key;
	uint32_t duration;
	time_t time;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	key = __dnode_get_key3(args->dnode, true);
	time = yang_dnode_get_date_and_time(args->dnode, "../start-date-time");
	duration = yang_dnode_get_uint32(args->dnode, NULL);

	if (send)
		key->send.end = time + duration;
	if (accept)
		key->accept.end = time + duration;
	return NB_OK;
}

/*
 * lifetime 公共处理：设置结束时间（end-date-time）。
 * 参数：send / accept 指明作用方向；时间从节点解析后写入 end。
 * 返回值：NB_OK。
 */
static int __lifetime_end_date_time_modify(struct nb_cb_modify_args *args,
					   bool send, bool accept)
{
	struct key *key;
	time_t time;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	key = __dnode_get_key3(args->dnode, true);
	time = yang_dnode_get_date_and_time(args->dnode, NULL);

	if (send)
		key->send.end = time;
	if (accept)
		key->accept.end = time;
	return NB_OK;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime
 */
/*
 * 创建 send-accept-lifetime 容器（同时作用于发送与接收窗口）。
 * 置默认时间范围（无起始限制、永不过期）。转交 __lifetime_create。
 */
static int key_chains_key_chain_key_lifetime_send_accept_lifetime_create(
	struct nb_cb_create_args *args)
{

	return __lifetime_create(args, true, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/always
 */
/*
 * 创建 send-accept-lifetime/always：发送与接收窗口同时置为常生效默认值。
 * always 节点路径更深，内部用 __dnode_get_key3 定位 key。
 */
static int key_chains_key_chain_key_lifetime_send_accept_lifetime_always_create(
	struct nb_cb_create_args *args)
{
	return __lifetime_create(args, true, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/start-date-time
 */
/*
 * 修改 send-accept-lifetime/start-date-time：写入发送与接收窗口的起始时间。
 * 转交 __lifetime_start_date_time_modify。
 */
static int
key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_modify(
	struct nb_cb_modify_args *args)
{
	return __lifetime_start_date_time_modify(args, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/no-end-time
 */
/*
 * 创建 send-accept-lifetime/no-end-time：发送与接收窗口设为永不过期。
 * 转交 __lifetime_no_end_time_create。
 */
static int
key_chains_key_chain_key_lifetime_send_accept_lifetime_no_end_time_create(
	struct nb_cb_create_args *args)
{
	return __lifetime_no_end_time_create(args, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/duration
 */
/*
 * 修改 send-accept-lifetime/duration：发送与接收窗口的结束时间
 * = 起始时间 + 持续秒数。转交 __lifetime_duration_modify。
 */
static int key_chains_key_chain_key_lifetime_send_accept_lifetime_duration_modify(
	struct nb_cb_modify_args *args)
{
	return __lifetime_duration_modify(args, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/end-date-time
 */
/*
 * 修改 send-accept-lifetime/end-date-time：写入发送与接收窗口的结束时间。
 * 转交 __lifetime_end_date_time_modify。
 */
static int
key_chains_key_chain_key_lifetime_send_accept_lifetime_end_date_time_modify(
	struct nb_cb_modify_args *args)
{
	return __lifetime_end_date_time_modify(args, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime
 */
/*
 * 创建 send-lifetime 容器（仅作用于发送窗口）。
 * 置默认时间范围（无起始限制、永不过期）。转交 __lifetime_create。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_create(
	struct nb_cb_create_args *args)
{

	return __lifetime_create(args, true, false, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/always
 */
/*
 * 创建 send-lifetime/always：发送窗口置为常生效默认值。
 * always 节点路径更深，内部用 __dnode_get_key3 定位 key。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_always_create(
	struct nb_cb_create_args *args)
{
	return __lifetime_create(args, true, false, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/start-date-time
 */
/*
 * 修改 send-lifetime/start-date-time：写入发送窗口的起始时间。
 * 转交 __lifetime_start_date_time_modify。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_start_date_time_modify(args, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/no-end-time
 */
/*
 * 创建 send-lifetime/no-end-time：发送窗口设为永不过期。
 * 转交 __lifetime_no_end_time_create。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_no_end_time_create(struct nb_cb_create_args *args)
{
	return __lifetime_no_end_time_create(args, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/duration
 */
/*
 * 修改 send-lifetime/duration：发送窗口结束时间 = 起始时间 + 持续秒数。
 * 转交 __lifetime_duration_modify。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_duration_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_duration_modify(args, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/end-date-time
 */
/*
 * 修改 send-lifetime/end-date-time：写入发送窗口的结束时间。
 * 转交 __lifetime_end_date_time_modify。
 */
static int key_chains_key_chain_key_lifetime_send_lifetime_end_date_time_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_end_date_time_modify(args, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime
 */
/*
 * 创建 accept-lifetime 容器（仅作用于接收窗口）。
 * 置默认时间范围（无起始限制、永不过期）。转交 __lifetime_create。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_create(
	struct nb_cb_create_args *args)
{

	return __lifetime_create(args, false, true, false);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/always
 */
/*
 * 创建 accept-lifetime/always：接收窗口置为常生效默认值。
 * always 节点路径更深，内部用 __dnode_get_key3 定位 key。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_always_create(struct nb_cb_create_args *args)
{
	return __lifetime_create(args, false, true, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/start-date-time
 */
/*
 * 修改 accept-lifetime/start-date-time：写入接收窗口的起始时间。
 * 转交 __lifetime_start_date_time_modify。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_start_date_time_modify(args, false, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/no-end-time
 */
/*
 * 创建 accept-lifetime/no-end-time：接收窗口设为永不过期。
 * 转交 __lifetime_no_end_time_create。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_no_end_time_create(struct nb_cb_create_args *args)
{
	return __lifetime_no_end_time_create(args, false, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/duration
 */
/*
 * 修改 accept-lifetime/duration：接收窗口结束时间 = 起始时间 + 持续秒数。
 * 转交 __lifetime_duration_modify。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_duration_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_duration_modify(args, false, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/end-date-time
 */
/*
 * 修改 accept-lifetime/end-date-time：写入接收窗口的结束时间。
 * 转交 __lifetime_end_date_time_modify。
 */
static int key_chains_key_chain_key_lifetime_accept_lifetime_end_date_time_modify(struct nb_cb_modify_args *args)
{
	return __lifetime_end_date_time_modify(args, false, true);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/crypto-algorithm
 */
/*
 * 回调：修改 key 的 crypto-algorithm。
 * VALIDATE 阶段把算法名（先剥去 ietf-key-chain: 前缀）映射为枚举值，
 * 未识别的算法报错返回 NB_ERR_VALIDATION；未启用 openssl 时若映射为
 * KEYCHAIN_ALGO_NULL 也报错。
 * APPLY 阶段把枚举值写入 key->hash_algo 并打时间戳。
 * 返回值：NB_OK 或 NB_ERR_VALIDATION。
 */
static int key_chains_key_chain_key_crypto_algorithm_modify(struct nb_cb_modify_args *args)
{
	static const char prefix[] = "ietf-key-chain:";
	static const int prefix_len = sizeof(prefix) - 1;
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;
	uint8_t hash_algo;

	if (args->event != NB_EV_VALIDATE && args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, NULL);
	if (!strncmp(name, prefix, prefix_len))
		name += prefix_len;
	hash_algo = keychain_get_algo_id_by_name(name);

	if (args->event == NB_EV_VALIDATE) {
		if (!hash_algo) {
			zlog_err("\"%s\" hash algo not supported", name);
			return NB_ERR_VALIDATION;
		}
#ifndef CRYPTO_OPENSSL
		if (hash_algo == KEYCHAIN_ALGO_NULL) {
			zlog_err("\"%s\" algo not supported, compile with --with-crypto=openssl",
				 name);
			return NB_ERR_VALIDATION;
		}
#endif /* CRYPTO_OPENSSL */
		return NB_OK;
	}

	assert(args->event == NB_EV_APPLY);
	name = yang_dnode_get_string(args->dnode, "../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(args->dnode, "../key-id");
	key = key_lookup(keychain, index);
	key->hash_algo = hash_algo;

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：删除 key 的 crypto-algorithm。
 * 把 key->hash_algo 复位为 KEYCHAIN_ALGO_NULL 并打时间戳。
 * 返回值：NB_OK。
 */
static int key_chains_key_chain_key_crypto_algorithm_destroy(
	struct nb_cb_destroy_args *args)
{
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(args->dnode, "../key-id");
	key = key_lookup(keychain, index);
	key->hash_algo = KEYCHAIN_ALGO_NULL;
	keychain_touch(keychain);

	return NB_OK;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/key-string/keystring
 */
/*
 * 回调：修改 key 的 key-string/keystring。
 * 释放旧密钥串后用 YANG 节点值重建（MTYPE_KEY），并打时间戳。
 * 返回值：NB_OK。
 */
static int key_chains_key_chain_key_key_string_keystring_modify(struct nb_cb_modify_args *args)
{
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(args->dnode, "../../key-id");
	key = key_lookup(keychain, index);
	assert(key);


	if (key->string)
		XFREE(MTYPE_KEY, key->string);
	key->string = XSTRDUP(MTYPE_KEY,
			      yang_dnode_get_string(args->dnode, NULL));

	keychain_touch(keychain);
	return NB_OK;
}

/*
 * 回调：删除 key 的 key-string/keystring。
 * 释放密钥串并打时间戳。返回值：NB_OK。
 */
static int key_chains_key_chain_key_key_string_keystring_destroy(struct nb_cb_destroy_args *args)
{
	struct keychain *keychain;
	const char *name;
	struct key *key;
	uint32_t index;

	if (args->event != NB_EV_APPLY)
		return NB_OK;

	name = yang_dnode_get_string(args->dnode, "../../../name");
	keychain = keychain_lookup(name);
	index = (uint32_t)yang_dnode_get_uint64(args->dnode, "../../key-id");
	key = key_lookup(keychain, index);
	assert(key);

	XFREE(MTYPE_KEY, key->string);
	keychain_touch(keychain);

	return NB_OK;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/send-lifetime-active
 */
/*
 * 状态回调：读取 key 的 send-lifetime-active。
 * 按发送窗口是否覆盖当前时间判定：send.start 为 0 表示无起始限制即生效；
 * 否则需 start <= now 且（end >= now 或 end == -1）。
 * 返回布尔型状态数据。
 */
static struct yang_data *key_chains_key_chain_key_send_lifetime_active_get_elem(struct nb_cb_get_elem_args *args)
{
	const struct listnode *node = args->list_entry;
	const struct key *key = node->data;
	time_t now = time(NULL);
	bool active = false;

	if (key->send.start == 0)
		active = true;
	else if (key->send.start <= now)
		if (key->send.end >= now || key->send.end == -1)
			active = true;

	return yang_data_new_bool(args->xpath, active);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/accept-lifetime-active
 */
/*
 * 状态回调：读取 key 的 accept-lifetime-active。
 * 判定规则同 send 版本，只是作用于接收窗口（accept.start / accept.end）。
 * 返回布尔型状态数据。
 */
static struct yang_data *key_chains_key_chain_key_accept_lifetime_active_get_elem(struct nb_cb_get_elem_args *args)
{
	const struct listnode *node = args->list_entry;
	const struct key *key = node->data;
	time_t now = time(NULL);
	bool active = false;

	if (key->accept.start == 0)
		active = true;
	else if (key->accept.start <= now)
		if (key->accept.end >= now || key->accept.end == -1)
			active = true;

	return yang_data_new_bool(args->xpath, active);
}

/*
 * 本模块声明的 YANG feature 列表。
 * independent-send-accept-lifetime 表示支持发送 / 接收窗口各自独立配置。
 * 以 NULL 结尾。
 */
static const char * const keychain_features[] = {
	"independent-send-accept-lifetime",
	NULL,
};

/* clang-format off */
/*
 * ietf-key-chain 模块信息：把每个 YANG XPath 与对应的回调绑定。
 * 列表项（key-chain、key）绑定 create / destroy / get_next / get_keys /
 * lookup_entry 及 CLI 回显回调；lifetime 各叶子绑定 modify / create 与
 * 空销毁回调 __destroy_nop；状态叶子绑定 get_elem。
 * 末尾以 xpath = NULL 的哨兵节点结束。
 */
const struct frr_yang_module_info ietf_key_chain_info = {
	.name = "ietf-key-chain",
	.features = (const char **)keychain_features,
	.nodes = {
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain",
			.cbs = {
				.create = key_chains_key_chain_create,
				.destroy = key_chains_key_chain_destroy,
				.get_next = key_chains_key_chain_get_next,
				.get_keys = key_chains_key_chain_get_keys,
				.lookup_entry = key_chains_key_chain_lookup_entry,
				.cli_show = key_chains_key_chain_cli_write,
				.cli_show_end = key_chains_key_chain_cli_write_end,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/description",
			.cbs = {
				.modify = key_chains_key_chain_description_modify,
				.destroy = key_chains_key_chain_description_destroy,
				.cli_show = key_chains_key_chain_description_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/last-modified-timestamp",
			.cbs = {
				.get_elem = key_chains_key_chain_last_modified_timestamp_get_elem,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key",
			.cbs = {
				.create = key_chains_key_chain_key_create,
				.destroy = key_chains_key_chain_key_destroy,
				.get_next = key_chains_key_chain_key_get_next,
				.get_keys = key_chains_key_chain_key_get_keys,
				.lookup_entry = key_chains_key_chain_key_lookup_entry,
				.cli_show = key_chains_key_chain_key_cli_write,
				.cli_show_end = key_chains_key_chain_key_cli_write_end,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_accept_lifetime_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/always",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_accept_lifetime_always_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/start-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_modify,
				.destroy = __destroy_nop,
				.cli_show = key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/no-end-time",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_accept_lifetime_no_end_time_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/duration",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_accept_lifetime_duration_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/end-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_accept_lifetime_end_date_time_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_lifetime_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/always",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_lifetime_always_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/start-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_modify,
				.destroy = __destroy_nop,
				.cli_show = key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/no-end-time",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_send_lifetime_no_end_time_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/duration",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_lifetime_duration_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/end-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_send_lifetime_end_date_time_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_accept_lifetime_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/always",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_accept_lifetime_always_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/start-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_modify,
				.destroy = __destroy_nop,
				.cli_show = key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/no-end-time",
			.cbs = {
				.create = key_chains_key_chain_key_lifetime_accept_lifetime_no_end_time_create,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/duration",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_accept_lifetime_duration_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/end-date-time",
			.cbs = {
				.modify = key_chains_key_chain_key_lifetime_accept_lifetime_end_date_time_modify,
				.destroy = __destroy_nop,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/crypto-algorithm",
			.cbs = {
				.modify = key_chains_key_chain_key_crypto_algorithm_modify,
				.destroy = key_chains_key_chain_key_crypto_algorithm_destroy,
				.cli_show = key_chains_key_chain_key_crypto_algorithm_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/key-string/keystring",
			.cbs = {
				.modify = key_chains_key_chain_key_key_string_keystring_modify,
				.destroy = key_chains_key_chain_key_key_string_keystring_destroy,
				.cli_show = key_chains_key_chain_key_key_string_keystring_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/send-lifetime-active",
			.cbs = {
				.get_elem = key_chains_key_chain_key_send_lifetime_active_get_elem,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/accept-lifetime-active",
			.cbs = {
				.get_elem = key_chains_key_chain_key_accept_lifetime_active_get_elem,
			}
		},
		{
			.xpath = NULL,
		},
	},
};

// SPDX-License-Identifier: GPL-2.0-or-later
/* key-chain for authentication.
 * Copyright (C) 2000 Kunihiro Ishiguro
 */

/*
 * 文件导读：keychain.c —— 密钥链模块的主体实现
 *
 * 1. 本文件在 FRR 里干什么：
 *    实现密钥链（key chain）的创建 / 查找 / 删除，以及“发送方向取哪个
 *    key、接收方向用哪个 key 校验”的核心逻辑，并维护一张全局密钥链
 *    链表 keychain_list。各路由协议（OSPF、RIP、BGP、IS-IS）的认证
 *    都通过这里的 API 取用密钥及算法参数。
 *
 * 2. 核心数据结构：
 *    struct keychain（见 keychain.h）：一条密钥链，内含 struct key 链表；
 *    struct key：单个密钥，含密钥串、算法、send / accept 时间窗口；
 *    algo_info[]：算法元信息表，下标即 enum keychain_hash_algo；
 *    keychain_list：全局密钥链链表（本文件定义）。
 *
 * 3. 与其它模块的关系：
 *    - keychain.h 声明数据结构与 API；
 *    - keychain_cli.c 负责 VTY 命令解析，最终调用 keychain_get / key_get
 *      等函数落库；
 *    - keychain_nb.c 是 YANG 北向回调，同样调用这些底层函数；
 *    - 协议侧主要用 key_lookup_for_send（发）与 key_match_for_accept /
 *      key_lookup_for_accept（收）。
 *
 * 4. 阅读顺序建议：
 *    先看内存 / 生命周期管理（keychain_new / key_new / 各 delete），
 *    再看查找与获取（keychain_lookup / keychain_get / key_get），
 *    然后看时间窗口判定（key_lookup_for_send / key_match_for_accept），
 *    最后看算法表与初始化 / 清理函数。
 *
 * 5. 易踩的坑：
 *    - key_get 会对不存在的 key 自动新建，注意别在只读路径误用；
 *    - 时间窗口判定里 end == -1 表示永不过期，且 accept 判定带“编号下限”
 *      语义（index 之后的第一个可用 key），与 send 不同。
 */
#include "config.h"
#include <zebra.h>

#include "keychain.h"
#include "linklist.h"
#include "memory.h"

/* 为密钥、密钥链、密钥链描述字符串分配独立的内存类型，便于统计与排错 */
DEFINE_MTYPE(LIB, KEY, "Key");
DEFINE_MTYPE(LIB, KEYCHAIN, "Key chain");
DEFINE_MTYPE(LIB, KEYCHAIN_DESC, "Key chain description");

/* 为 keychain 与 key 注册 QOBJ 类型，使其可被 show object 等调试命令枚举 */
DEFINE_QOBJ_TYPE(keychain);
DEFINE_QOBJ_TYPE(key);

/* 全局密钥链链表：保存本进程内所有 key chain（元素为 struct keychain） */
/* Master list of key chain. */
struct list *keychain_list;

/*
 * 分配并初始化一条新的（空的）密钥链对象。
 * 仅做内存分配与 QOBJ 注册，不设置名称、不加入全局链表；
 * 名称与链表登记由 keychain_get 负责。
 * 返回值：新分配的 struct keychain 指针。
 */
static struct keychain *keychain_new(void)
{
	struct keychain *keychain;
	keychain = XCALLOC(MTYPE_KEYCHAIN, sizeof(struct keychain));
	QOBJ_REG(keychain, keychain);
	return keychain;
}

/*
 * 释放密钥链对象本身（注销 QOBJ 并归还内存）。
 * 不负责释放 name / key 链表，调用方需先行清理。
 */
static void keychain_free(struct keychain *keychain)
{
	QOBJ_UNREG(keychain);
	XFREE(MTYPE_KEYCHAIN, keychain);
}

/*
 * 分配并初始化一个新的（空的）密钥对象。
 * 仅内存分配与 QOBJ 注册；编号、算法、链表挂接由 key_get 完成。
 * 返回值：新分配的 struct key 指针。
 */
static struct key *key_new(void)
{
	struct key *key = XCALLOC(MTYPE_KEY, sizeof(struct key));

	QOBJ_REG(key, key);
	return key;
}

/* 释放密钥对象本身（注销 QOBJ 并归还内存），不释放 string 字段 */
static void key_free(struct key *key)
{
	QOBJ_UNREG(key);
	XFREE(MTYPE_KEY, key);
}

/*
 * 按名称在全局链表中查找密钥链。
 * name 为 NULL 直接返回 NULL（防御性判断）。
 * 返回值：命中的 struct keychain，找不到返回 NULL（不创建）。
 */
struct keychain *keychain_lookup(const char *name)
{
	struct listnode *node;
	struct keychain *keychain;

	if (name == NULL)
		return NULL;

	for (ALL_LIST_ELEMENTS_RO(keychain_list, node, keychain)) {
		if (strcmp(keychain->name, name) == 0)
			return keychain;
	}
	return NULL;
}

/*
 * key 链表元素的排序比较函数：按 index 升序。
 * 作为 keychain->key 链表的 cmp 回调，使 listnode_add_sort 保持有序。
 * 返回值：k1 大于 / 小于 k2 时为 1 / -1，相等为 0。
 */
static int key_cmp_func(void *arg1, void *arg2)
{
	const struct key *k1 = arg1;
	const struct key *k2 = arg2;

	if (k1->index > k2->index)
		return 1;
	if (k1->index < k2->index)
		return -1;
	return 0;
}

/*
 * key 链表元素被移除时的析构回调：释放密钥字符串并释放 key 本身。
 * 由 linklist 在 list_delete / listnode_delete 时自动调用。
 */
static void key_delete_func(struct key *key)
{
	if (key->string)
		XFREE(MTYPE_KEY, key->string);
	key_free(key);
}

/*
 * 按名称获取密钥链，不存在则创建。
 * 新建时会设置 name、建立并配置 key 链表（排序函数 + 析构回调）并加入
 * 全局链表；因此本函数有“写副作用”。
 * 返回值：已存在或新建的 struct keychain。
 */
struct keychain *keychain_get(const char *name)
{
	struct keychain *keychain;

	keychain = keychain_lookup(name);

	if (keychain)
		return keychain;

	keychain = keychain_new();
	keychain->name = XSTRDUP(MTYPE_KEYCHAIN, name);
	keychain->key = list_new();
	keychain->key->cmp = (int (*)(void *, void *))key_cmp_func;
	keychain->key->del = (void (*)(void *))key_delete_func;
	listnode_add(keychain_list, keychain);

	return keychain;
}

/*
 * 删除一条密钥链：释放名称、销毁 key 链表、从全局链表摘除并释放对象。
 */
void keychain_delete(struct keychain *keychain)
{
	XFREE(MTYPE_KEYCHAIN, keychain->name);

	list_delete(&keychain->key);
	listnode_delete(keychain_list, keychain);
	keychain_free(keychain);
}

/*
 * 在指定密钥链内按编号查找 key（精确匹配 index）。
 * 返回值：命中的 struct key，找不到返回 NULL。
 */
struct key *key_lookup(const struct keychain *keychain, uint32_t index)
{
	struct listnode *node;
	struct key *key;

	for (ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if (key->index == index)
			return key;
	}
	return NULL;
}

/*
 * 按编号查找“可用于接收校验”的 key。
 * 语义：从编号 >= index 的 key 起，返回第一个处于接收窗口内的 key：
 *   - accept.start == 0 表示未设时间限制，直接可用；
 *   - 否则要求 accept.start <= now 且（accept.end >= now 或 end == -1）。
 * 参数：keychain 目标密钥链；index 接收侧期望的起始编号（下限）。
 * 返回值：命中的 struct key，找不到返回 NULL。
 * 注意：这里用的是编号下限语义，与 key_match_for_accept 的匹配方式不同。
 */
struct key *key_lookup_for_accept(const struct keychain *keychain,
				  uint32_t index)
{
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for (ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if (key->index >= index) {
			if (key->accept.start == 0)
				return key;

			if (key->accept.start <= now)
				if (key->accept.end >= now
				    || key->accept.end == -1)
					return key;
		}
	}
	return NULL;
}

/*
 * 按密钥串匹配“可用于接收校验”的 key。
 * 遍历链内 key，先判断是否处于接收时间窗口（窗口规则同
 * key_lookup_for_accept），再用 strncmp 比较前 16 字节密钥串，命中即返回。
 * 参数：keychain 目标密钥链；auth_str 报文携带的认证串。
 * 返回值：命中的 struct key，找不到返回 NULL。
 * 注意：仅比较前 16 字节，适合 MD5 等定长摘要场景。
 */
struct key *key_match_for_accept(const struct keychain *keychain,
				 const char *auth_str)
{
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for (ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if (key->accept.start == 0
		    || (key->accept.start <= now
			&& (key->accept.end >= now || key->accept.end == -1)))
			if (key->string && (strncmp(key->string, auth_str, 16) == 0))
				return key;
	}
	return NULL;
}

/*
 * 查找当前发送方向应使用的 key。
 * 语义：第一个处于发送窗口内的 key：
 *   - send.start == 0 表示未设时间限制，直接可用；
 *   - 否则要求 send.start <= now 且（send.end >= now 或 end == -1）。
 * 参数：keychain 目标密钥链。
 * 返回值：命中的 struct key，找不到返回 NULL（协议此时应放弃认证）。
 */
struct key *key_lookup_for_send(const struct keychain *keychain)
{
	struct listnode *node;
	struct key *key;
	time_t now;

	now = time(NULL);

	for (ALL_LIST_ELEMENTS_RO(keychain->key, node, key)) {
		if (key->send.start == 0)
			return key;

		if (key->send.start <= now)
			if (key->send.end >= now || key->send.end == -1)
				return key;
	}
	return NULL;
}

/*
 * 在指定密钥链内按编号获取 key，不存在则新建。
 * 新建的 key 编号为 index、算法初始化为 KEYCHAIN_ALGO_NULL，
 * 并按编号排序插入链表。
 * 返回值：已存在或新建的 struct key。
 * 注意：有“自动创建”副作用，仅应在配置写入路径调用。
 */
struct key *key_get(const struct keychain *keychain, uint32_t index)
{
	struct key *key;

	key = key_lookup(keychain, index);

	if (key)
		return key;

	key = key_new();
	key->index = index;
	key->hash_algo = KEYCHAIN_ALGO_NULL;
	listnode_add_sort(keychain->key, key);

	return key;
}

/* 从密钥链中删除一个 key：摘除链表节点、释放字符串并释放对象 */
void key_delete(struct keychain *keychain, struct key *key)
{
	listnode_delete(keychain->key, key);

	XFREE(MTYPE_KEY, key->string);
	key_free(key);
}

/*
 * 算法元信息表，下标必须与 enum keychain_hash_algo 一一对应
 * （NULL、MD5、SHA1、SHA256、SHA384、SHA512、MAX）。
 * 每项依次为：算法枚举、CLI 名称、摘要长度、内部分组长度、可读描述。
 * 若调整枚举顺序，务必同步此表。
 */
const struct keychain_algo_info algo_info[] = {
	{KEYCHAIN_ALGO_NULL, "null", 0, 0, "NULL"},
	{KEYCHAIN_ALGO_MD5, "md5", KEYCHAIN_MD5_HASH_SIZE,
	 KEYCHAIN_ALGO_MD5_INTERNAL_BLK_SIZE, "MD5"},
	{KEYCHAIN_ALGO_HMAC_SHA1, "hmac-sha-1", KEYCHAIN_HMAC_SHA1_HASH_SIZE,
	 KEYCHAIN_ALGO_SHA1_INTERNAL_BLK_SIZE, "HMAC-SHA-1"},
	{KEYCHAIN_ALGO_HMAC_SHA256, "hmac-sha-256",
	 KEYCHAIN_HMAC_SHA256_HASH_SIZE, KEYCHAIN_ALGO_SHA256_INTERNAL_BLK_SIZE,
	 "HMAC-SHA-256"},
	{KEYCHAIN_ALGO_HMAC_SHA384, "hmac-sha-384",
	 KEYCHAIN_HMAC_SHA384_HASH_SIZE, KEYCHAIN_ALGO_SHA384_INTERNAL_BLK_SIZE,
	 "HMAC-SHA-384"},
	{KEYCHAIN_ALGO_HMAC_SHA512, "hmac-sha-512",
	 KEYCHAIN_HMAC_SHA512_HASH_SIZE, KEYCHAIN_ALGO_SHA512_INTERNAL_BLK_SIZE,
	 "HMAC-SHA-512"},
	{KEYCHAIN_ALGO_MAX, "max", KEYCHAIN_MAX_HASH_SIZE,
	 KEYCHAIN_ALGO_MAX_INTERNAL_BLK_SIZE, "Not defined"}
};

/* 返回指定算法的内部分组长度（HMAC 归零补位时需要） */
uint16_t keychain_get_block_size(enum keychain_hash_algo key)
{
	return algo_info[key].block;
}

/* 返回指定算法的摘要（哈希）长度 */
uint16_t keychain_get_hash_len(enum keychain_hash_algo key)
{
	return algo_info[key].length;
}

/* 返回指定算法的人类可读描述字符串 */
const char *keychain_get_description(enum keychain_hash_algo key)
{
	return algo_info[key].desc;
}

/* 返回指定算法的完整元信息结构体（按值返回副本） */
struct keychain_algo_info
keychain_get_hash_algo_info(enum keychain_hash_algo key)
{
	return algo_info[key];
}

/*
 * 由命令中出现的算法名称反查算法枚举值。
 * 根据是否启用内部加密库（CRYPTO_INTERNAL）走不同分支：
 *   - 内部实现只识别 hmac-sha-2（映射到 SHA256）与以 m 开头的 md5；
 *   - 否则按前缀依次识别 md5、hmac-sha-1/2/3/5。
 * 参数：name 用户输入的名称（只比对前缀）。
 * 返回值：算法枚举；未识别时返回 KEYCHAIN_ALGO_NULL。
 * 注意：前缀匹配较宽松，判断顺序敏感（先 md5 再其它）。
 */
enum keychain_hash_algo keychain_get_algo_id_by_name(const char *name)
{
#ifdef CRYPTO_INTERNAL
	if (!strncmp(name, "hmac-sha-2", 10))
		return KEYCHAIN_ALGO_HMAC_SHA256;
	else if (!strncmp(name, "m", 1))
		return KEYCHAIN_ALGO_MD5;
	else
		return KEYCHAIN_ALGO_NULL;
#else
	if (!strncmp(name, "m", 1))
		return KEYCHAIN_ALGO_MD5;
	else if (!strncmp(name, "hmac-sha-1", 10))
		return KEYCHAIN_ALGO_HMAC_SHA1;
	else if (!strncmp(name, "hmac-sha-2", 10))
		return KEYCHAIN_ALGO_HMAC_SHA256;
	else if (!strncmp(name, "hmac-sha-3", 10))
		return KEYCHAIN_ALGO_HMAC_SHA384;
	else if (!strncmp(name, "hmac-sha-5", 10))
		return KEYCHAIN_ALGO_HMAC_SHA512;
	else
		return KEYCHAIN_ALGO_NULL;
#endif
}

/* 由算法枚举值反查 CLI 名称（未定义时返回对应表项名） */
const char *keychain_get_algo_name_by_id(enum keychain_hash_algo key)
{
	return algo_info[key].name;
}

/*
 * 关闭密钥链子系统：逐个删除全局链表中的所有密钥链，再销毁链表本身。
 * 每轮取链表头元素删除，避免迭代中失效。进程退出时调用。
 */
void keychain_terminate(void)
{
	struct keychain *keychain;

	while (listcount(keychain_list)) {
		keychain = listgetdata(listhead(keychain_list));

		listnode_delete(keychain_list, keychain);
		keychain_delete(keychain);
	}

	list_delete(&keychain_list);
}

/*
 * 初始化密钥链子系统：建立全局链表；若不在后端进程（!in_backend）
 * 还注册 VTY 命令。
 * 参数：in_backend 为 true 表示独立后端进程，跳过 CLI。
 */
void keychain_init_new(bool in_backend)
{
	keychain_list = list_new();

	if (!in_backend)
		keychain_cli_init();
}

/* keychain_init_new(false) 的便捷封装：前台进程含 CLI 初始化 */
void keychain_init(void)
{
	keychain_init_new(false);
}

/*
 * frr-deviations-ietf-key-chain 的 YANG 模块信息。
 * ignore_cfg_cbs = true 表示该偏差模块不需要配置回调；
 * nodes 里仅一个 xpath 为 NULL 的哨兵节点，起占位作用。
 */
const struct frr_yang_module_info ietf_key_chain_deviation_info = {
	.name = "frr-deviations-ietf-key-chain",
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = NULL,
		},
	},
};

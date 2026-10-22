// SPDX-License-Identifier: GPL-2.0-or-later
/* key-chain for authentication.
 * Copyright (C) 2000 Kunihiro Ishiguro
 */

/*
 * 文件导读：keychain.h —— FRR 密钥链（key chain）模块的公共头文件
 *
 * 1. 本文件在 FRR 里干什么：
 *    keychain 是 FRR 各路由协议（OSPF、RIP、BGP、IS-IS 等）共用的
 *    认证密钥管理设施。一个 key chain（密钥链）下可挂多个 key（密钥），
 *    每个 key 有自己的编号、密钥字符串、哈希算法，以及发送 / 接收两个
 *    生效时间窗口。协议在发报文或校验收报文时，向 keychain 模块查询
 *    当前应当使用哪个 key，从而做到密钥的定时轮换与平滑过渡。
 *
 * 2. 核心数据结构（阅读顺序）：
 *    - enum keychain_hash_algo：支持的哈希 / HMAC 算法枚举；
 *    - struct keychain_algo_info：算法元信息（名称、摘要长度、分组长度）；
 *    - struct keychain：一条密钥链，含名称、描述、key 链表；
 *    - struct key_range：一个时间窗口（起始、结束、持续秒数）；
 *    - struct key：单个密钥，含编号、密钥串、算法、send / accept 窗口；
 *    - 末尾的 keychain users 区声明了被其它模块引用的 API。
 *
 * 3. 与其它模块的关系：
 *    - keychain.c 给出主体实现；
 *    - keychain_cli.c 提供 VTY 命令解析与配置写出；
 *    - keychain_nb.c 提供 frr-keychain YANG 北向（NB）回调，把 YANG
 *      数据映射到这里的 struct keychain / struct key 上；
 *    - 各协议通过 key_lookup_for_send / key_lookup_for_accept 等函数取用。
 *
 * 4. 阅读顺序建议：
 *    先看本头文件了解数据结构，再读 keychain.c 的实现，然后按需查看
 *    keychain_cli.c（命令行）与 keychain_nb.c（YANG 回调）。
 */
#ifndef _ZEBRA_KEYCHAIN_H
#define _ZEBRA_KEYCHAIN_H

#include "memory.h"
#include "northbound.h"
#include "qobj.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 密钥链支持的哈希 / HMAC 认证算法枚举。
 * KEYCHAIN_ALGO_NULL 表示尚未指定算法；KEYCHAIN_ALGO_MAX 为哨兵值，
 * 不是真实算法，仅用于数组边界与取值校验。
 */
enum keychain_hash_algo {
	KEYCHAIN_ALGO_NULL,
	KEYCHAIN_ALGO_MD5,
	KEYCHAIN_ALGO_HMAC_SHA1,
	KEYCHAIN_ALGO_HMAC_SHA256,
	KEYCHAIN_ALGO_HMAC_SHA384,
	KEYCHAIN_ALGO_HMAC_SHA512,
	KEYCHAIN_ALGO_MAX
};

/* 各算法输出的摘要（哈希）字节长度 */
#define KEYCHAIN_MD5_HASH_SIZE 16
#define KEYCHAIN_HMAC_SHA1_HASH_SIZE 20
#define KEYCHAIN_HMAC_SHA256_HASH_SIZE 32
#define KEYCHAIN_HMAC_SHA384_HASH_SIZE 48
#define KEYCHAIN_HMAC_SHA512_HASH_SIZE 64
#define KEYCHAIN_MAX_HASH_SIZE 64

/* 各算法内部处理分组（block）的字节长度，HMAC 运算需要用到 */
#define KEYCHAIN_ALGO_MD5_INTERNAL_BLK_SIZE 16
#define KEYCHAIN_ALGO_SHA1_INTERNAL_BLK_SIZE 64
#define KEYCHAIN_ALGO_SHA256_INTERNAL_BLK_SIZE 64
#define KEYCHAIN_ALGO_SHA384_INTERNAL_BLK_SIZE 128
#define KEYCHAIN_ALGO_SHA512_INTERNAL_BLK_SIZE 128
#define KEYCHAIN_ALGO_MAX_INTERNAL_BLK_SIZE 128

/*
 * 单个哈希算法的元信息表项。
 * key：算法枚举值；name：CLI 名称（如 "hmac-sha-256"）；
 * length：摘要长度；block：内部分组长度；desc：人类可读描述。
 */
struct keychain_algo_info {
	enum keychain_hash_algo key;
	const char *name;
	uint16_t length;
	uint16_t block;
	const char *desc;
};

/* frr-keychain YANG 模块及其 CLI / 偏差（deviation）模块的注册信息 */
extern const struct frr_yang_module_info ietf_key_chain_info;
extern const struct frr_yang_module_info ietf_key_chain_cli_info;
extern const struct frr_yang_module_info ietf_key_chain_deviation_info;

/*
 * 算法信息全局表，下标即 enum keychain_hash_algo 的取值，
 * 定义在 keychain.c 中，供本文件下方的取用函数查询。
 */
extern const struct keychain_algo_info algo_info[];
uint16_t keychain_get_block_size(enum keychain_hash_algo key);
uint16_t keychain_get_hash_len(enum keychain_hash_algo key);
const char *keychain_get_description(enum keychain_hash_algo key);
struct keychain_algo_info
keychain_get_hash_algo_info(enum keychain_hash_algo key);
enum keychain_hash_algo keychain_get_algo_id_by_name(const char *name);
const char *keychain_get_algo_name_by_id(enum keychain_hash_algo key);

/*
 * 一条密钥链。
 * name：链名（唯一标识）；desc：描述；last_touch：最近一次配置修改时间；
 * key：本链下的 struct key 链表，按 index 升序排列（比较函数见 keychain.c）。
 */
struct keychain {
	char *name;
	char *desc;
	time_t last_touch;

	struct list *key;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(keychain);

/*
 * 密钥的时间窗口。
 * start / end：窗口起止的绝对时间（Unix 时间戳，end 为 -1 表示永不过期）；
 * duration：以 start 为基准的持续秒数（配置 lifetime duration 时使用）。
 */
struct key_range {
	time_t start;
	time_t end;

	uint8_t duration;
};

/*
 * 单个密钥。
 * index：密钥编号（同一 key chain 内唯一）；
 * string：密钥字符串；hash_algo：哈希 / HMAC 算法；
 * send / accept：发送方向与接收方向各自的时间窗口。
 */
struct key {
	uint32_t index;

	char *string;
	enum keychain_hash_algo hash_algo;
	struct key_range send;
	struct key_range accept;

	QOBJ_FIELDS;
};
DECLARE_QOBJ_TYPE(key);

/* 内存类型：密钥、密钥链、密钥链描述字符串 */
DECLARE_MTYPE(KEY);
DECLARE_MTYPE(KEYCHAIN);
DECLARE_MTYPE(KEYCHAIN_DESC);

/*
 * === keychain 实现层 API（定义在 keychain.c） ===
 */
/* keychain implementation */
extern struct list *keychain_list;
/* 按名称查找密钥链，不存在返回 NULL（不会创建） */
struct keychain *keychain_lookup(const char *name);
/* 按名称获取密钥链，不存在则新建并加入全局链表 */
struct keychain *keychain_get(const char *name);
/* 删除密钥链：释放名称、key 链表并从全局链表中摘除 */
void keychain_delete(struct keychain *keychain);
/* 在指定密钥链内按编号查找 key，不存在返回 NULL */
struct key *key_lookup(const struct keychain *keychain, uint32_t index);
/* 在指定密钥链内按编号获取 key，不存在则新建（按编号排序插入） */
struct key *key_get(const struct keychain *keychain, uint32_t index);
/* 删除指定密钥链内的某个 key */
void key_delete(struct keychain *keychain, struct key *key);

/* 初始化 keychain 的 VTY 命令节点（由 keychain_init 调用） */
void keychain_cli_init(void);
/*
 * 以下 *_cli_write 系列函数是 YANG 数据节点的 CLI 写出回调，
 * 由 confd / northbound 框架在 show running-config 时依次调用，
 * 把对应的 YANG 节点渲染成 VTY 配置行。命名规则：
 * key_chains_<路径各级>_cli_write。
 */
extern void key_chains_key_chain_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_cli_write_end(struct vty *vty, const struct lyd_node *dnode);
extern void key_chains_key_chain_description_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
void key_chains_key_chain_key_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_key_cli_write_end(struct vty *vty, const struct lyd_node *dnode);
extern void key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_key_crypto_algorithm_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);
extern void key_chains_key_chain_key_key_string_keystring_cli_write(struct vty *vty, const struct lyd_node *dnode, bool show_defaults);

/*
 * === keychain 使用方 API（供各路由协议调用） ===
 * key_lookup_for_accept：按编号与接收窗口查找可接收的 key；
 * key_match_for_accept：按密钥串匹配接收窗口内的 key；
 * key_lookup_for_send：查找当前发送方向应使用的 key。
 */
/* keychain users */
/* 初始化 keychain 子系统（含 CLI）；进程启动时调用一次 */
extern void keychain_init(void);
/*
 * 初始化 keychain 子系统。
 * in_backend 为 true 时表示运行在独立后端进程，不注册 VTY 命令。
 */
extern void keychain_init_new(bool in_backend);
/* 关闭 keychain 子系统：清空并销毁所有密钥链与全局链表 */
extern void keychain_terminate(void);
/* 同 keychain_lookup，供外部模块引用的声明 */
extern struct keychain *keychain_lookup(const char *);
/* 按编号查找接收窗口内可用的 key（用于校验收到的报文） */
extern struct key *key_lookup_for_accept(const struct keychain *keychain,
					 uint32_t index);
/* 按密钥串匹配接收窗口内的 key（认证串比对，取前 16 字节） */
extern struct key *key_match_for_accept(const struct keychain *keychain,
					const char *auth_str);
/* 查找当前发送方向应使用的 key（发送窗口内第一个可用者） */
extern struct key *key_lookup_for_send(const struct keychain *);
/* 返回算法的字符串名称（用于日志等场景） */
const char *keychain_algo_str(enum keychain_hash_algo hash_algo);



#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_KEYCHAIN_H */

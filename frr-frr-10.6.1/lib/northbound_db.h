// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

#ifndef _FRR_NORTHBOUND_DB_H_
#define _FRR_NORTHBOUND_DB_H_

/* ============================================================================
 * 【文件导读】northbound_db.h —— 北向配置数据库(Northbound DB)接口
 * ============================================================================
 * 【这个文件干什么】声明“把配置事务持久化到本地数据库”的一组接口。FRR 的
 * 北向子系统(northbound)在每次配置变更时生成一个 nb_transaction；本模块
 * 负责把事务存进 SQLite 数据库、按 ID 读回、清理旧记录、限制最长日志条数，
 * 从而支撑配置回滚(rollback)与变更历史查询。
 *
 * 【核心数据结构】围绕 struct nb_transaction(先声明结构体在 northbound.h)
 * 与 struct nb_config 展开；本头文件只暴露函数接口。
 *
 * 【关键前提】大部分功能在编译开关 HAVE_CONFIG_ROLLBACKS 打开时才真正生效；
 * 关闭时多数函数退化为直接返回 NB_OK 的空实现(详见 northbound_db.c)。
 *
 * 【与其他模块的关系】依赖 northbound.h(事务/配置类型)与 db.h(SQLite 封装)；
 * 由管理/配置模块调用。
 * 【阅读顺序】nb_db_init(建表) → save/load(存取事务) → clear/set_max(裁剪)
 * → transactions_iterate(遍历展示历史)。
 * ============================================================================
 */
#include "northbound.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the northbound database.
 *
 * Currently the database is used only for storing and retrieving configuration
 * transactions.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
/* 【初始化】建立北向配置数据库。
 * 在开启配置回滚(CONFIG_ROLLBACKS)时创建 transactions 表，并挂一个
 * delete_tail 的 SQL 触发器，实现“只保留最近 N 条记录”的环形缓冲。
 * 返回 NB_OK 成功、NB_ERR 失败；未开启回滚特性时直接返回 NB_OK(空实现)。 */
int nb_db_init(void);

/*
 * Save a configuration transaction in the northbound database.
 *
 * transaction
 *    Configuration transaction to be saved.
 *
 * transaction_id
 *    Output parameter providing the ID of the saved transaction.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
/* 【保存事务】把一次配置事务写进 transactions 日志表。
 *   transaction    : 待保存的配置事务。
 *   transaction_id : 输出参数，回填本次写入行的 ID(可传 NULL)。
 * 返回 NB_OK 成功、NB_ERR 失败。
 * 【要点】用 BEGIN/COMMIT 包住 INSERT 与取行号的 SELECT 以保证一致性；配置
 * 一律以 XML 记录且含默认值，以覆盖“默认值日后变化”的情况；失败时回滚。 */
int nb_db_transaction_save(const struct nb_transaction *transaction,
			   uint32_t *transaction_id);

/*
 * Load a configuration transaction from the transactions log.
 *
 * transaction_id
 *    ID of the transaction to be loaded.
 *
 * Returns:
 *    Pointer to newly created configuration or NULL in the case of an error.
 */
/* 【加载事务】按 transaction_id 从日志表读出配置并解析成 nb_config。
 *   返回：新建的 nb_config 指针；找不到或解析出错时返回 NULL。
 * 【易踩的坑】解析失败只发告警(flog_warn)并返回 NULL，绝不返回半成品。 */
extern struct nb_config *nb_db_transaction_load(uint32_t transaction_id);

/*
 * Delete the specified number of transactions from the transactions log.
 *
 * n_oldest
 *    Number of transactions to delete.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
/* 【清理旧记录】删除最旧的 n_oldest 条事务(按 ROWID 升序取前 N 条删除)。
 * 返回 NB_OK 成功、NB_ERR 失败。 */
extern int nb_db_clear_transactions(unsigned int n_oldest);

/*
 * Specify the maximum number of transactions we want to record in the
 * transactions log. Note that older transactions can be removed during this
 * operation.
 *
 * max
 *    New upper limit of maximum transactions to log.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
/* 【设上限】设定日志表最多保留多少条事务；若当前超限会先删旧记录，并重建
 * delete_tail 触发器按新上限自动裁剪。返回 NB_OK 成功、NB_ERR 失败。
 * 【易踩的坑】过程中会 DROP 再 CREATE 触发器，事务期间触发器短暂缺失。 */
extern int nb_db_set_max_transactions(unsigned int max);

/*
 * Iterate over all configuration transactions stored in the northbound
 * database, sorted in descending order.
 *
 * func
 *    Function to call with each configuration transaction.
 *
 * arg
 *    Arbitrary argument passed as the first parameter in each call to 'func'.
 *
 * Returns:
 *    NB_OK on success, NB_ERR otherwise.
 */
/* 【遍历】按 ROWID 降序(新到旧)遍历所有事务，对每条调用回调
 * func(arg, transaction_id, client_name, date, comment)。返回 NB_OK / NB_ERR。
 * 【易踩的坑】单条 db_loadf 失败会 continue 跳过该条，不中断整体遍历。 */
extern int nb_db_transactions_iterate(
	void (*func)(void *arg, int transaction_id, const char *client_name,
		     const char *date, const char *comment),
	void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_NORTHBOUND_DB_H_ */

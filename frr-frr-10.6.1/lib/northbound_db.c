// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018  NetDEF, Inc.
 *                     Renato Westphal
 */

/* ============================================================================
 * 【文件导读】northbound_db.c —— 用 SQLite 保存配置事务，实现配置回滚
 * ============================================================================
 * 【这个文件干什么】把 northbound 子系统每次提交的配置事务(含客户端名、时间、
 * 注释、完整 XML 配置)写进 SQLite 的 transactions 表，并提供按 ID 读回、清理
 * 旧记录、限制日志条数、遍历历史的能力，支撑“配置回滚”和变更审计。
 *
 * 【表结构】transactions(client, date, comment, configuration)，rowid 作为事
 * 务 ID；date 默认取 CURRENT_TIMESTAMP。另有一个 delete_tail 触发器，在每次
 * INSERT 后删除过老的行，形成环形缓冲，只保留最近 N 条。
 *
 * 【依赖】db.h 提供 SQLite 封装(db_execute / db_prepare / db_bindf / db_run
 *   / db_loadf / db_finalize)；libyang 负责 XML 与 nb_config 的互转。
 *
 * 【重要前提】除 nb_db_transaction_load 外，几乎全部逻辑都在 HAVE_CONFIG_ROLLBACKS
 * 宏内；未开启回滚时各函数退化为直接返回 NB_OK 的空壳。阅读时注意 #ifdef。
 *
 * 【阅读顺序】nb_db_init(建表+挂触发器) → save(事务写入) → load(读回) →
 * clear / set_max(裁剪) → transactions_iterate(遍历)。
 * ============================================================================
 */
#include <zebra.h>

#include "libfrr.h"
#include "log.h"
#include "lib_errors.h"
#include "command.h"
#include "db.h"
#include "northbound.h"
#include "northbound_db.h"

/* 【初始化】创建 transactions 表并安装 delete_tail 触发器。
 * 触发器语义：每次插入新行后，把 rowid 与 NEW.rowid 同余但非自身的旧行删掉，
 * 从而让表中始终只剩最近的 NB_DLFT_MAX_CONFIG_ROLLBACKS 条。
 * 返回 NB_OK / NB_ERR；未开启回滚特性时直接 NB_OK。 */
int nb_db_init(void)
{
#ifdef HAVE_CONFIG_ROLLBACKS
	/*
	 * NOTE: the delete_tail SQL trigger is used to implement a ring buffer
	 * where only the last N transactions are recorded in the configuration
	 * log.
	 */
	if (db_execute(
		    "BEGIN TRANSACTION;\n"
		    "  CREATE TABLE IF NOT EXISTS transactions(\n"
		    "    client         CHAR(32)             NOT NULL,\n"
		    "    date           DATETIME             DEFAULT CURRENT_TIMESTAMP,\n"
		    "    comment        CHAR(80)             ,\n"
		    "    configuration  TEXT                 NOT NULL\n"
		    "  );\n"
		    "  CREATE TRIGGER IF NOT EXISTS delete_tail\n"
		    "    AFTER INSERT ON transactions\n"
		    "    FOR EACH ROW\n"
		    "    BEGIN\n"
		    "    DELETE\n"
		    "    FROM\n"
		    "      transactions\n"
		    "    WHERE\n"
		    "      rowid%%%u=NEW.rowid%%%u AND rowid!=NEW.rowid;\n"
		    "    END;\n"
		    "COMMIT;",
		    NB_DLFT_MAX_CONFIG_ROLLBACKS, NB_DLFT_MAX_CONFIG_ROLLBACKS)
	    != 0)
		return NB_ERR;
#endif /* HAVE_CONFIG_ROLLBACKS */

	return NB_OK;
}

/* 【保存事务】把一次配置事务写入日志表并(可选)回填其行号 ID。
 *   流程：BEGIN → INSERT(client, comment, configuration) → 取 last_insert_rowid
 *         得到行号 → COMMIT；任一步失败则跳转 exit 并 ROLLBACK，返回 NB_ERR。
 *   参数：transaction 待存事务；transaction_id 输出参数，可传 NULL。
 * 【要点】配置统一以 XML 打印并带默认值(LYD_PRINT_WD_ALL)，以便默认值日后
 * 变化时历史仍可精确重建；client_name 由 nb_client_name() 转换得到。
 * 【易踩的坑】exit 处对 ss 是否已 finalize 的清理要小心，避免二次释放；只有
 * 失败(ret 非 OK)时才发 ROLLBACK，依赖前面确已 BEGIN。 */
int nb_db_transaction_save(const struct nb_transaction *transaction,
			   uint32_t *transaction_id)
{
#ifdef HAVE_CONFIG_ROLLBACKS
	struct sqlite3_stmt *ss;
	const char *client_name;
	char *config_str = NULL;
	int ret = NB_ERR;

	/*
	 * Use a transaction to ensure consistency between the INSERT and SELECT
	 * queries.
	 */
	if (db_execute("BEGIN TRANSACTION;") != 0)
		return NB_ERR;

	ss = db_prepare(
		"INSERT INTO transactions\n"
		"  (client, comment, configuration)\n"
		"VALUES\n"
		"  (?, ?, ?);");
	if (!ss)
		goto exit;

	client_name = nb_client_name(transaction->context.client);
	/*
	 * Always record configurations in the XML format, save the default
	 * values too, as this covers the case where defaults may change.
	 */
	if (lyd_print_mem(&config_str, transaction->config->dnode, LYD_XML,
			  LYD_PRINT_WITHSIBLINGS | LYD_PRINT_WD_ALL)
	    != 0)
		goto exit;

	if (db_bindf(ss, "%s%s%s", client_name, strlen(client_name),
		     transaction->comment, strlen(transaction->comment),
		     config_str ? config_str : "",
		     config_str ? strlen(config_str) : 0)
	    != 0)
		goto exit;

	if (db_run(ss) != SQLITE_OK)
		goto exit;

	db_finalize(&ss);

	/*
	 * transaction_id is an optional output parameter that provides the ID
	 * of the recorded transaction.
	 */
	if (transaction_id) {
		ss = db_prepare("SELECT last_insert_rowid();");
		if (!ss)
			goto exit;

		if (db_run(ss) != SQLITE_ROW)
			goto exit;

		if (db_loadf(ss, "%i", transaction_id) != 0)
			goto exit;

		db_finalize(&ss);
	}

	if (db_execute("COMMIT;") != 0)
		goto exit;

	ret = NB_OK;

exit:
	if (config_str)
		free(config_str);
	if (ss)
		db_finalize(&ss);
	if (ret != NB_OK)
		(void)db_execute("ROLLBACK TRANSACTION;");

	return ret;
#else  /* HAVE_CONFIG_ROLLBACKS */
	return NB_OK;
#endif /* HAVE_CONFIG_ROLLBACKS */
}

/* 【加载事务】按 transaction_id 读出一行 configuration 并按 XML 严格解析成
 * nb_config。返回：新的 nb_config；查询无结果或解析失败返回 NULL(后者会发
 * flog_warn 告警)。
 * 【易踩的坑】解析使用 STRICT 与 NO_STATE 选项，只接受合法且不含状态的数据，
 * 以保证还原出来的是纯配置。 */
struct nb_config *nb_db_transaction_load(uint32_t transaction_id)
{
	struct nb_config *config = NULL;
#ifdef HAVE_CONFIG_ROLLBACKS
	struct lyd_node *dnode;
	const char *config_str;
	struct sqlite3_stmt *ss;
	LY_ERR err;

	ss = db_prepare(
		"SELECT\n"
		"  configuration\n"
		"FROM\n"
		"  transactions\n"
		"WHERE\n"
		"  rowid=?;");
	if (!ss)
		return NULL;

	if (db_bindf(ss, "%d", transaction_id) != 0)
		goto exit;

	if (db_run(ss) != SQLITE_ROW)
		goto exit;

	if (db_loadf(ss, "%s", &config_str) != 0)
		goto exit;

	err = lyd_parse_data_mem(ly_native_ctx, config_str, LYD_XML,
				 LYD_PARSE_STRICT | LYD_PARSE_NO_STATE,
				 LYD_VALIDATE_NO_STATE, &dnode);
	if (err || !dnode)
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_parse_data_mem() failed",
			  __func__);
	else
		config = nb_config_new(dnode);

exit:
	db_finalize(&ss);
#endif /* HAVE_CONFIG_ROLLBACKS */

	return config;
}

/* 【清理旧记录】删除最旧的 n_oldest 条事务：子查询按 ROWID 升序取前 N 个
 * rowid 再删除。返回 NB_OK / NB_ERR；未开启回滚特性时直接 NB_OK。 */
int nb_db_clear_transactions(unsigned int n_oldest)
{
#ifdef HAVE_CONFIG_ROLLBACKS
	/* Delete oldest N entries. */
	if (db_execute("DELETE\n"
		       "FROM\n"
		       "  transactions\n"
		       "WHERE\n"
		       "  ROWID IN (\n"
		       "    SELECT\n"
		       "      ROWID\n"
		       "    FROM\n"
		       "      transactions\n"
		       "    ORDER BY ROWID ASC LIMIT %u\n"
		       "  );",
		       n_oldest)
	    != 0)
		return NB_ERR;
#endif /* HAVE_CONFIG_ROLLBACKS */

	return NB_OK;
}

/* 【设上限】把日志表上限改为 max：先删掉超出部分(按 ROWID 降序、OFFSET max
 * 之后的行)，再 DROP 并重建 delete_tail 触发器以套用新上限。
 * 返回 NB_OK / NB_ERR；未开启回滚特性时直接 NB_OK。 */
int nb_db_set_max_transactions(unsigned int max)
{
#ifdef HAVE_CONFIG_ROLLBACKS
	/*
	 * Delete old entries if necessary and update the SQL trigger that
	 * auto-deletes old entries.
	 */
	if (db_execute("BEGIN TRANSACTION;\n"
		       "  DELETE\n"
		       "  FROM\n"
		       "    transactions\n"
		       "  WHERE\n"
		       "    ROWID IN (\n"
		       "      SELECT\n"
		       "        ROWID\n"
		       "      FROM\n"
		       "        transactions\n"
		       "      ORDER BY ROWID DESC LIMIT -1 OFFSET %u\n"
		       "    );\n"
		       "  DROP TRIGGER delete_tail;\n"
		       "  CREATE TRIGGER delete_tail\n"
		       "  AFTER INSERT ON transactions\n"
		       "    FOR EACH ROW\n"
		       "    BEGIN\n"
		       "    DELETE\n"
		       "    FROM\n"
		       "      transactions\n"
		       "    WHERE\n"
		       "      rowid%%%u=NEW.rowid%%%u AND rowid!=NEW.rowid;\n"
		       "    END;\n"
		       "COMMIT;",
		       max, max, max)
	    != 0)
		return NB_ERR;
#endif /* HAVE_CONFIG_ROLLBACKS */

	return NB_OK;
}

/* 【遍历】按 rowid 降序(新到旧)查询全部事务，逐条调用回调
 *   func(arg, transaction_id, client_name, date, comment)。
 * 返回 NB_OK；未开启回滚特性时直接 NB_OK。
 * 【易踩的坑】单条读取失败(ret 非 0)会 continue 跳过该条，不终止整体遍历；
 * 数据库句柄在本函数末尾才 finalize。 */
int nb_db_transactions_iterate(void (*func)(void *arg, int transaction_id,
					    const char *client_name,
					    const char *date,
					    const char *comment),
			       void *arg)
{
#ifdef HAVE_CONFIG_ROLLBACKS
	struct sqlite3_stmt *ss;

	/* Send SQL query and parse the result. */
	ss = db_prepare(
		"SELECT\n"
		"  rowid, client, date, comment\n"
		"FROM\n"
		"  transactions\n"
		"ORDER BY\n"
		"  rowid DESC;");
	if (!ss)
		return NB_ERR;

	while (db_run(ss) == SQLITE_ROW) {
		int transaction_id;
		const char *client_name;
		const char *date;
		const char *comment;
		int ret;

		ret = db_loadf(ss, "%i%s%s%s", &transaction_id, &client_name,
			       &date, &comment);
		if (ret != 0)
			continue;

		(*func)(arg, transaction_id, client_name, date, comment);
	}

	db_finalize(&ss);
#endif /* HAVE_CONFIG_ROLLBACKS */

	return NB_OK;
}

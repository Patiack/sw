// SPDX-License-Identifier: ISC AND GPL-2.0-or-later
/*
 * Copyright (c) 2018 Rafael Zalamena <rzalamena@gmail.com>
 */

/*
 * Copyright (c) 2016 Rafael Zalamena <rzalamena@gmail.com>
 */

/*
 * ============================================================================
 * 【文件导读】lib/db.c —— FRR 的 SQLite3 数据库封装
 * ============================================================================
 * 职责：
 *   本文件是 FRR 对 SQLite3 的一层极薄封装，为上层模块提供统一的
 *   "数据库表" 读写接口。核心思想是把 SQL 语句的准备、绑定、执行、
 *   取值、释放这一整套生命周期，收敛成几个以 db_ 开头的函数，
 *   让调用者不必直接面对 sqlite3 C API 的细节。
 *
 * 核心数据结构：
 *   static struct sqlite3 *dbp —— 全进程唯一的数据库句柄（单例）。
 *       db_init 打开它，db_close 关闭并置空；其它函数都要求它非空。
 *   struct sqlite3_stmt       —— SQLite 的"预编译语句"句柄，代表一条
 *       已解析的 SQL，由 db_prepare / db_prepare_len 产生，由
 *       db_finalize 回收。
 *
 * 与其它模块的关系：
 *   上层模块（如各类配置/状态持久化代码、需要落盘的 L2/L3 表项）通过
 *   这些接口把内存中的表编码成 SQL 行写入磁盘，或从磁盘解码回内存。
 *   本文件只做机制不做策略：不关心表结构，也不关心 ZEBRA 消息语义，
 *   语义由调用方通过格式串（fmt）与 SQL 语句本身表达。
 *
 * 阅读顺序：
 *   1) db_init / db_close       —— 生命周期的起止；
 *   2) db_vbindf / db_bindf     —— 把参数写进 SQL 占位符；
 *   3) db_prepare_len / db_prepare / db_run —— 语句的准备与执行；
 *   4) db_vloadf / db_loadf     —— 从结果行读回变量；
 *   5) db_finalize / db_execute —— 回收与一次性执行多语句。
 *
 * 易踩的坑：
 *   - dbp 是全局单例：重复调用 db_init 会失败（返回 -1）。
 *   - 绑定/取值用的格式串是 "SQLite 风格"，不是 printf / scanf 风格：
 *     i=int32、d=int64、s=字符串、b=二进制、n=NULL；s 与 b 需要额外
 *     跟一个长度参数。别把它当 printf 用。
 *   - SQLITE_DONE 与 SQLITE_OK 都会被当作成功，但取值阶段期望
 *     SQLITE_ROW，务必用 db_run 的返回值判断是否有结果行。
 *   - 字符串/二进制绑定用 SQLITE_STATIC，表示调用方保证内存在语句
 *     执行完成前不被释放；传临时缓冲区要格外小心。
 * ============================================================================
 */

#include <zebra.h>

#include "db.h"
#include "log.h"

// 【全局变量】dbp：全进程唯一的 SQLite3 数据库句柄（单例）。
// 由 db_init 赋值、db_close 置空；其余接口都依赖它非空才能工作。
static struct sqlite3 *dbp;

/*
 * Initialize the database in path.
 *
 * It's possible to use in memory database with ':memory:' path.
 */
// 【函数】db_init
//   打开（必要时创建）数据库。path_fmt 是 printf 风格的路径模板，
//   支持用 ':memory:' 打开纯内存数据库。成功返回 0，失败返回 -1。
//   注意 dbp 是单例：已打开时直接返回 -1，不会重复打开。
int db_init(const char *path_fmt, ...)
{
	char path[BUFSIZ];
	va_list ap;

	if (dbp)
		return -1;

	va_start(ap, path_fmt);
	vsnprintf(path, sizeof(path), path_fmt, ap);
	va_end(ap);

	if (sqlite3_open_v2(path, &dbp,
			    (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE), NULL)
	    != SQLITE_OK) {
		if (dbp == NULL) {
			zlog_warn("%s: failed to open database '%s'", __func__,
				  path);
			return -1;
		}

		zlog_warn("%s: failed to open database '%s': %s", __func__,
			  path, sqlite3_errmsg(dbp));
		if (sqlite3_close_v2(dbp) != SQLITE_OK)
			zlog_warn("%s: failed to terminate database", __func__);
		dbp = NULL;
		return -1;
	}

	return 0;
}

/* Closes the database if open. */
// 【函数】db_close
//   关闭数据库（若已打开）。未打开时视作成功返回 0；
//   关闭失败时打印告警并返回 -1。
int db_close(void)
{
	if (dbp == NULL)
		return 0;

	if (sqlite3_close_v2(dbp) != SQLITE_OK) {
		zlog_warn("%s: failed to terminate database", __func__);
		return -1;
	}
	return 0;
}

/* Helper function to handle formating. */
// 【函数】db_vbindf（static 辅助函数）
//   把可变参数按 fmt 格式串依次绑定到预编译语句 ss 的占位符上。
//   格式符含义：
//     i -> uint32_t，绑定为 SQL 整数（int）
//     d -> uint64_t，绑定为 SQL 64 位整数（int64）
//     s -> const char 指针，其后紧跟一个 int 长度，绑定为文本
//     b -> void 指针，其后紧跟一个 int 长度，绑定为二进制 BLOB
//     n -> 无参数，绑定 SQL NULL
//   列号 column 从 1 开始自增；遇到未知格式符打印告警并返回 -1。
//   所有绑定内存均用 SQLITE_STATIC：调用方需保证语句执行完前内存有效。
static int db_vbindf(struct sqlite3_stmt *ss, const char *fmt, va_list vl)
{
	const char *sptr = fmt;
	int column = 1;
	const char *str;
	void *blob;
	uint64_t uinteger64;
	uint32_t uinteger;
	int vlen;

	while (*sptr) {
		if (*sptr != '%') {
			sptr++;
			continue;
		}
		if (sptr++ && *sptr == 0)
			break;

		switch (*sptr) {
		case 'i':
			uinteger = va_arg(vl, uint32_t);
			if (sqlite3_bind_int(ss, column++, uinteger)
			    != SQLITE_OK)
				return -1;
			break;
		case 'd':
			uinteger64 = va_arg(vl, uint64_t);
			if (sqlite3_bind_int64(ss, column++, uinteger64)
			    != SQLITE_OK)
				return -1;
			break;
		case 's':
			str = va_arg(vl, const char *);
			vlen = va_arg(vl, int);
			if (sqlite3_bind_text(ss, column++, str, vlen,
					      SQLITE_STATIC)
			    != SQLITE_OK)
				return -1;
			break;
		case 'b':
			blob = va_arg(vl, void *);
			vlen = va_arg(vl, int);
			if (sqlite3_bind_blob(ss, column++, blob, vlen,
					      SQLITE_STATIC)
			    != SQLITE_OK)
				return -1;
			break;
		case 'n':
			if (sqlite3_bind_null(ss, column++) != SQLITE_OK)
				return -1;
			break;
		default:
			zlog_warn("%s: invalid format '%c'", __func__, *sptr);
			return -1;
		}
	}

	return 0;
}

/*
 * Binds values using format to the database query.
 *
 * Might be used to bind variables to a query, insert or update.
 */
// 【函数】db_bindf
//   可变参数版本的绑定入口：打包 va_list 后转调 db_vbindf。
//   常用于 insert / update 之类的写操作。
int db_bindf(struct sqlite3_stmt *ss, const char *fmt, ...)
{
	va_list vl;
	int result;

	va_start(vl, fmt);
	result = db_vbindf(ss, fmt, vl);
	va_end(vl);

	return result;
}

/* Prepares an statement to the database with the statement length. */
// 【函数】db_prepare_len
//   用给定长度把 SQL 文本预编译成语句句柄。sqlite3 允许语句中出现
//   嵌入的 NUL（用显式长度），因此单独提供 _len 版本。
//   数据库未打开或准备失败时返回 NULL。
struct sqlite3_stmt *db_prepare_len(const char *stmt, int stmtlen)
{
	struct sqlite3_stmt *ss;
	int c;

	if (dbp == NULL)
		return NULL;

	c = sqlite3_prepare_v2(dbp, stmt, stmtlen, &ss, NULL);
	if (ss == NULL) {
		zlog_warn("%s: failed to prepare (%d:%s)", __func__, c,
			  sqlite3_errmsg(dbp));
		return NULL;
	}

	return ss;
}

/* Prepares an statement to the database. */
// 【函数】db_prepare
//   db_prepare_len 的便捷封装：用 strlen 计算语句长度。
struct sqlite3_stmt *db_prepare(const char *stmt)
{
	return db_prepare_len(stmt, strlen(stmt));
}

/* Run a prepared statement. */
// 【函数】db_run
//   执行一次已准备好的语句（底层 sqlite3_step）。
//   返回值语义被归一化：
//     SQLITE_BUSY —— 数据库忙，目前仅留 TODO，未做重试；
//     SQLITE_OK / SQLITE_DONE —— 统一返回 SQLITE_OK（查询正常结束）；
//     SQLITE_ROW  —— 说明取到一行结果（搜索类查询的正常返回）；
//     其它 —— 打印告警并返回原始错误码。
int db_run(struct sqlite3_stmt *ss)
{
	int result;

	result = sqlite3_step(ss);
	switch (result) {
	case SQLITE_BUSY:
		/* TODO handle busy database. */
		break;

	case SQLITE_OK:
	/*
	 * SQLITE_DONE just causes confusion since it means the query went OK,
	 * but it has a different value.
	 */
	case SQLITE_DONE:
		result = SQLITE_OK;
		break;

	case SQLITE_ROW:
		/* NOTHING */
		/* It is expected to receive SQLITE_ROW on search queries. */
		break;

	default:
		zlog_warn("%s: step failed (%d:%s)", __func__, result,
			  sqlite3_errstr(result));
	}

	return result;
}

/* Helper function to load format to variables. */
// 【函数】db_vloadf（static 辅助函数）
//   与 db_vbindf 相反：把当前结果行的各列按 fmt 读出，写入调用方指针。
//   列号 column 从 0 开始自增（注意与绑定时从 1 开始不同！）。
//   格式符含义：
//     i -> uint32_t 指针，取整数列
//     d -> uint64_t 指针，取 64 位整数列
//     s -> const char 二重指针，取文本列（指向 SQLite 内部缓冲，勿长期持有）
//     b -> void 指针，其后紧跟一个 int 长度，拷贝 BLOB，按较小者截断
//   查询无列（column_count 为 0）或格式非法时返回 -1。
static int db_vloadf(struct sqlite3_stmt *ss, const char *fmt, va_list vl)
{
	const char *sptr = fmt;
	int column = 0;
	const char **str;
	void *blob;
	const void *blobsrc;
	uint64_t *uinteger64;
	uint32_t *uinteger;
	int vlen;
	int dlen;
	int columncount;

	columncount = sqlite3_column_count(ss);
	if (columncount == 0)
		return -1;

	while (*sptr) {
		if (*sptr != '%') {
			sptr++;
			continue;
		}
		if (sptr++ && *sptr == 0)
			break;

		switch (*sptr) {
		case 'i':
			uinteger = va_arg(vl, uint32_t *);
			*uinteger = sqlite3_column_int(ss, column);
			break;
		case 'd':
			uinteger64 = va_arg(vl, uint64_t *);
			*uinteger64 = sqlite3_column_int64(ss, column);
			break;
		case 's':
			str = va_arg(vl, const char **);
			*str = (const char *)sqlite3_column_text(ss, column);
			break;
		case 'b':
			blob = va_arg(vl, void *);
			vlen = va_arg(vl, int);
			dlen = sqlite3_column_bytes(ss, column);
			blobsrc = sqlite3_column_blob(ss, column);
			memcpy(blob, blobsrc, MIN(vlen, dlen));
			break;
		default:
			zlog_warn("%s: invalid format '%c'", __func__, *sptr);
			return -1;
		}

		column++;
	}

	return 0;
}

/* Function to load format from database row. */
// 【函数】db_loadf
//   可变参数版本的取值入口：打包 va_list 后转调 db_vloadf。
int db_loadf(struct sqlite3_stmt *ss, const char *fmt, ...)
{
	va_list vl;
	int result;

	va_start(vl, fmt);
	result = db_vloadf(ss, fmt, vl);
	va_end(vl);

	return result;
}

/* Finalize query and return memory. */
// 【函数】db_finalize
//   释放预编译语句占用的资源，并把调用方的句柄指针置 NULL，
//   防止悬空指针被再次使用。
void db_finalize(struct sqlite3_stmt **ss)
{
	sqlite3_finalize(*ss);
	*ss = NULL;
}

/* Execute one or more statements. */
// 【函数】db_execute
//   一次性执行一条或多条（分号分隔）SQL 文本，不需要预编译/绑定。
//   stmt_fmt 为 printf 风格模板。数据库未打开或执行失败返回 -1。
int db_execute(const char *stmt_fmt, ...)
{
	char stmt[BUFSIZ];
	va_list ap;

	if (dbp == NULL)
		return -1;

	va_start(ap, stmt_fmt);
	vsnprintf(stmt, sizeof(stmt), stmt_fmt, ap);
	va_end(ap);

	if (sqlite3_exec(dbp, stmt, NULL, 0, NULL) != SQLITE_OK) {
		zlog_warn("%s: failed to execute statement(s): %s", __func__,
			  sqlite3_errmsg(dbp));
		return -1;
	}

	return 0;
}

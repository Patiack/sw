// SPDX-License-Identifier: ISC AND GPL-2.0-or-later
/*
 * Copyright (c) 2018 Rafael Zalamena <rzalamena@gmail.com>
 */

/*
 * Copyright (c) 2016 Rafael Zalamena <rzalamena@gmail.com>
 */

/*
 * ============================================================================
 * 【文件导读】lib/db.h —— SQLite3 数据库封装的对外声明
 * ============================================================================
 * 职责：
 *   声明 lib/db.c 中实现的数据库接口，供全 FRR 引用。整份头文件被
 *   HAVE_SQLITE3 宏保护：没有在编译期打开 SQLite3 时，这里不导出任何
 *   符号，调用方须自行用同样的宏做条件编译。
 *
 * 核心抽象：
 *   struct sqlite3 / struct sqlite3_stmt 是 SQLite3 的不透明句柄，
 *   前者代表一个数据库连接（在 db.c 中以单例 dbp 持有），后者代表
 *   一条预编译语句。本头文件不暴露其内部结构，只传递指针。
 *
 * 与其它模块的关系：
 *   - 依赖 sqlite3.h 提供底层类型。
 *   - 依赖 compiler.h 提供 PRINTFRR 属性宏（格式串静态检查）。
 *   - 调用方是各类需要持久化"数据库表"的上层代码：先 db_init 建连接，
 *     再 db_prepare 编译 SQL，db_bindf 绑值，db_run 执行，db_loadf 取值，
 *     最后 db_finalize 回收。
 *
 * 阅读顺序：
 *   连接类：db_init / db_close
 *   语句类：db_prepare / db_prepare_len / db_finalize / db_run / db_execute
 *   数据类：db_bindf（写）/ db_loadf（读）
 *
 * 易踩的坑：
 *   - db_bindf / db_loadf 的格式串是 "SQLite 风格"，不是 printf / scanf
 *     风格；其上方英文注释特意标了 WARNING。
 *   - db_init / db_execute 的路径与语句模板是 printf 风格，故带
 *     PRINTFRR(1, 2) 静态检查；而 db_bindf / db_loadf 的 fmt 不是
 *     printf 风格，故未加该属性。
 * ============================================================================
 */

#ifndef _FRR_DB_H_
#define _FRR_DB_H_
#ifdef HAVE_SQLITE3

#include "compiler.h"
#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

// db_init：按 printf 风格路径模板打开或创建数据库，返回 0 成功、-1 失败。
extern int db_init(const char *path_fmt, ...) PRINTFRR(1, 2);
// db_close：关闭数据库；未打开时视为成功返回 0。
extern int db_close(void);
/* WARNING: sqlite format string! not printf compatible! */
// db_bindf：按 SQLite 风格格式串把参数绑定到预编译语句的占位符上。
extern int db_bindf(struct sqlite3_stmt *ss, const char *fmt, ...);
// db_prepare_len：用显式长度把 SQL 预编译为语句句柄（允许嵌入 NUL）。
extern struct sqlite3_stmt *db_prepare_len(const char *stmt, int stmtlen);
// db_prepare：db_prepare_len 的便捷版，长度取 strlen(stmt)。
extern struct sqlite3_stmt *db_prepare(const char *stmt);
// db_run：执行一次已准备语句，返回归一化后的结果码（见 db.c）。
extern int db_run(struct sqlite3_stmt *ss);
/* WARNING: sqlite format string! not scanf compatible! */
// db_loadf：按 SQLite 风格格式串从当前结果行读回各列到调用方指针。
extern int db_loadf(struct sqlite3_stmt *ss, const char *fmt, ...);
// db_finalize：释放语句资源，并把调用方句柄指针置为 NULL。
extern void db_finalize(struct sqlite3_stmt **ss);
// db_execute：一次性执行一条或多条 SQL 模板（printf 风格），返回 0 / -1。
extern int db_execute(const char *stmt_fmt, ...) PRINTFRR(1, 2);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_SQLITE3 */
#endif /* _FRR_DB_H_ */

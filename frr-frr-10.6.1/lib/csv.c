// SPDX-License-Identifier: GPL-2.0-or-later
/* CSV
 * Copyright (C) 2013,2020  Cumulus Networks, Inc.
 */

/*
 * 文件导读：csv.c —— 逗号分隔值（CSV）编解码实现
 *
 * 一、这个文件做什么
 *   实现 csv.h 声明的 CSV 编解码：把若干字段拼成以逗号分隔、以换行结尾的一行
 *   （记录），或把这样的文本切分成记录链表与字段链表。FRR 中主要用于进程间 /
 *   节点间的结构化消息传递。
 *
 * 二、核心数据结构
 *   - csv_field_t：一个字段，field 指向所属字符串中的子串，field_len 为其长度。
 *   - csv_record_t：一条记录，内部是字段的 TAILQ 链表；record 指向整行文本。
 *   - csv_t：会话，含记录 TAILQ 链表、底层缓冲 buf、容量 buflen、已用长度
 *     csv_len、写指针 pointer、记录数 num_recs。
 *   三者都用 TAILQ 串成链表（见 sys/queue.h）。
 *
 * 三、两种缓冲模式（重要）
 *   1. 单一连续缓冲：csv_init 时传入 buf，多条记录共用同一块内存，只能顺序追加。
 *   2. 独立记录缓冲（discrete bufs）：csv_init 时 buf 传 NULL，每条记录单独
 *      分配内存；append / serialize / clone / insert / remove / concat 仅在
 *      该模式下可用，单缓冲模式下这些函数会直接报错返回。
 *
 * 四、编解码约定
 *   编码：各字段直接拼接，字段之间与记录末尾分别补逗号和换行。
 *   解码：以逗号切分字段、以换行切分记录；注意本实现不做引号 / 转义处理，
 *   字段内不应包含未转义的逗号或换行。
 *
 * 五、阅读顺序
 *   先看三个结构体与 csv_safe_snprintf，再看 csv_encode 与 csv_decode 主干，
 *   然后是各种 record 操作（insert / remove / concat / clone / serialize），
 *   最后看文件末尾 #ifdef TEST_CSV 下的自测代码。
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zebra.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <unistd.h>
#include "csv.h"

/*
 * DEBUG_E — 错误日志开关：非 0 时 log_error 才会真正打印。
 */
#define DEBUG_E 1
/*
 * DEBUG_V — 详细日志开关：非 0 时 log_verbose 才会真正打印。
 */
#define DEBUG_V 1

/*
 * log_error — 打印带 文件:行号:函数名 前缀的错误日志到 stderr，受 DEBUG_E 控制。
 * 用法同 printf；仅用于本文件内部诊断。
 */
#define log_error(fmt, ...)                                                    \
	do {                                                                   \
		if (DEBUG_E)                                                   \
			fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__,          \
				__LINE__, __func__, ##__VA_ARGS__);            \
	} while (0)

/*
 * csv_safe_snprintf — 安全的 vsnprintf 包装。
 * buf / size：目标缓冲与大小；fmt / ...：格式串与参数。
 * 返回实际写入的字符数（不超过 size-1）；size<=0 时直接返回 0。
 * 副作用：临时关闭 -Wformat-nonliteral 警告（fmt 来自调用方的字面量）。
 */
static inline int csv_safe_snprintf(char *buf, int size, const char *fmt, ...)
{
	va_list args;
	int ret;

	if (size <= 0)
		return 0;

	va_start(args, fmt);
	/* Suppress format-nonliteral warning: This is a legitimate wrapper
	 * function where fmt is passed from callers who use literal strings
	 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	ret = vsnprintf(buf, size, fmt, args);
#pragma GCC diagnostic pop
	va_end(args);

	return MIN(ret, size - 1);
}

/*
 * log_verbose — 与 log_error 类似的详细日志宏，受 DEBUG_V 控制。
 * 注意：该宏的格式串后必须有参数（未使用 ## 拼接），不能只传 fmt。
 */
#define log_verbose(fmt, ...)                                                  \
	do {                                                                   \
		if (DEBUG_V)                                                   \
			fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__,          \
				__LINE__, __func__, __VA_ARGS__);              \
	} while (0)

/*
 * csv_field_t — 单个 CSV 字段。
 * next_field：同记录内下一字段的链表钩子（TAILQ）。
 * field：指向该字段在字符串中的起始位置（不复制内容）。
 * field_len：字段长度（字节）。
 */
struct _csv_field_t_ {
	TAILQ_ENTRY(_csv_field_t_) next_field;
	char *field;
	int field_len;
};

/*
 * csv_record_t — 一条 CSV 记录（一行）。
 * fields：本记录内字段的 TAILQ 链表头。
 * next_record：同 CSV 内下一条记录的链表钩子。
 * record：指向该行整段文本。
 * rec_len：本行文本长度。
 */
struct _csv_record_t_ {
	TAILQ_HEAD(, _csv_field_t_) fields;
	TAILQ_ENTRY(_csv_record_t_) next_record;
	char *record;
	int rec_len;
};

/*
 * csv_t — 一次 CSV 编解码会话。
 * records：所有记录的 TAILQ 链表头。
 * buf：底层字符串缓冲（单缓冲模式使用；独立缓冲模式下为 NULL）。
 * buflen：缓冲容量。
 * csv_len：已编码内容总长度。
 * pointer：顺序写入位置（单缓冲模式下的写指针）。
 * num_recs：记录条数。
 */
struct _csv_t_ {
	TAILQ_HEAD(, _csv_record_t_) records;
	char *buf;
	int buflen;
	int csv_len;
	int pointer;
	int num_recs;
};


/*
 * csvlen — 返回本 CSV 已编码内容的字符总长度。
 */
int csvlen(csv_t *csv)
{
	return (csv->csv_len);
}

/*
 * csv_init — 初始化 CSV 会话并绑定缓冲。
 * csv：NULL 时内部 malloc 一个新的；buf / buflen：底层字符串与容量。
 * 返回会话指针。副作用：清零整个结构、初始化 records 链表。
 * 注意：buf 传 NULL 表示启用"独立记录缓冲"模式，见文件导读。
 */
csv_t *csv_init(csv_t *csv, char *buf, int buflen)
{
	if (csv == NULL) {
		csv = malloc(sizeof(csv_t));
		if (csv == NULL) {
			log_error("CSV Malloc failed\n");
			return NULL;
		}
	}
	memset(csv, 0, sizeof(csv_t));

	csv->buf = buf;
	csv->buflen = buflen;
	TAILQ_INIT(&(csv->records));
	return (csv);
}

/*
 * csv_clean — 清空会话中的所有记录（逐条调用 csv_remove_record 释放）。
 * 副作用：记录链表、num_recs、csv_len、pointer 被一并复位。
 */
void csv_clean(csv_t *csv)
{
	csv_record_t *rec;
	csv_record_t *rec_n;

	rec = TAILQ_FIRST(&(csv->records));
	while (rec != NULL) {
		rec_n = TAILQ_NEXT(rec, next_record);
		csv_remove_record(csv, rec);
		rec = rec_n;
	}
}

/*
 * csv_free — 释放会话结构本身（不释放底层 buf，buf 由调用方管理）。
 */
void csv_free(csv_t *csv)
{
	if (csv != NULL) {
		free(csv);
	}
}

/*
 * csv_init_record — 初始化一条记录：清空其字段链表并把 rec_len 置 0。
 */
static void csv_init_record(csv_record_t *record)
{
	TAILQ_INIT(&(record->fields));
	record->rec_len = 0;
}

/*
 * csv_record_iter — 返回 CSV 的第一条记录（无则 NULL）。
 */
csv_record_t *csv_record_iter(csv_t *csv)
{
	return (TAILQ_FIRST(&(csv->records)));
}

/*
 * csv_record_iter_next — 返回下一条记录；rec 为 NULL 时返回 NULL。
 */
csv_record_t *csv_record_iter_next(csv_record_t *rec)
{
	if (!rec)
		return NULL;
	return (TAILQ_NEXT(rec, next_record));
}

/*
 * csv_field_iter — 取记录内第一个字段，并把字段迭代器写入 *fld。
 * 返回字段字符串；rec 为 NULL 时返回 NULL。
 * 注意：记录无字段时会对空指针 ( *fld ) 解引用，调用前应确认记录含字段。
 */
char *csv_field_iter(csv_record_t *rec, csv_field_t **fld)
{
	if (!rec)
		return NULL;
	*fld = TAILQ_FIRST(&(rec->fields));
	return ((*fld)->field);
}

/*
 * csv_field_iter_next — 推进字段迭代器并返回下一字段字符串；到末尾返回 NULL。
 */
char *csv_field_iter_next(csv_field_t **fld)
{
	*fld = TAILQ_NEXT(*fld, next_field);
	if ((*fld) == NULL) {
		return NULL;
	}
	return ((*fld)->field);
}

/*
 * csv_field_len — 返回字段长度；fld 为 NULL 时返回 0。
 */
int csv_field_len(csv_field_t *fld)
{
	if (fld) {
		return fld->field_len;
	}
	return 0;
}

/*
 * csv_decode_record — 把一条记录文本按逗号切分成字段链表。
 * 逐段用 strpbrk 找逗号建立字段，最后以换行收尾；缺少换行时提前返回。
 * 副作用：为记录分配并挂接 csv_field_t，填充 field 与 field_len。
 * 注意：不做引号 / 转义处理，字段内出现逗号会被误切分。
 */
static void csv_decode_record(csv_record_t *rec)
{
	char *curr = rec->record;
	char *field;
	csv_field_t *fld;

	field = strpbrk(curr, ",");
	while (field != NULL) {
		fld = malloc(sizeof(csv_field_t));
		if (fld) {
			TAILQ_INSERT_TAIL(&(rec->fields), fld, next_field);
			fld->field = curr;
			fld->field_len = field - curr;
		}
		curr = field + 1;
		field = strpbrk(curr, ",");
	}
	field = strstr(curr, "\n");
	if (!field)
		return;

	fld = malloc(sizeof(csv_field_t));
	if (fld) {
		fld->field = curr;
		fld->field_len = field - curr;
		TAILQ_INSERT_TAIL(&(rec->fields), fld, next_field);
	}
}

/*
 * csv_add_field_to_record — 把字符串 col 作为一个字段追加到记录 rec。
 * csv：所属会话（提供缓冲长度）；rec：目标记录；col：字段内容。
 * 返回新字段；内存分配失败返回 NULL。
 * 副作用：在 rec->record 的写入位置拼接 col，并更新 rec_len。
 */
static csv_field_t *csv_add_field_to_record(csv_t *csv, csv_record_t *rec,
					    char *col)
{
	csv_field_t *fld;
	char *str = rec->record;
	int rlen = rec->rec_len;
	int blen = csv->buflen;

	fld = malloc(sizeof(csv_field_t));
	if (!fld) {
		log_error("field malloc failed\n");
		/* more cleanup needed */
		return NULL;
	}
	TAILQ_INSERT_TAIL(&(rec->fields), fld, next_field);
	fld->field = str + rlen;
	fld->field_len = csv_safe_snprintf(str + rlen, blen - rlen, "%s", col);
	rlen += fld->field_len;
	rec->rec_len = rlen;
	return fld;
}

/*
 * csv_encode — 编码一条新记录：把 count 个字符串参数拼成一行并追加。
 * csv：会话；count：字段个数；...：各字段字符串（char *）。
 * 返回新记录指针；失败返回 NULL（并回收已分配资源）。
 * 副作用：写入底层缓冲，更新 rec_len / csv_len / pointer / num_recs。
 * 注意：字段之间自动补逗号，行末补换行；空字段用 NULL 传入。
 */
csv_record_t *csv_encode(csv_t *csv, int count, ...)
{
	int tempc;
	va_list list;
	char *buf = csv->buf;
	int len = csv->buflen;
	int pointer = csv->pointer;
	char *str = NULL;
	char *col;
	csv_record_t *rec;
	csv_field_t *fld;

	if (buf) {
		str = buf + pointer;
	} else {
		/* allocate sufficient buffer */
		str = (char *)malloc(csv->buflen);
		if (!str) {
			log_error("field str malloc failed\n");
			return NULL;
		}
	}

	va_start(list, count);
	rec = malloc(sizeof(csv_record_t));
	if (!rec) {
		log_error("record malloc failed\n");
		if (!buf)
			free(str);
		va_end(list);
		return NULL;
	}
	csv_init_record(rec);
	rec->record = str;
	TAILQ_INSERT_TAIL(&(csv->records), rec, next_record);
	csv->num_recs++;

	/**
	 * Iterate through the fields passed as a variable list and add them
	 */
	for (tempc = 0; tempc < count; tempc++) {
		col = va_arg(list, char *);
		fld = csv_add_field_to_record(csv, rec, col);
		if (!fld) {
			log_error("fld malloc failed\n");
			csv_remove_record(csv, rec);
			va_end(list);
			return NULL;
		}
		if (tempc < (count - 1))
			rec->rec_len += csv_safe_snprintf(str + rec->rec_len, len - rec->rec_len,
							  ",");
	}
	rec->rec_len += csv_safe_snprintf(str + rec->rec_len, len - rec->rec_len, "\n");
	va_end(list);
	csv->csv_len += rec->rec_len;
	csv->pointer += rec->rec_len;
	return (rec);
}

/*
 * csv_num_records — 返回记录条数；csv 为 NULL 时返回 0。
 */
int csv_num_records(csv_t *csv)
{
	if (csv) {
		return csv->num_recs;
	}
	return 0;
}

/*
 * csv_encode_record — 用新值就地覆写一条已编码记录的字段内容。
 * csv：会话；rec：先前由 csv_encode 返回的记录；count / ...：新字段值。
 * 返回 rec；失败返回 NULL。
 * 注意：按原各字段长度逐字节拷贝，新值不应加长字段（不做一致性检查）。
 */
csv_record_t *csv_encode_record(csv_t *csv, csv_record_t *rec, int count, ...)
{
	int tempc;
	va_list list;
	char *str;
	char *col;
	csv_field_t *fld = NULL;
	int i;

	va_start(list, count);
	str = csv_field_iter(rec, &fld);
	if (!fld) {
		va_end(list);
		return NULL;
	}

	for (tempc = 0; tempc < count; tempc++) {
		col = va_arg(list, char *);
		for (i = 0; i < fld->field_len; i++) {
			str[i] = col[i];
		}
		str = csv_field_iter_next(&fld);
	}
	va_end(list);
	return (rec);
}

/*
 * csv_append_record — 向已有记录追加字段；rec 为 NULL 时新建一条记录。
 * 返回该记录；失败或处于单缓冲模式返回 NULL。
 * 注意：仅独立记录缓冲模式可用；会先把行末换行改回逗号再接续字段。
 */
csv_record_t *csv_append_record(csv_t *csv, csv_record_t *rec, int count, ...)
{
	int tempc;
	va_list list;
	int len = csv->buflen, tlen;
	char *str;
	csv_field_t *fld;
	char *col;

	if (csv->buf) {
		/* not only works with discrete bufs */
		return NULL;
	}

	if (!rec) {
		/* create a new rec */
		rec = calloc(1, sizeof(csv_record_t));
		if (!rec) {
			log_error("record malloc failed\n");
			return NULL;
		}
		csv_init_record(rec);
		rec->record = calloc(1, csv->buflen);
		if (!rec->record) {
			log_error("field str malloc failed\n");
			free(rec);
			return NULL;
		}
		csv_insert_record(csv, rec);
	}

	str = rec->record;

	va_start(list, count);

	if (rec->rec_len && (str[rec->rec_len - 1] == '\n'))
		str[rec->rec_len - 1] = ',';

	/**
	 * Iterate through the fields passed as a variable list and add them
	 */
	tlen = rec->rec_len;
	for (tempc = 0; tempc < count; tempc++) {
		col = va_arg(list, char *);
		fld = csv_add_field_to_record(csv, rec, col);
		if (!fld) {
			log_error("fld malloc failed\n");
			break;
		}
		if (tempc < (count - 1))
			rec->rec_len += csv_safe_snprintf(str + rec->rec_len, len - rec->rec_len,
							  ",");
	}
	rec->rec_len += csv_safe_snprintf(str + rec->rec_len, len - rec->rec_len, "\n");
	va_end(list);
	csv->csv_len += (rec->rec_len - tlen);
	csv->pointer += (rec->rec_len - tlen);
	return (rec);
}

/*
 * csv_serialize — 把所有记录顺序序列化写入 msgbuf（容量 msglen）。
 * 成功返回 0；csv / msgbuf 为空或缓冲不足返回 -1。
 * 注意：仅独立记录缓冲模式下有意义。
 */
int csv_serialize(csv_t *csv, char *msgbuf, int msglen)
{
	csv_record_t *rec;
	int offset = 0;

	if (!csv || !msgbuf)
		return -1;

	rec = csv_record_iter(csv);
	while (rec != NULL) {
		if ((offset + rec->rec_len) >= msglen)
			return -1;
		offset += sprintf(&msgbuf[offset], "%s", rec->record);
		rec = csv_record_iter_next(rec);
	}

	return 0;
}

/*
 * csv_clone_record — 克隆记录 in_rec，结果写入 *out_rec。
 * 副作用：为新记录分配内存并解码出字段链表。
 * 注意：仅独立记录缓冲模式可用，且 in_rec 必须属于本 csv；出错时不修改 *out_rec。
 */
void csv_clone_record(csv_t *csv, csv_record_t *in_rec, csv_record_t **out_rec)
{
	char *curr;
	csv_record_t *rec;

	/* first check if rec belongs to this csv */
	if (!csv_is_record_valid(csv, in_rec)) {
		log_error("rec not in this csv\n");
		return;
	}

	/* only works with csv with discrete bufs */
	if (csv->buf) {
		log_error(
			"un-supported for this csv type - single buf detected\n");
		return;
	}

	/* create a new rec */
	rec = calloc(1, sizeof(csv_record_t));
	if (!rec) {
		log_error("record malloc failed\n");
		return;
	}
	csv_init_record(rec);
	curr = calloc(1, csv->buflen);
	if (!curr) {
		log_error("field str malloc failed\n");
		free(rec);
		return;
	}
	rec->record = curr;
	rec->rec_len = in_rec->rec_len;
	strlcpy(rec->record, in_rec->record, csv->buflen);

	/* decode record into fields */
	csv_decode_record(rec);

	*out_rec = rec;
}

/*
 * csv_remove_record — 从 CSV 中删除一条记录及其所有字段并释放内存。
 * 注意：rec 必须属于本 csv；单缓冲模式下不释放底层 buf，仅移除链表节点。
 */
void csv_remove_record(csv_t *csv, csv_record_t *rec)
{
	csv_field_t *fld = NULL, *p_fld;

	/* first check if rec belongs to this csv */
	if (!csv_is_record_valid(csv, rec)) {
		log_error("rec not in this csv\n");
		return;
	}

	/* remove fields */
	csv_field_iter(rec, &fld);
	while (fld) {
		p_fld = fld;
		csv_field_iter_next(&fld);
		TAILQ_REMOVE(&(rec->fields), p_fld, next_field);
		free(p_fld);
	}

	TAILQ_REMOVE(&(csv->records), rec, next_record);

	csv->num_recs--;
	csv->csv_len -= rec->rec_len;
	csv->pointer -= rec->rec_len;
	if (!csv->buf)
		free(rec->record);
	free(rec);
}

/*
 * csv_insert_record — 把一条记录插入 CSV 尾部。
 * 注意：仅独立记录缓冲模式可用；rec 不能已在 csv 中；超出缓冲容量时拒绝插入。
 */
void csv_insert_record(csv_t *csv, csv_record_t *rec)
{
	/* first check if rec already in csv */
	if (csv_is_record_valid(csv, rec)) {
		log_error("rec already in this csv\n");
		return;
	}

	/* we can only insert records if no buf was supplied during csv init */
	if (csv->buf) {
		log_error(
			"un-supported for this csv type - single buf detected\n");
		return;
	}

	/* do we go beyond the max buf set for this csv ?*/
	if ((csv->csv_len + rec->rec_len) > csv->buflen) {
		log_error("cannot insert - exceeded buf size\n");
		return;
	}

	TAILQ_INSERT_TAIL(&(csv->records), rec, next_record);
	csv->num_recs++;
	csv->csv_len += rec->rec_len;
	csv->pointer += rec->rec_len;
}

/*
 * csv_concat_record — 把 rec1 与 rec2 拼成一条新记录并返回；rec1 / rec2 会被移除。
 * 失败返回 NULL（出错时通过 goto 回收已分配的临时内存）。
 * 注意：仅独立记录缓冲模式可用；两条记录都须属于本 csv 且以换行结尾。
 */
csv_record_t *csv_concat_record(csv_t *csv, csv_record_t *rec1,
				csv_record_t *rec2)
{
	char *curr;
	char *ret;
	csv_record_t *rec;

	/* first check if rec1 and rec2 belong to this csv */
	if (!csv_is_record_valid(csv, rec1)
	    || !csv_is_record_valid(csv, rec2)) {
		log_error("rec1 and/or rec2 invalid\n");
		return NULL;
	}

	/* we can only concat records if no buf was supplied during csv init */
	if (csv->buf) {
		log_error(
			"un-supported for this csv type - single buf detected\n");
		return NULL;
	}

	/* create a new rec */
	rec = calloc(1, sizeof(csv_record_t));
	if (!rec) {
		log_error("record malloc failed\n");
		return NULL;
	}
	csv_init_record(rec);

	curr = (char *)calloc(1, csv->buflen);
	if (!curr) {
		log_error("field str malloc failed\n");
		goto out_rec;
	}
	rec->record = curr;

	/* concat the record string */
	ret = strstr(rec1->record, "\n");
	if (!ret) {
		log_error("rec1 str not properly formatted\n");
		goto out_curr;
	}

	snprintf(curr, (int)(ret - rec1->record + 1), "%s", rec1->record);
	strlcat(curr, ",", csv->buflen);

	ret = strstr(rec2->record, "\n");
	if (!ret) {
		log_error("rec2 str not properly formatted\n");
		goto out_curr;
	}

	snprintf((curr + strlen(curr)), (int)(ret - rec2->record + 1), "%s",
		 rec2->record);
	strlcat(curr, "\n", csv->buflen);
	rec->rec_len = strlen(curr);

	/* paranoia */
	assert(csv->buflen
	       > (csv->csv_len - rec1->rec_len - rec2->rec_len + rec->rec_len));

	/* decode record into fields */
	csv_decode_record(rec);

	/* now remove rec1 and rec2 and insert rec into this csv */
	csv_remove_record(csv, rec1);
	csv_remove_record(csv, rec2);
	csv_insert_record(csv, rec);

	return rec;

out_curr:
	free(curr);
out_rec:
	free(rec);
	return NULL;
}

/*
 * csv_decode — 解码一段 CSV 文本为记录 / 字段链表。
 * csv：会话；inbuf：待解码文本（NULL 则改用 csv->buf，单缓冲模式常用）。
 * 副作用：按换行切分记录、逐条解码字段，更新 num_recs。
 * 注意：单缓冲模式下 rec->record 直接指向共享 buf，独立模式则各自拷贝一份。
 */
void csv_decode(csv_t *csv, char *inbuf)
{
	char *buf;
	char *pos;
	csv_record_t *rec;

	buf = (inbuf) ? inbuf : csv->buf;
	assert(buf);

	pos = strpbrk(buf, "\n");
	while (pos != NULL) {
		rec = calloc(1, sizeof(csv_record_t));
		if (!rec)
			return;
		csv_init_record(rec);
		TAILQ_INSERT_TAIL(&(csv->records), rec, next_record);
		csv->num_recs++;
		if (csv->buf)
			rec->record = buf;
		else {
			rec->record = calloc(1, csv->buflen);
			if (!rec->record) {
				log_error("field str malloc failed\n");
				return;
			}
			memcpy(rec->record, buf, MIN(pos - buf + 1, csv->buflen - 1));
		}
		rec->rec_len = pos - buf + 1;
		/* decode record into fields */
		csv_decode_record(rec);
		buf = pos + 1;
		pos = strpbrk(buf, "\n");
	}
}

/*
 * csv_is_record_valid — 判断记录 in_rec 是否属于 csv。返回 1 有效 / 0 无效。
 */
int csv_is_record_valid(csv_t *csv, csv_record_t *in_rec)
{
	csv_record_t *rec;
	int valid = 0;

	rec = csv_record_iter(csv);
	while (rec) {
		if (rec == in_rec) {
			valid = 1;
			break;
		}
		rec = csv_record_iter_next(rec);
	}

	return valid;
}

/*
 * csv_dump — 把解码后每条记录的每个字段打印到 stderr（调试用）。
 */
void csv_dump(csv_t *csv)
{
	csv_record_t *rec;
	csv_field_t *fld;
	char *str;

	rec = csv_record_iter(csv);
	while (rec != NULL) {
		str = csv_field_iter(rec, &fld);
		while (str != NULL) {
			fprintf(stderr, "%s\n", str);
			str = csv_field_iter_next(&fld);
		}
		rec = csv_record_iter_next(rec);
	}
}

/*
 * 自测区：以下代码仅在编译时定义 TEST_CSV 宏才会被纳入，用于手工验证 CSV
 * 编解码与内存使用，正常构建下不参与编译。
 */
#ifdef TEST_CSV

/*
 * get_memory_usage — 读取 /proc/<pid>/status，返回该进程 VmData 与 VmStk 之和
 * （近似内存占用，仅 Linux）。打开失败返回 -1。
 * 仅自测使用。
 */
static int get_memory_usage(pid_t pid)
{
	int fd, data, stack;
	char buf[4096], status_child[PATH_MAX];
	char *vm;

	snprintf(status_child, sizeof(status_child), "/proc/%d/status", pid);
	fd = open(status_child, O_RDONLY);
	if (fd < 0)
		return -1;

	read(fd, buf, 4095);
	buf[4095] = '\0';
	close(fd);

	data = stack = 0;

	vm = strstr(buf, "VmData:");
	if (vm) {
		sscanf(vm, "%*s %d", &data);
	}
	vm = strstr(buf, "VmStk:");
	if (vm) {
		sscanf(vm, "%*s %d", &stack);
	}

	return data + stack;
}

/*
 * main — 自测入口：依次演示编码、覆写记录、清理、解码与打印，并打印内存占用。
 * 仅当定义 TEST_CSV 时参与编译。
 */
int main()
{
	char buf[10000];
	csv_t csv;
	int i;
	csv_record_t *rec;
	char hdr1[32], hdr2[32];

	log_verbose("Mem: %d\n", get_memory_usage(getpid()));
	csv_init(&csv, buf, 256);
	snprintf(hdr1, sizeof(hdr1), "%4d", 0);
	snprintf(hdr2, sizeof(hdr2), "%4d", 1);
	log_verbose("(%zu/%zu/%d/%d)\n", strlen(hdr1), strlen(hdr2), atoi(hdr1),
		    atoi(hdr2));
	rec = csv_encode(&csv, 2, hdr1, hdr2);
	csv_encode(&csv, 4, "name", "age", "sex", "hei");
	csv_encode(&csv, 3, NULL, "0", NULL);
	csv_encode(&csv, 2, "p", "35");
	for (i = 0; i < 50; i++) {
		csv_encode(&csv, 2, "p", "10");
	}
	csv_encode(&csv, 2, "pdfadfadfadsadsaddfdfdsfdsd", "35444554545454545");
	log_verbose("%s\n", buf);
	snprintf(hdr1, sizeof(hdr1), "%4d", csv.csv_len);
	snprintf(hdr2, sizeof(hdr2), "%4d", 1);
	log_verbose("(%zu/%zu/%d/%d)\n", strlen(hdr1), strlen(hdr2), atoi(hdr1),
		    atoi(hdr2));
	rec = csv_encode_record(&csv, rec, 2, hdr1, hdr2);
	log_verbose("(%d/%d)\n%s\n", rec->rec_len, csv.csv_len, buf);

	log_verbose("Mem: %d\n", get_memory_usage(getpid()));
	csv_clean(&csv);
	log_verbose("Mem: %d\n", get_memory_usage(getpid()));
	csv_init(&csv, buf, 256);
	csv_decode(&csv, NULL);
	log_verbose("%s", "AFTER DECODE\n");
	csv_dump(&csv);
	csv_clean(&csv);
	log_verbose("Mem: %d\n", get_memory_usage(getpid()));
}
#endif

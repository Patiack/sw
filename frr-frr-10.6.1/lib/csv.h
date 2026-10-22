// SPDX-License-Identifier: GPL-2.0-or-later
/* CSV
 * Copyright (C) 2013 Cumulus Networks, Inc.
 */

/*
 * 文件导读：csv.h —— 逗号分隔值（CSV）编解码接口
 *
 * 一、这个文件做什么
 *   提供把一组字段编码成 CSV 字符串、以及把 CSV 字符串解码成记录 / 字段链表的
 *   接口。FRR 主要用于进程间 / 节点间传递结构化消息（例如 ldpd 的标签映射消息、
 *   部分内部 IPC），把变长字段拼成一行文本。
 *
 * 二、核心数据结构
 *   - csv_t：一次编解码会话，持有字符串缓冲等信息。
 *   - csv_record_t：一条记录（一行），内部是字段的链表。
 *   - csv_field_t：单个字段，指向所属字符串中的某一段。
 *   三者关系：csv_t 含多条 record，每条 record 含多个 field。
 *
 * 三、使用方式（编码 / 解码）
 *   编码：csv_init 初始化 -> csv_encode 追加记录 -> csv_encode_record 覆写已
 *         编码记录 -> csv_clean 收尾。
 *   解码：csv_init -> csv_decode -> 用 csv_record_iter / csv_field_iter 遍历。
 *
 * 四、已知限制（重要）
 *   部分接口（append / serialize / clone / insert / remove / concat）仅在
 *   "每条记录使用独立缓冲"（discrete record bufs）时有效，普通连续缓冲下不可用。
 *
 * 五、阅读顺序
 *   先看三个 typedef，再看 init / encode / decode 主干，最后看各种 record 操作
 *   与遍历接口。实现见 lib/csv.c。
 */

#ifndef __CSV_H__
#define __CSV_H__

/*
 * CSV encoding and decoding routines.
 *
 * Example:
 * Encoding side:
 *
 *      csv_t *csv;
 *      csv_record_t *fstrec;
 *      csv_record_t *rec;
 *      char  buf[BUFSIZ];
 *
 *      csv = csv_init(csv, buf, BUFSIZ);
 *      ...
 *      fstrec = csv_encode(csv, 2, "hello", "world");
 *      rec = csv_encode(csv, 2, "foo", "bar");
 *      ...
 *      fstrec = csv_encode_record(csv, fstrec, 2, "HELLO", "WORLD");
 *      ...
 *      csv_clean(csv);
 *
 * Decoding side:
 *
 *      csv_t *csv;
 *      csv_record_t *rec;
 *      csv_field_t *fld;
 *      char  *rcvdbuf;
 *
 *      csv = csv_init(csv, rcvdbuf, BUFSIZ);
 *      ...
 *      csv_decode(csv);
 *      csv_dump(csv);
 *
 *      for (rec = csv_record_iter(csv); rec;
 *           rec = csv_record_iter_next(rec)) {
 *          ...
 *          for (str = csv_field_iter(rec, &fld); str;
 *               str = csv_field_iter_next(&fld)) {
 *              ...
 *          }
 *      }
 *      ...
 *      csv_clean(csv);
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 三个不透明句柄的前向声明：字段、记录、会话。
 * 具体结构定义在 csv.c 中，外部只使用这些 typedef 指针。
 */
typedef struct _csv_field_t_ csv_field_t;
typedef struct _csv_record_t_ csv_record_t;
typedef struct _csv_t_ csv_t;

/*
 * csv_init — 初始化 CSV 会话；csv 为 NULL 时分配一个，并绑定到字符串缓冲
 * （buf / buflen）。返回会话指针（可返回传入的 csv 或新建对象）。
 */
/**
 * Initialize the CSV structure (if necessary, allocate first). Point to
 * the passed string buffer.
 */
csv_t *csv_init(csv_t *csv, char *buf, int buflen);

/*
 * csv_encode — 把 count 个可变参数字段编码追加为一条新记录。
 * 返回新记录指针（记录指针指向其字段所在字符串）。
 */
/**
 * Encode the variable list of arguments as CSV fields. The csv structure
 * should have been initialized (with the string buffer). The fields get
 * concatenated into the string.
 */
csv_record_t *csv_encode(csv_t *csv, int count, ...);

/*
 * csv_encode_record — 就地覆写一条已编码记录（rec 必须来自先前的 csv_encode）。
 * 字段数与各字段长度须与首次编码一致，不做一致性检查。
 * 典型用途：所有记录编码完成后，回头改写消息头记录（如填入总长度）。
 */
/**
 * Encode the variable list arguments into an existing record, essentially
 * overwriting the record. No checking is done for consistency. The number
 * of fields should be the same as what was encoded and the length of each
 * field should also be the same as what was encoded before. The "rec"
 * parameter should be the same as what was returned from a previous call
 * to csv_encode().
 *
 * Useful for message encoding/decoding that get passed around between
 * processes/nodes - e.g. the message header record can be rewritten AFTER
 * encoding all other records, with new information such as total length.
 */
csv_record_t *csv_encode_record(csv_t *csv, csv_record_t *rec, int count, ...);

/*
 * csv_decode — 解码一段 CSV 字符串：构造记录链表，每条记录再构造字段链表。
 * 记录指向其所在字符串，字段指向字段字符串本身。inbuf 为要解码的输入。
 */
/**
 * Decode a CSV formatted string. The csv structure should have been
 * initialized (with the string). The function creates a LIST of records
 * (csv_record_t structure) where each record is in turn a LIST of fields
 * (csv_field_t structure). The record points to the string containing the
 * list of fields. Similarly, the field points to the field string.
 * NB: csv initialized for discrete buf , caller will pass inbuf
 */
void csv_decode(csv_t *csv, char *inbuf);

/*
 * csv_dump — 把解码后的所有字段打印到 stderr（调试用）。
 */
/**
 * Dump all fields of a decoded CSV to stderr
 */
void csv_dump(csv_t *csv);

/*
 * csvlen — 返回已编码进本 CSV 的所有字符总长度。
 */
/**
 * Total length of all characters encoded in the CSV.
 */
int csvlen(csv_t *csv);

/*
 * csv_clean — 复位会话（清空记录 / 字段链表等），保留已初始化的缓冲以便复用。
 * csv_free — 释放会话占用的动态内存。
 */
void csv_clean(csv_t *csv);
void csv_free(csv_t *csv);

/*
 * 遍历接口：csv_record_iter 取第一条记录，csv_record_iter_next 取下一条；
 * csv_field_iter 取记录内第一个字段（并把迭代器写入 fld），
 * csv_field_iter_next 取下一个字段。返回字段字符串，NULL 表示遍历结束。
 */
/**
 * Iterate through the records and fields of an encoded/decoded CSV.
 */
csv_record_t *csv_record_iter(csv_t *csv);
csv_record_t *csv_record_iter_next(csv_record_t *rec);
char *csv_field_iter(csv_record_t *rec, csv_field_t **fld);
char *csv_field_iter_next(csv_field_t **fld);

/*
 * csv_field_len — 返回某字段的长度（字节数）。
 */
/**
 * Return the length of field
 */
int csv_field_len(csv_field_t *fld);

/*
 * csv_is_record_valid — 判断某条记录是否属于给定 CSV 会话。返回非 0 表示有效。
 */
/**
 * Checks to see if a record belongs to a csv
 */
int csv_is_record_valid(csv_t *csv, csv_record_t *in_rec);

/*
 * csv_concat_record — 把 rec1 与 rec2 两条记录拼接成一条新记录。
 * 返回新记录；rec1、rec2 会被移除（其结果字段来自两者）。
 * 仅在记录使用独立缓冲时可用。
 */
/**
 * concat two records in a csv
 * Returns the newly formed record which includes fields from rec1 and rec2
 * rec1 and rec2 are removed
 */
csv_record_t *csv_concat_record(csv_t *csv, csv_record_t *rec1,
				csv_record_t *rec2);

/*
 * csv_remove_record — 从 CSV 中移除一条记录。仅在记录使用独立缓冲时可用。
 */
/**
 * Remove a record from csv
 * Only works when csv has discrete record bufs
 */
void csv_remove_record(csv_t *csv, csv_record_t *rec);

/*
 * csv_insert_record — 把一条记录插入 CSV。仅在记录使用独立缓冲时可用。
 */
/**
 * Insert a record into csv
 * Only works when csv has discrete record bufs
 */
void csv_insert_record(csv_t *csv, csv_record_t *rec);

/*
 * csv_append_record — 向已有记录追加 count 个字段，返回该记录。
 * 仅在记录使用独立缓冲时可用。
 */
/**
 * append fields to a record
 * Only works when csv has discrete record bufs
 */
csv_record_t *csv_append_record(csv_t *csv, csv_record_t *rec, int count, ...);

/*
 * csv_serialize — 把整个 CSV 内容序列化写进 msgbuf（上限 msglen），返回写入长度。
 * 仅在记录使用独立缓冲时可用。
 */
/**
 * Serialize contents of csv into string
 * Only works when csv has discrete record bufs
 */
int csv_serialize(csv_t *csv, char *msgbuf, int msglen);

/*
 * csv_clone_record — 克隆一条记录到 out_rec。仅在记录使用独立缓冲时可用。
 */
/**
 * Clone a record.
 * Only works when csv has discrete record bufs
 */
void csv_clone_record(csv_t *csv, csv_record_t *in_rec, csv_record_t **out_rec);

/*
 * csv_num_records — 返回会话中的记录条数。
 */
/**
 * Return number of records
 */
int csv_num_records(csv_t *csv);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: GPL-2.0-or-later
/* PTM Library
 * Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * ============================================================================
 * 【文件导读】lib/ptm_lib.c —— PTM 客户端库实现（消息编解码 + 收发）
 * ============================================================================
 * 职责：
 *   实现 lib/ptm_lib.h 的接口。核心是把结构化消息在“内存中的 CSV 对象”
 *   与“线上文本字节流”之间来回转换，并负责从 socket 读取、按消息类型
 *   分派到用户回调。客户端（如 bgpd）用它在 Unix socket 上与 ptmd 通信。
 *
 * 消息模型：
 *   一条 PTM 消息在内存里表示为一个 CSV 对象（csv_t），通常含两条记录：
 *     - 表头记录（header）：若干定宽字段 + 若干 key（列名）。
 *     - 数据记录（data）：与 key 一一对应的 value。
 *   表头前 5 个字段固定为 msglen / version / type / cmd_id / client_name，
 *   它们构成 37 字节的固定消息头（PTMLIB_MSG_HDR_LEN）。
 *
 * 核心数据结构：
 *   ptm_lib_msg_ctxt_t —— 消息上下文：csv 对象 + cmd_id + 消息类型。
 *     上层回调拿到的就是它（以 void * 传递）。
 *
 * 与其他模块的关系：
 *   - 依赖 lib/csv.h（CSV 编解码）、lib/zebra.h。
 *   - 与 lib/ptm_lib.h 成对。
 *   - 使用 recv 从 socket 读数据，用 usleep 做重试退避。
 *
 * 阅读顺序：
 *   1. 顶层调试开关与 ERRLOG / DLOG 宏。
 *   2. 消息上下文结构体 ptm_lib_msg_ctxt_t。
 *   3. 编解码内核：_ptm_lib_encode_header、_ptm_lib_decode_header。
 *   4. 上层 API：append / init / complete / cleanup / find_key。
 *   5. 收发：_ptm_lib_read_ptm_socket、ptm_lib_process_msg。
 *   6. 生命周期：register / deregister。
 *
 * 易踩的坑：
 *   - 表头字段用 %4d 定宽编码，解析时再 atoi；长度字段与实际长度需自洽，
 *     否则对端解析会错位。
 *   - client_name 定宽 16 字节，编码用 %16.16s 截断，解码时手动去空格。
 *   - complete_msg / cleanup_msg 会 free 上下文，调用后不可再用。
 *   - 兼容“遗留命令”：非 CSV 的裸字符串只识别 get-status。
 *   - _ptm_lib_read_ptm_socket 用非阻塞 recv 加重试，可能读不满就返回。
 * ============================================================================
 */

#include <zebra.h>

/* 标准库与系统头：stdio 用于 fprintf / snprintf，string 用于 memcpy 等，
 * ctype 提供 isspace，unistd 提供 usleep，sys/socket.h 提供 recv。
 * csv.h 为消息体的 CSV 编解码，ptm_lib.h 为本模块接口。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include "csv.h"
#include "ptm_lib.h"

/* DEBUG_E：错误日志开关（0 关闭）。为 1 时 ERRLOG 才真正打印。 */
#define DEBUG_E 0
/* DEBUG_V：调试日志开关（0 关闭）。为 1 时 DLOG 才真正打印。 */
#define DEBUG_V 0

/* ERRLOG：错误级日志宏。仅在 DEBUG_E 非 0 时经 fprintf 输出到 stderr，
 * 前缀为“文件:行号:函数名”；变参可省略（##__VA_ARGS__）。
 */
#define ERRLOG(fmt, ...)                                                       \
	do {                                                                   \
		if (DEBUG_E)                                                   \
			fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__,          \
				__LINE__, __func__, ##__VA_ARGS__);            \
	} while (0)

/* DLOG：调试级日志宏，行为同 ERRLOG，但受 DEBUG_V 控制。 */
#define DLOG(fmt, ...)                                                         \
	do {                                                                   \
		if (DEBUG_V)                                                   \
			fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__,          \
				__LINE__, __func__, ##__VA_ARGS__);            \
	} while (0)

/* ptm_lib_msg_ctxt_t：一条 PTM 消息的运行时上下文。
 *   cmd_id —— 命令号，标识是哪条命令 / 响应对。
 *   csv    —— 承载消息内容的 CSV 对象（表头记录 + 数据记录）。
 *   type   —— 消息类型（通知 / 命令 / 响应）。
 * 该结构以 void * 在回调之间传递，由本文件内部解读其具体字段。
 */
typedef struct ptm_lib_msg_ctxt_s {
	int cmd_id;
	csv_t *csv;
	ptmlib_msg_type type;
} ptm_lib_msg_ctxt_t;

/* _ptm_lib_encode_header：把消息头 5 个字段编码进 CSV。
 * 字段依次为 msglen / version / type / cmd_id / client_name，均为定宽：
 * 前四个用 %4d，客户端名用 %16.16s 截断。
 * rec 非空时在该记录上续写（用于更新已有表头），否则新建记录。
 * 返回编码后的记录指针。
 */
static csv_record_t *_ptm_lib_encode_header(csv_t *csv, csv_record_t *rec,
					    int msglen, int version, int type,
					    int cmd_id, char *client_name)
{
	char msglen_buf[16], vers_buf[16], type_buf[16], cmdid_buf[16];
	char client_buf[32];
	csv_record_t *rec1;

	snprintf(msglen_buf, sizeof(msglen_buf), "%4d", msglen);
	snprintf(vers_buf, sizeof(vers_buf), "%4d", version);
	snprintf(type_buf, sizeof(type_buf), "%4d", type);
	snprintf(cmdid_buf, sizeof(cmdid_buf), "%4d", cmd_id);
	snprintf(client_buf, 17, "%16.16s", client_name);
	if (rec) {
		rec1 = csv_encode_record(csv, rec, 5, msglen_buf, vers_buf,
					 type_buf, cmdid_buf, client_buf);
	} else {
		rec1 = csv_encode(csv, 5, msglen_buf, vers_buf, type_buf,
				  cmdid_buf, client_buf);
	}
	return (rec1);
}

/* _ptm_lib_decode_header：从 CSV 里解析出消息头的 5 个字段。
 * 依次取 msglen / version / type / cmd_id（用 atoi 转换），最后把
 * client_name 的定宽字段去掉空白后拷入出参。
 * 任一步骤记录 / 字段缺失（NULL）都返回 -1，成功返回 0。
 */
static int _ptm_lib_decode_header(csv_t *csv, int *msglen, int *version,
				  int *type, int *cmd_id, char *client_name)
{
	char *hdr;
	csv_record_t *rec;
	csv_field_t *fld;
	int i, j;

	csv_decode(csv, NULL);
	rec = csv_record_iter(csv);
	if (rec == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	hdr = csv_field_iter(rec, &fld);
	if (hdr == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	*msglen = atoi(hdr);
	hdr = csv_field_iter_next(&fld);
	if (hdr == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	*version = atoi(hdr);
	hdr = csv_field_iter_next(&fld);
	if (hdr == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	*type = atoi(hdr);
	hdr = csv_field_iter_next(&fld);
	if (hdr == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	*cmd_id = atoi(hdr);
	hdr = csv_field_iter_next(&fld);
	if (hdr == NULL) {
		DLOG("malformed CSV\n");
		return -1;
	}
	/* 去掉客户端名定宽字段里的所有空白字符（含前导空格）。 */
	/* remove leading spaces */
	for (i = j = 0; i < csv_field_len(fld); i++) {
		if (!isspace((unsigned char)hdr[i])) {
			client_name[j] = hdr[i];
			j++;
		}
	}
	client_name[j] = '\0';

	return 0;
}

/* ptm_lib_append_msg：向消息追加一个 key / value。
 * 先在表头记录追加 key，再在其后的数据记录追加 value，最后重算消息头
 * （msglen = 当前 CSV 长度减去固定头长度 PTMLIB_MSG_HDR_LEN）。
 * 上下文为空或任一次追加失败返回 -1，成功返回 0。
 */
int ptm_lib_append_msg(ptm_lib_handle_t *hdl, void *ctxt, const char *key,
		       const char *val)
{
	ptm_lib_msg_ctxt_t *p_ctxt = ctxt;
	csv_t *csv;
	csv_record_t *mh_rec, *rec;

	if (!p_ctxt) {
		ERRLOG("%s: no context \n", __func__);
		return -1;
	}

	csv = p_ctxt->csv;
	mh_rec = csv_record_iter(csv);
	rec = csv_record_iter_next(mh_rec);

	/* 把 key 追加到表头记录（列名行）。 */
	/* append to the hdr record */
	rec = csv_append_record(csv, rec, 1, key);
	if (!rec) {
		ERRLOG("%s: Could not append key \n", __func__);
		return -1;
	}

	rec = csv_record_iter_next(rec);
	/* 把 value 追加到数据记录，与上面的 key 列对齐。 */
	/* append to the data record */
	rec = csv_append_record(csv, rec, 1, val);
	if (!rec) {
		ERRLOG("%s: Could not append val \n", __func__);
		return -1;
	}

	/* 内容变了，重算并回填消息头（尤其是长度字段）。 */
	/* update the msg hdr */
	_ptm_lib_encode_header(csv, mh_rec, (csvlen(csv) - PTMLIB_MSG_HDR_LEN),
			       PTMLIB_MSG_VERSION, p_ctxt->type, p_ctxt->cmd_id,
			       hdl->client_name);

	return 0;
}

/* ptm_lib_init_msg：初始化一条新消息。
 * 分配一个 CSV（离散记录缓冲，容量 PTMLIB_MSG_SZ），写入初始消息头，
 * 再分配 ptm_lib_msg_ctxt_t 记录 cmd_id / type，并通过 out_ctxt 返回。
 * 若入参 in_ctxt 非空，则克隆其表头与数据两条记录作为新消息初值。
 * 任一步分配失败都会清理并返回 -1；成功返回 0。
 */
int ptm_lib_init_msg(ptm_lib_handle_t *hdl, int cmd_id, int type, void *in_ctxt,
		     void **out_ctxt)
{
	ptm_lib_msg_ctxt_t *p_ctxt;
	ptm_lib_msg_ctxt_t *p_in_ctxt = in_ctxt;
	csv_t *csv;
	csv_record_t *rec, *d_rec;

	/* 初始化 CSV 对象，使用“离散记录缓冲”模式（每条记录单独一块内存）。 */
	/* Initialize csv for using discrete record buffers */
	csv = csv_init(NULL, NULL, PTMLIB_MSG_SZ);

	if (!csv) {
		ERRLOG("%s: Could not allocate csv \n", __func__);
		return -1;
	}

	rec = _ptm_lib_encode_header(csv, NULL, 0, PTMLIB_MSG_VERSION, type,
				     cmd_id, hdl->client_name);

	if (!rec) {
		ERRLOG("%s: Could not allocate record \n", __func__);
		csv_clean(csv);
		csv_free(csv);
		return -1;
	}

	p_ctxt = calloc(1, sizeof(*p_ctxt));
	if (!p_ctxt) {
		ERRLOG("%s: Could not allocate context \n", __func__);
		csv_clean(csv);
		csv_free(csv);
		return -1;
	}

	p_ctxt->csv = csv;
	p_ctxt->cmd_id = cmd_id;
	p_ctxt->type = type;

	*(ptm_lib_msg_ctxt_t **)out_ctxt = p_ctxt;

	/* 调用者是否提供了一个待克隆的上下文（如把收到命令改造为响应）？ */
	/* caller supplied a context to initialize with? */
	if (p_in_ctxt) {
		/* 克隆并插入表头记录。 */
		/* insert the hdr rec */
		rec = csv_record_iter(p_in_ctxt->csv);
		csv_clone_record(p_in_ctxt->csv, rec, &d_rec);
		csv_insert_record(csv, d_rec);
		/* 克隆并插入数据记录。 */
		/* insert the data rec */
		rec = csv_record_iter_next(rec);
		csv_clone_record(p_in_ctxt->csv, rec, &d_rec);
		csv_insert_record(csv, d_rec);
	}
	return 0;
}

/* ptm_lib_cleanup_msg：释放消息上下文：先清空并释放 CSV，再释放上下文。
 * 上下文为空返回 -1，否则返回 0。
 */
int ptm_lib_cleanup_msg(ptm_lib_handle_t *hdl, void *ctxt)
{
	ptm_lib_msg_ctxt_t *p_ctxt = ctxt;
	csv_t *csv;

	if (!p_ctxt) {
		ERRLOG("%s: no context \n", __func__);
		return -1;
	}

	csv = p_ctxt->csv;

	csv_clean(csv);
	csv_free(csv);
	free(p_ctxt);

	return 0;
}

/* ptm_lib_complete_msg：完成消息。
 * 先回填消息头长度（CSV 长度减固定头长度），再（若提供 buf 与 len）
 * 把 CSV 序列化成字符串并回写实际长度 len；随后释放上下文。
 * 注意：与 cleanup 一样会释放上下文，调用后不可再使用。
 */
int ptm_lib_complete_msg(ptm_lib_handle_t *hdl, void *ctxt, char *buf, int *len)
{
	ptm_lib_msg_ctxt_t *p_ctxt = ctxt;
	csv_t *csv;
	csv_record_t *rec;

	if (!p_ctxt) {
		ERRLOG("%s: no context \n", __func__);
		return -1;
	}

	csv = p_ctxt->csv;
	rec = csv_record_iter(csv);

	_ptm_lib_encode_header(csv, rec, (csvlen(csv) - PTMLIB_MSG_HDR_LEN),
			       PTMLIB_MSG_VERSION, p_ctxt->type, p_ctxt->cmd_id,
			       hdl->client_name);

	/* 把 CSV 内容序列化成可直接写往 socket 的字符串。 */
	/* parse csv contents into string */
	if (buf && len) {
		if (csv_serialize(csv, buf, *len)) {
			ERRLOG("%s: cannot serialize\n", __func__);
			return -1;
		}
		*len = csvlen(csv);
	}

	csv_clean(csv);
	csv_free(csv);
	free(p_ctxt);

	return 0;
}

/* ptm_lib_find_key_in_msg：按 key 查找 value。
 * 若 CSV 记录数大于 2，说明带 PTM 头，先跳过第一条记录；之后第一条是
 * key 记录、下一条是 data 记录，两条记录并行遍历，用 key 的长度做
 * strncmp 比较（兼容定宽尾随空格）。命中即把 value 拷进 val 并返回 0，
 * 未命中返回 -1。
 */
int ptm_lib_find_key_in_msg(void *ctxt, const char *key, char *val)
{
	ptm_lib_msg_ctxt_t *p_ctxt = ctxt;
	csv_t *csv = p_ctxt->csv;
	csv_record_t *hrec, *drec;
	csv_field_t *hfld, *dfld;
	char *hstr, *dstr;

	/* 若存在 PTM 消息头，先跳过它：真正的 key 与 data 记录在其后。 */
	/**
	 * skip over ptm hdr if present
	 * The next hdr is the keys (column name)
	 * The next hdr is the data
	 */
	if (csv_num_records(csv) > 2) {
		hrec = csv_record_iter(csv);
		hrec = csv_record_iter_next(hrec);
	} else {
		hrec = csv_record_iter(csv);
	}
	drec = csv_record_iter_next(hrec);
	val[0] = '\0';
	for (hstr = csv_field_iter(hrec, &hfld),
	    dstr = csv_field_iter(drec, &dfld);
	     (hstr && dstr); hstr = csv_field_iter_next(&hfld),
	    dstr = csv_field_iter_next(&dfld)) {
		if (!strncmp(hstr, key, csv_field_len(hfld))) {
			snprintf(val, csv_field_len(dfld) + 1, "%s", dstr);
			return 0;
		}
	}

	return -1;
}

/* _ptm_lib_read_ptm_socket：从 fd 尽力读满 len 字节。
 * 使用非阻塞 recv：遇 EAGAIN / EWOULDBLOCK 或读空时，最多重试 2 次
 * （每次间隔 10ms）；致命错误返回负值。返回实际读到的字节数，可能小于
 * len（对端数据尚未到齐）。
 */
static int _ptm_lib_read_ptm_socket(int fd, char *buf, int len)
{
	int retries = 0, rc;
	int bytes_read = 0;

	while (bytes_read != len) {
		rc = recv(fd, (void *)(buf + bytes_read), (len - bytes_read),
			  MSG_DONTWAIT);
		if (rc < 0 && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
			ERRLOG("fatal recv error(%s), closing connection, rc %d\n", strerror(errno),
			       rc);
			return (rc);
		} else if (rc <= 0) {
			if (retries++ < 2) {
				usleep(10000);
				continue;
			}
			DLOG("max retries - recv error(%d - %s) bytes read %d (%d)\n", errno,
			     strerror(errno), bytes_read, len);
			return (bytes_read);
		} else {
			bytes_read += rc;
		}
	}

	return bytes_read;
}

/* ptm_lib_process_msg：读取并处理一条 PTM 消息（本库的核心入口）。
 * 流程：
 *   1. 先读固定头 PTMLIB_MSG_HDR_LEN 字节，尝试按 CSV 解析消息头。
 *   2. 若解析失败，可能是“遗留纯文本命令”：再读剩余部分，仅支持
 *      get-status，命中则内部构造一条等价的 CSV 命令。
 *   3. 若解析成功，按头里的 msglen 读满整条消息并解码成 CSV 上下文。
 *   4. 依 type 分派到 notify_cb / cmd_cb / response_cb。
 *   5. 处理完释放上下文，返回本次读取的字节数；出错返回负值。
 */
int ptm_lib_process_msg(ptm_lib_handle_t *hdl, int fd, char *inbuf, int inlen,
			void *arg)
{
	int rc, len;
	char client_name[32];
	int cmd_id = 0, type = 0, ver = 0, msglen = 0;
	csv_t *csv;
	ptm_lib_msg_ctxt_t *p_ctxt = NULL;

	len = _ptm_lib_read_ptm_socket(fd, inbuf, PTMLIB_MSG_HDR_LEN);
	if (len <= 0)
		return (len);

	csv = csv_init(NULL, inbuf, PTMLIB_MSG_HDR_LEN);

	if (!csv) {
		DLOG("Cannot allocate csv for hdr\n");
		return -1;
	}

	rc = _ptm_lib_decode_header(csv, &msglen, &ver, &type, &cmd_id,
				    client_name);

	csv_clean(csv);
	csv_free(csv);

	if (rc < 0) {
		/* 头解析失败，可能不是 CSV 而是遗留的纯文本命令；把整条命令读全
		 * 后再判断能否处理。 */
		/* could not decode the CSV - maybe its legacy cmd?
		 * get the entire cmd from the socket and see if we can process
		 * it
		 */
		if (len == PTMLIB_MSG_HDR_LEN) {
			len += _ptm_lib_read_ptm_socket(
				fd, (inbuf + PTMLIB_MSG_HDR_LEN),
				inlen - PTMLIB_MSG_HDR_LEN);
			if (len <= 0)
				return (len);
		}

		inbuf[len] = '\0';
		/* 遗留命令里目前只支持 get-status 这一条。 */
		/* we only support the get-status cmd */
		if (strcmp(inbuf, PTMLIB_CMD_GET_STATUS)) {
			DLOG("unsupported legacy cmd %s\n", inbuf);
			return -1;
		}
		/* 内部构造一条与遗留命令等价的 CSV 风格命令上下文。 */
		/* internally create a csv-style cmd */
		ptm_lib_init_msg(hdl, 0, PTMLIB_MSG_TYPE_CMD, NULL,
				 (void *)&p_ctxt);
		if (!p_ctxt) {
			DLOG("couldnt allocate context\n");
			return -1;
		}
		ptm_lib_append_msg(hdl, p_ctxt, "cmd", PTMLIB_CMD_GET_STATUS);

	} else {

		if (msglen > inlen) {
			DLOG("msglen [%d] > inlen [%d]\n", msglen, inlen);
			return -1;
		}

		/* 头已解析成功，按头里的 msglen 把整条消息读全。 */
		/* read the rest of the msg */
		len = _ptm_lib_read_ptm_socket(fd, inbuf, msglen);
		if (len <= 0) {
			return (len);
		}

		inbuf[len] = '\0';

		csv = csv_init(NULL, NULL, PTMLIB_MSG_SZ);
		if (!csv) {
			ERRLOG("Cannot allocate csv for msg\n");
			return -1;
		}

		csv_decode(csv, inbuf);
		p_ctxt = calloc(1, sizeof(*p_ctxt));
		if (!p_ctxt) {
			ERRLOG("%s: Could not allocate context \n", __func__);
			csv_clean(csv);
			csv_free(csv);
			return -1;
		}

		p_ctxt->csv = csv;
		p_ctxt->cmd_id = cmd_id;
		p_ctxt->type = type;
	}

	/* 按消息类型分派到对应回调；未知类型视为错误返回 -1。 */
	switch (p_ctxt->type) {
	case PTMLIB_MSG_TYPE_NOTIFICATION:
		if (hdl->notify_cb)
			hdl->notify_cb(arg, p_ctxt);
		break;
	case PTMLIB_MSG_TYPE_CMD:
		if (hdl->cmd_cb)
			hdl->cmd_cb(arg, p_ctxt);
		break;
	case PTMLIB_MSG_TYPE_RESPONSE:
		if (hdl->response_cb)
			hdl->response_cb(arg, p_ctxt);
		break;
	default:
		return -1;
	}

	csv_clean(p_ctxt->csv);
	csv_free(p_ctxt->csv);
	free(p_ctxt);

	return len;
}

/* ptm_lib_register：分配并初始化客户端句柄。
 * 用 strlcpy 拷贝客户端名（保证不越界），并记录三个回调。
 * 分配失败返回 NULL。
 */
ptm_lib_handle_t *ptm_lib_register(char *client_name, ptm_cmd_cb cmd_cb,
				   ptm_notify_cb notify_cb,
				   ptm_response_cb response_cb)
{
	ptm_lib_handle_t *hdl;

	hdl = calloc(1, sizeof(*hdl));

	if (hdl) {
		strlcpy(hdl->client_name, client_name, sizeof(hdl->client_name));
		hdl->cmd_cb = cmd_cb;
		hdl->notify_cb = notify_cb;
		hdl->response_cb = response_cb;
	}

	return hdl;
}

/* ptm_lib_deregister：清零并释放句柄。句柄为空时安全返回。 */
void ptm_lib_deregister(ptm_lib_handle_t *hdl)
{
	if (hdl) {
		memset(hdl, 0x00, sizeof(*hdl));
		free(hdl);
	}
}

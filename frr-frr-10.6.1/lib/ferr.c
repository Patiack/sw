// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读: ferr.c
 * ----------------------------------------------------------------------------
 * 本文件实现 FRR 错误对象框架(ferr)与错误参考表机制.
 * 作用: 1) 在当前线程内保存最近一次错误的详情(位置, 类别, 消息, errno, 路径),
 *   供 ferr_get_last 读取; 2) 维护全局错误参考哈希表, 支持按错误码查询并展示
 *   标题, 描述与建议(show error 命令).
 *
 * 核心数据结构:
 *   - errkey: 线程局部键, 每线程一份 struct ferr;
 *   - ferr_free: 挂在线程键上的销毁回调, 负责释放线程的错误对象;
 *   - refs 与 refs_mtx: 全局错误参考哈希表及其互斥锁.
 *
 * 关键函数: ferr_set_va 是设置错误的核心; log_ref_add / log_ref_get /
 *   log_ref_display 管理错误参考表; vty_print_error 负责带 $ERR 替换的输出.
 *
 * 与其他模块关系: 实现 ferr.h 声明的接口; 依赖 jhash 计算唯一 id, 依赖 hash,
 *   linklist, json 与 vty 完成查询与展示.
 *
 * 阅读顺序: 先看线程键及其构造析构, 再看哈希表与比较, 取键函数, 然后读
 *   log_ref_add / get / display, 最后看错误设置(ferr_set_va)与错误打印.
 *
 * 易踩的坑: errkey 的创建与删除通过 _CONSTRUCTOR 与 _DESTRUCTOR 属性在加载,
 *   卸载时自动执行; 对 refs 的访问都要加 refs_mtx; ferr_set_va 的 unique_id 由
 *   文件名与格式串哈希得到, 修改格式串会改变该 id.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <inttypes.h>

#include "ferr.h"
#include "vty.h"
#include "jhash.h"
#include "memory.h"
#include "hash.h"
#include "command.h"
#include "json.h"
#include "linklist.h"
#include "frr_pthread.h"

/*
 * 声明本文件专用的内存类型 ERRINFO, 用于跟踪错误对象的内存分配.
 */
DEFINE_MTYPE_STATIC(LIB, ERRINFO, "error information");

/*
 * Thread-specific key for temporary storage of allocated ferr.
 */
/*
 * 线程局部键: 每线程关联一个 struct ferr, 保存该线程最近的错误详情.
 */
static pthread_key_t errkey;

/*
 * 线程局部存储的销毁回调: 释放该线程的错误对象.
 * arg: pthread_getspecific 取到的 struct ferr 指针(可为 NULL).
 */
static void ferr_free(void *arg)
{
	XFREE(MTYPE_ERRINFO, arg);
}

/*
 * 库加载时(_CONSTRUCTOR 优先级 500)创建线程键 errkey, 注册销毁回调 ferr_free.
 */
static void err_key_init(void) __attribute__((_CONSTRUCTOR(500)));
static void err_key_init(void)
{
	pthread_key_create(&errkey, ferr_free);
}

/*
 * 库卸载时(_DESTRUCTOR 优先级 500)删除线程键 errkey.
 */
static void err_key_fini(void) __attribute__((_DESTRUCTOR(500)));
static void err_key_fini(void)
{
	pthread_key_delete(errkey);
}

/*
 * Global shared hash table holding reference text for all defined errors.
 */
/*
 * 保护全局错误参考哈希表 refs 的互斥锁.
 */
static pthread_mutex_t refs_mtx = PTHREAD_MUTEX_INITIALIZER;
/*
 * 全局错误参考哈希表: 键为错误码, 值为各模块注册进来的 struct log_ref.
 */
struct hash *refs;

/*
 * 哈希表比较函数: 判断两条错误参考条目是否为同一错误码.
 * a / b: 指向 struct log_ref 的指针.
 */
static bool ferr_hash_cmp(const void *a, const void *b)
{
	const struct log_ref *f_a = a;
	const struct log_ref *f_b = b;

	return f_a->code == f_b->code;
}

/*
 * 哈希表取键函数: 直接以错误码 log_ref.code 作为哈希键.
 */
static inline unsigned int ferr_hash_key(const void *a)
{
	const struct log_ref *f = a;

	return f->code;
}

/*
 * 把一张以 END_FERR 结尾的错误表注册进 refs 哈希表.
 * ref: 错误表首地址.
 * 副作用: 加锁后自 ref[0] 起逐条 hash_get 插入, 直到遇到 code 为 END_FERR 的项.
 */
void log_ref_add(struct log_ref *ref)
{
	uint32_t i = 0;

	frr_with_mutex (&refs_mtx) {
		while (ref[i].code != END_FERR) {
			(void)hash_get(refs, &ref[i], hash_alloc_intern);
			i++;
		}
	}
}

/*
 * 按错误码在 refs 中查找错误参考条目.
 * code: 错误码. 返回: 命中的 log_ref 指针, 未命中返回 NULL.
 */
struct log_ref *log_ref_get(uint32_t code)
{
	struct log_ref holder;
	struct log_ref *ref;

	holder.code = code;
	frr_with_mutex (&refs_mtx) {
		ref = hash_lookup(refs, &holder);
	}

	return ref;
}

/*
 * 展示错误参考信息(文本或 JSON).
 * vty: 输出目标; code: 错误码(0 表示全部); json: 是否输出 JSON.
 * 说明: code 为 0 时把整张哈希表转成链表遍历; 指定 code 时只查单条; 文本模式
 *   打印标题, 描述与建议, JSON 模式以错误码为键输出对象.
 */
void log_ref_display(struct vty *vty, uint32_t code, bool json)
{
	struct log_ref *ref;
	struct json_object *top = NULL, *obj = NULL;
	struct list *errlist;
	struct listnode *ln;

	if (json)
		top = json_object_new_object();

	frr_with_mutex (&refs_mtx) {
		errlist = code ? list_new() : hash_to_list(refs);
	}

	if (code) {
		ref = log_ref_get(code);
		if (!ref) {
			if (top)
				json_object_free(top);
			list_delete(&errlist);
			return;
		}
		listnode_add(errlist, ref);
	}

	for (ALL_LIST_ELEMENTS_RO(errlist, ln, ref)) {
		if (json) {
			char key[11];

			snprintf(key, sizeof(key), "%u", ref->code);
			obj = json_object_new_object();
			json_object_string_add(obj, "title", ref->title);
			json_object_string_add(obj, "description",
					       ref->description);
			json_object_string_add(obj, "suggestion",
					       ref->suggestion);
			json_object_object_add(top, key, obj);
		} else {
			char pbuf[256];
			char ubuf[256];

			snprintf(pbuf, sizeof(pbuf), "\nError %u - %s",
				 ref->code, ref->title);
			memset(ubuf, '=', strlen(pbuf));
			ubuf[strlen(pbuf)] = '\0';

			vty_out(vty, "%s\n%s\n", pbuf, ubuf);
			vty_out(vty, "Description:\n%s\n\n", ref->description);
			vty_out(vty, "Recommendation:\n%s\n", ref->suggestion);
		}
	}

	vty_json(vty, top);
	list_delete(&errlist);
}

/*
 * show error 命令: 查询指定错误码或全部错误的参考信息, 可选 json 输出.
 * 用法: show error 加 (1-4294967295) 或 all, 可再跟 json.
 */
DEFUN_NOSH(show_error_code,
	   show_error_code_cmd,
	   "show error <(1-4294967295)|all> [json]",
	   SHOW_STR
	   "Information on errors\n"
	   "Error code to get info about\n"
	   "Information on all errors\n"
	   JSON_STR)
{
	bool json = strmatch(argv[argc-1]->text, "json");
	uint32_t arg = 0;

	if (!strmatch(argv[2]->text, "all"))
		arg = strtoul(argv[2]->arg, NULL, 10);

	log_ref_display(vty, arg, json);
	return CMD_SUCCESS;
}

/*
 * 初始化错误参考子系统: 创建 refs 哈希表.
 */
void log_ref_init(void)
{
	frr_with_mutex (&refs_mtx) {
		refs = hash_create(ferr_hash_key, ferr_hash_cmp,
				   "Error Reference Texts");
	}
}

/*
 * 清理错误参考子系统: 清空并释放 refs 哈希表.
 */
void log_ref_fini(void)
{
	frr_with_mutex (&refs_mtx) {
		hash_clean_and_free(&refs, NULL);
	}
}

/*
 * 注册 show error 命令到 VIEW 节点.
 */
void log_ref_vty_init(void)
{
	install_element(VIEW_NODE, &show_error_code_cmd);
}


/*
 * 取当前线程最近一次设置的错误.
 * errval: 调用方持有的 ferr_r(当前不参与查找, 仅为 API 兼容保留).
 * 返回: 错误指针; 若线程尚无错误或错误已被清除则返回 NULL.
 */
const struct ferr *ferr_get_last(ferr_r errval)
{
	struct ferr *last_error = pthread_getspecific(errkey);
	if (!last_error || last_error->kind == 0)
		return NULL;
	return last_error;
}

/*
 * 清除当前线程的错误状态(把 kind 置为 0).
 * 返回 0, 表示成功.
 */
ferr_r ferr_clear(void)
{
	struct ferr *last_error = pthread_getspecific(errkey);
	if (last_error)
		last_error->kind = 0;
	return ferr_ok();
}

PRINTFRR(7, 0)
/*
 * 设置错误的核心函数(内部).
 * file / line / func: 错误位置; kind: 错误类别; pathname: 可选路径;
 * errno_val: 相关 errno; text 与 va: 格式串及参数.
 * 副作用: 首次调用时为当前线程分配并绑定 struct ferr; 写入位置, 类别, 由文件名
 *   与格式串哈希得到的 unique_id, errno, 路径与格式化消息.
 * 返回: 固定返回 -1, 以表示出错.
 */
static ferr_r ferr_set_va(const char *file, int line, const char *func,
			  enum ferr_kind kind, const char *pathname,
			  int errno_val, const char *text, va_list va)
{
	struct ferr *error = pthread_getspecific(errkey);

	if (!error) {
		error = XCALLOC(MTYPE_ERRINFO, sizeof(*error));

		pthread_setspecific(errkey, error);
	}

	error->file = file;
	error->line = line;
	error->func = func;
	error->kind = kind;

	error->unique_id = jhash(text, strlen(text),
				 jhash(file, strlen(file), 0xd4ed0298));

	error->errno_val = errno_val;
	if (pathname)
		snprintf(error->pathname, sizeof(error->pathname), "%s",
			 pathname);
	else
		error->pathname[0] = '\0';

	vsnprintf(error->message, sizeof(error->message), text, va);
	return -1;
}

/*
 * 设置错误的对外入口(带 FILE, LINE, func, 无路径与 errno).
 * 内部转发到 ferr_set_va; 返回 -1 表示出错. 仅供 ferr_* 宏间接调用.
 */
ferr_r ferr_set_internal(const char *file, int line, const char *func,
			 enum ferr_kind kind, const char *text, ...)
{
	ferr_r rv;
	va_list va;
	va_start(va, text);
	rv = ferr_set_va(file, line, func, kind, NULL, 0, text, va);
	va_end(va);
	return rv;
}

/*
 * 设置错误的对外入口扩展版: 额外携带 pathname 与 errno_val.
 * 内部转发到 ferr_set_va; 返回 -1 表示出错. 仅供带 errno 的 ferr_* 宏间接调用.
 */
ferr_r ferr_set_internal_ext(const char *file, int line, const char *func,
			     enum ferr_kind kind, const char *pathname,
			     int errno_val, const char *text, ...)
{
	ferr_r rv;
	va_list va;
	va_start(va, text);
	rv = ferr_set_va(file, line, func, kind, pathname, errno_val, text, va);
	va_end(va);
	return rv;
}

/*
 * vty_print_error 中用于占位替换错误的标记串: 消息里的 $ERR 会被替换.
 */
#define REPLACE "$ERR"
/*
 * 把错误消息打印到 vty, 支持 $ERR 占位替换.
 * vty: 输出目标; err: 错误返回值; msg: 格式串(可能含 $ERR).
 * 实现: 先格式化 msg, 找到 $ERR 后把其前半段, 错误对象的 message 与其后半段
 *   拼接输出; 无 $ERR 时整条直接输出; 找不到错误对象时打印 (no error?).
 */
void vty_print_error(struct vty *vty, ferr_r err, const char *msg, ...)
{
	char tmpmsg[512], *replacepos;
	const struct ferr *last_error = ferr_get_last(err);

	va_list va;
	va_start(va, msg);
	vsnprintf(tmpmsg, sizeof(tmpmsg), msg, va);
	va_end(va);

	replacepos = strstr(tmpmsg, REPLACE);
	if (!replacepos)
		vty_out(vty, "%s\n", tmpmsg);
	else {
		replacepos[0] = '\0';
		replacepos += sizeof(REPLACE) - 1;
		vty_out(vty, "%s%s%s\n", tmpmsg,
			last_error ? last_error->message : "(no error?)",
			replacepos);
	}
}

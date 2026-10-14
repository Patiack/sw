// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读: ferr.h
 * ----------------------------------------------------------------------------
 * 本文件是 FRR 错误对象框架(ferr)的公开接口, 提供比 errno 更丰富, 更易读的
 *   错误表达与展示方式.
 * 作用: 让函数返回一个可判定真假的 ferr_r, 同时在当前线程内保留文件, 行号,
 *   函数名, 错误类别, 格式化消息, errno 与路径名等细节, 供上层统一打印或记录.
 *
 * 核心数据结构:
 *   - typedef int ferr_r: 返回类型, 约定 0 表示成功, 非 0 表示出错.
 *   - enum ferr_kind: 错误大类(BUG, 配置非法, 配置与现网不符, 资源, 系统, 库).
 *   - struct ferr: 单条错误详情(位置, 类别, 唯一 id, 消息, errno, 路径).
 *   - struct log_ref: 面向用户的错误参考条目(错误码, 标题, 描述, 建议), 与
 *     lib_errors.c 等错误表配套.
 *
 * 与其他模块关系: 各守护进程用这里的宏(ferr_code_bug 等)上报错误;
 *   ferr_set_internal 系列与 ferr_get_last 在 ferr.c 中实现; 错误码区间由各
 *   *_errors.c 表使用.
 *
 * 阅读顺序: 先读 ferr_r 与 ferr_kind, 再看 struct ferr, 然后看报告宏
 *   (ferr_code_bug 等), 最后看 CMD_FERR_* 与文件末尾的完整示例.
 *
 * 易踩的坑: 判定错误时只写 if (value), 不要与整数常量比较或做符号判断; 错误
 *   消息末尾不要带换行; 错误码按守护进程区间分配, 不要跨区间乱用.
 */

#ifndef _FRR_FERR_H
#define _FRR_FERR_H

/***********************************************************
 * scroll down to the end of this file for a full example! *
 ***********************************************************/

#include <stdint.h>
#include <limits.h>
#include <errno.h>

#include "vty.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ferr 返回类型. 约定: 0 表示成功(布尔假), 非 0 表示出错(布尔真).
 * 判定时只应写成 if (value), 不要与 0 比较或做正负判断(未来可能改为指针).
 */
/* return type when this error indication stuff is used.
 *
 * guaranteed to have boolean evaluation to "false" when OK, "true" when error
 * (i.e. can be changed to pointer in the future if necessary)
 *
 * For checking, always use "if (value)", nothing else.
 * Do _NOT_ use any integer constant (!= 0), or sign check (< 0).
 */
typedef int ferr_r;

/*
 * 错误的大类.
 * FERR_OK 无错; FERR_CODE_BUG 本不该发生的内部错误(近似断言); FERR_CONFIG_INVALID
 * 用户配置不合理; FERR_CONFIG_REALITY 配置与现网不符; FERR_RESOURCE 资源耗尽;
 * FERR_SYSTEM 系统调用错误; FERR_LIBRARY 外部库返回错误.
 */
/* rough category of error indication */
enum ferr_kind {
	/* no error */
	FERR_OK = 0,

	/* something isn't the way it's supposed to be.
	 * (things that might otherwise be asserts, really)
	 */
	FERR_CODE_BUG,

	/* user-supplied parameters don't make sense or is inconsistent
	 * if you can express a rule for it (e.g. "holdtime > 2 * keepalive"),
	 * it's this category.
	 */
	FERR_CONFIG_INVALID,

	/* user-supplied parameters don't line up with reality
	 * (IP address or interface not available, etc.)
	 * NB: these are really TODOs where the code needs to be fixed to
	 * respond to future changes!
	 */
	FERR_CONFIG_REALITY,

	/* out of some system resource (probably memory)
	 * aka "you didn't spend enough money error" */
	FERR_RESOURCE,

	/* system error (permission denied, etc.) */
	FERR_SYSTEM,

	/* error return from some external library
	 * (FERR_SYSTEM and FERR_LIBRARY are not strongly distinct) */
	FERR_LIBRARY,
};

/*
 * 单条错误详情(存放在线程局部存储中).
 * file / func / line: 出错代码位置; kind: 错误类别;
 * unique_id: 由文件名与错误格式串算出的校验和(跨代码改动较稳定);
 * message: 格式化后的错误消息; errno_val: 相关 errno(非 0 时有效);
 * pathname: 相关路径(首字节非 0 时有效).
 */
struct ferr {
	/* code location */
	const char *file;
	const char *func;
	int line;

	enum ferr_kind kind;

	/* unique_id is calculated as a checksum of source filename and error
	 * message format (*before* calling vsnprintf).  Line number and
	 * function name are not used; this keeps the number reasonably static
	 * across changes.
	 */
	uint32_t unique_id;

	char message[384];

	/* valid if != 0.  note "errno" might be preprocessor foobar. */
	int errno_val;
	/* valid if pathname[0] != '\0' */
	char pathname[PATH_MAX];
};

/* Numeric ranges assigned to daemons for use as error codes. */
/*
 * 分配给各守护进程的错误码数值区间.
 * 每个守护进程占据一个 0xXX000001 至 0xXXFFFFFF 的区段, 例如 LIB_FERR_START
 * 为 lib 的起点; 各 *_errors.h 中的枚举以此为基准分配具体错误码.
 * END_FERR 是错误表结束的哨兵值.
 */
#define BABEL_FERR_START    0x01000001
#define BABEL_FRRR_END      0x01FFFFFF
#define BGP_FERR_START      0x02000001
#define BGP_FERR_END        0x02FFFFFF
#define EIGRP_FERR_START    0x03000001
#define EIGRP_FERR_END      0x03FFFFFF
#define ISIS_FERR_START     0x04000001
#define ISIS_FERR_END       0x04FFFFFF
#define LDP_FERR_START      0x05000001
#define LDP_FERR_END        0x05FFFFFF
#define LIB_FERR_START      0x06000001
#define LIB_FERR_END        0x06FFFFFF
#define NHRP_FERR_START     0x07000001
#define NHRP_FERR_END       0x07FFFFFF
#define OSPF_FERR_START     0x08000001
#define OSPF_FERR_END       0x08FFFFFF
#define OSPFV3_FERR_START   0x09000001
#define OSPFV3_FERR_END     0x09FFFFFF
#define PBR_FERR_START      0x0A000001
#define PBR_FERR_END        0x0AFFFFFF
#define PIM_FERR_START      0x0B000001
#define PIM_FERR_STOP       0x0BFFFFFF
#define RIP_FERR_START      0x0C000001
#define RIP_FERR_STOP       0x0CFFFFFF
#define RIPNG_FERR_START    0x0D000001
#define RIPNG_FERR_STOP     0x0DFFFFFF
#define SHARP_FERR_START    0x0E000001
#define SHARP_FERR_END      0x0EFFFFFF
#define VTYSH_FERR_START    0x0F000001
#define VTYSH_FRR_END       0x0FFFFFFF
#define WATCHFRR_FERR_START 0x10000001
#define WATCHFRR_FERR_END   0x10FFFFFF
#define PATH_FERR_START     0x11000001
#define PATH_FERR_END       0x11FFFFFF
#define ZEBRA_FERR_START    0xF1000001
#define ZEBRA_FERR_END      0xF1FFFFFF
#define END_FERR            0xFFFFFFFF

/*
 * 面向用户的错误参考条目.
 * code: 错误码(约定 -1 表示无编码的临时错误); title: 极短标题;
 * description: 简短描述; suggestion: 处理建议.
 */
struct log_ref {
	/* Unique error code displayed to end user as a reference. -1 means
	 * this is an uncoded error that does not have reference material. */
	uint32_t code;
	/* Ultra brief title */
	const char *title;
	/* Brief description of error */
	const char *description;
	/* Remedial suggestion */
	const char *suggestion;
};

/*
 * 把一张以 END_FERR 结尾的错误表注册进全局错误哈希表.
 * ref: 错误表首地址.
 * 副作用: 加锁遍历表项并逐条插入哈希表.
 */
void log_ref_add(struct log_ref *ref);
/*
 * 按错误码查询错误参考条目.
 * code: 错误码. 返回: 命中的 log_ref 指针, 未命中返回 NULL.
 */
struct log_ref *log_ref_get(uint32_t code);
/*
 * 把错误参考信息输出到 vty.
 * vty: 输出目标; code: 错误码(0 表示全部); json: 为真则输出 JSON.
 * 副作用: 可能创建并释放临时链表与 JSON 对象.
 */
void log_ref_display(struct vty *vty, uint32_t code, bool json);

/*
 * This function should be called by the
 * code in libfrr.c
 */
/*
 * 错误参考子系统的初始化与清理, 以及 vty 命令注册.
 * 这几个函数应由 libfrr.c 的初始化流程在合适时机调用.
 */
void log_ref_init(void);
void log_ref_fini(void);
void log_ref_vty_init(void);

/* get error details.
 *
 * NB: errval/ferr_r does NOT carry the full error information.  It's only
 * passed around for future API flexibility.  ferr_get_last always returns
 * the last error set in the current thread.
 */
/*
 * 取当前线程最近一次设置的错误详情.
 * errval: 调用方持有的 ferr_r(仅用于未来 API 扩展, 当前不参与查找).
 * 返回: 最近错误指针; 无错误时返回 NULL. 注意: 该详情是线程局部的, 始终返回
 *   本线程最后一次设置的错误.
 */
const struct ferr *ferr_get_last(ferr_r errval);

/*
 * Can optionally be called at strategic locations.
 * Always returns 0.
 */
/*
 * 清除当前线程的错误状态(可在关键位置选择性地调用).
 * 恒返回 0, 便于写成 return ferr_clear(); 形式.
 */
ferr_r ferr_clear(void);

/* do NOT call these functions directly.  only for macro use! */
/*
 * 设置错误详情的内部函数. 不要直接调用, 仅供宏使用.
 * 记录位置, 类别与格式化消息; ext 版本还带 pathname 与 errno_val.
 * 返回: 非 0(约定为 -1)表示出错.
 */
ferr_r ferr_set_internal(const char *file, int line, const char *func,
			 enum ferr_kind kind, const char *text, ...)
	PRINTFRR(5, 6);
ferr_r ferr_set_internal_ext(const char *file, int line, const char *func,
			     enum ferr_kind kind, const char *pathname,
			     int errno_val, const char *text, ...)
	PRINTFRR(7, 8);

/*
 * 返回表示成功的值(0), 使 return ferr_ok(); 能表达成功语义.
 */
#define ferr_ok() 0

/*
 * 各类错误的便捷上报宏, 自动填入 FILE, LINE 与 func.
 * ferr_code_bug / ferr_cfg_invalid / ferr_cfg_reality / ferr_cfg_resource /
 * ferr_system / ferr_library 分别对应不同 ferr_kind; 消息末尾不要加换行.
 * 若需善后清理, 请先把返回值存入 ferr_r 变量再处理.
 */
/* Report an error.
 *
 * If you need to do cleanup (free memory, etc.), save the return value in a
 * variable of type ferr_r.
 *
 * Don't put a \n at the end of the error message.
 */
#define ferr_code_bug(...)                                                     \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_CODE_BUG,         \
			  __VA_ARGS__)
#define ferr_cfg_invalid(...)                                                  \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_CONFIG_INVALID,   \
			  __VA_ARGS__)
#define ferr_cfg_reality(...)                                                  \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_CONFIG_REALITY,   \
			  __VA_ARGS__)
#define ferr_cfg_resource(...)                                                 \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_RESOURCE,         \
			  __VA_ARGS__)
#define ferr_system(...)                                                       \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_SYSTEM,           \
			  __VA_ARGS__)
#define ferr_library(...)                                                      \
	ferr_set_internal(__FILE__, __LINE__, __func__, FERR_LIBRARY,          \
			  __VA_ARGS__)

/*
 * 带额外信息的错误上报宏变体.
 * ferr_system_errno 自动带上当前 errno; ferr_system_path_errno 另外带上路径.
 */
/* extended information variants */
#define ferr_system_errno(...)                                                 \
	ferr_set_internal_ext(__FILE__, __LINE__, __func__, FERR_SYSTEM, NULL, \
			      errno, __VA_ARGS__)
#define ferr_system_path_errno(path, ...)                                      \
	ferr_set_internal_ext(__FILE__, __LINE__, __func__, FERR_SYSTEM, path, \
			      errno, __VA_ARGS__)

#include "vty.h"
/*
 * 把错误消息打印到 vty, 其中 $ERR 会被替换为错误对象的 message.
 * vty: 输出目标; err: 错误返回值; msg: 含可选 $ERR 占位符的格式串.
 */
/* print error message to vty;  $ERR is replaced by the error's message */
void vty_print_error(struct vty *vty, ferr_r err, const char *msg, ...)
	PRINTFRR(3, 4);

/*
 * 命令处理中的错误处理辅助宏.
 * CMD_FERR_DO: 执行 func, 出错则用 vty_print_error 打印并按 action 处理.
 * CMD_FERR_RETURN: 出错时返回 CMD_WARNING_CONFIG_FAILED.
 * CMD_FERR_GOTO: 出错时跳转到指定标签.
 */
#define CMD_FERR_DO(func, action, ...)                                         \
	do {                                                                   \
		ferr_r cmd_retval = func;                                      \
		if (cmd_retval) {                                              \
			vty_print_error(vty, cmd_retval, __VA_ARGS__);         \
			action;                                                \
		}                                                              \
	} while (0)

#define CMD_FERR_RETURN(func, ...)                                             \
	CMD_FERR_DO(func, return CMD_WARNING_CONFIG_FAILED, __VA_ARGS__)
#define CMD_FERR_GOTO(func, label, ...)                                        \
	CMD_FERR_DO(func, goto label, __VA_ARGS__)

/*
 * 以下为 ferr 的完整使用示例(含设置错误与用 CMD_FERR_RETURN 处理错误),
 * 由 THIS_IS_AN_EXAMPLE 开关控制, 正常编译时不会启用.
 */
/* example: uses bogus #define to keep indent.py happy */
#ifdef THIS_IS_AN_EXAMPLE
ferr_r foo_bar_set(struct object *obj, int bar)
{
	if (bar < 1 || bar >= 100)
		return ferr_config_invalid("bar setting (%d) must be 0<x<100",
					   bar);
	obj->bar = bar;
	if (ioctl(obj->fd, bar))
		return ferr_system_errno("couldn't set bar to %d", bar);

	return ferr_ok();
}

DEFUN("bla")
{
	CMD_FERR_RETURN(foo_bar_set(obj, atoi(argv[1])),
			"command failed: $ERR\n");
	return CMD_SUCCESS;
}

#endif /* THIS_IS_AN_EXAMPLE */

#ifdef __cplusplus
}
#endif

#endif /* _FERR_H */

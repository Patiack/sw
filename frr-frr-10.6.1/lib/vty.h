// SPDX-License-Identifier: GPL-2.0-or-later
/* Virtual terminal [aka TeletYpe] interface routine
 * Copyright (C) 1997 Kunihiro Ishiguro
 */

/* ============================================================================
 * vty.h —— VTY（虚拟终端，Virtual TeletYpe）子系统的数据结构与接口声明
 *
 * VTY 是 FRR 所有“用户交互”的统一抽象：telnet 连接、本地 stdin/stdout、
 * vtysh 客户端 / 服务端，以及读取配置文件时临时创建的会话，都用 struct vty
 * 表示。命令解析、配置进入退出、输出分页、CLI 历史等都在此结构上展开。
 *
 * 本头文件主要内容：
 *   - 编译期常量：命令行缓冲大小、历史上限、CLI 最大嵌套深度、候选变更条数。
 *   - 若干小结构：vty_error（错误回显）、vty_cfg_change（一条候选配置变更）、
 *                 vty_arg（命令参数）。
 *   - enum vty_status：vty 的当前状态（正常 / 关闭 / 分页等）。
 *   - struct vty：核心大结构，字段按 连接输出 / 命令解析 / 候选配置 /
 *                 XPath 上下文 / mgmtd 交互 / 终端属性 等分组。
 *   - 操作上下文的宏：VTY_PUSH_CONTEXT / VTY_GET_CONTEXT / VTY_CHECK_CONTEXT /
 *                 VTY_DECLVAR_CONTEXT / VTY_PUSH_XPATH 等，配合 qobj 做对象安全引用。
 *   - 对外函数原型：初始化、输出、JSON 输出、读写配置文件、启停服务、进入退出配置。
 *   - mgmtd 前端相关回调钩子（vty_new_mgmt_cb 等）。
 *
 * 阅读建议：先看 struct vty 的字段分组理解“一个 vty 保存了什么”，再看上下文宏
 * 如何用 qobj_index 安全引用配置对象，最后看函数原型；真正实现见 vty.c。
 * ============================================================================
 */

#ifndef _ZEBRA_VTY_H
#define _ZEBRA_VTY_H

#include <sys/types.h>

#include "frrevent.h"
#include "log.h"
#include "sockunion.h"
#include "qobj.h"
#include "compiler.h"
#include "northbound.h"
#include "zlog_live.h"
#include "libfrr.h"
#include "mgmt_fe_client.h"

#ifdef __cplusplus
extern "C" {
#endif

struct json_object;
struct frregex;

/* 编译期常量（VTY 相关容量上限）：
 *   VTY_BUFSIZ  每行命令 / 错误信息的缓冲大小（8 KiB）。
 *   VTY_MAXHIST 每个 vty 保存的命令历史上限（条数）。
 *   VTY_MAXDEPTH CLI 配置嵌套的最大深度（XPath 栈深度上限）。 */
#define VTY_BUFSIZ   8192
#define VTY_MAXHIST 20
#define VTY_MAXDEPTH 8

/* 单次操作里可累积的候选配置变更条数上限（cfg_changes 数组的容量）。 */
#define VTY_MAXCFGCHANGES 16

/* 命令执行错误信息：把某条命令的失败细节暂存起来以便统一回显。
 *   error_buf  错误文本缓冲区（最大 VTY_BUFSIZ）。
 *   line_num   出错所在行号。
 *   cmd_ret    该命令的返回码（CMD_WARNING / CMD_ERR_* 等）。 */
struct vty_error {
	char error_buf[VTY_BUFSIZ];
	uint32_t line_num;
	int cmd_ret;
};

/* 一条待应用的候选（candidate）配置变更。
 *   xpath     目标数据节点的绝对路径。
 *   operation 操作类型（创建 / 修改 / 删除等，见 enum nb_operation）。
 *   value     变更携带的值（可为 NULL）。 */
struct vty_cfg_change {
	char xpath[XPATH_MAXLEN];
	enum nb_operation operation;
	const char *value;
};

/* vty 当前状态：驱动输出分页与关闭流程。
 *   VTY_NORMAL   正常交互。
 *   VTY_CLOSE    已请求关闭。
 *   VTY_MORE     处于分页等待（显示 --More--）。
 *   VTY_MORELINE 分页时“下一行”被请求。
 *   VTY_PASSFD   正在传递或接收一个文件描述符（如 terminal monitor）。 */
/* Current vty status. */
enum vty_status {
	VTY_NORMAL,
	VTY_CLOSE,
	VTY_MORE,
	VTY_MORELINE,
	VTY_PASSFD,
};

/* 声明双向链表类型 vtys：下方 struct vty 的 itm 字段属于它，
 * 用于把所有活跃的 vty 串成全局链表统一管理。 */
PREDECL_DLIST(vtys);

/* 核心结构：一个 VTY 会话的全部状态。字段可按用途分为几组：
 *   连接与输出： fd / wfd / of / of_saved / type / is_paged，决定数据从哪读、
 *              往哪写，以及是 终端 / 文件 / vtysh 客户端 / vtysh 服务端。
 *   命令解析：   node（当前节点）、buf / cp / length / max（命令行缓冲与光标）、
 *              lbuf（行缓冲）、obuf（输出缓冲）、error（错误列表）、
 *              hist / hp / hindex（命令历史）、status（状态枚举）。
 *   候选配置：   num_cfg_changes / cfg_changes、num_rpc_params / rpc_params、
 *              candidate_config / candidate_config_base、pending_allowed、
 *              pending_commit、pending_cmds_* 等，支撑“进入配置-暂存-提交”。
 *   XPath 上下文：xpath_index + xpath[][] 栈，配合 VTY_PUSH_XPATH 使用。
 *   mgmtd 交互： qobj_index / qobj_index_sub、mgmt_session_id / mgmt_client_id /
 *              mgmt_req_id、mgmt_req_pending_cmd、mgmt_locked_*_ds 等。
 *   终端属性：   width / height / lines / escape、IAC 与 NAWS 子协商字段、
 *              live_log（terminal monitor）、address、frame 输出缓冲、
 *              v_timeout / t_timeout 超时控制、vty_buf_threshold 增量输出控制。
 * 各字段上一行的英文注释给出逐字段含义；此处仅做分组导引。 */
/* VTY struct. */
struct vty {
	struct vtys_item itm;

	/* File descripter of this vty. */
	int fd;

	/* output FD, to support stdin/stdout combination */
	int wfd;

	/* File output, used for VTYSH only */
	FILE *of;
	FILE *of_saved;

	/* whether we are using pager or not */
	bool is_paged;

	/* Is this vty connect to file or not */
	enum { VTY_TERM,       /* telnet conn or stdin/stdout UI */
	       VTY_FILE,       /* reading and writing config files */
	       VTY_SHELL,      /* vtysh client side UI */
	       VTY_SHELL_SERV, /* server-side vtysh connection */
	} type;

	/* Node status of this vty */
	int node;

	/* Failure count */
	int fail;

	/* Output filer regex */
	bool filter;
	struct frregex *include;

	/* Line buffer */
	struct buffer *lbuf;

	/* Output buffer. */
	struct buffer *obuf;

	/* Command input buffer */
	char *buf;

	/* Command input error buffer */
	struct list *error;

	/* Command cursor point */
	int cp;

	/* Command length */
	int length;

	/* Command max length. */
	int max;

	/* Histry of command */
	char *hist[VTY_MAXHIST];

	/* History lookup current point */
	int hp;

	/* History insert end point */
	int hindex;

	/* Changes enqueued to be applied in the candidate configuration. */
	size_t num_cfg_changes;
	struct nb_cfg_change cfg_changes[VTY_MAXCFGCHANGES];

	/* Input parameters */
	size_t num_rpc_params;
	struct nb_cfg_change rpc_params[VTY_MAXCFGCHANGES];

	/* XPath of the current node */
	int xpath_index;
	char xpath[VTY_MAXDEPTH][XPATH_MAXLEN];

	/*
	 * Keep track of how many SET_CFG requests has been sent so far that
	 * has not been committed yet.
	 */
	size_t mgmt_num_pending_setcfg;

	/* In configure mode. */
	bool config;

	/* Private candidate configuration mode. */
	bool private_config;

	/* Candidate configuration. */
	struct nb_config *candidate_config;

	/* Base candidate configuration. */
	struct nb_config *candidate_config_base;

	/* Dynamic transaction information. */
	size_t buffer_cmd_count;
	bool pending_allowed;
	bool pending_commit;
	char *pending_cmds_buf;
	size_t pending_cmds_buflen;
	size_t pending_cmds_bufpos;

	/* Confirmed-commit timeout and rollback configuration. */
	struct event *t_confirmed_commit_timeout;
	struct nb_config *confirmed_commit_rollback;

	/* qobj object ID (replacement for "index") */
	uint64_t qobj_index;

	/* qobj second-level object ID (replacement for "index_sub") */
	uint64_t qobj_index_sub;

	/* For escape character. */
	unsigned char escape;

	/* Current vty status. */
	enum vty_status status;

	/* vtysh socket/fd passing (for terminal monitor) */
	int pass_fd;

	/* CLI command return value (likely CMD_SUCCESS) when pass_fd != -1 */
	uint8_t pass_fd_status[4];

	/* live logging target / terminal monitor */
	struct zlog_live_cfg live_log;

	/* IAC handling: was the last character received the
	   IAC (interpret-as-command) escape character (and therefore the next
	   character will be the command code)?  Refer to Telnet RFC 854. */
	unsigned char iac;

	/* IAC SB (option subnegotiation) handling */
	unsigned char iac_sb_in_progress;
/* 子协商缓冲区长度：当前只处理 NAWS（窗口大小），按 RFC 1073 需要 5 字节：
 *   <NAWS 类型字节> <16 位宽> <16 位高>。定长缓冲，超出的字节会被丢弃。 */
/* At the moment, we care only about the NAWS (window size) negotiation,
   and that requires just a 5-character buffer (RFC 1073):
     <NAWS char> <16-bit width> <16-bit height> */
#define TELNET_NAWS_SB_LEN 5
	unsigned char sb_buf[TELNET_NAWS_SB_LEN];
	/* How many subnegotiation characters have we received?  We just drop
	   those that do not fit in the buffer. */
	size_t sb_len;

	/* Window width/height. */
	int width;
	int height;

	/* Configure lines. */
	int lines;

	/* Read and write thread. */
	struct event *t_read;
	struct event *t_write;

	/* Timeout seconds and thread. */
	unsigned long v_timeout;
	struct event *t_timeout;

	/* What address is this vty comming from. */
	char address[SU_ADDRSTRLEN];

	/* "frame" output.  This is buffered and will be printed if some
	 * actual output follows, or will be discarded if the frame ends
	 * without any output. */
	size_t frame_pos;
	char frame[1024];

	uint64_t mgmt_session_id; /* FE adapter identifies session w/ this */
	uint64_t mgmt_client_id;  /* FE vty client identifies w/ this ID */
	uint64_t mgmt_req_id;
	/* set when we have sent mgmtd a *REQ command in response to some vty
	 * CLI command and we are waiting on the reply so we can respond to the
	 * vty user. */
	const char *mgmt_req_pending_cmd;
	uintptr_t mgmt_req_pending_data;
	bool mgmt_locked_candidate_ds;
	bool mgmt_locked_running_ds;
	/* Incremental write/flush limit and current accumulator. When
	 * producing large outputs, we try to avoid buffering the entire
	 * output, sending incremental output periodically
	 * as the application code produces it.
	 */
	size_t vty_buf_threshold;
	size_t vty_buf_size_accum;
};

/* 进入某个配置上下文：同时记录节点号与该配置对象的 qobj id。
 * 参数：node 为要切换到的节点；id 为对象的 qobj 标识。
 * 返回：无。副作用：改写 vty->node 与 vty->qobj_index。 */
static inline void vty_push_context(struct vty *vty, int node, uint64_t id)
{
	vty->node = node;
	vty->qobj_index = id;
}

/* 上下文入栈 / 取值 / 校验宏族（基于 qobj 的对象安全引用）：
 *   VTY_PUSH_CONTEXT(node, ptr)     切到 node，并把 ptr 的 qobj id 记入 qobj_index。
 *   VTY_PUSH_CONTEXT_NULL(node)     只切节点，qobj_index 置 0（表示无对象）。
 *   VTY_PUSH_CONTEXT_SUB(node, ptr) 切节点并写 qobj_index_sub，不动 qobj_index（二级子对象）。
 *   VTY_GET_CONTEXT / _SUB          按 id 反查对象，可能返回 NULL。
 *   VTY_CHECK_CONTEXT(ptr)          为 NULL 时输出提示并 return CMD_WARNING，防空指针解引用。
 *   VTY_DECLVAR_CONTEXT(_SUB/_INSTANCE/_CONTEXT_VRF)
 *                                   一步声明并校验上下文对象指针的便捷宏。
 * 坑：VTY_PUSH_CONTEXT(..., NULL) 不可用，因为宏会去解引用 ptr 取 qobj id。 */
/* note: VTY_PUSH_CONTEXT(..., NULL) doesn't work, since it will try to
 * dereference "NULL->qobj_node.nid" */
#define VTY_PUSH_CONTEXT(nodeval, ptr)                                         \
	vty_push_context(vty, nodeval, QOBJ_ID_0SAFE(ptr))
#define VTY_PUSH_CONTEXT_NULL(nodeval) vty_push_context(vty, nodeval, 0ULL)
#define VTY_PUSH_CONTEXT_SUB(nodeval, ptr)                                     \
	do {                                                                   \
		vty->node = nodeval;                                           \
		/* qobj_index stays untouched */                               \
		vty->qobj_index_sub = QOBJ_ID_0SAFE(ptr);                      \
	} while (0)

/* 按 qobj_index 反查上下文对象；若对象已失效（被其它进程删除）可能返回 NULL，
 * 所以用前务必配 VTY_CHECK_CONTEXT 或自行判空。 */
/* can return NULL if context is invalid! */
#define VTY_GET_CONTEXT(structname)                                            \
	QOBJ_GET_TYPESAFE(vty->qobj_index, structname)
#define VTY_GET_CONTEXT_SUB(structname)                                        \
	QOBJ_GET_TYPESAFE(vty->qobj_index_sub, structname)

/* 上下文对象判空：ptr 为 NULL（对象已被别的进程删除）时输出提示并
 * return CMD_WARNING，避免后续解引用空指针。 */
/* will return if ptr is NULL. */
#define VTY_CHECK_CONTEXT(ptr)                                                 \
	if (!ptr) {                                                            \
		vty_out(vty,                                                   \
			"Current configuration object was deleted "            \
			"by another process.\n");                              \
		return CMD_WARNING;                                            \
	}

/* 便捷宏：一步“取上下文对象 + 判空”。
 *   VTY_DECLVAR_CONTEXT(structname, ptr)           从当前上下文取对象并校验。
 *   VTY_DECLVAR_CONTEXT_SUB(structname, ptr)       取二级子对象并校验。
 *   VTY_DECLVAR_INSTANCE_CONTEXT(structname, ptr)  实例上下文；qobj_index 为 0
 *       时直接 return CMD_NOT_MY_INSTANCE。
 *   VTY_DECLVAR_CONTEXT_VRF(vrfptr)                取 VRF；顶层配置节点下用默认 VRF。
 * 声明出的 ptr 保证非 NULL（取不到时会先 return）。 */
/* struct structname *ptr = <context>;   ptr will never be NULL. */
#define VTY_DECLVAR_CONTEXT(structname, ptr)                                   \
	struct structname *ptr = VTY_GET_CONTEXT(structname);                  \
	VTY_CHECK_CONTEXT(ptr);
#define VTY_DECLVAR_CONTEXT_SUB(structname, ptr)                               \
	struct structname *ptr = VTY_GET_CONTEXT_SUB(structname);              \
	VTY_CHECK_CONTEXT(ptr);
#define VTY_DECLVAR_INSTANCE_CONTEXT(structname, ptr)                          \
	if (vty->qobj_index == 0)                                              \
		return CMD_NOT_MY_INSTANCE;                                    \
	struct structname *ptr = VTY_GET_CONTEXT(structname);                  \
	VTY_CHECK_CONTEXT(ptr);

#define VTY_DECLVAR_CONTEXT_VRF(vrfptr)                                        \
	struct vrf *vrfptr;                                                    \
	if (vty->node == CONFIG_NODE)                                          \
		vrfptr = vrf_lookup_by_id(VRF_DEFAULT);                        \
	else                                                                   \
		vrfptr = VTY_GET_CONTEXT(vrf);                                 \
	VTY_CHECK_CONTEXT(vrfptr);                                             \
	MACRO_REQUIRE_SEMICOLON() /* end */

/* XPath 上下文栈相关宏：
 *   VTY_PUSH_XPATH(node, value) 切到 node 并把 XPath 压栈；超过 VTY_MAXDEPTH 报错返回。
 *   VTY_CURR_XPATH             取当前栈顶 XPath。
 *   VTY_CHECK_XPATH            校验当前 XPath 对应数据节点是否仍存在，不存在则报错返回。 */
/* XPath macros. */
#define VTY_PUSH_XPATH(nodeval, value)                                         \
	do {                                                                   \
		if (vty->xpath_index >= VTY_MAXDEPTH) {                        \
			vty_out(vty, "%% Reached maximum CLI depth (%u)\n",    \
				VTY_MAXDEPTH);                                 \
			return CMD_WARNING;                                    \
		}                                                              \
		vty->node = nodeval;                                           \
		strlcpy(vty->xpath[vty->xpath_index], value,                   \
			sizeof(vty->xpath[0]));                                \
		vty->xpath_index++;                                            \
	} while (0)

#define VTY_CURR_XPATH vty->xpath[vty->xpath_index - 1]

#define VTY_CHECK_XPATH                                                        \
	do {                                                                   \
		if (vty->type != VTY_FILE && !vty->private_config &&           \
		    vty->xpath_index > 0 &&                                    \
		    !yang_dnode_exists(vty->candidate_config->dnode,           \
				       VTY_CURR_XPATH)) {                      \
			vty_out(vty,                                           \
				"Current configuration object was deleted "    \
				"by another process.\n\n");                    \
			return CMD_WARNING;                                    \
		}                                                              \
	} while (0)

/* 供命令回调使用的参数集合（具名选项 + 位置参数）。
 *   name     参数名（用于带名字的选项）。
 *   value    参数值。
 *   argv/argc 位置参数数组及其个数。 */
struct vty_arg {
	const char *name;
	const char *value;
	const char **argv;
	int argc;
};

/* 配置读取相关常量与目录分隔：
 *   INTEGRATE_DEFAULT_CONFIG 整合配置文件名（frr.conf）。
 *   VTY_TIMEOUT_DEFAULT      空闲超时默认值（秒）。
 *   VTY_READ_BUFSIZ          读取配置时的行缓冲大小。
 *   VTY_SEND_BUF_MAX         发送缓冲上限（约 16 MiB）。
 *   DIRECTORY_SEP / IS_DIRECTORY_SEP  目录分隔符及其判定宏（非 Windows 为斜杠）。 */
/* Integrated configuration file. */
#define INTEGRATE_DEFAULT_CONFIG "frr.conf"

/* Default time out value */
#define VTY_TIMEOUT_DEFAULT 600

/* Vty read buffer size. */
#define VTY_READ_BUFSIZ 512

/* Vty max send buffer size */
#define VTY_SEND_BUF_MAX 16777216

/* Directory separator. */
#ifndef DIRECTORY_SEP
#define DIRECTORY_SEP '/'
#endif /* DIRECTORY_SEP */

#ifndef IS_DIRECTORY_SEP
#define IS_DIRECTORY_SEP(c) ((c) == DIRECTORY_SEP)
#endif

/* 全局状态：
 *   vty_mgmt_candidate_config  mgmtd 场景下的候选配置指针。
 *   vty_log_commands           是否把每条执行的命令记入日志。
 *   mgmt_daemons / mgmt_daemons_count  受 mgmtd 管理的守护进程名单及其数量。 */
extern struct nb_config *vty_mgmt_candidate_config;
extern bool vty_log_commands;

extern char const *const mgmt_daemons[];
extern uint mgmt_daemons_count;

/* 函数原型：初始化 / 输出 / JSON 输出 / 配置文件读写 / 服务启停 / 配置进入退出 等。
 * 各函数的具体语义见 vty.c 中的实现与注释。 */
/* Prototypes. */
extern void vty_init(struct event_loop *m, bool do_command_logging);
extern void vty_init_vtysh(void);
extern void vty_terminate(void);
extern void vty_reset(void);
extern struct vty *vty_new(void);
extern struct vty *vty_stdio(void (*atclose)(int isexit));

/* 输出相关：
 *   vty_out()      格式化输出到 vty（printf 风格），会先把已缓存的 frame 冲出。
 *   vty_frame()    把一段“上下文起始标记”写入 frame 缓冲，延迟到有真实输出时才打印。
 *   vty_endframe() 清空 frame 缓冲；若缓冲原本为空，则打印 endtext（上下文结束标记）。
 * 另有 vty_set_include()/vty_json*()/vty_pass_fd() 见下。 */
/* - vty_frame() output goes to a buffer (for context-begin markers)
 * - vty_out() will first print this buffer, and clear it
 * - vty_endframe() clears the buffer without printing it, and prints an
 *   extra string if the buffer was empty before (for context-end markers)
 */
extern int vty_out(struct vty *vty, const char *format, ...) PRINTFRR(2, 3);
extern void vty_frame(struct vty *vty, const char *format, ...) PRINTFRR(2, 3);
extern void vty_endframe(struct vty *vty, const char *endtext);
/* 设置输出过滤正则：只有匹配 include 的输出才会显示；regexp 为 NULL 表示取消过滤。
 * 返回：正则编译是否成功。 */
extern bool vty_set_include(struct vty *vty, const char *regexp);
/* JSON 输出：
 *   vty_json()           输出 JSON（带美化换行），失败返回 CMD_* 码。
 *   vty_json_no_pretty() 同上但不美化，输出尽量紧凑。
 *   二者便于写成 return vty_json(...) 的一行式，并内建 json 对象释放。
 *   _no_pretty 即“不添加大量换行、尽可能密集地输出”。 */
/* returns CMD_SUCCESS so you can do a one-line "return vty_json(...)"
 * NULL check and json_object_free() is included.
 *
 * _no_pretty means do not add a bunch of newlines and dump the output
 * as densely as possible.
 */
extern int vty_json(struct vty *vty, struct json_object *json);
extern int vty_json_no_pretty(struct vty *vty, struct json_object *json);
/* JSON 手工拼装辅助：
 *   vty_json_key()   输出一个键（借助 first_key 标志决定是否加前导逗号）。
 *   vty_json_close() 收尾当前对象。
 *   vty_json_empty() 输出空 JSON 对象。 */
void vty_json_key(struct vty *vty, const char *key, bool *first_key);
void vty_json_close(struct vty *vty, bool first_key);
extern void vty_json_empty(struct vty *vty, struct json_object *json);
/* 把 fd 递给 vtysh 客户端（如 terminal monitor 会话）。
 * 调用后 fd 归 VTY 代码所有，用完会自动关闭，调用方不要再碰它。 */
/* post fd to be passed to the vtysh client
 * fd is owned by the VTY code after this and will be closed when done
 */
extern void vty_pass_fd(struct vty *vty, int fd);

/* 配置文件读写：
 *   vty_open_config()      打开指定配置文件（或默认目录下的），返回 FILE* 或 NULL。
 *   vty_read_config()      读取整份配置，返回是否成功。
 *   vty_read_file()        从一个已打开的 FILE* 读取配置。
 *   vty_read_file_finish() 读取收尾：触发相关回调与提交检查。 */
extern FILE *vty_open_config(const char *config_file, char *config_default_dir);
extern bool vty_read_config(struct nb_config *config, const char *config_file,
			    char *config_default_dir);
extern void vty_read_file(struct nb_config *config, FILE *confp);
extern void vty_read_file_finish(struct vty *vty, struct nb_config *config);
/* 杂项：
 *   vty_time_print()   打印时间戳（cr 决定是否加回车）。
 *   vty_serv_start()   在指定地址 / 端口启动 VTY 服务（TCP）。
 *   vty_serv_stop()    停止 VTY 服务。
 *   vty_close()        关闭并释放一个 vty。
 *   vty_get_cwd()      获取当前工作目录。
 *   vty_update_xpath() 配置对象迁移时，批量更新已存 XPath 前缀。 */
extern void vty_time_print(struct vty *vty, int cr);
extern void vty_serv_start(const char *addr, unsigned short port, const char *path);
extern void vty_serv_stop(void);
extern void vty_close(struct vty *vty);
extern char *vty_get_cwd(void);
extern void vty_update_xpath(const char *oldpath, const char *newpath);
/* 配置模式进入 / 退出：
 *   vty_config_enter()     进入配置模式；可指定私有配置、是否独占、是否加文件锁。
 *   vty_config_exit()      退出配置模式。
 *   vty_config_node_exit() 退出当前配置节点，返回 CMD_* 码。 */
extern int vty_config_enter(struct vty *vty, bool private_config,
			    bool exclusive, bool file_lock);
extern void vty_config_exit(struct vty *vty);
extern int vty_config_node_exit(struct vty *vty);
/* 交互外壳与问候：
 *   vty_shell()      以 stdin/stdout 方式运行一个 vty（本地交互外壳）。
 *   vty_shell_serv() 服务端 vtysh 会话的 vty 处理。
 *   vty_hello()      打印 vty 的欢迎 / 问候信息。 */
extern int vty_shell(struct vty *vty);
extern int vty_shell_serv(struct vty *vty);
extern void vty_hello(struct vty *vty);

/* 本地 stdin/stdout vty 的挂起 / 恢复 / 关闭（用于 ^Z 与 SIGTSTP 处理）。 */
/* ^Z / SIGTSTP handling */
extern void vty_stdio_suspend(void);
extern void vty_stdio_resume(void);
extern void vty_stdio_close(void);


/* 判断 vty 是否已关闭：
 *   status 为 VTY_CLOSE 视为已关闭；VTY_SHELL（vtysh）场景下 fd / wfd 均为 -1，
 *   此时改看 of / of_saved 这两个 FILE* 是否可用。 */
/* Applications can check vty status */
static inline bool vty_is_closed(const struct vty *vty)
{
	if (vty == NULL || vty->status == VTY_CLOSE)
		return true;
	/*
	 * VTY_SHELL (vtysh) uses fd = wfd = -1 and outputs
	 * through vty->of (stdout) instead of socket FDs. If FDs are
	 * valid, the VTY can produce output. Otherwise, fall back to
	 * checking the FILE output path used by VTY_SHELL.
	 */
	if (vty->fd >= 0 && vty->wfd >= 0)
		return false;

	return (vty->of == NULL && vty->of_saved == NULL);
}

/*
 * Semi-private APIs for use in mgmtd-vty code
 */
extern void vty_resume_response(struct vty *vty, int ret);

/* mgmtd 前端 CLI 与 vty 交互的回调钩子（由 mgmtd 相关代码注册）：
 *   vty_new_mgmt_cb / vty_close_mgmt_cb                         vty 新建 / 关闭时调用。
 *   vty_config_enter_mgmt_cb / vty_config_node_exit_mgmt_cb     进入 / 退出配置时调用。
 *   nb_cli_apply_changes_mgmt_cb                                应用候选变更时调用。
 *   nb_cli_rpc_mgmt_cb                                          发起 RPC 时调用。 */
/* --------------------------------------------------- */
/* Callbacks for Mgmtd front-end CLI vty modifications */
/* --------------------------------------------------- */
extern void (*vty_new_mgmt_cb)(struct vty *vty);
extern void (*vty_close_mgmt_cb)(struct vty *vty);
extern int (*vty_config_enter_mgmt_cb)(struct vty *vty, bool private_config, bool exclusive,
				       bool file_lock);
extern void (*vty_config_node_exit_mgmt_cb)(struct vty *vty);
extern int (*nb_cli_apply_changes_mgmt_cb)(struct vty *vty, const char *xpath_base_abs);
extern int (*nb_cli_rpc_mgmt_cb)(struct vty *vty, const char *xpath, const struct lyd_node *input);


#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_VTY_H */

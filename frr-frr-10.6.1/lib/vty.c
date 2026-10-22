// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtual terminal [aka TeletYpe] interface routine.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 */

/* ============================================================================
 * vty.c —— VTY（虚拟终端）子系统的核心实现
 *
 * VTY 是 FRR 面向用户交互的统一抽象层，本文件实现它的“引擎”部分：
 *   - 连接的建立与注销：监听 socket、accept 新连接、把每个连接包装成 struct vty，
 *     并维护 vty_sessions / vtysh_sessions / vty_servs 三条全局链表。
 *   - 事件驱动：所有 socket 读写、超时都用 libfrr 的 event 机制挂回调
 *     （见 enum vty_event 与 vty_event()）。
 *   - 输入解析：读入字节流，处理 telnet 的 IAC 控制序列与 NAWS 窗口协商，
 *     按行切分后交给 command.c 的命令解析器执行。
 *   - 输出：vty_out() 是统一的 printf 风格输出入口，内部用输出缓冲 + 可选的
 *     include 正则过滤 + 增量刷新（避免一次性缓存巨量输出）。
 *   - 命令历史：上下键翻历史（hist / hp / hindex）。
 *   - 配置读写：vty_read_config / vty_read_file 读取配置文件；
 *     vty_config_enter / vty_config_exit 管理配置模式的进入退出。
 *   - 分页：输出超过一屏时用 --More-- 分页（vty_more / vty_moreline）。
 *
 * 与其它模块的关系：
 *   - command.c 提供命令树与解析；本文件负责“输入输出 + 会话生命周期”。
 *   - northbound_cli.c 提供配置提交相关辅助。
 *   - vtysh 客户端 / 服务端复用同一套 struct vty，只是 type 不同（VTY_SHELL 等）。
 *
 * 阅读建议：先看 struct vty（定义在 vty.h）的字段分组，再顺着
 * vty_new -> vty_accept -> vty_read 的调用链理解一个交互会话的生命周期，
 * 最后看 vty_out / vty_more 理解输出与分页。
 * ============================================================================
 */

#include <zebra.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <lib/version.h>
#include <sys/types.h>
#include <sys/types.h>
#include <stdio.h>

/* 若系统自带 libcrypt 则直接用其 crypt()；否则在仅有 OpenSSL 可用时，
 * 用 OpenSSL 的 DES_crypt 作为 crypt() 的替代实现（vty_auth 校验密码用）。 */
#ifndef HAVE_LIBCRYPT
#ifdef HAVE_LIBCRYPTO
#include <openssl/des.h>
#define crypt DES_crypt
#endif
#endif

#include "debug.h"
#include "linklist.h"
#include "frrevent.h"
#include "buffer.h"
#include "command.h"
#include "sockunion.h"
#include "memory.h"
#include "log.h"
#include "prefix.h"
#include "filter.h"
#include "vty.h"
#include "privs.h"
#include "network.h"
#include "libfrr.h"
#include "frrstr.h"
#include "lib_errors.h"
#include <libyang/version.h>
#include "northbound_cli.h"
#include "printfrr.h"
#include "json.h"
#include "sockopt.h"
#include "frregex_real.h"
#include "lib/lib_vty.h"

#include <arpa/telnet.h>
#include <termios.h>

#include "lib/config_paths.h"

#include "lib/vty_clippy.c"

/* 本文件使用的内存类型（MTYPE）登记：把不同用途的分配区分开，便于 qmem 统计。
 *   VTY         struct vty 本体。
 *   VTY_SERV    监听服务端结构 struct vty_serv。
 *   VTY_OUT_BUF 输出格式化用的临时缓冲。
 *   VTY_HIST    命令历史字符串。
 *   VTY_REGEX   输出过滤正则结构。 */
DEFINE_MTYPE_STATIC(LIB, VTY, "VTY");
DEFINE_MTYPE_STATIC(LIB, VTY_SERV, "VTY server");
DEFINE_MTYPE_STATIC(LIB, VTY_OUT_BUF, "VTY output buffer");
DEFINE_MTYPE_STATIC(LIB, VTY_HIST, "VTY history");
DEFINE_MTYPE_STATIC(LIB, VTY_REGEX, "VTY filter regex");

DECLARE_DLIST(vtys, struct vty, itm);

/* VTY 事件类型：每个枚举值对应一类可挂到 event loop 上的回调，由 vty_event() 分发。
 *   VTY_SERV         监听 socket 上来了新连接。
 *   VTY_READ / VTY_WRITE  已建立连接的读 / 写就绪。
 *   VTY_TIMEOUT_RESET     重置空闲超时。
 *   VTYSH_SERV / VTYSH_READ / VTYSH_WRITE  仅编译 vtysh 服务端（VTYSH 宏）时启用。 */
/* Vty events */
enum vty_event {
	VTY_SERV,
	VTY_READ,
	VTY_WRITE,
	VTY_TIMEOUT_RESET,
#ifdef VTYSH
	VTYSH_SERV,
	VTYSH_READ,
	VTYSH_WRITE
#endif /* VTYSH */
};

struct nb_config *vty_mgmt_candidate_config;

void (*vty_new_mgmt_cb)(struct vty *vty);
void (*vty_close_mgmt_cb)(struct vty *vty);
int (*vty_config_enter_mgmt_cb)(struct vty *vty, bool private_config, bool exclusive,
				bool file_lock);
void (*vty_config_node_exit_mgmt_cb)(struct vty *vty);
int (*nb_cli_apply_changes_mgmt_cb)(struct vty *vty, const char *xpath_base_abs);
int (*nb_cli_rpc_mgmt_cb)(struct vty *vty, const char *xpath, const struct lyd_node *input);


PREDECL_DLIST(vtyservs);

/* 一个监听中的 VTY 服务端（对应一个监听 socket）。
 *   itm      挂入全局 vtyservs 链表。
 *   sock     监听 socket 的 fd。
 *   vtysh    是否为 vtysh 服务端（决定 accept 之后如何包装会话）。
 *   t_accept 该监听 socket 上等待新连接的 event。 */
struct vty_serv {
	struct vtyservs_item itm;

	int sock;
	bool vtysh;

	struct event *t_accept;
};

DECLARE_DLIST(vtyservs, struct vty_serv, itm);

static void vty_event_serv(enum vty_event event, struct vty_serv *);
static void vty_event(enum vty_event, struct vty *);
static int vtysh_flush(struct vty *vty);

/* 全局状态：
 *   host            来自 command.c 的宿主机信息（如 hostname）。
 *   vty_servs       当前所有监听服务端链表。
 *   vty_sessions    所有普通（telnet / 本地 / 配置读取）vty 会话链表。
 *   vtysh_sessions  所有 vtysh 服务端 vty 会话链表。
 *   vty_timeout_val 空闲超时秒数（默认 VTY_TIMEOUT_DEFAULT）。
 *   vty_accesslist_name / vty_ipv6_accesslist_name  访问控制列表名（IPv4 / IPv6）。
 *   vty_cwd          当前工作目录缓存。
 *   no_password_check  是否跳过登录密码校验。
 *   integrate_default  默认整合配置文件路径。
 *   vty_log_commands(_perm)  是否记录命令日志及其“永久”标志。
 *   VTY_MAX_INCREMENTAL_FLUSH  增量刷新阈值（超过即主动 flush）。 */
/* Extern host structure from command.c */
extern struct host host;

/* active listeners */
static struct vtyservs_head vty_servs[1] = {INIT_DLIST(vty_servs[0])};

/* active connections */
static struct vtys_head vty_sessions[1] = {INIT_DLIST(vty_sessions[0])};
static struct vtys_head vtysh_sessions[1] = {INIT_DLIST(vtysh_sessions[0])};

/* Vty timeout value. */
static unsigned long vty_timeout_val = VTY_TIMEOUT_DEFAULT;

/* Vty access-class command */
static char *vty_accesslist_name = NULL;

/* Vty access-calss for IPv6. */
static char *vty_ipv6_accesslist_name = NULL;

/* Current directory. */
static char vty_cwd[MAXPATHLEN];

/* Login password check. */
static int no_password_check = 0;

/* Integrated configuration file path */
static char integrate_default[] = SYSCONFDIR INTEGRATE_DEFAULT_CONFIG;

bool vty_log_commands;
static bool vty_log_commands_perm;

/* Vty incremental flush threshold size */
static const int VTY_MAX_INCREMENTAL_FLUSH = 1024 * 128;

/* mgmtd 前端：向等待中的 vty 用户回送一个响应码。
 * 参数：ret 为命令返回码。
 * 行为：非“读配置文件”的 vty 会把 4 字节头（低字节为 ret）写入输出缓冲并尝试
 *   flush；若 vty 已请求关闭则关闭它，否则重新挂上读事件继续交互。
 * 说明：仅对非 VTY_FILE 类型发送响应头；VTY_FILE 下出现该情形视为异常并告警。 */
void vty_resume_response(struct vty *vty, int ret)
{
	uint8_t header[4] = {0, 0, 0, 0};

	if (vty->type != VTY_FILE) {
		header[3] = ret;
		buffer_put(vty->obuf, header, 4);
		if (!vty->t_write && (vtysh_flush(vty) < 0)) {
			zlog_err("failed to vtysh_flush");
			/* Try to flush results; exit if a write error occurs */
			return;
		}
	}

	if (vty->status == VTY_CLOSE)
		vty_close(vty);
	else if (vty->type != VTY_FILE)
		vty_event(VTYSH_READ, vty);
	else
		/* should we assert here? */
		zlog_err("mgmtd: unexpected resume while reading config file");
}

/* 把一段“上下文起始标记”格式化写入 frame 缓冲（延迟输出）。
 * 参数：format / ... 为 printf 风格内容。
 * 说明：内容先攒在 vty->frame 里，等真正有 vty_out() 输出时才一并冲出；
 *   若之后一直没有真实输出，这段标记就不会显示（见 vty_endframe）。 */
void vty_frame(struct vty *vty, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintfrr(vty->frame + vty->frame_pos,
		    sizeof(vty->frame) - vty->frame_pos, format, args);
	vty->frame_pos = strlen(vty->frame);
	va_end(args);
}

/* 结束一段 frame：清空 frame 缓冲。
 * 若缓冲原本为空（说明该上下文没有任何真实输出），则打印 endtext 作为结束标记。
 * 参数：endtext 为缓冲为空时要打印的文本（可为 NULL）。 */
void vty_endframe(struct vty *vty, const char *endtext)
{
	if (vty->frame_pos == 0 && endtext)
		vty_out(vty, "%s", endtext);
	vty->frame_pos = 0;
}

/* 设置 / 清除输出过滤正则（只显示匹配 include 的行）。
 * 参数：regexp 为扩展正则；传 NULL 表示清除过滤。
 * 返回：正则编译成功为 true，失败为 false（并已向 vty 报错）。
 * 副作用：编译成功置 vty->filter=true；清除时释放旧正则。 */
bool vty_set_include(struct vty *vty, const char *regexp)
{
	int errcode;
	bool ret = true;
	char errbuf[256];

	if (!regexp) {
		if (vty->filter) {
			regfree(&vty->include->real);
			XFREE(MTYPE_VTY_REGEX, vty->include);
			vty->filter = false;
		}
		return true;
	}

	vty->include = XCALLOC(MTYPE_VTY_REGEX, sizeof(*vty->include));
	errcode = regcomp(&vty->include->real, regexp, REG_EXTENDED | REG_NEWLINE | REG_NOSUB);
	if (errcode) {
		ret = false;
		regerror(errcode, &vty->include->real, errbuf, sizeof(errbuf));
		vty_out(vty, "%% Regex compilation error: %s\n", errbuf);
		XFREE(MTYPE_VTY_REGEX, vty->include);
	} else {
		vty->filter = true;
	}

	return ret;
}

/* VTY 标准输出函数：printf 风格把内容写到 vty。
 * 参数：format / ... 为格式串与可变参数。
 * 返回：成功返回写入字节数（负值表示出错或 vty 已关闭）。
 * 关键行为：若之前有未冲出的 frame 标记会先冲出；输出可能被 vty->filter 正则
 *   按行过滤；内容最终进入 obuf，在合适时机刷新。
 * 坑：格式串里若含 %m 需要保留 errno，故函数开头先保存 errno 再恢复。 */
/* VTY standard output function. */
int vty_out(struct vty *vty, const char *format, ...)
{
	va_list args;
	ssize_t len;
	char buf[1024];
	char *p = NULL;
	char *filtered;
	/* format string may contain %m, keep errno intact for printfrr */
	int saved_errno = errno;
	size_t filt_len;

	/* If this vty has failed, just return */
	if (vty->status == VTY_CLOSE)
		return -1;

	if (vty->frame_pos) {
		vty->frame_pos = 0;
		vty_out(vty, "%s", vty->frame);
	}

	va_start(args, format);
	errno = saved_errno;
	p = vasnprintfrr(MTYPE_VTY_OUT_BUF, buf, sizeof(buf), format, args);
	va_end(args);

	len = strlen(p);

	/* filter buffer */
	if (vty->filter) {
		vector lines = frrstr_split_vec(p, "\n");

		/* Place first value in the cache */
		char *firstline = vector_slot(lines, 0);
		buffer_put(vty->lbuf, (uint8_t *) firstline, strlen(firstline));

		/* If our split returned more than one entry, time to filter */
		if (vector_active(lines) > 1) {
			/*
			 * returned string is MTYPE_TMP so it matches the MTYPE
			 * of everything else in the vector
			 */
			char *bstr = buffer_getstr(vty->lbuf);
			buffer_reset(vty->lbuf);
			XFREE(MTYPE_TMP, lines->index[0]);
			vector_set_index(lines, 0, bstr);
			frrstr_filter_vec(lines, vty->include);
			vector_compact(lines);
			/*
			 * Consider the string "foo\n". If the regex is an empty string
			 * and the line ended with a newline, then the vector will look
			 * like:
			 *
			 * [0]: 'foo'
			 * [1]: ''
			 *
			 * If the regex isn't empty, the vector will look like:
			 *
			 * [0]: 'foo'
			 *
			 * In this case we'd like to preserve the newline, so we add
			 * the empty string [1] as in the first example.
			 */
			if (p[strlen(p) - 1] == '\n' && vector_active(lines) > 0
			    && strlen(vector_slot(lines, vector_active(lines) - 1)))
				vector_set(lines, XSTRDUP(MTYPE_TMP, ""));

			filtered = frrstr_join_vec(lines, "\n");
		}
		else {
			filtered = NULL;
		}

		frrstr_strvec_free(lines);

	} else {
		filtered = p;
	}

	if (!filtered)
		goto done;

	switch (vty->type) {
	case VTY_TERM:
		/* print with crlf replacement */
		buffer_put_crlf(vty->obuf, (uint8_t *)filtered,
				strlen(filtered));
		break;
	case VTY_SHELL:
		if (vty->of) {
			fprintf(vty->of, "%s", filtered);
			fflush(vty->of);
		} else if (vty->of_saved) {
			fprintf(vty->of_saved, "%s", filtered);
			fflush(vty->of_saved);
		}
		break;
	case VTY_SHELL_SERV:
	case VTY_FILE:
	default:
		filt_len = strlen(filtered);
		vty->vty_buf_size_accum += filt_len;
		/* print without crlf replacement */
		buffer_put(vty->obuf, (uint8_t *)filtered, filt_len);

		/* If we've reached the incremental threshold, try
		 * to flush buffered output.
		 */
		if (vty->vty_buf_size_accum >= vty->vty_buf_threshold) {
			vty->vty_buf_size_accum = 0;
			vtysh_flush(vty);
		}
		break;
	}

done:

	if (vty->filter && filtered)
		XFREE(MTYPE_TMP, filtered);

	/* If p is not different with buf, it is allocated buffer.  */
	if (p != buf)
		XFREE(MTYPE_VTY_OUT_BUF, p);

	if (vty->status == VTY_CLOSE)
		return -1;
	else
		return len;
}

/* JSON 输出的公共实现：把 json 对象序列化后经 vty_out 输出并释放。
 * 参数：json 为待输出对象（为 NULL 直接返回 CMD_SUCCESS）；options 传给
 *   json_object_to_json_string_ext，控制美化 / 转义风格。
 * 返回：CMD_SUCCESS；若 vty 已关闭则释放对象并返回 CMD_ERR_NOTHING_TODO。
 * 说明：无论成功与否都会释放 json，调用方交出对象所有权。 */
static int vty_json_helper(struct vty *vty, struct json_object *json,
			   uint32_t options)
{
	const char *text;

	if (!json)
		return CMD_SUCCESS;

	if (vty_is_closed(vty)) {
		json_object_free(json);
		return CMD_ERR_NOTHING_TODO;
	}

	text = json_object_to_json_string_ext(
		json, options);
	vty_out(vty, "%s\n", text);
	json_object_free(json);

	return CMD_SUCCESS;
}

/* 输出 JSON（带美化换行、斜杠不转义）。返回 CMD_* 码，便于 return vty_json(...)。 */
int vty_json(struct vty *vty, struct json_object *json)
{
	return vty_json_helper(vty, json,
			       JSON_C_TO_STRING_PRETTY |
				       JSON_C_TO_STRING_NOSLASHESCAPE);
}

/* 输出 JSON（不美化、尽量紧凑，斜杠不转义）。返回 CMD_* 码。 */
int vty_json_no_pretty(struct vty *vty, struct json_object *json)
{
	return vty_json_helper(vty, json, JSON_C_TO_STRING_NOSLASHESCAPE);
}


/* 手工拼装 JSON：输出一个键。
 * 参数：key 为键名；first_key 为“是否首个键”标志，首键输出 { 前缀，其余输出逗号。
 * 副作用：把 *first_key 置 false。 */
void vty_json_key(struct vty *vty, const char *key, bool *first_key)
{
	vty_out(vty, "%s\"%s\":", *first_key ? "{" : ",", key);
	*first_key = false;
}

/* 手工拼装 JSON：收尾当前对象。
 * 参数：first_key 为 true 表示此前一个键都没写过（对象未开启），此时补一个 {。
 * 最后统一输出 } 并换行。 */
void vty_json_close(struct vty *vty, bool first_key)
{
	if (first_key)
		/* JSON was not opened */
		vty_out(vty, "{");
	vty_out(vty, "}\n");
}

/* 输出一个空 JSON 对象：若传入 json 为 NULL 则新建 {}，否则直接输出传入对象。 */
void vty_json_empty(struct vty *vty, struct json_object *json)
{
	json_object *jsonobj = json;

	if (!json)
		jsonobj = json_object_new_object();

	vty_json(vty, jsonobj);
}

/* 把当前时间戳打印到 vty。
 * 参数：cr 非 0 时结尾加换行，否则加空格（用于行内前缀）。
 * 说明：frr_timestamp 失败时记日志并直接返回，不输出任何内容。 */
/* Output current time to the vty. */
void vty_time_print(struct vty *vty, int cr)
{
	char buf[FRR_TIMESTAMP_LEN];

	if (frr_timestamp(0, buf, sizeof(buf)) == 0) {
		zlog_info("frr_timestamp error");
		return;
	}
	if (cr)
		vty_out(vty, "%s\n", buf);
	else
		vty_out(vty, "%s ", buf);

	return;
}

/* 向 vty 打印欢迎信息（MOTD）。
 * 若配置了 motdfile 则打开该文件逐行输出（输出前去掉行尾空白）；文件打不开则提示；
 * 否则若存在 host.motd 字符串则直接输出。 */
/* Say hello to vty interface. */
void vty_hello(struct vty *vty)
{
	if (host.motdfile) {
		FILE *f;
		char buf[4096];

		f = fopen(host.motdfile, "r");
		if (f) {
			while (fgets(buf, sizeof(buf), f)) {
				char *s;
				/* work backwards to ignore trailling isspace()
				 */
				for (s = buf + strlen(buf);
				     (s > buf) && isspace((unsigned char)s[-1]);
				     s--)
					;
				*s = '\0';
				vty_out(vty, "%s\n", buf);
			}
			fclose(f);
		} else
			vty_out(vty, "MOTD file not found\n");
	} else if (host.motd)
		vty_out(vty, "%s", host.motd);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
/* prompt formatting has a %s in the cmd_node prompt string.
 *
 * Also for some reason GCC emits the warning on the end of the function
 * (optimization maybe?) rather than on the vty_out line, so this pragma
 * wraps the entire function rather than just the vty_out line.
 */

/* 输出命令行提示符（仅对终端型 vty）。
 * 提示符来自 cmd_prompt(当前节点)，其中会做 %s 替换（通常是主机名）。
 * 见上方 pragma：因为提示串是运行期格式串且含 %s，这里临时关闭格式告警。 */
/* Put out prompt and wait input from user. */
static void vty_prompt(struct vty *vty)
{
	if (vty->type == VTY_TERM) {
		vty_out(vty, cmd_prompt(vty->node), cmd_hostname_get());
	}
}
#pragma GCC diagnostic pop

/* 向对端发送 telnet WILL ECHO 选项，协商由本端负责回显。 */
/* Send WILL TELOPT_ECHO to remote server. */
static void vty_will_echo(struct vty *vty)
{
	unsigned char cmd[] = {IAC, WILL, TELOPT_ECHO, '\0'};
	vty_out(vty, "%s", cmd);
}

/* 发送 telnet WILL SGA（抑制 Go-Ahead）选项，避免每行输入的冗余协商。 */
/* Make suppress Go-Ahead telnet option. */
static void vty_will_suppress_go_ahead(struct vty *vty)
{
	unsigned char cmd[] = {IAC, WILL, TELOPT_SGA, '\0'};
	vty_out(vty, "%s", cmd);
}

/* 发送 telnet DONT LINEMODE 选项，禁用远端行模式，改为逐字符交互。 */
/* Make don't use linemode over telnet. */
static void vty_dont_linemode(struct vty *vty)
{
	unsigned char cmd[] = {IAC, DONT, TELOPT_LINEMODE, '\0'};
	vty_out(vty, "%s", cmd);
}

/* 发送 telnet DO NAWS 选项，请求对端上报窗口大小（宽高）。 */
/* Use window size. */
static void vty_do_window_size(struct vty *vty)
{
	unsigned char cmd[] = {IAC, DO, TELOPT_NAWS, '\0'};
	vty_out(vty, "%s", cmd);
}

/* vty 密码认证：按当前节点（AUTH_NODE / AUTH_ENABLE_NODE）取对应密码做校验。
 * 参数：buf 为用户输入的密码。
 * 行为：按 host.encrypt 决定是否用 crypt() 比对；成功则清失败计数并前进到 next_node；
 * 失败累计三次则提示并置 VTY_CLOSE（登录 / enable 两种提示不同）。
 * 说明：密码未设置时一律视为失败。 */
/* Authentication of vty */
static void vty_auth(struct vty *vty, char *buf)
{
	char *passwd = NULL;
	enum node_type next_node = 0;
	int fail;

	switch (vty->node) {
	case AUTH_NODE:
		if (host.encrypt)
			passwd = host.password_encrypt;
		else
			passwd = host.password;
		if (host.advanced)
			next_node = host.enable ? VIEW_NODE : ENABLE_NODE;
		else
			next_node = VIEW_NODE;
		break;
	case AUTH_ENABLE_NODE:
		if (host.encrypt)
			passwd = host.enable_encrypt;
		else
			passwd = host.enable;
		next_node = ENABLE_NODE;
		break;
	}

	if (passwd) {
		if (host.encrypt)
			fail = strcmp(crypt(buf, passwd), passwd);
		else
			fail = strcmp(buf, passwd);
	} else
		fail = 1;

	if (!fail) {
		vty->fail = 0;
		vty->node = next_node; /* Success ! */
	} else {
		vty->fail++;
		if (vty->fail >= 3) {
			if (vty->node == AUTH_NODE) {
				vty_out(vty,
					"%% Bad passwords, too many failures!\n");
				vty->status = VTY_CLOSE;
			} else {
				/* AUTH_ENABLE_NODE */
				vty->fail = 0;
				vty_out(vty,
					"%% Bad enable passwords, too many failures!\n");
				vty->status = VTY_CLOSE;
			}
		}
	}
}

/* 在 vty 上执行一条命令行。
 * 参数：buf 为待执行的命令行文本。
 * 返回：cmd_execute() 的返回码（CMD_SUCCESS / CMD_WARNING / CMD_ERR_* 等）。
 * 行为：若开启命令日志则先把该行记入日志（跳过 echo PING 之类）；
 *   用 rusage 统计命令 CPU / 墙钟耗时，超阈值时告警（CPU HOG / STARVATION）；
 *   执行后在失败时按错误类型打印统一提示。
 * 坑：cputime_enabled 可能被命令本身改动，故先快照 cputime_enabled_here 再比较。 */
/* Command execution over the vty interface. */
static int vty_command(struct vty *vty, char *buf)
{
	int ret;
	const char *protocolname;
	char *cp = NULL;

	assert(vty);

	/*
	 * Log non empty command lines
	 */
	if (vty_log_commands &&
	    strncmp(buf, "echo PING", strlen("echo PING")) != 0)
		cp = buf;
	if (cp != NULL) {
		/* Skip white spaces. */
		while (isspace((unsigned char)*cp) && *cp != '\0')
			cp++;
	}
	if (cp != NULL && *cp != '\0') {
		char vty_str[VTY_BUFSIZ];
		char prompt_str[VTY_BUFSIZ];

		/* format the base vty info */
		snprintf(vty_str, sizeof(vty_str), "vty[%d]@%s", vty->fd,
			 vty->address);

		/* format the prompt */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
		/* prompt formatting has a %s in the cmd_node prompt string */
		snprintf(prompt_str, sizeof(prompt_str), cmd_prompt(vty->node),
			 vty_str);
#pragma GCC diagnostic pop

		/* now log the command */
		zlog_notice("%s%s", prompt_str, buf);
	}

	RUSAGE_T before;
	RUSAGE_T after;
	unsigned long walltime, cputime;

	/* cmd_execute() may change cputime_enabled if we're executing the
	 * "service cputime-stats" command, which can result in nonsensical
	 * and very confusing warnings
	 */
	bool cputime_enabled_here = cputime_enabled;

	GETRUSAGE(&before);

	ret = cmd_execute(vty, buf, NULL, 0);

	GETRUSAGE(&after);

	walltime = event_consumed_time(&after, &before, &cputime);

	if (cputime_enabled_here && cputime_enabled && cputime_threshold
	    && cputime > cputime_threshold)
		/* Warn about CPU hog that must be fixed. */
		flog_warn(EC_LIB_SLOW_THREAD_CPU,
			  "CPU HOG: command took %lums (cpu time %lums): %s",
			  walltime / 1000, cputime / 1000, buf);
	else if (walltime_threshold && walltime > walltime_threshold)
		flog_warn(EC_LIB_SLOW_THREAD_WALL,
			  "STARVATION: command took %lums (cpu time %lums): %s",
			  walltime / 1000, cputime / 1000, buf);

	/* Get the name of the protocol if any */
	protocolname = frr_protoname;

	if (ret != CMD_SUCCESS)
		switch (ret) {
		case CMD_WARNING:
			if (vty->type == VTY_FILE)
				vty_out(vty, "Warning...\n");
			break;
		case CMD_ERR_AMBIGUOUS:
			vty_out(vty, "%% Ambiguous command.\n");
			break;
		case CMD_ERR_NO_MATCH:
			vty_out(vty, "%% [%s] Unknown command: %s\n",
				protocolname, buf);
			break;
		case CMD_ERR_INCOMPLETE:
			vty_out(vty, "%% Command incomplete.\n");
			break;
		}

	return ret;
}

/* 行编辑用的两个特殊字节：退格字符（0x08）与空格字符。 */
static const char telnet_backward_char = 0x08;
static const char telnet_space_char = ' ';

/* 把原始字节写入 vty 输出缓冲。
 * 参数：buf / nbytes 为数据与长度。
 * 说明：在认证节点（AUTH_NODE / AUTH_ENABLE_NODE）下不输出，避免回显敏感输入。
 * 注意：这里只是入缓冲，真正写到 socket 由 vty_flush 完成。 */
/* Basic function to write buffer to vty. */
static void vty_write(struct vty *vty, const char *buf, size_t nbytes)
{
	if ((vty->node == AUTH_NODE) || (vty->node == AUTH_ENABLE_NODE))
		return;

	/* Should we do buffering here ?  And make vty_flush (vty) ? */
	buffer_put(vty->obuf, buf, nbytes);
}

/* 在光标处插入一个字符（insert 模式，右侧内容右移）。
 * 参数：c 为待插入字符。
 * 说明：命令行长度上限 VTY_BUFSIZ；会重绘光标右侧内容并把光标退回到原位。
 * 副作用：改 vty->buf / cp / length 并写输出。 */
/* Basic function to insert character into vty. */
static void vty_self_insert(struct vty *vty, char c)
{
	int i;
	int length;

	if (vty->length + 1 >= VTY_BUFSIZ)
		return;

	length = vty->length - vty->cp;
	memmove(&vty->buf[vty->cp + 1], &vty->buf[vty->cp], length);
	vty->buf[vty->cp] = c;

	vty_write(vty, &vty->buf[vty->cp], length + 1);
	for (i = 0; i < length; i++)
		vty_write(vty, &telnet_backward_char, 1);

	vty->cp++;
	vty->length++;

	vty->buf[vty->length] = '\0';
}

/* 覆盖模式插入字符：光标在行尾等价于普通插入，否则就地覆盖当前字符。
 * 参数：c 为待写入字符。 */
/* Self insert character 'c' in overwrite mode. */
static void vty_self_insert_overwrite(struct vty *vty, char c)
{
	if (vty->cp == vty->length) {
		vty_self_insert(vty, c);
		return;
	}

	vty->buf[vty->cp++] = c;
	vty_write(vty, &c, 1);
}

/* 在光标处插入一个字符串（用于 Tab 补全等），超出 VTY_BUFSIZ 时截断。
 * 参数：str 为待插入字符串。
 * 说明：不改动历史内容，直接覆盖式写入并同步 cp / length。
 * 见下方 doxygen 注释。 */
/**
 * Insert a string into vty->buf at the current cursor position.
 *
 * If the resultant string would be larger than VTY_BUFSIZ it is
 * truncated to fit.
 */
static void vty_insert_word_overwrite(struct vty *vty, char *str)
{
	if (vty->cp == VTY_BUFSIZ)
		return;

	size_t nwrite = MIN((int)strlen(str), VTY_BUFSIZ - vty->cp - 1);
	memcpy(&vty->buf[vty->cp], str, nwrite);
	vty->cp += nwrite;
	vty->length = MAX(vty->cp, vty->length);
	vty->buf[vty->length] = '\0';
	vty_write(vty, str, nwrite);
}

/* 光标右移一个字符（不越过行尾）。 */
/* Forward character. */
static void vty_forward_char(struct vty *vty)
{
	if (vty->cp < vty->length) {
		vty_write(vty, &vty->buf[vty->cp], 1);
		vty->cp++;
	}
}

/* 光标左移一个字符（不越过行首）。 */
/* Backward character. */
static void vty_backward_char(struct vty *vty)
{
	if (vty->cp > 0) {
		vty->cp--;
		vty_write(vty, &telnet_backward_char, 1);
	}
}

/* 光标移到行首（通过连续左移实现）。 */
/* Move to the beginning of the line. */
static void vty_beginning_of_line(struct vty *vty)
{
	while (vty->cp)
		vty_backward_char(vty);
}

/* 光标移到行尾（通过连续右移实现）。 */
/* Move to the end of the line. */
static void vty_end_of_line(struct vty *vty)
{
	while (vty->cp < vty->length)
		vty_forward_char(vty);
}

static void vty_kill_line_from_beginning(struct vty *);
static void vty_redraw_line(struct vty *);

/* 把历史缓冲区中当前 hp 指向的那条命令回显到输入行上。
 * 先清掉从行首到光标的内容，再拷入历史命令并重绘。
 * 见下方英文注释：本函数被 vty_next_line / vty_previous_line 调用。 */
/* Print command line history.  This function is called from
   vty_next_line and vty_previous_line. */
static void vty_history_print(struct vty *vty)
{
	int length;

	vty_kill_line_from_beginning(vty);

	/* Get previous line from history buffer */
	length = strlen(vty->hist[vty->hp]);
	memcpy(vty->buf, vty->hist[vty->hp], length);
	vty->cp = vty->length = length;
	vty->buf[vty->length] = '\0';

	/* Redraw current line */
	vty_redraw_line(vty);
}

/* 显示“下一条”命令历史（向下翻）。
 * hp 为当前条目的环形索引，hindex 为插入端点；hp 已到 hindex 表示已是最新，直接返回。
 * 环形缓冲：加一后越过末尾则回到 0；该槽为空则表示无更“新”的历史，停止。 */
/* Show next command line history. */
static void vty_next_line(struct vty *vty)
{
	int try_index;

	if (vty->hp == vty->hindex)
		return;

	/* Try is there history exist or not. */
	try_index = vty->hp;
	if (try_index == (VTY_MAXHIST - 1))
		try_index = 0;
	else
		try_index++;

	/* If there is not history return. */
	if (vty->hist[try_index] == NULL)
		return;
	else
		vty->hp = try_index;

	vty_history_print(vty);
}

/* 显示“上一条”命令历史（向上翻）。
 * 环形缓冲：减一后越过开头则回到末尾；该槽为空则表示无更“旧”的历史，停止。 */
/* Show previous command line history. */
static void vty_previous_line(struct vty *vty)
{
	int try_index;

	try_index = vty->hp;
	if (try_index == 0)
		try_index = VTY_MAXHIST - 1;
	else
		try_index--;

	if (vty->hist[try_index] == NULL)
		return;
	else
		vty->hp = try_index;

	vty_history_print(vty);
}

/* 重绘整条输入行：把 buf 内容原样写出，并把光标移到行尾（cp=length）。 */
/* This function redraw all of the command line character. */
static void vty_redraw_line(struct vty *vty)
{
	vty_write(vty, vty->buf, vty->length);
	vty->cp = vty->length;
}

/* 光标向右前进一个“词”：先跳过当前词的非空格字符，再跳过其后的空格。 */
/* Forward word. */
static void vty_forward_word(struct vty *vty)
{
	while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
		vty_forward_char(vty);

	while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
		vty_forward_char(vty);
}

/* 光标向左回退一个“纯词”（不跳过前导空格）：仅回退到词首。 */
/* Backward word without skipping training space. */
static void vty_backward_pure_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_backward_char(vty);
}

/* 光标向左回退一个“词”：先跳过左侧空格，再回退到词首。 */
/* Backward word. */
static void vty_backward_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
		vty_backward_char(vty);

	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_backward_char(vty);
}

/* 行首输入 ^D 时“下降一级”：换行后调用 cmd_exit 退出当前节点，再重画提示符。
 * 说明：cmd_exit 负责节点栈的弹出。 */
/* When '^D' is typed at the beginning of the line we move to the down
   level. */
static void vty_down_level(struct vty *vty)
{
	vty_out(vty, "\n");
	cmd_exit(vty);
	vty_prompt(vty);
	vty->cp = 0;
}

/* 收到 ^Z 时回到 enable 模式：若处于配置模式则退出配置并切到 ENABLE_NODE，
 * 然后重画提示符。 */
/* When '^Z' is received from vty, move down to the enable mode. */
static void vty_end_config(struct vty *vty)
{
	vty_out(vty, "\n");

	if (vty->config) {
		vty_config_exit(vty);
		vty->node = ENABLE_NODE;
	}

	vty_prompt(vty);
	vty->cp = 0;
}

/* 删除光标处的字符（delete 键）。
 * 行首（length==0）时触发 vty_down_level 下降一级；光标已在行尾则无操作。
 * 删除后重绘光标右侧内容并用退格把光标回退到原位。 */
/* Delete a character at the current point. */
static void vty_delete_char(struct vty *vty)
{
	int i;
	int size;

	if (vty->length == 0) {
		vty_down_level(vty);
		return;
	}

	if (vty->cp == vty->length)
		return; /* completion need here? */

	size = vty->length - vty->cp;

	vty->length--;
	memmove(&vty->buf[vty->cp], &vty->buf[vty->cp + 1], size - 1);
	vty->buf[vty->length] = '\0';

	if (vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
		return;

	vty_write(vty, &vty->buf[vty->cp], size - 1);
	vty_write(vty, &telnet_space_char, 1);

	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_backward_char, 1);
}

/* 删除光标前一个字符（backspace）：光标先左移，再调用删除。 */
/* Delete a character before the point. */
static void vty_delete_backward_char(struct vty *vty)
{
	if (vty->cp == 0)
		return;

	vty_backward_char(vty);
	vty_delete_char(vty);
}

/* 删除从光标到行尾的所有字符：先用空格覆盖显示，再回退光标并截断 buf。 */
/* Kill rest of line from current point. */
static void vty_kill_line(struct vty *vty)
{
	int i;
	int size;

	size = vty->length - vty->cp;

	if (size == 0)
		return;

	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_space_char, 1);
	for (i = 0; i < size; i++)
		vty_write(vty, &telnet_backward_char, 1);

	memset(&vty->buf[vty->cp], 0, size);
	vty->length = vty->cp;
}

/* 删除从行首到光标的所有字符：先移到行首，再执行 vty_kill_line。 */
/* Kill line from the beginning. */
static void vty_kill_line_from_beginning(struct vty *vty)
{
	vty_beginning_of_line(vty);
	vty_kill_line(vty);
}

/* 向右删除一个词：先删当前词后的空格，再删该词直到下一个空格。 */
/* Delete a word before the point. */
static void vty_forward_kill_word(struct vty *vty)
{
	while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
		vty_delete_char(vty);
	while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
		vty_delete_char(vty);
}

/* 向左删除一个词：先删光标左侧空格，再从词尾删到词首。 */
/* Delete a word before the point. */
static void vty_backward_kill_word(struct vty *vty)
{
	while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
		vty_delete_backward_char(vty);
	while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
		vty_delete_backward_char(vty);
}

/* 交换光标前或光标处的两个字符（^T）。
 * 行太短或光标在行首时不做。光标在行尾则交换最后两个字符，否则交换光标与其前一字符。 */
/* Transpose chars before or at the point. */
static void vty_transpose_chars(struct vty *vty)
{
	char c1, c2;

	/* If length is short or point is near by the beginning of line then
	   return. */
	if (vty->length < 2 || vty->cp < 1)
		return;

	/* In case of point is located at the end of the line. */
	if (vty->cp == vty->length) {
		c1 = vty->buf[vty->cp - 1];
		c2 = vty->buf[vty->cp - 2];

		vty_backward_char(vty);
		vty_backward_char(vty);
		vty_self_insert_overwrite(vty, c1);
		vty_self_insert_overwrite(vty, c2);
	} else {
		c1 = vty->buf[vty->cp];
		c2 = vty->buf[vty->cp - 1];

		vty_backward_char(vty);
		vty_self_insert_overwrite(vty, c1);
		vty_self_insert_overwrite(vty, c2);
	}
}

/* 处理 Tab 补全：把当前行交给 cmd_complete_command，再按返回类型分别处理。
 *   FULL_MATCH / MATCH：回写补全结果（FULL_MATCH 还会补一个空格）。
 *   LIST_MATCH：列出所有候选（每行 6 个）。
 *   AMBIGUOUS / NO_MATCH：仅打印提示并重画。
 * 参数：vty。返回：void。会释放 matched 中的补全字符串。
 * 坑：FULL_MATCH 却拿到 NULL 时视为 CLI BUG（历史上曾在此 SEGV）。 */
/* Do completion at vty interface. */
static void vty_complete_command(struct vty *vty)
{
	int i;
	int ret;
	char **matched = NULL;
	vector vline;

	if (vty->node == AUTH_NODE || vty->node == AUTH_ENABLE_NODE)
		return;

	vline = cmd_make_strvec(vty->buf);
	if (vline == NULL)
		return;

	/* In case of 'help \t'. */
	if (isspace((unsigned char)vty->buf[vty->length - 1]))
		vector_set(vline, NULL);

	matched = cmd_complete_command(vline, vty, &ret);

	cmd_free_strvec(vline);

	vty_out(vty, "\n");
	switch (ret) {
	case CMD_ERR_AMBIGUOUS:
		vty_out(vty, "%% Ambiguous command.\n");
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_ERR_NO_MATCH:
		/* vty_out (vty, "%% There is no matched command.\n"); */
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_COMPLETE_FULL_MATCH:
		if (!matched[0]) {
			/* 2016-11-28 equinox -- need to debug, SEGV here */
			vty_out(vty, "%% CLI BUG: FULL_MATCH with NULL str\n");
			vty_prompt(vty);
			vty_redraw_line(vty);
			break;
		}
		vty_prompt(vty);
		vty_redraw_line(vty);
		vty_backward_pure_word(vty);
		vty_insert_word_overwrite(vty, matched[0]);
		vty_self_insert(vty, ' ');
		XFREE(MTYPE_COMPLETION, matched[0]);
		break;
	case CMD_COMPLETE_MATCH:
		vty_prompt(vty);
		vty_redraw_line(vty);
		vty_backward_pure_word(vty);
		vty_insert_word_overwrite(vty, matched[0]);
		XFREE(MTYPE_COMPLETION, matched[0]);
		break;
	case CMD_COMPLETE_LIST_MATCH:
		for (i = 0; matched[i] != NULL; i++) {
			if (i != 0 && ((i % 6) == 0))
				vty_out(vty, "\n");
			vty_out(vty, "%-10s ", matched[i]);
			XFREE(MTYPE_COMPLETION, matched[i]);
		}
		vty_out(vty, "\n");

		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	case CMD_ERR_NOTHING_TODO:
		vty_prompt(vty);
		vty_redraw_line(vty);
		break;
	default:
		break;
	}
	XFREE(MTYPE_TMP, matched);
}

/* 打印一条命令的帮助描述，必要时按宽度折行。
 * 参数：cmd_width 命令名列宽；desc_width 描述列宽（<=0 表示不折行）；token 为命令词元。
 * 说明：按空格寻找折行点逐段输出；每段之后命令名列清空只留对齐空格。 */
static void vty_describe_fold(struct vty *vty, int cmd_width,
			      unsigned int desc_width, struct cmd_token *token)
{
	char *buf;
	const char *cmd, *p;
	int pos;

	cmd = token->text;

	if (desc_width <= 0) {
		vty_out(vty, "  %-*s  %s\n", cmd_width, cmd, token->desc);
		return;
	}

	buf = XCALLOC(MTYPE_TMP, strlen(token->desc) + 1);

	for (p = token->desc; strlen(p) > desc_width; p += pos + 1) {
		for (pos = desc_width; pos > 0; pos--)
			if (*(p + pos) == ' ')
				break;

		if (pos == 0)
			break;

		memcpy(buf, p, pos);
		buf[pos] = '\0';
		vty_out(vty, "  %-*s  %s\n", cmd_width, cmd, buf);

		cmd = "";
	}

	vty_out(vty, "  %-*s  %s\n", cmd_width, cmd, p);

	XFREE(MTYPE_TMP, buf);
}

/* 处理 '?'：列出当前输入可匹配的命令及其帮助。
 * 把当前行解析成词向量交给 cmd_describe_command 得到描述列表；
 * 先统计最宽命令名以对齐，再逐项调用 vty_describe_fold 输出。
 * 歧义 / 无匹配时打印对应提示。 */
/* Describe matched command function. */
static void vty_describe_command(struct vty *vty)
{
	int ret;
	vector vline;
	vector describe;
	unsigned int i, width, desc_width;
	struct cmd_token *token, *token_cr = NULL;

	vline = cmd_make_strvec(vty->buf);

	/* In case of '> ?'. */
	if (vline == NULL) {
		vline = vector_init(1);
		vector_set(vline, NULL);
	} else if (isspace((unsigned char)vty->buf[vty->length - 1]))
		vector_set(vline, NULL);

	describe = cmd_describe_command(vline, vty, &ret);

	vty_out(vty, "\n");

	/* Ambiguous error. */
	switch (ret) {
	case CMD_ERR_AMBIGUOUS:
		vty_out(vty, "%% Ambiguous command.\n");
		goto out;
		break;
	case CMD_ERR_NO_MATCH:
		vty_out(vty, "%% There is no matched command.\n");
		goto out;
		break;
	}

	/* Get width of command string. */
	width = 0;
	for (i = 0; i < vector_active(describe); i++)
		if ((token = vector_slot(describe, i)) != NULL) {
			unsigned int len;

			if (token->text[0] == '\0')
				continue;

			len = strlen(token->text);

			if (width < len)
				width = len;
		}

	/* Get width of description string. */
	desc_width = vty->width - (width + 6);

	/* Print out description. */
	for (i = 0; i < vector_active(describe); i++)
		if ((token = vector_slot(describe, i)) != NULL) {
			if (token->text[0] == '\0')
				continue;

			if (strcmp(token->text, CMD_CR_TEXT) == 0) {
				token_cr = token;
				continue;
			}

			if (!token->desc)
				vty_out(vty, "  %-s\n", token->text);
			else if (desc_width >= strlen(token->desc))
				vty_out(vty, "  %-*s  %s\n", width, token->text,
					token->desc);
			else
				vty_describe_fold(vty, width, desc_width,
						  token);

			if (IS_VARYING_TOKEN(token->type)) {
				const char *ref = vector_slot(
					vline, vector_active(vline) - 1);

				vector varcomps = vector_init(VECTOR_MIN_SIZE);
				cmd_variable_complete(token, ref, varcomps);

				if (vector_active(varcomps) > 0) {
					char *ac = cmd_variable_comp2str(
						varcomps, vty->width);
					vty_out(vty, "%s\n", ac);
					XFREE(MTYPE_TMP, ac);
				}

				vector_free(varcomps);
			}
		}

	if ((token = token_cr)) {
		if (!token->desc)
			vty_out(vty, "  %-s\n", token->text);
		else if (desc_width >= strlen(token->desc))
			vty_out(vty, "  %-*s  %s\n", width, token->text,
				token->desc);
		else
			vty_describe_fold(vty, width, desc_width, token);
	}

out:
	cmd_free_strvec(vline);
	if (describe)
		vector_free(describe);

	vty_prompt(vty);
	vty_redraw_line(vty);
}

/* 清空命令输入缓冲：把 vty->buf 的前 vty->max 字节清零。 */
static void vty_clear_buf(struct vty *vty)
{
	memset(vty->buf, 0, vty->max);
}

/* ^C：中止当前输入，并且不把该行计入历史。
 * 清空行缓冲与输入缓冲、换行；若在配置模式则退出配置回到 enable 模式；
 * 重画提示符并把历史指针指向最新条目。 */
/* ^C stop current input and do not add command line to the history. */
static void vty_stop_input(struct vty *vty)
{
	vty->cp = vty->length = 0;
	vty_clear_buf(vty);
	vty_out(vty, "\n");

	if (vty->config) {
		vty_config_exit(vty);
		vty->node = ENABLE_NODE;
	}

	vty_prompt(vty);

	/* Set history pointer to the latest one. */
	vty->hp = vty->hindex;
}

/* 把当前命令行加入历史缓冲（环形）。
 * 空行不加；与上一条完全相同的命令跳过（只把 hp 指到最新）。
 * 索引：hindex 为插入位置，写完后 hindex 前移并在 VTY_MAXHIST 处回绕；
 * hp 同步指向“最新下一条”。 */
/* Add current command line to the history buffer. */
static void vty_hist_add(struct vty *vty)
{
	int index;

	if (vty->length == 0)
		return;

	index = vty->hindex ? vty->hindex - 1 : VTY_MAXHIST - 1;

	/* Ignore the same string as previous one. */
	if (vty->hist[index])
		if (strcmp(vty->buf, vty->hist[index]) == 0) {
			vty->hp = vty->hindex;
			return;
		}

	/* Insert history entry. */
	XFREE(MTYPE_VTY_HIST, vty->hist[vty->hindex]);
	vty->hist[vty->hindex] = XSTRDUP(MTYPE_VTY_HIST, vty->buf);

	/* History index rotation. */
	vty->hindex++;
	if (vty->hindex == VTY_MAXHIST)
		vty->hindex = 0;

	vty->hp = vty->hindex;
}

/* #define TELNET_OPTION_DEBUG */

/* 解析一个 telnet 选项序列（IAC ...），返回是否已消费掉该控制字节。
 * 参数：buf / nbytes 为收到的字节及其长度。
 * 返回：1 表示本字节属于 telnet 控制序列、已被处理（调用方不应当作普通输入）；0 表示无需额外处理。
 * 关键逻辑：SB 开始子协商并置 iac_sb_in_progress；SE 结束子协商，若是 NAWS 则按
 *   RFC 1073 从 sb_buf 解析宽高（并校验长度，违反协议时报错）。
 * 调试：定义 TELNET_OPTION_DEBUG 可打印每个控制字节。 */
/* Get telnet window size. */
static int vty_telnet_option(struct vty *vty, unsigned char *buf, int nbytes)
{
#ifdef TELNET_OPTION_DEBUG
	int i;

	for (i = 0; i < nbytes; i++) {
		switch (buf[i]) {
		case IAC:
			vty_out(vty, "IAC ");
			break;
		case WILL:
			vty_out(vty, "WILL ");
			break;
		case WONT:
			vty_out(vty, "WONT ");
			break;
		case DO:
			vty_out(vty, "DO ");
			break;
		case DONT:
			vty_out(vty, "DONT ");
			break;
		case SB:
			vty_out(vty, "SB ");
			break;
		case SE:
			vty_out(vty, "SE ");
			break;
		case TELOPT_ECHO:
			vty_out(vty, "TELOPT_ECHO \n");
			break;
		case TELOPT_SGA:
			vty_out(vty, "TELOPT_SGA \n");
			break;
		case TELOPT_NAWS:
			vty_out(vty, "TELOPT_NAWS \n");
			break;
		default:
			vty_out(vty, "%x ", buf[i]);
			break;
		}
	}
	vty_out(vty, "\n");

#endif /* TELNET_OPTION_DEBUG */

	switch (buf[0]) {
	case SB:
		vty->sb_len = 0;
		vty->iac_sb_in_progress = 1;
		return 0;
	case SE: {
		if (!vty->iac_sb_in_progress)
			return 0;

		if ((vty->sb_len == 0) || (vty->sb_buf[0] == '\0')) {
			vty->iac_sb_in_progress = 0;
			return 0;
		}
		switch (vty->sb_buf[0]) {
		case TELOPT_NAWS:
			if (vty->sb_len != TELNET_NAWS_SB_LEN)
				flog_err(
					EC_LIB_SYSTEM_CALL,
					"RFC 1073 violation detected: telnet NAWS option should send %d characters, but we received %lu",
					TELNET_NAWS_SB_LEN,
					(unsigned long)vty->sb_len);
			else if (sizeof(vty->sb_buf) < TELNET_NAWS_SB_LEN)
				flog_err(
					EC_LIB_DEVELOPMENT,
					"Bug detected: sizeof(vty->sb_buf) %lu < %d, too small to handle the telnet NAWS option",
					(unsigned long)sizeof(vty->sb_buf),
					TELNET_NAWS_SB_LEN);
			else {
				vty->width = ((vty->sb_buf[1] << 8)
					      | vty->sb_buf[2]);
				vty->height = ((vty->sb_buf[3] << 8)
					       | vty->sb_buf[4]);
#ifdef TELNET_OPTION_DEBUG
				vty_out(vty,
					"TELNET NAWS window size negotiation completed: width %d, height %d\n",
					vty->width, vty->height);
#endif
			}
			break;
		}
		vty->iac_sb_in_progress = 0;
		return 0;
	}
	default:
		break;
	}
	return 1;
}

/* 执行当前输入行。
 * 认证节点（AUTH_NODE / AUTH_ENABLE_NODE）走 vty_auth 做密码校验；
 * 其它节点调用 vty_command，终端型还会把命令加入历史。
 * 返回：命令结果码（认证节点下初始为 CMD_SUCCESS）。
 * 执行后清空行缓冲与输入缓冲。 */
/* Execute current command line. */
static int vty_execute(struct vty *vty)
{
	int ret;

	ret = CMD_SUCCESS;

	switch (vty->node) {
	case AUTH_NODE:
	case AUTH_ENABLE_NODE:
		vty_auth(vty, vty->buf);
		break;
	default:
		ret = vty_command(vty, vty->buf);
		if (vty->type == VTY_TERM)
			vty_hist_add(vty);
		break;
	}

	/* Clear command line buffer. */
	vty->cp = vty->length = 0;
	vty_clear_buf(vty);

	if (vty->status != VTY_CLOSE)
		vty_prompt(vty);

	return ret;
}

#define CONTROL(X)  ((X) - '@')
#define VTY_NORMAL     0
#define VTY_PRE_ESCAPE 1
#define VTY_ESCAPE     2
#define VTY_CR         3

/* 行编辑状态与辅助宏：
 *   CONTROL(X)     把控制字母 X 转成对应控制字符（X 减 @），如 CONTROL('C') 为 0x03。
 *   VTY_NORMAL     普通输入状态。
 *   VTY_PRE_ESCAPE 收到 ESC 后的“预转义”状态（等下一字节判断是不是 [）。
 *   VTY_ESCAPE     进入转义序列状态（如方向键的 ESC [ A）。
 *   VTY_CR         刚收到回车、等待可能紧随的换行。
 * 这些状态存放在 vty->escape 字段，由 vty_read 驱动。 */
/* 处理 ESC [ 之后的转义字符（方向键等）。
 *   A 上 / B 下 / C 右 / D 左。
 * 处理完把状态复位为 VTY_NORMAL。 */
/* Escape character command map. */
static void vty_escape_map(unsigned char c, struct vty *vty)
{
	switch (c) {
	case ('A'):
		vty_previous_line(vty);
		break;
	case ('B'):
		vty_next_line(vty);
		break;
	case ('C'):
		vty_forward_char(vty);
		break;
	case ('D'):
		vty_backward_char(vty);
		break;
	default:
		break;
	}

	/* Go back to normal mode. */
	vty->escape = VTY_NORMAL;
}

/* 丢弃缓冲中的待输出内容，并重画提示符与当前行。
 * 用于分页时按 q / ^C 退出、或读取出错后清理。 */
/* Quit print out to the buffer. */
static void vty_buffer_reset(struct vty *vty)
{
	buffer_reset(vty->obuf);
	buffer_reset(vty->lbuf);
	vty_prompt(vty);
	vty_redraw_line(vty);
}

/* vty socket 的读事件回调：读一块字节并逐字符处理。
 * 处理链：telnet IAC 转义 -> NAWS 子协商收集 -> telnet 选项解析 ->
 *   分页(VTY_MORE)时只认 q/Q/^C -> 转义序列(方向键) -> 控制字符
 *   (^A/^B/^C/^D/^E/^K/^N/^P/^T/^U/^W/^Z 等) -> 回车/换行时执行命令。
 * 结束：若已请求关闭则 vty_close，否则重新挂读写事件继续交互。
 * 坑：CR 后紧跟的 LF 必须丢弃，否则会多打一行空提示符（见 VTY_CR 分支）。 */
/* Read data via vty socket. */
static void vty_read(struct event *event)
{
	int i;
	int nbytes;
	unsigned char buf[VTY_READ_BUFSIZ];

	struct vty *vty = EVENT_ARG(event);

	/* Read raw data from socket */
	if ((nbytes = read(vty->fd, buf, VTY_READ_BUFSIZ)) <= 0) {
		if (nbytes < 0) {
			if (ERRNO_IO_RETRY(errno)) {
				vty_event(VTY_READ, vty);
				return;
			}
			flog_err(
				EC_LIB_SOCKET,
				"%s: read error on vty client fd %d, closing: %s",
				__func__, vty->fd, safe_strerror(errno));
			buffer_reset(vty->obuf);
			buffer_reset(vty->lbuf);
		}
		vty->status = VTY_CLOSE;
	}

	for (i = 0; i < nbytes; i++) {
		if (buf[i] == IAC) {
			if (!vty->iac) {
				vty->iac = 1;
				continue;
			} else {
				vty->iac = 0;
			}
		}

		if (vty->iac_sb_in_progress && !vty->iac) {
			if (vty->sb_len < sizeof(vty->sb_buf))
				vty->sb_buf[vty->sb_len] = buf[i];
			vty->sb_len++;
			continue;
		}

		if (vty->iac) {
			/* In case of telnet command */
			int ret = 0;
			ret = vty_telnet_option(vty, buf + i, nbytes - i);
			vty->iac = 0;
			i += ret;
			continue;
		}


		if (vty->status == VTY_MORE) {
			switch (buf[i]) {
			case CONTROL('C'):
			case 'q':
			case 'Q':
				vty_buffer_reset(vty);
				break;
			default:
				break;
			}
			continue;
		}

		/* Escape character. */
		if (vty->escape == VTY_ESCAPE) {
			vty_escape_map(buf[i], vty);
			continue;
		}

		/* Pre-escape status. */
		if (vty->escape == VTY_PRE_ESCAPE) {
			switch (buf[i]) {
			case '[':
				vty->escape = VTY_ESCAPE;
				break;
			case 'b':
				vty_backward_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case 'f':
				vty_forward_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case 'd':
				vty_forward_kill_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			case CONTROL('H'):
			case 0x7f:
				vty_backward_kill_word(vty);
				vty->escape = VTY_NORMAL;
				break;
			default:
				vty->escape = VTY_NORMAL;
				break;
			}
			continue;
		}

		if (vty->escape == VTY_CR) {
			/* if we get CR+NL, the NL results in an extra empty
			 * prompt line being printed without this;  just drop
			 * the NL if it immediately follows CR.
			 */
			vty->escape = VTY_NORMAL;

			if (buf[i] == '\n')
				continue;
		}

		switch (buf[i]) {
		case CONTROL('A'):
			vty_beginning_of_line(vty);
			break;
		case CONTROL('B'):
			vty_backward_char(vty);
			break;
		case CONTROL('C'):
			vty_stop_input(vty);
			break;
		case CONTROL('D'):
			vty_delete_char(vty);
			break;
		case CONTROL('E'):
			vty_end_of_line(vty);
			break;
		case CONTROL('F'):
			vty_forward_char(vty);
			break;
		case CONTROL('H'):
		case 0x7f:
			vty_delete_backward_char(vty);
			break;
		case CONTROL('K'):
			vty_kill_line(vty);
			break;
		case CONTROL('N'):
			vty_next_line(vty);
			break;
		case CONTROL('P'):
			vty_previous_line(vty);
			break;
		case CONTROL('T'):
			vty_transpose_chars(vty);
			break;
		case CONTROL('U'):
			vty_kill_line_from_beginning(vty);
			break;
		case CONTROL('W'):
			vty_backward_kill_word(vty);
			break;
		case CONTROL('Z'):
			vty_end_config(vty);
			break;
		case '\r':
			vty->escape = VTY_CR;
			fallthrough;
		case '\n':
			vty_out(vty, "\n");
			buffer_flush_available(vty->obuf, vty->wfd);
			vty_execute(vty);

			if (vty->pass_fd != -1) {
				close(vty->pass_fd);
				vty->pass_fd = -1;
			}
			break;
		case '\t':
			vty_complete_command(vty);
			break;
		case '?':
			if (vty->node == AUTH_NODE
			    || vty->node == AUTH_ENABLE_NODE)
				vty_self_insert(vty, buf[i]);
			else
				vty_describe_command(vty);
			break;
		case '\033':
			if (i + 1 < nbytes && buf[i + 1] == '[') {
				vty->escape = VTY_ESCAPE;
				i++;
			} else
				vty->escape = VTY_PRE_ESCAPE;
			break;
		default:
			if (buf[i] > 31 && buf[i] < 127)
				vty_self_insert(vty, buf[i]);
			break;
		}
	}

	/* Check status. */
	if (vty->status == VTY_CLOSE)
		vty_close(vty);
	else {
		vty_event(VTY_WRITE, vty);
		vty_event(VTY_READ, vty);
	}
}

/* vty 写事件回调：把输出缓冲刷新到 wfd。
 * 若处于分页状态（VTY_MORE / VTY_MORELINE）先做擦除处理；
 * width / height / lines 未知（为 0）时按“无分页”直接 flush，否则走带分页控制的刷新。 */
/* Flush buffer to the vty. */
static void vty_flush(struct event *event)
{
	int erase;
	buffer_status_t flushrc;
	struct vty *vty = EVENT_ARG(event);

	/* Tempolary disable read event. */
	if (vty->lines == 0)
		event_cancel(&vty->t_read);

	/* Function execution continue. */
	erase = ((vty->status == VTY_MORE || vty->status == VTY_MORELINE));

	/* N.B. if width is 0, that means we don't know the window size. */
	if ((vty->lines == 0) || (vty->width == 0) || (vty->height == 0))
		flushrc = buffer_flush_available(vty->obuf, vty->wfd);
	else if (vty->status == VTY_MORELINE)
		flushrc = buffer_flush_window(vty->obuf, vty->wfd, vty->width,
					      1, erase, 0);
	else
		flushrc = buffer_flush_window(
			vty->obuf, vty->wfd, vty->width,
			vty->lines >= 0 ? vty->lines : vty->height, erase, 0);
	switch (flushrc) {
	case BUFFER_ERROR:
		zlog_info("buffer_flush failed on vty client fd %d/%d, closing",
			  vty->fd, vty->wfd);
		buffer_reset(vty->lbuf);
		buffer_reset(vty->obuf);
		vty_close(vty);
		return;
	case BUFFER_EMPTY:
		if (vty->status == VTY_CLOSE)
			vty_close(vty);
		else {
			vty->status = VTY_NORMAL;
			if (vty->lines == 0)
				vty_event(VTY_READ, vty);
		}
		break;
	case BUFFER_PENDING:
		/* There is more data waiting to be written. */
		vty->status = VTY_MORE;
		if (vty->lines == 0)
			vty_event(VTY_WRITE, vty);
		break;
	}
}

/* 分配并初始化一个新的 struct vty（不绑定 socket）。
 * 默认 fd/wfd = -1、of = stdout、输入/输出缓冲新建、命令缓冲 VTY_BUFSIZ、
 * pass_fd = -1；若注册了 vty_new_mgmt_cb 则回调它。
 * 返回：新分配的 vty（MTYPE_VTY）。 */
/* Allocate new vty struct. */
struct vty *vty_new(void)
{
	struct vty *new = XCALLOC(MTYPE_VTY, sizeof(struct vty));

	new->fd = new->wfd = -1;
	new->of = stdout;
	new->lbuf = buffer_new(0);
	new->obuf = buffer_new(0); /* Use default buffer size. */
	new->buf = XCALLOC(MTYPE_VTY, VTY_BUFSIZ);
	new->max = VTY_BUFSIZ;
	new->pass_fd = -1;

	if (vty_new_mgmt_cb)
		vty_new_mgmt_cb(new);

	return new;
}


/* 在 vty_new 基础上做“网络会话”初始化：绑定 socket、类型设为终端、
 * 初始节点 AUTH_NODE、清空命令缓冲与历史、重置 xpath 与状态，
 * 并把 vty 挂入全局 vty_sessions 链表。
 * 参数：vty_sock 为已 accept 的连接 fd。返回：初始化好的 vty。 */
/* allocate and initialise vty */
static struct vty *vty_new_init(int vty_sock)
{
	struct vty *vty;

	vty = vty_new();
	vty->fd = vty_sock;
	vty->wfd = vty_sock;
	vty->type = VTY_TERM;
	vty->node = AUTH_NODE;
	vty->fail = 0;
	vty->cp = 0;
	vty_clear_buf(vty);
	vty->length = 0;
	memset(vty->hist, 0, sizeof(vty->hist));
	vty->hp = 0;
	vty->hindex = 0;
	vty->xpath_index = 0;
	memset(vty->xpath, 0, sizeof(vty->xpath));
	vty->private_config = false;
	vty->candidate_config = vty_shared_candidate_config;
	vty->status = VTY_NORMAL;
	vty->lines = -1;
	vty->iac = 0;
	vty->iac_sb_in_progress = 0;
	vty->sb_len = 0;

	vtys_add_tail(vty_sessions, vty);

	return vty;
}

/* 创建一个面向网络连接的 vty：在 vty_new_init 之上设置超时、对端地址，
 * 并按 no_password_check / host 配置决定初始节点（ENABLE / VIEW）。
 * 若未设置密码且需要校验，则打印提示、关闭并返回 NULL（拒绝连接）。
 * 否则打印欢迎信息、协商终端选项，挂上读写事件并返回 vty。 */
/* Create new vty structure. */
static struct vty *vty_create(int vty_sock, union sockunion *su)
{
	char buf[SU_ADDRSTRLEN];
	struct vty *vty;

	sockunion2str(su, buf, SU_ADDRSTRLEN);

	/* Allocate new vty structure and set up default values. */
	vty = vty_new_init(vty_sock);

	/* configurable parameters not part of basic init */
	vty->v_timeout = vty_timeout_val;
	strlcpy(vty->address, buf, sizeof(vty->address));
	if (no_password_check) {
		if (host.advanced)
			vty->node = ENABLE_NODE;
		else
			vty->node = VIEW_NODE;
	}
	if (host.lines >= 0)
		vty->lines = host.lines;

	if (!no_password_check) {
		/* Vty is not available if password isn't set. */
		if (host.password == NULL && host.password_encrypt == NULL) {
			vty_out(vty, "Vty password is not set.\n");
			vty->status = VTY_CLOSE;
			vty_close(vty);
			return NULL;
		}
	}

	/* Say hello to the world. */
	vty_hello(vty);
	if (!no_password_check)
		vty_out(vty, "\nUser Access Verification\n\n");

	/* Setting up terminal. */
	vty_will_echo(vty);
	vty_will_suppress_go_ahead(vty);

	vty_dont_linemode(vty);
	vty_do_window_size(vty);
	/* vty_dont_lflow_ahead (vty); */

	vty_prompt(vty);

	/* Add read/write thread. */
	vty_event(VTY_WRITE, vty);
	vty_event(VTY_READ, vty);

	return vty;
}

/* 本地 stdio（stdin/stdout）vty 的私有状态：
 *   stdio_orig_termios  进入前保存的终端属性，退出时恢复。
 *   stdio_vty           当前 stdio vty（全局单例，禁止同时开两个）。
 *   stdio_termios       终端属性是否已被我们改写。
 *   stdio_vty_atclose   关闭时的用户回调。 */
/* create vty for stdio */
static struct termios stdio_orig_termios;
static struct vty *stdio_vty = NULL;
static bool stdio_termios = false;
static void (*stdio_vty_atclose)(int isexit);

/* 复位 stdio vty：恢复原始终端属性、清空单例、调用关闭回调。
 * 参数：isexit 透传给 atclose 回调（区分是否进程退出）。 */
static void vty_stdio_reset(int isexit)
{
	if (stdio_vty) {
		if (stdio_termios)
			tcsetattr(0, TCSANOW, &stdio_orig_termios);
		stdio_termios = false;

		stdio_vty = NULL;

		if (stdio_vty_atclose)
			stdio_vty_atclose(isexit);
		stdio_vty_atclose = NULL;
	}
}

/* 进程退出时的清理钩子：调用 vty_stdio_reset(1)。 */
static void vty_stdio_atexit(void)
{
	vty_stdio_reset(1);
}

/* 挂起 stdio vty（用于 ^Z / SIGTSTP）：取消读写与超时事件并恢复原始终端属性。 */
void vty_stdio_suspend(void)
{
	if (!stdio_vty)
		return;

	event_cancel(&stdio_vty->t_write);
	event_cancel(&stdio_vty->t_read);
	event_cancel(&stdio_vty->t_timeout);

	if (stdio_termios)
		tcsetattr(0, TCSANOW, &stdio_orig_termios);
	stdio_termios = false;
}

/* 恢复 stdio vty：把终端切到原始（raw）模式（关行缓冲、回显等），重画提示符，
 * 并重新挂上读写事件。若无 stdio vty 则直接返回。 */
void vty_stdio_resume(void)
{
	if (!stdio_vty)
		return;

	if (!tcgetattr(0, &stdio_orig_termios)) {
		struct termios termios;

		termios = stdio_orig_termios;
		termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR
				     | IGNCR | ICRNL | IXON);
		termios.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
		termios.c_cflag &= ~(CSIZE | PARENB);
		termios.c_cflag |= CS8;
		tcsetattr(0, TCSANOW, &termios);
		stdio_termios = true;
	}

	vty_prompt(stdio_vty);

	/* Add read/write thread. */
	vty_event(VTY_WRITE, stdio_vty);
	vty_event(VTY_READ, stdio_vty);
}

/* 关闭 stdio vty（若存在）。 */
void vty_stdio_close(void)
{
	if (!stdio_vty)
		return;
	vty_close(stdio_vty);
}

/* 在本进程 stdio 上创建一个 vty（本地交互）。
 * 参数：atclose 为关闭回调。返回：新 vty；若已存在 stdio vty 则返回 NULL。
 * 说明：wfd 设为 1，节点固定为 ENABLE_NODE、超时置 0、地址记为 console，
 * 以保证脚本化调用行为稳定（配置不会影响它）。 */
struct vty *vty_stdio(void (*atclose)(int isexit))
{
	struct vty *vty;

	/* refuse creating two vtys on stdio */
	if (stdio_vty)
		return NULL;

	vty = stdio_vty = vty_new_init(0);
	stdio_vty_atclose = atclose;
	vty->wfd = 1;

	/* always have stdio vty in a known _unchangeable_ state, don't want
	 * config
	 * to have any effect here to make sure scripting this works as intended
	 */
	vty->node = ENABLE_NODE;
	vty->v_timeout = 0;
	strlcpy(vty->address, "console", sizeof(vty->address));

	vty_stdio_resume();
	return vty;
}

/* 监听 socket 的连接到达回调：accept 一个新连接并做准入检查。
 * 流程：把对端地址转成 prefix；按 IPv4 / IPv6 分别套用访问控制列表（DENY 则拒绝）；
 * 设置 TCP_NODELAY 等选项，最后创建 vty 会话。
 * 参数：event 的 arg 为 struct vty_serv（accept_sock 的来源）。 */
/* Accept connection from the network. */
static void vty_accept(struct event *event)
{
	struct vty_serv *vtyserv = EVENT_ARG(event);
	int vty_sock;
	union sockunion su;
	int ret;
	unsigned int on;
	int accept_sock = vtyserv->sock;
	struct prefix p;
	struct access_list *acl = NULL;

	/* We continue hearing vty socket. */
	vty_event_serv(VTY_SERV, vtyserv);

	memset(&su, 0, sizeof(union sockunion));

	/* We can handle IPv4 or IPv6 socket. */
	vty_sock = sockunion_accept(accept_sock, &su);
	if (vty_sock < 0) {
		flog_err(EC_LIB_SOCKET, "can't accept vty socket : %s",
			 safe_strerror(errno));
		return;
	}
	set_nonblocking(vty_sock);
	set_cloexec(vty_sock);

	if (!sockunion2hostprefix(&su, &p)) {
		close(vty_sock);
		zlog_info("Vty unable to convert prefix from sockunion %pSU",
			  &su);
		return;
	}

	/* VTY's accesslist apply. */
	if (p.family == AF_INET && vty_accesslist_name) {
		if ((acl = access_list_lookup(AFI_IP, vty_accesslist_name))
		    && (access_list_apply(acl, &p) == FILTER_DENY)) {
			zlog_info("Vty connection refused from %pSU", &su);
			close(vty_sock);
			return;
		}
	}

	/* VTY's ipv6 accesslist apply. */
	if (p.family == AF_INET6 && vty_ipv6_accesslist_name) {
		if ((acl = access_list_lookup(AFI_IP6,
					      vty_ipv6_accesslist_name))
		    && (access_list_apply(acl, &p) == FILTER_DENY)) {
			zlog_info("Vty connection refused from %pSU", &su);
			close(vty_sock);
			return;
		}
	}

	on = 1;
	ret = setsockopt(vty_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&on,
			 sizeof(on));
	if (ret < 0)
		zlog_info("can't set sockopt to vty_sock : %s",
			  safe_strerror(errno));

	zlog_info("Vty connection from %pSU", &su);

	vty_create(vty_sock, &su);
}

/* 用 getaddrinfo 解析 hostname:port，并为每个可用的 IPv4 / IPv6 地址创建一个监听
 * socket（REUSEADDR / REUSEPORT / CLOEXEC），bind + listen 后登记为 vty_serv 并挂 accept 事件。
 * 参数：hostname 为监听地址（NULL 表示任意）；port 为端口。
 * 说明：getaddrinfo 失败会直接 exit(1)；单个地址 bind / listen 失败则跳过该地址。 */
static void vty_serv_sock_addrinfo(const char *hostname, unsigned short port)
{
	int ret;
	struct addrinfo req;
	struct addrinfo *ainfo;
	struct addrinfo *ainfo_save;
	int sock;
	char port_str[BUFSIZ];

	memset(&req, 0, sizeof(req));
	req.ai_flags = AI_PASSIVE;
	req.ai_family = AF_UNSPEC;
	req.ai_socktype = SOCK_STREAM;
	snprintf(port_str, sizeof(port_str), "%d", port);
	port_str[sizeof(port_str) - 1] = '\0';

	ret = getaddrinfo(hostname, port_str, &req, &ainfo);

	if (ret != 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL, "getaddrinfo failed: %s",
			     gai_strerror(ret));
		exit(1);
	}

	ainfo_save = ainfo;

	do {
		struct vty_serv *vtyserv;

		if (ainfo->ai_family != AF_INET && ainfo->ai_family != AF_INET6)
			continue;

		sock = socket(ainfo->ai_family, ainfo->ai_socktype,
			      ainfo->ai_protocol);
		if (sock < 0)
			continue;

		sockopt_v6only(ainfo->ai_family, sock);
		sockopt_reuseaddr(sock);
		sockopt_reuseport(sock);
		set_cloexec(sock);

		ret = bind(sock, ainfo->ai_addr, ainfo->ai_addrlen);
		if (ret < 0) {
			close(sock); /* Avoid sd leak. */
			continue;
		}

		ret = listen(sock, 3);
		if (ret < 0) {
			close(sock); /* Avoid sd leak. */
			continue;
		}

		vtyserv = XCALLOC(MTYPE_VTY_SERV, sizeof(*vtyserv));
		vtyserv->sock = sock;
		vtyservs_add_tail(vty_servs, vtyserv);

		vty_event_serv(VTY_SERV, vtyserv);
	} while ((ainfo = ainfo->ai_next) != NULL);

	freeaddrinfo(ainfo_save);
}

#ifdef VTYSH
/* For sockaddr_un. */
#include <sys/un.h>

/* 创建 vtysh 用的 UNIX 域套接字并监听（sockaddr_un）。
 * 步骤：先 unlink 旧路径；临时 umask(0007) 建 socket 并 bind；随后恢复 umask；
 * 若配置了 gid_vty 则 chown 该套接字文件；最后登记为 vtysh 服务端并挂 VTYSH_SERV 事件。
 * 参数：path 为套接字路径。失败时记日志并返回（不致命）。 */
/* VTY shell UNIX domain socket. */
static void vty_serv_un(const char *path)
{
	struct vty_serv *vtyserv;
	int ret;
	int sock, len;
	struct sockaddr_un serv;
	mode_t old_mask;
	struct zprivs_ids_t ids;

	/* First of all, unlink existing socket */
	unlink(path);

	/* Set umask */
	old_mask = umask(0007);

	/* Make UNIX domain socket. */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		flog_err_sys(EC_LIB_SOCKET,
			     "Cannot create unix stream socket: %s",
			     safe_strerror(errno));
		return;
	}

	/* Make server socket. */
	memset(&serv, 0, sizeof(serv));
	serv.sun_family = AF_UNIX;
	strlcpy(serv.sun_path, path, sizeof(serv.sun_path));
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	len = serv.sun_len = SUN_LEN(&serv);
#else
	len = sizeof(serv.sun_family) + strlen(serv.sun_path);
#endif /* HAVE_STRUCT_SOCKADDR_UN_SUN_LEN */

	set_cloexec(sock);

	ret = bind(sock, (struct sockaddr *)&serv, len);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "Cannot bind path %s: %s", path,
			     safe_strerror(errno));
		close(sock); /* Avoid sd leak. */
		return;
	}

	ret = listen(sock, 5);
	if (ret < 0) {
		flog_err_sys(EC_LIB_SOCKET, "listen(fd %d) failed: %s", sock,
			     safe_strerror(errno));
		close(sock); /* Avoid sd leak. */
		return;
	}

	umask(old_mask);

	zprivs_get_ids(&ids);

	/* Hack: ids.gid_vty is actually a uint, but we stored -1 in it
	   earlier for the case when we don't need to chown the file
	   type casting it here to make a compare */
	if ((int)ids.gid_vty > 0) {
		/* set group of socket */
		if (chown(path, -1, ids.gid_vty)) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "vty_serv_un: could chown socket, %s",
				     safe_strerror(errno));
		}
	}

	vtyserv = XCALLOC(MTYPE_VTY_SERV, sizeof(*vtyserv));
	vtyserv->sock = sock;
	vtyserv->vtysh = true;
	vtyservs_add_tail(vty_servs, vtyserv);

	vty_event_serv(VTYSH_SERV, vtyserv);
}

/* #define VTYSH_DEBUG 1 */

/* vtysh 客户端的连接到达回调：accept 一个本地连接并创建 VTY_SHELL_SERV 会话。
 * 步骤：非阻塞化、放大发送缓冲（VTY_SEND_BUF_MAX）、按发送缓冲算增量刷新阈值、
 * 设置 fd/wfd/type/node，挂入 vtysh_sessions 并注册 VTYSH_READ 事件。
 * 参数：event 的 arg 为 struct vty_serv。 */
static void vtysh_accept(struct event *event)
{
	struct vty_serv *vtyserv = EVENT_ARG(event);
	int accept_sock = vtyserv->sock;
	int sock;
	int client_len;
	struct sockaddr_un client;
	struct vty *vty;
	int ret = 0;
	uint32_t sndbufsize = VTY_SEND_BUF_MAX;

	vty_event_serv(VTYSH_SERV, vtyserv);

	memset(&client, 0, sizeof(client));
	client_len = sizeof(struct sockaddr_un);

	sock = accept(accept_sock, (struct sockaddr *)&client,
		      (socklen_t *)&client_len);

	if (sock < 0) {
		flog_err(EC_LIB_SOCKET, "can't accept vty socket : %s",
			 safe_strerror(errno));
		return;
	}

	if (set_nonblocking(sock) < 0) {
		flog_err(
			EC_LIB_SOCKET,
			"vtysh_accept: could not set vty socket %d to non-blocking, %s, closing",
			sock, safe_strerror(errno));
		close(sock);
		return;
	}

	/*
	 * Increasing the SEND socket buffer size so that the socket can hold
	 * before sending it to VTY shell.
	 */
	ret = setsockopt_so_sendbuf(sock, sndbufsize);
	if (ret <= 0) {
		flog_err(EC_LIB_SOCKET,
			 "Cannot set socket %d send buffer size, %s", sock,
			 safe_strerror(errno));
		close(sock);
		return;
	}

	set_cloexec(sock);

#ifdef VTYSH_DEBUG
	printf("VTY shell accept\n");
#endif /* VTYSH_DEBUG */

	vty = vty_new();

	/* Capture incremental write threshold */
	if (ret < VTY_MAX_INCREMENTAL_FLUSH)
		vty->vty_buf_threshold = ret / 2;
	else
		vty->vty_buf_threshold = VTY_MAX_INCREMENTAL_FLUSH;

	vty->fd = sock;
	vty->wfd = sock;
	vty->type = VTY_SHELL_SERV;
	vty->node = VIEW_NODE;
	vtys_add_tail(vtysh_sessions, vty);

	vty_event(VTYSH_READ, vty);
}

/* 通过 UNIX 域套接字用 SCM_RIGHTS 把 vty->pass_fd 传递给 vtysh 客户端。
 * 返回：BUFFER_PENDING（需重试）/ BUFFER_ERROR（失败）/ 发送成功的状态。
 * 说明：发送成功后关闭 pass_fd 并复位 status，重新挂上 VTYSH_READ；
 * 若只发出去一部分状态字节，则把剩余部分放入 obuf 并返回 PENDING。 */
static int vtysh_do_pass_fd(struct vty *vty)
{
	struct iovec iov[1] = {
		{
			.iov_base = vty->pass_fd_status,
			.iov_len = sizeof(vty->pass_fd_status),
		},
	};
	union {
		uint8_t buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} u;
	struct msghdr mh = {
		.msg_iov = iov,
		.msg_iovlen = array_size(iov),
		.msg_control = u.buf,
		.msg_controllen = sizeof(u.buf),
	};
	struct cmsghdr *cmh = CMSG_FIRSTHDR(&mh);
	ssize_t ret;

	memset(&u.buf, 0, sizeof(u.buf));
	cmh->cmsg_level = SOL_SOCKET;
	cmh->cmsg_type = SCM_RIGHTS;
	cmh->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cmh), &vty->pass_fd, sizeof(int));

	ret = sendmsg(vty->wfd, &mh, 0);
	if (ret < 0 && ERRNO_IO_RETRY(errno))
		return BUFFER_PENDING;

	close(vty->pass_fd);
	vty->pass_fd = -1;
	vty->status = VTY_NORMAL;

	if (ret <= 0)
		return BUFFER_ERROR;

	/* resume accepting commands (suspended in vtysh_read) */
	vty_event(VTYSH_READ, vty);

	if ((size_t)ret < sizeof(vty->pass_fd_status)) {
		size_t remains = sizeof(vty->pass_fd_status) - ret;

		buffer_put(vty->obuf, vty->pass_fd_status + ret, remains);
		return BUFFER_PENDING;
	}
	return BUFFER_EMPTY;
}

/* 把输出缓冲刷给 vtysh 客户端，并在需要时补发 fd。
 * 返回：0 成功；-1 出错（写失败，已置 VTY_CLOSE、清缓冲）。
 * 说明：当缓冲已空且状态为 VTY_PASSFD 时，调用 vtysh_do_pass_fd 把 fd 发出去；
 * PENDING 时挂写事件等待，EMPTY 时复位增量计数。 */
static int vtysh_flush(struct vty *vty)
{
	int ret;

	ret = buffer_flush_available(vty->obuf, vty->wfd);
	if (ret == BUFFER_EMPTY && vty->status == VTY_PASSFD)
		ret = vtysh_do_pass_fd(vty);

	switch (ret) {
	case BUFFER_PENDING:
		vty_event(VTYSH_WRITE, vty);
		break;
	case BUFFER_ERROR:
		flog_err(EC_LIB_SOCKET, "%s: write error to fd %d, closing",
			 __func__, vty->fd);
		buffer_reset(vty->lbuf);
		buffer_reset(vty->obuf);

		/* Set error status. The vty may be in the middle of
		 * application code that may not be prepared to handle
		 * an error, so we'll defer cleanup until the app handler
		 * returns.
		 */
		vty->status = VTY_CLOSE;
		return -1;
	case BUFFER_EMPTY:
		vty->vty_buf_size_accum = 0;
		break;
	}
	return 0;
}

/* 记录一个要传递给 vtysh 客户端的 fd（如 terminal monitor 的日志 fd）。
 * 若已有待传 fd 则先关闭旧的；随后保存新 fd。 */
void vty_pass_fd(struct vty *vty, int fd)
{
	if (vty->pass_fd != -1)
		close(vty->pass_fd);

	vty->pass_fd = fd;
}

/* vtysh 服务端读回调：从 vtysh 客户端 socket 读命令字节流并执行。
 * 逐字节累积到 vty->buf，遇到 '\0' 即认为一条命令结束，调用 vty_execute 执行，
 * 再把 4 字节返回码写回客户端（若本命令要传递 fd，则先发 fd 并暂停读取）。
 * 参数：event 的 arg 为 struct vty。返回：void。
 * 坑：一次 read 里通常只承载一条命令——为传 fd / CMD_SUSPEND / 等待 mgmtd 响应，
 *   本函数会在中途 return，尚未处理的字节会被丢弃（见上方英文长注释）。 */
static void vtysh_read(struct event *event)
{
	int ret;
	int sock;
	int nbytes;
	struct vty *vty;
	unsigned char buf[VTY_READ_BUFSIZ];
	unsigned char *p;
	uint8_t header[4] = {0, 0, 0, 0};

	sock = EVENT_FD(event);
	vty = EVENT_ARG(event);

	/*
	 * This code looks like it can read multiple commands from the `buf`
	 * value returned by read(); however, it cannot in some cases.
	 *
	 * There are multiple paths out of the "copying to vty->buf" loop, which
	 * lose any content not yet copied from the stack `buf`, `passfd`,
	 * `CMD_SUSPEND` and finally if a front-end for mgmtd (generally this
	 * would be mgmtd itself). So these code paths are counting on vtysh not
	 * sending us more than 1 command line before waiting on the reply to
	 * that command.
	 */
	assert(vty->type == VTY_SHELL_SERV);

	if ((nbytes = read(sock, buf, VTY_READ_BUFSIZ)) <= 0) {
		if (nbytes < 0) {
			if (ERRNO_IO_RETRY(errno)) {
				vty_event(VTYSH_READ, vty);
				return;
			}
			flog_err(
				EC_LIB_SOCKET,
				"%s: read failed on vtysh client fd %d, closing: %s",
				__func__, sock, safe_strerror(errno));
		}
		buffer_reset(vty->lbuf);
		buffer_reset(vty->obuf);
		vty_close(vty);
#ifdef VTYSH_DEBUG
		printf("close vtysh\n");
#endif /* VTYSH_DEBUG */
		return;
	}

#ifdef VTYSH_DEBUG
	printf("line: %.*s\n", nbytes, buf);
#endif /* VTYSH_DEBUG */

	if (vty->length + nbytes >= VTY_BUFSIZ) {
		/* Clear command line buffer. */
		vty->cp = vty->length = 0;
		vty_clear_buf(vty);
		flog_err(EC_LIB_VTY, "Command too long (%d bytes): %.80s...", nbytes, buf);
		vty_out(vty, "%% Command is too long.\n");
	} else {
		for (p = buf; p < buf + nbytes; p++) {
			vty->buf[vty->length++] = *p;
			if (*p == '\0') {
				/* Pass this line to parser. */
				ret = vty_execute(vty);
/* Note that vty_execute clears the command buffer and resets
   vty->length to 0. */

/* Return result. */
#ifdef VTYSH_DEBUG
				printf("result: %d\n", ret);
				printf("vtysh node: %d\n", vty->node);
#endif /* VTYSH_DEBUG */
				if (vty->pass_fd >= 0) {
					memset(vty->pass_fd_status, 0, 4);
					vty->pass_fd_status[3] = ret;
					vty->status = VTY_PASSFD;

					if (!vty->t_write)
						vty_event(VTYSH_WRITE, vty);

					/* this introduces a "sequence point"
					 * command output is written normally,
					 * read processing is suspended until
					 * buffer is empty
					 * then retcode + FD is written
					 * then normal processing resumes
					 *
					 * => skip vty_event(VTYSH_READ, vty)!
					 */
					return;
				} else {
					assertf(vty->status != VTY_PASSFD,
						"%p address=%s passfd=%d", vty,
						vty->address, vty->pass_fd);

					/* normalize other invalid values */
					vty->pass_fd = -1;
				}

				/* hack for asynchronous "write integrated"
				 * - other commands in "buf" will be ditched
				 * - input during pending config-write is
				 * "unsupported" */
				if (ret == CMD_SUSPEND)
					break;

				/* with new infra we need to stop response till
				 * we get response through callback.
				 */
				if (vty->mgmt_req_pending_cmd) {
					debug_fe_client("postpone CLI response pending mgmtd %s on vty session-id %" PRIu64,
							vty->mgmt_req_pending_cmd,
							vty->mgmt_session_id);
					return;
				}

				/* warning: watchfrr hardcodes this result write
				 */
				header[3] = ret;
				buffer_put(vty->obuf, header, 4);

				if (!vty->t_write && (vtysh_flush(vty) < 0))
					/* Try to flush results; exit if a write
					 * error occurs. */
					break;

				/* If we encountered an error, break now */
				if (vty->status == VTY_CLOSE)
					break;
			}
		}
	}

	if (vty->status == VTY_CLOSE)
		vty_close(vty);
	else
		vty_event(VTYSH_READ, vty);
}

/* vtysh 服务端写回调：把输出缓冲刷给客户端（调用 vtysh_flush）。 */
static void vtysh_write(struct event *event)
{
	struct vty *vty = EVENT_ARG(event);

	vtysh_flush(vty);
}

#endif /* VTYSH */

/* 启动 VTY 服务：port 非 0 则监听 TCP（vty_serv_sock_addrinfo）；
 * 若编译了 VTYSH 则再创建 UNIX 域套接字（vty_serv_un）。
 * 参数：addr 监听地址；port 端口（0 表示不监听 TCP）；path 为 vtysh 套接字路径。 */
/* Determine address family to bind. */
void vty_serv_start(const char *addr, unsigned short port, const char *path)
{
	/* If port is set to 0, do not listen on TCP/IP at all! */
	if (port)
		vty_serv_sock_addrinfo(addr, port);

#ifdef VTYSH
	vty_serv_un(path);
#endif /* VTYSH */
}

/* 停止所有 VTY 服务：遍历并弹出全部监听服务端，取消 accept 事件、关闭 socket、
 * 释放结构，最后重置链表。 */
void vty_serv_stop(void)
{
	struct vty_serv *vtyserv;

	while ((vtyserv = vtyservs_pop(vty_servs))) {
		event_cancel(&vtyserv->t_accept);
		close(vtyserv->sock);
		XFREE(MTYPE_VTY_SERV, vtyserv);
	}

	vtyservs_fini(vty_servs);
	vtyservs_init(vty_servs);
}

/* vty->error 列表元素的删除回调：释放一个 struct vty_error（MTYPE_TMP）。 */
static void vty_error_delete(void *arg)
{
	struct vty_error *ve = arg;

	XFREE(MTYPE_TMP, ve);
}

/* 关闭并释放一个 vty。警告：调用后 vty 已被释放，切勿再访问；最好只在
 * 事件派发器直接调用的顶层函数里调用。
 * 关键步骤：标记 VTY_CLOSE、有未提交配置则告警、退出配置模式、触发 close 回调、
 * 取消读写/超时事件、关闭 pass_fd、刷新并释放输入输出缓冲、清历史、
 * 从会话链表摘除、必要时 fsync 配置文件、关闭 socket（但不关 stdin/stdout/stderr）、
 * 释放命令缓冲与 error 列表，最后释放 vty 本体；若是 stdio vty 则复位终端。 */
/* Close vty interface.  Warning: call this only from functions that
   will be careful not to access the vty afterwards (since it has
   now been freed).  This is safest from top-level functions (called
   directly by the thread dispatcher). */
void vty_close(struct vty *vty)
{
	int i;
	bool was_stdio = false;

	vty->status = VTY_CLOSE;

	/*
	 * If we reach here with pending config to commit we will be losing it
	 * so warn the user.
	 */
	if (vty->mgmt_num_pending_setcfg)
		log_err_fe_client(
			"vty closed, uncommitted config will be lost.");

	/* Drop out of configure / transaction if needed. */
	vty_config_exit(vty);

	if (vty_close_mgmt_cb)
		vty_close_mgmt_cb(vty);

	/* Cancel threads.*/
	event_cancel(&vty->t_read);
	event_cancel(&vty->t_write);
	event_cancel(&vty->t_timeout);

	if (vty->pass_fd != -1) {
		close(vty->pass_fd);
		vty->pass_fd = -1;
	}
	zlog_live_close(&vty->live_log);

	/* Flush buffer. */
	buffer_flush_all(vty->obuf, vty->wfd);

	/* Free input buffer. */
	buffer_free(vty->obuf);
	buffer_free(vty->lbuf);

	/* Free command history. */
	for (i = 0; i < VTY_MAXHIST; i++) {
		XFREE(MTYPE_VTY_HIST, vty->hist[i]);
	}

	/* Unset vector. */
	if (vty->fd != -1) {
		if (vty->type == VTY_SHELL_SERV)
			vtys_del(vtysh_sessions, vty);
		else if (vty->type == VTY_TERM)
			vtys_del(vty_sessions, vty);
	}

	if (vty->wfd > 0 && vty->type == VTY_FILE)
		fsync(vty->wfd);

	/* Close socket.
	 * note check is for fd > STDERR_FILENO, not fd != -1.
	 * We never close stdin/stdout/stderr here, because we may be
	 * running in foreground mode with logging to stdout.  Also,
	 * additionally, we'd need to replace these fds with /dev/null. */
	if (vty->wfd > STDERR_FILENO && vty->wfd != vty->fd)
		close(vty->wfd);
	if (vty->fd > STDERR_FILENO)
		close(vty->fd);
	if (vty->fd == STDIN_FILENO)
		was_stdio = true;

	XFREE(MTYPE_TMP, vty->pending_cmds_buf);
	XFREE(MTYPE_VTY, vty->buf);

	if (vty->error) {
		vty->error->del = vty_error_delete;
		list_delete(&vty->error);
	}

	/* OK free vty. */
	XFREE(MTYPE_VTY, vty);

	if (was_stdio)
		vty_stdio_reset(0);
}

/* vty 空闲超时回调：清空缓冲、提示“连接超时”，随后关闭该 vty。 */
/* When time out occur output message then close connection. */
static void vty_timeout(struct event *event)
{
	struct vty *vty;

	vty = EVENT_ARG(event);
	vty->v_timeout = 0;

	/* Clear buffer*/
	buffer_reset(vty->lbuf);
	buffer_reset(vty->obuf);
	vty_out(vty, "\nVty connection is timed out.\n");

	/* Close connection. */
	vty->status = VTY_CLOSE;
	vty_close(vty);
}

/* 从一个已打开的配置文件把整份配置读入。
 * 参数：config 为目标候选配置（NULL 表示先建私有候选配置）；confp 为已打开的 FILE*。
 * 说明：新建一个 VTY_FILE 型 vty，wfd 指向 STDERR（配置文件里的报错输出到 stderr），
 * 节点置 CONFIG_NODE，由 config_from_file 逐行解析，最后调用 vty_read_file_finish 收尾。 */
/* Read up configuration file from file_name. */
void vty_read_file(struct nb_config *config, FILE *confp)
{
	struct vty *vty;
	unsigned int line_num = 0;

	vty = vty_new();
	/* vty_close won't close stderr;  if some config command prints
	 * something it'll end up there.  (not ideal; it'd be better if output
	 * from a file-load went to logging instead.  Also note that if this
	 * function is called after daemonizing, stderr will be /dev/null.)
	 *
	 * vty->fd will be -1 from vty_new()
	 */
	vty->wfd = STDERR_FILENO;
	vty->type = VTY_FILE;
	vty->node = CONFIG_NODE;
	vty->config = true;
	if (config)
		vty->candidate_config = config;
	else {
		vty->private_config = true;
		vty->candidate_config = nb_config_new(NULL);
	}

	/* Execute configuration file */
	(void)config_from_file(vty, confp, &line_num);

	vty_read_file_finish(vty, config);
}

/* 配置文件读取收尾：先冲刷缓冲，再把读取期间记录的错误逐条打日志，
 * 最后（当 config 为 NULL，即非外部候选配置时）自动提交候选配置并关闭 vty。
 * 参数：vty 为读取用的 vty；config 为外部候选配置（可为 NULL）。
 * 坑：各 cmd_ret 都会映射成一句人类可读的 message；error_buf 只取第一行。 */
void vty_read_file_finish(struct vty *vty, struct nb_config *config)
{
	struct vty_error *ve;
	struct listnode *node;

	/* Flush any previous errors before printing messages below */
	buffer_flush_all(vty->obuf, vty->wfd);

	for (ALL_LIST_ELEMENTS_RO(vty->error, node, ve)) {
		const char *message = NULL;
		char *nl;

		switch (ve->cmd_ret) {
		case CMD_SUCCESS:
			message = "Command succeeded";
			break;
		case CMD_ERR_NOTHING_TODO:
			message = "Nothing to do";
			break;
		case CMD_ERR_AMBIGUOUS:
			message = "Ambiguous command";
			break;
		case CMD_ERR_NO_MATCH:
			message = "No such command";
			break;
		case CMD_WARNING:
			message = "Command returned Warning";
			break;
		case CMD_WARNING_CONFIG_FAILED:
			message = "Command returned Warning Config Failed";
			break;
		case CMD_ERR_INCOMPLETE:
			message = "Command returned Incomplete";
			break;
		case CMD_ERR_EXEED_ARGC_MAX:
			message =
				"Command exceeded maximum number of Arguments";
			break;
		default:
			message = "Command returned unhandled error message";
			break;
		}

		nl = strchr(ve->error_buf, '\n');
		if (nl)
			*nl = '\0';
		flog_err(EC_LIB_VTY, "%s on config line %u: %s", message,
			 ve->line_num, ve->error_buf);
	}

	/*
	 * Automatically commit the candidate configuration after
	 * reading the configuration file.
	 */
	if (config == NULL) {
		struct nb_context context = {};
		char errmsg[BUFSIZ] = {0};
		int ret;

		context.client = NB_CLIENT_CLI;
		context.user = vty;
		ret = nb_candidate_commit(context, vty->candidate_config, true,
					  "Read configuration file", NULL,
					  errmsg, sizeof(errmsg));
		if (ret != NB_OK && ret != NB_ERR_NO_CHANGES)
			zlog_err(
				"%s: failed to read configuration file: %s (%s)",
				__func__, nb_err_name(ret), errmsg);
	}

	vty_close(vty);
}

/* 当主配置文件打不开时，尝试使用备份文件（路径追加 CONF_BACKUP_EXT）。
 * 做法：把备份内容复制到一个临时文件（mkstemp、权限 CONFIGFILE_MASK），
 * 再 rename 覆盖回原路径并打开返回。
 * 参数：fullpath 为原配置文件全路径。返回：打开的 FILE*，失败返回 NULL。
 * 注意：内部用 malloc/free（而非 MTYPE）管理路径字符串。 */
static FILE *vty_use_backup_config(const char *fullpath)
{
	char *fullpath_sav, *fullpath_tmp;
	FILE *ret = NULL;
	int tmp, sav;
	int c;
	char buffer[512];

	size_t fullpath_sav_sz = strlen(fullpath) + strlen(CONF_BACKUP_EXT) + 1;
	fullpath_sav = malloc(fullpath_sav_sz);
	strlcpy(fullpath_sav, fullpath, fullpath_sav_sz);
	strlcat(fullpath_sav, CONF_BACKUP_EXT, fullpath_sav_sz);

	sav = open(fullpath_sav, O_RDONLY);
	if (sav < 0) {
		free(fullpath_sav);
		return NULL;
	}

	fullpath_tmp = malloc(strlen(fullpath) + 8);
	snprintf(fullpath_tmp, strlen(fullpath) + 8, "%s.XXXXXX", fullpath);

	/* Open file to configuration write. */
	tmp = mkstemp(fullpath_tmp);
	if (tmp < 0)
		goto out_close_sav;

	if (fchmod(tmp, CONFIGFILE_MASK) != 0)
		goto out_close;

	while ((c = read(sav, buffer, 512)) > 0) {
		if (write(tmp, buffer, c) <= 0)
			goto out_close;
	}
	close(sav);
	close(tmp);

	if (rename(fullpath_tmp, fullpath) == 0)
		ret = fopen(fullpath, "r");
	else
		unlink(fullpath_tmp);

	if (0) {
	out_close:
		close(tmp);
		unlink(fullpath_tmp);
	out_close_sav:
		close(sav);
	}

	free(fullpath_sav);
	free(fullpath_tmp);
	return ret;
}

/* 打开配置文件：优先用显式指定的 config_file，否则用 config_default_dir。
 * 相对路径会拼上当前工作目录；打开失败时退回到备份文件。
 * 参数：config_file 用户指定的文件（可为 NULL）；config_default_dir 默认目录配置。
 * 返回：打开的 FILE*（失败返回 NULL）。
 * 说明：VTYSH 场景下，若存在整合配置 frr.conf 则不读本 daemon 配置，
 *   等待 vtysh -b 的启动配置（见函数内的英文说明）。 */
FILE *vty_open_config(const char *config_file, char *config_default_dir)
{
	char cwd[MAXPATHLEN];
	FILE *confp = NULL;
	const char *fullpath;
	char *tmp = NULL;

	/* If -f flag specified. */
	if (config_file != NULL) {
		if (!IS_DIRECTORY_SEP(config_file[0])) {
			if (getcwd(cwd, MAXPATHLEN) == NULL) {
				flog_err_sys(
					EC_LIB_SYSTEM_CALL,
					"%s: failure to determine Current Working Directory %d!",
					__func__, errno);
				goto tmp_free_and_out;
			}
			size_t tmp_len = strlen(cwd) + strlen(config_file) + 2;
			tmp = XMALLOC(MTYPE_TMP, tmp_len);
			snprintf(tmp, tmp_len, "%s/%s", cwd, config_file);
			fullpath = tmp;
		} else
			fullpath = config_file;

		confp = fopen(fullpath, "r");

		if (confp == NULL) {
			flog_warn(
				EC_LIB_BACKUP_CONFIG,
				"%s: failed to open configuration file %s: %s, checking backup",
				__func__, fullpath, safe_strerror(errno));

			confp = vty_use_backup_config(fullpath);
			if (confp)
				flog_warn(EC_LIB_BACKUP_CONFIG,
					  "using backup configuration file!");
			else {
				flog_err(
					EC_LIB_VTY,
					"%s: can't open configuration file [%s]",
					__func__, config_file);
				goto tmp_free_and_out;
			}
		}
	} else {

		host_config_set(config_default_dir);

#ifdef VTYSH
		int ret;
		struct stat conf_stat;

		/* !!!!PLEASE LEAVE!!!!
		 * This is NEEDED for use with vtysh -b, or else you can get
		 * a real configuration food fight with a lot garbage in the
		 * merged configuration file it creates coming from the per
		 * daemon configuration files.  This also allows the daemons
		 * to start if there default configuration file is not
		 * present or ignore them, as needed when using vtysh -b to
		 * configure the daemons at boot - MAG
		 */

		/* Stat for vtysh Zebra.conf, if found startup and wait for
		 * boot configuration
		 */

		if (strstr(config_default_dir, "vtysh") == NULL) {
			ret = stat(integrate_default, &conf_stat);
			if (ret >= 0)
				goto tmp_free_and_out;
		}
#endif /* VTYSH */
		confp = fopen(config_default_dir, "r");
		if (confp == NULL) {
			flog_err(
				EC_LIB_SYSTEM_CALL,
				"%s: failed to open configuration file %s: %s, checking backup",
				__func__, config_default_dir,
				safe_strerror(errno));

			confp = vty_use_backup_config(config_default_dir);
			if (confp) {
				flog_warn(EC_LIB_BACKUP_CONFIG,
					  "using backup configuration file!");
				fullpath = config_default_dir;
			} else {
				flog_err(EC_LIB_VTY,
					 "can't open configuration file [%s]",
					 config_default_dir);
				goto tmp_free_and_out;
			}
		} else
			fullpath = config_default_dir;
	}

	host_config_set(fullpath);

tmp_free_and_out:
	XFREE(MTYPE_TMP, tmp);

	return confp;
}


/* 打开并读取一份配置文件（= vty_open_config + vty_read_file + 关闭）。
 * 参数：config 目标候选配置；config_file 指定文件；config_default_dir 默认目录。
 * 返回：成功读取 true，打不开文件 false。 */
bool vty_read_config(struct nb_config *config, const char *config_file,
		     char *config_default_dir)
{
	FILE *confp;

	confp = vty_open_config(config_file, config_default_dir);
	if (!confp)
		return false;

	vty_read_file(config, confp);

	fclose(confp);

	return true;
}

/* 进入配置模式。参数：private_config 私有配置；exclusive 是否独占（加运行锁）；
 * file_lock 是否加文件锁。
 * 返回：CMD_SUCCESS 或 CMD_WARNING（被其它客户端锁定时）。
 * 说明：有 mgmtd 回调时交由它处理；私有配置会拷贝 running 配置；非私有则指向
 * mgmtd / 共享候选配置，事务模式下再拷贝一份基线配置。 */
int vty_config_enter(struct vty *vty, bool private_config, bool exclusive,
		     bool file_lock)
{
	int ret = CMD_SUCCESS;

	if (vty_config_enter_mgmt_cb)
		ret = vty_config_enter_mgmt_cb(vty, private_config, exclusive, file_lock);
	else if (exclusive && nb_running_lock(NB_CLIENT_CLI, vty)) {
		vty_out(vty, "%% Configuration is locked by other client\n");
		ret = CMD_WARNING;
	}

	if (ret != CMD_SUCCESS)
		return ret;

	vty->node = CONFIG_NODE;
	vty->config = true;
	vty->private_config = private_config;
	vty->xpath_index = 0;

	if (private_config) {
		vty->candidate_config = nb_config_dup(running_config);
		vty->candidate_config_base = nb_config_dup(running_config);
		vty_out(vty,
			"Warning: uncommitted changes will be discarded on exit.\n\n");
		return CMD_SUCCESS;
	}

	/*
	 * NOTE: On the MGMTD daemon we point the VTY candidate DS to
	 * the global MGMTD candidate DS. Else we point to the VTY
	 * Shared Candidate Config.
	 */
	vty->candidate_config = vty_mgmt_candidate_config
					? vty_mgmt_candidate_config
					: vty_shared_candidate_config;
	if (frr_get_cli_mode() == FRR_CLI_TRANSACTIONAL)
		vty->candidate_config_base = nb_config_dup(running_config);

	return CMD_SUCCESS;
}


/* 退出配置模式：若当前节点在 CONFIG_NODE 之下，则逐级调用 cmd_exit 一直退到
 * ENABLE_NODE（其间会触发 vty_config_node_exit 做清理与提交检查）。
 * 若本就不在配置子树内则直接返回（例如在 ENABLE_NODE 调 vty_close 的情形）。 */
void vty_config_exit(struct vty *vty)
{
	enum node_type node = vty->node;
	struct cmd_node *cnode;

	/* unlock and jump up to ENABLE_NODE if -and only if- we're
	 * somewhere below CONFIG_NODE */
	while (node && node != CONFIG_NODE) {
		cnode = vector_lookup(cmdvec, node);
		node = cnode->parent_node;
	}
	if (node != CONFIG_NODE)
		/* called outside config, e.g. vty_close() in ENABLE_NODE */
		return;

	while (vty->node != ENABLE_NODE)
		/* will call vty_config_node_exit() below */
		cmd_exit(vty);
}

/* 退出当前配置节点的清理钩子（由 cmd_exit 调用）。
 * 依次：清 xpath 索引、关闭 pending_allowed、触发 mgmtd 回调、做待提交检查、
 * 若有“确认提交（confirmed-commit）”超时在挂则回滚并清理、释放运行锁、
 * 释放候选配置（私有配置）与基线配置、复位 config 标志。
 * 返回：1。特殊：读配置文件过程中退到配置节点会提示并置 VTY_CLOSE。 */
int vty_config_node_exit(struct vty *vty)
{
	vty->xpath_index = 0;

	/* TODO: could we check for un-commited changes here? */

	vty->pending_allowed = false;

	if (vty_config_node_exit_mgmt_cb)
		vty_config_node_exit_mgmt_cb(vty);

	/* Perform any pending commits. */
	(void)nb_cli_pending_commit_check(vty);

	/* Check if there's a pending confirmed commit. */
	if (vty->t_confirmed_commit_timeout) {
		vty_out(vty,
			"exiting with a pending confirmed commit. Rolling back to previous configuration.\n\n");
		nb_cli_confirmed_commit_rollback(vty);
		nb_cli_confirmed_commit_clean(vty);
	}

	(void)nb_running_unlock(NB_CLIENT_CLI, vty);

	if (vty->candidate_config) {
		if (vty->private_config)
			nb_config_free(vty->candidate_config);
		vty->candidate_config = NULL;
	}
	if (vty->candidate_config_base) {
		nb_config_free(vty->candidate_config_base);
		vty->candidate_config_base = NULL;
	}

	vty->config = false;

	/*
	 * If this is a config file and we are dropping out of config end
	 * parsing.
	 */
	if (vty->type == VTY_FILE && vty->status != VTY_CLOSE) {
		vty_out(vty, "exit from config node while reading config file");
		vty->status = VTY_CLOSE;
	}

	return 1;
}

/* 所有 vty 事件（服务器与各会话）统一注册到的事件循环，由 vty_init 设置。 */
/* Master of the threads. */
static struct event_loop *vty_master;

/* 为监听服务端注册事件。参数：event 仅支持 VTY_SERV（或 VTYSH_SERV）；
 * vty_serv 为对应服务端。传入其它事件类型属于误用，会触发断言。 */
static void vty_event_serv(enum vty_event event, struct vty_serv *vty_serv)
{
	switch (event) {
	case VTY_SERV:
		event_add_read(vty_master, vty_accept, vty_serv, vty_serv->sock,
			       &vty_serv->t_accept);
		break;
#ifdef VTYSH
	case VTYSH_SERV:
		event_add_read(vty_master, vtysh_accept, vty_serv,
			       vty_serv->sock, &vty_serv->t_accept);
		break;
#endif /* VTYSH */
	case VTY_READ:
	case VTY_WRITE:
	case VTY_TIMEOUT_RESET:
	case VTYSH_READ:
	case VTYSH_WRITE:
		assert(!"vty_event_serv() called incorrectly");
	}
}

/* 为某个 vty 会话注册 / 重置事件：
 *   VTY_READ / VTYSH_READ   挂读事件（VTY_READ 还会重置空闲超时定时器）。
 *   VTY_WRITE / VTYSH_WRITE 挂写事件。
 *   VTY_TIMEOUT_RESET       取消并重设超时定时器。
 * 参数：event；vty。传入服务端类事件会触发断言。 */
static void vty_event(enum vty_event event, struct vty *vty)
{
	switch (event) {
#ifdef VTYSH
	case VTYSH_READ:
		event_add_read(vty_master, vtysh_read, vty, vty->fd,
			       &vty->t_read);
		break;
	case VTYSH_WRITE:
		event_add_write(vty_master, vtysh_write, vty, vty->wfd,
				&vty->t_write);
		break;
#endif /* VTYSH */
	case VTY_READ:
		event_add_read(vty_master, vty_read, vty, vty->fd,
			       &vty->t_read);

		/* Time out treatment. */
		if (vty->v_timeout) {
			event_cancel(&vty->t_timeout);
			event_add_timer(vty_master, vty_timeout, vty,
					vty->v_timeout, &vty->t_timeout);
		}
		break;
	case VTY_WRITE:
		event_add_write(vty_master, vty_flush, vty, vty->wfd,
				&vty->t_write);
		break;
	case VTY_TIMEOUT_RESET:
		event_cancel(&vty->t_timeout);
		if (vty->v_timeout)
			event_add_timer(vty_master, vty_timeout, vty,
					vty->v_timeout, &vty->t_timeout);
		break;
	case VTY_SERV:
	case VTYSH_SERV:
		assert(!"vty_event() called incorrectly");
	}
}

/* VTY 命令 "who"：列出当前所有 vty 会话及其来源地址；
 * 配置模式中的会话用 * 标记，启用了 live log 的追加提示。返回 CMD_SUCCESS。 */
DEFUN_NOSH (config_who,
       config_who_cmd,
       "who",
       "Display who is on vty\n")
{
	struct vty *v;

	frr_each (vtys, vty_sessions, v)
		vty_out(vty, "%svty[%d] connected from %s%s.\n",
			v->config ? "*" : " ", v->fd, v->address,
			zlog_live_is_null(&v->live_log) ? "" : ", live log");
	return CMD_SUCCESS;
}

/* VTY 命令 "line vty"：进入 VTY_NODE 配置节点（配置终端行参数）。
 * 仅切换节点并返回 CMD_SUCCESS。 */
/* Move to vty configuration mode. */
DEFUN_NOSH (line_vty,
       line_vty_cmd,
       "line vty",
       "Configure a terminal line\n"
       "Virtual terminal\n")
{
	vty->node = VTY_NODE;
	return CMD_SUCCESS;
}

/* 设置 exec 超时（内部实现）。参数：min_str 分钟、sec_str 秒（均可能为 NULL）。
 * 换算成秒后存入全局 vty_timeout_val 与当前 vty->v_timeout，并重置超时事件。
 * 返回 CMD_SUCCESS。 */
/* Set time out value. */
static int exec_timeout(struct vty *vty, const char *min_str,
			const char *sec_str)
{
	unsigned long timeout = 0;

	/* min_str and sec_str are already checked by parser.  So it must be
	   all digit string. */
	if (min_str) {
		timeout = strtol(min_str, NULL, 10);
		timeout *= 60;
	}
	if (sec_str)
		timeout += strtol(sec_str, NULL, 10);

	vty_timeout_val = timeout;
	vty->v_timeout = timeout;
	vty_event(VTY_TIMEOUT_RESET, vty);


	return CMD_SUCCESS;
}

/* VTY 命令 "exec-timeout (0-35791)"：只给分钟，转调 exec_timeout。 */
DEFUN (exec_timeout_min,
       exec_timeout_min_cmd,
       "exec-timeout (0-35791)",
       "Set timeout value\n"
       "Timeout value in minutes\n")
{
	int idx_number = 1;
	return exec_timeout(vty, argv[idx_number]->arg, NULL);
}

/* VTY 命令 "exec-timeout (0-35791) (0-2147483)"：分 + 秒，转调 exec_timeout。 */
DEFUN (exec_timeout_sec,
       exec_timeout_sec_cmd,
       "exec-timeout (0-35791) (0-2147483)",
       "Set the EXEC timeout\n"
       "Timeout in minutes\n"
       "Timeout in seconds\n")
{
	int idx_number = 1;
	int idx_number_2 = 2;
	return exec_timeout(vty, argv[idx_number]->arg,
			    argv[idx_number_2]->arg);
}

/* VTY 命令 "no exec-timeout"：把超时清零（即不超时），转调 exec_timeout(NULL, NULL)。 */
DEFUN (no_exec_timeout,
       no_exec_timeout_cmd,
       "no exec-timeout",
       NO_STR
       "Set the EXEC timeout\n")
{
	return exec_timeout(vty, NULL, NULL);
}

/* VTY 命令 "access-class WORD"：设置 IPv4 访问控制列表名。
 * 释放旧名并把新名复制进 vty_accesslist_name。返回 CMD_SUCCESS。 */
/* Set vty access class. */
DEFUN (vty_access_class,
       vty_access_class_cmd,
       "access-class WORD",
       "Filter connections based on an IP access list\n"
       "IP access list\n")
{
	int idx_word = 1;
	if (vty_accesslist_name)
		XFREE(MTYPE_VTY, vty_accesslist_name);

	vty_accesslist_name = XSTRDUP(MTYPE_VTY, argv[idx_word]->arg);

	return CMD_SUCCESS;
}

/* VTY 命令 "no access-class [WORD]"：清除（或按名清除）IPv4 访问控制列表。
 * 若当前未设置、或指定名与当前不符，则报错返回 CMD_WARNING_CONFIG_FAILED；
 * 命中则释放并置空。 */
/* Clear vty access class. */
DEFUN (no_vty_access_class,
       no_vty_access_class_cmd,
       "no access-class [WORD]",
       NO_STR
       "Filter connections based on an IP access list\n"
       "IP access list\n")
{
	int idx_word = 2;
	const char *accesslist = (argc == 3) ? argv[idx_word]->arg : NULL;
	if (!vty_accesslist_name
	    || (argc == 3 && strcmp(vty_accesslist_name, accesslist))) {
		vty_out(vty, "Access-class is not currently applied to vty\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	XFREE(MTYPE_VTY, vty_accesslist_name);

	vty_accesslist_name = NULL;

	return CMD_SUCCESS;
}

/* VTY 命令 "ipv6 access-class WORD"：设置 IPv6 访问控制列表名（释放旧值后复制新值）。 */
/* Set vty access class. */
DEFUN (vty_ipv6_access_class,
       vty_ipv6_access_class_cmd,
       "ipv6 access-class WORD",
       IPV6_STR
       "Filter connections based on an IP access list\n"
       "IPv6 access list\n")
{
	int idx_word = 2;
	if (vty_ipv6_accesslist_name)
		XFREE(MTYPE_VTY, vty_ipv6_accesslist_name);

	vty_ipv6_accesslist_name = XSTRDUP(MTYPE_VTY, argv[idx_word]->arg);

	return CMD_SUCCESS;
}

/* VTY 命令 "no ipv6 access-class [WORD]"：清除（或按名清除）IPv6 访问控制列表；
 * 未设置或名不符时报错返回 CMD_WARNING_CONFIG_FAILED。 */
/* Clear vty access class. */
DEFUN (no_vty_ipv6_access_class,
       no_vty_ipv6_access_class_cmd,
       "no ipv6 access-class [WORD]",
       NO_STR
       IPV6_STR
       "Filter connections based on an IP access list\n"
       "IPv6 access list\n")
{
	int idx_word = 3;
	const char *accesslist = (argc == 4) ? argv[idx_word]->arg : NULL;

	if (!vty_ipv6_accesslist_name
	    || (argc == 4 && strcmp(vty_ipv6_accesslist_name, accesslist))) {
		vty_out(vty,
			"IPv6 access-class is not currently applied to vty\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	XFREE(MTYPE_VTY, vty_ipv6_accesslist_name);

	vty_ipv6_accesslist_name = NULL;

	return CMD_SUCCESS;
}

/* VTY 命令 "login"：开启登录密码校验（no_password_check = 0）。返回 CMD_SUCCESS。 */
/* vty login. */
DEFUN (vty_login,
       vty_login_cmd,
       "login",
       "Enable password checking\n")
{
	no_password_check = 0;
	return CMD_SUCCESS;
}

/* VTY 命令 "no login"：跳过登录密码校验（no_password_check = 1）。返回 CMD_SUCCESS。 */
DEFUN (no_vty_login,
       no_vty_login_cmd,
       "no login",
       NO_STR
       "Enable password checking\n")
{
	no_password_check = 1;
	return CMD_SUCCESS;
}

/* VTY 命令 "service advanced-vty"：开启高级模式 vty（host.advanced = 1）。 */
DEFUN (service_advanced_vty,
       service_advanced_vty_cmd,
       "service advanced-vty",
       "Set up miscellaneous service\n"
       "Enable advanced mode vty interface\n")
{
	host.advanced = 1;
	return CMD_SUCCESS;
}

/* VTY 命令 "no service advanced-vty"：关闭高级模式 vty（host.advanced = 0）。 */
DEFUN (no_service_advanced_vty,
       no_service_advanced_vty_cmd,
       "no service advanced-vty",
       NO_STR
       "Set up miscellaneous service\n"
       "Enable advanced mode vty interface\n")
{
	host.advanced = 0;
	return CMD_SUCCESS;
}

/* VTY 命令 "terminal monitor [detach]"：把 debug 日志复制到当前终端。
 * 仅支持 vtysh 服务端会话；打开 live log 拿到一个 fd，再经 vty_pass_fd 传给客户端。
 * detach 时使用一个“独立且脱管”的 live log（会话退出后继续）。返回 CMD_SUCCESS。 */
DEFUN_NOSH(terminal_monitor,
	   terminal_monitor_cmd,
	   "terminal monitor [detach]",
	   "Set terminal line parameters\n"
	   "Copy debug output to the current terminal line\n"
	   "Keep logging feed open independent of VTY session\n")
{
	int fd_ret = -1;

	if (vty->type != VTY_SHELL_SERV) {
		vty_out(vty, "%% not supported\n");
		return CMD_WARNING;
	}

	if (argc == 3) {
		struct zlog_live_cfg detach_log = {};

		zlog_live_open(&detach_log, LOG_DEBUG, &fd_ret);
		zlog_live_disown(&detach_log);
	} else
		zlog_live_open(&vty->live_log, LOG_DEBUG, &fd_ret);

	if (fd_ret == -1) {
		vty_out(vty, "%% error opening live log: %m\n");
		return CMD_WARNING;
	}

	vty_pass_fd(vty, fd_ret);
	return CMD_SUCCESS;
}

/* VTY 命令 "no terminal monitor"：关闭本会话的 live log（terminal monitor）。 */
DEFUN_NOSH(no_terminal_monitor,
	   no_terminal_monitor_cmd,
	   "no terminal monitor",
	   NO_STR
	   "Set terminal line parameters\n"
	   "Copy debug output to the current terminal line\n")
{
	zlog_live_close(&vty->live_log);
	return CMD_SUCCESS;
}

/* VTY 命令 "terminal no monitor"：别名，转调 no_terminal_monitor。 */
DEFUN_NOSH(terminal_no_monitor,
	   terminal_no_monitor_cmd,
	   "terminal no monitor",
	   "Set terminal line parameters\n"
	   NO_STR
	   "Copy debug output to the current terminal line\n")
{
	return no_terminal_monitor(self, vty, argc, argv);
}


/* VTY 命令 "show history"：按时间顺序打印本会话的命令历史
 * （环形缓冲，从 hindex+1 起绕一圈遍历）。 */
DEFUN_NOSH (show_history,
       show_history_cmd,
       "show history",
       SHOW_STR
       "Display the session command history\n")
{
	int index;

	for (index = vty->hindex + 1; index != vty->hindex;) {
		if (index == VTY_MAXHIST) {
			index = 0;
			continue;
		}

		if (vty->hist[index] != NULL)
			vty_out(vty, "  %s\n", vty->hist[index]);

		index++;
	}

	return CMD_SUCCESS;
}

/* VTY 命令 "[no] log commands"：开启 / 关闭命令日志。
 * 若守护进程以“永久记录命令”方式启动（vty_log_commands_perm），则 no 分支
 * 只提示并返回 CMD_WARNING，不能关闭。 */
/* vty login. */
DEFPY (log_commands,
       log_commands_cmd,
       "[no] log commands",
       NO_STR
       "Logging control\n"
       "Log all commands\n")
{
	if (no) {
		if (vty_log_commands_perm) {
			vty_out(vty,
				"Daemon started with permanent logging turned on for commands, ignoring\n");
			return CMD_WARNING;
		}

		vty_log_commands = false;
	} else
		vty_log_commands = true;

	return CMD_SUCCESS;
}

/* 把与 vty 相关的当前配置写回配置文本（用于 show running-config）。
 * 依次输出 line vty、access-class（IPv4 / IPv6）、exec-timeout、no login，
 * 以及 log commands；frame 机制保证有内容时才输出 line vty 段。
 * 返回 CMD_SUCCESS。 */
/* Display current configuration. */
static int vty_config_write(struct vty *vty)
{
	vty_frame(vty, "line vty\n");

	if (vty_accesslist_name)
		vty_out(vty, " access-class %s\n", vty_accesslist_name);

	if (vty_ipv6_accesslist_name)
		vty_out(vty, " ipv6 access-class %s\n",
			vty_ipv6_accesslist_name);

	/* exec-timeout */
	if (vty_timeout_val != VTY_TIMEOUT_DEFAULT)
		vty_out(vty, " exec-timeout %ld %ld\n", vty_timeout_val / 60,
			vty_timeout_val % 60);

	/* login */
	if (no_password_check)
		vty_out(vty, " no login\n");

	vty_endframe(vty, "exit\n");

	if (vty_log_commands)
		vty_out(vty, "log commands\n");

	vty_out(vty, "!\n");

	return CMD_SUCCESS;
}

#ifdef HAVE_TCMALLOC
/*
 * Support clis when using the tcmalloc lib. The main cli definition is in
 * vtysh; these NOSH handlers are for individual daemons.
 */
#include "gperftools/tcmalloc.h"
#include "gperftools/malloc_extension_c.h"

/* tcmalloc 统计表的表项结构（label + val）与预置空表；
 * 末尾以 {NULL, 0} 作为哨兵。仅在 HAVE_TCMALLOC 下编译。 */
struct tcm_stats_s {
	const char *label;
	size_t val;
} tcm_stats_table[] = {
	{},
	{},
	{},
	{},
	{NULL, 0},
};

/* VTY 命令 "show tcmalloc stats"：打印 tcmalloc 版本、mallinfo2 统计，
 * 以及若干 MallocExtension 数值属性。仅在 HAVE_TCMALLOC 下编译。 */
DEFUN_NOSH (show_tcmalloc_stats,
	    show_tcmalloc_stats_cmd,
	    "show tcmalloc stats",
	    SHOW_STR
	    "tcmalloc library info\n"
	    "Show tcmalloc stats\n")
{
	struct mallinfo2 minfo;
	char buf[30];
	size_t sval;
	int major = 0, minor = 0;
	const char *str, *patch = NULL;

	str = tc_version(&major, &minor, &patch);

	vty_out(vty, "Tcmalloc version '%s' (%d.%d)\n", str, major, minor);

	minfo = tc_mallinfo2();

	vty_out(vty, "Tcmalloc mallinfo statistics:\n");
	vty_out(vty, "  Total heap allocated:  %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.arena));
	vty_out(vty, "  Holding block headers: %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.hblkhd));
	vty_out(vty, "  Used small blocks:     %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.usmblks));
	vty_out(vty, "  Used ordinary blocks:  %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.uordblks));
	vty_out(vty, "  Free small blocks:     %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.fsmblks));
	vty_out(vty, "  Free ordinary blocks:  %s\n",
		mtype_memstr(buf, sizeof(buf), minfo.fordblks));
	vty_out(vty, "  Ordinary blocks:       %ld\n",
		(unsigned long)minfo.ordblks);
	vty_out(vty, "  Small blocks:          %ld\n",
		(unsigned long)minfo.smblks);
	vty_out(vty, "  Holding blocks:        %ld\n\n",
		(unsigned long)minfo.hblks);

	/* Read some tcmalloc lib values */
	MallocExtension_GetNumericProperty("generic.heap_size", &sval);
	vty_out(vty, "%s: %zu\n", "generic.heap_size", sval);

	MallocExtension_GetNumericProperty("generic.total_physical_bytes", &sval);
	vty_out(vty, "%s: %zu\n", "generic.total_physical_bytes", sval);

	MallocExtension_GetNumericProperty("tcmalloc.pageheap_free_bytes", &sval);
	vty_out(vty, "%s: %zu\n", "tcmalloc.pageheap_free_bytes", sval);

	MallocExtension_GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &sval);
	vty_out(vty, "%s: %zu\n", "tcmalloc.pageheap_unmapped_bytes", sval);

	return CMD_SUCCESS;
}

/* VTY 命令 "memory release rate (0-100)"：设置向 OS 释放空闲内存的速率。
 * 参数取自 argv[3]；调用 frr_mem_release_config。返回 CMD_SUCCESS。 */
DEFUN_NOSH (lib_tcmalloc_config,
	    lib_tcmalloc_config_cmd,
	    "memory release rate (0-100)",
	    "memory library config\n"
	    "Release free memory to OS\n"
	    "Set mem release rate (zero to disable)\n"
	    "Set release rate (MB/sec)\n")
{
	uint32_t rate = 0;

	rate = (uint32_t)atol(argv[3]->arg);

	frr_mem_release_config(rate);

	return CMD_SUCCESS;
}
#endif /* HAVE_TCMALLOC */

/* VTY_NODE 命令节点定义：父节点为 CONFIG_NODE，提示符 "%s(config-line)# "，
 * 其 config_write 回调为 vty_config_write（把 line vty 段的配置写回）。
 * 上面一行是 vty_config_write 的前向声明。 */
static int vty_config_write(struct vty *vty);
struct cmd_node vty_node = {
	.name = "vty",
	.node = VTY_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-line)# ",
	.config_write = vty_config_write,
};

/* 复位所有 VTY 状态：关闭全部普通会话（清缓冲后 vty_close），
 * 把空闲超时恢复默认值，并释放 IPv4 / IPv6 访问控制列表名。
 * 注意：不处理 vtysh 会话（由 vty_terminate 另行处理）。 */
/* Reset all VTY status. */
void vty_reset(void)
{
	struct vty *vty;

	frr_each_safe (vtys, vty_sessions, vty) {
		buffer_reset(vty->lbuf);
		buffer_reset(vty->obuf);
		vty->status = VTY_CLOSE;
		vty_close(vty);
	}

	vty_timeout_val = VTY_TIMEOUT_DEFAULT;

	XFREE(MTYPE_VTY, vty_accesslist_name);
	XFREE(MTYPE_VTY, vty_ipv6_accesslist_name);
}

/* 记录当前工作目录到 vty_cwd（供后续读配置用）。
 * 若 getcwd 失败，则切到 SYSCONFDIR 再取一次；仍失败则记致命错误并 exit(-1)。 */
static void vty_save_cwd(void)
{
	char *c;

	c = getcwd(vty_cwd, sizeof(vty_cwd));

	if (!c) {
		/*
		 * At this point if these go wrong, more than likely
		 * the whole world is coming down around us
		 * Hence not worrying about it too much.
		 */
		if (chdir(SYSCONFDIR)) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "Failure to chdir to %s, errno: %d",
				     SYSCONFDIR, errno);
			exit(-1);
		}
		if (getcwd(vty_cwd, sizeof(vty_cwd)) == NULL) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "Failure to getcwd, errno: %d", errno);
			exit(-1);
		}
	}
}

/* 返回缓存的当前工作目录字符串 vty_cwd（静态存储，勿释放）。 */
char *vty_get_cwd(void)
{
	return vty_cwd;
}

/* 判断该 vty 是否为 vtysh 客户端外壳（VTY_SHELL）。返回 1 / 0。 */
int vty_shell(struct vty *vty)
{
	return vty->type == VTY_SHELL ? 1 : 0;
}

/* 判断该 vty 是否为 vtysh 服务端会话（VTY_SHELL_SERV）。返回 1 / 0。 */
int vty_shell_serv(struct vty *vty)
{
	return vty->type == VTY_SHELL_SERV ? 1 : 0;
}

/* vtysh 侧的 vty 初始化占位函数（当前无内容，预留给将来使用）。 */
void vty_init_vtysh(void)
{
	/* currently nothing to do, but likely to have future use */
}

/* VTY 子系统初始化：各守护进程启动时调用一次。
 * 参数：master_thread 事件循环；do_command_logging 是否永久记录命令日志。
 * 行为：保存当前目录、记录事件循环、注册 atexit 清理、安装 vty 节点与一批命令
 * （who / show history / line vty / service advanced-vty / log commands /
 *  terminal monitor 系 / exec-timeout / access-class / login 等）。
 * 副作用：修改全局命令树，重复调用会重复安装。 */
/* Install vty's own commands like `who' command. */
void vty_init(struct event_loop *master_thread, bool do_command_logging)
{
	/* For further configuration read, preserve current directory. */
	vty_save_cwd();

	vty_master = master_thread;

	atexit(vty_stdio_atexit);

	/* Install bgp top node. */
	install_node(&vty_node);

	install_element(VIEW_NODE, &config_who_cmd);
	install_element(VIEW_NODE, &show_history_cmd);
	install_element(CONFIG_NODE, &line_vty_cmd);
	install_element(CONFIG_NODE, &service_advanced_vty_cmd);
	install_element(CONFIG_NODE, &no_service_advanced_vty_cmd);
	install_element(CONFIG_NODE, &show_history_cmd);
	install_element(CONFIG_NODE, &log_commands_cmd);

	if (do_command_logging) {
		vty_log_commands = true;
		vty_log_commands_perm = true;
	}

	install_element(ENABLE_NODE, &terminal_monitor_cmd);
	install_element(ENABLE_NODE, &terminal_no_monitor_cmd);
	install_element(ENABLE_NODE, &no_terminal_monitor_cmd);

	install_default(VTY_NODE);
	install_element(VTY_NODE, &exec_timeout_min_cmd);
	install_element(VTY_NODE, &exec_timeout_sec_cmd);
	install_element(VTY_NODE, &no_exec_timeout_cmd);
	install_element(VTY_NODE, &vty_access_class_cmd);
	install_element(VTY_NODE, &no_vty_access_class_cmd);
	install_element(VTY_NODE, &vty_login_cmd);
	install_element(VTY_NODE, &no_vty_login_cmd);
	install_element(VTY_NODE, &vty_ipv6_access_class_cmd);
	install_element(VTY_NODE, &no_vty_ipv6_access_class_cmd);

#ifdef HAVE_TCMALLOC
	install_element(VIEW_NODE, &show_tcmalloc_stats_cmd);
	install_element(CONFIG_NODE, &lib_tcmalloc_config_cmd);
#endif /* HAVE_TCMALLOC */
}

/* 关闭 VTY 子系统：清空 cwd 缓存，调用 vty_reset 关掉普通会话，
 * 再单独关闭全部 vtysh 会话（vty_reset 不处理它们），最后停掉所有监听服务端。 */
void vty_terminate(void)
{
	struct vty *vty;

	memset(vty_cwd, 0x00, sizeof(vty_cwd));

	vty_reset();

	/* default state of vty_sessions is initialized & empty. */
	vtys_fini(vty_sessions);
	vtys_init(vty_sessions);

	/* vty_reset() doesn't close vtysh sessions */
	frr_each_safe (vtys, vtysh_sessions, vty) {
		buffer_reset(vty->lbuf);
		buffer_reset(vty->obuf);
		vty->status = VTY_CLOSE;
		vty_close(vty);
	}

	vtys_fini(vtysh_sessions);
	vtys_init(vtysh_sessions);

	vty_serv_stop();
}

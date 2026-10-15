// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2019-22  David Lamparter, for NetDEF, Inc.
 */

/*
 * 文件导读（zlog_live.c）
 *
 * 实现 zlog_live.h 声明的"实时日志流"目标。核心思路：用一对 Unix 套接字，
 * 一端作为日志目标 fd，另一端交给调用方（如 vtysh 会话）去读取；每当有日志
 * 产生，zlog 框架就回调本文件的 zlog_live，把消息以"头部 + 可选参数位置 +
 * 正文"这 1~3 段 iovec 一次性（sendmmsg）投递给客户端。
 *
 * 关键点：
 *  - struct zlt_live：目标对象，内嵌 struct zlog_target（框架句柄），持有
 *    fd（原子）、丢失计数（原子）、状态（原子）以及 RCU 回收头。
 *  - 非阻塞、尽力而为：套接字写满时不阻塞日志线程，而是把未发出的消息计入
 *    lost_msgs 后放弃（best-effort）。
 *  - 两条回调：zlog_live 走常规批量（sendmmsg）；zlog_live_sigsafe 走信号安全
 *    路径（只用 writev 与 clock_gettime）。
 *  - 状态机 STATE_NORMAL / STATE_FD_DEAD / STATE_DISOWNED 用于协调"写失败要
 *    关闭"与"用户主动 disown"两种清理时机，避免重复释放对象。
 *
 * 使用流程：zlog_live_open / zlog_live_open_fd 建立目标 -> 运行期由 zlog 框架
 * 回调投递 -> zlog_live_close 或 zlog_live_disown 收尾。
 */
#include "zebra.h"

#include "frrsendmmsg.h"

#include "zlog_live.h"

#include "memory.h"
#include "frrcu.h"
#include "zlog.h"
#include "log.h"
#include "log_vty.h"
#include "printfrr.h"
#include "network.h"

/* 定义本文件私有的内存类型：用于统计实时日志流目标对象的内存占用 */
DEFINE_MTYPE_STATIC(LOG, LOG_LIVE, "log vtysh live target");

/*
 * 目标生命周期状态（原子变量）。NORMAL=正常工作；FD_DEAD=写失败后 fd 已被
 * 关闭、目标待回收；DISOWNED=调用方已 disown，对象的所有权已交出。
 * 用 CAS 在两条清理路径之间协调，确保只有一方真正释放对象。
 */
enum {
	STATE_NORMAL = 0,
	STATE_FD_DEAD,
	STATE_DISOWNED,
};

/*
 * 实时日志流目标对象。zt 是框架要求的基类（zlog_target），必须作为首成员，
 * 以便用 container_of 从基类反查回本结构。
 *  - fd：原子保存的目标套接字 fd，-1 表示已失效。
 *  - head_close：RCU 延迟关闭 fd 用的回收头。
 *  - head_self：RCU 延迟释放本对象用的回收头。
 *  - state：见上面的 STATE_* 状态机。
 *  - lost_msgs：因非阻塞写失败而丢弃的消息计数（原子）。
 */
struct zlt_live {
	struct zlog_target zt;

	atomic_uint_fast32_t fd;
	struct rcu_head_close head_close;
	struct rcu_head head_self;

	atomic_uint_fast32_t state;
	atomic_uint_fast32_t lost_msgs;
};

/*
 * 常规投递回调：批量把 nmsgs 条日志推给客户端。
 *
 * 对每条消息先按 prio_min 过滤，再构造 1~3 个 iovec（头部、可选的 printf
 * 参数位置数组、正文），用 sendmmsg 一次发送多条。未发完的消息（套接字写满
 * 或 EAGAIN/EWOULDBLOCK）计入 lost_msgs 后放弃，保证不阻塞日志线程。若
 * sendmmsg 返回真实错误，则走 out_err：原子地关闭 fd、从框架摘除目标，并按
 * 状态决定是否立即释放对象。
 *
 * 易踩的坑：栈上用了变长数组 hdrs / mmhs / iovs，nmsgs 由调用方限定；iovs
 * 的容量是 nmsgs*3（每条消息最多 3 段）。
 */
static void zlog_live(struct zlog_target *zt, struct zlog_msg *msgs[],
		      size_t nmsgs)
{
	struct zlt_live *zte = container_of(zt, struct zlt_live, zt);
	struct zlog_live_hdr hdrs[nmsgs], *hdr = hdrs;
	struct mmsghdr mmhs[nmsgs], *mmh = mmhs;
	struct iovec iovs[nmsgs * 3], *iov = iovs;
	struct timespec ts;
	size_t i, textlen;
	int fd;
	uint_fast32_t state;

	fd = atomic_load_explicit(&zte->fd, memory_order_relaxed);

	if (fd < 0)
		return;

	memset(mmhs, 0, sizeof(mmhs));
	memset(hdrs, 0, sizeof(hdrs));

	for (i = 0; i < nmsgs; i++) {
		const struct fmt_outpos *argpos;
		size_t n_argpos, texthdrlen;
		struct zlog_msg *msg = msgs[i];
		int prio = zlog_msg_prio(msg);
		const struct xref_logmsg *xref;
		intmax_t pid, tid;

		if (prio > zt->prio_min)
			continue;

		zlog_msg_args(msg, &texthdrlen, &n_argpos, &argpos);

		mmh->msg_hdr.msg_iov = iov;

		iov->iov_base = hdr;
		iov->iov_len = sizeof(*hdr);
		iov++;

		if (n_argpos) {
			iov->iov_base = (char *)argpos;
			iov->iov_len = sizeof(*argpos) * n_argpos;
			iov++;
		}

		iov->iov_base = (char *)zlog_msg_text(msg, &textlen);
		iov->iov_len = textlen;
		iov++;

		zlog_msg_tsraw(msg, &ts);
		zlog_msg_pid(msg, &pid, &tid);
		xref = zlog_msg_xref(msg);

		hdr->ts_sec = ts.tv_sec;
		hdr->ts_nsec = ts.tv_nsec;
		hdr->pid = pid;
		hdr->tid = tid;
		hdr->lost_msgs = atomic_load_explicit(&zte->lost_msgs,
						      memory_order_relaxed);
		hdr->prio = prio;
		hdr->flags = 0;
		hdr->ts_subsec = zt_file.ts_subsec;
		hdr->textlen = textlen;
		hdr->texthdrlen = texthdrlen;
		hdr->n_argpos = n_argpos;
		if (xref) {
			memcpy(hdr->uid, xref->xref.xrefdata->uid,
			       sizeof(hdr->uid));
			hdr->ec = xref->ec;
		} else {
			memset(hdr->uid, 0, sizeof(hdr->uid));
			hdr->ec = 0;
		}
		hdr->hdrlen = sizeof(*hdr) + sizeof(*argpos) * n_argpos;

		mmh->msg_hdr.msg_iovlen = iov - mmh->msg_hdr.msg_iov;
		mmh++;
		hdr++;
	}

	size_t msgtotal = mmh - mmhs;
	ssize_t sent;

	for (size_t msgpos = 0; msgpos < msgtotal; msgpos += sent) {
		sent = sendmmsg(fd, mmhs + msgpos, msgtotal - msgpos, 0);

		if (sent <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			atomic_fetch_add_explicit(&zte->lost_msgs,
						  msgtotal - msgpos,
						  memory_order_relaxed);
			break;
		}
		if (sent <= 0)
			goto out_err;
	}
	return;

out_err:
	fd = atomic_exchange_explicit(&zte->fd, -1, memory_order_relaxed);
	if (fd < 0)
		return;

	rcu_close(&zte->head_close, fd);
	zlog_target_replace(zt, NULL);

	state = STATE_NORMAL;
	atomic_compare_exchange_strong_explicit(
		&zte->state, &state, STATE_FD_DEAD, memory_order_relaxed,
		memory_order_relaxed);
	if (state == STATE_DISOWNED)
		rcu_free(MTYPE_LOG_LIVE, zte, head_self);
}

/*
 * 信号安全投递回调：在信号处理上下文中输出一条文本日志。
 * 只能使用异步信号安全的系统调用（clock_gettime 与 writev），因此只发两段
 * iovec（小头部 + 正文），不发送参数位置、也不批量。头部时间戳直接取自
 * CLOCK_REALTIME，优先级固定为 LOG_CRIT。
 */
static void zlog_live_sigsafe(struct zlog_target *zt, const char *text,
			      size_t len)
{
	struct zlt_live *zte = container_of(zt, struct zlt_live, zt);
	struct zlog_live_hdr hdr[1] = {};
	struct iovec iovs[2], *iov = iovs;
	struct timespec ts;
	int fd;

	fd = atomic_load_explicit(&zte->fd, memory_order_relaxed);
	if (fd < 0)
		return;

	clock_gettime(CLOCK_REALTIME, &ts);

	hdr->ts_sec = ts.tv_sec;
	hdr->ts_nsec = ts.tv_nsec;
	hdr->prio = LOG_CRIT;
	hdr->ts_subsec = zt_file.ts_subsec;
	hdr->textlen = len;

	iov->iov_base = (char *)hdr;
	iov->iov_len = sizeof(hdr);
	iov++;

	iov->iov_base = (char *)text;
	iov->iov_len = len;
	iov++;

	writev(fd, iovs, iov - iovs);
}

/*
 * 打开实时日志流目标：创建一对 Unix 套接字，一端用于日志输出，另一端通过
 * other_fd 返回给调用方读取（如 vtysh 会话）。优先使用 SOCK_SEQPACKET（能
 * 收到对端关闭通知，避免僵尸 fd），失败再退回 SOCK_DGRAM；SEQPACKET 成功时
 * 会把读方向 shutdown 掉。prio_min 为 ZLOG_DISABLED 时直接返回（不启用）。
 * 若已有 target 会先关闭旧的。
 */
void zlog_live_open(struct zlog_live_cfg *cfg, int prio_min, int *other_fd)
{
	int sockets[2];

	if (cfg->target)
		zlog_live_close(cfg);

	*other_fd = -1;
	if (prio_min == ZLOG_DISABLED)
		return;

	/* the only reason for SEQPACKET here is getting close notifications.
	 * otherwise if you open a bunch of vtysh connections with live logs
	 * and close them all, the fds will stick around until we get an error
	 * when trying to log something to them at some later point -- which
	 * eats up fds and might be *much* later for some daemons.
	 */
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) < 0) {
		if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) < 0) {
			zlog_warn("%% could not open socket pair: %m");
			return;
		}
	} else
		/* SEQPACKET only: try to zap read direction */
		shutdown(sockets[0], SHUT_RD);

	*other_fd = sockets[1];
	zlog_live_open_fd(cfg, prio_min, sockets[0]);
}

/*
 * 用一个已存在的 fd（如调用方已建好的套接字）打开实时日志流目标：分配
 * zlt_live、把 fd 设为非阻塞、填写回调函数，并注册到 zlog 框架。
 * 若已有 target 会先关闭旧的。
 */
void zlog_live_open_fd(struct zlog_live_cfg *cfg, int prio_min, int fd)
{
	struct zlt_live *zte;
	struct zlog_target *zt;

	if (cfg->target)
		zlog_live_close(cfg);

	zt = zlog_target_clone(MTYPE_LOG_LIVE, NULL, sizeof(*zte));
	zte = container_of(zt, struct zlt_live, zt);
	cfg->target = zte;

	set_nonblocking(fd);
	zte->fd = fd;
	zte->zt.prio_min = prio_min;
	zte->zt.logfn = zlog_live;
	zte->zt.logfn_sigsafe = zlog_live_sigsafe;

	zlog_target_replace(NULL, zt);
}

/*
 * 关闭实时日志流目标：摘除 FD、从框架注销目标，并通过 RCU 释放对象，
 * cfg->target 置空。若本来就没有 target 则直接返回。
 */
void zlog_live_close(struct zlog_live_cfg *cfg)
{
	struct zlt_live *zte;
	int fd;

	if (!cfg->target)
		return;

	zte = cfg->target;
	cfg->target = NULL;

	fd = atomic_exchange_explicit(&zte->fd, -1, memory_order_relaxed);

	if (fd >= 0) {
		rcu_close(&zte->head_close, fd);
		zlog_target_replace(&zte->zt, NULL);
	}
	rcu_free(MTYPE_LOG_LIVE, zte, head_self);
}

/*
 * 放弃对目标的所有权但不主动关闭 fd：把对象状态置为 DISOWNED 并从 cfg 摘除。
 * 若此刻对象已处于 FD_DEAD（写失败路径已判定待回收），则由本函数负责释放，
 * 避免泄漏。
 */
void zlog_live_disown(struct zlog_live_cfg *cfg)
{
	struct zlt_live *zte;
	uint_fast32_t state;

	if (!cfg->target)
		return;

	zte = cfg->target;
	cfg->target = NULL;

	state = STATE_NORMAL;
	atomic_compare_exchange_strong_explicit(
		&zte->state, &state, STATE_DISOWNED, memory_order_relaxed,
		memory_order_relaxed);
	if (state == STATE_FD_DEAD)
		rcu_free(MTYPE_LOG_LIVE, zte, head_self);
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Network library.
 * Copyright (C) 1997 Kunihiro Ishiguro
 */
/*
 * 【文件导读】network.c —— 网络 I/O、字节序与序列号的小工具箱
 * =====================================================================
 * 【是什么】这是一个“杂项工具箱”，把许多模块都要用到、又很琐碎的底层
 *   网络操作集中到一处，避免每个守护进程各写一份。包括：
 *   - 面向阻塞 fd 的整块读写 readn / writen；
 *   - 设置 fd 模式：set_nonblocking(非阻塞) / set_cloexec(执行 exec 时关闭)；
 *   - 单精度浮点的字节序转换 htonf / ntohf；
 *   - 基于单调时钟的递增序列号 frr_sequence_next / frr_sequence32_next。
 *
 * 【为什么放在一起】调用方遍布整个 FRR(zebra 与各协议守护进程)，且都不
 *   依赖任何具体协议逻辑，故归入 lib 公共层；声明见 network.h。
 *
 * 【核心实现要点】
 *   - readn / writen 循环直到读满 / 写满 nbytes，或遇到错误 / EOF；
 *     writen 对 EAGAIN / EWOULDBLOCK / EINTR 会重试(见 ERRNO_IO_RETRY)。
 *   - set_nonblocking 用 F_GETFL 读标志再或上 O_NONBLOCK 回写；失败经
 *     flog_err(EC_LIB_SYSTEM_CALL) 记录后返回 -1。
 *   - set_cloexec 用 F_GETFD / F_SETFD 设置 FD_CLOEXEC，静默失败返回 -1。
 *   - htonf 通过 memcpy 把 float 当 uint32 做 htonl，规避类型别名问题。
 *   - frr_sequence_next 用 static 变量加 CLOCK_MONOTONIC，同一秒内自增，
 *     保证序列号单调不回头(仅回绕 / 溢出时例外)，但**非线程安全**。
 *
 * 【阅读顺序】readn -> writen -> set_nonblocking -> set_cloexec ->
 *   htonf -> ntohf -> frr_sequence_next -> frr_sequence32_next。
 *
 * 【易踩的坑】
 *   1) readn / writen 已被标记为“不推荐”，对非阻塞 fd 不适用，新代码勿用；
 *   2) ntohf 与 htonf 是同一套实现(字节序转换是自反的)；
 *   3) frr_sequence_next 依赖 static 状态，多线程同时调用会竞争。
 * =====================================================================
 */

#include <zebra.h>
#include <fcntl.h>
#include "log.h"
#include "network.h"
#include "lib_errors.h"

/* 【函数】从 fd 循环读取 nbytes 字节到 ptr 指向的缓冲。
 * 参数 fd : 文件描述符；ptr : 目标缓冲；nbytes : 期望读入字节数。
 * 返回：实际读入的字节数；若中途 read 出错则返回负的 errno 值。
 * 语义：循环直到读满 nbytes；read 返回 0(EOF)时提前结束，
 *   最终返回 nbytes 减去剩余未读字节数。
 * 【易踩的坑】面向阻塞 fd 设计，对非阻塞 fd 遇到 EAGAIN 会立即当作错误返回；
 *   已标记 deprecated。 */
/* Read nbytes from fd and store into ptr. */
int readn(int fd, uint8_t *ptr, int nbytes)
{
	int nleft;
	int nread;

	nleft = nbytes;

	while (nleft > 0) {
		nread = read(fd, ptr, nleft);

		if (nread < 0)
			return (nread);
		else if (nread == 0)
			break;

		nleft -= nread;
		ptr += nread;
	}

	return nbytes - nleft;
}

/* 【函数】把 ptr 处的 nbytes 字节循环写出到 fd。
 * 参数 fd / ptr / nbytes。
 * 返回：实际写出的字节数；写失败返回负值或 0。
 * 语义：对 EAGAIN / EWOULDBLOCK / EINTR 会重试(经 ERRNO_IO_RETRY)，
 *   write 返回 0 视为异常并立即返回 0。
 * 【易踩的坑】同样不适合非阻塞 fd；已标记 deprecated。 */
/* Write nbytes from ptr to fd. */
int writen(int fd, const uint8_t *ptr, int nbytes)
{
	int nleft;
	int nwritten;

	nleft = nbytes;

	while (nleft > 0) {
		nwritten = write(fd, ptr, nleft);

		if (nwritten < 0) {
			if (!ERRNO_IO_RETRY(errno))
				return nwritten;
		}
		if (nwritten == 0)
			return (nwritten);

		nleft -= nwritten;
		ptr += nwritten;
	}
	return nbytes - nleft;
}

/* 【函数】把 fd 设为非阻塞模式。
 * 参数 fd。返回：成功 0，失败 -1。
 * 实现：先 F_GETFL 取当前标志(按 Single UNIX Spec 不应返回负值)，再或上
 *   O_NONBLOCK 用 F_SETFL 回写；任一步失败都 flog_err(EC_LIB_SYSTEM_CALL)
 *   记录并返回 -1。
 * 【易踩的坑】F_SETFL 只能改 O_NONBLOCK 等少数“可改”标志，不会动访问模式
 *   (O_RDONLY / O_WRONLY 之类)。 */
int set_nonblocking(int fd)
{
	int flags;

	/* According to the Single UNIX Spec, the return value for F_GETFL
	   should
	   never be negative. */
	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		flog_err(EC_LIB_SYSTEM_CALL,
			 "fcntl(F_GETFL) failed for fd %d: %s", fd,
			 safe_strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETFL, (flags | O_NONBLOCK)) < 0) {
		flog_err(EC_LIB_SYSTEM_CALL,
			 "fcntl failed setting fd %d non-blocking: %s", fd,
			 safe_strerror(errno));
		return -1;
	}
	return 0;
}

/* 【函数】把 fd 设为 close-on-exec(进程执行 exec 时自动关闭该 fd)。
 * 参数 fd。返回：成功 0，失败 -1。
 * 实现：F_GETFD 读描述符标志，或上 FD_CLOEXEC 后 F_SETFD 回写。
 * 【易踩的坑】与 set_nonblocking 不同，这里失败**不**记日志，静默返回 -1；
 *   且用的是 FD_CLOEXEC(描述符标志)而非 O_CLOEXEC(打开标志)。 */
int set_cloexec(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFD, 0);
	if (flags == -1)
		return -1;

	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1)
		return -1;
	return 0;
}

/* 【函数】把宿主字节序的单精度浮点转成网络字节序。
 * 参数 host : 宿主序的 float。返回：网络序表示的 float(按位转换)。
 * 实现：用 memcpy 把 float 的位模式搬进 uint32，做 htonl 后再搬回 float，
 *   规避对 float 直接取址 / 类型别名带来的未定义行为。
 * 说明：IEEE754 单精度在网络中就是 4 字节，等价于把这 4 字节按网络序排列。 */
float htonf(float host)
{
	uint32_t lu1, lu2;
	float convert;

	memcpy(&lu1, &host, sizeof(uint32_t));
	lu2 = htonl(lu1);
	memcpy(&convert, &lu2, sizeof(uint32_t));
	return convert;
}

/* 【函数】把网络字节序的单精度浮点转回宿主序。
 * 参数 net : 网络序的 float。返回：宿主序的 float。
 * 实现：直接复用 htonf —— 字节序转换是自反的(再转一次即还原)。 */
float ntohf(float net)
{
	return htonf(net);
}

/* 【函数】基于单调时钟生成一个“尽量唯一且不回头”的递增序列号。
 * 参数：无。返回：uint64_t 序列号。
 * 实现：用 CLOCK_MONOTONIC 取秒；若本次秒数与上次相同，则在上次基础上加 1，
 *   否则直接采用当前秒数。于是同一秒内多次调用也能得到严格递增的值。
 * 【易踩的坑】1) 使用 static 变量故**非线程安全**；
 *   2) 只在时钟回绕 / 溢出时可能“回头”；
 *   3) 用单调钟而非墙上钟，避免受系统改时间影响。 */
uint64_t frr_sequence_next(void)
{
	static uint64_t last_sequence;
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	if (last_sequence == (uint64_t)ts.tv_sec) {
		last_sequence++;
		return last_sequence;
	}

	last_sequence = ts.tv_sec;
	return last_sequence;
}

/* 【函数】frr_sequence_next 的 32 位截断版本。
 * 参数：无。返回：截断为 uint32_t 的序列号。
 * 说明：函数内标注了 coverity 的 Y2K38_SAFETY 以屏蔽 32 位截断告警；
 *   适合填入协议头里只有 32 位宽度的序列号域。 */
uint32_t frr_sequence32_next(void)
{
	/* coverity[Y2K38_SAFETY] */
	return (uint32_t)frr_sequence_next();
}

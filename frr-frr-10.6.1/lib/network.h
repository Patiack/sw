// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Network library header.
 * Copyright (C) 1998 Kunihiro Ishiguro
 */
/*
 * 【文件导读】network.h —— 网络 I/O、字节序与序列号的公共接口声明
 * =====================================================================
 * 【是什么】本头文件与 network.c 配套，声明 FRR 里被广泛复用的底层网络
 *   小工具：阻塞式整块读写、fd 模式设置、浮点字节序转换、单调序列号生成，
 *   以及若干经过封装的字节序宏(be64toh / htobe64)。
 *
 * 【包含内容一览】
 *   - readn / writen      : 整块读写(已弃用，注明不适合非阻塞 fd)；
 *   - set_nonblocking     : 置 fd 非阻塞；
 *   - set_cloexec         : 置 fd close-on-exec；
 *   - ERRNO_IO_RETRY      : 判断 errno 是否属“可重试”类(EAGAIN 等)；
 *   - htonf / ntohf       : float 的字节序转换；
 *   - be64toh / htobe64   : 被本头文件重新定义，强制返回 uint64_t；
 *   - frr_sequence_next 等: 基于单调时钟的递增序列号生成器；
 *   - frr_weak_random     : 弱随机数(用于抖动 / 简单用途)。
 *
 * 【为什么自己重定义 be64toh / htobe64】系统提供的这对宏在有些编译器 /
 *   平台上会让 FRR 的 frr-format GCC 插件误报 printfrr 警告。诀窍是用
 *   “语句表达式”先给变量赋值再返回变量——对变量不会触发告警，而直接写成
 *   (uint64_t) 强转才会触发。故先 #undef 系统宏，再按本机字节序重新定义。
 *
 * 【与其他模块关系】声明被几乎全部守护进程引用；函数实现在 network.c。
 *
 * 【阅读顺序】函数声明 -> ERRNO_IO_RETRY -> 字节序宏(be64toh 段) ->
 *   序列号 / 随机数内联函数。
 *
 * 【易踩的坑】
 *   1) 这对 be64toh / htobe64 依赖 BYTE_ORDER，未知字节序会在 #error 处报错；
 *   2) frr_sequence_next 用 static 变量，非线程安全；
 *   3) readn / writen 已废弃，勿用于非阻塞 fd。
 * =====================================================================
 */

#ifndef _ZEBRA_NETWORK_H
#define _ZEBRA_NETWORK_H

#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
#include <endian.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 【接口】整块读写(实现在 network.c)。注意：二者已标注为弃用，对
 *   非阻塞 fd 不适用——非阻塞下可能只读写部分数据或被 EAGAIN 打断。
 *   readn : 循环读满 nbytes 到 ptr，返回实际读入数(出错返回负值)。
 *   writen: 循环写出 nbytes；可重试类 errno 会重试(见 ERRNO_IO_RETRY)。 */
/* Both readn and writen are deprecated and will be removed.  They are not
   suitable for use with non-blocking file descriptors.
 */
extern int readn(int fd, uint8_t *ptr, int nbytes);
extern int writen(int fd, const uint8_t *ptr, int nbytes);

/* 【接口】把 fd 设为非阻塞。成功返回 0，失败返回 -1(失败会记录错误日志)。 */
/* Set the file descriptor to use non-blocking I/O.  Returns 0 for success,
   -1 on error. */
extern int set_nonblocking(int fd);

/* 【接口】把 fd 设为 close-on-exec。成功返回 0，失败返回 -1
 *   (与 set_nonblocking 不同，这里失败不记日志)。 */
extern int set_cloexec(int fd);

/* Does the I/O error indicate that the operation should be retried later? */
/* 【宏】判断某 errno 是否表示“可稍后重试”，而非真正的致命错误。
 * 参数 EN : errno 值。返回：EAGAIN / EWOULDBLOCK / EINTR 之一时非 0(真)。
 * 用途：writen 等循环在遇到这些错误时选择重试而非退出。
 * 【易踩的坑】EWOULDBLOCK 与 EAGAIN 在多数平台同值，这里一并列出以防差异。 */
#define ERRNO_IO_RETRY(EN)                                                     \
	(((EN) == EAGAIN) || ((EN) == EWOULDBLOCK) || ((EN) == EINTR))

/* 【接口】单精度 float 与网络字节序之间的转换(实现在 network.c)。
 *   htonf : 宿主序 -> 网络序；ntohf : 网络序 -> 宿主序(同一实现，转换自反)。 */
extern float htonf(float host);
extern float ntohf(float net);

/* force type for be64toh/htobe64 to be uint64_t, *without* a direct cast
 *
 * this is a workaround for false-positive printfrr warnings from FRR's
 * frr-format GCC plugin that would be triggered from
 * { printfrr("%"PRIu64, (uint64_t)be64toh(...)); }
 *
 * the key element here is that "(uint64_t)expr" causes the warning, while
 * "({ uint64_t x = expr; x; })" does not.  (The cast is the trigger, a
 * variable of the same type works correctly.)
 */

/* 【字节序宏】以下重新定义 be64toh / htobe64，使其返回类型确定为 uint64_t。
 * 背景：某些平台上系统提供的这对宏会被 FRR 的 frr-format GCC 插件误判并产生
 *   printfrr 格式告警。规避手法：用语句表达式“先赋给 uint64_t 变量再返回该
 *   变量”，因为对变量取值不会触发告警，而写成 (uint64_t) 强转就会触发(上方
 *   英文注释即在说明这一点)。
 * 做法：先 #undef 掉系统定义，再按本机字节序重定义——小端需 bswap64 翻转，
 *   大端为恒等；未知字节序直接 #error。
 * 【易踩的坑】此处依赖 <endian.h> 提供的 BYTE_ORDER；
 *   64 位转换自反：be64toh 与 htobe64 在两种字节序下实现对称。 */
/* zap system definitions... */
#ifdef be64toh
#undef be64toh
#endif
#ifdef htobe64
#undef htobe64
#endif

/* 【分支】按本机字节序选择实现：小端做字节翻转(__builtin_bswap64)，大端
 *   恒等返回；两者都借助语句表达式把结果放进 uint64_t 变量再返回，以配合
 *   上文的 frr-format 规避手法。 */
#if BYTE_ORDER == LITTLE_ENDIAN
#define be64toh(x)	({ uint64_t r = __builtin_bswap64(x); r; })
#define htobe64(x)	({ uint64_t r = __builtin_bswap64(x); r; })
#elif BYTE_ORDER == BIG_ENDIAN
#define be64toh(x)	({ uint64_t r = (x); r; })
#define htobe64(x)	({ uint64_t r = (x); r; })
#else
#error nobody expects the endianish inquisition. check OS endian.h headers.
#endif

/* 【接口】取下一个基于单调时钟的递增序列号(实现在 network.c)。
 * 返回：uint64_t；同一秒内多次调用会自增以保证严格递增，且不回头
 *   (仅回绕 / 溢出例外)。之所以不直接用 time() 等返回 time_t 的函数，是为
 *   避免 time_t 截断告警，统一改走本接口。
 * 【易踩的坑】内部用 static 变量，**非线程安全**。 */
/**
 * Generate a sequence number using monotonic clock with a same second call
 * protection to help guarantee a unique incremental sequence number that never
 * goes back (except when wrapping/overflow).
 *
 * **NOTE** this function is not thread safe since it uses `static` variable.
 *
 * This function and `frr_sequence32_next` should be used to initialize
 * sequence numbers without directly calling other `time_t` returning
 * functions because of `time_t` truncation warnings.
 *
 * \returns `uint64_t` number based on the monotonic clock.
 */
extern uint64_t frr_sequence_next(void);

/* 【接口】frr_sequence_next 的 32 位截断版本，供只放 32 位序列号域的
 *   协议头使用。 */
/** Same as `frr_sequence_next` but returns truncated number. */
extern uint32_t frr_sequence32_next(void);

/* 【内联函数】返回一个弱随机 long，封装了 random() 调用。
 * 用途：生成抖动(jitter)或用于只需简单弱随机的场合。
 * 【易踩的坑】random() 并非密码学安全；此处封装主要是把 random() 调用集中
 *   到一处，以屏蔽 coverity 扫描对直接调用随机数的告警(见内联体上方的
 *   coverity[dont_call] 标注)。 */
/**
 * Helper function that returns a random long value. The main purpose of
 * this function is to hide a `random()` call that gets flagged by coverity
 * scan and put it into one place.
 *
 * The main usage of this function should be for generating jitter or weak
 * random values for simple purposes.
 *
 * See 'man 3 random' for more information.
 *
 * \returns random long integer.
 */
static inline long frr_weak_random(void)
{
	/* coverity[dont_call] */
	return random();
}

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_NETWORK_H */

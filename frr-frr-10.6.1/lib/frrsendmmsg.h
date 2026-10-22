// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FRR sendmmsg wrapper
 * Copyright (C) 2024 by Nvidia, Inc.
 *                       Donald Sharp
 */
#ifndef __FRRSENDMMSG_H__
#define __FRRSENDMMSG_H__

/* ============================================================================
 * 【文件导读】frrsendmmsg.h —— sendmmsg() 的可移植性垫片
 * ============================================================================
 * 【sendmmsg 是什么】Linux 特有的批量发送接口：一次系统调用发送多个数据报，
 * 把 N 次 sendmsg 的上下文切换合并成 1 次。对“一秒钟要吐几万个 BGP 更新”的
 * FRR 来说，这是实打实的性能点。
 *
 * 【为什么需要垫片】① sendmmsg 是 Linux 专有，非 Linux 平台没有；② 即使在
 * Linux 上，老内核/老 libc 也可能缺 struct mmsghdr 或该函数。原注释说
 * "avoid conflicts in case we have partial support" —— 意思是**部分支持**最
 * 麻烦：可能结构体有了但函数没有，或反之。
 *
 * 【本文件的解法：宏重命名 + 单发降级】
 *   ① 把 mmsghdr / sendmmsg 两个标识符**宏替换**成本文件的 mmsghdr /
 *      frr_sendmmsg(见下)，从而既提供缺失的定义，又不与可能已存在的系统符号
 *      冲突。注意宏里的第二个 `#define sendmmsg frr_sendmmsg` 展开后就是
 *      `#define frr_sendmmsg frr_sendmmsg`——自指宏在 C 里是合法的空操作，
 *      这是上游有意写的“防重复定义”技巧，初看会以为是笔误。
 *   ② 降级实现：**每次只发一个**，靠上层循环把剩下的发完(原注释已说明)。
 *
 * 【降级实现的语义细节(最值得记的一点)】真 sendmmsg 返回**成功发送的报文个数**；
 * 因此降级版也返回 1 而非字节数——`return rv > 0 ? 1 : rv;` 就是为此：sendmsg
 * 成功时 rv 是字节数，必须归一成“1 个报文”；失败时 rv <= 0，原样返回错误码。
 * 【易踩的坑】调用方必须像对待真 sendmmsg 一样处理“返回值小于 len”的部分成功
 *   情况；在降级实现下这会**每次**发生，若调用方假设“要么全发要么全失败”，
 *   行为会与真实现不同。 */
#if !defined(HAVE_STRUCT_MMSGHDR_MSG_HDR) || !defined(HAVE_SENDMMSG)
/* avoid conflicts in case we have partial support */
/* 【重命名宏】把对外名字映射到本文件提供的名字，避免与系统定义撞名。 */
#define mmsghdr	 frr_mmsghdr
#define sendmmsg frr_sendmmsg

/* 【结构体】sendmmsg 的参数元素：一个完整报文的描述 + 该报文实际发送长度。
 *   msg_hdr: 标准的 sendmsg 头(目标地址、iovec 数组、控制信息)。
 *   msg_len: **输出**字段，由 sendmmsg 回填每个报文实际发了多少字节。 */
struct mmsghdr {
	struct msghdr msg_hdr;
	unsigned int msg_len;
};

/* 【降级实现】只发第 0 个报文，其余交给上层的循环(原注释)。
 *   mmh/len: 报文数组及其个数——注意本实现**只读 mmh[0]**，len 仅用于语义对齐。
 *   返回    : 成功返回 1(表示“发了 1 个报文”)，失败返回负错误码。
 * 【易踩的坑】① 只有 mmh[0].msg_len 会(可能)被回填，其余元素保持调用方原值；
 *   ② flags 被**丢弃**，直接以 0 调 sendmsg——传 MSG_DONTWAIT 之类语义的调用方
 *   在降级路径上会失去非阻塞保证；③ 因为是 static inline，每个包含者各有一份，
 *   不会有多个定义冲突，但也意味着改动后必须重新编译所有包含者。 */
/* just go 1 at a time here, the loop this is used in will handle the rest */
static inline int sendmmsg(int fd, struct mmsghdr *mmh, unsigned int len,
			   int flags)
{
	int rv = sendmsg(fd, &mmh->msg_hdr, 0);

	return rv > 0 ? 1 : rv;
}
#endif

#endif

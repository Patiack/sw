// SPDX-License-Identifier: GPL-2.0-or-later
/* C-Ares integration to Quagga mainloop
 * Copyright (c) 2014-2015 Timo Teräs
 */

/*
 * ============================================================
 * 文件导读：resolver.h —— 异步 DNS 解析器接口（c-ares 集成）
 * ============================================================
 *
 * 这个文件在 FRR 里干什么
 *   FRR 里很多配置项是主机名（如 BGP neighbor 用域名、日志服务器等），
 *   需要解析成 IP。同步的 getaddrinfo 会阻塞主事件循环导致协议超时，
 *   因此 FRR 用 c-ares 库做异步解析，把结果通过回调交还给调用方。本
 *   头文件定义查询对象与三个对外接口；实现见 resolver.c。
 *
 * 核心数据结构
 *   struct resolver_query —— 一次解析请求。调用方内嵌（或持有）一个该
 *     结构，填好回调后交给 resolver_resolve；解析完成（或失败）时 FRR
 *     会调用 callback，把错误串 / 地址个数 / 地址数组传回。
 *     其中 literal_addr / literal_cb 用于“传入的本来就是 IP 字面量”
 *     这一快捷路径：此时无需真正走 DNS，可立即回调。
 *
 * 与其他模块的关系
 *   依赖 frrevent.h（事件循环 / event）、sockunion.h（地址表示）。被
 *   需要名称解析的守护进程（bgpd、zebra 等）使用。resolver_init 需在
 *   事件循环建立后、使用解析前调用。
 *
 * 阅读顺序建议
 *   先看 struct resolver_query 的回调签名，再看 resolver_resolve 的参数，
 *   理解“查询 -> 回调”的异步模型；随后读 resolver.c 了解 c-ares 如何
 *   与 FRR 主事件循环对接。
 * ============================================================
 */

#ifndef _FRR_RESOLVER_H
#define _FRR_RESOLVER_H

#include "frrevent.h"
#include "sockunion.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一次 DNS 解析请求。
 *   callback     —— 解析完成时调用的回调。参数：本 query、错误字符串
 *                   （成功为 NULL）、地址个数 n、地址数组（union sockunion
 *                   *，长度 n）。注意回调只被调用一次。
 *   literal_addr —— 当传入的 hostname 本身就是 IP 字面量时，直接放这里，
 *                   避免走真实 DNS。
 *   literal_cb   —— 用于把“立即回调”也放入事件循环（避免在调用栈内
 *                   直接回调导致的时序 / 重入问题），指向那个延迟事件。
 * 使用约定：调用方通常把本结构内嵌在自己的会话结构里，生命周期需覆盖
 *   到回调返回之后。
 */
struct resolver_query {
	void (*callback)(struct resolver_query *, const char *errstr, int n,
			 union sockunion *);

	/* used to immediate provide the result if IP literal is passed in */
	union sockunion literal_addr;
	struct event *literal_cb;
};

/*
 * 初始化 / 关闭解析子系统：resolver_init 需在事件循环 tm 建立后调用，
 * 注册 c-ares 的 socket 与超时事件；resolver_terminate 释放相关资源。
 */
void resolver_init(struct event_loop *tm);
void resolver_terminate(void);
/*
 * 发起一次异步解析请求。
 *   query    —— 调用方提供的查询对象（回调由本次传入的 cb 设置）。
 *   af       —— 期望地址族（AF_INET / AF_INET6）。
 *   vrf_id   —— 网络命名空间 / VRF，决定在哪个 VRF 内解析（影响可用
 *               的 DNS 出口路由）。
 *   hostname —— 待解析主机名；若本身是 IP 字面量则走快捷路径。
 *   cb       —— 完成回调，签名同 struct resolver_query.callback。
 * 返回 void：结果通过回调异步返回。
 */
void resolver_resolve(struct resolver_query *query, int af, vrf_id_t vrf_id,
		      const char *hostname,
		      void (*cb)(struct resolver_query *, const char *, int,
				 union sockunion *));

#ifdef __cplusplus
}
#endif

#endif /* _FRR_RESOLVER_H */

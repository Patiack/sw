// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (c) 2021-2022, LabN Consulting, L.L.C
// Copyright (C) 2019  NetDEF, Inc.
//                     Renato Westphal
//

/* 【文件导读】northbound_grpc.cpp —— FRR northbound 层的 gRPC 可选后端
 *
 * 职责：
 *   实现 FRR northbound（北向）接口的 gRPC 服务端。它把 FRR 的配置 / 运维数据通过
 *   gRPC（protobuf 定义的 frr.Northbound 服务）暴露给外部客户端，并提供对候选配置
 *   （candidate）的创建 / 编辑 / 提交（两阶段提交）等操作。属于构建时可选特性，
 *   仅在编译配置启用了 gRPC 时才会被编入。
 *
 * 核心数据结构 / 类型：
 *   - enum CallState / call_states[]：RPC 状态机的取值与名字。
 *   - struct candidate / class Candidates：候选配置仓库（id 到 candidate 的映射）。
 *   - class RpcStateBase：一次 RPC 的公共基类，负责线程切换与状态同步。
 *   - class UnaryRpcState<Q,S>：一元（Unary）RPC 的状态封装（模板）。
 *   - class StreamRpcState<Q,S,X>：流式（Streaming）RPC 的状态封装（模板）。
 *
 * 与其他模块的关系：
 *   - frr-northbound.grpc.pb.h：protobuf / gRPC 生成的服务与消息类型。
 *   - northbound.h / northbound_db.h：FRR 北向核心（配置、事务、提交）。
 *   - libyang：配置数据以 lyd_node 树表示，与 DataTree（protobuf）互转。
 *   - frr_pthread / frrevent：gRPC 在自己的 pthread 上跑，回调则切回主线程。
 *
 * 阅读顺序：
 *   1) 先看 CallState / candidate / Candidates，理解状态与候选配置仓库。
 *   2) 看 RpcStateBase 的 run() 与 c_callback()，理解 grpc 线程与主线程如何协作。
 *   3) 看若干 HandleUnary 与 HandleStreaming 回调，理解每个 RPC 的语义。
 *   4) 最后看 grpc_pthread_start 与 module 初始化 / 收尾函数。
 *
 * 易踩的坑：
 *   - RPC 回调（Handle 函数）运行在主线程；gRPC 收发在 grpc-io-thread，两者通过
 *     event_add_event 与互斥锁 / 条件变量同步，改动时要保持这套同步不变。
 *   - gRPC 库内部会创建线程，故初始化被推迟到 fork 之后（见 very_late_init）。
 *   - 关闭时先在主线程 Shutdown 服务器，让 CQ 的 Next() 返回空以便回收线程退出。
 */
#include <zebra.h>
#include <grpcpp/grpcpp.h>
#include "grpc/frr-northbound.grpc.pb.h"

#include "log.h"
#include "libfrr.h"
#include "lib/version.h"
#include "frrevent.h"
#include "command.h"
#include "lib_errors.h"
#include "northbound.h"
#include "northbound_db.h"
#include "frr_pthread.h"

/* C++ 标准库：iostream/sstream 用于调试输出与地址拼接，memory 用智能指针管理
 * 服务器 / 完成队列，string 处理路径与字符串。 */
#include <iostream>
#include <sstream>
#include <memory>
#include <string>

/* gRPC 服务端默认监听端口，可在模块加载参数里覆盖（见 very_late_init）。 */
#define GRPC_DEFAULT_PORT 50051


/* 本文件私有的全局变量。 */
// ------------------------------------------------------
//                 File Local Variables
// ------------------------------------------------------

/* nb_dbg_client_grpc：gRPC 调试开关。因为 FRR 的调试设施基于 atomics，而 C++ 的
 * atomics API 与 C 不同，这里无法直接复用，故用一个普通 bool 做开关。 */
/*
 * NOTE: we can't use the FRR debugging infrastructure here since it uses
 * atomics and C++ has a different atomics API. Enable gRPC debugging
 * unconditionally until we figure out a way to solve this problem.
 */
static bool nb_dbg_client_grpc = 0;

/* main_master：主线程的事件循环（event loop）。RPC 回调通过它投递到主线程执行。 */
static struct event_loop *main_master;

/* fpt：承载 gRPC 服务器运行的那个 FRR 线程对象（在自己的线程里处理 gRPC 收发）。 */
static struct frr_pthread *fpt;

/* grpc_running：gRPC 服务器是否正在运行的标志。 */
static bool grpc_running;

/* grpc_debug(...)：仅当 nb_dbg_client_grpc 为真时，用 zlog_debug 打印调试信息。 */
#define grpc_debug(...)                                                        \
	do {                                                                   \
		if (nb_dbg_client_grpc)                                        \
			zlog_debug(__VA_ARGS__);                               \
	} while (0)

/* 本文件定义的新类型：RPC 状态机枚举、候选配置仓库及其记录。 */
// ------------------------------------------------------
//                      New Types
// ------------------------------------------------------

/* enum CallState：一次 RPC 的状态机取值。
 *   CREATE 刚创建；PROCESS 正在（主线程）处理；MORE 流式 RPC 还有后续数据；
 *   FINISH 结束；DELETED 已删除。 */
enum CallState { CREATE, PROCESS, MORE, FINISH, DELETED };
/* call_states：与 CallState 一一对应的名字数组，仅用于调试日志打印。 */
const char *call_states[] = {"CREATE", "PROCESS", "MORE", "FINISH", "DELETED"};

/* struct candidate：一条候选配置记录。id 是候选编号；config 是候选配置快照；
 * transaction 指向进行中的事务（无则为 NULL）。 */
struct candidate {
	uint64_t id;
	struct nb_config *config;
	struct nb_transaction *transaction;
};

/* class Candidates：候选配置仓库，用 std::map 维护 id 到 candidate 的映射。
 *   ~Candidates()      —— 析构时删除所有候选（逐个 delete_candidate）；
 *   create_candidate() —— 生成新 id，以运行配置 running_config 的快照初始化，返回记录；
 *   contains(id)       —— 判断给定候选 id 是否存在；
 *   delete_candidate() —— 释放该候选的配置，若在事务中则中止事务，再从表中移除；
 *   get_candidate(id)  —— 按 id 取记录，不存在返回 NULL；
 *   成员 _next_id（自增 id 计数器）、_cdb（id 到 candidate 的映射）。 */
class Candidates
{
      public:
	~Candidates(void)
	{
		// Delete candidates.
		for (auto it = _cdb.begin(); it != _cdb.end(); it++)
			delete_candidate(it->first);
	}

	struct candidate *create_candidate(void)
	{
		uint64_t id = ++_next_id;
		assert(id); // TODO: implement an algorithm for unique reusable
			    // IDs.
		struct candidate *c = &_cdb[id];
		c->id = id;
		c->config = nb_config_dup(running_config);
		c->transaction = NULL;

		return c;
	}

	bool contains(uint64_t candidate_id)
	{
		return _cdb.count(candidate_id) > 0;
	}

	void delete_candidate(uint64_t candidate_id)
	{
		struct candidate *c = &_cdb[candidate_id];
		char errmsg[BUFSIZ] = {0};

		nb_config_free(c->config);
		if (c->transaction)
			nb_candidate_commit_abort(c->transaction, errmsg,
						  sizeof(errmsg));
		_cdb.erase(c->id);
	}

	struct candidate *get_candidate(uint64_t id)
	{
		return _cdb.count(id) == 0 ? NULL : &_cdb[id];
	}

      private:
	uint64_t _next_id = 0;
	std::map<uint64_t, struct candidate> _cdb;
};

/* class RpcStateBase：所有 RPC 状态类的公共基类，负责一次 RPC 的生命周期与线程同步。
 *   do_request()         —— 纯虚，向 gRPC 服务投递下一个该类型的请求（可能新建副本）；
 *   RpcStateBase(name)   —— 构造，记录 RPC 名字；
 *   ~RpcStateBase()      —— 虚析构；
 *   get_state()          —— 取当前 CallState；
 *   is_initial_process() —— 是否处于首次处理（entered_state 为 CREATE；Unary 恒为真）；
 *   run()                —— 在 grpc 线程调用：把状态置为 PROCESS，用 event_add_event 把
 *                           回调调度到主线程，然后阻塞等待主线程把状态改成 MORE 或 FINISH；
 *                           若为 FINISH 则重新 do_request 以接收下一个同类请求；
 *   run_mainthread()     —— 纯虚，主线程里真正执行业务逻辑并返回新状态；
 *   c_callback()         —— 主线程事件回调：持锁调用 run_mainthread、更新状态、唤醒等待者；
 *   成员 ctx（gRPC 上下文）、cmux/cond（互斥锁与条件变量）、state/entered_state、name。 */
/*
 * RpcStateBase is the common base class used to track a gRPC RPC.
 */
class RpcStateBase
{
      public:
	virtual void do_request(::frr::Northbound::AsyncService *service,
				::grpc::ServerCompletionQueue *cq,
				bool no_copy) = 0;

	RpcStateBase(const char *name) : name(name){};

	virtual ~RpcStateBase() = default;

	CallState get_state() const
	{
		return state;
	}

	bool is_initial_process() const
	{
		/* Will always be true for Unary */
		return entered_state == CREATE;
	}

	// Returns "more" status, if false caller can delete
	bool run(frr::Northbound::AsyncService *service,
		 grpc::ServerCompletionQueue *cq)
	{
		/*
		 * We enter in either CREATE or MORE state, and transition to
		 * PROCESS state.
		 */
		this->entered_state = this->state;
		this->state = PROCESS;
		grpc_debug("%s RPC: %s -> %s on grpc-io-thread", name,
			   call_states[this->entered_state],
			   call_states[this->state]);
		/*
		 * We schedule the callback on the main pthread, and wait for
		 * the state to transition out of the PROCESS state. The new
		 * state will either be MORE or FINISH. It will always be FINISH
		 * for Unary RPCs.
		 */
		event_add_event(main_master, c_callback, (void *)this, 0, NULL);

		pthread_mutex_lock(&this->cmux);
		while (this->state == PROCESS)
			pthread_cond_wait(&this->cond, &this->cmux);
		pthread_mutex_unlock(&this->cmux);

		grpc_debug("%s RPC in %s on grpc-io-thread", name,
			   call_states[this->state]);

		if (this->state == FINISH) {
			/*
			 * Server is done (FINISH) so prep to receive a new
			 * request of this type. We could do this earlier but
			 * that would mean we could be handling multiple same
			 * type requests in parallel without limit.
			 */
			this->do_request(service, cq, false);
		}
		return true;
	}

      protected:
	virtual CallState run_mainthread(struct event *event) = 0;

	static void c_callback(struct event *event)
	{
		auto _tag = static_cast<RpcStateBase *>(EVENT_ARG(event));
		/*
		 * We hold the lock until the callback finishes and has updated
		 * _tag->state, then we signal done and release.
		 */
		pthread_mutex_lock(&_tag->cmux);

		CallState enter_state = _tag->state;
		grpc_debug("%s RPC: running %s on main event", _tag->name,
			   call_states[enter_state]);

		_tag->state = _tag->run_mainthread(event);

		grpc_debug("%s RPC: %s -> %s [main event]", _tag->name,
			   call_states[enter_state], call_states[_tag->state]);

		pthread_cond_signal(&_tag->cond);
		pthread_mutex_unlock(&_tag->cmux);
		return;
	}

	grpc::ServerContext ctx;
	pthread_mutex_t cmux = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	CallState state = CREATE;
	CallState entered_state = CREATE;

      public:
	const char *name;
};

/* class UnaryRpcState<Q,S>：一元 RPC 的状态封装（Q 请求类型，S 响应类型）。
 *   reqfunc_t        —— 指向 AsyncService 中该 RPC 请求函数的成员指针类型；
 *   UnaryRpcState()  —— 构造，保存候选仓库、请求函数、业务回调与名字；
 *   do_request()     —— 按需新建副本并调用请求函数投递请求；
 *   run_mainthread() —— 调用业务回调后直接 Finish 响应并返回 FINISH（一元 RPC 必结束）；
 *   成员 cdb、request、response、responder、callback、requestf。 */
/*
 * The UnaryRpcState class is used to track the execution of a Unary RPC.
 *
 * Template Args:
 *     Q - the request type for a given unary RPC
 *     S - the response type for a given unary RPC
 */
template <typename Q, typename S> class UnaryRpcState : public RpcStateBase
{
      public:
	typedef void (frr::Northbound::AsyncService::*reqfunc_t)(
		::grpc::ServerContext *, Q *,
		::grpc::ServerAsyncResponseWriter<S> *,
		::grpc::CompletionQueue *, ::grpc::ServerCompletionQueue *,
		void *);

	UnaryRpcState(Candidates *cdb, reqfunc_t rfunc,
		      grpc::Status (*cb)(UnaryRpcState<Q, S> *),
		      const char *name)
	    : RpcStateBase(name), cdb(cdb), requestf(rfunc), callback(cb),
	      responder(&ctx){};

	void do_request(::frr::Northbound::AsyncService *service,
			::grpc::ServerCompletionQueue *cq,
			bool no_copy) override
	{
		grpc_debug("%s, posting a request for: %s", __func__, name);
		auto copy = no_copy ? this
				    : new UnaryRpcState(cdb, requestf, callback,
							name);
		(service->*requestf)(&copy->ctx, &copy->request,
				     &copy->responder, cq, cq, copy);
	}

	CallState run_mainthread(struct event *event) override
	{
		// Unary RPC are always finished, see "Unary" :)
		grpc::Status status = this->callback(this);
		responder.Finish(response, status, this);
		return FINISH;
	}

	Candidates *cdb;

	Q request;
	S response;
	grpc::ServerAsyncResponseWriter<S> responder;

	grpc::Status (*callback)(UnaryRpcState<Q, S> *);
	reqfunc_t requestf = NULL;
};

/* class StreamRpcState<Q,S,X>：流式 RPC 的状态封装（X 为跟踪流式进度的上下文类型）。
 *   reqsfunc_t       —— 指向 AsyncService 中该流式 RPC 请求函数的成员指针类型；
 *   StreamRpcState() —— 构造，保存请求函数、业务回调与名字；
 *   do_request()     —— 按需新建副本并投递请求；
 *   run_mainthread() —— 调用回调，返回 true 则状态转 MORE（继续流），false 转 FINISH；
 *   成员 request、response、async_responder、callback、requestsf、context。 */
/*
 * The StreamRpcState class is used to track the execution of a Streaming RPC.
 *
 * Template Args:
 *     Q - the request type for a given streaming RPC
 *     S - the response type for a given streaming RPC
 *     X - the type used to track the streaming state
 */
template <typename Q, typename S, typename X>
class StreamRpcState : public RpcStateBase
{
      public:
	typedef void (frr::Northbound::AsyncService::*reqsfunc_t)(
		::grpc::ServerContext *, Q *, ::grpc::ServerAsyncWriter<S> *,
		::grpc::CompletionQueue *, ::grpc::ServerCompletionQueue *,
		void *);

	StreamRpcState(reqsfunc_t rfunc, bool (*cb)(StreamRpcState<Q, S, X> *),
		       const char *name)
	    : RpcStateBase(name), requestsf(rfunc), callback(cb),
	      async_responder(&ctx){};

	void do_request(::frr::Northbound::AsyncService *service,
			::grpc::ServerCompletionQueue *cq,
			bool no_copy) override
	{
		grpc_debug("%s, posting a request for: %s", __func__, name);
		auto copy =
			no_copy ? this
				: new StreamRpcState(requestsf, callback, name);
		(service->*requestsf)(&copy->ctx, &copy->request,
				      &copy->async_responder, cq, cq, copy);
	}

	CallState run_mainthread(struct event *event) override
	{
		if (this->callback(this))
			return MORE;
		else
			return FINISH;
	}

	Q request;
	S response;
	grpc::ServerAsyncWriter<S> async_responder;

	bool (*callback)(StreamRpcState<Q, S, X> *);
	reqsfunc_t requestsf = NULL;

	X context;
};

/* 工具函数：编码转换、libyang 数据树编辑 / 删除、dnode 与 DataTree 互转、配置 / 运维数据获取。 */
// ------------------------------------------------------
//                    Utility Functions
// ------------------------------------------------------

/* encoding2lyd_format：把 gRPC 侧的编码枚举换算成 libyang 的 LYD_FORMAT。
 * 目前支持 JSON / XML，遇到未知编码则记录错误并退出进程。 */
static LYD_FORMAT encoding2lyd_format(enum frr::Encoding encoding)
{
	switch (encoding) {
	case frr::JSON:
		return LYD_JSON;
	case frr::XML:
		return LYD_XML;
	default:
		flog_err(EC_LIB_DEVELOPMENT,
			 "%s: unknown data encoding format (%u)", __func__,
			 encoding);
		exit(1);
	}
}

/* yang_dnode_edit：在给定数据树上按 XPath 新建 / 更新节点（LYD_NEW_PATH_UPDATE）。
 * 成功返回 0，失败返回 -1 并记录警告。 */
static int yang_dnode_edit(struct lyd_node *dnode, const std::string &path,
			   const char *value)
{
	LY_ERR err = lyd_new_path(dnode, ly_native_ctx, path.c_str(), value,
				  LYD_NEW_PATH_UPDATE, &dnode);
	if (err != LY_SUCCESS) {
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_new_path() failed: %s",
			  __func__, ly_errmsg(ly_native_ctx));
		return -1;
	}

	return 0;
}

/* yang_dnode_delete：按 XPath 找到节点并释放该子树。找到并删除返回 0，否则返回 -1。 */
static int yang_dnode_delete(struct lyd_node *dnode, const std::string &path)
{
	dnode = yang_dnode_get(dnode, path.c_str());
	if (!dnode)
		return -1;

	lyd_free_tree(dnode);

	return 0;
}

/* data_tree_from_dnode：把 libyang 的 lyd_node 树序列化为字符串写入 DataTree（protobuf）。
 * 按 lyd_format 选择格式，with_defaults 决定是否输出默认节点。 */
static LY_ERR data_tree_from_dnode(frr::DataTree *dt,
				   const struct lyd_node *dnode,
				   LYD_FORMAT lyd_format, bool with_defaults)
{
	char *strp;
	int options = 0;

	SET_FLAG(options, LYD_PRINT_WITHSIBLINGS);
	if (with_defaults)
		SET_FLAG(options, LYD_PRINT_WD_ALL);
	else
		SET_FLAG(options, LYD_PRINT_WD_TRIM);

	LY_ERR err = lyd_print_mem(&strp, dnode, lyd_format, options);
	if (err == LY_SUCCESS) {
		if (strp) {
			dt->set_data(strp);
			free(strp);
		}
	}
	return err;
}

/* dnode_from_data_tree：把 DataTree 中的字符串解析成 libyang 的 lyd_node 树。
 * config_only 为真时按"配置数据"解析（排除状态节点）且不做额外校验，否则严格解析。 */
static struct lyd_node *dnode_from_data_tree(const frr::DataTree *dt,
					     bool config_only)
{
	struct lyd_node *dnode;
	int options, opt2;
	LY_ERR err;

	if (config_only) {
		options = LYD_PARSE_NO_STATE;
		opt2 = LYD_VALIDATE_NO_STATE;
	} else {
		options = LYD_PARSE_STRICT;
		opt2 = 0;
	}

	err = lyd_parse_data_mem(ly_native_ctx, dt->data().c_str(),
				 encoding2lyd_format(dt->encoding()), options,
				 opt2, &dnode);
	if (err != LY_SUCCESS) {
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_parse_mem() failed: %s",
			  __func__, ly_errmsg(ly_native_ctx));
	}
	return dnode;
}

/* get_dnode_config：从运行配置 running_config 中按 XPath 复制出配置子树（path 为空表示整树）。
 * 不存在则返回 NULL。 */
static struct lyd_node *get_dnode_config(const std::string &path)
{
	struct lyd_node *dnode;

	if (!yang_dnode_exists(running_config->dnode,
			       path.empty() ? NULL : path.c_str()))
		return NULL;

	dnode = yang_dnode_get(running_config->dnode,
			       path.empty() ? NULL : path.c_str());
	if (dnode)
		dnode = yang_dnode_dup(dnode);

	return dnode;
}

/* get_dnode_state：通过北向 legacy 迭代接口按 XPath 取得运维（状态）数据子树。 */
static struct lyd_node *get_dnode_state(const std::string &path)
{
	struct lyd_node *dnode = NULL;

	(void)nb_oper_iterate_legacy(path.c_str(), NULL, 0, NULL, NULL, &dnode);

	return dnode;
}

/* get_path：Get RPC 的核心实现。按 type（ALL / CONFIG / STATE）分别取配置与/或运维数据，
 * 需要时把两者合并成一棵树，做校验补齐隐式默认，再按请求格式序列化进 DataTree。 */
static grpc::Status get_path(frr::DataTree *dt, const std::string &path,
			     int type, LYD_FORMAT lyd_format,
			     bool with_defaults)
{
	struct lyd_node *dnode_config = NULL;
	struct lyd_node *dnode_state = NULL;
	struct lyd_node *dnode_final;

	// Configuration data.
	if (type == frr::GetRequest_DataType_ALL
	    || type == frr::GetRequest_DataType_CONFIG) {
		dnode_config = get_dnode_config(path);
		if (!dnode_config)
			return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
					    "Data path not found");
	}

	// Operational data.
	if (type == frr::GetRequest_DataType_ALL
	    || type == frr::GetRequest_DataType_STATE) {
		dnode_state = get_dnode_state(path);
		if (!dnode_state) {
			if (dnode_config)
				yang_dnode_free(dnode_config);
			return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
					    "Failed to fetch operational data");
		}
	}

	switch (type) {
	case frr::GetRequest_DataType_ALL:
		//
		// Combine configuration and state data into a single
		// dnode.
		//
		if (lyd_merge_siblings(&dnode_state, dnode_config,
				       LYD_MERGE_DESTRUCT)
		    != LY_SUCCESS) {
			yang_dnode_free(dnode_state);
			yang_dnode_free(dnode_config);
			return grpc::Status(
				grpc::StatusCode::INTERNAL,
				"Failed to merge configuration and state data",
				ly_errmsg(ly_native_ctx));
		}

		dnode_final = dnode_state;
		break;
	case frr::GetRequest_DataType_CONFIG:
		dnode_final = dnode_config;
		break;
	case frr::GetRequest_DataType_STATE:
		dnode_final = dnode_state;
		break;
	}

	// Validate data to create implicit default nodes if necessary.
	int validate_opts = 0;
	if (type == frr::GetRequest_DataType_CONFIG)
		validate_opts = LYD_VALIDATE_NO_STATE;
	else
		validate_opts = 0;

	LY_ERR err = lyd_validate_all(&dnode_final, ly_native_ctx,
				      validate_opts, NULL);

	if (err)
		flog_warn(EC_LIB_LIBYANG, "%s: lyd_validate_all() failed: %s",
			  __func__, ly_errmsg(ly_native_ctx));
	// Dump data using the requested format.
	if (!err)
		err = data_tree_from_dnode(dt, dnode_final, lyd_format,
					   with_defaults);
	yang_dnode_free(dnode_final);
	if (err)
		return grpc::Status(grpc::StatusCode::INTERNAL,
				    "Failed to dump data");
	return grpc::Status::OK;
}


/* 以下是各 RPC 的业务回调。它们都运行在主线程（由 RpcStateBase::c_callback 调度），
 * 每个函数对应 frr.Northbound 服务中的一个 RPC。命名规律：
 *   HandleUnary<名字>     —— 一元 RPC；HandleStreaming<名字> —— 流式 RPC。 */
// ------------------------------------------------------
//       RPC Callback Functions: run on main thread
// ------------------------------------------------------

/* HandleUnaryGetCapabilities：返回能力信息——FRR 版本、是否支持回滚（编译期决定）、
 * 所有已加载 YANG 模块（名 / 修订 / 组织）以及支持的编码（JSON 与 XML）。 */
grpc::Status HandleUnaryGetCapabilities(
	UnaryRpcState<frr::GetCapabilitiesRequest, frr::GetCapabilitiesResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	// Response: string frr_version = 1;
	tag->response.set_frr_version(FRR_VERSION);

	// Response: bool rollback_support = 2;
#ifdef HAVE_CONFIG_ROLLBACKS
	tag->response.set_rollback_support(true);
#else
	tag->response.set_rollback_support(false);
#endif
	// Response: repeated ModuleData supported_modules = 3;
	struct yang_module *module;
	RB_FOREACH (module, yang_modules, &yang_modules) {
		auto m = tag->response.add_supported_modules();

		m->set_name(module->name);
		if (module->info->revision)
			m->set_revision(module->info->revision);
		m->set_organization(module->info->org);
	}

	// Response: repeated Encoding supported_encodings = 4;
	tag->response.add_supported_encodings(frr::JSON);
	tag->response.add_supported_encodings(frr::XML);

	return grpc::Status::OK;
}

// Define the context variable type for this streaming handler
/* GetContextType：Get 流式 RPC 的上下文类型，用字符串列表保存待处理的路径队列。 */
typedef std::list<std::string> GetContextType;

/* HandleStreamingGet：Get 流式 RPC。首次进入时把请求里的路径全部压入上下文队列；之后每次
 * 处理弹出一个路径，取数据并 Write 回响应，队列空时 Finish。返回 true 表示还需继续处理。 */
bool HandleStreamingGet(
	StreamRpcState<frr::GetRequest, frr::GetResponse, GetContextType> *tag)
{
	grpc_debug("%s: entered", __func__);

	auto mypathps = &tag->context;
	if (tag->is_initial_process()) {
		// Fill our context container first time through
		grpc_debug("%s: initialize streaming state", __func__);
		auto paths = tag->request.path();
		for (const std::string &path : paths) {
			mypathps->push_back(std::string(path));
		}
	}

	// Request: DataType type = 1;
	int type = tag->request.type();
	// Request: Encoding encoding = 2;
	frr::Encoding encoding = tag->request.encoding();
	// Request: bool with_defaults = 3;
	bool with_defaults = tag->request.with_defaults();

	if (mypathps->empty()) {
		tag->async_responder.Finish(grpc::Status::OK, tag);
		return false;
	}

	frr::GetResponse response;
	grpc::Status status;

	// Response: int64 timestamp = 1;
	response.set_timestamp(time(NULL));

	// Response: DataTree data = 2;
	auto *data = response.mutable_data();
	data->set_encoding(tag->request.encoding());
	status = get_path(data, mypathps->back().c_str(), type,
			  encoding2lyd_format(encoding), with_defaults);

	if (!status.ok()) {
		tag->async_responder.WriteAndFinish(
			response, grpc::WriteOptions(), status, tag);
		return false;
	}

	mypathps->pop_back();
	if (mypathps->empty()) {
		tag->async_responder.WriteAndFinish(
			response, grpc::WriteOptions(), grpc::Status::OK, tag);
		return false;
	} else {
		tag->async_responder.Write(response, tag);
		return true;
	}
}

/* HandleUnaryCreateCandidate：创建一个新的候选配置，把其 id 放入响应返回给客户端。 */
grpc::Status HandleUnaryCreateCandidate(
	UnaryRpcState<frr::CreateCandidateRequest, frr::CreateCandidateResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	struct candidate *candidate = tag->cdb->create_candidate();
	if (!candidate)
		return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
				    "Can't create candidate configuration");
	tag->response.set_candidate_id(candidate->id);
	return grpc::Status::OK;
}

/* HandleUnaryDeleteCandidate：删除指定 id 的候选配置（不存在则返回 NOT_FOUND）。 */
grpc::Status HandleUnaryDeleteCandidate(
	UnaryRpcState<frr::DeleteCandidateRequest, frr::DeleteCandidateResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	uint32_t candidate_id = tag->request.candidate_id();

	grpc_debug("%s(candidate_id: %u)", __func__, candidate_id);

	if (!tag->cdb->contains(candidate_id))
		return grpc::Status(grpc::StatusCode::NOT_FOUND,
				    "candidate configuration not found");
	tag->cdb->delete_candidate(candidate_id);
	return grpc::Status::OK;
}

/* HandleUnaryUpdateCandidate：用当前运行配置刷新指定候选配置（须不在事务中）。 */
grpc::Status HandleUnaryUpdateCandidate(
	UnaryRpcState<frr::UpdateCandidateRequest, frr::UpdateCandidateResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	uint32_t candidate_id = tag->request.candidate_id();

	grpc_debug("%s(candidate_id: %u)", __func__, candidate_id);

	struct candidate *candidate = tag->cdb->get_candidate(candidate_id);

	if (!candidate)
		return grpc::Status(grpc::StatusCode::NOT_FOUND,
				    "candidate configuration not found");
	if (candidate->transaction)
		return grpc::Status(
			grpc::StatusCode::FAILED_PRECONDITION,
			"candidate is in the middle of a transaction");
	if (nb_candidate_update(candidate->config) != NB_OK)
		return grpc::Status(grpc::StatusCode::INTERNAL,
				    "failed to update candidate configuration");

	return grpc::Status::OK;
}

/* HandleUnaryEditCandidate：在候选配置副本上依次应用 update（新建 / 改值）与 delete
 * （删除）操作，全部成功后才用 nb_config_replace 原子替换候选配置；任一步失败即整体放弃。 */
grpc::Status HandleUnaryEditCandidate(
	UnaryRpcState<frr::EditCandidateRequest, frr::EditCandidateResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	uint32_t candidate_id = tag->request.candidate_id();

	grpc_debug("%s(candidate_id: %u)", __func__, candidate_id);

	struct candidate *candidate = tag->cdb->get_candidate(candidate_id);
	if (!candidate)
		return grpc::Status(grpc::StatusCode::NOT_FOUND,
				    "candidate configuration not found");

	struct nb_config *candidate_tmp = nb_config_dup(candidate->config);

	auto pvs = tag->request.update();
	for (const frr::PathValue &pv : pvs) {
		if (yang_dnode_edit(candidate_tmp->dnode, pv.path(),
				    pv.value().c_str()) != 0) {
			nb_config_free(candidate_tmp);

			return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
					    "Failed to update \"" + pv.path() +
						    "\"");
		}
	}

	pvs = tag->request.delete_();
	for (const frr::PathValue &pv : pvs) {
		if (yang_dnode_delete(candidate_tmp->dnode, pv.path()) != 0) {
			nb_config_free(candidate_tmp);
			return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
					    "Failed to remove \"" + pv.path() +
						    "\"");
		}
	}

	// No errors, accept all changes.
	nb_config_replace(candidate->config, candidate_tmp, false);
	return grpc::Status::OK;
}

/* HandleUnaryLoadToCandidate：把请求里携带的整棵配置树载入候选配置。type 为 REPLACE 时整体
 * 替换，否则与现有候选配置合并。 */
grpc::Status HandleUnaryLoadToCandidate(
	UnaryRpcState<frr::LoadToCandidateRequest, frr::LoadToCandidateResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	uint32_t candidate_id = tag->request.candidate_id();

	grpc_debug("%s(candidate_id: %u)", __func__, candidate_id);

	// Request: LoadType type = 2;
	int load_type = tag->request.type();
	// Request: DataTree config = 3;
	auto config = tag->request.config();

	struct candidate *candidate = tag->cdb->get_candidate(candidate_id);
	if (!candidate)
		return grpc::Status(grpc::StatusCode::NOT_FOUND,
				    "candidate configuration not found");

	struct lyd_node *dnode = dnode_from_data_tree(&config, true);
	if (!dnode)
		return grpc::Status(grpc::StatusCode::INTERNAL,
				    "Failed to parse the configuration");

	struct nb_config *loaded_config = nb_config_new(dnode);
	if (load_type == frr::LoadToCandidateRequest::REPLACE)
		nb_config_replace(candidate->config, loaded_config, false);
	else if (nb_config_merge(candidate->config, loaded_config, false) !=
		 NB_OK)
		return grpc::Status(grpc::StatusCode::INTERNAL,
				    "Failed to merge the loaded configuration");

	return grpc::Status::OK;
}

/* HandleUnaryCommit：提交（两阶段提交）相关操作。按 phase 分别执行 VALIDATE / PREPARE /
 * ABORT / APPLY / ALL；并在提交前检查两阶段提交协议的误用（如重复 PREPARE 或对不存在的事务
 * APPLY）。最后把 northbound 错误码映射成 gRPC 状态码，并回填事务 id 与错误信息。 */
grpc::Status
HandleUnaryCommit(UnaryRpcState<frr::CommitRequest, frr::CommitResponse> *tag)
{
	grpc_debug("%s: entered", __func__);

	// Request: uint32 candidate_id = 1;
	uint32_t candidate_id = tag->request.candidate_id();

	grpc_debug("%s(candidate_id: %u)", __func__, candidate_id);

	// Request: Phase phase = 2;
	int phase = tag->request.phase();
	// Request: string comment = 3;
	const std::string comment = tag->request.comment();

	// Find candidate configuration.
	struct candidate *candidate = tag->cdb->get_candidate(candidate_id);
	if (!candidate)
		return grpc::Status(grpc::StatusCode::NOT_FOUND,
				    "candidate configuration not found");

	int ret = NB_OK;
	uint32_t transaction_id = 0;

	// Check for misuse of the two-phase commit protocol.
	switch (phase) {
	case frr::CommitRequest::PREPARE:
	case frr::CommitRequest::ALL:
		if (candidate->transaction)
			return grpc::Status(
				grpc::StatusCode::FAILED_PRECONDITION,
				"candidate is in the middle of a transaction");
		break;
	case frr::CommitRequest::ABORT:
	case frr::CommitRequest::APPLY:
		if (!candidate->transaction)
			return grpc::Status(
				grpc::StatusCode::FAILED_PRECONDITION,
				"no transaction in progress");
		break;
	default:
		break;
	}


	// Execute the user request.
	struct nb_context context = {};
	context.client = NB_CLIENT_GRPC;
	char errmsg[BUFSIZ] = {0};

	switch (phase) {
	case frr::CommitRequest::VALIDATE:
		grpc_debug("`-> Performing VALIDATE");
		ret = nb_candidate_validate(&context, candidate->config, errmsg,
					    sizeof(errmsg));
		break;
	case frr::CommitRequest::PREPARE:
		grpc_debug("`-> Performing PREPARE");
		ret = nb_candidate_commit_prepare(
			context, candidate->config, comment.c_str(),
			&candidate->transaction, false, false, errmsg,
			sizeof(errmsg));
		break;
	case frr::CommitRequest::ABORT:
		grpc_debug("`-> Performing ABORT");
		nb_candidate_commit_abort(candidate->transaction, errmsg,
					  sizeof(errmsg));
		break;
	case frr::CommitRequest::APPLY:
		grpc_debug("`-> Performing APPLY");
		nb_candidate_commit_apply(candidate->transaction, true,
					  &transaction_id, errmsg,
					  sizeof(errmsg));
		break;
	case frr::CommitRequest::ALL:
		grpc_debug("`-> Performing ALL");
		ret = nb_candidate_commit(context, candidate->config, true,
					  comment.c_str(), &transaction_id,
					  errmsg, sizeof(errmsg));
		break;
	}

	// Map northbound error codes to gRPC status codes.
	grpc::Status status;
	switch (ret) {
	case NB_OK:
		status = grpc::Status::OK;
		break;
	case NB_ERR_NO_CHANGES:
		status = grpc::Status(grpc::StatusCode::ABORTED, errmsg);
		break;
	case NB_ERR_LOCKED:
		status = grpc::Status(grpc::StatusCode::UNAVAILABLE, errmsg);
		break;
	case NB_ERR_VALIDATION:
		status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				      errmsg);
		break;
	case NB_ERR_RESOURCE:
		status = grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
				      errmsg);
		break;
	case NB_ERR:
	default:
		status = grpc::Status(grpc::StatusCode::INTERNAL, errmsg);
		break;
	}

	grpc_debug("`-> Result: %s (message: '%s')",
		   nb_err_name((enum nb_error)ret), errmsg);

	if (ret == NB_OK) {
		// Response: uint32 transaction_id = 1;
		if (transaction_id)
			tag->response.set_transaction_id(transaction_id);
	}
	if (strlen(errmsg) > 0)
		tag->response.set_error_message(errmsg);

	return status;
}

/* HandleUnaryLockConfig：锁定运行配置，防止其他客户端并发修改。 */
grpc::Status HandleUnaryLockConfig(
	UnaryRpcState<frr::LockConfigRequest, frr::LockConfigResponse> *tag)
{
	grpc_debug("%s: entered", __func__);

	if (nb_running_lock(NB_CLIENT_GRPC, NULL))
		return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
				    "running configuration is locked already");
	return grpc::Status::OK;
}

/* HandleUnaryUnlockConfig：解锁运行配置。 */
grpc::Status HandleUnaryUnlockConfig(
	UnaryRpcState<frr::UnlockConfigRequest, frr::UnlockConfigResponse> *tag)
{
	grpc_debug("%s: entered", __func__);

	if (nb_running_unlock(NB_CLIENT_GRPC, NULL))
		return grpc::Status(
			grpc::StatusCode::FAILED_PRECONDITION,
			"failed to unlock the running configuration");
	return grpc::Status::OK;
}

/* list_transactions_cb：遍历事务数据库时的回调，把每个事务的
 * （id, 客户端名, 日期, 注释）打包成 tuple 追加到 arg 指向的列表中。 */
static void list_transactions_cb(void *arg, int transaction_id,
				 const char *client_name, const char *date,
				 const char *comment)
{
	auto list = static_cast<std::list<
		std::tuple<int, std::string, std::string, std::string>> *>(arg);
	list->push_back(
		std::make_tuple(transaction_id, std::string(client_name),
				std::string(date), std::string(comment)));
}

// Define the context variable type for this streaming handler
/* ListTransactionsContextType：ListTransactions 流式 RPC 的上下文类型，
 * 保存事务元组列表。 */
typedef std::list<std::tuple<int, std::string, std::string, std::string>>
	ListTransactionsContextType;

/* HandleStreamingListTransactions：ListTransactions 流式 RPC。首次进入时遍历事务数据库填充
 * 上下文列表（并追加两条假的示例项）；之后每次弹出一条，组装成响应 Write 回去，列表空则 Finish。 */
bool HandleStreamingListTransactions(
	StreamRpcState<frr::ListTransactionsRequest,
		       frr::ListTransactionsResponse,
		       ListTransactionsContextType> *tag)
{
	grpc_debug("%s: entered", __func__);

	auto list = &tag->context;
	if (tag->is_initial_process()) {
		grpc_debug("%s: initialize streaming state", __func__);
		// Fill our context container first time through
		nb_db_transactions_iterate(list_transactions_cb, list);
		list->push_back(std::make_tuple(
			0xFFFF, std::string("fake client"),
			std::string("fake date"), std::string("fake comment")));
		list->push_back(std::make_tuple(0xFFFE,
						std::string("fake client2"),
						std::string("fake date"),
						std::string("fake comment2")));
	}

	if (list->empty()) {
		tag->async_responder.Finish(grpc::Status::OK, tag);
		return false;
	}

	auto item = list->back();

	frr::ListTransactionsResponse response;

	// Response: uint32 id = 1;
	response.set_id(std::get<0>(item));

	// Response: string client = 2;
	response.set_client(std::get<1>(item).c_str());

	// Response: string date = 3;
	response.set_date(std::get<2>(item).c_str());

	// Response: string comment = 4;
	response.set_comment(std::get<3>(item).c_str());

	list->pop_back();
	if (list->empty()) {
		tag->async_responder.WriteAndFinish(
			response, grpc::WriteOptions(), grpc::Status::OK, tag);
		return false;
	} else {
		tag->async_responder.Write(response, tag);
		return true;
	}
}

/* HandleUnaryGetTransaction：按 transaction_id 从事务数据库载入历史配置，按请求编码与是否
 * 含默认值序列化后返回。 */
grpc::Status HandleUnaryGetTransaction(
	UnaryRpcState<frr::GetTransactionRequest, frr::GetTransactionResponse>
		*tag)
{
	grpc_debug("%s: entered", __func__);

	// Request: uint32 transaction_id = 1;
	uint32_t transaction_id = tag->request.transaction_id();
	// Request: Encoding encoding = 2;
	frr::Encoding encoding = tag->request.encoding();
	// Request: bool with_defaults = 3;
	bool with_defaults = tag->request.with_defaults();

	grpc_debug("%s(transaction_id: %u, encoding: %u)", __func__,
		   transaction_id, encoding);

	struct nb_config *nb_config;

	// Load configuration from the transactions database.
	nb_config = nb_db_transaction_load(transaction_id);
	if (!nb_config)
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Transaction not found");

	// Response: DataTree config = 1;
	auto config = tag->response.mutable_config();
	config->set_encoding(encoding);

	// Dump data using the requested format.
	if (data_tree_from_dnode(config, nb_config->dnode,
				 encoding2lyd_format(encoding), with_defaults)
	    != 0) {
		nb_config_free(nb_config);
		return grpc::Status(grpc::StatusCode::INTERNAL,
				    "Failed to dump data");
	}

	nb_config_free(nb_config);

	return grpc::Status::OK;
}

/* HandleUnaryExecute：执行（RPC 类）操作。按 xpath 找到 nb_node，构造输入数据树、填入请求
 * 携带的输入参数、校验后调用注册在 XPath 上的 rpc 回调，最后把输出子节点逐个转成 PathValue 返回。 */
grpc::Status HandleUnaryExecute(
	UnaryRpcState<frr::ExecuteRequest, frr::ExecuteResponse> *tag)
{
	grpc_debug("%s: entered", __func__);

	struct nb_node *nb_node;
	struct lyd_node *input_tree, *output_tree, *child;
	const char *xpath;
	char errmsg[BUFSIZ] = {0};
	char path[XPATH_MAXLEN];
	LY_ERR err;

	// Request: string path = 1;
	xpath = tag->request.path().c_str();

	grpc_debug("%s(path: \"%s\")", __func__, xpath);

	if (tag->request.path().empty())
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Data path is empty");

	nb_node = nb_node_find(xpath);
	if (!nb_node)
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Unknown data path");

	// Create input data tree.
	err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, (LYD_ANYDATA_VALUETYPE)0, 0,
			     NULL, &input_tree);
	if (err != LY_SUCCESS) {
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Invalid data path");
	}

	// Read input parameters.
	auto input = tag->request.input();
	for (const frr::PathValue &pv : input) {
		// Request: repeated PathValue input = 2;
		err = lyd_new_path(input_tree, ly_native_ctx, pv.path().c_str(),
				   pv.value().c_str(), 0, NULL);
		if (err != LY_SUCCESS) {
			lyd_free_tree(input_tree);
			return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
					    "Invalid input data");
		}
	}

	// Validate input data.
	err = lyd_validate_op(input_tree, NULL, LYD_TYPE_RPC_YANG, NULL);
	if (err != LY_SUCCESS) {
		lyd_free_tree(input_tree);
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Invalid input data");
	}

	// Create output data tree.
	err = yang_new_path2(NULL, ly_native_ctx, xpath, NULL, 0, (LYD_ANYDATA_VALUETYPE)0, 0,
			     NULL, &output_tree);
	if (err != LY_SUCCESS) {
		lyd_free_tree(input_tree);
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				    "Invalid data path");
	}

	// Execute callback registered for this XPath.
	if (nb_callback_rpc(nb_node, xpath, input_tree, output_tree, errmsg,
			    sizeof(errmsg)) != NB_OK) {
		flog_warn(EC_LIB_NB_CB_RPC, "%s: rpc callback failed: %s",
			  __func__, xpath);
		lyd_free_tree(input_tree);
		lyd_free_tree(output_tree);

		return grpc::Status(grpc::StatusCode::INTERNAL, "RPC failed");
	}

	// Process output parameters.
	LY_LIST_FOR (lyd_child(output_tree), child) {
		// Response: repeated PathValue output = 1;
		frr::PathValue *pv = tag->response.add_output();
		pv->set_path(lyd_path(child, LYD_PATH_STD, path, sizeof(path)));
		pv->set_value(yang_dnode_get_string(child, NULL));
	}

	// Release memory.
	lyd_free_tree(input_tree);
	lyd_free_tree(output_tree);

	return grpc::Status::OK;
}

// ------------------------------------------------------
/* 线程初始化与运行函数：创建承载 gRPC 的线程、启动 / 结束服务器、以及模块初始化挂接。 */
//        Thread Initialization and Run Functions
// ------------------------------------------------------


/* REQUEST_NEWRPC(NAME, cdb)：为某个一元 RPC 构造 UnaryRpcState 并立即投递一次请求。
 * cdb 为该 RPC 需要的候选配置仓库（不需要时传 NULL）。 */
#define REQUEST_NEWRPC(NAME, cdb)                                              \
	do {                                                                   \
		auto _rpcState = new UnaryRpcState<frr::NAME##Request,         \
						   frr::NAME##Response>(       \
			(cdb), &frr::Northbound::AsyncService::Request##NAME,  \
			&HandleUnary##NAME, #NAME);                            \
		_rpcState->do_request(&service, cq.get(), true);               \
	} while (0)

/* REQUEST_NEWRPC_STREAMING(NAME)：为某个流式 RPC 构造 StreamRpcState 并投递一次请求。
 * 上下文类型约定为 NAME##ContextType。 */
#define REQUEST_NEWRPC_STREAMING(NAME)                                         \
	do {                                                                   \
		auto _rpcState = new StreamRpcState<frr::NAME##Request,        \
						    frr::NAME##Response,       \
						    NAME##ContextType>(        \
			&frr::Northbound::AsyncService::Request##NAME,         \
			&HandleStreaming##NAME, #NAME);                        \
		_rpcState->do_request(&service, cq.get(), true);               \
	} while (0)

/* struct grpc_pthread_attr：FRR 线程属性 + gRPC 监听端口，用于把端口传给新线程。 */
struct grpc_pthread_attr {
	struct frr_pthread_attr attr;
	unsigned long port;
};

/* s_server_lock / s_server：捕获服务器对象以便在主线程干净地关闭它；
 * 锁用来保护对 s_server 与 grpc_running 的并发访问。 */
// Capture these objects so we can try to shut down cleanly
static pthread_mutex_t s_server_lock = PTHREAD_MUTEX_INITIALIZER;
static grpc::Server *s_server;

/* grpc_pthread_start：gRPC 线程的入口。创建服务器与完成队列、注册所有 RPC 的处理状态，
 * 然后进入 while 循环不断从完成队列取事件并分发；退出时关闭服务器与完成队列、排空未处理事件。 */
static void *grpc_pthread_start(void *arg)
{
	struct frr_pthread *fpt = static_cast<frr_pthread *>(arg);
	uint port = (uint) reinterpret_cast<intptr_t>(fpt->data);

	Candidates candidates;
	grpc::ServerBuilder builder;
	std::stringstream server_address;
	frr::Northbound::AsyncService service;

	frr_pthread_set_name(fpt);

	server_address << "0.0.0.0:" << port;
	builder.AddListeningPort(server_address.str(),
				 grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	builder.AddChannelArgument(
		GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
	std::unique_ptr<grpc::ServerCompletionQueue> cq =
		builder.AddCompletionQueue();
	std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
	s_server = server.get();

	pthread_mutex_lock(&s_server_lock); // Make coverity happy
	grpc_running = true;
	pthread_mutex_unlock(&s_server_lock); // Make coverity happy

	/* Schedule unary RPC handlers */
	REQUEST_NEWRPC(GetCapabilities, NULL);
	REQUEST_NEWRPC(CreateCandidate, &candidates);
	REQUEST_NEWRPC(DeleteCandidate, &candidates);
	REQUEST_NEWRPC(UpdateCandidate, &candidates);
	REQUEST_NEWRPC(EditCandidate, &candidates);
	REQUEST_NEWRPC(LoadToCandidate, &candidates);
	REQUEST_NEWRPC(Commit, &candidates);
	REQUEST_NEWRPC(GetTransaction, NULL);
	REQUEST_NEWRPC(LockConfig, NULL);
	REQUEST_NEWRPC(UnlockConfig, NULL);
	REQUEST_NEWRPC(Execute, NULL);

	/* Schedule streaming RPC handlers */
	REQUEST_NEWRPC_STREAMING(Get);
	REQUEST_NEWRPC_STREAMING(ListTransactions);

	zlog_notice("gRPC server listening on %s",
		    server_address.str().c_str());

	/* Process inbound RPCs */
	bool ok;
	void *tag;
	while (true) {
		if (!cq->Next(&tag, &ok)) {
			grpc_debug("%s: CQ empty exiting", __func__);
			break;
		}

		grpc_debug("%s: got next from CQ tag: %p ok: %d", __func__, tag,
			   ok);

		if (!ok) {
			delete static_cast<RpcStateBase *>(tag);
			break;
		}

		RpcStateBase *rpc = static_cast<RpcStateBase *>(tag);
		if (rpc->get_state() != FINISH)
			rpc->run(&service, cq.get());
		else {
			grpc_debug("%s RPC FINISH -> [delete]", rpc->name);
			delete rpc;
		}
	}

	/* This was probably done for us to get here, but let's be safe */
	pthread_mutex_lock(&s_server_lock);
	grpc_running = false;
	if (s_server) {
		grpc_debug("%s: shutdown server and CQ", __func__);
		server->Shutdown();
		s_server = NULL;
	}
	pthread_mutex_unlock(&s_server_lock);

	grpc_debug("%s: shutting down CQ", __func__);
	cq->Shutdown();

	grpc_debug("%s: draining the CQ", __func__);
	while (cq->Next(&tag, &ok)) {
		grpc_debug("%s: drain tag %p", __func__, tag);
		delete static_cast<RpcStateBase *>(tag);
	}

	zlog_info("%s: exiting from grpc pthread", __func__);
	return NULL;
}


/* frr_grpc_init：创建一个 FRR 线程来跑 gRPC（因为 gRPC 自带事件循环），
 * 并把端口通过线程的 data 字段传给它。 */
static int frr_grpc_init(uint port)
{
	struct frr_pthread_attr attr = {
		.start = grpc_pthread_start,
		.stop = NULL,
	};

	grpc_debug("%s: entered", __func__);

	fpt = frr_pthread_new(&attr, "frr-grpc", "frr-grpc");
	fpt->data = reinterpret_cast<void *>((intptr_t)port);

	/* Create a pthread for gRPC since it runs its own event loop. */
	if (frr_pthread_run(fpt, NULL) < 0) {
		flog_err(EC_LIB_SYSTEM_CALL, "%s: error creating pthread: %s",
			 __func__, safe_strerror(errno));
		return -1;
	}

	return 0;
}

/* frr_grpc_finish：主线程中的收尾函数。Shutdown 服务器让 CQ 的 Next() 退出，join 并销毁
 * gRPC 线程，最后调用 ShutdownProtobufLibrary 清理 protobuf 的"内存泄漏"。 */
static int frr_grpc_finish(void)
{
	grpc_debug("%s: entered", __func__);

	if (!fpt)
		return 0;

	/*
	 * Shut the server down here in main thread. This will cause the wait on
	 * the completion queue (cq.Next()) to exit and cleanup everything else.
	 */
	pthread_mutex_lock(&s_server_lock);
	grpc_running = false;
	if (s_server) {
		grpc_debug("%s: shutdown server", __func__);
		s_server->Shutdown();
		s_server = NULL;
	}
	pthread_mutex_unlock(&s_server_lock);

	grpc_debug("%s: joining and destroy grpc thread", __func__);
	pthread_join(fpt->thread, NULL);
	frr_pthread_destroy(fpt);

	// Fix protobuf 'memory leaks' during shutdown.
	// https://groups.google.com/g/protobuf/c/4y_EmQiCGgs
	google::protobuf::ShutdownProtobufLibrary();

	return 0;
}

/* frr_grpc_module_very_late_init：真正的初始化时机很晚——因为 gRPC 库内部会创建线程，
 * 必须在 fork 之后才初始化，所以把本函数作为事件任务调度，等 fork 后事件循环启动时才执行。
 * 它解析模块加载参数里的端口（缺省用 GRPC_DEFAULT_PORT，合法范围 1025 到 UINT16_MAX）。 */
/*
 * This is done this way because module_init and module_late_init are both
 * called during daemon pre-fork initialization. Because the GRPC library
 * spawns threads internally, we need to delay initializing it until after
 * fork. This is done by scheduling this init function as an event task, since
 * the event loop doesn't run until after fork.
 */
static void frr_grpc_module_very_late_init(struct event *event)
{
	const char *args = THIS_MODULE->load_args;
	uint port = GRPC_DEFAULT_PORT;

	if (args) {
		port = std::stoul(args);
		if (port < 1024 || port > UINT16_MAX) {
			flog_err(EC_LIB_GRPC_INIT,
				 "%s: port number must be between 1025 and %d",
				 __func__, UINT16_MAX);
			goto error;
		}
	}

	if (frr_grpc_init(port) < 0)
		goto error;

	return;

error:
	flog_err(EC_LIB_GRPC_INIT, "failed to initialize the gRPC module");
}

/* frr_grpc_module_late_init：记录主事件循环，注册退出钩子（frr_fini -> frr_grpc_finish），
 * 并把 very_late_init 作为事件任务排入队列。 */
static int frr_grpc_module_late_init(struct event_loop *tm)
{
	main_master = tm;
	hook_register(frr_fini, frr_grpc_finish);
	event_add_event(tm, frr_grpc_module_very_late_init, NULL, 0, NULL);
	return 0;
}

/* frr_grpc_module_init：模块初始化钩子，仅注册 late_init 回调（真正的初始化推迟到 fork 后）。 */
static int frr_grpc_module_init(void)
{
	hook_register(frr_late_init, frr_grpc_module_late_init);

	return 0;
}

/* FRR_MODULE_SETUP：声明本模块的元信息（名 frr_grpc、版本、描述）与初始化函数入口。 */
FRR_MODULE_SETUP(.name = "frr_grpc", .version = FRR_VERSION,
		 .description = "FRR gRPC northbound module",
		 .init = frr_grpc_module_init, );

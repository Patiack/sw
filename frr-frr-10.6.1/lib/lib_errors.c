// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Library-specific error messages.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 *                     Donald Sharp
 */

/*
 * 文件导读: lib_errors.c
 * ----------------------------------------------------------------------------
 * 本文件是 lib 子系统的错误码文本表, 属于 FRR 统一错误码体系(ferr)的注册数据.
 * 说明: 文件中两张表由脚本根据错误码枚举自动生成, 一般不要手工编辑; 如需调整
 *   错误码文本应修改生成器并重新生成.
 * 作用: 为 lib_errors.h 中每个 EC_LIB_* 错误码提供展示给用户的标题, 描述与建议.
 *
 * 核心数据结构:
 *   - static struct log_ref ferr_lib_warn[]: 警告级错误表.
 *   - static struct log_ref ferr_lib_err[]:  错误级错误表.
 *   每个元素是 struct log_ref 的字面量初始化, 含 .code(错误码), .title(短标题),
 *   .description(描述), .suggestion(处置建议); 每张表以 .code = END_FERR 的哨兵
 *   项结束.
 *
 * 与其他模块关系: 使用 ferr.h 的 struct log_ref 与 END_FERR; 由 lib_error_init
 *   调用 log_ref_add 注册进全局错误哈希表, 之后可用 show error 命令查询.
 *
 * 阅读顺序: 先看两张表的结构与哨兵, 再看文件末尾 lib_error_init 的注册逻辑.
 *
 * 易踩的坑: 每张表必须以 END_FERR 结尾, 否则 log_ref_add 会越界扫描; 两张表被
 *   clang-format off 与 on 包裹, 以保持自动生成的排版不被格式化工具改动.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib_errors.h"

/* clang-format off */
/*
 * lib 子系统警告级错误表.
 * 每项给出错误码与面向用户的标题, 描述, 建议; 最后以 END_FERR 哨兵结束.
 * 覆盖 SNMP 警告, stream 校验, Linux 命名空间解析, 事件子系统的慢 CPU / 慢墙钟
 * 线程与线程饥饿, 定时器过久, 路由映射递归上限, 备份配置, VRF 名称长度, 以及
 * 大量 YANG 与北向(northbound)相关的警告.
 */
static struct log_ref ferr_lib_warn[] = {
	{
		.code = EC_LIB_SNMP,
		.title = "SNMP has discovered a warning",
		.description = "The SNMP AgentX library has returned a warning that we should report to the end user",
		.suggestion = "Gather Log data and open an Issue.",
	},
	{
		.code = EC_LIB_STREAM,
		.title = "The stream subsystem has encountered an error",
		.description = "During sanity checking stream.c has detected an error in the data associated with a particular stream",
		.suggestion = "Gather log data and open an Issue, restart FRR",
	},
	{
		.code = EC_LIB_LINUX_NS,
		.title = "The Linux namespace subsystem has encountered a parsing error",
		.description = "During system startup an invalid parameter for the namespace was give to FRR",
		.suggestion = "Gather log data and open an Issue. restart FRR",
	},
	{
		.code = EC_LIB_SLOW_THREAD_CPU,
		.title = "The Event subsystem has detected a slow cpu time process",
		.description = "The Event subsystem has detected a slow process, this typically indicates that FRR is having trouble completing work in a timely manner.  This can be either a misconfiguration, bug, or some combination thereof.  In this case total CPU time was over 5 seconds.  Which indicates that FRR is very busy doing some work and should be addressed",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_SLOW_THREAD_WALL,
		.title = "The Event subsystem has detected a slow wall time process",
		.description = "The Event subsystem has detected a slow process, this typically indicates that FRR is having trouble completing work in a timely manner.  This can be either a misconfiguration, bug or some combination thereof.  In this case total WALL time was over 5 seconds.  Which indicates that FRR might be having trouble being scheduled or some system call is delaying",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_STARVE_THREAD,
		.title = "The Event subsystem has detected a thread starvation issue",
		.description = "The event subsystem has detected a thread starvation issue.  This typically indicates that the system FRR is running on is heavily loaded and this load might be impacting FRR's ability to handle events in a timely fashion",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_NO_THREAD,
		.title = "The Event subsystem has detected an internal FD problem",
		.description = "The Event subsystem has detected a file descriptor read/write event without an associated handling function.  This is a bug, please collect log data and open an issue.",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_TIMER_TOO_LONG,
		.title = "The Event subsystem has detected an internal timer that is scheduled to pop in greater than one year",
		.description = "The Event subsystem has detected a timer being started that will pop in a timer that is greater than one year.  This is a bug, please collect log data and open an issue.",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_RMAP_RECURSION_LIMIT,
		.title = "Reached the Route-Map Recursion Limit",
		.description = "The Route-Map subsystem has detected a route-map depth of RMAP_RECURSION_LIMIT and has stopped processing",
		.suggestion = "Re-work the Route-Map in question to not have so many route-map statements, or recompile FRR with a higher limit",
	},
	{
		.code = EC_LIB_BACKUP_CONFIG,
		.title = "Unable to open configuration file",
		.description = "The config subsystem attempted to read in it's configuration file which failed, so we are falling back to the backup config file to see if it is available",
		.suggestion = "Create configuration file",
	},
	{
		.code = EC_LIB_VRF_LENGTH,
		.title = "The VRF subsystem has encountered a parsing error",
		.description = "The VRF subsystem, during initialization, has found a parsing error with input it has received",
		.suggestion = "Check the length of the vrf name and adjust accordingly",
	},
	{
		.code = EC_LIB_YANG_DATA_TRUNCATED,
		.title = "YANG data truncation",
		.description = "The northbound subsystem has detected that YANG data has been truncated as the given buffer wasn't big enough",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_YANG_UNKNOWN_DATA_PATH,
		.title = "Unknown YANG data path",
		.description = "The northbound subsystem has detected an unknown YANG data path",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_YANG_TRANSLATOR_LOAD,
		.title = "Unable to load YANG module translator",
		.description = "The northbound subsystem has detected an error while loading a YANG module translator",
		.suggestion = "Ensure the YANG module translator file is valid. See documentation for further information.",
	},
	{
		.code = EC_LIB_YANG_TRANSLATION_ERROR,
		.title = "YANG translation error",
		.description = "The northbound subsystem has detected an error while performing a YANG XPath translation",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_NB_DATABASE,
		.title = "The northbound database wasn't initialized correctly",
		.description = "An error occurred while initializing the northbound database. As a result, the configuration rollback feature won't work as expected.",
		.suggestion = "Ensure permissions are correct for FRR files, users and groups are correct."
	},
	{
		.code = EC_LIB_NB_CB_UNNEEDED,
		.title = "Unneeded northbound callback",
		.description = "The northbound subsystem, during initialization, has detected a callback that doesn't need to be implemented",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_CB_CONFIG_VALIDATE,
		.title = "A northbound configuration callback has failed in the VALIDATE phase",
		.description = "A callback used to process a configuration change has returned a validation error",
		.suggestion = "The provided configuration is invalid. Fix any inconsistency and try again.",
	},
	{
		.code = EC_LIB_NB_CB_CONFIG_PREPARE,
		.title = "A northbound configuration callback has failed in the PREPARE phase",
		.description = "A callback used to process a configuration change has returned a resource allocation error",
		.suggestion = "The system might be running out of resources. Check the log for more details.",
	},
	{
		.code = EC_LIB_NB_CB_STATE,
		.title = "A northbound callback for operational data has failed",
		.description = "The northbound subsystem has detected that a callback used to fetch operational data has returned an error",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = EC_LIB_NB_CB_RPC,
		.title = "A northbound RPC callback has failed",
		.description = "The northbound subsystem has detected that a callback used to process YANG RPCs/actions has returned an error",
		.suggestion = "The log message should contain further details on the specific error that occurred; investigate the reported error.",
	},
	{
		.code = EC_LIB_NB_CANDIDATE_INVALID,
		.title = "Invalid candidate configuration",
		.description = "The northbound subsystem failed to validate a candidate configuration",
		.suggestion = "Check the log messages to see the validation errors and edit the candidate configuration to fix them",
	},
	{
		.code = EC_LIB_NB_CANDIDATE_EDIT_ERROR,
		.title = "Failure to edit a candidate configuration",
		.description = "The northbound subsystem failed to edit a candidate configuration",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_OPERATIONAL_DATA,
		.title = "Failure to obtain operational data",
		.description = "The northbound subsystem failed to obtain YANG-modeled operational data",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_TRANSACTION_CREATION_FAILED,
		.title = "Failure to create a configuration transaction",
		.description = "The northbound subsystem failed to create a configuration transaction",
		.suggestion = "The log message should contain further details on the specific error that occurred; investigate the reported error.",
	},
	{
		.code = EC_LIB_NB_TRANSACTION_RECORD_FAILED,
		.title = "Failure to record a configuration transaction",
		.description = "The northbound subsystem failed to record a configuration transaction in the northbound database",
		.suggestion = "Gather log data and open an Issue",
	},
	{
		.code = END_FERR,
	},
};

/*
 * lib 子系统错误级错误表.
 * 每项给出错误码与标题, 描述, 建议; 最后以 END_FERR 哨兵结束.
 * 覆盖权限升降, VRF 启动, socket 与 ZAPI 错误, 系统调用, VTY, 接口与命名空间,
 * ZMQ, YANG 模块加载与数据转换, 北向回调与候选配置, libyang / sysrepo / gRPC
 * 初始化, DNS 解析等错误.
 */
static struct log_ref ferr_lib_err[] = {
	{
		.code = EC_LIB_PRIVILEGES,
		.title = "Failure to raise or lower privileges",
		.description = "FRR attempted to raise or lower its privileges and was unable to do so",
		.suggestion = "Ensure that you are running FRR as the frr user and that the user has sufficient privileges to properly access root privileges"
	},
	{
		.code = EC_LIB_VRF_START,
		.title = "VRF Failure on Start",
		.description = "Upon startup FRR failed to properly initialize and startup the VRF subsystem",
		.suggestion = "Ensure that there is sufficient memory to start processes and restart FRR",
	},
	{
		.code = EC_LIB_SOCKET,
		.title = "Socket Error",
		.description = "When attempting to access a socket a system error has occurred and we were unable to properly complete the request",
		.suggestion = "Ensure that there are sufficient system resources available and ensure that the frr user has sufficient permissions to work.  If necessary open an Issue",
	},
	{
		.code = EC_LIB_ZAPI_MISSMATCH,
		.title = "ZAPI Error",
		.description = "A version miss-match has been detected between zebra and client protocol",
		.suggestion = "Two different versions of FRR have been installed and the install is not properly setup.  Completely stop FRR, remove it from the system and reinstall.  Typically only developers should see this issue."
	},
	{
		.code = EC_LIB_ZAPI_ENCODE,
		.title = "ZAPI Error",
		.description = "The ZAPI subsystem has detected an encoding issue, between zebra and a client protocol",
		.suggestion = "Gather data and open an Issue, also Restart FRR"
	},
	{
		.code = EC_LIB_ZAPI_SOCKET,
		.title = "ZAPI Error",
		.description = "The ZAPI subsystem has detected a socket error between zebra and a client",
		.suggestion = "Restart FRR"
	},
	{
		.code = EC_LIB_SYSTEM_CALL,
		.title = "System Call Error",
		.description = "FRR has detected a error from using a vital system call and has probably already exited",
		.suggestion = "Ensure permissions are correct for FRR files, users and groups are correct. Additionally check that sufficient system resources are available."
	},
	{
		.code = EC_LIB_VTY,
		.title = "VTY Subsystem Error",
		.description = "FRR has detected a problem with the specified configuration file",
		.suggestion = "Ensure configuration file exists and has correct permissions for operations Additionally ensure that all config lines are correct as well",
	},
	{
		.code = EC_LIB_INTERFACE,
		.title = "Interface Subsystem Error",
		.description = "FRR has detected a problem with interface data from the kernel as it deviates from what we would expect to happen via normal netlink messaging",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_NS,
		.title = "NameSpace Subsystem Error",
		.description = "FRR has detected a problem with NameSpace data from the kernel as it deviates from what we would expect to happen via normal kernel messaging",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_DEVELOPMENT,
		.title = "Developmental Escape Error",
		.description = "FRR has detected an issue where new development has not properly updated all code paths.",
		.suggestion = "Open an Issue with all relevant log files"
	},
	{
		.code = EC_LIB_ZMQ,
		.title = "ZMQ Subsystem Error",
		.description = "FRR has detected an issue with the Zero MQ subsystem and ZeroMQ is not working properly now",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_UNAVAILABLE,
		.title = "Feature or system unavailable",
		.description = "FRR was not compiled with support for a particular feature, or it is not available on the current platform",
		.suggestion = "Recompile FRR with the feature enabled, or find out what platforms support the feature"
	},
	{
		.code = EC_LIB_YANG_MODULE_LOAD,
		.title = "Unable to load YANG module from the file system",
		.description = "The northbound subsystem has detected an error while loading a YANG module from the file system",
		.suggestion = "Ensure all FRR YANG modules were installed correctly in the system.",
	},
	{
		.code = EC_LIB_YANG_MODULE_LOADED_ALREADY,
		.title = "Attempt to load a YANG module that is already loaded",
		.description = "The northbound subsystem has detected an attempt to load a YANG module that is already loaded",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_YANG_DATA_CONVERT,
		.title = "YANG data conversion error",
		.description = "An error has occurred while converting a YANG data value from string to binary representation or vice-versa",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_YANG_DNODE_NOT_FOUND,
		.title = "YANG data node not found",
		.description = "The northbound subsystem failed to find a YANG data node that was supposed to exist",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_CB_MISSING,
		.title = "Missing northbound callback",
		.description = "The northbound subsystem, during initialization, has detected a missing callback for one node of the loaded YANG modules",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_CB_INVALID_PRIO,
		.title = "Northbound callback has an invalid priority",
		.description = "The northbound subsystem, during initialization, has detected a callback whose priority is invalid",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_NB_CBS_VALIDATION,
		.title = "Failure to validate the northbound callbacks",
		.description = "The northbound subsystem, during initialization, has detected one or more errors while loading the northbound callbacks",
		.suggestion = "This is a bug; please report it"
	},
	{
		.code = EC_LIB_LIBYANG,
		.title = "The libyang library returned an error",
		.description = "The northbound subsystem has detected that the libyang library returned an error",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_LIBYANG_PLUGIN_LOAD,
		.title = "Failure to load a libyang plugin",
		.description = "The northbound subsystem, during initialization, has detected that a libyang plugin failed to be loaded",
		.suggestion = "Check if the FRR libyang plugins were installed correctly in the system",
	},
	{
		.code = EC_LIB_SYSREPO_INIT,
		.title = "Sysrepo initialization error",
		.description = "Upon startup FRR failed to properly initialize and startup the Sysrepo northbound plugin",
		.suggestion = "Check if Sysrepo is installed correctly in the system",
	},
	{
		.code = EC_LIB_SYSREPO_DATA_CONVERT,
		.title = "Sysrepo data conversion error",
		.description = "An error has occurred while converting a YANG data value to the Sysrepo format",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_LIBSYSREPO,
		.title = "libsysrepo error",
		.description = "The northbound subsystem has detected that the libsysrepo library returned an error",
		.suggestion = "Open an Issue with all relevant log files and restart FRR"
	},
	{
		.code = EC_LIB_GRPC_INIT,
		.title = "gRPC initialization error",
		.description = "Upon startup FRR failed to properly initialize and startup the gRPC northbound plugin",
		.suggestion = "Check if the gRPC libraries are installed correctly in the system.",
	},
	{
		.code = EC_LIB_NB_CB_CONFIG_ABORT,
		.title = "A northbound configuration callback has failed in the ABORT phase",
		.description = "A callback used to process a configuration change has returned an error while trying to abort a change",
		.suggestion = "Gather log data and open an Issue.",
	},
	{
		.code = EC_LIB_NB_CB_CONFIG_APPLY,
		.title = "A northbound configuration callback has failed in the APPLY phase",
		.description = "A callback used to process a configuration change has returned an error while applying the changes",
		.suggestion = "Gather log data and open an Issue.",
	},
	{
		.code = EC_LIB_RESOLVER,
		.title = "DNS Resolution",
		.description = "An error was detected while attempting to resolve a hostname",
		.suggestion = "Ensure that DNS is working properly and the hostname is configured in dns.  If you are still seeing this error, open an issue"
	},
	{
		.code = END_FERR,
	}
};
/* clang-format on */

/*
 * 注册 lib 子系统的两张错误表到全局错误哈希表.
 * 实现: 依次调用 log_ref_add, 把 ferr_lib_warn 与 ferr_lib_err 中的每一项(直到
 *   END_FERR 哨兵)加入 refs 哈希表, 供 log_ref_get 与 show error 查询.
 */
void lib_error_init(void)
{
	log_ref_add(ferr_lib_warn);
	log_ref_add(ferr_lib_err);
}

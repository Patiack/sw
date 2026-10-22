// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * February 22 2024, Christian Hopps <chopps@labn.net>
 *
 * Copyright (C) 2024 LabN Consulting, L.L.C.
 */
/*
 * 文件导读：keychain_cli.c —— 密钥链的 VTY 命令行层
 *
 * 1. 本文件在 FRR 里干什么：
 *    把用户在 VTY 里输入的 key chain 相关命令（key chain、key、key-string、
 *    cryptographic-algorithm、send-lifetime / accept-lifetime 等）翻译成对
 *    frr-keychain YANG 模型的修改请求，交由北向（northbound）框架统一落库；
 *    同时把运行配置回显为命令行（cli_write 系列回调）。
 *
 * 2. 关键机制：
 *    - DEFPY_YANG / DEFPY_YANG_NOSH：YANG 化的命令宏，函数体里用
 *      nb_cli_enqueue_change 往待提交列表塞改动，再 nb_cli_apply_changes
 *      统一提交；成功后可 VTY_PUSH_XPATH 进入子视图。
 *    - 配置回显走 *_cli_write 回调，由 northbound 在 show running-config
 *      时按 XPath 逐节点调用。
 *    - ietf_key_chain_cli_info 把各 XPath 与 cli_show 回调绑定在一起。
 *
 * 3. 核心流程（时间字符串）：
 *    用户输入的日期 / 时间（HH:MM:SS、日、月、年）经 __lifetime_set 拼成
 *    ISO 8601 串（带本地时区偏移），写入 YANG 的 start-date-time /
 *    end-date-time；duration 与 infinite 则写对应叶子节点。
 *
 * 4. 阅读顺序建议：
 *    先看头部几条 key chain / key / key-string / 算法命令，再看 month_name
 *    及 __get_month / __timezone_offset / __lifetime_set 等辅助函数，然后
 *    是大量 send-lifetime / accept-lifetime 命令（结构高度相似），接着是
 *    cli_write 回显回调，最后是模块信息、cmd_node 与 keychain_cli_init。
 *
 * 5. 易踩的坑：
 *    - send-lifetime / accept-lifetime 有多种日期顺序（日在前或月在前），
 *      对应多个命令，argv 下标各不相同，注意对齐；
 *    - 命令名以 no- 开头的使用 NB_OP_DESTROY。
 */
#include <zebra.h>
#include "command.h"
#include "keychain.h"
#include "northbound.h"
#include "northbound_cli.h"
#include "vty.h"

#include "lib/keychain_cli_clippy.c"

/*
 * 命令：key chain WORD
 * 在 /ietf-key-chain:key-chains 下创建（或进入）指定名称的密钥链。
 * 通过 NB_OP_CREATE 提交 XPath，成功后压栈进入 KEYCHAIN_NODE 子视图。
 */
DEFPY_YANG_NOSH(
       key_chain,
       key_chain_cmd,
       "key chain WORD",
       "Authentication key management\n"
       "Key-chain management\n"
       "Key-chain name\n")
{
	char *xpath;
	int ret;

	xpath = asprintfrr(MTYPE_TMP,
			   "/ietf-key-chain:key-chains/key-chain[name='%s']",
			   chain);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	ret = nb_cli_apply_changes(vty, NULL);
	if (ret == CMD_SUCCESS)
		VTY_PUSH_XPATH(KEYCHAIN_NODE, xpath);
	XFREE(MTYPE_TMP, xpath);
	return ret;
}

/*
 * 命令：no key chain WORD
 * 删除指定名称的密钥链；用 NB_OP_DESTROY 提交并清空待提交列表。
 */
DEFPY_YANG(
       no_key_chain,
       no_key_chain_cmd,
       "no key chain WORD",
       NO_STR
       "Authentication key management\n"
       "Key-chain management\n"
       "Key-chain name\n")
{
	char *xpath;

	xpath = asprintfrr(MTYPE_TMP,
			   "/ietf-key-chain:key-chains/key-chain[name='%s']",
			   chain);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	XFREE(MTYPE_TMP, xpath);
	return nb_cli_apply_changes_clear_pending(vty, NULL);
}

/*
 * 命令：key (0-2147483647)
 * 在当前密钥链下创建（或进入）指定编号的 key，进入 KEYCHAIN_KEY_NODE。
 * key-id 由 key_str 提供（对应命令数字参数）。
 */
DEFPY_YANG_NOSH(
       key,
       key_cmd,
       "key (0-2147483647)",
       "Configure a key\n"
       "Key identifier number\n")
{
	char *xpath;
	int ret;

	xpath = asprintfrr(MTYPE_TMP, "%s/key[key-id='%s']", VTY_CURR_XPATH,
			   key_str);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, NULL);
	ret = nb_cli_apply_changes(vty, NULL);
	if (ret == CMD_SUCCESS)
		VTY_PUSH_XPATH(KEYCHAIN_KEY_NODE, xpath);
	XFREE(MTYPE_TMP, xpath);
	return ret;
}

/*
 * 命令：no key (0-2147483647)
 * 删除当前密钥链下指定编号的 key。
 */
DEFPY_YANG(
       no_key,
       no_key_cmd,
       "no key (0-2147483647)",
       NO_STR
       "Delete a key\n"
       "Key identifier number\n")
{
	char *xpath;

	xpath = asprintfrr(MTYPE_TMP, "%s/key[key-id='%s']", VTY_CURR_XPATH,
			   key_str);
	nb_cli_enqueue_change(vty, xpath, NB_OP_DESTROY, NULL);
	XFREE(MTYPE_TMP, xpath);
	return nb_cli_apply_changes_clear_pending(vty, NULL);
}

/*
 * 命令：key-string LINE
 * 设置当前 key 的密钥字符串，写入 key-string/keystring 叶子。
 */
DEFPY_YANG(
       key_string,
       key_string_cmd,
       "key-string LINE",
       "Set key string\n"
       "The key\n")
{
	nb_cli_enqueue_change(vty, "./key-string/keystring", NB_OP_CREATE, line);
	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 命令：no key-string [LINE]
 * 清除当前 key 的密钥字符串。
 */
DEFPY_YANG(
       no_key_string,
       no_key_string_cmd,
       "no key-string [LINE]",
       NO_STR
       "Unset key string\n"
       "The key\n")
{
	nb_cli_enqueue_change(vty, "./key-string/keystring", NB_OP_DESTROY, line);
	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 命令：cryptographic-algorithm <md5|hmac-sha-1|...|hmac-sha-512>
 * 设置当前 key 的哈希 / HMAC 算法，写入 crypto-algorithm 叶子。
 */
DEFPY_YANG(
      cryptographic_algorithm,
      cryptographic_algorithm_cmd,
      "cryptographic-algorithm "
      "<md5|hmac-sha-1|hmac-sha-256|hmac-sha-384|hmac-sha-512>$algo",
      "Cryptographic-algorithm\n"
      "Use MD5 algorithm\n"
      "Use HMAC-SHA-1 algorithm\n"
      "Use HMAC-SHA-256 algorithm\n"
      "Use HMAC-SHA-384 algorithm\n"
      "Use HMAC-SHA-512 algorithm\n")
{
	nb_cli_enqueue_change(vty, "./crypto-algorithm", NB_OP_CREATE, algo);
	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 命令：no cryptographic-algorithm [<algo>]
 * 清除当前 key 的哈希算法设置。
 */
DEFPY_YANG(
      no_cryptographic_algorithm,
      no_cryptographic_algorithm_cmd,
      "no cryptographic-algorithm "
      "[<md5|hmac-sha-1|hmac-sha-256|hmac-sha-384|hmac-sha-512>$algo]",
      NO_STR
      "Cryptographic-algorithm\n"
      "Use MD5 algorithm\n"
      "Use HMAC-SHA-1 algorithm\n"
      "Use HMAC-SHA-256 algorithm\n"
      "Use HMAC-SHA-384 algorithm\n"
      "Use HMAC-SHA-512 algorithm\n")
{
	nb_cli_enqueue_change(vty, "./crypto-algorithm", NB_OP_DESTROY, algo);
	return nb_cli_apply_changes(vty, NULL);
}

/* 月份英文全名表（下标 1 起始对应一月），供 __get_month 前缀匹配使用 */
const char *month_name[] = {
	"january", "february", "march", "april", "may", "june", "july",
	"august", "september", "october", "november", "december", NULL
};

/*
 * 把月份字符串（英文，可为前若干字符的前缀）转换为月份序号 1~12。
 * 参数：month_str 用户输入的月份名。
 * 返回值：1~12；无法识别（长度小于 3 或无匹配）返回 -1。
 * 注意：对 month_name 做不区分大小写的前缀比较（strncasecmp）。
 */
static int __get_month(const char *month_str)
{
	int i, len;

	len = strlen(month_str);
	if (len < 3)
		return -1;
	for (i = 1; month_name[i-1]; i++)
		if (strncasecmp(month_str, month_name[i-1], len) == 0)
			return i;
	return -1;
}


/*
 * 取本地时区相对于 UTC 的偏移（秒）。
 * 返回值：本地时区偏移秒数（东为正，西为负），来自 tm_gmtoff。
 */
static long __timezone_offset(void)
{
	time_t now;
	struct tm *tm_now;

	time(&now);
	tm_now = localtime(&now);
	return tm_now->tm_gmtoff;
}

/*
 * 组装并提交一个 lifetime 时间点（start-date-time 或 end-date-time）。
 * 参数：vty 会话；timebuf 输出缓冲（32 字节，供调用方后续读取）；
 *       time_node 时间点路径名（如 send-lifetime / accept-lifetime）；
 *       leaf_node 叶子名（start-date-time / end-date-time）；
 *       time_str 时分秒；day_str 日；month_str 月；year_str 年。
 * 行为：校验月份，取本地时区偏移，拼成 ISO 8601 串写入 YANG。
 * 返回值：0 成功；-1 失败（月份非法，会向 vty 打印错误）。
 */
static int __lifetime_set(struct vty *vty, char timebuf[32],
			  const char *time_node, const char *leaf_node,
			  const char *time_str, const char *day_str,
			  const char *month_str, const char *year_str)
{
	char xpath[128];
	int month = __get_month(month_str);
	int hoff, moff;
	long offset;

	if (month < 1) {
		vty_out(vty, "Bad month value: %s\n", month_str);
		return -1;
	}

	offset = __timezone_offset();
	hoff = offset / 3600;
	if (offset < 0)
		offset = -offset;
	moff = (offset % 3600) / 60;

	snprintf(timebuf, 32, "%s-%02d-%02dT%s%+03d:%02d", year_str, month,
		 atoi(day_str), time_str, hoff, moff);
	snprintf(xpath, sizeof(xpath), "./lifetime/%s/%s", time_node, leaf_node);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, timebuf);
	return 0;
}


/*
 * 设置带明确起止时间点的 lifetime。
 * 依次提交 start-date-time 与 end-date-time，任一失败返回
 * CMD_WARNING_CONFIG_FAILED；否则统一 apply。
 * 参数：time_node 为 send-lifetime 或 accept-lifetime，其余为各时间字段串。
 * 返回值：nb_cli_apply_changes 的结果或 CMD_WARNING_CONFIG_FAILED。
 */
static int key_lifetime_set(struct vty *vty, const char *time_node,
			    const char *stime_str, const char *sday_str,
			    const char *smonth_str, const char *syear_str,
			    const char *etime_str, const char *eday_str,
			    const char *emonth_str, const char *eyear_str)
{
	char time1[32];
	char time2[32];

	if (__lifetime_set(vty, time1, time_node, "start-date-time", stime_str,
			   sday_str, smonth_str, syear_str))
		return CMD_WARNING_CONFIG_FAILED;

	if (__lifetime_set(vty, time2, time_node, "end-date-time", etime_str,
			   eday_str, emonth_str, eyear_str))
		return CMD_WARNING_CONFIG_FAILED;

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 设置 lifetime：起始时间点 + duration（持续秒数）。
 * 参数同 key_lifetime_set，最后一项 duration_str 为持续秒数。
 * 返回值：apply 结果或 CMD_WARNING_CONFIG_FAILED。
 */
static int key_lifetime_duration_set(struct vty *vty, const char *time_node,
				     const char *stime_str, const char *sday_str,
				     const char *smonth_str,
				     const char *syear_str,
				     const char *duration_str)
{
	char xpath[128];
	char time[32];

	if (__lifetime_set(vty, time, time_node, "start-date-time", stime_str,
			   sday_str, smonth_str, syear_str))
		return CMD_WARNING_CONFIG_FAILED;

	/* End time. */
	snprintf(xpath, sizeof(xpath), "./lifetime/%s/duration", time_node);
	nb_cli_enqueue_change(vty, xpath, NB_OP_CREATE, duration_str);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 设置 lifetime：起始时间点 + 永不过期（no-end-time）。
 * 参数同 key_lifetime_set（无结束时间字段）。
 * 返回值：apply 结果或 CMD_WARNING_CONFIG_FAILED。
 */
static int key_lifetime_infinite_set(struct vty *vty, const char *time_node,
				     const char *stime_str, const char *sday_str,
				     const char *smonth_str,
				     const char *syear_str)
{
	char xpath[128];
	char time[32];

	if (__lifetime_set(vty, time, time_node, "start-date-time", stime_str,
			   sday_str, smonth_str, syear_str))
		return CMD_WARNING_CONFIG_FAILED;

	/* End time. */
	snprintf(xpath, sizeof(xpath), "./lifetime/%s/no-end-time", time_node);
	nb_cli_enqueue_change(vty, xpath, NB_OP_MODIFY, NULL);

	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 命令：accept-lifetime HH:MM:SS 日 月 年 HH:MM:SS 日 月 年
 * 接收窗口：起止均为 日-月-年。转交 key_lifetime_set 提交。
 */
DEFPY_YANG(
       accept_lifetime_day_month_day_month,
       accept_lifetime_day_month_day_month_cmd,
       "accept-lifetime HH:MM:SS (1-31) MONTH (1993-2035) HH:MM:SS (1-31) MONTH (1993-2035)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Day of th month to expire\n"
       "Month of the year to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_number_3 = 6;
	int idx_month_2 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "accept-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 日-月-年，结束 月-日-年 */
DEFPY_YANG(accept_lifetime_day_month_month_day,
       accept_lifetime_day_month_month_day_cmd,
       "accept-lifetime HH:MM:SS (1-31) MONTH (1993-2035) HH:MM:SS MONTH (1-31) (1993-2035)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Month of the year to expire\n"
       "Day of th month to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_month_2 = 6;
	int idx_number_3 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "accept-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 月-日-年，结束 日-月-年 */
DEFPY_YANG(accept_lifetime_month_day_day_month,
       accept_lifetime_month_day_day_month_cmd,
       "accept-lifetime HH:MM:SS MONTH (1-31) (1993-2035) HH:MM:SS (1-31) MONTH (1993-2035)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Day of th month to expire\n"
       "Month of the year to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_number_3 = 6;
	int idx_month_2 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "accept-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起止均为 月-日-年 */
DEFPY_YANG(accept_lifetime_month_day_month_day,
       accept_lifetime_month_day_month_day_cmd,
       "accept-lifetime HH:MM:SS MONTH (1-31) (1993-2035) HH:MM:SS MONTH (1-31) (1993-2035)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Month of the year to expire\n"
       "Day of th month to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_month_2 = 6;
	int idx_number_3 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "accept-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 日-月-年，永不过期 */
DEFPY_YANG(accept_lifetime_infinite_day_month,
       accept_lifetime_infinite_day_month_cmd,
       "accept-lifetime HH:MM:SS (1-31) MONTH (1993-2035) infinite",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Never expires\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;

	return key_lifetime_infinite_set(vty, "accept-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 月-日-年，永不过期 */
DEFPY_YANG(accept_lifetime_infinite_month_day,
       accept_lifetime_infinite_month_day_cmd,
       "accept-lifetime HH:MM:SS MONTH (1-31) (1993-2035) infinite",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Never expires\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;

	return key_lifetime_infinite_set(vty, "accept-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 日-月-年，持续 duration 秒 */
DEFPY_YANG(accept_lifetime_duration_day_month,
       accept_lifetime_duration_day_month_cmd,
       "accept-lifetime HH:MM:SS (1-31) MONTH (1993-2035) duration (1-2147483646)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Duration of the key\n"
       "Duration seconds\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 6;

	return key_lifetime_duration_set(vty, "accept-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg,
					 argv[idx_number_3]->arg);
}

/* 命令：accept-lifetime ... 接收窗口：起始 月-日-年，持续 duration 秒 */
DEFPY_YANG(accept_lifetime_duration_month_day,
       accept_lifetime_duration_month_day_cmd,
       "accept-lifetime HH:MM:SS MONTH (1-31) (1993-2035) duration (1-2147483646)",
       "Set accept lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Duration of the key\n"
       "Duration seconds\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 6;

	return key_lifetime_duration_set(vty, "accept-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg,
					 argv[idx_number_3]->arg);
}

/* 命令：no accept-lifetime，清除接收窗口设置 */
DEFPY_YANG(no_accept_lifetime,
       no_accept_lifetime_cmd,
       "no accept-lifetime",
       NO_STR
       "Unset accept-lifetime\n")
{
	nb_cli_enqueue_change(vty, "accept-lifetime", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/*
 * 命令系列：send-lifetime ...
 * 发送窗口的多种日期写法，语义与对应的 accept-lifetime 相同，
 * 只是作用于 send-lifetime 时间节点。以下各条不再逐一展开。
 */
DEFPY_YANG(
	send_lifetime_day_month_day_month, send_lifetime_day_month_day_month_cmd,
	"send-lifetime HH:MM:SS (1-31) MONTH (1993-2035) HH:MM:SS (1-31) MONTH (1993-2035)",
	"Set send lifetime of the key\n"
	"Time to start\n"
	"Day of th month to start\n"
	"Month of the year to start\n"
	"Year to start\n"
	"Time to expire\n"
	"Day of th month to expire\n"
	"Month of the year to expire\n"
	"Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_number_3 = 6;
	int idx_month_2 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "send-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 日-月-年，结束 月-日-年 */
DEFPY_YANG(send_lifetime_day_month_month_day,
       send_lifetime_day_month_month_day_cmd,
       "send-lifetime HH:MM:SS (1-31) MONTH (1993-2035) HH:MM:SS MONTH (1-31) (1993-2035)",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Month of the year to expire\n"
       "Day of th month to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_month_2 = 6;
	int idx_number_3 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "send-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 月-日-年，结束 日-月-年 */
DEFPY_YANG(send_lifetime_month_day_day_month,
       send_lifetime_month_day_day_month_cmd,
       "send-lifetime HH:MM:SS MONTH (1-31) (1993-2035) HH:MM:SS (1-31) MONTH (1993-2035)",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Day of th month to expire\n"
       "Month of the year to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_number_3 = 6;
	int idx_month_2 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "send-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起止均为 月-日-年 */
DEFPY_YANG(send_lifetime_month_day_month_day,
       send_lifetime_month_day_month_day_cmd,
       "send-lifetime HH:MM:SS MONTH (1-31) (1993-2035) HH:MM:SS MONTH (1-31) (1993-2035)",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Time to expire\n"
       "Month of the year to expire\n"
       "Day of th month to expire\n"
       "Year to expire\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_hhmmss_2 = 5;
	int idx_month_2 = 6;
	int idx_number_3 = 7;
	int idx_number_4 = 8;

	return key_lifetime_set(vty, "send-lifetime", argv[idx_hhmmss]->arg,
				argv[idx_number]->arg, argv[idx_month]->arg,
				argv[idx_number_2]->arg, argv[idx_hhmmss_2]->arg,
				argv[idx_number_3]->arg, argv[idx_month_2]->arg,
				argv[idx_number_4]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 日-月-年，永不过期 */
DEFPY_YANG(send_lifetime_infinite_day_month,
       send_lifetime_infinite_day_month_cmd,
       "send-lifetime HH:MM:SS (1-31) MONTH (1993-2035) infinite",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Never expires\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;

	return key_lifetime_infinite_set(vty, "send-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 月-日-年，永不过期 */
DEFPY_YANG(send_lifetime_infinite_month_day,
       send_lifetime_infinite_month_day_cmd,
       "send-lifetime HH:MM:SS MONTH (1-31) (1993-2035) infinite",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Never expires\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;

	return key_lifetime_infinite_set(vty, "send-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 日-月-年，持续 duration 秒 */
DEFPY_YANG(send_lifetime_duration_day_month,
       send_lifetime_duration_day_month_cmd,
       "send-lifetime HH:MM:SS (1-31) MONTH (1993-2035) duration (1-2147483646)",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Day of th month to start\n"
       "Month of the year to start\n"
       "Year to start\n"
       "Duration of the key\n"
       "Duration seconds\n")
{
	int idx_hhmmss = 1;
	int idx_number = 2;
	int idx_month = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 6;

	return key_lifetime_duration_set(vty, "send-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg,
					 argv[idx_number_3]->arg);
}

/* 命令：send-lifetime ... 发送窗口：起始 月-日-年，持续 duration 秒 */
DEFPY_YANG(send_lifetime_duration_month_day,
       send_lifetime_duration_month_day_cmd,
       "send-lifetime HH:MM:SS MONTH (1-31) (1993-2035) duration (1-2147483646)",
       "Set send lifetime of the key\n"
       "Time to start\n"
       "Month of the year to start\n"
       "Day of th month to start\n"
       "Year to start\n"
       "Duration of the key\n"
       "Duration seconds\n")
{
	int idx_hhmmss = 1;
	int idx_month = 2;
	int idx_number = 3;
	int idx_number_2 = 4;
	int idx_number_3 = 6;

	return key_lifetime_duration_set(vty, "send-lifetime",
					 argv[idx_hhmmss]->arg,
					 argv[idx_number]->arg,
					 argv[idx_month]->arg,
					 argv[idx_number_2]->arg,
					 argv[idx_number_3]->arg);
}

/* 命令：no send-lifetime，清除发送窗口设置 */
DEFUN (no_send_lifetime,
       no_send_lifetime_cmd,
       "no send-lifetime",
       NO_STR
       "Unset send-lifetime\n")
{
	nb_cli_enqueue_change(vty, "send-lifetime", NB_OP_DESTROY, NULL);
	return nb_cli_apply_changes(vty, NULL);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain
 */
/*
 * 回显回调：/ietf-key-chain:key-chains/key-chain
 * 输出 "key chain <name>" 一行。show_defaults 参数未使用。
 */
void key_chains_key_chain_cli_write(struct vty *vty,
				    const struct lyd_node *dnode,
				    bool show_defaults)
{
	vty_out(vty, "key chain %s\n", yang_dnode_get_string(dnode, "name"));
}

/*
 * 回显结束回调：/ietf-key-chain:key-chains/key-chain
 * 输出 "exit" 与 "!"，结束该密钥链配置块。
 */
void key_chains_key_chain_cli_write_end(struct vty *vty,
					const struct lyd_node *dnode)
{
	vty_out(vty, "exit\n");
	vty_out(vty, "!\n");
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/description
 */
/*
 * 回显回调：/ietf-key-chain:key-chains/key-chain/description
 * 当前为空实现（description 的 CLI 输出被注释掉，尚未启用）。
 */
void key_chains_key_chain_description_cli_write(struct vty *vty,
						const struct lyd_node *dnode,
						bool show_defaults)
{
	/* Implement CLI */
	/* vty_out(vty, " description %s\n", yang_dnode_get_string(dnode)); */
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key
 */
/*
 * 回显回调：/ietf-key-chain:key-chains/key-chain/key
 * 输出 " key <key-id>" 一行。
 */
void key_chains_key_chain_key_cli_write(struct vty *vty,
					const struct lyd_node *dnode,
					bool show_defaults)
{
	vty_out(vty, " key %s\n", yang_dnode_get_string(dnode, "key-id"));
}

/*
 * 回显结束回调：/ietf-key-chain:key-chains/key-chain/key
 * 输出 " exit" 结束该 key 配置块（不输出 !）。
 */
void key_chains_key_chain_key_cli_write_end(struct vty *vty,
					    const struct lyd_node *dnode)
{
	vty_out(vty, " exit\n");
}

/*
 * 把 lifetime 的 start-date-time 节点渲染为命令行时间串（回显用）。
 * 参数：buf 输出缓冲；bufsize 缓冲大小；lt_start_dnode 指向 start-date-time。
 * 规则：若同级存在 no-end-time 追加 " infinite"；若存在 duration 追加
 *       " duration N"；否则追加结束时间 " <时> <月> <日> <年>"。
 * 返回值：始终返回 buf。
 * 注意：时间串先经 ly_time_str2time 解析再用本地时区格式化。
 */
static const char *__dnode_to_key_strftime(char *buf, size_t bufsize,
					   const struct lyd_node *lt_start_dnode)
{
	const char *timestr;
	struct lyd_node *end_node;
	struct tm tm;
	uint32_t duration;
	time_t time;
	int len, sz;
	char *s;

	s = buf;
	sz = bufsize;

	timestr = yang_dnode_get_string(lt_start_dnode, NULL);
	(void)ly_time_str2time(timestr, &time, NULL);
	localtime_r(&time, &tm);
	len = strftime(s, sz, "%T %b %e %Y", &tm);
	s += len;
	sz -= len;

	if (yang_dnode_exists(lt_start_dnode, "../no-end-time")) {
		strlcat(s, " infinite", sz);
		return buf;
	}

	end_node = yang_dnode_get(lt_start_dnode, "../duration");
	if (end_node) {
		duration = yang_dnode_get_uint32(end_node, NULL);
		snprintf(s, sz, " duration %u", (uint)duration);
		return buf;
	}

	timestr = yang_dnode_get_string(lt_start_dnode, "../end-date-time");
	(void)ly_time_str2time(timestr, &time, NULL);
	localtime_r(&time, &tm);
	strftime(s, sz, " %T %b %e %Y", &tm);
	return buf;
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/start-date-time
 */
/*
 * 回显回调：lifetime/send-accept-lifetime/start-date-time
 * 发送与接收窗口共用一个起始时间时，同时输出 send-lifetime 与
 * accept-lifetime 两行（取自同一时间串）。
 */
void key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_cli_write(
	struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	char s[256];

	vty_out(vty, "  send-lifetime %s\n",
		__dnode_to_key_strftime(s, sizeof(s), dnode));
	vty_out(vty, "  accept-lifetime %s\n", s);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/start-date-time
 */
/*
 * 回显回调：lifetime/send-lifetime/start-date-time
 * 仅输出 send-lifetime 一行（发送 / 接收窗口各自独立时）。
 */
void key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_cli_write(
	struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	char s[256];

	vty_out(vty, "  send-lifetime %s\n",
		__dnode_to_key_strftime(s, sizeof(s), dnode));
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/start-date-time
 */
/*
 * 回显回调：lifetime/accept-lifetime/start-date-time
 * 仅输出 accept-lifetime 一行。
 */
void key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_cli_write(
	struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	char s[256];

	vty_out(vty, "  accept-lifetime %s\n",
		__dnode_to_key_strftime(s, sizeof(s), dnode));
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/crypto-algorithm
 */
/*
 * 回显回调：key/crypto-algorithm
 * 输出 "  cryptographic-algorithm <name>"。若取到的标识带有
 * "ietf-key-chain:" 前缀则剥掉前缀再输出。
 */
void key_chains_key_chain_key_crypto_algorithm_cli_write(
	struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	static const char prefix[] = "ietf-key-chain:";
	static const int prefix_len = sizeof(prefix) - 1;
	const char *name = yang_dnode_get_string(dnode, NULL);

	if (!strncmp(name, prefix, prefix_len))
		name += prefix_len;
	vty_out(vty, "  cryptographic-algorithm %s\n", name);
}

/*
 * XPath: /ietf-key-chain:key-chains/key-chain/key/key-string/keystring
 */
/*
 * 回显回调：key/key-string/keystring
 * 输出 "  key-string <字符串>"。
 * 注意：这里回显的是密钥明文，配置文件中密钥是明文保存的。
 */
void key_chains_key_chain_key_key_string_keystring_cli_write(
	struct vty *vty, const struct lyd_node *dnode, bool show_defaults)
{
	vty_out(vty, "  key-string %s\n", yang_dnode_get_string(dnode, NULL));
}

/*
 * 本 CLI 模块声明的 YANG feature 列表。
 * independent-send-accept-lifetime 表示支持发送 / 接收窗口各自独立配置。
 * 以 NULL 结尾。
 */
static const char * const keychain_features[] = {
	"independent-send-accept-lifetime",
	NULL,
};

/* clang-format off */
/*
 * ietf-key-chain CLI 模块信息：把各 YANG XPath 与回显回调绑定。
 * ignore_cfg_cbs = true 表示本模块只用 CLI 回显回调，不注册配置回调。
 * 末尾以 xpath = NULL 的哨兵节点结束。
 */
const struct frr_yang_module_info ietf_key_chain_cli_info = {
	.name = "ietf-key-chain",
	.features = (const char **)keychain_features,
	.ignore_cfg_cbs = true,
	.nodes = {
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain",
			.cbs = {
				.cli_show = key_chains_key_chain_cli_write,
				.cli_show_end = key_chains_key_chain_cli_write_end,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/description",
			.cbs = {
				.cli_show = key_chains_key_chain_description_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key",
			.cbs = {
				.cli_show = key_chains_key_chain_key_cli_write,
				.cli_show_end = key_chains_key_chain_key_cli_write_end,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-accept-lifetime/start-date-time",
			.cbs = {
				.cli_show = key_chains_key_chain_key_lifetime_send_accept_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/send-lifetime/start-date-time",
			.cbs = {
				.cli_show = key_chains_key_chain_key_lifetime_send_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/lifetime/accept-lifetime/start-date-time",
			.cbs = {
				.cli_show = key_chains_key_chain_key_lifetime_accept_lifetime_start_date_time_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/crypto-algorithm",
			.cbs = {
				.cli_show = key_chains_key_chain_key_crypto_algorithm_cli_write,
			}
		},
		{
			.xpath = "/ietf-key-chain:key-chains/key-chain/key/key-string/keystring",
			.cbs = {
				.cli_show = key_chains_key_chain_key_key_string_keystring_cli_write,
			}
		},
		{
			.xpath = NULL,
		},
	}
};

/*
 * 配置写出函数：把运行配置中的 /ietf-key-chain:key-chains 子树
 * 渲染成命令行写入 vty（供 show running-config 使用）。
 * 返回值：写出内容则为 1，无该子树则为 0。
 */
static int keychain_config_write(struct vty *vty)
{
	const struct lyd_node *dnode;
	int written = 0;

	dnode = yang_dnode_get(running_config->dnode,
			       "/ietf-key-chain:key-chains");
	if (dnode) {
		nb_cli_show_dnode_cmds(vty, dnode, false);
		written = 1;
	}
	return written;
}

/*
 * KEYCHAIN_NODE 命令节点定义：提示符为 "(config-keychain)#"，
 * 挂在 CONFIG_NODE 下，config_write 指向 keychain_config_write。
 */
static struct cmd_node keychain_node = {
	.name = "keychain",
	.node = KEYCHAIN_NODE,
	.parent_node = CONFIG_NODE,
	.prompt = "%s(config-keychain)# ",
	.config_write = keychain_config_write,
};

/*
 * KEYCHAIN_KEY_NODE 命令节点定义：提示符 "(config-keychain-key)#"，
 * 父节点为 KEYCHAIN_NODE（不单独写出配置）。
 */
static struct cmd_node keychain_key_node = {
	.name = "keychain key",
	.node = KEYCHAIN_KEY_NODE,
	.parent_node = KEYCHAIN_NODE,
	.prompt = "%s(config-keychain-key)# ",
};

/*
 * 命令变量补全处理器：为 key chain 名称提供自动补全。
 * key_chain 对应 WORD 变量名，KEYCHAIN_NAME 为 token 名，
 * 候选值来自 YANG 中 key-chain 的 name 叶子；以 NULL 结尾。
 */
static const struct cmd_variable_handler keychain_var_handlers[] = {
	{.varname = "key_chain", .xpath = "/ietf-key-chain:key-chains/key-chain/name" },
	{.tokenname = "KEYCHAIN_NAME", .xpath = "/ietf-key-chain:key-chains/key-chain/name" },
	{.completions = NULL}
};

/*
 * 初始化密钥链 CLI：注册变量补全处理器、安装两个命令节点及其默认命令，
 * 并把所有 keychain 相关命令挂到对应视图上。
 * 由 keychain_init / keychain_init_new 在前台进程调用。
 */
void keychain_cli_init(void)
{
	/* Register handler for keychain auto config support */
	cmd_variable_handler_register(keychain_var_handlers);
	install_node(&keychain_node);
	install_node(&keychain_key_node);

	install_default(KEYCHAIN_NODE);
	install_default(KEYCHAIN_KEY_NODE);

	install_element(CONFIG_NODE, &key_chain_cmd);
	install_element(CONFIG_NODE, &no_key_chain_cmd);
	install_element(KEYCHAIN_NODE, &key_cmd);
	install_element(KEYCHAIN_NODE, &no_key_cmd);

	install_element(KEYCHAIN_NODE, &key_chain_cmd);
	install_element(KEYCHAIN_NODE, &no_key_chain_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_string_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_string_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_chain_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_chain_cmd);

	install_element(KEYCHAIN_KEY_NODE, &key_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_key_cmd);

	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_day_month_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_day_month_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_month_day_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_month_day_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_infinite_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_infinite_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_duration_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&accept_lifetime_duration_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_accept_lifetime_cmd);

	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_day_month_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_day_month_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_month_day_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_month_day_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_infinite_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_infinite_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_duration_day_month_cmd);
	install_element(KEYCHAIN_KEY_NODE,
			&send_lifetime_duration_month_day_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_send_lifetime_cmd);
	install_element(KEYCHAIN_KEY_NODE, &cryptographic_algorithm_cmd);
	install_element(KEYCHAIN_KEY_NODE, &no_cryptographic_algorithm_cmd);
}

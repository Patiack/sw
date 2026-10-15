// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Logging - Filtered file log target
 * Copyright (C) 2019 Cumulus Networks, Inc.
 *                    Stephen Worley
 */

// ============================================================
// 【文件导读】log_filter.c
// ------------------------------------------------------------
// 职责：
//   实现"带字符串过滤的日志文件目标"(filtered-file log target)。
//   当用 CLI 配置了 `log filtered-file FILE [LEVEL]` 后，库会为它
//   建立一个专用日志目标；该目标在把消息写盘之前，先按一张全局
//   "过滤字符串表"筛选：只有"文本中包含任一过滤子串"的调试消息
//   才会被保留，其余调试消息丢弃。
//
//   注意区分两类"过滤"：
//     1. 按等级过滤(level / severity suppression)：由 log.c 与
//        zlog_target 机制按严重级阈值完成，本文件不负责(这里只在
//        fd 回调里对 >= LOG_DEBUG 的消息做字符串筛选)；
//     2. 按来源/关键字 suppression：即本文件的"过滤字符串表"，
//        常用于把海量 debug 日志收敛到关心的关键字上。
//
// 核心数据结构：
//   - zlog_filters[][]: 过滤子串二维数组，上限 ZLOG_FILTERS_MAX 条，
//     每条最长 ZLOG_FILTER_LENGTH_MAX；
//   - zlog_filter_count: 当前已登记条数；
//   - logfilterlock: 保护上述两者的互斥锁。
//
// 与其他模块的关系：
//   - log.c / log_vty.c：提供 zlog_target、zlog_fd、zlog_msg_* 接口；
//   - log_cli.c：向上提供 `log filter-text` 等命令，经 northbound
//     落到这里的 zlog_filter_add/del/clear。
//
// 阅读顺序：
//   全局状态 -> zlog_filter_add/del/clear(增删清) -> search_buf
//   (匹配) -> zlog_filterfile_fd(写盘回调，串起来) -> init/fini。
//
// 易踩的坑：
//   - zlog_filter_lookup 必须持锁调用(见其注释)；
//   - 过滤是"子串包含"，不是前缀；大小写敏感；
//   - 达到 ZLOG_FILTERS_MAX 后 add 返回非 0，上层需处理。
// ============================================================

#include <zebra.h>

// frr_pthread.h：FRR 的线程/锁辅助(frr_with_mutex 宏)。
// log.h：日志核心接口(zlog_target / zlog_fd / zlog_msg_* 等)。
#include "frr_pthread.h"
#include "log.h"

// logfilterlock: 保护过滤表(zlog_filters / zlog_filter_count)的互斥锁。
//   所有读写过滤表的操作都通过 frr_with_mutex 加锁，避免多线程竞争。
static pthread_mutex_t logfilterlock = PTHREAD_MUTEX_INITIALIZER;
// zlog_filters: 过滤子串表。每一行一个过滤字符串，行宽为
//   ZLOG_FILTER_LENGTH_MAX + 1(多留 1 字节放结尾 '\0')。
static char zlog_filters[ZLOG_FILTERS_MAX][ZLOG_FILTER_LENGTH_MAX + 1];
// zlog_filter_count: 当前已登记的有效过滤串条数(0 ~ ZLOG_FILTERS_MAX)。
static uint8_t zlog_filter_count;

/*
 * look for a match on the filter in the current filters,
 * logfilterlock must be held
 */
// zlog_filter_lookup: 在过滤表中查找 lookup。
//   线性扫描前 zlog_filter_count 条，用 strncmp(限定行宽)比较，
//   命中返回下标，未命中返回 -1。调用者必须已持有 logfilterlock。
static int zlog_filter_lookup(const char *lookup)
{
	for (int i = 0; i < zlog_filter_count; i++) {
		if (strncmp(lookup, zlog_filters[i], sizeof(zlog_filters[0]))
		    == 0)
			return i;
	}
	return -1;
}

// zlog_filter_clear: 清空整张过滤表(仅把计数置 0，不逐条清零)。
void zlog_filter_clear(void)
{
	frr_with_mutex (&logfilterlock) {
		zlog_filter_count = 0;
	}
}

// zlog_filter_add: 追加一条过滤串。
//   返回值：0 成功；1 表已满(ZLOG_FILTERS_MAX)；-1 重复或拷贝失败。
//   全程持 logfilterlock；用 strlcpy 做边界安全拷贝。
int zlog_filter_add(const char *filter)
{
	frr_with_mutex (&logfilterlock) {
		if (zlog_filter_count >= ZLOG_FILTERS_MAX)
			return 1;

		if (zlog_filter_lookup(filter) != -1)
			/* Filter already present */
			return -1;

		strlcpy(zlog_filters[zlog_filter_count], filter,
			sizeof(zlog_filters[0]));

		if (zlog_filters[zlog_filter_count][0] == '\0')
			/* Filter was either empty or didn't get copied
			 * correctly
			 */
			return -1;

		zlog_filter_count++;
	}
	return 0;
}

// zlog_filter_del: 删除一条过滤串。
//   找不到返回 -1；否则用 memmove 把后续条目整体前移一格覆盖空洞，
//   再递减计数，返回 0。全程持 logfilterlock。
int zlog_filter_del(const char *filter)
{
	frr_with_mutex (&logfilterlock) {
		int found_idx = zlog_filter_lookup(filter);
		int last_idx = zlog_filter_count - 1;

		if (found_idx == -1)
			/* Didn't find the filter to delete */
			return -1;

		/* Adjust the filter array */
		memmove(zlog_filters[found_idx], zlog_filters[found_idx + 1],
			(last_idx - found_idx) * sizeof(zlog_filters[0]));

		zlog_filter_count--;
	}
	return 0;
}

/* Dump all filters to buffer, delimited by new line */
// zlog_filter_dump: 把全部过滤串输出到 buf(每条前导一个空格、后接换行)。
//   返回写入的总字节数；若 snprintf 出错或缓冲不足则返回 -1。
//   CLI 的 `show logging` 类输出会用到它。持 logfilterlock。
int zlog_filter_dump(char *buf, size_t max_size)
{
	int len = 0;

	frr_with_mutex (&logfilterlock) {
		for (int i = 0; i < zlog_filter_count; i++) {
			int ret;

			ret = snprintf(buf + len, max_size - len, " %s\n",
				       zlog_filters[i]);
			len += ret;
			if ((ret < 0) || ((size_t)len >= max_size))
				return -1;
		}
	}

	return len;
}

// search_buf: 判断 buf(长度 len)中是否"包含"任一过滤串。
//   用 memmem 做子串匹配(可含 '\0' 的二进制安全比较)，
//   命中任一即返回 0，全部未命中返回 -1。持 logfilterlock。
static int search_buf(const char *buf, size_t len)
{
	char *found = NULL;

	frr_with_mutex (&logfilterlock) {
		for (int i = 0; i < zlog_filter_count; i++) {
			found = memmem(buf, len, zlog_filters[i],
				       strlen(zlog_filters[i]));
			if (found != NULL)
				return 0;
		}
	}

	return -1;
}

// zlog_filterfile_fd: filtered-file 日志目标的"写"回调。
//   对每条消息：
//     - 若严重级 >= LOG_DEBUG(即 debug 类)，取文本交给 search_buf，
//       只有命中过滤串才保留；
//     - 若严重级更"严重"(低于 LOG_DEBUG)，直接放行。
//   保留下来的消息收集进临时数组 msgfilt，最后一次性交给 zlog_fd
//   真正写文件。这样海量 debug 日志可按关键字收敛。
static void zlog_filterfile_fd(struct zlog_target *zt, struct zlog_msg *msgs[],
			       size_t nmsgs)
{
	struct zlog_msg *msgfilt[nmsgs];
	size_t i, o = 0;
	const char *text;
	size_t text_len;

	for (i = 0; i < nmsgs; i++) {
		if (zlog_msg_prio(msgs[i]) >= LOG_DEBUG) {
			text = zlog_msg_text(msgs[i], &text_len);
			if (search_buf(text, text_len) < 0)
				continue;
		}
		msgfilt[o++] = msgs[i];
	}

	if (o)
		zlog_fd(zt, msgfilt, o);
}

// zlog_filterfile_init: 初始化 filtered-file 目标。
//   先按普通文件目标初始化(打开文件、设缓冲区等)，再把父目标的
//   写回调 zlog_wrap 替换成 zlog_filterfile_fd，从而插入过滤逻辑。
void zlog_filterfile_init(struct zlog_cfg_filterfile *zcf)
{
	zlog_file_init(&zcf->parent);
	zcf->parent.zlog_wrap = zlog_filterfile_fd;
}

// zlog_filterfile_fini: 反初始化 filtered-file 目标，直接转调
//   普通文件目标的反初始化(zlog_file_fini)。
void zlog_filterfile_fini(struct zlog_cfg_filterfile *zcf)
{
	zlog_file_fini(&zcf->parent);
}

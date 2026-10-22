// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2017-20  David Lamparter, for NetDEF, Inc.
 */

/*
 * ============================================================================
 * xref.c —— 交叉引用（xref）元数据的运行时实现
 * ============================================================================
 *
 * xref 机制：在代码中通过宏在特定位置“打点”，每个点对应一个 struct xref
 * （编译期常量，含类型与代码位置）。链接器把所有这些 xref 指针收集到一个
 * 名为 xref_array 的段中；本文件在程序启动时遍历该段，为每个 xref 计算一个
 * 稳定的唯一 ID（UID），并建立可查询的表，供外部工具（如 xrelfo.py）或运行时
 * 按 xref 定位代码。
 *
 * 核心数据：
 *   - xref_blocks   各 DSO 的 xref_block 链表（每块含段起止指针）
 *   - xrefdata_uid  已分配 UID 的 xrefdata 红黑树（按 uid 排序）
 *
 * 关键函数：
 *   - base32()             Crockford base32 编码器，用于生成 UID 文本
 *   - xref_add_one()       为单个 xref 计算 UID 并登记
 *   - xref_block_add()     登记一个 xref_block 并处理其中所有 xref
 *   - xref_gcc_workaround() GCC 下的兜底入口
 *
 * 与其它模块关系：依赖 sha256（摘要）、jhash / hash、vty、memory；
 * 由 module.c 的 XREF_SETUP 在启动时触发。
 *
 * 阅读顺序：base32 -> xref_add_one -> xref_block_add。
 * ============================================================================
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <inttypes.h>

#include "xref.h"
#include "vty.h"
#include "jhash.h"
#include "sha256.h"
#include "memory.h"
#include "hash.h"

/*
 * xref_blocks     - 所有已登记 xref_block 的链表头。
 * xref_block_last - 链表尾指针，便于 O(1) 追加。
 */
struct xref_block *xref_blocks;
static struct xref_block **xref_block_last = &xref_blocks;

/*
 * xrefdata_uid - 存放全部已分配 UID 的 xrefdata 红黑树（唯一，按 uid 排序），
 * 供按 UID 反查 xref 使用。
 */
struct xrefdata_uid_head xrefdata_uid = INIT_RBTREE_UNIQ(xrefdata_uid);

/*
 * base32 - 把字节流按 Crockford base32 编码成 n_chars 个字符
 * @inpos   输入指针的指针（编码后前进到下一个未用字节）
 * @bitpos  当前位偏移（-1 表示起始，编码首字符时会置好填充位）
 * @out     输出缓冲（写入 n_chars 个字符并补 '\0'）
 * @n_chars 需要产生的字符数
 * 说明：函数用状态（inpos / bitpos）支持多次调用产生连续编码，用于把一个
 *       256 位摘要切成两段 UID（例如 5+5 字符）。
 */
static void base32(uint8_t **inpos, int *bitpos,
		   char *out, size_t n_chars)
{
	static const char base32ch[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

	char *opos = out;
	uint8_t *in = *inpos;
	int bp = *bitpos;

	while (opos < out + n_chars) {
		uint32_t bits = in[0] | (in[1] << 8);

		if (bp == -1)
			bits |= 0x10;
		else
			bits >>= bp;

		*opos++ = base32ch[bits & 0x1f];

		bp += 5;
		if (bp >= 8)
			in++, bp -= 8;
	}
	*opos = '\0';
	*inpos = in;
	*bitpos = bp;
}

/*
 * xref_add_one - 为一个 xref 计算并登记唯一 ID（UID）
 * @xref 待处理的交叉引用点（需带 xrefdata 且 hashstr 非空）
 * 流程：取文件名的最后一级目录名 + 文件名（如 "bgpd/bgp_route.c"），与
 *       hashstr、hashu32 一起做 SHA-256，再用 base32 取两段生成形如
 *       XXXXX-XXXXX 的 UID，最后加入 xrefdata_uid 树。
 * 说明：只取“最后两级”路径是为了便于代码移动、避免绝对路径 / 树外构建
 *       影响 UID 的稳定性。
 * 坑：xref 或 xrefdata 为空、或 hashstr 为空时直接返回（不分配 UID）。
 */
static void xref_add_one(const struct xref *xref)
{
	SHA256_CTX sha;
	struct xrefdata *xrefdata;

	const char *filename, *p, *q;
	uint8_t hash[32], *h = hash;
	uint32_t be_val;
	int bitpos;

	if (!xref || !xref->xrefdata)
		return;

	xrefdata = xref->xrefdata;
	xrefdata->xref = xref;

	if (!xrefdata->hashstr)
		return;

	/* as far as the unique ID is concerned, only use the last
	 * directory name + filename, e.g. "bgpd/bgp_route.c".  This
	 * gives a little leeway in moving things and avoids IDs being
	 * screwed up by out of tree builds or absolute pathnames.
	 */
	filename = xref->file;
	p = strrchr(filename, '/');
	if (p) {
		q = memrchr(filename, '/', p - filename);
		if (q)
			filename = q + 1;
	}

	SHA256_Init(&sha);
	SHA256_Update(&sha, filename, strlen(filename));
	SHA256_Update(&sha, xrefdata->hashstr,
		      strlen(xrefdata->hashstr));
	be_val = htonl(xrefdata->hashu32[0]);
	SHA256_Update(&sha, &be_val, sizeof(be_val));
	be_val = htonl(xrefdata->hashu32[1]);
	SHA256_Update(&sha, &be_val, sizeof(be_val));
	SHA256_Final(hash, &sha);

	bitpos = -1;
	base32(&h, &bitpos, &xrefdata->uid[0], 5);
	xrefdata->uid[5] = '-';
	base32(&h, &bitpos, &xrefdata->uid[6], 5);

	xrefdata_uid_add(&xrefdata_uid, xrefdata);
}

/*
 * xref_gcc_workaround - GCC 场景下的兜底登记入口
 * @xref 待登记的 xref
 * 作用：某些编译器（GCC C++ 等）无法靠链接器段收集 xref，改为在运行时显式
 *       调用本函数完成登记，内部直接转调 xref_add_one。
 */
void xref_gcc_workaround(const struct xref *xref)
{
	xref_add_one(xref);
}

/*
 * xref_block_add - 登记一个 xref_block 并处理其中全部 xref
 * @block 待登记的块（含 [start, stop) 的 xref 指针区间）
 * 副作用：把块接到 xref_blocks 链表尾，然后逐个调用 xref_add_one。
 * 说明：由 XREF_SETUP 生成的构造函数在程序启动时调用。
 */
void xref_block_add(struct xref_block *block)
{
	const struct xref * const *xrefp;

	*xref_block_last = block;
	xref_block_last = &block->next;

	for (xrefp = block->start; xrefp < block->stop; xrefp++)
		xref_add_one(*xrefp);
}

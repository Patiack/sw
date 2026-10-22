// SPDX-License-Identifier: GPL-2.0-or-later
/* AS path management routines.
 * Copyright (C) 1996, 97, 98, 99 Kunihiro Ishiguro
 * Copyright (C) 2005 Sun Microsystems, Inc.
 */
/* ============================================================================
 * 【文件导读】bgp_aspath.c —— AS_PATH / AS4_PATH 属性的内存表示、解析、编码、
 *                    比较（选路）与聚合
 * ============================================================================
 * 职责与定位：
 *   AS_PATH 是 BGP UPDATE 里的核心路径属性，记录一条路由沿途经过的自治系统号，
 *   其作用是 (a) 环路检测、(b) 路径长度（AS_PATH length）参与选路决策。
 *   本文件是 AS_PATH 在 bgpd 里的“唯一权威实现”，把线格式的字节流 ↔ 内存里的
 *   段链表（struct assegment 链，整体由 struct aspath 持有）双向转换，并在此之上
 *   提供比较、聚合、替换、过滤、字符串/正则匹配等全部操作。
 *
 * 两条核心数据结构（务必先建立直觉）：
 *   - struct aspath（见 bgp_aspath.h）：一条 AS_PATH 的“句柄”，含 segments 段链、
 *     缓存字符串 str（选路正则、vty 输出用）、count（跳数缓存，选路用）、refcnt。
 *   - struct assegment：一段 AS 号序列，type ∈ {AS_SET=1, AS_SEQUENCE=2,
 *     AS_CONFED_SEQUENCE=3, AS_CONFED_SET=4}，length + as[] 数组 + next 指针。
 *   AS_PATH 只允许“段与段不同类型”，同类相邻段（尤其 AS_SEQUENCE）会被
 *   assegment_normalise() 合并，以保证“同一语义的路径内存表示唯一”——这正是哈希
 *   去重（interning）能成立的前提。
 *
 * 关键路径（谁调用它 / 它调用谁）：
 *   - 解析入口 aspath_parse ← bgp_attr.c（属性解析时读 AS_PATH / AS4_PATH 线格式）。
 *   - 编码入口 aspath_put ← bgp_attr.c（属性写包时把内存段链写回线格式）。
 *   - 选路比较 aspath_cmp_left / aspath_count_hops ← bgp_route.c（bestpath/MED）。
 *   - 聚合合并 aspath_aggregate ← bgp_route.c / 本轮定义的 bgp_compute_aggregate_aspath。
 *   - 过滤/替换 ← bgp_route.c、bgp_filter.c（route-map set as-path 相关）。
 *   - aspath_cmp / aspath_key_make 作为回调注册进全局哈希表 ashash（aspath_init）。
 *
 * 【interned 共享（本文件最易误解的点）】
 *   与 struct attr 一样，aspath 也被全局哈希表 ashash 去重：内容相同的 AS_PATH 只
 *   保留一份，靠 refcnt 引用计数。aspath_intern()/aspath_parse() 命中已有项时返回
 *   已有指针并 refcnt++，aspath_unintern() 时 refcnt--，归零才真正释放。**因此被
 *   intern 的 aspath 必须视为只读**，需要修改的函数（replace/prepend/aggregate…）
 *   都先 aspath_dup() 复制出新的一份再改。
 *
 * 关键函数清单（建议阅读顺序）：
 *   1) assegment_*（底层段链表增删/复制/normalise）
 *   2) aspath_new/dup/free/intern/unintern（生命周期与引用计数）
 *   3) assegments_parse / aspath_parse（线格式 → 内存，注意 2B/4B AS 差异）
 *   4) aspath_put / assegment_data_put（内存 → 线格式，注意 AS_TRANS 替换）
 *   5) aspath_count_hops / aspath_count_confeds / aspath_count（三种计数之别）
 *   6) aspath_cmp_left / aspath_cmp_left_confed / aspath_cmp（三种“比较”用途不同）
 *   7) aspath_aggregate / aspath_merge / aspath_prepend（聚合与拼接规则）
 *   8) aspath_reg* 系列（正则匹配）/ aspath_filter* / aspath_replace*（过滤、替换）
 *
 * 相关 RFC：AS_PATH 线格式与语义 RFC4271 §4.3/§5.1.2；4 字节 AS 与 AS_TRANS
 *   RFC6793（AS4_PATH / AS4_AGGREGATOR）；BGP 联盟 confed 段 RFC5065；
 *   UPDATE 错误处理（如 AS_SET 校验）RFC7606。
 * ============================================================================
 */

#include <zebra.h>

#include "hash.h"
#include "memory.h"
#include "vector.h"
#include "log.h"
#include "stream.h"
#include "command.h"
#include "jhash.h"
#include "queue.h"
#include "filter.h"
#include "frregex_real.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_errors.h"
#include "bgpd/bgp_filter.h"

/* Attr. Flags and Attr. Type Code. */
/* ========== 0. 常量与尺寸宏（线格式 vs 内部表示） ========== */
/* 每段 AS 的头部 = 1 字节 type + 1 字节 length，共 2 字节（RFC4271 §4.3）。 */
#define AS_HEADER_SIZE 2

/* Now FOUR octets are used for AS value. */
/* 4 字节 AS：一个 AS 号在线格式里占 4 字节（as_t = uint32_t），RFC6793 起支持。 */
#define AS_VALUE_SIZE         sizeof(as_t)
/* This is the old one */
/* 2 字节 AS：老式（16bit）AS 号在线格式里占 2 字节（as16_t = uint16_t）。 */
#define AS16_VALUE_SIZE	      sizeof(as16_t)

/* Maximum protocol segment length value */
/* 一个段里最多 255 个 AS 号（length 字段只有 1 字节，RFC4271）。超长段编码时要拆开。 */
#define AS_SEGMENT_MAX		255

/* The following length and size macros relate specifically to Quagga's
 * internal representation of AS-Segments, not per se to the on-wire
 * sizes and lengths.  At present (200508) they sort of match, however
 * the ONLY functions which should now about the on-wire syntax are
 * aspath_put, assegment_put and assegment_parse.
 *
 * aspath_put returns bytes written, the only definitive record of
 * size of wire-format attribute..
 */

/* Calculated size in bytes of ASN segment data to hold N ASN's */
/* N 个 AS 号的数据区字节数：S 非 0 表示 4 字节 AS，否则 2 字节 AS。 */
#define ASSEGMENT_DATA_SIZE(N, S)                                              \
	((N) * ((S) ? AS_VALUE_SIZE : AS16_VALUE_SIZE))

/* Calculated size of segment struct to hold N ASN's */
/* 一个含 N 个 AS 号的段在线格式里的总字节数 = 2 字节头 + 数据区。 */
#define ASSEGMENT_SIZE(N,S)  (AS_HEADER_SIZE + ASSEGMENT_DATA_SIZE (N,S))

/* AS segment octet length. */
/* 取段 X 在线格式里的字节数（S 决定 2B/4B）。 */
#define ASSEGMENT_LEN(X,S) ASSEGMENT_SIZE((X)->length,S)

/* AS_SEQUENCE segments can be packed together */
/* Can the types of X and Y be considered for packing? */
/* 两段类型相同且都是 AS_SEQUENCE 时，编码时才可以“打包”进同一段（省头部开销）。 */
#define ASSEGMENT_TYPES_PACKABLE(X, Y)                                         \
	(((X)->type == (Y)->type) && ((X)->type == AS_SEQUENCE))
/* Types and length of X,Y suitable for packing? */
/* 类型可打包，且打包后总长不超过单段上限 255。 */
#define ASSEGMENTS_PACKABLE(X, Y)                                              \
	(ASSEGMENT_TYPES_PACKABLE((X), (Y))                                    \
	 && (((X)->length + (Y)->length) <= AS_SEGMENT_MAX))

/* As segment header - the on-wire representation
 * NOT the internal representation!
 */
/* 【结构】as-segment 的线格式头部（仅 2 字节），注意与内部表示 struct assegment
 * （带 next 指针、as 数组、unsigned short length）**不是同一个东西**：
 *   - struct assegment_header：只在收发线格式时临时用，字段是 uint8_t type/length。
 *   - struct assegment（定义在 bgp_aspath.h）：内部表示，length 放宽为 unsigned short
 *     （因为内部不刻意遵守 255 限制，normalise 时可能合并出更长的段）。
 * 这种“线格式 ≠ 内部表示”的区分是本文件的重要设计点。 */
struct assegment_header {
	uint8_t type;
	uint8_t length;
};

/* Hash for aspath.  This is the top level structure of AS path. */
/* 【全局】AS_PATH 去重哈希表（interning 表）。key=aspath_key_make，cmp=aspath_cmp。
 * 所有被共享的 aspath 都登记在此，用 refcnt 管理生命周期，见 aspath_intern()。 */
static struct hash *ashash;

/* Stream for SNMP. See aspath_snmp_pathseg */
/* 【全局】仅供 SNMP 复用的静态 stream：SNMP 无法自行管理存储，故用固定缓冲。 */
static struct stream *snmp_stream;

/* as-path orphan exclude list */
/* 【全局】“无主”的 as-path 排除项列表（`set as-path exclude` 尚未绑定到具体
 * peer/af 时暂存于此），见后面的 as_exclude_* 函数。 */
static struct as_list_list_head as_exclude_list_orphan;

/* ========== 1. assegment 的底层分配/释放/复制 ========== */
/* 【函数】assegment_data_new —— 分配一个能容纳 num 个 AS 号的数据数组（4 字节 AS）。
 * 只分配内存，不初始化内容（调用方负责填充）。 */
/* Callers are required to initialize the memory */
static as_t *assegment_data_new(int num)
{
	return (XMALLOC(MTYPE_AS_SEG_DATA, ASSEGMENT_DATA_SIZE(num, 1)));
}

/* 【函数】assegment_data_free —— 释放段的数据数组。 */
static void assegment_data_free(as_t *asdata)
{
	XFREE(MTYPE_AS_SEG_DATA, asdata);
}

/* 【数组】段类型 → JSON 字符串名。索引即段 type 值（0=无效占位，1..4 对应
 * AS_SET/AS_SEQUENCE/AS_CONFED_SEQUENCE/AS_CONFED_SET），供 aspath_make_str_count
 * 生成 JSON 时给段类型命名。 */
const char *const aspath_segment_type_str[] = {
	"as-invalid", "as-set", "as-sequence", "as-confed-sequence",
	"as-confed-set"
};

/* Get a new segment. Note that 0 is an allowed length,
 * and will result in a segment with no allocated data segment.
 * the caller should immediately assign data to the segment, as the segment
 * otherwise is not generally valid
 */
/* 【函数】assegment_new —— 新建一个段。
 * 参数：type 段类型（1..4）；length 段内 AS 号个数，允许为 0。
 * 返回：新段（as 数组在 length>0 时才分配，否则 as=NULL）。
 * 易误解：length=0 时返回的段“尚不完整”，调用方应尽快用 append_asns 等填入数据；
 * 这样设计的目的是支持“先建空段、再往里追加”的构造流程（聚合/排序时会用到）。 */
static struct assegment *assegment_new(uint8_t type, unsigned short length)
{
	struct assegment *new;

	new = XCALLOC(MTYPE_AS_SEG, sizeof(struct assegment));

	if (length)
		new->as = assegment_data_new(length);

	new->length = length;
	new->type = type;

	return new;
}

/* 【函数】assegment_free —— 释放单个段（含其数据数组）。
 * 注意 memset(0xfe) 是调试手法：把已释放的段填成 0xfe 便于发现 use-after-free。 */
static void assegment_free(struct assegment *seg)
{
	if (!seg)
		return;

	assegment_data_free(seg->as);
	memset(seg, 0xfe, sizeof(struct assegment));
	XFREE(MTYPE_AS_SEG, seg);

	return;
}

/* free entire chain of segments */
/* 【函数】assegment_free_all —— 沿 next 链释放整串段。 */
static void assegment_free_all(struct assegment *seg)
{
	struct assegment *prev;

	while (seg) {
		prev = seg;
		seg = seg->next;
		assegment_free(prev);
	}
}

/* Duplicate just the given assegment and its data */
/* 【函数】assegment_dup —— 深拷贝单个段（含 as 数据），next 不复制（置空）。 */
static struct assegment *assegment_dup(struct assegment *seg)
{
	struct assegment *new;

	new = assegment_new(seg->type, seg->length);
	memcpy(new->as, seg->as, ASSEGMENT_DATA_SIZE(new->length, 1));

	return new;
}

/* Duplicate entire chain of assegments, return the head */
/* 【函数】assegment_dup_all —— 深拷贝整条段链，返回新链头。
 * 用于在“只读的 interned aspath”基础上复制出一份可修改的副本（aspath_dup 调用）。 */
static struct assegment *assegment_dup_all(struct assegment *seg)
{
	struct assegment *new = NULL;
	struct assegment *head = NULL;

	while (seg) {
		if (head) {
			new->next = assegment_dup(seg);
			new = new->next;
		} else
			head = new = assegment_dup(seg);

		seg = seg->next;
	}
	return head;
}

/* prepend the as number to given segment, given num of times */
/* 【函数】assegment_prepend_asns —— 在段**头部**插入 num 个相同的 asnum。
 * 用于“向 AS_PATH 左边添加自己的 AS”（出向通告时 prepend 本 AS）。
 * 返回：可能返回原 seg（原地扩展）；num<=0 或 num>=255 时直接原样返回（不做巨量插入）。
 * 实现：新建一块更大的数组 [num 个 asnum][原 length 个 as]，释放旧数组。 */
static struct assegment *assegment_prepend_asns(struct assegment *seg,
						as_t asnum, int num)
{
	as_t *newas;
	int i;

	if (!num)
		return seg;

	if (num >= AS_SEGMENT_MAX)
		return seg; /* we don't do huge prepends */

	newas = assegment_data_new(seg->length + num);
	if (newas == NULL)
		return seg;

	for (i = 0; i < num; i++)
		newas[i] = asnum;

	memcpy(newas + num, seg->as, ASSEGMENT_DATA_SIZE(seg->length, 1));
	assegment_data_free(seg->as);
	seg->as = newas;
	seg->length += num;

	return seg;
}

/* append given array of as numbers to the segment */
/* 【函数】assegment_append_asns —— 把 asnos[0..num) 追加到段尾部。
 * 与 prepend 不同，此处**不检查**是否超过 255（内部表示允许超长段，编码时再拆）。
 * seg 为 NULL 时按原样返回（保持不变，调用方需自行处理）。 */
static struct assegment *assegment_append_asns(struct assegment *seg,
					       as_t *asnos, int num)
{
	as_t *newas;

	if (!seg)
		return seg;

	newas = XREALLOC(MTYPE_AS_SEG_DATA, seg->as,
			 ASSEGMENT_DATA_SIZE(seg->length + num, 1));

	seg->as = newas;
	memcpy(seg->as + seg->length, asnos,
	       ASSEGMENT_DATA_SIZE(num, 1));
	seg->length += num;
	return seg;
}

/* 【函数】int_cmp —— AS 号升序比较器，供 qsort 给 AS_SET 段排序（决定性）。 */
static int int_cmp(const void *p1, const void *p2)
{
	const as_t *as1 = p1;
	const as_t *as2 = p2;

	return (*as1 == *as2) ? 0 : ((*as1 > *as2) ? 1 : -1);
}

/* normalise the segment.
 * In particular, merge runs of AS_SEQUENCEs into one segment
 * Internally, we do not care about the wire segment length limit, and
 * we want each distinct AS_PATHs to have the exact same internal
 * representation - eg, so that our hashing actually works..
 */
/* 【函数】assegment_normalise —— 规范化为“唯一的内部表示”，是 interning 的前提。
 * 做两件事：
 *   1) 对 AS_SET / AS_CONFED_SET 段：先按 AS 号升序排序，再去掉重复值。
 *      因为 SET 语义上无序且可去重，排序后能让“内容等价的 SET”内存表示一致，
 *      从而哈希键一致（否则顺序不同会被当成不同路径，浪费且影响比较）。
 *   2) 把相邻且可打包的 AS_SEQUENCE 段合并成一段（内部不遵守 255 上限）。
 * 调用时机：解析完线格式后（assegments_parse 末尾）、聚合/合并后，
 * 保证进入哈希表前表示已统一。 */
static struct assegment *assegment_normalise(struct assegment *head)
{
	struct assegment *seg = head, *pin;
	struct assegment *tmp;

	if (!head)
		return head;

	while (seg) {
		pin = seg;

		/* Sort values SET segments, for determinism in paths to aid
		 * creation of hash values / path comparisons
		 * and because it helps other lesser implementations ;)
		 */
		if (seg->type == AS_SET || seg->type == AS_CONFED_SET) {
			int tail = 0;
			int i;

			qsort(seg->as, seg->length, sizeof(as_t), int_cmp);

			/* weed out dupes */
			/* 去重：tail 指向“已是唯一值”的末尾；把每个新值搬到 tail+1 处。 */
			for (i = 1; i < seg->length; i++) {
				if (seg->as[tail] == seg->as[i])
					continue;

				tail++;
				if (tail < i)
					seg->as[tail] = seg->as[i];
			}
			/* seg->length can be 0.. */
			/* 新长度 = 唯一值个数（tail+1）；length 为 0 时保持不变。 */
			if (seg->length)
				seg->length = tail + 1;
		}

		/* read ahead from the current, pinned segment while the
		 * segments
		 * are packable/mergeable. Append all following packable
		 * segments
		 * to the segment we have pinned and remove these appended
		 * segments.
		 */
		/* 向后看：只要下一个仍是可打包的 AS_SEQUENCE，就把它的内容并进 pin，
		 * 然后释放该段并让 pin->next 跳过它。循环直到遇到不同类型/不可打包的段。 */
		while (pin->next && ASSEGMENT_TYPES_PACKABLE(pin, pin->next)) {
			tmp = pin->next;
			seg = pin->next;

			/* append the next sequence to the pinned sequence */
			pin = assegment_append_asns(pin, seg->as, seg->length);

			/* bypass the next sequence */
			pin->next = seg->next;

			/* get rid of the now referenceless segment */
			assegment_free(tmp);
		}

		seg = pin->next;
	}
	return head;
}

/* ========== 2. aspath 生命周期（分配 / 释放 / 引用计数） ========== */
/* 【函数】aspath_new —— 裸分配一个空 aspath（未挂哈希、refcnt=0）。
 * 参数 asnotation 决定后续字符串表达用的是 asplain(如“65001”) 还是 asdot 等格式。
 * 此时 str/segments 均为空，需由调用方继续填充。 */
static struct aspath *aspath_new(enum asnotation_mode asnotation)
{
	struct aspath *as;

	as = XCALLOC(MTYPE_AS_PATH, sizeof(struct aspath));
	as->asnotation = asnotation;
	as->count = 0;

	return as;
}

/* Free AS path structure. */
/* 【函数】aspath_free —— 彻底释放一条 aspath（含段链、字符串缓存、JSON）。
 * 注意：**不做 refcnt 判断**，调用方必须确认“已无引用”才能调用；
 * 通常由 aspath_unintern() 在 refcnt 归零时调用，或用于尚未 intern 的临时对象。 */
void aspath_free(struct aspath *aspath)
{
	if (!aspath)
		return;
	if (aspath->segments)
		assegment_free_all(aspath->segments);
	XFREE(MTYPE_AS_STR, aspath->str);

	if (aspath->json) {
		json_object_free(aspath->json);
		aspath->json = NULL;
	}

	XFREE(MTYPE_AS_PATH, aspath);
}

/* Unintern aspath from AS path bucket. */
/* 【函数】aspath_unintern —— 解除一次 aspath 引用（引用计数减 1）。
 * 参数是二级指针：当 refcnt 归零、对象被真正释放后，会把调用方持有的指针置 NULL，
 * 避免悬垂。归零时会先从全局哈希表 ashash 里 hash_release（应命中，否则 assert），
 * 再 aspath_free。语义上与 bgp_attr_unintern 一致。 */
void aspath_unintern(struct aspath **aspath)
{
	struct aspath *ret;
	struct aspath *asp;

	if (!*aspath)
		return;

	asp = *aspath;

	if (asp->refcnt)
		asp->refcnt--;

	if (asp->refcnt == 0) {
		/* This aspath must exist in aspath hash table. */
		ret = hash_release(ashash, asp);
		assert(ret != NULL);
		aspath_free(asp);
		*aspath = NULL;
	}
}

/* Return the start or end delimiters for a particular Segment type */
/* ========== 3. 段分隔符与各种“计数/统计” ========== */
#define AS_SEG_START 0
#define AS_SEG_END 1
/* 【函数】aspath_delimiter_char —— 返回某类段在字符串表达里的起始/结束定界符。
 * 用途：aspath_make_str_count 把段链渲染成可读字符串（也是正则匹配的输入）。
 * 映射：AS_SET→{ }；AS_CONFED_SET→[ ]；AS_CONFED_SEQUENCE→( )；
 *       AS_SEQUENCE 无定界符（返回空格，调用方在 type==AS_SEQUENCE 时不会用它）。
 * which：AS_SEG_START 取起始符，AS_SEG_END 取结束符。 */
static char aspath_delimiter_char(uint8_t type, uint8_t which)
{
	int i;
	struct {
		int type;
		char start;
		char end;
	} aspath_delim_char[] = {{AS_SET, '{', '}'},
				 {AS_CONFED_SET, '[', ']'},
				 {AS_CONFED_SEQUENCE, '(', ')'},
				 {0}};

	for (i = 0; aspath_delim_char[i].type != 0; i++) {
		if (aspath_delim_char[i].type == type) {
			if (which == AS_SEG_START)
				return aspath_delim_char[i].start;
			else if (which == AS_SEG_END)
				return aspath_delim_char[i].end;
		}
	}
	return ' ';
}

/* countup asns from this segment and index onward */
/* 【函数】assegment_count_asns —— 从 seg 起统计 AS 号“总个数”（含 SET 内每个成员）。
 * from：仅对第一段生效，表示从该下标起算（用于跳过已处理的前缀）。
 * 用途：主要是给 aspath_make_str_count 估算字符串缓冲大小，不是选路计数。 */
static int assegment_count_asns(struct assegment *seg, int from)
{
	int count = 0;
	while (seg) {
		if (!from)
			count += seg->length;
		else {
			count += (seg->length - from);
			from = 0;
		}
		seg = seg->next;
	}
	return count;
}

/* 【函数】aspath_count_confeds —— 统计 confed 段的“跳数”。
 * AS_CONFED_SEQUENCE 计其长度（成员个数），AS_CONFED_SET 只计 1。
 * 用途：AS_PATH 与 AS4_PATH 做 AS4 兼容合成(aspath_reconcile_as4)、以及
 * 外发前剥离 confed 判断“hop 差”时用；**不用于普通 bestpath 比较**。 */
unsigned int aspath_count_confeds(struct aspath *aspath)
{
	int count = 0;
	struct assegment *seg = aspath->segments;

	while (seg) {
		if (seg->type == AS_CONFED_SEQUENCE)
			count += seg->length;
		else if (seg->type == AS_CONFED_SET)
			count++;

		seg = seg->next;
	}
	return count;
}

/* 【函数】aspath_count_hops —— 返回缓存的“跳数”（选路用的路径长度）。
 * 直接读 aspath->count（每次构造/修改后用 aspath_count_hops_internal 刷新）。
 * bestpath 比较 AS_PATH 长度用的就是它。 */
unsigned int aspath_count_hops(const struct aspath *aspath)
{
	return aspath->count;
}

/* 【函数】aspath_count_hops_internal —— 真正遍历计算“跳数”。
 * 规则：AS_SEQUENCE 计其长度；AS_SET 只计 1（SET 作为一个整体，无论含多少 AS）。
 * confed 段**不**计入（联盟内部 AS 不算路径长度）。
 * 结果会写入 aspath->count 供 aspath_count_hops 读取。
 * 与 aspath_count_confeds 的区别：这里数的是“有效路径长度”，排除 confed。 */
static unsigned int aspath_count_hops_internal(const struct aspath *aspath)
{
	int count = 0;
	struct assegment *seg = aspath->segments;

	while (seg) {
		if (seg->type == AS_SEQUENCE)
			count += seg->length;
		else if (seg->type == AS_SET)
			count++;

		seg = seg->next;
	}
	return count;
}

/* Check if aspath has AS_SET or AS_CONFED_SET */
/* 【函数】aspath_check_as_sets —— 路径里是否含 AS_SET / AS_CONFED_SET。
 * 用途：聚合等场景要求“没有 SET 段”时才允许某些操作；也用于 RFC7606 相关校验。 */
bool aspath_check_as_sets(struct aspath *aspath)
{
	struct assegment *seg;

	if (!aspath || !aspath->segments)
		return false;

	seg = aspath->segments;
	while (seg) {
		if (seg->type == AS_SET || seg->type == AS_CONFED_SET)
			return true;
		seg = seg->next;
	}
	return false;
}

/* Check if aspath has BGP_AS_ZERO */
/* 【函数】aspath_check_as_zero —— 路径里是否含 AS 0（非法值，RFC7606 视为错误）。 */
bool aspath_check_as_zero(struct aspath *aspath)
{
	struct assegment *seg = aspath->segments;
	unsigned int i;

	while (seg) {
		for (i = 0; i < seg->length; i++)
			if (seg->as[i] == BGP_AS_ZERO)
				return true;
		seg = seg->next;
	}

	return false;
}

/* Estimate size aspath /might/ take if encoded into an
 * ASPATH attribute.
 *
 * This is a quick estimate, not definitive! aspath_put()
 * may return a different number!!
 */
/* 【函数】aspath_size —— 粗估编码成 AS_PATH 属性后的字节数（含每段 2 字节头）。
 * 注意：这只是估算（假定 4 字节 AS），aspath_put 打包后可能不同。
 * 用途：发包前预判属性长度。 */
unsigned int aspath_size(struct aspath *aspath)
{
	int size = 0;
	struct assegment *seg = aspath->segments;

	while (seg) {
		size += ASSEGMENT_SIZE(seg->length, 1);
		seg = seg->next;
	}
	return size;
}

/* Return highest public ASN in path */
/* 【函数】aspath_highest —— 路径中最大的“公网”AS 号（忽略私有 AS）。
 * 用途：把私有 AS 替换/去私有时，需要一个稳定的代表 AS。 */
as_t aspath_highest(struct aspath *aspath)
{
	struct assegment *seg = aspath->segments;
	as_t highest = 0;
	unsigned int i;

	while (seg) {
		for (i = 0; i < seg->length; i++)
			if (seg->as[i] > highest
			    && !BGP_AS_IS_PRIVATE(seg->as[i]))
				highest = seg->as[i];
		seg = seg->next;
	}
	return highest;
}

/* Return the left-most ASN in path */
/* 【函数】aspath_leftmost —— 最左边的 AS 号。
 * 仅当**第一段是 AS_SEQUENCE** 时取 seg->as[0]；否则返回 0
 * （例如第一段是 AS_SET 或 confed 段，没有唯一的“最左 AS”）。
 * 注意与 aspath_cmp_left 区别：后者用于选路 MED 比较，语义更细。 */
as_t aspath_leftmost(struct aspath *aspath)
{
	struct assegment *seg = aspath->segments;
	as_t leftmost = 0;

	if (seg && seg->length && seg->type == AS_SEQUENCE)
		leftmost = seg->as[0];

	return leftmost;
}

/* Return 1 if there are any 4-byte ASes in the path */
/* 【函数】aspath_has_as4 —— 路径里是否存在 >65535 的 4 字节 AS 号。
 * 用途：判断向老式(16bit)邻居发送时是否需要走 AS_TRANS/AS4_PATH 兼容路径。 */
bool aspath_has_as4(struct aspath *aspath)
{
	struct assegment *seg = aspath->segments;
	unsigned int i;

	while (seg) {
		for (i = 0; i < seg->length; i++)
			if (seg->as[i] > BGP_AS_MAX)
				return true;
		seg = seg->next;
	}
	return false;
}

/* ========== 4. 字符串 / JSON 表达生成，以及 interned 共享 ========== */
/* Convert aspath structure to string expression. */
/* 【函数】aspath_make_str_count —— 把段链渲染成字符串（并可选生成 JSON）。
 * 产物写入 as->str / as->str_len，JSON 写入 as->json。
 * 该字符串是“给人看 + 给正则匹配”的规范形式，例如：“65001 65002 {65003,65004}”。
 * 注意：这里**不**更新 as->count（跳数），调用方负责另设。
 * 关键点：
 *   - 空路径（无段）：字符串为空串，但 JSON 里标成 "Local"（本地起源路径）。
 *   - 字符串缓冲先按“AS 数 × 每 AS 最大字符数”粗估，再按需 realloc（下方宏）。
 *   - 两种分隔符：AS_SET/AS_CONFED_SET 用逗号；AS_SEQUENCE/AS_CONFED_SEQUENCE 用空格。
 *   - 只对非 AS_SEQUENCE 段加定界符（{}/[]/()）。
 * make_json 为 true 时同时生成 as->json（供 SNMP/show json 输出）。 */
static void aspath_make_str_count(struct aspath *as, bool make_json)
{
	struct assegment *seg;
	int str_size;
	int len = 0;
	char *str_buf;
	json_object *jaspath_segments = NULL;
	json_object *jseg = NULL;
	json_object *jseg_list = NULL;

	if (make_json) {
		as->json = json_object_new_object();
		jaspath_segments = json_object_new_array();
	}

	/* Empty aspath. */
	/* 空 AS_PATH：iBGP 起源或本地生成，字符串为空，长度 0。 */
	if (!as->segments) {
		if (make_json) {
			json_object_string_add(as->json, "string", "Local");
			json_object_object_add(as->json, "segments",
					       jaspath_segments);
			json_object_int_add(as->json, "length", 0);
		}
		as->str = XMALLOC(MTYPE_AS_STR, 1);
		as->str[0] = '\0';
		as->str_len = 0;
		return;
	}

	seg = as->segments;

/* ASN takes 5 to 10 chars plus separator, see below.
 * If there is one differing segment type, we need an additional
 * 2 chars for segment delimiters, and the final '\0'.
 * Hopefully this is large enough to avoid hitting the realloc
 * code below for most common sequences.
 *
 * This was changed to 10 after the well-known BGP assertion, which
 * had hit some parts of the Internet in May of 2009.
 * plain format : '4294967295 ' : 10 + 1
 * astod format : '65535.65535 ': 11 + 1
 */
/* 单个 AS 号在字符串里的最大字节数（含分隔符）：asdot 形式 "65535.65535 " 为 11(+1)。 */
#define ASN_STR_LEN (11 + 1)
	str_size = MAX(assegment_count_asns(seg, 0) * ASN_STR_LEN + 2 + 1,
		       ASPATH_STR_DEFAULT_LEN);
	str_buf = XMALLOC(MTYPE_AS_STR, str_size);

	while (seg) {
		int i;
		char separator;

		/* Check AS type validity. Set separator for segment */
		/* 依据段类型选分隔符；同时校验类型合法性（非法则清理并放弃生成）。 */
		switch (seg->type) {
		case AS_SET:
		case AS_CONFED_SET:
			separator = ',';
			break;
		case AS_SEQUENCE:
		case AS_CONFED_SEQUENCE:
			separator = ' ';
			break;
		default:
			/* 非法 type：释放已分配缓冲与 JSON，置空返回。 */
			XFREE(MTYPE_AS_STR, str_buf);
			as->str = NULL;
			as->str_len = 0;
			json_object_free(as->json);
			as->json = NULL;
			json_object_free(jaspath_segments);
			return;
		}

/* We might need to increase str_buf, particularly if path has
 * differing segments types, our initial guesstimate above will
 * have been wrong. Need 11 chars for ASN, a separator each and
 * potentially two segment delimiters, plus a space between each
 * segment and trailing zero.
 *
 * This definitely didn't work with the value of 5 bytes and
 * 32-bit ASNs.
 */
/* 本段字符串所需字节数上界：段内 AS 数 × 每 AS 最大长 + 两个定界符 + 段间空格 + '\0'。 */
#define SEGMENT_STR_LEN(X) (((X)->length * ASN_STR_LEN) + 2 + 1 + 1)
		if ((len + SEGMENT_STR_LEN(seg)) > str_size) {
			str_size = len + SEGMENT_STR_LEN(seg);
			str_buf = XREALLOC(MTYPE_AS_STR, str_buf, str_size);
		}
#undef ASN_STR_LEN
#undef SEGMENT_STR_LEN

		/* 非 AS_SEQUENCE 段：先输出起始定界符（如 '{'）。 */
		if (seg->type != AS_SEQUENCE)
			len += snprintf(
				str_buf + len, str_size - len, "%c",
				aspath_delimiter_char(seg->type, AS_SEG_START));

		if (make_json)
			jseg_list = json_object_new_array();

		/* write out the ASNs, with their separators, bar the last one*/
		/* 逐个输出 AS 号，最后一个后面不加分隔符。 */
		for (i = 0; i < seg->length; i++) {
			if (make_json)
				asn_asn2json_array(jseg_list, seg->as[i],
						   as->asnotation);
			len += snprintfrr(str_buf + len, str_size - len,
					  ASN_FORMAT(as->asnotation),
					  &seg->as[i]);

			if (i < (seg->length - 1))
				len += snprintf(str_buf + len, str_size - len,
						"%c", separator);
		}

		if (make_json) {
			/* 每段在 JSON 里表示为一个对象：type 名 + as 列表。 */
			jseg = json_object_new_object();
			json_object_string_add(
				jseg, "type",
				aspath_segment_type_str[seg->type]);
			json_object_object_add(jseg, "list", jseg_list);
			json_object_array_add(jaspath_segments, jseg);
		}

		/* 非 AS_SEQUENCE 段：输出结束定界符（如 '}'）。 */
		if (seg->type != AS_SEQUENCE)
			len += snprintf(
				str_buf + len, str_size - len, "%c",
				aspath_delimiter_char(seg->type, AS_SEG_END));
		/* 段与段之间用一个空格分隔。 */
		if (seg->next)
			len += snprintf(str_buf + len, str_size - len, " ");

		seg = seg->next;
	}

	assert(len < str_size);

	str_buf[len] = '\0';
	as->str = str_buf;
	as->str_len = len;

	if (make_json) {
		/* JSON 顶层：string（完整字符串）、segments（数组）、length（跳数）。 */
		json_object_string_add(as->json, "string", str_buf);
		json_object_object_add(as->json, "segments", jaspath_segments);
		json_object_int_add(as->json, "length", aspath_count_hops(as));
	}

	return;
}

/* 【函数】aspath_str_update —— 重建 aspath 的字符串/JSON 缓存。
 * 先释放旧的 str 与 json，再调用 aspath_make_str_count 重新生成。
 * make_json=false 时只生成字符串（大多数内部调用如此，省内存）。
 * 注意：**任何修改 segments 之后都必须调用它**刷新缓存，否则 str 与实际不符
 * （aspath_key_make 依赖 str 计算哈希，会出错）。 */
void aspath_str_update(struct aspath *as, bool make_json)
{
	XFREE(MTYPE_AS_STR, as->str);

	if (as->json) {
		json_object_free(as->json);
		as->json = NULL;
	}

	aspath_make_str_count(as, make_json);
}

/* Intern allocated AS path. */
/* 【函数】aspath_intern —— 把一条 aspath 加入全局去重哈希表并返回“权威副本”。
 * 前提：aspath 尚未 intern（refcnt==0）且已生成字符串表示（str != NULL，即哈希键可用）。
 * 流程：hash_get 查表——命中则释放传入的临时对象、返回已有对象；未命中则新登记。
 *       无论哪种，都对返回对象 refcnt++。
 * 返回：指向共享对象的指针（调用方以后要用 unintern 释放这一次引用）。 */
struct aspath *aspath_intern(struct aspath *aspath)
{
	struct aspath *find;

	/* Assert this AS path structure is not interned and has the string
	   representation built. */
	assert(aspath->refcnt == 0);
	assert(aspath->str);

	/* Check AS path hash. */
	find = hash_get(ashash, aspath, hash_alloc_intern);
	if (find != aspath)
		aspath_free(aspath);

	find->refcnt++;

	return find;
}

/* Duplicate aspath structure.  Created same aspath structure but
   reference count and AS path string is cleared. */
/* 【函数】aspath_dup —— 复制一条 aspath，返回一份**独立（未 intern、refcnt=0）**的副本。
 * 段链深拷贝（assegment_dup_all）；若原对象有 str 也一并复制。json 置空（按需重建）。
 * 用途：所有要“改内容”的操作都先 dup 再改，避免污染被 interned 的共享对象。 */
struct aspath *aspath_dup(struct aspath *aspath)
{
	unsigned short buflen = aspath->str_len + 1;
	struct aspath *new;

	new = XCALLOC(MTYPE_AS_PATH, sizeof(struct aspath));
	new->json = NULL;

	if (aspath->segments)
		new->segments = assegment_dup_all(aspath->segments);

	if (!aspath->str)
		return new;

	new->str = XMALLOC(MTYPE_AS_STR, buflen);
	new->str_len = aspath->str_len;
	new->asnotation = aspath->asnotation;

	/* copy the string data */
	if (aspath->str_len > 0)
		memcpy(new->str, aspath->str, buflen);
	else
		new->str[0] = '\0';

	new->count = aspath->count;
	return new;
}

/* 【函数】aspath_hash_alloc —— hash_get 未命中时的回调：为哈希表“造”一个 held 对象。
 * 它**不做深拷贝**：直接把调用方临时对象里的 segments/str/json 指针移交给新对象
 * （即“窃取”所有权），因此这里不是复制而是承接。refcnt 从 0 起。
 * 这也是为什么 aspath_parse 命中已存在项时要自行释放临时对象的内存，而未命中时
 * 这些内存就归哈希表里的对象所有。 */
static void *aspath_hash_alloc(void *arg)
{
	const struct aspath *aspath = arg;
	struct aspath *new;

	/* Malformed AS path value. */
	assert(aspath->str);

	/* New aspath structure is needed. */
	new = XMALLOC(MTYPE_AS_PATH, sizeof(struct aspath));

	/* Reuse segments and string representation */
	new->refcnt = 0;
	new->segments = aspath->segments;
	new->str = aspath->str;
	new->str_len = aspath->str_len;
	new->json = aspath->json;
	new->asnotation = aspath->asnotation;
	new->count = aspath->count;

	return new;
}

/* ========== 5. 线格式 → 内存：解析 AS_PATH / AS4_PATH ========== */
/* parse as-segment byte stream in struct assegment */
/* 【函数】assegments_parse —— 把 AS_PATH 属性的字节流解析成段链。
 * 参数：s 输入流；length 属性值长度（字节）；result 出参（段链头）；use32bit 为真
 *   表示按 4 字节 AS 解析（AS4_PATH / 4 字节能力协商后），否则按 2 字节解析。
 * 返回：0 成功；-1 格式错误（并已释放已分配的部分段）。
 * 线格式：重复 [type(1B) length(1B) as[length]*(2B 或 4B)]。
 * 关键校验：整体可读、段未越界、length 非 0、不超 255、type 合法。
 * 完成后调用 assegment_normalise 统一内部表示（供哈希去重）。
 * 注意：空 length（=0）直接返回 0 且 *result 未被赋值（保持调用方原值）——
 *   即“空 AS_PATH”（iBGP/本地起源的合法情形）。 */
static int assegments_parse(struct stream *s, size_t length,
			    struct assegment **result, int use32bit)
{
	struct assegment_header segh;
	struct assegment *seg, *prev = NULL, *head = NULL;
	size_t bytes = 0;

	/* empty aspath (ie iBGP or somesuch) */
	if (length == 0)
		return 0;

	if (BGP_DEBUG(as4, AS4_SEGMENT))
		zlog_debug(
			"[AS4SEG] Parse aspath segment: got total byte length %lu",
			(unsigned long)length);
	/* basic checks */
	/* 基本校验：流里剩余可读字节要够 length；至少能读下一个段头；length 必须偶数
	 * （因为 AS 号以 2 字节为单位）。 */
	if ((STREAM_READABLE(s) < length)
	    || (STREAM_READABLE(s) < AS_HEADER_SIZE)
	    || (length % AS16_VALUE_SIZE))
		return -1;

	while (bytes < length) {
		int i;
		size_t seg_size;

		/* 剩余字节已不足以容纳一个段头（2 字节）→ 畸形。 */
		if ((length - bytes) <= AS_HEADER_SIZE) {
			if (head)
				assegment_free_all(head);
			return -1;
		}

		/* softly softly, get the header first on its own */
		/* 先单独读 2 字节段头（type、length），再据 length 决定读多少数据。 */
		segh.type = stream_getc(s);
		segh.length = stream_getc(s);

		seg_size = ASSEGMENT_SIZE(segh.length, use32bit);

		if (BGP_DEBUG(as4, AS4_SEGMENT))
			zlog_debug(
				"[AS4SEG] Parse aspath segment: got type %d, length %d",
				segh.type, segh.length);

		/* check it.. */
		/* 段级校验：本段是否越界、length 是否非 0、是否超过 255 上限。 */
		if (((bytes + seg_size) > length)
		    /* 1771bis 4.3b: seg length contains one or more */
		    || (segh.length == 0)
		    /* Paranoia in case someone changes type of segment length.
		     * Shift both values by 0x10 to make the comparison operate
		     * on more, than 8 bits (otherwise it's a warning, bug
		     * #564).
		     */
		    || ((sizeof(segh.length) > 1)
			&& (0x10 + segh.length > 0x10 + AS_SEGMENT_MAX))) {
			if (head)
				assegment_free_all(head);
			return -1;
		}

		/* type 必须在已知四类之内，否则畸形。 */
		switch (segh.type) {
		case AS_SEQUENCE:
		case AS_SET:
		case AS_CONFED_SEQUENCE:
		case AS_CONFED_SET:
			break;
		default:
			if (head)
				assegment_free_all(head);
			return -1;
		}

		/* now its safe to trust lengths */
		/* 校验通过，创建段并挂到链尾。 */
		seg = assegment_new(segh.type, segh.length);

		if (head)
			prev->next = seg;
		else /* it's the first segment */
			head = seg;

		/* 按 use32bit 读每个 AS：4 字节用 stream_getl，2 字节用 stream_getw。 */
		for (i = 0; i < segh.length; i++)
			seg->as[i] =
				(use32bit) ? stream_getl(s) : stream_getw(s);

		bytes += seg_size;

		if (BGP_DEBUG(as4, AS4_SEGMENT))
			zlog_debug(
				"[AS4SEG] Parse aspath segment: Bytes now: %lu",
				(unsigned long)bytes);

		prev = seg;
	}

	/* 规范化后交给调用方（排序 SET、合并相邻 SEQUENCE）。 */
	*result = assegment_normalise(head);
	return 0;
}

/* AS path parse function.  pnt is a pointer to byte stream and length
   is length of byte stream.  If there is same AS path in the the AS
   path hash then return it else make new AS path structure.

   On error NULL is returned.
 */
/* 【函数】aspath_parse —— AS_PATH 解析的对外入口。
 * 参数：s 输入流；length 字节数；use32bit 是否 4 字节 AS；asnotation 字符串格式。
 * 返回：指向 **interned** aspath 的指针（refcnt 已 +1）；出错返回 NULL。
 * 流程：校验长度 → assegments_parse 建临时段链 → 算跳数 → hash_get 去重。
 *   - 命中已有对象：释放临时段链/字符串/json（这些内存本属于临时对象）。
 *   - 未命中：hash_alloc 把临时对象的指针“过继”给哈希表里新建的对象。
 * 最后 refcnt++ 并返回。调用方最终需 aspath_unintern 释放引用。
 * 这是 bgp_attr.c 解析 AS_PATH/AS4_PATH 时进入本文件的主路径。 */
struct aspath *aspath_parse(struct stream *s, size_t length, int use32bit,
			    enum asnotation_mode asnotation)
{
	struct aspath as;
	struct aspath *find;

	/* If length is odd it's malformed AS path. */
	/* Nit-picking: if (use32bit == 0) it is malformed if odd,
	 * otherwise its malformed when length is larger than 2 and (length-2)
	 * is not dividable by 4.
	 * But... this time we're lazy
	 */
	/* 长度必须偶数（AS 以 2 字节为基本单位）。 */
	if (length % AS16_VALUE_SIZE)
		return NULL;

	/* 在栈上构造一个临时 aspath 骨架，asnotation 直接设好。 */
	memset(&as, 0, sizeof(as));
	as.asnotation = asnotation;
	if (assegments_parse(s, length, &as.segments, use32bit) < 0)
		return NULL;

	/* 刷新跳数缓存（选路用）。 */
	as.count = aspath_count_hops_internal(&as);

	/* If already same aspath exist then return it. */
	find = hash_get(ashash, &as, aspath_hash_alloc);

	/* if the aspath was already hashed free temporary memory. */
	/* 命中已有对象（refcnt>0）：临时对象的内存已被“复用”，需释放段链/str/json。 */
	if (find->refcnt) {
		assegment_free_all(as.segments);
		/* aspath_key_make() always updates the string */
		XFREE(MTYPE_AS_STR, as.str);
		if (as.json) {
			json_object_free(as.json);
			as.json = NULL;
		}
	}

	find->refcnt++;

	return find;
}

/* ========== 6. 内存 → 线格式：编码 AS_PATH，及向路径两端添加 AS ========== */
/* Add specified AS to the rightmost of aspath. */
/* 【函数】aspath_add_asns_rightmost —— 在路径**最右端**追加 num 个 asno（type 指定段类型）。
 * 用于聚合时把“剩余信息”追加到结果尾部（aspath_aggregate 调用）。
 * 逻辑：空路径→新建段；否则找到最后一段，若类型匹配且不超 255 则直接扩展，否则新起一段。
 * 注意：这是**原地**修改 aspath（调用方须保证它是可改的非共享对象）。
 * 最后刷新字符串与跳数缓存。 */
static struct aspath *aspath_add_asns_rightmost(struct aspath *aspath, as_t asno, uint8_t type,
						unsigned int num)
{
	struct assegment *seg = aspath->segments;
	struct assegment *last = NULL;

	if (!seg) {
		/* 路径为空：直接建一个新段。 */
		struct assegment *newseg = assegment_new(type, num);
		unsigned int i;

		for (i = 0; i < num; i++)
			newseg->as[i] = asno;

		aspath->segments = newseg;
	} else {
		while (seg) {
			last = seg;
			seg = seg->next;
		}

		/* 末段类型相同且合并后不超 255：就地扩展；否则新起一段挂到末尾。 */
		if (last->type == type && (last->length + num) <= AS_SEGMENT_MAX) {
			assegment_append_asns(last, &asno, num);
		} else {
			struct assegment *newseg = assegment_new(type, num);
			unsigned int i;

			for (i = 0; i < num; i++)
				newseg->as[i] = asno;

			last->next = newseg;
		}
	}

	aspath_str_update(aspath, false);
	aspath->count = aspath_count_hops_internal(aspath);
	return aspath;
}

/* 【函数】assegment_data_put —— 把一个段的 AS 数组写进流。
 * use32bit：写 4 字节 AS（stream_putl）；否则写 2 字节 AS（stream_putw）。
 * 关键（RFC6793）：2 字节模式下，若 AS 号 > 65535，不能直接写，必须写
 *   **AS_TRANS（23456）**占位——真正的 4 字节值由 AS4_PATH 属性携带。
 * assert(num <= 255)：编码时单段不得超长（超长段在 aspath_put 里已先拆开）。 */
static void assegment_data_put(struct stream *s, as_t *as, int num,
			       int use32bit)
{
	int i;

	assert(num <= AS_SEGMENT_MAX);

	for (i = 0; i < num; i++)
		if (use32bit)
			stream_putl(s, as[i]);
		else {
			if (as[i] <= BGP_AS_MAX)
				stream_putw(s, as[i]);
			else
				stream_putw(s, BGP_AS_TRANS);
		}
}

/* 【函数】assegment_header_put —— 写 2 字节段头，返回“length 字段”在流中的位置。
 * 返回 lenp 是为了后续可回填长度（打包相邻 SEQUENCE 后需更新本段长度）。 */
static size_t assegment_header_put(struct stream *s, uint8_t type, int length)
{
	size_t lenp;
	assert(length <= AS_SEGMENT_MAX);
	stream_putc(s, type);
	lenp = stream_get_endp(s);
	stream_putc(s, length);
	return lenp;
}

/* write aspath data to stream */
/* 【函数】aspath_put —— 把 aspath 编码写入流（AS_PATH 属性的值部分）。
 * 返回实际写入字节数（是确定值，不像 aspath_size 只是估算）。
 * use32bit 决定 2B/4B 编码（4B 时 >65535 直接写；2B 时超范围写 AS_TRANS）。
 * 说明：
 *   - 空段/空路径返回 0。
 *   - 内部段可能比 255 长（normalise 会合并），编码时须拆成多个 ≤255 的段。
 *   - 相邻的 AS_SEQUENCE 段会“打包”进同一段头以省空间（用 lenp 回填长度）。
 *   - 循环条件要求本段编码后不超流剩余空间，否则停止（避免写半截）。
 * 调用方：bgp_attr.c 打 AS_PATH 属性时。 */
size_t aspath_put(struct stream *s, struct aspath *as, int use32bit)
{
	struct assegment *seg = as->segments;
	size_t bytes = 0;

	if (!seg || seg->length == 0)
		return 0;

	/*
	 * Hey, what do we do when we have > STREAM_WRITABLE(s) here?
	 * At the moment, we would write out a partial aspath, and our
	 * peer
	 * will complain and drop the session :-/
	 *
	 * The general assumption here is that many things tested will
	 * never happen.  And, in real live, up to now, they have not.
	 */
	while (seg && (ASSEGMENT_LEN(seg, use32bit) <= STREAM_WRITEABLE(s))) {
		struct assegment *next = seg->next;
		int written = 0;
		int asns_packed = 0;
		size_t lenp;

		/* Overlength segments have to be split up */
		/* 段内 AS 数 > 255：先按 255 一块块写出，直到剩余不足 255。 */
		while ((seg->length - written) > AS_SEGMENT_MAX) {
			assegment_header_put(s, seg->type, AS_SEGMENT_MAX);
			assegment_data_put(s, (seg->as + written),
					   AS_SEGMENT_MAX, use32bit);
			written += AS_SEGMENT_MAX;
			bytes += ASSEGMENT_SIZE(AS_SEGMENT_MAX, use32bit);
		}

		/* write the final segment, probably is also the first
		 */
		/* 写出最后（也可能就是唯一）一块，长度 = length - written。 */
		lenp = assegment_header_put(s, seg->type,
					    seg->length - written);
		assegment_data_put(s, (seg->as + written),
				   seg->length - written, use32bit);

		/* Sequence-type segments can be 'packed' together
		 * Case of a segment which was overlength and split up
		 * will be missed here, but that doesn't matter.
		 */
		/* 把后续可打包的 AS_SEQUENCE 段内容并进当前段，并回填段头长度。 */
		while (next && ASSEGMENTS_PACKABLE(seg, next)) {
			/* NB: We should never normally get here given
			 * we
			 * normalise aspath data when parse them.
			 * However, better
			 * safe than sorry. We potentially could call
			 * assegment_normalise here instead, but it's
			 * cheaper and
			 * easier to do it on the fly here rather than
			 * go through
			 * the segment list twice every time we write
			 * out
			 * aspath's.
			 */

			/* Next segment's data can fit in this one */
			assegment_data_put(s, next->as, next->length, use32bit);

			/* update the length of the segment header */
			stream_putc_at(s, lenp,
				       seg->length - written + next->length);
			asns_packed += next->length;

			next = next->next;
		}

		bytes += ASSEGMENT_SIZE(seg->length - written + asns_packed,
					use32bit);
		seg = next;
	}
	return bytes;
}

/* This is for SNMP BGP4PATHATTRASPATHSEGMENT
 * We have no way to manage the storage, so we use a static stream
 * wrapper around aspath_put.
 */
/* 【函数】aspath_snmp_pathseg —— 为 SNMP 提供路径段字节（包装 aspath_put）。
 * 因 SNMP 接口无法自行管理内存，这里用全局静态 snmp_stream（最多 1024 字节）。
 * 返回指向该静态缓冲的指针，*varlen 为有效长度。注意：**始终按 2 字节 AS 编码**
 * （use32bit=0），仅供 SNMP 老接口使用。 */
uint8_t *aspath_snmp_pathseg(struct aspath *as, size_t *varlen)
{
#define SNMP_PATHSEG_MAX 1024

	if (!snmp_stream)
		snmp_stream = stream_new(SNMP_PATHSEG_MAX);
	else
		stream_reset(snmp_stream);

	if (!as) {
		*varlen = 0;
		return NULL;
	}
	aspath_put(snmp_stream, as, 0); /* use 16 bit for now here */

	*varlen = stream_get_endp(snmp_stream);
	return stream_pnt(snmp_stream);
}

/* Modify as1 using as2 for aggregation. */
/* 【函数】aspath_aggregate —— 聚合两条 AS_PATH，求“合并后”的路径（RFC4271 §9.2.2.2 / §5.1.2）。
 * 参数：as1、as2 待合并的两条路径（只读，函数**不**改它们）。
 * 返回：一条**新** aspath（未 intern，调用方负责 intern 或 free）。
 * 规则（聚合的本质是“保留公共前缀 + 把差异压成 AS_SET”）：
 *   1) 从头逐段比较，找出两条路径**公共的前导**（相同 type、逐 AS 相同），原样保留为
 *      SEQUENCE 前缀。
 *   2) 从第一个不同点开始，把 as1 剩余的所有 AS、as2 剩余的所有 AS 分别用
 *      aspath_add_asns_rightmost 追加为 **AS_SET** 段（每个 AS 单独一段，随后被
 *      normalise 合并排序去重成一个 SET）。
 *   3) 若遇到 confed 段（AS_CONFED_SEQUENCE/SET），差异部分用 **AS_CONFED_SET** 收纳，
 *      以保留联盟语义。
 * 这就是“聚合路由的 AS_PATH 为什么会变成 { a,b,c } 形式”的原因——产生 AS_SET 的
 * 条件正是“两条明细路由在某个前缀之后出现了分歧”。
 * 易误解点：match 变量在段切换时的语义（本段内已匹配的 AS 数）；from 取 match 表示
 * 从“首个分歧点”开始才纳入 SET。 */
struct aspath *aspath_aggregate(struct aspath *as1, struct aspath *as2)
{
	int i;
	int minlen = 0;
	int match = 0;
	int from;
	struct assegment *seg1 = as1->segments;
	struct assegment *seg2 = as2->segments;
	struct aspath *aspath = NULL;
	struct assegment *prevseg = NULL;

	/* First of all check common leading sequence. */
	/* 阶段一：抽取两条路径的公共前导段。 */
	while (seg1 && seg2) {
		/* Check segment type. */
		if (seg1->type != seg2->type)
			break;

		/* Minimum segment length. */
		minlen = MIN(seg1->length, seg2->length);

		/* 逐 AS 比较，match = 前面连续相同的 AS 个数。 */
		for (match = 0; match < minlen; match++)
			if (seg1->as[match] != seg2->as[match])
				break;

		/* 若有相同前缀，原样复制成一个段挂到结果链。 */
		if (match) {
			struct assegment *seg = assegment_new(seg1->type, 0);

			seg = assegment_append_asns(seg, seg1->as, match);

			if (!aspath) {
				aspath = aspath_new(as1->asnotation);
				aspath->segments = seg;
			} else
				prevseg->next = seg;

			prevseg = seg;
		}

		/* 段内没比完、或长度不等 → 后面不再有公共前导，停。 */
		if (match != minlen || match != seg1->length
		    || seg1->length != seg2->length)
			break;
		/* We are moving on to the next segment to reset match */
		else
			match = 0;

		seg1 = seg1->next;
		seg2 = seg2->next;
	}

	if (!aspath)
		aspath = aspath_new(as1->asnotation);

	/* Make as-set using rest of all information. */
	/* 阶段二：as1 的剩余（从分歧点 from=match 起）全部并入 AS_SET。
	 * confed 段则并成 AS_CONFED_SET。 */
	from = match;
	while (seg1) {
		for (i = from; i < seg1->length; i++) {
			if (seg1->type == AS_CONFED_SEQUENCE || seg1->type == AS_CONFED_SET) {
				aspath_add_asns_rightmost(aspath, seg1->as[i], AS_CONFED_SET, 1);
				continue;
			}
			aspath_add_asns_rightmost(aspath, seg1->as[i], AS_SET, 1);
		}

		from = 0;
		seg1 = seg1->next;
	}

	/* 阶段三：as2 的剩余同样并入 AS_SET / AS_CONFED_SET。 */
	from = match;
	while (seg2) {
		for (i = from; i < seg2->length; i++) {
			if (seg2->type == AS_CONFED_SEQUENCE || seg2->type == AS_CONFED_SET) {
				aspath_add_asns_rightmost(aspath, seg2->as[i], AS_CONFED_SET, 1);
				continue;
			}
			aspath_add_asns_rightmost(aspath, seg2->as[i], AS_SET, 1);
		}

		from = 0;
		seg2 = seg2->next;
	}

	/* 规范化（SET 排序去重、合并相邻 SEQUENCE）并刷新字符串/跳数缓存。 */
	assegment_normalise(aspath->segments);
	aspath_str_update(aspath, false);
	aspath->count = aspath_count_hops_internal(aspath);

	return aspath;
}

/* ========== 7. 首/末 AS 查询与 AS 环路检测 ========== */
/* When a BGP router receives an UPDATE with an MP_REACH_NLRI
   attribute, check the leftmost AS number in the AS_PATH attribute is
   or not the peer's AS number. */
/* 【函数】aspath_firstas_check —— 第一段是 AS_SEQUENCE 且其首个 AS == asno。
 * 用途：收 UPDATE 时校验对端是否把自己的 AS 放在路径最左（防伪造 / 环路）。
 * 注意：首段若是 AS_SET / confed 段则不满足（返回 false）。 */
bool aspath_firstas_check(struct aspath *aspath, as_t asno)
{
	if ((aspath == NULL) || (aspath->segments == NULL))
		return false;

	if (aspath->segments && (aspath->segments->type == AS_SEQUENCE)
	    && (aspath->segments->as[0] == asno))
		return true;

	return false;
}

/* 【函数】aspath_get_first_as —— 取首段第一个 AS。
 * 注意：**不检查首段类型**，直接返回 segments->as[0]；空路径返回 0。
 * 若无条件调用需先确认路径非空（这是与 aspath_leftmost 的差异）。 */
unsigned int aspath_get_first_as(struct aspath *aspath)
{
	if (aspath == NULL || aspath->segments == NULL)
		return 0;

	return aspath->segments->as[0];
}

/* 【函数】aspath_get_last_as —— 取路径“最后一个 AS”。
 * 遍历所有 SEQUENCE / AS_CONFED_SEQUENCE 段，取每段最后一个元素，最终得到最右 AS。
 * 空路径返回 0。用于 `bgp bestpath peer-type` 等判断对端 AS。 */
unsigned int aspath_get_last_as(struct aspath *aspath)
{
	int i;
	unsigned int last_as = 0;
	const struct assegment *seg;

	if (aspath == NULL || aspath->segments == NULL)
		return last_as;

	seg = aspath->segments;

	while (seg) {
		if (seg->type == AS_SEQUENCE || seg->type == AS_CONFED_SEQUENCE)
			for (i = 0; i < seg->length; i++)
				last_as = seg->as[i];
		seg = seg->next;
	}

	return last_as;
}

/* AS path loop check.  If aspath contains asno then return >= 1. */
/* 【函数】aspath_loop_check —— 统计 asno 在路径中出现的次数（>0 即构成环路）。
 * 遍历所有段的每个 AS 号；不区分段类型（SET 里的成员也算）。
 * 返回出现次数，调用方据此拒绝“会形成环”的路由。 */
int aspath_loop_check(struct aspath *aspath, as_t asno)
{
	struct assegment *seg;
	int count = 0;

	if ((aspath == NULL) || (aspath->segments == NULL))
		return 0;

	seg = aspath->segments;

	while (seg) {
		int i;

		for (i = 0; i < seg->length; i++)
			if (seg->as[i] == asno)
				count++;

		seg = seg->next;
	}
	return count;
}

/* AS path loop check.  If aspath contains asno
 * that is a confed id then return >= 1.
 */
/* 【函数】aspath_loop_check_confed —— 只在**非 confed 段**里统计 asno 出现次数。
 * 用途：联盟内防环时，confed 段里的 AS 号不算（它们属于联盟内部，另有 confed 环检测）。 */
int aspath_loop_check_confed(struct aspath *aspath, as_t asno)
{
	struct assegment *seg;
	int count = 0;

	if (aspath == NULL || aspath->segments == NULL)
		return 0;

	seg = aspath->segments;

	while (seg) {
		unsigned int i;

		for (i = 0; i < seg->length; i++)
			if (seg->type != AS_CONFED_SEQUENCE &&
			    seg->type != AS_CONFED_SET && seg->as[i] == asno)
				count++;

		seg = seg->next;
	}
	return count;
}


/* When all of AS path is private AS return 1.  */
/* 【函数】aspath_private_as_check —— 路径里的 AS 是否**全部**为私有 AS。
 * 只要有任一公网 AS 即返回 false。用途：决定能否用 `neighbor ... remove-private-AS` 处理。 */
bool aspath_private_as_check(struct aspath *aspath)
{
	struct assegment *seg;

	if (!(aspath && aspath->segments))
		return false;

	seg = aspath->segments;

	while (seg) {
		int i;

		for (i = 0; i < seg->length; i++) {
			if (!BGP_AS_IS_PRIVATE(seg->as[i]))
				return false;
		}
		seg = seg->next;
	}
	return true;
}

/* ========== 8. ASN 替换（route-map set as-path / 去私有 AS） ========== */
/* Replace all ASN instances of the regex rule with our own ASN  */
/* 【函数】aspath_replace_regex_asn —— 把路径中**匹配正则**的 AS 换成 our_asn。
 * 参数：acl_list 是 as-path 过滤表（每项含正则 real）；our_asn 为替换值。
 * 用途：route-map 里 `set as-path replace <regex> <asn>` 的实现。
 * 做法：先 aspath_dup 出可改副本；对每个 AS 号格式化成字符串后用
 *   regexec 逐条正则匹配，命中即替换。最后刷新缓存并重算跳数。
 * 注意：正则作用在“单个 AS 号的字符串”上（非整条路径），见 bgp_filter.c 的规则编译。 */
struct aspath *aspath_replace_regex_asn(struct aspath *aspath,
					struct as_list *acl_list, as_t our_asn)
{
	struct aspath *new;
	struct assegment *cur_seg;
	struct as_list *cur_as_list;
	struct as_filter *cur_as_filter;
	char str_buf[ASPATH_STR_DEFAULT_LEN];
	uint32_t i;

	new = aspath_dup(aspath);
	cur_seg = new->segments;

	while (cur_seg) {
		cur_as_list = acl_list;
		if (cur_as_list) {
			cur_as_filter = cur_as_list->head;
			while (cur_as_filter) {
				for (i = 0; i < cur_seg->length; i++) {
					/* 把单个 AS 号格式化成字符串，供正则匹配。 */
					snprintfrr(str_buf,
						   ASPATH_STR_DEFAULT_LEN,
						   ASN_FORMAT(new->asnotation),
						   &cur_seg->as[i]);
					/* regexec 返回 0 表示匹配成功 → 替换该 AS。 */
					if (!regexec(&cur_as_filter->reg->real, str_buf, 0, NULL, 0))
						cur_seg->as[i] = our_asn;
				}
				cur_as_filter = cur_as_filter->next;
			}
		}
		cur_seg = cur_seg->next;
	}

	aspath_str_update(new, false);
	new->count = aspath_count_hops_internal(new);
	return new;
}


/* Replace all instances of the target ASN with our own ASN */
/* 【函数】aspath_replace_specific_asn —— 把等于 target_asn 的 AS 换成 our_asn。
 * 先 dup 再原地改，最后刷新缓存/跳数。 */
struct aspath *aspath_replace_specific_asn(struct aspath *aspath,
					   as_t target_asn, as_t our_asn)
{
	struct aspath *new;
	struct assegment *seg;

	new = aspath_dup(aspath);
	seg = new->segments;

	while (seg) {
		int i;

		for (i = 0; i < seg->length; i++) {
			if (seg->as[i] == target_asn)
				seg->as[i] = our_asn;
		}
		seg = seg->next;
	}

	aspath_str_update(new, false);
	new->count = aspath_count_hops_internal(new);

	return new;
}

/* Replace all ASNs with our own ASN */
/* 【函数】aspath_replace_all_asn —— 把路径里所有 AS 都换成 our_asn（全量改写）。
 * 用途：如 some route-map 场景强制把整条路径归一为本地 AS。 */
struct aspath *aspath_replace_all_asn(struct aspath *aspath, as_t our_asn)
{
	struct aspath *new;
	struct assegment *seg;

	new = aspath_dup(aspath);
	seg = new->segments;

	while (seg) {
		int i;

		for (i = 0; i < seg->length; i++)
			seg->as[i] = our_asn;

		seg = seg->next;
	}

	aspath_str_update(new, false);
	new->count = aspath_count_hops_internal(new);

	return new;
}

/* Replace all private ASNs with our own ASN */
/* 【函数】aspath_replace_private_asns —— 把私有 AS 替换为 asn。
 * 但**跳过** peer_asn：对端自己的 AS 即便落在私有区间也不替换（避免改掉对端身份）。
 * 实现与 replace_specific 类似，只是判断条件是“是私有 AS 且不等于 peer_asn”。 */
struct aspath *aspath_replace_private_asns(struct aspath *aspath, as_t asn,
					   as_t peer_asn)
{
	struct aspath *new;
	struct assegment *seg;

	new = aspath_dup(aspath);
	seg = new->segments;

	while (seg) {
		int i;

		for (i = 0; i < seg->length; i++) {
			/* Don't replace if public ASN or peer's ASN */
			if (BGP_AS_IS_PRIVATE(seg->as[i])
			    && (seg->as[i] != peer_asn))
				seg->as[i] = asn;
		}
		seg = seg->next;
	}

	aspath_str_update(new, false);
	new->count = aspath_count_hops_internal(new);

	return new;
}

/* Remove all private ASNs */
/* 【函数】aspath_remove_private_asns —— 从路径中**删除**所有私有 AS（不替换）。
 * peer_asn 为对端 AS，予以保留（即使它在私有段）。注意此处不是 dup 后改，而是
 * 新建一个 aspath，逐段过滤重建新段链。
 * 关键：最后 `if (!aspath->refcnt) aspath_free(aspath);` ——
 *   若传入的 aspath 尚未被 intern（refcnt==0），说明调用方把所有权交过来了，
 *   这里顺手释放它；若已 intern（refcnt>0）则不能释放（那是共享对象）。
 * 用途：`neighbor ... remove-private-AS` 的实现。 */
struct aspath *aspath_remove_private_asns(struct aspath *aspath, as_t peer_asn)
{
	struct aspath *new;
	struct assegment *seg;
	struct assegment *new_seg;
	struct assegment *last_new_seg;
	int i;
	int j;
	int public = 0;
	int peer = 0;

	new = XCALLOC(MTYPE_AS_PATH, sizeof(struct aspath));

	new->json = NULL;
	new_seg = NULL;
	last_new_seg = NULL;
	seg = aspath->segments;
	while (seg) {
		public = 0;
		peer = 0;
		/* 先数一遍本段里公网 AS 和对端 AS 的个数，用于决定新段长度。 */
		for (i = 0; i < seg->length; i++) {
			// ASN is public
			if (!BGP_AS_IS_PRIVATE(seg->as[i]))
				public++;
			/* ASN matches peer's.
			 * Don't double-count if peer_asn is public.
			 */
			else if (seg->as[i] == peer_asn)
				peer++;
		}

		// The entire segment is public so copy it
		/* 整段都是公网 AS：直接深拷贝整段。 */
		if (public == seg->length)
			new_seg = assegment_dup(seg);

		// The segment is a mix of public and private ASNs. Copy as many
		// spots as
		// there are public ASNs then come back and fill in only the
		// public ASNs.
		else {
			/* length needs to account for all retained ASNs
			 * (public or peer_asn), not just public
			 */
			/* 混有私有 AS：只保留 public+peer 个槽位，再逐个填入保留项。 */
			new_seg = assegment_new(seg->type, (public + peer));
			j = 0;
			for (i = 0; i < seg->length; i++) {
				// keep ASN if public or matches peer's ASN
				if (!BGP_AS_IS_PRIVATE(seg->as[i])
				    || (seg->as[i] == peer_asn)) {
					new_seg->as[j] = seg->as[i];
					j++;
				}
			}
		}

		// This is the first segment so set the aspath segments pointer
		// to this one
		if (!last_new_seg)
			new->segments = new_seg;
		else
			last_new_seg->next = new_seg;

		last_new_seg = new_seg;
		seg = seg->next;
	}
	/* 传入对象若未被共享（refcnt==0）则一并释放；被共享则不动。 */
	if (!aspath->refcnt)
		aspath_free(aspath);
	aspath_str_update(new, false);
	new->count = aspath_count_hops_internal(new);
	return new;
}

/* ========== 9. 联盟(confed)检查、合并与拼接 ========== */
/* AS path confed check.  If aspath contains confed set or sequence then return
 * 1. */
/* 【函数】aspath_confed_check —— 路径中是否存在任何 confed 段（SEQ 或 SET）。
 * 用途：判断一条路径是否携带联盟信息（RFC5065）。 */
bool aspath_confed_check(struct aspath *aspath)
{
	struct assegment *seg;

	if (!(aspath && aspath->segments))
		return false;

	seg = aspath->segments;

	while (seg) {
		if (seg->type == AS_CONFED_SET
		    || seg->type == AS_CONFED_SEQUENCE)
			return true;
		seg = seg->next;
	}
	return false;
}

/* Leftmost AS path segment confed check.  If leftmost AS segment is of type
  AS_CONFED_SEQUENCE or AS_CONFED_SET then return 1.  */
/* 【函数】aspath_left_confed_check —— **首段**是否是 confed 段。
 * 用途：对端若把 confed 段放在最左，说明消息来自联盟内部邻居，选路/预处理据此区别对待。 */
bool aspath_left_confed_check(struct aspath *aspath)
{

	if (!(aspath && aspath->segments))
		return false;

	if ((aspath->segments->type == AS_CONFED_SEQUENCE)
	    || (aspath->segments->type == AS_CONFED_SET))
		return true;

	return false;
}

/* Merge as1 to as2.  as2 should be uninterned aspath. */
/* 【函数】aspath_merge —— 把 as1 的段链**直接挂到** as2 段链的前面，形成 as1+as2。
 * 前提：as2 必须是**未 intern（可改）**的对象；函数会**原地修改 as2**。
 * 实现：复制 as1 的段链（assegment_dup_all），把其尾接到 as2 原有段链头，再让
 *   as2->segments 指向新链头。注意 as2 原有段链被“接管”，不做类型合并
 *   （即可能留下两个相邻同类型段，调用方随后会 normalise）。
 * 返回 as2（被修改后的对象）。 */
static struct aspath *aspath_merge(struct aspath *as1, struct aspath *as2)
{
	struct assegment *last, *new;

	if (!as1 || !as2)
		return NULL;

	last = new = assegment_dup_all(as1->segments);

	/* find the last valid segment */
	while (last && last->next)
		last = last->next;

	if (last)
		last->next = as2->segments;
	as2->segments = new;
	aspath_str_update(as2, false);
	as2->count = aspath_count_hops_internal(as2);
	return as2;
}

/* Prepend as1 to as2.  as2 should be uninterned aspath. */
/* 【函数】aspath_prepend —— 把 as1 拼接到 as2 **前面**（as1 更靠左），返回结果。
 * 前提：as2 必须是未 intern 的可改对象（函数原地改 as2 并返回它）。
 * 用途：出向通告前把本 AS prepend 到路径（`neighbor ... as-path prepend`）。
 * 处理分支（保证结果为规范化、无多余分段）：
 *   1) as2 为空 → 直接把 as1 段链复制给 as2。
 *   2) as1 为空 → 无需 prepend，原样返回 as2。
 *   3) 若 as1 尾段是 AS_SEQUENCE 而 as2 头段是 AS_CONFED_SEQUENCE → 先删掉 as2 的
 *      confed 段（其处不再有效），见 aspath_delete_confed_seq。
 *   4) as1 尾段与 as2 头段类型不同 → 直接 aspath_merge 拼接。
 *   5) 两者尾/头都是 AS_SEQUENCE → 需要把相邻的两段**合并成一段**（下面详述步骤）。
 * 易误解点：本函数只正确处理“as1 尾段与 as2 头段”相接；作者在末尾注释 XXX 指出
 *   “若 as1 有多段会怎样”并未特别处理（依赖后续 normalise 兜底）。 */
struct aspath *aspath_prepend(struct aspath *as1, struct aspath *as2)
{
	struct assegment *as1segtail;
	struct assegment *as2segtail;
	struct assegment *as2seghead;

	if (!as1 || !as2)
		return NULL;

	/* If as2 is empty, only need to dupe as1's chain onto as2 */
	if (as2->segments == NULL) {
		as2->segments = assegment_dup_all(as1->segments);
		aspath_str_update(as2, false);
		as2->count = aspath_count_hops_internal(as2);
		return as2;
	}

	/* If as1 is empty AS, no prepending to do. */
	if (as1->segments == NULL)
		return as2;

	/* find the tail as1's segment chain. */
	as1segtail = as1->segments;
	while (as1segtail && as1segtail->next)
		as1segtail = as1segtail->next;

	/* Delete any AS_CONFED_SEQUENCE segment from as2. */
	/* 当 as1 以 SEQUENCE 结尾、as2 以 confed-sequence 开头时，confed 段应被移除。 */
	if (as1segtail->type == AS_SEQUENCE
	    && as2->segments->type == AS_CONFED_SEQUENCE)
		as2 = aspath_delete_confed_seq(as2);

	if (!as2->segments) {
		as2->segments = assegment_dup_all(as1->segments);
		aspath_str_update(as2, false);
		as2->count = aspath_count_hops_internal(as2);
		return as2;
	}

	/* Compare last segment type of as1 and first segment type of as2. */
	if (as1segtail->type != as2->segments->type)
		return aspath_merge(as1, as2);

	if (as1segtail->type == AS_SEQUENCE) {
		/* We have two chains of segments, as1->segments and seg2,
		 * and we have to attach them together, merging the attaching
		 * segments together into one.
		 *
		 * 1. dupe as1->segments onto head of as2
		 * 2. merge seg2's asns onto last segment of this new chain
		 * 3. attach chain after seg2
		 */

		/* save as2 head */
		/* 先存好 as2 原有链头（其首段要继续合并进来）。 */
		as2seghead = as2->segments;

		/* dupe as1 onto as2's head */
		/* 复制 as1 段链作为新的 as2 链头，并找到其尾段。 */
		as2segtail = as2->segments = assegment_dup_all(as1->segments);

		/* refind the tail of as2 */
		while (as2segtail && as2segtail->next)
			as2segtail = as2segtail->next;

		/* merge the old head, seg2, into tail, seg1 */
		/* 把 as2 原首段的 AS 追加到合并后的尾段，实现两段合一。 */
		assegment_append_asns(as2segtail, as2seghead->as,
				      as2seghead->length);

		/*
		 * bypass the merged seg2, and attach any chain after it
		 * to chain descending from as2's head
		 */
		if (as2segtail)
			as2segtail->next = as2seghead->next;

		/* as2->segments is now referenceless and useless */
		/* 旧的 as2 首段已被合并，释放它。 */
		assegment_free(as2seghead);

		/* we've now prepended as1's segment chain to as2, merging
		 * the inbetween AS_SEQUENCE of seg2 in the process
		 */
		aspath_str_update(as2, false);
		as2->count = aspath_count_hops_internal(as2);
		return as2;
	} else {
		/* AS_SET merge code is needed at here. */
		/* 两边都不是 SEQUENCE（如 SET）：直接拼接即可。 */
		return aspath_merge(as1, as2);
	}
	/* XXX: Ermmm, what if as1 has multiple segments?? */

	/* Not reached */
}

/* ========== 10. as-path exclude 的“无主”暂存表 ========== */
/* insert aspath exclude in head of orphan exclude list*/
/* 【函数】as_exclude_set_orphan —— 把一个 aspath 排除项挂到“无主”链表头部。
 * 无主 = 该 `set as-path exclude` 规则尚未绑定到具体邻居/地址族，暂存于此等待绑定。
 * 同时把 exclude_aspath_acl 置空。 */
void as_exclude_set_orphan(struct aspath_exclude *ase)
{
	ase->exclude_aspath_acl = NULL;
	as_list_list_add_head(&as_exclude_list_orphan, ase);
}

/* 【函数】as_exclude_remove_orphan —— 从“无主”链表摘除指定项。 */
void as_exclude_remove_orphan(struct aspath_exclude *ase)
{
	if (as_list_list_count(&as_exclude_list_orphan))
		as_list_list_del(&as_exclude_list_orphan, ase);
}

/* currently provide only one exclude, not a list */
/* 【函数】as_exclude_lookup_orphan —— 按 acl 名在“无主”表里查找排除项。
 * 找到后**顺手从表中移除**（因为接下来它将绑定到具体位置，不再是无主）。
 * 注意：注释说明当前只支持一个匹配项（找到即 break）。 */
struct aspath_exclude *as_exclude_lookup_orphan(const char *acl_name)
{
	struct aspath_exclude *ase = NULL;
	char *name = NULL;

	frr_each (as_list_list, &as_exclude_list_orphan, ase) {
		if (ase->exclude_aspath_acl_name) {
			name = ase->exclude_aspath_acl_name;
			if (!strcmp(name, acl_name))
				break;
		}
	}
	if (ase)
		as_exclude_remove_orphan(ase);

	return ase;
}

/* ========== 11. as-path 过滤（exclude） ========== */
/* Iterate over AS_PATH segments and wipe all occurrences of the
 * listed AS numbers. Hence some segments may lose some or even
 * all data on the way, the operation is implemented as a smarter
 * version of aspath_dup(), which allocates memory to hold the new
 * data, not the original. The new AS path is returned.
 */
/* 【函数】aspath_filter_exclude —— 从 source 中剔除 exclude_list 里出现的所有 AS。
 * 用途：`set as-path exclude <asn-list>`（用一条 AS_PATH 作为“要删的 AS 清单”）。
 * 逐段处理：先数出本段要保留的 AS 个数 newlen（两遍扫描：先数后填，因为新段长度
 *   事先未知）；newlen 为 0 的段整体丢弃；否则建新段并填入保留项。
 * 注意：函数末尾 aspath_free(source) —— **调用方把 source 的所有权交给它**。
 *   因此返回的 newpath 是唯一存活对象；若 source 被别处共享会出错（调用方须保证）。
 * 返回：新 aspath（可能为空路径，注释说明这是管理员可能期望的行为）。 */
struct aspath *aspath_filter_exclude(struct aspath *source,
				     struct aspath *exclude_list)
{
	struct assegment *srcseg, *exclseg, *lastseg;
	struct aspath *newpath;

	newpath = aspath_new(source->asnotation);
	lastseg = NULL;

	for (srcseg = source->segments; srcseg; srcseg = srcseg->next) {
		unsigned i, y, newlen = 0, done = 0, skip_as;
		struct assegment *newseg;

		/* Find out, how much ASns are we going to pick from this
		 * segment.
		 * We can't perform filtering right inline, because the size of
		 * the new segment isn't known at the moment yet.
		 */
		/* 第一遍：统计本段中“不在排除清单里”的 AS 个数 → newlen。 */
		for (i = 0; i < srcseg->length; i++) {
			skip_as = 0;
			for (exclseg = exclude_list->segments;
			     exclseg && !skip_as; exclseg = exclseg->next)
				for (y = 0; y < exclseg->length; y++)
					if (srcseg->as[i] == exclseg->as[y]) {
						skip_as = 1;
						// There's no sense in testing
						// the rest of exclusion list,
						// bail out.
						break;
					}
			if (!skip_as)
				newlen++;
		}
		/* newlen is now the number of ASns to copy */
		/* 整段都会被删掉 → 直接跳过（不新建段）。 */
		if (!newlen)
			continue;

		/* Actual copying. Allocate memory and iterate once more,
		 * performing filtering. */
		/* 第二遍：按 newlen 建新段，把保留的 AS 依次填入。 */
		newseg = assegment_new(srcseg->type, newlen);
		for (i = 0; i < srcseg->length; i++) {
			skip_as = 0;
			for (exclseg = exclude_list->segments;
			     exclseg && !skip_as; exclseg = exclseg->next)
				for (y = 0; y < exclseg->length; y++)
					if (srcseg->as[i] == exclseg->as[y]) {
						skip_as = 1;
						break;
					}
			if (skip_as)
				continue;
			newseg->as[done++] = srcseg->as[i];
		}
		/* At his point newlen must be equal to done, and both must be
		 * positive. Append
		 * the filtered segment to the gross result. */
		/* 把过滤后的新段接到结果链尾。 */
		if (!lastseg)
			newpath->segments = newseg;
		else
			lastseg->next = newseg;
		lastseg = newseg;
	}
	aspath_str_update(newpath, false);
	newpath->count = aspath_count_hops_internal(newpath);
	/* We are happy returning even an empty AS_PATH, because the
	 * administrator
	 * might expect this very behaviour. There's a mean to avoid this, if
	 * necessary,
	 * by having a match rule against certain AS_PATH regexps in the
	 * route-map index.
	 */
	aspath_free(source);
	return newpath;
}

/* 【函数】aspath_filter_exclude_all —— 排除“全部”，返回空 AS_PATH。
 * 用于 `set as-path exclude all`；同样接管并释放 source 的所有权。 */
struct aspath *aspath_filter_exclude_all(struct aspath *source)
{
	struct aspath *newpath;

	newpath = aspath_new(source->asnotation);

	aspath_str_update(newpath, false);
	newpath->count = aspath_count_hops_internal(newpath);
	/* We are happy returning even an empty AS_PATH, because the
	 * administrator
	 * might expect this very behaviour. There's a mean to avoid this, if
	 * necessary,
	 * by having a match rule against certain AS_PATH regexps in the
	 * route-map index.
	 */
	aspath_free(source);
	return newpath;
}

/* 【函数】aspath_filter_exclude_acl —— 用 as-path 正则过滤表剔除匹配的 AS。
 * 与 aspath_filter_exclude 不同，这里用 acl_list（正则）判定，且是**原地**修改
 *   source 并返回同一对象（不新造整条路径）。
 * 做法：对每段每个 AS 号格式化成字符串后用 regexec 匹配；命中的先**标记为 0**
 *   （占位），并计 nb_as_del。
 *   - 若整段都被命中 → 从链上摘除并释放该段。
 *   - 若段内部分命中 → 新建一个更小的段（跳过标记为 0 的），替换旧段。
 *   - 否则保留原段。
 * 最后刷新字符串/跳数缓存，返回 source 本身。
 * 注意：之所以先把命中项置 0 再重建段，是为了在“不确定段最终长度”前先做标记。 */
struct aspath *aspath_filter_exclude_acl(struct aspath *source,
					 struct as_list *acl_list)
{
	struct assegment *cur_seg, *new_seg, *prev_seg, *next_seg;
	struct as_list *cur_as_list;
	struct as_filter *cur_as_filter;
	char str_buf[ASPATH_STR_DEFAULT_LEN];
	uint32_t nb_as_del;
	uint32_t i, j;

	cur_seg = source->segments;
	prev_seg = NULL;
	/* segments from source aspath */
	while (cur_seg) {
		next_seg = cur_seg->next;
		cur_as_list = acl_list;
		nb_as_del = 0;
		/* aspath filter list from acl_list */
		while (cur_as_list) {
			cur_as_filter = cur_as_list->head;
			while (cur_as_filter) {
				for (i = 0; i < cur_seg->length; i++) {
					/* 已标记为 0 的项跳过（避免重复计数）。 */
					if (cur_seg->as[i] == 0)
						continue;

					snprintfrr(str_buf,
						   ASPATH_STR_DEFAULT_LEN,
						   ASN_FORMAT(source->asnotation),
						   &cur_seg->as[i]);
					/* 命中正则 → 把该 AS 标记为 0（待会儿重建时丢弃）。 */
					if (!regexec(&cur_as_filter->reg->real, str_buf, 0, NULL,
						     0)) {
						cur_seg->as[i] = 0;
						nb_as_del++;
					}
				}

				cur_as_filter = cur_as_filter->next;
			}

			cur_as_list = cur_as_list->next;
		}
		/* full segment is excluded remove it */
		/* 整段命中：从链表摘除并释放。 */
		if (nb_as_del == cur_seg->length) {
			if (cur_seg == source->segments)
				/* first segment */
				source->segments = cur_seg->next;
			else if (prev_seg)
				prev_seg->next = cur_seg->next;
			assegment_free(cur_seg);
		}
		/* change in segment size -> new allocation and replace segment*/
		/* 部分命中：新建更小的段（跳过 0）替换旧段。 */
		else if (nb_as_del) {
			new_seg = assegment_new(cur_seg->type,
						cur_seg->length - nb_as_del);
			j = 0;
			for (i = 0; i < cur_seg->length; i++) {
				if (cur_seg->as[i] == 0)
					continue;
				new_seg->as[j] = cur_seg->as[i];
				j++;
			}
			new_seg->next = next_seg;
			if (cur_seg == source->segments)
				/* first segment */
				source->segments = new_seg;
			else if (prev_seg)
				prev_seg->next = new_seg;
			assegment_free(cur_seg);
			prev_seg = new_seg;
		} else
			prev_seg = cur_seg;
		cur_seg = next_seg;
	}


	aspath_str_update(source, false);
	source->count = aspath_count_hops_internal(source);
	/* We are happy returning even an empty AS_PATH, because the
	 * administrator
	 * might expect this very behaviour. There's a mean to avoid this, if
	 * necessary,
	 * by having a match rule against certain AS_PATH regexps in the
	 * route-map index.
	 */
	return source;
}

/* ========== 12. 向路径最左端添加 AS / 选路比较 ========== */
/* Add specified AS to the leftmost of aspath. */
/* 【函数】aspath_add_asns —— 在路径**最左端**插入 num 个 asno（type 指定段类型）。
 * 用于 prepend 本 AS / 加 confed 段（aspath_add_seq / aspath_add_confed_seq 都走它）。
 * 逻辑：若首段类型匹配 → 用 assegment_prepend_asns 就地前置扩展；
 *   否则新建段插到链头；若原首段是空段（length==0）则被新段替换（并释放空段）。
 * 原地修改 aspath，末了刷新字符串/跳数。 */
static struct aspath *aspath_add_asns(struct aspath *aspath, as_t asno, uint8_t type,
				      unsigned int num)
{
	struct assegment *assegment = aspath->segments;
	unsigned int i;

	if (assegment && assegment->type == type) {
		/* extend existing segment */
		aspath->segments = assegment_prepend_asns(aspath->segments, asno, num);
	} else {
		/* prepend with new segment */
		struct assegment *newsegment = assegment_new(type, num);
		for (i = 0; i < num; i++)
			newsegment->as[i] = asno;

		/* insert potentially replacing empty segment */
		/* 首段若为空段（length==0），用新段取代它并跳过它。 */
		if (assegment && assegment->length == 0) {
			newsegment->next = assegment->next;
			assegment_free(assegment);
		} else
			newsegment->next = assegment;
		aspath->segments = newsegment;
	}

	aspath_str_update(aspath, false);
	aspath->count = aspath_count_hops_internal(aspath);
	return aspath;
}

/* Add specified AS to the leftmost of aspath num times. */
/* 【函数】aspath_add_seq_n —— 在最左端连续插入 num 个 asno（AS_SEQUENCE）。
 * 即 `neighbor ... as-path prepend <asn> <count>` 的“次数”形式。 */
struct aspath *aspath_add_seq_n(struct aspath *aspath, as_t asno, unsigned num)
{
	return aspath_add_asns(aspath, asno, AS_SEQUENCE, num);
}

/* Add specified AS to the leftmost of aspath. */
/* 【函数】aspath_add_seq —— 在最左端插入一个 asno（AS_SEQUENCE），即常见 prepend 一次。 */
struct aspath *aspath_add_seq(struct aspath *aspath, as_t asno)
{
	return aspath_add_asns(aspath, asno, AS_SEQUENCE, 1);
}

/* Compare leftmost AS value for MED check.  If as1's leftmost AS and
   as2's leftmost AS is same return 1. */
/* 【函数】aspath_cmp_left —— 比较两条路径的“最左 AS”（用于 MED 比较判断是否同一邻居）。
 * 返回 true 表示两者“最左非 confed 相邻 AS”相同（视为来自同一相邻 AS，可比较 MED）。
 * 细节：
 *   - 两条都是空路径（本地起源）→ 返回 true（都来自本 AS，值得比 MED）。
 *   - 各自**跳过开头的 confed 段**，找第一个非 confed 段。
 *   - 要求两边的该段都是 AS_SEQUENCE，再比 as[0]。
 * 与 aspath_cmp_left_confed 的分工：本函数用于普通(非联盟)邻居的 MED 比较；
 *   联盟场景另用 *_confed 版本。医学语境下的“left”指路径最左（越靠近本端）。 */
bool aspath_cmp_left(const struct aspath *aspath1, const struct aspath *aspath2)
{
	const struct assegment *seg1;
	const struct assegment *seg2;

	if (!(aspath1 && aspath2))
		return false;

	seg1 = aspath1->segments;
	seg2 = aspath2->segments;

	/* If both paths are originated in this AS then we do want to compare
	 * MED */
	if (!seg1 && !seg2)
		return true;

	/* find first non-confed segments for each */
	/* 跳过各自开头的 confed 段，找第一个非 confed 段。 */
	while (seg1 && ((seg1->type == AS_CONFED_SEQUENCE)
			|| (seg1->type == AS_CONFED_SET)))
		seg1 = seg1->next;

	while (seg2 && ((seg2->type == AS_CONFED_SEQUENCE)
			|| (seg2->type == AS_CONFED_SET)))
		seg2 = seg2->next;

	/* Check as1's */
	/* 两边都必须落在 AS_SEQUENCE 段上，才可以比较最左 AS。 */
	if (!(seg1 && seg2 && (seg1->type == AS_SEQUENCE)
	      && (seg2->type == AS_SEQUENCE)))
		return false;

	if (seg1->as[0] == seg2->as[0])
		return true;

	return false;
}

/* Truncate an aspath after a number of hops, and put the hops remaining
 * at the front of another aspath.  Needed for AS4 compat.
 *
 * Returned aspath is a /new/ aspath, which should either by free'd or
 * interned by the caller, as desired.
 */
/* 【函数】aspath_reconcile_as4 —— 把 AS_PATH 与 AS4_PATH 合成真实的完整路径（RFC6793）。
 * 背景：与老式(2 字节 AS)邻居交互时，真实的 4 字节 AS 被拆成两份：AS_PATH 里放
 *   AS_TRANS(23456) 占位、AS4_PATH 里放真正的 4 字节值。本函数把二者“对上”。
 * 参数：aspath 是收到的 AS_PATH；as4path 是对应的 AS4_PATH。
 * 返回：**新** aspath（调用方决定 free 还是 intern）；任一为 NULL 返回 NULL。
 * 算法（RFC6793 §4.2.3）：
 *   1) 计算 hop 差 hops = (AS_PATH 跳数+confed) - (AS4_PATH 跳数+confed)。
 *      - hops < 0：AS4_PATH 比 AS_PATH 还长，不合法 → 忽略 AS4_PATH，原样返回 AS_PATH 的副本。
 *      - hops == 0：二者长度相同 → 直接采用 AS4_PATH（返回其副本）。
 *   2) hops > 0：保留 AS_PATH 的**最左 hops 个跳**（这段对应老路径中 AS 号被 AS_TRANS
 *      糊掉的部分），把 AS4_PATH 拼到其后，拼出完整路径。
 *      - 遍历时按段处理：AS_SET/CONFED_SET 算 1 跳（整体保留）；AS_SEQUENCE 按需截取；
 *        AS_CONFED_SEQUENCE 不允许被截断（若 hop 数迫使截断则整段并入，见注释）。
 *   3) 最后 aspath_merge 拼接 + assegment_normalise 规范化。
 * 易误解点：这里处理的是“路径左部的 AS_TRANS 被替换为真实值”，故保留的是 AS_PATH 的
 *   左段（最靠近对端/陈旧部分），AS4_PATH 描述的是同样这些跳的真实 AS。 */
struct aspath *aspath_reconcile_as4(struct aspath *aspath,
				    struct aspath *as4path)
{
	struct assegment *seg, *newseg, *prevseg = NULL;
	struct aspath *newpath = NULL, *mergedpath;
	int hops, cpasns = 0;

	if (!aspath || !as4path)
		return NULL;

	seg = aspath->segments;

	/* CONFEDs should get reconciled too.. */
	/* hop 差 = AS_PATH 长度(含 confed) - AS4_PATH 长度(含 confed)。 */
	hops = (aspath_count_hops(aspath) + aspath_count_confeds(aspath)) -
	       (aspath_count_hops(as4path) + aspath_count_confeds(as4path));

	if (hops < 0) {
		flog_warn(EC_BGP_ASPATH_FEWER_HOPS,
			  "[AS4] Fewer hops in AS_PATH than AS4_PATH, ignoring AS4_PATH (RFC 6793)");
		/* RFC 6793 4.1.3:
		 * if AS_PATH < AS4_PATH, AS4_PATH SHALL be ignored and
		 * AS_PATH SHALL be taken as-is.
		 */
		return aspath_dup(aspath);
	}

	if (!hops) {
		/* 长度相同：AS4_PATH 完整描述了这条路径 → 采用它。 */
		newpath = aspath_dup(as4path);
		aspath_str_update(newpath, false);
		/* dup sets the count properly */
		return newpath;
	}

	if (BGP_DEBUG(as4, AS4))
		zlog_debug(
			"[AS4] got AS_PATH %s and AS4_PATH %s synthesizing now",
			aspath->str, as4path->str);

	/* hops > 0：保留 AS_PATH 最左 hops 跳，后面再接 AS4_PATH。 */
	while (seg && hops > 0) {
		switch (seg->type) {
		case AS_SET:
		case AS_CONFED_SET:
			/* SET 段整体算 1 跳，全部保留（cpasns = 段长）。 */
			hops--;
			cpasns = seg->length;
			break;
		case AS_CONFED_SEQUENCE:
			/* Should never split a confed-sequence, if hop-count
			 * suggests we must then something's gone wrong
			 * somewhere.
			 *
			 * Most important goal is to preserve AS_PATHs prime
			 * function
			 * as loop-detector, so we fudge the numbers so that the
			 * entire
			 * confed-sequence is merged in.
			 */
			/* 不允许把 confed-sequence 从中间截断：若 hop 数要求截断，
			 * 就干脆整段并入（调整 hops）。 */
			if (hops < seg->length) {
				if (BGP_DEBUG(as4, AS4))
					zlog_debug(
						"[AS4] AS4PATHmangle: AS_CONFED_SEQUENCE falls across 2/4 ASN boundary somewhere, broken..");
				hops = seg->length;
			}
			fallthrough;
		case AS_SEQUENCE:
			/* 按 hop 数截取：最多取 min(段长, 剩余 hops) 个。 */
			cpasns = MIN(seg->length, hops);
			hops -= seg->length;
		}

		assert(cpasns <= seg->length);

		/* 把截取到的部分复制成新段挂到 newpath。 */
		newseg = assegment_new(seg->type, 0);
		newseg = assegment_append_asns(newseg, seg->as, cpasns);

		if (!newpath) {
			newpath = aspath_new(aspath->asnotation);
			newpath->segments = newseg;
		} else
			prevseg->next = newseg;

		prevseg = newseg;
		seg = seg->next;
	}

	/* We may be able to join some segments here, and we must
	 * do this because... we want normalised aspaths in out hash
	 * and we do not want to stumble in aspath_put.
	 */
	/* 拼接：newpath(AS_PATH 左段) + as4path 副本，再规范化。 */
	mergedpath = aspath_merge(newpath, aspath_dup(as4path));
	aspath_free(newpath);
	mergedpath->segments = assegment_normalise(mergedpath->segments);
	aspath_str_update(mergedpath, false);
	mergedpath->count = aspath_count_hops_internal(mergedpath);

	if (BGP_DEBUG(as4, AS4))
		zlog_debug("[AS4] result of synthesizing is %s",
			   mergedpath->str);

	return mergedpath;
}

/* Compare leftmost AS value for MED check.  If as1's leftmost AS and
   as2's leftmost AS is same return 1. (confederation as-path
   only).  */
/* 【函数】aspath_cmp_left_confed —— 联盟场景下比较两条路径的最左 AS。
 * 与 aspath_cmp_left 的区别：此处要求两边的**首段都是 AS_CONFED_SEQUENCE**，
 * 且比较的是 confed 段的首个 AS。用于联盟内选路时判断是否同一联盟成员邻居。
 * 严格得多（类型不符直接 false）。 */
bool aspath_cmp_left_confed(const struct aspath *aspath1,
			    const struct aspath *aspath2)
{
	if (!(aspath1 && aspath2))
		return false;

	if (!(aspath1->segments && aspath2->segments))
		return false;

	/* 两边首段都必须正好是 AS_CONFED_SEQUENCE，否则不可比。 */
	if ((aspath1->segments->type != AS_CONFED_SEQUENCE)
	    || (aspath2->segments->type != AS_CONFED_SEQUENCE))
		return false;

	if (aspath1->segments->as[0] == aspath2->segments->as[0])
		return true;

	return false;
}

/* Delete all AS_CONFED_SEQUENCE/SET segments from aspath.
 * RFC 5065 section 4.1.c.1
 *
 * 1) if any path segments of the AS_PATH are of the type
 *    AS_CONFED_SEQUENCE or AS_CONFED_SET, those segments MUST be
 *    removed from the AS_PATH attribute, leaving the sanitized
 *    AS_PATH attribute to be operated on by steps 2, 3 or 4.
 */
/* 【函数】aspath_delete_confed_seq —— 删除路径中所有 confed 段（原地修改并返回 aspath）。
 * 依据 RFC5065 §4.1.c.1：AS_PATH 对外(离开联盟)发布前必须剥离 confed 段。
 * 实现：遍历链表，遇到 confed 段就从链上摘除并释放（首段特殊处理）；
 * 若有删除则刷新字符串/跳数缓存。 */
struct aspath *aspath_delete_confed_seq(struct aspath *aspath)
{
	struct assegment *seg, *prev, *next;
	char removed_confed_segment;

	if (!(aspath && aspath->segments))
		return aspath;

	seg = aspath->segments;
	removed_confed_segment = 0;
	next = NULL;
	prev = NULL;

	while (seg) {
		next = seg->next;

		if (seg->type == AS_CONFED_SEQUENCE
		    || seg->type == AS_CONFED_SET) {
			/* This is the first segment in the aspath */
			/* 是首段：直接改 aspath->segments 指向下一个；否则让 prev 跳过它。 */
			if (aspath->segments == seg)
				aspath->segments = seg->next;
			else
				prev->next = seg->next;

			assegment_free(seg);
			removed_confed_segment = 1;
		} else
			prev = seg;

		seg = next;
	}

	if (removed_confed_segment) {
		aspath_str_update(aspath, false);
		aspath->count = aspath_count_hops_internal(aspath);
	}

	return aspath;
}

/* Add new AS number to the leftmost part of the aspath as
   AS_CONFED_SEQUENCE.  */
/* 【函数】aspath_add_confed_seq —— 在最左端加一个 AS_CONFED_SEQUENCE 类型的段（含 asno）。 */
struct aspath *aspath_add_confed_seq(struct aspath *aspath, as_t asno)
{
	return aspath_add_asns(aspath, asno, AS_CONFED_SEQUENCE, 1);
}

/* Add new as value to as path structure. */
/* 【函数】aspath_as_add —— 把一个 AS 号追加到路径**最后一段的末尾**。
 * 供 aspath_str2aspath 从字符串构建路径时使用（不刷新缓存，由调用方最后统一刷新）。 */
static void aspath_as_add(struct aspath *as, as_t asno)
{
	struct assegment *seg = as->segments;

	if (!seg)
		return;

	/* Last segment search procedure. */
	while (seg->next)
		seg = seg->next;

	assegment_append_asns(seg, &asno, 1);
}

/* Add new as segment to the as path. */
/* 【函数】aspath_segment_add —— 在路径末尾追加一个**空的新段**（type 指定）。
 * 供 aspath_str2aspath 遇到 '{'/'(' 等定界符时新开段使用。 */
static void aspath_segment_add(struct aspath *as, int type)
{
	struct assegment *seg = as->segments;
	struct assegment *new = assegment_new(type, 0);

	if (seg) {
		while (seg->next)
			seg = seg->next;
		seg->next = new;
	} else
		as->segments = new;
}

/* ========== 13. 空路径、字符串解析、哈希与打印 ========== */
/* 【函数】aspath_empty —— 返回“空 AS_PATH”（interned）。
 * 通过 aspath_parse(NULL, 0, 1, ...) 走正常解析/去重路径得到：length=0 → 空段链。
 * 硬编码 use32bit=1（注释 “32Bit ;-)”）是因为空路径与 2B/4B 无关。
 * 返回的是共享对象（refcnt 已 +1），用完需 unintern。 */
struct aspath *aspath_empty(enum asnotation_mode asnotation)
{
	return aspath_parse(NULL, 0, 1, asnotation); /* 32Bit ;-) */
}

/* 【函数】aspath_empty_get —— 造一个**未 intern** 的空 aspath（本地起源/临时用）。
 * asnotation 取自 bgp 全局设置，并预生成字符串表示。 */
struct aspath *aspath_empty_get(void)
{
	struct aspath *aspath;

	aspath = aspath_new(bgp_get_asnotation(NULL));
	aspath_make_str_count(aspath, false);
	return aspath;
}

/* 【函数】aspath_count —— 返回全局哈希表里 aspath 的条目数（供 `show bgp summary` 等统计）。 */
unsigned long aspath_count(void)
{
	return ashash->count;
}

/*
   Theoretically, one as path can have:

   One BGP packet size should be less than 4096.
   One BGP attribute size should be less than 4096 - BGP header size.
   One BGP aspath size should be less than 4096 - BGP header size -
       BGP mandantry attribute size.
*/

/* AS path string lexical token enum. */
/* 【枚举】aspath 字符串词法记号：见 aspath_gettoken 的切分。
 * 各成员含义：
 *   as_token_asval            —— 一个 AS 号值
 *   as_token_set_start        —— '{'（AS_SET 开始）
 *   as_token_set_end          —— '}'（AS_SET 结束）
 *   as_token_confed_seq_start —— '('（AS_CONFED_SEQUENCE 开始）
 *   as_token_confed_seq_end   —— ')'（AS_CONFED_SEQUENCE 结束）
 *   as_token_confed_set_start —— '['（AS_CONFED_SET 开始）
 *   as_token_confed_set_end   —— ']'（AS_CONFED_SET 结束）
 *   as_token_unknown          —— 非法记号
 */
enum as_token {
	as_token_asval,
	as_token_set_start,
	as_token_set_end,
	as_token_confed_seq_start,
	as_token_confed_seq_end,
	as_token_confed_set_start,
	as_token_confed_set_end,
	as_token_unknown
};

/* Return next token and point for string parse. */
/* 【函数】aspath_gettoken —— 从字符串里取下一个词法记号。
 * 参数：buf 当前解析位置；token 出参记号类型；asno 出参 AS 号（若为 AS 值）。
 * 返回：下一个解析位置指针；到字符串末尾返回 NULL。
 * 步骤：先跳过分隔符（空格/逗号）→ 判断定界符（{ } ( ) [ ]）→ 否则用
 *   asn_str2asn_parse 解析一个 AS 号（支持 asplain / asdot 记法）。
 * 供 aspath_str2aspath 使用（把配置里的字符串如 “65001 65002 {65003,65004}” 解析成路径）。 */
static const char *aspath_gettoken(const char *buf, enum as_token *token,
				   unsigned long *asno)
{
	const char *p = buf;
	as_t asval;
	bool found = false;

	/* Skip separators (space for sequences, ',' for sets). */
	while (isspace((unsigned char)*p) || *p == ',')
		p++;

	/* Check the end of the string and type specify characters
	   (e.g. {}()). */
	switch (*p) {
	case '\0':
		return NULL;
	case '{':
		*token = as_token_set_start;
		p++;
		return p;
	case '}':
		*token = as_token_set_end;
		p++;
		return p;
	case '(':
		*token = as_token_confed_seq_start;
		p++;
		return p;
	case ')':
		*token = as_token_confed_seq_end;
		p++;
		return p;
	case '[':
		*token = as_token_confed_set_start;
		p++;
		return p;
	case ']':
		*token = as_token_confed_set_end;
		p++;
		return p;
	}

	/* 非定界符：尝试按 AS 号解析（asplain/asdot 均可）。 */
	asval = 0;
	p = asn_str2asn_parse(p, &asval, &found);
	if (found) {
		*asno = asval;
		*token = as_token_asval;
	} else
		*token = as_token_unknown;
	return p;
}

/* 【函数】aspath_str2aspath —— 把字符串形式的 AS_PATH 解析成 aspath 结构。
 * 例：“65001 65002 {65003,65004}” → SEQUENCE[65001,65002] + SET{65003,65004}。
 * 返回：**未 intern** 的新 aspath（调用方决定 intern 或 free）；遇到非法记号返回 NULL。
 * 状态机：默认类型 AS_SEQUENCE；遇到 '{' 切到 AS_SET、'(' 到 AS_CONFED_SEQUENCE、
 *   '[' 到 AS_CONFED_SET，遇到对应闭符切回 AS_SEQUENCE；needtype 标记“是否需新开段”。
 * 主要用途：命令行/配置里直接写 AS_PATH 列表（如 set as-path prepend 后的解析、
 *   聚合的 static as-path 等）。 */
struct aspath *aspath_str2aspath(const char *str,
				 enum asnotation_mode asnotation)
{
	enum as_token token = as_token_unknown;
	unsigned short as_type;
	unsigned long asno = 0;
	struct aspath *aspath;
	int needtype;

	aspath = aspath_new(asnotation);

	/* We start default type as AS_SEQUENCE. */
	as_type = AS_SEQUENCE;
	needtype = 1;

	while ((str = aspath_gettoken(str, &token, &asno)) != NULL) {
		switch (token) {
		case as_token_asval:
			/* 普通 AS 号：如需新段先建段，再把 asno 追加到当前段尾。 */
			if (needtype) {
				aspath_segment_add(aspath, as_type);
				needtype = 0;
			}
			aspath_as_add(aspath, asno);
			break;
		case as_token_set_start:
			as_type = AS_SET;
			aspath_segment_add(aspath, as_type);
			needtype = 0;
			break;
		case as_token_set_end:
			as_type = AS_SEQUENCE;
			needtype = 1;
			break;
		case as_token_confed_seq_start:
			as_type = AS_CONFED_SEQUENCE;
			aspath_segment_add(aspath, as_type);
			needtype = 0;
			break;
		case as_token_confed_seq_end:
			as_type = AS_SEQUENCE;
			needtype = 1;
			break;
		case as_token_confed_set_start:
			as_type = AS_CONFED_SET;
			aspath_segment_add(aspath, as_type);
			needtype = 0;
			break;
		case as_token_confed_set_end:
			as_type = AS_SEQUENCE;
			needtype = 1;
			break;
		case as_token_unknown:
		default:
			/* 非法记号：释放半成品并失败返回。 */
			aspath_free(aspath);
			return NULL;
		}
	}

	aspath_make_str_count(aspath, false);

	return aspath;
}

/* Make hash value by raw aspath data. */
/* 【函数】aspath_key_make —— 计算 aspath 的哈希键（供全局哈希表 ashash）。
 * 直接用“字符串表示”的 jhash：因为 normalise 保证了同语义路径字符串一致，
 *   借助已有 str 做键既准确又免去自定义比较。str 为空时先 aspath_str_update 生成。
 * 易误解点：**依赖 aspath->str 与 segments 同步**——若改了 segments 未刷新 str，
 *   哈希键就会错，导致去重失效或重复条目。 */
unsigned int aspath_key_make(const void *p)
{
	const struct aspath *aspath = p;
	unsigned int key = 0;

	if (!aspath->str)
		aspath_str_update((struct aspath *)aspath, false);

	key = jhash(aspath->str, aspath->str_len, 2334325);

	return key;
}

/* If two aspath have same value then return 1 else return 0 */
/* 【函数】aspath_cmp —— 逐段逐 AS 判断两条 aspath 是否“相等”（哈希表比较器）。
 * 与 aspath_key_make 配套：键相同还不够，必须用本函数确认完全一致，避免哈希碰撞误判。
 * 比较内容：asnotation 必须相同；段链长度、每段 type、length、每个 AS 值都要一致。
 * 注意：这里比的是**整个段链**（结构 + 内容），与 aspath_cmp_left（只比最左 AS）不同。 */
bool aspath_cmp(const void *arg1, const void *arg2)
{
	const struct assegment *seg1 = ((const struct aspath *)arg1)->segments;
	const struct assegment *seg2 = ((const struct aspath *)arg2)->segments;

	/* 字符串记法(asplain/asdot)不同的路径不视为相等。 */
	if (((const struct aspath *)arg1)->asnotation !=
	    ((const struct aspath *)arg2)->asnotation)
		return false;

	while (seg1 || seg2) {
		int i;
		/* 一个已结束另一个还有 → 不相等。 */
		if ((!seg1 && seg2) || (seg1 && !seg2))
			return false;
		if (seg1->type != seg2->type)
			return false;
		if (seg1->length != seg2->length)
			return false;
		for (i = 0; i < seg1->length; i++)
			if (seg1->as[i] != seg2->as[i])
				return false;
		seg1 = seg1->next;
		seg2 = seg2->next;
	}
	return true;
}

/* AS path hash initialize. */
/* 【函数】aspath_init —— bgpd 启动时初始化全局 aspath 哈希表与“无主排除”链表。
 * 哈希表大小 32768，键/比较器分别为 aspath_key_make / aspath_cmp。 */
void aspath_init(void)
{
	ashash = hash_create_size(32768, aspath_key_make, aspath_cmp,
				  "BGP AS Path");

	as_list_list_init(&as_exclude_list_orphan);
}

/* 【函数】aspath_finish —— bgpd 退出时清理：释放哈希表全部条目、SNMP stream、
 * 以及“无主排除”链表中剩余项。 */
void aspath_finish(void)
{
	struct aspath_exclude *ase;

	hash_clean_and_free(&ashash, (void (*)(void *))aspath_free);

	if (snmp_stream)
		stream_free(snmp_stream);

	while ((ase = as_list_list_pop(&as_exclude_list_orphan))) {
		aspath_free(ase->aspath);
		if (ase->exclude_aspath_acl_name)
			XFREE(MTYPE_TMP, ase->exclude_aspath_acl_name);
		XFREE(MTYPE_ROUTE_MAP_COMPILED, ase);
	}
	as_list_list_fini(&as_exclude_list_orphan);
}

/* return and as path value */
/* 【函数】aspath_print —— 返回 aspath 的字符串表示；NULL 返回 "(null)"。 */
const char *aspath_print(struct aspath *as)
{
	return as ? as->str : "(null)";
}

/* Printing functions */
/* Feed the AS_PATH to the vty; the space suffix follows it only in case
 * AS_PATH wasn't empty.
 */
/* 【函数】aspath_print_vty —— 向 vty 输出 AS_PATH 字符串；非空时末尾补一个空格
 * （便于拼接后续字段）。 */
void aspath_print_vty(struct vty *vty, struct aspath *as)
{
	vty_out(vty, "%s%s", as->str, as->str_len ? " " : "");
}

/* 【回调】aspath_show_all_iterator —— 遍历哈希表时打印单个条目（指针、哈希键、refcnt、字符串）。 */
static void aspath_show_all_iterator(struct hash_bucket *bucket,
				     struct vty *vty)
{
	struct aspath *as;

	as = (struct aspath *)bucket->data;

	vty_out(vty, "[%p:%u] (%ld) ", (void *)bucket, bucket->key, as->refcnt);
	vty_out(vty, "%s\n", as->str);
}

/* Print all aspath and hash information.  This function is used from
   `show [ip] bgp paths' command. */
/* 【函数】aspath_print_all_vty —— 遍历全局哈希表，打印所有 interned aspath（`show bgp paths`）。 */
void aspath_print_all_vty(struct vty *vty)
{
	hash_iterate(ashash, (void (*)(struct hash_bucket *,
				       void *))aspath_show_all_iterator,
		     vty);
}

/* ========== 14. 聚合(aggregate)专用的 aspath 收集与合并 ========== */
/* 【函数】bgp_aggr_aspath_lookup —— 在聚合对象的 aspath_hash 里查找与 aspath 匹配的条目。 */
static struct aspath *bgp_aggr_aspath_lookup(struct bgp_aggregate *aggregate,
					     struct aspath *aspath)
{
	return hash_lookup(aggregate->aspath_hash, aspath);
}

/* 【回调】bgp_aggr_aspath_hash_alloc —— aspath_hash 未命中时新建条目：深拷贝一份 aspath。 */
static void *bgp_aggr_aspath_hash_alloc(void *p)
{
	struct aspath *ref = (struct aspath *)p;
	struct aspath *aspath = NULL;

	aspath = aspath_dup(ref);
	return aspath;
}

/* 【回调】bgp_aggr_aspath_prepare —— 遍历聚合哈希表时，把每个 aspath 逐步聚合进 *aggr_aspath。
 * 第一次直接复制；之后每来一个就 aspath_aggregate 合并（旧结果释放），实现“全部成员求并”。
 * 结果即为该聚合前缀的最终 AS_PATH。 */
static void bgp_aggr_aspath_prepare(struct hash_bucket *hb, void *arg)
{
	struct aspath *hb_aspath = hb->data;
	struct aspath **aggr_aspath = arg;
	struct aspath *aspath = NULL;

	if (*aggr_aspath) {
		/* 已有聚合中间结果：与新成员聚合，释放旧结果。 */
		aspath = aspath_aggregate(*aggr_aspath, hb_aspath);
		aspath_free(*aggr_aspath);
		*aggr_aspath = aspath;
	} else {
		/* 第一个成员：直接复制作为起点。 */
		*aggr_aspath = aspath_dup(hb_aspath);
	}
}

/* 【函数】bgp_aggr_aspath_remove —— 聚合哈希条目的释放回调（只释放 aspath）。 */
void bgp_aggr_aspath_remove(void *arg)
{
	struct aspath *aspath = arg;

	aspath_free(aspath);
}

/* 【函数】bgp_compute_aggregate_aspath —— 加入一条 aspath 到聚合集合，并重算聚合结果 AS_PATH。
 * 两步：先把 aspath 记入哈希（去重+引用计数），再重算聚合值。 */
void bgp_compute_aggregate_aspath(struct bgp_aggregate *aggregate,
				  struct aspath *aspath)
{
	bgp_compute_aggregate_aspath_hash(aggregate, aspath);

	bgp_compute_aggregate_aspath_val(aggregate);

}

/* 【函数】bgp_compute_aggregate_aspath_hash —— 把一条 AS_PATH 记入聚合的 aspath_hash。
 * 用途：聚合路由的 AS_PATH 是所有被聚合明细路径的“并”。这里维护那个集合（去重）。
 * 若哈希表不存在则创建；命中已有条目则只 refcnt++，否则插入新副本（bgp_aggr_aspath_hash_alloc）。
 * 注意：这里的 refcnt 语义是“有多少条明细路径贡献了这条 aspath”，归零才移除。 */
void bgp_compute_aggregate_aspath_hash(struct bgp_aggregate *aggregate,
				       struct aspath *aspath)
{
	struct aspath *aggr_aspath = NULL;

	if ((aggregate == NULL) || (aspath == NULL))
		return;

	/* Create hash if not already created.
	 */
	if (aggregate->aspath_hash == NULL)
		aggregate->aspath_hash = hash_create(
					aspath_key_make, aspath_cmp,
					"BGP Aggregator as-path hash");

	aggr_aspath = bgp_aggr_aspath_lookup(aggregate, aspath);
	if (aggr_aspath == NULL) {
		/* Insert as-path into hash.
		 */
		aggr_aspath = hash_get(aggregate->aspath_hash, aspath,
				       bgp_aggr_aspath_hash_alloc);
	}

	/* Increment reference counter.
	 */
	aggr_aspath->refcnt++;
}

/* 【函数】bgp_compute_aggregate_aspath_val —— 重算聚合对象的最终 AS_PATH（aggregate->aspath）。
 * 先释放旧结果，再遍历 aspath_hash 用 bgp_aggr_aspath_prepare 把所有成员聚合成一条。
 * 无成员时不设结果（保持 NULL）。 */
void bgp_compute_aggregate_aspath_val(struct bgp_aggregate *aggregate)
{
	if (aggregate == NULL)
		return;
	/* Re-compute aggregate's as-path.
	 */
	if (aggregate->aspath) {
		aspath_free(aggregate->aspath);
		aggregate->aspath = NULL;
	}
	if (aggregate->aspath_hash
	    && aggregate->aspath_hash->count) {
		hash_iterate(aggregate->aspath_hash,
			     bgp_aggr_aspath_prepare,
			     &aggregate->aspath);
	}
}

/* 【函数】bgp_remove_aspath_from_aggregate —— 从聚合集合中“撤销”一条 aspath。
 * 使对应条目 refcnt--；归零则从哈希表摘除释放，并重算聚合结果 AS_PATH。
 * 用途：某条明细路由被撤回/变化时，更新聚合路由的 AS_PATH。 */
void bgp_remove_aspath_from_aggregate(struct bgp_aggregate *aggregate,
				      struct aspath *aspath)
{
	struct aspath *aggr_aspath = NULL;
	struct aspath *ret_aspath = NULL;

	if ((!aggregate)
	    || (!aggregate->aspath_hash)
	    || (!aspath))
		return;

	/* Look-up the aspath in the hash.
	 */
	aggr_aspath = bgp_aggr_aspath_lookup(aggregate, aspath);
	if (aggr_aspath) {
		aggr_aspath->refcnt--;

		if (aggr_aspath->refcnt == 0) {
			ret_aspath = hash_release(aggregate->aspath_hash,
						  aggr_aspath);
			aspath_free(ret_aspath);
			ret_aspath = NULL;

			/* Remove aggregate's old as-path.
			 */
			/* 集合变化后，重算聚合结果。 */
			aspath_free(aggregate->aspath);
			aggregate->aspath = NULL;

			bgp_compute_aggregate_aspath_val(aggregate);
		}
	}
}

/* 【函数】bgp_remove_aspath_from_aggregate_hash —— 仅从哈希表撤销条目（refcnt 归零则释放），
 * **不**重算聚合结果。与上面函数的区别：调用方自行决定何时刷新聚合值。 */
void bgp_remove_aspath_from_aggregate_hash(struct bgp_aggregate *aggregate,
					   struct aspath *aspath)
{
	struct aspath *aggr_aspath = NULL;
	struct aspath *ret_aspath = NULL;

	if ((!aggregate)
	    || (!aggregate->aspath_hash)
	    || (!aspath))
		return;

	/* Look-up the aspath in the hash.
	 */
	aggr_aspath = bgp_aggr_aspath_lookup(aggregate, aspath);
	if (aggr_aspath) {
		aggr_aspath->refcnt--;

		if (aggr_aspath->refcnt == 0) {
			ret_aspath = hash_release(aggregate->aspath_hash,
						  aggr_aspath);
			aspath_free(ret_aspath);
			ret_aspath = NULL;
		}
	}
}

/* 【函数】aspath_delete_as_set_seq —— 删除路径中所有 SET 段（AS_SET 与 AS_CONFED_SET）。
 * 原地修改并返回 aspath。与 aspath_delete_confed_seq 类似（后者删的是 confed 的两种段），
 *   本函数额外把 AS_SET 也一并删除。用途：某些场景(如特定 bestpath/出向处理)需要
 *   去掉 SET 段使路径只含 SEQUENCE。 */
struct aspath *aspath_delete_as_set_seq(struct aspath *aspath)
{
	struct assegment *seg, *prev, *next;
	bool removed = false;

	if (!(aspath && aspath->segments))
		return aspath;

	seg = aspath->segments;
	next = NULL;
	prev = NULL;

	while (seg) {
		next = seg->next;

		if (seg->type == AS_SET || seg->type == AS_CONFED_SET) {
			/* 从链上摘除该 SET 段（首段特殊处理）并释放。 */
			if (aspath->segments == seg)
				aspath->segments = seg->next;
			else
				prev->next = seg->next;

			assegment_free(seg);
			removed = true;
		} else
			prev = seg;

		seg = next;
	}

	if (removed) {
		aspath_str_update(aspath, false);
		aspath->count = aspath_count_hops_internal(aspath);
	}

	return aspath;
}

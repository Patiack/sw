// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Administrative-group library (RFC3630, RFC5305, RFC5329, RFC7308)
 *
 * Copyright 2022 Hiroki Shirokura, LINE Corporation
 * Copyright 2022 Masakazu Asama
 * Copyright 2022 6WIND S.A.
 */

/*
 * 【文件导读】admin_group.c —— 链路管理组(Administrative Group)位图实现
 * =====================================================================
 * 【职责】实现 admin_group.h 声明的位图操作。管理组是 IGP 给链路打的位串
 *   (俗称 link color / affinity)，用于 TE 与 FAD(灵活算法) 约束匹配。
 *   “空=标准 32 位 / 非空=扩展位图(RFC7308)”的约定详见 admin_group.h。
 *
 * 【底层】位图用 lib/bitfield.h 的 bitfield_t(该头文件已有详细注释)。其字段：
 *   bitmap.data : word_t(即 unsigned int) 数组，按字存放位；
 *   bitmap.m    : 已**分配**的字数(容量)；
 *   bitmap.n    : 本模块借用它作“长度/已用字数”语义(注意与 bitfield.h 注释里
 *                 n 的本意不同)：size/显式全 0 的判定都依赖 n 的取值。
 *   位操作辅助：bf_index(pos)=pos/WORD_SIZE(字下标), bf_offset(pos)=pos%WORD_SIZE
 *   (字内偏移), bf_release_index 清位。
 *
 * 【两条主线】
 *   1) 容量管理：admin_group_extend 在写入越界时按需扩容并把新字清零；
 *   2) 长度语义：admin_group_size(ag) 返回“最高非零字之后”的长度(扫描得到)，
 *      而 n 则被显式设定，用来区分“全 0 但被显式设置”(n==1 且 data[0]==0) 与
 *      “空 / 未设置”。
 *
 * 【标准 vs 扩展的换算】标准 32 位管理组在扩展位图里就是第 0 个字；因此 match_*
 *   用 `i == 0 && link_std_ag` 优先取标准值。RFC7308 2.3.1 规定两者同现时，标准值
 *   充当低 32 位(color 0-31)、扩展值提供高位。
 *
 * 【阅读顺序】打印 -> 比较 / 拷贝 / 生命周期 -> 单字读(get_offset) -> 扩容辅助
 *   -> 置位 / 清位 / 取值 -> 批量写入 -> 容量与清零 -> 显式全 0 开关 -> 匹配。
 * =====================================================================
 */
#include "admin_group.h"
#include "bitfield.h"

/* 【格式化】把管理组位图格式化成可读的十六进制字符串(每 4 个字换行)。
 * 参数 out    : 输出缓冲(调用方宜给 ADMIN_GROUP_PRINT_MAX_SIZE)。
 * 参数 sz     : 缓冲大小。
 * 参数 indent : 换行后的缩进空格数。
 * 参数 ag     : 待格式化位图。
 * 返回：out(便于链式使用)。
 * 分支语义：先判 admin_group_explicit_zero -> 打印 "0x00000000"(区别于未设置)；
 *   再判 admin_group_zero -> 打印 "not-set"；否则从高位字往低位字打印。
 * 【易踩的坑】手工维护 index，不返回实际长度；缓冲区过小时会静默截断。 */
char *admin_group_string(char *out, size_t sz, int indent,
			 const struct admin_group *ag)
{
	bool printed = false;
	size_t index = 2;
	int nb_print = 0;

	if (sz < index)
		return out;

	if (admin_group_explicit_zero(ag)) {
		snprintf(out, sz, "0x00000000");
		return out;
	}

	if (admin_group_zero(ag)) {
		snprintf(out, sz, "not-set");
		return out;
	}

	snprintf(out, sz, "0x");
	for (ssize_t i = ag->bitmap.m - 1; i >= 0; i--) {
		if (sz - index <= 0)
			break;
		if (ag->bitmap.data[i] == 0 && !printed)
			continue;
		if (nb_print != 0 && (nb_print % 4) == 0) {
			snprintf(&out[index], sz - index, "\n%*s", indent, "");
			index += indent + 1;
			snprintf(&out[index], sz - index, "0x%08x ",
				 ag->bitmap.data[i]);
			index += 2;
		} else
			snprintf(&out[index], sz - index, "%08x ",
				 ag->bitmap.data[i]);
		index += 9;
		nb_print++;
		printed = true;
	}
	return out;
}

/* 【格式化】把**裸的 32 位**标准管理组直接格式化成位号列表(如 "0, 3, 17")。
 * 参数 out    : 输出缓冲(需 >= ADMIN_GROUP_PRINT_MAX_SIZE)。
 * 参数 indent : 换行缩进空格数。
 * 参数 bitmap : 待格式化的 32 位值(注意：不是 struct admin_group)。
 * 返回：out。
 * 分支：bitmap==0 -> "not-set"；否则逐位扫描,输出**置位的位序号**,一行超过
 *   一个 4 字十六进制的宽度就换行。
 * 【易踩的坑】与 admin_group_print 不同,此函数按位号枚举(便于人读),且直接
 *   假定 out 有 ADMIN_GROUP_PRINT_MAX_SIZE 空间。 */
char *admin_group_standard_print(char *out, int indent, uint32_t bitmap)
{
	bool first = true;
	int bit, i;
	size_t ret, line_sz = 0, line_max_sz;

	out[0] = '\0';

	if (bitmap == 0) {
		snprintf(out, ADMIN_GROUP_PRINT_MAX_SIZE, "not-set");
		return out;
	}

	line_max_sz = strlen("0xffffffff ffffffff ffffffff ffffffff");

	for (i = 0; i < 32; i++) {
		bit = bitmap >> i & 1;
		if (bit == 0)
			continue;
		if (!first) {
			ret = snprintf(&out[strlen(out)],
				       ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out),
				       ", ");
			line_sz += ret;
		}
		if (line_sz >= line_max_sz) {
			snprintf(&out[strlen(out)],
				 ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out),
				 "\n%*s", indent, "");

			line_sz = 0;
		}
		ret = snprintf(&out[strlen(out)],
			       ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out), "%d",
			       i);
		line_sz += ret;
		first = false;
	}

	return out;
}

/* 【格式化】把 struct admin_group 格式化成位号列表(自动适配标准 / 扩展)。
 * 参数 out / indent 同上；ag 为位图。
 * 返回：out。
 * 分支：admin_group_size(ag)==0 -> "not-set"(即空位图)；否则遍历
 *   [0, size*WORD_SIZE) 逐个 admin_group_get 取位,输出置位的位号。
 * 【易踩的坑】循环中若 snprintf 返回值显示已写满缓冲,会把 out 清空并提前返回,
 *   属“缓冲区不足”的防御分支。 */
char *admin_group_print(char *out, int indent, const struct admin_group *ag)
{
	bool first = true;
	uint32_t i;
	size_t ret, line_sz = 0, line_max_sz;

	out[0] = '\0';

	if (admin_group_size(ag) == 0) {
		snprintf(out, ADMIN_GROUP_PRINT_MAX_SIZE, "not-set");
		return out;
	}

	line_max_sz = strlen("0xffffffff ffffffff ffffffff ffffffff");

	for (i = 0; i < (admin_group_size(ag) * WORD_SIZE); i++) {
		if (!admin_group_get(ag, i))
			continue;
		if (!first) {
			ret = snprintf(&out[strlen(out)],
				       ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out),
				       ", ");
			line_sz += ret;
		}
		if (line_sz >= line_max_sz) {
			snprintf(&out[strlen(out)],
				 ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out),
				 "\n%*s", indent, "");

			line_sz = 0;
		}
		ret = snprintf(&out[strlen(out)],
			       ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out), "%d",
			       i);
		line_sz += ret;
		if (ret >= (ADMIN_GROUP_PRINT_MAX_SIZE - strlen(out))) {
			out[0] = '\0';
			return out;
		}
		first = false;
	}

	return out;
}

/* 【比较】判断两个管理组是否**按内容相等**。
 * 参数 ag1, ag2 : 待比较位图。
 * 返回：内容相等为 true；否则 false。
 * 实现要点：以两者中较大的字数为界遍历；一侧字数较多时,多出来的字必须全 0 才
 *   算相等(因此“短的全 0”与“长的全 0 扩展”判为相等——刻意忽略长度差异)。
 * 【易踩的坑】直接访问 bitmap.m / bitmap.data,不做 bf_is_inited 断言。 */
bool admin_group_cmp(const struct admin_group *ag1,
		     const struct admin_group *ag2)
{
	size_t i;

	for (i = 0; i < ag1->bitmap.m || i < ag2->bitmap.m; i++) {
		if (i >= ag1->bitmap.m) {
			if (ag2->bitmap.data[i] != 0)
				return false;
		} else if (i >= ag2->bitmap.m) {
			if (ag1->bitmap.data[i] != 0)
				return false;
		} else if (memcmp(&ag1->bitmap.data[i], &ag2->bitmap.data[i],
				  sizeof(word_t)) != 0)
			return false;
	}

	return true;
}

/* 【拷贝】深拷贝 src 到 dst。
 * 参数 dst / src。
 * 返回：无。
 * 副作用：若 dst 已初始化,先 bf_free 释放其旧位图,再 bf_copy 复制。
 * 断言：src 必须已初始化。
 * 【易踩的坑】把 dst 当作可覆盖对象,故重复拷贝到同一 dst 不会泄漏(旧资源已释放)。 */
void admin_group_copy(struct admin_group *dst, const struct admin_group *src)
{
	assert(bf_is_inited(src->bitmap));
	if (bf_is_inited(dst->bitmap))
		bf_free(dst->bitmap);
	dst->bitmap = bf_copy(src->bitmap);
}

/* 【生命周期-建】初始化一个管理组(分配字长为 WORD_SIZE 的位图)。
 * 参数 ag。返回：无。断言：ag 必须**未**初始化(防重复 init 造成泄漏)。 */
void admin_group_init(struct admin_group *ag)
{
	assert(!bf_is_inited(ag->bitmap));
	bf_init(ag->bitmap, WORD_SIZE);
}

/* 【生命周期-拆】释放管理组的位图资源。
 * 参数 ag。返回：无。断言：ag 必须已初始化。
 * 说明：term 后若要继续使用须重新 init。 */
void admin_group_term(struct admin_group *ag)
{
	assert(bf_is_inited(ag->bitmap));
	bf_free(ag->bitmap);
}

/* 【单字读】取位图中第 oct_offset 个**字**的值(注意 oct_offset 是字下标,非位序号)。
 * 参数 ag, oct_offset。
 * 返回：该字的 32 位值；越界(oct_offset >= 已分配字数 m)返回 0。
 * 用途：把扩展位图换算成标准 32 位格式(取第 0 个字)。
 * 【易踩的坑】边界判断用的是容量 m 而非有效长度;越界返回 0 而不报错。 */
word_t admin_group_get_offset(const struct admin_group *ag, size_t oct_offset)
{
	assert(bf_is_inited(ag->bitmap));
	if (ag->bitmap.m < oct_offset)
		return 0;
	return ag->bitmap.data[oct_offset];
}

/* 【内部辅助】把位图容量扩到至少 idx+1 个字,并把新增的字清零。
 * 参数 ag, idx(目标最大字下标)。
 * 返回：无。
 * 副作用：更新 bitmap.m,用 XREALLOC(MTYPE_BITFIELD) 重分配 data,
 *   把新增区间 [old_m, m) memset 为 0。
 * 说明：set / bulk_set 在写入越界时调用它按需扩容。 */
static void admin_group_extend(struct admin_group *ag, size_t idx)
{
	size_t old_m, m;

	old_m = ag->bitmap.m;
	m = idx + 1;
	ag->bitmap.m = m;
	ag->bitmap.data =
		XREALLOC(MTYPE_BITFIELD, ag->bitmap.data, m * sizeof(word_t));
	memset(&ag->bitmap.data[old_m], 0, (m - old_m) * sizeof(word_t));
}

/* 【置位】把第 pos 位置 1。
 * 参数 ag, pos(位序号)。
 * 返回：无。
 * 副作用：若 pos 落在已分配范围外,先 admin_group_extend 扩容;置位后把 bitmap.n
 *   更新为至少 idx+1(记录已使用字数)。
 * 说明：n 的这一步增长使“显式设置”得以被 admin_group_explicit_zero 等识别。 */
void admin_group_set(struct admin_group *ag, size_t pos)
{
	size_t idx = bf_index(pos);

	if (idx >= ag->bitmap.m)
		admin_group_extend(ag, idx);

	ag->bitmap.data[idx] |= 1 << (bf_offset(pos));

	if (idx >= ag->bitmap.n)
		ag->bitmap.n = idx + 1;
}

/* 【清位】把第 pos 位清 0。
 * 参数 ag, pos。
 * 返回：无。
 * 逻辑：若 pos 所在字超出已分配范围(bf_index(pos) > m-1)直接返回；
 *   否则 bf_release_index 清位,并用 admin_group_size 重算 n。
 * 【易踩的坑】此处可能把 n 降到 0,从而抹掉“扩展长度”语义。 */
void admin_group_unset(struct admin_group *ag, size_t pos)
{
	if (bf_index(pos) > (ag->bitmap.m - 1))
		return;
	bf_release_index(ag->bitmap, pos);
	ag->bitmap.n = admin_group_size(ag);
}

/* 【取值】读第 pos 位,返回 0 / 1。
 * 参数 ag, pos。
 * 返回：位值(0 或 1)；有效长度为 0 或 pos 越界时返回 0。
 * 实现：先算有效长度,再取 pos 所在字,右移取最低位。
 * 【易踩的坑】最后一步写作 `oct_offset >> pos & 1`,按 C 标准,当 pos>=32 时移位量
 *   超出字宽属未定义行为；在 x86-64 上 CPU 会把移位量按 5 位取模,于是恰好等价于
 *   bf_offset(pos)。换架构阅读时需留意这一“靠硬件兜底”的写法。 */
int admin_group_get(const struct admin_group *ag, size_t pos)
{
	size_t admin_group_length = admin_group_size(ag);
	uint32_t oct_offset;
	size_t idx;

	if (admin_group_length == 0)
		return 0;

	idx = bf_index(pos);

	if (idx >= admin_group_length)
		return 0;

	oct_offset = admin_group_get_offset(ag, idx);
	return oct_offset >> pos & 1;
}

/* 【批量写入】在字下标 oct_offset 处**整字**写入 bitmap(一次铺 32 位)。
 * 参数 ag, bitmap(要写入的 32 位值), oct_offset(字下标,非位序号!)。
 * 返回：无。
 * 特殊分支：bitmap==0 且 oct_offset==0 -> 走 admin_group_allow_explicit_zero,
 *   即“把全 0 当作显式取值”记下来,而不是当作空。这是 RFC7308 显式 all-zero 的语义。
 * 一般路径：越界先扩容,写入该字,并把 bitmap.n 至少更新为 oct_offset+1。
 * 【易踩的坑】oct_offset 单位是“字”,与 set / unset 的 pos(位序号)单位不同。 */
void admin_group_bulk_set(struct admin_group *ag, uint32_t bitmap,
			  size_t oct_offset)
{

	if (bitmap == 0 && oct_offset == 0) {
		admin_group_allow_explicit_zero(ag);
		return;
	}

	if (oct_offset >= ag->bitmap.m)
		admin_group_extend(ag, oct_offset);

	ag->bitmap.data[oct_offset] = bitmap;

	if (oct_offset >= ag->bitmap.n)
		ag->bitmap.n = oct_offset + 1;
}

/* 【容量】返回“有效长度”,单位是**字**(word)。
 * 参数 ag。返回：最高非零字的下标 + 1;全 0 时返回 0。
 * 说明：它扫描 data 找最后一个非零字,故与写死的分配容量 m 不同；调用方常用它
 *   判断“标准 32 位 / 空”的边界(size==0 即空)。 */
size_t admin_group_size(const struct admin_group *ag)
{
	size_t size = 0;

	for (size_t i = 0; i < ag->bitmap.m; i++)
		if (ag->bitmap.data[i] != 0)
			size = i + 1;
	return size;
}

/* 【容量】返回已使用的字数,即 bitmap.n(由 set / bulk_set / 显式 0 开关维护)。
 * 参数 ag。返回：n。
 * 【易踩的坑】n 与 admin_group_size 不总是相等：显式全 0 时 n==1 而 size==0。 */
size_t admin_group_nb_words(const struct admin_group *ag)
{
	return ag->bitmap.n;
}

/* 【清空】把所有已分配的字清零,并把 n 归零。
 * 参数 ag。返回：无。
 * 说明：容量 m 不变(不释放内存),只是内容清空。 */
void admin_group_clear(struct admin_group *ag)
{
	for (size_t i = 0; i < ag->bitmap.m; i++)
		ag->bitmap.data[i] = 0;
	ag->bitmap.n = 0;
}

/* 【判定】判断所有位是否都为 0(内容层面,不管长度)。
 * 参数 ag。返回：全 0 为 true;存在任一非零字为 false。 */
bool admin_group_zero(const struct admin_group *ag)
{
	for (size_t i = 0; i < ag->bitmap.m; i++)
		if (ag->bitmap.data[i] != 0)
			return false;
	return true;
}


/* 【判定】判断是否为“显式全 0”:恰好用了 1 个字且该字为 0(n==1 且 data[0]==0)。
 * 参数 ag。返回：是则为 true。
 * 语义：用于区分“被显式设为全 0”(允许保存)与“从未设置 / 空”(not-set)。 */
bool admin_group_explicit_zero(const struct admin_group *ag)
{
	return ag->bitmap.n == 1 && ag->bitmap.data[0] == 0;
}

/* 【开关】允许把全 0 当作显式取值保存:若当前内容全 0,则把 n 置 1(制造出
 * “用了 1 个全 0 字”的状态,即 admin_group_explicit_zero 为真)。
 * 参数 ag。返回：无。副作用：可能修改 n。 */
void admin_group_allow_explicit_zero(struct admin_group *ag)
{
	if (admin_group_zero(ag))
		ag->bitmap.n = 1;
}

/* 【开关】禁止显式全 0:若当前内容全 0,则把 n 置 0(退回“空 / 未设置”状态)。
 * 参数 ag。返回：无。副作用：可能修改 n。 */
void admin_group_disallow_explicit_zero(struct admin_group *ag)
{
	if (admin_group_zero(ag))
		ag->bitmap.n = 0;
}

/* link_std_ag: admin-group in the RFC5305 section 3.1 format
 * link_ext_ag: admin-group in the RFC7308 format
 * RFC7308 specifies in section 2.3.1 that:
 * "If both an AG and EAG are present, a receiving node MUST use the AG
 * as the first 32 bits (0-31) of administrative color and use the EAG
 * for bits 32 and higher, if present."
 */
/* 【匹配-任意】判断 FAD 约束管理组与链路管理组是否**有任意一位相交**。
 * 参数 fad_ag      : 灵活算法定义里的约束管理组(必须非空,函数有 assert)。
 * 参数 link_std_ag : 链路的 RFC5305 标准 32 位管理组;NULL 表示未给出。
 * 参数 link_ext_ag : 链路的 RFC7308 扩展管理组;可为空位图。
 * 返回：有公共位则 true,否则 false。
 * 逻辑：
 *   1) 先按“标准 / 扩展是否出现”定出 link 的有效字数 link_ag_sz(两者都缺则 0);
 *   2) 在 [0, min(fad_sz, link_sz)) 内逐字做按位与,非 0 即命中;
 *   3) 第 0 个字且 link_std_ag 存在时,用标准值替代扩展位图的第 0 字
 *      —— 符合 RFC7308 2.3.1：两者同现时标准值充当低 32 位。
 * 【易踩的坑】link 全缺(link_ag_sz==0)时循环不执行,直接返回 false(视为不匹配)。 */
bool admin_group_match_any(const struct admin_group *fad_ag,
			   const uint32_t *link_std_ag,
			   const struct admin_group *link_ext_ag)
{
	size_t fad_ag_sz, link_ag_sz, i;
	uint32_t link_ag_bitmap, fad_ag_bitmap;

	assert(fad_ag);

	/* get the size of admin-groups: i.e. number of used words */
	fad_ag_sz = admin_group_size(fad_ag);
	if (link_std_ag && link_ext_ag) {
		link_ag_sz = admin_group_size(link_ext_ag);
		if (link_ag_sz == 0)
			link_ag_sz = 1;
	} else if (link_std_ag && !link_ext_ag)
		link_ag_sz = 1;
	else if (!link_std_ag && link_ext_ag)
		link_ag_sz = admin_group_size(link_ext_ag);
	else
		link_ag_sz = 0;

	for (i = 0; i < fad_ag_sz && i < link_ag_sz; i++) {
		fad_ag_bitmap = fad_ag->bitmap.data[i];
		if (i == 0 && link_std_ag)
			link_ag_bitmap = *link_std_ag;
		else
			link_ag_bitmap = link_ext_ag->bitmap.data[i];

		if (fad_ag_bitmap & link_ag_bitmap)
			return true;
	}
	return false;
}

/* same comments as admin_group_match_any() */
/* 【匹配-全部】判断 FAD 约束管理组的**每一位**是否都被链路覆盖。
 * 参数同 admin_group_match_any。
 * 返回：约束每一位都被满足为 true,否则 false。
 * 逻辑：
 *   1) 先求 link 有效字数(规则同上,见 match_any);
 *   2) 若 fad 比 link 还长(fad_ag_sz > link_ag_sz)则不可能全覆盖,直接返回 false;
 *   3) 逐字检查:跳过 FAD 里为 0 的字;否则要求该字是 link 对应字的子集
 *      ((fad & link) == fad)。
 * 与 match_any 的差异：这里是“全覆盖”判定,故先做长度可行性剪枝,再逐字求子集。 */
bool admin_group_match_all(const struct admin_group *fad_ag,
			   const uint32_t *link_std_ag,
			   const struct admin_group *link_ext_ag)
{
	size_t fad_ag_sz, link_ag_sz, i;
	uint32_t link_ag_bitmap, fad_ag_bitmap;

	assert(fad_ag);

	/* get the size of admin-groups: i.e. number of used words */
	fad_ag_sz = admin_group_size(fad_ag);
	if (link_std_ag && link_ext_ag) {
		link_ag_sz = admin_group_size(link_ext_ag);
		if (link_ag_sz == 0)
			link_ag_sz = 1;
	} else if (link_std_ag && !link_ext_ag)
		link_ag_sz = 1;
	else if (!link_std_ag && link_ext_ag)
		link_ag_sz = admin_group_size(link_ext_ag);
	else
		link_ag_sz = 0;

	if (fad_ag_sz > link_ag_sz)
		return false;

	for (i = 0; i < fad_ag_sz; i++) {
		fad_ag_bitmap = fad_ag->bitmap.data[i];
		if (fad_ag_bitmap == 0)
			continue;

		if (i == 0 && link_std_ag)
			link_ag_bitmap = *link_std_ag;
		else
			link_ag_bitmap = link_ext_ag->bitmap.data[i];

		if ((fad_ag_bitmap & link_ag_bitmap) != fad_ag_bitmap)
			return false;
	}
	return true;
}

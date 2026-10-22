// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ISO Network definition - iso_net.h
 *
 * Author: Olivier Dugeon <olivier.dugeon@orange.com>
 *
 * Copyright (C) 2023 Orange http://www.orange.com
 */

#ifndef LIB_ISO_H_
#define LIB_ISO_H_

/* ============================================================================
 * 【文件导读】iso.h —— ISO(IS-IS) 地址的最小公共定义
 * ============================================================================
 * 【定位】IS-IS 的地址体系源自 OSI：系统 ID(固定 6 字节)、区域地址(1~20 字节)、
 * NET(网络实体标题)。这些定义在 isisd 里用量极大，但 FRR 有别的模块也要引用
 * (如 Segment Routing 相关的公共代码)，于是把最小的一份抽到 lib/。注意本文件
 * **只有数据结构与格式化探针，不含任何算法**——真正的处理逻辑在 isisd 里。
 *
 * 【两种地址形态与字符串长度】这些长度宏都是**给栈上 char[] 用的安全上限**，
 * 开小了会静默截断：
 *   区域地址：点分十六进制 xx.xxxx.…，最长 47 字符 + 结尾符 = 51；
 *   系统 ID ：形如 xxxx.xxxx.xxxx-xx(含 NSEL 字节)，21 字符足够(含结尾符)。
 *
 * 【为什么用 typesafe 双链表】一个 isis_area 可以配置多个区域地址，用 FRR 的
 * typesafe DLIST 统一管理：类型安全、节点内联、零额外分配。注意 typesafe 的
 * 固定两步套路：先 PREDECL(预告类型，供结构体内嵌节点用)，结构体定义之后再
 * DECLARE(真正实例化)，两个顺序不能颠倒。
 *
 * 【末尾的 pragma 块】注册 printfrr 扩展转换符，让日志/回显能用一句格式串直接
 * 打印 ISO 地址(详见文件末尾的说明)。
 * ============================================================================
 */
#include "compiler.h"
#include "typesafe.h"

/* len of "xx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xx" + '\0' */
/* 【常量组：区域地址的长度】
 *   ISO_ADDR_STRLEN(51) : 区域地址**格式化后**的最长字符数加结尾符。按
 *                         “每个字节写成 'xx' 再加一个点”估算 20 个字节的上限。
 *                         用作格式化输出缓冲的容量。
 *   ISO_ADDR_MIN(8)     : 区域地址的**最小字节数**(合法的短区域地址)。
 *   ISO_ADDR_SIZE(20)   : 区域地址的**最大字节数**，同时是定长存储数组的容量。
 * 【易踩的坑】ISO_ADDR_SIZE 是数组容量而不是有效长度；有效长度看
 * struct iso_address 的 addr_len 字段。比较/打印/序列化时都必须带着 addr_len
 * 走，不能对整个定长数组做整体比较，否则会把尾部未清零的填充字节也算进去。 */
#define ISO_ADDR_STRLEN	51
#define ISO_ADDR_MIN	8
#define ISO_ADDR_SIZE	20

/* Predeclare typesafe list for isis_area->area_addrs */
PREDECL_DLIST(iso_address_list);

/* 【结构体】一条 ISO 区域地址。
 *   addr_len  : 有效字节数(取值范围 ISO_ADDR_MIN ~ ISO_ADDR_SIZE)。
 *   area_addr : 定长存储区，**只有前 addr_len 个字节有效**，其余字节的取值
 *               没有意义(不要假设它被清零)。
 *   item      : typesafe 链表节点，把本条地址挂到 isis_area 的 area_addrs 上。
 * 【易踩的坑】变长内容装在定长数组里，是这个结构体所有陷阱的来源：拷贝、比较、
 *   哈希、格式化都必须显式使用 addr_len。 */
struct iso_address {
	uint8_t addr_len;
	uint8_t area_addr[ISO_ADDR_SIZE];

	/* Typesafe list linkage for isis_area->area_addrs */
	struct iso_address_list_item item;
};

DECLARE_DLIST(iso_address_list, struct iso_address, item);

/* len of "xxxx.xxxx.xxxx.xx-xx" + '\0' */
/* 【常量】系统 ID 的格式化缓冲长度：“xxxx.xxxx.xxxx-xx” 的形式(12 个十六进制
 * 字符 + 2 个点 + 1 个连字符 + 2 个 NSEL 字符)再加结尾符，21 足够。 */
#define ISO_SYSID_STRLEN 21

/* 【printfrr 扩展注册】下面每个 pragma 向 FRR 自研的格式化引擎登记一个扩展
 * 转换符，并声明它期望的**实参类型**：
 *   %pSY : 系统 ID(实参 uint8_t 指针)
 *   %pPN : 见 isisd 中对应的打印实现
 *   %pLS : 链路状态相关数据(实参 uint8_t 指针)
 *   %pIS : ISO 区域地址(实参 struct iso_address 指针)
 * 具体打印函数分散在 isisd 等模块，通过 xref 元数据在运行期找到。
 * 【易踩的坑】这些 pragma 只在 _FRR_ATTRIBUTE_PRINTFRR 已定义时生效，作用是让
 *   编译器**按声明的类型检查格式串与实参**；缺了它，格式串传错类型不会被发现。 */
#ifdef _FRR_ATTRIBUTE_PRINTFRR
#pragma FRR printfrr_ext "%pSY" (uint8_t *)
#pragma FRR printfrr_ext "%pPN" (uint8_t *)
#pragma FRR printfrr_ext "%pLS" (uint8_t *)
#pragma FRR printfrr_ext "%pIS" (struct iso_address *)
#endif

#endif /* LIB_ISO_H_ */

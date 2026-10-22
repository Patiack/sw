// SPDX-License-Identifier: GPL-2.0-or-later
/* BGP dump to ascii converter
 * Copyright (C) 1999 Kunihiro Ishiguro
 */

/*
 * 文件导读：BGP 路由转储（MRT dump）到 ASCII 的转换工具
 *
 * 这是一个独立的小工具程序（bgp_btoa，binary to ascii），不是 bgpd 的常驻
 * 组件。它读取由 bgpd 通过 bgp_dump.c 写出的 MRT 格式转储文件（二进制），
 * 逐条解析出记录头与记录体，并以人类可读的文本打印到标准输出，便于离线查看
 * 与排错。它既处理表转储（TABLE_DUMP）记录，也处理 BGP4MP 消息记录。
 *
 * 与相邻模块的配合：本文件复用 bgp_dump.h 中定义的 MRT 类型与子类型常量、
 * bgp_attr.h 的属性类型常量，以及 bgp_aspath.c 的 aspath_parse 来解析 AS 路径；
 * 它自身只做“读文件、拆字段、打印”的工作，不修改任何路由状态。
 *
 * 读的顺序：先看 attr_parse 解析一条记录的属性部分，再看 main 里的主循环：
 * 先读 12 字节记录头（时间戳、类型、子类型、长度），再按类型分派读取记录体。
 */

#include <zebra.h>
#include <fcntl.h>

#include "zebra.h"
#include "stream.h"
#include "log.h"
#include "prefix.h"
#include "command.h"
#include "memory.h"
#include "privs.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_dump.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"

/*
 * 本工具运行所需的 capability 集合（绑定、原始套接字、网络管理等）。
 */
/* privileges */
static zebra_capabilities_t _caps_p[] = {
	ZCAP_BIND, ZCAP_NET_RAW, ZCAP_NET_ADMIN,
};

/*
 * 权限 / capability 配置结构：填写运行用户、组、vty 组与上文的能力数组，
 * 供 lib 的权限降级框架使用。
 */
struct zebra_privs_t bgpd_privs = {
#if defined(FRR_USER) && defined(FRR_GROUP)
	.user = FRR_USER,
	.group = FRR_GROUP,
#endif
#ifdef VTY_GROUP
	.vty_group = VTY_GROUP,
#endif
	.caps_p = _caps_p,
	.cap_num_p = array_size(_caps_p),
	.cap_num_i = 0,
};

/*
 * MRT 记录类型枚举（按 MRT 规范）。本工具关注 MSG_PROTOCOL_BGP4MP、
 * MSG_PROTOCOL_BGP4MP_ET 与 MSG_TABLE_DUMP，其余类型多为历史遗留。
 */
enum MRT_MSG_TYPES {
	MSG_NULL,
	MSG_START,		  /* sender is starting up */
	MSG_DIE,		  /* receiver should shut down */
	MSG_I_AM_DEAD,		  /* sender is shutting down */
	MSG_PEER_DOWN,		  /* sender's peer is down */
	MSG_PROTOCOL_BGP,	 /* msg is a BGP packet */
	MSG_PROTOCOL_RIP,	 /* msg is a RIP packet */
	MSG_PROTOCOL_IDRP,	/* msg is an IDRP packet */
	MSG_PROTOCOL_RIPNG,       /* msg is a RIPNG packet */
	MSG_PROTOCOL_BGP4PLUS,    /* msg is a BGP4+ packet */
	MSG_PROTOCOL_BGP4PLUS_01, /* msg is a BGP4+ (draft 01) packet */
	MSG_PROTOCOL_OSPF,	/* msg is an OSPF packet */
	MSG_TABLE_DUMP		  /* routing table dump */
};

/*
 * 解析一条记录体里的 BGP 路径属性段并打印。len 为本段总字节数，用当前位置
 * 加 len 算出上界 lim，循环读到 lim 为止。每个属性先读 flag 与 type，
 * 若带 EXTLEN 标志则长度为 2 字节，否则 1 字节。目前仅对 ORIGIN、AS_PATH、
 * NEXTHOP 三种做解析并打印；其余类型的属性用 stream_getw_from 按长度跳过。
 * 注意：这是调试工具，遇到任何非法输入不会做严格校验，可能打印异常结果。
 */
static void attr_parse(struct stream *s, uint16_t len)
{
	unsigned int flag;
	unsigned int type;
	uint16_t length;
	uint16_t lim;

	lim = s->getp + len;

	printf("%s s->getp %zd, len %d, lim %d\n", __func__, s->getp, len, lim);

	while (s->getp < lim) {
		flag = stream_getc(s);
		type = stream_getc(s);

		if (CHECK_FLAG(flag, BGP_ATTR_FLAG_EXTLEN))
			length = stream_getw(s);
		else
			length = stream_getc(s);

		printf("FLAG: %d\n", flag);
		printf("TYPE: %d\n", type);
		printf("Len: %d\n", length);

		switch (type) {
		case BGP_ATTR_ORIGIN: {
			uint8_t origin;
			origin = stream_getc(s);
			printf("ORIGIN: %d\n", origin);
		} break;
		case BGP_ATTR_AS_PATH: {
			struct aspath *aspath;

			aspath = aspath_parse(s, length, 1,
					      bgp_get_asnotation(NULL));
			if (aspath) {
				printf("ASPATH: %s\n", aspath->str);
				aspath_free(aspath);
			} else {
				printf("ASPATH: malformed\n");
			}
		} break;
		case BGP_ATTR_NEXT_HOP: {
			struct in_addr nexthop;
			nexthop.s_addr = stream_get_ipv4(s);
			printf("NEXTHOP: %pI4\n", &nexthop);
		} break;
		default:
			stream_getw_from(s, length);
			break;
		}
	}
}

/*
 * 程序入口。要求命令行给出一个参数（MRT 转储文件名），打开后进入主循环：
 * 每次先读 12 字节记录头（时间戳、类型、子类型、长度），打印头部信息；再按
 * 长度读取记录体。若记录类型为 TABLE_DUMP，则按表转储格式解析（view、序号，
 * 再循环读取前缀、状态、来源、属性等）；否则按 BGP4MP 消息格式解析对端 AS、
 * 接口、地址族与源 / 目的地址。文件读完或读取出错即退出，返回 0。
 * 注意：本工具假定输入格式正确，未做健壮性校验，畸形文件可能触发越界读取。
 */
int main(int argc, char **argv)
{
	int ret;
	int fd;
	struct stream *s;
	time_t now;
	int type;
	int subtype;
	size_t len;
	int source_as;
	int dest_as;
	ifindex_t ifindex;
	int family;
	struct in_addr sip;
	struct in_addr dip;
	uint16_t viewno, seq_num;
	struct prefix_ipv4 p;
	char tbuf[32];

	s = stream_new(10000);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s FILENAME\n", argv[0]);
		exit(1);
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stdout,
			"%% Can't open configuration file %s due to '%s'.\n",
			argv[1], safe_strerror(errno));
		exit(1);
	}

	while (1) {
		stream_reset(s);

		ret = stream_read(s, fd, 12);
		if (ret != 12) {
			if (!ret)
				printf("END OF FILE\n");
			else if (ret < 0)
				printf("ERROR OF READ\n");
			else
				printf("UNDERFLOW\n");
			break;
		}

		/* Extract header. */
		now = stream_getl(s);
		type = stream_getw(s);
		subtype = stream_getw(s);
		len = stream_getl(s);

		printf("TIME: %s", ctime_r(&now, tbuf));

		/* printf ("TYPE: %d/%d\n", type, subtype); */

		if (type == MSG_PROTOCOL_BGP4MP)
			printf("TYPE: BGP4MP");
		else if (type == MSG_PROTOCOL_BGP4MP_ET)
			printf("TYPE: BGP4MP_ET");
		else if (type == MSG_TABLE_DUMP)
			printf("TYPE: MSG_TABLE_DUMP");
		else
			printf("TYPE: Unknown %d", type);

		if (type == MSG_TABLE_DUMP)
			switch (subtype) {
			case AFI_IP:
				printf("/AFI_IP\n");
				break;
			case AFI_IP6:
				printf("/AFI_IP6\n");
				break;
			default:
				printf("/UNKNOWN %d", subtype);
				break;
			}
		else {
			switch (subtype) {
			case BGP4MP_STATE_CHANGE:
				printf("/CHANGE\n");
				break;
			case BGP4MP_MESSAGE:
				printf("/MESSAGE\n");
				break;
			case BGP4MP_ENTRY:
				printf("/ENTRY\n");
				break;
			case BGP4MP_SNAPSHOT:
				printf("/SNAPSHOT\n");
				break;
			default:
				printf("/UNKNOWN %d", subtype);
				break;
			}
		}

		printf("len: %zd\n", len);

		ret = stream_read(s, fd, len);
		if (ret != (int)len) {
			if (!ret)
				printf("END OF FILE 2\n");
			else if (ret < 0)
				printf("ERROR OF READ 2\n");
			else
				printf("UNDERFLOW 2\n");
			break;
		}

		/* printf ("now read %d\n", len); */

		if (type == MSG_TABLE_DUMP) {
			uint8_t status;
			time_t originated;
			struct in_addr peer;
			uint16_t attrlen;

			viewno = stream_getw(s);
			seq_num = stream_getw(s);
			printf("VIEW: %d\n", viewno);
			printf("SEQUENCE: %d\n", seq_num);

			/* start */
			while (s->getp < len - 16) {
				p.prefix.s_addr = stream_get_ipv4(s);
				p.prefixlen = stream_getc(s);
				printf("PREFIX: %pI4/%d\n", &p.prefix,
				       p.prefixlen);

				status = stream_getc(s);
				originated = stream_getl(s);
				peer.s_addr = stream_get_ipv4(s);
				source_as = stream_getw(s);

				printf("FROM: %pI4 AS%d\n", &peer, source_as);
				printf("ORIGINATED: %s", ctime_r(&originated,
								 tbuf));

				attrlen = stream_getw(s);
				printf("ATTRLEN: %d\n", attrlen);

				attr_parse(s, attrlen);

				printf("STATUS: 0x%x\n", status);
			}
		} else {
			source_as = stream_getw(s);
			dest_as = stream_getw(s);
			printf("source_as: %d\n", source_as);
			printf("dest_as: %d\n", dest_as);

			ifindex = stream_getw(s);
			family = stream_getw(s);

			printf("ifindex: %d\n", ifindex);
			printf("family: %d\n", family);

			sip.s_addr = stream_get_ipv4(s);
			dip.s_addr = stream_get_ipv4(s);

			printf("saddr: %pI4\n", &sip);
			printf("daddr: %pI4\n", &dip);

			printf("\n");
		}
	}
	close(fd);
	return 0;
}

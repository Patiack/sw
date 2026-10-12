/**
 @file ctc_goldengate_packet.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-11-25

 @version v2.0

 This file define sys functions

*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_debug.h"
#include "ctc_packet.h"
#include "ctc_parser.h"
#include "ctc_oam.h"
#include "ctc_qos.h"
#include "ctc_crc.h"
#include "ctc_goldengate_packet.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_packet.h"
#include "sys_goldengate_packet_priv.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_l3if.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_stacking.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_cpu_reason.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_chip_ctrl.h"
#include "goldengate/include/drv_chip_agent.h"
/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_PACKET_ENCAP_LOOP_NHPTR(lport,ttl_valid) ((((lport & 0x80)>>7) << 16) |(ttl_valid<<7)| (lport & 0x7F))
#define SYS_PACKET_BASIC_STACKING_HEADER_LEN 32

#define SYS_PACKET_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_pkt_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)


enum sys_pkt_ctag_action_e
{
    SYS_PKT_CTAG_ACTION_NONE,                  /* 0 */
    SYS_PKT_CTAG_ACTION_MODIFY,                /* 1 */
    SYS_PKT_CTAG_ACTION_ADD,                   /* 2 */
    SYS_PKT_CTAG_ACTION_DELETE,                /* 3 */
};
typedef enum sys_pkt_ctag_action_e sys_pkt_ctag_action_t;


/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
sys_pkt_master_t* p_gg_pkt_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

extern char* sys_goldengate_reason_2Str(uint16 reason_id);
extern int32 _sys_goldengate_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value);
/****************************************************************************
*
* Function
*
*****************************************************************************/
STATIC int32
_sys_goldengate_packet_dump(uint8 lchip, uint8* data, uint32 len)
{
    uint32 cnt = 0;
    char line[256] = {'\0'};
    char tmp[32] = {'\0'};

    if (0 == len)
    {
        return CTC_E_NONE;
    }

    for (cnt = 0; cnt < len; cnt++)
    {
        if ((cnt % 16) == 0)
        {
            if (cnt != 0)
            {
                SYS_PKT_DUMP("%s", line);
            }

            sal_memset(line, 0, sizeof(line));
            if (cnt == 0)
            {
                sal_sprintf(tmp, "0x%04x:  ", cnt);
            }
            else
            {
                sal_sprintf(tmp, "\n0x%04x:  ", cnt);
            }
            sal_strcat(line, tmp);
        }

        sal_sprintf(tmp, "%02x", data[cnt]);
        sal_strcat(line, tmp);

        if ((cnt % 2) == 1)
        {
            sal_strcat(line, " ");
        }
    }

    SYS_PKT_DUMP("%s", line);
    SYS_PKT_DUMP("\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_packet_dump_tx_header(uint8 lchip, ctc_pkt_tx_t  *p_tx_info)
{
    char* str_op_type[] = {"NORMAL", "LMTX", "E2ILOOP", "DMTX", "C2C", "PTP", "NAT", "OAM"};
    SYS_PKT_DUMP( "-----------------------------------------------\n");
    SYS_PKT_DUMP( "Packet Header Raw Info(Length : %d): \n", SYS_GG_PKT_HEADER_LEN);
    SYS_PKT_DUMP( "-----------------------------------------------\n");

    SYS_PKT_DUMP( "%-24s: 0x%.04x\n", "dest gport", p_tx_info->tx_info.dest_gport);

    /*source*/
    SYS_PKT_DUMP( "%-24s: 0x%.04x\n", "src port", p_tx_info->tx_info.src_port);
    SYS_PKT_DUMP( "%-24s: %d\n", "src svid", p_tx_info->tx_info.src_svid);
    SYS_PKT_DUMP( "%-24s: %d\n", "src cvid", p_tx_info->tx_info.src_cvid);
    SYS_PKT_DUMP( "%-24s: %d\n", "src cos", p_tx_info->tx_info.src_cos);
    SYS_PKT_DUMP( "%-24s: %d\n", "stag op", p_tx_info->tx_info.stag_op);
    SYS_PKT_DUMP( "%-24s: %d\n", "ctag op", p_tx_info->tx_info.ctag_op);
    SYS_PKT_DUMP( "%-24s: %d\n", "fid", p_tx_info->tx_info.fid);
    SYS_PKT_DUMP( "%-24s: %d\n", "ttl", p_tx_info->tx_info.ttl);
    SYS_PKT_DUMP( "%-24s: %s\n", "operation type", str_op_type[p_tx_info->tx_info.oper_type]);
    SYS_PKT_DUMP( "%-24s: %d\n", "priority", p_tx_info->tx_info.priority);
    SYS_PKT_DUMP( "%-24s: %d\n", "dest group id", p_tx_info->tx_info.dest_group_id);
    SYS_PKT_DUMP( "%-24s: %d\n", "color", p_tx_info->tx_info.color);
    SYS_PKT_DUMP( "%-24s: %d\n", "critical packet", p_tx_info->tx_info.is_critical);
    SYS_PKT_DUMP( "%-24s: %d\n", "nexthop offset", p_tx_info->tx_info.nh_offset);
    SYS_PKT_DUMP( "%-24s: %d\n", "hash ", p_tx_info->tx_info.hash);
    SYS_PKT_DUMP( "%-24s: %d\n", "vlan domain", p_tx_info->tx_info.vlan_domain);
    SYS_PKT_DUMP( "%-24s: %d\n", "svlan tpid", p_tx_info->tx_info.svlan_tpid);
    SYS_PKT_DUMP( "%-24s: %d\n", "nexthop id", p_tx_info->tx_info.nhid);
    SYS_PKT_DUMP( "%-24s: %d\n", "buffer log victim packet",CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_BUFFER_VICTIM_PKT));

    SYS_PKT_DUMP( "%-24s: %d\n", "MCAST", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_MCAST));
    SYS_PKT_DUMP( "%-24s: %d\n", "NH_OFFSET_BYPASS", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_NH_OFFSET_BYPASS));
    SYS_PKT_DUMP( "%-24s: %d\n", "NH_OFFSET_IS_8W", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_NH_OFFSET_IS_8W));
    SYS_PKT_DUMP( "%-24s: %d\n", "INGRESS_MODE", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_INGRESS_MODE));
    SYS_PKT_DUMP( "%-24s: %d\n", "CANCEL_DOT1AE", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_CANCEL_DOT1AE));
    SYS_PKT_DUMP( "%-24s: %d\n", "ASSIGN_DEST_PORT", CTC_FLAG_ISSET(p_tx_info->tx_info.flags, CTC_PKT_FLAG_ASSIGN_DEST_PORT));

    if (p_tx_info->tx_info.oper_type == CTC_PKT_OPER_OAM)
    {
        SYS_PKT_DUMP( "\n");
        SYS_PKT_DUMP( "oam info:\n");
        SYS_PKT_DUMP( "%-24s: %d\n", "oam type", p_tx_info->tx_info.oam.type);
        SYS_PKT_DUMP( "%-24s: %d\n", "mep index", p_tx_info->tx_info.oam.mep_index);
    }

    if (p_tx_info->tx_info.oper_type == CTC_PKT_OPER_PTP)
    {
        SYS_PKT_DUMP( "\n");
        SYS_PKT_DUMP( "ptp info:\n");
        SYS_PKT_DUMP( "%-24s: %u\n", "seconds", p_tx_info->tx_info.ptp.ts.seconds);
        SYS_PKT_DUMP( "%-24s: %u\n", "nanoseconds", p_tx_info->tx_info.ptp.ts.nanoseconds);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_packet_dump_header(uint8 lchip, ctc_pkt_rx_t  *p_rx_info)
{
    char* p_str_tmp = NULL;
    char str[40] = {0};

    SYS_PKT_DUMP("-----------------------------------------------\n");
    SYS_PKT_DUMP("Packet Header Info(Length : %d): \n", SYS_GG_PKT_HEADER_LEN);
    SYS_PKT_DUMP("-----------------------------------------------\n");

    p_str_tmp = sys_goldengate_reason_2Str(p_rx_info->rx_info.reason);
    sal_sprintf((char*)&str, "%s%s%d%s", p_str_tmp, "(ID:", p_rx_info->rx_info.reason, ")");

    SYS_PKT_DUMP("%-20s: %s\n", "cpu reason", (char*)&str);
    SYS_PKT_DUMP("%-20s: %d\n", "queue id", p_rx_info->rx_info.dest_gport);

    /*source*/
    SYS_PKT_DUMP("%-20s: 0x%.04x\n", "src port", p_rx_info->rx_info.src_port);
    SYS_PKT_DUMP("%-20s: 0x%.04x\n", "stacking src port", p_rx_info->rx_info.stacking_src_port);
    SYS_PKT_DUMP("%-20s: %d\n", "src svid", p_rx_info->rx_info.src_svid);
    SYS_PKT_DUMP("%-20s: %d\n", "src cvid", p_rx_info->rx_info.src_cvid);
    SYS_PKT_DUMP("%-20s: %d\n", "src cos", p_rx_info->rx_info.src_cos);
    SYS_PKT_DUMP("%-20s: %d\n", "stag op", p_rx_info->rx_info.stag_op);
    SYS_PKT_DUMP("%-20s: %d\n", "ctag op", p_rx_info->rx_info.ctag_op);
    SYS_PKT_DUMP("%-20s: %d\n", "vrf", p_rx_info->rx_info.vrfid);
    SYS_PKT_DUMP("%-20s: %d\n", "fid", p_rx_info->rx_info.fid);
    SYS_PKT_DUMP("%-20s: %d\n", "ttl", p_rx_info->rx_info.ttl);
    SYS_PKT_DUMP("%-20s: %d\n", "priority", p_rx_info->rx_info.priority);
    SYS_PKT_DUMP("%-20s: %d\n", "color", p_rx_info->rx_info.color);
    SYS_PKT_DUMP("%-20s: %d\n", "hash value", p_rx_info->rx_info.hash);
    SYS_PKT_DUMP("%-20s: %d\n", "critical packet", p_rx_info->rx_info.is_critical);
    SYS_PKT_DUMP("%-20s: %d\n", "packet type", p_rx_info->rx_info.packet_type);
    SYS_PKT_DUMP("%-20s: %d\n", "src chip", p_rx_info->rx_info.src_chip);
    SYS_PKT_DUMP("%-20s: %d\n", "payload offset", p_rx_info->rx_info.payload_offset);
    SYS_PKT_DUMP("%-20s: %d\n", "buffer log victim packet",CTC_FLAG_ISSET(p_rx_info->rx_info.flags, CTC_PKT_FLAG_BUFFER_VICTIM_PKT));
    SYS_PKT_DUMP("%-20s: %d\n", "operation type", p_rx_info->rx_info.oper_type);
    SYS_PKT_DUMP("%-20s: %d\n", "logic src port", p_rx_info->rx_info.logic_src_port);
    SYS_PKT_DUMP("%-20s: %d\n", "meta data", p_rx_info->rx_info.meta_data);
    SYS_PKT_DUMP("%-20s: %d\n", "mac unknown", CTC_FLAG_ISSET(p_rx_info->rx_info.flags, CTC_PKT_FLAG_UNKOWN_MACDA));
    SYS_PKT_DUMP("%-20s: 0x%x\n", "flags", p_rx_info->rx_info.flags);
    SYS_PKT_DUMP("%-20s: %d\n", "From internal port",CTC_FLAG_ISSET(p_rx_info->rx_info.flags, CTC_PKT_FLAG_INTERNAl_PORT));

    if (p_rx_info->rx_info.oper_type == CTC_PKT_OPER_OAM)
    {
        SYS_PKT_DUMP("\n");
        SYS_PKT_DUMP("oam info:\n");
        SYS_PKT_DUMP("%-20s: %d\n", "oam type", p_rx_info->rx_info.oam.type);
        SYS_PKT_DUMP("%-20s: %d\n", "mep index", p_rx_info->rx_info.oam.mep_index);
        SYS_PKT_DUMP("\n");
        SYS_PKT_DUMP("timestamp info:\n");
        SYS_PKT_DUMP("%-20s: %u\n", "seconds", p_rx_info->rx_info.oam.dm_ts.seconds);
        SYS_PKT_DUMP("%-20s: %u\n", "nanoseconds", p_rx_info->rx_info.oam.dm_ts.nanoseconds);
    }
    else if (p_rx_info->rx_info.oper_type == CTC_PKT_OPER_PTP)
    {
        SYS_PKT_DUMP("\n");
        SYS_PKT_DUMP("timestamp info:\n");
        SYS_PKT_DUMP("%-20s: %u\n", "seconds", p_rx_info->rx_info.ptp.ts.seconds);
        SYS_PKT_DUMP("%-20s: %u\n", "nanoseconds", p_rx_info->rx_info.ptp.ts.nanoseconds);
    }

    if ((p_rx_info->pkt_buf) && (p_rx_info->pkt_buf[0].data))
    {
        SYS_PKT_DUMP("\n");
        CTC_ERROR_RETURN(_sys_goldengate_packet_dump(lchip, (p_rx_info->pkt_buf[0].data), SYS_GG_PKT_HEADER_LEN));
    }

    SYS_PKT_DUMP("-----------------------------------------------\n");

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_packet_swap32(uint32* data, int32 len, uint32 hton)
{
    int32 cnt = 0;

    for (cnt = 0; cnt < len; cnt++)
    {
        if (hton)
        {
            data[cnt] = sal_htonl(data[cnt]);
        }
        else
        {
            data[cnt] = sal_ntohl(data[cnt]);
        }
    }

    return CTC_E_NONE;
}

char*
_sys_goldengate_packet_mode_str(uint32 mode)
{
    switch (mode)
    {
    case CTC_PKT_MODE_DMA:
        return "DMA";

    case CTC_PKT_MODE_ETH:
        return "ETH";

    default:
        return "Invalid";
    }
}

STATIC int32
_sys_goldengate_packet_bit62_to_ts(uint8 lchip, uint32 ns_only_format, uint64 ts_61_0, ctc_packet_ts_t* p_ts)
{
    if (ns_only_format)
    {
        /* [61:0] nano seconds */
        p_ts->seconds = ts_61_0 / 1000000000ULL;
        p_ts->nanoseconds = ts_61_0 % 1000000000ULL;
    }
    else
    {
        /* [61:30] seconds + [29:0] nano seconds */
        p_ts->seconds = (ts_61_0 >> 30) & 0xFFFFFFFFULL;
        p_ts->nanoseconds = ts_61_0 & 0x3FFFFFFFULL;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_packet_ts_to_bit62(uint8 lchip, uint32 ns_only_format, ctc_packet_ts_t* p_ts, uint64* p_ts_61_0)
{
    if (ns_only_format)
    {
        /* [61:0] nano seconds */
        *p_ts_61_0 = p_ts->seconds * 1000000000ULL + p_ts->nanoseconds;
    }
    else
    {
        /* [61:30] seconds + [29:0] nano seconds */
        *p_ts_61_0 = ((uint64)(p_ts->seconds) << 30) | ((uint64)p_ts->nanoseconds);
    }

    return CTC_E_NONE;
}




STATIC int
_sys_godengate_dword_reverse_copy(uint32* dest, uint32* src, uint32 len)
{
    uint32 i = 0;
    for (i = 0; i < len; i++)
    {
        *(dest + i) = *(src + (len - i - 1));
    }
    return 0;
}

STATIC int
_sys_godengate_byte_reverse_copy(uint8* dest, uint8* src, uint32 len)
{
    uint32 i = 0;
    for (i = 0; i < len; i++)
    {
        *(dest + i) = *(src + (len - 1 - i));
    }
    return 0;
}

STATIC int32
_sys_goldengate_packet_rx_dump(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    uint32 len = 0;

    if (CTC_BMP_ISSET(p_gg_pkt_master[lchip]->reason_bm, p_pkt_rx->rx_info.reason))
    {
        if (p_pkt_rx->pkt_len < SYS_GG_PKT_HEADER_LEN)
        {
            SYS_PKT_DUMP("Packet length is too small!!!Pkt len: %d\n",p_pkt_rx->pkt_len);
            return CTC_E_EXCEED_MIN_SIZE;
        }

        if (p_gg_pkt_master[lchip]->header_en[p_pkt_rx->rx_info.reason])
        {
            CTC_ERROR_RETURN(_sys_goldengate_packet_dump_header(lchip, p_pkt_rx));
        }

        if (p_pkt_rx->pkt_len - SYS_GG_PKT_HEADER_LEN < SYS_PKT_BUF_PKT_LEN)
        {
            len = p_pkt_rx->pkt_len- SYS_GG_PKT_HEADER_LEN;
        }
        else
        {
            len = SYS_PKT_BUF_PKT_LEN;
        }

        SYS_PKT_DUMP("\n-----------------------------------------------\n");
        SYS_PKT_DUMP("Packet Info(Length : %d):\n", (p_pkt_rx->pkt_len-SYS_GG_PKT_HEADER_LEN));
        SYS_PKT_DUMP("-----------------------------------------------\n");
        CTC_ERROR_RETURN(_sys_goldengate_packet_dump(lchip, (p_pkt_rx->pkt_buf[0].data+SYS_GG_PKT_HEADER_LEN), len));
    }

    return 0;
}

STATIC int32
_sys_goldengate_packet_get_svlan_tpid_index(uint8 lchip, uint16 svlan_tpid, uint8* svlan_tpid_index, sys_pkt_tx_hdr_info_t* p_tx_hdr_info)
{
    int32 ret = CTC_E_NONE;
    CTC_PTR_VALID_CHECK(p_tx_hdr_info);

    if ((0 == svlan_tpid) || (svlan_tpid == p_tx_hdr_info->tp_id[0]))
    {
        *svlan_tpid_index = 0;
    }
    else if (svlan_tpid == p_tx_hdr_info->tp_id[1])
    {
        *svlan_tpid_index = 1;
    }
    else if (svlan_tpid == p_tx_hdr_info->tp_id[2])
    {
        *svlan_tpid_index = 2;
    }
    else if (svlan_tpid == p_tx_hdr_info->tp_id[3])
    {
        *svlan_tpid_index = 3;
    }
    else
    {
        ret = CTC_E_INVALID_PARAM;
    }

    return ret;
}

STATIC int32
_sys_goldengate_packet_init_svlan_tpid_index(uint8 lchip)
{
    uint32 cmd = 0;
    ParserEthernetCtl_m parser_ethernet_ctl;
    int32 ret = CTC_E_NONE;

    sal_memset(&parser_ethernet_ctl, 0, sizeof(ParserEthernetCtl_m));
    cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_ethernet_ctl));

    p_gg_pkt_master[lchip]->tx_hdr_info.tp_id[0] = GetParserEthernetCtl(V, array_0_svlanTpid_f, &parser_ethernet_ctl);
    p_gg_pkt_master[lchip]->tx_hdr_info.tp_id[1] = GetParserEthernetCtl(V, array_1_svlanTpid_f, &parser_ethernet_ctl);
    p_gg_pkt_master[lchip]->tx_hdr_info.tp_id[2] = GetParserEthernetCtl(V, array_2_svlanTpid_f, &parser_ethernet_ctl);
    p_gg_pkt_master[lchip]->tx_hdr_info.tp_id[3] = GetParserEthernetCtl(V, array_3_svlanTpid_f, &parser_ethernet_ctl);

    return ret;
}

int32
sys_goldengate_packet_tx_set_property(uint8 lchip, uint16 type, uint32 value1, uint32 value2)
{
    int32 ret = CTC_E_NONE;
    uint32 value = 0;

    SYS_PACKET_INIT_CHECK(lchip);

    switch(type)
    {
        case SYS_PKT_TX_TYPE_TPID:
            p_gg_pkt_master[lchip]->tx_hdr_info.tp_id[value1] = value2;
            break;
        case SYS_PKT_TX_TYPE_STACKING_EN:
            value = value1?1:0;
            p_gg_pkt_master[lchip]->tx_hdr_info.stacking_en= value;
            break;
        case SYS_PKT_TX_TYPE_C2C_SUB_QUEUE_ID:
            p_gg_pkt_master[lchip]->tx_hdr_info.c2c_sub_queue_id = value1;
            break;
        case SYS_PKT_TX_TYPE_PTP_ADJUST_OFFSET:
            p_gg_pkt_master[lchip]->tx_hdr_info.offset_ns = value1;
            p_gg_pkt_master[lchip]->tx_hdr_info.offset_s = value2;
            break;
        case SYS_PKT_TX_TYPE_VLAN_PTR:
            p_gg_pkt_master[lchip]->tx_hdr_info.vlanptr[value1] = value2;
            break;
        default:
            ret = CTC_E_INVALID_PARAM;
            break;
    }

    return ret;
}

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!WARNING!!!!!!!!!!!!!!!!!!!!!!!!!!!
  Note that you should not add other module APIs in this function.
  If you need to add it, save it to the packet module database.*/
STATIC int32
_sys_goldengate_packet_txinfo_to_rawhdr(uint8 lchip, ctc_pkt_info_t* p_tx_info, uint32* p_raw_hdr_net, uint8 mode, uint8 user_flag, uint8* user_data)
{
    uint32 bheader[SYS_GG_PKT_HEADER_LEN/4] = {0};
    uint32* p_raw_hdr              = bheader;
    uint32 dest_map                = 0;
    uint16 vlan_ptr                = 0;
    uint8* pkt_data                = NULL;
    uint8 gchip                    = 0;
    uint32 lport                   = 0;
    uint8 hash                     = 0;
    uint8 src_gchip                = 0;
    uint8 packet_type              = 0;
    uint8 priority                 = 0;
    uint8 color                    = 0;
    uint32 src_port                = 0;
    uint32 next_hop_ptr            = 0;
    uint32 ts_37_0_tmp[2]                 = {0};
    uint64 ts_37_0                 = 0;
    uint64 ts_61_38                = 0;
    uint64 ts_61_0                 = 0;
    uint32 offset_ns = 0;
    uint32 offset_s = 0;
    uint64 offset = 0;
    uint8 svlan_tpid_index = 0;
    uint16 port_num = 0;
    uint8 destid_mode = 0;
    uint32 is_unknow_mcast = 0;
    uint32 gport = 0;
    /*TsEngineOffsetAdj_m ptp_offset_adjust;*/
    drv_work_platform_type_t platform_type = MAX_WORK_PLATFORM;
    sys_nh_info_dsnh_t      sys_nh_info;
    bool* p_protect_en = NULL;
    bool protect_en = FALSE;
    uint32 mep_index = 0;

    /*sal_memset(&ptp_offset_adjust, 0, sizeof(ptp_offset_adjust));*/
    sal_memset(p_raw_hdr, 0, SYS_GG_PKT_HEADER_LEN);

    if (p_tx_info->priority > SYS_QOS_CLASS_PRIORITY_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_MAX_VALUE_CHECK(p_tx_info->isolated_id, 31);

    drv_goldengate_get_platform_type(&platform_type);
    /* Must be inited */
    SetMsPacketHeader(V , packetType_f     , p_raw_hdr , p_tx_info->packet_type);
    SetMsPacketHeader(V , fromCpuOrOam_f   , p_raw_hdr , TRUE);
    SetMsPacketHeader(V , criticalPacket_f , p_raw_hdr , p_tx_info->is_critical);
    SetMsPacketHeader(V , sourceCos_f      , p_raw_hdr , p_tx_info->src_cos);
    SetMsPacketHeader(V , ttl_f            , p_raw_hdr , p_tx_info->ttl);
    SetMsPacketHeader(V , macKnown_f       , p_raw_hdr , TRUE);
    SetMsPacketHeader(V , fid_f, p_raw_hdr , p_tx_info->fid);
    SetMsPacketHeader(V , logicSrcPort_f, p_raw_hdr , p_tx_info->logic_src_port);
    SetMsPacketHeader(V , logicPortType_f, p_raw_hdr , p_tx_info->logic_port_type);

    CTC_ERROR_RETURN(_sys_goldengate_packet_get_svlan_tpid_index(lchip, p_tx_info->svlan_tpid, &svlan_tpid_index, &p_gg_pkt_master[lchip]->tx_hdr_info));
    SetMsPacketHeader(V , svlanTpidIndex_f, p_raw_hdr , svlan_tpid_index);

    /*Set oamuse fid lookup */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_OAM_USE_FID))
    {
        SetMsPacketHeader(V, oamUseFid_f, p_raw_hdr, TRUE);
    }

   /*Set Vlan domain */
    if (p_tx_info->vlan_domain == CTC_PORT_VLAN_DOMAIN_SVLAN)
    {
        SetMsPacketHeader(V, outerVlanIsCVlan_f, p_raw_hdr, FALSE);
        SetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr, p_tx_info->src_svid);
    }
    else  if (p_tx_info->vlan_domain == CTC_PORT_VLAN_DOMAIN_CVLAN)
    {
        SetMsPacketHeader(V, outerVlanIsCVlan_f, p_raw_hdr, TRUE);
        SetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr, p_tx_info->src_cvid);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /* PHB priority + color */
    priority = (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_PRIORITY)) ? p_tx_info->priority : SYS_QOS_CLASS_PRIORITY_MAX;
    SetMsPacketHeader(V, _priority_f, p_raw_hdr, priority);
    color = (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_COLOR)) ? p_tx_info->color : CTC_QOS_COLOR_GREEN;
    SetMsPacketHeader(V, color_f, p_raw_hdr, color);
    /* Next_hop_ptr and DestMap*/
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_BYPASS))
    {
        /* GG Bypass NH is 8W  and egress edit mode*/
        SetMsPacketHeader(V, bypassIngressEdit_f, p_raw_hdr, TRUE);
        SetMsPacketHeader(V, nextHopExt_f, p_raw_hdr, TRUE);
        CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &next_hop_ptr));
    }
    else
    {
        if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_VALID))
        {
            next_hop_ptr = p_tx_info->nh_offset;
            if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_IS_8W))
            {
                SetMsPacketHeader(V, nextHopExt_f, p_raw_hdr, TRUE);
            }
        }
        else
        {
            /* GG Bridge NH is 4W */
            SetMsPacketHeader(V, nextHopExt_f, p_raw_hdr, FALSE);
            CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &next_hop_ptr));
        }
    }

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_MCAST))
    {
        dest_map = SYS_ENCODE_MCAST_NON_IPE_DESTMAP(p_tx_info->dest_group_id);
        next_hop_ptr = 0;

        _sys_goldengate_l2_get_unknown_mcast_tocpu(lchip, p_tx_info->fid, &is_unknow_mcast);
    }
    else
    {
        if (CTC_IS_LINKAGG_PORT(p_tx_info->dest_gport))
        {
            dest_map = SYS_ENCODE_DESTMAP(CTC_LINKAGG_CHIPID, p_tx_info->dest_gport&0xFF);
        }
        else
        {
            gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
            dest_map = SYS_ENCODE_DESTMAP(gchip, SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_tx_info->dest_gport));

            sys_goldengate_queue_get_enqueue_info(lchip, &port_num, &destid_mode);
            if (0 == destid_mode)
            {
                if (CTC_PKT_OPER_C2C == p_tx_info->oper_type)
                {
                    if (sys_goldengate_chip_is_local(lchip, gchip))
                    {
                        dest_map |= SYS_QSEL_TYPE_CPU_TX << 9;
                        p_tx_info->oper_type = CTC_PKT_OPER_OAM;
                        p_tx_info->oam.type = CTC_OAM_TYPE_ETH;
                        CTC_SET_FLAG(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM);
                    }
                    else
                    {
                        uint8 sub_queue_id = 0;
                        uint16 grp_id = 0;
                        /*1. For C2C to 7148 must set bit7 for enable c2c packet enqueue by priority
                          2. For C2C to 5160 must set bit4 for enable c2c packet enqueue */
                        /*CTC_ERROR_RETURN(sys_goldengate_get_sub_queue_id_by_cpu_reason(lchip, CTC_PKT_CPU_REASON_C2C_PKT, &sub_queue_id));*/
                        sub_queue_id = p_gg_pkt_master[lchip]->tx_hdr_info.c2c_sub_queue_id;
                        grp_id = (sub_queue_id / 8) + (priority/8);
                        dest_map = SYS_ENCODE_DESTMAP(gchip, grp_id);
                        dest_map |= 0x90;
                        dest_map |= SYS_QSEL_TYPE_C2C << 9;
                    }
                }
                else
                {
                    dest_map |= SYS_QSEL_TYPE_REGULAR << 9;
                }
            }
        }

    }

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NHID_VALID))
    {
        sal_memset(&sys_nh_info, 0, sizeof(sys_nh_info));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_tx_info->nhid, &sys_nh_info));
        next_hop_ptr = sys_nh_info.dsnh_offset;
        if (sys_nh_info.nexthop_ext)
        {
             SetMsPacketHeader(V, nextHopExt_f, p_raw_hdr, TRUE);
        }
        dest_map = sys_nh_info.is_mcast ? SYS_ENCODE_MCAST_IPE_DESTMAP(0, sys_nh_info.dest_id)
                                        : SYS_ENCODE_DESTMAP(sys_nh_info.dest_chipid, sys_nh_info.dest_id);
        if (sys_nh_info.aps_en)
        {
            if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_APS_PATH_EN))
            {
                mep_index = p_tx_info->aps_p_path ? 0x3 : 0x1;
                protect_en = p_tx_info->aps_p_path;
                p_protect_en = &protect_en;

                if (!sys_nh_info.mep_on_tunnel)
                {
                    mep_index = 0;
                }
            }

            CTC_ERROR_RETURN(sys_goldengate_nh_get_aps_working_path(lchip, p_tx_info->nhid, &gport, &next_hop_ptr, p_protect_en));
            dest_map = SYS_ENCODE_DESTMAP(SYS_MAP_CTC_GPORT_TO_GCHIP(gport), SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport));
        }
    }
    SetMsPacketHeader(V, destMap_f, p_raw_hdr, dest_map);
    SetMsPacketHeader(V, nextHopPtr_f, p_raw_hdr, next_hop_ptr);
    SetMsPacketHeader(V, operationType_f  , p_raw_hdr , p_tx_info->oper_type);

    /* SrcPort */
    sys_goldengate_get_gchip_id(lchip, &src_gchip);
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_PORT_VALID))
    {
        src_port = p_tx_info->src_port;
    }
    else if (is_unknow_mcast)
    {
        src_port = CTC_MAP_LPORT_TO_GPORT(src_gchip, SYS_RSV_PORT_ILOOP_ID);
    }
    else
    {
        src_port = CTC_MAP_LPORT_TO_GPORT(src_gchip, SYS_RSV_PORT_OAM_CPU_ID);
    }
    src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(src_port);
    if ((CTC_IS_LINKAGG_PORT(p_tx_info->src_port))
        && (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_PORT_VALID)))
    {
        if(p_gg_pkt_master[lchip]->tx_hdr_info.stacking_en)
        {
            src_port &= 0x3E3F;
            src_port |= ((src_gchip >> 3) << 14) + ((src_gchip & 0x7) << 6);
        }
    }

    SetMsPacketHeader(V, sourcePort_f, p_raw_hdr, src_port);

    /* Svid */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID))
    {
        SetMsPacketHeader(V, srcVlanId_f, p_raw_hdr, p_tx_info->src_svid);
        SetMsPacketHeader(V, stagAction_f, p_raw_hdr, CTC_VLAN_TAG_OP_ADD);
        SetMsPacketHeader(V, svlanTagOperationValid_f, p_raw_hdr, TRUE);
    }

    /* Cvid */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID))
    {
        SetMsPacketHeader(V, u1_normal_srcCvlanId_f, p_raw_hdr, p_tx_info->src_cvid);
        SetMsPacketHeader(V, u1_normal_cTagAction_f, p_raw_hdr, CTC_VLAN_TAG_OP_ADD);
        SetMsPacketHeader(V, u1_normal_cvlanTagOperationValid_f, p_raw_hdr, TRUE);
    }

    /* isolated_id*/
    if (0 != p_tx_info->isolated_id)
    {
        SetMsPacketHeader(V, u1_normal_sourcePortIsolateId_f, p_raw_hdr, p_tx_info->isolated_id);
    }

    /* Linkagg Hash*/
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_HASH_VALID))
    {
        hash = p_tx_info->hash;
    }
    else
    {
        pkt_data = user_flag? user_data:((uint8*)p_raw_hdr_net + SYS_GG_PKT_HEADER_LEN);
        hash = ctc_crc_calculate_crc8(pkt_data, 12, 0);
    }
    SetMsPacketHeader(V, headerHash_f, p_raw_hdr, hash);

    /* Iloop */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_INGRESS_MODE))
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_tx_info->dest_gport);
        /* dest_map is ILOOP */
        dest_map = SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_ILOOP_ID);
        dest_map = dest_map | (lport&0x100); /*sliceId*/
        SetMsPacketHeader(V, destMap_f, p_raw_hdr, dest_map);

        /* nexthop_ptr is lport */
        next_hop_ptr = SYS_PACKET_ENCAP_LOOP_NHPTR(lport, 0);;
        SetMsPacketHeader(V, nextHopPtr_f, p_raw_hdr, next_hop_ptr);
    }

 /* OAM Operation*/
    if (CTC_PKT_OPER_OAM == p_tx_info->oper_type)
    {
        SetMsPacketHeader(V, u1_oam_mepIndex_f, p_raw_hdr, mep_index);

        SetMsPacketHeader(V, u1_oam_oamType_f, p_raw_hdr, p_tx_info->oam.type);
        SetMsPacketHeader(V, u1_oam_rxOam_f, p_raw_hdr, FALSE);
        SetMsPacketHeader(V, u1_oam_entropyLabelExist_f, p_raw_hdr, FALSE);
        SetMsPacketHeader(V, u1_oam_isEloopPkt_f, p_raw_hdr, FALSE);


        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM))
        {
            SetMsPacketHeader(V, u1_oam_dmEn_f, p_raw_hdr, TRUE);
            SetMsPacketHeader(V, u1_dmtx_dmOffset_f, p_raw_hdr, p_tx_info->oam.dm_ts_offset);
        }
        else
        {
            SetMsPacketHeader(V, u1_oam_dmEn_f, p_raw_hdr, FALSE);
        }

        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM))
        {
            SetMsPacketHeader(V, u1_oam_linkOam_f, p_raw_hdr, TRUE);
        }

        /* TPOAM Y.1731 Section need not append 13 label */
        if (CTC_OAM_TYPE_ACH == p_tx_info->oam.type)        {

            SetMsPacketHeader(V, u2_oam_mipEn_f, p_raw_hdr, TRUE);
            /* only ACH encapsulation need to change packet_type */
            if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_GAL))
            {
                SetMsPacketHeader(V, u1_oam_galExist_f, p_raw_hdr, TRUE);
                packet_type = CTC_PARSER_PKT_TYPE_MPLS;
            }
            else
            {
                packet_type = CTC_PARSER_PKT_TYPE_RESERVED;
            }
            SetMsPacketHeader(V, packetType_f, p_raw_hdr, packet_type);
        }

        /* set for UP MEP */
        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP))
        {
            if (CTC_OAM_TYPE_ETH != p_tx_info->oam.type)
            {
                /* only Eth OAM support UP MEP */
                return CTC_E_INVALID_DIR;
            }

            SetMsPacketHeader(V, u1_oam_isUp_f, p_raw_hdr, TRUE);
            gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
            lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_tx_info->dest_gport);
            /* dest_map is ILOOP */
            dest_map = SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_ILOOP_ID);
            dest_map = dest_map | (lport&0x100);/*SliceId*/
            SetMsPacketHeader(V, destMap_f, p_raw_hdr, dest_map);

            next_hop_ptr = SYS_PACKET_ENCAP_LOOP_NHPTR(lport, (p_tx_info->ttl != 0));
            SetMsPacketHeader(V, nextHopPtr_f, p_raw_hdr, next_hop_ptr);

            /* src vlan_ptr is vlan_ptr by vid */
            /*sys_goldengate_vlan_get_vlan_ptr(lchip, p_tx_info->oam.vid, &vlan_ptr);*/
            vlan_ptr = p_gg_pkt_master[lchip]->tx_hdr_info.vlanptr[p_tx_info->oam.vid];
            SetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr, vlan_ptr);

        }

        /* set bypass MIP port */
        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_TX_MIP_TUNNEL))
        {
            /* bypass OAM packet to MIP configured port; otherwise, packet will send to CPU again */
            SetMsPacketHeader(V, u1_oam_oamType_f, p_raw_hdr, CTC_OAM_TYPE_NONE);
            SetMsPacketHeader(V, oamTunnelEn_f, p_raw_hdr, TRUE);
        }
    }
    else if (CTC_PKT_OPER_PTP == p_tx_info->oper_type)
    {
        /* 7. get PTP */
        /*  DM Timestamp Bits Mapping
            Timestamp               Field Name          BIT Width   BIT Base
            dmTimestamp[37:0]       timestamp           38          0
            dmTimestamp[61:38]      u3_other_timestamp  24          38
         */
        _sys_goldengate_packet_ts_to_bit62(lchip, TRUE, &p_tx_info->ptp.ts, &ts_61_0);

        if (CTC_PTP_CORRECTION == p_tx_info->ptp.oper)
        {

            offset_ns = p_gg_pkt_master[lchip]->tx_hdr_info.offset_ns;
            offset_s = p_gg_pkt_master[lchip]->tx_hdr_info.offset_s;
            /*cmd = DRV_IOR(TsEngineOffsetAdj_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_offset_adjust));

            GetTsEngineOffsetAdj(A, offsetNs_f, &ptp_offset_adjust, &offset_ns);
            GetTsEngineOffsetAdj(A, offsetSecond_f, &ptp_offset_adjust, &offset_s);*/
            offset = offset_s * 1000000000ULL + offset_ns;
            if (ts_61_0 >= offset)
            {
                ts_61_0 = ts_61_0 -offset;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
        }

        ts_37_0 = ts_61_0 & 0x3FFFFFFFFFULL;
        ts_37_0_tmp[0] = (uint32)ts_37_0;
        ts_37_0_tmp[1] = (ts_37_0 >> 32);
        ts_61_38 = (ts_61_0 >> 38) & 0xFFFFFFULL;
        SetMsPacketHeader(A, timestamp_f, p_raw_hdr, &ts_37_0_tmp);
        SetMsPacketHeader(V, u3_other_timestamp_f, p_raw_hdr, ts_61_38);

        if (CTC_PTP_CAPTURE_ONLY == p_tx_info->ptp.oper)
        {
            SetMsPacketHeader(V, u1_ptp_ptpCaptureTimestamp_f, p_raw_hdr, TRUE);
            SetMsPacketHeader(V, u1_ptp_ptpSequenceId_f, p_raw_hdr, p_tx_info->ptp.seq_id);
        }
        else if (CTC_PTP_REPLACE_ONLY == p_tx_info->ptp.oper)
        {
            SetMsPacketHeader(V, u1_ptp_ptpReplaceTimestamp_f, p_raw_hdr, TRUE);
            SetMsPacketHeader(V, u1_ptp_ptpOffset_f, p_raw_hdr, p_tx_info->ptp.ts_offset);
        }
        else if (CTC_PTP_CORRECTION == p_tx_info->ptp.oper)
        {
            SetMsPacketHeader(V, u1_ptp_ptpReplaceTimestamp_f, p_raw_hdr, TRUE);
            SetMsPacketHeader(V, u1_ptp_ptpUpdateResidenceTime_f, p_raw_hdr, TRUE);
            SetMsPacketHeader(V, u1_ptp_ptpOffset_f, p_raw_hdr, p_tx_info->ptp.ts_offset);
        }
        else
        {
            /* CTC_PTP_NULL_OPERATION */
        }
    }
    else if (CTC_PKT_OPER_C2C == p_tx_info->oper_type)
    {
        SetMsPacketHeader(V, bypassIngressEdit_f, p_raw_hdr, 1);
        if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_MCAST))
        {
            SetMsPacketHeader(V, u1_c2c_c2cCheckDisable_f, p_raw_hdr, 1);
        }
    }

    if(CTC_PKT_MODE_ETH == mode)
    {
        /*6bytes MACDA + 6 bytes MACSA + 4 bytes VLAN Tag + 2 bytes EtherType + 40 bytes bridge header*/
        SetMsPacketHeader(V, packetOffset_f, p_raw_hdr, 58);
    }

    if (!CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_INGRESS_MODE))
    {
        SetMsPacketHeader(V, debugSessionEn_f, p_raw_hdr, 1);
    }

    if (platform_type == HW_PLATFORM)
    {
        CTC_ERROR_RETURN(_sys_godengate_dword_reverse_copy(p_raw_hdr_net, p_raw_hdr, SYS_GG_PKT_HEADER_LEN/4));
        CTC_ERROR_RETURN(_sys_goldengate_packet_swap32((uint32*)p_raw_hdr_net, SYS_GG_PKT_HEADER_LEN / 4, FALSE))
    }
    else
    {
        CTC_ERROR_RETURN(_sys_godengate_byte_reverse_copy((uint8*)p_raw_hdr_net, (uint8*)p_raw_hdr, SYS_GG_PKT_HEADER_LEN));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_packet_rawhdr_to_rxinfo(uint8 lchip, uint32* p_raw_hdr_net, ctc_pkt_rx_t* p_pkt_rx)
{
    uint32 bheader[SYS_GG_PKT_HEADER_LEN/4] = {0};
    uint32* p_raw_hdr = bheader;
    uint32 dest_map                = 0;
    uint64 ts_37_0                 = 0;
    uint64 ts_61_38                = 0;
    uint64 ts_61_0                 = 0;
    uint32 ts_37_0_tmp[2]                 = {0};
    uint32 src_port                = 0;
    uint32 next_hop_ptr            = 0;
    ctc_pkt_info_t* p_rx_info      = NULL;
    uint32 offset_ns = 0;
    uint32 offset_s = 0;
    uint16 c2c_queue_base = 0;
    uint32 cmd = 0;
    ctc_packet_ts_t ts;
    TsEngineOffsetAdj_m ptp_offset_adjust;

    p_rx_info = &p_pkt_rx->rx_info;

    CTC_ERROR_RETURN(_sys_godengate_dword_reverse_copy((uint32*)bheader, (uint32*)p_raw_hdr_net, SYS_GG_PKT_HEADER_LEN/4));
    CTC_ERROR_RETURN(_sys_goldengate_packet_swap32((uint32*)p_raw_hdr, SYS_GG_PKT_HEADER_LEN / 4, FALSE));

    sal_memset(p_rx_info, 0, sizeof(ctc_pkt_info_t));
    sal_memset(&ptp_offset_adjust, 0, sizeof(ptp_offset_adjust));
    sal_memset(&ts, 0, sizeof(ctc_packet_ts_t));


    /* Must be inited */
    p_rx_info->packet_type    = GetMsPacketHeader(V, packetType_f, p_raw_hdr);
    p_rx_info->oper_type      = GetMsPacketHeader(V, operationType_f, p_raw_hdr);
    p_rx_info->priority       = GetMsPacketHeader(V, _priority_f, p_raw_hdr);
    p_rx_info->color          = GetMsPacketHeader(V, color_f, p_raw_hdr);
    p_rx_info->src_cos        = GetMsPacketHeader(V, sourceCos_f, p_raw_hdr);
    p_rx_info->fid          = GetMsPacketHeader(V, fid_f, p_raw_hdr);
    p_rx_info->payload_offset = GetMsPacketHeader(V, packetOffset_f, p_raw_hdr);
    p_rx_info->ttl            = GetMsPacketHeader(V, ttl_f, p_raw_hdr);
    p_rx_info->is_critical    = GetMsPacketHeader(V, criticalPacket_f, p_raw_hdr);
    p_rx_info->logic_src_port  = GetMsPacketHeader(V, logicSrcPort_f, p_raw_hdr);

    if (!GetMsPacketHeader(V, macKnown_f, p_raw_hdr))
    {
        CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_UNKOWN_MACDA);
    }

    if (GetMsPacketHeader(V, u1_normal_metadataType_f, p_raw_hdr) == 0x01)
    {
        /*meta*/
        p_rx_info->meta_data= GetMsPacketHeader(V, u1_normal_metadata_f, p_raw_hdr);
        CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_METADATA_VALID);
    }
    else if (GetMsPacketHeader(V, u1_normal_metadataType_f, p_raw_hdr) == 0)
    {
        /*vrfid*/
        p_rx_info->vrfid= GetMsPacketHeader(V, u1_normal_metadata_f, p_raw_hdr);
        CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_VRFID_VALID);
    }

    src_port                  = GetMsPacketHeader(V, sourcePort_f, p_raw_hdr);
    if (SYS_DRV_IS_LINKAGG_PORT(src_port))
    {
        p_rx_info->src_chip = ((src_port >> 11) & 0x18 ) | ((src_port >> 6) & 0x07);
        p_rx_info->src_port = (src_port & 0x3E3F);  /* clear 0x3E3F for chipId */
    }
    else
    {
        p_rx_info->src_chip = SYS_DRV_GPORT_TO_GCHIP(src_port);
        p_rx_info->src_port = src_port;
    }

    p_rx_info->src_port = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(p_rx_info->src_port);
    p_rx_info->stacking_src_port = p_rx_info->src_port;
    p_rx_info->lport = CTC_MAX_UINT16_VALUE;
    /* RX Reason from next_hop_ptr */
    next_hop_ptr = GetMsPacketHeader(V, nextHopPtr_f, p_raw_hdr);
    p_rx_info->reason = CTC_PKT_CPU_REASON_GET_BY_NHPTR(next_hop_ptr);

    /* Queue Id */
    dest_map = GetMsPacketHeader(V, destMap_f, p_raw_hdr);
    p_rx_info->dest_gport = dest_map & 0xFF;

    /* Source svlan */
    p_rx_info->src_svid = GetMsPacketHeader(V, srcVlanId_f, p_raw_hdr);
    if ((p_rx_info->src_svid == 0) && (GetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr) < 4096))
    {
        p_rx_info->src_svid = GetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr);
    }

    if ((SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT+4096) == GetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr))
    {
        CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_INTERNAl_PORT);
    }

    if (p_rx_info->src_svid)
    {
        CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID);
    }

    /* Source cvlan */
    if (CTC_PKT_OPER_NORMAL == p_rx_info->oper_type)
    {
        p_rx_info->src_cvid = GetMsPacketHeader(V, u1_normal_srcCvlanId_f, p_raw_hdr);
        if (p_rx_info->src_cvid)
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID);
        }
    }


    if (GetMsPacketHeader(V, svlanTagOperationValid_f, p_raw_hdr))
    {
        switch (GetMsPacketHeader(V, stagAction_f, p_raw_hdr))
        {
        case SYS_PKT_CTAG_ACTION_MODIFY:
            p_rx_info->stag_op = CTC_VLAN_TAG_OP_REP;
            break;
        case SYS_PKT_CTAG_ACTION_ADD:
            p_rx_info->stag_op = CTC_VLAN_TAG_OP_ADD;
            break;
        case SYS_PKT_CTAG_ACTION_DELETE:
            p_rx_info->stag_op = CTC_VLAN_TAG_OP_DEL;
            break;
        }
    }

    if (CTC_PKT_OPER_NORMAL == p_rx_info->oper_type)
    {

        if (GetMsPacketHeader(V, u1_normal_cvlanTagOperationValid_f, p_raw_hdr))
        {
            switch (GetMsPacketHeader(V, u1_normal_cTagAction_f, p_raw_hdr))
            {
                case SYS_PKT_CTAG_ACTION_MODIFY:
                    p_rx_info->ctag_op = CTC_VLAN_TAG_OP_REP;
                    break;
                case SYS_PKT_CTAG_ACTION_ADD:
                    p_rx_info->ctag_op = CTC_VLAN_TAG_OP_ADD;
                    break;
                case SYS_PKT_CTAG_ACTION_DELETE:
                    p_rx_info->ctag_op = CTC_VLAN_TAG_OP_DEL;
                    break;
            }
        }
    }

    /*  Timestamp Bits Mapping
                Timestamp               Field Name          BIT Width   BIT Base
                dmTimestamp[37:0]       timestamp           38          0
                dmTimestamp[61:38]      u3_other_timestamp  24          38
    */
    GetMsPacketHeader(A, timestamp_f, p_raw_hdr, &ts_37_0_tmp);
    ts_37_0 = ts_37_0_tmp[1];
    ts_37_0 <<= 32;
    ts_37_0 |= ts_37_0_tmp[0];

    ts_61_38 = GetMsPacketHeader(V, u3_other_timestamp_f, p_raw_hdr);

    ts_61_0 = ((uint64)(ts_37_0) << 0)
            | ((uint64)(ts_61_38) << 38);

    /* for normal packet timestamp */
    cmd = DRV_IOR(TsEngineOffsetAdj_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_offset_adjust));

    GetTsEngineOffsetAdj(A, offsetNs_f, &ptp_offset_adjust, &offset_ns);
    GetTsEngineOffsetAdj(A, offsetSecond_f, &ptp_offset_adjust, &offset_s);

    if (CTC_PKT_OPER_OAM == p_rx_info->oper_type)
    {
        /* ts_61_0 is 32bit s + 30bit ns */
        _sys_goldengate_packet_bit62_to_ts(lchip, FALSE, ts_61_0, &ts);
        if (ts.nanoseconds + offset_ns >= 1000000000)
        {
            ts.nanoseconds = ts.nanoseconds + offset_ns - 1000000000;
            ts.seconds = ts.seconds + offset_s + 1;
        }
        else
        {
            ts.nanoseconds = ts.nanoseconds + offset_ns;
            ts.seconds = ts.seconds + offset_s;
        }
    }
    else
    {
        ts_61_0 = ts_61_0 + offset_s * 1000000000ULL + offset_ns;
        _sys_goldengate_packet_bit62_to_ts(lchip, TRUE, ts_61_0, &p_rx_info->ptp.ts);
    }


    /* OAM */
    if (CTC_PKT_OPER_OAM == p_rx_info->oper_type)
    {
        p_rx_info->oam.type = GetMsPacketHeader(V, u1_oam_oamType_f, p_raw_hdr);
        p_rx_info->oam.mep_index = GetMsPacketHeader(V, u1_oam_mepIndex_f, p_raw_hdr);

        if (GetMsPacketHeader(V, u1_oam_isUp_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP);
        }

        if (GetMsPacketHeader(V, u1_oam_lmReceivedPacket_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_LM);
        }

        if (GetMsPacketHeader(V, u1_oam_linkOam_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM);
        }

        if (GetMsPacketHeader(V, u1_oam_dmEn_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM);
        }

        if (GetMsPacketHeader(V, u1_oam_galExist_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_GAL);
        }

        if (GetMsPacketHeader(V, u2_oam_mipEn_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_MIP);
        }

        /* get timestamp offset in bytes for DM */
        if (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM))
        {
            p_rx_info->oam.dm_ts.seconds = ts.seconds;
            p_rx_info->oam.dm_ts.nanoseconds = ts.nanoseconds;
        }

        if (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_LM))
        {
            p_rx_info->oam.lm_fcl = GetMsPacketHeader(V, u3_oam_rxtxFcl_f, p_raw_hdr);
            p_rx_info->oam.lm_fcl = (p_rx_info->oam.lm_fcl << 8) + GetMsPacketHeader(V, u1_oam_rxtxFcl_f, p_raw_hdr);
        }

        /* get svid from vlan_ptr for Up MEP/Up MIP */
        if (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP))
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID);
            p_rx_info->src_svid = GetMsPacketHeader(V, srcVlanPtr_f, p_raw_hdr);
        }
    }
    else if (CTC_PKT_OPER_C2C == p_rx_info->oper_type)
    {
        /* Stacking*/
        p_rx_info->reason       = CTC_PKT_CPU_REASON_C2C_PKT;
        p_rx_info->stacking_src_port     = GetMsPacketHeader(V, u1_c2c_stackingSrcPort_f, p_raw_hdr);
        p_rx_info->stacking_src_port     = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(p_rx_info->stacking_src_port);

        if (CTC_FLAG_ISSET(p_rx_info->flags, CTC_PKT_FLAG_MCAST))
        {
            CTC_UNSET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_MCAST);
            p_rx_info->dest_group_id = 0;
        }
        p_rx_info->vrfid = 0;

        sys_goldengate_queue_get_c2c_queue_base(lchip, &c2c_queue_base);
        p_rx_info->dest_gport = p_rx_info->priority /((p_gg_queue_master[lchip]->priority_mode)? 1:4)+ c2c_queue_base;
    }

    if (p_rx_info->reason == CTC_PKT_CPU_REASON_MONITOR_BUFFER_LOG)
    {
        if (GetMsPacketHeader(V, sourceCfi_f, p_raw_hdr))
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_BUFFER_VICTIM_PKT);
        }
    }

    if (GetMsPacketHeader(V, fromFabric_f, p_raw_hdr) ||
        CTC_PKT_OPER_OAM == p_rx_info->oper_type)
    {
        p_pkt_rx->stk_hdr_len = SYS_PACKET_BASIC_STACKING_HEADER_LEN;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_rx(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    int32 ret = 0;
    SYS_PACKET_INIT_CHECK(lchip);
    p_pkt_rx->lchip = lchip;
    ret = (sys_goldengate_packet_decap(lchip, p_pkt_rx));
    if (ret < 0)
    {
        SYS_PKT_DUMP("packet decap Error!ret:%d\n", ret);
        /* return ret;*/
    }

    if (p_pkt_rx->rx_info.reason < CTC_PKT_CPU_REASON_MAX_COUNT)
    {
        p_gg_pkt_master[lchip]->stats.rx[p_pkt_rx->rx_info.reason]++;
    }

    _sys_goldengate_packet_rx_dump(lchip, p_pkt_rx);

    if ((CTC_OAM_EXCP_LEARNING_BFD_TO_CPU == (p_pkt_rx->rx_info.reason - CTC_PKT_CPU_REASON_OAM))
        ||(CTC_OAM_EXCP_BFD_TIMER_NEGOTIATION == (p_pkt_rx->rx_info.reason - CTC_PKT_CPU_REASON_OAM)))
    {
        if (p_gg_pkt_master[lchip]->oam_rx_cb)
        {
            p_gg_pkt_master[lchip]->oam_rx_cb(lchip, p_pkt_rx);
        }
    }

    if (p_gg_pkt_master[lchip]->buf_id  < SYS_PKT_BUF_MAX)
    {
        p_gg_pkt_master[lchip]->pkt_buf[p_gg_pkt_master[lchip]->buf_id].pkt_len = p_pkt_rx->pkt_len;
        sal_time(&(p_gg_pkt_master[lchip]->pkt_buf[p_gg_pkt_master[lchip]->buf_id].tm));

        if (p_pkt_rx->pkt_buf->len <= SYS_PKT_BUF_PKT_LEN)
        {
            sal_memcpy(p_gg_pkt_master[lchip]->pkt_buf[p_gg_pkt_master[lchip]->buf_id].pkt_data,
                       p_pkt_rx->pkt_buf->data,
                       p_pkt_rx->pkt_buf->len);
        }

        p_gg_pkt_master[lchip]->buf_id++;
        if (p_gg_pkt_master[lchip]->buf_id == SYS_PKT_BUF_MAX)
        {
            p_gg_pkt_master[lchip]->buf_id = 0;
        }

    }

    if (CTC_PKT_CPU_REASON_DIAG_DISCARD_PKT == p_pkt_rx->rx_info.reason)
    {
        if (p_pkt_rx->pkt_buf->len <= SYS_PKT_BUF_PKT_LEN)
        {
            sal_memcpy(p_gg_pkt_master[lchip]->diag_pkt_buf[p_gg_pkt_master[lchip]->diag_buf_id].pkt_data,
                       p_pkt_rx->pkt_buf->data,
                       p_pkt_rx->pkt_buf->len);
            p_gg_pkt_master[lchip]->diag_pkt_buf[p_gg_pkt_master[lchip]->diag_buf_id].pkt_len = p_pkt_rx->pkt_buf->len;
        }
        else
        {
            sal_memcpy(p_gg_pkt_master[lchip]->diag_pkt_buf[p_gg_pkt_master[lchip]->diag_buf_id].pkt_data,
                       p_pkt_rx->pkt_buf->data,
                       SYS_PKT_BUF_PKT_LEN);
            p_gg_pkt_master[lchip]->diag_pkt_buf[p_gg_pkt_master[lchip]->diag_buf_id].pkt_len = SYS_PKT_BUF_PKT_LEN;
        }

        p_gg_pkt_master[lchip]->diag_buf_id++;
        if (p_gg_pkt_master[lchip]->diag_buf_id == SYS_PKT_BUF_MAX)
        {
            p_gg_pkt_master[lchip]->diag_buf_id = 0;
        }
    }

    if (p_gg_pkt_master[lchip]->cfg.rx_cb)
    {
        p_gg_pkt_master[lchip]->cfg.rx_cb(p_pkt_rx);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_packet_stkhdr_to_rxinfo(uint8 lchip, uint32* p_stk_hdr_net, ctc_pkt_rx_t* p_pkt_rx)
{
    uint32 bheader[SYS_PACKET_BASIC_STACKING_HEADER_LEN/4] = {0};
    uint32* p_stk_hdr = bheader;
    uint32 src_port                = 0;
    ctc_pkt_info_t* p_rx_info      = NULL;

    p_rx_info = &p_pkt_rx->rx_info;

    CTC_ERROR_RETURN(_sys_godengate_dword_reverse_copy((uint32*)bheader, (uint32*)p_stk_hdr_net, SYS_PACKET_BASIC_STACKING_HEADER_LEN/4));
    CTC_ERROR_RETURN(_sys_goldengate_packet_swap32((uint32*)p_stk_hdr, SYS_PACKET_BASIC_STACKING_HEADER_LEN / 4, FALSE));

    src_port                  = GetPacketHeaderOuter(V, sourcePort_f, p_stk_hdr);
    if (SYS_DRV_IS_LINKAGG_PORT(src_port))
    {
        p_rx_info->src_chip = ((src_port >> 11) & 0x18 ) | ((src_port >> 6) & 0x07);
        p_rx_info->src_port = (src_port & 0x3E3F);  /* clear 0xC1C0 for chipId */
    }
    else
    {
        p_rx_info->src_chip = SYS_DRV_GPORT_TO_GCHIP(src_port);
        p_rx_info->src_port = src_port;
    }
    p_rx_info->src_port = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(p_rx_info->src_port);

    return CTC_E_NONE;
}

STATIC int32
sys_goldengate_packet_tx_alloc(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx, sys_pkt_tx_info_t** pp_pkt_info)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    uint8 user_flag = 0;

    *pp_pkt_info = NULL;
    p_pkt_info = (sys_pkt_tx_info_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_pkt_tx_info_t));
    if (NULL == p_pkt_info)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_pkt_info, 0, sizeof(sys_pkt_tx_info_t));
    p_pkt_info->callback = p_pkt_tx->callback;
    p_pkt_info->user_data = p_pkt_tx->user_data;
    sal_memcpy(&p_pkt_info->tx_info, &p_pkt_tx->tx_info, sizeof(ctc_pkt_info_t));
    p_pkt_info->p_pkt_tx = p_pkt_tx;
    p_pkt_info->lchip = p_pkt_tx->lchip;

    user_flag = ((p_pkt_tx->skb.head == NULL) ||
         (p_pkt_tx->skb.tail == NULL) ||
         (p_pkt_tx->skb.end == NULL)) &&
         (p_pkt_tx->skb.data != NULL);
    p_pkt_info->user_flag = user_flag;
    if (NULL != p_pkt_tx->callback)
    {
        p_pkt_info->is_async = TRUE;
    }

    if (user_flag)
    {
        p_pkt_info->header_len = p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM];
        if (p_pkt_info->is_async)
        {
            p_pkt_info->header = (uint8*)mem_malloc(MEM_SYSTEM_MODULE, p_pkt_info->header_len);
            if (NULL == p_pkt_info->header)
            {
                mem_free(p_pkt_info);
                return CTC_E_NO_MEMORY;
            }
            sal_memcpy(p_pkt_info->header, p_pkt_tx->skb.buf +(CTC_PKT_HDR_ROOM - p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM]), p_pkt_info->header_len);
        }
        else
        {
            p_pkt_info->header = p_pkt_tx->skb.buf +(CTC_PKT_HDR_ROOM - p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM]);
        }
        p_pkt_info->data = p_pkt_tx->skb.data;
        p_pkt_info->data_len = p_pkt_tx->skb.len;
    }
    else
    {
        p_pkt_info->data_len = p_pkt_tx->skb.len;
        if (p_pkt_info->is_async)
        {
            p_pkt_info->data = (uint8*)mem_malloc(MEM_SYSTEM_MODULE, p_pkt_info->data_len);
            if (NULL == p_pkt_info->data)
            {
                mem_free(p_pkt_info);
                return CTC_E_NO_MEMORY;
            }
            sal_memcpy(p_pkt_info->data, p_pkt_tx->skb.data, p_pkt_info->data_len);
        }
        else
        {
            p_pkt_info->data = p_pkt_tx->skb.data;
        }
    }

    *pp_pkt_info = p_pkt_info;

    return CTC_E_NONE;
}

STATIC void
sys_goldengate_packet_tx_free(uint8 lchip, sys_pkt_tx_info_t* p_pkt_info)
{

    if (p_pkt_info->user_flag)
    {
        if (p_pkt_info->is_async)
        {
            mem_free(p_pkt_info->header)
        }
    }
    else
    {

        if (p_pkt_info->is_async)
        {
            mem_free(p_pkt_info->data);

        }
    }

    mem_free(p_pkt_info);;

    return;
}

int32
sys_goldengate_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    uint8 is_skb_buf = 0;

    SYS_PACKET_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_pkt_tx);
    p_pkt_tx->lchip = lchip;

    if (p_pkt_tx->mode > CTC_PKT_MODE_ETH)
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    is_skb_buf = (((p_pkt_tx->skb.head == NULL) ||
                   (p_pkt_tx->skb.tail == NULL) ||
                   (p_pkt_tx->skb.end == NULL)) &&
                   (p_pkt_tx->skb.data != NULL) ? 0:1);

    CTC_ERROR_RETURN(sys_goldengate_packet_encap(lchip, p_pkt_tx));


    /*tx buf dump data store*/
    if (p_gg_pkt_master[lchip]->buf_id_tx< SYS_PKT_BUF_MAX)
    {
        sys_pkt_tx_buf_t* p_tx_buf = &p_gg_pkt_master[lchip]->pkt_tx_buf[p_gg_pkt_master[lchip]->buf_id_tx];
        uint32 pkt_len  = (p_pkt_tx->mode == CTC_PKT_MODE_DMA )?
                          (p_pkt_tx->skb.len+SYS_GG_PKT_HEADER_LEN):(p_pkt_tx->skb.len+SYS_GG_PKT_HEADER_LEN+SYS_GG_PKT_CPUMAC_LEN);
        p_tx_buf->mode  = p_pkt_tx->mode;
        p_tx_buf->pkt_len = pkt_len;

        sal_time(&p_tx_buf->tm);
        sal_memcpy(&p_tx_buf->tx_info,&(p_pkt_tx->tx_info),sizeof(ctc_pkt_info_t));

        pkt_len = (pkt_len >SYS_PKT_BUF_PKT_LEN) ?SYS_PKT_BUF_PKT_LEN:pkt_len;

        if (p_pkt_tx->mode == CTC_PKT_MODE_DMA )
        {
            if(is_skb_buf)
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],p_pkt_tx->skb.data, pkt_len);
            }
            else
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],&p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM - SYS_GG_PKT_HEADER_LEN], SYS_GG_PKT_HEADER_LEN);
               /*copy userdata*/
               sal_memcpy(&p_tx_buf->pkt_data[SYS_GG_PKT_HEADER_LEN],p_pkt_tx->skb.data, (pkt_len-SYS_GG_PKT_HEADER_LEN));
            }
        }
        else
        {
            if(is_skb_buf)
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],p_pkt_tx->skb.data, pkt_len);
            }
            else
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],&p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM - SYS_GG_PKT_HEADER_LEN -SYS_GG_PKT_CPUMAC_LEN],
                         (SYS_GG_PKT_CPUMAC_LEN+ SYS_GG_PKT_HEADER_LEN));

               /*copy userdata*/
               sal_memcpy(&p_tx_buf->pkt_data[SYS_GG_PKT_HEADER_LEN + SYS_GG_PKT_CPUMAC_LEN],p_pkt_tx->skb.data,
                         (pkt_len-SYS_GG_PKT_HEADER_LEN -SYS_GG_PKT_CPUMAC_LEN));
            }

        }

        p_gg_pkt_master[lchip]->buf_id_tx++;
        if (p_gg_pkt_master[lchip]->buf_id_tx == SYS_PKT_BUF_MAX)
        {
            p_gg_pkt_master[lchip]->buf_id_tx = 0;
        }

    }


    if (CTC_PKT_MODE_ETH == p_pkt_tx->mode)
    {
        if (p_gg_pkt_master[lchip]->cfg.socket_tx_cb )
        {
            CTC_ERROR_RETURN(p_gg_pkt_master[lchip]->cfg.socket_tx_cb(p_pkt_tx));
        }
        else
        {
            SYS_PKT_DUMP("Do not support Socket Tx!\n");
            return CTC_E_UNEXPECT;
        }
    }
    else if (CTC_PKT_MODE_DMA == p_pkt_tx->mode)
    {
        if (p_gg_pkt_master[lchip]->cfg.dma_tx_cb )
        {
            CTC_ERROR_RETURN(sys_goldengate_packet_tx_alloc(lchip, p_pkt_tx, &p_pkt_info));
            if (NULL == p_pkt_info)
            {
                return CTC_E_NO_MEMORY;
            }

            /*async send return*/
            if (p_pkt_info->is_async)
            {
                SYS_PKT_TX_LOCK(lchip);
                ctc_listnode_add_tail(p_gg_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info);
                SYS_PKT_TX_UNLOCK(lchip);
                CTC_ERROR_RETURN(sal_sem_give(p_gg_pkt_master[lchip]->tx_sem));
                return CTC_E_NONE;
            }
            CTC_ERROR_RETURN(p_gg_pkt_master[lchip]->cfg.dma_tx_cb((ctc_pkt_tx_t*)p_pkt_info));
            sys_goldengate_packet_tx_free(lchip, p_pkt_info);
        }
        else
        {
            SYS_PKT_DUMP("Do not support Dma Tx!\n");
            return CTC_E_UNEXPECT;
        }
    }

    if (CTC_FLAG_ISSET(p_pkt_tx->tx_info.flags, CTC_PKT_FLAG_MCAST))
    {
         p_gg_pkt_master[lchip]->stats.mc_tx++;
    }
    else
    {
         p_gg_pkt_master[lchip]->stats.uc_tx++;
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_packet_encap(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    ms_packet_header_t* p_raw_hdr = NULL;
    sys_goldengate_cpumac_header_t* p_cpumac_hdr = NULL;
    uint32 cmd = 0;
    IpeHeaderAdjustCtl_m ipe_header_adjust_ctl;
    uint16 tpid = 0;
    uint8 user_flag = 0;

    CTC_PTR_VALID_CHECK(p_pkt_tx);
    SYS_PACKET_INIT_CHECK(lchip);
    SYS_PKT_DBG_FUNC();
    SYS_PKT_DBG_INFO("[Packet Tx Info]",_sys_goldengate_packet_dump(lchip, p_pkt_tx->skb.data, p_pkt_tx->skb.len));
    SYS_PKT_DBG_INFO("Flags=0x%x, Dest port=0x%x, Oper_type=%d, Priority=%d, Color=%d, nh_offset=%d\n",
                    p_pkt_tx->tx_info.flags, p_pkt_tx->tx_info.dest_gport,
                    p_pkt_tx->tx_info.oper_type, p_pkt_tx->tx_info.priority,
                    p_pkt_tx->tx_info.color, p_pkt_tx->tx_info.nh_offset);

    user_flag = ((p_pkt_tx->skb.head == NULL) ||
                 (p_pkt_tx->skb.tail == NULL) ||
                 (p_pkt_tx->skb.end == NULL)) &&
                 (p_pkt_tx->skb.data != NULL);

    /* 1. encode packet header */
    if (user_flag)
    {
        p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM] = SYS_GG_PKT_HEADER_LEN;
        p_raw_hdr = (ms_packet_header_t*)(p_pkt_tx->skb.buf + CTC_PKT_HDR_ROOM - SYS_GG_PKT_HEADER_LEN);
    }
    else
    {
        p_raw_hdr = (ms_packet_header_t*)ctc_packet_skb_push(&p_pkt_tx->skb, SYS_GG_PKT_HEADER_LEN);
    }

    if (p_pkt_tx->callback)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_packet_txinfo_to_rawhdr(lchip, &p_pkt_tx->tx_info, (uint32*)p_raw_hdr, p_pkt_tx->mode, user_flag, p_pkt_tx->skb.data));

    if (CTC_PKT_MODE_ETH == p_pkt_tx->mode)
    {
        cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

        tpid = GetIpeHeaderAdjustCtl(V, headerTpid_f, &ipe_header_adjust_ctl);

        /* 2. encode CPUMAC Header */
        if (user_flag)
        {
            p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM] += SYS_GG_PKT_CPUMAC_LEN;
            p_cpumac_hdr = (sys_goldengate_cpumac_header_t*)(p_pkt_tx->skb.buf + CTC_PKT_HDR_ROOM - SYS_GG_PKT_HEADER_LEN - SYS_GG_PKT_CPUMAC_LEN);
        }
        else
        {
            p_cpumac_hdr = (sys_goldengate_cpumac_header_t*)ctc_packet_skb_push(&p_pkt_tx->skb, SYS_GG_PKT_CPUMAC_LEN);
        }
        p_cpumac_hdr->macda[0] = p_gg_pkt_master[lchip]->cpu_mac_sa[0];
        p_cpumac_hdr->macda[1] = p_gg_pkt_master[lchip]->cpu_mac_sa[1];
        p_cpumac_hdr->macda[2] = p_gg_pkt_master[lchip]->cpu_mac_sa[2];
        p_cpumac_hdr->macda[3] = p_gg_pkt_master[lchip]->cpu_mac_sa[3];
        p_cpumac_hdr->macda[4] = p_gg_pkt_master[lchip]->cpu_mac_sa[4];
        p_cpumac_hdr->macda[5] = p_gg_pkt_master[lchip]->cpu_mac_sa[5];
        p_cpumac_hdr->macsa[0] = p_gg_pkt_master[lchip]->cpu_mac_da[0];
        p_cpumac_hdr->macsa[1] = p_gg_pkt_master[lchip]->cpu_mac_da[1];
        p_cpumac_hdr->macsa[2] = p_gg_pkt_master[lchip]->cpu_mac_da[2];
        p_cpumac_hdr->macsa[3] = p_gg_pkt_master[lchip]->cpu_mac_da[3];
        p_cpumac_hdr->macsa[4] = p_gg_pkt_master[lchip]->cpu_mac_da[4];
        p_cpumac_hdr->macsa[5] = p_gg_pkt_master[lchip]->cpu_mac_da[5];
        p_cpumac_hdr->vlan_tpid = sal_htons(tpid);
        p_cpumac_hdr->vlan_vid = sal_htons(0x23);
        p_cpumac_hdr->type = sal_htons(0x5A5A);
        SYS_PKT_DBG_INFO("[Packet Tx] Ethernet mode\n");
    }

    /* add stats */
    p_gg_pkt_master[lchip]->stats.encap++;

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_decap(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    ds_t raw_hdr;
    ds_t* p_raw_hdr = &raw_hdr;
    ds_t stk_hdr;
    ds_t* p_stk_hdr = &stk_hdr;
    uint16 eth_hdr_len = 0;
    uint16 pkt_hdr_len = 0;
    uint16 stk_hdr_len = 0;

    CTC_PTR_VALID_CHECK(p_pkt_rx);
    SYS_PACKET_INIT_CHECK(lchip);
    SYS_PKT_DBG_FUNC();
    SYS_PKT_DBG_INFO("len %d, buf_count %d \n", p_pkt_rx->pkt_len,
                     p_pkt_rx->buf_count);

    /* 1. check packet length */
    /* ethernet has 20 Bytes L2 header */
    if (CTC_PKT_MODE_ETH == p_pkt_rx->mode)
    {
        eth_hdr_len = SYS_GG_PKT_CPUMAC_LEN;
    }
    else
    {
        eth_hdr_len = 0;
    }

    pkt_hdr_len = SYS_GG_PKT_HEADER_LEN;

    if (p_pkt_rx->pkt_len < (eth_hdr_len + pkt_hdr_len))
    {
        return DRV_E_WRONG_SIZE;
    }

    if (p_pkt_rx->pkt_buf[0].len < (eth_hdr_len + pkt_hdr_len))
    {
        return DRV_E_WRONG_SIZE;
    }

    p_pkt_rx->eth_hdr_len = eth_hdr_len;
    p_pkt_rx->pkt_hdr_len = pkt_hdr_len;
    p_pkt_rx->stk_hdr_len = stk_hdr_len;
    /* 2. decode raw header */
    sal_memcpy(p_raw_hdr, p_pkt_rx->pkt_buf[0].data + eth_hdr_len, SYS_GG_PKT_HEADER_LEN);

    /* 3. convert raw header to rx_info */
    CTC_ERROR_RETURN(_sys_goldengate_packet_rawhdr_to_rxinfo(lchip, (uint32*)p_raw_hdr, p_pkt_rx));

    /* 4. from fabric, get stacking header length */
    CTC_ERROR_RETURN(sys_goldengate_stacking_get_stkhdr_len(lchip, p_pkt_rx->rx_info.stacking_src_port, p_pkt_rx->rx_info.src_chip, &stk_hdr_len));

    if ((p_pkt_rx->stk_hdr_len == SYS_PACKET_BASIC_STACKING_HEADER_LEN)
        || (stk_hdr_len && ((CTC_PKT_CPU_REASON_L3_MTU_FAIL == p_pkt_rx->rx_info.reason)
        || (CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL == p_pkt_rx->rx_info.reason))))
    {
        if (p_pkt_rx->stk_hdr_len != SYS_PACKET_BASIC_STACKING_HEADER_LEN)
        {
            sal_memcpy(p_stk_hdr, p_pkt_rx->pkt_buf[0].data + eth_hdr_len+SYS_GG_PKT_HEADER_LEN+stk_hdr_len-SYS_PACKET_BASIC_STACKING_HEADER_LEN, SYS_PACKET_BASIC_STACKING_HEADER_LEN);
            CTC_ERROR_RETURN(_sys_goldengate_packet_stkhdr_to_rxinfo(lchip, (uint32*)p_stk_hdr, p_pkt_rx));
        }
        p_pkt_rx->stk_hdr_len = stk_hdr_len;
        if (CTC_PKT_OPER_OAM == p_pkt_rx->rx_info.oper_type &&
            stk_hdr_len != 0)
        {
            if (stk_hdr_len > SYS_PACKET_BASIC_STACKING_HEADER_LEN)
            {
                uint8 remove_len = 0;
                remove_len = (stk_hdr_len - SYS_PACKET_BASIC_STACKING_HEADER_LEN);

                sal_memmove(p_pkt_rx->pkt_buf[0].data + SYS_GG_PKT_HEADER_LEN,
                            p_pkt_rx->pkt_buf[0].data + SYS_GG_PKT_HEADER_LEN + remove_len,
                            p_pkt_rx->pkt_buf[0].len - SYS_GG_PKT_HEADER_LEN - remove_len);

                p_pkt_rx->pkt_len -= remove_len;
                p_pkt_rx->pkt_buf[0].len -= remove_len;
            }

            p_pkt_rx->stk_hdr_len = 0;
        }
        SYS_PKT_DBG_INFO("[Packet Rx] is from fabric\n");
    }

    /* add stats */
    p_gg_pkt_master[lchip]->stats.decap++;

    return CTC_E_NONE;
}

/**
 @brief Display packet
*/
int32
sys_goldengate_packet_stats_dump(uint8 lchip)
{
    uint16 idx = 0;
    uint64 rx = 0;
    char* p_str_tmp = NULL;
    char str[40] = {0};
    uint64 reason_cnt = 0;

    SYS_PACKET_INIT_CHECK(lchip);

    SYS_PKT_DUMP("Packet Tx Statistics:\n");
    SYS_PKT_DUMP("------------------------------\n");
    SYS_PKT_DUMP("%-20s: %"PRIu64" \n", "Total TX Count", (p_gg_pkt_master[lchip]->stats.uc_tx+p_gg_pkt_master[lchip]->stats.mc_tx));

    if ((p_gg_pkt_master[lchip]->stats.uc_tx) || (p_gg_pkt_master[lchip]->stats.mc_tx))
    {
        SYS_PKT_DUMP("--%-18s: %"PRIu64" \n", "Uc Count", p_gg_pkt_master[lchip]->stats.uc_tx);
        SYS_PKT_DUMP("--%-18s: %"PRIu64" \n", "Mc Count", p_gg_pkt_master[lchip]->stats.mc_tx);
    }

    SYS_PKT_DUMP("\n");

    for (idx  = 0; idx  < CTC_PKT_CPU_REASON_MAX_COUNT; idx ++)
    {
        rx += p_gg_pkt_master[lchip]->stats.rx[idx];
    }

    SYS_PKT_DUMP("Packet Rx Statistics:\n");
    SYS_PKT_DUMP("------------------------------\n");
    SYS_PKT_DUMP("%-20s: %"PRIu64" \n", "Total RX Count", rx);
    SYS_PKT_DUMP("\n");
    SYS_PKT_DUMP("%-20s\n", "Detail RX Count");

    if (rx)
    {
        for (idx  = 0; idx  < CTC_PKT_CPU_REASON_MAX_COUNT; idx ++)
        {
            reason_cnt = p_gg_pkt_master[lchip]->stats.rx[idx];

            if (reason_cnt)
            {
                p_str_tmp = sys_goldengate_reason_2Str(idx);
                sal_sprintf((char*)&str, "%s%s%d%s", p_str_tmp, "(ID:", idx, ")");

                SYS_PKT_DUMP("%-20s: %"PRIu64" \n", (char*)&str, reason_cnt);
            }
        }
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_packet_stats_clear(uint8 lchip)
{
    SYS_PACKET_INIT_CHECK(lchip);

    sal_memset(&p_gg_pkt_master[lchip]->stats, 0, sizeof(sys_pkt_stats_t));

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_set_reason_log(uint8 lchip, uint16 reason_id, uint8 enable, uint8 is_all, uint8 is_detail)
{
    uint16 index = 0;
    SYS_PACKET_INIT_CHECK(lchip);
    if ((reason_id >= CTC_PKT_CPU_REASON_MAX_COUNT) && !is_all)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (enable)
    {
        if (is_all)
        {
            for (index = 0; index < CTC_PKT_CPU_REASON_MAX_COUNT; index++)
            {
                CTC_BMP_SET(p_gg_pkt_master[lchip]->reason_bm, index);
                p_gg_pkt_master[lchip]->header_en[index] = is_detail;
            }
        }
        else
        {
            CTC_BMP_SET(p_gg_pkt_master[lchip]->reason_bm, reason_id);
            p_gg_pkt_master[lchip]->header_en[reason_id] = is_detail;
        }
    }
    else
    {
        if (is_all)
        {
            for (index = 0; index < CTC_PKT_CPU_REASON_MAX_COUNT; index++)
            {
                CTC_BMP_UNSET(p_gg_pkt_master[lchip]->reason_bm, index);
                p_gg_pkt_master[lchip]->header_en[index] = is_detail;
            }
        }
        else
        {
            CTC_BMP_UNSET(p_gg_pkt_master[lchip]->reason_bm, reason_id);
            p_gg_pkt_master[lchip]->header_en[reason_id] = is_detail;
        }
    }

    return CTC_E_NONE;
}

extern int32
_ctc_goldengate_dkit_memory_show_ram_by_data_tbl_id(uint32 tbl_id, uint32 index, uint32 * data_entry, char * regex);

static int32
_sys_goldengate_packet_dump_stacking_header(uint8 lchip, uint8* data, uint32 len, uint8 brife)
{
    uint8 *pkt_buf = NULL;
    uint8 pkt_hdr_out[32] = {0};
    int32 ret = 0;
    if (!data)
    {
        return CTC_E_INVALID_PARAM;
    }
    pkt_buf = mem_malloc(MEM_SYSTEM_MODULE, CTC_PKT_MTU);
    if (!pkt_buf)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pkt_buf, data, len);

    CTC_ERROR_GOTO((_sys_godengate_dword_reverse_copy((uint32*)pkt_hdr_out, (uint32*)pkt_buf, 32/4)), ret, error0);
    CTC_ERROR_GOTO((_sys_goldengate_packet_swap32((uint32*)pkt_hdr_out, 32 / 4, FALSE)), ret, error0);

    if (!brife)
    {
        SYS_PKT_DUMP("%-8s%-8s%-13s%-9s%-40s\n", "Index", "FldID", "Value", "BitLen", "FldName");
        SYS_PKT_DUMP("-------------------------------------------------------------\n");
        _ctc_goldengate_dkit_memory_show_ram_by_data_tbl_id(PacketHeaderOuter_t, 0, (uint32 *)pkt_hdr_out, NULL);
    }
    else
    {
        SYS_PKT_DUMP( "%-20s: %d\n", "Priority",  GetPacketHeaderOuter(V, _priority_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: 0x%08x\n", "DestMap", GetPacketHeaderOuter(V, destMap_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "HeaderType", GetPacketHeaderOuter(V, headerType_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "HeaderVersion", GetPacketHeaderOuter(V, headerVersion_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "LogicPortType", GetPacketHeaderOuter(V, logicPortType_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "LogicSrcPort", GetPacketHeaderOuter(V, logicSrcPort_f, pkt_hdr_out));
        if (CTC_PKT_OPER_C2C == GetPacketHeaderOuter(V, operationType_f, pkt_hdr_out))
        {
            SYS_PKT_DUMP( "%-20s: %d\n", "NextHopPtr", GetPacketHeaderOuter(V, nextHopPtr_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "NextHopExt", GetPacketHeaderOuter(V, nextHopExt_f, pkt_hdr_out));
        }
        else
        {
            SYS_PKT_DUMP("%-20s: %d\n", "CpuReason", CTC_PKT_CPU_REASON_GET_BY_NHPTR(GetPacketHeaderOuter(V, nextHopPtr_f, pkt_hdr_out)));
        }
        SYS_PKT_DUMP( "%-20s: %d\n", "OperationType", GetPacketHeaderOuter(V, operationType_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: 0x%04x\n", "SourcePort", GetPacketHeaderOuter(V, sourcePort_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "PacketType", GetPacketHeaderOuter(V, packetType_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "SrcVlanPtr", GetPacketHeaderOuter(V, srcVlanPtr_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "Ttl", GetPacketHeaderOuter(V, ttl_f, pkt_hdr_out));
        SYS_PKT_DUMP( "%-20s: %d\n", "PacketType", GetPacketHeaderOuter(V, packetType_f, pkt_hdr_out));
        if (CTC_PKT_OPER_OAM == GetPacketHeaderOuter(V, operationType_f, pkt_hdr_out))
        {
            SYS_PKT_DUMP("OAM info\n");
            SYS_PKT_DUMP("-------------------------------------------------------------\n");
            SYS_PKT_DUMP( "%-20s: %d\n", "DmEn", GetPacketHeaderOuter(V, u1_oam_dmEn_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "FromCpuOrOAM", GetPacketHeaderOuter(V, u1_oam_fromCpuOrOam_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "GalExist", GetPacketHeaderOuter(V, u1_oam_galExist_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "IsUp", GetPacketHeaderOuter(V, u1_oam_isUp_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "LinkOAM", GetPacketHeaderOuter(V, u1_oam_linkOam_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "MipEn", GetPacketHeaderOuter(V, u1_oam_mipEn_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "OAMType", GetPacketHeaderOuter(V, u1_oam_oamType_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "RxOAM", GetPacketHeaderOuter(V, u1_oam_rxOam_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "EntrypyLableExist", GetPacketHeaderOuter(V, u1_oam_entropyLabelExist_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "OAMLocalPhyPort", GetPacketHeaderOuter(V, u2_oam_localPhyPort_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "MepIndex", GetPacketHeaderOuter(V, u2_oam_mepIndex_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "MepIndex", GetPacketHeaderOuter(V, u2_oam_mepIndex_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "DestChipId", GetPacketHeaderOuter(V, u2_oam_oamDestChipId_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "Offset3to2", GetPacketHeaderOuter(V, u2_oam_oamPacketOffset_3to2_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "Offset4", GetPacketHeaderOuter(V, u2_oam_oamPacketOffset_4_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "Offset7to5", GetPacketHeaderOuter(V, u2_oam_oamPacketOffset_7to5_f, pkt_hdr_out));
        }
        if (CTC_PKT_OPER_PTP == GetPacketHeaderOuter(V, operationType_f, pkt_hdr_out))
        {
            SYS_PKT_DUMP("PTP info\n");
            SYS_PKT_DUMP("-------------------------------------------------------------\n");
            SYS_PKT_DUMP( "%-20s: %d\n", "PtpEditType", GetPacketHeaderOuter(V, u1_ptp_ptpEditType_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "PtpExtraOffset", GetPacketHeaderOuter(V, u1_ptp_ptpExtraOffset_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "PtpSequenceId", GetPacketHeaderOuter(V, u1_ptp_ptpSequenceId_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "PtpOffset", GetPacketHeaderOuter(V, u2_ptp_ptpOffset_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "Timestamp63to32", GetPacketHeaderOuter(V, u3_ptp_timestamp_63to32_f, pkt_hdr_out));
            SYS_PKT_DUMP( "%-20s: %d\n", "Timestamp31to0", GetPacketHeaderOuter(V, u4_ptp_timestamp_31to0_f, pkt_hdr_out));
        }
    }

error0:
    if (pkt_buf)
    {
        mem_free(pkt_buf);
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_packet_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag)
{
    uint8 buf_id = 0;
    uint8 idx = 0;
    uint16 len = 0;
    uint16 stk_hdr_len = 0;
    ctc_pkt_rx_t pkt_rx;
    ds_t raw_hdr;
    ds_t stk_hdr;
    char* p_time_str = NULL;

    SYS_PACKET_INIT_CHECK(lchip);

    CTC_MAX_VALUE_CHECK(buf_cnt, SYS_PKT_BUF_MAX);

    if (!CTC_IS_BIT_SET(flag, 0))
    {
        CTC_BIT_SET(flag, 0);
        buf_cnt = SYS_PKT_BUF_MAX;
    }

    buf_id = p_gg_pkt_master[lchip]->buf_id;
    while(buf_cnt)
    {
        buf_id = buf_id? (buf_id - 1):(SYS_PKT_BUF_MAX - 1);
        buf_cnt--;

        if (p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_len)
        {
            sal_memset(&pkt_rx, 0, sizeof(pkt_rx));
            sal_memset(raw_hdr, 0, sizeof(raw_hdr));
            sal_memcpy(raw_hdr, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data, SYS_GG_PKT_HEADER_LEN);
            /* convert raw header to rx_info */
            CTC_ERROR_RETURN(_sys_goldengate_packet_rawhdr_to_rxinfo(lchip, (uint32*)&raw_hdr, &pkt_rx));
            CTC_ERROR_RETURN(sys_goldengate_stacking_get_stkhdr_len(lchip, pkt_rx.rx_info.stacking_src_port, pkt_rx.rx_info.src_chip, &stk_hdr_len));
            if (stk_hdr_len && ((CTC_PKT_CPU_REASON_L3_MTU_FAIL == pkt_rx.rx_info.reason)
                || (CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL == pkt_rx.rx_info.reason)))
            {
                pkt_rx.stk_hdr_len = stk_hdr_len;
                sal_memcpy(stk_hdr, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data + SYS_GG_PKT_HEADER_LEN + stk_hdr_len - SYS_PACKET_BASIC_STACKING_HEADER_LEN, SYS_PACKET_BASIC_STACKING_HEADER_LEN);
                CTC_ERROR_RETURN(_sys_goldengate_packet_stkhdr_to_rxinfo(lchip, (uint32*)&stk_hdr, &pkt_rx));
            }

            SYS_PKT_DUMP("Packet No.%d, Pkt len: %d\n", idx++, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_len);
            SYS_PKT_DUMP( "-----------------------------------------------\n");
            p_time_str = sal_ctime(&(p_gg_pkt_master[lchip]->pkt_buf[buf_id].tm));
            SYS_PKT_DUMP( "Timestamp:%s", p_time_str);

            if (CTC_IS_BIT_SET(flag, 1))
            {
                if (CTC_IS_BIT_SET(flag, 3))
                {
                    uint8 buf[SYS_GG_PKT_HEADER_LEN] = {0};
                    uint8 real_buf[SYS_GG_PKT_HEADER_LEN] = {0};
                    sal_memcpy(buf, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data, SYS_GG_PKT_HEADER_LEN);
                    SYS_PKT_DUMP("-----------------------------------------------\n");
                    SYS_PKT_DUMP("Packet Header Raw Info(Length : %d): \n", SYS_GG_PKT_HEADER_LEN);
                    SYS_PKT_DUMP("-----------------------------------------------\n");
                    SYS_PKT_DUMP("%-8s%-8s%-13s%-9s%-40s\n", "Index", "FldID", "Value", "BitLen", "FldName");
                    SYS_PKT_DUMP("-------------------------------------------------------------\n");
                    CTC_ERROR_RETURN(_sys_godengate_dword_reverse_copy((uint32*)real_buf, (uint32*)buf, SYS_GG_PKT_HEADER_LEN/4));
                    CTC_ERROR_RETURN(_sys_goldengate_packet_swap32((uint32*)real_buf, SYS_GG_PKT_HEADER_LEN / 4, FALSE));
                    _ctc_goldengate_dkit_memory_show_ram_by_data_tbl_id(MsPacketHeader_t, 0, (uint32 *)real_buf, NULL);
                }
                else
                {
                    CTC_ERROR_RETURN(_sys_goldengate_packet_dump_header(lchip, &pkt_rx));
                }

            }

            if (CTC_IS_BIT_SET(flag, 2) && stk_hdr_len)
            {
                SYS_PKT_DUMP("-------------------------------------------------------------\n");
                SYS_PKT_DUMP("Stacking Header Info(Length : %d): \n", stk_hdr_len);
                SYS_PKT_DUMP("-------------------------------------------------------------\n");
                _sys_goldengate_packet_dump_stacking_header(lchip, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data+SYS_GG_PKT_HEADER_LEN, stk_hdr_len, !CTC_IS_BIT_SET(flag, 3));
            }

            if (CTC_IS_BIT_SET(flag, 0))
            {
                /*print packet header data*/
                SYS_PKT_DUMP("-----------------------------------------------\n");
                _sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data, SYS_GG_PKT_HEADER_LEN);
                SYS_PKT_DUMP("-----------------------------------------------\n");
                SYS_PKT_DUMP("Packet Info(Length : %d):\n", (p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_len-SYS_GG_PKT_HEADER_LEN));
                SYS_PKT_DUMP("-----------------------------------------------\n");

                /*print packet*/
                len = (p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_len <= SYS_PKT_BUF_PKT_LEN)?
                (p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_len-SYS_GG_PKT_HEADER_LEN): (SYS_PKT_BUF_PKT_LEN-SYS_GG_PKT_HEADER_LEN);
                CTC_ERROR_RETURN(_sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->pkt_buf[buf_id].pkt_data+SYS_GG_PKT_HEADER_LEN, len));
            }
        }

    }

    return CTC_E_NONE;

}

int32
sys_goldengate_packet_tx_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag)
{
    uint8 cursor = 0;
    uint8 idx = 0;
    uint8 len = 0;
    uint8 header_len = 0;
    char* p_time_str = NULL;

    LCHIP_CHECK(lchip);
    if (p_gg_pkt_master[lchip] == NULL)
    {
        SYS_PKT_DUMP( " Feature not initialized \n");
        return CTC_E_NOT_INIT;

    }

    CTC_MAX_VALUE_CHECK(buf_cnt, SYS_PKT_BUF_MAX);

    if (!CTC_IS_BIT_SET(flag, 0))
    {
        CTC_BIT_SET(flag, 0);
        buf_cnt = SYS_PKT_BUF_MAX;
    }
    cursor = (p_gg_pkt_master[lchip]->buf_id_tx);
    while(buf_cnt)
    {
        cursor = cursor? (cursor - 1):(SYS_PKT_BUF_MAX - 1);
        buf_cnt--;

        header_len = p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].mode?(SYS_GG_PKT_HEADER_LEN+SYS_GG_PKT_CPUMAC_LEN):SYS_GG_PKT_HEADER_LEN;

        if (!p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len)
        {
            continue;
        }

        SYS_PKT_DUMP( "Packet No.%d, Pkt len: %d\n", idx++, p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len);

        SYS_PKT_DUMP( "-----------------------------------------------\n");
        p_time_str = sal_ctime(&(p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].tm));
        SYS_PKT_DUMP( "Timestamp:%s", p_time_str);

        if (CTC_IS_BIT_SET(flag, 1))
        {
            ctc_pkt_tx_t* p_pkt_tx = NULL;
            ctc_pkt_info_t* p_pkt_info = NULL;
            uint8 cpu_mac_len = 0;

            p_pkt_tx = mem_malloc(MEM_SYSTEM_MODULE, sizeof(ctc_pkt_tx_t));
            if (NULL == p_pkt_tx)
            {
                return CTC_E_NO_MEMORY;
            }
            sal_memset(p_pkt_tx, 0, sizeof(ctc_pkt_tx_t));

            p_pkt_info = &(p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].tx_info);
            sal_memcpy(&(p_pkt_tx->tx_info),p_pkt_info ,sizeof(ctc_pkt_info_t));
            _sys_goldengate_packet_dump_tx_header(lchip, p_pkt_tx);
            mem_free(p_pkt_tx);

            if (p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].mode == CTC_PKT_MODE_ETH)
            {
                SYS_PKT_DUMP( "-----------------------------------------------\n");
                SYS_PKT_DUMP( "Packet CPU Header\n");
                SYS_PKT_DUMP( "-----------------------------------------------\n");

                _sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data, SYS_GG_PKT_CPUMAC_LEN);
                cpu_mac_len = SYS_GG_PKT_CPUMAC_LEN;
            }
            SYS_PKT_DUMP( "-----------------------------------------------\n");
            SYS_PKT_DUMP( "Packet Bridge Header\n");
            SYS_PKT_DUMP( "-----------------------------------------------\n");

            _sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data + cpu_mac_len, SYS_GG_PKT_HEADER_LEN);

        }

        if (CTC_IS_BIT_SET(flag, 0))
        {
            SYS_PKT_DUMP( "-----------------------------------------------\n");
            SYS_PKT_DUMP( "Packet Info(Length : %d):\n", (p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len -header_len));
            SYS_PKT_DUMP( "-----------------------------------------------\n");

            /*print packet*/
            len = (p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len <= SYS_PKT_BUF_PKT_LEN)?
            (p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len -header_len ): (SYS_PKT_BUF_PKT_LEN -header_len);
            _sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data+header_len, len);
        }

    }
    return CTC_E_NONE;
}

int32
sys_goldengate_packet_buffer_dump_discard_pkt(uint8 lchip, uint8 buf_cnt, uint8 flag)
{
    uint8 buf_id = 0;
    uint8 idx = 0;
    uint16 len = 0;

    SYS_PACKET_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(buf_cnt, SYS_PKT_BUF_MAX);

    if (!CTC_IS_BIT_SET(flag, 0))
    {
        CTC_BIT_SET(flag, 0);
        buf_cnt = SYS_PKT_BUF_MAX;
    }

    buf_id = p_gg_pkt_master[lchip]->diag_buf_id;

    while(buf_cnt)
    {
        buf_id = buf_id? (buf_id - 1):(SYS_PKT_BUF_MAX - 1);
        buf_cnt--;

        if (p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len)
        {
            SYS_PKT_DUMP("Packet No.%d, Pkt len: %d, Buf ID: %d\n", idx++, p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len, buf_id);

            if (CTC_IS_BIT_SET(flag, 1))
            {
                ctc_pkt_rx_t pkt_rx;
                ds_t raw_hdr;

                sal_memset(&pkt_rx, 0, sizeof(pkt_rx));
                sal_memset(raw_hdr, 0, sizeof(raw_hdr));
                sal_memcpy(raw_hdr, p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_data, SYS_GG_PKT_HEADER_LEN);

                /* convert raw header to rx_info */
                CTC_ERROR_RETURN(_sys_goldengate_packet_rawhdr_to_rxinfo(lchip, (uint32*)&raw_hdr, &pkt_rx));
                CTC_ERROR_RETURN(_sys_goldengate_packet_dump_header(lchip, &pkt_rx));

                _sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_data, SYS_GG_PKT_HEADER_LEN);
            }

            if (CTC_IS_BIT_SET(flag, 0))
            {
                SYS_PKT_DUMP("-----------------------------------------------\n");
                SYS_PKT_DUMP("Packet Info(Length : %d):\n", (p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len-SYS_GG_PKT_HEADER_LEN));
                SYS_PKT_DUMP("-----------------------------------------------\n");

                /*print packet*/
                len = (p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len <= SYS_PKT_BUF_PKT_LEN)?
                (p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len-SYS_GG_PKT_HEADER_LEN): (SYS_PKT_BUF_PKT_LEN-SYS_GG_PKT_HEADER_LEN);
                CTC_ERROR_RETURN(_sys_goldengate_packet_dump(lchip, p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_data+SYS_GG_PKT_HEADER_LEN, len));
            }
        }

    }

    return CTC_E_NONE;

}


int32
sys_goldengate_packet_buffer_clear(uint8 lchip ,uint8 bitmap)
{
    SYS_PACKET_INIT_CHECK(lchip);

    /*rx  buffer clear*/
    if (CTC_IS_BIT_SET(bitmap, 0))
    {
        sal_memset(&p_gg_pkt_master[lchip]->pkt_buf, 0, sizeof(p_gg_pkt_master[lchip]->pkt_buf));
        p_gg_pkt_master[lchip]->buf_id = 0;
    }

    /*tx  buffer clear*/
    if (CTC_IS_BIT_SET(bitmap, 1))
    {
        sal_memset(&p_gg_pkt_master[lchip]->pkt_tx_buf, 0, sizeof(p_gg_pkt_master[lchip]->pkt_tx_buf));
        p_gg_pkt_master[lchip]->buf_id = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_buffer_clear_discard_pkt(uint8 lchip)
{
    SYS_PACKET_INIT_CHECK(lchip);

    sal_memset(&p_gg_pkt_master[lchip]->diag_pkt_buf, 0, sizeof(p_gg_pkt_master[lchip]->diag_pkt_buf));
    p_gg_pkt_master[lchip]->diag_buf_id = 0;

    return CTC_E_NONE;
}


int32
sys_goldengate_packet_tx_discard_pkt(uint8 lchip, uint8 buf_id, ctc_pkt_tx_t* p_pkt_tx)
{
    uint8* p_data = NULL;
    uint32 pkt_len = 0;
    ctc_pkt_skb_t* p_skb = &(p_pkt_tx->skb);

    SYS_PACKET_INIT_CHECK(lchip);

    if (buf_id >= SYS_PKT_BUF_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (0 == p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len)
    {
        return CTC_E_INVALID_PARAM;
    }

    pkt_len = p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_len - SYS_GG_PKT_HEADER_LEN - 4;
    if (0 == pkt_len)
    {
        return CTC_E_INVALID_PARAM;
    }
    ctc_packet_skb_init(p_skb);
    p_data = ctc_packet_skb_put(p_skb, pkt_len);
    sal_memcpy(p_data, p_gg_pkt_master[lchip]->diag_pkt_buf[buf_id].pkt_data + SYS_GG_PKT_HEADER_LEN, pkt_len);

    CTC_ERROR_RETURN(sys_goldengate_packet_tx(lchip, p_pkt_tx));

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_register_tx_cb(uint8 lchip, CTC_PKT_TX_CALLBACK tx_cb)
{
    SYS_PACKET_INIT_CHECK(lchip);

    p_gg_pkt_master[lchip]->cfg.dma_tx_cb = tx_cb;

    return CTC_E_NONE;
}

#ifdef _SAL_LINUX_UM
STATIC int32
sys_goldengate_packet_eadp_dump_gg_header(uint8 lchip, ms_packet_header_t* p_header)
{
    SYS_PKT_DBG_INFO("\nGoldengate Header Information\n");
    SYS_PKT_DBG_INFO("------------------------------------------------------------\n");
    SYS_PKT_DBG_INFO("dest_map %d, source_port %d, logic_src_port %d, packet_type %d\n"
                 "next_hop_ptr %d, src_vlan_id %d, src_vlan_ptr %d, priority %d\n",
                 p_header->dest_map, p_header->source_port, (
                 p_header->logic_src_port_13_7<<7|p_header->logic_src_port_6_0), p_header->packet_type,
                 p_header->next_hop_ptr, p_header->src_vlan_id_11<<11|p_header->src_vlan_id_10_0,
                 p_header->src_vlan_ptr, p_header->priority);
    SYS_PKT_DBG_INFO("------------------------------------------------------------\n");

    return CTC_E_NONE;
}

int32
sys_goldengate_packet_eadp_rx_dump(uint8* pkt, uint32 mode, uint32 pkt_len)
{
    ms_packet_header_t header;
    uint8 lchip = 0;

    sal_memset(&header, 0, sizeof(header));
    CTC_ERROR_RETURN(_goldengate_chip_agent_decode_gg_header(pkt, pkt_len, &header));
    CTC_ERROR_RETURN(sys_goldengate_packet_eadp_dump_gg_header(lchip, &header));

    return CTC_E_NONE;
}

int32
sys_goldengate_set_pkt_rx_cb(uint8 lchip, CTC_PKT_RX_CALLBACK cb)
{
    SYS_PACKET_INIT_CHECK(lchip);

    p_gg_pkt_master[lchip]->cfg.rx_cb = cb;
    return CTC_E_NONE;
}


int32
sys_goldengate_get_pkt_rx_cb(uint8 lchip, void** cb)
{
    SYS_PACKET_INIT_CHECK(lchip);

    *cb = p_gg_pkt_master[lchip]->cfg.rx_cb;
    return CTC_E_NONE;
}
#endif

int32
sys_goldengate_packet_register_internal_rx_cb(uint8 lchip, SYS_PKT_RX_CALLBACK oam_rx_cb)
{
    SYS_PACKET_INIT_CHECK(lchip);

    p_gg_pkt_master[lchip]->oam_rx_cb = oam_rx_cb;
    return CTC_E_NONE;
}

STATIC void
_sys_goldengate_packet_tx_callback_thread(void *param)
{
    int32 ret = 0;
    uint8 lchip = 0;
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;
    uint32* header = NULL;

    lchip = (uintptr)param;
    while (1) {
        ret = sal_sem_take(p_gg_pkt_master[lchip]->tx_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
next_pkt:
        SYS_PKT_TX_LOCK(lchip); /* Grab the lists from the interrupt handler */
        CTC_LIST_LOOP_DEL(p_gg_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info, node, next_node)
        {
            ctc_listnode_delete_node(p_gg_pkt_master[lchip]->async_tx_pkt_list, node);
            if (CTC_FLAG_ISSET(p_pkt_info->tx_info.flags, CTC_PKT_FLAG_MCAST))
            {
                 p_gg_pkt_master[lchip]->stats.mc_tx++;
            }
            else
            {
                 p_gg_pkt_master[lchip]->stats.uc_tx++;
            }
            break;
        }
        SYS_PKT_TX_UNLOCK(lchip);
        if (NULL != p_pkt_info)
        {
            header =  (p_pkt_info->user_flag)?(uint32*)p_pkt_info->header:(uint32*)p_pkt_info->data;
            _sys_goldengate_packet_txinfo_to_rawhdr(lchip, &p_pkt_info->tx_info, (uint32*)header, CTC_PKT_MODE_DMA, p_pkt_info->user_flag, p_pkt_info->data);
            p_gg_pkt_master[lchip]->cfg.dma_tx_cb((ctc_pkt_tx_t*)p_pkt_info);
            (p_pkt_info->callback)(p_pkt_info->p_pkt_tx, p_pkt_info->user_data);
            sys_goldengate_packet_tx_free(lchip, p_pkt_info);
            p_pkt_info = NULL;
            goto next_pkt;
        }
    }

}

int32
sys_goldengate_packet_init(uint8 lchip, void* p_global_cfg)
{
    ctc_pkt_global_cfg_t* p_pkt_cfg = (ctc_pkt_global_cfg_t*)p_global_cfg;
    mac_addr_t mac_sa = {0xFE, 0xFD, 0x0, 0x0, 0x0, 0x1};
    mac_addr_t mac_da = {0xFE, 0xFD, 0x0, 0x0, 0x0, 0x0};
    int32 ret = 0;
    uint8 sub_queue_id = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    LCHIP_CHECK(lchip);
    /* 2. allocate interrupt master */
    if (p_gg_pkt_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_pkt_master[lchip] = (sys_pkt_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_pkt_master_t));
    if (NULL == p_gg_pkt_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_pkt_master[lchip], 0, sizeof(sys_pkt_master_t));
    sal_memcpy(&p_gg_pkt_master[lchip]->cfg, p_pkt_cfg, sizeof(ctc_pkt_global_cfg_t));
    sal_memcpy(p_gg_pkt_master[lchip]->cpu_mac_sa, mac_sa, sizeof(mac_sa));
    sal_memcpy(p_gg_pkt_master[lchip]->cpu_mac_da, mac_da, sizeof(mac_da));
#ifdef _SAL_LINUX_UM
    drv_goldengate_register_pkt_rx_cb(sys_goldengate_packet_eadp_rx_dump);
#endif
    sys_goldengate_packet_register_tx_cb(lchip, sys_goldengate_dma_pkt_tx);
#if 0
    sys_goldengate_get_chip_cpumac(lchip, p_gg_pkt_master[lchip]->cpu_mac_sa, p_gg_pkt_master[lchip]->cpu_mac_da);
#endif

    SYS_PKT_CREAT_TX_LOCK(lchip);

    p_gg_pkt_master[lchip]->async_tx_pkt_list= ctc_list_new();
    if (NULL == p_gg_pkt_master[lchip]->async_tx_pkt_list)
    {
        ret = CTC_E_NOT_INIT;
        goto roll_back_1;
    }

    CTC_ERROR_GOTO(sal_sem_create(&p_gg_pkt_master[lchip]->tx_sem, 0), ret, roll_back_2);

    sal_sprintf(buffer, "asyncTxTask-%d", lchip);
    CTC_ERROR_GOTO(sal_task_create(&p_gg_pkt_master[lchip]->p_async_tx_task, buffer,
        SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_goldengate_packet_tx_callback_thread, (void*)(uintptr)lchip), ret, roll_back_3);

    CTC_ERROR_GOTO(_sys_goldengate_packet_init_svlan_tpid_index(lchip), ret, roll_back_4);

    CTC_ERROR_GOTO(sys_goldengate_get_sub_queue_id_by_cpu_reason(lchip, CTC_PKT_CPU_REASON_C2C_PKT, &sub_queue_id), ret, roll_back_4);
    CTC_ERROR_GOTO(sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_C2C_SUB_QUEUE_ID, sub_queue_id, 0), ret, roll_back_4);

    return CTC_E_NONE;
roll_back_4:
    sal_task_destroy(p_gg_pkt_master[lchip]->p_async_tx_task);

roll_back_3:
    sal_sem_destroy(p_gg_pkt_master[lchip]->tx_sem);

roll_back_2:
    ctc_list_free(p_gg_pkt_master[lchip]->async_tx_pkt_list);

roll_back_1:
    sal_spinlock_destroy(p_gg_pkt_master[lchip]->tx_spinlock);
    mem_free(p_gg_pkt_master[lchip]);

    return ret;
}

int32
sys_goldengate_packet_deinit(uint8 lchip)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_pkt_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_sem_give(p_gg_pkt_master[lchip]->tx_sem);
    sal_task_destroy(p_gg_pkt_master[lchip]->p_async_tx_task);
    sal_sem_destroy(p_gg_pkt_master[lchip]->tx_sem);

    if (p_gg_pkt_master[lchip]->tx_spinlock)
    {
        ctc_sal_spinlock_destroy(p_gg_pkt_master[lchip]->tx_spinlock);
    }

    CTC_LIST_LOOP_DEL(p_gg_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info, node, next_node)
    {
        (p_pkt_info->callback)(p_pkt_info->p_pkt_tx, p_pkt_info->user_data);
        sys_goldengate_packet_tx_free(lchip, p_pkt_info);
        ctc_listnode_delete_node(p_gg_pkt_master[lchip]->async_tx_pkt_list, node);
    }
    ctc_list_free(p_gg_pkt_master[lchip]->async_tx_pkt_list);
    mem_free(p_gg_pkt_master[lchip]);

    return CTC_E_NONE;
}


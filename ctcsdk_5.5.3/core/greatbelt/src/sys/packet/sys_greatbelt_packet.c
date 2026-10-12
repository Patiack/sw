/**
 @file ctc_greatbelt_packet.c

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
#include "ctc_qos.h"
#include "ctc_oam.h"
#include "ctc_crc.h"
#include "ctc_greatbelt_packet.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_packet.h"
#include "sys_greatbelt_packet_priv.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_stacking.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_cpu_reason.h"
#include "sys_greatbelt_queue_enq.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_enum.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_GB_HDR_CRC_OFFSET       15
#define SYS_GB_HDR_IPSA_OFFSET      28
#define SYS_GB_PKT_STR      "Packet Module "
#define SYS_PACKET_BASIC_STACKING_HEADER_LEN 32

#define SYS_PACKET_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_pkt_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

#if (HOST_IS_LE == 0)
union sys_pkt_hdr_src_vlan_ptr_s
{
    struct
    {
        uint32 from_cpu_lm_down_disable     :1;
        uint32 ingress_edit_en         :1;
        uint32 outer_vlan_is_cvlan     :1;
        uint32 src_vlan_ptr            :13;
    }share1;

    uint32 rev_1                  :16;
};
typedef union sys_pkt_hdr_src_vlan_ptr_s sys_pkt_hdr_src_vlan_ptr_t;

#else

union sys_pkt_hdr_src_vlan_ptr_s
{
    struct
    {
        uint32 src_vlan_ptr            :13;
        uint32 outer_vlan_is_cvlan     :1;
        uint32 ingress_edit_en         :1;
        uint32 from_cpu_lm_down_disable     :1;
    }share1;

    uint32 rev_1                  :16;
};
typedef union sys_pkt_hdr_src_vlan_ptr_s sys_pkt_hdr_src_vlan_ptr_t;

#endif

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
sys_pkt_master_t* p_gb_pkt_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern char* sys_greatbelt_reason_2Str(uint8 lchip, uint16 reason_id);
extern int32
_sys_greatbelt_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value);

extern uint8
drv_greatbelt_get_host_type(void);
/****************************************************************************
*
* Function
*
*****************************************************************************/
STATIC int32
_sys_greatbelt_packet_dump(uint8 lchip, uint8* data, uint32 len)
{
    uint32 cnt = 0;
    char line[256];
    char tmp[32];

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
_sys_greatbelt_packet_dump_tx_header(uint8 lchip, ctc_pkt_tx_t  *p_tx_info)
{
    char* str_op_type[] = {"NORMAL", "LMTX", "E2ILOOP", "DMTX", "C2C", "PTP", "NAT", "OAM"};
    SYS_PKT_DUMP( "-----------------------------------------------\n");
    SYS_PKT_DUMP( "Packet Header Raw Info(Length : %d): \n", SYS_GB_PKT_HEADER_LEN);
    SYS_PKT_DUMP( "-----------------------------------------------\n");

    SYS_PKT_DUMP( "%-24s: %d\n", "dest gport", p_tx_info->tx_info.dest_gport);

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
        SYS_PKT_DUMP( "%-24s: %d\n", "seconds", p_tx_info->tx_info.ptp.ts.seconds);
        SYS_PKT_DUMP( "%-24s: %d\n", "nanoseconds", p_tx_info->tx_info.ptp.ts.nanoseconds);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_packet_dump_header(uint8 lchip, ctc_pkt_rx_t  *p_rx_info)
{
    char* p_str_tmp = NULL;
    char str[40] = {0};

    SYS_PKT_DUMP("-----------------------------------------------\n");
    SYS_PKT_DUMP("Packet Header Info(Length : %d): \n", SYS_GB_PKT_HEADER_LEN);
    SYS_PKT_DUMP("-----------------------------------------------\n");

    p_str_tmp = sys_greatbelt_reason_2Str(lchip, p_rx_info->rx_info.reason);
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

    if (p_rx_info->rx_info.oper_type == CTC_PKT_OPER_OAM)
    {
        SYS_PKT_DUMP("\n");
        SYS_PKT_DUMP("oam info:\n");
        SYS_PKT_DUMP("%-20s: %d\n", "oam type", p_rx_info->rx_info.oam.type);
        SYS_PKT_DUMP("%-20s: %d\n", "mep index", p_rx_info->rx_info.oam.mep_index);
    }

    if (p_rx_info->rx_info.oper_type == CTC_PKT_OPER_PTP)
    {
        SYS_PKT_DUMP("\n");
        SYS_PKT_DUMP("ptp info:\n");
        SYS_PKT_DUMP("%-20s: %d\n", "seconds", p_rx_info->rx_info.ptp.ts.seconds);
        SYS_PKT_DUMP("%-20s: %d\n", "nanoseconds", p_rx_info->rx_info.ptp.ts.nanoseconds);
    }

    if ((p_rx_info->pkt_buf) && (p_rx_info->pkt_buf[0].data))
    {
        SYS_PKT_DUMP("\n");
        CTC_ERROR_RETURN(_sys_greatbelt_packet_dump(lchip, (p_rx_info->pkt_buf[0].data), SYS_GB_PKT_HEADER_LEN));
    }

    SYS_PKT_DUMP("-----------------------------------------------\n");

    return CTC_E_NONE;
}
int32
sys_greatbelt_packet_swap32(uint8 lchip, uint32* data, int32 len, uint32 hton)
{
    int32 cnt;

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
_sys_greatbelt_packet_mode_str(uint32 mode)
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
_sys_greatbelt_packet_bit62_to_ts(uint8 lchip, uint32 ns_only_format, uint64 ts_61_0, ctc_packet_ts_t* p_ts)
{
    if (ns_only_format)
    {
        /* [61:0] nano seconds */
        p_ts->seconds = ts_61_0 / 1000000000;
        p_ts->nanoseconds = ts_61_0 % 1000000000;
    }
    else
    {
        /* [61:30] seconds + [29:0] nano seconds */
        p_ts->seconds = (ts_61_0 >> 30) & 0xFFFFFFFF;
        p_ts->nanoseconds = ts_61_0 & 0x3FFFFFFF;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_packet_ts_to_bit62(uint8 lchip, uint32 ns_only_format, ctc_packet_ts_t* p_ts, uint64* p_ts_61_0)
{
    if (ns_only_format)
    {
        /* [61:0] nano seconds */
        *p_ts_61_0 = ((uint64)(p_ts->seconds)) * 1000000000 + p_ts->nanoseconds;
    }
    else
    {
        /* [61:30] seconds + [29:0] nano seconds */
        *p_ts_61_0 = ((uint64)(p_ts->seconds) << 30) | ((uint64)p_ts->nanoseconds);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_packet_rx_dump(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    uint32 len = 0;

    if (CTC_BMP_ISSET(p_gb_pkt_master[lchip]->reason_bm, p_pkt_rx->rx_info.reason))
    {
        if (p_pkt_rx->pkt_len < SYS_GB_PKT_HEADER_LEN)
        {
            SYS_PKT_DUMP("Packet length is too small!!!Pkt len: %d\n",p_pkt_rx->pkt_len);
            return CTC_E_EXCEED_MIN_SIZE;
        }

        if (p_gb_pkt_master[lchip]->header_en[p_pkt_rx->rx_info.reason])
        {
            CTC_ERROR_RETURN(_sys_greatbelt_packet_dump_header(lchip, p_pkt_rx));
        }

        if (p_pkt_rx->pkt_len - SYS_GB_PKT_HEADER_LEN < SYS_PKT_BUF_PKT_LEN)
        {
            len = p_pkt_rx->pkt_len- SYS_GB_PKT_HEADER_LEN;
        }
        else
        {
            len = SYS_PKT_BUF_PKT_LEN;
        }

        SYS_PKT_DUMP("\n-----------------------------------------------\n");
        SYS_PKT_DUMP("Packet Info(Length : %d):\n", (p_pkt_rx->pkt_len-SYS_GB_PKT_HEADER_LEN));
        SYS_PKT_DUMP("-----------------------------------------------\n");
        CTC_ERROR_RETURN(_sys_greatbelt_packet_dump(lchip, (p_pkt_rx->pkt_buf[0].data+SYS_GB_PKT_HEADER_LEN), len));
    }

    return 0;
}

int32
sys_greatbelt_packet_set_svlan_tpid_index(uint8 lchip, uint16 svlan_tpid, uint8 svlan_tpid_index)
{
    int32 ret = CTC_E_NONE;

    CTC_MAX_VALUE_CHECK(svlan_tpid_index, 3);

    p_gb_pkt_master[lchip]->tp_id[svlan_tpid_index] = svlan_tpid;

    return ret;
}

STATIC int32
_sys_greatbelt_packet_init_svlan_tpid_index(uint8 lchip)
{
    uint32 cmd = 0;
    parser_ethernet_ctl_t parser_ethernet_ctl;
    int32 ret = CTC_E_NONE;

    sal_memset(&parser_ethernet_ctl, 0, sizeof(parser_ethernet_ctl_t));
    cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_ethernet_ctl));

    p_gb_pkt_master[lchip]->tp_id[0] = parser_ethernet_ctl.svlan_tpid0;
    p_gb_pkt_master[lchip]->tp_id[1] = parser_ethernet_ctl.svlan_tpid1;
    p_gb_pkt_master[lchip]->tp_id[2] = parser_ethernet_ctl.svlan_tpid2;
    p_gb_pkt_master[lchip]->tp_id[3] = parser_ethernet_ctl.svlan_tpid3;

    return ret;
}

STATIC int32
_sys_greatbelt_packet_get_svlan_tpid_index(uint8 lchip, uint16 svlan_tpid, uint8* svlan_tpid_index)
{
    int32 ret = CTC_E_NONE;

    if ((0 == svlan_tpid) || (svlan_tpid == p_gb_pkt_master[lchip]->tp_id[0]))
    {
        *svlan_tpid_index = 0;
    }
    else if (svlan_tpid == p_gb_pkt_master[lchip]->tp_id[1])
    {
        *svlan_tpid_index = 1;
    }
    else if (svlan_tpid == p_gb_pkt_master[lchip]->tp_id[2])
    {
        *svlan_tpid_index = 2;
    }
    else if (svlan_tpid == p_gb_pkt_master[lchip]->tp_id[3])
    {
        *svlan_tpid_index = 3;
    }
    else
    {
        ret = CTC_E_INVALID_PARAM;
    }

    return ret;
}

/*Note that you should not add other module APIs in this function.
  If you need to add it, save it to the packet module database.*/
STATIC int32
_sys_greatbelt_packet_txinfo_to_rawhdr(uint8 lchip, ctc_pkt_info_t* p_tx_info, ms_packet_header_t* p_raw_hdr, ctc_pkt_mode_t mode, uint8 user_flag, uint8* user_data)
{
    sys_greatbelt_pkt_hdr_ip_sa_t* p_ip_sa = NULL;
    uint32 dest_map = 0;
    uint64 ts_61_0 = 0;
    uint16 vlan_ptr = 0;
    uint8 rawhdr_crc4 = 0;
    uint8* p = NULL;
    uint8* pkt_data = NULL;
    uint8 gchip = 0;
    uint8 src_gchip = 0;
    uint16 lport = 0;
    uint8 hash = 0;
    uint32 offset = 0;
    uint8 svlan_tpid_index = 0;
    uint32 is_unknow_mcast = 0;
    drv_work_platform_type_t platform_type;
    sys_pkt_hdr_src_vlan_ptr_t src_vlan_ptr_u;
    sys_nh_info_dsnh_t      sys_nh_info;

    drv_greatbelt_get_platform_type(&platform_type);

    if (p_tx_info->priority > SYS_QOS_CLASS_PRIORITY_MAX)
    {
        /*64 is special used for dyingGasp*/
        return CTC_E_INVALID_PARAM;
    }
    CTC_MAX_VALUE_CHECK(p_tx_info->isolated_id, 31);
    CTC_PTR_VALID_CHECK(p_raw_hdr);

    CTC_ERROR_RETURN( sys_greatbelt_port_dest_gport_check(lchip, p_tx_info->dest_gport));
    sal_memset(p_raw_hdr, 0, sizeof(ms_packet_header_t));

    /* 1. must be inited */

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_PKT_TYPE_VALID))
    {
        p_raw_hdr->packet_type = p_tx_info->packet_type;
    }
    else
    {
        p_raw_hdr->packet_type = CTC_PARSER_PKT_TYPE_ETHERNET;
    }
    p_raw_hdr->non_crc = FALSE;

    if(CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_INGRESS_MODE)
        && CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_VALID)
        && (CTC_PKT_OPER_OAM != p_tx_info->oper_type))
    {
        p_raw_hdr->from_cpu_or_oam = FALSE;
    }
    else
    {
        p_raw_hdr->from_cpu_or_oam = TRUE;
    }
    p_raw_hdr->critical_packet = p_tx_info->is_critical;
    p_raw_hdr->source_cos = p_tx_info->src_cos;
    p_raw_hdr->ttl = p_tx_info->ttl;
    p_raw_hdr->logic_src_port = p_tx_info->logic_src_port;
    p_raw_hdr->logic_port_type = p_tx_info->logic_port_type;

    CTC_ERROR_RETURN(_sys_greatbelt_packet_get_svlan_tpid_index(lchip, p_tx_info->svlan_tpid, &svlan_tpid_index));
    p_raw_hdr->svlan_tpid_index = svlan_tpid_index;

    /*Set Vlan domain */
    if (p_tx_info->vlan_domain == CTC_PORT_VLAN_DOMAIN_SVLAN)
    {
        src_vlan_ptr_u.share1.outer_vlan_is_cvlan = 0;
        src_vlan_ptr_u.share1.src_vlan_ptr = p_tx_info->src_svid;
        p_raw_hdr->src_vlan_ptr = src_vlan_ptr_u.rev_1;
    }
    else if (p_tx_info->vlan_domain == CTC_PORT_VLAN_DOMAIN_CVLAN)
    {
        src_vlan_ptr_u.share1.outer_vlan_is_cvlan = 1;
        src_vlan_ptr_u.share1.src_vlan_ptr = p_tx_info->src_cvid;
        p_raw_hdr->src_vlan_ptr = src_vlan_ptr_u.rev_1;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /* 2. next_hop_ptr */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_BYPASS))
    {
        p_raw_hdr->bypass_ingress_edit = TRUE;
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH,
                                                      &offset));
        p_raw_hdr->next_hop_ptr = offset;
        p_raw_hdr->next_hop_ext = TRUE;
    }
    else
    {
        if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_VALID))
        {
            p_raw_hdr->next_hop_ptr = p_tx_info->nh_offset;
            if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NH_OFFSET_IS_8W))
            {
                p_raw_hdr->next_hop_ext = TRUE;
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
                                                  &offset));
            p_raw_hdr->next_hop_ptr = offset;
        }
    }

    /* 3. dest_map */
    dest_map = 0;
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_MCAST))
    {
        /* mcast */
        p_raw_hdr->next_hop_ptr = 0;
        p_raw_hdr->next_hop_ext  = 0;
        if (sys_greatbelt_get_gchip_id(lchip, &gchip) < 0)
        {
            gchip = 0;
        }
        dest_map = SYS_GREATBELT_BUILD_DESTMAP(1, gchip, p_tx_info->dest_group_id);
        _sys_greatbelt_l2_get_unknown_mcast_tocpu(lchip, p_tx_info->fid, &is_unknow_mcast);
    }
    else
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
        dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, gchip, CTC_MAP_GPORT_TO_LPORT(p_tx_info->dest_gport));

        if (sys_greatbelt_queue_get_enqueue_mode(lchip))
        {
            if (CTC_PKT_OPER_C2C == p_tx_info->oper_type)
            {
                if (sys_greatbelt_chip_is_local(lchip, gchip))
                {
                    dest_map |= SYS_QSEL_TYPE_CPU_TX << 13;
                    p_tx_info->oper_type = CTC_PKT_OPER_OAM;
                    p_tx_info->oam.type = CTC_OAM_TYPE_ETH;
                    CTC_SET_FLAG(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM);
                }
                else
                {
                    uint8 offset = 0;
                    uint16 sub_queue_id = 0;
                    uint16 grp_id = 0;
                    CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_get_queue_offset(lchip, CTC_PKT_CPU_REASON_C2C_PKT, &offset,&sub_queue_id));
                    grp_id = (sub_queue_id / 8) + (p_tx_info->priority/8);
                    dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, gchip, grp_id);
                    dest_map |= 0X80; /*set bit7 indicate tocpu for CTC7148*/
                    dest_map |= 0X10; /*set bit4 to dis to stacking port and to CPU*/
                    dest_map |= SYS_QSEL_TYPE_C2C << 13;
                }
            }
            else
            {
                dest_map |= SYS_QSEL_TYPE_REGULAR << 13;
            }
        }

    }

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_NHID_VALID))
    {
        sal_memset(&sys_nh_info, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_tx_info->nhid, &sys_nh_info));
        p_raw_hdr->next_hop_ptr = sys_nh_info.dsnh_offset;
        if (sys_nh_info.nexthop_ext)
        {
             p_raw_hdr->next_hop_ext = TRUE;
        }
        dest_map = SYS_NH_ENCODE_DESTMAP(sys_nh_info.is_mcast, sys_nh_info.dest_chipid, sys_nh_info.dest_id);
    }

    p_raw_hdr->dest_map = dest_map;
    p_raw_hdr->operation_type = p_tx_info->oper_type;

    sys_greatbelt_get_gchip_id(lchip, &src_gchip);
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_PORT_VALID))
    {
        p_raw_hdr->source_port = p_tx_info->src_port;
    }
    else if (is_unknow_mcast)
    {
        sys_greatbelt_get_gchip_id(lchip, &src_gchip);
        p_raw_hdr->source_port = CTC_MAP_LPORT_TO_GPORT(src_gchip, SYS_RESERVE_PORT_ID_ILOOP);
    }
    else
    {
        p_raw_hdr->source_port = CTC_MAP_LPORT_TO_GPORT(src_gchip, SYS_RESERVE_PORT_ID_CPU);
    }

    SYS_GREATBELT_GPORT_TO_GPORT14(p_raw_hdr->source_port);
    if (CTC_IS_LINKAGG_PORT(p_tx_info->src_port))
    {
        bool  stacking_en = 0;
        uint32 stacking_mcast_offset = 0;
        sys_greatbelt_stacking_get_enable(lchip, &stacking_en, &stacking_mcast_offset);
        if(stacking_en)
        {
            p_raw_hdr->source_port &= 0x3E3F;
            p_raw_hdr->source_port |=  ((src_gchip & 0x7) << 6);
            p_raw_hdr->source_port15_14 = (src_gchip >> 3) & 0x3;
        }
    }

    /* 4. svid */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID))
    {
        p_raw_hdr->src_svlan_id_valid = TRUE;
        p_raw_hdr->src_vlan_id = p_tx_info->src_svid;
        p_raw_hdr->stag_action = SYS_PKT_CTAG_ACTION_ADD;
        p_raw_hdr->svlan_tag_operation_valid = TRUE;
    }

    /* 5. cvid */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID))
    {
        p_raw_hdr->src_cvlan_id_valid = TRUE;
        p_raw_hdr->src_cvlan_id = p_tx_info->src_cvid;

        /* TTL: { 2'b00, cTagAction[1:0], srcCtagCos[2:0], srcCtagCfi } */
        CTC_UNSET_FLAG(p_raw_hdr->ttl, 0x30);
        CTC_SET_FLAG(p_raw_hdr->ttl, ((SYS_PKT_CTAG_ACTION_ADD << 4) & 0x30));
        /* IPSA: {macKnown, srcDscp[5:0], 5'd0, isIpv4, cvlanTagOperationValid, aclDscp[5:0],
         * isidValid, ecnAware, congestionValid, ecnEn, layer3Offset[7:0] }
         */
        CTC_SET_FLAG(p_raw_hdr->ip_sa, (1 << 18));
    }

    /* isolated_id*/
    if (0 != p_tx_info->isolated_id)
    {
        p_raw_hdr->source_port_isolate_id = p_tx_info->isolated_id;
    }

    /* 6. encap header hash for linkagg */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_HASH_VALID))
    {
        hash = p_tx_info->hash;
    }
    else
    {
        pkt_data = user_flag?user_data:((uint8*)p_raw_hdr + SYS_GB_PKT_HEADER_LEN);
        hash = ctc_crc_calculate_crc8(pkt_data, 12, 0);
    }

    p_raw_hdr->header_hash2_0 = (hash & 0x07);
    p_raw_hdr->header_hash7_3 = ((hash >> 3) & 0x1F);


    /* 7. encap priority, prioty 63 mapping to dma channel 3 */
    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_PRIORITY))
    {
        p_raw_hdr->priority = (p_tx_info->priority==(SYS_QOS_CLASS_PRIORITY_MAX+1))?SYS_QOS_CLASS_PRIORITY_MAX:p_tx_info->priority;
    }
    else
    {
        p_raw_hdr->priority = SYS_QOS_CLASS_PRIORITY_MAX;
    }

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_COLOR))
    {
        p_raw_hdr->color = p_tx_info->color;
    }
    else
    {
        p_raw_hdr->color = CTC_QOS_COLOR_GREEN;
    }

    if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_INGRESS_MODE))
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
        lport = CTC_MAP_GPORT_TO_LPORT(p_tx_info->dest_gport);
        /* dest_map is ILOOP */
        dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_ILOOP);
        p_raw_hdr->dest_map = dest_map;

        /* nexthop_ptr is lport */
        p_raw_hdr->next_hop_ptr = lport;
    }

    if (CTC_PKT_OPER_OAM == p_tx_info->oper_type)
    {
        /* 8. OAM */
        p = (uint8*)(p_raw_hdr) + SYS_GB_HDR_IPSA_OFFSET;
        p_ip_sa = (sys_greatbelt_pkt_hdr_ip_sa_t*)p;
        p_ip_sa->oam.rx_oam = FALSE;
        p_ip_sa->oam.link_oam = CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM);
        p_ip_sa->oam.dm_en = CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM);
        p_ip_sa->oam.gal_exist = CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_GAL);
        p_ip_sa->oam.mip_en_or_cw_added = CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_CW);
        p_ip_sa->oam.oam_type = p_tx_info->oam.type;

        /* TPOAM Y.1731 Section need not append 13 label */
        if (CTC_OAM_TYPE_ACH == p_tx_info->oam.type)
        {
            p_ip_sa->oam.mpls_label_disable =
                CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM) ? 0xF : 0x0;

            /* only ACH encapsulation need to change packet_type */
            if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_GAL))
            {
                p_raw_hdr->packet_type = CTC_PARSER_PKT_TYPE_MPLS;
            }
            else
            {
                p_raw_hdr->packet_type = CTC_PARSER_PKT_TYPE_RESERVED;
            }
        }

        /* set for UP MEP */
        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP))
        {
            if (CTC_OAM_TYPE_ETH != p_tx_info->oam.type)
            {
                /* only Eth OAM support UP MEP */
                return CTC_E_INVALID_DIR;
            }

            /* set UP MEP */
            CTC_SET_FLAG(p_raw_hdr->source_port_isolate_id, 0x01);

            gchip = CTC_MAP_GPORT_TO_GCHIP(p_tx_info->dest_gport);
            lport = CTC_MAP_GPORT_TO_LPORT(p_tx_info->dest_gport);
            /* dest_map is ILOOP */
            dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_ILOOP);
            p_raw_hdr->dest_map = dest_map;

            /* nexthop_ptr is lport */
            p_raw_hdr->next_hop_ptr = lport;

            /* src vlan_ptr is vlan_ptr by vid */
            sys_greatbelt_vlan_get_vlan_ptr(lchip, p_tx_info->oam.vid, &vlan_ptr);
            p_raw_hdr->src_vlan_ptr = vlan_ptr;
        }

        /* set bypass MIP port */
        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_TX_MIP_TUNNEL))
        {
            /* bypass OAM packet to MIP configured port; otherwise, packet will send to CPU again */
            p_ip_sa->oam.oam_type = CTC_OAM_TYPE_NONE;
            p_raw_hdr->oam_tunnel_en = TRUE;
        }

        /* set timestamp offset in bytes for DM */
        if (CTC_FLAG_ISSET(p_tx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM))
        {
            p_raw_hdr->logic_src_port = (p_tx_info->oam.dm_ts_offset >> 2) & 0x3F;
            p_raw_hdr->rxtx_fcl3 = (p_tx_info->oam.dm_ts_offset >> 1) & 0x01;
            p_raw_hdr->rxtx_fcl0 = (p_tx_info->oam.dm_ts_offset) & 0x01;
        }
    }
    else if (CTC_PKT_OPER_PTP == p_tx_info->oper_type)
    {
        /* 9. PTP */
        /*  PTP Timestamp Bits Mapping
            Timestamp               Field Name          BIT Width       BIT Base
            ptpTimestamp[21:0]      ipSa                22(ipSa[21:0])  0
            ptpTimestamp[22]        rxtxFcl0            1               22
            ptpTimestamp[24:23]     rxtxFcl2_1          2               23
            ptpTimestamp[25]        rxtxFcl3            1               25
            ptpTimestamp[41:26]     fid                 16              26
            ptpTimestamp[53:42]     srcCvlanId          12              42
            ptpTimestamp[54]        srcCvlanIdValid     1               54
            ptpTimestamp[60:55]     rxtxFcl22_17        6               55
            ptpTimestamp[61]        srcCtagOffsetType   1               61
        */
        _sys_greatbelt_packet_ts_to_bit62(lchip, TRUE, &p_tx_info->ptp.ts, &ts_61_0);
        p_raw_hdr->ip_sa                = (ts_61_0 >> 0) & 0x3FFFFF;
        p_raw_hdr->rxtx_fcl0            = (ts_61_0 >> 22) & 0x0001;
        p_raw_hdr->rxtx_fcl2_1          = (ts_61_0 >> 23) & 0x0003;
        p_raw_hdr->rxtx_fcl3            = (ts_61_0 >> 25) & 0x0001;
        p_raw_hdr->fid                  = (ts_61_0 >> 26) & 0xFFFF;
        p_raw_hdr->src_cvlan_id         = (ts_61_0 >> 42) & 0x0FFF;
        p_raw_hdr->src_cvlan_id_valid   = (ts_61_0 >> 54) & 0x0001;
        p_raw_hdr->rxtx_fcl22_17        = (ts_61_0 >> 55) & 0x003F;
        p_raw_hdr->src_ctag_offset_type = (ts_61_0 >> 61) & 0x0001;

        /* source_port_isolate_id[5:0] = ptpOffsetType[1:0] + ptpEditType[1:0] + ptpSequenceId[1:0] */
        p_raw_hdr->source_port_isolate_id = 0;
        p_raw_hdr->source_port_isolate_id |= (p_tx_info->ptp.oper & 0x03) << 2;
        if (CTC_PTP_CAPTURE_ONLY == p_tx_info->ptp.oper)
        {
            p_raw_hdr->source_port_isolate_id |= (p_tx_info->ptp.seq_id & 0x03);
        }
    }
    else if (CTC_PKT_OPER_C2C == p_tx_info->oper_type)
    {
        p_raw_hdr->bypass_ingress_edit = 1;
        if (CTC_FLAG_ISSET(p_tx_info->flags, CTC_PKT_FLAG_MCAST))
        {
            p_raw_hdr->rxtx_fcl0 = 1;
        }
    }

    sys_greatbelt_packet_swap32(lchip, (uint32*)p_raw_hdr, SYS_GB_PKT_HEADER_LEN / 4, TRUE);
    /* calc bridge header CRC */
    rawhdr_crc4 = ctc_crc_calculate_crc4((uint8*)p_raw_hdr, SYS_GB_PKT_HEADER_LEN, 0);
    if ((platform_type == HW_PLATFORM)&&(CTC_PKT_MODE_DMA == mode))
    {
        sys_greatbelt_packet_swap32(lchip, (uint32*)p_raw_hdr, SYS_GB_PKT_HEADER_LEN / 4, TRUE);
        p_raw_hdr->header_crc = (rawhdr_crc4 & 0xF);
    }
    else
    {
        p_raw_hdr->header_crc = (rawhdr_crc4 & 0xF);
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_packet_rawhdr_to_rxinfo(uint8 lchip, ms_packet_header_t* p_raw_hdr, ctc_pkt_rx_t* p_pkt_rx)
{
    sys_greatbelt_pkt_hdr_ip_sa_t* p_ip_sa = NULL;
    uint64 ts_61_0 = 0;
    uint32 ns_only_format = FALSE;
    uint8* p = NULL;
    uint8 gchip = 0;
    uint8 lport = 0;
    uint8 from_egress = FALSE;
    uint8 offset = 0;
    uint16 queue_id = 0;
    ctc_pkt_info_t* p_rx_info      = NULL;

    p_rx_info = &p_pkt_rx->rx_info;

    /* convert endian */
    CTC_ERROR_RETURN(sys_greatbelt_packet_swap32(lchip, (uint32*)p_raw_hdr, SYS_GB_PKT_HEADER_LEN / 4, FALSE));

    /* 1. must be inited */
    p_rx_info->packet_type = p_raw_hdr->packet_type;
    p_rx_info->oper_type = p_raw_hdr->operation_type;
    p_rx_info->priority = p_raw_hdr->priority;
    p_rx_info->color = p_raw_hdr->color;
    p_rx_info->src_cos = p_raw_hdr->source_cos;
    p_rx_info->src_port = p_raw_hdr->source_port;
    p_rx_info->vrfid = p_raw_hdr->fid;
    p_rx_info->payload_offset = p_raw_hdr->packet_offset;

    SYS_GREATBELT_GPORT14_TO_GPORT(p_rx_info->src_port);
    if (CTC_IS_LINKAGG_PORT(p_rx_info->src_port))
    {
        /* if sourcePort[13:9] == 5'd31, {sourcePort[15:14], sourcePort[8:6]} is chipId[4:0], sourcePort[5:0] is TID  */
        p_rx_info->src_chip = ((p_raw_hdr->source_port15_14 << 3) & 0x18 ) | ((p_raw_hdr->source_port >> 6) & 0x07);
        p_rx_info->src_port &= 0x3F3F;  /* clear 0x00C0 for chipId */
    }
    else
    {
        p_rx_info->src_chip = CTC_MAP_GPORT_TO_GCHIP(p_rx_info->src_port);
    }
    p_rx_info->lport = CTC_MAX_UINT16_VALUE;
    p_rx_info->ttl = p_raw_hdr->ttl;
    p_rx_info->is_critical = p_raw_hdr->critical_packet ? TRUE : FALSE;

    /* 2. get RX reason from next_hop_ptr */
    p_rx_info->reason = CTC_PKT_CPU_REASON_GET_BY_NHPTR(p_raw_hdr->next_hop_ptr);

    /* 3. judge whether from ingress or egress */
    from_egress = (CTC_PKT_CPU_REASON_L3_MTU_FAIL == p_rx_info->reason)
               || (CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL == p_rx_info->reason);

    /* 4. get queue id */
    sys_greatbelt_cpu_reason_get_queue_offset(lchip, p_rx_info->reason, &offset, &queue_id);

    if (CTC_PKT_CPU_REASON_FWD_CPU == p_rx_info->reason)
    {
        queue_id += p_rx_info->priority / ((p_gb_queue_master[lchip]->priority_mode)? 2:8);
    }
    p_rx_info->dest_gport = queue_id;

    /* 5. get svid / cvid */
    if (from_egress)
    {
        /* EPE: can only get FID */
        if (p_raw_hdr->fid)
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID);
            p_rx_info->src_svid = p_raw_hdr->fid;
        }
    }
    else
    {
        /* IPE: use src_svlan_id_valid/src_cvlan_id_valid */
        if (p_raw_hdr->src_svlan_id_valid)
        {
            p_rx_info->src_svid = p_raw_hdr->src_vlan_id;
        }

        if ((p_rx_info->src_svid == 0) && ((p_raw_hdr->src_vlan_ptr & 0x1FFF) < 4096))
        {
            p_rx_info->src_svid = p_raw_hdr->src_vlan_ptr;
        }

        if (p_rx_info->src_svid)
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID);
        }

        if (p_raw_hdr->src_cvlan_id_valid)
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID);
            p_rx_info->src_cvid = p_raw_hdr->src_cvlan_id;
        }
    }

    if (p_raw_hdr->svlan_tag_operation_valid)
    {
        switch (p_raw_hdr->stag_action)
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

    if (p_raw_hdr->operation_type == CTC_PKT_OPER_NORMAL)
    {
        p = (uint8*)(p_raw_hdr) + SYS_GB_HDR_IPSA_OFFSET;
        p_ip_sa = (sys_greatbelt_pkt_hdr_ip_sa_t*)p;

        if (p_ip_sa->normal.cvlan_tag_operation_valid)
        {
            switch ((p_raw_hdr->ttl>>4)&0x3)
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


    if (CTC_PKT_OPER_OAM == p_rx_info->oper_type)
    {
        /* 6. get OAM */
        if (CTC_FLAG_ISSET(p_raw_hdr->source_port_isolate_id, 0x01))  /* sourcePortIsolateId[5:0]/{oamDestChipId[4:0],isUp} */
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP);
        }

        if (CTC_FLAG_ISSET(p_raw_hdr->pbb_src_port_type, 0x04))  /* pbbSrcPortType[2:0]/{lmReceivedPacket, lmPacketType[1:0] } */
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_LM);
        }

        p = (uint8*)(p_raw_hdr) + SYS_GB_HDR_IPSA_OFFSET;
        p_ip_sa = (sys_greatbelt_pkt_hdr_ip_sa_t*)p;

        p_rx_info->oam.type = p_ip_sa->oam.oam_type;
        if (p_ip_sa->oam.link_oam)
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM);
        }

        if (p_ip_sa->oam.dm_en)
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_DM);
        }

        if (p_ip_sa->oam.gal_exist)
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_HAS_MPLS_GAL);
        }

        if (p_ip_sa->oam.mip_en_or_cw_added)
        {
            CTC_SET_FLAG(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_MIP);
        }

        p_rx_info->oam.mep_index = (p_ip_sa->oam.mpls_label_disable << 10) | (p_ip_sa->oam.mep_index_9_0);

        /* get timestamp offset in bytes for DM */
        if (p_ip_sa->oam.dm_en)
        {
            /* If dm_en, the srcCvlanIdValid and srcCvlanId is used by DM Timestamp */
            if (CTC_FLAG_ISSET(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID))
            {
                CTC_UNSET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID);
                p_rx_info->src_cvid = 0;
            }

            /*  DM Timestamp Bits Mapping
                Timestamp               Field Name          BIT Width   BIT Base
                dmTimestamp[0]          rxtxFcl0            1           0
                dmTimestamp[1]          rxtxFcl3            1           1
                dmTimestamp[17:2]       logicSrcPort        16          2
                dmTimestamp[33:18]      fid                 16          18
                dmTimestamp[45:34]      srcCvlanId          12          34
                dmTimestamp[46]         srcCvlanIdValid     1           46
                dmTimestamp[54:47]      ttl                 8           47
                dmTimestamp[60:55]      rxtxFcl22_17        6           55
                dmTimestamp[61]         srcCtagOffsetType   1           61
             */
            ts_61_0 = ((uint64)(p_raw_hdr->rxtx_fcl0 & 0x0001) << 0)
                | ((uint64)(p_raw_hdr->rxtx_fcl3 & 0x0001) << 1)
                | ((uint64)(p_raw_hdr->logic_src_port & 0xFFFF) << 2)
                | ((uint64)(p_raw_hdr->fid & 0xFFFF) << 18)
                | ((uint64)(p_raw_hdr->src_cvlan_id & 0x0FFF) << 34)
                | ((uint64)(p_raw_hdr->src_cvlan_id_valid & 0x0001) << 46)
                | ((uint64)(p_raw_hdr->ttl & 0x00FF) << 47)
                | ((uint64)(p_raw_hdr->rxtx_fcl22_17 & 0x003F) << 55)
                | ((uint64)(p_raw_hdr->src_ctag_offset_type & 0x0001) << 61);
            ns_only_format = (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP)) ? FALSE : TRUE;
            _sys_greatbelt_packet_bit62_to_ts(lchip, ns_only_format, ts_61_0, &p_rx_info->oam.dm_ts);
        }

        if (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_LM))
        {
            /*  LM FCL Bits Mapping
                LM FCL                  Field Name          BIT Width   BIT Base
                Fcl[0]                  rxtxFcl0            1           0
                Fcl[2:1]                rxtxFcl2_1          2           1
                Fcl[3]                  rxtxFcl3            1           3
                Fcl[15:4]               srcCvlanId          12          4
                Fcl[16]                 srcCvlanIdValid     1           16
                Fcl[22:17]              rxtxFcl22_17        6           17
                Fcl[30:23]              ttl                 8           23
                Fcl[31]                 srcCtagOffsetType   1           31
            */
            p_rx_info->oam.lm_fcl = ((p_raw_hdr->rxtx_fcl0 & 0x0001) << 0)
                | ((p_raw_hdr->rxtx_fcl2_1 & 0x0003) << 1)
                | ((p_raw_hdr->rxtx_fcl3 & 0x0001) << 3)
                | ((p_raw_hdr->src_cvlan_id & 0x0FFF) << 4)
                | ((p_raw_hdr->src_cvlan_id_valid & 0x0001) << 16)
                | ((p_raw_hdr->rxtx_fcl22_17 & 0x003F) << 17)
                | ((p_raw_hdr->ttl & 0x00FF) << 23)
                | ((p_raw_hdr->src_ctag_offset_type & 0x0001) << 31);
        }

        /* get svid from vlan_ptr for Up MEP/Up MIP */
        if (CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_IS_UP_MEP))
        {
            CTC_SET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_SVID_VALID);
            p_rx_info->src_svid = p_raw_hdr->src_vlan_ptr;
        }
    }
    else if (CTC_PKT_OPER_PTP == p_rx_info->oper_type)
    {
        /* 7. get PTP */

        /* If is PTP, the srcCvlanIdValid and srcCvlanId is used by PTP Timestamp */
        if (CTC_FLAG_ISSET(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID))
        {
            CTC_UNSET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_SRC_CVID_VALID);
            p_rx_info->src_cvid = 0;
        }

        /*  PTP Timestamp Bits Mapping
            Timestamp               Field Name          BIT Width       BIT Base
            ptpTimestamp[21:0]      ipSa                22(ipSa[21:0])  0
            ptpTimestamp[22]        rxtxFcl0            1               22
            ptpTimestamp[24:23]     rxtxFcl2_1          2               23
            ptpTimestamp[25]        rxtxFcl3            1               25
            ptpTimestamp[41:26]     fid                 16              26
            ptpTimestamp[53:42]     srcCvlanId          12              42
            ptpTimestamp[54]        srcCvlanIdValid     1               54
            ptpTimestamp[60:55]     rxtxFcl22_17        6               55
            ptpTimestamp[61]        srcCtagOffsetType   1               61
        */
        ts_61_0 = ((uint64)(p_raw_hdr->ip_sa & 0x3FFFFF) << 0)
            | ((uint64)(p_raw_hdr->rxtx_fcl0 & 0x0001) << 22)
            | ((uint64)(p_raw_hdr->rxtx_fcl2_1 & 0x0003) << 23)
            | ((uint64)(p_raw_hdr->rxtx_fcl3 & 0x0001) << 25)
            | ((uint64)(p_raw_hdr->fid & 0xFFFF) << 26)
            | ((uint64)(p_raw_hdr->src_cvlan_id & 0x0FFF) << 42)
            | ((uint64)(p_raw_hdr->src_cvlan_id_valid & 0x0001) << 54)
            | ((uint64)(p_raw_hdr->rxtx_fcl22_17 & 0x003F) << 55)
            | ((uint64)(p_raw_hdr->src_ctag_offset_type & 0x0001) << 61);
        _sys_greatbelt_packet_bit62_to_ts(lchip, TRUE, ts_61_0, &p_rx_info->ptp.ts);
    }
    else if (CTC_PKT_OPER_C2C == p_rx_info->oper_type)
    {
        ctc_chip_device_info_t device_info;
        sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
        sys_greatbelt_chip_get_device_info(lchip, &device_info);

        p_rx_info->reason       = CTC_PKT_CPU_REASON_C2C_PKT;
        if(device_info.version_id >= 2)
        {
            lport = p_raw_hdr->src_vlan_id & 0x7F;
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            p_rx_info->stacking_src_port     = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        }

        if (CTC_FLAG_ISSET(p_rx_info->flags, CTC_PKT_FLAG_MCAST))
        {
            CTC_UNSET_FLAG(p_rx_info->flags, CTC_PKT_FLAG_MCAST);
            p_rx_info->dest_group_id = 0;
        }

        sys_greatbelt_queue_get_c2c_queue_base(lchip, &queue_id);
        p_rx_info->dest_gport = p_rx_info->priority / ((p_gb_queue_master[lchip]->priority_mode)? 1:4) + queue_id;
    }

    if (CTC_PKT_OPER_NORMAL == p_rx_info->oper_type)
    {
        p_rx_info->logic_src_port = p_raw_hdr->logic_src_port & 0x3FFF;
    }


    if (p_raw_hdr->from_fabric ||
        (CTC_PKT_OPER_OAM == p_rx_info->oper_type
    && CTC_FLAG_ISSET(p_rx_info->oam.flags, CTC_PKT_OAM_FLAG_LINK_OAM)))
    {
        p_pkt_rx->stk_hdr_len = SYS_PACKET_BASIC_STACKING_HEADER_LEN;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_swap_payload(uint8 lchip, uint32 len, uint8* p_payload)
{
    uint32 u32_len = len / 4;
    uint32 left_len = len % 4;
    host_type_t byte_order;
    drv_work_platform_type_t platform_type;

    byte_order = drv_greatbelt_get_host_type();
    drv_greatbelt_get_platform_type(&platform_type);

    if ((byte_order == HOST_LE)&&(platform_type == HW_PLATFORM))
    {
        if (left_len)
        {
            u32_len += 1;
        }
        sys_greatbelt_packet_swap32(lchip, (uint32*)p_payload, u32_len, TRUE);
    }

    return 0;
}

int32
sys_greatbelt_packet_rx(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    SYS_PACKET_INIT_CHECK(lchip);
    p_pkt_rx->lchip = lchip;
    CTC_ERROR_RETURN(sys_greatbelt_packet_decap(lchip, p_pkt_rx));

    if ((p_pkt_rx->rx_info.reason - CTC_PKT_CPU_REASON_OAM) == CTC_OAM_EXCP_BFD_TIMER_NEGOTIATION)
    {
        if (p_gb_pkt_master[lchip]->internal_rx_cb)
        {
            p_gb_pkt_master[lchip]->internal_rx_cb(lchip, p_pkt_rx);
        }
    }

    if (p_pkt_rx->rx_info.reason < CTC_PKT_CPU_REASON_MAX_COUNT)
    {
        p_gb_pkt_master[lchip]->stats.rx[p_pkt_rx->rx_info.reason]++;
    }

    _sys_greatbelt_packet_rx_dump(lchip, p_pkt_rx);

    if (p_gb_pkt_master[lchip]->buf_id  < SYS_PKT_BUF_MAX)
    {
        p_gb_pkt_master[lchip]->pkt_buf[p_gb_pkt_master[lchip]->buf_id].pkt_len = p_pkt_rx->pkt_len;
        sal_time(&(p_gb_pkt_master[lchip]->pkt_buf[p_gb_pkt_master[lchip]->buf_id].tm));


        if (p_pkt_rx->pkt_buf->len <= SYS_PKT_BUF_PKT_LEN)
        {
            sal_memcpy(p_gb_pkt_master[lchip]->pkt_buf[p_gb_pkt_master[lchip]->buf_id].pkt_data,
                       p_pkt_rx->pkt_buf->data,
                       p_pkt_rx->pkt_buf->len);
        }

        p_gb_pkt_master[lchip]->buf_id++;
        if (p_gb_pkt_master[lchip]->buf_id == SYS_PKT_BUF_MAX)
        {
            p_gb_pkt_master[lchip]->buf_id = 0;
        }

    }

    if (p_gb_pkt_master[lchip]->cfg.rx_cb)
    {
        p_gb_pkt_master[lchip]->cfg.rx_cb(p_pkt_rx);
    }

    return CTC_E_NONE;
}

STATIC int32
sys_greatbelt_packet_tx_alloc(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx, sys_pkt_tx_info_t** pp_pkt_info)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    uint8 user_flag = 0;
    uint16 pkt_buf_len = 0;

    *pp_pkt_info = NULL;
    p_pkt_info = (sys_pkt_tx_info_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_pkt_tx_info_t));
    if (NULL == p_pkt_info)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_pkt_info, 0, sizeof(sys_pkt_tx_info_t));
    p_pkt_info->callback = p_pkt_tx->callback;
    p_pkt_info->user_data = p_pkt_tx->user_data;
    p_pkt_info->flags = p_pkt_tx->tx_info.flags;
    p_pkt_info->priority = p_pkt_tx->tx_info.priority;
    p_pkt_info->p_pkt_tx = p_pkt_tx;

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
        if(((p_pkt_info->data_len % 4)!=0))
        {
            pkt_buf_len = p_pkt_info->data_len + 4 - (p_pkt_info->data_len % 4);
        }
        else
        {
            pkt_buf_len = p_pkt_info->data_len;
        }

        if (p_pkt_info->is_async)
        {
            p_pkt_info->data = (uint8*)mem_malloc(MEM_SYSTEM_MODULE, pkt_buf_len);
            if (NULL == p_pkt_info->data)
            {
                mem_free(p_pkt_info);
                return CTC_E_NO_MEMORY;
            }
            sal_memcpy(p_pkt_info->data, p_pkt_tx->skb.data, pkt_buf_len);
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
sys_greatbelt_packet_tx_free(uint8 lchip, sys_pkt_tx_info_t* p_pkt_info)
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
sys_greatbelt_packet_tx(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    uint8 is_skb_buf = 0;

    is_skb_buf = (((p_pkt_tx->skb.head == NULL) ||
               (p_pkt_tx->skb.tail == NULL) ||
               (p_pkt_tx->skb.end == NULL)) &&
               (p_pkt_tx->skb.data != NULL) ? 0:1);

    CTC_ERROR_RETURN(sys_greatbelt_packet_encap(lchip, p_pkt_tx));


    /*tx buf dump data store*/
    if (p_gb_pkt_master[lchip]->buf_id_tx< SYS_PKT_BUF_MAX)
    {
        sys_pkt_tx_buf_t* p_tx_buf = &p_gb_pkt_master[lchip]->pkt_tx_buf[p_gb_pkt_master[lchip]->buf_id_tx];
        uint32 pkt_len  = 0;

        if (!is_skb_buf)
        {
            pkt_len = (p_pkt_tx->mode == CTC_PKT_MODE_DMA )? (p_pkt_tx->skb.len +SYS_GB_PKT_HEADER_LEN)
                                : (p_pkt_tx->skb.len + SYS_GB_PKT_HEADER_LEN+SYS_GB_PKT_CPUMAC_LEN);
        }
        else
        {
            pkt_len = p_pkt_tx->skb.len;
        }

        p_tx_buf->mode  = p_pkt_tx->mode;
        p_tx_buf->pkt_len = pkt_len;

        sal_time(&p_tx_buf->tm);
        sal_memcpy(&p_tx_buf->tx_info,&p_pkt_tx->tx_info,sizeof(ctc_pkt_info_t));

        pkt_len = (pkt_len >SYS_PKT_BUF_PKT_LEN) ?SYS_PKT_BUF_PKT_LEN:pkt_len;

        if (p_pkt_tx->mode == CTC_PKT_MODE_DMA )
        {
            if(is_skb_buf)
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],p_pkt_tx->skb.data, pkt_len);
            }
            else
            {
               sal_memcpy(&p_tx_buf->pkt_data[0],&p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM - SYS_GB_PKT_HEADER_LEN], SYS_GB_PKT_HEADER_LEN);
               /*copy userdata*/
               sal_memcpy(&p_tx_buf->pkt_data[SYS_GB_PKT_HEADER_LEN],p_pkt_tx->skb.data, (pkt_len-SYS_GB_PKT_HEADER_LEN));
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
               sal_memcpy(&p_tx_buf->pkt_data[0],&p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM - SYS_GB_PKT_HEADER_LEN -SYS_GB_PKT_CPUMAC_LEN],
                         (SYS_GB_PKT_CPUMAC_LEN+ SYS_GB_PKT_HEADER_LEN));

               /*copy userdata*/
               sal_memcpy(&p_tx_buf->pkt_data[SYS_GB_PKT_HEADER_LEN + SYS_GB_PKT_CPUMAC_LEN],p_pkt_tx->skb.data,
                         (pkt_len-SYS_GB_PKT_HEADER_LEN -SYS_GB_PKT_CPUMAC_LEN));
            }

        }

        p_gb_pkt_master[lchip]->buf_id_tx++;
        if (p_gb_pkt_master[lchip]->buf_id_tx == SYS_PKT_BUF_MAX)
        {
            p_gb_pkt_master[lchip]->buf_id_tx = 0;
        }

    }


    if (CTC_PKT_MODE_ETH == p_pkt_tx->mode)
    {
        if (p_gb_pkt_master[lchip]->cfg.socket_tx_cb )
        {
            p_gb_pkt_master[lchip]->cfg.socket_tx_cb(p_pkt_tx);
        }
        else
        {
            SYS_PKT_DUMP(SYS_GB_PKT_STR "do not support Socket Tx!\n");
            return CTC_E_UNEXPECT;
        }
    }
    else if (CTC_PKT_MODE_DMA == p_pkt_tx->mode)
    {
        {
            CTC_ERROR_RETURN(sys_greatbelt_packet_tx_alloc(lchip, p_pkt_tx, &p_pkt_info));
            if (NULL == p_pkt_info)
            {
                return CTC_E_NO_MEMORY;
            }
            if (p_pkt_tx->callback)
            {


                SYS_PKT_TX_LOCK(lchip);
                ctc_listnode_add_tail(p_gb_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info);
                SYS_PKT_TX_UNLOCK(lchip);
                CTC_ERROR_RETURN(sal_sem_give(p_gb_pkt_master[lchip]->tx_sem));
                return CTC_E_NONE;
            }
            /* call ctc DMA TX API */
            sys_greatbelt_dma_pkt_tx(lchip, (ctc_pkt_tx_t*)p_pkt_info);
            sys_greatbelt_packet_tx_free(lchip, p_pkt_info);
        }
    }
    else
    {
        return CTC_E_UNEXPECT;
    }

    if (CTC_FLAG_ISSET(p_pkt_tx->tx_info.flags, CTC_PKT_FLAG_MCAST))
    {
         p_gb_pkt_master[lchip]->stats.mc_tx++;
    }
    else
    {
         p_gb_pkt_master[lchip]->stats.uc_tx++;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_encap(uint8 lchip, ctc_pkt_tx_t* p_pkt_tx)
{
    ctc_pkt_info_t* p_tx_info = NULL;
    ms_packet_header_t* p_raw_hdr = NULL;
    sys_greatbelt_cpumac_header_t* p_cpumac_hdr = NULL;
    uint8 user_flag = 0;

    CTC_PTR_VALID_CHECK(p_pkt_tx);
    p_tx_info = &p_pkt_tx->tx_info;

    SYS_PKT_DBG_FUNC();
    SYS_PKT_DBG_INFO("[Packet Tx Info]",_sys_greatbelt_packet_dump(lchip, p_pkt_tx->skb.data, p_pkt_tx->skb.len));
    SYS_PKT_DBG_INFO("Flags:0x%x  Dest port:0x%x Oper_type:%d Priority:%d Color:%d nh_offset:%d\n",
                    p_pkt_tx->tx_info.flags, p_pkt_tx->tx_info.dest_gport,
                    p_pkt_tx->tx_info.oper_type, p_pkt_tx->tx_info.priority,
                    p_pkt_tx->tx_info.color, p_pkt_tx->tx_info.nh_offset);

    if (p_pkt_tx->skb.len > (CTC_PKT_MTU - CTC_PKT_HDR_ROOM))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }
    user_flag = ((p_pkt_tx->skb.head == NULL) ||
                 (p_pkt_tx->skb.tail == NULL) ||
                 (p_pkt_tx->skb.end == NULL)) &&
                 (p_pkt_tx->skb.data != NULL);
    /* 1. encode packet header */
    if (user_flag)
    {
        p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM] = SYS_GB_PKT_HEADER_LEN;
        p_raw_hdr = (ms_packet_header_t*)(p_pkt_tx->skb.buf + CTC_PKT_HDR_ROOM - SYS_GB_PKT_HEADER_LEN);
    }
    else
    {
        p_raw_hdr = (ms_packet_header_t*)ctc_packet_skb_push(&p_pkt_tx->skb, SYS_GB_PKT_HEADER_LEN);
    }
    CTC_ERROR_RETURN(_sys_greatbelt_packet_txinfo_to_rawhdr(lchip, p_tx_info, p_raw_hdr, p_pkt_tx->mode, user_flag, p_pkt_tx->skb.data));

    if (CTC_PKT_MODE_ETH == p_pkt_tx->mode)
    {
        /* 2. encode CPUMAC Header */
        if (user_flag)
        {
            p_pkt_tx->skb.buf[CTC_PKT_HDR_ROOM] += SYS_GB_PKT_CPUMAC_LEN;
            p_cpumac_hdr = (sys_greatbelt_cpumac_header_t*)(p_pkt_tx->skb.buf + CTC_PKT_HDR_ROOM - SYS_GB_PKT_HEADER_LEN - SYS_GB_PKT_CPUMAC_LEN);
        }
        else
        {
            p_cpumac_hdr = (sys_greatbelt_cpumac_header_t*)ctc_packet_skb_push(&p_pkt_tx->skb, SYS_GB_PKT_CPUMAC_LEN);
        }
        p_cpumac_hdr->macda[0] = p_gb_pkt_master[lchip]->cpu_mac_sa[0];
        p_cpumac_hdr->macda[1] = p_gb_pkt_master[lchip]->cpu_mac_sa[1];
        p_cpumac_hdr->macda[2] = p_gb_pkt_master[lchip]->cpu_mac_sa[2];
        p_cpumac_hdr->macda[3] = p_gb_pkt_master[lchip]->cpu_mac_sa[3];
        p_cpumac_hdr->macda[4] = p_gb_pkt_master[lchip]->cpu_mac_sa[4];
        p_cpumac_hdr->macda[5] = p_gb_pkt_master[lchip]->cpu_mac_sa[5];
        p_cpumac_hdr->macsa[0] = p_gb_pkt_master[lchip]->cpu_mac_da[0];
        p_cpumac_hdr->macsa[1] = p_gb_pkt_master[lchip]->cpu_mac_da[1];
        p_cpumac_hdr->macsa[2] = p_gb_pkt_master[lchip]->cpu_mac_da[2];
        p_cpumac_hdr->macsa[3] = p_gb_pkt_master[lchip]->cpu_mac_da[3];
        p_cpumac_hdr->macsa[4] = p_gb_pkt_master[lchip]->cpu_mac_da[4];
        p_cpumac_hdr->macsa[5] = p_gb_pkt_master[lchip]->cpu_mac_da[5];
        p_cpumac_hdr->vlan_tpid = sal_htons(0x8100);
        p_cpumac_hdr->vlan_vid = sal_htons(0);
        p_cpumac_hdr->type = sal_htons(0x5A5A);
        p_cpumac_hdr->reserved = sal_htons(0);
        SYS_PKT_DBG_INFO("[Packet Tx] Ethernet mode\n");
    }

    /* add stats */
    p_gb_pkt_master[lchip]->stats.encap++;

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_decap(uint8 lchip, ctc_pkt_rx_t* p_pkt_rx)
{
    ms_packet_header_t raw_hdr;
    ms_packet_header_t* p_raw_hdr = &raw_hdr;
    uint16 eth_hdr_len = 0;
    uint16 pkt_hdr_len = 0;
    uint16 stk_hdr_len = 0;
    int32 ret = 0;
    int8  i = 0;

    CTC_PTR_VALID_CHECK(p_pkt_rx);

    SYS_PKT_DBG_FUNC();
    SYS_PKT_DBG_INFO("len %d, buf_count %d \n", p_pkt_rx->pkt_len,
                     p_pkt_rx->buf_count);

    if (CTC_PKT_MODE_DMA == p_pkt_rx->mode)
    {
        for (i = 0; i < p_pkt_rx->buf_count; i++)
        {
            sys_greatbelt_packet_swap_payload(lchip, p_pkt_rx->pkt_buf[i].len, p_pkt_rx->pkt_buf[i].data);
        }
    }
    /* 1. check packet length */
    /* ethernet has 20 Bytes L2 header */
    if (CTC_PKT_MODE_ETH == p_pkt_rx->mode)
    {
        eth_hdr_len = SYS_GB_PKT_CPUMAC_LEN;
    }
    else
    {
        eth_hdr_len = 0;
    }

    pkt_hdr_len = SYS_GB_PKT_HEADER_LEN;

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
    sal_memcpy(p_raw_hdr, p_pkt_rx->pkt_buf[0].data + eth_hdr_len, SYS_GB_PKT_HEADER_LEN);

    /* 3. convert raw header to rx_info */
    CTC_ERROR_RETURN(_sys_greatbelt_packet_rawhdr_to_rxinfo(lchip, p_raw_hdr, p_pkt_rx));

	if (SYS_PACKET_BASIC_STACKING_HEADER_LEN == p_pkt_rx->stk_hdr_len)
    {
        uint16 stk_hdr_len = 0;
        sys_greatbelt_stacking_get_stkhdr_len(lchip, p_pkt_rx->rx_info.stacking_src_port, p_pkt_rx->rx_info.src_chip, &stk_hdr_len);
         p_pkt_rx->stk_hdr_len = stk_hdr_len;

         if (CTC_PKT_OPER_OAM == p_pkt_rx->rx_info.oper_type &&
		 	stk_hdr_len != 0)
         {
             if (stk_hdr_len > SYS_PACKET_BASIC_STACKING_HEADER_LEN)
             {
                 uint8 remove_len = 0;
                 remove_len = (stk_hdr_len - SYS_PACKET_BASIC_STACKING_HEADER_LEN);

                 sal_memmove(p_pkt_rx->pkt_buf[0].data + SYS_GB_PKT_HEADER_LEN,
                             p_pkt_rx->pkt_buf[0].data + SYS_GB_PKT_HEADER_LEN + remove_len,
                             p_pkt_rx->pkt_buf[0].len - SYS_GB_PKT_HEADER_LEN - remove_len);

                 p_pkt_rx->pkt_len -= remove_len;
                 p_pkt_rx->pkt_buf[0].len -= remove_len;
             }

             p_pkt_rx->stk_hdr_len = 0;
         }
         SYS_PKT_DBG_INFO("[Packet Rx] is from fabric\n");
    }


    /* add stats */
    p_gb_pkt_master[lchip]->stats.decap++;

    return ret;
}
int32
sys_greatbelt_packet_dump(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    return CTC_E_NONE;
}

/**
 @brief Display packet
*/
int32
sys_greatbelt_packet_stats_dump(uint8 lchip)
{
    uint16 idx = 0;
    uint64 rx = 0;
    char* p_str_tmp = NULL;
    char str[40] = {0};
    uint64 reason_cnt = 0;

    SYS_PACKET_INIT_CHECK(lchip);

    SYS_PKT_DUMP("Packet Tx Statistics:\n");
    SYS_PKT_DUMP("------------------------------\n");
    SYS_PKT_DUMP("%-20s: %"PRIu64" \n", "Total TX Count", (p_gb_pkt_master[lchip]->stats.uc_tx+p_gb_pkt_master[lchip]->stats.mc_tx));

    if ((p_gb_pkt_master[lchip]->stats.uc_tx) || (p_gb_pkt_master[lchip]->stats.mc_tx))
    {
        SYS_PKT_DUMP("--%-18s: %"PRIu64" \n", "Uc Count", p_gb_pkt_master[lchip]->stats.uc_tx);
        SYS_PKT_DUMP("--%-18s: %"PRIu64" \n", "Mc Count", p_gb_pkt_master[lchip]->stats.mc_tx);
    }

    SYS_PKT_DUMP("\n");

    for (idx  = 0; idx  < CTC_PKT_CPU_REASON_MAX_COUNT; idx ++)
    {
        rx += p_gb_pkt_master[lchip]->stats.rx[idx];
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
            reason_cnt = p_gb_pkt_master[lchip]->stats.rx[idx];

            if (reason_cnt)
            {
                p_str_tmp = sys_greatbelt_reason_2Str(lchip, idx);
                sal_sprintf((char*)&str, "%s%s%d%s", p_str_tmp, "(ID:", idx, ")");

                SYS_PKT_DUMP("%-20s: %"PRIu64" \n", (char*)&str, reason_cnt);
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_stats_clear(uint8 lchip)
{
    SYS_PACKET_INIT_CHECK(lchip);

    sal_memset(&p_gb_pkt_master[lchip]->stats, 0, sizeof(sys_pkt_stats_t));

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_set_reason_log(uint8 lchip, uint16 reason_id, uint8 enable, uint8 is_all, uint8 is_detail)
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
                CTC_BMP_SET(p_gb_pkt_master[lchip]->reason_bm, index);
                p_gb_pkt_master[lchip]->header_en[index] = is_detail;
            }
        }
        else
        {
            CTC_BMP_SET(p_gb_pkt_master[lchip]->reason_bm, reason_id);
            p_gb_pkt_master[lchip]->header_en[reason_id] = is_detail;
        }
    }
    else
    {
        if (is_all)
        {
            for (index = 0; index < CTC_PKT_CPU_REASON_MAX_COUNT; index++)
            {
                CTC_BMP_UNSET(p_gb_pkt_master[lchip]->reason_bm, index);
                p_gb_pkt_master[lchip]->header_en[index] = is_detail;
            }
        }
        else
        {
            CTC_BMP_UNSET(p_gb_pkt_master[lchip]->reason_bm, reason_id);
            p_gb_pkt_master[lchip]->header_en[reason_id] = is_detail;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_packet_dump_stacking_header(uint8 lchip, uint8* data, uint32 len)
{
    uint8* pkt_buf = NULL;
    uint8 pkt_hdr_out[32] = {0};
    uint32 value = 0;
    int32 ret = CTC_E_NONE;
    if (!data)
    {
        return CTC_E_INVALID_PARAM;
    }
    pkt_buf = mem_malloc(MEM_SYSTEM_MODULE, CTC_PKT_MTU*sizeof(uint8));
    if (!pkt_buf)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(pkt_buf, 0, CTC_PKT_MTU*sizeof(uint8));
    sal_memcpy(pkt_buf, data, len);

    sal_memcpy(pkt_hdr_out, pkt_buf, 32);
    CTC_ERROR_GOTO((sys_greatbelt_packet_swap_payload(lchip, 32, pkt_hdr_out)), ret, error0);

    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_Priority_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "Priority", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_DestMap_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: 0x%08x\n", "DestMap", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_HeaderType_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "HeaderType", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_HeaderVersion_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "HeaderVersion", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_LogicPortType_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "LogicPortType", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_LogicSrcPort_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "LogicSrcPort", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_OperationType_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "OperationType", value);
    if (CTC_PKT_OPER_C2C == value)
    {
        drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_NextHopPtr_f, (uint32*)pkt_hdr_out, &value);
        SYS_PKT_DUMP( "%-20s: %d\n", "NextHopPtr", value);
        drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_NextHopExt_f, (uint32*)pkt_hdr_out, &value);
        SYS_PKT_DUMP( "%-20s: %d\n", "NextHopExt", value);
    }
    else
    {
        drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_NextHopPtr_f, (uint32*)pkt_hdr_out, &value);
        SYS_PKT_DUMP( "%-20s: %d\n", "CpuReason", CTC_PKT_CPU_REASON_GET_BY_NHPTR(value));
    }
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_PacketType_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "PacketType", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_SourcePort_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: 0x%04x\n", "SourcePort", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_SrcVlanPtr_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "SrcVlanPtr", value);
    drv_greatbelt_get_field(PacketHeaderOuter_t, PacketHeaderOuter_Ttl_f, (uint32*)pkt_hdr_out, &value);
    SYS_PKT_DUMP( "%-20s: %d\n", "Ttl", value);

error0:
    if (pkt_buf)
    {
        mem_free(pkt_buf);
    }
    return ret;

}


int32
sys_greatbelt_packet_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag)
{
    uint8 buf_id = 0;
    uint8 idx = 0;

    SYS_PACKET_INIT_CHECK(lchip);

    CTC_MAX_VALUE_CHECK(buf_cnt, SYS_PKT_BUF_MAX);

    if (!CTC_IS_BIT_SET(flag, 0))
    {
        CTC_BIT_SET(flag, 0);
        buf_cnt = SYS_PKT_BUF_MAX;
    }

    buf_id = p_gb_pkt_master[lchip]->buf_id;

    while(buf_cnt)
    {
        buf_id = buf_id? (buf_id - 1):(SYS_PKT_BUF_MAX - 1);
        buf_cnt--;

        if (p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_len)
        {
            uint8 len = 0;
            char* p_time_str = NULL;
            ctc_pkt_rx_t pkt_rx;
            ms_packet_header_t raw_hdr;

            sal_memset(&pkt_rx, 0, sizeof(pkt_rx));
            sal_memset(&raw_hdr, 0, sizeof(raw_hdr));
            sal_memcpy(&raw_hdr, p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_data, SYS_GB_PKT_HEADER_LEN);

            SYS_PKT_DUMP("Packet No.%d, Pkt len: %d\n", idx++, p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_len);
            SYS_PKT_DUMP( "-----------------------------------------------\n");

            /* convert raw header to rx_info */
            CTC_ERROR_RETURN(_sys_greatbelt_packet_rawhdr_to_rxinfo(lchip, &raw_hdr, &pkt_rx));

            p_time_str = sal_ctime(&(p_gb_pkt_master[lchip]->pkt_buf[buf_id].tm));
            SYS_PKT_DUMP( "Timestamp:%s", p_time_str);

            if (CTC_IS_BIT_SET(flag, 1))
            {
                CTC_ERROR_RETURN(_sys_greatbelt_packet_dump_header(lchip, &pkt_rx));
            }

            if (CTC_IS_BIT_SET(flag, 2) && pkt_rx.stk_hdr_len)
            {
                SYS_PKT_DUMP("Stacking header info(Length : %d):\n", pkt_rx.stk_hdr_len);
                SYS_PKT_DUMP("-------------------------------------------------------------\n");
                _sys_greatbelt_packet_dump_stacking_header(lchip, p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_data+SYS_GB_PKT_HEADER_LEN, pkt_rx.stk_hdr_len);
                SYS_PKT_DUMP("-------------------------------------------------------------\n");
            }

            if (CTC_IS_BIT_SET(flag, 0))
            {
                /*print packet header*/
                _sys_greatbelt_packet_dump(lchip, p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_data, SYS_GB_PKT_HEADER_LEN);
                SYS_PKT_DUMP("-----------------------------------------------\n");
                SYS_PKT_DUMP("Packet Info(Length : %d):\n", (p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_len-SYS_GB_PKT_HEADER_LEN));
                SYS_PKT_DUMP("-----------------------------------------------\n");

                /*print packet*/
                len = (p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_len <= SYS_PKT_BUF_PKT_LEN)?
                (p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_len-SYS_GB_PKT_HEADER_LEN): (SYS_PKT_BUF_PKT_LEN-SYS_GB_PKT_HEADER_LEN);
                CTC_ERROR_RETURN(_sys_greatbelt_packet_dump(lchip, p_gb_pkt_master[lchip]->pkt_buf[buf_id].pkt_data+SYS_GB_PKT_HEADER_LEN, len));
            }

        }

    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_packet_tx_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag)
{
    uint8 cursor = 0;
    uint8 idx = 0;
    uint8 len = 0;
    uint8 header_len = 0;
    char* p_time_str = NULL;

    LCHIP_CHECK(lchip);
    if (p_gb_pkt_master[lchip] == NULL)
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
    cursor = (p_gb_pkt_master[lchip]->buf_id_tx);
    while(buf_cnt)
    {
        cursor = cursor? (cursor - 1):(SYS_PKT_BUF_MAX - 1);
        buf_cnt--;

        header_len = p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].mode?(SYS_GB_PKT_HEADER_LEN+SYS_GB_PKT_CPUMAC_LEN):SYS_GB_PKT_HEADER_LEN;

        if (!p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len)
        {
            continue;
        }

        SYS_PKT_DUMP( "Packet No.%d, Pkt len: %d\n", idx++, p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len);

        SYS_PKT_DUMP( "-----------------------------------------------\n");
        p_time_str = sal_ctime(&(p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].tm));
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

            p_pkt_info = &(p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].tx_info);
            sal_memcpy(&(p_pkt_tx->tx_info),p_pkt_info ,sizeof(ctc_pkt_info_t));
            _sys_greatbelt_packet_dump_tx_header(lchip, p_pkt_tx);
            mem_free(p_pkt_tx);

            if (p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].mode == CTC_PKT_MODE_ETH)
            {
                SYS_PKT_DUMP( "-----------------------------------------------\n");
                SYS_PKT_DUMP( "Packet CPU Header\n");
                SYS_PKT_DUMP( "-----------------------------------------------\n");

                _sys_greatbelt_packet_dump(lchip, p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data, SYS_GB_PKT_CPUMAC_LEN);
                cpu_mac_len = SYS_GB_PKT_CPUMAC_LEN;
            }
            SYS_PKT_DUMP( "-----------------------------------------------\n");
            SYS_PKT_DUMP( "Packet Bridge Header\n");
            SYS_PKT_DUMP( "-----------------------------------------------\n");

            _sys_greatbelt_packet_dump(lchip, p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data + cpu_mac_len, SYS_GB_PKT_HEADER_LEN);

        }

        if (CTC_IS_BIT_SET(flag, 0))
        {
            SYS_PKT_DUMP( "-----------------------------------------------\n");
            SYS_PKT_DUMP( "Packet Info(Length : %d):\n", (p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len -header_len));
            SYS_PKT_DUMP( "-----------------------------------------------\n");

            /*print packet*/
            len = (p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len <= SYS_PKT_BUF_PKT_LEN)?
            (p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_len -header_len ): (SYS_PKT_BUF_PKT_LEN -header_len);
            _sys_greatbelt_packet_dump(lchip, p_gb_pkt_master[lchip]->pkt_tx_buf[cursor].pkt_data+header_len, len);
        }

    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_buffer_clear(uint8 lchip ,uint8 bitmap)
{
    SYS_PACKET_INIT_CHECK(lchip);

    /*rx clear*/


    if (CTC_IS_BIT_SET(bitmap, 0))
    {
        sal_memset(&p_gb_pkt_master[lchip]->pkt_buf, 0, sizeof(p_gb_pkt_master[lchip]->pkt_buf));
        p_gb_pkt_master[lchip]->buf_id = 0;
    }

    /*tx clear*/
    if (CTC_IS_BIT_SET(bitmap, 1))
    {
        sal_memset(&p_gb_pkt_master[lchip]->pkt_tx_buf, 0, sizeof(p_gb_pkt_master[lchip]->pkt_tx_buf));
        p_gb_pkt_master[lchip]->buf_id_tx = 0;
    }

    return CTC_E_NONE;
}
int32
sys_greatbelt_packet_register_internal_rx_cb(uint8 lchip, SYS_PKT_RX_CALLBACK internal_rx_cb)
{
    SYS_PACKET_INIT_CHECK(lchip);

    p_gb_pkt_master[lchip]->internal_rx_cb = internal_rx_cb;
    return CTC_E_NONE;
}

STATIC void
_sys_greatbelt_packet_tx_callback_thread(void *param)
{
    int32 ret = 0;
    uint8 lchip = 0;
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;

    lchip = (uintptr)param;
    while (1) {
        ret = sal_sem_take(p_gb_pkt_master[lchip]->tx_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
next_pkt:
        SYS_PKT_TX_LOCK(lchip); /* Grab the lists from the interrupt handler */
        CTC_LIST_LOOP_DEL(p_gb_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info, node, next_node)
        {
            ctc_listnode_delete_node(p_gb_pkt_master[lchip]->async_tx_pkt_list, node);
            if (CTC_FLAG_ISSET(p_pkt_info->flags, CTC_PKT_FLAG_MCAST))
            {
                 p_gb_pkt_master[lchip]->stats.mc_tx++;
            }
            else
            {
                 p_gb_pkt_master[lchip]->stats.uc_tx++;
            }
            break;
        }
        SYS_PKT_TX_UNLOCK(lchip);
        if (NULL != p_pkt_info)
        {
            sys_greatbelt_dma_pkt_tx(lchip, (ctc_pkt_tx_t*)p_pkt_info);

            (p_pkt_info->callback)(p_pkt_info->p_pkt_tx, p_pkt_info->user_data);
            sys_greatbelt_packet_tx_free(lchip, p_pkt_info);
            p_pkt_info = NULL;
            goto next_pkt;
        }

    }

}

int32
sys_greatbelt_packet_init(uint8 lchip, void* p_global_cfg)
{
    ctc_pkt_global_cfg_t* p_pkt_cfg = (ctc_pkt_global_cfg_t*)p_global_cfg;
    int32 ret = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    /* 2. allocate interrupt master */
    if (p_gb_pkt_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gb_pkt_master[lchip] = (sys_pkt_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_pkt_master_t));
    if (NULL == p_gb_pkt_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_pkt_master[lchip], 0, sizeof(sys_pkt_master_t));
    sal_memcpy(&p_gb_pkt_master[lchip]->cfg, p_pkt_cfg, sizeof(ctc_pkt_global_cfg_t));
    sys_greatbelt_get_chip_cpumac(lchip, p_gb_pkt_master[lchip]->cpu_mac_sa, p_gb_pkt_master[lchip]->cpu_mac_da);

    p_gb_pkt_master[lchip]->async_tx_pkt_list= ctc_list_new();
    if (NULL == p_gb_pkt_master[lchip]->async_tx_pkt_list)
    {
        mem_free(p_gb_pkt_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    SYS_PKT_CREAT_TX_LOCK(lchip);
    ret = sal_sem_create(&p_gb_pkt_master[lchip]->tx_sem, 0);
    if (ret < 0)
    {
        mem_free(p_gb_pkt_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    sal_sprintf(buffer, "asyncTxTask-%d", lchip);
    ret = sal_task_create(&p_gb_pkt_master[lchip]->p_async_tx_task, buffer,
        SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_greatbelt_packet_tx_callback_thread, (void*)(uintptr)lchip);
    if (ret < 0)
    {
        mem_free(p_gb_pkt_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    ret = _sys_greatbelt_packet_init_svlan_tpid_index(lchip);
    if (ret < 0)
    {
        mem_free(p_gb_pkt_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_packet_deinit(uint8 lchip)
{
    sys_pkt_tx_info_t* p_pkt_info = NULL;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_pkt_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_sem_give(p_gb_pkt_master[lchip]->tx_sem);
    sal_task_destroy(p_gb_pkt_master[lchip]->p_async_tx_task);
    sal_sem_destroy(p_gb_pkt_master[lchip]->tx_sem);

    if (p_gb_pkt_master[lchip]->tx_spinlock)
    {
        ctc_sal_spinlock_destroy(p_gb_pkt_master[lchip]->tx_spinlock);
    }

    CTC_LIST_LOOP_DEL(p_gb_pkt_master[lchip]->async_tx_pkt_list, p_pkt_info, node, next_node)
    {
        (p_pkt_info->callback)(p_pkt_info->p_pkt_tx, p_pkt_info->user_data);
        sys_greatbelt_packet_tx_free(lchip, p_pkt_info);
        ctc_listnode_delete_node(p_gb_pkt_master[lchip]->async_tx_pkt_list, node);
    }
    ctc_list_free(p_gb_pkt_master[lchip]->async_tx_pkt_list);
    mem_free(p_gb_pkt_master[lchip]);

    return CTC_E_NONE;
}


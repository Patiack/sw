/**
 @file ctc_greatbelt_oam.c

 @date 2010-3-9

 @version v2.0

  This file contains oam sys layer function implementation
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_oam.h"
#include "ctc_port.h"
#include "ctc_error.h"
#include "ctc_crc.h"
#include "ctc_common.h"
#include "ctc_packet.h"
#include "sys_greatbelt_packet.h"
#include "sys_greatbelt_oam_com.h"
#include "sys_greatbelt_oam_trpt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_common.h"


/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

#define OAM_CYCLE_MUL 64

extern int32
sys_greatbelt_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size);

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
extern sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

sys_oam_trpt_master_t* p_gb_trpt_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

int32
sys_greatbelt_oam_trpt_glb_cfg_check(uint8 lchip, ctc_oam_trpt_t* p_trpt)
{
    /* kbps */
    uint32 max_speed = 0;
    uint32 speed_mode = 0;
    uint32 cmd      = 0;
    auto_gen_pkt_ctl_t pkt_ctl;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trpt);

    if (p_trpt->vlan_id)
    {
        CTC_VLAN_RANGE_CHECK(p_trpt->vlan_id);
    }

    if (0 != p_trpt->header_len)
    {
        CTC_PTR_VALID_CHECK(p_trpt->pkt_header);

        if (p_trpt->header_len > 64)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (p_trpt->session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /*check is enable or not*/
    sal_memset(&pkt_ctl, 0, sizeof(auto_gen_pkt_ctl_t));
    cmd = DRV_IOR(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    if(CTC_IS_BIT_SET(pkt_ctl.auto_gen_en, p_trpt->session_id))
    {
        return CTC_E_OAM_TRPT_SESSION_EN;
    }

    if (p_trpt->direction >= CTC_BOTH_DIRECTION)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* valid, >=64 && >=p_trpt->header_len+4 */
    if ((p_trpt->size < 64) || (p_trpt->size < (p_trpt->header_len+4)))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_MAX_VALUE_CHECK(p_trpt->size, 16128);
    if (0 == p_trpt->nhid)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, p_trpt->gport, CTC_PORT_PROP_SPEED, &speed_mode));
        switch (speed_mode)
        {
            case CTC_PORT_SPEED_1G:
                max_speed = 1000000;
                break;
            case CTC_PORT_SPEED_100M:
                max_speed = 100000;
                break;
            case CTC_PORT_SPEED_10M:
                max_speed = 10000;
                break;
            case CTC_PORT_SPEED_2G5:
                max_speed = 2500000;
                break;
            case CTC_PORT_SPEED_10G:
                max_speed = 10000000;
                break;
            default:
                max_speed = 1000000;
                break;
        }
        if ((max_speed) && ((p_trpt->rate < 16) || (p_trpt->rate > max_speed)))
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    /* kbps, 16k - speed of dest port */


    CTC_MAX_VALUE_CHECK(p_trpt->tx_seq_en, 1);
    /* 14-31 */
    if ((p_trpt->tx_seq_en) &&
        ((p_trpt->seq_num_offset < 14) || (p_trpt->seq_num_offset > 31)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_trpt->pattern_type >= CTC_OAM_PATTERN_MAX_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }


    if (p_trpt->tx_mode > 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((0 == p_trpt->tx_mode) && ((0 == p_trpt->packet_num)))
    {
       return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
@ brief config section packet header parameter
*/
int32
sys_greatbelt_oam_trpt_set_bridgr_hdr(uint8 lchip, ctc_oam_trpt_t* p_trpt,  ms_packet_header_t* p_bridge_hdr)
{
    uint32 nexthop_ptr = 0;
    uint8 header_crc = 0;
    uint8  lport = 0;
    uint8 gchip = 0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_bridge_hdr);

    gchip = CTC_MAP_GPORT_TO_GCHIP(p_trpt->gport);
    lport = CTC_MAP_GPORT_TO_LPORT(p_trpt->gport);

    if (p_trpt->direction == CTC_INGRESS)/*iloop*/
    {
        p_bridge_hdr->dest_map = (gchip << 16) | SYS_RESERVE_PORT_ID_ILOOP;
        p_bridge_hdr->next_hop_ptr = lport;
    }
    else
    {
        if (p_trpt->nhid)
        {
            sys_nh_info_dsnh_t nhinfo;
            sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_trpt->nhid, &nhinfo));
            if (nhinfo.is_mcast)
            {
                p_bridge_hdr->dest_map = SYS_GREATBELT_BUILD_DESTMAP(1, nhinfo.dest_chipid, nhinfo.dest_id);
                p_bridge_hdr->next_hop_ptr = 0;
            }
            else
            {
                p_bridge_hdr->dest_map = SYS_GREATBELT_BUILD_DESTMAP(0, nhinfo.dest_chipid, nhinfo.dest_id);
                p_bridge_hdr->next_hop_ptr = nhinfo.dsnh_offset;
                p_bridge_hdr->next_hop_ext = nhinfo.nexthop_ext;
            }
        }
        else
        {
            sys_greatbelt_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &nexthop_ptr);
            p_bridge_hdr->dest_map = (gchip << 16) | lport;
            p_bridge_hdr->next_hop_ptr = nexthop_ptr;
            p_bridge_hdr->next_hop_ext = 1;
        }
    }
    /* get bypass nexthop */

    p_bridge_hdr->source_port = SYS_RESERVE_PORT_ID_OAM;
    p_bridge_hdr->src_cvlan_id = 1;
    p_bridge_hdr->src_vlan_ptr = p_trpt->vlan_id;
    p_bridge_hdr->color = 3;
    p_bridge_hdr->from_cpu_or_oam = 1;
    p_bridge_hdr->operation_type = 7;

    sys_greatbelt_packet_swap32(lchip, (uint32*)p_bridge_hdr, SYS_GB_PKT_HEADER_LEN / 4, TRUE);

    /* calc bridge header CRC */
    header_crc = ctc_crc_calculate_crc4((uint8*)p_bridge_hdr, BRIDGE_HEADER_WORD_NUM*4, 0);
    sys_greatbelt_packet_swap32(lchip, (uint32*)p_bridge_hdr, SYS_GB_PKT_HEADER_LEN / 4, TRUE);

    p_bridge_hdr->header_crc = (header_crc & 0xF);


    return CTC_E_NONE;
}

/**
@ brief config section packet header parameter
*/
int32
sys_greatbelt_oam_trpt_set_pkthd(uint8 lchip, uint8 session_id, void* p_header, uint8 header_length)
{
    uint32 cmd = 0;
    uint32 up_ptr = 0;
    auto_gen_pkt_pkt_hdr_t ds_pkt_hdr;
    uint32* p_hdr = NULL;
    uint8 index = 0;
    uint8 remnant = 0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_header);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    up_ptr = ((session_id << 4) - (session_id << 2));

    SYS_OAM_DBG_INFO("PktEntry Ptr:%d \n", up_ptr);

    sal_memset(&ds_pkt_hdr, 0, sizeof(auto_gen_pkt_pkt_hdr_t));

    /* config one section */
    p_hdr = (uint32*)p_header;

    sys_greatbelt_packet_swap32(lchip, p_hdr + (SYS_GB_PKT_HEADER_LEN / 4), (header_length) / 4, TRUE);

    /*1. config bridge header */
    for (index = 0; index < BRIDGE_HEADER_WORD_NUM/2; index++)
    {
        ds_pkt_hdr.pkt_hdr_userdata0 = p_hdr[index*2];
        ds_pkt_hdr.pkt_hdr_userdata1 = p_hdr[index*2+1];

        cmd = DRV_IOW(AutoGenPktPktHdr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (up_ptr+index), cmd, &ds_pkt_hdr));
    }

    /*2. config packet header */
    for (index = 0; index < header_length/8; index++)
    {
        ds_pkt_hdr.pkt_hdr_userdata0 = p_hdr[BRIDGE_HEADER_WORD_NUM+index*2];
        ds_pkt_hdr.pkt_hdr_userdata1 = p_hdr[BRIDGE_HEADER_WORD_NUM+index*2+1];

        cmd = DRV_IOW(AutoGenPktPktHdr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (up_ptr+index+4), cmd, &ds_pkt_hdr));
    }

    remnant = header_length % 8;
    index = header_length/8;

    if (remnant)
    {
        sal_memset(&ds_pkt_hdr, 0, sizeof(auto_gen_pkt_pkt_hdr_t));
        sal_memcpy((uint8*)&ds_pkt_hdr, (uint8*)((uint8*)p_hdr+index*8 +32), remnant);
        cmd = DRV_IOW(AutoGenPktPktHdr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (up_ptr+index+4), cmd, &ds_pkt_hdr));
    }

    return CTC_E_NONE;
}


/**
@ brief gen autogen section enable
*/
bool
_sys_greatbelt_oam_trpt_get_enable(uint8 lchip, uint8 session_id)
{
    uint32 cmd = 0;
    auto_gen_pkt_ctl_t pkt_ctl;

    sal_memset(&pkt_ctl, 0, sizeof(auto_gen_pkt_ctl_t));

    cmd = DRV_IOR(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    if ((pkt_ctl.auto_gen_en >> session_id) & 0x01)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}



STATIC int32
_sys_greatbelt_oam_trpt_check_conflict(uint8 lchip, uint8 session_id)
{
    uint8 index = 0;
    uint8 not_match = FALSE;
    uint32 tick_interval = 0;

    tick_interval = p_gb_trpt_master[lchip]->session_info[session_id].act_cycle;

    /* adjust different session used cycle whether is match */
    for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
    {
        if (_sys_greatbelt_oam_trpt_get_enable(lchip, index))
        {
            if (p_gb_trpt_master[lchip]->session_info[index].act_cycle != tick_interval)
            {
                not_match = TRUE;
                break;
            }

            if (p_gb_trpt_master[lchip]->session_info[index].is_over_1g)
            {
                not_match = TRUE;
                break;
            }
        }
    }

    if (not_match)
    {
        SYS_OAM_DBG_INFO("tlcks interval is conflict with other section, please check!! \n");
        return CTC_E_OAM_TRPT_INTERVAL_CONFLICT;
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_oam_trpt_cal_rate(uint8 lchip, uint8 session_id, uint16 size, uint32 rate)
{
    uint64 core_freq = 0;
    uint8 ipg = 0;
    uint8 session_num = 4;
    uint32 min_interval = 0;
    uint32 max_session_num = 0;
    uint64 interval = 0;
    uint64 rate_remainder = 0-1;
    uint64 new_rate_remainder = 0;
    uint64 divisor = 10;
    uint64 temp = 0;
    uint64 temp1 = 0;
    uint8 success = 0;

    SYS_OAM_DBG_FUNC();

    /* get core frequency */
    core_freq = sys_greatbelt_get_core_freq(lchip);

    /* get ipg */
    CTC_ERROR_RETURN(sys_greatbelt_get_ipg_size(lchip, 0, &ipg));

    p_gb_trpt_master[lchip]->session_info[session_id].is_over_1g = (rate <= 1000000)?0:1;
    session_num = (rate <= 1000000)?4:1;
    temp = 8*(size+32)/ OAM_CYCLE_MUL+ 3;
    min_interval = (uint32)temp;
    for (interval = min_interval;interval<=0xFFFF ; interval++)
    {
        temp = 8000*core_freq*(size + ipg);
        temp = temp/(rate*interval);
        max_session_num = (uint32)temp;
        if (max_session_num < session_num)
        {
            break;
        }
        temp = 8000*core_freq*(size + ipg)*10;
        temp = temp%((interval*10 + divisor)*rate*session_num);
        new_rate_remainder = temp;

        if (new_rate_remainder < rate_remainder)
        {
            /*ticks_cnt*/
            temp = (8000*core_freq*(size + ipg)*10) ;
            temp = temp/ ((interval*10 + divisor)*rate*session_num);
            /*ticks_frace*/
            temp1 = (8000*core_freq*(size + ipg)*10) ;
            temp1 =temp1 % ((interval*10 + divisor)*rate*session_num);
            temp1 = (temp1 *65536)/((interval*10 + divisor)*rate*session_num);

            if((temp <= 0xFFFF)&& (temp1 <= 0xFFFF))
            {
                p_gb_trpt_master[lchip]->session_info[session_id].act_cycle = (uint16)interval;
                p_gb_trpt_master[lchip]->session_info[session_id].ticks_cnt = (uint16)temp;
                p_gb_trpt_master[lchip]->session_info[session_id].ticks_frace = (uint16)temp1;
                rate_remainder = new_rate_remainder;
                success = 1;
                if (0 == rate_remainder)
                {
                    break;
                }
            }
        }
    }
    if (0 == success)
    {
       return  CTC_E_INVALID_PARAM;
    }

    SYS_OAM_DBG_INFO("cal rate result:\nact_cycle: %u ticks_cnt: %u ticks_frace: %u\n",
        p_gb_trpt_master[lchip]->session_info[session_id].act_cycle,
        p_gb_trpt_master[lchip]->session_info[session_id].ticks_cnt,
        p_gb_trpt_master[lchip]->session_info[session_id].ticks_frace );


   return CTC_E_NONE;
}

/**
@ brief config section packet header parameter
*/
int32
sys_greatbelt_oam_trpt_set_rate(uint8 lchip, uint8 session_id, ctc_oam_trpt_t* p_rate)
{
    auto_gen_pkt_pkt_cfg_t autogen_pkt_cfg;
    uint32 cmd = 0;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    sal_memset(&autogen_pkt_cfg, 0, sizeof(auto_gen_pkt_pkt_cfg_t));

    /* cal rate */
    CTC_ERROR_RETURN(_sys_greatbelt_oam_trpt_cal_rate(lchip, session_id, p_rate->size, p_rate->rate));

    autogen_pkt_cfg.seq_num_offset = p_rate->seq_num_offset + 32;
    autogen_pkt_cfg.tx_mode = p_rate->tx_mode;
    autogen_pkt_cfg.tx_seq_num_en = p_rate->tx_seq_en;
    autogen_pkt_cfg.is_have_end_tlv = 1;
    autogen_pkt_cfg.pkt_hdr_len = p_rate->header_len + 32;
    autogen_pkt_cfg.pattern_type = p_rate->pattern_type;
    autogen_pkt_cfg.repeat_pattern = p_rate->repeat_pattern;
    autogen_pkt_cfg.tx_pkt_cnt = p_rate->packet_num;
    autogen_pkt_cfg.tx_pkt_size = p_rate->size;
    autogen_pkt_cfg.rate_cnt_cfg = p_gb_trpt_master[lchip]->session_info[session_id].ticks_cnt;
    autogen_pkt_cfg.rate_frac_cnt_cfg = p_gb_trpt_master[lchip]->session_info[session_id].ticks_frace;

    /*cfg AutoGenPktPktCfg */
    cmd = DRV_IOW(AutoGenPktPktCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &autogen_pkt_cfg));

    return CTC_E_NONE;
}

/**
@ brief config autogem section enable
*/
int32
sys_greatbelt_oam_trpt_set_enable(uint8 lchip, uint8 session_id, uint8 enable)
{
    uint32 cmd = 0;
    auto_gen_pkt_ctl_t pkt_ctl;
    uint8 max_session = 0;
    auto_gen_pkt_pkt_cfg_t autogen_pkt_cfg;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    sal_memset(&pkt_ctl, 0, sizeof(auto_gen_pkt_ctl_t));

    cmd = DRV_IOR(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    if (enable)
    {
        cmd = DRV_IOR(AutoGenPktPktCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &autogen_pkt_cfg));

        if (autogen_pkt_cfg.pkt_hdr_len == 0)
        {
            return CTC_E_OAM_TRPT_SESSION_NOT_CFG;
        }

        if (p_gb_trpt_master[lchip]->session_info[session_id].is_over_1g)
        {
            if (pkt_ctl.auto_gen_en)
            {
                SYS_OAM_DBG_INFO("For over 1g only support 1 session!! \n");
                return CTC_E_OAM_TRPT_INTERVAL_CONFLICT;
            }

            max_session = 0;
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_trpt_check_conflict(lchip, session_id));
            max_session = TRPT_SESSION_NUM_USED - 1;
        }

        pkt_ctl.tick_gen_en = 1;
        pkt_ctl.tick_gen_interval =  p_gb_trpt_master[lchip]->session_info[session_id].act_cycle;
        pkt_ctl.auto_gen_en |= (1 << session_id);
        pkt_ctl.upd_valid_ptr = max_session;
    }
    else
    {
        pkt_ctl.auto_gen_en &= ~(1 << session_id);
    }

    cmd = DRV_IOW(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_trpt_set_autogen_ptr(uint8 lchip, uint8 session_id, uint16 lmep_idx)
{
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    p_gb_trpt_master[lchip]->session_info[session_id].lmep_index = lmep_idx;

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_trpt_get_autogen_ptr(uint8 lchip, uint16 mep_id, uint8* p_ptr)
{
    uint8 index = 0;
    uint8 find_flag = 0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);

    for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
    {
        if (mep_id ==  p_gb_trpt_master[lchip]->session_info[index].lmep_index)
        {
            find_flag = 1;
            break;
        }
    }

    if (find_flag)
    {
        *p_ptr = index;
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

STATIC int32
sys_greatbelt_oam_trpt_get_stats_hw(uint8 lchip, uint8 session_id, ctc_oam_trpt_stats_t* p_stat_info)
{
    uint32 cmd = 0;
    auto_gen_pkt_tx_pkt_stats_t tx_stats;
    auto_gen_pkt_rx_pkt_stats_t rx_stats;
    ctc_oam_trpt_stats_t* p_old_stats;
    uint64 tmp =0;

    sal_memset(&tx_stats, 0, sizeof(auto_gen_pkt_tx_pkt_stats_t));
    sal_memset(&rx_stats, 0, sizeof(auto_gen_pkt_rx_pkt_stats_t));

    OAM_TRPT_LOCK;
    /* get stats from AutoGenPktTxPktStats and AutoGenPktRxPktStats */
    cmd = DRV_IOR(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, session_id, cmd, &tx_stats), p_gb_trpt_master[lchip]->p_trpt_mutex);

    cmd = DRV_IOR(AutoGenPktRxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, session_id, cmd, &rx_stats), p_gb_trpt_master[lchip]->p_trpt_mutex);

    tmp = (rx_stats.rx_octets1);
    tmp <<= 32;
    tmp |= rx_stats.rx_octets0;
    p_stat_info->rx_oct = tmp;
    p_stat_info->rx_pkt = rx_stats.rx_pkts;

    tmp = (tx_stats.tx_octets1);
    tmp <<= 32;
    tmp |= tx_stats.tx_octets0;
    p_stat_info->tx_oct = tmp;
    p_stat_info->tx_pkt = tx_stats.tx_pkts;


    p_old_stats = &(p_gb_trpt_master[lchip]->session_stats[session_id]);

    if (p_stat_info->rx_oct < p_old_stats->rx_oct)
    {
        p_gb_trpt_master[lchip]->overflow_rec[session_id].rx_oct_overflow_cnt++;
    }

    if (p_stat_info->rx_pkt < p_old_stats->rx_pkt)
    {
        p_gb_trpt_master[lchip]->overflow_rec[session_id].rx_pkt_overflow_cnt++;
    }

    if (p_stat_info->tx_oct < p_old_stats->tx_oct)
    {
        p_gb_trpt_master[lchip]->overflow_rec[session_id].tx_oct_overflow_cnt++;
    }

    if (p_stat_info->tx_pkt < p_old_stats->tx_pkt)
    {
        p_gb_trpt_master[lchip]->overflow_rec[session_id].tx_pkt_overflow_cnt++;
    }

    sal_memcpy(p_old_stats, p_stat_info, sizeof(ctc_oam_trpt_stats_t));

    OAM_TRPT_UNLOCK;

    return CTC_E_NONE;
}
/**
 @brief oam throughput get stats information
*/
int32
sys_greatbelt_oam_trpt_get_stats(uint8 lchip, uint8 session_id, ctc_oam_trpt_stats_t* p_stat_info)
{
    ctc_oam_trpt_stats_t hw_stats;
    sys_oam_trpt_overflow_rec_t* overflow_rec = NULL;
    uint64 full_oct_stats = 0;
    uint64 full_pkt_stats = 0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_stat_info);

    sal_memset(&hw_stats, 0, sizeof(ctc_oam_trpt_stats_t));

    CTC_ERROR_RETURN(sys_greatbelt_oam_trpt_get_stats_hw(lchip, session_id, &hw_stats));

    SYS_OAM_DBG_INFO("get stats Hw, rx_oct:0x%"PRIu64" rx_pkt:0x%"PRIu64" tx_oct:0x%"PRIu64"  tx_pkt:0x%"PRIu64"\n", hw_stats.rx_oct,
        hw_stats.rx_pkt, hw_stats.tx_oct, hw_stats.tx_pkt);

    full_oct_stats = 0x3f;
    full_oct_stats <<= 32;
    full_oct_stats |=  0xffffffff;
    full_pkt_stats = 0xffffffff;
    overflow_rec = &(p_gb_trpt_master[lchip]->overflow_rec[session_id]);

    SYS_OAM_DBG_INFO("Over Flow Info, rx_oct:0x%x rx_pkt:0x%x tx_oct:0x%x tx_pkt:0x%x\n", overflow_rec->rx_oct_overflow_cnt,
        overflow_rec->rx_pkt_overflow_cnt, overflow_rec->tx_oct_overflow_cnt, overflow_rec->tx_pkt_overflow_cnt);

    p_stat_info->rx_oct = hw_stats.rx_oct + (full_oct_stats+1)*(overflow_rec->rx_oct_overflow_cnt);
    p_stat_info->rx_pkt = hw_stats.rx_pkt + (full_pkt_stats+1)*(overflow_rec->rx_pkt_overflow_cnt);
    p_stat_info->tx_oct = hw_stats.tx_oct+ (full_oct_stats+1)*(overflow_rec->tx_oct_overflow_cnt);
    p_stat_info->tx_pkt = hw_stats.tx_pkt+ (full_pkt_stats+1)*(overflow_rec->tx_pkt_overflow_cnt);

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_trpt_clear_stats(uint8 lchip, uint8 session_id)
{
    uint32 cmd = 0;
    auto_gen_pkt_tx_pkt_stats_t tx_stats;
    auto_gen_pkt_rx_pkt_stats_t rx_stats;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    sal_memset(&tx_stats, 0, sizeof(auto_gen_pkt_tx_pkt_stats_t));
    sal_memset(&rx_stats, 0, sizeof(auto_gen_pkt_rx_pkt_stats_t));


    /* get stats from AutoGenPktTxPktStats and AutoGenPktRxPktStats */
    cmd = DRV_IOW(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_stats));

    cmd = DRV_IOW(AutoGenPktRxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &rx_stats));

    sal_memset((void*)&(p_gb_trpt_master[lchip]->session_stats[session_id]), 0, sizeof(ctc_oam_trpt_stats_t));
    sal_memset((void*)&(p_gb_trpt_master[lchip]->overflow_rec[session_id]), 0, sizeof(sys_oam_trpt_overflow_rec_t));

    return CTC_E_NONE;
}

void
_sys_greatbelt_oam_trpt_stats_thread(void* user_param)
{
    ctc_oam_trpt_stats_t trpt_stats;
    uint8 index = 0;
    uint8 lchip = (uintptr)user_param;
    int32 ret = 0;
    uint16 lmep_index;                    /* lmep index */
    uint32 cmd = 0;
    ds_eth_mep_t ds_eth_mep;

    sal_memset(&trpt_stats, 0, sizeof(ctc_oam_trpt_stats_t));
    sal_memset(&ds_eth_mep, 0, sizeof(ds_eth_mep_t));

    while (1)
    {
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
        for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
        {
            lmep_index = p_gb_trpt_master[lchip]->session_info[index].lmep_index;

            cmd = DRV_IOR(DsEthMep_t, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, lmep_index, cmd, &ds_eth_mep);

            if (ds_eth_mep.auto_gen_en || _sys_greatbelt_oam_trpt_get_enable(lchip, index))
            {
                ret = sys_greatbelt_oam_trpt_get_stats_hw(lchip, index, &trpt_stats);
                if (ret < 0)
                {
                    SYS_OAM_DBG_INFO("get trpt stats fail! \n");
                    continue;
                }
            }
        }

        /* 2 minutes scan once */
        sal_task_sleep(2*60*1000);
    }

    return;
}

int32
sys_greatbelt_oam_trpt_show_state(uint8 lchip, uint8 gchip)
{
    uint8 index = 0;
    sys_oam_trpt_session_info_t* session_info = NULL;
    uint32 cmd = 0;
    auto_gen_pkt_ctl_t pkt_ctl;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);

    SYS_OAM_DBG_DUMP("Oam TroughPut State: \n");

    SYS_OAM_DBG_DUMP("%-9s %-9s %-9s %-10s  %-9s %-9s\n", "Session", "Enable", "lmep", "Tick(cycles)", "RateCnt", "RateFrac");
    SYS_OAM_DBG_DUMP("----------------------------------------------------\n");

    sal_memset(&pkt_ctl, 0, sizeof(auto_gen_pkt_ctl_t));

    cmd = DRV_IOR(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));
    /* dump session information */
    for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
    {
        session_info = &(p_gb_trpt_master[lchip]->session_info[index]);

        SYS_OAM_DBG_DUMP("%-9d %-9s %-9d %-10d %-9d %-9d\n", index, (((pkt_ctl.auto_gen_en>>index)&0x01)?("Enable"):("Disable")),
            session_info->lmep_index, session_info->act_cycle, session_info->ticks_cnt, session_info->ticks_frace)
    }

    return CTC_E_NONE;
}

/**
 @brief oam throughput init
*/
int32
sys_greatbelt_oam_trpt_init(uint8 lchip)
{
    int32 ret = 0;
    uint8 session_id = 0;
    uint8 index = 0;
    uint32 cmd = 0;
    auto_gen_pkt_pkt_cfg_t autogen_pkt_cfg;
    auto_gen_pkt_ctl_t autogen_cfg;
    auto_gen_pkt_pkt_hdr_t autogen_hdr;
    uintptr lchip_tmp = lchip;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    if (p_gb_trpt_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    if(NULL == g_gb_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gb_trpt_master[lchip] = (sys_oam_trpt_master_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_trpt_master_t)*CTC_OAM_TRPT_SESSION_NUM);
    if (p_gb_trpt_master[lchip] == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_trpt_master[lchip], 0, sizeof(sys_oam_trpt_master_t));

    sal_sprintf(buffer, "TrptStats-%d", lchip);
    ret = sal_task_create(&p_gb_trpt_master[lchip]->trpt_stats_thread, buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_greatbelt_oam_trpt_stats_thread, (void*)lchip_tmp);
    if (ret < 0)
    {
        return CTC_E_NOT_INIT;
    }

    sal_mutex_create(&(p_gb_trpt_master[lchip]->p_trpt_mutex));
    if (NULL == p_gb_trpt_master[lchip]->p_trpt_mutex)
    {
        mem_free(p_gb_trpt_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }


    sal_memset(&autogen_pkt_cfg, 0, sizeof(auto_gen_pkt_pkt_cfg_t));
    sal_memset(&autogen_cfg, 0, sizeof(auto_gen_pkt_ctl_t));
    sal_memset(&autogen_hdr, 0, sizeof(auto_gen_pkt_pkt_hdr_t));

    /* clear trpt cfg */

    cmd = DRV_IOW(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &autogen_cfg));

    for (session_id = 0; session_id < CTC_OAM_TRPT_SESSION_NUM; session_id++)
    {
        sys_greatbelt_oam_trpt_clear_stats(lchip, session_id);

        cmd = DRV_IOW(AutoGenPktPktCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &autogen_pkt_cfg));
    }

    for (index = 0; index < 48; index++)
    {
        cmd = DRV_IOW(AutoGenPktPktHdr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &autogen_hdr));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_trpt_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_trpt_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_task_destroy(p_gb_trpt_master[lchip]->trpt_stats_thread);

    sal_mutex_destroy(p_gb_trpt_master[lchip]->p_trpt_mutex);
    mem_free(p_gb_trpt_master[lchip]);

    return CTC_E_NONE;
}


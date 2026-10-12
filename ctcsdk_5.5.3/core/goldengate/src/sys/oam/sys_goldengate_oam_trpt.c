/**
 @file sys_goldengate_oam_trpt.c

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
#include "sys_goldengate_oam_com.h"
#include "sys_goldengate_oam_trpt.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_oam_debug.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_common.h"
#include "goldengate/include/drv_chip_ctrl.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_OAM_TRPT_CYCLE_MUL 64
#define SYS_OAM_TRPT_PERIOD_MAX (0xFFFFFFFFFFLL) /*40bits*/
#define SYS_OAM_TRPT_1G  (1000000)
#define SYS_OAM_TRPT_ENTRY_PER_SESSION  (12)
#define SYS_OAM_TRPT_BYTE_PER_ENTRY (8)

extern int32
sys_goldengate_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size);

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
extern sys_oam_master_t* g_gg_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

sys_oam_trpt_master_t* p_gg_trpt_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_OAM_TRPT_INIT_CHECK(lchip) \
do { \
    if (NULL == p_gg_trpt_master[lchip]) \
    { \
        return CTC_E_OAM_TRPT_NOT_INIT; \
    } \
} while (0)

#define OAM_TRPT_LOCK(lchip) \
    do { \
        if (p_gg_trpt_master[lchip]->p_trpt_mutex){ \
            sal_mutex_lock(p_gg_trpt_master[lchip]->p_trpt_mutex); } \
    } while (0)

#define OAM_TRPT_UNLOCK(lchip) \
   do { \
        if (p_gg_trpt_master[lchip]->p_trpt_mutex){ \
            sal_mutex_unlock(p_gg_trpt_master[lchip]->p_trpt_mutex); } \
    } while (0)

int32
sys_goldengate_oam_trpt_glb_cfg_check(uint8 lchip, ctc_oam_trpt_t* p_trpt)
{
    uint16  lport = 0;
    uint32 max_speed = 0;
    uint32 speed_mode = CTC_PORT_SPEED_10G;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trpt);

    if (p_trpt->vlan_id)
    {
        CTC_VLAN_RANGE_CHECK(p_trpt->vlan_id);
    }

    if (0 != p_trpt->header_len)
    {
        CTC_PTR_VALID_CHECK(p_trpt->pkt_header);

        if (p_trpt->header_len > (SYS_OAM_TRPT_ENTRY_PER_SESSION*SYS_OAM_TRPT_BYTE_PER_ENTRY - SYS_GG_PKT_HEADER_LEN))/*every sesseion use 12 entries*/
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (p_trpt->session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /*check enable or not*/
    if(p_gg_trpt_master[lchip]->session_info[p_trpt->session_id].enable)
    {
        return CTC_E_OAM_TRPT_SESSION_ALREADY_EN;
    }


    /* valid, >=64 && >=p_trpt->header_len+4 */
    if ((p_trpt->size < 64) || (p_trpt->size < (p_trpt->header_len+4)))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_MAX_VALUE_CHECK(p_trpt->size, 16128);

    if (0 == p_trpt->nhid)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_trpt->gport, lchip, lport);
        CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, p_trpt->gport, CTC_PORT_PROP_SPEED, &speed_mode));
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
                max_speed = 10000000;
                break;
        }

        /* kbps, 128k - speed of dest port */
        if ((max_speed) && ((p_trpt->rate < 128) || (p_trpt->rate > max_speed)))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if((p_trpt->rate == 0)||(p_trpt->size == 0))
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_MAX_VALUE_CHECK(p_trpt->tx_seq_en, 1);

    if ((p_trpt->tx_seq_en) &&
         ((p_trpt->seq_num_offset < 14) || (p_trpt->seq_num_offset > p_trpt->header_len)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_trpt->pattern_type >= CTC_OAM_PATTERN_MAX_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }


    if (p_trpt->tx_mode >= CTC_OAM_TX_TYPE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((CTC_OAM_TX_TYPE_PACKET == p_trpt->tx_mode) && ((0 == p_trpt->packet_num)))
    {
        return CTC_E_INVALID_PARAM;
    }


    if ((CTC_OAM_TX_TYPE_PERIOD == p_trpt->tx_mode) && ((0 == p_trpt->tx_period) || ( p_trpt->tx_period > 86400000))) /*24hours*/
    {
        return CTC_E_OAM_TRPT_PERIOD_INVALID;
    }


    return CTC_E_NONE;

}

STATIC int
_byte_reverse_copy(uint8* dest, uint8* src, uint32 len)
{
    uint32 i = 0;
    for (i = 0; i < len; i++)
    {
        *(dest + i) = *(src + (len - 1 - i));
    }
    return 0;
}


/**
@ brief config section packet header parameter
*/
int32
sys_goldengate_oam_trpt_set_bridge_hdr(uint8 lchip, ctc_oam_trpt_t* p_trpt,  MsPacketHeader_m* p_bridge_hdr)
{
    uint8  gchip = 0;
    uint16  lport = 0;
    uint32 nexthop_ptr = 0;
    int32 ret = CTC_E_NONE;
    uint8 bridge_hdr[SYS_GG_PKT_HEADER_LEN] = {0};
    uint8 i=0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_bridge_hdr);
    CTC_PTR_VALID_CHECK(p_trpt);
    CTC_BOTH_DIRECTION_CHECK(p_trpt->direction);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_trpt->gport, lchip, lport);
    gchip = SYS_DRV_GPORT_TO_GCHIP(p_trpt->gport);
    if (CTC_INGRESS == p_trpt->direction)
    {
        SetMsPacketHeader(V, destMap_f, p_bridge_hdr, (gchip << 12) | (lport&0x100) | SYS_RSV_PORT_ILOOP_ID);
        SetMsPacketHeader(V, nextHopPtr_f, p_bridge_hdr, (((lport >> 7)&0x1) << 16) | (lport&0xFF));
    }
    else
    {
        if (p_trpt->nhid)
        {
            sys_nh_info_dsnh_t nhinfo;
            sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
            CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_trpt->nhid, &nhinfo));
            if (nhinfo.is_mcast)
            {
                SetMsPacketHeader(V, destMap_f, p_bridge_hdr, SYS_ENCODE_MCAST_IPE_DESTMAP(0, nhinfo.dest_id));
            }
            else
            {
                SetMsPacketHeader(V, destMap_f, p_bridge_hdr, SYS_ENCODE_DESTMAP(nhinfo.dest_chipid, nhinfo.dest_id));
                SetMsPacketHeader(V, nextHopPtr_f, p_bridge_hdr, nhinfo.dsnh_offset);
                SetMsPacketHeader(V, nextHopExt_f , p_bridge_hdr, nhinfo.nexthop_ext);
            }
        }
        else
        {
            /* get bypass nexthop */
            ret = sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &nexthop_ptr);
            SetMsPacketHeader(V, destMap_f, p_bridge_hdr, (gchip << 12) | lport);
            SetMsPacketHeader(V, nextHopPtr_f, p_bridge_hdr, nexthop_ptr);
            SetMsPacketHeader(V, nextHopExt_f , p_bridge_hdr, 1);
        }
    }

    SetMsPacketHeader(V, srcVlanPtr_f, p_bridge_hdr, p_trpt->vlan_id);
    SetMsPacketHeader(V, sourcePort_f , p_bridge_hdr, SYS_RSV_PORT_OAM_CPU_ID);
    SetMsPacketHeader(V, srcVlanId_f, p_bridge_hdr, 1);
    SetMsPacketHeader(V, color_f , p_bridge_hdr, 3);
    SetMsPacketHeader(V, fromCpuOrOam_f, p_bridge_hdr, 1);
    SetMsPacketHeader(V, operationType_f , p_bridge_hdr, 7);

    for (i = 0; i < (SYS_GG_PKT_HEADER_LEN / 4); i++)
    {
        bridge_hdr[i*4 + 0] = ((uint8*)p_bridge_hdr)[i*4 + 3];
        bridge_hdr[i*4 + 1] = ((uint8*)p_bridge_hdr)[i*4 + 2];
        bridge_hdr[i*4 + 2] = ((uint8*)p_bridge_hdr)[i*4 + 1];
        bridge_hdr[i*4 + 3] = ((uint8*)p_bridge_hdr)[i*4 + 0];
    }
    sal_memcpy((uint8*)p_bridge_hdr, bridge_hdr,  SYS_GG_PKT_HEADER_LEN);
    for (i = 0; i < (SYS_GG_PKT_HEADER_LEN / 4); i++)
    {
        ((uint32 *)bridge_hdr)[i] = DRV_SWAP32(((uint32 *)p_bridge_hdr)[i]);
    }
    _byte_reverse_copy((uint8*)p_bridge_hdr, bridge_hdr, SYS_GG_PKT_HEADER_LEN);
    return ret;

}

/**
@ brief config section packet header parameter
*/
int32
sys_goldengate_oam_trpt_set_user_data(uint8 lchip, uint8 session_id, void* p_header, uint8 header_length)
{

    uint32 cmd = 0;
    uint32 up_ptr = 0;
    AutoGenPktPktHdr_m ds_pkt_hdr;
    uint32* p_hdr = NULL;
    uint8 index = 0;
    uint8 i = 0;

    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_header);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    up_ptr = ((session_id << 4) - (session_id << 2));

    SYS_OAM_DBG_INFO("PktEntry Ptr:%d \n", up_ptr);
    SYS_OAM_DBG_INFO("Packet data:%d \n", up_ptr);
    for (i = 0; i < header_length; i++)
    {
         /*SYS_OAM_DBG_INFO("0x%02x  ", ((uint8*)p_header)[i]);*/
    }

    sal_memset(&ds_pkt_hdr, 0, sizeof(AutoGenPktPktHdr_m));

    /* config one section */
    p_hdr = (uint32*)p_header;
    for (index = 0; index <= header_length/8; index++)
    {
        SetAutoGenPktPktHdr(V, pktHdrUserdata0_f, &ds_pkt_hdr, DRV_SWAP32(p_hdr[index*2]));
        SetAutoGenPktPktHdr(V, pktHdrUserdata1_f, &ds_pkt_hdr, DRV_SWAP32(p_hdr[index*2+1]));
        cmd = DRV_IOW(AutoGenPktPktHdr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (up_ptr+index), cmd, &ds_pkt_hdr));
    }

    return CTC_E_NONE;
}


/**
@ brief gen autogen section enable
*/
bool
_sys_goldengate_oam_trpt_get_enable(uint8 lchip, uint8 session_id)
{
    if (p_gg_trpt_master[lchip]->session_info[session_id].enable)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

STATIC int32
_sys_goldengate_oam_trpt_cal_rate(uint8 lchip, uint8 session_id, uint16 size, uint32 rate)
{
    uint64 core_freq = 0;
    uint8 ipg = 0;
    uint8 session_num = 1;
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
    core_freq = sys_goldengate_get_core_freq(lchip, 0);

    /* get ipg */
    CTC_ERROR_RETURN(sys_goldengate_get_ipg_size(lchip, 0, &ipg));

    p_gg_trpt_master[lchip]->session_info[session_id].rate= rate;
    temp = 8*(size+32)/ SYS_OAM_TRPT_CYCLE_MUL+ 3;
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
                p_gg_trpt_master[lchip]->session_info[session_id].act_cycle = (uint16)interval;
                p_gg_trpt_master[lchip]->session_info[session_id].ticks_cnt = (uint16)temp;
                p_gg_trpt_master[lchip]->session_info[session_id].ticks_frace = (uint16)temp1;
                rate_remainder = new_rate_remainder;
                success = 1;
            }
        }
    }
    if (0 == success)
    {
       return  CTC_E_INVALID_PARAM;
    }

    SYS_OAM_DBG_INFO("cal rate result:\nact_cycle: %u ticks_cnt: %u ticks_frace: %u\n",
        p_gg_trpt_master[lchip]->session_info[session_id].act_cycle,
        p_gg_trpt_master[lchip]->session_info[session_id].ticks_cnt,
        p_gg_trpt_master[lchip]->session_info[session_id].ticks_frace );


   return CTC_E_NONE;
}

/**
@ brief config section packet header parameter
*/
int32
sys_goldengate_oam_trpt_set_cfg(uint8 lchip, uint8 session_id, ctc_oam_trpt_t* p_cfg)
{

    AutoGenPktPktCfg_m autogen_pkt_cfg;
    AutoGenPktTxPktStats_m tx_stats;
    uint32 cmd = 0;
    uint8 tx_mode = 0;
    uint8 is_use_period = 0;
    uint64 period_cnt = 0;
    uint32 core_freq = 0;
    uint64 temp = 0;
    uint32 tmp[2] = {0};

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_cfg);

    sal_memset(&autogen_pkt_cfg, 0, sizeof(AutoGenPktPktCfg_m));
    sal_memset(&tx_stats, 0, sizeof(AutoGenPktTxPktStats_m));
    cmd = DRV_IOR(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_stats));

    /* get core frequency */
    core_freq = sys_goldengate_get_core_freq(lchip, 0);

    /* cal rate */
    CTC_ERROR_RETURN(_sys_goldengate_oam_trpt_cal_rate(lchip, session_id, p_cfg->size, p_cfg->rate));

    switch(p_cfg->tx_mode)
    {
    case CTC_OAM_TX_TYPE_CONTINUOUS:
        tx_mode = 1;
        is_use_period = 0;
        break;
    case CTC_OAM_TX_TYPE_PACKET:
        tx_mode = 0;
        is_use_period = 0;
        break;
    case CTC_OAM_TX_TYPE_PERIOD:
        tx_mode = 1;
        is_use_period = 1;

        if ((p_gg_trpt_master[lchip]->session_info[session_id].act_cycle != 0)
            && (p_gg_trpt_master[lchip]->session_info[session_id].ticks_cnt != 0))
        {
            temp = core_freq;
            temp = temp*65535*1000000;
            temp = temp / (p_gg_trpt_master[lchip]->session_info[session_id].act_cycle+1) / p_gg_trpt_master[lchip]->session_info[session_id].ticks_cnt;
            temp = temp / (65535 + p_gg_trpt_master[lchip]->session_info[session_id].ticks_frace);
            period_cnt = temp*p_cfg->tx_period;
            if ((period_cnt < temp)||(period_cnt < p_cfg->tx_period))
            {
                return CTC_E_INVALID_PARAM;
            }

            SYS_OAM_DBG_INFO("tx_period:%ds\tperiod_cnt in asic:%"PRIu64"\n", p_cfg->tx_period, period_cnt);
            if (period_cnt > SYS_OAM_TRPT_PERIOD_MAX)
            {
                return CTC_E_INVALID_PARAM;
            }
        }

        break;
    default:
        break;
    }

    SetAutoGenPktPktCfg(V, txMode_f, &autogen_pkt_cfg,  tx_mode);
    SetAutoGenPktPktCfg(V, txPktCnt_f, &autogen_pkt_cfg,  p_cfg->packet_num);
    SetAutoGenPktPktCfg(V, isUsePeriod_f, &autogen_pkt_cfg,  is_use_period);
    tmp[1] = period_cnt>>32;
    tmp[0] = period_cnt&0xFFFFFFFF;
    SetAutoGenPktPktCfg(A, periodCnt_f, &autogen_pkt_cfg,  tmp);
    SetAutoGenPktPktCfg(V, seqNumOffset_f, &autogen_pkt_cfg, p_cfg->seq_num_offset + SYS_GG_PKT_HEADER_LEN);
    SetAutoGenPktPktCfg(V, txSeqNumEn_f , &autogen_pkt_cfg,  p_cfg->tx_seq_en);
    SetAutoGenPktPktCfg(V, isHaveEndTlv_f, &autogen_pkt_cfg,  1);
    SetAutoGenPktPktCfg(V, pktHdrLen_f, &autogen_pkt_cfg,  p_cfg->header_len + SYS_GG_PKT_HEADER_LEN);
    SetAutoGenPktPktCfg(V, patternType_f, &autogen_pkt_cfg,  p_cfg->pattern_type);
    SetAutoGenPktPktCfg(V, repeatPattern_f, &autogen_pkt_cfg,  p_cfg->repeat_pattern);
    SetAutoGenPktPktCfg(V, txPktSize_f, &autogen_pkt_cfg,  p_cfg->size);
    SetAutoGenPktPktCfg(V, rateCntCfg_f, &autogen_pkt_cfg,  p_gg_trpt_master[lchip]->session_info[session_id].ticks_cnt);
    SetAutoGenPktPktCfg(V, rateFracCntCfg_f, &autogen_pkt_cfg, p_gg_trpt_master[lchip]->session_info[session_id].ticks_frace);

    if (p_cfg->is_slm)
    {
        if (p_cfg->first_pkt_clear_en)
        {
            SetAutoGenPktTxPktStats(V, firstPktTrigger_f, &tx_stats,  1);
        }
    }

    /*cfg AutoGenPktPktCfg */
    cmd = DRV_IOW(AutoGenPktPktCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &autogen_pkt_cfg));
    cmd = DRV_IOW(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_stats));

    return CTC_E_NONE;

}

/**
@ brief config autogem section enable
*/
int32
sys_goldengate_oam_trpt_set_enable(uint8 lchip, uint8 session_id, uint8 enable)
{

    uint32 cmd = 0;
    AutoGenPktCtl_m pkt_ctl;
    /*uint8 max_session = 0;*/
    AutoGenPktPktCfg_m autogen_pkt_cfg;
    AutoGenEnCtl0_m  auto_gen_en_ctl;
    uint32 pkt_hdr_len = 0;
    uint32 auto_gen_en = 0;
    uint32 tick_gen_en = 0;
    uint32 tick_gen_interval = 0;
    uint8 i = 0;
    uint32 total_rate = 0;
    uint32 tx_mode = 0;
    uint32 tx_pkt_count = 0;
    uint32 is_use_period = 0;
    uint32 tmp[2] = {0};


    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    sal_memset(&pkt_ctl, 0, sizeof(pkt_ctl));
    sal_memset(&autogen_pkt_cfg, 0, sizeof(autogen_pkt_cfg));
    sal_memset(&auto_gen_en_ctl, 0, sizeof(auto_gen_en_ctl));

    cmd = DRV_IOR(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    if (enable)
    {
        if (p_gg_trpt_master[lchip]->session_info[session_id].enable)
        {
            return CTC_E_OAM_TRPT_SESSION_EN;
        }

        cmd = DRV_IOR(AutoGenPktPktCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &autogen_pkt_cfg));

        GetAutoGenPktPktCfg(A, pktHdrLen_f, &autogen_pkt_cfg, &pkt_hdr_len);
        if (0 == pkt_hdr_len)
        {
            return CTC_E_OAM_TRPT_SESSION_NOT_CFG;
        }

        for (i = 0; i < TRPT_SESSION_NUM_USED; i++)
        {
            if (p_gg_trpt_master[lchip]->session_info[i].enable)
            {
                total_rate += p_gg_trpt_master[lchip]->session_info[i].rate;
            }
        }
        total_rate += p_gg_trpt_master[lchip]->session_info[session_id].rate;

        if (total_rate > TRPT_TOTAL_RATE)
        {
            return CTC_E_OAM_TRPT_INTERVAL_CONFLICT;
        }

        auto_gen_en = 1;
        tick_gen_en = 1;
        tick_gen_interval =  p_gg_trpt_master[lchip]->session_info[session_id].act_cycle;

        SetAutoGenEnCtl0(A, autoGenEn0_f, &auto_gen_en_ctl, &auto_gen_en);
        switch(session_id)
        {
        case 0:
            SetAutoGenPktCtl(A, tickGenEn0_f, &pkt_ctl, &tick_gen_en);
            SetAutoGenPktCtl(A, tickGenInterval0_f, &pkt_ctl, &tick_gen_interval);
            break;
        case 1:
            SetAutoGenPktCtl(A, tickGenEn1_f, &pkt_ctl, &tick_gen_en);
            SetAutoGenPktCtl(A, tickGenInterval1_f, &pkt_ctl, &tick_gen_interval);
            break;
        case 2:
            SetAutoGenPktCtl(A, tickGenEn2_f, &pkt_ctl, &tick_gen_en);
            SetAutoGenPktCtl(A, tickGenInterval2_f, &pkt_ctl, &tick_gen_interval);
            break;
        case 3:
            SetAutoGenPktCtl(A, tickGenEn3_f, &pkt_ctl, &tick_gen_en);
            SetAutoGenPktCtl(A, tickGenInterval3_f, &pkt_ctl, &tick_gen_interval);
            break;
        default:
            break;
        }
        p_gg_trpt_master[lchip]->session_info[session_id].enable = 1;

        GetAutoGenPktPktCfg(A, txMode_f, &autogen_pkt_cfg, &tx_mode);
        GetAutoGenPktPktCfg(A, txPktCnt_f, &autogen_pkt_cfg, &tx_pkt_count);
        GetAutoGenPktPktCfg(A, isUsePeriod_f, &autogen_pkt_cfg, &is_use_period);
        GetAutoGenPktPktCfg(A, periodCnt_f, &autogen_pkt_cfg, &tmp);


        if ((0 == tx_mode) && (0 == is_use_period )) /*CTC_OAM_TX_TYPE_PACKET*/
        {
            if (0  == tx_pkt_count)
            {
                tx_mode = 1;
                cmd = DRV_IOW(AutoGenPktPktCfg_t, AutoGenPktPktCfg_txMode_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_mode));
            }
        }
        else if ((1 == tx_mode) && (1 == is_use_period ))/*CTC_OAM_TX_TYPE_PERIOD*/
        {
            if ((0  == tmp[0]) && (0 == tmp[1]))
            {
                is_use_period = 0;
                cmd = DRV_IOW(AutoGenPktPktCfg_t, AutoGenPktPktCfg_isUsePeriod_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &is_use_period));
            }
        }

        if ((0 == tx_mode) && (0  == tx_pkt_count))
        {
            tx_mode = 1;
            cmd = DRV_IOW(AutoGenPktPktCfg_t, AutoGenPktPktCfg_txMode_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_mode));
        }

    }
    else
    {
        auto_gen_en = 0;
        SetAutoGenEnCtl0(A, autoGenEn0_f, &auto_gen_en_ctl, &auto_gen_en);
        p_gg_trpt_master[lchip]->session_info[session_id].enable = 0;
    }

    cmd = DRV_IOW(AutoGenEnCtl0_t + session_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_gen_en_ctl));

    cmd = DRV_IOW(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pkt_ctl));

    return CTC_E_NONE;

}

int32
sys_goldengate_oam_trpt_set_autogen_ptr(uint8 lchip, uint8 session_id, uint16 lmep_idx)
{

    SYS_OAM_TRPT_INIT_CHECK(lchip);

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    p_gg_trpt_master[lchip]->session_info[session_id].lmep_index = lmep_idx;

    return CTC_E_NONE;
}

int32
sys_goldengate_oam_trpt_get_autogen_ptr(uint8 lchip, uint16 mep_id, uint8* p_ptr)
{

    uint8 index = 0;
    uint8 find_flag = 0;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ptr);

    for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
    {
        if (mep_id ==  p_gg_trpt_master[lchip]->session_info[index].lmep_index)
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
sys_goldengate_oam_trpt_get_stats_hw(uint8 lchip, uint8 session_id, ctc_oam_trpt_stats_t* p_stat_info)
{
    uint32 cmd = 0;
    AutoGenPktTxPktStats_m tx_stats;
    AutoGenPktRxPktStats_m rx_stats;
    ctc_oam_trpt_stats_t* p_old_stats;
    uint32 tmp[2] = {0};

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stat_info);

    sal_memset(&tx_stats, 0, sizeof(AutoGenPktTxPktStats_m));
    sal_memset(&rx_stats, 0, sizeof(AutoGenPktRxPktStats_m));

    OAM_TRPT_LOCK(lchip);
    /* get stats from AutoGenPktTxPktStats and AutoGenPktRxPktStats */
    cmd = DRV_IOR(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, session_id, cmd, &tx_stats), p_gg_trpt_master[lchip]->p_trpt_mutex);
    p_stat_info->tx_pkt = GetAutoGenPktTxPktStats(V, txPkts_f, &tx_stats);
    GetAutoGenPktTxPktStats(A, txOctets_f , &tx_stats, &tmp);
    p_stat_info->tx_oct = ((uint64)tmp[1]<<32) + tmp[0];
    p_stat_info->tx_fcf = GetAutoGenPktTxPktStats(V, seqNum_f , &tx_stats);

    cmd = DRV_IOR(AutoGenPktRxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, session_id, cmd, &rx_stats), p_gg_trpt_master[lchip]->p_trpt_mutex);
    p_stat_info->rx_pkt = GetAutoGenPktRxPktStats(V, rxPkts_f, &rx_stats);
    tmp[0] = 0;
    tmp[1] = 0;
    GetAutoGenPktRxPktStats(A, rxOctets_f, &rx_stats, &tmp);
    p_stat_info->rx_oct = ((uint64)tmp[1]<<32) + tmp[0];
    p_stat_info->tx_fcb = GetAutoGenPktRxPktStats(V, lastSeqNo_f, &rx_stats);
    p_stat_info->rx_fcl = GetAutoGenPktRxPktStats(V, rxRcl_f, &rx_stats);

    p_old_stats = &(p_gg_trpt_master[lchip]->session_stats[session_id]);

    if (p_stat_info->rx_oct < p_old_stats->rx_oct)
    {
        p_gg_trpt_master[lchip]->overflow_rec[session_id].rx_oct_overflow_cnt++;
    }

    if (p_stat_info->rx_pkt < p_old_stats->rx_pkt)
    {
        p_gg_trpt_master[lchip]->overflow_rec[session_id].rx_pkt_overflow_cnt++;
    }

    if (p_stat_info->tx_oct < p_old_stats->tx_oct)
    {
        p_gg_trpt_master[lchip]->overflow_rec[session_id].tx_oct_overflow_cnt++;
    }

    if (p_stat_info->tx_pkt < p_old_stats->tx_pkt)
    {
        p_gg_trpt_master[lchip]->overflow_rec[session_id].tx_pkt_overflow_cnt++;
    }
    sal_memcpy(p_old_stats, p_stat_info, sizeof(ctc_oam_trpt_stats_t));

    OAM_TRPT_UNLOCK(lchip);

    return CTC_E_NONE;

}
/**
 @brief oam throughput get stats information
*/
int32
sys_goldengate_oam_trpt_get_stats(uint8 lchip, uint8 session_id, ctc_oam_trpt_stats_t* p_stat_info)
{

    ctc_oam_trpt_stats_t hw_stats;
    sys_oam_trpt_overflow_rec_t* overflow_rec = NULL;
    uint64 full_oct_stats = 0;
    uint64 full_pkt_stats = 0;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stat_info);

    sal_memset(&hw_stats, 0, sizeof(ctc_oam_trpt_stats_t));

    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_get_stats_hw(lchip, session_id, &hw_stats));

    SYS_OAM_DBG_INFO("get stats Hw, rx_oct:0x%"PRIx64" rx_pkt:0x%"PRIx64" tx_oct:0x%"PRIx64"  tx_pkt:0x%"PRIx64"  tx_fcf:0x%"PRIx64"   tx_fcb:0x%"PRIx64"   rx_fcl:0x%"PRIx64"\n",
                     hw_stats.rx_oct,  hw_stats.rx_pkt, hw_stats.tx_oct, hw_stats.tx_pkt, hw_stats.tx_fcf, hw_stats.tx_fcb, hw_stats.rx_fcl);

    full_oct_stats = 0x3f;
    full_oct_stats <<= 32;
    full_oct_stats  |=  0xffffffff;
    full_pkt_stats = 0xffffffff;
    overflow_rec = &(p_gg_trpt_master[lchip]->overflow_rec[session_id]);

    SYS_OAM_DBG_INFO("Over Flow Info, rx_oct:0x%x rx_pkt:0x%x tx_oct:0x%x tx_pkt:0x%x\n", overflow_rec->rx_oct_overflow_cnt,
                     overflow_rec->rx_pkt_overflow_cnt, overflow_rec->tx_oct_overflow_cnt, overflow_rec->tx_pkt_overflow_cnt);

    p_stat_info->rx_oct = hw_stats.rx_oct + (full_oct_stats + 1)*(overflow_rec->rx_oct_overflow_cnt);
    p_stat_info->rx_pkt = hw_stats.rx_pkt + (full_pkt_stats + 1)*(overflow_rec->rx_pkt_overflow_cnt);
    p_stat_info->tx_oct = hw_stats.tx_oct + (full_oct_stats + 1)*(overflow_rec->tx_oct_overflow_cnt);
    p_stat_info->tx_pkt = hw_stats.tx_pkt + (full_pkt_stats + 1)*(overflow_rec->tx_pkt_overflow_cnt);
    p_stat_info->tx_fcf = hw_stats.tx_fcf;
    p_stat_info->tx_fcb = hw_stats.tx_fcb;
    p_stat_info->rx_fcl = hw_stats.rx_fcl;


    return CTC_E_NONE;

}

int32
sys_goldengate_oam_trpt_clear_stats(uint8 lchip, uint8 session_id)
{

    uint32 cmd = 0;
    AutoGenPktTxPktStats_m tx_stats;
    AutoGenPktRxPktStats_m rx_stats;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_TRPT_INIT_CHECK(lchip);

    sal_memset(&tx_stats, 0, sizeof(AutoGenPktTxPktStats_m));
    sal_memset(&rx_stats, 0, sizeof(AutoGenPktRxPktStats_m));


    /* write AutoGenPktTxPktStats and AutoGenPktRxPktStats */
    cmd = DRV_IOW(AutoGenPktTxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &tx_stats));

    cmd = DRV_IOW(AutoGenPktRxPktStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, session_id, cmd, &rx_stats));

    sal_memset((void*)&(p_gg_trpt_master[lchip]->session_stats[session_id]), 0, sizeof(ctc_oam_trpt_stats_t));
    sal_memset((void*)&(p_gg_trpt_master[lchip]->overflow_rec[session_id]), 0, sizeof(sys_oam_trpt_overflow_rec_t));

    return CTC_E_NONE;

}

void
_sys_goldengate_oam_trpt_stats_thread(void* user_param)
{

    ctc_oam_trpt_stats_t trpt_stats;
    uint8 lchip = (uintptr)user_param;
    uint8 index = 0;
    int32 ret = 0;
    uint16 lmep_index;                    /* lmep index */
    uint32 cmd = 0;
    uint32 auto_gen_en = 0;

    sal_memset(&trpt_stats, 0, sizeof(ctc_oam_trpt_stats_t));

    while (1)
    {
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
        for (index = 0; index < CTC_OAM_TRPT_SESSION_NUM; index++)
        {
            lmep_index = p_gg_trpt_master[lchip]->session_info[index].lmep_index;

            cmd = DRV_IOR(DsEthMep_t, DsEthMep_autoGenEn_f);
            DRV_IOCTL(lchip, lmep_index, cmd, &auto_gen_en);

            if (auto_gen_en || _sys_goldengate_oam_trpt_get_enable(lchip, index))
            {
                ret = sys_goldengate_oam_trpt_get_stats_hw(lchip, index, &trpt_stats);
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

/**
 @brief oam throughput init
*/
int32
sys_goldengate_oam_trpt_init(uint8 lchip)
{

    int32 ret = 0;
    uint8 session_id = 0;
    uint8 index = 0;
    uint32 cmd = 0;
    uintptr lchip_tmp = lchip;
    AutoGenPktPktCfg_m autogen_pkt_cfg;
    AutoGenPktCtl_m autogen_cfg;
    AutoGenPktPktCfg_m autogen_hdr;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    if (p_gg_trpt_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    if(NULL == g_gg_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_trpt_master[lchip] = (sys_oam_trpt_master_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_trpt_master_t)*CTC_OAM_TRPT_SESSION_NUM);
    if (NULL == p_gg_trpt_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_trpt_master[lchip], 0, sizeof(sys_oam_trpt_master_t));
    sal_sprintf(buffer, "TrptStats-%d", lchip);
    ret = sal_task_create(&p_gg_trpt_master[lchip]->trpt_stats_thread, buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_goldengate_oam_trpt_stats_thread, (void*)lchip_tmp);
    if (ret < 0)
    {
        return CTC_E_NOT_INIT;
    }

    sal_mutex_create(&(p_gg_trpt_master[lchip]->p_trpt_mutex));
    if (NULL == p_gg_trpt_master[lchip]->p_trpt_mutex)
    {
        mem_free(p_gg_trpt_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }


    sal_memset(&autogen_pkt_cfg, 0, sizeof(AutoGenPktPktCfg_m));
    sal_memset(&autogen_cfg, 0, sizeof(AutoGenPktCtl_m));
    sal_memset(&autogen_hdr, 0, sizeof(AutoGenPktPktCfg_m));

    /* clear trpt cfg */
    cmd = DRV_IOW(AutoGenPktCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &autogen_cfg));

    for (session_id = 0; session_id < CTC_OAM_TRPT_SESSION_NUM; session_id++)
    {
        sys_goldengate_oam_trpt_clear_stats(lchip, session_id);

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
sys_goldengate_oam_trpt_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_trpt_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_task_destroy(p_gg_trpt_master[lchip]->trpt_stats_thread);

    sal_mutex_destroy(p_gg_trpt_master[lchip]->p_trpt_mutex);
    mem_free(p_gg_trpt_master[lchip]);

    return CTC_E_NONE;
}

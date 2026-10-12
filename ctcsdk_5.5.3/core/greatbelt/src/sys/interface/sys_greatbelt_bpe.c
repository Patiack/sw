/**
 @file sys_greatbelt_port.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-22

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_nexthop.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_bpe.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_port.h"

#include "greatbelt/include/drv_data_path.h"
#include "greatbelt/include/drv_chip_info.h"

#define SYS_BPE_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_bpe_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

static sys_bpe_master_t* p_gb_bpe_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define __BPE__INTERNAL__

STATIC int32
_sys_greatbelt_bpe_set_cross_connect(uint8 lchip, uint8 enable, uint8 lport, uint32 fwd_ptr)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortCrossConnect_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = fwd_ptr;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DsFwdPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_bpe_intlk_fwd(uint8 lchip, uint8 intlk_mode, uint8 enable)
{
    uint8 index = 0;
    int32 ret = 0;
    uint32 fwd_offset = 0;
    uint8 local_port_num = 0;
    uint8 local_phy_port_base = 0;
    ctc_internal_port_assign_para_t  inter_port_para;
    uint8 gchip = 0;

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "interlaken mode: 0x%x\n", intlk_mode);

    local_port_num = intlk_mode?(SYS_INTLK_MAX_LOCAL_PORT_SEGMENT):(SYS_INTLK_MAX_LOCAL_PORT_PKT);
    local_phy_port_base = intlk_mode?(SYS_INTLK_PORT_BASE_SEGMENT):(SYS_INTLK_PORT_BASE_PKT);


    sal_memset(&inter_port_para, 0, sizeof(ctc_internal_port_assign_para_t));
    sys_greatbelt_get_gchip_id(lchip, &gchip);

    /* config forwarding from local to interlaken */
    for (index = 0; index < local_port_num; index++)
    {
        if (enable)
        {
            ret = sys_greatbelt_brguc_get_dsfwd_offset(lchip, index+local_phy_port_base, CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS, &fwd_offset);
            if (ret)
            {
                SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get DsFwd Error! 0x%x\n", ret);
                return ret;
            }
        }

       CTC_ERROR_RETURN(_sys_greatbelt_bpe_set_cross_connect(lchip, enable, index, fwd_offset));
    }

    /* config forwarding from interlaken to local */
    for (index = local_phy_port_base; index < local_phy_port_base+local_port_num; index++)
    {
        if (enable)
        {
            ret = sys_greatbelt_brguc_get_dsfwd_offset(lchip, (index-local_phy_port_base), CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS, &fwd_offset);
            if (ret)
            {
                SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get DsFwd Error! 0x%x\n", ret);
                return ret;
            }
        }

       CTC_ERROR_RETURN(_sys_greatbelt_bpe_set_cross_connect(lchip, enable, index, fwd_offset));
    }

    /* for pkt mode banding internal port to channel 56 */
    if (intlk_mode == CTC_BPE_INTLK_PKT_MODE)
    {
        for (index = SYS_INTLK_INTERNAL_PORT_BASE; index < SYS_INTLK_INTERNAL_PORT_BASE+local_port_num; index++)
        {
           inter_port_para.gchip = gchip;
           inter_port_para.fwd_gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_INTLK);
           inter_port_para.inter_port = index;
           inter_port_para.type = CTC_INTERNAL_PORT_TYPE_FWD;
           CTC_ERROR_RETURN(sys_greatbelt_internal_port_set(lchip, &inter_port_para));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_bpe_set_mux_demux_en(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender)
{
    uint8 gchip = 0;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 index = 0;
    uint8 chan_id = 0;
    ctc_internal_port_assign_para_t  inter_port_para;
    ipe_header_adjust_phy_port_map_t ipe_map;
    epe_header_edit_phy_port_map_t epe_map;
    epe_hdr_proc_phy_port_map_t epe_hdr_proc_phy_port_map;
    bool logic_port = FALSE;

    sal_memset(&ipe_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));
    sal_memset(&epe_map, 0, sizeof(epe_header_edit_phy_port_map_t));
    sal_memset(&epe_hdr_proc_phy_port_map, 0, sizeof(epe_hdr_proc_phy_port_map_t));

    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*get channel from port */
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sys_greatbelt_get_logic_port_check_en(lchip, &logic_port);

    if (p_extender->enable)
    {
        /* cfg vlan base */
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ipe_map));

        cmd = DRV_IOR(EpeHeaderEditPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_map));

        cmd = DRV_IOR(EpeHdrProcPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_hdr_proc_phy_port_map));


        if (p_extender->vlan_base >= p_extender->port_base)
        {
            ipe_map.symbol = 1;
            ipe_map.port_vlan_base = p_extender->vlan_base - p_extender->port_base;

            epe_map.symbol = 0;
            epe_map.port_vlan_base = p_extender->vlan_base - p_extender->port_base;

            epe_hdr_proc_phy_port_map.symbol = 0;
            epe_hdr_proc_phy_port_map.port_vlan_base = p_extender->vlan_base - p_extender->port_base;
        }
        else
        {
            ipe_map.symbol = 0;
            ipe_map.port_vlan_base = p_extender->port_base - p_extender->vlan_base;

            epe_map.symbol = 1;
            epe_map.port_vlan_base = p_extender->port_base - p_extender->vlan_base;

            epe_hdr_proc_phy_port_map.symbol = 1;
            epe_hdr_proc_phy_port_map.port_vlan_base = p_extender->port_base - p_extender->vlan_base;
        }
        cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ipe_map));

        cmd = DRV_IOW(EpeHeaderEditPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_map));

        cmd = DRV_IOW(EpeHdrProcPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_hdr_proc_phy_port_map));

        field_val =  0x01;
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_MuxType0_f+chan_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        for (index = 0; index < p_extender->port_num; index++)
        {
            /* set internal port */
           inter_port_para.gchip = gchip;
           inter_port_para.fwd_gport = gport;
           inter_port_para.inter_port = p_extender->port_base + index;
           inter_port_para.type = CTC_INTERNAL_PORT_TYPE_FWD;
           CTC_ERROR_RETURN(sys_greatbelt_internal_port_set(lchip, &inter_port_para));

            /* cfg dest port */
            field_val =  0x02;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg src port disable stp */
            field_val = 1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg src port disable add default vlan */
            field_val = 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg dest port disable stp */
            field_val = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg src port enable learning */
            field_val = 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LearningDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            if(logic_port)
            {
                field_val = 1;
                cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultLogicSrcPort_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));
            }

        }
    }
    else
    {
        /* release port extender */
        field_val =  0x00;
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_MuxType0_f+chan_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        for (index = 0; index < p_extender->port_num; index++)
        {
            /* cfg dest port */
            field_val =  0x00;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg src port disable add default vlan */
            field_val = 1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            /* cfg src port disable learning */
            field_val = 1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LearningDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));

            if(logic_port)
            {
                field_val = 0;
                cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultLogicSrcPort_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_extender->port_base + index), cmd, &field_val));
            }

            /* release internal port */
           inter_port_para.gchip = gchip;
           inter_port_para.fwd_gport = gport;
           inter_port_para.inter_port = p_extender->port_base + index;
           inter_port_para.type = CTC_INTERNAL_PORT_TYPE_FWD;
           sys_greatbelt_internal_port_release(lchip, &inter_port_para);
        }
    }
    return CTC_E_NONE;
}

#define __BPE_INTERFACE__
int32
sys_greatbelt_bpe_init(uint8 lchip, void* bpe_global_cfg)
{
    ctc_bpe_global_cfg_t* p_bpe_global_cfg = NULL;

    if (NULL != p_gb_bpe_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_bpe_global_cfg = (ctc_bpe_global_cfg_t*)bpe_global_cfg;

    /*init soft table*/
    p_gb_bpe_master[lchip] =
        (sys_bpe_master_t*)mem_malloc(MEM_PORT_MODULE, sizeof(sys_bpe_master_t));
    if (NULL == p_gb_bpe_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_bpe_master[lchip], 0, sizeof(sys_bpe_master_t));


    p_gb_bpe_master[lchip]->intlk_mode = p_bpe_global_cfg->intlk_mode;

    return CTC_E_NONE;
}

int32
sys_greatbelt_bpe_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_bpe_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gb_bpe_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_bpe_set_intlk_en(uint8 lchip, bool enable)
{
    uint8 gchip = 0;
    uint8 chan_id = 0;
    uint16 gport = 0;
    uint8 intkk_en = enable?1:0;
    int32 ret = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    net_tx_chan_id_map_t ds_nettx_chan;
    net_tx_port_id_map_t ds_nettx_port;
    parser_ethernet_ctl_t parser_ctl;
    epe_hdr_adjust_ctl_t hdr_adj_ctl;
    int_lk_soft_reset_t intlk_reset;

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "set interlaken enable 0x%x\n", enable);

    SYS_BPE_INIT_CHECK(lchip);

    if (intkk_en == p_gb_bpe_master[lchip]->intlk_en)
    {
        return CTC_E_NONE;
    }

    sal_memset(&intlk_reset, 0, sizeof(int_lk_soft_reset_t));


    if ((!drv_greatbelt_get_intlk_support(lchip)))
    {
        SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "not support interlaken!\n");
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_INTLK);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == chan_id)
    {
        SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "not support interlaken!\n");
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* interlaken mode, not edit vlan , all packets need treated as untag */
    cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_ctl));
    parser_ctl.vlan_parsing_num = intkk_en?0:2;
    cmd = DRV_IOW(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_ctl));

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_adj_ctl));
    hdr_adj_ctl.vlan_edit_en = intkk_en?0:1;
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_adj_ctl));

    /* select calendar depend on intlk mode */
    cmd = DRV_IOW(NetTxCalCtl_t, NetTxCalCtl_CalEntrySel_f);
    field_val =  (p_gb_bpe_master[lchip]->intlk_mode)?1:0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* cfg channel map for interlaken */
    cmd = DRV_IOW(NetTxChanIdMap_t, DRV_ENTRY_FLAG);
    ds_nettx_chan.port_id = INTERLAKEN_NETTX_PORT_ID;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_INTLK_CHANNEL_ID, cmd, &ds_nettx_chan));

    /* cfg NetTxPortIdMap */
    cmd = DRV_IOW(NetTxPortIdMap_t, DRV_ENTRY_FLAG);
    ds_nettx_port.chan_id = SYS_INTLK_CHANNEL_ID;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTERLAKEN_NETTX_PORT_ID, cmd, &ds_nettx_port));

    ret = sys_greatbelt_chip_intlk_register_init(lchip, p_gb_bpe_master[lchip]->intlk_mode);
    if (ret)
    {
        SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "InterLaken register init fail! 0x%x\n", ret);
        return ret;
    }

    /*release align reset*/
    cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
    intlk_reset.soft_reset_rx_align = 0;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    /*release rx reset*/
    intlk_reset.soft_reset_lrx = 0;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    ret = _sys_greatbelt_bpe_intlk_fwd(lchip, p_gb_bpe_master[lchip]->intlk_mode, intkk_en);
    if (ret)
    {
        SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "InterLaken cfg fwd failed! 0x%x\n", ret);
        return ret;
    }


    p_gb_bpe_master[lchip]->intlk_en = enable?1:0;

    return CTC_E_NONE;
}

int32
sys_greatbelt_bpe_get_intlk_en(uint8 lchip, bool* enable)
{
    SYS_BPE_INIT_CHECK(lchip);

    if (p_gb_bpe_master[lchip]->intlk_en)
    {
        *enable = TRUE;
    }
    else
    {
        *enable = FALSE;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_bpe_set_port_extender(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender)
{

    uint8 lport = 0;

    SYS_BPE_INIT_CHECK(lchip);

    if (NULL == p_extender)
    {
        return CTC_E_INVALID_PTR;
    }

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "Set port:%d vlan_base %d num:%d internal_port:%d!\n",
        gport, p_extender->vlan_base, p_extender->port_num, p_extender->port_base);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_extender->vlan_base < CTC_MIN_VLAN_ID || (p_extender->vlan_base+p_extender->port_num) > CTC_MAX_VLAN_ID)
    {
        return CTC_E_VLAN_INVALID_VLAN_ID;
    }

    CTC_VALUE_RANGE_CHECK(p_extender->port_base, SYS_INTERNAL_PORT_START,
                    (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1));
    CTC_VALUE_RANGE_CHECK((p_extender->port_base + p_extender->port_num - 1),
                    SYS_INTERNAL_PORT_START, (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1));

    if (p_extender->enable == p_gb_bpe_master[lchip]->extender_en[lport])
    {
        return CTC_E_NONE;
    }

    switch(p_extender->mode)
    {
        case CTC_BPE_MUX_DEMUX_MODE:
            CTC_ERROR_RETURN(_sys_greatbelt_bpe_set_mux_demux_en(lchip, gport, p_extender));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    p_gb_bpe_master[lchip]->extender_en[lport] = p_extender->enable?1:0;
    p_gb_bpe_master[lchip]->extender_mode[lport] = p_extender->mode;

    return CTC_E_NONE;
}

int32
sys_greatbelt_bpe_get_port_extender(uint8 lchip, uint16 gport, bool* enable)
{
    uint8 lport = 0;


    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_BPE_INIT_CHECK(lchip);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (p_gb_bpe_master[lchip]->extender_en[lport])
    {
        *enable = TRUE;
    }
    else
    {
        *enable = FALSE;
    }

    return CTC_E_NONE;
}



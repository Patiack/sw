/**
 @file sys_goldengate_port.c

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

#include "sys_goldengate_common.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_bpe.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_linkagg.h"
#include "sys_goldengate_nexthop.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

static sys_bpe_master_t* p_gg_bpe_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define __BPE__INTERNAL__

#define MIN_ECID_INIT_VAL 0xFFF
#define MAX_ECID_INIT_VAL 0
#define SYS_BPE_FULL_EXTEND_MODE(lchip) (2 == p_gg_bpe_master[lchip]->enq_mode)

#define SYS_BPE_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_bpe_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

int32
sys_goldengate_bpe_use_logic_port_en(uint8 lchip, uint8 enable)
{
    if (p_gg_bpe_master[lchip])
    {
        p_gg_bpe_master[lchip]->use_logic_port = enable;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_update_phyport(uint8 lchip, uint16 lport, uint8 chan_id, bool is_add)
{
    if (SYS_BPE_FULL_EXTEND_MODE(lchip))
    {
        if (is_add)
        {
            CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), chan_id));
            CTC_ERROR_RETURN(sys_goldengate_add_port_range_to_channel(lchip, lport, lport, chan_id));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_remove_port_range_to_channel(lchip, lport, lport, chan_id));
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_get_channel_agg_info(uint8 lchip, uint16 gport, uint8* chanagg_id, uint8* chanagg_ref_cnt)
{
    uint8  chan_id = 0;
    uint16 lport = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /*get channel from port */
    if (SYS_BPE_FULL_EXTEND_MODE(lchip))
    {
        chan_id = p_gg_bpe_master[lchip]->port_attr[lport].chan_id +
        p_gg_bpe_master[lchip]->port_attr[lport].slice_id*64;
    }
    else
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
        if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_get_channel_agg_group(lchip, chan_id, chanagg_id));
    if (*chanagg_id)
    {
        CTC_ERROR_RETURN(sys_goldengate_linkagg_get_channel_agg_ref_cnt(lchip, *chanagg_id, chanagg_ref_cnt));
    }
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Channel agg group:%d, ref_cnt:%d!\n", *chanagg_id, *chanagg_ref_cnt);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_bpe_port_en(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 field = 0;
    uint32 cmd = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    field = enable ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_receiveDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));

    field = enable ? 0 : 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_transmitDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_src_extender_tag_en(uint8 lchip, uint16 gport, bool enable)
{

    uint8  tbl_idx = 0;
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 port_extender_downlink[2] = {0};
    EpePortExtenderDownlink_m epe_port_extender_downlink;

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    tbl_idx = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
    sal_memset(&epe_port_extender_downlink, 0, sizeof(EpePortExtenderDownlink_m));

    cmd = DRV_IOR(EpePortExtenderDownlink_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_idx, cmd, &epe_port_extender_downlink));

    GetEpePortExtenderDownlink(A, portExtenderDownlink_f, &epe_port_extender_downlink, port_extender_downlink);

    if (enable)
    {
        CTC_BMP_SET(port_extender_downlink, (lport & 0x3F));
    }
    else
    {
        CTC_BMP_UNSET(port_extender_downlink, (lport & 0x3F));
    }

    SetEpePortExtenderDownlink(A, portExtenderDownlink_f, &epe_port_extender_downlink, port_extender_downlink);

    cmd = DRV_IOW(EpePortExtenderDownlink_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tbl_idx, cmd, &epe_port_extender_downlink));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_stp_en(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 field = 0;
    uint32 cmd = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    field = enable ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));

    field = enable ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_stpCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_update_ecid_range(uint8 lchip, uint16 ecid)
{
    uint32 cmd = 0;
    uint32 min = 0;
    uint32 max = 0;
    IpeMuxHeaderAdjustCtl_m ctl;

    sal_memset(&ctl, 0, sizeof(IpeMuxHeaderAdjustCtl_m));
    cmd = DRV_IOR(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ctl));

    GetIpeMuxHeaderAdjustCtl(A, localMinEcid_f, &ctl, &min);
    GetIpeMuxHeaderAdjustCtl(A, localMaxEcid_f, &ctl, &max);

    if ((MIN_ECID_INIT_VAL == min) && (MAX_ECID_INIT_VAL == max))
    {
        min = ecid;
        max = ecid;
    }
    else
    {
        min = (ecid < min) ? ecid : min;
        max = (ecid > max) ? ecid : max;
    }

    SetIpeMuxHeaderAdjustCtl(A, localMinEcid_f, &ctl, &min);
    SetIpeMuxHeaderAdjustCtl(A, localMaxEcid_f, &ctl, &max);

    cmd = DRV_IOW(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_cfg_port(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender, uint8 src_mux_type, uint8 dest_mux_type)
{
    uint8  index = 0;
    uint8  gchip = 0;
    uint8  local_gchip = 0;

    uint8  chan_id = 0;
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 cascade_drv_port_base = 0;
    uint16 port_vlan_base = 0;
    uint16 port_base = 0;
    uint8 chanagg_id = 0;
    uint8 chanagg_ref_cnt= 0;
    int32 ret = CTC_E_NONE;
    ctc_internal_port_assign_para_t   inter_port_para;
    IpeHeaderAdjustPhyPortMap_m  ipe_map;
    EpeHeaderAdjustPhyPortMap_m  epe_l2_map;
    EpeHdrProcPhyPortMap_m       epe_l3_map;

    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    sys_goldengate_get_gchip_id(lchip, &local_gchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /*get channel from port */
    if (SYS_BPE_FULL_EXTEND_MODE(lchip))
    {
        chan_id = p_gg_bpe_master[lchip]->port_attr[lport].chan_id +
                       p_gg_bpe_master[lchip]->port_attr[lport].slice_id*64;
    }
    else
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
        if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_bpe_get_channel_agg_info(lchip, gport, &chanagg_id, &chanagg_ref_cnt));

    if (p_extender->enable)
    {
        /* cfg vlan base */
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ipe_map));

        cmd = DRV_IOR(EpeHeaderEditPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_l2_map));

        cmd = DRV_IOR(EpeHdrProcPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_l3_map));

        SetIpeHeaderAdjustPhyPortMap(V, useLogicPort_f, &ipe_map, 0);
        SetIpeHeaderAdjustPhyPortMap(V, localPhyPort_f, &ipe_map, lport);

        if (CTC_BPE_8021BR_UPSTREAM != p_extender->mode)
        {
            cascade_drv_port_base = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base);
        }
        else
        {
            cascade_drv_port_base = p_extender->port_base;
        }

        /* IPE */
        if (p_extender->vlan_base >= cascade_drv_port_base)
        {
            /* evbVlanId - portVlanBase - localPhyPortBase */
            SetIpeHeaderAdjustPhyPortMap(V, symbol_f, &ipe_map, 1);
            SetIpeHeaderAdjustPhyPortMap(V, portVlanBase_f, &ipe_map, p_extender->vlan_base - cascade_drv_port_base);
        }
        else
        {
            /* evbVlanId + portVlanBase - localPhyPortBase */
            SetIpeHeaderAdjustPhyPortMap(V, symbol_f, &ipe_map, 0);
            SetIpeHeaderAdjustPhyPortMap(V,portVlanBase_f, &ipe_map, cascade_drv_port_base - p_extender->vlan_base);
        }

        /* EPE */
        if (CTC_BPE_8021BR_UPSTREAM != p_extender->mode)
        {
            port_base = cascade_drv_port_base;
        }
        else
        {
            port_base = cascade_drv_port_base;
        }

        if (p_extender->vlan_base >= port_base)
        {
            /* localPhyPort + localPhyPortBase + portVlanBase */
            SetEpeHeaderEditPhyPortMap(V, symbol_f, &epe_l2_map, 0);
            SetEpeHdrProcPhyPortMap(V, symbol_f, &epe_l3_map, 0);
            port_vlan_base = p_extender->vlan_base - port_base;
        }
        else
        {
            /* localPhyPort + localPhyPortBase - portVlanBase */
            SetEpeHeaderEditPhyPortMap(V, symbol_f, &epe_l2_map, 1);
            SetEpeHdrProcPhyPortMap(V, symbol_f, &epe_l3_map, 1);
            port_vlan_base = port_base - p_extender->vlan_base;
        }

        cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ipe_map));

        SetEpeHeaderEditPhyPortMap(V, portVlanBase_f, &epe_l2_map, port_vlan_base);
        cmd = DRV_IOW(EpeHeaderEditPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_l2_map));

        SetEpeHdrProcPhyPortMap(V, portVlanBase_f, &epe_l3_map, port_vlan_base);
        cmd = DRV_IOW(EpeHdrProcPhyPortMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &epe_l3_map));

        /* cfg mux type */
        field_val =  src_mux_type;
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

        if ((CTC_BPE_8021BR_CB_CASCADE == p_extender->mode) || (CTC_BPE_8021BR_EXTENDED == p_extender->mode))
        {
            CTC_ERROR_RETURN(_sys_goldengate_bpe_src_extender_tag_en(lchip, gport, FALSE));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_bpe_src_extender_tag_en(lchip, gport, TRUE));
        }

        if ((CTC_BPE_MUX_DEMUX_MODE == p_extender->mode) || (CTC_BPE_8021QBG_M_VEPA == p_extender->mode)
           || (CTC_BPE_8021BR_CB_CASCADE == p_extender->mode))
        {
            CTC_ERROR_RETURN(_sys_goldengate_bpe_update_phyport(lchip, lport, chan_id, FALSE));
            CTC_ERROR_RETURN(sys_goldengate_add_port_range_to_channel(lchip, p_extender->port_base, p_extender->port_base + p_extender->port_num - 1, chan_id));
            for (index = 0; index < p_extender->port_num; index++)
            {
                /* set internal port */
                inter_port_para.gchip = gchip;
                inter_port_para.fwd_gport = gport;
                inter_port_para.inter_port = p_extender->port_base + index;
                inter_port_para.type = CTC_INTERNAL_PORT_TYPE_FWD;

                ret = sys_goldengate_internal_port_set(lchip, &inter_port_para);
                if ((CTC_E_NONE != ret)&&(!((CTC_E_QUEUE_INTERNAL_PORT_IN_USE == ret) && chanagg_id)))
                {
                    return ret;
                }

                if (chanagg_id)
                {
                    CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel_agg(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), chanagg_id));
                }
                else
                {
                    CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), chan_id));
                }

                /* cfg disable port stp */
                if(CTC_BPE_MUX_DEMUX_MODE != p_extender->mode)
                {
                    CTC_ERROR_RETURN(_sys_goldengate_bpe_stp_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, (p_extender->port_base + index)), FALSE));
                }

                field_val =  dest_mux_type;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), cmd, &field_val));

                field_val = chanagg_id? chanagg_id:(lport & 0x7F);
                cmd = DRV_IOW(DsPortLinkAgg_t, DsPortLinkAgg_u1_g2_sourceChannel_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), cmd, &field_val));

                CTC_ERROR_RETURN(_sys_goldengate_bpe_port_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, (p_extender->port_base + index)), TRUE));
            }
            if (chanagg_id)
            {
                CTC_ERROR_RETURN(sys_goldengate_linkagg_set_channel_agg_ref_cnt(lchip, chanagg_id, TRUE));
            }
        }
        else
        {
            /* cfg disable port stp */
            CTC_ERROR_RETURN(_sys_goldengate_bpe_stp_en(lchip, gport, FALSE));

            field_val = 1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }

        if ((CTC_BPE_8021BR_UPSTREAM == p_extender->mode))
        {
            /* cfg dest port */
            field_val =  dest_mux_type;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            /*disable bridge from upstream*/
            field_val =  0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }
        else if (CTC_BPE_8021BR_CB_CASCADE == p_extender->mode)/*for mcast and cpu send pdu*/
        {
            /* cfg dest port */
            field_val =  dest_mux_type;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            field_val =  1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_evbDefaultLocalPhyPortValid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            if (chanagg_id)
            {
                field_val = chanagg_id;
                cmd = DRV_IOW(DsPortLinkAgg_t, DsPortLinkAgg_u1_g2_sourceChannel_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
            }
        }

        if (SYS_BPE_FULL_EXTEND_MODE(lchip) && (1 == p_extender->port_num))/*extend to one phyport*/
        {
            field_val =  0;
            cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base), cmd, &field_val));

            field_val = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base);
            cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        }
        if (p_gg_bpe_master[lchip]->use_logic_port)
        {
            field_val =  1;
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_useLogicPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

            cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_useLogicPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        }
    }
    else
    {
        /* release port extender */
        field_val =  0;
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

        CTC_ERROR_RETURN(_sys_goldengate_bpe_src_extender_tag_en(lchip, gport, FALSE));

        if ((CTC_BPE_MUX_DEMUX_MODE == p_extender->mode) || (CTC_BPE_8021QBG_M_VEPA == p_extender->mode)
           || ((CTC_BPE_8021BR_CB_CASCADE == p_extender->mode)))
        {
            if (chanagg_ref_cnt <= 1)
            {
                for (index = 0; index < p_extender->port_num; index++)
                {
                    /* set internal port */
                    inter_port_para.gchip = gchip;
                    inter_port_para.fwd_gport = gport;
                    inter_port_para.inter_port = p_extender->port_base + index;
                    inter_port_para.type = CTC_INTERNAL_PORT_TYPE_FWD;

                    CTC_ERROR_RETURN(sys_goldengate_internal_port_release(lchip, &inter_port_para));

                    if (chanagg_id)
                    {
                        CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel_agg(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index)));
                    }
                    else
                    {
                        CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), SYS_DROP_CHANNEL_ID));
                    }

                    /* cfg enable port stp */
                    if(CTC_BPE_MUX_DEMUX_MODE != p_extender->mode)
                    {
                        CTC_ERROR_RETURN(_sys_goldengate_bpe_stp_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, (p_extender->port_base + index)), TRUE));
                    }

                    field_val =  0;
                    cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), cmd, &field_val));

                    field_val = lport & 0x7F;
                    cmd = DRV_IOW(DsPortLinkAgg_t, DsPortLinkAgg_u1_g2_sourceChannel_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base + index), cmd, &field_val));

                    CTC_ERROR_RETURN(_sys_goldengate_bpe_port_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, (p_extender->port_base + index)), FALSE));

                }
                CTC_ERROR_RETURN(sys_goldengate_remove_port_range_to_channel(lchip, p_extender->port_base, p_extender->port_base + p_extender->port_num - 1, chan_id));
            }
            if (chanagg_id)
            {
                CTC_ERROR_RETURN(sys_goldengate_linkagg_set_channel_agg_ref_cnt(lchip, chanagg_id, FALSE));
            }
             CTC_ERROR_RETURN(_sys_goldengate_bpe_update_phyport(lchip, lport, chan_id, TRUE));
        }
        else
        {
            /* cfg enable port stp */
            CTC_ERROR_RETURN(_sys_goldengate_bpe_stp_en(lchip, gport, TRUE));

            field_val = 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }

        if ((CTC_BPE_8021BR_UPSTREAM == p_extender->mode))
        {
            /* cfg dest port */
            field_val =  0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            /*enable bridge from upstream*/
            field_val =  1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }
        else if (CTC_BPE_8021BR_CB_CASCADE == p_extender->mode)
        {
            /* cfg dest port */
            field_val =  0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            field_val =  0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_evbDefaultLocalPhyPortValid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

            field_val = lport & 0x7F;
            cmd = DRV_IOW(DsPortLinkAgg_t, DsPortLinkAgg_u1_g2_sourceChannel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }

        if (SYS_BPE_FULL_EXTEND_MODE(lchip))
        {
            field_val = lport;
            cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        }

        if (p_gg_bpe_master[lchip]->use_logic_port)
        {
            field_val =  0;
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_useLogicPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

            cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_useLogicPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_ecid_nh_update(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender)
{
    DsFwd_m dsfwd;
    uint32 cmd = 0;
    uint16 idx = 0;
    uint32 dest_map = 0;
    uint16 offset = 0;

    uint16 lport = 0;
    uint16 mc_gid = 0;
    uint32 dsnh_offset = 0;

    uint16 ecid_base = 0;
    uint16 nh_num = 0;
    uint8 enable = 0;

    if (!(CTC_IS_LINKAGG_PORT(gport) && (CTC_BPE_8021BR_EXTENDED == p_extender->mode)))
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    }

    CTC_PTR_VALID_CHECK(p_extender);

    ecid_base = p_extender->vlan_base;
    nh_num = p_extender->port_num;
    enable = p_extender->enable;

    for (idx = 0; idx < nh_num; idx++)
    {
        if (ecid_base > 0x1000)
        {
            mc_gid = ecid_base - 0x1000;
            offset = p_gg_bpe_master[lchip]->pe_mc_dsfwd_base + (mc_gid + idx);

            dest_map = SYS_ENCODE_MCAST_IPE_DESTMAP(0, (mc_gid));
            dsnh_offset = SYS_DSNH_INDEX_FOR_NONE;
        }
        else
        {
            if (CTC_IS_LINKAGG_PORT(gport))
            {
                dest_map = SYS_ENCODE_DESTMAP(CTC_LINKAGG_CHIPID, CTC_GPORT_LINKAGG_ID(gport));
            }
            else
            {
                dest_map = SYS_ENCODE_DESTMAP(SYS_MAP_CTC_GPORT_TO_GCHIP(gport), lport);
            }
            sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &dsnh_offset);
            offset = p_gg_bpe_master[lchip]->pe_uc_dsfwd_base + (ecid_base + idx);
        }

        if (0 == enable)/*drop when disable*/
        {
            dest_map = 0xFFFF;
            dsnh_offset = 0x3FFFF;
        }

        sal_memset(&dsfwd, 0, sizeof(dsfwd));
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (offset / 2), cmd, &dsfwd));

        if ((offset) % 2 == 0)
        {
            SetDsFwd(V, array_0_destMap_f,             &dsfwd, dest_map);
            SetDsFwd(V, array_0_nextHopPtr_f,          &dsfwd, dsnh_offset);
            SetDsFwd(V, array_0_nextHopExt_f,          &dsfwd, 1);
        }
        else
        {
            SetDsFwd(V, array_1_destMap_f,             &dsfwd, dest_map);
            SetDsFwd(V, array_1_nextHopPtr_f,          &dsfwd, dsnh_offset);
            SetDsFwd(V, array_1_nextHopExt_f,          &dsfwd, 1);
        }

        cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (offset / 2), cmd, &dsfwd));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_bpe_update_datapath_port_attr(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender)
{
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    sys_datapath_serdes_info_t* p_serdes = NULL;
    uint16 drv_port = 0;
    uint8 i = 0;
    uint16 port_base = 0;

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "Set gport:%d vlan_base %d num:%d internal_gport:%d!\n",
                    gport, p_extender->vlan_base, p_extender->port_num, p_extender->port_base);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_PTR_VALID_CHECK(p_extender);

    if (p_extender->enable)
    {
        CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, p_extender->port_base, &port_attr));
        SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"lport:%d, port_type:%d, mac_id:%d, pcs_mode:%d, serdes_id:%d, need_ext:%d\n",
            lport, port_attr->port_type, port_attr->mac_id, port_attr->pcs_mode, port_attr->serdes_id, port_attr->need_ext);

        /* 1. updata lport info for datapath */
        port_attr->port_type = SYS_DATAPATH_NETWORK_PORT;
        port_attr->mac_id = p_gg_bpe_master[lchip]->port_attr[lport].mac_id;
        port_attr->slice_id = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
        port_attr->serdes_id = p_gg_bpe_master[lchip]->port_attr[lport].serdes_id;

        port_attr->speed_mode = p_gg_bpe_master[lchip]->port_attr[lport].speed_mode;
        port_attr->cal_index = p_gg_bpe_master[lchip]->port_attr[lport].cal_index;
        port_attr->chan_id = p_gg_bpe_master[lchip]->port_attr[lport].chan_id;
        port_attr->pcs_mode = p_gg_bpe_master[lchip]->port_attr[lport].pcs_mode;

        port_attr->need_ext = p_gg_bpe_master[lchip]->port_attr[lport].need_ext;
        for (drv_port = (p_extender->port_base + 1); drv_port < p_extender->port_num; drv_port++)
        {
            CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, drv_port, &port_attr));
            port_attr->port_type = SYS_DATAPATH_RSV_PORT;
        }

        /* 2. updata serdes info for datapath */
        if (p_gg_bpe_master[lchip]->port_attr[lport].need_ext)
        {
            for(i = 0; i < 4; i++)
            {
                CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, (p_gg_bpe_master[lchip]->port_attr[lport].serdes_id -i), &p_serdes));
                p_serdes->lport = p_extender->port_base % SYS_CHIP_PER_SLICE_PORT_NUM;
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, p_gg_bpe_master[lchip]->port_attr[lport].serdes_id, &p_serdes));
            p_serdes->lport = p_extender->port_base % SYS_CHIP_PER_SLICE_PORT_NUM;
        }
    }
    else
    {
        for (drv_port = p_extender->port_base; drv_port < p_extender->port_num; drv_port++)
        {
            CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, drv_port, &port_attr));
            port_attr->port_type = SYS_DATAPATH_RSV_PORT;
        }

        /* 1. recover port_base info */
        port_base = p_extender->port_base;
        CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, p_extender->port_base, &port_attr));
        port_attr->port_type = SYS_DATAPATH_RSV_PORT;
        port_attr->mac_id = p_gg_bpe_master[lchip]->port_attr[port_base].mac_id;
        port_attr->slice_id = SYS_MAP_DRV_LPORT_TO_SLICE(port_base);
        port_attr->serdes_id = p_gg_bpe_master[lchip]->port_attr[port_base].serdes_id;

        port_attr->speed_mode = p_gg_bpe_master[lchip]->port_attr[port_base].speed_mode;
        port_attr->cal_index = p_gg_bpe_master[lchip]->port_attr[port_base].cal_index;
        port_attr->chan_id = p_gg_bpe_master[lchip]->port_attr[port_base].chan_id;
        port_attr->pcs_mode = p_gg_bpe_master[lchip]->port_attr[port_base].pcs_mode;

        port_attr->need_ext = p_gg_bpe_master[lchip]->port_attr[port_base].need_ext;

        /* 2. recover gport's serdes info */
        CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
        if (port_attr->need_ext)
        {
            for(i = 0; i < 4; i++)
            {
                CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, (p_gg_bpe_master[lchip]->port_attr[lport].serdes_id -i), &p_serdes));
                p_serdes->lport = 0xFFFF;
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, p_gg_bpe_master[lchip]->port_attr[lport].serdes_id, &p_serdes));
            p_serdes->lport = 0xFFFF;
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_bpe_extend_nh_create(uint8 lchip, uint16 gport, uint16 port_base, uint16 nh_num)
{
    int32  ret = 0;
    uint16 idx = 0;
    uint8 lchip_start = 0;
    uint8 lchip_end                 = 0;
    uint16 lport = 0;
    uint16 local_chip = lchip;

    uint16 extend_gport = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    for (idx = 0; idx < nh_num; idx++)
    {
        extend_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(SYS_MAP_CTC_GPORT_TO_GCHIP(gport), \
                                                             SYS_MAP_SYS_LPORT_TO_DRV_LPORT(port_base) + idx);
        lchip = local_chip;
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            ret = sys_goldengate_brguc_nh_create(lchip, extend_gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
            if ((CTC_E_NONE != ret) && (CTC_E_ENTRY_EXIST != ret))
            {
                return ret;
            }
        }
        CTC_ERROR_RETURN(sys_goldengate_l2_set_dsmac_for_bpe(local_chip, extend_gport, TRUE));
   }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_extend_nh_delete(uint8 lchip, uint16 gport, uint16 port_base, uint16 nh_num)
{
    uint16 idx = 0;
    uint8 lchip_start = 0;
    uint8 lchip_end                 = 0;
    uint16 lport = 0;
    int32  ret = 0;

    uint16 extend_gport = 0;
    uint8 chanagg_id = 0;
    uint8 chanagg_ref_cnt= 0;
    uint8 lchip_temp = lchip;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    CTC_ERROR_RETURN(_sys_goldengate_bpe_get_channel_agg_info(lchip, gport, &chanagg_id, &chanagg_ref_cnt));
    if ((chanagg_id) && (0 != chanagg_ref_cnt))
    {
        return CTC_E_NONE;
    }

    for (idx = 0; idx < nh_num; idx++)
    {
        extend_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(SYS_MAP_CTC_GPORT_TO_GCHIP(gport),\
                                                      SYS_MAP_SYS_LPORT_TO_DRV_LPORT(port_base + idx));
        lchip = lchip_temp;
       CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
       {
           ret = sys_goldengate_brguc_nh_delete(lchip, extend_gport);
           if ((CTC_E_NONE != ret) && (CTC_E_NH_NOT_EXIST != ret))
            {
                return ret;
            }
       }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_bpe_get_port_extend_mcast_en(uint8 lchip, uint8 *p_enable)
{
    uint32 cmd = 0;
    uint32 value = 0;


    SYS_BPE_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_portExtenderMcastEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    *p_enable = value;
    return CTC_E_NONE;
}

int32
sys_goldengate_bpe_set_port_extend_mcast_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 value = 0;
    ctc_chip_device_info_t device_info;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    value = enable ? 1 : 0;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_portExtenderMcastEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    value = enable ? 1 : 0;
    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_portExtenderMcastEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, &device_info));
    if (device_info.version_id > 1)
    {
        cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        if (enable)
        {
            CTC_BIT_SET(value, 4);
        }
        else
        {
            CTC_BIT_UNSET(value, 4);
        }
        cmd = DRV_IOW(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        value = enable?1:0;
        cmd = DRV_IOW(EpeHdrProcParityCtl2_t, EpeHdrProcParityCtl2_l2L3EditPipe3FifoParityEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_bpe_update_datapath_original_mapping(uint8 lchip, uint16 lport, sys_datapath_lport_attr_t* port_attr)
{
    if ((NULL == port_attr) || (NULL == p_gg_bpe_master[lchip]) || (NULL == p_gg_bpe_master[lchip]->port_attr))
    {
        return CTC_E_NONE;
    }
    p_gg_bpe_master[lchip]->port_attr[lport].port_type = port_attr->port_type;
    p_gg_bpe_master[lchip]->port_attr[lport].mac_id = port_attr->mac_id;
    p_gg_bpe_master[lchip]->port_attr[lport].chan_id = port_attr->chan_id;
    p_gg_bpe_master[lchip]->port_attr[lport].serdes_id = port_attr->serdes_id;

    p_gg_bpe_master[lchip]->port_attr[lport].need_ext = port_attr->need_ext;
    p_gg_bpe_master[lchip]->port_attr[lport].cal_index = port_attr->cal_index;
    p_gg_bpe_master[lchip]->port_attr[lport].speed_mode = port_attr->speed_mode;
    p_gg_bpe_master[lchip]->port_attr[lport].slice_id = port_attr->slice_id;

    p_gg_bpe_master[lchip]->port_attr[lport].pcs_mode = port_attr->pcs_mode;
    return CTC_E_NONE;
}

#define __BPE_INTERFACE__
int32
sys_goldengate_bpe_set_port_extender(uint8 lchip, uint16 gport, ctc_bpe_extender_t* p_extender)
{
    uint16  src_lport = 0;
    uint16  dst_lport = 0;

    uint8  src_mux_type = 0;
    uint8  dest_mux_type = 0;
    uint16 extern_start_port = 0;
    uint16 extern_end_port = 0;
    uint16 temp = 0;

    SYS_BPE_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_extender);

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Enqueue mode:%d, max extend number:%d!\n",
                    p_gg_bpe_master[lchip]->enq_mode, p_gg_bpe_master[lchip]->max_extend_num);
    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "Set gport:%d vlan_base %d num:%d internal_gport:%d!\n",
                    gport, p_extender->vlan_base, p_extender->port_num, p_extender->port_base);

     if (p_gg_bpe_master[lchip]->is_port_extender
        && ((p_extender->mode == CTC_BPE_MUX_DEMUX_MODE)
        || (p_extender->mode == CTC_BPE_8021QBG_M_VEPA)
        || (p_extender->mode == CTC_BPE_8021BR_CB_CASCADE)))
    {
        return CTC_E_BPE_INVLAID_BPE_MODE;
    }
    else if ((!p_gg_bpe_master[lchip]->is_port_extender)
            && ((p_extender->mode == CTC_BPE_8021BR_PE_CASCADE)
            || (p_extender->mode == CTC_BPE_8021BR_EXTENDED)
            || (p_extender->mode == CTC_BPE_8021BR_UPSTREAM)))
    {
        return CTC_E_BPE_INVLAID_BPE_MODE;
    }

    if (CTC_IS_LINKAGG_PORT(gport) && (CTC_BPE_8021BR_EXTENDED == p_extender->mode))
    {
        /*extended port on PE need support agg*/
    }
    else /*do check*/
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, src_lport);

        if ((src_lport & 0xFF) >= SYS_PHY_PORT_NUM_PER_SLICE)
        {
            return CTC_E_BPE_INVALID_BPE_CAPABLE_GPORT;
        }
        if (CTC_BPE_8021BR_EXTENDED == p_extender->mode)
        {
            p_extender->port_num = 0;
        }

        if (SYS_BPE_FULL_EXTEND_MODE(lchip))
        {
            if (SYS_DATAPATH_NETWORK_PORT != p_gg_bpe_master[lchip]->port_attr[src_lport].port_type)
            {
                return CTC_E_BPE_INVALID_BPE_CAPABLE_GPORT;
            }
        }

        if ((CTC_BPE_MUX_DEMUX_MODE == p_extender->mode) || (CTC_BPE_8021QBG_M_VEPA == p_extender->mode)
           || (CTC_BPE_8021BR_CB_CASCADE == p_extender->mode))
        {
            if (SYS_BPE_FULL_EXTEND_MODE(lchip))
            {
                uint8 queue_num_per_port = 0;
                if (p_gg_bpe_master[lchip]->max_extend_num > 200)/*4 queue mode*/
                {
                    queue_num_per_port = 4;
                }
                else
                {
                    queue_num_per_port = 8;
                }
                if ((p_extender->port_num < (8 / queue_num_per_port) /* queue number range for one channel must be 8~64 */
                    || p_extender->port_num > (64 / queue_num_per_port)))
                {
                    return CTC_E_INVALID_PARAM;
                }
            }

            dst_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base);
            extern_start_port = SYS_BPE_FULL_EXTEND_MODE(lchip)? 0 : SYS_INTERNAL_PORT_START;
            extern_end_port = SYS_BPE_FULL_EXTEND_MODE(lchip)?
                             (p_gg_bpe_master[lchip]->max_extend_num - 1) : (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1);
            if ((SYS_MAP_DRV_LPORT_TO_SLICE(src_lport) != SYS_MAP_DRV_LPORT_TO_SLICE(dst_lport))
                || ((dst_lport & 0xFF) > extern_end_port)
                || ((dst_lport & 0xFF) < extern_start_port)
                || (dst_lport >= SYS_CHIP_PER_SLICE_PORT_NUM*2))
            {
                return CTC_E_BPE_INVALID_PORT_BASE;
            }

            if (p_extender->vlan_base > CTC_MAX_VLAN_ID)
            {
                return CTC_E_BPE_INVALID_VLAN_BASE;
            }

            if ((0 == p_extender->port_num)
                || ((p_extender->vlan_base + p_extender->port_num - 1) > 0x1FF)/*due to the limit of spec ,vlan_base + port_num can not beyond 0x1FF*/
                || ((dst_lport & 0xFF) + p_extender->port_num  > (extern_end_port + 1)) )
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        else
        {
            if (0 == p_gg_bpe_master[lchip]->use_logic_port)
            {
                dst_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_extender->port_base);
                if (dst_lport >= SYS_CHIP_PER_SLICE_PORT_NUM*2)
                {
                    return CTC_E_BPE_INVALID_PORT_BASE;
                }
            }

            if (CTC_BPE_8021BR_UPSTREAM == p_extender->mode)
            {
                temp = (p_extender->vlan_base >= p_extender->port_base)?
                                      (p_extender->vlan_base - p_extender->port_base) : (p_extender->port_base - p_extender->vlan_base);
                if (temp > 0xFFF)/*the value is EpeHeaderEditPhyPortMap.portVlanBase[11:0]*/
                {
                    return CTC_E_INVALID_PARAM;
                }
            }
        }


    }

    switch(p_extender->mode)
    {
    case CTC_BPE_MUX_DEMUX_MODE:
        src_mux_type = BPE_IGS_MUXTYPE_MUXDEMUX;
        dest_mux_type = BPE_EGS_MUXTYPE_MUXDEMUX;
        break;
    case CTC_BPE_8021QBG_M_VEPA:
        src_mux_type = BPE_IGS_MUXTYPE_EVB;
        dest_mux_type = BPE_EGS_MUXTYPE_EVB;
        break;
    case CTC_BPE_8021BR_CB_CASCADE:
        src_mux_type = BPE_IGS_MUXTYPE_CB_DOWNLINK;
        dest_mux_type = BPE_EGS_MUXTYPE_CB_CASCADE;
        break;
    case  CTC_BPE_8021BR_PE_CASCADE:
        src_mux_type = BPE_IGS_MUXTYPE_PE_DOWNLINK_WITH_CASCADE_PORT;
        dest_mux_type = BPE_EGS_MUXTYPE_NOMUX;
        break;
    case CTC_BPE_8021BR_EXTENDED:
        src_mux_type = BPE_IGS_MUXTYPE_NOMUX;
        dest_mux_type = BPE_EGS_MUXTYPE_NOMUX;
        break;
    case CTC_BPE_8021BR_UPSTREAM:
        src_mux_type = BPE_IGS_MUXTYPE_PE_UPLINK;
        dest_mux_type = BPE_EGS_MUXTYPE_UPSTREAM;
        break;
    default:
        return CTC_E_BPE_INVLAID_BPE_MODE;
    }

    if ((CTC_BPE_8021BR_EXTENDED == p_extender->mode) || (CTC_BPE_8021BR_PE_CASCADE == p_extender->mode))
    {
        p_extender->port_num = (CTC_BPE_8021BR_EXTENDED == p_extender->mode) ? 1 : p_extender->port_num;
        CTC_ERROR_RETURN(_sys_goldengate_bpe_ecid_nh_update(lchip, gport, p_extender));
    }

    if ((CTC_BPE_8021BR_EXTENDED == p_extender->mode) && (p_extender->vlan_base < 0x1000))
    {
        CTC_ERROR_RETURN(_sys_goldengate_bpe_update_ecid_range(lchip, p_extender->vlan_base));
    }

    if (!(CTC_IS_LINKAGG_PORT(gport) && (CTC_BPE_8021BR_EXTENDED == p_extender->mode)))
    {
        CTC_ERROR_RETURN(_sys_goldengate_bpe_cfg_port(lchip, gport, p_extender, src_mux_type, dest_mux_type));
    }

    if ((CTC_BPE_MUX_DEMUX_MODE == p_extender->mode) || (CTC_BPE_8021QBG_M_VEPA == p_extender->mode)
        || (CTC_BPE_8021BR_CB_CASCADE == p_extender->mode))
    {
        if (p_extender->enable)
        {
            CTC_ERROR_RETURN(_sys_goldengate_bpe_extend_nh_create(lchip, gport, p_extender->port_base, p_extender->port_num));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_bpe_extend_nh_delete(lchip, gport, p_extender->port_base, p_extender->port_num));
        }
        if (SYS_BPE_FULL_EXTEND_MODE(lchip))
        {
            CTC_ERROR_RETURN( _sys_goldengate_bpe_update_datapath_port_attr(lchip, gport, p_extender));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_bpe_get_port_extender(uint8 lchip, uint16 gport, bool* p_enable)
{
    uint32 cmd = 0;
    uint16 lport = 0;

    uint8  mux_type = 0;
    IpePhyPortMuxCtl_m ipe_phy_port_mux_ctl;

    SYS_BPE_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_BPE_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if ((lport & 0xFF) >= SYS_PHY_PORT_NUM_PER_SLICE)
    {
        return CTC_E_BPE_INVALID_BPE_CAPABLE_GPORT;
    }

    sal_memset(&ipe_phy_port_mux_ctl, 0, sizeof(IpePhyPortMuxCtl_m));
    cmd = DRV_IOR(IpePhyPortMuxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (lport & 0xFF) + SYS_MAP_DRV_LPORT_TO_SLICE(lport) * 64, cmd, &ipe_phy_port_mux_ctl));

    mux_type = GetIpePhyPortMuxCtl(V, muxType_f, &ipe_phy_port_mux_ctl);

    if (mux_type)
    {
        *p_enable = TRUE;
    }
    else
    {
        *p_enable = FALSE;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_bpe_init(uint8 lchip, void* bpe_global_cfg)
{
    ctc_bpe_global_cfg_t* p_bpe_global_cfg = NULL;
    int32  ret = 0;
    uint16 offset = 0;
    uint32 dest_map = 0;
    uint32 cmd = 0;
    uint32 val = 0;
    uint8  gchip = 0;
    uint32 dsfwd_offset = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    DsFwd_m dsfwd;
    IpeDsFwdCtl_m dsfwd_ctl;
    IpeMuxHeaderAdjustCtl_m mux_hdr_ctl;
    EpePortExtenderDownlink_m epe_port_extender_downlink;
    uint16 gport = 0;
    uint32 dsnh_offset = 0;
    uint8 serdes_id = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    uint32 field_val = 0;
    ctc_chip_device_info_t dev_info;

    if (NULL != p_gg_bpe_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_bpe_global_cfg = (ctc_bpe_global_cfg_t*)bpe_global_cfg;

    /*init soft table*/
    p_gg_bpe_master[lchip] = (sys_bpe_master_t*)mem_malloc(MEM_PORT_MODULE, sizeof(sys_bpe_master_t));
    if (NULL == p_gg_bpe_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_bpe_master[lchip], 0, sizeof(sys_bpe_master_t));
    p_gg_bpe_master[lchip]->is_port_extender = p_bpe_global_cfg->is_port_extender;

    if (p_gg_bpe_master[lchip]->is_port_extender)
    {
        /*alloc ucast dsfwd offset*/
        ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                             (p_bpe_global_cfg->max_uc_ecid_id +1), &p_gg_bpe_master[lchip]->pe_uc_dsfwd_base);
        if (CTC_E_NONE != ret)
        {
           mem_free(p_gg_bpe_master[lchip]);
          return ret;
        }
        if ((p_gg_bpe_master[lchip]->pe_uc_dsfwd_base % 2) != 0)
        {
            p_gg_bpe_master[lchip]->pe_uc_dsfwd_base ++;
        }
        /*alloc mcast dsfwd offset*/
        ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                            (p_bpe_global_cfg->max_mc_ecid_id +1), &p_gg_bpe_master[lchip]->pe_mc_dsfwd_base);

        if (CTC_E_NONE != ret)
        {
           mem_free(p_gg_bpe_master[lchip]);
           return ret;
        }
        if ((p_gg_bpe_master[lchip]->pe_mc_dsfwd_base % 2) != 0)
        {
            p_gg_bpe_master[lchip]->pe_mc_dsfwd_base ++;
        }

        cmd = DRV_IOR(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &dsfwd_ctl));
        SetIpeDsFwdCtl(V, portExtenderUcFwdPtrBase_f, &dsfwd_ctl, (p_gg_bpe_master[lchip]->pe_uc_dsfwd_base/2));
        SetIpeDsFwdCtl(V, portExtenderMcFwdPtrBase_f, &dsfwd_ctl, (p_gg_bpe_master[lchip]->pe_mc_dsfwd_base/2));
        cmd = DRV_IOW(IpeDsFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &dsfwd_ctl));

        /* config to drop for PE*/
        for(offset = 0; offset  <= p_bpe_global_cfg->max_uc_ecid_id;offset++)
        {
            dsfwd_offset = p_gg_bpe_master[lchip]->pe_uc_dsfwd_base + offset;
            dest_map = 0xFFFF;
            sal_memset(&dsfwd, 0, sizeof(dsfwd));

            cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset/2) ,cmd, &dsfwd));
            if (dsfwd_offset % 2 == 0)
            {
                SetDsFwd(V, array_0_destMap_f,             &dsfwd, dest_map);
                SetDsFwd(V, array_0_nextHopPtr_f,          &dsfwd, 0x3FFFF);
            }
            else
            {
                SetDsFwd(V, array_1_destMap_f,             &dsfwd, dest_map);
                SetDsFwd(V, array_1_nextHopPtr_f,          &dsfwd, 0x3FFFF);
            }
            cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset / 2), cmd, &dsfwd));
        }
        for(offset = 0; offset  <= p_bpe_global_cfg->max_mc_ecid_id;offset++)
        {
            dsfwd_offset = p_gg_bpe_master[lchip]->pe_mc_dsfwd_base + offset;
            dest_map = 0xFFFF;
            sal_memset(&dsfwd, 0, sizeof(dsfwd));

            cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset / 2), cmd, &dsfwd));
            if (dsfwd_offset % 2 == 0)
            {
                SetDsFwd(V, array_0_destMap_f,             &dsfwd, dest_map);
                SetDsFwd(V, array_0_nextHopPtr_f,          &dsfwd, 0x3FFFF);
            }
            else
            {
                SetDsFwd(V, array_1_destMap_f,             &dsfwd, dest_map);
                SetDsFwd(V, array_1_nextHopPtr_f,          &dsfwd, 0x3FFFF);
            }
            cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset / 2), cmd, &dsfwd));
        }

        /*for rx untagged on PE upstream, need to cpu, use default PE E-CID 0*/
        offset = 0;
        dsfwd_offset = p_gg_bpe_master[lchip]->pe_uc_dsfwd_base + offset;
        sal_memset(&dsfwd, 0, sizeof(dsfwd));
        sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &dsnh_offset);
        dest_map = SYS_ENCODE_EXCP_DESTMAP(lchip, SYS_PKT_CPU_QDEST_BY_DMA);
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset / 2) , cmd, &dsfwd));
        if (dsfwd_offset % 2 == 0)
        {
            SetDsFwd(V, array_0_destMap_f,             &dsfwd, dest_map);
            SetDsFwd(V, array_0_nextHopPtr_f,          &dsfwd, dsnh_offset);
        }
        else
        {
            SetDsFwd(V, array_1_destMap_f,             &dsfwd, dest_map);
            SetDsFwd(V, array_1_nextHopPtr_f,          &dsfwd, dsnh_offset);
        }
        cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (dsfwd_offset / 2), cmd, &dsfwd));

        p_gg_bpe_master[lchip]->use_logic_port = 1;  /*PE must use logic port to mapping ecid for upstream agg*/
    }

    cmd = DRV_IOR(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mux_hdr_ctl));
    SetIpeMuxHeaderAdjustCtl(V, muxVlanCheck_f, &mux_hdr_ctl, 1);
    SetIpeMuxHeaderAdjustCtl(V, ecidNoneZeroDiscard_f, &mux_hdr_ctl, 0);
    SetIpeMuxHeaderAdjustCtl(V, portExtenderMetBase_f, &mux_hdr_ctl, 0);
    SetIpeMuxHeaderAdjustCtl(V, etagTpid_f, &mux_hdr_ctl, CTC_BPE_ETAG_TPID);
    SetIpeMuxHeaderAdjustCtl(V, evbTpid_f, &mux_hdr_ctl, CTC_BPE_EVB_TPID);
    SetIpeMuxHeaderAdjustCtl(V, tpid_f, &mux_hdr_ctl, 0x88A8);
    SetIpeMuxHeaderAdjustCtl(V, cpuLocalPhyPort_f, &mux_hdr_ctl, 0);   /*default PE E-CID*/
    SetIpeMuxHeaderAdjustCtl(V, localEcidChkEn_f, &mux_hdr_ctl, 1);
    SetIpeMuxHeaderAdjustCtl(V, localMinEcid_f, &mux_hdr_ctl, MIN_ECID_INIT_VAL);
    SetIpeMuxHeaderAdjustCtl(V, localMaxEcid_f, &mux_hdr_ctl, MAX_ECID_INIT_VAL);
    cmd = DRV_IOW(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &mux_hdr_ctl));

    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id > 1)
    {
        /* bit[15]=0, total 16bits */
        cmd = DRV_IOR(IpeForwardReserved2_t, IpeForwardReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val &= 0x7fff;
        cmd = DRV_IOW(IpeForwardReserved2_t, IpeForwardReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /* bit[5]=1, total 16bits */
        cmd = DRV_IOR(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= 0x20;
        cmd = DRV_IOW(EpeHdrEditReserved2_t, EpeHdrEditReserved2_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /* bit[4]=1, total 16bits */
        cmd = DRV_IOR(BufRetrvReserved0_t, BufRetrvReserved0_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= 0x10;
        cmd = DRV_IOW(BufRetrvReserved0_t, BufRetrvReserved0_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        cmd = DRV_IOR(BufRetrvReserved1_t, BufRetrvReserved1_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val |= 0x10;
        cmd = DRV_IOW(BufRetrvReserved1_t, BufRetrvReserved1_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    val = CTC_BPE_ETAG_TPID;
    cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_portExtenderTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &val));
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_portExtenderTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &val));

    val = CTC_BPE_EVB_TPID;
    cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_evbTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 ,cmd, &val));
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_evbTpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0 , cmd, &val));

    sal_memset(&epe_port_extender_downlink, 0, sizeof(EpePortExtenderDownlink_m));

    CTC_ERROR_RETURN(sys_goldengate_bpe_set_port_extend_mcast_en(lchip, 0));

    for (offset = 0; offset < SYS_MAX_LOCAL_SLICE_NUM; offset++)
    {
        val = SYS_CHIP_PER_SLICE_PORT_NUM * offset;
        cmd = DRV_IOW(IpeHeaderAdjustPortBaseCtl_t, IpeHeaderAdjustPortBaseCtl_localPhyPortBase_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset ,cmd, &val));

        val = SYS_CHIP_PER_SLICE_PORT_NUM * offset;
        cmd = DRV_IOW(EpeHeaderEditPortBaseCtl_t, EpeHeaderEditPortBaseCtl_localPhyPortBase_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset ,cmd, &val));

        val = SYS_CHIP_PER_SLICE_PORT_NUM * offset;
        cmd = DRV_IOW(EpeHdrProcPortBaseCtl_t, EpeHdrProcPortBaseCtl_localPhyPortBase_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset ,cmd, &val));

        cmd = DRV_IOW(EpePortExtenderDownlink_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, offset, cmd, &epe_port_extender_downlink));
    }

    CTC_ERROR_RETURN(sys_goldengate_queue_get_enqueue_info(lchip, &p_gg_bpe_master[lchip]->max_extend_num, &p_gg_bpe_master[lchip]->enq_mode));
    if (SYS_BPE_FULL_EXTEND_MODE(lchip))
    {
        p_gg_bpe_master[lchip]->port_attr = (sys_bpe_port_attr_t*)mem_malloc(MEM_PORT_MODULE, sizeof(sys_bpe_port_attr_t)*SYS_GG_MAX_PORT_NUM_PER_CHIP);
        if (NULL == p_gg_bpe_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
        {
            CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
            CTC_ERROR_RETURN(_sys_goldengate_bpe_update_datapath_original_mapping(lchip, lport, port_attr));
            if (SYS_DATAPATH_NETWORK_PORT == port_attr->port_type)
            {
                port_attr->port_type = SYS_DATAPATH_RSV_PORT;
                CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, lport, SYS_DROP_CHANNEL_ID));
            }

            /*delete bridge nexthop*/
            sys_goldengate_get_gchip_id(lchip, &gchip);
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
            ret = sys_goldengate_brguc_nh_delete(lchip, gport);
            if ((CTC_E_NONE != ret) && (CTC_E_NH_NOT_EXIST != ret))
            {
                return ret;
            }
        }

        for (serdes_id = 0; serdes_id < SYS_GG_DATAPATH_SERDES_NUM; serdes_id++)
        {
            ret = sys_goldengate_datapath_get_serdes_info(lchip, serdes_id, &p_serdes);
            if(ret < 0)
            {
                continue;
            }
            p_serdes->lport = 0xFFFF;
        }

        CTC_ERROR_RETURN(sys_goldengate_datapath_cb_register(lchip, _sys_goldengate_bpe_update_datapath_original_mapping));
    }
    CTC_ERROR_RETURN(sys_goldengate_internal_port_set_alloc_mode(lchip, SYS_BPE_FULL_EXTEND_MODE(lchip)));

    return CTC_E_NONE;
}

int32
sys_goldengate_bpe_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_bpe_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free bpe port attr*/
    if (SYS_BPE_FULL_EXTEND_MODE(lchip))
    {
        mem_free(p_gg_bpe_master[lchip]->port_attr);
    }

    mem_free(p_gg_bpe_master[lchip]);

    return CTC_E_NONE;
}


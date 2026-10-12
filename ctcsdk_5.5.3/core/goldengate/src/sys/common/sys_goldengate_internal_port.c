/**
 @file sys_goldengate_internal_port.c

 @date 2010-03-29

 @version v2.0

*/

/****************************************************************************
 *  current queue allocation scheme (Alt + F12 for pretty):
 *
 *           type          port id    channel id      queue number    queue id range
 *     ----------------  ----------   ----------      ------------    --------------
 *      internal port     64 - 191       X              4 * 128        708 - 1219

 ****************************************************************************/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_debug.h"
#include "ctc_macro.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "ctc_const.h"
#include "ctc_warmboot.h"

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_drop.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_bpe.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_io.h"

/* #include "drv_tbl_reg.h" --never--*/

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/
sys_gg_inter_port_master_t* gg_inter_port_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_INTERNAL_PORT_INIT_CHECK() \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == gg_inter_port_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_INTERNAL_PORT_CREATE_LOCK(lchip)                    \
    do                                                          \
    {                                                           \
        sal_mutex_create(&gg_inter_port_master[lchip]->mutex);  \
        if (NULL == gg_inter_port_master[lchip]->mutex)         \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_INTERNAL_PORT_LOCK(lchip) \
    sal_mutex_lock(gg_inter_port_master[lchip]->mutex)

#define SYS_INTERNAL_PORT_UNLOCK(lchip) \
    sal_mutex_unlock(gg_inter_port_master[lchip]->mutex)

#define CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(gg_inter_port_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

enum sys_goldengate_internal_port_op_type_e
{
    SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_SET = 1,
    SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_FREE,
    SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_ALLOC,
    SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_NUM = SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_ALLOC
};
typedef enum sys_goldengate_internal_port_op_type_e sys_goldengate_internal_port_op_type_t;

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/
#define __________INTERNAL_FUNCTION__________

extern int32
sys_goldengate_internal_port_wb_restore(uint8 lchip);

STATIC int32
_sys_goldengate_internal_port_set_channel(uint8 lchip, uint16 lport, ctc_internal_port_type_t type, uint16 channel, uint8 is_bind)
{
    uint16 chan_id = 0;
    uint16 drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);

    if (CTC_INTERNAL_PORT_TYPE_ILOOP == type)
    {
        chan_id = SYS_ILOOP_CHANNEL_ID;
    }
    else if (CTC_INTERNAL_PORT_TYPE_ELOOP == type)
    {

       chan_id = channel;

    }
    else if (CTC_INTERNAL_PORT_TYPE_FWD == type)
    {
        chan_id = channel;
    }
    else
    {
        /* CTC_INTERNAL_PORT_TYPE_DISCARD */
        chan_id = SYS_DROP_CHANNEL_ID;
    }

    if (is_bind)
    {
        CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, drv_lport, chan_id));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel(lchip, drv_lport, chan_id));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_port_bitmap(uint8 lchip, ctc_internal_port_assign_para_t* p_port_assign, uint8 op_type)
{

    uint8  igs_set = 0;
    uint8  egs_set = 0;
    uint16 lport = 0;
    uint16 start_port = 0;
    uint16 end_port = 0;
    uint8 slice_id = 0;
    uint8 enq_mode = 0;
    uint16 max_network_port_num = 0;

    if (SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_SET == op_type)
    {
        igs_set = CTC_IS_BIT_SET(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][p_port_assign->inter_port / BITS_NUM_OF_WORD], p_port_assign->inter_port % BITS_NUM_OF_WORD)
                  && ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type));
        egs_set = CTC_IS_BIT_SET(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][p_port_assign->inter_port / BITS_NUM_OF_WORD], p_port_assign->inter_port % BITS_NUM_OF_WORD)
                  && ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type));

        if (igs_set || egs_set)
        {
            return CTC_E_QUEUE_INTERNAL_PORT_IN_USE;
        }

        if ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][p_port_assign->inter_port / BITS_NUM_OF_WORD] |= (1 << (p_port_assign->inter_port % BITS_NUM_OF_WORD));
        }

        if ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][p_port_assign->inter_port / BITS_NUM_OF_WORD] |= (1 << (p_port_assign->inter_port % BITS_NUM_OF_WORD));
        }

    }
    else if (SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_FREE == op_type)
    {
        if((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][p_port_assign->inter_port / BITS_NUM_OF_WORD] &= ~(1 << (p_port_assign->inter_port % BITS_NUM_OF_WORD));
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][p_port_assign->inter_port / BITS_NUM_OF_WORD] &= ~(1 << (p_port_assign->inter_port % BITS_NUM_OF_WORD));
        }
    }
    else if (SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_ALLOC == op_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_queue_get_enqueue_info(lchip, &max_network_port_num, &enq_mode));

        /*
            fwd/efm: (max_network_port_num-1) -->  48
            eloop: 247 --> max_network_port_num
            iloop: 247 --> 48
        */
        if (CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)
        {
            start_port = max_network_port_num - 1;
            end_port = 47;
        }
        else if (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type)
        {
            start_port = SYS_RSV_PORT_BASE - 1;
            end_port = max_network_port_num - 1;
        }
        else if (CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type)
        {
            start_port = SYS_RSV_PORT_BASE - 1;
            end_port = 47;
        }

        /* alloc slice 0/1 iloop/eloop/fwd internal port */
        if ((CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type))
        {
            CTC_MAX_VALUE_CHECK(p_port_assign->slice_id, 1);
            slice_id = p_port_assign->slice_id;
        }
        else
        {
            SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_port_assign->fwd_gport, lchip, lport);

            if ((lport & 0xFF) >= SYS_PHY_PORT_NUM_PER_SLICE)
            {
                return CTC_E_INVALID_PARAM;
            }

            slice_id = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
        }

        start_port += SYS_CHIP_PER_SLICE_PORT_NUM * slice_id;
        end_port += SYS_CHIP_PER_SLICE_PORT_NUM * slice_id;

        for (lport = start_port; lport != end_port; lport--)
        {
            igs_set = CTC_IS_BIT_SET(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) / BITS_NUM_OF_WORD], SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) % BITS_NUM_OF_WORD)
                      && ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type));
            egs_set = CTC_IS_BIT_SET(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) / BITS_NUM_OF_WORD], SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) % BITS_NUM_OF_WORD)
                      && ((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type));

            if (igs_set ||egs_set)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        if (end_port == lport)
        {
            return CTC_E_QUEUE_NO_FREE_INTERNAL_PORT;
        }

        p_port_assign->inter_port = SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport);

        if((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) / BITS_NUM_OF_WORD] |= (1 << (SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) % BITS_NUM_OF_WORD));
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type))
        {
            gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) / BITS_NUM_OF_WORD] |= (1 << (SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) % BITS_NUM_OF_WORD));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_loop_port(uint8 lchip, uint16 lport, uint32 nhid, uint8 is_add)
{
    DsDestPortLoopback_m ds_dest_port_loopback;
    uint8 index = 0;
    uint32 cmd  = 0;
    uint32 field_val = 0;
    uint8 gchip = 0;

    if (is_add)
    {
        sys_nh_info_dsnh_t sys_nh_info_dsnh;
        sal_memset(&sys_nh_info_dsnh, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nhid, &sys_nh_info_dsnh));

        /*get loopback index*/
        for (index = 0; index < TABLE_MAX_INDEX(DsDestPortLoopback_t); index++)
        {
            cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
            sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));

            if ((0 == GetDsDestPortLoopback(V, lbDestMap_f, &ds_dest_port_loopback))
               && (0 == GetDsDestPortLoopback(V, lbNextHopPtr_f, &ds_dest_port_loopback)))
            {
                break;
            }
        }

        if (TABLE_MAX_INDEX(DsDestPortLoopback_t) == index)
        {
            return CTC_E_NO_RESOURCE;
        }

        field_val = BPE_EGS_MUXTYPE_LOOPBACK_ENCODE;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_val));

        field_val = index;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_portLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_val));

        sal_memset(&ds_dest_port_loopback, 0, sizeof(DsDestPortLoopback_m));

        if ((SYS_RSV_PORT_ILOOP_ID == (sys_nh_info_dsnh.dest_id&0xFF)) || (SYS_RSV_PORT_IP_TUNNEL == (sys_nh_info_dsnh.dest_id&0xFF)))
        {
            sys_goldengate_get_gchip_id(lchip, &gchip);
            SetDsDestPortLoopback(V, lbDestMap_f, &ds_dest_port_loopback, SYS_ENCODE_DESTMAP(gchip, sys_nh_info_dsnh.dest_id));
        }
        else
        {
            SetDsDestPortLoopback(V, lbDestMap_f, &ds_dest_port_loopback, SYS_ENCODE_DESTMAP(sys_nh_info_dsnh.dest_chipid, sys_nh_info_dsnh.dest_id));
        }

        SetDsDestPortLoopback(V, lbNextHopPtr_f, &ds_dest_port_loopback, sys_nh_info_dsnh.dsnh_offset);
        SetDsDestPortLoopback(V, lbNextHopExt_f, &ds_dest_port_loopback, sys_nh_info_dsnh.nexthop_ext);

        cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));
    }
    else
    {
        field_val = BPE_EGS_MUXTYPE_NOMUX;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_muxPortType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_val));

        field_val = 0;
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_portLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_val));

        index = field_val;
        sal_memset(&ds_dest_port_loopback, 0, sizeof(DsDestPortLoopback_m));
        cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));

        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_portLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_val));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_internal_port_ip_tunnel_init(uint8 lchip)
{
    uint8 gchip = 0;
    uint8 index = 0;
    uint32 lport = 0;
    uint32 cmd = 0;
    uint16 interface_id = 0;
    DsSrcInterface_m src_interface;
    DsDestInterface_m dest_interface;
    DsDestPort_m dest_port;
    DsSrcPort_m src_port;
    DsPhyPortExt_m phy_port_ext;
    DsPhyPort_m phy_port;
    DsDestPortLoopback_m ds_dest_port_loopback;

    sys_goldengate_get_gchip_id(lchip, &gchip);
    lport = SYS_RSV_PORT_IP_TUNNEL;
    interface_id = SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT;
    sal_memset(&src_interface, 0, sizeof(src_interface));
    SetDsSrcInterface(V, v4UcastEn_f, &src_interface, 1);
    SetDsSrcInterface(V, v4McastEn_f, &src_interface, 1);
    SetDsSrcInterface(V, v6UcastEn_f, &src_interface, 1);
    SetDsSrcInterface(V, v6McastEn_f, &src_interface, 1);
    SetDsSrcInterface(V, routeAllPackets_f, &src_interface, 1);
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &src_interface));

    sal_memset(&dest_interface, 0, sizeof(dest_interface));
    cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
    SetDsDestInterface(V, mtuExceptionEn_f, &dest_interface, 1);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &dest_interface));

    for (index = 0; index < SYS_MAX_LOCAL_SLICE_NUM; index++)
    {
    lport = lport + index * SYS_CHIP_PER_SLICE_PORT_NUM;
    sal_memset(&dest_port, 0, sizeof(dest_port));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));
    SetDsDestPort(V, dot1QEn_f, &dest_port, 0);
    SetDsDestPort(V, defaultVlanId_f, &dest_port, 0);
    SetDsDestPort(V, muxPortType_f, &dest_port, 3);
    SetDsDestPort(V, portLoopbackIndex_f, &dest_port, index);
    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));

    sal_memset(&src_port, 0, sizeof(src_port));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &src_port));
    SetDsSrcPort(V, interfaceId_f, &src_port, interface_id);
    SetDsSrcPort(V, routedPort_f, &src_port, 1);
    SetDsSrcPort(V, addDefaultVlanDisable_f, &src_port, 1);
    cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &src_port));

    sal_memset(&phy_port_ext, 0, sizeof(phy_port_ext));
    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
    SetDsPhyPortExt(V, defaultVlanId_f, &phy_port_ext, 0);
    SetDsPhyPortExt(V, qosPolicy_f, &phy_port_ext, 1);
    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

    sal_memset(&phy_port, 0, sizeof(phy_port));
    SetDsPhyPort(V, fwdHashGenDis_f, &phy_port, 1);
    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));

    /*enable eloop for port*/
    sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
    cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback)); /* LoopbackEncode */
    SetDsDestPortLoopback(V, lbDestMap_f, &ds_dest_port_loopback,SYS_ENCODE_DESTMAP(gchip,SYS_RSV_PORT_ILOOP_ID + index * SYS_CHIP_PER_SLICE_PORT_NUM));
    SetDsDestPortLoopback(V, lbNextHopPtr_f, &ds_dest_port_loopback, ((((lport & 0x80)>>7) << 16) | (lport & 0x7F)));
    cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));
    }

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_internal_port_queue_drop_init(uint8 lchip)
{
    uint32 cmd = 0;
	uint8 lport = 0;
    DsPhyPortExt_m phy_port_ext;

	lport = SYS_RSV_PORT_IPE_QUE_DROP;
    sal_memset(&phy_port_ext, 0, sizeof(phy_port_ext));
    SetDsPhyPortExt(V, enqDropSpanDestEn_f, &phy_port_ext, 1);
    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
	CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport + 256, cmd, &phy_port_ext));

	 return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_internal_port_gb_gg_interconnect_chip_init(uint8 lchip)
{
    uint16 lport = 0;
    ctc_internal_port_assign_para_t port_assign;

    sal_memset(&port_assign, 0, sizeof(port_assign));

    if (sys_goldengate_get_gb_gg_interconnect_en(lchip))
    {
        port_assign.type = CTC_INTERNAL_PORT_TYPE_FWD;
        for (lport=64; lport<112; lport++)
        {
            CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, lport, lport));
            port_assign.inter_port = lport;
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_bitmap(lchip, &port_assign, SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_SET));
        }
        for (lport=320; lport<368; lport++)
        {
            port_assign.inter_port = lport;
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_bitmap(lchip, &port_assign, SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_SET));
        }
    }

    return CTC_E_NONE;
}

#define __________EXTERNAL_FUNCTION___________

int32
sys_goldengate_internal_port_set_iloop_for_pkt_to_cpu(uint8 lchip, uint32 gport, uint8 enable)
{
    uint8 slice = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint16 lport = 0;

    SYS_INTERNAL_PORT_INIT_CHECK();
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    slice = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
    tbl_id = slice? IpeForwardReserved1_t : IpeForwardReserved0_t;

    cmd = DRV_IOR(tbl_id, IpeForwardReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    if (enable)
    {
        CTC_BIT_SET(value, 13);
    }
    else
    {
        CTC_BIT_UNSET(value, 13);
    }
    value |= 0x3 << 11;
    value &= 0xFFFFFF00;
    value |= lport&0xFF;
    cmd = DRV_IOW(tbl_id, IpeForwardReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

/**
 @brief Set internal port for special usage, for example, I-Loop, E-Loop.
*/
int32
sys_goldengate_internal_port_set_alloc_mode(uint8 lchip, uint8 alloc_mode)
{
    SYS_INTERNAL_PORT_LOCK(lchip);
    if (gg_inter_port_master[lchip])
    {
        gg_inter_port_master[lchip]->alloc_mode = alloc_mode;
    }
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_internal_port_set(uint8 lchip, ctc_internal_port_assign_para_t* p_port_assign)
{
    uint16 dst_drv_lport = 0;
    uint16 internal_drv_lport = 0;
    uint16 chan_id = 0;
    uint16 internal_port_start = 0;
    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();

    CTC_PTR_VALID_CHECK(p_port_assign);
    CTC_MAX_VALUE_CHECK(p_port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);

    internal_drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_port_assign->inter_port);

    SYS_INTERNAL_PORT_LOCK(lchip);
    internal_port_start = gg_inter_port_master[lchip]->alloc_mode? 0 : SYS_INTERNAL_PORT_START;
    if (((internal_drv_lport&0xFF) < internal_port_start) || ((internal_drv_lport&0xFF) > (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1)))
    {
        SYS_INTERNAL_PORT_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(_sys_goldengate_port_set_port_bitmap(lchip, p_port_assign, SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_SET));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    if (p_port_assign->type == CTC_INTERNAL_PORT_TYPE_FWD)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_port_assign->fwd_gport, lchip, dst_drv_lport);
        if ((dst_drv_lport & 0xFF) >= SYS_PHY_PORT_NUM_PER_SLICE)
        {
            return CTC_E_INVALID_PARAM;
        }

        chan_id = SYS_GET_CHANNEL_ID(lchip, p_port_assign->fwd_gport);
    }
    else if (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_port_set_loop_port(lchip, p_port_assign->inter_port, p_port_assign->nhid, 1));
        chan_id = SYS_ELOOP_CHANNEL_ID + SYS_MAP_DRV_LPORT_TO_SLICE(internal_drv_lport) * SYS_MAX_CHANNEL_ID;
    }

    if (p_port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(_sys_goldengate_internal_port_set_channel(lchip, p_port_assign->inter_port, p_port_assign->type, chan_id, 1));
    }

    return CTC_E_NONE;
}

/**
 @brief Allocate internal port for special usage, for example, I-Loop, E-Loop.
*/
int32
sys_goldengate_internal_port_allocate(uint8 lchip, ctc_internal_port_assign_para_t* p_port_assign)
{

    uint8 chan_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 internal_drv_lport = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_port_assign);
    CTC_MAX_VALUE_CHECK(p_port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);

    SYS_INTERNAL_PORT_LOCK(lchip);
    /* alloc discard port just return reserve drop port */
    if (CTC_INTERNAL_PORT_TYPE_DISCARD == p_port_assign->type)
    {
        p_port_assign->inter_port = gg_inter_port_master[lchip]->inter_port[SYS_INTERNAL_PORT_TYPE_DROP];
        SYS_INTERNAL_PORT_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    /*alloc a free internal port*/
    CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(_sys_goldengate_port_set_port_bitmap(lchip, p_port_assign, SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_ALLOC));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    internal_drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_port_assign->inter_port);

    if (CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)  /* alloc fwd port just for internal use of efm */
    {
        /* cfg src port disable stp */
        field_val = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));

        /* cfg dest port disable stp */
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_stpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));

        chan_id = SYS_GET_CHANNEL_ID(lchip, p_port_assign->fwd_gport);
    }
    else if (CTC_INTERNAL_PORT_TYPE_ELOOP == p_port_assign->type)
    {
        chan_id = SYS_ELOOP_CHANNEL_ID + SYS_MAX_CHANNEL_ID * p_port_assign->slice_id;
        CTC_ERROR_RETURN(_sys_goldengate_port_set_loop_port(lchip, p_port_assign->inter_port, p_port_assign->nhid, 1));

        /* cfg dest port disable stp */
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_stpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));
    }
    else if (CTC_INTERNAL_PORT_TYPE_ILOOP == p_port_assign->type)
    {
        /* cfg src port disable stp */
        field_val = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));
    }


    if (p_port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(_sys_goldengate_internal_port_set_channel(lchip, p_port_assign->inter_port, p_port_assign->type, chan_id, 1));
    }

    return CTC_E_NONE;
}

/**
 @brief Release internal port.
*/
int32
sys_goldengate_internal_port_release(uint8 lchip, ctc_internal_port_assign_para_t* p_port_assign)
{

    uint8 chan_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 internal_drv_lport = 0;
    uint16 internal_port_start = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_port_assign);
    CTC_MAX_VALUE_CHECK(p_port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);
    CTC_MAX_VALUE_CHECK(p_port_assign->inter_port, SYS_GG_MAX_PORT_NUM_PER_CHIP-1);

    SYS_INTERNAL_PORT_LOCK(lchip);
    internal_drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_port_assign->inter_port);

    internal_port_start = gg_inter_port_master[lchip]->alloc_mode? 0 : SYS_INTERNAL_PORT_START;
    SYS_INTERNAL_PORT_UNLOCK(lchip);
    CTC_VALUE_RANGE_CHECK((internal_drv_lport&0xFF), internal_port_start, (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1));

    if (CTC_INTERNAL_PORT_TYPE_FWD == p_port_assign->type)
    {
        field_val = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));

        field_val = 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_stpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));
        chan_id = SYS_DROP_CHANNEL_ID;
    }

    if (p_port_assign->type == CTC_INTERNAL_PORT_TYPE_ELOOP)
    {
        CTC_ERROR_RETURN(_sys_goldengate_port_set_loop_port(lchip,  p_port_assign->inter_port, p_port_assign->nhid, 0));
        chan_id = SYS_ELOOP_CHANNEL_ID + SYS_MAP_DRV_LPORT_TO_SLICE(internal_drv_lport) * SYS_MAX_CHANNEL_ID;

        field_val = 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_stpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));
    }

    if (p_port_assign->type == CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        field_val = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, internal_drv_lport, cmd, &field_val));
    }

    SYS_INTERNAL_PORT_LOCK(lchip);
    CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(_sys_goldengate_port_set_port_bitmap(lchip, p_port_assign, SYS_GOLDENGATE_INTERNAL_PORT_OP_TYPE_FREE));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    if (p_port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(_sys_goldengate_internal_port_set_channel(lchip, p_port_assign->inter_port, p_port_assign->type, chan_id, 0));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_internal_port_get_rsv_port(uint8 lchip, sys_internal_port_type_t type, uint16* lport)
{
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(lport);

    SYS_INTERNAL_PORT_LOCK(lchip);
    *lport = gg_inter_port_master[lchip]->inter_port[type];
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_internal_port_set_overlay_property(uint8 lchip, uint16 lport)
{

    uint32 value = 0;
    uint8 gchip = 0;
    uint32 gport = 0;
    SYS_INTERNAL_PORT_INIT_CHECK();

    value = 0;
    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_HASH_GEN_DIS, value));
    value = 1;
    CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_NVGRE_ENABLE, value));

    return CTC_E_NONE;
}

/**
 @brief Internal port initialization.
*/

int32
sys_goldengate_internal_port_init(uint8 lchip)
{
    uint8 port_type = 0;
    uint8 chan_id = 0;
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;

    if (NULL != gg_inter_port_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_POINTER(sys_gg_inter_port_master_t, gg_inter_port_master[lchip]);
    if (NULL == gg_inter_port_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gg_inter_port_master[lchip], 0, sizeof(sys_gg_inter_port_master_t));


    for (port_type = SYS_INTERNAL_PORT_TYPE_MIN; port_type < SYS_INTERNAL_PORT_TYPE_MAX; port_type++)
    {
        switch(port_type)
        {
        case SYS_INTERNAL_PORT_TYPE_ILOOP:
            gg_inter_port_master[lchip]->inter_port[port_type]   = SYS_RSV_PORT_ILOOP_ID;
            chan_id = SYS_ILOOP_CHANNEL_ID;
            break;
        case SYS_INTERNAL_PORT_TYPE_ELOOP:
            gg_inter_port_master[lchip]->inter_port[port_type]   = SYS_RSV_PORT_ELOOP_ID;
            chan_id = SYS_ELOOP_CHANNEL_ID;
            break;
        case SYS_INTERNAL_PORT_TYPE_IP_TUNNEL:
            gg_inter_port_master[lchip]->inter_port[port_type]   = SYS_RSV_PORT_IP_TUNNEL;
              chan_id = SYS_ELOOP_CHANNEL_ID;
            break;
         case SYS_INTERNAL_PORT_TYPE_MIRROR:
            gg_inter_port_master[lchip]->inter_port[port_type]   = SYS_RSV_PORT_MIRROR;
             chan_id = SYS_ILOOP_CHANNEL_ID;
            break;
         case SYS_INTERNAL_PORT_TYPE_DROP:
            gg_inter_port_master[lchip]->inter_port[port_type]   = SYS_RSV_PORT_DROP_ID;
             chan_id = SYS_DROP_CHANNEL_ID;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
        lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(gg_inter_port_master[lchip]->inter_port[port_type]);
        CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, lport, chan_id));
        lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(gg_inter_port_master[lchip]->inter_port[port_type]) + SYS_CHIP_PER_SLICE_PORT_NUM;
        CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, lport, (chan_id + 64)));
    }

    CTC_ERROR_RETURN(_sys_goldengate_internal_port_ip_tunnel_init(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_internal_port_queue_drop_init(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_internal_port_gb_gg_interconnect_chip_init(lchip));

     /*iloop taking the original source port for IP-TUNNEL and CoPP*/
    CTC_BIT_SET(value, 13);
    CTC_BIT_SET(value, 12);
    value |= SYS_RSV_PORT_IP_TUNNEL;
    cmd = DRV_IOW(IpeForwardReserved0_t, IpeForwardReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    cmd = DRV_IOW(IpeForwardReserved1_t, IpeForwardReserved1_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    cmd = DRV_IOW(IpeForwardReserved2_t, IpeForwardReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));


    SYS_INTERNAL_PORT_CREATE_LOCK(lchip);
    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_internal_port_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_internal_port_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == gg_inter_port_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(gg_inter_port_master[lchip]->mutex);
    mem_free(gg_inter_port_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_internal_port_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_interport_master_t  *p_wb_inter_port_master;

    /*syncup  internal port matser*/
    wb_data.buffer = mem_malloc(MEM_PORT_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_interport_master_t, CTC_FEATURE_PORT, SYS_WB_APPID_INTERPORT_SUBID_MASTER);

    p_wb_inter_port_master = (sys_wb_interport_master_t  *)wb_data.buffer;

    p_wb_inter_port_master->lchip = lchip;

    sal_memcpy(p_wb_inter_port_master->is_used_igs, gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS],  SYS_GG_MAX_PORT_NUM_PER_CHIP / 8);
    sal_memcpy(p_wb_inter_port_master->is_used_egs, gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS],  SYS_GG_MAX_PORT_NUM_PER_CHIP / 8);
    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_internal_port_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_interport_master_t  *p_wb_inter_port_master;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_PORT_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_interport_master_t, CTC_FEATURE_PORT, SYS_WB_APPID_INTERPORT_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore internal port master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query internal port master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_inter_port_master = (sys_wb_interport_master_t *)wb_query.buffer;

    sal_memcpy(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS], p_wb_inter_port_master->is_used_igs, SYS_GG_MAX_PORT_NUM_PER_CHIP / 8);
    sal_memcpy(gg_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS], p_wb_inter_port_master->is_used_egs, SYS_GG_MAX_PORT_NUM_PER_CHIP / 8);

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}


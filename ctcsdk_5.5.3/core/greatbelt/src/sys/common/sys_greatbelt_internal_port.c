/**
 @file sys_greatbelt_internal_port.c

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

#include "sys_greatbelt_common.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_drop.h"
#include "sys_greatbelt_queue_shape.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_nexthop_api.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"


/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/
sys_gb_inter_port_master_t* gb_inter_port_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

extern sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM];

#define SYS_INTERNAL_PORT_INIT_CHECK() \
    do { \
        if (gb_inter_port_master[lchip] == NULL){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_INTERNAL_PORT_CREATE_LOCK(lchip)                    \
    do                                                          \
    {                                                           \
        sal_mutex_create(&gb_inter_port_master[lchip]->mutex);  \
        if (NULL == gb_inter_port_master[lchip]->mutex)         \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_INTERNAL_PORT_LOCK(lchip) \
    sal_mutex_lock(gb_inter_port_master[lchip]->mutex)

#define SYS_INTERNAL_PORT_UNLOCK(lchip) \
    sal_mutex_unlock(gb_inter_port_master[lchip]->mutex)

#define CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(gb_inter_port_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_SET     1
#define SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_FREE    2
#define SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_ALLOC   3

/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/

int32
_sys_greatbelt_internal_port_set_queue(uint8 lchip, uint8 inter_port, uint8 type, uint8 channel)
{
   uint8 gchip = 0;
   uint8 chan_id = 0;

   if (type == CTC_INTERNAL_PORT_TYPE_ILOOP)
   {
       chan_id = SYS_ILOOP_CHANNEL_ID;
   }
   else if (type == CTC_INTERNAL_PORT_TYPE_ELOOP)
   {
       chan_id = SYS_ELOOP_CHANNEL_ID;
   }
   else if (type == CTC_INTERNAL_PORT_TYPE_FWD)
   {
       chan_id =  channel;
   }
   else
   {
       chan_id = 0;
   }

    CTC_ERROR_RETURN(sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_INTERNAL, inter_port, chan_id));

    if (type == CTC_INTERNAL_PORT_TYPE_DISCARD)
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        CTC_ERROR_RETURN(sys_greatbelt_queue_set_port_drop_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, inter_port),
                                                              TRUE, SYS_QUEUE_DROP_TYPE_ALL));
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_port_set_port_bitmap(uint8 lchip, ctc_internal_port_assign_para_t* port_assign, uint8 op_type)
{
    uint8 i = 0;
    uint8 igs_set = 0;
    uint8 egs_set = 0;


    if (SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_SET == op_type)
    {/*set*/

        igs_set = CTC_IS_BIT_SET(gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][port_assign->inter_port / BITS_NUM_OF_WORD], port_assign->inter_port % BITS_NUM_OF_WORD)
                    && ((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ILOOP == port_assign->type));
        egs_set = CTC_IS_BIT_SET(gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][port_assign->inter_port / BITS_NUM_OF_WORD], port_assign->inter_port % BITS_NUM_OF_WORD)
                    && ((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ELOOP == port_assign->type));

        if(igs_set || egs_set)
        {
            return CTC_E_QUEUE_INTERNAL_PORT_IN_USE;
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][port_assign->inter_port / BITS_NUM_OF_WORD] |= (1 << (port_assign->inter_port % BITS_NUM_OF_WORD));
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][port_assign->inter_port / BITS_NUM_OF_WORD] |= (1 << (port_assign->inter_port % BITS_NUM_OF_WORD));
        }

    }
    else if (SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_FREE == op_type)
    {/*free*/
        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][port_assign->inter_port / BITS_NUM_OF_WORD] &= ~(1 << (port_assign->inter_port % BITS_NUM_OF_WORD));
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][port_assign->inter_port / BITS_NUM_OF_WORD] &= ~(1 << (port_assign->inter_port % BITS_NUM_OF_WORD));
        }
    }
    else if (SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_ALLOC == op_type)
    {/*alloc*/
        for (i = SYS_INTERNAL_PORT_START; i < (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM); i++)
        {
            igs_set = CTC_IS_BIT_SET(gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][i / BITS_NUM_OF_WORD], i % BITS_NUM_OF_WORD)
                        && ((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ILOOP == port_assign->type));
            egs_set = CTC_IS_BIT_SET(gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][i / BITS_NUM_OF_WORD], i % BITS_NUM_OF_WORD)
                        && ((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type)||(CTC_INTERNAL_PORT_TYPE_ELOOP == port_assign->type));

            if (igs_set ||egs_set)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        if (i == SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM)
        {
            return CTC_E_QUEUE_NO_FREE_INTERNAL_PORT;
        }

        port_assign->inter_port = i;

        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ILOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_IGS][i / BITS_NUM_OF_WORD] |= (1 << (i % BITS_NUM_OF_WORD));
        }

        if((CTC_INTERNAL_PORT_TYPE_FWD == port_assign->type) || (CTC_INTERNAL_PORT_TYPE_ELOOP == port_assign->type))
        {
            gb_inter_port_master[lchip]->is_used[SYS_INTERNAL_PORT_DIR_EGS][i / BITS_NUM_OF_WORD] |= (1 << (i % BITS_NUM_OF_WORD));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_loop_port(uint8 lchip, uint8 inter_port, uint32 nhid, uint8 b_add)
{
    ds_dest_port_loopback_t ds_dest_port_loopback;
    uint8 index = 0;
    uint32 cmd  = 0;
    uint32 field_val = 0;
    uint8 gchip = 0;

    if (0 == nhid)
    {
        return CTC_E_NONE;
    }

    if(b_add)
    {
        sys_nh_info_dsnh_t sys_nh_info_dsnh;
        /*get loopback index*/
        for(index = 0; index < 8; index++)
        {
            cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
            sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));
            if((0 == ds_dest_port_loopback.lb_dest_map) &&
                (0 == ds_dest_port_loopback.lb_next_hop_ptr))
            {
                break;
            }
        }

        if(8 == index)
        {
            return CTC_E_NO_RESOURCE;
        }

        /*enable eloop for port*/
        field_val = 3;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,  inter_port, cmd, &field_val));

        field_val = index;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,  inter_port, cmd, &field_val));

        sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
        sal_memset(&sys_nh_info_dsnh, 0, sizeof(sys_nh_info_dsnh_t));

        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, nhid, &sys_nh_info_dsnh));

        if(SYS_IS_LOOP_PORT(sys_nh_info_dsnh.dest_id))
        {
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            ds_dest_port_loopback.lb_dest_map = ((gchip << 16) | (sys_nh_info_dsnh.dest_id));
        }
        else
        {
            ds_dest_port_loopback.lb_dest_map = (((sys_nh_info_dsnh.dest_chipid) << 16) | (sys_nh_info_dsnh.dest_id));
        }
        ds_dest_port_loopback.lb_next_hop_ptr = sys_nh_info_dsnh.dsnh_offset;
        ds_dest_port_loopback.lb_next_hop_ext = sys_nh_info_dsnh.nexthop_ext;
        cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));
    }
    else
    {
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,  inter_port, cmd, &field_val));

        field_val = 0;
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_PortLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,  inter_port, cmd, &field_val));

        index = field_val;
        sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
        cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port_loopback));

        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortLoopbackIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip,  inter_port, cmd, &field_val));
    }

    return CTC_E_NONE;
}


/**
 @brief Set internal port for special usage, for example, I-Loop, E-Loop.
*/
int32
sys_greatbelt_internal_port_set(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{
    uint8 chan_id = 0;
    uint8 lport = 0;

    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();

    /*param judge*/
    CTC_PTR_VALID_CHECK(port_assign);
    CTC_MAX_VALUE_CHECK(port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);


    CTC_VALUE_RANGE_CHECK(port_assign->inter_port, SYS_INTERNAL_PORT_START, (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1));

    SYS_INTERNAL_PORT_LOCK(lchip);
    /*judge if internal port is used*/
    CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(_sys_greatbelt_port_set_port_bitmap(lchip, port_assign, SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_SET));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    if (port_assign->type == CTC_INTERNAL_PORT_TYPE_FWD)
    {
        SYS_MAP_GPORT_TO_LPORT(port_assign->fwd_gport, lchip, lport);

        if (lport == SYS_RESERVE_PORT_ID_INTLK)
        {
            chan_id = SYS_INTLK_CHANNEL_ID;
        }
        else
        {
            if ((lport >= SYS_GB_MAX_PHY_PORT))
            {
                return CTC_E_INVALID_PARAM;
            }

           chan_id = SYS_GET_CHANNEL_ID(lchip, port_assign->fwd_gport);
        }

    }
    else if (port_assign->type == CTC_INTERNAL_PORT_TYPE_ELOOP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_loop_port(lchip,  port_assign->inter_port, port_assign->nhid, 1));
        chan_id = SYS_ELOOP_CHANNEL_ID;
    }

    if (port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_internal_port_set_queue(lchip, port_assign->inter_port, port_assign->type, chan_id));
    }

    return CTC_E_NONE;
}

/**
 @brief Allocate internal port for special usage, for example, I-Loop, E-Loop.
*/
int32
sys_greatbelt_internal_port_allocate(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{
    uint8 chan_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 lport = 0;


    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(port_assign);
    CTC_MAX_VALUE_CHECK(port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);

    SYS_INTERNAL_PORT_LOCK(lchip);
    /*alloc a free internal port*/
    CTC_ERROR_RETURN_INTERNAL_PORT_UNLOCK(_sys_greatbelt_port_set_port_bitmap(lchip, port_assign, SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_ALLOC));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    if (port_assign->type == CTC_INTERNAL_PORT_TYPE_FWD)
    {
        SYS_MAP_GPORT_TO_LPORT(port_assign->fwd_gport, lchip, lport);

        if (((lport >= SYS_GB_MAX_PHY_PORT)&& (lport != SYS_RESERVE_PORT_ID_ELOOP)) )
        {
            return CTC_E_INVALID_PARAM;
        }

        /* cfg src port disable stp */
        field_val = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_assign->inter_port, cmd, &field_val));

        /* cfg dest port disable stp */
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_assign->inter_port, cmd, &field_val));

        chan_id = SYS_GET_CHANNEL_ID(lchip, port_assign->fwd_gport);
    }
    else if (port_assign->type == CTC_INTERNAL_PORT_TYPE_ELOOP)
    {
        chan_id = SYS_ELOOP_CHANNEL_ID;
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_loop_port(lchip,  port_assign->inter_port, port_assign->nhid, 1));

        /* cfg dest port disable stp */
        field_val = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_assign->inter_port, cmd, &field_val));
    }
    else if (port_assign->type == CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        /* cfg src port disable stp */
        field_val = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_assign->inter_port, cmd, &field_val));
    }


    if (port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_internal_port_set_queue(lchip, port_assign->inter_port, port_assign->type, chan_id));
    }

    return CTC_E_NONE;
}

/**
 @brief Release internal port.
*/
int32
sys_greatbelt_internal_port_release(uint8 lchip, ctc_internal_port_assign_para_t* port_assign)
{

    SYS_QUEUE_DBG_FUNC();
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(port_assign);
    CTC_MAX_VALUE_CHECK(port_assign->type, CTC_INTERNAL_PORT_TYPE_FWD);


    CTC_VALUE_RANGE_CHECK(port_assign->inter_port, SYS_INTERNAL_PORT_START, (SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM - 1));

    if (port_assign->type == CTC_INTERNAL_PORT_TYPE_ELOOP)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_loop_port(lchip,  port_assign->inter_port, port_assign->nhid, 0));
    }

    SYS_INTERNAL_PORT_LOCK(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_port_set_port_bitmap(lchip, port_assign, SYS_GREATBELT_INTERNAL_PORT_OP_TYPE_FREE));
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    if (port_assign->type != CTC_INTERNAL_PORT_TYPE_ILOOP)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_queue_remove(lchip, SYS_QUEUE_TYPE_INTERNAL, port_assign->inter_port));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_internal_port_ip_tunnel_init(uint8 lchip)
{
    uint8 gchip = 0;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 interface_id = 0;
    ds_src_interface_t src_interface;
    ds_dest_interface_t dest_interface;
    ds_dest_port_loopback_t ds_dest_port_loopback;

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_IP_TUNNEL, &lport));
    interface_id = SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT;
    sal_memset(&src_interface, 0, sizeof(src_interface));
    src_interface.v4_ucast_en = 1;
    src_interface.v4_mcast_en = 1;
    src_interface.v6_ucast_en = 1;
    src_interface.v6_mcast_en = 1;
    src_interface.route_all_packets = 1;
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &src_interface));

    sal_memset(&dest_interface, 0, sizeof(ds_dest_interface_t));
    dest_interface.mtu_exception_en = 1;
    cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &dest_interface));

    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = interface_id;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_InterfaceId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RoutedPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_QosPolicy_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    /*enable eloop for port*/
    field_val = 3;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
    cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_dest_port_loopback)); /* LoopbackEncode */
    ds_dest_port_loopback.lb_dest_map = ((gchip) << 16) | (SYS_RESERVE_PORT_ID_ILOOP);
    ds_dest_port_loopback.lb_next_hop_ptr = lport;
    cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_dest_port_loopback));

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_internal_port_bfd_init(uint8 lchip)
{
    uint8 gchip = 0;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 interface_id = 0;
    ds_src_interface_t src_interface;
    ds_dest_port_loopback_t ds_dest_port_loopback;
    ctc_internal_port_assign_para_t port_assign;
    ctc_chip_device_info_t device_info;

    sal_memset(&port_assign, 0, sizeof(ctc_internal_port_assign_para_t));

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_MPLS_BFD, &lport));

    interface_id = SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT;
    sal_memset(&src_interface, 0, sizeof(src_interface));
    src_interface.v4_ucast_en = 1;
    src_interface.v4_mcast_en = 1;
    src_interface.v6_ucast_en = 1;
    src_interface.v6_mcast_en = 1;
    src_interface.route_all_packets = 1;
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &src_interface));


    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = interface_id;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_InterfaceId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RoutedPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_QosPolicy_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    /*enable eloop for port*/
    field_val = 3;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortLoopbackIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
    cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_dest_port_loopback)); /* LoopbackEncode */
    ds_dest_port_loopback.lb_dest_map = ((gchip) << 16) | (SYS_RESERVE_PORT_ID_ILOOP);
    ds_dest_port_loopback.lb_next_hop_ptr = (0x18<<8)|lport;
    ds_dest_port_loopback.lb_next_hop_ext = 1;
    cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &ds_dest_port_loopback));

    CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_PW_VCCV_BFD, &lport));
    interface_id    = SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT;
    sal_memset(&src_interface, 0, sizeof(src_interface));
    src_interface.v4_ucast_en = 1;
    src_interface.v4_mcast_en = 1;
    src_interface.v6_ucast_en = 1;
    src_interface.v6_mcast_en = 1;
    src_interface.route_all_packets = 1;
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, interface_id, cmd, &src_interface));

    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = interface_id;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_InterfaceId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RoutedPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_QosPolicy_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_StpCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    /*enable eloop for port*/
    field_val = 3;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_MuxPortType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = 2;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortLoopbackIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    sal_memset(&ds_dest_port_loopback, 0, sizeof(ds_dest_port_loopback));
    cmd = DRV_IOR(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_dest_port_loopback)); /* LoopbackEncode */
    ds_dest_port_loopback.lb_dest_map = ((gchip) << 16) | (SYS_RESERVE_PORT_ID_ILOOP);
    ds_dest_port_loopback.lb_next_hop_ptr = (0x1a<<8)|lport;

    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    sys_greatbelt_chip_get_device_info(lchip, &device_info);

    if(1 == device_info.version_id)
    {
        ds_dest_port_loopback.lb_next_hop_ext = 1;
    }
    else
    {
        ds_dest_port_loopback.lb_next_hop_ext = 0;
    }

    cmd = DRV_IOW(DsDestPortLoopback_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &ds_dest_port_loopback));
    return CTC_E_NONE;

}

int32
sys_greatbelt_internal_port_get_rsv_port(uint8 lchip, sys_internal_port_type_t type, uint8* lport)
{
    SYS_INTERNAL_PORT_INIT_CHECK();
    CTC_PTR_VALID_CHECK(lport);

    SYS_INTERNAL_PORT_LOCK(lchip);
    *lport = gb_inter_port_master[lchip]->inter_port[type];
    SYS_INTERNAL_PORT_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
 @brief Internal port initialization.
*/
int32
sys_greatbelt_internal_port_init(uint8 lchip)
{
    uint8 port_type = 0;
    uint8 fwd_type = 0;
    uint8 chan_id = 0;

    if (NULL != gb_inter_port_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_POINTER(sys_gb_inter_port_master_t, gb_inter_port_master[lchip]);
    if (NULL == gb_inter_port_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gb_inter_port_master[lchip], 0, sizeof(sys_gb_inter_port_master_t));

    SYS_INTERNAL_PORT_CREATE_LOCK(lchip);

    for (port_type = SYS_INTERNAL_PORT_TYPE_MIN; port_type < SYS_INTERNAL_PORT_TYPE_MAX; port_type++)
    {
        switch(port_type)
        {
        case SYS_INTERNAL_PORT_TYPE_ILOOP:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_ILOOP;
            fwd_type = CTC_INTERNAL_PORT_TYPE_ILOOP;
            chan_id = SYS_ILOOP_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_CPU:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_CPU;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_CPU_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_CPU_REMOTE:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_CPU_REMOTE;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_CPU_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_CPU_OAM:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_CPU_OAM;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_CPU_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_DMA:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_DMA;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_DMA_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_OAM:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_OAM;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_OAM_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_INLK:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_INTLK;
            fwd_type = CTC_INTERNAL_PORT_TYPE_FWD;
            chan_id = SYS_INTLK_CHANNEL_ID;
            break;

        case SYS_INTERNAL_PORT_TYPE_IP_TUNNEL:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_IP_TUNNEL;
            fwd_type = CTC_INTERNAL_PORT_TYPE_ELOOP;
            break;

        case SYS_INTERNAL_PORT_TYPE_MPLS_BFD:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_MPLS_BFD;
            fwd_type = CTC_INTERNAL_PORT_TYPE_ELOOP;
            break;

        case SYS_INTERNAL_PORT_TYPE_PW_VCCV_BFD:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_PW_VCCV_BFD;
            fwd_type = CTC_INTERNAL_PORT_TYPE_ELOOP;
            break;

         case SYS_INTERNAL_PORT_TYPE_MIRROR:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_MIRROR;
            fwd_type = CTC_INTERNAL_PORT_TYPE_ILOOP;
            break;

        case SYS_INTERNAL_PORT_TYPE_DROP:
            gb_inter_port_master[lchip]->inter_port[port_type]   = SYS_RESERVE_PORT_ID_DROP;
            fwd_type = CTC_INTERNAL_PORT_TYPE_DISCARD;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        /* init drop port in  sys_greatbelt_queue_enq_init instead */
        if (gb_inter_port_master[lchip]->inter_port[port_type]   == SYS_RESERVE_PORT_ID_DROP)
        {
            continue;
        }


         CTC_ERROR_RETURN(_sys_greatbelt_internal_port_set_queue(lchip,
                                                                 gb_inter_port_master[lchip]->inter_port[port_type],
                                                                 fwd_type,
                                                                 chan_id));


    }



     CTC_ERROR_RETURN(sys_greatbelt_queue_cpu_reason_init(lchip));
     CTC_ERROR_RETURN(_sys_greatbelt_internal_port_ip_tunnel_init(lchip));
     CTC_ERROR_RETURN(_sys_greatbelt_internal_port_bfd_init(lchip));

    return CTC_E_NONE;
}

int32
sys_greatbelt_internal_port_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == gb_inter_port_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(gb_inter_port_master[lchip]->mutex);
    mem_free(gb_inter_port_master[lchip]);

    return CTC_E_NONE;
}


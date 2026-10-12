/**
 @file sys_greatbelt_mirror.c

 @date 2009-10-21

 @version v2.0
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_const.h"
#include "ctc_cpu_traffic.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_mirror.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_cpu_reason.h"
#include "greatbelt/include/drv_io.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define MAX_SYS_MIRROR_SESSION_TYPE (MAX_CTC_MIRROR_SESSION_TYPE + SYS_MIRROR_ACL_ID_MAX)
#define SYS_MIRROR_ACL_ID_MAX 4

#define SYS_MIRROR_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gb_mirror_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_MIRROR_CREATE_LOCK(lchip)                            \
    do                                                          \
    {                                                           \
        sal_mutex_create(&p_gb_mirror_master[lchip]->mutex);    \
        if (NULL == p_gb_mirror_master[lchip]->mutex)           \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_MIRROR_LOCK(lchip) \
    sal_mutex_lock(p_gb_mirror_master[lchip]->mutex)

#define SYS_MIRROR_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_mirror_master[lchip]->mutex)

#define CTC_ERROR_RETURN_MIRROR_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_mirror_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

struct sys_mirror_master_s
{
    uint32 mirror_nh[MAX_CTC_MIRROR_SESSION_ID][MAX_SYS_MIRROR_SESSION_TYPE][CTC_BOTH_DIRECTION];
    uint8 external_nh[MAX_CTC_MIRROR_SESSION_ID][MAX_SYS_MIRROR_SESSION_TYPE][CTC_BOTH_DIRECTION];
    sal_mutex_t    * mutex;
};
typedef struct sys_mirror_master_s sys_mirror_master_t;

sys_mirror_master_t* p_gb_mirror_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/*
 @brief This function is to write chip to set the destination port
*/
STATIC int32
_sys_greatbelt_mirror_set_iloop_port(uint8 lchip, uint32 nh_id, bool is_add)
{
    ds_phy_port_ext_t phy_port_ext;
    ds_phy_port_t phy_port;
    ds_src_port_t src_port;
    sys_nh_info_com_t* p_nhinfo = NULL;
    uint32 cmd = 0;
    uint16 lport = 0;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo_by_nhid(lchip, nh_id, &p_nhinfo));
    if (p_nhinfo->hdr.nh_entry_type != SYS_NH_TYPE_ILOOP)
    {
        return CTC_E_NONE;
    }

    if (CTC_IS_BIT_SET(p_nhinfo->hdr.dsfwd_info.dsnh_offset, 11)
        && ((7 == ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7))
        || (0 == ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7))))
    {
        lport = p_nhinfo->hdr.dsfwd_info.dsnh_offset & 0x7F;

        cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &src_port));
        src_port.route_disable = is_add? 1 : 0;
        src_port.stp_disable = is_add? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &src_port));

        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
        phy_port_ext.default_vlan_id = is_add? 0 : 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

        cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));
        phy_port.packet_type_valid  = is_add? 1 : 0;
        phy_port.packet_type        = is_add? ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7) : 0;
        cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_mirror_set_dest_write_chip(uint8 lchip, uint32 excp_index, uint32 dest_gport, uint32 nh_ptr)
{

    uint8 gchip_id  = 0;
    uint8 gchip_id0 = 0;
    uint16 lport = 0;
    uint32 destmap = 0;
    uint32 cmd = 0;
    uint16  sub_queue_id = 0;
    uint8  offset = 0;
    ds_met_fifo_excp_t met_excp;
    ds_buf_retrv_excp_t bufrev_exp;

    sal_memset(&met_excp, 0, sizeof(ds_met_fifo_excp_t));
    sal_memset(&bufrev_exp, 0, sizeof(ds_buf_retrv_excp_t));

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, excp_index = %d, dest_gport = %d, nh_ptr = %d\n", lchip, excp_index, dest_gport, nh_ptr);

    if (CTC_IS_CPU_PORT(dest_gport))
    {
        lport = SYS_RESERVE_PORT_ID_DMA;
    }
    else
    {
        lport = CTC_MAP_GPORT_TO_LPORT(dest_gport);
    }

    if ((SYS_RESERVE_PORT_ID_DROP == lport)
        || SYS_IS_LOOP_PORT(dest_gport))
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip_id);
        destmap = (gchip_id << 16) | lport;
    }
    else
    {
        gchip_id0 = SYS_MAP_GPORT_TO_GCHIP(dest_gport);

        gchip_id = (lport == SYS_RESERVE_PORT_ID_CPU || lport == SYS_RESERVE_PORT_ID_DMA)? gchip_id0 : SYS_MAP_GPORT_TO_GCHIP(dest_gport);

        if ((lport & 0xFF) >= SYS_INTERNAL_PORT_START)
        {
            destmap = (gchip_id << 16) | (SYS_QSEL_TYPE_INTERNAL_PORT << 12) | lport;   /*queueseltype:destmap[15:12]*/
        }
        else if(CTC_IS_CPU_PORT(dest_gport))
        {
            CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_get_queue_offset(lchip, CTC_PKT_CPU_REASON_MIRRORED_TO_CPU, &offset, &sub_queue_id));
            destmap = SYS_REASON_ENCAP_DEST_MAP(gchip_id, sub_queue_id);
        }
        else
        {
            destmap = (gchip_id << 16) | lport;
        }
    }

    /*read DsMetFifoExcp_t table*/
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));

    met_excp.dest_map = destmap;

    /*write DsMetFifoExcp_t table*/
    cmd = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));

    /*read DsBufRetrvExcp_t table*/
    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));

    bufrev_exp.next_hop_ptr = nh_ptr;

    /*write DsBufRetrvExcp_t table*/
    cmd = DRV_IOW(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));

    return CTC_E_NONE;

}

/*
 @brief This function is to initialize the mirror module
*/
int32
sys_greatbelt_mirror_init(uint8 lchip)
{
    uint8 session_id;
    uint8 gchip_id = 0;
    uint32 cmd1, cmd2;
    uint32 excp_index;
    uint32 nh_ptr;
    uint32 cmd;
    ds_met_fifo_excp_t met_excp;
    ds_buf_retrv_excp_t bufrev_exp;
    epe_mirror_escape_cam_t mirror_escape;

    LCHIP_CHECK(lchip);
    if (p_gb_mirror_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_memset(&met_excp, 0, sizeof(ds_met_fifo_excp_t));
    sal_memset(&bufrev_exp, 0, sizeof(ds_buf_retrv_excp_t));
    cmd1 = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    cmd2 = DRV_IOW(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_mirror_dsnh_offset(lchip, &nh_ptr));

    sys_greatbelt_get_gchip_id(lchip, &gchip_id);
    met_excp.dest_map = (gchip_id << 16) | SYS_RESERVE_PORT_ID_DROP;
    bufrev_exp.next_hop_ptr = nh_ptr;

    for (session_id = 0; session_id < MAX_CTC_MIRROR_SESSION_ID; session_id++)
    {
        /*port session ingress*/
        excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));

        /*port session egress*/
        excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));

        /*vlan session ingress*/
        excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));

        /*vlan session egress*/
        excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));

        /*acl session ingress*/
        excp_index = SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd2, &bufrev_exp));

        /*acl session egress*/
        excp_index = SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd2, &bufrev_exp));

    }


    p_gb_mirror_master[lchip] = (sys_mirror_master_t*)mem_malloc(MEM_MIRROR_MODULE, sizeof(sys_mirror_master_t));
    if (NULL == p_gb_mirror_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_mirror_master[lchip], 0, sizeof(sys_mirror_master_t));
    sal_memset(&mirror_escape, 0, sizeof(epe_mirror_escape_cam_t));

    mirror_escape.mac_da_mask0_low = 0xFFFFFFFF;
    mirror_escape.mac_da_mask0_high = 0xFFFF;

    mirror_escape.mac_da_mask1_low = 0xFFFFFFFF;
    mirror_escape.mac_da_mask1_high = 0xFFFF;
    cmd = DRV_IOW(EpeMirrorEscapeCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mirror_escape));

    SYS_MIRROR_CREATE_LOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_greatbelt_mirror_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_mirror_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gb_mirror_master[lchip]->mutex);
    mem_free(p_gb_mirror_master[lchip]);

    return CTC_E_NONE;
}

/*
 @brief This function is to set port able to mirror
*/
int32
sys_greatbelt_mirror_set_port_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool enable, uint8 session_id)
{
    uint16 lport;
    uint32 cmd;
    buffer_store_ctl_t buffer_store_ctl;

    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl));
    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    if (CTC_IS_CPU_PORT(gport))
    {
        SYS_MAP_GCHIP_TO_LCHIP(SYS_MAP_GPORT_TO_GCHIP(gport), lchip);
        cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            buffer_store_ctl.cpu_rx_exception_en0 = (enable == TRUE)? 1 : 0;
            buffer_store_ctl.cpu_rx_exception_en1 = (enable == TRUE)? 1 : 0;
        }
        if((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            buffer_store_ctl.cpu_tx_exception_en = (enable == TRUE)? 1 : 0;
        }
        cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        return CTC_E_NONE;
    }
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);


    if (TRUE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d, direction = %d, span_id = %d\n",
                           gport, dir, session_id);

        if (session_id >= MAX_CTC_MIRROR_SESSION_ID)
        {
            return CTC_E_MIRROR_EXCEED_SESSION;
        }

        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, session_id));
        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, TRUE));
    }
    else if (FALSE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d, direction = %d\n", gport, dir);

        CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, FALSE));
    }

    return CTC_E_NONE;
}

/*
 @brief This function is to get the information of port mirror
*/
int32
sys_greatbelt_mirror_get_port_info(uint8 lchip, uint32 gport, ctc_direction_t dir, bool* enable, uint8* session_id)
{
    uint16 lport;
    uint32 cmd;
    uint32 value = 0;
    uint32 value_enable = 0;
    buffer_store_ctl_t buffer_store_ctl;

    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl));
    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d, direction = %d\n", gport, dir);

    CTC_PTR_VALID_CHECK(enable);
    CTC_PTR_VALID_CHECK(session_id);

    if (CTC_IS_CPU_PORT(gport))
    {
        if (dir == CTC_BOTH_DIRECTION)
        {
            return CTC_E_INVALID_PARAM;
        }
        SYS_MAP_GCHIP_TO_LCHIP(SYS_MAP_GPORT_TO_GCHIP(gport), lchip);
        cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        if (dir == CTC_INGRESS)
        {
             *enable = (buffer_store_ctl.cpu_rx_exception_en0)? TRUE : FALSE;
             *session_id = value;
        }
        else
        {
             *enable = (buffer_store_ctl.cpu_tx_exception_en)? TRUE : FALSE;
             *session_id = value;
        }
        return CTC_E_NONE;
    }
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &value_enable));
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &value));
    *session_id = value;
    *enable = value_enable ? TRUE : FALSE;

    return CTC_E_NONE;
}

/*
 @brief This function is to set vlan able to mirror
*/
int32
sys_greatbelt_mirror_set_vlan_en(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool enable, uint8 session_id)
{

    int32 ret = 0;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    if (TRUE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d, span_id = %d\n", vlan_id, dir, session_id);

        CTC_VLAN_RANGE_CHECK(vlan_id);

        if (session_id >= MAX_CTC_MIRROR_SESSION_ID)
        {
            return CTC_E_MIRROR_EXCEED_SESSION;
        }

        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, session_id);
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, 1);
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, session_id);
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, 1);
        }
    }
    else if (FALSE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d\n", vlan_id, dir);

        CTC_VLAN_RANGE_CHECK(vlan_id);

        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, 0);
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, 0);
        }
    }

    return ret;

}

/*
 @brief This function is to get the information of vlan mirror
*/
int32
sys_greatbelt_mirror_get_vlan_info(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool* enable, uint8* session_id)
{
    int32           ret = 0;
    uint32          value = 0;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d\n", vlan_id, dir);

    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(enable);
    CTC_PTR_VALID_CHECK(session_id);

    if (dir == CTC_INGRESS)
    {
        ret = ret ? ret : sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, &value);
        *session_id = value;
        ret = ret ? ret : sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, &value);
        *enable = value;
    }
    else if (dir == CTC_EGRESS)
    {
        ret = ret ? ret : sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, &value);
        *session_id = value;
        ret = ret ? ret : sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, &value);
        *enable = value;
    }
    else
    {
        return CTC_E_INVALID_DIR;
    }

    return ret;

}

STATIC int32
_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(uint8 lchip, ctc_mirror_dest_t* mirror, uint32 dest_gport, uint32 nh_ptr)
{
    uint32 excp_index = 0;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, direction = %d, dest_gport = %d, session_id = %d, nh_ptr = %d\n", lchip, mirror->dir, dest_gport, mirror->session_id, nh_ptr);

    if (mirror->type == CTC_MIRROR_L2SPAN_SESSION)    /*port mirror*/
    {
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + mirror->session_id;

            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }

        if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + mirror->session_id;
            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }
    }
    else if (mirror->type == CTC_MIRROR_L3SPAN_SESSION)   /*vlan mirror*/
    {
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + mirror->session_id;

            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }

        if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + mirror->session_id;

            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }
    }
    else if (mirror->type == CTC_MIRROR_ACLLOG_SESSION)  /*acllog0 mirror set dest*/
    {
        CTC_MAX_VALUE_CHECK(mirror->session_id, MAX_CTC_MIRROR_SESSION_ID - 1);
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE + mirror->session_id + mirror->acl_priority * MAX_CTC_MIRROR_SESSION_ID;

            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }

        if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE + mirror->session_id + mirror->acl_priority * MAX_CTC_MIRROR_SESSION_ID;
            CTC_ERROR_RETURN(
                _sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));

        }
    }
    else if(mirror->type == CTC_MIRROR_CPU_SESSION)
    {
        excp_index = SYS_MIRROR_CPU_SESSION_INDEX;
        CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr));
    }

    return CTC_E_NONE;
}

/*
 @brief This function is to set remote mirror destination port
*/
int32
_sys_greatbelt_mirror_rspan_set_dest(uint8 lchip, ctc_mirror_dest_t* mirror)
{
    uint16 offset_arry = 0;
    uint16 nh_offset = 0;
    uint8 type = 0;
    uint32 temp_gport = 0;
    int32 ret = CTC_E_NONE;
    uint32 nh_id = CTC_MAX_UINT32_VALUE;
    ctc_rspan_nexthop_param_t  temp_p_nh_param;

    sal_memset(&temp_p_nh_param, 0, sizeof(ctc_rspan_nexthop_param_t));

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dest_gport = %d, direction = %d, session_id = %d",
                       mirror->dest_gport, mirror->dir, mirror->session_id);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, mirror->dest_gport));

    if (1 == mirror->vlan_valid)
    {
        CTC_VLAN_RANGE_CHECK(mirror->rspan.vlan_id);
        if (!sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(mirror->dest_gport)) && !CTC_IS_LINKAGG_PORT(mirror->dest_gport))
        {
            temp_p_nh_param.remote_chip = TRUE;
        }

        temp_p_nh_param.rspan_vid = mirror->rspan.vlan_id;
        CTC_ERROR_RETURN(sys_greatbelt_rspan_nh_create(lchip, &nh_id,  CTC_NH_RESERVED_DSNH_OFFSET_FOR_RSPAN, &temp_p_nh_param));


    }
    else
    {
        nh_id = mirror->rspan.nh_id;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_mirror_info_by_nhid(lchip, nh_id, &offset_arry, &temp_gport, TRUE));

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    if (temp_gport == CTC_MAX_UINT16_VALUE)
    {
        temp_gport = mirror->dest_gport;
    }

    nh_offset = offset_arry;
    CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_iloop_port(lchip, nh_id, TRUE));
    CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, temp_gport, nh_offset));

    type = mirror->type;
    if (mirror->type == CTC_MIRROR_ACLLOG_SESSION)
    {
        type = CTC_MIRROR_ACLLOG_SESSION + mirror->acl_priority;
    }
    else if (mirror->type > CTC_MIRROR_ACLLOG_SESSION)
    {
        type = mirror->type + SYS_MIRROR_ACL_ID_MAX - 1;
    }

    SYS_MIRROR_LOCK(lchip);
    p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][mirror->dir] = nh_id;
    if (0 == mirror->vlan_valid)
    {
        p_gb_mirror_master[lchip]->external_nh[mirror->session_id][type][mirror->dir] = 1;
    }
    SYS_MIRROR_UNLOCK(lchip);

    return CTC_E_NONE;

}

/*
 @brief This function is to set local mirror destination port
*/
int32
sys_greatbelt_mirror_set_dest(uint8 lchip, ctc_mirror_dest_t* mirror)
{
    uint32 temp_gport = 0;
    uint32 nh_ptr = 0;
    uint32 nh_id = 0;
    uint8 type = 0;
    uint16 offset_arry = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dest_gport = %d, direction = %d, session_id = %d\n",
                       mirror->dest_gport, mirror->dir, mirror->session_id);

    if (mirror->type >= MAX_CTC_MIRROR_SESSION_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*cpu mirror*/
    if (mirror->type == CTC_MIRROR_CPU_SESSION)
    {
         mirror->session_id = 0;
    }

    if (mirror->session_id >= MAX_CTC_MIRROR_SESSION_ID)
    {
        return CTC_E_MIRROR_EXCEED_SESSION;
    }

    type = mirror->type;

    if (mirror->type == CTC_MIRROR_ACLLOG_SESSION)
    {
        if (mirror->acl_priority < SYS_MIRROR_ACL_ID_MAX)
        {
            type = CTC_MIRROR_ACLLOG_SESSION + mirror->acl_priority;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (mirror->type > CTC_MIRROR_ACLLOG_SESSION)
    {
        type = mirror->type + SYS_MIRROR_ACL_ID_MAX - 1;
    }

    if (mirror->dir > CTC_BOTH_DIRECTION)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MIRROR_LOCK(lchip);
    /*delete external nexthop mirror session*/
    if ((mirror->dir == CTC_INGRESS || mirror->dir == CTC_BOTH_DIRECTION)
        && p_gb_mirror_master[lchip]->external_nh[mirror->session_id][type][CTC_INGRESS])
    {
        nh_id = p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_INGRESS];
        CTC_ERROR_RETURN_MIRROR_UNLOCK(_sys_greatbelt_mirror_set_iloop_port(lchip, nh_id, FALSE));
        CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_greatbelt_nh_get_mirror_info_by_nhid(lchip, nh_id, &offset_arry, &temp_gport, FALSE));
        p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_INGRESS] = 0;
        p_gb_mirror_master[lchip]->external_nh[mirror->session_id][type][CTC_INGRESS] = 0;
    }

    if ((mirror->dir == CTC_EGRESS || mirror->dir == CTC_BOTH_DIRECTION)
        && p_gb_mirror_master[lchip]->external_nh[mirror->session_id][type][CTC_EGRESS])
    {
        nh_id = p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_EGRESS];
        CTC_ERROR_RETURN_MIRROR_UNLOCK(_sys_greatbelt_mirror_set_iloop_port(lchip, nh_id, FALSE));
        CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_greatbelt_nh_get_mirror_info_by_nhid(lchip, nh_id, &offset_arry, &temp_gport, FALSE));
        p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_EGRESS] = 0;
        p_gb_mirror_master[lchip]->external_nh[mirror->session_id][type][CTC_EGRESS] = 0;
    }

    /*delete old mirror session*/
    if ((mirror->dir == CTC_INGRESS || mirror->dir == CTC_BOTH_DIRECTION))
    {
        nh_id = p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_INGRESS];
        if (nh_id != 0)
        {
            CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_greatbelt_rspan_nh_delete(lchip, nh_id));
            p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_INGRESS] = 0;
        }
    }

    if ((mirror->dir == CTC_EGRESS || mirror->dir == CTC_BOTH_DIRECTION))
    {
        nh_id = p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_EGRESS];
        if (nh_id != 0)
        {
            CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_greatbelt_rspan_nh_delete(lchip, nh_id));
            p_gb_mirror_master[lchip]->mirror_nh[mirror->session_id][type][CTC_EGRESS] = 0;
        }
    }
    SYS_MIRROR_UNLOCK(lchip);

    /*add new mirror session*/
    if (mirror->is_rspan)
    {
        if (mirror->dir != CTC_BOTH_DIRECTION)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_rspan_set_dest(lchip, mirror));
        }
        else if (mirror->dir == CTC_BOTH_DIRECTION)
        {
            mirror->dir = CTC_INGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_rspan_set_dest(lchip, mirror));
            mirror->dir = CTC_EGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_rspan_set_dest(lchip, mirror));
            mirror->dir = CTC_BOTH_DIRECTION;
        }
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, mirror->dest_gport));
        if (CTC_IS_CPU_PORT(mirror->dest_gport))
        {
            nh_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_MIRRORED_TO_CPU, 0);
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_mirror_dsnh_offset(lchip, &nh_ptr));
        }

        if (mirror->dir == CTC_INGRESS)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, mirror->dest_gport, nh_ptr));
        }
        else if (mirror->dir == CTC_BOTH_DIRECTION)
        {
            mirror->dir = CTC_INGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, mirror->dest_gport, nh_ptr));
            mirror->dir = CTC_BOTH_DIRECTION;
        }

        if (mirror->dir == CTC_EGRESS)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, mirror->dest_gport, nh_ptr));
        }
        else if (mirror->dir == CTC_BOTH_DIRECTION)
        {
            mirror->dir = CTC_EGRESS;
            CTC_ERROR_RETURN(_sys_greatbelt_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, mirror->dest_gport, nh_ptr));
            mirror->dir = CTC_BOTH_DIRECTION;
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_mirror_rspan_escape_en(uint8 lchip, bool enable)
{
    uint32 cmd;
    uint32 field_val;

    SYS_MIRROR_INIT_CHECK(lchip);

    field_val = (TRUE == enable) ? 1 : 0;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_MirrorEscapeCamEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

int32
sys_greatbelt_mirror_rspan_escape_mac(uint8 lchip, ctc_mirror_rspan_escape_t* escape)
{

    uint32 cmd;
    epe_mirror_escape_cam_t mirror_escape;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    mirror_escape.mac_da_value0_low = (escape->mac0[2] << 24) | (escape->mac0[3] << 16)
        | (escape->mac0[4] << 8) | escape->mac0[5];
    mirror_escape.mac_da_value0_high = (escape->mac0[0] << 8) | escape->mac0[1];

    mirror_escape.mac_da_mask0_low = (escape->mac_mask0[2] << 24) | (escape->mac_mask0[3] << 16)
        | (escape->mac_mask0[4] << 8) | escape->mac_mask0[5];
    mirror_escape.mac_da_mask0_high = (escape->mac_mask0[0] << 8) | (escape->mac_mask0[1]);

    mirror_escape.mac_da_value1_low = (escape->mac1[2] << 24) | (escape->mac1[3] << 16)
        | (escape->mac1[4] << 8) | escape->mac1[5];
    mirror_escape.mac_da_value1_high = (escape->mac1[0] << 8) | escape->mac1[1];

    mirror_escape.mac_da_mask1_low = (escape->mac_mask1[2] << 24) | (escape->mac_mask1[3] << 16)
        | (escape->mac_mask1[4] << 8) | escape->mac_mask1[5];
    mirror_escape.mac_da_mask1_high = (escape->mac_mask1[0] << 8) | (escape->mac_mask1[1]);

    cmd = DRV_IOW(EpeMirrorEscapeCam_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mirror_escape));

    return CTC_E_NONE;

}

/*
 @brief This function is to remove mirror destination port
*/
int32
sys_greatbelt_mirror_unset_dest(uint8 lchip, ctc_mirror_dest_t* mirror)
{
    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "session_id = %d, direction = %d, session type = %d\n",
                       mirror->session_id, mirror->dir, mirror->type);
    mirror->dest_gport = SYS_RESERVE_PORT_ID_DROP;
    mirror->is_rspan = 0;
    CTC_ERROR_RETURN(sys_greatbelt_mirror_set_dest(lchip, mirror));

    return CTC_E_NONE;
}

/*
  @brief This functions is used to set packet enable or not to log if the packet is discarded on EPE process.
*/
int32
sys_greatbelt_mirror_set_mirror_discard(uint8 lchip, ctc_direction_t dir, uint16 discard_flag, bool enable)
{
    uint32 cmd;
    uint32 logon_discard;
    uint16 discard_flag_temp = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "discard_flag = %d, direction = %d, enable = %d\n", discard_flag, dir, enable);
    SYS_MIRROR_DISCARD_CHECK(discard_flag);

    if (discard_flag & CTC_MIRROR_L2SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L2SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_L3SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L3SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_ACLLOG_PRI_DISCARD);
    }

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_LogOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));

        if (TRUE == enable)
        {
            CTC_SET_FLAG(logon_discard, discard_flag_temp);
        }
        else
        {
            CTC_UNSET_FLAG(logon_discard, discard_flag_temp);
        }

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_LogOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_LogOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));

        if (TRUE == enable)
        {
            CTC_SET_FLAG(logon_discard, discard_flag_temp);
        }
        else
        {
            CTC_UNSET_FLAG(logon_discard, discard_flag_temp);
        }

        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_LogOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_mirror_get_mirror_discard(uint8 lchip, ctc_direction_t dir, ctc_mirror_discard_t discard_flag, bool* enable)
{

    uint32 cmd;
    uint32 field_val;
    uint16 discard_flag_temp = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "discard_flag = %d, direction = %d\n", discard_flag, dir);

    CTC_PTR_VALID_CHECK(enable);
    SYS_MIRROR_DISCARD_CHECK(discard_flag);

    switch (discard_flag)
    {
    case CTC_MIRROR_L2SPAN_DISCARD:
    case CTC_MIRROR_L3SPAN_DISCARD:
    case CTC_MIRROR_ACLLOG_PRI_DISCARD:
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (discard_flag & CTC_MIRROR_L2SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L2SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_L3SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L3SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_ACLLOG_PRI_DISCARD);
    }

    switch (dir)
    {
    case CTC_INGRESS:
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_LogOnDiscard_f);
        break;

    case CTC_EGRESS:
        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_LogOnDiscard_f);
        break;

    default:
        return CTC_E_INVALID_DIR;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *enable = CTC_FLAG_ISSET(field_val, discard_flag_temp) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mirror_show_port_info(uint8 lchip, uint32 gport)
{
    uint8 gchip_id  = 0;
    uint16 lport = 0;
    uint32 dest_map = 0;
    uint32 nh_ptr = 0;
    uint32 excp_index = 0;
    uint32 cmd = 0;
    uint32 session_id = 0;
    uint8  cpu_session_id = 0;
    uint32 enable = FALSE;
    bool   cpu_enable = FALSE;
    ctc_direction_t dir;
    ds_met_fifo_excp_t met_excp;
    ds_buf_retrv_excp_t bufrev_exp;

    sal_memset(&met_excp, 0, sizeof(ds_met_fifo_excp_t));
    sal_memset(&bufrev_exp, 0, sizeof(ds_buf_retrv_excp_t));

    SYS_MIRROR_INIT_CHECK(lchip);

    if (CTC_IS_CPU_PORT(gport))
    {
        cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MIRROR_CPU_SESSION_INDEX, cmd, &met_excp));
        gchip_id = met_excp.dest_map >> 16;
        lport = met_excp.dest_map & CTC_LOCAL_PORT_MASK;
        dest_map = ((gchip_id << 8) | lport);

        cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MIRROR_CPU_SESSION_INDEX, cmd, &bufrev_exp));
        nh_ptr = bufrev_exp.next_hop_ptr;

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Port=0x%.4x \n", gport);
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");
        CTC_ERROR_RETURN(sys_greatbelt_mirror_get_port_info(lchip, gport, CTC_INGRESS, &cpu_enable, &cpu_session_id));
        if (cpu_enable == TRUE)
        {

            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :session Id: %d \n", cpu_session_id);
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);
        }
        else
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");
        }

        CTC_ERROR_RETURN(sys_greatbelt_mirror_get_port_info(lchip, gport, CTC_EGRESS, &cpu_enable, &cpu_session_id));
        if (cpu_enable == TRUE)
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :session Id: %d \n", cpu_session_id);
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);
        }
        else
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");
        }
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");
        return CTC_E_NONE;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    dir = CTC_INGRESS;
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &enable));
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &session_id));

    excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    gchip_id = met_excp.dest_map >> 16;
    lport = SYS_MAP_DRV_PORT_TO_CTC_LPORT(met_excp.dest_map);
    dest_map = CTC_MAP_LPORT_TO_GPORT(gchip_id, lport);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
    nh_ptr = bufrev_exp.next_hop_ptr;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Port=0x%.4x \n", gport);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    if (enable == TRUE)
    {

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :session Id: %d \n", session_id);

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);

    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");

    }

    enable = FALSE;
    dir = CTC_EGRESS;
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &enable));
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &session_id));

    excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    gchip_id = met_excp.dest_map >> 16;
    lport = SYS_MAP_DRV_PORT_TO_CTC_LPORT(met_excp.dest_map);
    dest_map = CTC_MAP_LPORT_TO_GPORT(gchip_id, lport);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
    nh_ptr = bufrev_exp.next_hop_ptr;

    if (enable == TRUE)
    {

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :session Id: %d \n", session_id);

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");
    }

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    return CTC_E_NONE;

}

int32
sys_greatbelt_mirror_show_vlan_info(uint8 lchip, uint16 vlan_id)
{
    uint8 gchip_id  = 0;
    uint8 lport = 0;
    uint32 dest_map = 0;
    uint32 nh_ptr = 0;
    uint32 excp_index = 0;
    uint32 cmd = 0;
    uint32 session_id;
    uint32 enable = FALSE;
    ds_met_fifo_excp_t met_excp;
    ds_buf_retrv_excp_t bufrev_exp;

    sal_memset(&met_excp, 0, sizeof(ds_met_fifo_excp_t));
    sal_memset(&bufrev_exp, 0, sizeof(ds_buf_retrv_excp_t));

    SYS_MIRROR_INIT_CHECK(lchip);

    CTC_VLAN_RANGE_CHECK(vlan_id);

    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, &session_id));
    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, &enable));

    excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    gchip_id = met_excp.dest_map >> 16;
    lport = SYS_MAP_DRV_PORT_TO_CTC_LPORT(met_excp.dest_map);
    dest_map = CTC_MAP_LPORT_TO_GPORT(gchip_id, lport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
    nh_ptr = bufrev_exp.next_hop_ptr;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Vlan=%d \n", vlan_id);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    if (enable == TRUE)
    {

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :session Id: %d \n", session_id);

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");
    }

    enable = FALSE;
    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, &session_id));
    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, &enable));

    excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    gchip_id = met_excp.dest_map >> 16;
    lport = SYS_MAP_DRV_PORT_TO_CTC_LPORT(met_excp.dest_map);
    dest_map = CTC_MAP_LPORT_TO_GPORT(gchip_id, lport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
    nh_ptr = bufrev_exp.next_hop_ptr;

    if (enable == TRUE)
    {

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :session Id: %d \n", session_id);

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestMap: 0x%.4x  NexthopPtr: 0x%.4x\n", dest_map, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");

    }

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    return CTC_E_NONE;

}


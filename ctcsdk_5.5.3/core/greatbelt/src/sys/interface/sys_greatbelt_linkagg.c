/**
 @file sys_greatbelt_linkagg.c

 @date 2009-10-19

 @version v2.0

 The file contains all Linkagg APIs of sys layer
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_debug.h"
#include "ctc_error.h"

#include "sys_greatbelt_linkagg.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_register.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_data_path.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

/* dynamic interval */
#define SYS_LINKAGG_DYNAMIC_INTERVAL 5

/* index of DsDlbTokenThrd for 1G port */
#define SYS_LINKAGG_DLB_TOKEN_THRD_INDEX_1G 2

/* index of DsDlbTokenThrd for 10G port */
#define SYS_LINKAGG_DLB_TOKEN_THRD_INDEX_SG 3

#define SYS_LINKAGG_GOTO_ERROR(ret, error) \
    do { \
        if (ret < 0) \
        {\
            goto error;\
        } \
       } while (0)

#define SYS_LINKAGG_LOCK_INIT(lchip) \
    do { \
        int32 ret = sal_mutex_create(&(p_gb_linkagg_master[lchip]->p_linkagg_mutex)); \
        if (ret || !(p_gb_linkagg_master[lchip]->p_linkagg_mutex)) \
        { \
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Create linkagg mutex fail!\n"); \
            mem_free(p_gb_linkagg_master[lchip]); \
            return CTC_E_FAIL_CREATE_MUTEX; \
        } \
    } while (0)

#define SYS_LINKAGG_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gb_linkagg_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_TID_VALID_CHECK(tid) \
    if (tid >= p_gb_linkagg_master[lchip]->linkagg_num){return CTC_E_INVALID_TID; }

#define LINKAGG_LOCK \
    if (p_gb_linkagg_master[lchip]->p_linkagg_mutex) sal_mutex_lock(p_gb_linkagg_master[lchip]->p_linkagg_mutex)
#define LINKAGG_UNLOCK \
    if (p_gb_linkagg_master[lchip]->p_linkagg_mutex) sal_mutex_unlock(p_gb_linkagg_master[lchip]->p_linkagg_mutex)

extern int32
sys_greatbelt_interrupt_set_en(uint8 lchip, ctc_intr_type_t* p_type, uint32 enable);
STATIC bool
_sys_greatbelt_linkagg_port_is_member(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 gport, uint8* index);

extern int32
sys_greatbelt_qos_shape_token_rate_compute(uint8 lchip, uint32 rate,
                                           uint8 type,
                                           uint8 pps_en,
                                           uint32* p_token_rate,
                                           uint32* p_token_rate_frac);

sys_linkagg_master_t* p_gb_linkagg_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#define __HW_SYNC__

STATIC int32
_sys_greatbelt_linkagg_hw_sync(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


    /*1. configure Qwrite table */


    cmd = DRV_IOW(QWriteCtl_t, QWriteCtl_LinkChangeEn_f);
    field_val = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_hw_sync_add_member(uint8 lchip, uint8 tid, uint8 lport)
{
    uint32 cmd = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    ds_link_aggregation_port_t linkagg_port;
    uint8 channel_id = 0;
    uint16 gport = 0;
    uint8  gchip = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(ds_link_aggregate_channel_t));
    sal_memset(&linkagg_port, 0, sizeof(ds_link_aggregation_port_t));
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }



     /*channel_id = lport;*/

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    linkagg_channel.link_aggregation_group = tid;
    linkagg_channel.group_type = 0;

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    linkagg_port.channel_id = channel_id;
    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &linkagg_port));


    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_hw_sync_del_member(uint8 lchip, uint8 tid, uint8 lport)
{
    uint32 cmd = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    ds_link_aggregation_port_t linkagg_port;
    uint8 channel_id = 0;
    uint16 gport = 0;
    uint8  gchip = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(ds_link_aggregate_channel_t));
    sal_memset(&linkagg_port, 0, sizeof(ds_link_aggregation_port_t));
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


     /*channel_id = lport;*/

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    cmd = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &linkagg_port));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_hw_sync_remove_member(uint8 lchip, uint8 chan_id)
{
    sys_linkagg_t* p_linkagg = NULL;
    uint8 tid = 0;
    uint16 gport = 0;
    uint8 lport = 0;
    uint8 gchip = 0;
    uint8 tail_index = 0;
    uint32 agg_base = 0;
    ds_link_aggregate_group_t ds_linkagg_group;
    ds_link_aggregate_member_t ds_linkagg_member;
    ds_link_aggregate_channel_t linkagg_channel;
    uint8 mem_num = 0;
    uint32 cmd = 0;
    uint8 index = 0;

    sal_memset(&linkagg_channel, 0, sizeof(ds_link_aggregate_channel_t));

    /* get local phy port by chan_id */
    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel));
    tid = linkagg_channel.link_aggregation_group;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tid:%d channel:%d\n", tid, chan_id);
    /* update softtable */
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        return CTC_E_NONE;
    }

    if (FALSE == _sys_greatbelt_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mem not exist! \n");
        return CTC_E_MEMBER_PORT_NOT_EXIST;
    }

    (p_gb_linkagg_master[lchip]->mem_port_num[tid])--;
    (p_linkagg->port_cnt)--;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mem_port_num:%d, port_cnt:%d, gport:%d\n",
        p_gb_linkagg_master[lchip]->mem_port_num[tid], p_linkagg->port_cnt, gport);


    tail_index = p_gb_linkagg_master[lchip]->mem_port_num[tid];

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tail_index:%d \n", tail_index);

    sal_memcpy(&(p_linkagg->port[index]), &(p_linkagg->port[tail_index]), sizeof(sys_linkagg_port_t));
    p_linkagg->port[tail_index].valid = 0;

    /* port cnt is 0, need add reserve channel */
    if (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 0)
    {
        mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;

        cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));
        ds_linkagg_group.link_agg_mem_num = 1;
        cmd = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));

        agg_base = tid * mem_num;
        sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
        ds_linkagg_member.dest_chip_id = gchip;
        ds_linkagg_member.dest_queue = SYS_RESERVE_PORT_ID_DROP;
        cmd = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, agg_base, cmd, &ds_linkagg_member));
    }

    /*set local port's global_port*/
    if (TRUE == sys_greatbelt_chip_is_local(lchip, gchip))
    {
        sys_greatbelt_port_set_global_port(lchip, lport, gport);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_hw_sync_retrieve_member(uint8 lchip, uint8 chan_id)
{
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 is_up = 0;
    uint32 cmd = 0;
    uint8 tid = 0;
    ds_link_aggregate_channel_t linkagg_channel;
    ctc_chip_phy_mapping_para_t* p_phy_mapping = NULL;
    uint32 value = 0;
    uint8 mdio_bus = 0;
    uint8 pcs_mode = 0;
    uint8 lport = 0;
    drv_datapath_port_capability_t port_cap;
    int32 ret = CTC_E_NONE;

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    /* get local phy port by chan_id */
    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);


    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "retrive tid:%d channel:%d\n", tid, chan_id);

    p_phy_mapping = (ctc_chip_phy_mapping_para_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(ctc_chip_phy_mapping_para_t));
    if (NULL == p_phy_mapping)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_phy_mapping, 0, sizeof(ctc_chip_phy_mapping_para_t));

    CTC_ERROR_GOTO(sys_greatbelt_chip_get_phy_mapping(lchip, p_phy_mapping), ret, out);
    if (p_phy_mapping->port_mdio_mapping_tbl[lport] != 0xff)
    {
        mdio_bus = p_phy_mapping->port_mdio_mapping_tbl[lport];

        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mdio bus:%d phy:%d\n", mdio_bus, p_phy_mapping->port_phy_mapping_tbl[lport]);

        cmd = DRV_IOR(MdioLinkStatus_t, MdioLinkStatus_Mdio0LinkStatus_f+mdio_bus);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value), ret, out);

        if ((value >> (p_phy_mapping->port_phy_mapping_tbl[lport]))&0x01)
        {
            /* mdio scan status is up */
            is_up = 1;
        }
    }
    else
    {
#if 0
        sys_greatbelt_port_get_port_type(lchip, chan_id, &mac_type);

        if (mac_type == CTC_PORT_MAC_GMAC)
        {
            /* gmac ---> serdes */
            CTC_ERROR_RETURN(drv_greatbelt_get_gmac_info(lchip, chan_id, DRV_CHIP_MAC_PCS_INFO, (void*)&pcs_mode));
        }
        else
        {
            /* sgmac --> serdes */
            chan_id = chan_id - 48;
            CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_info(lchip, chan_id, DRV_CHIP_MAC_PCS_INFO, (void*)&pcs_mode));
        }
#endif

        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if (!port_cap.valid)
        {
            ret = CTC_E_INVALID_LOCAL_PORT;
            goto out;
        }

        pcs_mode = port_cap.pcs_mode;

        if (pcs_mode != DRV_SERDES_SGMII_MODE)
        {
            /* pcs mode is not match */
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs mode is not match \n");
            ret = CTC_E_INVALID_PARAM;
            goto out;
        }

        /* get port link status from mac, this only used for sgmii mode */
        sys_greatbelt_port_get_mac_link_up(lchip, gport, &is_up, 0);
    }

    if (is_up)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "linkmember need retrieve !%d()\n", chan_id);
        sal_memset(&linkagg_channel, 0, sizeof(ds_link_aggregate_channel_t));
        cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel), ret, out);
        tid = linkagg_channel.link_aggregation_group;

        /* need retrieve */
        sys_greatbelt_linkagg_add_port(lchip, tid, gport);
    }

out:
    if (p_phy_mapping)
    {
        mem_free(p_phy_mapping);
    }

    return ret;
}

int32
sys_greatbelt_linkagg_failover_detec(uint8 lchip, uint8 linkdown_chan)
{
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_down:0x%x\n ", linkdown_chan);

    _sys_greatbelt_hw_sync_remove_member(lchip, linkdown_chan);
    _sys_greatbelt_hw_sync_retrieve_member(lchip, linkdown_chan);

    return CTC_E_NONE;
}

#define __DLB__

STATIC int32
_sys_greatbelt_dlb_add_member(uint8 lchip, uint8 tid, uint8 port_index, uint8 lport)
{
    uint32 cmd = 0;
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    ds_link_aggregate_group_t ds_linkagg_group;
    uint32 value = 0;
    uint8 port_type = 0;
    uint8 gchip = 0;
    uint16 gport = 0;

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

    cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));

     /*dest_channel = lport;*/
    dest_quene = lport;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    dest_channel = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == dest_channel)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestChannel0_f + port_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestQueue0_f + port_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_quene));

    /*cfg QDlbChanSpeedModeCtl */
    cmd = DRV_IOW(QDlbChanSpeedModeCtl_t, QDlbChanSpeedModeCtl_SpeedMode0_f + lport);
    sys_greatbelt_port_get_port_type(lchip, lport, &port_type);
    if (port_type == CTC_PORT_MAC_GMAC)
    {
        /* for gmac */
        value = 2;
    }
    else
    {
        /* for sgmac */
        value = 3;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_dlb_del_member(uint8 lchip, uint8 tid, uint8 port_index, uint8 tail_index)
{
    uint32 cmd = 0;
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    ds_link_aggregate_group_t ds_linkagg_group;

    if (tail_index > SYS_MAX_DLB_MEMBER_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }


    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

    cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));
    /* get link_agg_port_member_ptr */

    /* copy the last one to the removed position */
    cmd = DRV_IOR(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestChannel0_f + tail_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOR(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestQueue0_f + tail_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_quene));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestChannel0_f + port_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestQueue0_f + port_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_quene));

    /* set the last one to reserve */
     /*dest_channel = SYS_RESERVE_PORT_ID_DROP;*/
    dest_channel = SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);
    dest_quene = SYS_RESERVE_PORT_ID_DROP;
    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestChannel0_f + tail_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestQueue0_f + tail_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd, &dest_quene));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_linkagg_reverve_member(uint8 lchip, uint8 tid)
{
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    uint32 cmd_member_set = 0;
    uint32 cmd_group_r = 0;
    uint8 index = 0;
    ds_link_aggregate_group_t ds_linkagg_group;

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

     /*dest_channel = SYS_RESERVE_PORT_ID_DROP;*/
    dest_channel = SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);
    dest_quene = SYS_RESERVE_PORT_ID_DROP;
    cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd_group_r, &ds_linkagg_group));


    for (index = 0; index < 8; index++)
    {
        cmd_member_set = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestChannel0_f + index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd_member_set, &dest_channel));
        cmd_member_set = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_DestQueue0_f + index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_linkagg_group.link_agg_port_member_ptr, cmd_member_set, &dest_quene));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to clear flow active in linkagg group
*/
STATIC int32
_sys_greatbelt_linkagg_clear_flow_active(uint8 lchip, uint8 tid)
{
    uint32 cmd_r = 0;
    uint32 cmd_w = 0;
    ds_link_aggregate_group_t ds_link_aggregate_group;
    ds_link_aggregate_member_t ds_link_aggregate_member;
    uint16 flow_num = 0;
    uint32 agg_base = 0;
    uint16 index = 0;


    sal_memset(&ds_link_aggregate_group, 0, sizeof(ds_link_aggregate_group_t));
    sal_memset(&ds_link_aggregate_member, 0, sizeof(ds_link_aggregate_member_t));

    cmd_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd_r, &ds_link_aggregate_group));

    agg_base = ds_link_aggregate_group.link_agg_mem_base;

    switch (ds_link_aggregate_group.link_agg_flow_num)
    {
        case 0:
            flow_num = 256;
            break;
        case 1:
            flow_num = 128;
            break;
        case 2:
            flow_num = 64;
            break;
        case 3:
            flow_num = 32;
            break;
        default:
            return CTC_E_EXCEED_MAX_SIZE;
    }

    /* clear active */
    cmd_r = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    cmd_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);

    for (index = 0; index < flow_num; index++)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, agg_base + index, cmd_r, &ds_link_aggregate_member));
        ds_link_aggregate_member.active = 0;
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, agg_base + index, cmd_w, &ds_link_aggregate_member));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to get index of unused member port position
*/
STATIC int32
_sys_greatbelt_linkagg_get_first_unused_pos(uint8 lchip, sys_linkagg_t* p_linkagg, uint8* index)
{
    uint8 idx = 0;
    uint8 dynamic_mode = 0;
    uint8 mem_num = 0;

    CTC_PTR_VALID_CHECK(p_linkagg);
    CTC_PTR_VALID_CHECK(index);

    /* linkagg_num*mem_num = SYS_LINKAGG_ALL_MEM_NUM(1024) for static balance  */
    if (!dynamic_mode)
    {
        mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    }
    else
    {
        mem_num = SYS_MAX_DLB_MEMBER_NUM;
    }

    for (idx = 0; idx < mem_num; idx++)
    {
        if (!(p_linkagg->port[idx].valid))
        {
            *index = idx;
            return CTC_E_NONE;
        }
    }

    return CTC_E_EXCEED_MAX_SIZE;
}

STATIC int32
_sys_greatbelt_linkagg_add_padding_mem(uint8 lchip, sys_linkagg_t* p_linkagg, uint8 start_index)
{
    uint8 mem_num = 0;
    uint8 index = 0;
    uint32 cmd_member_w = 0;
    uint32 cmd_member_r = 0;
    uint16 agg_base = 0;
    ds_link_aggregate_member_t ds_linkagg_member;
    uint8 temp = 0;
    uint8 act_mem_num = 0;

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    cmd_member_r = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    agg_base = (p_linkagg->tid) * mem_num;

    sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));


    act_mem_num = p_gb_linkagg_master[lchip]->mem_port_num[p_linkagg->tid];


    for (index = start_index + 1; index < mem_num; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + (temp % act_mem_num)), cmd_member_r, &ds_linkagg_member));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + index), cmd_member_w, &ds_linkagg_member));
        p_linkagg->port[index].pad_flag = 1;
        p_linkagg->port[index].gport = p_linkagg->port[temp].gport;
        temp++;
    }

    return CTC_E_NONE;
}

/**
 @brief The function is update asic table, add/remove member port from linkagg.
*/
STATIC int32
_sys_greatbelt_linkagg_update_table(uint8 lchip, sys_linkagg_t* p_linkagg, bool is_add_port, uint8 port_index)
{
    uint8 gchip = 0;
    uint32 cmd_group_r = 0;
    uint32 cmd_group_w = 0;
    uint32 cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    uint32 cmd_member_r = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    uint32 port_cnt = 0;
    uint8 mem_num = 0;
    uint32 agg_base = 0;
    ds_link_aggregate_member_t ds_linkagg_member;
    ds_link_aggregate_group_t ds_linkagg_group;
    uint8 lport = 0;
    int32 ret = 0;
    uint8 tail_index = 0;
    ds_link_aggregate_member_set_t ds_link_aggregate_member_set;

    sal_memset(&ds_link_aggregate_member_set, 0, sizeof(ds_link_aggregate_member_set_t));

    CTC_PTR_VALID_CHECK(p_linkagg);

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;

    sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

    agg_base = (p_linkagg->tid) * mem_num;
    tail_index = p_gb_linkagg_master[lchip]->mem_port_num[p_linkagg->tid];

    if (TRUE == is_add_port)
    {
        port_cnt = p_linkagg->port_cnt;

        lport = CTC_MAP_GPORT_TO_LPORT(p_linkagg->port[port_index].gport);
        /*for slice1 use 64-127 queue*/
        if (sys_greatbelt_get_gb_gg_interconnect_en(lchip))
        {
            if ((p_linkagg->port[port_index].gport) >> (CTC_LOCAL_PORT_LENGTH + CTC_GCHIP_LENGTH) & CTC_EXT_PORT_MASK)
            {
                lport = (lport&CTC_LOCAL_PORT_MASK)+SYS_MAX_CHANNEL_NUM;
            }
        }

        if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
        {
            /*update DsLinkAggregateMember_t */
            ds_linkagg_member.dest_chip_id = SYS_MAP_GPORT_TO_GCHIP(p_linkagg->port[port_index].gport);
            ds_linkagg_member.dest_queue = lport;

            cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + port_index), cmd_member_w, &ds_linkagg_member));
        }
        else
        {
            /* dlb mode need set DsLinkAggregateMemberSet_t */
            CTC_ERROR_RETURN(_sys_greatbelt_dlb_add_member(lchip, p_linkagg->tid, port_index,  lport));
        }

        /* update DsLinkAggregateGroup_t linkagg port cnt */
        cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_r, &ds_linkagg_group));
        ds_linkagg_group.link_agg_mem_num = port_cnt;
        cmd_group_w = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_w, &ds_linkagg_group));

        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_hw_sync_add_member(lchip, p_linkagg->tid, lport));
        }

        if (p_linkagg->need_pad)
        {
            ret = _sys_greatbelt_linkagg_add_padding_mem(lchip, p_linkagg, p_gb_linkagg_master[lchip]->mem_port_num[p_linkagg->tid]-1);
            if (ret < 0)
            {
                return ret;
            }
        }
    }
    else
    {
        /*before this function calling, the port cnt has been decreased.*/
        port_cnt = p_linkagg->port_cnt;
        lport = CTC_MAP_GPORT_TO_LPORT(p_linkagg->port[port_index].gport);
        /*for slice1 use 64-127 queue*/
        if (sys_greatbelt_get_gb_gg_interconnect_en(lchip))
        {
            if ((p_linkagg->port[port_index].gport) >> (CTC_LOCAL_PORT_LENGTH + CTC_GCHIP_LENGTH) & CTC_EXT_PORT_MASK)
            {
                lport = (lport&CTC_LOCAL_PORT_MASK)+SYS_MAX_CHANNEL_NUM;
            }
        }

        if (0 == port_cnt)
        {
            port_cnt = 1;
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "member is null ,drop it!\n");

            /* update DsLinkAggregateGroup_t linkagg port cnt */
            cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_r, &ds_linkagg_group));
            ds_linkagg_group.link_agg_mem_num = port_cnt;

            cmd_group_w = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_w, &ds_linkagg_group));

            if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
            {
                sys_greatbelt_get_gchip_id(lchip, &gchip);
                sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
                ds_linkagg_member.dest_chip_id = gchip;
                ds_linkagg_member.dest_queue = SYS_RESERVE_PORT_ID_DROP;

                cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, agg_base, cmd_member_w, &ds_linkagg_member));
            }

            p_linkagg->port[0].valid = 0;
        }
        else
        {
            /* update DsLinkAggregateGroup_t linkagg port cnt */
            cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_r, &ds_linkagg_group));
            ds_linkagg_group.link_agg_mem_num = port_cnt;
            cmd_group_w = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_w, &ds_linkagg_group));

            if(CTC_FLAG_ISSET(p_linkagg->flag, CTC_LINKAGG_GROUP_FLAG_MEM_ASCEND_ORDER))
            {
                uint16 tmp_index = port_index;

                for(;tmp_index < tail_index; tmp_index++)
                {
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tmp_index+agg_base+1, cmd_member_r, &ds_linkagg_member));
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tmp_index+agg_base, cmd_member_w, &ds_linkagg_member));
                    sal_memcpy(p_linkagg->port+tmp_index, p_linkagg->port+tmp_index+1, sizeof(sys_linkagg_port_t));
                }
            }
            else
            {
                if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
                {
                    /*copy the last one to the removed port position,and remove member port from linkagg at tail*/
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + tail_index), cmd_member_r, &ds_linkagg_member));
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + port_index), cmd_member_w, &ds_linkagg_member));
                }
                /* update soft table */
                sal_memcpy(&(p_linkagg->port[port_index]), &(p_linkagg->port[tail_index]), sizeof(sys_linkagg_port_t));
            }

            p_linkagg->port[tail_index].valid = 0;

            /* update padding member */
            if (p_linkagg->need_pad)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_linkagg_add_padding_mem(lchip, p_linkagg, tail_index - 1));
            }
         }

        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_dlb_del_member(lchip, p_linkagg->tid, port_index, tail_index));
        }

        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_hw_sync_del_member(lchip, p_linkagg->tid, lport));
        }
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to check whether the port is member of the linkagg.
*/
STATIC bool
_sys_greatbelt_linkagg_port_is_member(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 gport, uint8* index)
{
    uint8 idx = 0;

    CTC_PTR_VALID_CHECK(p_linkagg);
    CTC_PTR_VALID_CHECK(index);

    for (idx = 0; idx < p_gb_linkagg_master[lchip]->mem_port_num[p_linkagg->tid]; idx++)
    {
        if ((p_linkagg->port[idx].gport == gport) && (p_linkagg->port[idx].valid))
        {
            *index = idx;
            return TRUE;
        }
    }

    return FALSE;
}

STATIC int32
_sys_greatbelt_qmgr_dlb_init(uint8 lchip)
{
    uint32 value = 0;
    uint32 cmd = 0;
    ds_dlb_token_thrd_t dlb_token;
    uint32 rate = 0;
    uint32 token_rate = 0;
    uint32 rate_frac = 0;
    uint32 step = 0;

    sal_memset(&dlb_token, 0, sizeof(ds_dlb_token_thrd_t));

    value = 1;
    cmd = DRV_IOW(QMgrDlbCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /* cfg 1g threshold for dlb */
    rate = 1000000;
    CTC_ERROR_RETURN(sys_greatbelt_qos_shape_token_rate_compute(lchip, rate, 0, 0, &token_rate, &rate_frac));
    step = token_rate/6;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "1g rate: 0x%x, step:0x%x\n", token_rate, step);

    /* cfg DsDlbTokenThrd for 1g */
    dlb_token.token_thrd0 = step*6;
    dlb_token.token_thrd1 = step*5;
    dlb_token.token_thrd2 = step*4;
    dlb_token.token_thrd3 = step*3;
    dlb_token.token_thrd4 = step*2;
    dlb_token.token_thrd5 = step;
    dlb_token.token_thrd6 = 0;

    cmd = DRV_IOW(DsDlbTokenThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_LINKAGG_DLB_TOKEN_THRD_INDEX_1G, cmd, &dlb_token));

    /* cfg DsDlbTokenThrd for 10g */
    rate = 10000000;
    CTC_ERROR_RETURN(sys_greatbelt_qos_shape_token_rate_compute(lchip, rate, 0, 0, &token_rate, &rate_frac));
    step = token_rate/6;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "10g rate: 0x%x, step:0x%x\n", token_rate, step);

    dlb_token.token_thrd0 = step*6;
    dlb_token.token_thrd1 = step*5;
    dlb_token.token_thrd2 = step*4;
    dlb_token.token_thrd3 = step*3;
    dlb_token.token_thrd4 = step*2;
    dlb_token.token_thrd5 = step;
    dlb_token.token_thrd6 = 0;

    cmd = DRV_IOW(DsDlbTokenThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_LINKAGG_DLB_TOKEN_THRD_INDEX_SG, cmd, &dlb_token));

    return CTC_E_NONE;
}

/**
 @brief The function is to init the linkagg module
*/
int32
sys_greatbelt_linkagg_init(uint8 lchip, void* linkagg_global_cfg)
{
    uint8 gchip = 0;
    uint16 agg_idx = 0;
    uint16 mem_idx = 0;
    uint16 index = 0;
    uint32 core_frequecy = 0;
    uint32 linkagg_interval = SYS_DLB_TIMER_INTERVAL; /*s*/
    uint16 max_phy_ptr0 = (SYS_LINKAGG_ALL_MEM_NUM - 1);
    uint8  ts_threshold0 = SYS_DLB_TS_THRES;
    uint16 max_ptr0 = SYS_DLB_MAX_PTR;
    uint16 min_ptr0 = 0;
    uint32 cmd_group = 0;
    uint32 cmd_timer = 0;
    uint32 cmd_linkagg = 0;
    uint32 cmd_chan_port = 0;
    ctc_linkagg_global_cfg_t* p_linkagg_global_cfg = NULL;

    /* port linkagg */
    ds_link_aggregate_group_t ds_linkagg_group;
    ds_link_aggregate_member_t ds_linkagg_member;
    /* for dynamic balance */
    q_link_agg_timer_ctl0_t q_linkagg_timer_ctl0;

    /* channel linkagg */
    ds_link_aggregation_port_t ds_linkagg_port;

    uint8 linkagg_num = 0;
    uint8 mem_num = 0;

    CTC_PTR_VALID_CHECK(linkagg_global_cfg);

    p_linkagg_global_cfg = (ctc_linkagg_global_cfg_t*)linkagg_global_cfg;

    core_frequecy = sys_greatbelt_get_core_freq(lchip);

    switch (p_linkagg_global_cfg->linkagg_mode)
    {
        case CTC_LINKAGG_MODE_4:
            linkagg_num = 4;
            break;

        case CTC_LINKAGG_MODE_8:
            linkagg_num = 8;
            break;

        case CTC_LINKAGG_MODE_16:
            linkagg_num = 16;
            break;

        case CTC_LINKAGG_MODE_32:
            linkagg_num = 32;
            break;

        case CTC_LINKAGG_MODE_64:
            linkagg_num = 64;
            break;

        default:
            return CTC_E_LINKAGG_INVALID_LINKAGG_MODE;
    }

    if (NULL != p_gb_linkagg_master[lchip])
    {
        return CTC_E_NONE;
    }

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkagg_num: 0x%x\n", linkagg_num);

    /*init soft table*/
    p_gb_linkagg_master[lchip] =
        (sys_linkagg_master_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(sys_linkagg_master_t));
    if (NULL == p_gb_linkagg_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_linkagg_master[lchip], 0, sizeof(sys_linkagg_master_t));

    SYS_LINKAGG_LOCK_INIT(lchip);

    p_gb_linkagg_master[lchip]->linkagg_num = linkagg_num;

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;

    p_gb_linkagg_master[lchip]->linkagg_mode = p_linkagg_global_cfg->linkagg_mode;
    p_gb_linkagg_master[lchip]->mem_port_num = (uint8*)mem_malloc(MEM_LINKAGG_MODULE, linkagg_num * sizeof(uint8));
    if (NULL == p_gb_linkagg_master[lchip]->mem_port_num)
    {
        sal_mutex_destroy(p_gb_linkagg_master[lchip]->p_linkagg_mutex);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_linkagg_master[lchip]->mem_port_num, 0, linkagg_num * sizeof(uint8));

    p_gb_linkagg_master[lchip]->p_linkagg_vector = ctc_vector_init(4, linkagg_num / 4);
    if (NULL == p_gb_linkagg_master[lchip]->p_linkagg_vector)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Allocate vector for linkagg fail!\n");
        sal_mutex_destroy(p_gb_linkagg_master[lchip]->p_linkagg_mutex);
        mem_free(p_gb_linkagg_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    ctc_vector_reserve(p_gb_linkagg_master[lchip]->p_linkagg_vector, 1);

    /*init asic table*/
    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));
    sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
    sal_memset(&ds_linkagg_port, 0, sizeof(ds_link_aggregation_port_t));
    sal_memset(&q_linkagg_timer_ctl0, 0, sizeof(q_link_agg_timer_ctl0_t));
    q_linkagg_timer_ctl0.max_phy_ptr0 = max_phy_ptr0;
    q_linkagg_timer_ctl0.max_ptr0 = max_ptr0;
    q_linkagg_timer_ctl0.min_ptr0 = min_ptr0;
    q_linkagg_timer_ctl0.update_en0 = 1;

    q_linkagg_timer_ctl0.ts_threshold0 = ts_threshold0;
    q_linkagg_timer_ctl0.update_threshold0 =
        (linkagg_interval * (core_frequecy * 1000000 / DOWN_FRE_RATE)) / ((max_ptr0 - min_ptr0 + 1) * ts_threshold0);

    cmd_timer = DRV_IOW(QLinkAggTimerCtl0_t, DRV_ENTRY_FLAG);
    cmd_group = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    cmd_linkagg = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    cmd_chan_port = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);


    /* init linkagg */
    mem_idx = SYS_LINKAGG_ALL_MEM_NUM;


    sys_greatbelt_get_gchip_id(lchip, &gchip);
    ds_linkagg_member.dest_chip_id = gchip;

    /* init port linkAgg group info */
    for (agg_idx = 0; agg_idx < linkagg_num; agg_idx++)
    {
        ds_linkagg_group.link_agg_mem_base = agg_idx * mem_num;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, agg_idx, cmd_group, &ds_linkagg_group));
    }

    /* init dlb timer control */
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_timer, &q_linkagg_timer_ctl0));

    /* init port linkagg member info */
    for (index = 0; index < mem_idx; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd_linkagg, &ds_linkagg_member));
    }

    /* init channel linkagg port info */
    for (index = 0; index < SYS_GB_MAX_PORT_NUM_PER_CHIP; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd_chan_port, &ds_linkagg_port));
    }

    /* init QMgrDlbCtl for dlb */
    _sys_greatbelt_qmgr_dlb_init(lchip);


    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_GROUP_NUM, p_gb_linkagg_master[lchip]->linkagg_num));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_MEMBER_NUM, mem_num));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_FLOW_NUM, mem_num));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_MEMBER_NUM, SYS_MAX_DLB_MEMBER_NUM));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_GROUP_NUM, p_gb_linkagg_master[lchip]->linkagg_num));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_linkagg_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_linkagg_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_linkagg_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free linkagg*/
    ctc_vector_traverse(p_gb_linkagg_master[lchip]->p_linkagg_vector, (vector_traversal_fn)_sys_greatbelt_linkagg_free_node_data, NULL);
    ctc_vector_release(p_gb_linkagg_master[lchip]->p_linkagg_vector);

    mem_free(p_gb_linkagg_master[lchip]->mem_port_num);

    sal_mutex_destroy(p_gb_linkagg_master[lchip]->p_linkagg_mutex);
    mem_free(p_gb_linkagg_master[lchip]);

    return CTC_E_NONE;
}

/**
 @brief The function is to create one linkagg
*/
int32
sys_greatbelt_linkagg_create(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp)
{
    uint8 mem_idx = 0;
    uint8 tid = 0;
    uint32 cmd_group = 0;
    uint32 cmd_member = 0;
    int32  ret = CTC_E_NONE;
    sys_linkagg_t* p_linkagg = NULL;
    uint8 mem_num = 0;
    uint8 gchip = 0;
    uint32 agg_base = 0;
    ds_link_aggregate_group_t ds_linkagg_group;
    ds_link_aggregate_member_t ds_linkagg_member;

    /* sanity check */
    CTC_PTR_VALID_CHECK(p_linkagg_grp);
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(p_linkagg_grp->tid);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId = 0x%x", tid);

    /* static mode not support CTC_LINKAGG_MODE_4 */
    if ((p_linkagg_grp->linkagg_mode != CTC_LINKAGG_MODE_DLB) && (p_gb_linkagg_master[lchip]->linkagg_num == 4))
    {
        return CTC_E_LINKAGG_INVALID_LINKAGG_MODE;
    }
    if(CTC_FLAG_ISSET(p_linkagg_grp->flag, CTC_LINKAGG_GROUP_FLAG_MEM_ASCEND_ORDER) && p_linkagg_grp->linkagg_mode != CTC_LINKAGG_MODE_STATIC)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Only static mode support member in ascend order\n");
        return CTC_E_INVALID_PARAM;
    }
    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));
    tid = p_linkagg_grp->tid;

    /* do create */
    LINKAGG_LOCK;

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, p_linkagg_grp->tid);
    if (NULL != p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg has already exist!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_LINKAGG_HAS_EXIST, p_gb_linkagg_master[lchip]->p_linkagg_mutex);
    }

    p_linkagg = (sys_linkagg_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(sys_linkagg_t));
    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create new linkagg group!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_NO_MEMORY, p_gb_linkagg_master[lchip]->p_linkagg_mutex);
    }

    sal_memset(p_linkagg, 0, sizeof(sys_linkagg_t));

    p_linkagg->port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, mem_num * sizeof(sys_linkagg_port_t));
    if (NULL == p_linkagg->port)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create linkagg member port group!\n");
        ret = CTC_E_NO_MEMORY;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_2);
    }

    sal_memset(p_linkagg->port, 0, (mem_num*sizeof(sys_linkagg_port_t)));

    /* init linkAgg group info */
    p_linkagg->tid = p_linkagg_grp->tid;
    p_linkagg->port_cnt = 0;
    p_linkagg->linkagg_mode = p_linkagg_grp->linkagg_mode;
    p_linkagg->flag = p_linkagg_grp->flag;

    for (mem_idx = 0; mem_idx < mem_num; mem_idx++)
    {
        p_linkagg->port[mem_idx].valid = FALSE;
    }

    if (FALSE == ctc_vector_add(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid, (void*)p_linkagg))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Add linkagg to vector error, create linkagg fail!\n");
        ret = CTC_E_UNEXPECT;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }


    if (CTC_LINKAGG_MODE_DLB == p_linkagg_grp->linkagg_mode)
    {
        /* dynamic balance */
        cmd_group = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, tid, cmd_group, &ds_linkagg_group);
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);

        if (p_gb_linkagg_master[lchip]->linkagg_mode != CTC_LINKAGG_MODE_64)
        {
            ds_linkagg_group.link_agg_flow_num = p_gb_linkagg_master[lchip]->linkagg_mode;
        }
        else
        {
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Not support 64 linkagg group when use dynamic load balance!\n");
            ret = CTC_E_UNEXPECT;
            SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
        }
        ds_linkagg_group.link_agg_port_remap_en = 1;
        ds_linkagg_group.link_agg_port_member_ptr = tid;
        cmd_group = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);

        ret = DRV_IOCTL(lchip, tid, cmd_group, &ds_linkagg_group);
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);

    }

    /* when create linkagg and no member, need do special for oam */
    cmd_group = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, tid, cmd_group, &ds_linkagg_group);
    SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);

    ds_linkagg_group.link_agg_mem_num = 1;
    agg_base = tid * mem_num;

    cmd_group = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, tid, cmd_group, &ds_linkagg_group);
    SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);

    if (CTC_LINKAGG_MODE_DLB != p_linkagg_grp->linkagg_mode)
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
        ds_linkagg_member.dest_chip_id = gchip;
        ds_linkagg_member.dest_queue = SYS_RESERVE_PORT_ID_DROP;
        cmd_member = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, agg_base, cmd_member, &ds_linkagg_member);
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }
    else    /* flush all DsLinkAggregateMemberSet_t to reverve channel for dlb mode */
    {
        ret = _sys_greatbelt_linkagg_reverve_member(lchip, tid);
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }

    /* for Hw Sync Mode */
    if (CTC_LINKAGG_MODE_STATIC_FAILOVER == p_linkagg_grp->linkagg_mode)
    {
        ret = _sys_greatbelt_linkagg_hw_sync(lchip);
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }

    LINKAGG_UNLOCK;

    ret = sys_greatbelt_brguc_nh_create(lchip, CTC_MAP_TID_TO_GPORT(tid), CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    if (CTC_E_ENTRY_EXIST != ret && CTC_E_NONE != ret)
    {
        mem_free(p_linkagg->port);
        mem_free(p_linkagg);
        return ret;
    }

    ret = sys_greatbelt_l2_set_dsmac_for_linkagg_hw_learning(lchip, CTC_MAP_TID_TO_GPORT(tid),TRUE);
    if(ret < 0)
    {
        mem_free(p_linkagg->port);
        mem_free(p_linkagg);
        return ret;
    }

    return CTC_E_NONE;

ERROR_1:
    mem_free(p_linkagg->port);

ERROR_2:
    mem_free(p_linkagg);
    LINKAGG_UNLOCK;
    return ret;

}

/**
 @brief The function is to delete one linkagg
*/
int32
sys_greatbelt_linkagg_destroy(uint8 lchip, uint8 tid)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip = 0;
    uint8 lport = 0;
    uint8 agg_idx = 0;
    uint32 agg_base = 0;
    /* for iterator and empty linkagg num*/
    uint32 member_num = 0;
    uint32 cmd_mem = 0;
    uint32 cmd_remapen = 0;
    uint32 remapen = 0;
    uint32 cmd_agg = 0;
    ds_link_aggregate_member_t ds_linkagg_member;
    sys_linkagg_t* p_linkagg = NULL;
    uint8 mem_num = 0;

    /*sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId = 0x%x\n", tid);

    /*do remove*/
    LINKAGG_LOCK;
    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    p_linkagg = ctc_vector_del(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    for (member_num = 0; member_num < p_gb_linkagg_master[lchip]->mem_port_num[tid]; member_num++)
    {
        gchip = SYS_MAP_GPORT_TO_GCHIP(p_linkagg->port[member_num].gport);
        lport = CTC_MAP_GPORT_TO_LPORT(p_linkagg->port[member_num].gport);

        if (FALSE == sys_greatbelt_chip_is_local(lchip, gchip))
        {
            continue;
        }

        ret = sys_greatbelt_port_set_global_port(lchip, lport, CTC_MAP_LPORT_TO_GPORT(gchip, lport));
        if (ret < 0)
        {
            ret = CTC_E_UNEXPECT;
            goto OUT;
        }
        sys_greatbelt_vlan_dft_entry_change_for_linkagg(lchip, p_linkagg->port[member_num].gport, CTC_MAP_TID_TO_GPORT(tid), FALSE, TRUE);
    }

    p_gb_linkagg_master[lchip]->mem_port_num[tid] = 0;

    /*recover asic table */
    member_num = 1;
    sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));

    cmd_mem = DRV_IOW(DsLinkAggregateGroup_t, DsLinkAggregateGroup_LinkAggMemNum_f);
    cmd_remapen = DRV_IOW(DsLinkAggregateGroup_t, DsLinkAggregateGroup_LinkAggPortRemapEn_f);
    cmd_agg = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);

    agg_base = tid * mem_num;

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    ds_linkagg_member.dest_chip_id = gchip;
    ds_linkagg_member.dest_queue = SYS_RESERVE_PORT_ID_DROP;
    if ((ret = DRV_IOCTL(lchip, tid, cmd_mem, &member_num)) < 0)
    {
        ret = CTC_E_HW_OP_FAIL;
        goto OUT;
    }

    if ((ret = DRV_IOCTL(lchip, tid, cmd_remapen, &remapen)) < 0)
    {
        ret = CTC_E_HW_OP_FAIL;
        goto OUT;
    }

    for (agg_idx = 0; agg_idx < mem_num; agg_idx++)
    {
        ret = DRV_IOCTL(lchip, (agg_base + agg_idx), cmd_agg, &ds_linkagg_member);
        if (ret  < 0)
        {
            ret = CTC_E_HW_OP_FAIL;
            goto OUT;
        }
    }

    if (NULL != p_linkagg)
    {
        mem_free(p_linkagg->port);
        mem_free(p_linkagg);
    }

    CTC_ERROR_RETURN(sys_greatbelt_l2_set_dsmac_for_linkagg_hw_learning(lchip, CTC_MAP_TID_TO_GPORT(tid),FALSE));

OUT:
    LINKAGG_UNLOCK;
    return ret;
}

/**
 @brief The function is to add a port to linkagg
*/
int32
sys_greatbelt_linkagg_add_port(uint8 lchip, uint8 tid, uint16 gport)
{
    uint8 index = 0;
    uint8 gchip = 0;
    uint8 lport = 0;
    uint16 agg_gport = 0;
    int32 ret = CTC_E_NONE;
    sys_linkagg_t* p_linkagg = NULL;
    /* max member num per linkagg group */
    uint8 mem_num = 0;
    uint8 mem_base = 0;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));
    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*do add*/
    LINKAGG_LOCK;
    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    /*check if the port is member of linkagg*/
    if (TRUE == _sys_greatbelt_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The port is exist in linkagg group, add member port fail!\n");
        ret = CTC_E_MEMBER_PORT_EXIST;
        goto OUT;
    }

    if (((!(p_linkagg->linkagg_mode)) && (mem_num == p_gb_linkagg_master[lchip]->mem_port_num[tid])) ||
        ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gb_linkagg_master[lchip]->mem_port_num[tid] == SYS_MAX_DLB_MEMBER_NUM)))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member of linkagg group reach Max, add member port fail!\n");
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }
    if(CTC_FLAG_ISSET(p_linkagg->flag, CTC_LINKAGG_GROUP_FLAG_MEM_ASCEND_ORDER))
    {
        uint16 tmp_index = p_gb_linkagg_master[lchip]->mem_port_num[tid];
        ds_link_aggregate_member_t  hw_member;
        uint32 cmdr = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        uint32 cmdw = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        mem_base = (p_linkagg->tid) * mem_num;
        for(; tmp_index > 0; tmp_index--)
        {
            if((gport >= p_linkagg->port[tmp_index-1].gport) && p_linkagg->port[tmp_index-1].valid)
            {
                break;
            }
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+mem_base-1, cmdr, &hw_member), p_gb_linkagg_master[lchip]->p_linkagg_mutex);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+mem_base, cmdw, &hw_member), p_gb_linkagg_master[lchip]->p_linkagg_mutex);

            sal_memcpy(p_linkagg->port+tmp_index, p_linkagg->port+tmp_index-1, sizeof(sys_linkagg_port_t));
        }

        index = tmp_index;
    }
    else
    {
        /*get the first unused pos*/
        if ((ret = _sys_greatbelt_linkagg_get_first_unused_pos(lchip, p_linkagg, &index)) < 0)
        {
            goto OUT;
        }
    }

    p_linkagg->port[index].gport = gport;
    p_linkagg->port[index].valid = 1;
    p_linkagg->port[index].pad_flag = 0;

    /*
     * member num 1~8:   member num in linkagg group:1~8
     * member num 9~15:  member num in linkagg group:9
     * member num 16~31: member num in linkagg group:10
     * member num 32~63: member num in linkagg group:11
     */
    (p_gb_linkagg_master[lchip]->mem_port_num[tid])++;
    if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)++;
        p_linkagg->need_pad = 0;
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_2)
    {
        p_linkagg->port_cnt = 9;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 9)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_3)
    {
        p_linkagg->port_cnt = 10;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 17)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_4)
    {
        p_linkagg->port_cnt = 11;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 33)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }

    /* dlb mode do not need pad */
    if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
    {
        p_linkagg->need_pad = 0;
    }

    /*write asic table*/
    if ((ret = _sys_greatbelt_linkagg_update_table(lchip, p_linkagg, TRUE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        goto OUT;
    }

    /*for the first member in dlb mode ,need flush active */
    if ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 1))
    {
        ret = _sys_greatbelt_linkagg_clear_flow_active(lchip, tid);
        if (CTC_E_NONE != ret)
        {
            goto OUT;
        }
    }

    if (TRUE == sys_greatbelt_chip_is_local(lchip, gchip))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        agg_gport = CTC_MAP_TID_TO_GPORT(tid);
        sys_greatbelt_port_set_global_port(lchip, lport, agg_gport);
    }

    sys_greatbelt_vlan_dft_entry_change_for_linkagg(lchip, gport, CTC_MAP_TID_TO_GPORT(tid), TRUE, FALSE);


OUT:
    LINKAGG_UNLOCK;
    return ret;
}

/**
 @brief The function is to remove the port from linkagg
*/
int32
sys_greatbelt_linkagg_remove_port(uint8 lchip, uint8 tid, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint8 gchip = 0;
    uint8 lport = 0;
    bool port_is_member = 0;
    sys_linkagg_t* p_linkagg = NULL;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));
    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    LINKAGG_LOCK;
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    if (0 == p_gb_linkagg_master[lchip]->mem_port_num[tid])
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member num of linkagg is zero, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }

    /*check if port is a member of linkagg*/
    if (FALSE == _sys_greatbelt_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The port is not exist in linkagg group, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }


    (p_gb_linkagg_master[lchip]->mem_port_num[tid])--;
    if (p_gb_linkagg_master[lchip]->mem_port_num[tid] < SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 8)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 16)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 32)
    {
        (p_linkagg->port_cnt)--;
    }
    else
    {
        /* keep port_cnt not change */
    }

    if ((p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB) && (p_linkagg->port_cnt > 8))
    {
        p_linkagg->need_pad = 1;
    }
    else
    {
        p_linkagg->need_pad = 0;
    }

    /*write asic table*/
    if ((ret = _sys_greatbelt_linkagg_update_table(lchip, p_linkagg, FALSE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        goto OUT;
    }

    /* for dlb mode, when remove member, need flush flow active */
    if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
    {
        ret = _sys_greatbelt_linkagg_clear_flow_active(lchip, p_linkagg->tid);
        if (CTC_E_NONE != ret)
        {
            goto OUT;
        }
    }

    port_is_member = _sys_greatbelt_linkagg_port_is_member(lchip, p_linkagg, gport, &index);
    /*set local port's global_port*/
    if ((TRUE == sys_greatbelt_chip_is_local(lchip, gchip))
        && (FALSE == port_is_member))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        sys_greatbelt_port_set_global_port(lchip, lport, gport);
    }
    if (FALSE == port_is_member)
    {
        sys_greatbelt_vlan_dft_entry_change_for_linkagg(lchip, gport, CTC_MAP_TID_TO_GPORT(tid), FALSE,FALSE);
    }

OUT:
    LINKAGG_UNLOCK;
    return ret;
}

/**
 @brief The function is to get the a local member port of linkagg
*/
int32
sys_greatbelt_linkagg_get_1st_local_port(uint8 lchip, uint8 tid, uint32* p_gport, uint16* local_cnt)
{
    int32 index = 0;
    uint8 gchip_id = 0;
    sys_linkagg_t* p_linkagg = NULL;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gport);
    CTC_PTR_VALID_CHECK(local_cnt);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*do it*/
    LINKAGG_LOCK;
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    *local_cnt = 0;

    if (NULL == p_linkagg)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_LINKAGG_NOT_EXIST, p_gb_linkagg_master[lchip]->p_linkagg_mutex);
    }

    for (index = 0; index < (p_gb_linkagg_master[lchip]->mem_port_num[tid]); index++)
    {
        gchip_id = SYS_MAP_GPORT_TO_GCHIP(p_linkagg->port[index].gport);

        if (TRUE == sys_greatbelt_chip_is_local(lchip, gchip_id))
        {
            *p_gport = (p_linkagg->port[index].gport);
            (*local_cnt)++;
        }
    }

    if (*local_cnt > 0)
    {
        LINKAGG_UNLOCK;
        return CTC_E_NONE;
    }

    LINKAGG_UNLOCK;
    return CTC_E_LOCAL_PORT_NOT_EXIST;
}

/**
 @brief The function is to show member ports of linkagg.
*/
int32
sys_greatbelt_linkagg_show_ports(uint8 lchip, uint8 tid, uint32* p_gports, uint16* cnt)
{
    uint8 idx = 0;
    sys_linkagg_t* p_linkagg = NULL;

    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);
    CTC_PTR_VALID_CHECK(p_gports);
    CTC_PTR_VALID_CHECK(cnt);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        return CTC_E_LINKAGG_NOT_EXIST;
    }

    for (idx = 0; idx < p_gb_linkagg_master[lchip]->mem_port_num[tid]; idx++)
    {
        p_gports[idx] = p_linkagg->port[idx].gport;
    }

    *cnt = p_gb_linkagg_master[lchip]->mem_port_num[tid];

    return CTC_E_NONE;
}

/**
 @brief The function is to show member ports of linkagg.
*/
int32
sys_greatbelt_linkagg_show_all_member(uint8 lchip, uint8 tid, sys_linkagg_port_t** p_linkagg_port, uint8* dyn_mode)
{
    sys_linkagg_t* p_linkagg_temp = NULL;

    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);
    CTC_PTR_VALID_CHECK(p_linkagg_port);
    CTC_PTR_VALID_CHECK(dyn_mode);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_linkagg_temp = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg_temp)
    {
        return CTC_E_LINKAGG_NOT_EXIST;
    }

    *p_linkagg_port = p_linkagg_temp->port;
    *dyn_mode = p_linkagg_temp->linkagg_mode;
    return CTC_E_NONE;
}

/**
 @brief The function is to set member of linkagg hash key
*/
int32
sys_greatbelt_linkagg_set_psc(uint8 lchip, ctc_linkagg_psc_t* psc)
{
    parser_ethernet_ctl_t eth_ctl;
    parser_ip_hash_ctl_t ip_ctl;
    parser_layer4_hash_ctl_t l4_ctl;
    parser_pbb_ctl_t pbb_ctl;
    parser_mpls_ctl_t mpls_ctl;
    parser_fcoe_ctl_t  fcoe_ctl;
    parser_trill_ctl_t  trill_ctl;
    uint32 cmd = 0;
    uint32 value = 0;

    CTC_PTR_VALID_CHECK(psc);

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_L2)
    {
        sal_memset(&eth_ctl, 0, sizeof(parser_ethernet_ctl_t));
        cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_ctl));

        eth_ctl.vlan_hash_en = (psc->l2_flag & CTC_LINKAGG_PSC_L2_VLAN) ? 1 : 0;
        eth_ctl.vlan_hash_mode = (psc->l2_flag & CTC_LINKAGG_PSC_L2_DOUBLE_VLAN) ? 0 : 1;
        eth_ctl.cos_hash_en = (psc->l2_flag & CTC_LINKAGG_PSC_L2_COS) ? 1 : 0;
        eth_ctl.layer2_header_protocol_hash_en = (psc->l2_flag & CTC_LINKAGG_PSC_L2_ETHERTYPE) ? 1 : 0;
        eth_ctl.chip_id_hash_en = (psc->l2_flag & CTC_LINKAGG_PSC_L2_PORT) ? 1 : 0;
        eth_ctl.port_hash_en = (psc->l2_flag & CTC_LINKAGG_PSC_L2_PORT) ? 1 : 0;

        if (CTC_FLAG_ISSET(psc->l2_flag, CTC_LINKAGG_PSC_L2_MACSA))
        {
            CTC_BIT_UNSET(eth_ctl.mac_hash_disable, 2);
        }
        else
        {
            CTC_BIT_SET(eth_ctl.mac_hash_disable, 2);
        }

        if (CTC_FLAG_ISSET(psc->l2_flag, CTC_LINKAGG_PSC_L2_MACDA))
        {
            CTC_BIT_UNSET(eth_ctl.mac_hash_disable, 3);
        }
        else
        {
            CTC_BIT_SET(eth_ctl.mac_hash_disable, 3);
        }

        cmd = DRV_IOW(ParserEthernetCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_ctl));

        value = 0;

        cmd = DRV_IOW(ParserIpHashCtl_t, ParserIpHashCtl_UseIpHash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        cmd = DRV_IOW(ParserLayer4HashCtl_t, ParserLayer4HashCtl_UseLayer4Hash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        cmd = DRV_IOW(ParserMplsCtl_t, ParserMplsCtl_UseMplsHash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        cmd = DRV_IOW(ParserFcoeCtl_t, ParserFcoeCtl_UseFcoeHash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        cmd = DRV_IOW(ParserTrillCtl_t, ParserTrillCtl_UsetrillHash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_IP)
    {
        sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
        cmd = DRV_IOR(ParserIpHashCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ip_ctl));

        ip_ctl.use_ip_hash = (psc->ip_flag & CTC_LINKAGG_PSC_USE_IP) ? 1 : 0;
        ip_ctl.ip_protocol_hash_en = (psc->ip_flag & CTC_LINKAGG_PSC_IP_PROTOCOL) ? 1 : 0;
        if (CTC_FLAG_ISSET(psc->ip_flag, CTC_LINKAGG_PSC_IP_IPSA))
        {
            CTC_BIT_UNSET(ip_ctl.ip_ip_hash_disable, 2);
        }
        else
        {
            CTC_BIT_SET(ip_ctl.ip_ip_hash_disable, 2);
        }

        if (CTC_FLAG_ISSET(psc->ip_flag, CTC_LINKAGG_PSC_IP_IPDA))
        {
            CTC_BIT_UNSET(ip_ctl.ip_ip_hash_disable, 3);
        }
        else
        {
            CTC_BIT_SET(ip_ctl.ip_ip_hash_disable, 3);
        }

        cmd = DRV_IOW(ParserIpHashCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ip_ctl));

        value = 0;
        cmd = DRV_IOW(ParserLayer4HashCtl_t, ParserLayer4HashCtl_UseLayer4Hash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_PBB)
    {
        sal_memset(&pbb_ctl, 0, sizeof(parser_pbb_ctl_t));
        cmd = DRV_IOR(ParserPbbCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pbb_ctl));

        pbb_ctl.pbb_vlan_hash_en = (psc->pbb_flag & CTC_LINKAGG_PSC_PBB_BVLAN) ? 1 : 0;
        pbb_ctl.pbb_cos_hash_en = (psc->pbb_flag & CTC_LINKAGG_PSC_PBB_BCOS) ? 1 : 0;
        if (CTC_FLAG_ISSET(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACSA))
        {
            CTC_BIT_UNSET(pbb_ctl.c_mac_hash_disable, 2);
        }
        else
        {
            CTC_BIT_SET(pbb_ctl.c_mac_hash_disable, 2);
        }

        if (CTC_FLAG_ISSET(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACDA))
        {
            CTC_BIT_UNSET(pbb_ctl.c_mac_hash_disable, 3);
        }
        else
        {
            CTC_BIT_SET(pbb_ctl.c_mac_hash_disable, 3);
        }

        cmd = DRV_IOW(ParserPbbCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pbb_ctl));

    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_MPLS)
    {
        sal_memset(&mpls_ctl, 0, sizeof(parser_mpls_ctl_t));
        cmd = DRV_IOR(ParserMplsCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mpls_ctl));

        mpls_ctl.use_mpls_hash = (psc->mpls_flag & CTC_LINKAGG_PSC_USE_MPLS) ? 1 : 0;
        mpls_ctl.mpls_protocol_hash_en = (psc->mpls_flag & CTC_LINKAGG_PSC_MPLS_PROTOCOL) ? 1 : 0;
        if (CTC_FLAG_ISSET(psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPSA))
        {
            CTC_BIT_UNSET(mpls_ctl.mpls_ip_hash_disable, 2);
        }
        else
        {
            CTC_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 2);
        }

        if (CTC_FLAG_ISSET(psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPDA))
        {
            CTC_BIT_UNSET(mpls_ctl.mpls_ip_hash_disable, 3);
        }
        else
        {
            CTC_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 3);
        }

        cmd = DRV_IOW(ParserMplsCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mpls_ctl));

        value = 0;
        cmd = DRV_IOW(ParserLayer4HashCtl_t, ParserLayer4HashCtl_UseLayer4Hash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_FCOE)
    {
        sal_memset(&fcoe_ctl, 0, sizeof(parser_fcoe_ctl_t));
        cmd = DRV_IOR(ParserFcoeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fcoe_ctl));

        fcoe_ctl.use_fcoe_hash = (psc->fcoe_flag & CTC_LINKAGG_PSC_USE_FCOE) ? 1 : 0;
        fcoe_ctl.fcoe_agg_hash_en = 0;
        if (CTC_FLAG_ISSET(psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_SID))
        {
            CTC_BIT_SET(fcoe_ctl.fcoe_agg_hash_en, 0);
        }
        else
        {
            CTC_BIT_UNSET(fcoe_ctl.fcoe_agg_hash_en, 0);
        }

        if (CTC_FLAG_ISSET(psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_DID))
        {
            CTC_BIT_SET(fcoe_ctl.fcoe_agg_hash_en, 1);
        }
        else
        {
            CTC_BIT_UNSET(fcoe_ctl.fcoe_agg_hash_en, 1);
        }

        cmd = DRV_IOW(ParserFcoeCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fcoe_ctl));

        value = 0;
        cmd = DRV_IOW(ParserLayer4HashCtl_t, ParserLayer4HashCtl_UseLayer4Hash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_TRILL)
    {
        sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));
        cmd = DRV_IOR(ParserTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trill_ctl));

        trill_ctl.usetrill_hash = (psc->trill_flag & CTC_LINKAGG_PSC_USE_TRILL) ? 1 : 0;
        trill_ctl.trill_agg_hash_en = 0;
        if (CTC_FLAG_ISSET(psc->trill_flag, CTC_LINKAGG_PSC_TRILL_INGRESS_NICKNAME))
        {
            CTC_BIT_SET(trill_ctl.trill_agg_hash_en, 0);
        }
        else
        {
            CTC_BIT_UNSET(trill_ctl.trill_agg_hash_en, 0);
        }

        if (CTC_FLAG_ISSET(psc->trill_flag, CTC_LINKAGG_PSC_TRILL_EGRESS_NICKNAME))
        {
            CTC_BIT_SET(trill_ctl.trill_agg_hash_en, 1);
        }
        else
        {
            CTC_BIT_UNSET(trill_ctl.trill_agg_hash_en, 1);
        }

        cmd = DRV_IOW(ParserTrillCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trill_ctl));

        value = 0;
        cmd = DRV_IOW(ParserLayer4HashCtl_t, ParserLayer4HashCtl_UseLayer4Hash_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_L4)
    {
        sal_memset(&l4_ctl, 0, sizeof(parser_layer4_hash_ctl_t));
        cmd = DRV_IOR(ParserLayer4HashCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l4_ctl));

        l4_ctl.use_layer4_hash = (psc->l4_flag & CTC_LINKAGG_PSC_USE_L4) ? 1 : 0;
        l4_ctl.source_port_hash_en = (psc->l4_flag & CTC_LINKAGG_PSC_L4_SRC_PORT) ? 1 : 0;
        l4_ctl.dest_port_hash_en = (psc->l4_flag & CTC_LINKAGG_PSC_L4_DST_PORT) ? 1 : 0;
        l4_ctl.gre_key_hash_en = (psc->l4_flag & CTC_LINKAGG_PSC_L4_GRE_KEY) ? 1 : 0;
        cmd = DRV_IOW(ParserLayer4HashCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l4_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to get member of linkagg hash key
*/
int32
sys_greatbelt_linkagg_get_psc(uint8 lchip, ctc_linkagg_psc_t* psc)
{
    parser_ethernet_ctl_t eth_ctl;
    parser_ip_hash_ctl_t ip_ctl;
    parser_layer4_hash_ctl_t l4_ctl;
    parser_pbb_ctl_t pbb_ctl;
    parser_mpls_ctl_t mpls_ctl;
    parser_fcoe_ctl_t  fcoe_ctl;
    parser_trill_ctl_t  trill_ctl;
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(psc);

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_L2)
    {
        sal_memset(&eth_ctl, 0, sizeof(parser_ethernet_ctl_t));
        cmd = DRV_IOR(ParserEthernetCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_ctl));

        psc->l2_flag = 0;
        if (eth_ctl.vlan_hash_en)
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_VLAN);
        }

        if (eth_ctl.cos_hash_en)
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_COS);
        }

        if (eth_ctl.layer2_header_protocol_hash_en)
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_ETHERTYPE);
        }

        if (!eth_ctl.vlan_hash_mode)
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_DOUBLE_VLAN);
        }

        if (!CTC_IS_BIT_SET(eth_ctl.mac_hash_disable, 2))
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_MACSA);
        }

        if (!CTC_IS_BIT_SET(eth_ctl.mac_hash_disable, 3))
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_MACDA);
        }

        if ((eth_ctl.port_hash_en) && (eth_ctl.chip_id_hash_en))
        {
            CTC_SET_FLAG(psc->l2_flag, CTC_LINKAGG_PSC_L2_PORT);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_IP)
    {
        sal_memset(&ip_ctl, 0, sizeof(parser_ip_hash_ctl_t));
        cmd = DRV_IOR(ParserIpHashCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ip_ctl));

        psc->ip_flag = 0;
        if (ip_ctl.use_ip_hash)
        {
            CTC_SET_FLAG(psc->ip_flag, CTC_LINKAGG_PSC_USE_IP);
        }

        if (ip_ctl.ip_protocol_hash_en)
        {
            CTC_SET_FLAG(psc->ip_flag, CTC_LINKAGG_PSC_IP_PROTOCOL);
        }

        if (!CTC_IS_BIT_SET(ip_ctl.ip_ip_hash_disable, 2))
        {
            CTC_SET_FLAG(psc->ip_flag, CTC_LINKAGG_PSC_IP_IPSA);
        }

        if (!CTC_IS_BIT_SET(ip_ctl.ip_ip_hash_disable, 3))
        {
            CTC_SET_FLAG(psc->ip_flag, CTC_LINKAGG_PSC_IP_IPDA);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_L4)
    {
        sal_memset(&l4_ctl, 0, sizeof(parser_layer4_hash_ctl_t));
        cmd = DRV_IOR(ParserLayer4HashCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l4_ctl));

        psc->l4_flag = 0;
        if (l4_ctl.use_layer4_hash)
        {
            CTC_SET_FLAG(psc->l4_flag, CTC_LINKAGG_PSC_USE_L4);
        }

        if (l4_ctl.source_port_hash_en)
        {
            CTC_SET_FLAG(psc->l4_flag, CTC_LINKAGG_PSC_L4_SRC_PORT);
        }

        if (l4_ctl.dest_port_hash_en)
        {
            CTC_SET_FLAG(psc->l4_flag, CTC_LINKAGG_PSC_L4_DST_PORT);
        }

        if (l4_ctl.gre_key_hash_en)
        {
            CTC_SET_FLAG(psc->l4_flag, CTC_LINKAGG_PSC_L4_GRE_KEY);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_PBB)
    {
        sal_memset(&pbb_ctl, 0, sizeof(parser_pbb_ctl_t));
        cmd = DRV_IOR(ParserPbbCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pbb_ctl));

        psc->pbb_flag = 0;
        if (pbb_ctl.pbb_vlan_hash_en)
        {
            CTC_SET_FLAG(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BVLAN);
        }

        if (pbb_ctl.pbb_cos_hash_en)
        {
            CTC_SET_FLAG(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BCOS);
        }

        if (!CTC_IS_BIT_SET(pbb_ctl.c_mac_hash_disable, 2))
        {
            CTC_SET_FLAG(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACSA);
        }

        if (!CTC_IS_BIT_SET(pbb_ctl.c_mac_hash_disable, 3))
        {
            CTC_SET_FLAG(psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACDA);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_MPLS)
    {
        sal_memset(&mpls_ctl, 0, sizeof(parser_mpls_ctl_t));
        cmd = DRV_IOR(ParserMplsCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mpls_ctl));

        psc->mpls_flag = 0;
        if (mpls_ctl.use_mpls_hash)
        {
            CTC_SET_FLAG(psc->mpls_flag, CTC_LINKAGG_PSC_USE_MPLS);
        }

        if (mpls_ctl.mpls_protocol_hash_en)
        {
            CTC_SET_FLAG(psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_PROTOCOL);
        }

        if (!CTC_IS_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 2))
        {
            CTC_SET_FLAG(psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPSA);
        }

        if (!CTC_IS_BIT_SET(mpls_ctl.mpls_ip_hash_disable, 3))
        {
            CTC_SET_FLAG(psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPDA);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_FCOE)
    {
        sal_memset(&fcoe_ctl, 0, sizeof(parser_fcoe_ctl_t));
        cmd = DRV_IOR(ParserFcoeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fcoe_ctl));

        psc->fcoe_flag = 0;
        if (fcoe_ctl.use_fcoe_hash)
        {
            CTC_SET_FLAG(psc->fcoe_flag, CTC_LINKAGG_PSC_USE_FCOE);
        }

        if (CTC_IS_BIT_SET(fcoe_ctl.fcoe_agg_hash_en, 0))
        {
            CTC_SET_FLAG(psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_SID);
        }

        if (CTC_IS_BIT_SET(fcoe_ctl.fcoe_agg_hash_en, 1))
        {
            CTC_SET_FLAG(psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_DID);
        }
    }

    if (psc->psc_type_bitmap & CTC_LINKAGG_PSC_TYPE_TRILL)
    {
        sal_memset(&trill_ctl, 0, sizeof(parser_trill_ctl_t));
        cmd = DRV_IOR(ParserTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &trill_ctl));

        psc->trill_flag = 0;
        if (trill_ctl.usetrill_hash)
        {
            CTC_SET_FLAG(psc->trill_flag, CTC_LINKAGG_PSC_USE_TRILL);
        }

        if (CTC_IS_BIT_SET(trill_ctl.trill_agg_hash_en, 0))
        {
            CTC_SET_FLAG(psc->trill_flag, CTC_LINKAGG_PSC_TRILL_INGRESS_NICKNAME);
        }

        if (CTC_IS_BIT_SET(trill_ctl.trill_agg_hash_en, 1))
        {
            CTC_SET_FLAG(psc->trill_flag, CTC_LINKAGG_PSC_TRILL_EGRESS_NICKNAME);
        }
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to get member of linkagg
*/
int32
sys_greatbelt_linkagg_get_max_mem_num(uint8 lchip, uint16* max_num)
{
    uint8 mem_num = 0;

    SYS_LINKAGG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(max_num);

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;
    *max_num = mem_num;

    return CTC_E_NONE;
}

/**
 @brief The function is to set QLinkAggTimerCtl0 for linkagg dlb flow inactive
*/
int32
sys_greatbelt_linkagg_set_flow_inactive_interval(uint8 lchip, uint16 max_ptr, uint32 update_cycle, uint8 inactive_round_num)
{
    uint16 min_ptr0 = 0;
    uint32 cmd = 0;
    uint32 core_frequecy = 0;
    uint32 update_threshold0 = 0;
    q_link_agg_timer_ctl0_t q_linkagg_timer_ctl0;

    SYS_LINKAGG_INIT_CHECK(lchip);

    sal_memset(&q_linkagg_timer_ctl0, 0, sizeof(q_link_agg_timer_ctl0_t));

    core_frequecy = sys_greatbelt_get_core_freq(0);
    update_threshold0 = (SYS_DLB_TIMER_INTERVAL * (core_frequecy * 1000000)) / ((SYS_DLB_MAX_PTR - min_ptr0 + 1) * SYS_DLB_TS_THRES);

    cmd = DRV_IOR(QLinkAggTimerCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_linkagg_timer_ctl0));

    q_linkagg_timer_ctl0.max_ptr0 = max_ptr ? max_ptr : SYS_DLB_MAX_PTR;
    q_linkagg_timer_ctl0.ts_threshold0 = inactive_round_num ? inactive_round_num : SYS_DLB_TS_THRES;
    q_linkagg_timer_ctl0.update_threshold0 = update_cycle ? update_cycle : update_threshold0;

    cmd = DRV_IOW(QLinkAggTimerCtl0_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_linkagg_timer_ctl0));

    return CTC_E_NONE;
}

bool
sys_greatbelt_linkagg_port_is_lag_member(uint8 lchip, uint16 gport, uint16* agg_gport)
{
    uint8 lport = 0;
    uint8 gchip = 0;
    uint16 gport_temp = 0;
    int32 ret =0;

    SYS_LINKAGG_INIT_CHECK(lchip);

    ret = sys_greatbelt_port_dest_gport_check(lchip, gport);
    if (ret < 0)
    {
        return FALSE;
    }

    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    if (TRUE == sys_greatbelt_chip_is_local(lchip, gchip))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        sys_greatbelt_port_get_global_port(lchip, lport, &gport_temp);
        if (CTC_IS_LINKAGG_PORT(gport_temp))
        {
            *agg_gport = gport_temp;
            return  TRUE;
        }
    }

    return  FALSE;
}

/** number of linkagg member in vlan*/
int32
sys_greatbelt_linkagg_get_mem_num_in_vlan(uint8 lchip, uint16 vlan_ptr, uint16 agg_gport)
{
    uint8 mem_num = 0;
    uint8 idx = 0;
    int32 valid_cnt = 0;
    uint8 tid = 0;
    uint8 lport = 0;
    sys_linkagg_t* p_linkagg_temp = NULL;

    mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;

    tid = CTC_GPORT_LINKAGG_ID(agg_gport);
    p_linkagg_temp = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg_temp)
    {
        return 0;
    }
    for (idx = 0; idx < mem_num; idx++)
    {
        if (p_linkagg_temp->port[idx].valid)
        {
            if (FALSE == sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_linkagg_temp->port[idx].gport)))
            {
                continue;
            }

            lport = CTC_MAP_GPORT_TO_LPORT(p_linkagg_temp->port[idx].gport);
            if (TRUE == _sys_greatbelt_vlan_is_member_port(lchip, vlan_ptr, lport))
            {
                valid_cnt++;
            }
        }
    }

    return  valid_cnt;
}

STATIC int32
_sys_greatbelt_linkagg_replace_port(uint8 lchip, sys_linkagg_t* p_linkagg, uint8 tid, uint16 gport)
{
    uint16   lport      = 0;
    uint16   agg_gport  = 0;
    int32    ret        = CTC_E_NONE;
    uint16   index      = p_gb_linkagg_master[lchip]->mem_port_num[tid];

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n", tid, gport);

    p_linkagg->port[index].gport = gport;
    p_linkagg->port[index].valid = 1;
    p_linkagg->port[index].pad_flag = 0;

    /*
     * member num 1~24:   member num in linkagg group:1~24
     * member num 25~32:  member num in linkagg group:25
     * member num 33~64:  member num in linkagg group:26
     * member num 65~128: member num in linkagg group:27
     */
    (p_gb_linkagg_master[lchip]->mem_port_num[tid])++;
    if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)++;
        p_linkagg->need_pad = 0;
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_2)
    {
        p_linkagg->port_cnt = 9;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 9)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_3)
    {
        p_linkagg->port_cnt = 10;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 17)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gb_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_4)
    {
        p_linkagg->port_cnt = 11;
        if ((p_gb_linkagg_master[lchip]->mem_port_num[tid]) == 33)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }

    /* dlb mode do not need pad */
    if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
    {
        p_linkagg->need_pad = 0;
    }

    /*write asic table*/
    if ((ret = _sys_greatbelt_linkagg_update_table(lchip, p_linkagg, TRUE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        return ret;
    }

    /*for the first member in dlb mode ,need flush active */
    if ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gb_linkagg_master[lchip]->mem_port_num[tid] == 1))
    {
        ret = _sys_greatbelt_linkagg_clear_flow_active(lchip, tid);
        if (CTC_E_NONE != ret)
        {
            return ret;
        }
    }

    if (TRUE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport)))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        agg_gport = CTC_MAP_TID_TO_GPORT(tid);
        sys_greatbelt_port_set_global_port(lchip, lport, agg_gport);
    }

    sys_greatbelt_vlan_dft_entry_change_for_linkagg(lchip, gport, CTC_MAP_TID_TO_GPORT(tid), TRUE, FALSE);

    return CTC_E_NONE;
}

int32
sys_greatbelt_linkagg_replace_ports(uint8 lchip, uint8 tid, uint32* gport, uint16 mem_num)
{
    uint16          index        = 0;
    uint16          index1       = 0;
    uint16          max_mem_num  = 0;
    uint8           gchip        = 0;
    int32           ret          = 0;
    sys_linkagg_t*  p_linkagg    = NULL;
    sys_linkagg_port_t* p_port   = NULL;
    uint16  pre_port_cnt         = 0;
    uint8  lport                 = 0;

    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (gport == NULL)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*gchip check for per gport*/
    for (index = 0; index < mem_num; index++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport[index]));
        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport[index]);
        SYS_GLOBAL_CHIPID_CHECK(gchip);
    }

    LINKAGG_LOCK;
    p_linkagg = ctc_vector_get(p_gb_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    /* max member num per linkagg group */
    max_mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gb_linkagg_master[lchip]->linkagg_num;

    if (((!(p_linkagg->linkagg_mode)) && (max_mem_num < mem_num)) ||
        ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (mem_num > SYS_MAX_DLB_MEMBER_NUM)))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member of linkagg group reach Max, add member port fail!\n");
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }

    /*p_port save the previous info of p_linkagg */
    p_port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, max_mem_num*sizeof(sys_linkagg_port_t));
    if (p_port == NULL)
    {
        ret = CTC_E_NO_MEMORY;
        goto OUT;
    }
    sal_memcpy(p_port, p_linkagg->port, max_mem_num * sizeof(sys_linkagg_port_t));

    for (index1 = 0; index1 < p_gb_linkagg_master[lchip]->mem_port_num[tid]; index1++)
    {
        for (index = 0; index < mem_num; index++)
        {
            if (p_linkagg->port[index1].gport == gport[index])
            {
                p_port[index1].valid = 0;
                break;
            }
        }
    }

    /*if mem_num == 0, clear all members of the linkagg group*/
    if ((!mem_num) && (p_linkagg->port_cnt))
    {
        LINKAGG_UNLOCK;
        for (index = 0; index < max_mem_num; index++)
        {
            sys_greatbelt_linkagg_remove_port(lchip, tid, p_linkagg->port[0].gport);
            if (!p_linkagg->port_cnt)
            {
                break;
            }
        }
        sal_memset(p_linkagg->port, 0, max_mem_num*sizeof(sys_linkagg_port_t));
        LINKAGG_LOCK;
    }
    else
    {
        pre_port_cnt = p_gb_linkagg_master[lchip]->mem_port_num[tid];
        p_linkagg->port_cnt = 0;
        p_linkagg->need_pad = 0;
        p_gb_linkagg_master[lchip]->mem_port_num[tid] = 0;
        sal_memset(p_linkagg->port, 0, max_mem_num*sizeof(sys_linkagg_port_t));

        for(index = 0; index < mem_num; index++)
        {
            ret = _sys_greatbelt_linkagg_replace_port(lchip, p_linkagg, tid, gport[index]);
            if (ret < 0)
            {
                mem_free(p_port);
                goto OUT;
            }
        }

        /*clear the different gport info*/
        for (index = 0; index < pre_port_cnt; index++)
        {
            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_port[index].gport);
            lport = CTC_MAP_GPORT_TO_LPORT(p_port[index].gport);
            if (index >= mem_num)
            {
                if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
                {
                    _sys_greatbelt_dlb_del_member(lchip, tid, index, pre_port_cnt);
                }
                if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
                {
                    _sys_greatbelt_hw_sync_del_member(lchip, tid, lport);
                }
            }

            if (TRUE == sys_greatbelt_chip_is_local(lchip, gchip))
            {
                if (!p_port[index].valid)
                {
                    continue;
                }
                sys_greatbelt_port_set_global_port(lchip, lport, p_port[index].gport);
                sys_greatbelt_vlan_dft_entry_change_for_linkagg(lchip, p_port[index].gport, CTC_MAP_TID_TO_GPORT(tid), FALSE,FALSE);
            }
        }
    }
    mem_free(p_port);

    LINKAGG_UNLOCK;
    return CTC_E_NONE;

    OUT:
        LINKAGG_UNLOCK;
        return ret;

}



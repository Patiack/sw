/**
 @file sys_goldengate_linkagg.c

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

#include "sys_goldengate_common.h"
#include "sys_goldengate_linkagg.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

/* dynamic interval */
#define SYS_LINKAGG_DYNAMIC_INTERVAL 5

#define SYS_LINKAGG_GOTO_ERROR(ret, error) \
    do { \
        if (ret < 0) \
        {\
            goto error;\
        } \
       } while (0)
#define SYS_LINKAGG_LOCK_INIT() \
    do { \
        int32 ret = sal_mutex_create(&(p_gg_linkagg_master[lchip]->p_linkagg_mutex)); \
        if (ret || !(p_gg_linkagg_master[lchip]->p_linkagg_mutex)) \
        { \
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Create linkagg mutex fail!\n"); \
            mem_free(p_gg_linkagg_master[lchip]); \
            return CTC_E_FAIL_CREATE_MUTEX; \
        } \
    } while (0)

#define SYS_LINKAGG_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_linkagg_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_TID_VALID_CHECK(tid) \
    if (tid >= p_gg_linkagg_master[lchip]->linkagg_num){return CTC_E_INVALID_TID; }

#define LINKAGG_LOCK \
    if (p_gg_linkagg_master[lchip]->p_linkagg_mutex) sal_mutex_lock(p_gg_linkagg_master[lchip]->p_linkagg_mutex)
#define LINKAGG_UNLOCK \
    if (p_gg_linkagg_master[lchip]->p_linkagg_mutex) sal_mutex_unlock(p_gg_linkagg_master[lchip]->p_linkagg_mutex)


#define SYS_LINKAGG_GET_GROUP_MEMBER_NUM(tid, mem_num) \
    if(p_gg_linkagg_master[lchip]->linkagg_num == 56) \
    { \
        mem_num = (tid > SYS_MODE56_DLB_TID_MAX) ? 16 : 32; \
    } \
    else \
    { \
        mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num; \
    }

#define SYS_LINKAGG_GET_GROUP_MEMBER_BASE(tid, mem_num, mem_base) \
    if((p_gg_linkagg_master[lchip]->linkagg_num == 56) && (tid > SYS_MODE56_DLB_TID_MAX)) \
    { \
        mem_base =  SYS_MODE56_DLB_MEM_MAX + (tid - SYS_MODE56_DLB_TID_MAX -1) * mem_num; \
    } \
    else \
    { \
        mem_base = tid * mem_num; \
    } \

int32
sys_goldengate_linkagg_wb_sync(uint8 lchip);

int32
sys_goldengate_linkagg_wb_restore(uint8 lchip);

STATIC bool
_sys_goldengate_linkagg_port_is_member(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 gport, uint16* index);

extern int32
sys_goldengate_qos_shape_token_rate_compute(uint8 lchip, uint32 rate,
                                            uint8 type,
                                            uint32* p_token_rate,
                                            uint32* p_token_rate_frac);

extern int32
sys_goldengate_l2_set_dsmac_for_linkagg_hw_learning(uint8 lchip, uint16 gport, bool b_add);


extern int32
sys_goldengate_parser_set_linkagg_hash_field(uint8 lchip, ctc_parser_linkagg_hash_ctl_t* p_hash_ctl);

extern int32
sys_goldengate_parser_get_linkagg_hash_field(uint8 lchip, ctc_parser_linkagg_hash_ctl_t* p_hash_ctl);


sys_linkagg_master_t* p_gg_linkagg_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

#define __HW_SYNC__

STATIC int32
_sys_goldengate_hw_sync_add_member(uint8 lchip, uint8 tid, uint16 lport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint32 channel_id = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 value = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    value = tid;
    SetDsLinkAggregateChannel(V, u1_g1_linkAggregationGroup_f, &linkagg_channel, value);
    SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 0);
    SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 1);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_hw_sync_del_member(uint8 lchip, uint8 tid, uint16 lport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint8 channel_id = 0;
    uint8 gchip = 0;
    uint16 gport = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_hw_sync_remove_member(uint8 lchip, uint8 chan_id)
{
    sys_linkagg_t* p_linkagg = NULL;
    uint8 tid = 0;
    uint16 gport = 0;
    uint16 lport = 0;
    uint8 gchip = 0;
    uint16 tail_index = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint32 cmd = 0;
    uint16 index = 0;

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    /* get local phy port by chan_id */
    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);
    SYS_MAX_PHY_PORT_CHECK(lport);

    sys_goldengate_get_gchip_id(lchip, &gchip);
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);

    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel));
    tid = GetDsLinkAggregateChannel(V, u1_g1_linkAggregationGroup_f, &linkagg_channel);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tid:%d channel:%d\n", tid, chan_id);
    /* update softtable */
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        return CTC_E_NONE;
    }

    if (FALSE == _sys_goldengate_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mem not exist! \n");
        return CTC_E_MEMBER_PORT_NOT_EXIST;
    }

    (p_gg_linkagg_master[lchip]->mem_port_num[tid])--;
    (p_linkagg->port_cnt)--;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mem_port_num:%d, port_cnt:%d, gport:%d\n",
        p_gg_linkagg_master[lchip]->mem_port_num[tid], p_linkagg->port_cnt, gport);


    tail_index = p_gg_linkagg_master[lchip]->mem_port_num[tid];

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tail_index:%d \n", tail_index);

    sal_memcpy(&(p_linkagg->port[index]), &(p_linkagg->port[tail_index]), sizeof(sys_linkagg_port_t));
    p_linkagg->port[tail_index].valid = 0;

    /*set local port's global_port*/
    if (TRUE == sys_goldengate_chip_is_local(lchip, gchip))
    {
        sys_goldengate_port_set_global_port(lchip, lport, gport, TRUE);
    }

    return CTC_E_NONE;
}

/* no need this api for gg */
int32
_sys_goldengate_hw_sync_retrieve_member(uint8 lchip, uint8 chan_id)
{
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 is_up = 0;
    uint32 cmd = 0;
    uint8 tid = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint16 lport = 0;
    ds_link_aggregate_group_t ds_linkagg_group;
    uint32 hw_cnt = 0;
    sys_linkagg_t* p_linkagg = NULL;

    /* get local phy port by chan_id */
    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);
    SYS_MAX_PHY_PORT_CHECK(lport);

    sys_goldengate_get_gchip_id(lchip, &gchip);
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "retrive tid:%d channel:%d\n", tid, chan_id);

    sys_goldengate_port_get_mac_link_up(lchip, gport, &is_up, 0);

    if (is_up)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "linkmember need retrieve !%d()\n", chan_id);

        sal_memset(&linkagg_channel, 0, sizeof(DsLinkAggregateChannel_m));
        cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &linkagg_channel));
        tid = GetDsLinkAggregateChannel(V,u1_g1_linkAggregationGroup_f, &linkagg_channel);

        /*get port number from chip */
        cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));
        hw_cnt = GetDsLinkAggregateGroup(V, linkAggMemNum_f, &ds_linkagg_group);

        p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
        if (!p_linkagg)
        {
            return CTC_E_UNEXPECT;
        }

        if (p_linkagg->port_cnt > hw_cnt)
        {
            /* need retrieve */
            sys_goldengate_linkagg_add_port(lchip, tid, gport);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_linkagg_failover_detect(uint8 lchip, uint8 linkdown_chan)
{

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_down:0x%x\n ", linkdown_chan);

    _sys_goldengate_hw_sync_remove_member(lchip, linkdown_chan);

    _sys_goldengate_hw_sync_retrieve_member(lchip, linkdown_chan);

    return CTC_E_NONE;
}

#define __DLB__

STATIC int32
_sys_goldengate_dlb_add_member_channel(uint8 lchip, uint8 tid, uint16 lport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint8 channel_id = 0;
    uint8 gchip = 0;
    uint16 gport = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    SetDsLinkAggregateChannel(V, groupEn_f, &linkagg_channel, 1);
    SetDsLinkAggregateChannel(V, groupType_f, &linkagg_channel, 0);     /* port level */
    SetDsLinkAggregateChannel(V, linkChangeEn_f, &linkagg_channel, 0);
    SetDsLinkAggregateChannel(V, u1_g1_linkAggregationGroup_f, &linkagg_channel, tid);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dlb_del_member_channel(uint8 lchip, uint8 tid, uint16 lport)
{
    uint32 cmd = 0;
    DsLinkAggregateChannel_m linkagg_channel;
    uint8 channel_id = 0;
    uint8 gchip = 0;
    uint32 gport = 0;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = 0x%x lport =  %d \n", tid, lport);

    sal_memset(&linkagg_channel, 0, sizeof(linkagg_channel));

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dlb_add_member(uint8 lchip, uint8 tid, uint8 port_index, uint16 lport)
{
    uint32 cmd = 0;
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    uint8  port_member_ptr = 0;
    DsLinkAggregateGroup_m ds_linkagg_group;
    uint8 gchip = 0;
    uint16 gport = 0;

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_linkagg_group));

    cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));
    port_member_ptr = GetDsLinkAggregateGroup(V, linkAggPortMemberPtr_f, &ds_linkagg_group);

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
    gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);
    dest_channel = SYS_GET_CHANNEL_ID(lchip, gport);
    dest_quene = lport;

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destChannel_f + port_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destQueue_f + port_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_quene));

    CTC_ERROR_RETURN(_sys_goldengate_dlb_add_member_channel(lchip, tid, lport));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_dlb_del_member(uint8 lchip, uint8 tid, uint8 port_index, uint8 tail_index, uint16 lport)
{
    uint32 cmd = 0;
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    uint8  port_member_ptr = 0;
    DsLinkAggregateGroup_m ds_linkagg_group;

    if (tail_index > SYS_MAX_DLB_MEMBER_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_linkagg_group));

    /* get link_agg_port_member_ptr */
    cmd = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd, &ds_linkagg_group));
    port_member_ptr = GetDsLinkAggregateGroup(V, linkAggPortMemberPtr_f, &ds_linkagg_group);

    /* copy the last one to the removed position */
    cmd = DRV_IOR(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destChannel_f+tail_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOR(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destQueue_f+tail_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_quene));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destChannel_f+port_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destQueue_f+port_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_quene));

    /* set the last one to reserve */
    dest_channel = SYS_RSV_PORT_DROP_ID;
    dest_quene = SYS_RSV_PORT_DROP_ID;
    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destChannel_f+tail_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_channel));

    cmd = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destQueue_f+tail_index*2);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd, &dest_quene));

    CTC_ERROR_RETURN(_sys_goldengate_dlb_del_member_channel(lchip, tid, lport));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_linkagg_reverve_member(uint8 lchip, uint8 tid)
{
    uint32 dest_channel = 0;
    uint32 dest_quene = 0;
    uint32 cmd_member_set = 0;
    uint32 cmd_group_r = 0;
    uint8 index = 0;
    uint8 port_member_ptr;
    DsLinkAggregateGroup_m ds_linkagg_group;

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

    dest_channel = SYS_RSV_PORT_DROP_ID;
    dest_quene = SYS_RSV_PORT_DROP_ID;
    cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd_group_r, &ds_linkagg_group));
    port_member_ptr = GetDsLinkAggregateGroup(V, linkAggPortMemberPtr_f, &ds_linkagg_group);

    for (index = 0; index < 16; index++)
    {
        cmd_member_set = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destChannel_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd_member_set, &dest_channel));
        cmd_member_set = DRV_IOW(DsLinkAggregateMemberSet_t, DsLinkAggregateMemberSet_array_0_destQueue_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_member_ptr, cmd_member_set, &dest_quene));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to clear flow active in linkagg group
*/
STATIC int32
_sys_goldengate_linkagg_clear_flow_active(uint8 lchip, uint8 tid)
{
    uint32 cmd_r = 0;
    uint32 cmd_w = 0;
    DsLinkAggregateGroup_m ds_link_aggregate_group;
    DsLinkAggregateMember_m ds_link_aggregate_member;
    uint16 flow_num = 0;
    uint8  flow_num_flag = 0;
    uint32 agg_base = 0;
    uint16 index = 0;

    sal_memset(&ds_link_aggregate_group, 0, sizeof(ds_link_aggregate_group_t));
    sal_memset(&ds_link_aggregate_member, 0, sizeof(ds_link_aggregate_member_t));

    cmd_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, tid, cmd_r, &ds_link_aggregate_group));

    agg_base = GetDsLinkAggregateGroup(V, linkAggMemBase_f, &ds_link_aggregate_group);
    flow_num_flag = GetDsLinkAggregateGroup(V, linkAggFlowNum_f, &ds_link_aggregate_group);

    switch (flow_num_flag)
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
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, agg_base+index, cmd_r, &ds_link_aggregate_member));
        SetDsLinkAggregateMember(V, active_f, &ds_link_aggregate_member, 0);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, agg_base+index, cmd_w, &ds_link_aggregate_member));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to get index of unused member port position
*/
STATIC int32
_sys_goldengate_linkagg_get_first_unused_pos(uint8 lchip, sys_linkagg_t* p_linkagg, uint16* index)
{
    uint16 idx = 0;
    uint16 mem_num = 0;

    CTC_PTR_VALID_CHECK(p_linkagg);
    CTC_PTR_VALID_CHECK(index);

    /* linkagg_num*mem_num = SYS_LINKAGG_ALL_MEM_NUM(1024) for static balance  */
    if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
    {
        SYS_LINKAGG_GET_GROUP_MEMBER_NUM(p_linkagg->tid, mem_num);
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
_sys_goldengate_linkagg_add_padding_mem(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 start_index)
{
    uint16 mem_num = 0;
    uint16 index = 0;
    uint32 cmd_member_w = 0;
    uint32 cmd_member_r = 0;
    uint16 agg_base = 0;
    DsLinkAggregateMember_m ds_linkagg_member;
    uint8 temp = 0;
    uint8 act_mem_num = 0;

    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(p_linkagg->tid, mem_num);
    cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    cmd_member_r = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    SYS_LINKAGG_GET_GROUP_MEMBER_BASE(p_linkagg->tid, mem_num, agg_base);

    sal_memset(&ds_linkagg_member, 0, sizeof(ds_linkagg_member));

    act_mem_num = p_gg_linkagg_master[lchip]->mem_port_num[p_linkagg->tid];


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

uint16
_sys_goldengate_linkagg_get_repeat_member_cnt(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 gport)
{
    uint16 idx = 0;
    uint16 repeat_cnt = 0;
    uint16 max_mem_num = 0;
    CTC_PTR_VALID_CHECK(p_linkagg);
    max_mem_num = (p_gg_linkagg_master[lchip]->linkagg_num == 56)
                ? 16 : (SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num);
    for (idx = 0; idx < max_mem_num; idx++)
    {
        if ((p_linkagg->port[idx].gport == gport) && (p_linkagg->port[idx].valid))
        {
            repeat_cnt++;
        }
    }
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:%d, repeat_cnt:%d\n", gport,repeat_cnt);
    return repeat_cnt;
}

/**
 @brief The function is update asic table, add/remove member port from linkagg.
*/
STATIC int32
_sys_goldengate_linkagg_update_table(uint8 lchip, sys_linkagg_t* p_linkagg, bool is_add_port, uint16 port_index)
{

    uint32 cmd_group_r = 0;
    uint32 cmd_group_w = 0;
    uint32 cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    uint32 cmd_member_r = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    uint32 port_cnt = 0;
    uint16 mem_num = 0;
    uint32 agg_base = 0;
    DsLinkAggregateMember_m ds_linkagg_member;
    DsLinkAggregateGroup_m ds_linkagg_group;
    uint16 lport = 0;
    int32 ret = 0;
    uint8 tail_index = 0;
    uint8 dest_chip_id = 0;

    CTC_PTR_VALID_CHECK(p_linkagg);

    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(p_linkagg->tid, mem_num);

    sal_memset(&ds_linkagg_member, 0, sizeof(ds_link_aggregate_member_t));
    sal_memset(&ds_linkagg_group, 0, sizeof(ds_link_aggregate_group_t));

    SYS_LINKAGG_GET_GROUP_MEMBER_BASE(p_linkagg->tid, mem_num, agg_base);
    tail_index = p_gg_linkagg_master[lchip]->mem_port_num[p_linkagg->tid];

    if (TRUE == is_add_port)
    {
        port_cnt = p_linkagg->port_cnt;

        /*update DsLinkAggregateMember_t */
        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_linkagg->port[port_index].gport);

        if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
        {
            dest_chip_id = SYS_MAP_CTC_GPORT_TO_GCHIP(p_linkagg->port[port_index].gport);
            SetDsLinkAggregateMember(V, destQueue_f, &ds_linkagg_member, lport);
            SetDsLinkAggregateMember(V, destChipId_f, &ds_linkagg_member, dest_chip_id);

            cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + port_index), cmd_member_w, &ds_linkagg_member));
        }
        else
        {
            /* dlb mode need set DsLinkAggregateMemberSet_t */
            CTC_ERROR_RETURN(_sys_goldengate_dlb_add_member(lchip, p_linkagg->tid, port_index, lport));
        }

        /* update DsLinkAggregateGroup_t linkagg port cnt */
        cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_r, &ds_linkagg_group));
        SetDsLinkAggregateGroup(V, linkAggMemNum_f, &ds_linkagg_group, port_cnt);
        cmd_group_w = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_w, &ds_linkagg_group));

        /*update mcast linkagg bitmap*/
        if (TRUE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_linkagg->port[port_index].gport)))
        {
            CTC_ERROR_RETURN(sys_goldengate_port_update_mc_linkagg(lchip, p_linkagg->tid, lport, is_add_port));
        }

        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
        {
            CTC_ERROR_RETURN(_sys_goldengate_hw_sync_add_member(lchip, p_linkagg->tid, lport));
        }

        if (p_linkagg->need_pad)
        {
            ret = _sys_goldengate_linkagg_add_padding_mem(lchip, p_linkagg, p_gg_linkagg_master[lchip]->mem_port_num[p_linkagg->tid]-1);
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
        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_linkagg->port[port_index].gport);

        /*update mcast linkagg bitmap first when remove*/
        if ((TRUE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_linkagg->port[port_index].gport)))
            && (_sys_goldengate_linkagg_get_repeat_member_cnt(lchip, p_linkagg, p_linkagg->port[port_index].gport) <= 1))
        {
            CTC_ERROR_RETURN(sys_goldengate_port_update_mc_linkagg(lchip, p_linkagg->tid, lport, is_add_port));
        }

        /* update DsLinkAggregateGroup_t linkagg port cnt */
        cmd_group_r = DRV_IOR(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_r, &ds_linkagg_group));
        SetDsLinkAggregateGroup(V, linkAggMemNum_f, &ds_linkagg_group, port_cnt);
        cmd_group_w = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_linkagg->tid, cmd_group_w, &ds_linkagg_group));

        if (0 == port_cnt)
        {
            if (p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB)
            {
                cmd_member_w = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, (agg_base + port_index), cmd_member_w, &ds_linkagg_member));
            }
            p_linkagg->port[0].valid = 0;
        }
        else
        {
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
                CTC_ERROR_RETURN(_sys_goldengate_linkagg_add_padding_mem(lchip, p_linkagg, tail_index - 1));
            }
        }


        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
        {
            CTC_ERROR_RETURN(_sys_goldengate_dlb_del_member(lchip, p_linkagg->tid, port_index, tail_index, lport));
        }

        if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
        {
            CTC_ERROR_RETURN(_sys_goldengate_hw_sync_del_member(lchip, p_linkagg->tid, lport));
        }
    }

    return CTC_E_NONE;

}

/**
 @brief The function is to check whether the port is member of the linkagg.
*/
STATIC bool
_sys_goldengate_linkagg_port_is_member(uint8 lchip, sys_linkagg_t* p_linkagg, uint16 gport, uint16* index)
{
    int16 idx = 0;

    CTC_PTR_VALID_CHECK(p_linkagg);
    CTC_PTR_VALID_CHECK(index);

    for (idx = 0; idx < p_gg_linkagg_master[lchip]->mem_port_num[p_linkagg->tid]; idx++)
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
_sys_goldengate_qmgr_dlb_init(uint8 lchip)
{
    uint32 value = 0;
    uint32 cmd = 0;
    /*uint32 core_frequecy = 0;*/
    uint8 chan_id = 0;
    QMgrDreTimerCtl_m qmgr_dre_ctl;
    QWriteCtl_m qwrite_ctl;
    DlbChanMaxLoadByteCnt_m chan_max_load_byte;
    RefDivQMgrEnqDrePulse_m dre_pulse;

    sal_memset(&qmgr_dre_ctl, 0, sizeof(qmgr_dre_ctl));
    sal_memset(&qwrite_ctl, 0, sizeof(qwrite_ctl));
    sal_memset(&chan_max_load_byte, 0, sizeof(chan_max_load_byte));
    sal_memset(&dre_pulse, 0, sizeof(dre_pulse));

    /*core_frequecy = sys_goldengate_get_core_freq(lchip, 0);*/

    /* 1. config QMgrDreTimerCtl_t and RefDivQMgrEnqDrePulse_t for Tp and Rp, if Tp=100us, Rp=1/512,
       than the history load will be subtract to 0 every 50ms */
    SetQMgrDreTimerCtl(V, chanDreUpdEn_f, &qmgr_dre_ctl, 1);
    SetQMgrDreTimerCtl(V, chanDreInterval_f, &qmgr_dre_ctl, 100);
    SetQMgrDreTimerCtl(V, chanDreMaxPtr_f, &qmgr_dre_ctl, 127);
    SetQMgrDreTimerCtl(V, chanMinPtr_f, &qmgr_dre_ctl, 0);
    SetQMgrDreTimerCtl(V, chanMaxPtr_f, &qmgr_dre_ctl, 127);
    /* Rp = 1/(2^chanDreDiscountShift) = 1/512 */
    SetQMgrDreTimerCtl(V, chanDreDiscountShift_f, &qmgr_dre_ctl, 9);

    cmd = DRV_IOW(QMgrDreTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgr_dre_ctl));

    /* Config pulse timer for Tp=100us*/
    value = (625*25) << 8;
    cmd = DRV_IOW(RefDivQMgrEnqDrePulse_t, RefDivQMgrEnqDrePulse_cfgRefDivQMgrEnqDrePulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    value = 0;
    cmd = DRV_IOW(RefDivQMgrEnqDrePulse_t, RefDivQMgrEnqDrePulse_cfgResetDivQMgrEnqDrePulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /* 2. config DlbChanMaxLoadByteCnt_t and shift, unit is 100 usec */
    SetDlbChanMaxLoadByteCnt(V, maxLoadThreshold0_f, &chan_max_load_byte,   12500);    /* 1G */
    SetDlbChanMaxLoadByteCnt(V, maxLoadThreshold1_f, &chan_max_load_byte,  125000);    /* 10G */
    SetDlbChanMaxLoadByteCnt(V, maxLoadThreshold2_f, &chan_max_load_byte,  500000);    /* 40G */
    SetDlbChanMaxLoadByteCnt(V, maxLoadThreshold3_f, &chan_max_load_byte, 1250000);    /* 100G */
    cmd = DRV_IOW(DlbChanMaxLoadByteCnt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_max_load_byte));

    value = 0;
    cmd = DRV_IOW(DlbEngineCtl_t, DlbEngineCtl_chanByteCountShift_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /* config DlbChanMaxLoadType */
    for (chan_id = 0; chan_id < (SYS_MAX_CHANNEL_ID*2); chan_id++)
    {
        CTC_ERROR_RETURN(sys_goldengate_chip_set_dlb_chan_type(lchip, chan_id));
    }

    /* 3. config QWriteCtl_t for dre counter and member move check */
    cmd = DRV_IOR(QWriteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_ctl));

    SetQWriteCtl(V, dlbEn_f, &qwrite_ctl, 1);
    SetQWriteCtl(V, byteCountShift_f, &qwrite_ctl, 0);  /* unit of dre counter */
    SetQWriteCtl(V, portLagMemberMoveCheckEn_f, &qwrite_ctl, 1);
    SetQWriteCtl(V, chanLagMemberMoveCheckEn_f, &qwrite_ctl, 1);
    SetQWriteCtl(V, ipg_f, &qwrite_ctl, 20);     /* for dre ipg */
    SetQWriteCtl(V, chanBandwidthShift_f, &qwrite_ctl, 8);

    cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qwrite_ctl));

    return CTC_E_NONE;
}

/**
 @brief The function is to init the linkagg module
*/
int32
sys_goldengate_linkagg_init(uint8 lchip, void* linkagg_global_cfg)
{
    uint8 gchip = 0;
    uint16 mem_idx = 0;
    uint16 index = 0;
    uint32 core_frequecy = 0;
    uint32 cmd_linkagg = 0;
    uint32 cmd_chan_port = 0;
    uint32 cmdr_chan_port = 0;
    uint32 cmd_timer = 0;
    ctc_linkagg_global_cfg_t* p_linkagg_global_cfg = NULL;
    DsLinkAggregateMember_m ds_linkagg_member;
    QLinkAggTimerCtl0_m q_linkagg_timer_ctl0;
    QLinkAggTimerCtl1_m q_linkagg_timer_ctl1;
    DsLinkAggregationPort_m ds_linkagg_port;
    uint8 linkagg_num = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(linkagg_global_cfg);

    p_linkagg_global_cfg = (ctc_linkagg_global_cfg_t*)linkagg_global_cfg;

    core_frequecy = sys_goldengate_get_core_freq(lchip, 0);

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

        case CTC_LINKAGG_MODE_56:
            linkagg_num = 56;
            break;

        default:
            return CTC_E_LINKAGG_INVALID_LINKAGG_MODE;
    }

    if (NULL != p_gg_linkagg_master[lchip])
    {
        return CTC_E_NONE;
    }

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkagg_num: 0x%x\n", linkagg_num);

    /*init soft table*/
    p_gg_linkagg_master[lchip] =
        (sys_linkagg_master_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(sys_linkagg_master_t));
    if (NULL == p_gg_linkagg_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_linkagg_master[lchip], 0, sizeof(sys_linkagg_master_t));

    SYS_LINKAGG_LOCK_INIT();

    p_gg_linkagg_master[lchip]->linkagg_num = linkagg_num;

    /*mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num;*/

    p_gg_linkagg_master[lchip]->linkagg_mode = p_linkagg_global_cfg->linkagg_mode;
    p_gg_linkagg_master[lchip]->mem_port_num = (uint8*)mem_malloc(MEM_LINKAGG_MODULE, linkagg_num * sizeof(uint8));
    if (NULL == p_gg_linkagg_master[lchip]->mem_port_num)
    {
        sal_mutex_destroy(p_gg_linkagg_master[lchip]->p_linkagg_mutex);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_linkagg_master[lchip]->mem_port_num, 0, linkagg_num * sizeof(uint8));

    p_gg_linkagg_master[lchip]->p_linkagg_vector = ctc_vector_init(4, linkagg_num / 4);
    if (NULL == p_gg_linkagg_master[lchip]->p_linkagg_vector)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Allocate vector for linkagg fail!\n");
        sal_mutex_destroy(p_gg_linkagg_master[lchip]->p_linkagg_mutex);
        mem_free(p_gg_linkagg_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    ctc_vector_reserve(p_gg_linkagg_master[lchip]->p_linkagg_vector, 1);

    /* init asic table */
    sal_memset(&ds_linkagg_member, 0, sizeof(ds_linkagg_member));
    sal_memset(&ds_linkagg_port, 0, sizeof(ds_linkagg_port));
    sal_memset(&q_linkagg_timer_ctl0, 0, sizeof(q_linkagg_timer_ctl0));
    sal_memset(&q_linkagg_timer_ctl1, 0, sizeof(q_linkagg_timer_ctl1));

    /* init dlb flow inactive timer, Tsunit =  updateThreshold0_f * (maxPtr0_f+1) / core_frequecy = 1ms */
    SetQLinkAggTimerCtl0(V, maxPhyPtr0_f, &q_linkagg_timer_ctl0, (SYS_LINKAGG_ALL_MEM_NUM-1));
    SetQLinkAggTimerCtl0(V, maxPtr0_f, &q_linkagg_timer_ctl0, SYS_DLB_MAX_PTR);
    SetQLinkAggTimerCtl0(V, minPtr0_f, &q_linkagg_timer_ctl0, 0);
    SetQLinkAggTimerCtl0(V, updateEn0_f, &q_linkagg_timer_ctl0, 1);
    SetQLinkAggTimerCtl0(V, tsThreshold0_f, &q_linkagg_timer_ctl0, SYS_DLB_TS_THRES);   /* ts unit is 1ms */
    SetQLinkAggTimerCtl0(V, updateThreshold0_f, &q_linkagg_timer_ctl0,      /* must big than 73 */
        (core_frequecy * 1000000 / 1000 / (SYS_DLB_MAX_PTR+1)));

    cmd_timer = DRV_IOW(QLinkAggTimerCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_timer, &q_linkagg_timer_ctl0));

    SetQLinkAggTimerCtl1(V, maxPhyPtr1_f, &q_linkagg_timer_ctl1, (512-1));
    SetQLinkAggTimerCtl1(V, maxPtr1_f, &q_linkagg_timer_ctl1, SYS_DLB_MAX_PTR);
    SetQLinkAggTimerCtl1(V, minPtr1_f, &q_linkagg_timer_ctl1, 0);
    SetQLinkAggTimerCtl1(V, updateEn1_f, &q_linkagg_timer_ctl1, 1);
    SetQLinkAggTimerCtl1(V, tsThreshold1_f, &q_linkagg_timer_ctl1, SYS_DLB_TS_THRES);   /* ts unit is 1ms */
    SetQLinkAggTimerCtl1(V, updateThreshold1_f, &q_linkagg_timer_ctl1,      /* must big than 73 */
        (core_frequecy * 1000000 / 1000 / (SYS_DLB_MAX_PTR+1)));

    cmd_timer = DRV_IOW(QLinkAggTimerCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_timer, &q_linkagg_timer_ctl1));

    /* init port linkagg member info */
    sys_goldengate_get_gchip_id(lchip, &gchip);
    mem_idx = SYS_LINKAGG_ALL_MEM_NUM;

    cmd_linkagg = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
    SetDsLinkAggregateMember(V, destChipId_f, &ds_linkagg_member, gchip);
    for (index = 0; index < mem_idx; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd_linkagg, &ds_linkagg_member));
    }

    /* init channel linkagg port info */
    cmdr_chan_port = DRV_IOR(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    cmd_chan_port = DRV_IOW(DsLinkAggregationPort_t, DRV_ENTRY_FLAG);
    for (index = 0; index < SYS_GG_MAX_PORT_NUM_PER_CHIP; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmdr_chan_port, &ds_linkagg_port));
         /*SetDsLinkAggregationPort(V, linkAggregationChannelGroup_f, &ds_linkagg_port, 0);*/
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd_chan_port, &ds_linkagg_port));
    }
    p_gg_linkagg_master[lchip]->chanagg_grp_num = TABLE_MAX_INDEX(DsLinkAggregateChannelGroup_t) - 1; /*resv for drop*/
    p_gg_linkagg_master[lchip]->chanagg_mem_per_grp = TABLE_MAX_INDEX(DsLinkAggregateChannelMember_t)/p_gg_linkagg_master[lchip]->chanagg_grp_num;

    p_gg_linkagg_master[lchip]->p_chanagg_vector = ctc_vector_init(8, (p_gg_linkagg_master[lchip]->chanagg_grp_num +7)/ 8);
    if (NULL == p_gg_linkagg_master[lchip]->p_chanagg_vector)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Allocate vector for chanagg fail!\n");
        sal_mutex_destroy(p_gg_linkagg_master[lchip]->p_linkagg_mutex);
        mem_free(p_gg_linkagg_master[lchip]);
        return CTC_E_NO_MEMORY;
    }
    ctc_vector_reserve(p_gg_linkagg_master[lchip]->p_chanagg_vector, 1);


    /* init hw sync */
    cmd = DRV_IOW(QWriteCtl_t, QWriteCtl_linkChangeEn_f);
    field_val = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

	cmd = DRV_IOW(QWriteCtl_t, QWriteCtl_chanBandwidthShift_f);
    field_val = 9;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* init QMgrDreTimerCtl_t for dlb */
    CTC_ERROR_RETURN(_sys_goldengate_qmgr_dlb_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_LINKAGG, sys_goldengate_linkagg_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
	  CTC_ERROR_RETURN(sys_goldengate_linkagg_wb_restore(lchip));
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_GROUP_NUM, p_gg_linkagg_master[lchip]->linkagg_num));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_MEMBER_NUM, SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_FLOW_NUM, SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_MEMBER_NUM, SYS_MAX_DLB_MEMBER_NUM));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LINKAGG_DLB_GROUP_NUM, p_gg_linkagg_master[lchip]->linkagg_num));

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_linkagg_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_linkagg_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_linkagg_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free chanagg data*/
    ctc_vector_traverse(p_gg_linkagg_master[lchip]->p_chanagg_vector, (vector_traversal_fn)_sys_goldengate_linkagg_free_node_data, NULL);
    ctc_vector_release(p_gg_linkagg_master[lchip]->p_chanagg_vector);

    /*free linkagg*/
    ctc_vector_traverse(p_gg_linkagg_master[lchip]->p_linkagg_vector, (vector_traversal_fn)_sys_goldengate_linkagg_free_node_data, NULL);
    ctc_vector_release(p_gg_linkagg_master[lchip]->p_linkagg_vector);

    mem_free(p_gg_linkagg_master[lchip]->mem_port_num);

    sal_mutex_destroy(p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    mem_free(p_gg_linkagg_master[lchip]);

    return CTC_E_NONE;
}

/**
 @brief The function is to create one linkagg
*/
int32
sys_goldengate_linkagg_create(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp)
{
    uint16 mem_idx = 0;
    uint8 tid = 0;
    uint8 gchip = 0;
    uint32 cmd_group = 0;
    uint32 cmd_rr = 0;
    uint32 cmd_member = 0;
    int32  ret = CTC_E_NONE;
    sys_linkagg_t* p_linkagg = NULL;
    uint16 mem_num = 0;
    uint16 mem_base = 0;
    DsLinkAggregateGroup_m ds_linkagg_group;
    DsLinkAggregateRrCount_m ds_linkagg_rr;
    DsLinkAggregateMember_m ds_linkagg_member;

    /* sanity check */
    CTC_PTR_VALID_CHECK(p_linkagg_grp);
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(p_linkagg_grp->tid);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId = 0x%x", p_linkagg_grp->tid);
    if((p_linkagg_grp->linkagg_mode == CTC_LINKAGG_MODE_RR) && (p_linkagg_grp->tid > SYS_RR_TID_MAX))
    {
        return CTC_E_INVALID_TID;
    }

    /* static mode not support CTC_LINKAGG_MODE_4 */
    if ((p_linkagg_grp->linkagg_mode != CTC_LINKAGG_MODE_DLB) && (p_gg_linkagg_master[lchip]->linkagg_num == 4))
    {
        return CTC_E_LINKAGG_INVALID_LINKAGG_MODE;
    }

    if((p_linkagg_grp->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gg_linkagg_master[lchip]->linkagg_num == 56)
        && (p_linkagg_grp->tid > SYS_MODE56_DLB_TID_MAX))
    {
        return CTC_E_INVALID_TID;
    }
    if(CTC_FLAG_ISSET(p_linkagg_grp->flag, CTC_LINKAGG_GROUP_FLAG_MEM_ASCEND_ORDER) && p_linkagg_grp->linkagg_mode != CTC_LINKAGG_MODE_STATIC)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Only static mode support member in ascend order\n");
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_linkagg_group));
    sal_memset(&ds_linkagg_rr, 0, sizeof(ds_linkagg_rr));
    sal_memset(&ds_linkagg_member, 0, sizeof(ds_linkagg_member));

    tid = p_linkagg_grp->tid;

    /* do create */
    LINKAGG_LOCK;
    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(tid, mem_num);
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, p_linkagg_grp->tid);
    if (NULL != p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg has already exist!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_LINKAGG_HAS_EXIST, p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    }

    p_linkagg = (sys_linkagg_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(sys_linkagg_t));
    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create new linkagg group!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_NO_MEMORY, p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    }

    sal_memset(p_linkagg, 0, sizeof(sys_linkagg_t));

    p_linkagg->flag = p_linkagg_grp->flag;
    p_linkagg->port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, mem_num * sizeof(sys_linkagg_port_t));
    if (NULL == p_linkagg->port)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create linkagg member port group!\n");
        ret = CTC_E_NO_MEMORY;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_3);
    }

    sal_memset(p_linkagg->port, 0, mem_num*sizeof(sys_linkagg_port_t));

    /* init linkAgg group info */
    p_linkagg->tid = p_linkagg_grp->tid;
    p_linkagg->port_cnt = 0;
    p_linkagg->linkagg_mode = p_linkagg_grp->linkagg_mode;

    for (mem_idx = 0; mem_idx < mem_num; mem_idx++)
    {
        p_linkagg->port[mem_idx].valid = FALSE;
    }

    if (FALSE == ctc_vector_add(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid, (void*)p_linkagg))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Add linkagg to vector error, create linkagg fail!\n");
        ret = CTC_E_NO_MEMORY;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_2);
    }

    SYS_LINKAGG_GET_GROUP_MEMBER_BASE(tid, mem_num, mem_base);

    SetDsLinkAggregateGroup(V, linkAggMemBase_f, &ds_linkagg_group, mem_base);
    if (CTC_LINKAGG_MODE_DLB == p_linkagg_grp->linkagg_mode)
    {
        /* dynamic balance */
        if (p_gg_linkagg_master[lchip]->linkagg_mode != CTC_LINKAGG_MODE_64)
        {
            if(CTC_LINKAGG_MODE_56 == p_gg_linkagg_master[lchip]->linkagg_mode)
            {
                /* dlb in CTC_LINKAGG_MODE_56 only support 32 flow per group */
                SetDsLinkAggregateGroup(V, linkAggFlowNum_f, &ds_linkagg_group, 3);
            }
            else
            {
                SetDsLinkAggregateGroup(V, linkAggFlowNum_f, &ds_linkagg_group, p_gg_linkagg_master[lchip]->linkagg_mode);
            }

            SetDsLinkAggregateGroup(V, linkAggPortRemapEn_f, &ds_linkagg_group, 1);
            SetDsLinkAggregateGroup(V, linkAggPortMemberPtr_f, &ds_linkagg_group, tid);
        }
        else
        {   /* will modify later, not support 64 group if dynamic balance */
             ret = CTC_E_UNEXPECT;
             SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
        }
    }
    else if (CTC_LINKAGG_MODE_RR == p_linkagg_grp->linkagg_mode)
    {
        SetDsLinkAggregateGroup(V, rrEn_f, &ds_linkagg_group, 1);

        if (p_linkagg_grp->flag & CTC_LINKAGG_GROUP_FLAG_RANDOM_RR)
        {
            SetDsLinkAggregateRrCount(V, randomRrEn_f, &ds_linkagg_rr, 1);
        }
        cmd_rr = DRV_IOW(DsLinkAggregateRrCount_t, DRV_ENTRY_FLAG);
        if ((ret = DRV_IOCTL(lchip, tid, cmd_rr, &ds_linkagg_rr)) < 0)
        {
            SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
        }
    }
    cmd_group = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    if ((ret = DRV_IOCTL(lchip, tid, cmd_group, &ds_linkagg_group)) < 0)
    {
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }

    /* init chipid and flush all DsLinkAggregateMemberSet_t to reverve channel for dlb mode */
    if (CTC_LINKAGG_MODE_DLB == p_linkagg_grp->linkagg_mode)
    {
        /* init DsLinkAggregateMember chipid */
        sys_goldengate_get_gchip_id(lchip, &gchip);
        cmd_member = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        SetDsLinkAggregateMember(V, destChipId_f, &ds_linkagg_member, gchip);
        for (mem_idx = mem_base; mem_idx < (mem_base + mem_num); mem_idx++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mem_idx, cmd_member, &ds_linkagg_member));
        }

        if ((ret = _sys_goldengate_linkagg_reverve_member(lchip, tid)) < 0)
        {
            SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
        }
    }


    LINKAGG_UNLOCK;

    sys_goldengate_brguc_nh_create(lchip, CTC_MAP_TID_TO_GPORT(tid), CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    sys_goldengate_l2_set_dsmac_for_linkagg_hw_learning(lchip, CTC_MAP_TID_TO_GPORT(tid),TRUE);

    return CTC_E_NONE;


ERROR_1:
    ctc_vector_del(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
ERROR_2:
    mem_free(p_linkagg->port);
ERROR_3:
    mem_free(p_linkagg);
    LINKAGG_UNLOCK;

    return ret;
}

/**
 @brief The function is to delete one linkagg
*/
int32
sys_goldengate_linkagg_destroy(uint8 lchip, uint8 tid)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip = 0;

    uint16 lport = 0;
    uint32 member_num = 0;
    uint32 cmd_mem = 0;
    sys_linkagg_t* p_linkagg = NULL;
    DsLinkAggregateGroup_m ds_linkagg_group;

    /*sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId = 0x%x\n", tid);

    sal_memset(&ds_linkagg_group, 0, sizeof(ds_linkagg_group));

    /*do remove*/
    LINKAGG_LOCK;
    p_linkagg = ctc_vector_del(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    for (member_num = 0; member_num < p_gg_linkagg_master[lchip]->mem_port_num[tid]; member_num++)
    {
        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_linkagg->port[member_num].gport);
        if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
        {
            continue;
        }

        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_linkagg->port[member_num].gport);
        ret = sys_goldengate_port_update_mc_linkagg(lchip, tid, lport, FALSE);
        if (ret < 0)
        {
            ret = CTC_E_UNEXPECT;
            goto OUT;
        }

        lport = CTC_MAP_GPORT_TO_LPORT(p_linkagg->port[member_num].gport);
        ret = sys_goldengate_port_set_global_port(lchip, lport, CTC_MAP_LPORT_TO_GPORT(gchip, lport), FALSE);
        if (ret < 0)
        {
            ret = CTC_E_UNEXPECT;
            goto OUT;
        }
    }

    p_gg_linkagg_master[lchip]->mem_port_num[tid] = 0;

    /*reset linkagg group */
    cmd_mem = DRV_IOW(DsLinkAggregateGroup_t, DRV_ENTRY_FLAG);
    if ((ret = DRV_IOCTL(lchip, tid, cmd_mem, &ds_linkagg_group)) < 0)
    {
        ret = CTC_E_HW_OP_FAIL;
        goto OUT;
    }

    if (NULL != p_linkagg)
    {
        mem_free(p_linkagg->port);
        mem_free(p_linkagg);
    }

    CTC_ERROR_RETURN(sys_goldengate_l2_set_dsmac_for_linkagg_hw_learning(lchip, CTC_MAP_TID_TO_GPORT(tid),FALSE));

OUT:
    LINKAGG_UNLOCK;
    sys_goldengate_brguc_nh_delete(lchip, CTC_MAP_TID_TO_GPORT(tid));
    return ret;

}

/**
 @brief The function is to add a port to linkagg
*/
int32
sys_goldengate_linkagg_add_port(uint8 lchip, uint8 tid, uint16 gport)
{
    uint16 index = 0;
    uint8 gchip = 0;

    uint16 lport = 0;
    uint16 agg_gport = 0;
    int32 ret = CTC_E_NONE;
    sys_linkagg_t* p_linkagg = NULL;
    /* max member num per linkagg group */
    uint16 mem_num = 0;
    uint16 mem_base = 0;
    uint32 cmd = 0;
    uint32 mux_type = 0;
    uint8  channel_id = 0xff;
    uint8  is_local_chip = 0;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);

    CTC_GLOBAL_PORT_CHECK(gport);
    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*do add*/
    LINKAGG_LOCK;
    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(tid, mem_num);
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    is_local_chip  =  sys_goldengate_chip_is_local(lchip, gchip);
    if (CTC_LINKAGG_MODE_DLB == p_linkagg->linkagg_mode || CTC_LINKAGG_MODE_STATIC_FAILOVER == p_linkagg->linkagg_mode)
    {
        if(!is_local_chip || (CTC_MAP_GPORT_TO_LPORT(gport)&0xFF) >= SYS_INTERNAL_PORT_START || channel_id == 0xFF)
        {
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Invalid localphy port \n");
            ret =  CTC_E_INVALID_PORT;
            goto OUT;
        }
        cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, channel_id, cmd, &mux_type), ret, OUT);
        if (mux_type > 0)
        {
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "DLB or FailOver group do not support mux port!\n");
            ret =  CTC_E_INVALID_PARAM;
            goto OUT;
        }
    }

    /*check if the port is member of linkagg*/
    if (TRUE == _sys_goldengate_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The port is exist in linkagg group, add member port fail!\n");
        ret = CTC_E_MEMBER_PORT_EXIST;
        goto OUT;
    }

    if (((!(p_linkagg->linkagg_mode)) && (mem_num == p_gg_linkagg_master[lchip]->mem_port_num[tid])) ||
        ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gg_linkagg_master[lchip]->mem_port_num[tid] == SYS_MAX_DLB_MEMBER_NUM)))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member of linkagg group reach Max, add member port fail!\n");
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }

    /* for rr, only support 24 member */
    if ((p_gg_linkagg_master[lchip]->mem_port_num[tid] >= SYS_LINKAGG_MEM_NUM_1)
        && (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_RR))
    {
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }

    if(CTC_FLAG_ISSET(p_linkagg->flag, CTC_LINKAGG_GROUP_FLAG_MEM_ASCEND_ORDER))
    {
        uint16 tmp_index = p_gg_linkagg_master[lchip]->mem_port_num[tid];
        DsLinkAggregateMember_m  hw_member;
        uint32 cmdr = DRV_IOR(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        uint32 cmdw = DRV_IOW(DsLinkAggregateMember_t, DRV_ENTRY_FLAG);
        SYS_LINKAGG_GET_GROUP_MEMBER_BASE(tid, mem_num, mem_base);
        for(; tmp_index > 0; tmp_index--)
        {
            if((gport >= p_linkagg->port[tmp_index-1].gport) && p_linkagg->port[tmp_index-1].valid)
            {
                break;
            }
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+mem_base-1, cmdr, &hw_member), p_gg_linkagg_master[lchip]->p_linkagg_mutex);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, tmp_index+mem_base, cmdw, &hw_member), p_gg_linkagg_master[lchip]->p_linkagg_mutex);

            sal_memcpy(p_linkagg->port+tmp_index, p_linkagg->port+tmp_index-1, sizeof(sys_linkagg_port_t));
        }

        index = tmp_index;
    }
    else
    {
        /*get the first unused pos*/
        if ((ret = _sys_goldengate_linkagg_get_first_unused_pos(lchip, p_linkagg, &index)) < 0)
        {
            goto OUT;
        }
    }

    p_linkagg->port[index].gport = gport;
    p_linkagg->port[index].valid = 1;
    p_linkagg->port[index].pad_flag = 0;

    /*
     * member num 1~24:   member num in linkagg group:1~24
     * member num 25~32:  member num in linkagg group:25
     * member num 33~64:  member num in linkagg group:26
     * member num 65~128: member num in linkagg group:27
     */
    (p_gg_linkagg_master[lchip]->mem_port_num[tid])++;
    if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)++;
        p_linkagg->need_pad = 0;
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_2)
    {
        p_linkagg->port_cnt = 25;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 25)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_3)
    {
        p_linkagg->port_cnt = 26;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 33)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_4)
    {
        p_linkagg->port_cnt = 27;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 65)
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
    if ((ret = _sys_goldengate_linkagg_update_table(lchip, p_linkagg, TRUE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        goto OUT;
    }

    /*for the first member in dlb mode ,need flush active */
    if ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gg_linkagg_master[lchip]->mem_port_num[tid] == 1))
    {
        ret = _sys_goldengate_linkagg_clear_flow_active(lchip, tid);
        if (CTC_E_NONE != ret)
        {
            goto OUT;
        }
    }

    if (TRUE == sys_goldengate_chip_is_local(lchip, gchip))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        agg_gport = CTC_MAP_TID_TO_GPORT(tid);
        sys_goldengate_port_set_global_port(lchip, lport, agg_gport, FALSE);
    }

OUT:
    LINKAGG_UNLOCK;
    return ret;
}

/**
 @brief The function is to remove the port from linkagg
*/
int32
sys_goldengate_linkagg_remove_port(uint8 lchip, uint8 tid, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint16 index = 0;
    uint8 gchip = 0;

    uint16 lport = 0;
    sys_linkagg_t* p_linkagg = NULL;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);

    CTC_GLOBAL_PORT_CHECK(gport);
    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    LINKAGG_LOCK;
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);

    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    if (0 == p_gg_linkagg_master[lchip]->mem_port_num[tid])
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member num of linkagg is zero, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }

    /*check if port is a member of linkagg*/
    if (FALSE == _sys_goldengate_linkagg_port_is_member(lchip, p_linkagg, gport, &index))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The port is not exist in linkagg group, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }

    (p_gg_linkagg_master[lchip]->mem_port_num[tid])--;
    if (p_gg_linkagg_master[lchip]->mem_port_num[tid] < SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] == 24)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] == 32)
    {
        (p_linkagg->port_cnt)--;
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] == 64)
    {
        (p_linkagg->port_cnt)--;
    }
    else
    {
        /* keep port_cnt not change */
    }

    if ((p_linkagg->linkagg_mode != CTC_LINKAGG_MODE_DLB) && (p_linkagg->port_cnt > 24))
    {
        p_linkagg->need_pad = 1;
    }
    else
    {
        p_linkagg->need_pad = 0;
    }

    /*write asic table*/
    if ((ret = _sys_goldengate_linkagg_update_table(lchip, p_linkagg, FALSE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        goto OUT;
    }

    /*set local port's global_port*/
    if ((TRUE == sys_goldengate_chip_is_local(lchip, gchip))
        && (FALSE == _sys_goldengate_linkagg_port_is_member(lchip, p_linkagg, gport, &index)))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        sys_goldengate_port_set_global_port(lchip, lport, gport, FALSE);
    }

OUT:
    LINKAGG_UNLOCK;
    return ret;
}

/**
 @brief The function is to get the a local member port of linkagg
*/
int32
sys_goldengate_linkagg_get_1st_local_port(uint8 lchip, uint8 tid, uint32* p_gport, uint16* local_cnt)
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
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
    *local_cnt = 0;

    if (NULL == p_linkagg)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_LINKAGG_NOT_EXIST, p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    }

    for (index = 0; index < (p_gg_linkagg_master[lchip]->mem_port_num[tid]); index++)
    {
        gchip_id = SYS_MAP_CTC_GPORT_TO_GCHIP(p_linkagg->port[index].gport);

        if (TRUE == sys_goldengate_chip_is_local(lchip, gchip_id))
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
sys_goldengate_linkagg_show_ports(uint8 lchip, uint8 tid, uint32* p_gports, uint16* cnt)
{
    uint16 idx = 0;
    sys_linkagg_t* p_linkagg = NULL;

    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);
    CTC_PTR_VALID_CHECK(p_gports);
    CTC_PTR_VALID_CHECK(cnt);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        return CTC_E_LINKAGG_NOT_EXIST;
    }

    for (idx = 0; idx < p_gg_linkagg_master[lchip]->mem_port_num[tid]; idx++)
    {
        p_gports[idx] = p_linkagg->port[idx].gport;
    }

    *cnt = p_gg_linkagg_master[lchip]->mem_port_num[tid];

    return CTC_E_NONE;
}

/**
 @brief The function is to show member ports of linkagg.
*/
int32
sys_goldengate_linkagg_show_all_member(uint8 lchip, uint8 tid)
{
    sys_linkagg_t* p_linkagg_temp = NULL;
    uint8 mem_num = 0;
    uint8 idx = 0;
    uint16 gport = 0;
    char* mode[4] = {"static", "static-failover", "round-robin", "dynamic"};

    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_TID_VALID_CHECK(tid);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_linkagg_temp = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg_temp)
    {
        return CTC_E_LINKAGG_NOT_EXIST;
    }

    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(tid, mem_num);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s: %u\n", "tid", tid);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s: %s\n", "mode", mode[p_linkagg_temp->linkagg_mode]);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s: %u\n", "count", mem_num);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-11s%s\n", "No.", "Member-Port");
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------\n");

    for (idx = 0; idx < mem_num; idx++)
    {
        if (0 == p_linkagg_temp->port[idx].valid)
        {
            continue;
        }
        gport = p_linkagg_temp->port[idx].gport;
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-11u0x%04X\n", idx + 1, gport);
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to set member of linkagg hash key
*/
int32
sys_goldengate_linkagg_set_psc(uint8 lchip, ctc_linkagg_psc_t* p_psc)
{
    uint32 value = 0;
    ctc_parser_linkagg_hash_ctl_t hash_ctl;

    SYS_LINKAGG_INIT_CHECK(lchip);

    sal_memset(&hash_ctl, 0, sizeof(ctc_parser_linkagg_hash_ctl_t));

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_INNER))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_INNER);
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_L2))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L2);

        value = CTC_FLAG_ISSET(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_VLAN);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_VID : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_COS);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_COS : 0));
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_CTAG_COS : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag ,CTC_LINKAGG_PSC_L2_ETHERTYPE);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_ETHERTYPE : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_DOUBLE_VLAN);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_STAG_VID : 0));
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_CTAG_VID : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_MACSA);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACSA : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag,  CTC_LINKAGG_PSC_L2_MACDA);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_MACDA : 0));

        value = CTC_FLAG_ISSET(p_psc->l2_flag,  CTC_LINKAGG_PSC_L2_PORT);
        CTC_SET_FLAG(hash_ctl.l2_flag, (value ? CTC_PARSER_L2_HASH_FLAGS_PORT: 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_IP))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_IP);

        value = CTC_FLAG_ISSET(p_psc->ip_flag, CTC_LINKAGG_PSC_IP_PROTOCOL);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_PROTOCOL : 0));

        value = CTC_FLAG_ISSET(p_psc->ip_flag, CTC_LINKAGG_PSC_IP_IPSA);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_IPSA : 0));

        value = CTC_FLAG_ISSET(p_psc->ip_flag ,CTC_LINKAGG_PSC_IP_IPDA);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_IPDA : 0));

        value = CTC_FLAG_ISSET(p_psc->ip_flag, CTC_LINKAGG_PSC_IP_DSCP);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_DSCP : 0));

        value = CTC_FLAG_ISSET(p_psc->ip_flag, CTC_LINKAGG_PSC_IPV6_FLOW_LABEL);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_IPV6_FLOW_LABEL : 0));

        value = CTC_FLAG_ISSET(p_psc->ip_flag, CTC_LINKAGG_PSC_IP_ECN);
        CTC_SET_FLAG(hash_ctl.ip_flag, (value ? CTC_PARSER_IP_HASH_FLAGS_ECN : 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_L4))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L4);

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_SRC_PORT);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_SRC_PORT : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_DST_PORT);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_DST_PORT : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_GRE_KEY);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_GRE_KEY : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_TYPE);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TYPE : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_USER_TYPE);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_USER_TYPE : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_VXLAN_L4_SRC_PORT);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_VXLAN_L4_SRC_PORT : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_TCP_FLAG);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TCP_FLAG : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_TCP_ECN);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_TCP_ECN : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_NVGRE_VSID);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_VSID : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_NVGRE_FLOW_ID);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_FLOW_ID : 0));

        value = CTC_FLAG_ISSET(p_psc->l4_flag, CTC_LINKAGG_PSC_L4_VXLAN_VNI);
        CTC_SET_FLAG(hash_ctl.l4_flag, (value ? CTC_PARSER_L4_HASH_FLAGS_L4_VXLAN_VNI : 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_PBB))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_PBB);

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACDA);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_CMAC_DA : 0));

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BMACSA);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_CMAC_SA : 0));

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_ISID);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_ISID : 0));

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BVLAN);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_VID : 0));
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_VID : 0));

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BCOS);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_COS : 0));
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_COS : 0));

        value = CTC_FLAG_ISSET(p_psc->pbb_flag, CTC_LINKAGG_PSC_PBB_BCFI);
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_STAG_CFI : 0));
        CTC_SET_FLAG(hash_ctl.pbb_flag, (value ? CTC_PARSER_PBB_HASH_FLAGS_CTAG_CFI : 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_MPLS))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_MPLS);

        value = CTC_FLAG_ISSET(p_psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_PROTOCOL);
        CTC_SET_FLAG(hash_ctl.mpls_flag, (value ? CTC_PARSER_MPLS_HASH_FLAGS_PROTOCOL : 0));

        value = CTC_FLAG_ISSET(p_psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPSA);
        CTC_SET_FLAG(hash_ctl.mpls_flag, (value ? CTC_PARSER_MPLS_HASH_FLAGS_IPSA : 0));

        value = CTC_FLAG_ISSET(p_psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_IPDA);
        CTC_SET_FLAG(hash_ctl.mpls_flag, (value ? CTC_PARSER_MPLS_HASH_FLAGS_IPDA : 0));

        value = CTC_FLAG_ISSET(p_psc->mpls_flag, CTC_LINKAGG_PSC_MPLS_CANCEL_LABEL);
        CTC_SET_FLAG(hash_ctl.mpls_flag, (value ? CTC_PARSER_MPLS_HASH_FLAGS_CANCEL_LABEL : 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_TRILL))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_TRILL);

        value = CTC_FLAG_ISSET(p_psc->trill_flag, CTC_LINKAGG_PSC_TRILL_INNER_VID);
        CTC_SET_FLAG(hash_ctl.trill_flag, (value ? CTC_PARSER_TRILL_HASH_FLAGS_INNER_VLAN_ID : 0));

        value = CTC_FLAG_ISSET(p_psc->trill_flag, CTC_LINKAGG_PSC_TRILL_INGRESS_NICKNAME);
        CTC_SET_FLAG(hash_ctl.trill_flag, (value ? CTC_PARSER_TRILL_HASH_FLAGS_INGRESS_NICKNAME : 0));

        value = CTC_FLAG_ISSET(p_psc->trill_flag, CTC_LINKAGG_PSC_TRILL_EGRESS_NICKNAME);
        CTC_SET_FLAG(hash_ctl.trill_flag, (value ? CTC_PARSER_TRILL_HASH_FLAGS_EGRESS_NICKNAME : 0));
    }

    if (CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_FCOE))
    {
        CTC_SET_FLAG(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_FCOE);

        value = CTC_FLAG_ISSET(p_psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_SID);
        CTC_SET_FLAG(hash_ctl.fcoe_flag, (value ? CTC_PARSER_FCOE_HASH_FLAGS_SID : 0));

        value = CTC_FLAG_ISSET(p_psc->fcoe_flag, CTC_LINKAGG_PSC_FCOE_DID);
        CTC_SET_FLAG(hash_ctl.fcoe_flag, (value ? CTC_PARSER_FCOE_HASH_FLAGS_DID : 0));
    }

    CTC_ERROR_RETURN(sys_goldengate_parser_set_linkagg_hash_field(lchip, &hash_ctl));

    return CTC_E_NONE;
}

/**
 @brief The function is to get member of linkagg hash key
*/
int32
sys_goldengate_linkagg_get_psc(uint8 lchip, ctc_linkagg_psc_t* p_psc)
{
    uint32 value1 = 0;
    uint32 value2 = 0;
    ctc_parser_linkagg_hash_ctl_t hash_ctl;

    SYS_LINKAGG_INIT_CHECK(lchip);

    sal_memset(&hash_ctl, 0, sizeof(ctc_parser_linkagg_hash_ctl_t));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_INNER);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_INNER : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_L2);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_L2 : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_IP);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_IP : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_L4);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_L4 : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_PBB);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_PBB : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_MPLS);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_MPLS : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_TRILL);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_TRILL : 0));

    value1 = CTC_FLAG_ISSET(p_psc->psc_type_bitmap, CTC_LINKAGG_PSC_TYPE_FCOE);
    CTC_SET_FLAG(hash_ctl.hash_type_bitmap, (value1 ? CTC_PARSER_HASH_TYPE_FLAGS_FCOE : 0));

    CTC_ERROR_RETURN(sys_goldengate_parser_get_linkagg_hash_field(lchip, &hash_ctl));

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L2))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_STAG_VID);
        value2 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_CTAG_VID);

        if (value1 && value2)
        {
            CTC_SET_FLAG(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_DOUBLE_VLAN);
        }
        else if (value1 || value2)
        {
            CTC_SET_FLAG(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_VLAN);
        }

        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_STAG_COS);
        value2 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_CTAG_COS);

        if (value1 || value2)
        {
            CTC_SET_FLAG(p_psc->l2_flag, CTC_LINKAGG_PSC_L2_COS);
        }

        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_ETHERTYPE);
        CTC_SET_FLAG(p_psc->l2_flag, (value1 ? CTC_LINKAGG_PSC_L2_ETHERTYPE : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_MACSA );
        CTC_SET_FLAG(p_psc->l2_flag, value1 ? CTC_LINKAGG_PSC_L2_MACSA : 0);

        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_MACDA );
        CTC_SET_FLAG(p_psc->l2_flag, value1 ? CTC_LINKAGG_PSC_L2_MACDA : 0);

        value1 = CTC_FLAG_ISSET(hash_ctl.l2_flag, CTC_PARSER_L2_HASH_FLAGS_PORT);
        CTC_SET_FLAG(p_psc->l2_flag, value1 ? CTC_LINKAGG_PSC_L2_PORT : 0);
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_IP))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_PROTOCOL);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IP_PROTOCOL : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPSA);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IP_IPSA  : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPDA);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IP_IPDA : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_DSCP);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IP_DSCP : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_IPV6_FLOW_LABEL);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IPV6_FLOW_LABEL : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.ip_flag, CTC_PARSER_IP_HASH_FLAGS_ECN);
        CTC_SET_FLAG(p_psc->ip_flag, (value1 ? CTC_LINKAGG_PSC_IP_ECN : 0));
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_L4))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_SRC_PORT);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_SRC_PORT : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_DST_PORT);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_DST_PORT : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_GRE_KEY);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_GRE_KEY : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TYPE);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_TYPE : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_USER_TYPE);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_USER_TYPE : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_VXLAN_L4_SRC_PORT);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_VXLAN_L4_SRC_PORT : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TCP_FLAG);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_TCP_FLAG : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_TCP_ECN);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_TCP_ECN : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_VSID);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_NVGRE_VSID : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_NVGRE_FLOW_ID);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_NVGRE_FLOW_ID : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.l4_flag, CTC_PARSER_L4_HASH_FLAGS_L4_VXLAN_VNI);
        CTC_SET_FLAG(p_psc->l4_flag, (value1 ? CTC_LINKAGG_PSC_L4_VXLAN_VNI : 0));
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_PBB))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CMAC_DA);
        CTC_SET_FLAG(p_psc->pbb_flag, (value1 ? CTC_LINKAGG_PSC_PBB_BMACDA : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CMAC_SA);
        CTC_SET_FLAG(p_psc->pbb_flag, (value1 ? CTC_LINKAGG_PSC_PBB_BMACSA : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_ISID);
        CTC_SET_FLAG(p_psc->pbb_flag, (value1 ? CTC_LINKAGG_PSC_PBB_ISID : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_VID);
        value2 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_VID);
        CTC_SET_FLAG(p_psc->pbb_flag, ((value1 && value2) ? CTC_LINKAGG_PSC_PBB_BVLAN : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_COS);
        value2 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_COS);
        CTC_SET_FLAG(p_psc->pbb_flag, ((value1 && value2) ? CTC_LINKAGG_PSC_PBB_BCOS : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_STAG_CFI);
        value2 = CTC_FLAG_ISSET(hash_ctl.pbb_flag, CTC_PARSER_PBB_HASH_FLAGS_CTAG_CFI);
        CTC_SET_FLAG(p_psc->pbb_flag, ((value1 && value2) ? CTC_LINKAGG_PSC_PBB_BCFI : 0));
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_MPLS))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_PROTOCOL);
        CTC_SET_FLAG(p_psc->mpls_flag, (value1 ? CTC_LINKAGG_PSC_MPLS_PROTOCOL : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPSA);
        CTC_SET_FLAG(p_psc->mpls_flag, (value1 ? CTC_LINKAGG_PSC_MPLS_IPSA : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_IPDA);
        CTC_SET_FLAG(p_psc->mpls_flag, (value1 ? CTC_LINKAGG_PSC_MPLS_IPDA : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.mpls_flag, CTC_PARSER_MPLS_HASH_FLAGS_CANCEL_LABEL);
        CTC_SET_FLAG(p_psc->mpls_flag, (value1 ? CTC_LINKAGG_PSC_MPLS_CANCEL_LABEL : 0));
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_TRILL))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_INNER_VLAN_ID);
        CTC_SET_FLAG(p_psc->trill_flag, (value1 ? CTC_LINKAGG_PSC_TRILL_INNER_VID : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_INGRESS_NICKNAME);
        CTC_SET_FLAG(p_psc->trill_flag, (value1 ? CTC_LINKAGG_PSC_TRILL_INGRESS_NICKNAME : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.trill_flag, CTC_PARSER_TRILL_HASH_FLAGS_EGRESS_NICKNAME);
        CTC_SET_FLAG(p_psc->trill_flag, (value1 ? CTC_LINKAGG_PSC_TRILL_EGRESS_NICKNAME : 0));
    }

    if (CTC_FLAG_ISSET(hash_ctl.hash_type_bitmap, CTC_PARSER_HASH_TYPE_FLAGS_FCOE))
    {
        value1 = CTC_FLAG_ISSET(hash_ctl.fcoe_flag, CTC_PARSER_FCOE_HASH_FLAGS_SID);
        CTC_SET_FLAG(p_psc->fcoe_flag, (value1 ? CTC_LINKAGG_PSC_FCOE_SID : 0));

        value1 = CTC_FLAG_ISSET(hash_ctl.fcoe_flag, CTC_PARSER_FCOE_HASH_FLAGS_DID);
        CTC_SET_FLAG(p_psc->fcoe_flag, (value1 ? CTC_LINKAGG_PSC_FCOE_DID : 0));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to get member of linkagg
*/
int32
sys_goldengate_linkagg_get_max_mem_num(uint8 lchip, uint16* max_num)
{
    uint16 mem_num = 0;

    SYS_LINKAGG_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(max_num);

    if(p_gg_linkagg_master[lchip]->linkagg_num == 56)
    {
        mem_num = 16;
    }
    else
    {
        mem_num = SYS_LINKAGG_ALL_MEM_NUM / p_gg_linkagg_master[lchip]->linkagg_num;
    }

    *max_num = mem_num;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_linkagg_replace_port(uint8 lchip, sys_linkagg_t* p_linkagg, uint8 tid, uint16 gport)
{
    uint16  lport     = 0;
    uint16  agg_gport = 0;
    int32   ret       = CTC_E_NONE;
    uint16  index     = p_gg_linkagg_master[lchip]->mem_port_num[tid];

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
     p_gg_linkagg_master[lchip]->mem_port_num[tid]++;
    if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_1)
    {
        (p_linkagg->port_cnt)++;
        p_linkagg->need_pad = 0;
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_2)
    {
        p_linkagg->port_cnt = 25;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 25)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_3)
    {
        p_linkagg->port_cnt = 26;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 33)
        {
            p_linkagg->need_pad = 1;
        }
        else
        {
            p_linkagg->need_pad = 0;
        }
    }
    else if (p_gg_linkagg_master[lchip]->mem_port_num[tid] <= SYS_LINKAGG_MEM_NUM_4)
    {
        p_linkagg->port_cnt = 27;
        if ((p_gg_linkagg_master[lchip]->mem_port_num[tid]) == 65)
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
    if ((ret = _sys_goldengate_linkagg_update_table(lchip, p_linkagg, TRUE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg update asic table fail!\n");
        return ret;
    }

    /*for the first member in dlb mode ,need flush active */
    if ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (p_gg_linkagg_master[lchip]->mem_port_num[tid] == 1))
    {
        ret = _sys_goldengate_linkagg_clear_flow_active(lchip, tid);
        if (CTC_E_NONE != ret)
        {
            return ret;
        }
    }

    if (TRUE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport)))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        agg_gport = CTC_MAP_TID_TO_GPORT(tid);
        sys_goldengate_port_set_global_port(lchip, lport, agg_gport, FALSE);
    }

    return CTC_E_NONE;
}

/** replace all members for linkagg group*/
int32
sys_goldengate_linkagg_replace_ports(uint8 lchip, uint8 tid, uint32* gport, uint16 mem_num)
{
    uint16 index                = 0;
    uint16 index1               = 0;
    uint16 max_mem_num          = 0;
    int32  ret                  = 0;
    uint32 cmd                  = 0;
    uint32 mux_type             = 0;
    uint8  gchip                = 0;
    sys_linkagg_t* p_linkagg    = NULL;
    sys_linkagg_port_t* p_port  = NULL;
    uint16  pre_port_cnt        = 0;
    uint16  lport               = 0;

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
        CTC_GLOBAL_PORT_CHECK(gport[index]);
        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport[index]);
        SYS_GLOBAL_CHIPID_CHECK(gchip);
    }

    LINKAGG_LOCK;
    p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, tid);
    if (NULL == p_linkagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Linkagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    /*get max member number of per group*/
    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(tid, max_mem_num);

    if (((!(p_linkagg->linkagg_mode)) && (max_mem_num < mem_num)) ||
        ((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) && (mem_num > SYS_MAX_DLB_MEMBER_NUM)))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member of linkagg group reach Max, add member port fail!\n");
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }

    /* for rr, only support 24 member */
    if ((mem_num >= SYS_LINKAGG_MEM_NUM_1)
        && (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_RR))
    {
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }
    /*check*/
    for (index = 0; index < mem_num; index++)
    {
        gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport[index]);
        if (TRUE == sys_goldengate_chip_is_local(lchip, gchip))
        {
            cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, SYS_GET_CHANNEL_ID(lchip, gport[index]), cmd, &mux_type), ret, OUT);
        }
        else if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB)
        {
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "DLB group do not support remote port!\n");
            ret =  CTC_E_INVALID_PORT;
            goto OUT;
        }

        if (((p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_DLB) ||(p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER))
            && ((CTC_MAP_GPORT_TO_LPORT(gport[index])&0xFF) >= SYS_INTERNAL_PORT_START || (mux_type > 0)))
        {
            ret =  CTC_E_INVALID_LOCAL_PORT;
            goto OUT;
        }
    }

    /*p_port save the previous info of p_linkagg */
    p_port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, max_mem_num * sizeof(sys_linkagg_port_t));
    if (p_port == NULL)
    {
        ret = CTC_E_NO_MEMORY;
        goto OUT;
    }
    sal_memcpy(p_port, p_linkagg->port, max_mem_num * sizeof(sys_linkagg_port_t));

    for (index1 = 0; index1 < p_gg_linkagg_master[lchip]->mem_port_num[tid]; index1++)
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
    if((!mem_num) && (p_linkagg->port_cnt))
    {
        LINKAGG_UNLOCK;
        for (index = 0; index < max_mem_num; index++)
        {
            sys_goldengate_linkagg_remove_port(lchip, tid, p_linkagg->port[0].gport);
            if (!p_linkagg->port_cnt)
            {
                break;
            }
        }
        sal_memset(p_linkagg->port, 0, max_mem_num * sizeof(sys_linkagg_port_t));
        LINKAGG_LOCK;
    }
    else
    {
        pre_port_cnt = p_gg_linkagg_master[lchip]->mem_port_num[tid];
        p_linkagg->port_cnt = 0;
        p_linkagg->need_pad = 0;
        p_gg_linkagg_master[lchip]->mem_port_num[tid] = 0;
        sal_memset(p_linkagg->port, 0, max_mem_num*sizeof(sys_linkagg_port_t));
        /*do replace*/
        for(index = 0; index < mem_num; index++)
        {
            ret = _sys_goldengate_linkagg_replace_port(lchip, p_linkagg, tid, gport[index]);
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
                    _sys_goldengate_dlb_del_member(lchip, tid, index, pre_port_cnt, lport);
                }
                if (p_linkagg->linkagg_mode == CTC_LINKAGG_MODE_STATIC_FAILOVER)
                {

_sys_goldengate_hw_sync_del_member(lchip, tid, lport);
                }
            }

            if (TRUE == sys_goldengate_chip_is_local(lchip, gchip))
            {
                if (!p_port[index].valid) /*  p_port[index].valid = 1 , indicate p_port[index1].gport different with all new gports*/
                {
                    continue;
                }
                sys_goldengate_port_set_global_port(lchip, lport, p_port[index].gport, FALSE);
                sys_goldengate_port_update_mc_linkagg(lchip, tid, lport, FALSE);
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


#define __CHANNEL_AGG__
int32
sys_goldengate_linkagg_set_channel_agg_ref_cnt(uint8 lchip, uint8 tid, bool is_add)
{
    int ret = CTC_E_NONE;
    sys_linkagg_t* p_chanagg = NULL;
    SYS_LINKAGG_INIT_CHECK(lchip);
    if ((tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == tid))
    {
        return CTC_E_INVALID_TID;
    }

    LINKAGG_LOCK;
    p_chanagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, tid);
    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    if (is_add)
    {
        p_chanagg->ref_cnt++;
    }
    else
    {
        p_chanagg->ref_cnt = p_chanagg->ref_cnt? (p_chanagg->ref_cnt - 1) : 0;
    }
OUT:
    LINKAGG_UNLOCK;

    return ret;
}

int32
sys_goldengate_linkagg_get_channel_agg_ref_cnt(uint8 lchip, uint8 tid, uint8* ref_cnt)
{
    int ret = CTC_E_NONE;
    sys_linkagg_t* p_chanagg = NULL;
    SYS_LINKAGG_INIT_CHECK(lchip);
    if ((tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == tid))
    {
        return CTC_E_INVALID_TID;
    }

    LINKAGG_LOCK;
    p_chanagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, tid);
    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }
    *ref_cnt = p_chanagg->ref_cnt;
OUT:
    LINKAGG_UNLOCK;

    return ret;
}

STATIC int32
_sys_goldengate_linkagg_update_channel_table(uint8 lchip, sys_linkagg_t* p_chankagg, bool is_add_port, uint16 port_index)
{
    uint32 cmd = 0;
    uint32 mem_base = 0;
    DsLinkAggregateChannelMember_m ds_chanagg_member;
    DsLinkAggregateChannelGroup_m ds_chanagg_group;
    uint8 tail_index = 0;
    uint8  chan_id = 0;

    CTC_PTR_VALID_CHECK(p_chankagg);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


    sal_memset(&ds_chanagg_member, 0, sizeof(ds_chanagg_member));
    sal_memset(&ds_chanagg_group, 0, sizeof(ds_chanagg_group));
    mem_base = p_chankagg->tid * p_gg_linkagg_master[lchip]->chanagg_mem_per_grp;

    if (TRUE == is_add_port)
    {
        /*update member */
        /*get channel from port */
        chan_id = SYS_GET_CHANNEL_ID(lchip, p_chankagg->port[port_index].gport);
        if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }
        SetDsLinkAggregateChannelMember(V, channelId_f, &ds_chanagg_member, chan_id);
        cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mem_base + port_index), cmd, &ds_chanagg_member));

        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write DsLinkAggregateChannelMember %d\n", (mem_base + port_index));
        /* update group member cnt */
        cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chankagg->tid, cmd, &ds_chanagg_group));
        SetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_chanagg_group, p_chankagg->port_cnt);
        cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chankagg->tid, cmd, &ds_chanagg_group));
    }
    else
    {
        /* update group member cnt */
        cmd = DRV_IOR(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chankagg->tid, cmd, &ds_chanagg_group));
        SetDsLinkAggregateChannelGroup(V, channelLinkAggMemNum_f, &ds_chanagg_group, p_chankagg->port_cnt);
        cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_chankagg->tid, cmd, &ds_chanagg_group));

        if (0 == p_chankagg->port_cnt)
        {
            cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mem_base + port_index), cmd, &ds_chanagg_member));
            p_chankagg->port[0].valid = 0;
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Clear DsLinkAggregateChannelMember %d\n", (mem_base + port_index));
        }
        else
        {
             tail_index = p_chankagg->port_cnt;
            /*copy the last one to the removed port position,and remove member port from linkagg at tail*/
            cmd = DRV_IOR(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mem_base + tail_index), cmd, &ds_chanagg_member));

            cmd = DRV_IOW(DsLinkAggregateChannelMember_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mem_base + port_index), cmd, &ds_chanagg_member));

            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Copy DsLinkAggregateChannelMember from %d to %d\n", (mem_base + tail_index), (mem_base + port_index));
            /* update soft table */
            sal_memcpy(&(p_chankagg->port[port_index]), &(p_chankagg->port[tail_index]), sizeof(sys_linkagg_port_t));
            p_chankagg->port[tail_index].valid = 0;
        }
    }

    return CTC_E_NONE;

}


int32
sys_goldengate_linkagg_create_channel_agg(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp)
{
    uint16 mem_idx = 0;
    uint32 cmd = 0;
    int32  ret = CTC_E_NONE;
    sys_linkagg_t* p_chanagg = NULL;
    uint16 mem_num = 0;
    uint16 mem_base = 0;
    DsLinkAggregateChannelGroup_m ds_chanagg_group;

    /* sanity check */
    CTC_PTR_VALID_CHECK(p_linkagg_grp);
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ChanAggId = 0x%x\n", p_linkagg_grp->tid);

    if ((p_linkagg_grp->tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == p_linkagg_grp->tid))
    {
        return CTC_E_INVALID_TID;
    }

    sal_memset(&ds_chanagg_group, 0, sizeof(ds_chanagg_group));

    /* do create */
    LINKAGG_LOCK;
    mem_num = p_gg_linkagg_master[lchip]->chanagg_mem_per_grp;
    p_chanagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, p_linkagg_grp->tid);
    if (NULL != p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Channel agg has already exist!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_LINKAGG_HAS_EXIST, p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    }

    p_chanagg = (sys_linkagg_t*)mem_malloc(MEM_LINKAGG_MODULE, sizeof(sys_linkagg_t));
    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create new linkagg group!\n");
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_NO_MEMORY, p_gg_linkagg_master[lchip]->p_linkagg_mutex);
    }

    sal_memset(p_chanagg, 0, sizeof(sys_linkagg_t));

    p_chanagg->port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, mem_num * sizeof(sys_linkagg_port_t));
    if (NULL == p_chanagg->port)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory to create chanagg member port group!\n");
        ret = CTC_E_NO_MEMORY;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_3);
    }

    sal_memset(p_chanagg->port, 0, mem_num*sizeof(sys_linkagg_port_t));

    /* init linkAgg group info */
    p_chanagg->tid = p_linkagg_grp->tid;
    p_chanagg->port_cnt = 0;
    p_chanagg->linkagg_mode = CTC_LINKAGG_MODE_STATIC;

    for (mem_idx = 0; mem_idx < mem_num; mem_idx++)
    {
        p_chanagg->port[mem_idx].valid = FALSE;
    }

    if (FALSE == ctc_vector_add(p_gg_linkagg_master[lchip]->p_chanagg_vector, p_linkagg_grp->tid, (void*)p_chanagg))
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Add chanagg to vector error, create chanagg fail!\n");
        ret = CTC_E_NO_MEMORY;
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_2);
    }
    mem_base = p_linkagg_grp->tid*mem_num;
    SetDsLinkAggregateChannelGroup(V, channelLinkAggMemBase_f, &ds_chanagg_group, mem_base);
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    if ((ret = DRV_IOCTL(lchip, p_linkagg_grp->tid, cmd, &ds_chanagg_group)) < 0)
    {
        SYS_LINKAGG_GOTO_ERROR(ret, ERROR_1);
    }

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Groupid = %d, mem_base = %d!\n", p_linkagg_grp->tid, mem_base);

    LINKAGG_UNLOCK;

    return CTC_E_NONE;
ERROR_1:
    ctc_vector_del(p_gg_linkagg_master[lchip]->p_chanagg_vector, p_linkagg_grp->tid);
ERROR_2:
    mem_free(p_chanagg->port);
ERROR_3:
    mem_free(p_chanagg);
    LINKAGG_UNLOCK;

    return ret;
}

int32
sys_goldengate_linkagg_destroy_channel_agg(uint8 lchip, uint8 tid)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    sys_linkagg_t* p_chanagg = NULL;
    DsLinkAggregateChannelGroup_m ds_chanagg_group;

    /*sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ChanAggId = 0x%x\n", tid);

    if ((tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == tid))
    {
        return CTC_E_INVALID_TID;
    }

    sal_memset(&ds_chanagg_group, 0, sizeof(ds_chanagg_group));

    /*do remove*/
    LINKAGG_LOCK;
    p_chanagg = ctc_vector_del(p_gg_linkagg_master[lchip]->p_chanagg_vector, tid);
    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    /*reset linkagg group */
    cmd = DRV_IOW(DsLinkAggregateChannelGroup_t, DRV_ENTRY_FLAG);
    if ((ret = DRV_IOCTL(lchip, tid, cmd, &ds_chanagg_group)) < 0)
    {
        ret = CTC_E_HW_OP_FAIL;
        goto OUT;
    }

    if (NULL != p_chanagg)
    {
        mem_free(p_chanagg->port);
        mem_free(p_chanagg);
    }

OUT:

    LINKAGG_UNLOCK;
    return ret;

}

int32
sys_goldengate_linkagg_add_channel(uint8 lchip, uint8 tid, uint16 gport)
{
    uint16 index = 0;
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    sys_linkagg_t* p_chanagg = NULL;
    uint16 mem_num = 0;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ChanAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);
    if ((tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == tid))
    {
        return CTC_E_INVALID_TID;
    }

    CTC_GLOBAL_PORT_CHECK(gport);
    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*do add*/
    LINKAGG_LOCK;
    mem_num = p_gg_linkagg_master[lchip]->chanagg_mem_per_grp;
    p_chanagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, tid);

    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    /*check exist*/
    for (index = 0; index < mem_num; index++)
    {
        if ((p_chanagg->port[index].gport == gport) && (p_chanagg->port[index].valid))
        {
            ret = CTC_E_MEMBER_EXIST;
            goto OUT;
        }
    }

    if (p_chanagg->port_cnt >= p_gg_linkagg_master[lchip]->chanagg_mem_per_grp)
    {
        ret = CTC_E_EXCEED_MAX_SIZE;
        goto OUT;
    }

    /*get the first unused pos*/
    for (index = 0; index < mem_num; index++)
    {
        if (!(p_chanagg->port[index].valid))
        {
            break;
        }
    }
    if (index >= mem_num)  /* the group is full*/
    {
        goto OUT;
    }

    p_chanagg->port[index].gport = gport;
    p_chanagg->port[index].valid = 1;
    p_chanagg->port_cnt++;

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index = %d, port_cnt = %d\n", index,  p_chanagg->port_cnt);

    /*write asic table*/
    if ((ret = _sys_goldengate_linkagg_update_channel_table(lchip, p_chanagg, TRUE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg update asic table fail!\n");
        goto OUT;
    }

OUT:
    LINKAGG_UNLOCK;
    return ret;
}


int32
sys_goldengate_linkagg_remove_channel(uint8 lchip, uint8 tid, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint16 index = 0;
    uint8 gchip = 0;
    uint16 mem_num = 0;
    sys_linkagg_t* p_chanagg = NULL;

    /*Sanity check*/
    SYS_LINKAGG_INIT_CHECK(lchip);

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "linkAggId: 0x%x,mem_port: 0x%x\n",
                        tid, gport);
    if ((tid >= p_gg_linkagg_master[lchip]->chanagg_grp_num) || (0 == tid))
    {
        return CTC_E_INVALID_TID;
    }

    CTC_GLOBAL_PORT_CHECK(gport);
    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    LINKAGG_LOCK;
    p_chanagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, tid);

    if (NULL == p_chanagg)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg Not Exist!\n");
        ret = CTC_E_LINKAGG_NOT_EXIST;
        goto OUT;
    }

    if (0 == p_chanagg->port_cnt)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The member num of chanagg is zero, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }

    /*check if port is a member of linkagg*/
    mem_num = p_gg_linkagg_master[lchip]->chanagg_mem_per_grp;
    for (index = 0; index < mem_num; index++)
    {
        if ((p_chanagg->port[index].gport == gport) && (p_chanagg->port[index].valid))
        {
            break;
        }
    }
    if (index >= mem_num)  /* not find the member*/
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "The port is not exist in chanagg group, remove fail!\n");
        ret = CTC_E_MEMBER_PORT_NOT_EXIST;
        goto OUT;
    }
    if (p_chanagg->port_cnt)
    {
        (p_chanagg->port_cnt)--;
    }

    SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index = %d, port_cnt = %d\n", index,  p_chanagg->port_cnt);

    /*write asic table*/
    if ((ret = _sys_goldengate_linkagg_update_channel_table(lchip, p_chanagg, FALSE, index)) < 0)
    {
        SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Chanagg update asic table fail!\n");
        goto OUT;
    }


OUT:
    LINKAGG_UNLOCK;
    return ret;
}


#define __WB__
int32
_sys_goldengate_linkagg_wb_mapping_group(uint8 lchip, sys_wb_linkagg_group_t *p_wb_linkagg, sys_linkagg_t *p_linkagg, uint8 sync)
{
    uint16 mem_num = 0;

    if (sync)
    {
        p_wb_linkagg->tid 			       = p_linkagg->tid;
        p_wb_linkagg->port_cnt 		= p_linkagg->port_cnt;
        p_wb_linkagg->linkagg_mode 	= p_linkagg->linkagg_mode;
        p_wb_linkagg->need_pad 		= p_linkagg->need_pad;
        p_wb_linkagg->ref_cnt 		       = p_linkagg->ref_cnt;
    }
    else
    {
        SYS_LINKAGG_GET_GROUP_MEMBER_NUM(p_wb_linkagg->tid, mem_num);
        p_linkagg->port = (sys_linkagg_port_t*)mem_malloc(MEM_LINKAGG_MODULE, mem_num * sizeof(sys_linkagg_port_t));
        if (NULL == p_linkagg->port)
        {
            SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No memory to create linkagg member port group!\n");
            return CTC_E_NO_MEMORY;
        }

        p_linkagg->tid			= p_wb_linkagg->tid;
        p_linkagg->port_cnt		= p_wb_linkagg->port_cnt;
        p_linkagg->linkagg_mode	= p_wb_linkagg->linkagg_mode;
        p_linkagg->need_pad		= p_wb_linkagg->need_pad;
        p_linkagg->ref_cnt		= p_wb_linkagg->ref_cnt;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_linkagg_wb_sync_func(void* array_data, uint32 index, void* user_data)
{
    uint16 max_entry_cnt = 0;
    sys_linkagg_t *p_linkagg = (sys_linkagg_t *)array_data;
    sys_wb_linkagg_group_t  *p_wb_linkagg;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_linkagg = (sys_wb_linkagg_group_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_linkagg, 0, sizeof(sys_wb_linkagg_group_t));

    CTC_ERROR_RETURN(_sys_goldengate_linkagg_wb_mapping_group(lchip, p_wb_linkagg, p_linkagg, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_linkagg_wb_mapping_port(uint8 lchip, uint32 loop, sys_wb_linkagg_port_t *p_wb_linkagg, sys_linkagg_t *p_linkagg, uint8 sync)
{

    if (sync)
    {
        p_wb_linkagg->tid = p_linkagg->tid;
        p_wb_linkagg->index = loop;
        p_wb_linkagg->valid = p_linkagg->port[loop].valid;
        p_wb_linkagg->pad_flag = p_linkagg->port[loop].pad_flag;
        p_wb_linkagg->gport = p_linkagg->port[loop].gport;
    }
    else
    {
        p_linkagg->port[loop].valid = p_wb_linkagg->valid;
        p_linkagg->port[loop].pad_flag = p_wb_linkagg->pad_flag;
        p_linkagg->port[loop].gport = p_wb_linkagg->gport;
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_linkagg_wb_sync_port_func(void* array_data, uint32 index, void* user_data)
{
    uint16 max_entry_cnt = 0;
    uint16 mem_num = 0;
    uint32 loop = 0;
    sys_linkagg_t *p_linkagg = (sys_linkagg_t *)array_data;
    sys_wb_linkagg_port_t  *p_wb_linkagg;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    SYS_LINKAGG_GET_GROUP_MEMBER_NUM(p_linkagg->tid, mem_num);

    do
    {
        p_wb_linkagg = (sys_wb_linkagg_port_t *)wb_data->buffer + wb_data->valid_cnt;

        CTC_ERROR_RETURN(_sys_goldengate_linkagg_wb_mapping_port(lchip, loop, p_wb_linkagg, p_linkagg, 1));
        if (++wb_data->valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
            wb_data->valid_cnt = 0;
        }
    }while(++loop < mem_num);

    return CTC_E_NONE;
}


int32
sys_goldengate_linkagg_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_linkagg_master_t  *p_wb_linkagg_master;

    /*syncup linkagg matser*/
    wb_data.buffer = mem_malloc(MEM_LINKAGG_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_linkagg_master_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_MASTER);

    p_wb_linkagg_master = (sys_wb_linkagg_master_t  *)wb_data.buffer;

    p_wb_linkagg_master->lchip = lchip;
    sal_memcpy(p_wb_linkagg_master->mem_port_num, p_gg_linkagg_master[lchip]->mem_port_num, p_gg_linkagg_master[lchip]->linkagg_num);

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup linkagg group*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_linkagg_group_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_GROUP);

    user_data.data = &wb_data;
    user_data.value1 = lchip;
    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_linkagg_master[lchip]->p_linkagg_vector, 0, _sys_goldengate_linkagg_wb_sync_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup linkagg group port*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_linkagg_port_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_PORT);

    user_data.data = &wb_data;
    user_data.value1 = lchip;
    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_linkagg_master[lchip]->p_linkagg_vector, 0, _sys_goldengate_linkagg_wb_sync_port_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_linkagg_group_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_CHANGROUP);

    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_linkagg_master[lchip]->p_chanagg_vector, 0, _sys_goldengate_linkagg_wb_sync_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup linkagg chan group port*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_linkagg_port_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_CHANPORT);

    user_data.data = &wb_data;
    user_data.value1 = lchip;
    CTC_ERROR_GOTO(ctc_vector_traverse2(p_gg_linkagg_master[lchip]->p_chanagg_vector, 0, _sys_goldengate_linkagg_wb_sync_port_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_linkagg_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_linkagg_master_t  *p_wb_linkagg_master;
    sys_wb_linkagg_group_t *p_wb_linkagg;
    sys_wb_linkagg_port_t *p_wb_linkagg_port;
    sys_linkagg_t *p_linkagg;

    wb_query.buffer = mem_malloc(MEM_LINKAGG_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }

    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_linkagg_master_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore linkagg master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query linkagg master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_linkagg_master = (sys_wb_linkagg_master_t *)wb_query.buffer;

    sal_memcpy(p_gg_linkagg_master[lchip]->mem_port_num, p_wb_linkagg_master->mem_port_num, p_gg_linkagg_master[lchip]->linkagg_num);

    /*restore linkagg group*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_linkagg_group_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_GROUP);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_linkagg = (sys_wb_linkagg_group_t *)wb_query.buffer + entry_cnt++;

        p_linkagg = mem_malloc(MEM_LINKAGG_MODULE,  sizeof(sys_linkagg_t));
        if (NULL == p_linkagg)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_linkagg, 0, sizeof(sys_linkagg_t));

        ret = _sys_goldengate_linkagg_wb_mapping_group(lchip, p_wb_linkagg, p_linkagg, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_vector_add(p_gg_linkagg_master[lchip]->p_linkagg_vector, p_linkagg->tid, p_linkagg);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore linkagg group port*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_linkagg_port_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_PORT);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_linkagg_port = (sys_wb_linkagg_port_t *)wb_query.buffer + entry_cnt++;

        p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_linkagg_vector, p_wb_linkagg_port->tid);
        if (NULL == p_linkagg)
        {
            return CTC_E_LINKAGG_NOT_EXIST;
        }

        ret = _sys_goldengate_linkagg_wb_mapping_port(lchip, p_wb_linkagg_port->index, p_wb_linkagg_port, p_linkagg, 0);
        if (ret)
        {
            continue;
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_linkagg_group_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_CHANGROUP);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_linkagg = (sys_wb_linkagg_group_t *)wb_query.buffer + entry_cnt++;

        p_linkagg = mem_malloc(MEM_LINKAGG_MODULE,  sizeof(sys_linkagg_t));
        if (NULL == p_linkagg)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_linkagg, 0, sizeof(sys_linkagg_t));

        ret = _sys_goldengate_linkagg_wb_mapping_group(lchip, p_wb_linkagg, p_linkagg, 0);
        if (ret)
        {
            if(p_linkagg->port)
            {
                mem_free(p_linkagg->port);
            }
            mem_free(p_linkagg);

            continue;
        }

        /*add to soft table*/
        ctc_vector_add(p_gg_linkagg_master[lchip]->p_chanagg_vector, p_linkagg->tid, p_linkagg);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore linkagg chan group port*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_linkagg_port_t, CTC_FEATURE_LINKAGG, SYS_WB_APPID_LINKAGG_SUBID_CHANPORT);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_linkagg_port = (sys_wb_linkagg_port_t *)wb_query.buffer + entry_cnt++;

        p_linkagg = ctc_vector_get(p_gg_linkagg_master[lchip]->p_chanagg_vector, p_wb_linkagg_port->tid);
        if (NULL == p_linkagg)
        {
            return CTC_E_LINKAGG_NOT_EXIST;
        }

        ret = _sys_goldengate_linkagg_wb_mapping_port(lchip, p_wb_linkagg_port->index, p_wb_linkagg_port, p_linkagg, 0);
        if (ret)
        {
            continue;
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

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



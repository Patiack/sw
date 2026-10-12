/**
 @file sys_goldengate_aps.c

 @date 2010-3-11

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_port.h"
#include "ctc_interrupt.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_aps.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_mpls.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_APS_L3IFID_VAILD_CHECK(l3if_id)         \
    if (l3if_id > SYS_GG_MAX_CTC_L3IF_ID) \
    {                                               \
        return CTC_E_L3IF_INVALID_IF_ID;            \
    }
#define SYS_APS_MPLS_L3IF_CHECK(aps_group)   \
    if ((CTC_FLAG_ISSET(aps_group->aps_flag, CTC_APS_FLAG_IS_MPLS)) \
        && (((!aps_group->next_w_aps_en) && (!aps_group->w_l3if_id)) \
            || ((!aps_group->next_p_aps_en) && (!aps_group->p_l3if_id)))) \
    { \
        return CTC_E_APS_MPLS_TYPE_L3IF_NOT_EXIST; \
    }

#define SYS_APS_INIT_CHECK(lchip)           \
    {                                       \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_aps_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    }

#define SYS_APS_FILELD_SET(fld, val) DRV_SET_FIELD_V(DsApsBridge_t, fld , &ds_aps_bridge, val)
#define SYS_APS_FILELD_GET(fld, val) drv_goldengate_get_field(DsApsBridge_t, fld , &ds_aps_bridge, val)
/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
enum sys_aps_flag_e
{
    SYS_APS_FLAG_PROTECT_EN                  = 1U<<0,   /**< If fast_x_aps_en is set, protect_en is egnored, the bit must from hardware */
    SYS_APS_FLAG_NEXT_W_APS_EN               = 1U<<1,   /**< Use next_w_aps neglect working_gport */
    SYS_APS_FLAG_NEXT_P_APS_EN               = 1U<<2,   /**< Use next_w_aps neglect protection_gport */
    SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED    = 1U<<3,   /**< Indicate port aps use physical isolated path */
    SYS_APS_FLAG_HW_BASED_FAILOVER_EN        = 1U<<4,   /**< Used for 1-level link fast aps, by hardware switch path when link down */
    SYS_APS_FLAG_RAPS                        = 1U<<5,   /**< Used for raps */
    MAX_SYS_APS_FLAG
};
typedef enum sys_aps_flag_e sys_aps_flag_t;

struct sys_aps_bridge_s
{
    uint16 w_l3if_id;                   /**< working l3if id */
    uint16 p_l3if_id;                   /**< protection l3if id */

    uint16 raps_group_id;
    uint16 flag;                /**< it's the sys_aps_flag_t value */
};
typedef struct sys_aps_bridge_s sys_aps_bridge_t;

struct sys_raps_mcast_node_s
{
    ctc_slistnode_t head;
    uint16 group_id;
    uint16 rsv;
    uint32 raps_nhid;
};
typedef struct sys_raps_mcast_node_s sys_raps_mcast_node_t;

struct sys_aps_master_s
{
    ctc_vector_t* aps_bridge_vector;
    sal_mutex_t* p_aps_mutex;
    ctc_slist_t* raps_grp_list;

    uint16 aps_used_num;
};
typedef struct sys_aps_master_s sys_aps_master_t;
sys_aps_master_t* p_gg_aps_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
* Function
*
*****************************************************************************/
int32
_sys_goldengate_aps_wb_traverse_sync_node(void *data,uint32 vec_index, void *user_data)
{
    int32 ret;
    sys_aps_bridge_t *p_aps_node = (sys_aps_bridge_t*)data;
    sys_traverse_t *wb_traverse = (sys_traverse_t*)user_data;
    ctc_wb_data_t *wb_data = wb_traverse->data;
    uint16 max_buffer_cnt = wb_data->buffer_len/(wb_data->key_len + wb_data->data_len);
    sys_wb_aps_node_t* p_wb_aps_node = NULL;

    p_wb_aps_node = (sys_wb_aps_node_t *)wb_data->buffer + wb_data->valid_cnt;

    sal_memset(p_wb_aps_node, 0, sizeof(sys_wb_aps_node_t));

    p_wb_aps_node->group_id = vec_index;
    p_wb_aps_node->w_l3if_id = p_aps_node->w_l3if_id;
    p_wb_aps_node->p_l3if_id = p_aps_node->p_l3if_id;
    p_wb_aps_node->raps_group_id = p_aps_node->raps_group_id;
    p_wb_aps_node->flag = p_aps_node->flag;

    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_aps_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint16 max_buffer_cnt = 0;
    ctc_wb_data_t wb_data;
    sys_traverse_t  wb_aps_traverse;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    ctc_slistnode_t* lisnode = NULL;
    sys_wb_aps_mcast_node_t* p_wb_raps_mcast_node = NULL;

    wb_data.buffer = mem_malloc(MEM_APS_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No memory \n");
        return CTC_E_NO_MEMORY;

    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);


    /*sync aps nodes*/
    wb_aps_traverse.data = &wb_data;
    wb_aps_traverse.value1 = lchip;
    wb_aps_traverse.value2 = 0;
    wb_aps_traverse.value3 = 0;
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_aps_node_t, CTC_FEATURE_APS, SYS_WB_APPID_APS_SUBID_NODE);
    ctc_vector_traverse2(p_gg_aps_master[lchip]->aps_bridge_vector,0,_sys_goldengate_aps_wb_traverse_sync_node,&wb_aps_traverse);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync raps nodes*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_aps_mcast_node_t, CTC_FEATURE_APS, SYS_WB_APPID_APS_SUBID_MCAST_NODE);
    max_buffer_cnt = wb_data.buffer_len/(wb_data.key_len + wb_data.data_len);
    CTC_SLIST_LOOP(p_gg_aps_master[lchip]->raps_grp_list, lisnode)
    {
        p_raps_mcast_node = _ctc_container_of(lisnode, sys_raps_mcast_node_t, head);
        p_wb_raps_mcast_node = (sys_wb_aps_mcast_node_t *)wb_data.buffer + wb_data.valid_cnt;

        sal_memset(p_wb_raps_mcast_node, 0, sizeof(sys_wb_aps_mcast_node_t));

        p_wb_raps_mcast_node->group_id = p_raps_mcast_node->group_id;
        p_wb_raps_mcast_node->raps_nhid = p_raps_mcast_node->raps_nhid;

        if (++wb_data.valid_cnt ==  max_buffer_cnt)
        {
            ret = ctc_wb_add_entry(&wb_data);
            if ( ret != CTC_E_NONE )
            {
               return ret;
            }
            wb_data.valid_cnt = 0;
        }
    }

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
sys_goldengate_aps_wb_restore(uint8 lchip)
{
    ctc_wb_query_t    wb_query;
    ctc_wb_query_t* p_wb_query = &wb_query;
    int32 ret = 0;
    uint16 entry_cnt = 0;
    sys_wb_aps_node_t* p_wb_aps_node = NULL;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    sys_wb_aps_mcast_node_t* p_wb_raps_mcast_node = NULL;

    wb_query.buffer = mem_malloc(MEM_NEXTHOP_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No memory \n");
        return CTC_E_NO_MEMORY;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore aps nodes*/
    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_aps_node_t, CTC_FEATURE_APS, SYS_WB_APPID_APS_SUBID_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);
    p_wb_aps_node = (sys_wb_aps_node_t*)wb_query.buffer + entry_cnt++ ;

    p_aps_bridge = mem_malloc(MEM_APS_MODULE, sizeof(sys_aps_bridge_t));
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }

    sal_memset(p_aps_bridge, 0, sizeof(sys_aps_bridge_t));
    p_aps_bridge->flag = p_wb_aps_node->flag;
    p_aps_bridge->raps_group_id = p_wb_aps_node->raps_group_id;
    p_aps_bridge->p_l3if_id = p_wb_aps_node->p_l3if_id;
    p_aps_bridge->w_l3if_id = p_wb_aps_node->w_l3if_id;

    ctc_vector_add(p_gg_aps_master[lchip]->aps_bridge_vector,  p_wb_aps_node->group_id, p_aps_bridge);
    p_gg_aps_master[lchip]->aps_used_num++;

    CTC_WB_QUERY_ENTRY_END(p_wb_query);

    /*restore raps nodes*/
    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_aps_mcast_node_t, CTC_FEATURE_APS, SYS_WB_APPID_APS_SUBID_MCAST_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);
    p_wb_raps_mcast_node = (sys_wb_aps_mcast_node_t*)wb_query.buffer + entry_cnt++ ;

    p_raps_mcast_node = (sys_raps_mcast_node_t*)mem_malloc(MEM_APS_MODULE, sizeof(sys_raps_mcast_node_t));
    if (NULL == p_raps_mcast_node)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }

    sal_memset(p_raps_mcast_node, 0, sizeof(sys_raps_mcast_node_t));
    p_raps_mcast_node->raps_nhid = p_wb_raps_mcast_node->raps_nhid;
    p_raps_mcast_node->group_id = p_wb_raps_mcast_node->group_id;

    ctc_slist_add_tail(p_gg_aps_master[lchip]->raps_grp_list, &p_raps_mcast_node->head);

    CTC_WB_QUERY_ENTRY_END(p_wb_query);

done:
    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return 0;
}

int32
sys_goldengate_aps_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint16 index = 0;
    uint32 entry_num = 0;
    DsApsBridge_m ds_aps_bridge;
    MetFifoCtl_m ds_met_fifo;

    if (NULL != p_gg_aps_master[lchip])
    {
        return CTC_E_NONE;
    }
    entry_num = TABLE_MAX_INDEX(DsApsBridge_t);
    p_gg_aps_master[lchip] = mem_malloc(MEM_APS_MODULE, sizeof(sys_aps_master_t));
    if (NULL == p_gg_aps_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_aps_master[lchip], 0, sizeof(sys_aps_master_t));
#ifndef PACKET_TX_USE_SPINLOCK
    sal_mutex_create(&(p_gg_aps_master[lchip]->p_aps_mutex));
#else
    sal_spinlock_create((sal_spinlock_t**)&(p_gg_aps_master[lchip]->p_aps_mutex));
#endif
    if (NULL == p_gg_aps_master[lchip]->p_aps_mutex)
    {
        mem_free(p_gg_aps_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gg_aps_master[lchip]->aps_bridge_vector
        = ctc_vector_init(CTC_APS_GROUP_BLOCK, entry_num / CTC_APS_GROUP_BLOCK);
    if (NULL == p_gg_aps_master[lchip]->aps_bridge_vector)
    {
        sal_mutex_destroy(p_gg_aps_master[lchip]->p_aps_mutex);
        mem_free(p_gg_aps_master[lchip]);
        return CTC_E_NO_MEMORY;
    }
    ctc_vector_reserve(p_gg_aps_master[lchip]->aps_bridge_vector, 1);

    p_gg_aps_master[lchip]->raps_grp_list = ctc_slist_new();
    if (NULL == p_gg_aps_master[lchip]->raps_grp_list)
    {
        mem_free(p_gg_aps_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(DsApsBridge_m));
    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    for (index = 0; index < entry_num; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_aps_bridge));
    }
    entry_num = (0 == entry_num) ? 0 : (entry_num -1);
     /* configure hw-based failover aps*/
    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_met_fifo));
    SetMetFifoCtl(V, linkChangeEn_f, &ds_met_fifo, 1);
    SetMetFifoCtl(V, maxApsGroupNum_f, &ds_met_fifo, entry_num);
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_met_fifo));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_APS, sys_goldengate_aps_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_aps_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_aps_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_aps_deinit(uint8 lchip)
{
    ctc_slistnode_t* node = NULL, * next_node = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_aps_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free aps slist*/
    CTC_SLIST_LOOP_DEL(p_gg_aps_master[lchip]->raps_grp_list, node, next_node)
    {
        ctc_slist_delete_node(p_gg_aps_master[lchip]->raps_grp_list, node);
        mem_free(node);
    }
    ctc_slist_delete(p_gg_aps_master[lchip]->raps_grp_list);

    /*free aps bridge*/
    ctc_vector_traverse(p_gg_aps_master[lchip]->aps_bridge_vector, (vector_traversal_fn)_sys_goldengate_aps_free_node_data, NULL);
    ctc_vector_release(p_gg_aps_master[lchip]->aps_bridge_vector);

#ifndef PACKET_TX_USE_SPINLOCK
    sal_mutex_destroy(p_gg_aps_master[lchip]->p_aps_mutex);
#else
    sal_spinlock_destroy((sal_spinlock_t*)p_gg_aps_master[lchip]->p_aps_mutex);
#endif
    mem_free(p_gg_aps_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_aps_failover_detect(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 cmd = 0;
    MetFifoLinkState_m chan_down;
    uint8 index = 0;
    sys_intr_type_t type;

    SYS_APS_INIT_CHECK(lchip);

    type.intr = SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE;
    type.sub_intr = 0;

    /* APS protect switch done interrupt */
    sys_goldengate_interrupt_set_en_internal(lchip, &type, FALSE);

    for (index = 0; index < 128; index++)
    {
        cmd = DRV_IOR(MetFifoLinkState_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &chan_down));

        if (GetMetFifoLinkState(V, linkDown_f,&chan_down))
        {
            SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Aps channel failover, channel Id:0x%x\n ", index);
            cmd = DRV_IOR(MetFifoLinkState_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &chan_down));
            SetMetFifoLinkState(V, linkDown_f, &chan_down, 0);
            cmd = DRV_IOR(MetFifoLinkState_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &chan_down));
        }
    }

    /* release linkdown interrupt */
    sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);

    return CTC_E_NONE;
}

STATIC sys_raps_mcast_node_t*
_sys_goldengate_aps_find_raps_mcast_node(uint8 lchip, uint16 group_id)
{
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    ctc_slistnode_t* lisnode = NULL;
    CTC_SLIST_LOOP(p_gg_aps_master[lchip]->raps_grp_list, lisnode)
    {
        p_raps_mcast_node = _ctc_container_of(lisnode, sys_raps_mcast_node_t, head);
        if (p_raps_mcast_node->group_id == group_id)
        {
            return p_raps_mcast_node;
        }
    }
    return NULL;
}

int32
sys_goldengate_aps_create_raps_mcast_group(uint8 lchip, uint16 group_id)
{
    sys_nh_param_mcast_group_t  nh_mcast_group;
    sys_raps_mcast_node_t* p_raps_mcast_node;

    SYS_APS_INIT_CHECK(lchip);
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));
    nh_mcast_group.dsfwd_valid= 0;
    CTC_ERROR_RETURN(sys_goldengate_mcast_nh_create(lchip, group_id, &nh_mcast_group));

    p_raps_mcast_node = (sys_raps_mcast_node_t*)mem_malloc(MEM_APS_MODULE, sizeof(sys_raps_mcast_node_t));
    sal_memset(p_raps_mcast_node, 0, sizeof(sys_raps_mcast_node_t));
    p_raps_mcast_node->raps_nhid = nh_mcast_group.nhid;
    p_raps_mcast_node->group_id = group_id;

    ctc_slist_add_tail(p_gg_aps_master[lchip]->raps_grp_list, &p_raps_mcast_node->head);

    return CTC_E_NONE;
}

int32
sys_goldengate_aps_remove_raps_mcast_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;

    SYS_APS_INIT_CHECK(lchip);
    p_raps_mcast_node = _sys_goldengate_aps_find_raps_mcast_node(lchip, group_id);
    if (NULL == p_raps_mcast_node)
    {
        return CTC_E_APS_RAPS_GROUP_NOT_EXIST;
    }

    ret = sys_goldengate_mcast_nh_delete(lchip, p_raps_mcast_node->raps_nhid);

    ctc_slist_delete_node(p_gg_aps_master[lchip]->raps_grp_list, &p_raps_mcast_node->head);
    mem_free(p_raps_mcast_node);

    return ret;
}

int32
sys_goldengate_aps_update_raps_mcast_member(uint8 lchip, ctc_raps_member_t* p_raps_mem)
{
    sys_nh_param_mcast_group_t  nh_param_mcast_group;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    uint16 lport = 0;
    uint16 dest_port = 0;

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_raps_mem);
    sal_memset(&nh_param_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    p_raps_mcast_node = _sys_goldengate_aps_find_raps_mcast_node(lchip, p_raps_mem->group_id);
    if (NULL == p_raps_mcast_node)
    {
        return CTC_E_APS_RAPS_GROUP_NOT_EXIST;
    }

    nh_param_mcast_group.nhid = p_raps_mcast_node->raps_nhid;
    nh_param_mcast_group.dsfwd_valid = 0;

    if (p_raps_mem->remote_chip)
    {
        nh_param_mcast_group.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_REMOTE;
    }
    else
    {
        nh_param_mcast_group.mem_info.member_type = SYS_NH_PARAM_BRGMC_MEM_RAPS;
    }

    if (!p_raps_mem->remove_flag)
    {
        nh_param_mcast_group.opcode = SYS_NH_PARAM_MCAST_ADD_MEMBER;
    }
    else
    {
        nh_param_mcast_group.opcode = SYS_NH_PARAM_MCAST_DEL_MEMBER;
    }

    dest_port = p_raps_mem->mem_port;
    lport = CTC_MAP_GPORT_TO_LPORT(dest_port);
    if (CTC_IS_LINKAGG_PORT(dest_port))
    {
        nh_param_mcast_group.mem_info.is_linkagg = 1;
        nh_param_mcast_group.mem_info.destid = lport;
    }
    else
    {
        nh_param_mcast_group.mem_info.is_linkagg = 0;
        nh_param_mcast_group.mem_info.destid = lport;
    }

    CTC_ERROR_RETURN(sys_goldengate_mcast_nh_update(lchip, &nh_param_mcast_group));

    return CTC_E_NONE;

}


STATIC int32
_sys_goldengate_aps_set_bridge_group(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group, DsApsBridge_m* ds_aps_bridge)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    sys_l3if_prop_t w_l3if, p_l3if;

    if (!aps_group->next_w_aps_en)
    {
        gchip   = SYS_MAP_CTC_GPORT_TO_GCHIP(aps_group->working_gport);
        dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(aps_group->working_gport);
        SetDsApsBridge(V,workingDestMap_f, ds_aps_bridge, SYS_ENCODE_DESTMAP(gchip, dest_id));
        SetDsApsBridge(V,workingNextApsBridgeEn_f, ds_aps_bridge, 0);
        SetDsApsBridge(V,workingIsLinkAggregation_f, ds_aps_bridge, 0);
        SetDsApsBridge(V, workingRemoteChip_f, ds_aps_bridge, 0);
        SetDsApsBridge(V,workingUcastId_f, ds_aps_bridge, dest_id);
        if (CTC_IS_LINKAGG_PORT(aps_group->working_gport))
        {
            SetDsApsBridge(V,workingIsLinkAggregation_f, ds_aps_bridge, 1);
        }
        else if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
        {
            SetDsApsBridge(V, workingRemoteChip_f, ds_aps_bridge, 1);
            SetDsApsBridge(V, workingUcastId_f, ds_aps_bridge, 0);
        }

        if (aps_group->w_l3if_id)
        {
            sal_memset(&w_l3if, 0, sizeof(sys_l3if_prop_t));
            w_l3if.l3if_id = aps_group->w_l3if_id;
            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info(lchip, 1, &w_l3if));
            if ((CTC_L3IF_TYPE_PHY_IF == w_l3if.l3if_type) && (w_l3if.gport != aps_group->working_gport))
            {
                ret = CTC_E_APS_INTERFACE_ERROR;
                goto out;
            }
            SetDsApsBridge(V,workingDestVlanPtr_f, ds_aps_bridge, w_l3if.vlan_ptr);
        }
    }
    else
    {
        SetDsApsBridge(V, workingNextApsBridgeEn_f, ds_aps_bridge, 1);
        SetDsApsBridge(V, workingDestMap_f, ds_aps_bridge, aps_group->next_aps.w_group_id);
    }

    if (!aps_group->next_p_aps_en)
    {
        gchip   = SYS_MAP_CTC_GPORT_TO_GCHIP(aps_group->protection_gport);
        dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(aps_group->protection_gport);
        SetDsApsBridge(V, protectingDestMap_f, ds_aps_bridge, SYS_ENCODE_DESTMAP(gchip, dest_id));
        SetDsApsBridge(V, protectingNextApsBridgeEn_f, ds_aps_bridge, 0);
        SetDsApsBridge(V, protectingUcastId_f, ds_aps_bridge, dest_id);
        SetDsApsBridge(V, protectingIsLinkAggregation_f, ds_aps_bridge, 0);
        if (CTC_IS_LINKAGG_PORT(aps_group->protection_gport))
        {
            SetDsApsBridge(V, protectingIsLinkAggregation_f, ds_aps_bridge, 1);
        }
        else if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
        {
            SetDsApsBridge(V, protectingRemoteChip_f, ds_aps_bridge, 1);
            SetDsApsBridge(V, protectingUcastId_f, ds_aps_bridge, 0);
        }

        if (aps_group->p_l3if_id)
        {
            sal_memset(&p_l3if, 0, sizeof(sys_l3if_prop_t));
            p_l3if.l3if_id = aps_group->p_l3if_id;
            CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info(lchip, 1, &p_l3if));
            if ((CTC_L3IF_TYPE_PHY_IF == p_l3if.l3if_type) && (p_l3if.gport != aps_group->protection_gport))
            {
                ret = CTC_E_APS_INTERFACE_ERROR;
                goto out;
            }
            SetDsApsBridge(V, protectingDestVlanPtr_f, ds_aps_bridge, p_l3if.vlan_ptr);
        }
    }
    else
    {
        SetDsApsBridge(V, protectingNextApsBridgeEn_f, ds_aps_bridge, 1);
        SetDsApsBridge(V, protectingDestMap_f, ds_aps_bridge, aps_group->next_aps.p_group_id);
    }

out:
    return ret;
}

int32
sys_goldengate_aps_create_bridge_group(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    uint32 cmd = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    SYS_APS_GROUP_ID_CHECK(group_id);
    CTC_PTR_VALID_CHECK(aps_group);
    CTC_GLOBAL_PORT_CHECK(aps_group->working_gport);
    CTC_GLOBAL_PORT_CHECK(aps_group->protection_gport);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->w_l3if_id);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->p_l3if_id);
    SYS_APS_MPLS_L3IF_CHECK(aps_group);

    if (aps_group->raps_en) /*RAPS protection*/
    {
        p_raps_mcast_node = _sys_goldengate_aps_find_raps_mcast_node(lchip, aps_group->raps_group_id);
        if (NULL == p_raps_mcast_node)
        {
            return CTC_E_APS_RAPS_GROUP_NOT_EXIST;
        }
    }

    SYS_APS_BRIDGE_DUMP(group_id, aps_group);

    if (aps_group->next_w_aps_en)
    {
        SYS_APS_GROUP_ID_CHECK(aps_group->next_aps.w_group_id);
    }
    if (aps_group->next_p_aps_en)
    {
        SYS_APS_GROUP_ID_CHECK(aps_group->next_aps.p_group_id);
    }
    if (aps_group->aps_failover_en)
    {
        if((aps_group->next_w_aps_en)||(aps_group->next_p_aps_en))
        {
            return CTC_E_APS_DONT_SUPPORT_2_LEVEL_HW_BASED_APS;
        }
        if(CTC_IS_LINKAGG_PORT(aps_group->working_gport)||CTC_IS_LINKAGG_PORT(aps_group->protection_gport))
        {
            return CTC_E_APS_DONT_SUPPORT_HW_BASED_APS_FOR_LINKAGG;
        }
    }

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL != p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_EXIST;
        goto out;
    }

    p_aps_bridge = mem_malloc(MEM_APS_MODULE, sizeof(sys_aps_bridge_t));
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_NO_MEMORY;
        goto out;
    }

    sal_memset(p_aps_bridge, 0, sizeof(sys_aps_bridge_t));
    sal_memset(&ds_aps_bridge, 0, sizeof(DsApsBridge_m));
    SYS_APS_FILELD_SET(DsApsBridge_protectingEn_f, aps_group->protect_en);
    SYS_APS_FILELD_SET(DsApsBridge_spmeApsEn_f, 0);
    SYS_APS_FILELD_SET(DsApsBridge_differentNexthop_f, 1);
    if (aps_group->physical_isolated)
    {
        CTC_SET_FLAG(p_aps_bridge->flag, SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED);
        SYS_APS_FILELD_SET(DsApsBridge_differentNexthop_f, 0);
    }
    if(aps_group->raps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_RAPS);
    }
    p_aps_bridge->raps_group_id   = aps_group->raps_group_id;

    if(aps_group->aps_failover_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN);
        SYS_APS_FILELD_SET(DsApsBridge_linkChangeEn_f, 1);
    }
    if(aps_group->protect_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN);
    }
    if(aps_group->next_w_aps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_W_APS_EN);
    }
    if(aps_group->next_p_aps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_P_APS_EN);
    }
    p_aps_bridge->w_l3if_id = aps_group->w_l3if_id;
    p_aps_bridge->p_l3if_id = aps_group->p_l3if_id;

    ret = _sys_goldengate_aps_set_bridge_group(lchip,  group_id, aps_group, &ds_aps_bridge);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_aps_bridge);
        goto out;
    }

    if (FALSE == ctc_vector_add(p_gg_aps_master[lchip]->aps_bridge_vector, group_id, p_aps_bridge))
    {
        mem_free(p_aps_bridge);
        ret = CTC_E_UNEXPECT;
        goto out;
    }

    if(aps_group->raps_en)
    {
        sys_goldengate_get_gchip_id(lchip, &gchip);
        dest_id = aps_group->raps_group_id;
        SYS_APS_FILELD_SET(DsApsBridge_protectingDestMap_f, SYS_ENCODE_MCAST_IPE_DESTMAP(0, dest_id));
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);

    if (CTC_E_NONE == ret)
    {
        p_gg_aps_master[lchip]->aps_used_num++;
    }

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_update_bridge_group(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    SYS_APS_GROUP_ID_CHECK(group_id);
    CTC_PTR_VALID_CHECK(aps_group);
    CTC_GLOBAL_PORT_CHECK(aps_group->working_gport);
    CTC_GLOBAL_PORT_CHECK(aps_group->protection_gport);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->w_l3if_id);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->p_l3if_id);
    SYS_APS_MPLS_L3IF_CHECK(aps_group);
    if (aps_group->next_w_aps_en)
    {
        SYS_APS_GROUP_ID_CHECK(aps_group->next_aps.w_group_id);
    }
    if (aps_group->next_p_aps_en)
    {
        SYS_APS_GROUP_ID_CHECK(aps_group->next_aps.p_group_id);
    }
    SYS_APS_BRIDGE_DUMP(group_id, aps_group);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    if(aps_group->next_w_aps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_W_APS_EN);
    }
    else
    {
        CTC_UNSET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_W_APS_EN);
    }
    if(aps_group->next_p_aps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_P_APS_EN);
    }
    else
    {
        CTC_UNSET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_P_APS_EN);
    }
    p_aps_bridge->w_l3if_id = aps_group->w_l3if_id;
    p_aps_bridge->p_l3if_id = aps_group->p_l3if_id;

    sal_memset(&ds_aps_bridge, 0, sizeof(DsApsBridge_m));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);
    ret = ret ? ret : _sys_goldengate_aps_set_bridge_group(lchip,  group_id, aps_group, &ds_aps_bridge);
    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);

out:
        APS_UNLOCK(lchip);
        return ret;
}

int32
sys_goldengate_aps_remove_bridge_group(uint8 lchip, uint16 group_id)
{
    uint8 gchip = 0;
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    DsApsBridge_m ds_aps_bridge;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id %d\n", group_id);

    SYS_APS_INIT_CHECK(lchip);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_del(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }
    mem_free(p_aps_bridge);

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    SYS_APS_FILELD_SET(DsApsBridge_linkChangeEn_f,0);
    sys_goldengate_get_gchip_id(lchip, &gchip);
    SYS_APS_FILELD_SET(DsApsBridge_workingDestMap_f, SYS_ENCODE_DESTMAP( gchip, SYS_RSV_PORT_DROP_ID));
    SYS_APS_FILELD_SET(DsApsBridge_protectingDestMap_f, SYS_ENCODE_DESTMAP( gchip, SYS_RSV_PORT_DROP_ID));
    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);

    if (CTC_E_NONE == ret)
    {
        p_gg_aps_master[lchip]->aps_used_num = 0? 0 : (p_gg_aps_master[lchip]->aps_used_num - 1);
    }

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_set_bridge_path(uint8 lchip, uint16 group_id, uint16 gport, bool is_working)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, gport=0x%04x\n", group_id, gport);

    SYS_APS_INIT_CHECK(lchip);
    SYS_APS_GROUP_ID_CHECK(group_id);
    CTC_GLOBAL_PORT_CHECK(gport);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
    dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    if (is_working)
    {
        if (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
        {
            SYS_APS_FILELD_SET(DsApsBridge_workingNextApsBridgeEn_f, 0);
            SYS_APS_FILELD_SET(DsApsBridge_workingIsLinkAggregation_f, 0);
            SYS_APS_FILELD_SET(DsApsBridge_workingRemoteChip_f, 0);
            SYS_APS_FILELD_SET(DsApsBridge_workingDestMap_f, SYS_ENCODE_DESTMAP( gchip, dest_id));
            SYS_APS_FILELD_SET(DsApsBridge_workingUcastId_f , dest_id);
            if (CTC_IS_LINKAGG_PORT(gport))
            {
                SYS_APS_FILELD_SET(DsApsBridge_workingIsLinkAggregation_f, 1);
            }
            else if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
            {
                SYS_APS_FILELD_SET(DsApsBridge_workingRemoteChip_f, 1);
                SYS_APS_FILELD_SET(DsApsBridge_workingUcastId_f , 0);
            }
        }
        else
        {
            ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
            goto out;
        }
    }
    else
    {
        if (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))
        {
            SYS_APS_FILELD_SET(DsApsBridge_protectingNextApsBridgeEn_f, 0);
            gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);
            dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
            SYS_APS_FILELD_SET(DsApsBridge_protectingDestMap_f, SYS_ENCODE_DESTMAP( gchip, dest_id));
            SYS_APS_FILELD_SET(DsApsBridge_protectingIsLinkAggregation_f, 0);
            if (CTC_IS_LINKAGG_PORT(gport))
            {
                SYS_APS_FILELD_SET(DsApsBridge_protectingIsLinkAggregation_f, 1);
            }
            else if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
            {
                SYS_APS_FILELD_SET(DsApsBridge_protectingRemoteChip_f, 1);
                SYS_APS_FILELD_SET(DsApsBridge_protectingUcastId_f , 0);
            }
            SYS_APS_FILELD_SET(DsApsBridge_protectingUcastId_f , dest_id);
        }
        else
        {
            ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
            goto out;
        }
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_get_bridge_path(uint8 lchip, uint16 group_id, uint32* gport, bool is_working)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 dest_map = 0;
    uint32 globle_port = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(gport);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        *gport = CTC_MAX_UINT16_VALUE;  /*invalid value*/
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    if (is_working)
    {
        if (CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
        {
            ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
            goto out;
        }
        GetDsApsBridge(A, workingDestMap_f, &ds_aps_bridge, &dest_map);
    }
    else
    {
        if (CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))
        {
            ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
            goto out;
        }
        GetDsApsBridge(A, protectingDestMap_f, &ds_aps_bridge, &dest_map);

    }
    globle_port = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
    *gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(globle_port);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, gport=0x%04x\n", group_id, *gport);

out:
    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_goldengate_aps_set_protection(uint8 lchip, uint16 group_id, bool protect_en)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_val = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, protect_en=%d\n", group_id, protect_en);

    SYS_APS_INIT_CHECK(lchip);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    if(protect_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN);
    }
    else
    {
        CTC_UNSET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN);
    }

    cmd = DRV_IOW(DsApsBridge_t, DsApsBridge_protectingEn_f);
    field_val = (TRUE == protect_en) ? 1 : 0;
    ret = ret ? ret : DRV_IOCTL(lchip, group_id, cmd, &field_val);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_get_protection(uint8 lchip, uint16 group_id, bool* protect_en)
{
    int32 ret = CTC_E_NONE;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(protect_en);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);

    if (NULL == p_aps_bridge)
    {
        *protect_en = FALSE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
    {
        uint32 protecting_en;
        uint32 cmd = 0;
        cmd = DRV_IOR(DsApsBridge_t, DsApsBridge_protectingEn_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &protecting_en), p_gg_aps_master[lchip]->p_aps_mutex);
        *protect_en = protecting_en;
    }
    else
    {
        *protect_en = CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN)?1:0;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, protect_en=%d\n", group_id, *protect_en);

    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_update_tunnel(uint8 lchip, uint16 group_id, uint32 ds_edit_ptr, bool is_protection)
{

    sys_aps_bridge_t* p_aps_bridge = NULL;
    int32 ret = CTC_E_NONE;
    uint32 cmd      = 0;
    DsApsBridge_m ds_aps_bridge;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_GROUP_ID_CHECK(group_id);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, ds_l2edit_ptr=%d, is_protection = %d\n", group_id, ds_edit_ptr, is_protection);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(DsApsBridge_m));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    if (TRUE == is_protection)
    {
        SYS_APS_FILELD_SET(DsApsBridge_protectingOuterEditPtr_f, ds_edit_ptr);
    }
    else
    {
        SYS_APS_FILELD_SET(DsApsBridge_workingOuterEditPtr_f, ds_edit_ptr);
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge);

    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_get_ports(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;
    uint32 cmd = 0;
    uint32 dest_map = 0;
    uint32 globle_port = 0;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    sal_memset(aps_group, 0, sizeof(ctc_aps_bridge_group_t));
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        APS_UNLOCK(lchip);
        return CTC_E_APS_GROUP_NOT_EXIST;
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    aps_group->protect_en = p_aps_bridge->flag & SYS_APS_FLAG_PROTECT_EN;
    if (p_aps_bridge->flag & SYS_APS_FLAG_NEXT_W_APS_EN)
    {
        aps_group->next_w_aps_en = 1;
        aps_group->next_aps.w_group_id = GetDsApsBridge(V, workingDestMap_f, &ds_aps_bridge);
    }
    else
    {
        GetDsApsBridge(A, workingDestMap_f, &ds_aps_bridge, &dest_map);
        globle_port = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
        aps_group->working_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(globle_port);
        aps_group->w_l3if_id = p_aps_bridge->w_l3if_id;
    }

    if (p_aps_bridge->flag & SYS_APS_FLAG_NEXT_P_APS_EN)
    {
        aps_group->next_p_aps_en = 1;
        aps_group->next_aps.p_group_id = GetDsApsBridge(V, protectingDestMap_f, &ds_aps_bridge);
    }
    else
    {
        GetDsApsBridge(A, protectingDestMap_f, &ds_aps_bridge, &dest_map);
        globle_port = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
        aps_group->protection_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(globle_port);
        aps_group->p_l3if_id = p_aps_bridge->p_l3if_id;
    }

    APS_UNLOCK(lchip);
    return CTC_E_NONE;

}

int32
sys_goldengate_aps_set_share_nh(uint8 lchip, uint16 group_id, bool share_nh)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_val = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d,share_nh=%d", group_id, share_nh);

    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    cmd = DRV_IOW(DsApsBridge_t, DsApsBridge_differentNexthop_f);
    field_val = (TRUE == share_nh) ? 0 : 1;
    ret = DRV_IOCTL(lchip, group_id, cmd, &field_val);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_get_share_nh(uint8 lchip, uint16 group_id, bool* share_nh)
{
    int32 ret = CTC_E_NONE;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(share_nh);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        *share_nh = 0;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }

    *share_nh = CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d,share_nh=%d\n", group_id, *share_nh);

    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_goldengate_aps_set_spme_en(uint8 lchip, uint16 group_id, bool spme_en)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_val = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d,spme_en=%d\n", group_id, spme_en);

    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    cmd = DRV_IOW(DsApsBridge_t, DsApsBridge_spmeApsEn_f);
    field_val = (TRUE == spme_en) ? 1 : 0;
    ret = DRV_IOCTL(lchip, group_id, cmd, &field_val);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_goldengate_aps_get_next_group(uint8 lchip, uint16 group_id, uint16* next_group_id, bool is_working)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(next_group_id);
    SYS_APS_GROUP_ID_CHECK(group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    *next_group_id = CTC_MAX_UINT16_VALUE;
    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }
    if ((is_working && (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN)))
        || (!is_working && (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))))
    {
        APS_UNLOCK(lchip);
        return CTC_E_NONE;

    }
    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gg_aps_master[lchip]->p_aps_mutex);

    if (is_working)
    {
        *next_group_id = GetDsApsBridge(V, workingDestMap_f, &ds_aps_bridge);
    }
    else
    {
        *next_group_id = GetDsApsBridge(V, protectingDestMap_f, &ds_aps_bridge);
    }
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "group_id=%d, next_aps_id=%d\n", group_id, *next_group_id);

    APS_UNLOCK(lchip);
    return ret;
}


int32
sys_goldengate_aps_get_current_working_port(uint8 lchip, uint16 group_id, uint16* gport)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    DsApsBridge_m ds_aps_bridge;
    uint8 protection    = 0;
    uint8 next_is_aps   = 0;
    uint32 dest_map     = 0;
    uint32 globle_port = 0;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(gport);
    SYS_APS_GROUP_ID_CHECK(group_id);

    /*check aps exist or not*/
    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        *gport = CTC_MAX_UINT16_VALUE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gg_aps_master[lchip]->p_aps_mutex);
    }
    APS_UNLOCK(lchip);

    /*Get 1st level aps group*/
    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge));

    protection = GetDsApsBridge(V, protectingEn_f, &ds_aps_bridge);

    if (protection)
    {
        next_is_aps = GetDsApsBridge(V, protectingNextApsBridgeEn_f, &ds_aps_bridge);
        dest_map    = GetDsApsBridge(V, protectingDestMap_f, &ds_aps_bridge);
    }
    else
    {
        next_is_aps = GetDsApsBridge(V, workingNextApsBridgeEn_f, &ds_aps_bridge);
        dest_map    = GetDsApsBridge(V, workingDestMap_f, &ds_aps_bridge);
    }

    if(!next_is_aps)
    {
        globle_port = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
        *gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(globle_port);
    }
    else
    {/*Get 2nd level aps group*/
        return sys_goldengate_aps_get_current_working_port(lchip, dest_map, gport);
    }

    return ret;
}


int32
sys_goldengate_aps_show_status(uint8 lchip)
{
    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DUMP("-----------APS status------------\n");
    SYS_APS_DUMP("%-22s: %d\n", "APS group total", TABLE_MAX_INDEX(DsApsBridge_t));
    SYS_APS_DUMP("%-22s: %d\n", "APS group allocated", p_gg_aps_master[lchip]->aps_used_num);

    return CTC_E_NONE;
}



int32
_sys_goldengate_aps_show_one_bridge(uint8 lchip, uint16 group_id, uint8 detail)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    ctc_aps_bridge_group_t aps_group;
    char w_path[16]={0};
    char p_path[16]={0};
    char* w_flag = "P";
    char* p_flag = "P";
    char w_ifid[8] = "-";
    char p_ifid[8] = "-";
    char* hw = "NO";
    char* w_p = "W";
    uint8 raps_valid = 0;
    bool protection_en;

    SYS_APS_INIT_CHECK(lchip);
    p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
    if (NULL == p_aps_bridge)
    {
        return CTC_E_APS_GROUP_NOT_EXIST;
    }

    sal_memset(&aps_group, 0, sizeof(ctc_aps_bridge_group_t));
    sys_goldengate_aps_get_ports(lchip, group_id, &aps_group);
    if(!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        sal_sprintf(w_path, "0x%04x", aps_group.working_gport);
         w_flag = "P";
    }
    else
    {
        sal_sprintf(w_path, "%d", aps_group.next_aps.w_group_id);
         w_flag = "G";
    }

    if (p_aps_bridge->w_l3if_id)
    {
        sal_sprintf(w_ifid, "%d", p_aps_bridge->w_l3if_id);
    }

    if(!CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
    {
        if (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))
        {
            sal_sprintf(p_path, "0x%04x", aps_group.protection_gport);
            p_flag = "P";
        }
        else
        {
            sal_sprintf(p_path, "%d", aps_group.next_aps.p_group_id);
            p_flag = "G";
        }

        if (p_aps_bridge->p_l3if_id)
        {
            sal_sprintf(p_ifid, "%d", p_aps_bridge->p_l3if_id);
        }

         /*if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED))*/
         /*{*/
         /*    SYS_APS_DUMP("%-28s: TRUE\n","Use physical isolated path");*/
         /*}*/
    }
    else
    {
        sal_sprintf(p_path, "%d", p_aps_bridge->raps_group_id);
        p_flag = "R";

        p_raps_mcast_node = _sys_goldengate_aps_find_raps_mcast_node(lchip, p_aps_bridge->raps_group_id);
        if (p_raps_mcast_node)
        {
            raps_valid = 1;
        }
    }

    if (CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
    {
        hw = "YES";
    }

    CTC_ERROR_RETURN(sys_goldengate_aps_get_protection(lchip, group_id, &protection_en));
    if (protection_en)
    {
        w_p = "P";
    }
    else
    {
        w_p = "W";
    }

    SYS_APS_DUMP("%-6d%-9s%-6s%-8s%-9s%-6s%-7s%-6s%-6s\n", group_id, w_path, w_flag, w_ifid, p_path, p_flag, p_ifid, w_p, hw);
    if (detail)
    {
        SYS_APS_DUMP("\nDetail information\n");
        SYS_APS_DUMP("----------------------\n");
        SYS_APS_DUMP("%-15s:%d\n","--DsApsBridge", group_id);
        if (raps_valid)
        {
            SYS_APS_DUMP("%-15s:%d\n", "--Raps nexthop", p_raps_mcast_node->raps_nhid);
        }
    }
    return CTC_E_NONE;
}


int32
sys_goldengate_aps_show_bridge(uint8 lchip, uint16 group_id, uint8 detail)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;
    uint16 i = 0;

    SYS_APS_INIT_CHECK(lchip);

    if (0xFFFF == group_id)
    {
        if (0 == p_gg_aps_master[lchip]->aps_used_num)
        {
            SYS_APS_DUMP("Not any aps group!!\n");
            return CTC_E_APS_GROUP_NOT_EXIST;
        }
    }
    else
    {
        p_aps_bridge = ctc_vector_get(p_gg_aps_master[lchip]->aps_bridge_vector, group_id);
        if (NULL == p_aps_bridge)
        {
            SYS_APS_DUMP("No aps group %d!!\n", group_id);
            return CTC_E_APS_GROUP_NOT_EXIST;
        }
    }

    SYS_APS_DUMP("Flag: %-15s\n", "P - Port");
    SYS_APS_DUMP("      %-15s\n",  "G - APS Group");
    SYS_APS_DUMP("      %-15s\n\n", "R - RAPS Group");
    SYS_APS_DUMP("-------------------------------------------------------------\n");
    SYS_APS_DUMP("%-6s%-7s%-7s%-9s%-7s%-7s%-8s%-6s%-6s\n", "G_ID", "W", "W-Flag", "W-IFID", "P", "P-Flag", "P-IFID", "W/P", "HW");
    SYS_APS_DUMP("-------------------------------------------------------------\n");


    if (0xFFFF == group_id)
    {
        for (i = 0; i < TABLE_MAX_INDEX(DsApsBridge_t); i++)
        {
               _sys_goldengate_aps_show_one_bridge(lchip, i, 0);
        }
    }
    else
    {
        _sys_goldengate_aps_show_one_bridge(lchip, group_id, detail);
    }

    return CTC_E_NONE;
}




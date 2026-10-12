/**
 @file sys_greatbelt_aps.c

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
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_aps.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_mpls.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_port.h"

#include "greatbelt/include/drv_io.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define SYS_BUILD_DESTMAP(is_mcast, chip_id, destid) \
    (((is_mcast & 0x1) << 21) | ((chip_id & 0x1F) << 16) | (destid & 0xFFFF))

#define SYS_APS_L3IFID_VAILD_CHECK(l3if_id)         \
    if (l3if_id > SYS_GB_MAX_CTC_L3IF_ID) \
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
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_aps_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_aps_master_t* p_gb_aps_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

struct sys_raps_mcast_node_s
{
    ctc_slistnode_t head;
    uint32 group_id;
    uint32 raps_nhid;
};
typedef struct sys_raps_mcast_node_s sys_raps_mcast_node_t;

/****************************************************************************
 *
* Function
*
*****************************************************************************/

int32
sys_greatbelt_aps_fast_detec(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 cmd = 0;
    met_fifo_link_down_state_record_t chan_down;
    uint8 index = 0;
    uint8 linkdown_chan = 0;
    ctc_intr_type_t type;

    type.intr = CTC_INTR_GB_FUNC_APS_FAILOVER;
    type.sub_intr = 0;

    /* APS protect switch done interrupt */
    sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);

    sal_memset(&chan_down, 0, sizeof(q_mgr_enq_scan_link_down_chan_record_t));

    cmd = DRV_IOR(MetFifoLinkScanDoneState_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_down:0x%x-0x%x\n ",
        chan_down.link_down_state0, chan_down.link_down_state1);

    for (index = 0; index < 64; index++)
    {
        if (index < 32)
        {
            if ((chan_down.link_down_state0 >>index) & 0x1)
            {
                linkdown_chan = index;
                chan_down.link_down_state0 |= (1<<linkdown_chan);
            }
        }
        else
        {
            if ((chan_down.link_down_state1 >>(index-32)) & 0x1)
            {
                linkdown_chan = index;
                chan_down.link_down_state1 |= (1<<(linkdown_chan-32));
            }
        }

    }

    cmd = DRV_IOW(MetFifoLinkScanDoneState_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));

    /* release linkdown interrupt */
    sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

    return CTC_E_NONE;
}


int32
sys_greatbelt_aps_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 cmd_u = 0;
    uint16 index = 0;
    ds_aps_bridge_t ds_aps_bridge;
    ds_aps_select_t ds_aps_select;
    met_fifo_ctl_t  met_fifo_ctl;

    if (NULL != p_gb_aps_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gb_aps_master[lchip] = mem_malloc(MEM_APS_MODULE, sizeof(sys_aps_master_t));
    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_aps_master[lchip], 0, sizeof(sys_aps_master_t));

    sal_mutex_create(&(p_gb_aps_master[lchip]->p_aps_mutex));
    if (NULL == p_gb_aps_master[lchip]->p_aps_mutex)
    {
        mem_free(p_gb_aps_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gb_aps_master[lchip]->aps_bridge_vector
        = ctc_vector_init(CTC_APS_GROUP_BLOCK, SYS_GB_MAX_APS_GROUP_NUM / CTC_APS_GROUP_BLOCK);

    if (NULL == p_gb_aps_master[lchip]->aps_bridge_vector)
    {
        sal_mutex_destroy(p_gb_aps_master[lchip]->p_aps_mutex);
        mem_free(p_gb_aps_master[lchip]);

        return CTC_E_NO_MEMORY;
    }

    ctc_vector_reserve(p_gb_aps_master[lchip]->aps_bridge_vector, 1);

    p_gb_aps_master[lchip]->raps_grp_list = ctc_slist_new();

    if (NULL == p_gb_aps_master[lchip]->raps_grp_list)
    {
        mem_free(p_gb_aps_master[lchip]);
        return CTC_E_NOT_INIT;
    }

    /*initial asic table*/
    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    sal_memset(&ds_aps_select, 0, sizeof(ds_aps_select));

    cmd_u = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    cmd = DRV_IOW(DsApsSelect_t, DRV_ENTRY_FLAG);


    for (index = 0; index < SYS_GB_MAX_APS_GROUP_NUM; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd_u, &ds_aps_bridge));
    }

    for (index = 0; index < SYS_GB_MAX_APS_GROUP_NUM / BITS_NUM_OF_WORD; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_aps_select));
    }


     /* configure hw-based failover aps*/

    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl_t));
     /*sal_memset(&ds_aps_channel_map, 0, sizeof(ds_aps_channel_map_t));*/

    cmd = DRV_IOR(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    met_fifo_ctl.link_change_en = 1;
    cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_aps_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_deinit(uint8 lchip)
{
    ctc_slistnode_t* node = NULL, * next_node = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free aps slist*/
    CTC_SLIST_LOOP_DEL(p_gb_aps_master[lchip]->raps_grp_list, node, next_node)
    {
        ctc_slist_delete_node(p_gb_aps_master[lchip]->raps_grp_list, node);
        mem_free(node);
    }
    ctc_slist_delete(p_gb_aps_master[lchip]->raps_grp_list);

    /*free aps bridge*/
    ctc_vector_traverse(p_gb_aps_master[lchip]->aps_bridge_vector, (vector_traversal_fn)_sys_greatbelt_aps_free_node_data, NULL);
    ctc_vector_release(p_gb_aps_master[lchip]->aps_bridge_vector);

    sal_mutex_destroy(p_gb_aps_master[lchip]->p_aps_mutex);
    mem_free(p_gb_aps_master[lchip]);

    return CTC_E_NONE;
}

STATIC sys_raps_mcast_node_t*
_sys_greatbelt_aps_find_raps_mcast_node(uint8 lchip, uint16 group_id)
{
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    ctc_slistnode_t* lisnode = NULL;

    CTC_SLIST_LOOP(p_gb_aps_master[lchip]->raps_grp_list, lisnode)
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
sys_greatbelt_aps_create_raps_mcast_group(uint8 lchip, uint16 group_id)
{
     /* uint8 chip_id, chip_id2 = 0;*/
    sys_nh_param_mcast_group_t  nh_mcast_group;
    sys_raps_mcast_node_t* p_raps_mcast_node;
     /* uint8 lchip_num = 0;*/

    SYS_APS_INIT_CHECK(lchip);
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    /*create group*/
    nh_mcast_group.dsfwd_valid = 0;

    CTC_ERROR_RETURN(sys_greatbelt_mcast_nh_create(lchip, group_id, &nh_mcast_group));
    p_raps_mcast_node = (sys_raps_mcast_node_t*)mem_malloc(MEM_APS_MODULE, sizeof(sys_raps_mcast_node_t));
    if (NULL == p_raps_mcast_node)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_raps_mcast_node, 0, sizeof(sys_raps_mcast_node_t));
    p_raps_mcast_node->raps_nhid = nh_mcast_group.nhid;
    p_raps_mcast_node->group_id = group_id;

    ctc_slist_add_tail(p_gb_aps_master[lchip]->raps_grp_list, &p_raps_mcast_node->head);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_remove_raps_mcast_group(uint8 lchip, uint16 group_id)
{
    int32 ret = CTC_E_NONE;

    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;

    SYS_APS_INIT_CHECK(lchip);
    p_raps_mcast_node = _sys_greatbelt_aps_find_raps_mcast_node(lchip, group_id);

    if (NULL == p_raps_mcast_node)
    {
        return CTC_E_APS_RAPS_GROUP_NOT_EXIST;
    }

    ret = sys_greatbelt_mcast_nh_delete(lchip, p_raps_mcast_node->raps_nhid);

    ctc_slist_delete_node(p_gb_aps_master[lchip]->raps_grp_list, &p_raps_mcast_node->head);
    mem_free(p_raps_mcast_node);

    return ret;
}

int32
sys_greatbelt_aps_update_raps_mcast_member(uint8 lchip, ctc_raps_member_t* p_raps_mem)
{
    sys_nh_param_mcast_group_t  nh_param_mcast_group;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;
    uint8 lport = 0;
    uint16 dest_port = 0;

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_raps_mem);
    sal_memset(&nh_param_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    p_raps_mcast_node = _sys_greatbelt_aps_find_raps_mcast_node(lchip, p_raps_mem->group_id);

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

    CTC_ERROR_RETURN(sys_greatbelt_mcast_nh_update(lchip, &nh_param_mcast_group));


    return CTC_E_NONE;

}

int32
sys_greatbelt_aps_create_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group)
{
    int32 ret = 0;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    uint32 cmd = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    ds_aps_bridge_t ds_aps_bridge;
    sys_l3if_prop_t w_l3if, p_l3if;
    sys_raps_mcast_node_t* p_raps_mcast_node = NULL;

    SYS_APS_INIT_CHECK(lchip);

    if (aps_group->raps_en) /*RAPS protection*/
    {
        p_raps_mcast_node = _sys_greatbelt_aps_find_raps_mcast_node(lchip, aps_group->raps_group_id);
        if (NULL == p_raps_mcast_node)
        {
            return CTC_E_APS_RAPS_GROUP_NOT_EXIST;
        }
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);
    CTC_PTR_VALID_CHECK(aps_group);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, aps_group->working_gport));
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, aps_group->protection_gport));
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->w_l3if_id);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->p_l3if_id);
    SYS_APS_MPLS_L3IF_CHECK(aps_group);

    SYS_APS_BRIDGE_DUMP(aps_bridge_group_id, aps_group);

    if (aps_group->next_w_aps_en)
    {
        CTC_APS_GROUP_ID_CHECK(aps_group->next_aps.w_group_id);
    }

    if (aps_group->next_p_aps_en)
    {
        CTC_APS_GROUP_ID_CHECK(aps_group->next_aps.p_group_id);
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
    /*if the group exist, return error*/
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);
    if (NULL != p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_EXIST;
        goto out;
    }

    /*allocate space for new aps bridge group*/
    p_aps_bridge = mem_malloc(MEM_APS_MODULE, sizeof(sys_aps_bridge_t));
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_NO_MEMORY;
        goto out;
    }

    sal_memset(p_aps_bridge, 0, sizeof(sys_aps_bridge_t));

    p_aps_bridge->protection_gport   = aps_group->protection_gport;
    p_aps_bridge->working_gport      = aps_group->working_gport;

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

    p_aps_bridge->next_w_aps = aps_group->next_aps.w_group_id;
    p_aps_bridge->next_p_aps = aps_group->next_aps.p_group_id;
    p_aps_bridge->w_l3if_id = aps_group->w_l3if_id;
    p_aps_bridge->p_l3if_id = aps_group->p_l3if_id;

    if(aps_group->physical_isolated)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED);
    }

    if(aps_group->raps_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_RAPS);
    }
    p_aps_bridge->raps_group_id   = aps_group->raps_group_id;
    p_aps_bridge->raps_nhid       = (aps_group->raps_en)? p_raps_mcast_node->raps_nhid : 0;

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));

    ds_aps_bridge.protecting_en = aps_group->protect_en;
    ds_aps_bridge.different_l2_edit_ptr_en = 1;
    ds_aps_bridge.l2_edit_ptr_shift_en = 0;

    if (1 == aps_group->physical_isolated)
    {
        ds_aps_bridge.protecting_next_hop_ptr = 1;
    }
    else
    {
        ds_aps_bridge.replication_ctl_en = 1;
    }

    if (!aps_group->next_w_aps_en)
    {
        gchip   = CTC_MAP_GPORT_TO_GCHIP(aps_group->working_gport);
        dest_id = CTC_MAP_GPORT_TO_LPORT(aps_group->working_gport);
        ds_aps_bridge.working_dest_map    = SYS_BUILD_DESTMAP(0, gchip, dest_id);

        if (CTC_IS_LINKAGG_PORT(aps_group->working_gport))
        {
            ds_aps_bridge.working_is_link_aggregation = 1;
        }

        ds_aps_bridge.working_ucast_id = dest_id;

        if (aps_group->w_l3if_id)
        {
            /* getting l3 interface id */
            sal_memset(&w_l3if, 0, sizeof(sys_l3if_prop_t));
            w_l3if.l3if_id = aps_group->w_l3if_id;
            ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &w_l3if);
            if (ret < 0)
            {
                mem_free(p_aps_bridge);
                goto out;
            }
            if ((CTC_L3IF_TYPE_PHY_IF == w_l3if.l3if_type) && (w_l3if.gport != aps_group->working_gport))
            {
                ret = CTC_E_APS_INTERFACE_ERROR;
                mem_free(p_aps_bridge);
                goto out;
            }

            ds_aps_bridge.working_dest_vlan_ptr = w_l3if.vlan_ptr;
        }
    }
    else
    {
        ds_aps_bridge.working_next_aps_bridge_en = 1;
        ds_aps_bridge.working_dest_map    = p_aps_bridge->next_w_aps;
    }

    if (!aps_group->next_p_aps_en)
    {
        /*APS protection*/
        if (!CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
        {
            gchip   = CTC_MAP_GPORT_TO_GCHIP(aps_group->protection_gport);
            dest_id = CTC_MAP_GPORT_TO_LPORT(aps_group->protection_gport);
            ds_aps_bridge.protecting_dest_map = SYS_BUILD_DESTMAP(0, gchip, dest_id);
            if (CTC_IS_LINKAGG_PORT(aps_group->protection_gport))
            {
                ds_aps_bridge.protecting_is_link_aggregation = 1;
            }

            ds_aps_bridge.protecting_ucast_id = dest_id;

            if (aps_group->p_l3if_id)
            {
                /* getting l3 interface id */
                sal_memset(&p_l3if, 0, sizeof(sys_l3if_prop_t));
                p_l3if.l3if_id = aps_group->p_l3if_id;
                ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &p_l3if);
                if (ret < 0)
                {
                    mem_free(p_aps_bridge);
                    goto out;
                }
                if ((CTC_L3IF_TYPE_PHY_IF == p_l3if.l3if_type) && (p_l3if.gport != aps_group->protection_gport))
                {
                    ret = CTC_E_APS_INTERFACE_ERROR;
                    mem_free(p_aps_bridge);
                    goto out;
                }

                ds_aps_bridge.protecting_dest_vlan_ptr = p_l3if.vlan_ptr;
            }
        }
        else
        {
            ds_aps_bridge.protecting_ucast_id = aps_group->raps_group_id;
        }
    }
    else
    {
        ds_aps_bridge.protecting_next_aps_bridge_en = 1;
        ds_aps_bridge.protecting_dest_map   = p_aps_bridge->next_p_aps;
    }

    /* Configure hw-based failover aps  */
    if(aps_group->aps_failover_en)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN);
        ds_aps_bridge.link_change_en = 1;
    }

    /*add the group to aps bridge vector*/
    if (FALSE == ctc_vector_add(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id, p_aps_bridge))
    {
        mem_free(p_aps_bridge);
        ret = CTC_E_UNEXPECT;
        goto out;
    }


    sys_greatbelt_get_gchip_id(lchip, &gchip);
    /*RAPS protection*/
    if (CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_RAPS))
    {

        dest_id = aps_group->raps_group_id;
        ds_aps_bridge.protecting_dest_map = SYS_BUILD_DESTMAP(1, gchip, dest_id);
    }

    if (!aps_group->next_w_aps_en)
    {

        if (!CTC_IS_LINKAGG_PORT(aps_group->working_gport)
            &&(CTC_MAP_GPORT_TO_GCHIP(aps_group->working_gport) != gchip))
        {
            ds_aps_bridge.working_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.working_remote_chip = 0;
        }
    }

    if (!aps_group->next_p_aps_en)
    {
        if (!CTC_IS_LINKAGG_PORT(aps_group->protection_gport)
            &&CTC_MAP_GPORT_TO_GCHIP(aps_group->protection_gport) != gchip)
        {
            ds_aps_bridge.protecting_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.protecting_remote_chip = 0;
        }
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);


out:
    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_greatbelt_aps_remove_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id)
{
    uint8 gchip = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    ds_aps_bridge_t ds_aps_bridge;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id %d\n", aps_bridge_group_id);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    /*delete the group from the vector, return error if not exist*/
    p_aps_bridge = ctc_vector_del(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    /* Configure hw-based failover aps  */
    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
    {
        ds_aps_bridge.link_change_en = 0;
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

    /*free the memory of the group*/
    if (NULL != p_aps_bridge)
    {
        mem_free(p_aps_bridge);
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    ds_aps_bridge.working_dest_map    = SYS_BUILD_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_DROP);
    ds_aps_bridge.protecting_dest_map    = SYS_BUILD_DESTMAP(0, gchip, SYS_RESERVE_PORT_ID_DROP);

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_greatbelt_aps_set_aps_bridge_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint16 gport)
{
    int32 ret = 0;
    uint32 cmd = 0;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    ds_aps_bridge_t ds_aps_bridge;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, gport=0x%04x\n", aps_bridge_group_id, gport);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    p_aps_bridge->working_gport = gport;

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

    if(!CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        ds_aps_bridge.working_next_aps_bridge_en = 0;

        gchip   = CTC_MAP_GPORT_TO_GCHIP(gport);
        dest_id = CTC_MAP_GPORT_TO_LPORT(gport);
        ds_aps_bridge.working_dest_map    = SYS_BUILD_DESTMAP(0, gchip, dest_id);

        /* mcast and linkagg property */
        if (CTC_IS_LINKAGG_PORT(gport))
        {
            ds_aps_bridge.working_is_link_aggregation = 1;
        }

        ds_aps_bridge.working_ucast_id = dest_id;

    }
    else
    {
        ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
        goto out;
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);


    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        if (!CTC_IS_LINKAGG_PORT(gport)
            &&CTC_MAP_GPORT_TO_GCHIP(gport) != gchip)
        {
            ds_aps_bridge.working_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.working_remote_chip = 0;
        }
    }

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);

out:
    APS_UNLOCK(lchip);
    return ret;

}


int32
sys_greatbelt_aps_set_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group)
{
    int32 ret = 0;
    uint8 gchip   = 0;
    uint16 dest_id   = 0;
    uint32 cmd = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    ds_aps_bridge_t ds_aps_bridge;
    sys_l3if_prop_t w_l3if, p_l3if;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);
    CTC_PTR_VALID_CHECK(aps_group);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, aps_group->working_gport));
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, aps_group->protection_gport));
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->w_l3if_id);
    SYS_APS_L3IFID_VAILD_CHECK(aps_group->p_l3if_id);
    SYS_APS_MPLS_L3IF_CHECK(aps_group);

    SYS_APS_BRIDGE_DUMP(aps_bridge_group_id, aps_group);

    if (aps_group->next_w_aps_en)
    {
        CTC_APS_GROUP_ID_CHECK(aps_group->next_aps.w_group_id);
    }

    if (aps_group->next_p_aps_en)
    {
        CTC_APS_GROUP_ID_CHECK(aps_group->next_aps.p_group_id);
    }

    APS_LOCK(lchip);
    /*if the group exist, return error*/
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);
    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    /* don't support update physical_isolated protect_en aps_failover_en raps_en*/
    if((aps_group->physical_isolated ^ CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED))
       ||(aps_group->aps_failover_en^ CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
       ||(aps_group->raps_en ^ CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
       ||CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
    {
        ret = CTC_E_INVALID_PARAM;
        goto out;
    }

    p_aps_bridge->protection_gport   = aps_group->protection_gport;
    p_aps_bridge->working_gport      = aps_group->working_gport;

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

    p_aps_bridge->next_w_aps = aps_group->next_aps.w_group_id;
    p_aps_bridge->next_p_aps = aps_group->next_aps.p_group_id;
    p_aps_bridge->w_l3if_id = aps_group->w_l3if_id;
    p_aps_bridge->p_l3if_id = aps_group->p_l3if_id;


    sys_greatbelt_get_gchip_id(lchip, &gchip);

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));

    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);

    if (!aps_group->next_w_aps_en)
    {
        gchip   = CTC_MAP_GPORT_TO_GCHIP(aps_group->working_gport);
        dest_id = CTC_MAP_GPORT_TO_LPORT(aps_group->working_gport);
        ds_aps_bridge.working_dest_map    = SYS_BUILD_DESTMAP(0, gchip, dest_id);
        ds_aps_bridge.working_next_aps_bridge_en = 0;

        if (CTC_IS_LINKAGG_PORT(aps_group->working_gport))
        {
            ds_aps_bridge.working_is_link_aggregation = 1;
        }

        ds_aps_bridge.working_ucast_id = dest_id;

        if (aps_group->w_l3if_id)
        {
            /* getting l3 interface id */
            sal_memset(&w_l3if, 0, sizeof(sys_l3if_prop_t));
            w_l3if.l3if_id = aps_group->w_l3if_id;
            ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &w_l3if);
            if (ret < 0)
            {
                goto out;
            }
            if ((CTC_L3IF_TYPE_PHY_IF == w_l3if.l3if_type) && (w_l3if.gport != aps_group->working_gport))
            {
                ret = CTC_E_APS_INTERFACE_ERROR;
                goto out;
            }

            ds_aps_bridge.working_dest_vlan_ptr = w_l3if.vlan_ptr;
        }
    }
    else
    {
        ds_aps_bridge.working_next_aps_bridge_en = 1;
        ds_aps_bridge.working_dest_map    = p_aps_bridge->next_w_aps;
    }

    if (!aps_group->next_p_aps_en)
    {
        /*APS protection*/
        gchip   = CTC_MAP_GPORT_TO_GCHIP(aps_group->protection_gport);
        dest_id = CTC_MAP_GPORT_TO_LPORT(aps_group->protection_gport);
        ds_aps_bridge.protecting_dest_map = SYS_BUILD_DESTMAP(0, gchip, dest_id);
        ds_aps_bridge.protecting_next_aps_bridge_en = 0;
        if (CTC_IS_LINKAGG_PORT(aps_group->protection_gport))
        {
            ds_aps_bridge.protecting_is_link_aggregation = 1;
        }

        ds_aps_bridge.protecting_ucast_id = dest_id;

        if (aps_group->p_l3if_id)
        {
            /* getting l3 interface id */
            sal_memset(&p_l3if, 0, sizeof(sys_l3if_prop_t));
            p_l3if.l3if_id = aps_group->p_l3if_id;
            ret = sys_greatbelt_l3if_get_l3if_info(lchip, 1, &p_l3if);
            if (ret < 0)
                {
                goto out;
            }
            if ((CTC_L3IF_TYPE_PHY_IF == p_l3if.l3if_type) && (p_l3if.gport != aps_group->protection_gport))
            {
                ret = CTC_E_APS_INTERFACE_ERROR;
                goto out;
            }

            ds_aps_bridge.protecting_dest_vlan_ptr = p_l3if.vlan_ptr;
        }
    }
    else
    {
        ds_aps_bridge.protecting_next_aps_bridge_en = 1;
        ds_aps_bridge.protecting_dest_map   = p_aps_bridge->next_p_aps;
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (!aps_group->next_w_aps_en)
    {

        if (!CTC_IS_LINKAGG_PORT(aps_group->working_gport)
            &&(CTC_MAP_GPORT_TO_GCHIP(aps_group->working_gport) != gchip))
        {
            ds_aps_bridge.working_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.working_remote_chip = 0;
        }
    }

    if (!aps_group->next_p_aps_en)
    {
        if (!CTC_IS_LINKAGG_PORT(aps_group->protection_gport)
            &&CTC_MAP_GPORT_TO_GCHIP(aps_group->protection_gport) != gchip)
        {
            ds_aps_bridge.protecting_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.protecting_remote_chip = 0;
        }
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);


out:
    APS_UNLOCK(lchip);
    return ret;
}



int32
sys_greatbelt_aps_get_aps_bridge_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint32* gport)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(gport);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *gport = CTC_MAX_UINT16_VALUE;  /*invalid value*/
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        APS_UNLOCK(lchip);
        return CTC_E_APS_GROUP_NEXT_APS_EXIST;
    }

    *gport = p_aps_bridge->working_gport;
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, gport=0x%04x\n", aps_bridge_group_id, *gport);
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_get_aps_bridge_current_working_port(uint8 lchip, uint16 aps_bridge_group_id, uint16* gport)
{
    sys_aps_bridge_t* p_aps_bridge_level1 = NULL, * p_aps_bridge_level2 = NULL, * p_aps_bridge_level3 = NULL;
    bool next_p_aps_en = FALSE;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(gport);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge_level1 = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge_level1)
    {
        *gport = CTC_MAX_UINT16_VALUE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_NEXT_W_APS_EN)
        ||CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_NEXT_P_APS_EN))
    {
        next_p_aps_en = CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_PROTECT_EN)
                            ? CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_NEXT_P_APS_EN)
                            : CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_NEXT_W_APS_EN);
    }

    if (next_p_aps_en)
    {
        p_aps_bridge_level2 = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, p_aps_bridge_level1->next_w_aps);
        if (NULL == p_aps_bridge_level2)
        {
            *gport = CTC_MAX_UINT16_VALUE;
            CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
        }

        if(CTC_FLAG_ISSET(p_aps_bridge_level2->flag,SYS_APS_FLAG_PROTECT_EN))
        {
            if(CTC_FLAG_ISSET(p_aps_bridge_level2->flag,SYS_APS_FLAG_NEXT_P_APS_EN))
            {
                p_aps_bridge_level3 = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, p_aps_bridge_level2->next_p_aps);
                if (NULL == p_aps_bridge_level3)
                {
                    *gport = CTC_MAX_UINT16_VALUE;
                    CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
                }

                if(CTC_FLAG_ISSET(p_aps_bridge_level3->flag,SYS_APS_FLAG_PROTECT_EN))
                {
                    *gport = p_aps_bridge_level3->protection_gport;
                }
                else
                {
                    *gport = p_aps_bridge_level3->working_gport;
                }
            }
            else
            {
                *gport = p_aps_bridge_level2->protection_gport;
            }
        }
        else
        {
             /* if (p_aps_bridge_level2->next_w_aps_en)*/
            if(CTC_FLAG_ISSET(p_aps_bridge_level2->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
            {
                p_aps_bridge_level3 = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, p_aps_bridge_level2->next_w_aps);
                if (NULL == p_aps_bridge_level3)
                {
                    *gport = CTC_MAX_UINT16_VALUE;
                    CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
                }

                if (CTC_FLAG_ISSET(p_aps_bridge_level3->flag,SYS_APS_FLAG_PROTECT_EN))
                {
                    *gport = p_aps_bridge_level3->protection_gport;
                }
                else
                {
                    *gport = p_aps_bridge_level3->working_gport;
                }
            }
            else
            {
                *gport = p_aps_bridge_level2->working_gport;
            }
        }
    }
    else
    {
        if(CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
        {
            ds_aps_bridge_t ds_aps_bridge;
            uint32 cmd = 0;
            sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
            cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);
            if(ds_aps_bridge.protecting_en)
            {
                CTC_SET_FLAG(p_aps_bridge_level1->flag,SYS_APS_FLAG_PROTECT_EN);
            }
            else
            {
                CTC_UNSET_FLAG(p_aps_bridge_level1->flag,SYS_APS_FLAG_PROTECT_EN);
            }
        }
        *gport = (CTC_FLAG_ISSET(p_aps_bridge_level1->flag,SYS_APS_FLAG_PROTECT_EN)) ? (p_aps_bridge_level1->protection_gport) : (p_aps_bridge_level1->working_gport);
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, gport=0x%04x\n", aps_bridge_group_id, *gport);

    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_set_aps_bridge_protection_path(uint8 lchip, uint16 aps_bridge_group_id, uint16 gport)
{
    int32 ret = 0;
    uint32 cmd = 0;
    uint8 gchip = 0;
    uint16 dest_id = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    ds_aps_bridge_t ds_aps_bridge;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, gport=0x%04x\n", aps_bridge_group_id, gport);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    p_aps_bridge->protection_gport = gport;

    /*update asic table*/
    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
    cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

    if(!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))
    {
        gchip   = CTC_MAP_GPORT_TO_GCHIP(gport);
        dest_id = CTC_MAP_GPORT_TO_LPORT(gport);
        ds_aps_bridge.protecting_dest_map = SYS_BUILD_DESTMAP(0, gchip, dest_id);
        ds_aps_bridge.protecting_next_aps_bridge_en = 0;

        /* mcast and linkagg property */
        if (CTC_IS_LINKAGG_PORT(gport))
        {
            ds_aps_bridge.protecting_is_link_aggregation = 1;
        }

        ds_aps_bridge.protecting_ucast_id = dest_id;
    }
    else
    {
        ret = CTC_E_APS_GROUP_NEXT_APS_EXIST;
        goto out;
    }

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_P_APS_EN))
    {
        if (!CTC_IS_LINKAGG_PORT(gport)
            &&CTC_MAP_GPORT_TO_GCHIP(gport) != gchip)
        {
            ds_aps_bridge.protecting_remote_chip = 1;
        }
        else
        {
            ds_aps_bridge.protecting_remote_chip = 0;
        }
    }

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);

out:
    APS_UNLOCK(lchip);
    return ret;

}

int32
sys_greatbelt_aps_get_aps_bridge_protection_path(uint8 lchip, uint16 aps_bridge_group_id, uint32* gport)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(gport);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *gport = CTC_MAX_UINT16_VALUE;  /*invalid value*/
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        APS_UNLOCK(lchip);
        return CTC_E_APS_GROUP_NEXT_APS_EXIST;
    }

    *gport = p_aps_bridge->protection_gport;
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, gport=0x%04x\n", aps_bridge_group_id, *gport);
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_set_aps_bridge(uint8 lchip, uint16 aps_bridge_group_id, bool protect_en)
{
    int32 ret = 0;
    uint32 cmd_u = 0;
    uint32 field_val = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, protect_en=%d\n", aps_bridge_group_id, protect_en);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

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

    /*update asic table*/
    cmd_u = DRV_IOW(DsApsBridge_t, DsApsBridge_ProtectingEn_f);

    field_val = (TRUE == protect_en) ? 1 : 0;

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd_u, &field_val);

out:
    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_greatbelt_aps_get_aps_bridge(uint8 lchip, uint16 aps_bridge_group_id, bool* protect_en)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(protect_en);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *protect_en = FALSE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
    {
        ds_aps_bridge_t ds_aps_bridge;
        uint32 cmd = 0;
        sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
        cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);
        *protect_en = ds_aps_bridge.protecting_en;
    }
    else
    {
        *protect_en = CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN)?1:0;
    }
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, protect_en=%d\n", aps_bridge_group_id, *protect_en);
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_get_aps_raps_en(uint8 lchip, uint16 aps_bridge_group_id, bool* raps_en)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_PTR_VALID_CHECK(raps_en);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *raps_en = FALSE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
    {
        *raps_en = TRUE;
    }
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_set_aps_select(uint8 lchip, uint16 aps_select_group_id, bool protect_en)
{
    int32 ret       = 0;
    uint32 index    = 0;
    uint32 cmd      = 0;
    uint32 value    = 0;
    uint32 field_id = DsApsSelect_ProtectingEn0_f;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_select_group_id=%d, protect_en=%d\n", aps_select_group_id, protect_en);

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(aps_select_group_id);

    index       = aps_select_group_id / BITS_NUM_OF_WORD;
    field_id    = aps_select_group_id % BITS_NUM_OF_WORD;
    cmd         = DRV_IOW(DsApsSelect_t, field_id);

    value = (TRUE == protect_en) ? 1 : 0;

    ret = ret ? ret : DRV_IOCTL(lchip, index, cmd, &value);

    return ret;
}

int32
sys_greatbelt_aps_get_aps_select(uint8 lchip, uint16 aps_select_group_id, bool* protect_en)
{
    int32 ret       = 0;
    uint32 cmd      = 0;
    uint32 value    = 0;
    uint32 index    = 0;
    uint32 field_id = DsApsSelect_ProtectingEn0_f;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(protect_en);
    CTC_APS_GROUP_ID_CHECK(aps_select_group_id);

    index       = aps_select_group_id / BITS_NUM_OF_WORD;
    field_id    = aps_select_group_id % BITS_NUM_OF_WORD;
    cmd         = DRV_IOR(DsApsSelect_t, field_id);

    ret = ret ? ret : DRV_IOCTL(lchip, index, cmd, &value);

    *protect_en = (1 == value) ? TRUE : FALSE;

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_select_group_id=%d, protect_en=%d\n", aps_select_group_id, *protect_en);

    return ret;
}

int32
sys_greatbelt_aps_update_l2edit(uint8 lchip, uint16 aps_bridge_group_id, uint32 ds_l2edit_ptr, bool is_protection)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;
    uint8 ret = CTC_E_NONE;
    uint32 cmd      = 0;
    ds_aps_bridge_t ds_aps_bridge;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, ds_l2edit_ptr=%d, is_protection = %d\n", aps_bridge_group_id, ds_l2edit_ptr, is_protection);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

    if (TRUE == is_protection)
    {
        ds_aps_bridge.protecting_l2_edit_ptr = (uint32)ds_l2edit_ptr;
    }
    else
    {
        ds_aps_bridge.working_l2_edit_ptr = (uint32)ds_l2edit_ptr;
    }
    /*for lsp aps group use same Dsnexthop*/
    ds_aps_bridge.replication_ctl_en      = 0;

    cmd = DRV_IOW(DsApsBridge_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge);

    APS_UNLOCK(lchip);

    return ret;
}

int32
sys_greatbelt_aps_get_ports(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;
    uint8 ret = CTC_E_NONE;
    uint32 cmd      = 0;
    ds_aps_bridge_t ds_aps_bridge;
    uint16 tmp_aps_group_id = 0;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    sal_memset(aps_group, 0, sizeof(ctc_aps_bridge_group_t));

    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);
    if (NULL == p_aps_bridge)
    {
        return CTC_E_APS_GROUP_NOT_EXIST;
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);

    /*get working path's port & l3if*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge));
    tmp_aps_group_id = aps_bridge_group_id;
    if (ds_aps_bridge.working_next_aps_bridge_en)
    {
        tmp_aps_group_id = ds_aps_bridge.working_dest_map & 0x3FF;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tmp_aps_group_id, cmd, &ds_aps_bridge));

        if (ds_aps_bridge.working_next_aps_bridge_en)
        {
            tmp_aps_group_id = ds_aps_bridge.working_dest_map & 0x3FF;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tmp_aps_group_id, cmd, &ds_aps_bridge));
        }
    }

    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, tmp_aps_group_id);
    if (NULL != p_aps_bridge)
    {
        aps_group->working_gport = p_aps_bridge->working_gport;
        aps_group->w_l3if_id = p_aps_bridge->w_l3if_id;
    }

    /*get protection path's port & l3if*/
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge));
    tmp_aps_group_id = aps_bridge_group_id;
    if (ds_aps_bridge.protecting_next_aps_bridge_en)
    {
        tmp_aps_group_id = ds_aps_bridge.protecting_dest_map & 0x3FF;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_dest_map & 0x3FF, cmd, &ds_aps_bridge));

        if (ds_aps_bridge.protecting_next_aps_bridge_en)
        {
            tmp_aps_group_id = ds_aps_bridge.protecting_dest_map & 0x3FF;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_aps_bridge.protecting_dest_map & 0x3FF, cmd, &ds_aps_bridge));
        }
    }

    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, tmp_aps_group_id);
    if (NULL != p_aps_bridge)
    {
        aps_group->protection_gport = p_aps_bridge->protection_gport;
        aps_group->p_l3if_id = p_aps_bridge->p_l3if_id;

    }

    return ret;
}

int32
sys_greatbelt_aps_get_bridge(uint8 lchip, uint16 aps_bridge_group_id,  sys_aps_bridge_t* p_sys_aps_bridge, void* ds_aps_bridge)
{
    uint8 ret = CTC_E_NONE;
    uint32 cmd      = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_sys_aps_bridge);
    CTC_PTR_VALID_CHECK(ds_aps_bridge);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d\n", aps_bridge_group_id);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    sal_memcpy(p_sys_aps_bridge, p_aps_bridge, sizeof(sys_aps_bridge_t));

    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

    APS_UNLOCK(lchip);

    return ret;
}

int32
sys_greatbelt_aps_set_share_nh(uint8 lchip, uint16 aps_bridge_group_id, bool share_nh)
{
    int32 ret = 0;
    uint32 cmd_u = 0, cmd_k = 0;
    uint32 field_val = 0,field_valk=0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d,share_nh=%d", aps_bridge_group_id, share_nh);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    if(share_nh)
    {
        CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_SHARE_NEXTHOP);
    }
    else
    {
        CTC_UNSET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_SHARE_NEXTHOP);
    }

    /*update asic table*/
    cmd_u = DRV_IOW(DsApsBridge_t, DsApsBridge_ProtectingNextHopPtr_f);
    cmd_k = DRV_IOW(DsApsBridge_t, DsApsBridge_ReplicationCtlEn_f);


    field_val = (TRUE == share_nh) ? 1 : 0;
    field_valk = (TRUE == share_nh) ? 0 : 1;

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd_u, &field_val);
    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd_k, &field_valk);

out:
    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_greatbelt_aps_set_l2_shift(uint8 lchip, uint16 aps_bridge_group_id, bool shift_l2)
{
    int32 ret = 0;
    uint32 cmd_u = 0;
    uint32 field_val = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d,shift_l2=%d\n", aps_bridge_group_id, shift_l2);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        ret = CTC_E_APS_GROUP_NOT_EXIST;
        goto out;
    }

    /*update asic table*/
    cmd_u = DRV_IOW(DsApsBridge_t, DsApsBridge_L2EditPtrShiftEn_f);

    field_val = (TRUE == shift_l2) ? 1 : 0;

    ret = ret ? ret : DRV_IOCTL(lchip, aps_bridge_group_id, cmd_u, &field_val);

out:
    APS_UNLOCK(lchip);
    return ret;
}

int32
sys_greatbelt_aps_get_next_aps_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint16* next_aps_id)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(next_aps_id);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *next_aps_id = CTC_MAX_UINT16_VALUE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    *next_aps_id = p_aps_bridge->next_w_aps;
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d, next_aps_id=%d\n", aps_bridge_group_id, *next_aps_id);

    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_get_next_aps_protecting_path(uint8 lchip, uint16 aps_bridge_group_id, uint16* next_aps_id)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(next_aps_id);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *next_aps_id = CTC_MAX_UINT16_VALUE;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    *next_aps_id = p_aps_bridge->next_p_aps;
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d,next_aps_id=%d\n", aps_bridge_group_id, *next_aps_id);
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_get_aps_physical_isolated(uint8 lchip, uint16 aps_bridge_group_id, uint8* physical_isolated)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    if (NULL == p_gb_aps_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(physical_isolated);
    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        *physical_isolated = 0;
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    *physical_isolated = CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "aps_bridge_group_id=%d,physical_isolated=%d\n", aps_bridge_group_id, *physical_isolated);
    APS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_dump_aps(uint8 lchip, uint16 aps_bridge_group_id)
{
    uint32 cmd      = 0;
    sys_aps_bridge_t* p_aps_bridge = NULL;
    ds_aps_bridge_t ds_aps_bridge;

    SYS_APS_INIT_CHECK(lchip);

    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-20s : %d\n", "aps_bridge_group_id",aps_bridge_group_id);

    CTC_APS_GROUP_ID_CHECK(aps_bridge_group_id);

    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, aps_bridge_group_id);

    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge_t));
    cmd  = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, aps_bridge_group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);
    APS_UNLOCK(lchip);

    if (ds_aps_bridge.working_next_aps_bridge_en)
    {
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Next working aps group id",(ds_aps_bridge.working_dest_map) & 0x3FFFFF);
    }
    else
    {
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Working dest map",ds_aps_bridge.working_dest_map & 0x3FFFFF);
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Working l2 edit ptr",ds_aps_bridge.working_l2_edit_ptr & 0xFFFF);
    }

    if (ds_aps_bridge.protecting_next_aps_bridge_en)
    {
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Next protecting aps group id",ds_aps_bridge.protecting_dest_map & 0x3FFFFF);
    }
    else
    {
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Protecting dest map", ds_aps_bridge.protecting_dest_map & 0x3FFFFF);
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%-28s: 0x%06x \n","Protecting l2 edit ptr", ds_aps_bridge.protecting_l2_edit_ptr & 0xFFFF);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_aps_show_bridge(uint8 lchip, uint16 group_id)
{
    sys_aps_bridge_t* p_aps_bridge = NULL;

    SYS_APS_INIT_CHECK(lchip);
    CTC_APS_GROUP_ID_CHECK(group_id);
    APS_LOCK(lchip);
    p_aps_bridge = ctc_vector_get(p_gb_aps_master[lchip]->aps_bridge_vector, group_id);

    if (NULL == p_aps_bridge)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_APS_GROUP_NOT_EXIST, p_gb_aps_master[lchip]->p_aps_mutex);
    }

    if(!CTC_FLAG_ISSET(p_aps_bridge->flag, SYS_APS_FLAG_NEXT_W_APS_EN))
    {
        SYS_APS_DUMP("%-28s: %d\n", "W-Path",p_aps_bridge->working_gport);
    }
    else
    {
        SYS_APS_DUMP("%-28s: %d\n","W-Path (aps group)", p_aps_bridge->next_w_aps);
    }

    if (p_aps_bridge->w_l3if_id)
    {
        SYS_APS_DUMP("%-28s: %d\n","W-Path IFID",p_aps_bridge->w_l3if_id);
    }

    if(!CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_RAPS))
    {
        if(!CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_NEXT_P_APS_EN))
        {
            SYS_APS_DUMP("%-28s: %d\n","P-Path",p_aps_bridge->protection_gport);
        }
        else
        {
            SYS_APS_DUMP("%-28s: %d\n","P-Path (aps group)", p_aps_bridge->next_p_aps);
        }

        if (p_aps_bridge->p_l3if_id)
        {
            SYS_APS_DUMP("%-28s: %d\n","P-Path IFID", p_aps_bridge->p_l3if_id);
        }

        if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_SHARE_NEXTHOP))
        {
            SYS_APS_DUMP("%-28s: TRUE\n","Share nexthop for W/P");
        }

        if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED))
        {
            SYS_APS_DUMP("%-28s: TRUE\n","Use physical isolated path");
        }
    }
    else
    {
        SYS_APS_DUMP("%-28s: %d\n","Raps group id", p_aps_bridge->raps_group_id);
        SYS_APS_DUMP("%-28s: %d\n","Raps nh id", p_aps_bridge->raps_nhid);
    }
    SYS_APS_DUMP("%-28s: %s\n","Failover", CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN)?"Enable":"Disable");

    if(CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_HW_BASED_FAILOVER_EN))
    {
        ds_aps_bridge_t ds_aps_bridge;
        uint32 cmd = 0;
        sal_memset(&ds_aps_bridge, 0, sizeof(ds_aps_bridge));
        cmd = DRV_IOR(DsApsBridge_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, group_id, cmd, &ds_aps_bridge), p_gb_aps_master[lchip]->p_aps_mutex);

        if(ds_aps_bridge.protecting_en)
        {
            CTC_SET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN);
        }
    }

    if (CTC_FLAG_ISSET(p_aps_bridge->flag,SYS_APS_FLAG_PROTECT_EN))
    {
        SYS_APS_DUMP("%-28s: P\n","W/P");
    }
    else
    {
        SYS_APS_DUMP("%-28s: W\n","W/P");
    }

    APS_UNLOCK(lchip);

    sys_greatbelt_nh_dump_aps(lchip, group_id);

    return CTC_E_NONE;
}


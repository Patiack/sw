/**
 @file ctc_goldengate_scl.c

 @date 2013-02-21

 @version v2.0

 The file contains scl APIs
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_linklist.h"
#include "ctc_goldengate_scl.h"

#include "sys_goldengate_scl.h"
#include "sys_goldengate_chip.h"



STATIC int32
_ctc_goldengate_scl_map_action_type(ctc_scl_action_type_t ctc_type, sys_scl_action_type_t* sys_type)
{
    switch (ctc_type)
    {
        case CTC_SCL_ACTION_INGRESS:
            *sys_type = SYS_SCL_ACTION_INGRESS;
            break;
        case CTC_SCL_ACTION_EGRESS:
            *sys_type = SYS_SCL_ACTION_EGRESS;
            break;
        case CTC_SCL_ACTION_FLOW:
            *sys_type = SYS_SCL_ACTION_FLOW;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}



STATIC int32
_ctc_goldengate_scl_map_key_type(uint8 key_size, ctc_scl_key_type_t ctc_type, sys_scl_key_type_t* sys_type)
{
    switch (ctc_type)
    {
    case CTC_SCL_KEY_TCAM_VLAN:
        *sys_type = SYS_SCL_KEY_TCAM_VLAN;
        break;
    case CTC_SCL_KEY_TCAM_MAC:
        *sys_type = SYS_SCL_KEY_TCAM_MAC;
        break;
    case CTC_SCL_KEY_TCAM_IPV4:
        *sys_type = (CTC_SCL_KEY_SIZE_DOUBLE == key_size)? SYS_SCL_KEY_TCAM_IPV4: SYS_SCL_KEY_TCAM_IPV4_SINGLE;
        break;
    case CTC_SCL_KEY_TCAM_IPV6:
        *sys_type = SYS_SCL_KEY_TCAM_IPV6;
        break;
    case CTC_SCL_KEY_HASH_PORT:
        *sys_type = SYS_SCL_KEY_HASH_PORT;
        break;
    case CTC_SCL_KEY_HASH_PORT_CVLAN:
        *sys_type = SYS_SCL_KEY_HASH_PORT_CVLAN;
        break;
    case CTC_SCL_KEY_HASH_PORT_SVLAN:
        *sys_type = SYS_SCL_KEY_HASH_PORT_SVLAN;
        break;
    case CTC_SCL_KEY_HASH_PORT_2VLAN:
        *sys_type = SYS_SCL_KEY_HASH_PORT_2VLAN;
        break;
    case CTC_SCL_KEY_HASH_PORT_CVLAN_COS:
        *sys_type = SYS_SCL_KEY_HASH_PORT_CVLAN_COS;
        break;
    case CTC_SCL_KEY_HASH_PORT_SVLAN_COS:
        *sys_type = SYS_SCL_KEY_HASH_PORT_SVLAN_COS;
        break;
    case CTC_SCL_KEY_HASH_MAC:
        *sys_type = SYS_SCL_KEY_HASH_MAC;
        break;
    case CTC_SCL_KEY_HASH_PORT_MAC:
        *sys_type = SYS_SCL_KEY_HASH_PORT_MAC;
        break;
    case CTC_SCL_KEY_HASH_IPV4:
        *sys_type = SYS_SCL_KEY_HASH_IPV4;
        break;
    case CTC_SCL_KEY_HASH_PORT_IPV4:
        *sys_type = SYS_SCL_KEY_HASH_PORT_IPV4;
        break;
    case CTC_SCL_KEY_HASH_IPV6:
        *sys_type = SYS_SCL_KEY_HASH_IPV6;
        break;
    case CTC_SCL_KEY_HASH_PORT_IPV6:
        *sys_type = SYS_SCL_KEY_HASH_PORT_IPV6;
        break;
    case CTC_SCL_KEY_HASH_L2:
        *sys_type = SYS_SCL_KEY_HASH_L2;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}




STATIC int32
_ctc_goldengate_scl_map_action(ctc_scl_action_t* ctc, sys_scl_action_t* sys)
{
    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_action_type(ctc->type, &(sys->type)));

    sal_memcpy(&sys->u.igs_action, &ctc->u.igs_action, sizeof(ctc->u.igs_action));
    sal_memcpy(&sys->u.egs_action, &ctc->u.egs_action, sizeof(ctc->u.egs_action));
    sal_memcpy(&sys->u.flow_action, &ctc->u.flow_action, sizeof(ctc->u.flow_action));

    return CTC_E_NONE;
}



STATIC int32
_ctc_goldengate_scl_map_key(ctc_scl_key_t* ctc, sys_scl_key_t* sys)
{
    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_key_type(ctc->u.tcam_ipv4_key.key_size, ctc->type, &(sys->type)));

    sal_memcpy(&sys->u.tcam_vlan_key ,  &ctc->u.tcam_vlan_key, sizeof(ctc->u.tcam_vlan_key));
    sal_memcpy(&sys->u.tcam_mac_key ,  &ctc->u.tcam_mac_key, sizeof(ctc->u.tcam_mac_key));
    sal_memcpy(&sys->u.tcam_ipv4_key ,  &ctc->u.tcam_ipv4_key, sizeof(ctc->u.tcam_ipv4_key));
    sal_memcpy(&sys->u.tcam_ipv6_key ,  &ctc->u.tcam_ipv6_key, sizeof(ctc->u.tcam_ipv6_key));
    sal_memcpy(&sys->u.hash_port_key ,  &ctc->u.hash_port_key, sizeof(ctc->u.hash_port_key));
    sal_memcpy(&sys->u.hash_port_cvlan_key ,  &ctc->u.hash_port_cvlan_key, sizeof(ctc->u.hash_port_cvlan_key));
    sal_memcpy(&sys->u.hash_port_svlan_key ,  &ctc->u.hash_port_svlan_key, sizeof(ctc->u.hash_port_svlan_key));
    sal_memcpy(&sys->u.hash_port_2vlan_key ,  &ctc->u.hash_port_2vlan_key, sizeof(ctc->u.hash_port_2vlan_key));
    sal_memcpy(&sys->u.hash_port_cvlan_cos_key ,  &ctc->u.hash_port_cvlan_cos_key, sizeof(ctc->u.hash_port_cvlan_cos_key));
    sal_memcpy(&sys->u.hash_port_svlan_cos_key ,  &ctc->u.hash_port_svlan_cos_key, sizeof(ctc->u.hash_port_svlan_cos_key));
    sal_memcpy(&sys->u.hash_mac_key ,  &ctc->u.hash_mac_key, sizeof(ctc->u.hash_mac_key));
    sal_memcpy(&sys->u.hash_port_mac_key ,  &ctc->u.hash_port_mac_key, sizeof(ctc->u.hash_port_mac_key));
    sal_memcpy(&sys->u.hash_ipv4_key ,  &ctc->u.hash_ipv4_key, sizeof(ctc->u.hash_ipv4_key));
    sal_memcpy(&sys->u.hash_port_ipv4_key ,  &ctc->u.hash_port_ipv4_key, sizeof(ctc->u.hash_port_ipv4_key));
    sal_memcpy(&sys->u.hash_ipv6_key ,  &ctc->u.hash_ipv6_key, sizeof(ctc->u.hash_ipv6_key));
    sal_memcpy(&sys->u.hash_port_ipv6_key ,  &ctc->u.hash_port_ipv6_key, sizeof(ctc->u.hash_port_ipv6_key));
    sal_memcpy(&sys->u.hash_l2_key ,  &ctc->u.hash_l2_key, sizeof(ctc->u.hash_l2_key));

    return CTC_E_NONE;
}

int32
ctc_goldengate_scl_init(uint8 lchip, void* scl_global_cfg)
{
    uint8 lchip_start               = 0;
    uint8 lchip_end                 = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_init(lchip, scl_global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_scl_deinit(uint8 lchip)
{
    uint8 lchip_start               = 0;
    uint8 lchip_end                 = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_deinit(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    uint8 all_lchip                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(group_info);

    if((CTC_SCL_GROUP_TYPE_PORT == group_info->type) && (!CTC_IS_LINKAGG_PORT(group_info->un.gport)))
    {
        SYS_MAP_GPORT_TO_LCHIP(group_info->un.gport, lchip_start);
        lchip_end = lchip_start + 1;
        all_lchip = 0;
    }
    else
    {
        all_lchip = 1;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, all_lchip)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_EXIST,
                                 ret,
                                 sys_goldengate_scl_create_group(lchip, group_id, group_info, 0));
    }

    if (all_lchip)
    {
        /*rollback if error exist*/
        CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
        {
            sys_goldengate_scl_destroy_group(lchip, group_id, 0);
        }
    }

    return ret;
}

int32
ctc_goldengate_scl_destroy_group(uint8 lchip, uint32 group_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_destroy_group(lchip, group_id, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_install_group(lchip, group_id, group_info, 0));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_scl_uninstall_group(lchip, group_id, 0);
    }

    return ret;
}

int32
ctc_goldengate_scl_uninstall_group(uint8 lchip, uint32 group_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_uninstall_group(lchip, group_id, 0));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_scl_install_group(lchip, group_id, NULL, 0);
    }

    return ret;
}

int32
ctc_goldengate_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        ret = sys_goldengate_scl_get_group_info(lchip, group_id, group_info, 0);
        if(CTC_E_NONE == ret)
        {
            break;
        }
    }

    return ret;
}

STATIC int32
_ctc_goldengate_scl_get_lchip_by_gport(uint32 gport_type,
                                       uint32 gport,
                                       uint8* lchip_id,
                                       uint8* all_lchip)
{
    if((CTC_SCL_GPROT_TYPE_PORT == gport_type) && (!CTC_IS_LINKAGG_PORT(gport)))
    {
        SYS_MAP_GPORT_TO_LCHIP(gport, *lchip_id);
        *all_lchip = 0;
    }
    else
    {
        *all_lchip = 1;
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_scl_add_entry(uint8 lchip, uint32 group_id, ctc_scl_entry_t* scl_entry)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    uint8 all_lchip                = 0;
    uint8 lchip_id                 = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;
    sys_scl_entry_t  sys;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(scl_entry);

    switch(scl_entry->key.type)
    {
    case CTC_SCL_KEY_HASH_PORT:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_key.gport_type,
                                               scl_entry->key.u.hash_port_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_CVLAN:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_cvlan_key.gport_type,
                                               scl_entry->key.u.hash_port_cvlan_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_SVLAN:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_svlan_key.gport_type,
                                               scl_entry->key.u.hash_port_svlan_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_2VLAN:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_2vlan_key.gport_type,
                                               scl_entry->key.u.hash_port_2vlan_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_CVLAN_COS:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_cvlan_cos_key.gport_type,
                                               scl_entry->key.u.hash_port_cvlan_cos_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_SVLAN_COS:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_svlan_cos_key.gport_type,
                                               scl_entry->key.u.hash_port_svlan_cos_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_MAC:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_mac_key.gport_type,
                                               scl_entry->key.u.hash_port_mac_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_IPV4:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_ipv4_key.gport_type,
                                               scl_entry->key.u.hash_port_ipv4_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;
    case CTC_SCL_KEY_HASH_PORT_IPV6:
        _ctc_goldengate_scl_get_lchip_by_gport(scl_entry->key.u.hash_port_ipv6_key.gport_type,
                                               scl_entry->key.u.hash_port_ipv6_key.gport,
                                               &lchip_id,
                                               &all_lchip);
        break;

    default:
         all_lchip                 = 1;
    }


    sal_memset(&sys, 0 ,sizeof(sys));

    sys.entry_id    = scl_entry->entry_id;
    sys.priority        = scl_entry->priority;
    sys.priority_valid  = scl_entry->priority_valid;

    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_action(&scl_entry->action ,&sys.action));
    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_key(&scl_entry->key ,&sys.key));

    CTC_SET_API_LCHIP(lchip, lchip_id);

    lchip_start         = lchip;
    lchip_end           = lchip + 1;
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, all_lchip)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_add_entry(lchip, group_id, &sys, 0));
    }

    if (all_lchip)
    {
        /*rollback if error exist*/
        CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
        {
            sys_goldengate_scl_remove_entry(lchip, scl_entry->entry_id, 0);
        }
    }

    return ret;
}

int32
ctc_goldengate_scl_remove_entry(uint8 lchip, uint32 entry_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count                    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN2(CTC_E_SCL_GROUP_UNEXIST, CTC_E_ENTRY_NOT_EXIST,
                                  ret,
                                  sys_goldengate_scl_remove_entry(lchip, entry_id, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_install_entry(uint8 lchip, uint32 entry_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count                    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN2(CTC_E_SCL_GROUP_UNEXIST, CTC_E_ENTRY_NOT_EXIST,
                                  ret,
                                  sys_goldengate_scl_install_entry(lchip, entry_id, 0));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_scl_uninstall_entry(lchip, entry_id, 0);
    }

    return ret;
}

int32
ctc_goldengate_scl_uninstall_entry(uint8 lchip, uint32 entry_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count                    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN2(CTC_E_SCL_GROUP_UNEXIST, CTC_E_ENTRY_NOT_EXIST,
                                  ret,
                                  sys_goldengate_scl_uninstall_entry(lchip, entry_id, 0));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start, lchip_end)
    {
        sys_goldengate_scl_install_entry(lchip, entry_id, 0);
    }

    return ret;
}

int32
ctc_goldengate_scl_remove_all_entry(uint8 lchip, uint32 group_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_remove_all_entry(lchip, group_id, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count                    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN2(CTC_E_SCL_GROUP_UNEXIST, CTC_E_ENTRY_NOT_EXIST,
                                  ret,
                                  sys_goldengate_scl_set_entry_priority(lchip, entry_id, priority, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        ret = sys_goldengate_scl_get_multi_entry(lchip, query, 0);
        if(CTC_E_NONE == ret)
        {
            break;
        }
    }

    return ret;
}

int32
ctc_goldengate_scl_update_action(uint8 lchip, uint32 entry_id, ctc_scl_action_t* action)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count = 0;
    sys_scl_action_t  sys;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(action);

    sal_memset(&sys, 0 ,sizeof(sys));

    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_action_type(action->type ,&sys.type));
    sal_memcpy(&sys.u.igs_action,  &action->u.igs_action, sizeof(action->u.igs_action));

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ACL_SCL_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_goldengate_scl_update_action(lchip, entry_id, &sys, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_set_default_action(uint8 lchip, ctc_scl_default_action_t* def_action)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    sys_scl_action_t  sys;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(def_action);

    sal_memset(&sys, 0 ,sizeof(sys));

    CTC_ERROR_RETURN(_ctc_goldengate_scl_map_action_type(def_action->action.type ,&sys.type));
    sal_memcpy(&sys.u.igs_action,  &def_action->action.u.igs_action, sizeof(def_action->action.u.igs_action));

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        /*in function: sys_goldengate_scl_set_default_action
          if chip is not local, it returns CTC_E_NONE*/
        CTC_ERROR_RETURN(sys_goldengate_scl_set_default_action(lchip, def_action->gport, &sys));
    }

    return CTC_E_NONE;
}


int32
ctc_goldengate_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 count                    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN2(CTC_E_ENTRY_NOT_EXIST, CTC_E_SCL_GROUP_UNEXIST,
                                 ret,
                                 sys_goldengate_scl_copy_entry(lchip, copy_entry, 0));
    }

    return ret;
}

int32
ctc_goldengate_scl_set_hash_field_sel(uint8 lchip, ctc_scl_hash_field_sel_t* field_sel)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_SCL);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_set_hash_field_sel(lchip, field_sel));
    }

    return CTC_E_NONE;
}




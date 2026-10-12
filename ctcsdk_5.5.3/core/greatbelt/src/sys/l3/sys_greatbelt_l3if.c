/**
@file sys_greatbelt_l3if.c

@date 2009 - 12 - 7

@version v2.0

---file comments----
*/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_const.h"

#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_rpf_spool.h"
#include "sys_greatbelt_stats.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_tbl_reg.h"

/**********************************************************************************
Defines and Macros
***********************************************************************************/
#define SYS_ROUTER_MAC_LABEL_INDEX       255
#define MAX_L3IF_RELATED_PROPERTY        10

#define SYS_L3IF_LOCK(lchip) \
    sal_mutex_lock(p_gb_l3if_master[lchip]->p_mutex)

#define SYS_L3IF_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_l3if_master[lchip]->p_mutex)

#define CTC_ERROR_RETURN_L3IF_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_l3if_master[lchip]->p_mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_L3IF_INIT_CHECK()                   \
    do {                                        \
        SYS_LCHIP_CHECK_ACTIVE(lchip);          \
        if (NULL == p_gb_l3if_master[lchip]){   \
            return CTC_E_NOT_INIT; }            \
    } while (0)

#define SYS_L3IF_CREATE_CHECK(l3if_id)                              \
    if (FALSE == p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vaild) \
    {                                                               \
        return CTC_E_L3IF_NOT_EXIST;                                \
    }

#define SYS_L3IFID_VAILD_CHECK(l3if_id)         \
    if (l3if_id == 0 || l3if_id > SYS_GB_MAX_CTC_L3IF_ID) \
    {                                               \
        return CTC_E_L3IF_INVALID_IF_ID;            \
    }

struct sys_l3if_vmac_entry_s
{
    uint8 vaild;
    uint8 rsv0;
    uint8 router_mac1_type;
    uint8 router_mac0_type;
    uint8 router_mac2_type;
    uint8 router_mac3_type;
}
;
typedef struct sys_l3if_vmac_entry_s sys_l3if_vmac_entry_t;

struct sys_l3if_master_s
{
    sys_l3if_prop_t  l3if_prop[SYS_GB_MAX_CTC_L3IF_ID + 1];
    sys_l3if_vmac_entry_t  vmac_entry[SYS_ROUTER_MAC_LABEL_INDEX + 1];
    mac_addr_t       router_mac;   /*per system*/
    sal_mutex_t* p_mutex;
    uint16 max_vrfid_num;
    uint8  ipv4_vrf_en;
    uint8  ipv6_vrf_en;
    uint8*  vrfid_stats_en;
}
;
typedef struct sys_l3if_master_s sys_l3if_master_t;

/****************************************************************************
Global and Declaration
*****************************************************************************/

sys_l3if_master_t* p_gb_l3if_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/**********************************************************************************
Define API function interfaces
***********************************************************************************/

/**
@brief    init l3 interface module
*/
int32
sys_greatbelt_l3if_init(uint8 lchip, ctc_l3if_global_cfg_t* l3if_global_cfg)
{
    int32 ret = CTC_E_NONE;
    uint32 index = 0;
    uint32 write_cmd = 0;
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    ds_src_interface_t src_interface;
    ipe_router_mac_ctl_t       rt_mac_ctl;
    ds_router_mac_t             rt_mac;
    ds_dest_interface_t          dest_interface;
    epe_l2_router_mac_sa_t  l2rt_macsa;

    sal_memset(&src_interface, 0, sizeof(src_interface));
    sal_memset(&dest_interface, 0, sizeof(dest_interface));
    sal_memset(&rt_mac, 0, sizeof(rt_mac));

    if (p_gb_l3if_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    p_gb_l3if_master[lchip] = (sys_l3if_master_t*)mem_malloc(MEM_L3IF_MODULE, sizeof(sys_l3if_master_t));

    if (NULL == p_gb_l3if_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_l3if_master[lchip], 0, sizeof(sys_l3if_master_t));

    ret = sal_mutex_create(&(p_gb_l3if_master[lchip]->p_mutex));
    if (ret || !(p_gb_l3if_master[lchip]->p_mutex))
    {
        mem_free(p_gb_l3if_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gb_l3if_master[lchip]->max_vrfid_num = l3if_global_cfg->max_vrfid_num;
    MALLOC_ZERO(MEM_L3IF_MODULE, p_gb_l3if_master[lchip]->vrfid_stats_en,
        l3if_global_cfg->max_vrfid_num * sizeof(uint8));
    if (NULL == p_gb_l3if_master[lchip]->vrfid_stats_en)
    {
        return CTC_E_NO_MEMORY;
    }

    p_gb_l3if_master[lchip]->ipv4_vrf_en  = l3if_global_cfg->ipv4_vrf_en;
    p_gb_l3if_master[lchip]->ipv6_vrf_en  = l3if_global_cfg->ipv6_vrf_en;
    rt_mac.router_mac0_type     = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
    rt_mac.router_mac1_type     = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
    rt_mac.router_mac2_type     = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
    rt_mac.router_mac3_type     = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;

    src_interface.router_mac_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC; /*multiple router mac*/
    src_interface.router_mac_label = 0;
    dest_interface.mac_sa_type     = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;
    dest_interface.mac_sa          = 0;

    write_cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);

    for (index = 0; index <= SYS_ROUTER_MAC_LABEL_INDEX; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, write_cmd, &rt_mac));
    }

    for (index = 0; index <= SYS_GB_MAX_CTC_L3IF_ID; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, src_if_cmd, &src_interface));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, dest_if_cmd, &dest_interface));
    }

    sal_memset(&l2rt_macsa, 0, sizeof(epe_l2_router_mac_sa_t));
    write_cmd = DRV_IOW(EpeL2RouterMacSa_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, write_cmd, &l2rt_macsa));

    sal_memset(&rt_mac_ctl, 0, sizeof(ipe_router_mac_ctl_t));
    write_cmd = DRV_IOW(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, write_cmd, &rt_mac_ctl));

    for (index = 0; index <= SYS_GB_MAX_CTC_L3IF_ID; index++)
    {
        p_gb_l3if_master[lchip]->l3if_prop[index].router_mac_label = SYS_ROUTER_MAC_LABEL_INDEX;
        p_gb_l3if_master[lchip]->l3if_prop[index].vaild = FALSE;
        p_gb_l3if_master[lchip]->l3if_prop[index].rmac_prefix  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;
        p_gb_l3if_master[lchip]->l3if_prop[index].vlan_ptr = 0;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_l3if_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gb_l3if_master[lchip]->vrfid_stats_en);
    sal_mutex_destroy(p_gb_l3if_master[lchip]->p_mutex);
    mem_free(p_gb_l3if_master[lchip]);

    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_l3if_init_vlan_property(uint8 lchip, uint16 l3if_id, uint16 vlan_id, uint8 enable)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsDestInterface_t, DsDestInterface_InterfaceSvlanTagged_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &field_val));

    field_val = enable ? vlan_id : 0;
    cmd = DRV_IOW(DsDestInterface_t, DsDestInterface_VlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &field_val));

    return CTC_E_NONE;

}

/**
@brief  Create l3 interfaces, gport should be global logical port
*/
int32
sys_greatbelt_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    uint16  vlan_ptr = 0;
    int32 ret = 0;
    ds_src_interface_t src_interface;
    ds_dest_interface_t dest_interface;
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    uint16 index;
    uint8 gchip = 0;
    uint16 subif_use_scl = 0;

    SYS_L3IF_INIT_CHECK();
    CTC_PTR_VALID_CHECK(l3_if);
    SYS_L3IFID_VAILD_CHECK(l3if_id);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_type:%d ,l3if_id :%d port:0x%x, vlan_id:%d \n",
                      l3_if->l3if_type, l3if_id, l3_if->gport, l3_if->vlan_id);

    gchip = SYS_MAP_GPORT_TO_GCHIP(l3_if->gport);

    SYS_L3IF_LOCK(lchip);
    if (TRUE == p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vaild)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_EXIST;
    }

    for (index = 0; index <= SYS_GB_MAX_CTC_L3IF_ID; index++)
    {
        if (CTC_L3IF_TYPE_SERVICE_IF == l3_if->l3if_type)
        {
            break;
        }
        if ((p_gb_l3if_master[lchip]->l3if_prop[index].vaild) &&
            (p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == l3_if->l3if_type) &&
            (p_gb_l3if_master[lchip]->l3if_prop[index].gport == l3_if->gport) &&
            (p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == l3_if->vlan_id))
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_L3IF_EXIST;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    switch (l3_if->l3if_type)
    {
    case CTC_L3IF_TYPE_PHY_IF:
        {
            CTC_ERROR_RETURN( sys_greatbelt_port_dest_gport_check(lchip, l3_if->gport));
            vlan_ptr  = 0x1000 + l3if_id;
            break;
        }

    case CTC_L3IF_TYPE_SUB_IF:
        {
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            CTC_ERROR_RETURN( sys_greatbelt_port_dest_gport_check(lchip, l3_if->gport));
            vlan_ptr  = 0x1000 + l3if_id;

            if(CTC_IS_LINKAGG_PORT(l3_if->gport) || sys_greatbelt_chip_is_local(lchip, gchip))
            {
                subif_use_scl = 1;
            }
            break;
        }

    case CTC_L3IF_TYPE_VLAN_IF:
        {
            uint32 tmp_l3if_id = 0;
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            ret = sys_greatbelt_vlan_get_internal_property(lchip, l3_if->vlan_id, SYS_VLAN_PROP_INTERFACE_ID, &tmp_l3if_id);
            if (ret == CTC_E_NONE
                && (tmp_l3if_id != SYS_L3IF_INVALID_L3IF_ID)
                && (tmp_l3if_id != l3if_id))
            {
                ret = CTC_E_L3IF_EXIST;
            }
            else
            {
                ret  = sys_greatbelt_vlan_set_internal_property(lchip, l3_if->vlan_id, SYS_VLAN_PROP_INTERFACE_ID, l3if_id);
            }

            sys_greatbelt_vlan_get_vlan_ptr(lchip, l3_if->vlan_id, &vlan_ptr);
            break;
        }
    case CTC_L3IF_TYPE_SERVICE_IF:
        {
            vlan_ptr  = 0x1000 + l3if_id;
            break;
        }

    default:
        ret = CTC_E_INVALID_PARAM;
    }

    if (ret != 0)
    {
        CTC_ERROR_RETURN(ret);
    }

    SYS_L3IF_LOCK(lchip);
    /*only save the last port property*/
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].gport = l3_if->gport;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id = l3_if->vlan_id;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type = l3_if->l3if_type;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vaild = TRUE;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr = vlan_ptr;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].l3if_id  = l3if_id;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].rmac_prefix = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;

    sal_memset(&src_interface, 0, sizeof(src_interface));
    sal_memset(&dest_interface, 0, sizeof(dest_interface));

    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);

    src_interface.router_mac_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC; /*multiple router mac*/
    src_interface.router_mac_label = p_gb_l3if_master[lchip]->router_mac[5];
    dest_interface.mac_sa_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;
    dest_interface.mac_sa  = p_gb_l3if_master[lchip]->router_mac[5];
    SYS_L3IF_UNLOCK(lchip);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, src_if_cmd, &src_interface));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, dest_if_cmd, &dest_interface));

     if (CTC_L3IF_TYPE_VLAN_IF == l3_if->l3if_type ||
        CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type )
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l3if_init_vlan_property(lchip, l3if_id, l3_if->vlan_id, 1));
    }

    if ((CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type) && subif_use_scl)
    {
        sys_scl_entry_t   scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        /* KEY */
        scl_entry.key.u.hash_port_svlan_key.gport = l3_if->gport;
        scl_entry.key.u.hash_port_svlan_key.svlan = l3_if->vlan_id;
        scl_entry.key.u.hash_port_svlan_key.dir = CTC_INGRESS;
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_SVLAN;


        /* AD */
        scl_entry.action.type = SYS_SCL_ACTION_INGRESS;
        scl_entry.action.u.igs_action.user_vlanptr = vlan_ptr;
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, &scl_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl_entry.entry_id, 1));
    }

    /* set l3if default config */
    ret = ret ? ret : sys_greatbelt_l3if_set_property(lchip, l3if_id, CTC_L3IF_PROP_ROUTE_EN, TRUE);
    ret = ret ? ret : sys_greatbelt_l3if_set_property(lchip, l3if_id, CTC_L3IF_PROP_IPV4_UCAST, TRUE);
    ret = ret ? ret : sys_greatbelt_l3if_set_property(lchip, l3if_id, CTC_L3IF_PROP_VRF_ID, 0);
    ret = ret ? ret : sys_greatbelt_l3if_set_property(lchip, l3if_id, CTC_L3IF_PROP_VRF_EN, FALSE);
    if (ret != 0)
    {
        sys_greatbelt_l3if_delete(lchip, l3if_id, l3_if);
        CTC_ERROR_RETURN(ret);
    }

    return CTC_E_NONE;
}

/**
@brief    Delete  l3 interfaces
*/
int32
sys_greatbelt_l3if_delete(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    ds_src_interface_t src_interface;
    ds_dest_interface_t dest_interface;
    uint32 src_if_cmd = 0;
    uint32 dest_if_cmd = 0;
    uint8  route_mac_label = 0xFF;
    uint8 is_local_port = 0;
    uint8 gchip = 0;
    uint8 subif_use_scl = 0;

    SYS_L3IF_INIT_CHECK();
    CTC_PTR_VALID_CHECK(l3_if);
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    gchip = SYS_MAP_GPORT_TO_GCHIP(l3_if->gport);
    is_local_port = sys_greatbelt_chip_is_local(lchip, gchip);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_type:%d ,l3if_id :%d port:0x%x, vlan_id:%d \n",
                      l3_if->l3if_type, l3if_id, l3_if->gport, l3_if->vlan_id);

    sal_memset(&src_interface, 0, sizeof(src_interface));
    sal_memset(&dest_interface, 0, sizeof(dest_interface));

    switch (l3_if->l3if_type)
    {
    case CTC_L3IF_TYPE_PHY_IF:
        {
            SYS_L3IF_LOCK(lchip);
            if (p_gb_l3if_master[lchip]->l3if_prop[l3if_id].gport != l3_if->gport)
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }

    case CTC_L3IF_TYPE_SUB_IF:
        {
            CTC_GLOBAL_PORT_CHECK(l3_if->gport);
            CTC_VLAN_RANGE_CHECK(l3_if->vlan_id);
            if(CTC_IS_LINKAGG_PORT(l3_if->gport) || is_local_port)
            {
                subif_use_scl = 1;
            }

            SYS_L3IF_LOCK(lchip);
            if (p_gb_l3if_master[lchip]->l3if_prop[l3if_id].gport != l3_if->gport ||
                 p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id != l3_if->vlan_id)
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }

    case CTC_L3IF_TYPE_VLAN_IF:
        {
            SYS_L3IF_LOCK(lchip);
            if (p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id != l3_if->vlan_id)
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);

            sys_greatbelt_vlan_set_internal_property(lchip, l3_if->vlan_id, SYS_VLAN_PROP_INTERFACE_ID, 0);
            break;
        }
    case CTC_L3IF_TYPE_SERVICE_IF:
        {
            SYS_L3IF_LOCK(lchip);
            if(p_gb_l3if_master[lchip]->l3if_prop[l3if_id].l3if_type != l3_if->l3if_type)
            {
                SYS_L3IF_UNLOCK(lchip);
                return CTC_E_L3IF_MISMATCH;
            }
            SYS_L3IF_UNLOCK(lchip);
            break;
        }

    default:
        return CTC_E_INVALID_PARAM;
    }

    src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);
    src_interface.router_mac_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC; /*multiple router mac*/
    src_interface.router_mac_label = 0;
    dest_interface.mac_sa_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;
    dest_interface.mac_sa  = 0;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, src_if_cmd, &src_interface));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, dest_if_cmd, &dest_interface));

    SYS_L3IF_LOCK(lchip);
    route_mac_label = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label = SYS_ROUTER_MAC_LABEL_INDEX;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vaild = FALSE;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr = 0;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].l3if_id  = 0;
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].rmac_prefix = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC;
    SYS_L3IF_UNLOCK(lchip);

    if (CTC_L3IF_TYPE_VLAN_IF == l3_if->l3if_type ||
        CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type )
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l3if_init_vlan_property(lchip, l3if_id, l3_if->vlan_id, 0));
    }
    if ((CTC_L3IF_TYPE_SUB_IF == l3_if->l3if_type) && subif_use_scl)
    {
        sys_scl_entry_t   scl_entry;
        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        /* KEY */
        scl_entry.key.u.hash_port_svlan_key.gport = l3_if->gport;
        scl_entry.key.u.hash_port_svlan_key.svlan = l3_if->vlan_id;
        scl_entry.key.u.hash_port_svlan_key.dir = CTC_INGRESS;
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_SVLAN;

        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS));
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, scl_entry.entry_id, 1));
    }

    sys_greatbelt_nh_del_ipmc_dsnh_offset(lchip, l3if_id);
    if (route_mac_label != SYS_ROUTER_MAC_LABEL_INDEX)
    {
        sys_l3if_vmac_entry_t* vmac_entry = NULL;
        ds_router_mac_t rt_mac;
        uint32 cmd;
        sal_memset(&rt_mac, 0, sizeof(ds_router_mac_t));

        SYS_L3IF_LOCK(lchip);
        vmac_entry = &p_gb_l3if_master[lchip]->vmac_entry[route_mac_label];
        rt_mac.router_mac0_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac1_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac2_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac3_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac0_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac1_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac2_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac3_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->vaild  = FALSE;
        SYS_L3IF_UNLOCK(lchip);

        cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* l3_if, uint16* l3if_id)
{
    uint16 index;

    SYS_L3IF_INIT_CHECK();
    CTC_PTR_VALID_CHECK(l3_if);
    CTC_PTR_VALID_CHECK(l3if_id);

    SYS_L3IF_LOCK(lchip);
    if(l3_if->l3if_type == 0xFF &&  (*l3if_id !=0) )
    { /*get l3if info by L3if id*/
        if (*l3if_id <= SYS_GB_MAX_CTC_L3IF_ID && p_gb_l3if_master[lchip]->l3if_prop[*l3if_id].vaild)
        {
            index = *l3if_id;
            l3_if->l3if_type = p_gb_l3if_master[lchip]->l3if_prop[*l3if_id].l3if_type;
            l3_if->gport = p_gb_l3if_master[lchip]->l3if_prop[*l3if_id].gport;
            l3_if->vlan_id = p_gb_l3if_master[lchip]->l3if_prop[*l3if_id].vlan_id;
        }
        else
        {
            index = SYS_GB_MAX_CTC_L3IF_ID + 1;
        }
    }
    else
    {/*get L3if id by l3if info*/
        if (CTC_L3IF_TYPE_SERVICE_IF == l3_if->l3if_type)
        {
            SYS_L3IF_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
        {
            if ((p_gb_l3if_master[lchip]->l3if_prop[index].vaild) &&
                (p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == l3_if->l3if_type) &&
                (p_gb_l3if_master[lchip]->l3if_prop[index].gport == l3_if->gport) &&
                (p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == l3_if->vlan_id))
            {
                break;;
            }
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    *l3if_id = index;

    if ((MAX_CTC_L3IF_ID + 1) == index)
    {
        return CTC_E_L3IF_NOT_EXIST;
    }

    return CTC_E_NONE;

}


bool
sys_greatbelt_l3if_is_port_phy_if(uint8 lchip, uint16 gport)
{
    uint16 index = 0;
    bool  ret = FALSE;

    SYS_L3IF_INIT_CHECK();

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
    {
        if (p_gb_l3if_master[lchip]->l3if_prop[index].vaild == TRUE
            && p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_PHY_IF
            && p_gb_l3if_master[lchip]->l3if_prop[index].gport == gport)
        {
            ret = TRUE;
            break;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

bool
sys_greatbelt_l3if_is_port_sub_if(uint8 lchip, uint16 gport)
{
    uint16 index = 0;
    bool  ret = FALSE;

    SYS_L3IF_INIT_CHECK();

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
    {
        if (p_gb_l3if_master[lchip]->l3if_prop[index].vaild == TRUE
            && p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SUB_IF
            && p_gb_l3if_master[lchip]->l3if_prop[index].gport == gport)
        {
            ret = TRUE;
            break;
        }
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_greatbelt_l3if_get_l3if_info(uint8 lchip, uint8 query_type, sys_l3if_prop_t* l3if_prop)
{
    int32 ret = CTC_E_L3IF_NOT_EXIST;
    uint16  index = 0;
    bool find = FALSE;

    SYS_L3IF_INIT_CHECK();

    SYS_L3IF_LOCK(lchip);
    if (query_type == 1)
    {
        if (p_gb_l3if_master[lchip]->l3if_prop[l3if_prop->l3if_id].vaild == TRUE)
        {
            find = TRUE;
            index = l3if_prop->l3if_id;
        }
    }
    else
    {
        for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
        {
            if (p_gb_l3if_master[lchip]->l3if_prop[index].vaild == FALSE ||
                p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type != l3if_prop->l3if_type)
            {
                continue;
            }

            if (l3if_prop->l3if_type == CTC_L3IF_TYPE_PHY_IF)
            {
                if (p_gb_l3if_master[lchip]->l3if_prop[index].gport == l3if_prop->gport)
                {
                    find = TRUE;
                    break;
                }
            }
            else if (l3if_prop->l3if_type == CTC_L3IF_TYPE_SUB_IF)
            {
                if (p_gb_l3if_master[lchip]->l3if_prop[index].gport == l3if_prop->gport &&
                    p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == l3if_prop->vlan_id)
                {
                    find = TRUE;
                    break;
                }
            }
            else if (l3if_prop->l3if_type == CTC_L3IF_TYPE_VLAN_IF)
            {
                if (p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == l3if_prop->vlan_id)
                {
                    find = TRUE;
                    break;
                }
            }
            else
            {
                continue;
            }
        }
    }

    if (find)
    {
        sal_memcpy(l3if_prop, &p_gb_l3if_master[lchip]->l3if_prop[index], sizeof(sys_l3if_prop_t));
        ret = CTC_E_NONE;
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

int32
sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(uint8 lchip, uint16 gport, uint16 vlan_id, sys_l3if_prop_t* l3if_prop)
{
    int32 ret = CTC_E_L3IF_NOT_EXIST;
    uint16  index = 0;
    bool find = FALSE;

    SYS_L3IF_INIT_CHECK();

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
    {
        if (p_gb_l3if_master[lchip]->l3if_prop[index].vaild == FALSE)
        {
            continue;
        }
        if (p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SERVICE_IF)
        {
            continue;
        }
        if (p_gb_l3if_master[lchip]->l3if_prop[index].gport == gport
            && p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_PHY_IF)
        {
            find = TRUE;
            break;
        }
        else if (p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_SUB_IF)
        {
            if (p_gb_l3if_master[lchip]->l3if_prop[index].gport == gport &&
                p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == vlan_id)
            {
                find = TRUE;
                break;
            }
        }
    }

    if(!find)
    {
        for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
        {
            if (p_gb_l3if_master[lchip]->l3if_prop[index].vaild == FALSE)
            {
                continue;
            }

            if (p_gb_l3if_master[lchip]->l3if_prop[index].vlan_id == vlan_id
                     && p_gb_l3if_master[lchip]->l3if_prop[index].l3if_type == CTC_L3IF_TYPE_VLAN_IF)
            {
                find = TRUE;
                break;
            }
        }
    }

    if (find)
    {
        sal_memcpy(l3if_prop, &p_gb_l3if_master[lchip]->l3if_prop[index], sizeof(sys_l3if_prop_t));
        ret = CTC_E_NONE;
    }
    SYS_L3IF_UNLOCK(lchip);

    return ret;
}

/**
@brief    Config  l3 interface's  properties
*/
int32
sys_greatbelt_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value)
{
    uint32 cmd[MAX_L3IF_RELATED_PROPERTY] = {0};
    uint32 idx = 0;
    uint32 field[MAX_L3IF_RELATED_PROPERTY] = {0};
    sys_scl_entry_t   scl_entry;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id :%d, l3if_prop:%d, value:%d \n", l3if_id, l3if_prop, value);

    switch (l3if_prop)
    {
    /*src  interface */
    case CTC_L3IF_PROP_IPV4_UCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V4UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_MCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V4McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_SA_TYPE:
        if (value >= MAX_CTC_L3IF_IPSA_LKUP_TYPE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V4UcastSaType_f);
        break;

    case CTC_L3IF_PROP_IPV6_UCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V6UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_MCAST:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V6McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_SA_TYPE:
        if (value >= MAX_CTC_L3IF_IPSA_LKUP_TYPE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_V6UcastSaType_f);
        break;

    case CTC_L3IF_PROP_VRF_ID:
        if (value >= p_gb_l3if_master[lchip]->max_vrfid_num)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_VrfId_f);
        break;

    case CTC_L3IF_PROP_ROUTE_EN:
        field[0] = (value) ? 0 : 1;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RouteDisable_f);
        break;

    case CTC_L3IF_PROP_NAT_IFTYPE:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_L3IfType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_ALL_PKT:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RouteAllPackets_f);
        break;

    case CTC_L3IF_PROP_PBR_LABEL:
        if (value > MAX_CTC_L3IF_PBR_LABEL)
        {
            return CTC_E_L3IF_INVALID_PBR_LABEL;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_PbrLabel_f);
        break;

    case CTC_L3IF_PROP_VRF_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RouteLookupMode_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_PREFIX_TYPE:
        if (value >= CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RouterMacType_f);
        field[1] = value;
        cmd[1] = DRV_IOW(DsDestInterface_t, DsDestInterface_MacSaType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_LOW_8BITS:
        if (value > CTC_MAX_UINT8_VALUE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RouterMacLabel_f);
        field[1] = value;
        cmd[1] = DRV_IOW(DsDestInterface_t, DsDestInterface_MacSa_f);
        break;

    case CTC_L3IF_PROP_MTU_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_MtuCheckEn_f);
        break;

    case CTC_L3IF_PROP_MTU_EXCEPTION_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_MtuExceptionEn_f);
        break;

    case CTC_L3IF_PROP_EGS_MCAST_TTL_THRESHOLD:
        if (value >= MAX_CTC_L3IF_MCAST_TTL_THRESHOLD)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_McastTtlThreshold_f);
        break;

    case CTC_L3IF_PROP_MTU_SIZE:
        if (value >= MAX_CTC_L3IF_MTU_SIZE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_MtuSize_f);
        break;

    case CTC_L3IF_PROP_EGS_MAC_SA_PREFIX_TYPE:
        if (value >= CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_MacSaType_f);
        break;

    case CTC_L3IF_PROP_EGS_MAC_SA_LOW_8BITS:
        if (value > CTC_MAX_UINT8_VALUE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsDestInterface_t, DsDestInterface_MacSa_f);
        break;

    case CTC_L3IF_PROP_MPLS_EN:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_MplsEn_f);
        break;

    case CTC_L3IF_PROP_MPLS_LABEL_SPACE:
        if (value > CTC_MAX_UINT8_VALUE)
        {
            return CTC_E_INVALID_PARAM;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_MplsLabelSpace_f);
        break;

    case CTC_L3IF_PROP_RPF_CHECK_TYPE:
        if (!(value < SYS_RPF_CHK_TYPE_NUM))
        {
            return CTC_E_L3IF_INVALID_RPF_CHECK_TYPE;
        }

        field[0] = value;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RpfType_f);
        break;

    case CTC_L3IF_PROP_RPF_PERMIT_DEFAULT:
        field[0] = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcInterface_t, DsSrcInterface_RpfPermitDefault_f);
        break;

    case CTC_L3IF_PROP_STATS:

        sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
        SYS_L3IF_LOCK(lchip);
        /* KEY */
        scl_entry.key.u.hash_port_svlan_key.gport = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].gport;
        scl_entry.key.u.hash_port_svlan_key.svlan = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_id;
        scl_entry.key.u.hash_port_svlan_key.dir = CTC_INGRESS;
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_SVLAN;
        /* AD */
        scl_entry.action.type = SYS_SCL_ACTION_INGRESS;
        scl_entry.action.u.igs_action.user_vlanptr = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr;
        SYS_L3IF_UNLOCK(lchip);
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR);
        if (value == 0)
        {
            CTC_UNSET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_STATS);
        }
        else
        {
            scl_entry.action.u.igs_action.stats_id = value;
            CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_STATS);
        }
        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS));
        CTC_ERROR_RETURN(sys_greatbelt_scl_update_action(lchip, scl_entry.entry_id, &(scl_entry.action), 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, scl_entry.entry_id, 1));
        break;

    case CTC_L3IF_PROP_IGMP_SNOOPING_EN:
        return CTC_E_FEATURE_NOT_SUPPORT;
    default:
        return CTC_E_INVALID_PARAM;
    }

    for (idx = 0; idx < MAX_L3IF_RELATED_PROPERTY; idx++)
    {
        if (0 != cmd[idx])
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd[idx], &field[idx]));
        }
    }

    return CTC_E_NONE;
}

/**
@brief    Get  l3 interface's properties  according to interface id
*/

int32
sys_greatbelt_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* value)
{
    uint32 cmd = 0;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    switch (l3if_prop)
    {
    /*src  interface */
    case CTC_L3IF_PROP_IPV4_UCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V4UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_MCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V4McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV4_SA_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V4UcastSaType_f);
        break;

    case CTC_L3IF_PROP_IPV6_UCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V6UcastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_MCAST:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V6McastEn_f);
        break;

    case CTC_L3IF_PROP_IPV6_SA_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_V6UcastSaType_f);
        break;

    case CTC_L3IF_PROP_VRF_ID:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_VrfId_f);
        break;

    case CTC_L3IF_PROP_ROUTE_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RouteDisable_f);
        break;

    case CTC_L3IF_PROP_NAT_IFTYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_L3IfType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_ALL_PKT:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RouteAllPackets_f);
        break;

    case CTC_L3IF_PROP_PBR_LABEL:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_PbrLabel_f);
        break;

    case CTC_L3IF_PROP_VRF_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RouteLookupMode_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_PREFIX_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RouterMacType_f);
        break;

    case CTC_L3IF_PROP_ROUTE_MAC_LOW_8BITS:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RouterMacLabel_f);
        break;

    /*dest  interface */
    case CTC_L3IF_PROP_MTU_EN:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MtuCheckEn_f);
        break;

    case CTC_L3IF_PROP_MTU_EXCEPTION_EN:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MtuExceptionEn_f);
        break;

    case CTC_L3IF_PROP_EGS_MCAST_TTL_THRESHOLD:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_McastTtlThreshold_f);
        break;

    case CTC_L3IF_PROP_MTU_SIZE:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MtuSize_f);
        break;

    case CTC_L3IF_PROP_EGS_MAC_SA_PREFIX_TYPE:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MacSaType_f);
        break;

    case CTC_L3IF_PROP_EGS_MAC_SA_LOW_8BITS:
        cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MacSa_f);
        break;

    case CTC_L3IF_PROP_MPLS_EN:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_MplsEn_f);
        break;

    case CTC_L3IF_PROP_MPLS_LABEL_SPACE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_MplsLabelSpace_f);
        break;

    case CTC_L3IF_PROP_RPF_CHECK_TYPE:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RpfType_f);
        break;

    case CTC_L3IF_PROP_RPF_PERMIT_DEFAULT:
        cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_RpfPermitDefault_f);
        break;

    case CTC_L3IF_PROP_IGMP_SNOOPING_EN:
    /*   enable = (value)?TRUE:FALSE;
         ret = sys_greatbelt_vlan_set_igmp_snoop_en(&vlan_info, enable);*/

    default:
        return CTC_E_INVALID_PARAM;
    }

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id :%d l3if_prop:%d \n", l3if_id, l3if_prop);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, value));

    if (CTC_L3IF_PROP_ROUTE_EN == l3if_prop)
    {
        *value = !(*value);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_set_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool enbale)
{
#ifdef NEVER
    uint8   lchip = 0;
    uint32  cmd = 0;
    uint32  value = 0;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id = %d, index = %d, enbale[0]= %d\n", l3if_id, index, enbale);

    cmd = DRV_IOR(DS_SRC_INTERFACE, DS_SRC_INTERFACE_EXCEPTION3_EN);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &value));
    if (TRUE == enbale)
    {
        value | = (1 << index);

    }
    else
    {
        value& = (~(1 << index));
    }

    cmd = DRV_IOW(DS_SRC_INTERFACE, DS_SRC_INTERFACE_EXCEPTION3_EN);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &value));

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_get_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool* enbale)
{
#ifdef NEVER

    uint32  cmd = 0;
    uint32  value = 0;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    CTC_PTR_VALID_CHECK(enbale);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id = %d, index = %d\n", l3if_id, index);

    cmd = DRV_IOR(DS_SRC_INTERFACE, DS_SRC_INTERFACE_EXCEPTION3_EN);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l3if_id, cmd, &value));
    *enbale = (value & (1 << index)) ? TRUE : FALSE;
    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_set_aclqos_property(uint8 lchip, uint16 l3if_id, sys_l3if_aclaos_property_t l3if_prop, uint32 value[CTC_MAX_LOCAL_CHIP_NUM])
{

    return CTC_E_NONE;
}

/**
@brief    Get  l3 interface's properties  according to interface id
*/

int32
sys_greatbelt_l3if_get_aclqos_property(uint8 lchip, uint16 l3if_id, sys_l3if_aclaos_property_t l3if_prop, uint32 value[CTC_MAX_LOCAL_CHIP_NUM])
{

    return CTC_E_NONE;
}

/**
@brief    Config  l3 interface's  properties
*/
int32
sys_greatbelt_l3if_set_span_property(uint8 lchip, uint16 l3if_id, sys_l3if_span_property_t l3if_prop, uint32 value)
{

    return CTC_E_NONE;
}

/**
@brief    Get  l3 interface's properties  according to interface id
*/

int32
sys_greatbelt_l3if_get_span_property(uint8 lchip, uint16 l3if_id, sys_l3if_span_property_t l3if_prop, uint32* value)
{

    return CTC_E_NONE;
}

/**
@brief    Config  40bits  virtual router mac prefix
*/

int32
sys_greatbelt_l3if_set_vmac_prefix(uint8 lchip, ctc_l3if_route_mac_type_t prefix_type, mac_addr_t mac_40bit)
{
    uint32  cmd = 0;
    ipe_router_mac_ctl_t rt_mac_ctl;
    epe_l2_router_mac_sa_t l2rt_macsa;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("prefix_type :%d mac_prefix:%.2x%.2x.%.2x%.2x.%.2x \n", prefix_type,
                      mac_40bit[0], mac_40bit[1], mac_40bit[2], mac_40bit[3], mac_40bit[4]);

    CTC_MAX_VALUE_CHECK(prefix_type, CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID);

    cmd = DRV_IOR(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rt_mac_ctl));

    cmd = DRV_IOR(EpeL2RouterMacSa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l2rt_macsa));

    switch (prefix_type)
    {
    case CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE0:
        rt_mac_ctl.router_mac_type0_47_40 = mac_40bit[0];
        rt_mac_ctl.router_mac_type0_39_8 = (mac_40bit[1] << 24) + (mac_40bit[2] << 16) + (mac_40bit[3] << 8) + mac_40bit[4];
        l2rt_macsa.router_mac_sa0_47_32 = (mac_40bit[0] << 8) + mac_40bit[1];
        l2rt_macsa.router_mac_sa0_31_8 = (mac_40bit[2] << 16) + (mac_40bit[3] << 8) + mac_40bit[4];
        break;

    case CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE1:
        rt_mac_ctl.router_mac_type1_47_40 = mac_40bit[0];
        rt_mac_ctl.router_mac_type1_39_8 = (mac_40bit[1] << 24) + (mac_40bit[2] << 16) + (mac_40bit[3] << 8) + mac_40bit[4];
        l2rt_macsa.router_mac_sa1_47_32 = (mac_40bit[0] << 8) + mac_40bit[1];
        l2rt_macsa.router_mac_sa1_31_8 = (mac_40bit[2] << 16) + (mac_40bit[3] << 8) + mac_40bit[4];

        break;

    default:
        return CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE;
    }

    cmd = DRV_IOW(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rt_mac_ctl));

    cmd = DRV_IOW(EpeL2RouterMacSa_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l2rt_macsa));

    return CTC_E_NONE;
}

/**
@brief    Get  40bits   virtual router mac prefix
*/
int32
sys_greatbelt_l3if_get_vmac_prefix(uint8 lchip, ctc_l3if_route_mac_type_t prefix_type, mac_addr_t mac_40bit)
{
    uint32  cmd = 0;
    ipe_router_mac_ctl_t rt_mac_ctl;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IF_DBG_FUNC("");
    CTC_MAX_VALUE_CHECK(prefix_type, CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID);

    cmd = DRV_IOR(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rt_mac_ctl));

    switch (prefix_type)
    {
    case CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE0:
        mac_40bit[0] = (rt_mac_ctl.router_mac_type0_47_40) & 0xFF;
        mac_40bit[1] = (rt_mac_ctl.router_mac_type0_39_8 >> 24) & 0xFF;
        mac_40bit[2] = (rt_mac_ctl.router_mac_type0_39_8 >> 16) & 0xFF;
        mac_40bit[3] = (rt_mac_ctl.router_mac_type0_39_8 >> 8) & 0xFF;
        mac_40bit[4] = (rt_mac_ctl.router_mac_type0_39_8) & 0xFF;
        break;

    case CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE1:
        mac_40bit[0] = rt_mac_ctl.router_mac_type1_47_40 & 0xFF;
        mac_40bit[1] = (rt_mac_ctl.router_mac_type1_39_8 >> 24) & 0xFF;
        mac_40bit[2] = (rt_mac_ctl.router_mac_type1_39_8 >> 16) & 0xFF;
        mac_40bit[3] = (rt_mac_ctl.router_mac_type1_39_8 >> 8) & 0xFF;
        mac_40bit[4] = (rt_mac_ctl.router_mac_type1_39_8) & 0xFF;
        break;

    default:
        return CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE;
    }

    SYS_L3IF_DBG_INFO("prefix_type :%d mac_prefix:%.2x%.2x.%.2x%.2x.%.2x \n", prefix_type,
                      mac_40bit[0], mac_40bit[1], mac_40bit[2], mac_40bit[3], mac_40bit[4]);

    return CTC_E_NONE;
}

/**
@brief    Config a low 8 bits virtual router - mac  in the L3 interface, it can config up to 4 VRIDs  for a index

*/
int32
sys_greatbelt_l3if_add_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac)
{
    uint8 find_flag = FALSE;
    uint8 first_find_flag = FALSE;
    uint32 cmd = 0;
    uint16  index = 0;
    uint8  route_mac_label = 0;
    ds_router_mac_t rt_mac;
    ds_src_interface_t src_interface;
    sys_l3if_vmac_entry_t* vmac_entry = NULL;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    CTC_PTR_VALID_CHECK(p_l3if_vmac);
    CTC_MAX_VALUE_CHECK(p_l3if_vmac->low_8bits_mac_index, MAX_CTC_L3IF_VMAC_MAC_INDEX);

    if (p_l3if_vmac->prefix_type > CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID)
    {
        return CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE;
    }

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id:%d,prefix_type :%d low_8bits_mac:%.2x\n",
                      l3if_id, p_l3if_vmac->prefix_type, p_l3if_vmac->low_8bits_mac);

    SYS_L3IF_LOCK(lchip);
    route_mac_label = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label;
    if (route_mac_label == SYS_ROUTER_MAC_LABEL_INDEX)
    {
        for (index = 0; index < SYS_ROUTER_MAC_LABEL_INDEX; index++)
        {
            if (p_gb_l3if_master[lchip]->vmac_entry[index].vaild == FALSE)
            {
                route_mac_label = index;
                find_flag = TRUE;
                first_find_flag = TRUE;
                break;
            }
        }
    }
    else
    {
        find_flag = TRUE;
    }

    if (FALSE == find_flag)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_VMAC_ENTRY_EXCEED_MAX_SIZE, p_gb_l3if_master[lchip]->p_mutex);
    }

    cmd = DRV_IOR(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac), p_gb_l3if_master[lchip]->p_mutex);
    vmac_entry = &p_gb_l3if_master[lchip]->vmac_entry[route_mac_label];

    if (first_find_flag)
    {
        rt_mac.router_mac0_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac1_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac2_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac3_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac0_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac1_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac2_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->router_mac3_type  = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        vmac_entry->vaild  = FALSE;
    }

    switch (p_l3if_vmac->low_8bits_mac_index)
    {
    case 0:
        rt_mac.router_mac0_byte = p_l3if_vmac->low_8bits_mac;
        rt_mac.router_mac0_type = p_l3if_vmac->prefix_type;
        vmac_entry->router_mac0_type = p_l3if_vmac->prefix_type;
        break;

    case 1:
        rt_mac.router_mac1_byte = p_l3if_vmac->low_8bits_mac;
        rt_mac.router_mac1_type = p_l3if_vmac->prefix_type;
        vmac_entry->router_mac1_type = p_l3if_vmac->prefix_type;
        break;

    case 2:
        rt_mac.router_mac2_byte = p_l3if_vmac->low_8bits_mac;
        rt_mac.router_mac2_type = p_l3if_vmac->prefix_type;
        vmac_entry->router_mac2_type = p_l3if_vmac->prefix_type;
        break;

    case 3:
        rt_mac.router_mac3_byte = p_l3if_vmac->low_8bits_mac;
        rt_mac.router_mac3_type = p_l3if_vmac->prefix_type;
        vmac_entry->router_mac3_type = p_l3if_vmac->prefix_type;
        break;

    default:
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE, p_gb_l3if_master[lchip]->p_mutex);
    }

    cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac), p_gb_l3if_master[lchip]->p_mutex);
    p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label = route_mac_label;
    p_gb_l3if_master[lchip]->vmac_entry[route_mac_label].vaild = TRUE;

    cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface), p_gb_l3if_master[lchip]->p_mutex);
    src_interface.router_mac_label = route_mac_label;
    src_interface.router_mac_type  = 3;
    cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface),  p_gb_l3if_master[lchip]->p_mutex);

    SYS_L3IF_UNLOCK(lchip);
    return CTC_E_NONE;

}

/**
@brief    Config a low 8 bits virtual router - mac  in the L3 interface, it can config up to 4 VRIDs  for a index

*/
int32
sys_greatbelt_l3if_remove_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac)
{
    uint32  cmd = 0;
    uint8  route_mac_label = 0;
    ds_router_mac_t rt_mac;
    sys_l3if_vmac_entry_t* vmac_entry = NULL;
    ds_src_interface_t src_interface;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);

    CTC_PTR_VALID_CHECK(p_l3if_vmac);
    CTC_MAX_VALUE_CHECK(p_l3if_vmac->low_8bits_mac_index, MAX_CTC_L3IF_VMAC_MAC_INDEX);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id :%d low_8bits_mac_index:%d\n", l3if_id, p_l3if_vmac->low_8bits_mac_index);

    SYS_L3IF_LOCK(lchip);
    route_mac_label = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label;
    if (route_mac_label == SYS_ROUTER_MAC_LABEL_INDEX
        || !p_gb_l3if_master[lchip]->vmac_entry[route_mac_label].vaild)
    {
        CTC_ERROR_RETURN_L3IF_UNLOCK(CTC_E_L3IF_VMAC_NOT_EXIST);
    }

    vmac_entry = &p_gb_l3if_master[lchip]->vmac_entry[route_mac_label];

    cmd = DRV_IOR(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac));

    switch (p_l3if_vmac->low_8bits_mac_index)
    {
    case 0:
        rt_mac.router_mac0_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac0_byte = 0;
        vmac_entry->router_mac0_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        break;

    case 1:
        rt_mac.router_mac1_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac1_byte = 0;
        vmac_entry->router_mac1_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        break;

    case 2:
        rt_mac.router_mac2_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac2_byte = 0;
        vmac_entry->router_mac2_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        break;

    case 3:
        rt_mac.router_mac3_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        rt_mac.router_mac3_byte = 0;
        vmac_entry->router_mac3_type = CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID;
        break;

    default:
        return CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE;
    }

    cmd = DRV_IOW(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac));

    if (vmac_entry->router_mac0_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID
        && vmac_entry->router_mac1_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID
        && vmac_entry->router_mac2_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID
        && vmac_entry->router_mac3_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_INVALID)
    {
        p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label = SYS_ROUTER_MAC_LABEL_INDEX;
        p_gb_l3if_master[lchip]->vmac_entry[route_mac_label].vaild = FALSE;

        cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface));
        src_interface.router_mac_label = p_gb_l3if_master[lchip]->router_mac[5];
        src_interface.router_mac_type  = 2;
        cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, l3if_id, cmd, &src_interface));
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
@brief    Get a low 8 bits router - mac  according to interface id
*/
int32
sys_greatbelt_l3if_get_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac)
{
    uint32  cmd = 0;
    uint8  route_mac_label = 0;
    ds_router_mac_t rt_mac;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);
    SYS_L3IF_CREATE_CHECK(l3if_id);
    CTC_PTR_VALID_CHECK(p_l3if_vmac);
    CTC_MAX_VALUE_CHECK(p_l3if_vmac->low_8bits_mac_index, MAX_CTC_L3IF_VMAC_MAC_INDEX);

    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("l3if_id :%d low_8bits_mac_index:%d\n", l3if_id, p_l3if_vmac->low_8bits_mac_index);
    SYS_L3IF_LOCK(lchip);
    route_mac_label = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].router_mac_label;

    if (route_mac_label == SYS_ROUTER_MAC_LABEL_INDEX
        || !p_gb_l3if_master[lchip]->vmac_entry[route_mac_label].vaild)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_VMAC_NOT_EXIST;
    }

    SYS_L3IF_UNLOCK(lchip);
    cmd = DRV_IOR(DsRouterMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, route_mac_label, cmd, &rt_mac));

    switch (p_l3if_vmac->low_8bits_mac_index)
    {
    case 0:
        p_l3if_vmac->low_8bits_mac = rt_mac.router_mac0_byte;
        p_l3if_vmac->prefix_type  = rt_mac.router_mac0_type;
        break;

    case 1:
        p_l3if_vmac->low_8bits_mac = rt_mac.router_mac1_byte;
        p_l3if_vmac->prefix_type  = rt_mac.router_mac1_type;
        break;

    case 2:
        p_l3if_vmac->low_8bits_mac = rt_mac.router_mac2_byte;
        p_l3if_vmac->prefix_type   = rt_mac.router_mac2_type;
        break;

    case 3:
        p_l3if_vmac->low_8bits_mac = rt_mac.router_mac3_byte;
        p_l3if_vmac->prefix_type   = rt_mac.router_mac3_type;
        break;

    default:
        return CTC_E_L3IF_VMAC_INVALID_PREFIX_TYPE;
    }

    return CTC_E_NONE;
}

/**
@brief    Config  router mac

*/
int32
sys_greatbelt_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr)
{
    uint32  read_cmd = 0;
    uint32  write_cmd = 0;
    uint32  read_dest_if_cmd = 0;
    uint32  write_dest_if_cmd = 0;
    uint32  read_src_if_cmd = 0;
    uint32  write_src_if_cmd = 0;
    uint16  index = 0;

    ds_src_interface_t             src_interface;
    ipe_router_mac_ctl_t         rt_mac_ctl;
    ds_dest_interface_t           dest_interface;
    epe_l2_router_mac_sa_t   l2rt_macsa;

    sal_memset(&dest_interface, 0, sizeof(dest_interface));
    sal_memset(&src_interface, 0, sizeof(src_interface));
    sal_memset(&rt_mac_ctl, 0, sizeof(rt_mac_ctl));
    sal_memset(&l2rt_macsa, 0, sizeof(l2rt_macsa));

    SYS_L3IF_INIT_CHECK();
    SYS_L3IF_DBG_FUNC("");
    SYS_L3IF_DBG_INFO("router_mac:%.2x%.2x.%.2x%.2x.%.2x%.2x \n",
                      mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    read_cmd = DRV_IOR(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
    write_cmd = DRV_IOW(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
    read_src_if_cmd = DRV_IOR(DsSrcInterface_t, DRV_ENTRY_FLAG);
    write_src_if_cmd = DRV_IOW(DsSrcInterface_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, read_cmd, &rt_mac_ctl));
    rt_mac_ctl.router_mac_type2_47_40 = mac_addr[0];
    rt_mac_ctl.router_mac_type2_39_8 = (mac_addr[1] << 24) + (mac_addr[2] << 16) + (mac_addr[3] << 8) + mac_addr[4];

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, write_cmd, &rt_mac_ctl));

    SYS_L3IF_LOCK(lchip);
    for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
    {
        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, index, read_src_if_cmd, &src_interface));
        if (src_interface.router_mac_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC
            && src_interface.router_mac_label == p_gb_l3if_master[lchip]->router_mac[5])
        {
            src_interface.router_mac_label = mac_addr[5];
        }

        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, index, write_src_if_cmd, &src_interface));
    }

    read_cmd = DRV_IOR(EpeL2RouterMacSa_t, DRV_ENTRY_FLAG);
    write_cmd = DRV_IOW(EpeL2RouterMacSa_t, DRV_ENTRY_FLAG);
    read_dest_if_cmd = DRV_IOR(DsDestInterface_t, DRV_ENTRY_FLAG);
    write_dest_if_cmd = DRV_IOW(DsDestInterface_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, 0, read_cmd, &l2rt_macsa));
    l2rt_macsa.router_mac_sa2_47_32 = (mac_addr[0] << 8) + mac_addr[1];
    l2rt_macsa.router_mac_sa2_31_8 = (mac_addr[2] << 16) + (mac_addr[3] << 8) + mac_addr[4];
    CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, 0, write_cmd, &l2rt_macsa));

    for (index = 0; index <= MAX_CTC_L3IF_ID; index++)
    {
        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, index, read_dest_if_cmd, &dest_interface));
        if (dest_interface.mac_sa_type == CTC_L3IF_ROUTE_MAC_PFEFIX_TYPE_RSV_ROUTER_MAC
            && dest_interface.mac_sa == p_gb_l3if_master[lchip]->router_mac[5])
        {
            dest_interface.mac_sa = mac_addr[5];
        }

        CTC_ERROR_RETURN_L3IF_UNLOCK(DRV_IOCTL(lchip, index, write_dest_if_cmd, &dest_interface));
    }

    sal_memcpy(p_gb_l3if_master[lchip]->router_mac, mac_addr, sizeof(mac_addr_t));
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;

}

/**
@brief    Get  router mac
*/
int32
sys_greatbelt_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr)
{
    uint32  read_cmd = 0;
    ipe_router_mac_ctl_t rt_mac_ctl;

    SYS_L3IF_INIT_CHECK();
    SYS_L3IF_DBG_FUNC("");
    read_cmd = DRV_IOR(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, read_cmd, &rt_mac_ctl));
    SYS_L3IF_LOCK(lchip);
    mac_addr[0] = rt_mac_ctl.router_mac_type2_47_40 & 0xFF;
    mac_addr[1] = (rt_mac_ctl.router_mac_type2_39_8 >> 24) & 0xFF;
    mac_addr[2] = (rt_mac_ctl.router_mac_type2_39_8 >> 16) & 0xFF;
    mac_addr[3] = (rt_mac_ctl.router_mac_type2_39_8 >> 8) & 0xFF;
    mac_addr[4] = (rt_mac_ctl.router_mac_type2_39_8) & 0xFF;
    mac_addr[5]   = p_gb_l3if_master[lchip]->router_mac[5];
    SYS_L3IF_UNLOCK(lchip);

    SYS_L3IF_DBG_INFO("mac_prefix:%.2x%.2x.%.2x%.2x.%.2x%.2x \n",
                      mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    return CTC_E_NONE;
}

/**
@brief    Get  vlan_ptr by l3if_id
*/
int32
sys_greatbelt_l3if_get_vlan_ptr(uint8 lchip, uint16 l3if_id, uint16* vlan_ptr)
{
    SYS_L3IF_INIT_CHECK();
    SYS_L3IFID_VAILD_CHECK(l3if_id);

    SYS_L3IF_DBG_FUNC("");

    SYS_L3IF_LOCK(lchip);
    if (!p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vaild)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_L3IF_NOT_EXIST;
    }

    *vlan_ptr = p_gb_l3if_master[lchip]->l3if_prop[l3if_id].vlan_ptr;
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
@brief get max vrf id , 0 means disable else vrfid must less than return value
*/
uint16
sys_greatbelt_l3if_get_max_vrfid(uint8 lchip, uint8 ip_ver)
{
    return (MAX_CTC_IP_VER == ip_ver) ? (p_gb_l3if_master[lchip]->max_vrfid_num) :
           ((CTC_IP_VER_4 == ip_ver) ? (p_gb_l3if_master[lchip]->ipv4_vrf_en ? p_gb_l3if_master[lchip]->max_vrfid_num : 0)
            : (p_gb_l3if_master[lchip]->ipv6_vrf_en ? p_gb_l3if_master[lchip]->max_vrfid_num : 0));
}

int32
sys_greatbelt_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats)
{
    SYS_L3IF_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_vrf_stats);

    if (p_vrf_stats->vrf_id >= p_gb_l3if_master[lchip]->max_vrfid_num)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_vrf_stats->type != CTC_L3IF_VRF_STATS_UCAST)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_L3IF_LOCK(lchip);
    if (p_vrf_stats->enable)
    {
        /* vrf stats is always enable physical, here only clear stats */
        CTC_ERROR_RETURN_L3IF_UNLOCK(sys_greatbelt_stats_clear_vrf_stats(lchip, p_vrf_stats->vrf_id));

        p_gb_l3if_master[lchip]->vrfid_stats_en[p_vrf_stats->vrf_id] = TRUE;
    }
    else
    {
        p_gb_l3if_master[lchip]->vrfid_stats_en[p_vrf_stats->vrf_id] = FALSE;
    }
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3if_get_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats)
{
    SYS_L3IF_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_vrf_stats);

    SYS_L3IF_LOCK(lchip);
    if (p_vrf_stats->vrf_id >= p_gb_l3if_master[lchip]->max_vrfid_num)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    if (p_vrf_stats->type != CTC_L3IF_VRF_STATS_UCAST)
    {
        SYS_L3IF_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    p_vrf_stats->enable = p_gb_l3if_master[lchip]->vrfid_stats_en[p_vrf_stats->vrf_id];
    SYS_L3IF_UNLOCK(lchip);

    return CTC_E_NONE;
}




/**
 @file sys_greatbelt_common.c

 @date 2011-11-28

 @version v2.0

 This file provides functions which can be used for GB
*/

#include "sys_greatbelt_common.h"
#include "ctc_error.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_acl.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_port.h"
#include "greatbelt/include/drv_chip_info.h"
#include "greatbelt/include/drv_data_path.h"


char*
sys_greatbelt_output_mac(mac_addr_t in_mac)
{
    static char out_mac[20] = {0};

    sal_sprintf(out_mac, "%.2x%.2x.%.2x%.2x.%.2x%.2x", in_mac[0], in_mac[1], in_mac[2], in_mac[3], in_mac[4], in_mac[5]);
    return out_mac;
}

uint8
sys_greatebelt_common_get_mac_id(uint8 lchip, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint8 gchip = 0;
    drv_datapath_port_capability_t port_cap;

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

     /*SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);*/
    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    if ((gchip == 0x1F) || (gchip > SYS_GB_MAX_GCHIP_CHIP_ID))
    {
        ret = CTC_E_INVALID_GLOBAL_CHIPID;
    }

    lport = (gport & CTC_LOCAL_PORT_MASK);
    if (lport >= SYS_GB_MAX_PORT_NUM_PER_CHIP)
    {
        ret = CTC_E_INVALID_LOCAL_PORT;
    }

    if (FALSE == sys_greatbelt_chip_is_local(lchip, gchip))
    {
        ret = CTC_E_CHIP_IS_REMOTE;
    }

    if (CTC_E_NONE == ret)
    {
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    }

    if (!ret)
    {
         return port_cap.mac_id;
    }
    else
    {
        return 0xFF;
    }

}

uint8
sys_greatebelt_common_get_channel_id(uint8 lchip, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint8 gchip = 0;
    uint8 chan_id = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

     /*SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);*/
    gchip = SYS_MAP_GPORT_TO_GCHIP(gport);
    if ((gchip == 0x1F) || (gchip > SYS_GB_MAX_GCHIP_CHIP_ID))
    {
        ret = CTC_E_INVALID_GLOBAL_CHIPID;
        goto EXIT;
    }

    lport = (gport & CTC_LOCAL_PORT_MASK);
    if (lport >= SYS_GB_MAX_PORT_NUM_PER_CHIP)
    {
        ret = CTC_E_INVALID_LOCAL_PORT;
        goto EXIT;
    }

    if ((lport < SYS_GB_MAX_PHY_PORT))
    {
        if (FALSE == sys_greatbelt_chip_is_local(lchip, gchip))
        {
            ret = CTC_E_CHIP_IS_REMOTE;
            goto EXIT;
        }

        chan_id = drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    }
    else
    {
        /* dont care lchip if lport is internal port */
        chan_id = drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    }

EXIT:
    if (!ret)
    {
         return chan_id;
    }
    else
    {
        return 0xFF;
    }

}

uint8
sys_greatebelt_common_get_lport_with_mac(uint8 lchip, uint8 mac_id)
{
    uint32 chan_id = 0;
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;

    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));

    /* check mac valid */
    if (mac_id >= (SYS_MAX_GMAC_PORT_NUM+SYS_MAX_SGMAC_PORT_NUM))
    {
         return 0xFF;
    }


    cmd = DRV_IOR(NetRxChannelMap_t, NetRxChannelMap_ChanId_f);
    ret = DRV_IOCTL(lchip, mac_id, cmd, &chan_id);

    cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    ret = ret ? ret : DRV_IOCTL(lchip, chan_id, cmd, &ipe_header_adjust_phyport_map);

    if (!ret)
    {
        return ipe_header_adjust_phyport_map.local_phy_port;
    }
    else
    {
        return 0xFF;
    }

}

uint8
sys_greatebelt_common_get_lport_with_chan(uint8 lchip, uint8 chan_id)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;

    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));

    cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, chan_id, cmd, &ipe_header_adjust_phyport_map);

    if (!ret)
    {
        return ipe_header_adjust_phyport_map.local_phy_port;
    }
    else
    {
        return 0xFF;
    }

}

bool
sys_greatbelt_common_check_mac_valid(uint8 lchip, uint8 mac_id)
{
    uint8 lport = 0;
    drv_datapath_port_capability_t port_cap;

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    for (lport = 0; lport < 60; lport++)
    {
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if (port_cap.valid)
        {
            if (port_cap.mac_id == mac_id)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

int32
sys_greatbelt_common_init_unknown_mcast_tocpu(uint8 lchip, uint32* iloop_nhid)
{
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 nhid = 0;
    ctc_internal_port_assign_para_t port_assign;
    ctc_loopback_nexthop_param_t nh_param;
    ctc_acl_group_info_t group;
    ctc_acl_entry_t acl_entry;

    CTC_PTR_VALID_CHECK(iloop_nhid);

    sal_memset(&group, 0, sizeof(group));
    sal_memset(&acl_entry, 0, sizeof(acl_entry));
    sal_memset(&port_assign, 0, sizeof(port_assign));
    sal_memset(&nh_param, 0, sizeof(nh_param));

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    port_assign.gchip = gchip;
    port_assign.type = CTC_INTERNAL_PORT_TYPE_ILOOP;
    CTC_ERROR_RETURN(sys_greatbelt_internal_port_allocate(lchip, &port_assign));

    gport = CTC_MAP_LPORT_TO_GPORT(gchip, port_assign.inter_port);
    CTC_ERROR_RETURN(sys_greatbelt_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_EN, CTC_INGRESS, 1));
    CTC_ERROR_RETURN(sys_greatbelt_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_USE_CLASSID, CTC_INGRESS, 1));
    CTC_ERROR_RETURN(sys_greatbelt_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_CLASSID, CTC_INGRESS, SYS_GB_MAX_ACL_PORT_CLASSID));

    group.type = CTC_ACL_GROUP_TYPE_PORT_CLASS;
    group.lchip = lchip;
    group.dir = CTC_INGRESS;
    group.un.port_class_id = SYS_GB_MAX_ACL_PORT_CLASSID;
    CTC_ERROR_RETURN(sys_greatbelt_acl_create_group(lchip, CTC_ACL_GROUP_ID_NORMAL, &group));

    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID;
    acl_entry.key.type = CTC_ACL_KEY_IPV4;
    acl_entry.key.u.ipv4_key.mac_da[0] = 0;
    acl_entry.key.u.ipv4_key.mac_da_mask[0] = 1;
    CTC_SET_FLAG(acl_entry.key.u.ipv4_key.flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD;
    CTC_ERROR_RETURN(sys_greatbelt_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    sal_memset(&acl_entry, 0, sizeof(acl_entry));
    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID - 1;
    acl_entry.key.type = CTC_ACL_KEY_IPV4;
    sal_memset(acl_entry.key.u.ipv4_key.mac_da, 0xFF, CTC_ETH_ADDR_LEN);
    sal_memset(acl_entry.key.u.ipv4_key.mac_da_mask, 0xFF, CTC_ETH_ADDR_LEN);
    CTC_SET_FLAG(acl_entry.key.u.ipv4_key.flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD;
    CTC_ERROR_RETURN(sys_greatbelt_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    sal_memset(&acl_entry, 0, sizeof(acl_entry));
    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID - 2;
    acl_entry.key.type = CTC_ACL_KEY_IPV4;
    acl_entry.key.u.ipv4_key.mac_da[0] = 1;
    acl_entry.key.u.ipv4_key.mac_da_mask[0] = 1;

    CTC_SET_FLAG(acl_entry.key.u.ipv4_key.flag, CTC_ACL_IPV4_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD | CTC_ACL_ACTION_FLAG_COPY_TO_CPU;
    CTC_ERROR_RETURN(sys_greatbelt_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    CTC_ERROR_RETURN(sys_greatbelt_acl_install_group(lchip, CTC_ACL_GROUP_ID_NORMAL, NULL));

    nh_param.lpbk_lport = port_assign.inter_port;
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_max_external_nhid(lchip, &nhid));
    CTC_ERROR_RETURN(sys_greatbelt_iloop_nh_create(lchip, nhid - 1, &nh_param));

    *iloop_nhid = nhid - 1;

    return CTC_E_NONE;

}



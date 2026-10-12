/**
 @file sys_goldengate_common.c

 @date 2011-11-28

 @version v2.0

 This file provides functions which can be used for GB
*/

#include "ctc_qos.h"
#include "ctc_error.h"
#include "ctc_port.h"

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_datapath.h"
#include "sys_goldengate_acl.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_port.h"

#include "goldengate/include/drv_io.h"


char*
sys_goldengate_output_mac(mac_addr_t in_mac)
{
    static char out_mac[20] = {0};

    sal_sprintf(out_mac, "%.2x%.2x.%.2x%.2x.%.2x%.2x", in_mac[0], in_mac[1], in_mac[2], in_mac[3], in_mac[4], in_mac[5]);
    return out_mac;
}

#define SYS_TRUST_INVALID     0
#define SYS_TRUST_PORT          1
#define SYS_TRUST_OUTER        2
#define SYS_TRUST_COS            3
#define SYS_TRUST_DSCP          4
#define SYS_TRUST_IP               5
#define SYS_TRUST_STAG_COS  6
#define SYS_TRUST_CTAG_COS  7
#define SYS_TRUST_MAX            8

/*return mac is global mac, gport is ctc port*/
uint8
sys_goldengate_common_get_mac_id(uint8 lchip, uint16 gport)
{
    uint8  mac_id = 0;
    sys_datapath_lport_attr_t* p_port = NULL;
    int32 ret = 0;

    ret = sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port);
    if (ret < 0)
    {
        return 0xff;
    }

    if (p_port->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return 0xff;
    }

    mac_id = p_port->mac_id + p_port->slice_id*64;

    return mac_id;
}

/*return channel is global channel, gport is ctc gport*/
uint8
sys_goldengate_common_get_channel_id(uint8 lchip, uint16 gport)
{
    uint8  chan_id = 0xFF;
    sys_datapath_lport_attr_t* p_port = NULL;
    int32 ret = 0;

    if(FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(gport)))
    {
        return 0xFF;
    }
    ret = sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port);
    if (ret < 0)
    {
        return 0xff;
    }

    if ((p_port->chan_id == 0xff) || (p_port->port_type == SYS_DATAPATH_RSV_PORT))
    {
        return 0xff;
    }
    chan_id = p_port->chan_id + p_port->slice_id*64;

    return chan_id;
}

uint8
sys_goldengate_common_get_mac_type(uint8 lchip, uint8 mac_id)
{
    uint16 lport = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;
    uint8 slice_id = 0;
    uint8 slice_mac = 0;

    slice_id = (mac_id >= 64)?1:0;
    slice_mac = mac_id - slice_id*64;

    lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, slice_mac);
    if (lport == SYS_DATAPATH_USELESS_MAC)
    {
        return 0xff;
    }

    p_port_cap = sys_goldengate_datapath_get_port_capability(lchip, lport, slice_id);
    if (p_port_cap == NULL)
    {
        return 0xff;
    }

    return p_port_cap->speed_mode;
}

/*mac_id is global mac id, return port is sys Port*/
uint16
sys_goldengate_common_get_lport_with_mac(uint8 lchip, uint8 mac_id)
{
    uint16 chip_lport = 0;
    uint16 drv_lport = 0;
    uint8 slice_id = 0;
    uint8 slice_mac = 0;

    slice_id = (mac_id >= 64)?1:0;
    slice_mac = mac_id - slice_id*64;

    chip_lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, slice_mac);
    if (SYS_COMMON_USELESS_MAC == chip_lport)
    {
        return SYS_COMMON_USELESS_MAC;
    }

    drv_lport = slice_id*256+chip_lport;

    return SYS_MAP_DRV_LPORT_TO_SYS_LPORT(drv_lport);
}

/*chan_id is global chan id, return port is sys Port*/
uint16
sys_goldengate_common_get_lport_with_chan(uint8 lchip, uint8 chan_id)
{
    uint32 cmd = 0;
    IpeHeaderAdjustPhyPortMap_m port_map;
    uint16 chip_lport = 0;
    uint16 drv_lport = 0;
    uint8 slice_id = 0;

    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);

    cmd = DRV_IOR((IpeHeaderAdjustPhyPortMap_t), DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_id, cmd, &port_map);

    chip_lport = GetIpeHeaderAdjustPhyPortMap(V,localPhyPort_f,&port_map);

    drv_lport = slice_id*256+chip_lport;

    return SYS_MAP_DRV_LPORT_TO_SYS_LPORT(drv_lport);
}

bool
sys_goldengate_common_check_mac_valid(uint8 lchip, uint8 mac_id)
{
    uint16 lport = 0;
    uint8 slice_id = 0;
    uint8 slice_mac = 0;

    slice_id = (mac_id >= 64)?1:0;
    slice_mac = mac_id - slice_id*64;

    lport = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, slice_mac);
    if (lport == SYS_DATAPATH_USELESS_MAC)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

/*this interface is used to get network port capability, lport is sys layer port */
int32
sys_goldengate_common_get_port_capability(uint8 lchip, uint16 lport, sys_datapath_lport_attr_t** p_port)
{
    uint16 drv_lport = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);
    slice_id = (drv_lport >= SYS_CHIP_PER_SLICE_PORT_NUM)?1:0;
    chip_port = drv_lport - slice_id*SYS_CHIP_PER_SLICE_PORT_NUM;

    p_port_attr = sys_goldengate_datapath_get_port_capability(lchip, chip_port, slice_id);
    if (p_port_attr == NULL)
    {
        return CTC_E_MAC_NOT_USED;
    }

    *p_port = p_port_attr;

    return CTC_E_NONE;
}

int32
sys_goldengate_common_map_qos_policy(uint8 lchip, uint32 ctc_qos_trust, uint32* policy)
{
    switch (ctc_qos_trust)
    {
        case CTC_QOS_TRUST_PORT:
            *policy = SYS_TRUST_PORT;
            break;

        case CTC_QOS_TRUST_OUTER:
            *policy = SYS_TRUST_OUTER;
            break;

        case CTC_QOS_TRUST_COS:
            *policy = SYS_TRUST_COS;
            break;

        case CTC_QOS_TRUST_DSCP:
            *policy = SYS_TRUST_DSCP;
            break;

        case CTC_QOS_TRUST_IP:
            *policy = SYS_TRUST_IP;
            break;

        case CTC_QOS_TRUST_STAG_COS:
            *policy = SYS_TRUST_STAG_COS;
            break;

        case CTC_QOS_TRUST_CTAG_COS:
            *policy = SYS_TRUST_CTAG_COS;
            break;

        case CTC_QOS_TRUST_MAX:
            *policy = SYS_TRUST_INVALID;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_common_unmap_qos_policy(uint8 lchip, uint32 policy, uint32* ctc_qos_trust)
{
    switch (policy)
    {
        case SYS_TRUST_PORT:
            *ctc_qos_trust = CTC_QOS_TRUST_PORT;
            break;

        case SYS_TRUST_OUTER:
            *ctc_qos_trust = CTC_QOS_TRUST_OUTER;
            break;

        case SYS_TRUST_COS:
            *ctc_qos_trust = CTC_QOS_TRUST_COS;
            break;

        case SYS_TRUST_DSCP:
            *ctc_qos_trust = CTC_QOS_TRUST_DSCP;
            break;

        case SYS_TRUST_IP:
            *ctc_qos_trust = CTC_QOS_TRUST_IP;
            break;

        case SYS_TRUST_STAG_COS:
            *ctc_qos_trust = CTC_QOS_TRUST_STAG_COS;
            break;

        case SYS_TRUST_CTAG_COS:
            *ctc_qos_trust = CTC_QOS_TRUST_CTAG_COS;
            break;

        case SYS_TRUST_INVALID:
            *ctc_qos_trust = CTC_QOS_TRUST_MAX;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


/*get xqmac or cgmac index */
int32
sys_goldengate_common_get_block_mac_index(uint8 lchip, uint8 slice_id, uint8 mac_id, uint8* p_idx)
{
    uint16 chip_port = 0;
    sys_datapath_lport_attr_t* p_port = NULL;

    chip_port = sys_goldengate_datapath_get_lport_with_mac(lchip, slice_id, mac_id);
    if (SYS_DATAPATH_USELESS_MAC == chip_port)
    {
        return CTC_E_MAC_NOT_USED;
    }

    p_port = sys_goldengate_datapath_get_port_capability(lchip, chip_port, slice_id);
    if (NULL == p_port)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if (p_port->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if (p_port->speed_mode <= CTC_PORT_SPEED_40G)
    {
        if (mac_id <= 39)
        {
            *p_idx = mac_id/4 + 12*slice_id;
        }
        else if (mac_id <= 51)
        {
            *p_idx = 10 + 12*slice_id;
        }
        else if (mac_id <= 55)
        {
            *p_idx = 11 + 12*slice_id;
        }
    }
    else if (p_port->speed_mode <= CTC_PORT_SPEED_100G)
    {
        *p_idx = ((mac_id==36)?0:1) + 2*slice_id;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_common_init_unknown_mcast_tocpu(uint8 lchip, uint32* iloop_nhid)
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

    sys_goldengate_get_gchip_id(lchip, &gchip);
    port_assign.gchip = gchip;
    port_assign.type = CTC_INTERNAL_PORT_TYPE_ILOOP;
    CTC_ERROR_RETURN(sys_goldengate_internal_port_allocate(lchip, &port_assign));

    gport = CTC_MAP_LPORT_TO_GPORT(gchip, port_assign.inter_port);

    CTC_ERROR_RETURN(sys_goldengate_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_EN, CTC_INGRESS, 1));
    CTC_ERROR_RETURN(sys_goldengate_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_USE_CLASSID, CTC_INGRESS, 1));
    CTC_ERROR_RETURN(sys_goldengate_port_set_direction_property(lchip, gport, CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_0, CTC_INGRESS, CTC_ACL_TCAM_LKUP_TYPE_L2));

    group.type = CTC_ACL_GROUP_TYPE_PORT;
    group.lchip = lchip;
    group.dir = CTC_INGRESS;
    group.un.gport = gport;
    CTC_ERROR_RETURN(sys_goldengate_acl_create_group(lchip, CTC_ACL_GROUP_ID_NORMAL, &group));

    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID;
    acl_entry.key.type = CTC_ACL_KEY_MAC;
    acl_entry.key.u.mac_key.mac_da[0] = 0;
    acl_entry.key.u.mac_key.mac_da_mask[0] = 1;
    CTC_SET_FLAG(acl_entry.key.u.mac_key.flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD;
    CTC_ERROR_RETURN(sys_goldengate_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    sal_memset(&acl_entry, 0, sizeof(acl_entry));
    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID - 1;
    acl_entry.key.type = CTC_ACL_KEY_MAC;
    sal_memset(acl_entry.key.u.mac_key.mac_da, 0xFF, CTC_ETH_ADDR_LEN);
    sal_memset(acl_entry.key.u.mac_key.mac_da_mask, 0xFF, CTC_ETH_ADDR_LEN);
    CTC_SET_FLAG(acl_entry.key.u.mac_key.flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD;
    CTC_ERROR_RETURN(sys_goldengate_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    sal_memset(&acl_entry, 0, sizeof(acl_entry));
    acl_entry.entry_id = CTC_ACL_MAX_USER_ENTRY_ID - 2;
    acl_entry.key.type = CTC_ACL_KEY_MAC;
    acl_entry.key.u.mac_key.mac_da[0] = 1;
    acl_entry.key.u.mac_key.mac_da_mask[0] = 1;

    CTC_SET_FLAG(acl_entry.key.u.mac_key.flag, CTC_ACL_MAC_KEY_FLAG_MAC_DA);
    acl_entry.action.flag = CTC_ACL_ACTION_FLAG_DISCARD | CTC_ACL_ACTION_FLAG_COPY_TO_CPU;
    CTC_ERROR_RETURN(sys_goldengate_acl_add_entry(lchip, CTC_ACL_GROUP_ID_NORMAL, &acl_entry));

    CTC_ERROR_RETURN(sys_goldengate_acl_install_group(lchip, CTC_ACL_GROUP_ID_NORMAL, NULL));

    nh_param.lpbk_lport = port_assign.inter_port;
    CTC_ERROR_RETURN(sys_goldengate_nh_get_max_external_nhid(lchip, &nhid));
    CTC_ERROR_RETURN(sys_goldengate_iloop_nh_create(lchip, nhid - 1, &nh_param));

    *iloop_nhid = nhid - 1;

    return CTC_E_NONE;

}

/* value = division << shift_bits,is_negative = 0,the computed value is slightly larger*/
int32
sys_goldengate_common_get_compress_near_division_value(uint8 lchip, uint32 value,uint8 division_wide,
                                                                        uint8 shift_bit_wide, uint16* division, uint16* shift_bits, uint8 is_negative)
{
    uint8 i = 0;
    uint32 valuetmp = 0;
    uint32 base = 0;
    uint16 max_division = 0;
    uint16 max_shift_bit = 0;

    CTC_PTR_VALID_CHECK(division);
    CTC_PTR_VALID_CHECK(shift_bits);
    max_division = 1 << division_wide;
    max_shift_bit = 1 << shift_bit_wide;

    for (i = 0; i < max_shift_bit; i++)
    {
        base = (value >> i);

        if (base < max_division)
        {
            *division = base;
            *shift_bits = i;
            valuetmp = base << i;
            if (valuetmp != value)
            {
                break;
            }
            return CTC_E_NONE;
        }
    }

    valuetmp = value / max_division;
    if(0 == valuetmp)
    {
        *division = value;
        *shift_bits = 0;
        return CTC_E_NONE;
    }

    for (i = 1; i < max_shift_bit; i++)
    {
        if ((valuetmp >= (1 << (i - 1))) && (valuetmp <= ((1 << i)- 1)))
        {
            if(0 == is_negative)
            {
                if ((value + ((1 << i) - 1)) / (1 << i) >= max_division)
                {
                    i++;
                }

                *shift_bits = i;
                *division = (value + ((1 << i) - 1)) / (1 << i);
            }
            else
            {
                if (value  / (1 << i) >= max_division)
                {
                    i++;
                }

                *shift_bits = i;
                *division = value / (1 << i);
            }
            return CTC_E_NONE;
        }
    }

    return CTC_E_INVALID_PARAM;

}



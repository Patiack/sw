/**
 @file sys_greatbelt_nexthop_ip_tunnel.c

 @date 2011-05-23

 @version v2.0

 The file contains all nexthop ip tunnel related callback function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_linklist.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_stats.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
struct sys_nh_ip_tunnel_sa_v4_node_s
{
    uint32  count;
    ip_addr_t ipv4;
};
typedef struct sys_nh_ip_tunnel_sa_v4_node_s sys_nh_ip_tunnel_sa_v4_node_t;

struct sys_nh_ip_tunnel_sa_v6_node_s
{
    uint32  count;
    ipv6_addr_t ipv6;
};
typedef struct sys_nh_ip_tunnel_sa_v6_node_s sys_nh_ip_tunnel_sa_v6_node_t;

struct sys_nh_ip_tunnel_master_s
{
    void* ipsa_bmp [MAX_CTC_IP_VER];
};
typedef struct sys_nh_ip_tunnel_master_s sys_nh_ip_tunnel_master_t;

sys_nh_ip_tunnel_master_t* p_gb_nh_ip_tunnel_master[CTC_MAX_LOCAL_CHIP_NUM]={NULL};

#define SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE 41
#define SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE 4

#define SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM  8
#define SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM  8
#define SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM   0xFF

#define INTERCEPT_IPV6_ADDR(ip, len)                    \
{                                                       \
    uint8 sublen = len % 32;                            \
    uint8 index = len / 32;                              \
    if (sublen)                                         \
    {                                                   \
        (ip)[index] >>= 32 - sublen;                    \
        (ip)[index] <<= 32 - sublen;                    \
        index += 1;                                     \
    }                                                   \
    for (; index < 4; index++)                          \
    {                                                   \
        (ip)[index] = 0;                                \
    }                                                   \
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(uint8 lchip, uint8 ip_ver, void* p_add, uint8* p_index)
{
    uint8 idx = 0;
    uint8 max_idx = 0;
    sys_nh_ip_tunnel_sa_v4_node_t* p_node = NULL;
    sys_nh_ip_tunnel_sa_v4_node_t* p_free = NULL;
    sys_nh_ip_tunnel_sa_v6_node_t* p_v6_node = NULL;
    uint8 free_idx = SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM;
    int32 match = 1;
    uint32 cmd;
    ds_l3_tunnel_v4_ip_sa_t v4_sa = {0};
    ds_l3_tunnel_v6_ip_sa_t v6_sa = {0};
    void* value;
    uint32 tbl_id;

    max_idx = (ip_ver == CTC_IP_VER_4) ? SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM
        : SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM;
    p_node = (sys_nh_ip_tunnel_sa_v4_node_t*)p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4];
    p_v6_node = (sys_nh_ip_tunnel_sa_v6_node_t*)p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6];
    *p_index = SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM;

    p_node = (CTC_IP_VER_4 == ip_ver) ? p_node : (sys_nh_ip_tunnel_sa_v4_node_t*)p_v6_node;

    for (idx = 0; idx < max_idx ; idx++)
    {
        if ((p_node->count == 0) && (free_idx == SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM))
        {
            free_idx = idx ;
            p_free = p_node;
        }
        else if (p_node->count != 0)
        {
            match = sal_memcmp(&(p_node->ipv4), p_add,
                               ((CTC_IP_VER_4 == ip_ver) ? sizeof(ip_addr_t) : sizeof(ipv6_addr_t)));
        }

        if (0 == match)
        {
            *p_index = idx;
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Use old SA INDEX = %d old count is %d \n", *p_index, p_node->count);
            p_node->count++;
            return CTC_E_NONE;
        }
        if (CTC_IP_VER_4 == ip_ver)
        {
            p_node++;
        }
        else
        {
            p_v6_node++;
            p_node = (sys_nh_ip_tunnel_sa_v4_node_t*)p_v6_node;
        }

    }

    if (SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM != free_idx)
    {
        *p_index = free_idx;
        p_free->count++;
        sal_memcpy(&(p_free->ipv4), p_add, ((CTC_IP_VER_4 == ip_ver) ? sizeof(ip_addr_t) : sizeof(ipv6_addr_t)));
    }
    else
    {
        return CTC_E_NO_RESOURCE;
    }

    value = (CTC_IP_VER_4 == ip_ver) ? (void*)&v4_sa : (void*)&v6_sa;
    tbl_id = (CTC_IP_VER_4 == ip_ver) ? DsL3TunnelV4IpSa_t : DsL3TunnelV6IpSa_t;
    sal_memcpy(value, p_add, ((CTC_IP_VER_4 == ip_ver) ? sizeof(ip_addr_t) : sizeof(ipv6_addr_t)));
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, free_idx, cmd, value));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add new SA INDEX = %d \n", free_idx);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(uint8 lchip, uint8 ip_ver, uint8 index)
{
    uint8 idx = 0;
    uint8 max_idx = 0;
    sys_nh_ip_tunnel_sa_v4_node_t* p_node;
    sys_nh_ip_tunnel_sa_v6_node_t* p_v6_node;

    if (index == SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM)
    {
        return CTC_E_NONE;
    }

    if (((ip_ver == CTC_IP_VER_4) && index > SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM)
        || ((ip_ver == CTC_IP_VER_6) && index > SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM))
    {
        return CTC_E_INVALID_PARAM;
    }

    max_idx = (ip_ver == CTC_IP_VER_4) ? SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM
        : SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM;
    p_node = (sys_nh_ip_tunnel_sa_v4_node_t*)p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4];
    p_v6_node = (sys_nh_ip_tunnel_sa_v6_node_t*)p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6];

    for (idx = 0; idx <  max_idx; idx++)
    {
        if (idx  == index)
        {
            break;
        }

        if (CTC_IP_VER_4 == ip_ver)
        {
            p_node++;
        }
        else
        {
            p_v6_node++;
        }
    }

    p_node = (CTC_IP_VER_4 == ip_ver) ? p_node : (sys_nh_ip_tunnel_sa_v4_node_t*)p_v6_node;

    if (0 == p_node->count)
    {
        return CTC_E_NONE;
    }

    p_node->count--;
    if (0 == p_node->count)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free SA INDEX = %d \n", index);
        sal_memset(&(p_node->ipv4), 0, ((ip_ver == CTC_IP_VER_4) ? sizeof(ip_addr_t) : sizeof(ipv6_addr_t)));
        return CTC_E_NONE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free SA INDEX = %d counter is %d \n", index, p_node->count);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_v4(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                              sys_nh_info_ip_tunnel_t* p_edit_db,
                                              sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_tunnel_v4_t  dsl3edit4w;
    uint32 ttl_index = 0;
    uint8 sa_index = SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM;
    int32 ret = CTC_E_NONE;

    sal_memset(&dsl3edit4w, 0, sizeof(ds_l3_edit_tunnel_v4_t));
    dsl3edit4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_TUNNEL_V4;
    dsl3edit4w.ds_type   = SYS_NH_DS_TYPE_L3EDIT;
    dsl3edit4w.ip_identification_type = 0;
    /*ttl*/
    CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param->tunnel_info.ttl, &ttl_index));
    dsl3edit4w.ttl_index = ttl_index & 0xF;
    dsl3edit4w.map_ttl = CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag,
                                        CTC_IP_NH_TUNNEL_FLAG_MAP_TTL);

    /*stats*/
    if (p_nh_param->tunnel_info.stats_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr(lchip,
                                                          p_nh_param->tunnel_info.stats_id,
                                                          &dsnh->stats_ptr));
    }

    if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_COPY_DONT_FRAG))
    {
        dsl3edit4w.copy_dont_frag = 1;
        dsl3edit4w.dont_frag  = 0;
    }
    else
    {
        /*dont frag*/
        dsl3edit4w.copy_dont_frag = 0;
        dsl3edit4w.dont_frag = CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag,
                                              CTC_IP_NH_TUNNEL_FLAG_DONT_FRAG);
    }

    /* dscp */
    if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_ASSIGN)
    {
        dsl3edit4w.derive_dscp = FALSE;
        dsl3edit4w.dscp = p_nh_param->tunnel_info.dscp_or_tos;
    }
    else if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_MAP)
    {
        dsl3edit4w.derive_dscp = TRUE;
        dsl3edit4w.dscp = 1 << 0;
    }
    else if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_PACKET)
    {
        dsl3edit4w.derive_dscp = TRUE;
        dsl3edit4w.dscp = 0 << 0;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /* mtu check */
    if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_MTU_CHECK))
    {
        if (p_nh_param->tunnel_info.mtu_size > 0x3FFF)
        {
            return CTC_E_INVALID_PARAM;
        }

        dsl3edit4w.mtu_check_en = 1;
        dsl3edit4w.ipv4_sa_index |= (p_nh_param->tunnel_info.mtu_size & 0x3FFF) << 16;
    }

    switch (p_nh_param->tunnel_info.tunnel_type)
    {
    case CTC_TUNNEL_TYPE_IPV4_IN4:
        /* ip protocol */
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_IP_BFD ))
        {
            dsl3edit4w.ip_protocol_type = 17; /*UDP*/

        }
        else if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_VCCV_PW_BFD ))
        {
            dsl3edit4w.ip_protocol_type = 17; /*UDP*/
            dsl3edit4w.inner_header_valid = 1;
            dsl3edit4w.inner_header_type = 2;
            dsl3edit4w.gre_key = 3784;
            dsl3edit4w.gre_protocol13_0 = 16000;
        }
        else
        {
            dsl3edit4w.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE;
        }
        dsl3edit4w.ip_da = p_nh_param->tunnel_info.ip_da.ipv4;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;

        break;

    case CTC_TUNNEL_TYPE_IPV6_IN4:
        dsl3edit4w.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE;
        dsl3edit4w.ip_da = p_nh_param->tunnel_info.ip_da.ipv4;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;

        break;

    case CTC_TUNNEL_TYPE_GRE_IN4:
        dsl3edit4w.inner_header_valid   = 1;
        dsl3edit4w.gre_protocol13_0     = p_nh_param->tunnel_info.gre_info.protocol_type & 0x3FFF;
        dsl3edit4w.gre_protocol15_14    = (p_nh_param->tunnel_info.gre_info.protocol_type >> 14) & 0x3;
        dsl3edit4w.gre_version = p_nh_param->tunnel_info.gre_info.gre_ver & 0x1;
        if (p_nh_param->tunnel_info.gre_info.protocol_type == 0x6558)
        {
            dsnh->flag |= SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE;
        }

        /* gre with key */
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_MTU_CHECK)
                && (p_nh_param->tunnel_info.gre_info.gre_key > 0xFFFF))
            {
                return CTC_E_INVALID_PARAM;
            }

            dsl3edit4w.inner_header_type = 0x1;
            dsl3edit4w.gre_flags = 0x2;
            dsl3edit4w.gre_key = p_nh_param->tunnel_info.gre_info.gre_key & 0xFFFF;
            dsl3edit4w.ipv4_sa_index |= ((p_nh_param->tunnel_info.gre_info.gre_key >> 16) & 0xFFFF) << 16;
            CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                        &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
            dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;
        }
        else /* gre without key */
        {
            dsl3edit4w.inner_header_type = 0x0;
            CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                        &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
            dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;
        }

        dsl3edit4w.ip_da = p_nh_param->tunnel_info.ip_da.ipv4;

        break;

    case CTC_TUNNEL_TYPE_ISATAP:
        dsl3edit4w.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE;
        dsl3edit4w.isatp_tunnel = 1;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;
        break;

    case CTC_TUNNEL_TYPE_6TO4:
        dsl3edit4w.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE;
        dsl3edit4w.tunnel6_to4_da = 1;
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_6TO4_USE_MANUAL_IPSA))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                        &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
            dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;
        }
        else
        {
            dsl3edit4w.tunnel6_to4_sa = 1;
        }

        break;

    case CTC_TUNNEL_TYPE_6RD:
        if (p_nh_param->tunnel_info.sixrd_info.sp_prefix_len >= MAX_CTC_NH_SP_PREFIX_LENGTH)
        {
            return CTC_E_INVALID_PARAM;
        }

        if ((p_nh_param->tunnel_info.sixrd_info.v4_da_masklen % 8) || (p_nh_param->tunnel_info.sixrd_info.v4_sa_masklen % 8))
        {
            return CTC_E_NOT_SUPPORT;
        }

        dsl3edit4w.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE;

        /* ipda */
        dsl3edit4w.ip_da = p_nh_param->tunnel_info.ip_da.ipv4;
        if (p_nh_param->tunnel_info.sixrd_info.v4_da_masklen < 32)
        {
            dsl3edit4w.gre_key |= (p_nh_param->tunnel_info.sixrd_info.v4_da_masklen & 0x1F) << 8;
            dsl3edit4w.tunnel6_to4_da = 1;
            dsl3edit4w.gre_key |= (p_nh_param->tunnel_info.sixrd_info.sp_prefix_len & 0x7) << 13;
        }
        else if (p_nh_param->tunnel_info.sixrd_info.v4_da_masklen == 32)
        {
            dsl3edit4w.tunnel6_to4_da = 0;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }

        /* ipsa */
        if (p_nh_param->tunnel_info.sixrd_info.v4_sa_masklen < 32)
        {
            dsl3edit4w.tunnel6_to4_sa = 1;
            dsl3edit4w.gre_key |= p_nh_param->tunnel_info.sixrd_info.v4_sa_masklen & 0x1F;
            dsl3edit4w.ipv4_sa_index |= p_nh_param->tunnel_info.ip_sa.ipv4 & 0xFFFF;
            dsl3edit4w.gre_protocol13_0 = (p_nh_param->tunnel_info.ip_sa.ipv4 >> 16) & 0x3FFF;
            dsl3edit4w.gre_protocol15_14 = (p_nh_param->tunnel_info.ip_sa.ipv4 >> 30) & 0x3;
            dsl3edit4w.gre_key |= (p_nh_param->tunnel_info.sixrd_info.sp_prefix_len & 0x7) << 5;
        }
        else if (p_nh_param->tunnel_info.sixrd_info.v4_sa_masklen == 32)
        {
            dsl3edit4w.tunnel6_to4_sa = 0;
            CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_4,
                                                                        &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
            dsl3edit4w.ipv4_sa_index |= sa_index & 0x1F;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    /*1. Allocate new dsl3edit tunnel entry*/
    ret = sys_greatbelt_nh_offset_alloc(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4, 1,
                                        &dsnh->l3edit_ptr);
    if (ret)
    {
        _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_4, sa_index);
        return ret;
    }

    /*2. Write HW table*/
    ret = sys_greatbelt_nh_write_asic_table(lchip,
                                            SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4,
                                            dsnh->l3edit_ptr, &dsl3edit4w);
    if (ret)
    {
        _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_4, sa_index);
        sys_greatbelt_nh_offset_free(lchip,
                                     SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4, 1,
                                     dsnh->l3edit_ptr);
        return ret;
    }

    p_edit_db->dsl3edit_offset = dsnh->l3edit_ptr;
    p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IN_V4;
    p_edit_db->sa_index = sa_index;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_v6(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                              sys_nh_info_ip_tunnel_t* p_edit_db,
                                              sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_tunnel_v6_t  dsl3editv6;
    uint32 ttl_index = 0;
    uint8 sa_index = SYS_NH_IP_TUNNEL_INVALID_IPSA_NUM;
    int32 ret = CTC_E_NONE;

    sal_memset(&dsl3editv6, 0, sizeof(ds_l3_edit_tunnel_v6_t));
    dsl3editv6.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_TUNNEL_V6;
    dsl3editv6.ds_type   = SYS_NH_DS_TYPE_L3EDIT;

    /*ttl*/
    CTC_ERROR_RETURN(sys_greatbelt_lkup_ttl_index(lchip, p_nh_param->tunnel_info.ttl, &ttl_index));
    dsl3editv6.ttl_index = ttl_index & 0xF;
    dsl3editv6.map_ttl = CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag,
                                        CTC_IP_NH_TUNNEL_FLAG_MAP_TTL);

    /*stats*/
    if (p_nh_param->tunnel_info.stats_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stats_get_statsptr(lchip,
                                                          p_nh_param->tunnel_info.stats_id,
                                                          &dsnh->stats_ptr));
    }

    if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_ASSIGN)
    {
        dsl3editv6.derive_dscp = FALSE;
        dsl3editv6.tos = p_nh_param->tunnel_info.dscp_or_tos;
    }
    else if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_MAP)
    {
        dsl3editv6.derive_dscp = TRUE;
        dsl3editv6.tos = 1 << 0;
    }
    else if (p_nh_param->tunnel_info.dscp_select == CTC_NH_DSCP_SELECT_PACKET)
    {
        dsl3editv6.derive_dscp = TRUE;
        dsl3editv6.tos = 0 << 0;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_nh_param->tunnel_info.flow_label_mode == CTC_NH_FLOW_LABEL_NONE)
    {
        dsl3editv6.new_flow_label_valid = 0x0;
    }
    else if (p_nh_param->tunnel_info.flow_label_mode == CTC_NH_FLOW_LABEL_WITH_HASH ||
             p_nh_param->tunnel_info.flow_label_mode == CTC_NH_FLOW_LABEL_ASSIGN)
    {
        if (p_nh_param->tunnel_info.flow_label > 0xFFFFF)
        {
            return CTC_E_INVALID_PARAM;
        }

        dsl3editv6.new_flow_label_valid = 0x1;
        dsl3editv6.new_flow_label_mode = (p_nh_param->tunnel_info.flow_label_mode == CTC_NH_FLOW_LABEL_WITH_HASH);
        dsl3editv6.flow_label = p_nh_param->tunnel_info.flow_label;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    /* ipda */
    dsl3editv6.ip_da127_126 = (p_nh_param->tunnel_info.ip_da.ipv6[0] >> 30) & 0x3;
    dsl3editv6.ip_da125_123 = (p_nh_param->tunnel_info.ip_da.ipv6[0] >> 27) & 0x7;
    dsl3editv6.ip_da122_122 = (p_nh_param->tunnel_info.ip_da.ipv6[0] >> 26) & 0x1;
    dsl3editv6.ip_da121_114 = (p_nh_param->tunnel_info.ip_da.ipv6[0] >> 18) & 0xFF;
    dsl3editv6.ip_da113_102 = (p_nh_param->tunnel_info.ip_da.ipv6[0] >> 6) & 0xFFF;
    dsl3editv6.ip_da101_90  = ((p_nh_param->tunnel_info.ip_da.ipv6[0] & 0x3F) << 6)
        | ((p_nh_param->tunnel_info.ip_da.ipv6[1] >> 26) & 0x3F);
    dsl3editv6.ip_da89_80   = (p_nh_param->tunnel_info.ip_da.ipv6[1] >> 16) & 0x3FF;

    switch (p_nh_param->tunnel_info.tunnel_type)
    {
    case CTC_TUNNEL_TYPE_IPV4_IN6:
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_IP_BFD ))
        {
            dsl3editv6.ip_protocol_type = 17; /*UDP*/

        }
        else
        {
            dsl3editv6.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV4_PROTO_TYPE;
        }
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_6,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3editv6.ip_sa_index = sa_index & 0x1F;

        break;

    case CTC_TUNNEL_TYPE_IPV6_IN6:
        dsl3editv6.ip_protocol_type = SYS_NH_IP_TUNNEL_IPV6_PROTO_TYPE;
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_6,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3editv6.ip_sa_index = sa_index & 0x1F;

        break;

    case CTC_TUNNEL_TYPE_GRE_IN6:
        dsl3editv6.inner_header_valid   = 1;
        dsl3editv6.gre_protocol13_0     = p_nh_param->tunnel_info.gre_info.protocol_type & 0x3FFF;
        dsl3editv6.gre_protocol15_14    = (p_nh_param->tunnel_info.gre_info.protocol_type >> 14) & 0x3;
        dsl3editv6.ip_protocol_type    |= (p_nh_param->tunnel_info.gre_info.gre_ver & 0x1) << 4;
        if (p_nh_param->tunnel_info.gre_info.protocol_type == 0x6558)
        {
            dsnh->flag |= SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE;
        }

        /* gre with key */
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            dsl3editv6.inner_header_type = 0x1;
            dsl3editv6.ip_protocol_type |= 0x2;
            dsl3editv6.gre_key15_0 = p_nh_param->tunnel_info.gre_info.gre_key & 0xFFFF;
            dsl3editv6.gre_key31_16 = (p_nh_param->tunnel_info.gre_info.gre_key >> 16) & 0xFFFF;
        }
        else /* gre without key */
        {
            dsl3editv6.inner_header_type = 0x0;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_alloc_ipsa_idx(lchip, CTC_IP_VER_6,
                                                                    &(p_nh_param->tunnel_info.ip_sa.ipv4), &sa_index));
        dsl3editv6.ip_sa_index = sa_index & 0x1F;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    /*1. Allocate new dsl3edit tunnel entry*/
    ret = sys_greatbelt_nh_offset_alloc(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6, 1,
                                        &dsnh->l3edit_ptr);
    if (ret)
    {
        _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_6, sa_index);
        return ret;
    }

    /*2. Write HW table*/
    ret = sys_greatbelt_nh_write_asic_table(lchip,
                                            SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6,
                                            dsnh->l3edit_ptr, &dsl3editv6);
    if (ret)
    {

        _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_6, sa_index);
        sys_greatbelt_nh_offset_free(lchip,
                                     SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6, 1,
                                     dsnh->l3edit_ptr);
        return ret;
    }

    p_edit_db->dsl3edit_offset = dsnh->l3edit_ptr;
    p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IN_V6;
    p_edit_db->sa_index = sa_index;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_nat_v4(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                              sys_nh_info_ip_tunnel_t* p_edit_db,
                                              sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_nat4_w_t  dsl3edit4w;

    sal_memset(&dsl3edit4w, 0, sizeof(dsl3edit4w));

    dsl3edit4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_NAT_4W;
    dsl3edit4w.ds_type = SYS_NH_DS_TYPE_L3EDIT;
    dsl3edit4w.nat_mode = 1;
    dsl3edit4w.ip_da29_0 = p_nh_param->tunnel_info.ip_da.ipv4 & 0x3FFFFFFF;
    dsl3edit4w.ip_da31_30 = p_nh_param->tunnel_info.ip_da.ipv4 >> 30;
    dsl3edit4w.replace_ip_da = 1;

    /* for NAPT */
    if (p_nh_param->tunnel_info.l4_dst_port)
    {
        dsl3edit4w.replace_l4_dest_port = 1;
        dsl3edit4w.l4_dest_port = p_nh_param->tunnel_info.l4_dst_port;
    }

    /*1. Allocate new dsl3edit tunnel entry*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, 1,
                                        &dsnh->l3edit_ptr));

    /*2. Write HW table*/
    sys_greatbelt_nh_write_asic_table(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W,
                                        dsnh->l3edit_ptr, &dsl3edit4w);

    p_edit_db->dsl3edit_offset = dsnh->l3edit_ptr;
    p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_NAT_V4;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_ivi_6to4(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                              sys_nh_info_ip_tunnel_t* p_edit_db,
                                              sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_nat4_w_t  dsl3edit4w;

    CTC_MAX_VALUE_CHECK(p_nh_param->tunnel_info.ivi_info.prefix_len, (MAX_CTC_NH_IVI_PREFIX_LEN-1));
    CTC_MAX_VALUE_CHECK(p_nh_param->tunnel_info.ivi_info.is_rfc6052, 1);

    sal_memset(&dsl3edit4w, 0, sizeof(dsl3edit4w));

    dsl3edit4w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_NAT_4W;
    dsl3edit4w.ds_type = SYS_NH_DS_TYPE_L3EDIT;
    dsl3edit4w.nat_mode = 1;
    dsl3edit4w.ip_da_prefix_length1_0 = p_nh_param->tunnel_info.ivi_info.prefix_len & 0x3;
    dsl3edit4w.ip_da_prefix_length2_2 = (p_nh_param->tunnel_info.ivi_info.prefix_len >> 2) & 0x1;
    dsl3edit4w.ipv4_embeded_mode = p_nh_param->tunnel_info.ivi_info.is_rfc6052;

    /*1. Allocate new dsl3edit tunnel entry*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W, 1,
                                        &dsnh->l3edit_ptr));

    /*2. Write HW table*/
    sys_greatbelt_nh_write_asic_table(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W,
                                        dsnh->l3edit_ptr, &dsl3edit4w);

    p_edit_db->dsl3edit_offset = dsnh->l3edit_ptr;
    p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_ivi_4to6(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                              sys_nh_info_ip_tunnel_t* p_edit_db,
                                              sys_nh_param_dsnh_t* dsnh)
{
    ds_l3_edit_nat8_w_t  dsl3edit8w;
    uint8 prefix_len[8] = {32, 40, 48, 56, 64, 72, 80, 96};

    CTC_MAX_VALUE_CHECK(p_nh_param->tunnel_info.ivi_info.prefix_len, (MAX_CTC_NH_IVI_PREFIX_LEN-1));
    CTC_MAX_VALUE_CHECK(p_nh_param->tunnel_info.ivi_info.is_rfc6052, 1);

    sal_memset(&dsl3edit8w, 0, sizeof(dsl3edit8w));
    INTERCEPT_IPV6_ADDR(p_nh_param->tunnel_info.ivi_info.ipv6, prefix_len[p_nh_param->tunnel_info.ivi_info.prefix_len]);

    dsl3edit8w.l3_rewrite_type = SYS_NH_L3EDIT_TYPE_NAT_8W;
    dsl3edit8w.ds_type = SYS_NH_DS_TYPE_L3EDIT;
    dsl3edit8w.nat_mode = 1;
    dsl3edit8w.ip_da_prefix_length1_0 = p_nh_param->tunnel_info.ivi_info.prefix_len & 0x3;
    dsl3edit8w.ip_da_prefix_length2_2 = (p_nh_param->tunnel_info.ivi_info.prefix_len >> 2) & 0x1;
    dsl3edit8w.ipv4_embeded_mode = p_nh_param->tunnel_info.ivi_info.is_rfc6052;
    dsl3edit8w.ip_da127_124 = p_nh_param->tunnel_info.ivi_info.ipv6[0] >> 28;
    dsl3edit8w.ip_da123_120 = (p_nh_param->tunnel_info.ivi_info.ipv6[0] >> 24) & 0xF;
    dsl3edit8w.ip_da119_96 = p_nh_param->tunnel_info.ivi_info.ipv6[0] & 0xFFFFFF;
    dsl3edit8w.ip_da95_64 = p_nh_param->tunnel_info.ivi_info.ipv6[1];
    dsl3edit8w.ip_da63_49 = p_nh_param->tunnel_info.ivi_info.ipv6[2] >> 17;
    dsl3edit8w.ip_da48 = (p_nh_param->tunnel_info.ivi_info.ipv6[2] >> 16) & 0x1;
    dsl3edit8w.ip_da47_40 = (p_nh_param->tunnel_info.ivi_info.ipv6[2] >> 8) & 0xFF;
    dsl3edit8w.ip_da39_32 = p_nh_param->tunnel_info.ivi_info.ipv6[2] & 0xFF;
    dsl3edit8w.ip_da31_30 = p_nh_param->tunnel_info.ivi_info.ipv6[3] >> 30;
    dsl3edit8w.ip_da29_0 = p_nh_param->tunnel_info.ivi_info.ipv6[3] & 0x3FFFFFFF;

    /*1. Allocate new dsl3edit tunnel entry*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_alloc(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W, 1,
                                        &dsnh->l3edit_ptr));

    /*2. Write HW table*/
    sys_greatbelt_nh_write_asic_table(lchip,
                                        SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W,
                                        dsnh->l3edit_ptr, &dsl3edit8w);

    p_edit_db->dsl3edit_offset = dsnh->l3edit_ptr;
    p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl3edit(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                           sys_nh_info_ip_tunnel_t* p_edit_db,
                                           sys_nh_param_dsnh_t* dsnh)
{

    switch (p_nh_param->tunnel_info.tunnel_type)
    {
    case CTC_TUNNEL_TYPE_IPV4_IN4:
    case CTC_TUNNEL_TYPE_IPV6_IN4:
    case CTC_TUNNEL_TYPE_GRE_IN4:
    case CTC_TUNNEL_TYPE_ISATAP:
    case CTC_TUNNEL_TYPE_6TO4:
    case CTC_TUNNEL_TYPE_6RD:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_v4(lchip, p_nh_param, p_edit_db, dsnh));
        break;

    case CTC_TUNNEL_TYPE_IPV4_IN6:
    case CTC_TUNNEL_TYPE_IPV6_IN6:
    case CTC_TUNNEL_TYPE_GRE_IN6:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_v6(lchip, p_nh_param, p_edit_db, dsnh));
        break;

    case CTC_TUNNEL_TYPE_IPV4_NAT:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_nat_v4(lchip, p_nh_param, p_edit_db, dsnh));
        break;

    case CTC_TUNNEL_TYPE_IVI_6TO4:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_ivi_6to4(lchip, p_nh_param, p_edit_db, dsnh));
        break;

    case CTC_TUNNEL_TYPE_IVI_4TO6:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsl3edit_ivi_4to6(lchip, p_nh_param, p_edit_db, dsnh));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsl2edit(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_nh_param,
                                           sys_nh_info_ip_tunnel_t* p_edit_db,
                                           sys_nh_param_dsnh_t* dsnh)
{
    sys_nh_db_dsl2editeth8w_t   dsl2edit_8w;
    sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w = &dsl2edit_8w;
    sys_l3if_prop_t l3if_prop;

    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));

    sal_memset(&dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));
    sal_memcpy(p_edit_db->mac, p_nh_param->mac, sizeof(mac_addr_t));
    if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR)
        && (p_nh_param->tunnel_info.tunnel_type != CTC_TUNNEL_TYPE_IPV4_IN6
            && p_nh_param->tunnel_info.tunnel_type != CTC_TUNNEL_TYPE_IPV6_IN6
            && p_nh_param->tunnel_info.tunnel_type != CTC_TUNNEL_TYPE_GRE_IN6)
            &&(0 == p_nh_param->loop_nhid))
    {
        return CTC_E_NONE;
    }

    if(p_nh_param->arp_id)
    {
        sys_nh_db_arp_t *p_arp = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_nh_lkup_arp_id(lchip, p_nh_param->arp_id, &p_arp));
        dsnh->l2edit_ptr = p_arp->offset;
    }
    else if(p_nh_param->loop_nhid)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_add_eloop_edit(lchip, p_nh_param->loop_nhid, TRUE, &dsnh->l2edit_ptr));
        p_edit_db->loop_nhid = p_nh_param->loop_nhid;
        CTC_SET_FLAG(p_edit_db->flag, SYS_NH_IP_TUNNEL_LOOP_NH);
    }
    else
    {
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_NAT_REPLACE_IP)
                && (CTC_TUNNEL_TYPE_IPV4_NAT == p_nh_param->tunnel_info.tunnel_type) )
        {
            mac_addr_t mac_da;
            sal_memset(&mac_da, 0, sizeof(mac_addr_t));

            if (0 == sal_memcmp(p_nh_param->mac, mac_da, sizeof(mac_addr_t)))
            {
                return CTC_E_NONE;
            }
            else
            {
                dsl2edit_8w.packet_type = PKT_TYPE_IPV4;
                p_edit_db->packet_type = PKT_TYPE_IPV4;
            }
        }

        l3if_prop.l3if_id = p_edit_db->l3ifid;
        sys_greatbelt_l3if_get_l3if_info(lchip, 1, &l3if_prop);

        dsl2edit_8w.offset = 0;
        dsl2edit_8w.phy_if = ((CTC_L3IF_TYPE_PHY_IF == l3if_prop.l3if_type) ? 1 :0);
        p_edit_db->phy_if = dsl2edit_8w.phy_if;
        sal_memcpy(dsl2edit_8w.mac_da, p_nh_param->mac, sizeof(mac_addr_t));
        if (p_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_IPV4_IN6
            || p_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_IPV6_IN6
            || p_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_GRE_IN6)
        {
        if (CTC_FLAG_ISSET(p_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR) && !p_nh_param->mac[0] &&
            !p_nh_param->mac[1] && !p_nh_param->mac[2] && !p_nh_param->mac[3] && !p_nh_param->mac[4] && !p_nh_param->mac[5])
            {
                dsl2edit_8w.mac_da[1] = 0x55;
                dsl2edit_8w.mac_da[2] = 0xaa;
                dsl2edit_8w.mac_da[3] = 0x55;
                dsl2edit_8w.mac_da[4] = 0xaa;
            }

            dsl2edit_8w.ip0_31  = p_nh_param->tunnel_info.ip_da.ipv6[3];
            dsl2edit_8w.ip32_63 = p_nh_param->tunnel_info.ip_da.ipv6[2];
            dsl2edit_8w.ip64_79 = p_nh_param->tunnel_info.ip_da.ipv6[1] & 0xFFFF;

            p_edit_db->ip0_31  = p_nh_param->tunnel_info.ip_da.ipv6[3];
            p_edit_db->ip32_63 = p_nh_param->tunnel_info.ip_da.ipv6[2];
            p_edit_db->ip64_79 = p_nh_param->tunnel_info.ip_da.ipv6[1] & 0xFFFF;
            CTC_ERROR_RETURN(sys_greatbelt_nh_add_route_l2edit_8w(lchip, &p_dsl2edit_8w));
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_L2EDIT8W;
            dsnh->l2edit_ptr = p_dsl2edit_8w->offset;
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_add_route_l2edit(lchip, (sys_nh_db_dsl2editeth4w_t**)&p_dsl2edit_8w));
            dsnh->l2edit_ptr = p_dsl2edit_8w->offset;
        }
    }


    return CTC_E_NONE;
}

int32
_sys_greatbelt_nh_free_ip_tunnel_resource(uint8 lchip, sys_nh_info_ip_tunnel_t* p_nhinfo)
{
    uint8 l3edit_type = 0;
    sys_nh_db_dsl2editeth8w_t  dsl2edit_8w;

    sal_memset(&dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));
    sal_memcpy(dsl2edit_8w.mac_da, p_nhinfo->mac, sizeof(mac_addr_t));
    dsl2edit_8w.ip0_31  = p_nhinfo->ip0_31;
    dsl2edit_8w.ip32_63 = p_nhinfo->ip32_63;
    dsl2edit_8w.ip64_79 = p_nhinfo->ip64_79;
    dsl2edit_8w.packet_type = p_nhinfo->packet_type;
    dsl2edit_8w.phy_if = p_nhinfo->phy_if;

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V4)
        {
            l3edit_type = SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4;
        }
        else if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V6)
        {
            l3edit_type = SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6;
        }
        else if ((p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4)
            || (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_NAT_V4))
        {
            l3edit_type = SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W;
        }
        else if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6)
        {
            l3edit_type = SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W;
        }

        sys_greatbelt_nh_offset_free(lchip, \
        l3edit_type, 1, p_nhinfo->dsl3edit_offset);

        if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V4)
        {
            _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_4, p_nhinfo->sa_index);
        }
        else if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_IN_V6)
        {
            _sys_greatbelt_nh_ip_tunnel_free_ipsa_idx(lchip, CTC_IP_VER_6, p_nhinfo->sa_index);
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->flag, SYS_NH_IP_TUNNEL_LOOP_NH))
        {
            sys_greatbelt_nh_remove_eloop_edit(lchip, p_nhinfo->loop_nhid);
        }
        else if (p_nhinfo->flag & SYS_NH_IP_TUNNEL_FLAG_L2EDIT8W)
        {
            sys_greatbelt_nh_remove_route_l2edit_8w(lchip, &dsl2edit_8w);
        }
        else
        {
            sys_greatbelt_nh_remove_route_l2edit(lchip, (sys_nh_db_dsl2editeth4w_t*)&dsl2edit_8w);
        }
    }


    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    return CTC_E_NONE;
}




STATIC int32
_sys_greatbelt_nh_ip_tunnel_build_dsnh(uint8 lchip, ctc_ip_tunnel_nh_param_t* p_ip_nh_param,
                                       sys_nh_info_ip_tunnel_t* p_edit_db)
{

    uint8 gchip = 0;
    sys_nh_param_dsnh_t dsnh_param;
    sys_l3if_prop_t l3if_prop;
    uint16 gport = 0;
    uint8 lport = 0;
    int32 ret = CTC_E_NONE;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_ip_nh_param);
    CTC_PTR_VALID_CHECK(p_edit_db);

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));


    p_edit_db->sa_index = 0xFF;


    /*check*/
    if (CTC_FLAG_ISSET(p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR))
    {
        if(p_ip_nh_param->arp_id)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        if ((p_ip_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_IPV4_IN6
            || p_ip_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_IPV6_IN6
            || p_ip_nh_param->tunnel_info.tunnel_type == CTC_TUNNEL_TYPE_GRE_IN6) && p_ip_nh_param->arp_id)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (!CTC_FLAG_ISSET(p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR))
    {
        gchip = SYS_MAP_GPORT_TO_GCHIP(p_ip_nh_param->oif.gport);
        gport = p_ip_nh_param->oif.gport;
        CTC_ERROR_RETURN(sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(lchip, gport, p_ip_nh_param->oif.vid, &l3if_prop))
        p_edit_db->l3ifid  = l3if_prop.l3if_id;
        p_edit_db->gport = gport;
    }


    /*if need loopback, get egress loopback port*/
    if (CTC_FLAG_ISSET(p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR))
    {
        CTC_ERROR_RETURN(sys_greatbelt_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_IP_TUNNEL, &lport));
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        p_ip_nh_param->oif.gport = lport; /*only use lport (IP tunnel i/e loop port id). gchip_id will write later. */
        p_edit_db->gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        p_ip_nh_param->oif.vid = 0;
        l3if_prop.vlan_ptr = SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT + 0x1000;
        CTC_SET_FLAG(p_edit_db->flag, SYS_NH_IP_TUNNEL_REROUTE);
        if ((CTC_TUNNEL_TYPE_IPV4_NAT == p_ip_nh_param->tunnel_info.tunnel_type)
            || (CTC_TUNNEL_TYPE_IVI_6TO4 == p_ip_nh_param->tunnel_info.tunnel_type)
        || (CTC_TUNNEL_TYPE_IVI_4TO6 == p_ip_nh_param->tunnel_info.tunnel_type))
        {
            dsnh_param.flag |= SYS_NH_PARAM_FLAG_ROUTE_NOTTL;
        }
    }
    else
    {
        CTC_UNSET_FLAG(p_edit_db->flag, SYS_NH_IP_TUNNEL_REROUTE);
    }

    switch (p_ip_nh_param->tunnel_info.tunnel_type)
    {
        case CTC_TUNNEL_TYPE_IPV4_IN4:
        case CTC_TUNNEL_TYPE_IPV6_IN4:
        case CTC_TUNNEL_TYPE_GRE_IN4:
        case CTC_TUNNEL_TYPE_ISATAP:
        case CTC_TUNNEL_TYPE_6TO4:
        case CTC_TUNNEL_TYPE_6RD:
        case CTC_TUNNEL_TYPE_NVGRE_IN4:
        case CTC_TUNNEL_TYPE_VXLAN_IN4:
        case CTC_TUNNEL_TYPE_GENEVE_IN4:
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IN_V4;
            break;

        case CTC_TUNNEL_TYPE_IPV4_IN6:
        case CTC_TUNNEL_TYPE_IPV6_IN6:
        case CTC_TUNNEL_TYPE_GRE_IN6:
        case CTC_TUNNEL_TYPE_NVGRE_IN6:
        case CTC_TUNNEL_TYPE_VXLAN_IN6:
        case CTC_TUNNEL_TYPE_GENEVE_IN6:
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IN_V6;
            break;

        case CTC_TUNNEL_TYPE_IPV4_NAT:
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_NAT_V4;
            break;

        case CTC_TUNNEL_TYPE_IVI_6TO4:
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4;
            break;

        case CTC_TUNNEL_TYPE_IVI_4TO6:
            p_edit_db->flag |= SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(p_edit_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE))
    {
        if (SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;
        }
    }
    dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;
    /*1. Build dsL3Edit*/
    ret = _sys_greatbelt_nh_ip_tunnel_build_dsl3edit(lchip, p_ip_nh_param, p_edit_db, &dsnh_param);
    if (ret)
    {
        goto error_proc;
    }
    if (dsnh_param.l3edit_ptr != 0)
    {
        CTC_SET_FLAG(p_edit_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
    }

    /*2. Build dsL2Edit*/
    ret = _sys_greatbelt_nh_ip_tunnel_build_dsl2edit(lchip, p_ip_nh_param, p_edit_db, &dsnh_param);
    if (ret)
    {
        goto error_proc;
    }

    if ((dsnh_param.l2edit_ptr != 0) && (p_ip_nh_param->arp_id == 0))
    {
        CTC_SET_FLAG(p_edit_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    }

    if (p_ip_nh_param->tunnel_info.flag & CTC_IP_NH_TUNNEL_FLAG_MIRROR)
    {
        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL_FOR_MIRROR;
    }
    else
    {
        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL;
    }

    if ((CTC_TUNNEL_TYPE_IPV4_NAT == p_ip_nh_param->tunnel_info.tunnel_type)
        && (CTC_FLAG_ISSET(p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_NAT_REPLACE_IP)))
    {
        CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE);
    }

    if (CTC_FLAG_ISSET(p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_INNER_TTL_NO_DEC))
    {
        CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL);
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Lchip = %d, Write DsNexthop4w Table\n", lchip);
    dsnh_param.dsnh_offset = p_edit_db->hdr.dsfwd_info.dsnh_offset;
    ret = sys_greatbelt_nh_write_entry_dsnh4w(lchip, &dsnh_param);
    if (ret)
    {
        goto error_proc;
    }

   return CTC_E_NONE;
error_proc:
    _sys_greatbelt_nh_free_ip_tunnel_resource(lchip, p_edit_db);
     return ret;

}
STATIC int32
_sys_greatbelt_nh_ip_tunnel_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;

   sys_nh_info_ip_tunnel_t* p_nh_info  = (sys_nh_info_ip_tunnel_t*)(p_com_db);
   uint8 gchip;
   int ret = 0;

   sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

   dsfwd_param.is_mcast = FALSE;
   dsfwd_param.nexthop_ext = 0;

   if (CTC_FLAG_ISSET(p_nh_info->flag, SYS_NH_IP_TUNNEL_REROUTE))
   {
       sys_greatbelt_get_gchip_id(lchip, &gchip);
       dsfwd_param.dest_chipid = gchip;
   }
   else
   {
       dsfwd_param.dest_chipid = SYS_MAP_GPORT_TO_GCHIP(p_nh_info->gport);
   }
   dsfwd_param.dest_id = CTC_MAP_GPORT_TO_LPORT(p_nh_info->gport);

   /*2.2 Build DsFwd Table*/
   if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
   {
       ret = sys_greatbelt_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                           &(p_nh_info->hdr.dsfwd_info.dsfwd_offset));
       if (ret < 0)
       {

           CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip,
                                                         SYS_NH_ENTRY_TYPE_FWD, 1, p_nh_info->hdr.dsfwd_info.dsfwd_offset));

           return ret;
       }

       SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Lchip = %d, DsFwdOffset = %d\n", lchip,
                      (p_nh_info->hdr.dsfwd_info.dsfwd_offset));
   }

   dsfwd_param.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
   dsfwd_param.dsnh_offset = p_nh_info->hdr.dsfwd_info.dsnh_offset;
   dsfwd_param.bypass_ingress_edit = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

   SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsFwd(Lchip : %d) :: DestChipId = %d, DestId = %d"
   "DsNexthop Offset = %d, DsNexthopExt = %d\n", lchip,
                       dsfwd_param.dest_chipid, dsfwd_param.dest_id,
                       dsfwd_param.dsnh_offset, dsfwd_param.nexthop_ext);
   /*2.3 Write table*/
   sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param);

    CTC_SET_FLAG(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    return CTC_E_NONE;
}
int32
sys_greatbelt_nh_create_ip_tunnel_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db)
{

    sys_nh_param_ip_tunnel_t* p_nh_param = NULL;
    ctc_ip_tunnel_nh_param_t* p_nh_ip_param = NULL;
    sys_nh_info_ip_tunnel_t* p_nhdb = NULL;
    sys_nh_param_special_t nh_para_spec;


    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db)
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IP_TUNNEL, p_com_nh_para->hdr.nh_param_type);


    p_nh_param = (sys_nh_param_ip_tunnel_t*)(p_com_nh_para);
    p_nh_ip_param = p_nh_param->p_ip_nh_param;
    p_nhdb     = (sys_nh_info_ip_tunnel_t*)(p_com_db);
    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_IP_TUNNEL;


    /*1.Create unresolved ip-tunnel nh*/
    if (CTC_FLAG_ISSET(p_nh_ip_param->flag, CTC_IP_NH_FLAG_UNROV))
    {
        sal_memset(&nh_para_spec, 0, sizeof(nh_para_spec));
        nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
        nh_para_spec.hdr.is_internal_nh = TRUE;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create unresolved ip tunnel nexthop\n");

        CTC_ERROR_RETURN(sys_greatbelt_nh_create_special_cb(lchip, (
                                                                sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhdb)));

        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
        return CTC_E_NONE;
    }

    /*2.Create normal ip-tunnel nh*/
    /*2.1 op dsnh,l2edit,l3edit*/
    CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_build_dsnh(lchip, p_nh_ip_param, p_nhdb));

    /*2.Create Dwfwd*/
    if(!p_nh_param->hdr.have_dsfwd)
    {
      return CTC_E_NONE;
    }
     CTC_ERROR_RETURN(_sys_greatbelt_nh_ip_tunnel_add_dsfwd(lchip,  p_com_db));

    return CTC_E_NONE;

}


int32
sys_greatbelt_nh_delete_ip_tunnel_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{

    uint32 entry_type = SYS_NH_TYPE_IP_TUNNEL;
    sys_nh_param_dsfwd_t    dsfwd_param = {0};
    sys_nh_info_ip_tunnel_t* p_nhinfo   = NULL;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    CTC_EQUAL_CHECK(entry_type, p_data->hdr.nh_entry_type);
    p_nhinfo = (sys_nh_info_ip_tunnel_t*)(p_data);
    sys_greatbelt_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. delete all reference ecmp nh*/
    p_ref_node = p_nhinfo->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    /*2. Delete this ip tunnel nexthop*/

    dsfwd_param.dsfwd_offset = p_nhinfo->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.drop_pkt = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    /*Free DsFwd offset*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_offset_free(lchip, \
    SYS_NH_ENTRY_TYPE_FWD, 1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset));


    _sys_greatbelt_nh_free_ip_tunnel_resource(lchip, p_nhinfo);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_update_ip_tunnel_fwd_to_spec(uint8 lchip, sys_nh_param_ip_tunnel_t* p_nhpara,
                                               sys_nh_info_ip_tunnel_t* p_nhinfo)
{

    sys_nh_param_special_t nh_para_spec;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    sal_memset(&nh_para_spec, 0, sizeof(sys_nh_param_special_t));


    nh_para_spec.hdr.have_dsfwd= TRUE;
    nh_para_spec.hdr.nh_param_type = SYS_NH_TYPE_UNROV;
    nh_para_spec.hdr.is_internal_nh = TRUE;

    /*1.2 update dsfwd to unrov nh's dsfwd, write dsfwd*/
    CTC_ERROR_RETURN(sys_greatbelt_nh_create_special_cb(lchip, (
                                                            sys_nh_param_com_t*)(&nh_para_spec), (sys_nh_info_com_t*)(p_nhinfo)));
    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);

    _sys_greatbelt_nh_free_ip_tunnel_resource(lchip, p_nhinfo);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nh_update_ip_tunnel_fwd_attr(uint8 lchip, sys_nh_param_ip_tunnel_t* p_nhpara,
                                            sys_nh_info_ip_tunnel_t* p_nhinfo)
{


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);


    /*Build nhpara*/
    p_nhpara->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                || (NULL != p_nhinfo->hdr.p_ref_nh_list);


    _sys_greatbelt_nh_free_ip_tunnel_resource(lchip, p_nhinfo);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
    CTC_ERROR_RETURN(sys_greatbelt_nh_create_ip_tunnel_cb(lchip, (
                                                              sys_nh_param_com_t*)p_nhpara, (sys_nh_info_com_t*)p_nhinfo));

    return CTC_E_NONE;

}

int32
sys_greatbelt_nh_ip_tunnel_init(uint8 lchip)
{

    if (NULL != p_gb_nh_ip_tunnel_master[lchip])
    {
        return CTC_E_NONE;
    }


    p_gb_nh_ip_tunnel_master[lchip] = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_ip_tunnel_master_t));

    if (NULL == p_gb_nh_ip_tunnel_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_nh_ip_tunnel_master[lchip], 0, sizeof(sys_nh_ip_tunnel_master_t));
    /*IPv4 SA */
    p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4] = mem_malloc(MEM_NEXTHOP_MODULE,
                                                               (SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM) * sizeof(sys_nh_ip_tunnel_sa_v4_node_t));

    /*IPv6 SA */
    p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6] = mem_malloc(MEM_NEXTHOP_MODULE,
                                                               (SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM) * sizeof(sys_nh_ip_tunnel_sa_v6_node_t));
    if ((NULL == p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4])
        || (NULL == p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6]))
    {
        if (NULL != p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4])
        {
            mem_free(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4]);
        }
        if (NULL != p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6])
        {
            mem_free(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6]);
        }
        mem_free(p_gb_nh_ip_tunnel_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4], 0,
               (SYS_NH_IP_TUNNEL_IPV4_MAX_IPSA_NUM) * sizeof(sys_nh_ip_tunnel_sa_v4_node_t));
    sal_memset(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6], 0,
               (SYS_NH_IP_TUNNEL_IPV6_MAX_IPSA_NUM) * sizeof(sys_nh_ip_tunnel_sa_v6_node_t));

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_ip_tunnel_deinit(uint8 lchip)
{
    if (NULL == p_gb_nh_ip_tunnel_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_6]);
    mem_free(p_gb_nh_ip_tunnel_master[lchip]->ipsa_bmp[CTC_IP_VER_4]);
    mem_free(p_gb_nh_ip_tunnel_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_nh_update_ip_tunnel_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                     sys_nh_param_com_t* p_para)
{
    sys_nh_info_ip_tunnel_t* p_nh_info;
    sys_nh_param_ip_tunnel_t* p_nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IP_TUNNEL, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_IP_TUNNEL, p_nh_db->hdr.nh_entry_type);

    p_nh_info = (sys_nh_info_ip_tunnel_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_ip_tunnel_t*)(p_para);

    switch (p_nh_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ip_tunnel_fwd_to_spec(lchip, p_nh_para, p_nh_info));
        break;

    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ip_tunnel_fwd_attr(lchip, p_nh_para, p_nh_info));
        break;

    case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
        if (!CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            return CTC_E_NH_ISNT_UNROV;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_nh_update_ip_tunnel_fwd_attr(lchip, p_nh_para, p_nh_info));
        break;
  case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {
          if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
          {
              return CTC_E_NONE;
          }
          _sys_greatbelt_nh_ip_tunnel_add_dsfwd(lchip,  p_nh_db);
        }
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (p_nh_info->hdr.p_ref_nh_list)
    {
        sys_greatbelt_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type);
    }

    return CTC_E_NONE;
}


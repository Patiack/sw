/**
 @file sys_greatbelt_ipmc.c

 @date 2011-11-30

 @version v2.0

 The file contains all ipmc related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_avl_tree.h"
#include "ctc_hash.h"
#include "ctc_ipmc.h"
#include "ctc_stats.h"

#include "sys_greatbelt_sort.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_l3_hash.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_ipmc.h"
#include "sys_greatbelt_ipmc_db.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_rpf_spool.h"

#include "greatbelt/include/drv_io.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define SYS_IPMC_IP_FORCE_ROUTE_0 0x01
#define SYS_IPMC_IP_FORCE_ROUTE_1 0x02


sys_ipmc_master_t* p_gb_ipmc_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

STATIC int32
_sys_greatbelt_ipmc_write_ipda(uint8 lchip, sys_ipmc_group_node_t* p_group_node, uint32 fwd_offset, sys_rpf_rslt_t* p_rpf_rslt)
{
    uint32 cmd;
    ds_ip_da_t ds_da; /* ds_ipv4_mcast_da_tcam_t is as same as ds_ip_da_t */
    ds_mac_t ds_mac;
    uint8  ip_version;
    uint32 ad_index;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    ip_version = (p_group_node->flag & 0x4) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    ad_index = (p_gb_ipmc_master[lchip]->is_ipmcv4_key160 && (ip_version == CTC_IP_VER_4) && (!p_gb_ipmc_master[lchip]->is_sort_mode)) ? (p_group_node->ad_index / 2) : p_group_node->ad_index;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] _sys_greatbelt_ipmc_write_ipda\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "IPMC: p_group_node->sa_index  %d\r\n", p_group_node->sa_index);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "IPMC: p_group_node->ad_index  %d\r\n", p_group_node->ad_index);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "IPMC: write ad index is %d\r\n", ad_index);

    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        sal_memset(&ds_mac, 0, sizeof(ds_mac_t));
        ds_mac.next_hop_ptr_valid = 0;
        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->da_tcam_l2_table_id[ip_version], DRV_ENTRY_FLAG);




        if (p_group_node->flag & 0x3)
        {
            ds_mac.ds_fwd_ptr = fwd_offset;
            ds_mac.mac_known = 1;
        }
        else
        {
            ds_da.ds_fwd_ptr = fwd_offset;
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_mac));


        return CTC_E_NONE;
    }
    else
    {
        sal_memset(&ds_da, 0, sizeof(ds_ip_da_t));

        if (p_group_node->flag & 0x3)
        {
            CTC_PTR_VALID_CHECK(p_rpf_rslt);


            /* TBD */
            ds_da.ttl_check_en             = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_TTL_CHECK);
            ds_da.ip_da_exception_en       = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU);
            ds_da.self_address            = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_PTP_ENTRY);

            ds_da.next_hop_ptr_valid        = 0;
            ds_da.aging_valid = 1;
            ds_da.ds_fwd_ptr                = 0;
            ds_da.rpf_check_mode            = p_rpf_rslt->mode;
            ds_da.rpf_if_id                 = p_rpf_rslt->id;

            SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPMC: write ipda rpf_check_mode=  %d\r\n", ds_da.rpf_check_mode);
            SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPMC: write ipda rpf_if_id=  %d\r\n", ds_da.rpf_if_id);

        }
        else
        {
            /* default entry */
            CTC_PTR_VALID_CHECK(p_rpf_rslt);

            ds_da.rpf_check_mode           = p_rpf_rslt->mode;
            ds_da.rpf_if_id                = p_rpf_rslt->id;
            ds_da.is_default_route         = TRUE;
            ds_da.ttl_check_en             = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_TTL_CHECK);
            ds_da.self_address             = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_PTP_ENTRY);
            ds_da.ip_da_exception_en       = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU);
            ds_da.exception3_ctl_en = 1;
        }

        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU))
        {
            /*to CPU must be equal to 63(get the low 5bits is 31), refer to sys_greatbelt_pdu.h:SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU*/
            ds_da.exception_sub_index = 63 & 0x1F;
        }

        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->da_tcam_table_id[ip_version], DRV_ENTRY_FLAG);

        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_REDIRECT_TOCPU))
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, &fwd_offset));
        }

        ds_da.ds_fwd_ptr = fwd_offset;

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_da));

        return CTC_E_NONE;
    }
}

STATIC int32
_sys_greatbelt_ipmc_write_ipsa(uint8 lchip, sys_ipmc_group_node_t* p_group_node, sys_ipmc_rpf_info_t* p_sys_ipmc_rpf_info)
{

    uint32 cmd;
    uint8  ip_version;
    ds_rpf_t ds_rpf;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_write_ipsa\n");
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    CTC_PTR_VALID_CHECK(p_sys_ipmc_rpf_info);

    sal_memset(&ds_rpf, 0, sizeof(ds_rpf_t));
    ip_version = (p_group_node->flag & 0x4) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    /* ipv4 and ipv6 share the same data structure */
    ds_rpf.rpf_if_id0  = p_sys_ipmc_rpf_info->rpf_intf[0];
    ds_rpf.rpf_if_id1  = p_sys_ipmc_rpf_info->rpf_intf[1];
    ds_rpf.rpf_if_id2  = p_sys_ipmc_rpf_info->rpf_intf[2];
    ds_rpf.rpf_if_id3  = p_sys_ipmc_rpf_info->rpf_intf[3];

    ds_rpf.rpf_if_id4  = p_sys_ipmc_rpf_info->rpf_intf[4];
    ds_rpf.rpf_if_id5  = p_sys_ipmc_rpf_info->rpf_intf[5];
    ds_rpf.rpf_if_id6  = p_sys_ipmc_rpf_info->rpf_intf[6];
    ds_rpf.rpf_if_id7  = p_sys_ipmc_rpf_info->rpf_intf[7];

    ds_rpf.rpf_if_id8  = p_sys_ipmc_rpf_info->rpf_intf[8];
    ds_rpf.rpf_if_id9  = p_sys_ipmc_rpf_info->rpf_intf[9];
    ds_rpf.rpf_if_id10 = p_sys_ipmc_rpf_info->rpf_intf[10];
    ds_rpf.rpf_if_id11 = p_sys_ipmc_rpf_info->rpf_intf[11];

    ds_rpf.rpf_if_id12 = p_sys_ipmc_rpf_info->rpf_intf[12];
    ds_rpf.rpf_if_id13 = p_sys_ipmc_rpf_info->rpf_intf[13];
    ds_rpf.rpf_if_id14 = p_sys_ipmc_rpf_info->rpf_intf[14];
    ds_rpf.rpf_if_id15 = p_sys_ipmc_rpf_info->rpf_intf[15];

    ds_rpf.rpf_if_id_valid0  = p_sys_ipmc_rpf_info->rpf_intf_valid[0];
    ds_rpf.rpf_if_id_valid1  = p_sys_ipmc_rpf_info->rpf_intf_valid[1];
    ds_rpf.rpf_if_id_valid2  = p_sys_ipmc_rpf_info->rpf_intf_valid[2];
    ds_rpf.rpf_if_id_valid3  = p_sys_ipmc_rpf_info->rpf_intf_valid[3];

    ds_rpf.rpf_if_id_valid4  = p_sys_ipmc_rpf_info->rpf_intf_valid[4];
    ds_rpf.rpf_if_id_valid5  = p_sys_ipmc_rpf_info->rpf_intf_valid[5];
    ds_rpf.rpf_if_id_valid6  = p_sys_ipmc_rpf_info->rpf_intf_valid[6];
    ds_rpf.rpf_if_id_valid7  = p_sys_ipmc_rpf_info->rpf_intf_valid[7];

    ds_rpf.rpf_if_id_valid8  = p_sys_ipmc_rpf_info->rpf_intf_valid[8];
    ds_rpf.rpf_if_id_valid9  = p_sys_ipmc_rpf_info->rpf_intf_valid[9];
    ds_rpf.rpf_if_id_valid10 = p_sys_ipmc_rpf_info->rpf_intf_valid[10];
    ds_rpf.rpf_if_id_valid11 = p_sys_ipmc_rpf_info->rpf_intf_valid[11];

    ds_rpf.rpf_if_id_valid12 = p_sys_ipmc_rpf_info->rpf_intf_valid[12];
    ds_rpf.rpf_if_id_valid13 = p_sys_ipmc_rpf_info->rpf_intf_valid[13];
    ds_rpf.rpf_if_id_valid14 = p_sys_ipmc_rpf_info->rpf_intf_valid[14];
    ds_rpf.rpf_if_id_valid15 = p_sys_ipmc_rpf_info->rpf_intf_valid[15];

    cmd = DRV_IOW(p_gb_ipmc_master[lchip]->sa_table_id[ip_version], DRV_ENTRY_FLAG);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "sa_index is 0x%x \n", p_group_node->sa_index);


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->sa_index, cmd, &ds_rpf));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_ipv4_write_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 cmd;
    uint32 index = 0;
    uint8 is_160key = 0;
    tbl_entry_t tbl_ipkey;
    ds_ipv4_mcast_route_key_t ds_ipv4_route_key, ds_ipv4_route_mask;
    ds_mac_ipv4_key_t ds_mac_key, ds_mac_mask;
    uint8  is_default = 0;
    uint16 max_vrf_num = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    if ((CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160) && (!p_gb_ipmc_master[lchip]->is_sort_mode))
    { /*160 bit key*/
        is_160key = 1;
    }

    if (!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        max_vrf_num = p_gb_ipmc_master[lchip]->is_sort_mode? 0 : p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_4];
        index = p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_4] + max_vrf_num * (is_160key + 1);
        is_default = (p_group_node->ad_index == index) ? 1 : 0;

        sal_memset(&ds_ipv4_route_key, 0, sizeof(ds_ipv4_route_key_t));
        sal_memset(&ds_ipv4_route_mask, 0, sizeof(ds_ipv4_route_key_t));
        tbl_ipkey.data_entry = (uint32*)&ds_ipv4_route_key;
        tbl_ipkey.mask_entry = (uint32*)&ds_ipv4_route_mask;

        ds_ipv4_route_key.ip_da         = p_group_node->address.ipv4.group_addr;
        ds_ipv4_route_key.ip_sa         = p_group_node->address.ipv4.src_addr;
        ds_ipv4_route_key.vrf_id9_0     = p_group_node->address.ipv4.vrfid & 0x3FF;
        ds_ipv4_route_key.vrf_id13_10   = (p_group_node->address.ipv4.vrfid >> 10) & 0xF;
        ds_ipv4_route_key.table_id0     = p_gb_ipmc_master[lchip]->ipv4_router_key_tbl_id;
        ds_ipv4_route_key.table_id1     = p_gb_ipmc_master[lchip]->ipv4_router_key_tbl_id;
        ds_ipv4_route_key.sub_table_id0 = p_gb_ipmc_master[lchip]->ipv4_router_key_sub_tbl_id;
        ds_ipv4_route_key.sub_table_id1 = p_gb_ipmc_master[lchip]->ipv4_router_key_sub_tbl_id;

        ds_ipv4_route_mask.ip_da         = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFFFFFFFF : 0;
        ds_ipv4_route_mask.ip_sa         = (p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 0xFFFFFFFF : 0;
        ds_ipv4_route_mask.vrf_id9_0     = (is_default) ? 0 : ((p_gb_ipmc_master[lchip]->is_ipmcv4_key160) ? 0x3FF : 0);
        ds_ipv4_route_mask.vrf_id13_10   = (is_default) ? 0 : ((p_gb_ipmc_master[lchip]->is_ipmcv4_key160) ? 0xF : 0);
        ds_ipv4_route_mask.table_id0     = 0x7;
        ds_ipv4_route_mask.table_id1     = 0x7;
        ds_ipv4_route_mask.sub_table_id0 = 0x3;
        ds_ipv4_route_mask.sub_table_id1 = 0x3;

        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4], DRV_ENTRY_FLAG);

    }
    else
    {
        sal_memset(&ds_mac_key, 0, sizeof(ds_mac_ipv4_key_t));
        sal_memset(&ds_mac_mask, 0, sizeof(ds_mac_ipv4_key_t));
        tbl_ipkey.data_entry = (uint32*)&ds_mac_key;
        tbl_ipkey.mask_entry = (uint32*)&ds_mac_mask;

        ds_mac_key.ip_da            = p_group_node->address.ipv4.group_addr;
        ds_mac_key.ip_sa            = p_group_node->address.ipv4.src_addr;
        ds_mac_key.vrf_id3_0        = p_group_node->address.ipv4.vrfid & 0xF;
        ds_mac_key.vrf_id13_4       = (p_group_node->address.ipv4.vrfid >> 4) & 0x3FF;
        ds_mac_key.table_id0        = p_gb_ipmc_master[lchip]->mac_ipv4_key_tbl_id;
        ds_mac_key.table_id1        = p_gb_ipmc_master[lchip]->mac_ipv4_key_tbl_id;
        ds_mac_key.sub_table_id0    = p_gb_ipmc_master[lchip]->mac_ipv4_key_sub_tbl_id;
        ds_mac_key.sub_table_id1    = p_gb_ipmc_master[lchip]->mac_ipv4_key_sub_tbl_id;

        ds_mac_mask.ip_da            = 0xFFFFFFFF;
        ds_mac_mask.ip_sa            = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_mac_mask.vrf_id3_0        = 0xF;
        ds_mac_mask.vrf_id13_4       = 0x3FF;
        ds_mac_mask.table_id0        = 0x7;
        ds_mac_mask.table_id1        = 0x7;
        ds_mac_mask.sub_table_id0    = 0x3;
        ds_mac_mask.sub_table_id1    = 0x3;
        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_4], DRV_ENTRY_FLAG);

    }

    index = is_160key ? (p_group_node->ad_index / 2) : p_group_node->ad_index;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "write ipv4 key index is %d\n", index);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_ipkey));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_ipv6_write_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 cmd;
    tbl_entry_t tbl_ipkey;
    ds_ipv6_mcast_route_key_t ds_ipv6_route_key, ds_ipv6_route_mask;
    ds_mac_ipv6_key_t ds_mac_key, ds_mac_mask;
    uint8  is_default = 0;
    uint32 index = 0;
    uint16 max_vrf_num = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);


    if (!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        max_vrf_num = p_gb_ipmc_master[lchip]->is_sort_mode? 0 : p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_6];
        index = p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_6] + max_vrf_num;
        is_default = (p_group_node->ad_index == index) ? 1 : 0;

        sal_memset(&ds_ipv6_route_key, 0, sizeof(ds_ipv6_mcast_route_key_t));
        sal_memset(&ds_ipv6_route_mask, 0, sizeof(ds_ipv6_mcast_route_key_t));
        tbl_ipkey.data_entry = (uint32*)&ds_ipv6_route_key;
        tbl_ipkey.mask_entry = (uint32*)&ds_ipv6_route_mask;

        ds_ipv6_route_key.ip_da31_0          = p_group_node->address.ipv6.group_addr[0];
        ds_ipv6_route_key.ip_da63_32         = p_group_node->address.ipv6.group_addr[1];
        ds_ipv6_route_key.ip_da71_64         = p_group_node->address.ipv6.group_addr[2] & 0xFF;
        ds_ipv6_route_key.ip_da103_72        = ((p_group_node->address.ipv6.group_addr[2] & 0xFFFFFF00) >> 8) |
        ((p_group_node->address.ipv6.group_addr[3] & 0xFF) << 24);
        ds_ipv6_route_key.ip_da127_104       = ((p_group_node->address.ipv6.group_addr[3] & 0xFFFFFF00) >> 8);
        ds_ipv6_route_key.ip_sa31_0          = p_group_node->address.ipv6.src_addr[0];
        ds_ipv6_route_key.ip_sa63_32         = p_group_node->address.ipv6.src_addr[1];
        ds_ipv6_route_key.ip_sa71_64         = p_group_node->address.ipv6.src_addr[2] & 0xFF;
        ds_ipv6_route_key.ip_sa103_72        = ((p_group_node->address.ipv6.src_addr[2] & 0xFFFFFF00) >> 8) |
        ((p_group_node->address.ipv6.src_addr[3] & 0xFF) << 24);
        ds_ipv6_route_key.ip_sa127_104       = ((p_group_node->address.ipv6.src_addr[3] & 0xFFFFFF00) >> 8);
        ds_ipv6_route_key.vrf_id3_0          = p_group_node->address.ipv6.vrfid & 0xF;
        ds_ipv6_route_key.vrf_id13_4         = (p_group_node->address.ipv6.vrfid >> 4) & 0x3FF;
        ds_ipv6_route_key.table_id0          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id1          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id2          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id3          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id4          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id5          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id6          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.table_id7          = p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id;
        ds_ipv6_route_key.sub_table_id1      = p_gb_ipmc_master[lchip]->ipv6_router_key_sub_tbl_id;
        ds_ipv6_route_key.sub_table_id3      = p_gb_ipmc_master[lchip]->ipv6_router_key_sub_tbl_id;
        ds_ipv6_route_key.sub_table_id5      = p_gb_ipmc_master[lchip]->ipv6_router_key_sub_tbl_id;
        ds_ipv6_route_key.sub_table_id7      = p_gb_ipmc_master[lchip]->ipv6_router_key_sub_tbl_id;


        ds_ipv6_route_mask.ip_da31_0          = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFFFFFFFF : 0;
        ds_ipv6_route_mask.ip_da63_32         = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFFFFFFFF : 0;
        ds_ipv6_route_mask.ip_da71_64         = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFF : 0;
        ds_ipv6_route_mask.ip_da103_72        = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFFFFFFFF : 0;
        ds_ipv6_route_mask.ip_da127_104       = (p_group_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 0xFFFFFF : 0;
        ds_ipv6_route_mask.ip_sa31_0          = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_ipv6_route_mask.ip_sa63_32         = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_ipv6_route_mask.ip_sa71_64         = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFF;
        ds_ipv6_route_mask.ip_sa103_72        = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_ipv6_route_mask.ip_sa127_104       = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFF;
        ds_ipv6_route_mask.vrf_id3_0          = is_default ? 0 : ((p_gb_ipmc_master[lchip]->is_ipmcv4_key160) ? 0xF : 0);
        ds_ipv6_route_mask.vrf_id13_4         = is_default ? 0 : ((p_gb_ipmc_master[lchip]->is_ipmcv4_key160) ? 0x3FF : 0);


        ds_ipv6_route_mask.table_id0          = 0x7;
        ds_ipv6_route_mask.table_id1          = 0x7;
        ds_ipv6_route_mask.table_id2          = 0x7;
        ds_ipv6_route_mask.table_id3          = 0x7;
        ds_ipv6_route_mask.table_id4          = 0x7;
        ds_ipv6_route_mask.table_id5          = 0x7;
        ds_ipv6_route_mask.table_id6          = 0x7;
        ds_ipv6_route_mask.table_id7          = 0x7;
        ds_ipv6_route_mask.sub_table_id1      = 0x3;
        ds_ipv6_route_mask.sub_table_id3      = 0x3;
        ds_ipv6_route_mask.sub_table_id5      = 0x3;
        ds_ipv6_route_mask.sub_table_id7      = 0x3;

        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6], DRV_ENTRY_FLAG);
    }
    else
    {
        sal_memset(&ds_mac_key, 0, sizeof(ds_mac_ipv6_key_t));
        sal_memset(&ds_mac_mask, 0, sizeof(ds_mac_ipv6_key_t));
        tbl_ipkey.data_entry = (uint32*)&ds_mac_key;
        tbl_ipkey.mask_entry = (uint32*)&ds_mac_mask;

        ds_mac_key.ip_da31_0        = p_group_node->address.ipv6.group_addr[0];
        ds_mac_key.ip_da63_32       = p_group_node->address.ipv6.group_addr[1];
        ds_mac_key.ip_da95_64       = p_group_node->address.ipv6.group_addr[2];
        ds_mac_key.ip_da127_96      = p_group_node->address.ipv6.group_addr[3];
        ds_mac_key.ip_sa31_0        = p_group_node->address.ipv6.src_addr[0];
        ds_mac_key.ip_sa63_32       = p_group_node->address.ipv6.src_addr[1];
        ds_mac_key.ip_sa95_64       = p_group_node->address.ipv6.src_addr[2];
        ds_mac_key.ip_sa127_96      = p_group_node->address.ipv6.src_addr[3];
        ds_mac_key.vrf_id3_0        = p_group_node->address.ipv6.vrfid & 0xF;
        ds_mac_key.vrf_id13_4       = (p_group_node->address.ipv6.vrfid >> 4) & 0x3FF;
        ds_mac_key.table_id0        = p_gb_ipmc_master[lchip]->mac_ipv6_key_tbl_id;
        ds_mac_key.table_id1        = p_gb_ipmc_master[lchip]->mac_ipv6_key_tbl_id;
        ds_mac_key.table_id2        = p_gb_ipmc_master[lchip]->mac_ipv6_key_tbl_id;
        ds_mac_key.table_id3        = p_gb_ipmc_master[lchip]->mac_ipv6_key_tbl_id;
        ds_mac_key.sub_table_id0    = p_gb_ipmc_master[lchip]->mac_ipv6_key_sub_tbl_id;
        ds_mac_key.sub_table_id1    = p_gb_ipmc_master[lchip]->mac_ipv6_key_sub_tbl_id;
        ds_mac_key.sub_table_id2    = p_gb_ipmc_master[lchip]->mac_ipv6_key_sub_tbl_id;
        ds_mac_key.sub_table_id3    = p_gb_ipmc_master[lchip]->mac_ipv6_key_sub_tbl_id;

        ds_mac_mask.ip_da31_0          = 0xFFFFFFFF;
        ds_mac_mask.ip_da63_32         = 0xFFFFFFFF;
        ds_mac_mask.ip_da95_64         = 0xFFFFFFFF;
        ds_mac_mask.ip_da127_96        = 0xFFFFFFFF;
        ds_mac_mask.ip_sa31_0          = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_mac_mask.ip_sa63_32         = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_mac_mask.ip_sa95_64         = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_mac_mask.ip_sa127_96        = (!(p_group_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? 0 : 0xFFFFFFFF;
        ds_mac_mask.vrf_id3_0          = 0xF;
        ds_mac_mask.vrf_id13_4         = 0x3FF;
        ds_mac_mask.table_id0          = 0x7;
        ds_mac_mask.table_id1          = 0x7;
        ds_mac_mask.table_id2          = 0x7;
        ds_mac_mask.table_id3          = 0x7;
        ds_mac_mask.sub_table_id0      = 0x3;
        ds_mac_mask.sub_table_id1      = 0x3;
        ds_mac_mask.sub_table_id2      = 0x3;
        ds_mac_mask.sub_table_id3      = 0x3;

        cmd = DRV_IOW(p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_6], DRV_ENTRY_FLAG);

    }
    index = (p_gb_ipmc_master[lchip]->is_sort_mode)? (p_group_node->ad_index / 2) : p_group_node->ad_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_ipkey));
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "write ipv6 key index is %d\n", index);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipmc_write_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint8  ip_version = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    ip_version = (p_group_node->flag & 0x4) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (CTC_IP_VER_4 == ip_version)
    {
        return _sys_greatbelt_ipmc_ipv4_write_key(lchip, p_group_node);
    }
    else
    {
        return _sys_greatbelt_ipmc_ipv6_write_key(lchip, p_group_node);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_ipv4_remove_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 index = 0;

    uint32 is_160key = 0;
    uint32 tcam_table_id = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    if ((CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160)&&(!p_gb_ipmc_master[lchip]->is_sort_mode))
    { /*160 bit key*/
        is_160key = 1;
    }

    if (!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        tcam_table_id = p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4];
    }
    else
    {
        tcam_table_id = p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_4];
    }

    index = is_160key ? (p_group_node->ad_index / 2) : p_group_node->ad_index;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "remove ipv4 key index is %d\n", index);

    DRV_TCAM_TBL_REMOVE(lchip, tcam_table_id, index);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_ipv6_remove_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 index = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    index = (p_gb_ipmc_master[lchip]->is_sort_mode)? (p_group_node->ad_index / 2) : p_group_node->ad_index;
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "remove ipv6 key index is %d\n", index);

    if (!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        DRV_TCAM_TBL_REMOVE(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6], index);
        return CTC_E_NONE;
    }

    DRV_TCAM_TBL_REMOVE(lchip, p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_6], index);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipmc_remove_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint8  ip_version = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    ip_version = (p_group_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (CTC_IP_VER_4 == ip_version)
    {
        return _sys_greatbelt_ipmc_ipv4_remove_key(lchip, p_group_node);
    }
    else
    {
        return _sys_greatbelt_ipmc_ipv6_remove_key(lchip, p_group_node);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_add_group(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 ip_version = 0;
    uint8 is_default_entry = 0;
    int32 ret = 0;
    uint32 fwd_offset = 0;
    uint8 with_nexthop = 0;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_nh_param_mcast_group_t  nh_mcast_group;
    sys_ipmc_rpf_info_t sys_ipmc_rpf_info;
    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&fwd_offset, 0, sizeof(uint32));
    sal_memset(&sys_ipmc_rpf_info, 0, sizeof(sys_ipmc_rpf_info_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);

    with_nexthop = CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_WITH_NEXTHOP)?1:0;
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_WITH_NEXTHOP))
    {
        sys_nh_info_dsnh_t nh_info;
        if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) || !p_group->nhid)
        {
            return CTC_E_INVALID_PARAM;
        }
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_group->nhid, &nh_info));
        if ((p_group->src_ip_mask_len !=0 || p_group->group_ip_mask_len !=0) && (nh_info.nh_entry_type != SYS_NH_TYPE_MCAST))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if ((0 == p_group->src_ip_mask_len) && (0 == p_group->group_ip_mask_len))
    {
        /* add per vrf default entry */
        is_default_entry = 1;

        if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
        {
            return CTC_E_INVALID_PARAM;
        }

        if ((CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP) +
            CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_REDIRECT_TOCPU) +
            CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_RPF_CHECK)) > 1)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        if ((CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP) +
            CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_REDIRECT_TOCPU)) > 1)
        {
            return CTC_E_INVALID_PARAM;
        }
        SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
        SYS_IPMC_ADDRESS_SORT(p_group);
        SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    }

    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_add_group\n");

    ip_version = p_group->ip_version;

    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    p_group_node->group_id = p_group->group_id;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (p_group_node && (!is_default_entry))
    {
        /* The group to add has already existed, only default entry support update */
        CTC_RETURN_IPMC_UNLOCK(CTC_E_IPMC_GROUP_HAS_EXISTED);
    }

    if (NULL == p_group_node)
    {
        /* drop action default entry always existed */
        if (is_default_entry && CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP))
        {
            CTC_RETURN_IPMC_UNLOCK(ret);
        }

        /* if the group to add currently doesn't exist, creat it, and add into database */
        p_group_node = mem_malloc(MEM_IPMC_MODULE,  p_gb_ipmc_master[lchip]->info_size[ip_version]);
        if (NULL == p_group_node)
        {
            CTC_RETURN_IPMC_UNLOCK(CTC_E_NO_MEMORY);
        }
    }
    else
    {
        if (p_group->flag != p_group_node->extern_flag || with_nexthop)
        {
            /* update default entry: first remove old not drop action group node from database than add new */
            IPMC_UNLOCK;
            CTC_ERROR_RETURN(sys_greatbelt_ipmc_remove_group(lchip, p_group));
            IPMC_LOCK;

            /* update to drop : drop action default entry always existed */
            if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP))
            {
                CTC_RETURN_IPMC_UNLOCK(CTC_E_NONE);
            }
            else
            {
                p_group_node = mem_malloc(MEM_IPMC_MODULE,  p_gb_ipmc_master[lchip]->info_size[ip_version]);
                if (NULL == p_group_node)
                {
                    CTC_RETURN_IPMC_UNLOCK(CTC_E_NO_MEMORY);
                }
            }
        }
        else  /* not really update default entry */
        {
            CTC_RETURN_IPMC_UNLOCK(CTC_E_NONE);
        }
    }

    sal_memset(p_group_node, 0, p_gb_ipmc_master[lchip]->info_size[ip_version]);
    p_group_node->extern_flag       = p_group->flag;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);
    p_group_node->group_id = p_group->group_id;

    if (!is_default_entry || with_nexthop)
    {
        sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));
        nh_mcast_group.stats_valid = (p_group->flag & CTC_IPMC_FLAG_STATS) ? 1 : 0;
        nh_mcast_group.dsfwd_valid = 1;
        nh_mcast_group.stats_id = p_group->stats_id;

        if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP))/*dummy entry, no need to creat nexthop*/
        {
            if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS))
            {
                sys_nh_param_special_t p_nh_para;
                sal_memset(&p_nh_para, 0, sizeof(sys_nh_param_special_t));
                p_nh_para.hdr.is_internal_nh = 1;
                p_nh_para.hdr.nh_param_type = SYS_NH_TYPE_DROP;
                p_nh_para.hdr.stats_valid = 1;
                CTC_ERROR_GOTO(sys_greatbelt_stats_get_statsptr(lchip, p_group->stats_id, &p_nh_para.hdr.stats_ptr), ret, error_return);
                CTC_ERROR_GOTO(sys_greatbelt_nh_api_create(lchip, (sys_nh_param_com_t*)(&p_nh_para)), ret, error_return);
                p_group_node->nexthop_id = p_nh_para.hdr.nhid;
            }
            else
            {
                p_group_node->nexthop_id = SYS_NH_RESOLVED_NHID_FOR_DROP;
            }
            CTC_ERROR_GOTO(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_group_node->nexthop_id, &fwd_offset), ret, error_return);
        }
        else if(with_nexthop)
        {
            p_group_node->nexthop_id = p_group->nhid;
            CTC_ERROR_GOTO(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_group_node->nexthop_id, &fwd_offset), ret, error_return);
        }
        else
        {
            /* creat Nexthop for IP Mcast according to Group id */
            ret = sys_greatbelt_mcast_nh_create(lchip, p_group->group_id,  &nh_mcast_group);
            if (ret < 0)
            {
                mem_free(p_group_node);
                CTC_RETURN_IPMC_UNLOCK(ret);
            }
            p_group_node->nexthop_id = nh_mcast_group.nhid;
            fwd_offset = nh_mcast_group.fwd_offset;
        }


        if (0 == nh_mcast_group.stats_ptr)
        {
            p_group->statsFail = TRUE;
        }
    }

    /* lookup hw table to get key index */
    if (!CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[p_group->ip_version], SYS_IPMC_TCAM_LOOKUP))
    {
        if (!is_default_entry)
        {
            if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP)
                && (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS)))
            {
                sys_greatbelt_nh_api_delete(lchip, p_group_node->nexthop_id, SYS_NH_TYPE_DROP);
            }
            else
            {
                sys_greatbelt_mcast_nh_delete(lchip, p_group_node->nexthop_id);
            }
        }

        ret = CTC_E_NO_RESOURCE;
        goto error_return;
    }

    /* get rpf index:sa_index */
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf, p_group->rpf_intf, sizeof(sys_ipmc_rpf_info.rpf_intf));
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf_valid, p_group->rpf_intf_valid, sizeof(sys_ipmc_rpf_info.rpf_intf_valid));

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&
        !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        rpf_info.force_profile = FALSE;
        rpf_info.usage = is_default_entry ? SYS_RPF_USAGE_TYPE_DEFAULT : SYS_RPF_USAGE_TYPE_IPMC;
        rpf_info.profile_index = SYS_RPF_INVALID_PROFILE_ID;
        rpf_info.intf = (sys_rpf_intf_t*)&sys_ipmc_rpf_info;

        ret = sys_greatbelt_rpf_add_profile(lchip, &rpf_info, &rpf_rslt);
        if (ret < 0)
        {
            if (!is_default_entry)
            {
                if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP)
                    &&(CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS)))
                {
                    sys_greatbelt_nh_api_delete(lchip, p_group_node->nexthop_id, SYS_NH_TYPE_DROP);
                }
                else if(!with_nexthop)
                {
                    sys_greatbelt_mcast_nh_delete(lchip, p_group_node->nexthop_id);
                }
            }

            goto error_return;
        }

        if (!is_default_entry)
        {
            if (SYS_RPF_CHK_MODE_IFID == rpf_rslt.mode)
            {
                CTC_UNSET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
            }
            else
            {
                CTC_SET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
                p_group_node->sa_index = rpf_rslt.id;
            }
        }
    }

    /* get ad_index and add the new group into database */
    ret = sys_greatbelt_ipmc_db_add(lchip, p_group_node);
    if (ret < 0)
    {
        if (!is_default_entry)
        {
            if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP)
                && (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS)))
            {
                sys_greatbelt_nh_api_delete(lchip, p_group_node->nexthop_id, SYS_NH_TYPE_DROP);
            }
            else if(!with_nexthop)
            {
                sys_greatbelt_mcast_nh_delete(lchip, p_group_node->nexthop_id);
            }
        }
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&  \
            !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) &&      \
            CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
        {
             /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_greatbelt_rpf_remove_profile\n");*/
            CTC_ERROR_GOTO(sys_greatbelt_rpf_remove_profile(lchip, p_group_node->sa_index), ret, error_return);
        }

        goto error_return;
    }

    /* write group info to Asic hardware table : dsipda, dsrpf, key */
    ret = _sys_greatbelt_ipmc_write_ipda(lchip, p_group_node, fwd_offset, &rpf_rslt);
    ret = (ret || (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_RPF_CHECK)) ||
           CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) ||
           (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))) ? ret :
        _sys_greatbelt_ipmc_write_ipsa(lchip, p_group_node, &sys_ipmc_rpf_info);

    ret = ret ? ret :
        _sys_greatbelt_ipmc_write_key(lchip, p_group_node);

    if (ret < 0)
    {
        if (!is_default_entry)
        {
            if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_DROP)
                && (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS)))
            {
                sys_greatbelt_nh_api_delete(lchip, p_group_node->nexthop_id, SYS_NH_TYPE_DROP);
            }
            else if(!with_nexthop)
            {
                sys_greatbelt_mcast_nh_delete(lchip, p_group_node->nexthop_id);
            }
        }
        sys_greatbelt_ipmc_db_remove(lchip, p_group_node);
        goto error_return;
    }

    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);
    return ret;

error_return:
    if (p_group_node)
    {
        mem_free(p_group_node);
    }
    CTC_RETURN_IPMC_UNLOCK(ret);
    return ret;

}

int32
sys_greatbelt_ipmc_remove_group(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 ip_version;
    uint8 is_default_entry = 0;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if ((0 == p_group->src_ip_mask_len) && (0 == p_group->group_ip_mask_len))
    {
        is_default_entry = 1;
        if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        SYS_IPMC_ADDRESS_SORT(p_group);
        SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
        SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    }

    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_remove_group\n");
    ip_version = p_group->ip_version;
    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* the group doesn't exist */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }

    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_KEEP_EMPTY_ENTRY))
    {
        /* TBD*/
        /* remove ALL members belonged to this group */
        /* sys_greatbelt_mc_remove_all_member(p_group_node->local_member_list, p_group_node->nexthop_id); */
        IPMC_UNLOCK;
        return CTC_E_NONE;
    }

    /* remove key from TCAM, don't care Associate Table */
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_greatbelt_ipmc_remove_key(lchip, p_group_node));

    /* remove Nexthop for according to Nexthop id */
     /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_greatbelt_mcast_nh_delete\n");*/
    if (!is_default_entry)
    {
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP))
        {
            if (SYS_NH_RESOLVED_NHID_FOR_DROP != p_group_node->nexthop_id)
            {
                sys_greatbelt_nh_api_delete(lchip, p_group_node->nexthop_id, SYS_NH_TYPE_DROP);
            }
        }
        else if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_WITH_NEXTHOP))
        {
            sys_greatbelt_mcast_nh_delete(lchip, p_group_node->nexthop_id);
        }
    }

    /* remove group node from database */
    sys_greatbelt_ipmc_db_remove(lchip, p_group_node);

    mem_free(p_group_node);
    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);
    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_ipmc_update_member(uint8 lchip, ctc_ipmc_member_info_t* ipmc_member, sys_nh_param_mcast_group_t* nh_mcast_group, uint8  ip_l2mc)
{
    int32 ret = 0;
    uint8 aps_brg_en = 0;
    uint16 dest_id = 0;

    if (ipmc_member->is_nh)
    {
        nh_mcast_group->mem_info.ref_nhid = ipmc_member->nh_id;
        if (CTC_FLAG_ISSET(ipmc_member->flag, CTC_IPMC_MEMBER_FLAG_ASSIGN_PORT))
        {
            nh_mcast_group->mem_info.destid = CTC_MAP_GPORT_TO_LPORT(ipmc_member->global_port);
            nh_mcast_group->mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID;
            dest_id = ipmc_member->global_port;
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_nh_get_port(lchip, ipmc_member->nh_id, &aps_brg_en, &dest_id));
            nh_mcast_group->mem_info.destid  = aps_brg_en ? dest_id : CTC_MAP_GPORT_TO_LPORT(dest_id);
            nh_mcast_group->mem_info.member_type = aps_brg_en ? SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE :
                                                SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH;
        }
    }
    else
    {
        nh_mcast_group->mem_info.destid  = CTC_MAP_GPORT_TO_LPORT(ipmc_member->global_port);
        dest_id = ipmc_member->global_port;

        if (ipmc_member->remote_chip)
        {
            nh_mcast_group->mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_REMOTE;
        }
        else if (ipmc_member->vlan_port ||
            (ip_l2mc && (ipmc_member->l3_if_type == MAX_L3IF_TYPE_NUM)))
        {
            nh_mcast_group->mem_info.member_type = SYS_NH_PARAM_BRGMC_MEM_LOCAL;
        }
        else
        {
            nh_mcast_group->mem_info.member_type = SYS_NH_PARAM_IPMC_MEM_LOCAL;
        }

        if (CTC_L3IF_TYPE_PHY_IF == ipmc_member->l3_if_type)
        {
            /* don't care vlan id in case of Routed Port */
            nh_mcast_group->mem_info.vid     = 0;
        }
        else
        {
            /* vlan interface */
            nh_mcast_group->mem_info.vid     = ipmc_member->vlan_id;
        }

        nh_mcast_group->mem_info.l3if_type       = ipmc_member->l3_if_type;
        nh_mcast_group->mem_info.mtu_no_chk = CTC_FLAG_ISSET(ipmc_member->flag, CTC_IPMC_MEMBER_FLAG_MTU_NO_CHECK);
    }

    if (!ipmc_member->remote_chip && (aps_brg_en || CTC_IS_LINKAGG_PORT(dest_id) || SYS_IS_LOOP_PORT(dest_id)))
    {
        if (CTC_IS_LINKAGG_PORT(dest_id))
        {
            nh_mcast_group->mem_info.is_linkagg  = 1;
        }
        else
        {
            nh_mcast_group->mem_info.is_linkagg  = 0;
        }


        ret = sys_greatbelt_mcast_nh_update(lchip, nh_mcast_group);
        if (ret < 0)
        {
            return ret;
        }
    }
    else
    {
        if (FALSE == sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(dest_id)))
        {
            return CTC_E_NONE;
        }

        nh_mcast_group->mem_info.is_linkagg = 0;
        ret = sys_greatbelt_mcast_nh_update(lchip, nh_mcast_group);
        if (ret < 0)
        {
            return ret;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_update_member_with_bitmap(uint8 lchip, ctc_ipmc_member_info_t* ipmc_member, sys_nh_param_mcast_group_t* nh_mcast_group, uint8  ip_l2mc)
{
    uint8 loop1 = 0;
    uint8 loop2 = 0;
    uint16 lport = 0;
    int32  ret = 0;
    uint8  lchip_temp = 0;

    if (ipmc_member->gchip_id != CTC_LINKAGG_CHIPID)
    {
        SYS_MAP_GCHIP_TO_LCHIP(ipmc_member->gchip_id, lchip_temp);
        SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
    }
    for (loop1 = 0; loop1 < sizeof(ipmc_member->port_bmp) / 4; loop1++)
    {
        if (ipmc_member->port_bmp[loop1] == 0)
        {
            continue;
        }
        for (loop2 = 0; loop2 < 32; loop2++)
        {
            if (!CTC_IS_BIT_SET(ipmc_member->port_bmp[loop1], loop2))
            {
                continue;
            }
            lport = loop2 + loop1 * 32;
            ipmc_member->global_port = CTC_MAP_LPORT_TO_GPORT(ipmc_member->gchip_id, lport);
            ret = _sys_greatbelt_ipmc_update_member(lchip, ipmc_member, nh_mcast_group, ip_l2mc);
            if ((ret < 0) && (nh_mcast_group->opcode == SYS_NH_PARAM_MCAST_ADD_MEMBER))
            {
                return ret;
            }
        }
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_add_member(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 member_index = 0;
    uint8 ip_version;
    int32 ret = CTC_E_NONE;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_nh_param_mcast_group_t nh_mcast_group;
    uint8 ip_l2mc = 0;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_add_member\n");
    ip_version = p_group->ip_version;
    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* the group doesn't exist */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP))
    {
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_INVALID_CONFIG);
    }

    nh_mcast_group.nhid = p_group_node->nexthop_id;
    nh_mcast_group.dsfwd_valid = 0;
    nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_ADD_MEMBER;
    ip_l2mc = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC);

    for (member_index = 0; member_index < p_group->member_number; member_index++)
    {
        if ((p_group->ipmc_member[member_index].l3_if_type >= MAX_L3IF_TYPE_NUM)
        && !(p_group->ipmc_member[member_index].is_nh)
        && !(p_group->ipmc_member[member_index].re_route)
        && !(ip_l2mc))
        {
            IPMC_UNLOCK;
            return CTC_E_IPMC_BAD_L3IF_TYPE;
        }
        ret = p_group->ipmc_member[member_index].port_bmp_en ?
            _sys_greatbelt_ipmc_update_member_with_bitmap(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc):
            _sys_greatbelt_ipmc_update_member(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc);
        if (ret < 0)
        {
            break;
        }
    }

    /*if error, rollback*/
    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);
    if ((ret < 0) && (ret != CTC_E_EXIST))
    {
        p_group->member_number = member_index + 1;
        sys_greatbelt_ipmc_remove_member(lchip, p_group);
        return ret;
    }

    return ret;
}

int32
sys_greatbelt_ipmc_remove_member(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 member_index = 0;
    uint8 ip_version;
    int32 ret = 0;
    uint8 ip_l2mc = 0;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_nh_param_mcast_group_t nh_mcast_group;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_remove_member\n");
    ip_version = p_group->ip_version;
    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* the group doesn't exist */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP))
    {
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_INVALID_CONFIG);
    }
    nh_mcast_group.nhid = p_group_node->nexthop_id;
    nh_mcast_group.dsfwd_valid = 0;
    nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_DEL_MEMBER;
    ip_l2mc = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC);

    for (member_index = 0; member_index < p_group->member_number; member_index++)
    {
        if ((p_group->ipmc_member[member_index].l3_if_type >= MAX_L3IF_TYPE_NUM)
        && !(p_group->ipmc_member[member_index].is_nh)
        && !(p_group->ipmc_member[member_index].re_route)
        && !(ip_l2mc))
        {
            IPMC_UNLOCK;
            return CTC_E_IPMC_BAD_L3IF_TYPE;
        }
        ret = p_group->ipmc_member[member_index].port_bmp_en ?
            _sys_greatbelt_ipmc_update_member_with_bitmap(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc):
            _sys_greatbelt_ipmc_update_member(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc);
    }

    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);

    return ret;
}

int32
sys_greatbelt_ipmc_add_default_entry(uint8 lchip, uint8 ip_version, ctc_ipmc_default_action_t type)
{
    uint32 nh_id = SYS_NH_RESOLVED_NHID_FOR_DROP;

    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;
    sys_rpf_intf_t intf;
    sys_ipmc_group_node_t ipmc_default_group;
    uint32 fwd_offset = 0;
    uint8 entry_num = 0;
    uint16 max_vrf_num = 0;

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&ipmc_default_group, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&intf, 0, sizeof(sys_rpf_intf_t));

    rpf_info.intf = &intf;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_IP_VER_CHECK(ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(ip_version);

    if (CTC_IPMC_DEFAULT_ACTION_DROP != type)
    {
        nh_id = SYS_NH_RESOLVED_NHID_FOR_TOCPU;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, nh_id, &fwd_offset));

    if ((CTC_IP_VER_4 == ip_version) && p_gb_ipmc_master[lchip]->is_ipmcv4_key160)
    {
        entry_num = 2;
    }
    else
    {
        entry_num = 1;
    }
    max_vrf_num = p_gb_ipmc_master[lchip]->is_sort_mode? 0 : p_gb_ipmc_master[lchip]->max_vrf_num[ip_version];
    ipmc_default_group.ad_index = p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] + max_vrf_num * entry_num;

    if (CTC_IPMC_DEFAULT_ACTION_TO_CPU == type)
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;
    }
    else if (CTC_IPMC_DEFAULT_ACTION_FALLBACK_BRIDGE == type)
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;
    }
    else
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_IPMC;
    }

    CTC_ERROR_RETURN(sys_greatbelt_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    ipmc_default_group.flag = (ip_version == CTC_IP_VER_6) ? (ipmc_default_group.flag | SYS_IPMC_FLAG_VERSION) : ipmc_default_group.flag;

    if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[ip_version], SYS_IPMC_TCAM_LOOKUP))
    {
        /* write ipda entry */
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_write_ipda(lchip, &ipmc_default_group, fwd_offset, &rpf_rslt));
        /* write ipmc key entry */
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_write_key(lchip, &ipmc_default_group));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_remove_default_entry(uint8 lchip, uint8 ip_version)
{
    sys_ipmc_group_node_t ipmc_default_group;
    uint8 entry_num = 0;
    uint16 max_vrf_num = 0;

    sal_memset(&ipmc_default_group, 0, sizeof(sys_ipmc_group_node_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_IP_VER_CHECK(ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(ip_version);

    if ((CTC_IP_VER_4 == ip_version) && p_gb_ipmc_master[lchip]->is_ipmcv4_key160)
    {
        entry_num = 2;
    }
    else
    {
        entry_num = 1;
    }
    max_vrf_num = p_gb_ipmc_master[lchip]->is_sort_mode? 0 : p_gb_ipmc_master[lchip]->max_vrf_num[ip_version];
    ipmc_default_group.ad_index = p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] + max_vrf_num * entry_num;
    ipmc_default_group.flag = (ip_version == CTC_IP_VER_6) ? (ipmc_default_group.flag | SYS_IPMC_FLAG_VERSION) : ipmc_default_group.flag;

    if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[ip_version], SYS_IPMC_TCAM_LOOKUP))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_remove_key(lchip, &ipmc_default_group));
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_ipmc_update_rpf(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 ip_version = 0;
    uint8 i = 0;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_rpf_info_t sys_ipmc_rpf_info;
    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;
    uint32 dsfwd_offset;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&sys_ipmc_rpf_info, 0, sizeof(sys_ipmc_rpf_info_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    for (i = 0; i < SYS_GB_MAX_IPMC_RPF_IF; i++)
    {
        /* check if interfaceId overflow max value */
        if (p_group->rpf_intf_valid[i] && p_group->rpf_intf[i] > SYS_GB_MAX_CTC_L3IF_ID)
        {
            return CTC_E_L3IF_INVALID_IF_ID;
        }
    }

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_greatbelt_ipmc_update_rpf\n");

    ip_version = p_group->ip_version;

    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* The group to set not existed*/
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }

    if (!(CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&   \
          !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC)))
    {
        IPMC_UNLOCK;
        return CTC_E_IPMC_RPF_CHECK_DISABLE;
    }

    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_group_node->nexthop_id, &dsfwd_offset));
    /* write group info to Asic hardware table */
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf, p_group->rpf_intf, sizeof(sys_ipmc_rpf_info.rpf_intf));
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf_valid, p_group->rpf_intf_valid, sizeof(sys_ipmc_rpf_info.rpf_intf_valid));

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    rpf_info.force_profile = FALSE;
    rpf_info.usage = SYS_RPF_USAGE_TYPE_IPMC;
    rpf_info.profile_index = p_group_node->sa_index;
    rpf_info.intf = (sys_rpf_intf_t*)&sys_ipmc_rpf_info;

    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ad_index is %d\n", p_group_node->ad_index);

    if (SYS_RPF_CHK_MODE_PROFILE == rpf_rslt.mode)
    {
         /*CTC_SET_FLAG(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK);*/
        /* set ipda rpf_check_mode = 0, rpf use profile*/
        CTC_SET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);

        p_group_node->sa_index = rpf_rslt.id;
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "sa_index is %d\n", p_group_node->sa_index);

        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_greatbelt_ipmc_write_ipda(lchip, p_group_node, dsfwd_offset, &rpf_rslt));
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_greatbelt_ipmc_write_ipsa(lchip, p_group_node, &sys_ipmc_rpf_info));
    }
    else
    {
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
        {
            /* free old rpf profile */
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_rpf_remove_profile(lchip, p_group_node->sa_index));
            CTC_UNSET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
            p_group_node->sa_index = IPMC_INVALID_SA_INDEX;
        }

        CTC_UNSET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);

        /* set ipda rpf_if_id */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_greatbelt_ipmc_write_ipda(lchip, p_group_node, dsfwd_offset, &rpf_rslt));
    }

    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_get_group_info(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    uint8 ip_version = 0;
    uint32 cmd;
    uint32 ad_index;
    ds_rpf_t ds_rpf;
    ds_ip_da_t ds_da;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_get_group_info\n");

    ip_version = p_group->ip_version;

    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* the group doesn't exist */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }

    p_group->flag = p_group_node->extern_flag;
    p_group->group_id = p_group_node->group_id;

    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE) \
        && CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        sal_memset(&ds_rpf, 0, sizeof(ds_rpf_t));
        cmd = DRV_IOR(p_gb_ipmc_master[lchip]->sa_table_id[ip_version], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->sa_index, cmd, &ds_rpf));
        p_group->rpf_intf[0] = ds_rpf.rpf_if_id0;
        p_group->rpf_intf[1] = ds_rpf.rpf_if_id1;
        p_group->rpf_intf[2] = ds_rpf.rpf_if_id2;
        p_group->rpf_intf[3] = ds_rpf.rpf_if_id3;
        p_group->rpf_intf[4] = ds_rpf.rpf_if_id4;
        p_group->rpf_intf[5] = ds_rpf.rpf_if_id5;
        p_group->rpf_intf[6] = ds_rpf.rpf_if_id6;
        p_group->rpf_intf[7] = ds_rpf.rpf_if_id7;
        p_group->rpf_intf[8] = ds_rpf.rpf_if_id8;
        p_group->rpf_intf[9] = ds_rpf.rpf_if_id9;
        p_group->rpf_intf[10] = ds_rpf.rpf_if_id10;
        p_group->rpf_intf[11] = ds_rpf.rpf_if_id11;
        p_group->rpf_intf[12] = ds_rpf.rpf_if_id12;
        p_group->rpf_intf[13] = ds_rpf.rpf_if_id13;
        p_group->rpf_intf[14] = ds_rpf.rpf_if_id14;
        p_group->rpf_intf[15] = ds_rpf.rpf_if_id15;

        p_group->rpf_intf_valid[0] = ds_rpf.rpf_if_id_valid0;
        p_group->rpf_intf_valid[1] = ds_rpf.rpf_if_id_valid1;
        p_group->rpf_intf_valid[2] = ds_rpf.rpf_if_id_valid2;
        p_group->rpf_intf_valid[3] = ds_rpf.rpf_if_id_valid3;
        p_group->rpf_intf_valid[4] = ds_rpf.rpf_if_id_valid4;
        p_group->rpf_intf_valid[5] = ds_rpf.rpf_if_id_valid5;
        p_group->rpf_intf_valid[6] = ds_rpf.rpf_if_id_valid6;
        p_group->rpf_intf_valid[7] = ds_rpf.rpf_if_id_valid7;
        p_group->rpf_intf_valid[8] = ds_rpf.rpf_if_id_valid8;
        p_group->rpf_intf_valid[9] = ds_rpf.rpf_if_id_valid9;
        p_group->rpf_intf_valid[10] = ds_rpf.rpf_if_id_valid10;
        p_group->rpf_intf_valid[11] = ds_rpf.rpf_if_id_valid11;
        p_group->rpf_intf_valid[12] = ds_rpf.rpf_if_id_valid12;
        p_group->rpf_intf_valid[13] = ds_rpf.rpf_if_id_valid13;
        p_group->rpf_intf_valid[14] = ds_rpf.rpf_if_id_valid14;
        p_group->rpf_intf_valid[15] = ds_rpf.rpf_if_id_valid15;

    }
    else if(!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE) \
        && CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        sal_memset(&ds_da, 0, sizeof(ds_ip_da_t));
        ad_index = (p_gb_ipmc_master[lchip]->is_ipmcv4_key160 && (ip_version == CTC_IP_VER_4)&&(!p_gb_ipmc_master[lchip]->is_sort_mode)) ? (p_group_node->ad_index / 2) : p_group_node->ad_index;
        cmd = DRV_IOR(p_gb_ipmc_master[lchip]->da_tcam_table_id[ip_version], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, ad_index, cmd, &ds_da));

        p_group->rpf_intf[0] = ds_da.rpf_if_id;
        p_group->rpf_intf_valid[0] = 1;
    }

    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);
    return CTC_E_NONE;

}

int32
sys_greatbelt_ipmc_set_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data)
{
    uint32 cmd;
    uint32 field_val;
    ipv6_addr_t ipv6_mask;
    ipe_ipv4_mcast_force_route_t v4mcast;
    ipe_ipv6_mcast_force_route_t v6mcast;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_IP_VER_CHECK(p_data->ip_version);
    SYS_IPMC_ADDRESS_SORT_0(p_data);


    if (CTC_IP_VER_4 == p_data->ip_version)
    {
            cmd = DRV_IOR(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v4mcast));

            if (p_data->ipaddr0_valid)
            {
                v4mcast.addr0_value = p_data->ip_addr0.ipv4;
                IPV4_LEN_TO_MASK(v4mcast.addr0_mask, p_data->addr0_mask);
                p_gb_ipmc_master[lchip]->ipv4_force_route = p_gb_ipmc_master[lchip]->ipv4_force_route|SYS_IPMC_IP_FORCE_ROUTE_0;
            }
            else
            {
                if(!(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->ipv4_force_route, SYS_IPMC_IP_FORCE_ROUTE_0)))
                {
                    v4mcast.addr0_value = 0;
                    v4mcast.addr0_mask  = 0xFFFFFFFF;
                }
            }

            if (p_data->ipaddr1_valid)
            {
                v4mcast.addr1_value = p_data->ip_addr1.ipv4;
                IPV4_LEN_TO_MASK(v4mcast.addr1_mask, p_data->addr1_mask);
                p_gb_ipmc_master[lchip]->ipv4_force_route = p_gb_ipmc_master[lchip]->ipv4_force_route|SYS_IPMC_IP_FORCE_ROUTE_1;
            }
            else
            {
                if(!(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->ipv4_force_route, SYS_IPMC_IP_FORCE_ROUTE_1)))
                {
                    v4mcast.addr1_value = 0;
                    v4mcast.addr1_mask  = 0xFFFFFFFF;
                }
            }

            cmd = DRV_IOW(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v4mcast));

            field_val = p_data->force_bridge_en ? 1 : 0;
            cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_Ipv4McastForceBridgeEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            field_val = p_data->force_ucast_en ? 1 : 0;
            cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_Ipv4McastForceUnicastEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
            cmd = DRV_IOR(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v6mcast));

            if (p_data->ipaddr0_valid)
            {
                v6mcast.addr0_value3 = p_data->ip_addr0.ipv6[3];
                v6mcast.addr0_value2  = p_data->ip_addr0.ipv6[2];
                v6mcast.addr0_value1  = p_data->ip_addr0.ipv6[1];
                v6mcast.addr0_value0   = p_data->ip_addr0.ipv6[0];
                IPV6_LEN_TO_MASK(ipv6_mask, p_data->addr0_mask);
                v6mcast.addr0_mask3  = ipv6_mask[3];
                v6mcast.addr0_mask2   = ipv6_mask[2];
                v6mcast.addr0_mask1   = ipv6_mask[1];
                v6mcast.addr0_mask0    = ipv6_mask[0];
                p_gb_ipmc_master[lchip]->ipv6_force_route = p_gb_ipmc_master[lchip]->ipv6_force_route|SYS_IPMC_IP_FORCE_ROUTE_0;
            }
            else
            {
                if(!(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->ipv6_force_route, SYS_IPMC_IP_FORCE_ROUTE_0)))
                {
                    v6mcast.addr0_value3 = 0;
                    v6mcast.addr0_value2  = 0;
                    v6mcast.addr0_value1  = 0;
                    v6mcast.addr0_value0   = 0;
                    v6mcast.addr0_mask3  = 0xFFFFFFFF;
                    v6mcast.addr0_mask2   = 0xFFFFFFFF;
                    v6mcast.addr0_mask1   = 0xFFFFFFFF;
                    v6mcast.addr0_mask0    = 0xFFFFFFFF;
                }
            }

            if (p_data->ipaddr1_valid)
            {
                v6mcast.addr1_value3 = p_data->ip_addr1.ipv6[3];
                v6mcast.addr1_value2  = p_data->ip_addr1.ipv6[2];
                v6mcast.addr1_value1  = p_data->ip_addr1.ipv6[1];
                v6mcast.addr1_value0   = p_data->ip_addr1.ipv6[0];
                IPV6_LEN_TO_MASK(ipv6_mask, p_data->addr1_mask);
                v6mcast.addr1_mask3  = ipv6_mask[3];
                v6mcast.addr1_mask2   = ipv6_mask[2];
                v6mcast.addr1_mask1   = ipv6_mask[1];
                v6mcast.addr1_mask0    = ipv6_mask[0];
                p_gb_ipmc_master[lchip]->ipv6_force_route = p_gb_ipmc_master[lchip]->ipv6_force_route|SYS_IPMC_IP_FORCE_ROUTE_1;
            }
            else
            {
                if (!(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->ipv6_force_route, SYS_IPMC_IP_FORCE_ROUTE_1)))
                {
                    v6mcast.addr1_value3 = 0;
                    v6mcast.addr1_value2  = 0;
                    v6mcast.addr1_value1  = 0;
                    v6mcast.addr1_value0   = 0;
                    v6mcast.addr1_mask3  = 0xFFFFFFFF;
                    v6mcast.addr1_mask2   = 0xFFFFFFFF;
                    v6mcast.addr1_mask1   = 0xFFFFFFFF;
                    v6mcast.addr1_mask0    = 0xFFFFFFFF;
                }
            }

            cmd = DRV_IOW(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v6mcast));

            field_val = p_data->force_bridge_en ? 1 : 0;
            cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_Ipv6McastForceBridgeEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            field_val = p_data->force_ucast_en ? 1 : 0;
            cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_Ipv6McastForceUnicastEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_get_nhid(uint8 lchip, ctc_ipmc_group_info_t* p_group, uint32 * p_nhid )
{
    uint8 ip_version;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);
    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    ip_version = p_group->ip_version;
    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is%x, dest group is %x\n",
                         p_group->address.ipv4.src_addr,
                         p_group->address.ipv4.group_addr);
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "source IP is %x:%x:%x:%x, dest group is %x:%x:%x:%x\n",
                         p_group->address.ipv6.src_addr[0],
                         p_group->address.ipv6.src_addr[1],
                         p_group->address.ipv6.src_addr[2],
                         p_group->address.ipv6.src_addr[3],
                         p_group->address.ipv6.group_addr[0],
                         p_group->address.ipv6.group_addr[1],
                         p_group->address.ipv6.group_addr[2],
                         p_group->address.ipv6.group_addr[3]);
    }

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->extern_flag = p_group->flag;
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);

    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* the group doesn't exist */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(CTC_E_IPMC_GROUP_NOT_EXIST);
    }

    *p_nhid = p_group_node->nexthop_id;
    IPMC_UNLOCK;
    SYS_IPMC_ADDRESS_SORT(p_group);
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_get_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data)
{

    uint32 cmd;
    uint32 field_val;
    ipv6_addr_t ipv6_mask;
    ipe_ipv4_mcast_force_route_t v4mcast;
    ipe_ipv6_mcast_force_route_t v6mcast;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_IP_VER_CHECK(p_data->ip_version);

    if (CTC_IP_VER_4 == p_data->ip_version)
    {
        cmd = DRV_IOR(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v4mcast));

        p_data->ip_addr0.ipv4 = v4mcast.addr0_value;
        IPV4_MASK_TO_LEN(v4mcast.addr0_mask, p_data->addr0_mask);

        p_data->ip_addr1.ipv4 = v4mcast.addr1_value;
        IPV4_MASK_TO_LEN(v4mcast.addr1_mask, p_data->addr1_mask);

        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_Ipv4McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_data->force_bridge_en = field_val ? TRUE : FALSE;

        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_Ipv4McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        p_data->force_ucast_en = field_val ? TRUE : FALSE;

    }
    else
    {
        cmd = DRV_IOR(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &v6mcast));

        p_data->ip_addr0.ipv6[0] = v6mcast.addr0_value0;
        p_data->ip_addr0.ipv6[1] = v6mcast.addr0_value1;
        p_data->ip_addr0.ipv6[2] = v6mcast.addr0_value2;
        p_data->ip_addr0.ipv6[3] = v6mcast.addr0_value3;
        ipv6_mask[3]   = v6mcast.addr0_mask3;
        ipv6_mask[2]   = v6mcast.addr0_mask2;
        ipv6_mask[1]   = v6mcast.addr0_mask1;
        ipv6_mask[0]   = v6mcast.addr0_mask0;
        IPV6_MASK_TO_LEN(ipv6_mask, p_data->addr0_mask);

        p_data->ip_addr1.ipv6[0] = v6mcast.addr1_value0;
        p_data->ip_addr1.ipv6[1] = v6mcast.addr1_value1;
        p_data->ip_addr1.ipv6[2] = v6mcast.addr1_value2;
        p_data->ip_addr1.ipv6[3] = v6mcast.addr1_value3;
        ipv6_mask[3]   = v6mcast.addr1_mask3;
        ipv6_mask[2]   = v6mcast.addr1_mask2;
        ipv6_mask[1]   = v6mcast.addr1_mask1;
        ipv6_mask[0]   = v6mcast.addr1_mask0;
        IPV6_MASK_TO_LEN(ipv6_mask, p_data->addr1_mask);

        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_Ipv6McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_data->force_bridge_en = field_val ? TRUE : FALSE;

        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_Ipv6McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        p_data->force_ucast_en = field_val ? 1 : 0;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ipmc_traverse_pre(void* entry, void* p_trav)
{
    ctc_ipmc_group_info_t* p_ipmc_param = NULL;
    sys_ipmc_group_node_t* p_ipmc_info;
    hash_traversal_fn fn;
    void* data;
    int32 ret = 0;
    uint8 addr_len = 0;
    CTC_PTR_VALID_CHECK(entry);
    CTC_PTR_VALID_CHECK(p_trav);

    p_ipmc_param = (ctc_ipmc_group_info_t*)mem_malloc(MEM_IPMC_MODULE, sizeof(ctc_ipmc_group_info_t));
    if (NULL == p_ipmc_param)
    {
        return CTC_E_NO_MEMORY;
    }

    p_ipmc_info = entry;
    fn = ((sys_ipmc_traverse_t*)p_trav)->fn;
    data = ((sys_ipmc_traverse_t*)p_trav)->data;
    p_ipmc_param->ip_version = (p_ipmc_info->flag & 0x04) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    if (p_ipmc_param->ip_version == CTC_IP_VER_6)
    {
        p_ipmc_param->src_ip_mask_len = (p_ipmc_info->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 128 : 0;
        p_ipmc_param->group_ip_mask_len = (p_ipmc_info->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 128 : 0;
    }
    else
    {
        p_ipmc_param->src_ip_mask_len = (p_ipmc_info->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 32 : 0;
        p_ipmc_param->group_ip_mask_len = (p_ipmc_info->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 32 : 0;
    }

    addr_len = (p_ipmc_param->ip_version == CTC_IP_VER_6)? sizeof(ctc_ipmc_ipv6_addr_t) : sizeof(ctc_ipmc_ipv4_addr_t);
    sal_memcpy(&(p_ipmc_param->address), &(p_ipmc_info->address), addr_len);

    ret = (* fn)(p_ipmc_param, data);

    mem_free(p_ipmc_param);

    return ret;
}

int32
sys_greatbelt_ipmc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipmc_traverse_fn fn, void* user_data)
{
    hash_traversal_fn  fun = _sys_greatbelt_ipmc_traverse_pre;
    sys_ipmc_traverse_t trav;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(user_data);
    SYS_IPMC_VERSION_ENABLE_CHECK(ip_ver);

    trav.data = user_data;
    trav.fn = fn;
    if (NULL == fn)
    {
        return CTC_E_NONE;
    }

    return _sys_greatbelt_ipmc_db_traverse(lchip, ip_ver, fun, &trav);
}

int32
sys_greatbelt_ipmc_tcam_reinit(uint8 lchip, uint8 is_add)
{
    uint8 ip_ver;
    int32 ret = CTC_E_NONE;

    for (ip_ver = 0; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        if (is_add)
        {
            ret = (p_gb_ipmc_master[lchip]->version_en[ip_ver] && (ret == 0)) ? sys_greatbelt_ipmc_add_default_entry(lchip, ip_ver, CTC_IPMC_DEFAULT_ACTION_DROP) : ret;
        }
        else
        {
            ret = (p_gb_ipmc_master[lchip]->version_en[ip_ver] && (ret == 0)) ? sys_greatbelt_ipmc_remove_default_entry(lchip, ip_ver) : ret;
        }
    }

    return ret;
}


int32
sys_greatbelt_ipmc_init(uint8 lchip)
{
    uint32 table_size = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd;
    uint16 ipv4_vrf_max = 0;
    uint16 ipv6_vrf_max = 0;
    uint8 ip_ver;
    uint8 lookup_mode = 0;
    ipe_lookup_ctl_t ipe_lookup_ctl;

    p_gb_ipmc_master[lchip] = mem_malloc(MEM_IPMC_MODULE, sizeof(sys_ipmc_master_t));
    if (NULL == p_gb_ipmc_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_ipmc_master[lchip], 0, sizeof(sys_ipmc_master_t));
    sal_mutex_create(&(p_gb_ipmc_master[lchip]->p_ipmc_mutex));

    if (NULL == p_gb_ipmc_master[lchip]->p_ipmc_mutex)
    {
        mem_free(p_gb_ipmc_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gb_ipmc_master[lchip]->sa_table_id[CTC_IP_VER_4] = DsRpf_t;
    p_gb_ipmc_master[lchip]->sa_table_id[CTC_IP_VER_6] = DsRpf_t;

    p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4] = DsIpv4McastRouteKey_t;
    p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6] = DsIpv6McastRouteKey_t;

    p_gb_ipmc_master[lchip]->da_tcam_table_id[CTC_IP_VER_4] = DsIpv4McastDaTcam_t;
    p_gb_ipmc_master[lchip]->da_tcam_table_id[CTC_IP_VER_6] = DsIpv6McastDaTcam_t;

    p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_4] = DsMacIpv4Key_t;
    p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_6] = DsMacIpv6Key_t;
    p_gb_ipmc_master[lchip]->da_tcam_l2_table_id[CTC_IP_VER_4]  = DsMacIpv4Tcam_t;
    p_gb_ipmc_master[lchip]->da_tcam_l2_table_id[CTC_IP_VER_6]  = DsMacIpv6Tcam_t;

    p_gb_ipmc_master[lchip]->info_size[CTC_IP_VER_4] = sizeof(sys_ipmc_group_node_t) - sizeof(ctc_ipmc_ipv6_addr_t) + sizeof(ctc_ipmc_ipv4_addr_t);
    p_gb_ipmc_master[lchip]->info_size[CTC_IP_VER_6] = sizeof(sys_ipmc_group_node_t);

    /* max vrfid number*/
    ipv4_vrf_max = sys_greatbelt_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    ipv6_vrf_max = sys_greatbelt_l3if_get_max_vrfid(lchip, CTC_IP_VER_6);
    p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_4] = ipv4_vrf_max;
    p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_6] = ipv6_vrf_max;

    p_gb_ipmc_master[lchip]->cpu_rpf = FALSE;

    p_gb_ipmc_master[lchip]->max_mask_len[CTC_IP_VER_4] = 32;
    p_gb_ipmc_master[lchip]->max_mask_len[CTC_IP_VER_6] = 128;

    p_gb_ipmc_master[lchip]->addr_len[CTC_IP_VER_4] = sizeof(ctc_ipmc_ipv4_addr_t);
    p_gb_ipmc_master[lchip]->addr_len[CTC_IP_VER_6] = sizeof(ctc_ipmc_ipv6_addr_t);

    /* Get lookup mode from IPE_ROUTE_LOOKUP */
    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    lookup_mode = (ipe_lookup_ctl.ip_da_lookup_ctl1 >> 7) & 0x3;
    if (IS_BIT_SET(lookup_mode, 1))
    {
        p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPMC_TCAM_LOOKUP;
    }

    lookup_mode = (ipe_lookup_ctl.ip_da_lookup_ctl3 >> 7) & 0x3;
    if (IS_BIT_SET(lookup_mode, 1))
    {
        p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPMC_TCAM_LOOKUP;
    }

    p_gb_ipmc_master[lchip]->ipv4_router_key_tbl_id = (ipe_lookup_ctl.ip_da_lookup_ctl1 >> 2) & 0x7;
    p_gb_ipmc_master[lchip]->ipv4_router_key_sub_tbl_id = ipe_lookup_ctl.ip_da_lookup_ctl1 & 0x3;
    p_gb_ipmc_master[lchip]->ipv6_router_key_tbl_id = (ipe_lookup_ctl.ip_da_lookup_ctl3 >> 2) & 0x7;
    p_gb_ipmc_master[lchip]->ipv6_router_key_sub_tbl_id = ipe_lookup_ctl.ip_da_lookup_ctl3 & 0x3;

    p_gb_ipmc_master[lchip]->mac_ipv4_key_tbl_id = (ipe_lookup_ctl.mac_ipv4_lookup_ctl >> 2) & 0x7;
    p_gb_ipmc_master[lchip]->mac_ipv4_key_sub_tbl_id = ipe_lookup_ctl.mac_ipv4_lookup_ctl & 0x3;
    p_gb_ipmc_master[lchip]->mac_ipv6_key_tbl_id = (ipe_lookup_ctl.mac_ipv6_lookup_ctl >> 2) & 0x7;
    p_gb_ipmc_master[lchip]->mac_ipv6_key_sub_tbl_id = ipe_lookup_ctl.mac_ipv6_lookup_ctl & 0x3;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4], &table_size));
    if (0 == table_size)
    {
        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP))
        {
            CTC_UNSET_FLAG(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP);
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6], &table_size));
    if (0 == table_size)
    {
        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP))
        {
            CTC_UNSET_FLAG(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP);
        }
    }

    p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_4] = (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP)) ?
        TRUE : FALSE;
    p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_6] = (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP)) ?
        TRUE : FALSE;

    if (!p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_4] && !p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_6])
    {
        mem_free(p_gb_ipmc_master[lchip]);
        return CTC_E_NONE;
    }

    p_gb_ipmc_master[lchip]->is_sort_mode = sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_ipmc_db_init(lchip));

    /* deal with default entry*/
    for (ip_ver = 0; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        ret = (p_gb_ipmc_master[lchip]->version_en[ip_ver] && (ret == 0)) ? sys_greatbelt_ipmc_add_default_entry(lchip, ip_ver, CTC_IPMC_DEFAULT_ACTION_DROP) : ret;
    }

    if (ret < 0)
    {
        mem_free(p_gb_ipmc_master[lchip]);
        return ret;
    }

    CTC_ERROR_RETURN(sys_greatbelt_rpf_init(lchip));

    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_IPMC, sys_greatbelt_ipmc_tcam_reinit);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_ipmc_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_rpf_deinit(lchip);
    sys_greatbelt_ipmc_db_deinit(lchip);

    sal_mutex_destroy(p_gb_ipmc_master[lchip]->p_ipmc_mutex);
    mem_free(p_gb_ipmc_master[lchip]);

    return CTC_E_NONE;
}


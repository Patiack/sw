/**
 @file sys_goldengate_ipmc.c

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

#include "sys_goldengate_sort.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_l3_hash.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_ipmc.h"
#include "sys_goldengate_ipmc_db.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_IS_LOOP_PORT(port) ((SYS_RSV_PORT_ILOOP_ID == port) || (SYS_RSV_PORT_IP_TUNNEL == port))

sys_ipmc_master_t* p_gg_ipmc_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_ipmc_db_master_t* p_gg_ipmc_db_master[];

extern int32
sys_goldengate_nh_remove_all_members(uint8 lchip,  uint32 nhid);
extern int32
sys_goldengate_ipmc_wb_sync(uint8 lchip);
extern int32
sys_goldengate_ipmc_wb_restore(uint8 lchip);
extern int32
_sys_goldengate_ipmc_construct_ipv6_hash_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node,
                        DsFibHost0Ipv6McastHashKey_m* ds_fib_host0_ipv6_mcast_hash_key,
                        DsFibHost0MacIpv6McastHashKey_m* ds_fib_host0_mac_ipv6_mcast_hash_key);

int32
_sys_goldengate_ipmc_check(uint8 lchip, ctc_ipmc_group_info_t* p_group, uint8 support_def_entry)
{
    uint8 ip_version = 0;
    uint32 ipmc_use_vlan = 0;
    uint32 cmd = 0;
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);

    cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_ipmcUseVlan_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipmc_use_vlan));

    if (!(ipmc_use_vlan || CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (support_def_entry && (0 == p_group->src_ip_mask_len) && (0 == p_group->group_ip_mask_len))
    {
        /* add per vrf default entry */
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

        SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
      /*   SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);*/
    }

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
    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_BIDPIM_CHECK))
    {
        return CTC_E_INVALID_PARAM;
    }
    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        return CTC_E_INVALID_PARAM;
    }
    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_BIDPIM_CHECK) \
        && !CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_BIDIPIM_CHECK(p_group->rp_id);
    }
    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_STATS)
        && CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_WITH_NEXTHOP))
    {
        sys_nh_info_dsnh_t nh_info;
        if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_SHARE_GROUP) || CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) || !p_group->nhid)
        {
            return CTC_E_INVALID_PARAM;
        }
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_group->nhid, &nh_info));
        if ((p_group->src_ip_mask_len !=0 || p_group->group_ip_mask_len !=0) && (nh_info.nh_entry_type != SYS_NH_TYPE_MCAST))
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_update_ipda(uint8 lchip, sys_ipmc_group_node_t* p_group_node, uint8 type, void* data)
{
    uint32 value1 = 0;
    uint32 value2 = 0;
    uint32 cmd1;
    uint32 cmd2;
    sys_rpf_rslt_t* p_rpf_rslt;

    CTC_PTR_VALID_CHECK(p_group_node);

    switch(type)
    {
    case SYS_IPMC_IPDA_RPF_CHECK :
        CTC_PTR_VALID_CHECK(data);
        p_rpf_rslt = (sys_rpf_rslt_t*)data;
        cmd1 = DRV_IOW(DsIpDa_t, DsIpDa_rpfCheckMode_f);
        cmd2 = DRV_IOW(DsIpDa_t, DsIpDa_rpfIfId_f);
        value1 = p_rpf_rslt->mode;
        value2 = p_rpf_rslt->id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->ad_index, cmd1, &value1));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->ad_index, cmd2, &value2));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_ipmc_write_ipda(uint8 lchip, sys_ipmc_group_node_t* p_group_node, void* data)
{

    uint32 cmd;
    DsIpDa_m ds_ip_da; /* ds_ipv4_mcast_da_tcam_t is as same as DsIpDa_m */
    DsMac_m ds_mac;
    uint32 ad_index;
    uint32 value = 0;
    uint16 stats_ptr = 0;
    uint32 dsfwd_offset;
    sys_rpf_rslt_t* p_rpf_rslt;
    uint8 speed = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    sal_memset(&ds_ip_da, 0, sizeof(ds_ip_da));
    sal_memset(&ds_mac, 0, sizeof(ds_mac));

     speed = 0;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] _sys_goldengate_ipmc_write_ipda\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "IPMC: p_group_node->sa_index  %d\r\n", p_group_node->sa_index);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "IPMC: p_group_node->ad_index  %d\r\n", p_group_node->ad_index);
    ad_index = p_group_node->ad_index;
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        if (p_group_node->is_group_exist)
        {
            cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_mac));
        }
        else
        {
            SetDsMac(V, u1_g2_adNextHopPtr_f, &ds_mac, 0);
        }

        if (p_group_node->group_id)
        {
            SetDsMac(V, nextHopPtrValid_f, &ds_mac, 1);
            SetDsMac(V, u1_g2_adDestMap_f, &ds_mac, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, p_group_node->group_id));
        }

        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_mac));
    }
    else
    {
        if (p_group_node->is_group_exist)
        {
            cmd = DRV_IOR(DsIpDa_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_ip_da));

            SetDsIpDa(V, nextHopPtrValid_f, &ds_ip_da, 1);
            SetDsIpDa(V, u1_g1_dsFwdPtr_f, &ds_ip_da, 0);
        }

        if (p_group_node->flag & 0x3)
        {
            /* TBD */
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_TTL_CHECK);
            SetDsIpDa(V, ttlCheckEn_f, &ds_ip_da, value);
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU);
            SetDsIpDa(V, ipDaExceptionEn_f, &ds_ip_da, value);
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_PTP_ENTRY);
            SetDsIpDa(V, selfAddress_f, &ds_ip_da, value);

            SetDsIpDa(V, nextHopPtrValid_f, &ds_ip_da, 1);
            SetDsIpDa(V, u1_g2_adNextHopPtr_f, &ds_ip_da, 0);

            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_WITH_NEXTHOP))
            {
                sys_nh_info_dsnh_t nh_info;
                sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
                CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_group_node->nexthop_id, &nh_info));
                SetDsIpDa(V, nextHopPtrValid_f, &ds_ip_da, 1);
                SetDsIpDa(V, u1_g2_adDestMap_f, &ds_ip_da, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, nh_info.dest_id));
            }
            else if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP))
            {
                uint8 gchip = 0;
                sys_goldengate_get_gchip_id(lchip, &gchip);
                SetDsIpDa(V, u1_g2_adDestMap_f, &ds_ip_da, SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_DROP_ID));
            }
            else
            {
                SetDsIpDa(V, u1_g2_adDestMap_f, &ds_ip_da, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, p_group_node->group_id));
            }

            /* rpf check */
            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
            {
                CTC_PTR_VALID_CHECK(data);
                p_rpf_rslt = (sys_rpf_rslt_t*)data;
                SetDsIpDa(V, rpfCheckEn_f, &ds_ip_da, 1);
                SetDsIpDa(V, rpfIfId_f, &ds_ip_da, p_rpf_rslt->id);
                SetDsIpDa(V, rpfCheckMode_f, &ds_ip_da, p_rpf_rslt->mode);
                if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU))
                {
                    SetDsIpDa(V, mcastRpfFailCpuEn_f, &ds_ip_da, 1);
                }
            }
            /* bidipim check */
            else if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_BIDPIM_CHECK))
            {
                SetDsIpDa(V, bidiPimGroupValid_f, &ds_ip_da, 1);
                SetDsIpDa(V, rpfIfId_f, &ds_ip_da, p_group_node->rp_id);
                if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_BIDIPIM_FAIL_BRIDGE_AND_TOCPU))
                {
                    SetDsIpDa(V, mcastRpfFailCpuEn_f, &ds_ip_da, 1);
                }
            }
            /* stats */
            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_STATS))
            {
                CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_group_node->stats_id, CTC_STATS_STATSID_TYPE_IPMC, &stats_ptr));
                SetDsIpDa(V, ipmcStatsEn_f, &ds_ip_da, 1);
                SetDsIpDa(V, u1_g2_adNextHopPtr_f, &ds_ip_da, stats_ptr);
            }
        }
        else
        {
            /* TBD */
            ad_index = p_group_node->ad_index;
            /* rpf check */
            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
            {
                p_rpf_rslt = (sys_rpf_rslt_t*)data;
                value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK);
                SetDsIpDa(V, rpfCheckEn_f, &ds_ip_da, value);
                SetDsIpDa(V, rpfIfId_f, &ds_ip_da, p_rpf_rslt->id);
                SetDsIpDa(V, rpfCheckMode_f, &ds_ip_da, p_rpf_rslt->mode);
            }

            SetDsIpDa(V, isDefaultRoute_f, &ds_ip_da, 1);
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_TTL_CHECK);
            SetDsIpDa(V, ttlCheckEn_f, &ds_ip_da, value);
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_PTP_ENTRY);
            SetDsIpDa(V, selfAddress_f, &ds_ip_da, value);
            value = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU);
            SetDsIpDa(V, ipDaExceptionEn_f, &ds_ip_da, value);
            SetDsIpDa(V, exception3CtlEn_f, &ds_ip_da, 1);
            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU))
            {
                SetDsIpDa(V, mcastRpfFailCpuEn_f, &ds_ip_da, 1);
            }
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_group_node->nexthop_id, &dsfwd_offset));
        }

        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU))
        {
            /*to CPU must be equal to 63(get the low 5bits is 31), refer to sys_goldengate_pdu.h:SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU*/
            SetDsIpDa(V, exceptionSubIndex_f, &ds_ip_da, 63 & 0x1F);
        }

        cmd = DRV_IOW(DsIpDa_t, DRV_ENTRY_FLAG);

        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_REDIRECT_TOCPU))
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, &dsfwd_offset));
        }

        if (p_group_node->flag & 0x3)
        {
            if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_REDIRECT_TOCPU)&&!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_WITH_NEXTHOP))
            {
                SetDsIpDa(V, nextHopPtrValid_f, &ds_ip_da, 0);
                SetDsIpDa(V, u1_g1_dsFwdPtr_f, &ds_ip_da, dsfwd_offset);
            }
        }
        else
        {
            SetDsIpDa(V, nextHopPtrValid_f, &ds_ip_da, 0);
            SetDsIpDa(V, u1_g1_dsFwdPtr_f, &ds_ip_da, dsfwd_offset);
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &ds_ip_da));
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_write_ipsa(uint8 lchip, sys_ipmc_group_node_t* p_group_node, sys_ipmc_rpf_info_t* p_sys_ipmc_rpf_info)
{
    uint32 cmd;
    DsRpf_m ds_rpf;

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "[PROCESS] sys_ipmc_write_ipsa\n");
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    CTC_PTR_VALID_CHECK(p_sys_ipmc_rpf_info);

    sal_memset(&ds_rpf, 0, sizeof(ds_rpf));

    /* ipv4 and ipv6 share the same data structure */
    SetDsRpf(V, array_0_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[0]);
    SetDsRpf(V, array_1_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[1]);
    SetDsRpf(V, array_2_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[2]);
    SetDsRpf(V, array_3_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[3]);
    SetDsRpf(V, array_4_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[4]);
    SetDsRpf(V, array_5_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[5]);
    SetDsRpf(V, array_6_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[6]);
    SetDsRpf(V, array_7_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[7]);
    SetDsRpf(V, array_8_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[8]);
    SetDsRpf(V, array_9_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[9]);
    SetDsRpf(V, array_10_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[10]);
    SetDsRpf(V, array_11_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[11]);
    SetDsRpf(V, array_12_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[12]);
    SetDsRpf(V, array_13_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[13]);
    SetDsRpf(V, array_14_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[14]);
    SetDsRpf(V, array_15_rpfIfId_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf[15]);

    SetDsRpf(V, array_0_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[0]);
    SetDsRpf(V, array_1_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[1]);
    SetDsRpf(V, array_2_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[2]);
    SetDsRpf(V, array_3_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[3]);
    SetDsRpf(V, array_4_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[4]);
    SetDsRpf(V, array_5_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[5]);
    SetDsRpf(V, array_6_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[6]);
    SetDsRpf(V, array_7_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[7]);
    SetDsRpf(V, array_8_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[8]);
    SetDsRpf(V, array_9_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[9]);
    SetDsRpf(V, array_10_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[10]);
    SetDsRpf(V, array_11_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[11]);
    SetDsRpf(V, array_12_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[12]);
    SetDsRpf(V, array_13_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[13]);
    SetDsRpf(V, array_14_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[14]);
    SetDsRpf(V, array_15_rpfIfIdValid_f, &ds_rpf, p_sys_ipmc_rpf_info->rpf_intf_valid[15]);

    cmd = DRV_IOW(DsRpf_t, DRV_ENTRY_FLAG);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write rpf profile index: 0x%x \n", p_group_node->sa_index);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->sa_index, cmd, &ds_rpf));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_ipv4_write_key(uint8 lchip, uint8 write_base, sys_ipmc_group_node_t* p_group_node)
{
    uint32 index = 0;
    uint32 default_index = 0;
    uint32 hashtype = 0;
    uint32 ipv4type = 0;
    uint32 tbl_id = 0;
    uint32 ad_index = 0;
    drv_cpu_acc_in_t  lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv4HashKey_m ds_fib_host0_ipv4_hash_key;
    DsFibHost1Ipv4McastHashKey_m ds_fib_host1_ipv4_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(&ds_fib_host0_ipv4_hash_key, 0, sizeof(ds_fib_host0_ipv4_hash_key));
    sal_memset(&ds_fib_host1_ipv4_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv4_mcast_hash_key));

    SYS_IPMC_DBG_FUNC();
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    ad_index = p_group_node->ad_index;
    /* (*,g) */
    if (write_base)
    {
        default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
        ((p_gg_ipmc_master[lchip]->ipv4_l2mc_default_index << 8) + p_group_node->address.ipv4.vrfid): p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index+p_group_node->address.ipv4.vrfid;

        index = CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? default_index : ad_index;
        SetDsFibHost0Ipv4HashKey(V, dsAdIndex_f, &ds_fib_host0_ipv4_hash_key, index);
        hashtype = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 1 : 1;
        SetDsFibHost0Ipv4HashKey(V, hashType_f, &ds_fib_host0_ipv4_hash_key, hashtype);
        SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.group_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ds_fib_host0_ipv4_hash_key, ipv4type);
        SetDsFibHost0Ipv4HashKey(V, pointer_f, &ds_fib_host0_ipv4_hash_key, p_group_node->pointer);
        SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 1);
        SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.vrfid);

        key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
        key_acc_in.fib.data = (void*)&ds_fib_host0_ipv4_hash_key;
        key_acc_in.fib.overwrite_en = 1;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
        SYS_IPMC_DBG_INFO("Write FibHost0: key_index:%d, ad_index:%d\n", key_acc_out.fib.key_index, index);

        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
    }

    /* (s,pointer) */
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv4McastHashKey_t : DsFibHost1Ipv4McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_dsAdIndex_f, &ds_fib_host1_ipv4_mcast_hash_key, ad_index);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_hashType_f, &ds_fib_host1_ipv4_mcast_hash_key, 0);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipSa_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->address.ipv4.src_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipv4Type_f, &ds_fib_host1_ipv4_mcast_hash_key, ipv4type);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_pointer_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_valid_f, &ds_fib_host1_ipv4_mcast_hash_key, 1);

        lookup_info.data = (void*)(&ds_fib_host1_ipv4_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            /* (**,g) have been inserted */
            if (write_base)
            {
                SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 0);
                key_acc_in.fib.data = (void*)&ds_fib_host0_ipv4_hash_key;
                key_acc_in.fib.overwrite_en = 1;
                drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
            }
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            /* (**,g) have been inserted */
            if (write_base)
            {
                SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 0);
                key_acc_in.fib.data = (void*)&ds_fib_host0_ipv4_hash_key;
                key_acc_in.fib.overwrite_en = 1;
                drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
            }
            return CTC_E_IPMC_HASH_CONFLICT;
        }
        SYS_IPMC_DBG_INFO("Write FibHost1: lookup_result.key_index:%d ad_index:%d\n", lookup_result.key_index, ad_index);

       #ifdef  EMULATION_PLATFORM
       /* for emulation test host1 aging */
        /*- sys_goldengate_aging_set_aging_status(lchip, 0, SYS_AGING_DOMAIN_IP_HASH, lookup_result.key_index, TRUE);*/

       #endif
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_ipv6_write_key(uint8 lchip, uint8 write_base, sys_ipmc_group_node_t* p_group_node)
{
    uint32 index = 0;
    uint32 default_index = 0;
    uint32 _type = 0;
    uint32 tbl_id = 0;
    uint32 ad_index = 0;
    ipv6_addr_t ipv6_addr;
    drv_cpu_acc_in_t   lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv6McastHashKey_m ds_fib_host0_ipv6_mcast_hash_key;
    DsFibHost0MacIpv6McastHashKey_m ds_fib_host0_mac_ipv6_mcast_hash_key;
    DsFibHost1Ipv6McastHashKey_m ds_fib_host1_ipv6_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));
    sal_memset(&ds_fib_host0_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host0_mac_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_mac_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host1_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv6_mcast_hash_key));

    SYS_IPMC_DBG_FUNC();
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    ad_index = p_group_node->ad_index;
    /*(*,g)*/
    if (write_base)
    {
        default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
            ((p_gg_ipmc_master[lchip]->ipv6_l2mc_default_index << 8)+p_group_node->address.ipv6.vrfid) : p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index+p_group_node->address.ipv6.vrfid;
        index = CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? default_index : ad_index;

        _sys_goldengate_ipmc_construct_ipv6_hash_key(lchip, p_group_node,
                                                &ds_fib_host0_ipv6_mcast_hash_key,
                                                &ds_fib_host0_mac_ipv6_mcast_hash_key);
        /* write host0 hash key */
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            tbl_id = DsFibHost0MacIpv6McastHashKey_t;
            DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_dsAdIndex_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, index);

            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_mac_ipv6_mcast_hash_key;
        }
        else
        {
            tbl_id = DsFibHost0Ipv6McastHashKey_t;
            DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_dsAdIndex_f, &ds_fib_host0_ipv6_mcast_hash_key, index);

            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_ipv6_mcast_hash_key;
        }
        key_acc_in.fib.overwrite_en = 1;

        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
        SYS_IPMC_DBG_INFO("Write ipv6 FibHost0 key_index:%d, ad_index:%d\n", key_acc_out.fib.key_index, index);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
    }


    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv6McastHashKey_t : DsFibHost1Ipv6McastHashKey_t;

        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_dsAdIndex_f, &ds_fib_host1_ipv6_mcast_hash_key, ad_index);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType0_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType1_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        sal_memcpy(&ipv6_addr, &(p_group_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
        DRV_SET_FIELD_A(tbl_id, DsFibHost1Ipv6McastHashKey_ipSa_f, &ds_fib_host1_ipv6_mcast_hash_key, ipv6_addr);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_pointer_f, &ds_fib_host1_ipv6_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid0_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid1_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        _type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 3 : 2;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey__type_f, &ds_fib_host1_ipv6_mcast_hash_key, _type);

        lookup_info.data = (void*)(&ds_fib_host1_ipv6_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            if (write_base)
            {
                if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
                {
                    tbl_id = DsFibHost0MacIpv6McastHashKey_t;
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid0_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid1_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
                    key_acc_in.fib.data = (void*)&ds_fib_host0_mac_ipv6_mcast_hash_key;
                }
                else
                {
                    tbl_id = DsFibHost0Ipv6McastHashKey_t;
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid0_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid1_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
                    key_acc_in.fib.data = (void*)&ds_fib_host0_ipv6_mcast_hash_key;
                }
                key_acc_in.fib.overwrite_en = 1;
                CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out));

            }
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            if (write_base)
            {
                if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
                {
                    tbl_id = DsFibHost0MacIpv6McastHashKey_t;
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid0_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid1_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
                    key_acc_in.fib.data = (void*)&ds_fib_host0_mac_ipv6_mcast_hash_key;
                }
                else
                {
                    tbl_id = DsFibHost0Ipv6McastHashKey_t;
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid0_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
                    DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid1_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
                    key_acc_in.fib.data = (void*)&ds_fib_host0_ipv6_mcast_hash_key;
                }
                key_acc_in.fib.overwrite_en = 1;
                CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out));

            }
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        SYS_IPMC_DBG_INFO("Write ipv6 FibHost1 key_index:%d ad_index:%d\n", lookup_result.key_index, ad_index);

    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_write_key(uint8 lchip, uint8 write_base, sys_ipmc_group_node_t* p_group_node)
{
    uint8  ip_version = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    ip_version = (p_group_node->flag & 0x4) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (CTC_IP_VER_4 == ip_version)
    {
        return _sys_goldengate_ipmc_ipv4_write_key(lchip, write_base, p_group_node);
    }
    else
    {
        return _sys_goldengate_ipmc_ipv6_write_key(lchip, write_base, p_group_node);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_ipv4_remove_key(uint8 lchip, uint8 delete_base, sys_ipmc_group_node_t* p_group_node)
{
    uint32 default_index = 0;
    uint32 ad_index = 0;
    uint32 hashtype = 0;
    uint32 ipv4type = 0;
    uint32 tbl_id = 0;
    drv_cpu_acc_in_t   lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv4HashKey_m ds_fib_host0_ipv4_hash_key;
    DsFibHost1Ipv4McastHashKey_m ds_fib_host1_ipv4_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(&ds_fib_host0_ipv4_hash_key, 0, sizeof(ds_fib_host0_ipv4_hash_key));
    sal_memset(&ds_fib_host1_ipv4_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv4_mcast_hash_key));

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
        ((p_gg_ipmc_master[lchip]->ipv4_l2mc_default_index << 8)+p_group_node->address.ipv4.vrfid) : p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index+p_group_node->address.ipv4.vrfid;

    /* (*,g) delete --> (**,g) */
    if (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        hashtype = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 1 : 1;
        SetDsFibHost0Ipv4HashKey(V, hashType_f, &ds_fib_host0_ipv4_hash_key, hashtype);
        SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.group_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ds_fib_host0_ipv4_hash_key, ipv4type);
        SetDsFibHost0Ipv4HashKey(V, pointer_f, &ds_fib_host0_ipv4_hash_key, p_group_node->pointer);
        SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 1);
        SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.vrfid);

        /* lookup for adindex */
        key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
        key_acc_in.fib.data = (void*)&ds_fib_host0_ipv4_hash_key;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_FIB0, &key_acc_in, &key_acc_out);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
        ad_index = key_acc_out.fib.ad_index;

        /* set to default adindex */
        SetDsFibHost0Ipv4HashKey(V, dsAdIndex_f, &ds_fib_host0_ipv4_hash_key, default_index);
        key_acc_in.fib.overwrite_en = 1;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        /* free adindex */
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
        if (ad_index)
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, tbl_id, CTC_INGRESS, 1, ad_index));
        }
    }

    /*(*,g) or (**,g) delete */
    if (delete_base)
    {
        hashtype = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 1 : 1;
        SetDsFibHost0Ipv4HashKey(V, hashType_f, &ds_fib_host0_ipv4_hash_key, hashtype);
        SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.group_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ds_fib_host0_ipv4_hash_key, ipv4type);
        SetDsFibHost0Ipv4HashKey(V, pointer_f, &ds_fib_host0_ipv4_hash_key, p_group_node->pointer);
        SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 0);
        SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.vrfid);

        key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
        key_acc_in.fib.overwrite_en = 1;
        key_acc_in.fib.data = (void*)&ds_fib_host0_ipv4_hash_key;

        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);

        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
    }


    /* (s,pointer) */
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv4McastHashKey_t : DsFibHost1Ipv4McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_hashType_f, &ds_fib_host1_ipv4_mcast_hash_key, 0);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipSa_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->address.ipv4.src_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipv4Type_f, &ds_fib_host1_ipv4_mcast_hash_key, ipv4type);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_pointer_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_valid_f, &ds_fib_host1_ipv4_mcast_hash_key, 1);

        /* lookup key index first */
        lookup_info.data = (void*)(&ds_fib_host1_ipv4_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            /* (**,g) have been inserted */
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        /* lookup adindex than */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));
        ad_index = GetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, &lookup_result.data);

        /* delete key */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));

        /* free adindex */
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
        if (ad_index)
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, tbl_id, CTC_INGRESS, 1, ad_index));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_ipv6_remove_key(uint8 lchip, uint8 delete_base, sys_ipmc_group_node_t* p_group_node)
{
    uint32 ad_index = 0;
    uint32 default_index = 0;
    uint32 _type = 0;
    uint32 tbl_id = 0;
    ipv6_addr_t ipv6_addr;
    drv_cpu_acc_in_t   lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv6McastHashKey_m ds_fib_host0_ipv6_mcast_hash_key;
    DsFibHost0MacIpv6McastHashKey_m ds_fib_host0_mac_ipv6_mcast_hash_key;
    DsFibHost1Ipv6McastHashKey_m ds_fib_host1_ipv6_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));
    sal_memset(&ds_fib_host0_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host0_mac_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_mac_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host1_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv6_mcast_hash_key));
    default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
        ((p_gg_ipmc_master[lchip]->ipv6_l2mc_default_index << 8)+p_group_node->address.ipv6.vrfid) : p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index+p_group_node->address.ipv6.vrfid;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    /* (*,g) delete --> (**,g) */
    if (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        _sys_goldengate_ipmc_construct_ipv6_hash_key(lchip, p_group_node,
                                                &ds_fib_host0_ipv6_mcast_hash_key,
                                                &ds_fib_host0_mac_ipv6_mcast_hash_key);
        /* lookup for adindex */
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            tbl_id = DsFibHost0MacIpv6McastHashKey_t;
            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_mac_ipv6_mcast_hash_key;
        }
        else
        {
            tbl_id = DsFibHost0Ipv6McastHashKey_t;
            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_ipv6_mcast_hash_key;
        }
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_FIB0, &key_acc_in, &key_acc_out);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
        ad_index = key_acc_out.fib.ad_index;

        /* set to default adindex */
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            SetDsFibHost0MacIpv6McastHashKey(V, dsAdIndex_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, default_index);
        }
        else
        {
            SetDsFibHost0Ipv6McastHashKey(V, dsAdIndex_f, &ds_fib_host0_ipv6_mcast_hash_key, default_index);
        }
        key_acc_in.fib.overwrite_en = 1;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        /* free adindex */
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
        if (ad_index)
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, tbl_id, CTC_INGRESS, 1, ad_index));
        }
    }

    /*(*,g) or (**,g) delete */
    if (delete_base)
    {
        _sys_goldengate_ipmc_construct_ipv6_hash_key(lchip, p_group_node,
                                                &ds_fib_host0_ipv6_mcast_hash_key,
                                                &ds_fib_host0_mac_ipv6_mcast_hash_key);
        /* lookup for adindex */
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            tbl_id = DsFibHost0MacIpv6McastHashKey_t;
            DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid0_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
            DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid1_f, &ds_fib_host0_mac_ipv6_mcast_hash_key, 0);
            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_mac_ipv6_mcast_hash_key;
        }
        else
        {
            tbl_id = DsFibHost0Ipv6McastHashKey_t;
            DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid0_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
            DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid1_f, &ds_fib_host0_ipv6_mcast_hash_key, 0);
            key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6MCAST;
            key_acc_in.fib.data = (void*)&ds_fib_host0_ipv6_mcast_hash_key;
        }
        key_acc_in.fib.overwrite_en = 1;

        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &key_acc_in, &key_acc_out);
        if (key_acc_out.fib.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }
    }


    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv6McastHashKey_t : DsFibHost1Ipv6McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType0_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType1_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        sal_memcpy(&ipv6_addr, &(p_group_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
        DRV_SET_FIELD_A(tbl_id, DsFibHost1Ipv6McastHashKey_ipSa_f, &ds_fib_host1_ipv6_mcast_hash_key, ipv6_addr);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_pointer_f, &ds_fib_host1_ipv6_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid0_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid1_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        _type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 3 : 2;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey__type_f, &ds_fib_host1_ipv6_mcast_hash_key, _type);

        /* lookup key index first */
        lookup_info.data = (void*)(&ds_fib_host1_ipv6_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.conflict == TRUE)
        {
            return CTC_E_IPMC_HASH_CONFLICT;
        }

        /* lookup adindex than */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));
        ad_index = GetDsFibHost1Ipv6McastHashKey(V, dsAdIndex_f, &lookup_result.data);

        /* delete key */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));

        /* free adindex */
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
        if (ad_index)
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, tbl_id, CTC_INGRESS, 1, ad_index));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_remove_key(uint8 lchip, uint8 delete_base, sys_ipmc_group_node_t* p_group_node)
{
    uint8  ip_version = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    ip_version = (p_group_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (CTC_IP_VER_4 == ip_version)
    {
         /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"_sys_goldengate_ipmc_remove_key\n");*/
        return _sys_goldengate_ipmc_ipv4_remove_key(lchip, delete_base, p_group_node);
    }
    else
    {
        return _sys_goldengate_ipmc_ipv6_remove_key(lchip, delete_base, p_group_node);
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_ipmc_add_group(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint8 ip_version = 0;
    int32 ret = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 rpf_check_port = 0;
    uint8 i = 0;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    sys_ipmc_group_db_node_t* p_sg_group_node = NULL;
    sys_nh_param_mcast_group_t  nh_mcast_group;
    sys_ipmc_rpf_info_t sys_ipmc_rpf_info;
    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;
    sys_rpf_intf_t intf;
    ctc_ipmc_group_info_t *p_group = NULL;
    uint8 with_nexthop = 0;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(group_node));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    sal_memset(&sys_ipmc_rpf_info, 0, sizeof(sys_ipmc_rpf_info_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 1));

    /*ipmc db lookup*/
    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    IPMC_LOCK;

    if (p_gg_ipmc_master[lchip]->ipmc_group_cnt >= SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPMC))
    {
        IPMC_UNLOCK;
        return CTC_E_NO_RESOURCE;
    }

    ip_version = p_group->ip_version;
    with_nexthop = CTC_FLAG_ISSET(p_group_info->flag, CTC_IPMC_FLAG_WITH_NEXTHOP)?1:0;
    if ((0 == p_group->src_ip_mask_len) && (0 == p_group->group_ip_mask_len))
    {
        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
        {
            sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
            sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
            sal_memset(&intf, 0, sizeof(sys_rpf_intf_t));

            rpf_info.intf = &intf;

            rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));
        }

        p_group_node->nexthop_id = with_nexthop?p_group_info->nhid:SYS_NH_RESOLVED_NHID_FOR_DROP;

        if (ip_version == CTC_IP_VER_6)
        {
            p_group_node->ad_index = p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index + p_group->address.ipv6.vrfid;
        }
        else
        {
            p_group_node->ad_index = p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index + p_group->address.ipv4.vrfid;
        }

        if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
        {
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_write_ipda(lchip, p_group_node, &rpf_rslt));
        }
        else
        {
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_write_ipda(lchip, p_group_node, NULL));
        }

        IPMC_UNLOCK;
        return CTC_E_NONE;
    }

    ret = _sys_goldengate_ipmc_asic_lookup(lchip, p_group_node);
    if((CTC_E_NONE == ret) && (!CTC_FLAG_ISSET(p_group_info->flag, CTC_IPMC_FLAG_SHARE_GROUP)))
    {
        /* The group to add has already existed*/
        CTC_RETURN_IPMC_UNLOCK(CTC_E_IPMC_GROUP_HAS_EXISTED);
    }
    else if ((CTC_E_IPMC_GROUP_NOT_EXIST != ret) && (!CTC_FLAG_ISSET(p_group_info->flag, CTC_IPMC_FLAG_SHARE_GROUP)))
    {
        CTC_RETURN_IPMC_UNLOCK(ret);
    }

    /* if the group to add currently doesn't exist, creat it, and add into database */
    /* create global node */
    sal_memset(p_group_node, 0, p_gg_ipmc_master[lchip]->info_size[ip_version]);
    ret = _sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node);
    if (ret < 0)
    {
        CTC_RETURN_IPMC_UNLOCK(ret);
    }

    if (CTC_FLAG_ISSET(p_group_info->flag, CTC_IPMC_FLAG_SHARE_GROUP))
    {
        if (p_group_info->group_id)
        {
            CTC_ERROR_GOTO(sys_goldengate_nh_get_mcast_nh(lchip, p_group_info->group_id, &p_group_node->nexthop_id), ret, error0);
        }
        else if (!p_group_node->is_group_exist)
        {
            CTC_SET_FLAG(p_group_info->flag, CTC_IPMC_FLAG_DROP);
        }
    }
    else if (CTC_FLAG_ISSET(p_group_info->flag, CTC_IPMC_FLAG_DROP) && !with_nexthop)/*dummy entry, no need to creat nexthop*/
    {
        p_group_node->nexthop_id = 0;
    }
    else if (!with_nexthop) /* creat Nexthop for IP Mcast according to Group id */
    {
        sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));
        ret = sys_goldengate_mcast_nh_create(lchip, p_group->group_id,  &nh_mcast_group);
        if (ret < 0)
        {
            CTC_RETURN_IPMC_UNLOCK(ret);
        }
        p_group_node->nexthop_id = nh_mcast_group.nhid;
    }

    /* get rpf index:sa_index */
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf, p_group->rpf_intf, sizeof(sys_ipmc_rpf_info.rpf_intf));
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf_valid, p_group->rpf_intf_valid, sizeof(sys_ipmc_rpf_info.rpf_intf_valid));
    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&
        !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        cmd = DRV_IOR(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &rpf_check_port));

        if (rpf_check_port)
        {
            for (i=0; i<SYS_GG_MAX_IPMC_RPF_IF; i++)
            {
                if (p_group->rpf_intf_valid[i])
                {
                    sys_ipmc_rpf_info.rpf_intf[i] = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_group->rpf_intf[i]);
                }
            }
        }
        rpf_info.force_profile = FALSE;
        rpf_info.usage = SYS_RPF_USAGE_TYPE_IPMC;
        rpf_info.profile_index = SYS_RPF_INVALID_PROFILE_ID;
        rpf_info.intf = (sys_rpf_intf_t*)&sys_ipmc_rpf_info;

        ret = sys_goldengate_rpf_add_profile(lchip, &rpf_info, &rpf_rslt);
        if (ret < 0)
        {
            sys_goldengate_mcast_nh_delete(lchip, p_group_node->nexthop_id);
            CTC_RETURN_IPMC_UNLOCK(ret);
        }

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

    /* add (**,g)/(*,g) sw node ;; (s,g) node dont need be stored */
    ret = _sys_goldengate_ipmc_add_g_db_node(lchip, p_group_node, &p_base_group_node);
    if (ret < 0)
    {
        sys_goldengate_mcast_nh_delete(lchip, (with_nexthop?0:p_group_node->nexthop_id));
        if (p_group_node->sa_index > 0)
        {
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_rpf_remove_profile(lchip, p_group_node->sa_index));
        }
        CTC_RETURN_IPMC_UNLOCK(ret);
    }
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
         ret = _sys_goldengate_ipmc_add_sg_db_node(lchip, p_group_node, &p_sg_group_node);
         if (p_sg_group_node == NULL)
         {
             sys_goldengate_mcast_nh_delete(lchip, (with_nexthop?0:p_group_node->nexthop_id));
             sys_goldengate_ipmc_db_remove(lchip, p_group_node);
             if (p_base_group_node->refer_count == 0)
             {
                 mem_free(p_base_group_node);
             }
             CTC_RETURN_IPMC_UNLOCK(ret);
         }
         else if (ret != CTC_E_NONE)
         {
             sys_goldengate_mcast_nh_delete(lchip, (with_nexthop?0:p_group_node->nexthop_id));
             sys_goldengate_ipmc_db_remove(lchip, p_group_node);
             if (p_base_group_node->refer_count == 0)
             {
                 mem_free(p_base_group_node);
             }
             mem_free(p_sg_group_node);
             CTC_RETURN_IPMC_UNLOCK(ret);
         }
    }

    if (p_group_node->is_group_exist)
    {
        ret = _sys_goldengate_ipmc_write_ipda(lchip, p_group_node, NULL);

        sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
        if (p_base_group_node->group_id != p_group_info->group_id)
        {
            p_base_group_node->group_id = p_group_info->group_id;
        }
        goto error0;
    }

    /* write group info to Asic hardware table : dsipda, dsrpf, key */
    tbl_id = CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
    ret = sys_goldengate_ftm_alloc_table_offset(lchip, tbl_id, CTC_INGRESS, 1, &(p_group_node->ad_index));
    if (ret < 0)
    {
        sys_goldengate_mcast_nh_delete(lchip, (with_nexthop?0:p_group_node->nexthop_id));
        sys_goldengate_ipmc_db_remove(lchip, p_group_node);
        if(p_base_group_node->refer_count == 0)
        {
            mem_free(p_base_group_node);
        }
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
        {
            _sys_goldengate_ipmc_db_remove(lchip, p_sg_group_node);
            mem_free(p_sg_group_node);
        }
        CTC_RETURN_IPMC_UNLOCK(ret);
    }
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
    {
        ret = _sys_goldengate_ipmc_write_ipda(lchip, p_group_node, &rpf_rslt);
    }
    else
    {
        ret = _sys_goldengate_ipmc_write_ipda(lchip, p_group_node, NULL);
    }

    /* write rpf */
    ret = (ret || (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_RPF_CHECK)) ||
           CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) ||
           (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))) ? ret :
        _sys_goldengate_ipmc_write_ipsa(lchip, p_group_node, &sys_ipmc_rpf_info);

    /* write key */
    ret = ret ? ret :
        _sys_goldengate_ipmc_write_key(lchip, p_group_node->write_base, p_group_node);

    if (ret < 0)
    {
        tbl_id = CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC) ? DsMac_t : DsIpDa_t;
        sys_goldengate_ftm_free_table_offset(lchip, tbl_id, CTC_INGRESS, 1, p_group_node->ad_index);
        sys_goldengate_mcast_nh_delete(lchip, (with_nexthop?0:p_group_node->nexthop_id));
        sys_goldengate_ipmc_db_remove(lchip, p_group_node);
        if(p_base_group_node->refer_count == 0)
        {
            mem_free(p_base_group_node);
        }
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
        {
            _sys_goldengate_ipmc_db_remove(lchip, p_sg_group_node);
            mem_free(p_sg_group_node);
        }
        CTC_RETURN_IPMC_UNLOCK(ret);
    }

    p_gg_ipmc_master[lchip]->ipmc_group_cnt++;
error0:
    IPMC_UNLOCK;
    return ret;
}

int32
sys_goldengate_ipmc_remove_group(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint8 delete_base = 0;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    ctc_ipmc_group_info_t *p_group = NULL;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 1));

    /* (*,g) (**,g) node lookup */
    {
        p_group_node = &group_node;
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
        IPMC_LOCK;

        if ((0 == p_group->src_ip_mask_len) && (0 == p_group->group_ip_mask_len))
        {

            p_group_node->nexthop_id = SYS_NH_RESOLVED_NHID_FOR_DROP;

            if (p_group->ip_version == CTC_IP_VER_6)
            {
                p_group_node->ad_index = p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index + p_group->address.ipv6.vrfid;
            }
            else
            {
                p_group_node->ad_index = p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index + p_group->address.ipv4.vrfid;
            }

            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_write_ipda(lchip, p_group_node, NULL));

            IPMC_UNLOCK;
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_asic_lookup(lchip, p_group_node));
        p_base_group_node = &base_group_node;
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));
        UNSET_FLAG(p_base_group_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR);
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION))
        {
            sal_memset(&(p_base_group_node->address.ipv6.src_addr), 0, sizeof(ipv6_addr_t));
        }
        else
        {
            sal_memset(&(p_base_group_node->address.ipv4.src_addr), 0, sizeof(uint32));
        }
        sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
        if (NULL == p_base_group_node)
        {
            IPMC_UNLOCK;
            return CTC_E_INVALID_PTR;
        }

        if (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
        {
               p_group_node->nexthop_id = p_base_group_node->nexthop_id;
        }


        if (p_base_group_node->refer_count == 1)
        {
             delete_base = TRUE;
        }
    }

    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_KEEP_EMPTY_ENTRY))
    {
        CTC_RETURN_IPMC_UNLOCK(sys_goldengate_nh_remove_all_members(lchip, p_base_group_node->nexthop_id));

        /* TBD*/
        /* remove ALL members belonged to this group */
        /* sys_goldengate_mc_remove_all_member(lchip, p_group_node->local_member_list, p_group_node->nexthop_id); */
        IPMC_UNLOCK;
        return CTC_E_NONE;
    }

    /* remove key from TCAM, don't care Associate Table */
    /* (s,g) (*,g) node remove */
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_remove_key(lchip, delete_base, p_group_node));

    /* remove group node from database */
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_ipmc_db_remove(lchip, p_group_node));
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_ipmc_sg_db_remove(lchip, p_group_node));
    }
    /* remove Nexthop for according to Nexthop id */
     /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_goldengate_mcast_nh_delete\n");*/
    if (!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP) && !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_SHARE_GROUP))
    {
        sys_goldengate_mcast_nh_delete(lchip, p_group_node->nexthop_id);
    }
    if (p_base_group_node->refer_count == 0)
    {
        mem_free(p_base_group_node);
    }

    if(p_gg_ipmc_master[lchip]->ipmc_group_cnt)
    {
        p_gg_ipmc_master[lchip]->ipmc_group_cnt--;
    }
    IPMC_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_update_member(uint8 lchip, ctc_ipmc_member_info_t* ipmc_member, sys_nh_param_mcast_group_t* nh_mcast_group, uint8  ip_l2mc)
{
    int32 ret = 0;
    uint8 aps_brg_en = 0;
    uint32 dest_id = 0;

    if (ipmc_member->re_route)
    {
        nh_mcast_group->mem_info.destid  = SYS_RSV_PORT_IP_TUNNEL;
        nh_mcast_group->mem_info.member_type = SYS_NH_PARAM_IPMC_MEM_LOOP_BACK;
        dest_id = SYS_RSV_PORT_IP_TUNNEL;
    }
    else if (ipmc_member->is_nh)
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
            CTC_ERROR_RETURN(sys_goldengate_nh_get_port(lchip, ipmc_member->nh_id, &aps_brg_en, &dest_id));
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
        else if (ipmc_member->vlan_port
            || (ip_l2mc && (ipmc_member->l3_if_type == MAX_L3IF_TYPE_NUM)))
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
    }

    nh_mcast_group->mem_info.leaf_check_en = CTC_FLAG_ISSET(ipmc_member->flag, CTC_IPMC_MEMBER_FLAG_LEAF_CHECK_EN);
    nh_mcast_group->mem_info.mtu_no_chk = CTC_FLAG_ISSET(ipmc_member->flag, CTC_IPMC_MEMBER_FLAG_MTU_NO_CHECK);

    if (0 != ipmc_member->logic_dest_port)
    {
        nh_mcast_group->mem_info.logic_port = ipmc_member->logic_dest_port;
        nh_mcast_group->mem_info.is_logic_port = 1;
    }

    if (aps_brg_en || CTC_IS_LINKAGG_PORT(dest_id) || SYS_IS_LOOP_PORT(dest_id))
    {
        if (CTC_IS_LINKAGG_PORT(dest_id))
        {
            nh_mcast_group->mem_info.is_linkagg  = 1;
        }
        else
        {
            nh_mcast_group->mem_info.is_linkagg  = 0;
        }

        nh_mcast_group->mem_info.lchip = lchip;
        ret = sys_goldengate_mcast_nh_update(lchip, nh_mcast_group);
        if (ret < 0)
        {
            return ret;
        }
    }
    else
    {
        if (FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(dest_id)))
        {
            return CTC_E_NONE;
        }

        nh_mcast_group->mem_info.lchip = lchip;
        nh_mcast_group->mem_info.is_linkagg = 0;
        ret = sys_goldengate_mcast_nh_update(lchip, nh_mcast_group);
        if (ret < 0)
        {
            return ret;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_update_member_with_bitmap(uint8 lchip, ctc_ipmc_member_info_t* ipmc_member, sys_nh_param_mcast_group_t* nh_mcast_group, uint8  ip_l2mc)
{
    uint8 loop1 = 0;
    uint8 loop2 = 0;
    uint16 lport = 0;
    int32  ret = 0;
    uint8 lchip_temp = 0;

    if (ipmc_member->gchip_id != CTC_LINKAGG_CHIPID)
    {
        SYS_MAP_GCHIP_TO_LCHIP(ipmc_member->gchip_id, lchip_temp);
        SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
    }

    if (CTC_FLAG_ISSET(ipmc_member->flag, CTC_IPMC_MEMBER_FLAG_LEAF_CHECK_EN) || (0 != ipmc_member->logic_dest_port))
    {
        return CTC_E_INVALID_PARAM;
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
            ret = _sys_goldengate_ipmc_update_member(lchip, ipmc_member, nh_mcast_group, ip_l2mc);
            if ((ret < 0) && (nh_mcast_group->opcode == SYS_NH_PARAM_MCAST_ADD_MEMBER))
            {
                return ret;
            }
        }
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_add_member(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint8 member_index = 0;
    int32 ret = CTC_E_NONE;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    sys_nh_param_mcast_group_t nh_mcast_group;
    ctc_ipmc_group_info_t *p_group = NULL;
    uint8  ip_l2mc = 0;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 0));

    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    IPMC_LOCK;
    p_base_group_node = &base_group_node;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));

    sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
    if(NULL == p_base_group_node)
    {
        IPMC_UNLOCK;
        return CTC_E_INVALID_PTR;
    }
    if (CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_DROP))
    {
        IPMC_UNLOCK;
        return CTC_E_INVALID_CONFIG;
    }
    p_group_node->nexthop_id = p_base_group_node->nexthop_id;
    ip_l2mc = p_base_group_node->flag & SYS_IPMC_FLAG_DB_L2MC;

    nh_mcast_group.nhid = p_group_node->nexthop_id;
    nh_mcast_group.dsfwd_valid = 0;
    nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_ADD_MEMBER;

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
            _sys_goldengate_ipmc_update_member_with_bitmap(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc):
            _sys_goldengate_ipmc_update_member(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc);
        if (ret < 0)
        {
            break;
        }

    }

    /*if error, rollback*/
    IPMC_UNLOCK;
    if ((ret < 0) && (ret != CTC_E_EXIST))
    {
        p_group->member_number = member_index + 1;
        sys_goldengate_ipmc_remove_member(lchip, p_group);
        return ret;
    }

    return ret;
}

int32
sys_goldengate_ipmc_remove_member(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint8 member_index = 0;
    int32 ret = 0;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    sys_nh_param_mcast_group_t nh_mcast_group;
    ctc_ipmc_group_info_t *p_group = NULL;
    uint8 ip_l2mc = 0;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 0));

    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_asic_lookup(lchip, p_group_node));
    p_base_group_node = &base_group_node;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));

    sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
    if(NULL == p_base_group_node)
    {
        IPMC_UNLOCK;
        return CTC_E_INVALID_PTR;
    }
    if (CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_DROP))
    {
        IPMC_UNLOCK;
        return CTC_E_INVALID_CONFIG;
    }
    p_group_node->nexthop_id = p_base_group_node->nexthop_id;
    ip_l2mc = p_base_group_node->flag & SYS_IPMC_FLAG_DB_L2MC;

    nh_mcast_group.nhid = p_group_node->nexthop_id;
    nh_mcast_group.dsfwd_valid = 0;
    nh_mcast_group.opcode = SYS_NH_PARAM_MCAST_DEL_MEMBER;

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
            _sys_goldengate_ipmc_update_member_with_bitmap(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc):
            _sys_goldengate_ipmc_update_member(lchip, &(p_group->ipmc_member[member_index]), &nh_mcast_group, ip_l2mc);
    }

    IPMC_UNLOCK;

    return ret;
}

int32
sys_goldengate_ipmc_add_default_entry(uint8 lchip, uint8 ip_version, ctc_ipmc_default_action_t type)
{
    uint32 nh_id = SYS_NH_RESOLVED_NHID_FOR_DROP;
    uint16 i = 0;
    uint16 max_vrfid_num = 0;
    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;
    sys_rpf_intf_t intf;
    sys_ipmc_group_node_t ipmc_default_group;

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&ipmc_default_group, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&intf, 0, sizeof(sys_rpf_intf_t));

    rpf_info.intf = &intf;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_IP_VER_CHECK(ip_version);

    if (CTC_IPMC_DEFAULT_ACTION_DROP != type)
    {
        nh_id = SYS_NH_RESOLVED_NHID_FOR_TOCPU;
    }
    ipmc_default_group.nexthop_id = nh_id;
    if (CTC_IPMC_DEFAULT_ACTION_TO_CPU == type)
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;
        ipmc_default_group.extern_flag |= CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU;
    }
    else if (CTC_IPMC_DEFAULT_ACTION_FALLBACK_BRIDGE == type)
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;
        ipmc_default_group.extern_flag |= CTC_IPMC_FLAG_RPF_CHECK | CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU;
    }
    else
    {
        rpf_info.usage = SYS_RPF_USAGE_TYPE_IPMC;
    }

    CTC_ERROR_RETURN(sys_goldengate_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    ipmc_default_group.flag = (ip_version == CTC_IP_VER_6) ? (ipmc_default_group.flag | SYS_IPMC_FLAG_VERSION) : ipmc_default_group.flag;
    ipmc_default_group.ad_index = (ip_version == CTC_IP_VER_6) ? p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index : p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index;
    max_vrfid_num = (ip_version == CTC_IP_VER_6) ? sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_6) : sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    max_vrfid_num = (max_vrfid_num > 0) ? max_vrfid_num : 1;

    for (i=0; i<max_vrfid_num; i++)
    {
        /* write ipda entry */
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_write_ipda(lchip, &ipmc_default_group, &rpf_rslt));
        ipmc_default_group.ad_index ++;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_set_rp_intf(uint8 lchip, uint8 action, ctc_ipmc_rp_t * p_rp)
{
    uint8 i, j = 0;
    uint8 offset = 0;

    uint32 bidipim_group_index = 0;
    uint32 cmd = 0;
    DsBidiPimGroup_m ds_bidipim_group;
    uint32 bidipim_group[2] = {0};
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rp);
    SYS_IPMC_BIDIPIM_CHECK(p_rp->rp_id);
    CTC_MAX_VALUE_CHECK(p_rp->rp_intf_count, SYS_GG_IPMC_RP_INTF);
    sal_memset(&ds_bidipim_group, 0, sizeof(ds_bidipim_group));

    for (i = 0; i < p_rp->rp_intf_count; i++)
    {
        CTC_MIN_VALUE_CHECK(p_rp->rp_intf[i], 1);
        switch (p_gg_ipmc_master[lchip]->bidipim_mode)
        {
        case SYS_IPMC_BIDIPIM_MODE0:
            CTC_MAX_VALUE_CHECK(p_rp->rp_intf[i], 1023);
            bidipim_group_index = ((p_rp->rp_intf[i] >> 2) & 0xFF);
            offset = ((p_rp->rp_intf[i] & 0x3) << 4) | (p_rp->rp_id & 0xF);
            break;

        case SYS_IPMC_BIDIPIM_MODE1:
            CTC_MAX_VALUE_CHECK(p_rp->rp_intf[i], 511);
            bidipim_group_index = (p_rp->rp_intf[i] >> 1) & 0xFF;
            offset = ((p_rp->rp_intf[i] & 0x1) << 5) | (p_rp->rp_id & 0x1F);
            break;

        case SYS_IPMC_BIDIPIM_MODE2:
            CTC_MAX_VALUE_CHECK(p_rp->rp_intf[i], 255);
            bidipim_group_index = p_rp->rp_intf[i] & 0xFF;
            offset = p_rp->rp_id & 0x3F;
            break;

        case SYS_IPMC_BIDIPIM_MODE3:
            CTC_MAX_VALUE_CHECK(p_rp->rp_intf[i], 127);
            bidipim_group_index = ((p_rp->rp_intf[i] << 1) & 0xFF) | ((p_rp->rp_id & 0x40) >> 6);
            offset = p_rp->rp_id & 0x3F;
            break;

        default:
            return CTC_E_INVALID_PARAM;
            break;
        }

        cmd = DRV_IOR(DsBidiPimGroup_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, bidipim_group_index, cmd, &ds_bidipim_group));
        sal_memcpy(&bidipim_group, &ds_bidipim_group, sizeof(bidipim_group));
        j = (offset >= 32) ? 1 : 0;
        offset = (offset >= 32) ? (offset - 32) : offset;
        bidipim_group[j] = (action == TRUE) ? (bidipim_group[j] | (1 << offset)) : (bidipim_group[j] & (~(uint32)(1 << offset)));
        SetDsBidiPimGroup(A, bidiPimBlock_f, &ds_bidipim_group, bidipim_group);
        cmd = DRV_IOW(DsBidiPimGroup_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, bidipim_group_index, cmd, &ds_bidipim_group));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_add_rp_intf(uint8 lchip, ctc_ipmc_rp_t * p_rp)
{
    return _sys_goldengate_ipmc_set_rp_intf(lchip, TRUE, p_rp);
}


int32
sys_goldengate_ipmc_remove_rp_intf(uint8 lchip, ctc_ipmc_rp_t * p_rp)
{
    return _sys_goldengate_ipmc_set_rp_intf(lchip, FALSE, p_rp);
}

int32
sys_goldengate_ipmc_update_rpf(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint8 i = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 rpf_check_port = 0;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_rpf_info_t sys_ipmc_rpf_info;
    sys_rpf_info_t rpf_info;
    sys_rpf_rslt_t rpf_rslt;
    ctc_ipmc_group_info_t *p_group = NULL;
    sys_ipmc_group_db_node_t* p_group_db_node;
    sys_ipmc_group_db_node_t group_db_node;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&sys_ipmc_rpf_info, 0, sizeof(sys_ipmc_rpf_info_t));
    sal_memset(&group_db_node, 0, sizeof(sys_ipmc_group_db_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 0));
    if (CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        return CTC_E_IPMC_RPF_CHECK_DISABLE;
    }

    cmd = DRV_IOR(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rpf_check_port));

    if (!rpf_check_port)
    {
        for (i = 0; i < SYS_GG_MAX_IPMC_RPF_IF; i++)
        {
            /* check if interfaceId overflow max value */
            if (p_group->rpf_intf_valid[i] && p_group->rpf_intf[i] > SYS_GG_MAX_CTC_L3IF_ID)
            {
                return CTC_E_L3IF_INVALID_IF_ID;
            }
        }
    }

    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_asic_lookup(lchip, p_group_node));
    p_group_db_node = &group_db_node;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_group_db_node));
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_ipmc_db_lookup(lchip, &p_group_db_node));

    if (!(CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&   \
          !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC)))
    {
        IPMC_UNLOCK;
        return CTC_E_IPMC_RPF_CHECK_DISABLE;
    }

    cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckMode_f);
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &value));
    p_group_node->sa_index = 0;
    if (value == 0)
    {
        cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfIfId_f);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &value));
        p_group_node->sa_index = value;
        value = 0;
        CTC_SET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
    }

    /* write group info to Asic hardware table */
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf, p_group->rpf_intf, sizeof(sys_ipmc_rpf_info.rpf_intf));
    sal_memcpy(sys_ipmc_rpf_info.rpf_intf_valid, p_group->rpf_intf_valid, sizeof(sys_ipmc_rpf_info.rpf_intf_valid));
    if (rpf_check_port)
    {
        for (i=0; i<SYS_GG_MAX_IPMC_RPF_IF; i++)
        {
            if (p_group->rpf_intf_valid[i])
            {
                sys_ipmc_rpf_info.rpf_intf[i] = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_group->rpf_intf[i]);
            }
        }
    }

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    rpf_info.force_profile = FALSE;
    rpf_info.usage = SYS_RPF_USAGE_TYPE_IPMC;
    rpf_info.profile_index = p_group_node->sa_index;
    rpf_info.intf = (sys_rpf_intf_t*)&sys_ipmc_rpf_info;

    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ad_index is %d\n", p_group_node->ad_index);

    if (SYS_RPF_CHK_MODE_PROFILE == rpf_rslt.mode)
    {
         /*CTC_SET_FLAG(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK);*/
        /* set ipda rpf_check_mode = 0, rpf use profile*/
        CTC_SET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
        CTC_SET_FLAG(p_group_db_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE);
        p_group_node->sa_index = rpf_rslt.id;
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Alloc rpf profile index: %d\n", p_group_node->sa_index);

        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_update_ipda(lchip, p_group_node, SYS_IPMC_IPDA_RPF_CHECK, &rpf_rslt));
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_write_ipsa(lchip, p_group_node, &sys_ipmc_rpf_info));
    }
    else
    {
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
        {
            /* free old rpf profile */
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(sys_goldengate_rpf_remove_profile(lchip, p_group_node->sa_index));
            CTC_UNSET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
            p_group_node->sa_index = IPMC_INVALID_SA_INDEX;
        }

        CTC_UNSET_FLAG(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE);
        CTC_UNSET_FLAG(p_group_db_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE);

        /* set ipda rpf_if_id */
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_update_ipda(lchip, p_group_node, SYS_IPMC_IPDA_RPF_CHECK, &rpf_rslt));
    }
    p_group_db_node->rpf_index = p_group_node->sa_index;
    IPMC_UNLOCK;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_get_ipda_info(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 cmd = 0;
    uint32 value = 0;
    DsIpDa_m ds_da;

    cmd = DRV_IOR(DsIpDa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &ds_da));
    GetDsIpDa(A, rpfCheckEn_f, &ds_da, &value);
    p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_CHECK) : p_group_node->extern_flag;
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
    {
        GetDsIpDa(A, rpfCheckMode_f, &ds_da, &value);
        p_group_node->flag = (value == 0) ? (p_group_node->flag | SYS_IPMC_FLAG_RPF_PROFILE) : p_group_node->flag;
        GetDsIpDa(A, rpfIfId_f, &ds_da, &value);
        p_group_node->sa_index = value;
    }
    GetDsIpDa(A, bidiPimGroupValid_f, &ds_da, &value);
    p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_BIDPIM_CHECK) : p_group_node->extern_flag;
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_BIDPIM_CHECK))
    {
        GetDsIpDa(A, rpfIfId_f, &ds_da, &value);
        p_group_node->rp_id = value;
    }
    GetDsIpDa(A, ttlCheckEn_f, &ds_da, &value);
    p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_TTL_CHECK) : p_group_node->extern_flag;
    GetDsIpDa(A, ipmcStatsEn_f, &ds_da, &value);
    p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_STATS) : p_group_node->extern_flag;
    GetDsIpDa(A, selfAddress_f, &ds_da, &value);
    p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_PTP_ENTRY) : p_group_node->extern_flag;
    GetDsIpDa(A, mcastRpfFailCpuEn_f, &ds_da, &value);
    if (value == TRUE)
    {
        p_group_node->extern_flag = (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_BIDPIM_CHECK)) ? \
                                    (p_group_node->extern_flag | CTC_IPMC_FLAG_BIDIPIM_FAIL_BRIDGE_AND_TOCPU) \
                                    : (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU);
    }
    GetDsIpDa(A, nextHopPtrValid_f, &ds_da, &value);
    p_group_node->extern_flag = (value == FALSE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_REDIRECT_TOCPU) : p_group_node->extern_flag;
    if (value == TRUE)
    {
        GetDsIpDa(A, ipDaExceptionEn_f, &ds_da, &value);
        p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_COPY_TOCPU) : p_group_node->extern_flag;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_get_group_info(uint8 lchip, ctc_ipmc_group_info_t* p_group_info)
{
    uint32 cmd;
    uint32 value;
    DsRpf_m ds_rpf;
    DsIpDa_m ds_da;
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    ctc_ipmc_group_info_t *p_group = NULL;

    p_group = p_group_info;

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    sal_memset(&ds_rpf, 0, sizeof(ds_rpf));
    sal_memset(&ds_da, 0, sizeof(ds_da));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 0));

    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    IPMC_LOCK;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_asic_lookup(lchip, p_group_node));
    p_base_group_node = &base_group_node;
    CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));

    sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
    if(NULL == p_base_group_node)
    {
        IPMC_UNLOCK;
        return CTC_E_INVALID_PTR;
    }

    p_group_node->nexthop_id = p_base_group_node->nexthop_id;
    p_group_node->group_id = p_base_group_node->group_id;
    p_group_node->extern_flag = p_base_group_node->extern_flag;
    p_group_node->sa_index = p_base_group_node->rpf_index;
    p_group_node->flag = CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE)? (p_group_node->flag | SYS_IPMC_FLAG_RPF_PROFILE) : p_group_node->flag;
    if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
         /*CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(_sys_goldengate_ipmc_get_ipda_info(lchip, p_group_node));*/
    }

    p_group->flag = p_group_node->extern_flag;
    p_group->group_id = p_group_node->group_id;
    p_group->rp_id = p_group_node->rp_id;

    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE) \
        && CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        sal_memset(&ds_rpf, 0, sizeof(ds_rpf));
        cmd = DRV_IOR(DsRpf_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->sa_index, cmd, &ds_rpf));
        value = 0;
        GetDsRpf(A, array_0_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[0] = value;
        value = 0;
        GetDsRpf(A, array_1_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[1] = value;
        value = 0;
        GetDsRpf(A, array_2_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[2] = value;
        value = 0;
        GetDsRpf(A, array_3_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[3] = value;
        value = 0;
        GetDsRpf(A, array_4_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[4] = value;
        value = 0;
        GetDsRpf(A, array_5_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[5] = value;
        value = 0;
        GetDsRpf(A, array_6_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[6] = value;
        value = 0;
        GetDsRpf(A, array_7_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[7] = value;
        value = 0;
        GetDsRpf(A, array_8_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[8] = value;
        value = 0;
        GetDsRpf(A, array_9_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[9] = value;
        value = 0;
        GetDsRpf(A, array_10_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[10] = value;
        value = 0;
        GetDsRpf(A, array_11_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[11] = value;
        value = 0;
        GetDsRpf(A, array_12_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[12] = value;
        value = 0;
        GetDsRpf(A, array_13_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[13] = value;
        value = 0;
        GetDsRpf(A, array_14_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[14] = value;
        value = 0;
        GetDsRpf(A, array_15_rpfIfId_f, &ds_rpf, &value);
        p_group->rpf_intf[15] = value;
        value = 0;

        GetDsRpf(A, array_0_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[0] = value;
        value = 0;
        GetDsRpf(A, array_1_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[1] = value;
        value = 0;
        GetDsRpf(A, array_2_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[2] = value;
        value = 0;
        GetDsRpf(A, array_3_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[3] = value;
        value = 0;
        GetDsRpf(A, array_4_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[4] = value;
        value = 0;
        GetDsRpf(A, array_5_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[5] = value;
        value = 0;
        GetDsRpf(A, array_6_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[6] = value;
        value = 0;
        GetDsRpf(A, array_7_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[7] = value;
        value = 0;
        GetDsRpf(A, array_8_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[8] = value;
        value = 0;
        GetDsRpf(A, array_9_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[9] = value;
        value = 0;
        GetDsRpf(A, array_10_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[10] = value;
        value = 0;
        GetDsRpf(A, array_11_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[11] = value;
        value = 0;
        GetDsRpf(A, array_12_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[12] = value;
        value = 0;
        GetDsRpf(A, array_13_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[13] = value;
        value = 0;
        GetDsRpf(A, array_14_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[14] = value;
        value = 0;
        GetDsRpf(A, array_15_rpfIfIdValid_f, &ds_rpf, &value);
        p_group->rpf_intf_valid[15] = value;
        value = 0;
    }
    else if(!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE) \
        && CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) \
        && !CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        sal_memset(&ds_da, 0, sizeof(DsIpDa_m));
        cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfIfId_f);
        CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &value));

        p_group->rpf_intf[0] = value;
        p_group->rpf_intf_valid[0] = 1;
    }

    IPMC_UNLOCK;
    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_set_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data)
{

    uint32 cmd;
    uint32 field_val;
    uint32 ipv4_mask = 0;
    ipv6_addr_t ipv6_addr;
    ipv6_addr_t ipv6_mask;
    IpeIpv4McastForceRoute_m ipe_ipv4_mcast_force_route;
    IpeIpv6McastForceRoute_m ipe_ipv6_mcast_force_route;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_IP_VER_CHECK(p_data->ip_version);

    sal_memset(&ipv6_mask, 0, sizeof(ipv6_addr_t));
    sal_memset(&ipe_ipv4_mcast_force_route, 0, sizeof(ipe_ipv4_mcast_force_route));
    sal_memset(&ipe_ipv6_mcast_force_route, 0, sizeof(ipe_ipv6_mcast_force_route));


    if (CTC_IP_VER_4 == p_data->ip_version)
    {
        cmd = DRV_IOR(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv4_mcast_force_route));

        if (p_data->ipaddr0_valid)
        {
            if (CTC_IPV4_ADDR_LEN_IN_BIT < p_data->addr0_mask)
            {
                return CTC_E_INVALID_PARAM;
            }
            IPV4_LEN_TO_MASK(ipv4_mask, p_data->addr0_mask);
            SetIpeIpv4McastForceRoute(V, array_0_addrV4Value_f, &ipe_ipv4_mcast_force_route, p_data->ip_addr0.ipv4);
            SetIpeIpv4McastForceRoute(V, array_0_addrV4Mask_f, &ipe_ipv4_mcast_force_route, ipv4_mask);
            SetIpeIpv4McastForceRoute(V, array_0_camV4Valid_f, &ipe_ipv4_mcast_force_route, 1);
        }

        if (p_data->ipaddr1_valid)
        {
            if (CTC_IPV4_ADDR_LEN_IN_BIT < p_data->addr1_mask)
            {
                return CTC_E_INVALID_PARAM;
            }
            IPV4_LEN_TO_MASK(ipv4_mask, p_data->addr1_mask);
            SetIpeIpv4McastForceRoute(V, array_1_addrV4Value_f, &ipe_ipv4_mcast_force_route, p_data->ip_addr1.ipv4);
            SetIpeIpv4McastForceRoute(V, array_1_addrV4Mask_f, &ipe_ipv4_mcast_force_route, ipv4_mask);
            SetIpeIpv4McastForceRoute(V, array_1_camV4Valid_f, &ipe_ipv4_mcast_force_route, 1);

        }

        cmd = DRV_IOW(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv4_mcast_force_route));

        field_val = p_data->force_bridge_en ? 1 : 0;
        cmd = DRV_IOW(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv4McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = p_data->force_ucast_en ? 1 : 0;
        cmd = DRV_IOW(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv4McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        cmd = DRV_IOR(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv6_mcast_force_route));

        if (p_data->ipaddr0_valid)
        {
            ipv6_addr[0] = p_data->ip_addr0.ipv6[3];
            ipv6_addr[1] = p_data->ip_addr0.ipv6[2];
            ipv6_addr[2] = p_data->ip_addr0.ipv6[1];
            ipv6_addr[3] = p_data->ip_addr0.ipv6[0];

            SetIpeIpv6McastForceRoute(A, array_0_addrV6Value_f, &ipe_ipv6_mcast_force_route, ipv6_addr);
            if (CTC_IPV6_ADDR_LEN_IN_BIT < p_data->addr0_mask)
            {
                return CTC_E_INVALID_PARAM;
            }
            IPV6_LEN_TO_MASK(ipv6_mask, p_data->addr0_mask);
            SetIpeIpv6McastForceRoute(A, array_0_addrV6Mask_f, &ipe_ipv6_mcast_force_route, ipv6_mask);
            SetIpeIpv6McastForceRoute(V, array_0_camV6Valid_f, &ipe_ipv6_mcast_force_route, 1);
        }

        sal_memset(&ipv6_mask, 0, sizeof(ipv6_addr_t));
        if (p_data->ipaddr1_valid)
        {
            ipv6_addr[0] = p_data->ip_addr1.ipv6[3];
            ipv6_addr[1] = p_data->ip_addr1.ipv6[2];
            ipv6_addr[2] = p_data->ip_addr1.ipv6[1];
            ipv6_addr[3] = p_data->ip_addr1.ipv6[0];
            SetIpeIpv6McastForceRoute(A, array_1_addrV6Value_f, &ipe_ipv6_mcast_force_route, ipv6_addr);
            if (CTC_IPV6_ADDR_LEN_IN_BIT < p_data->addr1_mask)
            {
                return CTC_E_INVALID_PARAM;
            }
            IPV6_LEN_TO_MASK(ipv6_mask, p_data->addr1_mask);
            SetIpeIpv6McastForceRoute(A, array_1_addrV6Mask_f, &ipe_ipv6_mcast_force_route, ipv6_mask);
            SetIpeIpv6McastForceRoute(V, array_1_camV6Valid_f, &ipe_ipv6_mcast_force_route, 1);
        }

        cmd = DRV_IOW(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv6_mcast_force_route));

        field_val = p_data->force_bridge_en ? 1 : 0;
        cmd = DRV_IOW(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv6McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = p_data->force_ucast_en ? 1 : 0;
        cmd = DRV_IOW(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv6McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_get_mcast_force_route(uint8 lchip, ctc_ipmc_force_route_t* p_data)
{
    uint32 cmd;
    uint32 field_val;
    uint32 ipv4_mask = 0;
    ipv6_addr_t ipv6_addr;
    ipv6_addr_t ipv6_mask;
    IpeIpv4McastForceRoute_m ipe_ipv4_mcast_force_route;
    IpeIpv6McastForceRoute_m ipe_ipv6_mcast_force_route;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_data);
    CTC_IP_VER_CHECK(p_data->ip_version);

    sal_memset(&ipv6_mask, 0, sizeof(ipv6_addr_t));
    sal_memset(&ipe_ipv4_mcast_force_route, 0, sizeof(ipe_ipv4_mcast_force_route));
    sal_memset(&ipe_ipv6_mcast_force_route, 0, sizeof(ipe_ipv6_mcast_force_route));

    if (CTC_IP_VER_4 == p_data->ip_version)
    {
        cmd = DRV_IOR(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv4_mcast_force_route));
        p_data->ip_addr0.ipv4 = GetIpeIpv4McastForceRoute(V, array_0_addrV4Value_f, &ipe_ipv4_mcast_force_route);
        ipv4_mask             = GetIpeIpv4McastForceRoute(V, array_0_addrV4Mask_f, &ipe_ipv4_mcast_force_route);
        IPV4_MASK_TO_LEN(ipv4_mask, p_data->addr0_mask);
        p_data->ipaddr0_valid = GetIpeIpv4McastForceRoute(V, array_0_camV4Valid_f, &ipe_ipv4_mcast_force_route);

        ipv4_mask = 0;
        p_data->ip_addr1.ipv4 = GetIpeIpv4McastForceRoute(V, array_1_addrV4Value_f, &ipe_ipv4_mcast_force_route);
        ipv4_mask             = GetIpeIpv4McastForceRoute(V, array_1_addrV4Mask_f, &ipe_ipv4_mcast_force_route);
        IPV4_MASK_TO_LEN(ipv4_mask, p_data->addr1_mask);
        p_data->ipaddr1_valid= GetIpeIpv4McastForceRoute(V, array_1_camV4Valid_f, &ipe_ipv4_mcast_force_route);

        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv4McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_data->force_bridge_en = field_val ? TRUE : FALSE;

        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv4McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        p_data->force_ucast_en = field_val ? TRUE : FALSE;

    }
    else
    {
        cmd = DRV_IOR(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv6_mcast_force_route));
        GetIpeIpv6McastForceRoute(A, array_0_addrV6Value_f, &ipe_ipv6_mcast_force_route, ipv6_addr);
        p_data->ip_addr0.ipv6[0] = ipv6_addr[3];
        p_data->ip_addr0.ipv6[1] = ipv6_addr[2];
        p_data->ip_addr0.ipv6[2] = ipv6_addr[1];
        p_data->ip_addr0.ipv6[3] = ipv6_addr[0];

        GetIpeIpv6McastForceRoute(A, array_0_addrV6Mask_f, &ipe_ipv6_mcast_force_route, ipv6_mask);
        IPV6_MASK_TO_LEN(ipv6_mask, p_data->addr0_mask);
        p_data->ipaddr0_valid = GetIpeIpv6McastForceRoute(V, array_0_camV6Valid_f, &ipe_ipv6_mcast_force_route);

        GetIpeIpv6McastForceRoute(A, array_1_addrV6Value_f, &ipe_ipv6_mcast_force_route, ipv6_addr);
        p_data->ip_addr1.ipv6[0] = ipv6_addr[3];
        p_data->ip_addr1.ipv6[1] = ipv6_addr[2];
        p_data->ip_addr1.ipv6[2] = ipv6_addr[1];
        p_data->ip_addr1.ipv6[3] = ipv6_addr[0];
        sal_memset(&ipv6_mask, 0, sizeof(ipv6_addr_t));
        GetIpeIpv6McastForceRoute(A, array_1_addrV6Mask_f, &ipe_ipv6_mcast_force_route, ipv6_mask);
        IPV6_MASK_TO_LEN(ipv6_mask, p_data->addr1_mask);
        p_data->ipaddr1_valid = GetIpeIpv6McastForceRoute(V, array_1_camV6Valid_f, &ipe_ipv6_mcast_force_route);

        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv6McastForceBridgeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_data->force_bridge_en = field_val ? TRUE : FALSE;

        cmd = DRV_IOR(IpeAclLookupCtl_t, IpeAclLookupCtl_ipv6McastForceUnicastEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_data->force_ucast_en = field_val ? 1 : 0;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_traverse_pre(void* entry, void* p_trav)
{
    uint8 addr_len = 0;
    ctc_ipmc_group_info_t* p_ipmc_param = NULL;
    sys_ipmc_group_node_t* p_ipmc_info;
    hash_traversal_fn fn;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(entry);
    CTC_PTR_VALID_CHECK(p_trav);

    p_ipmc_param = (ctc_ipmc_group_info_t*)mem_malloc(MEM_IPMC_MODULE, sizeof(ctc_ipmc_group_info_t));
    if (NULL == p_ipmc_param)
    {
        return CTC_E_NO_MEMORY;
    }

    p_ipmc_info = entry;
    fn = ((sys_ipmc_traverse_t*)p_trav)->fn;
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
    ret = (* fn)(p_ipmc_param, p_trav);

    mem_free(p_ipmc_param);

    return ret;
}

int32
sys_goldengate_ipmc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipmc_traverse_fn fn, void* user_data)
{
    hash_traversal_fn  fun = _sys_goldengate_ipmc_traverse_pre;
    sys_ipmc_traverse_t trav;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(user_data);

    trav.data = user_data;
    trav.fn = fn;
    if (NULL == fn)
    {
        return CTC_E_NONE;
    }

    return _sys_goldengate_ipmc_db_traverse(lchip, ip_ver, fun, &trav);
}

int32
sys_goldengate_ipmc_init(uint8 lchip)
{

    uint32 cmd1, cmd2;
    uint32 tbl_id;
    uint32 value = 1;
    uint32 max_vrfid_num;
    uint8 ip_ver = 0;

    LCHIP_CHECK(lchip);

    if (p_gg_ipmc_master[lchip])
    {
        return CTC_E_NONE;
    }
    p_gg_ipmc_master[lchip] = mem_malloc(MEM_IPMC_MODULE, sizeof(sys_ipmc_master_t));
    if (NULL == p_gg_ipmc_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_ipmc_master[lchip], 0, sizeof(sys_ipmc_master_t));
    sal_mutex_create(&(p_gg_ipmc_master[lchip]->p_ipmc_mutex));

    if (NULL == p_gg_ipmc_master[lchip]->p_ipmc_mutex)
    {
        mem_free(p_gg_ipmc_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    tbl_id = DsIpDa_t;
    /*ipv4 ipmc*/
    max_vrfid_num = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    max_vrfid_num = (max_vrfid_num > 0) ? max_vrfid_num : 1;
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, tbl_id, CTC_INGRESS, max_vrfid_num, &(p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index)));
    cmd1 = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv4McastLookupResultCtl_defaultEntryBase_f);
    cmd2 = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv4McastLookupResultCtl_defaultEntryEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &(p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index)));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd2, &value));

    /*ipv6 ipmc*/
    max_vrfid_num = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_6);
    max_vrfid_num = (max_vrfid_num > 0) ? max_vrfid_num : 1;
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, tbl_id, CTC_INGRESS, max_vrfid_num, &(p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index)));
    cmd1 = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv6McastLookupResultCtl_defaultEntryBase_f);
    cmd2 = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv6McastLookupResultCtl_defaultEntryEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &(p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index)));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd2, &value));


    cmd1 = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gMacDaLookupResultCtl_defaultEntryBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &(p_gg_ipmc_master[lchip]->ipv4_l2mc_default_index)));

    p_gg_ipmc_master[lchip]->ipv6_l2mc_default_index = p_gg_ipmc_master[lchip]->ipv4_l2mc_default_index;


    p_gg_ipmc_master[lchip]->bidipim_mode = SYS_IPMC_BIDIPIM_MODE0;
    value = p_gg_ipmc_master[lchip]->bidipim_mode;
    cmd1 = DRV_IOW(IpeRouteCtl_t, IpeRouteCtl_bidiPimGroupType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &value));

    p_gg_ipmc_master[lchip]->info_size[CTC_IP_VER_4] = sizeof(sys_ipmc_group_node_t) - sizeof(ctc_ipmc_ipv6_addr_t) + sizeof(ctc_ipmc_ipv4_addr_t);
    p_gg_ipmc_master[lchip]->info_size[CTC_IP_VER_6] = sizeof(sys_ipmc_group_node_t);
    p_gg_ipmc_master[lchip]->db_info_size[CTC_IP_VER_4] = sizeof(sys_ipmc_group_db_node_t) - sizeof(ctc_ipmc_ipv6_addr_t) + sizeof(ctc_ipmc_ipv4_addr_t);
    p_gg_ipmc_master[lchip]->db_info_size[CTC_IP_VER_6] = sizeof(sys_ipmc_group_db_node_t);


    /* Get lookup mode from IPE_ROUTE_LOOKUP */

    CTC_ERROR_RETURN(sys_goldengate_ipmc_db_init(lchip));

    for (ip_ver = 0; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        CTC_ERROR_RETURN(sys_goldengate_ipmc_add_default_entry(lchip, ip_ver, CTC_IPMC_DEFAULT_ACTION_DROP));
    }

    CTC_ERROR_RETURN(sys_goldengate_rpf_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_IPMC, sys_goldengate_ipmc_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_ipmc_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_ipmc_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_rpf_deinit(lchip);
    sys_goldengate_ipmc_db_deinit(lchip);

    sal_mutex_destroy(p_gg_ipmc_master[lchip]->p_ipmc_mutex);
    mem_free(p_gg_ipmc_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_wb_mapping_node(uint8 lchip, sys_wb_ipmc_group_node_t *p_wb_ipmc_node, sys_ipmc_group_db_node_t *p_ipmc_node, uint8 sync)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = 0;
    sys_ipmc_group_node_t group_node;

    if (sync)
    {
        sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_db_set_group_node(lchip, p_ipmc_node, &group_node));
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_asic_lookup(lchip, &group_node));

        p_wb_ipmc_node->ad_index = group_node.ad_index;
        p_wb_ipmc_node->sa_index = group_node.sa_index;
        p_wb_ipmc_node->pointer = group_node.pointer;

        p_wb_ipmc_node->ip_ver = (p_ipmc_node->flag & SYS_IPMC_FLAG_DB_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
        p_wb_ipmc_node->group_id = p_ipmc_node->group_id;
        p_wb_ipmc_node->nexthop_id = p_ipmc_node->nexthop_id;
        p_wb_ipmc_node->refer_count= p_ipmc_node->refer_count;
        p_wb_ipmc_node->flag = p_ipmc_node->flag;
        p_wb_ipmc_node->extern_flag = p_ipmc_node->extern_flag;
        if (CTC_FLAG_ISSET(p_ipmc_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE))
        {
            p_wb_ipmc_node->rpf_index = p_ipmc_node->rpf_index;
        }


        if (p_wb_ipmc_node->ip_ver == CTC_IP_VER_4)
        {
            p_wb_ipmc_node->vrfid = p_ipmc_node->address.ipv4.vrfid;
            sal_memset(p_wb_ipmc_node->group_addr, 0, 4 * sizeof(uint32));
            sal_memset(p_wb_ipmc_node->src_addr, 0, 4 * sizeof(uint32));
            sal_memcpy(p_wb_ipmc_node->group_addr, &(p_ipmc_node->address.ipv4.group_addr), sizeof(uint32));
            sal_memcpy(p_wb_ipmc_node->src_addr, &(p_ipmc_node->address.ipv4.src_addr), sizeof(uint32));
        }
        else
        {
            p_wb_ipmc_node->vrfid = p_ipmc_node->address.ipv6.vrfid;
            sal_memcpy(p_wb_ipmc_node->group_addr, &(p_ipmc_node->address.ipv6.group_addr), 4 * sizeof(uint32));
            sal_memcpy(p_wb_ipmc_node->src_addr, &(p_ipmc_node->address.ipv6.src_addr), 4 * sizeof(uint32));
        }
    }
    else
    {
        p_ipmc_node->nexthop_id = p_wb_ipmc_node->nexthop_id;
        p_ipmc_node->group_id = p_wb_ipmc_node->group_id;
        p_ipmc_node->refer_count= p_wb_ipmc_node->refer_count;
        p_ipmc_node->flag = p_wb_ipmc_node->flag;
        p_ipmc_node->extern_flag = p_wb_ipmc_node->extern_flag;
        if (CTC_FLAG_ISSET(p_ipmc_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE))
        {
            p_ipmc_node->rpf_index = p_wb_ipmc_node->rpf_index;
        }

        if (p_wb_ipmc_node->ip_ver == CTC_IP_VER_4)
        {
            p_ipmc_node->address.ipv4.vrfid = p_wb_ipmc_node->vrfid;
            sal_memcpy(&(p_ipmc_node->address.ipv4.group_addr), p_wb_ipmc_node->group_addr, sizeof(uint32));
            sal_memcpy(&(p_ipmc_node->address.ipv4.src_addr), p_wb_ipmc_node->src_addr, sizeof(uint32));
        }
        else
        {
            p_wb_ipmc_node->vrfid = p_ipmc_node->address.ipv6.vrfid;
            sal_memcpy(&(p_ipmc_node->address.ipv6.group_addr), p_wb_ipmc_node->group_addr, 4 * sizeof(uint32));
            sal_memcpy(&(p_ipmc_node->address.ipv6.src_addr), p_wb_ipmc_node->src_addr, 4 * sizeof(uint32));
        }

        if (!p_ipmc_node->group_id && !p_ipmc_node->nexthop_id)
        {
            return CTC_E_NONE;
        }

        if(!CTC_FLAG_ISSET(p_wb_ipmc_node->flag, SYS_IPMC_FLAG_DB_L2MC))
        {
            cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_wb_ipmc_node->ad_index, cmd, &value));
            if (value == TRUE && p_wb_ipmc_node->sa_index > 0)
            {
                CTC_ERROR_RETURN(sys_goldengate_rpf_restore_profile(lchip, p_wb_ipmc_node->sa_index));
            }
        }

        tbl_id = CTC_FLAG_ISSET(p_wb_ipmc_node->flag, SYS_IPMC_FLAG_DB_L2MC) ? DsMac_t : DsIpDa_t;
        CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset_from_position(lchip, tbl_id, CTC_INGRESS, 1, p_wb_ipmc_node->ad_index));

        CTC_ERROR_RETURN(sys_goldengate_ipmc_db_alloc_pointer_from_position(lchip, p_ipmc_node, p_wb_ipmc_node->pointer));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_wb_sync_node_func(sys_ipmc_group_db_node_t *p_ipmc_node, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_ipmc_group_node_t  *p_wb_ipmc_node;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_ipmc_node = (sys_wb_ipmc_group_node_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_ipmc_node, 0, sizeof(sys_wb_ipmc_group_node_t));
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_wb_mapping_node(lchip, p_wb_ipmc_node, p_ipmc_node, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_wb_sync(uint8 lchip)
{
    uint32 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_ipmc_master_t  *p_wb_ipmc_master;

    /*syncup  ipmc_matser*/
    wb_data.buffer = mem_malloc(MEM_IPMC_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_ipmc_master_t, CTC_FEATURE_IPMC, SYS_WB_APPID_IPMC_SUBID_MASTER);

    p_wb_ipmc_master = (sys_wb_ipmc_master_t  *)wb_data.buffer;

    p_wb_ipmc_master->lchip = lchip;
    p_wb_ipmc_master->ipmc_group_cnt = p_gg_ipmc_master[lchip]->ipmc_group_cnt;

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup  ipmc_info*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_ipmc_group_node_t, CTC_FEATURE_IPMC, SYS_WB_APPID_IPMC_SUBID_GROUP_NODE);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    for (loop = CTC_IP_VER_4; loop < MAX_CTC_IP_VER; loop++)
    {
        wb_data.valid_cnt = 0;
        CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[loop], (hash_traversal_fn) _sys_goldengate_ipmc_wb_sync_node_func, (void *)&user_data), ret, done);
        if (wb_data.valid_cnt > 0)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_ipmc_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_ipmc_master_t  *p_wb_ipmc_master;
    sys_ipmc_group_db_node_t *p_ipmc_node;
    sys_wb_ipmc_group_node_t  *p_wb_ipmc_node;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_IPMC_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_ipmc_master_t, CTC_FEATURE_IPMC, SYS_WB_APPID_IPMC_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  ipmc_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query ipmc master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_ipmc_master = (sys_wb_ipmc_master_t *)wb_query.buffer;
    p_gg_ipmc_master[lchip]->ipmc_group_cnt = p_wb_ipmc_master->ipmc_group_cnt;

    /*restore  ipmc_info*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_ipmc_group_node_t, CTC_FEATURE_IPMC, SYS_WB_APPID_IPMC_SUBID_GROUP_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_ipmc_node = (sys_wb_ipmc_group_node_t *)wb_query.buffer + entry_cnt++;

        p_ipmc_node = mem_malloc(MEM_IPMC_MODULE,  p_gg_ipmc_master[lchip]->db_info_size[p_wb_ipmc_node->ip_ver]);
        if (NULL == p_ipmc_node)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_ipmc_node, 0, p_gg_ipmc_master[lchip]->info_size[p_wb_ipmc_node->ip_ver]);

        ret = _sys_goldengate_ipmc_wb_mapping_node(lchip, p_wb_ipmc_node, p_ipmc_node, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_hash_insert(p_gg_ipmc_db_master[lchip]->ipmc_hash[p_wb_ipmc_node->ip_ver], p_ipmc_node);
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


/**
 @file sys_greatbelt_ipuc.c

 @date 2011-11-30

 @version v2.0

 The file contains all ipuc related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/

#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_ipuc.h"
#include "ctc_parser.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_rpf_spool.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_l3_hash.h"
#include "sys_greatbelt_ipuc.h"
#include "sys_greatbelt_ipuc_db.h"
#include "sys_greatbelt_lpm.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_qos_policer.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_register.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

struct sys_ipuc_debug_info_s
{
    uint16 big_tcam_index;
    uint16 hash_high_key_index;
    uint16 hash_mid_key_index;
    uint16 hash_low_key_index;
    uint16 lpm_key_index[LPM_PIPE_LINE_STAGE_NUM];

    sys_lpm_key_type_t  hash_high_key;
    sys_lpm_key_type_t  lpm_key[LPM_PIPE_LINE_STAGE_NUM];

    uint8  high_in_small_tcam;
    uint8  mid_in_small_tcam;
    uint8  low_in_small_tcam;
    uint8  in_big_tcam;
};
typedef struct sys_ipuc_debug_info_s sys_ipuc_debug_info_t;

typedef uint32 (* updateIpDa_fn ) (uint8 lchip, void* data, void* change_nh_param);

extern sys_l3_hash_master_t* p_gb_l3hash_master[];
extern sys_ipuc_db_master_t* p_gb_ipuc_db_master[];

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_ipuc_master_t* p_gb_ipuc_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_IPUC_CREAT_LOCK(lchip)                   \
    do                                            \
    {                                             \
        sal_mutex_create(&p_gb_ipuc_master[lchip]->mutex); \
        if (NULL == p_gb_ipuc_master[lchip]->mutex)  \
        { \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX); \
        } \
    } while (0)

#define SYS_IPUC_LOCK(lchip) \
    sal_mutex_lock(p_gb_ipuc_master[lchip]->mutex)

#define SYS_IPUC_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_ipuc_master[lchip]->mutex)

#define CTC_ERROR_RETURN_IPUC_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_ipuc_master[lchip]->mutex); \
            return (rv); \
        } \
    } while (0)

#define CTC_RETURN_IPUC_UNLOCK(op) \
    do \
    { \
        sal_mutex_unlock(p_gb_ipuc_master[lchip]->mutex); \
        return (op); \
    } while (0)

#define SYS_IP_CHECK_VERSION_ENABLE(ver)                        \
    {                                                               \
        if ((!p_gb_ipuc_master[lchip]) || !p_gb_ipuc_master[lchip]->version_en[ver])     \
        {                                                           \
            return CTC_E_IPUC_VERSION_DISABLE;                      \
        }                                                           \
    }

#define SYS_IP_VRFID_CHECK(val)                                     \
    {                                                                   \
        if ((val)->vrf_id >= p_gb_ipuc_master[lchip]->max_vrfid[(val)->ip_ver])    \
        {                                                               \
            return CTC_E_IPUC_INVALID_VRF;                                 \
        }                                                               \
    }

#define SYS_IP_TUNNEL_KEY_CHECK(val)                                            \
    {                                                                           \
        if ((CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY)\
            ||CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_CHECKSUM)\
            ||CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_SEQ_NUM))      \
            && \
            (((val)->payload_type != CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)       \
            ||(((val)->gre_key == 0)&&CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY)))){                                     \
            return CTC_E_IPUC_TUNNEL_PAYLOAD_TYPE_MISMATCH; }                                       \
    }

#define SYS_IP_TUNNEL_AD_CHECK(val)                                                 \
    {                                                                               \
        if (CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD)    \
            && ((val)->nh_id == 0)){                                                \
            return CTC_E_INVALID_PARAM; }                                           \
        if (!CTC_FLAG_ISSET((val)->flag, CTC_IPUC_TUNNEL_FLAG_INNER_VRF_EN)       \
            && (0 != (val)->vrf_id)){                                               \
            return CTC_E_INVALID_PARAM; }                                           \
    }

#define SYS_IP_TUNNEL_ADDRESS_SORT(val)             \
    {                                                   \
        if (CTC_IP_VER_6 == (val)->ip_ver)               \
        {                                               \
            uint32 t;                                   \
            t = val->ip_da.ipv6[0];                        \
            val->ip_da.ipv6[0] = val->ip_da.ipv6[3];          \
            val->ip_da.ipv6[3] = t;                        \
                                                    \
            t = val->ip_da.ipv6[1];                        \
            val->ip_da.ipv6[1] = val->ip_da.ipv6[2];          \
            val->ip_da.ipv6[2] = t;                        \
            if (CTC_FLAG_ISSET(val->flag,                \
                               CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))   \
            {                                           \
                t = val->ip_sa.ipv6[0];                 \
                val->ip_sa.ipv6[0] = val->ip_sa.ipv6[3]; \
                val->ip_sa.ipv6[3] = t;                 \
                                                    \
                t = val->ip_sa.ipv6[1];                 \
                val->ip_sa.ipv6[1] = val->ip_sa.ipv6[2]; \
                val->ip_sa.ipv6[2] = t;                 \
            }                                           \
        }                                               \
    }

#define SYS_IP_TUNNEL_FUNC_DBG_DUMP(val)                                                    \
    {                                                                                           \
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);                         \
        if ((CTC_IP_VER_4 == val->ip_ver))                                                       \
        {                                                                                       \
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,                                             \
                             "route_flag:0x%x  ip_ver:%s  ip:%x  ipsa:%x\n",                                 \
                             val->flag, "IPv4", val->ip_da.ipv4,                                                 \
                             (CTC_FLAG_ISSET(val->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA)) ? 0 :            \
                             val->ip_sa.ipv4);                                                               \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,                                            \
                             "route_flag:0x%x  ip_ver:%s  ip:%x%x%x%x  ipsa:%x%x%x%x\n",                     \
                             val->flag, "IPv6", val->ip_da.ipv6[0], val->ip_da.ipv6[1],                              \
                             val->ip_da.ipv6[2], val->ip_da.ipv6[3],                                                \
                             val->ip_sa.ipv6[0], val->ip_sa.ipv6[1], val->ip_sa.ipv6[2], val->ip_sa.ipv6[3]);   \
        }                                                                                       \
    }

/****************************************************************************
 *
* Function
*
*****************************************************************************/
uint8
SYS_IPUC_OCTO(uint32* ip32, sys_ip_octo_t index)
{
    return ((ip32[index / 4] >> ((index & 3) * 8)) & 0xFF);
}

#define ___________IPUC_INNER_FUNCTION________________________
#define __1_IPDA__
STATIC int32
_sys_greatbelt_ipuc_write_rpf_profile(uint8 lchip, sys_rpf_rslt_t* p_rpf_rslt, sys_rpf_intf_t* p_rpf_intf)
{
    uint32 cmd = 0;
    ds_rpf_t ds_rpf;

    SYS_IPUC_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    sal_memset(&ds_rpf, 0, sizeof(ds_rpf_t));

    /* ipv4 and ipv6 share the same data structure */
    ds_rpf.rpf_if_id0  = p_rpf_intf->rpf_intf[0];
    ds_rpf.rpf_if_id1  = p_rpf_intf->rpf_intf[1];
    ds_rpf.rpf_if_id2  = p_rpf_intf->rpf_intf[2];
    ds_rpf.rpf_if_id3  = p_rpf_intf->rpf_intf[3];

    ds_rpf.rpf_if_id4  = p_rpf_intf->rpf_intf[4];
    ds_rpf.rpf_if_id5  = p_rpf_intf->rpf_intf[5];
    ds_rpf.rpf_if_id6  = p_rpf_intf->rpf_intf[6];
    ds_rpf.rpf_if_id7  = p_rpf_intf->rpf_intf[7];

    ds_rpf.rpf_if_id8  = p_rpf_intf->rpf_intf[8];
    ds_rpf.rpf_if_id9  = p_rpf_intf->rpf_intf[9];
    ds_rpf.rpf_if_id10 = p_rpf_intf->rpf_intf[10];
    ds_rpf.rpf_if_id11 = p_rpf_intf->rpf_intf[11];

    ds_rpf.rpf_if_id12 = p_rpf_intf->rpf_intf[12];
    ds_rpf.rpf_if_id13 = p_rpf_intf->rpf_intf[13];
    ds_rpf.rpf_if_id14 = p_rpf_intf->rpf_intf[14];
    ds_rpf.rpf_if_id15 = p_rpf_intf->rpf_intf[15];

    ds_rpf.rpf_if_id_valid0  = p_rpf_intf->rpf_intf_valid[0];
    ds_rpf.rpf_if_id_valid1  = p_rpf_intf->rpf_intf_valid[1];
    ds_rpf.rpf_if_id_valid2  = p_rpf_intf->rpf_intf_valid[2];
    ds_rpf.rpf_if_id_valid3  = p_rpf_intf->rpf_intf_valid[3];

    ds_rpf.rpf_if_id_valid4  = p_rpf_intf->rpf_intf_valid[4];
    ds_rpf.rpf_if_id_valid5  = p_rpf_intf->rpf_intf_valid[5];
    ds_rpf.rpf_if_id_valid6  = p_rpf_intf->rpf_intf_valid[6];
    ds_rpf.rpf_if_id_valid7  = p_rpf_intf->rpf_intf_valid[7];

    ds_rpf.rpf_if_id_valid8  = p_rpf_intf->rpf_intf_valid[8];
    ds_rpf.rpf_if_id_valid9  = p_rpf_intf->rpf_intf_valid[9];
    ds_rpf.rpf_if_id_valid10 = p_rpf_intf->rpf_intf_valid[10];
    ds_rpf.rpf_if_id_valid11 = p_rpf_intf->rpf_intf_valid[11];

    ds_rpf.rpf_if_id_valid12 = p_rpf_intf->rpf_intf_valid[12];
    ds_rpf.rpf_if_id_valid13 = p_rpf_intf->rpf_intf_valid[13];
    ds_rpf.rpf_if_id_valid14 = p_rpf_intf->rpf_intf_valid[14];
    ds_rpf.rpf_if_id_valid15 = p_rpf_intf->rpf_intf_valid[15];

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s %d ds_rpf index 0x%x.\n", __FUNCTION__, __LINE__, p_rpf_rslt->id);


    cmd = DRV_IOW(DsRpf_t, DRV_ENTRY_FLAG);


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_rpf_rslt->id, cmd, &ds_rpf));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_add_rpf(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile, sys_rpf_rslt_t* p_rpf_rslt)
{
    sys_nh_info_dsnh_t nh_info;
    sys_rpf_intf_t rpf_intf;
    sys_rpf_info_t rpf_info;
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_rslt = NULL;

    uint32 idx = 0;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_rpf_profile);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&rpf_intf, 0, sizeof(sys_rpf_intf_t));
    rpf_info.intf = &rpf_intf;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_rpf_profile->key.nh_id, &nh_info));

    if (nh_info.ecmp_valid)
    {
        if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_RPF_CHECK))
        {
            for (idx = 0; idx < nh_info.ecmp_cnt; idx++)
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_l3ifid(lchip, nh_info.nh_array[idx], &rpf_intf.rpf_intf[idx]));
                rpf_intf.rpf_intf_valid[idx] = 1;
            }

            rpf_info.force_profile = FALSE;
            rpf_info.usage = SYS_RPF_USAGE_TYPE_IPUC;
            rpf_info.profile_index = SYS_RPF_INVALID_PROFILE_ID;

            CTC_ERROR_RETURN(sys_greatbelt_rpf_add_profile(lchip, &rpf_info, p_rpf_rslt));

            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: rpf mode = %d rpf id = %d\n", p_rpf_rslt->mode, p_rpf_rslt->id);

            if ((SYS_RPF_CHK_MODE_PROFILE == p_rpf_rslt->mode) && (SYS_RPF_INVALID_PROFILE_ID != p_rpf_rslt->id))
            {
                _sys_greatbelt_ipuc_db_add_rpf_profile(lchip, p_rpf_profile, &p_rpf_profile_rslt);

                if (0 == p_rpf_profile_rslt->ad.counter)
                {
                    p_rpf_profile_rslt->ad.idx = p_rpf_rslt->id;
                    CTC_ERROR_RETURN(_sys_greatbelt_ipuc_write_rpf_profile(lchip, p_rpf_rslt, &rpf_intf));
                }
            }
        }
    }
    else
    {
        if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_ICMP_CHECK)
            || CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_RPF_CHECK))
        {
            if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_CONNECT))
            {
                p_rpf_rslt->id = p_rpf_profile->key.l3if;
            }
            else
            {
                CTC_ERROR_RETURN(sys_greatbelt_nh_get_l3ifid(lchip, p_rpf_profile->key.nh_id, &(p_rpf_rslt->id)));
                if ((p_rpf_rslt->id == SYS_L3IF_INVALID_L3IF_ID)
                    && (SYS_NH_TYPE_MPLS != nh_info.nh_entry_type))
                {
                    return CTC_E_NH_INVALID_NH_TYPE;
                }
            }
            p_rpf_rslt->mode = SYS_RPF_CHK_MODE_IFID;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_remove_rpf(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile)
{
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_result = NULL;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_rpf_profile);

    p_rpf_profile_result = _sys_greatbelt_ipuc_db_rpf_profile_lookup(lchip, p_rpf_profile);

    if (NULL != p_rpf_profile_result)
    {
        CTC_ERROR_RETURN(sys_greatbelt_rpf_remove_profile(lchip, p_rpf_profile_result->ad.idx));
        CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_remove_rpf_profile(lchip, p_rpf_profile));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_get_ipda_nexthop(uint8 lchip, sys_nh_info_dsnh_t* p_dsnh_info, ds_ip_da_t* p_dsipda)
{
    uint32 dest_id = p_dsnh_info->dest_id;
    uint16 gport = 0;
    uint32 dest_map = 0;
    uint16 dsnh_offset = 0;

    SYS_IPUC_DBG_FUNC();
    dsnh_offset = p_dsnh_info->dsnh_offset;
    if (CTC_IS_CPU_PORT(p_dsnh_info->gport))
    {
        dest_id = SYS_REASON_ENCAP_DEST_MAP(p_dsnh_info->dest_chipid, SYS_PKT_CPU_QDEST_BY_DMA);
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
    }

    if (p_dsnh_info->drop_pkt)
    {
        /* update DsIpDa to drop */
        p_dsipda->ds_fwd_ptr = 0xFFFF;
        p_dsipda->next_hop_ptr_valid = 0;
    }
    else
    {
        p_dsipda->tunnel_gre_options        = (dest_id) & 0x7;          /* adDestMap[2:0]   */
        p_dsipda->aps_select_valid          = (dest_id >> 3) & 0x1;     /* adDestMap[3]     */
        p_dsipda->payload_offset_type       = (dest_id >> 4) & 0x1;     /* adDestMap[4]     */
        p_dsipda->aps_select_group_id       = (dest_id >> 5) & 0x1;     /* adDestMap[5]     */
        p_dsipda->equal_cost_path_num2_0    = (dest_id >> 6) & 0x7;     /* adDestMap[6:8]   */
        p_dsipda->equal_cost_path_num3_3    = (dest_id >> 9) & 0x1;     /* adDestMap[9]     */
        p_dsipda->isatap_check_en           = (dest_id >> 10) & 0x1;    /* adDestMap[10]    */
        p_dsipda->tunnel_packet_type        = (dest_id >> 11) & 0x7;    /* adDestMap[13:11] */
        p_dsipda->color                     = (dest_id >> 14) & 0x3;    /* adDestMap[15:14] */
        p_dsipda->priority                  = (p_dsnh_info->dest_chipid) & 0x1F;     /* adDestMap[21:16] */

        p_dsipda->tunnel_payload_offset     |= (p_dsnh_info->nexthop_ext) << 1;  /* nextHopExt */
        p_dsipda->next_hop_ptr_valid        = 1;

        p_dsipda->ds_fwd_ptr = dsnh_offset;

        if(sys_greatbelt_get_cut_through_en(lchip))
        {
            /*aps destid not consider, not support multicast, linkagg*/
            if(p_dsnh_info->is_mcast)
            {
                p_dsipda->ad_speed0_0   = 1;
                p_dsipda->pt_enable     = 1;
            }
            else if(!p_dsnh_info->aps_en && (CTC_LINKAGG_CHIPID!= p_dsnh_info->dest_chipid))
            {
                dest_map = SYS_NH_ENCODE_DESTMAP(0, p_dsnh_info->dest_chipid, p_dsnh_info->dest_id);
                gport = SYS_GREATBELT_DESTMAP_TO_GPORT(dest_map);
                p_dsipda->ad_speed0_0   = CTC_IS_BIT_SET(sys_greatbelt_get_cut_through_speed(lchip, gport), 0);
                p_dsipda->pt_enable     = CTC_IS_BIT_SET(sys_greatbelt_get_cut_through_speed(lchip, gport), 1);
            }
            else
            {
                p_dsipda->ad_speed0_0   = 0;
                p_dsipda->pt_enable     = 0;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_update_ipda(uint8 lchip, void* data, void* change_nh_param)
{
    sys_ipuc_info_t* p_ipuc_info = data;
    sys_nh_info_dsnh_t* p_dsnh_info = change_nh_param;
    ds_ip_da_t   dsipda;
    uint32 cmdr;
    uint32 cmdw;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&dsipda, 0, sizeof(ds_ipv4_ucast_da_tcam_t));

    if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        cmdr = DRV_IOR(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);
        cmdw = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);
    }
    else
    {
        cmdr = DRV_IOR(p_gb_ipuc_master[lchip]->lpm_da_table, DRV_ENTRY_FLAG);
        cmdw = DRV_IOW(p_gb_ipuc_master[lchip]->lpm_da_table, DRV_ENTRY_FLAG);
    }

    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmdr, &dsipda);

    _sys_greatbelt_ipuc_get_ipda_nexthop(lchip, p_dsnh_info, &dsipda);


    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmdw, &dsipda);


    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC update_da: p_ipuc_info->ad_offset:0x%x   drop\n",
                     p_ipuc_info->ad_offset);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_bind_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info,uint32 stats_id,bool is_update)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_update_dsnh_param_t update_dsnh;
    bool is_host_route = FALSE;
    int32 ret = 0;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));

    if((p_ipuc_info->masklen == p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)])
            && CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
            && (!nh_info.dsfwd_valid))
    {
        is_host_route = TRUE;
    }

    if ((CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_STATS_EN) && is_update)
        || (nh_info.stats_en))
    {
        /* host route, write dsnh to ipda */
        update_dsnh.isAddDsFwd = TRUE;
        update_dsnh.isAddDsL2Edit = FALSE;
        update_dsnh.data = p_ipuc_info;
        update_dsnh.stats_id = stats_id;
        update_dsnh.stats_en = (stats_id?TRUE:FALSE);
        update_dsnh.updateAd = NULL;
        update_dsnh.nh_entry_type = nh_info.nh_entry_type;

        CTC_ERROR_RETURN(sys_greatbelt_nh_update_nhInfo(lchip, p_ipuc_info->nh_id, &update_dsnh));
        CTC_UNSET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    }
    else if (is_host_route && (nh_info.merge_dsfwd != 1)  && (nh_info.merge_dsfwd != 2))
    {
        /* host route, write dsnh to ipda */
        update_dsnh.isAddDsFwd = FALSE;
        update_dsnh.isAddDsL2Edit = FALSE;
        update_dsnh.data = p_ipuc_info;
        update_dsnh.updateAd = (updateIpDa_fn)_sys_greatbelt_ipuc_update_ipda;

        ret = sys_greatbelt_nh_update_nhInfo(lchip, p_ipuc_info->nh_id, &update_dsnh);
        if(CTC_E_NH_HR_NH_IN_USE != ret)
        {
             CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH);
             CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY);
        }
    }

    if (is_host_route && (nh_info.merge_dsfwd == 1))
    {
        CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    }

    if(!p_gb_ipuc_master[lchip]->must_use_dsfwd
        && (nh_info.merge_dsfwd == 1)
        && (!nh_info.dsfwd_valid)
        && !CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_STATS_EN))
    {
        CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_unbind_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_update_dsnh_param_t update_dsnh;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));

    if (0 == sys_greatbelt_nh_get_nh_valid(lchip, p_ipuc_info->nh_id))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));

    if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH) || p_ipuc_info->binding_nh)
    {
        /* host route , write dsnh to ipda */
        sys_nh_update_dsnh_param_t update_dsnh;

        update_dsnh.isAddDsFwd = FALSE;
        update_dsnh.data = NULL;
        update_dsnh.updateAd = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_nh_update_nhInfo(lchip, p_ipuc_info->nh_id, &update_dsnh));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_build_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt, bool do_lpm, uint32 stats_id )
{
    sys_ipuc_db_rpf_profile_t rpf_profile;
    uint32 fwd_offset;
    ctc_l3if_vrf_stats_t p_vrf_stats;
    uint8 vrf_stats_en = 0;
    int32 ret = CTC_E_NONE;
    int32 ad_spool_ref_cnt = 0;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));
    sal_memset(&p_vrf_stats, 0, sizeof(ctc_l3if_vrf_stats_t));

    /* add vrf stats */
    p_vrf_stats.vrf_id = p_ipuc_info->vrf_id;
    sys_greatbelt_l3if_get_vrf_stats_en(lchip, &p_vrf_stats);
    vrf_stats_en = p_vrf_stats.enable;

    if (vrf_stats_en)
    {
        /* call this func to alloc dsfwd */
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_ipuc_info->nh_id, &fwd_offset));

        CTC_ERROR_RETURN(sys_greatbelt_nh_add_stats_action(lchip, p_ipuc_info->nh_id, p_ipuc_info->vrf_id));
    }

    /* update nexthop info */
    CTC_ERROR_RETURN(_sys_greatbelt_ipuc_bind_nexthop(lchip, p_ipuc_info,stats_id,TRUE));

    if(p_ipuc_info->masklen == p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)] &&
        CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_IPV6))
    {
    /* Ipv6 128 bit route should set SYS_IPUC_FLAG_HIGH_PRIORITY to alloc different dsipda*/
        CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_HIGH_PRIORITY);
    }

    if (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY))
    {
        /* call this func to alloc dsfwd */
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_ipuc_info->nh_id, &fwd_offset));
    }

    /* add rpf */
    rpf_profile.key.nh_id = p_ipuc_info->nh_id;
    rpf_profile.key.route_flag = p_ipuc_info->route_flag;
    rpf_profile.key.l3if = p_ipuc_info->l3if;
    ret = _sys_greatbelt_ipuc_add_rpf(lchip, &rpf_profile, p_rpf_rslt);
    if (ret)
    {
        _sys_greatbelt_ipuc_unbind_nexthop(lchip, p_ipuc_info);
        return ret;
    }

    /* add hash key ad profile */
    if (do_lpm)
    {
        ret = _sys_greatbelt_ipuc_lpm_ad_profile_add(lchip, p_ipuc_info, &ad_spool_ref_cnt);
        if (ret)
        {
            _sys_greatbelt_ipuc_unbind_nexthop(lchip, p_ipuc_info);
            _sys_greatbelt_ipuc_remove_rpf(lchip, &rpf_profile);
            return ret;
        }

        if (ad_spool_ref_cnt > 1)
        {
            _sys_greatbelt_ipuc_unbind_nexthop(lchip, p_ipuc_info);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_unbuild_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, bool do_lpm)
{
    sys_rpf_rslt_t rpf_rslt;
    sys_ipuc_db_rpf_profile_t rpf_profile;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);

    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));

    /* update nexthop info */
    _sys_greatbelt_ipuc_unbind_nexthop(lchip, p_ipuc_info);

    /* remove rpf */
    rpf_profile.key.nh_id = p_ipuc_info->nh_id;
    rpf_profile.key.route_flag = p_ipuc_info->route_flag;
    _sys_greatbelt_ipuc_remove_rpf(lchip, &rpf_profile);

    /* free lpm ad profile */
    if (do_lpm)
    {
        _sys_greatbelt_ipuc_lpm_ad_profile_remove(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_write_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt)
{
    /* ds_ip_da_t and ds_ipv4_ucast_da_tcam_t are the same*/
    uint8 equal_cost_path_num = 0;
    uint32 route_flag = 0;
    uint32 cmd = 0;
    ds_ip_da_t dsipda;
    sys_nh_info_dsnh_t nh_info;
    uint32 fwd_offset;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);

    route_flag = p_ipuc_info->route_flag;

    sal_memset(&dsipda, 0, sizeof(ds_ip_da_t));
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));


    dsipda.ttl_check_en = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_TTL_CHECK);
    dsipda.ip_da_exception_en = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_CPU);
    dsipda.exception3_ctl_en = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_PROTOCOL_ENTRY);
    dsipda.self_address = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_SELF_ADDRESS);
    dsipda.icmp_check_en = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_CHECK);
    dsipda.icmp_err_msg_check_en = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_ERR_MSG_CHECK);
    dsipda.is_default_route = CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_DEFAULT);
    dsipda.l3_if_type = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_CANCEL_NAT); /* 0- external,1- internal */
    dsipda.rpf_check_mode = p_rpf_rslt->mode;
    dsipda.rpf_if_id = p_rpf_rslt->id;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));

    if (nh_info.ecmp_valid)
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ecmp number is %d\r\n", nh_info.ecmp_num);
        equal_cost_path_num = nh_info.ecmp_num - 1;

        dsipda.equal_cost_path_num2_0 = equal_cost_path_num & 0x7;
        dsipda.equal_cost_path_num3_3 = (equal_cost_path_num >> 3) & 0x1;
    }

    if (nh_info.is_ivi)
    {
        dsipda.deny_pbr = 1;    /* ivi enable */
        dsipda.pt_enable = 1;   /* need do ipv4-ipv6 address translate */
    }

    /*to CPU must be equal to 63(get the low 5bits is 31), refer to sys_greatbelt_pdu.h:SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU*/
    dsipda.exception_sub_index = 63 & 0x1F;
    if (!(CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL)))
    {
        cmd = DRV_IOW(p_gb_ipuc_master[lchip]->lpm_da_table, DRV_ENTRY_FLAG);
    }
    else
    {
        cmd = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM  ");
    }

    if (((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen > 64))
        || ((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen > 16)
            && (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))))
    {
        dsipda.result_priority = 1;
    }

    if (CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_MERGE_KEY))
    {
        _sys_greatbelt_ipuc_get_ipda_nexthop(lchip, &nh_info, &dsipda);
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_ipuc_info->nh_id, &fwd_offset));
    }

    if (!CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_MERGE_KEY))
    {
        dsipda.ds_fwd_ptr = fwd_offset & 0xfffff;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: p_ipuc_info->ad_offset:%d  dsipda.ds_fwd_ptr:0x%x\n",
                         p_ipuc_info->ad_offset, dsipda.ds_fwd_ptr);
    }

    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmd, &dsipda);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_nat_write_ipsa(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    /* ds_ip_da_t and ds_ipv4_ucast_da_tcam_t are the same*/
    uint32 cmd = 0;
    ds_ip_sa_nat_t dsnatsa;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_nat_info);

    sal_memset(&dsnatsa, 0, sizeof(dsnatsa));

    dsnatsa.replace_ip_sa = 1;
    dsnatsa.ip_sa16_0 = p_nat_info->new_ipv4[0] & 0x1FFFF;
    dsnatsa.ip_sa20_17 = (p_nat_info->new_ipv4[0] >> 17) & 0xF;
    dsnatsa.ip_sa22_21 = (p_nat_info->new_ipv4[0] >> 21) & 0x3;
    dsnatsa.ip_sa30_23 = (p_nat_info->new_ipv4[0] >> 23) & 0xFF;
    dsnatsa.ip_sa31 = (p_nat_info->new_ipv4[0] >> 31) & 0x1;

    if (p_nat_info->l4_src_port)
    {
        dsnatsa.replace_l4_source_port  = 1;
        dsnatsa.l4_source_port = p_nat_info->new_l4_src_port;
    }

    if (!p_nat_info->in_tcam)
    {
        cmd = DRV_IOW(DsIpSaNat_t, DRV_ENTRY_FLAG);
    }
    else
    {
        cmd = DRV_IOW(DsIpv4SaNatTcam_t, DRV_ENTRY_FLAG);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM  ");
    }


    DRV_IOCTL(lchip, p_nat_info->ad_offset, cmd, &dsnatsa);


    return CTC_E_NONE;
}

#define __2_KEY__
STATIC int32
_sys_greatbelt_ipuc_alloc_nat_tcam_ad_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    sys_greatbelt_opf_t opf;
    uint32 ad_offset = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &ad_offset));
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipv4 nat tcam key&ad index %d\r\n", ad_offset);

    p_nat_info->key_offset = ad_offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_free_nat_tcam_ad_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    sys_greatbelt_opf_t opf;
    uint32 ad_offset = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;

    ad_offset = p_nat_info->key_offset;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, ad_offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_build_tunnel_key(uint8 lchip, sys_scl_entry_t* scl_entry, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param)
{
    if (CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver)
    {
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_USE_FLEX))
        {
            scl_entry->key.type                        = SYS_SCL_KEY_TCAM_TUNNEL_IPV4;
            scl_entry->key.u.tcam_tunnel_ipv4_key.flag       =
                CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY) ?
                SYS_SCL_TUNNEL_GRE_KEY_VALID : 0;
            scl_entry->key.u.tcam_tunnel_ipv4_key.ipv4da     = p_ipuc_tunnel_param->ip_da.ipv4;

            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
            {
                scl_entry->key.u.tcam_tunnel_ipv4_key.ipv4sa     = p_ipuc_tunnel_param->ip_sa.ipv4;
                scl_entry->key.u.tcam_tunnel_ipv4_key.flag |= SYS_SCL_TUNNEL_IPSA_VALID;
            }

            if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_GRE;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_IPINIP;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_V6INIP;
            }

             scl_entry->key.u.tcam_tunnel_ipv4_key.gre_key    =
                CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY) ?
                p_ipuc_tunnel_param->gre_key : 0;
        }
        else if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
        {
            scl_entry->key.type                        = SYS_SCL_KEY_TCAM_TUNNEL_IPV4;
            scl_entry->key.u.tcam_tunnel_ipv4_key.flag       =
                CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY) ?
                SYS_SCL_TUNNEL_GRE_KEY_VALID : 0;
             scl_entry->key.u.tcam_tunnel_ipv4_key.ipv4da     = p_ipuc_tunnel_param->ip_da.ipv4;
             scl_entry->key.u.tcam_tunnel_ipv4_key.ipv4sa     = 0;
            if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_GRE;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_IPINIP;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
            {
                 scl_entry->key.u.tcam_tunnel_ipv4_key.l4type     = CTC_PARSER_L4_TYPE_V6INIP;
            }

             scl_entry->key.u.tcam_tunnel_ipv4_key.gre_key    =
                CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY) ?
                p_ipuc_tunnel_param->gre_key : 0;
        }
        else if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            scl_entry->key.type            = SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.ip_da = p_ipuc_tunnel_param->ip_da.ipv4;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.gre_key = p_ipuc_tunnel_param->gre_key;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.l4_type = CTC_PARSER_L4_TYPE_GRE;
        }
        else
        {
            scl_entry->key.type              = SYS_SCL_KEY_HASH_TUNNEL_IPV4;
            scl_entry->key.u.hash_tunnel_ipv4_key.ip_da = p_ipuc_tunnel_param->ip_da.ipv4;
            scl_entry->key.u.hash_tunnel_ipv4_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;
            if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
            {
                scl_entry->key.u.hash_tunnel_ipv4_key.l4_type     = CTC_PARSER_L4_TYPE_GRE;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
            {
                scl_entry->key.u.hash_tunnel_ipv4_key.l4_type     = CTC_PARSER_L4_TYPE_IPINIP;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
            {
                scl_entry->key.u.hash_tunnel_ipv4_key.l4_type     = CTC_PARSER_L4_TYPE_V6INIP;
            }
        }
    }
    else
    {
        scl_entry->key.type                  = SYS_SCL_KEY_TCAM_TUNNEL_IPV6;

        sal_memcpy(scl_entry->key.u.tcam_tunnel_ipv6_key.ipv6_da, p_ipuc_tunnel_param->ip_da.ipv6,
                   p_gb_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            scl_entry->key.u.tcam_tunnel_ipv6_key.flag     |= SYS_SCL_TUNNEL_GRE_KEY_VALID;
            scl_entry->key.u.tcam_tunnel_ipv6_key.gre_key  = p_ipuc_tunnel_param->gre_key;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
        {
            scl_entry->key.u.tcam_tunnel_ipv6_key.flag     |= SYS_SCL_TUNNEL_IPSA_VALID;
            sal_memcpy(scl_entry->key.u.tcam_tunnel_ipv6_key.ipv6_sa, p_ipuc_tunnel_param->ip_sa.ipv6,
                       p_gb_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
        }

        if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
        {
            scl_entry->key.u.tcam_tunnel_ipv6_key.l4type     = CTC_PARSER_L4_TYPE_GRE;
        }
        else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
        {
            scl_entry->key.u.tcam_tunnel_ipv6_key.l4type     = CTC_PARSER_L4_TYPE_IPINV6;
        }
        else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
        {
            scl_entry->key.u.tcam_tunnel_ipv6_key.l4type     = CTC_PARSER_L4_TYPE_V6INV6;
        }
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_traverse_pre(void* entry, void* p_trav)
{
    ctc_ipuc_param_t ipuc_param;
    ctc_ipuc_param_t* p_ipuc_param = &ipuc_param;
    sys_ipuc_info_t* p_ipuc_info = entry;
    uint8 lchip = 0;
    hash_traversal_fn fn = ((sys_ipuc_traverse_t*)p_trav)->fn;
    void* data = ((sys_ipuc_traverse_t*)p_trav)->data;

    sal_memset(&ipuc_param, 0, sizeof(ipuc_param));

    lchip = ((sys_ipuc_traverse_t*)p_trav)->lchip;

    p_ipuc_param->nh_id = p_ipuc_info->nh_id;
    p_ipuc_param->vrf_id = p_ipuc_info->vrf_id;
    p_ipuc_param->route_flag = (p_ipuc_info->route_flag & 0xFF);
    p_ipuc_param->masklen = p_ipuc_info->masklen;
    p_ipuc_param->ip_ver = CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_IPV6)
        ? CTC_IP_VER_6 : CTC_IP_VER_4;
    sal_memcpy(&(p_ipuc_param->ip), &(p_ipuc_info->ip), p_gb_ipuc_master[lchip]->addr_len[p_ipuc_param->ip_ver]);
    return (* fn)(p_ipuc_param, data);
}

uint32
_sys_greatbelt_ipuc_do_lpm_check(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, bool* do_lpm)
{
    *do_lpm = TRUE;

    if (!CTC_FLAG_ISSET(p_gb_ipuc_master[lchip]->lookup_mode[SYS_IPUC_VER(p_ipuc_info)], SYS_IPUC_HASH_LOOKUP))
    {
        p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
        *do_lpm = FALSE;
        return CTC_E_NONE;
    }


    if (((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && ((p_ipuc_info->masklen < 32)
                                                              || ((p_ipuc_info->masklen > 64) && (p_ipuc_info->masklen < 128))
                                                              || ((128 == p_ipuc_info->masklen) && (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)))))
            || ((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && (((p_ipuc_info->masklen < 16)
                                                                 && (!p_gb_ipuc_master[lchip]->use_hash8)) || ((p_ipuc_info->masklen < 8) && p_gb_ipuc_master[lchip]->use_hash8))))
    {
        p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
        *do_lpm = FALSE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipv4_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, void* p_key)
{
    ds_ipv4_ucast_route_key_t* p_dsipkey;
    ds_ipv4_ucast_route_key_t* p_dsipkeymask;
    tbl_entry_t* p_tbl_ipkey = p_key;

    p_dsipkey = (ds_ipv4_ucast_route_key_t*)p_tbl_ipkey->data_entry;
    p_dsipkeymask = (ds_ipv4_ucast_route_key_t*)p_tbl_ipkey->mask_entry;

    sal_memset(p_dsipkey, 0, sizeof(ds_ipv4_ucast_route_key_t));
    sal_memset(p_dsipkeymask, 0, sizeof(ds_ipv4_ucast_route_key_t));
    p_dsipkey->vrf_id9_0 = p_ipuc_info->vrf_id & 0x3ff;
    p_dsipkey->vrf_id13_10 = p_ipuc_info->vrf_id >> 10;
    p_dsipkey->ip_da = p_ipuc_info->ip.ipv4[0];
    p_dsipkey->table_id0 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_4];
    p_dsipkey->table_id1 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_4];
    p_dsipkey->sub_table_id0 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_4];
    p_dsipkey->sub_table_id1 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_4];

    if (p_ipuc_info->vrf_id)
    {
        p_dsipkey->route_lookup_mode = 1;
        p_dsipkey->ip_sa = p_ipuc_info->vrf_id;
        p_dsipkeymask->ip_sa = 0xffff;
    }

    if (p_ipuc_info->l4_dst_port > 0)
    {
        p_dsipkey->l4_dest_port = p_ipuc_info->l4_dst_port;
        p_dsipkey->is_tcp = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 1 : 0;
        p_dsipkey->is_udp = !p_dsipkey->is_tcp;
    }

    p_dsipkeymask->table_id0 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id1 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->sub_table_id0 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;
    p_dsipkeymask->sub_table_id1 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;
    if (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_DEFAULT))
    {
        p_dsipkeymask->vrf_id9_0 = 0x3ff;
        p_dsipkeymask->vrf_id13_10 = 0xf;
        IPV4_LEN_TO_MASK(p_dsipkeymask->ip_da, p_ipuc_info->masklen);
        if (p_ipuc_info->l4_dst_port > 0)
        {
            p_dsipkeymask->l4_dest_port = 0xFFFF;
            p_dsipkeymask->is_tcp = 0x1;
            p_dsipkeymask->is_udp = 0x1;
        }
    }

    p_dsipkeymask->route_lookup_mode = 1;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipv6_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, void* p_key)
{
    ds_ipv6_ucast_route_key_t* p_dsipkey;
    ds_ipv6_ucast_route_key_t* p_dsipkeymask;
    tbl_entry_t* p_tbl_ipkey = p_key;
    ipv6_addr_t   ipv6_mask;

    p_dsipkey = (ds_ipv6_ucast_route_key_t*)p_tbl_ipkey->data_entry;
    p_dsipkeymask = (ds_ipv6_ucast_route_key_t*)p_tbl_ipkey->mask_entry;
    sal_memset(p_dsipkey, 0, sizeof(ds_ipv6_ucast_route_key_t));
    sal_memset(p_dsipkeymask, 0, sizeof(ds_ipv6_ucast_route_key_t));

    p_dsipkey->vrf_id3_0 = p_ipuc_info->vrf_id & 0xf;
    p_dsipkey->vrf_id13_4 = p_ipuc_info->vrf_id >> 4;
    p_dsipkey->ip_da31_0 = p_ipuc_info->ip.ipv6[0];
    p_dsipkey->ip_da63_32 = p_ipuc_info->ip.ipv6[1];
    p_dsipkey->ip_da71_64 = p_ipuc_info->ip.ipv6[2] & 0xff;
    p_dsipkey->ip_da103_72 = (p_ipuc_info->ip.ipv6[2] >> 8) | (p_ipuc_info->ip.ipv6[3] << 24);
    p_dsipkey->ip_da127_104 = p_ipuc_info->ip.ipv6[3] >> 8;
    p_dsipkey->table_id0 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id1 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id2 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id3 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id4 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id5 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id6 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->table_id7 = p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6];
    p_dsipkey->sub_table_id1 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6];
    p_dsipkey->sub_table_id3 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6];
    p_dsipkey->sub_table_id5 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6];
    p_dsipkey->sub_table_id7 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6];

    p_dsipkeymask->table_id0 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id1 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id2 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id3 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id4 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id5 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id6 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->table_id7 = p_gb_ipuc_master[lchip]->tcam_key_table_id_mask;
    p_dsipkeymask->sub_table_id1 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;
    p_dsipkeymask->sub_table_id3 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;
    p_dsipkeymask->sub_table_id5 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;
    p_dsipkeymask->sub_table_id7 = p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask;

    if (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_DEFAULT))
    {
        p_dsipkeymask->vrf_id3_0 = 0xf;
        p_dsipkeymask->vrf_id13_4 = 0x3ff;
        IPV6_LEN_TO_MASK(ipv6_mask, p_ipuc_info->masklen);
        p_dsipkeymask->ip_da31_0 = ipv6_mask[0];
        p_dsipkeymask->ip_da63_32 = ipv6_mask[1];
        p_dsipkeymask->ip_da71_64 = ipv6_mask[2] & 0xff;
        p_dsipkeymask->ip_da103_72 = (ipv6_mask[2] >> 8) | (ipv6_mask[3] << 24);
        p_dsipkeymask->ip_da127_104 = ipv6_mask[3] >> 8;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    ds_ipv4_ucast_route_key_t v4_key, v4_mask;
    ds_ipv6_ucast_route_key_t v6_key, v6_mask;
    tbl_entry_t tbl_ipkey;
    uint16 ad_offset = 0;
    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    ad_offset = p_ipuc_info->ad_offset;
    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        tbl_ipkey.data_entry = (uint32*)&v4_key;
        tbl_ipkey.mask_entry = (uint32*)&v4_mask;
    }
    else
    {
        tbl_ipkey.data_entry = (uint32*)&v6_key;
        tbl_ipkey.mask_entry = (uint32*)&v6_mask;
        if(p_gb_ipuc_db_master[lchip]->tcam_share_mode)
        {
            ad_offset = p_ipuc_info->ad_offset / 2;
        }
    }

    p_gb_ipuc_master[lchip]->write_key[SYS_IPUC_VER(p_ipuc_info)](lchip, p_ipuc_info, &tbl_ipkey);
    cmd = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_key_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, ad_offset, cmd, &tbl_ipkey);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC TCAM: p_ipuc_info->ad_offset:%d \n", ad_offset);


    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;
    uint16 ad_offset = 0;
    SYS_IPUC_DBG_FUNC();

    if ((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && p_gb_ipuc_db_master[lchip]->tcam_share_mode)
    {
        ad_offset = p_ipuc_info->ad_offset / 2;
    }
    else
    {
        ad_offset = p_ipuc_info->ad_offset;
    }

    DRV_TCAM_TBL_REMOVE(lchip, p_gb_ipuc_master[lchip]->tcam_key_table[SYS_IPUC_VER(p_ipuc_info)],
                        ad_offset);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: ad_offset:%d \n", ad_offset);

    return ret;
}

int32
_sys_greatbelt_ipuc_write_nat_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    ds_ipv4_nat_key_t v4_key, v4_mask;
    tbl_entry_t tbl_ipkey;
    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&v4_key, 0, sizeof(ds_ipv4_nat_key_t));
    sal_memset(&v4_mask, 0, sizeof(ds_ipv4_nat_key_t));


    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC NAT TCAM: p_nat_info->ad_offset:%d \n", p_nat_info->ad_offset);

    v4_key.ip_sa = p_nat_info->ipv4[0];
    v4_mask.ip_sa = 0xFFFFFFFF;

    if (p_nat_info->l4_src_port)
    {
        v4_key.l4_source_port = p_nat_info->l4_src_port;
        v4_key.is_tcp = p_nat_info->is_tcp_port;
        v4_key.is_udp = !p_nat_info->is_tcp_port;

        v4_mask.l4_source_port = 0xFFFF;
        v4_mask.is_tcp = 0x1;
        v4_mask.is_udp = 0x1;
    }

    v4_key.table_id0 = IPV4SA_TABLEID;
    v4_key.table_id1 = IPV4SA_TABLEID;
    v4_key.sub_table_id0 = IPV4SA_NAT_SUB_TABLEID;
    v4_key.sub_table_id1 = IPV4SA_NAT_SUB_TABLEID;

    v4_mask.table_id0 = 0x7;
    v4_mask.table_id1 = 0x7;
    v4_mask.sub_table_id0 = 0x3;
    v4_mask.sub_table_id1 = 0x3;


    tbl_ipkey.data_entry = (uint32*)&v4_key;
    tbl_ipkey.mask_entry = (uint32*)&v4_mask;

    cmd = DRV_IOW(DsIpv4NatKey_t, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, p_nat_info->ad_offset, cmd, &tbl_ipkey);

    return CTC_E_NONE;

}

int32
_sys_greatbelt_ipuc_remove_nat_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    int32 ret = CTC_E_NONE;

    SYS_IPUC_DBG_FUNC();

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC NAT: p_nat_info->ad_offset:%d \n", p_nat_info->ad_offset);


    DRV_TCAM_TBL_REMOVE(lchip, DsIpv4NatKey_t, p_nat_info->ad_offset);


    return ret;
}

int32
_sys_greatbelt_ipuc_alloc_hash_lpm_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 size = 0;
    sys_lpm_result_t* p_lpm_result = NULL;
    int32 ret = CTC_E_NONE;

    SYS_IPUC_DBG_FUNC();
    /* malloc p_lpm_result */
    size = (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) ? sizeof(sys_lpm_result_v4_t) : sizeof(sys_lpm_result_t);
    p_lpm_result = mem_malloc(MEM_IPUC_MODULE, size);
    if (NULL == p_lpm_result)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_lpm_result, 0, size);
    LPM_RLT_INIT_POINTER(p_ipuc_info, p_lpm_result);
    p_ipuc_info->lpm_result = p_lpm_result;

    /* build hash soft db and hard index */
    CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_alloc_key_index(lchip, p_ipuc_info));
    if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        return CTC_E_NONE;
    }

    /* for NAPT, only use hash */
    if (p_ipuc_info->l4_dst_port > 0)
    {
        return CTC_E_NONE;
    }

    /* build lpm soft db and build hard index */
    if (((p_ipuc_info->masklen > 32) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen > 16) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))
        || ((p_ipuc_info->masklen > 8) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (p_gb_ipuc_master[lchip]->use_hash8)))
    {
        ret = _sys_greatbelt_lpm_add(lchip, p_ipuc_info);
        if (ret < 0)
        {
            _sys_greatbelt_l3_hash_free_key_index(lchip, p_ipuc_info);
            return ret;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_update_lpm_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (p_ipuc_info->l4_dst_port > 0)
    {
        return CTC_E_NONE;
    }

    if (((p_ipuc_info->masklen > 32) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen > 16) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))
        || ((p_ipuc_info->masklen > 8) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (p_gb_ipuc_master[lchip]->use_hash8)))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_lpm_update_nexthop(lchip, p_ipuc_info));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_update(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_ipuc_db_rpf_profile_t rpf_profile;
    sys_rpf_rslt_t rpf_rslt;
    uint32 update_ad = FALSE;
    bool do_lpm = TRUE;
    int32 ret = CTC_E_NONE;
    uint32 old_ad_offset = 0xFFFFFFFF;
    sys_ipuc_info_t ipuc_info_old;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));
    sal_memset(&ipuc_info_old, 0, sizeof(sys_ipuc_info_t));

    /* save old ipuc info */
    sal_memcpy(&ipuc_info_old, p_ipuc_info, p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
    /* save old ad_offset */
    old_ad_offset = p_ipuc_info->ad_offset;

    /* 1. remove old ad resource */
    _sys_greatbelt_ipuc_unbind_nexthop(lchip, &ipuc_info_old);

    rpf_profile.key.nh_id = ipuc_info_old.nh_id;
    rpf_profile.key.route_flag = ipuc_info_old.route_flag;
    ret = _sys_greatbelt_ipuc_remove_rpf(lchip, &rpf_profile);

    SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);
    p_ipuc_info->ad_offset = ipuc_info_old.ad_offset;   /* save used for big tcam */

    /* 2.Judge do lpm */
    if (CTC_FLAG_ISSET(ipuc_info_old.conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        do_lpm = FALSE;
    }

    /* 3. build ipda, include lpm ad profile */
    CTC_ERROR_GOTO(_sys_greatbelt_ipuc_build_ipda(lchip, p_ipuc_info, &rpf_rslt, do_lpm, p_ipuc_param->stats_id), ret, IPUC_UPDATE_ERROR_RETURN1);
    if (do_lpm)
    {
        update_ad = (p_ipuc_info->ad_offset == old_ad_offset);
        if (!update_ad)
        {
            CTC_ERROR_GOTO(_sys_greatbelt_ipuc_update_lpm_key_index(lchip, p_ipuc_info), ret, IPUC_UPDATE_ERROR_RETURN1);
        }
    }
    else
    {
        update_ad = TRUE;
    }

    /* 4. write ipda */
    _sys_greatbelt_ipuc_write_ipda(lchip, p_ipuc_info, &rpf_rslt);

    /* 5. write lpm key */
    if (!update_ad)
    {
        /* write lpm key entry */
        _sys_greatbelt_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

        /* write hash key entry */
        _sys_greatbelt_l3_hash_add_key(lchip, p_ipuc_info);
    }

    if (do_lpm)
    {
        /* free old lpm ad profile */
        _sys_greatbelt_ipuc_lpm_ad_profile_remove(lchip, &ipuc_info_old);
    }

    if (p_ipuc_info->masklen == p_gb_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
    {
        if (CTC_FLAG_ISSET(ipuc_info_old.route_flag, CTC_IPUC_FLAG_NEIGHBOR)
            && (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)))
        {
            p_gb_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] --;
        }
        else if (!CTC_FLAG_ISSET(ipuc_info_old.route_flag, CTC_IPUC_FLAG_NEIGHBOR)
            && (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)))
        {
            p_gb_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] ++;
        }
    }

    return CTC_E_NONE;

IPUC_UPDATE_ERROR_RETURN1:
    /* restore old ipuc info */
    sal_memcpy(p_ipuc_info, &ipuc_info_old, p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
    /* bind old nhid */
    _sys_greatbelt_ipuc_bind_nexthop(lchip, &ipuc_info_old,0,FALSE);
    /* add old rpf */
    rpf_profile.key.nh_id = ipuc_info_old.nh_id;
    rpf_profile.key.route_flag = ipuc_info_old.route_flag;
    _sys_greatbelt_ipuc_add_rpf(lchip, &rpf_profile, &rpf_rslt);
    return ret;
}

#define __3_OTHER__
STATIC int32
_sys_greatbelt_ipuc_flag_check(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_nh_info_dsnh_t nh_info;

    CTC_PTR_VALID_CHECK(p_ipuc_param);
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_nhinfo(lchip, p_ipuc_param->nh_id, &nh_info));

    /* neighbor flag check */
    if ((p_ipuc_param->masklen != p_gb_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
    {
        /* clear neighbor flag */
        CTC_UNSET_FLAG(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR);
    }

    /* icmp flag check */
    if ((nh_info.ecmp_valid) && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_ICMP_CHECK))
    {
        /* clear icmp-check flag */
        CTC_UNSET_FLAG(p_ipuc_param->route_flag, CTC_IPUC_FLAG_ICMP_CHECK);
    }

    /* NAPT check */
    if (p_ipuc_param->l4_dst_port > 0)
    {
        if (p_ipuc_param->masklen != p_gb_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        {
            return CTC_E_IPUC_NAT_NOT_SUPPORT_THIS_MASK_LEN;
        }

        if (p_ipuc_param->ip_ver == CTC_IP_VER_6)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        if (p_ipuc_param->vrf_id > 0xFF)    /* hash key only have 8 bit */
        {
            return CTC_E_IPUC_INVALID_VRF;
        }
    }

    return CTC_E_NONE;
}

#define ___________IPUC_OUTER_FUNCTION________________________
#define __0_IPUC_API__
int32
sys_greatbelt_ipuc_add(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_rpf_rslt_t rpf_rslt;
    bool do_lpm = TRUE;
    int32 ret = CTC_E_NONE;
    sys_ipuc_info_t ipuc_info;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    ctc_ipuc_param_t *p_ipuc_param = NULL;
    ctc_ipuc_param_t ipuc_param_tmp = {0};

    p_ipuc_param = &ipuc_param_tmp;
    sal_memcpy(p_ipuc_param,p_ipuc_param_info,sizeof(ctc_ipuc_param_t));

    /* param check and debug out */
    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_param);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    if (0 == p_ipuc_param->masklen)
    {
        ret = sys_greatbelt_ipuc_add_default_entry(lchip, p_ipuc_param->ip_ver, p_ipuc_param->vrf_id, p_ipuc_param->nh_id);
        if(CTC_E_NONE == ret)
        {
            sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));

            p_ipuc_info = &ipuc_info;

            SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
            SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));

            if (NULL == p_ipuc_info)
            {
                p_ipuc_info = mem_malloc(MEM_IPUC_MODULE,  p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
                if (NULL == p_ipuc_info)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_ipuc_info, 0, p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);

                SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
                SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

                p_ipuc_info->route_flag |=
                    SYS_IPUC_FLAG_DEFAULT | CTC_IPUC_FLAG_RPF_CHECK;

                SYS_IPUC_LOCK(lchip);
                _sys_greatbelt_ipuc_db_add(lchip, p_ipuc_info);
                SYS_IPUC_UNLOCK(lchip);
            }

            p_ipuc_info->ad_offset =
                p_gb_ipuc_master[lchip]->default_base[SYS_IPUC_VER_VAL(ipuc_info)] +
                (p_ipuc_info->vrf_id & 0x3FFF);
            p_ipuc_info->nh_id = p_ipuc_param->nh_id;
        }

        return ret;
    }

    /* prepare data */
    sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    /* para check */
    CTC_ERROR_RETURN(_sys_greatbelt_ipuc_flag_check(lchip, p_ipuc_param));

    p_ipuc_info = &ipuc_info;

    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
    SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

    SYS_IPUC_LOCK(lchip);
    /* 1. lookup sw node */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));

    if (NULL != p_ipuc_info)
    {
        CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_update(lchip, p_ipuc_info, p_ipuc_param));
    }
    else
    {
        p_ipuc_info = mem_malloc(MEM_IPUC_MODULE,  p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
        if (NULL == p_ipuc_info)
        {
            CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_NO_MEMORY);
        }

        sal_memset(p_ipuc_info, 0, p_gb_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);

        SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
        SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

        /* 2.Judge do lpm */
        _sys_greatbelt_ipuc_do_lpm_check(lchip, p_ipuc_info, &do_lpm);

        /* 3. build ipda, include lpm ad profile */
        CTC_ERROR_GOTO(_sys_greatbelt_ipuc_build_ipda(lchip, p_ipuc_info, &rpf_rslt, do_lpm, p_ipuc_param->stats_id), ret, IPUC_ADD_ERROR_RETURN2);
        if (do_lpm)
        {
            ret = _sys_greatbelt_ipuc_alloc_hash_lpm_key_index(lchip, p_ipuc_info);
            if (ret != CTC_E_NONE)
            {
                goto IPUC_ADD_ERROR_RETURN1;
            }

            if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
            {
                do_lpm = FALSE;
                /* free lpm ad profile */
                _sys_greatbelt_ipuc_lpm_ad_profile_remove(lchip, p_ipuc_info);
            }
        }

        /* 4. alloc tcam ad index */
        if (!do_lpm)
        {
            CTC_ERROR_GOTO(_sys_greatbelt_ipuc_alloc_tcam_ad_index(lchip, p_ipuc_info), ret, IPUC_ADD_ERROR_RETURN1);
        }

        /* 5. write ipda */
        if (do_lpm)  /* for write tcam ad, must write ipda latter */
        {
            _sys_greatbelt_ipuc_write_ipda(lchip, p_ipuc_info, &rpf_rslt);
        }

        /* 6. write ip key (if do_lpm = true, write lpm key to asic, else write Tcam key) */
        if (do_lpm)
        {
            /* write ipuc lpm key entry */
            _sys_greatbelt_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

            /* write ipuc hash key entry */
            _sys_greatbelt_l3_hash_add_key(lchip, p_ipuc_info);

            p_gb_ipuc_master[lchip]->lpm_counter ++;
        }
        else
        {
            _sys_greatbelt_ipuc_write_key_ex(lchip, p_ipuc_info, &rpf_rslt);

            p_gb_ipuc_master[lchip]->tcam_counter[p_ipuc_param->ip_ver] ++;
        }

        /* 7. write to soft table */
        _sys_greatbelt_ipuc_db_add(lchip, p_ipuc_info);

        /* stats */
        if (p_ipuc_info->masklen == p_gb_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        {
            p_gb_ipuc_master[lchip]->longest_counter[p_ipuc_param->ip_ver] ++;
            if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
            {
                p_gb_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] ++;
            }
        }
    }

    CTC_RETURN_IPUC_UNLOCK(CTC_E_NONE);

IPUC_ADD_ERROR_RETURN1:
    _sys_greatbelt_ipuc_unbuild_ipda(lchip, p_ipuc_info, do_lpm);

IPUC_ADD_ERROR_RETURN2:
    if(p_ipuc_info->lpm_result)
    {
        mem_free(p_ipuc_info->lpm_result);
    }
    mem_free(p_ipuc_info);
    CTC_RETURN_IPUC_UNLOCK(ret);
}

/**
 @brief function of remove ip route

 @param[in] p_ipuc_param, parameters used to remove ip route

 @return CTC_E_XXX
 */
int32
sys_greatbelt_ipuc_remove(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_ipuc_db_rpf_profile_t rpf_profile;
    sys_ipuc_info_t ipuc_info;
    int32            ret            = CTC_E_NONE;
    ctc_ipuc_param_t *p_ipuc_param = NULL;
    ctc_ipuc_param_t ipuc_param_tmp = {0};

    SYS_IPUC_INIT_CHECK(lchip);

    p_ipuc_param = &ipuc_param_tmp;
    sal_memcpy(p_ipuc_param,p_ipuc_param_info,sizeof(ctc_ipuc_param_t));

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_param);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    if (0 == p_ipuc_param->masklen)
    {
        ret = sys_greatbelt_ipuc_add_default_entry(lchip, p_ipuc_param->ip_ver, p_ipuc_param->vrf_id, SYS_NH_RESOLVED_NHID_FOR_DROP);
        if(CTC_E_NONE == ret)
        {
            sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));

            p_ipuc_info = &ipuc_info;

            SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
            SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));

            if (NULL != p_ipuc_info)
            {
                SYS_IPUC_LOCK(lchip);
                _sys_greatbelt_ipuc_db_remove(lchip, p_ipuc_info);
                mem_free(p_ipuc_info);
                SYS_IPUC_UNLOCK(lchip);
            }
        }

        return ret;
    }

    /* prepare data */
    sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));
    p_ipuc_info = &ipuc_info;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);

    SYS_IPUC_LOCK(lchip);
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));
    if (!p_ipuc_info)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {   /*do TCAM remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM remove\n");

        /* remove ipuc key entry from TCAM */
        _sys_greatbelt_ipuc_remove_key_ex(lchip, p_ipuc_info);

        /* TCAM do not need to build remove offset, only need to clear soft offset info */
        _sys_greatbelt_ipuc_free_tcam_ad_index(lchip, p_ipuc_info);
    }
    else
    {   /* do LPM remove */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "LPM remove\n");

        /* free lpm index */
        _sys_greatbelt_lpm_del(lchip, p_ipuc_info);

        /* write lpm key entry */
        _sys_greatbelt_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

        /* write hash key entry */
        _sys_greatbelt_l3_hash_remove_key(lchip, p_ipuc_info);

        /* free hash index */
        _sys_greatbelt_l3_hash_free_key_index(lchip, p_ipuc_info);

        /* free LPM DsIpda profile */
        _sys_greatbelt_ipuc_lpm_ad_profile_remove(lchip, p_ipuc_info);
    }

    /* write to soft table */
    _sys_greatbelt_ipuc_db_remove(lchip, p_ipuc_info);

    /* unbind nexthop info */
    _sys_greatbelt_ipuc_unbind_nexthop(lchip, p_ipuc_info);

    /* remove rpf profile */
    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));

    rpf_profile.key.nh_id = p_ipuc_info->nh_id;
    rpf_profile.key.route_flag = p_ipuc_info->route_flag;

    _sys_greatbelt_ipuc_remove_rpf(lchip, &rpf_profile);

    /* decrease counter */
    if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        p_gb_ipuc_master[lchip]->tcam_counter[p_ipuc_param->ip_ver]--;
    }
    else
    {
        p_gb_ipuc_master[lchip]->lpm_counter--;
    }

    if (p_ipuc_info->masklen == p_gb_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
    {
        p_gb_ipuc_master[lchip]->longest_counter[p_ipuc_param->ip_ver] --;
        if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            p_gb_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] --;
        }
    }

    if (p_ipuc_info->lpm_result)
    {
        mem_free(p_ipuc_info->lpm_result);
    }

    mem_free(p_ipuc_info);

    SYS_IPUC_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_get(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_ipuc_info_t ipuc_info;
    sys_ipuc_info_t* p_ipuc_info = NULL;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_param);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    sal_memset(&ipuc_info, 0, sizeof(ipuc_info));

    p_ipuc_info = &ipuc_info;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);

    CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));
    if (NULL != p_ipuc_info)
    {
        p_ipuc_param->nh_id = p_ipuc_info->nh_id;
        p_ipuc_param->route_flag = p_ipuc_info->route_flag;
        CTC_UNSET_FLAG(p_ipuc_param->route_flag, SYS_IPUC_FLAG_IS_IPV6);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_add_default_entry(uint8 lchip, uint8 ip_ver, uint16 vrfid, uint32 nh_id)
{
    sys_nh_info_dsnh_t nhinfo;
    sys_nh_update_dsnh_param_t update_dsnh;
    sys_ipuc_info_t ipuc_info;
    sys_rpf_rslt_t rpf_rslt;
    sys_rpf_info_t rpf_info;

    SYS_IPUC_DBG_FUNC();
    SYS_IP_CHECK_VERSION_ENABLE(ip_ver);

    sal_memset(&ipuc_info, 0, p_gb_ipuc_master[lchip]->info_size[ip_ver]);
    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;

    CTC_ERROR_RETURN(sys_greatbelt_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    ipuc_info.nh_id = nh_id;
    ipuc_info.route_flag |= (ip_ver == CTC_IP_VER_6) ? SYS_IPUC_FLAG_IS_IPV6 : 0;

    SYS_IPUC_LOCK(lchip);
    /* get ds_fwd */
    sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));

    ipuc_info.ad_offset = p_gb_ipuc_master[lchip]->default_base[SYS_IPUC_VER_VAL(ipuc_info)] + (vrfid & 0x3FFF);
    ipuc_info.route_flag |= SYS_IPUC_FLAG_DEFAULT | CTC_IPUC_FLAG_RPF_CHECK;

    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_write_ipda(lchip, &ipuc_info, &rpf_rslt));

    p_gb_ipuc_master[lchip]->default_route_nhid[ip_ver][vrfid] = nh_id;
    SYS_IPUC_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_add_nat_sa_default_entry(uint8 lchip)
{
    uint32 entry_num = 0;
    ds_ipv4_nat_key_t v4_key, v4_mask;
    tbl_entry_t tbl_ipkey;
    uint32 fwd_offset;
    ds_ip_sa_nat_t dsnatsa;

    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&v4_key, 0, sizeof(ds_ipv4_nat_key_t));
    sal_memset(&v4_mask, 0, sizeof(ds_ipv4_nat_key_t));
    sal_memset(&dsnatsa, 0, sizeof(ds_ip_sa_nat_t));


    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4NatKey_t, &entry_num));

    /* write ad */
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, &fwd_offset));
    dsnatsa.ip_sa_fwd_ptr_valid = 1;

    cmd = DRV_IOW(DsIpv4SaNatTcam_t, DRV_ENTRY_FLAG);


    dsnatsa.ip_sa16_0 = fwd_offset;
    DRV_IOCTL(lchip, (entry_num - 1), cmd, &dsnatsa);


    /* write key */
    v4_key.table_id0 = IPV4SA_TABLEID;
    v4_key.table_id1 = IPV4SA_TABLEID;
    v4_key.sub_table_id0 = IPV4SA_NAT_SUB_TABLEID;
    v4_key.sub_table_id1 = IPV4SA_NAT_SUB_TABLEID;

    v4_mask.table_id0 = 0x7;
    v4_mask.table_id1 = 0x7;
    v4_mask.sub_table_id0 = 0x3;
    v4_mask.sub_table_id1 = 0x3;


    tbl_ipkey.data_entry = (uint32*)&v4_key;
    tbl_ipkey.mask_entry = (uint32*)&v4_mask;

    cmd = DRV_IOW(DsIpv4NatKey_t, DRV_ENTRY_FLAG);


    DRV_IOCTL(lchip, (entry_num - 1), cmd, &tbl_ipkey);



    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_add_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param)
{
    int32  ret = CTC_E_NONE;
    uint32 table_id = DsIpSaNat_t;
    uint32 ad_offset = 0;
    uint8  do_hash = 1;
    sys_ipuc_nat_sa_info_t nat_info;
    sys_ipuc_nat_sa_info_t* p_nat_info = NULL;

    /* param check and debug out */
    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_nat_sa_param);
    CTC_IP_VER_CHECK(p_ipuc_nat_sa_param->ip_ver);
    CTC_MAX_VALUE_CHECK(p_ipuc_nat_sa_param->ip_ver, CTC_IP_VER_4)
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_nat_sa_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_nat_sa_param);
    if (p_ipuc_nat_sa_param->l4_src_port > 0)
    {
        if (p_ipuc_nat_sa_param->vrf_id > 0xFF)    /* port hash key only have 8 bit */
        {
            return CTC_E_IPUC_INVALID_VRF;
        }
    }
     /*SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);*/

    /* prepare data */
    sal_memset(&nat_info, 0, sizeof(nat_info));
    p_nat_info = &nat_info;

    SYS_IPUC_NAT_KEY_MAP(p_ipuc_nat_sa_param, p_nat_info);
    SYS_IPUC_NAT_DATA_MAP(p_ipuc_nat_sa_param, p_nat_info);

    SYS_IPUC_LOCK(lchip);
    /* 1. lookup sw node */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_nat_db_lookup(lchip, &p_nat_info));
    if (NULL != p_nat_info)
    {
        CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_EXIST);
    }

    p_nat_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_nat_sa_info_t));
    if (NULL == p_nat_info)
    {
        CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_NO_MEMORY);
    }
    sal_memset(p_nat_info, 0, sizeof(sys_ipuc_nat_sa_info_t));

    SYS_IPUC_NAT_KEY_MAP(p_ipuc_nat_sa_param, p_nat_info);
    SYS_IPUC_NAT_DATA_MAP(p_ipuc_nat_sa_param, p_nat_info);

    /* check hash enable */
    if (!CTC_FLAG_ISSET(p_gb_ipuc_master[lchip]->lookup_mode[p_ipuc_nat_sa_param->ip_ver], SYS_IPUC_HASH_LOOKUP))
    {
        do_hash = FALSE;
    }

    /* 2. alloc key offset */
    if (do_hash)
    {
        ret = _sys_greatbelt_l3_hash_alloc_nat_key_index(lchip, p_nat_info);
        if (ret)    /* hash conflict */
        {
            do_hash = 0;
        }
    }

    if (!do_hash)
    {
        CTC_ERROR_GOTO(_sys_greatbelt_ipuc_alloc_nat_tcam_ad_index(lchip, p_nat_info), ret, IPUC_ADD_ERROR_RETURN2);
        p_nat_info->in_tcam = 1;
    }

    /* 3. alloc ad offset */
    if (do_hash)
    {
        CTC_ERROR_GOTO(sys_greatbelt_ftm_alloc_table_offset(lchip, table_id, 0, 1, &ad_offset), ret, IPUC_ADD_ERROR_RETURN1);
        p_nat_info->ad_offset = ad_offset;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "NAT hash ad index %d\r\n", ad_offset);
    }
    else
    {
        p_nat_info->ad_offset = p_nat_info->key_offset;
    }

    /* 4. write nat ad */
    _sys_greatbelt_ipuc_nat_write_ipsa(lchip, p_nat_info);

    /* 5. write nat key (if do_hash = true, write hash key to asic, else write Tcam key) */
    if (do_hash)
    {
        /* write ipuc hash key entry */
        _sys_greatbelt_l3_hash_add_nat_sa_key(lchip, p_nat_info);

        p_gb_ipuc_master[lchip]->lpm_counter ++;
        p_gb_ipuc_master[lchip]->nat_lpm_counter ++;
    }
    else
    {
        _sys_greatbelt_ipuc_write_nat_key(lchip, p_nat_info);

        p_gb_ipuc_master[lchip]->nat_tcam_counter[p_ipuc_nat_sa_param->ip_ver] ++;
    }

    /* 6. write to soft table */
    _sys_greatbelt_ipuc_nat_db_add(lchip, p_nat_info);

    CTC_RETURN_IPUC_UNLOCK(CTC_E_NONE);

IPUC_ADD_ERROR_RETURN1:
    _sys_greatbelt_l3_hash_free_nat_key_index(lchip, p_nat_info);

IPUC_ADD_ERROR_RETURN2:
    mem_free(p_nat_info);
    CTC_RETURN_IPUC_UNLOCK(ret);
}

int32
sys_greatbelt_ipuc_remove_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param)
{
    sys_ipuc_nat_sa_info_t nat_info;
    sys_ipuc_nat_sa_info_t* p_nat_info = NULL;

    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_nat_sa_param);
    CTC_IP_VER_CHECK(p_ipuc_nat_sa_param->ip_ver);
    CTC_MAX_VALUE_CHECK(p_ipuc_nat_sa_param->ip_ver, CTC_IP_VER_4)
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_nat_sa_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_nat_sa_param);
     /*SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);*/

    /* prepare data */
    sal_memset(&nat_info, 0, sizeof(nat_info));
    p_nat_info = &nat_info;

    SYS_IPUC_NAT_KEY_MAP(p_ipuc_nat_sa_param, p_nat_info);

    SYS_IPUC_LOCK(lchip);
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_nat_db_lookup(lchip, &p_nat_info));
    if (!p_nat_info)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (p_nat_info->in_tcam)
    {   /*do TCAM remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM remove\n");

        /* remove ipuc key entry from TCAM */
        _sys_greatbelt_ipuc_remove_nat_key(lchip, p_nat_info);

        /* TCAM do not need to build remove offset, only need to clear soft offset info */
        _sys_greatbelt_ipuc_free_nat_tcam_ad_index(lchip, p_nat_info);
    }
    else
    {   /* do HASH remove */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "HASH remove\n");

        /* write hash key entry */
        _sys_greatbelt_l3_hash_remove_nat_sa_key(lchip, p_nat_info);

        /* free hash index */
        _sys_greatbelt_l3_hash_free_nat_key_index(lchip, p_nat_info);

        /* free hash ad offset */
        sys_greatbelt_ftm_free_table_offset(lchip, DsIpSaNat_t, 0, 1, p_nat_info->ad_offset);
    }

    /* write to soft table */
    _sys_greatbelt_ipuc_nat_db_remove(lchip, p_nat_info);

    /* decrease counter */
    if (p_nat_info->in_tcam)
    {
        p_gb_ipuc_master[lchip]->nat_tcam_counter[p_ipuc_nat_sa_param->ip_ver] --;
    }
    else
    {
        p_gb_ipuc_master[lchip]->lpm_counter --;
        p_gb_ipuc_master[lchip]->nat_lpm_counter --;
    }

    SYS_IPUC_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC uint32
_sys_greatbelt_ipuc_get_scl_gid(uint8 lchip, uint8 type)
{
    uint32  gid = 0;
    switch(type)
    {
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV4:
    case SYS_SCL_KEY_TCAM_TUNNEL_IPV6:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL;
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE :
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        gid = SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL;
        break;
    default: /*never*/
        break;
    }

    return gid;
}

int32
sys_greatbelt_ipuc_add_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param_info)
{
    sys_scl_entry_t*  p_scl_entry = NULL;
    sys_scl_entry_t*  p_scl_entry_rpf = NULL;
    uint32          gid = 0;
    uint32 fwd_offset;
    uint16 max_vrfid_num = 0;
    uint16 policer_ptr = 0;
    uint16 stats_ptr = 0;
    uint8 policer_type = 0;
    uint16 service_policer_id = 0;
    uint8 gre_options_num = 0;
    int32 ret = CTC_E_NONE;
    ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param = NULL;
    ctc_ipuc_tunnel_param_t ipuc_tunnel_param_tmp = {0};
    p_ipuc_tunnel_param = &ipuc_tunnel_param_tmp;
    sal_memcpy(p_ipuc_tunnel_param, p_ipuc_tunnel_param_info, sizeof(ctc_ipuc_tunnel_param_t));

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_tunnel_param);
    CTC_IP_VER_CHECK(p_ipuc_tunnel_param->ip_ver);
    SYS_IP_TUNNEL_KEY_CHECK(p_ipuc_tunnel_param);
    SYS_IP_TUNNEL_AD_CHECK(p_ipuc_tunnel_param);
    SYS_IP_TUNNEL_ADDRESS_SORT(p_ipuc_tunnel_param);
    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_QUEUE_EN))
        {
            CTC_NOT_EQUAL_CHECK(p_ipuc_tunnel_param->service_id, 0);
            SYS_LOGIC_PORT_CHECK(p_ipuc_tunnel_param->logic_port);
        }
        if (p_ipuc_tunnel_param->logic_port)
        {
            /* bind logic port */
            SYS_LOGIC_PORT_CHECK(p_ipuc_tunnel_param->logic_port);
        }
        if (( 0 == p_ipuc_tunnel_param->policer_id) && CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_POLICER_EN))
        {
            CTC_NOT_EQUAL_CHECK(p_ipuc_tunnel_param->service_id, 0);
        }
    }
    SYS_IP_TUNNEL_FUNC_DBG_DUMP(p_ipuc_tunnel_param);

    p_scl_entry = (sys_scl_entry_t*)mem_malloc(MEM_IPUC_MODULE, sizeof(sys_scl_entry_t));
    p_scl_entry_rpf = (sys_scl_entry_t*)mem_malloc(MEM_IPUC_MODULE, sizeof(sys_scl_entry_t));
    if((NULL == p_scl_entry) || (NULL == p_scl_entry_rpf))
    {
        ret = CTC_E_NO_MEMORY;
        goto out;
    }
    sal_memset(p_scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(p_scl_entry_rpf, 0, sizeof(sys_scl_entry_t));
    p_scl_entry_rpf->key.type = SYS_SCL_KEY_NUM;

    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        /* build scl key */
        _sys_greatbelt_ipuc_build_tunnel_key(lchip, p_scl_entry, p_ipuc_tunnel_param);

        /* get ds_fwd */
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD))
        {
            CTC_ERROR_GOTO(sys_greatbelt_nh_get_dsfwd_offset(lchip, p_ipuc_tunnel_param->nh_id, &fwd_offset), ret, out);
        }

        /* build scl action */
        p_scl_entry->action.type   = SYS_SCL_ACTION_TUNNEL;
        p_scl_entry->action.u.tunnel_action.is_tunnel = 1;
        p_scl_entry->action.u.tunnel_action.tunnel_packet_type =
            (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE) ? 0 :
            (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4) ? PKT_TYPE_IPV4 : PKT_TYPE_IPV6;

        if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
        {
            gre_options_num = CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY)
                              + CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_CHECKSUM)
                              + CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_SEQ_NUM);
            p_scl_entry->action.u.tunnel_action.tunnel_payload_offset = (1 + gre_options_num)*2;
        }
        p_scl_entry->action.u.tunnel_action.inner_packet_lookup =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD) ? 0 : 1;
        p_scl_entry->action.u.tunnel_action.ttl_check_en =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_TTL_CHECK) ? 1 : 0;
        p_scl_entry->action.u.tunnel_action.ttl_update =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_USE_OUTER_TTL) ? 0 : 1;
        p_scl_entry->action.u.tunnel_action.isatap_check_en =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_ISATAP_CHECK_EN) ? 1 : 0;
        p_scl_entry->action.u.tunnel_action.tunnel_rpf_check_request = 1;
        p_scl_entry->action.u.tunnel_action.acl_qos_use_outer_info =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_ACL_LKUP_BY_OUTER_HEAD) ? 1 : 0;
        p_scl_entry->action.u.tunnel_action.service_acl_qos_en =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_ACL_EN) ? 1 : 0;
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_QUEUE_EN))
        {
            p_scl_entry->action.u.tunnel_action.src_queue_select = 1;
            CTC_ERROR_GOTO(sys_greatbelt_qos_bind_service_logic_port(lchip, p_ipuc_tunnel_param->service_id,
                                                                           p_ipuc_tunnel_param->logic_port), ret, out);
        }

        if (p_ipuc_tunnel_param->logic_port)
        {
            /* bind logic port */
            p_scl_entry->action.u.tunnel_action.binding_data_low.logic_src_port = p_ipuc_tunnel_param->logic_port & 0x3FFF;
        }

        if (p_ipuc_tunnel_param->policer_id)
        {
            service_policer_id = p_ipuc_tunnel_param->policer_id;
            policer_type = CTC_QOS_POLICER_TYPE_FLOW;
        }
        else if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_POLICER_EN))
        {
            service_policer_id = p_ipuc_tunnel_param->service_id;
            policer_type = CTC_QOS_POLICER_TYPE_SERVICE;
        }

        if (service_policer_id)
        {
            CTC_ERROR_GOTO(sys_greatbelt_qos_policer_index_get(lchip,
                                                                 policer_type,
                                                                 service_policer_id,
                                                                 &policer_ptr), ret, out);

            if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
            {
                ret = CTC_E_INVALID_PARAM;
                goto out;
            }

            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_POLICER_EN))
            {
                p_scl_entry->action.u.tunnel_action.binding_data_high.service_policer_valid = 1;
                p_scl_entry->action.u.tunnel_action.binding_data_high.service_policer_mode = 0;    /* not support hierachial policer now */
            }

            p_scl_entry->action.u.tunnel_action.chip.policer_ptr = policer_ptr;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_STATS_EN))
        {
            CTC_ERROR_GOTO(sys_greatbelt_stats_get_statsptr(lchip, p_ipuc_tunnel_param->stats_id, &stats_ptr), ret, out);
            p_scl_entry->action.u.tunnel_action.chip.stats_ptr  = stats_ptr;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_DISCARD))
        {
            p_scl_entry->action.u.tunnel_action.ds_fwd_ptr_valid = 1;
            p_scl_entry->action.u.tunnel_action.fid = 0xFFFF;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD))
        {
            p_scl_entry->action.u.tunnel_action.ds_fwd_ptr_valid = 1;
            p_scl_entry->action.u.tunnel_action.fid = fwd_offset & 0xffff;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_INNER_VRF_EN))
        {
            max_vrfid_num = sys_greatbelt_l3if_get_max_vrfid(lchip, p_ipuc_tunnel_param->ip_ver);
            if (p_ipuc_tunnel_param->vrf_id >= max_vrfid_num)
            {
                ret = CTC_E_IPUC_INVALID_VRF;
                goto out;
            }

            p_scl_entry->action.u.tunnel_action.fid = 1 << 14 | p_ipuc_tunnel_param->vrf_id;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            CTC_BIT_SET(p_scl_entry->action.u.tunnel_action.tunnel_gre_options, 1);
        }
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_CHECKSUM))
        {
            CTC_BIT_SET(p_scl_entry->action.u.tunnel_action.tunnel_gre_options, 2);
        }
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_SEQ_NUM))
        {
            CTC_BIT_SET(p_scl_entry->action.u.tunnel_action.tunnel_gre_options, 0);
        }

        /* write entry */
        gid = _sys_greatbelt_ipuc_get_scl_gid(lchip, p_scl_entry->key.type);

        ret = sys_greatbelt_scl_add_entry(lchip, gid, p_scl_entry, 1);
        if (CTC_E_ENTRY_EXIST == ret)
        {
            /* update entry action */
            CTC_ERROR_GOTO(sys_greatbelt_scl_get_entry_id(lchip, p_scl_entry, gid), ret, out);
            CTC_ERROR_GOTO(sys_greatbelt_scl_update_action(lchip, p_scl_entry->entry_id, &p_scl_entry->action, 1), ret, out);
        }
        else if (ret < 0)
        {
            goto out;
        }
        else
        {
            if (CTC_FLAG_ISSET(p_scl_entry->key.u.tcam_tunnel_ipv6_key.flag, SYS_SCL_TUNNEL_IPSA_VALID))
            {   /* IPSA should be high priority */
                CTC_ERROR_GOTO(sys_greatbelt_scl_set_entry_priority(lchip, p_scl_entry->entry_id, 100, 1), ret, error0);
            }

            CTC_ERROR_GOTO(sys_greatbelt_scl_install_entry(lchip, p_scl_entry->entry_id, 1), ret, error0);
        }
    }
    else if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK)
        && (CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver))
    {
        p_scl_entry_rpf->key.type   = SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF;
        p_scl_entry_rpf->key.u.hash_tunnel_ipv4_rpf_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;

        /* set scl action */
        p_scl_entry_rpf->action.type   = SYS_SCL_ACTION_TUNNEL;
        p_scl_entry_rpf->action.u.tunnel_action.is_tunnel    = 1;
        p_scl_entry_rpf->action.u.tunnel_action.rpf_check_en = 1;

        /* tunnel RPF check */
        p_scl_entry_rpf->action.u.tunnel_action.logic_port_type = 1;

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD))
        {
            sys_greatbelt_nh_get_l3ifid(lchip, p_ipuc_tunnel_param->nh_id, &(p_ipuc_tunnel_param->l3_inf[0]));
            p_ipuc_tunnel_param->l3_inf[1] = 0;
            p_ipuc_tunnel_param->l3_inf[2] = 0;
            p_ipuc_tunnel_param->l3_inf[3] = 0;
            if (p_ipuc_tunnel_param->l3_inf[0] == SYS_L3IF_INVALID_L3IF_ID)
            {
                ret = CTC_E_L3IF_INVALID_IF_ID;
                goto out;
            }
        }

        /* rpf ifid upto 4 interface , 0 means invalid */
        /* tunnel_rpf_if_id_valid0 */
        if (p_ipuc_tunnel_param->l3_inf[0] > SYS_GB_MAX_CTC_L3IF_ID)
        {
            ret = CTC_E_L3IF_INVALID_IF_ID;
            goto out;
        }

        p_scl_entry_rpf->action.u.tunnel_action.service_acl_qos_en   = (0 == p_ipuc_tunnel_param->l3_inf[0]) ? 0 : 1;
        /* tunnel_rpf_if_id_valid1 */
        if (p_ipuc_tunnel_param->l3_inf[1] > SYS_GB_MAX_CTC_L3IF_ID)
        {
            ret = CTC_E_L3IF_INVALID_IF_ID;
            goto out;
        }

        p_scl_entry_rpf->action.u.tunnel_action.igmp_snoop_en        = (0 == p_ipuc_tunnel_param->l3_inf[1]) ? 0 : 1;
        /* tunnel_rpf_if_id_valid2 */
        if (p_ipuc_tunnel_param->l3_inf[2] > SYS_GB_MAX_CTC_L3IF_ID)
        {
            ret = CTC_E_L3IF_INVALID_IF_ID;
            goto out;
        }

        p_scl_entry_rpf->action.u.tunnel_action.isid_valid           = (0 == p_ipuc_tunnel_param->l3_inf[2]) ? 0 : 1;
        /* tunnel_rpf_if_id_valid3 */
        if (p_ipuc_tunnel_param->l3_inf[3] > SYS_GB_MAX_CTC_L3IF_ID)
        {
            ret = CTC_E_L3IF_INVALID_IF_ID;
            goto out;
        }

        p_scl_entry_rpf->action.u.tunnel_action.binding_en           = (0 == p_ipuc_tunnel_param->l3_inf[3]) ? 0 : 1;
        /* tunnel_more_rpf_if */
        p_scl_entry_rpf->action.u.tunnel_action.binding_mac_sa       =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_MORE_RPF) ? 1 : 0;

        /* tunnel_rpf_if_id0 */
        p_scl_entry_rpf->action.u.tunnel_action.binding_data_low.binding_data  |= p_ipuc_tunnel_param->l3_inf[0] & 0x3FF;
        /* tunnel_rpf_if_id1 */
        p_scl_entry_rpf->action.u.tunnel_action.binding_data_low.binding_data  |= (p_ipuc_tunnel_param->l3_inf[1] & 0x3FF) << 10;
        /* tunnel_rpf_if_id2 */
        p_scl_entry_rpf->action.u.tunnel_action.binding_data_low.binding_data  |= (p_ipuc_tunnel_param->l3_inf[2] & 0x3FF) << 20;
        /* tunnel_rpf_if_id3 */
        p_scl_entry_rpf->action.u.tunnel_action.binding_data_high.binding_data |= (p_ipuc_tunnel_param->l3_inf[3] >> 2) & 0xFF;
        p_scl_entry_rpf->action.u.tunnel_action.binding_data_low.binding_data  |= (p_ipuc_tunnel_param->l3_inf[3] & 0x3) << 30;

        gid = _sys_greatbelt_ipuc_get_scl_gid(lchip, p_scl_entry_rpf->key.type);
        ret = sys_greatbelt_scl_add_entry(lchip, gid, p_scl_entry_rpf, 1);
        if (CTC_E_ENTRY_EXIST == ret)
        {
            /* update entry action */
            CTC_ERROR_GOTO(sys_greatbelt_scl_get_entry_id(lchip, p_scl_entry_rpf, gid), ret, out);
            CTC_ERROR_GOTO(sys_greatbelt_scl_update_action(lchip, p_scl_entry_rpf->entry_id, &p_scl_entry_rpf->action, 1), ret, out);
        }
        else if (ret < 0)
        {
            goto out;
        }
        else
        {
            CTC_ERROR_GOTO(sys_greatbelt_scl_install_entry(lchip, p_scl_entry_rpf->entry_id, 1), ret, error0);
        }
    }

    ret = CTC_E_NONE;
    goto out;

error0:
    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        sys_greatbelt_scl_remove_entry(lchip, p_scl_entry_rpf->entry_id, 1);
    }
    else
    {
        sys_greatbelt_scl_remove_entry(lchip, p_scl_entry_rpf->entry_id, 1);
    }

out:
    if(p_scl_entry)
    {
        mem_free(p_scl_entry);
    }

    if (NULL == p_scl_entry_rpf)
    {
        mem_free(p_scl_entry_rpf);
    }

    return ret;
}

int32
sys_greatbelt_ipuc_remove_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param_info)
{
    sys_scl_entry_t scl_entry;
    uint32          gid = 0;
    ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param = NULL;
    ctc_ipuc_tunnel_param_t ipuc_tunnel_param_tmp = {0};
    p_ipuc_tunnel_param = &ipuc_tunnel_param_tmp;
    sal_memcpy(p_ipuc_tunnel_param, p_ipuc_tunnel_param_info, sizeof(ctc_ipuc_tunnel_param_t));

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_tunnel_param);
    CTC_IP_VER_CHECK(p_ipuc_tunnel_param->ip_ver);
    SYS_IP_TUNNEL_KEY_CHECK(p_ipuc_tunnel_param);
    SYS_IP_TUNNEL_ADDRESS_SORT(p_ipuc_tunnel_param);
    SYS_IP_TUNNEL_FUNC_DBG_DUMP(p_ipuc_tunnel_param);
    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));

    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        /* build scl key */
        _sys_greatbelt_ipuc_build_tunnel_key(lchip, &scl_entry, p_ipuc_tunnel_param);

        /* remove entry */
        gid = _sys_greatbelt_ipuc_get_scl_gid(lchip, scl_entry.key.type);
        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl_entry, gid));
        sys_greatbelt_scl_uninstall_entry(lchip, scl_entry.entry_id, 1);
        sys_greatbelt_scl_remove_entry(lchip, scl_entry.entry_id, 1);
    }
    else if ((CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver) && CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        sal_memset(&scl_entry, 0, sizeof(scl_entry));
        scl_entry.key.type          = SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF;
        scl_entry.key.u.hash_tunnel_ipv4_rpf_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;

        gid = 0;
        gid = _sys_greatbelt_ipuc_get_scl_gid(lchip, scl_entry.key.type);
        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &scl_entry, gid));
        sys_greatbelt_scl_uninstall_entry(lchip, scl_entry.entry_id, 1);
        sys_greatbelt_scl_remove_entry(lchip, scl_entry.entry_id, 1);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipuc_traverse_fn  fn, void* data)
{
    hash_traversal_fn  fun = _sys_greatbelt_ipuc_traverse_pre;
    sys_ipuc_traverse_t trav;

    SYS_IP_CHECK_VERSION_ENABLE(ip_ver);

    trav.data = data;
    trav.fn = fn;
    trav.lchip = lchip;
    if (NULL == fn)
    {
        return CTC_E_NONE;
    }

    return _sys_greatbelt_ipuc_db_traverse(lchip, ip_ver, fun, &trav);
}

int32
sys_greatbelt_ipuc_tcam_reinit(uint8 lchip, uint8 is_add)
{
    uint32 cmd = 0;
    uint32 entry_num = 0;
    ctc_ip_ver_t ip_ver = MAX_CTC_IP_VER;
    ipe_lookup_ctl_t ipe_lookup_ctl;
    fib_engine_lookup_ctl_t fib_engine_lookup_ctl;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;
    if (0 == is_add)
    {
        return CTC_E_NONE;
    }

    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_4] = (ipe_lookup_ctl.ip_da_lookup_ctl0 >> 2) & 0x7;
    p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_4] = ipe_lookup_ctl.ip_da_lookup_ctl0 & 0x3;
    p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6] = (ipe_lookup_ctl.ip_da_lookup_ctl2 >> 2) & 0x7;
    p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6] = ipe_lookup_ctl.ip_da_lookup_ctl2 & 0x3;

    sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
    cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en = p_gb_ipuc_master[lchip]->use_hash8 ? 1 : 0;
    fib_engine_lookup_ctl.ipv4_nat_da_lookup_en = 1;
    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmIpv4Hash8Key_t, &entry_num));
    if (0 == entry_num)
    {
        /* disable lpm, but default hash entry is used */
        sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
        cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

        fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en = 1;
        fib_engine_lookup_ctl.lpm_hash_mode |= 0x5;
        fib_engine_lookup_ctl.ipv4_nat_da_lookup_en = 0;
        cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));
    }

    for (ip_ver = CTC_IP_VER_4; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

        if (CTC_IP_VER_4 == ip_ver)
        {
            /* default_entry_index = (((lookup_result_ctl >> 1) & 0x3FF) << 6) + (p_fib_key->id.vsi_id & 0x3FFF);
            */
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
            fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl0 = (((p_gb_ipuc_master[lchip]->default_base[ip_ver] >> 6) & 0x3FF) << 1) + 1;
            fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl0 = (((p_gb_ipuc_master[lchip]->default_base[ip_ver] >> 6) & 0x3FF) << 1) + 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }
        else if (CTC_IP_VER_6 == ip_ver)
        {
            /* default_entry_index = (((lookup_result_ctl >> 1) & 0x3FF) << 6) + (p_fib_key->id.vsi_id & 0x3FFF);
            */
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
            fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl2 = (((p_gb_ipuc_master[lchip]->default_base[ip_ver] >> 6) & 0x3FF) << 1) + 1;
            fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl1 = (((p_gb_ipuc_master[lchip]->default_base[ip_ver] >> 6) & 0x3FF) << 1) + 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }
    }

    return CTC_E_NONE;
}
int32
sys_greatbelt_ipuc_init(uint8 lchip, ctc_ipuc_global_cfg_t* p_ipuc_global_cfg)
{
    int32  ret = 0;
    uint32 entry_num = 0;
    uint32 cmd = 0;
    uint32 offset = 0;
    uint16 max_vrf_num = 0;
    uint16 vrfid_block = 0;
    uint16 vrfid = 0;
    uint32 length = 0;
    ctc_ip_ver_t ip_ver = MAX_CTC_IP_VER;
    sys_greatbelt_opf_t opf;

    ipe_lookup_ctl_t ipe_lookup_ctl;
    fib_engine_lookup_ctl_t fib_engine_lookup_ctl;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(opf));

    p_gb_ipuc_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_master_t));
    if (NULL == p_gb_ipuc_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_ipuc_master[lchip], 0, sizeof(sys_ipuc_master_t));

    /* common init*/
    p_gb_ipuc_master[lchip]->tcam_mode = 1;
    p_gb_ipuc_master[lchip]->must_use_dsfwd = 1;
    p_gb_ipuc_master[lchip]->info_size[CTC_IP_VER_4] = sizeof(sys_ipuc_info_t) - (sizeof(ipv6_addr_t) - sizeof(ipv4_addr_t));
    p_gb_ipuc_master[lchip]->info_size[CTC_IP_VER_6] = sizeof(sys_ipuc_info_t);

    /* deal with vrfid */
    p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4] = sys_greatbelt_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6] = sys_greatbelt_l3if_get_max_vrfid(lchip, CTC_IP_VER_6);
    length = p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4] * sizeof(uint32);
    p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_4] = mem_malloc(MEM_IPUC_MODULE, length);
    if (NULL == p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_4])
    {
        return CTC_E_NO_MEMORY;
    }

    length = p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6] * sizeof(uint32);
    p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_6] = mem_malloc(MEM_IPUC_MODULE, length);
    if (NULL == p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_6])
    {
        return CTC_E_NO_MEMORY;
    }

    p_gb_ipuc_master[lchip]->max_mask_len[CTC_IP_VER_4] = CTC_IPV4_ADDR_LEN_IN_BIT;
    p_gb_ipuc_master[lchip]->max_mask_len[CTC_IP_VER_6] = CTC_IPV6_ADDR_LEN_IN_BIT;
    p_gb_ipuc_master[lchip]->addr_len[CTC_IP_VER_4] = CTC_IPV4_ADDR_LEN_IN_BYTE;
    p_gb_ipuc_master[lchip]->addr_len[CTC_IP_VER_6] = CTC_IPV6_ADDR_LEN_IN_BYTE;
    p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] = FALSE;
    p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_6] = FALSE;
    p_gb_ipuc_master[lchip]->lpm_da_table = DsIpDa_t;
    p_gb_ipuc_master[lchip]->conflict_da_table = LpmTcamAdMem_t;
    p_gb_ipuc_master[lchip]->conflict_key_table = DsLpmTcam80Key_t;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH8] = 0;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH16] = 0;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_HIGH32] = 0x40000;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_MID32] = 0x60000;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_LOW32] = 0x70000;
    p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask = 0x70000;
    p_gb_ipuc_master[lchip]->conflict_key_table_id = 0;
    p_gb_ipuc_master[lchip]->conflict_key_table_id_mask = 0x3;

    /* Get lookup mode from IPE_ROUTE_LOOKUP */
    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_4] = (ipe_lookup_ctl.ip_da_lookup_ctl0 >> 2) & 0x7;
    p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_4] = ipe_lookup_ctl.ip_da_lookup_ctl0 & 0x3;
    p_gb_ipuc_master[lchip]->tcam_key_table_id[CTC_IP_VER_6] = (ipe_lookup_ctl.ip_da_lookup_ctl2 >> 2) & 0x7;
    p_gb_ipuc_master[lchip]->tcam_key_sub_table_id[CTC_IP_VER_6] = ipe_lookup_ctl.ip_da_lookup_ctl2 & 0x3;
    p_gb_ipuc_master[lchip]->tcam_key_table_id_mask = 0x7;
    p_gb_ipuc_master[lchip]->tcam_key_sub_table_id_mask = 0x3;


    /* set pipeline3_en, NAPT en */
    sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
    cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en = (p_ipuc_global_cfg->use_hash8) ? 1 : 0;
    fib_engine_lookup_ctl.ipv4_nat_da_lookup_en = 1;
    p_gb_ipuc_master[lchip]->use_hash8 = fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en;
    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    /* ipv4 UNICAST TCAM */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4UcastRouteKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_TCAM_LOOKUP;
        p_gb_ipuc_master[lchip]->big_tcam_num[CTC_IP_VER_4] = entry_num;
    }

    /* ipv6 UNICAST TCAM */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv6UcastRouteKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_TCAM_LOOKUP;
        p_gb_ipuc_master[lchip]->big_tcam_num[CTC_IP_VER_6] = entry_num;
    }

    /* ipv4 nat sa TCAM */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4NatKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gb_ipuc_master[lchip]->nat_tcam_num[CTC_IP_VER_4] = entry_num;
    }

    /* ipv4/ipv6 UNICAST LPM LOOKUP, only need check hash resource */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmIpv4Hash8Key_t, &entry_num));
    if (entry_num > 0)
    {
        /* init l3 hash */
        CTC_ERROR_RETURN(sys_greatbelt_l3_hash_init(lchip));
        /* init lpm */
        CTC_ERROR_RETURN(_sys_greatbelt_lpm_init(lchip, CTC_IP_VER_4, p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4]));
        CTC_ERROR_RETURN(_sys_greatbelt_lpm_init(lchip, CTC_IP_VER_6, p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6]));

        p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_HASH_LOOKUP;
        p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_HASH_LOOKUP;
    }
    else
    {
        /* disable lpm, but default hash entry is used */
        sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
        cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

        fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en = 1;
        fib_engine_lookup_ctl.lpm_hash_mode |= 0x5;
        fib_engine_lookup_ctl.ipv4_nat_da_lookup_en = 0;
        cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));
    }

    p_gb_ipuc_master[lchip]->tcam_key_table[CTC_IP_VER_4] = DsIpv4UcastRouteKey_t;
    p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_4] = DsIpv4UcastDaTcam_t;
    p_gb_ipuc_master[lchip]->write_key[CTC_IP_VER_4] = _sys_greatbelt_ipv4_write_key;
    p_gb_ipuc_master[lchip]->tcam_sa_table[CTC_IP_VER_4] = DsIpv4RpfKey_t;

    p_gb_ipuc_master[lchip]->tcam_key_table[CTC_IP_VER_6] = DsIpv6UcastRouteKey_t;
    p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_6] = DsIpv6UcastDaTcam_t;
    p_gb_ipuc_master[lchip]->write_key[CTC_IP_VER_6] = _sys_greatbelt_ipv6_write_key;
    p_gb_ipuc_master[lchip]->tcam_sa_table[CTC_IP_VER_6] = DsIpv6RpfKey_t;

    if (p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] > 0)
    {
        p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] = TRUE;
    }

    if (p_gb_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] > 0)
    {
        p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_6] = TRUE;
    }

    SYS_IPUC_CREAT_LOCK(lchip);

    if ((p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] + p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_6]) == 0)
    {
        /* no resource for ipuc, not init ipuc */
        return CTC_E_NONE;
    }

    if ((p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4])
        ||(p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_6]))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_init(lchip));
    }

    CTC_ERROR_RETURN(sys_greatbelt_rpf_init(lchip));

    /* deal with default entry*/

    for (ip_ver = CTC_IP_VER_4; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        if (!p_gb_ipuc_master[lchip]->version_en[ip_ver])
        {
            continue;
        }

        /* For the ip version disabled. */
        if (0 == p_gb_ipuc_master[lchip]->max_vrfid[ip_ver])
        {
            vrfid_block = 1;
            /* only support vrfid 0. */
            p_gb_ipuc_master[lchip]->max_vrfid[ip_ver] = 1;
        }
        else
        {
            vrfid_block = (p_gb_ipuc_master[lchip]->max_vrfid[ip_ver] / SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM
            + ((p_gb_ipuc_master[lchip]->max_vrfid[ip_ver] % SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM) ? 1 : 0));
        }

        /* allocate ad offset for default entry*/
        max_vrf_num = vrfid_block * SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM;

        ret = sys_greatbelt_ftm_alloc_table_offset(lchip, DsIpDa_t, 1, max_vrf_num, &offset);
        if (ret)
        {
            return CTC_E_NO_RESOURCE;
        }

        offset = (offset + SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM - 1)
        / SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM * SYS_IPUC_DEFAULT_ENTRY_MIN_VRFID_NUM;

        p_gb_ipuc_master[lchip]->default_base[ip_ver] = offset;

        sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));
        cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

        if (CTC_IP_VER_4 == ip_ver)
        {
            /* default_entry_index = (((lookup_result_ctl >> 1) & 0x3FF) << 6) + (p_fib_key->id.vsi_id & 0x3FFF);
            */
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
            fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl0 = (((offset >> 6) & 0x3FF) << 1) + 1;
            fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl0 = (((offset >> 6) & 0x3FF) << 1) + 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }
        else if (CTC_IP_VER_6 == ip_ver)
        {
            /* default_entry_index = (((lookup_result_ctl >> 1) & 0x3FF) << 6) + (p_fib_key->id.vsi_id & 0x3FFF);
            */
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);

            fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl2 = (((offset >> 6) & 0x3FF) << 1) + 1;
            fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl1 = (((offset >> 6) & 0x3FF) << 1) + 1;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }

        for (vrfid = 0; vrfid < p_gb_ipuc_master[lchip]->max_vrfid[ip_ver]; vrfid++)
        {
            ret = sys_greatbelt_ipuc_add_default_entry(lchip, ip_ver, vrfid, SYS_NH_RESOLVED_NHID_FOR_DROP);
        }
    }

    /* NAT init */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4NatKey_t, &entry_num));

    sys_greatbelt_opf_init(lchip, OPF_IPV4_NAT, 1);
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;
    sys_greatbelt_opf_init_offset(lchip, &opf, 0, entry_num - 1);

    /* add nat sa default entry, use tcam default entry */
    if (entry_num > 0)
    {
        sys_greatbelt_ipuc_add_nat_sa_default_entry(lchip);
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_VRFID, p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4]-1));
    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_IPUC, sys_greatbelt_ipuc_tcam_reinit);
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_deinit(uint8 lchip)
{
    uint32 entry_num = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_ipuc_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, OPF_IPV4_NAT);

    sys_greatbelt_rpf_deinit(lchip);

    _sys_greatbelt_ipuc_db_deinit(lchip);

    sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmIpv4Hash8Key_t, &entry_num);
    if (entry_num > 0)
    {
        _sys_greatbelt_lpm_deinit(lchip);

        sys_greatbelt_l3_hash_deinit(lchip);
    }

    mem_free(p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_6]);
    mem_free(p_gb_ipuc_master[lchip]->default_route_nhid[CTC_IP_VER_4]);
    sal_mutex_destroy(p_gb_ipuc_master[lchip]->mutex);
    mem_free(p_gb_ipuc_master[lchip]);

    return CTC_E_NONE;
}


int32
sys_greatbelt_ipuc_reinit(uint8 lchip, bool use_hash8)
{
    fib_engine_lookup_ctl_t fib_engine_lookup_ctl;
    uint32 cmd = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* get pipeline3_en */
    sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
    cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    if (fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en == use_hash8)
    {
        return CTC_E_NONE;
    }

    fib_engine_lookup_ctl.lpm_ipv4_pipeline3_en = use_hash8;
    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    p_gb_ipuc_master[lchip]->use_hash8 = use_hash8;

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_set_global_property(uint8 lchip, ctc_ipuc_global_property_t* p_global_prop)
{
    uint32 cmdr = 0;
    uint32 cmdwl = 0;
    uint32 cmdwr = 0;
    ipe_lookup_route_ctl_t ipe_lookup_route_ctl;
    ipe_route_ctl_t ipe_route_ctl;
    void* vall = NULL;
    void* valr = NULL;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&ipe_lookup_route_ctl, 0, sizeof(ipe_lookup_route_ctl_t));
    cmdr = DRV_IOR(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_lookup_route_ctl));

    sal_memset(&ipe_route_ctl, 0, sizeof(ipe_route_ctl_t));
    cmdr = DRV_IOR(IpeRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_route_ctl));

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_V4_MARTIAN_CHECK_EN))
    {
        ipe_lookup_route_ctl.martian_check_en_low  = (p_global_prop->v4_martian_check_en) ? 0xF : 0;
        ipe_lookup_route_ctl.martian_address_check_disable =
            ((0 == ipe_lookup_route_ctl.martian_check_en_low)
             && (0 == ipe_lookup_route_ctl.martian_check_en_high)) ? 1 : 0;

        cmdwl = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
        vall = &ipe_lookup_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_V6_MARTIAN_CHECK_EN))
    {
        ipe_lookup_route_ctl.martian_check_en_high = (p_global_prop->v6_martian_check_en) ? 0x6FF : 0;
        ipe_lookup_route_ctl.martian_address_check_disable =
            ((0 == ipe_lookup_route_ctl.martian_check_en_low)
             && (0 == ipe_lookup_route_ctl.martian_check_en_high)) ? 1 : 0;

        cmdwl = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
        vall = &ipe_lookup_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_MCAST_MACDA_IP_UNMATCH_CHECK))
    {
        ipe_route_ctl.mcast_address_match_check_disable = (p_global_prop->mcast_match_check_en) ? 0 : 1;
        cmdwr = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
        valr = &ipe_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_TTL_THRESHOLD))
    {
        ipe_route_ctl.ip_ttl_limit = p_global_prop->ip_ttl_threshold;
        cmdwr = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
        valr = &ipe_route_ctl;
    }

    if (cmdwl != 0)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdwl, vall));
    }

    if (cmdwr != 0)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdwr, valr));
    }


    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_arrange_fragment(uint8 lchip)
{
    sys_ipuc_info_list_t* p_info_list = NULL;
    sys_ipuc_arrange_info_t* p_arrange_info_tbl[LPM_TABLE_MAX] = {NULL};
    sys_ipuc_arrange_info_t* p_arrange_tmp = NULL;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    sys_lpm_tbl_t* p_table_pre = NULL;
    uint32 head_offset = 0;
    uint32 old_offset = 0;
    uint32 new_pointer = 0;
    uint16 size = 0;
    uint8 stage = 0;
    uint8 i = 0;

    SYS_IPUC_DBG_FUNC();

    p_info_list = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_info_list_t));
    if (NULL == p_info_list)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_info_list, 0, sizeof(sys_ipuc_info_list_t));
    sal_memset(p_arrange_info_tbl, 0, sizeof(sys_ipuc_arrange_info_t*) * LPM_TABLE_MAX);

    SYS_IPUC_LOCK(lchip);
    _sys_greatbelt_ipuc_db_get_info(lchip, p_info_list);

    _sys_greatbelt_ipuc_db_anylize_info(lchip, p_info_list, p_arrange_info_tbl);

    for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
    {
        head_offset = 0;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n### start to arrange SRAM%d \n", i);

        while (p_arrange_info_tbl[i] != NULL)
        {
            stage = p_arrange_info_tbl[i]->p_data->stage;
            p_ipuc_info = p_arrange_info_tbl[i]->p_data->p_ipuc_info;
            p_lpm_result = p_ipuc_info->lpm_result;
            TABEL_GET_STAGE(stage, p_ipuc_info, p_table);
            if (!p_table)
            {
                SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when arrange offset\r\n");
                SYS_IPUC_UNLOCK(lchip);
                return CTC_E_UNEXPECT;
            }
            old_offset = p_table->offset;

            LPM_RLT_SET_OFFSET(p_ipuc_info, p_lpm_result, stage, p_table->offset);
            LPM_RLT_SET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage, p_table->sram_index);
            LPM_RLT_SET_IDX_MASK(p_ipuc_info, p_lpm_result, stage, p_table->idx_mask);

            LPM_INDEX_TO_SIZE(p_table->idx_mask, size);

            if (CTC_IP_VER_4 == SYS_IPUC_VER(p_arrange_info_tbl[i]->p_data->p_ipuc_info))
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "before arrange ipuc :%x  table[%d] ,old_offset = %d  head_offset = %d  size = %d\n",
                                 p_arrange_info_tbl[i]->p_data->p_ipuc_info->ip.ipv4[0], p_arrange_info_tbl[i]->p_data->stage, old_offset, head_offset, size);
            }
            else
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "before arrange ipuc :%x:%x:%x:%x  table[%d] ,old_offset = %d  head_offset = %d  size = %d\n",
                                 p_arrange_info_tbl[i]->p_data->p_ipuc_info->ip.ipv6[3], p_arrange_info_tbl[i]->p_data->p_ipuc_info->ip.ipv6[2],
                                 p_arrange_info_tbl[i]->p_data->p_ipuc_info->ip.ipv6[1], p_arrange_info_tbl[i]->p_data->p_ipuc_info->ip.ipv6[0],
                                 p_arrange_info_tbl[i]->p_data->stage, old_offset, head_offset, size);
            }

            if (old_offset <= head_offset)
            {
                if (p_arrange_info_tbl[i]->p_data)
                {
                    mem_free(p_arrange_info_tbl[i]->p_data);
                    p_arrange_info_tbl[i]->p_data = NULL;
                    p_arrange_tmp = p_arrange_info_tbl[i]->p_next_info;
                    mem_free(p_arrange_info_tbl[i]);
                }

                p_arrange_info_tbl[i] = p_arrange_tmp;
                if (old_offset == head_offset)
                {
                    head_offset += size;
                }

                continue;
            }

            if (LPM_TABLE_INDEX0 < stage)
            {
                TABEL_GET_STAGE((stage - 1), p_ipuc_info, p_table_pre);
            }
            else
            {
                p_table_pre = NULL;
            }

            if ((old_offset - head_offset) >= size)
            {
                CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatblet_lpm_offset_alloc(lchip, i, size, &new_pointer));
                new_pointer += (i << POINTER_OFFSET_BITS_LEN);
                CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_lpm_update(lchip, p_ipuc_info, stage, new_pointer,
                                                                            (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));
            }
            else
            {
                new_pointer = _sys_greatbelt_lpm_get_rsv_offset(lchip, i);
                new_pointer += (i << POINTER_OFFSET_BITS_LEN);

                CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_lpm_update(lchip, p_ipuc_info, stage, new_pointer,
                                                                            (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));

                old_offset = new_pointer;
                CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatblet_lpm_offset_alloc(lchip, i, size, &new_pointer));
                new_pointer += (i << POINTER_OFFSET_BITS_LEN);
                CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_lpm_update(lchip, p_ipuc_info, stage, new_pointer, old_offset));
            }

            if (p_arrange_info_tbl[i]->p_data)
            {
                mem_free(p_arrange_info_tbl[i]->p_data);
                p_arrange_info_tbl[i]->p_data = NULL;
                p_arrange_tmp = p_arrange_info_tbl[i]->p_next_info;
                mem_free(p_arrange_info_tbl[i]);
            }

            p_arrange_info_tbl[i] = p_arrange_tmp;
            head_offset += size;
        }
    }

    SYS_IPUC_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_update_ipda_self_address(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint8 self_address)
{
    uint32 cmd = 0;
    sys_ipuc_info_t ipuc_info;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    ds_ip_da_t dsipda;
    uint32 value = 0;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_param);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    sal_memset(&ipuc_info, 0, sizeof(ipuc_info));
    sal_memset(&dsipda, 0, sizeof(dsipda));

    value = self_address;
    p_ipuc_info = &ipuc_info;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);

    /* 1. lookup sw node */
    CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_info));
    if (NULL != p_ipuc_info)
    {
        if (!(CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL)))
        {
            cmd = DRV_IOW(p_gb_ipuc_master[lchip]->lpm_da_table, DsIpDa_SelfAddress_f);
        }
        else
        {
            cmd = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DsIpv4UcastDaTcam_SelfAddress_f);
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM  ");
        }

        DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmd, &value);
    }

    return CTC_E_NONE;
}

#define __1_SHOW__

int32
sys_greatbelt_ipuc_state_show(uint8 lchip)
{
    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPUC Working in %s mode\n",(p_gb_ipuc_master[lchip]->use_hash8 ? "HASH8" : "HASH16"));
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------IPUC(NAT-SA) Key Status---------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Key type                   |    size   |  allocated\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------\n");
    if (p_gb_l3hash_master[lchip])
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM hash key               |   0x%04x  |   0x%04x\n", p_gb_l3hash_master[lchip]->l3_hash_num, p_gb_ipuc_master[lchip]->hash_counter);
        sys_greatbelt_lpm_state_show(lchip);
    }
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM TCAM key size          |   0x%04x  |   0x%04x\n", 256, p_gb_ipuc_master[lchip]->conflict_tcam_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 TCAM key size         |   0x%04x  |   0x%04x\n", p_gb_ipuc_master[lchip]->big_tcam_num[CTC_IP_VER_4], p_gb_ipuc_master[lchip]->tcam_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV6 TCAM key size         |   0x%04x  |   0x%04x\n", p_gb_ipuc_master[lchip]->big_tcam_num[CTC_IP_VER_6], p_gb_ipuc_master[lchip]->tcam_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 NAT sa TCAM key size  |   0x%04x  |   0x%04x\n", p_gb_ipuc_master[lchip]->nat_tcam_num[CTC_IP_VER_4], p_gb_ipuc_master[lchip]->nat_tcam_counter[CTC_IP_VER_4]+1);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------IPUC Route(NAT-SA) Status---------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Route type               |  count\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM                      |  0x%08x\n", p_gb_ipuc_master[lchip]->lpm_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 TCAM                |  0x%08x\n", p_gb_ipuc_master[lchip]->tcam_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV6 TCAM                |  0x%08x\n", p_gb_ipuc_master[lchip]->tcam_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 NAT sa TCAM         |  0x%08x\n", p_gb_ipuc_master[lchip]->nat_tcam_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 NAT sa LPM          |  0x%08x\n", p_gb_ipuc_master[lchip]->nat_lpm_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ipv4 host                |  0x%08x\n", p_gb_ipuc_master[lchip]->host_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ipv6 host                |  0x%08x\n", p_gb_ipuc_master[lchip]->host_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ipv4 length 32           |  0x%08x\n", p_gb_ipuc_master[lchip]->longest_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ipv6 length 128          |  0x%08x\n", p_gb_ipuc_master[lchip]->longest_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");

    return CTC_E_NONE;
}

STATIC uint8
_sys_greatbelt_ipuc_mask_to_index(uint8 lchip, uint8 ip, uint8 index_mask)
{
    uint8 bits = 0;
    uint8 next = 0;
    uint8 index;

    for (index = 0; index < 8; index++)
    {
        if (IS_BIT_SET(index_mask, index))
        {
            if (IS_BIT_SET(ip, index))
            {
                SET_BIT(bits, next);
            }
            next++;
        }
    }

    return bits;
}

STATIC int32
_sys_greatbelt_ipuc_retrieve_lpm_lookup_key(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info, sys_ipuc_debug_info_t* debug_info)
{
    uint8 sram_index = 0;
    uint32 stage = 0;
    sys_lpm_tbl_t* p_table = NULL;
    uint8 masklen = 0;
    uint8  bucket = 0;

    for (stage = LPM_PIPE_LINE_STAGE0; stage < LPM_PIPE_LINE_STAGE_NUM; stage++)
    {
        TABEL_GET_STAGE(stage, p_sys_ipuc_info, p_table);

        if ((NULL == p_table) || (0 == p_table->count))
        {
            continue;
        }

        if (INVALID_POINTER_OFFSET == p_table->offset)
        {
            continue;
        }

        bucket = _sys_greatbelt_ipuc_mask_to_index(lchip, p_table->index, p_table->idx_mask);
        sram_index = p_table->sram_index;
        debug_info->lpm_key_index[sram_index] = p_table->offset + bucket;

        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info))
        {
            masklen = p_gb_ipuc_master[lchip]->use_hash8 ? (((stage+1) * 8) + 8) : (((stage+1) * 8) + 16);
        }
        else
        {
            masklen = ((stage+1) * 8) + 32;
        }

        if (p_sys_ipuc_info->masklen <= masklen)
        {
            debug_info->lpm_key[sram_index] = LPM_TYPE_NEXTHOP;
        }
        else
        {
            debug_info->lpm_key[sram_index] = LPM_TYPE_POINTER;
        }
    }

    return DRV_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_retrieve_hash_key(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info,
                                      sys_ipuc_hash_type_t sys_ipuc_hash_type,
                                      sys_ipuc_debug_info_t* debug_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_result_t* p_lpm_result = NULL;

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_sys_ipuc_info, &p_hash);
    if (!p_hash)
    {
        return CTC_E_UNEXPECT;
    }

    p_lpm_result = (sys_lpm_result_t*)p_sys_ipuc_info->lpm_result;

    if (SYS_IPUC_IPV4_HASH8 == sys_ipuc_hash_type)
    {
        if (p_sys_ipuc_info->masklen > 8)
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_POINTER];
            debug_info->hash_high_key = LPM_TYPE_POINTER;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_POINTER] ? TRUE : FALSE;
        }
        else
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            debug_info->hash_high_key = LPM_TYPE_NEXTHOP;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_NEXTHOP] ? TRUE : FALSE;
        }

    }
    else if (SYS_IPUC_IPV4_HASH16 == sys_ipuc_hash_type)
    {
        if (p_sys_ipuc_info->masklen > 16)
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_POINTER];
            debug_info->hash_high_key = LPM_TYPE_POINTER;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_POINTER] ? TRUE : FALSE;
        }
        else
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            debug_info->hash_high_key = LPM_TYPE_NEXTHOP;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_NEXTHOP] ? TRUE : FALSE;
        }
    }
    else if (SYS_IPUC_IPV6_HASH_HIGH32 == sys_ipuc_hash_type)
    {
        if (p_sys_ipuc_info->masklen > 32)
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_POINTER];
            debug_info->hash_high_key = LPM_TYPE_POINTER;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_POINTER] ? TRUE : FALSE;
        }
        else
        {
            debug_info->hash_high_key_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            debug_info->hash_high_key = LPM_TYPE_NEXTHOP;
            debug_info->high_in_small_tcam = p_hash->in_tcam[LPM_TYPE_NEXTHOP] ? TRUE : FALSE;
        }
    }
    else if (SYS_IPUC_IPV6_HASH_MID32 == sys_ipuc_hash_type)
    {
        debug_info->hash_mid_key_index = p_lpm_result->hash32mid_offset;
        if (CTC_FLAG_ISSET(p_sys_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID))
        {
            debug_info->mid_in_small_tcam = TRUE;
        }
    }
    else if (SYS_IPUC_IPV6_HASH_LOW32 == sys_ipuc_hash_type)
    {
        debug_info->hash_low_key_index = p_lpm_result->hash32low_offset;
        if (CTC_FLAG_ISSET(p_sys_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_LOW))
        {
            debug_info->low_in_small_tcam = TRUE;
        }
    }

    return DRV_E_NONE;
}

int32
sys_greatbelt_ipuc_retrieve_ipv4(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info)
{
    uint8  hash_bit_len = 0;
    sys_ipuc_debug_info_t debug_info;
    char str1[10];
    uint32 stage = 0;

    sal_memset(&debug_info, 0, sizeof(debug_info));
    debug_info.hash_high_key = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX0] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX1] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX2] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX3] = LPM_TYPE_NUM;

    hash_bit_len = p_gb_ipuc_master[lchip]->use_hash8 ? 8 : 16;

    if (CTC_FLAG_ISSET(p_sys_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        debug_info.big_tcam_index = p_sys_ipuc_info->ad_offset;
        debug_info.in_big_tcam = TRUE;
    }
    else
    {
        if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, SYS_IPUC_IPV4_HASH8, &debug_info));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, SYS_IPUC_IPV4_HASH16, &debug_info));
        }

        if (p_sys_ipuc_info->masklen > hash_bit_len)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_lpm_lookup_key(lchip, p_sys_ipuc_info, &debug_info));
        }
    }

    str1[0] = '\0';
    if (debug_info.hash_high_key != LPM_TYPE_NUM)
    {
        if (debug_info.hash_high_key == LPM_TYPE_NEXTHOP)
        {
            sal_strncat(str1, "O", 1);
        }
        else
        {
            sal_strncat(str1, "P", 1);
        }

        if (debug_info.high_in_small_tcam)
        {
            sal_strncat(str1, "T", 1);
        }
        else
        {
            sal_strncat(str1, " ", 1);
        }
    }
    else
    {
        sal_strncat(str1, "N ", 2);
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "O: nexthop, P: pointer, T: small tcam key, N: none\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Key type  | Big Tcam key | Hash key | lpm key0 | lpm key1 | lpm key2 | lpm key3\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Index     |    0x%04x    |  0x%04x  |  0x%04x  |  0x%04x  |  0x%04x  |  0x%04x\n",
        debug_info.big_tcam_index, debug_info.hash_high_key_index, debug_info.lpm_key_index[0], debug_info.lpm_key_index[1], debug_info.lpm_key_index[2], debug_info.lpm_key_index[3]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Indicate  |     %s       |   %s     ", (debug_info.in_big_tcam ? " O" : " N"), str1);

    for (stage = LPM_PIPE_LINE_STAGE0; stage < LPM_PIPE_LINE_STAGE_NUM ; stage ++)
    {
        str1[0] = '\0';
        if (debug_info.lpm_key[stage] != LPM_TYPE_NUM)
        {
            if (debug_info.lpm_key[stage] == LPM_TYPE_NEXTHOP)
            {
                sal_strncat(str1, "O", 1);
            }
            else
            {
                sal_strncat(str1, "P", 1);
            }
        }
        else
        {
            sal_strncat(str1, "N", 1);
        }
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "|     %s    ", str1);
    }
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n----------------------------------------------------------------------------------\n");

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_retrieve_ipv6(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info)
{
    sys_ipuc_debug_info_t debug_info;
    char str1[10];
    uint32 stage = 0;

    sal_memset(&debug_info, 0, sizeof(debug_info));
    debug_info.hash_high_key = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX0] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX1] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX2] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX3] = LPM_TYPE_NUM;

    if (CTC_FLAG_ISSET(p_sys_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        debug_info.big_tcam_index = p_gb_ipuc_db_master[lchip]->tcam_share_mode?
                                             (p_sys_ipuc_info->ad_offset / 2) : p_sys_ipuc_info->ad_offset;
        debug_info.in_big_tcam = TRUE;
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, SYS_IPUC_IPV6_HASH_HIGH32, &debug_info));

        if (p_sys_ipuc_info->masklen > 32)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_lpm_lookup_key(lchip, p_sys_ipuc_info, &debug_info));
        }

        if (128 == p_sys_ipuc_info->masklen)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, SYS_IPUC_IPV6_HASH_MID32, &debug_info));
            CTC_ERROR_RETURN(_sys_greatbelt_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, SYS_IPUC_IPV6_HASH_LOW32, &debug_info));
        }
    }

    str1[0] = '\0';
    if (debug_info.hash_high_key != LPM_TYPE_NUM)
    {
        if (debug_info.hash_high_key == LPM_TYPE_NEXTHOP)
        {
            sal_strncat(str1, "O", 1);
        }
        else
        {
            sal_strncat(str1, "P", 1);
        }

        if (debug_info.high_in_small_tcam)
        {
            sal_strncat(str1, "T", 1);
        }
        else
        {
            sal_strncat(str1, " ", 1);
        }
    }
    else
    {
        sal_strncat(str1, "N ", 2);
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "O: nexthop, P: pointer, T: small tcam key, M: mid, N: none\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Key type  | Big Tcam key | Hash high key | lpm key0 | lpm key1 | lpm key2 | lpm key3 | Hash mid key | Hash low key\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------------------------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Index     |    0x%04x    |     0x%04x    |  0x%04x  |  0x%04x  |  0x%04x  |  0x%04x  |    0x%04x    |    0x%04x\n",
        debug_info.big_tcam_index, debug_info.hash_high_key_index, debug_info.lpm_key_index[0], debug_info.lpm_key_index[1],
        debug_info.lpm_key_index[2], debug_info.lpm_key_index[3], debug_info.hash_mid_key_index, debug_info.hash_low_key_index);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Indicate  |     %s       |       %s      ", (debug_info.in_big_tcam ? " O" : " N"), str1);

    for (stage = LPM_PIPE_LINE_STAGE0; stage < LPM_PIPE_LINE_STAGE_NUM ; stage ++)
    {
        str1[0] = '\0';
        if (debug_info.lpm_key[stage] != LPM_TYPE_NUM)
        {
            if (debug_info.lpm_key[stage] == LPM_TYPE_NEXTHOP)
            {
                sal_strncat(str1, "O", 1);
            }
            else
            {
                sal_strncat(str1, "P", 1);
            }
        }
        else
        {
            sal_strncat(str1, "N", 1);
        }
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "|     %s    ", str1);
    }

    /* for mid */
    str1[0] = '\0';
    if (p_sys_ipuc_info->masklen == 128)
    {
        sal_strncat(str1, "M", 1);
        if (debug_info.mid_in_small_tcam)
        {
            sal_strncat(str1, "T", 1);
        }
        else
        {
            sal_strncat(str1, " ", 1);
        }
    }
    else
    {
        sal_strncat(str1, "N ", 2);
    }
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "|       %s     ", str1);

    /* for low */
    str1[0] = '\0';
    if (p_sys_ipuc_info->masklen == 128)
    {
        sal_strncat(str1, "O", 1);
        if (debug_info.mid_in_small_tcam)
        {
            sal_strncat(str1, "T", 1);
        }
        else
        {
            sal_strncat(str1, " ", 1);
        }
    }
    else
    {
        sal_strncat(str1, "N ", 2);
    }
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "|       %s     ", str1);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n-------------------------------------------------------------------------------------------------------------------\n");


    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_show_debug_info(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint8 detail)
{
    sys_ipuc_info_t ipuc_data;
    sys_ipuc_info_t* p_ipuc_data = NULL;
    sys_ipuc_traverse_t travs;

    SYS_IPUC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);

    travs.data = &detail;
    travs.lchip = lchip;
    /* prepare data */
    sal_memset(&ipuc_data, 0, sizeof(sys_ipuc_info_t));
    p_ipuc_data = &ipuc_data;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_data);

    SYS_IPUC_LOCK(lchip);
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_greatbelt_ipuc_db_lookup(lchip, &p_ipuc_data));

    if (!p_ipuc_data)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (detail)
    {
        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_data))
        {
            CTC_ERROR_RETURN_IPUC_UNLOCK(sys_greatbelt_ipuc_retrieve_ipv4(lchip, p_ipuc_data));
        }
        else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_data))
        {
            CTC_ERROR_RETURN_IPUC_UNLOCK(sys_greatbelt_ipuc_retrieve_ipv6(lchip, p_ipuc_data));
        }
    }
    else
    {
        IPUC_SHOW_HEADER (SYS_IPUC_VER(p_ipuc_data));
        sys_greatbelt_show_ipuc_info(p_ipuc_data, (void* )&travs);
    }
    SYS_IPUC_UNLOCK(lchip);

    return CTC_E_NONE;
}


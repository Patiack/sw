/**
 @file sys_goldengate_ipuc.c

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
#include "ctc_debug.h"
#include "ctc_hash.h"

#include "ctc_pdu.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_l3_hash.h"
#include "sys_goldengate_ipuc.h"
#include "sys_goldengate_ipuc_db.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_lpm.h"     /*tmp add for LPM_PIPE_LINE_STAGE_NUM*/
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_ipuc_db.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

struct sys_ipuc_debug_info_s
{
    uint32 tcam_index;
    uint32 host_hash_index;
    uint32 lpm_key_index[LPM_TABLE_MAX];

    sys_lpm_key_type_t  hash_high_key;
    sys_lpm_key_type_t  lpm_key[LPM_TABLE_MAX];

    uint8 in_host_hash;
};
typedef struct sys_ipuc_debug_info_s sys_ipuc_debug_info_t;

struct sys_ipuc_rpf_port_info_s
{
    uint8  rpf_port_num;
    uint32 rpf_port[SYS_GG_MAX_IPMC_RPF_IF];
};
typedef struct sys_ipuc_rpf_port_info_s sys_ipuc_rpf_port_info_t;

typedef uint32 (* updateIpDa_fn ) (uint8 lchip, void* data, void* change_nh_param);

extern sys_l3_hash_master_t* p_gg_l3hash_master[];
extern sys_ipuc_db_master_t* p_gg_ipuc_db_master[];
extern sys_lpm_master_t* p_gg_lpm_master[];
/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_ipuc_master_t* p_gg_ipuc_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_IPUC_CREAT_LOCK                   \
    do                                            \
    {                                             \
        sal_mutex_create(&p_gg_ipuc_master[lchip]->mutex); \
        if (NULL == p_gg_ipuc_master[lchip]->mutex)  \
        { \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX); \
        } \
    } while (0)

#define SYS_IPUC_LOCK \
    sal_mutex_lock(p_gg_ipuc_master[lchip]->mutex)

#define SYS_IPUC_UNLOCK \
    sal_mutex_unlock(p_gg_ipuc_master[lchip]->mutex)

#define CTC_ERROR_RETURN_IPUC_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_ipuc_master[lchip]->mutex); \
            return (rv); \
        } \
    } while (0)

#define CTC_RETURN_IPUC_UNLOCK(op) \
    do \
    { \
        sal_mutex_unlock(p_gg_ipuc_master[lchip]->mutex); \
        return (op); \
    } while (0)

#define SYS_IP_CHECK_VERSION_ENABLE(ver)                        \
    {                                                               \
        if ((!p_gg_ipuc_master[lchip]) || !p_gg_ipuc_master[lchip]->version_en[ver])     \
        {                                                           \
            return CTC_E_IPUC_VERSION_DISABLE;                      \
        }                                                           \
    }

#define SYS_IP_VRFID_CHECK(val)                                     \
    {                                                                   \
        if ((val)->vrf_id >= p_gg_ipuc_master[lchip]->max_vrfid[(val)->ip_ver])    \
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

int32
sys_goldengate_ipuc_wb_sync(uint8 lchip);
int32
sys_goldengate_ipuc_wb_restore(uint8 lchip);
int32
_sys_goldengate_ipuc_push_down(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 opt);

/****************************************************************************
 *
* Function
*
*****************************************************************************/
#if 0
STATIC uint8
SYS_IPUC_OCTO(uint32* ip32, sys_ip_octo_t index)
{
    return ((ip32[index / 4] >> ((index & 3) * 8)) & 0xFF);
}
#endif

#define ___________IPUC_INNER_FUNCTION________________________
#define __1_IPDA__
STATIC int32
_sys_goldengate_ipuc_write_rpf_profile(uint8 lchip, sys_rpf_rslt_t* p_rpf_rslt, sys_rpf_intf_t* p_rpf_intf)
{
    uint32 cmd = 0;
    DsRpf_m ds_rpf;

    SYS_IPUC_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    sal_memset(&ds_rpf, 0, sizeof(ds_rpf));

    /* ipv4 and ipv6 share the same data structure */
    SetDsRpf(V, array_0_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[0]);
    SetDsRpf(V, array_0_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[0]);
    SetDsRpf(V, array_1_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[1]);
    SetDsRpf(V, array_1_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[1]);
    SetDsRpf(V, array_2_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[2]);
    SetDsRpf(V, array_2_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[2]);
    SetDsRpf(V, array_3_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[3]);
    SetDsRpf(V, array_3_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[3]);

    SetDsRpf(V, array_4_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[4]);
    SetDsRpf(V, array_4_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[4]);
    SetDsRpf(V, array_5_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[5]);
    SetDsRpf(V, array_5_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[5]);
    SetDsRpf(V, array_6_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[6]);
    SetDsRpf(V, array_6_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[6]);
    SetDsRpf(V, array_7_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[7]);
    SetDsRpf(V, array_7_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[7]);

    SetDsRpf(V, array_8_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[8]);
    SetDsRpf(V, array_8_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[8]);
    SetDsRpf(V, array_9_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[9]);
    SetDsRpf(V, array_9_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[9]);
    SetDsRpf(V, array_10_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[10]);
    SetDsRpf(V, array_10_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[10]);
    SetDsRpf(V, array_11_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[11]);
    SetDsRpf(V, array_11_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[11]);

    SetDsRpf(V, array_12_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[12]);
    SetDsRpf(V, array_12_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[12]);
    SetDsRpf(V, array_13_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[13]);
    SetDsRpf(V, array_13_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[13]);
    SetDsRpf(V, array_14_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[14]);
    SetDsRpf(V, array_14_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[14]);
    SetDsRpf(V, array_15_rpfIfIdValid_f, &ds_rpf, p_rpf_intf->rpf_intf_valid[15]);
    SetDsRpf(V, array_15_rpfIfId_f,      &ds_rpf, p_rpf_intf->rpf_intf[15]);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s %d ds_rpf index 0x%x.\n", __FUNCTION__, __LINE__, p_rpf_rslt->id);

    cmd = DRV_IOW(DsRpf_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_rpf_rslt->id, cmd, &ds_rpf));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_add_rpf(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile, sys_rpf_rslt_t* p_rpf_rslt)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_info_dsnh_t nh_info_1;
    sys_rpf_intf_t rpf_intf;
    sys_rpf_info_t rpf_info;
    uint16 gport = 0;
    uint8 ecmp_cnt = 0;
    uint32 cmd = 0;
    uint32 rpf_check_port = 0;
    uint32 idx = 0;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_rpf_profile);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&nh_info_1, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&rpf_intf, 0, sizeof(sys_rpf_intf_t));
    rpf_info.intf = &rpf_intf;

    if (p_rpf_profile->key.route_flag & SYS_IPUC_FLAG_DEFAULT)
    {
        p_rpf_rslt->mode = SYS_RPF_CHK_MODE_PROFILE;
        p_rpf_rslt->id = SYS_RPF_PROFILE_DEFAULT_ENTRY;
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_rpf_profile->key.nh_id, &nh_info));

    cmd = DRV_IOR(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rpf_check_port));


    if ((nh_info.ecmp_valid)
        && CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        ecmp_cnt = (nh_info.ecmp_cnt > SYS_GG_MAX_IPMC_RPF_IF) ? SYS_GG_MAX_IPMC_RPF_IF : nh_info.ecmp_cnt;
        for (idx = 0; idx < ecmp_cnt; idx++)
        {
            if (!rpf_check_port)
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_get_l3ifid(lchip, nh_info.nh_array[idx], &rpf_intf.rpf_intf[idx]));
                if (rpf_intf.rpf_intf[idx] == SYS_L3IF_INVALID_L3IF_ID)
                {
                    return CTC_E_NH_INVALID_NH_TYPE;
                }
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nh_info.nh_array[idx], &nh_info_1));
                gport = ((nh_info_1.dest_chipid << 9) | nh_info_1.dest_id);
                rpf_intf.rpf_intf[idx] = gport;
            }
            rpf_intf.rpf_intf_valid[idx] = 1;
        }

        rpf_info.force_profile = FALSE;
        rpf_info.usage = SYS_RPF_USAGE_TYPE_IPUC;
        rpf_info.profile_index = SYS_RPF_INVALID_PROFILE_ID;

        CTC_ERROR_RETURN(sys_goldengate_rpf_add_profile(lchip, &rpf_info, p_rpf_rslt));

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: rpf mode = %d rpf id = %d\n", p_rpf_rslt->mode, p_rpf_rslt->id);

        if ((SYS_RPF_CHK_MODE_PROFILE == p_rpf_rslt->mode) && (SYS_RPF_INVALID_PROFILE_ID != p_rpf_rslt->id))
        {
            CTC_ERROR_RETURN(_sys_goldengate_ipuc_write_rpf_profile(lchip, p_rpf_rslt, &rpf_intf));
        }
    }
    else if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_RPF_CHECK)
        && rpf_check_port
        && p_rpf_profile->key.rpf_port_num > 1)
    {
        for (idx = 0; idx < p_rpf_profile->key.rpf_port_num; idx++)
        {
            rpf_intf.rpf_intf[idx] = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_rpf_profile->key.rpf_port[idx]);
            rpf_intf.rpf_intf_valid[idx] = 1;
        }

        rpf_info.force_profile = FALSE;
        rpf_info.usage = SYS_RPF_USAGE_TYPE_IPUC;
        rpf_info.profile_index = SYS_RPF_INVALID_PROFILE_ID;

        CTC_ERROR_RETURN(sys_goldengate_rpf_add_profile(lchip, &rpf_info, p_rpf_rslt));

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: rpf mode = %d rpf id = %d\n", p_rpf_rslt->mode, p_rpf_rslt->id);

        if ((SYS_RPF_CHK_MODE_PROFILE == p_rpf_rslt->mode) && (SYS_RPF_INVALID_PROFILE_ID != p_rpf_rslt->id))
        {
            CTC_ERROR_RETURN(_sys_goldengate_ipuc_write_rpf_profile(lchip, p_rpf_rslt, &rpf_intf));
        }
    }
    else
    {
        if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_ICMP_CHECK)
            || CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_RPF_CHECK))
        {
            /* icmp check must use interfaceId*/
            if ((!rpf_check_port)
                || CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_ICMP_CHECK))
            {
                if (CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_CONNECT)
                    || CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_ASSIGN_PORT))
                {
                    p_rpf_rslt->id = p_rpf_profile->key.l3if;
                }
                else
                {
                    CTC_ERROR_RETURN(sys_goldengate_nh_get_l3ifid(lchip, p_rpf_profile->key.nh_id, &(p_rpf_rslt->id)));
                    if ((p_rpf_rslt->id == SYS_L3IF_INVALID_L3IF_ID) && (SYS_NH_TYPE_MPLS != nh_info.nh_entry_type))
                    {
                        return CTC_E_NH_INVALID_NH_TYPE;
                    }
                }
            }
            else
            {
                if ((CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_CONNECT)
                    || CTC_FLAG_ISSET(p_rpf_profile->key.route_flag, CTC_IPUC_FLAG_ASSIGN_PORT))
                    && (p_rpf_profile->key.rpf_port_num == 0))
                {
                    return CTC_E_INVALID_PARAM;
                }
                if (p_rpf_profile->key.rpf_port_num > 0)
                {
                    p_rpf_rslt->id = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_rpf_profile->key.rpf_port[0]);
                }
                else
                {
                    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_rpf_profile->key.nh_id, &nh_info_1));
                    gport = ((nh_info_1.dest_chipid << 9) | nh_info_1.dest_id);
                    p_rpf_rslt->id = gport;
                }
            }
            p_rpf_rslt->mode = SYS_RPF_CHK_MODE_IFID;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_remove_rpf(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 cmd = 0;
    uint8 rpf_mode = 0;
    uint16 rpf_id = 0;
    DsIpDa_m dsipda;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&dsipda, 0, sizeof(dsipda));

    if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        cmd = DRV_IOR(DsIpDa_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmd, &dsipda);

        rpf_mode = GetDsIpDa(V, rpfCheckMode_f, &dsipda);
        rpf_id = GetDsIpDa(V, rpfIfId_f, &dsipda);

        if (!rpf_mode)
        {
            CTC_ERROR_RETURN(sys_goldengate_rpf_remove_profile(lchip, rpf_id));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_remove_rpf_by_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt)
{
    SYS_IPUC_DBG_FUNC();

    if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
        && (!p_rpf_rslt->mode))
    {
        CTC_ERROR_RETURN(sys_goldengate_rpf_remove_profile(lchip, p_rpf_rslt->id));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_get_ipda_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_nh_info_dsnh_t* p_dsnh_info, DsIpDa_m* p_dsipda)
{
    uint32 dest_id = 0;
    uint8 gchip_id = 0;
    uint16 lport = 0;
    uint32   dsnh_offset = 0;
    uint8 adjust_len_idx = 0;

    SYS_IPUC_DBG_FUNC();

    dsnh_offset = p_dsnh_info->dsnh_offset;
    if (CTC_IS_CPU_PORT(p_dsnh_info->gport))
    {
        dest_id = SYS_ENCODE_EXCP_DESTMAP(p_dsnh_info->dest_chipid, SYS_PKT_CPU_QDEST_BY_DMA);
        dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
    }
    else if (p_dsnh_info->drop_pkt)
    {
        sys_goldengate_get_gchip_id(lchip, &gchip_id);
        dest_id = SYS_ENCODE_DESTMAP(gchip_id, SYS_RSV_PORT_DROP_ID);
    }
    else if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_ASSIGN_PORT))
    {
        gchip_id = CTC_MAP_GPORT_TO_GCHIP(p_ipuc_info->gport);
        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_ipuc_info->gport);
        dest_id = SYS_ENCODE_DESTMAP(gchip_id, lport);
    }
    else if (p_dsnh_info->is_ecmp_intf)
    {
        dest_id = (0x3d << 12) | p_dsnh_info->ecmp_group_id;    /* special for ecmp */
    }
    else if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_TO_FABRIC))
    {
        dest_id = SYS_ENCODE_DESTMAP(p_dsnh_info->dest_chipid, 0x0FF);
    }
    else if( p_dsnh_info->is_mcast)
    {

        uint8   speed = 0;
        dest_id =SYS_ENCODE_MCAST_IPE_DESTMAP(speed, p_dsnh_info->dest_id);
    }
    else
    {
          dest_id =SYS_ENCODE_DESTMAP(p_dsnh_info->dest_chipid, p_dsnh_info->dest_id);
    }
    SetDsIpDa(V, u1_g2_adApsBridgeEn_f, p_dsipda, p_dsnh_info->aps_en);
    SetDsIpDa(V, u1_g2_adDestMap_f, p_dsipda, dest_id);
    SetDsIpDa(V, u1_g2_adNextHopPtr_f, p_dsipda, dsnh_offset);
    SetDsIpDa(V, u1_g2_adNextHopExt_f, p_dsipda, p_dsnh_info->nexthop_ext);
    if(0 != p_dsnh_info->adjust_len)
    {
        sys_goldengate_lkup_adjust_len_index(lchip, p_dsnh_info->adjust_len, &adjust_len_idx);
        SetDsIpDa(V, u1_g2_adLengthAdjustType_f, p_dsipda, adjust_len_idx);
    }
    else
    {
        SetDsIpDa(V, u1_g2_adLengthAdjustType_f, p_dsipda, 0);
    }
    SetDsIpDa(V, nextHopPtrValid_f, p_dsipda, 1);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_update_ipda(uint8 lchip, void* data, void* change_nh_param)
{
    sys_ipuc_info_t* p_ipuc_info = data;
    sys_nh_info_dsnh_t* p_dsnh_info = change_nh_param;
    DsIpDa_m   dsipda;
    uint32 cmdr;
    uint32 cmdw;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&dsipda, 0, sizeof(dsipda));

    cmdr = DRV_IOR(DsIpDa_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(DsIpDa_t, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmdr, &dsipda);
    if(GetDsIpDa(V, nextHopPtrValid_f, &dsipda) == 0)
    {
         return CTC_E_NONE;
    }

    _sys_goldengate_ipuc_get_ipda_nexthop(lchip, p_ipuc_info, p_dsnh_info, &dsipda);

    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmdw, &dsipda);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC update_da: p_ipuc_info->ad_offset:0x%x   drop\n",
                     p_ipuc_info->ad_offset);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_bind_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_update_dsnh_param_t update_dsnh;
    uint8 bind_nh = 0;
    int32 ret = 0;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));



   /* host route or nexthop use merge dsfwd mode */
    if (!nh_info.dsfwd_valid  && !nh_info.ecmp_valid && !nh_info.merge_dsfwd  && (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_ASSIGN_PORT)))
    {
        /* host route, write dsnh to ipda */
        update_dsnh.data = p_ipuc_info;
        update_dsnh.updateAd = _sys_goldengate_ipuc_update_ipda;
        ret = sys_goldengate_nh_bind_dsfwd_cb(lchip, p_ipuc_info->nh_id, &update_dsnh);
        if(CTC_E_NH_HR_NH_IN_USE != ret)
        {
             bind_nh = 1;
             CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH);
        }
    }

    if (bind_nh
        || (nh_info.merge_dsfwd == 1)
	    || (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_ASSIGN_PORT) && !nh_info.ecmp_valid && !nh_info.is_ecmp_intf))
    {
        CTC_SET_FLAG(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_unbind_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_nh_info_dsnh_t nh_info;
    sys_nh_update_dsnh_param_t update_dsnh;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));

    if (0 == sys_goldengate_nh_get_nh_valid(lchip, p_ipuc_info->nh_id))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));

    /* host route , write dsnh to ipda */
    if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH) || p_ipuc_info->binding_nh)
    {
        update_dsnh.data = NULL;
        update_dsnh.updateAd = NULL;
        CTC_ERROR_RETURN(sys_goldengate_nh_bind_dsfwd_cb(lchip, p_ipuc_info->nh_id, &update_dsnh));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_build_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt, uint32 stats_id, sys_ipuc_rpf_port_info_t* p_rpf_port)
{
    sys_ipuc_db_rpf_profile_t rpf_profile;
    sys_nh_info_dsnh_t* nh_info = NULL;
    uint32 fwd_offset;
    uint16 l3if = 0;
    uint8  rpf_mode = 0;
    int32 ret = CTC_E_NONE;
    int32 ad_spool_ref_cnt = 0;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);
    CTC_PTR_VALID_CHECK(p_rpf_port);

    nh_info = (sys_nh_info_dsnh_t*)mem_malloc(MEM_IPUC_MODULE, sizeof(sys_nh_info_dsnh_t));
    if(NULL == nh_info)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));
    sal_memset(nh_info, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&fwd_offset, 0, sizeof(fwd_offset));
    l3if = p_ipuc_info->l3if;
    rpf_mode = p_ipuc_info->rpf_mode;

    /* update nexthop info */
    CTC_ERROR_GOTO(_sys_goldengate_ipuc_bind_nexthop(lchip, p_ipuc_info), ret, error);

    CTC_ERROR_GOTO(sys_goldengate_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, nh_info), ret, error);
    if ((!CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_MERGE_KEY)) && (!nh_info->ecmp_valid))
    {
        /* call this func to alloc dsfwd */
        ret = sys_goldengate_nh_get_dsfwd_offset(lchip, p_ipuc_info->nh_id, &fwd_offset);
        if (ret)
        {
            _sys_goldengate_ipuc_unbind_nexthop(lchip, p_ipuc_info);
            goto error;
        }
    }

    /* add rpf */
    rpf_profile.key.nh_id = p_ipuc_info->nh_id;
    rpf_profile.key.route_flag = p_ipuc_info->route_flag;
    rpf_profile.key.l3if = p_ipuc_info->l3if;
    rpf_profile.key.rpf_port_num = p_rpf_port->rpf_port_num;
    sal_memcpy(&rpf_profile.key.rpf_port, &p_rpf_port->rpf_port, sizeof(rpf_profile.key.rpf_port));
    ret = _sys_goldengate_ipuc_add_rpf(lchip, &rpf_profile, p_rpf_rslt);
    if (ret)
    {
        _sys_goldengate_ipuc_unbind_nexthop(lchip, p_ipuc_info);
        goto error;
    }
    p_ipuc_info->l3if = p_rpf_rslt->id;
    p_ipuc_info->rpf_mode = p_rpf_rslt->mode;   /* this para is used for ad share pool */

    /* add hash key ad profile */
    ret = _sys_goldengate_ipuc_lpm_ad_profile_add(lchip, p_ipuc_info, &ad_spool_ref_cnt);
    if (ret)
    {
        _sys_goldengate_ipuc_unbind_nexthop(lchip, p_ipuc_info);
        _sys_goldengate_ipuc_remove_rpf_by_index(lchip, p_ipuc_info, p_rpf_rslt);
        p_ipuc_info->l3if = l3if;
        p_ipuc_info->rpf_mode = rpf_mode;
        goto error;
    }
    if (ad_spool_ref_cnt > 1 && !CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH))
    {
        _sys_goldengate_ipuc_unbind_nexthop(lchip, p_ipuc_info);
    }

error:
    if(NULL != nh_info)
    {
        mem_free(nh_info);
    }

    return ret;
}

STATIC int32
_sys_goldengate_ipuc_unbuild_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_rpf_rslt_t rpf_rslt;
    sys_ipuc_db_rpf_profile_t rpf_profile;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);

    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&rpf_profile, 0, sizeof(sys_ipuc_db_rpf_profile_t));

    /* update nexthop info */
    _sys_goldengate_ipuc_unbind_nexthop(lchip, p_ipuc_info);

    /* remove rpf */
    _sys_goldengate_ipuc_remove_rpf(lchip, p_ipuc_info);

    /* free lpm ad profile */
    _sys_goldengate_ipuc_lpm_ad_profile_remove(lchip, p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_write_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt)
{
    /* DsIpDa_m and ds_ipv4_ucast_da_tcam_t are the same*/
    uint32 route_flag = 0;
    uint32 cmd = 0;
    DsIpDa_m dsipda;
    uint32 field_value = 0;
    sys_nh_info_dsnh_t nh_info;
    uint32 fwd_offset;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_info);

    route_flag = p_ipuc_info->route_flag;

    sal_memset(&dsipda, 0, sizeof(dsipda));
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));


    field_value = !CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_TTL_NO_CHECK);
    SetDsIpDa(V, ttlCheckEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_CPU);
    SetDsIpDa(V, ipDaExceptionEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_PROTOCOL_ENTRY);
    SetDsIpDa(V, exception3CtlEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_SELF_ADDRESS);
    SetDsIpDa(V, selfAddress_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_CHECK);
    SetDsIpDa(V, icmpCheckEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_ERR_MSG_CHECK);
    SetDsIpDa(V, icmpErrMsgCheckEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_DEFAULT);
    SetDsIpDa(V, isDefaultRoute_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_RPF_CHECK);
    SetDsIpDa(V, rpfCheckEn_f, &dsipda, field_value);

    field_value = CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_CANCEL_NAT);
    SetDsIpDa(V, l3IfType_f, &dsipda, field_value);  /* 0- external,1- internal */

    field_value = p_rpf_rslt->mode;
    SetDsIpDa(V, rpfCheckMode_f, &dsipda, field_value);

    field_value = p_rpf_rslt->id;
    SetDsIpDa(V, rpfIfId_f, &dsipda, field_value);

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ipuc_info->nh_id, &nh_info));

    if (nh_info.is_ivi)
    {
        SetDsIpDa(V, u2_g2_iviEnable_f, &dsipda, 1); /* ivi enable */
        SetDsIpDa(V, ptEnable_f, &dsipda, 1);        /* need do ipv4-ipv6 address translate */
    }

    /*to CPU must be equal to 63(get the low 5bits is 31), refer to sys_goldengate_pdu.h:SYS_L3PDU_PER_L3IF_ACTION_INDEX_RSV_IPDA_TO_CPU*/
    field_value = CTC_L3PDU_ACTION_INDEX_IPDA & 0x1F;
    SetDsIpDa(V, exceptionSubIndex_f, &dsipda, field_value);

    if ((CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_MERGE_KEY) || CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_TO_FABRIC))
        && !CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_STATS_EN))
    {
        _sys_goldengate_ipuc_get_ipda_nexthop(lchip, p_ipuc_info, &nh_info, &dsipda);
    }
    else
    {
        if (nh_info.ecmp_valid)
        {
            if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_STATS_EN))
            {
                return CTC_E_INVALID_PARAM;
            }
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ecmp number is %d\r\n", nh_info.ecmp_cnt);
            field_value = nh_info.ecmp_group_id;
            SetDsIpDa(V, u1_g1_ecmpGroupId_f, &dsipda, field_value);


        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_ipuc_info->nh_id, &fwd_offset));

             field_value = fwd_offset & 0xFFFF;
              SetDsIpDa(V, u1_g1_dsFwdPtr_f, &dsipda, field_value);
              SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: p_ipuc_info->ad_offset:%d  dsipda.ds_fwd_ptr:0x%x\n",
                         p_ipuc_info->ad_offset, field_value);
              if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_STATS_EN))
              {
                uint16 stats_ptr = 0;
                CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_ipuc_info->stats_id, CTC_STATS_STATSID_TYPE_IP, &stats_ptr));

                field_value = (stats_ptr>>3)&0x3FF;
                SetDsIpDa(V, u1_g1_ecmpGroupId_f, &dsipda, field_value);
                field_value = stats_ptr&0x7;
                SetDsIpDa(V, u1_g1_ipmcStatsPtrLowBits_f, &dsipda, field_value);
                field_value = 1;
                SetDsIpDa(V, ipmcStatsEn_f, &dsipda, field_value);
              }
        }
    }

    cmd = DRV_IOW(DsIpDa_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ipuc_info->ad_offset, cmd, &dsipda);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_nat_write_ipsa(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    uint32 cmd = 0;
    ds_t dsnatsa;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&dsnatsa, 0, sizeof(dsnatsa));

    SetDsIpSaNat(V, replaceIpSa_f, dsnatsa, 1);
    SetDsIpSaNat(V, u1_g1_ipSa_f, dsnatsa, p_nat_info->new_ipv4[0]);
    if (p_nat_info->l4_src_port)
    {
        SetDsIpSaNat(V, replaceL4SourcePort_f, dsnatsa, 1);
        SetDsIpSaNat(V, l4SourcePort_f, dsnatsa, p_nat_info->new_l4_src_port);
    }

    cmd = DRV_IOW(DsIpSaNat_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_nat_info->ad_offset, cmd, &dsnatsa);


    return CTC_E_NONE;
}

#define __2_KEY__
STATIC int32
_sys_goldengate_ipuc_alloc_nat_tcam_ad_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    sys_goldengate_opf_t opf;
    uint32 ad_offset = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &ad_offset));

    p_nat_info->key_offset = ad_offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_free_nat_tcam_ad_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    sys_goldengate_opf_t opf;
    uint32 ad_offset = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;

    ad_offset = p_nat_info->key_offset;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, ad_offset));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_build_tunnel_key(uint8 lchip, sys_scl_entry_t* scl_entry, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param)
{
    if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
    {
         scl_entry->key.tunnel_type = CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver ?
                                        SYS_SCL_TUNNEL_TYPE_GRE_IN4 : SYS_SCL_TUNNEL_TYPE_GRE_IN6;
    }
    else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
    {
         scl_entry->key.tunnel_type = CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver ?
                                        SYS_SCL_TUNNEL_TYPE_IPV4_IN4 : SYS_SCL_TUNNEL_TYPE_IPV4_IN6;
    }
    else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
    {
         scl_entry->key.tunnel_type = CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver ?
                                        SYS_SCL_TUNNEL_TYPE_IPV6_IN4 : SYS_SCL_TUNNEL_TYPE_IPV6_IN6;
    }

    if (CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver)
    {
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_USE_FLEX))
        {
            scl_entry->key.type                        = SYS_SCL_KEY_TCAM_IPV4_SINGLE;

            scl_entry->key.u.tcam_ipv4_key.l3_type = CTC_PARSER_L3_TYPE_IPV4;
            scl_entry->key.u.tcam_ipv4_key.l3_type_mask = 0xFF;
            scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE;

            scl_entry->key.u.tcam_ipv4_key.ip_da      = p_ipuc_tunnel_param->ip_da.ipv4;
            scl_entry->key.u.tcam_ipv4_key.ip_da_mask = 0xFFFFFFFF;
            scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA;

            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
            {
                scl_entry->key.u.tcam_ipv4_key.ip_sa     = p_ipuc_tunnel_param->ip_sa.ipv4;
                scl_entry->key.u.tcam_ipv4_key.ip_sa_mask = 0xFFFFFFFF;
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA;
            }

            /* for SYS_SCL_KEY_TCAM_IPV4_SINGLE, must use l4_protocol, not support l4_type */
            if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
            {
                 scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_GRE;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
            {
                 scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_IPV4;
            }
            else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
            {
                 scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_IPV6;
            }
            scl_entry->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
            scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL;

            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
            {
                scl_entry->key.u.tcam_ipv4_key.sub_flag |= CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY;
                scl_entry->key.u.tcam_ipv4_key.gre_key = p_ipuc_tunnel_param->gre_key;
                scl_entry->key.u.tcam_ipv4_key.gre_key_mask = 0xFFFFFFFF;
            }

            /* won't decap fragment packet */
            scl_entry->key.u.tcam_ipv4_key.ip_frag = CTC_IP_FRAG_NON;
            scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG;
        }
        else if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
        {
            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
            {
                scl_entry->key.type                        = SYS_SCL_KEY_TCAM_IPV4_SINGLE;

                scl_entry->key.u.tcam_ipv4_key.l3_type = CTC_PARSER_L3_TYPE_IPV4;
                scl_entry->key.u.tcam_ipv4_key.l3_type_mask = 0xFF;
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE;

                scl_entry->key.u.tcam_ipv4_key.ip_da      = p_ipuc_tunnel_param->ip_da.ipv4;
                scl_entry->key.u.tcam_ipv4_key.ip_da_mask = 0xFFFFFFFF;
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA;

                if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
                {
                     scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_GRE;
                }
                else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
                {
                     scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_IPV4;
                }
                else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
                {
                     scl_entry->key.u.tcam_ipv4_key.l4_protocol     = SYS_L4_PROTOCOL_IPV6;
                }
                scl_entry->key.u.tcam_ipv4_key.l4_protocol_mask = 0xFF;
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL;

                if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
                {
                    scl_entry->key.u.tcam_ipv4_key.sub_flag |= CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY;
                    scl_entry->key.u.tcam_ipv4_key.gre_key = p_ipuc_tunnel_param->gre_key;
                    scl_entry->key.u.tcam_ipv4_key.gre_key_mask = 0xFFFFFFFF;
                }

                /* won't decap fragment packet */
                scl_entry->key.u.tcam_ipv4_key.ip_frag = CTC_IP_FRAG_NON;
                scl_entry->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG;
            }
            else
            {
                scl_entry->key.type                        = SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA;
                scl_entry->key.u.hash_tunnel_ipv4_key.ip_da = p_ipuc_tunnel_param->ip_da.ipv4;
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
        else if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            scl_entry->key.type            = SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.ip_da = p_ipuc_tunnel_param->ip_da.ipv4;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.gre_key = p_ipuc_tunnel_param->gre_key;
            scl_entry->key.u.hash_tunnel_ipv4_gre_key.l4_type = CTC_PARSER_L4_TYPE_GRE;
        }
        else if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
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
        scl_entry->key.type                  = SYS_SCL_KEY_TCAM_IPV6;

        sal_memcpy(scl_entry->key.u.tcam_ipv6_key.ip_da, p_ipuc_tunnel_param->ip_da.ipv6,
                    p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
        sal_memset(scl_entry->key.u.tcam_ipv6_key.ip_da_mask, 0xFF,
                    p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
        scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA;

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_GRE_WITH_KEY))
        {
            scl_entry->key.u.tcam_ipv6_key.sub_flag |= CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY;
            scl_entry->key.u.tcam_ipv6_key.gre_key  = p_ipuc_tunnel_param->gre_key;
            scl_entry->key.u.tcam_ipv6_key.gre_key_mask = 0xFFFFFFFF;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
        {
            scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA;
            sal_memcpy(scl_entry->key.u.tcam_ipv6_key.ip_sa, p_ipuc_tunnel_param->ip_sa.ipv6,
                        p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
            sal_memset(scl_entry->key.u.tcam_ipv6_key.ip_sa_mask, 0xFF,
                        p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
        }

        if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)
        {
            scl_entry->key.u.tcam_ipv6_key.l4_protocol= SYS_L4_PROTOCOL_GRE;
        }
        else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4)
        {
            scl_entry->key.u.tcam_ipv6_key.l4_protocol     = SYS_L4_PROTOCOL_IPV4;
        }
        else if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V6)
        {
            scl_entry->key.u.tcam_ipv6_key.l4_protocol     = SYS_L4_PROTOCOL_IPV6;
        }
        scl_entry->key.u.tcam_ipv6_key.l4_protocol_mask = 0xFF;
        scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL;

        /* won't decap fragment packet */
        scl_entry->key.u.tcam_ipv6_key.ip_frag = CTC_IP_FRAG_NON;
        scl_entry->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_FRAG;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_build_tunnel_rpf_key(uint8 lchip, sys_scl_entry_t* scl_entry_rpf, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param)
{
    if (CTC_IP_VER_4 == p_ipuc_tunnel_param->ip_ver)
    {
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_USE_FLEX))
        {
            scl_entry_rpf->key.type   = SYS_SCL_KEY_TCAM_IPV4_SINGLE;

            scl_entry_rpf->key.u.tcam_ipv4_key.ip_sa     = p_ipuc_tunnel_param->ip_sa.ipv4;
            scl_entry_rpf->key.u.tcam_ipv4_key.ip_sa_mask = 0xFFFFFFFF;
            scl_entry_rpf->key.u.tcam_ipv4_key.flag |= CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA;
        }
        else
        {
            scl_entry_rpf->key.type   = SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF;
            scl_entry_rpf->key.u.hash_tunnel_ipv4_rpf_key.ip_sa = p_ipuc_tunnel_param->ip_sa.ipv4;
        }
    }
    else
    {
        scl_entry_rpf->key.type                  = SYS_SCL_KEY_TCAM_IPV6;

        scl_entry_rpf->key.u.tcam_ipv6_key.flag |= CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA;
        sal_memcpy(scl_entry_rpf->key.u.tcam_ipv6_key.ip_sa, p_ipuc_tunnel_param->ip_sa.ipv6,
                    p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
        sal_memset(scl_entry_rpf->key.u.tcam_ipv6_key.ip_sa_mask, 0xFF,
                    p_gg_ipuc_master[lchip]->addr_len[p_ipuc_tunnel_param->ip_ver]);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_traverse_pre(void* entry, void* p_trav)
{
    uint8   lchip;
    ctc_ipuc_param_t ipuc_param;
    ctc_ipuc_param_t* p_ipuc_param = &ipuc_param;
    sys_ipuc_info_t* p_ipuc_info = entry;
    hash_traversal_fn fn = ((sys_ipuc_traverse_t*)p_trav)->fn;
    void* data = ((sys_ipuc_traverse_t*)p_trav)->data;

    sal_memset(p_ipuc_param, 0, sizeof(ctc_ipuc_param_t));

    lchip = ((sys_ipuc_traverse_t*)p_trav)->lchip;

    p_ipuc_param->nh_id = p_ipuc_info->nh_id;
    p_ipuc_param->vrf_id = p_ipuc_info->vrf_id;
    p_ipuc_param->route_flag = (p_ipuc_info->route_flag & MAX_CTC_IPUC_FLAG);
    p_ipuc_param->masklen = p_ipuc_info->masklen;
    p_ipuc_param->l4_dst_port = p_ipuc_info->l4_dst_port;
    p_ipuc_param->is_tcp_port = CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_TCP_PORT)
        ? 1 : 0;
    p_ipuc_param->ip_ver = CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_IPV6)
        ? CTC_IP_VER_6 : CTC_IP_VER_4;
    sal_memcpy(&(p_ipuc_param->ip), &(p_ipuc_info->ip), p_gg_ipuc_master[lchip]->addr_len[p_ipuc_param->ip_ver]);
    return (* fn)(p_ipuc_param, data);
}

uint32
_sys_goldengate_ipuc_operation_check(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8* opt)
{
    if (((p_ipuc_info->masklen == 32) || (p_ipuc_info->masklen == 128))
        && ((CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
        && (p_gg_ipuc_master[lchip]->lookup_mode[SYS_IPUC_VER(p_ipuc_info)] & SYS_IPUC_HASH_LOOKUP))
        || (p_ipuc_info->l4_dst_port > 0)))
    {
        *opt = DO_HASH;
    }
    else if (((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && ((p_ipuc_info->masklen < p_gg_ipuc_master[lchip]->masklen_ipv6_l) || (p_ipuc_info->masklen > (p_gg_ipuc_master[lchip]->masklen_ipv6_l+16))))
        || ((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen < 16) && p_gg_ipuc_master[lchip]->use_hash16)
        || ((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && ((p_ipuc_info->masklen < 8) || (p_ipuc_info->masklen > 24)) && (!p_gg_ipuc_master[lchip]->use_hash16)))
    {
        *opt = DO_TCAM;
    }
    else if (p_gg_ipuc_master[lchip]->lookup_mode[SYS_IPUC_VER(p_ipuc_info)] & SYS_IPUC_LPM_LOOKUP)
    {
        *opt = DO_LPM;
    }
    else
    {
        *opt = DO_TCAM;
    }

    p_ipuc_info->route_opt = *opt;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_add_host_route(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 is_update)
{
    uint8 l4PortType = 0;
    ipv6_addr_t ipv6_data;
    drv_fib_acc_in_t fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    drv_cpu_acc_in_t cpu_acc_in;
    drv_cpu_acc_out_t cpu_acc_out;
    hash_io_by_key_para_t hash_para;
    DsFibHost0Ipv4HashKey_m ipv4hashkey;
    DsFibHost0Ipv6UcastHashKey_m ipv6hashkey;
    DsFibHost1Ipv4NatDaPortHashKey_m ipv4_port_hashkey;

    sal_memset(&ipv6_data, 0, sizeof(ipv6_data));
    sal_memset(&fib_acc_in, 0, sizeof(fib_acc_in));
    sal_memset(&fib_acc_out, 0, sizeof(fib_acc_out));
    sal_memset(&cpu_acc_in, 0, sizeof(cpu_acc_in));
    sal_memset(&cpu_acc_out, 0, sizeof(cpu_acc_out));
    sal_memset(&hash_para, 0, sizeof(hash_para));
    sal_memset(&ipv4hashkey, 0, sizeof(ipv4hashkey));
    sal_memset(&ipv6hashkey, 0, sizeof(ipv6hashkey));
    sal_memset(&ipv4_port_hashkey, 0, sizeof(ipv4_port_hashkey));

    if (p_ipuc_info->l4_dst_port == 0)   /* host hash */
    {
        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
        {
            SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ipv4hashkey, p_ipuc_info->ip.ipv4[0]);
            SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ipv4hashkey, p_ipuc_info->vrf_id);
            SetDsFibHost0Ipv4HashKey(V, valid_f, &ipv4hashkey, 1);
            SetDsFibHost0Ipv4HashKey(V, dsAdIndex_f, &ipv4hashkey, p_ipuc_info->ad_offset);
            SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ipv4hashkey, 0);
            SetDsFibHost0Ipv4HashKey(V, hashType_f, &ipv4hashkey, FIBHOST0PRIMARYHASHTYPE_IPV4);

            fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
            fib_acc_in.fib.overwrite_en = 1;      /* always set to 1 for update function */
            fib_acc_in.fib.data = (void*)&ipv4hashkey;
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &fib_acc_in, &fib_acc_out));

            if (fib_acc_out.fib.conflict)
            {
                return CTC_E_IPUC_HASH_CONFLICT;
            }
            else
            {
                p_ipuc_info->key_offset = fib_acc_out.fib.key_index;
            }
        }
        else
        {
            /* DRV_SET_FIELD_A, ipv6_data must use little india */
            sal_memcpy(&ipv6_data, &(p_ipuc_info->ip.ipv6), sizeof(ipv6_addr_t));
            SetDsFibHost0Ipv6UcastHashKey(A, ipDa_f, &ipv6hashkey, ipv6_data);
            SetDsFibHost0Ipv6UcastHashKey(V, vrfId_f, &ipv6hashkey, p_ipuc_info->vrf_id);
            SetDsFibHost0Ipv6UcastHashKey(V, valid0_f, &ipv6hashkey, 1);
            SetDsFibHost0Ipv6UcastHashKey(V, valid1_f, &ipv6hashkey, 1);
            SetDsFibHost0Ipv6UcastHashKey(V, dsAdIndex_f, &ipv6hashkey, p_ipuc_info->ad_offset);
            SetDsFibHost0Ipv6UcastHashKey(V, hashType0_f, &ipv6hashkey, FIBHOST0PRIMARYHASHTYPE_IPV6UCAST);
            SetDsFibHost0Ipv6UcastHashKey(V, hashType1_f, &ipv6hashkey, FIBHOST0PRIMARYHASHTYPE_IPV6UCAST);

            fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6UCAST;
            fib_acc_in.fib.overwrite_en = 1;      /* always set to 1 for update function */
            fib_acc_in.fib.data = (void*)&ipv6hashkey;
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &fib_acc_in, &fib_acc_out));

            if (fib_acc_out.fib.conflict)
            {
                return CTC_E_IPUC_HASH_CONFLICT;
            }
            else
            {
                p_ipuc_info->key_offset = fib_acc_out.fib.key_index;
            }
        }
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "add host hash key, key_index:0x%x\n", fib_acc_out.fib.key_index);

#if 0
        if (p_ipuc_info->route_flag & CTC_IPUC_FLAG_AGING_EN) /**CTC_E_FEATURE_NOT_SUPPORT*/
        {
            CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status(lchip, SYS_AGING_DOMAIN_MAC_HASH,
                                                                   fib_acc_out.fib.key_index, SYS_AGING_TIMER_INDEX_SERVICE));
        }
#endif

    }
    else    /* port hash for NAPT*/
    {
        l4PortType = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 3 : 2;
        SetDsFibHost1Ipv4NatDaPortHashKey(V, ipDa_f, &ipv4_port_hashkey, p_ipuc_info->ip.ipv4[0]);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, l4DestPort_f, &ipv4_port_hashkey, p_ipuc_info->l4_dst_port);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, l4PortType_f, &ipv4_port_hashkey, l4PortType);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, vrfId_f, &ipv4_port_hashkey, p_ipuc_info->vrf_id);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, valid_f, &ipv4_port_hashkey, 1);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, dsAdIndex_f, &ipv4_port_hashkey, p_ipuc_info->ad_offset);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, ipv4Type_f, &ipv4_port_hashkey, 0);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, hashType_f, &ipv4_port_hashkey, FIBHOST1PRIMARYHASHTYPE_IPV4);

        cpu_acc_in.tbl_id = DsFibHost1Ipv4NatDaPortHashKey_t;
        cpu_acc_in.data = (void*)(&ipv4_port_hashkey);
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FIB_HOST1, &cpu_acc_in, &cpu_acc_out));
        if (cpu_acc_out.conflict == TRUE)
        {
            return CTC_E_IPUC_HASH_CONFLICT;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_remove_host_route(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 l4PortType = 0;
    ipv6_addr_t ipv6_data;
    hash_io_by_key_para_t hash_para;
    DsFibHost0Ipv4HashKey_m ipv4hashkey;
    DsFibHost0Ipv6UcastHashKey_m ipv6hashkey;
    DsFibHost1Ipv4NatDaPortHashKey_m ipv4_port_hashkey;
    drv_fib_acc_in_t fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    drv_cpu_acc_in_t cpu_acc_in;
    drv_cpu_acc_out_t cpu_acc_out;

    sal_memset(&ipv6_data, 0, sizeof(ipv6_data));
    sal_memset(&hash_para, 0, sizeof(hash_para));
    sal_memset(&ipv4hashkey, 0, sizeof(ipv4hashkey));
    sal_memset(&ipv6hashkey, 0, sizeof(ipv6hashkey));
    sal_memset(&ipv4_port_hashkey, 0, sizeof(ipv4_port_hashkey));
    sal_memset(&fib_acc_in, 0, sizeof(fib_acc_in));
    sal_memset(&fib_acc_out, 0, sizeof(fib_acc_out));
    sal_memset(&cpu_acc_in, 0, sizeof(cpu_acc_in));
    sal_memset(&cpu_acc_out, 0, sizeof(cpu_acc_out));

    if (p_ipuc_info->l4_dst_port == 0)   /* host hash */
    {
        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
        {
            SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ipv4hashkey, p_ipuc_info->ip.ipv4[0]);
            SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ipv4hashkey, p_ipuc_info->vrf_id);
            SetDsFibHost0Ipv4HashKey(V, valid_f, &ipv4hashkey, 0);
            SetDsFibHost0Ipv4HashKey(V, dsAdIndex_f, &ipv4hashkey, p_ipuc_info->ad_offset);
            SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ipv4hashkey, 0);
            SetDsFibHost0Ipv4HashKey(V, hashType_f, &ipv4hashkey, FIBHOST0PRIMARYHASHTYPE_IPV4);

            fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
            fib_acc_in.fib.overwrite_en = 1;
            fib_acc_in.fib.data = (void*)&ipv4hashkey;
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &fib_acc_in, &fib_acc_out));
        }
        else
        {
            /* DRV_SET_FIELD_A, ipv6_data must use little india */
            sal_memcpy(&ipv6_data, &(p_ipuc_info->ip.ipv6), sizeof(ipv6_addr_t));
            SetDsFibHost0Ipv6UcastHashKey(A, ipDa_f, &ipv6hashkey, ipv6_data);
            SetDsFibHost0Ipv6UcastHashKey(V, vrfId_f, &ipv6hashkey, p_ipuc_info->vrf_id);
            SetDsFibHost0Ipv6UcastHashKey(V, valid0_f, &ipv6hashkey, 0);
            SetDsFibHost0Ipv6UcastHashKey(V, valid1_f, &ipv6hashkey, 0);
            SetDsFibHost0Ipv6UcastHashKey(V, dsAdIndex_f, &ipv6hashkey, p_ipuc_info->ad_offset);
            SetDsFibHost0Ipv6UcastHashKey(V, hashType0_f, &ipv6hashkey, FIBHOST0PRIMARYHASHTYPE_IPV6UCAST);
            SetDsFibHost0Ipv6UcastHashKey(V, hashType1_f, &ipv6hashkey, FIBHOST0PRIMARYHASHTYPE_IPV6UCAST);

            fib_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6UCAST;
            fib_acc_in.fib.overwrite_en = 1;
            fib_acc_in.fib.data = (void*)&ipv6hashkey;
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_FIB0_BY_KEY, &fib_acc_in, &fib_acc_out));
        }

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free host hash key, key_index:0x%x\n", fib_acc_out.fib.key_index);

#if 0
        if (p_ipuc_info->route_flag & CTC_IPUC_FLAG_AGING_EN)/**CTC_E_FEATURE_NOT_SUPPORT*/
        {
            CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status(lchip, SYS_AGING_DOMAIN_MAC_HASH,
                                                                   fib_acc_out.fib.key_index, 0));
        }
#endif

    }
    else    /* port hash for NAPT*/
    {
        l4PortType = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 3 : 2;
        SetDsFibHost1Ipv4NatDaPortHashKey(V, ipDa_f, &ipv4_port_hashkey, p_ipuc_info->ip.ipv4[0]);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, l4DestPort_f, &ipv4_port_hashkey, p_ipuc_info->l4_dst_port);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, l4PortType_f, &ipv4_port_hashkey, l4PortType);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, vrfId_f, &ipv4_port_hashkey, p_ipuc_info->vrf_id);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, valid_f, &ipv4_port_hashkey, 1);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, dsAdIndex_f, &ipv4_port_hashkey, p_ipuc_info->ad_offset);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, ipv4Type_f, &ipv4_port_hashkey, 0);
        SetDsFibHost1Ipv4NatDaPortHashKey(V, hashType_f, &ipv4_port_hashkey, FIBHOST1PRIMARYHASHTYPE_IPV4);

        cpu_acc_in.tbl_id = DsFibHost1Ipv4NatDaPortHashKey_t;
        cpu_acc_in.data = (void*)(&ipv4_port_hashkey);
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FIB_HOST1, &cpu_acc_in, &cpu_acc_out));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipv4_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    ds_t tcam40key, tcam40mask;
    ds_t lpmtcam_ad;
    tbl_entry_t tbl_ipkey;
    uint32 ip_mask = 0;
    uint32 cmd = 0;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tcam40key, 0, sizeof(tcam40key));
    sal_memset(&tcam40mask, 0, sizeof(tcam40mask));
    sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));

    /* write ad */
    SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, p_ipuc_info->ad_offset);

    cmd = DRV_IOW(p_gg_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_4], DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ipuc_info->key_offset, cmd, &lpmtcam_ad);

    /* write key */
    tbl_ipkey.data_entry = (uint32*)&tcam40key;
    tbl_ipkey.mask_entry = (uint32*)&tcam40mask;

    SetDsLpmTcamIpv440Key(V, ipAddr_f, tcam40key, p_ipuc_info->ip.ipv4[0]);
    SetDsLpmTcamIpv440Key(V, vrfId_f, tcam40key, p_ipuc_info->vrf_id);
    SetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, tcam40key, 0);

    IPV4_LEN_TO_MASK(ip_mask, p_ipuc_info->masklen);
    SetDsLpmTcamIpv440Key(V, ipAddr_f, tcam40mask, ip_mask);
    SetDsLpmTcamIpv440Key(V, vrfId_f, tcam40mask, 0x7FF);
    SetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, tcam40mask, 0x1);

    cmd = DRV_IOW(DsLpmTcamIpv440Key_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ipuc_info->key_offset, cmd, &tbl_ipkey);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipv6_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    ds_t tcam160key, tcam160mask;
    ds_t lpmtcam_ad;
    tbl_entry_t tbl_ipkey;
    ipv6_addr_t ipv6_data, ipv6_mask;
    uint32 cmd = 0;
    uint32 key_offset = 0;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tcam160key, 0, sizeof(tcam160key));
    sal_memset(&tcam160mask, 0, sizeof(tcam160mask));
    sal_memset(&ipv6_data, 0, sizeof(ipv6_data));
    sal_memset(&ipv6_mask, 0, sizeof(ipv6_mask));
    sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));

    tbl_ipkey.data_entry = (uint32*)&tcam160key;
    tbl_ipkey.mask_entry = (uint32*)&tcam160mask;

    key_offset = p_gg_ipuc_db_master[lchip]->tcam_share_mode?
        (p_ipuc_info->key_offset / SYS_IPUC_TCAM_6TO4_STEP)  : p_ipuc_info->key_offset;

    /* write ad */
    SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, p_ipuc_info->ad_offset);

    cmd = DRV_IOW(p_gg_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_6], DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_ipuc_info->key_offset, cmd, &lpmtcam_ad);

    /* write key */
    /* DRV_SET_FIELD_A, ipv6_data must use little india */
    sal_memcpy(&ipv6_data, &(p_ipuc_info->ip.ipv6), sizeof(ipv6_addr_t));
    SetDsLpmTcamIpv6160Key0(A, ipAddr_f, tcam160key, ipv6_data);
    SetDsLpmTcamIpv6160Key0(V, vrfId_f, tcam160key, p_ipuc_info->vrf_id);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, tcam160key, 1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType1_f, tcam160key, 1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType2_f, tcam160key, 1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType3_f, tcam160key, 1);

    IPV6_LEN_TO_MASK(ipv6_mask, p_ipuc_info->masklen);
    SetDsLpmTcamIpv6160Key0(A, ipAddr_f, tcam160mask, ipv6_mask);
    SetDsLpmTcamIpv6160Key0(V, vrfId_f, tcam160mask, 0x7FF);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, tcam160mask, 0x1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType1_f, tcam160mask, 0x1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType2_f, tcam160mask, 0x1);
    SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType3_f, tcam160mask, 0x1);

    cmd = DRV_IOW(DsLpmTcamIpv6160Key0_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, key_offset, cmd, &tbl_ipkey);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        _sys_goldengate_ipv4_write_key(lchip, p_ipuc_info);
    }
    else
    {
        _sys_goldengate_ipv6_write_key(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;

    SYS_IPUC_DBG_FUNC();

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: remove tcam p_ipuc_info->key_offset:%d \n", p_ipuc_info->key_offset);

    if (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4)
    {
        DRV_TCAM_TBL_REMOVE(lchip, p_gg_ipuc_master[lchip]->tcam_key_table[SYS_IPUC_VER(p_ipuc_info)],
                        p_ipuc_info->key_offset);
    }
    else
    {
        if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
        {
            DRV_TCAM_TBL_REMOVE(lchip, p_gg_ipuc_master[lchip]->tcam_key_table[SYS_IPUC_VER(p_ipuc_info)],
                                p_ipuc_info->key_offset/SYS_IPUC_TCAM_6TO4_STEP);
        }
        else
        {
            DRV_TCAM_TBL_REMOVE(lchip, p_gg_ipuc_master[lchip]->tcam_key_table[SYS_IPUC_VER(p_ipuc_info)],
                                p_ipuc_info->key_offset);
        }
    }

    return ret;
}

int32
_sys_goldengate_ipuc_write_nat_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    ds_t v4_key, v4_mask;
    ds_t dsad;
    tbl_entry_t tbl_ipkey;
    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&v4_key, 0, sizeof(v4_key));
    sal_memset(&v4_mask, 0, sizeof(v4_mask));
    sal_memset(&dsad, 0, sizeof(dsad));

    /* write ad */
    SetDsLpmTcamIpv4Nat160Ad(V, nexthop_f, dsad, p_nat_info->ad_offset);
    cmd = DRV_IOW(DsLpmTcamIpv4Nat160Ad_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_nat_info->key_offset, cmd, &dsad);

    /* write key */
    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, v4_key, 2);
    SetDsLpmTcamIpv4160Key(V, ipSa_f, v4_key, p_nat_info->ipv4[0]);
    SetDsLpmTcamIpv4160Key(V, vrfId_f, v4_key, p_nat_info->vrf_id);

    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, v4_mask, 0x3);      /*tmp mask for spec is not sync */
    SetDsLpmTcamIpv4160Key(V, ipSa_f, v4_mask, 0xFFFFFFFF);
    SetDsLpmTcamIpv4160Key(V, vrfId_f, v4_mask, 0x7FF);

    if (p_nat_info->l4_src_port)
    {
        SetDsLpmTcamIpv4160Key(V, l4SourcePort_f, v4_key, p_nat_info->l4_src_port);
        SetDsLpmTcamIpv4160Key(V, isTcp_f, v4_key, p_nat_info->is_tcp_port);
        SetDsLpmTcamIpv4160Key(V, isUdp_f, v4_key, !p_nat_info->is_tcp_port);
        SetDsLpmTcamIpv4160Key(V, fragInfo_f, v4_key, 0);

        SetDsLpmTcamIpv4160Key(V, l4SourcePort_f, v4_mask, 0xFFFF);
        SetDsLpmTcamIpv4160Key(V, isTcp_f, v4_mask, 0x1);
        SetDsLpmTcamIpv4160Key(V, isUdp_f, v4_mask, 0x1);
        SetDsLpmTcamIpv4160Key(V, fragInfo_f, v4_mask, 0x3);
    }

    tbl_ipkey.data_entry = (uint32*)&v4_key;
    tbl_ipkey.mask_entry = (uint32*)&v4_mask;

    cmd = DRV_IOW(DsLpmTcamIpv4NAT160Key_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, p_nat_info->key_offset, cmd, &tbl_ipkey);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_remove_nat_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    int32 ret = CTC_E_NONE;

    SYS_IPUC_DBG_FUNC();

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove IPUC NAT: p_nat_info->key_offset:%d \n", p_nat_info->key_offset);

    DRV_TCAM_TBL_REMOVE(lchip, DsLpmTcamIpv4NAT160Key_t, p_nat_info->key_offset);

    return ret;
}

int32
_sys_goldengate_ipuc_alloc_hash_lpm_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_wb_lpm_info_t* lpm_info)
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
    CTC_ERROR_RETURN(_sys_goldengate_l3_hash_alloc_key_index(lchip, p_ipuc_info));

    /* build lpm soft db and build hard index */
    if (!(((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))))
    {
        ret = _sys_goldengate_lpm_add(lchip, p_ipuc_info, lpm_info);
        if (ret < 0)
        {
            _sys_goldengate_l3_hash_free_key_index(lchip, p_ipuc_info);
            return ret;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_build_hash_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info, DsFibHost1Ipv4NatSaPortHashKey_m* port_hash)
{
    SYS_IPUC_DBG_FUNC();

    SetDsFibHost1Ipv4NatSaPortHashKey(V, hashType_f, port_hash, FIBHOST1PRIMARYHASHTYPE_IPV4);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, l4PortType_f, port_hash, (p_nat_info->is_tcp_port ? 3 : 2));
    SetDsFibHost1Ipv4NatSaPortHashKey(V, dsAdIndex_f, port_hash, p_nat_info->ad_offset);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, vrfId_f, port_hash, p_nat_info->vrf_id);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, l4SourcePort_f, port_hash, p_nat_info->l4_src_port);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, ipSa_f, port_hash, p_nat_info->ipv4[0]);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, ipv4Type_f, port_hash, 3);
    SetDsFibHost1Ipv4NatSaPortHashKey(V, valid_f, port_hash, 1);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_alloc_nat_hash_key_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;
    DsFibHost1Ipv4NatSaPortHashKey_m port_hash;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    sal_memset(&port_hash, 0, sizeof(port_hash));

    _sys_goldengate_ipuc_build_hash_nat_sa_key(lchip, p_nat_info, &port_hash);

    in.tbl_id = DsFibHost1Ipv4NatSaPortHashKey_t;
    in.data   = &port_hash;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &in, &out));

    p_nat_info->in_tcam = out.conflict;
    if (!out.conflict)
    {
        if (out.key_index < 32)
        {
            p_nat_info->in_tcam = 1;
        }
        else
        {
            p_nat_info->key_offset = out.key_index;
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC NAT HASH: p_nat_info->key_offset:%d \n", p_nat_info->key_offset);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_add_hash_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;
    DsFibHost1Ipv4NatSaPortHashKey_m port_hash;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    sal_memset(&port_hash, 0, sizeof(port_hash));

    _sys_goldengate_ipuc_build_hash_nat_sa_key(lchip, p_nat_info, &port_hash);

    in.tbl_id = DsFibHost1Ipv4NatSaPortHashKey_t;
    in.key_index = p_nat_info->key_offset;
    in.data   = &port_hash;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_FIB_HOST1_BY_IDX, &in, &out));

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_remove_hash_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;
    DsFibHost1Ipv4NatSaPortHashKey_m port_hash;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    sal_memset(&port_hash, 0, sizeof(port_hash));

    _sys_goldengate_ipuc_build_hash_nat_sa_key(lchip, p_nat_info, &port_hash);

    in.tbl_id = DsFibHost1Ipv4NatSaPortHashKey_t;
    in.key_index = p_nat_info->key_offset;
    in.data   = &port_hash;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_FIB_HOST1_BY_IDX, &in, &out));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_update_lpm_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (p_ipuc_info->l4_dst_port > 0)
    {
        return CTC_E_NONE;
    }

    if (!((((p_ipuc_info->masklen == 32) || (p_ipuc_info->masklen == 40) || (p_ipuc_info->masklen == 48))
        && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))))
    {
        _sys_goldengate_lpm_del(lchip, p_ipuc_info);
        _sys_goldengate_lpm_add(lchip, p_ipuc_info, NULL);
    }

    return CTC_E_NONE;
}

/* only update lpm ad_index */
STATIC int32
_sys_goldengate_ipuc_update_lpm_key_index_O(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_lpm_tbl_t* p_table = NULL;  /* root node */
    uint8  masklen = p_ipuc_info->masklen;
    uint8  stage = 0;
    uint16 index = 0;
    uint32* ip = NULL;
    uint8 tcam_masklen = 0;
    sys_lpm_item_t* p_item = NULL;
    sys_lpm_item_t* p_item0 = NULL;
    sys_lpm_item_t* p_item1 = NULL;
    uint8 offset = 0;
    uint16 i = 0;
    uint16 j = 0;
    uint16 idx = 0;
    uint16 idx0 = 0;
    uint16 idx1 = 0;
    uint8  type = LPM_ITEM_TYPE_NONE;

    SYS_IPUC_DBG_FUNC();

    if (p_ipuc_info->l4_dst_port > 0)
    {
        return CTC_E_NONE;
    }

    if (!((((p_ipuc_info->masklen == 32) || (p_ipuc_info->masklen == 40) || (p_ipuc_info->masklen == 48))
        && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))))
    {
        /* 1. get sw root node by hash, and get node index */
        if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
        {
            masklen -= p_gg_ipuc_master[lchip]->masklen_ipv6_l;
            ip = p_ipuc_info->ip.ipv6;
            stage = (masklen>8)?1:0;

            /* get sw root node by hash */
            TABEL_GET_STAGE(stage, p_ipuc_info, p_table);

            tcam_masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
            index = SYS_IPUC_OCTO(ip, (SYS_IP_OCTO_88_95 - (tcam_masklen-32)/8 - stage));
        }
        else
        {
            masklen -= p_gg_ipuc_master[lchip]->masklen_l;
            ip =  p_ipuc_info->ip.ipv4;
            stage = (masklen>8)?1:0;

            /* get sw root node by hash */
            TABEL_GET_STAGE(stage, p_ipuc_info, p_table);

            if (p_gg_ipuc_master[lchip]->use_hash16)
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
                }
            }
            else
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
            }
        }

        /* 2. update ad_index */
        masklen = (masklen>8)?(masklen-8):masklen;
        offset = (index & GET_MASK(masklen)) >> (LPM_MASK_LEN - masklen);
        idx  = GET_BASE(masklen) + offset;              /* the idx is the location of the route */
        idx0 = GET_BASE(masklen) + (offset & 0xfffe);   /* left node, every father node have two child node */
        idx1 = idx0 + 1;                                /* right node */

        if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
        {
            type = LPM_ITEM_TYPE_V6;
        }
        else if (p_ipuc_info->masklen <= p_gg_ipuc_master[lchip]->masklen_l)
        {
            type = LPM_ITEM_TYPE_V4;
        }
        ITEM_GET_OR_INIT_PTR((p_table->p_item), idx, p_item, type);
        ITEM_GET_OR_INIT_PTR((p_table->p_item), idx0, p_item0, type);
        ITEM_GET_OR_INIT_PTR((p_table->p_item), idx1, p_item1, type);

        p_item->ad_index = p_ipuc_info->ad_offset;

        /* 3. push down, fill all sub node */
        for (i = masklen + 1; i <= LPM_MASK_LEN; i++)
        {
            offset = (index & GET_MASK(i)) >> (LPM_MASK_LEN - i);

            for (j = 0; j <= GET_MASK_MAX(i - masklen); j++)
            {
                idx = GET_BASE(i) + j + offset;
                ITEM_GET_OR_INIT_PTR((p_table->p_item), idx, p_item, type);

                if (p_item->t_masklen == masklen)
                {
                    p_item->ad_index = p_ipuc_info->ad_offset;
                }
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_update(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_rpf_rslt_t rpf_rslt;
    uint32 update_key = FALSE;
    int32 ret = CTC_E_NONE;
    uint32 old_ad_offset = 0xFFFFFFFF;
    sys_ipuc_info_t ipuc_info_old;
    sys_ipuc_rpf_port_info_t rpf_port;
    uint8 is_napt = 0;
    sys_ipuc_info_t* p_ipuc_info_temp = NULL;
    sys_ipuc_info_t ipuc_info_temp = {0};
    /* if update_lpm_ad_index==1, use new process logic; if update_lpm_ad_index==0, use old process logic */
    int32 update_lpm_ad_index = 1;
    uint8 old_opt = 0;
    uint8 new_opt = 0;
    uint8 old_conflict_flag = 0;
    uint32 old_key_offset = 0;
    uint32 new_key_offset = 0;
    uint32 old_route_flag = 0;
    uint8 step[2] = {1, 2};

    SYS_IPUC_DBG_FUNC();

    sal_memset(&rpf_rslt, 0, sizeof(rpf_rslt));
    sal_memset(&rpf_port, 0, sizeof(rpf_port));
    /* save old ipuc info */
    sal_memcpy(&ipuc_info_old, p_ipuc_info, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);

    is_napt = p_ipuc_param->l4_dst_port ? 1 : 0;

    /* save old ad_offset */
    old_ad_offset = p_ipuc_info->ad_offset;
    old_key_offset = p_ipuc_info->key_offset;

    old_opt = p_ipuc_info->route_opt;
    old_conflict_flag = p_ipuc_info->conflict_flag;

    old_route_flag = p_ipuc_info->route_flag;
    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
        != CTC_FLAG_ISSET(old_route_flag, CTC_IPUC_FLAG_NEIGHBOR))
    {
        if (p_ipuc_param->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver] && !is_napt
            && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            if (p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_6] * 2
                + step[p_ipuc_param->ip_ver] > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_HOST))
            {
                return CTC_E_NO_RESOURCE;
            }
        }
        else
        {
            if (p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_6]
                >= SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_LPM))
            {
                return CTC_E_NO_RESOURCE;
            }
        }
    }

    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR) || is_napt)
    {
        new_opt = DO_HASH;
    }
    else if (DO_HASH != old_opt)
    {
        new_opt = old_opt;
    }

    if (DO_HASH == old_opt && DO_HASH != new_opt)
    {
        p_ipuc_info_temp = &ipuc_info_temp;
        SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info_temp);
        SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info_temp);
        /* Judge operation */
        _sys_goldengate_ipuc_operation_check(lchip, p_ipuc_info_temp, &new_opt);
       p_ipuc_info->route_opt = p_ipuc_info_temp->route_opt;

        if (DO_TCAM == new_opt)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "update to TCAM\n");
            ret = _sys_goldengate_ipuc_alloc_tcam_key_index(lchip, p_ipuc_info);
            if (ret != CTC_E_NONE)
            {
                sal_memcpy(p_ipuc_info, &ipuc_info_old, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
                return ret;
            }
            /* write ipuc tcam key entry */
            _sys_goldengate_ipuc_write_key_ex(lchip, p_ipuc_info);
        }
        else if (DO_LPM == new_opt)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "update to LPM\n");
            ret = _sys_goldengate_ipuc_alloc_hash_lpm_key_index(lchip, p_ipuc_info, NULL);
            if (ret != CTC_E_NONE)
            {
                sal_memcpy(p_ipuc_info, &ipuc_info_old, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
                return ret;
            }

            /* write lpm key entry */
            _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

            /* write hash key entry */
            _sys_goldengate_l3_hash_add_key(lchip, p_ipuc_info);
        }
    }
    else if(DO_HASH != old_opt && DO_HASH == new_opt)
    {
        ret = _sys_goldengate_ipuc_add_host_route(lchip, p_ipuc_info, 0);
        if (CTC_E_NONE != ret)
        {
            p_ipuc_info->conflict_flag = 1;
            new_opt = old_opt;

            goto UPDATE_KEY_AND_AD;
        }

        p_ipuc_info->conflict_flag = 0;
        p_ipuc_info->route_opt = DO_HASH;

        new_key_offset = p_ipuc_info->key_offset;

        if (DO_TCAM == old_opt)
        {   /*do TCAM remove*/

            /*do TCAM remove*/
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM remove\n");
            p_ipuc_info->key_offset = old_key_offset;
            /* remove ipuc key entry from TCAM */
            _sys_goldengate_ipuc_remove_key_ex(lchip, p_ipuc_info);

            /* TCAM do not need to build remove offset, only need to clear soft offset info */
            _sys_goldengate_ipuc_free_tcam_ad_index(lchip, p_ipuc_info);

            p_ipuc_info->key_offset = new_key_offset;
        }else{
            /* do LPM remove */
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "LPM remove\n");

            /* free lpm index */
            _sys_goldengate_lpm_del(lchip, p_ipuc_info);

            /* write lpm key entry */
            _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

            /* write hash key entry */
            _sys_goldengate_l3_hash_remove_key(lchip, p_ipuc_info);

            /* free hash index */
            _sys_goldengate_l3_hash_free_key_index(lchip, p_ipuc_info);
        }
    }

UPDATE_KEY_AND_AD:
    /* 1. remove old ad resource */
    _sys_goldengate_ipuc_unbind_nexthop(lchip, &ipuc_info_old);

    SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);
    rpf_port.rpf_port_num = p_ipuc_param->rpf_port_num;
    sal_memcpy(&rpf_port.rpf_port, &p_ipuc_param->rpf_port, sizeof(rpf_port.rpf_port));

    /* 2. build ipda, include lpm ad profile */
    CTC_ERROR_GOTO(_sys_goldengate_ipuc_build_ipda(lchip, p_ipuc_info, &rpf_rslt, 0, &rpf_port), ret, IPUC_UPDATE_ERROR_RETURN1);
    update_key = !(p_ipuc_info->ad_offset == old_ad_offset);
    if (DO_HASH == old_opt && DO_HASH != new_opt)
    {
        /*do HOST remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Host hash remove\n");
        _sys_goldengate_ipuc_remove_host_route(lchip, p_ipuc_info);
    }

    /* 3. for lpm, need update soft db */
    if ((update_key) && (DO_LPM == new_opt))
    {
        if(1 == update_lpm_ad_index)
        {
            _sys_goldengate_ipuc_update_lpm_key_index_O(lchip, p_ipuc_info);
        }
        else
        {
            _sys_goldengate_ipuc_update_lpm_key_index(lchip, p_ipuc_info);
        }
    }

    /* 4. write ipda */
    _sys_goldengate_ipuc_remove_rpf(lchip, &ipuc_info_old);    /* remove old rpf before write ipda */

    _sys_goldengate_ipuc_write_ipda(lchip, p_ipuc_info, &rpf_rslt);

    /* 5. write key */
    if (DO_HASH == new_opt)
    {
        /* hash key always need update for aging */
        _sys_goldengate_ipuc_add_host_route(lchip, p_ipuc_info,1);
    }

    if (update_key)
    {
        if (DO_LPM == new_opt)
        {
            /* write lpm key entry */
            _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

            /* write hash key entry */
            _sys_goldengate_l3_hash_add_key(lchip, p_ipuc_info);
        }
        else if (DO_TCAM == new_opt)
        {
            /* write ipuc tcam key entry */
            _sys_goldengate_ipuc_write_key(lchip, p_ipuc_info);
        }
    }

    /* free old lpm ad profile */
    _sys_goldengate_ipuc_lpm_ad_profile_remove(lchip, &ipuc_info_old);

    if (update_key && (DO_TCAM == new_opt))
    {
        _sys_goldengate_ipuc_push_down(lchip, p_ipuc_info, DO_TCAM);
    }

    if (!old_conflict_flag && p_ipuc_info->conflict_flag)
    {
        if (DO_TCAM == new_opt)
        {
            p_gg_ipuc_master[lchip]->conflict_tcam_counter[p_ipuc_param->ip_ver] ++;
        }
        else
        {
            p_gg_ipuc_master[lchip]->conflict_alpm_counter[p_ipuc_param->ip_ver] ++;
        }
    }
    else if (old_conflict_flag && !p_ipuc_info->conflict_flag)
    {
        if (DO_TCAM == new_opt)
        {
            p_gg_ipuc_master[lchip]->conflict_tcam_counter[p_ipuc_param->ip_ver] --;
        }
        else
        {
            p_gg_ipuc_master[lchip]->conflict_alpm_counter[p_ipuc_param->ip_ver] --;
        }
    }

    /*napt don't match this condition*/
    if (DO_HASH == old_opt && DO_HASH != new_opt)
    {
        p_gg_ipuc_master[lchip]->hash_counter[p_ipuc_param->ip_ver] --;
        if (DO_LPM == new_opt)
        {
            p_gg_ipuc_master[lchip]->alpm_counter[p_ipuc_param->ip_ver] ++;/*tcam counter tcam_ip_count in ipuc_db_master*/
        }
    }
    else if (DO_HASH != old_opt && DO_HASH == new_opt)
    {
        p_gg_ipuc_master[lchip]->hash_counter[p_ipuc_param->ip_ver] ++;
        if (DO_LPM == old_opt)
        {
            p_gg_ipuc_master[lchip]->alpm_counter[p_ipuc_param->ip_ver] --;
        }
    }

    if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver] && (!is_napt))
    {
        if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
            && !CTC_FLAG_ISSET(old_route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            p_gg_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] ++;
            p_gg_ipuc_master[lchip]->lpm_counter[p_ipuc_param->ip_ver] --;
        }
        else if (!CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
            && CTC_FLAG_ISSET(old_route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            p_gg_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] --;
            p_gg_ipuc_master[lchip]->lpm_counter[p_ipuc_param->ip_ver] ++;
        }
    }

    return CTC_E_NONE;

IPUC_UPDATE_ERROR_RETURN1:
     /* 1. revert hw*/
    if (DO_HASH == old_opt && DO_HASH != new_opt)
    {
        /*remove new*/
        if (DO_TCAM == new_opt)
        {
            _sys_goldengate_ipuc_remove_key_ex(lchip, p_ipuc_info);
            _sys_goldengate_ipuc_free_tcam_ad_index(lchip, p_ipuc_info);
        }
        else if (DO_LPM == new_opt)
        {
            _sys_goldengate_lpm_del(lchip, p_ipuc_info);
            _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);
            _sys_goldengate_l3_hash_remove_key(lchip, p_ipuc_info);
            _sys_goldengate_l3_hash_free_key_index(lchip, p_ipuc_info);
        }
    }
    if (DO_HASH != old_opt && DO_HASH == new_opt)
    {
        /*add old*/
        if (DO_TCAM == old_opt)
        {
            _sys_goldengate_ipuc_alloc_tcam_key_index(lchip, &ipuc_info_old);
            _sys_goldengate_ipuc_write_key_ex(lchip, &ipuc_info_old);
        }
        else if (DO_LPM == old_opt)
        {
            _sys_goldengate_ipuc_alloc_hash_lpm_key_index(lchip, &ipuc_info_old, NULL);
            _sys_goldengate_lpm_add_key(lchip, &ipuc_info_old, LPM_TABLE_MAX);
            _sys_goldengate_l3_hash_add_key(lchip, &ipuc_info_old);
        }
        /*remove new*/
        _sys_goldengate_ipuc_remove_host_route(lchip, p_ipuc_info);
    }

    /* 2. revert sw */
    sal_memcpy(p_ipuc_info, &ipuc_info_old, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
    /* 3. bind old nhid */
    _sys_goldengate_ipuc_bind_nexthop(lchip, &ipuc_info_old);

    return ret;
}

/* when add route, need do push down */
int32
_sys_goldengate_ipuc_push_down(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 opt)
{
    if (opt == DO_LPM)
    {
        /* only push down itself */
        _sys_goldengate_ipuc_push_down_itself(lchip, p_ipuc_info);
    }
    else if (opt == DO_TCAM)
    {
        /* need push down all */
        _sys_goldengate_ipuc_push_down_all(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

/* when remove route, need do push up */
STATIC int32
_sys_goldengate_ipuc_push_up(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{

    if (DO_LPM == p_ipuc_info->route_opt)
    {
        /* only push up itself */
        _sys_goldengate_ipuc_push_up_itself(lchip, p_ipuc_info);
    }
    else if (DO_TCAM == p_ipuc_info->route_opt)
    {
        /* need push up all */
        _sys_goldengate_ipuc_push_up_all(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

#define __3_OTHER__
STATIC int32
_sys_goldengate_ipuc_flag_check(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_nh_info_dsnh_t nh_info;
    uint32 cmd = 0;
    uint32 rpf_check_port = 0;

    CTC_PTR_VALID_CHECK(p_ipuc_param);
    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

    cmd = DRV_IOR(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rpf_check_port));

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ipuc_param->nh_id, &nh_info));

    /* if masklen is 32 for ipv4 or 128 for ipv6, add the key to hash table to save tcam/lpm resource*/
    if ((p_ipuc_param->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        && (!p_gg_ipuc_master[lchip]->host_route_mode))
    {
        CTC_SET_FLAG(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR);
    }

    /* neighbor flag check */
    if ((p_ipuc_param->masklen != p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
    {
        /* clear neighbor flag */
        CTC_UNSET_FLAG(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR);
    }

    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_NEIGHBOR)
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_RPF_CHECK))
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

    if ((nh_info.ecmp_valid)
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
        && nh_info.ecmp_cnt > SYS_GG_MAX_IPMC_RPF_IF)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (rpf_check_port
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_ICMP_CHECK)
        && CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        /* when rpf check port, icmp check and rpf check can not both support */
        return CTC_E_INVALID_PARAM;
    }

    /* assign port check */
    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_ASSIGN_PORT)
        && nh_info.ecmp_valid)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* NAPT check */
    if (p_ipuc_param->l4_dst_port > 0)
    {
        if (p_ipuc_param->masklen != p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        {
            return CTC_E_IPUC_NAT_NOT_SUPPORT_THIS_MASK_LEN;
        }

        if (p_ipuc_param->ip_ver == CTC_IP_VER_6)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
    }

    /* for tcam default route */
    if (p_ipuc_param->masklen == 0)
    {
        CTC_SET_FLAG(p_ipuc_param->route_flag, SYS_IPUC_FLAG_DEFAULT);
        CTC_SET_FLAG(p_ipuc_param->route_flag, CTC_IPUC_FLAG_RPF_CHECK);
    }

    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_STATS_EN))
    {
        uint16 stats_ptr = 0;
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_ipuc_param->stats_id, CTC_STATS_STATSID_TYPE_IP, &stats_ptr));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_rpf_check_port(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    SYS_IPUC_INIT_CHECK(lchip);

    cmd = DRV_IOW(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    field_value = enable ? 1 : 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    _sys_goldengate_rpf_set_default(lchip);
    return CTC_E_NONE;
}

#define ___________IPUC_OUTER_FUNCTION________________________
#define __0_IPUC_API__
int32
sys_goldengate_ipuc_add(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_rpf_rslt_t rpf_rslt;
    uint8 opt = 0;
    uint8 step[2] = {1, 2};
    int32 ret = CTC_E_NONE;
    uint8 is_napt = 0;
    sys_ipuc_info_t ipuc_info;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_ipuc_rpf_port_info_t rpf_port;
    ctc_ipuc_param_t *p_ipuc_param = NULL;
    ctc_ipuc_param_t ipuc_param_tmp = {0};

    SYS_IPUC_INIT_CHECK(lchip);
    is_napt = p_ipuc_param_info->l4_dst_port ? 1 : 0;
    p_ipuc_param = &ipuc_param_tmp;
    sal_memcpy(p_ipuc_param,p_ipuc_param_info,sizeof(ctc_ipuc_param_t));

    /* param check and debug out */
    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IP_VRFID_CHECK(p_ipuc_param);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);
    if (CTC_FLAG_ISSET(p_ipuc_param->route_flag, CTC_IPUC_FLAG_AGING_EN))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }
    if (0 == p_ipuc_param->masklen
        &&  ( (0 == p_gg_ipuc_master[lchip]->default_route_mode &&  CTC_IP_VER_4 == p_ipuc_param->ip_ver)
            ||(CTC_IP_VER_6 == p_ipuc_param->ip_ver)))
    {
        ret = sys_goldengate_ipuc_add_default_entry(lchip, p_ipuc_param->ip_ver, p_ipuc_param->vrf_id, p_ipuc_param->nh_id, p_ipuc_param_info);
        if(CTC_E_NONE == ret)
        {
            sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));

            p_ipuc_info = &ipuc_info;

            SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
            SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

            CTC_ERROR_RETURN(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_info));

            if (NULL == p_ipuc_info)
            {
                p_ipuc_info = mem_malloc(MEM_IPUC_MODULE,  p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
                if (NULL == p_ipuc_info)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_ipuc_info, 0, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);

                SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
                SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

                p_ipuc_info->route_flag |=
                    SYS_IPUC_FLAG_DEFAULT | CTC_IPUC_FLAG_RPF_CHECK;

                SYS_IPUC_LOCK;
                _sys_goldengate_ipuc_db_add(lchip, p_ipuc_info);
                SYS_IPUC_UNLOCK;
            }

            p_ipuc_info->ad_offset =
                p_gg_ipuc_master[lchip]->default_base[SYS_IPUC_VER_VAL(ipuc_info)] +
                (p_ipuc_info->vrf_id & 0x3FFF);
            p_ipuc_info->nh_id = p_ipuc_param->nh_id;
        }

        return ret;
    }

    /* prepare data */
    sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));
    sal_memset(&rpf_port, 0, sizeof(rpf_port));

    /* para check */
    CTC_ERROR_RETURN(_sys_goldengate_ipuc_flag_check(lchip, p_ipuc_param));

    p_ipuc_info = &ipuc_info;

    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
    SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

    SYS_IPUC_LOCK;

    /* 1. lookup sw node */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_info));

    if (NULL != p_ipuc_info)
    {
        CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_update(lchip, p_ipuc_info, p_ipuc_param));
    }
    else
    {
        p_ipuc_info = mem_malloc(MEM_IPUC_MODULE,  p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);
        if (NULL == p_ipuc_info)
        {
            CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_NO_MEMORY);
        }

        sal_memset(p_ipuc_info, 0, p_gg_ipuc_master[lchip]->info_size[p_ipuc_param->ip_ver]);

        SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
        SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);
        rpf_port.rpf_port_num = p_ipuc_param->rpf_port_num;
        sal_memcpy(&rpf_port.rpf_port, &p_ipuc_param->rpf_port, sizeof(rpf_port.rpf_port));

        /* 2.Judge operation */
        _sys_goldengate_ipuc_operation_check(lchip, p_ipuc_info, &opt);

        if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver] && !is_napt
            && CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            if (p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_6] * 2
                    + step[p_ipuc_param->ip_ver] > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_HOST))
            {
                mem_free(p_ipuc_info);
                CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_NO_RESOURCE);
            }
        }
        else
        {
            if (p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_6]
                >= SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_LPM))
            {
                mem_free(p_ipuc_info);
                CTC_ERROR_RETURN_IPUC_UNLOCK(CTC_E_NO_RESOURCE);
            }
        }

        /* 3. build ipda, include lpm ad profile */
        CTC_ERROR_GOTO(_sys_goldengate_ipuc_build_ipda(lchip, p_ipuc_info, &rpf_rslt, p_ipuc_param->stats_id, &rpf_port), ret, IPUC_ADD_ERROR_RETURN2);

        /* 4. write ipda */
        _sys_goldengate_ipuc_write_ipda(lchip, p_ipuc_info, &rpf_rslt);

        /* 5. build key index */
        if (opt == DO_HASH)     /* for host route, add it to hash table directly */
        {
            ret = _sys_goldengate_ipuc_add_host_route(lchip, p_ipuc_info,0);
            if (ret < 0)
            {
                if (is_napt)    /* for NAPT, there's no tcam to solve conflict */
                {
                    goto IPUC_ADD_ERROR_RETURN1;
                }
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add host route failed ret = %d. Than do tcam or lpm. \r\n", ret);
                if (p_ipuc_param->ip_ver == CTC_IP_VER_6)
                {
                    opt = DO_TCAM;
                }
                else if (p_gg_ipuc_master[lchip]->use_hash16)
                {
                    opt = DO_LPM;       /* for hash16, must DO_LPM*/
                }
                else
                {
                    opt = DO_TCAM;      /* for hash8, must DO_TCAM */
                }

                p_ipuc_info->conflict_flag = 1;
                p_ipuc_info->route_opt = opt;
                CTC_UNSET_FLAG(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR);
            }
            else
            {
                if (is_napt)
                {
                    p_gg_ipuc_master[lchip]->napt_hash_counter ++;
                }
                else
                {
                    p_gg_ipuc_master[lchip]->hash_counter[p_ipuc_param->ip_ver] ++;
                }
            }
        }

        if (opt == DO_LPM)
        {
            ret = _sys_goldengate_ipuc_alloc_hash_lpm_key_index(lchip, p_ipuc_info, NULL);
            if (ret != CTC_E_NONE)
            {
                goto IPUC_ADD_ERROR_RETURN1;
            }
        }
        else if (opt == DO_TCAM)
        {
            ret = _sys_goldengate_ipuc_alloc_tcam_key_index(lchip, p_ipuc_info);
            if (ret != CTC_E_NONE)
            {
                goto IPUC_ADD_ERROR_RETURN1;
            }
        }

        /* 6. write ip key (if do_lpm = true, write lpm key to asic, else write Tcam key) */
        if (opt == DO_LPM)
        {
            /* write ipuc lpm key entry */
            _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

            /* write ipuc tcam key entry */
            _sys_goldengate_l3_hash_add_key(lchip, p_ipuc_info);

            p_gg_ipuc_master[lchip]->alpm_counter[p_ipuc_param->ip_ver] ++;

            if (p_ipuc_info->conflict_flag)
            {
                p_gg_ipuc_master[lchip]->conflict_alpm_counter[p_ipuc_param->ip_ver] ++;
            }
        }
        else if (opt == DO_TCAM)
        {
            /* write ipuc tcam key entry */
            _sys_goldengate_ipuc_write_key_ex(lchip, p_ipuc_info);

            if (p_ipuc_info->conflict_flag)
            {
                p_gg_ipuc_master[lchip]->conflict_tcam_counter[p_ipuc_param->ip_ver] ++;
            }
        }

        /* 7. write to soft table */
        _sys_goldengate_ipuc_db_add(lchip, p_ipuc_info);

        /* stats */
        if ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
            && CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
        {
            if(!is_napt)
            {
                p_gg_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] ++;
            }
        }
        else
        {
            p_gg_ipuc_master[lchip]->lpm_counter[p_ipuc_param->ip_ver] ++;
        }
    }

    _sys_goldengate_ipuc_push_down(lchip, p_ipuc_info, opt);

    CTC_RETURN_IPUC_UNLOCK(CTC_E_NONE);

IPUC_ADD_ERROR_RETURN1:
    _sys_goldengate_ipuc_unbuild_ipda(lchip, p_ipuc_info);

IPUC_ADD_ERROR_RETURN2:
    if(p_ipuc_info->lpm_result)
    {
        mem_free(p_ipuc_info->lpm_result);
    }
    mem_free(p_ipuc_info);
    CTC_RETURN_IPUC_UNLOCK(ret);
}

int32
sys_goldengate_ipuc_add_lpm(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_wb_lpm_info_t* lpm_info)
{
    int32 ret = CTC_E_NONE;
    sys_l3_hash_t *p_hash;

    ret = _sys_goldengate_ipuc_alloc_hash_lpm_key_index(lchip, p_ipuc_info, lpm_info);
    if (ret != CTC_E_NONE)
    {
        if(p_ipuc_info->lpm_result)
        {
            mem_free(p_ipuc_info->lpm_result);
        }

        return ret;
    }

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);

    p_hash->masklen_pushed= lpm_info->masklen_pushed;
    p_hash->is_mask_valid = lpm_info->is_mask_valid;
    p_hash->is_pushed = lpm_info->is_pushed;

    return CTC_E_NONE;
}

/**
 @brief function of remove ip route

 @param[in] p_ipuc_param, parameters used to remove ip route

 @return CTC_E_XXX
 */
int32
sys_goldengate_ipuc_remove(uint8 lchip,ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_ipuc_info_t ipuc_info;
    ctc_ipuc_param_t *p_ipuc_param = NULL;
    ctc_ipuc_param_t ipuc_param_tmp = {0};
    int32            ret            = CTC_E_NONE;
    uint8 is_napt = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    is_napt = p_ipuc_param_info->l4_dst_port ? 1 : 0;
    p_ipuc_param = &ipuc_param_tmp;
    sal_memcpy(p_ipuc_param,p_ipuc_param_info,sizeof(ctc_ipuc_param_t));

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);

    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);

    if (0 == p_ipuc_param->masklen
        &&  ( (0 == p_gg_ipuc_master[lchip]->default_route_mode &&  CTC_IP_VER_4 == p_ipuc_param->ip_ver)
            ||(CTC_IP_VER_6 == p_ipuc_param->ip_ver)))
    {
        ret = sys_goldengate_ipuc_add_default_entry(lchip, p_ipuc_param->ip_ver, p_ipuc_param->vrf_id, SYS_NH_RESOLVED_NHID_FOR_DROP, NULL);
        if(CTC_E_NONE == ret)
        {
            sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));

            p_ipuc_info = &ipuc_info;

            SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);
            SYS_IPUC_DATA_MAP(p_ipuc_param, p_ipuc_info);

            CTC_ERROR_RETURN(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_info));

            if (NULL != p_ipuc_info)
            {
                SYS_IPUC_LOCK;
                _sys_goldengate_ipuc_db_remove(lchip, p_ipuc_info);
                mem_free(p_ipuc_info);
                SYS_IPUC_UNLOCK;
            }
        }

        return ret;
    }

    /* prepare data */
    sal_memset(&ipuc_info, 0, sizeof(sys_ipuc_info_t));
    p_ipuc_info = &ipuc_info;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_info);

    SYS_IPUC_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_info));
    if (!p_ipuc_info)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (DO_HASH == p_ipuc_info->route_opt)
    {
        /*do HOST remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Host hash remove\n");

        _sys_goldengate_ipuc_remove_host_route(lchip, p_ipuc_info);

        if (is_napt)
        {
            p_gg_ipuc_master[lchip]->napt_hash_counter --;
        }
        else
        {
            p_gg_ipuc_master[lchip]->hash_counter[p_ipuc_param->ip_ver] --;
        }
    }
    else if (DO_TCAM == p_ipuc_info->route_opt)
    {   /*do TCAM remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM remove\n");

        /* remove ipuc key entry from TCAM */
        _sys_goldengate_ipuc_remove_key_ex(lchip, p_ipuc_info);

        /* TCAM do not need to build remove offset, only need to clear soft offset info */
        _sys_goldengate_ipuc_free_tcam_ad_index(lchip, p_ipuc_info);

        /* decrease counter */
        if (p_ipuc_info->conflict_flag)
        {
            p_gg_ipuc_master[lchip]->conflict_tcam_counter[p_ipuc_param->ip_ver] --;
        }
    }
    else
    {   /* do LPM remove */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "LPM remove\n");

        /* free lpm index */
        _sys_goldengate_lpm_del(lchip, p_ipuc_info);

        /* write lpm key entry */
        _sys_goldengate_lpm_add_key(lchip, p_ipuc_info, LPM_TABLE_MAX);

        /* write hash key entry */
        _sys_goldengate_l3_hash_remove_key(lchip, p_ipuc_info);

        /* free hash index */
        _sys_goldengate_l3_hash_free_key_index(lchip, p_ipuc_info);

        p_gg_ipuc_master[lchip]->alpm_counter[p_ipuc_param->ip_ver] --;

        if (p_ipuc_info->conflict_flag)
        {
            p_gg_ipuc_master[lchip]->conflict_alpm_counter[p_ipuc_param->ip_ver] --;
        }
    }

    /* write to soft table */
    _sys_goldengate_ipuc_db_remove(lchip, p_ipuc_info);

    /* unbuild ipda */
    _sys_goldengate_ipuc_unbuild_ipda(lchip, p_ipuc_info);

    if ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->max_mask_len[p_ipuc_param->ip_ver])
        && CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_NEIGHBOR))
    {
        if(!is_napt)
        {
            p_gg_ipuc_master[lchip]->host_counter[p_ipuc_param->ip_ver] --;
        }
    }
    else
    {
        p_gg_ipuc_master[lchip]->lpm_counter[p_ipuc_param->ip_ver] --;
    }

    _sys_goldengate_ipuc_push_up(lchip, p_ipuc_info);

    if (p_ipuc_info->lpm_result)
    {
        mem_free(p_ipuc_info->lpm_result);
    }

    mem_free(p_ipuc_info);

    SYS_IPUC_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_get(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param)
{
    sys_ipuc_info_t ipuc_info;
    sys_ipuc_info_t* p_ipuc_info = NULL;

    SYS_IPUC_INIT_CHECK(lchip);

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

    CTC_ERROR_RETURN(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_info));
    if (NULL != p_ipuc_info)
    {
        p_ipuc_param->nh_id = p_ipuc_info->nh_id;
        p_ipuc_param->route_flag = p_ipuc_info->route_flag;
        p_ipuc_param->l4_dst_port = p_ipuc_info->l4_dst_port;
        p_ipuc_param->gport = p_ipuc_info->gport;
        p_ipuc_param->stats_id = p_ipuc_info->stats_id;
        CTC_UNSET_FLAG(p_ipuc_param->route_flag, SYS_IPUC_FLAG_IS_IPV6);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_add_default_entry(uint8 lchip, uint8 ip_ver, uint16 vrfid, uint32 nh_id, ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_ipuc_info_t ipuc_info;
    sys_rpf_rslt_t rpf_rslt;
    sys_rpf_info_t rpf_info;

    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_FUNC();
    SYS_IP_CHECK_VERSION_ENABLE(ip_ver);

    sal_memset(&ipuc_info, 0, p_gg_ipuc_master[lchip]->info_size[ip_ver]);
    sal_memset(&rpf_info, 0, sizeof(sys_rpf_info_t));
    sal_memset(&rpf_rslt, 0, sizeof(sys_rpf_rslt_t));

    if(1 == p_gg_ipuc_master[lchip]->default_route_mode && CTC_IP_VER_4 == ip_ver)
    {
        return CTC_E_NOT_SUPPORT;
    }

    rpf_info.usage = SYS_RPF_USAGE_TYPE_DEFAULT;

    CTC_ERROR_RETURN(sys_goldengate_rpf_add_profile(lchip, &rpf_info, &rpf_rslt));

    ipuc_info.nh_id = nh_id;

    if (p_ipuc_param_info)
    {
        ipuc_info.route_flag = p_ipuc_param_info->route_flag;
    }
    ipuc_info.route_flag |= (ip_ver == CTC_IP_VER_6) ? SYS_IPUC_FLAG_IS_IPV6 : 0;

    SYS_IPUC_LOCK;

    ipuc_info.ad_offset = p_gg_ipuc_master[lchip]->default_base[SYS_IPUC_VER_VAL(ipuc_info)] + (vrfid & 0x3FFF);
    ipuc_info.route_flag |= SYS_IPUC_FLAG_DEFAULT | CTC_IPUC_FLAG_RPF_CHECK;

    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_write_ipda(lchip, &ipuc_info, &rpf_rslt));

    SYS_IPUC_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_add_nat_sa_default_entry(uint8 lchip)
{
    uint32 entry_num = 0;
    ds_t v4_key, v4_mask;
    tbl_entry_t tbl_ipkey;
    uint32 dsfwd_offset;
    uint32 ad_offset = 0;
    ds_t dsnatsa;
    ds_t dsad;

    uint32 cmd;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IPUC_DBG_FUNC();


    sal_memset(&v4_key, 0, sizeof(v4_key));
    sal_memset(&v4_mask, 0, sizeof(v4_mask));
    sal_memset(&dsnatsa, 0, sizeof(dsnatsa));
    sal_memset(&dsad, 0, sizeof(dsad));

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv4NAT160Key_t, &entry_num));
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsIpDa_t, 0, 1, &ad_offset));

    /* write natsa */
    CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, SYS_NH_RESOLVED_NHID_FOR_TOCPU, &dsfwd_offset));
    SetDsIpSaNat(V, ipSaFwdPtrValid_f, dsnatsa, 1);
    SetDsIpSaNat(V, u1_g2_dsFwdPtr_f, dsnatsa, dsfwd_offset);

    cmd = DRV_IOW(DsIpSaNat_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, ad_offset, cmd, &dsnatsa);

    /* write ad */
    SetDsLpmTcamIpv4Nat160Ad(V, nexthop_f, dsad, ad_offset);
    cmd = DRV_IOW(DsLpmTcamIpv4Nat160Ad_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (entry_num-1), cmd, &dsad);

    /* write key */
    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, v4_key, 2);
    SetDsLpmTcamIpv4160Key(V, lpmTcamKeyType_f, v4_mask, 3);

    tbl_ipkey.data_entry = (uint32*)&v4_key;
    tbl_ipkey.mask_entry = (uint32*)&v4_mask;

    cmd = DRV_IOW(DsLpmTcamIpv4NAT160Key_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, (entry_num-1), cmd, &tbl_ipkey);
    p_gg_ipuc_master[lchip]->snat_tcam_counter ++;

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_add_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param)
{
    int32  ret = CTC_E_NONE;
    uint32 table_id = DsIpDa_t;
    uint8  do_hash = 1;
    uint32 ad_offset = 0;
    sys_ipuc_nat_sa_info_t nat_info;
    sys_ipuc_nat_sa_info_t* p_nat_info = NULL;

    SYS_IPUC_INIT_CHECK(lchip);

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

    SYS_IPUC_LOCK;
    /* 1. lookup sw node */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_nat_db_lookup(lchip, &p_nat_info));
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
    if ((!CTC_FLAG_ISSET(p_gg_ipuc_master[lchip]->lookup_mode[p_ipuc_nat_sa_param->ip_ver], SYS_IPUC_NAT_HASH_LOOKUP))
        || (p_nat_info->l4_src_port == 0))
    {
        do_hash = FALSE;
    }

    /* 2. alloc key offset */
    if (do_hash)
    {
        ret = _sys_goldengate_ipuc_alloc_nat_hash_key_index(lchip, p_nat_info);
        if (ret)
        {
            goto IPUC_ADD_ERROR_RETURN2;
        }

        if (p_nat_info->in_tcam)
        {
            do_hash = 0;
        }
    }

    if (!do_hash)
    {
        CTC_ERROR_GOTO(_sys_goldengate_ipuc_alloc_nat_tcam_ad_index(lchip, p_nat_info), ret, IPUC_ADD_ERROR_RETURN2);
        p_nat_info->in_tcam = 1;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC NAT TCAM: p_nat_info->key_offset:%d \n", p_nat_info->key_offset);
    }

    /* 3. alloc ad offset */
    CTC_ERROR_GOTO(sys_goldengate_ftm_alloc_table_offset(lchip, table_id, 0, 1, &ad_offset), ret, IPUC_ADD_ERROR_RETURN1);
    p_nat_info->ad_offset = ad_offset;
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC NAT ad_offset: p_nat_info->ad_offset:%d \n", p_nat_info->ad_offset);

    /* 4. write nat sa */
    _sys_goldengate_ipuc_nat_write_ipsa(lchip, p_nat_info);

    /* 5. write nat key (if do_hash = true, write hash key to asic, else write Tcam key) */
    if (do_hash)
    {
        /* write ipuc hash key entry */
        _sys_goldengate_ipuc_add_hash_nat_sa_key(lchip, p_nat_info);

        p_gg_ipuc_master[lchip]->snat_hash_counter ++;
    }
    else
    {
        _sys_goldengate_ipuc_write_nat_key(lchip, p_nat_info);

        p_gg_ipuc_master[lchip]->snat_tcam_counter ++;
    }

    /* 6. write to soft table */
    _sys_goldengate_ipuc_nat_db_add(lchip, p_nat_info);

    CTC_RETURN_IPUC_UNLOCK(CTC_E_NONE);

IPUC_ADD_ERROR_RETURN1:
    if (!do_hash)
    {
        _sys_goldengate_ipuc_free_nat_tcam_ad_index(lchip, p_nat_info);
    }

IPUC_ADD_ERROR_RETURN2:
    mem_free(p_nat_info);
    CTC_RETURN_IPUC_UNLOCK(ret);
}

int32
sys_goldengate_ipuc_remove_nat_sa(uint8 lchip, ctc_ipuc_nat_sa_param_t* p_ipuc_nat_sa_param)
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

    SYS_IPUC_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_nat_db_lookup(lchip, &p_nat_info));
    if (!p_nat_info)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    if (p_nat_info->in_tcam)
    {   /*do TCAM remove*/
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM remove\n");

        /* remove nat key entry from TCAM */
        _sys_goldengate_ipuc_remove_nat_key(lchip, p_nat_info);

        /* TCAM do not need to build remove offset, only need to clear soft offset info */
        _sys_goldengate_ipuc_free_nat_tcam_ad_index(lchip, p_nat_info);
    }
    else
    {   /* do HASH remove */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "HASH remove\n");

        /* remove hash key entry */
        _sys_goldengate_ipuc_remove_hash_nat_sa_key(lchip, p_nat_info);
    }

    /* free ad offset */
    sys_goldengate_ftm_free_table_offset(lchip, DsIpDa_t, 0, 1, p_nat_info->ad_offset);

    /* write to soft table */
    _sys_goldengate_ipuc_nat_db_remove(lchip, p_nat_info);

    /* decrease counter */
    if (p_nat_info->in_tcam)
    {
        p_gg_ipuc_master[lchip]->snat_tcam_counter --;
    }
    else
    {
        p_gg_ipuc_master[lchip]->snat_hash_counter--;
    }

    SYS_IPUC_UNLOCK;

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_ipuc_get_scl_gid(uint8 lchip, uint8 type, uint8 location)
{
    uint32  gid = 0;

    switch(type)
    {
    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
    case SYS_SCL_KEY_TCAM_IPV6:
        gid = SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL + location;
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
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
sys_goldengate_ipuc_add_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param)
{
    sys_scl_entry_t*  p_scl_entry = NULL;
    sys_scl_entry_t*  p_scl_entry_rpf = NULL;
    uint32          gid = 0;
    uint32 fwd_offset;
    uint16 max_vrfid_num = 0;
    uint16 policer_ptr = 0;
    uint16 stats_ptr = 0;
    uint8 is_bwp = 0;
    uint8 is_triple_play = 0;
    uint8 gre_options_num = 0;
    int32 ret = 0;

    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_tunnel_param);
    CTC_IP_VER_CHECK(p_ipuc_tunnel_param->ip_ver);
    SYS_IP_TUNNEL_KEY_CHECK(p_ipuc_tunnel_param);
    SYS_IP_TUNNEL_AD_CHECK(p_ipuc_tunnel_param);
    CTC_MAX_VALUE_CHECK(p_ipuc_tunnel_param->scl_id, 1);
    CTC_MAX_VALUE_CHECK(p_ipuc_tunnel_param->payload_type, MAX_CTC_IPUC_TUNNEL_PAYLOAD_TYPE-1);
    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK) && (p_ipuc_tunnel_param->logic_port))
    {
        /* bind logic port */
        SYS_LOGIC_PORT_CHECK(p_ipuc_tunnel_param->logic_port);
    }
    /*SYS_IP_TUNNEL_ADDRESS_SORT(p_ipuc_tunnel_param); -- for scl api, need use big india */
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

    p_scl_entry->key.type = SYS_SCL_KEY_NUM;

    if ((p_ipuc_tunnel_param->logic_port) && (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_METADATA_EN)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto out;
    }

    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        /* build scl key */
        _sys_goldengate_ipuc_build_tunnel_key(lchip, p_scl_entry, p_ipuc_tunnel_param);

        /* get ds_fwd */
        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD))
        {
            CTC_ERROR_GOTO(sys_goldengate_nh_get_dsfwd_offset(lchip, p_ipuc_tunnel_param->nh_id, &fwd_offset), ret, out);
        }

        /* build scl action */
        p_scl_entry->action.type   = SYS_SCL_ACTION_TUNNEL;
        p_scl_entry->action.u.tunnel_action.is_tunnel = 1;
        p_scl_entry->action.u.tunnel_action.tunnel_packet_type =
            (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE) ? 0 :
            (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_V4) ? PKT_TYPE_IPV4 : PKT_TYPE_IPV6;

        if (p_ipuc_tunnel_param->payload_type == CTC_IPUC_TUNNEL_PAYLOAD_TYPE_GRE)/*IpeTunnelDecapCtl.offsetByteShift = 1*/
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
        p_scl_entry->action.u.tunnel_action.classify_use_outer_info =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_QOS_USE_OUTER_INFO) ? 1 : 0;
        p_scl_entry->action.u.tunnel_action.service_acl_qos_en =
            CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_ACL_EN) ? 1 : 0;

        if (p_ipuc_tunnel_param->logic_port)
        {
            /* bind logic port */
            p_scl_entry->action.u.tunnel_action.u1.g2.logic_src_port = p_ipuc_tunnel_param->logic_port & 0x3FFF;
        }

        if(CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag,CTC_IPUC_TUNNEL_FLAG_METADATA_EN))
        {
            if (p_ipuc_tunnel_param->metadata > 0x3FFF)
            {
                ret = CTC_E_INVALID_PARAM;
                goto out;
            }
            p_scl_entry->action.u.tunnel_action.u1.g2.metadata_type  = CTC_METADATA_TYPE_METADATA;
            p_scl_entry->action.u.tunnel_action.u1.g2.logic_src_port = p_ipuc_tunnel_param->metadata & 0x3FFF;
        }

        if (p_ipuc_tunnel_param->policer_id > 0)
        {

            CTC_ERROR_GOTO(sys_goldengate_qos_policer_index_get(lchip, p_ipuc_tunnel_param->policer_id,
                                                                  &policer_ptr,
                                                                  &is_bwp,
                                                                  &is_triple_play), ret, out);
            if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_SERVICE_POLICER_EN))
            {
                p_scl_entry->action.u.tunnel_action.u1.g2.service_policer_valid = 1;
                if (is_triple_play)
                {
                    p_scl_entry->action.u.tunnel_action.u1.g2.service_policer_mode = 1;
                }
            }
            else
            {
                if (is_bwp || is_triple_play)
                {
                    ret = CTC_E_INVALID_PARAM;
                    goto out;
                }
            }
            p_scl_entry->action.u.tunnel_action.chip.policer_ptr = policer_ptr;

        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_STATS_EN))
        {
                CTC_ERROR_GOTO(sys_goldengate_stats_get_statsptr(lchip, p_ipuc_tunnel_param->stats_id, CTC_STATS_STATSID_TYPE_TUNNEL, &stats_ptr), ret, out);
                p_scl_entry->action.u.tunnel_action.chip.stats_ptr = stats_ptr;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_DISCARD))
        {
            p_scl_entry->action.u.tunnel_action.ds_fwd_ptr_valid = 1;
            p_scl_entry->action.u.tunnel_action.u2.dsfwdptr = 0xFFFF;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_BY_OUTER_HEAD))
        {
            p_scl_entry->action.u.tunnel_action.ds_fwd_ptr_valid = 1;
            p_scl_entry->action.u.tunnel_action.u2.dsfwdptr = fwd_offset & 0xFFFF;
        }

        if (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_INNER_VRF_EN))
        {
            max_vrfid_num = sys_goldengate_l3if_get_max_vrfid(lchip, p_ipuc_tunnel_param->ip_ver);
            if (p_ipuc_tunnel_param->vrf_id >= max_vrfid_num)
            {
                ret = CTC_E_IPUC_INVALID_VRF;
                goto out;
            }

            p_scl_entry->action.u.tunnel_action.u2.fid = 1 << 14 | p_ipuc_tunnel_param->vrf_id;
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

        /* write tunnel entry */
        gid = _sys_goldengate_ipuc_get_scl_gid(lchip, p_scl_entry->key.type, p_ipuc_tunnel_param->scl_id);
        ret = sys_goldengate_scl_add_entry(lchip, gid, p_scl_entry, 1);

        if (CTC_E_ENTRY_EXIST == ret)
        {
            /* update entry action */
            CTC_ERROR_GOTO(sys_goldengate_scl_get_entry_id(lchip, p_scl_entry, gid), ret, out);
            CTC_ERROR_GOTO(sys_goldengate_scl_update_action(lchip, p_scl_entry->entry_id, &p_scl_entry->action, 1), ret, out);
        }
        else if (ret < 0)
        {
            goto out;
        }
        else
        {   /* add entry */
            if ((CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_LKUP_WITH_IPSA))
                && ((p_scl_entry->key.type == SYS_SCL_KEY_TCAM_IPV4_SINGLE)
            || (p_scl_entry->key.type == SYS_SCL_KEY_TCAM_IPV6)))
            {   /* IPSA should be high priority */
                CTC_ERROR_GOTO(sys_goldengate_scl_set_entry_priority(lchip, p_scl_entry->entry_id, 100, 1), ret, error0);
            }
            CTC_ERROR_GOTO(sys_goldengate_scl_install_entry(lchip, p_scl_entry->entry_id, 1), ret, error0);
        }
    }
    else    /* rpf process */
    {
        _sys_goldengate_ipuc_build_tunnel_rpf_key(lchip, p_scl_entry_rpf, p_ipuc_tunnel_param);

        /* set scl action */
        p_scl_entry_rpf->action.type   = SYS_SCL_ACTION_TUNNEL;
        p_scl_entry_rpf->action.u.tunnel_action.is_tunnel    = 1;
        p_scl_entry_rpf->action.u.tunnel_action.rpf_check_en = 1;

        /* rpf ifid upto 4 interface , 0 means invalid */
        if ((p_ipuc_tunnel_param->l3_inf[0] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[1] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[2] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[3] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[4] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[5] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[6] > SYS_GG_MAX_CTC_L3IF_ID)
            || (p_ipuc_tunnel_param->l3_inf[7] > SYS_GG_MAX_CTC_L3IF_ID))
        {
            ret = CTC_E_L3IF_INVALID_IF_ID;
            goto out;
        }

        p_scl_entry_rpf->action.u.tunnel_action.u1.data[0] |= ((0 == p_ipuc_tunnel_param->l3_inf[0]) ? 0 : 1) << 14;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[0] |= ((0 == p_ipuc_tunnel_param->l3_inf[1]) ? 0 : 1) << 30;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[1] |= ((0 == p_ipuc_tunnel_param->l3_inf[2]) ? 0 : 1) << 14;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[1] |= ((0 == p_ipuc_tunnel_param->l3_inf[3]) ? 0 : 1) << 30;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[2] |= ((0 == p_ipuc_tunnel_param->l3_inf[4]) ? 0 : 1) << 14;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[2] |= ((0 == p_ipuc_tunnel_param->l3_inf[5]) ? 0 : 1) << 30;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[3] |= ((0 == p_ipuc_tunnel_param->l3_inf[6]) ? 0 : 1) << 14;
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[3] |= ((0 == p_ipuc_tunnel_param->l3_inf[7]) ? 0 : 1) << 30;

        /* tunnel_more_rpf_if */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[0]       |=
            (CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_MORE_RPF) ? 1 : 0) << 15;

        /* tunnel_rpf_if_id0 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[0] |= p_ipuc_tunnel_param->l3_inf[0] & 0x3FF;
        /* tunnel_rpf_if_id1 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[0] |= (p_ipuc_tunnel_param->l3_inf[1] & 0x3FF) << 16;
        /* tunnel_rpf_if_id2 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[1] |= p_ipuc_tunnel_param->l3_inf[2] & 0x3FF;
        /* tunnel_rpf_if_id3 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[1] |= (p_ipuc_tunnel_param->l3_inf[3] & 0x3FF) << 16;
        /* tunnel_rpf_if_id4 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[2] |= p_ipuc_tunnel_param->l3_inf[4] & 0x3FF;
        /* tunnel_rpf_if_id5 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[2] |= (p_ipuc_tunnel_param->l3_inf[5] & 0x3FF) << 16;
        /* tunnel_rpf_if_id6 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[3] |= p_ipuc_tunnel_param->l3_inf[6] & 0x3FF;
        /* tunnel_rpf_if_id7 */
        p_scl_entry_rpf->action.u.tunnel_action.u1.data[3] |= (p_ipuc_tunnel_param->l3_inf[7] & 0x3FF) << 16;

        /* write rpf entry */
        gid = _sys_goldengate_ipuc_get_scl_gid(lchip, p_scl_entry_rpf->key.type, 1);  /* must use scl_id 1 */
        p_scl_entry_rpf->key.tunnel_type = SYS_SCL_TUNNEL_TYPE_RPF;
        ret = sys_goldengate_scl_add_entry(lchip, gid, p_scl_entry_rpf, 1);
        if (CTC_E_ENTRY_EXIST == ret)
        {
            /* update entry action */
            CTC_ERROR_GOTO(sys_goldengate_scl_get_entry_id(lchip, p_scl_entry_rpf, gid), ret, out);
            CTC_ERROR_GOTO(sys_goldengate_scl_update_action(lchip, p_scl_entry_rpf->entry_id, &p_scl_entry_rpf->action, 1), ret, out);
        }
        else if (ret < 0)
        {
            goto out;
        }
        else
        {   /* add entry */
            CTC_ERROR_GOTO(sys_goldengate_scl_install_entry(lchip, p_scl_entry_rpf->entry_id, 1), ret, error0);
        }
    }

    ret = CTC_E_NONE;
    goto out;

error0:
    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        sys_goldengate_scl_remove_entry(lchip, p_scl_entry->entry_id, 1);
    }
    else
    {
        sys_goldengate_scl_remove_entry(lchip, p_scl_entry_rpf->entry_id, 1);
    }

out:
    if(p_scl_entry)
    {
        mem_free(p_scl_entry);
    }

    if (p_scl_entry_rpf)
    {
        mem_free(p_scl_entry_rpf);
    }

    return ret;
}

int32
sys_goldengate_ipuc_remove_tunnel(uint8 lchip, ctc_ipuc_tunnel_param_t* p_ipuc_tunnel_param)
{
    sys_scl_entry_t scl_entry;
    sys_scl_entry_t scl_entry_rpf;
    uint32          gid = 0;

    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_ipuc_tunnel_param);
    CTC_IP_VER_CHECK(p_ipuc_tunnel_param->ip_ver);
    SYS_IP_TUNNEL_KEY_CHECK(p_ipuc_tunnel_param);
    /*SYS_IP_TUNNEL_ADDRESS_SORT(p_ipuc_tunnel_param);*/
    SYS_IP_TUNNEL_FUNC_DBG_DUMP(p_ipuc_tunnel_param);

    sal_memset(&scl_entry, 0, sizeof(sys_scl_entry_t));
    sal_memset(&scl_entry_rpf, 0, sizeof(sys_scl_entry_t));

    if (!CTC_FLAG_ISSET(p_ipuc_tunnel_param->flag, CTC_IPUC_TUNNEL_FLAG_V4_RPF_CHECK))
    {
        /* build scl key */
        _sys_goldengate_ipuc_build_tunnel_key(lchip, &scl_entry, p_ipuc_tunnel_param);

        /* remove entry */
        gid = _sys_goldengate_ipuc_get_scl_gid(lchip, scl_entry.key.type, p_ipuc_tunnel_param->scl_id);
        CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, gid));
        sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1);
        sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1);
    }
    else
    {
        _sys_goldengate_ipuc_build_tunnel_rpf_key(lchip, &scl_entry_rpf, p_ipuc_tunnel_param);

        gid = _sys_goldengate_ipuc_get_scl_gid(lchip, scl_entry_rpf.key.type, 1);
        CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry_rpf, gid));
        sys_goldengate_scl_uninstall_entry(lchip, scl_entry_rpf.entry_id, 1);
        sys_goldengate_scl_remove_entry(lchip, scl_entry_rpf.entry_id, 1);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_traverse(uint8 lchip, uint8 ip_ver, ctc_ipuc_traverse_fn  fn, void* data)
{
    hash_traversal_fn  fun = _sys_goldengate_ipuc_traverse_pre;
    sys_ipuc_traverse_t trav;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION_ENABLE(ip_ver);

    trav.data = data;
    trav.fn = fn;
    trav.lchip = lchip;
    if (NULL == fn)
    {
        return CTC_E_NONE;
    }

    return _sys_goldengate_ipuc_db_traverse(lchip, ip_ver, fun, &trav);
}

int32
sys_goldengate_ipuc_host_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);

    specs_info->used_size = p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_6] * 2;

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_lpm_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);

    specs_info->used_size = p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_6] ;

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_init(uint8 lchip, ctc_ipuc_global_cfg_t* p_ipuc_global_cfg)
{
    int32  ret = 0;
    uint32 entry_num = 0;
    uint32 cmd = 0;
    uint32 offset = 0;
    uint16 max_vrf_num = 0;
    uint16 vrfid = 0;
    uint32 field_value = 0;
    ctc_ip_ver_t ip_ver = MAX_CTC_IP_VER;
    sys_goldengate_opf_t opf;
    ds_t fib_engine_lookup_result_ctl;
    ds_t fib_engine_lookup_ctl;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    LCHIP_CHECK(lchip);

    sal_memset(&opf, 0, sizeof(opf));
    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));
    sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl));

    if (p_gg_ipuc_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_ipuc_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_master_t));
    if (NULL == p_gg_ipuc_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_ipuc_master[lchip], 0, sizeof(sys_ipuc_master_t));

    /* common init*/
    p_gg_ipuc_master[lchip]->tcam_mode = 1;
    p_gg_ipuc_master[lchip]->default_route_mode = p_ipuc_global_cfg->default_route_mode;
    p_gg_ipuc_master[lchip]->use_hash16 = !p_ipuc_global_cfg->use_hash8;      /* defaut use hash8 */
    p_gg_ipuc_master[lchip]->host_route_mode = 0;
    p_gg_ipuc_master[lchip]->masklen_l = p_gg_ipuc_master[lchip]->use_hash16 ? 16 : 8;
    p_gg_ipuc_master[lchip]->masklen_ipv6_l = 48; /* need open for config, can't config in tcam_mode == 1 */
    if ((p_gg_ipuc_master[lchip]->masklen_ipv6_l < 32) || (p_gg_ipuc_master[lchip]->masklen_ipv6_l > 112))
    {
        return CTC_E_INVALID_PARAM;
    }
    p_gg_ipuc_master[lchip]->info_size[CTC_IP_VER_4] = sizeof(sys_ipv4_info_t);
    p_gg_ipuc_master[lchip]->info_size[CTC_IP_VER_6] = sizeof(sys_ipv6_info_t);

    /* deal with vrfid */
    p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4] = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6] = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_6);

    p_gg_ipuc_master[lchip]->max_mask_len[CTC_IP_VER_4] = CTC_IPV4_ADDR_LEN_IN_BIT;
    p_gg_ipuc_master[lchip]->max_mask_len[CTC_IP_VER_6] = CTC_IPV6_ADDR_LEN_IN_BIT;
    p_gg_ipuc_master[lchip]->addr_len[CTC_IP_VER_4] = CTC_IPV4_ADDR_LEN_IN_BYTE;
    p_gg_ipuc_master[lchip]->addr_len[CTC_IP_VER_6] = CTC_IPV6_ADDR_LEN_IN_BYTE;
    p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] = FALSE;
    p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6] = FALSE;

    /* ipv4 UNICAST TCAM */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv440Key_t, &entry_num));
    if (entry_num > 0)
    {
        /* init l3 hash */
        CTC_ERROR_RETURN(sys_goldengate_l3_hash_init(lchip, CTC_IP_VER_4));
        /* init lpm */
        CTC_ERROR_RETURN(_sys_goldengate_lpm_init(lchip, CTC_IP_VER_4));

        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_TCAM_LOOKUP;
        p_gg_ipuc_master[lchip]->max_tcam_num[CTC_IP_VER_4] = entry_num;
    }

    /* ipv6 UNICAST TCAM */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv6160Key0_t, &entry_num));
    if (entry_num > 0)
    {
        /* init l3 hash */
        CTC_ERROR_RETURN(sys_goldengate_l3_hash_init(lchip, CTC_IP_VER_6));
        /* init lpm */
        CTC_ERROR_RETURN(_sys_goldengate_lpm_init(lchip, CTC_IP_VER_6));

        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_TCAM_LOOKUP;
        p_gg_ipuc_master[lchip]->max_tcam_num[CTC_IP_VER_6] = entry_num;
    }

    /* ipv4/ipv6 UNICAST host hash resource */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost0Ipv4HashKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_HASH_LOOKUP;
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_HASH_LOOKUP;
        p_gg_ipuc_master[lchip]->max_hash_num = entry_num;
    }

    /* ipv4/ipv6 lpm hash resource */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmLookupKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_LPM_LOOKUP;
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_LPM_LOOKUP;
    }

    /* ipv4/ipv6 NAT host hash resource */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost1Ipv4NatSaPortHashKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] += SYS_IPUC_NAT_HASH_LOOKUP;
        p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] += SYS_IPUC_NAT_HASH_LOOKUP;
        p_gg_ipuc_master[lchip]->max_snat_hash_num = entry_num;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost1Ipv4NatDaPortHashKey_t, &entry_num));
    if (entry_num > 0)
    {
        p_gg_ipuc_master[lchip]->max_napt_hash_num = entry_num;
    }

    p_gg_ipuc_master[lchip]->tcam_key_table[CTC_IP_VER_4] = DsLpmTcamIpv440Key_t;
    p_gg_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_4] = DsLpmTcamIpv4Route40Ad_t;
    p_gg_ipuc_master[lchip]->tcam_sa_table[CTC_IP_VER_4] = DsRpf_t;

    p_gg_ipuc_master[lchip]->tcam_key_table[CTC_IP_VER_6] = DsLpmTcamIpv6160Key0_t;
    p_gg_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_6] = DsLpmTcamIpv4Route160Ad_t;
    p_gg_ipuc_master[lchip]->tcam_sa_table[CTC_IP_VER_6] = DsRpf_t;

    if (p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_4] > 0)
    {
        p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] = TRUE;
    }

    if (p_gg_ipuc_master[lchip]->lookup_mode[CTC_IP_VER_6] > 0)
    {
        p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6] = TRUE;
    }

    if ((p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] + p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6]) == 0)
    {
        /* no resource for ipuc, not init ipuc */
        return CTC_E_NONE;
    }

    SYS_IPUC_CREAT_LOCK;

    CTC_ERROR_RETURN(_sys_goldengate_ipuc_db_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_rpf_init(lchip));

    /* deal with default entry*/

    for (ip_ver = CTC_IP_VER_4; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        if (!p_gg_ipuc_master[lchip]->version_en[ip_ver])
        {
            continue;
        }

        /* For the ip version disabled. */
        if (0 == p_gg_ipuc_master[lchip]->max_vrfid[ip_ver])
        {
            /* only support vrfid 0. */
            max_vrf_num = 1;
            p_gg_ipuc_master[lchip]->max_vrfid[ip_ver] = 1;
        }
        else
        {
            max_vrf_num = p_gg_ipuc_master[lchip]->max_vrfid[ip_ver];
        }

        if((CTC_IP_VER_4 == ip_ver && 0 == p_gg_ipuc_master[lchip]->default_route_mode)
            || (CTC_IP_VER_6 == ip_ver))
        {
            ret = sys_goldengate_ftm_alloc_table_offset(lchip, DsIpDa_t, 0, max_vrf_num, &offset);
            if (ret)
            {
                return CTC_E_NO_RESOURCE;
            }
        }

        p_gg_ipuc_master[lchip]->default_base[ip_ver] = offset;

        cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

        if (CTC_IP_VER_4 == ip_ver)
        {
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);

            /* ucast da default entry base */
            SetFibEngineLookupResultCtl(V, gIpv4UcastLookupResultCtl_defaultEntryBase_f, fib_engine_lookup_result_ctl, offset);
            SetFibEngineLookupResultCtl(V, gIpv4UcastLookupResultCtl_defaultEntryEn_f, fib_engine_lookup_result_ctl, p_gg_ipuc_master[lchip]->default_route_mode?0:1);

            /* ucast rpf sa default entry base */
            SetFibEngineLookupResultCtl(V, gIpv4RpfLookupResultCtl_defaultEntryBase_f, fib_engine_lookup_result_ctl, offset);
            SetFibEngineLookupResultCtl(V, gIpv4RpfLookupResultCtl_defaultEntryEn_f, fib_engine_lookup_result_ctl, p_gg_ipuc_master[lchip]->default_route_mode?0:1);

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }
        else if (CTC_IP_VER_6 == ip_ver)
        {
            cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);

            /* ucast da default entry base */
            SetFibEngineLookupResultCtl(V, gIpv6UcastLookupResultCtl_defaultEntryBase_f, fib_engine_lookup_result_ctl, offset);
            SetFibEngineLookupResultCtl(V, gIpv6UcastLookupResultCtl_defaultEntryEn_f, fib_engine_lookup_result_ctl, 1);

            /* ucast rpf sa default entry base */
            SetFibEngineLookupResultCtl(V, gIpv6RpfLookupResultCtl_defaultEntryBase_f, fib_engine_lookup_result_ctl, offset);
            SetFibEngineLookupResultCtl(V, gIpv6RpfLookupResultCtl_defaultEntryEn_f, fib_engine_lookup_result_ctl, 1);


            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
        }

        if((CTC_IP_VER_4 == ip_ver && 0 == p_gg_ipuc_master[lchip]->default_route_mode)
            || (CTC_IP_VER_6 == ip_ver))
        {
            for (vrfid = 0; vrfid < p_gg_ipuc_master[lchip]->max_vrfid[ip_ver]; vrfid++)
            {
                CTC_ERROR_RETURN(sys_goldengate_ipuc_add_default_entry(lchip, ip_ver, vrfid, SYS_NH_RESOLVED_NHID_FOR_DROP, NULL));
            }
        }
    }

    /* NAT init */
    cmd = DRV_IOR(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    SetFibEngineLookupCtl(V, ipv4NatSaPortLookupEn_f, fib_engine_lookup_ctl, 1);
    SetFibEngineLookupCtl(V, ipv4NatDaPortLookupEn_f, fib_engine_lookup_ctl, 1);

    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv4NAT160Key_t, &entry_num));
    p_gg_ipuc_master[lchip]->max_snat_tcam_num = entry_num;

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV4_NAT, 1));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_IPV4_NAT;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, entry_num-1));

    /* add nat sa default entry, use tcam default entry */
    CTC_ERROR_RETURN(sys_goldengate_ipuc_add_nat_sa_default_entry(lchip));

    /* other init */
    cmd = DRV_IOW(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    field_value = p_ipuc_global_cfg->rpf_check_port;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_IPUC_HOST,
                sys_goldengate_ipuc_host_ftm_cb);
    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_IPUC_LPM,
                sys_goldengate_ipuc_lpm_ftm_cb);

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_IPUC, sys_goldengate_ipuc_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
	  CTC_ERROR_RETURN(sys_goldengate_ipuc_wb_restore(lchip));
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_VRFID, p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4]-1));

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_reinit(uint8 lchip, uint8 use_hash8)
{

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_ipuc_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_IPV4_NAT);

    sys_goldengate_rpf_deinit(lchip);

    _sys_goldengate_ipuc_db_deinit(lchip);

    _sys_goldengate_lpm_deinit(lchip);

    sys_goldengate_l3_hash_deinit(lchip);

    sal_mutex_destroy(p_gg_ipuc_master[lchip]->mutex);
    mem_free(p_gg_ipuc_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_get_ipuc_prefix_mode(uint8 lchip)
{
    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    return !p_gg_ipuc_master[lchip]->use_hash16;
}

int32
sys_goldengate_ipuc_set_global_property(uint8 lchip, ctc_ipuc_global_property_t* p_global_prop)
{
    uint32 cmdr = 0;
    uint32 cmdwl = 0;
    uint32 cmdwr = 0;
    ds_t ipe_lookup_route_ctl;
    ds_t ipe_route_ctl;
    void* vall = NULL;
    void* valr = NULL;
    uint32 value1 = 0;
    uint32 value2 = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IPUC_DBG_FUNC();

    sal_memset(&ipe_lookup_route_ctl, 0, sizeof(ipe_lookup_route_ctl));
    sal_memset(&ipe_route_ctl, 0, sizeof(ipe_route_ctl));

    cmdr = DRV_IOR(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_lookup_route_ctl));

    cmdr = DRV_IOR(IpeRouteCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmdr, &ipe_route_ctl));

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_V4_MARTIAN_CHECK_EN))
    {
        GetIpeLookupRouteCtl(A, martianCheckEn_f, ipe_lookup_route_ctl, &value1);
        value1 &= 0x1FFC0;  /* clear low 6 bit */
        value1 |= (p_global_prop->v4_martian_check_en ? 0xF : 0);
        SetIpeLookupRouteCtl(V, martianCheckEn_f, ipe_lookup_route_ctl, value1);
        value2 = value1 ? 0 : 1;
        SetIpeLookupRouteCtl(V, martianAddressCheckDisable_f, ipe_lookup_route_ctl, value2);

        cmdwl = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
        vall = &ipe_lookup_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_V6_MARTIAN_CHECK_EN))
    {
        GetIpeLookupRouteCtl(A, martianCheckEn_f, ipe_lookup_route_ctl, &value1);
        value1 &= 0x3F;    /* clear high 11 bit */
        value1 |= (p_global_prop->v6_martian_check_en) ? 0x1BFC0 : 0;
        SetIpeLookupRouteCtl(V, martianCheckEn_f, ipe_lookup_route_ctl, value1);
        value2 = value1 ? 0 : 1;
        SetIpeLookupRouteCtl(V, martianAddressCheckDisable_f, ipe_lookup_route_ctl, value2);

        cmdwl = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
        vall = &ipe_lookup_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_MCAST_MACDA_IP_UNMATCH_CHECK))
    {
        value1 = (p_global_prop->mcast_match_check_en) ? 0 : 1;
        SetIpeRouteCtl(V, mcastAddressMatchCheckDisable_f, ipe_route_ctl, value1);

        cmdwr = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
        valr = &ipe_route_ctl;
    }

    if (CTC_FLAG_ISSET(p_global_prop->valid_flag, CTC_IP_GLB_PROP_TTL_THRESHOLD))
    {
        SetIpeRouteCtl(V, ipTtlLimit_f, ipe_route_ctl, p_global_prop->ip_ttl_threshold);

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
sys_goldengate_ipuc_arrange_fragment(uint8 lchip)
{
    sys_ipuc_info_list_t* p_info_list = NULL;
    sys_ipuc_arrange_info_t* p_arrange_info_tbl = NULL;
    sys_ipuc_arrange_info_t* p_arrange_tmp = NULL;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    uint32 head_offset = 0;
    uint32 old_offset = 0;
    uint32 new_pointer = 0;
    uint16 size = 0;
    uint8 stage = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IPUC_DBG_FUNC();

    p_info_list = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_info_list_t));
    if (NULL == p_info_list)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_info_list, 0, sizeof(sys_ipuc_info_list_t));

    SYS_IPUC_LOCK;
    /* prepare sort table */
    _sys_goldengate_ipuc_db_get_info(lchip, p_info_list);

    _sys_goldengate_ipuc_db_anylize_info(lchip, p_info_list, &p_arrange_info_tbl);

    /* start arrange by sort table */
    head_offset = 0;
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n### start to arrange SRAM \n");

    while (p_arrange_info_tbl != NULL)
    {
        stage = p_arrange_info_tbl->p_data->stage;
        p_ipuc_info = p_arrange_info_tbl->p_data->p_ipuc_info;
        p_lpm_result = p_ipuc_info->lpm_result;
        TABEL_GET_STAGE(stage, p_ipuc_info, p_table);
        if (!p_table)
        {
            SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when arrange offset\r\n");
            SYS_IPUC_UNLOCK;
            return CTC_E_UNEXPECT;
        }
        old_offset = p_table->offset;

        LPM_RLT_SET_OFFSET(p_ipuc_info, p_lpm_result, stage, p_table->offset);
        LPM_RLT_SET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage, p_table->sram_index);
        LPM_RLT_SET_IDX_MASK(p_ipuc_info, p_lpm_result, stage, p_table->idx_mask);

        LPM_INDEX_TO_SIZE(p_table->idx_mask, size);

        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_arrange_info_tbl->p_data->p_ipuc_info))
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "before arrange ipuc :%x  table[%d] ,old_offset = %d  head_offset = %d  size = %d\n",
                             p_arrange_info_tbl->p_data->p_ipuc_info->ip.ipv4[0], p_arrange_info_tbl->p_data->stage, old_offset, head_offset, size);
        }
        else
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "before arrange ipuc :%x:%x:%x:%x  table[%d] ,old_offset = %d  head_offset = %d  size = %d\n",
                             p_arrange_info_tbl->p_data->p_ipuc_info->ip.ipv6[3], p_arrange_info_tbl->p_data->p_ipuc_info->ip.ipv6[2],
                             p_arrange_info_tbl->p_data->p_ipuc_info->ip.ipv6[1], p_arrange_info_tbl->p_data->p_ipuc_info->ip.ipv6[0],
                             p_arrange_info_tbl->p_data->stage, old_offset, head_offset, size);
        }

        if (old_offset <= head_offset)
        {
            if (p_arrange_info_tbl->p_data)
            {
                mem_free(p_arrange_info_tbl->p_data);
                p_arrange_info_tbl->p_data = NULL;
                p_arrange_tmp = p_arrange_info_tbl->p_next_info;
                mem_free(p_arrange_info_tbl);
            }

            p_arrange_info_tbl = p_arrange_tmp;
            if (old_offset == head_offset)
            {
                head_offset += size;
            }

            continue;
        }

        if ((old_offset - head_offset) >= size)
        {
            CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_lpm_offset_alloc(lchip, stage, size, &new_pointer));
            new_pointer += (stage << POINTER_OFFSET_BITS_LEN);
            CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_lpm_update(lchip, p_ipuc_info, stage, new_pointer,
                                                                        (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));
        }
        else
        {
            new_pointer = _sys_goldengate_lpm_get_rsv_offset(lchip, stage);
            new_pointer += (stage << POINTER_OFFSET_BITS_LEN);

            CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_lpm_update(lchip, p_ipuc_info, stage, new_pointer,
                                                                        (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));

            old_offset = new_pointer;
            CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_lpm_offset_alloc(lchip, stage, size, &new_pointer));
            new_pointer += (stage << POINTER_OFFSET_BITS_LEN);
            CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_lpm_update(lchip, p_ipuc_info, stage, new_pointer, old_offset));
        }

        if (p_arrange_info_tbl->p_data)
        {
            mem_free(p_arrange_info_tbl->p_data);
            p_arrange_info_tbl->p_data = NULL;
            p_arrange_tmp = p_arrange_info_tbl->p_next_info;
            mem_free(p_arrange_info_tbl);
        }

        p_arrange_info_tbl = p_arrange_tmp;
        head_offset += size;
    }

    SYS_IPUC_UNLOCK;
    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_default_entry_en(uint8 lchip, uint32 enable)
{
    uint32 cmd = 0;

    SYS_IPUC_INIT_CHECK(lchip);

    p_gg_ipuc_master[lchip]->default_route_mode = enable? 0 : 1;
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv4UcastLookupResultCtl_defaultEntryEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &enable));
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_gIpv6UcastLookupResultCtl_defaultEntryEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &enable));

    return CTC_E_NONE;
}

/* when mask length is 32 for IPv4 or 128 for IPv6, 0: add the key to hash table,in this mode can not enable rpf-check,
1: add the key to tcam/lpm*/
int32
sys_goldengate_ipuc_set_host_route_mode(uint8 lchip, uint8 host_route_mode)
{
    SYS_IPUC_INIT_CHECK(lchip);
    p_gg_ipuc_master[lchip]->host_route_mode = host_route_mode;

    return CTC_E_NONE;
}


#define __1_SHOW__

int32
sys_goldengate_ipuc_state_show(uint8 lchip)
{
    SYS_IPUC_INIT_CHECK(lchip);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------SPEC Resource Usage---------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%s\n", "TYPE", " ", "SPEC", " ", "COUNT");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "HOST Route", " ", SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_HOST));
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%d\n", "--IPv4", " ", "- ", " ", p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%d\n", "--IPv6", " ", "-", " ", p_gg_ipuc_master[lchip]->host_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "LPM Route", " ", SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPUC_LPM));
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%d\n", "--IPv4", " ", "-", " ", p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%d\n", "--IPv6", " ", "-", " ", p_gg_ipuc_master[lchip]->lpm_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Memory Usage----------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%-15s%-4s%-15s\n", "TYPE", " ", "SIZE", " ", "Entries", " ", "Usage");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "Hash", " ", p_gg_ipuc_master[lchip]->max_hash_num);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--IPv4", " ", "-", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15d\n", p_gg_ipuc_master[lchip]->hash_counter[CTC_IP_VER_4], " ", p_gg_ipuc_master[lchip]->hash_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--IPv6", " ", "-", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15d\n", p_gg_ipuc_master[lchip]->hash_counter[CTC_IP_VER_6], " ", p_gg_ipuc_master[lchip]->hash_counter[CTC_IP_VER_6] * 2);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s\n", "Tcam", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s", "--IPv4", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s",  p_gg_ipuc_master[lchip]->max_tcam_num[CTC_IP_VER_4], " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15d\n", p_gg_ipuc_db_master[lchip]->tcam_ip_count[CTC_IP_VER_4], " ", p_gg_ipuc_db_master[lchip]->tcam_ip_count[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s", "--IPv6", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s",  p_gg_ipuc_master[lchip]->max_tcam_num[CTC_IP_VER_6], " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15d\n", p_gg_ipuc_db_master[lchip]->tcam_ip_count[CTC_IP_VER_6], " ", p_gg_ipuc_db_master[lchip]->tcam_ip_count[CTC_IP_VER_6] * 4);

    sys_goldengate_lpm_state_show(lchip);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Hash Conflict Status--------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-10s\n", "TYPE", " ", "COUNT");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s\n", "In TCAM");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "--IPv4", " ", p_gg_ipuc_master[lchip]->conflict_tcam_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "--IPv6", " ", p_gg_ipuc_master[lchip]->conflict_tcam_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s\n", "In ALPM");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "--IPv4", " ", p_gg_ipuc_master[lchip]->conflict_alpm_counter[CTC_IP_VER_4]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "--IPv6", " ", p_gg_ipuc_master[lchip]->conflict_alpm_counter[CTC_IP_VER_6]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------Nat Status------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s%s\n", "TYPE", " ", "SIZE", " ", "COUNT");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s", "IPV4 SNAT Tcam", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s",  p_gg_ipuc_master[lchip]->max_snat_tcam_num, " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d\n", p_gg_ipuc_master[lchip]->snat_tcam_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s", "IPV4 SNAT Hash", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s",  p_gg_ipuc_master[lchip]->max_snat_hash_num, " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d\n", p_gg_ipuc_master[lchip]->snat_hash_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s", "IPV4 NAPT Hash", " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s",  p_gg_ipuc_master[lchip]->max_napt_hash_num, " ");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%d\n", p_gg_ipuc_master[lchip]->napt_hash_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------------------------------------\n");

    return CTC_E_NONE;
}

STATIC uint8
_sys_goldengate_ipuc_mask_to_index(uint8 lchip, uint8 ip, uint8 index_mask)
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
_sys_goldengate_ipuc_retrieve_lpm_lookup_key(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info, sys_ipuc_debug_info_t* debug_info)
{
    uint8 sram_index = 0;
    uint32 stage = 0;
    sys_lpm_tbl_t* p_table = NULL;
    uint8 masklen = 0;
    uint8  bucket = 0;
    uint8  index = 0;
    uint32 tmp_masklen = 0;

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
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

        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info))
        {
            masklen = ((stage+1) * 8) + p_gg_ipuc_master[lchip]->masklen_l;
            index = p_gg_ipuc_master[lchip]->use_hash16 ? ((p_sys_ipuc_info->ip.ipv4[0] >> ((1-stage) * 8)) & 0xFF)
                : ((p_sys_ipuc_info->ip.ipv4[0] >> ((2-stage) * 8)) & 0xFF);
        }
        else
        {
            tmp_masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
            masklen = ((stage+1) * 8) + tmp_masklen;
            index = (p_sys_ipuc_info->ip.ipv6[2] >> ((1-stage) * 8)) & 0xFF;
        }

        bucket = _sys_goldengate_ipuc_mask_to_index(lchip, index, p_table->idx_mask);
        sram_index = p_table->sram_index;
        debug_info->lpm_key_index[sram_index] = p_table->offset + bucket;

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
_sys_goldengate_ipuc_retrieve_hash_key(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info,
                                      sys_ipuc_hash_type_t sys_ipuc_hash_type,
                                      sys_ipuc_debug_info_t* debug_info)
{
    sys_l3_hash_t* p_hash = NULL;

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_sys_ipuc_info, &p_hash);
    if (!p_hash)
    {
        return CTC_E_UNEXPECT;
    }

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info))
    {
        if (p_sys_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_l)
        {
            debug_info->tcam_index = p_hash->hash_offset[LPM_TYPE_POINTER];
            debug_info->hash_high_key = LPM_TYPE_POINTER;
        }
        else
        {
            debug_info->tcam_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            debug_info->hash_high_key = LPM_TYPE_NEXTHOP;
        }
    }
    else
    {
        if (p_sys_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l)
        {
            debug_info->tcam_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            debug_info->hash_high_key = LPM_TYPE_NEXTHOP;
        }
        else
        {
            debug_info->tcam_index = p_hash->hash_offset[LPM_TYPE_POINTER];
            debug_info->hash_high_key = LPM_TYPE_POINTER;
        }
    }

    return DRV_E_NONE;
}

int32
sys_goldengate_ipuc_retrieve_ip(uint8 lchip, sys_ipuc_info_t* p_sys_ipuc_info)
{
    sys_ipuc_debug_info_t debug_info;
    char str1[10];
    uint32 stage = 0;
    char  flag[4]  = {'\0'};
    uint8 flag_idx = 0;
     uint32 tcam_index = 0;

    SYS_IPUC_INIT_CHECK(lchip);

    sal_memset(&debug_info, 0, sizeof(debug_info));
    debug_info.hash_high_key = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX0] = LPM_TYPE_NUM;
    debug_info.lpm_key[LPM_TABLE_INDEX1] = LPM_TYPE_NUM;

    if (DO_HASH == p_sys_ipuc_info->route_opt)
    {
        debug_info.host_hash_index = p_sys_ipuc_info->key_offset;
        debug_info.in_host_hash = TRUE;
    }
    else
    {
        if (DO_TCAM == p_sys_ipuc_info->route_opt)
        {
            debug_info.tcam_index = p_sys_ipuc_info->key_offset;
        }
        else if (p_sys_ipuc_info->route_opt == DO_LPM)
        {
            CTC_ERROR_RETURN(_sys_goldengate_ipuc_retrieve_hash_key(lchip, p_sys_ipuc_info, 0, &debug_info));

            if (((CTC_IP_VER_6 == SYS_IPUC_VER(p_sys_ipuc_info))
                && (!(p_sys_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l)))
                || ((CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info))
                    && (p_sys_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_l)))
            {
                CTC_ERROR_RETURN(_sys_goldengate_ipuc_retrieve_lpm_lookup_key(lchip, p_sys_ipuc_info, &debug_info));
            }
        }
        else
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nDetail Table offset\n");
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------\n");
            goto DISPLAY_AD;
        }
    }

    str1[0] = '\0';
    if (!debug_info.in_host_hash)
    {
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
        }
        else
        {
            sal_strncat(str1, "O", 1);
        }
    }
    else
    {
        sal_strncat(str1, "N ", 2);
    }
    flag[flag_idx++] = debug_info.in_host_hash ? 'O' : 'N';
    flag[flag_idx++] = str1[0];
#if 0 /* no display */
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nO: nexthop, P: pointer, N: none\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "---------------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Type       Host_hash_key     Tcam_key       Lpm_key0       Lpm_key1       Ad_index\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "---------------------------------------------------------------------------------------\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Index      %-18d%-15d%-15d%-15d%-d\n",
                                           debug_info.host_hash_index,
                                           debug_info.tcam_index,
                                           debug_info.lpm_key_index[0],
                                           debug_info.lpm_key_index[1],
                                           p_sys_ipuc_info->ad_offset);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Indicate   %-18s%-15s", (debug_info.in_host_hash ? "O" : "N"), str1);
#endif
    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX ; stage ++)
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
        flag[flag_idx++] = str1[0];
    }
#if 0
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s", str1);
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n---------------------------------------------------------------------------------------\n");
#endif
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nDetail Table offset\n");
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------\n");

    /* host table */
    switch(flag[0])
    {
        case 'O':
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-25s:%d\n",
                                CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info) ? "DsFibHost0Ipv4HashKey" : "DsFibHost0Ipv6UcastHashKey",
                                debug_info.host_hash_index);
            break;
        case 'N':
        default:
            break;
    }

    /* TCAM table */
    switch(flag[1])
    {
        case 'P':
        case 'O':
            if ((CTC_IP_VER_6 == SYS_IPUC_VER(p_sys_ipuc_info))
                && p_gg_ipuc_db_master[lchip]->tcam_share_mode)
            {
                tcam_index = debug_info.tcam_index/SYS_IPUC_TCAM_6TO4_STEP;
            }
            else
            {
                tcam_index = debug_info.tcam_index;
            }
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-25s:%d\n",
                                CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info) ? "DsLpmTcamIpv440Key" : "DsLpmTcamIpv6160Key0",
                                tcam_index);
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-25s:%d\n",
                                CTC_IP_VER_4 == SYS_IPUC_VER(p_sys_ipuc_info) ? "DsLpmTcamIpv4Route40Ad" : "DsLpmTcamIpv4Route160Ad",
                                debug_info.tcam_index);
            break;
        case 'N':
            break;
        default:
            break;
    }

    /* LPM table */
    for(flag_idx = 0; flag_idx < 2; flag_idx++)
    {
        switch(flag[flag_idx + 2])
        {
            case 'P':
            case 'O':
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-25s:%d (pipeline%d)\n","DsLpmLookupKey",
                                    debug_info.lpm_key_index[flag_idx],
                                    flag_idx);
                break;
            case 'N':
                break;
            default:
                break;
        }
    }

DISPLAY_AD:
    /* ad table */
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-25s:%d\n","DsIpDa",p_sys_ipuc_info->ad_offset);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------\n");

    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipuc_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, sys_ipuc_info_t* p_ipuc_data,uint32 detail);
int32
sys_goldengate_ipuc_show_debug_info(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param_info)
{
    sys_ipuc_info_t ipuc_data;
    sys_ipuc_info_t* p_ipuc_data = NULL;
    ctc_ipuc_param_t *p_ipuc_param = NULL;
    ctc_ipuc_param_t ipuc_param_tmp = {0};

    SYS_IPUC_INIT_CHECK(lchip);
    p_ipuc_param = &ipuc_param_tmp;
    sal_memcpy(p_ipuc_param,p_ipuc_param_info,sizeof(ctc_ipuc_param_t));
    CTC_PTR_VALID_CHECK(p_ipuc_param);
    CTC_IP_VER_CHECK(p_ipuc_param->ip_ver);
    SYS_IP_CHECK_VERSION_ENABLE(p_ipuc_param->ip_ver);
    SYS_IPUC_MASK_LEN_CHECK(p_ipuc_param->ip_ver, p_ipuc_param->masklen);
    SYS_IP_ADDRESS_SORT(p_ipuc_param);
    SYS_IP_ADDR_MASK(p_ipuc_param->ip, p_ipuc_param->masklen, p_ipuc_param->ip_ver);
    SYS_IP_FUNC_DBG_DUMP(p_ipuc_param);

    /* prepare data */
    sal_memset(&ipuc_data, 0, sizeof(sys_ipuc_info_t));
    p_ipuc_data = &ipuc_data;
    SYS_IPUC_KEY_MAP(p_ipuc_param, p_ipuc_data);

    SYS_IPUC_LOCK;
    /* lookup for ipuc entrise */
    CTC_ERROR_RETURN_IPUC_UNLOCK(_sys_goldengate_ipuc_db_lookup(lchip, &p_ipuc_data));

    if (!p_ipuc_data)
    {
        CTC_RETURN_IPUC_UNLOCK(CTC_E_ENTRY_NOT_EXIST);
    }

    CTC_ERROR_RETURN_IPUC_UNLOCK(sys_goldengate_ipuc_db_show(lchip, SYS_IPUC_VER(p_ipuc_data),p_ipuc_data,1));

    SYS_IPUC_UNLOCK;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_wb_mapping_info(uint8 lchip, sys_wb_ipuc_info_t *p_wb_ipuc_info, sys_ipuc_info_t *p_ipuc_info, sys_wb_lpm_info_t *lpm_info, uint8 sync)
{
    uint32 loop = 0;
    sys_l3_hash_t *p_hash;
    sys_lpm_tbl_t *p_table;

    if (sync)
    {
        p_wb_ipuc_info->ip_ver = SYS_IPUC_VER(p_ipuc_info);
        p_wb_ipuc_info->masklen = p_ipuc_info->masklen;
        p_wb_ipuc_info->vrf_id = p_ipuc_info->vrf_id;
        sal_memcpy(p_wb_ipuc_info->ip, &(p_ipuc_info)->ip, p_gg_ipuc_master[lchip]->addr_len[(p_wb_ipuc_info)->ip_ver]);
        if (p_wb_ipuc_info->ip_ver == CTC_IP_VER_4)
        {
            sal_memset(&p_wb_ipuc_info->ip[1], 0, sizeof(uint32) * 3);
        }
        p_wb_ipuc_info->nh_id = p_ipuc_info->nh_id;
        p_wb_ipuc_info->ad_offset = p_ipuc_info->ad_offset;
        p_wb_ipuc_info->key_offset = p_ipuc_info->key_offset;
        p_wb_ipuc_info->l3if = p_ipuc_info->l3if;
        p_wb_ipuc_info->route_flag = p_ipuc_info->route_flag;
        p_wb_ipuc_info->conflict_flag = p_ipuc_info->conflict_flag;
        p_wb_ipuc_info->rpf_mode = p_ipuc_info->rpf_mode;
        p_wb_ipuc_info->l4_dst_port = p_ipuc_info->l4_dst_port;
        p_wb_ipuc_info->gport = p_ipuc_info->gport;
        p_wb_ipuc_info->route_opt = p_ipuc_info->route_opt;



        if (DO_LPM != p_ipuc_info->route_opt)
        {
            sal_memset(&p_wb_ipuc_info->lpm_info, 0, sizeof(sys_wb_lpm_info_t));

            return CTC_E_NONE;
        }

        _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
        if (!p_hash)
        {
            return CTC_E_UNEXPECT;
        }

        for (loop = LPM_TABLE_INDEX0; loop < LPM_TABLE_MAX; loop++)
        {
            TABEL_GET_STAGE(loop, p_ipuc_info, p_table);
            if (p_table)
            {
                p_wb_ipuc_info->lpm_info.idx_mask[loop] = p_table->idx_mask;
                p_wb_ipuc_info->lpm_info.offset[loop] = p_table->offset;
            }
            else
            {
                p_wb_ipuc_info->lpm_info.idx_mask[loop] = 0xFF;
                p_wb_ipuc_info->lpm_info.offset[loop] = INVALID_POINTER_OFFSET;
            }
        }

        p_wb_ipuc_info->lpm_info.masklen_pushed = p_hash->masklen_pushed;
        p_wb_ipuc_info->lpm_info.is_mask_valid = p_hash->is_mask_valid;
        p_wb_ipuc_info->lpm_info.is_pushed = p_hash->is_pushed;
        p_wb_ipuc_info->lpm_info.rsv = 0;
    }
    else
    {
        p_ipuc_info->nh_id = p_wb_ipuc_info->nh_id;
        p_ipuc_info->ad_offset = p_wb_ipuc_info->ad_offset;
        p_ipuc_info->lpm_result = NULL;
        p_ipuc_info->key_offset = p_wb_ipuc_info->key_offset;
        p_ipuc_info->l3if = p_wb_ipuc_info->l3if;
        p_ipuc_info->vrf_id = p_wb_ipuc_info->vrf_id;
        p_ipuc_info->is_exist = 0;
        p_ipuc_info->route_flag = p_wb_ipuc_info->route_flag;
        p_ipuc_info->masklen = p_wb_ipuc_info->masklen;
        p_ipuc_info->conflict_flag = p_wb_ipuc_info->conflict_flag;
        p_ipuc_info->rpf_mode = p_wb_ipuc_info->rpf_mode;
        p_ipuc_info->l4_dst_port = p_wb_ipuc_info->l4_dst_port;
        p_ipuc_info->gport = p_wb_ipuc_info->gport;
        p_ipuc_info->route_opt = p_wb_ipuc_info->route_opt;
        sal_memcpy(&(p_ipuc_info)->ip, &(p_wb_ipuc_info)->ip, p_gg_ipuc_master[lchip]->addr_len[(p_wb_ipuc_info)->ip_ver]);

        if (CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK) && p_ipuc_info->rpf_mode == SYS_RPF_CHK_MODE_PROFILE
            && p_ipuc_info->l3if != SYS_RPF_INVALID_PROFILE_ID)
        {
            CTC_ERROR_RETURN(sys_goldengate_rpf_restore_profile(lchip, p_ipuc_info->l3if));
        }

        if (DO_TCAM == p_ipuc_info->route_opt)
        {
            if (p_gg_ipuc_master[lchip]->tcam_mode)
            {
                p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->key_offset] = SYS_IPUC_TCAM_FLAG_IPUC_INFO;
                SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->key_offset) = p_ipuc_info;
            }
            else
            {
                p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->key_offset].type = SYS_IPUC_TCAM_FLAG_IPUC_INFO;
                p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->key_offset].p_info = p_ipuc_info;
            }
        }

        if (DO_LPM != p_ipuc_info->route_opt)
        {
            return CTC_E_NONE;
        }

        for (loop = LPM_TABLE_INDEX0; loop < LPM_TABLE_MAX; loop++)
        {
            lpm_info->idx_mask[loop] = p_wb_ipuc_info->lpm_info.idx_mask[loop];
            lpm_info->offset[loop] = p_wb_ipuc_info->lpm_info.offset[loop];
        }

        lpm_info->masklen_pushed = p_wb_ipuc_info->lpm_info.masklen_pushed;
        lpm_info->is_mask_valid = p_wb_ipuc_info->lpm_info.is_mask_valid;
        lpm_info->is_pushed = p_wb_ipuc_info->lpm_info.is_pushed;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_wb_mapping_natsa(uint8 lchip, sys_wb_ipuc_nat_sa_info_t *p_wb_natsa_info, sys_ipuc_nat_sa_info_t *p_natsa_info, uint8 sync)
{
    uint32 table_id = DsIpDa_t;
    sys_goldengate_opf_t opf = {0};

    if (sync)
    {
        sal_memcpy(p_wb_natsa_info->ipv4, p_natsa_info->ipv4, sizeof(ipv4_addr_t));
        sal_memcpy(p_wb_natsa_info->new_ipv4, p_natsa_info->new_ipv4, sizeof(ipv4_addr_t));
        p_wb_natsa_info->l4_src_port = p_natsa_info->l4_src_port;
        p_wb_natsa_info->vrf_id = p_natsa_info->vrf_id;
        p_wb_natsa_info->is_tcp_port = p_natsa_info->is_tcp_port;
        p_wb_natsa_info->ad_offset = p_natsa_info->ad_offset;
        p_wb_natsa_info->key_offset = p_natsa_info->key_offset;
        p_wb_natsa_info->new_l4_src_port = p_natsa_info->new_l4_src_port;
        p_wb_natsa_info->in_tcam = p_natsa_info->in_tcam;
    }
    else
    {
        sal_memcpy(p_natsa_info->ipv4, p_wb_natsa_info->ipv4, sizeof(ipv4_addr_t));
        sal_memcpy(p_natsa_info->new_ipv4, p_wb_natsa_info->new_ipv4, sizeof(ipv4_addr_t));
        p_natsa_info->l4_src_port = p_wb_natsa_info->l4_src_port;
        p_natsa_info->vrf_id = p_wb_natsa_info->vrf_id;
        p_natsa_info->is_tcp_port = p_wb_natsa_info->is_tcp_port;
        p_natsa_info->ad_offset = p_wb_natsa_info->ad_offset;
        p_natsa_info->key_offset = p_wb_natsa_info->key_offset;
        p_natsa_info->new_l4_src_port = p_wb_natsa_info->new_l4_src_port;
        p_natsa_info->in_tcam = p_wb_natsa_info->in_tcam;

        if (p_natsa_info->in_tcam)
        {
            opf.pool_index = 0;
            opf.pool_type = OPF_IPV4_NAT;

            CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_natsa_info->key_offset));
        }

        return sys_goldengate_ftm_alloc_table_offset_from_position(lchip, table_id, 0, 1, p_natsa_info->ad_offset);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_wb_sync_info_func(sys_ipuc_info_t *p_ipuc_info, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_ipuc_info_t  *p_wb_ipuc_info;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_ipuc_info = (sys_wb_ipuc_info_t *)wb_data->buffer + wb_data->valid_cnt;

    CTC_ERROR_RETURN(_sys_goldengate_ipuc_wb_mapping_info(lchip, p_wb_ipuc_info, p_ipuc_info, NULL, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_wb_sync_natsa_func(sys_ipuc_nat_sa_info_t *p_natsa_info, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_ipuc_nat_sa_info_t  *p_wb_natsa_info;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_natsa_info = (sys_wb_ipuc_nat_sa_info_t *)wb_data->buffer + wb_data->valid_cnt;

    CTC_ERROR_RETURN(_sys_goldengate_ipuc_wb_mapping_natsa(lchip, p_wb_natsa_info, p_natsa_info, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_wb_sync(uint8 lchip)
{
    uint32 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t ipuc_hash_data;
    sys_wb_ipuc_master_t  *p_wb_ipuc_master;

    /*syncup ipuc matser*/
    wb_data.buffer = mem_malloc(MEM_IPUC_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_ipuc_master_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_MASTER);

    p_wb_ipuc_master = (sys_wb_ipuc_master_t  *)wb_data.buffer;

    p_wb_ipuc_master->lchip = lchip;
    p_wb_ipuc_master->tcam_mode = p_gg_ipuc_master[lchip]->tcam_mode;
    p_wb_ipuc_master->napt_hash_counter = p_gg_ipuc_master[lchip]->napt_hash_counter ;
    p_wb_ipuc_master->snat_hash_counter = p_gg_ipuc_master[lchip]->snat_hash_counter ;
    p_wb_ipuc_master->snat_tcam_counter = p_gg_ipuc_master[lchip]->snat_tcam_counter ;

    for ( loop = 0; loop < MAX_CTC_IP_VER; loop++ )
    {
        p_wb_ipuc_master->host_counter[loop] = p_gg_ipuc_master[lchip]->host_counter[loop] ;
        p_wb_ipuc_master->lpm_counter[loop] = p_gg_ipuc_master[lchip]->lpm_counter[loop] ;
        p_wb_ipuc_master->hash_counter[loop] = p_gg_ipuc_master[lchip]->hash_counter[loop] ;
        p_wb_ipuc_master->tcam_ip_count[loop] = p_gg_ipuc_db_master[lchip]->tcam_ip_count[loop];
        p_wb_ipuc_master->alpm_counter[loop] = p_gg_ipuc_master[lchip]->alpm_counter[loop] ;
        p_wb_ipuc_master->conflict_tcam_counter[loop] = p_gg_ipuc_master[lchip]->conflict_tcam_counter[loop] ;
        p_wb_ipuc_master->conflict_alpm_counter[loop] = p_gg_ipuc_master[lchip]->conflict_alpm_counter[loop] ;
    }

    for ( loop = 0; loop < LPM_TABLE_MAX; loop++)
    {
        p_wb_ipuc_master->alpm_usage[loop] = p_gg_lpm_master[lchip]->alpm_usage[loop];
    }

    for ( loop = 0; loop <= CTC_IPV4_ADDR_LEN_IN_BIT; loop++ )
    {
        p_wb_ipuc_master->ipv4_block_left[loop] = p_gg_ipuc_db_master[lchip]->ipv4_blocks[loop].all_left_b ;
        p_wb_ipuc_master->ipv4_block_right[loop] = p_gg_ipuc_db_master[lchip]->ipv4_blocks[loop].all_right_b ;
    }

    for ( loop = 0; loop <= CTC_IPV6_ADDR_LEN_IN_BIT; loop++ )
    {
        p_wb_ipuc_master->ipv6_block_left[loop] = p_gg_ipuc_db_master[lchip]->ipv6_blocks[loop].all_left_b ;
        p_wb_ipuc_master->ipv6_block_right[loop] = p_gg_ipuc_db_master[lchip]->ipv6_blocks[loop].all_right_b ;
    }

    for ( loop = 0; loop < (CTC_IPV4_ADDR_LEN_IN_BIT+CTC_IPV6_ADDR_LEN_IN_BIT); loop++ )
    {
        p_wb_ipuc_master->share_block_left[loop] = p_gg_ipuc_db_master[lchip]->share_blocks[loop].all_left_b ;
        p_wb_ipuc_master->share_block_right[loop] = p_gg_ipuc_db_master[lchip]->share_blocks[loop].all_right_b ;
    }

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup ipuc_info*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_ipuc_info_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_INFO);
    ipuc_hash_data.data = &wb_data;
    ipuc_hash_data.value1 = lchip;

    for (loop = CTC_IP_VER_4; loop < MAX_CTC_IP_VER; loop++)
    {
        wb_data.valid_cnt = 0;
        CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_ipuc_db_master[lchip]->ipuc_hash[loop], (hash_traversal_fn) _sys_goldengate_ipuc_wb_sync_info_func, (void *)&ipuc_hash_data), ret, done);
        if (wb_data.valid_cnt > 0)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }

     /*syncup natsa_info*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_ipuc_nat_sa_info_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_NAT_SA_INFO);
    ipuc_hash_data.data = &wb_data;
    ipuc_hash_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, (hash_traversal_fn) _sys_goldengate_ipuc_wb_sync_natsa_func, (void *)&ipuc_hash_data), ret, done);
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
sys_goldengate_ipuc_wb_restore(uint8 lchip)
{
    uint16 loop = 0;
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    uint32 table_size = 0;
    uint32 obsolete_flag = 0;
    uint32 ipv6_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint8 ipv4_block_num = 0;
    uint8 ip_ver = 0;
    sys_goldengate_opf_t opf;
    ctc_wb_query_t wb_query;
    sys_wb_ipuc_master_t  *p_wb_ipuc_master;
    sys_ipuc_info_t *p_ipuc_info;
    sys_wb_ipuc_info_t  *p_wb_ipuc_info;
    sys_wb_lpm_info_t   lpm_info;
    sys_sort_block_init_info_t init_info;
    sys_ipuc_ad_spool_t* p_ipuc_ad_new = NULL;
    sys_ipuc_ad_spool_t* p_ipuc_ad_get = NULL;
    sys_ipuc_nat_sa_info_t* p_natsa_info = NULL;
    sys_wb_ipuc_nat_sa_info_t* p_wb_natsa_info = NULL;

    wb_query.buffer = mem_malloc(MEM_IPUC_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_ipuc_master_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  ipuc_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query ipuc master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_ipuc_master = (sys_wb_ipuc_master_t  *)wb_query.buffer;

    if (p_gg_ipuc_master[lchip]->tcam_mode !=  p_wb_ipuc_master->tcam_mode)
    {
        ret = CTC_E_UNEXPECT;
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "tcam mode %u is not equal restore tcam mode %u!!\n",
            p_gg_ipuc_master[lchip]->tcam_mode, p_wb_ipuc_master->tcam_mode);

        goto done;
    }

    p_gg_ipuc_master[lchip]->napt_hash_counter = p_wb_ipuc_master->napt_hash_counter ;
    p_gg_ipuc_master[lchip]->snat_hash_counter = p_wb_ipuc_master->snat_hash_counter ;
    p_gg_ipuc_master[lchip]->snat_tcam_counter = p_wb_ipuc_master->snat_tcam_counter ;

    for ( loop = 0; loop < MAX_CTC_IP_VER; loop++ )
    {
        p_gg_ipuc_master[lchip]->host_counter[loop] = p_wb_ipuc_master->host_counter[loop] ;
        p_gg_ipuc_master[lchip]->lpm_counter[loop] = p_wb_ipuc_master->lpm_counter[loop] ;
        p_gg_ipuc_master[lchip]->hash_counter[loop] = p_wb_ipuc_master->hash_counter[loop] ;
        p_gg_ipuc_db_master[lchip]->tcam_ip_count[loop] = p_wb_ipuc_master->tcam_ip_count[loop];
        p_gg_ipuc_master[lchip]->alpm_counter[loop] =  p_wb_ipuc_master->alpm_counter[loop];
        p_gg_ipuc_master[lchip]->conflict_tcam_counter[loop] = p_wb_ipuc_master->conflict_tcam_counter[loop] ;
        p_gg_ipuc_master[lchip]->conflict_alpm_counter[loop] = p_wb_ipuc_master->conflict_alpm_counter[loop] ;
    }

    for ( loop = 0; loop < LPM_TABLE_MAX; loop++)
    {
        p_gg_lpm_master[lchip]->alpm_usage[loop] = p_wb_ipuc_master->alpm_usage[loop];
    }

    if (p_gg_ipuc_master[lchip]->tcam_mode && (0 == p_gg_ipuc_db_master[lchip]->tcam_share_mode))
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        init_info.user_data_of = NULL;
        init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;
        init_info.opf = &opf;

        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv440Key_t, &table_size));
        if (table_size <= 0)
        {
            return CTC_E_UNEXPECT;
        }

        opf.pool_type = OPF_IPV4_UC_BLOCK;
        init_info.max_offset_num = table_size;

        for ( loop = 0; loop <= CTC_IPV4_ADDR_LEN_IN_BIT; loop++ )
        {
            if (p_wb_ipuc_master->ipv4_block_right[loop] == 0)
            {
                break;
            }
            sys_goldengate_opf_free(lchip, opf.pool_type, loop);

            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[loop];
            init_info.boundary_l = p_wb_ipuc_master->ipv4_block_left[loop];
            init_info.boundary_r = p_wb_ipuc_master->ipv4_block_right[loop];
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = loop;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }

        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv6160Key0_t, &table_size));
        if (table_size <= 0)
        {
            return CTC_E_UNEXPECT;
        }

        opf.pool_type = OPF_IPV6_UC_BLOCK;
        init_info.max_offset_num = table_size;

        for ( loop = 0; loop <= CTC_IPV6_ADDR_LEN_IN_BIT; loop++ )
        {
            if (p_wb_ipuc_master->ipv6_block_right[loop] == 0)
            {
                break;
            }
            sys_goldengate_opf_free(lchip, opf.pool_type, loop);

            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv6_blocks[loop];
            init_info.boundary_l = p_wb_ipuc_master->ipv6_block_left[loop];
            init_info.boundary_r = p_wb_ipuc_master->ipv6_block_right[loop];
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = loop;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }
    }
    else if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        init_info.user_data_of = NULL;
        init_info.dir = SYS_SORT_BLOCK_DIR_UP;
        init_info.opf = &opf;

        CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 0, &ipv4_table_size));
        ipv4_table_size = ipv4_table_size*2;
        if (ipv4_table_size <= 0)
        {
            return CTC_E_UNEXPECT;
        }
        CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 1, &ipv6_table_size));
        ipv6_table_size = ipv6_table_size*2;
        if (ipv6_table_size <= 0)
        {
            return CTC_E_UNEXPECT;
        }

        opf.pool_type = OPF_IPV4_UC_BLOCK;
        init_info.max_offset_num = ipv4_table_size + ipv6_table_size;
        ipv4_block_num = (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);

        for ( loop = 0; loop < ipv4_block_num; loop++ )
        {
            if (p_wb_ipuc_master->share_block_right[loop] == 0)
            {
                break;
            }
            sys_goldengate_opf_free(lchip, opf.pool_type, loop);

            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[loop];
            init_info.boundary_l = p_wb_ipuc_master->share_block_left[loop];
            init_info.boundary_r = p_wb_ipuc_master->share_block_right[loop];
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = loop;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }

        init_info.multiple = 4;
        init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

        for ( loop = ipv4_block_num; loop < 112; loop++ )
        {
            if (p_wb_ipuc_master->share_block_right[loop] == 0)
            {
                break;
            }
            sys_goldengate_opf_free(lchip, opf.pool_type, loop);

            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[loop];
            init_info.boundary_l = p_wb_ipuc_master->share_block_left[loop];
            init_info.boundary_r = p_wb_ipuc_master->share_block_right[loop];
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = loop;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }

    }

    /*restore  ipuc_info*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_ipuc_info_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_INFO);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_ipuc_info = (sys_wb_ipuc_info_t *)wb_query.buffer + entry_cnt++;

        p_ipuc_info = mem_malloc(MEM_IPUC_MODULE,  p_gg_ipuc_master[lchip]->info_size[(p_wb_ipuc_info)->ip_ver]);
        if (NULL == p_ipuc_info)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_ipuc_info, 0, p_gg_ipuc_master[lchip]->info_size[(p_wb_ipuc_info)->ip_ver]);

        ret = _sys_goldengate_ipuc_wb_mapping_info(lchip, p_wb_ipuc_info, p_ipuc_info, &lpm_info, 0);
        if (ret)
        {
            continue;
        }

        /*add ad spool*/
        p_ipuc_ad_new = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_ad_spool_t));
        if (NULL == p_ipuc_ad_new)
        {
            ret = CTC_E_NO_MEMORY;

            continue;
        }

        sal_memset(p_ipuc_ad_new, 0, sizeof(sys_ipuc_ad_spool_t));
        p_ipuc_ad_new->nhid = p_ipuc_info->nh_id;
        obsolete_flag = CTC_IPUC_FLAG_NEIGHBOR | CTC_IPUC_FLAG_CONNECT | CTC_IPUC_FLAG_STATS_EN | CTC_IPUC_FLAG_AGING_EN
                               | SYS_IPUC_FLAG_MERGE_KEY | SYS_IPUC_FLAG_IS_BIND_NH;
        p_ipuc_ad_new->route_flag = p_ipuc_info->route_flag & (~obsolete_flag);
        p_ipuc_ad_new->l3if = p_ipuc_info->l3if;
        p_ipuc_ad_new->rpf_mode = p_ipuc_info->rpf_mode;
        p_ipuc_ad_new->gport = p_ipuc_info->gport;
        if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH))
        {
            p_ipuc_ad_new->binding_nh  =1;
        }

        ret = ctc_spool_add(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_new, NULL,&p_ipuc_ad_get);
        if (p_ipuc_ad_get)
        {
            p_ipuc_info->binding_nh = p_ipuc_ad_get->binding_nh;
        }

        if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
        {
            mem_free(p_ipuc_ad_new);
        }

        if (ret < 0)
        {
            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

            continue;
        }
        else
        {
            if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
            {
                ip_ver = SYS_IPUC_VER(p_ipuc_info);
                if ((CTC_IP_VER_4 == ip_ver && 0 == p_gg_ipuc_master[lchip]->default_route_mode)
                    || (CTC_IP_VER_6 == ip_ver))
                {
                    if (p_ipuc_info->ad_offset >= (p_gg_ipuc_master[lchip]->default_base[ip_ver] +  p_gg_ipuc_master[lchip]->max_vrfid[ip_ver]))
                    {

                        /*alloc ad index from position*/
                        ret = sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsIpDa_t, 0, 1, p_ipuc_info->ad_offset);
                        if (ret)
                        {
                            ctc_spool_remove(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_get, NULL);
                            mem_free(p_ipuc_ad_new);
                            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc IpDa ftm table offset from position %u error! ret: %d.\n", p_ipuc_info->ad_offset, ret);

                            continue;
                        }
                    }
                }
                 /*SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc ad index from %u.\n", p_ipuc_info->ad_offset);*/
            }
        }

        /*add lpm route once again*/
        if (p_ipuc_info->route_opt == DO_LPM)
        {
            ret = sys_goldengate_ipuc_add_lpm(lchip, p_ipuc_info, &lpm_info);
            if (ret)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "sys_goldengate_ipuc_add_lpm error! ret: %d.\n", ret);
                continue;
            }
        }/*alloc Tcam key index from position*/
        else if (DO_TCAM == p_ipuc_info->route_opt)
        {
            if (p_gg_ipuc_master[lchip]->tcam_mode)
            {
                if (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4)
                {
                    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                        masklen_block_map[p_gg_ipuc_master[lchip]->use_hash16][p_ipuc_info->masklen];
                }
                else
                {
                    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                        masklen_ipv6_block_map[p_ipuc_info->masklen];
                }


                ret = sys_goldengate_sort_key_alloc_offset_from_position(lchip,
                    &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)], 1, p_ipuc_info->key_offset);
                if (ret)
                {
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "sys_goldengate_sort_key_alloc_offset_from_position %u error! ret: %d.\n", p_ipuc_info->key_offset, ret);
                    continue;
                }
                 /*SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc sort key index from %u.\n", p_ipuc_info->key_offset);*/
            }
        }

        /*add to soft table*/
        ctc_hash_insert(p_gg_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info);

    CTC_WB_QUERY_ENTRY_END((&wb_query));

/*restore  natsa info*/
    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_ipuc_nat_sa_info_t, CTC_FEATURE_IPUC, SYS_WB_APPID_IPUC_SUBID_NAT_SA_INFO);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_natsa_info = (sys_wb_ipuc_nat_sa_info_t *)wb_query.buffer + entry_cnt++;

        p_natsa_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_nat_sa_info_t));
        if (NULL == p_natsa_info)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_natsa_info, 0, sizeof(sys_ipuc_nat_sa_info_t));

        ret = _sys_goldengate_ipuc_wb_mapping_natsa(lchip, p_wb_natsa_info, p_natsa_info, 0);
        if (ret)
        {
            mem_free(p_natsa_info);
            continue;
        }

        ctc_hash_insert(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, p_natsa_info);

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


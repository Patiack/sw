/**
   @file sys_goldengate_l2_fdb.c

   @date 2013-6-13

   @version v2.0

   The file implement   FDB/L2 unicast /mac filtering /mac security/L2 mcast functions
 */

#include "sal.h"
#include "ctc_const.h"
#include "ctc_hash.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_error.h"



#include "ctc_l2.h"
#include "ctc_pdu.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_aps.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_security.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_stp.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_fdb_sort.h"

#include "goldengate/include/drv_lib.h"

#define FAIL(r)                 ((r) < 0)
#define FDB_ERR_RET_UL(r)       CTC_ERROR_RETURN_WITH_UNLOCK((r), pl2_gg_master[lchip]->l2_mutex)
#define FDB_SET_MAC(d, s)       sal_memcpy(d, s, sizeof(mac_addr_t))
#define FDB_SET_HW_MAC(d, s)    SYS_GOLDENGATE_SET_HW_MAC(d, s)
#define SYS_L2_FDB_CAM_NUM    32
#define HASH_INDEX(index)    (index)
#define SYS_FDB_DISCARD_FWD_PTR    0
#define DISCARD_PORT               0xFFFF
#define SYS_L2_MAX_FID             0xFFFF

#define SYS_FDB_FLUSH_SLEEP(cnt)         \
{                                        \
    cnt++;                               \
    if (cnt >= 1024)                     \
    {                                    \
        sal_task_sleep(1);               \
        cnt = 0;                         \
    }                                    \
}


#define SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop)  \
    {                                                       \
        flush_fdb_cnt_per_loop = pl2_gg_master[lchip]->cfg_flush_cnt; \
    }

#define SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop) \
    {                                                          \
        if ((flush_fdb_cnt_per_loop) > 0)                      \
        {                                                      \
            (flush_fdb_cnt_per_loop)--;                        \
            if (0 == (flush_fdb_cnt_per_loop))                 \
            {                                                  \
                return CTC_E_FDB_OPERATION_PAUSE;              \
            }                                                  \
        }                                                      \
    }

#define SYS_FDB_DBG_OUT(level, FMT, ...) \
    CTC_DEBUG_OUT(l2, fdb, L2_FDB_SYS, level, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_FUNC() \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)

#define SYS_FDB_DBG_INFO(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_PARAM(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_ERROR(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ## __VA_ARGS__)

#define SYS_FDB_DBG_DUMP(FMT, ...) \
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)


#define SYS_L2_DUMP_SYS_INFO(psys)                                        \
    {                                                                     \
        SYS_FDB_DBG_INFO("@@@@@ MAC[%s]  FID[%d]\n"                       \
                         "     gport:0x%x,   key_index:0x%x, nhid:0x%x\n" \
                         "     ad_index:0x%x\n",                          \
                         sys_goldengate_output_mac((psys)->mac), (psys)->fid,        \
                         (psys)->gport, (psys)->key_index, (psys)->nhid,  \
                         (psys)->ad_index);                               \
    }

#define SYS_IGS_VSI_PARAM_MAX    8
#define SYS_EGS_VSI_PARAM_MAX    2

typedef DsFibHost0MacHashKey_m   ds_key_t;

/****************************************************************************
 *
 * Global and Declaration
 *
 *****************************************************************************/

sys_l2_master_t* pl2_gg_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
sal_timer_t* sys_l2_pf_timer = NULL;

#define  _ENTRY_

#define CONTINUE_IF_FLAG_NOT_MATCH(query_flag, flag_ad, flag_node) \
    switch (query_flag)                              \
    {                                                \
    case CTC_L2_FDB_ENTRY_STATIC:                    \
        if (!flag_ad.is_static)                         \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case  CTC_L2_FDB_ENTRY_DYNAMIC:                  \
        if (flag_ad.is_static)                          \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:             \
        if (flag_node.remote_dynamic || flag_ad.is_static)   \
        {                                            \
            continue;                                \
        }                                            \
        break;                                       \
                                                     \
    case CTC_L2_FDB_ENTRY_ALL:                       \
    default:                                         \
        break;                                       \
    }


#define RETURN_IF_FLAG_NOT_MATCH(query_flag, flag_ad, flag_node) \
    switch (query_flag)                            \
    {                                              \
    case CTC_L2_FDB_ENTRY_STATIC:                  \
        if (!flag_ad.is_static)                       \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case  CTC_L2_FDB_ENTRY_DYNAMIC:                \
        if (flag_ad.is_static)                        \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:           \
        if (flag_node.remote_dynamic || flag_ad.is_static) \
        {                                          \
            return 0;                              \
        }                                          \
        break;                                     \
                                                   \
    case CTC_L2_FDB_ENTRY_ALL:                     \
    default:                                       \
        break;                                     \
    }


#define SYS_FDB_IS_DYNAMIC(FLAG_AD, FLAG_NODE, IS_DYNAMIC)               \
{                                                         \
    IS_DYNAMIC = (!FLAG_AD.is_static && !FLAG_NODE.ecmp_valid     \
                 && !FLAG_NODE.type_l2mc);                     \
}

#define SYS_FDB_FLUSH_FDB_MAC_LIMIT_FLAG      \
  ( DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_PORT_LIMIT  \
  | DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_VLAN_LIMIT  \
  | DRV_FIB_ACC_FLUSH_DEC_DYNAMIC_LIMIT  \
  | DRV_FIB_ACC_FLUSH_DEC_SYSTEM_LIMIT )

#define UTILITY
#define GET_FID_NODE          0
#define GET_FID_LIST          1

#define L2_COUNT_GLOBAL       0
#define L2_COUNT_PORT_LIST    1
#define L2_COUNT_FID_LIST     2

#define L2_TYPE_UC            0
#define L2_TYPE_MC            1
#define L2_TYPE_DF            2
#define L2_TYPE_TCAM          3

#define DEFAULT_ENTRY_INDEX(fid)            (pl2_gg_master[lchip]->dft_entry_base + (fid))
#define IS_DEFAULT_ENTRY_INDEX(ad_index)    ((ad_index) <= (pl2_gg_master[lchip]->dft_entry_base+pl2_gg_master[lchip]->cfg_max_fid))

#define SYS_L2_DECODE_QUERY_FID(index)   (((index) >> 18) & 0x3FFF)
#define SYS_L2_DECODE_QUERY_IDX(index)   ((index) & 0x3FFFF)
#define SYS_L2_ENCODE_QUERY_IDX(fid, index)   (((fid) << 18)|(index))


typedef struct
{
    uint32 key_index[20];
    uint8  index_cnt;
    uint8  rsv[3];
}sys_l2_calc_index_t;

STATIC int32
_sys_goldengate_l2_free_dsmac_index(uint8 lchip, sys_l2_info_t* psys, sys_l2_ad_spool_t* pa, sys_l2_flag_node_t* pn, uint8* freed);

STATIC int32
_sys_goldengate_l2_fdb_calc_index(uint8 lchip, mac_addr_t mac, uint16 fid, sys_l2_calc_index_t* p_index);
STATIC int32 _sys_goldengate_l2_set_fid_property(uint8 lchip, uint16 fid_id, uint32 fid_prop, uint32 value);
/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/
extern int32 sys_goldengate_nh_remove_all_members(uint8 lchip,  uint32 nhid);
extern int32 sys_goldengate_l2_fdb_wb_sync(uint8 lchip);
extern int32 sys_goldengate_l2_fdb_wb_restore(uint8 lchip);
extern int32 sys_goldengate_aging_get_aging_timer(uint8 lchip, uint32 key_index, uint8* p_timer);

STATIC int32
_sys_goldengate_get_dsmac_by_index(uint8 lchip, uint32 ad_index, void* p_ad_out)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, p_ad_out));
     /*SYS_FDB_DBG_INFO("Read dsmac, ad_index:0x%x\n", ad_index);*/

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_acc_lookup_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, mac_lookup_result_t* pr)
{
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    SYS_FDB_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    sal_memset(pr, 0, sizeof(mac_lookup_result_t));

    in.mac.fid = fid;
    FDB_SET_HW_MAC(in.mac.mac, mac);
    CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_MAC, &in, &out));

    pr->ad_index  = out.mac.ad_index;
    pr->key_index = out.mac.key_index;
    pr->hit       = out.mac.hit;
    pr->conflict  = out.mac.conflict;
    pr->pending   = out.mac.pending;
    if (pr->pending)
    {
        pr->hit       = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_decode_dskey(uint8 lchip, sys_l2_info_t* psys, void* p_dskey, uint32 key_index)
{
    uint8           timer = 0;
    hw_mac_addr_t   hw_mac;

    SYS_FDB_DBG_FUNC();

    psys->fid = GetDsFibHost0MacHashKey(V, vsiId_f, p_dskey);

    GetDsFibHost0MacHashKey(A, mappedMac_f, p_dskey, hw_mac);
    SYS_GOLDENGATE_SET_USER_MAC(psys->mac, hw_mac);
    psys->ad_index  = GetDsFibHost0MacHashKey(V, dsAdIndex_f, p_dskey);
    psys->key_valid = GetDsFibHost0MacHashKey(V, valid_f, p_dskey);
    psys->pending   = GetDsFibHost0MacHashKey(V, pending_f, p_dskey);

    CTC_ERROR_RETURN(sys_goldengate_aging_get_aging_timer(lchip, key_index, &timer));
    if(timer == 0)
    {
        psys->flag.flag_node.aging_disable = 1;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_decode_dsmac(uint8 lchip, sys_l2_info_t* psys, DsMac_m* p_dsmac, uint32 ad_index)
{
    uint32 destmap = 0;
    CTC_PTR_VALID_CHECK(psys);
    CTC_PTR_VALID_CHECK(p_dsmac);

    SYS_FDB_DBG_FUNC();

    psys->ad_index        = ad_index;
    psys->flag.flag_ad.is_static  = GetDsMac(V, isStatic_f, p_dsmac);
    psys->flag.flag_node.aging_disable = (psys->flag.flag_ad.is_static)?1:psys->flag.flag_node.aging_disable;
    psys->flag.flag_ad.bind_port  = GetDsMac(V, srcMismatchDiscard_f, p_dsmac);
    psys->flag.flag_ad.logic_port = GetDsMac(V, learnSource_f, p_dsmac);
    psys->fwd_ptr             = GetDsMac(V, u1_g1_dsFwdPtr_f, p_dsmac);

    if (psys->flag.flag_ad.logic_port)
    {
        psys->gport = GetDsMac(V, u2_g2_logicSrcPort_f, p_dsmac);
    }
    else
    {
        if ((psys->fwd_ptr == SYS_FDB_DISCARD_FWD_PTR) && (!GetDsMac(V, nextHopPtrValid_f , p_dsmac)))
        {
            psys->gport = DISCARD_PORT;
        }
        else
        {
            psys->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsMac(V, u2_g1_globalSrcPort_f, p_dsmac));
        }
    }
    psys->flag.flag_ad.protocol_entry = GetDsMac(V, protocolExceptionEn_f, p_dsmac);
    psys->flag.flag_ad.copy_to_cpu    = GetDsMac(V, macDaExceptionEn_f, p_dsmac);
    psys->flag.flag_ad.src_discard    = GetDsMac(V, srcDiscard_f, p_dsmac);
    psys->flag.flag_ad.self_address   = GetDsMac(V, localAddress_f , p_dsmac);

    /* | tid| gport| vport| default|*/
    if (ad_index >= pl2_gg_master[lchip]->base_trunk)
    {
        psys->flag.flag_node.rsv_ad_index = 1;
    }
    else if (ad_index == pl2_gg_master[lchip]->ad_index_drop)
    {
        psys->flag.flag_ad.drop = 1;
        psys->flag.flag_node.rsv_ad_index = 1;
    }
    else if ( !GetDsMac(V, nextHopPtrValid_f, p_dsmac) && (SYS_FDB_DISCARD_FWD_PTR == GetDsMac(V, u1_g1_dsFwdPtr_f, p_dsmac)))
    {
        psys->flag.flag_ad.drop = 1;
    }

    destmap = GetDsMac(V, u1_g2_adDestMap_f, p_dsmac);
    if (psys->flag.flag_ad.is_static && /*mcast is static.*/
        (GetDsMac(V, nextHopPtrValid_f, p_dsmac) || psys->flag.flag_ad.drop) && /*if mcast is drop, nexhtop ptr valid is 0*/
        IS_MCAST_DESTMAP(destmap))
    {
        if (IS_DEFAULT_ENTRY_INDEX(ad_index))
        {
            psys->flag.flag_node.type_default = 1;
        }
        else
        {
            psys->flag.flag_node.type_l2mc = 1;
            psys->mc_gid         = (destmap &0xFFFF);
        }
    }
    psys->flag.flag_ad.src_discard_to_cpu = GetDsMac(V, srcDiscard_f, p_dsmac) &&
                                    GetDsMac(V, macSaExceptionEn_f, p_dsmac);
    psys->share_grp_en = !GetDsMac(V, fastLearningEn_f, p_dsmac);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_encode_dsmac(uint8 lchip, DsMac_m* p_dsmac, sys_l2_info_t* psys)
{
    uint8  gchip    = 0;
    uint16 group_id = 0;
    uint8   speed = 0;
    sys_nh_info_dsnh_t nh_info;
    uint32 cmd = 0;
    uint32 value = 0;

    CTC_PTR_VALID_CHECK(psys);
    CTC_PTR_VALID_CHECK(p_dsmac);
    SYS_FDB_DBG_FUNC();

      speed = 0;

    sal_memset(p_dsmac, 0, sizeof(DsMac_m));

    if (!psys->flag.flag_node.type_l2mc && !psys->flag.flag_node.type_default)
    {
        SetDsMac(V, sourcePortCheckEn_f, p_dsmac, 1);

        if (!psys->flag.flag_ad.is_static && (psys->gport != DISCARD_PORT))
        {
            SetDsMac(V, srcMismatchLearnEn_f, p_dsmac, 1);
        }

        if (psys->flag.flag_ad.bind_port)
        {
            SetDsMac(V, srcMismatchDiscard_f, p_dsmac, 1);
        }

        if (psys->flag.flag_ad.logic_port)
        {
            SYS_FDB_DBG_INFO(" DsMac.logic_port 0x%x\n", psys->gport);
            SetDsMac(V, learnSource_f, p_dsmac, 1);
            SetDsMac(V, u2_g2_logicSrcPort_f, p_dsmac, psys->gport);
        }
        else
        {
            SYS_FDB_DBG_INFO(" DsMac.gport 0x%x\n", psys->gport);
            SetDsMac(V, learnSource_f, p_dsmac, 0);
            SetDsMac(V, u2_g1_globalSrcPort_f, p_dsmac, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(psys->gport));
        }

        SetDsMac(V, isConversation_f, p_dsmac, 0);
        if (!psys->merge_dsfwd)
        {
             if (psys->flag.flag_node.ecmp_valid)
            {
                SetDsMac(V, u1_g1_ecmpEn_f, p_dsmac, 1);
                SetDsMac(V, u1_g1_ecmpGroupId_f, p_dsmac, psys->dest_id);
            }
             else
             {
                 SetDsMac(V, u1_g1_dsFwdPtr_f, p_dsmac, psys->fwd_ptr);
             }
        }
        else
        {
            uint8 dest_chipid = psys->dest_chipid;
            uint16 dest_id = psys->dest_id;

            SetDsMac(V, nextHopPtrValid_f, p_dsmac, 1);
            if (psys->flag.flag_node.mc_nh)
            {
                SetDsMac(V, u1_g2_adDestMap_f, p_dsmac, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, dest_id));
            }
            else if (psys->flag.flag_node.is_ecmp_intf)
            {
                /*just for ecmp interface*/
                SetDsMac(V, u1_g2_adDestMap_f, p_dsmac, ((0x3d << 12) | (dest_id&0xFFF)));
            }
            else
            {
                SetDsMac(V, u1_g2_adDestMap_f, p_dsmac, SYS_ENCODE_DESTMAP(dest_chipid, dest_id));
            }

            SetDsMac(V, u1_g2_adNextHopPtr_f, p_dsmac,psys->dsnh_offset);
            SetDsMac(V, u1_g2_adApsBridgeEn_f, p_dsmac, psys->flag.flag_node.aps_valid);

            if (psys->with_nh)
            {
                uint8 adjust_len_idx = 0;
                sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
                /*get nexthop info from nexthop id */
                CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, psys->nhid, &nh_info));
                if(0 != nh_info.adjust_len)
                {
                    sys_goldengate_lkup_adjust_len_index(lchip, nh_info.adjust_len, &adjust_len_idx);
                    SetDsMac(V, u1_g2_adLengthAdjustType_f, p_dsmac, adjust_len_idx);
                }
                else
                {
                    SetDsMac(V, u1_g2_adLengthAdjustType_f, p_dsmac, 0);
                }
                if (nh_info.nexthop_ext)
                {
                    SetDsMac(V, u1_g2_adNextHopExt_f, p_dsmac, 1);
                }
                if (SYS_NH_TYPE_IP_TUNNEL == nh_info.nh_entry_type)
                {
                    SetDsMac(V, u1_g2_adCareLength_f, p_dsmac, 1);
                }
            }
        }
    }
    else if (psys->flag.flag_node.type_default && psys->revert_default)
    {
        SetDsMac(V, u1_g1_dsFwdPtr_f, p_dsmac, SYS_FDB_DISCARD_FWD_PTR);
        SetDsMac(V, protocolExceptionEn_f, p_dsmac, 1);
    }
    else if (psys->flag.flag_node.type_default && !psys->revert_default)
    {
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
        {
            SetDsMac(V, learnSource_f, p_dsmac, 1);
        }

        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_LEARNING_DISABLE))
        {
            SetDsMac(V, conversationCheckEn_f, p_dsmac, 1);
        }
        else
        {
            SetDsMac(V, conversationCheckEn_f, p_dsmac, 0);
        }

        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_PROTOCOL_EXCP_TOCPU))
        {
            SetDsMac(V, protocolExceptionEn_f, p_dsmac, 1);
        }
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_PORT_MAC_AUTH))
        {
            SetDsMac(V, srcMismatchDiscard_f, p_dsmac, 1);
            SetDsMac(V, u2_g1_globalSrcPort_f, p_dsmac, SYS_RSV_PORT_DROP_ID);
        }
        if (CTC_FLAG_ISSET(psys->fid_flag, CTC_L2_DFT_VLAN_FLAG_VLAN_MAC_AUTH))
        {
            SetDsMac(V, srcDiscard_f, p_dsmac, 1);
            cmd = DRV_IOR(DsMac_t, DsMac_macSaExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, &value));
            SetDsMac(V, macSaExceptionEn_f, p_dsmac, value);
        }

        /* always use nexthop */
        SetDsMac(V, nextHopPtrValid_f, p_dsmac, 1);
        SetDsMac(V, u1_g2_adDestMap_f, p_dsmac, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, psys->mc_gid));
        SetDsMac(V, u1_g2_adNextHopPtr_f, p_dsmac, 0);
        SetDsMac(V, isConversation_f, p_dsmac, 0);

        SYS_FDB_DBG_INFO("gchip=%d, group_id=0x%x\n", gchip, group_id);
    }
    else  /* (psys->flag.type_l2mc)*/
    {
        if (psys->is_exist)
        {
            cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, p_dsmac));
        }
        /* always use nexthop */
        SetDsMac(V, nextHopPtrValid_f, p_dsmac, 1);
        if (psys->mc_gid)
        {
            SetDsMac(V, u1_g2_adDestMap_f, p_dsmac, SYS_ENCODE_MCAST_IPE_DESTMAP(speed, psys->mc_gid));
        }
        SetDsMac(V, u1_g2_adNextHopPtr_f, p_dsmac, 0);
        SetDsMac(V, isConversation_f, p_dsmac, 0);

        SYS_FDB_DBG_INFO("gchip=%d, group_id=0x%x\n", gchip, group_id);
    }

    if (psys->flag.flag_ad.is_static)
    {
        SetDsMac(V, isStatic_f, p_dsmac, 1);
    }

    if (psys->flag.flag_ad.self_address)
    {
        SetDsMac(V, localAddress_f, p_dsmac, 1);
    }
    if (psys->flag.flag_ad.drop)
    {
        SetDsMac(V, nextHopPtrValid_f, p_dsmac, 0);
        SetDsMac(V, u1_g1_dsFwdPtr_f, p_dsmac, SYS_FDB_DISCARD_FWD_PTR);
        SetDsMac(V, u1_g1_priorityPathEn_f, p_dsmac, 0);
        SetDsMac(V, u1_g1_ecmpEn_f, p_dsmac, 0);
    }

    if (psys->flag.flag_ad.src_discard)
    {
        SetDsMac(V, srcDiscard_f, p_dsmac, 1);
        SetDsMac(V, macSaExceptionEn_f, p_dsmac, 0);
    }

    if (psys->flag.flag_ad.src_discard_to_cpu)
    {
        SetDsMac(V, srcDiscard_f, p_dsmac, 1);
        SetDsMac(V, macSaExceptionEn_f, p_dsmac, 1);
    }

    if (psys->flag.flag_ad.copy_to_cpu)
    {
        SetDsMac(V, macDaExceptionEn_f, p_dsmac, 1);
        /* Reserved for normal MACDA copy_to_cpu */
        SetDsMac(V, exceptionSubIndex_f, p_dsmac, CTC_L2PDU_ACTION_INDEX_MACDA);
    }

    if (psys->flag.flag_ad.protocol_entry)
    {
        SetDsMac(V, protocolExceptionEn_f, p_dsmac, 1);
    }

    SetDsMac(V, stormCtlEn_f, p_dsmac, 1);
    SetDsMac(V, fastLearningEn_f, p_dsmac, (psys->share_grp_en)?0:1);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_map_sys_to_sw(uint8 lchip, sys_l2_info_t * psys, sys_l2_node_t * pn, sys_l2_ad_spool_t* pa)
{
    SYS_FDB_DBG_FUNC();

    if (pn) /* no map key */
    {
        pn->key.fid = psys->fid;
        FDB_SET_MAC(pn->key.mac, psys->mac);
    }

    pa->flag = psys->flag.flag_ad;
    pa->gport = psys->gport;
    pa->dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(psys->dest_chipid, psys->dest_id);
    pa->nhid = psys->nhid;

    return CTC_E_NONE;
}

void*
_sys_goldengate_l2_fid_node_lkup(uint8 lchip, uint16 fid, uint8 type)
{
    sys_l2_fid_node_t* p_lkup;

    SYS_FDB_DBG_FUNC();

    p_lkup = ctc_vector_get(pl2_gg_master[lchip]->fid_vec, fid);

    if (GET_FID_NODE == type)
    {
        return p_lkup;
    }
    else if (GET_FID_LIST == type)
    {
        return (p_lkup) ? (p_lkup->fid_list) : NULL;
    }

    return NULL;
}

STATIC sys_l2_tcam_t*
_sys_goldengate_l2_tcam_hash_lookup(uint8 lchip, mac_addr_t mac)
{
    sys_l2_tcam_t tcam;

    sal_memset(&tcam, 0, sizeof(tcam));

    FDB_SET_MAC(tcam.key.mac, mac);
    return ctc_hash_lookup(pl2_gg_master[lchip]->tcam_hash, &tcam);
}

STATIC sys_l2_node_t*
_sys_goldengate_l2_fdb_hash_lkup(uint8 lchip, mac_addr_t mac, uint16 fid)
{
    sys_l2_node_t node;

    SYS_FDB_DBG_FUNC();

    sal_memset(&node, 0, sizeof(node));

    node.key.fid = fid;
    FDB_SET_MAC(node.key.mac, mac);

    return ctc_hash_lookup(pl2_gg_master[lchip]->fdb_hash, &node);
}


STATIC int32
_sys_goldengate_l2_unmap_flag(uint8 lchip, uint32* ctc_out, sys_l2_flag_t* p_flag)
{
    uint32 ctc_flag = 0;

    if (p_flag->flag_ad.is_static)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_IS_STATIC);
    }
    if (p_flag->flag_node.remote_dynamic)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
    }
    if (p_flag->flag_ad.drop)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_DISCARD);
    }
    if (p_flag->flag_node.system_mac)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SYSTEM_RSV);
    }
    if (p_flag->flag_ad.copy_to_cpu)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU);
    }
    if (p_flag->flag_ad.protocol_entry)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY);
    }
    if (p_flag->flag_ad.bind_port)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_BIND_PORT);
    }
    if (p_flag->flag_ad.src_discard)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD);
    }
    if (p_flag->flag_ad.src_discard_to_cpu)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU);
    }
    if (p_flag->flag_ad.self_address)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_SELF_ADDRESS);
    }
    if (p_flag->flag_node.white_list)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_AGING_DISABLE);
    }

    if (p_flag->flag_node.aging_disable)
    {
        CTC_SET_FLAG(ctc_flag, CTC_L2_FLAG_AGING_DISABLE);
    }

    *ctc_out = ctc_flag;
    return CTC_E_NONE;
}

/*Notice: here gport means dest port */
STATIC int32
_sys_goldengate_l2_map_nh_to_sys(uint8 lchip, uint32 nhid, sys_l2_info_t * psys, uint8 is_assign, uint16 src_gport, uint16 dest_gport)
{
    sys_nh_info_dsnh_t nh_info;

    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

    /*get nexthop info from nexthop id */
    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nhid, &nh_info));
    if (is_assign && !nh_info.ecmp_valid && !nh_info.is_ecmp_intf)
    {
        psys->merge_dsfwd = 1;
        psys->dsnh_offset = nh_info.dsnh_offset;
        psys->dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(dest_gport);
        psys->dest_id  = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dest_gport);
    }
    else if ((nh_info.merge_dsfwd == 1) || (nh_info.is_mcast))
    {
        psys->merge_dsfwd = 1;
        psys->dsnh_offset = nh_info.dsnh_offset;
        psys->dest_chipid = nh_info.dest_chipid;

        if (nh_info.is_ecmp_intf)
         {
                psys->dest_id  = nh_info.ecmp_group_id;
                psys->dest_chipid = 0x3d;
                psys->flag.flag_node.is_ecmp_intf = 1;
         }
        else
        {
            psys->dest_id  = nh_info.dest_id;
        }
    }
    else  if (nh_info.ecmp_valid)
     {
            psys->dest_id = nh_info.ecmp_group_id;
     }
    else
    {
        sys_goldengate_nh_get_dsfwd_offset(lchip, nhid, &psys->fwd_ptr);
        psys->merge_dsfwd = 0;

    }

    psys->nhid = nhid;
    psys->flag.flag_node.ecmp_valid = nh_info.ecmp_valid;
    psys->flag.flag_node.aps_valid = nh_info.aps_en;
    psys->flag.flag_node.mc_nh = nh_info.is_mcast;
    if(is_assign && (!psys->flag.flag_ad.logic_port))
    {
        psys->gport          = dest_gport;
    }
    else
    {
        psys->gport          = src_gport;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_map_flag(uint8 lchip, uint8 type, void* l2, sys_l2_flag_t* p_flag)
{
    SYS_FDB_DBG_FUNC();

    if ((L2_TYPE_UC == type) || (L2_TYPE_TCAM == type))
    {
        ctc_l2_addr_t* l2_addr = (ctc_l2_addr_t*) l2;
        sys_l2_fid_node_t * fid_node   = NULL;
        uint32              ctc_flag   = l2_addr->flag;

        if (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC)
            && CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC))
        {
            return CTC_E_INVALID_PARAM;
        }

        p_flag->flag_ad.is_static          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC));
        p_flag->flag_node.remote_dynamic     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_REMOTE_DYNAMIC));
        p_flag->flag_ad.drop               = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_DISCARD));
        p_flag->flag_node.system_mac         = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SYSTEM_RSV));
        p_flag->flag_ad.copy_to_cpu        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU));
        p_flag->flag_ad.protocol_entry     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY));
        p_flag->flag_ad.bind_port          = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_BIND_PORT));
        p_flag->flag_ad.src_discard        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD));
        p_flag->flag_ad.src_discard_to_cpu = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU));
        p_flag->flag_ad.self_address       = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SELF_ADDRESS));
        p_flag->flag_node.white_list       = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY));
        p_flag->flag_node.aging_disable = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_AGING_DISABLE)) | \
            (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_IS_STATIC)) | (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY));
        p_flag->flag_node.limit_exempt     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_LEARN_LIMIT_EXEMPT));

        if (L2_TYPE_TCAM == type)
        {
            p_flag->flag_ad.is_static          = 1;
            p_flag->flag_node.is_tcam            = 1;
        }

        fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);

        if (fid_node)
        {
            p_flag->flag_ad.logic_port = (CTC_FLAG_ISSET(fid_node->flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT));
        }
    }
    else if (L2_TYPE_MC == type)
    {
        ctc_l2_mcast_addr_t* l2_addr = (ctc_l2_mcast_addr_t *) l2;
        uint32             ctc_flag  = l2_addr->flag;

        /* mc support these action.*/
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_DISCARD);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_SELF_ADDRESS);
        CTC_UNSET_FLAG(ctc_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        if (ctc_flag)
        {
            return CTC_E_INVALID_PARAM;
        }

        ctc_flag                   = l2_addr->flag;
        p_flag->flag_ad.is_static          = 1;
        p_flag->flag_ad.drop               = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_DISCARD));
        p_flag->flag_ad.copy_to_cpu        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_COPY_TO_CPU));
        p_flag->flag_ad.protocol_entry     = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_PROTOCOL_ENTRY));
        p_flag->flag_ad.src_discard        = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD));
        p_flag->flag_ad.src_discard_to_cpu = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SRC_DISCARD_TOCPU));
        p_flag->flag_ad.self_address       = (CTC_FLAG_ISSET(ctc_flag, CTC_L2_FLAG_SELF_ADDRESS));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_flush_by_hw(uint8 lchip, ctc_l2_flush_fdb_t* p_flush, uint8 mac_limit_en)
{
    drv_fib_acc_in_t  acc_in;
    drv_fib_acc_out_t acc_out;
    uint8             fib_acc_type = 0;

    LCHIP_CHECK(lchip);

    sal_memset(&acc_in, 0, sizeof(acc_in));
    sal_memset(&acc_out, 0, sizeof(acc_out));

    switch (p_flush->flush_type)
    {
    /* maybe need to add flush by port +vid */

    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.fid  = p_flush->fid;
        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_VSI;

        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_PORT;
        if (p_flush->use_logic_port)
        {
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_LOGIC_PORT;
            acc_in.mac_flush.port = p_flush->gport;
        }
        else
        {
            acc_in.mac_flush.port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_flush->gport);
        }

        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        fib_acc_type          = DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN;
        acc_in.mac_flush.fid  = p_flush->fid;
        acc_in.mac_flush.flag = DRV_FIB_ACC_FLUSH_BY_VSI | DRV_FIB_ACC_FLUSH_BY_PORT;
        if (p_flush->use_logic_port)
        {
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_LOGIC_PORT;
            acc_in.mac_flush.port = p_flush->gport;
        }
        else
        {
            acc_in.mac_flush.port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_flush->gport);
        }
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        fib_acc_type = DRV_FIB_ACC_DEL_MAC_BY_KEY;
        FDB_SET_HW_MAC(acc_in.mac.mac, p_flush->mac);
        acc_in.mac.fid = p_flush->fid;
        /* add for mac limit*/
        if (0 != mac_limit_en)
        {
            acc_in.mac.flag |= mac_limit_en;
        }

        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        fib_acc_type = DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT;
        FDB_SET_HW_MAC(acc_in.mac_flush.mac, p_flush->mac);
        break;


    case CTC_L2_FDB_ENTRY_OP_ALL:
        fib_acc_type = DRV_FIB_ACC_FLUSH_MAC_ALL;
        break;
/* not done yet */
    default:
        return CTC_E_NOT_SUPPORT;
    }

    if (DRV_FIB_ACC_FLUSH_MAC_ALL == fib_acc_type
        || DRV_FIB_ACC_FLUSH_MAC_BY_PORT_VLAN == fib_acc_type
        || DRV_FIB_ACC_FLUSH_MAC_BY_MAC_PORT == fib_acc_type)
    {
        switch (p_flush->flush_flag)
        {
        case CTC_L2_FDB_ENTRY_STATIC:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_STATIC;
            break;

        case CTC_L2_FDB_ENTRY_DYNAMIC:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_DYNAMIC;
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            acc_in.mac_flush.flag |= DRV_FIB_ACC_FLUSH_STATIC + DRV_FIB_ACC_FLUSH_DYNAMIC;
            break;

        default:
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
        /* add for mac limit*/
        if (0 != mac_limit_en)
        {
            acc_in.mac_flush.flag |= SYS_FDB_FLUSH_FDB_MAC_LIMIT_FLAG;
            if (pl2_gg_master[lchip]->static_fdb_limit)
            {
                 acc_in.mac_flush.flag |= (DRV_FIB_ACC_FLUSH_DEC_STATIC_PORT_LIMIT | DRV_FIB_ACC_FLUSH_DEC_STATIC_VLAN_LIMIT);
            }
        }
    }


    CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, fib_acc_type, &acc_in, &acc_out));


    return CTC_E_NONE;
}

int32
sys_goldengate_l2_flush_by_hw(uint8 lchip, ctc_l2_flush_fdb_t* p_flush, uint8 mac_limit_en)
{
    uint8 mac_limit = mac_limit_en;
    sys_l2_info_t       psys;
    int8                is_dynamic = 0;
    DsMac_m             ds_mac;
    mac_lookup_result_t rslt;

    SYS_L2_INIT_CHECK(lchip);
    SYS_FDB_DBG_FUNC();
    if ((p_flush->flush_type == CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN) && (0 != mac_limit_en))
    {
        sal_memset(&ds_mac, 0, sizeof(DsMac_m));
        sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));
        sal_memset(&psys, 0, sizeof(sys_l2_info_t));
        CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, p_flush->mac, p_flush->fid, &rslt));
        if((!rslt.hit) && (!rslt.pending))
        {
            return CTC_E_NONE;
        }
        _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &ds_mac);
        _sys_goldengate_l2_decode_dsmac(lchip, &psys, &ds_mac, rslt.ad_index);
        SYS_FDB_IS_DYNAMIC(psys.flag.flag_ad, psys.flag.flag_node,is_dynamic);

        mac_limit = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!psys.flag.flag_node.limit_exempt))
        {
            mac_limit |= (psys.flag.flag_ad.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                         | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
            if (is_dynamic)
            {
                mac_limit |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
        }

    }

    return _sys_goldengate_l2_flush_by_hw(lchip, p_flush, mac_limit);
}


int32
sys_goldengate_l2_delete_hw_by_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, uint8 mac_limit_en)
{
    /*remove fail not return for syn with system*/
    ctc_l2_flush_fdb_t flush_fdb;
    sal_memset(&flush_fdb, 0, sizeof(ctc_l2_flush_fdb_t));
    FDB_SET_MAC(flush_fdb.mac, mac);

    flush_fdb.fid        = fid;
    flush_fdb.flush_type = CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN;
    flush_fdb.flush_flag = CTC_L2_FDB_ENTRY_ALL;
    return _sys_goldengate_l2_flush_by_hw(lchip, &flush_fdb, mac_limit_en);
}

STATIC void
_sys_goldengate_l2_update_count(uint8 lchip, sys_l2_flag_ad_t* pflag_a, sys_l2_flag_node_t* pflag_n, int32 do_increase, uint8 type, void* data)
{
    uint32* p_dynamic_count       = NULL;
    uint32* p_local_dynamic_count = NULL;

    SYS_FDB_DBG_FUNC();

    if (L2_COUNT_GLOBAL == type)
    {
        uint32 key_index = *(uint32 *) data;
        p_dynamic_count       = &(pl2_gg_master[lchip]->dynamic_count);
        p_local_dynamic_count = &(pl2_gg_master[lchip]->local_dynamic_count);

        pl2_gg_master[lchip]->total_count += do_increase;
        if (pflag_n->type_l2mc)
        {
            pl2_gg_master[lchip]->l2mc_count += do_increase;
        }
        if (key_index < SYS_L2_FDB_CAM_NUM)
        {
            pl2_gg_master[lchip]->cam_count += do_increase;
        }
    }
    else if (L2_COUNT_PORT_LIST == type) /* mc won't get to this */
    {
        p_dynamic_count       = &(((sys_l2_port_node_t *) data)->dynamic_count);
        p_local_dynamic_count = &(((sys_l2_port_node_t *) data)->local_dynamic_count);
    }
    else if (L2_COUNT_FID_LIST == type) /* mc won't get to this */
    {
        p_dynamic_count       = &(((sys_l2_fid_node_t *) data)->dynamic_count);
        p_local_dynamic_count = &(((sys_l2_fid_node_t *) data)->local_dynamic_count);
    }

    if (!pflag_n->type_l2mc && !pflag_a->is_static)
    {
        *p_dynamic_count += (do_increase);
        if (!pflag_n->remote_dynamic)
        {
            *p_local_dynamic_count += (do_increase);
        }
    }

    return;
}

/* based on node, so require has_sw = 1*/
STATIC int32
_sys_goldengate_l2_map_sw_to_entry(uint8 lchip, sys_l2_node_t* p_l2_node,
                                   ctc_l2_fdb_query_rst_t* pr,
                                   sys_l2_detail_t * pi,
                                   uint32* count)
{
    uint32 index = *count;
    sys_l2_flag_t flag;

    sal_memset(&flag, 0, sizeof(sys_l2_flag_t));

    (pr->buffer[index]).fid   = p_l2_node->key.fid;
    (pr->buffer[index]).gport = p_l2_node->adptr->gport;
    flag.flag_ad = p_l2_node->adptr->flag;
    flag.flag_node = p_l2_node->flag;
    _sys_goldengate_l2_unmap_flag(lchip, &pr->buffer[index].flag, &flag);
    (pr->buffer[index]).is_logic_port = p_l2_node->adptr->flag.logic_port;
    FDB_SET_MAC((pr->buffer[index]).mac, p_l2_node->key.mac);

    if (pi)
    {
        pi[index].key_index = HASH_INDEX(p_l2_node->key_index);
        pi[index].ad_index  = p_l2_node->adptr->index;
        pi[index].flag.flag_ad = p_l2_node->adptr->flag;
        pi[index].flag.flag_node = p_l2_node->flag;
        pi[index].pending   = 0; /* from software, pending always 0. */
    }

    (*count)++;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_map_dma_to_entry(uint8 lchip, sys_dma_info_t* dma_info,
                        ctc_l2_addr_t* pctc, sys_l2_detail_t* pi)
{
    uint16 index = 0;
    DmaFibDumpFifo_m* p_dump_info = NULL;
    hw_mac_addr_t                mac_sa   = { 0 };
    uint32 key_index= 0;
    uint32 ad_index = 0;
    DsMac_m            ds_mac;
    sys_l2_info_t       sys;
    uint8               timer = 0;
    SYS_FDB_DBG_FUNC();
    SYS_LCHIP_CHECK_ACTIVE(lchip);

    for (index = 0; index < dma_info->entry_num; index++)
    {
        sal_memset(&sys, 0, sizeof(sys_l2_info_t));
        p_dump_info = (DmaFibDumpFifo_m*)dma_info->p_data+index;
        GetDmaFibDumpFifo(A, mappedMac_f, p_dump_info, mac_sa);
        pctc[index].fid = GetDmaFibDumpFifo(V, vsiId_f, p_dump_info);
        SYS_GOLDENGATE_SET_USER_MAC(pctc[index].mac, mac_sa);
        key_index = GetDmaFibDumpFifo(V, keyIndex_f, p_dump_info);
        ad_index = GetDmaFibDumpFifo(V, dsAdIndex_f, p_dump_info);

        /*read dsmac */
        _sys_goldengate_get_dsmac_by_index(lchip, ad_index, &ds_mac);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &ds_mac, ad_index);
        CTC_ERROR_RETURN(sys_goldengate_aging_get_aging_timer(lchip, key_index, &timer));
        if(timer == 0)
        {
            sys.flag.flag_node.aging_disable = 1;
        }

        pctc[index].gport = sys.gport;
        if (sys.flag.flag_ad.logic_port)
        {
            pctc[index].is_logic_port = 1;
        }
        _sys_goldengate_l2_unmap_flag(lchip, &pctc[index].flag, &sys.flag);
        if (pi)
        {
            pi[index].ad_index = ad_index;
            pi[index].key_index = key_index;
            pi[index].flag = sys.flag;
            pi[index].pending = GetDmaFibDumpFifo(V, pending_f, p_dump_info);

        }
        if (GetDmaFibDumpFifo(V, pending_f, p_dump_info))
        {
            CTC_SET_FLAG(pctc[index].flag, CTC_L2_FLAG_PENDING_ENTRY);
        }
    }

    return CTC_E_NONE;
}
int32
_sys_goldengate_l2_get_blackhole_entry(uint8 lchip,
                                   ctc_l2_fdb_query_t* pq,
                                   ctc_l2_fdb_query_rst_t* pr,
                                   uint8  only_get_cnt,
                                   sys_l2_detail_t* detail_info)
{
    int32 ret = 0;
    uint32 cmd = 0;
    uint32 max_entry_cnt = pr->buffer_len/sizeof(ctc_l2_addr_t);
    DsFibMacBlackHoleHashKey_m tcam_key;
    sys_l2_detail_t*     temp_detail = detail_info;
    ctc_l2_addr_t*       temp_addr = pr->buffer;
    sys_l2_info_t       l2_info;
    uint32 index = 0;
    uint32 end_index = 0;
    hw_mac_addr_t hw_mac = {0};
    uint16 max_blackhole_entry_num = 128;
    uint32 ad_index = 0;
    DsMac_m ds_mac;

    pq->count = 0;
    if (pr->start_index >= max_blackhole_entry_num)
    {
        pr->is_end = 1;
        return CTC_E_NONE;
    }
    if ((pq->query_flag != CTC_L2_FDB_ENTRY_ALL)
        && (pq->query_flag != CTC_L2_FDB_ENTRY_STATIC))
    {
        pr->is_end = 1;
        return CTC_E_NONE;
    }

    end_index = ((pr->start_index + max_entry_cnt) >= max_blackhole_entry_num)
                ? max_blackhole_entry_num : (pr->start_index + max_entry_cnt);

    for (index = pr->start_index; index < end_index; index++)
    {
        sal_memset(&tcam_key, 0, sizeof(tcam_key));
        cmd = DRV_IOR(DsFibMacBlackHoleHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, index, cmd, &tcam_key), ret, error_roll);
        if (!GetDsFibMacBlackHoleHashKey(V, valid_f, &tcam_key))
        {
            continue;
        }
        pq->count++;
        if (only_get_cnt)
        {
            continue;
        }
        temp_addr->fid = DONTCARE_FID;
        GetDsFibMacBlackHoleHashKey(A, mappedMac_f, &tcam_key, hw_mac);
        SYS_GOLDENGATE_SET_USER_MAC(temp_addr->mac, hw_mac);
        ad_index = GetDsFibMacBlackHoleHashKey(V, dsAdIndex_f, &tcam_key);
        /*read dsmac */
        sal_memset(&l2_info, 0, sizeof(l2_info));
        sal_memset(&ds_mac, 0, sizeof(ds_mac));
        _sys_goldengate_get_dsmac_by_index(lchip, ad_index, &ds_mac);
        _sys_goldengate_l2_decode_dsmac(lchip, &l2_info, &ds_mac, ad_index);
        temp_addr->gport = l2_info.gport;
        _sys_goldengate_l2_unmap_flag(lchip, &temp_addr->flag, &l2_info.flag);
        if (temp_detail)
        {
            temp_detail->ad_index = ad_index;
            temp_detail->key_index = index;
            temp_detail->flag = l2_info.flag;
            temp_detail++;
        }
        temp_addr++;
    }
    if (end_index >= max_blackhole_entry_num)
    {
        pr->is_end = 1;
    }
    pr->next_query_index = end_index;
    return CTC_E_NONE;

error_roll:
    pr->next_query_index = index;
    pr->is_end = 1;
    return ret;
}


STATIC uint32
_sys_goldengate_l2_get_entry_by_list(uint8 lchip, ctc_linklist_t * fdb_list,
                                     uint8 query_flag,
                                     ctc_l2_fdb_query_rst_t * pr,
                                     sys_l2_detail_t * pi,
                                     uint8 extra_port,
                                     uint16 gport,
                                     uint32* all_index)
{
    ctc_listnode_t * listnode  = NULL;
    uint32         start_idx   = 0;
    uint32         count       = 0;
    uint32         max_count   = pr->buffer_len / sizeof(ctc_l2_addr_t);
    sys_l2_node_t  * p_l2_node = NULL;


    SYS_FDB_DBG_FUNC();

    if (all_index)
    {
        count = *all_index;
    }

    listnode = CTC_LISTHEAD(fdb_list);
    while (listnode != NULL && start_idx < pr->start_index)
    {
        CTC_NEXTNODE(listnode);
        start_idx++;
    }

    for (start_idx = pr->start_index;
         listnode && (count < max_count);
         CTC_NEXTNODE(listnode), start_idx++)
    {
        p_l2_node = listnode->data;
        if (extra_port && (p_l2_node->adptr->gport != gport))
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(query_flag, p_l2_node->adptr->flag, p_l2_node->flag)
        _sys_goldengate_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &count);
    }

    pr->next_query_index = start_idx;

    if (listnode == NULL)
    {
        pr->is_end = 1;
    }

    return count;
}

STATIC uint32
_sys_goldengate_l2_get_entry_by_port(uint8 lchip, uint8 query_flag,
                                     uint16 gport,
                                     uint8 use_logic_port,
                                     ctc_l2_fdb_query_rst_t* pr,
                                     sys_l2_detail_t* pi)
{
    ctc_linklist_t    * fdb_list      = NULL;
    sys_l2_port_node_t* port_fdb_node = NULL;

    SYS_FDB_DBG_FUNC();

    pr->is_end = 0;

    if (!use_logic_port)
    {
        port_fdb_node = ctc_vector_get(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_fdb_node || NULL == port_fdb_node->port_list)
        {
            pr->is_end = 1;
            return 0;
        }

        fdb_list = port_fdb_node->port_list;
    }
    else
    {
        port_fdb_node = ctc_vector_get(pl2_gg_master[lchip]->vport_vec, gport);
        if (NULL == port_fdb_node || NULL == port_fdb_node->port_list)
        {
            pr->is_end = 1;
            return 0;
        }

        fdb_list = port_fdb_node->port_list;
    }
    return _sys_goldengate_l2_get_entry_by_list(lchip, fdb_list, query_flag, pr, pi, 0, 0, 0);
}

STATIC uint32
_sys_goldengate_l2_get_entry_by_fid(uint8 lchip, uint8 query_flag,
                                    uint16 fid,
                                    ctc_l2_fdb_query_rst_t* query_rst,
                                    sys_l2_detail_t* fdb_detail_info)
{
    ctc_linklist_t* fdb_list = NULL;

    SYS_FDB_DBG_FUNC();

    query_rst->is_end = FALSE;

    fdb_list = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    if (NULL == fdb_list)
    {
        query_rst->is_end = TRUE;
        return 0;
    }

    return _sys_goldengate_l2_get_entry_by_list(lchip, fdb_list, query_flag, query_rst, fdb_detail_info, 0, 0, 0);
}

STATIC int32
_sys_goldengate_l2_get_entry_by_hw(uint8 lchip, uint8 query_flag,
                                   uint8 acc_type,
                                   ctc_l2_fdb_query_t* query_rq,
                                   ctc_l2_fdb_query_rst_t* query_rst,
                                   sys_l2_detail_t* fdb_detail_info)
{
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;
    uint32 max_index = 0;
    uint32 threshold_cnt = query_rst->buffer_len/sizeof(ctc_l2_addr_t);
    sys_dma_info_t dma_info;
    int32 ret = 0;
    uint8 is_end = 0;
    ctc_l2_addr_t*    p_l2_addr = query_rst->buffer;
    SYS_FDB_DBG_FUNC();

    query_rq->count = 0;
    sal_memset(&in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&dma_info, 0, sizeof(sys_dma_info_t));

    dma_info.p_data = (DmaFibDumpFifo_m*)mem_malloc(MEM_FDB_MODULE,
            sizeof(DmaFibDumpFifo_m)*threshold_cnt);
    if (dma_info.p_data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(dma_info.p_data, 0, sizeof(DmaFibDumpFifo_m)*threshold_cnt);

    L2_DUMP_LOCK;
    /* set request for fib acc to dump hash key by fid */
    in.dump.fid = query_rq->fid;
    in.dump.port = query_rq->use_logic_port ? query_rq->gport : SYS_MAP_CTC_GPORT_TO_DRV_GPORT(query_rq->gport);
    in.dump.flag = query_flag;
    in.dump.min_index = query_rst->start_index;
    in.dump.threshold_cnt = ((threshold_cnt>=16*1024)?16*1024:threshold_cnt);

    sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost0MacHashKey_t, &max_index);
    max_index += 32;
    in.dump.max_index = max_index;

    /**/
    CTC_ERROR_GOTO(drv_goldengate_fib_acc(lchip, acc_type, &in, &out),ret, error);

    do
    {
        if (out.dump.count == 0)
        {
            /* Even if count is 0, but Dma memory still may have invalid entry, need clear, Eg:the last invalid entry */
            SYS_FDB_DBG_INFO("ACC dump count is 0! Clear DMA chan\n");
            query_rst->is_end = 1;
            ret = CTC_E_NONE;
            ret += sys_goldengate_dma_wait_desc_done(lchip, SYS_DMA_HASHKEY_CHAN_ID);
            goto error;
        }

        /* get dump result from dma interface */
        CTC_ERROR_GOTO(sys_goldengate_dma_sync_hash_dump(lchip, &dma_info, threshold_cnt, out.dump.count, out.dump.dma_full),ret, error);

        p_l2_addr = query_rst->buffer + query_rq->count;
        CTC_ERROR_GOTO(_sys_goldengate_l2_map_dma_to_entry(lchip, &dma_info, p_l2_addr, fdb_detail_info),ret, error);
        query_rq->count += dma_info.entry_num;
        threshold_cnt -= dma_info.entry_num;

        if (out.dump.continuing)
        {
            query_rst->is_end = 0;
            query_rst->next_query_index = out.dump.last_index + 1;
            is_end = 1;
        }
        else
        {
            if (out.dump.dma_full)
            {
                SYS_FDB_DBG_INFO("DMA fifo is full! Last index is:%d\n", out.dump.last_index);
                is_end = 0;
                in.dump.threshold_cnt = threshold_cnt;
                in.dump.min_index = out.dump.last_index;
                CTC_ERROR_GOTO(drv_goldengate_fib_acc(lchip, acc_type, &in, &out),ret, error);
            }
            else
            {
                query_rst->is_end = 1;
                is_end = 1;
            }
        }
    }while(is_end == 0);

    mem_free(dma_info.p_data);
    L2_DUMP_UNLOCK;
    return CTC_E_NONE;
error:
    sys_goldengate_dma_clear_chan_data(lchip, SYS_DMA_HASHKEY_CHAN_ID);
    mem_free(dma_info.p_data);
    L2_DUMP_UNLOCK;
    return ret;
}

STATIC uint32
_sys_goldengate_l2_get_entry_by_port_fid(uint8 lchip, uint8 query_flag,
                                         uint16 gport,
                                         uint16 fid,
                                         uint8 use_logic_port,
                                         ctc_l2_fdb_query_rst_t* pr,
                                         sys_l2_detail_t* pi)
{
    ctc_linklist_t* fdb_list = NULL;

    SYS_FDB_DBG_FUNC();

    fdb_list = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    if (NULL == fdb_list)
    {
        pr->is_end = TRUE;
        return 0;
    }

    return _sys_goldengate_l2_get_entry_by_list(lchip, fdb_list, query_flag, pr, pi, 1, gport, 0);
}


STATIC int32
_sys_goldengate_l2_get_entry_by_mac_cb(sys_l2_node_t* p_l2_node,
                                       sys_l2_mac_cb_t* mac_cb)
{
    uint8                  lchip = 0;
    uint32                 max_index   = 0;
    uint32                 start_index = 0;
    ctc_l2_fdb_query_rst_t * pr        = mac_cb->query_rst;
    sys_l2_detail_t        * pi        = mac_cb->fdb_detail_rst;
    if (NULL == p_l2_node || NULL == mac_cb)
    {
        return 0;
    }

    SYS_FDB_DBG_FUNC();

    lchip = mac_cb->lchip;

    start_index = pr->start_index;
    max_index   = pr->buffer_len / sizeof(ctc_l2_addr_t);

    if (mac_cb->count >= max_index)
    {
        return -1;
    }

    if (sal_memcmp(mac_cb->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        return 0;
    }

    if (pr->next_query_index++ < start_index)
    {
        return 0;
    }

    RETURN_IF_FLAG_NOT_MATCH(mac_cb->query_flag, p_l2_node->adptr->flag, p_l2_node->flag);
    _sys_goldengate_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &(mac_cb->count));

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_l2_get_entry_by_mac(uint8 lchip, uint8 query_flag,
                                    mac_addr_t mac,
                                    ctc_l2_fdb_query_rst_t* query_rst,
                                    sys_l2_detail_t* fdb_detail_info)
{
    sys_l2_mac_cb_t   mac_hash_info;
    int32             ret = 0;
    hash_traversal_fn fun = NULL;

    SYS_FDB_DBG_FUNC();

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    FDB_SET_MAC(mac_hash_info.l2_node.key.mac, mac);
    mac_hash_info.query_flag     = query_flag;
    mac_hash_info.query_rst      = query_rst;
    mac_hash_info.fdb_detail_rst = fdb_detail_info;
    mac_hash_info.lchip          = lchip;
    query_rst->next_query_index  = 0;

    fun = (hash_traversal_fn) _sys_goldengate_l2_get_entry_by_mac_cb;

    ret = ctc_hash_traverse2(pl2_gg_master[lchip]->mac_hash, fun, &(mac_hash_info.l2_node));
    if (ret > 0)
    {
        query_rst->is_end = 1;
    }

    return mac_hash_info.count;
}


STATIC uint32
_sys_goldengate_l2_get_entry_by_mac_fid(uint8 lchip, uint8 query_flag,
                                        mac_addr_t mac,
                                        uint16 fid,
                                        ctc_l2_fdb_query_rst_t* pr,
                                        sys_l2_detail_t* pi)
{
    sys_l2_node_t       * p_l2_node = NULL;
    uint32              count       = 0;

    SYS_FDB_DBG_FUNC();

 /*    max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);*/

    /* fid = 0xffff use for sys mac in tcam, so use fdb_hash to store */
    if (DONTCARE_FID == fid)
    {
        /*lookup black hole hash.*/
        sys_l2_tcam_t* p_tcam = NULL;
        p_tcam = _sys_goldengate_l2_tcam_hash_lookup(lchip, mac);

        if (p_tcam)
        {
            RETURN_IF_FLAG_NOT_MATCH(query_flag, p_tcam->flag.flag_ad, p_tcam->flag.flag_node);

            FDB_SET_MAC((pr->buffer[0]).mac, mac);
            (pr->buffer[0]).fid   = fid;
            (pr->buffer[0]).gport = DISCARD_PORT;
            if (pi)
            {
                pi[0].key_index = p_tcam->key_index;
                pi[0].ad_index  = p_tcam->ad_index;
                pi[0].pending   = 0;
                sal_memcpy(&pi[0].flag, &p_tcam->flag, sizeof(sys_l2_flag_t));
            }
        }
        pr->is_end = 1;
        return 1;
    }

/* l2uc, l2mc. */
    p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, mac, fid);
    if (p_l2_node)
    {
        RETURN_IF_FLAG_NOT_MATCH(query_flag, p_l2_node->adptr->flag, p_l2_node->flag);
        _sys_goldengate_l2_map_sw_to_entry(lchip, p_l2_node, pr, pi, &count);
    }

    pr->is_end = 1;
    return count;
}


STATIC int32
_sys_goldengate_l2_get_entry_by_mac_fid_hw(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                           ctc_l2_fdb_query_rst_t* pr,
                                           sys_l2_detail_t* pi)
{
    DsMac_m  ds_mac;
    mac_lookup_result_t rslt;

    SYS_FDB_DBG_FUNC();

    pq->count  = 0;
    pr->is_end = 1;

    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, pq->mac, pq->fid, &rslt));

    if ((rslt.hit) || (rslt.pending && pi)) /* if show detail, include pending entry */
    {
        uint8         timer = 0;
        sys_l2_info_t sys;
        sal_memset(&sys, 0, sizeof(sys_l2_info_t));

        _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &ds_mac);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &ds_mac, rslt.ad_index);
        CTC_ERROR_RETURN(sys_goldengate_aging_get_aging_timer(lchip, rslt.key_index, &timer));
        if(timer == 0)
        {
            sys.flag.flag_node.aging_disable = 1;
        }
        RETURN_IF_FLAG_NOT_MATCH(pq->query_flag, sys.flag.flag_ad, sys.flag.flag_node);

        (pr->buffer[0]).is_logic_port = sys.flag.flag_ad.logic_port;
        (pr->buffer[0]).fid   = pq->fid;
        (pr->buffer[0]).gport = sys.gport;
        FDB_SET_MAC((pr->buffer[0]).mac, pq->mac);

        _sys_goldengate_l2_unmap_flag(lchip, &pr->buffer[0].flag, &sys.flag);
        pq->count  = 1;
        if (rslt.pending)
        {
            CTC_SET_FLAG(pr->buffer[0].flag, CTC_L2_FLAG_PENDING_ENTRY);
        }
        if (pi)
        {
            pi[0].key_index = rslt.key_index;
            pi[0].ad_index  = rslt.ad_index;
            pi[0].pending   = rslt.pending;
            sal_memcpy(&pi[0].flag, &sys.flag, sizeof(sys_l2_flag_t));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_get_entry_by_all_cb(sys_l2_fid_node_t* pf, uint32 fid, sys_l2_all_cb_t* all_cb)
{

    ctc_l2_fdb_query_rst_t * query_rst       = all_cb->pr;
    sys_l2_detail_t        * fdb_detail_info = all_cb->pi;
    ctc_linklist_t         * fdb_list        = NULL;
    uint32                  max_count        = 0;
    uint8 lchip = 0;

    if (NULL == pf || NULL == all_cb)
    {
        return 0;
    }

    SYS_FDB_DBG_FUNC();
    lchip = all_cb->lchip;
    max_count = query_rst->buffer_len / sizeof(ctc_l2_addr_t);

    fdb_list = pf->fid_list;
    all_cb->fid   = fid;
    all_cb->count = _sys_goldengate_l2_get_entry_by_list(lchip, fdb_list, all_cb->query_flag, query_rst, fdb_detail_info, 0, 0, &all_cb->count);

    /*If traverse to next fid, should clear start index*/
    query_rst->start_index = 0;

    if(max_count == all_cb->count)
    {
        return -1;
    }
    return 0;
}

STATIC uint32
_sys_goldengate_l2_get_entry_by_all(uint8 lchip, uint8 query_flag,
                                    ctc_l2_fdb_query_rst_t* pr,
                                    sys_l2_detail_t* pi)
{
 /*    uint32          max_count      = pr->buffer_len / sizeof(ctc_l2_addr_t);*/
    sys_l2_all_cb_t all_cb;
    int32 ret  = 0;

    uint32 fid = 0;
    pr->is_end = 0;

    SYS_FDB_DBG_FUNC();

    sal_memset(&all_cb, 0, sizeof(all_cb));
    all_cb.pr         = pr;
    all_cb.pi         = pi;
    all_cb.query_flag = query_flag;
    all_cb.lchip      = lchip;

    fid             =   SYS_L2_DECODE_QUERY_FID(pr->start_index);
    pr->start_index =   SYS_L2_DECODE_QUERY_IDX(pr->start_index);

    ret = ctc_vector_traverse2(pl2_gg_master[lchip]->fid_vec,
                                    fid,
                                    (vector_traversal_fn2) _sys_goldengate_l2_get_entry_by_all_cb,
                                    &all_cb);

    if(ret < 0)
    {
        pr->next_query_index = SYS_L2_ENCODE_QUERY_IDX(all_cb.fid, pr->next_query_index);
        pr->is_end = 0;
    }
    else
    {
        pr->is_end = 1;
    }

    return all_cb.count;
}


/**
   @brief Query fdb enery according to specified query condition

 */
STATIC int32
_sys_goldengate_l2_query_flush_check(uint8 lchip, void* p, uint8 is_flush)
{
    uint8  type           = 0;
    uint16 fid            = 0;
    uint16 gport          = 0;
    uint8  use_logic_port = 0;

    SYS_FDB_DBG_FUNC();

    if (is_flush)
    {
        ctc_l2_flush_fdb_t* pf = (ctc_l2_flush_fdb_t *) p;
        type           = pf->flush_type;
        fid            = pf->fid;
        use_logic_port = pf->use_logic_port;
        gport          = pf->gport;
    }
    else
    {
        ctc_l2_fdb_query_t* pq = (ctc_l2_fdb_query_t *) p;
        type           = pq->query_type;
        fid            = pq->fid;
        use_logic_port = pq->use_logic_port;
        gport          = pq->gport;
    }

    if ((type == CTC_L2_FDB_ENTRY_OP_BY_VID) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN))
    {
        SYS_L2UC_FID_CHECK(fid); /* allow 0xFFFF */
    }

    if ((type == CTC_L2_FDB_ENTRY_OP_BY_PORT) ||
        (type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN))
    {
        if (!use_logic_port)
        {
            CTC_GLOBAL_PORT_CHECK(gport);
        }
        else
        {
            SYS_LOGIC_PORT_CHECK(gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_map_flag_to_hw(uint8 lchip, ctc_l2_fdb_query_t* p_src, uint8* flag, uint8* acc_type)
{
    uint8 flag_t = 0;
    uint8 acc_type_t = DRV_FIB_ACC_DUMP_MAC_BY_PORT_VLAN;

    if (p_src->query_type == CTC_L2_FDB_ENTRY_OP_ALL)
    {
        acc_type_t = DRV_FIB_ACC_DUMP_MAC_ALL;
    }
    else if (p_src->query_type == CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN)
    {
        acc_type_t = DRV_FIB_ACC_LKP_MAC;
    }
    else if (p_src->query_type == CTC_L2_FDB_ENTRY_OP_BY_VID)
    {
        flag_t |= DRV_FIB_ACC_DUMP_BY_VSI;
    }
    else if (p_src->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT)
    {
        flag_t |= DRV_FIB_ACC_DUMP_BY_PORT;
    }
    else if (p_src->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN)
    {
        flag_t |= (DRV_FIB_ACC_DUMP_BY_VSI|DRV_FIB_ACC_DUMP_BY_PORT);
    }
    else
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (p_src->use_logic_port)
    {
        flag_t |= DRV_FIB_ACC_DUMP_LOGIC_PORT;
    }
    if (p_src->query_flag == CTC_L2_FDB_ENTRY_STATIC)
    {
        flag_t |= DRV_FIB_ACC_DUMP_STATIC;
    }
    if (p_src->query_flag == CTC_L2_FDB_ENTRY_DYNAMIC)
    {
        flag_t |= DRV_FIB_ACC_DUMP_DYNAMIC;
    }
    if (p_src->query_flag == CTC_L2_FDB_ENTRY_ALL)
    {
        flag_t |= (DRV_FIB_ACC_DUMP_DYNAMIC|DRV_FIB_ACC_DUMP_STATIC);
    }
    if (p_src->query_flag == CTC_L2_FDB_ENTRY_MCAST)
    {
        flag_t |= (DRV_FIB_ACC_DUMP_DYNAMIC|DRV_FIB_ACC_DUMP_STATIC|DRV_FIB_ACC_DUMP_MCAST);
    }

    *flag = flag_t;
    *acc_type = acc_type_t;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_get_entry_by_sw(uint8 lchip, ctc_l2_fdb_query_t* pq,
                             ctc_l2_fdb_query_rst_t* pr,
                             sys_l2_detail_t* pi)
{
    uint8 flag = 0;
    uint8 is_vport = 0;
    uint32 rq_count   = pr->buffer_len / sizeof(ctc_l2_addr_t);

    SYS_FDB_DBG_FUNC();

    flag     = pq->query_flag;
    is_vport = pq->use_logic_port;
    pq->count = 0;

    switch (pq->query_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        pq->count = _sys_goldengate_l2_get_entry_by_fid(lchip, flag, pq->fid, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        pq->count = _sys_goldengate_l2_get_entry_by_port(lchip, flag, pq->gport, is_vport, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        pq->count = _sys_goldengate_l2_get_entry_by_port_fid(lchip, flag, pq->gport, pq->fid, is_vport, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        pq->count = _sys_goldengate_l2_get_entry_by_mac_fid(lchip, flag, pq->mac, pq->fid, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        pq->count = _sys_goldengate_l2_get_entry_by_mac(lchip, flag, pq->mac, pr, pi);
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
        pq->count = _sys_goldengate_l2_get_entry_by_all(lchip, flag, pr, pi);
        break;

    default:
        pq->count = 0;
        break;
    }

    /*cannot get enough entry means query end */
    if (rq_count > pq->count)
    {
        pr->is_end = TRUE;
    }

    return CTC_E_NONE;
}

typedef struct
{
    uint32                 count;
    ctc_l2_fdb_query_rst_t * pr;
    ctc_l2_fdb_query_t     * pq;
    sys_l2_detail_t        * pi;
    uint8                  lchip; /**< for tranvase speific lchip*/
    uint8                  rev[3];

}sys_l2_dump_mcast_cb_t;

STATIC int32
_sys_goldengate_l2_get_mcast_entry_cb(sys_l2_mcast_node_t* p_node, sys_l2_dump_mcast_cb_t* all_cb)
{
    ctc_l2_fdb_query_rst_t * pr              = NULL;
    ctc_l2_fdb_query_t     * pq              = NULL;
    sys_l2_detail_t        * detail_info     = NULL;
    uint32                  count            = 0;
    uint8                   lchip            = 0;
    uint32                  key_index        = 0;
    uint32                  max_count        = 0;
    ctc_l2_addr_t           l2_addr;
    int32                   ret = 0;
    sys_l2_detail_t         l2_detail;

    if (NULL == p_node || NULL == all_cb)
    {
        return 0;
    }

    pr              = all_cb->pr;
    pq              = all_cb->pq;
    detail_info     = all_cb->pi;
    sal_memset(&l2_addr, 0, sizeof(ctc_l2_addr_t));
    sal_memset(&l2_detail, 0, sizeof(sys_l2_detail_t));
    max_count = pr->buffer_len / sizeof(ctc_l2_addr_t);
    lchip = all_cb->lchip;
    key_index = p_node->key_index;

    all_cb->count++;
    if (all_cb->count <= pr->start_index)
    {
        return 0;
    }

    ret = sys_goldengate_l2_get_fdb_by_index(lchip, key_index, &l2_addr, &l2_detail);
    if (ret < 0)
    {
        return 0;
    }
    switch (pq->query_type)
    {
        case CTC_L2_FDB_ENTRY_OP_BY_VID:
            if (pq->fid != l2_addr.fid)
                return 0;
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_MAC:
            if (sal_memcmp(pq->mac, l2_addr.mac, sizeof(pq->mac)))
                return 0;
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
            if ((pq->fid != l2_addr.fid) || (sal_memcmp(pq->mac, l2_addr.mac, sizeof(pq->mac))))
                return 0;
            break;

        case CTC_L2_FDB_ENTRY_OP_ALL:
            break;

        default:
            return -1;
    }
    count = pq->count;
    pr->buffer[count].fid = l2_addr.fid;
    pr->buffer[count].flag = l2_addr.flag;
    FDB_SET_MAC(pr->buffer[count].mac, l2_addr.mac);
    if (detail_info)
    {
        sal_memcpy(&detail_info[count], &l2_detail, sizeof(sys_l2_detail_t));
    }
    pq->count++;
    if (pq->count >= max_count)
    {
        return -1;
    }

    return 0;
}

STATIC int32
_sys_goldengate_l2_get_mcast_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                               ctc_l2_fdb_query_rst_t* pr, sys_l2_detail_t* pi)
{
    sys_l2_dump_mcast_cb_t all_cb;
    int32 ret  = 0;
    pr->is_end = 0;
    pq->count  = 0;

    L2_LOCK;
    sal_memset(&all_cb, 0, sizeof(all_cb));
    all_cb.pr         = pr;
    all_cb.pq         = pq;
    all_cb.pi         = pi;
    all_cb.lchip      = lchip;

    pr->start_index =   SYS_L2_DECODE_QUERY_IDX(pr->start_index);
    ret = ctc_vector_traverse(pl2_gg_master[lchip]->mcast2nh_vec,
                                    (vector_traversal_fn) _sys_goldengate_l2_get_mcast_entry_cb,
                                    &all_cb);
    if(ret < 0)
    {
        pr->next_query_index = all_cb.count;
        pr->is_end = 0;
    }
    else
    {
        pr->is_end = 1;
    }

    if (pq->count == 0)
    {
        pr->is_end = 1;
    }
    L2_UNLOCK;
    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_l2_get_fdb_conflict(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                ctc_l2_fdb_query_rst_t* pr, sys_l2_detail_t* pi)
{
    int32 ret = 0;
    uint8 index = 0;
    uint16 buf_cnt = pr->buffer_len/sizeof(ctc_l2_addr_t);
    uint16 cf_max = 0;
    uint16 cf_count = 0;
    uint32 key_index = 0;
    sys_l2_calc_index_t calc_index;
    sys_l2_detail_t* l2_detail = NULL;
    ctc_l2_addr_t* cf_addr = NULL;
    sys_l2_detail_t* l2_detail_temp = NULL;
    ctc_l2_addr_t* cf_addr_temp = NULL;
    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);

    SYS_L2UC_FID_CHECK(pq->fid);

    if (pq->fid == DONTCARE_FID)
    {
        return CTC_E_NOT_SUPPORT;
    }
    pq->count = 0;

    sal_memset(&calc_index, 0, sizeof(sys_l2_calc_index_t));
    CTC_ERROR_RETURN(_sys_goldengate_l2_fdb_calc_index(lchip, pq->mac, pq->fid, &calc_index));
    cf_max = calc_index.index_cnt + SYS_L2_FDB_CAM_NUM;
    cf_addr = (ctc_l2_addr_t*)mem_malloc(MEM_FDB_MODULE, cf_max * sizeof(ctc_l2_addr_t));
    if (cf_addr == NULL)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(cf_addr, 0, cf_max * sizeof(ctc_l2_addr_t));

    l2_detail = (sys_l2_detail_t*)mem_malloc(MEM_FDB_MODULE, cf_max * sizeof(sys_l2_detail_t));
    if (l2_detail == NULL)
    {
        mem_free(cf_addr);
        return CTC_E_NO_MEMORY;
    }
    sal_memset(l2_detail, 0, cf_max * sizeof(sys_l2_detail_t));

    l2_detail_temp = l2_detail;
    cf_addr_temp = cf_addr;

    for (index = 0; index < cf_max; index++)
    {
        key_index = (index >= calc_index.index_cnt) ? (index - calc_index.index_cnt) : calc_index.key_index[index];
        ret = sys_goldengate_l2_get_fdb_by_index(lchip, key_index, cf_addr_temp, l2_detail_temp);
        if (ret < 0)
        {
            if (ret == CTC_E_ENTRY_NOT_EXIST)
            {
                ret = CTC_E_NONE;
                continue;
                 /*goto error_return;*/
            }
            else
            {
                goto error_return;
            }
        }
        cf_addr_temp++;
        l2_detail_temp++;
        cf_count++;
    }

    pr->next_query_index = pr->start_index + buf_cnt;
    if (pr->next_query_index >= cf_count)
    {
        pr->next_query_index = cf_count;
        pr->is_end = 1;
    }

    if (pr->start_index >= cf_count)
    {
        goto error_return;
    }
    pq->count = pr->next_query_index - pr->start_index;
    sal_memcpy(pr->buffer, cf_addr + pr->start_index, pq->count * sizeof(ctc_l2_addr_t));

    if (pi)
    {
        sal_memcpy(pi, l2_detail + pr->start_index, pq->count * sizeof(sys_l2_detail_t));
    }

    mem_free(cf_addr);
    mem_free(l2_detail);
    return CTC_E_NONE;

error_return:
    pr->is_end = 1;
    mem_free(cf_addr);
    mem_free(l2_detail);
    return ret;
}


int32
sys_goldengate_l2_get_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                             ctc_l2_fdb_query_rst_t* pr,
                             sys_l2_detail_t* pi)
{
    uint8 flag = 0;
    uint8 acc_type = 0;
    SYS_L2_INIT_CHECK(lchip);

    SYS_FDB_DBG_FUNC();

    if ((pq->query_flag == CTC_L2_FDB_ENTRY_MCAST) && !pl2_gg_master[lchip]->trie_sort_en)
    {
        if ((pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT)
            || (pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN))
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_goldengate_l2_get_mcast_entry(lchip, pq, pr, pi);
    }

    if (pq->query_flag == CTC_L2_FDB_ENTRY_CONFLICT)
    {
        if (pq->query_type != CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN)
        {
            return CTC_E_NOT_SUPPORT;
        }
        return _sys_goldengate_l2_get_fdb_conflict(lchip, pq, pr, pi);
    }

    if ((pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_VID)
    && (pq->fid == DONTCARE_FID))
    {
        return _sys_goldengate_l2_get_blackhole_entry(lchip, pq, pr, 0, pi);
    }

    CTC_ERROR_RETURN(_sys_goldengate_l2_query_flush_check(lchip, pq, 0));
    if ((pq->query_hw) || (pl2_gg_master[lchip]->cfg_hw_learn))
    {
        CTC_ERROR_RETURN(_sys_goldengate_l2_map_flag_to_hw(lchip, pq, &flag, &acc_type));
        if (DRV_FIB_ACC_LKP_MAC == acc_type)
        {
            CTC_ERROR_RETURN(_sys_goldengate_l2_get_entry_by_mac_fid_hw(lchip, pq,pr,pi));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_l2_get_entry_by_hw(lchip, flag,acc_type, pq, pr, pi));
        }
    }
    else
    {
        L2_LOCK;
        _sys_goldengate_l2_get_entry_by_sw(lchip, pq, pr, pi);
        L2_UNLOCK;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_get_fdb_entry(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                ctc_l2_fdb_query_rst_t* pr)
{
    SYS_FDB_DBG_FUNC();

    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);

    return sys_goldengate_l2_get_entry(lchip, pq, pr, NULL);
}

int32
sys_goldengate_l2_fdb_dump_all(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                    ctc_l2_fdb_query_rst_t* pr)
{
    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);
    pq->query_flag = CTC_L2_FDB_ENTRY_MCAST; /** means include ucast and mcast*/
    pq->query_type = CTC_L2_FDB_ENTRY_OP_ALL;
    pq->query_hw = 1;

    return sys_goldengate_l2_get_entry(lchip, pq, pr, NULL);
}

int32
sys_goldengate_l2_get_fdb_entry_detail(uint8 lchip, ctc_l2_fdb_query_t* pq,
                                       ctc_l2_fdb_query_rst_t* pr,
                                       sys_l2_detail_t* pi)
{
    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pq);
    CTC_PTR_VALID_CHECK(pr);

    if (!(pl2_gg_master[lchip]->cfg_hw_learn) &&(pq->query_hw))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return sys_goldengate_l2_get_entry(lchip, pq, pr, pi);
}


#define _FDB_COUNT_
/**
 * get some type fdb count by specified port,not include the default entry num
 */
uint32
_sys_goldengate_l2_get_count_by_port(uint8 lchip, uint8 query_flag, uint16 gport, uint8 use_logic_port)
{
    uint32            count       = 0;
    sys_l2_port_node_t* port_node = NULL;

    SYS_FDB_DBG_FUNC();

    if (!use_logic_port)
    {
        port_node = ctc_vector_get(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }
    else
    {
        port_node = ctc_vector_get(pl2_gg_master[lchip]->vport_vec, gport);
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        count = CTC_LISTCOUNT(port_node->port_list) - port_node->dynamic_count;
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = port_node->dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = port_node->local_dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_ALL:
        count = CTC_LISTCOUNT(port_node->port_list);
        break;

    default:
        break;
    }

    return count;
}

/**
 * get some type fdb count by specified vid,not include the default entry num
 */
uint32
_sys_goldengate_l2_get_count_by_fid(uint8 lchip, uint8 query_flag, uint16 fid)
{
    uint32           count      = 0;
    sys_l2_fid_node_t* fid_node = NULL;

    SYS_FDB_DBG_FUNC();

    fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    if (NULL == fid_node || NULL == fid_node->fid_list)
    {
        return 0;
    }

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        count = CTC_LISTCOUNT(fid_node->fid_list) - fid_node->dynamic_count;
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = fid_node->dynamic_count;
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = fid_node->local_dynamic_count;
        break;
    case CTC_L2_FDB_ENTRY_ALL:
        count = CTC_LISTCOUNT(fid_node->fid_list);

    default:
        break;
    }

    return count;
}

STATIC uint32
_sys_goldengate_l2_get_count_by_port_fid(uint8 lchip, uint8 query_flag, uint16 gport, uint16 fid, uint8 use_logic_port)
{
    uint32            count       = 0;
    ctc_listnode_t    * listnode  = NULL;
    ctc_linklist_t    * fdb_list  = NULL;
    sys_l2_node_t     * p_l2_node = NULL;
    sys_l2_port_node_t* port_node = NULL;

    SYS_FDB_DBG_FUNC();

    if (!use_logic_port)
    {
        port_node = ctc_vector_get(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }
    else
    {
        port_node = ctc_vector_get(pl2_gg_master[lchip]->vport_vec, gport);
        if (NULL == port_node || NULL == port_node->port_list)
        {
            return 0;
        }
    }

    fdb_list = port_node->port_list;

    switch (query_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (p_l2_node->adptr->flag.is_static && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }

        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (!p_l2_node->adptr->flag.is_static && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (!p_l2_node->flag.remote_dynamic &&
                !p_l2_node->adptr->flag.is_static
                && (p_l2_node->key.fid == fid))
            {
                count++;
            }
        }
        break;

    case CTC_L2_FDB_ENTRY_ALL:
        CTC_LIST_LOOP(fdb_list, p_l2_node, listnode)
        {
            if (p_l2_node->key.fid == fid)
            {
                count++;
            }
        }
        break;

    default:
        break;
    }

    return count;
}

/* prameter 1 is node stored in hash.
   prameter 2 is self-defined struct.*/
STATIC int32
_sys_goldengate_l2_get_count_by_mac_cb(sys_l2_node_t* p_l2_node, sys_l2_mac_cb_t* mac_hash_info)
{
    uint8 match = 0;
    if (NULL == p_l2_node || NULL == mac_hash_info)
    {
        return 0;
    }

    SYS_FDB_DBG_FUNC();

    if (sal_memcmp(mac_hash_info->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        match = 0;
    }
    else
    {
        switch (mac_hash_info->query_flag)
        {
        case CTC_L2_FDB_ENTRY_STATIC:
            match = p_l2_node->adptr->flag.is_static;
            break;

        case  CTC_L2_FDB_ENTRY_DYNAMIC:
            match = !p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
            match = (!p_l2_node->adptr->flag.is_static && !p_l2_node->flag.remote_dynamic);
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            match = 1;
            break;

        default:
            break;
        }
    }

    if (match)
    {
        mac_hash_info->count++;
    }
    return 0;
}

STATIC uint32
_sys_goldengate_l2_get_count_by_mac(uint8 lchip, uint8 query_flag, mac_addr_t mac)
{
    sys_l2_mac_cb_t mac_hash_info;

    SYS_FDB_DBG_FUNC();

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    sal_memcpy(mac_hash_info.l2_node.key.mac, mac, sizeof(mac_addr_t));
    mac_hash_info.query_flag = query_flag;
    mac_hash_info.lchip      = lchip;

    ctc_hash_traverse2(pl2_gg_master[lchip]->mac_hash,
                             (hash_traversal_fn) _sys_goldengate_l2_get_count_by_mac_cb,
                             &(mac_hash_info.l2_node));

    return mac_hash_info.count;
}

STATIC uint32
_sys_goldengate_l2_get_count_by_mac_fid(uint8 lchip, uint8 query_flag, mac_addr_t mac, uint16 fid)
{
    sys_l2_node_t * p_l2_node = NULL;
    uint8         count       = 0;

    SYS_FDB_DBG_FUNC();

    p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, mac, fid);

    if (!p_l2_node)
    {
        count = 0;
    }
    else
    {
        switch (query_flag)
        {
        case  CTC_L2_FDB_ENTRY_STATIC:
            count = p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_DYNAMIC:
            count = !p_l2_node->adptr->flag.is_static;
            break;

        case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
            count = (!p_l2_node->adptr->flag.is_static && !p_l2_node->flag.remote_dynamic);
            break;

        case CTC_L2_FDB_ENTRY_ALL:
            count = 1;
            break;

        default:
            break;
        }
    }

    return count;
}

/**
   @brief only count unicast FDB entries
 */
uint32
_sys_goldengate_l2_get_count_by_all(uint8 lchip, uint8 query_flag)
{
    uint32 count = 0;

    SYS_FDB_DBG_FUNC();

    switch (query_flag)
    {
    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        count = pl2_gg_master[lchip]->dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        count = pl2_gg_master[lchip]->local_dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_STATIC:
        count = pl2_gg_master[lchip]->total_count - pl2_gg_master[lchip]->dynamic_count;
        return count;

    case CTC_L2_FDB_ENTRY_ALL:
        count = pl2_gg_master[lchip]->total_count;
        return count;

    default:
        break;
    }

    return count;
}

/**
 * get some type fdb count, not include the default entry num
 */
int32
sys_goldengate_l2_get_fdb_count(uint8 lchip, ctc_l2_fdb_query_t* pq)
{
    ctc_security_learn_limit_t limit;
    mac_lookup_result_t rslt;
    sys_l2_info_t sys;
    DsMac_m  ds_mac;
    uint32 all_count = 0;
    uint32 cmd = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pq);
    CTC_MAX_VALUE_CHECK(pq->query_flag, MAX_CTC_L2_FDB_ENTRY_OP - 1);

    SYS_FDB_DBG_FUNC();

    if ((pq->query_type == CTC_L2_FDB_ENTRY_OP_BY_VID)
    && (pq->fid == DONTCARE_FID))
    {
        ctc_l2_fdb_query_rst_t query_rst;
        sal_memset(&query_rst, 0, sizeof(query_rst));
        query_rst.buffer_len = 128 * sizeof(ctc_l2_addr_t);
        return _sys_goldengate_l2_get_blackhole_entry(lchip, pq, &query_rst, 1, NULL);
    }


    CTC_ERROR_RETURN(_sys_goldengate_l2_query_flush_check(lchip, pq, 0));

    pq->count = 0;

    sal_memset(&limit, 0, sizeof(limit));
    if ((pq->query_hw) || (pl2_gg_master[lchip]->cfg_hw_learn))/* this mode only get dynamic, except query_type is CTC_L2_FDB_ENTRY_OP_ALL .*/
    {
        switch (pq->query_type)
        {
        case CTC_L2_FDB_ENTRY_OP_BY_VID:
            limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN;
            limit.vlan = pq->fid;

            if (pl2_gg_master[lchip]->static_fdb_limit)
            {
                if (pq->query_flag != CTC_L2_FDB_ENTRY_ALL)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }
            else
            {
                if (pq->query_flag != CTC_L2_FDB_ENTRY_DYNAMIC)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }

            break;

        case CTC_L2_FDB_ENTRY_OP_BY_PORT:
            if (pq->use_logic_port)
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }

            if (pl2_gg_master[lchip]->static_fdb_limit)
            {
                if (pq->query_flag != CTC_L2_FDB_ENTRY_ALL)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }
            else
            {
                if (pq->query_flag != CTC_L2_FDB_ENTRY_DYNAMIC)
                {
                    return CTC_E_FEATURE_NOT_SUPPORT;
                }
            }

            limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_PORT;
            limit.gport = pq->gport;

            break;

        case CTC_L2_FDB_ENTRY_OP_ALL:
            limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM;
            if ((pq->query_flag == CTC_L2_FDB_ENTRY_STATIC) || (pq->query_flag == CTC_L2_FDB_ENTRY_ALL))
            {
                cmd = DRV_IOR(MacLimitSystem_t, MacLimitSystem_profileRunningCount_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &all_count));
            }
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
            CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, pq->mac, pq->fid, &rslt));
            sal_memset(&sys, 0, sizeof(sys_l2_info_t));
            _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &ds_mac);
            _sys_goldengate_l2_decode_dsmac(lchip, &sys, &ds_mac, rslt.ad_index);

            RETURN_IF_FLAG_NOT_MATCH(pq->query_flag, sys.flag.flag_ad, sys.flag.flag_node);
            pq->count = (rslt.hit);

            return CTC_E_NONE; /* return */

        default:
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
        CTC_ERROR_RETURN(sys_goldengate_mac_security_get_learn_limit(lchip, &limit, &pq->count));

        if((pq->query_type == CTC_L2_FDB_ENTRY_OP_ALL) && (pq->query_flag != CTC_L2_FDB_ENTRY_DYNAMIC))
        {
            if(pq->query_flag == CTC_L2_FDB_ENTRY_ALL)
            {
                pq->count = all_count;
            }
            else if(pq->query_flag == CTC_L2_FDB_ENTRY_STATIC)
            {
                pq->count = all_count - pq->count;
            }
        }
    }
    else
    {
        L2_LOCK;
        switch (pq->query_type)
        {
        case CTC_L2_FDB_ENTRY_OP_BY_VID:
            pq->count = _sys_goldengate_l2_get_count_by_fid(lchip, pq->query_flag, pq->fid);
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_PORT:

            pq->count = _sys_goldengate_l2_get_count_by_port(lchip, pq->query_flag, pq->gport, pq->use_logic_port);
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
            pq->count = _sys_goldengate_l2_get_count_by_port_fid(lchip, pq->query_flag, pq->gport, pq->fid, pq->use_logic_port);
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
            pq->count = _sys_goldengate_l2_get_count_by_mac_fid(lchip, pq->query_flag, pq->mac, pq->fid);
            break;

        case CTC_L2_FDB_ENTRY_OP_BY_MAC:
            pq->count = _sys_goldengate_l2_get_count_by_mac(lchip, pq->query_flag, pq->mac);
            break;

        case CTC_L2_FDB_ENTRY_OP_ALL:
            pq->count = _sys_goldengate_l2_get_count_by_all(lchip, pq->query_flag);
            break;

        default:
            L2_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        L2_UNLOCK;

    }


    return CTC_E_NONE;
}


#define  _SW_ADD_REMOVE_

STATIC int32
_sys_goldengate_l2_spool_remove(uint8 lchip, sys_l2_ad_spool_t* pa, uint8* freed)
{
    sys_l2_ad_spool_t * p_del = NULL;
    int32       ret     = CTC_E_NONE;

    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pa);
    SYS_FDB_DBG_ERROR("dsfwdptr =%d\n", pa->nhid);

    SYS_FDB_DBG_FUNC();

    p_del = ctc_spool_lookup(pl2_gg_master[lchip]->ad_spool, pa);
    if (!p_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(pl2_gg_master[lchip]->ad_spool, pa, NULL);

    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, p_del->index));
        pl2_gg_master[lchip]->alloc_ad_cnt--;

        SYS_FDB_DBG_INFO("remove from spool, free ad_index: 0x%x\n", p_del->index);
        if (freed)
        {
            *freed = 1;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_spool_add(uint8 lchip, sys_l2_info_t* psys,
                             sys_l2_ad_spool_t* pa_old,
                             sys_l2_ad_spool_t* pa_new,
                             sys_l2_ad_spool_t** pa_get,
                             uint32* ad_index)
{
    int32 ret = 0;

    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(psys);

    ret = ctc_spool_add(pl2_gg_master[lchip]->ad_spool, pa_new, NULL, pa_get);

    if (FAIL(ret))
    {
        return CTC_E_SPOOL_ADD_UPDATE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            /* allocate new ad index */
            ret = sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, ad_index);
            if (FAIL(ret))
            {
                ctc_spool_remove(pl2_gg_master[lchip]->ad_spool, *pa_get, NULL);
                return ret;
            }

            pa_new->index = *ad_index; /*set ad_index to new pa*/
            pl2_gg_master[lchip]->alloc_ad_cnt++;
        }
        else
        {
            *ad_index = (*pa_get)->index; /* get old_adindex*/
        }
        SYS_FDB_DBG_INFO("build new hash ucast adindex:0x%x\n", *ad_index);
    }

    return CTC_E_NONE;
}

/* for unicast and mcast.
 * ad index will set back to psys.
 */
STATIC int32
_sys_goldengate_l2_build_dsmac_index(uint8 lchip, sys_l2_info_t* psys,
                                     sys_l2_ad_spool_t* pa_old,
                                     sys_l2_ad_spool_t* pa_new,
                                     sys_l2_ad_spool_t** pa_get)
{
    int32  ret      = 0;
    uint32 ad_index = 0;

    CTC_PTR_VALID_CHECK(psys);
    SYS_FDB_DBG_FUNC();

    if (!psys->flag.flag_node.rsv_ad_index)
    {
        ret = _sys_goldengate_l2_spool_add(lchip, psys, pa_old, pa_new, pa_get, &ad_index);
        psys->ad_index = ad_index;
    }
    else
    {
        if (pl2_gg_master[lchip]->cfg_has_sw)
        {
            *pa_get = pa_new;  /* reserve index. so pa_get = pa_new. */
        }
    }

    return ret;
}


/* has_sw = 1 : pa != NULL
 * has_sw = 0 : pa = NULL
 */
STATIC int32
_sys_goldengate_l2_free_dsmac_index(uint8 lchip, sys_l2_info_t* psys, sys_l2_ad_spool_t* pa, sys_l2_flag_node_t*  pn, uint8* freed)
{
    int32  ret = CTC_E_NONE;
    uint8  is_rsv;

    SYS_FDB_DBG_FUNC();

    if (freed)
    {
        *freed = 0;
    }
    if (pa && pn)
    {
        is_rsv   = pn->rsv_ad_index;
    }
    else
    {
        CTC_PTR_VALID_CHECK(psys);
        is_rsv   = psys->flag.flag_node.rsv_ad_index;
    }

    /* for discard fdb */
    if (!is_rsv)
    {
        ret = _sys_goldengate_l2_spool_remove(lchip, pa, freed);
    }
    else if (freed)
    {
        *freed = 1;
    }

    return ret;
}


/* normal fdb hash opearation*/
STATIC int32
_sys_goldengate_l2_fdb_hash_add(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_insert(pl2_gg_master[lchip]->fdb_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_fdb_hash_remove(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (!ctc_hash_remove(pl2_gg_master[lchip]->fdb_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }
    return CTC_E_NONE;
}

/*fdb fid entry hash opearation*/
uint32
_sys_goldengate_l2_mac_hash_make(sys_l2_node_t* backet)
{
    return ctc_hash_caculate(CTC_ETH_ADDR_LEN, backet->key.mac);
}

/*fdb mac entry hash opearation*/
STATIC int32
_sys_goldengate_l2_mac_hash_add(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_insert(pl2_gg_master[lchip]->mac_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_mac_hash_remove(uint8 lchip, sys_l2_node_t* p_fdb_node)
{
    if (NULL == ctc_hash_remove(pl2_gg_master[lchip]->mac_hash, p_fdb_node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }

    return CTC_E_NONE;
}


uint32
_sys_goldengate_l2_tcam_hash_make(sys_l2_tcam_t* node)
{
    return ctc_hash_caculate(sizeof(sys_l2_tcam_key_t), &(node->key));
}

bool
_sys_goldengate_l2_tcam_hash_compare(sys_l2_tcam_t* stored_node, sys_l2_tcam_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return TRUE;
    }

    if (!sal_memcmp(&stored_node->key, &lkup_node->key, sizeof(sys_l2_tcam_key_t)))
    {
        return TRUE;
    }

    return FALSE;
}


uint32
_sys_goldengate_l2_fdb_hash_make(sys_l2_node_t* node)
{
    return ctc_hash_caculate(sizeof(sys_l2_key_t), &(node->key));
}

bool
_sys_goldengate_l2_fdb_hash_compare(sys_l2_node_t* stored_node, sys_l2_node_t* lkup_node)
{
    if (!stored_node || !lkup_node)
    {
        return TRUE;
    }

    if (!sal_memcmp(stored_node->key.mac, lkup_node->key.mac, CTC_ETH_ADDR_LEN)
        && (stored_node->key.fid == lkup_node->key.fid))
    {
        return TRUE;
    }

    return FALSE;
}

/* share pool for uc only. and us distinguished by fwdptr */
uint32
_sys_goldengate_l2_ad_spool_hash_make(sys_l2_ad_spool_t* p_ad)
{
        return ctc_hash_caculate((sizeof(p_ad->nhid) + sizeof(uint16)+ sizeof(uint16)+ sizeof(uint16)),
                             &(p_ad->nhid));
}

int
_sys_goldengate_l2_ad_spool_hash_cmp(sys_l2_ad_spool_t* p_ad1, sys_l2_ad_spool_t* p_ad2)
{
    SYS_FDB_DBG_FUNC();

    if (!p_ad1 || !p_ad2)
    {
        return FALSE;
    }

    /*first check flag, if flag not match,cannot use shared ad*/
    if (sal_memcmp(&p_ad1->flag, &p_ad2->flag, sizeof(uint16)))
    {
        return FALSE;
    }

    if ((p_ad1->nhid == p_ad2->nhid)
    && (p_ad1->gport == p_ad2->gport)
    && (p_ad1->dest_gport == p_ad2->dest_gport))
    {
        return TRUE;
    }

    return FALSE;
}


STATIC int32
_sys_goldengate_l2_tcam_hash_add(uint8 lchip, sys_l2_tcam_t * p_tcam_node)
{
    if (NULL == ctc_hash_insert(pl2_gg_master[lchip]->tcam_hash, p_tcam_node))
    {
        return CTC_E_FDB_HASH_INSERT_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_tcam_hash_remove(uint8 lchip, sys_l2_tcam_t * p_tcam_node)
{
    if (NULL == ctc_hash_remove(pl2_gg_master[lchip]->tcam_hash, p_tcam_node))
    {
        return CTC_E_FDB_HASH_REMOVE_FAILED;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_l2_port_list_add(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    int32             ret    = 0;
    sys_l2_port_node_t* port = NULL;
    ctc_vector_t      * pv   = NULL;
    uint16 tmp_gport = 0;

    CTC_PTR_VALID_CHECK(p_l2_node);

    SYS_FDB_DBG_FUNC();

    if (!p_l2_node->adptr->flag.logic_port)
    {
        if (DISCARD_PORT == p_l2_node->adptr->gport)
        {
            return CTC_E_NONE;
        }
        pv = pl2_gg_master[lchip]->gport_vec;
        tmp_gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport);
    }
    else
    {
        pv = pl2_gg_master[lchip]->vport_vec;
        tmp_gport = p_l2_node->adptr->gport;
    }

    port = ctc_vector_get(pv, tmp_gport);
    if (NULL == port)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, port, sizeof(sys_l2_port_node_t));
        if (NULL == port)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_0);
        }

        port->port_list = ctc_list_new();
        if (NULL == port->port_list)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_1);
        }

        ctc_vector_add(pv, tmp_gport, port);
    }

    p_l2_node->port_listnode = ctc_listnode_add_tail(port->port_list, p_l2_node);
    if (NULL == p_l2_node->port_listnode)
    {
        CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_2);
    }

    _sys_goldengate_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, 1, L2_COUNT_PORT_LIST, port);

    return CTC_E_NONE;

 error_2:
    ctc_list_delete(port->port_list);
 error_1:
    mem_free(port);
 error_0:
    return ret;
}

STATIC int32
_sys_goldengate_l2_port_list_remove(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    ctc_linklist_t    * fdb_list      = NULL;
    sys_l2_port_node_t* port_fdb_node = NULL;
    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(p_l2_node->port_listnode);

    SYS_FDB_DBG_FUNC();

    if (!p_l2_node->adptr->flag.logic_port)
    {
        if (DISCARD_PORT == p_l2_node->adptr->gport)
        {
            return CTC_E_NONE;
        }
        port_fdb_node = ctc_vector_get(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport));
    }
    else
    {
        port_fdb_node = ctc_vector_get(pl2_gg_master[lchip]->vport_vec, p_l2_node->adptr->gport);
    }

    if (!port_fdb_node || !port_fdb_node->port_list)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    fdb_list = port_fdb_node->port_list;

    _sys_goldengate_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag,  -1, L2_COUNT_PORT_LIST, port_fdb_node);

    ctc_listnode_delete_node(fdb_list, p_l2_node->port_listnode);
    p_l2_node->port_listnode = NULL;

    /* Assert adindex by port not exist in ad spool*/

    if (0 == CTC_LISTCOUNT(fdb_list))
    {
        ctc_list_free(fdb_list);
        mem_free(port_fdb_node);
        if (!p_l2_node->adptr->flag.logic_port)
        {
            ctc_vector_del(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_l2_node->adptr->gport));
        }
        else
        {
            ctc_vector_del(pl2_gg_master[lchip]->vport_vec, p_l2_node->adptr->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_fid_list_add(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    sys_l2_fid_node_t* fid = NULL;
    int32            ret   = 0;

    CTC_PTR_VALID_CHECK(p_l2_node);
    SYS_FDB_DBG_FUNC();

    if (p_l2_node->key.fid == SYS_L2_MAX_FID)
    {
        return CTC_E_NONE;
    }

    fid = _sys_goldengate_l2_fid_node_lkup(lchip, p_l2_node->key.fid, GET_FID_NODE);
    if (NULL == fid) /* for new implementation. this logic will never hit.*/
    {
        MALLOC_ZERO(MEM_FDB_MODULE, fid, sizeof(sys_l2_fid_node_t));
        if (NULL == fid)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_0);
        }

        fid->fid_list = ctc_list_new();
        if (NULL == fid->fid_list)
        {
            CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_1);
        }

        fid->fid = p_l2_node->key.fid;
        ctc_vector_add(pl2_gg_master[lchip]->fid_vec, p_l2_node->key.fid, fid);
    }
    p_l2_node->vlan_listnode = ctc_listnode_add_tail(fid->fid_list, p_l2_node);
    if (NULL == p_l2_node->vlan_listnode)
    {
        CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, error_2);
    }

    _sys_goldengate_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, 1, L2_COUNT_FID_LIST, fid);

    return CTC_E_NONE;
 error_2:
    ctc_list_delete(fid->fid_list);
 error_1:
    mem_free(fid);
 error_0:
    return ret;
}

STATIC int32
_sys_goldengate_l2_fid_list_remove(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    sys_l2_fid_node_t* fid_node = NULL;
    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(p_l2_node->vlan_listnode);
    SYS_FDB_DBG_FUNC();

    if (p_l2_node->key.fid == SYS_L2_MAX_FID)
    {
        return CTC_E_NONE;
    }

    fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, p_l2_node->key.fid, GET_FID_NODE);
    if (NULL == fid_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    _sys_goldengate_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, -1, L2_COUNT_FID_LIST, fid_node);

    ctc_listnode_delete_node(fid_node->fid_list, p_l2_node->vlan_listnode);
    p_l2_node->vlan_listnode = NULL;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_mc2nhop_add(uint8 lchip, uint16 l2mc_grp_id, uint32 nhp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_l2_mcast_node_t));
        if (NULL == p_node)
        {
            return CTC_E_NO_MEMORY;
        }

        p_node->nhid = nhp_id;
        p_node->ref_cnt = 1;
        ctc_vector_add(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_grp_id, p_node);
    }
    else
    {
        p_node->nhid = nhp_id;
        p_node->ref_cnt++;

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_mc2nhop_remove(uint8 lchip, uint16 l2mc_grp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        p_node->ref_cnt--;
        if(p_node->ref_cnt == 0)
        {
            ctc_vector_del(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_grp_id);
            mem_free(p_node);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_get_nhop_by_mc_grp_id(uint8 lchip, uint16 l2mc_grp_id, uint32* p_nhp_id)
{
    sys_l2_mcast_node_t *p_node = NULL;

    p_node = ctc_vector_get(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_grp_id);
    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
       *p_nhp_id = p_node->nhid;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_l2_bind_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id)
{
    sys_l2_info_t         sys;
    sys_l2_vport_2_nhid_t *p_node;
    uint32               dsfwd_offset = 0;
    DsMac_m              ds_mac;
    uint32                 cmd     = 0;
    sys_nh_info_dsnh_t p_nhinfo;
    int32                ret = 0;
    uint8                is_new_node = 0;
    uint32               old_nh_id = 0;

    p_node = ctc_vector_get(pl2_gg_master[lchip]->vport2nh_vec, logic_port);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_l2_vport_2_nhid_t));
        if (NULL == p_node)
        {
            return CTC_E_NO_MEMORY;
        }

        if (pl2_gg_master[lchip]->vp_alloc_ad_en)
        {
            ret = sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, &p_node->ad_idx);
            if (ret < 0)
            {
                mem_free(p_node);
                return ret;
            }
            SYS_FDB_DBG_INFO("alloc ad index:0x%x\n", p_node->ad_idx);
            pl2_gg_master[lchip]->alloc_ad_cnt++;
        }
        else
        {
            p_node->ad_idx = pl2_gg_master[lchip]->base_vport + logic_port;
        }
        p_node->nhid = nhp_id;
        ctc_vector_add(pl2_gg_master[lchip]->vport2nh_vec, logic_port, p_node);
        is_new_node = 1;
    }
    else
    {
        old_nh_id = p_node->nhid;
        p_node->nhid = nhp_id;
    }
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&p_nhinfo, 0, sizeof(sys_nh_info_dsnh_t));

    CTC_ERROR_GOTO(sys_goldengate_nh_get_nhinfo(lchip, nhp_id, &p_nhinfo), ret, error_return);
    if (p_nhinfo.ecmp_valid)
    {
        sys.flag.flag_node.ecmp_valid = 1;
        sys.dest_id = p_nhinfo.ecmp_group_id;
        SYS_FDB_DBG_INFO("logic_port:0x%x, nhid: %d ecmp_group:%d\n", logic_port, nhp_id, p_nhinfo.ecmp_group_id);
    }
    else if (p_nhinfo.dsfwd_valid)
    {
        sys.fwd_ptr = p_nhinfo.dsfwd_offset;
        SYS_FDB_DBG_INFO("logic_port:0x%x, nhid: %d fwd_ptr[0]:0x%x\n", logic_port, nhp_id, p_nhinfo.dsfwd_offset);
    }
    else
    {
        CTC_ERROR_GOTO(sys_goldengate_nh_get_dsfwd_offset(lchip, nhp_id, &dsfwd_offset), ret, error_return);
        sys.fwd_ptr = dsfwd_offset;
        SYS_FDB_DBG_INFO("logic_port:0x%x, nhid: %d fwd_ptr[0]:0x%x\n", logic_port, nhp_id, dsfwd_offset);
    }
    /* write binding logic-port dsmac */
    sal_memset(&ds_mac, 0, sizeof(DsMac_m));
    sys.gport           = logic_port;
    sys.flag.flag_ad.logic_port = 1;
    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);

    CTC_ERROR_GOTO(_sys_goldengate_l2_encode_dsmac(lchip, &ds_mac, &sys), ret, error_return);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_node->ad_idx, cmd, &ds_mac), ret, error_return);
    return CTC_E_NONE;

error_return:
    if(is_new_node)
    {
        ctc_vector_del(pl2_gg_master[lchip]->vport2nh_vec, logic_port);
        sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, p_node->ad_idx);
        mem_free(p_node);
    }
    else
    {
        p_node->nhid = old_nh_id;
    }
    return ret;
}


#define _FDB_FLUSH_
STATIC int32
_sys_goldengate_l2uc_remove_sw(uint8 lchip, sys_l2_node_t* pn)
{
    SYS_FDB_DBG_FUNC();

    _sys_goldengate_l2_port_list_remove(lchip, pn);
    _sys_goldengate_l2_fid_list_remove(lchip, pn);
    _sys_goldengate_l2_mac_hash_remove(lchip, pn);
    _sys_goldengate_l2_fdb_hash_remove(lchip, pn);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2uc_remove_sw_for_flush(uint8 lchip, sys_l2_node_t* pn)
{
    uint8 freed = 0;
    _sys_goldengate_l2uc_remove_sw(lchip, pn);
    _sys_goldengate_l2_free_dsmac_index(lchip, NULL, pn->adptr, &pn->flag, &freed);
    _sys_goldengate_l2_update_count(lchip, &pn->adptr->flag, &pn->flag, -1, L2_COUNT_GLOBAL, &pn->key_index);
    if(freed)
    {
        mem_free(pn->adptr);
    }
    mem_free(pn);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2uc_remove_sw_for_flush_traverse(uint8 lchip, sys_l2_node_t* pn)
{
    uint8 freed = 0;
    _sys_goldengate_l2_port_list_remove(lchip, pn);
    _sys_goldengate_l2_fid_list_remove(lchip, pn);
    _sys_goldengate_l2_mac_hash_remove(lchip, pn);
    _sys_goldengate_l2_free_dsmac_index(lchip, NULL, pn->adptr, &pn->flag, &freed);
    _sys_goldengate_l2_update_count(lchip, &pn->adptr->flag, &pn->flag, -1, L2_COUNT_GLOBAL, &pn->key_index);
    if(freed)
    {
        mem_free(pn->adptr);
    }
    mem_free(pn);
    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_l2_flush_fdb_cb(sys_l2_node_t* p_l2_node, sys_l2_flush_t* pf)
{
    uint8 lchip = 0;
    uint8 mac_limit_en = 0;
    uint8 is_dynamic = 0;
    CTC_PTR_VALID_CHECK(p_l2_node);
    CTC_PTR_VALID_CHECK(pf);

    if (p_l2_node->flag.system_mac ||
        p_l2_node->flag.type_l2mc)
    {
        return 0;
    }
    lchip = pf->lchip;
    RETURN_IF_FLAG_NOT_MATCH(pf->flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

    SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag, is_dynamic);
    mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
    if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
    {
        mac_limit_en |= (p_l2_node->adptr->flag.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                        | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;

        if (is_dynamic)
        {
            mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
        }

    }

    if (0 == pf->flush_fdb_cnt_per_loop)
    {
        return 0;
    }

    sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid, mac_limit_en);
    _sys_goldengate_l2uc_remove_sw_for_flush_traverse(lchip, p_l2_node);

    if ((pf->flush_fdb_cnt_per_loop) > 0)
    {
        --pf->flush_fdb_cnt_per_loop;
    }

    SYS_FDB_FLUSH_SLEEP(pf->sleep_cnt);

    return 1;
}

STATIC int32
_sys_goldengate_l2_flush_by_all(uint8 lchip, uint8 flush_flag)
{
    sys_l2_flush_t flush_info;

    sal_memset(&flush_info, 0, sizeof(flush_info));
    flush_info.flush_fdb_cnt_per_loop
                       = pl2_gg_master[lchip]->cfg_flush_cnt ? pl2_gg_master[lchip]->cfg_flush_cnt : CTC_MAX_UINT32_VALUE;
    flush_info.flush_flag = flush_flag;
    flush_info.lchip = lchip;

    ctc_hash_traverse_remove(pl2_gg_master[lchip]->fdb_hash,
                             (hash_traversal_fn) _sys_goldengate_l2_flush_fdb_cb,
                             &flush_info);

    if (0 == pl2_gg_master[lchip]->cfg_flush_cnt || flush_info.flush_fdb_cnt_per_loop > 0)
    {
        return CTC_E_NONE;
    }

    return CTC_E_FDB_OPERATION_PAUSE;
}

/**
   @brief flush fdb entry by port
 */
STATIC int32
_sys_goldengate_l2_flush_by_port(uint8 lchip, uint16 gport, uint32 flush_flag, uint8 use_logic_port)
{
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;
    ctc_linklist_t     * fdb_list             = NULL;
    sys_l2_node_t      * p_l2_node            = NULL;
    sys_l2_port_node_t * port                 = NULL;
    int32              ret                    = 0;
    uint32             flush_fdb_cnt_per_loop = 0;
    uint8              mac_limit_en           = 0;
    uint8              is_dynamic       = 0;
    uint16             sleep_cnt = 0;

    if (!use_logic_port)
    {
        port = ctc_vector_get(pl2_gg_master[lchip]->gport_vec, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
    }
    else
    {
        port = ctc_vector_get(pl2_gg_master[lchip]->vport_vec, gport);
    }

    if (!port || !port->port_list)
    {
        return CTC_E_NONE;
    }

    fdb_list = port->port_list;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);
    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag,is_dynamic);
        mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
        {
            mac_limit_en |= (use_logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                            | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;

            if (is_dynamic)
            {
                mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid, mac_limit_en));
        _sys_goldengate_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by port+vlan
 */
STATIC int32
_sys_goldengate_l2_flush_by_port_fid(uint8 lchip, uint16 gport, uint16 fid, uint32 flush_flag, uint8 use_logic_port)
{
    ctc_listnode_t * next_node      = NULL;
    ctc_listnode_t * node           = NULL;
    ctc_linklist_t * fdb_list       = NULL;
    sys_l2_node_t  * p_l2_node      = NULL;
    int32          ret              = 0;
    uint32         flush_fdb_cnt_per_loop;
    uint8          mac_limit_en     = 0;
    uint8          is_dynamic = 0;
    uint16         sleep_cnt = 0;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);

    fdb_list = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        if (use_logic_port && (!p_l2_node->adptr->flag.logic_port))
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        if (p_l2_node->adptr->gport != gport)
        {
            continue;
        }

        SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag,is_dynamic);
        mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
        {
            mac_limit_en |= (p_l2_node->adptr->flag.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                            | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;

            if (is_dynamic)
            {
                mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid, mac_limit_en));
        _sys_goldengate_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by mac+vlan
 */
STATIC int32
_sys_goldengate_l2_flush_by_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, uint32 flush_flag)
{
    sys_l2_node_t * p_l2_node           = NULL;
    uint8           mac_limit_en        = 0;
    uint8           is_dynamic    = 0;

    p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, mac, fid);
    if (NULL == p_l2_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (p_l2_node->flag.system_mac)
    {
        return CTC_E_NONE;
    }

    switch (flush_flag)
    {
    case CTC_L2_FDB_ENTRY_STATIC:
        if (!p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case  CTC_L2_FDB_ENTRY_DYNAMIC:
        if (p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC:
        if (p_l2_node->flag.remote_dynamic || p_l2_node->adptr->flag.is_static)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }
        break;

    case CTC_L2_FDB_ENTRY_ALL:
    default:
        break;
    }

    SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag,is_dynamic);
    mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
    if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
    {
        mac_limit_en |= (p_l2_node->adptr->flag.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                        | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;

        if (is_dynamic)
        {
            mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid, mac_limit_en));
    _sys_goldengate_l2uc_remove_sw_for_flush(lchip, p_l2_node);

    return CTC_E_NONE;
}

/**
   @brief flush fdb entry by vlan
 */
STATIC int32
_sys_goldengate_l2_flush_by_fid(uint8 lchip, uint16 fid, uint32 flush_flag)
{
    ctc_listnode_t * next_node      = NULL;
    ctc_listnode_t * node           = NULL;
    ctc_linklist_t * fdb_list       = NULL;
    sys_l2_node_t  * p_l2_node      = NULL;
    int32          ret              = 0;
    uint32         flush_fdb_cnt_per_loop;
    uint8          mac_limit_en     = 0;
    uint8          is_dynamic = 0;
    uint16         sleep_cnt = 0;

    SYS_L2_FDB_FLUSH_COUNT_SET(flush_fdb_cnt_per_loop);

    fdb_list = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_LIST);

    CTC_LIST_LOOP_DEL(fdb_list, p_l2_node, node, next_node)
    {
        if (p_l2_node->flag.system_mac)
        {
            continue;
        }
        CONTINUE_IF_FLAG_NOT_MATCH(flush_flag, p_l2_node->adptr->flag, p_l2_node->flag);

        SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag,is_dynamic);
        mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
        {
            mac_limit_en |= (p_l2_node->adptr->flag.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                            | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
            if (is_dynamic)
            {
                mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
        }

        CTC_ERROR_RETURN(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid, mac_limit_en));
        _sys_goldengate_l2uc_remove_sw_for_flush(lchip, p_l2_node);

        SYS_L2_FDB_FLUSH_COUNT_PROCESS(flush_fdb_cnt_per_loop);

        SYS_FDB_FLUSH_SLEEP(sleep_cnt);
    }

    return ret;
}

/**
   @brief flush fdb entry by mac
 */
STATIC int32
_sys_goldengate_l2_flush_by_mac_cb(sys_l2_node_t* p_l2_node, sys_l2_mac_cb_t* mac_hash_info)
{
    uint8                  lchip = 0;
    uint8 mac_limit_en      = 0;
    uint8 is_dynamic  = 0;

    if (NULL == p_l2_node || NULL == mac_hash_info)
    {
        return 0;
    }

    lchip = mac_hash_info->lchip;

    if (sal_memcmp(mac_hash_info->l2_node.key.mac, p_l2_node->key.mac, CTC_ETH_ADDR_LEN))
    {
        return 0;
    }

    if (p_l2_node->flag.system_mac)
    {
        return 0;
    }
    RETURN_IF_FLAG_NOT_MATCH(mac_hash_info->query_flag, p_l2_node->adptr->flag, p_l2_node->flag)

    SYS_FDB_IS_DYNAMIC(p_l2_node->adptr->flag, p_l2_node->flag,is_dynamic);
    mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;

    if ((is_dynamic ||  pl2_gg_master[lchip]->static_fdb_limit) && (!p_l2_node->flag.limit_exempt))
    {
        mac_limit_en |= (p_l2_node->adptr->flag.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                        | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
        if (is_dynamic)
        {
            mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, p_l2_node->key.mac, p_l2_node->key.fid,mac_limit_en));
    _sys_goldengate_l2uc_remove_sw_for_flush(lchip, p_l2_node);

    return 0;
}

/**
   @brief flush fdb entry by mac
 */
STATIC int32
_sys_goldengate_l2_flush_by_mac(uint8 lchip, mac_addr_t mac, uint8 flush_flag)
{
    sys_l2_mac_cb_t   mac_hash_info;
    hash_traversal_fn fun = NULL;

    sal_memset(&mac_hash_info, 0, sizeof(sys_l2_mac_cb_t));
    FDB_SET_MAC(mac_hash_info.l2_node.key.mac, mac);
    mac_hash_info.query_flag = flush_flag;
    mac_hash_info.lchip      = lchip;

    fun = (hash_traversal_fn) _sys_goldengate_l2_flush_by_mac_cb;

    ctc_hash_traverse2_remove(pl2_gg_master[lchip]->mac_hash, fun, &(mac_hash_info.l2_node));

    return CTC_E_NONE;
}


int32
sys_goldengate_l2_flush_by_sw(uint8 lchip, ctc_l2_flush_fdb_t* pf)
{
    int32   ret = CTC_E_NONE;
    SYS_FDB_DBG_FUNC();

    L2_LOCK;
    switch (pf->flush_type)
    {
    case CTC_L2_FDB_ENTRY_OP_BY_VID:
        ret = _sys_goldengate_l2_flush_by_fid(lchip, pf->fid, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT:
        ret = _sys_goldengate_l2_flush_by_port(lchip, pf->gport, pf->flush_flag, pf->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN:
        ret = _sys_goldengate_l2_flush_by_port_fid(lchip, pf->gport, pf->fid, pf->flush_flag, pf->use_logic_port);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC_VLAN:
        ret = _sys_goldengate_l2_flush_by_mac_fid(lchip, pf->mac, pf->fid, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_BY_MAC:
        ret = _sys_goldengate_l2_flush_by_mac(lchip, pf->mac, pf->flush_flag);
        break;

    case CTC_L2_FDB_ENTRY_OP_ALL:
        ret = _sys_goldengate_l2_flush_by_all(lchip, pf->flush_flag);
        break;

    default:
        ret = CTC_E_INVALID_PARAM;
        break;
    }

    L2_UNLOCK;

    return ret;
}

STATIC int32
_sys_goldengate_l2_unbinding_logic_ecmp(uint8 lchip, ctc_l2_flush_fdb_t* pf, uint8 unbinding)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    sys_l2_vport_2_nhid_t* p_vport_node = NULL;
    uint32                 cmd     = 0;
    sys_nh_info_dsnh_t p_nhinfo;
    DsMac_m              ds_mac;

    sal_memset(&p_nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
    sal_memset(&ds_mac, 0, sizeof(ds_mac));

    if (pl2_gg_master[lchip]->cfg_has_sw || (0 == pf->use_logic_port) || (CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN != pf->flush_type))
    {
        return CTC_E_NONE;
    }

    p_vport_node = ctc_vector_get(pl2_gg_master[lchip]->vport2nh_vec, pf->gport);
    p_fid_node = ctc_vector_get(pl2_gg_master[lchip]->fid_vec, pf->fid);
    if ((NULL == p_vport_node)||(NULL == p_fid_node))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_vport_node->nhid, &p_nhinfo));
    if (!p_nhinfo.ecmp_valid)
    {
        return CTC_E_NONE;
    }

    if (unbinding)/* set to vlan default entry when flushing*/
    {
        cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, DEFAULT_ENTRY_INDEX(pf->fid), cmd, &ds_mac));
        SetDsMac(V, u2_g2_logicSrcPort_f, &ds_mac, pf->gport);
        SetDsMac(V, isStatic_f, &ds_mac, 0);
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_vport_node->ad_idx, cmd, &ds_mac));
    }
    else /* recover to nexthop after flush*/
    {
        _sys_goldengate_l2_bind_nhid_by_logic_port(lchip, pf->gport, p_vport_node->nhid);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_flush_fdb(uint8 lchip, ctc_l2_flush_fdb_t* pf)
{
    int32                      ret = CTC_E_NONE;
    uint8                      flush_pending = 0;
    ctc_learning_action_info_t learning_action = { 0 };

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pf);
    CTC_ERROR_RETURN(_sys_goldengate_l2_query_flush_check(lchip, pf, 1));

    if (pf->use_logic_port
    && ((pf->flush_type != CTC_L2_FDB_ENTRY_OP_BY_PORT)
    && (pf->flush_type != CTC_L2_FDB_ENTRY_OP_BY_PORT_VLAN)))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (pl2_gg_master[lchip]->cfg_has_sw && pl2_gg_master[lchip]->cfg_hw_learn) /* disable learn*/
    {
        learning_action.action = CTC_LEARNING_ACTION_MAC_TABLE_FULL;
        learning_action.value  = 1;
        CTC_ERROR_RETURN(sys_goldengate_learning_set_action(lchip, &learning_action));
    }

    if (pf->flush_flag == CTC_L2_FDB_ENTRY_PENDING)
    {
        flush_pending = 1;
        pf->flush_flag = CTC_L2_FDB_ENTRY_DYNAMIC;
    }

    ret = sys_goldengate_l2_flush_by_sw(lchip, pf);
    if ((ret < 0) && (!pl2_gg_master[lchip]->cfg_has_sw))
    {
        ret = CTC_E_NONE;
    }

    if (!pl2_gg_master[lchip]->cfg_has_sw
        || (pl2_gg_master[lchip]->cfg_has_sw && flush_pending))
    {
        if (CTC_L2_FDB_ENTRY_LOCAL_DYNAMIC == pf->flush_flag)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }
        _sys_goldengate_l2_unbinding_logic_ecmp(lchip, pf, 1);
         ret = sys_goldengate_l2_flush_by_hw(lchip, pf, 1);
         _sys_goldengate_l2_unbinding_logic_ecmp(lchip, pf, 0);
    }

    if (pl2_gg_master[lchip]->cfg_has_sw && pl2_gg_master[lchip]->cfg_hw_learn) /* enable learn */
    {
        learning_action.action = CTC_LEARNING_ACTION_MAC_TABLE_FULL;
        learning_action.value  = 0;
        CTC_ERROR_RETURN(sys_goldengate_learning_set_action(lchip, &learning_action));
    }

    return ret;
}

int32
sys_goldengate_l2_set_dsmac_for_linkagg_hw_learning(uint8 lchip, uint16 gport, bool b_add)
{
    uint16          dest_id = 0;
    DsMac_m        ds_mac;
    uint32          cmd         = 0;
    uint32          ds_mac_base = 0;
    sys_l2_info_t   sys;
    uint32 nhid = 0;

    sal_memset(&ds_mac, 0, sizeof(DsMac_m));
    sal_memset(&sys, 0, sizeof(sys));

    dest_id = CTC_MAP_GPORT_TO_LPORT(gport);

    ds_mac_base = (pl2_gg_master[lchip]->base_trunk);

    if (b_add)
    {
        /*get nexthop id from gport*/
        sys_goldengate_l2_get_ucast_nh(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &nhid);

        _sys_goldengate_l2_map_nh_to_sys(lchip, nhid, &sys, 0, gport, 0);

        sys.flag.flag_ad.is_static = 0;    /* src_mismatch_learn_en*/

        /* write Ad */
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(_sys_goldengate_l2_encode_dsmac(lchip, &ds_mac, &sys));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_mac_base + dest_id, cmd, &ds_mac));

    }

    /* no need consider delete. actually the dsmac should be set at the initial process.
     * the reason not doing that is because linkagg nexthop is not created yet, back then.
     * Unless nexthop reserved linkagg nexthop, fdb cannot do it at the initial process.
     */

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_set_dsmac_for_bpe(uint8 lchip, uint16 gport, bool b_add)
{
    uint16          drv_lport = 0;
    DsMac_m        ds_mac;
    uint32          cmd         = 0;
    uint32          ds_mac_base = 0;
    sys_l2_info_t   sys;
    uint32 nhid = 0;

    sal_memset(&sys, 0, sizeof(sys));

    drv_lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);

    ds_mac_base = (pl2_gg_master[lchip]->base_gport);

    if (b_add)
    {
        /*get nexthop id from gport*/
        sys_goldengate_l2_get_ucast_nh(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &nhid);

        _sys_goldengate_l2_map_nh_to_sys(lchip, nhid, &sys, 0, gport, 0);

        /* write Ad */
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(_sys_goldengate_l2_encode_dsmac(lchip, &ds_mac, &sys));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ds_mac_base + drv_lport, cmd, &ds_mac));

    }


    return CTC_E_NONE;
}


int32
sys_goldengate_l2_flush_fid(uint8 lchip, uint16 fid, uint8 enable)
{
    uint32  cmd_fib    = 0;
    uint32  cmd_ipe    = 0;
    FibEngineLookupResultCtl_m fib;
    IpeLookupCtl_m               ipe;

    SYS_L2UC_FID_CHECK(fid);

    sal_memset(&fib, 0, sizeof(fib));
    sal_memset(&ipe, 0, sizeof(ipe));

    cmd_fib = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_fib, &fib));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe, &ipe));

    SetFibEngineLookupResultCtl(V, fdbFloodingVsiEn_f, &fib, !!enable);
    SetFibEngineLookupResultCtl(V, fdbFloodingVsi_f  , &fib, fid);

    SetIpeLookupCtl(V, fdbFlushVsiLearningDisable_f, &ipe, !enable);
    SetIpeLookupCtl(V, fdbFlushVsi_f, &ipe, fid);

    cmd_fib = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_fib, &fib));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe, &ipe));

    return CTC_E_NONE;
}


#define _FDB_HW_
STATIC int32
_sys_goldengate_l2_build_tcam_key(uint8 lchip, mac_addr_t mac, uint32 ad_index, void* mackey)
{
    hw_mac_addr_t hw_mac;

    CTC_PTR_VALID_CHECK(mackey);
    sal_memset(mackey, 0, sizeof(DsFibMacBlackHoleHashKey_m));

    FDB_SET_HW_MAC(hw_mac, mac);
    SetDsFibMacBlackHoleHashKey(A, mappedMac_f, mackey, hw_mac);
    SetDsFibMacBlackHoleHashKey(V, valid_f, mackey, 1);
    SetDsFibMacBlackHoleHashKey(V, dsAdIndex_f, mackey, ad_index);

    return CTC_E_NONE;

}
STATIC int32
_sys_goldengate_l2_write_hw(uint8 lchip, sys_l2_info_t* psys)
{
    uint32   cmd         = 0;

    DsMac_m ds_mac;

    SYS_FDB_DBG_FUNC();

    /* write Ad */
    CTC_ERROR_RETURN(_sys_goldengate_l2_encode_dsmac(lchip, &ds_mac, psys));
    if (!psys->flag.flag_node.rsv_ad_index)
    {
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->ad_index, cmd, &ds_mac));
    }

    if (psys->flag.flag_node.is_tcam) /* tcam */
    {
        DsFibMacBlackHoleHashKey_m tcam_key;
        _sys_goldengate_l2_build_tcam_key(lchip, psys->mac,psys->ad_index, &tcam_key);
        cmd = DRV_IOW(DsFibMacBlackHoleHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->key_index, cmd, &tcam_key));
    }
    else if (!psys->flag.flag_node.type_default) /* uc or mc */
    {
        drv_fib_acc_in_t  in;
        drv_fib_acc_out_t out;
        uint8 is_dynamic;

        sal_memset(&in, 0, sizeof(in));
        sal_memset(&out, 0, sizeof(out));
        /* write Key */
        FDB_SET_HW_MAC(in.mac.mac, psys->mac);
        in.mac.fid      = psys->fid;
        in.mac.ad_index = psys->ad_index;

        /*for white list entry do not aging */
        if ((psys->flag.flag_node.white_list) || (psys->flag.flag_node.aging_disable))
        {
            in.mac.aging_timer = 0;
        }
        else if (!psys->flag.flag_ad.is_static) /* pending entry already set aging status.*/
        {
            in.mac.aging_timer = SYS_AGING_TIMER_INDEX_MAC;
            in.mac.hw_aging_en = 1 ;
        }

        SYS_FDB_IS_DYNAMIC(psys->flag.flag_ad, psys->flag.flag_node, is_dynamic);
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!psys->flag.flag_node.limit_exempt))
        {
            if (!psys->flag.flag_node.type_l2mc)
            {
                if (!psys->flag.flag_ad.logic_port)
                {
                    in.mac.flag |= DRV_FIB_ACC_PORT_MAC_LIMIT_EN;
                }
                in.mac.flag |= DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
            }

            if (is_dynamic)
            {
                in.mac.flag |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
            else
            {
                in.mac.flag |= DRV_FIB_ACC_STATIC_COUNT_EN;
            }
        }
        in.mac.learning_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(psys->gport);
        if (!psys->flag.flag_node.type_l2mc)
        {
            in.mac.flag |= DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        }

        if (psys->pending)
        {
            if(pl2_gg_master[lchip]->vp_alloc_ad_en && psys->flag.flag_ad.logic_port)
            {
                uint32 is_static = 0;
                cmd = DRV_IOR(DsMac_t, DsMac_isStatic_f);
                DRV_IOCTL(lchip, psys->old_ad_index, cmd, &is_static);

                if(is_static)
                {
                    in.mac.flag &= ~(DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN|DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN|DRV_FIB_ACC_VLAN_MAC_LIMIT_EN|DRV_FIB_ACC_PORT_MAC_LIMIT_EN);
                }
            }
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
        }
        else
        {
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
            if (out.mac.conflict)
            {
                SYS_FDB_DBG_DUMP("MAC:%s, FID:%d\n", sys_goldengate_output_mac(psys->mac), psys->fid);
                return CTC_E_FDB_HASH_CONFLICT;
            }

            if (out.mac.pending)
            {
                return CTC_E_FDB_SECURITY_VIOLATION;
            }
            psys->key_index = out.mac.key_index;
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_write_dskey(uint8 lchip, sys_l2_info_t* psys)
{
    uint32 cmd = 0;

    SYS_FDB_DBG_FUNC();

    if (psys->flag.flag_node.is_tcam) /* tcam */
    {
        DsFibMacBlackHoleHashKey_m tcam_key;
        _sys_goldengate_l2_build_tcam_key(lchip, psys->mac,psys->ad_index, &tcam_key);
        cmd = DRV_IOW(DsFibMacBlackHoleHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, psys->key_index, cmd, &tcam_key));
    }
    else if (!psys->flag.flag_node.type_default) /* uc or mc */
    {
        drv_fib_acc_in_t  in;
        drv_fib_acc_out_t out;
        uint8 is_dynamic;

        sal_memset(&in, 0, sizeof(in));
        sal_memset(&out, 0, sizeof(out));
        /* write Key */
        FDB_SET_HW_MAC(in.mac.mac, psys->mac);
        in.mac.fid      = psys->fid;
        in.mac.ad_index = psys->ad_index;

        /*for white list entry do not aging */
        if ((psys->flag.flag_node.white_list) || (psys->flag.flag_node.aging_disable))
        {
            in.mac.aging_timer = 0;
        }
        else if (!psys->flag.flag_ad.is_static) /* pending entry already set aging status.*/
        {
            in.mac.aging_timer = SYS_AGING_TIMER_INDEX_MAC;
            in.mac.hw_aging_en = 1 ;
        }

        SYS_FDB_IS_DYNAMIC(psys->flag.flag_ad, psys->flag.flag_node, is_dynamic);
        if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && (!psys->flag.flag_node.limit_exempt))
        {
            if (!psys->flag.flag_node.type_l2mc)
            {
                if (!psys->flag.flag_ad.logic_port)
                {
                    in.mac.flag |= DRV_FIB_ACC_PORT_MAC_LIMIT_EN;
                }
                in.mac.flag |= DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
            }

            if (is_dynamic)
            {
                in.mac.flag |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
            }
            else
            {
                in.mac.flag |= DRV_FIB_ACC_STATIC_COUNT_EN;
            }
        }
        in.mac.learning_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(psys->gport);
        if (!psys->flag.flag_node.type_l2mc)
        {
            in.mac.flag |= DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        }

        if (psys->pending)
        {
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
        }
        else
        {
            CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_WRITE_MAC_BY_KEY, &in, &out));
            if (out.mac.conflict)
            {
                SYS_FDB_DBG_DUMP("MAC:%s, FID:%d\n", sys_goldengate_output_mac(psys->mac), psys->fid);
                return CTC_E_FDB_HASH_CONFLICT;
            }

            if (out.mac.pending)
            {
                return CTC_E_FDB_SECURITY_VIOLATION;
            }
            psys->key_index = out.mac.key_index;
        }

    }

    return CTC_E_NONE;
}


/**
   @brief add new fdb entry
 */
STATIC int32
_sys_goldengate_l2uc_add_sw(uint8 lchip, sys_l2_node_t* p_l2_node)
{
    int32 ret = CTC_E_NONE;

    SYS_FDB_DBG_FUNC();

    CTC_ERROR_GOTO(_sys_goldengate_l2_port_list_add(lchip, p_l2_node), ret, error_4);
    CTC_ERROR_GOTO(_sys_goldengate_l2_fid_list_add(lchip, p_l2_node), ret, error_3);
    CTC_ERROR_GOTO(_sys_goldengate_l2_fdb_hash_add(lchip, p_l2_node), ret, error_2);
    CTC_ERROR_GOTO(_sys_goldengate_l2_mac_hash_add(lchip, p_l2_node), ret, error_1);
    return ret;

 error_1:
    _sys_goldengate_l2_fdb_hash_remove(lchip, p_l2_node);
 error_2:
    _sys_goldengate_l2_fid_list_remove(lchip, p_l2_node);
 error_3:
    _sys_goldengate_l2_port_list_remove(lchip, p_l2_node);
 error_4:
    return ret;
}

STATIC int32
_sys_goldengate_l2uc_update_sw(uint8 lchip, sys_l2_ad_spool_t* src_ad, sys_l2_ad_spool_t* dst_ad, sys_l2_node_t* pn_new)
{

    SYS_FDB_DBG_FUNC();

    pn_new->adptr = src_ad;
    /* must do it first. otherwise, the src_ad port_listnode will be missing.*/
    _sys_goldengate_l2_port_list_remove(lchip, pn_new);
    _sys_goldengate_l2_fid_list_remove(lchip, pn_new);

    pn_new->adptr = dst_ad; /*recover it back.*/
    CTC_ERROR_RETURN(_sys_goldengate_l2_port_list_add(lchip, pn_new));
    CTC_ERROR_RETURN(_sys_goldengate_l2_fid_list_add(lchip, pn_new));

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_dump_l2master(uint8 lchip)
{
    SYS_L2_INIT_CHECK(lchip);
    SYS_FDB_DBG_DUMP("sw_hash_tbl_size           0x%x \n" \
                     "hash_base                  0x%x \n" \
                     "pure_hash_num              0x%x \n" \
                     "total_count                0x%x \n" \
                     "dynmac_count               0x%x \n" \
                     "local_dynmac_count         0x%x \n" \
                     "ad_index_drop              0x%x \n" \
                     "dft_entry_base             0x%x \n" \
                     "max_fid_value              0x%x \n" \
                     "flush_fdb_cnt_per_loop     0x%x \n" \
                     "hw_learn_en                0x%x \n" \
                     "has_sw_table               0x%x \n",
                     pl2_gg_master[lchip]->extra_sw_size,       \
                     SYS_L2_FDB_CAM_NUM,              \
                     pl2_gg_master[lchip]->pure_hash_num,       \
                     pl2_gg_master[lchip]->total_count,         \
                     pl2_gg_master[lchip]->dynamic_count,       \
                     pl2_gg_master[lchip]->local_dynamic_count, \
                     pl2_gg_master[lchip]->ad_index_drop,       \
                     pl2_gg_master[lchip]->dft_entry_base,      \
                     pl2_gg_master[lchip]->cfg_max_fid,         \
                     pl2_gg_master[lchip]->cfg_flush_cnt,       \
                     pl2_gg_master[lchip]->cfg_hw_learn,        \
                     pl2_gg_master[lchip]->cfg_has_sw);

    SYS_FDB_DBG_DUMP("hw_aging_en                0x%x \n" , pl2_gg_master[lchip]->cfg_hw_learn);
    if (!pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
        SYS_FDB_DBG_DUMP("base_vport                 0x%x \n" , pl2_gg_master[lchip]->base_vport);
    }
    SYS_FDB_DBG_DUMP("base_gport                 0x%x \n" \
                     "base_trunk                 0x%x \n" \
                     "logic_port_num             0x%x \n",
                     pl2_gg_master[lchip]->base_gport,   \
                     pl2_gg_master[lchip]->base_trunk,   \
                     pl2_gg_master[lchip]->cfg_vport_num);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_get_reserved_index(uint8 lchip, sys_l2_info_t* psys)
{
    int32  ret              = CTC_E_NONE;
    uint8  is_local_chip    = 0;
    uint8  is_dynamic = 0;
    uint8  is_trunk         = 0;
    uint8  is_phy_port      = 0;
    uint8  is_logic_port    = 0;
    uint8  is_binding       = 0;
    uint32 ad_index         = 0;
    uint8  other_flag    = 0;
    uint8 gchip_id          = 0;

    psys->flag.flag_node.rsv_ad_index = 1;
    other_flag += psys->flag.flag_ad.src_discard;
    other_flag += psys->flag.flag_ad.src_discard_to_cpu;
    other_flag += psys->flag.flag_ad.logic_port;
    other_flag += psys->flag.flag_ad.copy_to_cpu;
    other_flag += psys->flag.flag_ad.protocol_entry;
    other_flag += psys->flag.flag_ad.bind_port;

    /* for discard fdb */
    if (psys->flag.flag_ad.drop && (0 == other_flag))
    {
        ad_index = pl2_gg_master[lchip]->ad_index_drop;
        psys->ad_index = ad_index;
        SYS_FDB_DBG_INFO("build drop ucast ad_index:0x%x\n", ad_index);
        return CTC_E_NONE;
    }

    gchip_id = CTC_MAP_GPORT_TO_GCHIP(psys->gport);
    is_local_chip = sys_goldengate_chip_is_local(lchip, gchip_id);

	/* is pure dynamic. */
    is_dynamic = (!psys->flag.flag_ad.is_static && !psys->flag.flag_ad.bind_port);

    is_logic_port = psys->flag.flag_ad.logic_port;
    is_trunk      = !is_logic_port && CTC_IS_LINKAGG_PORT(psys->gport);
    is_phy_port   = !is_logic_port && !CTC_IS_LINKAGG_PORT(psys->gport);

    if (is_logic_port)
    {
        uint32 nhid;
        ret        = sys_goldengate_l2_get_nhid_by_logic_port(lchip, psys->gport, &nhid);
        is_binding = !FAIL(ret);
    }

    if (is_dynamic && is_trunk && !psys->with_nh)
    {
        ad_index = pl2_gg_master[lchip]->base_trunk +
                   CTC_GPORT_LINKAGG_ID(psys->gport); /* linkagg */
    }
    else if (is_dynamic && is_local_chip && is_phy_port && !psys->with_nh)
    {
        ad_index = pl2_gg_master[lchip]->base_gport +
                   SYS_MAP_CTC_GPORT_TO_DRV_LPORT(psys->gport); /* gport */
    }
    else if (is_dynamic  && is_logic_port && is_binding)
    {
        if (pl2_gg_master[lchip]->vp_alloc_ad_en)
        {
            sys_l2_vport_2_nhid_t *p_vp_node = NULL;
            p_vp_node = ctc_vector_get(pl2_gg_master[lchip]->vport2nh_vec, psys->gport);
            ad_index = p_vp_node->ad_idx;
        }
        else
        {
            ad_index = pl2_gg_master[lchip]->base_vport + psys->gport;  /* logic port */
        }
    }
    else if (psys->flag.flag_node.remote_dynamic && !is_local_chip && pl2_gg_master[lchip]->rchip_ad_rsv[gchip_id])
    {
        uint16  lport = CTC_MAP_GPORT_TO_LPORT(psys->gport);
        uint8   slice_en = (pl2_gg_master[lchip]->rchip_ad_rsv[gchip_id] >> 14) & 0x3;
        uint16  rsv_num = pl2_gg_master[lchip]->rchip_ad_rsv[gchip_id] & 0x3FFF;
        uint32  ad_base = pl2_gg_master[lchip]->base_rchip[gchip_id];

        if (slice_en == 1)
        {
            /*slice 0*/
            if (lport >= rsv_num)
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport + ad_base;
        }
        else if (slice_en == 3)
        {
            if (((lport >= 256) && (lport >= (256 + rsv_num/2)))
                || ((lport < 256) && (lport >= rsv_num/2)))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = ((lport >= 256) ? (lport - 256 + rsv_num/2) : lport) + ad_base;
        }
        else if (slice_en == 2)
        {
            /*slice 1*/
            if (((lport&0xFF) >= rsv_num) || (lport < 256))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport - 256 + ad_base;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        psys->flag.flag_node.rsv_ad_index = 0;
    }

    psys->ad_index = ad_index;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_map_ctc_to_sys(uint8 lchip, uint8 type, void * l2, sys_l2_info_t * psys,
                                  uint8 with_nh, uint32 nhid)
{
    uint8 gport_assign = 0;
    uint16 gport = 0;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_misc_t* p_nh_misc_info = NULL;
    uint8 is_cpu_nh = 0;

    SYS_FDB_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_l2_map_flag(lchip, type, l2, &psys->flag));

    if ((L2_TYPE_UC == type) ||(L2_TYPE_TCAM == type))
    {
        ctc_l2_addr_t * l2_addr = (ctc_l2_addr_t *) l2;
        uint32 temp_nhid = 0;
        sys_nh_info_dsnh_t nh_info;

        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));
        gport_assign = (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT))?1:0;

        if (!with_nh)
        {
            /* if not set discard, get dsfwdptr of port */
            if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
            {
                /*get nexthop id from gport*/
                CTC_ERROR_RETURN(sys_goldengate_l2_get_ucast_nh(lchip, l2_addr->gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &temp_nhid));
                _sys_goldengate_l2_map_nh_to_sys(lchip, temp_nhid, psys, gport_assign, l2_addr->gport, l2_addr->assign_port);
            }
            else
            {
                psys->fwd_ptr = SYS_FDB_DISCARD_FWD_PTR;
                psys->gport  = DISCARD_PORT;
                psys->merge_dsfwd  = 0;
            }
        }
        else  /* with nexthop */
        {
            psys->with_nh = 1;
            psys->nhid    = nhid;

            if (nhid == SYS_NH_RESOLVED_NHID_FOR_TOCPU)
            {
                is_cpu_nh = 1;
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo));
                if (p_nhinfo->hdr.nh_entry_type == SYS_NH_TYPE_MISC)
                {
                    p_nh_misc_info = (sys_nh_info_misc_t*)p_nhinfo;
                    if ((p_nh_misc_info->misc_nh_type == CTC_MISC_NH_TYPE_TO_CPU)&&CTC_IS_CPU_PORT(p_nh_misc_info->gport))
                    {
                        is_cpu_nh = 1;
                    }
                }
            }

            CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nhid, &nh_info));
            gport = (psys->flag.flag_ad.logic_port || nh_info.ecmp_valid || nh_info.aps_en || nh_info.is_mcast\
                                                            || nh_info.is_ecmp_intf)?l2_addr->gport:nh_info.gport;
            if (is_cpu_nh && !psys->flag.flag_ad.logic_port)
            {
                gport = SYS_RSV_PORT_OAM_CPU_ID;
            }
            else if(nh_info.drop_pkt && !psys->flag.flag_ad.logic_port)
            {
                gport = DISCARD_PORT;
            }
            _sys_goldengate_l2_map_nh_to_sys(lchip, nhid, psys, gport_assign, gport, l2_addr->assign_port);

            /*just for white list entry mismatch src port*/
            if (psys->flag.flag_node.white_list)
            {
                psys->gport = SYS_RSV_PORT_DROP_ID;
            }
            /* fid = 0xffff use for sys mac in tcam, so use fdb_hash to store */
        }

        /* map hw entry */
        psys->fid = l2_addr->fid;
        FDB_SET_MAC(psys->mac, l2_addr->mac);

        if(L2_TYPE_TCAM != type)
        {
            CTC_ERROR_RETURN(_sys_goldengate_l2_get_reserved_index(lchip, psys));
        }

        SYS_FDB_DBG_INFO("psys  gport 0x%x\n", psys->gport);
    }
    else if (L2_TYPE_MC == type)  /* mc*/
    {
        ctc_l2_mcast_addr_t * l2mc_addr = (ctc_l2_mcast_addr_t *) l2;
        /* map hw entry */
        psys->fid = l2mc_addr->fid;
        FDB_SET_MAC(psys->mac, l2mc_addr->mac);
        psys->flag.flag_ad.is_static = 1;
        psys->flag.flag_node.type_l2mc = 1;
        psys->mc_gid         = l2mc_addr->l2mc_grp_id;
        psys->share_grp_en = l2mc_addr->share_grp_en;
    }
    else if (L2_TYPE_DF == type)  /*df*/
    {
        ctc_l2dflt_addr_t * l2df_addr = (ctc_l2dflt_addr_t *) l2;
        /* map hw entry */
        psys->fid               = l2df_addr->fid;
        psys->flag.flag_ad.is_static    = 1;
        psys->flag.flag_node.type_default = 1;
        psys->mc_gid            = l2df_addr->l2mc_grp_id;
        psys->fid_flag          = l2df_addr->flag;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_add_tcam(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid)
{
    int32          ret    = 0;
    uint32         key_index = 0;
    uint32         ad_index = 0;
    sys_l2_tcam_t* p_tcam = NULL;
    sys_l2_info_t  sys;
    sys_goldengate_opf_t opf;

    SYS_FDB_DBG_FUNC();

    if (!pl2_gg_master[lchip]->tcam_num)
    {
        return CTC_E_NO_RESOURCE;
    }

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    CTC_ERROR_RETURN(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_TCAM, l2_addr, &sys, with_nh, nhid));

    /* 1.  lookup tcam_hash */
    p_tcam = _sys_goldengate_l2_tcam_hash_lookup(lchip, sys.mac);
    if (!p_tcam)  /* add*/
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_tcam, sizeof(sys_l2_tcam_t));
        if (!p_tcam)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }

        sal_memset(&opf, 0, sizeof(opf));
        opf.pool_type  = OPF_BLACK_HOLE_CAM;
        CTC_ERROR_GOTO(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &key_index), ret, error_1);
        CTC_ERROR_GOTO(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, &ad_index), ret, error_2);
        pl2_gg_master[lchip]->alloc_ad_cnt++;

        sys.key_index  = key_index;
        sys.ad_index   = ad_index;

        /* write sw */
        FDB_SET_MAC(p_tcam->key.mac , sys.mac);
        p_tcam->flag     = sys.flag;
        p_tcam->key_index= sys.key_index;
        p_tcam->ad_index = sys.ad_index;
        CTC_ERROR_GOTO(_sys_goldengate_l2_tcam_hash_add(lchip, p_tcam), ret, error_3);

        /* write hw */
        CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &sys), ret, error_4);

    }
    else  /*update*/
    {
        /* update sw. system not support forward. thus should not update port list */
        p_tcam->flag     = sys.flag;

        sys.key_index = p_tcam->key_index;
        sys.ad_index = p_tcam->ad_index;

        /* write hw */
        CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &sys), ret, error_0);
    }

    return CTC_E_NONE;

error_4:
    _sys_goldengate_l2_tcam_hash_remove(lchip, p_tcam);
error_3:
    sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, ad_index);
    pl2_gg_master[lchip]->alloc_ad_cnt--;
error_2:
    sys_goldengate_opf_free_offset(lchip, &opf, 1, key_index);
error_1:
    mem_free(p_tcam);
error_0:
    return ret;
}

STATIC int32
_sys_goldengate_l2_remove_tcam(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    sys_l2_tcam_t* p_tcam = NULL;
    sys_l2_info_t  sys;
    uint32         cmd   = 0;
    sys_goldengate_opf_t opf;
    DsFibMacBlackHoleHashKey_m empty;
    int32 ret = 0;

    L2_LOCK;

    FDB_SET_MAC(sys.mac, l2_addr->mac);

    p_tcam = _sys_goldengate_l2_tcam_hash_lookup(lchip, sys.mac);
    if (p_tcam)
    {
        sal_memset(&empty, 0, sizeof(empty));
        cmd = DRV_IOW(DsFibMacBlackHoleHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_tcam->key_index, cmd, &empty), ret, error);

        sal_memset(&opf, 0, sizeof(opf));
        opf.pool_type  = OPF_BLACK_HOLE_CAM;
        sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, p_tcam->ad_index);
        sys_goldengate_opf_free_offset(lchip, &opf, 1, p_tcam->key_index);
        _sys_goldengate_l2_tcam_hash_remove(lchip, p_tcam);
        mem_free(p_tcam);
        pl2_gg_master[lchip]->alloc_ad_cnt--;
    }
    else
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
    }

error:

    L2_UNLOCK;

    return ret;
}

/**
   @brief add fdb entry
 */
int32
sys_goldengate_l2_add_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 with_nh, uint32 nhid)
{
    sys_l2_info_t       new_sys;
    sys_l2_node_t       * p_l2_node = NULL;
    int32               ret         = 0;
    sys_l2_ad_spool_t   * p_old_ad  = NULL;
    sys_l2_flag_node_t     p_old_flag_n;
    sys_l2_fid_node_t   * fid    = NULL;
    uint32              fid_flag = 0;
    mac_lookup_result_t result = {0};
    sys_l2_ad_spool_t   * pa_new = NULL;
    sys_l2_ad_spool_t   * pa_get = NULL;
    sys_l2_node_t       * pn_new = NULL;
    sys_l2_info_t       old_sys;
    uint32              temp_flag = 0;
    uint8               freed = 0;
    uint8               is_overwrite = 0;
    uint8               need_has_sw  = 0;
    uint8               vp_flag_check = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&old_sys, 0, sizeof(sys_l2_info_t));

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s ,fid :%d ,flag:0x%x, gport:0x%x\n",
                      sys_goldengate_output_mac(l2_addr->mac), l2_addr->fid, l2_addr->flag, l2_addr->gport);

    if (l2_addr->fid ==  DONTCARE_FID) /* is_tcam is set when system mac. and system mac can update */
    {
        return _sys_goldengate_l2_add_tcam(lchip, l2_addr, with_nh, nhid);
    }

     /* 1. check flag supported*/
    if (!with_nh && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_WHITE_LIST_ENTRY))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (!with_nh && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_UCAST_DISCARD))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

     /* 2. check parameter*/
    if (!with_nh && !CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
    {
        CTC_GLOBAL_PORT_CHECK(l2_addr->gport);
    }

    if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
    {
        temp_flag = l2_addr->flag;
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_AGING_DISABLE);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_BIND_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_LEARN_LIMIT_EXEMPT);
        if (temp_flag) /* dynamic entry has other flag */
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (with_nh)
    {
        if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT))
        {
            CTC_GLOBAL_PORT_CHECK(l2_addr->gport);
        }
        /* nexthop module doesn't provide check nhid. */
    }

    if (!pl2_gg_master[lchip]->cfg_has_sw && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_LEARN_LIMIT_EXEMPT))
    {
        return CTC_E_NOT_SUPPORT;
    }

    L2_LOCK;

     /* 3. check fid created*/
    fid      = _sys_goldengate_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);
    fid_flag = (fid) ? fid->flag : 0;

    if (!with_nh && (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_0;
    }

    if (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT))
    {
        SYS_LOGIC_PORT_CHECK_WITH_UNLOCK(l2_addr->gport);
    }

     /* 4. map ctc to sys*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_UC, l2_addr, &new_sys, with_nh, nhid), ret, error_0);

     /*need to save SW for all unreserved ad_index when enable hw learning mode*/
    need_has_sw = pl2_gg_master[lchip]->cfg_has_sw ? 1 : (!new_sys.flag.flag_node.rsv_ad_index);

     /* 5. lookup hw, check result*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_acc_lookup_mac_fid
                    (lchip, l2_addr->mac, l2_addr->fid, &result), ret, error_0);
    new_sys.key_index = result.key_index;
    if (result.conflict)
    {
        ret = CTC_E_FDB_HASH_CONFLICT;
        goto error_0;
    }

     /* 6. if hit means entry already exist, pending entry has clear hit flag in driver*/
    is_overwrite = result.hit;
    if (need_has_sw)
    {
        p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);
        is_overwrite = (p_l2_node != NULL);
    }

    if (result.pending)
    {
        new_sys.pending = 1;
        new_sys.key_index = result.key_index;
        new_sys.old_ad_index = result.ad_index;
    }

    /*check the gport learned whether equals to write, if not, return error*/
    /*we dont need check mac whether need override. del by liuhq@2020.4.10, bug[3244]*/
    #if 0
    {
        DsMac_m ds_mac;
        _sys_goldengate_get_dsmac_by_index(lchip, result.ad_index, &ds_mac);
        _sys_goldengate_l2_decode_dsmac(lchip, &old_sys, &ds_mac, result.ad_index);

        vp_flag_check = (!new_sys.flag.flag_ad.logic_port || (new_sys.flag.flag_ad.logic_port && !pl2_gg_master[lchip]->vp_alloc_ad_en));
        if ((old_sys.gport != l2_addr->gport) && new_sys.pending && vp_flag_check)
        {
            ret = CTC_E_INVALID_CONFIG;
            goto error_0;
        }
    }
    #endif

    /* l2 mc shall not ovewrite */
    if (old_sys.flag.flag_node.type_l2mc)
    {
        ret = CTC_E_FDB_MCAST_ENTRY_EXIST;
        goto error_0;
    }

    /* dynamic cannot rewrite static */
    if (!new_sys.flag.flag_ad.is_static && old_sys.flag.flag_ad.is_static && vp_flag_check)
    {
        ret = CTC_E_ENTRY_EXIST;
        goto error_0;
    }

     /* 7. add/update sw*/
    if (need_has_sw)
    {
        /*update or add, both need malloc new ad. */
        MALLOC_ZERO(MEM_FDB_MODULE, pa_new, sizeof(sys_l2_ad_spool_t));
        if (!pa_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }

        if (!is_overwrite)  /*free hit. malloc */
        {
            MALLOC_ZERO(MEM_FDB_MODULE, pn_new, sizeof(sys_l2_node_t));
            if (!pn_new)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_1;
            }

            p_l2_node = pn_new;

            _sys_goldengate_l2_map_sys_to_sw(lchip, &new_sys, p_l2_node, pa_new);
            CTC_ERROR_GOTO(_sys_goldengate_l2_build_dsmac_index
                               (lchip, &new_sys, NULL, pa_new, &pa_get), ret, error_2);
            p_l2_node->adptr = pa_get;
            p_l2_node->flag = new_sys.flag.flag_node;

            if (pa_new != pa_get) /* means get an old. */
            {
                mem_free(pa_new);
                pa_new = NULL;
            }

            CTC_ERROR_GOTO(_sys_goldengate_l2uc_add_sw(lchip, p_l2_node), ret, error_3);

            p_l2_node->adptr->index= new_sys.ad_index;
        }
        else  /* is overwrite need do update */
        {
            /* lookup old node. must success.  */
            p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);

            if (!p_l2_node)
            {
                ret = CTC_E_INVALID_PTR;
                goto error_1;
            }
            /* flag that only in sw */
            old_sys.flag.flag_node.remote_dynamic = p_l2_node->flag.remote_dynamic;
            sal_memset(&p_old_flag_n, 0, sizeof(sys_l2_flag_node_t));
            p_old_ad = p_l2_node->adptr;
            p_old_flag_n = p_l2_node->flag;
            /*
             * update ucast fdb entry, only need to update
             * ad_index, port list and spool node,
             * because only flag, gport, and ad_index in l2_node may be changed.
             */

            /* map l2 node */
            _sys_goldengate_l2_map_sys_to_sw(lchip, &new_sys, NULL, pa_new);
            CTC_ERROR_GOTO(_sys_goldengate_l2_build_dsmac_index
                               (lchip, &new_sys, p_l2_node->adptr, pa_new, &pa_get), ret, error_1);

            if (pa_new != pa_get) /* means get an old. */
            {
                mem_free(pa_new);
                pa_new = NULL;
            }
            CTC_ERROR_GOTO(_sys_goldengate_l2uc_update_sw(lchip, p_old_ad, pa_get, p_l2_node), ret, error_3);
            p_l2_node->adptr        = pa_get;
            p_l2_node->flag  = new_sys.flag.flag_node;
            p_l2_node->adptr->index = new_sys.ad_index;

        }
    }
    SYS_L2_DUMP_SYS_INFO(&new_sys);

    /* write hw */
    CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &new_sys), ret, error_4);

    /* do at last. because this process is irreversible. */
    if (is_overwrite && need_has_sw)
    {
        /* free old index*/
        _sys_goldengate_l2_free_dsmac_index(lchip, NULL, p_old_ad, &p_old_flag_n, &freed);
        if (freed)
        {
            mem_free(p_old_ad);
        }
    }

     /* update sw index*/
    if (p_l2_node)
    {
        p_l2_node->key_index = new_sys.key_index;
    }

    if (need_has_sw)
    {
        if (!is_overwrite)  /* add*/
        {
            _sys_goldengate_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);
        }
        else  /* update*/
        {
            if (!old_sys.flag.flag_ad.is_static && new_sys.flag.flag_ad.is_static)  /* dynamic -> static */
            {
                pl2_gg_master[lchip]->dynamic_count -- ;
                if (!old_sys.flag.flag_node.remote_dynamic)
                {
                    pl2_gg_master[lchip]->local_dynamic_count -- ;
                }
            }
            else if((!old_sys.flag.flag_ad.is_static && !new_sys.flag.flag_ad.is_static)&&
                (!old_sys.flag.flag_node.remote_dynamic && new_sys.flag.flag_node.remote_dynamic)) /* local_dynamic -> remote dynamic*/
            {
                pl2_gg_master[lchip]->local_dynamic_count--;
            }
            else if((!old_sys.flag.flag_ad.is_static && !new_sys.flag.flag_ad.is_static)&&
                (old_sys.flag.flag_node.remote_dynamic && !new_sys.flag.flag_node.remote_dynamic))/* remote dynamic -> local dynamic*/
            {
                pl2_gg_master[lchip]->local_dynamic_count++;
            }
        }
    }
    L2_UNLOCK;
    return ret;

 error_4:
    if (p_l2_node)       /* has sw*/
    {
        if (is_overwrite)  /* update roll back*/
        {
            _sys_goldengate_l2uc_update_sw(lchip, pa_get, p_old_ad, p_l2_node);
        }
        else  /* remove*/
        {
            _sys_goldengate_l2uc_remove_sw(lchip, p_l2_node);
        }
    }
 error_3:
    if (p_l2_node)
    {
        _sys_goldengate_l2_free_dsmac_index(lchip, NULL, pa_get, &p_l2_node->flag, NULL);/* error process don't care freed*/
    }
 error_2:
    mem_free(pn_new);
 error_1:
    if (pa_new)
    {
        mem_free(pa_new);
    }
 error_0:
    L2_UNLOCK;
    return ret;
}



int32
sys_goldengate_l2_remove_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    DsMac_m            dsmac;

    uint8               freed = 0;
    sys_l2_info_t       sys;
    mac_lookup_result_t rslt;
    uint8               mac_limit_en = 0;
    int32               ret = CTC_E_NONE;
    uint8               is_dynamic = 0;
    sys_l2_node_t*      p_l2_node = NULL;
    uint8               has_sw = 0;
    uint8               update_limit = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac: %s,fid: %d  \n",
                      sys_goldengate_output_mac(l2_addr->mac),
                      l2_addr->fid);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    if (l2_addr->fid ==  DONTCARE_FID)
    {
        return _sys_goldengate_l2_remove_tcam(lchip, l2_addr);
    }

    L2_LOCK;
    CTC_ERROR_GOTO(_sys_goldengate_l2_acc_lookup_mac_fid
                    (lchip, l2_addr->mac, l2_addr->fid, &rslt), ret, error_0);

    if (rslt.hit)
    {
        CTC_ERROR_GOTO(_sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac), ret, error_0);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
        if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_REMOVE_DYNAMIC) && sys.flag.flag_ad.is_static)
        {
            ret = CTC_E_NONE;
            goto error_0;
        }
    }
    else
    {
         /*SYS_FDB_DBG_DUMP("\n", sys_goldengate_output_mac(l2_addr->mac), l2_addr->fid);*/
        ret =  CTC_E_ENTRY_NOT_EXIST;
        goto error_0;
    }

    p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, l2_addr->mac, l2_addr->fid);
    update_limit = p_l2_node ? (!p_l2_node->flag.limit_exempt) : 1;

    SYS_FDB_IS_DYNAMIC(sys.flag.flag_ad, sys.flag.flag_node,is_dynamic);
    mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;

    if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && update_limit)
    {
        mac_limit_en |= (sys.flag.flag_ad.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                        | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
        if(is_dynamic)
        {
            mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
        }
    }

    /* remove hw entry */
    CTC_ERROR_GOTO(sys_goldengate_l2_delete_hw_by_mac_fid
                    (lchip, l2_addr->mac, l2_addr->fid,mac_limit_en), ret, error_0);
    SYS_FDB_DBG_INFO("Remove ucast fdb: key_index:0x%x, ad_index:0x%x\n",
                     rslt.key_index, rslt.ad_index);

    if(p_l2_node)
    {
        has_sw  = 1;
        _sys_goldengate_l2uc_remove_sw(lchip, p_l2_node);
        _sys_goldengate_l2_free_dsmac_index(lchip, NULL, p_l2_node->adptr, &p_l2_node->flag, &freed);
        if (freed)
        {
            mem_free(p_l2_node->adptr);
        }
        mem_free(p_l2_node);
    }
    else if (pl2_gg_master[lchip]->cfg_has_sw)
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto error_0;
    }
    if ((pl2_gg_master[lchip]->total_count != 0) && has_sw)
    {
        _sys_goldengate_l2_update_count(lchip, &sys.flag.flag_ad,&sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &rslt.key_index);
    }


error_0:

    L2_UNLOCK;
    return ret;
}

/*only remove pending fdb */
int32
sys_goldengate_l2_remove_pending_fdb(uint8 lchip, mac_addr_t mac, uint16 fid, uint32 ad_idx)
{
    mac_lookup_result_t rslt;
    int32 ret = 0;
    sys_l2_info_t       sys;
    DsMac_m            dsmac;
    uint8               mac_limit_en = 0;
    uint8               freed = 0;

    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&dsmac, 0, sizeof(DsMac_m));

    L2_LOCK;

    CTC_ERROR_GOTO(_sys_goldengate_l2_acc_lookup_mac_fid
                    (lchip, mac, fid, &rslt), ret, error_0);

    if (!rslt.pending)
    {
        L2_UNLOCK;
        SYS_FDB_DBG_INFO("Not Pending,mac: %s,fid: %d  \n", sys_goldengate_output_mac(mac), fid);
        return CTC_E_NONE;
    }

    if (rslt.ad_index != ad_idx)
    {
         L2_UNLOCK;
            SYS_FDB_DBG_INFO("Pending Aging But ad not match, should not remove mac: %s,fid: %d  ad_old:0x%x, ad_new:0x%x\n",
            sys_goldengate_output_mac(mac), fid, ad_idx, rslt.ad_index);
        return CTC_E_NONE;
    }


    CTC_ERROR_GOTO(_sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac), ret, error_0);
    _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);

    mac_limit_en = (sys.flag.flag_ad.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN
                | DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN
                | DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;

    ret = sys_goldengate_l2_delete_hw_by_mac_fid(lchip, mac, fid, mac_limit_en);

    if (pl2_gg_master[lchip]->cfg_has_sw)
    {

        sys_l2_node_t* p_l2_node = NULL;

        p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, mac, fid);
        if(p_l2_node)
        {
            _sys_goldengate_l2uc_remove_sw(lchip, p_l2_node);
            _sys_goldengate_l2_free_dsmac_index(lchip, NULL, p_l2_node->adptr, &p_l2_node->flag, &freed);
            if (freed)
            {
                mem_free(p_l2_node->adptr);
            }
            mem_free(p_l2_node);
            _sys_goldengate_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &rslt.key_index);
        }
    }
error_0:

    L2_UNLOCK;
    return ret;
}

int32
sys_goldengate_l2_fdb_set_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 hit)
{
    mac_lookup_result_t rslt;


    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac: %s,fid: %d  \n",
                      sys_goldengate_output_mac(l2_addr->mac),
                      l2_addr->fid);
    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));

    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));
    if (!rslt.hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status(lchip, 1, rslt.key_index, hit));

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_fdb_get_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8* hit)
{
    mac_lookup_result_t rslt;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2UC_FID_CHECK(l2_addr->fid);
    CTC_PTR_VALID_CHECK(hit);
    SYS_FDB_DBG_FUNC();

    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));
    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2_addr->mac, l2_addr->fid, &rslt));
    if (!rslt.hit)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    CTC_ERROR_RETURN(sys_goldengate_aging_get_aging_status(lchip, 1, rslt.key_index, hit));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_fdb_calc_index(uint8 lchip, mac_addr_t mac, uint16 fid, sys_l2_calc_index_t* p_index)
{
    drv_cpu_acc_in_t  in;
    drv_cpu_acc_out_t out;
    uint8 hash_key[12] = {0};

    CTC_PTR_VALID_CHECK(p_index);
    sal_memset(&in, 0, sizeof(drv_cpu_acc_in_t));
    sal_memset(&out, 0, sizeof(drv_cpu_acc_out_t));

    hash_key[0] = 0x09;
    hash_key[2] = (fid & 0x3) << 6;
    hash_key[3] = (fid >> 2) & 0xFF;
    hash_key[4] = ((fid >> 10) & 0xF) | ((mac[5] & 0xF) << 4);
    hash_key[5] = (mac[5] >> 4) | ((mac[4] & 0xF) << 4);
    hash_key[6] = (mac[4] >> 4) | ((mac[3] & 0xF) << 4);
    hash_key[7] = (mac[3] >> 4) | ((mac[2] & 0xF) << 4);
    hash_key[8] = (mac[2] >> 4) | ((mac[1] & 0xF) << 4);
    hash_key[9] = (mac[1] >> 4) | ((mac[0] & 0xF) << 4);
    hash_key[10] = mac[0] >> 4;
    in.tbl_id = DsFibHost0MacHashKey_t;
    in.data = (void*)hash_key;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_CALC_ACC_FDB_INDEX, &in, &out));


    p_index->index_cnt = out.hit;
    sal_memcpy(p_index->key_index, out.data, p_index->index_cnt * sizeof(uint32));

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_replace_match_fdb(uint8 lchip, ctc_l2_replace_t* p_replace)
{
    uint32             match_key_index = 0;
    uint8              match_hit = 0;
    uint8              mac_limit_en = 0;
    mac_lookup_result_t match_result = {0};
    ctc_l2_addr_t*     l2_addr = &p_replace->l2_addr;
    ctc_l2_addr_t*     match_addr = &p_replace->match_addr;
    uint8              with_nh = (p_replace->nh_id ? 1 : 0);
    uint32             nh_id   = p_replace->nh_id;
    sys_l2_fid_node_t   * fid    = NULL;
    uint32              fid_flag = 0;
    uint8               need_has_sw  = 0;
    int32               ret = 0;
    sys_l2_info_t       new_sys;
    sys_l2_info_t       old_sys;
    sys_l2_ad_spool_t   * pa_new = NULL;
    sys_l2_ad_spool_t   * pa_get = NULL;
    sys_l2_node_t       * pn_new = NULL;
    sys_l2_node_t       * p_l2_node = NULL;
    sys_l2_node_t       * p_match_node = NULL;
    DsMac_m             old_dsmac;

    SYS_FDB_DBG_FUNC();

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&old_sys, 0, sizeof(sys_l2_info_t));

    if ((l2_addr->fid ==  DONTCARE_FID) || (match_addr->fid ==  DONTCARE_FID))
    {
        return CTC_E_NOT_SUPPORT;
    }

     /* check flag supported*/
    if (!with_nh && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_WHITE_LIST_ENTRY))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (!with_nh && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_UCAST_DISCARD))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

     /* check parameter*/
    if (!with_nh && !CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_DISCARD))
    {
        CTC_GLOBAL_PORT_CHECK(l2_addr->gport);
    }

    if (!CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_IS_STATIC))
    {
        uint32 temp_flag;
        temp_flag = l2_addr->flag;
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_AGING_DISABLE);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_REMOTE_DYNAMIC);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_BIND_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_WHITE_LIST_ENTRY);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT);
        CTC_UNSET_FLAG(temp_flag, CTC_L2_FLAG_LEARN_LIMIT_EXEMPT);
        if (temp_flag) /* dynamic entry has other flag */
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (with_nh)
    {
        if (CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT))
        {
            CTC_GLOBAL_PORT_CHECK(l2_addr->gport);
        }
        /* nexthop module doesn't provide check nhid. */
    }

    if (!pl2_gg_master[lchip]->cfg_has_sw && CTC_FLAG_ISSET(l2_addr->flag, CTC_L2_FLAG_LEARN_LIMIT_EXEMPT))
    {
        return CTC_E_NOT_SUPPORT;
    }

    L2_LOCK;
     /*1. if hit or pending, remove hw first*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_acc_lookup_mac_fid
        (lchip, match_addr->mac, match_addr->fid, &match_result), ret, error_0);
    if (match_result.hit || match_result.pending)
    {
         /*decode match key info*/
        uint8 is_dynamic = 0;
        uint8 update_limit = 0;
        uint8 index = 0;
        uint32 key_index = 0;
        uint8 cf_max = 0;
        sys_l2_calc_index_t calc_index;
        sal_memset(&old_dsmac, 0, sizeof(DsMac_m));
        sal_memset(&calc_index, 0, sizeof(sys_l2_calc_index_t));
        CTC_ERROR_GOTO(_sys_goldengate_get_dsmac_by_index(lchip, match_result.ad_index, &old_dsmac), ret, error_0);
        _sys_goldengate_l2_decode_dsmac(lchip, &old_sys, &old_dsmac, match_result.ad_index);

        match_key_index = match_result.key_index;

         /*check the hash equal*/
        CTC_ERROR_GOTO(_sys_goldengate_l2_fdb_calc_index(lchip, l2_addr->mac, l2_addr->fid, &calc_index), ret, error_0);
        cf_max = calc_index.index_cnt + SYS_L2_FDB_CAM_NUM;
        for (index = 0; index < cf_max; index++)
        {
            key_index = (index >= calc_index.index_cnt) ? (index - calc_index.index_cnt) : calc_index.key_index[index];
            if (match_key_index == key_index)
            {
                match_hit = 1;
                break;
            }
        }
        if (!match_hit)
        {
            ret = CTC_E_INVALID_CONFIG;
            goto  error_0;
        }

        p_match_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, match_addr->mac, match_addr->fid);
        if (p_match_node)
        {
            sal_memcpy(&old_sys.flag.flag_node, &p_match_node->flag, sizeof(sys_l2_flag_node_t));
        }

        if (match_result.pending)
        {    /*pending mac limit en*/
            mac_limit_en = (old_sys.flag.flag_ad.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN
                | DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN
                | DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
        }
        else
        {
            update_limit = p_match_node ? (!p_match_node->flag.limit_exempt) : 1;
            SYS_FDB_IS_DYNAMIC(old_sys.flag.flag_ad, old_sys.flag.flag_node,is_dynamic);
            mac_limit_en = DRV_FIB_ACC_PROFILE_MAC_LIMIT_EN;
            if ((is_dynamic || pl2_gg_master[lchip]->static_fdb_limit) && update_limit)
            {
                mac_limit_en |= (old_sys.flag.flag_ad.logic_port ? 0 : DRV_FIB_ACC_PORT_MAC_LIMIT_EN)
                                | DRV_FIB_ACC_VLAN_MAC_LIMIT_EN;
                if(is_dynamic)
                {
                    mac_limit_en |= DRV_FIB_ACC_DYNAMIC_MAC_LIMIT_EN;
                }
            }
        }
    }

     /* check fid created*/
    fid      = _sys_goldengate_l2_fid_node_lkup(lchip, l2_addr->fid, GET_FID_NODE);
    fid_flag = (fid) ? fid->flag : 0;

    if (!with_nh && (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_0;
    }

    if (CTC_FLAG_ISSET(fid_flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT)
        && (l2_addr->gport >= pl2_gg_master[lchip]->cfg_vport_num))
    {
        ret = CTC_E_INVALID_LOGIC_PORT;
        goto error_0;
    }

     /* map ctc to sys*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_UC, l2_addr, &new_sys, with_nh, nh_id), ret, error_0);

     /*need to save SW for all unreserved ad_index when enable hw learning mode*/
    need_has_sw = pl2_gg_master[lchip]->cfg_has_sw ? 1 : (!new_sys.flag.flag_node.rsv_ad_index);

     /*2. add new sw*/
    if (need_has_sw)
    {
        /*need malloc new ad. */
        MALLOC_ZERO(MEM_FDB_MODULE, pa_new, sizeof(sys_l2_ad_spool_t));
        if (!pa_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_0;
        }

        MALLOC_ZERO(MEM_FDB_MODULE, pn_new, sizeof(sys_l2_node_t));
        if (!pn_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto error_1;
        }

        p_l2_node = pn_new;

        _sys_goldengate_l2_map_sys_to_sw(lchip, &new_sys, p_l2_node, pa_new);
        CTC_ERROR_GOTO(_sys_goldengate_l2_build_dsmac_index
                           (lchip, &new_sys, NULL, pa_new, &pa_get), ret, error_2);
        p_l2_node->adptr = pa_get;
        p_l2_node->flag = new_sys.flag.flag_node;

        if (pa_new != pa_get) /* means get an old. */
        {
            mem_free(pa_new);
            pa_new = NULL;
        }

        CTC_ERROR_GOTO(_sys_goldengate_l2uc_add_sw(lchip, p_l2_node), ret, error_3);

        p_l2_node->adptr->index= new_sys.ad_index;

    }
    /* remove old hw entry */
    if (match_hit)
    {
        CTC_ERROR_GOTO(sys_goldengate_l2_delete_hw_by_mac_fid
                        (lchip, match_addr->mac, match_addr->fid, mac_limit_en), ret, error_4);
        SYS_FDB_DBG_INFO("Remove ucast fdb: key_index:0x%x, ad_index:0x%x\n",
                     match_result.key_index, match_result.ad_index);
    }

     /*3.write new entry hw*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &new_sys), ret, error_5);
    SYS_L2_DUMP_SYS_INFO(&new_sys);

    if (p_l2_node)
    {
        p_l2_node->key_index = new_sys.key_index;
    }

     /*4.remove old sw*/
    if(p_match_node)
    {
        uint8 freed = 0;
        _sys_goldengate_l2uc_remove_sw(lchip, p_match_node);
        _sys_goldengate_l2_free_dsmac_index(lchip, NULL, p_match_node->adptr, &p_match_node->flag, &freed);
        if (freed)
        {
            mem_free(p_match_node->adptr);
        }
        mem_free(p_match_node);
        _sys_goldengate_l2_update_count(lchip, &old_sys.flag.flag_ad,&old_sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &match_key_index);
    }
    if (need_has_sw)
    {
         /*update count*/
        _sys_goldengate_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);
    }

    SYS_FDB_DBG_INFO("replace match hit:%d\n", match_hit);
    L2_UNLOCK;
    return CTC_E_NONE;

 error_5:
    if (match_hit)
    {
        uint32 cmd = 0;
        FDB_SET_MAC(old_sys.mac, match_addr->mac);
        old_sys.fid = match_addr->fid;
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, old_sys.ad_index, cmd, &old_dsmac));
        _sys_goldengate_l2_write_dskey(lchip, &old_sys);
        if (p_match_node)
        {
            p_match_node->key_index = old_sys.key_index;
        }
    }
 error_4:
    if (p_l2_node)
    {
        _sys_goldengate_l2uc_remove_sw(lchip, p_l2_node);
    }
 error_3:
    if (p_l2_node)
    {
        _sys_goldengate_l2_free_dsmac_index(lchip, NULL, pa_get, &p_l2_node->flag, NULL);/* error process don't care freed*/
    }
 error_2:
    if (pn_new)
    {
        mem_free(pn_new);
    }
 error_1:
    if (pa_new)
    {
        mem_free(pa_new);
    }
 error_0:
    L2_UNLOCK;
    return ret;

}

int32
sys_goldengate_l2_replace_fdb(uint8 lchip, ctc_l2_replace_t* p_replace)
{
    mac_lookup_result_t result = {0};

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_replace);
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s, fid:%d, flag:0x%x, gport:0x%x\n",
                      sys_goldengate_output_mac(p_replace->l2_addr.mac), p_replace->l2_addr.fid,
                      p_replace->l2_addr.flag, p_replace->l2_addr.gport);
    SYS_FDB_DBG_PARAM("match-mac:%s, match-fid:%d\n", sys_goldengate_output_mac(p_replace->match_addr.mac), p_replace->match_addr.fid);

    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid
        (lchip, p_replace->l2_addr.mac, p_replace->l2_addr.fid, &result));
    if (result.hit || result.pending)
    {
        CTC_ERROR_RETURN(sys_goldengate_l2_add_fdb(lchip, &p_replace->l2_addr, p_replace->nh_id ? 1 : 0, p_replace->nh_id));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_l2_replace_match_fdb(lchip, p_replace));
    }

    return CTC_E_NONE;
}

#define _REMOVE_GET_BY_INDEX_
/* index is key_index, not aging_ptr*/
int32
sys_goldengate_l2_remove_fdb_by_index(uint8 lchip, uint32 index)
{
    ctc_l2_addr_t l2_addr;

    SYS_L2_INIT_CHECK(lchip);
    SYS_L2_INDEX_CHECK(index);

    CTC_ERROR_RETURN(sys_goldengate_l2_get_fdb_by_index(lchip, index, &l2_addr, NULL));
    CTC_ERROR_RETURN(sys_goldengate_l2_remove_fdb(lchip, &l2_addr));

    return CTC_E_NONE;
}


int32
sys_goldengate_l2_get_fdb_by_index(uint8 lchip, uint32 key_index, ctc_l2_addr_t* l2_addr, sys_l2_detail_t* pi)
{
    sys_l2_info_t     sys;
    DsMac_m          dsmac;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2_addr);
    SYS_L2_INDEX_CHECK(key_index);

    in.rw.tbl_id    = DsFibHost0MacHashKey_t;
    in.rw.key_index = key_index ;
    CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &in, &out));

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    _sys_goldengate_l2_decode_dskey(lchip, &sys, &out.read.data, key_index);

    if (!sys.key_valid)  /*not valid*/
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* get mac, fid */
    l2_addr->fid = sys.fid;
    FDB_SET_MAC(l2_addr->mac, sys.mac);

    /* decode and get gport, flag */
    _sys_goldengate_get_dsmac_by_index(lchip, sys.ad_index, &dsmac);
    _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, sys.ad_index);

    l2_addr->gport = sys.gport;
    _sys_goldengate_l2_unmap_flag(lchip, &l2_addr->flag, &sys.flag);

    if (sys.pending)
    {
        CTC_SET_FLAG(l2_addr->flag, CTC_L2_FLAG_PENDING_ENTRY);
    }
    if (pi)
    {
        pi->key_index   = key_index;
        pi->ad_index    = sys.ad_index;
        pi->flag        = sys.flag;
        pi->pending     = sys.pending;
    }
    return CTC_E_NONE;
}

#define _HW_SYNC_UP_

/**
   @brief update soft tables after hardware learn aging
 */
int32
sys_goldengate_l2_sync_hw_info(uint8 lchip, void* pf)
{
    uint8                    index       = 0;
    DsMac_m                 dsmac;
    sys_l2_info_t            sys;
    sys_learning_aging_data_t data;
    sys_learning_aging_info_t* p_info = NULL;

    SYS_FDB_DBG_FUNC();

    CTC_PTR_VALID_CHECK(pf);
    p_info = (sys_learning_aging_info_t*) pf;
    SYS_FDB_DBG_INFO("Hw total_num  %d\n", p_info->entry_num);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    for (index = 0; index < p_info->entry_num; index++)
    {
        sal_memcpy(&data, &(p_info->data[index]), sizeof(data));
        SYS_FDB_DBG_INFO("   index  %d\n"\
                         "   mac [%s] \n"\
                         "   fid [%d] \n"\
                         "   is_aging  [%d]\n",
                         index,
                         sys_goldengate_output_mac(data.mac),
                         data.fid,
                         data.is_aging);

        if (!data.is_hw)
        {
            continue;
        }

        if (data.is_aging)
        {
            _sys_goldengate_get_dsmac_by_index(lchip, data.ad_index, &dsmac);
            _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, data.ad_index);
            L2_LOCK;
#if 0
            if (pl2_gg_master[lchip]->cfg_has_sw)
            {
                p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, data.mac, data.fid);
                if (!p_l2_node)
                {
                    SYS_FDB_DBG_INFO(" lookup sw failed!!\n");
                    L2_UNLOCK;
                    continue;
                }

                FDB_ERR_RET_UL(_sys_goldengate_l2uc_remove_sw(lchip, p_l2_node));
                FDB_ERR_RET_UL(_sys_goldengate_l2_free_dsmac_index(lchip, &sys, NULL, &freed));

                if(freed)
                {
                    mem_free(p_l2_node->adptr);
                }
                mem_free(p_l2_node);
            }
#endif

            _sys_goldengate_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &data.key_index);
            L2_UNLOCK;
        }
        else  /* learning */
        {
            if ((data.fid != 0) && (data.fid != 0xFFFF)
                && (!pl2_gg_master[lchip] || (data.fid >= pl2_gg_master[lchip]->cfg_max_fid)))
            {
                SYS_FDB_DBG_INFO("learning fid check failed ! ---continue---next one!\n");
                continue;
            }

            L2_LOCK;
#if 0
            if (pl2_gg_master[lchip]->cfg_has_sw)
            {
                p_l2_node = _sys_goldengate_l2_fdb_hash_lkup(lchip, data.mac, data.fid);
                if (p_l2_node) /* update  */
                {
                    /* update learning port */
                    ret = _sys_goldengate_l2_port_list_remove(lchip, p_l2_node);
                    if (ret < 0)
                    {
                        L2_UNLOCK;
                        continue;
                    }

                    SYS_FDB_DBG_INFO("HW learning found old one: \n");
                    FDB_ERR_RET_UL(_sys_goldengate_l2_fdb_get_gport_by_adindex(lchip, data.ad_index, &(p_l2_node->adptr->gport), &is_logic));
                    p_l2_node->key.fid = data.fid;
                    FDB_SET_MAC(p_l2_node->key.mac, data.mac);
                    p_l2_node->key_index    = data.key_index;
                    p_l2_node->adptr->index = data.ad_index;
                    if (is_logic)
                    {
                        p_l2_node->adptr->flag.logic_port = 1;
                    }

                    FDB_ERR_RET_UL(_sys_goldengate_l2_port_list_add(lchip, p_l2_node));
                }
                else
                {
                    MALLOC_ZERO(MEM_FDB_MODULE, p_l2_node, sizeof(sys_l2_node_t));
                    if (NULL == p_l2_node)
                    {
                        L2_UNLOCK;
                        return CTC_E_NO_MEMORY;
                    }

                    FDB_ERR_RET_UL(_sys_goldengate_l2_fdb_get_gport_by_adindex(lchip, data.ad_index, &(p_l2_node->adptr->gport), &is_logic));
                    p_l2_node->key.fid = data.fid;
                    FDB_SET_MAC(p_l2_node->key.mac, data.mac);
                    p_l2_node->key_index    = data.key_index;
                    p_l2_node->adptr->index = data.ad_index;
                    if (is_logic)
                    {
                        p_l2_node->adptr->flag.logic_port = 1;
                    }
                    FDB_ERR_RET_UL(_sys_goldengate_l2uc_add_sw(lchip, p_l2_node));
                }
            }
#endif
            _sys_goldengate_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &data.key_index);
            L2_UNLOCK;
        }
    }

    return CTC_E_NONE;
}

#define _MC_DEFAULT_


STATIC int32
_sys_goldengate_l2_update_nh(uint8 lchip, uint32 nhid, uint8 with_nh, mc_mem_t* member, uint8 is_add)
{
    int32                      ret        = 0;
    uint8                      aps_brg_en = 0;
    uint32                     dest_id    = 0;
    uint32                     m_nhid     = member->nh_id;
    uint16                     m_gport    = member->mem_port;

    sys_nh_param_mcast_group_t nh_mc;
    sal_memset(&nh_mc, 0, sizeof(nh_mc));

    nh_mc.nhid        = nhid;
    nh_mc.dsfwd_valid = 0;
    nh_mc.opcode      = is_add ? SYS_NH_PARAM_MCAST_ADD_MEMBER :
                        SYS_NH_PARAM_MCAST_DEL_MEMBER;

    L2_LOCK;
    if (with_nh)
    {
        CTC_ERROR_GOTO(sys_goldengate_nh_get_port(lchip, m_nhid, &aps_brg_en, &dest_id), ret, error_0);
        nh_mc.mem_info.ref_nhid    = m_nhid;

        if (aps_brg_en && member->is_assign)
        {
            L2_UNLOCK;
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        if (member->is_assign)
        {
            /*Assign mode, dest id using user assign*/
            dest_id                    = m_gport;
            nh_mc.mem_info.member_type = SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID;
        }
        else
        {
            nh_mc.mem_info.member_type = aps_brg_en ? SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE :
                                         SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH;
        }
    }
    else
    {
        dest_id                    = m_gport;
        nh_mc.mem_info.is_linkagg  = 0;
        nh_mc.mem_info.member_type = member->remote_chip? SYS_NH_PARAM_MCAST_MEM_REMOTE : SYS_NH_PARAM_BRGMC_MEM_LOCAL;
    }

    if (aps_brg_en) /* dest_id is aps group id */
    {
        nh_mc.mem_info.destid = dest_id;
    }
    else if(member->remote_chip)
    {
        nh_mc.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(dest_id);
    }
    else /* dest_id is gport or linkagg */
    {

        CTC_GPORT_CHECK_WITH_UNLOCK(dest_id, pl2_gg_master[lchip]->l2_mutex);

        /* trunkid for linkagg, lport for gport */
        nh_mc.mem_info.destid = CTC_MAP_GPORT_TO_LPORT(dest_id);
    }

    if (!member->remote_chip && (aps_brg_en || CTC_IS_LINKAGG_PORT(dest_id)))
    {
        nh_mc.mem_info.is_linkagg = !aps_brg_en;
        nh_mc.mem_info.lchip = lchip;
        CTC_ERROR_GOTO(sys_goldengate_mcast_nh_update(lchip, &nh_mc), ret, error_1);

    }
    else
    {
        if (FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(dest_id)))
        {
            ret = CTC_E_NONE;
            goto error_0;
        }

        nh_mc.mem_info.lchip      = lchip;
        nh_mc.mem_info.is_linkagg = 0;
        CTC_ERROR_GOTO(sys_goldengate_mcast_nh_update(lchip, &nh_mc), ret, error_1);
    }

    L2_UNLOCK;
    return CTC_E_NONE;

 error_1:
    ret = is_add ? CTC_E_FDB_L2MCAST_ADD_MEMBER_FAILED : CTC_E_FDB_L2MCAST_MEMBER_INVALID;
 error_0:
    L2_UNLOCK;
    return ret;
}


STATIC int32
_sys_goldengate_l2_update_l2mc_nh(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr, uint8 is_add)
{
    mac_lookup_result_t rslt;
    DsMac_m            dsmac;
    uint32              nhp_id = 0;
    mc_mem_t mc_mem;
    sys_l2_info_t       sys;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);
    SYS_L2_FID_CHECK(l2mc_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s,fid :%d, member_nd:%d, member_port:%d \n",
                      sys_goldengate_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid, l2mc_addr->member.nh_id, l2mc_addr->member.mem_port);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&mc_mem, 0, sizeof(mc_mem_t));

    mc_mem.is_assign = CTC_FLAG_ISSET(l2mc_addr->flag, CTC_L2_FLAG_ASSIGN_OUTPUT_PORT);
    mc_mem.mem_port = l2mc_addr->member.mem_port;
    mc_mem.nh_id = l2mc_addr->member.nh_id;
    mc_mem.remote_chip = l2mc_addr->remote_chip;

    if (mc_mem.is_assign && !(l2mc_addr->with_nh))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    L2_LOCK;
    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_goldengate_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        FDB_ERR_RET_UL(CTC_E_ENTRY_NOT_EXIST);
    }
    L2_UNLOCK;

    return _sys_goldengate_l2_update_nh(lchip, nhp_id, l2mc_addr->with_nh,
                                        &mc_mem, is_add);
}

int32
_sys_goldengate_l2_get_default_nhid(uint8 lchip, uint16 dflt_fid, uint32 *nhid)
{
    sys_l2_fid_node_t * p_fid_node = NULL;

    L2_LOCK;
    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, dflt_fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        FDB_ERR_RET_UL(CTC_E_ENTRY_NOT_EXIST);
    }

    *nhid = p_fid_node->nhid;
    L2_UNLOCK;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_update_default_nh(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr, uint8 is_add)
{
    sys_l2_fid_node_t * p_fid_node = NULL;
    mc_mem_t mc_mem;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d \n", l2dflt_addr->fid);
    SYS_FDB_DBG_PARAM("member port:0x%x\n", l2dflt_addr->member.mem_port);

    sal_memset(&mc_mem, 0, sizeof(mc_mem_t));
    mc_mem.is_assign = CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_ASSIGN_OUTPUT_PORT);
    mc_mem.mem_port = l2dflt_addr->member.mem_port;
    mc_mem.nh_id = l2dflt_addr->member.nh_id;
    mc_mem.remote_chip = l2dflt_addr->remote_chip;

    if (mc_mem.is_assign && !(l2dflt_addr->with_nh))
    {
        return CTC_E_INVALID_PARAM;
    }

    L2_LOCK;
    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        FDB_ERR_RET_UL(CTC_E_ENTRY_NOT_EXIST);
    }
    L2_UNLOCK;

    return _sys_goldengate_l2_update_nh(lchip, p_fid_node->nhid, l2dflt_addr->with_nh,
                                        &mc_mem, is_add);
}

int32
sys_goldengate_l2mcast_add_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    return _sys_goldengate_l2_update_l2mc_nh(lchip, l2mc_addr, 1);
}
int32
sys_goldengate_l2mcast_remove_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    return _sys_goldengate_l2_update_l2mc_nh(lchip, l2mc_addr, 0);
}

int32
sys_goldengate_l2mcast_add_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    sys_nh_param_mcast_group_t nh_mcast_group;

    int32                      ret     = CTC_E_NONE;
    mac_lookup_result_t        result;
    sys_l2_info_t              new_sys;
    sys_l2_mcast_node_t *p_mc_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);

    SYS_L2_FID_CHECK(l2mc_addr->fid);

    if (SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_L2MC) <= pl2_gg_master[lchip]->l2mc_count)
    {
        return CTC_E_FDB_NO_RESOURCE;
    }

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s, fid :%d, l2mc_group_id: %d  \n",
                      sys_goldengate_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid,
                      l2mc_addr->l2mc_grp_id);

    sal_memset(&new_sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));

     /* 1. lookup hw, check result*/
    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &result));
    if (result.conflict)
    {
        return CTC_E_FDB_HASH_CONFLICT;
    }

    if (result.hit && !l2mc_addr->share_grp_en)
    {
        return CTC_E_ENTRY_EXIST;
    }
    new_sys.is_exist = result.hit;

    L2_LOCK;

     /* 2. map ctc to sys*/
    CTC_ERROR_GOTO(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_MC, l2mc_addr, &new_sys, 0, 0), ret, error_0);

     /* 1. create mcast group*/
    if (l2mc_addr->share_grp_en)
    {
        if (l2mc_addr->l2mc_grp_id)
        {
            CTC_ERROR_GOTO(sys_goldengate_nh_get_mcast_nh(lchip, l2mc_addr->l2mc_grp_id, &nh_mcast_group.nhid), ret, error_0);
        }
        else if (!result.hit)
        {
            new_sys.flag.flag_ad.drop = 1;
        }
    }
    else
    {
        CTC_ERROR_GOTO(sys_goldengate_mcast_nh_create(lchip, l2mc_addr->l2mc_grp_id, &nh_mcast_group), ret, error_0);
    }

     /* 2. add sw. always add for l2mc, because it's unique.*/
    if (result.hit)
    {
        new_sys.ad_index = result.ad_index;
    }
    else
    {
        CTC_ERROR_GOTO(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, &new_sys.ad_index), ret, error_1);
        pl2_gg_master[lchip]->alloc_ad_cnt++;
    }

    CTC_ERROR_GOTO(_sys_goldengate_l2_mc2nhop_add(lchip, l2mc_addr->l2mc_grp_id, nh_mcast_group.nhid),ret, error_2);

     /*4. write hw*/
     new_sys.fwd_ptr = nh_mcast_group.fwd_offset;
     CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &new_sys), ret, error_3);

     SYS_FDB_DBG_INFO("Add mcast fdb: chip_id:%d, key_index:0x%x, ad_index:0x%x, nexthop_id:0x%x, ds_fwd_offset:0x%x\n",
                         lchip, new_sys.key_index, (uint32) new_sys.ad_index, nh_mcast_group.nhid, new_sys.fwd_ptr);

     /*update key_index*/
    p_mc_node = ctc_vector_get(pl2_gg_master[lchip]->mcast2nh_vec, l2mc_addr->l2mc_grp_id);
    p_mc_node->key_index = new_sys.key_index;


     /* 5. update index*/
    _sys_goldengate_l2_update_count(lchip, &new_sys.flag.flag_ad, &new_sys.flag.flag_node, 1, L2_COUNT_GLOBAL, &new_sys.key_index);

    L2_UNLOCK;
    return ret;

 error_3:
    _sys_goldengate_l2_mc2nhop_remove(lchip, l2mc_addr->l2mc_grp_id);
 error_2:
    if (!result.hit)
    {
        sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, new_sys.ad_index);
        pl2_gg_master[lchip]->alloc_ad_cnt--;
    }
 error_1:
    if (!l2mc_addr->share_grp_en)
    {
        sys_goldengate_mcast_nh_delete(lchip, nh_mcast_group.nhid);
    }
 error_0:
    L2_UNLOCK;
    return ret;
}


int32
sys_goldengate_l2mcast_remove_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    int32            ret = CTC_E_NONE;
    sys_l2_info_t       sys;
    DsMac_m            dsmac;
    mac_lookup_result_t rslt;
    uint32 nhp_id = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2mc_addr);
    SYS_L2_FID_CHECK(l2mc_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("mac:%s,fid :%d  \n",
                      sys_goldengate_output_mac(l2mc_addr->mac),
                      l2mc_addr->fid);


    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    L2_LOCK;

    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_goldengate_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (CTC_FLAG_ISSET(l2mc_addr->flag, CTC_L2_FLAG_KEEP_EMPTY_ENTRY))
    {
        ret = sys_goldengate_nh_remove_all_members(lchip, nhp_id);
        L2_UNLOCK;
        return ret;
    }

    /* remove hw entry */
    FDB_ERR_RET_UL(sys_goldengate_l2_delete_hw_by_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid,0));
    SYS_FDB_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove mcast fdb: key_index:0x%x, ad_index:0x%x\n",
                    rslt.key_index, rslt.ad_index);


    /* must lookup success. */
    if(0 != nhp_id)
    {
        if (!sys.share_grp_en)
        {
            /* remove sw entry */
            sys_goldengate_mcast_nh_delete(lchip, nhp_id);
        }
        _sys_goldengate_l2_mc2nhop_remove(lchip, sys.mc_gid);
        sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, rslt.ad_index);
        pl2_gg_master[lchip]->alloc_ad_cnt--;
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }

    _sys_goldengate_l2_update_count(lchip, &sys.flag.flag_ad, &sys.flag.flag_node, -1, L2_COUNT_GLOBAL, &rslt.key_index);

    L2_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_add_port_to_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    return _sys_goldengate_l2_update_default_nh(lchip, l2dflt_addr, 1);
}

int32
sys_goldengate_l2_remove_port_from_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    return _sys_goldengate_l2_update_default_nh(lchip, l2dflt_addr, 0);
}

int32
sys_goldengate_l2_default_entry_set_mac_auth(uint8 lchip, uint16 fid, bool enable)
{
    uint16 ad_index = 0;
    uint32 cmd = 0;
    uint32 value = (enable ? 1 : 0 );

    SYS_L2_INIT_CHECK(lchip);
    ad_index = DEFAULT_ENTRY_INDEX(fid);
    cmd = DRV_IOW(DsMac_t, DsMac_macSaExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_default_entry_get_mac_auth(uint8 lchip, uint16 fid, bool* enable)
{
    uint16 ad_index = 0;
    uint32 cmd = 0;
    uint32  value = 0;

    SYS_L2_INIT_CHECK(lchip);
    ad_index = DEFAULT_ENTRY_INDEX(fid);
    cmd = DRV_IOR(DsMac_t, DsMac_macSaExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));
    *enable = value ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_add_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_info_t              sys;
    int32                      ret     = CTC_E_NONE;
    sys_nh_param_mcast_group_t nh_mcast_group;
    sys_l2_fid_node_t          * p_fid_node = NULL;
    uint32                     nhid = 0;
    uint32                     max_ex_nhid = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);


    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    sal_memset(&nh_mcast_group, 0, sizeof(sys_nh_param_mcast_group_t));
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d ,l2mc_grp_id:%d \n", l2dflt_addr->fid, l2dflt_addr->l2mc_grp_id);

    if (CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_UCAST_DROP))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;  /* not support. */
    }

    if (CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_MCAST_DROP))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;  /* not support. */
    }

    L2_LOCK;
    CTC_ERROR_GOTO(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_DF, l2dflt_addr, &sys, 0, 0), ret, error_0);

    sys.ad_index = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);
    SYS_FDB_DBG_INFO("build hash mcast ad_index:0x%x\n", sys.ad_index);

    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if ((NULL == p_fid_node) || (!p_fid_node->create)) /* node not really created */
    {
        /* 1,create mcast group */
        if (l2dflt_addr->share_grp_en)
        {
            CTC_ERROR_GOTO(sys_goldengate_nh_get_mcast_nh(lchip, l2dflt_addr->l2mc_grp_id, &nhid), ret, error_0);
            sys_goldengate_nh_get_max_external_nhid(lchip, &max_ex_nhid);
            if (nhid > max_ex_nhid)
            {
                ret = CTC_E_FEATURE_NOT_SUPPORT;
                goto  error_0;
            }
        }
        else
        {
            CTC_ERROR_GOTO(sys_goldengate_mcast_nh_create(lchip, l2dflt_addr->l2mc_grp_id, &nh_mcast_group), ret, error_0);
        }

        if (NULL == p_fid_node)
        {
            MALLOC_ZERO(MEM_FDB_MODULE, p_fid_node, sizeof(sys_l2_fid_node_t));
            if (!p_fid_node)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_1;
            }

            p_fid_node->fid_list = ctc_list_new();
            if (NULL == p_fid_node->fid_list)
            {
                ret = CTC_E_NO_MEMORY;
                goto error_2;
            }
        }

        /* map fid node.*/
        p_fid_node->fid          = l2dflt_addr->fid;
        p_fid_node->flag         = l2dflt_addr->flag;
        p_fid_node->nhid         = (l2dflt_addr->share_grp_en)? nhid : nh_mcast_group.nhid;
        p_fid_node->share_grp_en = (l2dflt_addr->share_grp_en)? 1:0;
        p_fid_node->mc_gid       = l2dflt_addr->l2mc_grp_id;
        p_fid_node->create       = 1;
        ret                = ctc_vector_add(pl2_gg_master[lchip]->fid_vec, l2dflt_addr->fid, p_fid_node);
        if (FAIL(ret))
        {
            goto error_3;
        }
    }
    else
    {
        ret = CTC_E_NONE;  /* already created. */
        goto error_0;
    }

    /* write hw */
    ret = _sys_goldengate_l2_write_hw(lchip, &sys);
    if (FAIL(ret))
    {
        goto error_4;
    }

    SYS_FDB_DBG_INFO("Add default entry: chip_id:%d, ad_index:0x%x, group_id: 0x%x, " \
    "nexthop_id:0x%x\n", lchip, sys.ad_index,
                         sys.mc_gid, sys.nhid);

    pl2_gg_master[lchip]->def_count++;
    L2_UNLOCK;
    return CTC_E_NONE;

 error_4:
    ctc_vector_del(pl2_gg_master[lchip]->fid_vec, l2dflt_addr->fid);
 error_3:
    ctc_list_delete(p_fid_node->fid_list);
 error_2:
    mem_free(p_fid_node);
 error_1:
    sys_goldengate_mcast_nh_delete(lchip, sys.nhid);
 error_0:
    L2_UNLOCK;
    return ret;
}


int32
sys_goldengate_l2_remove_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t * p_fid_node = NULL;
    sys_l2_info_t     sys;
    uint8             is_remove    = 0;
    int32             ret = 0;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d  \n", l2dflt_addr->fid);

    L2_LOCK;

    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        ret = CTC_E_ENTRY_NOT_EXIST;
        goto error_0;
    }

    sys.ad_index          = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);
    sys.flag.flag_node.type_default = 1;
    sys.revert_default    = 1;

    /* remove hw */
    CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &sys), ret, error_0);
    SYS_FDB_DBG_INFO("Remove default entry: chip_id:%d, ad_index:0x%x nexthop_id 0x%x\n" \
    , lchip, DEFAULT_ENTRY_INDEX(p_fid_node->fid), p_fid_node->nhid);

    if (!p_fid_node->share_grp_en)
    {
        sys_goldengate_mcast_nh_delete(lchip, p_fid_node->nhid);
    }

    is_remove = p_fid_node->create;
    p_fid_node->create = 0; /* vlan default removed */

    /* remove sw */
    /* we trust users on this. This routine must be called before flush by fid. */
    if (0 == CTC_LISTCOUNT(p_fid_node->fid_list))
    {
        ctc_list_free(p_fid_node->fid_list);
        ctc_vector_del(pl2_gg_master[lchip]->fid_vec, p_fid_node->fid);
        mem_free(p_fid_node);
    }


    if (is_remove)
    {
        pl2_gg_master[lchip]->def_count--;
    }

 error_0:
    L2_UNLOCK;
    return ret;
}

#define _FDB_SHOW_


int32
sys_goldengate_l2_show_status(uint8 lchip)
{
    uint32 ad_cnt = 0;
    uint32 rsv_ad_num = 0;
    uint32 fdb_num = 0;
    uint32 running_num = 0;
    uint32 dynamic_running_num = 0;
    uint32 cmd = 0;
    ctc_l2_fdb_query_t query;
    MacLimitSystem_m mac_limit_system;


    SYS_L2_INIT_CHECK(lchip);

    sal_memset(&query, 0, sizeof(ctc_l2_fdb_query_t));
    sal_memset(&mac_limit_system, 0, sizeof(MacLimitSystem_m));

    sys_goldengate_ftm_query_table_entry_num(lchip, DsMac_t, &ad_cnt);
    rsv_ad_num = SYS_GG_MAX_LINKAGG_GROUP_NUM + SYS_GG_MAX_PORT_NUM_PER_CHIP + (pl2_gg_master[lchip]->cfg_max_fid+1);

    if (!pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
        rsv_ad_num += pl2_gg_master[lchip]->cfg_vport_num;
    }

    fdb_num = sys_goldengate_ftm_get_spec(lchip, CTC_FTM_SPEC_MAC);

    cmd = DRV_IOR(MacLimitSystem_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mac_limit_system));
    GetMacLimitSystem(A, profileRunningCount_f, &mac_limit_system, &running_num);
    GetMacLimitSystem(A, dynamicRunningCount_f, &mac_limit_system, &dynamic_running_num);

    L2_LOCK;
    SYS_FDB_DBG_DUMP("-------------------------Work Mode---------------------\n");
    SYS_FDB_DBG_DUMP("%-30s:%6s \n", "Hardware Learning", pl2_gg_master[lchip]->cfg_hw_learn?"Y":"N");
    SYS_FDB_DBG_DUMP("\n");

    SYS_FDB_DBG_DUMP("-------------------------L2 FDB Resource---------------------\n");
    SYS_FDB_DBG_DUMP("Key Resource:\n");
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Total Key count", fdb_num);
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Used Key count", running_num);

    if (pl2_gg_master[lchip]->cfg_has_sw)
    {
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Local dynamic", pl2_gg_master[lchip]->local_dynamic_count);
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Dynamic", pl2_gg_master[lchip]->dynamic_count);
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Static", pl2_gg_master[lchip]->total_count - pl2_gg_master[lchip]->dynamic_count-pl2_gg_master[lchip]->l2mc_count);
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--L2MC", pl2_gg_master[lchip]->l2mc_count);
    }
    else
    {
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Dynamic", dynamic_running_num);
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Static", running_num - dynamic_running_num);
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--L2MC", pl2_gg_master[lchip]->l2mc_count);
    }

    SYS_FDB_DBG_DUMP("\n");

    SYS_FDB_DBG_DUMP("Ad Resource:\n");
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Total Ad count", ad_cnt);
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Used Ad count", pl2_gg_master[lchip]->alloc_ad_cnt);
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Reserved ", rsv_ad_num);
    SYS_FDB_DBG_DUMP("%-30s:%3u(%u) \n", "--Drop(index)", 1, pl2_gg_master[lchip]->ad_index_drop);

    if (pl2_gg_master[lchip]->cfg_has_sw)
    {
        SYS_FDB_DBG_DUMP("%-30s:%6u \n", "--Dynamic", pl2_gg_master[lchip]->alloc_ad_cnt-rsv_ad_num-1);
    }
    SYS_FDB_DBG_DUMP("\n");

    SYS_FDB_DBG_DUMP("Ad Base:\n");
    if (!pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
        SYS_FDB_DBG_DUMP("%-30s:0x%-4x \n", "Logic Port", pl2_gg_master[lchip]->base_vport);
    }
    SYS_FDB_DBG_DUMP("%-30s:0x%-4x \n", "Port", pl2_gg_master[lchip]->base_gport);
    SYS_FDB_DBG_DUMP("%-30s:0x%-4x \n", "Linkagg", pl2_gg_master[lchip]->base_trunk);
    SYS_FDB_DBG_DUMP("%-30s:0x%-4x \n", "Default Entry", pl2_gg_master[lchip]->dft_entry_base);
    SYS_FDB_DBG_DUMP("\n");

    SYS_FDB_DBG_DUMP("Other Resource:\n");
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Default entry count", pl2_gg_master[lchip]->def_count);
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "FID Number", pl2_gg_master[lchip]->cfg_max_fid+1);
    SYS_FDB_DBG_DUMP("%-30s:%6u \n", "Logic Port Number", pl2_gg_master[lchip]->cfg_vport_num);
    SYS_FDB_DBG_DUMP("\n");

     L2_UNLOCK;
    return CTC_E_NONE;
}

int32
sys_goldengate_l2_dump_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* p_fid_node = NULL;

    SYS_L2_INIT_CHECK(lchip);

    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    if (!p_fid_node->create)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    sys_goldengate_nh_dump(lchip, p_fid_node->nhid, FALSE);
    return CTC_E_NONE;
}

int32
sys_goldengate_l2_dump_all_default_entry(uint8 lchip)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    uint16 fid = 0;
    uint16 pre_entry = 0;
    uint16 last_entry = 0;
    uint8 first_flag = 0;
    uint8 find_first = 0;
    uint8 seq_flag = 0;
    uint16 count = 0;
    uint16 block_cnt = 0;
    char buf[20] = {0};
    char tmp_buf[10] = {0};

    SYS_L2_INIT_CHECK(lchip);

    SYS_FDB_DBG_DUMP("Fid list:\n");
    SYS_FDB_DBG_DUMP("------------------------------------------------------------\n");

    for (fid = 1; fid <= pl2_gg_master[lchip]->cfg_max_fid; fid++)
    {
        p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
        if (NULL == p_fid_node)
        {
            continue;
        }

        if (!p_fid_node->create)
        {
            continue;
        }

        count++;
        last_entry = fid;

        if (find_first == 0)
        {
            first_flag = 1;
        }

        if (first_flag)
        {
            sal_sprintf(buf, "%d",fid);
            find_first = 1;
            first_flag = 0;
            pre_entry = fid;
        }
        else
        {
            /*current fid is not sequenced by last*/
            if (fid != (pre_entry+1))
            {
                /*already have sequence fid list*/
                if (seq_flag)
                {
                    /*print last entry, and seperate remark*/
                    sal_sprintf(tmp_buf, "%d,",pre_entry);
                    sal_strcat((char*)buf, (char*)tmp_buf);
                    SYS_FDB_DBG_DUMP("%-10s ", (char*)buf);
                    block_cnt++;
                    if ((block_cnt%6) == 0)
                    {
                        SYS_FDB_DBG_DUMP("\n");
                    }
                    /*print seperate remark and current fid*/
                    sal_sprintf(buf, "%d",fid);
                }
                else
                {
                    /*print seperate remark and current fid*/
                    sal_strcat((char*)buf, ",");
                    SYS_FDB_DBG_DUMP("%-10s ", (char*)buf);
                    block_cnt++;

                    if ((block_cnt%6) == 0)
                    {
                        SYS_FDB_DBG_DUMP("\n");
                    }
                    sal_sprintf(buf, "%d",fid);
                }

                seq_flag = 0;
            }
            else
            {
                /*already have sequence fid list*/
                if (seq_flag)
                {
                    /*do nothing*/
                }
                else
                {
                    sal_strcat((char*)buf, "~");
                    seq_flag = 1;
                }
            }

            pre_entry = fid;
        }
    }

    if (seq_flag)
    {
        sal_sprintf(tmp_buf, "%d",last_entry);
        sal_strcat((char*)buf, (char*)tmp_buf);
    }

    SYS_FDB_DBG_DUMP("%-10s ", (char*)buf);

    SYS_FDB_DBG_DUMP("\n");
    SYS_FDB_DBG_DUMP("Total Count:%d\n", count);

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_dump_l2mc_entry(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    mac_lookup_result_t rslt;
    sys_l2_info_t       sys;
    DsMac_m            dsmac;
    uint32              nhp_id = 0;

    SYS_L2_INIT_CHECK(lchip);

    sal_memset(&rslt, 0, sizeof(mac_lookup_result_t));
    sal_memset(&dsmac, 0, sizeof(DsMac_m));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));
    CTC_ERROR_RETURN(_sys_goldengate_l2_acc_lookup_mac_fid(lchip, l2mc_addr->mac, l2mc_addr->fid, &rslt));

    if (rslt.hit)
    {
        _sys_goldengate_get_dsmac_by_index(lchip, rslt.ad_index, &dsmac);
        _sys_goldengate_l2_decode_dsmac(lchip, &sys, &dsmac, rslt.ad_index);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    L2_LOCK;

    if(sys.flag.flag_node.type_l2mc)
    {
        _sys_goldengate_l2_get_nhop_by_mc_grp_id(lchip, sys.mc_gid, &nhp_id);
    }
    else
    {
        L2_UNLOCK;
        return CTC_E_ENTRY_NOT_EXIST;
    }
    L2_UNLOCK;

    sys_goldengate_nh_dump(lchip, nhp_id, FALSE);
    return CTC_E_NONE;
}


int32
sys_goldengate_l2_get_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    int32            ret          = 0;
    uint16           fid = 0;
    DsMac_m         macda;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);
    fid = l2dflt_addr->fid;
    sal_memset(l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", l2dflt_addr->fid);
    sal_memset(&macda, 0, sizeof(DsMac_m));

    L2_LOCK;

    p_fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    if (NULL == p_fid_node)
    {
        ret = CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
        goto error_0;
    }

    l2dflt_addr->flag        = p_fid_node->flag;
    l2dflt_addr->fid         = p_fid_node->fid;
    l2dflt_addr->l2mc_grp_id = p_fid_node->mc_gid;

 error_0:
    L2_UNLOCK;
    return ret;
}

STATIC int32
_sys_goldengate_l2_unbind_nhid_by_logic_port(uint8 lchip, uint16 logic_port)
{
    sys_l2_vport_2_nhid_t *p_node;
    int32 ret  = 0;

    p_node = ctc_vector_get(pl2_gg_master[lchip]->vport2nh_vec, logic_port);
    if(p_node)
    {
        if (pl2_gg_master[lchip]->vp_alloc_ad_en)
        {
            ret = sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 0, 1, p_node->ad_idx);
            if (CTC_E_NONE == ret )
            {
                pl2_gg_master[lchip]->alloc_ad_cnt--;
            }

        }

        ctc_vector_del(pl2_gg_master[lchip]->vport2nh_vec, logic_port);
        mem_free(p_node);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    return CTC_E_NONE;

}

int32
sys_goldengate_l2_set_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id)
{

    SYS_LOGIC_PORT_CHECK(logic_port);

    if(0 == nhp_id)
    {
        CTC_ERROR_RETURN(_sys_goldengate_l2_unbind_nhid_by_logic_port(lchip, logic_port));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_l2_bind_nhid_by_logic_port(lchip, logic_port, nhp_id));
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_l2_get_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 *nhp_id)
{
    sys_l2_vport_2_nhid_t *p_node;

    SYS_LOGIC_PORT_CHECK(logic_port);

    p_node = ctc_vector_get(pl2_gg_master[lchip]->vport2nh_vec, logic_port);
    if (NULL == p_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    else
    {
        *nhp_id = p_node->nhid;
    }

    return CTC_E_NONE;
}


#define FDB_INDEX_VEC_B         2048
#define FDB_GPORT_VEC_T         (31 * 512 + SYS_GG_MAX_LINKAGG_GROUP_NUM)
#define FDB_GPORT_VEC_B         128
#define FDB_LOGIC_PORT_VEC_B    64
#define FDB_MAC_HASH_B          1024
#define FDB_MAC_FID_HASH_B      64
#define FDB_TCAM_HASH_B         64
#define FDB_DFT_HASH_T          (4 * 1024)
#define FDB_DFT_HASH_B          1024
#define FDB_MCAST_VEC_B         1024

int32
sys_goldengate_l2_set_static_mac_limit(uint8 lchip, uint8 static_limit_en)
{
    SYS_L2_INIT_CHECK(lchip);
    pl2_gg_master[lchip]->static_fdb_limit = static_limit_en;
    return CTC_E_NONE;
}


/* sw1 always exist:
    default entry, black hole .
    STATIC fdb, mcast fdb */
STATIC int32
_sys_goldengate_l2_init_sw(uint8 lchip)
{
    sys_goldengate_opf_t opf;
    uint32 size = 0;
    if (pl2_gg_master[lchip]->tcam_num)
    {
        pl2_gg_master[lchip]->tcam_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->tcam_num, FDB_TCAM_HASH_B),
                                           FDB_TCAM_HASH_B,
                                           (hash_key_fn) _sys_goldengate_l2_tcam_hash_make,
                                           (hash_cmp_fn) _sys_goldengate_l2_tcam_hash_compare);
        if (!pl2_gg_master[lchip]->tcam_hash)
        {
            return CTC_E_NO_MEMORY;
        }

        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_BLACK_HOLE_CAM, 1));
        opf.pool_index = 0;
        opf.pool_type  = OPF_BLACK_HOLE_CAM;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, pl2_gg_master[lchip]->tcam_num));
    }

    pl2_gg_master[lchip]->fid_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->cfg_max_fid + 1, FDB_DFT_HASH_B),
                                          FDB_DFT_HASH_B);

    if (!pl2_gg_master[lchip]->fid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    if (pl2_gg_master[lchip]->cfg_vport_num)
    {
        pl2_gg_master[lchip]->vport2nh_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->cfg_vport_num, FDB_LOGIC_PORT_VEC_B),
                                                   FDB_LOGIC_PORT_VEC_B);
        if (!pl2_gg_master[lchip]->vport2nh_vec)
        {
            return CTC_E_NO_MEMORY;
        }
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &size));

    pl2_gg_master[lchip]->mcast2nh_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(size/2, FDB_MCAST_VEC_B),
                                               FDB_MCAST_VEC_B);
    if (!pl2_gg_master[lchip]->mcast2nh_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

/* sw0 only when sw exist*/
STATIC int32
_sys_goldengate_l2_init_extra_sw(uint8 lchip)
{
    uint32 ds_mac_size = 0;
    ctc_spool_t spool;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    /* alloc fdb mac hash size  */

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsMac_t, &ds_mac_size));

    spool.lchip = lchip;
    spool.block_num = CTC_VEC_BLOCK_NUM(ds_mac_size, FDB_MAC_FID_HASH_B);
    spool.block_size = FDB_MAC_FID_HASH_B;
    spool.max_count = ds_mac_size;
    spool.user_data_size = sizeof(sys_l2_ad_spool_t);
    spool.spool_key = (hash_key_fn) _sys_goldengate_l2_ad_spool_hash_make;
    spool.spool_cmp = (hash_cmp_fn) _sys_goldengate_l2_ad_spool_hash_cmp;
    pl2_gg_master[lchip]->ad_spool = ctc_spool_create(&spool);

    pl2_gg_master[lchip]->fdb_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->extra_sw_size, FDB_MAC_FID_HASH_B),
                                           FDB_MAC_FID_HASH_B,
                                           (hash_key_fn) _sys_goldengate_l2_fdb_hash_make,
                                           (hash_cmp_fn) _sys_goldengate_l2_fdb_hash_compare);

    pl2_gg_master[lchip]->mac_hash = ctc_hash_create(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->extra_sw_size, FDB_MAC_HASH_B),
                                           FDB_MAC_HASH_B,
                                           (hash_key_fn) _sys_goldengate_l2_mac_hash_make,
                                           (hash_cmp_fn) _sys_goldengate_l2_fdb_hash_compare);

    /* only for ucast */
    pl2_gg_master[lchip]->gport_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(FDB_GPORT_VEC_T, FDB_GPORT_VEC_B),
                                            FDB_GPORT_VEC_B);

    if (pl2_gg_master[lchip]->cfg_vport_num)
    {
        pl2_gg_master[lchip]->vport_vec = ctc_vector_init(CTC_VEC_BLOCK_NUM(pl2_gg_master[lchip]->cfg_vport_num, FDB_LOGIC_PORT_VEC_B),
                                                FDB_LOGIC_PORT_VEC_B);

        if (!pl2_gg_master[lchip]->vport_vec)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    if (!pl2_gg_master[lchip]->fdb_hash ||
        !pl2_gg_master[lchip]->mac_hash ||
        !pl2_gg_master[lchip]->gport_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_init_default_entry(uint8 lchip, uint16 default_entry_num)
{
    uint32                         cmd          = 0;
    uint32                         start_offset = 0;
    uint32                         fld_value    = 0;
    uint16                         fid          = 0;
    sys_l2_info_t                  sys;
    FibEngineLookupResultCtl_m fib_engine_lookup_result_ctl;

    /* get the start offset of DaMac */
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, default_entry_num, &start_offset));
    pl2_gg_master[lchip]->dft_entry_base = start_offset;
    pl2_gg_master[lchip]->alloc_ad_cnt += default_entry_num;

    fld_value = ((pl2_gg_master[lchip]->dft_entry_base >> 8) & 0xFF);

    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl));

    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

    DRV_SET_FIELD_V(FibEngineLookupResultCtl_t,                                        \
                        FibEngineLookupResultCtl_gMacDaLookupResultCtl_defaultEntryBase_f, \
                        &fib_engine_lookup_result_ctl, fld_value);

    DRV_SET_FIELD_V(FibEngineLookupResultCtl_t,                                      \
                        FibEngineLookupResultCtl_gMacDaLookupResultCtl_defaultEntryEn_f, \
                        &fib_engine_lookup_result_ctl, TRUE);

    DRV_SET_FIELD_V(FibEngineLookupResultCtl_t,                                        \
                        FibEngineLookupResultCtl_gMacSaLookupResultCtl_defaultEntryBase_f, \
                        &fib_engine_lookup_result_ctl, fld_value);

    DRV_SET_FIELD_V(FibEngineLookupResultCtl_t,                                      \
                        FibEngineLookupResultCtl_gMacSaLookupResultCtl_defaultEntryEn_f, \
                        &fib_engine_lookup_result_ctl, TRUE);

    cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

    SYS_FDB_DBG_INFO("dft_entry_base = 0x%x \n", pl2_gg_master[lchip]->dft_entry_base);


    sal_memset(&sys, 0, sizeof(sys));
    sys.flag.flag_node.type_default = 1;
    sys.revert_default    = 1;

    for (fid = 0; fid < pl2_gg_master[lchip]->cfg_max_fid; fid++)
    {
        sys.ad_index = DEFAULT_ENTRY_INDEX(fid);
        CTC_ERROR_RETURN(_sys_goldengate_l2_write_hw(lchip, &sys));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_set_global_cfg(uint8 lchip, ctc_l2_fdb_global_cfg_t* pcfg)
{
    CTC_PTR_VALID_CHECK(pcfg);

    /* allocate logic_port_2_nh vec, only used for logic_port use hw learning */
    /* set the num of logic port */
    pl2_gg_master[lchip]->cfg_vport_num = pcfg->logic_port_num;
    pl2_gg_master[lchip]->cfg_hw_learn  = pcfg->hw_learn_en;
    pl2_gg_master[lchip]->init_hw_state = pcfg->hw_learn_en;
    pl2_gg_master[lchip]->cfg_has_sw    = !pl2_gg_master[lchip]->cfg_hw_learn;
    pl2_gg_master[lchip]->cfg_flush_cnt = pcfg->flush_fdb_cnt_per_loop;
    pl2_gg_master[lchip]->trie_sort_en  = pcfg->trie_sort_en;
    if (pl2_gg_master[lchip]->trie_sort_en)
    {
        sys_goldengate_fdb_sort_register_fdb_entry_cb(lchip, sys_goldengate_l2_fdb_dump_all);
        sys_goldengate_fdb_sort_register_trie_sort_cb(lchip, sys_goldengate_l2_fdb_trie_sort_en);
    }

    sys_goldengate_l2_set_static_mac_limit(lchip, pcfg->static_fdb_limit_en);

    if (pcfg->default_entry_rsv_num)
    {
        pl2_gg_master[lchip]->cfg_max_fid = pcfg->default_entry_rsv_num - 1;
    }
    else
    {
        pl2_gg_master[lchip]->cfg_max_fid = 5 * 1024 - 1;     /*default num 5K*/
    }

    /* add default entry */
    CTC_ERROR_RETURN(_sys_goldengate_l2_init_default_entry(lchip, pl2_gg_master[lchip]->cfg_max_fid + 1));

    /* stp init */
    CTC_ERROR_RETURN(sys_goldengate_stp_init(lchip, pcfg->stp_mode));

    if (pl2_gg_master[lchip]->cfg_hw_learn)
    {
        pl2_gg_master[lchip]->vp_alloc_ad_en = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_set_register(uint8 lchip, uint8 hw_en)
{
    uint32                 cmd   = 0;
    FibAccelerationCtl_m fib_acceleration_ctl;
    FibAccDrainEnable_m fib_drain_ctl;
    uint32 value = 0;
    uint32 cmd1 = 0;
    uint32 cmd2 = 0;
    uint32 cmd3 = 0;
    uint32 cmd4 = 0;
    uint32 cmd5 = 0;
    uint32 cmd6 = 0;
    uint32 field_value = 0;
    uint8 lgchip = 0;
    uint8 hw_sync_en = 0;

    sal_memset(&fib_acceleration_ctl, 0, sizeof(fib_acceleration_ctl));
    sal_memset(&fib_drain_ctl, 0, sizeof(FibAccDrainEnable_m));

    cmd = DRV_IOR(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));

    SetFibAccelerationCtl(V, dsMacBase0_f, &fib_acceleration_ctl, pl2_gg_master[lchip]->base_vport);
    SetFibAccelerationCtl(V, dsMacBase1_f, &fib_acceleration_ctl, pl2_gg_master[lchip]->base_gport);
    SetFibAccelerationCtl(V, dsMacBase3_f, &fib_acceleration_ctl, pl2_gg_master[lchip]->base_gport + (SYS_GG_MAX_PORT_NUM_PER_CHIP/2));
    SetFibAccelerationCtl(V, dsMacBase2_f, &fib_acceleration_ctl, pl2_gg_master[lchip]->base_trunk);

    field_value = pl2_gg_master[lchip]->cfg_has_sw? 0 : 1;
    SetFibAccelerationCtl(V, fastAgingIgnoreFifoFull_f, &fib_acceleration_ctl, field_value);
    SetFibAccelerationCtl(V, fastLearningIgnoreFifoFull_f, &fib_acceleration_ctl, field_value);

    SetFibAccelerationCtl(V, macNum_f, &fib_acceleration_ctl, pl2_gg_master[lchip]->pure_hash_num + SYS_L2_FDB_CAM_NUM);

    /* for hw_aging gb alwasy set 1 */
    field_value = pl2_gg_master[lchip]->cfg_hw_learn;
    cmd = DRV_IOW(IpeAgingCtl_t, IpeAgingCtl_hwAgingEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    /* always set hardware aging. like the bit in dsaging. */
    SetFibAccelerationCtl(V, hardwareAgingEn_f, &fib_acceleration_ctl, 1);
    sys_goldengate_get_gchip_id(lchip, &lgchip);
    SetFibAccelerationCtl(V, learningOnStacking_f, &fib_acceleration_ctl, hw_en);
    SetFibAccelerationCtl(V, chipId_f, &fib_acceleration_ctl, lgchip);
    cmd = DRV_IOW(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));



#if 0
    /* set IpeLearningCtl.macTableFull always 1 to support while-list function */
    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_macTableFull_f);
    value = 1;
    if(TRUE)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
#endif


    value = hw_en;
    cmd1 = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_learningOnStacking_f);
    cmd2 = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_learningOnStacking_f);
    cmd3 = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_learningOnStacking_f);
    cmd4 = DRV_IOW(IpeHeaderAdjustCtl_t, IpeHeaderAdjustCtl_learningOnStacking_f);
    cmd5 = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_learningOnStacking_f);
    cmd6 = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_stackingLearningType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd2, &value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd3, &value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd4, &value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd5, &value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd6, &value));

    /*Set acc priority*/
    cmd = DRV_IOR(FibAccDrainEnable_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_drain_ctl));
    SetFibAccDrainEnable(V, forceHighPriorityEn_f, &fib_drain_ctl, 1);
    cmd = DRV_IOW(FibAccDrainEnable_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_drain_ctl));

    CTC_ERROR_RETURN(sys_goldengate_dma_get_hw_learning_sync(lchip, &hw_sync_en));
    CTC_ERROR_RETURN(sys_goldengate_learning_aging_set_hw_sync(lchip, hw_sync_en));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_reserve_port_dsmac(uint8 lchip, uint8 gchip, uint32 ad_base, uint32 rsv_num)
{
    uint32        drv_lport = 0;
    uint32        cmd   = 0;
    DsMac_m      macda;
    uint16        dest_gport = 0;
    sys_l2_info_t sys;
    uint32        nhid = 0;
    uint16        index = 0;
    uint8         slice_en = (pl2_gg_master[lchip]->rchip_ad_rsv[gchip] >> 14) & 0x3;

    sal_memset(&macda, 0, sizeof(DsMac_m));
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);

    for (index = 0; index < rsv_num ; index++)
    {
        if ((rsv_num == SYS_GG_MAX_PORT_NUM_PER_CHIP) || (slice_en == 0) || (slice_en == 1))
        {
            drv_lport = index;
        }
        else if (slice_en == 2)
        {
            drv_lport = index + 256;
        }
        else if (slice_en == 3)
        {
            drv_lport = (index >= rsv_num/2) ? (256 + index - rsv_num/2) : index;
        }

        {
            dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, drv_lport);

            /*get nexthop id from gport*/
            sys_goldengate_l2_get_ucast_nh(lchip, dest_gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC, &nhid);

            _sys_goldengate_l2_map_nh_to_sys(lchip, nhid, &sys, 0, dest_gport, 0);

            CTC_ERROR_RETURN(_sys_goldengate_l2_encode_dsmac(lchip, &macda, &sys));

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_base + index, cmd, &macda));
        }
        SYS_FDB_DBG_INFO("Rsv Dsmac drv_lport: %d, nhid:%d\n", drv_lport, nhid);
    }

    /* write drop index */
    sys.flag.flag_node.type_default = 1;
    sys.revert_default    = 1;
    sys.flag.flag_ad.is_static = 1;
    sys.ad_index = pl2_gg_master[lchip]->ad_index_drop;
    CTC_ERROR_RETURN(_sys_goldengate_l2_write_hw(lchip, &sys));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_reserve_ad_index(uint8 lchip)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;
    uint8 gchip = 0;

    rsv_ad_num = SYS_GG_MAX_LINKAGG_GROUP_NUM + SYS_GG_MAX_PORT_NUM_PER_CHIP;
    if (!pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
         /*only hw learning should reserve AD for logic port*/
        rsv_ad_num += pl2_gg_master[lchip]->cfg_vport_num;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 1, rsv_ad_num, &offset));
    SYS_FDB_DBG_INFO(" logic_port_num: 0x%x  rsv_ad_num:0x%x  offset:0x%x\n",
                     pl2_gg_master[lchip]->cfg_vport_num, rsv_ad_num, offset);

    if (!pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
        pl2_gg_master[lchip]->base_vport = offset + SYS_GG_MAX_LINKAGG_GROUP_NUM + SYS_GG_MAX_PORT_NUM_PER_CHIP;
    }
    pl2_gg_master[lchip]->base_gport = offset + SYS_GG_MAX_LINKAGG_GROUP_NUM;
    pl2_gg_master[lchip]->base_trunk = offset;
    pl2_gg_master[lchip]->alloc_ad_cnt += rsv_ad_num;

    sys_goldengate_get_gchip_id(lchip, &gchip);
    CTC_ERROR_RETURN(_sys_goldengate_l2_reserve_port_dsmac(lchip, gchip,  pl2_gg_master[lchip]->base_gport, SYS_GG_MAX_PORT_NUM_PER_CHIP));


    return CTC_E_NONE;
}

/* for stacking
*  slice_en,means reserve for different slice : 1-slice0, 2-slice1, 3-slice0&slice1
**/
int32
sys_goldengate_l2_reserve_rchip_ad_index(uint8 lchip, uint8 gchip, uint16 max_port_num, uint8 slice_en)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;

    uint32 cmd = 0;
    FibAccelerationCtl_m fib_acceleration_ctl;

    SYS_L2_INIT_CHECK(lchip);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*if gchip is local, return */
    if (sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }

    rsv_ad_num = max_port_num;
    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 1, rsv_ad_num, &offset));
    pl2_gg_master[lchip]->alloc_ad_cnt += rsv_ad_num;

    /*bit14--slice0, bit15--slice1*/
    pl2_gg_master[lchip]->rchip_ad_rsv[gchip] = (slice_en & 0x3) << 14;
    CTC_ERROR_RETURN(_sys_goldengate_l2_reserve_port_dsmac(lchip, gchip, offset, rsv_ad_num));

    if (pl2_gg_master[lchip]->cfg_hw_learn)
    {
        cmd = DRV_IOR(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));
        DRV_IOW_FIELD(FibAccelerationCtl_t, FibAccelerationCtl_array_0_remoteChipAdBase_f+gchip, &offset, &fib_acceleration_ctl);
        cmd = DRV_IOW(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));
    }

    L2_LOCK;
    pl2_gg_master[lchip]->base_rchip[gchip] = offset;
    pl2_gg_master[lchip]->rchip_ad_rsv[gchip] += rsv_ad_num;
    L2_UNLOCK;
    SYS_FDB_DBG_INFO("DsMac rsv num for rchip:%d, base:0x%x\n",rsv_ad_num,offset);

    return CTC_E_NONE;
}

/* for stacking */
int32
sys_goldengate_l2_free_rchip_ad_index(uint8 lchip, uint8 gchip)
{
    uint32 rsv_ad_num = 0;
    uint32 offset     = 0;
    int32  ret        = 0;
    uint32 cmd = 0;
    FibAccelerationCtl_m fib_acceleration_ctl;

    SYS_L2_INIT_CHECK(lchip);
    SYS_GLOBAL_CHIPID_CHECK(gchip);

    /*if gchip is local, return */
    if (sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if ((pl2_gg_master[lchip]->rchip_ad_rsv[gchip] & 0x3FFF) == 0)
    {
        return CTC_E_NONE;
    }
    L2_LOCK;
    offset = pl2_gg_master[lchip]->base_rchip[gchip];
    rsv_ad_num = (pl2_gg_master[lchip]->rchip_ad_rsv[gchip]&0x3FFF);
    CTC_ERROR_GOTO(sys_goldengate_ftm_free_table_offset(lchip, DsMac_t, 1, rsv_ad_num, offset),ret, error_0);
    pl2_gg_master[lchip]->alloc_ad_cnt =  pl2_gg_master[lchip]->alloc_ad_cnt - rsv_ad_num;
    pl2_gg_master[lchip]->rchip_ad_rsv[gchip] = 0;
    L2_UNLOCK;

    if (pl2_gg_master[lchip]->cfg_hw_learn)
    {
        cmd = DRV_IOR(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));
        DRV_IOW_FIELD(FibAccelerationCtl_t, FibAccelerationCtl_array_0_remoteChipAdBase_f+gchip, &offset, &fib_acceleration_ctl);
        cmd = DRV_IOW(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acceleration_ctl));
    }

    return CTC_E_NONE;
error_0:
    L2_UNLOCK;
    return CTC_E_NONE;

}
STATIC int32
_sys_goldengate_l2_set_hw_learn(uint8 lchip, uint8 hw_en)
{
    uint32 cmd      = 0;
    uint32 value    = 0;

    /* a nicer solution. */
    value = hw_en ? 1:0;
    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_fastLearningEn_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    pl2_gg_master[lchip]->cfg_hw_learn  = hw_en;
    pl2_gg_master[lchip]->cfg_has_sw    = !pl2_gg_master[lchip]->cfg_hw_learn;

    return _sys_goldengate_l2_set_register(lchip, hw_en);
}

int32
sys_goldengate_l2_set_hw_learn(uint8 lchip, uint8 hw_en)
{
    SYS_L2_INIT_CHECK(lchip);

    if(pl2_gg_master[lchip]->cfg_hw_learn == hw_en)
    {
        return CTC_E_NONE;
    }

    if (pl2_gg_master[lchip]->total_count)
    {
        return CTC_E_FDB_ONLY_SW_LEARN;
    }
    if ((1 == pl2_gg_master[lchip]->init_hw_state) && (0 == hw_en))
    {
        return CTC_E_FDB_ONLY_HW_LEARN;
    }

    if(pl2_gg_master[lchip]->vp_alloc_ad_en)
    {
        return CTC_E_FDB_ONLY_SW_LEARN;
    }

    return _sys_goldengate_l2_set_hw_learn(lchip, hw_en);
}



int32
sys_goldengate_l2_mc_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_L2_INIT_CHECK(lchip);

    specs_info->used_size = pl2_gg_master[lchip]->l2mc_count;

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_fdb_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    uint32 cmd = 0;
    uint32 value = 0;

    cmd = DRV_IOR(MacLimitSystem_t, MacLimitSystem_profileRunningCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    specs_info->used_size = value;

    return CTC_E_NONE;
}

bool
sys_goldengate_l2_fdb_trie_sort_en(uint8 lchip)
{
    if (NULL == pl2_gg_master[lchip])
    {
        return FALSE;
    }
    if (pl2_gg_master[lchip]->trie_sort_en)
    {
        return TRUE;
    }

    return FALSE;
}
/**internal test**/
int32
sys_goldengate_l2_fdb_set_trie_sort(uint8 lchip, uint8 enable)
{
    SYS_L2_INIT_CHECK(lchip);

    pl2_gg_master[lchip]->trie_sort_en = enable;

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_fdb_init(uint8 lchip, void* l2_fdb_global_cfg)
{
    uint32 hash_entry_num = 0;
    uint32 tcam_entry_num = 0;
    int32  ret            = 0;
    uint32 spec_fdb = 0;
    uint32 cmd = 0;
    uint16 fid = 0;

    if (pl2_gg_master[lchip]) /*already init */
    {
        return CTC_E_NONE;
    }

    /* get num of Hash Key */
    sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost0MacHashKey_t, &hash_entry_num);
    sys_goldengate_ftm_query_table_entry_num(lchip, DsFibMacBlackHoleHashKey_t, &tcam_entry_num);
    if (0 == hash_entry_num)
    {
        return CTC_E_FDB_NO_RESOURCE;
    }

    MALLOC_ZERO(MEM_FDB_MODULE, pl2_gg_master[lchip], sizeof(sys_l2_master_t));
    if (!pl2_gg_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    ret = sal_mutex_create(&(pl2_gg_master[lchip]->l2_mutex));
    if (ret || !(pl2_gg_master[lchip]->l2_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }

    ret = sal_mutex_create(&(pl2_gg_master[lchip]->l2_dump_mutex));
    if (ret || !(pl2_gg_master[lchip]->l2_dump_mutex))
    {
        return CTC_E_NO_RESOURCE;
    }

    pl2_gg_master[lchip]->pure_hash_num = hash_entry_num;
    pl2_gg_master[lchip]->tcam_num      = tcam_entry_num;

     /*if set, indicate logic port alloc ad_index by opf when bind nexthop,only supported in SW learning mode*/
    pl2_gg_master[lchip]->vp_alloc_ad_en = 0;

    CTC_ERROR_RETURN(_sys_goldengate_l2_set_global_cfg(lchip, l2_fdb_global_cfg));

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsMac_t, 0, 1, &(pl2_gg_master[lchip]->ad_index_drop)));
    pl2_gg_master[lchip]->alloc_ad_cnt++;

    CTC_ERROR_RETURN(_sys_goldengate_l2_reserve_ad_index(lchip));

    pl2_gg_master[lchip]->extra_sw_size = pl2_gg_master[lchip]->pure_hash_num + SYS_L2_FDB_CAM_NUM;
    CTC_ERROR_RETURN(_sys_goldengate_l2_init_extra_sw(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_l2_init_sw(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_l2_set_hw_learn(lchip, pl2_gg_master[lchip]->cfg_hw_learn));

    CTC_ERROR_RETURN(sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_L2MC,
                                                sys_goldengate_l2_mc_ftm_cb));

    CTC_ERROR_RETURN(sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_MAC,
                                                sys_goldengate_l2_fdb_ftm_cb));

    spec_fdb = sys_goldengate_ftm_get_spec(lchip, CTC_FTM_SPEC_MAC);
    spec_fdb = (spec_fdb > 0x7FFFF) ? 0x7FFFF : spec_fdb;
    cmd = DRV_IOW(MacLimitSystem_t, MacLimitSystem_profileMaxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &spec_fdb));

    for(fid = pl2_gg_master[lchip]->cfg_max_fid+1;fid <= 0x3FFF; fid++)
    {
        _sys_goldengate_l2_set_fid_property(lchip, fid, CTC_L2_FID_PROP_LEARNING, 0);
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_LOGIC_PORT_NUM, pl2_gg_master[lchip]->cfg_vport_num));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_FID, pl2_gg_master[lchip]->cfg_max_fid));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_BLACK_HOLE_ENTRY_NUM, pl2_gg_master[lchip]->tcam_num));
    CTC_ERROR_RETURN(sys_goldengate_nh_vxlan_vni_init(lchip, pl2_gg_master[lchip]->cfg_max_fid));
    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_L2, sys_goldengate_l2_fdb_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_l2_fdb_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_fdb_free_node_data(void* node_data, void* user_data)
{
    sys_l2_fid_node_t* p_fid_node = NULL;
    sys_l2_port_node_t* p_port_node = NULL;

    /*release fid vector list*/
    if (user_data)
    {
        if (2 == *(uint32*)user_data)
        {
            p_fid_node = (sys_l2_fid_node_t*)node_data;
            ctc_list_delete(p_fid_node->fid_list);
        }
        else if (1 == *(uint32*)user_data)
        {
            p_port_node = (sys_l2_port_node_t*)node_data;
            ctc_list_free(p_port_node->port_list);
        }
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_fdb_deinit(uint8 lchip)
{
    uint32 free_port_list = 1;
    uint32 free_fid_list = 2;

    LCHIP_CHECK(lchip);
    if (NULL == pl2_gg_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (pl2_gg_master[lchip]->cfg_has_sw)
    {
        /*free ad data*/
        ctc_spool_free(pl2_gg_master[lchip]->ad_spool);
        /*free fdb hash data*/
        ctc_hash_traverse(pl2_gg_master[lchip]->fdb_hash, (hash_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, NULL);
        ctc_hash_free(pl2_gg_master[lchip]->fdb_hash);
        /*free mac data*/
        /*ctc_hash_traverse(pl2_gg_master[lchip]->mac_hash, (hash_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, NULL);*/
        ctc_hash_free(pl2_gg_master[lchip]->mac_hash);
        /*free gport data*/
        ctc_vector_traverse(pl2_gg_master[lchip]->gport_vec, (vector_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, &free_port_list);
        ctc_vector_release(pl2_gg_master[lchip]->gport_vec);
        if (pl2_gg_master[lchip]->cfg_vport_num)
        {
            /*free vport data*/
            ctc_vector_traverse(pl2_gg_master[lchip]->vport_vec, (vector_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, &free_port_list);
            ctc_vector_release(pl2_gg_master[lchip]->vport_vec);
        }
    }

    if (pl2_gg_master[lchip]->tcam_num)
    {
        /*free tcam data*/
        ctc_hash_traverse(pl2_gg_master[lchip]->tcam_hash, (hash_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, NULL);
        ctc_hash_free(pl2_gg_master[lchip]->tcam_hash);
        sys_goldengate_opf_deinit(lchip, OPF_BLACK_HOLE_CAM);
    }

    /*free fid data*/
    ctc_vector_traverse(pl2_gg_master[lchip]->fid_vec, (vector_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, &free_fid_list);
    ctc_vector_release(pl2_gg_master[lchip]->fid_vec);
    if (pl2_gg_master[lchip]->cfg_vport_num)
    {
        /*free vport2nh data*/
        ctc_vector_traverse(pl2_gg_master[lchip]->vport2nh_vec, (vector_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, NULL);
        ctc_vector_release(pl2_gg_master[lchip]->vport2nh_vec);
    }
    /*free mcast2nh data*/
    ctc_vector_traverse(pl2_gg_master[lchip]->mcast2nh_vec, (vector_traversal_fn)_sys_goldengate_l2_fdb_free_node_data, NULL);
    ctc_vector_release(pl2_gg_master[lchip]->mcast2nh_vec);

    sys_goldengate_stp_deinit(lchip);

    sal_mutex_destroy(pl2_gg_master[lchip]->l2_mutex);
    sal_mutex_destroy(pl2_gg_master[lchip]->l2_dump_mutex);
    mem_free(pl2_gg_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_get_unknown_mcast_tocpu(uint8 lchip, uint16 fid, uint32* value)
{
    sys_l2_fid_node_t* fid_node = NULL;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    SYS_FDB_DBG_FUNC();

    fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, fid, GET_FID_NODE);
    *value = (NULL == fid_node) ? 0 : (fid_node->unknown_mcast_tocpu ? 1 : 0);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l2_set_fid_property(uint8 lchip, uint16 fid_id, uint32 fid_prop, uint32 value)
{
    uint32 field_id_0 = 0;
    uint32 field_id   = 0;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 tableid    = DsVsi_t;
    uint8  step       = DsVsi_array_1_bcastDiscard_f - DsVsi_array_0_bcastDiscard_f;
    uint16 stats_ptr  = 0;
    uint32 unknown_mcast_tocpu_nhid = 0;
    uint32 unknown_mcast_tocpu = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    sys_l2_fid_node_t* fid_node = NULL;

    switch (fid_prop)
    {
    case CTC_L2_FID_PROP_LEARNING:
        field_id_0 = DsVsi_array_0_vsiLearningDisable_f;
        value      = (value) ? 0 : 1;
        break;

    case CTC_L2_FID_PROP_IGMP_SNOOPING:
        field_id_0 = DsVsi_array_0_igmpSnoopEn_f;
        value      = (value) ? 1 : 0;
        break;

    case CTC_L2_FID_PROP_DROP_UNKNOWN_MCAST:
        field_id_0 = DsVsi_array_0_mcastDiscard_f;
        value      = (value) ? 1 : 0;
        break;

    case CTC_L2_FID_PROP_DROP_UNKNOWN_UCAST:
        field_id_0 = DsVsi_array_0_ucastDiscard_f;
        value      = (value) ? 1 : 0;
        break;

    case CTC_L2_FID_PROP_DROP_BCAST:
        field_id_0 = DsVsi_array_0_bcastDiscard_f;
        value      = (value) ? 1 : 0;
        break;

    case CTC_L2_FID_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_l2_get_fid_property(lchip, fid_id, CTC_L2_FID_PROP_UNKNOWN_MCAST_COPY_TO_CPU, &unknown_mcast_tocpu));

        value = (value) ? 1 : 0;
        if (value == unknown_mcast_tocpu)
        {
            return CTC_E_NONE;
        }

        L2_LOCK;
        fid_node = _sys_goldengate_l2_fid_node_lkup(lchip, fid_id, GET_FID_NODE);
        if (NULL == fid_node)
        {
            L2_UNLOCK;
            return CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
        }

        fid_node->unknown_mcast_tocpu = value;

        L2_UNLOCK;

        if (0 == pl2_gg_master[lchip]->unknown_mcast_tocpu_nhid)
        {
            CTC_ERROR_RETURN(sys_goldengate_common_init_unknown_mcast_tocpu(lchip, &unknown_mcast_tocpu_nhid));
            pl2_gg_master[lchip]->unknown_mcast_tocpu_nhid = unknown_mcast_tocpu_nhid;
        }

        l2dflt_addr.fid = fid_id;
        l2dflt_addr.with_nh = 1;
        l2dflt_addr.member.nh_id = pl2_gg_master[lchip]->unknown_mcast_tocpu_nhid;

        if (value)
        {
            CTC_ERROR_RETURN(sys_goldengate_l2_add_port_to_default_entry(lchip, &l2dflt_addr));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_l2_remove_port_from_default_entry(lchip, &l2dflt_addr));
        }

        return CTC_E_NONE;

    case CTC_L2_FID_PROP_IGS_STATS_EN:
        if (value == 0) /* disable stats */
        {
            stats_ptr = 0;
        }
        else    /* enable stats */
        {
            CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, value, CTC_STATS_STATSID_TYPE_FID, &stats_ptr));
        }
        value = stats_ptr;

        field_id_0 = DsVsi_array_0_fwdStatsPtr_f;
        break;

    case CTC_L2_FID_PROP_EGS_STATS_EN:
        if (value == 0) /* disable stats */
        {
            stats_ptr = 0;
        }
        else    /* enable stats */
        {
            CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, value, CTC_STATS_STATSID_TYPE_FID, &stats_ptr));
        }
        value = stats_ptr;

        field_id_0 = DsEgressVsi_array_0_statsPtr_f;
        tableid = DsEgressVsi_t;
        step =DsEgressVsi_array_1_statsPtr_f -  DsEgressVsi_array_0_statsPtr_f;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    field_id = field_id_0 + (fid_id & 0x3) * step;
    cmd      = DRV_IOW(tableid, field_id);
    index    = (fid_id >> 2) & 0xFFF;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));


    return CTC_E_NONE;
}
int32
sys_goldengate_l2_set_fid_property(uint8 lchip, uint16 fid_id, uint32 fid_prop, uint32 value)
{
    SYS_L2_INIT_CHECK(lchip);
    SYS_L2_FID_CHECK(fid_id);
    CTC_ERROR_RETURN(_sys_goldengate_l2_set_fid_property(lchip, fid_id, fid_prop, value));

    return CTC_E_NONE;
}
int32
sys_goldengate_l2_get_fid_property(uint8 lchip, uint16 fid_id, uint32 fid_prop, uint32* value)
{
    uint32 field_id_0 = 0;
    uint32 field_id   = 0;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint8  step       = DsVsi_array_1_bcastDiscard_f - DsVsi_array_0_bcastDiscard_f;

    SYS_L2_FID_CHECK(fid_id);
    CTC_PTR_VALID_CHECK(value);

    switch (fid_prop)
    {
    case CTC_L2_FID_PROP_LEARNING:
        field_id_0 = DsVsi_array_0_vsiLearningDisable_f;
        break;

    case CTC_L2_FID_PROP_IGMP_SNOOPING:
        field_id_0 = DsVsi_array_0_igmpSnoopEn_f;
        break;

    case CTC_L2_FID_PROP_DROP_UNKNOWN_MCAST:
        field_id_0 = DsVsi_array_0_mcastDiscard_f;
        break;

    case CTC_L2_FID_PROP_DROP_UNKNOWN_UCAST:
        field_id_0 = DsVsi_array_0_ucastDiscard_f;
        break;

    case CTC_L2_FID_PROP_DROP_BCAST:
        field_id_0 = DsVsi_array_0_bcastDiscard_f;
        break;

    case CTC_L2_FID_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        L2_LOCK;
        _sys_goldengate_l2_get_unknown_mcast_tocpu(lchip, fid_id, value);
        L2_UNLOCK;

        return CTC_E_NONE;

    default:
        return CTC_E_INVALID_PARAM;
    }

    field_id = field_id_0 + (fid_id & 0x3) * step;
    cmd      = DRV_IOR(DsVsi_t, field_id);
    index    = (fid_id >> 2) & 0xFFF;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, value));

    if (CTC_L2_FID_PROP_LEARNING == fid_prop)
    {
        *value = !(*value);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_set_port_security(uint8 lchip, uint16 gport, uint8 enable, uint8 is_logic)
{
    uint32 cmd = 0;
    uint32 ad_index = 0;
    uint32 src_discard = (enable)?1:0;
    DsMac_m dsmac;

    SYS_L2_INIT_CHECK(lchip);

    if (pl2_gg_master[lchip]->cfg_hw_learn)
    {
        if (is_logic)
        {
             ad_index = pl2_gg_master[lchip]->base_vport + gport;  /* logic port */
        }
        else
        {
            if (CTC_IS_LINKAGG_PORT(gport))
            {
                ad_index = pl2_gg_master[lchip]->base_trunk + CTC_GPORT_LINKAGG_ID(gport); /* linkagg */
            }
            else
            {
                ad_index = pl2_gg_master[lchip]->base_gport + SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport); /* gport */
            }
        }

        /* update ad */
        cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &dsmac));
        SetDsMac(V, srcMismatchDiscard_f, &dsmac, src_discard);
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &dsmac));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_set_station_move(uint8 lchip, uint16 gport, uint8 type, uint32 value)
{
    uint32 cmd = 0;
    uint32 ad_index = 0;
    uint32 field_val = 0;
    uint16 lport = 0;
    uint8  gchip = 0;
    uint8  is_local_chip = 0;

    SYS_L2_INIT_CHECK(lchip);

    if (MAX_SYS_L2_STATION_MOVE_OP <= type)
    {
        return CTC_E_INVALID_PARAM;
    }
    CTC_GLOBAL_PORT_CHECK(gport);

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        field_val = gport;
    }
    else
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
        CTC_GLOBAL_CHIPID_CHECK(gchip);
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        is_local_chip = sys_goldengate_chip_is_local(lchip, gchip);
        if (is_local_chip)
        {
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_globalSrcPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
        }
    }

    if (SYS_DRV_IS_LINKAGG_PORT(field_val) || CTC_IS_LINKAGG_PORT(gport))
    {
        /* linkagg */
        ad_index = pl2_gg_master[lchip]->base_trunk + (field_val & CTC_LINKAGGID_MASK);
    }
    else if (is_local_chip)
    {

        ad_index = pl2_gg_master[lchip]->base_gport + lport; /* gport */
    }
    else if (pl2_gg_master[lchip]->rchip_ad_rsv[gchip])
    {
        uint8   slice_en = (pl2_gg_master[lchip]->rchip_ad_rsv[gchip] >> 14) & 0x3;
        uint16  rsv_num = pl2_gg_master[lchip]->rchip_ad_rsv[gchip] & 0x3FFF;
        uint32  ad_base = pl2_gg_master[lchip]->base_rchip[gchip];

        if (slice_en == 1)
        {
            /*slice 0*/
            if (lport >= rsv_num)
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport + ad_base;
        }
        else if (slice_en == 3)
        {
            if (((lport >= 256) && (lport >= (256 + rsv_num/2)))
                || ((lport < 256) && (lport >= rsv_num/2)))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = ((lport >= 256) ? (lport - 256 + rsv_num/2) : lport) + ad_base;
        }
        else if (slice_en == 2)
        {
            /*slice 1*/
            if (((lport&0xFF) >= rsv_num) || (lport < 256))
            {
                return CTC_E_INVALID_PARAM;
            }
            ad_index = lport - 256 + ad_base;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_NONE;
    }

    /* update ad */
    if (SYS_L2_STATION_MOVE_OP_PRIORITY == type)
    {
        field_val = (value == 0) ? 1 : 0;
        cmd = DRV_IOW(DsMac_t, DsMac_srcMismatchLearnEn_f);
    }
    else if (SYS_L2_STATION_MOVE_OP_ACTION == type)
    {
       field_val = (value != CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD) ? 1 : 0;
       cmd = DRV_IOW(DsMac_t, DsMac_srcMismatchDiscard_f);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &field_val));

    SYS_FDB_DBG_INFO("gchip:%d ad_index: 0x%x\n",gchip, ad_index);
    return CTC_E_NONE;
}

int32
sys_goldengate_l2_get_station_move(uint8 lchip, uint16 gport, uint8 type, uint32* value)
{
    uint32 cmd = 0;
    uint32 ad_index = 0;
    uint32 field_val = 0;
    uint16 lport = 0;

    SYS_L2_INIT_CHECK(lchip);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_globalSrcPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    if (SYS_DRV_IS_LINKAGG_PORT(field_val))
    {
        ad_index = pl2_gg_master[lchip]->base_trunk + (field_val & SYS_DRV_LOCAL_PORT_MASK); /* linkagg */
    }
    else
    {
        ad_index = pl2_gg_master[lchip]->base_gport + lport; /* gport */
    }

    /* update ad */
    if (SYS_L2_STATION_MOVE_OP_PRIORITY == type)
    {
        cmd = DRV_IOR(DsMac_t, DsMac_srcMismatchLearnEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &field_val));
        *value = field_val ? 0 : 1;
    }
    else if (SYS_L2_STATION_MOVE_OP_ACTION == type)
    {
        cmd = DRV_IOR(DsMac_t, DsMac_srcMismatchDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &field_val));
        *value = field_val ? CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD : CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    sys_l2_fid_node_t* fid   = NULL;
    int32            ret     = 0;
    sys_l2_info_t    sys;

    SYS_L2_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(l2dflt_addr);
    SYS_L2_FID_CHECK(l2dflt_addr->fid);

    SYS_FDB_DBG_FUNC();
    SYS_FDB_DBG_PARAM("fid :%d\n", l2dflt_addr->fid);
    sal_memset(&sys, 0, sizeof(sys_l2_info_t));

    if (CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_UCAST_DROP))
    {
        return  CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (CTC_FLAG_ISSET(l2dflt_addr->flag, CTC_L2_DFT_VLAN_FLAG_UNKOWN_MCAST_DROP))
    {
        return  CTC_E_FEATURE_NOT_SUPPORT;
    }

    L2_LOCK;

    fid = _sys_goldengate_l2_fid_node_lkup(lchip, l2dflt_addr->fid, GET_FID_NODE);
    if (NULL == fid)
    {
        ret = CTC_E_FDB_DEFAULT_ENTRY_NOT_EXIST;
        goto error_0;
    }

    fid->flag = l2dflt_addr->flag;
    l2dflt_addr->l2mc_grp_id = fid->mc_gid;

    CTC_ERROR_GOTO(_sys_goldengate_l2_map_ctc_to_sys(lchip, L2_TYPE_DF, l2dflt_addr, &sys, 0, 0), ret, error_0);
    sys.ad_index          = DEFAULT_ENTRY_INDEX(l2dflt_addr->fid);

    CTC_ERROR_GOTO(_sys_goldengate_l2_write_hw(lchip, &sys), ret, error_0);


 error_0:
    L2_UNLOCK;
    return ret;
}


int32
sys_goldengate_l2_dump_fdb_entry_detail(uint8 lchip, ctc_l2_fdb_query_t* pq,
        uint16 entry_cnt, uint8 need_check, void* p_file)
{
    sys_l2_detail_t* detail_info = NULL;
    int32 ret = 0;
    uint16 index = 0;
    uint16 pending = 0;
    ctc_l2_fdb_query_rst_t query_rst;
    uint32 total_count = 0;
    sal_file_t fp = NULL;
    char* char_buffer = NULL;
    uint8 hit = 0;

    SYS_L2_INIT_CHECK(lchip);
    SYS_FDB_DBG_FUNC();
    CTC_PTR_VALID_CHECK(pq);

    if (!(pl2_gg_master[lchip]->cfg_hw_learn) &&(pq->query_hw)&&need_check)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (p_file)
    {
        fp = (sal_file_t)p_file;
        char_buffer = (char*)mem_malloc(MEM_FDB_MODULE, 128);
        if (NULL == char_buffer)
        {
            return CTC_E_NO_MEMORY;
        }

        SYS_FDB_DBG_DUMP("Please read entry-file after seeing \"FDB entry have been writen!\"\n");
    }

    /* alloc memory for detail struct */
    detail_info = (sys_l2_detail_t*)mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_detail_t) * entry_cnt);
    if (NULL == detail_info)
    {
        if (char_buffer)
        {
            mem_free(char_buffer);
        }
        return CTC_E_NO_MEMORY;
    }

    sal_memset(&query_rst, 0, sizeof(ctc_l2_fdb_query_rst_t));
    sal_memset(detail_info, 0, sizeof(sys_l2_detail_t) * entry_cnt);

    query_rst.buffer_len = sizeof(ctc_l2_addr_t) * entry_cnt;
    query_rst.buffer = (ctc_l2_addr_t*)mem_malloc(MEM_FDB_MODULE, query_rst.buffer_len);
    if (NULL == query_rst.buffer)
    {
        mem_free(detail_info);
        if (char_buffer)
        {
            mem_free(char_buffer);
        }
        return CTC_E_NO_MEMORY;
    }

    sal_memset(query_rst.buffer, 0, query_rst.buffer_len);

    if (p_file)
    {
        sal_fprintf(fp, "%-10s: DsFibHost0MacHashKey \n", "Key Table");
        sal_fprintf(fp, "%-10s: DsMac \n", "Ad Table");
        sal_fprintf(fp, "\n");

        sal_fprintf(fp, "%8s  %8s  %8s  %11s    %4s  %6s  %6s  %4s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "Static", "Pending", "Hit");
        sal_fprintf(fp, "------------------------------------------------------------------------------\n");
    }
    else
    {
        SYS_FDB_DBG_DUMP("%-10s: DsFibHost0MacHashKey \n", "Key Table");
        SYS_FDB_DBG_DUMP("%-10s: DsMac \n", "Ad Table");
        SYS_FDB_DBG_DUMP("\n");

        SYS_FDB_DBG_DUMP("%8s  %8s  %8s  %11s    %4s  %6s  %6s  %4s\n", "KEY-INDEX", "AD-INDEX", "MAC", "FID", "GPORT", "Static", "Pending", "Hit");
        SYS_FDB_DBG_DUMP("------------------------------------------------------------------------------\n");
    }

    do
    {
        query_rst.start_index = query_rst.next_query_index;

        ret = sys_goldengate_l2_get_entry(lchip, pq, &query_rst, detail_info);
        if (ret < 0)
        {
            mem_free(query_rst.buffer);
            mem_free(detail_info);
            if (char_buffer)
            {
                mem_free(char_buffer);
            }
            return ret;
        }

        for (index = 0; index < pq->count; index++)
        {
            if (query_rst.buffer[index].fid != DONTCARE_FID)
            {
                sys_goldengate_aging_get_aging_status(lchip, SYS_AGING_DOMAIN_MAC_HASH, detail_info[index].key_index, &hit);
            }

            if (p_file)
            {
                sal_sprintf(char_buffer, "0x%-8x", detail_info[index].key_index);
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "0x%-8x", detail_info[index].ad_index);
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%.4x.%.4x.%.4x%4s ", sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                            " ");

                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%.4d  ", query_rst.buffer[index].fid);
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "0x%.4x  ", query_rst.buffer[index].gport);
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%3s     ", (detail_info[index].flag.flag_ad.is_static) ? "Yes" : "No");
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%5s     ", (detail_info[index].pending) ? "Y" : "N");
                sal_sprintf((char_buffer + sal_strlen(char_buffer)), "%3s     \n", (hit) ? "Y" : "N");

                sal_fprintf(fp, "%s", char_buffer);
            }
            else
            {
                SYS_FDB_DBG_DUMP("0x%-8x", detail_info[index].key_index);
                SYS_FDB_DBG_DUMP("0x%-8x", detail_info[index].ad_index);
                SYS_FDB_DBG_DUMP("%.4x.%.4x.%.4x%4s ", sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[0]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[2]),
                            sal_ntohs(*(unsigned short*)&query_rst.buffer[index].mac[4]),
                            " ");

                SYS_FDB_DBG_DUMP("%.4d  ", query_rst.buffer[index].fid);

                if (SYS_RSV_PORT_OAM_CPU_ID == query_rst.buffer[index].gport && !query_rst.buffer[index].is_logic_port)
                {
                    SYS_FDB_DBG_DUMP(" %s   ", "CPU");
                }
                else
                {
                    SYS_FDB_DBG_DUMP("0x%.4x  ", query_rst.buffer[index].gport);
                }
                SYS_FDB_DBG_DUMP("%3s     ", (detail_info[index].flag.flag_ad.is_static) ? "Yes" : "No");
                SYS_FDB_DBG_DUMP("%5s     ", (detail_info[index].pending) ? "Y" : "N");
                SYS_FDB_DBG_DUMP("%3s     \n", (hit) ? "Y" : "N");
            }

            if (detail_info[index].pending)
            {
                pending ++;
            }

            total_count++;
            sal_task_sleep(100);

            sal_memset(&detail_info[index], 0, sizeof(sys_l2_detail_t));
        }
    }
    while (query_rst.is_end == 0);

    if (p_file)
    {
        sal_fprintf(fp,"------------------------------------------------------------------------------\n");
        sal_fprintf(fp,"Total Entry Num: %d\n", total_count);
        sal_fprintf(fp,"Pending Entry Num: %d\n", pending);
        SYS_FDB_DBG_DUMP("FDB entry have been writen\n");
    }
    else
    {
        SYS_FDB_DBG_DUMP("------------------------------------------------------------------------------\n");
        SYS_FDB_DBG_DUMP("Total Entry Num: %d\n", total_count);
        SYS_FDB_DBG_DUMP("Pending Entry Num: %d\n", pending);
    }

    mem_free(query_rst.buffer);
    query_rst.buffer = NULL;
    mem_free(detail_info);
    if (char_buffer)
    {
        mem_free(char_buffer);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_mapping_ad(uint8 lchip, uint32 ad_index, sys_l2_info_t *p_l2_info)
{
    DsMac_m ds_mac;

    CTC_PTR_VALID_CHECK(p_l2_info);

    CTC_ERROR_RETURN(_sys_goldengate_get_dsmac_by_index(lchip, ad_index, &ds_mac));
    CTC_ERROR_RETURN(_sys_goldengate_l2_decode_dsmac(lchip, p_l2_info, &ds_mac, ad_index));

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_mapping_blackhole(uint8 lchip, sys_wb_l2_tcam_t *p_wb_tcam, sys_l2_tcam_t *p_tcam, uint8 sync)
{
    sys_goldengate_opf_t opf;
    sys_l2_info_t psys;

    if (sync)
    {
        sal_memcpy(&p_wb_tcam->mac, &p_tcam->key.mac, sizeof(sys_l2_tcam_key_t));
        p_wb_tcam->key_index = p_tcam->key_index;
        p_wb_tcam->ad_index = p_tcam->ad_index;
        sal_memcpy(&p_wb_tcam->flag_node, &p_tcam->flag.flag_node, sizeof(sys_l2_flag_node_t));
    }
    else
    {
        sal_memcpy(&p_tcam->key.mac, &p_wb_tcam->mac, sizeof(sys_l2_tcam_key_t));
        p_tcam->key_index = p_wb_tcam->key_index;
        p_tcam->ad_index = p_wb_tcam->ad_index;
        sal_memcpy(&p_tcam->flag.flag_node, &p_wb_tcam->flag_node, sizeof(sys_l2_flag_node_t));

        sal_memset(&psys, 0, sizeof(sys_l2_info_t));
        CTC_ERROR_RETURN(_sys_goldengate_l2_wb_mapping_ad(lchip, p_wb_tcam->ad_index, &psys));
        sal_memcpy(&(p_tcam->flag.flag_ad), &(psys.flag.flag_ad), sizeof(sys_l2_flag_ad_t));

        sal_memset(&opf, 0, sizeof(opf));
        opf.pool_type  = OPF_BLACK_HOLE_CAM;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_tcam->key_index));
        CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsMac_t, 0, 1, p_tcam->ad_index));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_mapping_fid(sys_wb_l2_fid_node_t *p_wb_fid, sys_l2_fid_node_t *p_fid, uint8 sync)
{
    if (sync)
    {
        p_wb_fid->fid = p_fid->fid;
        p_wb_fid->flag = p_fid->flag;
        p_wb_fid->nhid = p_fid->nhid;
        p_wb_fid->mc_gid = p_fid->mc_gid;
        p_wb_fid->create = p_fid->create;
    }
    else
    {
        p_fid->fid_list = ctc_list_new();
        if (NULL == p_fid->fid_list)
        {
            return CTC_E_NO_MEMORY;
        }

        p_fid->fid = p_wb_fid->fid;
        p_fid->flag = p_wb_fid->flag;
        p_fid->nhid = p_wb_fid->nhid;
        p_fid->mc_gid = p_wb_fid->mc_gid;
        p_fid->create = p_wb_fid->create;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_sync_blackhole_func(sys_l2_tcam_t *p_tcam, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_l2_tcam_t  *p_wb_tcam;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(traversal_data->data);
    uint8 lchip = (uint8)traversal_data->value1;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_tcam = (sys_wb_l2_tcam_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_tcam, 0, sizeof(sys_wb_l2_tcam_t));

    CTC_ERROR_RETURN(_sys_goldengate_l2_wb_mapping_blackhole(lchip, p_wb_tcam, p_tcam, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_sync_fid_func(void* array_data, uint32 index, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_l2_fid_node_t *p_fid = (sys_l2_fid_node_t *)array_data;
    sys_wb_l2_fid_node_t  *p_wb_fid;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_fid = (sys_wb_l2_fid_node_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_fid, 0, sizeof(sys_wb_l2_fid_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_l2_wb_mapping_fid(p_wb_fid, p_fid, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_sync_vport2nh_func(void* array_data, uint32 index, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_l2_vport_2_nhid_t *p_v2nhid = (sys_l2_vport_2_nhid_t *)array_data;
    sys_wb_l2_vport_2_nhid_t  *p_wb_v2nhid;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_v2nhid = (sys_wb_l2_vport_2_nhid_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_v2nhid, 0, sizeof(sys_wb_l2_vport_2_nhid_t));

    p_wb_v2nhid->vport = index;
    p_wb_v2nhid->nhid = p_v2nhid->nhid;
    p_wb_v2nhid->ad_idx = p_v2nhid->ad_idx;
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_sync_mcast2nh_func(void* array_data, uint32 index, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_l2_mcast_node_t *p_m2nhid = (sys_l2_mcast_node_t *)array_data;
    sys_wb_l2_mcast_2_nhid_t  *p_wb_m2nhid;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_m2nhid = (sys_wb_l2_mcast_2_nhid_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_m2nhid, 0, sizeof(sys_wb_l2_mcast_2_nhid_t));

    p_wb_m2nhid->group_id = index;
    p_wb_m2nhid->nhid = p_m2nhid->nhid;
    p_wb_m2nhid->key_index= p_m2nhid->key_index;
    p_wb_m2nhid->ref_cnt = p_m2nhid->ref_cnt;
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l2_wb_sync_fdb_func(sys_l2_node_t *p_l2_node, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_l2_node_t  *p_wb_l2_node;
    sys_traverse_t *traversal_data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)traversal_data->data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_l2_node = (sys_wb_l2_node_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_l2_node, 0, sizeof(sys_wb_l2_node_t));

    sal_memcpy(&p_wb_l2_node->key, &p_l2_node->key, sizeof(sys_l2_key_t));
    p_wb_l2_node->key_index = p_l2_node->key_index;
    p_wb_l2_node->nhid = p_l2_node->adptr->nhid;
    p_wb_l2_node->ad_index = p_l2_node->adptr->index;
    p_wb_l2_node->dest_gport = p_l2_node->adptr->dest_gport;
    sal_memcpy(&p_wb_l2_node->flag, &p_l2_node->flag, sizeof(sys_l2_key_t));

    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_l2_fdb_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_l2_master_t  *p_wb_l2_master;

    /*syncup fdb_matser*/
    wb_data.buffer = mem_malloc(MEM_FDB_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_master_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_MASTER);

    p_wb_l2_master = (sys_wb_l2_master_t *)wb_data.buffer;

    p_wb_l2_master->lchip = lchip;
    p_wb_l2_master->static_fdb_limit = pl2_gg_master[lchip]->static_fdb_limit;
    p_wb_l2_master->trie_sort_en = pl2_gg_master[lchip]->trie_sort_en;
    p_wb_l2_master->unknown_mcast_tocpu_nhid = pl2_gg_master[lchip]->unknown_mcast_tocpu_nhid;

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup fdb blackhole*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_tcam_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_TCAM);
    user_data.data = &wb_data;

    CTC_ERROR_GOTO(ctc_hash_traverse(pl2_gg_master[lchip]->tcam_hash, (hash_traversal_fn) _sys_goldengate_l2_wb_sync_blackhole_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup fdb fid*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_fid_node_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_FID_NODE);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_vector_traverse2(pl2_gg_master[lchip]->fid_vec, 0, _sys_goldengate_l2_wb_sync_fid_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync nhid of logic port*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_vport_2_nhid_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_VPORT_2_NHID);
    user_data.data = &wb_data;

    CTC_ERROR_GOTO(ctc_vector_traverse2(pl2_gg_master[lchip]->vport2nh_vec, 0, _sys_goldengate_l2_wb_sync_vport2nh_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync nhid of mcast group*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_mcast_2_nhid_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_MCAST_2_NHID);
    user_data.data = &wb_data;

    CTC_ERROR_GOTO(ctc_vector_traverse2(pl2_gg_master[lchip]->mcast2nh_vec, 0, _sys_goldengate_l2_wb_sync_mcast2nh_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup fdb hash*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_l2_node_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_NODE);
    user_data.data = &wb_data;

    CTC_ERROR_GOTO(ctc_hash_traverse(pl2_gg_master[lchip]->fdb_hash, (hash_traversal_fn) _sys_goldengate_l2_wb_sync_fdb_func, (void *)&user_data), ret, done);
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
sys_goldengate_l2_fdb_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_l2_master_t  *p_wb_l2_master;
    sys_l2_tcam_t  *p_tcam = NULL;
    sys_wb_l2_tcam_t  *p_wb_tcam;
    sys_l2_fid_node_t *p_fid;
    sys_wb_l2_fid_node_t *p_wb_fid;
    sys_l2_vport_2_nhid_t *p_v2nh;
    sys_wb_l2_vport_2_nhid_t *p_wb_v2nh;
    sys_l2_mcast_node_t *p_m2nh;
    sys_wb_l2_mcast_2_nhid_t *p_wb_m2nh;
    sys_l2_node_t *p_l2_node;
    sys_wb_l2_node_t *p_wb_l2_node;
    sys_l2_ad_spool_t   * pa_new = NULL;
    sys_l2_ad_spool_t   * pa_get = NULL;
    sys_l2_info_t psys;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_FDB_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_master_t, CTC_FEATURE_L2, SYS_WB_APPID_SCL_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore fdb_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query fdb master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_l2_master = (sys_wb_l2_master_t *)wb_query.buffer;

    pl2_gg_master[lchip]->static_fdb_limit = p_wb_l2_master->static_fdb_limit;
    pl2_gg_master[lchip]->trie_sort_en = p_wb_l2_master->trie_sort_en;
    pl2_gg_master[lchip]->unknown_mcast_tocpu_nhid = p_wb_l2_master->unknown_mcast_tocpu_nhid;

    /*restore fdb blackhole*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_tcam_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_TCAM);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_tcam = (sys_wb_l2_tcam_t *)wb_query.buffer + entry_cnt++;

        p_tcam = mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_tcam_t));
        if (NULL == p_tcam)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_tcam, 0, sizeof(sys_l2_tcam_t));

        ret = _sys_goldengate_l2_wb_mapping_blackhole(lchip, p_wb_tcam, p_tcam, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_hash_insert(pl2_gg_master[lchip]->tcam_hash, p_tcam);
        pl2_gg_master[lchip]->alloc_ad_cnt ++;
    CTC_WB_QUERY_ENTRY_END((&wb_query));

     /*restore fdb fid*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_fid_node_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_FID_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_fid = (sys_wb_l2_fid_node_t *)wb_query.buffer + entry_cnt++;

        p_fid = mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_fid_node_t));
        if (NULL == p_fid)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_fid, 0, sizeof(sys_l2_fid_node_t));

        ret = _sys_goldengate_l2_wb_mapping_fid(p_wb_fid, p_fid, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_vector_add(pl2_gg_master[lchip]->fid_vec, p_fid->fid, p_fid);
        if (p_fid->create)
        {
            pl2_gg_master[lchip]->def_count++;
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore nhid of logic port*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_vport_2_nhid_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_VPORT_2_NHID);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_v2nh = (sys_wb_l2_vport_2_nhid_t *)wb_query.buffer + entry_cnt++;

        p_v2nh = mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_vport_2_nhid_t));
        if (NULL == p_v2nh)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_v2nh, 0, sizeof(sys_l2_vport_2_nhid_t));

        p_v2nh->nhid = p_wb_v2nh->nhid;
        p_v2nh->ad_idx = p_wb_v2nh->ad_idx;

        if (pl2_gg_master[lchip]->vp_alloc_ad_en)
        {
            ret = sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsMac_t, 0, 1, p_v2nh->ad_idx);
            if (ret)
            {
                CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc dsmac profile id from position %u error! ret: %d.\n",  p_v2nh->ad_idx, ret);
                mem_free(p_v2nh);
                goto done;
            }
        }
        /*add to soft table*/
        ctc_vector_add(pl2_gg_master[lchip]->vport2nh_vec, p_wb_v2nh->vport, p_v2nh);
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore nhid of mcast group*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_mcast_2_nhid_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_MCAST_2_NHID);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_m2nh = (sys_wb_l2_mcast_2_nhid_t *)wb_query.buffer + entry_cnt++;

        p_m2nh = mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_mcast_node_t));
        if (NULL == p_m2nh)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_m2nh, 0, sizeof(sys_l2_mcast_node_t));

        p_m2nh->nhid = p_wb_m2nh->nhid;
        p_m2nh->key_index = p_wb_m2nh->key_index;
        p_m2nh->ref_cnt = p_wb_m2nh->ref_cnt;
        /*add to soft table*/
        ctc_vector_add(pl2_gg_master[lchip]->mcast2nh_vec, p_wb_m2nh->group_id, p_m2nh);
        pl2_gg_master[lchip]->alloc_ad_cnt += p_wb_m2nh->ref_cnt;
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore fdb hash*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_l2_node_t, CTC_FEATURE_L2, SYS_WB_APPID_L2_SUBID_NODE);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_l2_node = (sys_wb_l2_node_t *)wb_query.buffer + entry_cnt++;

        p_l2_node = mem_malloc(MEM_FDB_MODULE, sizeof(sys_l2_node_t));
        if (NULL == p_l2_node)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_l2_node, 0, sizeof(sys_l2_node_t));

        sal_memcpy(&p_l2_node->key, &p_wb_l2_node->key, sizeof(sys_l2_key_t));
        p_l2_node->key_index = p_wb_l2_node->key_index;
        sal_memcpy(&p_l2_node->flag, &p_wb_l2_node->flag, sizeof(sys_l2_flag_node_t));

        MALLOC_ZERO(MEM_FDB_MODULE, pa_new, sizeof(sys_l2_ad_spool_t));
        if (!pa_new)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }

        pa_new->nhid = p_wb_l2_node->nhid;
        pa_new->index = p_wb_l2_node->ad_index;
        pa_new->dest_gport = p_wb_l2_node->dest_gport;

        sal_memset(&psys, 0, sizeof(sys_l2_info_t));
        CTC_ERROR_RETURN(_sys_goldengate_l2_wb_mapping_ad(lchip, p_wb_l2_node->ad_index, &psys));
        sal_memcpy((&pa_new->flag), &(psys.flag.flag_ad), sizeof(sys_l2_flag_ad_t));

        pa_new->gport = psys.gport;

        if (!psys.flag.flag_node.rsv_ad_index)
        {
                ret = ctc_spool_add(pl2_gg_master[lchip]->ad_spool, pa_new, NULL, &pa_get);
                CTC_PTR_VALID_CHECK(pa_get);

                if (ret < 0)
                {
                    ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                    goto done;
                }
                else
                {
                    if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                    {
                        ret = sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsMac_t, 0, 1, p_wb_l2_node->ad_index);
                        if (ret)
                        {
                            ctc_spool_remove(pl2_gg_master[lchip]->ad_spool, pa_get, NULL);
                            mem_free(pa_new);
                            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc dsmac profile id from position %u error! ret: %d.\n",  pa_get->index, ret);
                            goto done;
                        }
                        pa_get->index = pa_new->index;
                        pl2_gg_master[lchip]->alloc_ad_cnt ++;
                    }
                    else
                    {

                    }

                }
        }
        else
        {
            if (pl2_gg_master[lchip]->cfg_has_sw)
            {
                pa_get = pa_new;  /* reserve index. so pa_get = pa_new. */
            }
        }

        p_l2_node->adptr = pa_get;

        if (pa_new != pa_get) /* means get an old. */
        {
            mem_free(pa_new);
            pa_new = NULL;
        }

        /*add to soft table*/
        CTC_ERROR_GOTO(_sys_goldengate_l2uc_add_sw(lchip, p_l2_node), ret, done);

        _sys_goldengate_l2_update_count(lchip, &p_l2_node->adptr->flag, &p_l2_node->flag, 1, L2_COUNT_GLOBAL, &p_l2_node->key_index);

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


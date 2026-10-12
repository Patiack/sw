/**
 @file sys_goldengate_opf.h

 @date 2009-10-22

 @version v2.0

 opf  -offset pool freelist
*/

#ifndef _SYS_GOLDENGATE_OPF_H_
#define _SYS_GOLDENGATE_OPF_H_
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 *
* Header Files
*
****************************************************************************/

#include "ctc_debug.h"
#include "sal.h"
/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define SYS_OPF_DBG_OUT(level, FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT(opf, opf, NH_SYS, level, FMT, ##__VA_ARGS__);  \
    } while (0)

enum sys_goldengate_opf_type
{
    /*module ower self-define,format OPF_TYPE_XXXX*/
    OPF_VLAN_PROFILE,
    OPF_USRID_VLAN_KEY,
    OPF_USRID_MAC_KEY,
    OPF_USRID_IPV4_KEY,
    OPF_USRID_IPV6_KEY,
    OPF_USRID_AD, /*for index of hash usrid*/
    OPF_SCL_VLAN_KEY,
    OPF_SCL_MAC_KEY,
    OPF_SCL_IPV4_KEY,
    OPF_SCL_IPV6_KEY,
    OPF_SCL_TUNNEL_IPV4_KEY,
    OPF_SCL_TUNNEL_IPV6_KEY,
    OPF_SCL_AD, /*for index of hash usrid*/
    OPF_SCL_VLAN_ACTION, /*for index of hash usrid*/
    OPF_SCL_ENTRY_ID,
    OPF_ACL_AD, /*for index of hash ACL */
    OPF_ACL_VLAN_ACTION,
    FDB_SRAM_HASH_COLLISION_KEY,
    FDB_AD_ALLOC, /* for index of dsmac */
    OPF_NH_DYN_SRAM,
    OPF_QOS_FLOW_POLICER,
    OPF_QOS_POLICER_PROFILE,
    FLOW_STATS_SRAM,
    OPF_STATS_STATSID,
    OPF_QUEUE_DROP_PROFILE,
    OPF_QUEUE_SHAPE_PROFILE,
    OPF_GROUP_SHAPE_PROFILE,
    OPF_QUEUE_GROUP,
    OPF_QUEUE_EXCEPTION_STACKING_PROFILE,
    OPF_SERVICE_QUEUE,
    OPF_OAM_CHAN,
    OPF_OAM_TCAM_KEY,
    OPF_OAM_MEP_LMEP,
    OPF_OAM_MEP_BFD,
    OPF_OAM_MA,
    OPF_OAM_MA_NAME,
    OPF_OAM_LM_PROFILE,
    OPF_OAM_LM,
    OPF_IPV4_UC_BLOCK,
    OPF_IPV6_UC_BLOCK,
    OPF_IPV4_MC_POINTER_BLOCK,
    OPF_IPV6_MC_POINTER_BLOCK,
    OPF_IPV4_L2MC_POINTER_BLOCK,
    OPF_IPV6_L2MC_POINTER_BLOCK,
    OPF_FOAM_MEP, /* start from 2 */
    OPF_FOAM_MA,  /* start from 0 */
    OPF_LPM_SRAM0,    /* used by LPM */
    OPF_LPM_SRAM1,    /* used by LPM */
    OPF_LPM_SRAM2,    /* used by LPM */
    OPF_LPM_SRAM3,    /* used by LPM */
    OPF_RPF,          /* used by RPF PROFILE*/
    OPF_FTM,          /* used by FTM */
    OPF_STORM_CTL,    /* used by storm ctl */
    OPF_TUNNEL_IPV4_SA,
    OPF_TUNNEL_IPV6_IP,
    OPF_TUNNEL_IPV4_IPUC,
    OPF_TUNNEL_IPV6_IPUC,
    OPF_FOAM_MA_NAME,  /* start from 0 */
    OPF_IPV4_BASED_L2_MC_BLOCK,
    OPF_IPV6_BASED_L2_MC_BLOCK,
    OPF_IPV4_NAT,
    OPF_MPLS,
    OPF_BLACK_HOLE_CAM,
    OPF_OVERLAY_TUNNEL_INNER_FID,
    OPF_SECURITY_LEARN_LIMIT_THRHOLD,
    OPF_ECMP_GROUP_ID,
    OPF_ECMP_MEMBER_ID,
    OPF_STACKING,
    OPF_ROUTER_MAC,
    MAX_OPF_TBL_NUM
};

struct sys_goldengate_opf_s
{
    uint8 pool_type; /*enum sys_goldengate_opf_type*/
    uint8 pool_index;
    uint8 multiple;   /* [alloc only] */
    uint8 reverse;    /* [alloc only] */
};
typedef  struct sys_goldengate_opf_s sys_goldengate_opf_t;

struct sys_goldengate_opf_entry_s
{
    struct sys_goldengate_opf_entry_s* prev;
    struct sys_goldengate_opf_entry_s* next;
    uint32 size;
    uint32 offset;
};
typedef struct sys_goldengate_opf_entry_s sys_goldengate_opf_entry_t;

struct sys_goldengate_opf_master_s
{
    sys_goldengate_opf_entry_t*** ppp_opf_forward;
    sys_goldengate_opf_entry_t*** ppp_opf_reverse;
    uint32* start_offset_a[MAX_OPF_TBL_NUM];
    uint32* max_size_a[MAX_OPF_TBL_NUM];
    uint32* forward_bound[MAX_OPF_TBL_NUM];             /*forward_bound is max_offset + 1*/
    uint32* reverse_bound[MAX_OPF_TBL_NUM];             /*reverse_bound is min_offset. no -1. prevent to be negative */
    uint8* is_reserve[MAX_OPF_TBL_NUM];
    uint8              pool_num[MAX_OPF_TBL_NUM];
    sal_mutex_t* p_mutex;
    uint32 count;
};
typedef struct sys_goldengate_opf_master_s sys_goldengate_opf_master_t;

/****************************************************************************
 *
* Function
*
*****************************************************************************/

extern int32
sys_goldengate_opf_init(uint8 lchip, uint8 pool_type, uint8 pool_num);
extern int32
sys_goldengate_opf_deinit(uint8 lchip, uint8 pool_type);
extern int32
sys_goldengate_opf_free(uint8 lchip, uint8 pool_type, uint8 pool_index);

extern int32
sys_goldengate_opf_init_offset(uint8 lchip, sys_goldengate_opf_t* opf, uint32 start_offset, uint32 max_size);
extern int32
sys_goldengate_opf_init_reverse_size(uint8 lchip, sys_goldengate_opf_t* opf, uint32 block_size);

extern int32
sys_goldengate_opf_alloc_offset(uint8 lchip, sys_goldengate_opf_t* opf, uint32 block_size, uint32* offset);
extern int32
sys_goldengate_opf_free_offset(uint8 lchip, sys_goldengate_opf_t* opf, uint32 block_size, uint32 offset);

extern int32
sys_goldengate_opf_get_bound(uint8 lchip, sys_goldengate_opf_t* opf, uint32* forward_bound, uint32* reverse_bound);

extern int32
sys_goldengate_opf_print_sample_info(uint8 lchip, sys_goldengate_opf_t* opf);

extern int32
sys_goldengate_opf_alloc_offset_from_position(uint8 lchip, sys_goldengate_opf_t* opf, uint32 block_size, uint32 begin);
extern int32
sys_goldengate_opf_alloc_offset_last(uint8 lchip, sys_goldengate_opf_t* opf, uint32 block_size, uint32* offset);

#ifdef __cplusplus
}
#endif

#endif /*_SYS_GOLDENGATE_OPF_H_*/


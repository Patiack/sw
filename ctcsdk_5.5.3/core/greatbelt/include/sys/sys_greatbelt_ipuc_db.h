/**
 @file sys_greatbelt_ipuc_db.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GREATBELT_IPUC_DB_H
#define _SYS_GREATBELT_IPUC_DB_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"

#include "ctc_port.h"
#include "ctc_const.h"
#include "ctc_hash.h"
#include "ctc_spool.h"
#include "sys_greatbelt_sort.h" 

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/* hash table size */
#define IPUC_IPV4_HASH_MASK             0xFFF
#define IPUC_IPV6_HASH_MASK             0xFFF
#define IPUC_AD_HASH_MASK               0xFFF
#define IPUC_RPF_PROFILE_HASH_MASK      0xFFF
#define IPUC_SMALL_TCAM_SIZE            256
#define IPUC_SMALL_TCAM_BLOCK_SIZE      16

#define IPUC_SHOW_HEADER(_IP_VER_)                                                                              \
{                                                                                                               \
    SYS_IPUC_DBG_DUMP("Flags:  R-RPF check    T-TTL check    I-ICMP redirect check      C-Send to CPU\n\r");    \
    SYS_IPUC_DBG_DUMP("        N-Neighbor     X-Connect      P-Protocol entry           S-Self address\n\r");   \
    SYS_IPUC_DBG_DUMP("        U-TCP port     E-ICMP error msg check    O-None flag\n\r");                      \
    SYS_IPUC_DBG_DUMP("\n\r");                                                                                  \
    if (_IP_VER_ == CTC_IP_VER_4)                                                                               \
    {                                                                                                           \
        SYS_IPUC_DBG_DUMP("VRF   Route                           Offset   NHID   Flags   In-SRAM\n\r");         \
        SYS_IPUC_DBG_DUMP("---------------------------------------------------------------------\n\r"); \
    }                                                                                                           \
    else                                                                                                        \
    {                                                                                                           \
        SYS_IPUC_DBG_DUMP("VRF   Route                                         Offset   NHID   Flags   In-SRAM\n\r");\
        SYS_IPUC_DBG_DUMP("-----------------------------------------------------------------------------------\n\r");\
    }                                                                                                           \
}

typedef int32 (* p_sys_ipuc_hash_make_cb_t)(sys_ipuc_info_t*);

struct sys_ipuc_arrange_data_s
{
    uint32 start_offset;

    uint8 idx_mask;
    uint8 moved;
    uint8 stage;
    uint8 rsv0;

    sys_ipuc_info_t* p_ipuc_info;
};
typedef struct sys_ipuc_arrange_data_s sys_ipuc_arrange_data_t;

struct sys_ipuc_arrange_info_s
{
    sys_ipuc_arrange_data_t* p_data;
    struct sys_ipuc_arrange_info_s* p_next_info;
};
typedef struct sys_ipuc_arrange_info_s sys_ipuc_arrange_info_t;

struct sys_ipuc_db_ipad_key_s
{
    uint16 nh_id;
    uint16 l3if;
    uint32 route_flag;
};
typedef struct sys_ipuc_db_ipad_key_s sys_ipuc_db_ipad_key_t;

struct sys_ipuc_db_rpf_profile_ad_s
{
    uint16  idx;
    uint16  counter;
};
typedef struct sys_ipuc_db_rpf_profile_ad_s sys_ipuc_db_rpf_profile_ad_t;

struct sys_ipuc_db_rpf_profile_s
{
    sys_ipuc_db_ipad_key_t key;
    sys_ipuc_db_rpf_profile_ad_t ad;
};
typedef struct sys_ipuc_db_rpf_profile_s sys_ipuc_db_rpf_profile_t;

struct sys_ipuc_ad_spool_s
{
    uint16 nhid;
    uint16 l3if;
    uint32 route_flag;
    uint8  binding_nh;
    uint8 rsv[3];
    uint32 ad_index;
};
typedef struct sys_ipuc_ad_spool_s sys_ipuc_ad_spool_t;

struct sys_ipuc_traverse_s
{
    void* data;
    void* fn;
    uint8 lchip;
    uint8 rsv[3];
};
typedef struct sys_ipuc_traverse_s sys_ipuc_traverse_t;

struct sys_ipuc_db_master_s
{
    ctc_hash_t* ipuc_hash[MAX_CTC_IP_VER];
    ctc_hash_t* ipuc_nat_hash;
    ctc_spool_t* ipuc_ad_spool;
    ctc_hash_t* ipuc_rpf_profile_hash;
    sys_sort_block_t ipv4_blocks[CTC_IPV4_ADDR_LEN_IN_BIT + 1];
    sys_sort_block_t ipv6_blocks[CTC_IPV6_ADDR_LEN_IN_BIT + 1];
    sys_sort_key_info_t ipuc_sort_key_info[MAX_CTC_IP_VER];
    skinfo_2a p_ipuc_offset_array[MAX_CTC_IP_VER];
    sys_ipuc_info_t** p_tcam_ipuc_info[MAX_CTC_IP_VER];
    uint16 tcam_ip_count[MAX_CTC_IP_VER];
    uint16 tcam_max_count[MAX_CTC_IP_VER];
    uint8 tcam_en[MAX_CTC_IP_VER];
    uint8 tcam_share_mode;
    uint8 rsv0[1];

    sys_sort_block_t* share_blocks;
};
typedef struct sys_ipuc_db_master_s sys_ipuc_db_master_t;
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
_sys_greatbelt_ipuc_db_lookup(uint8 lchip, sys_ipuc_info_t** pp_ipuc_info);

extern int32
_sys_greatbelt_ipuc_nat_db_lookup(uint8 lchip, sys_ipuc_nat_sa_info_t** pp_ipuc_nat_info);

extern int32
_sys_greatbelt_ipuc_db_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_ipuc_nat_db_add(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_greatbelt_ipuc_db_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_ipuc_nat_db_remove(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_greatbelt_ipuc_db_build_add_offset(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_ipv4_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset);

extern int32
_sys_greatbelt_ipv6_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset);

extern int32
_sys_greatbelt_ipuc_db_init(uint8 lchip);

extern int32
_sys_greatbelt_ipuc_db_deinit(uint8 lchip);

extern int32
_sys_greatbelt_ipuc_db_get_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list);

extern int32
_sys_greatbelt_ipuc_db_anylize_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list, sys_ipuc_arrange_info_t** pp_arrange_info);

extern int32
_sys_greatbelt_ipuc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data);

extern int32
_sys_greatbelt_ipuc_alloc_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_ipuc_free_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern sys_ipuc_db_rpf_profile_t*
_sys_greatbelt_ipuc_db_rpf_profile_lookup(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile_info);

extern int32
_sys_greatbelt_ipuc_db_add_rpf_profile(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_update_rpf_profile_info, sys_ipuc_db_rpf_profile_t** pp_rpf_profile_result);

extern int32
_sys_greatbelt_ipuc_db_remove_rpf_profile(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile_info);

extern int32
_sys_greatbelt_ipuc_lpm_ad_profile_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, int32* ad_spool_ref_cnt);

extern int32
_sys_greatbelt_ipuc_lpm_ad_profile_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_ipuc_write_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt);

extern int32
_sys_greatbelt_ipuc_remove_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
sys_greatbelt_show_ipuc_info(sys_ipuc_info_t* p_ipuc_data, void* data);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_goldengate_ipuc_db.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_IPUC_DB_H
#define _SYS_GOLDENGATE_IPUC_DB_H
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
#include "sys_goldengate_sort.h"  /*temp add*/

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
#define SYS_IPUC_IPV4_TCAM_BLOCK_NUM    16
#define SYS_IPUC_TCAM_6TO4_STEP (TCAM_KEY_SIZE(DsLpmTcamIpv6160Key0_t)/TCAM_KEY_SIZE(DsLpmTcamIpv440Key_t))
extern int32 masklen_block_map[2][33];
extern int32 masklen_ipv6_block_map[129];

enum sys_ipuc_tcam_flag_e
{
    SYS_IPUC_TCAM_FLAG_IPUC_INFO         =0,           /* sys_ipuc_info_t */
    SYS_IPUC_TCAM_FLAG_l3_HASH           =1            /* sys_l3_hash_t */
};
typedef enum sys_ipuc_tcam_flag_e sys_ipuc_tcam_flag_t;

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
    uint32 nh_id;
    uint32 route_flag;
    uint16 l3if;
    uint8  rsv;
    uint8  rpf_port_num;
    uint32 rpf_port[SYS_GG_MAX_IPMC_RPF_IF];
};
typedef struct sys_ipuc_db_ipad_key_s sys_ipuc_db_ipad_key_t;

struct sys_ipuc_db_rpf_profile_s
{
    sys_ipuc_db_ipad_key_t key;
};
typedef struct sys_ipuc_db_rpf_profile_s sys_ipuc_db_rpf_profile_t;

struct sys_ipuc_ad_spool_s
{
    uint32 nhid;
    uint16 l3if;
    uint8  rsv[2];
    uint32 route_flag;
    uint16 gport;
    uint8  rpf_mode;
    uint8  binding_nh;
    uint32 ad_index;
};
typedef struct sys_ipuc_ad_spool_s sys_ipuc_ad_spool_t;

struct sys_ipuc_tcam_manager_s
{
    void* p_info;
    uint8 type;         /* sys_ipuc_tcam_flag_t */
};
typedef struct sys_ipuc_tcam_manager_s sys_ipuc_tcam_manager_t;

struct sys_ipuc_db_master_s
{
    ctc_hash_t* ipuc_hash[MAX_CTC_IP_VER];
    ctc_hash_t* ipuc_nat_hash;
    ctc_spool_t* ipuc_ad_spool;
    sys_sort_block_t ipv4_blocks[CTC_IPV4_ADDR_LEN_IN_BIT + 1];
    sys_sort_block_t ipv6_blocks[CTC_IPV6_ADDR_LEN_IN_BIT + 1];
    sys_sort_key_info_t ipuc_sort_key_info[MAX_CTC_IP_VER];
    skinfo_2a p_ipuc_offset_array[MAX_CTC_IP_VER];
    uint8* p_ipuc_offset_type[MAX_CTC_IP_VER];      /* sys_ipuc_tcam_flag_t */
    sys_ipuc_tcam_manager_t* p_tcam_ipuc_info[MAX_CTC_IP_VER];
    uint16 tcam_ip_count[MAX_CTC_IP_VER];
    uint8 tcam_en[MAX_CTC_IP_VER];
    uint8 tcam_share_mode;
    uint8 rsv0[1];

    sys_sort_block_t* share_blocks;
};
typedef struct sys_ipuc_db_master_s sys_ipuc_db_master_t;

struct sys_ipuc_traverse_s
{
    void* data;
    void* fn;
    uint8 lchip;
};
typedef struct sys_ipuc_traverse_s sys_ipuc_traverse_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
_sys_goldengate_ipuc_db_lookup(uint8 lchip, sys_ipuc_info_t** pp_ipuc_info);

extern int32
_sys_goldengate_ipuc_nat_db_lookup(uint8 lchip, sys_ipuc_nat_sa_info_t** pp_ipuc_nat_info);

extern int32
_sys_goldengate_ipuc_db_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_nat_db_add(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_goldengate_ipuc_db_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_nat_db_remove(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_goldengate_ipuc_db_build_add_offset(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipv4_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset);

extern int32
_sys_goldengate_ipv6_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset);

extern int32
_sys_goldengate_ipuc_shift_key_up(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_write_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_shift_key_down(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_remove_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_push_down_itself(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_push_down_all(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_push_up_itself(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_push_up_all(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_db_init(uint8 lchip);

extern int32
_sys_goldengate_ipuc_db_deinit(uint8 lchip);

extern int32
_sys_goldengate_ipuc_db_get_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list);

extern int32
_sys_goldengate_ipuc_db_anylize_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list, sys_ipuc_arrange_info_t** pp_arrange_info);

extern int32
_sys_goldengate_ipuc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data);

extern int32
_sys_goldengate_ipuc_alloc_tcam_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_free_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_ipuc_lpm_ad_profile_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, int32* ad_spool_ref_cnt);

extern int32
_sys_goldengate_ipuc_lpm_ad_profile_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
sys_goldengate_ipuc_show_nat_sa_db(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail);

#ifdef __cplusplus
}
#endif

#endif


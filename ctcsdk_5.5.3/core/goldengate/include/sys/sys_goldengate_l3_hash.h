#ifndef _SYS_GOLDENGATE_L3_HASH_H
#define _SYS_GOLDENGATE_L3_HASH_H
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_debug.h"
#include "ctc_vector.h"
#include "sys_goldengate_lpm.h"

#define SYS_L3_HASH_TCAM_TYPE_MASK  0x80
#define SYS_L3_HASH_INVALID_INDEX   0xFFFF



#define POINTER_STAGE_BITS_LEN    1
#define POINTER_OFFSET_BITS_LEN   18
#define POINTER_BITS_LEN          (POINTER_STAGE_BITS_LEN + POINTER_OFFSET_BITS_LEN)
#define NEXTHOP_BITS_LEN          16

#define INVALID_POINTER_OFFSET      0x3FFFF
#define INVALID_POINTER           ((1 << 19) - 1)
#define INVALID_NEXTHOP           ((1 << NEXTHOP_BITS_LEN) - 1)

#define INVALID_HASH_INDEX_MASK   0xFFFFFFFF

#define POINTER_OFFSET_MASK       ((1 << POINTER_OFFSET_BITS_LEN) - 1)
#define POINTER_STAGE_MASK        (((1 << POINTER_BITS_LEN) - 1) >> POINTER_OFFSET_BITS_LEN)

#define LPM_PIPELINE_KEY_TYPE_POINTER       0x02
#define LPM_PIPELINE_KEY_TYPE_NEXTHOP       0x01
#define LPM_PIPELINE_KEY_TYPE_EMPTY         0x0



enum sys_l3_hash_type_e
{
    SYS_L3_HASH_TYPE_16,    /* for ipv4 */
    SYS_L3_HASH_TYPE_32,    /* for ipv6 */
    SYS_L3_HASH_TYPE_40,    /* for ipv6 */
    SYS_L3_HASH_TYPE_48,    /* for ipv6 */
    SYS_L3_HASH_TYPE_NAT_IPV4_SA,
    SYS_L3_HASH_TYPE_NAT_IPV4_SA_PORT,
    SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT,
    SYS_L3_HASH_TYPE_NUM
};
typedef enum sys_l3_hash_type_e sys_l3_hash_type_t;

struct sys_l3_hash_master_s
{
    ctc_hash_t* l3_hash[MAX_CTC_IP_VER];  /* ip & vrfid hash table */
};
typedef struct sys_l3_hash_master_s sys_l3_hash_master_t;

struct sys_l3_hash_result_s
{
    uint8 free;         /* hash key not exist, and is free */
    uint8 valid;        /* hash key is exist */
    uint16 rsv;

    uint32 key_index;
};
typedef struct sys_l3_hash_result_s sys_l3_hash_result_t;

struct sys_l3_hash_s
{
    sys_lpm_tbl_t n_table;

    uint16  vrf_id;
    uint16  l4_dst_port;

    uint8   masklen;
    uint8   is_tcp_port;
    uint8   ip_ver;
    uint8   is_exist;
    uint32  hash_offset[LPM_TYPE_NUM];  /* use to store hash32 index */

    uint32  ip32[4];                        /* use to store ip */

    uint8   masklen_pushed;             /* pushed masklen */
    uint8   is_mask_valid;              /* 0 means have push down, but no match key and do nothing */
    uint8   is_pushed;                  /* if new, need push */
};
typedef struct sys_l3_hash_s sys_l3_hash_t;

struct sys_lpm_param_s
{
    sys_l3_hash_t* p_lpm_prefix;
    ctc_ipuc_param_t* p_ipuc_param;
    uint16 key_index;
    uint32 ad_index;
};
typedef struct sys_lpm_param_s sys_lpm_param_t;

extern int32
_sys_goldengate_l3_hash_add_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 type);

extern int32
_sys_goldengate_l3_hash_get_hash_tbl(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_t** pp_hash);

extern int32
_sys_goldengate_l3_hash_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_l3_hash_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_lpm_traverse_prefix(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void *data);

extern int32
_sys_goldengate_l3_hash_update_tcam_ad(uint8 lchip, uint8 ip_ver, uint32 key_offset, uint32 ad_offset);

extern int32
_sys_goldengate_l3_hash_alloc_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_l3_hash_free_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
sys_goldengate_l3_hash_init(uint8 lchip, ctc_ip_ver_t ip_version);

extern int32
sys_goldengate_l3_hash_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


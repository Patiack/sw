#ifndef _SYS_GREATBELT_L3_HASH_H
#define _SYS_GREATBELT_L3_HASH_H
#ifdef __cplusplus
extern "C" {
#endif

#include "ctc_debug.h"
#include "ctc_vector.h"
#include "sys_greatbelt_lpm.h"

#define SYS_L3_HASH_TCAM_TYPE_MASK  0x80
#define SYS_L3_HASH_INVALID_INDEX   0xFFFF

enum sys_l3_hash_type_e
{
    SYS_L3_HASH_TYPE_8,
    SYS_L3_HASH_TYPE_16,
    SYS_L3_HASH_TYPE_HIGH32,
    SYS_L3_HASH_TYPE_MID32,
    SYS_L3_HASH_TYPE_LOW32,
    SYS_L3_HASH_TYPE_NAT_IPV4_SA,
    SYS_L3_HASH_TYPE_NAT_IPV4_SA_PORT,
    SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT,
    SYS_L3_HASH_TYPE_NUM
};
typedef enum sys_l3_hash_type_e sys_l3_hash_type_t;

struct sys_l3_hash_master_s
{
    uint8* hash_status;                     /* hash bucket */
    ctc_hash_t* duplicate_hash;             /* used for mid and low which are duplicate key*/
    ctc_hash_t* l3_hash[MAX_CTC_IP_VER];  /* ip & vrfid hash table */
    ctc_hash_t* pointer_hash;               /* ipv6 pointer hash */
    ctc_hash_t* mid_hash;                   /* ipv6 mid hash */

    uint32 l3_hash_num;
    uint8 hash_num_mode;
    uint8 hash_bucket_type;
    uint8 rsv[2];
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

struct sys_l3_hash_counter_s
{
    uint16 key_offset;
    uint16 counter:15,
           in_tcam:1;
    uint32 ip;
};
typedef struct sys_l3_hash_counter_s sys_l3_hash_counter_t;

struct sys_l3_hash_s
{
    sys_lpm_tbl_t n_table;

    uint16  vrf_id;
    uint16  l4_dst_port;

    uint8   lchip;
    uint8   masklen;
    uint8   is_tcp_port;
    uint8   in_tcam[LPM_TYPE_NUM];      /* use to identify in tcam */

    uint16  hash_offset[LPM_TYPE_NUM];  /* use to store hash32 index */

    uint32 ip32;                        /* use to store ip */
};
typedef struct sys_l3_hash_s sys_l3_hash_t;

struct sys_l3_hash_pointer_mid_info_s
{
    uint16 pointer;
    uint16 mid;
    uint32 ip128_97_64_33;
    uint32 ip96_65;
};
typedef struct sys_l3_hash_pointer_mid_info_s sys_l3_hash_pointer_mid_info_t;

struct sys_l3_hash_pointer_info_s
{
    uint16 pointer;
    uint16 rsv0;
    uint32 ip128_97;
    uint32 ip96_65;
};
typedef struct sys_l3_hash_pointer_info_s sys_l3_hash_pointer_info_t;

struct sys_l3_hash_mid_info_s
{
    uint16 rsv0;
    uint16 mid;
    uint32 ip64_33;
};
typedef struct sys_l3_hash_mid_info_s sys_l3_hash_mid_info_t;

struct sys_l3_hash_pointer_mid_hash_s
{
    uint32 count;
    sys_l3_hash_pointer_mid_info_t* data;
};
typedef struct sys_l3_hash_pointer_mid_hash_s sys_l3_hash_pointer_mid_hash_t;

extern int32
_sys_greatbelt_l3_hash_add_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 type);

extern int32
_sys_greatbelt_l3_hash_get_hash_tbl(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_t** pp_hash);

extern int32
_sys_greatbelt_l3_hash_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_l3_hash_add_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_greatbelt_l3_hash_remove_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_greatbelt_l3_hash_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_l3_hash_alloc_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_l3_hash_free_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_greatbelt_l3_hash_alloc_nat_key_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
_sys_greatbelt_l3_hash_free_nat_key_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info);

extern int32
sys_greatbelt_l3_hash_init(uint8 lchip);

extern int32
sys_greatbelt_l3_hash_deinit(uint8 lchip);

extern int32
sys_greatbelt_l3_hash_state_show(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


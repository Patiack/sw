/**
 @file sys_greatbelt_ipmc_db.h

 @date 2010-01-14

 @version v2.0

*/
#ifndef _SYS_GREATBELT_IPMC_DB_H
#define _SYS_GREATBELT_IPMC_DB_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sys_greatbelt_sort.h"

#define MIX(a, b, c) \
    do \
    { \
        a -= b; a -= c; a ^= (c >> 13); \
        b -= c; b -= a; b ^= (a << 8); \
        c -= a; c -= b; c ^= (b >> 13); \
        a -= b; a -= c; a ^= (c >> 12);  \
        b -= c; b -= a; b ^= (a << 16); \
        c -= a; c -= b; c ^= (b >> 5); \
        a -= b; a -= c; a ^= (c >> 3);  \
        b -= c; b -= a; b ^= (a << 10); \
        c -= a; c -= b; c ^= (b >> 15); \
    } while (0)


enum sys_ipmc_sort_block_e
{
    SYS_IPMC_SORT_V4_S_G,
    SYS_IPMC_SORT_V4_G,
    SYS_IPMC_SORT_V4_DFT,
    SYS_IPMC_SORT_V6_S_G,
    SYS_IPMC_SORT_V6_G,
    SYS_IPMC_SORT_V6_DFT,
    SYS_IPMC_SORT_MAX
};
typedef enum sys_ipmc_sort_block_e sys_ipmc_sort_block_t;

struct sys_ipmc_db_master_s
{
    ctc_hash_t* ipmc_hash[MAX_CTC_IP_VER];
    sal_mutex_t* sys_ipmc_db_mutex;
    sys_sort_block_t blocks[SYS_IPMC_SORT_MAX];
    sys_sort_key_info_t ipmc_sort_key_info;
    skinfo_2a p_ipmc_offset_array;
};
typedef struct sys_ipmc_db_master_s sys_ipmc_db_master_t;

struct sys_ipmc_traverse_s
{
    void* data;
    void* fn;
    uint8 lchip;
};
typedef struct sys_ipmc_traverse_s sys_ipmc_traverse_t;

extern int32
_sys_greatbelt_ipmc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data);

extern int32
sys_greatbelt_ipmc_db_lookup(uint8 lchip, sys_ipmc_group_node_t** pp_node);

extern int32
sys_greatbelt_ipmc_db_add(uint8 lchip, sys_ipmc_group_node_t* p_node);

extern int32
sys_greatbelt_ipmc_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node);

extern int32
sys_greatbelt_ipmc_show_status(uint8 lchip);

extern int32
sys_greatbelt_ipmc_db_init(uint8 lchip);

extern int32
sys_greatbelt_ipmc_db_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


/**
 @file sys_goldengate_ipmc_db.h

 @date 2010-01-14

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_IPMC_DB_H
#define _SYS_GOLDENGATE_IPMC_DB_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sys_goldengate_sort.h"

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

struct sys_ipmc_db_master_s
{
    ctc_hash_t* ipmc_hash[MAX_CTC_IP_VER];
    sal_mutex_t* sys_ipmc_db_mutex;
};
typedef struct sys_ipmc_db_master_s sys_ipmc_db_master_t;

struct sys_ipmc_traverse_s
{
    void* data;
    void* fn;
    uint8   lchip;
};
typedef struct sys_ipmc_traverse_s sys_ipmc_traverse_t;

#define UNSET_FLAG(V, F)      (V) = (V)&~(F)

extern uint32
sys_golengate_ipmc_db_get_cur_total(uint8 lchip);

extern int32
sys_goldengate_ipmc_db_alloc_pointer_from_position(uint8 lchip, sys_ipmc_group_db_node_t* p_ipmc_group_db_node, uint16 pointer);

extern int32
_sys_goldengate_ipmc_db_remove(uint8 lchip, sys_ipmc_group_db_node_t * p_node);

extern int32
sys_goldengate_ipmc_sg_db_remove(uint8 lchip, sys_ipmc_group_node_t * p_node);

extern int32
_sys_goldengate_ipmc_set_group_node(uint8 lchip, ctc_ipmc_group_info_t * p_group, sys_ipmc_group_node_t * p_group_node);

extern int32
_sys_goldengate_ipmc_set_group_db_node(uint8 lchip, sys_ipmc_group_node_t * p_group_node, sys_ipmc_group_db_node_t * p_group_db_node);

extern int32
_sys_goldengate_ipmc_add_g_db_node(uint8 lchip, sys_ipmc_group_node_t * p_group_node, sys_ipmc_group_db_node_t ** p_group_db_node);

extern int32
_sys_goldengate_ipmc_add_sg_db_node(uint8 lchip, sys_ipmc_group_node_t * p_group_node, sys_ipmc_group_db_node_t ** p_group_db_node);

extern int32
_sys_goldengate_ipmc_asic_lookup(uint8 lchip, sys_ipmc_group_node_t * p_group_node);

extern int32
_sys_goldengate_ipmc_db_alloc_pointer(uint8 lchip, sys_ipmc_group_db_node_t* p_group_node, uint16* pointer);

extern int32
_sys_goldengate_ipmc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data);

extern int32
sys_goldengate_ipmc_db_lookup(uint8 lchip, sys_ipmc_group_db_node_t** pp_node);

extern int32
sys_goldengate_ipmc_db_add(uint8 lchip, sys_ipmc_group_db_node_t* p_node);

extern int32
sys_goldengate_ipmc_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node);

extern int32
_sys_goldengate_ipmc_db_set_group_node(uint8 lchip, sys_ipmc_group_db_node_t* p_group_db_node, sys_ipmc_group_node_t* p_group_node);

extern int32
sys_goldengate_ipmc_show_status(uint8 lchip);

extern int32
sys_goldengate_ipmc_db_init(uint8 lchip);

extern int32
sys_goldengate_ipmc_db_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


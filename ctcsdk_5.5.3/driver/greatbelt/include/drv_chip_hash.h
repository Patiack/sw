/****************************************************************************
 * drv_chip_hash.h  Provides driver chip hash handle function declaration.
 *
 * Copyright (C) 2011 Centec Networks Inc.  All rights reserved.
 *
 * Vision:      V4.1.1
 * Author       XuZx.
 * Date:        2011-7-7.
 * Reason:      Sync for GreatBelt 4.2.1
 *
 * Modify History:
 *
 ****************************************************************************/
#ifndef _DRV_CHIP_HASH_H_
#define _DRV_CHIP_HASH_H_

#include "sal.h"
#include "greatbelt/include/drv_lib.h"

enum fib_mac_accelerate_type_e
{
    FIB_MAC_ACCELERATE_TYPE_ADD_BY_IDX,
    FIB_MAC_ACCELERATE_TYPE_ADD_BY_KEY,
    FIB_MAC_ACCELERATE_TYPE_DEL_BY_IDX,
    FIB_MAC_ACCELERATE_TYPE_DEL_BY_KEY,
    FIB_MAC_ACCELERATE_TYPE_DEL_BY_VSI,
    FIB_MAC_ACCELERATE_TYPE_RESV,
    FIB_MAC_ACCELERATE_TYPE_LKUP,
    FIB_MAC_ACCELERATE_TYPE_NUM
};
typedef enum fib_mac_accelerate_type_e fib_mac_accelerate_type_t;

int32
drv_greatbelt_chip_hash_key_add_by_key(hash_io_by_key_para_t* p_para);

int32
drv_greatbelt_chip_hash_key_del_by_key(hash_io_by_key_para_t* p_para);

int32
drv_greatbelt_chip_hash_key_add_by_index(hash_io_by_idx_para_t* p_para);

int32
drv_greatbelt_chip_hash_key_del_by_index(hash_io_by_idx_para_t* p_para);

int32
drv_greatbelt_chip_hash_index_lkup_by_key(hash_io_by_key_para_t* p_para, hash_io_rslt_t* p_hash_io_rslt);

int32
drv_greatbelt_chip_hash_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result);

#endif


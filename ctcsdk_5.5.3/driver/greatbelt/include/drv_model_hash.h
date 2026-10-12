/****************************************************************************
 * drv_model_hash.h  Provides driver model hash handle function declaration.
 *
 * Copyright (C) 2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:    V4.1.1
 * Author       XuZx.
 * Date:        2011-7-7.
 * Reason:      Revise GreatBelt 4.2.1
 ****************************************************************************/
#ifndef _DRV_MODEL_HASH_H_
#define _DRV_MODEL_HASH_H_

#include "sal.h"
#include "greatbelt/include/drv_lib.h"

#define MODEL_HASH_DEBUG_INFO(fmt, args...)  \
    do \
    { \
        if (gb_hash_debug_on) \
        { \
            sal_printf(fmt, ## args); \
        } \
    } \
    while (0)

#define DRV_POLY_ANSI_CRC16   0x00018005  /* polynomial: (0 2 15 16) */
#define DRV_POLY_CCITT_CRC16  0x00011021  /* polynomial: (0 5 12 16) */
#define DRV_POLY_T10DIF_CRC16 0x00018BB7  /* polynomial: (0 1 2 4 5 7 8 9 11 15 16) */
#define DRV_POLY_DNP_CRC16    0x00013D65  /* polynomial: (0 2 5 6 8 10 11 12 13 16) */
#define DRV_POLY_CRC15        0x0000C599  /* polynomial: (0 3 4 7 8 10 14 15) */
#define DRV_POLY_CRC14        0x00004805  /* polynomial: (0 2 11 14) */
#define DRV_POLY_CRC12        0x0000180F  /* polynomial: (0 1 2 3 11 12) */
#define DRV_POLY_CCITT_CRC8   0x00000107  /* polynomial: (0 1 2 8) */
#define DRV_POLY_CRC8         0x000001D5  /* polynomial: (0 2 4 6 7 8) */
#define DRV_POLY_INVALID      0xFFFFFFFF

struct key_lookup_result_s
{
   uint32 bucket_index;
};
typedef struct key_lookup_result_s key_lookup_result_t;

struct key_lookup_info_s
{
    uint8* key_data;
    uint32 key_bits;
    uint32 polynomial;
    uint32 poly_len;
    uint32 type;
    uint32 crc_bits;
};
typedef struct key_lookup_info_s key_lookup_info_t;

extern int32
drv_greatbelt_model_hash_lookup(lookup_info_t*, lookup_result_t*);

extern int32
drv_greatbelt_model_hash_key_add_by_key(hash_io_by_key_para_t*);

extern int32
drv_greatbelt_model_hash_key_add_by_index(hash_io_by_idx_para_t*);

extern int32
drv_greatbelt_model_hash_key_del_by_key(hash_io_by_key_para_t*);

extern int32
drv_greatbelt_model_hash_key_del_by_index(hash_io_by_idx_para_t*);

extern int32
drv_greatbelt_model_hash_index_lkup_by_key(hash_io_by_key_para_t* p_para, hash_io_rslt_t* p_hash_io_rslt);

extern int32
drv_greatbelt_model_hash_calculate_index(key_info_t* p_key_info, key_result_t* p_key_result);

#endif


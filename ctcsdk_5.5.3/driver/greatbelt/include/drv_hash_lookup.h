/****************************************************************************
 * drv_hash_lookup.h  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2010 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V2.0.
 * Author:       ZhouW
 * Date:         2011-05-12.
 * Reason:       Create for GreatBelt v2.0.
 *
 * Revision:     V4.5.1
 * Revisor:      XuZx
 * Date:         2011-7-27.
 * Reason:       Revise for GreatBelt v4.5.1
 *
 * Vision:       V4.9.0
 * Revisor:      XuZx
 * Date:         2011-08-03.
 * Reason:       Revise for GreatBelt v4.9.0
 *
 * Revision:     V5.0.0
 * Revisor:      XuZx
 * Date:         2011-11-11
 * Reason:       Revise for GreatBelt v5.0.0
 *
 * Revision:     v5.9.0
 * Revisor:      XuZx
 * Date:         2012-02-03.
 * Reason:       Revise for GreatBelt v5.9.0
 ****************************************************************************/

#ifndef _DRV_HASH_LOOKUP_H
#define _DRV_HASH_LOOKUP_H

#include "sal.h"

#define DRV_HASH_DEBUG_ON 0

extern uint32 gb_hash_debug_on;

/**********************************************************************************
              Define Typedef, define and Data Structure
***********************************************************************************/
#define LPM_HASH_KEY_NUM                   (32 * 1024)
#define DRV_HASH_80BIT_KEY_LENGTH           10
#define DRV_HASH_96BIT_KEY_LENGTH           12
#define DRV_HASH_160BIT_KEY_LENGTH          20
#define DRV_HASH_KEY_MAX_LENGTH             (DRV_HASH_96BIT_KEY_LENGTH * 2)
#define DRV_HASH_INVALID_AD_VALUE           0xFFFFFFFF
#define INVALID_BUCKET_ENTRY_INDEX          0xFFFFFFFF

#define KEY_MATCH(src, dst, mask, byte_num) drv_greatbelt_hash_lookup_key_match(src, dst, mask, byte_num)

#define MaxField_f 0xFFFFFFFF

struct ds_3word_hash_key_s
{
    uint32 field[DRV_WORDS_PER_ENTRY];
};
typedef struct ds_3word_hash_key_s ds_3word_hash_key_t;

struct ds_6word_hash_key_s
{
    uint32 field[DRV_WORDS_PER_ENTRY * 2];
};
typedef struct ds_6word_hash_key_s ds_6word_hash_key_t;

struct ds_12word_hash_key_s
{
    uint32 field[DRV_WORDS_PER_ENTRY * 4];
};
typedef struct ds_12word_hash_key_s ds_12word_hash_key_t;

struct drv_hash_entry_valid_s
{
    uint8 drv_lpm_hash_entry_valid[LPM_HASH_KEY_NUM / 8];
};
typedef struct drv_hash_entry_valid_s drv_hash_entry_valid_t;

enum hash_key_length_mode_e
{
    HASH_KEY_LENGTH_MODE_80BIT = 0,
    HASH_KEY_LENGTH_MODE_160BIT = 1,
    HASH_KEY_LENGTH_MODE_320BIT = 2,
    HASH_KEY_LENGTH_MODE_NUM
};
typedef enum hash_key_length_mode_e hash_key_length_mode_t;

enum hash_module_e
{
    HASH_MODULE_USERID,
    HASH_MODULE_FIB,
    HASH_MODULE_LPM,
    HASH_MODULE_QUEUE,
    HASH_MODULE_NUM
};
typedef enum hash_module_e hash_module_t;

enum key_info_type_e
{
    KEY_INFO_AD,
    KEY_INFO_INDEX_MASK,
    KEY_INFO_TYPE,
    KEY_INFO_VALID,
    KEY_INFO_HASH_TYPE,
    KEY_INFO_NUM
};
typedef enum key_info_type_e key_info_type_t;

struct key_info_s
{
    uint8* p_hash_key;
    uint16 key_length;
    hash_module_t hash_module;
    uint8 polynomial_index;
};
typedef struct key_info_s key_info_t;

struct lookup_info_s
{
    uint8 chip_id;
    void* p_ds_key;
    uint8 hash_type;
    hash_module_t hash_module;
    void* extra;
};
typedef struct lookup_info_s lookup_info_t;

struct key_result_s
{
    uint32 bucket_index;
};
typedef struct key_result_s key_result_t;

struct lookup_result_s
{
    uint32  key_index;
    uint8   valid;
    uint32  ad_index;
    uint8   free;
    uint8   conflict;
    void*   extra;
};
typedef struct lookup_result_s lookup_result_t;
typedef struct lookup_result_s cam_result_t;

struct hash_io_by_key_para_s
{
    uint8 chip_id;
    void* key;
    uint8 hash_type;
    hash_module_t hash_module;
};
typedef struct hash_io_by_key_para_s hash_io_by_key_para_t;

struct hash_io_by_idx_para_s
{
    uint8  chip_id;
    uint32 table_id;
    uint32 table_index;
    uint32* key;
};
typedef struct hash_io_by_idx_para_s hash_io_by_idx_para_t;

struct hash_io_rslt_s
{
    uint32  key_index;
    uint8   valid;
    uint32  ad_index;
    uint8   free;
    uint8   conflict;
};
typedef struct hash_io_rslt_s hash_io_rslt_t;

enum bucket_entry_e
{
    BUCKET_ENTRY_INDEX0,
    BUCKET_ENTRY_INDEX1,
    BUCKET_ENTRY_INDEX2,
    BUCKET_ENTRY_INDEX3,
    BUCKET_ENTRY_NUM
};
typedef enum bucket_entry_e bucket_entry_t;

/* Define cpu req hash key type */

/* Hash add/delete operation type */
enum hash_op_type_e
{
    HASH_OP_TP_ADD_BY_KEY,
    HASH_OP_TP_DEL_BY_KEY,
    HASH_OP_TP_ADD_BY_INDEX,
    HASH_OP_TP_DEL_BY_INDEX,
    HASH_OP_TP_LKUP_BY_KEY,
    HASH_OP_TP_MAX_VALUE
};
typedef enum hash_op_type_e hash_op_type_t;

enum step_num_e
{
    X3_STEP = 1,
    X6_STEP
};
typedef enum step_num_e step_num_t;

struct drv_hash_key_s
{
    union
    {
        uint8 hash_80bit[BUCKET_ENTRY_NUM][DRV_HASH_80BIT_KEY_LENGTH];
        uint8 hash_160bit[BUCKET_ENTRY_NUM / 2][DRV_HASH_160BIT_KEY_LENGTH];
    } key;
};
typedef struct drv_hash_key_s drv_hash_key_t;

struct ds_hash_key_s
{
    union
    {
        ds_12word_hash_key_t x12;
        ds_6word_hash_key_t x6[sizeof(ds_6word_hash_key_t) * 2];
        ds_3word_hash_key_t x3[sizeof(ds_3word_hash_key_t) * 4];
    } word;
};
typedef struct ds_hash_key_s ds_hash_key_t;

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @addtogroup hash
 @{
*/
extern tbls_id_t
drv_greatbelt_hash_lookup_get_key_table_id(hash_module_t, uint32);

extern int32
drv_greatbelt_hash_lookup_get_key_entry_valid(lookup_info_t*, uint32*);

extern int32
drv_greatbelt_hash_lookup_get_key_mask(uint8, hash_module_t, uint32, uint8*);

extern int32
drv_greatbelt_hash_lookup_get_key_ad_index(lookup_info_t*, uint32*);

extern uint32
drv_greatbelt_hash_lookup_key_match(uint8*, uint8*, uint8*, uint8);

extern int32
drv_greatbelt_hash_lookup_get_convert_key(lookup_info_t*, uint8*);

extern int32
drv_greatbelt_hash_lookup_check_key_mask(hash_module_t, uint8);

extern int32
drv_greatbelt_hash_lookup_get_key_index_mask(lookup_info_t*, uint32*);

extern int32
drv_greatbelt_hash_lookup_get_key_type(lookup_info_t*, uint32*);

extern uint32
drv_greatbelt_hash_lookup_check_hash_type(hash_module_t, uint32);

extern void
drv_greatbelt_hash_lookup_add_field(uint32, uint16, uint16*, uint8*);

/**@}*/ /*end of @addtogroup*/
#endif /*end of _DRV_HUMBER_HASH_H*/


/****************************************************************************
 * drv_hash_db.h
 *
 * Copyright: (c)2011 Centec Networks Inc.  All rights reserved.

 * Modify History:
 * Revision:     V2.0.
 * Author:       ZhouW
 * Date:         2011-05-12.
 * Reason:       Create for GreatBelt v2.0.
 *
 * Revision:     V4.2.1
 * Revisor:      XuZx
 * Date:         2011-07-07.
 * Reason:       Revise for GreatBelt v4.2.1
 ****************************************************************************/

#ifndef _DRV_HASH_DB_H
#define _DRV_HASH_DB_H

#include "sal.h"
#include "greatbelt/include/drv_lib.h"

/**********************************************************************************
              Define Typedef, define and Data Structure
***********************************************************************************/
#define HASH_MAX_COMPARE_FIELD_NUM 30
#define HASH_MAX_VALID_FLAG_FIELD_NUM 3
#define HASH_MAX_AD_FIELD_NUM 5
#define HASH_MAX_TYPE_FIELD_NUM 2
#define HASH_MAX_RECORD_NUM 2
#define HASH_MAX_INDEX_MASK_FIELD_NUM 2
#define HASH_KEY_MASK_ARRY_NUM 3

struct hash_key_property_s
{
    tbls_id_t table_id;
    fld_id_t  compare_field[HASH_MAX_COMPARE_FIELD_NUM];
    fld_id_t  entry_valid_field[HASH_MAX_VALID_FLAG_FIELD_NUM];
    fld_id_t  ad_field[HASH_MAX_AD_FIELD_NUM];
    uint32    key_cmp_mask[HASH_MAX_RECORD_NUM][HASH_KEY_MASK_ARRY_NUM];
    fld_id_t  index_mask_field[HASH_MAX_INDEX_MASK_FIELD_NUM];
    fld_id_t  type_field[HASH_MAX_TYPE_FIELD_NUM];
};
typedef struct hash_key_property_s hash_key_property_t;

extern hash_key_property_t* p_hash_key_property[];

#endif


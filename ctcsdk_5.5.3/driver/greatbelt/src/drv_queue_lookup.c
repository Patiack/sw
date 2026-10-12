/****************************************************************************
 * drv_queue_lookup.c  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 ****************************************************************************/
#include "greatbelt/include/drv_lib.h"
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)
/* temp code !! add by zhouw  for cosim store hash key */
#include "sim_common.h"
#endif

int32
drv_greatbelt_queue_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    lookup_info_t lookup_info;
    key_info_t key_info;
    key_result_t key_result;
    uint8 hash_lookup_key[DRV_HASH_96BIT_KEY_LENGTH] = {0};
    uint8 hash_lookup_mask[DRV_HASH_96BIT_KEY_LENGTH] = {0};
    uint8 index = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 ad_value[QUEUE_HASH_KEY_ENTRY_RECORD_NUM] = {0};
    uint32 entry_valid[QUEUE_HASH_KEY_ENTRY_RECORD_NUM] = {0};
    ds_queue_hash_key_t* p_ds_queue_lookup_key = NULL;
    ds_queue_hash_cfg_t ds_queue_search_key[QUEUE_HASH_KEY_ENTRY_RECORD_NUM];

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);
    DRV_PTR_VALID_CHECK(p_lookup_result);

    p_ds_queue_lookup_key = p_lookup_info->p_ds_key;

    sal_memset(ds_queue_search_key, 0, sizeof(ds_queue_search_key));
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&key_info, 0, sizeof(key_info_t));
    sal_memset(&key_result, 0, sizeof(key_result_t));

    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, hash_lookup_key));

    key_info.hash_module = HASH_MODULE_QUEUE;
    key_info.key_length = QUEUE_CRC_DATA_WIDTH;
    key_info.p_hash_key = hash_lookup_key;

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(lookup_info.hash_module, lookup_info.hash_type);
    if (MaxTblId_t == tbl_id)
    {
        return DRV_E_INVALID_TBL;
    }

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lookup_info.chip_id, key_result.bucket_index, cmd, ds_queue_search_key));

#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)
    if (cosim_db.store_table)
    {
        /* cosim using */
        uint32 data_entry[MAX_ENTRY_WORD] = {0};
        if ((DRV_IOC_READ == DRV_IOC_OP(cmd))
            && (!drv_greatbelt_table_is_tcam_key(DRV_IOC_MEMID(cmd))))
        {
            if (drv_gb_io_api.drv_sram_tbl_read)
            {
                DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                 DRV_IOC_MEMID(cmd), key_result.bucket_index, (uint32*)data_entry));
            }

            if (TABLE_ENTRY_SIZE(tbl_id) == 24)
            {
                DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                 DRV_IOC_MEMID(cmd), key_result.bucket_index + 1, (uint32*)(data_entry + 3)));
            }

            DRV_IF_ERROR_RETURN(cosim_db.store_table(lookup_info.chip_id, tbl_id, key_result.bucket_index, data_entry, FALSE));
        }
    }

#endif

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_mask(lookup_info.chip_id, lookup_info.hash_module,
                                                     lookup_info.hash_type, hash_lookup_mask));

    for (index = QUEUE_HASH_KEY_ENTRY_RECORD0; index < QUEUE_HASH_KEY_ENTRY_RECORD_NUM; index++)
    {
        lookup_info.p_ds_key = &ds_queue_search_key[index];
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_ad_index(&lookup_info, &ad_value[index]));
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_entry_valid(&lookup_info, &entry_valid[index]));
    }

    for (index = QUEUE_HASH_KEY_ENTRY_RECORD0; index < QUEUE_HASH_KEY_ENTRY_RECORD_NUM; index++)
    {
        if (KEY_MATCH((uint8*)p_ds_queue_lookup_key, (uint8*)&ds_queue_search_key[index],
                      hash_lookup_mask, DRV_HASH_96BIT_KEY_LENGTH))
        {
            p_lookup_result->key_index = key_result.bucket_index;
            p_lookup_result->ad_index = ad_value[index];
            p_lookup_result->valid = TRUE;
            p_lookup_result->free = FALSE;
            goto END_DRV_QUEUE_LOOKUP;
        }
    }

    for (index = QUEUE_HASH_KEY_ENTRY_RECORD0; index < QUEUE_HASH_KEY_ENTRY_RECORD_NUM; index++)
    {
        if (!entry_valid[index])
        {
            p_lookup_result->key_index = key_result.bucket_index;
            p_lookup_result->valid = FALSE;
            p_lookup_result->free = TRUE;
            goto END_DRV_QUEUE_LOOKUP;
        }
    }

END_DRV_QUEUE_LOOKUP:
    return DRV_E_NONE;
}


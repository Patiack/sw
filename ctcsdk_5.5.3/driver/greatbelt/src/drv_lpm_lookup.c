/****************************************************************************
 * drv_lpm_lookup.c  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 *
 * Revision:     V4.5.1
 * Revisor:      XuZx
 * Date:         2011-07-20
 * Reason:       Revise for GreatBelt v4.5.1
 *
 * Revision:     v5.7.0
 * Revisor:      XuZx
 * Date:         2012-01-17.
 * Reason:       Revise for GreatBelt v5.7.0
 ****************************************************************************/
#include "greatbelt/include/drv_lib.h"
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)
/* temp code !! add by zhouw  for cosim store hash key */
#include "sim_common.h"
#endif

int32
_drv_greatbelt_lpm_perform_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 hash_result_index[BUCKET_ENTRY_NUM] = {0};
    uint32 tbl_id = MaxTblId_t;
    uint32 search_key_entry_valid[BUCKET_ENTRY_NUM] = {0};
    uint32 nexthop[BUCKET_ENTRY_NUM] = {0};
    uint32 index_mask[BUCKET_ENTRY_NUM] = {0};
    uint32 type[BUCKET_ENTRY_NUM] = {0};
    uint32 search_key_type = 0;
    uint16 pointer_hit_index = 0;
    uint16 nexthop_hit_index = 0;
    uint8  step = 0;
    uint8  nexthop_hit = FALSE;
    uint8  pointer_hit = FALSE;
    lpm_info_t* p_lpm_info = NULL;
    ds_hash_key_t*  p_ds_hash_search_key = NULL;
    drv_hash_key_t drv_hash_mask;
    drv_hash_key_t drv_hash_lookup_key;
    drv_hash_key_t drv_hash_search_key;
    fib_engine_hash_ctl_t fib_engine_hash_ctl;
    lookup_info_t lookup_info;
    key_info_t key_info;
    key_result_t key_result;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_result);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);

    sal_memset(&key_info, 0, sizeof(key_info));
    sal_memset(&key_result, 0, sizeof(key_result));
    sal_memset(&drv_hash_mask,      0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_lookup_key, 0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_search_key, 0, sizeof(drv_hash_key_t));
    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));

    p_lpm_info = p_lookup_result->extra;

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(lookup_info.hash_module, lookup_info.hash_type);
    if (MaxTblId_t == tbl_id)
    {
        return DRV_E_INVALID_TBL;
    }

    step = (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) * X3_STEP;

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, drv_hash_lookup_key.key.hash_80bit[X6_STEP - step]));

    sal_memset(&fib_engine_hash_ctl, 0, sizeof(fib_engine_hash_ctl_t));
    cmd = DRV_IOR(FibEngineHashCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lookup_info.chip_id, 0, cmd, &fib_engine_hash_ctl));

    key_info.hash_module = HASH_MODULE_LPM;
    key_info.polynomial_index = fib_engine_hash_ctl.lpm_hash_bucket_type;
    key_info.key_length = LPM_CRC_DATA_WIDTH;
    key_info.p_hash_key = drv_hash_lookup_key.key.hash_160bit[0];
    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));

    switch (fib_engine_hash_ctl.lpm_hash_num_mode)
    {
    case 3:
        /* 2K buckets * 2 entries */
        CLEAR_BIT(key_result.bucket_index, 11);

    case 2:
        /* 4K buckets * 2 entries */
        CLEAR_BIT(key_result.bucket_index, 12);

    case 1:
        /* 8K buckets * 2 entries */
        CLEAR_BIT(key_result.bucket_index, 13);

    default:
        /* 16K buckets * 2 entries */
        break;
    }

    p_ds_hash_search_key = (ds_hash_key_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ds_hash_key_t));
    if (NULL == p_ds_hash_search_key)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_ds_hash_search_key, 0, sizeof(ds_hash_key_t));
    cmd = DRV_IOR(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);

    for (index = BUCKET_ENTRY_INDEX0; index <= BUCKET_ENTRY_INDEX1; index++)
    {
        hash_result_index[index] = (key_result.bucket_index << 1) + index;
        DRV_IF_ERROR_GOTO(DRV_IOCTL(lookup_info.chip_id, hash_result_index[index],
                                      cmd, &p_ds_hash_search_key->word.x3[index]), ret, out);
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
                    DRV_IF_ERROR_GOTO(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                     DRV_IOC_MEMID(cmd), hash_result_index[index], (uint32*)data_entry), ret, out);
                    if (TABLE_ENTRY_SIZE(tbl_id) == 24)
                    {
                        DRV_IF_ERROR_GOTO(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                         DRV_IOC_MEMID(cmd), hash_result_index[index] + 1, (uint32*)(data_entry + 3)), ret, out);
                    }
                }

                DRV_IF_ERROR_GOTO(cosim_db.store_table(lookup_info.chip_id, tbl_id, hash_result_index[index], data_entry, FALSE), ret, out);
            }
        }

#endif

    }

    DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_mask(lookup_info.chip_id,
                                                     lookup_info.hash_module,
                                                     lookup_info.hash_type,
                                                     drv_hash_mask.key.hash_160bit[0]), ret, out);

    for (index = BUCKET_ENTRY_INDEX0; index <= BUCKET_ENTRY_INDEX1; index += step)
    {
        lookup_info.p_ds_key = &p_ds_hash_search_key->word.x3[index];

        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, drv_hash_search_key.key.hash_80bit[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_entry_valid(&lookup_info, &search_key_entry_valid[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_ad_index(&lookup_info, &nexthop[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_index_mask(&lookup_info, &index_mask[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_type(&lookup_info, &type[index]), ret, out);

        if ((KEY_MATCH(drv_hash_lookup_key.key.hash_80bit[X6_STEP - step], drv_hash_search_key.key.hash_80bit[index],
                       drv_hash_mask.key.hash_160bit[0], DRV_HASH_80BIT_KEY_LENGTH * step))
            && (LPM_HASH_KEY_TYPE_NEXTHOP == type[index]))
        {
            nexthop_hit = TRUE;
            nexthop_hit_index = index;
        }
        else if ((KEY_MATCH(drv_hash_lookup_key.key.hash_80bit[X6_STEP - step], drv_hash_search_key.key.hash_80bit[index],
                            drv_hash_mask.key.hash_160bit[0], DRV_HASH_80BIT_KEY_LENGTH * step))
                 && (LPM_HASH_KEY_TYPE_POINTER == type[index]))
        {
            pointer_hit = TRUE;
            pointer_hit_index = index;
        }

        if (nexthop_hit)
        {
            ;
        }

        if (pointer_hit)
        {
            ;
        }

        if (!search_key_entry_valid[index] && (!p_lpm_info->free))
        {
            p_lpm_info->free = TRUE;
            p_lpm_info->free_key_index = hash_result_index[index];
        }
    }

    if (pointer_hit)
    {
        p_lpm_info->pointer.value = nexthop[pointer_hit_index];
    }

    if (nexthop_hit)
    {
        p_lpm_info->nexthop.value = nexthop[nexthop_hit_index];
    }

    DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_type(p_lookup_info, &search_key_type), ret, out);

    if ((INVALID_NEXTHOP != p_lpm_info->nexthop.value) && (LPM_HASH_KEY_TYPE_NEXTHOP == search_key_type))
    {
        p_lpm_info->nexthop_valid = TRUE;
    }

    if ((INVALID_POINTER != p_lpm_info->pointer.value) && (LPM_HASH_KEY_TYPE_POINTER == search_key_type))
    {
        p_lpm_info->pointer_valid = TRUE;
    }

    if (p_lpm_info->nexthop_valid || p_lpm_info->pointer_valid)
    {
        /* when hash key hit and value is meaningful. */
        p_lpm_info->valid = TRUE;
    }

    if (pointer_hit && (LPM_HASH_KEY_TYPE_POINTER == search_key_type))
    {
        p_lookup_result->key_index = hash_result_index[pointer_hit_index];
        /* when hash key hit no matter ad value. */
        p_lookup_result->valid = TRUE;
    }
    else if (nexthop_hit && (LPM_HASH_KEY_TYPE_NEXTHOP == search_key_type))
    {
        p_lookup_result->key_index = hash_result_index[nexthop_hit_index];
        /* when hash key hit no matter ad value. */
        p_lookup_result->valid = TRUE;
    }

    p_lpm_info->index_mask = index_mask[pointer_hit_index];

out:
    if (p_ds_hash_search_key)
    {
        mem_free(p_ds_hash_search_key);
    }

    return ret;
}

int32
drv_greatbelt_lpm_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    lpm_info_t* p_lpm_info = NULL;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);
    DRV_PTR_VALID_CHECK(p_lookup_result);
    p_lpm_info = p_lookup_result->extra;
    DRV_PTR_VALID_CHECK(p_lpm_info);

    p_lpm_info->pointer.value = INVALID_POINTER;
    p_lpm_info->nexthop.value = INVALID_NEXTHOP;

    DRV_IF_ERROR_RETURN(_drv_greatbelt_lpm_perform_lookup(p_lookup_info, p_lookup_result));

    if (p_lpm_info->nexthop_valid || p_lpm_info->pointer_valid)
    {
        p_lpm_info->valid = TRUE;
    }
    else if (p_lpm_info->free)
    {
        ; /* free entry*/
    }
    else
    {
        p_lookup_result->conflict = TRUE;
    }

    return DRV_E_NONE;
}


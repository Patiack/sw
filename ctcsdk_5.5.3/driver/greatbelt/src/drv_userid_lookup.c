/****************************************************************************
 * drv_userid_lookup.c  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:     V4.3.0
 * Author:       XuZx
 * Date:         2011-07-15
 * Reason:       Create for GreatBelt v4.3.0
 *
 * Revision:     V4.7.1
 * Revisor:      XuZx
 * Date:         2011-08-02
 * Reason:       Sync for GreatBelt v4.7.1
 *
 * Revision:     V4.29.3
 * Revisor:      XuZx
 * Date:         2011-10-10
 * Reason:       Sync for GreatBelt v4.29.3
 *
 * Revision:     V5.1.0
 * Revisor:      XuZx
 * Date:         2011-12-15
 * Reason:       Sync for GreatBelt v5.1.0
 *
 * Revision:     v5.7.0
 * Revisor:      XuZx
 * Date:         2012-01-17.
 * Reason:       Revise for GreatBelt v5.7.0
 *
 * Revision:     v5.9.0
 * Revisor:      XuZx
 * Date:         2012-02-03.
 * Reason:       Revise for GreatBelt v5.9.0
 ****************************************************************************/
#include "greatbelt/include/drv_lib.h"
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)
/* temp code !! add by zhouw  for cosim store hash key */
#include "sim_common.h"
#endif

/* 80bit unit */
static uint32 level_base[USERID_HASH_LEVEL_NUM] = {0, 0x4000};

STATIC int32
_drv_greatbelt_userid_lookup_select_result(uint8 chip_id,
                                 lookup_result_t* p_lookup_result,
                                 lookup_result_t* p_userid_result)
{
    uint8  level_index = 0;
    uint8  level_hash_en = 0;
    uint32 cmd;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl_t));
    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &user_id_hash_lookup_ctl));

    SET_BIT(level_hash_en, USERID_HASH_LEVEL0);
    SET_BIT(level_hash_en, user_id_hash_lookup_ctl.level1_hash_en);

    p_userid_result->valid = FALSE;
    p_userid_result->free = FALSE;
    p_userid_result->conflict = FALSE;

    for (level_index = USERID_HASH_LEVEL0; level_index < USERID_HASH_LEVEL_NUM; level_index++)
    {
        if (IS_BIT_SET(level_hash_en, level_index) && p_lookup_result[level_index].valid)
        {
            p_userid_result->valid = p_lookup_result[level_index].valid;
            p_userid_result->ad_index = p_lookup_result[level_index].ad_index;
            p_userid_result->free = p_lookup_result[level_index].free;
            p_userid_result->key_index = level_base[level_index]
                + (p_lookup_result[level_index].key_index & USERID_HASH_INDEX_MASK);
            goto END_USERID_LOOKUP_SELECT_RESULT;
        }
    }

    for (level_index = USERID_HASH_LEVEL0; level_index < USERID_HASH_LEVEL_NUM; level_index++)
    {
        if (IS_BIT_SET(level_hash_en, level_index) && p_lookup_result[level_index].free)
        {
            p_userid_result->valid = p_lookup_result[level_index].valid;
            p_userid_result->ad_index = p_lookup_result[level_index].ad_index;
            p_userid_result->free = p_lookup_result[level_index].free;
            p_userid_result->key_index = level_base[level_index]
                + (p_lookup_result[level_index].key_index & USERID_HASH_INDEX_MASK);
            goto END_USERID_LOOKUP_SELECT_RESULT;
        }
    }

    p_userid_result->conflict = TRUE;

END_USERID_LOOKUP_SELECT_RESULT:

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_userid_lookup_hash(userid_hash_level_t userid_hash_level,
                        bucket_entry_t      bucket_entry,
                        lookup_info_t*      p_lookup_info,
                        lookup_result_t*    p_lookup_result)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 ds_ad_index[BUCKET_ENTRY_NUM] = {0};
    uint32 bucket[BUCKET_ENTRY_NUM] = {0};
    uint32 tbl_id = MaxTblId_t;
    uint32 search_key_entry_valid[BUCKET_ENTRY_NUM] = {0};
    uint32 bucket_ptr = 0;
    uint32 level_bucket_num[USERID_HASH_LEVEL_NUM] = {0};
    uint8  hit = FALSE;
    uint8  free_hit = FALSE;
    uint8  hit_index = 0;
    uint8  free_index = 0;
    uint8  step = 0;
    ds_hash_key_t*  p_ds_hash_search_key = NULL;
    drv_hash_key_t drv_hash_mask;
    drv_hash_key_t drv_hash_lookup_key;
    drv_hash_key_t drv_hash_search_key;
    lookup_info_t lookup_info;
    key_info_t key_info;
    key_result_t key_result;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;
    uint8 polynomial_index[USERID_HASH_LEVEL_NUM] = {0};
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_result);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);

    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));

    sal_memset(&key_info, 0, sizeof(key_info));
    sal_memset(&key_result, 0, sizeof(key_result));
    sal_memset(&drv_hash_mask,       0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_lookup_key, 0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_search_key, 0, sizeof(drv_hash_key_t));

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl_t));
    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lookup_info.chip_id, 0, cmd, &user_id_hash_lookup_ctl));

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(lookup_info.hash_module, lookup_info.hash_type);

    if (MaxTblId_t == tbl_id)
    {
        return DRV_E_INVALID_TBL;
    }

    step = (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) * X3_STEP;
    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, drv_hash_lookup_key.key.hash_80bit[X6_STEP - step]));

    polynomial_index[USERID_HASH_LEVEL0] = user_id_hash_lookup_ctl.level0_hash_type;
    polynomial_index[USERID_HASH_LEVEL1] = user_id_hash_lookup_ctl.level1_hash_type;

    key_info.hash_module = HASH_MODULE_USERID;
    key_info.polynomial_index = polynomial_index[userid_hash_level];
    key_info.key_length = USERID_CRC_DATA_WIDTH;
    key_info.p_hash_key = drv_hash_lookup_key.key.hash_160bit[0];
    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));


    p_ds_hash_search_key = (ds_hash_key_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ds_hash_key_t));
    if (NULL == p_ds_hash_search_key)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_ds_hash_search_key, 0, sizeof(ds_hash_key_t));
    cmd = DRV_IOR(DsUserIdDoubleVlanHashKey_t, DRV_ENTRY_FLAG);

    level_bucket_num[BUCKET_ENTRY_INDEX0] = 0;
    level_bucket_num[BUCKET_ENTRY_INDEX1] = user_id_hash_lookup_ctl.level1_bucket_num;

    for (index = BUCKET_ENTRY_INDEX0; index < bucket_entry; index++)
    {
        if (USERID_HASH_LEVEL0 == userid_hash_level)
        {
            key_result.bucket_index = key_result.bucket_index & 0xFFF;
        }
        else if (USERID_HASH_LEVEL1 == userid_hash_level)
        {
            key_result.bucket_index = key_result.bucket_index & 0x3FFF;
        }
        bucket_ptr = key_result.bucket_index >> level_bucket_num[userid_hash_level];
        bucket[index] = (bucket_ptr * bucket_entry) + index;

        /* bucket_ptr address 80 bit, UserId level0 size is fixed 16K x 80bit */
        bucket_ptr = level_base[userid_hash_level] + (bucket_ptr * bucket_entry) + index;


        DRV_IF_ERROR_GOTO(DRV_IOCTL(lookup_info.chip_id, bucket_ptr, cmd, &p_ds_hash_search_key->word.x3[index]), ret, out);
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
                                                                     DRV_IOC_MEMID(cmd), bucket_ptr, (uint32*)data_entry), ret, out);
                }

#if 0
                if (TABLE_ENTRY_SIZE(tbl_id) == 24)
                {
                    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                     DRV_IOC_MEMID(cmd), bucket_ptr + 1, (uint32*)(data_entry + 3)));
                }

#endif
                if (index < 2)  /* hash0 4 4th read 80bits (320bits), hash1 2th read 80bits (160bits) */
                {
                    DRV_IF_ERROR_GOTO(cosim_db.store_table(lookup_info.chip_id, DRV_IOC_MEMID(cmd),
                                                             bucket_ptr, data_entry, FALSE), ret, out);
                }
                else
                {
                    if (userid_hash_level == USERID_HASH_LEVEL0)
                    {
                        DRV_IF_ERROR_GOTO(cosim_db.store_table(lookup_info.chip_id, DRV_IOC_MEMID(cmd),
                                                                 bucket_ptr, data_entry, FALSE), ret, out);
                    }
                }
            }
        }

#endif

    }

    DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_mask(lookup_info.chip_id,
                                                     lookup_info.hash_module,
                                                     lookup_info.hash_type,
                                                     drv_hash_mask.key.hash_160bit[0]), ret, out);

    for (index = BUCKET_ENTRY_INDEX0; index < bucket_entry; index += step)
    {
        lookup_info.p_ds_key = &p_ds_hash_search_key->word.x3[index];

        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, drv_hash_search_key.key.hash_80bit[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_entry_valid(&lookup_info, &search_key_entry_valid[index]), ret, out);
        DRV_IF_ERROR_GOTO(drv_greatbelt_hash_lookup_get_key_ad_index(&lookup_info, &ds_ad_index[index]), ret, out);

        if (KEY_MATCH(drv_hash_lookup_key.key.hash_80bit[X6_STEP - step], drv_hash_search_key.key.hash_80bit[index],
                      drv_hash_mask.key.hash_160bit[0], DRV_HASH_80BIT_KEY_LENGTH * step))
        {
            hit = TRUE;
            hit_index = index;
            break;
        }

        if ((!search_key_entry_valid[index]) && (!free_hit))
        {
            free_hit = TRUE;
            free_index = index;
        }
    }

    if (hit)
    {
        p_lookup_result->valid = TRUE;
        p_lookup_result->key_index = bucket[hit_index];
        p_lookup_result->ad_index = ds_ad_index[hit_index];
    }
    else if (free_hit)
    {
        p_lookup_result->free = TRUE;
        p_lookup_result->key_index = bucket[free_index];
    }
    else
    {
        p_lookup_result->key_index = DRV_HASH_INVALID_INDEX;
    }

out:
    if (p_ds_hash_search_key)
    {
        mem_free(p_ds_hash_search_key);
    }

    return ret;
}

int32
drv_greatbelt_userid_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_userid_result)
{
    uint8 hash_level = 0;
    uint8 level_hash_en = 0;
    uint8 userid_bucket_entry[USERID_HASH_LEVEL_NUM] = {4, 2};
    uint32 cmd = 0;
    lookup_info_t lookup_info;
    lookup_result_t lookup_result[USERID_HASH_LEVEL_NUM];
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);
    DRV_PTR_VALID_CHECK(p_userid_result);

    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));
    sal_memset(lookup_result, 0, sizeof(lookup_result_t) * USERID_HASH_LEVEL_NUM);

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl_t));
    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lookup_info.chip_id, 0, cmd, &user_id_hash_lookup_ctl));

    /* userid hash level0 always enabled */
    SET_BIT(level_hash_en, USERID_HASH_LEVEL0);
    SET_BIT(level_hash_en, user_id_hash_lookup_ctl.level1_hash_en);

    for (hash_level = USERID_HASH_LEVEL0; hash_level < USERID_HASH_LEVEL_NUM; hash_level++)
    {
        if (IS_BIT_SET(level_hash_en, hash_level))
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_userid_lookup_hash(hash_level,
                                                        userid_bucket_entry[hash_level],
                                                        &lookup_info,
                                                        &lookup_result[hash_level]));
        }
    }

    DRV_IF_ERROR_RETURN(_drv_greatbelt_userid_lookup_select_result(lookup_info.chip_id, lookup_result, p_userid_result));

    return DRV_E_NONE;
}


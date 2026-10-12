/****************************************************************************
 * drv_fib_lookup.c  All error code Deinfines, include SDK error code.
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
 * Date:         2011-07-22
 * Reason:       Revise for GreatBelt v4.5.1
 *
 * Revision:     V4.28.2
 * Revisor:      XuZx
 * Date:         2011-09-27
 * Reason:       Revise for GreatBelt v4.28.2
 *
 * Revision:     V4.29.3
 * Revisor:      XuZx
 * Date:         2011-10-10
 * Reason:       Revise for GreatBelt v4.29.3
 *
 * Vision:       V5.1.0
 * Revisor:      XuZx
 * Date:         2011-12-12.
 * Reason:       Revise for GreatBelt v5.1.0.
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

STATIC int32
_drv_greatbelt_fib_lookup_register_cam(lookup_info_t* p_lookup_info, lookup_result_t* p_fib_lookup_result)
{
    uint32 cam_entry_index = 0;
    uint32 cmd = 0;
    uint8 free_index = FDB_CAM_NUM;
    tbls_id_t tbls_id = MaxTblId_t;
    dynamic_ds_fdb_lookup_index_cam_t dynamic_ds_fdb_lookup_index_cam;
    ds_mac_hash_key_t ds_mac_hash_key[FDB_CAM_NUM];
    ds_6word_hash_key_t ds_6word_hash_key;
    uint8 hash_lookup_key[DRV_HASH_160BIT_KEY_LENGTH] = {0};
    uint8 hash_lookup_mask[DRV_HASH_160BIT_KEY_LENGTH] = {0};
    uint8 hash_search_key[DRV_HASH_160BIT_KEY_LENGTH] = {0};
    uint32 search_key_entry_valid = FALSE;
    uint32 ds_ad_index = {0};
    uint32 step = 0;
    lookup_info_t lookup_info;

    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_mask(lookup_info.chip_id,
                                                     lookup_info.hash_module,
                                                     lookup_info.hash_type,
                                                     hash_lookup_mask));

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, hash_lookup_key));
    tbls_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_FIB, lookup_info.hash_type);
    if (MaxTblId_t == tbls_id)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }
    step = TABLE_ENTRY_SIZE(tbls_id) / DRV_BYTES_PER_ENTRY;

    cmd = DRV_IOR(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);

    for (cam_entry_index = 0; cam_entry_index < TABLE_MAX_INDEX(DynamicDsFdbLookupIndexCam_t); cam_entry_index++)
    {
        sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));
        DRV_IF_ERROR_RETURN(DRV_IOCTL(p_lookup_info->chip_id, cam_entry_index, cmd, &dynamic_ds_fdb_lookup_index_cam));
        sal_memcpy(&ds_mac_hash_key[cam_entry_index * 2], &dynamic_ds_fdb_lookup_index_cam, sizeof(ds_6word_hash_key_t));
    }

    for (cam_entry_index = 0; cam_entry_index < TABLE_MAX_INDEX(DynamicDsFdbLookupIndexCam_t) * 2; cam_entry_index += step)
    {
        sal_memset(&ds_6word_hash_key, 0, sizeof(ds_6word_hash_key_t));
        sal_memcpy((uint8*)&ds_6word_hash_key, &ds_mac_hash_key[cam_entry_index], sizeof(ds_3word_hash_key_t) * step);

        lookup_info.p_ds_key = &ds_6word_hash_key;
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, hash_search_key));
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_entry_valid(&lookup_info, &search_key_entry_valid));
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_ad_index(&lookup_info, &ds_ad_index));

        if (KEY_MATCH(hash_lookup_key, hash_search_key, hash_lookup_mask, (DRV_HASH_80BIT_KEY_LENGTH * step)))
        {
            p_fib_lookup_result->key_index = cam_entry_index;
            p_fib_lookup_result->ad_index = ds_ad_index;
            p_fib_lookup_result->valid = TRUE;
            break;
        }
        else
        {
            if ((!search_key_entry_valid) && (FDB_CAM_NUM == free_index))
            {
                free_index = cam_entry_index;
                continue;
            }
        }
    }

    if (FDB_CAM_NUM == cam_entry_index)
    {
        if (FDB_CAM_NUM != free_index)
        {
            p_fib_lookup_result->free = TRUE;
            p_fib_lookup_result->key_index = free_index;
        }
        else
        {
            p_fib_lookup_result->free = FALSE;
        }

        p_fib_lookup_result->valid = FALSE;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_fib_lookup_result(uint8 chip_id, lookup_result_t* p_lookup_result, lookup_result_t* p_fib_result)
{
    uint8  level_index = 0;
    uint8  mac_level_bitmap = 0;
    int32  free_count = 0;
    uint32 cmd;
    uint32 level_mac_base[FIB_HASH_LEVEL_NUM] = {0};
    /* XuZx Note: should read chip pin info */
    uint32 pin_full_size_en = PIN_FULL_SIZE_EN;
    uint32 key_index;

    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;
    dynamic_ds_fdb_hash_index_cam_t dynamic_ds_fdb_hash_index_cam;

    sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl));
    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

    mac_level_bitmap = dynamic_ds_fdb_hash_ctl.mac_level_bitmap;

    if (0 != TABLE_MAX_INDEX(DynamicDsFdbLookupIndexCam_t))
    {
        SET_BIT(mac_level_bitmap, FIB_HASH_LEVEL5);
    }

    sal_memset(&dynamic_ds_fdb_hash_index_cam, 0, sizeof(dynamic_ds_fdb_hash_index_cam_t));
    cmd = DRV_IOR(DynamicDsFdbHashIndexCam_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_index_cam));

    level_mac_base[FIB_HASH_LEVEL0] = dynamic_ds_fdb_hash_index_cam.mac_num0 << 13;
    level_mac_base[FIB_HASH_LEVEL1] = dynamic_ds_fdb_hash_index_cam.mac_num1 << 13;
    level_mac_base[FIB_HASH_LEVEL2] = dynamic_ds_fdb_hash_index_cam.mac_num2 << 13;
    level_mac_base[FIB_HASH_LEVEL3] = dynamic_ds_fdb_hash_index_cam.mac_num3 << 13;
    level_mac_base[FIB_HASH_LEVEL4] = dynamic_ds_fdb_hash_index_cam.mac_num4 << 13;
    level_mac_base[FIB_HASH_LEVEL5] = 0;

    p_fib_result->valid = FALSE;
    p_fib_result->free = FALSE;
    p_fib_result->conflict = FALSE;

    for (level_index = FIB_HASH_LEVEL0; level_index < FIB_HASH_LEVEL_NUM; level_index++)
    {
        key_index = level_mac_base[level_index] + p_lookup_result[level_index].key_index;
        if (IS_BIT_SET(mac_level_bitmap, level_index)
            && p_lookup_result[level_index].valid
            && ((key_index < DDB_MAX_NUM) || pin_full_size_en))
        {
            p_fib_result->valid = p_lookup_result[level_index].valid;
            p_fib_result->ad_index = p_lookup_result[level_index].ad_index;
            p_fib_result->free = p_lookup_result[level_index].free;
            p_fib_result->key_index = key_index;

            if (FIB_HASH_LEVEL5 != level_index)
            {
                p_fib_result->key_index += FDB_CAM_NUM;
            }

            goto END_FIB_LOOKUP_SELECT_RESULT;
        }
    }

    for (free_count = FIB_BUCKET_ENTRY_NUM; free_count > 0; free_count--)
    {
        for (level_index = FIB_HASH_LEVEL0; level_index < FIB_HASH_LEVEL_NUM; level_index++)
        {
            if (IS_BIT_SET(mac_level_bitmap, level_index) && (p_lookup_result[level_index].free == free_count))
            {
                p_fib_result->valid = p_lookup_result[level_index].valid;
                p_fib_result->ad_index = p_lookup_result[level_index].ad_index;
                p_fib_result->free = p_lookup_result[level_index].free;
                p_fib_result->key_index = level_mac_base[level_index] + p_lookup_result[level_index].key_index;

                if (FIB_HASH_LEVEL5 != level_index)
                {
                    p_fib_result->key_index += FDB_CAM_NUM;
                }

                goto END_FIB_LOOKUP_SELECT_RESULT;
            }
        }
    }

END_FIB_LOOKUP_SELECT_RESULT:
    return DRV_E_NONE;
}

int32
_drv_greatbelt_fib_lookup_hash(uint8 fib_hash_level, lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    uint8  hit = FALSE;
    uint8  free_hit = 0;
    uint8  hit_index = 0;
    uint8  step = X6_STEP;
    uint8  chip_id = p_lookup_info->chip_id;
    uint8  polynomial_index[FIB_HASH_LEVEL_NUM] = {0};
    uint8  bucket_num[FIB_HASH_LEVEL_NUM] = {0};
    uint8  mac_num[FIB_HASH_LEVEL_NUM] = {0};
    uint32 free_index = INVALID_BUCKET_ENTRY_INDEX;
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 ds_ad_index[BUCKET_ENTRY_NUM] = {0};
    uint32 tbl_id = 0;
    uint32 bucket[BUCKET_ENTRY_NUM] = {0};
    uint32 bucket_ptr = 0;
    uint32 search_key_entry_valid[BUCKET_ENTRY_NUM] = {0};
    uint32 bucket_index = 0;

    ds_hash_key_t*  p_ds_hash_search_key = NULL;
    drv_hash_key_t drv_hash_mask;
    drv_hash_key_t drv_hash_lookup_key;
    drv_hash_key_t drv_hash_search_key;

    lookup_info_t lookup_info;
    key_info_t key_info;
    key_result_t key_result;
    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;
    dynamic_ds_fdb_hash_index_cam_t dynamic_ds_fdb_hash_index_cam;
    int32 ret = DRV_E_NONE;

    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&key_info, 0, sizeof(key_info));
    sal_memset(&key_result, 0, sizeof(key_result));

    sal_memset(&drv_hash_mask,      0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_lookup_key, 0, sizeof(drv_hash_key_t));
    sal_memset(&drv_hash_search_key, 0, sizeof(drv_hash_key_t));
    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(lookup_info.hash_module, lookup_info.hash_type);

    if (MaxTblId_t == tbl_id)
    {
        return DRV_E_INVALID_TBL;
    }

    step = (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) * X3_STEP;

    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &fib_engine_lookup_result_ctl));
    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(p_lookup_info, drv_hash_lookup_key.key.hash_80bit[X6_STEP - step]));

    sal_memset(&dynamic_ds_fdb_hash_index_cam, 0, sizeof(dynamic_ds_fdb_hash_index_cam_t));
    cmd = DRV_IOR(DynamicDsFdbHashIndexCam_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_index_cam));

    polynomial_index[FIB_HASH_LEVEL0] = dynamic_ds_fdb_hash_index_cam.level0_bucket_index;
    polynomial_index[FIB_HASH_LEVEL1] = dynamic_ds_fdb_hash_index_cam.level1_bucket_index;
    polynomial_index[FIB_HASH_LEVEL2] = dynamic_ds_fdb_hash_index_cam.level2_bucket_index;
    polynomial_index[FIB_HASH_LEVEL3] = dynamic_ds_fdb_hash_index_cam.level3_bucket_index;
    polynomial_index[FIB_HASH_LEVEL4] = dynamic_ds_fdb_hash_index_cam.level4_bucket_index;

    bucket_num[FIB_HASH_LEVEL0] = dynamic_ds_fdb_hash_index_cam.level0_bucket_num;
    bucket_num[FIB_HASH_LEVEL1] = dynamic_ds_fdb_hash_index_cam.level1_bucket_num;
    bucket_num[FIB_HASH_LEVEL2] = dynamic_ds_fdb_hash_index_cam.level2_bucket_num;
    bucket_num[FIB_HASH_LEVEL3] = dynamic_ds_fdb_hash_index_cam.level3_bucket_num;
    bucket_num[FIB_HASH_LEVEL4] = dynamic_ds_fdb_hash_index_cam.level4_bucket_num;

    mac_num[FIB_HASH_LEVEL0] = dynamic_ds_fdb_hash_index_cam.mac_num0;
    mac_num[FIB_HASH_LEVEL1] = dynamic_ds_fdb_hash_index_cam.mac_num1;
    mac_num[FIB_HASH_LEVEL2] = dynamic_ds_fdb_hash_index_cam.mac_num2;
    mac_num[FIB_HASH_LEVEL3] = dynamic_ds_fdb_hash_index_cam.mac_num3;
    mac_num[FIB_HASH_LEVEL4] = dynamic_ds_fdb_hash_index_cam.mac_num4;

    key_info.hash_module = HASH_MODULE_FIB;
    key_info.polynomial_index = polynomial_index[fib_hash_level];
    key_info.key_length = FIB_CRC_DATA_WIDTH;
    key_info.p_hash_key = drv_hash_lookup_key.key.hash_80bit[0];

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_mask(chip_id,
                                                     p_lookup_info->hash_module,
                                                     p_lookup_info->hash_type,
                                                     drv_hash_mask.key.hash_160bit[0]));

    p_ds_hash_search_key = (ds_hash_key_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ds_hash_key_t));
    if (NULL == p_ds_hash_search_key)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_ds_hash_search_key, 0, sizeof(ds_hash_key_t));
    cmd = DRV_IOR(DsMacHashKey_t, DRV_ENTRY_FLAG);

    for (index = BUCKET_ENTRY_INDEX0; index <= BUCKET_ENTRY_INDEX1; index++)
    {
        bucket_index = key_result.bucket_index >> (bucket_num[fib_hash_level]);
        bucket_ptr = (mac_num[fib_hash_level] << 13) + (bucket_index << 1) + index;
        bucket[index] = (bucket_index << 1) + index;
        DRV_IF_ERROR_GOTO(DRV_IOCTL(chip_id, bucket_ptr, cmd, &p_ds_hash_search_key->word.x3[index]), ret, out);

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
                    DRV_IF_ERROR_GOTO(drv_gb_io_api.drv_sram_tbl_read(chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                     DRV_IOC_MEMID(cmd), bucket_ptr, (uint32*)data_entry), ret, out);
                }

                if (TABLE_ENTRY_SIZE(tbl_id) == 24)
                {
                    DRV_IF_ERROR_GOTO(drv_gb_io_api.drv_sram_tbl_read(lookup_info.chip_id - drv_gb_init_chip_info.drv_init_chipid_base,
                                                                     DRV_IOC_MEMID(cmd), bucket[index] + 1, (uint32*)(data_entry + 3)), ret, out);
                }

                DRV_IF_ERROR_GOTO(cosim_db.store_table(chip_id, tbl_id, bucket_ptr, data_entry, FALSE), ret, out);
            }
        }

#endif
    }

    for (index = BUCKET_ENTRY_INDEX0; index <= BUCKET_ENTRY_INDEX1; index += step)
    {
        sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));
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

        if (!search_key_entry_valid[index])
        {
            free_hit++;

            if (INVALID_BUCKET_ENTRY_INDEX == free_index)
            {
                free_index = index;
            }
        }
    }

    p_lookup_result[fib_hash_level].valid = FALSE;
    p_lookup_result[fib_hash_level].free = FALSE;

    if (hit)
    {
        p_lookup_result[fib_hash_level].valid = TRUE;
        p_lookup_result[fib_hash_level].key_index = bucket[hit_index];
        p_lookup_result[fib_hash_level].ad_index = ds_ad_index[hit_index];
    }
    /* XuZx note: Add for Cmodel and SDK only. */
    else if (free_hit)
    {
        p_lookup_result[fib_hash_level].free = free_hit;
        p_lookup_result[fib_hash_level].key_index = bucket[free_index];
    }
    else
    {
        p_lookup_result[fib_hash_level].key_index = DRV_HASH_INVALID_INDEX;
    }

out:
    if (p_ds_hash_search_key)
    {
        mem_free(p_ds_hash_search_key);
    }

    return ret;
}

int32
drv_greatbelt_fib_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_fib_result)
{

    uint8 level_index = 0;
    uint32 cmd = 0;
    lookup_info_t lookup_info;
    lookup_result_t lookup_result[FIB_HASH_LEVEL_NUM];
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);
    DRV_PTR_VALID_CHECK(p_fib_result);

    sal_memcpy(&lookup_info, p_lookup_info, sizeof(lookup_info_t));
    sal_memset(lookup_result, 0, sizeof(lookup_result_t) * FIB_HASH_LEVEL_NUM);

    sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl));
    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lookup_info.chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

    for (level_index = FIB_HASH_LEVEL0; level_index <= FIB_HASH_LEVEL4; level_index++)
    {
        if (IS_BIT_SET(dynamic_ds_fdb_hash_ctl.mac_level_bitmap, level_index))
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_fib_lookup_hash(level_index, &lookup_info, lookup_result));
        }
    }

    if (0 != TABLE_MAX_INDEX(DynamicDsFdbLookupIndexCam_t))
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_fib_lookup_register_cam(&lookup_info, &lookup_result[FIB_HASH_LEVEL5]));
    }

    DRV_IF_ERROR_RETURN(_drv_greatbelt_fib_lookup_result(lookup_info.chip_id, lookup_result, p_fib_result));

    return DRV_E_NONE;
}


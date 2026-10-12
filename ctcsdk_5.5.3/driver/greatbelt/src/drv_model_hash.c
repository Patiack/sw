/****************************************************************************
 * drv_model_hash.c  Provides driver model hash handle function.
 *
 * Copyright (C) 2011 Centec Networks Inc.  All rights reserved.
 *
 ****************************************************************************/
#include "greatbelt/include/drv_model_hash.h"

int32
drv_greatbelt_model_add_fib_key(uint8 chip_id, fib_hash_type_t fib_hash_type, void* p_ds_key)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 key_index = 0;
    dynamic_ds_fdb_lookup_index_cam_t dynamic_ds_fdb_lookup_index_cam;
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;
    ds_mac_hash_key_t ds_mac_hash_key;

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_FIB, fib_hash_type);

    if (MaxTblId_t == tbl_id)
    {
        MODEL_HASH_DEBUG_INFO("Invalid FIB key type.\n");
        return DRV_E_INVALID_HASH_TYPE;
    }

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_FIB;
    lookup_info.hash_type = fib_hash_type;
    lookup_info.p_ds_key = p_ds_key;

    if (FIB_HASH_TYPE_MAC == fib_hash_type)
    {
        sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl_t));
        cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

        if (dynamic_ds_fdb_hash_ctl.ad_bits_type)
        {
            sal_memset(&ds_mac_hash_key, 0, sizeof(ds_mac_hash_key_t));
            sal_memcpy(&ds_mac_hash_key, lookup_info.p_ds_key, sizeof(ds_mac_hash_key_t));
            CLEAR_BIT(ds_mac_hash_key.vsi_id, 13);
            lookup_info.p_ds_key = &ds_mac_hash_key;
        }
    }

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));
    key_index = lookup_result.key_index;

    MODEL_HASH_DEBUG_INFO("@@ Show lookup result: @@\n");
    MODEL_HASH_DEBUG_INFO("is_conflict = %d\n", lookup_result.conflict);
    MODEL_HASH_DEBUG_INFO("key_index = 0x%x!\n", lookup_result.key_index);
    MODEL_HASH_DEBUG_INFO("valid = 0x%x!\n", lookup_result.valid);
    MODEL_HASH_DEBUG_INFO("ad_index = 0x%x!\n", lookup_result.ad_index);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");

    if (lookup_result.valid)
    {
        MODEL_HASH_DEBUG_INFO("The FIB hash key, table-id = 0x%X already exist.\n", tbl_id);
    }
    else
    {
        if (lookup_result.free)
        {
            if (key_index >= FDB_CAM_NUM)
            {
                key_index -= FDB_CAM_NUM;
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, key_index, cmd, p_ds_key));

                sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
                DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));
                MODEL_HASH_DEBUG_INFO("Add FIB hash key: table-id = 0x%X, key-index = 0x%X, ad-index = 0x%X.\n",
                                      tbl_id, key_index, lookup_result.ad_index);
            }
            else
            {
                if (2 == (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY))
                {
                    cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, key_index / 2, cmd, p_ds_key));
                }
                else
                {
                    sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));
                    cmd = DRV_IOR(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));

                    if (key_index % 2)
                    {
                        sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                                   p_ds_key,
                                   sizeof(ds_3word_hash_key_t));
                    }
                    else
                    {
                        sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam, p_ds_key, sizeof(ds_3word_hash_key_t));
                    }

                    cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));
                }

                DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));
                MODEL_HASH_DEBUG_INFO("Add DynamicDsFdbLookupIndexCam: cam-index = 0x%X, entry-index = 0x%X, ad-index = 0x%X.\n",
                                      key_index / 2, key_index % 2, lookup_result.ad_index);
            }
        }
        else
        {
            MODEL_HASH_DEBUG_INFO("Fail to add new FIB hash key because of conflict.\n");
            return DRV_E_HASH_CONFLICT;
        }
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_delete_fib_key(uint8 chip_id, fib_hash_type_t fib_hash_type, void* p_ds_key)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    uint32 cmd = 0;
    uint32 tbl_id = MaxTblId_t;
    ds_mac_hash_key_t ds_mac_hash_key;
    ds_6word_hash_key_t ds_6word_hash_key;
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;

    dynamic_ds_fdb_lookup_index_cam_t dynamic_ds_fdb_lookup_index_cam;

    sal_memset(&ds_6word_hash_key, 0, sizeof(ds_6word_hash_key_t));
    sal_memset(&ds_mac_hash_key, 0, sizeof(ds_mac_hash_key_t));
    sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));

    tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_FIB, fib_hash_type);

    if (MaxTblId_t == tbl_id)
    {
        MODEL_HASH_DEBUG_INFO("Invalid FIB key type.\n");
        return DRV_E_INVALID_HASH_TYPE;
    }

    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_FIB;
    lookup_info.hash_type = fib_hash_type;
    lookup_info.p_ds_key = p_ds_key;

    if (FIB_HASH_TYPE_MAC == fib_hash_type)
    {
        sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl_t));
        cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

        if (dynamic_ds_fdb_hash_ctl.ad_bits_type)
        {
            sal_memcpy(&ds_mac_hash_key, lookup_info.p_ds_key, sizeof(ds_mac_hash_key_t));
            CLEAR_BIT(ds_mac_hash_key.vsi_id, 13);
            lookup_info.p_ds_key = &ds_mac_hash_key;
        }
    }

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    MODEL_HASH_DEBUG_INFO("@@ Show lookup result: @@\n");
    MODEL_HASH_DEBUG_INFO("is_conflict = %d\n", lookup_result.conflict);
    MODEL_HASH_DEBUG_INFO("key_index = 0x%x!\n", lookup_result.key_index);
    MODEL_HASH_DEBUG_INFO("valid = 0x%x!\n", lookup_result.valid);
    MODEL_HASH_DEBUG_INFO("ad_index = 0x%x!\n", lookup_result.ad_index);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");

    if (lookup_result.valid)
    {
        if (lookup_result.key_index >= FDB_CAM_NUM)
        {
            lookup_result.key_index -= FDB_CAM_NUM;

            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index, cmd, &ds_6word_hash_key));
            MODEL_HASH_DEBUG_INFO("Delete FIB hash key: table-id = 0x%X, key-index = 0x%X, ad-index = 0x%X.\n",
                                  tbl_id, lookup_result.key_index, lookup_result.ad_index);
#if (DRVIER_WORK_PLATFORM == 1)
            uint8 chipid_base = 0;
            DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
            drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, tbl_id, lookup_result.key_index);
#endif
        }
        else
        {
            if (2 == (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY))
            {
                cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index / 2, cmd, &ds_6word_hash_key));
#if (DRVIER_WORK_PLATFORM == 1)
                uint8 chipid_base = 0;
                DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
                drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, DynamicDsFdbLookupIndexCam_t, lookup_result.key_index);
#endif
                MODEL_HASH_DEBUG_INFO("Delete DynamicDsFdbLookupIndexCam: cam-index = 0x%X, ad-index = 0x%X.\n",
                                      lookup_result.key_index / 2, lookup_result.ad_index);
            }
            else
            {
                sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));
                cmd = DRV_IOR(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));

                if (lookup_result.key_index % 2)
                {
                    sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                               &ds_6word_hash_key,
                               sizeof(ds_3word_hash_key_t));
                }
                else
                {
                    sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam, &ds_6word_hash_key, sizeof(ds_3word_hash_key_t));
                }

                cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));

#if (DRVIER_WORK_PLATFORM == 1)
                uint8 chipid_base = 0;

                if ((!dynamic_ds_fdb_lookup_index_cam.valid0) && (!dynamic_ds_fdb_lookup_index_cam.valid1))
                {
                    DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
                    drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, DynamicDsFdbLookupIndexCam_t, lookup_result.key_index / 2);
                }

#endif
                MODEL_HASH_DEBUG_INFO("Delete DynamicDsFdbLookupIndexCam: cam-index = 0x%X, entry-index = 0x%X, ad-index = 0x%X.\n",
                                      lookup_result.key_index / 2,  lookup_result.key_index % 2, lookup_result.ad_index);
            }
        }
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("The FIB hash key isn't exist.\n");
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_add_lpm_key(uint8 chip_id, uint8 hash_type, void* p_ds_hash_key)
{
    lookup_result_t lookup_result;
    lookup_info_t lookup_info;
    lpm_info_t lpm_info;
    uint32 table_id = 0;
    uint32 cmd = 0;
    uint8 key_size = 0;
    ds_3word_hash_key_t ds_3word_hash_key;

    sal_memset(&lpm_info, 0, sizeof(lpm_info_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&ds_3word_hash_key, 0, sizeof(ds_3word_hash_key_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_LPM;
    lookup_info.p_ds_key = p_ds_hash_key;
    lookup_info.hash_type = hash_type;
    lookup_result.extra = &lpm_info;

    table_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_LPM, hash_type);

    if (MaxTblId_t == table_id)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    if ((!lookup_result.valid) && lpm_info.free)
    {
        key_size = TABLE_ENTRY_SIZE(table_id);

        if (DRV_BYTES_PER_ENTRY == key_size)
        {
            cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lpm_info.free_key_index, cmd, p_ds_hash_key));
        }
        else if ((DRV_BYTES_PER_ENTRY * 2) == key_size)
        {
            /* All LPM hash key use share memory */
            cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);

            sal_memcpy(&ds_3word_hash_key, p_ds_hash_key, sizeof(ds_3word_hash_key_t));
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lpm_info.free_key_index, cmd, &ds_3word_hash_key));
            sal_memcpy(&ds_3word_hash_key, (uint8*)p_ds_hash_key + sizeof(ds_3word_hash_key_t), sizeof(ds_3word_hash_key_t));
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lpm_info.free_key_index + 1, cmd, &ds_3word_hash_key));
        }

        MODEL_HASH_DEBUG_INFO("Add LPM hash key, table-id = 0x%X, key-index = 0x%X.\n", table_id, lpm_info.free_key_index);
    }
    else if (lookup_result.valid)
    {
        MODEL_HASH_DEBUG_INFO("The LPM hash key already exists.\n");
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("Add LPM hash key failed because of conflict.\n");
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_delete_lpm_key(uint8 chip_id, fib_hash_type_t hash_type, void* p_ds_key)
{
    lookup_result_t lookup_result;
    lookup_info_t lookup_info;
    lpm_info_t lpm_info;
    uint32 cmd = 0;
    uint32 table_id = MaxTblId_t;
    ds_3word_hash_key_t ds_3word_hash_key;
    uint8 key_size = 0;

    sal_memset(&lpm_info, 0, sizeof(lpm_info_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&ds_3word_hash_key, 0, sizeof(ds_3word_hash_key_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_LPM;
    lookup_info.p_ds_key = p_ds_key;
    lookup_info.hash_type = hash_type;

    lookup_result.extra = &lpm_info;

    table_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_LPM, hash_type);

    if (MaxTblId_t == table_id)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    if (!lookup_result.valid)
    {
        MODEL_HASH_DEBUG_INFO("The LPM hash key isn't exist.\n");
    }
    else
    {
        key_size = TABLE_ENTRY_SIZE(table_id);

        if (DRV_BYTES_PER_ENTRY == key_size)
        {
            cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index, cmd, &ds_3word_hash_key));
        }
        else if ((DRV_BYTES_PER_ENTRY * 2) == key_size)
        {
            /* All LPM hash key use share memory */
            cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index, cmd, &ds_3word_hash_key));
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index + 1, cmd, &ds_3word_hash_key));
        }

        MODEL_HASH_DEBUG_INFO("Delete LPM hash key, table-id = 0x%X, key-index = 0x%X.\n", table_id, lookup_result.key_index);

#if (DRVIER_WORK_PLATFORM == 1)
        uint8 chipid_base = 0;
        DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
        drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, table_id, lookup_result.key_index);
#endif
    }

    return DRV_E_NONE;

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_model_add_queue_key_cam(uint8 chip_id, void* p_queue_key)
{
    ds_queue_hash_key_t* p_key = NULL;
    q_hash_cam_ctl_t q_hash_cam_ctl;
    uint32 cmd = 0;
    uint8 i = 0;
    uint32 valid = 0;

    p_key = (ds_queue_hash_key_t*)p_queue_key;

    sal_memset(&q_hash_cam_ctl, 0, sizeof(q_hash_cam_ctl));
    cmd = DRV_IOR(QHashCamCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &q_hash_cam_ctl));
    for(i = 0; i < 8; i++)
    {
        cmd = DRV_IOR(QHashCamCtl_t, (QHashCamCtl_Valid0_f + i));
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &valid));

        if(valid)
        {
            continue;
        }
        else
        {
            switch(i)
            {
                case 0:
                    q_hash_cam_ctl.channel_id0      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac0           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id0    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue0      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type0       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast0           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base0      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id0      = p_key->service_id0;
                    q_hash_cam_ctl.valid0           = 1;
                    break;

                case 1:
                    q_hash_cam_ctl.channel_id1      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac1           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id1    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue1      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type1       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast1           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base1      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id1      = p_key->service_id0;
                    q_hash_cam_ctl.valid1           = 1;
                    break;

                case 2:
                    q_hash_cam_ctl.channel_id2      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac2           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id2    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue2      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type2       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast2           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base2      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id2      = p_key->service_id0;
                    q_hash_cam_ctl.valid2           = 1;
                    break;

                case 3:
                    q_hash_cam_ctl.channel_id3      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac3           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id3    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue3      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type3       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast3           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base3      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id3      = p_key->service_id0;
                    q_hash_cam_ctl.valid3           = 1;
                    break;

                case 4:
                    q_hash_cam_ctl.channel_id4      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac4           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id4    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue4      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type4       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast4           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base4      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id4      = p_key->service_id0;
                    q_hash_cam_ctl.valid4           = 1;
                    break;

                case 5:
                    q_hash_cam_ctl.channel_id5      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac5           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id5    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue5      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type5       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast5           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base5      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id5      = p_key->service_id0;
                    q_hash_cam_ctl.valid5           = 1;
                    break;

                case 6:
                    q_hash_cam_ctl.channel_id6      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac6           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id6    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue6      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type6       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast6           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base6      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id6      = p_key->service_id0;
                    q_hash_cam_ctl.valid6           = 1;
                    break;

                case 7:
                    q_hash_cam_ctl.channel_id7      = p_key->channel_id0;
                    q_hash_cam_ctl.sgmac7           = p_key->sgmac0;
                    q_hash_cam_ctl.dest_chip_id7    = p_key->dest_chip_id0;
                    q_hash_cam_ctl.dest_queue7      = p_key->dest_queue0;
                    q_hash_cam_ctl.hash_type7       = p_key->hash_type0;
                    q_hash_cam_ctl.mcast7           = p_key->mcast0;
                    q_hash_cam_ctl.queue_base7      = p_key->queue_base0;
                    q_hash_cam_ctl.service_id7      = p_key->service_id0;
                    q_hash_cam_ctl.valid7           = 1;
                    break;

            }
            cmd = DRV_IOW(QHashCamCtl_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &q_hash_cam_ctl));
            break;
        }
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_model_delete_queue_key_cam(uint8 chip_id, void* p_queue_key)
{

    ds_queue_hash_key_t* p_key = NULL;
    q_hash_cam_ctl_t q_hash_cam_ctl;
    uint32 cmd = 0;
    uint8 i = 0;

    p_key = (ds_queue_hash_key_t*)p_queue_key;
    sal_memset(&q_hash_cam_ctl, 0, sizeof(q_hash_cam_ctl));
    cmd = DRV_IOR(QHashCamCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &q_hash_cam_ctl));

    for(i = 0; i < 8; i++)
    {
        switch(i)
        {
            case 0:
                if((q_hash_cam_ctl.valid0)
                    && (q_hash_cam_ctl.channel_id0 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id0 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue0 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type0 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast0 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac0 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id0 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id0      = 0;
                    q_hash_cam_ctl.dest_chip_id0    = 0;
                    q_hash_cam_ctl.dest_queue0      = 0;
                    q_hash_cam_ctl.hash_type0       = 0;
                    q_hash_cam_ctl.mcast0           = 0;
                    q_hash_cam_ctl.sgmac0           = 0;
                    q_hash_cam_ctl.service_id0      = 0;
                    q_hash_cam_ctl.valid0           = 0;
                }
                break;
            case 1:
                if((q_hash_cam_ctl.valid1)
                    && (q_hash_cam_ctl.channel_id1 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id1 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue1 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type1 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast1 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac1 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id1 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id1      = 0;
                    q_hash_cam_ctl.dest_chip_id1    = 0;
                    q_hash_cam_ctl.dest_queue1      = 0;
                    q_hash_cam_ctl.hash_type1       = 0;
                    q_hash_cam_ctl.mcast1           = 0;
                    q_hash_cam_ctl.sgmac1           = 0;
                    q_hash_cam_ctl.service_id1      = 0;
                    q_hash_cam_ctl.valid1           = 0;
                }
                break;

            case 2:
                if((q_hash_cam_ctl.valid2)
                    && (q_hash_cam_ctl.channel_id2 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id2 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue2 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type2 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast2 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac2 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id2 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id2      = 0;
                    q_hash_cam_ctl.dest_chip_id2    = 0;
                    q_hash_cam_ctl.dest_queue2      = 0;
                    q_hash_cam_ctl.hash_type2       = 0;
                    q_hash_cam_ctl.mcast2           = 0;
                    q_hash_cam_ctl.sgmac2           = 0;
                    q_hash_cam_ctl.service_id2      = 0;
                    q_hash_cam_ctl.valid2           = 0;
                }
                break;

            case 3:
                if((q_hash_cam_ctl.valid3)
                    && (q_hash_cam_ctl.channel_id3 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id3 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue3 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type3 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast3 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac3 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id3 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id3      = 0;
                    q_hash_cam_ctl.dest_chip_id3    = 0;
                    q_hash_cam_ctl.dest_queue3      = 0;
                    q_hash_cam_ctl.hash_type3       = 0;
                    q_hash_cam_ctl.mcast3           = 0;
                    q_hash_cam_ctl.sgmac3           = 0;
                    q_hash_cam_ctl.service_id3      = 0;
                    q_hash_cam_ctl.valid3           = 0;
                }
                break;

            case 4:
                if((q_hash_cam_ctl.valid4)
                    && (q_hash_cam_ctl.channel_id4 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id4 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue4 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type4 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast4 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac4 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id4 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id4      = 0;
                    q_hash_cam_ctl.dest_chip_id4    = 0;
                    q_hash_cam_ctl.dest_queue4      = 0;
                    q_hash_cam_ctl.hash_type4       = 0;
                    q_hash_cam_ctl.mcast4           = 0;
                    q_hash_cam_ctl.sgmac4           = 0;
                    q_hash_cam_ctl.service_id4      = 0;
                    q_hash_cam_ctl.valid4           = 0;
                }
                break;

            case 5:
                if((q_hash_cam_ctl.valid5)
                    && (q_hash_cam_ctl.channel_id5 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id5 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue5 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type5 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast5 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac5 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id5 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id5      = 0;
                    q_hash_cam_ctl.dest_chip_id5    = 0;
                    q_hash_cam_ctl.dest_queue5      = 0;
                    q_hash_cam_ctl.hash_type5       = 0;
                    q_hash_cam_ctl.mcast5           = 0;
                    q_hash_cam_ctl.sgmac5           = 0;
                    q_hash_cam_ctl.service_id5      = 0;
                    q_hash_cam_ctl.valid5           = 0;
                }
                break;

            case 6:
                if((q_hash_cam_ctl.valid6)
                    && (q_hash_cam_ctl.channel_id6 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id6 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue6 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type6 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast6 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac6 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id6 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id6      = 0;
                    q_hash_cam_ctl.dest_chip_id6    = 0;
                    q_hash_cam_ctl.dest_queue6      = 0;
                    q_hash_cam_ctl.hash_type6       = 0;
                    q_hash_cam_ctl.mcast6           = 0;
                    q_hash_cam_ctl.sgmac6           = 0;
                    q_hash_cam_ctl.service_id6      = 0;
                    q_hash_cam_ctl.valid6           = 0;
                }
                break;

            case 7:
                if((q_hash_cam_ctl.valid7)
                    && (q_hash_cam_ctl.channel_id7 == p_key->channel_id0)
                    && (q_hash_cam_ctl.dest_chip_id7 == p_key->dest_chip_id0)
                    && (q_hash_cam_ctl.dest_queue7 == p_key->dest_queue0)
                    && (q_hash_cam_ctl.hash_type7 == p_key->hash_type0)
                    && (q_hash_cam_ctl.mcast7 == p_key->mcast0)
                    && (q_hash_cam_ctl.sgmac7 == p_key->sgmac0)
                    && (q_hash_cam_ctl.service_id7 == p_key->service_id0))
                {
                    q_hash_cam_ctl.channel_id7      = 0;
                    q_hash_cam_ctl.dest_chip_id7    = 0;
                    q_hash_cam_ctl.dest_queue7      = 0;
                    q_hash_cam_ctl.hash_type7       = 0;
                    q_hash_cam_ctl.mcast7           = 0;
                    q_hash_cam_ctl.sgmac7           = 0;
                    q_hash_cam_ctl.service_id7      = 0;
                    q_hash_cam_ctl.valid7           = 0;
                }
                break;

            default:
                break;
        }
    }

    cmd = DRV_IOW(QHashCamCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &q_hash_cam_ctl));

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_add_queue_key(uint8 chip_id, void* p_queue_key)
{
    ds_queue_hash_key_t* p_key = (ds_queue_hash_key_t*)p_queue_key;
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    ds_queue_hash_key_t search_queue_key;
    uint32 table_index = 0;
    uint32 cmd = 0;

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&search_queue_key, 0, sizeof(ds_queue_hash_key_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_QUEUE;
    lookup_info.p_ds_key = p_key;
    lookup_info.hash_type = QUEUE_KEY_TYPE_KEY;

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    if (lookup_result.valid)
    {
        MODEL_HASH_DEBUG_INFO("The queue hash key already exist.\n");
    }
    else if (lookup_result.free)
    {
        cmd = DRV_IOR(DsQueueHashKey_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_result.key_index, cmd, &search_queue_key));

        table_index = lookup_result.key_index;

        if (!search_queue_key.valid0)
        {
            search_queue_key.sgmac0 = p_key->sgmac0;
            search_queue_key.channel_id0 = p_key->channel_id0;
            search_queue_key.dest_chip_id0 = p_key->dest_chip_id0;
            search_queue_key.dest_queue0 = p_key->dest_queue0;
            search_queue_key.hash_type0 = p_key->hash_type0;
            search_queue_key.mcast0 = p_key->mcast0;
            search_queue_key.queue_base0 = p_key->queue_base0;
            search_queue_key.valid0 = TRUE;
            search_queue_key.service_id0 = p_key->service_id0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Add queue hash key0 indexed 0x%X.\n", table_index);
        }
        else if (!search_queue_key.valid1)
        {
            search_queue_key.sgmac1 = p_key->sgmac0;
            search_queue_key.channel_id1 = p_key->channel_id0;
            search_queue_key.dest_chip_id1 = p_key->dest_chip_id0;
            search_queue_key.dest_queue1 = p_key->dest_queue0;
            search_queue_key.hash_type1 = p_key->hash_type0;
            search_queue_key.mcast1 = p_key->mcast0;
            search_queue_key.queue_base1 = p_key->queue_base0;
            search_queue_key.valid1 = TRUE;
            search_queue_key.service_id1 = p_key->service_id0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Add queue hash key1 indexed 0x%X.\n", table_index);
        }
        else if (!search_queue_key.valid2)
        {
            search_queue_key.sgmac2 = p_key->sgmac0;
            search_queue_key.channel_id2 = p_key->channel_id0;
            search_queue_key.dest_chip_id2 = p_key->dest_chip_id0;
            search_queue_key.dest_queue2 = p_key->dest_queue0;
            search_queue_key.hash_type2 = p_key->hash_type0;
            search_queue_key.mcast2 = p_key->mcast0;
            search_queue_key.queue_base2 = p_key->queue_base0;
            search_queue_key.valid2 = TRUE;
            search_queue_key.service_id2 = p_key->service_id0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Add queue hash key2 indexed 0x%X.\n", table_index);
        }
        else if (!search_queue_key.valid3)
        {
            search_queue_key.sgmac3 = p_key->sgmac0;
            search_queue_key.channel_id3 = p_key->channel_id0;
            search_queue_key.dest_chip_id3 = p_key->dest_chip_id0;
            search_queue_key.dest_queue3 = p_key->dest_queue0;
            search_queue_key.hash_type3 = p_key->hash_type0;
            search_queue_key.mcast3 = p_key->mcast0;
            search_queue_key.queue_base3 = p_key->queue_base0;
            search_queue_key.valid3 = TRUE;
            search_queue_key.service_id3 = p_key->service_id0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Add queue hash key3 indexed 0x%X.\n", table_index);
        }
        else
        {
            MODEL_HASH_DEBUG_INFO("Fail to add new queue hash key because of conflict.\n");
        }
    }
    else
    {
        _drv_greatbelt_model_add_queue_key_cam(chip_id, p_queue_key);
        MODEL_HASH_DEBUG_INFO("drv_gb_io_api.drv_hash_key_get_index is NULL!");
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_delete_queue_key(uint8 chip_id, void* p_queue_key)
{
    ds_queue_hash_key_t* p_key = (ds_queue_hash_key_t*)p_queue_key;
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    ds_queue_hash_key_t search_queue_key;
    uint32 table_index = 0;
    uint32 cmd = 0;

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&search_queue_key, 0, sizeof(ds_queue_hash_key_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_module = HASH_MODULE_QUEUE;
    lookup_info.p_ds_key = p_key;
    lookup_info.hash_type = QUEUE_KEY_TYPE_KEY;

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    if (lookup_result.valid)
    {
        table_index = lookup_result.key_index;

        cmd = DRV_IOR(DsQueueHashKey_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));

        if ((search_queue_key.valid0)
            && (search_queue_key.channel_id0 == p_key->channel_id0)
            && (search_queue_key.dest_chip_id0 == p_key->dest_chip_id0)
            && (search_queue_key.dest_queue0 == p_key->dest_queue0)
            && (search_queue_key.hash_type0 == p_key->hash_type0)
            && (search_queue_key.mcast0 == p_key->mcast0)
            && (search_queue_key.sgmac0 == p_key->sgmac0)
            && (search_queue_key.service_id0 == p_key->service_id0))
        {
            search_queue_key.valid0 = FALSE;
            search_queue_key.channel_id0 = 0;
            search_queue_key.dest_chip_id0 = 0;
            search_queue_key.dest_queue0 = 0;
            search_queue_key.hash_type0 = 0;
            search_queue_key.mcast0 = 0;
            search_queue_key.queue_base0 = 0;
            search_queue_key.sgmac0 = 0;
            search_queue_key.service_id0 = 0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Del queue hash key0 indexed 0x%X!\n", table_index);
        }
        else if ((search_queue_key.valid1)
                 && (search_queue_key.channel_id1 == p_key->channel_id0)
                 && (search_queue_key.dest_chip_id1 == p_key->dest_chip_id0)
                 && (search_queue_key.dest_queue1 == p_key->dest_queue0)
                 && (search_queue_key.hash_type1 == p_key->hash_type0)
                 && (search_queue_key.mcast1 == p_key->mcast0)
                 && (search_queue_key.sgmac1 == p_key->sgmac0)
                 && (search_queue_key.service_id1 == p_key->service_id0))
        {
            search_queue_key.valid1 = FALSE;
            search_queue_key.channel_id1 = 0;
            search_queue_key.dest_chip_id1 = 0;
            search_queue_key.dest_queue1 = 0;
            search_queue_key.hash_type1 = 0;
            search_queue_key.mcast1 = 0;
            search_queue_key.queue_base1 = 0;
            search_queue_key.sgmac1 = 0;
            search_queue_key.service_id1 = 0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Del queue hash key1 indexed 0x%X.\n", table_index);
        }
        else if ((search_queue_key.valid2)
                 && (search_queue_key.channel_id2 == p_key->channel_id0)
                 && (search_queue_key.dest_chip_id2 == p_key->dest_chip_id0)
                 && (search_queue_key.dest_queue2 == p_key->dest_queue0)
                 && (search_queue_key.hash_type2 == p_key->hash_type0)
                 && (search_queue_key.mcast2 == p_key->mcast0)
                 && (search_queue_key.sgmac2 == p_key->sgmac0)
                 && (search_queue_key.service_id2 == p_key->service_id0))
        {
            search_queue_key.valid2 = FALSE;
            search_queue_key.channel_id2 = 0;
            search_queue_key.dest_chip_id2 = 0;
            search_queue_key.dest_queue2 = 0;
            search_queue_key.hash_type2 = 0;
            search_queue_key.mcast2 = 0;
            search_queue_key.queue_base2 = 0;
            search_queue_key.sgmac2 = 0;
            search_queue_key.service_id2 = 0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Del queue hash key2 indexed 0x%X.\n", table_index);
        }
        else if ((search_queue_key.valid3)
                 && (search_queue_key.channel_id3 == p_key->channel_id0)
                 && (search_queue_key.dest_chip_id3 == p_key->dest_chip_id0)
                 && (search_queue_key.dest_queue3 == p_key->dest_queue0)
                 && (search_queue_key.hash_type3 == p_key->hash_type0)
                 && (search_queue_key.mcast3 == p_key->mcast0)
                 && (search_queue_key.sgmac3 == p_key->sgmac0)
                 && (search_queue_key.service_id3 == p_key->service_id0))
        {
            search_queue_key.valid3 = FALSE;
            search_queue_key.channel_id3 = 0;
            search_queue_key.dest_chip_id3 = 0;
            search_queue_key.dest_queue3 = 0;
            search_queue_key.hash_type3 = 0;
            search_queue_key.mcast3 = 0;
            search_queue_key.queue_base3 = 0;
            search_queue_key.sgmac3 = 0;
            search_queue_key.service_id3 = 0;

            cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, table_index, cmd, &search_queue_key));
            MODEL_HASH_DEBUG_INFO("Del queue hash key3 indexed 0x%X.\n", table_index);
        }

#if (DRVIER_WORK_PLATFORM == 1)
        if ((!search_queue_key.valid0) && (!search_queue_key.valid1)
            && (!search_queue_key.valid2) && (!search_queue_key.valid3))
        {
            uint8 chipid_base = 0;
            DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
            drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, DsQueueHashKey_t, lookup_result.key_index);
        }

#endif
    }
    else if (lookup_result.free)
    {
        MODEL_HASH_DEBUG_INFO("The hash key isn't exist.\n");
    }
    else
    {
        _drv_greatbelt_model_delete_queue_key_cam(chip_id, p_queue_key);
        MODEL_HASH_DEBUG_INFO("drv_gb_io_api.drv_hash_key_get_index is NULL!");
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_add_userid_key(uint32 chip_id, userid_hash_type_t hash_type, void* p_userid_key)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    uint32 lookup_key_index = 0;
    uint32 cmd = 0;
    uint32 tbl_id = MaxTblId_t;
    uint32 key_size = 0;
    uint32 ad_index = 0;

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    lookup_info.hash_module = HASH_MODULE_USERID;
    lookup_info.hash_type = hash_type;
    lookup_info.p_ds_key = p_userid_key;
    lookup_info.chip_id = chip_id;

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));
    lookup_key_index = lookup_result.key_index;

    if (lookup_result.free)
    {
        tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_USERID, hash_type);
        if (MaxTblId_t == tbl_id)
        {
            return DRV_E_INVALID_HASH_TYPE;
        }

        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_ad_index(&lookup_info, &ad_index));

        MODEL_HASH_DEBUG_INFO("Add UserId hash key: table-id = 0x%X, key-index = 0x%X, ad-index = 0x%X.\n",
                              tbl_id, lookup_key_index, ad_index);

        key_size = TABLE_ENTRY_SIZE(tbl_id);

        if (DRV_BYTES_PER_ENTRY == key_size)
        {
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index, cmd, p_userid_key));
        }
        else if ((DRV_BYTES_PER_ENTRY * 2) == key_size)
        {
            /* All user id hash key use share memory */
            cmd = DRV_IOW(DsUserIdMacHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index, cmd, p_userid_key));
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index + 1, cmd, (uint8*)p_userid_key + sizeof(ds_3word_hash_key_t)));
        }
    }
    else if (lookup_result.valid)
    {
        MODEL_HASH_DEBUG_INFO("The key already exists.\n");
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("Add UserId hash key failed because of conflict.\n");
        return DRV_E_HASH_CONFLICT;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_delete_userid_key(uint32 chip_id, userid_hash_type_t hash_type, void* p_userid_key)
{

    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    uint32 cmd = 0;
    uint32 lookup_key_index = 0;
    uint32 tbl_id = MaxTblId_t;
    uint32 key_size = 0;
    ds_3word_hash_key_t ds_80bit_hash_key;

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&ds_80bit_hash_key, 0, sizeof(ds_3word_hash_key_t));

    lookup_info.chip_id = chip_id;
    lookup_info.hash_type = hash_type;
    lookup_info.p_ds_key = p_userid_key;
    lookup_info.hash_module = HASH_MODULE_USERID;

    DRV_PTR_VALID_CHECK(drv_gb_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_hash_lookup(&lookup_info, &lookup_result));

    if (lookup_result.valid)
    {
        lookup_key_index = lookup_result.key_index;

        tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(HASH_MODULE_USERID, hash_type);
        if (MaxTblId_t == tbl_id)
        {
            return DRV_E_INVALID_HASH_TYPE;
        }

        key_size = TABLE_ENTRY_SIZE(tbl_id);

        if (DRV_BYTES_PER_ENTRY == key_size)
        {
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index, cmd, &ds_80bit_hash_key));

#if (DRVIER_WORK_PLATFORM == 1)
            uint8 chipid_base = 0;
            DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
            drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, tbl_id, lookup_result.key_index);
#endif
        }
        else if ((DRV_BYTES_PER_ENTRY * 2) == key_size)
        {
            /* All user id hash key use share memory */
            cmd = DRV_IOW(DsUserIdMacHashKey_t, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index, cmd, &ds_80bit_hash_key));
            DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, lookup_key_index + 1, cmd, &ds_80bit_hash_key));

#if (DRVIER_WORK_PLATFORM == 1)
            uint8 chipid_base = 0;
            DRV_IF_ERROR_RETURN(drv_greatbelt_get_chipid_base(&chipid_base));
            drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, DsUserIdMacHashKey_t, lookup_result.key_index);
            drv_greatbelt_model_sram_tbl_clear_wbit(chip_id - chipid_base, DsUserIdMacHashKey_t, lookup_result.key_index + 1);
#endif
        }

        MODEL_HASH_DEBUG_INFO("Remove UserId hash key: table-id = 0x%X, key-index = 0x%X.\n", tbl_id, lookup_key_index);
    }
    else if (lookup_result.free)
    {
        /*no need to remove*/
        MODEL_HASH_DEBUG_INFO("The UserId hash key isn't exist.\n", lookup_result.key_index);
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_hash_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    if (HASH_MODULE_USERID == p_lookup_info->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_userid_lookup(p_lookup_info, p_lookup_result));
    }
    else if (HASH_MODULE_FIB == p_lookup_info->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_fib_lookup(p_lookup_info, p_lookup_result));
    }
    else if (HASH_MODULE_LPM == p_lookup_info->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_lpm_lookup(p_lookup_info, p_lookup_result));
    }
    else if (HASH_MODULE_QUEUE == p_lookup_info->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_queue_lookup(p_lookup_info, p_lookup_result));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_hash_key_add_by_key(hash_io_by_key_para_t* p_para)
{
    if (HASH_MODULE_FIB == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_add_fib_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_USERID == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_add_userid_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_LPM == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_add_lpm_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_QUEUE == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_add_queue_key(p_para->chip_id, p_para->key));
    }

    return DRV_E_NONE;
}


int32
drv_greatbelt_model_hash_key_del_by_key(hash_io_by_key_para_t* p_para)
{
    if (HASH_MODULE_FIB == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_delete_fib_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_USERID == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_delete_userid_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_LPM == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_delete_lpm_key(p_para->chip_id, p_para->hash_type, p_para->key));
    }
    else if (HASH_MODULE_QUEUE == p_para->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_delete_queue_key(p_para->chip_id, p_para->key));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_hash_key_add_by_index(hash_io_by_idx_para_t* p_para)
{
    uint32 cmd = 0;

    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    cmd = DRV_IOW(p_para->table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(p_para->chip_id, p_para->table_index, cmd, p_para->key));

    return DRV_E_NONE;
}

int32
drv_greatbelt_model_hash_key_del_by_index(hash_io_by_idx_para_t* p_para)
{
    uint32 cmd = 0;
    uint8 chip_id_offset = 0;
    ds_12word_hash_key_t ds_12word_hash_key;

    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    sal_memset(&ds_12word_hash_key, 0, sizeof(ds_12word_hash_key_t));

    cmd = DRV_IOW(p_para->table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(p_para->chip_id, p_para->table_index, cmd, &ds_12word_hash_key));
    chip_id_offset = p_para->chip_id - drv_gb_init_chip_info.drv_init_chipid_base;
    chip_id_offset = chip_id_offset;
#if (DRVIER_WORK_PLATFORM == 1)
    drv_greatbelt_model_sram_tbl_clear_wbit(chip_id_offset, p_para->table_id, p_para->table_index);
#endif
    return DRV_E_NONE;
}

int32
drv_greatbelt_model_hash_index_lkup_by_key(hash_io_by_key_para_t* p_para, hash_io_rslt_t* p_rslt)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;

    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_rslt);

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    lookup_info.chip_id = p_para->chip_id;
    lookup_info.hash_module = p_para->hash_module;
    lookup_info.hash_type = p_para->hash_type;
    lookup_info.p_ds_key = p_para->key;

    DRV_IF_ERROR_RETURN(drv_greatbelt_model_hash_lookup(&lookup_info, &lookup_result));

    p_rslt->ad_index = lookup_result.ad_index;
    p_rslt->conflict = lookup_result.conflict;
    p_rslt->free = lookup_result.free;
    p_rslt->key_index = lookup_result.key_index;
    p_rslt->valid = lookup_result.valid;

    return DRV_E_NONE;
}


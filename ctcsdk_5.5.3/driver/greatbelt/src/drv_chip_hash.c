/****************************************************************************
 * drv_model_hash.c  Provides driver chip hash handle function.
 *
 * Copyright (C) 2011 Centec Networks Inc.  All rights reserved.
 *
 ****************************************************************************/

#include "greatbelt/include/drv_chip_hash.h"
#include "greatbelt/include/drv_model_hash.h"

STATIC int32
_drv_greatbelt_chip_hash_mac_accelerate_request(uint8 chip_id, fib_hash_key_cpu_req_t* p_req, fib_hash_key_cpu_result_t* p_rslt)
{
    uint32 count = 0;
    uint32 trig = 0;
    uint32 cmd = 0;

    cmd = DRV_IOW(FibHashKeyCpuReq_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, p_req));

    trig  = 1;
    cmd = DRV_IOW(FibHashKeyCpuReq_t, FibHashKeyCpuReq_HashKeyCpuReqValid_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &trig));

    count = 0;
    p_rslt->hash_cpu_key_done = 0;

    while (0 == p_rslt->hash_cpu_key_done)
    {
        cmd = DRV_IOR(FibHashKeyCpuResult_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, p_rslt));

        if (!p_rslt->hash_cpu_key_done)
        {
            if((++count) > 100)
            {
                DRV_DBG_INFO("%s %d Error!!! fib_hash_key_cpu_result_t.hash_cpu_key_done = 0!\n", __FUNCTION__, __LINE__);
                break;
            }
            sal_task_sleep(50);
        }
    }

    return DRV_E_NONE;
}

STATIC void
_drv_greatbelt_chip_hash_mac_accelerate_key2req(fib_hash_key_cpu_req_t* p_req, ds_mac_hash_key_t* p_ds_mac_hash_key)
{
    p_req->cpu_key_vrf_id = p_ds_mac_hash_key->vsi_id;
    p_req->cpu_key_mac31_0 = p_ds_mac_hash_key->mapped_mac31_0;
    p_req->cpu_key_mac47_32 = p_ds_mac_hash_key->mapped_mac47_32;
    p_req->cpu_key_ds_ad_index = (p_ds_mac_hash_key->ds_ad_index14_3 << 3)
                               | (p_ds_mac_hash_key->ds_ad_index2_2 << 2)
                               | p_ds_mac_hash_key->ds_ad_index1_0;
}

STATIC int32
_drv_greatbelt_chip_hash_mac_accelerate_by_index(uint8 chip_id,
                                       ds_mac_hash_key_t* p_ds_mac_hash_key,
                                       fib_mac_accelerate_type_t fib_mac_accelerate_type,
                                       uint32 key_index)
{
    fib_hash_key_cpu_req_t fib_hash_key_cpu_req;
    fib_hash_key_cpu_result_t fib_hash_key_cpu_result;

    sal_memset(&fib_hash_key_cpu_req, 0, sizeof(fib_hash_key_cpu_req_t));
    sal_memset(&fib_hash_key_cpu_result, 0, sizeof(fib_hash_key_cpu_result_t));

    _drv_greatbelt_chip_hash_mac_accelerate_key2req(&fib_hash_key_cpu_req, p_ds_mac_hash_key);

    fib_hash_key_cpu_req.hash_key_cpu_req_type = fib_mac_accelerate_type;
    fib_hash_key_cpu_req.cpu_key_index = key_index;

    DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_request(chip_id, &fib_hash_key_cpu_req, &fib_hash_key_cpu_result));

    if (fib_hash_key_cpu_result.hash_cpu_key_done)
    {
        return DRV_E_NONE;
    }
    else
    {
        return DRV_E_FIB_MAC_ACCELERATE_FAIL;
    }
}

STATIC int32
_drv_greatbelt_chip_hash_mac_accelerate_by_key(uint8 chip_id,
                                     ds_mac_hash_key_t* p_ds_mac_hash_key,
                                     fib_mac_accelerate_type_t fib_mac_accelerate_type,
                                     lookup_result_t* p_lookup_result)
{
    fib_hash_key_cpu_req_t fib_hash_key_cpu_req;
    fib_hash_key_cpu_result_t fib_hash_key_cpu_result;

    sal_memset(&fib_hash_key_cpu_req, 0, sizeof(fib_hash_key_cpu_req_t));
    sal_memset(&fib_hash_key_cpu_result, 0, sizeof(fib_hash_key_cpu_result_t));

    _drv_greatbelt_chip_hash_mac_accelerate_key2req(&fib_hash_key_cpu_req, p_ds_mac_hash_key);

    fib_hash_key_cpu_req.hash_key_cpu_req_type = fib_mac_accelerate_type;

    DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_request(chip_id, &fib_hash_key_cpu_req, &fib_hash_key_cpu_result));

    if (fib_hash_key_cpu_result.hash_cpu_key_done)
    {
        if (NULL != p_lookup_result)
        {
            p_lookup_result->valid = fib_hash_key_cpu_result.hash_cpu_key_hit;
            p_lookup_result->free = fib_hash_key_cpu_result.hash_cpu_free_valid;
            p_lookup_result->conflict = (!p_lookup_result->valid) && (!p_lookup_result->free);
            p_lookup_result->key_index = fib_hash_key_cpu_result.hash_cpu_lu_index;
            p_lookup_result->ad_index = fib_hash_key_cpu_result.hash_cpu_ds_index;
        }

        return DRV_E_NONE;
    }
    else
    {
        return DRV_E_FIB_MAC_ACCELERATE_FAIL;
    }
}

int32
drv_greatbelt_chip_hash_lookup(lookup_info_t* p_lookup_info, lookup_result_t* p_lookup_result)
{
    if (HASH_MODULE_USERID == p_lookup_info->hash_module)
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_userid_lookup(p_lookup_info, p_lookup_result));
    }
    else if (HASH_MODULE_FIB == p_lookup_info->hash_module)
    {
        if (FIB_HASH_TYPE_MAC == p_lookup_info->hash_type)
        {
            DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_by_key(p_lookup_info->chip_id,
                                                                     p_lookup_info->p_ds_key,
                                                                     FIB_MAC_ACCELERATE_TYPE_LKUP,
                                                                     p_lookup_result));
        }
        else
        {
            DRV_IF_ERROR_RETURN(drv_greatbelt_fib_lookup(p_lookup_info, p_lookup_result));
        }
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
drv_greatbelt_chip_hash_key_add_by_key(hash_io_by_key_para_t* p_para)
{
    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_para->key);

    if ((HASH_MODULE_FIB == p_para->hash_module) && (FIB_HASH_TYPE_MAC == p_para->hash_type))
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_by_key(p_para->chip_id,
                                                                 (ds_mac_hash_key_t*)p_para->key,
                                                                 FIB_MAC_ACCELERATE_TYPE_ADD_BY_KEY,
                                                                 NULL));
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_hash_key_add_by_key(p_para));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_hash_key_del_by_key(hash_io_by_key_para_t* p_para)
{
    DRV_PTR_VALID_CHECK(p_para);
    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    if ((HASH_MODULE_FIB == p_para->hash_module) && (FIB_HASH_TYPE_MAC == p_para->hash_type))
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_by_key(p_para->chip_id,
                                                                   (ds_mac_hash_key_t*)p_para->key,
                                                                   FIB_MAC_ACCELERATE_TYPE_DEL_BY_KEY,
                                                                   NULL));
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_hash_key_del_by_key(p_para));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_hash_key_add_by_index(hash_io_by_idx_para_t* p_para)
{
    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_para->key);

    if (DsMacHashKey_t == p_para->table_id)
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_by_index(p_para->chip_id,
                                                                   (ds_mac_hash_key_t*)p_para->key,
                                                                    FIB_MAC_ACCELERATE_TYPE_ADD_BY_IDX,
                                                                    p_para->table_index));
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_hash_key_add_by_index(p_para));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_hash_key_del_by_index(hash_io_by_idx_para_t* p_para)
{
    ds_12word_hash_key_t ds_12word_hash_key;

    DRV_PTR_VALID_CHECK(p_para);
    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    sal_memset(&ds_12word_hash_key, 0, sizeof(ds_12word_hash_key_t));

    if (DsMacHashKey_t == p_para->table_id)
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_chip_hash_mac_accelerate_by_index(p_para->chip_id,
                                                                   (ds_mac_hash_key_t*)&ds_12word_hash_key,
                                                                   FIB_MAC_ACCELERATE_TYPE_DEL_BY_IDX,
                                                                   p_para->table_index));
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_model_hash_key_del_by_index(p_para));
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_chip_hash_index_lkup_by_key(hash_io_by_key_para_t* p_para, hash_io_rslt_t* p_rslt)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;

    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_para->key);
    DRV_PTR_VALID_CHECK(p_rslt);

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    lookup_info.chip_id = p_para->chip_id;
    lookup_info.hash_module = p_para->hash_module;
    lookup_info.hash_type = p_para->hash_type;
    lookup_info.p_ds_key = p_para->key;

    DRV_IF_ERROR_RETURN(drv_greatbelt_chip_hash_lookup(&lookup_info, &lookup_result));

    p_rslt->ad_index = lookup_result.ad_index;
    p_rslt->conflict = lookup_result.conflict;
    p_rslt->free = lookup_result.free;
    p_rslt->key_index = lookup_result.key_index;
    p_rslt->valid = lookup_result.valid;

    return DRV_E_NONE;
}


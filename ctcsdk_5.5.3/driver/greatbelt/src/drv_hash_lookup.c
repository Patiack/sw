/****************************************************************************
 * drv_hash_lookup.c  All error code Deinfines, include SDK error code.
 *
 * Copyright:    (c)2011 Centec Networks Inc.  All rights reserved.
 *
 * Revision:     V2.0.
 * Author:       ZhouW
 * Date:         2011-05-12.
 * Reason:       Create for GreatBelt v2.0.
 *
 * Modify History:
 * Revision:     V4.2.1
 * Revisor:      XuZx
 * Date:         2011-07-07
 * Reason:       Revise for GreatBelt v4.2.1
 *
 * Revision:     V4.5.1
 * Revisor:      XuZx
 * Date:         2011-07-21
 * Reason:       Revise for GreatBelt v4.5.1
 *
 * Revision:     V5.0.0
 * Revisor:      XuZx
 * Date:         2011-11-11
 * Reason:       Revise for GreatBelt v5.0.0
*****************************************************************************/
#include "greatbelt/include/drv_lib.h"

extern uint8 drv_greatbelt_get_host_type(uint8 lchip);

static INLINE uint16
MAKE_UINT16(uint8 hb, uint8 lb)
{
    return ((uint16)(
                (((uint16)hb) << 8) |
                (((uint16)lb) << 0)));
}

static INLINE uint32
MAKE_UINT32(uint8 b3, uint8 b2, uint8 b1, uint8 b0)
{
    return ((uint32)(
                (((uint32)b3) << 24) |
                (((uint32)b2) << 16) |
                (((uint32)b1) << 8) |
                (((uint32)b0) << 0)));
}

uint32 gb_hash_debug_on = FALSE;

void
drv_greatbelt_hash_lookup_add_field(uint32 field, uint16 field_len, uint16* p_field_offset, uint8* p_hash_key)
{
    uint8   start_byte_index = 0;
    uint8   offset_in_byte = (*p_field_offset) % 8;
    int16   remain_bits = field_len;
    int16   remain_bytes = 8;
    uint64  field64 = field;

    start_byte_index = (*p_field_offset) / 8;
    offset_in_byte = (*p_field_offset) % 8;

    field64 = field64 << (64 - field_len);
    field64 = field64 >> offset_in_byte;
    remain_bits += offset_in_byte;

    do
    {
        p_hash_key[start_byte_index++] |= ((field64 >> ((remain_bytes - 1) * 8)) & 0xFF);
        remain_bits -= 8;
        remain_bytes--;
    }
    while (remain_bits > 0);

    *p_field_offset += field_len;
}

#if DRV_HASH_DEBUG_ON
STATIC void
_drv_greatbelt_hash_lookup_shift_key(uint8* p_hash_key, uint8 bits_num)
{
    uint8 i;
    uint8 byte_num = (bits_num / 8) + (((bits_num % 8) == 0) ? 0 : 1);
    uint8 shift_bit = (8 - (bits_num % 8));

    for (i = byte_num - 1; i > 0; i--)
    {
        if (0 != i)
        {
            p_hash_key[i] = (p_hash_key[i] >> shift_bit) | (p_hash_key[i - 1] << (8 - shift_bit));
        }
    }

    p_hash_key[i] = (p_hash_key[i] >> shift_bit);
}

#endif

STATIC void*
_drv_greatbelt_hash_lookup_get_key_property_info(hash_module_t hash_module, uint32 hash_type)
{
    uint8 valid = drv_greatbelt_hash_lookup_check_hash_type(hash_module, hash_type);

    if (valid)
    {
        return &p_hash_key_property[hash_module][hash_type];
    }
    else
    {
        return NULL;
    }
}

STATIC int32
_drv_greatbelt_hash_lookup_get_key_info(lookup_info_t* p_lookup_info, uint32* p_info, key_info_type_t key_info_type)
{
    hash_key_property_t* p_hash_key_property = NULL;
    uint32 ad_value = {0};
    uint32 result = 0;
    uint8 filed_index = 0;
    uint8 filed_len = 0;
    fields_t* p_field = NULL;
    fld_id_t* p_fld_id = NULL;
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;
    uint32 cmd = 0;

    sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl));
    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(p_lookup_info->chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

    p_hash_key_property = _drv_greatbelt_hash_lookup_get_key_property_info(p_lookup_info->hash_module, p_lookup_info->hash_type);

    if (!p_hash_key_property)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    if (KEY_INFO_AD == key_info_type)
    {
        p_fld_id = p_hash_key_property->ad_field;
    }
    else if (KEY_INFO_TYPE == key_info_type)
    {
        p_fld_id = p_hash_key_property->type_field;
    }
    else if (KEY_INFO_INDEX_MASK == key_info_type)
    {
        p_fld_id = p_hash_key_property->index_mask_field;
    }
    else if (KEY_INFO_VALID == key_info_type)
    {
        p_fld_id = p_hash_key_property->entry_valid_field;
    }

    while (p_fld_id && (MaxField_f != p_fld_id[filed_index]))
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_get_field(p_hash_key_property->table_id, p_fld_id[filed_index],
                                          (uint32*)p_lookup_info->p_ds_key, &ad_value));

        p_field = drv_greatbelt_find_field(p_hash_key_property->table_id, p_fld_id[filed_index]);

        if (filed_index && p_field)
        {
            filed_len = p_field->len;
        }

        result = (result << filed_len) | ad_value;

        filed_index++;
    }

    if (filed_index)
    {
        *p_info = result;
    }

    if ((KEY_INFO_AD == key_info_type)
        && (HASH_MODULE_FIB == p_lookup_info->hash_module)
        && (FIB_HASH_TYPE_MAC == p_lookup_info->hash_type)
        && (dynamic_ds_fdb_hash_ctl.ad_bits_type))
    {
        /* 13 bits VSI, 16 bits dsAdIndex */
        result = ((((ds_mac_hash_key_t*)p_lookup_info->p_ds_key)->vsi_id & 0x2000) << 2) | result;
        *p_info = result;
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_hash_lookup_convert_ds_endian(uint32 tbl_id, void* p_ds_key)
{
    uint8 word_index;
    uint8 word_num;
    uint32 key_size;
    ds_12word_hash_key_t ds_12word_hash_key;
    uint8 byte_order;

    DRV_PTR_VALID_CHECK(p_ds_key);

    byte_order = drv_greatbelt_get_host_type(0);

    if (HOST_LE == byte_order)
    {
        sal_memset(&ds_12word_hash_key, 0, sizeof(ds_12word_hash_key_t));

        key_size = TABLE_ENTRY_SIZE(tbl_id);
        word_num = key_size / DRV_BYTES_PER_WORD;

        if (key_size > sizeof(ds_12word_hash_key))
        {
            return DRV_E_EXCEED_MAX_SIZE;
        }

        sal_memcpy(&ds_12word_hash_key, p_ds_key, key_size);

        for (word_index = 0; word_index < word_num; word_index++)
        {
            ds_12word_hash_key.field[word_index] = sal_htonl(ds_12word_hash_key.field[word_index]);
        }

        sal_memcpy(p_ds_key, &ds_12word_hash_key, key_size);
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_hash_lookup_convert_xbits_key(uint32 tbl_id, void* p_src_key, uint8* p_dst_key)
{
    uint8 entry_index;
    uint8 entry_num;
    uint32 key_size;

    DRV_PTR_VALID_CHECK(p_src_key);
    DRV_PTR_VALID_CHECK(p_dst_key);

    key_size = TABLE_ENTRY_SIZE(tbl_id);

    entry_num = key_size / DRV_BYTES_PER_ENTRY;

    for (entry_index = 0; entry_index < entry_num; entry_index++)
    {
        sal_memcpy((uint8*)p_dst_key + (DRV_HASH_80BIT_KEY_LENGTH * entry_index),
                   (uint8*)((ds_3word_hash_key_t*)p_src_key + entry_index) + (DRV_BYTES_PER_WORD / 2),
                   DRV_HASH_80BIT_KEY_LENGTH);
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_greatbelt_hash_lookup_convert_key(lookup_info_t* p_lookup_info, uint8* p_hash_key)
{
    hash_key_property_t* p_hash_key_info = NULL;
    uint8 filed_index = 0;
    uint32 field_value = 0;
    fields_t* p_fields = NULL;
    uint16 field_offset = 0;
    ds_12word_hash_key_t ds_12word_hash_key;

#if DRV_HASH_DEBUG_ON
    uint32 queue_hash_key64 = 0;
    uint16 queue_hash_key63_48 = 0;
    uint16 queue_hash_key47_32 = 0;
    uint16 queue_hash_key31_16 = 0;
    uint16 queue_hash_key15_0 = 0;
    uint8  hash_key[DRV_HASH_160BIT_KEY_LENGTH * 2] = {0};
#endif
    p_hash_key_info = _drv_greatbelt_hash_lookup_get_key_property_info(p_lookup_info->hash_module, p_lookup_info->hash_type);

    if (p_hash_key_info)
    {
        if (HASH_MODULE_QUEUE == p_lookup_info->hash_module)
        {
            while (MaxField_f != p_hash_key_info->compare_field[filed_index])
            {
                DRV_IF_ERROR_RETURN(drv_greatbelt_get_field(p_hash_key_info->table_id,
                                                  p_hash_key_info->compare_field[filed_index],
                                                  (uint32*)p_lookup_info->p_ds_key, &field_value));

                p_fields = drv_greatbelt_find_field(p_hash_key_info->table_id, p_hash_key_info->compare_field[filed_index]);

                if (!p_fields)
                {
                    return DRV_E_INVALID_FLD;
                }

                drv_greatbelt_hash_lookup_add_field(field_value, p_fields->len, &field_offset, p_hash_key);
                filed_index++;
            }

#if DRV_HASH_DEBUG_ON
            sal_memcpy(hash_key, p_hash_key, (field_offset / 8) + (((field_offset % 8) == 0) ? 0 : 1));
            _drv_greatbelt_hash_lookup_shift_key(p_hash_key, DRV_HASH_QUEUE_KEY_BIT_LEN);
            queue_hash_key64 = p_hash_key[0] & 0x1;
            queue_hash_key63_48 = MAKE_UINT16(p_hash_key[1], p_hash_key[2]);
            queue_hash_key47_32 = MAKE_UINT16(p_hash_key[3], p_hash_key[4]);
            queue_hash_key31_16 = MAKE_UINT16(p_hash_key[5], p_hash_key[6]);
            queue_hash_key15_0 = MAKE_UINT16(p_hash_key[7], p_hash_key[8]);
            DRV_DBG_INFO("queue hash key %d'h%0X_%04X_%04X_%04X_%04X\n", field_offset, queue_hash_key64,
                       queue_hash_key63_48, queue_hash_key47_32, queue_hash_key31_16, queue_hash_key15_0);
#endif
        }
        else
        {
            sal_memset(&ds_12word_hash_key, 0, sizeof(ds_12word_hash_key_t));

            while (MaxField_f != p_hash_key_info->compare_field[filed_index])
            {
                if (drv_gb_io_api.drv_get_field)
                {
                    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_get_field(p_hash_key_info->table_id,
                                                                 p_hash_key_info->compare_field[filed_index],
                                                                 (uint32*)p_lookup_info->p_ds_key, &field_value));
                }
                else
                {
                    DRV_DBG_INFO("%% drv_gb_io_api.drv_greatbelt_get_field is NULL!!\n");
                    return DRV_E_INVALID_PTR;
                }

                if (drv_gb_io_api.drv_set_field)
                {
                    DRV_IF_ERROR_RETURN(drv_gb_io_api.drv_set_field(p_hash_key_info->table_id,
                                                                 p_hash_key_info->compare_field[filed_index],
                                                                 (uint32*)&ds_12word_hash_key, field_value));
                }
                else
                {
                    DRV_DBG_INFO("%% drv_gb_io_api.drv_greatbelt_set_field is NULL!!\n");
                    return DRV_E_INVALID_PTR;
                }

                filed_index++;
            }

            DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_convert_ds_endian(p_hash_key_info->table_id, &ds_12word_hash_key));
            DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_convert_xbits_key(p_hash_key_info->table_id, &ds_12word_hash_key, p_hash_key));
        }

        return DRV_E_NONE;
    }
    else
    {
        return DRV_E_INVALID_PTR;
    }
}

uint32
drv_greatbelt_hash_lookup_check_hash_type(hash_module_t hash_module, uint32 hash_type)
{
    switch (hash_module)
    {
    case HASH_MODULE_USERID:
        if (hash_type >= USERID_HASH_TYPE_NUM)
        {
            return FALSE;
        }

        break;

    case HASH_MODULE_FIB:
        if (hash_type >= FIB_HASH_TYPE_NUM)
        {
            return FALSE;
        }

        break;

    case HASH_MODULE_LPM:
        if (hash_type >= LPM_HASH_TYPE_NUM)
        {
            return FALSE;
        }

        break;

    case HASH_MODULE_QUEUE:
        if (QUEUE_KEY_TYPE_KEY != hash_type)
        {
            return FALSE;
        }

        break;

    default:
        return FALSE;
    }

    return TRUE;
}

uint32
drv_greatbelt_hash_lookup_key_match(uint8* src, uint8* dst, uint8* mask, uint8 byte_num)
{
    uint8 i;

    for (i = 0; i < byte_num; i++)
    {
        if ((src[i] & mask[i]) != (dst[i] & mask[i]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

tbls_id_t
drv_greatbelt_hash_lookup_get_key_table_id(hash_module_t hash_module, uint32 hash_type)
{
    uint8 valid = drv_greatbelt_hash_lookup_check_hash_type(hash_module, hash_type);

    if (valid)
    {
        return p_hash_key_property[hash_module][hash_type].table_id;
    }
    else
    {
        return MaxTblId_t;
    }
}

int32
drv_greatbelt_hash_lookup_get_key_mask(uint8 chip_id, hash_module_t hash_module, uint32 hash_type, uint8* p_ds_hash_mask)
{
    uint32 tbl_id = MaxTblId_t;
    uint32 cmd = 0;
    ds_3word_hash_key_t* p_ds_3word_hash_mask = NULL;
    ds_6word_hash_key_t ds_6word_hash_mask;
    uint8 valid = FALSE;
    uint8 entry_num = 0;
    uint8 entry_index = 0;
    dynamic_ds_fdb_hash_ctl_t dynamic_ds_fdb_hash_ctl;

    sal_memset(&dynamic_ds_fdb_hash_ctl, 0, sizeof(dynamic_ds_fdb_hash_ctl));
    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &dynamic_ds_fdb_hash_ctl));

    sal_memset(&ds_6word_hash_mask, 0, sizeof(ds_6word_hash_mask));
    valid = drv_greatbelt_hash_lookup_check_hash_type(hash_module, hash_type);

    if (valid)
    {
        tbl_id  = drv_greatbelt_hash_lookup_get_key_table_id(hash_module, hash_type);
        if (MaxTblId_t == tbl_id)
        {
            return DRV_E_INVALID_TBL;
        }

        if ((HASH_MODULE_USERID == hash_module) || (HASH_MODULE_FIB == hash_module) || (HASH_MODULE_LPM == hash_module))
        {
            if ((HASH_MODULE_FIB == hash_module) && (FIB_HASH_TYPE_MAC == hash_type) && dynamic_ds_fdb_hash_ctl.ad_bits_type)
            {
                ds_6word_hash_mask.field[0] = 6;
                ds_6word_hash_mask.field[1] = 0x1FFFFFFF;
                ds_6word_hash_mask.field[2] = 0xFFFFFFFF;
            }
            else
            {
                entry_num = TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;

                for (entry_index = 0; entry_index < entry_num; entry_index++)
                {
                    p_ds_3word_hash_mask = (ds_3word_hash_key_t*)(p_hash_key_property[hash_module][hash_type].key_cmp_mask[entry_index]);
                    sal_memcpy((uint8*)&ds_6word_hash_mask + (sizeof(ds_3word_hash_key_t) * entry_index),
                               p_ds_3word_hash_mask, sizeof(ds_3word_hash_key_t));
                }
            }

            /* XuZx note: before convert ds struct to to 80bits unit, if host is little endian change ds's endian first */
            DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_convert_ds_endian(tbl_id, &ds_6word_hash_mask));
            DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_convert_xbits_key(tbl_id, &ds_6word_hash_mask, p_ds_hash_mask));
        }
        else
        {
            sal_memset(&ds_6word_hash_mask, 0, sizeof(ds_6word_hash_mask));
            p_ds_3word_hash_mask = (ds_3word_hash_key_t*)(p_hash_key_property[hash_module][hash_type].key_cmp_mask);
            sal_memcpy(&ds_6word_hash_mask, p_ds_3word_hash_mask, sizeof(ds_3word_hash_key_t));
            /* XuZx note: do not need compress ds struct, so mask and ds's endian are the same. */
            sal_memcpy(p_ds_hash_mask, &ds_6word_hash_mask, sizeof(ds_3word_hash_key_t));
        }
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_get_key_ad_index(lookup_info_t* p_lookup_info, uint32* p_value)
{
    DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_get_key_info(p_lookup_info, p_value, KEY_INFO_AD));

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_get_key_index_mask(lookup_info_t* p_lookup_info, uint32* p_value)
{
    DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_get_key_info(p_lookup_info, p_value, KEY_INFO_INDEX_MASK));

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_get_key_entry_valid(lookup_info_t* p_lookup_info, uint32* p_value)
{
    DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_get_key_info(p_lookup_info, p_value, KEY_INFO_VALID));

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_get_key_type(lookup_info_t* p_lookup_info, uint32* p_value)
{
    DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_get_key_info(p_lookup_info, p_value, KEY_INFO_TYPE));

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_get_convert_key(lookup_info_t* p_lookup_info, uint8* p_hash_key)
{
    uint32 tbl_id = drv_greatbelt_hash_lookup_get_key_table_id(p_lookup_info->hash_module, p_lookup_info->hash_type);

    if (MaxTblId_t == tbl_id)
    {
        return DRV_E_INVALID_TBL;
    }

    if ((HASH_MODULE_USERID == p_lookup_info->hash_module) || (HASH_MODULE_FIB == p_lookup_info->hash_module)
        || (HASH_MODULE_LPM == p_lookup_info->hash_module) || (HASH_MODULE_QUEUE == p_lookup_info->hash_module))
    {
        DRV_IF_ERROR_RETURN(_drv_greatbelt_hash_lookup_convert_key(p_lookup_info, p_hash_key));
    }
    else
    {
        DRV_DBG_INFO("@@ Invalid Hash Module When convert hash key, hashModule = %d\n", p_lookup_info->hash_module);
        return DRV_E_INVALID_HASH_MODULE_TYPE;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_hash_lookup_check_key_mask(hash_module_t hash_module, uint8 hash_type)
{
    hash_key_property_t* p_hash_key_info = NULL;
    ds_6word_hash_key_t ds_6word_hash_key;
    ds_queue_hash_key_t ds_queue_hash_key;
    lookup_info_t lookup_info;
    uint8 src_mask[DRV_HASH_160BIT_KEY_LENGTH] = {0};
    uint8 dst_mask[DRV_HASH_160BIT_KEY_LENGTH] = {0};
    uint8 mask_length = 0;
    uint8 i;
    uint8 key_size = 0;
    uint32 table_id = MaxTblId_t;
    uint8 valid = FALSE;
    uint32 chip_id = 2;

    valid = drv_greatbelt_hash_lookup_check_hash_type(hash_module, hash_type);

    if (!valid)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    p_hash_key_info = _drv_greatbelt_hash_lookup_get_key_property_info(hash_module, hash_type);
    if (!p_hash_key_info)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    key_size = TABLE_ENTRY_SIZE(p_hash_key_info->table_id);

    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&ds_6word_hash_key, 0xFF, sizeof(ds_6word_hash_key_t));

    lookup_info.hash_module = hash_module;
    lookup_info.hash_type = hash_type;
    lookup_info.p_ds_key = &ds_6word_hash_key;

    DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_key_mask(chip_id, lookup_info.hash_module, lookup_info.hash_type, dst_mask));

    if (HASH_MODULE_QUEUE == hash_module)
    {
        sal_memset(&ds_queue_hash_key, 0xFF, sizeof(ds_queue_hash_key_t));
        ds_queue_hash_key.rsv_0 = 0;
        ds_queue_hash_key.rsv_1 = 0;
        ds_queue_hash_key.rsv_2 = 0;
        ds_queue_hash_key.rsv_3 = 0;
        ds_queue_hash_key.rsv_4 = 0;
        ds_queue_hash_key.rsv_5 = 0;
        ds_queue_hash_key.rsv_6 = 0;
        ds_queue_hash_key.rsv_7 = 0;
        ds_queue_hash_key.rsv_8 = 0;
        ds_queue_hash_key.rsv_9 = 0;
        ds_queue_hash_key.rsv_10 = 0;
        ds_queue_hash_key.rsv_11 = 0;
        ds_queue_hash_key.rsv_12 = 0;
        ds_queue_hash_key.rsv_13 = 0;
        ds_queue_hash_key.rsv_14 = 0;
        ds_queue_hash_key.rsv_15 = 0;
        ds_queue_hash_key.rsv_16 = 0;
        ds_queue_hash_key.rsv_17 = 0;
        ds_queue_hash_key.rsv_18 = 0;
        ds_queue_hash_key.rsv_19 = 0;
        ds_queue_hash_key.rsv_20 = 0;
        ds_queue_hash_key.rsv_21 = 0;
        ds_queue_hash_key.rsv_22 = 0;
        ds_queue_hash_key.rsv_23 = 0;
        ds_queue_hash_key.rsv_24 = 0;
        ds_queue_hash_key.rsv_25 = 0;
        ds_queue_hash_key.rsv_26 = 0;
        ds_queue_hash_key.rsv_27 = 0;
        ds_queue_hash_key.queue_base0 = 0;
        ds_queue_hash_key.queue_base1 = 0;
        ds_queue_hash_key.queue_base2 = 0;
        ds_queue_hash_key.queue_base3 = 0;

        sal_memcpy(src_mask, &ds_queue_hash_key, DRV_HASH_96BIT_KEY_LENGTH);
        mask_length = DRV_HASH_96BIT_KEY_LENGTH;
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_greatbelt_hash_lookup_get_convert_key(&lookup_info, src_mask));
        mask_length = (key_size / DRV_BYTES_PER_ENTRY) * DRV_HASH_80BIT_KEY_LENGTH;
    }

    for (i = 0; i < mask_length; i++)
    {
        if (src_mask[i] != dst_mask[i])
        {
            break;
        }
    }

    table_id = drv_greatbelt_hash_lookup_get_key_table_id(hash_module, hash_type);
    if (MaxTblId_t == table_id)
    {
        return DRV_E_INVALID_TBL;
    }

    if (i == mask_length)
    {
        DRV_DBG_INFO("table_id = 0x%X, hash module = 0x%X, hash type = 0x%X check pass.\n", table_id, hash_module, hash_type);
    }
    else
    {
        DRV_DBG_INFO("table_id = 0x%X, hash module = 0x%X, hash type = 0x%X check failed.\n", table_id, hash_module, hash_type);

        DRV_DBG_INFO("expect mask:\n");

        for (i = 0; i < mask_length; i++)
        {
            DRV_DBG_INFO("%02X ", src_mask[i]);
        }

        DRV_DBG_INFO("\ncurrnet mask:\n");

        for (i = 0; i < mask_length; i++)
        {
            DRV_DBG_INFO("%02X ", dst_mask[i]);
        }

        DRV_DBG_INFO("\n");
    }

    return DRV_E_NONE;
}


/**
 @file drv_model_io.c

 @date 2010-07-23

 @version v5.1

 The file contains all driver I/O interface realization
*/
#if (SDK_WORK_PLATFORM == 1)
#include "greatbelt/include/drv_model_io.h"
#include "sram_model.h"
#include "tcam_model.h"

#if 0
/* move to sram_model, named  sram_model_get_sw_address_by_tbl_id_and_index */
/* only used by static and dynamic table, not used by tcam key table */
int32
_drv_greatbelt_get_sw_address_by_tbl_id_and_index(uint8 chip_id_offset, tbls_id_t tbl_id,
                                        uint32 index, uint32* start_data_addr)
{
    uint32 sw_data_base = 0;
    uint8 blk_id = 0;

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("ERROR (_drv_greatbelt_get_sw_address_by_tbl_id_and_index): chip-0x%x, tbl-%d, tcam key call the function only for non tcam key.\n",
                     chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base, tbl_id);
        return DRV_E_INVALID_TBL;
    }

    if (drv_greatbelt_table_is_dynamic_table(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_MEMORY_BLOCK_NUM; blk_id++)
        {
            if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= DYNAMIC_START_INDEX(tbl_id, blk_id)) && (index <= DYNAMIC_END_INDEX(tbl_id, blk_id)))
            {
                sw_data_base = SRAM_DATA_BASE(chip_id_offset, tbl_id, blk_id);
                break;
            }
        }

        if (DYNAMIC_ACCESS_MODE(tbl_id) == DYNAMIC_8W_MODE)
        {
            if (index % 2)
            {
                DRV_DBG_INFO("ERROR!! get tbl_id %d index must be even! now index = %d\n", tbl_id, index);
                return DRV_E_INVALID_INDEX;
            }

            *start_data_addr = sw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * DRV_BYTES_PER_ENTRY;
        }
        else
        {
            *start_data_addr = sw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * TABLE_ENTRY_SIZE(tbl_id);
        }
    }
    else
    {
        sw_data_base = SRAM_SW_DATA_BASE(chip_id_offset, tbl_id);
        *start_data_addr = sw_data_base + index * TABLE_ENTRY_SIZE(tbl_id);
    }

    return DRV_E_NONE;
}

#endif

/**
 @brief sram table model set entry write bit
*/
void
drv_greatbelt_model_sram_tbl_set_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uintptr start_data_addr = 0;
    uint32 add_num = 0;
    uint8* wbit_start_addr_ptr = NULL;
    uint8 set_wbit_flag = TRUE, first_hit_flag = FALSE, first_hit_value = FALSE;

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
        DRV_DBG_INFO("%% ERROR! can't set table sbit for Tcam!\n");
        return;
    }

    if (drv_greatbelt_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            DRV_DBG_INFO("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            DRV_DBG_INFO("%% ERROR! dynamic table is not allocated!\n");
            return;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
            (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset)) / DRV_BYTES_PER_ENTRY;

        /* check if the space wbit is same */
        for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
        {
            if (!first_hit_flag)
            {
                first_hit_flag = TRUE;
                first_hit_value = *(wbit_start_addr_ptr + add_num);
            }
            else
            {
                /* if there is different value of first wbit, set flag to indicate can't set wbit */
                if (first_hit_value != *(wbit_start_addr_ptr + add_num))
                {
                    set_wbit_flag = FALSE;
                    break;
                }
            }
        }

        if (set_wbit_flag)
        {
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
            {
                *(wbit_start_addr_ptr + add_num) = TRUE;
            }
        }
        else
        {
#if 0
            /* if can't set wbit ,output the error info */
            sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            sal_printf("%% ERROR! duplicate set wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
#endif
            return;
        }

        set_wbit_flag = TRUE;
        first_hit_flag = FALSE;
        first_hit_value = FALSE;

    }
    else
    {
        /* tcam ad wbit option is different from static table, 8W should clear 2 wbit,
           because sram allocated memory for tcam ad is per 4W, so 8W will use 2 unit space*/
        if (sram_model_is_tcam_ad_table(chip_id_offset, tbl_id))
        {
            /* check if the space wbit is same */
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
            {
                if (!first_hit_flag)
                {
                    first_hit_flag = TRUE;
                    first_hit_value = *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                        index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num);
                }
                else
                {
                    /* if there is different value of first wbit, set flag to indicate can't set wbit */
                    if (first_hit_value != *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                             index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num))
                    {
                        set_wbit_flag = FALSE;
                        break;
                    }
                }
            }

            if (set_wbit_flag)
            {
                for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
                {
                    *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                      index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num) = TRUE;
                }
            }
            else
            {
#if 0
                /* if can't set wbit ,output the error info */
                sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
                sal_printf("%% ERROR! duplicate set wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
#endif
                return;
            }

            set_wbit_flag = TRUE;
            first_hit_flag = FALSE;
            first_hit_value = FALSE;

        }
        else
        {
            *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) + index) = TRUE;
        }
    }

    return;
}

/**
 @brief sram table model clear entry write bit
*/
void
drv_greatbelt_model_sram_tbl_clear_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uintptr start_data_addr = 0;
    uint32 add_num = 0;
    uint8* wbit_start_addr_ptr = NULL;
    uint8 set_wbit_flag = TRUE, first_hit_flag = FALSE, first_hit_value = FALSE;

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("%% ERROR! clear table %d index %d wbit fail!!\n", tbl_id, index);
        DRV_DBG_INFO("%% ERROR! can't clear table sbit for Tcam!\n");
        return;
    }

    if (drv_greatbelt_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            DRV_DBG_INFO("%% ERROR! clear table %d index %d wbit fail!!\n", tbl_id, index);
            DRV_DBG_INFO("%% ERROR! dynamic table is not allocated!\n");
            return;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
            (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset)) / DRV_BYTES_PER_ENTRY;

        /* check if the space wbit is same */
        for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
        {
            if (!first_hit_flag)
            {
                first_hit_flag = TRUE;
                first_hit_value = *(wbit_start_addr_ptr + add_num);
            }
            else
            {
                /* if there is different value of first wbit, set flag to indicate can't set wbit */
                if (first_hit_value != *(wbit_start_addr_ptr + add_num))
                {
                    set_wbit_flag = FALSE;
                    break;
                }
            }
        }

        if (set_wbit_flag)
        {
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
            {
                *(wbit_start_addr_ptr + add_num) = FALSE;
            }
        }
        else
        {
            /* if can't clear wbit ,output the error info */
            DRV_DBG_INFO("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            DRV_DBG_INFO("%% ERROR! duplicate clear wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
            return;
        }

        set_wbit_flag = TRUE;
        first_hit_flag = FALSE;
        first_hit_value = FALSE;

    }
    else
    {
        /* tcam ad wbit option is different from static table, 8W should clear 2 wbit,
           because sram allocated memory for tcam ad is per 4W, so 8W will use 2 unit space*/
        if (sram_model_is_tcam_ad_table(chip_id_offset, tbl_id))
        {
            /* check if the space wbit is same */
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
            {
                if (!first_hit_flag)
                {
                    first_hit_flag = TRUE;
                    first_hit_value = *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                        index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num);
                }
                else
                {
                    /* if there is different value of first wbit, set flag to indicate can't set wbit */
                    if (first_hit_value != *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                             index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num))
                    {
                        set_wbit_flag = FALSE;
                        break;
                    }
                }
            }

            if (set_wbit_flag)
            {
                for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY); add_num++)
                {
                    *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                      index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY) + add_num) = FALSE;
                }
            }
            else
            {
                /* if can't set wbit ,output the error info */
                DRV_DBG_INFO("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
                DRV_DBG_INFO("%% ERROR! duplicate clear wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
                return;
            }

            set_wbit_flag = TRUE;
            first_hit_flag = FALSE;
            first_hit_value = FALSE;

        }
        else
        {
            *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) + index) = FALSE;
        }
    }

    return;
}

/**
 @brief sram table model get entry write bit
*/
uint8
drv_greatbelt_model_sram_tbl_get_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uintptr start_data_addr = 0;
    uint8* wbit_start_addr_ptr = NULL;

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("%% ERROR! get table %d index %d wbit fail!!\n", tbl_id, index);
        DRV_DBG_INFO("%% ERROR! can't get table sbit for Tcam!\n");
        return FALSE;
    }

    if (drv_greatbelt_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            DRV_DBG_INFO("%% ERROR! get table %d index %d wbit fail!!\n", tbl_id, index);
            DRV_DBG_INFO("%% ERROR! dynamic table is not allocated!\n");
            return FALSE;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
            (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset)) / DRV_BYTES_PER_ENTRY;
        return *wbit_start_addr_ptr;

    }
    else
    {
        /* tcam ad wbit option is different from static table, 8W should clear 2 wbit,
           because sram allocated memory for tcam ad is per 4W, so 8W will use 2 unit space*/
        if (sram_model_is_tcam_ad_table(chip_id_offset, tbl_id))
        {
            return *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                     index * (TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY));

        }
        else
        {
            return *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) + index);
        }
    }

    return FALSE;
}

/**
 @brief Real sram direct write operation I/O
*/
int32
drv_greatbelt_model_write_sram_entry(uint8 chip_id_offset, uintptr addr,
                           uint32* data, int32 len)
{
    DRV_IF_ERROR_RETURN(sram_model_write(chip_id_offset, addr, data, len));
    return DRV_E_NONE;
}

/**
 @brief Real sram direct read operation I/O
*/
int32
drv_greatbelt_model_read_sram_entry(uint8 chip_id_offset, uintptr addr,
                          uint32* data, int32 len)
{
    DRV_IF_ERROR_RETURN(sram_model_read(chip_id_offset, addr, data, len));
    return DRV_E_NONE;
}

/**
 @brief The function write table data to a sram memory location
*/
int32
drv_greatbelt_model_sram_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    uintptr start_data_addr = 0;
    uint32  entry_size = 0;
    uint32 max_index_num = 0;

    DRV_TBL_EMPTY_CHECK(tbl_id);
    DRV_PTR_VALID_CHECK(data);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    entry_size = TABLE_ENTRY_SIZE(tbl_id);
    max_index_num = TABLE_MAX_INDEX(tbl_id);

    if (max_index_num <= index)
    {
        DRV_DBG_INFO("\nERROR (drv_greatbelt_model_sram_tbl_write): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base, tbl_id, index, max_index_num);
        return DRV_E_INVALID_TBL;
    }

    DRV_IF_ERROR_RETURN(sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr));

    DRV_IF_ERROR_RETURN(drv_greatbelt_model_write_sram_entry(chip_id_offset, start_data_addr, data, entry_size));

    drv_greatbelt_model_sram_tbl_set_wbit(chip_id_offset, tbl_id, index);

    return DRV_E_NONE;
}

/**
 @brief write register field data to a memory location on memory model
*/
int32
drv_greatbelt_model_register_field_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 fld_id, uint32* data)
{
    uintptr start_data_addr = 0;
    fields_t* field = NULL;

    DRV_TBL_EMPTY_CHECK(tbl_id);
    DRV_PTR_VALID_CHECK(data);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    DRV_IF_ERROR_RETURN(sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, 0, &start_data_addr));

    field = drv_greatbelt_find_field(tbl_id, fld_id);

    DRV_IF_ERROR_RETURN(drv_greatbelt_model_write_sram_entry(chip_id_offset, (start_data_addr + field->word_offset*DRV_BYTES_PER_WORD), (data+field->word_offset), DRV_BYTES_PER_WORD));

    drv_greatbelt_model_sram_tbl_set_wbit(chip_id_offset, tbl_id, 0);

    return DRV_E_NONE;
}

/**
 @brief The function read table data from a sram memory location
*/
int32
drv_greatbelt_model_sram_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    uintptr start_data_addr = 0;
    uint32 entry_size = 0;
    uint32 max_index_num = 0;

    DRV_TBL_EMPTY_CHECK(tbl_id);
    DRV_PTR_VALID_CHECK(data);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    entry_size = TABLE_ENTRY_SIZE(tbl_id);
    max_index_num = TABLE_MAX_INDEX(tbl_id);

    if (max_index_num <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_model_sram_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base, tbl_id, index, max_index_num);
        return DRV_E_INVALID_TBL;
    }

    DRV_IF_ERROR_RETURN(sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr));

    DRV_IF_ERROR_RETURN(drv_greatbelt_model_read_sram_entry(chip_id_offset, start_data_addr, data, entry_size));

    return DRV_E_NONE;
}

/**
 @brief write tcam interface (include operate model and real tcam)
*/
int32
drv_greatbelt_model_tcam_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 tcam_asic_data_base = 0, tcam_asic_mask_base = 0;
    uint32 entry_num_each_idx = 0, entry_idx = 0, offset = 0;
    uintptr tcam_model_data_base = 0, tcam_model_mask_base = 0;
    uintptr data_addr = 0, mask_addr = 0;
    uint32 max_int_tcam_data_base_tmp = 0;
    uint32 max_ext_tcam_data_base_tmp = 0;
    uint32 max_int_lpm_tcam_data_base_tmp = 0;
    uint32* model_wbit_base = NULL;
    uint32 entry_num_offset = 0;
    uint8  tcam_type = 0;
    uint8 entry_num_half = 0;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When write operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_model_tcam_tbl_write): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }

    max_int_tcam_data_base_tmp = tcam_info.max_int_tcam_data_base;
    max_ext_tcam_data_base_tmp = tcam_info.max_ext_tcam_data_base;
    max_int_lpm_tcam_data_base_tmp = tcam_info.max_int_lpm_tcam_data_base;

    if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (TABLE_DATA_BASE(tbl_id) < max_int_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_INT_TCAM_KEY_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.int_tcam_wbit[chip_id_offset];
        tcam_type = 1;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_EXT_TCAM_KEY_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_ext_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_EXT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_EXT_TCAM_KEY_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.ext_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.ext_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.ext_tcam_wbit[chip_id_offset];
        tcam_type = 2;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_int_lpm_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_LPM_TCAM_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_INT_LPM_TCAM_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_lpm_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_lpm_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.int_lpm_tcam_wbit[chip_id_offset];
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Tcam key id 0x%x 's hwdatabase is not correct when write!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

    /* the key's each index includes 80bit entry number */
    entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;

    offset = (TABLE_ENTRY_SIZE(tbl_id) - TCAM_KEY_SIZE(tbl_id)) / DRV_BYTES_PER_WORD; /* wordOffset */

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; entry_idx++)
    {
        if (3 == tcam_type)
        {
            data_addr = tcam_model_data_base
                + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;

            mask_addr = tcam_model_mask_base
                + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;

            entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                + (index + entry_idx);
        }
        else
        {
            if (8 == entry_num_each_idx)
            {
                entry_num_half = 4;
            }
            else
            {
                entry_num_half = entry_num_each_idx;
            }

            if (!IS_BIT_SET(entry_idx, 2))
            {
                data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;

                mask_addr = tcam_model_mask_base
                    + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;

                entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                    + (index * entry_num_half + entry_idx);
            }
            else
            {
                data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;

                mask_addr = tcam_model_mask_base
                    + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;

                entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half);
            }
        }

        DRV_IF_ERROR_RETURN(tcam_model_write(chip_id_offset, data_addr,
                                             data + entry_idx * DRV_WORDS_PER_ENTRY, DRV_BYTES_PER_ENTRY));

        DRV_IF_ERROR_RETURN(tcam_model_write(chip_id_offset, mask_addr,
                                             mask + entry_idx * DRV_WORDS_PER_ENTRY, DRV_BYTES_PER_ENTRY));

        if (!IS_BIT_SET(model_wbit_base[entry_num_offset / DRV_BITS_PER_WORD], entry_num_offset % DRV_BITS_PER_WORD))
        {
            SET_BIT(model_wbit_base[entry_num_offset / DRV_BITS_PER_WORD], entry_num_offset % DRV_BITS_PER_WORD);
        }
    }

    return DRV_E_NONE;
}

/**
 @brief read tcam interface (include operate model and real tcam)
*/
int32
drv_greatbelt_model_tcam_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 tcam_asic_data_base = 0, tcam_asic_mask_base = 0;
    uint32 entry_num_each_idx = 0, entry_idx = 0, offset = 0;
    uintptr tcam_model_data_base = 0, tcam_model_mask_base = 0;
    uintptr data_addr = 0, mask_addr = 0;
    uint32 max_int_tcam_data_base_tmp = 0;
    uint32 max_ext_tcam_data_base_tmp = 0;
    uint32 max_int_lpm_tcam_data_base_tmp = 0;
    uint8  tcam_type = 0;
    uint8 entry_num_half = 0;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When tcam key read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_greatbelt_model_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id_offset + drv_gb_init_chip_info.drv_init_chipid_base, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }

    max_int_tcam_data_base_tmp = tcam_info.max_int_tcam_data_base;
    max_ext_tcam_data_base_tmp = tcam_info.max_ext_tcam_data_base;
    max_int_lpm_tcam_data_base_tmp = tcam_info.max_int_lpm_tcam_data_base;

    if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (TABLE_DATA_BASE(tbl_id) < max_int_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_INT_TCAM_KEY_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_tcam_mask_base[chip_id_offset];
        tcam_type = 1;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_EXT_TCAM_KEY_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_ext_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_EXT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_EXT_TCAM_KEY_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.ext_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.ext_tcam_mask_base[chip_id_offset];
        tcam_type = 2;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_int_lpm_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_LPM_TCAM_DATA_ASIC_BASE;
        tcam_asic_mask_base = DRV_INT_LPM_TCAM_MASK_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_lpm_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_lpm_tcam_mask_base[chip_id_offset];
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Tcam key id 0x%x 's hwdatabase is not correct when read!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

    /* the key's each index includes 80bit entry number */
    entry_num_each_idx = (TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY);
    offset = (TABLE_ENTRY_SIZE(tbl_id) - TCAM_KEY_SIZE(tbl_id)) / DRV_BYTES_PER_WORD;

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; entry_idx++)
    {
        if (3 == tcam_type)
        {
            data_addr = tcam_model_data_base
                + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;

            mask_addr = tcam_model_mask_base
                + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;
        }
        else
        {
            if (8 == entry_num_each_idx)
            {
                entry_num_half = 4;
            }
            else
            {
                entry_num_half = entry_num_each_idx;
            }

            if (!IS_BIT_SET(entry_idx, 2))
            { /*0~319bit*/
                data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;

                mask_addr = tcam_model_mask_base
                    + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;
            }
            else
            { /*320~639bit*/
                data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;

                mask_addr = tcam_model_mask_base
                    + (TCAM_MASK_BASE(tbl_id) - tcam_asic_mask_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;
            }
        }

        DRV_IF_ERROR_RETURN(tcam_model_read(chip_id_offset, data_addr,
                                            data + entry_idx * DRV_WORDS_PER_ENTRY, DRV_BYTES_PER_ENTRY));

        DRV_IF_ERROR_RETURN(tcam_model_read(chip_id_offset, mask_addr,
                                            mask + entry_idx * DRV_WORDS_PER_ENTRY, DRV_BYTES_PER_ENTRY));
    }

    return DRV_E_NONE;
}

/**
 @brief remove tcam entry interface (include operate model and real tcam)
*/
int32
drv_greatbelt_model_tcam_tbl_remove(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uint32 tcam_asic_data_base = 0;
    uint32 entry_idx = 0, entry_num_each_idx = 0;
    uintptr tcam_model_data_base = 0, tcam_model_mask_base = 0;
    uintptr sw_data_addr = 0, sw_mask_addr = 0;
    uint32* model_wbit_base = NULL;
    uint32 entry_num_offset = 0;
    uint32 max_int_tcam_data_base_tmp = 0;
    uint32 max_ext_tcam_data_base_tmp = 0;
    uint32 max_int_lpm_tcam_data_base_tmp = 0;
    uint8  tcam_type = 0;
    uint8 entry_num_half = 0;

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("@@ ERROR! When remove operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                     tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    max_int_tcam_data_base_tmp = tcam_info.max_int_tcam_data_base;
    max_ext_tcam_data_base_tmp = tcam_info.max_ext_tcam_data_base;
    max_int_lpm_tcam_data_base_tmp = tcam_info.max_int_lpm_tcam_data_base;

    if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (TABLE_DATA_BASE(tbl_id) < max_int_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.int_tcam_wbit[chip_id_offset];
        tcam_type = 1;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_EXT_TCAM_KEY_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_ext_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_EXT_TCAM_KEY_DATA_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.ext_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.ext_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.ext_tcam_wbit[chip_id_offset];
        tcam_type = 2;
    }
    else if ((TABLE_DATA_BASE(tbl_id) >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (TABLE_DATA_BASE(tbl_id) < max_int_lpm_tcam_data_base_tmp))
    {
        tcam_asic_data_base = DRV_INT_LPM_TCAM_DATA_ASIC_BASE;
        tcam_model_data_base = (uintptr)tcam_info.int_lpm_tcam_data_base[chip_id_offset];
        tcam_model_mask_base = (uintptr)tcam_info.int_lpm_tcam_mask_base[chip_id_offset];
        model_wbit_base = tcam_info.int_lpm_tcam_wbit[chip_id_offset];
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Tcam key id 0x%x 's database is not correct when remove!\n", tbl_id);
        return DRV_E_TCAM_KEY_DATA_ADDRESS;
    }

    /* the key's each index includes 80bit entry number */
    entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;

    /* According to keysize to clear tcam wbit(one bit each 80 bits tcam entry)*/
    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        if (3 == tcam_type)
        {
            sw_data_addr = tcam_model_data_base
                + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;

            sw_mask_addr = tcam_model_mask_base
                + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                + (index + entry_idx) * DRV_BYTES_PER_ENTRY;
            entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                + (index + entry_idx);
        }
        else
        {
            if (8 == entry_num_each_idx)
            {
                entry_num_half = 4;
            }
            else
            {
                entry_num_half = entry_num_each_idx;
            }

            /* clear Tcam memory */
            if (!IS_BIT_SET(entry_idx, 2))
            {
                sw_data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;

                sw_mask_addr = tcam_model_mask_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + entry_idx) * DRV_BYTES_PER_ENTRY;
                entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                    + (index * entry_num_half + entry_idx);
            }
            else
            {
                sw_data_addr = tcam_model_data_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;

                sw_mask_addr = tcam_model_mask_base
                    + (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base)
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half) * DRV_BYTES_PER_ENTRY;
                entry_num_offset = (TABLE_DATA_BASE(tbl_id) - tcam_asic_data_base) / DRV_BYTES_PER_ENTRY
                    + (index * entry_num_half + DRV_ACL_TCAM_640_OFFSET_IDX + entry_idx - entry_num_half);
            }
        }

        DRV_IF_ERROR_RETURN(tcam_model_remove(chip_id_offset, sw_data_addr));

        DRV_IF_ERROR_RETURN(tcam_model_remove(chip_id_offset, sw_mask_addr));

        if (IS_BIT_SET(model_wbit_base[entry_num_offset / DRV_BITS_PER_WORD], entry_num_offset % DRV_BITS_PER_WORD))
        {
            CLEAR_BIT(model_wbit_base[entry_num_offset / DRV_BITS_PER_WORD], entry_num_offset % DRV_BITS_PER_WORD);
        }
    }

    return DRV_E_NONE;
}

#if 0
int32
drv_greatbelt_model_hash_vbit_operation(uint8 hash_type, uint8 hash_module, uint32 index, hash_op_type_t op_type)
{
    uint8* vbit_array_temp = NULL;

    uint8* vbit_array = NULL;
    uint8 valid = FALSE;

    valid = drv_hash_lookup_check_hash_type(hash_module, hash_type);

    if (!valid)
    {
        return DRV_E_INVALID_HASH_TYPE;
    }

    if (HASH_MODULE_LPM == hash_module)
    {
        switch (hash_type)
        {
        case LPM_HASH_TYPE_IPV4_16:
        case LPM_HASH_TYPE_IPV4_8:
        case LPM_HASH_TYPE_IPV6_32:
        case LPM_HASH_TYPE_IPV6_64:
        case LPM_HASH_TYPE_IPV4_XG:
        case LPM_HASH_TYPE_IPV4_SG:
            vbit_array = hash_valid.drv_lpm_hash_entry_valid;
            break;

        default:
            break;
        }
    }

    vbit_array_temp = vbit_array;

#if 0

    /* each bucket have 2 * 80 bits, sw use 12 Bytes to sim the 80 bits */
    switch (index % ((2 * 12) / key_size))
    {
    case 0:
        vbit_array_temp = vbit_array_left;
        break;

    case 1:
        if (key_size == 16)
        {
            vbit_array_temp = vbit_array_left;
        }
        else
        {
            vbit_array_temp = vbit_array_right;
        }

        break;

    case 2:
    case 3:
        vbit_array_temp = vbit_array_right;
        break;

    default:
        return DRV_E_INVALID_PARAM;
    }

#endif

    if (HASH_OP_TP_ADD_BY_KEY == op_type)
    {
        if (!IS_BIT_SET(vbit_array_temp[index / 8], index % 8))
        {
            SET_BIT(vbit_array_temp[index / 8], index % 8);
            /*DRV_DBG_INFO("$$$$$ write vbit addr: 0x%x\n", (uint32)(&vbit_array_temp[index/8])); */
            /*DRV_DBG_INFO("$$$$$ index: 0x%x\n", index);*/
        }
    }
    else
    {
        if (IS_BIT_SET(vbit_array_temp[index / 8], index % 8))
        {
            CLEAR_BIT(vbit_array_temp[index / 8], index % 8);
            /*DRV_DBG_INFO("$$$$$ clear vbit addr: 0x%x\n", (uint32)(&vbit_array_temp[index/8]));*/
        }
    }

    return DRV_E_NONE;
}

#endif

#endif


/**
 @file drv_model_io.c

 @date 2010-07-23

 @version v5.1

 The file contains all driver I/O interface realization
*/
#if (SDK_WORK_PLATFORM == 1)
#include "goldengate/include/drv_model_io.h"
#include "sram_model.h"
#include "tcam_model.h"


/**
 @brief sram table model set entry write bit
*/
void
drv_goldengate_model_sram_tbl_set_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uintptr start_data_addr = 0;
    uint32 add_num = 0;
    uint8 *wbit_start_addr_ptr = NULL;
    uint8 set_wbit_flag = TRUE, first_hit_flag = FALSE, first_hit_value = FALSE;
   return ;
    if (drv_goldengate_table_is_tcam_key(tbl_id)
        || drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
        sal_printf("%% ERROR! can't set table sbit for Tcam!\n");
        return;
    }

    /*
    if(DsOamLmStats_t == tbl_id)
    {
        if(index < 128)
        {
            tbl_id = DsOamLmStats0_t;
        }
        else if(index < 256)
        {
            tbl_id = DsOamLmStats1_t;
            index -= 128;
        }
        else
        {

        }
    }
    */

    if (drv_goldengate_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            sal_printf("%% ERROR! dynamic table is not allocated!\n");
            return;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
                                (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset))/DRV_BYTES_PER_ENTRY;

        /* check if the space wbit is same */
        for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
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
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
            {
                *(wbit_start_addr_ptr + add_num) = TRUE;
            }
        }
        else
        {
            /* if can't set wbit ,output the error info */
            sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            sal_printf("%% ERROR! duplicate set wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
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
        if(drv_goldengate_table_is_tcam_ad(tbl_id))
        {
            sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE);
            wbit_start_addr_ptr = SRAM_INFO_TCAM_AD_MEM_WBIT_BASE_PTR(chip_id_offset) +
                                (start_data_addr - SRAM_INFO_TCAM_AD_MEM_BASE(chip_id_offset))/DRV_BYTES_PER_ENTRY;


            /* check if the space wbit is same */
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
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
                for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
                {
                    *(wbit_start_addr_ptr + add_num) = TRUE;
                }
            }
            else
            {
                /* if can't set wbit ,output the error info */
                sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
                sal_printf("%% ERROR! duplicate set wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
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
drv_goldengate_model_sram_tbl_clear_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
#if 0
    uintptr start_data_addr = 0;
    uint32 add_num = 0;
    uint8 *wbit_start_addr_ptr = NULL;
    uint8 set_wbit_flag = TRUE, first_hit_flag = FALSE, first_hit_value = FALSE;

    if (drv_goldengate_table_is_tcam_key(tbl_id)
        || drv_table_is_lpm_tcam_key(tbl_id))
    {
        sal_printf("%% ERROR! clear table %d index %d wbit fail!!\n", tbl_id, index);
        sal_printf("%% ERROR! can't clear table sbit for Tcam!\n");
        return;
    }

    /*
    if(DsOamLmStats_t == tbl_id)
    {
        if(index < 128)
        {
            tbl_id = DsOamLmStats0_t;
        }
        else if(index < 256)
        {
            tbl_id = DsOamLmStats1_t;
            index -= 128;
        }
        else
        {
        }
    }
    */

    if (drv_goldengate_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            sal_printf("%% ERROR! clear table %d index %d wbit fail!!\n", tbl_id, index);
            sal_printf("%% ERROR! dynamic table is not allocated!\n");
            return;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
                                (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset))/DRV_BYTES_PER_ENTRY;

        /* check if the space wbit is same */
        for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
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
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
            {
                *(wbit_start_addr_ptr + add_num) = FALSE;
            }
        }
        else
        {
            /* if can't clear wbit ,output the error info */
            sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
            sal_printf("%% ERROR! duplicate clear wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
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
            for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
            {
                if (!first_hit_flag)
                {
                    first_hit_flag = TRUE;
                    first_hit_value = *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                        index*(TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY) + add_num);
                }
                else
                {
                    /* if there is different value of first wbit, set flag to indicate can't set wbit */
                    if (first_hit_value != *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                        index*(TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY) + add_num))
                    {
                        set_wbit_flag = FALSE;
                        break;
                    }
                }
            }

            if (set_wbit_flag)
            {
                for (add_num = 0; add_num < (TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY); add_num++)
                {
                    *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +
                                    index*(TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY) + add_num) = FALSE;
                }
            }
            else
            {
                /* if can't set wbit ,output the error info */
                sal_printf("%% ERROR! Set table %s index %d fail!!\n", TABLE_NAME(tbl_id), index);
                sal_printf("%% ERROR! duplicate clear wbit and previously set 1 wbit, now set 2 wbit include the wbit!!\n");
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
#endif

    return;

}

/**
 @brief sram table model get entry write bit
*/
uint8
drv_goldengate_model_sram_tbl_get_wbit(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{



    uintptr start_data_addr = 0;
    uint8 *wbit_start_addr_ptr = NULL;
   return TRUE;
    if (drv_goldengate_table_is_tcam_key(tbl_id)
        || drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        sal_printf("%% ERROR! get table %d index %d wbit fail!!\n", tbl_id, index);
        sal_printf("%% ERROR! can't get table sbit for Tcam!\n");
        return FALSE;
    }

    /*
    if(DsOamLmStats_t == tbl_id)
    {
        if(index < 128)
        {
            tbl_id = DsOamLmStats0_t;
        }
        else if(index < 256)
        {
            tbl_id = DsOamLmStats1_t;
            index -= 128;
        }
        else
        {

        }
    }
    */

    if (drv_goldengate_table_is_dynamic_table(tbl_id))
    {
        if (DYNAMIC_EXT_INFO_PTR(tbl_id) == NULL)
        {
            sal_printf("%% ERROR! get table %d index %d wbit fail!!\n", tbl_id, index);
            sal_printf("%% ERROR! dynamic table is not allocated!\n");
            return FALSE;
        }

        sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE);
        wbit_start_addr_ptr = SRAM_INFO_DYNAMIC_MEM_WBIT_BASE_PTR(chip_id_offset) +
                                (start_data_addr - SRAM_INFO_DYNAMIC_MEM_BASE(chip_id_offset))/DRV_BYTES_PER_ENTRY;
        return *wbit_start_addr_ptr;

    }
    else
    {
        /* tcam ad wbit option is different from static table, 8W should clear 2 wbit,
           because sram allocated memory for tcam ad is per 4W, so 8W will use 2 unit space*/
        if (sram_model_is_tcam_ad_table(chip_id_offset, tbl_id))
        {
            sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE);
            wbit_start_addr_ptr = SRAM_INFO_TCAM_AD_MEM_WBIT_BASE_PTR(chip_id_offset) +
                                (start_data_addr - SRAM_INFO_TCAM_AD_MEM_BASE(chip_id_offset))/DRV_BYTES_PER_ENTRY;
            /*return *(SRAM_INFO_MODEL_TBL_WBIT_PTR(chip_id_offset, tbl_id) +*/
            /*         index*(TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY));*/
           return *wbit_start_addr_ptr;

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
drv_goldengate_model_write_sram_entry(uint8 chip_id_offset, uintptr addr,
                                      uint32* data, int32 len)
{
    DRV_IF_ERROR_RETURN(sram_model_write(chip_id_offset, addr, data, len));
    return DRV_E_NONE;
}

/**
 @brief Real sram direct read operation I/O
*/
int32
drv_goldengate_model_read_sram_entry(uint8 chip_id_offset, uintptr addr,
                                     uint32* data, int32 len)
{
    DRV_IF_ERROR_RETURN(sram_model_read(chip_id_offset, addr, data, len));
    return DRV_E_NONE;
}

/**
 @brief The function write table data to a sram memory location
*/
int32
drv_goldengate_model_sram_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, uint32* data)
{
    uintptr start_data_addr = 0;
    uint32 entry_size= 0;
    uint32 max_index_num = 0;

    DRV_TBL_EMPTY_CHECK(tbl_id);
    DRV_PTR_VALID_CHECK(data);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* check if the index num exceed the max index num of the tbl */
    entry_size = TABLE_ENTRY_SIZE(tbl_id);
    max_index_num = TABLE_MAX_INDEX(tbl_id);

    if (max_index_num <= index)
    {
        DRV_DBG_INFO("\nERROR (drv_goldengate_model_sram_tbl_write): chip-0x%x, tbl-0x%x, index-0x%x exceeds the max_index-0x%x.\n",
                     chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base, tbl_id, index, max_index_num);
        return DRV_E_INVALID_TBL;
    }

    if (drv_goldengate_table_is_tcam_ad(tbl_id) || drv_goldengate_table_is_lpm_tcam_ad(tbl_id) |  drv_goldengate_table_is_lpm_tcam_ad(tbl_id)) /* SDK Modify*/
    {
        index = index* TCAM_KEY_SIZE(tbl_id);

#if 0
        DRV_DBG_INFO("Table[%d]: OldIndex = %d, NewIndex:%d \n", tbl_id, old_index, index);
#endif
    }

    DRV_IF_ERROR_RETURN(sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE));

    DRV_IF_ERROR_RETURN(drv_goldengate_model_write_sram_entry(chip_id_offset, start_data_addr, data, entry_size));

    drv_goldengate_model_sram_tbl_set_wbit(chip_id_offset, tbl_id, index);

    return DRV_E_NONE;
}

/**
 @brief The function read table data from a sram memory location
*/
int32
drv_goldengate_model_sram_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, uint32* data)
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
        DRV_DBG_INFO("ERROR (drv_goldengate_model_sram_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base, tbl_id, index, max_index_num);
        return DRV_E_INVALID_TBL;
    }

    if (drv_goldengate_table_is_tcam_ad(tbl_id) || drv_goldengate_table_is_lpm_tcam_ad(tbl_id))/* SDK Modify*/
    {
        index = index* TCAM_KEY_SIZE(tbl_id);

#if 0
        DRV_DBG_INFO("Table[%d]: OldIndex = %d, NewIndex:%d\n", tbl_id, old_index, index);
#endif
    }

    DRV_IF_ERROR_RETURN(sram_model_get_sw_address_by_tbl_id_and_index(chip_id_offset, tbl_id, index, &start_data_addr, FALSE));

    DRV_IF_ERROR_RETURN(drv_goldengate_model_read_sram_entry(chip_id_offset, start_data_addr, data, entry_size));

    return DRV_E_NONE;
}

/**
 @brief write tcam interface (include operate model and real tcam)
*/
int32
drv_goldengate_model_tcam_tbl_write(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t* entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 entry_num_each_idx = 0, entry_idx = 0, offset = 0;
    uintptr tcam_model_data_base = 0;
    uintptr data_addr = 0, mask_addr = 0;
    uint32 *model_wbit_base = NULL;
    uint32 entry_num_offset = 0;
    uint8  tcam_type = 0;
    uint32 entry_word_len = 0;
    uint32 words_per_entry = 0;
    uintptr tcam_tbl_data_base = 0, tcam_tbl_mask_base = 0;
    uint32 couple_mode = 0;


    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When write operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_model_tcam_tbl_write): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        tcam_model_data_base = (uintptr)tcam_info.int_tcam_data_base[chip_id_offset];
        model_wbit_base = tcam_info.int_tcam_wbit[chip_id_offset];
        tcam_type = 1;
    }
    else if(drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        tcam_model_data_base = (uintptr)tcam_info.int_lpm_tcam_data_base[chip_id_offset];
        model_wbit_base = tcam_info.int_lpm_tcam_wbit[chip_id_offset];
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Check address ERROR when Lookup TCAM!\n");
        return DRV_E_INVALID_ADDR;
    }

    /* the key's each index includes 80bit entry number, lpm 40bit entry number */
    if ( 3 == tcam_type)
    {
        if (drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_LPM_KEY_BYTES_PER_ENTRY;
        }
        else
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;
        }
		/* SDK Modify*/
    }
    else
    {
        entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;
		/* SDK Modify*/
    }

    /* Get the table's software data and mask address */ /* SDK Modify*/
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index, &tcam_tbl_data_base, TRUE);
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index, &tcam_tbl_mask_base, FALSE);
     /*sal_printf("tbl id is %d, index is %d, tcam_tbl_data_base is 0x%x\n",tbl_id, index, tcam_tbl_data_base);*/
    offset = (TABLE_ENTRY_SIZE(tbl_id) - TCAM_KEY_SIZE(tbl_id)) / DRV_BYTES_PER_WORD; /* wordOffset */

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; entry_idx++)
    {
	 	/* SDK Modify*/
        if( 3 == tcam_type)
        {
             if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
            {
                data_addr = tcam_tbl_data_base  +  entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base  +  entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                entry_num_offset = (tcam_tbl_data_base - tcam_model_data_base) / DRV_LPM_KEY_BYTES_PER_ENTRY +  entry_idx;
                entry_word_len = DRV_LPM_KEY_BYTES_PER_ENTRY;
                words_per_entry = DRV_LPM_WORDS_PER_ENTRY;
            }
            else
            {
                data_addr = tcam_tbl_data_base + entry_idx * DRV_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base  +  entry_idx * DRV_BYTES_PER_ENTRY;
                entry_num_offset = (DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM << couple_mode)
                   + (tcam_tbl_data_base - (tcam_model_data_base + (DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM << couple_mode)*DRV_LPM_KEY_BYTES_PER_ENTRY)) / DRV_BYTES_PER_ENTRY
                   +  entry_idx;
                entry_word_len = DRV_BYTES_PER_ENTRY;
                words_per_entry = DRV_WORDS_PER_ENTRY;
            }
        }
        else
        {
            data_addr = tcam_tbl_data_base  + ( entry_idx) * DRV_BYTES_PER_ENTRY;
            mask_addr = tcam_tbl_mask_base + ( entry_idx) * DRV_BYTES_PER_ENTRY;
            entry_num_offset = (tcam_tbl_data_base - tcam_model_data_base) / DRV_BYTES_PER_ENTRY   + (entry_idx);
            entry_word_len = DRV_BYTES_PER_ENTRY;
            words_per_entry = DRV_WORDS_PER_ENTRY;

        }
        DRV_IF_ERROR_RETURN(tcam_model_write(chip_id_offset, data_addr,
                         data + entry_idx*words_per_entry, entry_word_len));

        DRV_IF_ERROR_RETURN(tcam_model_write(chip_id_offset, mask_addr,
                         mask + entry_idx*words_per_entry, entry_word_len));

#if 0
       sal_printf("tbl:%d,  index :%d, entry_num_offset:%d\n",   tbl_id, index, entry_num_offset);
        sal_printf("tbl:%d, dataBase:0x%x, dataAddr:0x%x, maskBase:0x%x, maskAddr:0x%x\n",
                          tbl_id, tcam_tbl_data_base, data_addr, tcam_tbl_mask_base, mask_addr);
#endif

        if (!IS_BIT_SET(model_wbit_base[entry_num_offset/DRV_BITS_PER_WORD], entry_num_offset%DRV_BITS_PER_WORD))
        {
            SET_BIT(model_wbit_base[entry_num_offset/DRV_BITS_PER_WORD], entry_num_offset%DRV_BITS_PER_WORD);
        }
    }

    return DRV_E_NONE;
}


/**
 @brief read tcam interface (include operate model and real tcam)
*/
int32
drv_goldengate_model_tcam_tbl_read(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index, tbl_entry_t *entry)
{
    uint32* mask = NULL;
    uint32* data = NULL;
    uint32 entry_num_each_idx = 0, entry_idx = 0, offset = 0;
    uintptr data_addr = 0, mask_addr = 0;
    uint8  tcam_type = 0;
    uint32 entry_word_len = 0;
    uint32 words_per_entry = 0;
    uintptr tcam_tbl_data_base = 0, tcam_tbl_mask_base = 0;

    DRV_PTR_VALID_CHECK(entry);
    DRV_PTR_VALID_CHECK(entry->data_entry);
    DRV_PTR_VALID_CHECK(entry->mask_entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When tcam key read operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_model_tcam_tbl_read): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }
    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        tcam_type = 1;
    }
    else if(drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Check address ERROR when Lookup TCAM!\n");
        return DRV_E_INVALID_ADDR;
    }

    /* Get the table's software data and mask address */ /* SDK Modify*/
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index , &tcam_tbl_data_base, TRUE);
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index, &tcam_tbl_mask_base, FALSE);

     /*sal_printf("tbl id is %d, index is %d, tcam_tbl_data_base is 0x%x\n",tbl_id, index, tcam_tbl_data_base);*/

    /* the key's each index includes 80bit entry number, lpm 40bit entry number */
    if( 3 == tcam_type)
    {
        if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id)/DRV_LPM_KEY_BYTES_PER_ENTRY;
        }
        else
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY;
        }
    }
    else
    {
        entry_num_each_idx = TCAM_KEY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY;
    }
    offset = (TABLE_ENTRY_SIZE(tbl_id) - TCAM_KEY_SIZE(tbl_id))/DRV_BYTES_PER_WORD;

    data = entry->data_entry + offset;
    mask = entry->mask_entry + offset;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; entry_idx++)
    {
        /* SDK Modify*/
        if(3 == tcam_type)
        {
            if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
            {
                data_addr = tcam_tbl_data_base + entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base +  entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                entry_word_len = DRV_LPM_KEY_BYTES_PER_ENTRY;
                words_per_entry = DRV_LPM_WORDS_PER_ENTRY;
            }
            else
            {
                data_addr = tcam_tbl_data_base + entry_idx * DRV_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base + entry_idx * DRV_BYTES_PER_ENTRY;
                entry_word_len = DRV_BYTES_PER_ENTRY;
                words_per_entry = DRV_WORDS_PER_ENTRY;
            }
        }
        else
        {
            data_addr = tcam_tbl_data_base  +  entry_idx * DRV_BYTES_PER_ENTRY;
            mask_addr = tcam_tbl_mask_base  +  entry_idx * DRV_BYTES_PER_ENTRY;
            entry_word_len = DRV_BYTES_PER_ENTRY;
            words_per_entry = DRV_WORDS_PER_ENTRY;

        }
        DRV_IF_ERROR_RETURN(tcam_model_read(chip_id_offset, data_addr,
                                        data + entry_idx*words_per_entry, entry_word_len));

        DRV_IF_ERROR_RETURN(tcam_model_read(chip_id_offset, mask_addr,
                                        mask + entry_idx*words_per_entry, entry_word_len));
#if 0
        sal_printf("tbl:%d, index:%d, dataBase:0x%x, dataAddr:0x%x, maskBase:0x%x, maskAddr:0x%x\n",
                          tbl_id, index, tcam_tbl_data_base, data_addr, tcam_tbl_mask_base, mask_addr);
#endif

    }

    return DRV_E_NONE;
}

/**
 @brief remove tcam entry interface (include operate model and real tcam)
*/
/* still need check, shanz */
int32
drv_goldengate_model_tcam_tbl_remove(uint8 chip_id_offset, tbls_id_t tbl_id, uint32 index)
{
    uint32 entry_num_each_idx = 0, entry_idx = 0;
    uintptr tcam_model_data_base = 0;
    uintptr data_addr = 0, mask_addr = 0;
    uint32 *model_wbit_base = NULL;
    uint32 entry_num_offset = 0;
    uint8  tcam_type = 0;
    uint32 entry_word_len = 0;
    uintptr tcam_tbl_data_base = 0, tcam_tbl_mask_base = 0;
    uint32 couple_mode = 0;

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id)))
    {
        DRV_DBG_INFO("@@ ERROR! When write operation, INVALID Tcam key TblID! TblID: %d, file:%s line:%d function:%s\n",
                      tbl_id, __FILE__, __LINE__, __FUNCTION__);
        return DRV_E_INVALID_TBL;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_model_tcam_tbl_write): chip-0x%x, tbl-%d, index-0x%x exceeds the max_index-0x%x.\n",
                        chip_id_offset + drv_gg_init_chip_info.drv_init_chipid_base, tbl_id, index, TABLE_MAX_INDEX(tbl_id));
        return DRV_E_INVALID_TBL;
    }

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        tcam_model_data_base = (uintptr)tcam_info.int_tcam_data_base[chip_id_offset];
        model_wbit_base = tcam_info.int_tcam_wbit[chip_id_offset];
        tcam_type = 1;
    }
    else if(drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        tcam_model_data_base = (uintptr)tcam_info.int_lpm_tcam_data_base[chip_id_offset];
        model_wbit_base = tcam_info.int_lpm_tcam_wbit[chip_id_offset];
        tcam_type = 3;
    }
    else
    {
        tcam_type = 0;
        DRV_DBG_INFO("@@ Check address ERROR when Lookup TCAM!\n");
        return DRV_E_INVALID_ADDR;
    }

    /* the key's each index includes 80bit entry number, lpm 40bit entry number */
    if ( 3 == tcam_type)
    {
        if (drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_LPM_KEY_BYTES_PER_ENTRY;
        }
        else
        {
            entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;
        }
        /* SDK Modify*/
    }
    else
    {
        entry_num_each_idx = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY;
        /* SDK Modify*/
    }

    /* Get the table's software data and mask address */ /* SDK Modify*/
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index, &tcam_tbl_data_base, TRUE);
    tcam_model_get_sw_address(chip_id_offset, tbl_id, index, &tcam_tbl_mask_base, FALSE);

    for (entry_idx = 0; entry_idx < entry_num_each_idx; entry_idx++)
    {
        /* SDK Modify*/
        if( 3 == tcam_type)
        {
             if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
            {
                data_addr = tcam_tbl_data_base  +  entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base  +  entry_idx * DRV_LPM_KEY_BYTES_PER_ENTRY;
                entry_num_offset = (tcam_tbl_data_base - tcam_model_data_base) / DRV_LPM_KEY_BYTES_PER_ENTRY +  entry_idx;
                entry_word_len = DRV_LPM_KEY_BYTES_PER_ENTRY;
            }
            else
            {
                data_addr = tcam_tbl_data_base + entry_idx * DRV_BYTES_PER_ENTRY;
                mask_addr = tcam_tbl_mask_base  +  entry_idx * DRV_BYTES_PER_ENTRY;
                entry_num_offset = (DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM << couple_mode)
                   + (tcam_tbl_data_base - (tcam_model_data_base + (DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM << couple_mode)*DRV_LPM_KEY_BYTES_PER_ENTRY)) / DRV_BYTES_PER_ENTRY
                   +  entry_idx;
                entry_word_len = DRV_BYTES_PER_ENTRY;
            }
        }
        else
        {
            data_addr = tcam_tbl_data_base  + ( entry_idx) * DRV_BYTES_PER_ENTRY;
            mask_addr = tcam_tbl_mask_base + ( entry_idx) * DRV_BYTES_PER_ENTRY;
            entry_num_offset = (tcam_tbl_data_base - tcam_model_data_base) / DRV_BYTES_PER_ENTRY   + (entry_idx);
            entry_word_len = DRV_BYTES_PER_ENTRY;
        }

        DRV_IF_ERROR_RETURN(tcam_model_remove(chip_id_offset, data_addr, entry_word_len));

        DRV_IF_ERROR_RETURN(tcam_model_remove(chip_id_offset, mask_addr, entry_word_len));


        if (IS_BIT_SET(model_wbit_base[entry_num_offset/DRV_BITS_PER_WORD], entry_num_offset%DRV_BITS_PER_WORD))
        {
            CLEAR_BIT(model_wbit_base[entry_num_offset/DRV_BITS_PER_WORD], entry_num_offset%DRV_BITS_PER_WORD);
        }
    }

    return DRV_E_NONE;
}

extern int ex_cplusplus_sim_cpu_add_delete(uint8 chip_id, uint32 * cpu_req, uint32 *cpu_rlt);
extern int ex_cplusplus_sim_cpu_lookup_acc_interface(uint8 chip_id, uint8 req_type,uint32 * cpu_req,uint32 * cpu_req1,uint32 *cpu_rlt);
extern int ex_cplusplus_sim_Ipfix_add_delete(void);

int32
drv_goldengate_model_fib_acc_process(uint8 chip_id, uint32* i_fib_acc_cpu_req, uint32* o_fib_acc_cpu_rlt, uint8 is_store)
{
    return ex_cplusplus_sim_cpu_add_delete(chip_id, i_fib_acc_cpu_req, o_fib_acc_cpu_rlt);
}

int32
drv_goldengate_model_cpu_acc_asic_lkup(uint8 chip_id, uint8 req_type, void * cpu_req, void * cpu_req1, void *cpu_rlt)
{
    return ex_cplusplus_sim_cpu_lookup_acc_interface(chip_id, req_type, cpu_req, cpu_req1, cpu_rlt);
}

int32
drv_goldengate_model_ipfix_acc_process(uint8 chip_id, void* i_ipfix_req, void* o_ipfix_result)
{
    uint32 cmdw = DRV_IOW(IpfixHashAdCpuReq_t, DRV_ENTRY_FLAG);
    uint32 cmdr = DRV_IOR(IpfixHashAdCpuResult_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdw, i_ipfix_req));
    ex_cplusplus_sim_Ipfix_add_delete();
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmdr, o_ipfix_result));
    return DRV_E_NONE;
}

#endif



/**
  @file drv_tbl_reg.c

  @date 2010-07-22

  @version v5.1

  The file implement driver IOCTL defines and macros
*/

#include "greatbelt/include/drv_lib.h"

/**********************************************************************************
              Define Global declarations, Typedef, define and Data Structure
***********************************************************************************/
extern uint8 gb_current_chip_type;
extern host_type_t gb_host_type;

/* global store driver all mem info */
extern tables_info_t drv_gb_tbls_list[MaxTblId_t];

/* OAM HASH and IPE LKP HASH is not the same at realize method, Need to think!!!*/
#define DRV_HASH_KEY_TBL_ID_VALID_CHECK(tbl_id)        \
    if ((DsLpmIpv4Hash16Key_t != (tbl_id))                     \
        && (DsLpmIpv4Hash8Key_t != (tbl_id))                    \
        && (DsLpmIpv6Hash32Key_t != (tbl_id)))                  \
    { \
        DRV_DBG_INFO("\nERROR! INVALID Hash Key MemID! MemID: %d, file:%s line:%d function:%s\n", tbl_id, __FILE__, __LINE__, __FUNCTION__); \
        return DRV_E_INVALID_TBL; \
    }

/* TCAM KEY will have 4 chances for lookup */
#define DRV_IS_TCAM_ACL_KEY(tbl_id) \
    ((DsAclMacKey0_t == (tbl_id))  \
     || (DsAclMacKey1_t == (tbl_id))  \
     || (DsAclMacKey2_t == (tbl_id))  \
     || (DsAclMacKey3_t == (tbl_id))  \
     || (DsAclIpv4Key0_t == (tbl_id)) \
     || (DsAclIpv4Key1_t == (tbl_id)) \
     || (DsAclIpv4Key2_t == (tbl_id)) \
     || (DsAclIpv4Key3_t == (tbl_id)) \
     || (DsAclMplsKey0_t == (tbl_id)) \
     || (DsAclMplsKey1_t == (tbl_id)) \
     || (DsAclMplsKey2_t == (tbl_id)) \
     || (DsAclMplsKey3_t == (tbl_id)) \
     || (DsAclIpv6Key0_t == (tbl_id)) \
     || (DsAclIpv6Key1_t == (tbl_id)))

/* index refer to sram_share_mem_id_t and sram_other_mem_id_t, please sync up with SPEC design!! */
/* spec V5.1 info is as follow: */
/* after spec V5.1 memory allocation ,the order of bitmap must same of share list id order */
uint16 gb_dynic_table_related_edram_bitmap[] =
{
    0x48,  /* LpmHashKey(s):    3, 6 edrams          -- LPM_HASH_KEY_SHARE_TABLE     */
    0x9,   /* LpmlkpKey0(s):    0, 3 edrams          -- LPM_LOOKUP_KEY_TABLE0        */
    0x6,   /* LpmlkpKey1(s):    1, 2 edrams          -- LPM_LOOKUP_KEY_TABLE1        */
    0x6,   /* LpmlkpKey2(s):    1, 2 edrams          -- LPM_LOOKUP_KEY_TABLE2        */
    0x9,   /* LpmlkpKey3(s):    0, 3 edrams          -- LPM_LOOKUP_KEY_TABLE3        */
    0x81,  /* UserIdHashKey(s): 0, 7 edrams          -- USER_ID_HASH_KEY_SHARE_TABLE */
    0x78,  /* UserIdHashAd(s):  3, 4, 5, 6 edrams    -- USER_ID_HASH_AD_SHARE_TABLE  */
    0x78,  /* OAMChanAd(s):     3, 4, 5, 6 edrams    -- OAM_HASH_AD_CHAN_TABLE  */
    0x4F,  /* FibHashKey(s):    0, 1, 2, 3, 6 edrams -- FIB_HASH_KEY_SHARE_TABLE     */
    0x71,  /* FibHashAD(s):     0, 4, 5, 6 edrams    -- FIB_HASH_AD_SHARE_TABLE      */
    0x39,  /* DsFwd:            0, 3, 4, 5 edrams    -- FWD_TABLE                    */
    0x31,  /* DsMetEntry:       0, 4, 5 edrams          -- MPLS_TABLE                   */
    0x36,  /* DsMpls:           1, 2, 4, 5 edrams    -- MPLS_TABLE                   */
    0x70,  /* DsMa(s):          4, 5, 6 edrams       -- MA_SHARE_TABLE          */
    0x70,  /* DsMaName(s):      4, 5, 6 edrams       -- MA_NAME_SHARE_TABLE          */
    0x3A,  /* NextHop(s):       1, 3, 4, 5 edrams    -- NEXTHOP_SHARE_TABLE          */

    0x32,  /* L3Edit(s):        1, 4, 5 edrams          -- L2/3_EDIT_SHARE_TABLE     */
    0x70,  /* OamMep(s):        4, 5, 6 edrams       -- OAM_MEP_SHARE_TABLE          */

    0x14C,  /* DsStats:          2, 3, 6, 8 edrams (must select edram 8) -- STATS_TABLE  */
    0x100,  /* DsOamLmStats:    8 edrams (must select edram 9)    -- OAM_LM_STATS_TABLE */

    0x0,   /* MaxOtherMemId:    invalid              -- MAX_OTHER_MEM_ID             */
};

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @brief register a memory to the register directory and register field info of this memory together
*/
int32
drv_greatbelt_tbl_register(tbls_id_t tbl_id, uint32 data_offset, uint32 max_idx,
                 uint16 entry_size, uint8 num_f, fields_t* ptr_f)
{
    tables_info_t* ptr_tbl_info = NULL;

    DRV_PTR_VALID_CHECK(ptr_f)

    if (tbl_id >= MaxTblId_t)
    {
        return DRV_E_INVALID_TBL;
    }

    ptr_tbl_info = &drv_gb_tbls_list[tbl_id];

    ptr_tbl_info->hw_data_base = data_offset;
    ptr_tbl_info->max_index_num = max_idx;
    ptr_tbl_info->entry_size = entry_size;
    ptr_tbl_info->num_fields = num_f;
    ptr_tbl_info->ptr_fields = ptr_f;

    return DRV_E_NONE;
}

/**
 @brief register a Memory field to the register field directory
*/
fields_t*
drv_greatbelt_find_field(tbls_id_t tbl_id, fld_id_t field_id)
{
    if (CHK_TABLE_ID_VALID(tbl_id) && CHK_FIELD_ID_VALID(tbl_id, field_id))
    {
        return (TABLE_FIELD_INFO_PTR(tbl_id) + field_id);
    }
    else
    {
        DRV_DBG_INFO("ERROR! INVALID memID or fieldID! MemID: %d, fieldID: %d\n", tbl_id, field_id);
        return NULL;
    }
}

/**
 @brief Get a field of  word & bit offset
*/
int32
drv_greatbelt_get_field_offset(tbls_id_t tbl_id, fld_id_t field_id, uint32* w_offset, uint32* b_offset)
{
    fields_t* field = NULL;

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    field = drv_greatbelt_find_field(tbl_id, field_id);
    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_greatbelt_mem_get_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }

    if (NULL != w_offset)
    {
        *w_offset = field->word_offset;
    }

    if (NULL != b_offset)
    {
        *b_offset = field->bit_offset;
    }

    return DRV_E_NONE;
}

/**
 @brief Check tcam field in key size
*/
int32
drv_greatbelt_tcam_field_chk(tbls_id_t tbl_id, fld_id_t field_id)
{
    uint32 w_offset     = 0;
    uint32 entry_size   = 0;
    uint32 key_size     = 0;

    DRV_IF_ERROR_RETURN(drv_greatbelt_get_field_offset(tbl_id, field_id, &w_offset, NULL));

    entry_size  = TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_WORD;
    key_size    = TCAM_KEY_SIZE(tbl_id) / DRV_BYTES_PER_WORD;

    if ((entry_size - w_offset) > key_size)
    {
        DRV_DBG_INFO("Error fld: %d, tbl_id %d\n", field_id, tbl_id);
        return DRV_E_INVALID_FLD;
    }

    return DRV_E_NONE;
}

/**
 @brief Get a field of a memory data entry
*/
int32
drv_greatbelt_get_field(tbls_id_t tbl_id, fld_id_t field_id,
              uint32* entry, uint32* value)
{
    fields_t* field = NULL;
    uint32 val;

    DRV_PTR_VALID_CHECK(entry)
    DRV_PTR_VALID_CHECK(value)
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    field = drv_greatbelt_find_field(tbl_id, field_id);
    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_greatbelt_mem_get_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }

    /* get from memory */
    val = entry[field->word_offset] >> field->bit_offset;

    if (field->len < 32)
    {
        *value = val & ((1 << field->len) - 1);
    }
    else
    {
        *value = val;
    }

    return DRV_E_NONE;
}

/**
 @brief Set a field of a memory data entry
*/
int32
drv_greatbelt_set_field(tbls_id_t tbl_id, fld_id_t field_id,
              uint32* entry, uint32 value)
{
    fields_t* field = NULL;
    uint32 mask;

    DRV_PTR_VALID_CHECK(entry)
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    /* register field support check */
    field = drv_greatbelt_find_field(tbl_id, field_id);
    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_greatbelt_mem_set_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }

    if (field->len < 32)
    {
        mask = (1 << field->len) - 1;
        value = value&mask;
        if ((value & ~mask) != 0)
        {
            /* check if value is too big for this field */
            DRV_DBG_INFO("ERROR! (drv_greatbelt_mem_set_field): memID-%d, field-%d, value is too big for this field.\n", tbl_id, field_id);
            return DRV_E_FIELD_OVER;
        }
    }
    else
    {
        mask = -1;
    }

    /* set to memory */
    entry[field->word_offset] = (entry[field->word_offset] & (~(mask << field->bit_offset))) | (value << field->bit_offset);

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_greatbelt_get_tbl_string_by_id(tbls_id_t tbl_id, char* name)
{
    if (!name)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if ((tbl_id >= 0) && (tbl_id < MaxTblId_t))
    {
        sal_strcpy(name, TABLE_NAME(tbl_id));
        return DRV_E_NONE;
    }

    return DRV_E_INVALID_TBL;
}

/**
 @brief
*/
int32
drv_greatbelt_get_field_string_by_id(tbls_id_t tbl_id, fld_id_t field_id, char* name)
{
    fields_t* ptr_field_info = NULL;

    if (!name)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if ((tbl_id >= 0) && (tbl_id < MaxTblId_t))
    {
        if (field_id < TABLE_FIELD_NUM(tbl_id))
        {
            ptr_field_info = TABLE_FIELD_INFO_PTR(tbl_id);
            sal_strcpy(name, ptr_field_info[field_id].ptr_field_name);
            return DRV_E_NONE;
        }
        else
        {
            return DRV_E_INVALID_FLD;
        }
    }

    return DRV_E_INVALID_TBL;
}

/**
 @brief
*/
int32
drv_greatbelt_get_tbl_id_by_string(tbls_id_t* tbl_id, char* name)
{
    tbls_id_t tmp_tableid = 0;

    /* This is so inefficient Code, need to consider to optimize it, add by zhouw??? */
    for (tmp_tableid = 0; tmp_tableid < MaxTblId_t; tmp_tableid++)
    {
        if (0 == sal_strcasecmp(name, TABLE_NAME(tmp_tableid)))
        {
            *tbl_id = tmp_tableid;
            return DRV_E_NONE;
        }
    }

    DRV_DBG_INFO("%% Not Find the TableID!! tableName: %s\n", name);

    return DRV_E_INVALID_TBL;
}

/**
 @brief
*/
int32
drv_greatbelt_get_field_id_by_string(tbls_id_t tbl_id, fld_id_t* field_id, char* name)
{
    fields_t* ptr_field_info = TABLE_FIELD_INFO_PTR(tbl_id);
    uint32 field_num = TABLE_FIELD_NUM(tbl_id);
    uint32 tmp_index = 0;

    for (tmp_index = 0; tmp_index < field_num; tmp_index++)
    {
        if (0 == sal_strcasecmp(name, ptr_field_info[tmp_index].ptr_field_name))
        {
            *field_id = tmp_index;
            return DRV_E_NONE;
        }
    }

    DRV_DBG_INFO("%% Not Find the FieldID!! tableId: %d; Fieldname: %s\n", tbl_id, name);

    return DRV_E_INVALID_FLD;
}

/**
 @brief
*/
int32
drv_greatbelt_sram_ds_to_entry(tbls_id_t tbl_id, void* ds, uint32* entry)
{
    uint32* p_ds = NULL;
    uint32 ds_size = 0; /* unit: bytes */

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    ds_size = TABLE_ENTRY_SIZE(tbl_id);
    p_ds = (uint32*)ds;     /* Here need to notice: if the ds size is less
                               than uint32, the imperative type conversion
                               may be produce problem. */

    sal_memcpy(entry, p_ds, ds_size);

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_greatbelt_sram_entry_to_ds(tbls_id_t tbl_id, uint32* entry, void* ds)
{
    uint32* p_ds = NULL;
    uint32 ds_size = 0; /* unit: bytes */

    DRV_TBL_ID_VALID_CHECK(tbl_id);
    p_ds = (uint32*)ds;     /* Here need to notice: if the ds size is less
                               than uint32, the imperative type conversion
                               may be produce problem. */

    ds_size = TABLE_ENTRY_SIZE(tbl_id);

    sal_memset((uint8*)ds, 0, ds_size);

    sal_memcpy(p_ds, entry, ds_size);

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_greatbelt_tcam_ds_to_entry(tbls_id_t tbl_id, void* ds, void* entry)
{
    int32 ret = DRV_E_NONE;
    uint32* ds_data = NULL;
    uint32* ds_mask = NULL;
    uint32* entry_data = NULL;
    uint32* entry_mask = NULL;
    tbl_entry_t* p_ds = NULL;
    tbl_entry_t* p_entry = NULL;

    DRV_PTR_VALID_CHECK(ds);
    DRV_PTR_VALID_CHECK(entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    p_ds = (tbl_entry_t*)ds;
    p_entry = (tbl_entry_t*)entry;
    ds_data = p_ds->data_entry;
    ds_mask = p_ds->mask_entry;
    entry_data = p_entry->data_entry;
    entry_mask = p_entry->mask_entry;

    DRV_PTR_VALID_CHECK(ds_data);
    DRV_PTR_VALID_CHECK(ds_mask);
    DRV_PTR_VALID_CHECK(entry_data);
    DRV_PTR_VALID_CHECK(entry_mask);

    ret = drv_greatbelt_sram_ds_to_entry(tbl_id, ds_data, entry_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    ret = drv_greatbelt_sram_ds_to_entry(tbl_id, ds_mask, entry_mask);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_greatbelt_tcam_entry_to_ds(tbls_id_t tbl_id, void* entry, void* ds)
{
    int32 ret = DRV_E_NONE;
    uint32* ds_data = NULL;
    uint32* ds_mask = NULL;
    uint32* entry_data = NULL;
    uint32* entry_mask = NULL;
    tbl_entry_t* p_ds = NULL;
    tbl_entry_t* p_entry = NULL;

    DRV_PTR_VALID_CHECK(ds);
    DRV_PTR_VALID_CHECK(entry);
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    p_ds = (tbl_entry_t*)ds;
    p_entry = (tbl_entry_t*)entry;
    ds_data = p_ds->data_entry;
    ds_mask = p_ds->mask_entry;
    entry_data = p_entry->data_entry;
    entry_mask = p_entry->mask_entry;

    DRV_PTR_VALID_CHECK(ds_data);
    DRV_PTR_VALID_CHECK(ds_mask);
    DRV_PTR_VALID_CHECK(entry_data);
    DRV_PTR_VALID_CHECK(entry_mask);

    ret = drv_greatbelt_sram_entry_to_ds(tbl_id, entry_data, ds_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    ret = drv_greatbelt_sram_entry_to_ds(tbl_id, entry_mask, ds_mask);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_table_consum_hw_addr_size_per_index(tbls_id_t tbl_id, uint32* hw_addr_size)
{
    uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);
    uint32 word_num = 0;

    if ((!entry_size)
        || (entry_size % DRV_BYTES_PER_WORD))
    {
        DRV_DBG_INFO("%% ERROR! tbl_id = %d 's entrySize(%d Bytes) is unreasonable in driver dataBase!!\n", tbl_id, entry_size);
        return DRV_E_INVALID_PARAMETER;
    }

    *hw_addr_size = 0;
    word_num = entry_size / DRV_BYTES_PER_WORD;

    switch (word_num) /* wordNum */
    {
    case 1:         /* 1 word */
        *hw_addr_size = 1;
        break;

    case 2:         /* 2 words */
        *hw_addr_size = 2;
        break;

    case 3:
    case 4:
        *hw_addr_size = 4;
        break;

    case 5:
    case 6:
    case 7:
    case 8:
        *hw_addr_size = 8;
        break;

    default:               /* addr_unit(=2^n) >= wordNum > 2^(n-1) */
        if ((word_num <= 16) && (word_num >= 9))
        {
            *hw_addr_size = 16;
        }
        else if ((word_num >= 17) && (word_num <= 32))
        {
            *hw_addr_size = 32;
        }
        else if ((word_num >= 33) && (word_num <= 64))
        {
            *hw_addr_size = 64;
        }

        /* hold on by zhouw???? Pay attention !! */
        break;
    }

    return DRV_E_NONE;
}

/* Get hardware address according to tablid + index (do not include tcam key) */
int32
drv_greatbelt_table_get_hw_addr(tbls_id_t tbl_id, uint32 index, uint32* hw_addr)
{
    uint32 hw_data_base = 0;
    uint8 blk_id = 0;
    uint32 hw_addr_size_per_idx = 0; /* unit: word, per-index consume hw address size */

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("ERROR (_drv_greatbelt_chip_get_sw_address_by_tbl_id_and_index): tbl-%d, tcam key call the function only for non tcam key.\n",
                     tbl_id);
        return DRV_E_INVALID_TBL;
    }

    DRV_TBL_INDEX_VALID_CHECK(tbl_id, index);

    DRV_IF_ERROR_RETURN(drv_greatbelt_table_consum_hw_addr_size_per_index(tbl_id, &hw_addr_size_per_idx));

    if (drv_greatbelt_table_is_dynamic_table(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_DRV_BLOCK_NUM; blk_id++)
        {
            if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= DYNAMIC_START_INDEX(tbl_id, blk_id)) && (index <= DYNAMIC_END_INDEX(tbl_id, blk_id)))
            {
                hw_data_base = DYNAMIC_DATA_BASE(tbl_id, blk_id);
                break;
            }
        }

        if (MAX_DRV_BLOCK_NUM == blk_id)
        {
            return DRV_E_INVALID_ALLOC_INFO;
        }

        if (DYNAMIC_ACCESS_MODE(tbl_id) == DYNAMIC_8W_MODE)
        {
            if (index % 2)
            {
                DRV_DBG_INFO("ERROR!! get tbl_id %d index must be even! now index = %d\n", tbl_id, index);
                return DRV_E_INVALID_INDEX;
            }

            *hw_addr = hw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * DRV_ADDR_BYTES_PER_ENTRY;
        }
        else
        {
            *hw_addr = hw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * hw_addr_size_per_idx * DRV_BYTES_PER_WORD;
        }
    }
    else
    {
        hw_data_base = TABLE_DATA_BASE(tbl_id);
        DRV_IS_INTR_MULTI_WORD_TBL(tbl_id)
        {
            hw_addr_size_per_idx = 1;
        }
        *hw_addr = hw_data_base + index * hw_addr_size_per_idx * DRV_BYTES_PER_WORD;
    }

    return DRV_E_NONE;
}

/* Get hardware address according to tablid + index + data/mask flag (only tcam key) */
int32
drv_greatbelt_tcam_key_get_hw_addr(tbls_id_t tbl_id, uint32 index, bool is_data, uint32* hw_addr)
{
    uint32 addr_base = 0;

    if (!drv_greatbelt_table_is_tcam_key(tbl_id)) /* is not TcamKey Table */
    {
        return DRV_E_INVALID_PARAMETER;
    }

    addr_base = is_data ? TABLE_DATA_BASE(tbl_id) : TCAM_MASK_BASE(tbl_id);

    /* TCAM key's HW Address rule: 0 4 8 c 10 14 18 1c 20 24 28 2c 30 ... ... */
    *hw_addr = addr_base + TCAM_KEY_SIZE(tbl_id) * index;
    return DRV_E_NONE;
}

/* add function by shenhg for memory allocation V5.1 */
int8
drv_greatbelt_table_is_tcam_key(tbls_id_t tbl_id)
{

    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM)
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_greatbelt_table_is_dynamic_table(tbls_id_t tbl_id)
{

    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_DYNAMIC)
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_greatbelt_table_is_register(tbls_id_t tbl_id)
{

    if (TABLE_MAX_INDEX(tbl_id) == 1)
    {
        return TRUE;
    }

    return FALSE;
}


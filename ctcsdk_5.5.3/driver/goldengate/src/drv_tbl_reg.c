/**
  @file drv_tbl_reg.c

  @date 2010-07-22

  @version v5.1

  The file implement driver IOCTL defines and macros
*/

#include "goldengate/include/drv_lib.h"

/**********************************************************************************
              Define Global declarations, Typedef, define and Data Structure
***********************************************************************************/
extern uint8 current_chip_type;
extern host_type_t host_type;

/* global store driver all mem info */
extern tables_info_t drv_gg_tbls_list[MaxTblId_t];

 /*#if (SDK_WORK_PLATFORM == 0)*/
dup_address_offset_type_t gg_duplicate_addr_type = SLICE_Addr_All;
 /*#endif*/

/* OAM HASH and IPE LKP HASH is not the same at realize method, Need to think!!!*/
#define DRV_HASH_KEY_TBL_ID_VALID_CHECK(tbl_id)        \
if ((DsLpmIpv4Hash16Key_t != (tbl_id))                     \
    && (DsLpmIpv4Hash8Key_t != (tbl_id))                    \
    && (DsLpmIpv6Hash32Key_t != (tbl_id)))                  \
{\
    DRV_DBG_INFO("\nERROR! INVALID Hash Key MemID! MemID: %d, file:%s line:%d function:%s\n",tbl_id,__FILE__,__LINE__,__FUNCTION__);\
    return DRV_E_INVALID_TBL;\
}

/* TCAM KEY will have 4 chances for lookup */
#define DRV_IS_TCAM_ACL_KEY(tbl_id) \
    ((DsAclMacKey0_t == (tbl_id))  \
    ||(DsAclMacKey1_t == (tbl_id))  \
    ||(DsAclMacKey2_t == (tbl_id))  \
    ||(DsAclMacKey3_t == (tbl_id))  \
    ||(DsAclIpv4Key0_t == (tbl_id)) \
    ||(DsAclIpv4Key1_t == (tbl_id)) \
    ||(DsAclIpv4Key2_t == (tbl_id)) \
    ||(DsAclIpv4Key3_t == (tbl_id)) \
    ||(DsAclMplsKey0_t == (tbl_id)) \
    ||(DsAclMplsKey1_t == (tbl_id)) \
    ||(DsAclMplsKey2_t == (tbl_id)) \
    ||(DsAclMplsKey3_t == (tbl_id)) \
    ||(DsAclIpv6Key0_t == (tbl_id)) \
    ||(DsAclIpv6Key1_t == (tbl_id)))

/* index refer to sram_share_mem_id_t and sram_other_mem_id_t, please sync up with SPEC design!! */
/* spec V5.1 info is as follow: */
/* after spec V5.1 memory allocation ,the order of bitmap must same of share list id order */
uint16 gg_dynic_table_related_edram_bitmap[] =
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

#if (SDK_WORK_PLATFORM == 0)
uint32 slice1_tbl_id_list[] =
{
    DynamicDsHashLpmPipeIndexCam1_t,
    DynamicDsHashMacHashIndexCam1_t,
    DynamicDsAdDsFlowIndexCam1_t,
    DynamicDsHashIpfixHashIndexCam1_t,
    DynamicDsAdDsIpfixIndexCam1_t,
    DynamicDsAdDsMacIndexCam1_t,
    DynamicDsAdDsIpIndexCam1_t,
    DynamicDsAdDsNextHopIndexCam1_t,
    DynamicDsAdDsMetIndexCam1_t,
    DynamicDsAdEgrOamHashIndexCam1_t,
    DynamicDsHashUserIdHashIndexCam1_t,
    DynamicDsHashUserIdAdIndexCam1_t,
    DynamicDsHashSharedRam0ArbCtl1_t,
    DynamicDsHashSharedRam1ArbCtl1_t,
    DynamicDsHashSharedRam2ArbCtl1_t,
    DynamicDsHashSharedRam3ArbCtl1_t,
    DynamicDsHashSharedRam4ArbCtl1_t,
    DynamicDsHashSharedRam5ArbCtl1_t,
    DynamicDsHashSharedRam6ArbCtl1_t,
    DynamicDsAdDsIpMacRam0ArbCtl1_t,
    DynamicDsAdDsIpMacRam1ArbCtl1_t,
    DynamicDsAdDsIpMacRam2ArbCtl1_t,
    DynamicDsAdDsIpMacRam3ArbCtl1_t,
    DynamicDsHashUserIdHashRam0ArbCtl1_t,
    DynamicDsHashUserIdHashRam1ArbCtl1_t,
    DynamicDsHashUserIdAdRamArbCtl1_t,
    DynamicDsAdNextHopMetRam0ArbCtl1_t,
    DynamicDsAdNextHopMetRam1ArbCtl1_t,

    IpeFwdDiscardTypeStats1_t,
    EpeHdrEditDiscardTypeStats1_t,
};

uint32 parser_tbl_id_list[] =
{
    ParserEthernetCtl_t,
    ParserIpCtl_t,
    ParserIpChecksumCtl_t,
    ParserL3Ctl_t,
    ParserLayer2ProtocolCam_t,
    ParserLayer2ProtocolCamValid_t,
    ParserLayer2UdfCam_t,
    ParserLayer2UdfCamResult_t,
    ParserLayer3FlexCtl_t,
    ParserLayer3HashCtl_t          ,
    ParserLayer3LengthOpCtl_t      ,
    ParserLayer3ProtocolCam_t      ,
    ParserLayer3ProtocolCamValid_t ,
    ParserLayer3ProtocolCtl_t      ,
    ParserLayer3UdfCam_t           ,
    ParserLayer3UdfCamResult_t     ,
    ParserLayer4AchCtl_t           ,
    ParserLayer4AppCtl_t           ,
    ParserLayer4FlagOpCtl_t        ,
    ParserLayer4FlexCtl_t          ,
    ParserLayer4PortOpCtl_t        ,
    ParserLayer4PortOpSel_t        ,
    ParserLayer4UdfCam_t           ,
    ParserLayer4UdfCamResult_t     ,
    ParserMplsCtl_t                ,
    ParserPacketTypeMap_t          ,
    ParserPbbCtl_t                 ,
    ParserTrillCtl_t               ,
};

#endif

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/
/**
 @brief register a memory to the register directory and register field info of this memory together
*/
int32
drv_goldengate_tbl_register(tbls_id_t tbl_id, uint8 entry_type, uint32 data_offset, uint32 max_idx,
                     uint16 entry_size, uint8 num_f, fields_t* ptr_f)
{
    tables_info_t* ptr_tbl_info = NULL;

    DRV_PTR_VALID_CHECK(ptr_f)

    if (tbl_id >= MaxTblId_t)
    {
        return DRV_E_INVALID_TBL;
    }

    ptr_tbl_info = &drv_gg_tbls_list[tbl_id];
    ptr_tbl_info->slicetype = entry_type;
    ptr_tbl_info->addrs[0] = data_offset;
    ptr_tbl_info->entry = max_idx;
    ptr_tbl_info->word = entry_size;
    ptr_tbl_info->field_num = num_f;
    ptr_tbl_info->ptr_fields = ptr_f;

    return DRV_E_NONE;
}

/**
 @brief register a Memory field to the register field directory
*/
fields_t *
drv_goldengate_find_field(tbls_id_t tbl_id, fld_id_t field_id)
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
drv_goldengate_get_field_offset(tbls_id_t tbl_id, fld_id_t field_id, uint32* w_offset, uint32 *b_offset)
{
#ifdef SHANZ_NOTE
    fields_t* field = NULL;

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    field = drv_goldengate_find_field(tbl_id, field_id);
    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_goldengate_mem_get_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }

    if(NULL != w_offset)
    {
        *w_offset = field->word_offset;
    }

    if(NULL != b_offset)
    {
        *b_offset = field->bit_offset;
    }
#endif
    return DRV_E_NONE;
}

/**
 @brief Check tcam field in key size
*/
int32
drv_goldengate_tcam_field_chk(tbls_id_t tbl_id, fld_id_t field_id)
{
    uint32 w_offset     = 0;
    uint32 entry_size   = 0;
    uint32 key_size     = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_field_offset(tbl_id, field_id, &w_offset, NULL));

    entry_size  = TABLE_ENTRY_SIZE(tbl_id)/DRV_BYTES_PER_WORD;
    key_size    = TCAM_KEY_SIZE(tbl_id)/DRV_BYTES_PER_WORD;

    if((entry_size - w_offset) > key_size)
    {
        DRV_DBG_INFO("Error fld: %d, tbl_id %d\n", field_id, tbl_id);
        return DRV_E_INVALID_FLD;
    }

    return DRV_E_NONE;
}


#define SHIFT_LEFT_MINUS_ONE(bits)    ( ((bits) < 32) ? ((1 << (bits)) - 1) : -1)

/**
@brief Get a field  valued of a memory data entry
*/
uint32
drv_goldengate_get_field_value(tbls_id_t tbl_id, fld_id_t field_id,  void* ds)
{
    uint32 value = 0;
    drv_goldengate_get_field(tbl_id, field_id,                  ds, &value);
    return value;
}

/**
 @brief Get a field of a memory data entry
*/
int32
drv_goldengate_get_field(tbls_id_t tbl_id, fld_id_t field_id,
                void* ds, uint32* value)
{
    fields_t* field = NULL;
    int32 seg_id;
    segs_t* seg = NULL;
    uint8 uint32_num  = 0;
    uint8 uint32_idx  = 0;
    uint16 remain_len  = 0;
    uint16 cross_word  = 0;
    uint32 remain_value = 0;
    uint32 seg_value = 0;
    uint32* entry  = NULL;

    DRV_PTR_VALID_CHECK(ds)
    DRV_PTR_VALID_CHECK(value)
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    entry = ds;
    field = drv_goldengate_find_field(tbl_id, field_id);
    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_goldengate_mem_get_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }

    seg_id = (field->seg_num-1);
    while( seg_id >= 0)
    {
        seg = &(field->ptr_seg[seg_id]);
        seg_value = (entry[seg->word_offset] >> seg->start) & SHIFT_LEFT_MINUS_ONE(seg->bits);

        value[uint32_idx] = (seg_value << remain_len) | remain_value;
        if ((seg->bits + remain_len) == 32)
        {
            remain_value = 0;
            cross_word = 0;
            uint32_idx ++;
        }
        else if ((seg->bits + remain_len) > 32)
        {
            /*create new remain_value */
            remain_value = seg_value >> (32 - remain_len);
            cross_word = 1;
            uint32_idx ++;
        }
        else
        {
            /*create new remain_value */
            remain_value = (seg_value << remain_len) | remain_value;
            cross_word = 0;
        }
        /*create new remain_len */
        remain_len   = (seg->bits + remain_len) & 0x1F; /*rule out bits that exceeds 32*/
        seg_id --;
    }

    if(cross_word) /* last seg cross word */
    {
        value[uint32_idx] = remain_value;
    }


/* add to support BE cpu get/set field */
    if(uint32_num > 1)
    {
    }

#if (HOST_IS_LE == 0) /* value is be*/
    if(field->bits > 8)
    {
         /*drv_be_to_le(field->bits, value);*/
    }
#endif

    return DRV_E_NONE;
}



/**
 @brief Set a field of a memory data entry
*/
int32
drv_goldengate_set_field(tbls_id_t tbl_id, fld_id_t field_id,
                        void* ds, uint32 *value)
{
    fields_t* field = NULL;
    segs_t* seg = NULL;
    int32 seg_id = 0;
    uint8 cut_len =  0;
    uint8 array_idx = 0;
    uint32 seg_value = 0;
    uint32 value_a = 0;
    uint32 value_b = 0;
    uint32* entry  = NULL;


    DRV_PTR_VALID_CHECK(ds)
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    entry = ds;
    /* register field support check */
    field = drv_goldengate_find_field(tbl_id, field_id);

    if (field == NULL)
    {
        DRV_DBG_INFO("ERROR! (drv_goldengate_mem_set_field): memID-%d, field-%d is not supported\n", tbl_id, field_id);
        return DRV_E_INVALID_FLD;
    }


    seg_id = (field->seg_num-1);
    cut_len = 0;
#if 0 /* never check, even in debug tool */
{
    uint32 mask[4] = {0};
    for (array_idx = 0; array_idx < DRV_MAX_ARRAY_NUM; array_idx++ )
    {
        int32 bits;
        bits =  field->bits - (32 * array_idx);
        if (bits >= 32)
        {
            bits = 32;
        }
        else if (bits <=0)
        {
            bits = 0;
        }

        mask[array_idx] = SHIFT_LEFT_MINUS_ONE(bits) ;
        if (value[array_idx] & ~mask[array_idx])
        {
            /* check if value is too big for this field */
            DRV_DBG_INFO("ERROR! (drv_goldengate_mem_set_field): memID-%d, field-%d, value is too big for this field.\n", tbl_id, field_id);
            return DRV_E_FIELD_OVER;
        }
    }
}

#endif
    array_idx = 0;
    while( seg_id >= 0)
    {
        seg = &(field->ptr_seg[seg_id]);

        if ((cut_len + seg->bits) >= 32)
        {
            value_b = (value[array_idx + 1 ] & SHIFT_LEFT_MINUS_ONE((cut_len + seg->bits - 32)));
            value_a = (value[array_idx] >> cut_len) & SHIFT_LEFT_MINUS_ONE((32- cut_len));
            seg_value = (value_b << (32 - cut_len)) | value_a;

            array_idx++;
        }
        else
        {
            value_a = (value[array_idx] >> cut_len) & SHIFT_LEFT_MINUS_ONE(seg->bits);
            seg_value =  value_a;
        }

        entry[seg->word_offset] &= ~ (SHIFT_LEFT_MINUS_ONE(seg->bits)  << seg->start);
        entry[seg->word_offset] |= (seg_value << seg->start);

        cut_len = (cut_len + seg->bits) & 0x1F; /*rule out bits that exceeds 32*/

        seg_id--;
    }

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_goldengate_get_tbl_index_base(tbls_id_t tbl_id, uint32 index, uint8 *addr_offset)
{
    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
    {
        if (index >= (TABLE_MAX_INDEX(tbl_id)/2))
        {
            *addr_offset = 1;
        }
        else
        {
            *addr_offset = 0;
        }
    }
#if (SDK_WORK_PLATFORM == 0)
    else if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Duplicated)
    {
        if (gg_duplicate_addr_type == SLICE_Addr_0)
        {
            *addr_offset = 1;
        }
        else if (gg_duplicate_addr_type == SLICE_Addr_1)
        {
            *addr_offset = 2;
        }
    }
#endif
    else
    {
        *addr_offset = 0;
    }

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_goldengate_get_tbl_string_by_id(tbls_id_t tbl_id, char* name)
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
drv_goldengate_get_field_string_by_id(tbls_id_t tbl_id, fld_id_t field_id, char* name)
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
drv_goldengate_get_tbl_id_by_string(tbls_id_t* tbl_id, char* name)
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
drv_goldengate_get_field_id_by_string(tbls_id_t tbl_id, fld_id_t* field_id, char* name)
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
drv_goldengate_sram_ds_to_entry(tbls_id_t tbl_id, void* ds, uint32* entry)
{
    uint32* p_ds = NULL;
    uint32 ds_size = 0; /* unit: bytes */

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    ds_size = TABLE_ENTRY_SIZE(tbl_id);
    p_ds = (uint32 *)ds;    /* Here need to notice: if the ds size is less
                               than uint32, the imperative type conversion
                               may be produce problem. */

    sal_memcpy(entry, p_ds, ds_size);

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_goldengate_sram_entry_to_ds(tbls_id_t tbl_id, uint32* entry, void* ds)
{
    uint32* p_ds = NULL;
    uint32 ds_size = 0; /* unit: bytes */

    DRV_TBL_ID_VALID_CHECK(tbl_id);
    p_ds = (uint32 *)ds;    /* Here need to notice: if the ds size is less
                               than uint32, the imperative type conversion
                               may be produce problem. */

    ds_size = TABLE_ENTRY_SIZE(tbl_id);

    sal_memset((uint8 *)ds, 0, ds_size);

    sal_memcpy(p_ds, entry, ds_size);

    return DRV_E_NONE;
}

/**
 @brief
*/
int32
drv_goldengate_tcam_ds_to_entry(tbls_id_t tbl_id, void* ds, void* entry)
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

    p_ds = (tbl_entry_t *)ds;
    p_entry = (tbl_entry_t *)entry;
    ds_data = p_ds->data_entry;
    ds_mask = p_ds->mask_entry;
    entry_data = p_entry->data_entry;
    entry_mask = p_entry->mask_entry;

    DRV_PTR_VALID_CHECK(ds_data);
    DRV_PTR_VALID_CHECK(ds_mask);
    DRV_PTR_VALID_CHECK(entry_data);
    DRV_PTR_VALID_CHECK(entry_mask);

    ret = drv_goldengate_sram_ds_to_entry(tbl_id, ds_data, entry_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    ret = drv_goldengate_sram_ds_to_entry(tbl_id, ds_mask, entry_mask);
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
drv_goldengate_tcam_entry_to_ds (tbls_id_t tbl_id, void* entry, void* ds)
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

    p_ds = (tbl_entry_t *)ds;
    p_entry = (tbl_entry_t *)entry;
    ds_data = p_ds->data_entry;
    ds_mask = p_ds->mask_entry;
    entry_data = p_entry->data_entry;
    entry_mask = p_entry->mask_entry;

    DRV_PTR_VALID_CHECK(ds_data);
    DRV_PTR_VALID_CHECK(ds_mask);
    DRV_PTR_VALID_CHECK(entry_data);
    DRV_PTR_VALID_CHECK(entry_mask);

    ret = drv_goldengate_sram_entry_to_ds(tbl_id, entry_data, ds_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    ret = drv_goldengate_sram_entry_to_ds(tbl_id, entry_mask, ds_mask);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_table_consum_hw_addr_size_per_index(tbls_id_t tbl_id, uint32 *hw_addr_size)
{
    uint32 entry_size = TABLE_ENTRY_SIZE(tbl_id);
    uint32 word_num = 0;

    if ((!entry_size)
        || (entry_size%DRV_BYTES_PER_WORD))
    {
        DRV_DBG_INFO("%% ERROR! tbl_id = %d 's entrySize(%d Bytes) is unreasonable in driver dataBase!!\n", tbl_id, entry_size);
        return DRV_E_INVALID_PARAMETER;
    }

    *hw_addr_size = 0;
    word_num = entry_size/DRV_BYTES_PER_WORD;

    switch(word_num) /* wordNum */
    {
        case 1:     /* 1 word */
            *hw_addr_size = 1;
            break;
        case 2:     /* 2 words */
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
        default:           /* addr_unit(=2^n) >= wordNum > 2^(n-1) */
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
drv_goldengate_table_get_hw_addr(tbls_id_t tbl_id, uint32 index, uint32 *hw_addr, uint32 is_dump_cfg)
{
    uint8 blk_id = 0;
    uint8 addr_offset = 0;
    uint32 hw_data_base = 0;
    uint32 hw_addr_size_per_idx = 0; /* unit: word, per-index consume hw address size */
    uint32 map_index = 0, mid_index = 0;
    uint32 couple_mode = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    DRV_TBL_ID_VALID_CHECK(tbl_id);

    if (drv_goldengate_table_is_tcam_key(tbl_id)
        || drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_table_get_hw_addr): tbl-%d, tcam key call the function only for non tcam key.\n",
                       tbl_id);
        return DRV_E_INVALID_TBL;
    }

    DRV_TBL_INDEX_VALID_CHECK(tbl_id, index);

    DRV_IF_ERROR_RETURN(drv_goldengate_table_consum_hw_addr_size_per_index(tbl_id, &hw_addr_size_per_idx));

    if (drv_goldengate_table_is_dynamic_table(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_DRV_BLOCK_NUM; blk_id++)
        {
            DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));

            if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= DYNAMIC_START_INDEX(tbl_id, blk_id)) && (index <= DYNAMIC_END_INDEX(tbl_id, blk_id)))
            {
                hw_data_base = DYNAMIC_DATA_BASE(tbl_id, blk_id, addr_offset);
                break;
            }
        }

        if (MAX_DRV_BLOCK_NUM == blk_id)
        {
            return DRV_E_INVALID_ALLOC_INFO;
        }

        if (DYNAMIC_ACCESS_MODE(tbl_id) == DYNAMIC_8W_MODE)
        {
            if ((index%2) && (!is_dump_cfg))
            {
                DRV_DBG_INFO("ERROR!! get tbl_id %d index must be even! now index = %d\n", tbl_id, index);
                return DRV_E_INVALID_INDEX;
            }

            *hw_addr = hw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * DRV_ADDR_BYTES_PER_ENTRY;
        }
        else if (DYNAMIC_ACCESS_MODE(tbl_id) == DYNAMIC_16W_MODE)
        {
            if ((index%4) && (!is_dump_cfg))
            {
                DRV_DBG_INFO("ERROR!! get tbl_id %d index must be times of 4! now index = %d\n", tbl_id, index);
                return DRV_E_INVALID_INDEX;
            }

            *hw_addr = hw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * DRV_ADDR_BYTES_PER_ENTRY;
        }
        else
        {
            *hw_addr = hw_data_base + (index - DYNAMIC_START_INDEX(tbl_id, blk_id)) * hw_addr_size_per_idx * DRV_BYTES_PER_WORD;
        }
    }
    else if (drv_goldengate_table_is_tcam_ad(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_NOR_TCAM_NUM; blk_id++)
        {
            DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));

            if (!IS_BIT_SET(TCAM_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= TCAM_START_INDEX(tbl_id, blk_id)) && (index <= TCAM_END_INDEX(tbl_id, blk_id)))
            {
                if(couple_mode)
                {
                    mid_index = (TCAM_START_INDEX(tbl_id, blk_id)
                            + (TCAM_END_INDEX(tbl_id, blk_id) - TCAM_START_INDEX(tbl_id, blk_id) + 1)/2);

                    if(index >= mid_index)
                    {
                        hw_data_base = TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) + DRV_TCAM_COUPLE_MODE_BASE_NUM* DRV_ADDR_BYTES_PER_ENTRY;
                        map_index = index - mid_index;
                    }
                    else
                    {
                        hw_data_base = TCAM_DATA_BASE(tbl_id, blk_id, addr_offset);
                        map_index = index - TCAM_START_INDEX(tbl_id, blk_id);
                    }
                }
                else
                {
                    hw_data_base = TCAM_DATA_BASE(tbl_id, blk_id, addr_offset);
                    map_index = index - TCAM_START_INDEX(tbl_id, blk_id);
                }

                break;
            }
        }
        *hw_addr = hw_data_base + map_index * hw_addr_size_per_idx * DRV_BYTES_PER_WORD;
    }
    else if (drv_goldengate_table_is_lpm_tcam_ip_ad(tbl_id)
            ||drv_goldengate_table_is_lpm_tcam_nat_ad(tbl_id))
    {

        DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));
        hw_data_base = TABLE_DATA_BASE(tbl_id, 0);
        map_index = index;
        *hw_addr = hw_data_base + map_index * hw_addr_size_per_idx * DRV_BYTES_PER_WORD;
    }
    else
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));
        hw_data_base = TABLE_DATA_BASE(tbl_id, addr_offset);

        if (TABLE_ENTRY_TYPE(tbl_id) == SLICE_Cascade)
        {
            if (index >= (TABLE_MAX_INDEX(tbl_id)/2))
            {
                map_index = index - (TABLE_MAX_INDEX(tbl_id)/2);
            }
            else
            {
                map_index = index;
            }
        }
        else
        {
            map_index = index;
        }

        *hw_addr = hw_data_base + map_index *hw_addr_size_per_idx*DRV_BYTES_PER_WORD;
    }

    return DRV_E_NONE;
}

/* Get hardware address according to tablid + index + data/mask flag (only tcam key) */
int32
drv_goldengate_tcam_key_get_hw_addr(tbls_id_t tbl_id, uint32 index, bool is_data, uint32 *hw_addr)
{
    uint8 addr_offset = 0;
    uint32 blk_id = 0;
    uint32 addr_base = 0;
    uint32 map_index = 0, mid_index = 0;
    uint32 couple_mode = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    if ((!drv_goldengate_table_is_tcam_key(tbl_id))
        && (!drv_goldengate_table_is_lpm_tcam_key(tbl_id))) /* is not TcamKey Table */
    {
        DRV_DBG_INFO("ERROR (drv_goldengate_tcam_key_get_hw_addr): tbl-%d, non tcam key call the function only for tcam key.\n",
                       tbl_id);
        return DRV_E_INVALID_PARAMETER;
    }

    if (drv_goldengate_table_is_tcam_key(tbl_id))
    {
        for (blk_id = 0; blk_id < MAX_NOR_TCAM_NUM; blk_id++)
        {
            DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));

            if (!IS_BIT_SET(TCAM_BITMAP(tbl_id), blk_id))
            {
                continue;
            }

            if ((index >= TCAM_START_INDEX(tbl_id, blk_id)) && (index <= TCAM_END_INDEX(tbl_id, blk_id)))
            {
                if(couple_mode)
                {
                    mid_index = (TCAM_START_INDEX(tbl_id, blk_id)
                            + (TCAM_END_INDEX(tbl_id, blk_id) - TCAM_START_INDEX(tbl_id, blk_id) + 1)/2);

                    if(index >= mid_index)
                    {
                        addr_base = (is_data ? TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) : TCAM_MASK_BASE(tbl_id, blk_id, addr_offset))
                                + DRV_TCAM_COUPLE_MODE_BASE_NUM* DRV_ADDR_BYTES_PER_ENTRY;
                        map_index = index - mid_index;
                    }
                    else
                    {
                        addr_base = is_data ? TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) : TCAM_MASK_BASE(tbl_id, blk_id, addr_offset);
                        map_index = index - TCAM_START_INDEX(tbl_id, blk_id);
                    }
                }
                else
                {
                    addr_base = is_data ? TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) : TCAM_MASK_BASE(tbl_id, blk_id, addr_offset);
                    map_index = index - TCAM_START_INDEX(tbl_id, blk_id);
                }

                 /*addr_base = is_data ? TCAM_DATA_BASE(tbl_id, blk_id, addr_offset) : TCAM_MASK_BASE(tbl_id, blk_id, addr_offset);*/
                *hw_addr = addr_base + TCAM_KEY_SIZE(tbl_id)/DRV_BYTES_PER_ENTRY*DRV_ADDR_BYTES_PER_ENTRY* map_index;
                break;
            }
        }
    }
    else if (drv_goldengate_table_is_lpm_tcam_key(tbl_id))
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_get_tbl_index_base(tbl_id, index, &addr_offset));
        addr_base = is_data ? TABLE_DATA_BASE(tbl_id, addr_offset) : TCAM_MASK_BASE_LPM(tbl_id, addr_offset);
        if(drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbl_id))
        {
            *hw_addr = addr_base + TCAM_KEY_SIZE(tbl_id)* index;
        }
        else
        {
            *hw_addr = addr_base + TCAM_KEY_SIZE(tbl_id)* index/DRV_BYTES_PER_ENTRY*DRV_ADDR_BYTES_PER_ENTRY;
        }

    }
    else
    {
        DRV_DBG_INFO("ERROR!! tbl_id %d is not tcam key!\n", tbl_id);
        return DRV_E_INVALID_INDEX;
    }

    /* TCAM key's HW Address rule: 0 4 8 c 10 14 18 1c 20 24 28 2c 30 ... ... */
     /**hw_addr = addr_base + TCAM_KEY_SIZE(tbl_id)* index;*/
    return DRV_E_NONE;
}

int32 tcam_block_entry_num[] =
{
    DRV_TCAM_KEY0_MAX_ENTRY_NUM,
    DRV_TCAM_KEY1_MAX_ENTRY_NUM,
    DRV_TCAM_KEY2_MAX_ENTRY_NUM,
    DRV_TCAM_KEY3_MAX_ENTRY_NUM,
    DRV_TCAM_KEY4_MAX_ENTRY_NUM,
    DRV_TCAM_KEY5_MAX_ENTRY_NUM,
    DRV_TCAM_KEY6_MAX_ENTRY_NUM,
};

int32
drv_goldengate_get_tcam_640_offset(tbls_id_t tbl_id, uint32 *tcam_640_offset)
{
    uint32 blk_id = 0;
    uint32 offset = 0;


    for (blk_id = 0; blk_id < MAX_NOR_TCAM_NUM; blk_id++)
    {
        if (IS_BIT_SET(TCAM_BITMAP(tbl_id), blk_id))
        {
            offset += tcam_block_entry_num[blk_id];
        }
    }

    *tcam_640_offset = offset/2;

    return DRV_E_NONE;

}


/* add function by shenhg for memory allocation V5.1 */
int8
drv_goldengate_table_is_tcam_key(tbls_id_t tbl_id)
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
drv_goldengate_table_is_dynamic_table(tbls_id_t tbl_id)
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
drv_goldengate_table_is_tcam_ad(tbls_id_t tbl_id)
{

    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM_AD)
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_goldengate_table_is_lpm_tcam_key(tbls_id_t tbl_id)
{

    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if ((TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_LPM_TCAM_IP)
        && (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_LPM_TCAM_NAT))
    {
        return FALSE;
    }

    return TRUE;
}

/* SDK Modify */
int8
drv_goldengate_table_is_lpm_tcam_ad(tbls_id_t tbl_id)
{

    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if ((TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM_LPM_AD) &&
        (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM_NAT_AD))
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_goldengate_table_is_lpm_tcam_key_ip_prefix(tbls_id_t tbl_id)
{
/*
    if ((tbl_id == DsLpmTcamIpv440Key_t) || (tbl_id == DsLpmTcamIpv6160Key0_t))
    {
        return TRUE;
    }

    return FALSE;
*/
    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_LPM_TCAM_IP)
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_goldengate_table_is_lpm_tcam_ip_ad(tbls_id_t tbl_id) /* SDK Modify*/
{
    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM_LPM_AD)
    {
        return FALSE;
    }

    return TRUE;
}

int8
drv_goldengate_table_is_lpm_tcam_nat_ad(tbls_id_t tbl_id) /* SDK Modify */
{
    if (TABLE_EXT_INFO_PTR(tbl_id) == NULL)
    {
        return FALSE;
    }

    if (TABLE_EXT_INFO_TYPE(tbl_id) != EXT_INFO_TYPE_TCAM_NAT_AD)
    {
        return FALSE;
    }

    return TRUE;
}

#if (SDK_WORK_PLATFORM == 0)
int8
drv_goldengate_table_is_slice1(tbls_id_t tbl_id)
{
    uint32 i =0 ;

    for(i =0; i < sizeof(slice1_tbl_id_list)/sizeof(slice1_tbl_id_list[0]); i++)
    {
        if(tbl_id == slice1_tbl_id_list[i])
        {
            return TRUE;
        }
    }

    return FALSE;
}

int8
drv_goldengate_table_is_parser_tbl(tbls_id_t tbl_id)
{
    uint32 i =0 ;

    for(i =0; i < sizeof(parser_tbl_id_list)/sizeof(parser_tbl_id_list[0]); i++)
    {
        if(tbl_id == parser_tbl_id_list[i])
        {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

int8
drv_goldengate_get_flow_tcam_line_bitmap(tbls_id_t tbl_id)
{
    int bit_map = 0;

    if(tbl_id == DsAclQosL3Key160Ingr0_t
        || tbl_id == DsAclQosL3Key320Ingr0_t
        || tbl_id == DsAclQosMacKey160Ingr0_t
        || tbl_id == DsAclQosIpv6Key640Ingr0_t)
    {
        bit_map = 0x66;  /*TCAM 1,2,5,6*/
    }
    else if (tbl_id == DsAclQosL3Key160Ingr1_t
        || tbl_id == DsAclQosL3Key320Ingr1_t
        || tbl_id == DsAclQosMacKey160Ingr1_t
        || tbl_id == DsAclQosIpv6Key640Ingr1_t)
    {
        bit_map = 0x20;  /*TCAM 5*/
    }
    else if (tbl_id == DsAclQosL3Key160Ingr2_t
        || tbl_id == DsAclQosL3Key320Ingr2_t
        || tbl_id == DsAclQosMacKey160Ingr2_t
        || tbl_id == DsAclQosIpv6Key640Ingr2_t)
    {
        bit_map = 0x4;  /*TCAM 2*/
    }
    else if (tbl_id == DsAclQosL3Key160Ingr3_t
        || tbl_id == DsAclQosL3Key320Ingr3_t
        || tbl_id == DsAclQosMacKey160Ingr3_t
        || tbl_id == DsAclQosIpv6Key640Ingr3_t)
    {
        bit_map = 0x2;  /*TCAM 1*/
    }
    else if (tbl_id == DsAclQosL3Key160Egr0_t
        || tbl_id == DsAclQosL3Key320Egr0_t
        || tbl_id == DsAclQosMacKey160Egr0_t
        || tbl_id == DsAclQosIpv6Key640Egr0_t)
    {
        bit_map = 0x30;  /*TCAM 4,5*/
    }
    else if (tbl_id == DsUserId0L3Key160_t
        || tbl_id == DsUserId0L3Key320_t
        || tbl_id == DsUserId0MacKey160_t
        || tbl_id == DsUserId0Ipv6Key640_t)
    {
        bit_map = 0x68;  /*TCAM 3,5,6*/
    }
    else if (tbl_id == DsUserId1L3Key160_t
        || tbl_id == DsUserId1L3Key320_t
        || tbl_id == DsUserId1MacKey160_t
        || tbl_id == DsUserId1Ipv6Key640_t)
    {
        bit_map = 0x1;  /*TCAM 0*/
    }

    return bit_map;

}

extern int32 drv_goldengate_get_dynamic_ram_couple_mode(uint32 *couple_mode);

/* SDK Modify */
uint32
drv_goldengate_get_tcam_addr_offset(uint8 block_num, uint8 is_mask)
{
    uint32 hw_base_addr = 0;
    uint32 is_couple    = 0;
    uint32 factor       = 0;
    uint32 entry_num    = 0;
    uint8 block_id      = 0;
    uint32 block_entry[9] = {0, DRV_TCAM_KEY0_MAX_ENTRY_NUM, DRV_TCAM_KEY1_MAX_ENTRY_NUM, DRV_TCAM_KEY2_MAX_ENTRY_NUM
                            , DRV_TCAM_KEY3_MAX_ENTRY_NUM, DRV_TCAM_KEY4_MAX_ENTRY_NUM, DRV_TCAM_KEY5_MAX_ENTRY_NUM};

    drv_goldengate_get_dynamic_ram_couple_mode(&is_couple);
    factor = is_couple? 2: 1;

    for (block_id = 0 ; block_id <= block_num;  block_id++)
    {
        entry_num += block_entry[block_id];
    }

    hw_base_addr = is_mask ? (DRV_TCAM_MASK0_BASE_4W + entry_num*factor*DRV_ADDR_BYTES_PER_ENTRY)
                            : (DRV_TCAM_KEY0_BASE_4W + entry_num*factor*DRV_ADDR_BYTES_PER_ENTRY);

    return hw_base_addr;

}

/* SDK Modify */
uint32
drv_goldengate_get_tcam_ad_addr_offset(uint8 block_num)
{
    uint32 hw_base_addr = 0;
    uint32 is_couple    = 0;
    uint32 factor       = 0;
    uint32 entry_num    = 0;
    uint8 block_id      = 0;
    uint32 block_entry[7] = {0, DRV_TCAM_AD0_MAX_ENTRY_NUM, DRV_TCAM_AD1_MAX_ENTRY_NUM, DRV_TCAM_AD2_MAX_ENTRY_NUM
                            , DRV_TCAM_AD3_MAX_ENTRY_NUM, DRV_TCAM_AD4_MAX_ENTRY_NUM, DRV_TCAM_AD5_MAX_ENTRY_NUM};

    drv_goldengate_get_dynamic_ram_couple_mode(&is_couple);
    factor = is_couple? 2: 1;

    for (block_id = 0 ; block_id <= block_num;  block_id++)
    {
        entry_num += block_entry[block_id];
    }

    hw_base_addr = DRV_TCAM_AD0_BASE_4W + entry_num*factor*DRV_ADDR_BYTES_PER_ENTRY;

    return hw_base_addr;

}

uint32
drv_goldengate_get_tcam_entry_num(uint8 block_num, uint8 is_ad)
{
    uint32 entry_num    = 0;
    uint32 is_couple    = 0;
    uint32 factor       = 0;

    drv_goldengate_get_dynamic_ram_couple_mode(&is_couple);
    factor = is_couple? 2: 1;

    if (is_ad)
    {
        switch(block_num)
        {
            case 0:
                entry_num = DRV_TCAM_AD0_MAX_ENTRY_NUM * factor;
                break;
            case 1:
                entry_num = DRV_TCAM_AD1_MAX_ENTRY_NUM * factor;
                break;

            case 2:
                entry_num = DRV_TCAM_AD2_MAX_ENTRY_NUM * factor;
                break;

            case 3:
                entry_num = DRV_TCAM_AD3_MAX_ENTRY_NUM * factor;
                break;

            case 4:
                entry_num = DRV_TCAM_AD4_MAX_ENTRY_NUM * factor;
                break;

            case 5:
                entry_num = DRV_TCAM_AD5_MAX_ENTRY_NUM * factor;
                break;

            case 6:
                entry_num = DRV_TCAM_AD6_MAX_ENTRY_NUM * factor;
                break;

        }
    }
    else
    {
        switch(block_num)
        {
            case 0:
                entry_num = DRV_TCAM_KEY0_MAX_ENTRY_NUM * factor;
                break;
            case 1:
                entry_num = DRV_TCAM_KEY1_MAX_ENTRY_NUM * factor;
                break;

            case 2:
                entry_num = DRV_TCAM_KEY2_MAX_ENTRY_NUM * factor;
                break;

            case 3:
                entry_num = DRV_TCAM_KEY3_MAX_ENTRY_NUM * factor;
                break;

            case 4:
                entry_num = DRV_TCAM_KEY4_MAX_ENTRY_NUM * factor;
                break;

            case 5:
                entry_num = DRV_TCAM_KEY5_MAX_ENTRY_NUM * factor;
                break;

            case 6:
                entry_num = DRV_TCAM_KEY6_MAX_ENTRY_NUM * factor;
                break;
        }
    }
    return entry_num;
}


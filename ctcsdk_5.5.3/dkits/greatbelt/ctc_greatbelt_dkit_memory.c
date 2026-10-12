#include "sal.h"
#include "ctc_cli.h"
#include "greatbelt/include/drv_lib.h"
#include "ctc_dkit.h"
#include "ctc_dkit_error.h"
#include "ctc_dkit_api.h"
#include "ctc_greatbelt_dkit_memory.h"
#include "ctc_greatbelt_dkit.h"

/* refer to hash_db*/
enum ctc_dkit_memory_hash_key_type_e
{
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_XC_OAM = 0,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_HOST0 = 1,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_HOST1 = 2,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_FLOW = 3,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_IP_FIX = 4,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_USER_ID = 5,
    CTC_DKIT_MEMORY_HASH_KEY_TPYE_MAX
};
typedef enum ctc_dkit_memory_hash_key_type_e ctc_dkit_memory_hash_key_type_t;

int32
drv_greatbelt_dkit_get_tbl_string_by_id(uint8 lchip, tbls_id_t tbl_id, char* name)
{
    return drv_greatbelt_get_tbl_string_by_id(tbl_id, name);
}


STATIC char*
_ctc_greatbelt_dkit_memory_tbl_type_to_str(ext_content_type_t type)
{
    switch (type)
    {
        case EXT_INFO_TYPE_NORMAL:
            return "RAM";

        case EXT_INFO_TYPE_TCAM:
            return "TCAM";

        case EXT_INFO_TYPE_DYNAMIC:
            return "DYNAMIC";

        default:
            return "Invalid";
    }
}

STATIC char*
_ctc_greatbelt_dkit_memory_tbl_slice_type_to_str(uint8 type)
{
    switch (type)
    {
        case 0:
            return "SHARE";


        default:
            return "Invalid";
    }
}

STATIC char*
_ctc_greatbelt_dkit_memory_tblreg_dump_list_one(uint32 tbl_id, uint32* first_flag, uint8 detail)
{

    tables_info_t* tbl_ptr = NULL;
    ext_content_type_t ext_type = 0;
    char tbl_name[CTC_DKITS_RW_BUF_SIZE];

    tbl_ptr = TABLE_INFO_PTR(tbl_id);
    if (tbl_ptr->ptr_ext_info)
    {
        ext_type = tbl_ptr->ptr_ext_info->ext_content_type;
    }

    if (*first_flag)
    {
        if(detail)
        {
            CTC_DKIT_PRINT("%-5s %-10s %-6s %-7s %-8s %-7s %-9s %-20s\n",
                        "TblID", "Address",  "Number", "EntrySZ", "Fields", "Type", "Slice", "TblName");
            CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n");

        }
        else
        {

        }
        *first_flag = FALSE;
    }

    drv_greatbelt_get_tbl_string_by_id(tbl_id, (char*)tbl_name);
    if(detail)
    {
        CTC_DKIT_PRINT("%-5d 0x%08x %-6d %-7d %-8d %-7s %-9s %-20s\n",
                    tbl_id, tbl_ptr->hw_data_base,  tbl_ptr->max_index_num,
                    tbl_ptr->entry_size, tbl_ptr->num_fields, _ctc_greatbelt_dkit_memory_tbl_type_to_str(ext_type),
                    _ctc_greatbelt_dkit_memory_tbl_slice_type_to_str(0), tbl_name);
    }
    else
    {
        CTC_DKIT_PRINT(" %s\n",tbl_name);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_memory_tblreg_dump_chip_read_tbl_db_by_id(char* tbl_name, uint32 id, uint32 detail)
{

    tables_info_t* tbl_ptr = NULL;
    fields_t* fld_ptr = NULL;
    uint32 fld_idx = 0;
    uint32 first_flag = TRUE;

    tbl_ptr = TABLE_INFO_PTR(id);
    _ctc_greatbelt_dkit_memory_tblreg_dump_list_one(id, &first_flag, 1);

    if (detail)
    {
        /* descrption */
        CTC_DKIT_PRINT("==================================================================================\n");
        CTC_DKIT_PRINT("%-8s %-8s %-8s %-8s %-8s %-30s\n",
                "FldID","Word", "Bit", "SegLen","TotalLen", "Name");
        CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n");

        /* value */
        for (fld_idx = 0; fld_idx < tbl_ptr->num_fields; fld_idx++)
        {
            fld_ptr = &(tbl_ptr->ptr_fields[fld_idx]);

            CTC_DKIT_PRINT("%-8d %-8d %-8d %-8d %-30s\n",
                fld_idx,
                fld_ptr->word_offset,
                fld_ptr->bit_offset,
                fld_ptr->len,
                fld_ptr->ptr_field_name);
        }

    }
    CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n\n");

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_memory_str_to_upper(char* str)
{
    uint32  i = 0;

    /* convert the alphabets to upper case before comparison */
    for (i = 0; i < sal_strlen((char*)str); i++)
    {
        if (sal_isalpha((int)str[i]) && sal_islower((int)str[i]))
        {
            str[i] = sal_toupper(str[i]);
        }
    }

    return CLI_SUCCESS;
}



int32
_ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id( uint32 tbl_id, uint32 index, uint32* data_entry, char* regex)
{
    uint32 value[MAX_ENTRY_WORD] = {0};
    fields_t* fld_ptr = NULL;
    int32 fld_idx = 0;
    char str[35] = {0};
    char format[10] = {0};
    tables_info_t* tbl_ptr = NULL;
    uint8 uint32_id = 0;
    uint32 addr = 0;
    uint8 print_fld = 0;
    char match_str[100] = {0};


    tbl_ptr = TABLE_INFO_PTR(tbl_id);

    for (fld_idx = 0; fld_idx < tbl_ptr->num_fields; fld_idx++)
    {
        sal_memset(value, 0 , sizeof(value));
        fld_ptr = &(tbl_ptr->ptr_fields[fld_idx]);

        drv_greatbelt_get_field(tbl_id, fld_idx, data_entry, value);

        if (NULL == regex)
        {
            if (0 == fld_idx)
            {
                sal_sprintf(match_str, "%-8u%-8u%-13s%-9u%-40s", index, fld_idx, CTC_DKITS_HEX_FORMAT(str, format, value[0], 8), fld_ptr->len, fld_ptr->ptr_field_name);
            }
            else
            {
                sal_sprintf(match_str, "%-8s%-8u%-13s%-9u%-40s", "", fld_idx, CTC_DKITS_HEX_FORMAT(str, format, value[0], 8), fld_ptr->len, fld_ptr->ptr_field_name);
            }

            print_fld = 1;
        }
        else
        {
            sal_sprintf(match_str, "%-8u%-8u%-13s%-9u%-40s", index, fld_idx, CTC_DKITS_HEX_FORMAT(str, format, value[0], 8), fld_ptr->len, fld_ptr->ptr_field_name);

            print_fld = ctc_re_exec(match_str);
        }


        if (print_fld)
        {
            CTC_DKIT_PRINT("%s\n", match_str);
        }

    }

    if (NULL == regex)
    {
        CTC_DKIT_PRINT("-------------------------------------------------------------\n");
    }


    drv_greatbelt_table_get_hw_addr(tbl_id, index, &addr);
    CTC_DKIT_PRINT_DEBUG("%-6s %-10s  %-10s \n", "Word",  "Addr", "Value");
    for (uint32_id = 0; uint32_id < TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_WORD; uint32_id ++)
    {
        CTC_DKIT_PRINT_DEBUG("%-6u ", uint32_id);
        CTC_DKIT_PRINT_DEBUG("%-10s  ",   CTC_DKITS_HEX_FORMAT(str, format, addr + uint32_id * 4, 8));
        CTC_DKIT_PRINT_DEBUG("%-10s\n", CTC_DKITS_HEX_FORMAT(str, format, *(data_entry + uint32_id), 8));
    }


    return CLI_SUCCESS;
}

int32
_ctc_greatbelt_dkit_memory_show_tcam_by_data_tbl_id(uint32 tbl_id, uint32 index, uint32* data_entry, uint32* mask_entry, char* regex)
{
    uint32 value[MAX_ENTRY_WORD] = {0};
    uint32 mask[MAX_ENTRY_WORD] = {0};
    fields_t* fld_ptr = NULL;
    int32 fld_idx = 0;
    tables_info_t* tbl_ptr = NULL;
    uint8 uint32_id = 0;
    uint32 addr = 0;
    uint8 print_fld = 0;
    char match_str[100] = {0};


    tbl_ptr = TABLE_INFO_PTR(tbl_id);


    for (fld_idx = 0; fld_idx < tbl_ptr->num_fields; fld_idx++)
    {
        sal_memset(value, 0 , sizeof(value));
        sal_memset(mask, 0 , sizeof(mask));
        fld_ptr = &(tbl_ptr->ptr_fields[fld_idx]);
        drv_greatbelt_get_field(tbl_id, fld_idx, data_entry, value);
        drv_greatbelt_get_field(tbl_id, fld_idx, mask_entry, mask);

        if (NULL == regex)
        {
            if (0 == fld_idx)
            {
                sal_sprintf(match_str, "%-6d %-6d 0x%08x 0x%08x %-6d %-10s", index,  fld_idx, value[0], mask[0], fld_ptr->len, fld_ptr->ptr_field_name);
            }
            else
            {
                sal_sprintf(match_str, "%-6s %-6d 0x%08x 0x%08x %-6d %-10s", "", fld_idx, value[0], mask[0], fld_ptr->len, fld_ptr->ptr_field_name);
            }

            print_fld = 1;
        }
        else
        {
            sal_sprintf(match_str, "%-6d %-6d 0x%08x 0x%08x %-6d %-10s", index,  fld_idx, value[0], mask[0], fld_ptr->len, fld_ptr->ptr_field_name);

            print_fld = ctc_re_exec(match_str);
        }


        if (print_fld)
        {
            CTC_DKIT_PRINT("%s\n", match_str);
        }

    }

    if (NULL == regex)
    {
        CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n");
    }

    drv_greatbelt_tcam_key_get_hw_addr(tbl_id, index, TRUE, &addr);
    CTC_DKIT_PRINT_DEBUG("%-6s %-10s  %-10s \n", "Word",  "Addr", "Value");
    for (uint32_id = 0; uint32_id < TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_WORD; uint32_id ++)
    {
        CTC_DKIT_PRINT_DEBUG("%-6d 0x%08x 0x%08x\n", uint32_id, addr + uint32_id*4, *(data_entry + uint32_id));
    }

    return CLI_SUCCESS;

}


STATIC int32
_ctc_greatbelt_dkit_memory_list_table(ctc_dkit_memory_para_t* p_memory_para)
{
#define CTC_GOLDENGEAT_LIST_MAX_NUM   9600
    uint32  *ids = NULL;
    char*    upper_name = NULL;
    char*    upper_name1 = NULL;
    char*    upper_name_grep = NULL;
    uint32  i = 0;
    uint32  id_index = 0;
    uint32  first_flag = TRUE;
    char* p_char = NULL;
    tables_info_t* tbl_ptr = NULL;

    DKITS_PTR_VALID_CHECK(p_memory_para);

    ids = (uint32 *)sal_malloc(CTC_GOLDENGEAT_LIST_MAX_NUM * sizeof(uint32));
    if(ids == NULL)
    {
        CTC_DKIT_PRINT("%% No memory\n");
        return CLI_ERROR;
    }
    upper_name = (char*)sal_malloc(CTC_DKITS_RW_BUF_SIZE * sizeof(char));
    if(upper_name == NULL)
    {
        CTC_DKIT_PRINT("%% No memory\n");
        goto clean3;
    }
    upper_name1 = (char*)sal_malloc(CTC_DKITS_RW_BUF_SIZE * sizeof(char));
    if(upper_name1 == NULL)
    {
        CTC_DKIT_PRINT("%% No memory\n");
        goto clean2;
    }
    upper_name_grep = (char*)sal_malloc(CTC_DKITS_RW_BUF_SIZE * sizeof(char));
    if(upper_name_grep == NULL)
    {
        CTC_DKIT_PRINT("%% No memory\n");
        goto clean1;
    }

    sal_memset(ids, 0, CTC_GOLDENGEAT_LIST_MAX_NUM * sizeof(uint32));
    sal_memset(upper_name, 0, CTC_DKITS_RW_BUF_SIZE * sizeof(char));
    sal_memset(upper_name1, 0, CTC_DKITS_RW_BUF_SIZE * sizeof(char));
    sal_memset(upper_name_grep, 0, CTC_DKITS_RW_BUF_SIZE * sizeof(char));

    if (0 == sal_strlen((char*)p_memory_para->buf))
    {
        /* list all */
        CTC_DKIT_PRINT("List all %d entries\n", MaxTblId_t);

        for (i = 0; i < MaxTblId_t; i++)
        {
            _ctc_greatbelt_dkit_memory_tblreg_dump_list_one(i, &first_flag, p_memory_para->detail);
        }

        goto clean;
    }

    sal_strncpy((char*)upper_name, (char*)p_memory_para->buf, CTC_DKITS_RW_BUF_SIZE);
    _ctc_greatbelt_dkit_memory_str_to_upper(upper_name);

    if (p_memory_para->grep)
    {
        sal_strncpy((char*)upper_name_grep, (char*)p_memory_para->buf2, CTC_DKITS_RW_BUF_SIZE);
        _ctc_greatbelt_dkit_memory_str_to_upper(upper_name_grep);
    }

    for (i = 0; i < MaxTblId_t; i++)
    {
        drv_greatbelt_get_tbl_string_by_id(i, (char*)upper_name1);
        _ctc_greatbelt_dkit_memory_str_to_upper(upper_name1);

        p_char = (char*)sal_strstr((char*)upper_name1, (char*)upper_name);
        if (p_char)
        {
            tbl_ptr = TABLE_INFO_PTR(i);
            if (0 == tbl_ptr->max_index_num)
            {
                continue;
            }

            if (sal_strlen((char*)upper_name1) == sal_strlen((char*)upper_name))
            {
                /* list full matched */
                _ctc_greatbelt_dkit_memory_tblreg_dump_chip_read_tbl_db_by_id(upper_name1, i, p_memory_para->detail);
                goto clean;
            }

            if (p_memory_para->grep)
            {
                p_char = NULL;
                p_char = (char*)sal_strstr((char*)upper_name1, (char*)upper_name_grep);
                if (p_char)
                {
                    ids[id_index] = i;
                    id_index++;
                }
            }
            else
            {
                ids[id_index] = i;
                id_index++;
            }

            if (id_index >= CTC_GOLDENGEAT_LIST_MAX_NUM)
            {
                break;
            }
        }
    }

    if (id_index == 0)
    {
        CTC_DKIT_PRINT("%% Not found %s \n", p_memory_para->buf);
        goto clean;
    }

    for (i = 0; i < id_index; i++)
    {
        /* list matched */
        _ctc_greatbelt_dkit_memory_tblreg_dump_list_one(ids[i], &first_flag, p_memory_para->detail);
    }

    if (p_memory_para->detail)
    {
        CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n");
    }
    if (id_index >= CTC_GOLDENGEAT_LIST_MAX_NUM)
    {
        CTC_DKIT_PRINT("Too much matched, only display first %d entries. \n", CTC_GOLDENGEAT_LIST_MAX_NUM);
    }
    CTC_DKIT_PRINT("\n");

clean:
    if(NULL != upper_name_grep)
    {
        sal_free(upper_name_grep);
    }
clean1:
    if(NULL != upper_name1)
    {
        sal_free(upper_name1);
    }
clean2:
    if(NULL != upper_name)
    {
        sal_free(upper_name);
    }
clean3:
    if(NULL != ids)
    {
        sal_free(ids);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_memory_read_table(ctc_dkit_memory_para_t* p_memory_para)
{
    char* tbl_name = NULL;
    uint8 chip = 0;
    int32 start_index = 0;
    uint32 count = 0;
    uint8 step = 0;
    uint32 tbl_id = 0;
    int32 ret = CLI_SUCCESS;
    uint32 cmd = 0;
    tbl_entry_t entry;
    uint32* data_entry = NULL;
    uint32* mask_entry = NULL;
    int32 i = 0;
    uint32 index = 0;
    char* regex = NULL;

    DKITS_PTR_VALID_CHECK(p_memory_para);

    data_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    mask_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    if (!data_entry || !mask_entry)
    {
        ret = CLI_ERROR;
        goto clean;
    }

    sal_memset(data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
    sal_memset(mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));

    chip = p_memory_para->param[1];
    start_index = p_memory_para->param[2];
    count = p_memory_para->param[4];
    step = p_memory_para->param[5];
    tbl_name = (char*)p_memory_para->buf;

    if (p_memory_para->grep)
    {
        regex = (char*)p_memory_para->buf2;
    }

    if (NULL != regex)
    {
        char* p = NULL;

        p = ctc_re_comp(regex);

        if (NULL != p)
        {
            CTC_DKIT_PRINT("Not match regex  %s: %s\n", p, regex);
            ret = DRV_E_NONE;
            goto clean;
        }
    }

    if (CTC_DKITS_INVALID_TBL_ID != p_memory_para->param[3])
    {
        tbl_id = p_memory_para->param[3];
        ret = drv_greatbelt_get_tbl_string_by_id(tbl_id , tbl_name);
        if (ret != DRV_E_NONE)
        {
            CTC_DKIT_PRINT("%% Not found 0x%x\n", tbl_id);
            ret = CLI_ERROR;
            goto clean;
        }
        CTC_DKIT_PRINT_DEBUG("read %s %d count %d step %d\n", tbl_name, index, count, step);
    }
    else
    {
        if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&tbl_id, tbl_name))
        {
            CTC_DKIT_PRINT("%% Not found %s\n", tbl_name);
            ret = CLI_ERROR;
            goto clean;
        }
    }

    if (drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        /* TCAM */
        CTC_DKIT_PRINT("\n%-6s %-6s %-10s %-10s %-6s %-10s\n", "Index", "FldID", "Value", "Mask", "BitLen", "Name");
        CTC_DKIT_PRINT("----------------------------------------------------------------------------------\n");
    }
    else
    {
        /* non-TCAM */
        CTC_DKIT_PRINT("\n%-8s%-8s%-13s%-9s%-40s\n", "Index", "FldID", "Value", "BitLen", "FldName");
        CTC_DKIT_PRINT("-------------------------------------------------------------\n");
    }

    for (i = 0; i < count; i++)
    {
        index = (start_index + (i * step));
        if (drv_greatbelt_table_is_tcam_key(tbl_id))
        {
            entry.data_entry = data_entry;
            entry.mask_entry = mask_entry;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            ret = DRV_IOCTL(chip, index, cmd, &entry);
            if (CLI_SUCCESS == ret)
            {
                _ctc_greatbelt_dkit_memory_show_tcam_by_data_tbl_id(tbl_id, index, data_entry, mask_entry, regex);
            }
        }
        else
        {
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            ret = DRV_IOCTL(chip, index, cmd, data_entry);
            if (CLI_SUCCESS == ret)
            {
                _ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id(tbl_id, index, data_entry, regex);
            }
        }
    }
    if (ret < 0)
    {
        CTC_DKIT_PRINT("%% Read index %d failed %d\n", index, ret);
    }
clean:
    if (data_entry)
    {
        sal_free(data_entry);
    }
    if (mask_entry)
    {
        sal_free(mask_entry);
    }

    return ret;
}

STATIC int32
_ctc_greatbelt_dkit_memory_write_table(ctc_dkit_memory_para_t* p_memory_para)
{
    char* reg_name = NULL;
    char* fld_name = NULL;
    uint8 chip = 0;
    int32 index = 0;
    uint32 reg_tbl_id = 0;
    uint32* p_value = NULL;
    uint32* p_mask = NULL;
    uint32 mask[MAX_ENTRY_WORD] = {0};
    uint32 value[MAX_ENTRY_WORD] = {0};
    uint32 cmd = 0;
    int32 ret = 0;
    tables_info_t* tbl_ptr = NULL;
    uint32 fld_id = 0;
    int32 fld_ok = FALSE;
    tbl_entry_t field_entry;

    DKITS_PTR_VALID_CHECK(p_memory_para);

    chip = p_memory_para->param[1];
    index = p_memory_para->param[2];
    p_value =(uint32*)p_memory_para->value;
    p_mask = (uint32*)p_memory_para->mask;
    reg_name = (char*)p_memory_para->buf;
    fld_name = (char*)p_memory_para->buf2;


    if (CTC_DKITS_INVALID_TBL_ID != p_memory_para->param[3])
    {
        reg_tbl_id = p_memory_para->param[3];
        ret = drv_greatbelt_get_tbl_string_by_id(reg_tbl_id , reg_name);
        if (ret != DRV_E_NONE)
        {
            CTC_DKIT_PRINT("%% Not found 0x%x\n", reg_tbl_id);
            return CLI_ERROR;
        }
    }
    else
    {
        if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&reg_tbl_id, reg_name))
        {
            CTC_DKIT_PRINT("%% Not found %s\n", reg_name);
            return CLI_ERROR;
        }
    }

    if (p_memory_para->param[4] != 0xFFFFFFFF)
    {
        fld_id = p_memory_para->param[4];
        drv_greatbelt_get_field_string_by_id(reg_tbl_id , fld_id, fld_name);
    }
    else
    {
        if (DRV_E_NONE != drv_greatbelt_get_field_id_by_string(reg_tbl_id, &fld_id, fld_name))
        {
            CTC_DKIT_PRINT("%% Not found %s\n", fld_name);
            return CLI_ERROR;
        }
    }

    CTC_DKIT_PRINT("Write %s %d %s\n", reg_name, index, fld_name);

    tbl_ptr = TABLE_INFO_PTR(reg_tbl_id);
    if (fld_id < tbl_ptr->num_fields)
    {
        fld_ok = TRUE;
    }

    if (!fld_ok)
    {
        CTC_DKIT_PRINT("%% %s has no field %d\n", reg_name, fld_id);
        return CLI_SUCCESS;
    }

    cmd = DRV_IOR(reg_tbl_id, DRV_ENTRY_FLAG);
    if (drv_greatbelt_table_is_tcam_key(reg_tbl_id))
    {
        field_entry.data_entry = value;
        field_entry.mask_entry = mask;
        ret = DRV_IOCTL(chip, index, cmd, &field_entry);
    }
    else
    {
        ret = DRV_IOCTL(chip, index, cmd, value);
    }

    cmd = DRV_IOW(reg_tbl_id, DRV_ENTRY_FLAG);
    if (drv_greatbelt_table_is_tcam_key(reg_tbl_id))
    {
        drv_greatbelt_set_field(reg_tbl_id, fld_id, field_entry.data_entry, *p_value);
        drv_greatbelt_set_field(reg_tbl_id, fld_id, field_entry.mask_entry, *p_mask);
        ret = DRV_IOCTL(chip, index, cmd, &field_entry);
    }
    else
    {
        drv_greatbelt_set_field(reg_tbl_id, fld_id, value, *p_value);
        if (((DsEthMep_t == reg_tbl_id) || (DsEthRmep_t == reg_tbl_id) || (DsBfdMep_t == reg_tbl_id) || (DsBfdRmep_t == reg_tbl_id)))
        {
            sal_memset(mask, 0xFF, sizeof(mask));
            *p_value = 0;
            drv_greatbelt_set_field(reg_tbl_id, fld_id, mask, *p_value);
            field_entry.data_entry = value;
            field_entry.mask_entry = mask;
            ret = DRV_IOCTL(chip, index, cmd, &field_entry);
        }
        else
        {
            ret = DRV_IOCTL(chip, index, cmd, value);
        }
    }

    if (ret < 0)
    {
        CTC_DKIT_PRINT("%% Write %s index %d failed %d\n",  "tbl-reg", index, ret);
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_memory_reset_table(ctc_dkit_memory_para_t* p_memory_para)
{
     return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_memory_show_tbl_by_addr(ctc_dkit_memory_para_t* p_memory_para)
{

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_memory_show_tbl_by_data(ctc_dkit_memory_para_t* p_memory_para)
{
    char* reg_name = NULL;
    uint32 reg_tbl_id = 0;
    uint32* p_value = NULL;
    uint32* p_mask_value = NULL;
    int32 ret = 0;

    p_value =(uint32*)p_memory_para->value;
    p_mask_value = (uint32*)p_memory_para->mask;
    reg_name = (char*)p_memory_para->buf;

    if (CTC_DKITS_INVALID_TBL_ID != p_memory_para->param[3])
    {
        reg_tbl_id = p_memory_para->param[3];
        drv_greatbelt_get_tbl_string_by_id(reg_tbl_id , reg_name);
    }
    else
    {
        if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&reg_tbl_id, reg_name))
        {
            CTC_DKIT_PRINT("%% Not found %s\n", reg_name);
            return CLI_ERROR;
        }
    }

    if (drv_greatbelt_table_is_tcam_key(reg_tbl_id))
    {
        _ctc_greatbelt_dkit_memory_show_tcam_by_data_tbl_id(reg_tbl_id, 0, p_value, p_mask_value, NULL);
    }
    else
    {
        ret = _ctc_greatbelt_dkit_memory_show_ram_by_data_tbl_id(reg_tbl_id, 0, p_value, NULL);
    }

    if (ret < 0)
    {
        CTC_DKIT_PRINT("%% show %s data failed %d\n", reg_name, ret);
    }

    return CLI_SUCCESS;
}

int32
_ctc_greatbelt_dkit_memory_check_table_field_id(uint8 lchip, ctc_dkit_tbl_reg_wr_t* tbl_info, uint32* p_table_id, uint16* p_field_id)
{
    tbls_id_t table_id = 0;
    fld_id_t field_id = 0;

    if (DRV_E_NONE != drv_greatbelt_get_tbl_id_by_string(&table_id, (char*)tbl_info->table_name))
    {
        CTC_DKIT_PRINT_DEBUG("invalid table name %s\n", tbl_info->table_name);
        return DKIT_E_INVALID_PARAM;
    }

    if (DRV_E_NONE != drv_greatbelt_get_field_id_by_string(table_id, &field_id, (char*)tbl_info->field_name))
    {
        CTC_DKIT_PRINT_DEBUG("invalid field name %s\n", tbl_info->field_name);
        return DKIT_E_INVALID_PARAM;
    }

    if (NULL == drv_greatbelt_find_field(table_id, field_id))
    {
        return DKIT_E_INVALID_PARAM;
    }

    *p_table_id = table_id;
    *p_field_id = field_id;

    return DKIT_E_NONE;
}

int32
ctc_greatbelt_dkit_memory_read_table(uint8 lchip, void* p_param)
{
    uint32 cmd = 0;
    tbl_entry_t entry;
    uint32* data_entry = NULL;
    uint32 table_id_temp = 0;
    uint16 field_id_temp = 0;
    ctc_dkit_tbl_reg_wr_t* tbl_info = (ctc_dkit_tbl_reg_wr_t*)p_param;

    CTC_DKIT_PTR_VALID_CHECK(tbl_info);
    CTC_DKIT_PTR_VALID_CHECK(tbl_info->table_name);
    CTC_DKIT_PTR_VALID_CHECK(tbl_info->field_name);
    CTC_DKIT_LCHIP_CHECK(lchip);
    CTC_DKIT_PRINT_DEBUG("%s()\n", __FUNCTION__);


    CTC_DKIT_ERROR_RETURN(_ctc_greatbelt_dkit_memory_check_table_field_id(lchip, tbl_info, &table_id_temp, &field_id_temp));

    CTC_DKIT_PRINT_DEBUG("lchip:%d tbl_id:%d index:%d field_id:%d\n", lchip, table_id_temp,
                                           tbl_info->table_index, field_id_temp);

    if (drv_greatbelt_table_is_tcam_key(table_id_temp))
    {
        entry.data_entry = tbl_info->value;
        entry.mask_entry = tbl_info->mask;
        cmd = DRV_IOR(table_id_temp, field_id_temp);
        CTC_DKIT_ERROR_RETURN(DRV_IOCTL(lchip, tbl_info->table_index, cmd, &entry));
    }
    else
    {
        data_entry = tbl_info->value;
        cmd = DRV_IOR(table_id_temp, field_id_temp);
        CTC_DKIT_ERROR_RETURN(DRV_IOCTL(lchip, tbl_info->table_index, cmd, data_entry));
    }

    return DKIT_E_NONE;
}

int32
ctc_greatbelt_dkit_memory_write_table(uint8 lchip, void* p_param)
{
    uint32 cmd = 0;
    tbl_entry_t entry;
    uint32* p_value = NULL;
    uint32* p_mask = NULL;
    uint32 table_id_temp = 0;
    uint16 field_id_temp = 0;
    ctc_dkit_tbl_reg_wr_t* tbl_info = (ctc_dkit_tbl_reg_wr_t*)p_param;
    int32 ret = DKIT_E_NONE;

    CTC_DKIT_PTR_VALID_CHECK(tbl_info);
    CTC_DKIT_PTR_VALID_CHECK(tbl_info->table_name);
    CTC_DKIT_PTR_VALID_CHECK(tbl_info->field_name);
    CTC_DKIT_LCHIP_CHECK(lchip);
    CTC_DKIT_PRINT_DEBUG("%s()\n", __FUNCTION__);


    CTC_DKIT_ERROR_RETURN(_ctc_greatbelt_dkit_memory_check_table_field_id(lchip, tbl_info, &table_id_temp, &field_id_temp));

    CTC_DKIT_PRINT_DEBUG("lchip:%d tbl_id:%d index:%d field_id:%d\n", lchip, table_id_temp,
                                           tbl_info->table_index, field_id_temp);

    p_value = (uint32*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*CTC_MAX_TABLE_DATA_LEN);
    p_mask = (uint32*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(uint32)*CTC_MAX_TABLE_DATA_LEN);
    if ((NULL == p_value) || (NULL == p_mask))
    {
        ret = DKIT_E_NO_MEMORY;
        goto out;
    }

    cmd = DRV_IOR(table_id_temp, DRV_ENTRY_FLAG);
    if (drv_greatbelt_table_is_tcam_key(table_id_temp))
    {
        entry.data_entry = p_value;
        entry.mask_entry = p_mask;
        CTC_DKIT_ERROR_GOTO(DRV_IOCTL(lchip, tbl_info->table_index, cmd, &entry), ret, out);
    }
    else
    {
        CTC_DKIT_ERROR_GOTO(DRV_IOCTL(lchip, tbl_info->table_index, cmd, p_value), ret, out);
    }

    cmd = DRV_IOW(table_id_temp, DRV_ENTRY_FLAG);
    if (drv_greatbelt_table_is_tcam_key(table_id_temp))
    {
        if (DRV_ENTRY_FLAG != field_id_temp)
        {
            drv_greatbelt_set_field(table_id_temp, field_id_temp, entry.data_entry, *tbl_info->value);
            drv_greatbelt_set_field(table_id_temp, field_id_temp, entry.mask_entry, *tbl_info->mask);
        }
        else
        {
            entry.data_entry = tbl_info->value;
            entry.mask_entry = tbl_info->mask;
        }
        CTC_DKIT_ERROR_GOTO(DRV_IOCTL(lchip, tbl_info->table_index, cmd, &entry), ret, out);
    }
    else
    {
        if (DRV_ENTRY_FLAG != field_id_temp)
        {
            drv_greatbelt_set_field(table_id_temp, field_id_temp, p_value, *tbl_info->value);
        }
        else
        {
            sal_memcpy(p_value, tbl_info->value, sizeof(CTC_MAX_TABLE_DATA_LEN*sizeof(uint32)));
        }
        if (((DsEthMep_t == table_id_temp) || (DsEthRmep_t == table_id_temp) || (DsBfdMep_t == table_id_temp) || (DsBfdRmep_t == table_id_temp)))
        {
           if (DRV_ENTRY_FLAG != field_id_temp)
           {
                sal_memset(p_mask, 0xFF, sizeof(uint32)*CTC_MAX_TABLE_DATA_LEN);
                *tbl_info->value = 0;
                drv_greatbelt_set_field(table_id_temp, field_id_temp, p_mask, *tbl_info->value);
            }
            else
            {
                sal_memset(p_mask, 0x0, CTC_MAX_TABLE_DATA_LEN * sizeof(uint32));
            }
            entry.data_entry = p_value;
            entry.mask_entry = p_mask;
            CTC_DKIT_ERROR_GOTO(DRV_IOCTL(lchip, tbl_info->table_index, cmd, &entry), ret, out);
        }
        else
        {
            CTC_DKIT_ERROR_GOTO(DRV_IOCTL(lchip, tbl_info->table_index, cmd, p_value), ret, out);
        }
    }

out:
    if (p_value)
    {
        mem_free(p_value);
    }
    if (p_mask)
    {
        mem_free(p_mask);
    }
    return ret;
}


int32
ctc_greatbelt_dkit_memory_process(void* p_para)
{
    ctc_dkit_memory_para_t* p_memory_para = (ctc_dkit_memory_para_t*)p_para;

    DKITS_PTR_VALID_CHECK(p_para);
    CTC_DKIT_LCHIP_CHECK(p_memory_para->param[1]);
    switch (p_memory_para->type)
    {
        case CTC_DKIT_MEMORY_LIST:
            _ctc_greatbelt_dkit_memory_list_table(p_memory_para);
            break;
        case CTC_DKIT_MEMORY_READ:
            _ctc_greatbelt_dkit_memory_read_table(p_memory_para);
            break;
        case CTC_DKIT_MEMORY_WRITE:
            _ctc_greatbelt_dkit_memory_write_table(p_memory_para);
            break;
        case CTC_DKIT_MEMORY_RESET:
            _ctc_greatbelt_dkit_memory_reset_table(p_memory_para);
            break;
        case CTC_DKIT_MEMORY_SHOW_TBL_BY_ADDR:
            _ctc_greatbelt_dkit_memory_show_tbl_by_addr(p_memory_para);
            break;
        case CTC_DKIT_MEMORY_SHOW_TBL_BY_DATA:
            _ctc_greatbelt_dkit_memory_show_tbl_by_data(p_memory_para);
            break;
        default:
            break;
    }

    return CLI_SUCCESS;
}





#include "greatbelt/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_dkit_cli.h"
#include "ctc_dkit_common.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_drv.h"
#include "ctc_greatbelt_dkit_tcam.h"
#include "ctc_greatbelt_dkit_dump_tbl.h"
#include "ctc_greatbelt_dkit_memory.h"
#include "ctc_greatbelt_dkit_misc.h"
#include "ctc_greatbelt_dkit_dump_cfg.h"

typedef int32 (*travel_callback_fun_t)(void*, tbls_id_t, void*, void*, void*);

enum swap_direction_e
{
    HOST_TO_NETWORK,
    NETWORK_TO_HOST
};
typedef enum swap_direction_e swap_direction_t;

enum ctc_dkits_fib_hash_fun_type_e
{
    CTC_DKITS_FIB_HASH_FUN_TYPE_BRIDGE,
    CTC_DKITS_FIB_HASH_FUN_TYPE_FLOW,
    CTC_DKITS_FIB_HASH_FUN_TYPE_FCOE,
    CTC_DKITS_FIB_HASH_FUN_TYPE_TRILL,

    CTC_DKITS_FIB_HASH_FUN_TYPE_NUM
};

enum ctc_dkits_lpm_hash_fun_type_e
{
    CTC_DKITS_LPM_HASH_FUN_TYPE_IPV4,
    CTC_DKITS_LPM_HASH_FUN_TYPE_IPV6,
    CTC_DKITS_LPM_HASH_FUN_TYPE_IPMC_V4,
    CTC_DKITS_LPM_HASH_FUN_TYPE_IPMC_V6,

    CTC_DKITS_LPM_HASH_FUN_TYPE_NUM
};

enum ctc_dkits_ip_tcam_fun_type_e
{
    CTC_DKITS_IP_TCAM_FUN_TYPE_IPUC,
    CTC_DKITS_IP_TCAM_FUN_TYPE_IPMC,
    CTC_DKITS_IP_TCAM_FUN_TYPE_NAT_IPV4,
    CTC_DKITS_IP_TCAM_FUN_TYPE_NAT_IPV6,
    CTC_DKITS_IP_TCAM_FUN_TYPE_PBR_IPV4,
    CTC_DKITS_IP_TCAM_FUN_TYPE_PBR_IPV6,
    CTC_DKITS_IP_TCAM_FUN_TYPE_NUM
};

enum ctc_dkits_nh_fun_type_e
{
    CTC_DKITS_NH_TBL_TYPE_FWD,
    CTC_DKITS_NH_TBL_TYPE_MET,
    CTC_DKITS_NH_TBL_TYPE_NEXTHOP,
    CTC_DKITS_NH_TBL_TYPE_EDIT,

    CTC_DKITS_NH_TBL_TYPE_NUM
};

enum ctc_dkits_scl_hash_fun_type_e
{
    CTC_DKITS_SCL_HASH_FUN_TYPE_BRIDGE,
    CTC_DKITS_SCL_HASH_FUN_TYPE_IPV4,
    CTC_DKITS_SCL_HASH_FUN_TYPE_IPV6,
    CTC_DKITS_SCL_HASH_FUN_TYPE_IP_TUNNEL,
    CTC_DKITS_SCL_HASH_FUN_TYPE_PBB_TUNNEL,
    CTC_DKITS_SCL_HASH_FUN_TYPE_TRILL_TUNNEL,
    CTC_DKITS_SCL_HASH_FUN_TYPE_OAM,

    CTC_DKITS_SCL_HASH_FUN_TYPE_NUM
};

enum ctc_dkits_hash_tbl_module_e
{
    CTC_DKITS_HASH_FUN_TYPE_SCL,
    CTC_DKITS_HASH_FUN_TYPE_FIB,
    CTC_DKITS_HASH_FUN_TYPE_IP,
    CTC_DKITS_HASH_FUN_TYPE_NUM
};

enum ctc_dkits_acl_tcam_fun_type_e
{
    CTC_DKITS_ACL_TCAM_FUN_TYPE_BRIDGE,
    CTC_DKITS_ACL_TCAM_FUN_TYPE_IPV4,
    CTC_DKITS_ACL_TCAM_FUN_TYPE_IPV6,
    CTC_DKITS_ACL_TCAM_FUN_TYPE_MPLS,
    CTC_DKITS_ACL_TCAM_FUN_TYPE_NUM
};

enum ctc_dkits_scl_tcam_fun_type_e
{
    CTC_DKITS_SCL_TCAM_FUN_TYPE_BRIDGE,
    CTC_DKITS_SCL_TCAM_FUN_TYPE_IPV4,
    CTC_DKITS_SCL_TCAM_FUN_TYPE_IPV6,
    CTC_DKITS_SCL_TCAM_FUN_TYPE_NUM
};

enum ctc_dkits_format_type_e
{
    CTC_DKITS_FORMAT_TYPE_NONE,
    CTC_DKITS_FORMAT_TYPE_1BYTE,
    CTC_DKITS_FORMAT_TYPE_2BYTE,
    CTC_DKITS_FORMAT_TYPE_4BYTE,
    CTC_DKITS_FORMAT_TYPE_NUM
};
typedef enum ctc_dkits_format_type_e ctc_dkits_format_type_t;

enum ctc_dkits_dump_tbl_diff_flag_e
{
    CTC_DKITS_DUMP_TBL_DIFF_FLAG_TBL = 1U << 0,
    CTC_DKITS_DUMP_TBL_DIFF_FLAG_KEY = 1U << 1,
    CTC_DKITS_DUMP_TBL_DIFF_FLAG_AD  = 1U << 2,
    CTC_DKITS_DUMP_TBL_DIFF_FLAG_ALL = 3
};
typedef enum ctc_dkits_dump_tbl_diff_flag_e ctc_dkits_dump_tbl_diff_flag_t;

struct ctc_dkits_hash_key_filed_s
{
    tbls_id_t tblid;
    uint32    type;
    uint32    valid;
};
typedef struct ctc_dkits_hash_key_filed_s ctc_dkits_hash_key_filed_t;

enum ctc_dkits_hash_module_type_e
{
    CTC_DKITS_HASH_MODULE_SCL,
    CTC_DKITS_HASH_MODULE_FIB,
    CTC_DKITS_HASH_MODULE_IP,
    CTC_DKITS_HASH_MODULE_NUM
};
typedef enum ctc_dkits_hash_module_type_e ctc_dkits_hash_module_type_t;

#define DKITS_NO_AD_TBL 0xFFFF

extern int32
drv_greatbelt_get_host_type (uint8 lchip);

struct ctc_dkits_dump_tbl_block_data_s
{
    uint32 entry;
    uint8  repeat;
}__attribute__((packed));
typedef struct ctc_dkits_dump_tbl_block_data_s ctc_dkits_dump_tbl_block_data_t;

struct ctc_dkits_tcam_tbl_info_s
{
    uint32                         valid;
    uint32                         tcam_type;
    tbls_id_t                      ad_tblid;
};
typedef struct ctc_dkits_tcam_tbl_info_s ctc_dkits_tcam_tbl_info_t;

struct ctc_dkits_dump_hash_usage_s
{
    uint32  key[CTC_DKITS_HASH_FUN_TYPE_NUM][50][5];
    uint32  cam[CTC_DKITS_HASH_FUN_TYPE_NUM][50];
};
typedef struct ctc_dkits_dump_hash_usage_s ctc_dkits_dump_hash_usage_t;

struct ctc_dkits_dump_tbl_usage_s
{
    uint32  key[CTC_DKITS_HASH_FUN_TYPE_NUM][50][10][5];
    uint32  cam[CTC_DKITS_HASH_FUN_TYPE_NUM][50][10];
};
typedef struct ctc_dkits_dump_tbl_usage_s ctc_dkits_dump_tbl_usage_t;

/*Copy from drv_chip_io.c*/
enum tcam_mem_type
{
    DRV_INT_TCAM_RECORD_DATA = 0,
    DRV_INT_TCAM_RECORD_MASK = 1,
    DRV_INT_TCAM_RECORD_REG = 2,
    DRV_INT_LPM_TCAM_RECORD_DATA = 3,
    DRV_INT_LPM_TCAM_RECORD_MASK = 4,
    DRV_INT_LPM_TCAM_RECORD_REG = 5
};
typedef enum tcam_mem_type tcam_mem_type_e;

enum fib_tcam_operation_type_t
{
    FIB_CPU_ACCESS_REQ_READ_X = 0,
    FIB_CPU_ACCESS_REQ_READ_Y = 1,
    FIB_CPU_ACCESS_REQ_WRITE_DATA_MASK = 2,
    FIB_CPU_ACCESS_REQ_INVALID_ENTRY = 3
};
typedef enum fib_tcam_operation_type_t fib_tcam_op_tp_e;

enum int_tcam_operation_type_t
{
    CPU_ACCESS_REQ_READ_X = 1,
    CPU_ACCESS_REQ_READ_Y = 2,
    CPU_ACCESS_REQ_WRITE_DATA_MASK = 5,
    CPU_ACCESS_REQ_INVALID_ENTRY = 9
};
typedef enum int_tcam_operation_type_t int_tcam_op_tp_e;

tbls_id_t* gb_fib_hash_key_tbl[] =
{
    gb_fib_hash_bridge_key_tbl, gb_fib_hash_flow_key_tbl,
    gb_fib_hash_fcoe_key_tbl, gb_fib_hash_trill_key_tbl, NULL
};

tbls_id_t* gb_lpm_hash_key_tbl[] =
{
    gb_lpm_hash_ipuc_ipv4_key_tbl, gb_lpm_hash_ipuc_ipv6_key_tbl,
    gb_lpm_hash_ipmc_ipv4_key_tbl, gb_lpm_hash_ipmc_ipv6_key_tbl,
    NULL
};

tbls_id_t* gb_scl_hash_key_tbl[] =
{
    gb_scl_hash_bridge_key_tbl,  gb_scl_hash_ipv4_key_tbl,
    gb_scl_hash_ipv6_key_tbl,  gb_scl_hash_iptunnel_key_tbl,
    gb_scl_hash_pbb_tunnel_key_tbl,   gb_scl_hash_trill_tunnel_key_tbl,
    gb_scl_hash_oam_key_tbl,
    NULL
};

ctc_dkits_tcam_key_inf_t* gb_acl_tcam_key_inf[] =
{
    gb_acl_tcam_bridge_key_inf, gb_acl_tcam_ipv4_key_inf,
    gb_acl_tcam_ipv6_key_inf,   gb_acl_tcam_mpls_key_inf,
    NULL
};

ctc_dkits_tcam_key_inf_t* gb_scl_tcam_key_inf[] =
{
    gb_scl_tcam_bridge_key_inf, gb_scl_tcam_ipv4_key_inf,
    gb_scl_tcam_ipv6_key_inf,
    NULL
};

ctc_dkits_tcam_key_inf_t* gb_ip_tcam_key_inf[] =
{
    gb_ipuc_tcam_key_inf, gb_ipmc_tcam_key_inf,
    gb_ip_tcam_nat_ipv4_key_inf, gb_ip_tcam_nat_ipv6_key_inf,
    gb_ip_tcam_pbr_ipv4_key_inf, gb_ip_tcam_pbr_ipv6_key_inf, NULL
};

tbls_id_t* gb_nh_tbl[] =
{
    gb_nh_fwd_tbl, gb_nh_met_tbl, gb_nh_nexthop_tbl, gb_nh_edit_tbl, NULL
};

tbls_id_t **gb_hash_tbl[] =
{
    gb_scl_hash_key_tbl, gb_fib_hash_key_tbl, gb_lpm_hash_key_tbl, NULL
};

ctc_dkits_hash_key_filed_t gb_hash_key_field[3] =
{
    {DsUserIdDoubleVlanHashKey_t, DsUserIdDoubleVlanHashKey_HashType_f, DsUserIdDoubleVlanHashKey_Valid_f},
    {DsMacHashKey_t, DsMacHashKey_IsMacHash_f, DsMacHashKey_Valid_f},
    {DsLpmIpv4Hash16Key_t, DsLpmIpv4Hash16Key_HashType_f, DsLpmIpv4Hash16Key_Valid_f}
};

ctc_dkits_tcam_key_inf_t** gb_tcam_key_inf[] =
{
    gb_acl_tcam_key_inf, gb_scl_tcam_key_inf, gb_ip_tcam_key_inf, NULL
};

#define ________DKIT_DUMP_INTERNAL_FUNCTION_________
#define _____COMMON_____
extern uint32 ctc_greatbelt_dkit_drv_get_bus_list_num(void);
extern void ctc_greatbelt_dkits_dump_get_tbl_type(tbls_id_t tblid, ctc_dkits_dump_tbl_type_t* p_tbl_type);
extern void ctc_greatbelt_dkits_dump_diff_write(tbls_id_t tblid_, uint32 idx_, uint32 bits, char* p_field_name, uint32* p_src_value, uint32* p_dst_value, sal_file_t p_wf);

void
ctc_greatbelt_dkits_dump_diff_status(uint8 reset, tbls_id_t* p_tblid, uint32* p_idx)
{
    static tbls_id_t lastest_tblid = MaxTblId_t;
    static uint32 lastest_tblidx = 0xFFFFFFFF;
    if (reset)
    {
        lastest_tblid = MaxTblId_t;
        lastest_tblidx = 0xFFFFFFFF;
    }
    if (NULL != p_tblid)
    {
        if (MaxTblId_t == *p_tblid)
        {
            *p_tblid = lastest_tblid;
        }
        else
        {
            lastest_tblid = *p_tblid;
        }
    }
    if  (NULL != p_idx)
    {
        if (0xFFFFFFFF == *p_idx)
        {
            *p_idx = lastest_tblidx;
        }
        else
        {
            lastest_tblidx = *p_idx;
        }
    }

    return;
}

void
ctc_greatbelt_dkits_dump_diff_write(tbls_id_t tblid_, uint32 idx_, uint32 bits, char* p_field_name, uint32* p_src_value, uint32* p_dst_value, sal_file_t p_wf)
{
    tbls_id_t tblid = MaxTblId_t;
    uint32  fvi = 0;
    uint32  fwn = 0;
    uint32  idx = 0xFFFFFFFF;
    char str[100] = {0};
    char str2[100] = {0};
    char format[50] = {0};
    char format2[50] = {0};

    fwn = (bits / (sizeof(uint32) * 8)) + ((bits % (sizeof(uint32) * 8)) ? 1 : 0);
    ctc_greatbelt_dkits_dump_diff_status(0, &tblid, &idx);

    for (fvi = 0; fvi < fwn; fvi++)
    {
        if (0 == fvi)
        {
            if ((tblid != tblid_) || (idx != idx_))
            {
                tblid = tblid_;
                idx = idx_;
                ctc_greatbelt_dkits_dump_diff_status(0, &tblid, &idx);

                sal_sprintf(str, "%s[%u]", TABLE_NAME(tblid), idx);
                ctc_dkits_dump_cfg_brief_str(str, sal_strlen(TABLE_NAME(tblid)), ((int8)(sal_strlen(str)) - TBL_NAME_IDX_MAX_PRINT_LEN));
                sal_sprintf(str2, "%s", p_field_name);
                ctc_dkits_dump_cfg_brief_str(str2, sal_strlen(p_field_name), ((int8)(sal_strlen(str2)) - FIELD_NAME_MAX_PRINT_LEN));

                sal_sprintf(format, "%%-%ds%%-%ds", TBL_NAME_IDX_MAX_PRINT_LEN + COLUNM_SPACE_LEN,
                                                    FIELD_NAME_MAX_PRINT_LEN + COLUNM_SPACE_LEN);

                sal_fprintf(p_wf, format, str, str2);
                sal_sprintf(format2, "%%-%ds", DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_src_value[fvi], 8));
                sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_dst_value[fvi], 8));
                sal_fprintf(p_wf, "\n");
            }
            else
            {
                sal_sprintf(str2, "%s", p_field_name);
                ctc_dkits_dump_cfg_brief_str(str2, sal_strlen(p_field_name), ((int8)(sal_strlen(str2)) - FIELD_NAME_MAX_PRINT_LEN));

                sal_sprintf(format, "%%-%ds%%-%ds", TBL_NAME_IDX_MAX_PRINT_LEN + COLUNM_SPACE_LEN,
                                                    FIELD_NAME_MAX_PRINT_LEN + COLUNM_SPACE_LEN);

                sal_fprintf(p_wf, format, " ", str2);
                sal_sprintf(format2, "%%-%ds", DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_src_value[fvi], 8));
                sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_dst_value[fvi], 8));
                sal_fprintf(p_wf, "\n");
            }
        }
        else
        {
            sal_sprintf(format, "%%-%ds%%-%ds", TBL_NAME_IDX_MAX_PRINT_LEN + COLUNM_SPACE_LEN,
                                                FIELD_NAME_MAX_PRINT_LEN + COLUNM_SPACE_LEN);

            sal_fprintf(p_wf, format, " ", " ");
            sal_sprintf(format2, "%%-%ds", DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
            sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_src_value[fvi], 8));
            sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_dst_value[fvi], 8));
            sal_fprintf(p_wf, "\n");
        }
    }

    return;
}

int32
ctc_greatbelt_dkits_dump_diff_field(ctc_dkits_dump_key_tbl_info_t* p_key_tbl_info, ctc_dkits_dump_tbl_entry_t* p_src_tbl, ctc_dkits_dump_tbl_entry_t* p_dst_tbl, sal_file_t p_wf)
{
    uint32 tfi = 0, fwn = 0, fvi = 0, is_diff = 0;
    uint32 src_value[MAX_ENTRY_WORD] = {0};
    uint32 dst_value[MAX_ENTRY_WORD] = {0};
    fields_t* p_fld_ptr = NULL;
    tables_info_t* p_tbl_ptr = NULL;

    p_tbl_ptr = TABLE_INFO_PTR(p_key_tbl_info->tblid);

    for (tfi = 0; tfi < TABLE_FIELD_NUM(p_key_tbl_info->tblid); tfi++)
    {
        p_fld_ptr = &(p_tbl_ptr->ptr_fields[tfi]);

        sal_memset(src_value, 0 , sizeof(src_value));
        sal_memset(dst_value, 0 , sizeof(dst_value));
        drv_greatbelt_get_field(p_key_tbl_info->tblid, tfi, p_src_tbl->data_entry, src_value);
        drv_greatbelt_get_field(p_key_tbl_info->tblid, tfi, p_dst_tbl->data_entry, dst_value);

        fwn = (CTC_DKITS_FIELD_LEN(p_fld_ptr) / (sizeof(uint32) * 8)) + ((CTC_DKITS_FIELD_LEN(p_fld_ptr) % (sizeof(uint32) * 8)) ? 1 : 0);
        for (fvi = 0; fvi < fwn; fvi++)
        {
            if (src_value[fvi] != dst_value[fvi])
            {
                is_diff = 1;
                break;
            }
        }

        if (is_diff)
        {
            is_diff = 0;
            fwn = (CTC_DKITS_FIELD_LEN(p_fld_ptr) / (sizeof(uint32) * 8)) + ((CTC_DKITS_FIELD_LEN(p_fld_ptr) % (sizeof(uint32) * 8)) ? 1 : 0);

            sal_memset(src_value, 0 , sizeof(src_value));
            sal_memset(dst_value, 0 , sizeof(dst_value));
            drv_greatbelt_get_field(p_key_tbl_info->tblid, tfi, p_src_tbl->data_entry, src_value);
            drv_greatbelt_get_field(p_key_tbl_info->tblid, tfi, p_dst_tbl->data_entry, dst_value);

            ctc_greatbelt_dkits_dump_diff_write(p_key_tbl_info->tblid, p_key_tbl_info->idx, (uint32)CTC_DKITS_FIELD_LEN(p_fld_ptr),
                                       p_fld_ptr->ptr_field_name, src_value, dst_value, p_wf);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_select_format(ctc_dkits_dump_tbl_block_hdr_t* p_tbl_block_hdr, uint32* p_data_entry, uint32* p_mask_entry)
{
    uint8  repeat = 0;
    uint16 word = 0;
    uint32 tblid = CTC_DKITS_DUMP_BLOCK_HDR_TBLID(p_tbl_block_hdr);

    word = TABLE_ENTRY_SIZE(tblid) / 4;

    ctc_dkits_dump_evaluate_entry(word, p_data_entry, &repeat);
    if (p_mask_entry)
    {
        ctc_dkits_dump_evaluate_entry(word, p_mask_entry, &repeat);
    }

    if ((repeat * 4) > (word - repeat))
    {
        p_tbl_block_hdr->format = CTC_DKITS_FORMAT_TYPE_4BYTE;
    }
    else
    {
        p_tbl_block_hdr->format = CTC_DKITS_FORMAT_TYPE_NONE;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_format_entry(ctc_dkits_dump_tbl_block_hdr_t* p_tbl_block_hdr, uint32* p_entry, uint8* p_buf, uint16* p_buf_len)
{
    ctc_dkits_dump_tbl_block_data_t tbl_block_data;
    uint32 i = 0, j = 0, entry = 0;
    uint8  repeat = 0;
    uint32 tblid = CTC_DKITS_DUMP_BLOCK_HDR_TBLID(p_tbl_block_hdr);

    for (i = 0; i < TABLE_ENTRY_SIZE(tblid) / 4;)
    {
        repeat = 0;
        entry= *(p_entry + i);

        sal_memset(&tbl_block_data, 0, sizeof(ctc_dkits_dump_tbl_block_data_t));
        /* compress all zero word */
        for (j = i + 1; j < TABLE_ENTRY_SIZE(tblid) / 4; j++)
        {
            if (entry != *(p_entry + j))
            {
                break;
            }

            if (entry == *(p_entry + j))
            {
                repeat++;
            }
        }
        if (0 != repeat)
        {
            tbl_block_data.repeat = repeat;
            i = j;
        }
        else
        {
            i++;
        }
        tbl_block_data.repeat++;
        tbl_block_data.entry = entry;

        sal_memcpy(p_buf + *p_buf_len, &tbl_block_data, sizeof(ctc_dkits_dump_tbl_block_data_t));
        *p_buf_len += sizeof(ctc_dkits_dump_tbl_block_data_t);
    }

    return CLI_SUCCESS;
}

STATIC void
 _ctc_greatbelt_dkits_dump_cmp_tbl_entry(ctc_dkits_dump_key_tbl_info_t* p_key_tbl_info,
                               ctc_dkits_dump_tbl_entry_t* p_src_entry,
                               ctc_dkits_dump_tbl_entry_t* p_dst_entry,
                               ctc_dkits_dump_tbl_diff_flag_t* p_diff_flag)
{
    uint32 i = 0;

    *p_diff_flag = 0;
    if (MaxTblId_t != p_key_tbl_info->tblid)
    {
        for (i = 0; i < (TABLE_ENTRY_SIZE(p_key_tbl_info->tblid) / DRV_BYTES_PER_WORD); i++)
        {
            if ((p_src_entry->data_entry[i] != p_dst_entry->data_entry[i])
               || (p_src_entry->mask_entry[i] != p_dst_entry->mask_entry[i]))
            {
                *p_diff_flag |= CTC_DKITS_DUMP_TBL_DIFF_FLAG_KEY;
            }
        }
    }
    if (MaxTblId_t != p_key_tbl_info->ad_tblid)
    {
        for (i = 0; i < (TABLE_ENTRY_SIZE(p_key_tbl_info->ad_tblid) / DRV_BYTES_PER_WORD); i++)
        {
            if (p_src_entry->ad_entry[i] != p_dst_entry->ad_entry[i])
            {
                *p_diff_flag |= CTC_DKITS_DUMP_TBL_DIFF_FLAG_AD;
            }
        }
    }

    return;
}

STATIC void
_ctc_greatbelt_dkits_dump_diff_ctl(tbls_id_t tblid, uint8 tbl_type, uint32* p_line_word, uint32* p_tbl_word)
{
    if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
        || (CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== tbl_type)
        || (CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type))
    {
        *p_line_word = DIFF_HASH_WORD_NUM_PER_LINE;
        *p_tbl_word = (TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_WORD) * 2;/* hash key&ad */
    }
    else if ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
            || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_type))
    {
        *p_line_word = DIFF_TCAM_WORD_NUM_PER_LINE;
        *p_tbl_word = (TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_WORD) * 4;/* key&mask&ad*/
    }

    return;
}

STATIC uint32
_ctc_greatbelt_dkits_dump_diff_check(tbls_id_t tblid, uint8 tbl_type,
                           ctc_dkits_dump_tbl_entry_t* p_src_entry,
                           ctc_dkits_dump_tbl_entry_t* p_dst_entry)
{
    if (((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
       || (CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== tbl_type)
       || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)))
    {
        if ((NULL != p_src_entry) && (NULL == p_src_entry->data_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s src data is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
        if ((NULL != p_dst_entry) && (NULL == p_dst_entry->data_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s dst data is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
    }

    if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
    {
        if ((NULL != p_src_entry) && (NULL == p_src_entry->mask_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s src mask is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
        if ((NULL != p_dst_entry) && (NULL == p_dst_entry->mask_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s dst mask is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
    }

    if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type)
       || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_type))
    {
        if ((NULL != p_src_entry) && (NULL == p_src_entry->data_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s src ad is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
        if ((NULL != p_dst_entry) && (NULL == p_dst_entry->data_entry))
        {
            CTC_DKIT_PRINT("Error, diff %s dst ad is null!\n", TABLE_NAME(tblid));
            goto CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END;
        }
    }

    return 1;

CTC_DKITS_DUMP_DIFF_CHECK_ERROR_END:

    return 0;
}

STATIC void
_ctc_greatbelt_dkits_dump_diff_entry(uint32 idx, tbls_id_t tblid, uint32* p_word_idx, uint8 tbl_type,
                           ctc_dkits_dump_tbl_entry_t* p_src_entry,
                           ctc_dkits_dump_tbl_entry_t* p_dst_entry,
                           uint32** pp_entry, uint32* p_is_empty)
{
    ctc_dkits_dump_tbl_entry_t* p_cmp_entry = NULL;
    static uint32 src_data_word = 0, dst_data_word = 0, src_mask_word = 0, dst_mask_word = 0;
    static uint32 line_word = 0, tbl_word = 0, is_empty = 0;
    static uint32 alter = 0, shift = 0;
    uint32* p_data_word = NULL;
    uint32* p_mask_word = NULL;

    *p_is_empty = 0;

    if (0 == idx)
    {
        src_data_word = 0;
        dst_data_word = 0;
        src_mask_word = 0;
        dst_mask_word = 0;
        is_empty = 0;
        if ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
           || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_type))
        {
            alter = DIFF_TCAM_WORD_ALTER_NUM;
            shift = alter * 2;

        }
        else if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
                || (CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type)
                || (CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== tbl_type))
        {
            alter = DIFF_HASH_WORD_ALTER_NUM;
            shift = alter;
        }
        _ctc_greatbelt_dkits_dump_diff_ctl(tblid, tbl_type, &line_word, &tbl_word);
    }

    if (0 == (idx % alter))
    {
        if (0 == ((idx / shift) % 2))
        {
            p_cmp_entry = p_src_entry;
            p_data_word = &src_data_word;
            p_mask_word = &src_mask_word;
        }
        else
        {
            p_cmp_entry = p_dst_entry;
            p_data_word = &dst_data_word;
            p_mask_word = &dst_mask_word;
        }

        if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
            || (CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== tbl_type)
           || (CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type))
        {
            if (NULL != p_cmp_entry)
            {
                *p_word_idx = *p_data_word;
                *p_data_word += alter;
                *pp_entry = (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type || CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD == tbl_type) ?
                             p_cmp_entry->data_entry : p_cmp_entry->ad_entry;
            }
            else
            {
                *pp_entry = NULL;
                is_empty = 0;
            }
        }
        else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
        {
            if (NULL != p_cmp_entry)
            {
                if (0 == ((idx / alter) % 2))
                {
                    *p_word_idx = *p_data_word;
                    *p_data_word += alter;
                    *pp_entry = p_cmp_entry->data_entry;
                }
                else
                {
                    *p_word_idx = *p_mask_word;
                    *p_mask_word += alter;
                    *pp_entry = p_cmp_entry->mask_entry;
                }
            }
            else
            {
                *pp_entry = NULL;
                is_empty = 0;
            }
        }
        else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_type)
        {
            if (NULL != p_cmp_entry)
            {
                if (0 == ((idx / alter) % 2))
                {
                    *p_word_idx = *p_data_word;
                    *p_data_word += alter;
                    *pp_entry = p_cmp_entry->ad_entry;
                }
                else
                {
                    *pp_entry = NULL;
                    is_empty = 1;
                }
            }
            else
            {
                *pp_entry = NULL;
                if (0 == ((idx / alter) % 2))
                {
                    is_empty = 0;
                }
                else
                {
                    is_empty = 1;
                }
            }
        }
    }
    else
    {
        (*p_word_idx)++;
    }

    *p_is_empty = is_empty;

    return;
}

STATIC int32
_ctc_greatbelt_dkits_dump_diff_key(ctc_dkits_dump_key_tbl_info_t* p_key_tbl_info, ctc_dkits_dump_tbl_entry_t* p_src_entry, ctc_dkits_dump_tbl_entry_t* p_dst_entry, sal_file_t p_wf)
{
    uint32 i = 0, j = 0, valid = 0;
    tbls_id_t tblid[] = {p_key_tbl_info->tblid, p_key_tbl_info->ad_tblid, MaxTblId_t};
    uint32 idx[2] = {p_key_tbl_info->idx, p_key_tbl_info->ad_idx};
    char str[300] = {0};
    char tbl[300] = {0};
    char format[100] = {0};
    char format2[100] = {0};
    uint32* p_entry = NULL;
    uint32 line_word = 0, tbl_word = 0, word_idx = 0, is_empty = 0;
    ctc_dkits_dump_tbl_type_t tbl_type = p_key_tbl_info->tbl_type;

    for (i = 0; MaxTblId_t != tblid[i]; i++)
    {
        if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == p_key_tbl_info->tbl_type) && i)
        {
            tbl_type = CTC_DKITS_DUMP_TBL_TYPE_HASH_AD;
        }

        if ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == p_key_tbl_info->tbl_type) && i)
        {
            tbl_type = CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD;
        }

        valid = _ctc_greatbelt_dkits_dump_diff_check(tblid[i], tbl_type, p_src_entry, p_dst_entry);
        if (!valid)
        {
            continue;
        }
        _ctc_greatbelt_dkits_dump_diff_ctl(tblid[i], tbl_type, &line_word, &tbl_word);

        for (j = 0; j < tbl_word; j++)
        {
            _ctc_greatbelt_dkits_dump_diff_entry(j, tblid[i], &word_idx, tbl_type,
                                       p_src_entry, p_dst_entry, &p_entry, &is_empty);

            if (0 == (j % line_word))
            {
                sal_sprintf(format2, "%%-%ds%%-%ds", TBL_NAME_IDX_MAX_PRINT_LEN + COLUNM_SPACE_LEN, DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                if (0 == j)
                {
                    sal_sprintf(tbl, "%s[%u]", TABLE_NAME(tblid[i]), idx[i]);
                    ctc_dkits_dump_cfg_brief_str(tbl, sal_strlen(TABLE_NAME(tblid[i])), ((int8)(sal_strlen(tbl)) - TBL_NAME_IDX_MAX_PRINT_LEN));
                    if (NULL != p_entry)
                    {
                        sal_fprintf(p_wf, format2, tbl, CTC_DKITS_DUMP_HEX(str, format, p_entry[word_idx], 8));
                    }
                    else
                    {
                        sal_fprintf(p_wf, format2, tbl, is_empty ? " " : "--------");
                    }
                }
                else
                {
                    if (NULL != p_entry)
                    {
                        sal_fprintf(p_wf, format2, " ", CTC_DKITS_DUMP_HEX(str, format, p_entry[word_idx], 8));
                    }
                    else
                    {
                        sal_fprintf(p_wf, format2, " ", is_empty ? " " : "--------");
                    }
                }
            }
            else
            {
                sal_sprintf(format2, "%%-%ds", DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                if (NULL != p_entry)
                {
                    sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_entry[word_idx], 8));
                }
                else
                {
                    sal_fprintf(p_wf, format2, is_empty ? " " : "--------");
                }
            }
            if (0 == ((j + 1) % line_word))
            {
                sal_fprintf(p_wf, "%s", "\n");
            }
        }
        if (0 != j % line_word)
        {
            sal_fprintf(p_wf, "\n");
        }
    }

    return CLI_SUCCESS;
}

/*p_flag: bit0--hash header, bit1--tcam header*/
STATIC void
_ctc_greatbelt_dkits_dump_diff(ctc_dkits_dump_tbl_type_t tbl_type, ctc_dkits_dump_key_tbl_info_t* p_key_tbl_info, ctc_dkits_dump_tbl_entry_t* p_src_tbl,
    ctc_dkits_dump_tbl_entry_t* p_dst_tbl, uint32* p_flag, sal_file_t p_wf)
{
    ctc_dkits_dump_tbl_diff_flag_t diff_flag = 0;

    if ((NULL != p_src_tbl) && (NULL != p_dst_tbl))
    {
        _ctc_greatbelt_dkits_dump_cmp_tbl_entry(p_key_tbl_info, p_src_tbl, p_dst_tbl, &diff_flag);
        if (!(DKITS_FLAG_ISSET(diff_flag, CTC_DKITS_DUMP_TBL_DIFF_FLAG_TBL)
           || DKITS_FLAG_ISSET(diff_flag, CTC_DKITS_DUMP_TBL_DIFF_FLAG_KEY)
           || DKITS_FLAG_ISSET(diff_flag, CTC_DKITS_DUMP_TBL_DIFF_FLAG_AD)))
        {
            return;
        }
    }

    /*Dump header*/
    if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== tbl_type) || (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type))
    {
        if (!DKITS_IS_BIT_SET(*p_flag, 0))
        {
            DKITS_BIT_SET(*p_flag, 0);
            ctc_dkits_dump_txt_header(CTC_DKITS_DUMP_TEXT_HEADER_DIFF_HASH, p_wf);
        }
    }
    else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
    {
        if (!DKITS_IS_BIT_SET(*p_flag, 1))
        {
            DKITS_BIT_SET(*p_flag, 1);
            ctc_dkits_dump_txt_header(CTC_DKITS_DUMP_TEXT_HEADER_DIFF_TCAM, p_wf);
        }
    }

    if ((CTC_DKITS_DUMP_TBL_TYPE_STATIC == tbl_type) || (CTC_DKITS_DUMP_TBL_TYPE_SERDES == tbl_type))
    {
        if ((NULL == p_src_tbl) || (NULL == p_dst_tbl))
        {
            CTC_DKIT_PRINT("Diff static tbl field error, ");
            if (NULL == p_src_tbl)
            {
                CTC_DKIT_PRINT("src tbl is NULL, ");
            }
            if (NULL == p_dst_tbl)
            {
                CTC_DKIT_PRINT("dst tbl is NULL, ");
            }
            if (p_key_tbl_info->tblid != MaxTblId_t)
            {
                CTC_DKIT_PRINT("tblName:%s, index:%u\n", TABLE_NAME(p_key_tbl_info->tblid), p_key_tbl_info->idx);
            }
            return;
        }

        if (CTC_DKITS_DUMP_TBL_TYPE_SERDES == tbl_type)
        {
            ctc_dkits_dump_diff_flex(p_key_tbl_info, p_src_tbl, p_dst_tbl, p_wf);
        }
        else
        {
            ctc_greatbelt_dkits_dump_diff_field(p_key_tbl_info, p_src_tbl, p_dst_tbl, p_wf);
        }

    }
    else if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
            || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
            || (CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD == tbl_type))
    {
        if ((NULL == p_src_tbl) && (NULL == p_dst_tbl))
        {
            CTC_DKIT_PRINT("Diff key tbl error!\n");
            return;
        }
        _ctc_greatbelt_dkits_dump_diff_key(p_key_tbl_info, p_src_tbl, p_dst_tbl, p_wf);
    }

    return;
}

int32
ctc_greatbelt_dkits_dump_tbl2data(uint16 tbl_id, uint16 index, uint32* p_data_entry, uint32* p_mask_entry, sal_file_t p_f)
{
    uint8  data[500] = {0}, is_tcam = 0;
    uint16 mem_len = 0;
    ctc_dkits_dump_tbl_block_hdr_t tbl_block_hdr;

    sal_memset(&tbl_block_hdr, 0, sizeof(ctc_dkits_dump_tbl_block_hdr_t));

    tbl_block_hdr.tblid_0_7 = tbl_id & 0xFF;
    tbl_block_hdr.tblid_8_12 = (tbl_id >> 8) & 0x1F;
    tbl_block_hdr.idx = index;
    is_tcam = (NULL == p_mask_entry) ? 0 : 1;

    _ctc_greatbelt_dkits_dump_select_format(&tbl_block_hdr, p_data_entry, p_mask_entry);

    if (CTC_DKITS_FORMAT_TYPE_4BYTE == tbl_block_hdr.format)
    {
        sal_memset(data, 0, sizeof(data));
        _ctc_greatbelt_dkits_dump_format_entry(&tbl_block_hdr, p_data_entry, data, &mem_len);

        if (1 == is_tcam)
        {
            _ctc_greatbelt_dkits_dump_format_entry(&tbl_block_hdr, p_mask_entry, data, &mem_len);
        }
        tbl_block_hdr.entry_num = (mem_len / sizeof(ctc_dkits_dump_tbl_block_data_t)) - 1;
        sal_fwrite(&tbl_block_hdr, sizeof(ctc_dkits_dump_tbl_block_hdr_t), 1, p_f);
        sal_fwrite(data, mem_len, 1, p_f);
    }
    else
    {
        sal_memset(data, 0, sizeof(data));
        ctc_dkits_dump_plain_entry((TABLE_ENTRY_SIZE(tbl_id)/4), p_data_entry, data, &mem_len);

        if (1 == is_tcam)
        {
            ctc_dkits_dump_plain_entry((TABLE_ENTRY_SIZE(tbl_id)/4), p_mask_entry, data, &mem_len);
        }
        tbl_block_hdr.entry_num = (mem_len / sizeof(uint32)) - 1;
        sal_fwrite(&tbl_block_hdr, sizeof(ctc_dkits_dump_tbl_block_hdr_t), 1, p_f);
        sal_fwrite(data, mem_len, 1, p_f);
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_cmp_tbl(uint8 lchip, sal_file_t p_srf, sal_file_t p_drf, uint8 src_cfg_endian, uint8 dst_cfg_endian, sal_file_t p_wf)
{
    uint32 read_src = 1, read_dst = 1, src_end = 0, dst_end = 0;
    uint32 seop = 0, deop = 0;
    uint32* src_data_entry = NULL;
    uint32* src_mask_entry = NULL;
    uint32* src_ad_entry = NULL;
    uint32* dst_data_entry = NULL;
    uint32* dst_mask_entry = NULL;
    uint32* dst_ad_entry = NULL;
    ctc_dkits_dump_tbl_entry_t src_tbl_entry, dst_tbl_entry;
    ctc_dkits_dump_key_tbl_info_t src_key_tbl_info, dst_key_tbl_info;
    ctc_dkits_dump_key_tbl_info_t ad_tbl_info;
    int32 ret = 0;
    uint32 flag = 0;

    src_data_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    src_mask_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    src_ad_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    dst_data_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    dst_mask_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    dst_ad_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));

    sal_memset(&src_tbl_entry, 0, sizeof(ctc_dkits_dump_tbl_entry_t));
    sal_memset(&dst_tbl_entry, 0, sizeof(ctc_dkits_dump_tbl_entry_t));

    src_tbl_entry.data_entry = src_data_entry;
    src_tbl_entry.mask_entry = src_mask_entry;
    src_tbl_entry.ad_entry = src_ad_entry;

    dst_tbl_entry.data_entry = dst_data_entry;
    dst_tbl_entry.mask_entry = dst_mask_entry;
    dst_tbl_entry.ad_entry = dst_ad_entry;

    sal_fseek(p_srf, 0, SEEK_END);
    seop = sal_ftell(p_srf);
    if (seop < sizeof(ctc_dkits_dump_centec_file_hdr_t))
    {
        return CLI_SUCCESS;
    }
    sal_fseek(p_srf, sizeof(ctc_dkits_dump_centec_file_hdr_t), SEEK_SET);

    sal_fseek(p_drf, 0, SEEK_END);
    deop = sal_ftell(p_drf);
    if (deop < sizeof(ctc_dkits_dump_centec_file_hdr_t))
    {
        return CLI_SUCCESS;
    }
    sal_fseek(p_drf, sizeof(ctc_dkits_dump_centec_file_hdr_t), SEEK_SET);

    while (!src_end || !dst_end)
    {
        if (read_src)
        {
            if ((1 != sal_feof(p_srf)) && (seop != sal_ftell(p_srf)))
            {
                sal_memset(src_data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
                sal_memset(src_mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
                sal_memset(src_ad_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));

                src_key_tbl_info.tblid = MaxTblId_t;
                src_key_tbl_info.ad_tblid = MaxTblId_t;

                /*get key info*/
                ret = ctc_dkits_dump_get_tbl_entry(lchip, p_srf, src_cfg_endian, &src_tbl_entry, &src_key_tbl_info);
                if (ret)
                {
                    continue;
                }

                if (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == src_key_tbl_info.tbl_type)
                {
                    /*get ad info*/
                    ctc_dkits_dump_get_tbl_entry(lchip, p_srf, src_cfg_endian, &src_tbl_entry, &ad_tbl_info);
                    src_key_tbl_info.ad_tblid = ad_tbl_info.ad_tblid;
                    src_key_tbl_info.ad_idx = ad_tbl_info.ad_idx;
                }
                else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == src_key_tbl_info.tbl_type)
                {
                    /*get ad info*/
                    ctc_dkits_dump_get_tbl_entry(lchip, p_srf, src_cfg_endian, &src_tbl_entry, &ad_tbl_info);
                    src_key_tbl_info.ad_tblid = ad_tbl_info.ad_tblid;
                    src_key_tbl_info.ad_idx = ad_tbl_info.ad_idx;
                }
            }
            else
            {
                src_end = 1;
            }
        }

        if (read_dst)
        {
            if ((1 != sal_feof(p_drf)) && (deop != sal_ftell(p_drf)))
            {
                sal_memset(dst_data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
                sal_memset(dst_mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
                sal_memset(dst_ad_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));

                dst_key_tbl_info.tblid = MaxTblId_t;
                dst_key_tbl_info.ad_tblid = MaxTblId_t;

                /*get key info*/
                ret = ctc_dkits_dump_get_tbl_entry(lchip, p_drf, dst_cfg_endian, &dst_tbl_entry, &dst_key_tbl_info);
                if (ret)
                {
                    continue;
                }

                if (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == dst_key_tbl_info.tbl_type)
                {
                    ctc_dkits_dump_get_tbl_entry(lchip, p_drf, dst_cfg_endian, &dst_tbl_entry, &ad_tbl_info);
                    dst_key_tbl_info.ad_tblid = ad_tbl_info.ad_tblid;
                    dst_key_tbl_info.ad_idx = ad_tbl_info.ad_idx;
                }
                else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == dst_key_tbl_info.tbl_type)
                {
                    ctc_dkits_dump_get_tbl_entry(lchip, p_drf, dst_cfg_endian, &dst_tbl_entry, &ad_tbl_info);
                    dst_key_tbl_info.ad_tblid = ad_tbl_info.ad_tblid;
                    dst_key_tbl_info.ad_idx = ad_tbl_info.ad_idx;
                }

            }
            else
            {
                dst_end = 1;
            }
        }

        if (!src_end && dst_end)
        {
            _ctc_greatbelt_dkits_dump_diff(src_key_tbl_info.tbl_type, &src_key_tbl_info, &src_tbl_entry, NULL, &flag, p_wf);
            read_src = 1;
            read_dst = 0;
        }
        else if (src_end && !dst_end)
        {
            _ctc_greatbelt_dkits_dump_diff(dst_key_tbl_info.tbl_type, &dst_key_tbl_info, NULL, &dst_tbl_entry, &flag, p_wf);
            read_src = 0;
            read_dst = 1;
        }
        else if ((!src_end && !dst_end) && (src_key_tbl_info.tblid == dst_key_tbl_info.tblid))
        {
            if (src_key_tbl_info.idx == dst_key_tbl_info.idx)
            {
                _ctc_greatbelt_dkits_dump_diff(src_key_tbl_info.tbl_type, &src_key_tbl_info, &src_tbl_entry, &dst_tbl_entry, &flag, p_wf);
                read_src = 1;
                read_dst = 1;
            }
            else if (src_key_tbl_info.idx < dst_key_tbl_info.idx)
            {
                _ctc_greatbelt_dkits_dump_diff(src_key_tbl_info.tbl_type, &src_key_tbl_info, &src_tbl_entry, NULL, &flag, p_wf);
                read_src = 1;
                read_dst = 0;
            }
            else if (src_key_tbl_info.idx > dst_key_tbl_info.idx)
            {
                _ctc_greatbelt_dkits_dump_diff(dst_key_tbl_info.tbl_type, &dst_key_tbl_info, NULL, &dst_tbl_entry, &flag, p_wf);
                read_src = 0;
                read_dst = 1;
            }
        }
        else if ((!src_end && !dst_end) && (src_key_tbl_info.tblid != dst_key_tbl_info.tblid))
        {
            if (((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == src_key_tbl_info.tbl_type || CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== src_key_tbl_info.tbl_type)
                && (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == dst_key_tbl_info.tbl_type || CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== dst_key_tbl_info.tbl_type))
                ||  ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == src_key_tbl_info.tbl_type)
                && (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == dst_key_tbl_info.tbl_type)))
            {
                if (src_key_tbl_info.order < dst_key_tbl_info.order)
                {
                    _ctc_greatbelt_dkits_dump_diff(src_key_tbl_info.tbl_type, &src_key_tbl_info, &src_tbl_entry, NULL, &flag, p_wf);
                    read_src = 1;
                    read_dst = 0;
                }
                else if (src_key_tbl_info.order > dst_key_tbl_info.order)
                {
                    _ctc_greatbelt_dkits_dump_diff(dst_key_tbl_info.tbl_type, &dst_key_tbl_info, NULL, &dst_tbl_entry, &flag, p_wf);
                    read_src = 0;
                    read_dst = 1;
                }
            }
            else if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == src_key_tbl_info.tbl_type || CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD== src_key_tbl_info.tbl_type)
                     && (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == dst_key_tbl_info.tbl_type))
            {
                _ctc_greatbelt_dkits_dump_diff(src_key_tbl_info.tbl_type, &src_key_tbl_info, &src_tbl_entry, NULL, &flag, p_wf);
                read_src = 1;
                read_dst = 0;
            }
            else if ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == src_key_tbl_info.tbl_type)
                     && (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == dst_key_tbl_info.tbl_type || CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD == dst_key_tbl_info.tbl_type))
            {
                _ctc_greatbelt_dkits_dump_diff(dst_key_tbl_info.tbl_type, &dst_key_tbl_info, NULL, &dst_tbl_entry, &flag, p_wf);
                read_src = 0;
                read_dst = 1;
            }
        }
    }

    sal_free(src_data_entry);
    sal_free(src_mask_entry);
    sal_free(src_ad_entry);
    sal_free(dst_data_entry);
    sal_free(dst_mask_entry);
    sal_free(dst_ad_entry);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_data2text(uint8 lchip, sal_file_t p_rf, uint8 cfg_endian, sal_file_t p_wf, uint8 data_translate)
{
    uint32 i = 0, eop = 0, tbl_header = 0;
    char* str = NULL;
    char* tbl = NULL;
    char format[100] = {0};
    char format2[100] = {0};
    uint32* data_entry = NULL;
    uint32* mask_entry = NULL;
    uint32* ad_entry = NULL;
    uint32* p_entry_data = NULL;
    uint32 tbl_word = 0, word_num_per_line = 0, word_idx = 0, data_word = 0, mask_word = 0;
    ctc_dkits_dump_tbl_entry_t tbl_entry;
    uint8 header_type = CTC_DKITS_DUMP_TEXT_HEADER_NUM;
    static uint32 pre = 0;
    uint32 cur = 0;
    ctc_dkits_dump_serdes_tbl_t* serdes_tbl = NULL;
    uint32 serdes_id = 0;
    uint32 register_mode = 0;
    uint32 offset = 0;
    uint32 value = 0;
    char* p_mode[CTC_DKIT_SERDES_PLLB+1] = {"Tx", "Rx", "Common", "PLLA", "PLLB"};
    ctc_dkits_dump_key_tbl_info_t tbl_info;
    tbls_id_t tb_id = 0;
    uint32 index = 0;
    ctc_dkit_memory_para_t* p_memory_para = NULL;
    int32 ret = CLI_SUCCESS;

    str = (char*)sal_malloc(300 * sizeof(char));
    tbl = (char*)sal_malloc(300 * sizeof(char));
    data_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    mask_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));
    ad_entry = (uint32*)sal_malloc(MAX_ENTRY_WORD * sizeof(uint32));

    if (!str || !tbl || !data_entry || !mask_entry || !ad_entry)
    {
        goto exit;
    }

    sal_memset(str, 0, 300 * sizeof(char));
    sal_memset(tbl, 0, 300 * sizeof(char));
    sal_memset(data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
    sal_memset(mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
    sal_memset(ad_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));

    tbl_entry.data_entry = data_entry;
    tbl_entry.mask_entry = mask_entry;
    tbl_entry.ad_entry = ad_entry;

    sal_fseek(p_rf, 0, SEEK_END);
    eop = sal_ftell(p_rf);
    if (eop <= sizeof(ctc_dkits_dump_centec_file_hdr_t))
    {
        goto exit;
    }
    sal_fseek(p_rf, sizeof(ctc_dkits_dump_centec_file_hdr_t), SEEK_SET);

    while (eop != sal_ftell(p_rf) && (1 != sal_feof(p_rf)))
    {
        cur = sal_ftell(p_rf);
        if ((cur - pre) > (eop/10))
        {
            sal_udelay(10);
            pre = cur;
        }

        sal_memset(data_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
        sal_memset(mask_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
        sal_memset(ad_entry, 0, MAX_ENTRY_WORD * sizeof(uint32));
        data_word = 0;
        mask_word = 0;

         ctc_dkits_dump_get_tbl_entry(lchip, p_rf, cfg_endian, &tbl_entry, &tbl_info);

        /*Default using key info*/
        tb_id = tbl_info.tblid;
        index = tbl_info.idx;


        switch(tbl_info.tbl_type)
        {
            case CTC_DKITS_DUMP_TBL_TYPE_STATIC:
            case CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY:
            case CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD:
                tbl_header = (CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TBL != header_type) ? 0 : 1;
                header_type = CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TBL;
                tbl_word = TABLE_ENTRY_SIZE(tb_id) / DRV_BYTES_PER_WORD;
                break;

            case CTC_DKITS_DUMP_TBL_TYPE_SERDES:
                tbl_header = (CTC_DKITS_DUMP_TEXT_HEADER_DUMP_SERDES != header_type) ? 0 : 1;
                header_type = CTC_DKITS_DUMP_TEXT_HEADER_DUMP_SERDES;
                tbl_word = 1;
                break;

            case CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY:
                tbl_header = (CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TCAM != header_type) ? 0 : 1;
                header_type = CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TCAM;
                tbl_word = (TABLE_ENTRY_SIZE(tb_id) / DRV_BYTES_PER_WORD) * 2;
                tbl_word = (tbl_word < DUMP_TCAM_WORD_NUM_PER_LINE) ? DUMP_TCAM_WORD_NUM_PER_LINE : tbl_word;
                break;

            case CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD:
                tbl_header = (CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TCAM != header_type) ? 0 : 1;
                header_type = CTC_DKITS_DUMP_TEXT_HEADER_DUMP_TCAM;
                tbl_word = (TABLE_ENTRY_SIZE(tbl_info.ad_tblid) / DRV_BYTES_PER_WORD);
                break;

            default:
               break;
        }

        if ((0 == tbl_header) && (!data_translate || (CTC_DKITS_DUMP_TBL_TYPE_SERDES == tbl_info.tbl_type)))
        {
            tbl_header = 1;
            ctc_dkits_dump_txt_header(header_type, p_wf);
        }

        for (i = 0; i < tbl_word; i++)
        {
            if (CTC_DKITS_DUMP_TBL_TYPE_SERDES == tbl_info.tbl_type)
            {
                word_num_per_line = DUMP_TBL_WORD_NUM_PER_LINE;
                sal_sprintf(format2, "%%-%du%%-%ds%%-%du0x%%0%dx", 10, 10, 10, 4);
                serdes_tbl = (ctc_dkits_dump_serdes_tbl_t*)tbl_entry.data_entry;
                serdes_id = serdes_tbl->serdes_id;
                register_mode = serdes_tbl->serdes_mode;
                offset = serdes_tbl->offset;
                value = serdes_tbl->data;

                sal_fprintf(p_wf, format2, serdes_id, ((register_mode<=CTC_DKIT_SERDES_PLLB)?p_mode[register_mode]:""), offset, value);
                continue;
            }
            else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_info.tbl_type)
            {
                word_num_per_line = DUMP_TCAM_WORD_NUM_PER_LINE;

                if (0 == (i%(DUMP_TCAM_WORD_NUM_PER_LINE/2)))
                {
                    if (0 == ((i/(DUMP_TCAM_WORD_NUM_PER_LINE/2))%2))
                    {
                        word_idx = data_word;
                        data_word += DUMP_TCAM_WORD_NUM_PER_LINE/2;
                        p_entry_data = tbl_entry.data_entry;
                    }
                    else
                    {
                        word_idx = mask_word;
                        mask_word += DUMP_TCAM_WORD_NUM_PER_LINE/2;
                        p_entry_data = tbl_entry.mask_entry;
                    }
                }
                else
                {
                    word_idx++;
                }
            }
            else
            {
                word_idx = i;
                if (CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_info.tbl_type)
                {
                    word_num_per_line = DUMP_TBL_WORD_NUM_PER_LINE;
                    p_entry_data = tbl_entry.ad_entry;
                    tb_id = tbl_info.ad_tblid;
                    index = tbl_info.ad_idx;
                }
                else if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_info.tbl_type)
                {
                    word_num_per_line = DUMP_TCAM_WORD_NUM_PER_LINE / 2;
                    p_entry_data = tbl_entry.ad_entry;
                    tb_id = tbl_info.ad_tblid;
                    index = tbl_info.ad_idx;
                }
                else
                {
                    word_num_per_line = DUMP_TBL_WORD_NUM_PER_LINE;
                    p_entry_data = tbl_entry.data_entry;
                }
            }

            if (data_translate)
            {
                break;
            }

            if (0 == (i % word_num_per_line))
            {
                sal_sprintf(format2, "%%-%ds%%-%ds", TBL_NAME_IDX_MAX_PRINT_LEN + COLUNM_SPACE_LEN, DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                if (0 == i)
                {
                    sal_sprintf(tbl, "%s[%u]", TABLE_NAME(tb_id), index);
                    ctc_dkits_dump_cfg_brief_str(tbl, sal_strlen(TABLE_NAME(tb_id)), ((int8)(sal_strlen(tbl)) - TBL_NAME_IDX_MAX_PRINT_LEN));
                    sal_fprintf(p_wf, format2, tbl, CTC_DKITS_DUMP_HEX(str, format, p_entry_data[word_idx], 8));
                }
                else
                {
                    sal_fprintf(p_wf, format2, " ", CTC_DKITS_DUMP_HEX(str, format, p_entry_data[word_idx], 8));
                }
            }
            else
            {
                sal_sprintf(format2, "%%-%ds", DUMP_ENTRY_WORD_STR_LEN + DUMP_ENTRY_WORD_SPACE_LEN);
                if (((tbl_word/2) > (TABLE_ENTRY_SIZE(tb_id)/DRV_BYTES_PER_WORD))
                   && ((i%(word_num_per_line/2)) >= (TABLE_ENTRY_SIZE(tb_id)/DRV_BYTES_PER_WORD)))
                {
                    sal_fprintf(p_wf, format2, " ", " ");
                }
                else
                {
                    sal_fprintf(p_wf, format2, CTC_DKITS_DUMP_HEX(str, format, p_entry_data[word_idx], 8));
                }
            }
            if (0 == ((i + 1) % word_num_per_line))
            {
                sal_fprintf(p_wf, "%s", "\n");
            }
        }


        if ((0 != i % word_num_per_line) && (!data_translate || (CTC_DKITS_DUMP_TBL_TYPE_SERDES == tbl_info.tbl_type)))
        {
            sal_fprintf(p_wf, "\n");
        }

        if ((data_translate) && (tbl_info.tbl_type != CTC_DKITS_DUMP_TBL_TYPE_SERDES))
        {
            CTC_DKIT_PRINT("%s[%d]:\n", TABLE_NAME(tb_id), index);
            CTC_DKIT_PRINT("-------------------------------------------------------------\n");
            p_memory_para = (ctc_dkit_memory_para_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ctc_dkit_memory_para_t));
            if (NULL == p_memory_para)
            {
                ret = CLI_ERROR;
                goto exit;
            }
            sal_memset(p_memory_para, 0, sizeof(ctc_dkit_memory_para_t));
            p_memory_para->param[1] = lchip;
            p_memory_para->param[2] = 0;
            p_memory_para->param[3] = tb_id;
            p_memory_para->param[4] = 0;
            if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_info.tbl_type)
            {
                sal_memcpy(p_memory_para->value, tbl_entry.data_entry, CTC_DKITS_MAX_ENTRY_WORD);
                sal_memcpy(p_memory_para->mask, tbl_entry.mask_entry, CTC_DKITS_MAX_ENTRY_WORD);
            }
            else
            {
                sal_memcpy(p_memory_para->value, p_entry_data, CTC_DKITS_MAX_ENTRY_WORD);
            }
            p_memory_para->type = CTC_DKIT_MEMORY_SHOW_TBL_BY_DATA;
            ctc_greatbelt_dkit_memory_process(p_memory_para);
            mem_free(p_memory_para);
        }
    }

    pre = 0;

exit:
    if (str)
    {
        sal_free(str);
    }
    if (tbl)
    {
        sal_free(tbl);
    }
    if (data_entry)
    {
        sal_free(data_entry);
    }
    if (mask_entry)
    {
        sal_free(mask_entry);
    }
    if (ad_entry)
    {
        sal_free(ad_entry);
    }

    return ret;
}

STATIC void
_ctc_greatbelt_dkits_dump_hash2ad(tbls_id_t tblid, tbls_id_t* p_ad)
{
    if (DsMacHashKey_t == tblid)
    {
        *p_ad = DsMac_t;
    }
    else if (DsFcoeHashKey_t == tblid)
    {
        *p_ad = DsIpDa_t;
    }
    else if (DsFcoeRpfHashKey_t == tblid)
    {
        *p_ad = DsIpSaNat_t;
    }
    else if ((DsTrillMcastHashKey_t == tblid)
      || (DsTrillMcastVlanHashKey_t == tblid)
      || (DsTrillUcastHashKey_t == tblid))
    {
        *p_ad = DsTrillDa_t;
    }
    else if ((DsAclQosIpv4HashKey_t == tblid)
       || (DsAclQosMacHashKey_t == tblid))
    {
        *p_ad = DsAcl_t;
    }
    else if ((DsLpmIpv4Hash16Key_t == tblid)
       || (DsLpmIpv4Hash8Key_t == tblid)
       || (DsLpmIpv4NatDaPortHashKey_t == tblid)
       || (DsLpmIpv4NatSaHashKey_t == tblid)
       || (DsLpmIpv4NatSaPortHashKey_t == tblid)
       || (DsLpmIpv6Hash32HighKey_t == tblid)
       || (DsLpmIpv6Hash32LowKey_t == tblid)
       || (DsLpmIpv6Hash32MidKey_t == tblid)
       || (DsLpmIpv6NatDaPortHashKey_t == tblid)
       || (DsLpmIpv6NatSaHashKey_t == tblid)
       || (DsLpmIpv6NatSaPortHashKey_t == tblid)
       || (DsLpmIpv4McastHash32Key_t == tblid)
       || (DsLpmIpv4McastHash64Key_t == tblid)
       || (DsLpmIpv6McastHash0Key_t == tblid)
       || (DsLpmIpv6McastHash1Key_t == tblid))
    {
        *p_ad = DKITS_NO_AD_TBL;
    }
    else if((DsUserIdDoubleVlanHashKey_t == tblid)
       || (DsUserIdSvlanHashKey_t == tblid)
       || (DsUserIdCvlanHashKey_t == tblid)
       || (DsUserIdSvlanCosHashKey_t == tblid)
       || (DsUserIdCvlanCosHashKey_t == tblid)
       || (DsUserIdMacHashKey_t == tblid)
       || (DsUserIdL2HashKey_t == tblid)
       || (DsUserIdMacPortHashKey_t == tblid)
       || (DsUserIdIpv4PortHashKey_t == tblid)
       || (DsUserIdPortHashKey_t == tblid)
       || (DsUserIdIpv4HashKey_t == tblid)
       || (DsUserIdIpv6HashKey_t == tblid))
    {
        /*Ingress DsUserId_t, for  egress DsTunnelId_t*/
        *p_ad = DsUserId_t;
    }
    else if((DsTunnelIdIpv4HashKey_t == tblid)
       || (DsTunnelIdIpv4RpfHashKey_t == tblid)
       || (DsTunnelIdPbbHashKey_t == tblid)
       || (DsTunnelIdTrillUcRpfHashKey_t == tblid)
       || (DsTunnelIdTrillMcRpfHashKey_t == tblid)
       || (DsTunnelIdTrillMcAdjCheckHashKey_t == tblid)
       || (DsTunnelIdTrillUcDecapHashKey_t == tblid))
    {
        *p_ad = DsTunnelId_t;
    }
     else if((DsPbtOamHashKey_t == tblid)
       || (DsEthOamRmepHashKey_t == tblid)
       || (DsEthOamHashKey_t == tblid))
    {
        *p_ad = DsEthOamChan_t;
    }
    else if((DsMplsOamLabelHashKey_t == tblid) || (DsBfdOamHashKey_t == tblid))
    {
        *p_ad = DsMplsPbtBfdOamChan_t;
    }
    else
    {
        *p_ad = MaxTblId_t;
    }

    return;
}

/*Not support trill tunnel*/
STATIC void
_ctc_greatbelt_dkits_dump_hash2type(tbls_id_t tblid, ctc_dkits_hash_module_type_t* p_module_type, uint32* p_type)
{
    if (DsMacHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_MAC;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsFcoeHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_FCOE;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsFcoeRpfHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_FCOE_RPF;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsTrillUcastHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_TRILL_UCAST;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsTrillMcastHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_TRILL_MCAST;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsTrillMcastVlanHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_TRILL_MCAST_VLAN;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsAclQosIpv4HashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_ACL_IPV4;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsAclQosMacHashKey_t == tblid)
    {
        *p_type = FIB_HASH_TYPE_ACL_MAC;
        *p_module_type = CTC_DKITS_HASH_MODULE_FIB;
    }
    else if (DsLpmIpv4Hash16Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_16;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4Hash8Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_8;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4NatDaPortHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_NAT_DA_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4NatSaHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_NAT_SA_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4NatSaHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_NAT_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4NatSaPortHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_NAT_SA_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6Hash32HighKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_HIGH32;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6Hash32LowKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_LOW32;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6Hash32MidKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_MID32;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6NatDaPortHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_NAT_DA_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6NatSaHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_NAT_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6NatSaPortHashKey_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_NAT_SA_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4McastHash32Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_XG;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv4McastHash64Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV4_SG;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6McastHash0Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_SG0;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if (DsLpmIpv6McastHash1Key_t == tblid)
    {
        *p_type = LPM_HASH_TYPE_IPV6_SG1;
        *p_module_type = CTC_DKITS_HASH_MODULE_IP;
    }
    else if(DsUserIdDoubleVlanHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_TWO_VLAN;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdSvlanHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_SVLAN;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdCvlanHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_CVLAN;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdSvlanCosHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_SVLAN_COS;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdCvlanCosHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_CVLAN_COS;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdMacHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_MAC_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdL2HashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_L2;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdMacPortHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_PORT_MAC_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdPortHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_PORT;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdIpv4HashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_IPV4_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdIpv6HashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_IPV6_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsUserIdIpv4PortHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_PORT_IPV4_SA;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsTunnelIdIpv4HashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_IPV4_TUNNEL;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsTunnelIdIpv4RpfHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_IPV4_RPF;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsTunnelIdPbbHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_PBB;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsPbtOamHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_PBT_OAM;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsMplsOamLabelHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_MPLS_SECTION_OAM;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsBfdOamHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_BFD_OAM;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsEthOamRmepHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_ETHER_RMEP;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else if(DsEthOamHashKey_t == tblid)
    {
        *p_type = USERID_HASH_TYPE_ETHER_VLAN_OAM;
        *p_module_type = CTC_DKITS_HASH_MODULE_SCL;
    }
    else
    {
        *p_type  = USERID_HASH_TYPE_INVALID;
        *p_module_type = CTC_DKITS_HASH_MODULE_NUM;
    }

    return;
}

STATIC bool
_ctc_greatbelt_dkits_dump_check_dbg(tbls_id_t tblid)
{
    uint32 i = 0, n = 0;
    uint8 is_match = 0;

    n = ctc_greatbelt_dkit_drv_get_bus_list_num();
    for (i = 0; i < n; i++)
    {
        if (tblid == DKIT_BUS_GET_FIFO_ID(i, 0))
        {
            is_match = 1;
            break;
        }
    }

    return (is_match?TRUE:FALSE);
}

STATIC void
_ctc_greatbelt_dkits_dump_cfg_brief_str(char* p_str, uint8 pos, int8 extr_len)
{
    if (extr_len < 0)
    {
        return;
    }

    sal_memcpy(p_str + pos - extr_len, p_str + pos, (sal_strlen(p_str + pos) + 1));

    return;
}

STATIC uint32
_ctc_greatbelt_dkits_dump_invalid_static_tbl(tbls_id_t tblid)
{
    uint32 i = 0;
    tables_info_t* ptr = NULL;

    ptr = TABLE_INFO_PTR(tblid);

    if (ptr->ptr_ext_info)
    {
        if (EXT_INFO_TYPE_TCAM == TABLE_EXT_INFO_TYPE(tblid))
        {
            return 1;
        }

        if (EXT_INFO_TYPE_DYNAMIC == TABLE_EXT_INFO_TYPE(tblid))
        {
            return 1;
        }
    }

    if ((tblid >= QsgmiiInterruptNormal0_t) && (tblid <= SgmacXfiTestPatSeed11_t))
    {
        return 1;
    }

    for (i = 0; MaxTblId_t != gb_exclude_tbl[i]; i++)
    {
        if (tblid == gb_exclude_tbl[i])
        {
            return 1;
        }
    }

    return 0;
}

STATIC int32
_ctc_greatbelt_dkits_dump_tcam_key2ad(tbls_id_t tblid, uint32 key_type, tbls_id_t* p_ad_tblid)
{
    *p_ad_tblid = MaxTblId_t;
    key_type = key_type;

    switch(tblid)
    {
        case DsIpv4UcastRouteKey_t:
        case DsIpv4RpfKey_t:
            *p_ad_tblid = DsIpv4UcastDaTcam_t;
            break;

        case DsIpv6UcastRouteKey_t:
        case DsIpv6RpfKey_t:
            *p_ad_tblid = DsIpv6UcastDaTcam_t;
            break;

        case DsIpv4McastRouteKey_t:
            *p_ad_tblid = DsIpv4McastDaTcam_t;
            break;

        case DsMacIpv4Key_t:
            *p_ad_tblid = DsMacIpv4Tcam_t;
            break;

        case DsIpv6McastRouteKey_t:
            *p_ad_tblid = DsIpv6McastDaTcam_t;
            break;

        case DsMacIpv6Key_t:
            *p_ad_tblid = DsMacIpv6Tcam_t;
            break;

        case DsIpv4NatKey_t:
            *p_ad_tblid = DsIpv4SaNatTcam_t;
            break;

        case DsIpv6NatKey_t:
            *p_ad_tblid = DsIpv6SaNatTcam_t;
            break;

        case DsIpv4PbrKey_t:
            *p_ad_tblid = DsIpv4UcastPbrDualDaTcam_t;
            break;

        case DsIpv6PbrKey_t:
            *p_ad_tblid = DsIpv6UcastPbrDualDaTcam_t;
            break;

        case DsTunnelIdIpv4Key_t:
            *p_ad_tblid = DsTunnelIdIpv4Tcam_t;
            break;

        case DsTunnelIdIpv6Key_t:
            *p_ad_tblid = DsTunnelIdIpv6Tcam_t;
            break;

        case DsTunnelIdPbbKey_t:
            *p_ad_tblid = DsTunnelIdPbbTcam_t;
            break;

        case DsUserIdMacKey_t:
            *p_ad_tblid = DsUserIdMacTcam_t;
            break;

        case DsUserIdIpv6Key_t:
            *p_ad_tblid = DsUserIdIpv6Tcam_t;
            break;

        case DsUserIdIpv4Key_t:
            *p_ad_tblid = DsUserIdIpv4Tcam_t;
            break;

         case DsUserIdVlanKey_t:
            *p_ad_tblid = DsUserIdVlanTcam_t;
            break;

         case DsAclMacKey0_t:
            *p_ad_tblid = DsMacAcl0Tcam_t;
            break;

         case DsAclIpv4Key0_t:
         case DsAclMplsKey0_t:
            *p_ad_tblid = DsIpv4Acl0Tcam_t;
            break;

         case DsAclMacKey1_t:
            *p_ad_tblid = DsMacAcl1Tcam_t;
            break;

         case DsAclIpv4Key1_t:
         case DsAclMplsKey1_t:
            *p_ad_tblid = DsIpv4Acl1Tcam_t;
            break;

         case DsAclMacKey2_t:
            *p_ad_tblid = DsMacAcl2Tcam_t;
            break;

         case DsAclIpv4Key3_t:
         case DsAclMplsKey3_t:
            *p_ad_tblid = DsIpv4Acl3Tcam_t;
            break;

         case DsAclIpv6Key0_t:
            *p_ad_tblid = DsIpv6Acl0Tcam_t;
            break;

         case DsAclIpv6Key1_t:
            *p_ad_tblid = DsIpv6Acl1Tcam_t;
            break;

        default:
            break;
    }

    return CLI_SUCCESS;
}

#if 0
STATIC void
_ctc_greatbelt_dkits_dump_get_hash_type(tbls_id_t tblid, ctc_dkits_ds_t* p_ds, uint32* p_hash_type)
{
    ctc_dkits_hash_module_type_t module_type;
    uint32 key_type = 0;
    lookup_info_t look_info;

    sal_memset(&look_info, 0, sizeof(lookup_info_t));

    _ctc_greatbelt_dkits_dump_hash2type(tblid, &module_type, &key_type);

    look_info.hash_module = module_type;
    look_info.hash_type = key_type;
    look_info.p_ds_key = p_ds;

    drv_greatbelt_hash_lookup_get_key_type(&look_info, p_hash_type);

    return;
}
#endif

STATIC void
_ctc_greatbelt_dkits_dump_get_hash_info(tbls_id_t tblid, ctc_dkits_hash_tbl_info_t* p_hash_tbl_info)
{
    uint8 i = 0;
    ctc_dkits_hash_module_type_t module_type;
    hash_key_property_t* p_hash_db = NULL;
    tbls_id_t ad_id;
    uint32 key_type = 0;
    uint8 num = 0;

    sal_memset(p_hash_tbl_info, 0, sizeof(ctc_dkits_hash_tbl_info_t));

    _ctc_greatbelt_dkits_dump_hash2type(tblid, &module_type, &key_type);
    if ((key_type == USERID_HASH_TYPE_INVALID) || (module_type == CTC_DKITS_HASH_MODULE_NUM))
    {
        return;
    }

    _ctc_greatbelt_dkits_dump_hash2ad(tblid, &ad_id);

    if (CTC_DKITS_HASH_MODULE_NUM == module_type)
    {
        return;
    }

    if (module_type == CTC_DKITS_HASH_MODULE_SCL)
    {
         /*num = sizeof(userid_hash_key_property_table)/sizeof(hash_key_property_t);*/
        num = 32;
    }
    else if (module_type == CTC_DKITS_HASH_MODULE_FIB)
    {
         /*num = sizeof(fib_hash_key_property_table)/sizeof(hash_key_property_t);*/
        num = 8;
    }
    else if (module_type == CTC_DKITS_HASH_MODULE_IP)
    {
         /*num = sizeof(lpm_hash_key_property_table)/sizeof(hash_key_property_t);*/
        num = 16;
    }

    p_hash_db = p_hash_key_property[module_type];

    for (i = 0; i < num ; i++)
    {
        if (tblid == p_hash_db[i].table_id)
        {
            p_hash_tbl_info->valid = 1;
            p_hash_tbl_info->ad_tblid = ad_id;
            p_hash_tbl_info->hash_type = key_type;
            p_hash_tbl_info->valid_fld = p_hash_db[i].entry_valid_field[0];
            return;
        }
    }

    return;
}

STATIC void
_ctc_greatbelt_dkits_dump_get_tbl_order(tbls_id_t tblid, uint32* p_order)
{
    uint32 i = 0, j = 0, k =0, count = 0;
    uint32 hash_fun[] =
    {
        CTC_DKITS_SCL_HASH_FUN_TYPE_NUM,
        CTC_DKITS_FIB_HASH_FUN_TYPE_NUM,
        CTC_DKITS_IP_TCAM_FUN_TYPE_NUM
    };
    uint32 tcam_fun[] =
    {
        CTC_DKITS_ACL_TCAM_FUN_TYPE_NUM,
        CTC_DKITS_SCL_TCAM_FUN_TYPE_NUM,
        CTC_DKITS_IP_TCAM_FUN_TYPE_NUM
     };
    ctc_dkits_dump_tbl_type_t tbl_type;

    ctc_greatbelt_dkits_dump_get_tbl_type(tblid, &tbl_type);

    if (CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
    {
        for (i = 0; i < CTC_DKITS_TCAM_FUN_TYPE_NUM; i++)
        {
            for (j = 0; j < tcam_fun[i]; j++)
            {
                for (k = 0; MaxTblId_t != gb_tcam_key_inf[i][j][k].tblid; k++)
                {
                    count++;
                    if (gb_tcam_key_inf[i][j][k].tblid == tblid)
                    {
                        *p_order = count | DUMP_CFG_TCAM_ORDER_OFFSET;
                        goto CTC_DKITS_DUMP_GET_TBL_INFO_END;
                    }
                }
            }
        }
    }
    else if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == tbl_type)
            || (CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type))
    {
        for (i = 0; i < CTC_DKITS_HASH_FUN_TYPE_NUM; i++)
        {
            for (j = 0; j < hash_fun[i]; j++)
            {
                for (k = 0; MaxTblId_t != gb_hash_tbl[i][j][k]; k++)
                {
                    count++;
                    if (tblid == gb_hash_tbl[i][j][k])
                    {
                        *p_order = count | DUMP_CFG_HASH_ORDER_OFFSET;
                        goto CTC_DKITS_DUMP_GET_TBL_INFO_END;
                    }
                }
            }
        }
    }
    else if (CTC_DKITS_DUMP_TBL_TYPE_HN == tbl_type)
    {
        for (i = 0; i < CTC_DKITS_NH_TBL_TYPE_NUM; i++)
        {
            for (j = 0; MaxTblId_t != gb_nh_tbl[i][j]; j++)
            {
                count++;
                if (tblid == gb_nh_tbl[i][j])
                {
                    if (NULL != p_order)
                    {
                        *p_order = count | DUMP_CFG_NH_ORDER_OFFSET;
                        goto CTC_DKITS_DUMP_GET_TBL_INFO_END;
                    }
                }
            }
        }
    }

CTC_DKITS_DUMP_GET_TBL_INFO_END:

    return;
}

void
ctc_greatbelt_dkits_dump_get_tbl_type(tbls_id_t tblid, ctc_dkits_dump_tbl_type_t* p_tbl_type)
{
    ctc_dkits_hash_tbl_info_t hash_tbl_info;

    *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_STATIC;

    if (HssAccessCtl_t == tblid)
    {
        *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_SERDES;
    }
    else if ((DsIpv4UcastDaTcam_t == tblid)
            || (DsIpv6UcastDaTcam_t == tblid)
            || (DsIpv4McastDaTcam_t == tblid)
            || (DsMacIpv4Tcam_t == tblid)
            || (DsIpv6McastDaTcam_t == tblid)
            || (DsMacIpv6Tcam_t == tblid)
            || (DsIpv4SaNatTcam_t == tblid)
            || (DsIpv6SaNatTcam_t == tblid)
            || (DsIpv4UcastPbrDualDaTcam_t == tblid)
            || (DsIpv6UcastPbrDualDaTcam_t == tblid)
            || (DsTunnelIdIpv4Tcam_t == tblid)
            || (DsTunnelIdIpv6Tcam_t == tblid)
            || (DsTunnelIdPbbTcam_t == tblid)
            || (DsUserIdMacTcam_t == tblid)
            || (DsUserIdIpv4Tcam_t == tblid)
            || (DsUserIdIpv6Tcam_t == tblid)
            || (DsUserIdVlanTcam_t == tblid)
            || (DsMacAcl0Tcam_t == tblid)
            || (DsIpv4Acl0Tcam_t == tblid)
            || (DsMacAcl1Tcam_t == tblid)
            || (DsIpv4Acl1Tcam_t == tblid)
            || (DsMacAcl2Tcam_t == tblid)
            || (DsIpv4Acl3Tcam_t == tblid)
            || (DsIpv6Acl0Tcam_t == tblid)
            || (DsIpv6Acl1Tcam_t == tblid)
            || (DsMplsPbtBfdOamChan_t == tblid))
    {
        *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD;
    }
    else
    {
        if ((DsMac_t == tblid)
           || (DsIpDa_t == tblid)
           || (DsIpSaNat_t == tblid)
           || (DsTrillDa_t == tblid)
           || (DsFcoeSa_t == tblid)
           || (DsIpSaNat_t == tblid)
           || (DsAcl_t == tblid)
           || (DsUserId_t == tblid)
           || (DsTunnelId_t == tblid)
           || (DsEthOamChan_t == tblid))
        {
            *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_HASH_AD;
        }
        else if ((DsFwd_t == tblid)
                || (DsMetEntry_t == tblid)
                || (DsNextHop4W_t == tblid)
                || (DsNextHop8W_t == tblid)
                || (DsL2EditEth4W_t == tblid)
                || (DsL2EditEth8W_t == tblid)
                || (DsL2EditFlex8W_t == tblid)
                || (DsL2EditLoopback_t == tblid)
                || (DsL2EditPbb4W_t == tblid)
                || (DsL2EditPbb8W_t == tblid)
                || (DsL2EditSwap_t == tblid)
                || (DsL3EditFlex_t == tblid)
                || (DsL3EditMpls4W_t == tblid)
                || (DsL3EditMpls8W_t == tblid)
                || (DsL3EditNat4W_t == tblid)
                || (DsL3EditNat8W_t == tblid)
                || (DsL3EditTunnelV4_t == tblid)
                || (DsL3EditTunnelV6_t == tblid))
        {
            *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_HN;
        }
        else if (_ctc_greatbelt_dkits_dump_check_dbg(tblid))
        {
            *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_DBG;
        }
        else
        {
            if (TABLE_EXT_INFO_PTR(tblid))
            {
                if (EXT_INFO_TYPE_TCAM == TABLE_EXT_INFO_TYPE(tblid))
                {
                    *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY;
                    return;
                }
            }

            if (NULL == TABLE_EXT_INFO_PTR(tblid))
            {
                *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_STATIC;
                return;
            }

            sal_memset(&hash_tbl_info, 0, sizeof(ctc_dkits_hash_tbl_info_t));
            _ctc_greatbelt_dkits_dump_get_hash_info(tblid, &hash_tbl_info);
            if (hash_tbl_info.valid)
            {
                if (hash_tbl_info.ad_tblid == DKITS_NO_AD_TBL)
                {
                    /*if hash no ad, process as CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD*/
                    *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_HASH_NO_AD;
                }
                else
                {
                    *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY;
                }
            }
            else
            {
                *p_tbl_type = CTC_DKITS_DUMP_TBL_TYPE_INVALID;
                CTC_DKIT_PRINT("Error tbl type: %s\n", TABLE_NAME(tblid));
            }
        }
    }

    return;
}


/**
 @brief Real embeded LPM tcam read operation I/O
*/
STATIC int32
_ctc_greatbelt_dkits_dump_read_int_lpm_tcam_entry(uint8 chip_id, uint32 index,
                                  uint32* data, tcam_mem_type_e type)
{
    fib_tcam_cpu_access_t access;
    uint32 time_out = 0;
    fib_tcam_read_data_t tcam_data;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);

    if (index >= DRV_INT_LPM_TCAM_MAX_ENTRY_NUM)
    {
        return -1;
    }

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));

    access.tcam_cpu_req_index = index;
    access.tcam_cpu_req = TRUE;
    if (DRV_INT_LPM_TCAM_RECORD_MASK == type)
    {
        access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_LPM_TCAM_RECORD_DATA == type)
    {
        access.tcam_cpu_req_type = FIB_CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        return DRV_E_INVALID_TCAM_TYPE;
    }

    cmd = DRV_IOW(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(chip_id, 0, cmd, &access);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (access.tcam_cpu_rd_data_valid)
        {
            break;
        }

        if ((time_out++) > 1000)
        {
            return -1;
        }
    }

    cmd = DRV_IOR(FibTcamReadData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    data[0] = tcam_data.tcam_read_data0;
    data[1] = tcam_data.tcam_read_data1;
    data[2] = tcam_data.tcam_read_data2;

    access.tcam_cpu_rd_data_valid = FALSE;

    cmd = DRV_IOW(FibTcamCpuAccess_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief Real embeded tcam read operation I/O
*/
STATIC int32
_ctc_greatbelt_dkits_dump_read_int_tcam_entry(uint8 chip_id, uint32 index,
                              uint32* data, tcam_mem_type_e type)
{
    tcam_ctl_int_cpu_access_cmd_t access;
    uint32 time_out = 0;
    tcam_ctl_int_cpu_rd_data_t tcam_data;
    uint32 cmd;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(data);

    if (index >= DRV_INT_TCAM_KEY_MAX_ENTRY_NUM)
    {
        return -1;
    }

    sal_memset(&access, 0, sizeof(access));
    sal_memset(&tcam_data, 0, sizeof(tcam_data));

    access.cpu_index = index;
    access.cpu_req = TRUE;
    if (DRV_INT_TCAM_RECORD_MASK == type)
    {
        access.cpu_req_type = CPU_ACCESS_REQ_READ_Y;
    }
    else if (DRV_INT_TCAM_RECORD_DATA == type)
    {
        access.cpu_req_type = CPU_ACCESS_REQ_READ_X;
    }
    else
    {
        return DRV_E_INVALID_TCAM_TYPE;
    }

    cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    while (TRUE)
    {
        cmd = DRV_IOR(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(chip_id, 0, cmd, &access);
        if (ret < DRV_E_NONE)
        {
            return ret;
        }

        if (access.cpu_read_data_valid)
        {
            break;
        }

        if ((time_out++) > 1000)
        {
            return DRV_E_TIME_OUT;
        }
    }

    cmd = DRV_IOR(TcamCtlIntCpuRdData_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &tcam_data);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    data[0] = tcam_data.tcam_read_data0;
    data[1] = tcam_data.tcam_read_data1;
    data[2] = tcam_data.tcam_read_data2;

    access.cpu_read_data_valid = FALSE;

    cmd = DRV_IOW(TcamCtlIntCpuAccessCmd_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(chip_id, 0, cmd, &access);
    if (ret < DRV_E_NONE)
    {
        return ret;
    }

    return DRV_E_NONE;
}

/**
 @brief convert embeded tcam content from X/Y format to data/mask format
*/
STATIC int32
_ctc_greatbelt_dkits_dump_convert_tcam_dump_content(uint8 chip_id, uint32* data, uint32* mask, uint32* p_empty)
{
#define TCAM_ENTRY_WIDTH 80
    uint32 bit_pos = 0;
    uint32 index = 0, bit_offset = 0;

    /* for IBM embeded tcam, mask bit = 1 denote compare,
       self-define the return mask bit = 1 denote compare
       to unify the embeded and external tcam read interface's return value;

       X    Y    Read_Data    Read_Mask   Return_Data   Return_Mask
       0    0       X            0            0            0
       0    1       1            1            1            1
       1    0       0            1            0            1
       1    1       X            X            0            0
       X=1, Y=1: No Write but read.  */

    /* data[1] -- [64,80]; data[2] -- [32,63]; data[3] -- [0,31] */
    for (bit_pos = 0; bit_pos < TCAM_ENTRY_WIDTH; bit_pos++)
    {
        index = 2 - bit_pos / 32;
        bit_offset = bit_pos % 32;

        if ((IS_BIT_SET(data[index], bit_offset))
            && IS_BIT_SET(mask[index], bit_offset))    /* X = 1; Y = 1 */
        {
            *p_empty |= 1;
            break;
        }

    }

    return 0;
}
/**
 @brief read tcam interface (include operate model and real tcam)
*/
int32
_ctc_greatbelt_dkit_tcam_tbl_empty(uint8 chip_id, tbls_id_t tbl_id, uint32 index,  uint32* p_empty)
{
    uint32 hw_database_addr = TABLE_DATA_BASE(tbl_id);
    uint32 key_size = TABLE_EXT_INFO_PTR(tbl_id)? TCAM_KEY_SIZE(tbl_id) : 0;
    uint32 entry_num_each_idx, entry_idx;
    bool is_lpm_tcam = FALSE;
    uint32 index_by_tcam;
    uint32 max_int_tcam_data_base_tmp;
    uint32 max_int_lpm_tcam_data_base_tmp;
    ds_t    data;
    ds_t    mask;
    tbl_entry_t entry;
    uint32* p_mask = NULL;
    uint32* p_data = NULL;

    sal_memset(&data, 0, sizeof(data));
    sal_memset(&mask, 0, sizeof(mask));
    sal_memset(&entry, 0, sizeof(tbl_entry_t));

    if (!drv_greatbelt_table_is_tcam_key(tbl_id))
    {
        return -1;
    }

    if (TABLE_MAX_INDEX(tbl_id) <= index)
    {
        return -1;
    }

    entry.data_entry = (uint32 *)&data;
    entry.mask_entry = (uint32 *)&mask;
    *p_empty = 0;

    max_int_tcam_data_base_tmp = DRV_INT_TCAM_KEY_DATA_ASIC_BASE
        + (DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);
    max_int_lpm_tcam_data_base_tmp = DRV_INT_LPM_TCAM_DATA_ASIC_BASE
        + (DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_BYTES_PER_ENTRY);

    /* the key's each index includes 80 bits entry number */
    entry_num_each_idx = key_size / DRV_BYTES_PER_ENTRY;

    if ((hw_database_addr >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE)
        && (hw_database_addr < max_int_tcam_data_base_tmp))
    {

        if (8 == entry_num_each_idx)
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * 4);
        }
        else
        {
            index_by_tcam = (hw_database_addr - DRV_INT_TCAM_KEY_DATA_ASIC_BASE) / DRV_BYTES_PER_ENTRY + (index * entry_num_each_idx);
        }

        is_lpm_tcam = FALSE;
    }
    else if ((hw_database_addr >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE)
             && (hw_database_addr < max_int_lpm_tcam_data_base_tmp))
    {
        index_by_tcam = (hw_database_addr - DRV_INT_LPM_TCAM_DATA_ASIC_BASE + (index * DRV_BYTES_PER_ENTRY)) / DRV_BYTES_PER_ENTRY;
        is_lpm_tcam = TRUE;
    }
    else
    {
        return -1;
    }


    p_data = entry.data_entry;
    p_mask = entry.mask_entry;

    for (entry_idx = 0; entry_idx < entry_num_each_idx; ++entry_idx)
    {
        /* read real tcam address */
        if (!is_lpm_tcam)
        {
            if (!IS_BIT_SET(entry_idx, 2))
            {
                DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_tcam_entry(chip_id, index_by_tcam,
                                                                  p_mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_tcam_entry(chip_id, index_by_tcam,
                                                                  p_data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_DATA));
            }
            else
            {
                DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_tcam_entry(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX),
                                                                  p_mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_MASK));

                DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_tcam_entry(chip_id, (index_by_tcam - 4 + DRV_ACL_TCAM_640_OFFSET_IDX),
                                                                  p_data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_TCAM_RECORD_DATA));
            }
        }
        else
        {
            DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_lpm_tcam_entry(chip_id, index_by_tcam,
                                                                  p_mask + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_LPM_TCAM_RECORD_MASK));

            DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_read_int_lpm_tcam_entry(chip_id, index_by_tcam,
                                                                  p_data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                  DRV_INT_LPM_TCAM_RECORD_MASK));
        }

        DRV_IF_ERROR_RETURN(_ctc_greatbelt_dkits_dump_convert_tcam_dump_content(chip_id,
                                                                p_data + entry_idx * DRV_WORDS_PER_ENTRY,
                                                                p_mask + entry_idx * DRV_WORDS_PER_ENTRY, p_empty));

        index_by_tcam++;
    }

    return 0;
}

STATIC int32
_ctc_greatbelt_dkits_tcam_check_tbl_empty(uint32 tbl_id, uint32* p_data, uint32* p_empty)
{
    uint8 i = 0, j = 0;
    fields_t* fld_ptr = NULL;
    tables_info_t* tbl_ptr = NULL;
    uint32 data[MAX_ENTRY_WORD] = {0};
    *p_empty = 1;

    tbl_ptr = TABLE_INFO_PTR(tbl_id);

    for (i = 0; i < tbl_ptr->num_fields; i++)
    {
        sal_memset(data, 0, sizeof(data));
        fld_ptr = &(tbl_ptr->ptr_fields[i]);

        drv_greatbelt_get_field(tbl_id, i, (uint32*)p_data, data);

        for (j = 0; j < ((fld_ptr->len + 31)/32); j++)
        {
            if (0 != data[j])
            {
                *p_empty = 0;
                break;
            }
        }
        if (0 == *p_empty)
        {
            break;
        }
    }

    return 0;
}

#if 0
STATIC int32
_ctc_greatbelt_dkits_dump_get_tcam_ad(tbls_id_t key_tblid, ctc_dkits_dump_tbl_entry_t* p_tbl_entry, tbls_id_t* p_ad_tblid)
{
    int32 ret = CLI_SUCCESS;
    uint32 key_type;

    ret = _ctc_greatbelt_dkits_dump_parser_tcam_type(key_tblid, p_tbl_entry, &key_type);
    ret = ret ? ret : _ctc_greatbelt_dkits_dump_tcam_key2ad(key_tblid, key_type, p_ad_tblid);

    return ret;
}
#endif

int32
ctc_greatbelt_dkits_tcam_read_tcam_key(tbls_id_t tbl_id, uint32 index, uint32* p_empty, tbl_entry_t* p_tcam_key)
{
    uint8  lchip = 0;
    int32  ret = 0;
    uint32 cmd = 0;
    uint32 data_empty = 0;
    uint32 mask_empty = 0;

    sal_memset(p_tcam_key->data_entry, 0, sizeof(ds_t));
    sal_memset(p_tcam_key->mask_entry, 0, sizeof(ds_t));

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, index, cmd, p_tcam_key);
    if (ret < 0)
    {
        CTC_DKIT_PRINT("Read tcam key %s:%u error!\n", TABLE_NAME(tbl_id), index);
        return CLI_ERROR;
    }

    if (0 == SDK_WORK_PLATFORM)
    {
        _ctc_greatbelt_dkit_tcam_tbl_empty(0, tbl_id, index, p_empty);
    }
    else
    {
        _ctc_greatbelt_dkits_tcam_check_tbl_empty(tbl_id, p_tcam_key->data_entry, &data_empty);
        _ctc_greatbelt_dkits_tcam_check_tbl_empty(tbl_id, p_tcam_key->mask_entry, &mask_empty);
        *p_empty = (1 == data_empty) || (1 == mask_empty);
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_decode_entry(uint8 lchip, void* p_para1, void* p_para2)
{
    ctc_dkits_hash_tbl_info_t hash_tbl_info;
    ctc_dkits_dump_tbl_type_t type = 0;
    tbls_id_t tb_id = MaxTblId_t;
    uint32 index = 0;
    ctc_dkits_dump_tbl_block_hdr_t* p_hdr = NULL;
    ctc_dkits_dump_key_tbl_info_t*  p_info = NULL;

    p_hdr = (ctc_dkits_dump_tbl_block_hdr_t*)p_para1;
    p_info = (ctc_dkits_dump_key_tbl_info_t*)p_para2;

    tb_id = CTC_DKITS_DUMP_BLOCK_HDR_TBLID(p_hdr);
    index = CTC_DKITS_DUMP_BLOCK_HDR_TBLIDX(p_hdr);

    ctc_greatbelt_dkits_dump_get_tbl_type(tb_id, &type);

    if (CTC_DKITS_DUMP_TBL_TYPE_HASH_KEY == type)
    {
         _ctc_greatbelt_dkits_dump_get_hash_info(tb_id, &hash_tbl_info);
        if ((!hash_tbl_info.valid)|| (MaxTblId_t == hash_tbl_info.ad_tblid))
        {
            return -1;
        }
    }

    if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == type) || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == type))
    {
        p_info->ad_tblid = tb_id;
        p_info->ad_idx = index;
        p_info->tbl_type = type;
    }
    else
    {
        _ctc_greatbelt_dkits_dump_get_tbl_order(tb_id, &p_info->order);
        p_info->tblid = tb_id;
        p_info->idx = index;
        p_info->tbl_type = type;
    }

    return 0;
}

STATIC void
_ctc_greatbelt_dkits_dump_search_cam(uint8 chip_id, uint32 key_index,  ds_mac_hash_key_t* ds_mac_hash_key, uint8 b_read)
{
    uint32            cmd = 0;
    dynamic_ds_fdb_lookup_index_cam_t dynamic_ds_fdb_lookup_index_cam;


    sal_memset(&dynamic_ds_fdb_lookup_index_cam, 0, sizeof(dynamic_ds_fdb_lookup_index_cam_t));

    cmd = DRV_IOR(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
    (DRV_IOCTL(chip_id, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));
    if(b_read)
    {
        if (key_index % 2)
        {
            sal_memcpy(ds_mac_hash_key, (uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                         sizeof(ds_3word_hash_key_t));
        }
        else
        {
            sal_memcpy(ds_mac_hash_key, (uint8*)&dynamic_ds_fdb_lookup_index_cam, sizeof(ds_3word_hash_key_t));
        }
    }
    else
    {
        if (key_index % 2)
        {
            sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam + sizeof(ds_3word_hash_key_t),
                       ds_mac_hash_key,  sizeof(ds_3word_hash_key_t));
        }
        else
        {
            sal_memcpy((uint8*)&dynamic_ds_fdb_lookup_index_cam, ds_mac_hash_key, sizeof(ds_3word_hash_key_t));
        }

        cmd = DRV_IOW(DynamicDsFdbLookupIndexCam_t, DRV_ENTRY_FLAG);
        (DRV_IOCTL(chip_id, key_index / 2, cmd, &dynamic_ds_fdb_lookup_index_cam));
    }

    return;
}

#define _____DUMP_____
STATIC int32
_ctc_greatbelt_dkits_dump_hash_tbl(uint8 lchip, uint32 fun_module, uint8 fun, sal_file_t p_f)
{
    int32 ret = 0;
    uint32 i = 0, j = 0, cmd = 0, ad_idx = 0, valid = 0, step = 0;
    ctc_dkits_hash_tbl_info_t hash_tbl_info;
    ctc_dkits_ds_t ad, hash_key;
    ds_mac_hash_key_t cam_key;
    ctc_dkits_hash_module_type_t module_type;
    uint32 key_type = 0;
    lookup_info_t look_info;
    ds_user_id_svlan_cos_hash_key_t* p_userid = NULL;
    ds_lpm_ipv4_hash8_key_t* p_lpm = NULL;
    ds_mac_hash_key_t* p_mac = NULL;
    uint32 hash_type = 0;
    uint32 is_mac = 0;
    uint32 k = 0;

    for (i = 0; MaxTblId_t != gb_hash_tbl[fun_module][fun][i]; i++)
    {
        _ctc_greatbelt_dkits_dump_get_hash_info(gb_hash_tbl[fun_module][fun][i], &hash_tbl_info);

        if (!hash_tbl_info.valid)
        {
            CTC_DKIT_PRINT_DEBUG("Error hash tbl %s\n", TABLE_NAME(gb_hash_tbl[fun_module][fun][i]));
            continue;
        }

        step = (TABLE_ENTRY_SIZE(gb_hash_tbl[fun_module][fun][i]) / DRV_BYTES_PER_ENTRY);

        _ctc_greatbelt_dkits_dump_hash2type(gb_hash_tbl[fun_module][fun][i], &module_type, &key_type);
        if ((key_type == USERID_HASH_TYPE_INVALID) || (module_type == CTC_DKITS_HASH_MODULE_NUM))
        {
            continue;
        }

        for (j = 0; j < TABLE_MAX_INDEX(gb_hash_tbl[fun_module][fun][i]); j += step)
        {
            cmd = DRV_IOR(gb_hash_tbl[fun_module][fun][i], DRV_ENTRY_FLAG);
            ret = DRV_IOCTL(lchip, j, cmd, &hash_key);
            if (ret < 0)
            {
                CTC_DKIT_PRINT_DEBUG("Error read tbl %s[%u]\n", TABLE_NAME(gb_hash_tbl[fun_module][fun][i]), j);
                continue;
            }

            drv_greatbelt_get_field(gb_hash_tbl[fun_module][fun][i], hash_tbl_info.valid_fld, hash_key.entry, &valid);
            if (!valid)
            {
                continue;
            }

            /*get real hash type and compare to stored type*/
            if (module_type == CTC_DKITS_HASH_MODULE_SCL)
            {
                p_userid = (ds_user_id_svlan_cos_hash_key_t*)hash_key.entry;
                drv_greatbelt_get_field(DsUserIdSvlanHashKey_t, DsUserIdSvlanHashKey_HashType_f, (void*)p_userid, &hash_type);
                if (hash_type != key_type+1)
                {
                    CTC_DKIT_PRINT_DEBUG("tb:%d, Hash type:%d, key type:%d \n", gb_hash_tbl[fun_module][fun][i], hash_type, key_type);
                    continue;
                }
            }
            else if (module_type == CTC_DKITS_HASH_MODULE_IP)
            {
                p_lpm = (ds_lpm_ipv4_hash8_key_t*)hash_key.entry;
                drv_greatbelt_get_field(DsLpmIpv4Hash8Key_t, DsLpmIpv4Hash8Key_HashType_f, (void*)p_lpm, &hash_type);
                if (hash_type != key_type)
                {
                    CTC_DKIT_PRINT_DEBUG("tb:%d, Hash type:%d, key type:%d \n", gb_hash_tbl[fun_module][fun][i], hash_type, key_type);
                    continue;
                }
            }
            else if (module_type == CTC_DKITS_HASH_MODULE_FIB)
            {
                p_mac = (ds_mac_hash_key_t*)hash_key.entry;
                drv_greatbelt_get_field(DsMacHashKey_t, DsMacHashKey_IsMacHash_f, (void*)p_mac, &is_mac);
                if ((CTC_DKITS_FIB_HASH_FUN_TYPE_BRIDGE == fun) && (!is_mac))
                {
                    continue;
                }

                if ((CTC_DKITS_FIB_HASH_FUN_TYPE_FLOW == fun) && (is_mac))
                {
                    continue;
                }
            }

            if (module_type == CTC_DKITS_HASH_MODULE_FIB)
            {
                ctc_greatbelt_dkits_dump_tbl2data(gb_hash_tbl[fun_module][fun][i], j + 32, hash_key.entry, NULL, p_f);
            }
            else
            {
                ctc_greatbelt_dkits_dump_tbl2data(gb_hash_tbl[fun_module][fun][i], j, hash_key.entry, NULL, p_f);
            }

            /*get ad index*/
            if (DKITS_NO_AD_TBL != hash_tbl_info.ad_tblid)
            {
                sal_memset(&look_info, 0, sizeof(lookup_info_t));
                look_info.hash_module = (hash_module_t)module_type;
                look_info.hash_type = key_type;
                look_info.p_ds_key = hash_key.entry;
                drv_greatbelt_hash_lookup_get_key_ad_index(&look_info, &ad_idx);

                if (MaxTblId_t != hash_tbl_info.ad_tblid)
                {
                    cmd = DRV_IOR(hash_tbl_info.ad_tblid, DRV_ENTRY_FLAG);
                    ret = DRV_IOCTL(lchip, ad_idx, cmd, &ad);
                    if (ret < 0)
                    {
                        CTC_DKIT_PRINT_DEBUG("Error read tbl %s[%u]\n", TABLE_NAME(hash_tbl_info.ad_tblid), ad_idx);
                        continue;
                    }
                    ctc_greatbelt_dkits_dump_tbl2data(hash_tbl_info.ad_tblid, ad_idx, ad.entry, NULL, p_f);
                }
            }
        }

        /*GB only fib hash using cam*/
        if (module_type == CTC_DKITS_HASH_MODULE_FIB)
        {

            for (k = 0; k < 16; k++)
            {
                _ctc_greatbelt_dkits_dump_search_cam(0, k, &cam_key, 1);
                if (cam_key.valid == 0)
                {
                    continue;
                }

                /*dump cam key*/
                ctc_greatbelt_dkits_dump_tbl2data(DsMacHashKey_t, k, (uint32*)&cam_key, NULL, p_f);

                /*dump ad*/
                ad_idx = (cam_key.ds_ad_index14_3 << 3) | (cam_key.ds_ad_index2_2 << 2) | cam_key.ds_ad_index1_0;

                cmd = DRV_IOR(DsMac_t, DRV_ENTRY_FLAG);
                ret = DRV_IOCTL(lchip, ad_idx, cmd, &ad);
                if (ret < 0)
                {
                    CTC_DKIT_PRINT_DEBUG("%s %d, error read tbl %s[%u]\n",
                                         __FUNCTION__, __LINE__, TABLE_NAME(hash_tbl_info.ad_tblid), ad_idx);
                    continue;
                }
                ctc_greatbelt_dkits_dump_tbl2data(DsMac_t, ad_idx, ad.entry, NULL, p_f);
            }
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_write_tbl(void* p_para4, tbls_id_t tblid, void* p_para1, void* p_para2, void* p_para3)
{
    int32 ret = CLI_SUCCESS;
    uint32 i = 0, cmd = 0;
    uint8 lchip = 0;
    sal_file_t p_f = (sal_file_t)p_para1;
    uint32 data_entry[MAX_ENTRY_WORD] = {0};
    uint32* p_total_count = (uint32*)p_para2;
    uint32* p_curr_count = (uint32*)p_para3;

    lchip = *((uint8*)p_para4);

    cmd = DRV_IOR(tblid, DRV_ENTRY_FLAG);

    for (i = 0; i < TABLE_MAX_INDEX(tblid); i++)
    {
        if (0 == ((*p_curr_count) % (*p_total_count / 30)))
        {
            sal_udelay(10);
        }

        ret = DRV_IOCTL(lchip, i, cmd, &data_entry);
        if (ret < 0)
        {
            CTC_DKIT_PRINT("Read tbl %s error, tblsid = %u.!\n", TABLE_NAME(tblid), tblid);
            return CLI_SUCCESS;
        }

        *p_curr_count += 1;
        ret = ctc_greatbelt_dkits_dump_tbl2data(tblid, i, data_entry, NULL, p_f);
        if (CLI_ERROR == ret)
        {
            return CLI_ERROR;
        }

    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_count_tbl(void* p_para4, tbls_id_t tblid, void* p_para1, void* p_para2, void* p_para3)
{
    *((uint32*)p_para1) += TABLE_MAX_INDEX(tblid);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_travel_static_cfg(void* p_para1, void* p_para2, void* p_para3, tbls_id_t* p_tblid, travel_callback_fun_t p_fun)
{
    int32   ret = 0;
    uint32 count = 0, i = 0;

    for (i = 0; MaxTblId_t != p_tblid[i]; i++)
    {
        ret = (*p_fun)(p_para1, p_tblid[i], p_para2, p_para3, &count);
    }

    return ret;
}

STATIC int32
_ctc_greatbelt_dkits_dump_travel_static_all(void* p_para1, void* p_para2, void* p_para3, travel_callback_fun_t p_fun)
{
    int32 ret = 0;
    uint32 i = 0;
    uint32 count = 0;

    for (i = 0; i < MaxTblId_t; i++)
    {
        if (_ctc_greatbelt_dkits_dump_invalid_static_tbl(i))
        {
            continue;
        }

        if (NULL == TABLE_EXT_INFO_PTR(i))
        {
            ret = (*p_fun)(p_para1, i, p_para2, p_para3, &count);
            if (CLI_ERROR == ret)
            {
                return CLI_ERROR;
            }
        }
    }

     /*ret = (*p_fun)(EfdElephantFlowBitmapCtl_t, p_para1, p_para2, &count);*/

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkits_dump_indirect(uint8 lchip, sal_file_t p_f, ctc_dkits_dump_func_flag_t flag)
{
    int32  ret = CLI_SUCCESS;

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_SERDES))
    {
        uint8  serdes_id = 0, addr_offset = 0;
        ctc_dkit_serdes_mode_t serdes_mode = 0;
        ctc_dkit_serdes_wr_t serdes_para;
        ctc_dkits_dump_serdes_tbl_t gb_serdes_tbl;
        void* p_data = NULL;

        for (serdes_id = 0; serdes_id < 32; serdes_id++)
        {
            sal_udelay(10);
            for (serdes_mode = CTC_DKIT_SERDES_TX; serdes_mode <= CTC_DKIT_SERDES_PLLA; serdes_mode++)
            {
                for (addr_offset = 0; addr_offset < 0x40; addr_offset++)
                {
                    sal_memset(&serdes_para, 0, sizeof(ctc_dkit_serdes_wr_t));
                    serdes_para.lchip = lchip;
                    serdes_para.serdes_id = serdes_id;
                    serdes_para.type = serdes_mode;
                    serdes_para.addr_offset = addr_offset;
                    ret = ret ? ret : ctc_greatbelt_dkit_misc_read_serdes(&serdes_para);

                    gb_serdes_tbl.serdes_id = serdes_id;
                    gb_serdes_tbl.serdes_mode = serdes_mode;
                    gb_serdes_tbl.offset = addr_offset;
                    gb_serdes_tbl.data = serdes_para.data;
                    p_data = (void*)&gb_serdes_tbl;

                    ret = ret ? ret : ctc_greatbelt_dkits_dump_tbl2data(HssAccessCtl_t, 0, (uint32*)p_data, NULL, p_f);

                }
            }
        }
    }

    return ret;
}

STATIC int32
_ctc_greatbelt_dkits_dump_tcam_tbl(uint8 lchip, ctc_dkits_tcam_tbl_module_t module, uint8 fun, sal_file_t p_f)
{
    ds_t   data, mask;
    int32  ret = 0;
    uint32 t = 0, i = 0, is_empty = 0, cmd = 0;
    tbl_entry_t tcam_key;
    uint32 key_type = 0;
    uint32 tcam_type = 0;
    tbls_id_t ad_tblid;
    ds_acl_mac_key0_t* p_mac_acl = NULL;

    tcam_key.data_entry = (uint32 *)&data;
    tcam_key.mask_entry = (uint32 *)&mask;

    for (t = 0; MaxTblId_t != gb_tcam_key_inf[module][fun][t].tblid; t++)
    {
        for (i = 0; i < TABLE_MAX_INDEX(gb_tcam_key_inf[module][fun][t].tblid); i++)
        {
            sal_memset(&data, 0, sizeof(data));
            sal_memset(&mask, 0, sizeof(mask));
            is_empty = 0;

            ret = ctc_greatbelt_dkits_tcam_read_tcam_key(gb_tcam_key_inf[module][fun][t].tblid, i, &is_empty, &tcam_key);
            if (ret < 0)
            {
                continue;
            }

            if (!is_empty)
            {
                _ctc_greatbelt_dkits_dump_tcam_key2ad(gb_tcam_key_inf[module][fun][t].tblid, key_type, &ad_tblid);
                key_type = gb_tcam_key_inf[module][fun][t].key_type;

                p_mac_acl = (ds_acl_mac_key0_t*)tcam_key.data_entry;
                drv_greatbelt_get_field(DsAclMacKey0_t, DsAclMacKey0_SubTableId1_f, (void*)p_mac_acl, &tcam_type);
                if (tcam_type+1 != key_type)
                {
                    CTC_DKIT_PRINT_DEBUG("tb:%d, index:%d, tcam_type:%d, key type:%d \n", gb_tcam_key_inf[module][fun][t].tblid, i, tcam_type, key_type);
                    continue;
                }

                ctc_greatbelt_dkits_dump_tbl2data(gb_tcam_key_inf[module][fun][t].tblid,
                                        i, tcam_key.data_entry, tcam_key.mask_entry, p_f);

                if (MaxTblId_t != ad_tblid)
                {
                    cmd = DRV_IOR(ad_tblid, DRV_ENTRY_FLAG);
                    ret = DRV_IOCTL(lchip, i, cmd, data);
                    if (ret < 0)
                    {
                        CTC_DKIT_PRINT("Error read tbl %s\n", TABLE_NAME(ad_tblid));
                        continue;
                    }
                    ctc_greatbelt_dkits_dump_tbl2data(ad_tblid, i, data, NULL, p_f);
                }
            }
        }
    }

    return CLI_SUCCESS;
}

#define _____TCAM_USAGE_____

char* gb_tcam_module_desc[CTC_DKITS_TCAM_FUN_TYPE_NUM] = {"ACL Tcam", "SCL Tcam", "IP Tcam"};

STATIC void
_ctc_greatbelt_dkits_dump_tcam_resc(uint8 type)
{
    uint32  i = 0, j = 0, is_empty = 0, sum = 0, key_type = 0, sid = 0xFFFFFFFF;
    uint32  alloc[20] = {0}, used[20] = {0}, total_alloc = 0, total_used = 0, max = 0, key_size = 0;
    tbl_entry_t tcam_key;
    ds_t    data;
    ds_t    mask;
    uint32 tcam_type = 0;
    ctc_dkits_dump_tbl_entry_t tbl_entry;
    ds_acl_mac_key0_t* p_mac_acl = NULL;

    sal_memset(&data, 0, sizeof(data));
    sal_memset(&mask, 0, sizeof(mask));

    tcam_key.data_entry = (uint32 *)&data;
    tcam_key.mask_entry = (uint32 *)&mask;

    for (i = 0;  (MaxTblId_t != gb_tcam_resc_inf[type][i].tblid); i++)
    {

        sum = 0;
        for (j = 0; j < TABLE_MAX_INDEX(gb_tcam_resc_inf[type][i].tblid); j++)
        {
            ctc_greatbelt_dkits_tcam_read_tcam_key(gb_tcam_resc_inf[type][i].tblid, j, &is_empty, &tcam_key);

            if (!is_empty)
            {
                sal_memset(&tbl_entry, 0, sizeof(ctc_dkits_dump_tbl_entry_t));
                tbl_entry.data_entry = tcam_key.data_entry;
                tbl_entry.mask_entry = tcam_key.mask_entry;

                key_type = gb_tcam_resc_inf[type][i].key_type;

                p_mac_acl = (ds_acl_mac_key0_t*)tcam_key.data_entry;
                drv_greatbelt_get_field(DsAclMacKey0_t, DsAclMacKey0_SubTableId1_f, (void*)p_mac_acl, &tcam_type);
                if (tcam_type+1 == key_type)
                {
                    sum++;
                }
            }
        }

        key_size = 0;
        if (TABLE_EXT_INFO_PTR(gb_tcam_resc_inf[type][i].tblid))
        {
            key_size = TCAM_KEY_SIZE(gb_tcam_resc_inf[type][i].tblid);
        }
        max = TABLE_MAX_INDEX(gb_tcam_resc_inf[type][i].tblid);

        alloc[i]    = max * key_size / DRV_BYTES_PER_ENTRY;
        used[i]     = sum * key_size / DRV_BYTES_PER_ENTRY;

        if (sid != gb_tcam_resc_inf[type][i].sid)
        {
            sid = gb_tcam_resc_inf[type][i].sid;
            total_alloc += alloc[i];
        }
        total_used  += used[i];
    }

    for (i = 0; (MaxTblId_t != gb_tcam_resc_inf[type][i].tblid); i++)
    {
        if (0 == i)
        {
            CTC_DKIT_PRINT(" %-15s%-28s%-13u%u\n",
                           gb_tcam_module_desc[type], "AllKey",
                           total_alloc, total_used);
        }
        if (sid != gb_tcam_resc_inf[type][i].sid)
        {
            sid = gb_tcam_resc_inf[type][i].sid;
            CTC_DKIT_PRINT(" %-15s%-28s%-13u%u\n",
                           " ", TABLE_NAME(gb_tcam_resc_inf[type][i].tblid),
                           alloc[i], used[i]);
        }
        else
        {
            CTC_DKIT_PRINT(" %-15s%-28s%-13s%u\n",
                           " ", TABLE_NAME(gb_tcam_resc_inf[type][i].tblid),
                           "", used[i]);
        }
    }

    CTC_DKIT_PRINT("\n");
    return;
}
STATIC int32
_ctc_greatbelt_dkits_dump_tcam_mem_usage(void)
{
    uint8 i = 0;

    CTC_DKIT_PRINT("\n");
    /* CTC_DKIT_PRINT(" Note:when TotalMem is 'S', means the memory of key is shared with the above key.\n"); */
    CTC_DKIT_PRINT(" ----------------------------------------------------------------\n");
    CTC_DKIT_PRINT(" %-15s%-28s%-13s%s\n", "TcamModule", "KeyName", "Total(80b)", "Used(80b)");
    CTC_DKIT_PRINT(" ----------------------------------------------------------------\n");

    for (i = 0; i < CTC_DKITS_TCAM_FUN_TYPE_NUM; i++)
    {
        _ctc_greatbelt_dkits_dump_tcam_resc(i);
         sal_udelay(10);
    }
    CTC_DKIT_PRINT(" ----------------------------------------------------------------\n\n");

    return CLI_SUCCESS;
}

#define _____HASH_USAGE_____

STATIC uint32
_ctc_greatbelt_dkits_dump_hash_level_num(ctc_dkits_hash_module_type_t type)
{
    uint32 level_num = 0;
    if (CTC_DKITS_HASH_MODULE_SCL == type)
    {
        level_num = 2;
    }
    else if (CTC_DKITS_HASH_MODULE_FIB == type)
    {
        level_num = 5;
    }
    else if (CTC_DKITS_HASH_MODULE_IP == type)
    {
        level_num = 2;
    }

    return level_num;
}

STATIC int32
_ctc_greatbelt_dkits_dump_hash_get_level_max(ctc_dkits_hash_module_type_t type, uint32 level, uint32* max_idx)
{
    uint8  lchip = 0;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 field_val = 0;

    if (CTC_DKITS_HASH_MODULE_SCL == type)
    {
        /* DsUserId {edram7, edram0} */
        field_id = DynamicDsDsUserIdHashIndexCam_DsUserIdHashMaxIndex0_f;
        cmd = DRV_IOR(DynamicDsDsUserIdHashIndexCam_t, field_id);
        (DRV_IOCTL(lchip, 0, cmd, &field_val));
        *max_idx = 16383;
    }
    else if (CTC_DKITS_HASH_MODULE_FIB == type)
    {
        /* HashKey {edram0, edram1, edram2, edram3, edram6 } */
        field_id = DynamicDsMacHashIndexCam_DsMacHashMaxIndex0_f+level;
        cmd = DRV_IOR(DynamicDsMacHashIndexCam_t, field_id);
        (DRV_IOCTL(lchip, 0, cmd, &field_val));
        *max_idx = (field_val << 10) |0x3ff;
    }
    else if (CTC_DKITS_HASH_MODULE_IP == type)
    {
        /* DsLpmHashKey    {edram6, edram3} */
        field_id = DynamicDsLpmIndexCam4_DsLpm4MaxIndex0_f+level;
        cmd = DRV_IOR(DynamicDsLpmIndexCam4_t, field_id);
        (DRV_IOCTL(lchip, 0, cmd, &field_val));
        *max_idx = (field_val << 10) |0x3ff;
    }

    return 0;
}

STATIC uint32
_ctc_greatbelt_dkits_dump_hash_get_level_en(ctc_dkits_hash_module_type_t type, uint32 level)
{
    uint8  lchip = 0;
    uint32 cmd = 0;
    uint32 en = 0;
    uint32 cfg_edram_bitmap = 0;
    dynamic_ds_edram_select_t ds_edram_sel_bitmap;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    if (CTC_DKITS_HASH_MODULE_SCL == type)
    {
        /* DsUserId {edram7, edram0} */
        cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
        (DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));
        if (level == 0)
        {
            en = 1;
        }
        else
        {
            en = user_id_hash_lookup_ctl.level1_hash_en?1:0;
        }
    }
    else if (CTC_DKITS_HASH_MODULE_FIB == type)
    {
        /* HashKey {edram0, edram1, edram2, edram3, edram6 } */
        cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DynamicDsFdbHashCtl_MacLevelBitmap_f);
        (DRV_IOCTL(lchip, 0, cmd, &cfg_edram_bitmap));
        en = DKITS_IS_BIT_SET(cfg_edram_bitmap, level);
    }
    else if (CTC_DKITS_HASH_MODULE_IP == type)
    {
        /* DsLpmHashKey    {edram6, edram3} */
        cmd = DRV_IOR(DynamicDsEdramSelect_t, DRV_ENTRY_FLAG);
        (DRV_IOCTL(lchip, 0, cmd, &ds_edram_sel_bitmap));
        en = DKITS_IS_BIT_SET(ds_edram_sel_bitmap.cfg_lpm_hash_edram_sel, level);
    }

    return en;
}

STATIC int32
_ctc_greatbelt_dkits_dump_hash_show_mem_usage(void)
{
    int32  ret = CLI_SUCCESS;
    char   desc[50];
    uint32 i = 0, j = 0, t = 0, valid = 0, step = 0;
    uint32 dynamic_mem_bitmap = 0, total = 0, used = 0, cmd = 0, start = 0, end = 0;
    uint32 sram_alloc[MAX_DRV_BLOCK_NUM][CTC_DKITS_HASH_MODULE_NUM] = {{0, 0}};
    uint32 sram_used[MAX_DRV_BLOCK_NUM][CTC_DKITS_HASH_MODULE_NUM] = {{0, 0}};
    ds_t   ds;
    char*  module[] = {"Scl", "Fib",  "IP-Prefix"};
    char mod_str[50];
    uint8 find_flag = 0;
    uint8 first_flag = 0;
    char* p_str = NULL;

    CTC_DKIT_PRINT("\n");
    CTC_DKIT_PRINT(" ------------------------------------------------------------------\n");
    CTC_DKIT_PRINT(" %-9s%-32s%-9s%-9s%s\n", "SramId", "HashModule", "Total", "Used", "Percent");
    CTC_DKIT_PRINT(" ------------------------------------------------------------------\n");

    for (i = 0; i < CTC_DKITS_HASH_MODULE_NUM; i++)
    {
        dynamic_mem_bitmap = (TABLE_INFO_PTR(gb_hash_key_field[i].tblid)
                             && TABLE_EXT_INFO_PTR(gb_hash_key_field[i].tblid)
                             && DYNAMIC_EXT_INFO_PTR(gb_hash_key_field[i].tblid))
                             ? DYNAMIC_BITMAP(gb_hash_key_field[i].tblid) : 0;

        for (j = 0; j < MAX_DRV_BLOCK_NUM; j++)
        {
            if (IS_BIT_SET(dynamic_mem_bitmap, j))
            {
                start = DYNAMIC_START_INDEX(gb_hash_key_field[i].tblid, j);
                end = DYNAMIC_END_INDEX(gb_hash_key_field[i].tblid, j);

                cmd = DRV_IOR(gb_hash_key_field[i].tblid, DRV_ENTRY_FLAG);
                step = (TABLE_ENTRY_SIZE(gb_hash_key_field[i].tblid) / DRV_BYTES_PER_ENTRY);

                for (t = start; t <= end; t += step)
                {
                     sal_memset(&ds, 0, sizeof(ds_t));
                    ret = DRV_IOCTL(0, t, cmd, &ds);
                    if (ret < 0)
                    {
                        CTC_DKIT_PRINT("Error read tbl %s[%u]\n", TABLE_NAME(gb_hash_key_field[i].tblid), t);
                        return CLI_ERROR;
                    }
                    drv_greatbelt_get_field(gb_hash_key_field[i].tblid, gb_hash_key_field[i].valid, (void*)&ds, &valid);
                    if (valid)
                    {
                        sram_used[j][i] += TABLE_ENTRY_SIZE(gb_hash_key_field[i].tblid)/DRV_BYTES_PER_ENTRY;
                    }
                }
                sram_alloc[j][i] = DYNAMIC_ENTRY_NUM(gb_hash_key_field[i].tblid, j);
            }
            else
            {
                sram_alloc[j][i] = 0xFFFFFFFF;
            }
        }
    }

    for (i = 0; i < MAX_DRV_BLOCK_NUM; i++)
    {
        total = 0;
        used = 0;
        find_flag = 0;
        first_flag =0;
        for (j = 0; j < CTC_DKITS_HASH_MODULE_NUM; j++)
        {
            if (0xFFFFFFFF != sram_alloc[i][j])
            {
                total += sram_alloc[i][j];
            }
            used += sram_used[i][j];
        }
        for (j = 0; j < CTC_DKITS_HASH_MODULE_NUM; j++)
        {
            if (0xFFFFFFFF != sram_alloc[i][j])
            {
                find_flag = 1;
                if (first_flag == 0)
                {
                    sal_memcpy(mod_str, module[j], sal_strlen(module[j])+1);
                    first_flag = 1;
                    p_str = module[j];
                }
                else
                {
                    p_str = sal_strcat((char*)mod_str, "/");
                    sal_memcpy(mod_str, p_str, sal_strlen(p_str)+1);
                    p_str = sal_strcat((char*)mod_str, module[j]);
                }
            }
        }

        if (1== find_flag)
        {
            if (0 == total)
            {
                sal_sprintf(desc, "%u", 0U);
            }
            else if (((used * 100) / total) < 1)
            {
                sal_sprintf(desc, "%s", "< 1");
            }
            else
            {
                sal_sprintf(desc, "%u", (used * 100) / total);
            }

            CTC_DKIT_PRINT(" %-9u%-32s%-9u%-9u%s\n", i, p_str, total, used, desc);
        }
    }

    CTC_DKIT_PRINT("\n");
    CTC_DKIT_PRINT(" ---------------------------------------------------------\n");
    CTC_DKIT_PRINT(" %-32s%-9s%-9s%s\n", "HashModule", "Total", "Used", "Percent");
    CTC_DKIT_PRINT(" ---------------------------------------------------------\n");

    for (i = 0; i < CTC_DKITS_HASH_MODULE_NUM; i++)
    {
        total = 0;
        used = 0;
        for (j = 0; j < MAX_DRV_BLOCK_NUM; j++)
        {
            if (0xFFFFFFFF != sram_alloc[j][i])
            {
                total += sram_alloc[j][i];
            }
            used += sram_used[j][i];
        }

        if (0 == total)
        {
            sal_sprintf(desc, "%u", 0U);
        }
        else if (((used * 100) / total) < 1)
        {
            sal_sprintf(desc, "%s", "< 1");
        }
        else
        {
            sal_sprintf(desc, "%u", (used * 100) / total);
        }
        CTC_DKIT_PRINT(" %-32s%-9u%-9u%s\n", module[i], total, used, desc);
    }
    CTC_DKIT_PRINT("\n");
    return CLI_SUCCESS;
}

STATIC void
_ctc_greatbelt_dkits_dump_hash_show_fun_usage(ctc_dkits_dump_hash_usage_t* p_fun_usage)
{
    uint32 i = 0, j = 0, v = 0, total = 0, level_total[5] = {0}, cam_total[5] = {0}, level_num = 0, level_en = 0;
    uint32 hash_class[] = {CTC_DKITS_SCL_HASH_FUN_TYPE_NUM, CTC_DKITS_FIB_HASH_FUN_TYPE_NUM, CTC_DKITS_LPM_HASH_FUN_TYPE_NUM};
    char* class_desc[CTC_DKITS_HASH_MODULE_NUM][10] =
    {
        {"Bridge",    "Ipv4",     "Ipv6",  "Ip-Tunnel", "PBB-Tunnel", "Trill-Tunnel", "OAM"},  /*scl*/
        {"FDB",       "Flow",      "Fcoe",    "Trill"},  /*fib*/
        {"IPv4Ucast",      "IPv6Ucast",    "IPv4Mcast", "IPv6Mcast"}  /*lpm*/
    };
    char    desc[50] = {0};
    char*   module_desc[CTC_DKITS_HASH_MODULE_NUM] = {"Scl", "Fib",  "IP-Prefix"};

    CTC_DKIT_PRINT("\n");
    /* CTC_DKIT_PRINT(" '-' means memory not allocated for the hash level.\n"); */
    for (i = 0; i < CTC_DKITS_HASH_MODULE_NUM; i++)
    {
        CTC_DKIT_PRINT(" -----------------------------------------------------------------------------------------\n");
        CTC_DKIT_PRINT(" %-32s%-9s", module_desc[i], "Total");
        level_num = _ctc_greatbelt_dkits_dump_hash_level_num(i);
        for (v = 0; v < level_num; v++)
        {
            sal_sprintf(desc, "Level%u", v);
            CTC_DKIT_PRINT("%-9s", desc);
        }

        /*Only Fib using cam*/
        if (CTC_DKITS_HASH_MODULE_FIB == i)
        {
            CTC_DKIT_PRINT("%-9s", "Cam");
        }
        CTC_DKIT_PRINT("\n");
        CTC_DKIT_PRINT(" -----------------------------------------------------------------------------------------\n");

        total = 0;
        sal_memset(level_total, 0, sizeof(level_total));

        cam_total[i] = 0;
        for (j = 0; j < hash_class[i]; j++)
        {
            cam_total[i] += p_fun_usage->cam[i][j];

            for (v = 0; v < level_num; v++)
            {
                total += p_fun_usage->key[i][j][v];
                level_total[v] += p_fun_usage->key[i][j][v];
            }
        }

        CTC_DKIT_PRINT(" %-32s%-9u", "All", total);
        for (v = 0; v < level_num; v++)
        {
            level_en = _ctc_greatbelt_dkits_dump_hash_get_level_en(i, v);
            if (level_en)
            {
                CTC_DKIT_PRINT("%-9u", level_total[v]);
            }
            else
            {
                CTC_DKIT_PRINT("%-9s", "-");
            }
        }

        if (CTC_DKITS_HASH_MODULE_FIB == i)
        {
            CTC_DKIT_PRINT("%-9u", cam_total[i]);
        }
        CTC_DKIT_PRINT("\n");

        for (j = 0; j < hash_class[i]; j++)
        {
            total = 0;
            for (v = 0; v < level_num; v++)
            {
                total += p_fun_usage->key[i][j][v];
            }

            CTC_DKIT_PRINT(" %-32s%-9u", class_desc[i][j], total);
            for (v = 0; v < level_num; v++)
            {
                level_en = _ctc_greatbelt_dkits_dump_hash_get_level_en(i, v);
                if (level_en)
                {
                    CTC_DKIT_PRINT("%-9u", p_fun_usage->key[i][j][v]);
                }
                else
                {
                    CTC_DKIT_PRINT("%-9s", "-");
                }
            }

            if (CTC_DKITS_HASH_MODULE_FIB == i)
            {
                CTC_DKIT_PRINT("%-9u", p_fun_usage->cam[i][j]);
            }

            CTC_DKIT_PRINT("\n");
        }
    }
    CTC_DKIT_PRINT("\n");
    return;
}

STATIC void
_ctc_greatbelt_dkits_dump_hash_show_fun_usage_detail(
        ctc_dkits_dump_hash_usage_t* p_hash_usage, ctc_dkits_dump_tbl_usage_t* p_tbl_usage)
{
    uint32  i = 0, j = 0, k = 0, s = 0;
    uint32  total = 0, level_total[5] = {0}, cam_total[5] = {0}, level_num = 0, level_en = 0;
    char    desc[50];
    char    format[50] = {0};
    char*   module_desc[CTC_DKITS_HASH_MODULE_NUM] = {"Scl", "Fib",  "IP-Prefix"};

    uint32  tbl_name_width[CTC_DKITS_HASH_MODULE_NUM] = {29, 20, 29};

    CTC_DKIT_PRINT("\n");
    /* CTC_DKIT_PRINT(" '-' means memory not allocated for the hash level.\n"); */
    for (i = 0; i < CTC_DKITS_HASH_MODULE_NUM; i++)
    {
        CTC_DKIT_PRINT(" -----------------------------------------------------------------------------------------\n");

        sal_sprintf(format, " %%-%us%%-9s", tbl_name_width[i] + 3);
        CTC_DKIT_PRINT(format, module_desc[i], "Total");

        level_num = _ctc_greatbelt_dkits_dump_hash_level_num(i);
        for (k = 0; k < level_num; k++)
        {
            sal_sprintf(desc, "Level%u", k);
            CTC_DKIT_PRINT("%-9s", desc);
        }

        if (CTC_DKITS_HASH_MODULE_FIB == i)
        {
            CTC_DKIT_PRINT("%-9s", "Cam");
        }
        CTC_DKIT_PRINT("\n");
        CTC_DKIT_PRINT(" -----------------------------------------------------------------------------------------\n");

        total = 0;
        sal_memset(level_total, 0, sizeof(level_total));

        cam_total[i] = 0;
        for (j = 0; NULL != gb_hash_tbl[i][j]; j++)
        {
            cam_total[i] += p_hash_usage->cam[i][j];

            for (k = 0; k < level_num; k++)
            {
                total += p_hash_usage->key[i][j][k];
                level_total[k] += p_hash_usage->key[i][j][k];
            }
        }

        sal_sprintf(format, " %%-%us%%-9u", tbl_name_width[i] + 3);
        CTC_DKIT_PRINT(format, "All", total);
        for (k = 0; k < level_num; k++)
        {
            level_en = _ctc_greatbelt_dkits_dump_hash_get_level_en(i, k);
            if (level_en)
            {
                CTC_DKIT_PRINT("%-9u", level_total[k]);
            }
            else
            {
                CTC_DKIT_PRINT("%-9s", "-");
            }
        }

        if (CTC_DKITS_HASH_MODULE_FIB == i)
        {
            CTC_DKIT_PRINT("%-9u", cam_total[i]);
        }
        CTC_DKIT_PRINT("\n");

        for (j = 0; NULL != gb_hash_tbl[i][j]; j++)
        {

            total = 0;

            for (k = 0; k < level_num; k++)
            {
                total += p_hash_usage->key[i][j][k];
            }

            for (s = 0; MaxTblId_t != gb_hash_tbl[i][j][s]; s++)
            {
                sal_strcpy(desc, TABLE_NAME(gb_hash_tbl[i][j][s]));
                _ctc_greatbelt_dkits_dump_cfg_brief_str(desc, sal_strlen(desc),\
                                             ((sal_strlen(desc) > tbl_name_width[i]) \
                                             ? (sal_strlen(desc) - tbl_name_width[i]) : 0));


                total = 0;

                for (k = 0; k < level_num; k++)
                {
                    level_en = _ctc_greatbelt_dkits_dump_hash_get_level_en(i, k);
                    level_total[k] = 0;
                    if (level_en)
                    {
                        level_total[k] += p_tbl_usage->key[i][j][s][k];
                        total += p_tbl_usage->key[i][j][s][k];
                    }
                }

                CTC_DKIT_PRINT(format, desc, total);

                for (k = 0; k < level_num; k++)
                {
                    level_en = _ctc_greatbelt_dkits_dump_hash_get_level_en(i, k);
                    if (level_en)
                    {

                        CTC_DKIT_PRINT("%-9u", level_total[k]);
                    }
                    else
                    {
                        CTC_DKIT_PRINT("%-9s", "-");
                    }
                }

                if (CTC_DKITS_HASH_MODULE_FIB == i)
                {
                    total = 0;
                    total += p_tbl_usage->cam[i][j][s];

                    CTC_DKIT_PRINT("%-9u", total);
                }

                CTC_DKIT_PRINT("\n");

            }

        }
    }

    CTC_DKIT_PRINT("\n");
    return;
}

STATIC void
_ctc_greatbelt_dkits_dump_hash_get_key_level(ctc_dkits_hash_module_type_t type, uint32 key_idx, uint32* p_level)
{
    uint32 max_idx = 0;
    uint8  i = 0, level_num = 0, en = 0;

    *p_level = 0;

    level_num = _ctc_greatbelt_dkits_dump_hash_level_num(type);
    for (i = 0; i < level_num; i++)
    {
        en = _ctc_greatbelt_dkits_dump_hash_get_level_en(type, i);
        if (!en)
        {
            continue;
        }

        _ctc_greatbelt_dkits_dump_hash_get_level_max(type, i, &max_idx);

        if (type == CTC_DKITS_HASH_MODULE_SCL)
        {
            if (i == 0)
            {
                if (key_idx <= max_idx)
                {
                    *p_level = 0;
                }
                else
                {
                    *p_level = 1;
                }

                return;
            }
        }
        else
        {
            if (key_idx <= max_idx)
            {
                *p_level = i;
                break;
            }
        }
    }
    return;
}

STATIC void
_ctc_greatbelt_dkits_dump_hash_get_cam_usage(ctc_dkits_hash_module_type_t module_type, ctc_dkits_dump_hash_usage_t* p_hash_usage)
{
    uint32    i = 0;
    ds_mac_hash_key_t cam_key;

    if (CTC_DKITS_HASH_MODULE_FIB != module_type)
    {
        return;
    }

    for (i = 0; i < 16; i++)
    {
        _ctc_greatbelt_dkits_dump_search_cam(0, i, &cam_key, 1);
        if (cam_key.valid)
        {
            p_hash_usage->cam[CTC_DKITS_HASH_MODULE_FIB][0]++;
        }
    }

    return;
}

STATIC int32
_ctc_greatbelt_dkits_dump_hash_get_key_usage(ctc_dkits_dump_hash_usage_t* p_hash_usage, ctc_dkits_dump_tbl_usage_t* p_tbl_usage)
{
    int32   ret = CLI_SUCCESS;
    uint32  i = 0, j = 0, k = 0, t = 0, valid = 0, level = 0;
    uint32  cmd = 0, step = 0;
    ctc_dkits_ds_t ds;
    tbls_id_t tblid = MaxTblId_t;
    ctc_dkits_hash_module_type_t module_type;
    ctc_dkits_hash_tbl_info_t hash_tbl_info;
    uint32 key_type = 0;
    ds_user_id_svlan_cos_hash_key_t* p_userid = NULL;
    ds_lpm_ipv4_hash8_key_t* p_lpm = NULL;
    uint32 hash_type = 0;
    ds_mac_hash_key_t* p_mac = NULL;
    uint32 is_mac = 0;
    for (i = 0; i < CTC_DKITS_HASH_MODULE_NUM; i++)
    {
        _ctc_greatbelt_dkits_dump_hash_get_cam_usage(i, p_hash_usage);

        for (j = 0; NULL != gb_hash_tbl[i][j];  j++)
        {
            for (k = 0; MaxTblId_t != gb_hash_tbl[i][j][k]; k++)
            {
                tblid = gb_hash_tbl[i][j][k];
                step = TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY;
                sal_memset(&hash_tbl_info, 0, sizeof(ctc_dkits_hash_tbl_info_t));
                _ctc_greatbelt_dkits_dump_get_hash_info(tblid, &hash_tbl_info);
                _ctc_greatbelt_dkits_dump_hash2type(tblid, &module_type, &key_type);
                cmd = DRV_IOR(tblid, DRV_ENTRY_FLAG);
                for (t = 0; t < TABLE_MAX_INDEX(tblid); t += step)
                {
                    sal_memset(&ds, 0, sizeof(ctc_dkits_ds_t));
                    ret = DRV_IOCTL(0, t, cmd, &ds);
                    if (ret < 0)
                    {
                        CTC_DKIT_PRINT("%s %d\n", __FUNCTION__, __LINE__);
                        continue;
                    }

                    drv_greatbelt_get_field(gb_hash_tbl[i][j][k], hash_tbl_info.valid_fld, ds.entry, &valid);
                    if (valid)
                    {
                        _ctc_greatbelt_dkits_dump_hash_get_key_level(i, t, &level);

                        /*get real hash type and compare to stored type*/
                        if (module_type == CTC_DKITS_HASH_MODULE_SCL)
                        {
                            p_userid = (ds_user_id_svlan_cos_hash_key_t*)ds.entry;
                            drv_greatbelt_get_field(DsUserIdSvlanHashKey_t, DsUserIdSvlanHashKey_HashType_f, (void*)p_userid, &hash_type);
                            if (hash_type == key_type+1)
                            {
                                p_hash_usage->key[i][j][level]++;
                                p_tbl_usage->key[i][j][k][level]++;
                            }
                        }
                        else if (module_type == CTC_DKITS_HASH_MODULE_IP)
                        {
                            p_lpm = (ds_lpm_ipv4_hash8_key_t*)ds.entry;
                            drv_greatbelt_get_field(DsLpmIpv4Hash8Key_t, DsLpmIpv4Hash8Key_HashType_f, (void*)p_lpm, &hash_type);
                            if (hash_type == key_type)
                            {
                                p_hash_usage->key[i][j][level]++;
                                p_tbl_usage->key[i][j][k][level]++;
                            }
                        }
                        else if (module_type == CTC_DKITS_HASH_MODULE_FIB)
                        {
                            p_mac = (ds_mac_hash_key_t*)ds.entry;
                            drv_greatbelt_get_field(DsMacHashKey_t, DsMacHashKey_IsMacHash_f, (void*)p_mac, &is_mac);
                            if (((CTC_DKITS_FIB_HASH_FUN_TYPE_BRIDGE == j) && (is_mac)) ||
                                ((CTC_DKITS_FIB_HASH_FUN_TYPE_FLOW == j) && (!is_mac)))
                            {
                                p_hash_usage->key[i][j][level]++;
                                p_tbl_usage->key[i][j][k][level]++;
                            }

                        }
                    }

                }
            }
        }

     sal_udelay(10);
     }
    return CLI_SUCCESS;
}


int32
ctc_greatbelt_dkits_dump_static(uint8 lchip, sal_file_t p_f, ctc_dkits_dump_func_flag_t flag)
{
    int32  ret = CLI_SUCCESS;
    uint32 count = 0;

    CTC_DKIT_LCHIP_CHECK(lchip);

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_HSS))
    {
        if (1 == SDK_WORK_PLATFORM)
        {
            return 0;
        }

        ret = _ctc_greatbelt_dkits_dump_travel_static_cfg(NULL, &count, NULL, gb_serdes_tbl, &_ctc_greatbelt_dkits_dump_count_tbl);
        ret = ret ? ret : _ctc_greatbelt_dkits_dump_travel_static_cfg(&lchip, p_f, &count, gb_serdes_tbl, &_ctc_greatbelt_dkits_dump_write_tbl);
    }
    else if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_SERDES))
    {
        if (1 == SDK_WORK_PLATFORM)
        {
            return 0;
        }

        ret = _ctc_greatbelt_dkits_dump_indirect(lchip, p_f, flag);
    }
    else if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_ALL))
    {
        ret = _ctc_greatbelt_dkits_dump_travel_static_all(NULL, &count, NULL, &_ctc_greatbelt_dkits_dump_count_tbl);
        ret = ret ? ret : _ctc_greatbelt_dkits_dump_travel_static_all(&lchip, p_f, &count, &_ctc_greatbelt_dkits_dump_write_tbl);
    }
    else if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_IPE))
    {
        ret = _ctc_greatbelt_dkits_dump_travel_static_cfg(NULL, &count, NULL, gb_static_tbl_ipe, &_ctc_greatbelt_dkits_dump_count_tbl);
        ret = ret ? ret : _ctc_greatbelt_dkits_dump_travel_static_cfg(&lchip, p_f, &count, gb_static_tbl_ipe, &_ctc_greatbelt_dkits_dump_write_tbl);
    }
    else if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_BSR))
    {
        ret = _ctc_greatbelt_dkits_dump_travel_static_cfg(NULL, &count, NULL, gb_static_tbl_bsr, &_ctc_greatbelt_dkits_dump_count_tbl);
        ret = ret ? ret : _ctc_greatbelt_dkits_dump_travel_static_cfg(&lchip, p_f, &count, gb_static_tbl_bsr, &_ctc_greatbelt_dkits_dump_write_tbl);
    }
    else if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_STATIC_EPE))
    {
        ret = _ctc_greatbelt_dkits_dump_travel_static_cfg(NULL, &count, NULL, gb_static_tbl_epe, &_ctc_greatbelt_dkits_dump_count_tbl);
        ret = ret ? ret : _ctc_greatbelt_dkits_dump_travel_static_cfg(&lchip, p_f, &count, gb_static_tbl_epe, &_ctc_greatbelt_dkits_dump_write_tbl);
    }

    return ret;
}

int32
ctc_greatbelt_dkits_dump_dynamic(uint8 lchip, sal_file_t p_f, ctc_dkits_dump_func_flag_t flag)
{
    int32 ret = 0;
    uint8 index = 0;

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_FDB))
    {
        /*only store FIB Host0*/
        ret = _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_FIB, CTC_DKITS_FIB_HASH_FUN_TYPE_BRIDGE, p_f);
    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_SCL))
    {
        /*1. Hash scl */
        for (index = 0; index <= CTC_DKITS_SCL_HASH_FUN_TYPE_TRILL_TUNNEL; index++)
        {
            ret = ret ? ret: _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_SCL, index, p_f);
        }

        /*2. Tcam scl*/
        for (index = 0; index < CTC_DKITS_SCL_TCAM_FUN_TYPE_NUM; index++)
        {
            ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_SCL, index, p_f);
        }
    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_ACL))
    {
        /*1. Hash flow*/
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_FIB, CTC_DKITS_FIB_HASH_FUN_TYPE_FLOW, p_f);

        /*2.Tcam acl*/
        for (index = 0; index < CTC_DKITS_ACL_TCAM_FUN_TYPE_NUM; index++)
        {
            ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_ACL, index, p_f);
        }
    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_OAM))
    {
        /*just hash*/
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_SCL, CTC_DKITS_SCL_HASH_FUN_TYPE_OAM, p_f);
    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_IPUC))
    {
        /*hash*/
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_IP, CTC_DKITS_LPM_HASH_FUN_TYPE_IPV4, p_f);
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_hash_tbl(lchip, CTC_DKITS_HASH_FUN_TYPE_IP, CTC_DKITS_LPM_HASH_FUN_TYPE_IPV6, p_f);

        /*tcam*/
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_IPUC, p_f);
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_NAT_IPV4, p_f);
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_NAT_IPV6, p_f);
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_PBR_IPV4, p_f);
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_PBR_IPV6, p_f);

    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_IPMC))
    {
        ret = ret ? ret: _ctc_greatbelt_dkits_dump_tcam_tbl(lchip, CTC_DKITS_TCAM_FUN_TYPE_IP, CTC_DKITS_IP_TCAM_FUN_TYPE_IPMC, p_f);
    }

    if (DKITS_IS_BIT_SET(flag, CTC_DKITS_DUMP_FUNC_DYN_IPFIX))
    {
        ret = -1;
    }

    return ret;

}

#define ________DKIT_DUMP_EXTERNAL_FUNCTION_________

int32
ctc_greatbelt_dkits_dump_memory_usage(uint8 lchip, void* p_para)
{
    ctc_dkits_dump_hash_usage_t* p_hash_usage = NULL;
    ctc_dkits_dump_tbl_usage_t* p_tbl_usage = NULL;

    ctc_dkits_dump_usage_type_t type = *((ctc_dkits_dump_usage_type_t*)p_para);

    if (CTC_DKITS_DUMP_USAGE_TCAM_KEY == type)
    {
        _ctc_greatbelt_dkits_dump_tcam_mem_usage();
    }
    else if (CTC_DKITS_DUMP_USAGE_HASH_BRIEF == type)
    {
        p_hash_usage = (ctc_dkits_dump_hash_usage_t*)sal_malloc(sizeof(ctc_dkits_dump_hash_usage_t));
        if (NULL == p_hash_usage)
        {
            CTC_DKIT_PRINT("%s %d, malloc hash_usage failed!\n", __FUNCTION__, __LINE__);
            goto DUMP_MEMORY_USAGE_ERROR;
        }
        p_tbl_usage = (ctc_dkits_dump_tbl_usage_t*)sal_malloc(sizeof(ctc_dkits_dump_tbl_usage_t));
        if (NULL == p_tbl_usage)
        {
            CTC_DKIT_PRINT("%s %d, malloc fun_usage failed!\n", __FUNCTION__, __LINE__);
            goto DUMP_MEMORY_USAGE_ERROR;
        }

        sal_memset(p_hash_usage, 0, sizeof(ctc_dkits_dump_hash_usage_t));
        sal_memset(p_tbl_usage, 0, sizeof(ctc_dkits_dump_tbl_usage_t));

        _ctc_greatbelt_dkits_dump_hash_get_key_usage(p_hash_usage, p_tbl_usage);
        _ctc_greatbelt_dkits_dump_hash_show_fun_usage(p_hash_usage);
    }
    else if (CTC_DKITS_DUMP_USAGE_HASH_DETAIL == type)
    {
        p_hash_usage = (ctc_dkits_dump_hash_usage_t*)sal_malloc(sizeof(ctc_dkits_dump_hash_usage_t));
        if (NULL == p_hash_usage)
        {
            CTC_DKIT_PRINT("%s %d, malloc hash_usage failed!\n", __FUNCTION__, __LINE__);
            goto DUMP_MEMORY_USAGE_ERROR;
        }

        p_tbl_usage = (ctc_dkits_dump_tbl_usage_t*)sal_malloc(sizeof(ctc_dkits_dump_tbl_usage_t));
        if (NULL == p_tbl_usage)
        {
            CTC_DKIT_PRINT("%s %d, malloc fun_usage failed!\n", __FUNCTION__, __LINE__);
            goto DUMP_MEMORY_USAGE_ERROR;
        }

        sal_memset(p_hash_usage, 0, sizeof(ctc_dkits_dump_hash_usage_t));
        sal_memset(p_tbl_usage, 0, sizeof(ctc_dkits_dump_tbl_usage_t));

        _ctc_greatbelt_dkits_dump_hash_get_key_usage(p_hash_usage, p_tbl_usage);
        _ctc_greatbelt_dkits_dump_hash_show_fun_usage_detail(p_hash_usage, p_tbl_usage);
    }
    else if (CTC_DKITS_DUMP_USAGE_HASH_MEM == type)
    {
        _ctc_greatbelt_dkits_dump_hash_show_mem_usage();
    }

DUMP_MEMORY_USAGE_ERROR:
    if (NULL != p_hash_usage)
    {
        sal_free(p_hash_usage);
    }

    if (NULL != p_tbl_usage)
    {
        sal_free(p_tbl_usage);
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_cfg_dump(uint8 lchip, void* p_info)
{
    int32 ret = CLI_SUCCESS;
    ctc_dkits_dump_cfg_t* p_dump_cfg = NULL;
    sal_file_t p_wf = NULL;
    char cfg_file[300] = {0};
    ctc_dkits_dump_centec_file_hdr_t centec_file_hdr;
    sal_time_t begine_time, end_time;
    host_type_t host_endian = HOST_LE;

    DKITS_PTR_VALID_CHECK(p_info);
    p_dump_cfg = (ctc_dkits_dump_cfg_t*)p_info;

    sal_sprintf(cfg_file, "%s%s", p_dump_cfg->file, DUMP_CENTEC_FILE_POSTFIX);

    p_wf = sal_fopen(cfg_file, "wb+");

    if (NULL == p_wf)
    {
        CTC_DKIT_PRINT(" Store file: %s failed!\n\n", cfg_file);
        goto CTC_DKITS_DUMP_CFG_END;
    }

    sal_memset(&centec_file_hdr, 0, sizeof(ctc_dkits_dump_centec_file_hdr_t));
    sal_strcpy((char*)(centec_file_hdr.centec_cfg_stamp), CENTEC_CFG_STAMP);
    sal_strcpy((char*)(centec_file_hdr.version), CTC_DKITS_VERSION_STR);
    sal_strcpy((char*)(centec_file_hdr.sdk_release_data), CTC_DKITS_RELEASE_DATE);
    sal_strcpy((char*)(centec_file_hdr.copy_right_time), CTC_DKITS_COPYRIGHT_TIME);
    sal_strcpy((char*)(centec_file_hdr.chip_name), CTC_DKITS_CURRENT_GB_CHIP);
    centec_file_hdr.func_flag = p_dump_cfg->func_flag;
    host_endian = drv_greatbelt_get_host_type(lchip);
    centec_file_hdr.endian_type = host_endian;
    sal_fwrite(&centec_file_hdr, sizeof(ctc_dkits_dump_centec_file_hdr_t), 1, p_wf);

    if (DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_IPE)
        || DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_BSR)
        || DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_EPE)
        || DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_ALL)
        || DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_HSS)
        || DKITS_IS_BIT_SET(p_dump_cfg->func_flag, CTC_DKITS_DUMP_FUNC_STATIC_SERDES))
    {
        sal_time(&begine_time);
        ret = ctc_greatbelt_dkits_dump_static(p_dump_cfg->lchip, p_wf, p_dump_cfg->func_flag);
        sal_time(&end_time);

        if (CLI_ERROR == ret)
        {
            goto CTC_DKITS_DUMP_CFG_END;
        }
    }
    else
    {
        ret = ctc_greatbelt_dkits_dump_dynamic(p_dump_cfg->lchip, p_wf, p_dump_cfg->func_flag);
    }

    if (CLI_ERROR == ret)
    {
        goto CTC_DKITS_DUMP_CFG_END;
    }

    CTC_DKIT_PRINT(" Store file: %s\n\n", cfg_file);
     /*CTC_DKIT_PRINT("\nTime elapse: %lf(s)\n", interval);*/

CTC_DKITS_DUMP_CFG_END:
    if (NULL != p_wf)
    {
        sal_fclose(p_wf);
    }
    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_cfg_decode(uint8 lchip, void* p_para)
{
    int32 ret = CLI_SUCCESS;
    char* p_file = NULL;
    char* p_str = NULL;
    sal_file_t p_rf = NULL;
    sal_file_t p_wf = NULL;
    char txt_file_path[256] = {0};
    ctc_dkits_dump_centec_file_hdr_t centec_file_hdr;
    sal_time_t begine_time, end_time;
    host_type_t cfg_endian = HOST_LE, host_endian = HOST_LE;
    uint8 data_translate = 0;
    sal_file_t p_file_temp = NULL;

    DKITS_PTR_VALID_CHECK(p_para);
    p_file = ((ctc_dkits_dump_cfg_t*)p_para)->file;
    data_translate = ((ctc_dkits_dump_cfg_t*)p_para)->data_translate;

    sal_strcpy(txt_file_path, p_file);
    p_str = sal_strstr(txt_file_path, DUMP_CENTEC_FILE_POSTFIX);
    if (NULL != p_str)
    {
        *p_str = '\0';
    }

    sal_strcat(txt_file_path, DUMP_DECODC_TXT_FILE_POSTFIX);

    p_rf = sal_fopen(p_file, "rb");
    if (NULL == p_rf)
    {
        CTC_DKIT_PRINT(" Access centec file: %s failed!\n\n", p_file);
        goto CTC_DKITS_DUMP_CFG_DECODE_END;
    }

    sal_memset(&centec_file_hdr, 0, sizeof(ctc_dkits_dump_centec_file_hdr_t));
    sal_fread(&centec_file_hdr, sizeof(ctc_dkits_dump_centec_file_hdr_t), 1, p_rf);

    cfg_endian = (0 == centec_file_hdr.endian_type) ? HOST_LE : HOST_BE;
    host_endian = drv_greatbelt_get_host_type(lchip);

    if (cfg_endian != host_endian)
    {
        ctc_dkits_dump_swap32((uint32*)&centec_file_hdr.func_flag, sizeof(centec_file_hdr.func_flag) / 4);
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr.centec_cfg_stamp), CENTEC_CFG_STAMP))
    {
        CTC_DKIT_PRINT(" File: %s is not centec file!\n\n", p_file);
        goto CTC_DKITS_DUMP_CFG_DECODE_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr.chip_name), CTC_DKITS_CURRENT_GB_CHIP))
    {
        CTC_DKIT_PRINT(" Centec file: %s chip series mismatch!\n\n", p_file);
        goto CTC_DKITS_DUMP_CFG_DECODE_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr.version), CTC_DKITS_VERSION_STR))
    {
        CTC_DKIT_PRINT(" Centec file: %s chip series mismatch!\n\n", p_file);
        goto CTC_DKITS_DUMP_CFG_DECODE_END;
    }

    p_wf = sal_fopen(txt_file_path, "wt");
    if (NULL == p_wf)
    {
        CTC_DKIT_PRINT(" Write %s file failed!\n\n", txt_file_path);
        goto CTC_DKITS_DUMP_CFG_DECODE_END;
    }

    ctc_greatbelt_dkit_misc_chip_info(lchip, p_wf);

    if (data_translate)
    {
        p_file_temp = dkits_log_file;
        dkits_log_file = p_wf;
    }

    sal_time(&begine_time);

    ret = ctc_greatbelt_dkits_dump_data2text(lchip, p_rf, cfg_endian, p_wf, data_translate);
    sal_time(&end_time);

    if (data_translate)
    {
        dkits_log_file = p_file_temp;
    }

    if (CLI_ERROR == ret)
    {
        return CLI_ERROR;
    }

    CTC_DKIT_PRINT(" Convert centec file: %s to text file: %s\n\n", p_file, txt_file_path);
     /*CTC_DKIT_PRINT("\nTime elapse: %lf(s)\n", interval);*/

CTC_DKITS_DUMP_CFG_DECODE_END:

    if (NULL != p_wf)
    {
        sal_fclose(p_wf);
    }

    if (NULL != p_rf)
    {
        sal_fclose(p_rf);
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_cfg_cmp(uint8 lchip, void* p_para1, void* p_para2)
{
    sal_file_t p_srf = NULL;
    sal_file_t p_drf = NULL;
    sal_file_t p_wf = NULL;
    char dump_diff_file[256] = "dump_cfg.diff";
    ctc_dkits_dump_centec_file_hdr_t centec_file_hdr1;
    ctc_dkits_dump_centec_file_hdr_t centec_file_hdr2;
    sal_time_t begine_time, end_time;
    host_type_t src_cfg_endian = HOST_LE, dst_cfg_endian = HOST_LE, host_endian = HOST_LE;

    DKITS_PTR_VALID_CHECK(p_para1);
    DKITS_PTR_VALID_CHECK(p_para2);

    p_srf = sal_fopen((char*)p_para1, "rb");
    if (NULL == p_srf)
    {
        CTC_DKIT_PRINT(" Read file1: %s failed!\n\n", (char*)p_para1);
        return CLI_ERROR;
    }

    p_drf = sal_fopen((char*)p_para2, "rb");
    if (NULL == p_drf)
    {
        CTC_DKIT_PRINT(" Read file2: %s failed!\n\n", (char*)p_para2);
        sal_fclose(p_srf);
        return CLI_ERROR;
    }

    sal_memset(&centec_file_hdr1, 0, sizeof(ctc_dkits_dump_centec_file_hdr_t));
    sal_memset(&centec_file_hdr2, 0, sizeof(ctc_dkits_dump_centec_file_hdr_t));

    sal_fread(&centec_file_hdr1, sizeof(ctc_dkits_dump_centec_file_hdr_t), 1, p_srf);
    sal_fread(&centec_file_hdr2, sizeof(ctc_dkits_dump_centec_file_hdr_t), 1, p_drf);

    host_endian = drv_greatbelt_get_host_type(lchip);
    src_cfg_endian = (0 == centec_file_hdr1.endian_type) ? HOST_LE : HOST_BE;
    dst_cfg_endian = (0 == centec_file_hdr2.endian_type) ? HOST_LE : HOST_BE;

    if (src_cfg_endian != host_endian)
    {
        ctc_dkits_dump_swap32((uint32*)&centec_file_hdr1.func_flag, sizeof(centec_file_hdr1.func_flag) / 4);
    }

    if (dst_cfg_endian != host_endian)
    {
        ctc_dkits_dump_swap32((uint32*)&centec_file_hdr2.func_flag, sizeof(centec_file_hdr2.func_flag) / 4);
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr1.centec_cfg_stamp), CENTEC_CFG_STAMP))
    {
        CTC_DKIT_PRINT(" File: %s is not centec file!\n\n", (char*)p_para1);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr2.centec_cfg_stamp), CENTEC_CFG_STAMP))
    {
        CTC_DKIT_PRINT(" File: %s is not centec file!\n\n", (char*)p_para2);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr1.chip_name), CTC_DKITS_CURRENT_GB_CHIP))
    {
        CTC_DKIT_PRINT(" File: %s is not centec file!\n\n", (char*)p_para1);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr2.chip_name), CTC_DKITS_CURRENT_GB_CHIP))
    {
        CTC_DKIT_PRINT(" File: %s is not centec file!\n\n", (char*)p_para2);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    if (0 != sal_strcmp((char*)(centec_file_hdr1.version), (char*)(centec_file_hdr2.version)))
    {
        CTC_DKIT_PRINT(" Centec file1 version: %s,  centec file2 version: %s, mismatch!\n\n",
                       centec_file_hdr1.version, centec_file_hdr2.version);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    if (centec_file_hdr1.func_flag != centec_file_hdr2.func_flag)
    {
        CTC_DKIT_PRINT(" Centec file1 and centec file2 dump option mismatch!\n\n");
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    p_wf = sal_fopen(dump_diff_file, "wt+");
    if (NULL == p_wf)
    {
        CTC_DKIT_PRINT(" Create diff file: %s\n\n", dump_diff_file);
        goto CTC_DKITS_DUMP_CFG_CMP_END;
    }

    sal_fprintf(p_wf, "File1:%s\n", (char*)p_para1);
    sal_fprintf(p_wf, "File2:%s\n", (char*)p_para2);

    if (DKITS_IS_BIT_SET(centec_file_hdr1.func_flag, CTC_DKITS_DUMP_FUNC_STATIC_ALL)
       || DKITS_IS_BIT_SET(centec_file_hdr1.func_flag, CTC_DKITS_DUMP_FUNC_STATIC_HSS))
    {
        ctc_dkits_dump_txt_header(CTC_DKITS_DUMP_TEXT_HEADER_DIFF_TBL, p_wf);
    }
    else if (DKITS_IS_BIT_SET(centec_file_hdr1.func_flag, CTC_DKITS_DUMP_FUNC_STATIC_SERDES))
    {
        ctc_dkits_dump_txt_header(CTC_DKITS_DUMP_TEXT_HEADER_DIFF_SERDES, p_wf);
    }

    ctc_greatbelt_dkits_dump_diff_status(1, NULL, NULL);
    sal_time(&begine_time);
    ctc_greatbelt_dkits_dump_cmp_tbl(lchip, p_srf, p_drf, src_cfg_endian, dst_cfg_endian, p_wf);
    sal_time(&end_time);

    CTC_DKIT_PRINT(" Diff file: %s\n\n", dump_diff_file);
     /*CTC_DKIT_PRINT("Time elapse: %lf(s)\n", interval);*/

CTC_DKITS_DUMP_CFG_CMP_END:

    if (NULL != p_wf)
    {
        sal_fclose(p_wf);
    }
    if (NULL != p_srf)
    {
        sal_fclose(p_srf);
    }
    if (NULL != p_drf)
    {
        sal_fclose(p_drf);
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkits_dump_data2tbl(uint8 lchip, ctc_dkits_dump_tbl_type_t tbl_type, uint8 cfg_endian, ctc_dkits_dump_tbl_block_hdr_t* p_tbl_block_hdr, uint8* p_buf, ctc_dkits_dump_tbl_entry_t* p_tbl_entry)
{
    uint8 i = 0, j  = 0, bytes = 0, count = 0;
    ctc_dkits_dump_tbl_block_data_t tbl_block_data;
    uint8 host_endian;
    tbls_id_t tblid = CTC_DKITS_DUMP_BLOCK_HDR_TBLID(p_tbl_block_hdr);

    host_endian = drv_greatbelt_get_host_type(lchip);

    for (i = 0; i < p_tbl_block_hdr->entry_num + 1; i++)
    {
        sal_memset(&tbl_block_data, 0, sizeof(ctc_dkits_dump_tbl_block_data_t));
        bytes = CTC_DKITS_DUMP_BLOCK_DATA_BYTE(p_tbl_block_hdr->format);

        if (CTC_DKITS_FORMAT_TYPE_4BYTE == p_tbl_block_hdr->format)
        {
            sal_memcpy(&tbl_block_data, p_buf + (i * bytes), bytes);
        }
        else
        {
            sal_memcpy(&(tbl_block_data.entry), p_buf + (i * bytes), bytes);
            tbl_block_data.repeat = 1;
        }

        if (cfg_endian != host_endian)
        {
            ctc_dkits_dump_swap32((uint32*)&tbl_block_data.entry, 1);
        }

        for (j = 0; j < tbl_block_data.repeat; j++)
        {
            if ((CTC_DKITS_DUMP_TBL_TYPE_TCAM_KEY == tbl_type)
                && ((count * sizeof(uint32)) >= TABLE_ENTRY_SIZE(tblid)))
            {
                p_tbl_entry->mask_entry[count - (TABLE_ENTRY_SIZE(tblid) / sizeof(uint32))] = tbl_block_data.entry;
            }
            else if ((CTC_DKITS_DUMP_TBL_TYPE_HASH_AD == tbl_type)
                    || (CTC_DKITS_DUMP_TBL_TYPE_TCAM_AD == tbl_type))
            {
                p_tbl_entry->ad_entry[count] = tbl_block_data.entry;
            }
            else
            {
                p_tbl_entry->data_entry[count] = tbl_block_data.entry;
            }
            count++;
        }
    }

    return CLI_SUCCESS;
}


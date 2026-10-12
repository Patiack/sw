/**
 @file sys_greatbelt_lpm.c

 @date 2009-12-09

 @version v2.0

 The file contains all ipuc related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_vector.h"
#include "ctc_dma.h"

#include "sys_greatbelt_sort.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_rpf_spool.h"

#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_l3_hash.h"
#include "sys_greatbelt_ipuc_db.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_lpm.h"
#include "sys_greatbelt_dma.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
struct sys_lpm_select_counter_s
{
    uint8 block_base;
    uint8 entry_number;
};
typedef struct sys_lpm_select_counter_s sys_lpm_select_counter_t;

struct sys_lpm_index_info_s
{
    uint32 end_index_array[MAX_DRV_BLOCK_NUM];
    uint32 hash_block_count;
};
typedef struct sys_lpm_index_info_s sys_lpm_index_info_t;

struct sys_lpm_master_s
{
    uint32 sram_count[LPM_TABLE_MAX];
    uint32 lpm_lookup_key_table_size[LPM_TABLE_MAX];
    ds_lpm_prefix_t ds_lpm_prefix[DS_LPM_PREFIX_MAX_INDEX];

    uint8 version_en[MAX_CTC_IP_VER];
    uint8 dma_enable;
    uint8 rsv0;

    sys_lpm_index_info_t sys_lpm_index_info[LPM_TABLE_MAX];
};
typedef struct sys_lpm_master_s sys_lpm_master_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_lpm_master_t* p_gb_lpm_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_ipuc_master_t* p_gb_ipuc_master[CTC_MAX_LOCAL_CHIP_NUM];

 #define SYS_LPM_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        if (0)  \
        {   \
            CTC_DEBUG_OUT(ip, ipuc, IPUC_SYS, level, FMT, ##__VA_ARGS__);  \
        }   \
    }

#define SYS_LPM_INIT_CHECK(lchip)\
        do{\
            SYS_LCHIP_CHECK_ACTIVE(lchip); \
            if (p_gb_lpm_master[lchip] == NULL)\
            {\
                return CTC_E_NOT_INIT;\
            }\
        }while(0)

/****************************************************************************
 *
* Function
*
*****************************************************************************/

STATIC int32
_sys_greatbelt_lpm_init_hash_mem_info(uint8 lchip)
{
    uint32 tbl_id[LPM_TABLE_MAX] = {DsLpmLookupKey0_t, DsLpmLookupKey1_t, DsLpmLookupKey2_t, DsLpmLookupKey3_t};
    uint32 blk_id;
    sys_lpm_index_info_t sys_lpm_index_info;
    uint8 i = 0;

    for (i = 0; i < LPM_TABLE_MAX; i++)
    {
        sal_memset(&sys_lpm_index_info, 0, sizeof(sys_lpm_index_info_t));
        for (blk_id = 0; blk_id < MAX_DRV_BLOCK_NUM; blk_id++)
        {
            if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id[i]), blk_id))
            {
                continue;
            }
            sys_lpm_index_info.end_index_array[sys_lpm_index_info.hash_block_count] = DYNAMIC_START_INDEX(tbl_id[i], blk_id) + DYNAMIC_ENTRY_NUM(tbl_id[i], blk_id) - 1;
            sys_lpm_index_info.hash_block_count++;
        }
        sal_memcpy(&(p_gb_lpm_master[lchip]->sys_lpm_index_info[i]), &sys_lpm_index_info, sizeof(sys_lpm_index_info_t));
    }

    return CTC_E_NONE;
}

STATIC bool
_sys_greatbelt_lpm_check_hash_mem_info(uint8 lchip, uint32 tbl_id, uint32 start_index, uint32 entry_num)
{
    sys_lpm_index_info_t* sys_lpm_index_info = NULL;
    uint32 blk_id;

    sys_lpm_index_info = &(p_gb_lpm_master[lchip]->sys_lpm_index_info[tbl_id - DsLpmLookupKey0_t]);
    for (blk_id = 0; blk_id < sys_lpm_index_info->hash_block_count; blk_id++)
    {
        if ((start_index <= sys_lpm_index_info->end_index_array[blk_id])
            &&((start_index+entry_num) > sys_lpm_index_info->end_index_array[blk_id]))
        {
            return FALSE;  /*cross two block, must write by io*/
        }
    }
    return TRUE;
}


int32
_sys_greatbelt_ipuc_idx(uint8 lchip, uint8 ip, uint8 mask, uint8 index_mask, uint8* offset, uint8* num)
{
    uint8 F = index_mask & mask;
    uint8 S = ip & F;
    uint8 B = F ^ index_mask;
    uint8 count = 0;
    uint8 i;

    uint8 BIT[8] = {0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1};
    uint8 result = 0;

    for (i = 0; i < 8; i++)
    {
        if (BIT[i] & F)
        {
            result <<= 1;
            result += (S & BIT[i]) ? 1 : 0;
        }
    }

    while (B)
    {
        count++;
        B &= (B - 1);
    }

    *num = (1 << count);
    *offset = (result << count);

    return CTC_E_NONE;
}

void
sys_greatbelt_lpm_state_show(uint8 lchip)
{
    if (p_gb_lpm_master[lchip] == NULL)
    {
        return;
    }
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM lookup key stage0      |   0x%04x  |   0x%04x\n", p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX0], p_gb_lpm_master[lchip]->sram_count[LPM_TABLE_INDEX0]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM lookup key stage1      |   0x%04x  |   0x%04x\n", p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX1], p_gb_lpm_master[lchip]->sram_count[LPM_TABLE_INDEX1]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM lookup key stage2      |   0x%04x  |   0x%04x\n", p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX2], p_gb_lpm_master[lchip]->sram_count[LPM_TABLE_INDEX2]);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "LPM lookup key stage3      |   0x%04x  |   0x%04x\n", p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX3], p_gb_lpm_master[lchip]->sram_count[LPM_TABLE_INDEX3]);
}

#define __0_HARD_WRITE__
int32
sys_greatbelt_lpm_enable_dma(uint8 lchip, uint8 enable)
{
    SYS_LPM_INIT_CHECK(lchip);
    p_gb_lpm_master[lchip]->dma_enable = enable;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_asic_write_lpm(uint8 lchip, ds_lpm_lookup_key_t* key, uint16 start_offset, uint16 entry_num, uint32 cmd)
{
    uint16 i = 0;
    drv_work_platform_type_t platform_type = 0;
    ctc_dma_tbl_rw_t tbl_cfg;
    tbls_id_t tbl_id;
    uint8 use_dma = 0;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tbl_cfg, 0, sizeof(tbl_cfg));

    drv_greatbelt_get_platform_type(&platform_type);

    tbl_id = DRV_IOC_MEMID(cmd);


    if ((platform_type == HW_PLATFORM) && (p_gb_lpm_master[lchip]->dma_enable) && (entry_num >= 12))
    {
        if (_sys_greatbelt_lpm_check_hash_mem_info(lchip, tbl_id, start_offset, entry_num))
        {
            use_dma = 1;
        }
        else
        {
            use_dma = 0;
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                            "Change to io!, tbl_id = %d, start_offset = %d, entry_num = %d\n",
                                                                                                          tbl_id, start_offset, entry_num);
        }
    }

    if (use_dma)
    {
        CTC_ERROR_RETURN(drv_greatbelt_table_get_hw_addr(tbl_id, start_offset, &tbl_cfg.tbl_addr));
        tbl_cfg.buffer = (uint32*)key;
        tbl_cfg.entry_num = entry_num;
        tbl_cfg.entry_len = sizeof(ds_lpm_lookup_key_t);
        tbl_cfg.rflag = 0;
        sys_greatbelt_dma_rw_table(lchip, &tbl_cfg);
    }
    else
    {
        for (i = 0; i < entry_num; i++)
        {
            DRV_IOCTL(lchip, start_offset + i, cmd, key);
            key ++;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage)
{
    ds_lpm_lookup_key_t* ds_lpm_lookup_key = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    sys_lpm_tbl_t* p_ntbl = NULL;
    sys_lpm_item_t* p_item = NULL;
    uint8 offset, item_size, ip, lpm_stage;
    uint16 i, j, k, idx, entry_size;
    uint32 cmd = 0;
    uint8 sram_index = 0;

    SYS_IPUC_DBG_FUNC();
    if (((p_ipuc_info->masklen <= 32) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen <= 16) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (!p_gb_ipuc_master[lchip]->use_hash8))
        || ((p_ipuc_info->masklen <= 8) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (p_gb_ipuc_master[lchip]->use_hash8))
        || (p_ipuc_info->l4_dst_port > 0))
    {
        return CTC_E_NONE;
    }

    ds_lpm_lookup_key = mem_malloc(MEM_IPUC_MODULE, sizeof(ds_lpm_lookup_key_t)*256);
    if (NULL == ds_lpm_lookup_key)
    {
        return CTC_E_NO_MEMORY;
    }

    for (lpm_stage = LPM_TABLE_INDEX0; lpm_stage < LPM_TABLE_MAX; lpm_stage++)
    {
        if ((stage != lpm_stage) && (LPM_TABLE_MAX != stage))
        {
            continue;
        }

        /*write new lpm key*/
        TABEL_GET_STAGE(lpm_stage, p_ipuc_info, p_table);
        if (p_table == NULL || (0 == p_table->count))
        {
            continue;
        }

        LPM_INDEX_TO_SIZE(p_table->idx_mask, entry_size);
        sal_memset(ds_lpm_lookup_key, 0, sizeof(ds_lpm_lookup_key_t) * entry_size);

        for (i = 0; i < LPM_TBL_NUM; i++)
        {
            TABLE_GET_PTR(p_table, i, p_ntbl);
            if (NULL == p_ntbl)
            {
                continue;
            }

            /* save all pointer node to ds_lpm_lookup_key */
            if (p_ntbl != p_table)
            {
                _sys_greatbelt_ipuc_idx(lchip, i, 0xff, p_table->idx_mask, &offset, &item_size);

                /* the pointer value is the relative lpm lookup key address
                 * and p_table->n_table[i]->pointer is the absolute adress
                 * since lpm pipeline lookup mechanism,
                 * the address should use relative address
                 * the code added here suppose ipv4
                 */

                if (p_ntbl->sram_index <= p_table->sram_index)
                {
                    mem_free(ds_lpm_lookup_key);
                    return CTC_E_NONE;
                }

                sram_index = p_ntbl->sram_index - p_table->sram_index - 1;

                if (LPM_LOOKUP_KEY_TYPE_EMPTY == ds_lpm_lookup_key[offset].type0)
                {
                    ds_lpm_lookup_key[offset].mask0 = p_ntbl->idx_mask;
                    ds_lpm_lookup_key[offset].ip0 = i;
                    ds_lpm_lookup_key[offset].nexthop0 = p_ntbl->offset;
                    ds_lpm_lookup_key[offset].pointer0_17_16 = sram_index;
                    ds_lpm_lookup_key[offset].type0 = LPM_LOOKUP_KEY_TYPE_POINTER;
                }
                else
                {
                    ds_lpm_lookup_key[offset].mask1 = p_ntbl->idx_mask;
                    ds_lpm_lookup_key[offset].ip1 = i;
                    ds_lpm_lookup_key[offset].nexthop1 = p_ntbl->offset;
                    ds_lpm_lookup_key[offset].pointer1_17_16 = sram_index;
                    ds_lpm_lookup_key[offset].type1 = LPM_LOOKUP_KEY_TYPE_POINTER;
                }

                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                 "write DsLpmLookupKey%d[%d], ip = 0x%x, pointer = 0x%X   \r\n",
                                 p_table->sram_index, p_table->offset+offset, i, (sram_index << 16) | p_ntbl->offset);
            }
            else    /* ipv6 pointer */
            {
                _sys_greatbelt_ipuc_idx(lchip, i, 0xff, p_table->idx_mask, &offset, &item_size);
                ITEM_GET_PTR(p_table->p_item, i, p_item);

                if (LPM_LOOKUP_KEY_TYPE_EMPTY == ds_lpm_lookup_key[offset].type0)
                {
                    ds_lpm_lookup_key[offset].mask0 = 0; /* don't care */
                    ds_lpm_lookup_key[offset].ip0 = i;
                    ds_lpm_lookup_key[offset].nexthop0 = p_item->pointer & 0xffff;
                    ds_lpm_lookup_key[offset].pointer0_17_16 = p_item->pointer17_16 & 0x3;
                    ds_lpm_lookup_key[offset].type0 = LPM_LOOKUP_KEY_TYPE_POINTER;
                }
                else
                {
                    ds_lpm_lookup_key[offset].mask1 = 0; /* don't care */
                    ds_lpm_lookup_key[offset].ip1 = i;
                    ds_lpm_lookup_key[offset].nexthop1 = p_item->pointer & 0xffff;
                    ds_lpm_lookup_key[offset].pointer1_17_16 = p_item->pointer17_16 & 0x3;
                    ds_lpm_lookup_key[offset].type1 = LPM_LOOKUP_KEY_TYPE_POINTER;
                }

                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                 "write DsLpmLookupKey%d[%d], ip = 0x%x, pointer = 0x%x  \r\n",
                                 p_table->sram_index, p_table->offset+offset, i, p_item->pointer);
            }
        }

        /* save all nexthop node to ds_lpm_lookup_key */
        for (i = LPM_MASK_LEN; i > 0; i--)
        {
            for (j = 0; j <= GET_MASK_MAX(i); j++)
            {
                idx = GET_BASE(i) + j;
                ip = (j << (LPM_MASK_LEN - i));
                ITEM_GET_PTR(p_table->p_item, idx, p_item);

                if ((NULL == p_item) || (p_item->t_masklen <= 0)
                    || !CTC_FLAG_ISSET(p_item->item_flag, LPM_ITEM_FLAG_VALID))
                {
                    continue;
                }

                _sys_greatbelt_ipuc_idx(lchip, ip, GET_MASK(i), p_table->idx_mask, &offset, &item_size);

                for (k = 0; k < item_size; k++)
                {
                    if (LPM_LOOKUP_KEY_TYPE_EMPTY == ds_lpm_lookup_key[offset + k].type0)
                    {
                        ds_lpm_lookup_key[offset + k].ip0 = ip;
                        ds_lpm_lookup_key[offset + k].mask0 = GET_MASK(i);

                        ds_lpm_lookup_key[offset + k].nexthop0 = p_item->ad_index & 0xFFFF;
                        ds_lpm_lookup_key[offset + k].pointer0_17_16 = 0;
                        ds_lpm_lookup_key[offset + k].type0 = LPM_LOOKUP_KEY_TYPE_NEXTHOP;
                    }
                    else
                    {
                        ds_lpm_lookup_key[offset + k].ip1 = ip;
                        ds_lpm_lookup_key[offset + k].mask1 = GET_MASK(i);

                        ds_lpm_lookup_key[offset + k].nexthop1 = p_item->ad_index & 0xFFFF;
                        ds_lpm_lookup_key[offset + k].pointer1_17_16 = 0;
                        ds_lpm_lookup_key[offset + k].type1 = LPM_LOOKUP_KEY_TYPE_NEXTHOP;
                    }

                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                     "write DsLpmLookupKey%d[%d], ip = 0x%x, nexthop = 0x%x  \r\n",
                                     p_table->sram_index, p_table->offset+offset, ip, p_item->ad_index);
                }
            }
        }

        cmd = DRV_IOW((DsLpmLookupKey0_t+p_table->sram_index), DRV_ENTRY_FLAG);

        /* notice */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " start offset:%d    write entry_size:%d\n", p_table->offset, entry_size);

        _sys_greatbelt_lpm_asic_write_lpm(lchip, ds_lpm_lookup_key, p_table->offset, entry_size, cmd);

    }

    mem_free(ds_lpm_lookup_key);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_update(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage, uint32 new_pointer, uint32 old_pointer)
{
    uint16 size;
    uint8 sram_index;
    uint16 offset;
    sys_lpm_tbl_t* p_table;
    sys_lpm_tbl_t* p_table_pre;
    sys_lpm_result_t* p_lpm_result;

    SYS_IPUC_DBG_FUNC();
    p_lpm_result = p_ipuc_info->lpm_result;
    TABEL_GET_STAGE(stage, p_ipuc_info, p_table);
    if (!p_table)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when update lpm\r\n");
        return CTC_E_UNEXPECT;
    }

    sram_index = (old_pointer >> POINTER_OFFSET_BITS_LEN) & 0x3;
    offset = old_pointer & 0xFFFF;
    LPM_INDEX_TO_SIZE(p_table->idx_mask, size);

    if (LPM_TABLE_INDEX0 < stage)
    {
        TABEL_GET_STAGE((stage - 1), p_ipuc_info, p_table_pre);
    }
    else
    {
        p_table_pre = NULL;
    }

    LPM_RLT_SET_OFFSET(p_ipuc_info, p_lpm_result, stage, p_table->offset);
    LPM_RLT_SET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage, p_table->sram_index);
    LPM_RLT_SET_IDX_MASK(p_ipuc_info, p_lpm_result, stage, p_table->idx_mask);

    p_table->offset = new_pointer & 0xFFFF;
    p_table->sram_index = new_pointer >> POINTER_OFFSET_BITS_LEN;

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_add_key(lchip, p_ipuc_info, stage));

    if (NULL != p_table_pre)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_lpm_add_key(lchip, p_ipuc_info, stage - 1));
    }
    else
    {
        if ((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && (p_gb_ipuc_master[lchip]->use_hash8))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_8));
        }
        else if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_16));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_HIGH32));
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_offset_free(lchip, sram_index, size, offset));

    return CTC_E_NONE;
}

#define __1_HARD_WRITE__
STATIC int32
_sys_greatbelt_lpm_add_tree(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 stage_ip, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 offset = 0;
    uint8 depth = 0;
    uint8 pa_loop = 0;
    uint8 ch_loop = 0;
    uint8 valid_parent_masklen = 0;
    uint16 real_idx = 0;
    uint16 child_idx = 0;
    uint16 parent_idx = 0;
    uint16 valid_parent_idx = 0;
    uint8 type = LPM_ITEM_TYPE_NONE;
    sys_lpm_item_t* p_real_item = NULL;
    sys_lpm_item_t* p_child_item = NULL;
    sys_lpm_item_t* p_parent_item = NULL;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);

    if (p_ipuc_info->masklen == 128)
    {
        ITEM_GET_OR_INIT_PTR((p_table->p_item), stage_ip, p_real_item, LPM_ITEM_TYPE_V6);

        p_real_item->pointer = p_ipuc_info->pointer & POINTER_OFFSET_MASK;
        p_real_item->pointer17_16 = (p_ipuc_info->pointer >> POINTER_OFFSET_BITS_LEN) & 0x3;
        p_real_item->nhp_or_cnt++;
        TABLE_SET_PTR(p_table->p_ntable, stage_ip, p_table);

        return CTC_E_NONE;
    }

    if (!masklen)
    {
        return CTC_E_NONE;
    }

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        type = LPM_ITEM_TYPE_V6;
    }
    else if (p_ipuc_info->masklen <= 16)
    {
        type = LPM_ITEM_TYPE_V4;
    }

    real_idx = SYS_LPM_GET_ITEM_IDX(stage_ip, masklen);   /* the idx is the location of the route */
    ITEM_GET_OR_INIT_PTR((p_table->p_item), real_idx, p_real_item, type);
    valid_parent_idx = real_idx;
    valid_parent_masklen = masklen;

    /* set this node */
    p_real_item->ad_index = p_ipuc_info->ad_offset;
    p_real_item->t_masklen = masklen;
    CTC_SET_FLAG(p_real_item->item_flag, LPM_ITEM_FLAG_REAL);
    CTC_SET_FLAG(p_real_item->item_flag, LPM_ITEM_FLAG_VALID);
    SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "node[%d] is real node, mask: %d, ad_index: %d!\n", real_idx, p_real_item->t_masklen, p_real_item->ad_index);

    if (masklen > 1)
    {
        /* find last parent valid node */
        for (depth = masklen; depth > 1; depth--)
        {
            valid_parent_idx = SYS_LPM_GET_PARENT_ITEM_IDX(stage_ip, depth);
            ITEM_GET_OR_INIT_PTR((p_table->p_item), valid_parent_idx, p_parent_item, type);

            CTC_SET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_PARENT);
            SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "node[%d] set parent flag!\n", valid_parent_idx);

            if (CTC_FLAG_ISSET(p_parent_item->item_flag, LPM_ITEM_FLAG_VALID))
            {
                break;
            }
        }

        /* found valid parent node */
        if ((depth >= 2) && CTC_FLAG_ISSET(p_parent_item->item_flag, LPM_ITEM_FLAG_VALID))
        {
            valid_parent_masklen = depth -1;
        }
        else
        {
            valid_parent_idx = real_idx;
            valid_parent_masklen = masklen;
        }
    }

    /* push sub route of last valid parent node */
    for (depth = valid_parent_masklen; depth < 8; depth++)
    {
        offset = (valid_parent_idx - GET_BASE(valid_parent_masklen)) * (1 << (depth - valid_parent_masklen));
        for (pa_loop = 0; pa_loop <= GET_MASK_MAX(depth - valid_parent_masklen); pa_loop++)
        {
            parent_idx = GET_BASE(depth) + offset + pa_loop;
            ITEM_GET_PTR(p_table->p_item, parent_idx, p_parent_item);

            if (p_parent_item && CTC_FLAG_ISSET(p_parent_item->item_flag, LPM_ITEM_FLAG_PARENT))
            {
                SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "push parent node[%d] flag: 0x%x!\n", parent_idx, p_parent_item->item_flag);

                for (ch_loop = 0; ch_loop < 2; ch_loop++)
                {
                    child_idx = GET_BASE(depth+1) + (offset + pa_loop) * 2 + ch_loop;
                    ITEM_GET_OR_INIT_PTR((p_table->p_item), child_idx, p_child_item, type);

                    CTC_SET_FLAG(p_child_item->item_flag, LPM_ITEM_FLAG_VALID);

                    if (p_child_item->t_masklen < p_parent_item->t_masklen)
                    {
                        p_child_item->t_masklen = p_parent_item->t_masklen;
                        p_child_item->ad_index = p_parent_item->ad_index;
                        SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "push down child node[%d] masklen: %d, ad index: %d.\n", child_idx, p_child_item->t_masklen, p_child_item->ad_index);
                    }
                }

                if (!CTC_FLAG_ISSET(p_parent_item->item_flag, LPM_ITEM_FLAG_REAL))
                {
                    p_parent_item->t_masklen = 0;
                    p_parent_item->ad_index = INVALID_NEXTHOP;
                }
                CTC_UNSET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_VALID);
                SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "clear parent node[%d] valid flag, masklen: %d, ad index: %d.\n", parent_idx, p_parent_item->t_masklen, p_parent_item->ad_index);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_del_tree(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 stage_ip, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 offset = 0;
    uint8 depth = 0;
    uint8 depth_valid_ct = 0;
    uint8 last_valid_ct = 0;
    uint16 ch_loop = 0;
    uint16 real_idx = 0;
    uint16 child0_idx = 0;
    uint16 child1_idx = 0;
    uint16 parent_idx = 0;
    uint32 real_ad_index = 0;
    uint8 type = LPM_ITEM_TYPE_NONE;
    sys_lpm_item_t* p_replace_item = NULL;
    sys_lpm_item_t* p_real_item = NULL;
    sys_lpm_item_t* p_child0_item = NULL;
    sys_lpm_item_t* p_child1_item = NULL;
    sys_lpm_item_t* p_parent_item = NULL;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);

    if (p_ipuc_info->masklen == 128)
    {
        ITEM_GET_PTR(p_table->p_item, stage_ip, p_real_item);

        if (NULL != p_real_item)
        {
            if (p_real_item->nhp_or_cnt == 1)
            {
                p_real_item->nhp_or_cnt--;
                p_real_item->pointer = INVALID_POINTER_OFFSET;
                p_real_item->pointer17_16 = 0x3;
                TABLE_FREE_PTR(p_table->p_ntable, stage_ip);
            }
            else
            {
                p_real_item->nhp_or_cnt--;
            }
        }

        return CTC_E_NONE;
    }

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        type = LPM_ITEM_TYPE_V6;
    }
    else if ((p_ipuc_info->masklen <= 16 && !p_gb_ipuc_master[lchip]->use_hash8) ||
             (p_ipuc_info->masklen <= 8 && p_gb_ipuc_master[lchip]->use_hash8))
    {
        type = LPM_ITEM_TYPE_V4;
    }

    real_idx = SYS_LPM_GET_ITEM_IDX(stage_ip, masklen);   /* the idx is the location of the route */
    child1_idx = (real_idx % 2) ? (real_idx - 1) : (real_idx + 1);
    parent_idx = SYS_LPM_GET_PARENT_ITEM_IDX(stage_ip, masklen);

    ITEM_GET_PTR(p_table->p_item, real_idx, p_real_item);
    ITEM_GET_PTR(p_table->p_item, child1_idx, p_child1_item);

    if (!p_real_item)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    real_ad_index = p_real_item->ad_index;

    /* unset this node */
    CTC_UNSET_FLAG(p_real_item->item_flag, LPM_ITEM_FLAG_REAL);

    /* judge whether or not push up when the real node is deleted */
    if (!CTC_FLAG_ISSET(p_real_item->item_flag, LPM_ITEM_FLAG_PARENT) &&
        (p_child1_item && (p_child1_item->t_masklen < masklen) &&
        CTC_FLAG_ISSET(p_child1_item->item_flag, LPM_ITEM_FLAG_VALID)) &&
        (p_real_item->nhp_or_cnt == 0 || CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)))
    {
        ITEM_GET_OR_INIT_PTR((p_table->p_item), parent_idx, p_parent_item, type);
        p_parent_item->t_masklen = p_child1_item->t_masklen;
        p_parent_item->ad_index = p_child1_item->ad_index;
        CTC_SET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_VALID);
        CTC_UNSET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_PARENT);
        SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free real node[%d] masklen: %d, ad_index: %d\n\tfree brother node[%d] masklen: %d, ad_index: %d\n\tpush up to parent node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n",
            real_idx, p_real_item->t_masklen, p_real_item->ad_index, child1_idx, p_child1_item->t_masklen, p_child1_item->ad_index, parent_idx, p_parent_item->t_masklen, p_parent_item->ad_index, p_parent_item->item_flag);

        ITEM_FREE_PTR(p_table->p_item, p_real_item);
        ITEM_FREE_PTR(p_table->p_item, p_child1_item);

        for (depth = masklen - 1; depth > 1; depth--)
        {
            child0_idx = SYS_LPM_GET_ITEM_IDX(stage_ip, depth);
            child1_idx = (child0_idx % 2) ? (child0_idx - 1) : (child0_idx + 1);
            parent_idx = SYS_LPM_GET_PARENT_ITEM_IDX(stage_ip, depth);

            ITEM_GET_PTR(p_table->p_item, child0_idx, p_child0_item);
            ITEM_GET_PTR(p_table->p_item, child1_idx, p_child1_item);

            if (p_child0_item && p_child1_item &&
                (p_child0_item->t_masklen == p_child1_item->t_masklen) &&
                (p_child0_item->ad_index == p_child1_item->ad_index) &&
                (p_child0_item->item_flag == p_child1_item->item_flag) &&
                (p_child0_item->t_masklen < depth))
            {
                    ITEM_GET_OR_INIT_PTR((p_table->p_item), parent_idx, p_parent_item, type);
                    p_parent_item->t_masklen = p_child1_item->t_masklen;
                    p_parent_item->ad_index = p_child1_item->ad_index;
                    CTC_SET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_VALID);
                    CTC_UNSET_FLAG(p_parent_item->item_flag, LPM_ITEM_FLAG_PARENT);

                    SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free child0 node[%d] masklen: %d, ad_index: %d\n\tfree child1 node[%d] masklen: %d, ad_index: %d\n\tpush up to parent node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n",
                        child0_idx, p_child0_item->t_masklen, p_child0_item->ad_index, child1_idx, p_child1_item->t_masklen, p_child1_item->ad_index, parent_idx, p_parent_item->t_masklen, p_parent_item->ad_index, p_parent_item->item_flag);

                    ITEM_FREE_PTR(p_table->p_item, p_child0_item);
                    ITEM_FREE_PTR(p_table->p_item, p_child1_item);
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        /* find last valid or real parent node */
        for (depth = masklen; depth > 1; depth--)
        {
            parent_idx = SYS_LPM_GET_PARENT_ITEM_IDX(stage_ip, depth);
            ITEM_GET_PTR(p_table->p_item, parent_idx, p_replace_item);

            if (p_replace_item && (CTC_FLAG_ISSET(p_replace_item->item_flag, LPM_ITEM_FLAG_VALID) ||
                    CTC_FLAG_ISSET(p_replace_item->item_flag, LPM_ITEM_FLAG_REAL)))
            {
                SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "found parent node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n", parent_idx, p_replace_item->t_masklen, p_replace_item->ad_index, p_replace_item->item_flag);
                break;
            }
            else
            {
                p_replace_item = NULL;
            }
        }

        if (!CTC_FLAG_ISSET(p_real_item->item_flag, LPM_ITEM_FLAG_PARENT))
        {/* the deleted real node don't have child node */
            if (p_replace_item)
            {
                p_real_item->t_masklen = p_replace_item->t_masklen;
                p_real_item->ad_index = p_replace_item->ad_index;
                CTC_SET_FLAG(p_real_item->item_flag, LPM_ITEM_FLAG_VALID);
                SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "replace real node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n", real_idx, p_real_item->t_masklen, p_real_item->ad_index, p_real_item->item_flag);
            }
            else
            {
                SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free real node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n", real_idx, p_real_item->t_masklen, p_real_item->ad_index, p_real_item->item_flag);
                if (p_real_item->nhp_or_cnt && CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
                {
                    /*means have 128bits route*/
                    return CTC_E_NONE;
                }
                else
                {
                    ITEM_FREE_PTR(p_table->p_item, p_real_item);
                }
            }
        }
        else
        {
            /* the deleted real node has child node, replace the pushed node by parent node*/
            for (depth = masklen; depth <= LPM_MASK_LEN; depth++)
            {
                depth_valid_ct = 0;
                offset = SYS_LPM_ITEM_OFFSET(stage_ip, depth);
                for (ch_loop = 0; ch_loop <= GET_MASK_MAX(depth - masklen); ch_loop++)
                {
                    child0_idx = GET_BASE(depth) + offset + ch_loop;
                    ITEM_GET_PTR(p_table->p_item, child0_idx, p_child0_item);

                    if (!p_child0_item)
                    {
                        continue;
                    }

                    if (CTC_FLAG_ISSET(p_child0_item->item_flag, LPM_ITEM_FLAG_VALID))
                    {
                        depth_valid_ct++;
                    }

                    /* find the pushed node of the deleted real node */
                    if ((p_child0_item->ad_index == real_ad_index) &&
                         (p_child0_item->t_masklen == masklen))
                     {  /* such as real node */
                        if (!CTC_FLAG_ISSET(p_child0_item->item_flag, LPM_ITEM_FLAG_VALID) && !CTC_FLAG_ISSET(p_child0_item->item_flag, LPM_ITEM_FLAG_PARENT))
                        {
                            SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free invalid node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n", child0_idx, p_real_item->t_masklen, p_real_item->ad_index, p_real_item->item_flag);
                            ITEM_FREE_PTR(p_table->p_item, p_child0_item);

                            return CTC_E_NONE;
                        }  /* don't have parent node, release all pushed node directly */
                        else if (!p_replace_item && !CTC_FLAG_ISSET(p_child0_item->item_flag, LPM_ITEM_FLAG_PARENT))
                        {
                            SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free pushed node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n", child0_idx, p_real_item->t_masklen, p_real_item->ad_index, p_real_item->item_flag);
                            ITEM_FREE_PTR(p_table->p_item, p_child0_item);
                        }  /* have parent node, replace all pushed node by parent node */
                        else if (p_replace_item)
                        {
                            p_child0_item->t_masklen = p_replace_item->t_masklen;
                            p_child0_item->ad_index = p_replace_item->ad_index;
                            SYS_LPM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "replace node[%d] mask: %d, ad_index: %d, flag: 0x%x!\n",
                                    child0_idx, p_child0_item->t_masklen, p_child0_item->ad_index, p_child0_item->item_flag);
                        } /* only parent node */
                        else if (CTC_FLAG_ISSET(p_child0_item->item_flag, LPM_ITEM_FLAG_PARENT))
                        {
                            p_child0_item->t_masklen = 0;
                            p_child0_item->ad_index = INVALID_NEXTHOP;
                        }
                    }

                    /* lookup end when valid count of current depth add push down count of last depth, note: invalid parent but not real node will bel free when add tree */
                    if ((last_valid_ct * 2 + depth_valid_ct) == (GET_MASK_MAX(depth - masklen) + 1))
                    {
                        return CTC_E_NONE;
                    }
                }
                last_valid_ct = last_valid_ct * 2 + depth_valid_ct;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_update_tree(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 index, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 offset = 0;
    uint16 i, j, idx;
    sys_lpm_item_t* p_item = NULL;

    SYS_IPUC_DBG_FUNC();

    if (p_ipuc_info->masklen == 128)
    {
        ITEM_GET_PTR((p_table->p_item), index, p_item);
        if (NULL == p_item)
        {
            SYS_IPUC_DBG_ERROR("ERROR: p_item is NULL\r\n");
            return CTC_E_UNEXPECT;
        }

        p_item->pointer = p_ipuc_info->pointer & POINTER_OFFSET_MASK;
        p_item->pointer17_16 = (p_ipuc_info->pointer >> POINTER_OFFSET_BITS_LEN) & 0x3;
        TABLE_SET_PTR(p_table->p_ntable, index, p_table);

        return CTC_E_NONE;
    }

    offset = (index & GET_MASK(masklen)) >> (LPM_MASK_LEN - masklen);
    idx = GET_BASE(masklen) + offset;

    ITEM_GET_PTR(p_table->p_item, idx, p_item);
    if (NULL == p_item)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_item is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    /* set this node */
    p_item->ad_index = p_ipuc_info->ad_offset;

    /* fill all sub route */
    for (i = masklen + 1; i <= LPM_MASK_LEN; i++)
    {
        offset = (index & GET_MASK(i)) >> (LPM_MASK_LEN - i);

        for (j = 0; j <= GET_MASK_MAX(i - masklen); j++)
        {
            idx = GET_BASE(i) + j + offset;
            ITEM_GET_PTR((p_table->p_item), idx, p_item);
            if (!p_item)
            {
                continue;
            }

            if (p_item->t_masklen <= masklen)
            {
                p_item->ad_index = p_ipuc_info->ad_offset;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_tbl_new(uint8 lchip, sys_lpm_tbl_t** pp_table, uint8 tbl_index, uint8 ip_ver)
{
    uint8 tbl_size = 0;

    SYS_IPUC_DBG_FUNC();

    tbl_size = ((tbl_index == LPM_TABLE_INDEX3) && (CTC_IP_VER_4 == ip_ver)) ?
        sizeof(sys_lpm_tbl_end_t) : sizeof(sys_lpm_tbl_t);

    *pp_table = mem_malloc(MEM_IPUC_MODULE, tbl_size);

    if (NULL == *pp_table)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(*pp_table, 0, tbl_size);
    (*pp_table)->offset = INVALID_POINTER & 0xFFFF;
    (*pp_table)->sram_index = INVALID_POINTER >> 16;
    (*pp_table)->tbl_flag |= ((tbl_index == LPM_TABLE_INDEX3) && (CTC_IP_VER_4 == ip_ver))
        ? LPM_TBL_FLAG_NO_NEXT : 0;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_item_clean(uint8 lchip, sys_lpm_tbl_t* p_table)
{
    sys_lpm_item_t* p_item;
    uint8 i;

    SYS_IPUC_DBG_FUNC();

    if (NULL == p_table)
    {
        return CTC_E_NONE;
    }

    for (i = 0; i < LPM_ITEM_ALLOC_NUM; i++)
    {
        p_item = p_table->p_item[i];

        while (p_item)
        {
            p_table->p_item[i] = p_table->p_item[i]->p_nitem;
            mem_free(p_item);
            p_item = p_table->p_item[i];
        }
    }

    return CTC_E_NONE;
}

/* write a ip to the tree, and push down */
STATIC int32
_sys_greatbelt_lpm_add_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 stage = 0;
    uint16 index = 0;
    sys_lpm_tbl_t* p_ntbl = NULL;
    uint32* ip = NULL;
    uint8 end = LPM_TABLE_INDEX3;

    SYS_IPUC_DBG_FUNC();

    ip = (CTC_IP_VER_4 == ip_ver) ? p_ipuc_info->ip.ipv4 : p_ipuc_info->ip.ipv6;

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        end = p_gb_ipuc_master[lchip]->use_hash8 ? LPM_TABLE_INDEX2 : LPM_TABLE_INDEX1;
    }

    for (stage = 0; stage <= end; stage++)
    {
        if (CTC_IP_VER_4 == ip_ver)
        {
            if (stage == 0)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
            }
            else if (stage == 1)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
            else if (stage == 2)
            {
                index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
        }
        else
        {
            index = SYS_IPUC_OCTO(ip, (11 - stage));
        }

        if (masklen > 8)
        {
            masklen -= 8;

            TABLE_GET_PTR(p_table[stage], index, p_ntbl);
            if (NULL == p_ntbl)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_lpm_tbl_new(lchip, &p_table[stage + 1], (stage + 1), ip_ver));
                TABLE_SET_PTR(p_table[stage]->p_ntable, index, p_table[stage + 1]);
                p_table[stage + 1]->index = index;
                p_table[stage]->count++;
            }
            else
            {
                p_table[stage + 1] = p_ntbl;
            }

            continue;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_lpm_add_tree(lchip, p_table[stage], index, masklen, p_ipuc_info));

        p_table[stage]->count++;
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatblet_lpm_del_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info, uint32* ip)
{
    int8 stage = 0;
    uint8 masklen_t = 0;
    uint8 index = 0;
    uint16 idx = 0;
    sys_lpm_tbl_t* p_ntbl = NULL;
    sys_lpm_tbl_t* p_nntbl = NULL;
    sys_lpm_item_t* p_item = NULL;

    SYS_IPUC_DBG_FUNC();

    for (stage = LPM_TABLE_INDEX3; stage >= (int8)LPM_TABLE_INDEX0; stage--)
    {
        if (NULL == p_table[(uint8)stage])
        {
            continue;
        }

        masklen_t = masklen - 8 * stage;
        if (ip_ver == CTC_IP_VER_4)
        {
            if (stage == 0)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
            }
            else if (stage == 1)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
            else if (stage == 2)
            {
                index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
        }
        else if (ip_ver == CTC_IP_VER_6)
        {
            index = SYS_IPUC_OCTO(ip, (SYS_IP_OCTO_88_95 - stage));
        }

        if (masklen_t > 8)
        {
            if ((stage < LPM_TABLE_INDEX3) && p_table[(uint8)stage + 1] && (0 == p_table[(uint8)stage + 1]->count))
            {
                TABLE_GET_PTR(p_table[(uint8)stage], index, p_ntbl);

                if (NULL == p_ntbl)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_ntbl is NULL for stage %d index %d\r\n", stage, index);
                    return CTC_E_UNEXPECT;
                }

                _sys_greatbelt_lpm_item_clean(lchip, p_ntbl);

                for (idx = 0; idx < LPM_TBL_NUM; idx++)
                {
                    TABLE_GET_PTR(p_ntbl, (idx / 2), p_nntbl);

                    if (p_nntbl)
                    {
                        TABLE_FREE_PTR(p_ntbl->p_ntable, (idx / 2));
                        mem_free(p_nntbl);
                    }
                }

                mem_free(p_ntbl);
                TABLE_FREE_PTR(p_table[(uint8)stage]->p_ntable, index);
                p_table[(uint8)stage + 1] = NULL;
                p_table[(uint8)stage]->count--;
            }

            continue;
        }

        idx = SYS_LPM_GET_ITEM_IDX(index, masklen_t);
        ITEM_GET_PTR(p_table[(uint8)stage]->p_item, idx, p_item);
        if (!p_item)
        {
            SYS_IPUC_DBG_ERROR("ERROR: p_item is NULL for stage %d idx %d\r\n", stage, idx);
            return CTC_E_UNEXPECT;
        }

        if ((CTC_IP_VER_6 == ip_ver) && (p_ipuc_info->masklen > 64))
        {
            _sys_greatbelt_lpm_del_tree(lchip, p_table[(uint8)stage], index, masklen_t, p_ipuc_info);
        }
        else if ((ip_ver != SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen > 32))
        {
            if (p_item->nhp_or_cnt == 1)
            {
                p_item->pointer = INVALID_POINTER_OFFSET;
                p_item->pointer17_16 = 0x3;
            }
            else
            {
                p_item->nhp_or_cnt--;
            }
        }
        else
        {
            if (ip_ver == SYS_IPUC_VER(p_ipuc_info))
            {
                _sys_greatbelt_lpm_del_tree(lchip, p_table[(uint8)stage], index, masklen_t, p_ipuc_info);
            }
            else
            {
                p_item->nhp_or_cnt--;
            }
        }

        p_table[(uint8)stage]->count--;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_update_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 stage = 0;
    uint16 index = 0;
    uint32* ip = NULL;
    sys_lpm_tbl_t* p_ntbl = NULL;

    SYS_IPUC_DBG_FUNC();
    ip = (CTC_IP_VER_4 == ip_ver) ? p_ipuc_info->ip.ipv4 : p_ipuc_info->ip.ipv6;

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        if (CTC_IP_VER_4 == ip_ver)
        {
            if (stage == 0)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
            }
            else if (stage == 1)
            {
                index = p_gb_ipuc_master[lchip]->use_hash8 ? SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15) : SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
            else if (stage == 2)
            {
                index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
            }
        }
        else
        {
            index = SYS_IPUC_OCTO(ip, (SYS_IP_OCTO_88_95 - stage));
        }

        if (masklen > 8)
        {
            masklen -= 8;

            TABLE_GET_PTR(p_table[stage], index, p_ntbl);
            if (NULL == p_ntbl)
            {
                SYS_IPUC_DBG_ERROR("ERROR: p_ntbl is NULL for stage %d index %d\r\n", stage, index);
                return CTC_E_UNEXPECT;
            }
            else
            {
                p_table[stage + 1] = p_ntbl;
            }

            continue;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_lpm_update_tree(lchip, p_table[stage], index, masklen, p_ipuc_info));

        break;
    }

    return CTC_E_NONE;
}

void
_sys_greatbelt_lpm_update_select_counter(uint8 lchip, sys_lpm_select_counter_t* select_counter,
                                     uint8 ip, uint8 mask, uint8 test_mask)
{
    uint8 next = 0;
    uint8 number = 0;
    uint8 index;

    for (index = 0; index <= 7; index++)
    {
        if (IS_BIT_SET(test_mask, index))
        {
            if (IS_BIT_SET(mask, index))
            {
                if (IS_BIT_SET(ip, index))
                {
                    SET_BIT(select_counter->block_base, next);
                }
            }
            /* One X bit index Occupy  2 ds_ip_prefix entries in one bucket */
            else
            {
                /* Per bucket_number add 2 ds_ip_prefix entry which totally 512 entries, max value is 7 */
                number++;
            }
            next++;
        }
    }

    select_counter->entry_number = (1 << (number & 0x7));
    /* 1^ bucket_number*/
}

static uint8 ds_lpm_test_mask_table[256] =
    {
        0,

        128,  64,  32,  16,   8,   4,   2,   1,

        192, 160, 144, 136, 132, 130, 129,  96,  80,  72,
        68,  66,  65,  48,  40,  36,  34,  33,  24,  20,
        18,  17,  12,  10,   9,   6,   5,   3,

        224, 208, 200, 196, 194, 193, 176, 168, 164, 162,
        161, 152, 148, 146, 145, 140, 138, 137, 134, 133,
        131, 112, 104, 100,  98,  97,  88,  84,  82,  81,
        76,  74,  73,  70,  69,  67,  56,  52,  50,  49,
        44,  42,  41,  38,  37,  35,  28,  26,  25,  22,
        21,  19,  14,  13,  11,   7,

        240, 232, 228, 226, 225, 216, 212, 210, 209, 204,
        202, 201, 198, 197, 195, 184, 180, 178, 177, 172,
        170, 169, 166, 165, 163, 156, 154, 153, 150, 149,
        147, 142, 141, 139, 135, 120, 116, 114, 113, 108,
        106, 105, 102, 101,  99,  92,  90,  89,  86,  85,
        83,  78,  77,  75,  71,  60,  58,  57,  54,  53,
        51,  46,  45,  43,  39,  30,  29,  27,  23,  15,

        248, 244, 242, 241, 236, 234, 233, 230, 229, 227,
        220, 218, 217, 214, 213, 211, 206, 205, 203, 199,
        188, 186, 185, 182, 181, 179, 174, 173, 171, 167,
        158, 157, 155, 151, 143, 124, 122, 121, 118, 117,
        115, 110, 109, 107, 103,  94,  93,  91,  87,  79,
        62,  61,  59,  55,  47,  31,

        252, 250, 249, 246, 245, 243, 238, 237, 235, 231,
        222, 221, 219, 215, 207, 190, 189, 187, 183, 175,
        159, 126, 125, 123, 119, 111,  95,  63,

        254, 253, 251, 247, 239, 223, 191, 127,

        255
    };

int32
_sys_greatbelt_lpm_calc_index_mask(uint8 lchip, uint8* new_test_mask,
                                    uint8 test_mask_start_index,
                                    uint16 prefix_count,
                                    uint8 test_mask_end_index,
                                    uint8 old_test_mask)
{
    uint8     ip = 0;
    uint8     mask = 0;
    uint8     test_mask = 0;
    uint8     i = 0;
    uint16    prefix_index = 0;
    uint32*   counter = NULL;
    sys_lpm_select_counter_t select_counter = {0};
    uint8    conflict = FALSE;
    uint16   test_mask_index = test_mask_start_index;

    counter = (uint32*)mem_malloc(MEM_IPUC_MODULE, sizeof(uint32)*(DS_LPM_PREFIX_MAX_INDEX / LPM_COMPRESSED_BLOCK_SIZE));
    if (NULL == counter)
    {
        return CTC_E_NO_MEMORY;
    }

    /* since in worst condition, the last index mask is determined to 255, so just need 255 times index test */
    for (; test_mask_index <= 255; test_mask_index++)
    {
        /* compute hash confilct summaries for all DsLpmPrefixTable entries one by one using this test index mask */
        if (test_mask_end_index == test_mask_index)
        {
            *new_test_mask = old_test_mask;
            mem_free(counter);
            return DRV_E_NONE;
        }

        test_mask = ds_lpm_test_mask_table[test_mask_index];

        sal_memset(counter, 0, sizeof(uint32)*(DS_LPM_PREFIX_MAX_INDEX / LPM_COMPRESSED_BLOCK_SIZE));

        for (prefix_index = 0; prefix_index < prefix_count; prefix_index++)
        {
            ip = p_gb_lpm_master[lchip]->ds_lpm_prefix[prefix_index].ip;

            if (LPM_LOOKUP_KEY_TYPE_POINTER == p_gb_lpm_master[lchip]->ds_lpm_prefix[prefix_index].type)
            {
                mask = 0xFF;
            }
            else
            {
                mask = p_gb_lpm_master[lchip]->ds_lpm_prefix[prefix_index].mask;
            }

            select_counter.block_base = 0;
            select_counter.entry_number = 0;
            _sys_greatbelt_lpm_update_select_counter(lchip, &select_counter, ip, mask, test_mask);

            conflict = FALSE;

            for (i = 0; i < select_counter.entry_number; i++)
            {
                /* a counter present 2 tuple in one block */
                counter[select_counter.block_base + i]++;

                if (counter[select_counter.block_base + i] > LPM_COMPRESSED_BLOCK_SIZE)
                {
                    conflict = TRUE;
                    break;
                }
            }

            if (conflict)
            {
                break;
            }
        }

        /* All ip_prefix known using this index_mask do not conflict with each other */
        if (!conflict)
        {
            *new_test_mask = test_mask;
            break;
        }
    }

    mem_free(counter);
    return DRV_E_NONE;
}

uint8
_sys_greatbelt_lpm_get_idx_mask(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 old_mask)
{
    uint16 i;
    uint16 j;
    uint16 idx;
    uint8 index_mask = 0;
    uint16 count = 0;
    uint16 nbit = 0;
    uint16 nbit_o = 0;
    sys_lpm_item_t* p_item;
    sys_lpm_tbl_t* p_ntbl;

    for (i = 0; i < LPM_TBL_NUM; i++)
    {
        TABLE_GET_PTR(p_table, i, p_ntbl);
        if (p_ntbl)
        {
            p_gb_lpm_master[lchip]->ds_lpm_prefix[count].mask = 0xFF;
            p_gb_lpm_master[lchip]->ds_lpm_prefix[count].ip = i;
            count++;
        }
    }

    for (i = LPM_MASK_LEN; i > 0; i--)
    {
        for (j = 0; j <= GET_MASK_MAX(i); j++)
        {
            idx = GET_BASE(i) + j;
            ITEM_GET_PTR(p_table->p_item, idx, p_item);
            if ((NULL != p_item) && p_item->t_masklen > 0 &&
                CTC_FLAG_ISSET(p_item->item_flag, LPM_ITEM_FLAG_VALID))
            {
                p_gb_lpm_master[lchip]->ds_lpm_prefix[count].mask = GET_MASK(i);
                p_gb_lpm_master[lchip]->ds_lpm_prefix[count].ip = (j << (LPM_MASK_LEN - i));

                count++;

                if (count > 256)
                {
                    return 0xff;
                }
            }
        }
    }

    for (nbit = 7; nbit > 0; nbit--)
    {
        if (count > (1 << nbit))
        {
            break;
        }
    }

    LPM_INDEX_TO_BITNUM(old_mask, nbit_o);

    if (nbit == nbit_o)
    {
        return old_mask;
    }

    if (nbit == 0)
    {
        return 0;
    }

    _sys_greatbelt_lpm_calc_index_mask(lchip, &index_mask, GET_INDEX_MASK_OFFSET(nbit), count,
                                        GET_INDEX_MASK_OFFSET(nbit_o), old_mask);
    return index_mask;
}

/* add lpm soft db */
STATIC int32
_sys_greatbelt_lpm_add_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 stage;
    uint8 masklen = p_ipuc_info->masklen;
    sys_lpm_result_t* p_lpm_result = NULL;
    uint8 end = LPM_TABLE_INDEX3;

    SYS_IPUC_DBG_FUNC();

    p_lpm_result = p_ipuc_info->lpm_result;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        if ((p_ipuc_info->masklen > 32) && (p_ipuc_info->masklen <= 64))
        {
            masklen -= 32;
        }
        else if (p_ipuc_info->masklen > 64)
        {
            masklen = 32;
        }
    }
    else
    {
        if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            masklen -= 8;
        }
        else
        {
            masklen -= 16;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_add_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info));

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        end = p_gb_ipuc_master[lchip]->use_hash8 ? LPM_TABLE_INDEX2 : LPM_TABLE_INDEX1;
    }

    for (stage = LPM_TABLE_INDEX0; stage <= end; stage++)
    {
        if (!p_table[stage])
        {
            continue;
        }

        /* save old lpm info for delete later */
        LPM_RLT_SET_OFFSET(p_ipuc_info, p_lpm_result, stage, p_table[stage]->offset);
        LPM_RLT_SET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage, p_table[stage]->sram_index);
        LPM_RLT_SET_IDX_MASK(p_ipuc_info, p_lpm_result, stage, p_table[stage]->idx_mask);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_del_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 masklen;
    uint32* ip;

    SYS_IPUC_DBG_FUNC();

    masklen = p_ipuc_info->masklen;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->masklen <= 32)
        {
            return CTC_E_NONE;
        }
        else if ((p_ipuc_info->masklen > 32) && (p_ipuc_info->masklen <= 64))
        {
            masklen -= 32;
        }
        else if (p_ipuc_info->masklen > 64)
        {
            masklen = 32;
        }

        ip = p_ipuc_info->ip.ipv6;
    }
    else
    {
        ip = p_ipuc_info->ip.ipv4;
        if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            masklen -= 8;
        }
        else
        {
            masklen -= 16;
        }
    }

    _sys_greatblet_lpm_del_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info, ip);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_lpm_update_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 masklen = p_ipuc_info->masklen;

    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->masklen <= 32)
        {
            return CTC_E_NONE;
        }
        else if ((p_ipuc_info->masklen > 32) && (p_ipuc_info->masklen <= 64))
        {
            masklen -= 32;
        }
        else if (p_ipuc_info->masklen > 64)
        {
            masklen = 32;
        }
    }
    else
    {
        if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            masklen -= 8;
        }
        else
        {
            masklen -= 16;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_update_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_update_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};

    SYS_IPUC_DBG_FUNC();
    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_table[LPM_TABLE_INDEX0] = &p_hash->n_table;
    p_table[LPM_TABLE_INDEX1] = NULL;
    p_table[LPM_TABLE_INDEX2] = NULL;
    p_table[LPM_TABLE_INDEX3] = NULL;

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_update_soft(lchip, p_ipuc_info, p_table));

    return CTC_E_NONE;
}


/* find most empty sram(4) to write */
int32
_sys_greatbelt_lpm_pipeline_add(uint8 lchip, uint8* index, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 stage, nstage, count = 0;
    uint16 tbl_idx;
    uint8 sram_index[LPM_TABLE_MAX] = {0};
    uint8 tmp_index;
    uint32 lpm_size[LPM_TABLE_MAX] = {0};
    uint16 size;
    uint8 next_index[LPM_TABLE_MAX] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8 now_index[LPM_TABLE_MAX] = {0xFF, 0xFF, 0xFF, 0xFF};
    sys_lpm_tbl_t* p_ntbl;

    SYS_IPUC_DBG_FUNC();

    for (stage = 0; stage < LPM_TABLE_MAX; stage++)
    {
        if (!p_table[stage])
        {
            continue;
        }

        if (INVALID_POINTER != (p_table[stage]->offset | (p_table[stage]->sram_index << 16)))
        {
            now_index[count] = p_table[stage]->sram_index & 0x3;
        }


        /* If there's more than two pipe line, for the second pipe line, there may be many trees
           in different sram, so need scan all the p_ntbl of second pipe line, find the most small
           sram index B. Than for the first pipe line, must select a sram which is small than B,
           and is most idle */

        for (tbl_idx = 0; tbl_idx < LPM_TBL_NUM; tbl_idx++)
        {
            TABLE_GET_PTR(p_table[stage], tbl_idx, p_ntbl);
            if (p_ntbl &&
                (INVALID_POINTER != ((p_ntbl->sram_index << 16) | p_ntbl->offset)) &&
                (next_index[count] > (p_ntbl->sram_index & 0x3)))
            {
                next_index[count] = p_ntbl->sram_index & 0x3;

            }
        }
        count++;
    }

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        index[stage] = LPM_TABLE_MAX - stage - 1;
    }

    if (count == LPM_TABLE_INDEX0 || count == LPM_TABLE_MAX)
    {
        return CTC_E_NONE;
    }

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        lpm_size[stage] = p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[stage] - p_gb_lpm_master[lchip]->sram_count[stage];
        sram_index[stage] = stage;
    }

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        if (p_table[stage] &&
            (INVALID_POINTER != (p_table[stage]->offset | (p_table[stage]->sram_index << 16))))
        {
            LPM_INDEX_TO_SIZE(p_table[stage]->idx_mask, size);
            lpm_size[p_table[stage]->sram_index] += size;
        }
    }

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX - 1; stage++)
    {
        for (nstage = stage + 1; nstage < LPM_TABLE_MAX; nstage++)
        {
            if (lpm_size[sram_index[stage]] <= lpm_size[sram_index[nstage]])
            {
                tmp_index = sram_index[nstage];
                sram_index[nstage] = sram_index[stage];
                sram_index[stage] = tmp_index;
            }
        }
    }

    for (stage = LPM_TABLE_INDEX0; stage < count - 1; stage++)
    {
        for (nstage = stage + 1; nstage < count; nstage++)
        {
            if (sram_index[stage] > sram_index[nstage])
            {
                tmp_index = sram_index[nstage];
                sram_index[nstage] = sram_index[stage];
                sram_index[stage] = tmp_index;
            }
        }
    }

    for (stage = LPM_TABLE_INDEX0; stage < count; stage++)
    {
        tmp_index = 0xFF;
        if (sram_index[stage] >= next_index[stage])
        {
            tmp_index = sram_index[stage];
            sram_index[stage] = now_index[stage];

            for (nstage = stage + 1; nstage < count; nstage++)
            {
                if ((0xFF != tmp_index) && (lpm_size[tmp_index] >= lpm_size[sram_index[nstage]]))
                {
                    tmp_index = sram_index[nstage] + tmp_index;
                    sram_index[nstage] = tmp_index - sram_index[nstage];
                    tmp_index = tmp_index - sram_index[nstage];
                }
                else
                {
                    tmp_index = 0xFF;
                }
            }
        }
    }

    for (stage = LPM_TABLE_INDEX0; stage < count; stage++)
    {
        index[stage] = sram_index[count - 1 - stage];
    }

    if(SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6)
    {
        if((index[count-1] != LPM_TABLE_INDEX0) || (index[count-1] != LPM_TABLE_INDEX1))
        {
            index[count-1] = LPM_TABLE_INDEX0;
        }
    }


    return CTC_E_NONE;
}

int32
_sys_greatblet_lpm_offset_alloc(uint8 lchip, uint8 sram_index,
                                uint32 entry_num, uint32* p_offset)
{
    sys_greatbelt_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 sram_size;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_offset);
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;

    switch (sram_index)
    {
    case 0:
        opf.pool_type = OPF_LPM_SRAM0;
        break;

    case 1:
        opf.pool_type = OPF_LPM_SRAM1;
        break;

    case 2:
        opf.pool_type = OPF_LPM_SRAM2;
        break;

    case 3:
        opf.pool_type = OPF_LPM_SRAM3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset);

    if (ret)
    {
        sram_size = p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[sram_index] -
            p_gb_lpm_master[lchip]->sram_count[sram_index] - LPM_SRAM_RSV_FOR_ARRANGE;

        if (sram_size >= entry_num)
        {
            return CTC_E_TOO_MANY_FRAGMENT;
        }
        else
        {
            return ret;
        }
    }

    p_gb_lpm_master[lchip]->sram_count[sram_index] += entry_num;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "allocate offset sram type : %d    start offset : %d   entry number: %d\n",
                     sram_index, *p_offset, entry_num);

    return CTC_E_NONE;
}

uint32
_sys_greatbelt_lpm_get_rsv_offset(uint8 lchip, uint8 sram_index)
{
    return (p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[sram_index] - LPM_SRAM_RSV_FOR_ARRANGE);
}

int32
_sys_greatbelt_lpm_offset_free(uint8 lchip, uint8 sram_index, uint32 entry_num, uint32 offset)
{
    sys_greatbelt_opf_t opf;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;

    switch (sram_index)
    {
    case 0:
        opf.pool_type = OPF_LPM_SRAM0;
        break;

    case 1:
        opf.pool_type = OPF_LPM_SRAM1;
        break;

    case 2:
        opf.pool_type = OPF_LPM_SRAM2;
        break;

    case 3:
        opf.pool_type = OPF_LPM_SRAM3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (offset == _sys_greatbelt_lpm_get_rsv_offset(lchip, sram_index))
    {
        return CTC_E_NONE;
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free offset sram type : %d     start offset : %d   entry number: %d\n",
                     sram_index, offset, entry_num);

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, offset));

    p_gb_lpm_master[lchip]->sram_count[sram_index] -= entry_num;

    return CTC_E_NONE;
}

/* build lpm hard index */
STATIC int32
_sys_greatbelt_lpm_calculate_asic_table(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table, uint8* index, uint8* old_mask)
{
    uint8 stage;
    uint8 idx;
    uint8 nstage;
    uint32 offset;
    uint16 size;
    uint32 sram_index;
    int32 ret;
    uint32 old_pointer[LPM_TABLE_MAX] = {0};
    uint8 old_idx_msk[LPM_TABLE_MAX] = {0};
    uint8 new_index;

    SYS_IPUC_DBG_FUNC();
    old_mask = old_mask + LPM_TABLE_MAX;

    for (idx = LPM_TABLE_INDEX3+1; idx > LPM_TABLE_INDEX0; idx--)
    {
        stage = idx - 1;
        old_mask--;
        if (!p_table[stage])
        {
            continue;
        }

        if (p_table[stage]->count == 0)
        {
            p_table[stage]->offset = INVALID_POINTER & 0xFFFF;
            p_table[stage]->sram_index = INVALID_POINTER >> 16;
            continue;
        }

        /* get p_table new pointer */
        new_index = _sys_greatbelt_lpm_get_idx_mask(lchip, p_table[stage], *old_mask);

        LPM_INDEX_TO_SIZE(new_index, size);

        /* alloc the SRAM */
        ret = _sys_greatblet_lpm_offset_alloc(lchip, *index, size, &offset);
        if (ret)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "alloc new offset fail sram %d  size %d \r\n", *index, size);

            for (nstage = stage + 1; nstage <= LPM_TABLE_INDEX3; nstage++)
            {
                if (!p_table[nstage])
                {
                    continue;
                }

                sram_index = p_table[nstage]->sram_index & 0x3;
                LPM_INDEX_TO_SIZE(p_table[nstage]->idx_mask, size);
                offset = p_table[nstage]->offset;
                _sys_greatbelt_lpm_offset_free(lchip, sram_index, size, offset);

                p_table[nstage]->idx_mask = old_idx_msk[nstage];
                p_table[nstage]->offset = old_pointer[nstage] & 0xFFFF;
                p_table[nstage]->sram_index = old_pointer[nstage] >> 16;
            }

            CTC_ERROR_RETURN(ret);
        }

        old_idx_msk[stage] = p_table[stage]->idx_mask;
        p_table[stage]->idx_mask = new_index;

        sram_index = (*index) & 0x3;

        index++;
        old_pointer[stage] = p_table[stage]->offset | (p_table[stage]->sram_index << 16);
        p_table[stage]->offset = offset;
        p_table[stage]->sram_index = sram_index;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};
    uint8 new_index[LPM_TABLE_MAX] = {0};
    uint8 old_mask[LPM_TABLE_MAX] = {0xFF, 0xFF, 0xFF, 0xFF};
    int32 ret = 0;
    uint16 size = 0;
    uint16 i = 0;
    sys_lpm_result_t* p_lpm_result = NULL;

    SYS_IPUC_DBG_FUNC();
    p_lpm_result = (sys_lpm_result_t*)p_ipuc_info->lpm_result;

    /* do add*/
    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_table[LPM_TABLE_INDEX0] = &p_hash->n_table;
    p_table[LPM_TABLE_INDEX1] = NULL;
    p_table[LPM_TABLE_INDEX2] = NULL;
    p_table[LPM_TABLE_INDEX3] = NULL;

    /* add lpm soft db */
    ret = _sys_greatbelt_lpm_add_soft(lchip, p_ipuc_info, p_table);
    /* find most empty sram(4) to write */
    ret = ret ? ret : _sys_greatbelt_lpm_pipeline_add(lchip, new_index, p_ipuc_info, p_table);
    /* build lpm hard index */
    ret = ret ? ret : _sys_greatbelt_lpm_calculate_asic_table(lchip, p_ipuc_info, p_table, new_index, old_mask);

    if (ret)
    {
        _sys_greatbelt_lpm_del_soft(lchip, p_ipuc_info, p_table);

        for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
        {
            LPM_RLT_INIT_POINTER_STAGE(p_ipuc_info, p_lpm_result, i);
        }
        return ret;
    }

    /* free old offset */
    for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
    {
        if (INVALID_POINTER != ((LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, i) |
                                 LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, i) << 16) & INVALID_POINTER))
        {
            LPM_INDEX_TO_SIZE(LPM_RLT_GET_IDX_MASK(p_ipuc_info, p_lpm_result, i), size);
            _sys_greatbelt_lpm_offset_free(lchip, LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, i),
                                           size, LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, i));
            LPM_RLT_INIT_POINTER_STAGE(p_ipuc_info, p_lpm_result, i);
        }
    }

    return CTC_E_NONE;
}

/* remove lpm soft db */
int32
_sys_greatbelt_lpm_del_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, bool redo)
{
    uint8 old_index[LPM_TABLE_MAX];
    uint8 new_index[LPM_TABLE_MAX] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8 tmp_mask[LPM_TABLE_MAX];
    uint32 old_pointer[LPM_TABLE_MAX];
    uint8 stage = 0;
    uint8 count = 0;
    uint16 size = 0;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};
    int32 ret = 0;

    SYS_IPUC_DBG_FUNC();
    p_lpm_result = p_ipuc_info->lpm_result;

    /*save old lpm info*/
    sal_memset(old_index, 0, sizeof(uint8) * LPM_TABLE_MAX);
    LPM_RLT_INIT_POINTER(p_ipuc_info, p_lpm_result);

    sal_memset(old_pointer, INVALID_HASH_INDEX_MASK, sizeof(uint32) * LPM_TABLE_MAX);

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        TABEL_GET_STAGE(stage, p_ipuc_info, p_table[stage]);
        tmp_mask[stage] = LPM_RLT_GET_IDX_MASK(p_ipuc_info, p_lpm_result, stage);
        if (NULL == p_table[stage])
        {
            continue;
        }

        old_index[stage] = p_table[stage]->idx_mask;
        LPM_RLT_SET_OFFSET(p_ipuc_info, p_lpm_result, stage, p_table[stage]->offset);
        LPM_RLT_SET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage, p_table[stage]->sram_index);
        LPM_RLT_SET_IDX_MASK(p_ipuc_info, p_lpm_result, stage, p_table[stage]->idx_mask);

        old_pointer[stage] = p_table[stage]->offset | (p_table[stage]->sram_index << 16);
    }

    _sys_greatbelt_lpm_del_soft(lchip, p_ipuc_info, p_table);

    if (!redo)
    {
        /* calc new stage */
        _sys_greatbelt_lpm_pipeline_add(lchip, new_index, p_ipuc_info, p_table);
    }
    else
    {
        /* use old stage */
        count = 0;

        for (stage = LPM_TABLE_INDEX0; stage <= LPM_TABLE_INDEX3; stage++)
        {
            if (NULL != p_table[LPM_TABLE_INDEX3 - stage])
            {
                new_index[count] = p_table[LPM_TABLE_INDEX3 - stage]->sram_index;
                count++;
            }
        }
    }

    ret = _sys_greatbelt_lpm_calculate_asic_table(lchip, p_ipuc_info, p_table, new_index, tmp_mask);
    if (ret)
    {
        _sys_greatbelt_lpm_add_soft(lchip, p_ipuc_info, p_table);

        for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
        {
            if (INVALID_POINTER != (old_pointer[stage] & INVALID_POINTER))
            {
                p_table[stage]->offset = old_pointer[stage] & 0xFFFF;
                p_table[stage]->sram_index = old_pointer[stage] >> 16;
            }

            LPM_RLT_INIT_POINTER_STAGE(p_ipuc_info, p_lpm_result, stage);
        }

        return ret;
    }

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        if (INVALID_POINTER != ((LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, stage) |
            LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage) << POINTER_OFFSET_BITS_LEN) & INVALID_POINTER))
        {
            LPM_INDEX_TO_SIZE(old_index[stage], size);
            CTC_ERROR_RETURN(_sys_greatbelt_lpm_offset_free(lchip, LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage), size,
                                                            LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, stage)));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_del(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;
    uint32 new_pointer = INVALID_POINTER;
    sys_lpm_tbl_t* p_table = NULL;
    uint8 size = 0;
    uint8 i = 0;
    uint8 sram_index = 0;

    SYS_IPUC_DBG_FUNC();
    if (((p_ipuc_info->masklen <= 32) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen <= 16) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (!p_gb_ipuc_master[lchip]->use_hash8))
        || ((p_ipuc_info->masklen <= 8) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) && (p_gb_ipuc_master[lchip]->use_hash8))
        || (p_ipuc_info->l4_dst_port > 0))
    {
        return CTC_E_NONE;
    }

    /* remove lpm soft db and hard index */
    ret = _sys_greatbelt_lpm_del_ex(lchip, p_ipuc_info, FALSE);
    if (CTC_E_NONE == ret)
    {
        return CTC_E_NONE;
    }

    /* return error LPM key full need to use rsv space to del*/
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "use rsv space to remove \n");

    for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
    {
        TABEL_GET_STAGE(i, p_ipuc_info, p_table);
        if (p_table)
        {
            size = size;
            LPM_INDEX_TO_SIZE(p_table->idx_mask, size);
            sram_index = p_table->sram_index & 0x3;
            new_pointer = _sys_greatbelt_lpm_get_rsv_offset(lchip, sram_index);
            new_pointer += (sram_index << POINTER_OFFSET_BITS_LEN);

            CTC_ERROR_RETURN(_sys_greatbelt_lpm_update(lchip, p_ipuc_info, i, new_pointer,
                                                            (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_lpm_del_ex(lchip, p_ipuc_info, TRUE));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_init(uint8 lchip, ctc_ip_ver_t ip_version, uint16 max_vrf_num)
{
    sys_greatbelt_opf_t opf;

    if (NULL == p_gb_lpm_master[lchip])
    {
        p_gb_lpm_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_lpm_master_t));

        if (NULL == p_gb_lpm_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gb_lpm_master[lchip], 0, sizeof(sys_lpm_master_t));
    }

    /* already inited */
    if (p_gb_lpm_master[lchip]->version_en[ip_version])
    {
        return CTC_E_NONE;
    }

    if (!p_gb_lpm_master[lchip]->version_en[CTC_IP_VER_4] && !p_gb_lpm_master[lchip]->version_en[CTC_IP_VER_6])
    {
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmLookupKey0_t,
                                              &(p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX0]));
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmLookupKey1_t,
                                              &(p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX1]));
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmLookupKey2_t,
                                              &(p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX2]));
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmLookupKey3_t,
                                              &(p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX3]));

        if (((p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX0]) <= LPM_SRAM_RSV_FOR_ARRANGE) ||
            ((p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX1]) <= LPM_SRAM_RSV_FOR_ARRANGE) ||
            ((p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX2]) <= LPM_SRAM_RSV_FOR_ARRANGE) ||
            ((p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX3]) <= LPM_SRAM_RSV_FOR_ARRANGE))
        {
            mem_free(p_gb_lpm_master[lchip]);
            return CTC_E_NO_RESOURCE;
        }

        sys_greatbelt_opf_init(lchip, OPF_LPM_SRAM0, 1);
        sys_greatbelt_opf_init(lchip, OPF_LPM_SRAM1, 1);
        sys_greatbelt_opf_init(lchip, OPF_LPM_SRAM2, 1);
        sys_greatbelt_opf_init(lchip, OPF_LPM_SRAM3, 1);

        /*init opf for LPM*/
        /* LPM_SRAM_RSV_FOR_ARRANGE reserve for arrange */
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_index = 0;
        opf.pool_type = OPF_LPM_SRAM0;
        sys_greatbelt_opf_init_offset(lchip, &opf, 0,
                                      (p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX0] - LPM_SRAM_RSV_FOR_ARRANGE));
        opf.pool_type = OPF_LPM_SRAM1;
        sys_greatbelt_opf_init_offset(lchip, &opf, 0,
                                      (p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX1] - LPM_SRAM_RSV_FOR_ARRANGE));
        opf.pool_type = OPF_LPM_SRAM2;
        sys_greatbelt_opf_init_offset(lchip, &opf, 0,
                                      (p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX2] - LPM_SRAM_RSV_FOR_ARRANGE));
        opf.pool_type = OPF_LPM_SRAM3;
        sys_greatbelt_opf_init_offset(lchip, &opf, 0,
                                      (p_gb_lpm_master[lchip]->lpm_lookup_key_table_size[LPM_TABLE_INDEX3] - LPM_SRAM_RSV_FOR_ARRANGE));
    }

    p_gb_lpm_master[lchip]->version_en[ip_version] = TRUE;

    p_gb_lpm_master[lchip]->dma_enable = TRUE;

    _sys_greatbelt_lpm_init_hash_mem_info(lchip);
    return CTC_E_NONE;
}

int32
_sys_greatbelt_lpm_deinit(uint8 lchip)
{
    if (NULL == p_gb_lpm_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (!p_gb_lpm_master[lchip]->version_en[CTC_IP_VER_4] && !p_gb_lpm_master[lchip]->version_en[CTC_IP_VER_6])
    {
        sys_greatbelt_opf_deinit(lchip, OPF_LPM_SRAM0);
        sys_greatbelt_opf_deinit(lchip, OPF_LPM_SRAM1);
        sys_greatbelt_opf_deinit(lchip, OPF_LPM_SRAM2);
        sys_greatbelt_opf_deinit(lchip, OPF_LPM_SRAM3);
    }

    mem_free(p_gb_lpm_master[lchip]);

    return CTC_E_NONE;
}


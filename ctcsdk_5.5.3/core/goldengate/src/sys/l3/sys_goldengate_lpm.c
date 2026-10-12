/**
 @file sys_goldengate_lpm.c

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
#include "ctc_debug.h"
 /*#include "ctc_dma.h"*/
#include "ctc_warmboot.h"

#include "sys_goldengate_sort.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_rpf_spool.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_l3_hash.h"
#include "sys_goldengate_ipuc.h"
#include "sys_goldengate_ipuc_db.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_lpm.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_common.h"

#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

 #define SYS_LPM_DBG_OUT(level, FMT, ...)                              \
    {                                                                       \
        if (0)  \
        {   \
            CTC_DEBUG_OUT(ip, ipuc, IPUC_SYS, level, FMT, ##__VA_ARGS__);  \
        }   \
    }

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_lpm_master_t* p_gg_lpm_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_ipuc_master_t* p_gg_ipuc_master[];
extern sys_l3_hash_master_t* p_gg_l3hash_master[];

/****************************************************************************
 *
* Function
*
*****************************************************************************/

STATIC int32
_sys_goldengate_ipuc_idx(uint8 lchip, uint8 ip, uint8 mask, uint8 index_mask, uint8* offset, uint8* num)
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
sys_goldengate_lpm_state_show(uint8 lchip)
{
    if (p_gg_lpm_master[lchip])
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%d\n", "ALPM", " ", p_gg_lpm_master[lchip]->lpm_lookup_key_table_size);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--IPv4", " ", "/", " ");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15s\n", p_gg_ipuc_master[lchip]->alpm_counter[CTC_IP_VER_4], " ", "/");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--IPv6", " ", "/", " ");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15d%-4s%-15s\n", p_gg_ipuc_master[lchip]->alpm_counter[CTC_IP_VER_6], " ", "/");

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--ALPM1", " ", "-", " ");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15d\n", "/", " ", p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX0]);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15s%-4s", "--ALPM2", " ", "-", " ");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s%-4s%-15d\n", "/", " ", p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX1]);
    }
}

#define __0_HARD_WRITE__
int32
sys_goldengate_lpm_enable_dma(uint8 lchip, uint8 enable)
{
    if (p_gg_lpm_master[lchip])
    {
        p_gg_lpm_master[lchip]->dma_enable = enable;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_lpm_asic_write_lpm(uint8 lchip, DsLpmLookupKey_m* key, uint32 start_offset, uint16 entry_size, uint32 cmd)
{
    uint16 i = 0;
    drv_work_platform_type_t platform_type = 0;
    sys_dma_tbl_rw_t tbl_cfg;
    tbls_id_t tbl_id;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tbl_cfg, 0, sizeof(tbl_cfg));

    drv_goldengate_get_platform_type(&platform_type);

    if ((platform_type == HW_PLATFORM) && (p_gg_lpm_master[lchip]->dma_enable))
    {

        tbl_id = DRV_IOC_MEMID(cmd);
        CTC_ERROR_RETURN(drv_goldengate_table_get_hw_addr(tbl_id, start_offset, &tbl_cfg.tbl_addr, 0));
        tbl_cfg.buffer = (uint32*)key;
        tbl_cfg.entry_num = entry_size;
        tbl_cfg.entry_len = sizeof(DsLpmLookupKey_m);
        tbl_cfg.rflag = 0;
        sys_goldengate_dma_rw_table(lchip, &tbl_cfg);
    }
    else
    {
        for (i = 0; i < entry_size; i++)
        {
            DRV_IOCTL(lchip, start_offset + i, cmd, key);

            key ++;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage)
{
    DsLpmLookupKey_m* ds_lpm_lookup_key = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    sys_lpm_tbl_t* p_ntbl = NULL;
    sys_lpm_item_t* p_item = NULL;
    uint8 offset, item_size, ip, lpm_stage;
    uint16 i, j, k, idx, entry_size;
    uint32 cmd = 0;
    uint8 sram_index = 0;
    uint32 pointer = 0;
    uint32 type = 0;

    SYS_IPUC_DBG_FUNC();
    if (((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4)))
    {
        return CTC_E_NONE;
    }

	ds_lpm_lookup_key = mem_malloc(MEM_IPUC_MODULE, sizeof(DsLpmLookupKey_m)*256);
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
        sal_memset(ds_lpm_lookup_key, 0, sizeof(DsLpmLookupKey_m) * entry_size);

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
                _sys_goldengate_ipuc_idx(lchip, i, 0xff, p_table->idx_mask, &offset, &item_size);

                /* the pointer value is the relative lpm lookup key address
                 * and p_table->n_table[i]->pointer is the absolute adress
                 * since lpm pipeline lookup mechanism,
                 * the address should use relative address
                 * the code added here suppose ipv4
                 */
                if ((p_table->sram_index & POINTER_STAGE_MASK) == LPM_TABLE_INDEX0)
                {
                    sram_index = p_ntbl->sram_index - LPM_TABLE_INDEX1;
                }

                GetDsLpmLookupKey(A, type0_f, &ds_lpm_lookup_key[offset], &type);

                if (LPM_PIPELINE_KEY_TYPE_EMPTY == type)
                {
                    SetDsLpmLookupKey(V, mask0_f, &ds_lpm_lookup_key[offset], p_ntbl->idx_mask);
                    SetDsLpmLookupKey(V, ip0_f, &ds_lpm_lookup_key[offset], i);
                    pointer = ((sram_index & POINTER_STAGE_MASK) << POINTER_OFFSET_BITS_LEN) | (p_ntbl->offset & POINTER_OFFSET_MASK);
                    SetDsLpmLookupKey(V, nexthop0_f, &ds_lpm_lookup_key[offset], pointer);
                    SetDsLpmLookupKey(V, type0_f, &ds_lpm_lookup_key[offset], LPM_PIPELINE_KEY_TYPE_POINTER);
                }
                else
                {
                    SetDsLpmLookupKey(V, mask1_f, &ds_lpm_lookup_key[offset], p_ntbl->idx_mask);
                    SetDsLpmLookupKey(V, ip1_f, &ds_lpm_lookup_key[offset], i);
                    pointer = ((sram_index & POINTER_STAGE_MASK) << POINTER_OFFSET_BITS_LEN) | (p_ntbl->offset & POINTER_OFFSET_MASK);
                    SetDsLpmLookupKey(V, nexthop1_f, &ds_lpm_lookup_key[offset], pointer);
                    SetDsLpmLookupKey(V, type1_f, &ds_lpm_lookup_key[offset], LPM_PIPELINE_KEY_TYPE_POINTER);
                }

                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                 "write DsLpmLookupKey%d[%d], ip = 0x%x, pointer = 0x%X   \r\n",
                                 p_table->sram_index, p_table->offset+offset, i, (sram_index << POINTER_OFFSET_BITS_LEN) | p_ntbl->offset);
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

                _sys_goldengate_ipuc_idx(lchip, ip, GET_MASK(i), p_table->idx_mask, &offset, &item_size);

                for (k = 0; k < item_size; k++)
                {
                    GetDsLpmLookupKey(A, type0_f, &ds_lpm_lookup_key[offset + k], &type);
                    if (LPM_PIPELINE_KEY_TYPE_EMPTY == type)
                    {
                        SetDsLpmLookupKey(V, mask0_f, &ds_lpm_lookup_key[offset+k], GET_MASK(i));
                        SetDsLpmLookupKey(V, ip0_f, &ds_lpm_lookup_key[offset+k], ip);
                        SetDsLpmLookupKey(V, nexthop0_f, &ds_lpm_lookup_key[offset+k], p_item->ad_index);
                        SetDsLpmLookupKey(V, type0_f, &ds_lpm_lookup_key[offset+k], LPM_PIPELINE_KEY_TYPE_NEXTHOP);
                    }
                    else
                    {
                        SetDsLpmLookupKey(V, mask1_f, &ds_lpm_lookup_key[offset+k], GET_MASK(i));
                        SetDsLpmLookupKey(V, ip1_f, &ds_lpm_lookup_key[offset+k], ip);
                        SetDsLpmLookupKey(V, nexthop1_f, &ds_lpm_lookup_key[offset+k], p_item->ad_index);
                        SetDsLpmLookupKey(V, type1_f, &ds_lpm_lookup_key[offset+k], LPM_PIPELINE_KEY_TYPE_NEXTHOP);
                    }

                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                                     "write DsLpmLookupKey%d[%d], ip = 0x%x, nexthop = 0x%x  \r\n",
                                     p_table->sram_index, p_table->offset+offset, ip, p_item->ad_index);
                }
            }
        }

        cmd = DRV_IOW(DsLpmLookupKey_t, DRV_ENTRY_FLAG);

        /* notice */
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " start offset:%d    write entry_size:%d\n", p_table->offset, entry_size);

        _sys_goldengate_lpm_asic_write_lpm(lchip, ds_lpm_lookup_key, p_table->offset, entry_size, cmd);
    }

	mem_free(ds_lpm_lookup_key);

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_update(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage, uint32 new_pointer, uint32 old_pointer)
{
    uint16 size;
    uint8 sram_index;
    uint32 offset;
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

    sram_index = (old_pointer >> POINTER_OFFSET_BITS_LEN) & POINTER_STAGE_MASK;
    offset = old_pointer & POINTER_OFFSET_MASK;
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

    p_table->offset = new_pointer & POINTER_OFFSET_MASK;
    p_table->sram_index = new_pointer >> POINTER_OFFSET_BITS_LEN;

    CTC_ERROR_RETURN(_sys_goldengate_lpm_add_key(lchip, p_ipuc_info, stage));

    if (NULL != p_table_pre)
    {
        CTC_ERROR_RETURN(_sys_goldengate_lpm_add_key(lchip, p_ipuc_info, stage - 1));
    }
    else
    {
        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
        {
            CTC_ERROR_RETURN(_sys_goldengate_l3_hash_add_key_by_type(lchip, p_ipuc_info, 0));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_l3_hash_add_key_by_type(lchip, p_ipuc_info, 0));
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_free(lchip, sram_index, size, offset));

    return CTC_E_NONE;
}

#define __1_HARD_WRITE__
STATIC int32
_sys_goldengate_lpm_add_tree(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 stage_ip, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
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

    if (!masklen)
    {
        return CTC_E_NONE;
    }

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        type = LPM_ITEM_TYPE_V6;
    }
    else if (p_ipuc_info->masklen <= p_gg_ipuc_master[lchip]->masklen_l)
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
            valid_parent_masklen = depth - 1;
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
_sys_goldengate_lpm_del_tree(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 stage_ip, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
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

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        type = LPM_ITEM_TYPE_V6;
    }
    else if (p_ipuc_info->masklen <= p_gg_ipuc_master[lchip]->masklen_l)
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
        CTC_FLAG_ISSET(p_child1_item->item_flag, LPM_ITEM_FLAG_VALID)))
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
                ITEM_FREE_PTR(p_table->p_item, p_real_item);
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

/* only update lpm ad_index */
int32
_sys_goldengate_lpm_update_tree_ad(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 index, uint8 masklen, sys_ipuc_info_t* p_ipuc_info)
{
    sys_lpm_item_t* p_item = NULL;
    uint16 idx = 0;
    uint8 prefix_len = 0;
    uint8 offset = 0;
    uint8 depth = 0;
    uint8 loop = 0;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);

    prefix_len = (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4) ? p_gg_ipuc_master[lchip]->masklen_l : p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    masklen = p_ipuc_info->masklen - prefix_len;
    if (!masklen)
    {
        return CTC_E_NONE;
    }

    /* 2. update ad_index */
    offset = (index & GET_MASK(masklen)) >> (LPM_MASK_LEN - masklen);
    idx = GET_BASE(masklen) + offset;   /* the idx is the location of the route */
    ITEM_GET_PTR(p_table->p_item, idx, p_item);
    if (!p_item)
    {
        return CTC_E_NOT_EXIST;
    }

    p_item->ad_index = p_ipuc_info->ad_offset;

    /* 3. fill all sub push down node */
    for (depth = masklen + 1; depth <= LPM_MASK_LEN; depth++)
    {
        offset = (index & GET_MASK(depth)) >> (LPM_MASK_LEN - depth);
        for (loop = 0; loop <= GET_MASK_MAX(depth - masklen); loop++)
        {
            idx = GET_BASE(depth) + loop + offset;
            ITEM_GET_PTR(p_table->p_item, idx, p_item);
            if (!p_item)
            {
                continue;
            }

            if (p_item->t_masklen == masklen)
            {
                p_item->ad_index = p_ipuc_info->ad_offset;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_lpm_tbl_new(uint8 lchip, sys_lpm_tbl_t** pp_table, uint8 tbl_index, uint8 ip_ver)
{
    uint8 tbl_size = 0;

    SYS_IPUC_DBG_FUNC();

    /*tbl_size = ((tbl_index == LPM_TABLE_INDEX1) && (CTC_IP_VER_4 == ip_ver)) ?
        sizeof(sys_lpm_tbl_end_t) : sizeof(sys_lpm_tbl_t);*/

    tbl_size = (tbl_index == LPM_TABLE_INDEX1) ?
        sizeof(sys_lpm_tbl_end_t) : sizeof(sys_lpm_tbl_t);

    *pp_table = mem_malloc(MEM_IPUC_MODULE, tbl_size);

    if (NULL == *pp_table)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(*pp_table, 0, tbl_size);
    (*pp_table)->offset = INVALID_POINTER;
    (*pp_table)->sram_index = INVALID_POINTER >> POINTER_OFFSET_BITS_LEN;
    (*pp_table)->tbl_flag |= (tbl_index == LPM_TABLE_INDEX1)
        ? LPM_TBL_FLAG_NO_NEXT : 0;

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_item_clean(uint8 lchip, sys_lpm_tbl_t* p_table)
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
_sys_goldengate_lpm_add_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 stage = 0;
    uint16 index = 0;
    sys_lpm_tbl_t* p_ntbl = NULL;
    uint32* ip = NULL;
    uint8 end = LPM_TABLE_INDEX1;
    uint8 tcam_masklen = 0;

    SYS_IPUC_DBG_FUNC();

    ip = (CTC_IP_VER_4 == ip_ver) ? p_ipuc_info->ip.ipv4 : p_ipuc_info->ip.ipv6;

    for (stage = 0; stage <= end; stage++)
    {
        if (CTC_IP_VER_4 == ip_ver)
        {
            if (p_gg_ipuc_master[lchip]->use_hash16)
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
                }
            }
            else
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
            }
        }
        else
        {
            tcam_masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
            index = SYS_IPUC_OCTO(ip, (SYS_IP_OCTO_88_95 - (tcam_masklen-32)/8 - stage));
        }

        if (masklen > 8)
        {
            masklen -= 8;

            TABLE_GET_PTR(p_table[stage], index, p_ntbl);
            if (NULL == p_ntbl)
            {
                CTC_ERROR_RETURN(_sys_goldengate_lpm_tbl_new(lchip, &p_table[stage + 1], (stage + 1), ip_ver));
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

        /* push down */
        CTC_ERROR_RETURN(_sys_goldengate_lpm_add_tree(lchip, p_table[stage], index, masklen, p_ipuc_info));

        p_table[stage]->count++;
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_lpm_del_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info, uint32* ip)
{
    int8 stage = 0;
    uint8 masklen_t = 0;
    uint8 index = 0;
    uint16 idx = 0;
    uint8 tcam_masklen = 0;
    sys_lpm_tbl_t* p_ntbl = NULL;
    sys_lpm_tbl_t* p_nntbl = NULL;

    SYS_IPUC_DBG_FUNC();

    for (stage = LPM_TABLE_INDEX1; stage >= (int8)LPM_TABLE_INDEX0; stage--)
    {
        if (NULL == p_table[stage])
        {
            continue;
        }

        masklen_t = masklen - 8 * stage;
        if (ip_ver == CTC_IP_VER_4)
        {
            if (p_gg_ipuc_master[lchip]->use_hash16)
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_0_7);
                }
            }
            else
            {
                if (stage == 0)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_16_23);
                }
                else if (stage == 1)
                {
                    index = SYS_IPUC_OCTO(ip, SYS_IP_OCTO_8_15);
                }
            }
        }
        else if (ip_ver == CTC_IP_VER_6)
        {
            tcam_masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
            index = SYS_IPUC_OCTO(ip, (SYS_IP_OCTO_88_95 - (tcam_masklen-32)/8 - stage));
        }

        if (masklen_t > 8)
        {
            if ((stage < LPM_TABLE_INDEX1) && p_table[stage + 1] && (0 == p_table[stage + 1]->count))
            {
                TABLE_GET_PTR(p_table[stage], index, p_ntbl);

                if (NULL == p_ntbl)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_ntbl is NULL for stage %d index %d\r\n", stage, index);
                    return CTC_E_UNEXPECT;
                }

                _sys_goldengate_lpm_item_clean(lchip, p_ntbl);

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
                TABLE_FREE_PTR(p_table[stage]->p_ntable, index);
                p_table[stage + 1] = NULL;
                p_table[stage]->count--;
            }

            continue;
        }

        if (ip_ver == SYS_IPUC_VER(p_ipuc_info))
        {
            _sys_goldengate_lpm_del_tree(lchip, p_table[stage], index, masklen_t, p_ipuc_info);
        }

        p_table[stage]->count--;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_lpm_update_ip_tree(uint8 lchip, uint8 masklen, ctc_ip_ver_t ip_ver, sys_lpm_tbl_t** p_table, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 stage;
    uint8 offset;
    uint16 index;
    uint32* ip = NULL;
    sys_lpm_tbl_t* p_ntbl = NULL;
    sys_lpm_item_t* p_item = NULL;

    SYS_IPUC_DBG_FUNC();
    ip = (CTC_IP_VER_4 == ip_ver) ? p_ipuc_info->ip.ipv4 : p_ipuc_info->ip.ipv6;

    for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
    {
        if (masklen > 8)
        {
            masklen -= 8;

            if (CTC_IP_VER_4 == ip_ver)
            {
                index = SYS_IPUC_OCTO(ip, (3 - stage));
            }
            else if (CTC_IP_VER_6 == ip_ver)
            {
                index = SYS_IPUC_OCTO(ip, (11 - stage));
            }
            else
            {
                index = SYS_IPUC_OCTO(ip, (15 - stage));
            }

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

        if (MAX_CTC_IP_VER == ip_ver)
        {
            index = (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) ? SYS_IPUC_OCTO(ip, (3 - stage)) :
                SYS_IPUC_OCTO(ip, (15 - stage));
            offset = (index & GET_MASK(masklen)) >> (LPM_MASK_LEN - masklen);
            index = GET_BASE(masklen) + offset;

            ITEM_GET_PTR((p_table[stage]->p_item), index, p_item);

            if (NULL == p_item)
            {
                SYS_IPUC_DBG_ERROR("ERROR: p_item is NULL\r\n");
                return CTC_E_UNEXPECT;
            }

            if ((((CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen > 16))
                 || ((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && (p_ipuc_info->masklen > 32))))
            {
                ;
            }
            else
            {
                p_item->ad_index = p_ipuc_info->ad_offset;
            }

            break;
        }
        else if (CTC_IP_VER_4 == ip_ver)
        {
            index = SYS_IPUC_OCTO(ip, (3 - stage));
            _sys_goldengate_lpm_update_tree_ad(lchip, p_table[stage], index, masklen, p_ipuc_info);
        }
        else if (CTC_IP_VER_6 == ip_ver)
        {
            index = SYS_IPUC_OCTO(ip, (11 - stage));
            if (p_ipuc_info->masklen == 128)
            {
                /* to identify it by set next table ptr to oneself  */
                 /*CTC_ERROR_RETURN(_sys_goldengate_lpm_update_P(p_table[stage], index, p_ipuc_info->pointer));*/
            }
            else
            {
                CTC_ERROR_RETURN(_sys_goldengate_lpm_update_tree_ad(lchip, p_table[stage], index, masklen, p_ipuc_info));
            }
        }

        break;
    }

    return CTC_E_NONE;
}

STATIC void
_sys_goldengate_lpm_update_select_counter(uint8 lchip, sys_lpm_select_counter_t* select_counter,
                                     uint8 ip, uint8 mask, uint8 test_mask)
{
    uint8 next = 0;
    uint8 number = 0;
    uint8 index = 0;

    for (index = 0; index <= 7; index++)
    {
        if (IS_BIT_SET(test_mask, index))
        {
            if (IS_BIT_SET(ip, index) && IS_BIT_SET(mask, index))
            {
                SET_BIT(select_counter->block_base, next);
            }

            next++;

            /* One X bit index Occupy  2 ds_ip_prefix entries in one bucket */
            if (!IS_BIT_SET(mask, index))
            {
                /* Per bucket_number add 2 ds_ip_prefix entry which totally 512 entries, max value is 7 */
                number++;
            }
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

STATIC int32
_sys_goldengate_lpm_calc_index_mask(uint8 lchip, uint8* new_test_mask,
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
            ip = p_gg_lpm_master[lchip]->ds_lpm_prefix[prefix_index].ip;

            if (LPM_PIPELINE_KEY_TYPE_POINTER == p_gg_lpm_master[lchip]->ds_lpm_prefix[prefix_index].type)
            {
                mask = 0xFF;
            }
            else
            {
                mask = p_gg_lpm_master[lchip]->ds_lpm_prefix[prefix_index].mask;
            }

            select_counter.block_base = 0;
            select_counter.entry_number = 0;
            _sys_goldengate_lpm_update_select_counter(lchip, &select_counter, ip, mask, test_mask);

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

STATIC uint8
_sys_goldengate_lpm_get_idx_mask(uint8 lchip, sys_lpm_tbl_t* p_table, uint8 old_mask)
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
            p_gg_lpm_master[lchip]->ds_lpm_prefix[count].mask = 0xFF;
            p_gg_lpm_master[lchip]->ds_lpm_prefix[count].ip = i;
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
                p_gg_lpm_master[lchip]->ds_lpm_prefix[count].mask = GET_MASK(i);
                p_gg_lpm_master[lchip]->ds_lpm_prefix[count].ip = (j << (LPM_MASK_LEN - i));

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

    _sys_goldengate_lpm_calc_index_mask(lchip, &index_mask, GET_INDEX_MASK_OFFSET(nbit), count,
                                        GET_INDEX_MASK_OFFSET(nbit_o), old_mask);

    return index_mask;
}

STATIC int32
_sys_goldengate_lpm_update_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
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

    CTC_ERROR_RETURN(_sys_goldengate_lpm_update_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info));

    return CTC_E_NONE;
}

/* add lpm soft db */
STATIC int32
_sys_goldengate_lpm_add_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 stage;
    uint8 masklen = p_ipuc_info->masklen;
    sys_lpm_result_t* p_lpm_result = NULL;
    uint8 end = LPM_TABLE_INDEX1;

    SYS_IPUC_DBG_FUNC();

    p_lpm_result = p_ipuc_info->lpm_result;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        masklen -= p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    }
    else
    {
        masklen -= p_gg_ipuc_master[lchip]->masklen_l;
    }

    CTC_ERROR_RETURN(_sys_goldengate_lpm_add_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        return CTC_E_NONE;
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
_sys_goldengate_lpm_del_soft(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table)
{
    uint8 masklen;
    uint32* ip;

    SYS_IPUC_DBG_FUNC();

    masklen = p_ipuc_info->masklen;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        masklen -= p_gg_ipuc_master[lchip]->masklen_ipv6_l;
        ip = p_ipuc_info->ip.ipv6;
    }
    else
    {
        ip = p_ipuc_info->ip.ipv4;
        masklen -= p_gg_ipuc_master[lchip]->masklen_l;
    }

    _sys_goldengate_lpm_del_ip_tree(lchip, masklen, SYS_IPUC_VER(p_ipuc_info), p_table, p_ipuc_info, ip);

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_offset_alloc(uint8 lchip, uint8 sram_index,
                                uint32 entry_num, uint32* p_offset)
{
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 sram_size;

    SYS_IPUC_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_offset);
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_index = 0;
    opf.pool_type = OPF_LPM_SRAM0;

    ret = sys_goldengate_opf_alloc_offset(lchip, &opf, entry_num, p_offset);

    if (ret)
    {
        sram_size = p_gg_lpm_master[lchip]->lpm_lookup_key_table_size - p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX0] -
			p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX1] - (LPM_SRAM_RSV_FOR_ARRANGE*2);

        if (sram_size >= entry_num)
        {
            return CTC_E_TOO_MANY_FRAGMENT;
        }
        else
        {
            return ret;
        }
    }

    p_gg_lpm_master[lchip]->alpm_usage[sram_index] += entry_num;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "allocate offset sram type : %d    start offset : %d   entry number: %d\n",
                     sram_index, *p_offset, entry_num);

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_offset_alloc_from_position(uint8 lchip, uint8 sram_index,
                                uint32 entry_num, uint32 offset)
{
    sys_goldengate_opf_t opf;
    int32 ret = CTC_E_NONE;
    uint32 sram_size;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_index = 0;
    opf.pool_type = OPF_LPM_SRAM0;

    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, entry_num, offset);

    if (ret && ret != CTC_E_ENTRY_EXIST)
    {
        sram_size = p_gg_lpm_master[lchip]->lpm_lookup_key_table_size - p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX0] -
			p_gg_lpm_master[lchip]->alpm_usage[LPM_TABLE_INDEX1] - (LPM_SRAM_RSV_FOR_ARRANGE*2);

        if (sram_size >= entry_num)
        {
            return CTC_E_TOO_MANY_FRAGMENT;
        }
        else
        {
            return ret;
        }
    }

    if (ret != CTC_E_ENTRY_EXIST)
    {
        p_gg_lpm_master[lchip]->alpm_usage[sram_index] += entry_num;
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "allocate offset from position sram type : %d    start offset : %d   entry number: %d\n",
                     sram_index, offset, entry_num);

    return CTC_E_NONE;
}

uint32
_sys_goldengate_lpm_get_rsv_offset(uint8 lchip, uint8 sram_index)
{
    return (p_gg_lpm_master[lchip]->lpm_lookup_key_table_size - (LPM_SRAM_RSV_FOR_ARRANGE*(sram_index+1)));
}

int32
_sys_goldengate_lpm_offset_free(uint8 lchip, uint8 sram_index, uint32 entry_num, uint32 offset)
{
    sys_goldengate_opf_t opf;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    opf.pool_index = 0;
    opf.pool_type = OPF_LPM_SRAM0;

    if (offset == _sys_goldengate_lpm_get_rsv_offset(lchip, sram_index))
    {
        return CTC_E_NONE;
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free offset sram type : %d     start offset : %d   entry number: %d\n",
                     sram_index, offset, entry_num);

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, entry_num, offset));

    p_gg_lpm_master[lchip]->alpm_usage[sram_index] -= entry_num;

    return CTC_E_NONE;
}

/* build lpm hard index */
STATIC int32
_sys_goldengate_lpm_calculate_asic_table(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table, uint8* index, uint8* old_mask)
{
    int8 stage;
    uint32 offset[LPM_TABLE_MAX];
    uint16 size[LPM_TABLE_MAX];
    int32 ret;
    uint8 new_index[LPM_TABLE_MAX];

    SYS_IPUC_DBG_FUNC();

    /* Here, must alloc big stage size first, or may alloc fail when do delete */
    old_mask = old_mask + LPM_TABLE_MAX;
    size[LPM_TABLE_INDEX0] = 0;
    size[LPM_TABLE_INDEX1] = 0;
    offset[LPM_TABLE_INDEX0] = 0;
    offset[LPM_TABLE_INDEX1] = 0;
    new_index[LPM_TABLE_INDEX0] = 0;
    new_index[LPM_TABLE_INDEX1] = 0;
    for (stage = LPM_TABLE_INDEX1; stage >= (int8)LPM_TABLE_INDEX0; stage--)
    {
        old_mask--;
        if (!p_table[stage])
        {
            continue;
        }

        if (p_table[stage]->count == 0)
        {
            p_table[stage]->offset = INVALID_POINTER & POINTER_OFFSET_MASK;
            p_table[stage]->sram_index = INVALID_POINTER >> POINTER_OFFSET_BITS_LEN;
            continue;
        }

        /* get p_table new pointer */
        new_index[stage] = _sys_goldengate_lpm_get_idx_mask(lchip, p_table[stage], *old_mask);

        LPM_INDEX_TO_SIZE(new_index[stage], size[stage]);
    }

    /* alloc the SRAM */
    if ((size[LPM_TABLE_INDEX0] > 0) && (size[LPM_TABLE_INDEX1] > 0))
    {
        if (size[LPM_TABLE_INDEX0] > size[LPM_TABLE_INDEX1])
        {
            CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], &(offset[LPM_TABLE_INDEX0])));
            ret = _sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], &(offset[LPM_TABLE_INDEX1]));
            if (ret)
            {
                _sys_goldengate_lpm_offset_free(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], offset[LPM_TABLE_INDEX0]);
                return ret;
            }
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], &(offset[LPM_TABLE_INDEX1])));
            ret = _sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], &(offset[LPM_TABLE_INDEX0]));
            if (ret)
            {
                _sys_goldengate_lpm_offset_free(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], offset[LPM_TABLE_INDEX1]);
                return ret;
            }
        }
    }
    else if (size[LPM_TABLE_INDEX0] > 0)
    {
        CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], &(offset[LPM_TABLE_INDEX0])));
    }
    else if (size[LPM_TABLE_INDEX1] > 0)
    {
        CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], &(offset[LPM_TABLE_INDEX1])));
    }

    /* update db */
    for (stage = LPM_TABLE_INDEX1; stage >= (int8)LPM_TABLE_INDEX0; stage--)
    {
        if (size[stage] == 0)
        {
            continue;
        }

        p_table[stage]->idx_mask = new_index[stage];
        p_table[stage]->offset = offset[stage];
        p_table[stage]->sram_index = stage;
    }

    return CTC_E_NONE;
}

/* build lpm hard index */
STATIC int32
_sys_goldengate_lpm_reload_asic_table(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_lpm_tbl_t** p_table, uint8* index, uint8* old_mask, sys_wb_lpm_info_t* lpm_info)
{
    int8 stage;
    uint32 offset[LPM_TABLE_MAX] = {0};
    uint16 size[LPM_TABLE_MAX];
    int32 ret;

    SYS_IPUC_DBG_FUNC();

    /* Here, must alloc big stage size first, or may alloc fail when do delete */
    size[LPM_TABLE_INDEX0] = 0;
    size[LPM_TABLE_INDEX1] = 0;

    for (stage = LPM_TABLE_INDEX1; stage >= (int8)LPM_TABLE_INDEX0; stage--)
    {
        if (!p_table[stage])
        {
            continue;
        }

        if (p_table[stage]->count > 1)
        {
            if (p_table[stage]->offset == INVALID_POINTER_OFFSET ||
                p_table[stage]->sram_index != stage)
            {
                return CTC_E_UNEXPECT;
            }

            continue;
        }

        p_table[stage]->idx_mask = lpm_info->idx_mask[stage];
        p_table[stage]->offset = lpm_info->offset[stage];
        p_table[stage]->sram_index = stage;

        offset[stage] = lpm_info->offset[stage];
        LPM_INDEX_TO_SIZE(p_table[stage]->idx_mask, size[stage]);
    }

    /* alloc the SRAM */
    if ((size[LPM_TABLE_INDEX0] > 0) && (size[LPM_TABLE_INDEX1] > 0))
    {
        if (size[LPM_TABLE_INDEX0] > size[LPM_TABLE_INDEX1])
        {
            CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], offset[LPM_TABLE_INDEX0]));
            ret = _sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], offset[LPM_TABLE_INDEX1]);
            if (ret)
            {
                _sys_goldengate_lpm_offset_free(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], offset[LPM_TABLE_INDEX0]);
                return ret;
            }
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], offset[LPM_TABLE_INDEX1]));
            ret = _sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], offset[LPM_TABLE_INDEX0]);
            if (ret)
            {
                _sys_goldengate_lpm_offset_free(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], offset[LPM_TABLE_INDEX1]);
                return ret;
            }
        }
    }
    else if (size[LPM_TABLE_INDEX0] > 0)
    {
        CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX0, size[LPM_TABLE_INDEX0], offset[LPM_TABLE_INDEX0]));
    }
    else if (size[LPM_TABLE_INDEX1] > 0)
    {
        CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_alloc_from_position(lchip, LPM_TABLE_INDEX1, size[LPM_TABLE_INDEX1], offset[LPM_TABLE_INDEX1]));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_update_nexthop(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};

    SYS_IPUC_DBG_FUNC();
    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_table[LPM_TABLE_INDEX0] = &p_hash->n_table;
    p_table[LPM_TABLE_INDEX1] = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_lpm_update_soft(lchip, p_ipuc_info, p_table));

    return CTC_E_NONE;
}

char*
_sys_goldengate_lpm_get_item_flag(uint32 flag, char* flag_str)
{
    sal_memset(flag_str, 0, sal_strlen(flag_str));

    if (!flag)
    {
        return "N";
    }

    if (CTC_FLAG_ISSET(flag, LPM_ITEM_FLAG_REAL))
    {
        sal_strncat(flag_str, "R", 1);
    }

    if (CTC_FLAG_ISSET(flag, LPM_ITEM_FLAG_PARENT))
    {
        sal_strncat(flag_str, "P", 1);
    }

    if (CTC_FLAG_ISSET(flag, LPM_ITEM_FLAG_VALID))
    {
        sal_strncat(flag_str, "V", 1);
    }

    return flag_str;
}

int32
sys_goldengate_lpm_show_lpm_tree(sys_l3_hash_t* p_lpm_prefix, uint8 stage, sys_lpm_stats_t *p_stats, uint8 detail)
{
    uint8 pointer_ct = 0;
    uint8 tbl1_idx0 = 0;
    uint8 tbl1_idx1 = 0;
    uint8 tbl1_no = 0;
    uint8 item_idx = 0;
    uint8 stage_ip = 0;
    uint8 first_print = 1;
    char flag_str[16] = {0};
    uint8 table_ct[LPM_TABLE_MAX] = {0};
    uint16 item_ct[LPM_TABLE_MAX] = {0};
    uint16 real_ct[LPM_TABLE_MAX] = {0};
    uint16 valid_ct[LPM_TABLE_MAX] = {0};
    sys_lpm_item_t *item_head = NULL;
    sys_lpm_item_t *item_cur = NULL;
    sys_lpm_tbl_t* n0_table = NULL;
    char prefix_str[64] = {0};

    if (CTC_IP_VER_4 == p_lpm_prefix->ip_ver)
    {
        if (p_lpm_prefix->masklen == 8)
        {
            sal_sprintf(prefix_str, "%d.", (p_lpm_prefix->ip32[0] >> 24) & 0xFF);
        }
        else
        {
            sal_sprintf(prefix_str, "%d.%d.", (p_lpm_prefix->ip32[0] >> 24) & 0xFF,  (p_lpm_prefix->ip32[0] >> 16) & 0xFF);
        }
    }
    else
    {
        sal_sprintf(prefix_str, "%x:%x:%x:", (p_lpm_prefix->ip32[0] >> 16) & 0xFFFF,  p_lpm_prefix->ip32[0] & 0xFFFF,
            (p_lpm_prefix->ip32[1] >> 16) & 0xFFFF);
    }

    if (detail)
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n-------------------------prefix %s---------------------\n", prefix_str);
    }

    n0_table = &p_lpm_prefix->n_table;
    if (!n0_table)
    {
        if (detail)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\ndb usage\n--tbl_count: %-3d\n--node_count: %-3d (real: %d)\n", 0, 0, 0);
        }
        return CTC_E_NONE;
    }

    if (stage != LPM_TABLE_INDEX1)
    {
        table_ct[LPM_TABLE_INDEX0]++;
        if (detail)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------pipeline 0------------\nstage0 info\n--pointer count: %d\n--lpmkey index: %-4d\n--compress_index: 0x%02x\n\nentry list\n",
                    n0_table->count, n0_table->offset, n0_table->idx_mask);
        }

        for (item_idx = 0; item_idx < LPM_ITEM_ALLOC_NUM; item_idx++)
        {
            if (!n0_table->p_item[item_idx])
            {
                continue;
            }

            item_head = n0_table->p_item[item_idx];
            while(item_head)
            {
                item_cur = item_head;
                item_head = item_head->p_nitem;

                if (detail == 2)
                {
                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--node[%03d] mask: %d flag: %-3s ad_index: %-4d\n", item_cur->index, item_cur->t_masklen,
                        _sys_goldengate_lpm_get_item_flag(item_cur->item_flag, flag_str), item_cur->ad_index);
                }

                if (CTC_FLAG_ISSET(item_cur->item_flag, LPM_ITEM_FLAG_VALID))
                {
                    valid_ct[LPM_TABLE_INDEX0]++;
                }

                if (CTC_FLAG_ISSET(item_cur->item_flag, LPM_ITEM_FLAG_REAL))
                {
                    if (detail)
                    {
                        real_ct[LPM_TABLE_INDEX0]++;
                        if (p_lpm_prefix->ip_ver == CTC_IP_VER_4)
                        {
                            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%2d.    %s%d/%d    \tad: %d\n", real_ct[LPM_TABLE_INDEX0], prefix_str, ((item_cur->index - GET_BASE(item_cur->t_masklen)) << (8 - item_cur->t_masklen)),
                                    item_cur->t_masklen + p_lpm_prefix->masklen, item_cur->ad_index);
                        }
                        else
                        {
                            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%2d.    %s%x::/%d    \tad: %d\n", real_ct[LPM_TABLE_INDEX0], prefix_str, ((item_cur->index - GET_BASE(item_cur->t_masklen)) << (8 - item_cur->t_masklen)),
                                    item_cur->t_masklen + p_lpm_prefix->masklen, item_cur->ad_index);
                        }
                    }
                }
                item_ct[LPM_TABLE_INDEX0]++;
            }
        }

        if (detail)
        {
            if (!real_ct[LPM_TABLE_INDEX0])
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  NULL\n");
            }
        }

        if (n0_table->count)
        {
            if (detail)
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\npointer list\n");
            }

            for (tbl1_idx0 = 0; tbl1_idx0 < LPM_TABLE_ALLOC_NUM; tbl1_idx0++)
            {
                if (!n0_table->p_ntable[tbl1_idx0])
                {
                    continue;
                }

                for (tbl1_idx1 = 0; tbl1_idx1 < LPM_TABLE_BLOCK_NUM; tbl1_idx1++)
                {
                    if (!n0_table->p_ntable[tbl1_idx0][tbl1_idx1])
                    {
                        continue;
                    }

                    if (detail)
                    {
                        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%2d.    %s%d.*\n", ++pointer_ct, prefix_str, tbl1_idx0 * LPM_TABLE_BLOCK_NUM + tbl1_idx1);
                    }
                }
            }
        }

        if (detail)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\ndb usage\n--tbl_count: %-3d\n--node_count: %-3d (real: %d, valid: %d)\n",
                n0_table->count, item_ct[LPM_TABLE_INDEX0], real_ct[LPM_TABLE_INDEX0], valid_ct[LPM_TABLE_INDEX0]);
        }
    }

    if (stage != LPM_TABLE_INDEX0)
    {
        if (detail)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------pipeline 1------------\n");
        }

        if (stage == LPM_TABLE_INDEX1)
        {
            if (CTC_IP_VER_4 == p_lpm_prefix->ip_ver)
            {
                stage_ip = (p_lpm_prefix->ip32[0] >> (32 - p_lpm_prefix->masklen - 8)) & 0xFF;
            }
            else
            {
                stage_ip = (p_lpm_prefix->ip32[1] >> (64 - p_lpm_prefix->masklen - 8)) & 0xFF;
            }
        }

        for (tbl1_idx0 = 0; tbl1_idx0 < LPM_TABLE_ALLOC_NUM; tbl1_idx0++)
        {
            if (!n0_table->p_ntable[tbl1_idx0])
            {
                continue;
            }

            for (tbl1_idx1 = 0; tbl1_idx1 < LPM_TABLE_BLOCK_NUM; tbl1_idx1++)
            {
                if (!n0_table->p_ntable[tbl1_idx0][tbl1_idx1])
                {
                    continue;
                }

                table_ct[LPM_TABLE_INDEX1]++;
                if ((stage == LPM_TABLE_INDEX1) && ((tbl1_idx0 * LPM_TABLE_ALLOC_NUM + tbl1_idx1) != stage_ip))
                {
                    continue;
                }
                stage_ip = tbl1_idx0 * LPM_TABLE_ALLOC_NUM + tbl1_idx1;

                if (detail)
                {
                    if (first_print)
                    {
                        first_print = 0;
                    }
                    else
                    {
                        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n------------------------\n");
                    }

                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "stage1.%d info\n--segment ip: %d\n--lpmkey index: %-4d\n--makslen: 0x%02x\n\nentry list\n", ++tbl1_no, stage_ip,
                                                            n0_table->p_ntable[tbl1_idx0][tbl1_idx1]->offset, n0_table->p_ntable[tbl1_idx0][tbl1_idx1]->idx_mask);
                }

                for (item_idx = 0; item_idx < LPM_ITEM_ALLOC_NUM; item_idx++)
                {
                    item_head = n0_table->p_ntable[tbl1_idx0][tbl1_idx1]->p_item[item_idx];
                    if (!item_head)
                    {
                        continue;
                    }

                    while(item_head)
                    {
                        item_cur = item_head;
                        item_head = item_head->p_nitem;

                        if (detail == 2)
                        {
                            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "--node[%03d] mask: %d flag: %-3s ad_index: %-4d\n", item_cur->index, item_cur->t_masklen,
                                _sys_goldengate_lpm_get_item_flag(item_cur->item_flag, flag_str), item_cur->ad_index);
                        }

                        if (CTC_FLAG_ISSET(item_cur->item_flag, LPM_ITEM_FLAG_VALID))
                        {
                            valid_ct[LPM_TABLE_INDEX1]++;
                        }

                        if (CTC_FLAG_ISSET(item_cur->item_flag, LPM_ITEM_FLAG_REAL))
                        {
                            if (detail)
                            {
                                if (p_lpm_prefix->ip_ver == CTC_IP_VER_4)
                                {
                                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%2d.    %s%d.%d/%d\tad: %d\n", real_ct[LPM_TABLE_INDEX1], prefix_str, stage_ip,
                                            ((item_cur->index - GET_BASE(item_cur->t_masklen)) << (8 - item_cur->t_masklen)), item_cur->t_masklen + 8 + p_lpm_prefix->masklen, item_cur->ad_index);
                                }
                                else
                                {
                                    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%2d.    %s%x%x::/%d\tad: %d\n", real_ct[LPM_TABLE_INDEX1], prefix_str, stage_ip,
                                            ((item_cur->index - GET_BASE(item_cur->t_masklen)) << (8 - item_cur->t_masklen)), item_cur->t_masklen + 8 + p_lpm_prefix->masklen, item_cur->ad_index);
                                }
                            }
                            real_ct[LPM_TABLE_INDEX1]++;
                        }
                        item_ct[1]++;
                    }
                }
            }
        }

        if (detail)
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\ndb usage\n--tbl_count: 0\n--node_count: %-3d (real: %d, valid: %d)\n", item_ct[LPM_TABLE_INDEX1], real_ct[LPM_TABLE_INDEX1], valid_ct[LPM_TABLE_INDEX1]);
        }
    }

    if (p_stats)
    {
        sal_memcpy(p_stats->table_ct, table_ct, sizeof(uint8) * LPM_TABLE_MAX);
        sal_memcpy(p_stats->item_ct, item_ct, sizeof(uint16) * LPM_TABLE_MAX);
        sal_memcpy(p_stats->real_ct, real_ct, sizeof(uint16) * LPM_TABLE_MAX);
        sal_memcpy(p_stats->valid_ct, valid_ct, sizeof(uint16) * LPM_TABLE_MAX);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_show_lpm_tree_fn(sys_l3_hash_t* p_lpm_prefix, void *data)
{
    sys_lpm_stats_t lpm_stats;
    sys_lpm_traverse_t* travs = (sys_lpm_traverse_t*)data;
    uint8 lchip = travs->lchip;
    uint16 detail = travs->value0;
    uint16 vrf_id = travs->value1;
    uint32* all_tbl_ct = travs->data;
    uint32* all_item_ct = travs->data1;
    uint32* all_real_item_ct = travs->data2;
    uint32* all_valid_ct = travs->data3;

    sal_memset(&lpm_stats, 0, sizeof(sys_lpm_stats_t));

    if ((vrf_id <= p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4]-1) && (p_lpm_prefix->vrf_id != vrf_id))
    {
        return CTC_E_NONE;
    }

    travs->value2++;

    CTC_ERROR_RETURN(sys_goldengate_lpm_show_lpm_tree(p_lpm_prefix, LPM_TABLE_MAX, &lpm_stats, detail));

    all_tbl_ct[LPM_TABLE_INDEX0] += lpm_stats.table_ct[LPM_TABLE_INDEX0];
    all_tbl_ct[LPM_TABLE_INDEX1] += lpm_stats.table_ct[LPM_TABLE_INDEX1];
    all_item_ct[LPM_TABLE_INDEX0] += lpm_stats.item_ct[LPM_TABLE_INDEX0];
    all_item_ct[LPM_TABLE_INDEX1] += lpm_stats.item_ct[LPM_TABLE_INDEX1];
    all_real_item_ct[LPM_TABLE_INDEX0] += lpm_stats.real_ct[LPM_TABLE_INDEX0];
    all_real_item_ct[LPM_TABLE_INDEX1] += lpm_stats.real_ct[LPM_TABLE_INDEX1];
    all_valid_ct[LPM_TABLE_INDEX0] += lpm_stats.valid_ct[LPM_TABLE_INDEX0];
    all_valid_ct[LPM_TABLE_INDEX1] += lpm_stats.valid_ct[LPM_TABLE_INDEX1];

    return CTC_E_NONE;
}

int32
sys_goldengate_lpm_db_show(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint32 detail)
{
    uint8 stage = 0;
    uint8 prefix_len = 0;
    uint16 prefix_count = 0;
    char  vrfid_str[8] = {0};
    uint32 db_total[3] = {0};
    uint32 all_tbl_ct[2] = {0};
    uint32 all_item_ct[2] = {0};
    uint32 all_real_item_ct[2] = {0};
    uint32 all_valid_ct[2] = {0};
    sys_lpm_traverse_t travs = {0};
    sys_lpm_param_t lpm_param = {0};
    sys_lpm_param_t *p_lpm_param = &lpm_param;

    LCHIP_CHECK(lchip);
    p_lpm_param->p_ipuc_param = p_ipuc_param;

    if (p_ipuc_param->masklen)
    {
        sys_ipuc_info_t ipuc_info = {0};
        SYS_IPUC_KEY_MAP(p_ipuc_param, &ipuc_info);
        SYS_IPUC_DATA_MAP(p_ipuc_param, &ipuc_info);
        _sys_goldengate_l3_hash_get_hash_tbl(lchip, &ipuc_info, &p_lpm_param->p_lpm_prefix);
        if (!p_lpm_param->p_lpm_prefix)
        {
            return CTC_E_NOT_EXIST;
        }

        if (CTC_IP_VER_4 == p_ipuc_param->ip_ver)
        {
            p_lpm_param->p_lpm_prefix->ip32[0] = p_ipuc_param->ip.ipv4;
        }
        else
        {
            p_lpm_param->p_lpm_prefix->ip32[0] = p_ipuc_param->ip.ipv6[3];
            p_lpm_param->p_lpm_prefix->ip32[1] = p_ipuc_param->ip.ipv6[2];
        }

        stage = (p_ipuc_param->masklen - p_lpm_param->p_lpm_prefix->masklen < 8) ?  LPM_TABLE_INDEX0 : LPM_TABLE_INDEX1;
        sys_goldengate_lpm_show_lpm_tree(p_lpm_param->p_lpm_prefix, stage, NULL, detail);
    }
    else
    {
        travs.lchip = lchip;
        travs.value0 = detail;
        travs.value1 = p_ipuc_param->vrf_id;
        travs.data = (void*)all_tbl_ct;
        travs.data1 = (void*)all_item_ct;
        travs.data2 = (void*)all_real_item_ct;
        travs.data3 = (void*)all_valid_ct;

        sal_sprintf(vrfid_str, "%d", p_ipuc_param->vrf_id);

        prefix_count = p_gg_l3hash_master[lchip]->l3_hash[p_ipuc_param->ip_ver]->count;
        prefix_len = (p_ipuc_param->ip_ver == CTC_IP_VER_4) ? p_gg_ipuc_master[lchip]->masklen_l : p_gg_ipuc_master[lchip]->masklen_ipv6_l;

        _sys_goldengate_lpm_traverse_prefix(lchip, p_ipuc_param->ip_ver, (hash_traversal_fn)_sys_goldengate_lpm_show_lpm_tree_fn, (void*)&travs);

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------lpm softdb info---------------------------------\n");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ip version: %-6s         vrf id: %s\n", (p_ipuc_param->ip_ver == CTC_IP_VER_4) ? "IPv4" : "IPv6", (p_ipuc_param->vrf_id > p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4]-1) ? "all vrf" : vrfid_str);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "prefix length: %-2d          pipeline level: 2\n", prefix_len);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "total prefix: %-8d     vrf prefix: %-8d       prefix db_size: %d\n", prefix_count, travs.value2, (uint32)sizeof(sys_l3_hash_t));
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "pipeline0 db_size: %-3d     pipeline1 db_size: %-3d     node db_size: %d\n", (uint32)sizeof(sys_lpm_tbl_t), (uint32)(sizeof(sys_lpm_tbl_t) - sizeof(sys_lpm_tbl_t *) * LPM_TABLE_ALLOC_NUM), (uint32)sizeof(sys_lpm_item_t));
        db_total[0] = all_tbl_ct[0] * sizeof(sys_lpm_tbl_t) + all_item_ct[0] * sizeof(sys_lpm_item_t);
        db_total[1] = all_tbl_ct[1] * CTC_OFFSET_OF(sys_lpm_tbl_t, p_ntable) + all_item_ct[1] * sizeof(sys_lpm_item_t);
        db_total[2] = db_total[0] + db_total[1] + prefix_count * sizeof(sys_l3_hash_t);

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nall_db_stats:  %-4uB\n", db_total[2]);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------lpm stats------------------\n");
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
            "pipeline 0\n--all_tbl_count: %-3d\n--all_node_count: %-3d (real: %d, valid: %d)\n--all_db_stats: %-4uB\npipeline 1\n--all_tbl_count: %-3d\n--all_node_count: %-3d (real: %d, valid: %d)\n--all_db_stats: %-4uB\n\n",
            all_tbl_ct[0], all_item_ct[0], all_real_item_ct[0], all_valid_ct[0], db_total[0], all_tbl_ct[1], all_item_ct[1], all_real_item_ct[1], all_valid_ct[1], db_total[1]);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_wb_lpm_info_t* lpm_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};
    uint8 new_index[LPM_TABLE_MAX] = {0};
    uint8 old_mask[LPM_TABLE_MAX] = {0xFF, 0xFF};
    int32 ret = 0;
    uint16 size = 0;
    uint16 i = 0;
    sys_lpm_result_t* p_lpm_result = NULL;

    SYS_IPUC_DBG_FUNC();
    p_lpm_result = (sys_lpm_result_t*)p_ipuc_info->lpm_result;

    /* do add*/
    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_table[LPM_TABLE_INDEX0] = &p_hash->n_table;
    p_table[LPM_TABLE_INDEX1] = NULL;

    /* add lpm soft db */
    ret = _sys_goldengate_lpm_add_soft(lchip, p_ipuc_info, p_table);

    if (lpm_info)
    {
       return  _sys_goldengate_lpm_reload_asic_table(lchip, p_ipuc_info, p_table, new_index, old_mask, lpm_info);
    }

    /* build lpm hard index */
    ret = ret ? ret : _sys_goldengate_lpm_calculate_asic_table(lchip, p_ipuc_info, p_table, new_index, old_mask);
    if (ret)
    {
        _sys_goldengate_lpm_del_soft(lchip, p_ipuc_info, p_table);

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
                                 LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, i) << POINTER_OFFSET_BITS_LEN) & INVALID_POINTER))
        {
            LPM_INDEX_TO_SIZE(LPM_RLT_GET_IDX_MASK(p_ipuc_info, p_lpm_result, i), size);
            _sys_goldengate_lpm_offset_free(lchip, LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, i),
                                           size, LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, i));
            LPM_RLT_INIT_POINTER_STAGE(p_ipuc_info, p_lpm_result, i);
        }
    }

    return CTC_E_NONE;
}

/* remove lpm soft db */
int32
_sys_goldengate_lpm_del_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, bool redo)
{
    uint8 old_index[LPM_TABLE_MAX];
    uint8 new_index[LPM_TABLE_MAX] = {0xFF, 0xFF};
    uint8 tmp_mask[LPM_TABLE_MAX] = {0};
    uint32 old_pointer[LPM_TABLE_MAX] = {0};
    uint8 stage = 0;
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

        old_pointer[stage] = p_table[stage]->offset | (p_table[stage]->sram_index << POINTER_OFFSET_BITS_LEN);
    }

    _sys_goldengate_lpm_del_soft(lchip, p_ipuc_info, p_table);

    ret = _sys_goldengate_lpm_calculate_asic_table(lchip, p_ipuc_info, p_table, new_index, tmp_mask);
    if (ret)
    {
        _sys_goldengate_lpm_add_soft(lchip, p_ipuc_info, p_table);

        for (stage = LPM_TABLE_INDEX0; stage < LPM_TABLE_MAX; stage++)
        {
            if (INVALID_POINTER != (old_pointer[stage] & INVALID_POINTER))
            {
                p_table[stage]->offset = old_pointer[stage] & POINTER_OFFSET_MASK;
                p_table[stage]->sram_index = old_pointer[stage] >> POINTER_OFFSET_BITS_LEN;
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
            CTC_ERROR_RETURN(_sys_goldengate_lpm_offset_free(lchip, LPM_RLT_GET_SRAM_IDX(p_ipuc_info, p_lpm_result, stage), size,
                                                            LPM_RLT_GET_OFFSET(p_ipuc_info, p_lpm_result, stage)));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_del(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;
    uint32 new_pointer = INVALID_POINTER;
    sys_lpm_tbl_t* p_table = NULL;
    uint8 i = 0;
    uint8 sram_index = 0;

    SYS_IPUC_DBG_FUNC();
    if (((p_ipuc_info->masklen <= p_gg_ipuc_master[lchip]->masklen_ipv6_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6))
        || ((p_ipuc_info->masklen <= p_gg_ipuc_master[lchip]->masklen_l) && (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4))
        || (p_ipuc_info->l4_dst_port > 0))
    {
        return CTC_E_NONE;
    }

    /* remove lpm soft db and hard index */
    ret = _sys_goldengate_lpm_del_ex(lchip, p_ipuc_info, FALSE);
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
            sram_index = p_table->sram_index & POINTER_STAGE_MASK;
            new_pointer = _sys_goldengate_lpm_get_rsv_offset(lchip, sram_index);
            new_pointer += (sram_index << POINTER_OFFSET_BITS_LEN);

            CTC_ERROR_RETURN(_sys_goldengate_lpm_update(lchip, p_ipuc_info, i, new_pointer,
                                                            (p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN))));
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_lpm_del_ex(lchip, p_ipuc_info, TRUE));

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_init(uint8 lchip, ctc_ip_ver_t ip_version)
{
    sys_goldengate_opf_t opf;

    if (NULL == p_gg_lpm_master[lchip])
    {
        p_gg_lpm_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_lpm_master_t));

        if (NULL == p_gg_lpm_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gg_lpm_master[lchip], 0, sizeof(sys_lpm_master_t));
    }

    /* already inited */
    if (p_gg_lpm_master[lchip]->version_en[ip_version])
    {
        return CTC_E_NONE;
    }

    if (!p_gg_lpm_master[lchip]->version_en[CTC_IP_VER_4] && !p_gg_lpm_master[lchip]->version_en[CTC_IP_VER_6])
    {
        sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmLookupKey_t,
                                              &(p_gg_lpm_master[lchip]->lpm_lookup_key_table_size));

        if ((p_gg_lpm_master[lchip]->lpm_lookup_key_table_size) <= (LPM_SRAM_RSV_FOR_ARRANGE * 2))
        {
            mem_free(p_gg_lpm_master[lchip]);
            return CTC_E_NO_RESOURCE;
        }

        sys_goldengate_opf_init(lchip, OPF_LPM_SRAM0, 1);

        /*init opf for LPM*/
        /* LPM_SRAM_RSV_FOR_ARRANGE reserve for arrange */
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_index = 0;
        opf.pool_type = OPF_LPM_SRAM0;
        sys_goldengate_opf_init_offset(lchip, &opf, 0,
                                      (p_gg_lpm_master[lchip]->lpm_lookup_key_table_size - (LPM_SRAM_RSV_FOR_ARRANGE * 2)));
    }

    p_gg_lpm_master[lchip]->version_en[ip_version] = TRUE;
    p_gg_lpm_master[lchip]->dma_enable = FALSE;

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_lpm_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (!p_gg_lpm_master[lchip]->version_en[CTC_IP_VER_4] && !p_gg_lpm_master[lchip]->version_en[CTC_IP_VER_6])
    {
        sys_goldengate_opf_deinit(lchip, OPF_LPM_SRAM0);
    }

    mem_free(p_gg_lpm_master[lchip]);

    return CTC_E_NONE;
}


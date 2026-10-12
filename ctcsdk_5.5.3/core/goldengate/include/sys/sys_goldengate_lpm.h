/**
 @file sys_goldengate_lpm.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_LPM_H
#define _SYS_GOLDENGATE_LPM_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"

#include "ctc_port.h"
#include "ctc_const.h"
#include "sys_goldengate_ipuc.h"
#include "sys_goldengate_wb_common.h"


/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/* hash table size */
#define LPM_ITEM_NUM                        512
#define LPM_MASK_LEN                        8
#define LPM_TBL_NUM                         256
#define LPM_SRAM_RSV_FOR_ARRANGE            256
#define LPM_ITEM_ALLOC_NUM                  16
#define LPM_TABLE_ALLOC_NUM                 8
#define LPM_TABLE_BLOCK_NUM                 32      /*LPM_TBL_NUM/LPM_TABLE_ALLOC_NUM*/
#define DS_LPM_PREFIX_MAX_INDEX   512

/*a compressed table entry */
#define LPM_COMPRESSED_BLOCK_SIZE           2

#define SYS_LPM_ITEM_OFFSET(IDX, MSK_LEN)                                            \
    (((IDX)&GET_MASK(MSK_LEN)) >> (LPM_MASK_LEN - MSK_LEN))

#define SYS_LPM_GET_ITEM_IDX(IDX, MSK_LEN)                                            \
    (GET_BASE(MSK_LEN) + SYS_LPM_ITEM_OFFSET(IDX, MSK_LEN))

#define SYS_LPM_GET_PARENT_ITEM_IDX(IDX, MSK_LEN)                                            \
    (GET_BASE(MSK_LEN-1) + SYS_LPM_ITEM_OFFSET(IDX, MSK_LEN)/2)

#define LPM_RLT_INIT_POINTER(ipuc_ptr, ptr)                  \
    {                                                           \
        ptr->old_pointer0.offset = INVALID_POINTER_OFFSET;      \
        ptr->old_pointer0.stage_sram = 0xFF;                    \
        ptr->old_pointer0.idx_mask = 0xFF;                      \
                                                            \
        ptr->old_pointer1.offset = INVALID_POINTER_OFFSET;      \
        ptr->old_pointer1.stage_sram = 0xFF;                    \
        ptr->old_pointer1.idx_mask = 0xFF;                      \
    }

#define LPM_INDEX_TO_SIZE(I, S)     \
    {                                   \
        uint8 N = 0;                    \
        uint8 T = (I);                  \
        while (T)                        \
        {                               \
            T &= (T - 1);               \
            N++;                        \
        }                               \
        (S) = (1 << N);                 \
    }

#define LPM_INDEX_TO_BITNUM(I, N)   \
    {                                   \
        uint8 T = (I);                  \
        (N) = 0;                        \
        while (T)                        \
        {                               \
            T &= (T - 1);               \
            (N)++;                      \
        }                               \
    }

#define GET_MASK(ln)                        \
    (                                           \
        ((ln) == 0) ? 0x00 : ((ln) == 1) ? 0x80 :      \
        ((ln) == 2) ? 0xc0 : ((ln) == 3) ? 0xe0 :      \
        ((ln) == 4) ? 0xf0 : ((ln) == 5) ? 0xf8 :      \
        ((ln) == 6) ? 0xfc : ((ln) == 7) ? 0xfe : 0xff  \
    )

#define GET_MASK_MAX(ln)                    \
    (                                           \
        ((ln) == 0) ? 0x00 : ((ln) == 1) ? 0x01 :      \
        ((ln) == 2) ? 0x03 : ((ln) == 3) ? 0x07 :      \
        ((ln) == 4) ? 0x0f : ((ln) == 5) ? 0x1f :      \
        ((ln) == 6) ? 0x3f : ((ln) == 7) ? 0x7f : 0xff  \
    )

/* the base value of ip tree. Every masklen have a base. For masklen 8, it is at the leaf layer, the base is 0 */
#define GET_BASE(ln)                        \
    (                                           \
        ((ln) == 0) ? 510 : ((ln) == 1) ? 508 :        \
        ((ln) == 2) ? 504 : ((ln) == 3) ? 496 :        \
        ((ln) == 4) ? 480 : ((ln) == 5) ? 448 :        \
        ((ln) == 6) ? 384 : ((ln) == 7) ? 256 : 0       \
    )

#define GET_INDEX_MASK_OFFSET(ln)       \
    (                                       \
        ((ln) == 0) ? 0 : ((ln) == 1) ? 1 :        \
        ((ln) == 2) ? 9 : ((ln) == 3) ? 37 :       \
        ((ln) == 4) ? 93 : ((ln) == 5) ? 163 :     \
        ((ln) == 6) ? 219 : ((ln) == 7) ? 247 : 255 \
    )

#define ITEM_GET_OR_INIT_PTR(ptr_o, idx, ptr, type)                            \
    {                                                                           \
        ptr = ptr_o[(idx / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM))];                   \
        while (ptr)                                                              \
        {                                                                       \
            if (idx == ptr->index)                                             \
            {                                                                   \
                break;                                                          \
            }                                                                   \
            ptr = ptr->p_nitem;                                                 \
        }                                                                       \
        if (NULL == ptr)                                                         \
        {                                                                       \
            if (type == LPM_ITEM_TYPE_V6)                                        \
            {                                                                   \
                ptr = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_lpm_item_t));       \
                if (NULL == ptr)                                                 \
                {                                                               \
                    return CTC_E_NO_MEMORY;                                     \
                }                                                               \
                sal_memset(ptr, 0, sizeof(sys_lpm_item_t));                       \
            }                                                                   \
            else if (type == LPM_ITEM_TYPE_V4)                                  \
            {                                                                   \
                ptr = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_lpm_item_v4_t));   \
                if (NULL == ptr)                                                 \
                {                                                               \
                    return CTC_E_NO_MEMORY;                                     \
                }                                                               \
                sal_memset(ptr, 0, sizeof(sys_lpm_item_v4_t));                    \
            }                                                                   \
            else                                                                \
            {                                                                   \
                ptr = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_lpm_item_none_t)); \
                if (NULL == ptr)                                                 \
                {                                                               \
                    return CTC_E_NO_MEMORY;                                     \
                }                                                               \
                sal_memset(ptr, 0, sizeof(sys_lpm_item_none_t));                  \
            }                                                                   \
            ptr->ad_index = INVALID_NEXTHOP;                                    \
            ptr->p_nitem = ptr_o[(idx / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM))];      \
            ptr->index = idx;                                                   \
            ptr_o[(idx / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM))] = ptr;               \
        }                                                                       \
    }

#define ITEM_GET_PTR(ptr_o, idx, ptr)                                         \
    {                                                                           \
        ptr = ptr_o[(idx / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM))];                   \
        while (ptr)                                                              \
        {                                                                       \
            if (idx == ptr->index)                                             \
            {                                                                   \
                break;                                                          \
            }                                                                   \
            ptr = ptr->p_nitem;                                                 \
        }                                                                       \
    }

#define ITEM_FREE_PTR(ptr_o, ptr)                                            \
    {                                                                           \
        sys_lpm_item_t* p_tmp =                                                 \
            ptr_o[(ptr->index) / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM)];          \
        if (ptr_o[(ptr->index) / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM)] == ptr)        \
        {                                                                       \
            ptr_o[(ptr->index) / (LPM_ITEM_NUM / LPM_ITEM_ALLOC_NUM)]               \
                = ptr->p_nitem;                                                 \
            p_tmp = NULL;                                                       \
        }                                                                       \
        while (p_tmp)                                                            \
        {                                                                       \
            if (p_tmp->p_nitem == ptr){                                           \
                break; }                                                          \
            p_tmp = p_tmp->p_nitem;                                             \
        }                                                                       \
        if (p_tmp)                                                               \
        {                                                                       \
            p_tmp->p_nitem = ptr->p_nitem;                                      \
        }                                                                       \
        mem_free(ptr);                                                          \
    }

#define TABLE_SET_PTR(ptr_o, idx, ptr)                                                                    \
    {                                                                                                       \
        if (ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))] == NULL)                                     \
        {                                                                                                   \
            ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))] =                                              \
                mem_malloc(MEM_IPUC_MODULE, (sizeof(sys_lpm_tbl_t*) * (LPM_TABLE_BLOCK_NUM)));   \
            sal_memset(ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))], 0                                 \
                       , (sizeof(sys_lpm_tbl_t*) * (LPM_TABLE_BLOCK_NUM)));                    \
        }                                                                                                   \
        ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))][(idx) % (LPM_TABLE_BLOCK_NUM)] = ptr;   \
    }

#define TABLE_GET_PTR(ptr_o, idx, ptr)                                                                    \
    {                                                                                                       \
        if ((ptr_o == NULL) || CTC_FLAG_ISSET(ptr_o->tbl_flag, LPM_TBL_FLAG_NO_NEXT)                         \
            || (NULL == ptr_o->p_ntable[((idx) / (LPM_TABLE_BLOCK_NUM))]))                        \
        {                                                                                                   \
            ptr = NULL;                                                                                     \
        }                                                                                                   \
        else                                                                                                \
        {                                                                                                   \
            ptr = ptr_o->p_ntable[((idx) / (LPM_TABLE_BLOCK_NUM))]                                \
                [(idx) % (LPM_TABLE_BLOCK_NUM)];                                              \
        }                                                                                                   \
    }

/* get all table */
#define TABEL_GET_STAGE(I, ipuc_ptr, ptr)                                               \
    {                                                                                   \
        uint32 IP;                                                                      \
        uint8 idx;                                                                      \
        uint8 SHIFT = 0;                                                                \
        uint8 STAGE = I;                                                                \
        uint8 masklen = 0;                                                              \
        sys_l3_hash_t* p_hash;                                                          \
        ptr = NULL;                                                                     \
                                                                                        \
        if (CTC_IP_VER_4 == SYS_IPUC_VER(ipuc_ptr))                                     \
        {                                                                               \
            SHIFT = p_gg_ipuc_master[lchip]->use_hash16 ? 1 : 2;                                                                  \
            masklen = ((I * 8) + p_gg_ipuc_master[lchip]->masklen_l);      \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            if (ipuc_ptr->masklen < 40)                                                 \
            {                                                                           \
                masklen = ((I * 8) + 32);                                               \
            }                                                                           \
            else if (ipuc_ptr->masklen < 48)                                            \
            {                                                                           \
                masklen = ((I * 8) + 40);                                               \
            }                                                                           \
            else if (ipuc_ptr->masklen < 65)                                            \
            {                                                                           \
                SHIFT = 1;                                                              \
                masklen = ((I * 8) + 48);                                               \
            }                                                                           \
        }                                                                               \
                                                                                        \
        if (masklen >= (ipuc_ptr->masklen))                                             \
        {                                                                               \
            ptr = NULL;                                                                 \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            _sys_goldengate_l3_hash_get_hash_tbl(lchip, ipuc_ptr, &p_hash);                    \
            (ptr) = &(p_hash->n_table);                                                 \
        }                                                                               \
        IP = (CTC_IP_VER_4 == SYS_IPUC_VER(ipuc_ptr)) ? ipuc_ptr->ip.ipv4[0] :          \
            ipuc_ptr->ip.ipv6[2];                                                       \
        while ((STAGE != 0) && (ptr != NULL))                                           \
        {                                                                               \
            idx = (IP >> (SHIFT * 8)) & 0xFF;                                           \
            TABLE_GET_PTR((ptr), idx, (ptr));                                           \
            STAGE--;                                                                    \
            SHIFT--;                                                                    \
        }                                                                               \
    }

#define TABLE_FREE_PTR(ptr_o, idx)                                                                       \
    {                                                                                                       \
        uint8 IS_FREE = 1;                                                                                  \
        uint8 I = 0;                                                                                        \
        ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))][(idx) % (LPM_TABLE_BLOCK_NUM)] = NULL;   \
        while (I < (LPM_TABLE_BLOCK_NUM))                                                       \
        {                                                                                                   \
            if (ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))][I] != NULL)                               \
            {                                                                                               \
                IS_FREE = 0;                                                                               \
                break;                                                                                      \
            }                                                                                               \
            I++;                                                                                            \
        }                                                                                                   \
        if (IS_FREE)                                                                                         \
        {                                                                                                   \
            mem_free(ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))]);                                    \
            ptr_o[((idx) / (LPM_TABLE_BLOCK_NUM))] = NULL;                    \
        }                                                                                                   \
    }

#define LPM_RLT_INIT_POINTER_STAGE(ipuc_ptr, ptr, stage)                  \
    {                                                                       \
        if ((((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage))          \
        {                                                                   \
            ptr->old_pointer0.stage_sram = 0xFF;                            \
            ptr->old_pointer0.offset = INVALID_POINTER_OFFSET;              \
            ptr->old_pointer0.stage_sram = 0xFF;                            \
            ptr->old_pointer0.idx_mask = 0xFF;                              \
        }                                                                   \
        else if ((((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage))     \
        {                                                                   \
            ptr->old_pointer1.stage_sram = 0xFF;                            \
            ptr->old_pointer1.offset = INVALID_POINTER_OFFSET;              \
            ptr->old_pointer1.stage_sram = 0xFF;                            \
            ptr->old_pointer1.idx_mask = 0xFF;                              \
        }                                                                   \
    }

#define LPM_RLT_GET_OFFSET(ipuc_ptr, ptr, stage)                      \
    (                                                                   \
        (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage) ?         \
        (ptr->old_pointer0.offset) :                                 \
        (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage) ?         \
        (ptr->old_pointer1.offset) : INVALID_POINTER_OFFSET             \
    )

#define LPM_RLT_GET_IDX_MASK(ipuc_ptr, ptr, stage)                     \
    (                                                                   \
        (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage) ?         \
        (0xFF & ptr->old_pointer0.idx_mask) :                       \
        (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage) ?         \
        (0xFF & ptr->old_pointer1.idx_mask) : 0xFF                  \
    )

#define LPM_RLT_GET_SRAM_IDX(ipuc_ptr, ptr, stage)                     \
    (                                                                   \
        (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage) ?         \
        (0x3 & ptr->old_pointer0.stage_sram) :                      \
        (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage) ?         \
        (0x3 & ptr->old_pointer1.stage_sram) : 0x3                  \
    )

#define LPM_RLT_SET_OFFSET(ipuc_ptr, ptr, stage, val)                          \
    {                                                                           \
        if ((((ptr->old_pointer0.stage_sram >> 4) & 0xF) == 0xF) ||              \
            (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer0.stage_sram = stage << 4 |                         \
                (ptr->old_pointer0.stage_sram & 0xF);                       \
            ptr->old_pointer0.offset = val;                                     \
        }                                                                       \
        else if ((((ptr->old_pointer1.stage_sram >> 4) & 0xF) == 0xF) ||         \
                 (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer1.stage_sram = stage << 4 |                          \
                (ptr->old_pointer1.stage_sram & 0xF);                       \
            ptr->old_pointer1.offset = val;                                     \
        }                                                                       \
    }

#define LPM_RLT_SET_SRAM_IDX(ipuc_ptr, ptr, stage, val)                        \
    {                                                                           \
        if ((((ptr->old_pointer0.stage_sram >> 4) & 0xF) == 0xF) ||              \
            (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer0.stage_sram = (stage << 4) | val;                   \
        }                                                                       \
        else if ((((ptr->old_pointer1.stage_sram >> 4) & 0xF) == 0xF) ||         \
                 (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer1.stage_sram = (stage << 4) | val;                   \
        }                                                                       \
    }

#define LPM_RLT_SET_IDX_MASK(ipuc_ptr, ptr, stage, val)                        \
    {                                                                           \
        if ((((ptr->old_pointer0.stage_sram >> 4) & 0xF) == 0xF) ||              \
            (((ptr->old_pointer0.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer0.stage_sram = stage << 4 |                         \
                (ptr->old_pointer0.stage_sram & 0xF);                       \
            ptr->old_pointer0.idx_mask = val;                                   \
        }                                                                       \
        else if ((((ptr->old_pointer1.stage_sram >> 4) & 0xF) == 0xF) ||         \
                 (((ptr->old_pointer1.stage_sram >> 4) & 0xF) == stage))             \
        {                                                                       \
            ptr->old_pointer1.stage_sram = stage << 4 |                          \
                (ptr->old_pointer1.stage_sram & 0xF);                       \
            ptr->old_pointer1.idx_mask = val;                                   \
        }                                                                       \
    }

enum sys_lpm_tree_item_op_type_e
{
    SYS_LPM_TREE_ITEM_OP_TYPE_ADD,
    SYS_LPM_TREE_ITEM_OP_TYPE_DEL,
    SYS_LPM_TREE_ITEM_OP_TYPE_UPDATE,
    SYS_LPM_TREE_ITEM_OP_TYPE_NUM
};
typedef enum sys_lpm_tree_item_op_type_e sys_lpm_tree_item_op_type_t;

enum sys_lpm_key_type_e
{
    LPM_TYPE_NEXTHOP,
    LPM_TYPE_POINTER,
    LPM_TYPE_NUM
};
typedef enum sys_lpm_key_type_e sys_lpm_key_type_t;

enum lpm_sram_table_index_e
{
    LPM_TABLE_INDEX0,
    LPM_TABLE_INDEX1,
    LPM_TABLE_MAX
};
typedef enum lpm_sram_table_index_e lpm_sram_table_index_t;

/* sys_lpm_item_none_t  -> LPM_ITEM_TYPE_NONE   this item is used to organize ipv4 lpm information, and no hash offset is recorded
 * sys_lpm_item_v4_t    -> LPM_ITEM_TYPE_V4     this item is used to record ipv4 hash information
 * sys_lpm_item_t       -> LPM_ITEM_TYPE_V6     this item is used to organize ipv6 lpm information
 * these three types of item have different size, while alloc a new item should declare the type first
 */
enum lpm_item_type_e
{
    LPM_ITEM_TYPE_NONE                  =0x0,
    LPM_ITEM_TYPE_V4                    =0x1,
    LPM_ITEM_TYPE_V6                    =0x1 << 2
};

/* REAL  means the item is the route node, the real node can be invalid when it whole push down
 * VALID  means the item is in use, it can be nor real node
 * PARENT  means the item hash child node
 */
enum calpm_item_flag_e
{
    LPM_ITEM_FLAG_REAL                  =0x1,
    LPM_ITEM_FLAG_VALID                =0x1 << 1,
    LPM_ITEM_FLAG_PARENT             =0x1 << 2
};

enum lpm_tbl_flag_e
{
    LPM_TBL_FLAG_NO_NEXT                =0x1 << 1 /*use to save memory for the leaf node p_ntable[LPM_TABLE_ALLOC_NUM]*/
};

/*===========================item struct start===========================*/
/* if modify this struct DO NOT FORGET to modify the following TWO structs too */
struct sys_lpm_item_s
{
    uint32 ad_index;
    uint16 t_masklen;       /* indicate this item is pushed or not */

    uint16 item_flag;       /*lpm_item_flag_e*/
    uint16 index;

    struct sys_lpm_item_s* p_nitem;
};
typedef struct sys_lpm_item_s sys_lpm_item_t;

/* the same as sys_lpm_item_t , only used to alloc new object */
struct sys_lpm_item_v4_s
{
    uint32 ad_index;
    uint16 t_masklen;

    uint16 item_flag;
    uint16 index;

    struct sys_lpm_item_s* p_nitem;
};
typedef struct sys_lpm_item_v4_s sys_lpm_item_v4_t;

/* the same as sys_lpm_item_t , only used to alloc new object */
struct sys_lpm_item_none_s
{
    uint32 ad_index;
    uint16 t_masklen;

    uint16 item_flag;
    uint16 index;

    struct sys_lpm_item_s* p_nitem;
};
typedef struct sys_lpm_item_none_s sys_lpm_item_none_t;
/*===========================item struct end===========================*/

/*===========================table struct start===========================*/
/* if modify this struct DO NOT FORGET to modify the following struct too */
struct sys_lpm_tbl_s
{
    uint16 count;
    uint8 tbl_flag;     /* lpm_tbl_flag_e */
    uint8 idx_mask;

    uint32 offset;      /*used to store lpm key offset, base addr of the tree */
    uint8 sram_index;   /*used to store lpm sram index*/
    uint8 index;        /* ip8 */
    uint8 rsv[2];

    sys_lpm_item_t* p_item[LPM_ITEM_ALLOC_NUM];     /* tree node, most 512 node for a tree */
    struct sys_lpm_tbl_s** p_ntable[LPM_TABLE_ALLOC_NUM];   /* next table */
};
typedef struct sys_lpm_tbl_s sys_lpm_tbl_t;

/* the same as sys_lpm_tbl_t ,p_table[3] do not have p_ntable , only used to alloc new object */
struct sys_lpm_tbl_end_s
{
    uint16 count;
    uint8 tbl_flag;
    uint8 idx_mask;

    uint32 offset;       /*used to store lpm key offset*/
    uint8 sram_index;    /*used to store lpm sram index*/
    uint8 index;
    uint8 rsv[2];

    sys_lpm_item_t* p_item[LPM_ITEM_ALLOC_NUM];
};
typedef struct sys_lpm_tbl_end_s sys_lpm_tbl_end_t;
/*===========================table struct end===========================*/

struct sys_lpm_pointer_info_s
{
    uint32 offset;      /* lookupkey offset */
    uint8  stage_sram;  /* high 4bits means pipeline stage ; low 4bits means lookupkey sram index */
    uint8  idx_mask;
    uint8  rsv[2];
};
typedef struct sys_lpm_pointer_info_s sys_lpm_pointer_info_t;

/*===========================lpm_result struct start===========================*/
/* if modify this struct DO NOT FORGET to modify the following two structs too */
struct sys_lpm_result_s
{
    sys_lpm_pointer_info_t old_pointer0;    /* store old lookupkey info ipv4 only used 3 block*/
    sys_lpm_pointer_info_t old_pointer1;    /* store old lookupkey info ipv4 only used 3 block*/
};
typedef struct sys_lpm_result_s sys_lpm_result_t;

/* the same as sys_lpm_result_t , only used to alloc new object */
struct sys_lpm_result_v4_s
{
    sys_lpm_pointer_info_t old_pointer0;
    sys_lpm_pointer_info_t old_pointer1;
     /*sys_lpm_pointer_info_t old_pointer2;*/
};
typedef struct sys_lpm_result_v4_s sys_lpm_result_v4_t;
/*===========================lpm_result struct end===========================*/

struct sys_lpm_select_counter_s
{
    uint8 block_base;
    uint8 entry_number;
};
typedef struct sys_lpm_select_counter_s sys_lpm_select_counter_t;

struct ds_lpm_prefix_ex_s
{
    uint32 nexthop1                                                         : 1;
    uint32 type                                                             : 2;
    uint32 rsv_0                                                            : 29;

    uint32 ip                                                               : 8;
    uint32 mask                                                             : 8;
    uint32 nexthop0                                                         : 16;
};
typedef struct ds_lpm_prefix_ex_s ds_lpm_prefix_ex_t;

struct sys_lpm_stats_s
{
    uint8 table_ct[LPM_TABLE_MAX];
    uint16 item_ct[LPM_TABLE_MAX];
    uint16 real_ct[LPM_TABLE_MAX];
    uint16 valid_ct[LPM_TABLE_MAX];
};
typedef struct sys_lpm_stats_s sys_lpm_stats_t;

struct sys_lpm_traverse_s
{
    uint32 value0;
    uint32 value1;
    uint32 value2;
    uint32 value3;
    void* data;
    void* data1;
    void* data2;
    void* data3;
    void* data4;
    void* fn;
    uint8 lchip;
};
typedef struct sys_lpm_traverse_s sys_lpm_traverse_t;

struct sys_lpm_master_s
{
    uint32 alpm_usage[LPM_TABLE_MAX];
    uint32 lpm_lookup_key_table_size;
    ds_lpm_prefix_ex_t ds_lpm_prefix[DS_LPM_PREFIX_MAX_INDEX];

    uint8 version_en[MAX_CTC_IP_VER];
    uint8 dma_enable;
    uint8 rsv0;
};
typedef struct sys_lpm_master_s sys_lpm_master_t;
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

extern int32
_sys_goldengate_lpm_init(uint8 lchip, ctc_ip_ver_t ip_version);

extern int32
_sys_goldengate_lpm_deinit(uint8 lchip);

extern int32
_sys_goldengate_lpm_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_wb_lpm_info_t* lpm_info);

extern int32
_sys_goldengate_lpm_del(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

extern int32
_sys_goldengate_lpm_update(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage, uint32 new_pointer, uint32 old_pointer);

extern int32
_sys_goldengate_lpm_offset_alloc(uint8 lchip, uint8 sram_index, uint32 entry_num, uint32* p_offset);

extern int32
_sys_goldengate_lpm_offset_free(uint8 lchip, uint8 sram_index, uint32 entry_num, uint32 offset);

extern uint32
_sys_goldengate_lpm_get_rsv_offset(uint8 lchip, uint8 sram_index);

extern int32
_sys_goldengate_lpm_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 stage);

extern void
sys_goldengate_lpm_state_show(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif


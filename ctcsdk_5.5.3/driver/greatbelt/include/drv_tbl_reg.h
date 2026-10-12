/**
  @file drv_tbl_reg.h

  @date 2010-07-22

  @version v5.1

  The file implement driver IOCTL defines and macros
*/

#ifndef _DRV_TBL_REG_H_
#define _DRV_TBL_REG_H_

#include "greatbelt/include/drv_common.h"
#include "greatbelt/include/drv_enum.h"

/**********************************************************************************
              Define Typedef, define and Data Structure
***********************************************************************************/
#define DRV_BYTES_PER_ENTRY 12        /* 12 bytes per entry for 80 bits*/
#define DRV_ADDR_BYTES_PER_ENTRY 16   /* 16 bytes address per entry for 80 bits*/
#define DRV_BYTES_PER_WORD 4
#define DRV_BITS_PER_WORD 32
#define DRV_WORDS_PER_ENTRY (DRV_BYTES_PER_ENTRY / DRV_BYTES_PER_WORD)  /* 3 words per entry for 80 bits*/

#define DRV_ENTRYS_PER_TCAM_BLOCK 512
#define DRV_MAX_TCAM_BLOCK_NUM 16
#define DRV_ACL_TCAM_BLOCK_NUM 4 /* blocks reserved for ACL keys */
#define DRV_ACL_TCAM_ENTRY_NUM (DRV_ACL_TCAM_BLOCK_NUM * DRV_ENTRYS_PER_TCAM_BLOCK)

#define DRV_ACL_TCAM_640_OFFSET_IDX  (2 * 1024)

#define MAX_ENTRY_NUMBER   256   /* who wrote this, what's its meaming? */
#define INDEX_BASE_OFFSET    6   /* who wrote this, what's its meaming? */

/* All the following asic mem base is not determined, shall refer to asic design */
#define DRV_EXT_TCAM_KEY_DATA_ASIC_BASE 0xF0000000
#define DRV_EXT_TCAM_KEY_MASK_ASIC_BASE 0xF1000000
#define DRV_EXT_TCAM_AD_MEM_BASE        0xF2000000

#define DRV_INT_TCAM_KEY_DATA_ASIC_BASE TCAM_KEY_OFFSET
#define DRV_INT_TCAM_KEY_MASK_ASIC_BASE TCAM_MASK_OFFSET
#define DRV_INT_TCAM_AD_MEM_4W_BASE     TCAM_DS_RAM4_W_BUS_OFFSET
#define DRV_INT_TCAM_AD_MEM_8W_BASE     TCAM_DS_RAM8_W_BUS_OFFSET

#if 0
#define MAX_MEMORY_BLOCK_NUM 10
#else
#define MAX_MEMORY_BLOCK_NUM 9
#define LM_EXT_BLOCK_NUM 2
#define MAX_DRV_BLOCK_NUM (MAX_MEMORY_BLOCK_NUM + LM_EXT_BLOCK_NUM)
#endif

#define MAX_SHARE_TABLE_IN_ONE_MEMORY_BLOCK 8
/*The following memory base must updated according to RTL designe*/
/* 4 word dynic ram's CPU map address */
#define DRV_MEMORY0_BASE_4W (DYNAMIC_DS_EDRAM4_W_OFFSET)
#define DRV_MEMORY1_BASE_4W (DRV_MEMORY0_BASE_4W + DRV_MEMORY0_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY2_BASE_4W (DRV_MEMORY1_BASE_4W + DRV_MEMORY1_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY3_BASE_4W (DRV_MEMORY2_BASE_4W + DRV_MEMORY2_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY4_BASE_4W (DRV_MEMORY3_BASE_4W + DRV_MEMORY3_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY5_BASE_4W (DRV_MEMORY4_BASE_4W + DRV_MEMORY4_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY6_BASE_4W (DRV_MEMORY5_BASE_4W + DRV_MEMORY5_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY7_BASE_4W (DRV_MEMORY6_BASE_4W + DRV_MEMORY6_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY8_BASE_4W (DRV_MEMORY7_BASE_4W + DRV_MEMORY7_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)

/* 8 word dynic ram's CPU map address */
#define DRV_MEMORY0_BASE_8W (DYNAMIC_DS_EDRAM8_W_OFFSET)
#define DRV_MEMORY1_BASE_8W (DRV_MEMORY0_BASE_8W + DRV_MEMORY0_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY2_BASE_8W (DRV_MEMORY1_BASE_8W + DRV_MEMORY1_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY3_BASE_8W (DRV_MEMORY2_BASE_8W + DRV_MEMORY2_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY4_BASE_8W (DRV_MEMORY3_BASE_8W + DRV_MEMORY3_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY5_BASE_8W (DRV_MEMORY4_BASE_8W + DRV_MEMORY4_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY6_BASE_8W (DRV_MEMORY5_BASE_8W + DRV_MEMORY5_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY7_BASE_8W (DRV_MEMORY6_BASE_8W + DRV_MEMORY6_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_MEMORY8_BASE_8W (DRV_MEMORY7_BASE_8W + DRV_MEMORY7_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)

/*Entry num is based on 72/80 bits, whatever the mem type*/
#define DRV_EXT_TCAM_KEY_MAX_ENTRY_NUM  0
#define DRV_EXT_TCAM_AD_MAX_ENTRY_NUM   0
#define DRV_INT_TCAM_KEY_MAX_ENTRY_NUM                  (8 * 1024)   /* 8k*80 */
#define DRV_INT_TCAM_AD_MAX_ENTRY_NUM                   (8 * 1024)   /* 8k*80 */

#define DRV_MEMORY0_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY1_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY2_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY3_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY4_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY5_MAX_ENTRY_NUM                       (32 * 1024)  /* 16k*160 */
#define DRV_MEMORY6_MAX_ENTRY_NUM                       (16 * 1024)  /* 8k*160 */ /*if the space is enough,increase to 16k*160 */
#define DRV_MEMORY7_MAX_ENTRY_NUM                       (16 * 1024)  /* 4k*320 */
#define DRV_MEMORY8_MAX_ENTRY_NUM                       (16 * 1024)   /* 4k*320, DsStats */

/* TODO: DsLmStats is fixed to have 258 entries in V2.0, but now it uses dynamic tables and has 384 entries */

#define DRV_FIB_CAM_KEY_MAX_ENTRY_NUM 16 /* Only Fib, User id has no cam */

#define DRV_INT_LPM_TCAM_DATA_ASIC_BASE LPM_TCAM_KEY_OFFSET
#define DRV_INT_LPM_TCAM_MASK_ASIC_BASE LPM_TCAM_MASK_OFFSET
#define DRV_INT_LPM_TCAM_MAX_ENTRY_NUM  256

#define DRV_INT_LPM_TCAM_AD_ASIC_BASE LPM_TCAM_AD_MEM_OFFSET
#define DRV_INT_LPM_TCAM_AD_MAX_ENTRY_NUM  (256 * 2) /* 256*160 bit, so the max entry is 256*(160/80) */

#define DRV_DYNAMIC_SRAM_MAX_ENTRY_NUM \
    (DRV_MEMORY0_MAX_ENTRY_NUM + DRV_MEMORY1_MAX_ENTRY_NUM + DRV_MEMORY2_MAX_ENTRY_NUM + \
     DRV_MEMORY3_MAX_ENTRY_NUM + DRV_MEMORY4_MAX_ENTRY_NUM + DRV_MEMORY5_MAX_ENTRY_NUM + \
     DRV_MEMORY6_MAX_ENTRY_NUM + DRV_MEMORY7_MAX_ENTRY_NUM + DRV_MEMORY8_MAX_ENTRY_NUM) /* not include mem 9 */

#define DRV_DYNAMIC_SRAM_4W_MAX_ADDR (DRV_MEMORY0_BASE_4W + DRV_DYNAMIC_SRAM_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_DYNAMIC_SRAM_8W_MAX_ADDR (DRV_MEMORY0_BASE_8W + DRV_DYNAMIC_SRAM_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_TCAMAD_SRAM_4W_MAX_ADDR (DRV_INT_TCAM_AD_MEM_4W_BASE + DRV_INT_TCAM_AD_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)
#define DRV_TCAMAD_SRAM_8W_MAX_ADDR (DRV_INT_TCAM_AD_MEM_8W_BASE + DRV_INT_TCAM_AD_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY)

#define DRV_ADDR_IN_DYNAMIC_SRAM_4W_RANGE(addr) \
    (((addr) >= DRV_MEMORY0_BASE_4W) && ((addr) < DRV_DYNAMIC_SRAM_4W_MAX_ADDR))

#define DRV_ADDR_IN_TCAMAD_SRAM_4W_RANGE(addr) \
    (((addr) >= DRV_INT_TCAM_AD_MEM_4W_BASE) && ((addr) < DRV_TCAMAD_SRAM_4W_MAX_ADDR))

#define DRV_ADDR_IN_DYNAMIC_SRAM_8W_RANGE(addr) \
    (((addr) >= DRV_MEMORY0_BASE_8W) && ((addr) < DRV_DYNAMIC_SRAM_8W_MAX_ADDR))

#define DRV_ADDR_IN_TCAMAD_SRAM_8W_RANGE(addr) \
    (((addr) >= DRV_INT_TCAM_AD_MEM_8W_BASE) && ((addr) < DRV_TCAMAD_SRAM_8W_MAX_ADDR))

#define DRV_ADDR_IN_TCAM_DATA_RANGE(addr) \
    (((addr) >= DRV_INT_TCAM_KEY_DATA_ASIC_BASE) && ((addr) < DRV_INT_TCAM_KEY_DATA_ASIC_BASE + DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY))

#define DRV_ADDR_IN_TCAM_MASK_RANGE(addr) \
    (((addr) >= DRV_INT_TCAM_KEY_MASK_ASIC_BASE) && ((addr) < DRV_INT_TCAM_KEY_MASK_ASIC_BASE + DRV_INT_TCAM_KEY_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY))

#define DRV_ADDR_IN_LPM_TCAM_DATA_RANGE(addr) \
    (((addr) >= DRV_INT_LPM_TCAM_DATA_ASIC_BASE) && ((addr) < DRV_INT_LPM_TCAM_DATA_ASIC_BASE + DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY))

#define DRV_ADDR_IN_LPM_TCAM_MASK_RANGE(addr) \
    (((addr) >= DRV_INT_LPM_TCAM_MASK_ASIC_BASE) && ((addr) < DRV_INT_LPM_TCAM_MASK_ASIC_BASE + DRV_INT_LPM_TCAM_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY))

#define DRV_ADDR_IN_LPM_TCAM_AD_RANGE(addr) \
    (((addr) >= DRV_INT_LPM_TCAM_AD_ASIC_BASE) && ((addr) < DRV_INT_LPM_TCAM_AD_ASIC_BASE + DRV_INT_LPM_TCAM_AD_MAX_ENTRY_NUM * DRV_ADDR_BYTES_PER_ENTRY))

#define DRV_IS_INTR_MULTI_WORD_TBL(_tbl_id) \
    if (GbSupInterruptFatal_t == (_tbl_id) \
     || GbSupInterruptNormal_t == (_tbl_id) \
     || DynamicDsInterruptFatal_t == (_tbl_id) \
     || IntLkInterruptFatal_t == (_tbl_id) \
     || PciExpCoreInterruptNormal_t == (_tbl_id) \
     || QMgrDeqInterruptNormal_t == (_tbl_id))

enum drv_tcam_lookup_e
{
    DRV_TCAM_LOOKUP1,
    DRV_TCAM_LOOKUP2,
    DRV_TCAM_LOOKUP3,
    DRV_TCAM_LOOKUP4,
    DRV_TCAM_LOOKUP_NUM
};
typedef enum drv_tcam_lookup_e drv_tcam_lookup_t;

enum drv_acl_lookup_e
{
    DRV_ACL_LOOKUP0,
    DRV_ACL_LOOKUP1,
    DRV_ACL_LOOKUP2,
    DRV_ACL_LOOKUP3,
    DRV_ACL_LOOKUP_NUM
};
typedef enum drv_acl_lookup_e drv_acl_lookup_t;

/* table extended property type */
enum ext_content_type_s
{
    EXT_INFO_TYPE_NORMAL = 0,
    EXT_INFO_TYPE_TCAM,
    EXT_INFO_TYPE_DESC,
    EXT_INFO_TYPE_DBG,
    EXT_INFO_TYPE_DYNAMIC,
    EXT_INFO_TYPE_INVALID
};
typedef enum ext_content_type_s ext_content_type_t;

/* descriptor extended property structure */
struct entry_desc_s
{
    char desc[64];
};
typedef struct entry_desc_s entry_desc_t;

/* debug extended property structure */
struct dbg_info_s
{
    uint8 b_show;
};
typedef struct dbg_info_s dbg_info_t;

/* Tcam key memories extended property structure */
struct tcam_mem_ext_content_s
{
    uint32 hw_mask_base;          /* Mask hardware base address */

    uint8 key_size;               /* Allocated key size */
};
typedef struct tcam_mem_ext_content_s tcam_mem_ext_content_t;

/* dynamic tbl extended property structure */
enum dynamic_mem_access_mode_u
{
    DYNAMIC_DEFAULT = 0,    /* according table entry size to decide how many 80-bits per index */
    DYNAMIC_4W_MODE,        /* 80 bits per index */
    DYNAMIC_8W_MODE,        /* 160 bits per index */
    DYNAMIC_INVALID
};
typedef enum dynamic_mem_access_mode_u dynamic_mem_access_mode_t;

struct dynamic_mem_allocate_info_s
{
    uint16 dynamic_mem_bitmap;                              /* bitmap indicate table in which dynamic memory block */
    uint32 dynamic_mem_hw_data_base[MAX_DRV_BLOCK_NUM];  /* hw_data_base per memory block */
    uint32 dynamic_mem_entry_num[MAX_DRV_BLOCK_NUM];     /* enter num in per memory block, per 80 bit */
    uint32 dynamic_mem_entry_start_index[MAX_DRV_BLOCK_NUM]; /* global table start index in per memory block */
    uint32 dynamic_mem_entry_end_index[MAX_DRV_BLOCK_NUM];   /* global table end index in per memory bloci */
};
typedef struct dynamic_mem_allocate_info_s dynamic_mem_allocate_info_t;

struct dynamic_mem_ext_content_s
{
    dynamic_mem_allocate_info_t dynamic_mem_allocate_info;  /* store memory allocated info of table in which block */
    dynamic_mem_access_mode_t dynamic_access_mode;          /* access mode, actual entry size of each index */
};
typedef struct dynamic_mem_ext_content_s dynamic_mem_ext_content_t;

#define TCAM_TYPE_EXT (tcam_mem_ext_content_t*)
#define DYNAMIC_TYPE_EXT (dynamic_mem_ext_content_t*)


/* Define get Memory information operation */
#define TABLE_INFO_PTR(tbl_id)           (&drv_gb_tbls_list[tbl_id])           /* Pointer to struct tables_info of tbl */
#define TABLE_INFO(tbl_id)               (drv_gb_tbls_list[tbl_id])            /* Struct tables_info of tbl */
#define TABLE_NAME(tbl_id)               (TABLE_INFO(tbl_id).ptr_tbl_name)  /* String name of tbl */
#define TABLE_DATA_BASE(tbl_id)          (TABLE_INFO(tbl_id).hw_data_base)  /* Hardward data base of tbl*/
#define TABLE_MAX_INDEX(tbl_id)          (TABLE_INFO(tbl_id).max_index_num) /* Max index of tbl */
/* Entry size of tbl, (unit:Byte. Multiple of 12. Full size) */
#define TABLE_ENTRY_SIZE(tbl_id)         (TABLE_INFO(tbl_id).entry_size)
/* Address offset from Begin address to End address.  */
#define TABLE_MAX_ADDR_OFFSET(tbl_id)    ((TABLE_MAX_INDEX(tbl_id)) * ((TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY * DRV_ADDR_BYTES_PER_ENTRY)))
/* End address of tbl  */
#define TABLE_DATA_END_ADDR(tbl_id)      ((TABLE_DATA_BASE(tbl_id)) + (TABLE_MAX_ADDR_OFFSET(tbl_id)))
#define TABLE_FIELD_NUM(tbl_id)          (TABLE_INFO(tbl_id).num_fields)    /* Number of fields */
#define TABLE_FIELD_INFO_PTR(tbl_id)     (TABLE_INFO(tbl_id).ptr_fields)    /* Pointer to first element of fields array */
#define TABLE_EXT_INFO_PTR(tbl_id)       (TABLE_INFO(tbl_id).ptr_ext_info)  /* Pointer to extend struct . */
/* Extend content type. Is EXT_INFO_TYPE_TCAM or EXT_INFO_TYPE_DYNAMIC */
#define TABLE_EXT_INFO_TYPE(tbl_id)      (TABLE_EXT_INFO_PTR(tbl_id)->ext_content_type)
/* Pointer to actual extend content */
#define TABLE_EXT_INFO_CONTENT_PTR(tbl_id)(TABLE_EXT_INFO_PTR(tbl_id)->ptr_ext_content)

#define CHK_IS_REGISTER(tbl_id) \
    ((tbl_id >= 0) && (tbl_id < MaxTblId_t) && (TABLE_MAX_INDEX(tbl_id) == 1)) \

#define CHK_TABLE_ID_VALID(tbl_id) \
    ((tbl_id >= 0) && (tbl_id < MaxTblId_t)) \

#define CHK_FIELD_ID_VALID(tbl_id, field_id) \
    ((field_id) >= 0) && ((field_id) < TABLE_FIELD_NUM(tbl_id)) \

/* Check table ext info is tcam */
#define CHK_TABLE_EXT_INFO_ISTCAM(tbl_id) \
    (EXT_INFO_TYPE_TCAM == (TABLE_EXT_INFO_TYPE(tbl_id))) \


#define TCAM_EXT_INFO_PTR(tbl_id)       (TCAM_TYPE_EXT(TABLE_EXT_INFO_CONTENT_PTR(tbl_id))) /* Pointer to tcam extend content*/
#define TCAM_MASK_BASE(tbl_id)          (TCAM_EXT_INFO_PTR(tbl_id)->hw_mask_base)           /* Hardware mask base of tcam extend content*/
#define TCAM_KEY_SIZE(tbl_id)           (TCAM_EXT_INFO_PTR(tbl_id)->key_size)               /* Key size of tcam extend content */

#define DYNAMIC_EXT_INFO_PTR(tbl_id)    (DYNAMIC_TYPE_EXT(TABLE_EXT_INFO_CONTENT_PTR(tbl_id)))  /* Pointer to dynamic extend content */



/* Actual entry size of dynamic tbl */
#define DYNAMIC_ACCESS_MODE(tbl_id)     (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_access_mode)
/* Bitmap indicate table in which dynamic memory block */
#define DYNAMIC_BITMAP(tbl_id)          (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_mem_allocate_info.dynamic_mem_bitmap)
/* Hardware data base address of dynamic tbl */
#define DYNAMIC_DATA_BASE(tbl_id, blk_id) (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_mem_allocate_info.dynamic_mem_hw_data_base[blk_id])
/* Entry number of dynamic tbl in each memory block */
#define DYNAMIC_ENTRY_NUM(tbl_id, blk_id) (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_mem_allocate_info.dynamic_mem_entry_num[blk_id])
/* Start index of dynamic tbl in each memory block */
#define DYNAMIC_START_INDEX(tbl_id, blk_id) (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_mem_allocate_info.dynamic_mem_entry_start_index[blk_id])
/* End index of dynamic tbl in each memory block */
#define DYNAMIC_END_INDEX(tbl_id, blk_id) (DYNAMIC_EXT_INFO_PTR(tbl_id)->dynamic_mem_allocate_info.dynamic_mem_entry_end_index[blk_id])

/* check chip id valid */
extern drv_init_chip_info_t drv_gb_init_chip_info;

#define DRV_CHIP_ID_VALID_CHECK(chip_id) \
    if ((drv_gb_init_chip_info.drv_init_chipid_base \
         + drv_gb_init_chip_info.drv_init_chip_num) <= (chip_id)) \
    { \
        DRV_DBG_INFO("\nERROR! INVALID ChipID! chipid: %d, file:%s line:%d function:%s\n", chip_id, __FILE__, __LINE__, __FUNCTION__); \
        return DRV_E_INVALID_CHIP; \
    }

/* check Table Id valid */
#define DRV_TBL_ID_VALID_CHECK(tbl_id) \
    if ((tbl_id) >= (MaxTblId_t)) \
    { \
        DRV_DBG_INFO("\nERROR! INVALID TblID! TblID: %d, file:%s line:%d function:%s\n", tbl_id, __FILE__, __LINE__, __FUNCTION__); \
        return DRV_E_INVALID_TBL; \
    }

/* check if Table is empty */
#define DRV_TBL_EMPTY_CHECK(tbl_id) \
    if (TABLE_MAX_INDEX(tbl_id) == 0) \
    { \
        DRV_DBG_INFO("\nERROR! Operation on an empty table! TblID: %d, file:%s line:%d function:%s\n", tbl_id, __FILE__, __LINE__, __FUNCTION__); \
        return DRV_E_INVALID_TBL; \
    }

/* check is index of table id is valid */
#define DRV_TBL_INDEX_VALID_CHECK(tbl_id, index) \
    if (index >= TABLE_MAX_INDEX(tbl_id)) \
    { \
        DRV_DBG_INFO("\nERROR! Index-0x%x exceeds the max_index 0x%x! TblID: %d, file:%s line:%d function:%s\n", \
                     index, TABLE_MAX_INDEX(tbl_id), tbl_id, __FILE__, __LINE__, __FUNCTION__); \
        return DRV_E_INVALID_INDEX; \
    }

enum sram_share_mem_id_e
{
    /* Hash key and ADs*/
    LPM_HASH_KEY_SHARE_TABLE = 0,
    LPM_LOOKUP_KEY_TABLE0    = 1,
    LPM_LOOKUP_KEY_TABLE1    = 2,
    LPM_LOOKUP_KEY_TABLE2    = 3,
    LPM_LOOKUP_KEY_TABLE3    = 4,
    USER_ID_HASH_KEY_SHARE_TABLE = 5,
    USER_ID_HASH_AD_SHARE_TABLE  = 6,
    OAM_HASH_CHAN_SHARE_TABLE  = 7,

    FIB_HASH_KEY_SHARE_TABLE = 8,
    FIB_HASH_AD_SHARE_TABLE  = 9,

    /* other indepencence tables treated as share table */
    FWD_SHARE_TABLE = 10,
    MET_ENTRY_SHARE_TABLE = 11,
    MPLS_SHARE_TABLE = 12,
    MA_SHARE_TABLE = 13,
    MA_NAME_SHARE_TABLE = 14,

    /*Dynamic tables*/
    NEXTHOP_SHARE_TABLE = 15,
    EDIT_SHARE_TABLE = 16,
    OAM_MEP_SHARE_TABLE = 17,

    STATS_SHARE_TABLE = 18,

    OAM_LM_STAT = 19,

    MAX_SRAM_SHARE_MEM_ID
};
typedef enum sram_share_mem_id_e sram_share_mem_id_t;

/* Unified fields structure */
struct fields_s
{
    uint32 len;

    uint8 word_offset;   /* for example if entry_size = 8 bytes, word_offset = 0 or 1*/
    uint8 bit_offset;    /* bit offset in one word of a entry, range 0~31 */

    char* ptr_field_name;
};
typedef struct fields_s fields_t;

/* Unified tables extended property structure */
struct tbls_ext_info_s
{
    ext_content_type_t ext_content_type;
    void* ptr_ext_content;
    struct tbls_ext_info_s* ptr_next;
};
typedef struct tbls_ext_info_s tbls_ext_info_t;

/* Unified memories property structure */
struct tables_info_s
{
    char* ptr_tbl_name;            /* table name */

    uint32 hw_data_base;           /* Hardware data base address */

    uint32 max_index_num;          /* Max index number */

    uint8 entry_size;              /* Each index's size (unit: Byte), but for Tcam key, is full key size */
    uint8 num_fields;              /* Included fields max number */

    fields_t* ptr_fields;          /* Point to each fileds' information */

    tbls_ext_info_t* ptr_ext_info; /* Point to table's additional information */
};
typedef struct tables_info_s tables_info_t;

/**********************************************************************************
                      Define external transfer var
***********************************************************************************/
/* sdk has a global pointer arry for per-chip */
extern tables_info_t drv_gb_tbls_list[MaxTblId_t];

/* all dynimic table's related EDRAM info */
extern uint16 gb_dynic_table_related_edram_bitmap[];
/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/

/**
 @brief according to memory field id to find the field property
*/
extern fields_t*
drv_greatbelt_find_field(tbls_id_t tbl_id, fld_id_t field_id);

/**
 @brief set a field of a memory data entry
*/
extern int32
drv_greatbelt_set_field(tbls_id_t tbl_id, fld_id_t field_id,
              uint32* entry, uint32 value);

/**
 @brief get a field of a memory data entry
*/
extern int32
drv_greatbelt_get_field(tbls_id_t tbl_id, fld_id_t field_id,
              uint32* entry, uint32* value);

/**
 @brief Get a field of  word & bit offset
*/
extern int32
drv_greatbelt_get_field_offset(tbls_id_t tbl_id, fld_id_t field_id, uint32* w_offset, uint32* b_offset);

/**
 @brief Check tcam field in key size
*/
extern int32
drv_greatbelt_tcam_field_chk(tbls_id_t tbl_id, fld_id_t field_id);

/**
 @brief register a memory
*/
extern int32
drv_greatbelt_tbl_register(tbls_id_t tbl_id, uint32 data_offset, uint32 max_idx,
                 uint16 entry_size, uint8 num_f, fields_t* ptr_f);

/**
 @brief
*/
extern int32
drv_greatbelt_sram_ds_to_entry(tbls_id_t tbl_id, void* ds, uint32* entry);

/**
 @brief
*/
extern int32
drv_greatbelt_sram_entry_to_ds(tbls_id_t tbl_id, uint32* entry, void* ds);

/**
 @brief
*/
extern int32
drv_greatbelt_tcam_ds_to_entry(tbls_id_t tbl_id, void* ds, void* entry);

/**
 @brief
*/
extern int32
drv_greatbelt_tcam_entry_to_ds(tbls_id_t tbl_id, void* entry, void* ds);

/**
 @brief
*/
extern int32
drv_greatbelt_get_tbl_string_by_id(tbls_id_t tbl_id, char* name);

/**
 @brief
*/
extern int32
drv_greatbelt_get_field_string_by_id(tbls_id_t tbl_id, fld_id_t field_id, char* name);

/**
 @brief
*/
extern int32
drv_greatbelt_get_tbl_id_by_string(tbls_id_t* tbl_id, char* name);

/**
 @brief
*/
extern int32
drv_greatbelt_get_field_id_by_string(tbls_id_t tbl_id, fld_id_t* field_id, char* name);

/* Get hardware address according to tablid + index (do not include tcam key) */
extern int32
drv_greatbelt_table_get_hw_addr(tbls_id_t tbl_id, uint32 index, uint32* hw_addr);

/* Get hardware address according to tablid + index + data/mask flag (only tcam key) */
extern int32
drv_greatbelt_tcam_key_get_hw_addr(tbls_id_t tbl_id, uint32 index, bool is_data, uint32* hw_addr);

/* judge table is tcam key table according tbl_id */
extern int8 drv_greatbelt_table_is_tcam_key(tbls_id_t tbl_id);

/* judge table is dynamic table according tbl_id */
extern int8 drv_greatbelt_table_is_dynamic_table(tbls_id_t tbl_id);

extern int8
drv_greatbelt_table_is_register(tbls_id_t tbl_id);

extern int32 drv_greatbelt_table_consum_hw_addr_size_per_index(tbls_id_t tbl_id, uint32* hw_addr_size);

#endif  /*end of _DRV_TBL_REG_H_*/


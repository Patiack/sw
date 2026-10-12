/*
 @file drv_ftm.c

 @date 2011-11-16

 @version v2.0

 This file provides all sys alloc function
*/

#include "sal.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_ftm.h"
#include "goldengate/include/drv_common.h"
#include "goldengate/include/drv_io.h"

#define DRV_FTM_DBG_OUT(fmt, args...)       sal_printf(fmt, ##args)
#define DRV_FTM_DBG_DUMP(fmt, args...)     /*sal_printf(fmt, ##args)*/

#define   DRV_FTM_TBL_INFO(tbl_id)   g_gg_ftm_master->p_sram_tbl_info[tbl_id]
#define   DRV_FTM_KEY_INFO(tbl_id)   g_gg_ftm_master->p_tcam_key_info[tbl_id]
#define   DRV_FTM_MEM_INFO(mem_id)   drv_gg_ftm_mem_info[mem_id]

#define DRV_FTM_TBL_ACCESS_FLAG(tbl_id)               DRV_FTM_TBL_INFO(tbl_id).access_flag
#define DRV_FTM_TBL_MAX_ENTRY_NUM(tbl_id)             DRV_FTM_TBL_INFO(tbl_id).max_entry_num
#define DRV_FTM_TBL_MEM_BITMAP(tbl_id)                DRV_FTM_TBL_INFO(tbl_id).mem_bitmap
#define DRV_FTM_TBL_MEM_START_OFFSET(tbl_id, mem_id)  DRV_FTM_TBL_INFO(tbl_id).mem_start_offset[mem_id]
#define DRV_FTM_TBL_MEM_ENTRY_NUM(tbl_id, mem_id)     DRV_FTM_TBL_INFO(tbl_id).mem_entry_num[mem_id]

#define DRV_FTM_TBL_BASE_BITMAP(tbl_id)      DRV_FTM_TBL_INFO(tbl_id).dyn_ds_base.bitmap
#define DRV_FTM_TBL_BASE_VAL(tbl_id, i)   DRV_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_base[i]
#define DRV_FTM_TBL_BASE_MIN(tbl_id, i)   DRV_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_min_index[i]
#define DRV_FTM_TBL_BASE_MAX(tbl_id, i)   DRV_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_max_index[i]

#define DRV_FTM_MEM_ALLOCED_ENTRY_NUM(mem_id) DRV_FTM_MEM_INFO(mem_id) .allocated_entry_num
#define DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id)     DRV_FTM_MEM_INFO(mem_id).max_mem_entry_num
#define DRV_FTM_MEM_HW_DATA_BASE(mem_id)      DRV_FTM_MEM_INFO(mem_id).hw_data_base_addr_4w
#define DRV_FTM_MEM_HW_DATA_BASE6W(mem_id)      DRV_FTM_MEM_INFO(mem_id).hw_data_base_addr_mask
#define DRV_FTM_MEM_HW_DATA_BASE12W(mem_id)      DRV_FTM_MEM_INFO(mem_id).hw_data_base_addr_12w
#define DRV_FTM_MEM_HW_MASK_BASE(mem_id)      DRV_FTM_MEM_INFO(mem_id).hw_data_base_addr_mask

#define DRV_FTM_KEY_MAX_INDEX(tbl_id)                  DRV_FTM_KEY_INFO(tbl_id).max_key_index
#define DRV_FTM_KEY_MEM_BITMAP(tbl_id)                 DRV_FTM_KEY_INFO(tbl_id).mem_bitmap
#define DRV_FTM_KEY_ENTRY_NUM(tbl_id, type, mem_id)                 DRV_FTM_KEY_INFO(tbl_id).entry_num[type][mem_id]
#define DRV_FTM_KEY_START_OFFSET(tbl_id, type, mem_id)                 DRV_FTM_KEY_INFO(tbl_id).start_offset[type][mem_id]

#define DRV_FTM_ADD_TABLE(mem_id, table, start, size) \
    do { \
        if (size) \
        { \
            DRV_FTM_TBL_MEM_BITMAP(table)               |= (1 << mem_id); \
            DRV_FTM_TBL_MAX_ENTRY_NUM(table)            += size; \
            DRV_FTM_TBL_MEM_START_OFFSET(table, mem_id) = start; \
            DRV_FTM_TBL_MEM_ENTRY_NUM(table, mem_id)    = size; \
        } \
    } while (0)


#define DRV_FTM_ADD_KEY(mem_id, table, start, size) \
    do { \
        if (size) \
        { \
            DRV_FTM_KEY_MEM_BITMAP(table)               |=  (1 << (mem_id-DRV_FTM_TCAM_KEY0)); \
            DRV_FTM_KEY_MAX_INDEX(table)                += size; \
        } \
    } while (0)

#define DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, type, start, size) \
    do { \
        if (size) \
        { \
           DRV_FTM_KEY_START_OFFSET(table, type, mem_id-DRV_FTM_TCAM_KEY0)       = start; \
           DRV_FTM_KEY_ENTRY_NUM(table, type, mem_id-DRV_FTM_TCAM_KEY0)          = size; \
        } \
    } while (0)


#define DRV_FTM_ADD_FULL_TABLE(mem_id, table) \
        do {\
            uint32 couple = 0;\
            drv_goldengate_get_dynamic_ram_couple_mode(&couple);\
            if (couple) \
            {\
                DRV_FTM_ADD_TABLE(mem_id, table, 0, (DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id)*2));\
            } else {\
                DRV_FTM_ADD_TABLE(mem_id, table, 0, DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id));    \
            }\
        } while (0)


#define ENTRY_NUM entry_num

#define DRV_FTM_ADD_FULL_KEY(mem_id, table) \
        do { \
            uint32 couple = 0;\
            uint32 entry_num = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id);\
            drv_goldengate_get_dynamic_ram_couple_mode(&couple);\
            if (couple) \
            {\
                entry_num = (ENTRY_NUM*2);\
            } else {\
                entry_num = ENTRY_NUM;\
            }\
            DRV_FTM_ADD_KEY(mem_id, table, 0, entry_num);\
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_160, 0, entry_num/4); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_320, entry_num/4, entry_num/4); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_640, entry_num/2, entry_num/2); \
       } while (0)


#define DRV_FTM_ADD_LPM0_KEY(mem_id, table) \
        do { \
            uint32 couple = 0;\
            uint32 entry_num = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id);\
            drv_goldengate_get_dynamic_ram_couple_mode(&couple);\
            if (couple) \
            {\
                entry_num = (ENTRY_NUM*2);\
            } else {\
                entry_num = ENTRY_NUM;\
            }\
            DRV_FTM_ADD_KEY(mem_id, table, 0, entry_num);\
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_LPM40, entry_num*0/2, entry_num/2); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_LPM160, entry_num*1/2, entry_num/2); \
     } while (0)


#define DRV_FTM_ADD_LPM1_KEY(mem_id, table) \
        do { \
            uint32 couple = 0;\
            uint32 entry_num = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_id);\
            drv_goldengate_get_dynamic_ram_couple_mode(&couple);\
            if (couple) \
            {\
                entry_num = (ENTRY_NUM*2);\
            } else {\
                entry_num = ENTRY_NUM;\
            }\
            DRV_FTM_ADD_KEY(mem_id, table, 0, entry_num);\
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_PBR0, entry_num*0/4, entry_num/4); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_PBR1, entry_num*1/4, entry_num/4); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_NAT0, entry_num*2/4, entry_num/4); \
            DRV_FTM_ADD_KEY_BYTYPE(mem_id, table, DRV_FTM_TCAM_SIZE_NAT1, entry_num*3/4, entry_num/4); \
     } while (0)

enum drv_ftm_tcam_key_type_e
{
    DRV_FTM_TCAM_TYPE_IGS_ACL0,
    DRV_FTM_TCAM_TYPE_IGS_ACL1,
    DRV_FTM_TCAM_TYPE_IGS_ACL2,
    DRV_FTM_TCAM_TYPE_IGS_ACL3,

    DRV_FTM_TCAM_TYPE_EGS_ACL0,
    DRV_FTM_TCAM_TYPE_IGS_USERID0,
    DRV_FTM_TCAM_TYPE_IGS_USERID1,

    DRV_FTM_TCAM_TYPE_IGS_LPM0,
    DRV_FTM_TCAM_TYPE_IGS_LPM1,

    DRV_FTM_TCAM_TYPE_MAX
};
typedef enum drv_ftm_tcam_key_type_e drv_ftm_tcam_key_type_t;

enum drv_ftm_sram_tbl_type_e
{
    DRV_FTM_SRAM_TBL_FIB0_HASH_KEY,
    DRV_FTM_SRAM_TBL_FIB1_HASH_KEY,
    DRV_FTM_SRAM_TBL_FLOW_HASH_KEY,
    DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY,
    DRV_FTM_SRAM_TBL_USERID_HASH_KEY,
    DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY,

    DRV_FTM_SRAM_TBL_LPM_LKP_KEY,

    DRV_FTM_SRAM_TBL_DSMAC_AD,
    DRV_FTM_SRAM_TBL_DSIP_AD,
    DRV_FTM_SRAM_TBL_FLOW_AD,
    DRV_FTM_SRAM_TBL_IPFIX_AD,
    DRV_FTM_SRAM_TBL_USERID_AD,

    DRV_FTM_SRAM_TBL_NEXTHOP,
    DRV_FTM_SRAM_TBL_FWD,
    DRV_FTM_SRAM_TBL_MET,
    DRV_FTM_SRAM_TBL_EDIT,

    DRV_FTM_SRAM_TBL_OAM_APS,
    DRV_FTM_SRAM_TBL_OAM_LM,
    DRV_FTM_SRAM_TBL_OAM_MEP,
    DRV_FTM_SRAM_TBL_OAM_MA,
    DRV_FTM_SRAM_TBL_OAM_MA_NAME,

    DRV_FTM_SRAM_TBL_MAX
};
typedef enum drv_ftm_sram_tbl_type_e drv_ftm_sram_tbl_type_t;

enum drv_ftm_dyn_cam_type_e
{
    DRV_FTM_DYN_CAM_TYPE_LPM_KEY,
    DRV_FTM_DYN_CAM_TYPE_FIB_MACHOST0_KEY,
    DRV_FTM_DYN_CAM_TYPE_FIB_MACHOST1_KEY,
    DRV_FTM_DYN_CAM_TYPE_FIB_FLOW_KEY,
    DRV_FTM_DYN_CAM_TYPE_FIB_FLOW_AD,
    DRV_FTM_DYN_CAM_TYPE_USERID_HASH_KEY,
    DRV_FTM_DYN_CAM_TYPE_USERID_HASH_AD,

    DRV_FTM_DYN_CAM_TYPE_EGRESS_OAM_HASH_KEY,

    DRV_FTM_DYN_CAM_TYPE_IP_FIX_HASH_KEY,
    DRV_FTM_DYN_CAM_TYPE_IP_FIX_HASH_AD,
    DRV_FTM_DYN_CAM_TYPE_FIB_L2_AD,
    DRV_FTM_DYN_CAM_TYPE_FIB_L3_AD,
    DRV_FTM_DYN_CAM_TYPE_DS_LM,
    DRV_FTM_DYN_CAM_TYPE_DS_APS,
    DRV_FTM_DYN_CAM_TYPE_DS_MEP,
    DRV_FTM_DYN_CAM_TYPE_DS_MA,
    DRV_FTM_DYN_CAM_TYPE_DS_MANAME,
    DRV_FTM_DYN_CAM_TYPE_DS_EDIT,
    DRV_FTM_DYN_CAM_TYPE_DS_NEXTHOP,
    DRV_FTM_DYN_CAM_TYPE_DS_MET,
    DRV_FTM_DYN_CAM_TYPE_MAX
};
typedef enum drv_ftm_dyn_cam_type_e drv_ftm_dyn_cam_type_t;

#define DRV_FTM_UNIT_1K 10
#define DRV_FTM_UNIT_8K 13
#define DRV_FTM_FLD_INVALID 0xFFFFFFFF

#define DRV_FTM_MAX_MEM_NUM 5

struct drv_ftm_dyn_ds_base_s
{
    uint32 bitmap;
    uint32 ds_dynamic_base[DRV_FTM_MAX_MEM_NUM];
    uint32 ds_dynamic_max_index[DRV_FTM_MAX_MEM_NUM];
    uint32 ds_dynamic_min_index[DRV_FTM_MAX_MEM_NUM];
};
typedef struct drv_ftm_dyn_ds_base_s drv_ftm_dyn_ds_base_t;

struct drv_ftm_sram_tbl_info_s
{
    uint32 access_flag; /*judge weather tbl share alloc*/
    uint32 mem_bitmap;
    uint32 max_entry_num;
    uint32 mem_start_offset[DRV_FTM_SRAM_MAX];
    uint32 mem_entry_num[DRV_FTM_SRAM_MAX];
    drv_ftm_dyn_ds_base_t dyn_ds_base;
};
typedef struct drv_ftm_sram_tbl_info_s drv_ftm_sram_tbl_info_t;

#define BYTE_TO_4W  12

#define DRV_FTM_TCAM_SIZE_160 0
#define DRV_FTM_TCAM_SIZE_320 1
#define DRV_FTM_TCAM_SIZE_640 2

#define DRV_FTM_TCAM_SIZE_PBR0 0
#define DRV_FTM_TCAM_SIZE_PBR1 1
#define DRV_FTM_TCAM_SIZE_NAT0 2
#define DRV_FTM_TCAM_SIZE_NAT1 3

#define DRV_FTM_TCAM_SIZE_LPM40 0
#define DRV_FTM_TCAM_SIZE_LPM160 1

#define DRV_FTM_TCAM_SIZE_MAX 4

#define DRV_FTM_TCAM_ACL_AD_MAP(tbl0, tbl1, tbl2, tbl3) \
        do { \
             if (size_type == DRV_FTM_TCAM_SIZE_160)\
             {\
                 p_ad_share_tbl[idx0++] = tbl0;\
                 p_ad_share_tbl[idx0++] = tbl1;\
             }\
             else if  (size_type == DRV_FTM_TCAM_SIZE_320)\
             {\
                 p_ad_share_tbl[idx0++] = tbl2;\
             } \
             else if(size_type == DRV_FTM_TCAM_SIZE_640)\
             {\
                 p_ad_share_tbl[idx0++] = tbl3; \
             }\
       } while (0)

#define DRV_FTM_TCAM_LPM0_AD_MAP(tbl0, tbl1) \
        do { \
             if (size_type == DRV_FTM_TCAM_SIZE_LPM40)\
             {\
                 p_ad_share_tbl[idx0++] = tbl0;\
             }\
             else if  (size_type == DRV_FTM_TCAM_SIZE_LPM160)\
             {\
                 p_ad_share_tbl[idx0++] = tbl1;\
             } \
       } while (0)



#define DRV_FTM_TCAM_LPM1_AD_MAP(tbl0, tbl1, tbl2, tbl3) \
        do { \
             if (size_type == DRV_FTM_TCAM_SIZE_PBR0)\
             {\
                 p_ad_share_tbl[idx0++] = tbl0;\
             }\
             else if  (size_type == DRV_FTM_TCAM_SIZE_PBR1)\
             {\
                 p_ad_share_tbl[idx0++] = tbl1;\
             } \
             else if(size_type == DRV_FTM_TCAM_SIZE_NAT1)\
             {\
                 p_ad_share_tbl[idx0++] = tbl3; \
             }\
             else if(size_type == DRV_FTM_TCAM_SIZE_NAT0)\
             {\
                 p_ad_share_tbl[idx0++] = tbl2; \
             }\
       } while (0)

#define DRV_FTM_CAM_CHECK_TBL_VALID(cam_type, tbl_id)\
    if ((DRV_FTM_CAM_TYPE_FIB_HOST1_NAT == cam_type)\
        && (DsFibHost1Ipv4NatDaPortHashKey_t != tbl_id))\
    {\
        return DRV_E_HASH_CONFLICT;\
    }

struct drv_ftm_tcam_key_info_s
{
    uint32 mem_bitmap;
    uint32  max_key_index;
    uint32 entry_num[DRV_FTM_TCAM_SIZE_MAX][DRV_FTM_TCAM_KEYM];
    uint32 start_offset[DRV_FTM_TCAM_SIZE_MAX][DRV_FTM_TCAM_KEYM];
};
typedef struct drv_ftm_tcam_key_info_s drv_ftm_tcam_key_info_t;



struct g_ftm_master_s
{
    drv_ftm_sram_tbl_info_t* p_sram_tbl_info;
    drv_ftm_tcam_key_info_t* p_tcam_key_info;

    uint16 int_tcam_used_entry;
    uint8 rsv1[2];

    /*cam*/
    uint8 cfg_max_cam_num[DRV_FTM_CAM_TYPE_MAX];
    uint8 conflict_cam_num[DRV_FTM_CAM_TYPE_MAX];

    /*cam bitmap*/
    uint32 fib1_cam_bitmap0[1];     /*single*/
    uint32 fib1_cam_bitmap1[1];     /*dual*/
    uint32 fib1_cam_bitmap2[1];     /*quad*/

    uint32 userid_cam_bitmap0[1];   /*single*/
    uint32 userid_cam_bitmap1[1];   /*dual*/
    uint32 userid_cam_bitmap2[1];   /*quad*/

    uint32 xcoam_cam_bitmap0[4];    /*single*/
    uint32 xcoam_cam_bitmap1[4];    /*dual*/
    uint32 xcoam_cam_bitmap2[4];    /*quad*/

    uint32 cam_bitmap;
    uint32 host1_poly_type;
};
typedef struct g_ftm_master_s g_ftm_master_t;

g_ftm_master_t* g_gg_ftm_master = NULL;


struct drv_ftm_mem_allo_info_s
{
    uint32 allocated_entry_num;            /* entry size is always 72/80 */
    uint32 max_mem_entry_num;
    uint32 hw_data_base_addr_4w;     /* to dynic memory and TcamAd memory, is 4word mode base address */
    uint32 hw_data_base_addr_mask; /*union of 6w*/
    uint32 hw_data_base_addr_12w;
};
typedef struct drv_ftm_mem_allo_info_s drv_ftm_mem_allo_info_t;

#define DRV_BIT_SET(flag, bit)      ((flag) = (flag) | (1 << (bit)))

static drv_ftm_mem_allo_info_t drv_gg_ftm_mem_info[DRV_FTM_MAX_ID] =
{
{0 , DRV_SHARE_RAM0_MAX_ENTRY_NUM        , DRV_SHARE_RAM0_BASE_4W        ,DRV_SHARE_RAM0_BASE_6W        ,DRV_SHARE_RAM0_BASE_12W        }, /*MEMORY0*/
{0 , DRV_SHARE_RAM1_MAX_ENTRY_NUM        , DRV_SHARE_RAM1_BASE_4W        ,DRV_SHARE_RAM1_BASE_6W        ,DRV_SHARE_RAM1_BASE_12W        }, /*MEMORY1*/
{0 , DRV_SHARE_RAM2_MAX_ENTRY_NUM        , DRV_SHARE_RAM2_BASE_4W        ,DRV_SHARE_RAM2_BASE_6W        ,DRV_SHARE_RAM2_BASE_12W        }, /*MEMORY2*/
{0 , DRV_SHARE_RAM3_MAX_ENTRY_NUM        , DRV_SHARE_RAM3_BASE_4W        ,DRV_SHARE_RAM3_BASE_6W        ,DRV_SHARE_RAM3_BASE_12W        }, /*MEMORY3*/
{0 , DRV_SHARE_RAM4_MAX_ENTRY_NUM        , DRV_SHARE_RAM4_BASE_4W        ,DRV_SHARE_RAM4_BASE_6W        ,DRV_SHARE_RAM4_BASE_12W        }, /*MEMORY4*/
{0 , DRV_SHARE_RAM5_MAX_ENTRY_NUM        , DRV_SHARE_RAM5_BASE_4W        ,DRV_SHARE_RAM5_BASE_6W        ,DRV_SHARE_RAM5_BASE_12W        }, /*MEMORY5*/
{0 , DRV_SHARE_RAM6_MAX_ENTRY_NUM        , DRV_SHARE_RAM6_BASE_4W        ,DRV_SHARE_RAM6_BASE_6W        ,DRV_SHARE_RAM6_BASE_12W        }, /*MEMORY6*/
{0 , DRV_DS_IPMAC_RAM0_MAX_ENTRY_NUM     , DRV_DS_IPMAC_RAM0_BASE_4W     ,DRV_DS_IPMAC_RAM0_BASE_6W     ,DRV_DS_IPMAC_RAM0_BASE_12W     }, /*MEMORY7*/
{0 , DRV_DS_IPMAC_RAM1_MAX_ENTRY_NUM     , DRV_DS_IPMAC_RAM1_BASE_4W     ,DRV_DS_IPMAC_RAM1_BASE_6W     ,DRV_DS_IPMAC_RAM1_BASE_12W     }, /*MEMORY8*/
{0 , DRV_DS_IPMAC_RAM2_MAX_ENTRY_NUM     , DRV_DS_IPMAC_RAM2_BASE_4W     ,DRV_DS_IPMAC_RAM2_BASE_6W     ,DRV_DS_IPMAC_RAM2_BASE_12W     }, /*MEMORY9*/
{0 , DRV_DS_IPMAC_RAM3_MAX_ENTRY_NUM     , DRV_DS_IPMAC_RAM3_BASE_4W     ,DRV_DS_IPMAC_RAM3_BASE_6W     ,DRV_DS_IPMAC_RAM3_BASE_12W     }, /*MEMORY10*/
{0 , DRV_USERIDHASHKEY_RAM0_MAX_ENTRY_NUM, DRV_USERIDHASHKEY_RAM0_BASE_4W,DRV_USERIDHASHKEY_RAM0_BASE_6W,DRV_USERIDHASHKEY_RAM0_BASE_12W}, /*MEMORY11*/
{0 , DRV_USERIDHASHKEY_RAM1_MAX_ENTRY_NUM, DRV_USERIDHASHKEY_RAM1_BASE_4W,DRV_USERIDHASHKEY_RAM1_BASE_6W,DRV_USERIDHASHKEY_RAM1_BASE_12W}, /*MEMORY12*/
{0 , DRV_USERIDHASHAD_RAM_MAX_ENTRY_NUM  , DRV_USERIDHASHAD_RAM_BASE_4W  ,DRV_USERIDHASHAD_RAM_BASE_6W  ,DRV_USERIDHASHAD_RAM_BASE_12W  }, /*MEMORY13*/
{0 , DRV_L23EDITRAM0_MAX_ENTRY_NUM       , DRV_L23EDITRAM0_BASE_4W       ,DRV_L23EDITRAM0_BASE_6W       ,DRV_L23EDITRAM0_BASE_12W       }, /*MEMORY14*/
{0 , DRV_L23EDITRAM1_MAX_ENTRY_NUM       , DRV_L23EDITRAM1_BASE_4W       ,DRV_L23EDITRAM1_BASE_6W       ,DRV_L23EDITRAM1_BASE_12W       }, /*MEMORY15*/
{0 , DRV_NEXTHOPMET_RAM0_MAX_ENTRY_NUM   , DRV_NEXTHOPMET_RAM0_BASE_4W   ,DRV_NEXTHOPMET_RAM0_BASE_6W   ,DRV_NEXTHOPMET_RAM0_BASE_12W   }, /*MEMORY16*/
{0 , DRV_NEXTHOPMET_RAM1_MAX_ENTRY_NUM   , DRV_NEXTHOPMET_RAM1_BASE_4W   ,DRV_NEXTHOPMET_RAM1_BASE_6W   ,DRV_NEXTHOPMET_RAM1_BASE_12W   }, /*MEMORY17*/
{0,0,0,0,0},

{0 , DRV_TCAM_KEY0_MAX_ENTRY_NUM     , DRV_TCAM_KEY0_BASE_4W        , DRV_TCAM_MASK0_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY1_MAX_ENTRY_NUM     , DRV_TCAM_KEY1_BASE_4W        , DRV_TCAM_MASK1_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY2_MAX_ENTRY_NUM     , DRV_TCAM_KEY2_BASE_4W        , DRV_TCAM_MASK2_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY3_MAX_ENTRY_NUM     , DRV_TCAM_KEY3_BASE_4W        , DRV_TCAM_MASK3_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY4_MAX_ENTRY_NUM     , DRV_TCAM_KEY4_BASE_4W        , DRV_TCAM_MASK4_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY5_MAX_ENTRY_NUM     , DRV_TCAM_KEY5_BASE_4W        , DRV_TCAM_MASK5_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_TCAM_KEY6_MAX_ENTRY_NUM     , DRV_TCAM_KEY6_BASE_4W        , DRV_TCAM_MASK6_BASE_4W, 0} , /*TCAM_KEY*/
{0 , DRV_LPM_TCAM_KEY0_MAX_ENTRY_NUM , DRV_LPM_TCAM_DATA_ASIC_BASE0 , DRV_LPM_TCAM_MASK_ASIC_BASE0, 0}                      , /*LPM TCAM KEY */
{0 , DRV_LPM_TCAM_KEY1_MAX_ENTRY_NUM , DRV_LPM_TCAM_DATA_ASIC_BASE1 , DRV_LPM_TCAM_MASK_ASIC_BASE1, 0}                      , /*LPM TCAM KEY */
{0,0,0,0,0},

{0 , DRV_TCAM_AD0_MAX_ENTRY_NUM     , DRV_TCAM_AD0_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD1_MAX_ENTRY_NUM     , DRV_TCAM_AD1_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD2_MAX_ENTRY_NUM     , DRV_TCAM_AD2_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD3_MAX_ENTRY_NUM     , DRV_TCAM_AD3_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD4_MAX_ENTRY_NUM     , DRV_TCAM_AD4_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD5_MAX_ENTRY_NUM     , DRV_TCAM_AD5_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_TCAM_AD6_MAX_ENTRY_NUM     , DRV_TCAM_AD6_BASE_4W       , 0, 0} , /*TCAM_AD*/
{0 , DRV_LPM_TCAM_AD0_MAX_ENTRY_NUM , DRV_LPM_TCAM_AD_ASIC_BASE0 , 0, 0} , /*LPM TCAM AD */
{0 , DRV_LPM_TCAM_AD1_MAX_ENTRY_NUM , DRV_LPM_TCAM_AD_ASIC_BASE1 , 0, 0} , /*LPM TCAM AD */
{0,0,0,0,0}
};

/*
STATIC int32
_drv_goldengate_ftm_init_mem_alloc_info()
{
    drv_ftm_mem_allo_info_t drv_gg_ftm_mem_info[DRV_FTM_MAX_ID] =

    return DRV_E_NONE;
}
*/


STATIC int32
_drv_goldengate_ftm_set_profile_default(void)
{

    uint8 table         = 0;
    uint32 couple_mode  = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));
    /********************************************
    dynamic edram table
    *********************************************/

    /*FIB Host0 Hash Key*/
    table = DRV_FTM_SRAM_TBL_FIB0_HASH_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM1, table);
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM2, table);
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM4, table); /*Moidfy to 8K*/

    /*FIB Host0 Hash Key*/
    table = DRV_FTM_SRAM_TBL_FIB1_HASH_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM6, table); /*Moidfy to 16K*/

    /*FLOW Hash Key*/
    table = DRV_FTM_SRAM_TBL_FLOW_HASH_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM0, table);

    /*FLOW Hash AD*/
    table = DRV_FTM_SRAM_TBL_FLOW_AD;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM8, table);/*Moidfy to 16K*/

    /*Lpm lookup Key*/
    table = DRV_FTM_SRAM_TBL_LPM_LKP_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM5, table);/*Moidfy to 16K*/

    /*DsIpDa*/
    table = DRV_FTM_SRAM_TBL_DSIP_AD;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM9, table);/*Moidfy to 16K*/

    /*DsIpMac*/
    table = DRV_FTM_SRAM_TBL_DSMAC_AD;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM10, table);/*Moidfy to 16K*/

    /*UserIdTunnelMpls Hash Key*/
    table = DRV_FTM_SRAM_TBL_USERID_HASH_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM11, table);/*Moidfy to 8K*/
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM12, table);/*Moidfy to 8K*/

    /*UserIdTunnelMpls AD*/
    table = DRV_FTM_SRAM_TBL_USERID_AD;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM13, table);

    if(couple_mode)
    {
        /*Oam Mep*/
        table = DRV_FTM_SRAM_TBL_OAM_MEP;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 0, (8 * 1024*2));
        /*Oam Ma*/
        table = DRV_FTM_SRAM_TBL_OAM_MA;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, (8 * 1024*2), (2 * 1024*2));
        /*Oam MaName*/
        table = DRV_FTM_SRAM_TBL_OAM_MA_NAME;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, (10 * 1024*2), (4 * 1024*2));
        /*Oam Lm*/
        table = DRV_FTM_SRAM_TBL_OAM_LM;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, (14 * 1024*2), (2 * 1024*2));
        /*Oam APS*/
        table = DRV_FTM_SRAM_TBL_OAM_APS;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, (16 * 1024*2), (4 * 1024*2));
    }
    else
    {
        /*Oam Mep*/
        table = DRV_FTM_SRAM_TBL_OAM_MEP;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 0, 8 * 1024);
        /*Oam Ma*/
        table = DRV_FTM_SRAM_TBL_OAM_MA;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 8 * 1024, 2 * 1024);
        /*Oam MaName*/
        table = DRV_FTM_SRAM_TBL_OAM_MA_NAME;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 10 * 1024, 4 * 1024);
        /*Oam Lm*/
        table = DRV_FTM_SRAM_TBL_OAM_LM;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 14 * 1024, 2 * 1024);
        /*Oam APS*/
        table = DRV_FTM_SRAM_TBL_OAM_APS;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM14, table, 16 * 1024, 4 * 1024);
    }

    /*DsEdit*/
    table = DRV_FTM_SRAM_TBL_EDIT;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM15, table); /*Moidfy to 16K*/

    if(couple_mode)
    {
        /*DsNexthop*/
        table = DRV_FTM_SRAM_TBL_NEXTHOP;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM16, table, 0, (24 * 1024*2));
        /*DsMet*/
        table = DRV_FTM_SRAM_TBL_MET;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM16, table, (24 * 1024 *2), (24 * 1024*2));
    }
    else
    {
        /*DsNexthop*/
        table = DRV_FTM_SRAM_TBL_NEXTHOP;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM16, table, 0, 24 * 1024);
        /*DsMet*/
        table = DRV_FTM_SRAM_TBL_MET;
        DRV_FTM_ADD_TABLE(DRV_FTM_SRAM16, table, 24 * 1024, 24 * 1024);
    }



    /*XcOamHash */
    table = DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY;
    DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM17, table);


    /*IpFix */
    table = DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY;
     /*DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM3, table);*/
    DRV_FTM_ADD_TABLE(DRV_FTM_SRAM3, table, 0, (2*DRV_FTM_MEM_MAX_ENTRY_NUM(DRV_FTM_SRAM3)));

    /*IpFixAd */
    table = DRV_FTM_SRAM_TBL_IPFIX_AD;
     /*DRV_FTM_ADD_FULL_TABLE(DRV_FTM_SRAM7, table);*/
    DRV_FTM_ADD_TABLE(DRV_FTM_SRAM7, table, 0, (2*DRV_FTM_MEM_MAX_ENTRY_NUM(DRV_FTM_SRAM7)));

    /********************************************
   tcam table
    *********************************************/
    table = DRV_FTM_TCAM_TYPE_IGS_ACL0;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY6, table);

    table = DRV_FTM_TCAM_TYPE_IGS_ACL1;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY5, table);

    table = DRV_FTM_TCAM_TYPE_IGS_ACL2;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY2, table);

    table = DRV_FTM_TCAM_TYPE_IGS_ACL3;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY1, table);

    table = DRV_FTM_TCAM_TYPE_EGS_ACL0;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY4, table);

    table = DRV_FTM_TCAM_TYPE_IGS_USERID0;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY3, table);

    table = DRV_FTM_TCAM_TYPE_IGS_USERID1;
    DRV_FTM_ADD_FULL_KEY(DRV_FTM_TCAM_KEY0, table);

    table = DRV_FTM_TCAM_TYPE_IGS_LPM0;
    DRV_FTM_ADD_LPM0_KEY(DRV_FTM_TCAM_KEY7, table);

    table = DRV_FTM_TCAM_TYPE_IGS_LPM1;
    DRV_FTM_ADD_LPM1_KEY(DRV_FTM_TCAM_KEY8, table);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_key_id_info(uint8 key_id,uint8 key_size, uint8* drv_key_id, uint8* key_type)
{

    /* convert ctc size to drv size*/
    switch(key_size)
    {
        case DRV_FTM_KEY_SIZE_160_BIT:
            *key_type = DRV_FTM_TCAM_SIZE_160;
            break;
        case DRV_FTM_KEY_SIZE_320_BIT:
            *key_type = DRV_FTM_TCAM_SIZE_320;
            break;
        case DRV_FTM_KEY_SIZE_640_BIT:
            *key_type = DRV_FTM_TCAM_SIZE_640;
            break;
        default:
            break;
    }
    /*convert ctc id to drv id*/
    switch(key_id)
    {
        case DRV_FTM_KEY_TYPE_SCL0:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_USERID0;
            break;
        case DRV_FTM_KEY_TYPE_SCL1:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_USERID1;
            break;

        case DRV_FTM_KEY_TYPE_ACL0:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_ACL0;
            break;

        case DRV_FTM_KEY_TYPE_ACL1:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_ACL1;
            break;

        case DRV_FTM_KEY_TYPE_ACL2:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_ACL2;
            break;

        case DRV_FTM_KEY_TYPE_ACL3:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_ACL3;
            break;

        case DRV_FTM_KEY_TYPE_ACL0_EGRESS:
            *drv_key_id = DRV_FTM_TCAM_TYPE_EGS_ACL0;
            break;

        case DRV_FTM_KEY_TYPE_IPV4_UCAST:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM0;
            *key_type   = DRV_FTM_TCAM_SIZE_LPM40;
            break;
        case DRV_FTM_KEY_TYPE_IPV6_UCAST:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM0;
            *key_type   = DRV_FTM_TCAM_SIZE_LPM160;
            break;

        case DRV_FTM_KEY_TYPE_IPV4_NAT:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM1;
            *key_type   = DRV_FTM_TCAM_SIZE_NAT0;
            break;
        case DRV_FTM_KEY_TYPE_IPV6_NAT:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM1;
            *key_type   = DRV_FTM_TCAM_SIZE_NAT1;
            break;
        case DRV_FTM_KEY_TYPE_IPV4_PBR:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM1;
            *key_type   = DRV_FTM_TCAM_SIZE_PBR0;
            break;
        case DRV_FTM_KEY_TYPE_IPV6_PBR:
            *drv_key_id = DRV_FTM_TCAM_TYPE_IGS_LPM1;
            *key_type   = DRV_FTM_TCAM_SIZE_PBR1;
            break;
        default:
            break;
    }

    return DRV_E_NONE;
}


STATIC int32
_drv_goldengate_ftm_get_tbl_id(uint8 tbl_id,uint8* drv_tbl_id)
{
    switch(tbl_id)
    {
        case DRV_FTM_TBL_TYPE_LPM_PIPE0:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_LPM_LKP_KEY;
            break;
        case DRV_FTM_TBL_TYPE_NEXTHOP:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_NEXTHOP;
            break;

        case DRV_FTM_TBL_TYPE_FWD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_FWD;
            break;

        case DRV_FTM_TBL_TYPE_MET:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_MET;
            break;

        case DRV_FTM_TBL_TYPE_EDIT:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_EDIT;
            break;

        case DRV_FTM_TBL_TYPE_OAM_MEP:
            /*
            DRV_FTM_SRAM_TBL_OAM_MEP,
            DRV_FTM_SRAM_TBL_OAM_MA,
            DRV_FTM_SRAM_TBL_OAM_MA_NAME,*/
            break;

        case DRV_FTM_TBL_TYPE_STATS:
            break;

        case DRV_FTM_TBL_TYPE_LM:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_OAM_LM;
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_USERID_HASH_KEY;
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_AD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_USERID_AD;
            break;

        case DRV_FTM_TBL_FIB0_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_FIB0_HASH_KEY;
            break;
        case DRV_FTM_TBL_DSMAC_AD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_DSMAC_AD;
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_FIB1_HASH_KEY;
            break;

        case DRV_FTM_TBL_DSIP_AD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_DSIP_AD;
            break;


        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY;
            break;
        case DRV_FTM_TBL_FLOW_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_FLOW_HASH_KEY;
            break;
        case DRV_FTM_TBL_FLOW_AD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_FLOW_AD;
            break;
        case DRV_FTM_TBL_IPFIX_HASH_KEY:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY;
            break;

        case DRV_FTM_TBL_IPFIX_AD:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_IPFIX_AD;
            break;
        case DRV_FTM_TBL_OAM_APS:
            *drv_tbl_id = DRV_FTM_SRAM_TBL_OAM_APS;
            break;

        default:
            break;
    }
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_process_oam_tbl(drv_ftm_tbl_info_t *p_oam_tbl_info)
{

    uint32 bitmap = 0;
    uint8 bit_offset = 0;
    uint32 total_size = 0;

    uint32 tbl_ids[] = {DRV_FTM_SRAM_TBL_OAM_MEP, DRV_FTM_SRAM_TBL_OAM_MA, DRV_FTM_SRAM_TBL_OAM_MA_NAME};
    uint32 tbl_size[] = {0, 0, 0};
    uint8  idx  = 0;

    uint32 couple_mode      = 0;
    uint32 mem_entry_num    = 0;
    uint32 mem_start_offset = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    bitmap = p_oam_tbl_info->mem_bitmap;
    for (bit_offset = 0 ; bit_offset < 32; bit_offset++)
    {
        if(IS_BIT_SET(bitmap, bit_offset))
        {
            if(couple_mode)
            {
                total_size += (p_oam_tbl_info->mem_entry_num[bit_offset]*2);
            }
            else
            {
                total_size += p_oam_tbl_info->mem_entry_num[bit_offset];
            }
        }
    }
    tbl_size[0] = total_size/7*4;   /*MEP*/
    tbl_size[1] = total_size/7*1;   /*MA*/
    tbl_size[2] = total_size/7*2;   /*MANAME*/

    bit_offset = 0;
    while (bit_offset < DRV_FTM_MEM_MAX)
    {
        if(IS_BIT_SET(bitmap, bit_offset))
        {
            mem_entry_num       = p_oam_tbl_info->mem_entry_num[bit_offset];
            mem_start_offset    = p_oam_tbl_info->mem_start_offset[bit_offset];
            if (couple_mode)
            {
                mem_entry_num       *= 2;
            }

            if (tbl_size[idx] == mem_entry_num)
            {
                DRV_FTM_ADD_TABLE(bit_offset, tbl_ids[idx], mem_start_offset, mem_entry_num);
                idx++;
                bit_offset ++;
            }
            else if (tbl_size[idx] > mem_entry_num)
            {
                DRV_FTM_ADD_TABLE(bit_offset, tbl_ids[idx], mem_start_offset, mem_entry_num);
                tbl_size[idx] -= mem_entry_num;
                bit_offset ++;
            }
            else
            {
                DRV_FTM_ADD_TABLE(bit_offset, tbl_ids[idx], mem_start_offset, tbl_size[idx]);

                p_oam_tbl_info->mem_start_offset[bit_offset] += tbl_size[idx];
                p_oam_tbl_info->mem_entry_num[bit_offset]    -= tbl_size[idx];
                idx++;
            }

            if(idx >= 3)
            {
                break;
            }
        }
        else
        {
            bit_offset ++;
        }
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_set_profile_user_define_edram(drv_ftm_profile_info_t *profile_info)
{
    uint8 tbl_index     = 0;
    uint8 tbl_info_num  = 0;
    uint8 tbl_id        = 0;

    uint32 bitmap       = 0;
    uint8 mem_id        = 0;
    uint32 offset       = 0;
    uint32 entry_size   = 0;
    uint32 couple_mode  = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    /********************************************
    dynamic edram table
    *********************************************/
    tbl_info_num = profile_info->tbl_info_size;
    for(tbl_index = 0; tbl_index < tbl_info_num; tbl_index++)
    {
        bitmap = profile_info->tbl_info[tbl_index].mem_bitmap;
        if (DRV_FTM_TBL_TYPE_OAM_MEP == profile_info->tbl_info[tbl_index].tbl_id)
        {/*oam mep*/
            _drv_goldengate_ftm_process_oam_tbl(&profile_info->tbl_info[tbl_index]);
        }
        else
        {
            _drv_goldengate_ftm_get_tbl_id(profile_info->tbl_info[tbl_index].tbl_id,&tbl_id);

            for (mem_id = 0; mem_id < DRV_FTM_SRAM_MAX; mem_id++)
            {
                if(IS_BIT_SET(bitmap, mem_id))
                {
                    offset       = profile_info->tbl_info[tbl_index].mem_start_offset[mem_id];
                    entry_size   = profile_info->tbl_info[tbl_index].mem_entry_num[mem_id];
                    if((DRV_FTM_TBL_IPFIX_HASH_KEY == profile_info->tbl_info[tbl_index].tbl_id)
                        || (DRV_FTM_TBL_IPFIX_AD == profile_info->tbl_info[tbl_index].tbl_id))
                    {
                        {
                            entry_size *= 2;
                            offset     *= 2;
                        }
                    }
                    else if(couple_mode)
                    {
                        entry_size *= 2;
                        offset     *= 2;
                    }

                    DRV_FTM_ADD_TABLE(mem_id, tbl_id, offset, entry_size);
                }
            }
        }

    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_set_profile_user_define_tcam(drv_ftm_profile_info_t *profile_info)
{
    uint8 key_idx       = 0;
    uint8 key_info_num  = 0;
    uint32 bitmap       = 0;
    uint8 mem_id        = 0;

    uint8  key_id       = 0;
    uint8 key_type      = 0;

    uint32 offset       = 0;
    uint32 entry_size   = 0;
    uint32 couple_mode  = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));
    /********************************************
    tcam table
    *********************************************/
    key_info_num = profile_info->key_info_size;
    for(key_idx = 0; key_idx < key_info_num; key_idx++)
    {
        bitmap = profile_info->key_info[key_idx].tcam_bitmap;
        _drv_goldengate_ftm_get_key_id_info(profile_info->key_info[key_idx].key_id,
                                profile_info->key_info[key_idx].key_size,
                                &key_id, &key_type);
        for(mem_id = DRV_FTM_TCAM_KEY0; mem_id < DRV_FTM_TCAM_KEYM; mem_id++)
        {
            if(IS_BIT_SET(bitmap, (mem_id - DRV_FTM_TCAM_KEY0)))
            {
                offset       = profile_info->key_info[key_idx].tcam_start_offset[mem_id - DRV_FTM_TCAM_KEY0];
                entry_size   = profile_info->key_info[key_idx].tcam_entry_num[mem_id - DRV_FTM_TCAM_KEY0];
                if (couple_mode && (DRV_FTM_TCAM_KEY8 != mem_id))
                {
                    entry_size *= 2;
                    offset     *= 2;
                }
                DRV_FTM_ADD_KEY(mem_id, key_id, 0, entry_size);

                DRV_FTM_ADD_KEY_BYTYPE(mem_id, key_id, key_type, offset, entry_size);
            }
        }
         /*SYS_FTM_DBG_DUMP("key_id = %d, max_index = %d\n", key_id, SYS_FTM_KEY_MAX_INDEX(key_id));*/
    }

    return DRV_E_NONE;

}

STATIC int32
_drv_goldengate_ftm_set_profile_user_define(drv_ftm_profile_info_t *profile_info)
{

    _drv_goldengate_ftm_set_profile_user_define_edram(profile_info);

    _drv_goldengate_ftm_set_profile_user_define_tcam(profile_info);

    return DRV_E_NONE;
}

STATIC int32
drv_goldengate_ftm_get_tcam_info(uint32 tcam_key_type,
                                 uint32* p_tcam_key_id,
                                 uint32* p_tcam_ad_tbl,
                                 uint8* p_key_share_num,
                                 uint8* p_ad_share_num)
{
    uint8 idx0 = 0;
    uint8 idx1 = 0;

    DRV_PTR_VALID_CHECK(p_tcam_key_id);
    DRV_PTR_VALID_CHECK(p_tcam_ad_tbl);
    DRV_PTR_VALID_CHECK(p_key_share_num);
    DRV_PTR_VALID_CHECK(p_ad_share_num);

    switch (tcam_key_type)
    {
    case DRV_FTM_TCAM_TYPE_IGS_ACL0:

        p_tcam_key_id[idx0++] = DsAclQosL3Key160Ingr0_t;
        p_tcam_key_id[idx0++] = DsAclQosMacKey160Ingr0_t;
        p_tcam_key_id[idx0++] = DsAclQosL3Key320Ingr0_t;
        p_tcam_key_id[idx0++] = DsAclQosIpv6Key640Ingr0_t;

        p_tcam_ad_tbl[idx1++] = DsAcl0Ingress_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl0L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl0Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl0L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl0Ipv6640TcamAd_t;
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL1:
        p_tcam_key_id[idx0++] = DsAclQosL3Key160Ingr1_t;
        p_tcam_key_id[idx0++] = DsAclQosMacKey160Ingr1_t;
        p_tcam_key_id[idx0++] = DsAclQosL3Key320Ingr1_t;
        p_tcam_key_id[idx0++] = DsAclQosIpv6Key640Ingr1_t;

        p_tcam_ad_tbl[idx1++] = DsAcl1Ingress_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl1L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl1Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl1L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl1Ipv6640TcamAd_t;
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL2:
        p_tcam_key_id[idx0++] = DsAclQosL3Key160Ingr2_t;
        p_tcam_key_id[idx0++] = DsAclQosMacKey160Ingr2_t;
        p_tcam_key_id[idx0++] = DsAclQosL3Key320Ingr2_t;
        p_tcam_key_id[idx0++] = DsAclQosIpv6Key640Ingr2_t;

        p_tcam_ad_tbl[idx1++] = DsAcl2Ingress_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl2L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl2Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl2L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl2Ipv6640TcamAd_t;
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL3:
        p_tcam_key_id[idx0++] = DsAclQosL3Key160Ingr3_t;
        p_tcam_key_id[idx0++] = DsAclQosMacKey160Ingr3_t;
        p_tcam_key_id[idx0++] = DsAclQosL3Key320Ingr3_t;
        p_tcam_key_id[idx0++] = DsAclQosIpv6Key640Ingr3_t;

        p_tcam_ad_tbl[idx1++] = DsAcl3Ingress_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl3L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl3Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl3L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsIngAcl3Ipv6640TcamAd_t;
        break;

    case DRV_FTM_TCAM_TYPE_EGS_ACL0:
        p_tcam_key_id[idx0++] = DsAclQosL3Key160Egr0_t;
        p_tcam_key_id[idx0++] = DsAclQosMacKey160Egr0_t;
        p_tcam_key_id[idx0++] = DsAclQosL3Key320Egr0_t;
        p_tcam_key_id[idx0++] = DsAclQosIpv6Key640Egr0_t;

        p_tcam_ad_tbl[idx1++] = DsAcl0Egress_t;
        p_tcam_ad_tbl[idx1++] = DsEgrAcl0L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsEgrAcl0Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsEgrAcl0L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsEgrAcl0Ipv6640TcamAd_t;
        break;


    case DRV_FTM_TCAM_TYPE_IGS_USERID0:
        p_tcam_key_id[idx0++] = DsUserId0L3Key160_t;
        p_tcam_key_id[idx0++] = DsUserId0MacKey160_t;
        p_tcam_key_id[idx0++] = DsUserId0L3Key320_t;
        p_tcam_key_id[idx0++] = DsUserId0Ipv6Key640_t;

        p_tcam_ad_tbl[idx1++] = DsUserId0Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsUserId0L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId0Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId0L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId0Ipv6640TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelId0Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsSclFlow0Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdRpfTcam0_t;

        break;

    case DRV_FTM_TCAM_TYPE_IGS_USERID1:
        p_tcam_key_id[idx0++] = DsUserId1L3Key160_t;
        p_tcam_key_id[idx0++] = DsUserId1MacKey160_t;
        p_tcam_key_id[idx0++] = DsUserId1L3Key320_t;
        p_tcam_key_id[idx0++] = DsUserId1Ipv6Key640_t;

        p_tcam_ad_tbl[idx1++] = DsUserId1Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsUserId1Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsUserId1L3160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId1Mac160TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId1L3320TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsUserId1Ipv6640TcamAd_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelId1Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsSclFlow1Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdRpfTcam1_t;
        break;


    case DRV_FTM_TCAM_TYPE_IGS_LPM0:
        p_tcam_key_id[idx0++] = DsLpmTcamIpv440Key_t;
        p_tcam_key_id[idx0++] = DsLpmTcamIpv6160Key0_t;

         /*p_tcam_ad_tbl[idx1++] = DsLpmTcamAd0_t;*/
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv4Route40Ad_t;
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv4Route160Ad_t;
        break;

    case DRV_FTM_TCAM_TYPE_IGS_LPM1:
         /*p_tcam_key_id[idx0++] = DsLpmTcamIpv4160Key_t;*/
        p_tcam_key_id[idx0++] = DsLpmTcamIpv4Pbr160Key_t;   /*PBR*/
        p_tcam_key_id[idx0++] = DsLpmTcamIpv4NAT160Key_t;   /*NAT*/
        p_tcam_key_id[idx0++] = DsLpmTcamIpv6160Key1_t;     /*NAT*/
        p_tcam_key_id[idx0++] = DsLpmTcamIpv6320Key_t;      /*PBR*/

         /*p_tcam_ad_tbl[idx1++] = DsLpmTcamAd1_t;*/
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv4Pbr160Ad_t;
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv4Nat160Ad_t;
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv6Nat160Ad_t;
        p_tcam_ad_tbl[idx1++] = DsLpmTcamIpv6Pbr320Ad_t;

        break;

    default:
        return DRV_E_INVAILD_TYPE;
    }

    *p_key_share_num = idx0;
    *p_ad_share_num  = idx1;

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_sram_tbl_name(uint32 sram_type, char* sram_tbl_id_str)
{
    char* str[]={
        "FibHost0HashKey",
        "FibHost1HashKey",
        "FlowHashKey",
        "IpfixHashKey",
        "SclHashKey",
        "XoamHashKey",
        "LpmLookupKey",
        "DsMacAd",
        "DsIpAd",
        "DsFlowAd",
        "DsIpfixAd",
        "DsSclAd",
        "DsNexthop",
        "DsFwd",
        "DsMet",
        "DsEdit",
        "DsAps",
        "DsOamLm",
        "DsOamMep",
        "DsOamMa",
        "DsOamMaName"
    };

    DRV_PTR_VALID_CHECK(sram_tbl_id_str);
    if (sram_type >= DRV_FTM_SRAM_TBL_MAX)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    sal_strcpy(sram_tbl_id_str, str[sram_type]);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_sram_tbl_id(uint32 sram_type,
                                   uint32* p_sram_tbl_id,
                                   uint8* p_share_num)
{
    uint8 idx = 0;

    DRV_PTR_VALID_CHECK(p_sram_tbl_id);
    DRV_PTR_VALID_CHECK(p_share_num);

    switch (sram_type)
    {
    case DRV_FTM_SRAM_TBL_FIB0_HASH_KEY:
        p_sram_tbl_id[idx++] = DsFibHost0Ipv4HashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0Ipv6UcastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0Ipv6McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0MacIpv6McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0MacHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0TrillHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost0FcoeHashKey_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_FIB1_HASH_KEY:
        p_sram_tbl_id[idx++] = DsFibHost1Ipv4McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1Ipv4NatDaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1Ipv4NatSaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1Ipv6McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1MacIpv4McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1MacIpv6McastHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1Ipv6NatDaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1Ipv6NatSaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1FcoeRpfHashKey_t;
        p_sram_tbl_id[idx++] = DsFibHost1TrillMcastVlanHashKey_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_FLOW_HASH_KEY:
        p_sram_tbl_id[idx++] = DsFlowL2HashKey_t;
        p_sram_tbl_id[idx++] = DsFlowL2L3HashKey_t;
        p_sram_tbl_id[idx++] = DsFlowL3Ipv4HashKey_t;
        p_sram_tbl_id[idx++] = DsFlowL3Ipv6HashKey_t;
        p_sram_tbl_id[idx++] = DsFlowL3MplsHashKey_t;
 /*        p_sram_tbl_id[idx++] = DsMicroFlowHashKey_t;*/
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY:
        p_sram_tbl_id[idx++] = DsIpfixL2HashKey_t;
        p_sram_tbl_id[idx++] = DsIpfixL2L3HashKey_t;
        p_sram_tbl_id[idx++] = DsIpfixL3Ipv4HashKey_t;
        p_sram_tbl_id[idx++] = DsIpfixL3Ipv6HashKey_t;
        p_sram_tbl_id[idx++] = DsIpfixL3MplsHashKey_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_USERID_HASH_KEY:
        p_sram_tbl_id[idx++] = DsUserIdCvlanCosPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdCvlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdDoubleVlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv4PortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv4SaHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv6PortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv6SaHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdMacHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdMacPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanCosPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanMacSaHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSclFlowL2HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4DaHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4GreKeyHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4RpfHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4UdpHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4McNvgreMode0HashKey_t;
 /*        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4McNvgreMode1HashKey_t;*/
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4McVxlanMode0HashKey_t;
 /*        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4McVxlanMode1HashKey_t;*/
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4NvgreMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4VxlanMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4UcNvgreMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4UcNvgreMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4UcVxlanMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv4UcVxlanMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6McNvgreMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6McNvgreMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6McVxlanMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6McVxlanMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6UcNvgreMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6UcNvgreMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6UcVxlanMode0HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelIpv6UcVxlanMode1HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelMplsHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelPbbHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelTrillMcAdjHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelTrillMcDecapHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelTrillMcRpfHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelTrillUcDecapHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdTunnelTrillUcRpfHashKey_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY:
        p_sram_tbl_id[idx++] = DsEgressXcOamPortVlanCrossHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamCvlanCosPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamPortCrossHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamCvlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamDoubleVlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamSvlanCosPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamSvlanPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamTunnelPbbHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamBfdHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamEthHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamMplsLabelHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamMplsSectionHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamRmepHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamPortHashKey_t;
        p_sram_tbl_id[idx++] = DsEgressXcOamSvlanPortMacHashKey_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_LPM_LKP_KEY:
        p_sram_tbl_id[idx++] = DsLpmLookupKey_t;
        break;

    case DRV_FTM_SRAM_TBL_DSMAC_AD:
        p_sram_tbl_id[idx++] = DsMac_t;
        break;

    case DRV_FTM_SRAM_TBL_DSIP_AD:
        p_sram_tbl_id[idx++] = DsIpDa_t;
        p_sram_tbl_id[idx++] = DsIpSaNat_t;
        p_sram_tbl_id[idx++] = DsFcoeDa_t;
        p_sram_tbl_id[idx++] = DsFcoeSa_t;
        p_sram_tbl_id[idx++] = DsTrillDa_t;
        break;

    case DRV_FTM_SRAM_TBL_FLOW_AD:
        p_sram_tbl_id[idx++] = DsAcl_t;
        break;

    case DRV_FTM_SRAM_TBL_IPFIX_AD:
        p_sram_tbl_id[idx++] = DsIpfixSessionRecord_t;
        break;

    case DRV_FTM_SRAM_TBL_USERID_AD:
        p_sram_tbl_id[idx++] = DsTunnelId_t;
        p_sram_tbl_id[idx++] = DsUserId_t;
        p_sram_tbl_id[idx++] = DsSclFlow_t;
        p_sram_tbl_id[idx++] = DsMpls_t;
        p_sram_tbl_id[idx++] = DsTunnelIdRpf_t;
        break;

    case DRV_FTM_SRAM_TBL_NEXTHOP:
        p_sram_tbl_id[idx++] = DsNextHop4W_t;
        p_sram_tbl_id[idx++] = DsNextHop8W_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_FWD:
        p_sram_tbl_id[idx++] = DsFwd_t;
        break;

    case DRV_FTM_SRAM_TBL_MET:
        p_sram_tbl_id[idx++] = DsMetEntry3W_t;
        p_sram_tbl_id[idx++] = DsMetEntry6W_t;
        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_EDIT:
        p_sram_tbl_id[idx++] = DsL23Edit3W_t;
        p_sram_tbl_id[idx++] = DsL23Edit12W_t;
        p_sram_tbl_id[idx++] = DsL23Edit6W_t;
        p_sram_tbl_id[idx++] = DsL2EditEth3W_t;
        p_sram_tbl_id[idx++] = DsL2EditEth6W_t;
        p_sram_tbl_id[idx++] = DsL2EditInnerSwap_t;
        p_sram_tbl_id[idx++] = DsL2EditFlex_t;
        p_sram_tbl_id[idx++] = DsL2EditLoopback_t;
        p_sram_tbl_id[idx++] = DsL2EditPbb4W_t;
        p_sram_tbl_id[idx++] = DsL2EditPbb8W_t;
        p_sram_tbl_id[idx++] = DsL2EditSwap_t;
        p_sram_tbl_id[idx++] = DsL2EditOf_t;
        p_sram_tbl_id[idx++] = DsL3EditFlex_t;
        p_sram_tbl_id[idx++] = DsL3EditLoopback_t;
        p_sram_tbl_id[idx++] = DsL3EditMpls3W_t;
        p_sram_tbl_id[idx++] = DsL3EditNat3W_t;
        p_sram_tbl_id[idx++] = DsL3EditNat6W_t;
        p_sram_tbl_id[idx++] = DsL3EditOf6W_t;
        p_sram_tbl_id[idx++] = DsL3EditOf12W_t;
        p_sram_tbl_id[idx++] = DsL3EditTrill_t;
        p_sram_tbl_id[idx++] = DsL3EditTunnelV4_t;
        p_sram_tbl_id[idx++] = DsL3EditTunnelV6_t;
        p_sram_tbl_id[idx++] = DsL3Edit12W1st_t;
        p_sram_tbl_id[idx++] = DsL3Edit12W2nd_t;
        p_sram_tbl_id[idx++] = DsL2Edit12WInner_t;
        p_sram_tbl_id[idx++] = DsL2Edit12WShare_t;
        p_sram_tbl_id[idx++] = DsL2Edit6WShare_t;
        p_sram_tbl_id[idx++] = TempInnerEdit12W_t;
        p_sram_tbl_id[idx++] = TempOuterEdit12W_t;

        DRV_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    case DRV_FTM_SRAM_TBL_OAM_APS:
        p_sram_tbl_id[idx++] = DsApsBridge_t;
        break;

    case DRV_FTM_SRAM_TBL_OAM_LM:
        p_sram_tbl_id[idx++] = DsOamLmStats_t;
        break;

    case DRV_FTM_SRAM_TBL_OAM_MEP:
        p_sram_tbl_id[idx++] = DsBfdMep_t;
        p_sram_tbl_id[idx++] = DsBfdRmep_t;
        p_sram_tbl_id[idx++] = DsEthMep_t;
        p_sram_tbl_id[idx++] = DsEthRmep_t;
        break;

    case DRV_FTM_SRAM_TBL_OAM_MA:
        p_sram_tbl_id[idx++] = DsMa_t;
        break;

    case DRV_FTM_SRAM_TBL_OAM_MA_NAME:
        p_sram_tbl_id[idx++] = DsMaName_t;
        break;


    default:
        return DRV_E_INVAILD_TYPE;

    }

    *p_share_num = idx;

    return DRV_E_NONE;

}

STATIC int32
_sys_goldengate_ftm_get_edram_bitmap(uint8 sram_type,
                                     uint32* bitmap,
                                     uint32* sram_array)
{
    uint32 bit_map_temp = 0;
    uint32 mem_bit_map  = 0;
    uint8 mem_id        = 0;
    uint8 idx           = 0;
    uint8 bit[DRV_FTM_MAX_ID]     = {0};

    switch (sram_type)
    {
        /* SelEdram  {edram2, edram1, edram0, edram6, edram5 } */
        case DRV_FTM_SRAM_TBL_LPM_LKP_KEY:
        bit[DRV_FTM_SRAM5] = 0;
        bit[DRV_FTM_SRAM6] = 1;
        bit[DRV_FTM_SRAM0] = 2;
        bit[DRV_FTM_SRAM1] = 3;
        bit[DRV_FTM_SRAM2] = 4;
        break;

        /* SelEdram  {edram4, edram3, edram2, edram1, edram0 } */
        case DRV_FTM_SRAM_TBL_FIB0_HASH_KEY:
        bit[DRV_FTM_SRAM0] = 0;
        bit[DRV_FTM_SRAM1] = 1;
        bit[DRV_FTM_SRAM2] = 2;
        bit[DRV_FTM_SRAM3] = 3;
        bit[DRV_FTM_SRAM4] = 4;
        break;

        /* SelEdram  {edram10, edram9, edram8, edram7 } */
        case DRV_FTM_SRAM_TBL_FLOW_AD:
        bit[DRV_FTM_SRAM7]   = 0;
        bit[DRV_FTM_SRAM8]   = 1;
        bit[DRV_FTM_SRAM9]   = 2;
        bit[DRV_FTM_SRAM10] = 3;
        break;

        /* SelEdram  {edram3} */
        case DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY:
        bit[DRV_FTM_SRAM3] = 0;
        break;

        /* SelEdram  {edram7} */
        case DRV_FTM_SRAM_TBL_IPFIX_AD:
        bit[DRV_FTM_SRAM7] = 0;
        break;

        /* SelEdram  {edram10, edram9, edram8} */
        case DRV_FTM_SRAM_TBL_DSMAC_AD:
        bit[DRV_FTM_SRAM8]   = 0;
        bit[DRV_FTM_SRAM9]   = 1;
        bit[DRV_FTM_SRAM10] = 2;
        break;

        /* SelEdram  {edram10, edram9, edram8, edram7 } */
        case DRV_FTM_SRAM_TBL_DSIP_AD:
        bit[DRV_FTM_SRAM7]   = 0;
        bit[DRV_FTM_SRAM8]   = 1;
        bit[DRV_FTM_SRAM9]   = 2;
        bit[DRV_FTM_SRAM10] = 3;
        break;

        /* SelEdram  {edram15, edram14 } */
        case DRV_FTM_SRAM_TBL_EDIT:
        bit[DRV_FTM_SRAM14] = 0;
        bit[DRV_FTM_SRAM15] = 1;
        break;

        /* SelEdram  {edram17, edram16 } */
        case DRV_FTM_SRAM_TBL_NEXTHOP:
        bit[DRV_FTM_SRAM16] = 0;
        bit[DRV_FTM_SRAM17] = 1;
        break;

        /* SelEdram  {edram17, edram16 } */
        case DRV_FTM_SRAM_TBL_MET:
        bit[DRV_FTM_SRAM16] = 0;
        bit[DRV_FTM_SRAM17] = 1;
        break;

        /* SelEdram  {edram8, edram7, edram17 } */
        case DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY:
        bit[DRV_FTM_SRAM17] = 0;
        bit[DRV_FTM_SRAM7]   = 1;
        bit[DRV_FTM_SRAM8]   = 2;
        break;

        /* SelEdram  {edram12, edram3, edram11, edram2 } */
        case DRV_FTM_SRAM_TBL_USERID_HASH_KEY:
        bit[DRV_FTM_SRAM2]   = 0;
        bit[DRV_FTM_SRAM11] = 1;
        bit[DRV_FTM_SRAM3]   = 2;
        bit[DRV_FTM_SRAM12] = 3;
        break;

        /* SelEdram  {edram13, edram12, edram11 } */
        case DRV_FTM_SRAM_TBL_USERID_AD:
        bit[DRV_FTM_SRAM11] = 0;
        bit[DRV_FTM_SRAM12] = 1;
        bit[DRV_FTM_SRAM13] = 2;
        break;

        /*Need confirm -------------------------*/

        /* SelEdram  { edram0, edram5, edram6} */
        case DRV_FTM_SRAM_TBL_FIB1_HASH_KEY:
        bit[DRV_FTM_SRAM5] = 0;
        bit[DRV_FTM_SRAM6] = 1;
        bit[DRV_FTM_SRAM0] = 2;
        break;

        /* SelEdram  {edram4, edram3, edram2, edram1, edram0 } */
        case DRV_FTM_SRAM_TBL_FLOW_HASH_KEY:
        bit[DRV_FTM_SRAM0] = 0;
        bit[DRV_FTM_SRAM1] = 1;
        bit[DRV_FTM_SRAM2] = 2;
        bit[DRV_FTM_SRAM3] = 3;
        bit[DRV_FTM_SRAM4] = 4;
        break;


         /* SelEdram  {edram14} */
        case DRV_FTM_SRAM_TBL_OAM_APS:
        bit[DRV_FTM_SRAM14] = 0;
        break;

         /* SelEdram  {edram14} */
        case DRV_FTM_SRAM_TBL_OAM_LM:
        bit[DRV_FTM_SRAM14] = 0;
        break;

         /* SelEdram  {edram14} */
        case DRV_FTM_SRAM_TBL_OAM_MEP:
        bit[DRV_FTM_SRAM14] = 0;
        break;

         /* SelEdram  {edram14} */
        case DRV_FTM_SRAM_TBL_OAM_MA:
        bit[DRV_FTM_SRAM14] = 0;
        break;

         /* SelEdram  {edram14} */
        case DRV_FTM_SRAM_TBL_OAM_MA_NAME:
        bit[DRV_FTM_SRAM14] = 0;
        break;

        default:
           break;
    }

    mem_bit_map = DRV_FTM_TBL_MEM_BITMAP(sram_type);

    for (mem_id = DRV_FTM_SRAM0; mem_id < DRV_FTM_SRAM_MAX; mem_id++)
    {
        if (IS_BIT_SET(mem_bit_map, mem_id))
        {
            idx = bit[mem_id];
            SET_BIT(bit_map_temp, idx);
            sram_array[idx] = mem_id;
        }
    }

    *bitmap = bit_map_temp;

    return DRV_E_NONE;;
}

STATIC int32
_sys_goldengate_ftm_get_hash_type(uint8 sram_type, uint32 mem_id,
                                              uint8 couple, uint32 *poly)
{
    uint8 base = 0;

    switch (sram_type)
    {
        case DRV_FTM_SRAM_TBL_FIB0_HASH_KEY:
        /*Host0lkup ctl
         {HASH_CRC, 0x00000005, 11},  0->11
         {HASH_CRC, 0x00000003, 12},  1->12
         {HASH_CRC, 0x0000001b, 13},  2->13
         {HASH_CRC, 0x00000027, 13},  3->13
         {HASH_CRC, 0x000000c3, 13},  4->13
         {HASH_CRC, 0x00000143, 13},  5->13
         {HASH_CRC, 0x0000002b, 14},  6->14
         {HASH_CRC, 0x00000039, 14},  7->14
         {HASH_CRC, 0x000000a9, 14},  8->14
         {HASH_CRC, 0x00000113, 14},  9->14
         {HASH_XOR, 16        , 0},

         DRV_FTM_SRAM0  32K/4=8K  2, 6
         DRV_FTM_SRAM1  32K/4=8K  3, 7
         DRV_FTM_SRAM2  32K/4=8K  4, 8
         DRV_FTM_SRAM3  32K/4=8K  5, 9
         DRV_FTM_SRAM4  08K/4=2K  0, 1
        */
            if (DRV_FTM_SRAM4 == mem_id)
            {
                *poly = couple ? 1: 0;
            }
            else
            {
                base = (mem_id - DRV_FTM_SRAM0);
                *poly =  couple ? (base+6) : (base+ 2);
            }
            break;

        case DRV_FTM_SRAM_TBL_FIB1_HASH_KEY:
        /*Host1lkup ctl
         {HASH_CRC, 0x00000053, 12}, 0->12
         {HASH_CRC, 0x00000107, 12}, 1->12
         {HASH_CRC, 0x0000001b, 13}, 2->13
         {HASH_CRC, 0x00000027, 13}, 3->13
         {HASH_CRC, 0x0000002b, 14}, 4->14
         {HASH_XOR, 16        , 0},

         DRV_FTM_SRAM0  32K/4=8K  2, 4
         DRV_FTM_SRAM5  16K/4=4K  0, 2
         DRV_FTM_SRAM6  16K/4=4K  1, 3
         */
            if (DRV_FTM_SRAM0 == mem_id)
            {
                *poly = couple ? 4: 2;
            }
            else
            {
                base = (mem_id - DRV_FTM_SRAM5);
                *poly = couple ? (base + 2): base;
            }
            break;

        case DRV_FTM_SRAM_TBL_FLOW_HASH_KEY:
        /*Flow lkup ctl
         {HASH_CRC, 0x00000005, 11}, 0->11
         {HASH_CRC, 0x00000003, 12}, 1->12
         {HASH_CRC, 0x0000001b, 13}, 2->13
         {HASH_CRC, 0x00000027, 13}, 3->13
         {HASH_CRC, 0x000000c3, 13}, 4->13
         {HASH_CRC, 0x00000143, 13}, 5->13
         {HASH_CRC, 0x0000002b, 14}, 6->14
         {HASH_CRC, 0x00000039, 14}, 7->14
         {HASH_CRC, 0x000000a9, 14}, 8->14
         {HASH_CRC, 0x00000113, 14}, 9->14

         DRV_FTM_SRAM0  32K/4=8K  2, 6
         DRV_FTM_SRAM1  32K/4=8K  3, 7
         DRV_FTM_SRAM2  32K/4=8K  4, 8
         DRV_FTM_SRAM3  32K/4=8K  5, 9
         DRV_FTM_SRAM4  08K/4=2K  0, 1
        */
            if (DRV_FTM_SRAM4 == mem_id)
            {
                *poly = couple ? 1: 0;
            }
            else
            {
                base = (mem_id - DRV_FTM_SRAM0);
                *poly =  couple ? (base+6) : (base+ 2);
            }
            break;

        case DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY:
        /*
        bit[DRV_FTM_SRAM3] = 0;
        */
            break;

        case DRV_FTM_SRAM_TBL_USERID_HASH_KEY:
        /*userid  lkup ctl
         {HASH_CRC, 0x00000005, 11}, 0->11
         {HASH_CRC, 0x00000201, 11}, 1->11
         {HASH_CRC, 0x00000053, 12}, 2->12
         {HASH_CRC, 0x00000107, 12}, 3->12
         {HASH_CRC, 0x0000001b, 13}, 4->13
         {HASH_CRC, 0x0000008b, 13}, 5->13
         {HASH_CRC, 0x0000002b, 14}, 6->14
         {HASH_CRC, 0x00000113, 14}, 7->14
         {HASH_XOR, 16        , 0},
          DRV_FTM_SRAM2   32K/4=8K  4, 6
          DRV_FTM_SRAM3   32K/4=8K  5, 7
          DRV_FTM_SRAM11  08K/4=2K  0, 2
          DRV_FTM_SRAM12  08K/4=2K  1, 3
        */
            if((DRV_FTM_SRAM2 == mem_id)
                ||(DRV_FTM_SRAM3 == mem_id))
            {
                base = mem_id - DRV_FTM_SRAM2;
                *poly = couple ? (base + 6): (base + 4);
            }
            else if((DRV_FTM_SRAM11 == mem_id)
                ||(DRV_FTM_SRAM12 == mem_id))
            {
                base = mem_id - DRV_FTM_SRAM11;
                *poly = couple ? (base + 2): (base + 0);
            }
            break;

        case DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY:
        /*XcOamUserid  lkup ctl
         {HASH_CRC, 0x00000053, 12}, 0->12
         {HASH_CRC, 0x0000001b, 13}, 1->13
         {HASH_CRC, 0x0000008b, 13}, 2->13
         {HASH_CRC, 0x0000002b, 14}, 3->14
         {HASH_CRC, 0x00000113, 14}, 4->14
         {HASH_XOR, 16        , 0},

          DRV_FTM_SRAM17   32K/4=8K  1, 3
          DRV_FTM_SRAM7    32K/4=8K  2, 4
          DRV_FTM_SRAM8    16K/4=4K  0, 1
        */
            if (DRV_FTM_SRAM17 == mem_id)
            {
                *poly = couple ? 3: 1;
            }
            else if(DRV_FTM_SRAM7 == mem_id)
            {
                *poly = couple ? 3: 2;
            }
            else if(DRV_FTM_SRAM8 == mem_id)
            {
                *poly = couple ? 1: 0;
            }
            break;

    }
    return DRV_E_NONE;;
}


tbls_ext_info_t*
drv_goldengate_ftm_build_dynamic_tbl_info(void)
{
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    dynamic_mem_ext_content_t* p_dyn_mem_ext_info = NULL;

    p_tbl_ext_info = mem_malloc(MEM_FTM_MODULE, sizeof(tbls_ext_info_t));
    if (NULL == p_tbl_ext_info)
    {
        return NULL;
    }

    sal_memset(p_tbl_ext_info, 0, sizeof(tbls_ext_info_t));

    p_dyn_mem_ext_info = mem_malloc(MEM_FTM_MODULE, sizeof(dynamic_mem_ext_content_t));
    if (NULL == p_dyn_mem_ext_info)
    {
        mem_free(p_tbl_ext_info);
        return NULL;
    }

    sal_memset(p_dyn_mem_ext_info, 0, sizeof(dynamic_mem_ext_content_t));

    p_tbl_ext_info->ptr_ext_content = p_dyn_mem_ext_info;

    return p_tbl_ext_info;

}

STATIC int32
drv_goldengate_ftm_alloc_dyn_share_tbl(uint8 sram_type,
                                       uint32 tbl_id)
{
    uint32 bitmap = 0;
    uint32 sram_array[5] = {0};
    uint8   bit_idx = 0;

    uint32 mem_entry_num = 0;
    uint32 max_index_num = 0;
    uint32 start_index = 0;
    uint32 end_index = 0;

    uint32 start_base = 0;
    uint32 end_base = 0;
    uint32 entry_num = 0;

    uint32 hw_base = 0;
    uint32 tbl_offset = 0;
    int8 mem_id = 0;
    uint8 is_mep = 0;

    dynamic_mem_ext_content_t* p_dyn_mem_ext_info = NULL;
    tbls_ext_info_t* p_tbl_ext_info = NULL;

    is_mep      = (DRV_FTM_SRAM_TBL_OAM_MEP == sram_type);

    p_tbl_ext_info = drv_goldengate_ftm_build_dynamic_tbl_info();
    if (NULL == p_tbl_ext_info)
    {
        return DRV_E_NO_MEMORY;
    }

    if (0 == DRV_FTM_TBL_MAX_ENTRY_NUM(sram_type))
    {
        mem_free(p_tbl_ext_info);
        if (p_tbl_ext_info->ptr_ext_content)
        {
            mem_free(p_tbl_ext_info->ptr_ext_content);
        }
        return DRV_E_NONE;
    }

    p_dyn_mem_ext_info = p_tbl_ext_info->ptr_ext_content;


    TABLE_EXT_INFO_PTR(tbl_id) = p_tbl_ext_info;
    TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_DYNAMIC;

    p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_DEFAULT;

    if (DRV_FTM_TBL_ACCESS_FLAG(sram_type))
    {
        if (TABLE_ENTRY_SIZE(tbl_id) == (4 * DRV_BYTES_PER_ENTRY))
        {
            p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_16W_MODE;
        }
        else if (TABLE_ENTRY_SIZE(tbl_id) == (2 * DRV_BYTES_PER_ENTRY))
        {
            p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_8W_MODE;
        }
        else if (TABLE_ENTRY_SIZE(tbl_id) == DRV_BYTES_PER_ENTRY)
        {
            p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_4W_MODE;
        }
    }

    DYNAMIC_BITMAP(tbl_id) = DRV_FTM_TBL_MEM_BITMAP(sram_type);
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    for (bit_idx = 0; bit_idx < 5; bit_idx++)
    {
        if (!IS_BIT_SET(bitmap, bit_idx))
        {
            continue;
        }

        mem_id = sram_array[bit_idx];
        mem_entry_num = DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, mem_id);


#if (SDK_COSIM == 1)
        hw_base = DRV_FTM_MEM_HW_DATA_BASE(mem_id);
#else

        if (TABLE_ENTRY_SIZE(tbl_id) == (4 * DRV_BYTES_PER_ENTRY))
        {
            hw_base = DRV_FTM_MEM_HW_DATA_BASE12W(mem_id);
        }
        else if (TABLE_ENTRY_SIZE(tbl_id) == (2 * DRV_BYTES_PER_ENTRY))
        {
            hw_base = DRV_FTM_MEM_HW_DATA_BASE6W(mem_id);
        }
        else if (TABLE_ENTRY_SIZE(tbl_id) == DRV_BYTES_PER_ENTRY)
        {
            hw_base = DRV_FTM_MEM_HW_DATA_BASE(mem_id);
        }
#endif

        DYNAMIC_ENTRY_NUM(tbl_id, mem_id) = mem_entry_num;
        tbl_offset = DRV_FTM_TBL_MEM_START_OFFSET(sram_type, mem_id);



#if (0 == SDK_WORK_PLATFORM)
        /*
CPU indirect access to mep tables */
        if (is_mep)
        {
            DYNAMIC_DATA_BASE(tbl_id, mem_id, 0) = OAM_DS_MP_DATA_ADDR + tbl_offset * DRV_ADDR_BYTES_PER_ENTRY;
        }
        else
#endif
       {
            is_mep = is_mep;
            DYNAMIC_DATA_BASE(tbl_id, mem_id, 0) = hw_base +tbl_offset* DRV_ADDR_BYTES_PER_ENTRY;
            DYNAMIC_DATA_BASE(tbl_id, mem_id, 1) = hw_base +tbl_offset* DRV_ADDR_BYTES_PER_ENTRY
                                                            -DYNAMIC_SLICE0_ADDR_BASE_OFFSET_TO_DUP;
            DYNAMIC_DATA_BASE(tbl_id, mem_id, 2) = hw_base +tbl_offset* DRV_ADDR_BYTES_PER_ENTRY
                                                            -DYNAMIC_SLICE1_ADDR_BASE_OFFSET_TO_DUP;
        }

        start_index = max_index_num; /*per key index*/
        start_base = entry_num; /*per 80bit base*/
        entry_num += mem_entry_num;

        if (DRV_FTM_TBL_ACCESS_FLAG(sram_type))
        {
            max_index_num += mem_entry_num;
        }
        else
        {
            max_index_num += mem_entry_num * DRV_BYTES_PER_ENTRY / TABLE_ENTRY_SIZE(tbl_id);
        }

        end_index = (max_index_num - 1);
        end_base = entry_num - 1;

        DYNAMIC_START_INDEX(tbl_id, mem_id) = start_index;
        DYNAMIC_END_INDEX(tbl_id, mem_id) = end_index;

        DRV_FTM_TBL_BASE_VAL(sram_type, bit_idx) = tbl_offset;
        DRV_FTM_TBL_BASE_MIN(sram_type, bit_idx) = start_base;
        DRV_FTM_TBL_BASE_MAX(sram_type, bit_idx) = end_base;


    }

    TABLE_MAX_INDEX(tbl_id) = max_index_num;
    DRV_FTM_TBL_BASE_BITMAP(sram_type) = bitmap;


    for (bit_idx = 0; bit_idx < 5; bit_idx++)
    {
        if (IS_BIT_SET(bitmap, bit_idx))
        {
            mem_id = sram_array[bit_idx];
            TABLE_DATA_BASE(tbl_id, 0) = DYNAMIC_DATA_BASE(tbl_id, mem_id, 0);
            break;
        }
    }


    return DRV_E_NONE;
}



STATIC int32
drv_goldengate_ftm_alloc_sram(void)
{
    uint8 index = 0;
    uint32 sram_type = 0;
    uint32 sram_tbl_id = MaxTblId_t;
    uint32 sram_tbl_a[50] = {0};
    uint8 share_tbl_num  = 0;
    uint32 max_entry_num = 0;
    uint32 couple_mode  = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    for (sram_type = 0; sram_type < DRV_FTM_SRAM_TBL_MAX; sram_type++)
    {
        max_entry_num = DRV_FTM_TBL_MAX_ENTRY_NUM(sram_type);
        if (0 == max_entry_num)
        {
            continue;
        }

        _drv_goldengate_ftm_get_sram_tbl_id(sram_type,
                                           sram_tbl_a,
                                           &share_tbl_num);

        for (index = 0; index < share_tbl_num; index++)
        {
            sram_tbl_id = sram_tbl_a[index];

            if (sram_tbl_id >= MaxTblId_t)
            {
                DRV_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", sram_tbl_id);
                return DRV_E_INVAILD_TYPE;
            }

            DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_dyn_share_tbl(sram_type, sram_tbl_id));
        }

        index = 0;

    }

    /*DsFwd need process*/
    if(couple_mode)
    {

    }
    else
    {
        TABLE_MAX_INDEX(DsFwd_t) /= 2;
    }

    return DRV_E_NONE;
}

tbls_ext_info_t*
drv_goldengate_ftm_build_tcam_tbl_info(void)
{
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    tcam_mem_ext_content_t* p_tcam_mem_ext_info = NULL;

    p_tbl_ext_info = mem_malloc(MEM_FTM_MODULE, sizeof(tbls_ext_info_t));
    if (NULL == p_tbl_ext_info)
    {
        return NULL;
    }

    sal_memset(p_tbl_ext_info, 0, sizeof(tbls_ext_info_t));

    p_tcam_mem_ext_info = mem_malloc(MEM_FTM_MODULE, sizeof(tcam_mem_ext_content_t));
    if (NULL == p_tcam_mem_ext_info)
    {
        mem_free(p_tbl_ext_info);
        return NULL;
    }

    sal_memset(p_tcam_mem_ext_info, 0, sizeof(tcam_mem_ext_content_t));
    p_tcam_mem_ext_info->hw_mask_base =  mem_malloc(MEM_FTM_MODULE, sizeof(addrs_t)*3);
    if (NULL == p_tcam_mem_ext_info->hw_mask_base )
    {
        mem_free(p_tcam_mem_ext_info);
        mem_free(p_tbl_ext_info);
        return NULL;
    }
    p_tbl_ext_info->ptr_ext_content = p_tcam_mem_ext_info;

    return p_tbl_ext_info;

}


STATIC int32
drv_goldengate_ftm_alloc_tcam_key_share_tbl(uint8 tcam_key_type,
                                 uint32 tbl_id)
{
    uint32 max_index_num = 0;
    uint8 mem_id = 0;
    uint8 mem_idx = 0;
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    bool is_lpm0 = 0;
    bool is_lpm1 = 0;
    bool is_lpm = 0;
    uint8 per_entry_bytes = 0;
    uint32 couple_mode    = 0;
    uint32 mem_entry       = 0;

    is_lpm0 = tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0 ;
    is_lpm1 =   tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM1;
    is_lpm = ( is_lpm0 || is_lpm1);

    p_tbl_ext_info = drv_goldengate_ftm_build_tcam_tbl_info();
    if (NULL == p_tbl_ext_info)
    {
        return DRV_E_NO_MEMORY;
    }

    TABLE_EXT_INFO_PTR(tbl_id) = p_tbl_ext_info;
    if (is_lpm0)
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_LPM_TCAM_IP ;
    }
    else if (is_lpm1)
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_LPM_TCAM_NAT ;
    }
    else
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_TCAM ;
    }

    if (TABLE_ENTRY_SIZE(tbl_id) == 0)
    {
        DRV_FTM_DBG_DUMP("tbl_id:%d size is zero!!!\n", tbl_id);
        return DRV_E_NONE;
    }

    TCAM_KEY_SIZE(tbl_id) = TABLE_ENTRY_SIZE(tbl_id);
    per_entry_bytes =         ( is_lpm0)? DRV_LPM_KEY_BYTES_PER_ENTRY:
                                          DRV_BYTES_PER_ENTRY;

    TCAM_BITMAP(tbl_id) = DRV_FTM_KEY_MEM_BITMAP(tcam_key_type);
    if(tcam_key_type < DRV_FTM_TCAM_TYPE_IGS_LPM0)
    {
        drv_goldengate_set_tcam_lookup_bitmap(tcam_key_type, TCAM_BITMAP(tbl_id));
    }

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    for (mem_idx = DRV_FTM_TCAM_KEY0; mem_idx < DRV_FTM_TCAM_KEYM; mem_idx++)
    {
         mem_id = mem_idx - DRV_FTM_TCAM_KEY0;

        /* if share list is no info in the block ,continue */
        if (!IS_BIT_SET(TCAM_BITMAP((tbl_id)), mem_id))
        {
            continue;
        }

        if(couple_mode &&  (DRV_FTM_TCAM_KEY8 != mem_idx))
        {
            mem_entry = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_idx)*2;
        }
        else
        {
            mem_entry = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_idx);
        }

        if (is_lpm)
        {
            max_index_num += (mem_entry / (TABLE_ENTRY_SIZE(tbl_id) / per_entry_bytes));

            TABLE_DATA_BASE(tbl_id, 0) =  DRV_FTM_MEM_HW_DATA_BASE(mem_idx);
            TCAM_MASK_BASE_LPM(tbl_id, 0) =  DRV_FTM_MEM_HW_MASK_BASE(mem_idx);
        }
        else
        {
            TCAM_DATA_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_addr_offset( mem_id, 0);
            TCAM_MASK_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_addr_offset( mem_id, 1);

            TCAM_ENTRY_NUM(tbl_id, mem_id)  =  mem_entry;
            TCAM_START_INDEX(tbl_id, mem_id) = max_index_num;
            max_index_num += (mem_entry / (TABLE_ENTRY_SIZE(tbl_id) / per_entry_bytes));
            TCAM_END_INDEX(tbl_id, mem_id) = (max_index_num - 1);
        }
    }

    TABLE_MAX_INDEX(tbl_id) = max_index_num;
    DRV_FTM_DBG_DUMP("Key TABLE_MAX_INDEX(%d) = %d\n", tbl_id, max_index_num);

    return DRV_E_NONE;

}

uint32
drv_goldengate_ftm_alloc_tcam_key_map_szietype(uint32 tbl_id)
{
    uint8 new_type = 0;

    switch (tbl_id)
    {
        case DsLpmTcamIpv4Pbr160Key_t:
            new_type = DRV_FTM_TCAM_SIZE_PBR0;
            break;
        case DsLpmTcamIpv6320Key_t:
            new_type = DRV_FTM_TCAM_SIZE_PBR1;
            break;
        case DsLpmTcamIpv4NAT160Key_t:
            new_type = DRV_FTM_TCAM_SIZE_NAT0;
            break;
        case DsLpmTcamIpv6160Key1_t:
            new_type = DRV_FTM_TCAM_SIZE_NAT1;
            break;

        case DsLpmTcamIpv440Key_t:
            new_type = DRV_FTM_TCAM_SIZE_LPM40;
            break;
        case DsLpmTcamIpv6160Key0_t:
            new_type = DRV_FTM_TCAM_SIZE_LPM160;
            break;

        default:
            break;
    }
     return new_type;
}

STATIC uint32
drv_goldengate_ftm_alloc_tcam_ad_map_size(uint32 tbl_id)
{
    uint8 size = 0;

    switch (tbl_id)
    {
        case DsLpmTcamIpv4Pbr160Ad_t:
            size = 1;
            break;
        case DsLpmTcamIpv6Pbr320Ad_t:
            size = 2;
            break;
        case DsLpmTcamIpv4Nat160Ad_t:
            size = 1;
            break;
        case DsLpmTcamIpv6Nat160Ad_t:
            size = 1;
            break;

        case DsLpmTcamIpv4Route40Ad_t:
            size = 1;
            break;
        case DsLpmTcamIpv4Route160Ad_t:
            size = 4;
            break;

        default:
            break;
    }
     return size;
}

STATIC int32
drv_goldengate_ftm_alloc_tcam_key_share_tbl_realloc(uint8 tcam_key_type)
{
    uint32 tbl_id = 0;
   uint8 mem_id = 0;
   uint8 mem_idx = 0;
   uint8 size_type = 0;
   uint32 max_index_num = 0;

    bool is_lpm0 = 0;
    bool is_lpm1 = 0;
    bool is_lpm = 0;
   uint8 per_entry_bytes = 0;
   uint8 per_entry_addr = 0;

    uint8 index =0;
    uint32 tcam_key_a[20] = {0};
    uint32 tcam_ad_a[20] = {0};
    uint8 key_share_num  = 0;
    uint8 ad_share_num  = 0;

   is_lpm0 = tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0 ;
   is_lpm1 =   tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM1;
   is_lpm = ( is_lpm0 || is_lpm1);


    per_entry_bytes =     (is_lpm0)? DRV_LPM_KEY_BYTES_PER_ENTRY:  DRV_BYTES_PER_ENTRY;
    per_entry_addr = (is_lpm0)? DRV_LPM_KEY_BYTES_PER_ENTRY:  DRV_ADDR_BYTES_PER_ENTRY;

    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_get_tcam_info(tcam_key_type,
                                              tcam_key_a,
                                              tcam_ad_a,
                                              &key_share_num,
                                              &ad_share_num));
    /*tcam key alloc*/
    for (index = 0; index < key_share_num; index++)
    {
        tbl_id = tcam_key_a[index];

        if (!is_lpm)
        {
            size_type = TABLE_ENTRY_SIZE(tbl_id) / (per_entry_bytes*4);
        }
        else
        {
            size_type = drv_goldengate_ftm_alloc_tcam_key_map_szietype(tbl_id);
        }

        max_index_num = 0;
        TCAM_BITMAP(tbl_id)  = 0;

        for (mem_idx = DRV_FTM_TCAM_KEY0; mem_idx < DRV_FTM_TCAM_KEYM; mem_idx++)
        {
           uint32 entry_num = 0;
           uint8 entry_size = 0;

           mem_id = mem_idx - DRV_FTM_TCAM_KEY0;
           entry_num =  DRV_FTM_KEY_ENTRY_NUM(tcam_key_type, size_type, mem_id);
           entry_size =  TABLE_ENTRY_SIZE(tbl_id);

           if (0 == entry_num )
           {
               continue;
           }

            if (is_lpm)
            {
                TABLE_DATA_BASE(tbl_id, 0) =  DRV_FTM_MEM_HW_DATA_BASE(mem_idx)+ DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type,  mem_id)  * per_entry_addr;
                TCAM_MASK_BASE_LPM(tbl_id, 0) =  DRV_FTM_MEM_HW_MASK_BASE(mem_idx) + DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, mem_id)  * per_entry_addr;
                TCAM_ENTRY_NUM(tbl_id, mem_id)  =  entry_num;
                max_index_num += (entry_num / (TABLE_ENTRY_SIZE(tbl_id) / per_entry_bytes));
            }
            else
            {
                TCAM_BITMAP(tbl_id)  |= (1 << mem_id);
                TCAM_DATA_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_addr_offset(mem_id, 0) + DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type,  mem_id)  * per_entry_addr;
                TCAM_MASK_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_addr_offset(mem_id, 1) + DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, mem_id)  * per_entry_addr;

                TCAM_ENTRY_NUM(tbl_id, mem_id)  =  entry_num;
                TCAM_START_INDEX(tbl_id, mem_id) = max_index_num;
                max_index_num += ( entry_num / (entry_size / per_entry_bytes));
                TCAM_END_INDEX(tbl_id, mem_id) = (max_index_num - 1);
                DRV_FTM_DBG_DUMP("Tbl:%d,  mem_id:%d, dataBase:0x%x, maskBase:0x%x \n",
                                 tbl_id, mem_id, TCAM_DATA_BASE(tbl_id, mem_id, 0) ,  TCAM_MASK_BASE(tbl_id, mem_id, 0));
            }

        }

        TABLE_MAX_INDEX(tbl_id) = max_index_num;
        DRV_FTM_DBG_DUMP("Key TABLE_MAX_INDEX(%d) = %d\n", tbl_id, max_index_num);

    }

    return DRV_E_NONE;
}




STATIC int32
drv_goldengate_ftm_get_tcam_ad_by_size_type(uint8 tcam_key_type,
                                        uint8 size_type,
                                        uint32 *p_ad_share_tbl,
                                        uint8* p_ad_share_num)
{
    uint8 idx0 = 0;

    DRV_PTR_VALID_CHECK(p_ad_share_tbl);
    DRV_PTR_VALID_CHECK(p_ad_share_num);

    switch (tcam_key_type)
    {
    case DRV_FTM_TCAM_TYPE_IGS_ACL0:
        DRV_FTM_TCAM_ACL_AD_MAP(DsIngAcl0L3160TcamAd_t, DsIngAcl0Mac160TcamAd_t,
                                DsIngAcl0L3320TcamAd_t,
                                DsIngAcl0Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL1:
        DRV_FTM_TCAM_ACL_AD_MAP(DsIngAcl1L3160TcamAd_t, DsIngAcl1Mac160TcamAd_t,
                                DsIngAcl1L3320TcamAd_t,
                                DsIngAcl1Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL2:
        DRV_FTM_TCAM_ACL_AD_MAP(DsIngAcl2L3160TcamAd_t, DsIngAcl2Mac160TcamAd_t,
                                DsIngAcl2L3320TcamAd_t,
                                DsIngAcl2Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_ACL3:
        DRV_FTM_TCAM_ACL_AD_MAP(DsIngAcl3L3160TcamAd_t, DsIngAcl3Mac160TcamAd_t,
                                DsIngAcl3L3320TcamAd_t,
                                DsIngAcl3Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_EGS_ACL0:
        DRV_FTM_TCAM_ACL_AD_MAP(DsEgrAcl0L3160TcamAd_t, DsEgrAcl0Mac160TcamAd_t,
                                DsEgrAcl0L3320TcamAd_t,
                                DsEgrAcl0Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_USERID0:
        DRV_FTM_TCAM_ACL_AD_MAP(DsUserId0L3160TcamAd_t, DsUserId0Mac160TcamAd_t,
                                DsUserId0L3320TcamAd_t,
                                DsUserId0Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_USERID1:
        DRV_FTM_TCAM_ACL_AD_MAP(DsUserId1L3160TcamAd_t, DsUserId1Mac160TcamAd_t,
                                DsUserId1L3320TcamAd_t,
                                DsUserId1Ipv6640TcamAd_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_LPM0:
         DRV_FTM_TCAM_LPM0_AD_MAP(DsLpmTcamIpv4Route40Ad_t,
            DsLpmTcamIpv4Route160Ad_t);
        break;

    case DRV_FTM_TCAM_TYPE_IGS_LPM1:
         DRV_FTM_TCAM_LPM1_AD_MAP(DsLpmTcamIpv4Pbr160Ad_t, DsLpmTcamIpv6Pbr320Ad_t,
                                DsLpmTcamIpv4Nat160Ad_t,
                                DsLpmTcamIpv6Nat160Ad_t);
        break;

    default:
        return DRV_E_INVAILD_TYPE;
    }

    *p_ad_share_num  = idx0;

    return DRV_E_NONE;
}

STATIC int32
drv_goldengate_ftm_alloc_tcam_ad_share_tbl_realloc(uint8 tcam_key_type)
{
    uint32 tbl_id = 0;

   uint8 mem_id = 0;
   uint8 mem_idx = 0;
   uint8 size_type = 0;

   uint32 max_index_num = 0;
   uint32 tcam_ad_a[20] = {0};
   uint8 ad_share_num = 0;
   uint8 index =0;

    bool is_lpm0 = 0;
    bool is_lpm1 = 0;
    bool is_lpm = 0;
   uint8 per_entry_bytes = 0;

   is_lpm0 = tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0 ;
   is_lpm1 =   tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM1;
   is_lpm = ( is_lpm0 || is_lpm1);

   /*tcam key alloc*/

   for (size_type = DRV_FTM_TCAM_SIZE_160; size_type < DRV_FTM_TCAM_SIZE_MAX; size_type++)
   {
       drv_goldengate_ftm_get_tcam_ad_by_size_type(tcam_key_type, size_type, tcam_ad_a,  &ad_share_num );

       for (index = 0; index < ad_share_num; index++)
       {
           max_index_num = 0;
           tbl_id = tcam_ad_a[index];
           TCAM_BITMAP(tbl_id)  = 0;

           for (mem_idx = DRV_FTM_TCAM_AD0; mem_idx < DRV_FTM_TCAM_ADM; mem_idx++)
           {
               uint32 entry_num = 0;
               uint32 offset    = 0;

               mem_id = mem_idx - DRV_FTM_TCAM_AD0;
               entry_num =  DRV_FTM_KEY_ENTRY_NUM(tcam_key_type, size_type, mem_id);

               if (0 == entry_num )
               {
                   continue;
               }
               if (is_lpm)
               {
                   if (tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0)
                   {
                       per_entry_bytes = DRV_LPM_AD0_BYTE_PER_ENTRY;
                       offset = DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, mem_id) ;
                       entry_num = entry_num;
                   }
                   else
                   {
                       per_entry_bytes = DRV_LPM_AD1_BYTE_PER_ENTRY;
                       offset = DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, mem_id)/2;
                       entry_num = entry_num/2;
                   }
                   TABLE_DATA_BASE(tbl_id, 0) =  DRV_FTM_MEM_HW_DATA_BASE(mem_idx) + offset * per_entry_bytes;;
                   max_index_num += (entry_num / ( TABLE_ENTRY_SIZE(tbl_id) / per_entry_bytes));
                   TCAM_KEY_SIZE(tbl_id) =  drv_goldengate_ftm_alloc_tcam_ad_map_size(tbl_id) ;
                   DRV_FTM_DBG_DUMP("TCAM LPM AD TABLEX(%d) DataBase = 0x%x\n", tbl_id, TABLE_DATA_BASE(tbl_id, 0) );
               }
               else
               {
                   TCAM_BITMAP(tbl_id)  |= (1 << mem_id);
                   TCAM_DATA_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_ad_addr_offset(mem_id) + DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, mem_id)  * DRV_ADDR_BYTES_PER_ENTRY;
                   TCAM_ENTRY_NUM(tbl_id, mem_id)  = entry_num;
                   TCAM_START_INDEX(tbl_id, mem_id) = max_index_num;
                   max_index_num +=  (entry_num / ( TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY));
                   TCAM_END_INDEX(tbl_id, mem_id) = (max_index_num - 1);

                   TCAM_KEY_SIZE(tbl_id) = ( (size_type*2)?(size_type*2):1) ;
               }

           }

           TABLE_MAX_INDEX(tbl_id) = max_index_num;
           DRV_FTM_DBG_DUMP("AD TABLE_MAX_INDEX(%d) = %d\n", tbl_id, max_index_num);

       }


   }

    return DRV_E_NONE;
}


STATIC int32
drv_goldengate_ftm_alloc_tcam_ad_share_tbl(uint8 tcam_key_type,
                                 uint32 tbl_id)
{
    uint32 max_index_num = 0;
    int8 mem_id = 0;
    uint8 mem_idx = 0;
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    bool is_lpm0 = 0;
    bool is_lpm1 = 0;
    bool is_lpm = 0;
    uint8 per_entry_bytes = 0;

    uint32 couple_mode    = 0;
    uint32 mem_entry      = 0;

    is_lpm0 = tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0;
    is_lpm1 = tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM1;
    is_lpm = ( is_lpm0 || is_lpm1);

    p_tbl_ext_info = drv_goldengate_ftm_build_tcam_tbl_info();
    if (NULL == p_tbl_ext_info)
    {
      return DRV_E_NO_MEMORY;
    }

    TABLE_EXT_INFO_PTR(tbl_id) = p_tbl_ext_info;

    if (is_lpm0)
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_TCAM_LPM_AD ;
    }
    else if (is_lpm1)
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_TCAM_NAT_AD ;
    }
    else
    {
        TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_TCAM_AD ;
    }

    TCAM_BITMAP(tbl_id) = DRV_FTM_KEY_MEM_BITMAP(tcam_key_type);

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));


    for (mem_idx = DRV_FTM_TCAM_AD0; mem_idx < DRV_FTM_TCAM_ADM; mem_idx++)
    {
         mem_id = mem_idx - DRV_FTM_TCAM_AD0;

        /* if share list is no info in the block ,continue */
        if (!IS_BIT_SET(TCAM_BITMAP(tbl_id),mem_id))
        {
            continue;
        }

        if (couple_mode && (DRV_FTM_TCAM_AD8 != mem_idx))
        {
            mem_entry = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_idx)*2;
        }
        else
        {
            mem_entry = DRV_FTM_MEM_MAX_ENTRY_NUM(mem_idx);
        }

        if (is_lpm)
        {
            per_entry_bytes =     (tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0)? DRV_LPM_AD0_BYTE_PER_ENTRY:
                                                                                DRV_LPM_AD1_BYTE_PER_ENTRY;
            TABLE_DATA_BASE(tbl_id, 0) =  (tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0)? DRV_LPM_IP_AD_BASE_ADDR: DRV_LPM_NAT_AD_BASE_ADDR;
            max_index_num += (mem_entry / ( TABLE_ENTRY_SIZE(tbl_id) / per_entry_bytes));

            TCAM_KEY_SIZE(tbl_id) = 1;
        }
        else
        {
            TCAM_DATA_BASE(tbl_id, mem_id, 0) =  drv_goldengate_get_tcam_ad_addr_offset(mem_id);
            TCAM_ENTRY_NUM(tbl_id, mem_id)  = mem_entry;
            TCAM_START_INDEX(tbl_id, mem_id) = max_index_num;
            max_index_num += (mem_entry / ( TABLE_ENTRY_SIZE(tbl_id) / DRV_BYTES_PER_ENTRY));
            TCAM_END_INDEX(tbl_id, mem_id) = (max_index_num - 1);

            TCAM_KEY_SIZE(tbl_id) = 1;
            DRV_FTM_DBG_DUMP("Ad TCAM_START_INDEX(%d, %d) = %d\n", tbl_id, mem_id, TCAM_START_INDEX(tbl_id, mem_id));
            DRV_FTM_DBG_DUMP("Ad TCAM_END_INDEX(%d, %d) = %d\n", tbl_id, mem_id, TCAM_END_INDEX(tbl_id, mem_id));
        }

    }

    TABLE_MAX_INDEX(tbl_id) = max_index_num;

    DRV_FTM_DBG_DUMP("Ad TCAM_BITMAP(%d) = %d\n", tbl_id, TCAM_BITMAP(tbl_id));
    DRV_FTM_DBG_DUMP("Ad TABLE_MAX_INDEX(%d) = %d\n", tbl_id, TABLE_MAX_INDEX(tbl_id));
    DRV_FTM_DBG_DUMP("----------------------------------------\n");

    return DRV_E_NONE;

}

STATIC int32
drv_goldengate_ftm_alloc_tcam(void)
{
    uint8 index = 0;
    uint32 tcam_key_type = 0;
    uint32 tcam_key_id = MaxTblId_t;
    uint32 tcam_ad_tbl = MaxTblId_t;
    uint32 tcam_key_a[20] = {0};
    uint32 tcam_ad_a[20] = {0};
    uint8 key_share_num  = 0;
    uint8 ad_share_num  = 0;

    for (tcam_key_type = 0; tcam_key_type < DRV_FTM_TCAM_TYPE_MAX; tcam_key_type++)
    {

        DRV_IF_ERROR_RETURN(drv_goldengate_ftm_get_tcam_info(tcam_key_type,
                                                          tcam_key_a,
                                                          tcam_ad_a,
                                                          &key_share_num,
                                                          &ad_share_num));

        if (0 == key_share_num || 0 == ad_share_num)
        {
            continue;
        }

        /*tcam key alloc*/
        for (index = 0; index < key_share_num; index++)
        {
            tcam_key_id = tcam_key_a[index];

            if (tcam_key_id >= MaxTblId_t)
            {
                DRV_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", tcam_key_id);
                return DRV_E_INVAILD_TYPE;
            }

            DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_tcam_key_share_tbl(tcam_key_type, tcam_key_id));
        }

        /*tcam ad alloc*/
        for (index = 0; index < ad_share_num; index++)
        {
            tcam_ad_tbl = tcam_ad_a[index];

            if (tcam_ad_tbl >= MaxTblId_t)
            {
                DRV_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", tcam_ad_tbl);
                return DRV_E_INVAILD_TYPE;
            }

            DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_tcam_ad_share_tbl(tcam_key_type, tcam_ad_tbl));
        }

    }

    return DRV_E_NONE;

}


STATIC int32
drv_goldengate_ftm_realloc_tcam(uint8 lpm0_tcam_share)
{
    uint32 tcam_key_type = 0;

    for (tcam_key_type = DRV_FTM_TCAM_TYPE_IGS_LPM0; tcam_key_type < DRV_FTM_TCAM_TYPE_MAX; tcam_key_type++)
    {
        if (lpm0_tcam_share && (DRV_FTM_TCAM_TYPE_IGS_LPM0 == tcam_key_type))
        {
            continue;
        }
        DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_tcam_key_share_tbl_realloc(tcam_key_type));
        DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_tcam_ad_share_tbl_realloc(tcam_key_type));
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_alloc_cam(drv_ftm_cam_t *p_cam_profile)
{
    uint32 bitmap = 0;

    /*fib acc cam*/
    /*bit
    bit[0]: mac
    bit[1]: ipv4 uc host
    bit[2]: ipv4 l2 mc
    bit[3]: ipv4 mc
    bit[4]: ipv6 uc hot
    bit[5]: ipv6 l2 mc
    bit[6]: ipv6 mc
    bit[7]: fcoe
    bit[8]: trill
    */
    SET_BIT(bitmap, 1);
    SET_BIT(bitmap, 4);
    SET_BIT(bitmap, 7);
    SET_BIT(bitmap, 8);
    if((0 == p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_FDB])
       && (0 == p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_MC]))
    {/*share*/

    }
    else if(FIB_HOST0_CAM_NUM == p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_FDB])
    {
        SET_BIT(bitmap, 2);
        SET_BIT(bitmap, 3);
        SET_BIT(bitmap, 5);
        SET_BIT(bitmap, 6);
    }
    else if(FIB_HOST0_CAM_NUM == p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_MC])
    {
        SET_BIT(bitmap, 0);
    }
    /*
    else if(FIB_HOST0_CAM_NUM == (p_cam_profile->fdb_conflict_num + p_cam_profile->ipmc_g_conflict_num))
    {
        //write valid bit
        drv_fib_acc_in_t* fib_acc_in;
        drv_fib_acc_out_t* fib_acc_out;
        uint8 index = 0;
        ds_fib_host0_ipv4_hash_key_m ds_fib_host0_ipv4_hash_key;

        sal_memset(&fib_acc_in, 0, sizeof(fib_acc_in));
        sal_memset(&fib_acc_out, 0, sizeof(drv_fib_acc_out_t));
        sal_memset(&ds_fib_host0_ipv4_hash_key, 0, sizeof(ds_fib_host0_ipv4_hash_key));

        SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 1);
        for (index = 0; index < p_cam_profile->ipmc_g_conflict_num; index ++)
        {
            fib_acc_in.rw.tbl_id = DsFibHost0Ipv4HashKey_t;
            fib_acc_in.rw.key_index = index;
            fib_acc_in.rw.data = (void*)&ds_fib_host0_ipv4_hash_key;
            drv_fib_acc(0, DRV_FIB_ACC_WRITE_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out);
        }

        SET_BIT(bitmap, 2);
        SET_BIT(bitmap, 3);
        SET_BIT(bitmap, 5);
        SET_BIT(bitmap, 6);
    }
    */

    /*enable trill*/
    if (0 != p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_TRILL])
    {
        CLEAR_BIT(bitmap, 8);
    }

    /*enable fcoe*/
    if (0 != p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_FCOE])
    {
        CLEAR_BIT(bitmap, 7);
    }

    /* fib host0 acc bitmap */
    g_gg_ftm_master->cam_bitmap = bitmap;

    sal_memcpy(&g_gg_ftm_master->conflict_cam_num[0], &p_cam_profile->conflict_cam_num[0],
                sizeof(p_cam_profile->conflict_cam_num[0])*DRV_FTM_CAM_TYPE_MAX);
    sal_memcpy(&g_gg_ftm_master->cfg_max_cam_num[0], &p_cam_profile->conflict_cam_num[0],
                sizeof(p_cam_profile->conflict_cam_num[0])*DRV_FTM_CAM_TYPE_MAX);


     /*if (p_cam_profile->conflict_cam_num[DRV_FTM_CAM_TYPE_OAM])*/
    {
        SET_BIT(g_gg_ftm_master->xcoam_cam_bitmap0[3], 31); /*single*/
        SET_BIT(g_gg_ftm_master->xcoam_cam_bitmap1[1], 31); /*dual*/
        SET_BIT(g_gg_ftm_master->xcoam_cam_bitmap2[0], 31); /*quad*/

        if(g_gg_ftm_master->conflict_cam_num[DRV_FTM_CAM_TYPE_OAM])
        {
            g_gg_ftm_master->conflict_cam_num[DRV_FTM_CAM_TYPE_OAM] -= 1;
        }
        else
        {
            g_gg_ftm_master->conflict_cam_num[DRV_FTM_CAM_TYPE_XC] -= 1;
        }
    }

    return DRV_E_NONE;
}



STATIC int32
_drv_goldengate_ftm_get_cam_by_tbl_id(uint32 tbl_id, uint8* cam_type)
{
    uint8 type = DRV_FTM_CAM_TYPE_INVALID;

    switch(tbl_id)
    {
        case DsFibHost1Ipv6NatDaPortHashKey_t:
        case DsFibHost1Ipv6NatSaPortHashKey_t:
        case DsFibHost1Ipv4NatDaPortHashKey_t:
        case DsFibHost1Ipv4NatSaPortHashKey_t:
            type = DRV_FTM_CAM_TYPE_FIB_HOST1_NAT;
            break;

        case DsFibHost1Ipv4McastHashKey_t:
        case DsFibHost1Ipv6McastHashKey_t:
        case DsFibHost1MacIpv4McastHashKey_t:
        case DsFibHost1MacIpv6McastHashKey_t:
            type =DRV_FTM_CAM_TYPE_FIB_HOST1_MC ;
            break;


        case DsEgressXcOamPortVlanCrossHashKey_t:
        case DsEgressXcOamCvlanCosPortHashKey_t:
        case DsEgressXcOamPortCrossHashKey_t:
        case DsEgressXcOamCvlanPortHashKey_t:
        case DsEgressXcOamDoubleVlanPortHashKey_t:
        case DsEgressXcOamSvlanCosPortHashKey_t:
        case DsEgressXcOamSvlanPortHashKey_t:
        case DsEgressXcOamPortHashKey_t:
        case DsEgressXcOamSvlanPortMacHashKey_t:
        case DsEgressXcOamTunnelPbbHashKey_t:
            type = DRV_FTM_CAM_TYPE_XC;
            break;

        case DsEgressXcOamBfdHashKey_t:
        case DsEgressXcOamEthHashKey_t:
        case DsEgressXcOamMplsLabelHashKey_t:
        case DsEgressXcOamMplsSectionHashKey_t:
        case DsEgressXcOamRmepHashKey_t:
            type = DRV_FTM_CAM_TYPE_OAM;
            break;

        case DsUserIdTunnelMplsHashKey_t:
            type = DRV_FTM_CAM_TYPE_MPLS;
            break;

        case DsUserIdCvlanCosPortHashKey_t:
        case DsUserIdCvlanPortHashKey_t:
        case DsUserIdDoubleVlanPortHashKey_t:

        case DsUserIdIpv4PortHashKey_t:
        case DsUserIdIpv4SaHashKey_t:
        case DsUserIdIpv6PortHashKey_t:
        case DsUserIdIpv6SaHashKey_t:

        case DsUserIdMacHashKey_t:
        case DsUserIdMacPortHashKey_t:
        case DsUserIdPortHashKey_t:
        case DsUserIdSvlanCosPortHashKey_t:
        case DsUserIdSvlanHashKey_t:
        case DsUserIdSvlanMacSaHashKey_t:
        case DsUserIdSvlanPortHashKey_t:
        case DsUserIdSclFlowL2HashKey_t:

        case DsUserIdTunnelIpv4DaHashKey_t:
        case DsUserIdTunnelIpv4GreKeyHashKey_t:
        case DsUserIdTunnelIpv4HashKey_t:
        case DsUserIdTunnelIpv4RpfHashKey_t:
        case DsUserIdTunnelIpv4UdpHashKey_t:
            type = DRV_FTM_CAM_TYPE_SCL;
            break;

        case DsUserIdTunnelIpv4McNvgreMode0HashKey_t:
        case DsUserIdTunnelIpv4McVxlanMode0HashKey_t:
        case DsUserIdTunnelIpv4NvgreMode1HashKey_t:
        case DsUserIdTunnelIpv4VxlanMode1HashKey_t:
        case DsUserIdTunnelIpv4UcNvgreMode0HashKey_t:
        case DsUserIdTunnelIpv4UcNvgreMode1HashKey_t:
        case DsUserIdTunnelIpv4UcVxlanMode0HashKey_t:
        case DsUserIdTunnelIpv4UcVxlanMode1HashKey_t:
        case DsUserIdTunnelIpv6McNvgreMode0HashKey_t:
        case DsUserIdTunnelIpv6McNvgreMode1HashKey_t:
        case DsUserIdTunnelIpv6McVxlanMode0HashKey_t:
        case DsUserIdTunnelIpv6McVxlanMode1HashKey_t:
        case DsUserIdTunnelIpv6UcNvgreMode0HashKey_t:
        case DsUserIdTunnelIpv6UcNvgreMode1HashKey_t:
        case DsUserIdTunnelIpv6UcVxlanMode0HashKey_t:
        case DsUserIdTunnelIpv6UcVxlanMode1HashKey_t:
            type = DRV_FTM_CAM_TYPE_DCN;
            break;

        default:
            type = DRV_FTM_CAM_TYPE_INVALID;
            break;

    }

    *cam_type = type;

    return DRV_E_NONE;
}



STATIC int32
drv_goldengate_ftm_set_profile(drv_ftm_profile_info_t *profile_info)
{
    uint8 profile_index    = 0;
    uint32 mem_bitmap = 0;
    uint8 mem_id = 0;
    uint8 mem_num = 0;

    profile_index = profile_info->profile_type;
    switch (profile_index)
    {
        case 0:
            _drv_goldengate_ftm_set_profile_default();
            break;

        case 12:
            _drv_goldengate_ftm_set_profile_user_define(profile_info);
            break;

        default:
            return DRV_E_NONE;
    }

    /* check host1 memory number, must alloc one memory */
    mem_bitmap = DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB1_HASH_KEY);
    for (mem_id = DRV_FTM_SRAM0; mem_id < DRV_FTM_SRAM6; mem_id++)
    {
        if (IS_BIT_SET(mem_bitmap, mem_id))
        {
            mem_num++;
        }
        if (mem_num > 1)
        {
            return DRV_E_INVALID_MEM;
        }
    }


    if (DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_NEXTHOP) == DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_MET))
    {
        mem_bitmap = DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_NEXTHOP);
        if ((IS_BIT_SET(mem_bitmap, DRV_FTM_SRAM16)
                 && (DRV_FTM_TBL_MEM_START_OFFSET(DRV_FTM_SRAM_TBL_NEXTHOP, DRV_FTM_SRAM16)
                         == DRV_FTM_TBL_MEM_START_OFFSET(DRV_FTM_SRAM_TBL_MET, DRV_FTM_SRAM16)))
            || (IS_BIT_SET(mem_bitmap, DRV_FTM_SRAM17)
                 && (DRV_FTM_TBL_MEM_START_OFFSET(DRV_FTM_SRAM_TBL_NEXTHOP, DRV_FTM_SRAM17)
                        == DRV_FTM_TBL_MEM_START_OFFSET(DRV_FTM_SRAM_TBL_MET, DRV_FTM_SRAM17))))
        {
            profile_info->nexthop_share = 1;
        }
    }

    return DRV_E_NONE;

}



STATIC int32
drv_goldengate_ftm_init(void)
{
    if (NULL != g_gg_ftm_master)
    {
        sal_memset(g_gg_ftm_master->p_sram_tbl_info, 0, sizeof(drv_ftm_sram_tbl_info_t) * DRV_FTM_SRAM_TBL_MAX);
        sal_memset(g_gg_ftm_master->p_tcam_key_info, 0, sizeof(drv_ftm_tcam_key_info_t) * DRV_FTM_TCAM_TYPE_MAX);
        return DRV_E_NONE;
    }

    g_gg_ftm_master = mem_malloc(MEM_FTM_MODULE, sizeof(g_ftm_master_t));

    if (NULL == g_gg_ftm_master)
    {
        return DRV_E_NO_MEMORY;
    }

    sal_memset(g_gg_ftm_master, 0, sizeof(g_ftm_master_t));

    g_gg_ftm_master->p_sram_tbl_info =
        mem_malloc(MEM_FTM_MODULE, sizeof(drv_ftm_sram_tbl_info_t) * DRV_FTM_SRAM_TBL_MAX);
    if (NULL == g_gg_ftm_master->p_sram_tbl_info)
    {
        mem_free(g_gg_ftm_master);
        return DRV_E_NO_MEMORY;
    }

    sal_memset(g_gg_ftm_master->p_sram_tbl_info, 0, sizeof(drv_ftm_sram_tbl_info_t) * DRV_FTM_SRAM_TBL_MAX);

    g_gg_ftm_master->p_tcam_key_info =
        mem_malloc(MEM_FTM_MODULE, sizeof(drv_ftm_tcam_key_info_t) * DRV_FTM_TCAM_TYPE_MAX);
    if (NULL == g_gg_ftm_master->p_sram_tbl_info)
    {
        mem_free(g_gg_ftm_master->p_sram_tbl_info);
        mem_free(g_gg_ftm_master);
        return DRV_E_NO_MEMORY;
    }

    sal_memset(g_gg_ftm_master->p_tcam_key_info, 0, sizeof(drv_ftm_tcam_key_info_t) * DRV_FTM_TCAM_TYPE_MAX);

    return DRV_E_NONE;
}




STATIC int32
drv_goldengate_tcam_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 alloc_bitmap = 0;
    uint32 tcam_bitmap = 0;
    FlowTcamLookupCtl_m FlowTcamLookupCtl;
    uint32 couple_mode = 0;

    sal_memset(&FlowTcamLookupCtl, 0, sizeof(FlowTcamLookupCtl));

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    /*IPE ACL 0*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_IGS_ACL0);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap, 1) << 0) | (IS_BIT_SET(alloc_bitmap, 2)<<1)|  (IS_BIT_SET(alloc_bitmap, 5) << 2) | (IS_BIT_SET(alloc_bitmap, 6)<<3);
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl0Bitmap_f,
                          &tcam_bitmap, &FlowTcamLookupCtl);

   /*IPE ACL 1*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_IGS_ACL1);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap,5));
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl1Bitmap_f,
                          &tcam_bitmap, &FlowTcamLookupCtl);

    /*IPE ACL 2*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_IGS_ACL2);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap, 2));
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl2Bitmap_f,
                &tcam_bitmap, &FlowTcamLookupCtl);


    /*IPE ACL 3*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_IGS_ACL3);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap, 1));
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_ingressAcl3Bitmap_f,
                &tcam_bitmap, &FlowTcamLookupCtl);


   /*EPE ACL0*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_EGS_ACL0);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap,4) << 0) | (IS_BIT_SET(alloc_bitmap,5) << 1);

    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_egressAcl0Bitmap_f,
                          &tcam_bitmap, &FlowTcamLookupCtl);

   /*IPE UserID0*/
    alloc_bitmap = DRV_FTM_KEY_MEM_BITMAP(DRV_FTM_TCAM_TYPE_IGS_USERID0);
    tcam_bitmap = (IS_BIT_SET(alloc_bitmap, 3) << 0) | (IS_BIT_SET(alloc_bitmap, 5) << 1)
                         | (IS_BIT_SET(alloc_bitmap, 6)<<2);
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_userId0Bitmap_f,
                 &tcam_bitmap, &FlowTcamLookupCtl);

    /*flow tcam couple mode*/
    DRV_IOW_FIELD(FlowTcamLookupCtl_t, FlowTcamLookupCtl_modeSelect_f,
                          &couple_mode, &FlowTcamLookupCtl);

    cmd = DRV_IOW(FlowTcamLookupCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &FlowTcamLookupCtl));

    /*lpm tcam couple mode*/
    cmd = DRV_IOW(LpmTcamIpCtl_t, LpmTcamIpCtl_modeSel_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &couple_mode));

    return DRV_E_NONE;

}


int32
drv_goldengate_dynamic_cam_init(uint32 lchip)
{
    uint32 cmd                = 0;
    uint32 field_id        = 0;
    uint32 field_id_en      = 0;
    uint32 field_id_base      = 0;
    uint32 field_id_max_index = 0;
    uint32 field_id_min_index = 0;
    uint32 user_id_max_base = 0;

    uint32 ds[32] = {0};
    uint8 i = 0;
    uint8 idx = 0;
    uint32 field_val          = 0;
    uint32 tbl_id             = 0;
    uint32 tbl_id_array[5]   = {0};

    uint32 sum               = 0;
    uint8 step                  = 1;
    uint32 bitmap            = 0;
    uint8 cam_type            = 0;
    uint8 sram_type           = 0;
    uint8 unit                    = DRV_FTM_UNIT_8K;

    for (cam_type = 0; cam_type < DRV_FTM_DYN_CAM_TYPE_MAX; cam_type++)
    {
        idx = 0;

        switch (cam_type)
        {
        case DRV_FTM_DYN_CAM_TYPE_LPM_KEY:
            sram_type                    =  DRV_FTM_SRAM_TBL_LPM_LKP_KEY;
            tbl_id_array[idx++]      = DynamicDsHashLpmPipeIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsHashLpmPipeIndexCam1_t;
            field_id_en                   = DynamicDsHashLpmPipeIndexCam0_lpmPipeCamEnable0_f;
            field_id_base               = DynamicDsHashLpmPipeIndexCam0_lpmPipeBaseAddr0_f;
            field_id_min_index      = DynamicDsHashLpmPipeIndexCam0_lpmPipeMinIndex0_f;
            field_id_max_index     = DynamicDsHashLpmPipeIndexCam0_lpmPipeMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_FIB_MACHOST0_KEY:
            sram_type                    =  DRV_FTM_SRAM_TBL_FIB0_HASH_KEY;
            tbl_id_array[idx++]      = DynamicDsHashMacHashIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsHashMacHashIndexCam1_t;
            field_id_en                   = DynamicDsHashMacHashIndexCam0_macHashCamEnable0_f;
            field_id_base               = DynamicDsHashMacHashIndexCam0_macHashBaseAddr0_f;
            field_id_min_index      = DynamicDsHashMacHashIndexCam0_macHashMinIndex0_f;
            field_id_max_index     = DynamicDsHashMacHashIndexCam0_macHashMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_FIB_FLOW_AD:
            sram_type                    =  DRV_FTM_SRAM_TBL_FLOW_AD;
            tbl_id_array[idx++]      = DynamicDsAdDsFlowIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsFlowIndexCam1_t;
            field_id_en                   = DynamicDsAdDsFlowIndexCam0_dsFlowCamEnable0_f;
            field_id_base               = DynamicDsAdDsFlowIndexCam0_dsFlowBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsFlowIndexCam0_dsFlowMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsFlowIndexCam0_dsFlowMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;


        case DRV_FTM_DYN_CAM_TYPE_IP_FIX_HASH_KEY:
            sram_type                    =  DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY;
            tbl_id_array[idx++]      = DynamicDsHashIpfixHashIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsHashIpfixHashIndexCam1_t;
            field_id_en                   = DynamicDsHashIpfixHashIndexCam0_ipfixHashCamEnable0_f;
            field_id_base               = DynamicDsHashIpfixHashIndexCam0_ipfixHashBaseAddr0_f;
            field_id_min_index      = DynamicDsHashIpfixHashIndexCam0_ipfixHashMinIndex0_f;
            field_id_max_index     = DynamicDsHashIpfixHashIndexCam0_ipfixHashMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_IP_FIX_HASH_AD:
            sram_type                    =  DRV_FTM_SRAM_TBL_IPFIX_AD;
            tbl_id_array[idx++]      = DynamicDsAdDsIpfixIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsIpfixIndexCam1_t;
            field_id_en                   = DynamicDsAdDsIpfixIndexCam0_dsIpfixCamEnable0_f;
            field_id_base               = DynamicDsAdDsIpfixIndexCam0_dsIpfixBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsIpfixIndexCam0_dsIpfixMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsIpfixIndexCam0_dsIpfixMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_FIB_L2_AD:
            sram_type                    =  DRV_FTM_SRAM_TBL_DSMAC_AD;
            tbl_id_array[idx++]      = DynamicDsAdDsMacIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsMacIndexCam1_t;
            field_id_en                   = DynamicDsAdDsMacIndexCam0_dsMacCamEnable0_f;
            field_id_base               = DynamicDsAdDsMacIndexCam0_dsMacBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsMacIndexCam0_dsMacMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsMacIndexCam0_dsMacMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_FIB_L3_AD:
            sram_type                    =  DRV_FTM_SRAM_TBL_DSIP_AD;
            tbl_id_array[idx++]      = DynamicDsAdDsIpIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsIpIndexCam1_t;
            field_id_en                   = DynamicDsAdDsIpIndexCam0_dsIpCamEnable0_f;
            field_id_base               = DynamicDsAdDsIpIndexCam0_dsIpBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsIpIndexCam0_dsIpMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsIpIndexCam0_dsIpMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_DS_EDIT:
            sram_type                    =  DRV_FTM_SRAM_TBL_EDIT;
            tbl_id_array[idx++]      = DynamicDsShareDsEditIndexCam_t;
            field_id_en                   = DynamicDsShareDsEditIndexCam_dsL2L3EditCamEnable0_f;
            field_id_base               = DynamicDsShareDsEditIndexCam_dsL2L3EditBaseAddr0_f;
            field_id_min_index      = DynamicDsShareDsEditIndexCam_dsL2L3EditMinIndex0_f;
            field_id_max_index     = DynamicDsShareDsEditIndexCam_dsL2L3EditMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_1K;
            break;


        case DRV_FTM_DYN_CAM_TYPE_DS_NEXTHOP:
            sram_type                    =  DRV_FTM_SRAM_TBL_NEXTHOP;
            tbl_id_array[idx++]      = DynamicDsAdDsNextHopIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsNextHopIndexCam1_t;
            field_id_en                   = DynamicDsAdDsNextHopIndexCam0_dsNextHopCamEnable0_f;
            field_id_base               = DynamicDsAdDsNextHopIndexCam0_dsNextHopBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsNextHopIndexCam0_dsNextHopMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsNextHopIndexCam0_dsNextHopMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_DS_MET:
            sram_type                    =  DRV_FTM_SRAM_TBL_MET;
            tbl_id_array[idx++]      = DynamicDsAdDsMetIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdDsMetIndexCam1_t;
            field_id_en                   = DynamicDsAdDsMetIndexCam0_dsMetCamEnable0_f;
            field_id_base               = DynamicDsAdDsMetIndexCam0_dsMetBaseAddr0_f;
            field_id_min_index      = DynamicDsAdDsMetIndexCam0_dsMetMinIndex0_f;
            field_id_max_index     = DynamicDsAdDsMetIndexCam0_dsMetMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_EGRESS_OAM_HASH_KEY:
            sram_type                    =  DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY;
            tbl_id_array[idx++]      = DynamicDsAdEgrOamHashIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsAdEgrOamHashIndexCam1_t;
            field_id_en                   = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev0CamEnable0_f;
            field_id_base               = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev0BaseAddr0_f;
            field_id_min_index      = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev0MinIndex0_f;
            field_id_max_index     = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev0MaxIndex0_f;
            step                              = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev1CamEnable0_f -
                                                    DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev0CamEnable0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_USERID_HASH_KEY:
            sram_type                    =  DRV_FTM_SRAM_TBL_USERID_HASH_KEY;
            tbl_id_array[idx++]      = DynamicDsHashUserIdHashIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsHashUserIdHashIndexCam1_t;
            field_id_en                   = DynamicDsHashUserIdHashIndexCam0_userIdHash0CamEnable0_f;
            field_id_base               = DynamicDsHashUserIdHashIndexCam0_userIdHash0BaseAddr0_f;
            field_id_min_index      = DynamicDsHashUserIdHashIndexCam0_userIdHash0MinIndex0_f;
            field_id_max_index     = DynamicDsHashUserIdHashIndexCam0_userIdHash0MaxIndex0_f;
            step                              = DynamicDsHashUserIdHashIndexCam0_userIdHash1CamEnable0_f -
                                                    DynamicDsHashUserIdHashIndexCam0_userIdHash0CamEnable0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_USERID_HASH_AD:
            sram_type                    =  DRV_FTM_SRAM_TBL_USERID_AD;
            tbl_id_array[idx++]      = DynamicDsHashUserIdAdIndexCam0_t;
            tbl_id_array[idx++]      = DynamicDsHashUserIdAdIndexCam1_t;
            field_id_en                   = DynamicDsHashUserIdAdIndexCam0_userIdAdCamEnable0_f;
            field_id_base               = DynamicDsHashUserIdAdIndexCam0_userIdAdBaseAddr0_f;
            field_id_min_index      = DynamicDsHashUserIdAdIndexCam0_userIdAdMinIndex0_f;
            field_id_max_index     = DynamicDsHashUserIdAdIndexCam0_userIdAdMaxIndex0_f;
            unit                               = DRV_FTM_UNIT_8K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_DS_MEP:
            sram_type                    =  DRV_FTM_SRAM_TBL_OAM_MEP;
            tbl_id_array[idx++]      = DynamicDsShareDsMepIndexCam_t;
            field_id_en                   = DRV_FTM_FLD_INVALID;
            field_id_base               = DynamicDsShareDsMepIndexCam_dsMepBaseAddr_f;
            field_id_min_index      = DRV_FTM_FLD_INVALID;
            field_id_max_index     = DynamicDsShareDsMepIndexCam_dsMepMaxIndex_f;
            unit                               = DRV_FTM_UNIT_1K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_DS_LM:
            sram_type                    =  DRV_FTM_SRAM_TBL_OAM_LM;
            tbl_id_array[idx++]      = DynamicDsShareDsOamLmStatsIndexCam_t;
            field_id_en                   = DRV_FTM_FLD_INVALID;
            field_id_base               = DynamicDsShareDsOamLmStatsIndexCam_dsOamLmStatsBaseAddr_f;
            field_id_min_index      = DRV_FTM_FLD_INVALID;
            field_id_max_index     = DynamicDsShareDsOamLmStatsIndexCam_dsOamLmStatsMaxIndex_f;
            unit                               = DRV_FTM_UNIT_1K;
            break;

        case DRV_FTM_DYN_CAM_TYPE_DS_APS:
            sram_type                    =  DRV_FTM_SRAM_TBL_OAM_APS;
            tbl_id_array[idx++]      = DynamicDsShareDsApsIndexCam_t;
            field_id_en                   = DRV_FTM_FLD_INVALID;
            field_id_base               = DynamicDsShareDsApsIndexCam_dsApsBaseAddr_f;
            field_id_min_index      = DRV_FTM_FLD_INVALID;
            field_id_max_index     = DynamicDsShareDsApsIndexCam_dsApsMaxIndex_f;
            unit                               = DRV_FTM_UNIT_1K;
            break;

        default:
           continue;
        }

        bitmap = DRV_FTM_TBL_BASE_BITMAP(sram_type);
        tbl_id = tbl_id_array[0];

        sal_memset(ds, 0, sizeof(ds));

        for (i = 0; i < 5; i++)
        {
            if (!IS_BIT_SET(bitmap, i))
            {
                continue;
            }

            if (cam_type == DRV_FTM_DYN_CAM_TYPE_EGRESS_OAM_HASH_KEY)
            {
                if (0 == i)
                {
                    /* Ram 17 is used, and it's level 0 hash */
                    step = 0;
                }
                else
                {
                    /*
                        Ram 7 or Ram 8 is used, and they're level 1 hash
                        For Ram 7, step should be 0
                        For Ram 8, step should be 1
                    */
                    field_id_en                   = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev1CamEnable0_f;
                    field_id_base               = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev1BaseAddr0_f;
                    field_id_min_index      = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev1MinIndex0_f;
                    field_id_max_index     = DynamicDsAdEgrOamHashIndexCam0_egrOamHashLev1MaxIndex0_f;
                    step = i - 1;
                }

            }
            else if (cam_type == DRV_FTM_DYN_CAM_TYPE_USERID_HASH_KEY)
            {
                uint8 temp_step = DynamicDsHashUserIdHashIndexCam0_userIdHash1BaseAddr0_f -
                                               DynamicDsHashUserIdHashIndexCam0_userIdHash0BaseAddr0_f;
                step = (i >= 2)?(( i - 2) + temp_step):i;
            }

            else
            {
                step = i;
            }

            if (field_id_en != DRV_FTM_FLD_INVALID)
            {
                /*write ds cam enable value*/
                field_id = field_id_en + step;
                field_val = 1;
                DRV_SET_FIELD_V(tbl_id, field_id,   ds,   field_val );
            }

            if (field_id_base != DRV_FTM_FLD_INVALID)
            {
                /*write ds cam base value*/
                field_id = field_id_base + step;
                field_val = DRV_FTM_TBL_BASE_VAL(sram_type, i);
                field_val = (field_val >> unit);
                DRV_SET_FIELD_V(tbl_id, field_id,   ds,   field_val );
            }

            if (field_id_min_index != DRV_FTM_FLD_INVALID)
            {
                /*write ds cam min value*/
                field_id = field_id_min_index + step;
                field_val = DRV_FTM_TBL_BASE_MIN(sram_type, i);
                field_val = (field_val >> unit);

                if  (DRV_FTM_SRAM_TBL_USERID_HASH_KEY == sram_type)
                {
                    field_val = 0;
                }


                DRV_SET_FIELD_V(tbl_id, field_id,   ds,   field_val );

            }

            if (field_id_max_index != DRV_FTM_FLD_INVALID)
            {
                /*write ds cam max value*/
                field_id = field_id_max_index + step;

                /*write ds cam base max index*/
                if (DRV_FTM_SRAM_TBL_OAM_MEP == sram_type)
                {
                    field_val = DRV_FTM_TBL_BASE_MAX(DRV_FTM_SRAM_TBL_OAM_MEP,    i) +
                    DRV_FTM_TBL_BASE_MAX(DRV_FTM_SRAM_TBL_OAM_MA,      i) +
                    DRV_FTM_TBL_BASE_MAX(DRV_FTM_SRAM_TBL_OAM_MA_NAME, i);
                }
                else if (DRV_FTM_SRAM_TBL_USERID_HASH_KEY == sram_type)
                {
                    field_val = DRV_FTM_TBL_BASE_MAX(sram_type, i);
                    field_val = field_val - user_id_max_base;
                    user_id_max_base = DRV_FTM_TBL_BASE_MAX(sram_type, i) + 1;
                }
                else
                {
                    field_val = DRV_FTM_TBL_BASE_MAX(sram_type, i);
                }

                field_val = (field_val >> unit);
                DRV_SET_FIELD_V(tbl_id, field_id,   ds,   field_val );
            }

        }

        for (i = 0; i < idx; i++)
        {
            tbl_id = tbl_id_array[i];
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        }

    }


    /*init oam base*/
    field_val = 0;
    field_id_base = OamTblAddrCtl_mpBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += DRV_FTM_TBL_MAX_ENTRY_NUM(DRV_FTM_SRAM_TBL_OAM_MEP);
    field_val = (sum >> 6);
    field_id_base = OamTblAddrCtl_maBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum  += (DRV_FTM_TBL_MAX_ENTRY_NUM(DRV_FTM_SRAM_TBL_OAM_MA));
    field_val = (sum >> 6);
    field_id_base = OamTblAddrCtl_maNameBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*init fib host0 cam bitmap*/
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_camDisableBitmap_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g_gg_ftm_master->cam_bitmap));

    return DRV_E_NONE;
}



int32
drv_goldengate_dynamic_arb_init(uint32 lchip)
{
    tbls_id_t table_id = MaxTblId_t;
    uint32 cmd = 0;
    uint32 ds[MAX_ENTRY_WORD] = {0};
    uint32 bit_set = 0;
    uint32 couple_mode      = 0;
    uint32 ipfix_couple_en  = 1;
    uint32 couple_ctl_flag  = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));



#define ArbTable(i) DynamicDsHashSharedRam##i##ArbCtl0_t
#define ArbTable1(i) DynamicDsHashSharedRam##i##ArbCtl1_t
#define ArbTblFld(i, fld) DynamicDsHashSharedRam##i##ArbCtl0_sharedRam##i##Arb##fld##_f
#define ArbTblFldCouple(i, fld) DynamicDsHashSharedRam##i##ArbCtl0_sharedRam##i##Local##fld##_f

    /* Share RAM 0 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(0);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_HASH_KEY), DRV_FTM_SRAM0);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, FlowHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB1_HASH_KEY), DRV_FTM_SRAM0);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, IpHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_LPM_LKP_KEY), DRV_FTM_SRAM0);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, LpmPipeEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB0_HASH_KEY), DRV_FTM_SRAM0);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, MacHashEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),   &couple_mode, ds);

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(0), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 1 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(1);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_HASH_KEY), DRV_FTM_SRAM1);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, FlowHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_LPM_LKP_KEY), DRV_FTM_SRAM1);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, LpmPipeEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB0_HASH_KEY), DRV_FTM_SRAM1);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, MacHashEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(1, Couple),   &couple_mode, ds);

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(1), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /* Share RAM 2 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(2);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_HASH_KEY), DRV_FTM_SRAM2);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, FlowHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_LPM_LKP_KEY), DRV_FTM_SRAM2);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, LpmPipeEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB0_HASH_KEY), DRV_FTM_SRAM2);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, MacHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_HASH_KEY), DRV_FTM_SRAM2);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, UserIdHash0En),   &bit_set, ds);

    DRV_IOW_FIELD(table_id, ArbTblFldCouple(2, Couple),   &couple_mode, ds);

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(2), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /* Share RAM 3 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(3);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_HASH_KEY), DRV_FTM_SRAM3);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, FlowHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY), DRV_FTM_SRAM3);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, IpfixEn),   &bit_set, ds);
    if(bit_set && !couple_mode)
    {
        DRV_IOW_FIELD(table_id, ArbTblFldCouple(3, Couple),  &ipfix_couple_en, ds);
    }
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB0_HASH_KEY), DRV_FTM_SRAM3);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, MacHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_HASH_KEY), DRV_FTM_SRAM3);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, UserIdHash1En),   &bit_set, ds);
    if (couple_mode)
    {
        DRV_IOW_FIELD(table_id, ArbTblFldCouple(3, Couple),   &couple_mode, ds);
    }

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(3), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 4 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(4);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_HASH_KEY), DRV_FTM_SRAM4);
    DRV_IOW_FIELD(table_id, ArbTblFld(4, FlowHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB0_HASH_KEY), DRV_FTM_SRAM4);
    DRV_IOW_FIELD(table_id, ArbTblFld(4, MacHashEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(4, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(4), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /* Share RAM 5 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(5);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB1_HASH_KEY), DRV_FTM_SRAM5);
    DRV_IOW_FIELD(table_id, ArbTblFld(5, IpHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_LPM_LKP_KEY), DRV_FTM_SRAM5);
    DRV_IOW_FIELD(table_id, ArbTblFld(5, LpmPipeEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(5, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(5), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 6 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(6);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FIB1_HASH_KEY), DRV_FTM_SRAM6);
    DRV_IOW_FIELD(table_id, ArbTblFld(6, IpHashEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_LPM_LKP_KEY), DRV_FTM_SRAM6);
    DRV_IOW_FIELD(table_id, ArbTblFld(6, LpmPipeEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(6, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(6), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

#undef ArbTable
#undef ArbTable1
#undef ArbTblFld
#undef ArbTblFldCouple
#define ArbTable(i) DynamicDsAdDsIpMacRam##i##ArbCtl0_t
#define ArbTable1(i) DynamicDsAdDsIpMacRam##i##ArbCtl1_t
#define ArbTblFld(i, fld) DynamicDsAdDsIpMacRam##i##ArbCtl0_dsIpMacRam##i##Arb##fld##_f
#define ArbTblFldCouple(i, fld) DynamicDsAdDsIpMacRam##i##ArbCtl0_dsIpMacRam##i##Local##fld##_f

    /* Share RAM 7 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(0);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_AD), DRV_FTM_SRAM7);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, DsFlowEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSIP_AD), DRV_FTM_SRAM7);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, DsIpEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_IPFIX_AD), DRV_FTM_SRAM7);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, DsIpfixEn),   &bit_set, ds);
    if(bit_set && !couple_mode)
    {
        DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),  &ipfix_couple_en, ds);
    }
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY), DRV_FTM_SRAM7);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, EgrOamHash1En),   &bit_set, ds);
    if(couple_mode)
    {
        DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),   &couple_mode, ds);
    }

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(0), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 8 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(1);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_AD), DRV_FTM_SRAM8);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, DsFlowEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSIP_AD), DRV_FTM_SRAM8);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, DsIpEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSMAC_AD), DRV_FTM_SRAM8);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, DsMacEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY), DRV_FTM_SRAM8);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, EgrOamHash1En),   &bit_set, ds);
    /* DRV_IOW_FIELD(table_id, ArbTblFld(1, Couple),   &couple_mode, ds);*/
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(1), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 9 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(2);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_AD), DRV_FTM_SRAM9);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, DsFlowEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSIP_AD), DRV_FTM_SRAM9);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, DsIpEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSMAC_AD), DRV_FTM_SRAM9);
    DRV_IOW_FIELD(table_id, ArbTblFld(2, DsMacEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(2, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(2), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));



    /* Share RAM 10 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(3);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_FLOW_AD), DRV_FTM_SRAM10);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, DsFlowEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSIP_AD), DRV_FTM_SRAM10);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, DsIpEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_DSMAC_AD), DRV_FTM_SRAM10);
    DRV_IOW_FIELD(table_id, ArbTblFld(3, DsMacEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(3, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(3), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


#undef ArbTable
#undef ArbTable1
#undef ArbTblFld
#undef ArbTblFldCouple
#define ArbTable(i) DynamicDsHashUserIdHashRam##i##ArbCtl0_t
#define ArbTable1(i) DynamicDsHashUserIdHashRam##i##ArbCtl1_t
#define ArbTblFld(i, fld) DynamicDsHashUserIdHashRam##i##ArbCtl0_userIdHashRam##i##Arb##fld##_f
#define ArbTblFldCouple(i, fld) DynamicDsHashUserIdHashRam##i##ArbCtl0_userIdHashRam##i##Local##fld##_f

    /* Share RAM 11 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(0);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_AD), DRV_FTM_SRAM11);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, UserIdAdEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_HASH_KEY), DRV_FTM_SRAM11);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, UserIdHash0En),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(0), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));



    /* Share RAM 12 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(1);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_AD), DRV_FTM_SRAM12);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, UserIdAdEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_HASH_KEY), DRV_FTM_SRAM12);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, UserIdHash1En),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(1, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(1), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


#undef ArbTable
#undef ArbTable1
#undef ArbTblFld
#undef ArbTblFldCouple
#define ArbTable(i) DynamicDsHashUserIdAdRamArbCtl0_t
#define ArbTable1(i) DynamicDsHashUserIdAdRamArbCtl1_t
#define ArbTblFld(i, fld) DynamicDsHashUserIdAdRamArbCtl0_userIdAdRamArb##fld##_f
#define ArbTblFldCouple(i, fld) DynamicDsHashUserIdAdRamArbCtl0_userIdAdRamLocal##fld##_f

    /* Share RAM 13 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(0);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_USERID_AD), DRV_FTM_SRAM13);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, UserIdAdEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(0), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 14, 15 are L23EditRam, do not need arb config */

#undef ArbTable
#undef ArbTable1
#undef ArbTblFld
#undef ArbTblFldCouple
#define ArbTable(i) DynamicDsAdNextHopMetRam##i##ArbCtl0_t
#define ArbTable1(i) DynamicDsAdNextHopMetRam##i##ArbCtl1_t
#define ArbTblFld(i, fld) DynamicDsAdNextHopMetRam##i##ArbCtl0_nextHopMetRam##i##Arb##fld##_f
#define ArbTblFldCouple(i, fld) DynamicDsAdNextHopMetRam##i##ArbCtl0_nextHopMetRam##i##Local##fld##_f

    /* Share RAM 16 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(0);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_NEXTHOP), DRV_FTM_SRAM16);
    DRV_IOW_FIELD(table_id, ArbTblFld(0, DsNextHopMetEn),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(0, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(0), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* Share RAM 17 */
    sal_memset(ds, 0, sizeof(ds));
    table_id = ArbTable(1);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_NEXTHOP), DRV_FTM_SRAM17);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, DsNextHopMetEn),   &bit_set, ds);
    bit_set = IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY), DRV_FTM_SRAM17);
    DRV_IOW_FIELD(table_id, ArbTblFld(1, EgrOamHash0En),   &bit_set, ds);
    DRV_IOW_FIELD(table_id, ArbTblFldCouple(1, Couple),   &couple_mode, ds);
    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    cmd = DRV_IOW(ArbTable1(1), DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /* DynamicDsShareMacroCtl */

    sal_memset(ds, 0, sizeof(ds));
    table_id = DynamicDsShareMacroCtl_t;
    DRV_IOW_FIELD(table_id, DynamicDsShareMacroCtl_dsFwdLocalCouple_f,          &couple_mode, ds);
    DRV_IOW_FIELD(table_id, DynamicDsShareMacroCtl_dsL2L3EditRam0LocalCouple_f, &couple_mode, ds);
    DRV_IOW_FIELD(table_id, DynamicDsShareMacroCtl_dsL2L3EditRam1LocalCouple_f, &couple_mode, ds);

    cmd = DRV_IOW(table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*couple en*/
    table_id = ResvForce_t;
    cmd = DRV_IOR(table_id, ResvForce_resvForce_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &couple_ctl_flag));
    SET_BIT(couple_ctl_flag, 0);
    cmd = DRV_IOW(table_id, ResvForce_resvForce_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &couple_ctl_flag));

    return DRV_E_NONE;
}


STATIC int32
drv_goldengate_flow_lkup_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 ds[MAX_ENTRY_WORD] = {0};
    uint32 tbl_id = 0;
    uint32 en = 0;
    uint32 mem_alloced = 0;

    uint8 i  = 0;
    uint32 bitmap = 0;
    uint32 sram_array[5] = {0};
    uint8 sram_type = 0;
    uint32 couple_mode = 0;
    uint32 poly = 0;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));

    /*Flow lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));
    mem_alloced = 0;
    tbl_id = FlowHashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_FLOW_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);
    i  = 0;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel0HashEn_f,     ds, en);

    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel0HashType_f,   ds, poly);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    i  = 1;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel1HashEn_f,     ds, en);

    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel1HashType_f,   ds, poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel1IndexBase_f,   ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    i  = 2;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel2HashEn_f,     ds, en);

    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel2HashType_f,   ds, poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel2IndexBase_f,   ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    i  = 3;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel3HashEn_f,     ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel3HashType_f,   ds, poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel3IndexBase_f,   ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    i  = 4;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel4HashEn_f,     ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel4HashType_f,   ds, poly);
    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowLevel4IndexBase_f,   ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    DRV_SET_FIELD_V(tbl_id,   FlowHashLookupCtl_flowHashSizeMode_f,     ds, couple_mode);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*userid  lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));
    mem_alloced = 0;
    tbl_id = UserIdHashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_USERID_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    en = IS_BIT_SET(bitmap, 0) ||  IS_BIT_SET(bitmap, 1);
    i = IS_BIT_SET(bitmap, 1) && en;
    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdLevel0HashEn_f,    ds, en);

    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdLevel0HashType_f,  ds, poly);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    en = IS_BIT_SET(bitmap, 2) ||  IS_BIT_SET(bitmap, 3);
    i += 2;

    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdLevel1HashEn_f,    ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdLevel1HashType_f,  ds, poly);
    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdLevel1IndexBase_f, ds, mem_alloced);

    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdRamUsageMode_f,    ds, IS_BIT_SET(bitmap, 0));
    DRV_SET_FIELD_V(tbl_id, UserIdHashLookupCtl_userIdHashSizeMode_f,    ds, couple_mode);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    /*XcOamUserid  lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));
    mem_alloced = 0;
    tbl_id = EgressXcOamHashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    i = 0;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamLevel0HashEn_f, ds, en);

    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamLevel0HashType_f, ds, poly);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    en = IS_BIT_SET(bitmap, 1) ||  IS_BIT_SET(bitmap, 2);
    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamLevel1HashEn_f, ds, en);
    if (IS_BIT_SET(bitmap, 1))
    {
        i = 1;
    }
    else if (IS_BIT_SET(bitmap, 2))
    {
        i = 2;
    }
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamLevel1HashType_f, ds, poly);

    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamLevel1IndexBase_f, ds, mem_alloced);

    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamHashMode_f, ds, ((i == 1)? 1 :0) );
    DRV_SET_FIELD_V(tbl_id, EgressXcOamHashLookupCtl_egressXcOamHashSizeMode_f, ds, couple_mode);

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*Host0lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));
    mem_alloced = 0;
    tbl_id = FibHost0HashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_FIB0_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    i  = 0;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level0HashEn_f, ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level0HashType_f, ds, poly);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    i  = 1;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level1HashEn_f, ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level1HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level1IndexBase_f, ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    i  = 2;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level2HashEn_f, ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level2HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level2IndexBase_f, ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    i  = 3;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level3HashEn_f, ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level3HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level3IndexBase_f, ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;

    i  = 4;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level4HashEn_f, ds, en);
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level4HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0Level4IndexBase_f, ds, mem_alloced);

    DRV_SET_FIELD_V(tbl_id, FibHost0HashLookupCtl_fibHost0HashSizeMode_f, ds, couple_mode);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*Host1lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));
    mem_alloced = 0;
    tbl_id = FibHost1HashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_FIB1_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    i  = 0;
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);

    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel0HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel0HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel0HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel0HashType_f, ds, poly);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    if (en)
    {
        g_gg_ftm_master->host1_poly_type = poly;
    }

    i  = 1;
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);

    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel1HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel1HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel1IndexBase_f, ds, mem_alloced);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel1HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel1HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel1IndexBase_f, ds, mem_alloced);
    mem_alloced  += en?DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, sram_array[i]):0;
    if (en)
    {
        g_gg_ftm_master->host1_poly_type = poly;
    }


    i  = 2;
    _sys_goldengate_ftm_get_hash_type(sram_type, sram_array[i], couple_mode, &poly);

    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel2HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel2HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel2IndexBase_f, ds, mem_alloced);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel2HashEn_f, ds, en);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel2HashType_f, ds, poly);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel2IndexBase_f, ds, mem_alloced);
    if (en)
    {
        g_gg_ftm_master->host1_poly_type = poly;
    }

    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1DaHashSizeMode_f, ds, couple_mode);
    DRV_SET_FIELD_V(tbl_id, FibHost1HashLookupCtl_fibHost1SaHashSizeMode_f, ds, couple_mode);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*IPFix hash ctl*/
    sal_memset(ds, 0, sizeof(ds));
    tbl_id = IpfixHashLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    i  = 0;
    en = IS_BIT_SET(bitmap, i);
    DRV_SET_FIELD_V(tbl_id, IpfixHashLookupCtl_ipfixHashEn_f,   ds,   en );
    DRV_SET_FIELD_V(tbl_id, IpfixHashLookupCtl_ipfixHashSizeMode_f, ds, 1);
    DRV_SET_FIELD_V(tbl_id, IpfixHashLookupCtl_ipfixLevel0HashType_f, ds, 2);
    DRV_SET_FIELD_V(tbl_id, IpfixHashLookupCtl_ipfixLevel1HashType_f, ds, 3);

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*lpm ctl*/
    sal_memset(ds, 0, sizeof(ds));
    tbl_id = FibEngineLookupCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_LPM_LKP_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);
    DRV_SET_FIELD_V(tbl_id, FibEngineLookupCtl_lpmPipelineEn_f,   ds,   1 );
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    return DRV_E_NONE;
}


STATIC int32
drv_goldengate_fib_lkup_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 ds[MAX_ENTRY_WORD] = {0};
    uint32 tbl_id = 0;

    uint8 i  = 0;
    uint32 bitmap = 0;
    uint32 sram_array[5];
    uint8 sram_type = 0;

    /*Host0lkup ctl*/
    sal_memset(ds, 0, sizeof(ds));

    tbl_id = FibAccelerationCtl_t;
    sram_type = DRV_FTM_SRAM_TBL_FIB0_HASH_KEY;
    _sys_goldengate_ftm_get_edram_bitmap(sram_type,   &bitmap,   sram_array);

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    /*Support 5 level fibhostAcc*/
    for (i = 0; i < 5; i++)
    {
        if (IS_BIT_SET(bitmap, i))
        {
            DRV_SET_FIELD_V(tbl_id, FibAccelerationCtl_host0Level0HashEn_f + i, ds, 1);
        }
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_cam_info(drv_ftm_info_detail_t* p_ftm_info)
{
    p_ftm_info->max_size = g_gg_ftm_master->cfg_max_cam_num[p_ftm_info->tbl_type];
    p_ftm_info->used_size = g_gg_ftm_master->cfg_max_cam_num[p_ftm_info->tbl_type] - g_gg_ftm_master->conflict_cam_num[p_ftm_info->tbl_type];
    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_tcam_info_detail(drv_ftm_info_detail_t* p_ftm_info)
{
    uint8 tcam_key_type        = 0;
    uint32 tcam_key_id          = MaxTblId_t;
    uint32 tcam_ad_a[20]        = {0};
    uint32 old_key_size = 0;
    uint8 key_share_num         = 0;
    uint8 ad_share_num          = 0;
    uint32 key_size             = 0;
    uint8 index                 = 0;
    uint8 key_type = 0;
    char* arr_tcam_tbl[] = {
        "IpeAcl0",
        "IpeAcl1",
        "IpeAcl2",
        "IpeAcl3",
        "EpeAcl0",
        "SCL0",
        "SCL1",
        "LPM0",
        "LPM1",
    };


    _drv_goldengate_ftm_get_key_id_info(p_ftm_info->tbl_type, key_size, &tcam_key_type, &key_type);

    drv_goldengate_ftm_get_tcam_info(tcam_key_type,
                             p_ftm_info->tbl_id,
                             tcam_ad_a,
                             &key_share_num,
                             &ad_share_num);
    if (0 == key_share_num || 0 == ad_share_num)
    {
        return DRV_E_NONE;
    }

    p_ftm_info->tbl_entry_num = key_share_num;

    /*tcam key alloc*/
    for (index = 0; index < key_share_num; index++)
    {
        tcam_key_id = p_ftm_info->tbl_id[index];

        if (tcam_key_id >= MaxTblId_t)
        {
            DRV_FTM_DBG_OUT("unexpect tbl id:%d\r\n", tcam_key_id);
            return DRV_E_INVAILD_TYPE;
        }

        p_ftm_info->max_idx[index]  = TABLE_MAX_INDEX(tcam_key_id);
        key_size            = TCAM_KEY_SIZE(tcam_key_id);
        if (tcam_key_type == DRV_FTM_TCAM_TYPE_IGS_LPM0)
        {
            key_size            = (key_size / DRV_LPM_KEY_BYTES_PER_ENTRY) * 40;
        }
        else
        {
            key_size            = (key_size / DRV_BYTES_PER_ENTRY) * 80;
        }

        if (tcam_key_type != DRV_FTM_TCAM_TYPE_IGS_LPM1)
        {
            if (old_key_size != key_size)
            {
                p_ftm_info->key_size[index] = key_size;
            }
            else
            {
                p_ftm_info->key_size[index] = 0;
            }
        }
        else
        {
            p_ftm_info->key_size[index] = key_size;
        }

        old_key_size = key_size;

    }

    sal_strcpy(p_ftm_info->str, arr_tcam_tbl[tcam_key_type]);

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_ftm_get_edram_info_detail(drv_ftm_info_detail_t* p_ftm_info)
{
    uint8 drv_tbl_id = 0;
    uint8 mem_id = 0;
    uint8 mem_num = 0;
    uint32 sram_tbl_a[50]       = {0};
    uint8 share_tbl_num         = 0;

    _drv_goldengate_ftm_get_tbl_id(p_ftm_info->tbl_type, &drv_tbl_id);

    _drv_goldengate_ftm_get_sram_tbl_id(drv_tbl_id, sram_tbl_a, &share_tbl_num);
    _drv_goldengate_ftm_get_sram_tbl_name(drv_tbl_id, p_ftm_info->str);

    for (mem_id = DRV_FTM_SRAM0; mem_id < DRV_FTM_SRAM_MAX; mem_id++)
    {
        if (IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(drv_tbl_id), mem_id) &&
            DRV_FTM_TBL_MEM_ENTRY_NUM(drv_tbl_id, mem_id))
        {
            p_ftm_info->mem_id[mem_num] = mem_id;
            p_ftm_info->entry_num[mem_num] = DRV_FTM_TBL_MEM_ENTRY_NUM(drv_tbl_id, mem_id);
            p_ftm_info->offset[mem_num] = DRV_FTM_TBL_MEM_START_OFFSET(drv_tbl_id, mem_id);

            ++ mem_num;
        }
    }

    p_ftm_info->tbl_entry_num = mem_num;

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_get_host1_poly_type(uint32* poly, uint32* poly_len, uint32* type)
{
    uint32 crc[][3]=  {{HASH_CRC, 0x00000053, 12},/*refer to fib_host1_crc*/
                                        {HASH_CRC, 0x00000107, 12},
                                        {HASH_CRC, 0x0000001b, 13},
                                        {HASH_CRC, 0x00000027, 13},
                                        {HASH_CRC, 0x0000002b, 14},
                                        {HASH_XOR, 16        , 0}};

    *type = crc[g_gg_ftm_master->host1_poly_type][0];
    *poly = crc[g_gg_ftm_master->host1_poly_type][1];
    *poly_len = crc[g_gg_ftm_master->host1_poly_type][2];

    return DRV_E_NONE;
}
int32 drv_goldengate_ftm_set_edit_couple(uint8 lchip, drv_ftm_tbl_info_t* p_edit_info)
{
    DynamicDsShareMacroCtl_m ds;
    uint32 cmd;
    uint32 couple_mode;
    uint32 entry_num;
    DRV_PTR_VALID_CHECK(p_edit_info);

    cmd = DRV_IOR(DynamicDsShareMacroCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds));

    entry_num = p_edit_info[0].mem_entry_num[DRV_FTM_SRAM14]+p_edit_info[1].mem_entry_num[DRV_FTM_SRAM14];
    couple_mode = (entry_num > DRV_L23EDITRAM0_MAX_ENTRY_NUM);
    DRV_IOW_FIELD(DynamicDsShareMacroCtl_t, DynamicDsShareMacroCtl_dsL2L3EditRam0LocalCouple_f, &couple_mode, &ds);

    entry_num = p_edit_info[0].mem_entry_num[DRV_FTM_SRAM15]+p_edit_info[1].mem_entry_num[DRV_FTM_SRAM15];
    couple_mode = (entry_num > DRV_L23EDITRAM1_MAX_ENTRY_NUM);
    DRV_IOW_FIELD(DynamicDsShareMacroCtl_t, DynamicDsShareMacroCtl_dsL2L3EditRam1LocalCouple_f, &couple_mode, &ds);

    cmd = DRV_IOW(DynamicDsShareMacroCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds));

    return DRV_E_NONE;
}

#define _____APIs_____

int32
drv_goldengate_ftm_lookup_ctl_init(uint8 lchip)
{
    DRV_IF_ERROR_RETURN(drv_goldengate_tcam_ctl_init(lchip));
    DRV_IF_ERROR_RETURN(drv_goldengate_dynamic_cam_init(lchip));
    DRV_IF_ERROR_RETURN(drv_goldengate_dynamic_arb_init(lchip));
    DRV_IF_ERROR_RETURN(drv_goldengate_flow_lkup_ctl_init(lchip));
    DRV_IF_ERROR_RETURN(drv_goldengate_fib_lkup_ctl_init(lchip));

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_mem_alloc(void* p_profile_info)
{
    drv_ftm_profile_info_t *profile_info = (drv_ftm_profile_info_t *)p_profile_info;

    DRV_PTR_VALID_CHECK(profile_info);

    if ((profile_info->profile_type >= 1)
        && ((profile_info->profile_type != 12)))
    {
        return DRV_E_INVAILD_TYPE;
    }

    /*init global param*/
    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_init());

    /*set profile*/
    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_set_profile(profile_info));

    /*alloc hash table*/
    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_sram());

    /* alloc tcam table*/
    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_tcam());

    /*re alloc tcam table*/
    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_realloc_tcam(profile_info->lpm0_tcam_share));


    DRV_IF_ERROR_RETURN(drv_goldengate_ftm_alloc_cam(&profile_info->cam_info));

    return DRV_E_NONE;


}

int32
drv_goldengate_ftm_get_lpm_tcam_info(uint32 tblid, uint32* p_entry_offset, uint32* p_entry_num, uint32* p_entry_size)
{
    uint8 size_type = 0;
    uint8 tcam_type = 0;
    uint8 mem_id = 0;

    size_type = drv_goldengate_ftm_alloc_tcam_key_map_szietype(tblid);

    if ((DsLpmTcamIpv440Key_t == tblid) || (DsLpmTcamIpv6160Key0_t == tblid))
    {
        tcam_type = DRV_FTM_TCAM_TYPE_IGS_LPM0;
        mem_id = DRV_FTM_TCAM_KEY7 - DRV_FTM_TCAM_KEY0;
        *p_entry_size = TABLE_ENTRY_SIZE(tblid) / DRV_LPM_KEY_BYTES_PER_ENTRY;
    }
    else if ((DsLpmTcamIpv4Pbr160Key_t == tblid) || (DsLpmTcamIpv6320Key_t == tblid)
            || (DsLpmTcamIpv4NAT160Key_t == tblid) || (DsLpmTcamIpv6160Key1_t == tblid))
    {
        tcam_type = DRV_FTM_TCAM_TYPE_IGS_LPM1;
        mem_id = DRV_FTM_TCAM_KEY8 - DRV_FTM_TCAM_KEY0;
        *p_entry_size = TABLE_ENTRY_SIZE(tblid) / DRV_BYTES_PER_ENTRY;
    }
    else
    {
        return DRV_E_INVALID_TCAM_TYPE;
    }

    *p_entry_offset = DRV_FTM_KEY_START_OFFSET(tcam_type, size_type, mem_id);
    *p_entry_num = DRV_FTM_KEY_ENTRY_NUM(tcam_type, size_type, mem_id);

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_alloc_cam_offset(uint32 tbl_id, uint32 *offset)
{
    uint8 bit_len = 0;
    uint32 *bitmap0  = 0;
    uint32 *bitmap1  = 0;
    uint32 *bitmap2  = 0;

    uint32 *bitmap    = 0;
    uint32 loop_cnt  = 0;
    uint32 bit_offset = 0;

    uint8 cam_type = 0;
    uint32 alloc_offset = 0;

    /*1. get clear bit*/
    bit_len  = TABLE_ENTRY_SIZE(tbl_id) / BYTE_TO_4W;

    _drv_goldengate_ftm_get_cam_by_tbl_id(tbl_id, &cam_type);
    if (DRV_FTM_CAM_TYPE_INVALID == cam_type)
    {
        return DRV_E_INVAILD_TYPE;
    }

    DRV_FTM_CAM_CHECK_TBL_VALID(cam_type, tbl_id);

    if ((DRV_FTM_CAM_TYPE_FIB_HOST1_NAT == cam_type)
        || (DRV_FTM_CAM_TYPE_FIB_HOST1_MC == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->fib1_cam_bitmap0[0];
        bitmap1 = &g_gg_ftm_master->fib1_cam_bitmap1[0];
        bitmap2 = &g_gg_ftm_master->fib1_cam_bitmap2[0];

        loop_cnt = 32/bit_len;
    }
    else if ((DRV_FTM_CAM_TYPE_SCL == cam_type)
        || (DRV_FTM_CAM_TYPE_DCN == cam_type)
        || (DRV_FTM_CAM_TYPE_MPLS == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->userid_cam_bitmap0[0];
        bitmap1 = &g_gg_ftm_master->userid_cam_bitmap1[0];
        bitmap2 = &g_gg_ftm_master->userid_cam_bitmap2[0];

        loop_cnt = 32/bit_len;
    }
    else if ((DRV_FTM_CAM_TYPE_XC == cam_type)
            || (DRV_FTM_CAM_TYPE_OAM == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->xcoam_cam_bitmap0[0];
        bitmap1 = &g_gg_ftm_master->xcoam_cam_bitmap1[0];
        bitmap2 = &g_gg_ftm_master->xcoam_cam_bitmap2[0];

        loop_cnt = 128/bit_len;
    }

    /*judge allocate or not*/
    if(g_gg_ftm_master->conflict_cam_num[cam_type] < bit_len)
    {
        return DRV_E_HASH_CONFLICT;
    }

    bitmap  = (1 == bit_len) ? bitmap0 : ((2 == bit_len) ? bitmap1 : bitmap2);

    for(bit_offset = 0; bit_offset < loop_cnt; bit_offset++)
    {
        if(!IS_BIT_SET(bitmap[(bit_offset/32)], (bit_offset%32)))
        {
            break;
        }
    }

    if(bit_offset == loop_cnt)
    {
        return DRV_E_HASH_CONFLICT;
    }

    /*alloc num*/
    g_gg_ftm_master->conflict_cam_num[cam_type] -= bit_len;

    alloc_offset  = bit_offset*bit_len;

    /*set bitmap*/
    if (1 == bit_len)
    {
        SET_BIT(bitmap0[alloc_offset/32], (alloc_offset%32));
        SET_BIT(bitmap1[(alloc_offset/2)/32], ((alloc_offset/2)%32));
        SET_BIT(bitmap2[(alloc_offset/4)/32], ((alloc_offset/4)%32));
    }
    else if (2 == bit_len)
    {
        SET_BIT(bitmap0[alloc_offset/32], (alloc_offset%32));
        SET_BIT(bitmap0[(alloc_offset+1)/32], ((alloc_offset+1)%32));

        SET_BIT(bitmap1[(alloc_offset/2)/32], ((alloc_offset/2)%32));
        SET_BIT(bitmap2[(alloc_offset/4)/32], ((alloc_offset/4)%32));
    }
    else if (4 == bit_len)
    {
        SET_BIT(bitmap0[alloc_offset/32], (alloc_offset%32));
        SET_BIT(bitmap0[(alloc_offset+1)/32], ((alloc_offset+1)%32));
        SET_BIT(bitmap0[(alloc_offset+2)/32], ((alloc_offset+2)%32));
        SET_BIT(bitmap0[(alloc_offset+3)/32], ((alloc_offset+3)%32));

        SET_BIT(bitmap1[(alloc_offset/2)/32], ((alloc_offset/2)%32));
        SET_BIT(bitmap1[(alloc_offset/2+1)/32], ((alloc_offset/2+1)%32));

        SET_BIT(bitmap2[(alloc_offset/4)/32], ((alloc_offset/4)%32));
    }

    *offset = alloc_offset;

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_free_cam_offset(uint32 tbl_id, uint32 offset)
{
    uint8 bit_len = 0;
    uint32 *bitmap0  = 0;
    uint32 *bitmap1  = 0;
    uint32 *bitmap2  = 0;

    uint8 bit_offset0 = 0;
    uint8 bit_offset1 = 0;
    uint8 bit_offset2 = 0;

    uint8 cam_type = 0;
    uint8 is_alloc = 0;

    /*1. get clear bit*/
    bit_len  = TABLE_ENTRY_SIZE(tbl_id) / 12;


    _drv_goldengate_ftm_get_cam_by_tbl_id(tbl_id, &cam_type);
    if (DRV_FTM_CAM_TYPE_INVALID == cam_type)
    {
        return DRV_E_INVAILD_TYPE;
    }

    if ((DRV_FTM_CAM_TYPE_FIB_HOST1_NAT == cam_type)
        || (DRV_FTM_CAM_TYPE_FIB_HOST1_MC == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->fib1_cam_bitmap0[0];
        bitmap1 = &g_gg_ftm_master->fib1_cam_bitmap1[0];
        bitmap2 = &g_gg_ftm_master->fib1_cam_bitmap2[0];
        bit_offset0 = offset;
        bit_offset1 = offset/2;
        bit_offset2 = offset/4;
    }
    else if ((DRV_FTM_CAM_TYPE_SCL == cam_type)
        || (DRV_FTM_CAM_TYPE_DCN == cam_type)
        || (DRV_FTM_CAM_TYPE_MPLS == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->userid_cam_bitmap0[0];
        bitmap1 = &g_gg_ftm_master->userid_cam_bitmap1[0];
        bitmap2 = &g_gg_ftm_master->userid_cam_bitmap2[0];
        bit_offset0 = offset;
        bit_offset1 = offset/2;
        bit_offset2 = offset/4;
    }
    else if ((DRV_FTM_CAM_TYPE_XC == cam_type)
            || (DRV_FTM_CAM_TYPE_OAM == cam_type))
    {
        bitmap0 = &g_gg_ftm_master->xcoam_cam_bitmap0[offset/32];
        bitmap1 = &g_gg_ftm_master->xcoam_cam_bitmap1[offset/64];
        bitmap2 = &g_gg_ftm_master->xcoam_cam_bitmap2[offset/128];

        bit_offset0 = offset%32;
        bit_offset1 = (offset/2)%32;
        bit_offset2 = (offset/4)%32;
    }

    /*judge allocate or not*/
    if (1 == bit_len)
    {
        is_alloc = IS_BIT_SET((*bitmap0), bit_offset0);
    }
    else if (2 == bit_len)
    {
        is_alloc = IS_BIT_SET((*bitmap1), bit_offset1);
    }
    else if (4 == bit_len)
    {
        is_alloc = IS_BIT_SET((*bitmap2), bit_offset2);
    }

    if(!is_alloc)
    {
        return DRV_E_INVALID_ALLOC_INFO;
    }

    /*free num*/
    g_gg_ftm_master->conflict_cam_num[cam_type] += bit_len;

    /*free bitmap*/
    if (1 == bit_len)
    {
        CLEAR_BIT((*bitmap0), bit_offset0);


        if((!IS_BIT_SET((*bitmap0), (bit_offset0/2*2))
            &&(!IS_BIT_SET((*bitmap0), (bit_offset0/2*2 + 1)))))
        {
            CLEAR_BIT((*bitmap1), bit_offset1);
        }

        if(!IS_BIT_SET((*bitmap0), (bit_offset0/4*4))
            &&(!IS_BIT_SET((*bitmap0), (bit_offset0/4*4 + 1)))
            && (!IS_BIT_SET((*bitmap0), (bit_offset0/4*4 + 2)))
            && (!IS_BIT_SET((*bitmap0), (bit_offset0/4*4 + 3))))
        {
            CLEAR_BIT((*bitmap2), bit_offset2);
        }

    }
    else if (2 == bit_len)
    {
        CLEAR_BIT((*bitmap1), bit_offset1);

        CLEAR_BIT((*bitmap0), ((bit_offset0/2)*2));
        CLEAR_BIT((*bitmap0), ((bit_offset0/2)*2 + 1));

        if(!IS_BIT_SET((*bitmap1), (bit_offset1/2*2))
            &&(!IS_BIT_SET((*bitmap1), (bit_offset1/2*2 + 1))))
        {
            CLEAR_BIT((*bitmap2), bit_offset2);
        }

    }
    else if (4 == bit_len)
    {
        CLEAR_BIT((*bitmap2), bit_offset2);

        CLEAR_BIT((*bitmap1), ((bit_offset1/2)*2));
        CLEAR_BIT((*bitmap1), ((bit_offset1/2)*2 + 1));

        CLEAR_BIT((*bitmap0), ((bit_offset0/4)*4));
        CLEAR_BIT((*bitmap0), ((bit_offset0/4*4) + 1));
        CLEAR_BIT((*bitmap0), ((bit_offset0/4*4) + 2));
        CLEAR_BIT((*bitmap0), ((bit_offset0/4*4) + 3));
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_get_info_detail(drv_ftm_info_detail_t* p_ftm_info)
{

    switch (p_ftm_info->info_type)
    {
        case DRV_FTM_INFO_TYPE_CAM:
            _drv_goldengate_ftm_get_cam_info(p_ftm_info);
            break;
        case DRV_FTM_INFO_TYPE_TCAM:
            _drv_goldengate_ftm_get_tcam_info_detail(p_ftm_info);
            break;
        case DRV_FTM_INFO_TYPE_EDRAM:
            _drv_goldengate_ftm_get_edram_info_detail(p_ftm_info);
            break;
        default:
            break;
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_check_tbl_recover(uint8 mem_id, uint32 ram_offset, uint8* p_recover, uint32* p_tblid)
{
    uint32 sram_type       = 0, tbl_id = 0;
    uint32 mem_entry_num   = 0;
    uint32 start_offset    = 0, entry_num = 0;
    uint32 sram_tbl_a[50]  = {0};
    uint32 tcam_ad_a[20]   = {0};
    uint8  share_tbl_num   = 0, size_type = 0, ad_share_num = 0, tcam_key_type = 0;
    uint8  is_lpm = 0, ad_id = 0;

    if ((NULL == p_recover) || (NULL == p_tblid))
    {
        return DRV_E_INVALID_POINT;
    }
    *p_tblid = MaxTblId_t;

    if (mem_id < DRV_FTM_SRAM_MAX)
    {
        for (sram_type = 0; sram_type < DRV_FTM_SRAM_TBL_MAX; sram_type++)
        {
            mem_entry_num = DRV_FTM_TBL_MEM_ENTRY_NUM(sram_type, mem_id);
            if (!IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(sram_type), mem_id) || 0 == mem_entry_num)
            {
                continue;
            }
            else
            {
                start_offset = DRV_FTM_TBL_MEM_START_OFFSET(sram_type, mem_id);
                if ((ram_offset >= start_offset) && (ram_offset < (start_offset+ mem_entry_num)))
                {

                    break;
                }
                else
                {
                    continue;
                }
            }
        }

        if (sram_type == DRV_FTM_SRAM_TBL_MAX)
        {
            *p_recover = 0;
            return DRV_E_NONE;
        }
        else
        {
            /*
                DsOamLmStats DsBfdMep DsBfdRmep DsEthMep DsEthRmep can't recover
            */
            if ((DRV_FTM_SRAM_TBL_OAM_MEP == sram_type)
                || (DRV_FTM_SRAM_TBL_OAM_LM == sram_type)
                || (DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY == sram_type)
                || (DRV_FTM_SRAM_TBL_IPFIX_AD == sram_type))
            {
                *p_recover = 0;
            }
            else
            {
                *p_recover = 1;
            }

            _drv_goldengate_ftm_get_sram_tbl_id(sram_type, sram_tbl_a, &share_tbl_num);
            *p_tblid = sram_tbl_a[0];
        }
    }
    else if ((mem_id >= DRV_FTM_TCAM_AD0) && (mem_id < DRV_FTM_TCAM_ADM))
    {
        ad_id = mem_id - DRV_FTM_TCAM_AD0;

        for (tcam_key_type = DRV_FTM_TCAM_TYPE_IGS_ACL0; tcam_key_type < DRV_FTM_TCAM_TYPE_MAX; tcam_key_type++)
        {
            is_lpm = (DRV_FTM_TCAM_TYPE_IGS_LPM0 == tcam_key_type) || (DRV_FTM_TCAM_TYPE_IGS_LPM1 == tcam_key_type);

            for (size_type = DRV_FTM_TCAM_SIZE_160; size_type < DRV_FTM_TCAM_SIZE_MAX; size_type++)
            {
                drv_goldengate_ftm_get_tcam_ad_by_size_type(tcam_key_type, size_type, tcam_ad_a,  &ad_share_num);

                entry_num = 0;

                tbl_id = tcam_ad_a[0];
                entry_num =  DRV_FTM_KEY_ENTRY_NUM(tcam_key_type, size_type, ad_id);
                if (0 == entry_num)
                {
                    continue;
                }

                if (is_lpm)
                {
                    if (DRV_FTM_TCAM_AD7 == mem_id)
                    {
                        if ((ram_offset >= DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id))
                           && (ram_offset < (DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id) + entry_num)))
                        {
                            *p_tblid = tbl_id;
                            *p_recover = 1;
                            return DRV_E_NONE;
                        }
                    }
                    else
                    {
                        if ((ram_offset >= (DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id) / 2))
                            && (ram_offset < (DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id) / 2) + ((entry_num / 2))))
                        {
                            *p_tblid = tbl_id;
                            *p_recover = 1;
                            return DRV_E_NONE;
                        }
                    }
                }
                else
                {
                    if ((ram_offset >= DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id))
                        && (ram_offset < (DRV_FTM_KEY_START_OFFSET(tcam_key_type, size_type, ad_id) + entry_num)))
                    {
                        *p_tblid = tbl_id;
                        *p_recover = 1;
                        return DRV_E_NONE;
                    }
                }
            }
        }
    }
    else
    {
        return DRV_E_INVALID_PARAMETER;
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_get_sram_type(uint32 mem_id, uint8* p_sram_type)
{
    uint8 sram_type = 0;
    for (sram_type = DRV_FTM_SRAM_TBL_FIB0_HASH_KEY; sram_type <= DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY; sram_type++)
    {
        if (IS_BIT_SET(DRV_FTM_TBL_MEM_BITMAP(sram_type), mem_id))
        {
            break;
        }
    }

    switch (sram_type)
    {
        case DRV_FTM_SRAM_TBL_FIB0_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_FIB0_HASH_KEY;
            break;

        case DRV_FTM_SRAM_TBL_FIB1_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_FIB1_HASH_KEY;
            break;

        case DRV_FTM_SRAM_TBL_FLOW_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_FLOW_HASH_KEY;
            break;

        case DRV_FTM_SRAM_TBL_IPFIX_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_IPFIX_HASH_KEY;
            break;

        case DRV_FTM_SRAM_TBL_USERID_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_TYPE_SCL_HASH_KEY;
            break;

        case DRV_FTM_SRAM_TBL_XCOAM_HASH_KEY:
            *p_sram_type = DRV_FTM_TBL_XCOAM_HASH_KEY;
            break;

        default:
            *p_sram_type = DRV_FTM_TBL_TYPE_MAX;
            break;
    }

    return DRV_E_NONE;
}


int32
drv_goldengate_ftm_get_hash_poly(uint8 lchip, uint32 mem_id, uint8 sram_type, uint32* drv_poly)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;

    switch (sram_type)
    {
        case DRV_FTM_TBL_FLOW_HASH_KEY:
            tbl_id = FlowHashLookupCtl_t;
            if (DRV_FTM_SRAM0 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel0HashType_f;
            }
            else if (DRV_FTM_SRAM1 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel1HashType_f;
            }
            else if (DRV_FTM_SRAM2 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel2HashType_f;
            }
            else if (DRV_FTM_SRAM3 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel3HashType_f;
            }
            else if (DRV_FTM_SRAM4 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel4HashType_f;
            }
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, drv_poly);
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            tbl_id = UserIdHashLookupCtl_t;
            fld_id = ((DRV_FTM_SRAM2 == mem_id) || (DRV_FTM_SRAM11 == mem_id))?
            UserIdHashLookupCtl_userIdLevel0HashType_f : UserIdHashLookupCtl_userIdLevel1HashType_f;
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, drv_poly);
            break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            tbl_id = EgressXcOamHashLookupCtl_t;
            fld_id = (DRV_FTM_SRAM17 == mem_id)? EgressXcOamHashLookupCtl_egressXcOamLevel0HashType_f : EgressXcOamHashLookupCtl_egressXcOamLevel1HashType_f;
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, drv_poly);
            break;

        case DRV_FTM_TBL_FIB0_HASH_KEY:
            tbl_id = FibHost0HashLookupCtl_t;
            if (DRV_FTM_SRAM0 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level0HashType_f;
            }
            else if (DRV_FTM_SRAM1 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level1HashType_f;
            }
            else if (DRV_FTM_SRAM2 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level2HashType_f;
            }
            else if (DRV_FTM_SRAM3 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level3HashType_f;
            }
            else if (DRV_FTM_SRAM4 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level4HashType_f;
            }
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, drv_poly);
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            tbl_id = FibHost1HashLookupCtl_t;
            if (DRV_FTM_SRAM5 == mem_id)
            {
                fld_id = FibHost1HashLookupCtl_fibHost1DaLevel0HashType_f;
            }
            else if (DRV_FTM_SRAM6 == mem_id)
            {
                fld_id = FibHost1HashLookupCtl_fibHost1DaLevel1HashType_f;
            }
            else if (DRV_FTM_SRAM0 == mem_id)
            {
                fld_id = FibHost1HashLookupCtl_fibHost1DaLevel2HashType_f;
            }
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, drv_poly);
            break;

        default :
            break;

    }
    return DRV_E_NONE;
}


int32
drv_goldengate_ftm_set_hash_poly(uint8 lchip, uint32 mem_id, uint8 sram_type, uint32 drv_poly)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;

    switch (sram_type)
    {
        case DRV_FTM_TBL_FLOW_HASH_KEY:
            tbl_id = FlowHashLookupCtl_t;
            if (DRV_FTM_SRAM0 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel0HashType_f;
            }
            else if (DRV_FTM_SRAM1 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel1HashType_f;
            }
            else if (DRV_FTM_SRAM2 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel2HashType_f;
            }
            else if (DRV_FTM_SRAM3 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel3HashType_f;
            }
            else if (DRV_FTM_SRAM4 == mem_id)
            {
                fld_id = FlowHashLookupCtl_flowLevel4HashType_f;
            }
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            tbl_id = UserIdHashLookupCtl_t;
            fld_id = ((DRV_FTM_SRAM2 == mem_id) || (DRV_FTM_SRAM11 == mem_id))?
            UserIdHashLookupCtl_userIdLevel0HashType_f : UserIdHashLookupCtl_userIdLevel1HashType_f;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            tbl_id = EgressXcOamHashLookupCtl_t;
            fld_id = (DRV_FTM_SRAM17 == mem_id)? EgressXcOamHashLookupCtl_egressXcOamLevel0HashType_f : EgressXcOamHashLookupCtl_egressXcOamLevel1HashType_f;
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            break;

        case DRV_FTM_TBL_FIB0_HASH_KEY:
            tbl_id = FibHost0HashLookupCtl_t;
            if (DRV_FTM_SRAM0 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level0HashType_f;
            }
            else if (DRV_FTM_SRAM1 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level1HashType_f;
            }
            else if (DRV_FTM_SRAM2 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level2HashType_f;
            }
            else if (DRV_FTM_SRAM3 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level3HashType_f;
            }
            else if (DRV_FTM_SRAM4 == mem_id)
            {
                fld_id = FibHost0HashLookupCtl_fibHost0Level4HashType_f;
            }
            cmd = DRV_IOW(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            tbl_id = FibHost1HashLookupCtl_t;
            if (DRV_FTM_SRAM5 == mem_id)
            {
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel0HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel0HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            }
            else if (DRV_FTM_SRAM6 == mem_id)
            {
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel1HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel1HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            }
            else if (DRV_FTM_SRAM0 == mem_id)
            {
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1DaLevel2HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
                cmd = DRV_IOW(tbl_id, FibHost1HashLookupCtl_fibHost1SaLevel2HashType_f);
                DRV_IOCTL(lchip, 0, cmd, &drv_poly);
            }
            g_gg_ftm_master->host1_poly_type = drv_poly;
            break;

        default :
            break;

    }
    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_get_entry_num_by_size(uint8 lchip, uint32 tcam_key_type, uint32 size_type, uint32* entry_num)
{
    uint16 mem_id = 0;
    uint16 mem_idx = 0;
    uint32 count = 0;

    for (mem_idx = DRV_FTM_TCAM_KEY0; mem_idx < DRV_FTM_TCAM_KEYM; mem_idx++)
    {
         mem_id = mem_idx - DRV_FTM_TCAM_KEY0;

        count  += DRV_FTM_KEY_ENTRY_NUM(tcam_key_type, size_type, mem_id);
    }

    *entry_num = count/2;  /*unit out: 160bit*/

    return DRV_E_NONE;
}

int32
drv_goldengate_ftm_reset_dynamic_table(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value_0 = 0;
    uint32 value_1 = 1;

    cmd = DRV_IOW(DynamicDsHashInit0_t, DynamicDsHashInit0_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    DRV_IOCTL(lchip, 0, cmd, &value_1);

    cmd = DRV_IOW(DynamicDsHashInit1_t, DynamicDsHashInit1_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    DRV_IOCTL(lchip, 0, cmd, &value_1);

    cmd = DRV_IOW(DynamicDsShareInit_t, DynamicDsShareInit_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    DRV_IOCTL(lchip, 0, cmd, &value_1);

    cmd = DRV_IOW(DynamicDsAdInit0_t, DynamicDsAdInit0_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    DRV_IOCTL(lchip, 0, cmd, &value_1);

    cmd = DRV_IOW(DynamicDsAdInit1_t, DynamicDsAdInit1_init_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    DRV_IOCTL(lchip, 0, cmd, &value_1);

    cmd = DRV_IOR(DynamicDsHashInitDone0_t, DynamicDsHashInitDone0_initDone_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    if (!value_0)
    {
        return DRV_E_INIT_FAILED;
    }

    cmd = DRV_IOR(DynamicDsHashInitDone1_t, DynamicDsHashInitDone1_initDone_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    if (!value_0)
    {
        return DRV_E_INIT_FAILED;
    }

    cmd = DRV_IOR(DynamicDsShareInitDone_t, DynamicDsShareInitDone_initDone_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    if (!value_0)
    {
        return DRV_E_INIT_FAILED;
    }

    cmd = DRV_IOR(DynamicDsAdInitDone0_t, DynamicDsAdInitDone0_initDone_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    if (!value_0)
    {
        return DRV_E_INIT_FAILED;
    }

    cmd = DRV_IOR(DynamicDsAdInitDone1_t, DynamicDsAdInitDone1_initDone_f);
    DRV_IOCTL(lchip, 0, cmd, &value_0);
    if (!value_0)
    {
        return DRV_E_INIT_FAILED;
    }
    return DRV_E_NONE;
}


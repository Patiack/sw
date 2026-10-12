/*
 @file sys_greatbelt_ftm.c

 @date 2011-11-16

 @version v2.0

 This file provides all sys alloc function
*/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_ftm.h"
#include "ctc_debug.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_ftm_db.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"


#define NETWORK_CFG_FILE            "dut_topo.txt"
#define SYS_FTM_DBG_DUMP(FMT, ...)       \
    if (1)                                    \
    {                                        \
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);        \
    }

#define SYS_FTM_USERID_INGRESS_MAX_NUM  256
#define SYS_FTM_USERID_EGRESS_MAX_NUM   256
#define SYS_FTM_USERID_TUNNEL_MAX_NUM   64
#define SYS_FTM_USERID_AD_MAX_NUM   (8 * 1024)
#define SYS_FTM_DYNAMIC_SHIFT_NUM 10

#define SYS_FTM_TBL_INFO(tbl_id)                      g_ftm_master[lchip]->p_sram_tbl_info[tbl_id]
#define SYS_FTM_TBL_ACCESS_FLAG(tbl_id)               SYS_FTM_TBL_INFO(tbl_id).access_flag
#define SYS_FTM_TBL_MAX_ENTRY_NUM(tbl_id)             SYS_FTM_TBL_INFO(tbl_id).max_entry_num
#define SYS_FTM_TBL_MEM_BITMAP(tbl_id)                SYS_FTM_TBL_INFO(tbl_id).mem_bitmap
#define SYS_FTM_TBL_MEM_START_OFFSET(tbl_id, mem_id)  SYS_FTM_TBL_INFO(tbl_id).mem_start_offset[mem_id]
#define SYS_FTM_TBL_MEM_ENTRY_NUM(tbl_id, mem_id)     SYS_FTM_TBL_INFO(tbl_id).mem_entry_num[mem_id]

#define SYS_FTM_TBL_BASE_NUM(tbl_id)      SYS_FTM_TBL_INFO(tbl_id).dyn_ds_base.base_num
#define SYS_FTM_TBL_BASE_VAL(tbl_id, i)   SYS_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_base[i]
#define SYS_FTM_TBL_BASE_MIN(tbl_id, i)   SYS_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_min_index[i]
#define SYS_FTM_TBL_BASE_MAX(tbl_id, i)   SYS_FTM_TBL_INFO(tbl_id).dyn_ds_base.ds_dynamic_max_index[i]

#define SYS_FTM_MEM_ALLOCED_ENTRY_NUM(mem_id)         ftm_mem_info[mem_id].allocated_entry_num
#define SYS_FTM_MEM_MAX_ENTRY_NUM(mem_id)             ftm_mem_info[mem_id].max_mem_entry_num
#define SYS_FTM_MEM_LEFT_ENTRY_NUM(mem_id)            (SYS_FTM_MEM_MAX_ENTRY_NUM(mem_id) - SYS_FTM_MEM_ALLOCED_ENTRY_NUM(mem_id))

#define SYS_FTM_KEY_MAX_INDEX(tbl_id)                 g_ftm_master[lchip]->p_tcam_key_info[tbl_id].max_key_index
#define SYS_FTM_KEY_KEY_SIZE(tbl_id)                  g_ftm_master[lchip]->p_tcam_key_info[tbl_id].key_size

#define SYS_FTM_DYN_NH_BITMAP(dyn_type)                  g_ftm_master[lchip]->nh_info[dyn_type].tbl_bitmap
#define SYS_FTM_DYN_NH_VALID(dyn_type)                    g_ftm_master[lchip]->nh_info[dyn_type].valid

#define SYS_FTM_ADD_TABLE(mem_id, table, start, size) \
    do { \
        if (size) \
        { \
            SYS_FTM_TBL_MEM_BITMAP(table)               |= (1 << mem_id); \
            SYS_FTM_TBL_MAX_ENTRY_NUM(table)            += size; \
            SYS_FTM_TBL_MEM_START_OFFSET(table, mem_id) = start; \
            SYS_FTM_TBL_MEM_ENTRY_NUM(table, mem_id)    = size; \
        } \
    } while (0)

#define SYS_FTM_ADD_KEY(key, key_size, max_index)   \
    do { \
        SYS_FTM_KEY_MAX_INDEX(key)  = max_index; \
        SYS_FTM_KEY_KEY_SIZE(key)   = key_size; \
    } while (0)

enum sys_ftm_mem_id_e
{
    SYS_FTM_SRAM0,
    SYS_FTM_SRAM1,
    SYS_FTM_SRAM2,
    SYS_FTM_SRAM3,
    SYS_FTM_SRAM4,
    SYS_FTM_SRAM5,
    SYS_FTM_SRAM6,
    SYS_FTM_SRAM7,
    SYS_FTM_SRAM8,
    SYS_FTM_SRAM_MAX,

    SYS_FTM_TCAM_KEY0,
    SYS_FTM_SRAM_AD0,

    SYS_FTM_TCAM_KEY1,
    SYS_FTM_SRAM_AD1,

    SYS_FTM_MAX_ID
};
typedef enum sys_ftm_mem_id_e sys_ftm_mem_id_t;

enum sys_ftm_sram_tbl_type_e
{

    SYS_FTM_SRAM_TBL_FIB_HASH_AD,

    SYS_FTM_SRAM_TBL_LPM_LKP_KEY1,
    SYS_FTM_SRAM_TBL_LPM_LKP_KEY2,
    SYS_FTM_SRAM_TBL_LPM_LKP_KEY0,
    SYS_FTM_SRAM_TBL_LPM_LKP_KEY3,
    SYS_FTM_SRAM_TBL_LPM_HASH_KEY,
    SYS_FTM_SRAM_TBL_FIB_HASH_KEY,

    SYS_FTM_SRAM_TBL_MPLS,

    SYS_FTM_SRAM_TBL_NEXTHOP,
    SYS_FTM_SRAM_TBL_FWD,
    SYS_FTM_SRAM_TBL_MET,

    SYS_FTM_SRAM_TBL_OAM_MEP,
    SYS_FTM_SRAM_TBL_OAM_MA,
    SYS_FTM_SRAM_TBL_OAM_MA_NAME,     /*for adjust tbl*/

    SYS_FTM_SRAM_TBL_EDIT,

    SYS_FTM_SRAM_TBL_USERID_HASH_AD,
    SYS_FTM_SRAM_TBL_USERID_IGS_DFT,   /*for adjust tbl*/
    SYS_FTM_SRAM_TBL_USERID_EGS_DFT,   /*for adjust tbl*/
    SYS_FTM_SRAM_TBL_USERID_TUNL_DFT,  /*for adjust tbl*/

    SYS_FTM_SRAM_TBL_OAM_CHAN,         /*for adjust tbl*/

    SYS_FTM_SRAM_TBL_USERID_HASH_KEY,

    SYS_FTM_SRAM_TBL_STATS,
    SYS_FTM_SRAM_TBL_LM,
    SYS_FTM_SRAM_TBL_MAX
};
typedef enum sys_ftm_sram_tbl_type_e sys_ftm_sram_tbl_type_t;

enum sys_ftm_tcam_key_type_e
{
    SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0,
    SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1,

    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3,

    SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0,
    SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS1,
    SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS2,
    SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3,

    SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS,
    SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1_EGRESS,

    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0_EGRESS,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1_EGRESS,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2_EGRESS,
    SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS,

    SYS_FTM_TCAM_TYPE_FIB_COLISION,
    SYS_FTM_TCAM_TYPE_MAC_IPV4,
    SYS_FTM_TCAM_TYPE_MAC_IPV6,
    SYS_FTM_TCAM_TYPE_IPV4_UCAST,
    SYS_FTM_TCAM_TYPE_IPV6_UCAST,
    SYS_FTM_TCAM_TYPE_IPV4_MCAST,
    SYS_FTM_TCAM_TYPE_IPV6_MCAST,
    SYS_FTM_TCAM_TYPE_IPV4_NAT,
    SYS_FTM_TCAM_TYPE_IPV6_NAT,
    SYS_FTM_TCAM_TYPE_IPV4_PBR,
    SYS_FTM_TCAM_TYPE_IPV6_PBR,
    SYS_FTM_TCAM_TYPE_MAC_USERID,
    SYS_FTM_TCAM_TYPE_IPV4_USERID,
    SYS_FTM_TCAM_TYPE_IPV6_USERID,

    SYS_FTM_TCAM_TYPE_VLAN_USERID,
    SYS_FTM_TCAM_TYPE_IPV4_TUNNELID,
    SYS_FTM_TCAM_TYPE_IPV6_TUNNELID,

    SYS_FTM_TCAM_TYPE_PBB_TUNNELID,
    SYS_FTM_TCAM_TYPE_WLAN_TUNNELID,
    SYS_FTM_TCAM_TYPE_TRILL_TUNNELID,
    SYS_FTM_TCAM_TYPE_LPM_USERID,
    SYS_FTM_TCAM_TYPE_MAX

};
typedef enum tcam_key_type_e tcam_key_type_t;

enum sys_ftm_dyn_cam_type_e
{
    SYS_FTM_DYN_CAM_TYPE_DS_MAC_HASH,
    SYS_FTM_DYN_CAM_TYPE_DS_LPM_HASH,
    SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY0,
    SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY1,
    SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY2,
    SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY3,
    SYS_FTM_DYN_CAM_TYPE_DS_MAC_IP,
    SYS_FTM_DYN_CAM_TYPE_DS_USERID_HASH,
    SYS_FTM_DYN_CAM_TYPE_DS_USERID,
    SYS_FTM_DYN_CAM_TYPE_DS_FWD,
    SYS_FTM_DYN_CAM_TYPE_DS_MET,
    SYS_FTM_DYN_CAM_TYPE_DS_NEXTHOP,
    SYS_FTM_DYN_CAM_TYPE_DS_EDIT,
    SYS_FTM_DYN_CAM_TYPE_DS_MPLS,
    SYS_FTM_DYN_CAM_TYPE_DS_OAM,
    SYS_FTM_DYN_CAM_TYPE_DS_LM,
    SYS_FTM_DYN_CAM_TYPE_DS_STATS,
    SYS_FTM_DYN_CAM_TYPE_MAX
};

enum sys_ftm_opf_type_e
{
    SYS_FTM_OPF_TYPE_TCAM_KEY0,
    SYS_FTM_OPF_TYPE_TCAM_KEY1_2,
    SYS_FTM_OPF_TYPE_TCAM_KEY3,
    SYS_FTM_OPF_TYPE_TCAM_AD0,
    SYS_FTM_OPF_TYPE_TCAM_AD1_2,
    SYS_FTM_OPF_TYPE_TCAM_AD3,

    SYS_FTM_OPF_TYPE_LPM_KEY,
    SYS_FTM_OPF_TYPE_LPM_AD,

    SYS_FTM_OPF_TYPE_FIB_COLISION,
    SYS_FTM_OPF_TYPE_FIB_AD,
    SYS_FTM_OPF_TYPE_LPM_USERID,
    SYS_FTM_OPF_TYPE_MAX
};
typedef enum sys_ftm_opf_type_e sys_ftm_opf_type_t;


enum sys_ftm_acl_mode_e
{
    SYS_FTM_ACL_MODE_4LKP,
    SYS_FTM_ACL_MODE_2LKP,
    SYS_FTM_ACL_MODE_1LKP,
    SYS_FTM_ACL_MODE_0LKP,
    SYS_FTM_ACL_MODE_LKP_MAX
};
typedef enum sys_ftm_acl_mode_e sys_ftm_acl_mode_t;


struct sys_ftm_mem_allo_info_s
{
    uint32 allocated_entry_num;            /* entry size is always 72/80 */
    uint32 max_mem_entry_num;
    uint32 hw_data_base_addr_4w;     /* to dynic memory and TcamAd memory, is 4word mode base address */
    uint32 hw_data_base_addr_8w;     /* to dynic memory and TcamAd memory, is 8word mode base address */
};
typedef struct sys_ftm_mem_allo_info_s sys_ftm_mem_allo_info_t;

static sys_ftm_mem_allo_info_t ftm_mem_info[SYS_FTM_SRAM_MAX] =
{
    {0, DRV_MEMORY0_MAX_ENTRY_NUM, DRV_MEMORY0_BASE_4W, DRV_MEMORY0_BASE_8W},
    {0, DRV_MEMORY1_MAX_ENTRY_NUM, DRV_MEMORY1_BASE_4W, DRV_MEMORY1_BASE_8W},
    {0, DRV_MEMORY2_MAX_ENTRY_NUM, DRV_MEMORY2_BASE_4W, DRV_MEMORY2_BASE_8W},
    {0, DRV_MEMORY3_MAX_ENTRY_NUM, DRV_MEMORY3_BASE_4W, DRV_MEMORY3_BASE_8W},
    {0, DRV_MEMORY4_MAX_ENTRY_NUM, DRV_MEMORY4_BASE_4W, DRV_MEMORY4_BASE_8W},
    {0, DRV_MEMORY5_MAX_ENTRY_NUM, DRV_MEMORY5_BASE_4W, DRV_MEMORY5_BASE_8W},
    {0, DRV_MEMORY6_MAX_ENTRY_NUM, DRV_MEMORY6_BASE_4W, DRV_MEMORY6_BASE_8W},
    {0, DRV_MEMORY7_MAX_ENTRY_NUM, DRV_MEMORY7_BASE_4W, DRV_MEMORY7_BASE_8W},
    {0, DRV_MEMORY8_MAX_ENTRY_NUM, DRV_MEMORY8_BASE_4W, DRV_MEMORY8_BASE_8W}
};

struct sys_ftm_dyn_ds_base_s
{
    uint32 base_num;
    uint32 ds_dynamic_base[5];
    uint32 ds_dynamic_max_index[5];
    uint32 ds_dynamic_min_index[5];
};
typedef struct sys_ftm_dyn_ds_base_s sys_ftm_dyn_ds_base_t;

struct sys_ftm_sram_tbl_info_s
{
    uint16 access_flag; /*judge weather tbl share alloc*/
    uint16 mem_bitmap;
    uint32 max_entry_num;
    uint32 mem_start_offset[SYS_FTM_SRAM_MAX];
    uint32 mem_entry_num[SYS_FTM_SRAM_MAX];
    sys_ftm_dyn_ds_base_t dyn_ds_base;
};
typedef struct sys_ftm_sram_tbl_info_s sys_ftm_sram_tbl_info_t;

struct sys_ftm_tcam_key_info_s
{
    uint32 max_key_index;
    uint8 key_size;
    uint8 rsv0;
    uint16 rsv1;
};
typedef struct sys_ftm_tcam_key_info_s sys_ftm_tcam_key_info_t;

enum sys_ftm_dyn_nh_type_e
{
    SYS_FTM_DYN_NH0,
    SYS_FTM_DYN_NH1,
    SYS_FTM_DYN_NH2,
    SYS_FTM_DYN_NH3,
    SYS_FTM_DYN_NH_MAX
};

struct sys_ftm_dyn_nh_info_s
{
    uint16 tbl_bitmap;
    uint8 valid;
    uint8 rsv;
};
typedef struct sys_ftm_dyn_nh_info_s sys_ftm_dyn_nh_info_t;

struct g_ftm_master_s
{
    sys_ftm_sram_tbl_info_t* p_sram_tbl_info;
    sys_ftm_tcam_key_info_t* p_tcam_key_info;

    sys_ftm_dyn_nh_info_t nh_info[SYS_FTM_DYN_NH_MAX];

    uint32 glb_met_size;
    uint32 glb_nexthop_size;
    uint16 vsi_number;
    uint16 ecmp_number;
    uint16 logic_met_number;
    uint16 ipuc_v4_num;
    uint16 ipmc_v4_num;

    uint16 int_tcam_used_entry;
    uint8 profile_type;
    uint8 acl_mac_mode;

    uint16 tcam_head_offset;
    uint16 tcam_tail_offset;

    uint16 tcam_ad_head_offset;
    uint16 tcam_ad_tail_offset;

    uint8  acl1_mode;
    uint8  acl_ipv6_mode;
    uint8  ip_tcam_share_mode;/*0: individual, 1: share, include ipuc and ipmc*/
    uint8  resv;

    p_sys_ftm_tcam_cb_t tcam_cb[SYS_FTM_TCAM_KEY_MAX];
};
typedef struct g_ftm_master_s g_ftm_master_t;

#if (SDK_WORK_PLATFORM == 1)
extern int32 swemu_environment_setting(char* filename);
extern int32 sim_model_init(uint32 um_emu_mode);
extern int32 sram_model_initialize(uint8 lchip);
extern int32 tcam_model_initialize(uint8 lchip);
extern int32 sram_model_allocate_sram_tbl_info(void);
#endif

g_ftm_master_t* g_ftm_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_FTM_INIT_CHECK(lchip)                                 \
    do {                                                            \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                            \
        if (NULL == g_ftm_master[lchip])                          \
        {                                                          \
            return CTC_E_NOT_INIT;                                 \
        }                                                          \
    } while(0)

int32
_sys_greatbelt_ftm_init_sim_module(uint8 lchip, uint8 local_chip_num)
{
    int32 ret;

    /*process only to avoid warning*/
    ret = 0;
    ret = ret;



#ifndef SDK_IN_VXWORKS
    /* software simulation platform,init  testing swemu environment*/
#if (SDK_WORK_ENV == 1 && SDK_WORK_PLATFORM == 1)
    {
       if(!lchip)
       {
           ret = swemu_environment_setting(NETWORK_CFG_FILE);
           if (ret != 0)
           {
               SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"swemu_environment_setting failed:%s@%d \n",  __FUNCTION__, __LINE__);
               return ret;
           }

        }
        ret = sram_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }


        ret  = sram_model_allocate_sram_tbl_info();
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_allocate_sram_tbl_info failed:%s@%d %s\n",  __FUNCTION__, __LINE__, ctc_get_error_desc(ret));
            return ret;
        }

        /* um emu mode sdk */
        ret = sim_model_init(0);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sim_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }
    }
    /* software simulation platform,init memmodel*/
#elif (SDK_WORK_PLATFORM == 1)
    {

        ret = sram_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret  = sram_model_allocate_sram_tbl_info();
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_allocate_sram_tbl_info failed:%s@%d %s\n",  __FUNCTION__, __LINE__, ctc_get_error_desc(ret));
            return ret;
        }
    }
#endif
#else
    /* software simulation platform,init memmodel*/
#if (SDK_WORK_PLATFORM == 1)
    {

        ret = sram_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }


        ret  = sram_model_allocate_sram_tbl_info();
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_allocate_sram_tbl_info failed:%s@%d %s\n",  __FUNCTION__, __LINE__, ctc_get_error_desc(ret));
            return ret;
        }
    }
#endif
#endif

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_dsuserid(uint8 lchip, uint32 mem_id,
                                        uint32 start,
                                        uint32 userid_ad_size,
                                        uint32 oam_session)
{
    uint8 table            = 0;
    uint32 start_offset    = 0;

    /*DsUserid*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    start_offset =  start;
    userid_ad_size = (userid_ad_size - 256 * 2 - 128 - oam_session);
    SYS_FTM_ADD_TABLE(mem_id, table, start_offset, userid_ad_size);

    /*DsIgsDefault*/
    table = SYS_FTM_SRAM_TBL_USERID_IGS_DFT;
    start_offset += userid_ad_size;
    SYS_FTM_ADD_TABLE(mem_id, table, start_offset, 256);

    /*DsEgsDefault*/
    table = SYS_FTM_SRAM_TBL_USERID_EGS_DFT;
    start_offset +=  256;
    SYS_FTM_ADD_TABLE(mem_id, table, start_offset, 256);

    /*DsTunnelDefault*/
    table = SYS_FTM_SRAM_TBL_USERID_TUNL_DFT;
    start_offset +=  256;
    SYS_FTM_ADD_TABLE(mem_id, table, start_offset, 128);

    /*DsChan*/
    if (oam_session)
    {
        table = SYS_FTM_SRAM_TBL_OAM_CHAN;
        start_offset +=  128;
        SYS_FTM_ADD_TABLE(mem_id, table, start_offset, oam_session);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ftm_set_profile_default(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024,  8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 25 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 8 * 1024, 4 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 16 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 6 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 6 * 1024, 6 * 1024);

    /*Mpls*/
    table = SYS_FTM_SRAM_TBL_MPLS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 8 * 1024);

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 12 * 1024, 20 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 24 * 1024, 8 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 19 * 1024, 13 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 12 * 1024, 12 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 10 * 1024);

    /*OAM 1K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 25 * 1024, 4 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 29 * 1024, 1 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 30 * 1024, 2 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 10*1024, 9*1024, 1024);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 15 * 1024);

    /*Lm*/
    table = SYS_FTM_SRAM_TBL_LM;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 15 * 1024, 1 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*userid ipv6*/
    key = SYS_FTM_TCAM_TYPE_IPV6_USERID;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*ipv6 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_UCAST;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*ipv6 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_MCAST;
    SYS_FTM_ADD_KEY(key, 4, 128);

    /*ipv4 pbr*/
    key = SYS_FTM_TCAM_TYPE_IPV4_PBR;
    SYS_FTM_ADD_KEY(key, 2, 64);
    /*ipv6 pbr*/
    key = SYS_FTM_TCAM_TYPE_IPV6_PBR;
    SYS_FTM_ADD_KEY(key, 8, 32);

    /*ipv4 nat*/
    key = SYS_FTM_TCAM_TYPE_IPV4_NAT;
    SYS_FTM_ADD_KEY(key, 2, 64);

    /*ipv4 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV4_TUNNELID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*ipv6 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV6_TUNNELID;
    SYS_FTM_ADD_KEY(key, 8, 32);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 192);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 192);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 192);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 192);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_enterprise(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024,  8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 32 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 8 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 8 * 1024, 8 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 12 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 12 * 1024, 12 * 1024);

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 24 * 1024, 8 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 24 * 1024, 8 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 24 * 1024, 8 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 8 * 1024, 16 * 1024);

    /*OAM 1K session */
    /*MEP*/
    /*Not alloc*/

    /*MA*/
    /*Not alloc*/

    /*MA NAME*/
    /*Not alloc*/

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 0, 8*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD) | (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 1024);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);

    /*ipv4 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV4_TUNNELID;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*ipv6 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV6_TUNNELID;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 256);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 256);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 256);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 256);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_bridge(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 16 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 32 * 1024);

    /*Lpm Hash key*/
    /*not alloc*/

    /*Lpm Pipe line key1*/
    /*not alloc*/

    /*Lpm Pipe line key2*/
    /*not alloc*/

    /*Lpm Pipe line key0*/
    /*not alloc*/

    /*Lpm Pipe line key3*/
    /*not alloc*/

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 8 * 1024, 8 * 1024);
    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 8 * 1024, 8 * 1024);
    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 8 * 1024, 8 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 16 * 1024, 16 * 1024);

    /*OAM 1K session */
    /*MEP*/
    /*Not alloc*/

    /*MA*/
    /*Not alloc*/

    /*MA NAME*/
    /*MA*/
    /*Not alloc*/

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 0, 8*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD) | \
        (1 << SYS_FTM_SRAM_TBL_EDIT) | \
        (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    /********************************************
    tcam key
    *********************************************/

    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_ipv4_route_host(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024,  8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0,       32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 20 * 1024, 8 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 8 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 8 * 1024, 8 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 16 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 16 * 1024, 16 * 1024);

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0,  16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0,  20 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 26 * 1024, 6 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 26 * 1024, 6 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 12 * 1024, 14 * 1024);

    /*OAM 1K session */
    /*MEP*/
    /*Not alloc*/
    /*MA*/
    /*Not alloc*/
    /*MA NAME*/
    /*Not alloc*/

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 0, 12*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD) | (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);

    /*ipv4 pbr*/
    key = SYS_FTM_TCAM_TYPE_IPV4_PBR;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*ipv6 pbr*/
    key = SYS_FTM_TCAM_TYPE_IPV6_PBR;
    SYS_FTM_ADD_KEY(key, 8, 32);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 320);
    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 320);
    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 320);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 320);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_ipv4_route_lpm(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 24 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024,  8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0,       24 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 32 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 16 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 24 * 1024);

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16*1024,  16 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 20 * 1024, 12 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 20 * 1024, 12 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 4 * 1024, 16 * 1024);

    /*OAM 1K session */
    /*MEP*/
    /*Not alloc*/

    /*MA*/
    /*Not alloc*/

    /*MA NAME*/
    /*Not alloc*/

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    /*DsUserid*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24 * 1024, 8 * 1024);
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 0, 4*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD) | (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 1024);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 320);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 320);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 320);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 320);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_ipv6_route_lpm(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 24 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0,       16 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0,       24 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 24 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 16 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 16 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 16 * 1024);

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024,  16 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 26 * 1024, 6 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 16 * 1024, 10 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 16 * 1024);

    /*OAM 1K session */
    /*MEP*/
    /*Not alloc*/

    /*MA*/
    /*Not alloc*/

    /*MA NAME*/
    /*Not alloc*/

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM4, 24 * 1024, 4*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 64);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 128);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*userid ipv6*/
    key = SYS_FTM_TCAM_TYPE_IPV6_USERID;
    SYS_FTM_ADD_KEY(key, 8, 32);

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv6 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_UCAST;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*ipv6 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_MCAST;
    SYS_FTM_ADD_KEY(key, 4, 256);

    /*ipv4 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV4_TUNNELID;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*ipv6 tunnel*/
    key = SYS_FTM_TCAM_TYPE_IPV6_TUNNELID;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 192);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 192);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 192);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 192);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_metro(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0,       32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0,       32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0,       8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0,       8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0,       16 * 1024);

    /*Lpm Hash key*/
    /*Lpm Pipe line key1*/
    /*Lpm Pipe line key2*/
    /*Lpm Pipe line key0*/
    /*Lpm Pipe line key3*/

    /*Mpls*/
    /*Not alloc*/

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 6 * 1024, 10 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 4 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 6 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 4 * 1024, 16 * 1024);

    /*OAM 4K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 16 * 1024, 16 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 20 * 1024, 4 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 24 * 1024, 8 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    /*DsUserid*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 8 * 1024, 24 * 1024);
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM6, 8 * 1024, 8 * 1024, 4*1024);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_vpws(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0,       16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0,       8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 24 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0,       8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0,       12 * 1024);

    /*Lpm Hash key*/
    /*Lpm Pipe line key1*/
    /*Lpm Pipe line key2*/
    /*Lpm Pipe line key0*/
    /*Lpm Pipe line key3*/

    /*Mpls*/
    table = SYS_FTM_SRAM_TBL_MPLS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 8 * 1024, 24 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24 * 1024, 8 * 1024);

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 19 * 1024, 5 * 1024);
    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 16 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 19 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 12 * 1024, 12 * 1024);

    /*OAM 4K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 26 * 1024, 6 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024, 2 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 10 * 1024, 2 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 12 * 1024, 4 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 24 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    /*DsUserid*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 24 * 1024);
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM4, 16 * 1024, 6 * 1024, 2*1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 24 * 1024, 2 * 1024);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 1024);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_vpls(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0,       16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0,       32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0,       8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0,       24 * 1024);

    /*Lpm Hash key*/
    /*Lpm Pipe line key1*/
    /*Lpm Pipe line key2*/
    /*Lpm Pipe line key0*/
    /*Lpm Pipe line key3*/

    /*Mpls*/
    table = SYS_FTM_SRAM_TBL_MPLS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 16 * 1024, 16 * 1024);

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 27 * 1024, 5 * 1024);
    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 12 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 16 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 12 * 1024, 15 * 1024);

    /*OAM 4K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 9 * 1024, 4 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 13 * 1024, 1 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 14 * 1024, 2 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 24 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    /*DsUserid*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 9 * 1024);
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM6, 8 * 1024, 8 * 1024, 1024);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 1024);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_l3vpn(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 24 * 1024,       8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table,  8 * 1024,       8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0,       20 * 1024);


    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16*1024, 16 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 16*1024, 8 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 16*1024, 8 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 24*1024, 8 * 1024);

    /*Mpls*/
    table = SYS_FTM_SRAM_TBL_MPLS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 16 * 1024);

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 16 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 28 * 1024, 4 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 30 * 1024, 2 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 14 * 1024, 16 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 16 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 20 * 1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 18 * 1024, 10 * 1024);

    /*OAM 4K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 8 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 8 * 1024, 2 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 10 * 1024, 4 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 28 * 1024, 4 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM4, 0, 18 * 1024, 2*1024);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 128);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 128);
    /*userid ipv6*/

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 384);
    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 384);
    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 384);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 384);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_ptn(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 32* 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 8 * 1024, 24 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024,  8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 25 * 1024);

    /*Lpm Hash key*/
    table = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 8 * 1024);

    /*Lpm Pipe line key1*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 16 * 1024, 4 * 1024);

    /*Lpm Pipe line key2*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 8 * 1024);

    /*Lpm Pipe line key0*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 6 * 1024);

    /*Lpm Pipe line key3*/
    table = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 6 * 1024, 6 * 1024);

    /*Mpls*/
    table = SYS_FTM_SRAM_TBL_MPLS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 16 * 1024);

    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 20 * 1024, 12 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 24 * 1024, 8 * 1024);

    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 19 * 1024, 13 * 1024);

    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 12 * 1024, 12 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 10 * 1024);

    /*OAM 1K session */
    /*MEP*/
    table = SYS_FTM_SRAM_TBL_OAM_MEP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 25 * 1024, 4 * 1024);

    /*MA*/
    table = SYS_FTM_SRAM_TBL_OAM_MA;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 29 * 1024, 1 * 1024);

    /*MA NAME*/
    table = SYS_FTM_SRAM_TBL_OAM_MA_NAME;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 30 * 1024, 2 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 10*1024, 9*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 15 * 1024);

    /*Lm*/
    table = SYS_FTM_SRAM_TBL_LM;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 15 * 1024, 1 * 1024);

    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH2) = (1 << SYS_FTM_SRAM_TBL_NEXTHOP);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH2)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH3) = (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH3)  = 1;

    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 64);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, 128);
    /*userid ipv4*/
    key = SYS_FTM_TCAM_TYPE_IPV4_USERID;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*userid ipv6*/
    key = SYS_FTM_TCAM_TYPE_IPV6_USERID;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*ipv4 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
    SYS_FTM_ADD_KEY(key, 1, 512);
    /*ipv4 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*ipv6 ucast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_UCAST;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*ipv6 mcast*/
    key = SYS_FTM_TCAM_TYPE_IPV6_MCAST;
    SYS_FTM_ADD_KEY(key, 4, 256);

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 256);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 256);
    key = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 8, 64);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 256);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 192);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

/*
32K DsMac (0 4k 4  32K)
40K mac key (1 8 K 2 32 K )

48K DsUseid (3 32K 6 16K)
24 K usedid key(0, 8K; 7 ,16K)

32K dsFwd(5 28K  )
28K DsNexthop (1 24K 5 4K) share with edit
24 K DsMet (0 20K)
*/
STATIC int32
_sys_greatbelt_ftm_set_profile_pon(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;

    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8 * 1024, 8 * 1024);

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 22 * 1024, 10 * 1024);

    /*DsMet*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24*1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 22 * 1024);
    /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24*1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 22 * 1024);
    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24*1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 22 * 1024);
    /*DsFwd*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 24*1024, 8 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM5, table, 0, 22 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 32* 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 24 * 1024);
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM6, 0, 8*1024, 0);

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);


    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD )|
                                             (1 << SYS_FTM_SRAM_TBL_NEXTHOP)|
                                             (1 << SYS_FTM_SRAM_TBL_MET)|
                                             (1 << SYS_FTM_SRAM_TBL_EDIT);

    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;


    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
     /*key = SYS_FTM_TCAM_TYPE_FIB_COLISION;*/
     /*SYS_FTM_ADD_KEY(key, 1, 1048);*/

    /*userid mac*/
     /*key = SYS_FTM_TCAM_TYPE_MAC_USERID;*/
     /*SYS_FTM_ADD_KEY(key, 2, 64);*/
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, (512*1));
    /*userid ipv4*/
     /*key = SYS_FTM_TCAM_TYPE_IPV4_USERID;*/
     /*SYS_FTM_ADD_KEY(key, 4, 64);*/
    /*userid ipv6*/
     /*key = SYS_FTM_TCAM_TYPE_IPV6_USERID;*/
     /*SYS_FTM_ADD_KEY(key, 4, 64);*/

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 2, 1024);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 2, 1024);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 2, 1024);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 2, 256);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

#if 0
STATIC int32
_sys_greatbelt_ftm_set_profile_pon(uint8 lchip)
{
    uint8 table            = 0;
    uint8 key              = 0;

    /********************************************
    dynamic edram table
    *********************************************/
    /*FIB Hash Key*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;

    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM1, table, 0, 32 * 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM2, table, 0, 32 * 1024);
     /*SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0,  8 * 1024);*/

    /*FIB Hash AD*/
    table = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
     /*SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 16 * 1024);*/
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 0, 15 * 1024);
     /*SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8*1024,  8 * 1024);*/

    /*DsMet*/
     /*DsEdit*/
    table = SYS_FTM_SRAM_TBL_MET;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 15*1024, 16 * 1024);
    table = SYS_FTM_SRAM_TBL_EDIT;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 15*1024, 16 * 1024);

    /*DsFwd*/
    /*DsNexthop*/
    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 31 * 1024, 1 * 1024);
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM4, table, 31 * 1024, 1 * 1024);


    table = SYS_FTM_SRAM_TBL_FWD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);
    table = SYS_FTM_SRAM_TBL_NEXTHOP;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM3, table, 0, 32 * 1024);

    /*UserID Hash Key*/
    table = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM0, table, 0, 32* 1024);
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM7, table, 0, 16 * 1024);

    /*UserID Hash AD*/
    _sys_greatbelt_ftm_set_dsuserid(lchip, SYS_FTM_SRAM5, 0, 32*1024, 0);
    table = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 0, 16 * 1024);
     /*SYS_FTM_ADD_TABLE(SYS_FTM_SRAM6, table, 8*1024,  8 * 1024);*/

    /*Stats*/
    table = SYS_FTM_SRAM_TBL_STATS;
    SYS_FTM_ADD_TABLE(SYS_FTM_SRAM8, table, 0, 16 * 1024);


    /*--process dyn nh tbl--*/
    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH0) = (1 << SYS_FTM_SRAM_TBL_FWD )|
                                             (1 << SYS_FTM_SRAM_TBL_NEXTHOP);

    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH0)  = 1;

    SYS_FTM_DYN_NH_BITMAP(SYS_FTM_DYN_NH1) = (1 << SYS_FTM_SRAM_TBL_MET)|
                                              (1 << SYS_FTM_SRAM_TBL_EDIT);
    SYS_FTM_DYN_NH_VALID(SYS_FTM_DYN_NH1)  = 1;


    /********************************************
    tcam key
    *********************************************/
    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_FIB_COLISION;
    SYS_FTM_ADD_KEY(key, 1, 1048);

    /*userid mac*/
    key = SYS_FTM_TCAM_TYPE_MAC_USERID;
    SYS_FTM_ADD_KEY(key, 2, 256);
    /*userid vlan*/
    key = SYS_FTM_TCAM_TYPE_VLAN_USERID;
    SYS_FTM_ADD_KEY(key, 1, (1024*1));
    /*userid ipv4*/
     /*key = SYS_FTM_TCAM_TYPE_IPV4_USERID;*/
     /*SYS_FTM_ADD_KEY(key, 4, 64);*/
    /*userid ipv6*/
     /*key = SYS_FTM_TCAM_TYPE_IPV6_USERID;*/
     /*SYS_FTM_ADD_KEY(key, 4, 64);*/

    /*aclService 0*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*aclService 1*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*aclService 2*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
    SYS_FTM_ADD_KEY(key, 4, 64);
    /*aclService 3*/
    key = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
    SYS_FTM_ADD_KEY(key, 4, 64);

    /*lpmTcam 3*/
    key = SYS_FTM_TCAM_TYPE_LPM_USERID;
    SYS_FTM_ADD_KEY(key, 1, 256);

    return CTC_E_NONE;
}

#endif
int16 _sys_greatbelt_ftm_get_bit_set(uint8 lchip, uint32 bitmap)
{
    uint16 bit_offset = 0xFFFF;

    for(bit_offset = 0; bit_offset < 32; bit_offset++)
    {
        if(CTC_IS_BIT_SET(bitmap, bit_offset))
        {
            break;
        }

    }
    return bit_offset;
}

STATIC uint32
_sys_greatbelt_ftm_get_oam_session_num(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    uint32  mem_id = 0;
    uint32  session_num = 0;
    uint8 tbl_index         = 0;
    uint16 tbl_info_num     = 0;

    tbl_info_num = profile_info->tbl_info_size;
    for(tbl_index = 0; tbl_index < tbl_info_num; tbl_index++)
    {
        if(CTC_FTM_TBL_TYPE_OAM_MEP == profile_info->tbl_info[tbl_index].tbl_id)
        {
            for(mem_id = 0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
            {
                if(CTC_IS_BIT_SET(profile_info->tbl_info[tbl_index].mem_bitmap, mem_id))
                {
                    if (0 == profile_info->tbl_info[tbl_index].mem_entry_num[mem_id])
                    {
                        return profile_info->tbl_info[tbl_index].mem_start_offset[mem_id];/*line card no need mep but need chan, mem_start_offset means session number*/
                    }
                    session_num += profile_info->tbl_info[tbl_index].mem_entry_num[mem_id];
                }
            }

            break;
        }

    }
    /*OAM MEP : MA : MAName = 4:1:2*/
    return (session_num/7);
}

STATIC uint32
_sys_greatbelt_ftm_process_gb_oam_tbl(uint8 lchip, ctc_ftm_tbl_info_t *p_gb_oam_tbl_info)
{

    uint32 bitmap = 0;
    uint8 bit_offset = 0;
    uint32 total_size = 0;

    uint32 tbl_ids[] = {SYS_FTM_SRAM_TBL_OAM_MEP, SYS_FTM_SRAM_TBL_OAM_MA, SYS_FTM_SRAM_TBL_OAM_MA_NAME};
    uint32 tbl_size[] = {0, 0, 0};
    uint8  idx  = 0;

    bitmap = p_gb_oam_tbl_info->mem_bitmap;
    for (bit_offset = 0 ; bit_offset < 32; bit_offset++)
    {
        if(CTC_IS_BIT_SET(bitmap, bit_offset))
        {
            total_size += p_gb_oam_tbl_info->mem_entry_num[bit_offset];
        }
    }
    tbl_size[0] = total_size/7*4;   /*MEP*/
    tbl_size[1] = total_size/7*1;   /*MA*/
    tbl_size[2] = total_size/7*2;   /*MANAME*/

    bit_offset = 0;
    while (bit_offset < 32)
    {
        if(CTC_IS_BIT_SET(bitmap, bit_offset))
        {
            if (bit_offset >= SYS_FTM_SRAM_MAX)
            {
                return CTC_E_INVALID_PARAM;
            }
            if(tbl_size[idx] == p_gb_oam_tbl_info->mem_entry_num[bit_offset])
            {
                CTC_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(tbl_ids[idx]), bit_offset);
                SYS_FTM_TBL_MAX_ENTRY_NUM(tbl_ids[idx]) += p_gb_oam_tbl_info->mem_entry_num[bit_offset];
                SYS_FTM_TBL_MEM_START_OFFSET(tbl_ids[idx], bit_offset) = p_gb_oam_tbl_info->mem_start_offset[bit_offset];
                SYS_FTM_TBL_MEM_ENTRY_NUM(tbl_ids[idx], bit_offset)   = p_gb_oam_tbl_info->mem_entry_num[bit_offset];
                idx++;
                bit_offset ++;
            }
            else if(tbl_size[idx] > p_gb_oam_tbl_info->mem_entry_num[bit_offset])
            {
                CTC_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(tbl_ids[idx]), bit_offset);
                SYS_FTM_TBL_MAX_ENTRY_NUM(tbl_ids[idx]) += p_gb_oam_tbl_info->mem_entry_num[bit_offset];
                SYS_FTM_TBL_MEM_START_OFFSET(tbl_ids[idx], bit_offset) = p_gb_oam_tbl_info->mem_start_offset[bit_offset];
                SYS_FTM_TBL_MEM_ENTRY_NUM(tbl_ids[idx], bit_offset)   = p_gb_oam_tbl_info->mem_entry_num[bit_offset];
                tbl_size[idx] -= p_gb_oam_tbl_info->mem_entry_num[bit_offset];
                bit_offset ++;
            }
            else
            {
                CTC_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(tbl_ids[idx]), bit_offset);
                SYS_FTM_TBL_MAX_ENTRY_NUM(tbl_ids[idx]) += tbl_size[idx];
                SYS_FTM_TBL_MEM_START_OFFSET(tbl_ids[idx], bit_offset) = p_gb_oam_tbl_info->mem_start_offset[bit_offset];
                SYS_FTM_TBL_MEM_ENTRY_NUM(tbl_ids[idx], bit_offset)   = tbl_size[idx];
                p_gb_oam_tbl_info->mem_start_offset[bit_offset] += tbl_size[idx];
                p_gb_oam_tbl_info->mem_entry_num[bit_offset]    -= tbl_size[idx];
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
_sys_greatbelt_ftm_dy_nh_share(uint8 lchip)
{
    uint8 nh_tbl_num_next   = 0;
    uint8 table_next        = 0;
    uint32 tbl_bitmap_next  = 0;
    uint32 allocated_nh_bitmap = 0;
    uint8 nh_tbl_num        = 0;
    uint8 table             = 0;
    uint16 mem_id           = 0;
    uint32 tbl_bitmap       = 0;

    uint8 nh_tbl[SYS_FTM_DYN_NH_MAX] = {SYS_FTM_SRAM_TBL_FWD, SYS_FTM_SRAM_TBL_MET, SYS_FTM_SRAM_TBL_NEXTHOP, SYS_FTM_SRAM_TBL_EDIT};

    /*--process dyn nh tbl--*/
    for (nh_tbl_num = 0; nh_tbl_num < SYS_FTM_DYN_NH_MAX; nh_tbl_num++)
    {
        table       = nh_tbl[nh_tbl_num];
        tbl_bitmap  = SYS_FTM_TBL_MEM_BITMAP(table);

        if(tbl_bitmap)
        {
            if(CTC_IS_BIT_SET(allocated_nh_bitmap, table))
            {
                continue;
            }
            SYS_FTM_DYN_NH_VALID(nh_tbl_num)  = 1;
            SYS_FTM_DYN_NH_BITMAP(nh_tbl_num) |= (1 << table);
        }

        for (nh_tbl_num_next = (nh_tbl_num+1); nh_tbl_num_next < SYS_FTM_DYN_NH_MAX; nh_tbl_num_next++)
        {
            table_next       = nh_tbl[nh_tbl_num_next];
            tbl_bitmap_next  = SYS_FTM_TBL_MEM_BITMAP(table_next);

            if(tbl_bitmap&tbl_bitmap_next)
            {
                /*Get mem_id & check start offset*/
                mem_id = _sys_greatbelt_ftm_get_bit_set(lchip, tbl_bitmap&tbl_bitmap_next);
                if(SYS_FTM_TBL_MEM_START_OFFSET(table, mem_id)
                    == SYS_FTM_TBL_MEM_START_OFFSET(table_next, mem_id))
                {
                    SYS_FTM_DYN_NH_BITMAP(nh_tbl_num) |= (1 << table_next);
                    CTC_BIT_SET(allocated_nh_bitmap, table_next);
                }

            }
        }
    }

    return CTC_E_NONE;
}

STATIC uint32
_sys_greatbelt_ftm_get_id(uint8 lchip, bool b_key, uint32 id)
{
    uint32 sys_id = 0xFFFF;

    if (b_key)
    {
        switch(id)
        {
            case CTC_FTM_KEY_TYPE_IPV6_ACL0:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_ACL1:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1;
                break;

            case CTC_FTM_KEY_TYPE_ACL0:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0;
                break;

            case CTC_FTM_KEY_TYPE_ACL1:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1;
                break;

            case CTC_FTM_KEY_TYPE_ACL2:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2;
                break;

            case CTC_FTM_KEY_TYPE_ACL3:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_ACL0_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_ACL1_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_ACL0_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_ACL1_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_ACL2_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_ACL3_EGRESS:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_MCAST:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_MCAST;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_MCAST:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_MCAST;
                break;

            case CTC_FTM_KEY_TYPE_VLAN_SCL:
                sys_id = SYS_FTM_TCAM_TYPE_VLAN_USERID;
                break;

            case CTC_FTM_KEY_TYPE_MAC_SCL:
                sys_id = SYS_FTM_TCAM_TYPE_MAC_USERID;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_SCL:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_USERID;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_SCL:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_USERID;
                break;

            case CTC_FTM_KEY_TYPE_FDB:
                sys_id = SYS_FTM_TCAM_TYPE_FIB_COLISION;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_UCAST:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_UCAST;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_UCAST:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_UCAST;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_NAT:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_NAT;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_NAT:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_NAT;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_PBR:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_PBR;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_PBR:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_PBR;
                break;

            case CTC_FTM_KEY_TYPE_IPV4_TUNNEL:
                sys_id = SYS_FTM_TCAM_TYPE_IPV4_TUNNELID;
                break;

            case CTC_FTM_KEY_TYPE_IPV6_TUNNEL:
                sys_id = SYS_FTM_TCAM_TYPE_IPV6_TUNNELID;
                break;
            default:
                break;
        }
    }
    else
    {
        switch(id)
        {
            case CTC_FTM_TBL_TYPE_FIB_HASH_KEY:
                sys_id = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
                break;

            case CTC_FTM_TBL_TYPE_SCL_HASH_KEY:
                sys_id = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
                break;

            case CTC_FTM_TBL_TYPE_LPM_HASH_KEY:
                sys_id = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
                break;

            case CTC_FTM_TBL_TYPE_FIB_HASH_AD:
                sys_id = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
                break;

            case CTC_FTM_TBL_TYPE_SCL_HASH_AD:
                sys_id = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
                break;

            case CTC_FTM_TBL_TYPE_LPM_PIPE1:
                sys_id = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
                break;

            case CTC_FTM_TBL_TYPE_LPM_PIPE2:
                sys_id = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
                break;

            case CTC_FTM_TBL_TYPE_LPM_PIPE0:
                sys_id = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
                break;

            case CTC_FTM_TBL_TYPE_LPM_PIPE3:
                sys_id = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
                break;

            case CTC_FTM_TBL_TYPE_MPLS:
                sys_id = SYS_FTM_SRAM_TBL_MPLS;
                break;

            case CTC_FTM_TBL_TYPE_NEXTHOP:
                sys_id = SYS_FTM_SRAM_TBL_NEXTHOP;
                break;

            case CTC_FTM_TBL_TYPE_FWD:
                sys_id = SYS_FTM_SRAM_TBL_FWD;
                break;

            case CTC_FTM_TBL_TYPE_MET:
                sys_id = SYS_FTM_SRAM_TBL_MET;
                break;

            case CTC_FTM_TBL_TYPE_EDIT:
                sys_id = SYS_FTM_SRAM_TBL_EDIT;
                break;

            case CTC_FTM_TBL_TYPE_OAM_MEP:

                break;

            case CTC_FTM_TBL_TYPE_STATS:
                sys_id = SYS_FTM_SRAM_TBL_STATS;
                break;

            case CTC_FTM_TBL_TYPE_LM:
                sys_id = SYS_FTM_SRAM_TBL_LM;
                break;

            default:
                break;
        }
    }
    return sys_id;
}

STATIC int32
_sys_greatbelt_ftm_set_profile_flex(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    uint8 tbl_index         = 0;
    uint8 table_id          = 0;
    uint8 key_idx           = 0;
    uint32 key_id            = 0;
    uint16 tbl_info_num     = 0;
    uint16 key_info_num     = 0;
    uint16 mem_id           = 0;
    uint32 oam_session_num  = 0;

    /********************************************
    dynamic edram table
    *********************************************/

    oam_session_num = _sys_greatbelt_ftm_get_oam_session_num(lchip, profile_info);

    tbl_info_num = profile_info->tbl_info_size;
    for(tbl_index = 0; tbl_index < tbl_info_num; tbl_index++)
    {
        if (CTC_FTM_TBL_TYPE_OAM_MEP == profile_info->tbl_info[tbl_index].tbl_id)
        {
            _sys_greatbelt_ftm_process_gb_oam_tbl(lchip, &profile_info->tbl_info[tbl_index]);
        }
        else
        {
            table_id = _sys_greatbelt_ftm_get_id(lchip, FALSE, profile_info->tbl_info[tbl_index].tbl_id);

            SYS_FTM_TBL_MEM_BITMAP(table_id)   =  profile_info->tbl_info[tbl_index].mem_bitmap;

            for(mem_id = 0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
            {
                if(CTC_IS_BIT_SET(profile_info->tbl_info[tbl_index].mem_bitmap, mem_id))
                {
                    if((SYS_FTM_SRAM_TBL_USERID_HASH_AD == table_id)
                        && (0 == (profile_info->tbl_info[tbl_index].mem_bitmap >> (mem_id+1))))
                    {
                        /*Get OAM Session number*/
                        _sys_greatbelt_ftm_set_dsuserid(lchip, mem_id, profile_info->tbl_info[tbl_index].mem_start_offset[mem_id],
                                                        profile_info->tbl_info[tbl_index].mem_entry_num[mem_id],
                                                        oam_session_num);
                    }
                    else
                    {
                        SYS_FTM_TBL_MAX_ENTRY_NUM(table_id)            += profile_info->tbl_info[tbl_index].mem_entry_num[mem_id];
                        SYS_FTM_TBL_MEM_START_OFFSET(table_id, mem_id) = profile_info->tbl_info[tbl_index].mem_start_offset[mem_id];
                        SYS_FTM_TBL_MEM_ENTRY_NUM(table_id, mem_id)    = profile_info->tbl_info[tbl_index].mem_entry_num[mem_id];
                    }
                }
            }
        }
    }

    /*--process dyn nh tbl--*/
    _sys_greatbelt_ftm_dy_nh_share(lchip);

    /*tcam key*/
    key_info_num = profile_info->key_info_size;
    for(key_idx = 0; key_idx < key_info_num; key_idx++)
    {
        key_id = _sys_greatbelt_ftm_get_id(lchip, TRUE, profile_info->key_info[key_idx].key_id);
        if (key_id != 0xFFFF)
        {
            SYS_FTM_ADD_KEY(key_id, profile_info->key_info[key_idx].key_size, profile_info->key_info[key_idx].max_key_index);
        }

        if ((SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS > key_id)
            || (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS < key_id))
        {
            continue;
        }

        /* acl tcam key entry num = ingress + egress */
        if (SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
        else if (SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
        else if (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
        else if (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
        else if (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
        else if (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS == key_id)
        {
            SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3) += SYS_FTM_KEY_MAX_INDEX(key_id);
        }
         /*SYS_FTM_DBG_DUMP("key_id = %d, max_index = %d\n", key_id, SYS_FTM_KEY_MAX_INDEX(key_id));*/

    }

    SYS_FTM_ADD_KEY(SYS_FTM_TCAM_TYPE_LPM_USERID, 1, 256);
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ftm_get_tcam_info(uint8 lchip, uint32 tcam_key_type,
                                 uint32* p_tcam_key_id,
                                 uint32* p_tcam_ad_tbl,
                                 uint8* p_key_share_num,
                                 uint8* p_ad_share_num,
                                 uint8* p_ad_size,
                                 char* tcam_key_id_str)
{
    uint8 idx0 = 0;
    uint8 idx1 = 0;

    CTC_PTR_VALID_CHECK(p_tcam_key_id);
    CTC_PTR_VALID_CHECK(p_tcam_ad_tbl);
    CTC_PTR_VALID_CHECK(p_key_share_num);
    CTC_PTR_VALID_CHECK(p_ad_share_num);
    CTC_PTR_VALID_CHECK(tcam_key_id_str);

    switch (tcam_key_type)
    {
    case SYS_FTM_TCAM_TYPE_FIB_COLISION:
        /*mac bridge share table*/
        p_tcam_key_id[idx0++] = DsMacBridgeKey_t;
        p_tcam_key_id[idx0++] = DsMacLearningKey_t;
        p_tcam_ad_tbl[idx1++] = DsMacTcam_t;
        /*fcoe route share table*/
        p_tcam_key_id[idx0++] = DsFcoeRouteKey_t;
        p_tcam_key_id[idx0++] = DsFcoeRpfKey_t;
        p_tcam_ad_tbl[idx1++] = DsFcoeDaTcam_t;
        p_tcam_ad_tbl[idx1++] = DsFcoeSaTcam_t;
        /*Trill ucast table*/
        p_tcam_key_id[idx0++] = DsTrillUcastRouteKey_t;
        p_tcam_ad_tbl[idx1++] = DsTrillDaUcastTcam_t;

        /*Trill mcast table*/
        p_tcam_key_id[idx0++] = DsTrillMcastRouteKey_t;
        p_tcam_ad_tbl[idx1++] = DsTrillDaMcastTcam_t;
        sal_strcpy(tcam_key_id_str, "FibColisionKey");

        break;

    case SYS_FTM_TCAM_TYPE_MAC_IPV4:
        /*ipv4 mac mcast table*/
        break;

    case SYS_FTM_TCAM_TYPE_MAC_IPV6:
        /*ipv6 mac mcast table*/
        break;

    case SYS_FTM_TCAM_TYPE_IPV4_UCAST:
        /*ipv4 route share table*/
        p_tcam_key_id[idx0++] = DsIpv4UcastRouteKey_t;
        p_tcam_key_id[idx0++] = DsIpv4RpfKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4UcastDaTcam_t;
        if (g_ftm_master[lchip]->ip_tcam_share_mode)
        {
            p_tcam_key_id[idx0++] = DsIpv6UcastRouteKey_t;
            p_tcam_key_id[idx0++] = DsIpv6RpfKey_t;
            p_tcam_ad_tbl[idx1++] = DsIpv6UcastDaTcam_t;
        }
        sal_strcpy(tcam_key_id_str, "DsIpv4UcastRouteKey");
        break;

    case SYS_FTM_TCAM_TYPE_IPV6_UCAST:
        /*ipv6 route share table*/
        p_tcam_key_id[idx0++] = DsIpv6UcastRouteKey_t;
        p_tcam_key_id[idx0++] = DsIpv6RpfKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6UcastDaTcam_t;
        sal_strcpy(tcam_key_id_str, "DsIpv6UcastRouteKey");
        break;

    case SYS_FTM_TCAM_TYPE_MAC_USERID:
        /*UserID MAC not share table*/
        p_tcam_key_id[idx0++] = DsUserIdMacKey_t;
        p_tcam_ad_tbl[idx1++] = DsUserIdMacTcam_t;
        sal_strcpy(tcam_key_id_str, "DsUserIdMacKey");
        break;

    case SYS_FTM_TCAM_TYPE_IPV6_USERID:
        /*UserID IPV6 not share table*/
        p_tcam_key_id[idx0++] = DsUserIdIpv6Key_t;
        p_tcam_ad_tbl[idx1++] = DsUserIdIpv6Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsUserIdIpv6Key");
        break;

    case SYS_FTM_TCAM_TYPE_IPV4_USERID:
        /*UserID IPV4 not share table*/
        p_tcam_key_id[idx0++] = DsUserIdIpv4Key_t;
        p_tcam_ad_tbl[idx1++] = DsUserIdIpv4Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsUserIdIpv4Key");
        break;

    case SYS_FTM_TCAM_TYPE_VLAN_USERID:
        /*UserID VLAN not share table*/
        p_tcam_key_id[idx0++] = DsUserIdVlanKey_t;
        p_tcam_ad_tbl[idx1++] = DsUserIdVlanTcam_t;
        sal_strcpy(tcam_key_id_str, "DsUserIdVlanKey");
        break;

    /*ipv4 and macIpv4 mcast share table*/
    case SYS_FTM_TCAM_TYPE_IPV4_MCAST:
        p_tcam_key_id[idx0++] = DsIpv4McastRouteKey_t;
        p_tcam_key_id[idx0++] = DsMacIpv4Key_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4McastDaTcam_t;
        p_tcam_ad_tbl[idx1++] = DsMacIpv4Tcam_t;
        if (g_ftm_master[lchip]->ip_tcam_share_mode)
        {
            p_tcam_key_id[idx0++] = DsIpv6McastRouteKey_t;
            p_tcam_key_id[idx0++] = DsMacIpv6Key_t;
            p_tcam_ad_tbl[idx1++] = DsIpv6McastDaTcam_t;
            p_tcam_ad_tbl[idx1++] = DsMacIpv6Tcam_t;
        }
        sal_strcpy(tcam_key_id_str, "DsIpv4McastRouteKey");
        break;

    /*ipv6 and macIpv6 mcast share table*/
    case SYS_FTM_TCAM_TYPE_IPV6_MCAST:
        p_tcam_key_id[idx0++] = DsIpv6McastRouteKey_t;
        p_tcam_key_id[idx0++] = DsMacIpv6Key_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6McastDaTcam_t;
        p_tcam_ad_tbl[idx1++] = DsMacIpv6Tcam_t;

        sal_strcpy(tcam_key_id_str, "DsIpv6McastRouteKey");
        break;

    /*ipv4 nat table*/
    case SYS_FTM_TCAM_TYPE_IPV4_NAT:
        p_tcam_key_id[idx0++] = DsIpv4NatKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4SaNatTcam_t;
        sal_strcpy(tcam_key_id_str, "DsIpv4NatKey");
        break;

    /*ipv6 nat table*/
    case SYS_FTM_TCAM_TYPE_IPV6_NAT:
        p_tcam_key_id[idx0++] = DsIpv6NatKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6SaNatTcam_t;
        sal_strcpy(tcam_key_id_str, "DsIpv6NatKey");
        break;

    /*ipv4 pbr table*/
    case SYS_FTM_TCAM_TYPE_IPV4_PBR:
        p_tcam_key_id[idx0++] = DsIpv4PbrKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4UcastPbrDualDaTcam_t;
        sal_strcpy(tcam_key_id_str, "DsIpv4PbrKey");
        break;

    /*ipv6 pbr table*/
    case SYS_FTM_TCAM_TYPE_IPV6_PBR:
        p_tcam_key_id[idx0++] = DsIpv6PbrKey_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6UcastPbrDualDaTcam_t;
        sal_strcpy(tcam_key_id_str, "DsIpv6PbrKey");
        break;

    /*ipv4 tunnel table*/
    case SYS_FTM_TCAM_TYPE_IPV4_TUNNELID:
        p_tcam_key_id[idx0++] = DsTunnelIdIpv4Key_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdIpv4Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsTunnelIdIpv4Key");
        break;

    /*ipv6 tunnel table*/
    case SYS_FTM_TCAM_TYPE_IPV6_TUNNELID:
        p_tcam_key_id[idx0++] = DsTunnelIdIpv6Key_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdIpv6Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsTunnelIdIpv6Key");
        break;

    /*pbb tunnel table*/
    case SYS_FTM_TCAM_TYPE_PBB_TUNNELID:
        p_tcam_key_id[idx0++] = DsTunnelIdPbbKey_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdPbbTcam_t;
        sal_strcpy(tcam_key_id_str, "DsTunnelIdPbbKey");
        break;

    /*wlan tunnel table*/
    case SYS_FTM_TCAM_TYPE_WLAN_TUNNELID:
        p_tcam_key_id[idx0++] = DsTunnelIdCapwapKey_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdCapwapTcam_t;
        sal_strcpy(tcam_key_id_str, "DsTunnelIdWlanKey");
        break;

    /*trill tunnel table*/
    case SYS_FTM_TCAM_TYPE_TRILL_TUNNELID:
        p_tcam_key_id[idx0++] = DsTunnelIdTrillKey_t;
        p_tcam_ad_tbl[idx1++] = DsTunnelIdTrillTcam_t;
        sal_strcpy(tcam_key_id_str, "DsTunnelIdTrillKey");
        break;

    /*mac ipv4  share aclQos talbe*/
    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0:
        p_tcam_key_id[idx0++] = DsAclMacKey0_t;
        p_tcam_key_id[idx0++] = DsAclIpv4Key0_t;
        p_tcam_key_id[idx0++] = DsAclMplsKey0_t;
        p_tcam_ad_tbl[idx1++] = DsMacAcl0Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4Acl0Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsAclMacIPv4Key0");
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1:
        p_tcam_key_id[idx0++] = DsAclMacKey1_t;
        p_tcam_key_id[idx0++] = DsAclIpv4Key1_t;
        p_tcam_key_id[idx0++] = DsAclMplsKey1_t;
        p_tcam_ad_tbl[idx1++] = DsMacAcl1Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4Acl1Tcam_t;

        sal_strcpy(tcam_key_id_str, "DsAclMacIPv4Key1");
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2:
        p_tcam_key_id[idx0++] = DsAclMacKey2_t;
        p_tcam_key_id[idx0++] = DsAclIpv4Key2_t;
        p_tcam_key_id[idx0++] = DsAclMplsKey2_t;
        p_tcam_ad_tbl[idx1++] = DsMacAcl2Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4Acl2Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsAclMacIPv4Key2");
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3:
        p_tcam_key_id[idx0++] = DsAclMacKey3_t;
        p_tcam_key_id[idx0++] = DsAclIpv4Key3_t;
        p_tcam_key_id[idx0++] = DsAclMplsKey3_t;
        p_tcam_ad_tbl[idx1++] = DsMacAcl3Tcam_t;
        p_tcam_ad_tbl[idx1++] = DsIpv4Acl3Tcam_t;

        sal_strcpy(tcam_key_id_str, "DsAclMacIPv4Key3");
        break;

    /*ipv4 aclQos talbe*/
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0:
        break;

    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS1:
        break;

    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS2:
        break;

    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3:
        break;

    /*ipv6 aclQos talbe*/
    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0:
        p_tcam_key_id[idx0++] = DsAclIpv6Key0_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6Acl0Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsAclIpv6Key0");
        break;

    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1:
        p_tcam_key_id[idx0++] = DsAclIpv6Key1_t;
        p_tcam_ad_tbl[idx1++] = DsIpv6Acl1Tcam_t;
        sal_strcpy(tcam_key_id_str, "DsAclIpv6Key1");
        break;

    /*lpmUserid talbe*/
    case SYS_FTM_TCAM_TYPE_LPM_USERID:
        p_tcam_key_id[idx0++] = DsLpmTcam80Key_t;
        p_tcam_key_id[idx0++] = DsFibUserId80Key_t;
        p_tcam_key_id[idx0++] = DsLpmTcam160Key_t;
        p_tcam_key_id[idx0++] = DsFibUserId160Key_t;
        p_tcam_ad_tbl[idx1++] = LpmTcamAdMem_t;
        sal_strcpy(tcam_key_id_str, "DsLpmTcamUserIdKey");
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    *p_key_share_num = idx0;
    *p_ad_share_num  = idx1;
    *p_ad_size = TABLE_ENTRY_SIZE(p_tcam_ad_tbl[0]) / DRV_BYTES_PER_ENTRY;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_get_sram_tbl_id(uint8 lchip, uint32 sram_type,
                                   uint32* p_sram_tbl_id,
                                   uint8* p_share_num,
                                   char* sram_tbl_id_str)
{
    uint8 idx = 0;

    CTC_PTR_VALID_CHECK(p_sram_tbl_id);
    CTC_PTR_VALID_CHECK(p_share_num);
    CTC_PTR_VALID_CHECK(sram_tbl_id_str);

    switch (sram_type)
    {
    /*LPM Hash Key share table*/
    case SYS_FTM_SRAM_TBL_LPM_HASH_KEY:
        p_sram_tbl_id[idx++] = DsLpmIpv4Hash16Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4Hash8Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4McastHash32Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4McastHash64Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6Hash32HighKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6Hash32LowKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6Hash32MidKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6McastHash0Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6McastHash1Key_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4NatDaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4NatSaHashKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv4NatSaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6NatDaPortHashKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6NatSaHashKey_t;
        p_sram_tbl_id[idx++] = DsLpmIpv6NatSaPortHashKey_t;
        sal_strcpy(sram_tbl_id_str, "LPM_HASH_KEY");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*LPM Hash Key0 share table*/
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY0:
        p_sram_tbl_id[idx++] = DsLpmLookupKey0_t;
        sal_strcpy(sram_tbl_id_str, "LPM_LKP_KEY0");
        break;

    /*LPM Hash Key1 share table*/
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY1:
        p_sram_tbl_id[idx++] = DsLpmLookupKey1_t;
        sal_strcpy(sram_tbl_id_str, "LPM_LKP_KEY1");
        break;

    /*LPM Hash Key0 share table*/
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY2:
        p_sram_tbl_id[idx++] = DsLpmLookupKey2_t;
        sal_strcpy(sram_tbl_id_str, "LPM_LKP_KEY2");
        break;

    /*LPM Hash Key1 share table*/
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY3:
        p_sram_tbl_id[idx++] = DsLpmLookupKey3_t;
        sal_strcpy(sram_tbl_id_str, "LPM_LKP_KEY3");
        break;

    /*FIB Hash Key share table*/
    case SYS_FTM_SRAM_TBL_FIB_HASH_KEY:
        p_sram_tbl_id[idx++] = DsMacHashKey_t;
        p_sram_tbl_id[idx++] = DsFcoeHashKey_t;
        p_sram_tbl_id[idx++] = DsFcoeRpfHashKey_t;
        p_sram_tbl_id[idx++] = DsTrillMcastHashKey_t;
        p_sram_tbl_id[idx++] = DsTrillMcastVlanHashKey_t;
        p_sram_tbl_id[idx++] = DsTrillUcastHashKey_t;
        p_sram_tbl_id[idx++] = DsAclQosIpv4HashKey_t;
        p_sram_tbl_id[idx++] = DsAclQosMacHashKey_t;
        sal_strcpy(sram_tbl_id_str, "FIB_HASH_KEY");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*FIB Hash AD share table*/
    case SYS_FTM_SRAM_TBL_FIB_HASH_AD:
        p_sram_tbl_id[idx++] = DsMac_t;
        p_sram_tbl_id[idx++] = DsIpDa_t;
        p_sram_tbl_id[idx++] = DsIpSaNat_t;
        p_sram_tbl_id[idx++] = DsFcoeDa_t;
        p_sram_tbl_id[idx++] = DsFcoeSa_t;
        p_sram_tbl_id[idx++] = DsTrillDa_t;
        p_sram_tbl_id[idx++] = DsAcl_t;
        sal_strcpy(sram_tbl_id_str, "FIB_HASH_AD");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*UserID Hash Key share table*/
    case SYS_FTM_SRAM_TBL_USERID_HASH_KEY:
        p_sram_tbl_id[idx++] = DsUserIdDoubleVlanHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdCvlanHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdSvlanCosHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdCvlanCosHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdMacHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv4HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv6HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdL2HashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdIpv4PortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdMacPortHashKey_t;
        p_sram_tbl_id[idx++] = DsUserIdPortHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdIpv4HashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdPbbHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdCapwapHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdTrillUcRpfHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdTrillMcRpfHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdTrillMcAdjCheckHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdTrillUcDecapHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdTrillMcDecapHashKey_t;
        p_sram_tbl_id[idx++] = DsTunnelIdIpv4RpfHashKey_t;
        p_sram_tbl_id[idx++] = DsPbtOamHashKey_t;
        p_sram_tbl_id[idx++] = DsMplsOamLabelHashKey_t;
        p_sram_tbl_id[idx++] = DsBfdOamHashKey_t;
        p_sram_tbl_id[idx++] = DsEthOamRmepHashKey_t;
        p_sram_tbl_id[idx++] = DsEthOamHashKey_t;
        sal_strcpy(sram_tbl_id_str, "USERID_HASH_KEY");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*UserID Hash AD share table*/
    case SYS_FTM_SRAM_TBL_USERID_HASH_AD:
        p_sram_tbl_id[idx++] = DsUserId_t;
        p_sram_tbl_id[idx++] = DsTunnelId_t;
        p_sram_tbl_id[idx++] = DsVlanXlate_t;
        sal_strcpy(sram_tbl_id_str, "USERID_HASH_AD");
        break;

    /*UserID ingress default not share table*/
    case SYS_FTM_SRAM_TBL_USERID_IGS_DFT:
        p_sram_tbl_id[idx++] = DsUserIdIngressDefault_t;
        sal_strcpy(sram_tbl_id_str, "USERID_IGS_DFT");
        break;

    /*UserID egress default not share table*/
    case SYS_FTM_SRAM_TBL_USERID_EGS_DFT:
        p_sram_tbl_id[idx++] = DsUserIdEgressDefault_t;
        sal_strcpy(sram_tbl_id_str, "USERID_EGS_DFT");
        break;

    /*UserID tunnel default not share table*/
    case SYS_FTM_SRAM_TBL_USERID_TUNL_DFT:
        p_sram_tbl_id[idx++] = DsTunnelIdDefault_t;
        sal_strcpy(sram_tbl_id_str, "USERID_TUNNEL_DFT");
        break;

    /*UserID oam chan not share table*/
    case SYS_FTM_SRAM_TBL_OAM_CHAN:
        p_sram_tbl_id[idx++] = DsEthOamChan_t;
        p_sram_tbl_id[idx++] = DsMplsPbtBfdOamChan_t;
        sal_strcpy(sram_tbl_id_str, "USERID_OAM_CHAN");
        break;

    /*DsNexthop and DsEdit share table*/
    case SYS_FTM_SRAM_TBL_NEXTHOP:
        p_sram_tbl_id[idx++] = DsNextHop4W_t;
        p_sram_tbl_id[idx++] = DsNextHop8W_t;
        sal_strcpy(sram_tbl_id_str, "DS_NEXTHOP");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*DsEdit share table*/
    case SYS_FTM_SRAM_TBL_EDIT:
        p_sram_tbl_id[idx++] = DsL2EditEth4W_t;
        p_sram_tbl_id[idx++] = DsL2EditEth8W_t;
        p_sram_tbl_id[idx++] = DsL2EditFlex8W_t;
        p_sram_tbl_id[idx++] = DsL2EditLoopback_t;
        p_sram_tbl_id[idx++] = DsL2EditPbb4W_t;
        p_sram_tbl_id[idx++] = DsL2EditPbb8W_t;
        p_sram_tbl_id[idx++] = DsL3EditMpls4W_t;
        p_sram_tbl_id[idx++] = DsL3EditMpls8W_t;
        p_sram_tbl_id[idx++] = DsL3EditFlex_t;
        p_sram_tbl_id[idx++] = DsL3EditNat4W_t;
        p_sram_tbl_id[idx++] = DsL3EditNat8W_t;
        p_sram_tbl_id[idx++] = DsL3EditTunnelV4_t;
        p_sram_tbl_id[idx++] = DsL3EditTunnelV6_t;
        p_sram_tbl_id[idx++] = DsL2EditSwap_t;
        sal_strcpy(sram_tbl_id_str, "DS_EDIT");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*DsFwd */
    case SYS_FTM_SRAM_TBL_FWD:
        p_sram_tbl_id[idx++] = DsFwd_t;
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        sal_strcpy(sram_tbl_id_str, "DS_FWD");
        break;

    /*DsMet share table*/
    case SYS_FTM_SRAM_TBL_MET:
        p_sram_tbl_id[idx++] = DsMetEntry_t;
        sal_strcpy(sram_tbl_id_str, "DS_MET");
        SYS_FTM_TBL_ACCESS_FLAG(sram_type) = 1;
        break;

    /*DsMpls not share table*/
    case SYS_FTM_SRAM_TBL_MPLS:
        p_sram_tbl_id[idx++] = DsMpls_t;
        sal_strcpy(sram_tbl_id_str, "DS_MPLS");
        break;

    /*Mep share table*/
    case SYS_FTM_SRAM_TBL_OAM_MEP:
        p_sram_tbl_id[idx++] = DsEthMep_t;
        p_sram_tbl_id[idx++] = DsEthRmep_t;
        p_sram_tbl_id[idx++] = DsBfdMep_t;
        p_sram_tbl_id[idx++] = DsBfdRmep_t;
        sal_strcpy(sram_tbl_id_str, "OAM_MEP");
        break;

    /*Ma not share table*/
    case SYS_FTM_SRAM_TBL_OAM_MA:
        p_sram_tbl_id[idx++] = DsMa_t;
        sal_strcpy(sram_tbl_id_str, "OAM_MA");
        break;

    /*MaName not share table*/
    case SYS_FTM_SRAM_TBL_OAM_MA_NAME:
        p_sram_tbl_id[idx++] = DsMaName_t;
        sal_strcpy(sram_tbl_id_str, "OAM_MA_NAME");
        break;

    /*Stats not share table*/
    case SYS_FTM_SRAM_TBL_STATS:
        p_sram_tbl_id[idx++] = DsStats_t;
        sal_strcpy(sram_tbl_id_str, "STATS");
        break;

    /*Lm  share table*/
    case SYS_FTM_SRAM_TBL_LM:
        p_sram_tbl_id[idx++] = DsOamLmStats_t;
        sal_strcpy(sram_tbl_id_str, "LM");
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    *p_share_num = idx;

    return CTC_E_NONE;
}

tbls_ext_info_t*
_sys_greatbelt_ftm_build_dynamic_tbl_info(uint8 lchip)
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
_sys_greatbelt_ftm_alloc_dyn_share_tbl(uint8 lchip, uint8 sram_type,
                                       uint32 tbl_id)
{
    uint32 mem_entry_num = 0;
    uint32 max_index_num = 0;
    uint32 start_index = 0;
    uint32 end_index = 0;

    uint32 start_base = 0;
    uint32 end_base = 0;
    uint32 entry_num = 0;

    uint32 tbl_offset = 0;
    uint32 mp_offset = 0;
    uint32 stats_offset = 0;
    uint8 mem_id = 0;
    uint32 mem_alloc = 0;
    uint32 mp_mem_alloc = 0;
    uint8 base_idx = 0;
    dynamic_mem_ext_content_t* p_dyn_mem_ext_info = NULL;
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    bool is_userid  = FALSE;
    bool is_mep     = FALSE;

    p_tbl_ext_info = _sys_greatbelt_ftm_build_dynamic_tbl_info(lchip);
    if (NULL == p_tbl_ext_info)
    {
        return CTC_E_NO_MEMORY;
    }

    p_dyn_mem_ext_info = p_tbl_ext_info->ptr_ext_content;
    if (NULL == p_tbl_ext_info)
    {
        return CTC_E_NO_MEMORY;
    }

    TABLE_EXT_INFO_PTR(tbl_id) = p_tbl_ext_info;
    TABLE_EXT_INFO_TYPE(tbl_id) = EXT_INFO_TYPE_DYNAMIC;
    DYNAMIC_BITMAP(tbl_id) = SYS_FTM_TBL_MEM_BITMAP(sram_type);

    p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_DEFAULT;

    if (SYS_FTM_TBL_ACCESS_FLAG(sram_type))
    {
        if (TABLE_ENTRY_SIZE(tbl_id) == (2 * DRV_BYTES_PER_ENTRY))
        {
            p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_8W_MODE;
        }
        else if (TABLE_ENTRY_SIZE(tbl_id) == DRV_BYTES_PER_ENTRY)
        {
            p_dyn_mem_ext_info->dynamic_access_mode = DYNAMIC_4W_MODE;
        }
    }

    is_mep = is_mep;
    is_userid   = (SYS_FTM_SRAM_TBL_USERID_HASH_KEY == sram_type);
    is_mep      = (SYS_FTM_SRAM_TBL_OAM_MEP == sram_type);

    mp_offset = mp_offset;
    stats_offset = stats_offset;
    mp_mem_alloc = mp_mem_alloc;
    mp_offset       = 0;
    mp_mem_alloc    = 0;
    stats_offset    = 0;
    for (mem_id = SYS_FTM_SRAM0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
    {
        mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, mem_id);

        if (!IS_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(sram_type), mem_id) || 0 == mem_entry_num)
        {
            mem_alloc += SYS_FTM_MEM_MAX_ENTRY_NUM(mem_id);
            continue;
        }

        tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, mem_id) + mem_alloc;
        mem_alloc += SYS_FTM_MEM_MAX_ENTRY_NUM(mem_id);
#if (0 == SDK_WORK_PLATFORM)
        /*
CPU indirect access to mep tables */
        if (is_mep)
        {
            mp_offset      =  mp_mem_alloc;
            DYNAMIC_DATA_BASE(tbl_id, mem_id) = DS_MP_OFFSET + mp_offset * DRV_ADDR_BYTES_PER_ENTRY;
            mp_mem_alloc    += mem_entry_num;
        }
        else
#endif
        {
            if (TABLE_ENTRY_SIZE(tbl_id) == (DRV_BYTES_PER_ENTRY))
            {
                DYNAMIC_DATA_BASE(tbl_id, mem_id) = DRV_MEMORY0_BASE_4W + tbl_offset * DRV_ADDR_BYTES_PER_ENTRY;
            }
            else
            {
                DYNAMIC_DATA_BASE(tbl_id, mem_id) = DRV_MEMORY0_BASE_8W + tbl_offset * DRV_ADDR_BYTES_PER_ENTRY;
            }

#if (0 == SDK_WORK_PLATFORM)
            if (DsStats_t == tbl_id)
            {
                DYNAMIC_DATA_BASE(tbl_id, mem_id) = SYS_FTM_DS_STATS_HW_INDIRECT_ADDR + stats_offset * DRV_ADDR_BYTES_PER_ENTRY;
                stats_offset += mem_entry_num;
            }
#endif

        }

        DYNAMIC_ENTRY_NUM(tbl_id, mem_id) = mem_entry_num;

        if (is_userid && SYS_FTM_SRAM0 == mem_id)
        {
            entry_num = DRV_MEMORY7_MAX_ENTRY_NUM;
            max_index_num = entry_num;
        }

        start_index = max_index_num; /*per key index*/
        start_base = entry_num; /*per 80bit base*/
        entry_num += mem_entry_num;

        if (SYS_FTM_TBL_ACCESS_FLAG(sram_type))
        {
            max_index_num += mem_entry_num;
        }
        else
        {
            max_index_num += mem_entry_num * DRV_BYTES_PER_ENTRY / TABLE_ENTRY_SIZE(tbl_id);
        }

        end_index = (max_index_num - 1);
        end_base = entry_num - 1;

        if (is_userid && SYS_FTM_SRAM0 == mem_id)
        {
            entry_num = 0;
            max_index_num = 0;
            start_base  -= DRV_MEMORY7_MAX_ENTRY_NUM;
            end_base    -= DRV_MEMORY7_MAX_ENTRY_NUM;
        }

        DYNAMIC_START_INDEX(tbl_id, mem_id) = start_index;
        DYNAMIC_END_INDEX(tbl_id, mem_id) = end_index;


        SYS_FTM_TBL_BASE_VAL(sram_type, base_idx) = tbl_offset;
        SYS_FTM_TBL_BASE_MIN(sram_type, base_idx) = start_base;
        SYS_FTM_TBL_BASE_MAX(sram_type, base_idx) = end_base;
        base_idx++;

        if (is_userid && SYS_FTM_SRAM7 == mem_id)
        {
            base_idx--;
            max_index_num += DYNAMIC_ENTRY_NUM(tbl_id, SYS_FTM_SRAM0);
        }
    }

    TABLE_MAX_INDEX(tbl_id) = max_index_num;
    SYS_FTM_TBL_BASE_NUM(sram_type) = base_idx;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ftm_alloc_sram(uint8 lchip)
{
    uint8 index = 0;
    uint32 sram_type = 0;
    uint32 sram_tbl_id = MaxTblId_t;
    uint32 sram_tbl_a[50] = {0};
    char sram_tbl_id_str[40];
    uint8 share_tbl_num  = 0;
    uint32 max_entry_num = 0;

    for (sram_type = 0; sram_type < SYS_FTM_SRAM_TBL_MAX; sram_type++)
    {
        max_entry_num = SYS_FTM_TBL_MAX_ENTRY_NUM(sram_type);

        if (0 == max_entry_num)
        {
            continue;
        }

        _sys_greatbelt_ftm_get_sram_tbl_id(lchip, sram_type,
                                           sram_tbl_a,
                                           &share_tbl_num,
                                           sram_tbl_id_str);

        for (index = 0; index < share_tbl_num; index++)
        {
            sram_tbl_id = sram_tbl_a[index];

            if (sram_tbl_id >= MaxTblId_t)
            {
                SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", sram_tbl_id);
                return CTC_E_UNEXPECT;
            }

            CTC_ERROR_RETURN(_sys_greatbelt_ftm_alloc_dyn_share_tbl(lchip, sram_type, sram_tbl_id));
        }

        index = 0;

    }

    return CTC_E_NONE;
}

tbls_ext_info_t*
_sys_greatbelt_ftm_build_tcam_tbl_info(uint8 lchip)
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

    p_tbl_ext_info->ptr_ext_content = p_tcam_mem_ext_info;

    return p_tbl_ext_info;

}

STATIC int32
_sys_greatbelt_ftm_tcam_key_get_offset(uint8 lchip, uint8 tcam_key_type,
                                       uint32* p_offset)
{
    sys_greatbelt_opf_t opf;
    uint16 entry_num = 0;
    uint16 acl0_num  = 0;
    uint16 acl_tcam0_size = 0;
    bool is_alloc_once = FALSE;
    bool is_alloc_two = FALSE;
    uint32 begin = 0;
    uint8 mode = g_ftm_master[lchip]->acl_mac_mode;
    uint8 acl_ipv6_mode = g_ftm_master[lchip]->acl_ipv6_mode;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = SYS_FTM_OPF_TYPE_TCAM_KEY0;

    if ((g_ftm_master[lchip]->ip_tcam_share_mode))
    {
        if (SYS_FTM_TCAM_TYPE_IPV4_UCAST == tcam_key_type)
        {
            entry_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_UCAST)
                        + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_UCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_UCAST)/SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_UCAST));
            entry_num *= SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_UCAST);
        }
        else if (SYS_FTM_TCAM_TYPE_IPV4_MCAST == tcam_key_type)
        {
            entry_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST)
                        + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_MCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_MCAST)/SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_MCAST));
            entry_num *= SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_MCAST);
        }
        else
        {
            entry_num = SYS_FTM_KEY_MAX_INDEX(tcam_key_type) * SYS_FTM_KEY_KEY_SIZE(tcam_key_type);
        }
    }
    else
    {
        entry_num = SYS_FTM_KEY_MAX_INDEX(tcam_key_type) * SYS_FTM_KEY_KEY_SIZE(tcam_key_type);
    }

    g_ftm_master[lchip]->int_tcam_used_entry += entry_num;
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\ng_ftm_master[lchip]->tcam_head_offset:%d\n", g_ftm_master[lchip]->tcam_head_offset);

    switch (tcam_key_type)
    {

    case SYS_FTM_TCAM_TYPE_IPV6_PBR:
    case SYS_FTM_TCAM_TYPE_IPV6_USERID:
    case SYS_FTM_TCAM_TYPE_IPV6_TUNNELID:
        if(8 == SYS_FTM_KEY_KEY_SIZE(tcam_key_type))
        {
            begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num/2;
            g_ftm_master[lchip]->tcam_tail_offset = begin;
            is_alloc_two = TRUE;
        }

        break;

    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0:
        is_alloc_two = TRUE;
        if ((mode == SYS_FTM_ACL_MODE_4LKP)
            ||((mode == SYS_FTM_ACL_MODE_2LKP)&&(acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP)))
        {
            begin = g_ftm_master[lchip]->tcam_head_offset;
            g_ftm_master[lchip]->tcam_head_offset += entry_num / 2;
        }
        else if (mode == SYS_FTM_ACL_MODE_1LKP)
        {
            begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num / 2;
            g_ftm_master[lchip]->tcam_tail_offset = begin;
        }
        else if((mode == SYS_FTM_ACL_MODE_2LKP) && (acl_ipv6_mode == SYS_FTM_ACL_MODE_1LKP))
        {
            acl_tcam0_size  = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0)
                                        + (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0)/2);

            if (acl_tcam0_size <= 2*1024)
            {
                begin = g_ftm_master[lchip]->tcam_head_offset;
                g_ftm_master[lchip]->tcam_head_offset += entry_num / 2;
            }
            else
            {
                begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num / 2;
                g_ftm_master[lchip]->tcam_tail_offset = begin;
            }

        }

        break;

    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1:
        is_alloc_two = TRUE;
        if (((mode == SYS_FTM_ACL_MODE_4LKP) || (mode == SYS_FTM_ACL_MODE_2LKP))
            && (acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP))
        {
            begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num / 2;
            g_ftm_master[lchip]->tcam_tail_offset = begin;
        }
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0:
        is_alloc_once = TRUE;
        begin = g_ftm_master[lchip]->tcam_head_offset;
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS1:
        is_alloc_once = TRUE;
        if (mode == SYS_FTM_ACL_MODE_4LKP)
        {
            begin = g_ftm_master[lchip]->tcam_head_offset + 2*1024;
        }
        else if (mode == SYS_FTM_ACL_MODE_2LKP)
        {
            if(acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP)
            {
                begin = g_ftm_master[lchip]->tcam_head_offset + 2*1024;
            }
            else
            {
                acl0_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0);

                acl_tcam0_size  = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0)
                                        + (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0)/2);

                if (acl_tcam0_size <= 2*1024)
                {
                        begin = g_ftm_master[lchip]->tcam_head_offset + 2*1024;
                        g_ftm_master[lchip]->acl1_mode = 2;
                }
                else
                {
                    if(acl0_num > 4*1024)
                    {
                        g_ftm_master[lchip]->acl1_mode = 1;
                        begin = 6*1024;
                    }
                    else
                    {
                        g_ftm_master[lchip]->acl1_mode = 0;
                        begin = 4*1024;
                    }
                }


            }
        }
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS2:
         /*begin = 4*1024;*/
        begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num;
        is_alloc_once = TRUE;
        opf.reverse = 1;
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3:
         /*begin = 6*1024;*/
        begin = g_ftm_master[lchip]->tcam_tail_offset - entry_num + 2*1024;
        is_alloc_once = TRUE;
        opf.reverse = 1;
        break;

    case SYS_FTM_TCAM_TYPE_LPM_USERID:
        opf.pool_index = SYS_FTM_OPF_TYPE_LPM_KEY;
        g_ftm_master[lchip]->int_tcam_used_entry -= entry_num;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset));
        return CTC_E_NONE;

    default:
        break;

    }


    if (is_alloc_once)
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ONCE: tcam_key_type:%d, begin:%d, entry_num:%d\n", tcam_key_type, begin, entry_num);
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset_from_position(lchip, &opf, entry_num, begin));
        *p_offset = begin;
    }
    else if (is_alloc_two)
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TWO: tcam_key_type:%d, begin:%d, entry_num:%d\n", tcam_key_type, begin, entry_num);
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset_from_position(lchip, &opf, entry_num/2, begin));
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset_from_position(lchip, &opf, entry_num/2, begin + 2*1024));
        *p_offset = begin;
    }
    else
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Share: tcam_key_type:%d, entry num:%d\n", tcam_key_type, entry_num);
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_tcam_ad_get_offset(uint8 lchip, uint8 tcam_key_type,
                                      uint32 entry_num,
                                      uint32* p_offset)
{
    sys_greatbelt_opf_t opf;
    uint32 begin = 0;
    uint8 mode = g_ftm_master[lchip]->acl_mac_mode;
    uint8 acl_ipv6_mode = g_ftm_master[lchip]->acl_ipv6_mode;
    uint16 acl0_num = 0;
    uint16 acl_tcam0_size = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "g_ftm_master[lchip]->tcam_ad_head_offset:%d\n", g_ftm_master[lchip]->tcam_ad_head_offset);
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "g_ftm_master[lchip]->tcam_ad_tail_offset:%d\n", g_ftm_master[lchip]->tcam_ad_tail_offset);

    opf.pool_index = SYS_FTM_OPF_TYPE_TCAM_AD0;
    opf.pool_type = OPF_FTM;

    switch (tcam_key_type)
    {
    case SYS_FTM_TCAM_TYPE_IPV6_PBR:
    case SYS_FTM_TCAM_TYPE_IPV6_USERID:
    case SYS_FTM_TCAM_TYPE_IPV6_TUNNELID:

        if (8 == SYS_FTM_KEY_KEY_SIZE(tcam_key_type))
        {
            begin = g_ftm_master[lchip]->tcam_ad_tail_offset - entry_num;
            g_ftm_master[lchip]->tcam_ad_tail_offset = begin;
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset));
            return CTC_E_NONE;
        }

        break;

    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0:
        if((mode == SYS_FTM_ACL_MODE_4LKP)
                    ||((mode == SYS_FTM_ACL_MODE_2LKP)&&(acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP)))
         /*if (mode == SYS_FTM_ACL_MODE_4LKP)*/
        {

            begin = g_ftm_master[lchip]->tcam_ad_head_offset;
            g_ftm_master[lchip]->tcam_ad_head_offset += entry_num;
        }
        else if ((mode == SYS_FTM_ACL_MODE_1LKP))
        {
            begin = g_ftm_master[lchip]->tcam_ad_tail_offset - entry_num;
            g_ftm_master[lchip]->tcam_ad_tail_offset = begin;
        }
        else if((mode == SYS_FTM_ACL_MODE_2LKP) && (acl_ipv6_mode == SYS_FTM_ACL_MODE_1LKP))
        {
            acl_tcam0_size  = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0)
                                        + (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0)/2);

            if (acl_tcam0_size <= 2*1024)
            {
                begin = g_ftm_master[lchip]->tcam_ad_head_offset;
                g_ftm_master[lchip]->tcam_ad_head_offset += entry_num;
            }
            else
            {
                begin = g_ftm_master[lchip]->tcam_ad_tail_offset - entry_num;
                g_ftm_master[lchip]->tcam_ad_tail_offset = begin;
            }
        }

        break;
    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0:
        begin =  g_ftm_master[lchip]->tcam_ad_head_offset;
        g_ftm_master[lchip]->tcam_ad_head_offset += entry_num;
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS1:
        if (mode == SYS_FTM_ACL_MODE_4LKP)
        {
            begin = 2*1024;
        }
        else if (mode == SYS_FTM_ACL_MODE_2LKP)
        {
            if(acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP)
            {
                begin = 2*1024;
            }
            else
            {

                acl_tcam0_size  = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0)
                                        + (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0)/2);

                acl0_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0);

                if (acl_tcam0_size <= 2*1024)
                {
                        begin =  2*1024;
                }
                else
                {
                    if(acl0_num > 4*1024)
                    {
                        begin = 6*1024;
                    }
                    else
                    {
                        begin = 4*1024;
                    }
                }


            }
        }

        break;

    case SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1:
        if(((mode == SYS_FTM_ACL_MODE_4LKP) || (mode == SYS_FTM_ACL_MODE_2LKP))
            && (acl_ipv6_mode == SYS_FTM_ACL_MODE_2LKP))
        {
            begin = g_ftm_master[lchip]->tcam_ad_tail_offset - entry_num;
            g_ftm_master[lchip]->tcam_ad_tail_offset = begin;
        }
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS2:
        begin = 4*1024;
        break;

    case SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3:
    case SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3:
        begin = 6*1024;
        break;

    case SYS_FTM_TCAM_TYPE_LPM_USERID:
        opf.pool_index = SYS_FTM_OPF_TYPE_LPM_AD;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset));
        return CTC_E_NONE;

    default:
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, p_offset));
        return CTC_E_NONE;

    }

    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "AD: tcam_key_type:%d, begin:%d\n", tcam_key_type, begin);
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset_from_position(lchip, &opf, entry_num, begin));
    *p_offset = begin;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_alloc_tcam(uint8 lchip)
{
    uint8 index = 0;
    uint32 tcam_key_type = 0;
    uint32 tcam_key_id = MaxTblId_t;
    uint32 tcam_ad_tbl = MaxTblId_t;
    uint32 tcam_key_a[20] = {0};
    uint32 tcam_ad_a[20] = {0};
    char   tcam_key_id_str[40];
    uint8 key_share_num  = 0;
    uint8 ad_share_num  = 0;
    uint8 key_size = 0;
    uint32 entry_num = 0;
    uint32 max_key_index = 0;
    uint32 max_key_index_ipmc_total = 0;
    uint32 max_ad_index = 0;
    uint32 offset = 0;
    uint8 ad_size = 0;
    uint32 is_lpm_tcam = 0;
    tbls_ext_info_t* p_tbl_ext_info = NULL;
    uint8 allocation_time = 0;
#define SYS_GREATBELT_FTM_ALLOC_TCAM_TIMES 2

    for (allocation_time = 0; allocation_time < SYS_GREATBELT_FTM_ALLOC_TCAM_TIMES; allocation_time++)
    {
        for (tcam_key_type = 0; tcam_key_type < SYS_FTM_TCAM_TYPE_MAX; tcam_key_type++)
        {
            if ((SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS <= tcam_key_type)
                && (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS >= tcam_key_type))
            {
                /* egress acl key type not care */
                continue;
            }

            if (g_ftm_master[lchip]->ip_tcam_share_mode)
            {
                if ((SYS_FTM_TCAM_TYPE_IPV6_UCAST == tcam_key_type)
                    ||(SYS_FTM_TCAM_TYPE_IPV6_MCAST == tcam_key_type))
                {
                    continue;
                }
                if(SYS_FTM_TCAM_TYPE_IPV4_UCAST == tcam_key_type)
                {
                    max_key_index = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_UCAST)
                        + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_UCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_UCAST)/SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_UCAST));
                    g_ftm_master[lchip]->ipmc_v4_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_UCAST);
                    max_key_index_ipmc_total = max_key_index;
                }
                else if (SYS_FTM_TCAM_TYPE_IPV4_MCAST == tcam_key_type)
                {
                    max_key_index = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST)
                        + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_MCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_MCAST)/SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_MCAST));
                    g_ftm_master[lchip]->ipmc_v4_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST);
                    max_key_index_ipmc_total = max_key_index;
                }
                else
                {
                    max_key_index = SYS_FTM_KEY_MAX_INDEX(tcam_key_type);
                }
            }
            else
            {
                max_key_index = SYS_FTM_KEY_MAX_INDEX(tcam_key_type);
            }
            key_size      = SYS_FTM_KEY_KEY_SIZE(tcam_key_type);

            if ((0 == key_size) || (0 == max_key_index))
            {
                continue;
            }

            if ((8 != key_size) && (0 == allocation_time))
            {
                continue;
            }

            if ((8 == key_size) && (1 == allocation_time))
            {
                continue;
            }

            if (SYS_FTM_TCAM_TYPE_LPM_USERID == tcam_key_type)
            {
                is_lpm_tcam = 1;
            }

            CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_tcam_info(lchip, tcam_key_type,
                                                              tcam_key_a,
                                                              tcam_ad_a,
                                                              &key_share_num,
                                                              &ad_share_num,
                                                              &ad_size,
                                                              tcam_key_id_str));

            if (0 == key_share_num || 0 == ad_share_num)
            {
                continue;
            }

            CTC_ERROR_RETURN(_sys_greatbelt_ftm_tcam_key_get_offset(lchip, tcam_key_type, &offset));
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TCAM key %s max_key_index = %d, key_size = %d, offset = %d\n",  tcam_key_id_str, max_key_index, key_size, offset);
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "alloced tcam:%d, entry_num:%d\n", g_ftm_master[lchip]->int_tcam_used_entry, max_key_index * key_size);

            /*tcam key alloc*/
            for (index = 0; index < key_share_num; index++)
            {
                tcam_key_id = tcam_key_a[index];

                if (tcam_key_id >= MaxTblId_t)
                {
                    SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", tcam_key_id);
                    return CTC_E_UNEXPECT;
                }

                if (NULL == TABLE_EXT_INFO_PTR(tcam_key_id))
                {
                    p_tbl_ext_info = _sys_greatbelt_ftm_build_tcam_tbl_info(lchip);
                    if (NULL == p_tbl_ext_info)
                    {
                        return CTC_E_NO_MEMORY;
                    }

                    if (NULL == p_tbl_ext_info)
                    {
                        return CTC_E_NO_MEMORY;
                    }

                    TABLE_EXT_INFO_PTR(tcam_key_id) = p_tbl_ext_info;
                }
                TABLE_EXT_INFO_TYPE(tcam_key_id) = EXT_INFO_TYPE_TCAM;

                if (SYS_FTM_TCAM_TYPE_IPV4_UCAST == tcam_key_type)
                {
                    if (g_ftm_master[lchip]->ip_tcam_share_mode)
                    {
                        if ((DsIpv6UcastRouteKey_t == tcam_key_id) || DsIpv6RpfKey_t == tcam_key_id)
                        {
                            key_size = SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_UCAST);
                            max_key_index = max_key_index_ipmc_total / 2;
                        }
                    }
                }
                else if (SYS_FTM_TCAM_TYPE_IPV4_MCAST == tcam_key_type)/*ipv4mcast and ipv4l2mcast share but key size different*/
                {
                    if (DsMacIpv4Key_t == tcam_key_id && 1 == key_size)
                    {
                        key_size = 2;
                        max_key_index /= 2;
                    }
                    if (g_ftm_master[lchip]->ip_tcam_share_mode)
                    {
                        if ((DsIpv6McastRouteKey_t == tcam_key_id) || DsMacIpv6Key_t == tcam_key_id)
                        {
                            key_size = SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_MCAST);
                            max_key_index = max_key_index_ipmc_total / 2;
                        }
                    }
                }

                TABLE_MAX_INDEX(tcam_key_id)  = max_key_index;
                /* table key size are in bytes */
                if (!is_lpm_tcam)
                {
                    TCAM_KEY_SIZE(tcam_key_id) = DRV_BYTES_PER_ENTRY * key_size;
                    TABLE_DATA_BASE(tcam_key_id)  = offset * DRV_BYTES_PER_ENTRY + DRV_INT_TCAM_KEY_DATA_ASIC_BASE;
                    TCAM_MASK_BASE(tcam_key_id)   = offset * DRV_BYTES_PER_ENTRY + DRV_INT_TCAM_KEY_MASK_ASIC_BASE;
                }
                else
                {
                    if (DsLpmTcam160Key_t == tcam_key_id || DsFibUserId160Key_t == tcam_key_id)
                    {
                        TCAM_KEY_SIZE(tcam_key_id) = DRV_BYTES_PER_ENTRY * key_size * 2;
                    }
                    else
                    {
                        TCAM_KEY_SIZE(tcam_key_id) = DRV_BYTES_PER_ENTRY * key_size;
                    }

                    TABLE_DATA_BASE(tcam_key_id) = offset * DRV_BYTES_PER_ENTRY + DRV_INT_LPM_TCAM_DATA_ASIC_BASE;
                    TCAM_MASK_BASE(tcam_key_id)  = offset * DRV_BYTES_PER_ENTRY + DRV_INT_LPM_TCAM_MASK_ASIC_BASE;
                }
            }

            if (SYS_FTM_TCAM_TYPE_IPV4_UCAST == tcam_key_type)
            {
                max_key_index = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_UCAST)
                + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_UCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_UCAST) / SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_UCAST));
                g_ftm_master[lchip]->ipuc_v4_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_UCAST);
            }
            else if (SYS_FTM_TCAM_TYPE_IPV4_MCAST == tcam_key_type)
            {
                max_key_index = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST)
                + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_MCAST)*(SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_MCAST) / SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV4_MCAST));
                g_ftm_master[lchip]->ipmc_v4_num = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST);
            }
            else
            {
                max_key_index = SYS_FTM_KEY_MAX_INDEX(tcam_key_type);
            }
            max_ad_index = ((max_key_index - 1) / 64 + 1) * 64;
            entry_num = max_ad_index * ad_size;

            CTC_ERROR_RETURN(_sys_greatbelt_ftm_tcam_ad_get_offset(lchip, tcam_key_type, entry_num, &offset));
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "AD: tcam_key_type tcam:%d, entry_num:%d offset:%d\n", tcam_key_type, entry_num, offset);

            /*tcam ad alloc*/
            for (index = 0; index < ad_share_num; index++)
            {
                tcam_ad_tbl = tcam_ad_a[index];

                if (tcam_ad_tbl >= MaxTblId_t)
                {
                    SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", tcam_ad_tbl);
                    return CTC_E_UNEXPECT;
                }

                TABLE_MAX_INDEX(tcam_ad_tbl) = max_ad_index;

                if (!is_lpm_tcam)
                {
                    if (TABLE_ENTRY_SIZE(tcam_ad_tbl) == 2 * DRV_BYTES_PER_ENTRY) /* 8 word */
                    {
                        TABLE_DATA_BASE(tcam_ad_tbl) = DRV_INT_TCAM_AD_MEM_8W_BASE +
                            offset * DRV_ADDR_BYTES_PER_ENTRY;
                    }
                    else
                    {
                        TABLE_DATA_BASE(tcam_ad_tbl) = DRV_INT_TCAM_AD_MEM_4W_BASE +
                            offset * DRV_ADDR_BYTES_PER_ENTRY;
                    }
                }
                else
                {
                    TABLE_DATA_BASE(tcam_ad_tbl) = DRV_INT_LPM_TCAM_AD_ASIC_BASE +
                        offset * DRV_ADDR_BYTES_PER_ENTRY;
                }
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_set_profile(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    uint8 profile_index    = 0;

    profile_index = g_ftm_master[lchip]->profile_type;

    switch (profile_index)
    {
    case CTC_FTM_PROFILE_0:
        _sys_greatbelt_ftm_set_profile_default(lchip);
        g_ftm_master[lchip]->glb_met_size     = 7184;
        g_ftm_master[lchip]->glb_nexthop_size = 16016;
        g_ftm_master[lchip]->vsi_number       = 256;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 1024;

        break;

    case CTC_FTM_PROFILE_1:
        _sys_greatbelt_ftm_set_profile_enterprise(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5632;
        g_ftm_master[lchip]->glb_nexthop_size = 16736;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 1024;
        break;

    case CTC_FTM_PROFILE_2:
        _sys_greatbelt_ftm_set_profile_bridge(lchip);
        g_ftm_master[lchip]->glb_met_size     = 6656;
        g_ftm_master[lchip]->glb_nexthop_size = 1024;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 0;
        g_ftm_master[lchip]->logic_met_number = 1024;
        break;

    case CTC_FTM_PROFILE_3:
        _sys_greatbelt_ftm_set_profile_ipv4_route_host(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5120;
        g_ftm_master[lchip]->glb_nexthop_size = 24704;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 2 * 1024;
        break;

    case CTC_FTM_PROFILE_4:
        _sys_greatbelt_ftm_set_profile_ipv4_route_lpm(lchip);
        g_ftm_master[lchip]->glb_met_size     = 6144;
        g_ftm_master[lchip]->glb_nexthop_size = 4608;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 2 * 1024;
        break;

    case CTC_FTM_PROFILE_5:
        _sys_greatbelt_ftm_set_profile_ipv6_route_lpm(lchip);
        g_ftm_master[lchip]->glb_met_size     = 6144;
        g_ftm_master[lchip]->glb_nexthop_size = 5568;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 2 * 1024;
        break;

    case CTC_FTM_PROFILE_6:
        _sys_greatbelt_ftm_set_profile_metro(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5632;
        g_ftm_master[lchip]->glb_nexthop_size = 1024;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 0;
        g_ftm_master[lchip]->logic_met_number = 1024;
        break;

    case CTC_FTM_PROFILE_8:
        _sys_greatbelt_ftm_set_profile_vpls(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5632;
        g_ftm_master[lchip]->glb_nexthop_size = 11392;
        g_ftm_master[lchip]->vsi_number       = 1024;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 0;
        break;

    case CTC_FTM_PROFILE_7:
        _sys_greatbelt_ftm_set_profile_vpws(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5632;
        g_ftm_master[lchip]->glb_nexthop_size = 20608;
        g_ftm_master[lchip]->vsi_number       = 1024;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 0;
        break;

    case CTC_FTM_PROFILE_9:
        _sys_greatbelt_ftm_set_profile_l3vpn(lchip);
        g_ftm_master[lchip]->glb_met_size     = 5632;
        g_ftm_master[lchip]->glb_nexthop_size = 18560;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 1024;
        break;


    case CTC_FTM_PROFILE_10:
        _sys_greatbelt_ftm_set_profile_ptn(lchip);
        g_ftm_master[lchip]->glb_met_size     = 2048;
        g_ftm_master[lchip]->glb_nexthop_size = 18560;
        g_ftm_master[lchip]->vsi_number       = 1024;
        g_ftm_master[lchip]->ecmp_number      = 64;
        g_ftm_master[lchip]->logic_met_number = 0;
        break;


    case CTC_FTM_PROFILE_11:
        _sys_greatbelt_ftm_set_profile_pon(lchip);
        g_ftm_master[lchip]->glb_met_size     = (4096*2);
        g_ftm_master[lchip]->glb_nexthop_size = 18560;
        g_ftm_master[lchip]->vsi_number       = 0;
        g_ftm_master[lchip]->ecmp_number      = 0;
        g_ftm_master[lchip]->logic_met_number = 0;
        break;

    case CTC_FTM_PROFILE_USER_DEFINE:
        _sys_greatbelt_ftm_set_profile_flex(lchip, profile_info);
        g_ftm_master[lchip]->glb_met_size     = profile_info->misc_info.mcast_group_number;
        g_ftm_master[lchip]->glb_nexthop_size = profile_info->misc_info.glb_nexthop_number;
        g_ftm_master[lchip]->vsi_number       = profile_info->misc_info.vsi_number;
        g_ftm_master[lchip]->ecmp_number      = profile_info->misc_info.ecmp_number;
        g_ftm_master[lchip]->logic_met_number = (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV4_MCAST) + SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_MCAST)) * 2 ;
        break;

    default:
        return CTC_E_NONE;
    }

    return CTC_E_NONE;

}

int32
_sys_greatebelt_ftm_init_dynimic_table_base_register(uint8 lchip)
{
    uint32 cmd                = 0;
    uint32 field_id_base      = 0;
    uint32 field_id_max_index = 0;
    uint32 field_id_min_index = 0;
    uint32 field_val          = 0;
    uint32 tbl_id             = 0;
    uint8 base_idx            = 0;
    uint8 base_num            = 0;
    uint8 cam_type            = 0;
    uint8 sram_type           = 0;
    uint32 sum                = 0;

    for (cam_type = 0; cam_type < SYS_FTM_DYN_CAM_TYPE_MAX; cam_type++)
    {
        switch (cam_type)
        {
        case SYS_FTM_DYN_CAM_TYPE_DS_MAC_HASH:
            sram_type          = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
            tbl_id             = DynamicDsMacHashIndexCam_t;
            field_id_base      = DynamicDsMacHashIndexCam_DsMacHashBase0_f;
            field_id_min_index = DynamicDsMacHashIndexCam_DsMacHashMinIndex0_f;
            field_id_max_index = DynamicDsMacHashIndexCam_DsMacHashMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_MAC_IP:
            sram_type          = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
            tbl_id             = DynamicDsDsMacIpIndexCam_t;
            field_id_base      = DynamicDsDsMacIpIndexCam_DsMacIpBase0_f;
            field_id_min_index = DynamicDsDsMacIpIndexCam_DsMacIpMinIndex0_f;
            field_id_max_index = DynamicDsDsMacIpIndexCam_DsMacIpMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LPM_HASH:
            sram_type          = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
            tbl_id             = DynamicDsLpmIndexCam4_t;
            field_id_base      = DynamicDsLpmIndexCam4_DsLpm4Base0_f;
            field_id_min_index = DynamicDsLpmIndexCam4_DsLpm4MinIndex0_f;
            field_id_max_index = DynamicDsLpmIndexCam4_DsLpm4MaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY0:
            sram_type          = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
            tbl_id             = DynamicDsLpmIndexCam0_t;
            field_id_base      = DynamicDsLpmIndexCam0_DsLpm0Base0_f;
            field_id_min_index = DynamicDsLpmIndexCam0_DsLpm0MinIndex0_f;
            field_id_max_index = DynamicDsLpmIndexCam0_DsLpm0MaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY1:
            sram_type          = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
            tbl_id             = DynamicDsLpmIndexCam1_t;
            field_id_base      = DynamicDsLpmIndexCam1_DsLpm1Base0_f;
            field_id_min_index = DynamicDsLpmIndexCam1_DsLpm1MinIndex0_f;
            field_id_max_index = DynamicDsLpmIndexCam1_DsLpm1MaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY2:
            sram_type          = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
            tbl_id             = DynamicDsLpmIndexCam2_t;
            field_id_base      = DynamicDsLpmIndexCam2_DsLpm2Base0_f;
            field_id_min_index = DynamicDsLpmIndexCam2_DsLpm2MinIndex0_f;
            field_id_max_index = DynamicDsLpmIndexCam2_DsLpm2MaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LPM_KEY3:
            sram_type          = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
            tbl_id             = DynamicDsLpmIndexCam3_t;
            field_id_base      = DynamicDsLpmIndexCam3_DsLpm3Base0_f;
            field_id_min_index = DynamicDsLpmIndexCam3_DsLpm3MinIndex0_f;
            field_id_max_index = DynamicDsLpmIndexCam3_DsLpm3MaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_USERID_HASH:
            sram_type          = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
            tbl_id             = DynamicDsDsUserIdHashIndexCam_t;
            field_id_base      = DynamicDsDsUserIdHashIndexCam_DsUserIdHashBase0_f;
            field_id_min_index = DynamicDsDsUserIdHashIndexCam_DsUserIdHashMinIndex0_f;
            field_id_max_index = DynamicDsDsUserIdHashIndexCam_DsUserIdHashMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_USERID:
            sram_type          = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
            tbl_id             = DynamicDsDsUserIdIndexCam_t;
            field_id_base      = DynamicDsDsUserIdIndexCam_DsUserIdIndexBase0_f;
            field_id_min_index = DynamicDsDsUserIdIndexCam_DsUserIdIndexMinIndex0_f;
            field_id_max_index = DynamicDsDsUserIdIndexCam_DsUserIdIndexMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_FWD:
            sram_type          = SYS_FTM_SRAM_TBL_FWD;
            tbl_id             = DynamicDsFwdIndexCam_t;
            field_id_base      = DynamicDsFwdIndexCam_DsFwdIndexBase0_f;
            field_id_min_index = DynamicDsFwdIndexCam_DsFwdIndexMinIndex0_f;
            field_id_max_index = DynamicDsFwdIndexCam_DsFwdIndexMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_MET:
            sram_type          = SYS_FTM_SRAM_TBL_MET;
            tbl_id             = DynamicDsDsMetIndexCam_t;
            field_id_base      = DynamicDsDsMetIndexCam_DsMetBase0_f;
            field_id_min_index = DynamicDsDsMetIndexCam_DsMetMinIndex0_f;
            field_id_max_index = DynamicDsDsMetIndexCam_DsMetMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_NEXTHOP:
            sram_type          = SYS_FTM_SRAM_TBL_NEXTHOP;
            tbl_id             = DynamicDsDsNextHopIndexCam_t;
            field_id_base      = DynamicDsDsNextHopIndexCam_DsNextHopBase0_f;
            field_id_min_index = DynamicDsDsNextHopIndexCam_DsNextHopMinIndex0_f;
            field_id_max_index = DynamicDsDsNextHopIndexCam_DsNextHopMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_EDIT:
            sram_type          = SYS_FTM_SRAM_TBL_EDIT;
            tbl_id             = DynamicDsDsEditIndexCam_t;
            field_id_base      = DynamicDsDsEditIndexCam_DsEditBase0_f;
            field_id_min_index = DynamicDsDsEditIndexCam_DsEditMinIndex0_f;
            field_id_max_index = DynamicDsDsEditIndexCam_DsEditMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_MPLS:
            sram_type          = SYS_FTM_SRAM_TBL_MPLS;
            tbl_id             = DynamicDsDsMplsIndexCam_t;
            field_id_base      = DynamicDsDsMplsIndexCam_DsMplsBase0_f;
            field_id_min_index = DynamicDsDsMplsIndexCam_DsMplsMinIndex0_f;
            field_id_max_index = DynamicDsDsMplsIndexCam_DsMplsMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_OAM:
            sram_type          = SYS_FTM_SRAM_TBL_OAM_MEP;
            tbl_id             = DynamicDsOamIndexCam_t;
            field_id_base      = DynamicDsOamIndexCam_DsOamBase0_f;
            field_id_min_index = DynamicDsOamIndexCam_DsOamMinIndex0_f;
            field_id_max_index = DynamicDsOamIndexCam_DsOamMaxIndex0_f;

            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_LM:
            sram_type          = SYS_FTM_SRAM_TBL_LM;
            tbl_id             = DynamicDsLmStatsIndexCam_t;
            field_id_base      = DynamicDsLmStatsIndexCam_DsLmStatsBase0_f;
            field_id_min_index = DynamicDsLmStatsIndexCam_DsLmStatsMinIndex0_f;
            field_id_max_index = DynamicDsLmStatsIndexCam_DsLmStatsMaxIndex0_f;
            break;

        case SYS_FTM_DYN_CAM_TYPE_DS_STATS:
            sram_type          = SYS_FTM_SRAM_TBL_STATS;
            tbl_id             = DynamicDsDsStatsIndexCam_t;
            field_id_base      = DynamicDsDsStatsIndexCam_DsStatsBase0_f;
            field_id_min_index = DynamicDsDsStatsIndexCam_DsStatsMinIndex0_f;
            field_id_max_index = DynamicDsDsStatsIndexCam_DsStatsMaxIndex0_f;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        base_num = SYS_FTM_TBL_BASE_NUM(sram_type);

        for (base_idx = 0; base_idx < base_num; base_idx++)
        {
            /*write ds cam base value*/
            field_val = SYS_FTM_TBL_BASE_VAL(sram_type, base_idx);
            cmd = DRV_IOW(tbl_id, field_id_base);
            field_val = (field_val >> SYS_FTM_DYNAMIC_SHIFT_NUM);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            /*write ds cam base min index*/
            field_val = SYS_FTM_TBL_BASE_MIN(sram_type, base_idx);
            cmd = DRV_IOW(tbl_id, field_id_min_index);
            field_val = (field_val >> SYS_FTM_DYNAMIC_SHIFT_NUM);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            /*write ds cam base max index*/
            if (SYS_FTM_SRAM_TBL_OAM_MEP == sram_type)
            {
                if (base_num > 1 && base_idx != (base_num - 1))
                {
                    field_val = SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_OAM_MEP,    base_idx);
                }
                else
                {
                    field_val = SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_OAM_MEP,    base_idx) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_OAM_MA,      0) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_OAM_MA_NAME, 0);
                }
            }
            else if (sram_type == SYS_FTM_SRAM_TBL_USERID_HASH_AD)
            {
                if (base_num > 1 && base_idx != (base_num - 1))
                {
                    field_val = SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_USERID_HASH_AD, base_idx);
                }
                else
                {
                    field_val = SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_USERID_HASH_AD, base_idx) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_USERID_IGS_DFT,  0) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_USERID_EGS_DFT,  0) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_USERID_TUNL_DFT, 0) +
                        SYS_FTM_TBL_BASE_MAX(SYS_FTM_SRAM_TBL_OAM_CHAN,        0);
                }
            }
            else
            {
                field_val = SYS_FTM_TBL_BASE_MAX(sram_type, base_idx);

                 /* SYS_FTM_DBG_DUMP("sramtype :%d, max index = %d\n", sram_type, SYS_FTM_TBL_BASE_MAX(sram_type, base_idx));*/

            }

            cmd = DRV_IOW(tbl_id, field_id_max_index);
            field_val = (field_val >> SYS_FTM_DYNAMIC_SHIFT_NUM);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            field_id_base++;
            field_id_min_index++;
            field_id_max_index++;
        }
    }

    /*init oam base*/
    field_val = 0;
    field_id_base = OamTblAddrCtl_MpBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_OAM_MEP);
    field_val = (sum >> 6);
    field_id_base = OamTblAddrCtl_MaBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum  += (SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_OAM_MA));
    field_val = (sum >> 6);
    field_id_base = OamTblAddrCtl_MaNameBaseAddr_f;
    cmd = DRV_IOW(OamTblAddrCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*init userid base*/
    sum = 0;
    field_val = 0;
    field_id_base = UserIdResultCtl_UserIdResultBase_f;
    cmd = DRV_IOW(UserIdResultCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_USERID_HASH_AD);
    field_val = (sum >> 7);
    field_id_base = UserIdResultCtl_UserIdIngressDefaultEntryBase_f;
    cmd = DRV_IOW(UserIdResultCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_USERID_IGS_DFT);
    field_val = (sum >> 7);
    field_id_base = UserIdResultCtl_UserIdEgressDefaultEntryBase_f;
    cmd = DRV_IOW(UserIdResultCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_USERID_EGS_DFT);
    field_val = (sum >> 1);
    field_id_base = UserIdResultCtl_TunnelIdDefaultEntryBase_f;
    cmd = DRV_IOW(UserIdResultCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sum += SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_USERID_TUNL_DFT);
    field_val = SYS_FTM_TBL_MAX_ENTRY_NUM(SYS_FTM_SRAM_TBL_OAM_CHAN) ? (sum >> 6) : 0;
    field_id_base = UserIdResultCtl_OamResultBase_f;
    cmd = DRV_IOW(UserIdResultCtl_t, field_id_base);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_get_edram_bitmap(uint8 lchip, uint8 sram_type, uint32* bitmap)
{
    uint32 bit_map_temp = 0;
    uint32 mem_bit_map  = 0;
    uint8 mem_id        = 0;
    uint8 idx           = 0;
    uint8 bit[SYS_FTM_MAX_ID]     = {0};

    switch (sram_type)
    {
    /* HashKey {edram0, edram1, edram2, edram3, edram6 } */
    case SYS_FTM_SRAM_TBL_FIB_HASH_KEY:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_FIB_HASH_KEY);
        bit[SYS_FTM_SRAM0] = 0;
        bit[SYS_FTM_SRAM1] = 1;
        bit[SYS_FTM_SRAM2] = 2;
        bit[SYS_FTM_SRAM3] = 3;
        bit[SYS_FTM_SRAM6] = 4;
        break;

    /* DsNextHop {edram1, edram3, edram4, edram5} */
    case SYS_FTM_SRAM_TBL_NEXTHOP:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_NEXTHOP);
        bit[SYS_FTM_SRAM1] = 3;
        bit[SYS_FTM_SRAM3] = 2;
        bit[SYS_FTM_SRAM4] = 1;
        bit[SYS_FTM_SRAM5] = 0;
        break;

    /* DsEdit {edram1, edram4, edram5 } */
    case SYS_FTM_SRAM_TBL_EDIT:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_EDIT);
        bit[SYS_FTM_SRAM1] = 2;
        bit[SYS_FTM_SRAM4] = 1;
        bit[SYS_FTM_SRAM5] = 0;
        break;

    /* DsFwd  {edram0, edram3, edram4, edram5} */
    case SYS_FTM_SRAM_TBL_FWD:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_FWD);
        bit[SYS_FTM_SRAM0] = 3;
        bit[SYS_FTM_SRAM3] = 2;
        bit[SYS_FTM_SRAM4] = 1;
        bit[SYS_FTM_SRAM5] = 0;
        break;

    /* DsMetEntry {edram0, edram4, edram5} */
    case SYS_FTM_SRAM_TBL_MET:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_MET);
        bit[SYS_FTM_SRAM0] = 2;
        bit[SYS_FTM_SRAM4] = 1;
        bit[SYS_FTM_SRAM5] = 0;
        break;

    /* DsMac  {edram0, edram4, edram5, edram6} */
    case SYS_FTM_SRAM_TBL_FIB_HASH_AD:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_FIB_HASH_AD);
        bit[SYS_FTM_SRAM0] = 3;
        bit[SYS_FTM_SRAM4] = 2;
        bit[SYS_FTM_SRAM5] = 1;
        bit[SYS_FTM_SRAM6] = 0;
        break;

    /* DsMep  {edram4, edram5, edram6} */
    case SYS_FTM_SRAM_TBL_OAM_MEP:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_OAM_MEP);
        bit[SYS_FTM_SRAM4] = 2;
        bit[SYS_FTM_SRAM5] = 1;
        bit[SYS_FTM_SRAM6] = 0;
        break;

    case SYS_FTM_SRAM_TBL_MPLS:
        /* DsMpls {edram1, edram2, edram4, edram5} */
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_MPLS);
        bit[SYS_FTM_SRAM1] = 3;
        bit[SYS_FTM_SRAM2] = 2;
        bit[SYS_FTM_SRAM4] = 1;
        bit[SYS_FTM_SRAM5] = 0;
        break;

    case SYS_FTM_SRAM_TBL_LM:
        /* DsOamLm {edram8}, must in {edram9} */
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LM);
        bit[SYS_FTM_SRAM2] = 1;
        bit[SYS_FTM_SRAM8] = 0;
        break;

    /* DsStats {edram2, edram3, edram6, edram8} */
    case SYS_FTM_SRAM_TBL_STATS:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_STATS);
        bit[SYS_FTM_SRAM2] = 3;
        bit[SYS_FTM_SRAM3] = 2;
        bit[SYS_FTM_SRAM6] = 1;
        bit[SYS_FTM_SRAM8] = 0;
        break;

    /* DsUserId {edram3, edram4, edram5, edram6} */
    case SYS_FTM_SRAM_TBL_USERID_HASH_AD:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_USERID_HASH_AD);
        bit[SYS_FTM_SRAM3] = 3;
        bit[SYS_FTM_SRAM4] = 2;
        bit[SYS_FTM_SRAM5] = 1;
        bit[SYS_FTM_SRAM6] = 0;
        break;

    /* DsLpmHashKey    {edram3, edram6} */
    case SYS_FTM_SRAM_TBL_LPM_HASH_KEY:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LPM_HASH_KEY);
        bit[SYS_FTM_SRAM3] = 1;
        bit[SYS_FTM_SRAM6] = 0;
        break;

    /* DsLpmLookupKey0 {edram0, edram3} */
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY0:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LPM_LKP_KEY0);
        bit[SYS_FTM_SRAM0] = 1;
        bit[SYS_FTM_SRAM3] = 0;
        break;

    /* DsLpmLookupKey1 {edram1, edram2} */
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY1:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LPM_LKP_KEY1);
        bit[SYS_FTM_SRAM1] = 1;
        bit[SYS_FTM_SRAM2] = 0;
        break;

    /* DsLpmLookupKey2 {edram1, edram2} */
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY2:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LPM_LKP_KEY2);
        bit[SYS_FTM_SRAM1] = 1;
        bit[SYS_FTM_SRAM2] = 0;
        break;

    /* DsLpmLookupKey3 {edram0, edram3} */
    case SYS_FTM_SRAM_TBL_LPM_LKP_KEY3:
        mem_bit_map = SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_LPM_LKP_KEY3);
        bit[SYS_FTM_SRAM0] = 1;
        bit[SYS_FTM_SRAM3] = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    for (mem_id = SYS_FTM_SRAM0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
    {
        if (IS_BIT_SET(mem_bit_map, mem_id))
        {
            idx = bit[mem_id];
            SET_BIT(bit_map_temp, idx);
        }
    }

    *bitmap = bit_map_temp;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ftm_init_edram_bitmap(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 sram_type         = 0;
    uint32 cfg_edram_bitmap = 0;
    dynamic_ds_edram_select_t ds_edram_sel_bitmap;

    /* DsMacHashKey */
    sram_type = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    cmd = DRV_IOW(DynamicDsFdbHashCtl_t, DynamicDsFdbHashCtl_MacLevelBitmap_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cfg_edram_bitmap));

    sal_memset(&ds_edram_sel_bitmap, 0, sizeof(ds_edram_sel_bitmap));

    /* DsEdit */
    sram_type = SYS_FTM_SRAM_TBL_EDIT;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_edit_edram_sel = cfg_edram_bitmap & 0x7;

    /* DsFwd */
    sram_type = SYS_FTM_SRAM_TBL_FWD;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_fwd_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsMac */
    sram_type = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_mac_ip_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsMep */
    sram_type = SYS_FTM_SRAM_TBL_OAM_MEP;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_mep_edram_sel = cfg_edram_bitmap & 0x7;

    /* DsMetEntry */
    sram_type = SYS_FTM_SRAM_TBL_MET;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_met_edram_sel = cfg_edram_bitmap & 0x7;

    /* DsMpls */
    sram_type = SYS_FTM_SRAM_TBL_MPLS;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_mpls_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsNextHop */
    sram_type = SYS_FTM_SRAM_TBL_NEXTHOP;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_next_hop_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsOamLm {edram8}, must in {edram9} */
    sram_type = SYS_FTM_SRAM_TBL_LM;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_lm_stats_edram_sel = cfg_edram_bitmap & 0x1;

    /* DsStats */
    sram_type = SYS_FTM_SRAM_TBL_STATS;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_stats_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsUserId */
    sram_type = SYS_FTM_SRAM_TBL_USERID_HASH_AD;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_ds_user_id_edram_sel = cfg_edram_bitmap & 0xF;

    /* DsLpmHashKey */
    sram_type = SYS_FTM_SRAM_TBL_LPM_HASH_KEY;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_lpm_hash_edram_sel = cfg_edram_bitmap & 0x3;

    /* DsLpmLookupKey0 */
    sram_type = SYS_FTM_SRAM_TBL_LPM_LKP_KEY0;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_lpm_pipe0_edram_sel = cfg_edram_bitmap & 0x3;

    /* DsLpmLookupKey1*/
    sram_type = SYS_FTM_SRAM_TBL_LPM_LKP_KEY1;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_lpm_pipe1_edram_sel = cfg_edram_bitmap & 0x3;

    /* DsLpmLookupKey2 */
    sram_type = SYS_FTM_SRAM_TBL_LPM_LKP_KEY2;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_lpm_pipe2_edram_sel = cfg_edram_bitmap & 0x3;

    /* DsLpmLookupKey3*/
    sram_type = SYS_FTM_SRAM_TBL_LPM_LKP_KEY3;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_get_edram_bitmap(lchip, sram_type, &cfg_edram_bitmap));
    ds_edram_sel_bitmap.cfg_lpm_pipe3_edram_sel = cfg_edram_bitmap & 0x3;

    cmd = DRV_IOW(DynamicDsEdramSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_edram_sel_bitmap));

    return DRV_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_get_fib_bucket_size(uint8 lchip, uint32 entry_num)
{
    int32 bucket_num = 0;

    if (DRV_MEMORY0_MAX_ENTRY_NUM == entry_num)
    {
        bucket_num = 0;
    }
    else if((DRV_MEMORY0_MAX_ENTRY_NUM/2) == entry_num)
    {
        bucket_num = 1;
    }
    else if ((DRV_MEMORY0_MAX_ENTRY_NUM/4) == entry_num)
    {
        bucket_num = 2;
    }
    else if ((DRV_MEMORY0_MAX_ENTRY_NUM/8) == entry_num)
    {
        bucket_num = 3;
    }

    return bucket_num;

}

int32
_sys_greatebelt_ftm_init_fdb_and_lpm_entry_num_register(uint32 lchip)
{
    uint32 lpm_hash_num_mode = 0;
    uint32 cmd               = 0;
    uint8 sram_type          = 0;
    uint8 bucket_num         = 0;
    uint32 mem_alloced       = 0;
    uint32 tbl_offset        = 0;
    uint32 mem_entry_num     = 0;
    uint8 mem_id             = 0;
    uint32 field_val         = 0;
    uint8 bit_index          = 0;
    dynamic_ds_fdb_hash_index_cam_t dynamic_ds_fdb_hash_cam;
    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    /************************************************************
     * FIB HASH ALORIM INIT
     *************************************************************/
#define SYS_FTM_MAC_MIN_BLOCK_NUM 13
    sal_memset(&dynamic_ds_fdb_hash_cam, 0, sizeof(dynamic_ds_fdb_hash_cam));
    sram_type = SYS_FTM_SRAM_TBL_FIB_HASH_KEY;

    cmd = DRV_IOR(DynamicDsFdbHashIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dynamic_ds_fdb_hash_cam));

    /*Level 0*/
    mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM0);
    tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM0);
    bucket_num = _sys_greatbelt_ftm_get_fib_bucket_size(lchip, mem_entry_num);
    dynamic_ds_fdb_hash_cam.mac_num0 = (mem_alloced >> SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level0_bucket_num = bucket_num;
    dynamic_ds_fdb_hash_cam.level0_bucket_base = tbl_offset / (1 << SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level0_bucket_index = 0; /*select Hash alogrim 0*/
    mem_alloced += mem_entry_num;

    /*Level 1*/
    mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM1);
    tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM1);
    bucket_num = _sys_greatbelt_ftm_get_fib_bucket_size(lchip, mem_entry_num);
    dynamic_ds_fdb_hash_cam.mac_num1 = (mem_alloced >> SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level1_bucket_num = bucket_num;
    dynamic_ds_fdb_hash_cam.level1_bucket_base = tbl_offset / (1 << SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level1_bucket_index = 0; /*select Hash alogrim 0*/
    mem_alloced += mem_entry_num;

    /*Level 2*/
    mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM2);
    tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM2);
    bucket_num = _sys_greatbelt_ftm_get_fib_bucket_size(lchip, mem_entry_num);
    dynamic_ds_fdb_hash_cam.mac_num2 = (mem_alloced >> SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level2_bucket_num = bucket_num;
    dynamic_ds_fdb_hash_cam.level2_bucket_base = tbl_offset / (1 << SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level2_bucket_index = 1; /*select Hash alogrim 1*/
    mem_alloced += mem_entry_num;

    /*Level 3*/
    mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM3);
    tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM3);
    bucket_num = _sys_greatbelt_ftm_get_fib_bucket_size(lchip, mem_entry_num);
    dynamic_ds_fdb_hash_cam.mac_num3 = (mem_alloced >> SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level3_bucket_num = bucket_num;
    dynamic_ds_fdb_hash_cam.level3_bucket_base = tbl_offset / (1 << SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level3_bucket_index = 1; /*select Hash alogrim 1*/
    mem_alloced += mem_entry_num;

    /*Level 4*/
    mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM6);
    tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM6);
    bucket_num = _sys_greatbelt_ftm_get_fib_bucket_size(lchip, mem_entry_num);
    dynamic_ds_fdb_hash_cam.mac_num4 = (mem_alloced >> SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level4_bucket_num = bucket_num;
    dynamic_ds_fdb_hash_cam.level4_bucket_base = tbl_offset / (1 << SYS_FTM_MAC_MIN_BLOCK_NUM);
    dynamic_ds_fdb_hash_cam.level4_bucket_index = 0; /*select Hash alogrim 0*/
    mem_alloced += mem_entry_num;

    cmd = DRV_IOW(DynamicDsFdbHashIndexCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dynamic_ds_fdb_hash_cam));

    for (mem_id = SYS_FTM_SRAM0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
    {
        if (IS_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(SYS_FTM_SRAM_TBL_FIB_HASH_AD), mem_id))
        {
            bit_index++;
        }
    }

    field_val = (bit_index > 1) ? 1 : 0;
    cmd = DRV_IOW(DynamicDsFdbHashCtl_t, DynamicDsFdbHashCtl_AdBitsType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_AdBitsType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(IpeAgingCtl_t, IpeAgingCtl_AdBitsType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    /*SYS_FTM_DBG_DUMP("FDB: AdBitsType :%d\n", field_val);*/

    /*for flush by hardware*/
#define FDB_LOOKUP_INDEX_CAM_MAX_INDEX 16
    mem_alloced += FDB_LOOKUP_INDEX_CAM_MAX_INDEX;
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_MacNum_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mem_alloced));

    if (0 == SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_FIB_COLISION))
    {
        field_val = 0;
    }
    else
    {
        field_val = 1;
    }
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_MacTcamLookupEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val))

    /************************************************************
     * LPM ALORIM INIT
     *************************************************************/
    switch (TABLE_MAX_INDEX(DsLpmIpv4Hash8Key_t))
    {
    case (16 * 1024):
        lpm_hash_num_mode = 1;
        break;

    case (8 * 1024):
        lpm_hash_num_mode = 2;
        break;

    case (4 * 1024):
        lpm_hash_num_mode = 3;
        break;

    default:
        lpm_hash_num_mode = 0;
        break;
    }

    cmd = DRV_IOW(FibEngineHashCtl_t, FibEngineHashCtl_LpmHashNumMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &lpm_hash_num_mode));

    /************************************************************
     * UserID ALORIM INIT
     *************************************************************/
    sram_type = SYS_FTM_SRAM_TBL_USERID_HASH_KEY;
    if (IS_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(sram_type), SYS_FTM_SRAM0))
    {
        /*Level 1*/
        /*SYS_FTM_DBG_DUMP("UserId: Hash level1 enable\n");*/
        mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, SYS_FTM_SRAM0);
        tbl_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, SYS_FTM_SRAM0);
        bucket_num = mem_entry_num ? ((DRV_MEMORY0_MAX_ENTRY_NUM / mem_entry_num) - 1) : 0;

        cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));
        user_id_hash_lookup_ctl.level1_hash_en     = 1;
        user_id_hash_lookup_ctl.level1_hash_type   = 1;
        user_id_hash_lookup_ctl.level1_bucket_num  = bucket_num;
        user_id_hash_lookup_ctl.level1_bucket_base = 0;
        cmd = DRV_IOW(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));
    }

    return DRV_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_lkup_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_lookup_ctl_t ipe_lookup_ctl;

    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl_t));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));
    ipe_lookup_ctl.global_vrf_id_lookup_en          = 1;
    ipe_lookup_ctl.stp_block_bridge_disable         = 0;
    /* ingress default use packet vlan, not mapped vlan*/
    ipe_lookup_ctl.acl_use_packet_vlan              = 1;
    ipe_lookup_ctl.mpls_bfd_use_label               = 0;
    ipe_lookup_ctl.ach_cc_use_label                 = 1;
    ipe_lookup_ctl.ach_cv_use_label                 = 1;
    ipe_lookup_ctl.no_ip_mcast_mac_lookup           = 0;
    ipe_lookup_ctl.stp_block_layer3                 = 0;
    ipe_lookup_ctl.route_obey_stp                   = 1;
    ipe_lookup_ctl.mac_hash_lookup_en               = 1;
    ipe_lookup_ctl.oam_obey_acl_qos                 = 1;
    ipe_lookup_ctl.ipv4_ucast_route_key_sa_en       = 0;
    ipe_lookup_ctl.ipv4_ucast_rpf_key_da_en         = 0;
    ipe_lookup_ctl.trill_mbit_check_disable         = 0;
    ipe_lookup_ctl.ipv4_mcast_force_unicast_en      = 1;
    ipe_lookup_ctl.ipv6_mcast_force_unicast_en      = 1;
    ipe_lookup_ctl.routed_port_disable_bcast_bridge = 0;
    ipe_lookup_ctl.ipv4_mcast_force_bridge_en       = 1;
    ipe_lookup_ctl.ipv6_mcast_force_bridge_en       = 1;
    ipe_lookup_ctl.gre_flex_protocol                = 0;
    ipe_lookup_ctl.gre_flex_payload_packet_type     = 0;
    ipe_lookup_ctl.merge_mac_ip_acl_key             = 0;
    ipe_lookup_ctl.service_acl_qos_en               = 0;
    ipe_lookup_ctl.pim_snooping_en                  = 1;
    ipe_lookup_ctl.trill_inner_vlan_check           = 1;
    ipe_lookup_ctl.trill_version                    = 0;
    ipe_lookup_ctl.trill_mcast_mac                  = 1;
    ipe_lookup_ctl.trill_use_inner_vlan             = 1;

    /*ipv4 mc use tcam only*/
    CTC_BIT_UNSET(ipe_lookup_ctl.ip_da_lookup_ctl1, 7);
    CTC_BIT_SET(ipe_lookup_ctl.ip_da_lookup_ctl1, 8);
    /*ipv6 mc use tcam only*/
    CTC_BIT_UNSET(ipe_lookup_ctl.ip_da_lookup_ctl3, 7);
    CTC_BIT_SET(ipe_lookup_ctl.ip_da_lookup_ctl3, 8);

    cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_userid_lkup_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    user_id_hash_lookup_ctl_t user_id_hash_lookup_ctl;

    sal_memset(&user_id_hash_lookup_ctl, 0, sizeof(user_id_hash_lookup_ctl_t));

    cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));

    user_id_hash_lookup_ctl.other_tcam_en    = 1;

    cmd = DRV_IOW(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &user_id_hash_lookup_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatebelt_ftm_tcam_ctl_register(uint8 lchip)
{
    uint32 cmd    = 0;
    uint32  acl_tcam1_size = 0;
    tcam_ctl_int_key_size_cfg_t tcam_ctl_int_key_size_cfg;
    tcam_ctl_int_key_type_cfg_t tcam_ctl_int_key_type_cfg;

    sal_memset(&tcam_ctl_int_key_size_cfg, 0, sizeof(tcam_ctl_int_key_size_cfg_t));
    sal_memset(&tcam_ctl_int_key_type_cfg, 0, sizeof(tcam_ctl_int_key_type_cfg_t));
    if (g_ftm_master[lchip]->acl_mac_mode == SYS_FTM_ACL_MODE_4LKP)
    {
        if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
        {
            tcam_ctl_int_key_type_cfg.key160_acl0_en = (0x3 << 0);
            tcam_ctl_int_key_type_cfg.key160_acl1_en = (0x3 << 2);
            tcam_ctl_int_key_type_cfg.key160_acl2_en = (0x3 << 4);
            tcam_ctl_int_key_type_cfg.key160_acl3_en = (0x3 << 6);
        }
        else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
        {
            tcam_ctl_int_key_type_cfg.key320_acl0_en = 1 << 0;
            tcam_ctl_int_key_type_cfg.key320_acl1_en = 1 << 1;
            tcam_ctl_int_key_type_cfg.key320_acl2_en = 1 << 2;
            tcam_ctl_int_key_type_cfg.key320_acl3_en = 1 << 3;
        }

        if (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) != 0)
        {
            tcam_ctl_int_key_type_cfg.key640_acl0_en = (1 << 0);
        }

        if (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1) != 0)
        {
            tcam_ctl_int_key_type_cfg.key640_acl1_en = (1 << 1);
        }

    }

    if (g_ftm_master[lchip]->acl_mac_mode ==  SYS_FTM_ACL_MODE_2LKP)
    {
        if(g_ftm_master[lchip]->acl_ipv6_mode ==  SYS_FTM_ACL_MODE_2LKP)
        {
            if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
            {
                tcam_ctl_int_key_type_cfg.key160_acl0_en = (0x3 << 0);
                tcam_ctl_int_key_type_cfg.key160_acl1_en = (0xF << 2);
            }
            else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
            {
                tcam_ctl_int_key_type_cfg.key320_acl0_en = (0x1 << 0);
                tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x3 << 1);
            }

            tcam_ctl_int_key_type_cfg.key640_acl0_en = (1 << 0);
            tcam_ctl_int_key_type_cfg.key640_acl1_en = (1 << 1);
        }
        else
        {
            if(1 == g_ftm_master[lchip]->acl1_mode)
            {
                if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
                {
                    tcam_ctl_int_key_type_cfg.key160_acl0_en = (0x3F << 0);
                    tcam_ctl_int_key_type_cfg.key160_acl1_en = (0x3 << 6);
                }
                else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
                {
                    tcam_ctl_int_key_type_cfg.key320_acl0_en = (0x7 << 0);
                    tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x1 << 3);
                }
            }
            else if (0 == g_ftm_master[lchip]->acl1_mode )
            {
                if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
                {
                    tcam_ctl_int_key_type_cfg.key160_acl0_en = (0xF << 0);
                    tcam_ctl_int_key_type_cfg.key160_acl1_en = (0xF << 4);
                }
                else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
                {
                    tcam_ctl_int_key_type_cfg.key320_acl0_en = (0x3 << 0);
                    tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x1 << 2);
                }
            }
            else if(2 == g_ftm_master[lchip]->acl1_mode )
            {

                acl_tcam1_size  = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1)
                                        + (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) * SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0)/2);

                 if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
                {
                    tcam_ctl_int_key_type_cfg.key160_acl0_en = (0x3 << 0);

                    if(acl_tcam1_size <= 2*1024)
                    {
                        tcam_ctl_int_key_type_cfg.key160_acl1_en = (0x3 << 2);
                    }
                    else if(acl_tcam1_size <= 4*1024)
                    {
                        tcam_ctl_int_key_type_cfg.key160_acl1_en = (0xF << 2);
                    }
                    else
                    {
                        tcam_ctl_int_key_type_cfg.key160_acl1_en = (0x3F << 2);
                    }

                }
                else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
                {
                    tcam_ctl_int_key_type_cfg.key320_acl0_en = (0x1 << 0);
                    if(acl_tcam1_size <= 2*1024)
                    {
                        tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x1 << 1);
                    }
                    else if(acl_tcam1_size <= 4*1024)
                    {
                        tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x3 << 1);
                    }
                    else
                    {
                        tcam_ctl_int_key_type_cfg.key320_acl1_en = (0x7 << 1);
                    }
                }
            }


            if(2 == g_ftm_master[lchip]->acl1_mode )
            {
                if (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) != 0)
                {
                    tcam_ctl_int_key_type_cfg.key640_acl0_en = (1 << 0);
                }
            }
            else
            {
                if (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) != 0)
                {
                    tcam_ctl_int_key_type_cfg.key640_acl0_en = (1 << 1);
                }
            }


        }
    }

    if (g_ftm_master[lchip]->acl_mac_mode ==  SYS_FTM_ACL_MODE_1LKP)
    {
        if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 2)
        {
            tcam_ctl_int_key_type_cfg.key160_acl0_en = 0xFF;
        }
        else if (SYS_FTM_KEY_KEY_SIZE(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0) == 4)
        {
            tcam_ctl_int_key_type_cfg.key320_acl0_en = 0xF;
        }

        if (SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0) != 0)
        {
            tcam_ctl_int_key_type_cfg.key640_acl0_en = (1 << 1);
        }
    }

    cmd = DRV_IOW(TcamCtlIntKeyTypeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_ctl_int_key_type_cfg));

    tcam_ctl_int_key_size_cfg.key80_en  = 0xFFFF;
    tcam_ctl_int_key_size_cfg.key160_en = 0xFF;
    tcam_ctl_int_key_size_cfg.key320_en = 0xF;
    tcam_ctl_int_key_size_cfg.key640_en = 0x3;
    cmd = DRV_IOW(TcamCtlIntKeySizeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tcam_ctl_int_key_size_cfg));

    return DRV_E_NONE;
}


STATIC int32
_sys_greatbelt_fib_lkup_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    fib_engine_lookup_result_ctl_t fib_engine_lookup_result_ctl;
    fib_engine_lookup_ctl_t fib_engine_lookup_ctl;
    fib_engine_hash_ctl_t fib_engine_hash_ctl;
    fib_engine_lookup_result_ctl1_t fib_engine_lookup_result_ctl1;

    sal_memset(&fib_engine_lookup_result_ctl, 0, sizeof(fib_engine_lookup_result_ctl_t));
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));
    fib_engine_lookup_result_ctl.mac_da_lookup_result_ctl      = 0X1;
    fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl0      = 0X29;
    fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl1      = 0X2B;
    fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl2      = 0X2D;
    fib_engine_lookup_result_ctl.ip_da_lookup_result_ctl3      = 0X2F;
    fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl0      = 0X50;
    fib_engine_lookup_result_ctl.ip_sa_lookup_result_ctl1      = 0X5A0;
    fib_engine_lookup_result_ctl.fcoe_da_lookup_result_ctl     = 0X20F00;
    fib_engine_lookup_result_ctl.fcoe_sa_lookup_result_ctl     = 0X21000;
    fib_engine_lookup_result_ctl.trill_ucast_lookup_result_ctl = 0X21500;
    fib_engine_lookup_result_ctl.trill_mcast_lookup_result_ctl = 0X21600;

    sal_memset(&fib_engine_lookup_result_ctl1, 0, sizeof(fib_engine_lookup_result_ctl1_t));
    fib_engine_lookup_result_ctl1.ds_mac_base1                 = 0X100;
    fib_engine_lookup_result_ctl1.ds_mac_base0                 = 0X4E0;

    sal_memset(&fib_engine_lookup_ctl, 0, sizeof(fib_engine_lookup_ctl_t));
    sal_memset(&fib_engine_hash_ctl, 0, sizeof(fib_engine_hash_ctl_t));
    fib_engine_hash_ctl.lpm_hash_num_mode    = 0x1;
    fib_engine_hash_ctl.lpm_hash_bucket_type = 0;

    cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl));

    cmd = DRV_IOW(FibEngineLookupResultCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_result_ctl1));

    cmd = DRV_IOW(FibEngineLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_lookup_ctl));

    cmd = DRV_IOW(FibEngineHashCtl_t, DRV_ENTRY_FLAG);

    return CTC_E_NONE;

}

STATIC int32
_sys_greatebelt_ftm_init_dynimic_table_register(uint8  lchip)
{
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_init_edram_bitmap(lchip));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_dynimic_table_base_register(lchip));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_fdb_and_lpm_entry_num_register(lchip));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_lkp_register_init(uint8 lchip)
{

    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_dynimic_table_register(lchip));

    CTC_ERROR_RETURN(_sys_greatebelt_ftm_init_lkp_register(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_userid_lkup_ctl_init(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_lkup_ctl_init(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_fib_lkup_ctl_init(lchip));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_tcam_ctl_register(lchip));

    return DRV_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_init(uint8 lchip)
{
    if (NULL != g_ftm_master[lchip])
    {
        return CTC_E_NONE;
    }

    g_ftm_master[lchip] = mem_malloc(MEM_FTM_MODULE, sizeof(g_ftm_master_t));

    if (NULL == g_ftm_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(g_ftm_master[lchip], 0, sizeof(g_ftm_master_t));

    g_ftm_master[lchip]->p_sram_tbl_info =
        mem_malloc(MEM_FTM_MODULE, sizeof(sys_ftm_sram_tbl_info_t) * SYS_FTM_SRAM_TBL_MAX);
    if (NULL == g_ftm_master[lchip]->p_sram_tbl_info)
    {
        mem_free(g_ftm_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(g_ftm_master[lchip]->p_sram_tbl_info, 0, sizeof(sys_ftm_sram_tbl_info_t) * SYS_FTM_SRAM_TBL_MAX);

    g_ftm_master[lchip]->p_tcam_key_info =
        mem_malloc(MEM_FTM_MODULE, sizeof(sys_ftm_tcam_key_info_t) * SYS_FTM_TCAM_TYPE_MAX);
    if (NULL == g_ftm_master[lchip]->p_sram_tbl_info)
    {
        mem_free(g_ftm_master[lchip]->p_sram_tbl_info);
        mem_free(g_ftm_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(g_ftm_master[lchip]->p_tcam_key_info, 0, sizeof(sys_ftm_tcam_key_info_t) * SYS_FTM_TCAM_TYPE_MAX);
    g_ftm_master[lchip]->ip_tcam_share_mode = 1;
    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_get_table_entry_num(uint8 lchip, uint32 table_id, uint32* entry_num)
{
    CTC_DEBUG_ADD_STUB_CTC(NULL);

    CTC_PTR_VALID_CHECK(entry_num);

    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    *entry_num = TABLE_MAX_INDEX(table_id);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_get_entry_num_by_type(uint8 lchip, ctc_ftm_key_type_t key_type, uint32* entry_num)
{
    uint32 key_id = 0;

    CTC_DEBUG_ADD_STUB_CTC(NULL);

    CTC_PTR_VALID_CHECK(entry_num);

    if (key_type >= CTC_FTM_KEY_TYPE_MAX)
    {
        SYS_FTM_DBG_DUMP("unexpect key_type :%d\r\n", key_type);
        return CTC_E_UNEXPECT;
    }

    key_id = _sys_greatbelt_ftm_get_id(lchip, TRUE, key_type);

    if (key_id == 0xFFFF)
    {
        SYS_FTM_DBG_DUMP("unexpect key_id :%d\r\n", key_id);
        return CTC_E_UNEXPECT;
    }

    *entry_num = SYS_FTM_KEY_MAX_INDEX(key_id);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_alloc_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32* offset)
{
    uint32 opf_type = 0;
    uint8  multi_alloc = 0;
    sys_greatbelt_opf_t opf;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    CTC_PTR_VALID_CHECK(offset);
    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    switch (table_id)
    {
    case DsMacBridgeKey_t:
    case DsFcoeRouteKey_t:
    case DsTrillUcastRouteKey_t:
    case DsTrillMcastRouteKey_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_COLISION;
        break;

    case DsMac_t:
    case DsIpDa_t:
    case DsIpSaNat_t:
    case DsFcoeDa_t:
    case DsFcoeSa_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_AD;
        break;

    case DsAcl_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_AD;
        block_size  = 2 * block_size;
        multi_alloc = 2;
        break;

    case DsLpmTcam80Key_t:
    case DsFibUserId80Key_t:
        opf_type = SYS_FTM_OPF_TYPE_LPM_USERID;
        break;

    case DsLpmTcam160Key_t:
    case DsFibUserId160Key_t:
        block_size  = 2 * block_size;
        multi_alloc = 2;
        opf_type = SYS_FTM_OPF_TYPE_LPM_USERID;
        break;

    default:
        SYS_FTM_DBG_DUMP("Not suport tbl id:%d now!!\r\n", table_id);
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = opf_type;
    opf.multiple  = multi_alloc;
    opf.reverse = dir;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, block_size, offset));

     /* SYS_FTM_DBG_DUMP("FTM: alloc tableId:%d block_size:%d, offset:%d\n", table_id, block_size, *offset);*/
    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_free_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32 offset)
{
    uint32 opf_type = 0;
    sys_greatbelt_opf_t opf;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    switch (table_id)
    {
    case DsMacBridgeKey_t:
    case DsFcoeRouteKey_t:
    case DsTrillUcastRouteKey_t:
    case DsTrillMcastRouteKey_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_COLISION;
        break;

    case DsMac_t:
    case DsIpDa_t:
    case DsIpSaNat_t:
    case DsFcoeDa_t:
    case DsFcoeSa_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_AD;
        break;

    case DsAcl_t:
        opf_type = SYS_FTM_OPF_TYPE_FIB_AD;
        block_size  = 2 * block_size;
        break;

    case DsLpmTcam80Key_t:
    case DsFibUserId80Key_t:
        opf_type = SYS_FTM_OPF_TYPE_LPM_USERID;
        break;

    case DsLpmTcam160Key_t:
    case DsFibUserId160Key_t:
        block_size = block_size * 2;
        opf_type = SYS_FTM_OPF_TYPE_LPM_USERID;
        break;

    default:
        SYS_FTM_DBG_DUMP("FTM: Not suport tbl id:%d now!!\r\n", table_id);
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = opf_type;

    if (0 == dir)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, block_size, offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, block_size, offset));
    }

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : sys_greatbelt_ftm_get_dyn_entry_size
 Description  : xx
 Input        : sys_ftm_dyn_entry_type_t type
                uint32* p_size
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/4
 Modification : Created function

*****************************************************************************/
int32
sys_greatbelt_ftm_get_dyn_entry_size(uint8 lchip, sys_ftm_dyn_entry_type_t type,
                                     uint32* p_size)
{

    CTC_PTR_VALID_CHECK(p_size);

    switch (type)
    {
    case SYS_FTM_DYN_ENTRY_GLB_MET:
        *p_size = g_ftm_master[lchip]->glb_met_size;
        break;

    case SYS_FTM_DYN_ENTRY_VSI:
        *p_size = g_ftm_master[lchip]->vsi_number;
        break;

    case SYS_FTM_DYN_ENTRY_ECMP:
        *p_size = g_ftm_master[lchip]->ecmp_number;
        break;

    case SYS_FTM_DYN_ENTRY_LOGIC_MET:
        *p_size = g_ftm_master[lchip]->logic_met_number;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_get_dynamic_table_info(uint8 lchip, uint32 tbl_id,
                                         uint8* dyn_tbl_idx,
                                         uint32* entry_enum,
                                         uint32* nh_glb_entry_num)
{
    uint8 dyn_tbl = 0;
    uint8 blk_index = 0;

    CTC_PTR_VALID_CHECK(dyn_tbl_idx);
    CTC_PTR_VALID_CHECK(entry_enum);
    CTC_PTR_VALID_CHECK(nh_glb_entry_num);

    *entry_enum = 0;
    *nh_glb_entry_num = 0;

    switch (tbl_id)
    {
    case DsFwd_t:
        dyn_tbl = SYS_FTM_SRAM_TBL_FWD;
        break;

    case DsMetEntry_t:
        dyn_tbl = SYS_FTM_SRAM_TBL_MET;
        break;

    case DsNextHop4W_t:
        dyn_tbl = SYS_FTM_SRAM_TBL_NEXTHOP;
        break;

    case DsL2EditEth4W_t:
        dyn_tbl = SYS_FTM_SRAM_TBL_EDIT;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    for (blk_index = 0; blk_index < SYS_FTM_DYN_NH_MAX; blk_index++)
    {
        if (!SYS_FTM_DYN_NH_VALID(blk_index))
        {
            continue;
        }

        if (IS_BIT_SET(SYS_FTM_DYN_NH_BITMAP(blk_index), dyn_tbl))
        {
            *dyn_tbl_idx = blk_index;
            *entry_enum  = SYS_FTM_TBL_MAX_ENTRY_NUM(dyn_tbl);

            if (SYS_FTM_SRAM_TBL_MET == dyn_tbl)
            {

                *nh_glb_entry_num = g_ftm_master[lchip]->glb_met_size;
            }

            if (SYS_FTM_SRAM_TBL_NEXTHOP == dyn_tbl)
            {
                *nh_glb_entry_num = g_ftm_master[lchip]->glb_nexthop_size;
            }

            return CTC_E_NONE;
        }
    }

    return CTC_E_INVALID_PARAM;

}

STATIC int32
_sys_greatebelt_ftm_get_opf_size(uint8 lchip, uint32 opf_type, uint32* entry_num)
{
    uint32 key_type = 0;
    CTC_PTR_VALID_CHECK(entry_num);

    switch (opf_type)
    {
    case SYS_FTM_OPF_TYPE_FIB_COLISION:
        key_type = SYS_FTM_TCAM_TYPE_FIB_COLISION;
        *entry_num = SYS_FTM_KEY_MAX_INDEX(key_type);
        break;

    case SYS_FTM_OPF_TYPE_FIB_AD:
        key_type = SYS_FTM_SRAM_TBL_FIB_HASH_AD;
        *entry_num = SYS_FTM_TBL_MAX_ENTRY_NUM(key_type);
        break;

    case SYS_FTM_OPF_TYPE_LPM_USERID:
        key_type = SYS_FTM_TCAM_TYPE_LPM_USERID;
        *entry_num = SYS_FTM_KEY_MAX_INDEX(key_type);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatebelt_ftm_opf_init_tcam(uint8 lchip, uint8 is_reinit)
{
    uint16 start = 0;
    uint8 pool_num = 0;
    sys_greatbelt_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    if (is_reinit)
    {
        sys_greatbelt_opf_free(lchip, OPF_FTM, SYS_FTM_OPF_TYPE_TCAM_KEY0);/*for adjust tcam resouce*/
        sys_greatbelt_opf_free(lchip, OPF_FTM, SYS_FTM_OPF_TYPE_TCAM_AD0);
        sys_greatbelt_opf_free(lchip, OPF_FTM, SYS_FTM_OPF_TYPE_LPM_KEY);
        sys_greatbelt_opf_free(lchip, OPF_FTM, SYS_FTM_OPF_TYPE_LPM_AD);
    }

    pool_num = SYS_FTM_OPF_TYPE_MAX;
    sys_greatbelt_opf_init(lchip, OPF_FTM, pool_num);

    opf.pool_type = OPF_FTM;

    opf.pool_index = SYS_FTM_OPF_TYPE_TCAM_KEY0;
    start = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, start, 8 * 1024));

    opf.pool_index = SYS_FTM_OPF_TYPE_TCAM_AD0;
    start = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, start, 8 * 1024));


    opf.pool_index = SYS_FTM_OPF_TYPE_LPM_KEY;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, 256));

    opf.pool_index = SYS_FTM_OPF_TYPE_LPM_AD;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, 256 * 2));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatebelt_ftm_opf_init_table_share(uint8 lchip)
{
    uint8 i = 0;
    uint32 entry_num = 0;
    sys_greatbelt_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    for (i = SYS_FTM_OPF_TYPE_FIB_COLISION; i < SYS_FTM_OPF_TYPE_MAX; i++)
    {
        opf.pool_type = OPF_FTM;
        opf.pool_index = i;
        CTC_ERROR_RETURN(_sys_greatebelt_ftm_get_opf_size(lchip, i, &entry_num));
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, entry_num));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ftm_set_tcam_mode(uint8 lchip)
{
    uint16 acl0_size = 0;
    uint16 acl1_size = 0;
    uint16 acl2_size = 0;
    uint16 acl3_size = 0;

    uint16 acl0_ipv6_size = 0;
    uint16 acl1_ipv6_size = 0;

    acl0_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS0);
    acl1_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS1);
    acl2_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS2);
    acl3_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3);

    acl0_ipv6_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0);
    acl1_ipv6_size = SYS_FTM_KEY_MAX_INDEX(SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1);

    if(acl0_ipv6_size&&acl1_ipv6_size)
    {
        g_ftm_master[lchip]->acl_ipv6_mode = SYS_FTM_ACL_MODE_2LKP;
    }
    else if((0 != acl0_ipv6_size) && (0 == acl1_ipv6_size))
    {
        g_ftm_master[lchip]->acl_ipv6_mode = SYS_FTM_ACL_MODE_1LKP;
    }
    else
    {
        g_ftm_master[lchip]->acl_ipv6_mode = SYS_FTM_ACL_MODE_0LKP;
    }

    if ((acl0_size&&acl1_size&&acl2_size&&acl3_size))
    {
        g_ftm_master[lchip]->acl_mac_mode = SYS_FTM_ACL_MODE_4LKP;
    }
    else if ((acl1_size + acl2_size + acl3_size) == 0)
    {
        g_ftm_master[lchip]->acl_mac_mode = SYS_FTM_ACL_MODE_1LKP;
    }
    else if ( ((acl2_size + acl3_size) == 0) && acl0_size
&& acl1_size)
    {
        g_ftm_master[lchip]->acl_mac_mode = SYS_FTM_ACL_MODE_2LKP;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }
    g_ftm_master[lchip]->tcam_head_offset = 0;
    g_ftm_master[lchip]->tcam_ad_head_offset = 0;
    g_ftm_master[lchip]->tcam_tail_offset = 6*1024;
    g_ftm_master[lchip]->tcam_ad_tail_offset = 6*1024;


    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "acl_mac_mode:%d, acl_ipv6_mode:%d\n", g_ftm_master[lchip]->acl_mac_mode, g_ftm_master[lchip]->acl_ipv6_mode);

    return CTC_E_NONE;

}

int32
sys_greatbelt_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    CTC_PTR_VALID_CHECK(profile_info);

    if (profile_info->profile_type >= CTC_FTM_PROFILE_MAX)
    {
        return CTC_E_NOT_SUPPORT;
    }

    /*init global param*/
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_init(lchip));

    /*init opf */
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_opf_init_tcam(lchip, 0));

    /*set profile*/
    g_ftm_master[lchip]->profile_type = profile_info->profile_type;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_set_profile(lchip, profile_info));

    CTC_ERROR_RETURN(_sys_greatbelt_ftm_set_tcam_mode(lchip));

    /*alloc hash table*/
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_alloc_sram(lchip));

    /* alloc tcam table*/
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_alloc_tcam(lchip));

    /*init table share */
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_opf_init_table_share(lchip));

    /*init sim module */
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_init_sim_module(0, 1));
    return CTC_E_NONE;

}

int32
sys_greatbelt_ftm_mem_free(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_ftm_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, OPF_FTM);

    mem_free(g_ftm_master[lchip]->p_tcam_key_info);
    mem_free(g_ftm_master[lchip]->p_sram_tbl_info);
    mem_free(g_ftm_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_get_profile(uint8 lchip, uint32* p_profile)
{
    *p_profile = g_ftm_master[lchip]->profile_type;
    return CTC_E_NONE;
}


uint8
sys_greatbelt_ftm_get_ip_tcam_share_mode(uint8 lchip)
{
    return g_ftm_master[lchip]->ip_tcam_share_mode;
}

uint16
sys_greatbelt_ftm_get_ipuc_v4_number(uint8 lchip)
{
    return g_ftm_master[lchip]->ipuc_v4_num;
}

uint16
sys_greatbelt_ftm_get_ipmc_v4_number(uint8 lchip)
{
    return g_ftm_master[lchip]->ipmc_v4_num;
}

extern int32
sram_model_update_tbl_info(uint32 tbl_id, uint8 block_id, uint32 offset, uint32 size);

int32
sys_greatbelt_ftm_set_mpls_label_offset(uint8 lchip, uint8 mpls_block_id, uint32 offset, uint32 size)
{
    uint8 blk_id = 0;
    uint8 blk_num = 0;
    uint32 tbl_id = DsMpls_t;

    uint32 min_index    = 0;
    uint32 max_index    = 0;
    uint32 cmd = 0;
    uint32 field_id_min_index = 0;
    uint32 field_id_max_index = 0;

    uint32 entry_num = 0;
    uint32 max_entry = 0;
    /*check param*/
    if((offset%1024) || (size%1024))
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    if(mpls_block_id > 3)
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    /*1.update drv*/
    for (blk_id = 0; blk_id < MAX_DRV_BLOCK_NUM; blk_id++)
    {
        if (!IS_BIT_SET(DYNAMIC_BITMAP(tbl_id), blk_id))
        {
            continue;
        }
        else
        {
            if(blk_num == mpls_block_id)
            {
                 /*entry_num = DYNAMIC_END_INDEX(tbl_id, blk_id) - DYNAMIC_START_INDEX(tbl_id, blk_id);*/
                entry_num = (size << 1);
                if(entry_num > DYNAMIC_ENTRY_NUM(tbl_id, blk_id))
                {
                    return CTC_E_INTR_INVALID_PARAM;
                }
                DYNAMIC_START_INDEX(tbl_id, blk_id) = offset;
                DYNAMIC_END_INDEX(tbl_id, blk_id)   = offset + ((entry_num >> 1) - 1);
            }
            if((DYNAMIC_END_INDEX(tbl_id, blk_id)+1) > max_entry)
            {
                max_entry = DYNAMIC_END_INDEX(tbl_id, blk_id) + 1;
            }

            blk_num ++;
        }
    }
    TABLE_MAX_INDEX(tbl_id) = max_entry;


    /*2.update model*/
#if (SDK_WORK_PLATFORM == 1)
    sram_model_update_tbl_info(tbl_id, mpls_block_id, offset, size);
#endif

    /*3.update cam*/
    tbl_id             = DynamicDsDsMplsIndexCam_t;
    field_id_min_index = DynamicDsDsMplsIndexCam_DsMplsMinIndex0_f + mpls_block_id;
    field_id_max_index = DynamicDsDsMplsIndexCam_DsMplsMaxIndex0_f + mpls_block_id;

    /*write ds cam base min index*/
     /*min_index = SYS_FTM_TBL_BASE_MIN(sram_type, mpls_block_id);*/
    min_index = (offset << 1);
    cmd = DRV_IOW(tbl_id, field_id_min_index);
    min_index = (min_index >> SYS_FTM_DYNAMIC_SHIFT_NUM);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &min_index));

    /*write ds cam base max index*/
     /*max_index = SYS_FTM_TBL_BASE_MAX(sram_type, mpls_block_id) - SYS_FTM_TBL_BASE_MIN(sram_type, mpls_block_id);*/
    max_index = (size << 1) - 1;
    max_index += (offset << 1);
    cmd = DRV_IOW(tbl_id, field_id_max_index);
    max_index = (max_index >> SYS_FTM_DYNAMIC_SHIFT_NUM);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &max_index));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_check_tbl_recover(uint8 lchip, uint8 ram_id,uint32 ram_offset,
                                        uint8 *b_recover)
{
    uint32  sram_type       = 0;
    uint32  mem_entry_num   = 0;
    uint32  start_offset    = 0;
    /*
    ram type
    0: ram0
    1: ram1
    2: ram2
    3: ram3
    4: ram4
    5: ram5
    6: ram6
    7: ram7
    8: ram8
    9: ram9
    */
    if ((ram_id > 9) || (NULL == b_recover))
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    if (9 == ram_id)
    {/*dsoamlm*/
        *b_recover = 0;
        return CTC_E_NONE;
    }

    for (sram_type = 0; sram_type < SYS_FTM_SRAM_TBL_MAX; sram_type++)
    {
        mem_entry_num = SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, ram_id);
        if(!CTC_IS_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(sram_type), ram_id) || 0 == mem_entry_num)
        {
            continue;
        }
        else
        {
            start_offset = SYS_FTM_TBL_MEM_START_OFFSET(sram_type, ram_id);
            if((ram_offset >= start_offset) && (ram_offset < (start_offset+ mem_entry_num)))
            {

                break;
            }
            else
            {
                continue;
            }
        }
    }

    if (sram_type== SYS_FTM_SRAM_TBL_MAX)
    {
        *b_recover = 0;
        return CTC_E_NONE;
    }
    else
    {
        switch(sram_type)
        {
            case  SYS_FTM_SRAM_TBL_FIB_HASH_KEY:
            case  SYS_FTM_SRAM_TBL_OAM_MEP:
            case  SYS_FTM_SRAM_TBL_STATS:
            case  SYS_FTM_SRAM_TBL_LM:
                *b_recover = 0;
                break;
            default:
                *b_recover = 1;
                break;
        }
    }
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "SRAM TYPE is %d, revover: %s",
                        sram_type, ((1 == *b_recover)? "Y":"N") );
    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_show_alloc_info(uint8 lchip)
{
    uint32 used_tcam_entry      = 0;
    uint32 max_index            = 0;
    uint32 total_entry          = 0;
    uint32 in_use_entry         = 0;
    uint32 not_in_use_entry     = 0;
    uint32 tcam_key_type        = 0;
    uint32 tcam_key_id          = MaxTblId_t;
    uint32 tcam_key_a[20]       = {0};
    uint32 tcam_ad_a[20]        = {0};
    char   tcam_key_id_str[40];
    uint8 key_share_num         = 0;
    uint8 ad_share_num          = 0;
    uint32 key_size             = 0;

    uint32 sram_type            = 0;
    uint32 sram_tbl_a[50]       = {0};
    uint8 share_tbl_num         = 0;
    uint8 ad_size               = 0;

    uint8 mem_id                = 0;
    char sram_tbl_id_str[40];

    SYS_FTM_INIT_CHECK(lchip);
    /* Show internal Tcam alloc info */
    total_entry         = DRV_INT_TCAM_KEY_MAX_ENTRY_NUM;
    in_use_entry        = g_ftm_master[lchip]->int_tcam_used_entry;
    not_in_use_entry    = total_entry - in_use_entry;

    LCHIP_CHECK(lchip);
    SYS_FTM_DBG_DUMP("\n-----------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Internal Tcam                                       %-d\n", total_entry);
    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  In Use                                              %-d\n", in_use_entry);
    SYS_FTM_DBG_DUMP("  Free                                                %-d\n", not_in_use_entry);
    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Key Id                      Key Size    Max Index   Used Tcam Entry\n");
    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");

    for (tcam_key_type = 0; tcam_key_type < SYS_FTM_TCAM_TYPE_MAX; tcam_key_type++)
    {

        if ((SYS_FTM_TCAM_TYPE_LPM_USERID == tcam_key_type) || ((SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0_EGRESS <= tcam_key_type)
                && (SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3_EGRESS >= tcam_key_type)))
        {
            continue;
        }

        _sys_greatbelt_ftm_get_tcam_info(lchip, tcam_key_type,
                                         tcam_key_a,
                                         tcam_ad_a,
                                         &key_share_num,
                                         &ad_share_num,
                                         &ad_size,
                                         tcam_key_id_str);
        if (0 == key_share_num || 0 == ad_share_num)
        {
            continue;
        }

        tcam_key_id = tcam_key_a[0];
        if (tcam_key_id >= MaxTblId_t)
        {
            SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", tcam_key_id);
            return CTC_E_UNEXPECT;
        }

        max_index           = TABLE_MAX_INDEX(tcam_key_id);

        if (0 == max_index)
        {
            continue;
        }

        key_size            = TCAM_KEY_SIZE(tcam_key_id);

        used_tcam_entry     = max_index * key_size / DRV_BYTES_PER_ENTRY;
        key_size            = (key_size / DRV_BYTES_PER_ENTRY) * 80;

        if (0 != max_index)
        {
            SYS_FTM_DBG_DUMP("  %-28s%-12d%-12d%-12d\n", tcam_key_id_str, key_size, max_index, used_tcam_entry);
        }
    }

    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");

    /* Show SRAM table alloc info */
    SYS_FTM_DBG_DUMP("\n-----------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Internal Sram table alloc info                                     \n");
    SYS_FTM_DBG_DUMP("------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  %-26s  %-10s  %-10s  %-10s\n",
                     "DynamicTable", "Memory", "AllocedSize", "Offset");
    SYS_FTM_DBG_DUMP("------------------------------------------------------------------\n");

    for (sram_type = 0; sram_type < SYS_FTM_SRAM_TBL_MAX; sram_type++)
    {
        _sys_greatbelt_ftm_get_sram_tbl_id(lchip, sram_type, sram_tbl_a, &share_tbl_num, sram_tbl_id_str);

         /*  SYS_FTM_DBG_DUMP("\n%-10s totoal size:%-12d\n", sram_tbl_id_str, entry_num);*/

        for (mem_id = SYS_FTM_SRAM0; mem_id < SYS_FTM_SRAM_MAX; mem_id++)
        {
            if (IS_BIT_SET(SYS_FTM_TBL_MEM_BITMAP(sram_type), mem_id) &&
                SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, mem_id))
            {
                SYS_FTM_DBG_DUMP("  %-26s  %-10d  %-10d   %-10d\n",
                                 sram_tbl_id_str, mem_id, SYS_FTM_TBL_MEM_ENTRY_NUM(sram_type, mem_id), SYS_FTM_TBL_MEM_START_OFFSET(sram_type, mem_id));
                sal_strcpy(sram_tbl_id_str, "");
            }
        }
    }

    /* Show Dyanamic table alloc info */
    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Pofile Type                     : %d\n", g_ftm_master[lchip]->profile_type);
    SYS_FTM_DBG_DUMP("  Global DsNexthop                : %d\n", g_ftm_master[lchip]->glb_nexthop_size);
    SYS_FTM_DBG_DUMP("  Global DsMet(Mcast Group Number): %d\n", g_ftm_master[lchip]->glb_met_size);
    SYS_FTM_DBG_DUMP("  Vsi                             : %d\n", g_ftm_master[lchip]->vsi_number);
    SYS_FTM_DBG_DUMP("-----------------------------------------------------------------------\n");

    return CTC_E_NONE;
}


int32
sys_greatbelt_ftm_tcam_cb_register(uint8 lchip, uint8 type, p_sys_ftm_tcam_cb_t cb)
{
    if (type >= SYS_FTM_TCAM_KEY_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    g_ftm_master[lchip]->tcam_cb[type] = cb;
    return CTC_E_NONE;
}

int32
_sys_greatbelt_ftm_tcam_cb(uint8 lchip, uint8 is_add)
{
    uint8 type = 0;
    for (type = 0; type < SYS_FTM_TCAM_KEY_MAX; type++)
    {
        if (g_ftm_master[lchip]->tcam_cb[type])
        {
            g_ftm_master[lchip]->tcam_cb[type](lchip, is_add);
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ftm_realloc_profile(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    uint8 key_idx           = 0;
    uint8 key_id            = 0;
    uint16 key_info_num     = 0;
    uint8 i = 0;
    uint32 tcam[][5]=
    {
        {DsAclIpv6Key0_t, DsIpv6Acl0Tcam_t, DsAclIpv6Key0_t, DsIpv6Acl0Tcam_t, DsIpv6Acl0Tcam_t},
        {DsAclIpv6Key1_t, DsIpv6Acl1Tcam_t, DsAclIpv6Key1_t, DsIpv6Acl1Tcam_t, DsIpv6Acl1Tcam_t},
        {DsAclMacKey0_t, DsAclIpv4Key0_t, DsAclMplsKey0_t, DsMacAcl0Tcam_t, DsIpv4Acl0Tcam_t},
        {DsAclMacKey1_t, DsAclIpv4Key1_t, DsAclMplsKey1_t, DsMacAcl1Tcam_t, DsIpv4Acl1Tcam_t},
        {DsAclMacKey2_t, DsAclIpv4Key2_t, DsAclMplsKey2_t, DsMacAcl2Tcam_t, DsIpv4Acl2Tcam_t},
        {DsAclMacKey3_t, DsAclIpv4Key3_t, DsAclMplsKey3_t, DsMacAcl3Tcam_t, DsIpv4Acl3Tcam_t}
    };


    /*tcam key*/
    key_info_num = profile_info->key_info_size;
    for (key_idx = 0; key_idx < key_info_num; key_idx++)
    {
        key_id = _sys_greatbelt_ftm_get_id(lchip, TRUE, profile_info->key_info[key_idx].key_id);
        if (key_id <= SYS_FTM_TCAM_TYPE_MAC_ACL_QOS3)/*only support to adjust ACL resource*/
        {
            SYS_FTM_ADD_KEY(key_id, profile_info->key_info[key_idx].key_size, profile_info->key_info[key_idx].max_key_index);
        }
        if(0 == profile_info->key_info[key_idx].max_key_index)
        {
            for (i = 0; i < 5; i++)
            {
                TABLE_MAX_INDEX(tcam[key_id][i])  = 0;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_adjust_tcam_resource(uint8 lchip, ctc_ftm_profile_info_t* profile_info)/*sys api*/
{
    uint8 i = 0;
    uint32 total_acl_size = 0;
    uint32 old_acl_size = 0;
    uint8 key_id            = 0;
    CTC_PTR_VALID_CHECK(profile_info);
    SYS_FTM_INIT_CHECK(lchip);
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "key_info_size = %d\n", profile_info->key_info_size);
    if(profile_info->key_info_size!=SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0)
    {
        return CTC_E_INVALID_CONFIG;
    }
    for (i = 0; i < profile_info->key_info_size; i++)
    {
        key_id = _sys_greatbelt_ftm_get_id(lchip, TRUE, profile_info->key_info[i].key_id);
        if (key_id <= SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3)/*only support to adjust ACL resource*/
        {
            total_acl_size += ((profile_info->key_info + i)->key_size) * ((profile_info->key_info + i)->max_key_index);
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "key_id = %d, key_size = %d, max_key_index = %d\n",
                            (profile_info->key_info + i)->key_id, (profile_info->key_info + i)->key_size, (profile_info->key_info + i)->max_key_index);
        }
    }
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "total_acl_size = %d\n", total_acl_size);
    for (key_id =  SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS0; key_id <= SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS3; key_id++)
    {
        old_acl_size += SYS_FTM_KEY_MAX_INDEX(key_id)*SYS_FTM_KEY_KEY_SIZE(key_id);
    }
    SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "old_acl_size = %d\n", old_acl_size);
    if (total_acl_size != old_acl_size)
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Total ACL entry number change, new num = %d, old num = %d\n", total_acl_size, old_acl_size);
        return CTC_E_INVALID_CONFIG;
    }

    g_ftm_master[lchip]->int_tcam_used_entry = 0;
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_tcam_cb(lchip, 0));
    CTC_ERROR_RETURN(_sys_greatebelt_ftm_opf_init_tcam(lchip, 1));
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_realloc_profile(lchip, profile_info));
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_set_tcam_mode(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_alloc_tcam(lchip));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_lkp_register_init(lchip));
    CTC_ERROR_RETURN(_sys_greatbelt_ftm_tcam_cb(lchip, 1));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ftm_adjust_tcam(uint8 lchip, uint32 acl_tcam_size[])/*cli test function*/
{
    uint8 key_id            = 0;
    ctc_ftm_key_info_t*  p_ftm_key_info = NULL;
    ctc_ftm_key_type_t ftm_key_type[] = {
        CTC_FTM_KEY_TYPE_IPV6_ACL0,         /**< [GB]Ipv6 ACL key */
        CTC_FTM_KEY_TYPE_IPV6_ACL1,         /**< [GB]Ipv6 ACL key */
        CTC_FTM_KEY_TYPE_ACL0,                 /**< [GB.GG]ACL key include MAC , IPV4, MPLS */
        CTC_FTM_KEY_TYPE_ACL1,                 /**< [GB.GG]ACL key include MAC , IPV4, MPLS */
        CTC_FTM_KEY_TYPE_ACL2,                 /**< [GB.GG]ACL key include MAC , IPV4, MPLS */
        CTC_FTM_KEY_TYPE_ACL3                 /**< [GB.GG]ACL key include MAC , IPV4, MPLS */
    };
    ctc_ftm_profile_info_t profile_info;
    int32 ret = CTC_E_NONE;

    sal_memset(&profile_info, 0, sizeof(ctc_ftm_profile_info_t));
    p_ftm_key_info = (ctc_ftm_key_info_t*)mem_malloc(MEM_FTM_MODULE, sizeof(ctc_ftm_key_info_t)*SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0);
    if (NULL == p_ftm_key_info)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_ftm_key_info, 0, sizeof(ctc_ftm_key_info_t)*SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0);

    profile_info.key_info = p_ftm_key_info;
    for (key_id =  0; key_id < SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0; key_id++)
    {
        p_ftm_key_info[key_id].key_id = ftm_key_type[key_id];
        p_ftm_key_info[key_id].key_size = (key_id>SYS_FTM_TCAM_TYPE_IPV6_ACL_QOS1)?4:8;
        p_ftm_key_info[key_id].max_key_index = acl_tcam_size[key_id];
    }
    profile_info.key_info_size = SYS_FTM_TCAM_TYPE_IPV4_ACL_QOS0;
    CTC_ERROR_GOTO(sys_greatbelt_ftm_adjust_tcam_resource(lchip, &profile_info), ret, out);

out:
    if (p_ftm_key_info)
    {
        mem_free(p_ftm_key_info);
    }

    return ret;
}


/*
 @file sys_goldengate_ftm.c

 @date 2011-11-16

 @version v2.0

 This file provides all sys alloc function
*/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_register.h"
#include "sys_goldengate_opf.h"

#include "sys_goldengate_ftm.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_register.h"

#include "goldengate/include/drv_ftm.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_chip_agent.h"

struct sys_ftm_master_s
{
    uint32 glb_met_size;
    uint32 glb_nexthop_size;

    uint8 fdb_conflict_num;                     /**[GG]FDB hash conflict entry*/
    uint8 ipmc_g_conflict_num;                  /**[GG]IPMC (*,G) hash conflict entry, mac entry +ipmc entry= 32*/

    uint8 nat_conflict_num;                     /**[GG]Nat da hash conflict entry*/
    uint8 ipmc_sg_conflict_num;                 /**[GG]IPMC (S,G) hash conflict entry, nat entry +ipmc entry= 32*/

    uint8 mpls_conflict_num;                    /**[GG]MPLS hash conflict entry*/
    uint8 scl_conflict_num;                     /**[GG]SCL, Tunnel hash conflict entry*/
    uint8 dcn_conflict_num;                     /**[GG]Vxlan, NvGre hash conflict entry,MPLS+SCL+DCN = 32*/

    uint8 oam_key_conflict_num;                 /**[GG]OAM key hash conflict entry*/
    uint8 vlan_xlate_conflict_num;              /**[GG]Egress Vlan translation hash conflict entry, OAM+xlate = 128*/
    uint8 profile_type;
    uint8 lpm0_tcam_share; /*ipuc v4&v6 share if set*/
    uint8 nexthop_share;  /* dsnexthop and dsmet share */
    uint32 profile_specs[CTC_FTM_SPEC_MAX];     /**[GG]record the max entry num for different modules*/
    sys_ftm_callback_t ftm_callback[CTC_FTM_SPEC_MAX];  /**[GG] for ftm show*/
    sys_ftm_rsv_hash_key_cb_t ftm_rsv_hash_key_cb[SYS_FTM_HASH_KEY_MAX];  /**[GG] add or remove reserved key*/
    uint16   sram_type[DRV_FTM_SRAM_MAX];  /**[GG] add or remove reserved key*/

    ctc_ftm_tbl_info_t   edit_table_info[2];

};
typedef struct sys_ftm_master_s sys_ftm_master_t;

sys_ftm_master_t* g_gg_sys_ftm[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_FTM_INIT_CHECK(lchip)                                 \
    do {                                                            \
        SYS_LCHIP_CHECK_ACTIVE(lchip);                            \
        if (NULL == g_gg_sys_ftm[lchip])                          \
        {                                                          \
            return CTC_E_NOT_INIT;                                 \
        }                                                          \
    } while(0)

enum sys_ftm_opf_type_e
{
    SYS_FTM_OPF_TYPE_FIB_AD,
    SYS_FTM_OPF_TYPE_IPDA_AD,
    SYS_FTM_OPF_TYPE_FLOW_AD,
    SYS_FTM_OPF_TYPE_SCL_AD,    /**<[GG] for DsTunnelId/DsUserId/DsSclFlow/DsMpls/DsTunnelIdRpf >*/
    SYS_FTM_OPF_TYPE_MAX
};
typedef enum sys_ftm_opf_type_e sys_ftm_opf_type_t;

#define SYS_FTM_DBG_DUMP(FMT, ...)       \
    if (1)                                    \
    {                                        \
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);        \
    }


#define NETWORK_CFG_FILE            "dut_topo.txt"

#if (SDK_WORK_PLATFORM == 1)
extern int32 swemu_environment_setting(char* filename);
extern int32 sim_model_init(uint32 um_emu_mode);
extern int32 sram_model_initialize(uint8 chip_id);
extern int32 tcam_model_initialize(uint8 chip_id);
extern int32 sram_model_allocate_sram_tbl_info(void);
 /*extern int32 sram_model_allocate_sram_tbl_info(void);*/
#endif

extern int32
drv_goldengate_ftm_reset_dynamic_table(uint8 lchip);

int32
_sys_goldengate_ftm_init_sim_module(uint8 lchip)
{
    int32 ret = 0;
    uint32 cmd = 0;
    uint32 device_info = 0;

    device_info = device_info;
    cmd = cmd;

    /*process only to avoid warning*/
    ret = ret;

#ifndef SDK_IN_VXWORKS
    /* software simulation platform,init  testing swemu environment*/
#if (SDK_WORK_ENV == 1 && SDK_WORK_PLATFORM == 1)
    {
        ret = swemu_environment_setting(NETWORK_CFG_FILE);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"swemu_environment_setting failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = sram_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret  = sram_model_allocate_sram_tbl_info();
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"cm_sim_cfg_kit_allocate_sram_tbl_info failed:%s@%d %s\n",  __FUNCTION__, __LINE__, ctc_get_error_desc(ret));
            return ret;
        }

        /* ret  = sram_model_allocate_sram_tbl_info();*/
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

        /* init cmodel device version ID */
        cmd = DRV_IOR(DevId_t, DevId_deviceId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &device_info));
        device_info |= 0x63;
        cmd = DRV_IOW(DevId_t, DevId_deviceId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &device_info));
    }
    /* software simulation platform,init memmodel*/
#elif (SDK_WORK_PLATFORM == 1)
    {
        ret = sram_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
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
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sram_model_initialize failed:%s@%d \n",  __FUNCTION__, __LINE__);
            return ret;
        }

        ret = tcam_model_initialize(lchip);
        if (ret != 0)
        {
            SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"tcam_model_init failed:%s@%d \n",  __FUNCTION__, __LINE__);
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

int32
sys_goldengate_ftm_lkp_register_init(uint8 lchip)
{
    CTC_ERROR_RETURN(drv_goldengate_ftm_lookup_ctl_init(lchip));
    CTC_ERROR_RETURN(drv_goldengate_ftm_set_edit_couple(lchip, (drv_ftm_tbl_info_t*)g_gg_sys_ftm[lchip]->edit_table_info));
    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_spec(uint8 lchip, uint32 specs_type)
{
    SYS_FTM_INIT_CHECK(lchip);
    if (CTC_FTM_SPEC_MAX <= specs_type)
    {
        return CTC_E_INVALID_PARAM;
    }

    return g_gg_sys_ftm[lchip]->profile_specs[specs_type];
}

int32
sys_goldengate_ftm_query_table_entry_num(uint8 lchip, uint32 table_id, uint32* entry_num)
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

extern int32
sys_goldengate_ftm_get_tcam_ad_index(uint8 lchip, uint32 key_tbl_id,  uint32 key_index)
{
    uint8 key_size = 0;
    uint8  per_entry_bytes = 0;
    bool is_lpm_key = FALSE;
    uint8 shift = 0;

    is_lpm_key = ((key_tbl_id == DsLpmTcamIpv440Key_t)|| (key_tbl_id == DsLpmTcamIpv6160Key0_t));
    per_entry_bytes =          (is_lpm_key)? DRV_LPM_KEY_BYTES_PER_ENTRY: DRV_BYTES_PER_ENTRY;
    key_size =  (TABLE_ENTRY_SIZE(key_tbl_id) / per_entry_bytes);
    shift =  (is_lpm_key)? 1:2;

    return (key_index*key_size) / shift;
}

int32
sys_goldengate_ftm_get_opf_type(uint8 lchip, uint32 table_id, uint32* p_opf_type)
{

    switch (table_id)
    {
    case DsMac_t:
        *p_opf_type = SYS_FTM_OPF_TYPE_FIB_AD;
        break;

    case DsIpDa_t:
    case DsTrillDa_t:
    case DsFcoeDa_t:
        *p_opf_type = SYS_FTM_OPF_TYPE_IPDA_AD;
        break;

    case DsAcl_t:
        *p_opf_type = SYS_FTM_OPF_TYPE_FLOW_AD;
        break;

    case DsMpls_t:
    case DsTunnelId_t:
    case DsUserId_t:
    case DsSclFlow_t:
    case DsTunnelIdRpf_t:
        *p_opf_type = SYS_FTM_OPF_TYPE_SCL_AD;
        break;
    default:
        SYS_FTM_DBG_DUMP("Not suport tbl id:%d now!!\r\n", table_id);
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_alloc_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32* offset)
{
    uint32 opf_type = 0;
    uint8  multi_alloc = 0;
    sys_goldengate_opf_t opf;

    CTC_PTR_VALID_CHECK(offset);

    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_opf_type(lchip, table_id, &opf_type));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = opf_type;
    opf.multiple  = multi_alloc;
    opf.reverse = dir;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, block_size, offset));

     /* SYS_FTM_DBG_DUMP("FTM: alloc tableId:%d block_size:%d, offset:%d\n", table_id, block_size, *offset);*/
    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_alloc_table_offset_from_position(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32 offset)
{
    uint32 opf_type = 0;
    uint8  multi_alloc = 0;
    sys_goldengate_opf_t opf;

    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_opf_type(lchip, table_id, &opf_type));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = opf_type;
    opf.multiple  = multi_alloc;
    opf.reverse = dir;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, block_size, offset));

     /* SYS_FTM_DBG_DUMP("FTM: alloc tableId:%d block_size:%d, offset:%d\n", table_id, block_size, *offset);*/
    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_free_table_offset(uint8 lchip, uint32 table_id, uint8 dir, uint32 block_size, uint32 offset)
{
    uint32 opf_type = 0;
    sys_goldengate_opf_t opf;

    CTC_DEBUG_ADD_STUB_CTC(NULL);
    if (table_id >= MaxTblId_t)
    {
        SYS_FTM_DBG_DUMP("unexpect tbl id:%d\r\n", table_id);
        return CTC_E_UNEXPECT;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_opf_type(lchip, table_id, &opf_type));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_FTM;
    opf.pool_index = opf_type;

    if (0 == dir)
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, block_size, offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, block_size, offset));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_dyn_entry_size(uint8 lchip, sys_ftm_dyn_entry_type_t type,
                                      uint32* p_size)
{
    SYS_FTM_INIT_CHECK(lchip);
    switch(type)
    {
    case SYS_FTM_DYN_ENTRY_GLB_MET:
        *p_size =  g_gg_sys_ftm[lchip]->glb_met_size;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_entry_num_by_size(uint8 lchip, uint32 tcam_key_type, uint32 size_type, uint32 * entry_num)
{
    return drv_goldengate_ftm_get_entry_num_by_size(lchip, tcam_key_type, size_type, entry_num);
}

int32
sys_goldengate_ftm_get_dynamic_table_info(uint8 lchip, uint32 tbl_id,
                                          uint8* dyn_tbl_idx,
                                          uint32* entry_enum,
                                          uint32* nh_glb_entry_num)
{
    SYS_FTM_INIT_CHECK(lchip);
    DRV_PTR_VALID_CHECK(dyn_tbl_idx);
    DRV_PTR_VALID_CHECK(entry_enum);
    DRV_PTR_VALID_CHECK(nh_glb_entry_num);

    *entry_enum = 0;
    *nh_glb_entry_num = 0;

    switch (tbl_id)
    {
        case DsFwd_t:
            *dyn_tbl_idx = SYS_FTM_DYN_NH0;
            break;

        case DsL2EditEth3W_t:
            *dyn_tbl_idx = SYS_FTM_DYN_NH1;
            break;

        case DsMetEntry3W_t:
            *dyn_tbl_idx = SYS_FTM_DYN_NH2;
            *nh_glb_entry_num = g_gg_sys_ftm[lchip]->glb_met_size;
            break;

        case DsNextHop4W_t:
            *dyn_tbl_idx = g_gg_sys_ftm[lchip]->nexthop_share? SYS_FTM_DYN_NH2 : SYS_FTM_DYN_NH3;
            *nh_glb_entry_num = g_gg_sys_ftm[lchip]->glb_nexthop_size;
            break;

        default:
            return DRV_E_INVAILD_TYPE;
    }

    if (DsFwd_t == tbl_id)
    {    /*dsfwd only need half entry*/
        *entry_enum  = (TABLE_MAX_INDEX(tbl_id)*2);
    }
    else
    {
        *entry_enum  = TABLE_MAX_INDEX(tbl_id);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ftm_get_opf_size(uint8 lchip, uint32 opf_type, uint32* entry_num)
{
    uint32 table_id = 0;

    CTC_PTR_VALID_CHECK(entry_num);

    switch (opf_type)
    {

        case SYS_FTM_OPF_TYPE_FIB_AD:
            table_id = DsMac_t;
            break;

        case SYS_FTM_OPF_TYPE_IPDA_AD:
            table_id = DsIpDa_t;
            break;

        case SYS_FTM_OPF_TYPE_FLOW_AD:
            table_id = DsAcl_t;
            break;

        case SYS_FTM_OPF_TYPE_SCL_AD:
            table_id = DsMpls_t;
            break;

        default:
            return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, table_id, entry_num));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ftm_opf_init_table_share(uint8 lchip)
{
    uint8 i = 0;
    uint32 entry_num = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_FTM, SYS_FTM_OPF_TYPE_MAX));

    for (i = SYS_FTM_OPF_TYPE_FIB_AD; i < SYS_FTM_OPF_TYPE_MAX; i++)
    {
        opf.pool_type = OPF_FTM;
        opf.pool_index = i;
        CTC_ERROR_RETURN(_sys_goldengate_ftm_get_opf_size(lchip, i, &entry_num));
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, entry_num));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_set_misc(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    SYS_FTM_INIT_CHECK(lchip);

    if(CTC_FTM_PROFILE_USER_DEFINE == profile_info->profile_type)
    {
        g_gg_sys_ftm[lchip]->glb_met_size     = profile_info->misc_info.mcast_group_number;
        g_gg_sys_ftm[lchip]->glb_nexthop_size = profile_info->misc_info.glb_nexthop_number;

        sal_memcpy(&g_gg_sys_ftm[lchip]->profile_specs[0], &profile_info->misc_info.profile_specs[0],
                    sizeof(uint32) * CTC_FTM_SPEC_MAX);
    }
    else
    {
        g_gg_sys_ftm[lchip]->glb_met_size     = 7 * 1024 * 2;
        g_gg_sys_ftm[lchip]->glb_nexthop_size = 16016;

        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_MAC]          = 48 * 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_IPUC_HOST]    = 8 * 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_IPUC_LPM]     = 12 * 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_IPMC]         = 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_ACL]          = 0xffffffff;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_SCL_FLOW]     = 512;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_ACL_FLOW]     = 0xffffffff;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_OAM]          = 2 * 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_SCL]          = 4 * 1024 + 512;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_TUNNEL]       = 3072;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_MPLS]         = 4 * 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_L2MC]         = 1024;
        g_gg_sys_ftm[lchip]->profile_specs[CTC_FTM_SPEC_IPFIX]        = 0xffffffff;
    }


    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* profile_info)
{
    drv_ftm_profile_info_t drv_profile;
    uint8 realloc  = 0;
    ctc_ftm_tbl_info_t   edit_info[2];
    uint32     ret = 0;

    CTC_PTR_VALID_CHECK(profile_info);
    if (NULL != g_gg_sys_ftm[lchip])
    {
        realloc = 1;
        if (0 == SDK_WORK_PLATFORM)
        {
            CTC_ERROR_RETURN(drv_goldengate_ftm_reset_dynamic_table(lchip));
        }

        CTC_ERROR_RETURN(sys_goldengate_opf_deinit(lchip, OPF_FTM));
        return DRV_E_NONE;
    }
    else
    {
        g_gg_sys_ftm[lchip] = mem_malloc(MEM_FTM_MODULE, sizeof(sys_ftm_master_t));

        if (NULL == g_gg_sys_ftm[lchip])
        {
            return DRV_E_NO_MEMORY;
        }
    }
    sal_memset(g_gg_sys_ftm[lchip], 0, sizeof(sys_ftm_master_t));
    sal_memset(&drv_profile, 0, sizeof(drv_ftm_profile_info_t));
    sal_memset(&edit_info, 0, sizeof(edit_info));

    /*set default cam*/
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_FDB]   = 16;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_MC]    = 16;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_TRILL] = 0;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST0_FCOE]  = 0;

    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST1_NAT]   = 16;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_FIB_HOST1_MC]    = 16;


    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_SCL]     = 0;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_DCN]     = 0;
    drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_MPLS]    = 32;

    if(CTC_FTM_PROFILE_USER_DEFINE == profile_info->profile_type)
    {
        if(profile_info->misc_info.profile_specs[CTC_FTM_SPEC_OAM])
        {
            drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_OAM]     = 128;
            drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_XC]      = 0;
        }
        else
        {
            drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_OAM]     = 0;
            drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_XC]      = 128;
        }
    }
    else
    {
        drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_OAM]     = 128;
        drv_profile.cam_info.conflict_cam_num[DRV_FTM_CAM_TYPE_XC]      = 0;
    }

    /*set profile*/
    drv_profile.profile_type    = profile_info->profile_type;
    g_gg_sys_ftm[lchip]->profile_type = profile_info->profile_type;

    drv_profile.key_info_size   = profile_info->key_info_size;
    //drv_profile.tbl_info_size   = profile_info->tbl_info_size;

    if(drv_profile.key_info_size && profile_info->key_info)
    {
        drv_profile.key_info = (drv_ftm_key_info_t*)profile_info->key_info;
    }

    if(profile_info->tbl_info_size && profile_info->tbl_info)
    {
        uint16 loop = 0;
        drv_profile.tbl_info = mem_malloc(MEM_FTM_MODULE, profile_info->tbl_info_size*sizeof(ctc_ftm_tbl_info_t));
        if(NULL == drv_profile.tbl_info)
        {
            return CTC_E_NO_MEMORY;
        }
        for(loop=0; loop < profile_info->tbl_info_size; loop++)
        {
            if(profile_info->tbl_info[loop].tbl_id == CTC_FTM_TBL_TYPE_L2_EDIT)
            {
                sal_memcpy(edit_info, profile_info->tbl_info+loop, sizeof(ctc_ftm_tbl_info_t));
            }
            else if(profile_info->tbl_info[loop].tbl_id == CTC_FTM_TBL_TYPE_EDIT)
            {
                sal_memcpy(edit_info+1, profile_info->tbl_info+loop, sizeof(ctc_ftm_tbl_info_t));
            }
            else
            {
                sal_memcpy(drv_profile.tbl_info+drv_profile.tbl_info_size, profile_info->tbl_info+loop, sizeof(ctc_ftm_tbl_info_t));
                drv_profile.tbl_info_size++;
            }
        }

        /*merge L2 & L3 edit resource*/
        drv_profile.tbl_info[drv_profile.tbl_info_size].tbl_id = CTC_FTM_TBL_TYPE_EDIT;
        drv_profile.tbl_info[drv_profile.tbl_info_size].mem_bitmap = edit_info[0].mem_bitmap|edit_info[1].mem_bitmap;
        for(loop=0; loop < CTC_FTM_SRAM_MAX; loop++)
        {
            drv_profile.tbl_info[drv_profile.tbl_info_size].mem_start_offset[loop] = edit_info[0].mem_start_offset[loop]+edit_info[1].mem_start_offset[loop];
            drv_profile.tbl_info[drv_profile.tbl_info_size].mem_entry_num[loop] = edit_info[0].mem_entry_num[loop]+edit_info[1].mem_entry_num[loop];
        }
        drv_profile.tbl_info_size++;
        sal_memcpy(g_gg_sys_ftm[lchip]->edit_table_info, edit_info, sizeof(edit_info));
    }
    g_gg_sys_ftm[lchip]->lpm0_tcam_share = 1;
    drv_profile.lpm0_tcam_share = g_gg_sys_ftm[lchip]->lpm0_tcam_share;
    /*init couple mode*/
    CTC_ERROR_GOTO(drv_goldengate_set_dynamic_ram_couple_mode(CTC_FLAG_ISSET(profile_info->flag, CTC_FTM_FLAG_COUPLE_EN)), ret, end);

    /*init ftm profile */
    CTC_ERROR_GOTO(drv_goldengate_ftm_mem_alloc(&drv_profile), ret, end);
    g_gg_sys_ftm[lchip]->nexthop_share = drv_profile.nexthop_share;

    /*init table share */
    CTC_ERROR_GOTO(_sys_goldengate_ftm_opf_init_table_share(lchip), ret, end);

    CTC_ERROR_GOTO(sys_goldengate_ftm_set_misc(lchip, profile_info), ret, end);

    /*init sim module */
    if (DRV_CHIP_AGT_MODE_CLIENT != drv_goldengate_chip_agent_mode())
    {
        /* just server need to init, and one server just have one cm */
        CTC_ERROR_GOTO(_sys_goldengate_ftm_init_sim_module(lchip), ret, end);
    }

    if(realloc)
    {
        CTC_ERROR_GOTO(sys_goldengate_ftm_lkp_register_init(lchip), ret, end);
    }

end:
    if(drv_profile.tbl_info)
    {
        mem_free(drv_profile.tbl_info);
    }
    return ret;

}

int32
sys_goldengate_ftm_mem_free(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_gg_sys_ftm[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_FTM);

    mem_free(g_gg_sys_ftm[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_register_callback(uint8 lchip, uint32 spec_type, sys_ftm_callback_t func)
{
    CTC_PTR_VALID_CHECK(g_gg_sys_ftm[lchip]);
    CTC_PTR_VALID_CHECK(func);

    if (spec_type >= CTC_FTM_SPEC_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    g_gg_sys_ftm[lchip]->ftm_callback[spec_type] = func;

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_register_rsv_key_cb(uint8 lchip, uint32 hash_key_type, sys_ftm_rsv_hash_key_cb_t func)
{
    CTC_PTR_VALID_CHECK(g_gg_sys_ftm[lchip]);
    CTC_PTR_VALID_CHECK(func);

    if (hash_key_type >= SYS_FTM_HASH_KEY_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    g_gg_sys_ftm[lchip]->ftm_rsv_hash_key_cb[hash_key_type] = func;

    return CTC_E_NONE;
}

int32 sys_goldengate_ftm_get_edit_resource(uint8 lchip, uint8* split_en, sys_ftm_edit_resource_t* p_edit_resource)
{
    *split_en = (g_gg_sys_ftm[lchip]->edit_table_info[0].mem_bitmap) ? 1 :0;
    if(*split_en == 0)
    {
        return CTC_E_NONE;
    }

    p_edit_resource->l2_edit_start = CTC_IS_BIT_SET(g_gg_sys_ftm[lchip]->edit_table_info[0].mem_bitmap, 14) ? 0 : \
        g_gg_sys_ftm[lchip]->edit_table_info[0].mem_entry_num[15];
    p_edit_resource->l2_edit_num = CTC_IS_BIT_SET(g_gg_sys_ftm[lchip]->edit_table_info[0].mem_bitmap, 14) ? \
        g_gg_sys_ftm[lchip]->edit_table_info[0].mem_entry_num[14] : \
        g_gg_sys_ftm[lchip]->edit_table_info[0].mem_entry_num[15];

    p_edit_resource->l3_edit_start = CTC_IS_BIT_SET(g_gg_sys_ftm[lchip]->edit_table_info[1].mem_bitmap, 14) ? 0 : \
        g_gg_sys_ftm[lchip]->edit_table_info[1].mem_entry_num[15];
    p_edit_resource->l3_edit_num = CTC_IS_BIT_SET(g_gg_sys_ftm[lchip]->edit_table_info[1].mem_bitmap, 14) ? \
        g_gg_sys_ftm[lchip]->edit_table_info[1].mem_entry_num[14] : \
        g_gg_sys_ftm[lchip]->edit_table_info[1].mem_entry_num[15];

    return CTC_E_NONE;
}


#define ____hash_poly____

int32
_sys_goldengate_ftm_get_current_hash_poly_type(uint8 lchip, uint32 mem_id, uint8 sram_type, uint32 *poly_type)
{
    uint32 type = 0;
    drv_goldengate_ftm_get_hash_poly(lchip, mem_id, sram_type, &type);
    switch (sram_type)
    {
        case DRV_FTM_TBL_FIB0_HASH_KEY:

            if (0 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_1;
            }
            else if (1 == type)
            {
               *poly_type = CTC_FTM_HASH_POLY_TYPE_2;
            }
            else if (2 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_5;
            }
            else if (3 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_6;
            }
            else if (4 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_7;
            }
            else if (5 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_9;
            }
            else if (6 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_10;
            }
            else if (7 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_12;
            }
            else if (8 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_13;
            }
            else if (9 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_11;
            }
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            if (0 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_4;
            }
            else if (1 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_3;
            }
            else if (2 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_5;
            }
            else if (3 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_6;
            }
            else if (4 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_10;
            }
            break;

        case DRV_FTM_TBL_FLOW_HASH_KEY:
            if (0 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_1;
            }
            else if (1 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_2;
            }
            else if (2 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_5;
            }
            else if (3 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_6;
            }
            else if (4 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_7;
            }
            else if (5 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_9;
            }
            else if (6 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_10;
            }
            else if (7 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_12;
            }
            else if (8 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_13;
            }
            else if (9 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_11;
            }
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            if (0 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_1;
            }
            else if (1 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_0;
            }
            else if (2 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_4;
            }
            else if (3 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_3;
            }
            else if (4 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_5;
            }
            else if (5 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_8;
            }
            else if (6 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_10;
            }
            else if (7 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_11;
            }
            break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            if (0 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_4;
            }
            else if (1 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_5;
            }
            else if (2 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_8;
            }
            else if (3 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_10;
            }
            else if (4 == type)
            {
                *poly_type = CTC_FTM_HASH_POLY_TYPE_11;
            }
            break;

        default :
            break;
    }

    return DRV_E_NONE;

}

int32
_sys_goldengate_ftm_get_hash_poly(uint8 lchip, uint8 sram_type, ctc_ftm_hash_poly_t* hash_poly)
{
    uint32 couple = 0;

    CTC_PTR_VALID_CHECK(hash_poly);

    /*get hash poly capablity*/
    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple));
    switch (sram_type)/*refer to _sys_goldengate_ftm_get_hash_type*/
    {
        case DRV_FTM_TBL_FIB0_HASH_KEY:
            if (DRV_FTM_SRAM4 == hash_poly->mem_id)
            {
                hash_poly->poly_num = 1;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_2 : CTC_FTM_HASH_POLY_TYPE_1;
            }
            else
            {
                hash_poly->poly_num = 4;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_10 : CTC_FTM_HASH_POLY_TYPE_5;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_11 : CTC_FTM_HASH_POLY_TYPE_6;
                hash_poly->poly_type[2] = couple? CTC_FTM_HASH_POLY_TYPE_12 : CTC_FTM_HASH_POLY_TYPE_7;
                hash_poly->poly_type[3] = couple? CTC_FTM_HASH_POLY_TYPE_13 : CTC_FTM_HASH_POLY_TYPE_9;
            }
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            if (DRV_FTM_SRAM0 == hash_poly->mem_id)
            {
                if (!couple)
                {
                    hash_poly->poly_num = 2;
                    hash_poly->poly_type[0] = CTC_FTM_HASH_POLY_TYPE_5;
                    hash_poly->poly_type[1] = CTC_FTM_HASH_POLY_TYPE_6;
                }
                else
                {
                    hash_poly->poly_num = 1;
                    hash_poly->poly_type[0] = CTC_FTM_HASH_POLY_TYPE_10;
                }
            }
            else
            {
                hash_poly->poly_num = 2;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_5 : CTC_FTM_HASH_POLY_TYPE_3;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_6 : CTC_FTM_HASH_POLY_TYPE_4;
            }
            break;

        case DRV_FTM_TBL_FLOW_HASH_KEY:
            if (DRV_FTM_SRAM4 == hash_poly->mem_id)
            {
                hash_poly->poly_num = 1;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_2 : CTC_FTM_HASH_POLY_TYPE_1;
            }
            else
            {
                hash_poly->poly_num = 4;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_10 : CTC_FTM_HASH_POLY_TYPE_5;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_11 : CTC_FTM_HASH_POLY_TYPE_6;
                hash_poly->poly_type[2] = couple? CTC_FTM_HASH_POLY_TYPE_12 : CTC_FTM_HASH_POLY_TYPE_7;
                hash_poly->poly_type[3] = couple? CTC_FTM_HASH_POLY_TYPE_13 : CTC_FTM_HASH_POLY_TYPE_9;
            }
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            if ((DRV_FTM_SRAM2 == hash_poly->mem_id) || (DRV_FTM_SRAM3 == hash_poly->mem_id))
            {
                hash_poly->poly_num = 2;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_10 : CTC_FTM_HASH_POLY_TYPE_5;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_11 : CTC_FTM_HASH_POLY_TYPE_8;
            }
            else if ((DRV_FTM_SRAM11 == hash_poly->mem_id) || (DRV_FTM_SRAM12 == hash_poly->mem_id))
            {
                hash_poly->poly_num = 2;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_3 : CTC_FTM_HASH_POLY_TYPE_0;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_4 : CTC_FTM_HASH_POLY_TYPE_1;
            }
            break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            if (DRV_FTM_SRAM8 == hash_poly->mem_id)
            {
                if (!couple)
                {
                    hash_poly->poly_num = 1;
                    hash_poly->poly_type[0] = CTC_FTM_HASH_POLY_TYPE_4;
                }
                else
                {
                    hash_poly->poly_num = 2;
                    hash_poly->poly_type[0] = CTC_FTM_HASH_POLY_TYPE_5;
                    hash_poly->poly_type[1] = CTC_FTM_HASH_POLY_TYPE_8;
                }
            }
            else
            {
                hash_poly->poly_num = 2;
                hash_poly->poly_type[0] = couple? CTC_FTM_HASH_POLY_TYPE_10 : CTC_FTM_HASH_POLY_TYPE_5;
                hash_poly->poly_type[1] = couple? CTC_FTM_HASH_POLY_TYPE_11 : CTC_FTM_HASH_POLY_TYPE_8;
            }
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_ftm_mapping_drv_hash_poly_type(uint8 sram_type, uint32 type, uint32* p_poly)
{
    switch (sram_type)
    {
        case DRV_FTM_TBL_FIB0_HASH_KEY:

            if (CTC_FTM_HASH_POLY_TYPE_1 == type)
            {
                *p_poly = 0;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_2 == type)
            {
                *p_poly = 1;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_5 == type)
            {
                *p_poly = 2;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_6 == type)
            {
                *p_poly = 3;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_7 == type)
            {
                *p_poly = 4;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_9 == type)
            {
                *p_poly = 5;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_10 == type)
            {
                *p_poly = 6;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_12 == type)
            {
                *p_poly = 7;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_13 == type)
            {
                *p_poly = 8;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_11 == type)
            {
                *p_poly = 9;
            }
            break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
            if (CTC_FTM_HASH_POLY_TYPE_4 == type)
            {
                *p_poly = 0;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_3 == type)
            {
                *p_poly = 1;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_5 == type)
            {
                *p_poly = 2;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_6 == type)
            {
                *p_poly = 3;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_10 == type)
            {
                *p_poly = 4;
            }
            break;

        case DRV_FTM_TBL_FLOW_HASH_KEY:
            if (CTC_FTM_HASH_POLY_TYPE_1 == type)
            {
                *p_poly = 0;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_2 == type)
            {
                *p_poly = 1;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_5 == type)
            {
                *p_poly = 2;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_6 == type)
            {
                *p_poly = 3;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_7 == type)
            {
                *p_poly = 4;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_9 == type)
            {
                *p_poly = 5;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_10 == type)
            {
                *p_poly = 6;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_12 == type)
            {
                *p_poly = 7;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_13 == type)
            {
                *p_poly = 8;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_11 == type)
            {
                *p_poly = 9;
            }
            break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
            if (CTC_FTM_HASH_POLY_TYPE_1 == type)
            {
                *p_poly = 0;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_0 == type)
            {
                *p_poly = 1;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_4 == type)
            {
                *p_poly = 2;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_3 == type)
            {
                *p_poly = 3;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_5 == type)
            {
                *p_poly = 4;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_8 == type)
            {
                *p_poly = 5;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_10 == type)
            {
                *p_poly = 6;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_11 == type)
            {
                *p_poly = 7;
            }
            break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
            if (CTC_FTM_HASH_POLY_TYPE_4 == type)
            {
                *p_poly = 0;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_5 == type)
            {
                *p_poly = 1;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_8 == type)
            {
                *p_poly = 2;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_10 == type)
            {
                *p_poly = 3;
            }
            else if (CTC_FTM_HASH_POLY_TYPE_11 == type)
            {
                *p_poly = 4;
            }
            break;

        default :
            break;
    }

    return DRV_E_NONE;
}

int32
_sys_goldengate_ftm_check_hash_mem_empty(uint8 lchip, uint8 sram_type)
{
    uint8 step = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint32 mem_entry_num = 0;
    uint32 loop =0 ;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;

    switch(sram_type)
    {
        case DRV_FTM_TBL_FIB0_HASH_KEY:
        tbl_id = DsFibHost0FcoeHashKey_t;
        fld_id = DsFibHost0FcoeHashKey_valid_f;
        step = 1;
        break;

        case DRV_FTM_TBL_FIB1_HASH_KEY:
        tbl_id = DsFibHost1FcoeRpfHashKey_t;
        fld_id = DsFibHost1FcoeRpfHashKey_valid_f;
        step = 1;
        break;

        case DRV_FTM_TBL_FLOW_HASH_KEY:
        tbl_id = DsFlowL2HashKey_t;
        fld_id = DsFlowL2HashKey_hashKeyType_f;
        step = 2;
        break;

        case DRV_FTM_TBL_TYPE_SCL_HASH_KEY:
        tbl_id = DsUserIdCvlanCosPortHashKey_t;
        fld_id = DsUserIdCvlanCosPortHashKey_valid_f;
        step = 1;
        break;

        case DRV_FTM_TBL_XCOAM_HASH_KEY:
        tbl_id = DsEgressXcOamBfdHashKey_t;
        fld_id = DsEgressXcOamBfdHashKey_valid_f;
        step = 2;
        break;
        default :
        break;
    }
    mem_entry_num = TABLE_MAX_INDEX(tbl_id);
    cmd = DRV_IOR(tbl_id, fld_id);
    for (loop = 0; loop < mem_entry_num; loop += step)
    {
        DRV_IOCTL(lchip, loop, cmd, &value);
        if (value)
        {
            return CTC_E_ENTRY_EXIST;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ftm_set_rsv_hash_key(uint8 lchip, uint8 is_add)
{
    uint8 i = 0;
    for (i = 0; i < SYS_FTM_HASH_KEY_MAX; i++)
    {
        if (NULL != g_gg_sys_ftm[lchip]->ftm_rsv_hash_key_cb[i])
        {
            g_gg_sys_ftm[lchip]->ftm_rsv_hash_key_cb[i](lchip, is_add);
        }
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_hash_poly(uint8 lchip, ctc_ftm_hash_poly_t* hash_poly)
{

    uint8 sram_type = 0;

    SYS_FTM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(hash_poly);

    drv_goldengate_ftm_get_sram_type( hash_poly->mem_id, &sram_type);
    if ((DRV_FTM_TBL_IPFIX_HASH_KEY == sram_type) || (DRV_FTM_TBL_TYPE_MAX == sram_type))
    {
        SYS_FTM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " API or some feature is not supported \n");
        return CTC_E_NOT_SUPPORT;

    }

    /*get current hash poly*/
    _sys_goldengate_ftm_get_current_hash_poly_type(lchip, hash_poly->mem_id, sram_type, &hash_poly->cur_poly_type);/*get enum type from asic type*/

    _sys_goldengate_ftm_get_hash_poly(lchip, sram_type, hash_poly);

    return CTC_E_NONE;
}


int32
sys_goldengate_ftm_set_hash_poly(uint8 lchip, ctc_ftm_hash_poly_t* hash_poly)
{

    uint32 poly_type = 0;
    uint8 sram_type = 0;

    uint32 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_ftm_hash_poly_t poly_capabilty;

    SYS_FTM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(hash_poly);
    sal_memset(&poly_capabilty, 0, sizeof(ctc_ftm_hash_poly_t));
    poly_capabilty.mem_id = hash_poly->mem_id;


    drv_goldengate_ftm_get_sram_type(hash_poly->mem_id, &sram_type);
    if((DRV_FTM_TBL_IPFIX_HASH_KEY == sram_type) || (DRV_FTM_TBL_TYPE_MAX == sram_type))
    {
        return CTC_E_NOT_SUPPORT;
    }
    /*get hash poly capabilty*/
    _sys_goldengate_ftm_get_hash_poly(lchip, sram_type, &poly_capabilty);
    for (loop = 0; loop < poly_capabilty.poly_num; loop++)
    {
        if (hash_poly->cur_poly_type == poly_capabilty.poly_type[loop])
        {
            break;
        }
    }
    if (loop >= poly_capabilty.poly_num)
    {
        return CTC_E_INVALID_PARAM;
    }

    _sys_goldengate_ftm_set_rsv_hash_key(lchip, 0);/*remove reserved hash key*/
    ret = _sys_goldengate_ftm_check_hash_mem_empty(lchip, sram_type);
    if(ret < 0)
    {
        goto End;
    }
    _sys_goldengate_ftm_mapping_drv_hash_poly_type(sram_type, hash_poly->cur_poly_type, &poly_type);
    drv_goldengate_ftm_set_hash_poly(lchip, hash_poly->mem_id, sram_type, poly_type);

End:
    _sys_goldengate_ftm_set_rsv_hash_key(lchip, 1);/*add reserved hash key*/
    return ret;
}

int32
sys_goldengate_ftm_set_profile_specs(uint8 lchip, uint32 spec_type, uint32 value)
{
    uint32 cmd = 0;
    uint32 spec_fdb = 0;
    uint32 global_capability_type = CTC_GLOBAL_CAPABILITY_MAX;

    SYS_FTM_INIT_CHECK(lchip);

    if (spec_type >= CTC_FTM_SPEC_MAX || spec_type == CTC_FTM_SPEC_INVALID)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (spec_type == CTC_FTM_SPEC_MAC)
    {
        value = (value > 0x7FFFF) ? 0x7FFFF : value;
        spec_fdb = value;
        cmd = DRV_IOW(MacLimitSystem_t, MacLimitSystem_profileMaxCount_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &spec_fdb));
    }

    g_gg_sys_ftm[lchip]->profile_specs[spec_type] = value;

    switch (spec_type)
    {
        case CTC_FTM_SPEC_TUNNEL:
            global_capability_type = CTC_GLOBAL_CAPABILITY_TUNNEL_ENTRY_NUM;
            break;
        case CTC_FTM_SPEC_MPLS:
            global_capability_type = CTC_GLOBAL_CAPABILITY_MPLS_ENTRY_NUM;
            break;

        case CTC_FTM_SPEC_MAC:
            global_capability_type = CTC_GLOBAL_CAPABILITY_MAC_ENTRY_NUM;
            break;

        case CTC_FTM_SPEC_IPMC:
            global_capability_type = CTC_GLOBAL_CAPABILITY_IPMC_ENTRY_NUM;
            break;

        case CTC_FTM_SPEC_IPUC_HOST:
            global_capability_type = CTC_GLOBAL_CAPABILITY_HOST_ROUTE_ENTRY_NUM;
            break;

        default:
            break;
    }

    if (CTC_GLOBAL_CAPABILITY_MAX != global_capability_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, global_capability_type,
                                                                   g_gg_sys_ftm[lchip]->profile_specs[spec_type]));
    }


    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_profile_specs(uint8 lchip, uint32 spec_type, uint32* value)
{
    CTC_PTR_VALID_CHECK(value);
    SYS_FTM_INIT_CHECK(lchip);

    if (spec_type >= CTC_FTM_SPEC_MAX || spec_type == CTC_FTM_SPEC_INVALID)
    {
        return CTC_E_INVALID_PARAM;
    }

    *value = g_gg_sys_ftm[lchip]->profile_specs[spec_type];

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_get_lpm0_share_mode(uint8 lchip)
{
    return g_gg_sys_ftm[lchip]->lpm0_tcam_share;
}

int32
sys_goldengate_ftm_get_nexthop_share_mode(uint8 lchip)
{
    return g_gg_sys_ftm[lchip]->nexthop_share;
}


#define _____Show_____

STATIC int32
_sys_goldengate_ftm_show_specs(uint8 lchip)
{
    uint32 specs_type = 0;
    sys_ftm_specs_info_t specs_info;
    int32 ret = CTC_E_NONE;
    char* arr_name[][2] =
       {
            {"",""},
            {"MAC","80"},
            {"IP Host","80"},
            {"IP LPM", "80"},
            {"IPMC","80"},
            {"ACL","160"},
            {"Scl Flow","80"},
            {"Acl Flow","160"},
            {"Oam","160"},
            {"Scl","80"},
            {"Tunnel Decp","80"},
            {"MPLS","80"},
            {"L2Mcast Group",""},
            {"Ipfix","160"},
            {"",""},
       };

    SYS_FTM_DBG_DUMP("\n-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("FTM SPEC  (H-Hash/T-Tcam)                  \n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("%-15s%-10s%-10s%-10s%-5s\n",
                     "SPEC_NAME", "KEY_SIZE", "MAX_SIZE", "COUNT", "RAM_TYPE");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    for (specs_type = CTC_FTM_SPEC_MAC; specs_type < CTC_FTM_SPEC_MAX; ++ specs_type)
    {
        if (NULL != g_gg_sys_ftm[lchip]->ftm_callback[specs_type])
        {
            /*
                for CTC_FTM_SPEC_IPFIX, it can learn by hardware, so
                just show the max-entry-number which it can reach
            */
            specs_info.used_size = 0;
            ret = g_gg_sys_ftm[lchip]->ftm_callback[specs_type](lchip, &specs_info);
            if (ret < 0)
            {
                ret = CTC_E_NONE;
                continue;
            }

            if (0xffffffff == g_gg_sys_ftm[lchip]->profile_specs[specs_type])
            {
                SYS_FTM_DBG_DUMP("%-15s%-10s%-10s%-10d%-5s\n",
                    arr_name[specs_type][0], arr_name[specs_type][1],
                    "-", specs_info.used_size, (CTC_FTM_SPEC_ACL != specs_type) ? "H" : "T");
            }
            else
            {
                SYS_FTM_DBG_DUMP("%-15s%-10s%-10u%-10d%-5s\n",
                    arr_name[specs_type][0], arr_name[specs_type][1],
                    g_gg_sys_ftm[lchip]->profile_specs[specs_type], specs_info.used_size,
                    (CTC_FTM_SPEC_ACL != specs_type) ? "H" : "T");
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ftm_show_global_info(uint8 lchip)
{
    SYS_FTM_DBG_DUMP("\n-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Global Config Information                                     \n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("  Global DsNexthop                : %d\n", g_gg_sys_ftm[lchip]->glb_nexthop_size);
    SYS_FTM_DBG_DUMP("  Mcast Group Number              : %d\n", g_gg_sys_ftm[lchip]->glb_met_size/2);
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_show_alloc_info(uint8 lchip)
{
    SYS_FTM_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_goldengate_ftm_show_specs(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_ftm_show_global_info(lchip));

    return CTC_E_NONE;
}


int32
sys_goldengate_ftm_show_cam_info(uint8 lchip)
{
    uint8 cam_type = 0;
    drv_ftm_info_detail_t ftm_info;
    char *arr_cam_name[] =
    {
        "Invalid",
        "FibHost0Fdb",
        "FibHost0Mc",
        "FibHost1Nat",
        "FibHost1Mc",
        "Scl",
        "Dcn",
        "Mpls",
        "Oam",
        "Xc",
        "TRILL",
        "FCoE",
    };
    SYS_FTM_INIT_CHECK(lchip);
    sal_memset(&ftm_info, 0, sizeof(drv_ftm_info_detail_t));

    /* Show SRAM table alloc info */
    SYS_FTM_DBG_DUMP("\n-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("Internal Cam alloc info                                     \n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("%-26s  %-10s  %-10s \n",
                     "CAM NAME", "TATOL_SIZE", "USED_SIZE");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    ftm_info.info_type = DRV_FTM_INFO_TYPE_CAM;

    for (cam_type = DRV_FTM_CAM_TYPE_FIB_HOST0_FDB; cam_type < DRV_FTM_CAM_TYPE_MAX; ++ cam_type)
    {
        ftm_info.tbl_type = cam_type;
        drv_goldengate_ftm_get_info_detail(&ftm_info);
        SYS_FTM_DBG_DUMP("%-26s  %-10d  %-10d\n",
                arr_cam_name[cam_type], ftm_info.max_size, ftm_info.used_size);
    }

    return DRV_E_NONE;
}

int32
sys_goldengate_ftm_show_edram_info(uint8 lchip)
{
    uint32 sram_type            = 0;
    drv_ftm_info_detail_t ftm_info;
    uint32 loop = 0;

    SYS_FTM_INIT_CHECK(lchip);
    sal_memset(&ftm_info, 0, sizeof(drv_ftm_info_detail_t));

    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    /* Show SRAM table alloc info */
    SYS_FTM_DBG_DUMP("\n-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("Internal eDram table alloc info                                     \n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("%-20s%-10s%-10s%-10s%-10s\n",
                     "DYNAMIC_TABLE", "MEMORY", "ALLOCED", "OFFSET", "(UNIT: 80b)");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    ftm_info.info_type = DRV_FTM_INFO_TYPE_EDRAM;

    for (sram_type = 0; sram_type < DRV_FTM_TBL_TYPE_MAX; sram_type++)
    {
        if ((DRV_FTM_TBL_TYPE_FIB_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_TYPE_LPM_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_TYPE_FIB_HASH_AD == sram_type)
            || (DRV_FTM_TBL_TYPE_LPM_PIPE1 == sram_type)
            || (DRV_FTM_TBL_TYPE_LPM_PIPE2 == sram_type)
            || (DRV_FTM_TBL_TYPE_LPM_PIPE3 == sram_type)
            || (DRV_FTM_TBL_TYPE_MPLS == sram_type)
            || (DRV_FTM_TBL_TYPE_STATS == sram_type)
            || (DRV_FTM_TBL_TYPE_OAM_MEP == sram_type))
        {
            continue;
        }

        ftm_info.tbl_type = sram_type;
        drv_goldengate_ftm_get_info_detail(&ftm_info);
        for (loop = 0; loop < ftm_info.tbl_entry_num; ++loop)
        {
            SYS_FTM_DBG_DUMP("%-20s%-10d%-10d%-10d\n",
                                 ftm_info.str, ftm_info.mem_id[loop], ftm_info.entry_num[loop],
                                 ftm_info.offset[loop]);
            sal_strcpy(ftm_info.str, "");
        }

    }



    return DRV_E_NONE;

}

int32
sys_goldengate_ftm_show_tcam_info(uint8 lchip)
{
    uint8 tcam_key_type     = 0;
    drv_ftm_info_detail_t ftm_info;
    uint8 loop = 0;

    SYS_FTM_INIT_CHECK(lchip);
    sal_memset(&ftm_info, 0, sizeof(drv_ftm_info_detail_t));

    /* Show internal Tcam alloc info */
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("TCAM alloc info\n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("%-10s%-25s%-12s%-12s\n",
                    "TCAM_TBL", "KEY_NAME", "KEY_SIZE", "MAX_INDEX");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    ftm_info.info_type = DRV_FTM_INFO_TYPE_TCAM;

    for (tcam_key_type = 0; tcam_key_type < DRV_FTM_KEY_TYPE_MAX; tcam_key_type++)
    {
        if ((DRV_FTM_KEY_TYPE_IPV6_ACL0 == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_ACL1 == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV4_MCAST == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_MCAST == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_VLAN_SCL == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_MAC_SCL == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV4_SCL == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_SCL == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_FDB == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV4_TUNNEL == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_TUNNEL == tcam_key_type))
        {
            /*
                Just for GB, GG won't do anything
            */
            continue;
        }
        if ((DRV_FTM_KEY_TYPE_IPV6_UCAST == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_NAT == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV4_PBR == tcam_key_type)
            || (DRV_FTM_KEY_TYPE_IPV6_PBR) == tcam_key_type)
        {
            /*
                LPM0 and LPM1 just show once
            */
            continue;
        }

        ftm_info.tbl_type = tcam_key_type;
        drv_goldengate_ftm_get_info_detail(&ftm_info);
        for (loop = 0; loop < ftm_info.tbl_entry_num; ++ loop)
        {
            if (0 != ftm_info.key_size[loop])
            {
                SYS_FTM_DBG_DUMP("%-10s%-25s%-12d%-12d\n",
                ftm_info.str,TABLE_NAME(ftm_info.tbl_id[loop]),
                    ftm_info.key_size[loop], ftm_info.max_idx[loop]);

            }
            else
            {
                SYS_FTM_DBG_DUMP("%-10s%-25s%-12s%-12s\n",
                ftm_info.str,TABLE_NAME(ftm_info.tbl_id[loop]),
                    "", "");
            }
            sal_strcpy(ftm_info.str, "");
        }
    }
    return DRV_E_NONE;
}

int32
sys_goldengate_ftm_show_hash_poly_info(uint8 lchip)
{
    uint32 sram_type = 0;
    drv_ftm_info_detail_t ftm_info;
    ctc_ftm_hash_poly_t hash_poly;
    uint32 loop = 0;
    uint32 loop1 = 0;

    SYS_FTM_INIT_CHECK(lchip);
    sal_memset(&ftm_info, 0, sizeof(drv_ftm_info_detail_t));
    sal_memset(&hash_poly, 0, sizeof(ctc_ftm_hash_poly_t));

    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    /* Show hash poly alloc info */
    SYS_FTM_DBG_DUMP("\n-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("Hash poly alloc info\n");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");
    SYS_FTM_DBG_DUMP("%-20s%-10s%-15s%-10s\n",
                     "DYNAMIC_TABLE", "MEMORY", "CURRENT_TYPE", "HASH_CAPABILITY");
    SYS_FTM_DBG_DUMP("-------------------------------------------------------------\n");

    ftm_info.info_type = DRV_FTM_INFO_TYPE_EDRAM;

    for (sram_type = 0; sram_type < DRV_FTM_TBL_TYPE_MAX; sram_type++)            /*loop for function*/
    {
        if ((DRV_FTM_TBL_FIB0_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_FIB1_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_FLOW_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_TYPE_SCL_HASH_KEY == sram_type)
            || (DRV_FTM_TBL_XCOAM_HASH_KEY == sram_type))
        {
            ftm_info.tbl_type = sram_type;
            drv_goldengate_ftm_get_info_detail(&ftm_info);
            for (loop = 0; loop < ftm_info.tbl_entry_num; ++loop)        /*loop for mem_id*/
            {
                sal_memset(&hash_poly, 0, sizeof(ctc_ftm_hash_poly_t));
                hash_poly.mem_id = ftm_info.mem_id[loop];
                sys_goldengate_ftm_get_hash_poly(lchip, &hash_poly);
                SYS_FTM_DBG_DUMP("%-20s%-10d%-15d",ftm_info.str, ftm_info.mem_id[loop],hash_poly.cur_poly_type);
                for (loop1 = 0; loop1 < hash_poly.poly_num; loop1++)             /*loop for showing hash ability*/
                {
                    SYS_FTM_DBG_DUMP("%d ",hash_poly.poly_type[loop1]);
                }
                SYS_FTM_DBG_DUMP("\n");
                sal_strcpy(ftm_info.str, "");
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_show_detail(uint8 lchip)
{
    SYS_FTM_INIT_CHECK(lchip);
    sys_goldengate_ftm_show_edram_info(lchip);
    sys_goldengate_ftm_show_hash_poly_info(lchip);
    sys_goldengate_ftm_show_tcam_info(lchip);
    sys_goldengate_ftm_show_cam_info(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_ftm_show_alloc_info_detail(uint8 lchip)
{
    SYS_FTM_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(_sys_goldengate_ftm_show_specs(lchip));
    CTC_ERROR_RETURN(sys_goldengate_ftm_show_detail(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_ftm_show_global_info(lchip));

    return CTC_E_NONE;
}


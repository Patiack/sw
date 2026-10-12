/**
 @file sys_goldengate_mpls.c

 @date 2010-03-12

 @version v2.0

 The file contains all mpls related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"

#include "ctc_mpls.h"
#include "ctc_vector.h"
#include "ctc_stats.h"
#include "ctc_spool.h"

#include "sys_goldengate_opf.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_mpls.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_l3if.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_io.h"


/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
#define SYS_MPLS_TBL_BLOCK_SIZE         64
#define SYS_MPLS_MAX_L3VPN_VRF_ID       0xBFFE
#define SYS_MPLS_VPLS_SRC_PORT_NUM      0xFFF
#define SYS_MPLS_MAX_LABEL              0XFFFFF
#define SYS_MPLS_MAX_OAM_CHK_TYPE       0x5
#define SYS_MPLS_MAX_DOMAIN             7
#define SYS_MPLS_INNER_ROUTE_MAC_NUM    64

#define SYS_MPLS_INIT_CHECK(lchip) \
    do \
    { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_mpls_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_MPLS_ILM_KEY_CHECK(p_ilm)\
    do\
    {\
        if ((p_ilm)->label > SYS_MPLS_MAX_LABEL){   \
            return CTC_E_INVALID_PARAM; }\
    } while(0)

#define SYS_MPLS_DBG_DUMP(FMT, ...)                      \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT_INFO(mpls, mpls, MPLS_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DUMP(FMT, ...)                    \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT(mpls, mpls, MPLS_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(mpls, mpls, MPLS_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DUMP_ILM(p_mpls_ilm) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_ilm :: nh_id = %d, spaceid = %d , label = %d,\n" \
                         "    id_type = %d, 0-NULL,1-FLOW,2-VRF,3-SERVICE,4-APS,5-STATS,6-MAX,\n" \
                         "    label type = %d, 0-NORMAL LABEL,1-L3VPN LABEL,2-VPWS,3-VPLS,4-MAX,\n" \
                         "    tunnel model = %d, 0-UNIFORM,1-SHORT PIPE,2-PIPE,3-MAX,\n" \
                         "    pop = %d, cwen = %d, aps_select_protect_path = %d,  oam_en = %d,\n" \
                         "    pwid = %d, logic_port_type = %d,\n" \
                         "    flw_vrf_srv_aps = %d, flow_id   vrf_id   service_id   aps_select_grp_id\n\n", \
                         p_mpls_ilm->nh_id, p_mpls_ilm->spaceid, p_mpls_ilm->label, \
                         p_mpls_ilm->id_type, \
                         p_mpls_ilm->type, \
                         p_mpls_ilm->model, \
                         p_mpls_ilm->pop, p_mpls_ilm->cwen, p_mpls_ilm->aps_select_protect_path, p_mpls_ilm->oam_en, \
                         p_mpls_ilm->pwid, p_mpls_ilm->logic_port_type, \
                         p_mpls_ilm->flw_vrf_srv_aps.flow_id); \
    } while (0)

#define SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_info :: key_offset = %d, spaceid = %d ,\n" \
                         "label = %d,nh_id = %d,pop = %d\n" \
                         "    id_type = %d, 0x0-NULL,0x1-FLOW,0x2-VRF,0x4-SERVICE,0x8-APS_SELECT,0x10-STATS,0xFFFF-MAX,\n" \
                         "    label type = %d, 0-NORMAL LABEL,1-L3VPN LABEL,2-VPWS,3-VPLS,4-MAX,\n" \
                         "    tunnel model = %d, 0-UNIFORM,1-SHORT PIPE,2-PIPE,3-MAX,\n" \
                         "    pwid = %d, logic_port_type = %d,\n" \
                         "    fid = %d, pw_mode = %d  0-Tagged PW    1-Raw PW,\n" \
                         "    learn_disable = %d, igmp_snooping_enable =%d, maclimit_enable=%d, service_aclqos_enable = %d,\n" \
                         "    cwen = %d, oam_en = %d,stats_ptr = %d,\n" \
                         "    flw_vrf_srv = %d, aps_group_id = %d, aps_select_ppath =%d\n\n", \
                         p_mpls_info->key_offset, p_mpls_info->spaceid, p_mpls_info->label, p_mpls_info->nh_id, p_mpls_info->pop, \
                         p_mpls_info->id_type, \
                         p_mpls_info->type, \
                         p_mpls_info->model, \
                         p_mpls_info->logic_src_port, p_mpls_info->logic_port_type, \
                         p_mpls_info->fid, p_mpls_info->pw_mode, \
                         p_mpls_info->learn_disable, p_mpls_info->igmp_snooping_enable, p_mpls_info->maclimit_enable, p_mpls_info->service_aclqos_enable, \
                         p_mpls_info->cwen, p_mpls_info->oam_en, p_mpls_info->stats_ptr, \
                         p_mpls_info->mpls_flow_policer_ptr, p_mpls_info->aps_group_id,  p_mpls_info->aps_select_ppath); \
    } while (0)

#define SYS_MPLS_DUMP_VPWS_PARAM(p_mpls_pw) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_pw :: label = %d, l2vpntype = %d,\n" \
                         "    pw_mode = %d,  \n" \
                         "    learn_disable = %d, maclimit_enable = %d, service_aclqos_enable = %d, igmp_snooping_enable = %d\n" \
                         "    pw_nh_id = %d\n\n", \
                         p_mpls_pw->label, p_mpls_pw->l2vpntype, p_mpls_pw->pw_mode, \
                         p_mpls_pw->learn_disable, p_mpls_pw->maclimit_enable, p_mpls_pw->service_aclqos_enable, p_mpls_pw->igmp_snooping_enable, \
                         p_mpls_pw->u.pw_nh_id); \
    } while (0)

#define SYS_MPLS_DUMP_VPLS_PARAM(p_mpls_pw) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_pw :: label = %d, l2vpntype = %d,\n" \
                         "    pw_mode = %d,  \n" \
                         "    learn_disable = %d, maclimit_enable = %d, service_aclqos_enable = %d, igmp_snooping_enable = %d\n" \
                         "    fid = %d, logic_src_port = %d, " \
                         "    logic_port_type= %d 0-H_VPLS 1-Normal_VPLS \n\n", \
                         p_mpls_pw->label, p_mpls_pw->l2vpntype, p_mpls_pw->pw_mode, \
                         p_mpls_pw->learn_disable, p_mpls_pw->maclimit_enable, p_mpls_pw->service_aclqos_enable, p_mpls_pw->igmp_snooping_enable, \
                         p_mpls_pw->u.vpls_info.fid, p_mpls_pw->u.vpls_info.logic_port, p_mpls_pw->u.vpls_info.logic_port_type); \
    } while (0)

struct sys_mpls_show_info_s
{
    uint32 space_id;
    uint32 count;
    uint8 lchip;
    uint8 rsv[3];
};
typedef struct sys_mpls_show_info_s sys_mpls_show_info_t;

struct sys_mpls_ilm_spool_s
{
    uint32 ad_index;
    DsMpls_m spool_ad;
};
typedef struct sys_mpls_ilm_spool_s sys_mpls_ilm_spool_t;

struct sys_mpls_ilm_hash_s
{
    uint32 nh_id;
    uint32 stats_id;
    sys_mpls_ilm_spool_t* spool_ad_ptr;
    uint32 label;
    uint8 spaceid;
    uint8 is_interface;
    uint8 model;
    uint8 type;
    uint8 id_type;
    uint8 bind_dsfwd;
    uint32 ad_index;
};
typedef struct sys_mpls_ilm_hash_s sys_mpls_ilm_hash_t;

struct sys_mpls_ilm_s
{
    uint32 key_offset;
    uint32 ad_offset;
    uint16 aps_group_id;
    uint16 stats_ptr;
    uint32 flag;
    uint32 gport;
    uint8  update;
    sys_mpls_ilm_hash_t* p_cur_hash;
    sys_mpls_ilm_spool_t* p_spool_ad;
    ctc_mpls_property_t* p_prop_info;
};
typedef struct sys_mpls_ilm_s sys_mpls_ilm_t;

struct sys_mpls_inner_route_mac_s
{
    uint16 ref_cnt;
    mac_addr_t inner_route_mac;
};
typedef struct sys_mpls_inner_route_mac_s sys_mpls_inner_route_mac_t;

typedef int32 (* mpls_write_ilm_t)(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls);

struct sys_mpls_master_s
{
    sal_mutex_t* mutex;
    ctc_hash_t *p_mpls_hash;
    ctc_spool_t * ad_spool;

    mpls_write_ilm_t build_ilm[CTC_MPLS_MAX_LABEL_TYPE];
    mpls_write_ilm_t write_ilm[CTC_MPLS_MAX_LABEL_TYPE];

    uint32 cur_ilm_num;
};
typedef struct sys_mpls_master_s sys_mpls_master_t;

enum sys_mpls_oam_check_type_e
{
    SYS_MPLS_OAM_CHECK_TYPE_NONE,
    SYS_MPLS_OAM_CHECK_TYPE_MEP,
    SYS_MPLS_OAM_CHECK_TYPE_MIP,
    SYS_MPLS_OAM_CHECK_TYPE_DISCARD,
    SYS_MPLS_OAM_CHECK_TYPE_TOCPU,
    SYS_MPLS_OAM_CHECK_TYPE_DATA_DISCARD,
    SYS_MPLS_OAM_CHECK_TYPE_MAX
};
typedef enum sys_mpls_oam_check_type_e sys_mpls_oam_check_type_t;

sys_mpls_master_t* p_gg_mpls_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_MPLS_CREAT_LOCK(lchip)                   \
    do                                            \
    {                                             \
        sal_mutex_create(&p_gg_mpls_master[lchip]->mutex); \
        if (NULL == p_gg_mpls_master[lchip]->mutex)  \
        { \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX); \
        } \
    } while (0)

#define SYS_MPLS_LOCK(lchip) \
    sal_mutex_lock(p_gg_mpls_master[lchip]->mutex)

#define SYS_MPLS_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_mpls_master[lchip]->mutex)

#define CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_mpls_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define CTC_RETURN_MPLS_UNLOCK(lchip, op) \
    do \
    { \
        sal_mutex_unlock(p_gg_mpls_master[lchip]->mutex); \
        return (op); \
    } while (0)


int32
sys_goldengate_mpls_wb_sync(uint8 lchip);

int32
sys_goldengate_mpls_wb_restore(uint8 lchip);

#define _________OPERATION_ON_HASHKEY_________

STATIC int32
_sys_goldengate_mpls_lookup_key(uint8 lchip, DsUserIdTunnelMplsHashKey_m* pkey, uint32* p_key_index)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));
    in.tbl_id    = DsUserIdTunnelMplsHashKey_t;
    in.data      = pkey;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_USER_ID, &in, &out));

    if (out.conflict)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key conflict!!\n");
        return CTC_E_MPLS_NO_RESOURCE;
    }

    *p_key_index = out.key_index;

    if (out.hit)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key exist!!\n");
        return CTC_E_ENTRY_EXIST;
    }

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get Key Index:%#x \n", *p_key_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_alloc_key(uint8 lchip, DsUserIdTunnelMplsHashKey_m* pkey, uint32* p_key_index)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.tbl_id    = DsUserIdTunnelMplsHashKey_t;
    in.data      = pkey;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ALLOC_ACC_USER_ID, &in, &out));

    if (out.conflict)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key conflict!!\n");
        return CTC_E_MPLS_NO_RESOURCE;
    }

    *p_key_index = out.key_index;

    if (out.hit)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key exist!!\n");
        return CTC_E_ENTRY_EXIST;
    }

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Alloc Key Index:%#x \n", *p_key_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_add_key(uint8 lchip, DsUserIdTunnelMplsHashKey_m* p_key, uint32 key_index)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&in, 0, sizeof(in));
    in.tbl_id    = DsUserIdTunnelMplsHashKey_t;
    in.data      = p_key;
    in.key_index = key_index;

    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_ADD_ACC_USER_ID_BY_IDX, &in, &out));

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key index:%d\n", key_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_remove_key(uint8 lchip, uint32 key_index)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));
    sal_memset(&in, 0, sizeof(in));
    in.tbl_id    = DsUserIdTunnelMplsHashKey_t;
    in.data      = &key;
    in.key_index = key_index;
    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_DEL_ACC_USER_ID_BY_IDX, &in, &out));

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key index:%d\n", key_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_get_key_ad(uint8 lchip, uint32 key_index, uint32* p_ad_index)
{
    drv_cpu_acc_in_t in;
    drv_cpu_acc_out_t out;
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));
    sal_memset(&in, 0, sizeof(in));
    in.tbl_id    = DsUserIdTunnelMplsHashKey_t;
    in.key_index = key_index;
    CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_USER_ID_BY_IDX, &in, &out));

    sal_memcpy(&key, out.data, sizeof(key));

    GetDsUserIdTunnelMplsHashKey(A, dsAdIndex_f, &key, p_ad_index);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key index:%d\n", key_index);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key ad index:%d\n", *p_ad_index);

    return CTC_E_NONE;
}

#define _________OPF_________

STATIC int32
_sys_goldengate_mpls_alloc_index(uint8 lchip, uint32 *offset)
{

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    return sys_goldengate_ftm_alloc_table_offset(lchip, DsMpls_t, 1, 1, offset);
}


STATIC int32
_sys_goldengate_mpls_free_index(uint8 lchip, uint32 offset)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "offset = %d \n", offset);

    return sys_goldengate_ftm_free_table_offset(lchip, DsMpls_t, 1, 1, offset);
}



#define _________OPERATION_ON_DSMPLS_________
STATIC int32
_sys_goldengate_mpls_write_dsmpls(uint8 lchip, uint32 ad_offset, DsMpls_m* p_dsmpls)
{
    uint32 cmd = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls AD index:%d\n", ad_offset);

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_offset, cmd, p_dsmpls));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_read_dsmpls(uint8 lchip, uint32 ad_offset, DsMpls_m* p_dsmpls)
{
    uint32 cmd = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls AD index:%d\n", ad_offset);

    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_offset, cmd, p_dsmpls));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_remove_dsmpls(uint8 lchip, uint32 ad_offset)
{
    uint32 cmd = 0;
    DsMpls_m dsmpls;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls AD index:%d\n", ad_offset);

    sal_memset(&dsmpls, 0, sizeof(DsMpls_m));

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_offset, cmd, &dsmpls));

    return CTC_E_NONE;
}
 #define _________DB_HASH_________

STATIC sys_mpls_ilm_hash_t*
_sys_goldengate_mpls_db_lookup_hash(uint8 lchip, sys_mpls_ilm_hash_t* p_db_hash_info)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    spaceid = %d  label:%d,\n",
                  p_db_hash_info->spaceid, p_db_hash_info->label);

    return ctc_hash_lookup(p_gg_mpls_master[lchip]->p_mpls_hash, p_db_hash_info);
}

STATIC int32
_sys_goldengate_mpls_db_add_hash(uint8 lchip, sys_mpls_ilm_hash_t* p_db_hash_info)
{

    sys_mpls_ilm_hash_t* p_hash_ret_node = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


    p_hash_ret_node = ctc_hash_insert(p_gg_mpls_master[lchip]->p_mpls_hash, p_db_hash_info);
    if (NULL == p_hash_ret_node)
    {
        return CTC_E_MPLS_NO_RESOURCE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_db_remove_hash(uint8 lchip, sys_mpls_ilm_hash_t* p_db_hash_info)
{

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

     ctc_hash_remove(p_gg_mpls_master[lchip]->p_mpls_hash, p_db_hash_info);

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_mpls_hash_make(sys_mpls_ilm_hash_t* backet)
{
    uint32 val = 0;
    uint8* data = NULL;
    uint8   length = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (!backet)
    {
        return 0;
    }

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"spaceid = %d, label = %d\n",
                             backet->spaceid, backet->label);

    val = ((backet->spaceid << 24) | backet->label );
    data = (uint8*)&val;
    length = sizeof(uint32);

    return ctc_hash_caculate(length, data);

}


STATIC bool
_sys_goldengate_mpls_hash_cmp(sys_mpls_ilm_hash_t* p_ilm_data0,
                              sys_mpls_ilm_hash_t* p_ilm_data1)
{
    if (!p_ilm_data0 || !p_ilm_data1)
    {
        return FALSE;
    }

    if ((p_ilm_data0->spaceid == p_ilm_data1->spaceid)
        && (p_ilm_data0->label == p_ilm_data1->label))
    {
        return TRUE;
    }

    return FALSE;
}

#define _________DB_SPOOL_________

STATIC int32
_sys_goldengate_mpls_db_add_spool(uint8 lchip,
                                sys_mpls_ilm_spool_t* p_spool_node,
                                sys_mpls_ilm_spool_t** p_adptr)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_spool_t* p_ret_node = NULL;
    sys_mpls_ilm_spool_t* p_new_node = NULL;

    p_new_node = (sys_mpls_ilm_spool_t*)mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_ilm_spool_t));
    if (NULL == p_new_node)
    {
        CTC_ERROR_RETURN(CTC_E_NO_MEMORY);
    }

    sal_memset(p_new_node, 0, sizeof(sys_mpls_ilm_spool_t));
    sal_memcpy(p_new_node, p_spool_node, sizeof(sys_mpls_ilm_spool_t));

    ret = ctc_spool_add(p_gg_mpls_master[lchip]->ad_spool, p_new_node, NULL, &p_ret_node);
    if (0 > ret)
    {
        /* if call this function, there's not a node exist. */
        mem_free(p_new_node);
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
    }
    *p_adptr = p_new_node;

    return ret;
}


STATIC int32
_sys_goldengate_mpls_db_update_spool(uint8 lchip, sys_mpls_ilm_spool_t* p_spool_node)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_spool_t* p_dsmpls_get = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    ret = ctc_spool_add(p_gg_mpls_master[lchip]->ad_spool, p_spool_node, NULL, &p_dsmpls_get);
    if (0 > ret)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "add spool node failed:%d\n", ret);
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
    }
    return ret;
}


STATIC sys_mpls_ilm_spool_t*
_sys_goldengate_mpls_db_lookup_spool(uint8 lchip, sys_mpls_ilm_spool_t* p_spool_node)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    return ctc_spool_lookup(p_gg_mpls_master[lchip]->ad_spool, p_spool_node);
}

STATIC int32
_sys_goldengate_mpls_db_remove_spool(uint8 lchip, sys_mpls_ilm_spool_t* p_spool_node)
{
    int32 ret = 0 ;
    DsMpls_m* p_free_date = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    ret = ctc_spool_remove(p_gg_mpls_master[lchip]->ad_spool, p_spool_node, &p_free_date);
    if ((CTC_SPOOL_E_OPERATE_MEMORY == ret)
        && (NULL != p_free_date))
    {
        mem_free(p_free_date);
    }

    return ret;
}

STATIC uint32
_sys_goldengate_mpls_spool_make(sys_mpls_ilm_spool_t* p_spool_node)
{
    return ctc_hash_caculate(sizeof(DsMpls_m), &p_spool_node->spool_ad);
}

STATIC bool
_sys_goldengate_mpls_spool_cmp(sys_mpls_ilm_spool_t* p_ilm_data0,
                              sys_mpls_ilm_spool_t* p_ilm_data1)
{
    if (sal_memcmp(&p_ilm_data0->spool_ad, &p_ilm_data1->spool_ad, sizeof(DsMpls_m)))
    {
        return FALSE;
    }

    return TRUE;
}

#define _________SYS_CORE_________

STATIC int32
_sys_goldengate_mpls_ilm_get_ad_offset(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));

    if ((NULL != p_ilm_info->p_cur_hash)
        && (NULL == p_ilm_info->p_cur_hash->spool_ad_ptr))
    {
        /* VPLS or VPWS */
        _sys_goldengate_mpls_get_key_ad(lchip, p_ilm_info->key_offset,&p_ilm_info->ad_offset);
    }
    else
    {
        /* p_ilm_info->p_cur_hash shuold not be NULL*/
        p_ilm_info->ad_offset = p_ilm_info->p_cur_hash->spool_ad_ptr->ad_index;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_map_pw_ctc2sys(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_ctc, ctc_mpls_ilm_t *p_sys)
{
    p_sys->label                   = p_ctc->label;
    p_sys->pwid= p_ctc->logic_port;

    p_sys->flw_vrf_srv_aps.service_id  = p_ctc->service_id;
    p_sys->aps_select_grp_id       = p_ctc->aps_select_grp_id;

    p_sys->id_type                 = p_ctc->id_type;
    p_sys->spaceid                 = p_ctc->space_id;
    p_sys->cwen                    = p_ctc->cwen;

    p_sys->decap                   = 1;
    p_sys->aps_select_protect_path = p_ctc->aps_select_protect_path;
    p_sys->logic_port_type         = p_ctc->u.vpls_info.logic_port_type;

    p_sys->oam_en                  = p_ctc->oam_en;
    p_sys->l2vpn_oam_id    = p_ctc->l2vpn_oam_id;
    p_sys->stats_id                = p_ctc->stats_id;

    p_sys->qos_use_outer_info      = p_ctc->qos_use_outer_info;

    p_sys->svlan_tpid_index        = p_ctc->svlan_tpid_index;

    p_sys->pw_mode                 = p_ctc->pw_mode;

    p_sys->gport                  = p_ctc->gport;
    p_sys->inner_pkt_type = p_ctc->inner_pkt_type;

    if (p_ctc->service_aclqos_enable)
    {
        CTC_SET_FLAG(p_sys->flag, CTC_MPLS_ILM_FLAG_SERVICE_ACL_EN);
    }

    if (CTC_FLAG_ISSET(p_ctc->flag, CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT))
    {
        CTC_SET_FLAG(p_sys->flag, CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT);
    }

    if (p_ctc->service_policer_en)
    {
       CTC_SET_FLAG(p_sys->flag, CTC_MPLS_ILM_FLAG_SERVICE_POLICER_EN);
    }

    if (p_ctc->l2vpntype == CTC_MPLS_L2VPN_VPWS)
    {
        p_sys->type =  CTC_MPLS_LABEL_TYPE_VPWS;
        p_sys->nh_id                   = p_ctc->u.pw_nh_id;
    }
    else if (p_ctc->l2vpntype == CTC_MPLS_L2VPN_VPLS)
    {
        p_sys->type =  CTC_MPLS_LABEL_TYPE_VPLS;
        p_sys->fid                     = p_ctc->u.vpls_info.fid;
    }

    if (CTC_FLAG_ISSET(p_ctc->flag, CTC_MPLS_ILM_FLAG_L2VPN_OAM))
    {
        CTC_SET_FLAG(p_sys->flag, CTC_MPLS_ILM_FLAG_L2VPN_OAM);
    }
    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_ilm_add_spool(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    sys_mpls_ilm_spool_t spool_node;
    sys_mpls_ilm_spool_t* p_spool_ad = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&spool_node ,0 , sizeof(sys_mpls_ilm_spool_t));

    CTC_ERROR_RETURN(_sys_goldengate_mpls_alloc_index(lchip, &p_ilm_info->ad_offset));

    /* write the db spool */
    spool_node.ad_index = p_ilm_info->ad_offset;
    sal_memcpy(&spool_node.spool_ad, p_dsmpls, sizeof(DsMpls_m));
    CTC_ERROR_RETURN(_sys_goldengate_mpls_db_add_spool(lchip,
                                            &spool_node,
                                            &p_spool_ad));
    p_ilm_info->p_spool_ad = p_spool_ad;

    return CTC_E_NONE;
}

STATIC sys_mpls_ilm_spool_t*
_sys_goldengate_mpls_ilm_lookup_spool(uint8 lchip, DsMpls_m* p_dsmpls)
{
    sys_mpls_ilm_spool_t spool_node;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&spool_node ,0 , sizeof(sys_mpls_ilm_spool_t));

    sal_memcpy(&spool_node.spool_ad, p_dsmpls, sizeof(DsMpls_m));

    return _sys_goldengate_mpls_db_lookup_spool(lchip, &spool_node);
}


STATIC int32
_sys_goldengate_mpls_ilm_remove_spool(uint8 lchip, uint32 ad_offset, DsMpls_m* p_dsmpls)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_spool_t spool_node;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&spool_node ,0 , sizeof(sys_mpls_ilm_spool_t));
    sal_memcpy(&spool_node.spool_ad, p_dsmpls, sizeof(DsMpls_m));

    ret = _sys_goldengate_mpls_db_remove_spool(lchip, &spool_node);
    if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
        _sys_goldengate_mpls_remove_dsmpls(lchip, ad_offset);
        _sys_goldengate_mpls_free_index(lchip, ad_offset);
    }

    return ret;
}

STATIC int32
_sys_goldengate_mpls_ilm_write_key(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "    spaceid = %d  label:%d,\n",
                  p_mpls_ilm->spaceid, p_mpls_ilm->label);


    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));
    SetDsUserIdTunnelMplsHashKey(V, hashType_f,      &key, USERIDHASHTYPE_TUNNELMPLS);
    SetDsUserIdTunnelMplsHashKey(V, isInterfaceId_f, &key, 0);
    SetDsUserIdTunnelMplsHashKey(V, labelSpace_f,    &key, p_mpls_ilm->spaceid);
    SetDsUserIdTunnelMplsHashKey(V, mplsLabel_f,     &key, p_mpls_ilm->label);
    SetDsUserIdTunnelMplsHashKey(V, dsAdIndex_f,     &key, p_ilm_info->ad_offset);
    SetDsUserIdTunnelMplsHashKey(V, valid_f,         &key, 1);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " Mpls Hash key index:%d\n", p_ilm_info->key_offset);

    return _sys_goldengate_mpls_add_key(lchip, &key, p_ilm_info->key_offset);
}

/*
    the function actually just look up the key index
*/
STATIC int32
_sys_goldengate_mpls_ilm_get_key_index(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));

    SetDsUserIdTunnelMplsHashKey(V, hashType_f,         &key, USERIDHASHTYPE_TUNNELMPLS);
    SetDsUserIdTunnelMplsHashKey(V, isInterfaceId_f,    &key, 0);
    SetDsUserIdTunnelMplsHashKey(V, labelSpace_f,       &key, p_mpls_ilm->spaceid);
    SetDsUserIdTunnelMplsHashKey(V, mplsLabel_f,        &key, p_mpls_ilm->label);
    SetDsUserIdTunnelMplsHashKey(V, dsAdIndex_f,        &key, 0);
    SetDsUserIdTunnelMplsHashKey(V, valid_f,            &key, 1);

    return _sys_goldengate_mpls_lookup_key(lchip, &key, &p_ilm_info->key_offset);
}

STATIC int32
_sys_goldengate_mpls_ilm_alloc_key_index(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{
    DsUserIdTunnelMplsHashKey_m key;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&key, 0, sizeof(DsUserIdTunnelMplsHashKey_m));

    SetDsUserIdTunnelMplsHashKey(V, hashType_f,         &key, USERIDHASHTYPE_TUNNELMPLS);
    SetDsUserIdTunnelMplsHashKey(V, isInterfaceId_f,    &key, 0);
    SetDsUserIdTunnelMplsHashKey(V, labelSpace_f,       &key, p_mpls_ilm->spaceid);
    SetDsUserIdTunnelMplsHashKey(V, mplsLabel_f,        &key, p_mpls_ilm->label);
    SetDsUserIdTunnelMplsHashKey(V, dsAdIndex_f,        &key, 0);
    SetDsUserIdTunnelMplsHashKey(V, valid_f,            &key, 1);

    return _sys_goldengate_mpls_alloc_key(lchip, &key, &p_ilm_info->key_offset);
}

STATIC int32
_sys_goldengate_mpls_update_ad(uint8 lchip, void* data, void* change_nh_param)
{
    sys_mpls_ilm_hash_t* p_ilm_info = data;
    sys_nh_info_dsnh_t* p_dsnh_info = change_nh_param;
    DsMpls_m   dsmpls;
    uint32 cmdr;
    uint32 cmdw;
    uint32 dest_id = 0;
    uint8 adjust_len_idx = 0;
    uint32 bind_nh = 0;
    uint8 gchip = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&dsmpls, 0, sizeof(dsmpls));

    cmdr = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, p_ilm_info->ad_index, cmdr, &dsmpls);

    bind_nh = GetDsMpls(V, nextHopPtrValid_f, &dsmpls);
    if(bind_nh == 0)
    {
         return CTC_E_NONE;
    }

    if (p_dsnh_info->drop_pkt)
    {
        sys_goldengate_get_gchip_id(lchip, &gchip);
        dest_id = SYS_ENCODE_DESTMAP(gchip, SYS_RSV_PORT_DROP_ID);
    }
    else
    {
        dest_id = SYS_ENCODE_DESTMAP(p_dsnh_info->dest_chipid, p_dsnh_info->dest_id);
    }


    SetDsMpls(V, u2_g2_adApsBridgeEn_f, &dsmpls, (p_dsnh_info->aps_en?1:0));


    if( p_dsnh_info->adjust_len != 0)
    {
         sys_goldengate_lkup_adjust_len_index(lchip, p_dsnh_info->adjust_len, &adjust_len_idx);
         SetDsMpls(V, u2_g2_adLengthAdjustType_f, &dsmpls,adjust_len_idx);
    }
    SetDsMpls(V, u2_g2_adDestMap_f, &dsmpls, dest_id);
    SetDsMpls(V, u2_g2_adNextHopExt_f, &dsmpls, p_dsnh_info->nexthop_ext);
    SetDsMpls(V, u4_g4_adNextHopPtr_f, &dsmpls, p_dsnh_info->dsnh_offset);
    SetDsMpls(V, nextHopPtrValid_f, &dsmpls, 1);


    DRV_IOCTL(lchip, p_ilm_info->ad_index, cmdw, &dsmpls);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "MPLS update_ad: p_ilm_info->ad_index:0x%x    \n", p_ilm_info->ad_index);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_set_nhinfo(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, DsMpls_m* p_dsmpls,sys_mpls_ilm_t* p_ilm_info)
{
    sys_nh_info_dsnh_t nhinfo;
    uint32  fwd_offset ;
    uint32 nh_id = p_mpls_ilm->nh_id;
    uint8  merge_dsfwd = 0;
    uint8  bind_nh = 0;
    uint8  have_dsfwd = 0;


    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));

    if ((TRUE == p_mpls_ilm->pop) && (p_mpls_ilm->nh_id == 0))
    {   /* there's no label in the label stack, drop the pkt */
        nh_id = 1;
    }

    p_ilm_info->flag = p_mpls_ilm->flag;
    p_ilm_info->gport = p_mpls_ilm->gport;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, nh_id, &nhinfo));

     /*update ilm*/
    if (!nhinfo.dsfwd_valid && !nhinfo.ecmp_valid && !nhinfo.merge_dsfwd && !CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT))
    {

        if( p_ilm_info->p_cur_hash->nh_id != p_mpls_ilm->nh_id)
        {
             sys_nh_update_dsnh_param_t update_dsnh;
             sal_memset(&update_dsnh, 0, sizeof(sys_nh_update_dsnh_param_t));
             if(p_ilm_info->update && p_ilm_info->p_cur_hash->bind_dsfwd)
              {
                  update_dsnh.data = NULL;
                  update_dsnh.updateAd = NULL;
                  sys_goldengate_nh_bind_dsfwd_cb(lchip, p_ilm_info->p_cur_hash->nh_id , &update_dsnh) ;
              }

             update_dsnh.data = p_ilm_info->p_cur_hash;
             update_dsnh.updateAd = _sys_goldengate_mpls_update_ad;
             if (sys_goldengate_nh_bind_dsfwd_cb(lchip, p_mpls_ilm->nh_id, &update_dsnh) != CTC_E_NH_HR_NH_IN_USE)
             {
                   bind_nh = 1;
              }
             p_ilm_info->p_cur_hash->bind_dsfwd = bind_nh ;
        }
        else
         {
            bind_nh = p_ilm_info->p_cur_hash->bind_dsfwd ;
         }

    }
    have_dsfwd |= (nhinfo.merge_dsfwd == 2);
    merge_dsfwd = (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT)
                         || bind_nh
                         || (nhinfo.merge_dsfwd == 1&& !nhinfo.dsfwd_valid) )
                        && !have_dsfwd;



    if (p_mpls_ilm->pop)
    {
        SetDsMpls(V, _continue_f, p_dsmpls, TRUE);
    }
    else
    {
        SetDsMpls(V, _continue_f, p_dsmpls, 0);
    }

   if (nhinfo.ecmp_valid)
    {
        SetDsMpls(V, u2_g1_ecmpGroupId_f, p_dsmpls, nhinfo.ecmp_group_id);
        SetDsMpls(V, nextHopPtrValid_f, p_dsmpls, 0);
    }
    else
    {
        if(merge_dsfwd)
        {
            uint8 adjust_len_idx = 0;
            /* for pop, if the config the nexthop, need to write the hw tbl */
            if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_ASSIGN_OUTPUT_PORT))
            {
                SetDsMpls(V, u2_g2_adDestMap_f, p_dsmpls,
                          SYS_ENCODE_DESTMAP(SYS_MAP_CTC_GPORT_TO_GCHIP(p_mpls_ilm->gport),
                          SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_mpls_ilm->gport)));
            }
            else if( nhinfo.is_mcast)
            {
              uint8   speed = 0;
              SetDsMpls(V, u2_g2_adDestMap_f, p_dsmpls,
                          SYS_ENCODE_MCAST_IPE_DESTMAP(speed, nhinfo.dest_id));
           }
            else
            {
                SetDsMpls(V, u2_g2_adDestMap_f, p_dsmpls,
                          SYS_ENCODE_DESTMAP(nhinfo.dest_chipid, nhinfo.dest_id));
            }
            SetDsMpls(V, u2_g2_adNextHopExt_f, p_dsmpls, nhinfo.nexthop_ext);
            if( nhinfo.adjust_len != 0)
            {
                 sys_goldengate_lkup_adjust_len_index(lchip, nhinfo.adjust_len, &adjust_len_idx);
                 SetDsMpls(V, u2_g2_adLengthAdjustType_f, p_dsmpls, adjust_len_idx);
            }
            SetDsMpls(V, u4_g4_adNextHopPtr_f,p_dsmpls, nhinfo.dsnh_offset);
            SetDsMpls(V, u2_g2_adApsBridgeEn_f, p_dsmpls, nhinfo.aps_en);
            SetDsMpls(V, nextHopPtrValid_f, p_dsmpls, 1);
            if (SYS_NH_TYPE_IP_TUNNEL == nhinfo.nh_entry_type)
            {
                SetDsMpls(V, u2_g2_adCareLength_f, p_dsmpls, 1);
            }
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, nh_id, &fwd_offset));
            SetDsMpls(V, u4_g1_dsFwdPtrSelect_f, p_dsmpls, 0);
            SetDsMpls(V, u4_g1_dsFwdPtr_f, p_dsmpls, fwd_offset);
            SetDsMpls(V, nextHopPtrValid_f, p_dsmpls, 0);
        }
    }

    return CTC_E_NONE;
}



STATIC sys_mpls_ilm_hash_t*
_sys_goldengate_mpls_ilm_get_hash(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    sys_mpls_ilm_hash_t db_hash_info;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&db_hash_info, 0, sizeof(sys_mpls_ilm_hash_t));

    db_hash_info.label = p_mpls_ilm->label;
    db_hash_info.spaceid = p_mpls_ilm->spaceid;

    return _sys_goldengate_mpls_db_lookup_hash(lchip, &db_hash_info);
}

int32
_sys_goldengate_mpls_db_lookup_key_by_label(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, uint32* key_index, uint32* ad_index)
 {
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_t ilm_info;
    ctc_mpls_ilm_t mpls_ilm;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "spaceid = %d  label:%d,\n",p_mpls_ilm->spaceid, p_mpls_ilm->label);

    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));
    sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));

    mpls_ilm.spaceid = p_mpls_ilm->spaceid;
    mpls_ilm.label= p_mpls_ilm->label;

    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip, &mpls_ilm);
    if (NULL == ilm_info.p_cur_hash)
    {
        /* for the complex of update, it needs an independent path*/
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, &mpls_ilm, &ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip, &mpls_ilm, &ilm_info));

   *key_index = ilm_info.key_offset;
   *ad_index = ilm_info.ad_offset;

    return CTC_E_NONE;
 }

STATIC int32
_sys_goldengate_mpls_map_ilm_sys2ctc(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_hash_t* p_db_hashkey)
{
    DsMpls_m* p_spool_ad = NULL;
    sys_mpls_ilm_t ilm_info;
    DsMpls_m dsmpls;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));
    sal_memset(&dsmpls, 0, sizeof(DsMpls_m));

    CTC_ERROR_RETURN(_sys_goldengate_mpls_db_lookup_key_by_label(lchip, p_mpls_ilm,
                                    &ilm_info.key_offset, &ilm_info.ad_offset));
    CTC_ERROR_RETURN(_sys_goldengate_mpls_read_dsmpls(lchip, ilm_info.ad_offset, &dsmpls));
    p_spool_ad = &dsmpls;



    p_mpls_ilm->pwid = GetDsMpls(V, logicSrcPort_f, p_spool_ad);
    p_mpls_ilm->id_type = p_db_hashkey->id_type;
    p_mpls_ilm->type = p_db_hashkey->type;
    p_mpls_ilm->model = p_db_hashkey->model;
    p_mpls_ilm->cwen = GetDsMpls(V, cwExist_f, p_spool_ad);
    p_mpls_ilm->pop = GetDsMpls(V, _continue_f, p_spool_ad);
    p_mpls_ilm->nh_id = p_db_hashkey->nh_id;
    p_mpls_ilm->decap = GetDsMpls(V, innerPacketLookup_f, p_spool_ad);
    p_mpls_ilm->out_intf_spaceid = GetDsMpls(V, u4_g3_mplsLabelSpace_f, p_spool_ad);
    p_mpls_ilm->pw_mode = GetDsMpls(V, outerVlanIsCVlan_f, p_spool_ad);
    p_mpls_ilm->stats_id = p_db_hashkey->stats_id;

    if(p_mpls_ilm->id_type & CTC_MPLS_ID_FLOW)
    {
        /*p_mpls_ilm->flw_vrf_srv_aps.flow_id = p_ilm_data->flow_id;*/
    }

    if(p_mpls_ilm->id_type & CTC_MPLS_ID_VRF)
    {
        if (CTC_MPLS_LABEL_TYPE_GATEWAY != p_db_hashkey->type)
        {
            p_mpls_ilm->vrf_id=
                        GetDsMpls(V, u4_g2_fid_f, p_spool_ad);
        }
        else
        {
            p_mpls_ilm->vrf_id=
                        GetDsMpls(V, u2_g1_secondFid_f, p_spool_ad);
        }
    }

    if(p_mpls_ilm->id_type & CTC_MPLS_ID_SERVICE)
    {
        /*p_mpls_ilm->flw_vrf_srv_aps.service_id = p_ilm_data->service_id;*/
    }

    if(p_mpls_ilm->id_type & CTC_MPLS_ID_APS_SELECT)
    {
        p_mpls_ilm->flw_vrf_srv_aps.aps_select_grp_id =
                        GetDsMpls(V, apsSelectGroupId_f, p_spool_ad);
        p_mpls_ilm->aps_select_grp_id =
                        GetDsMpls(V, apsSelectGroupId_f, p_spool_ad);
        p_mpls_ilm->aps_select_protect_path =
                        GetDsMpls(V, apsSelectProtectingPath_f, p_spool_ad);
    }

    p_mpls_ilm->fid = GetDsMpls(V, u4_g2_fid_f, p_spool_ad);
    p_mpls_ilm->oam_en = GetDsMpls(V, vplsOam_f, p_spool_ad);
    p_mpls_ilm->trust_outer_exp =
                GetDsMpls(V, useLabelExp_f, p_spool_ad);
    p_mpls_ilm->qos_use_outer_info =
        GetDsMpls(V, aclQosUseOuterInfo_f, p_spool_ad);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_mpls_ilm_add_hash(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{


  p_ilm_info->p_cur_hash->label = p_mpls_ilm->label;
  p_ilm_info->p_cur_hash->nh_id = p_mpls_ilm->nh_id;
  p_ilm_info->p_cur_hash->spaceid = p_mpls_ilm->spaceid;
  p_ilm_info->p_cur_hash->stats_id = p_mpls_ilm->stats_id;
  p_ilm_info->p_cur_hash->model = p_mpls_ilm->model;
  p_ilm_info->p_cur_hash->type = p_mpls_ilm->type;
  p_ilm_info->p_cur_hash->id_type = p_mpls_ilm->id_type;
  p_ilm_info->p_cur_hash->spool_ad_ptr = p_ilm_info->p_spool_ad;
  p_ilm_info->p_cur_hash->ad_index = p_ilm_info->ad_offset;
  p_ilm_info->p_cur_hash->is_interface = 0;

    return _sys_goldengate_mpls_db_add_hash(lchip, p_ilm_info->p_cur_hash);;
}


STATIC int32
_sys_goldengate_mpls_ilm_remove_hash(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    sys_mpls_ilm_hash_t db_hash_info;

    sal_memset(&db_hash_info, 0, sizeof(sys_mpls_ilm_hash_t));

    db_hash_info.is_interface = 0;
    db_hash_info.label = p_mpls_ilm->label;
    db_hash_info.spaceid = p_mpls_ilm->spaceid;

    return _sys_goldengate_mpls_db_remove_hash(lchip, &db_hash_info);;
}

STATIC int32
_sys_goldengate_mpls_ilm_cfg_policer(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, DsMpls_m* p_dsmpls)
{
    uint16 policer_ptr = 0;
    uint8 is_bwp = 0;
    uint8 triple_play = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (p_mpls_ilm->policer_id != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_qos_policer_index_get(lchip, p_mpls_ilm->policer_id,
                                    &policer_ptr, &is_bwp, &triple_play));
        if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_SERVICE_POLICER_EN))
        {
            SetDsMpls(V, servicePolicerValid_f, p_dsmpls, 1);
        }
        else
        {
            if (is_bwp || triple_play)
            {
                return CTC_E_UNEXPECT;
            }
        }
        SetDsMpls(V, mplsFlowPolicerPtr_f, p_dsmpls, policer_ptr);
    }
    else
    {
        if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_SERVICE_POLICER_EN))
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_common(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_cfg_policer(lchip, p_mpls_ilm, p_dsmpls));

    if (p_mpls_ilm->id_type & CTC_MPLS_ID_APS_SELECT)
    {
        SetDsMpls(V, apsSelectValid_f, p_dsmpls, 1);
        if (p_mpls_ilm->aps_select_protect_path)
        {
            SetDsMpls(V, apsSelectProtectingPath_f, p_dsmpls, 1);
        }

        SetDsMpls(V, apsSelectGroupId_f, p_dsmpls, p_mpls_ilm->aps_select_grp_id & 0x1FFF);
    }
    else
    {
        SetDsMpls(V, apsSelectValid_f, p_dsmpls, 0);
        SetDsMpls(V, apsSelectProtectingPath_f, p_dsmpls, 0);
        SetDsMpls(V, apsSelectGroupId_f, p_dsmpls, 0);
    }

    if (p_mpls_ilm->id_type & CTC_MPLS_ID_STATS)
    {
        sys_stats_statsid_t stats_type = {0};

        CTC_ERROR_RETURN(sys_goldengate_stats_get_type_by_statsid(lchip, p_mpls_ilm->stats_id, &stats_type));
        p_ilm_info->stats_ptr = stats_type.stats_ptr;
        SetDsMpls(V, statsPtr_f, p_dsmpls, p_ilm_info->stats_ptr);

        if (!stats_type.is_vc_label)
        {
            SetDsMpls(V, statsType_f, p_dsmpls, 0);
        }
        else
        {
            SetDsMpls(V, statsType_f, p_dsmpls, 1);
        }
    }
    else
    {
        /* for update stats information */
        SetDsMpls(V, statsPtr_f, p_dsmpls, 0);
        SetDsMpls(V, statsType_f, p_dsmpls, 0);
    }

    if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_ACL_USE_OUTER_INFO))
    {
        SetDsMpls(V, aclQosUseOuterInfo_f, p_dsmpls, 1);
    }

    if (p_mpls_ilm->qos_use_outer_info)
    {
        SetDsMpls(V, classifyUseOuterInfo_f, p_dsmpls, TRUE);
    }

    if (p_mpls_ilm->cwen)
    {
        SetDsMpls(V, cwExist_f, p_dsmpls, 1);
        SetDsMpls(V, offsetBytes_f, p_dsmpls, 2);
    }
    else
    {
        SetDsMpls(V, offsetBytes_f, p_dsmpls, 1);
    }

    SetDsMpls(V, useLabelExp_f, p_dsmpls,
                (p_mpls_ilm->trust_outer_exp) ? FALSE : TRUE);

    if ((13 == p_mpls_ilm->label) && (0 == p_mpls_ilm->spaceid))
    {
        SetDsMpls(V, oamCheckType_f, p_dsmpls, 1);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_normal(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_set_nhinfo(lchip, p_mpls_ilm, p_dsmpls,p_ilm_info));

    /* for normal mpls pkt, we need the condition for pop mode than it'll decap! */
    /* php don't use decap pkt, without php must use inner pkt to transit */
    if (p_mpls_ilm->decap)
    {
        SetDsMpls(V, innerPacketLookup_f, p_dsmpls, TRUE);

        /* according to  rfc3270*/
        if (CTC_MPLS_TUNNEL_MODE_SHORT_PIPE != p_mpls_ilm->model)
        {
            SetDsMpls(V, aclQosUseOuterInfo_f, p_dsmpls, TRUE);
        }
    }

    if (CTC_MPLS_TUNNEL_MODE_UNIFORM != p_mpls_ilm->model)
    {
        SetDsMpls(V, useLabelTtl_f, p_dsmpls, TRUE);
        SetDsMpls(V, ttlUpdate_f, p_dsmpls, TRUE);
    }

    if (CTC_MPLS_INNER_IPV4 == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IPV4);
    }
    else if (CTC_MPLS_INNER_IPV6 == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IPV6);
    }
    else if (CTC_MPLS_INNER_IP == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IP);
    }

    SetDsMpls(V, offsetBytes_f, p_dsmpls, 1);
    SetDsMpls(V, overwritePriority_f, p_dsmpls, TRUE);

    if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_BOS_LABLE))
    {
        /*
            note for entropy label and flow label this field must be 0
        */
        SetDsMpls(V, bosAction_f, p_dsmpls, 1);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_gateway(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (0 != p_mpls_ilm->nh_id)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_set_nhinfo(lchip, p_mpls_ilm, p_dsmpls,p_ilm_info));
    }
    else
    {
        /*
            nexthop is prior. If set, bridge or routing information
            will cover nexthop information
        */
        SetDsMpls(V, innerPacketLookup_f, p_dsmpls, TRUE);
    }

    SetDsMpls(V, useLabelTtl_f, p_dsmpls, TRUE);

    if (CTC_MPLS_L2VPN_RAW == p_mpls_ilm->pw_mode)
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, TRUE);
    }
    else
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, FALSE);
    }

    if (CTC_MPLS_INNER_IP == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IP);
    }
    else
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_ETH);
    }

    SetDsMpls(V, overwritePriority_f, p_dsmpls, !p_mpls_ilm->trust_outer_exp);
    SetDsMpls(V, sBit_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBitCheckEn_f, p_dsmpls, TRUE);

    if (p_mpls_ilm->fid)
    {
        /*
            0 is invalid fid id
        */
        SetDsMpls(V, u4_g2_fidSelect_f, p_dsmpls, 1);
        SetDsMpls(V, u4_g2_fidType_f, p_dsmpls, TRUE);
        SetDsMpls(V, u4_g2_fid_f, p_dsmpls, p_mpls_ilm->fid);
    }

    SetDsMpls(V, logicSrcPort_f, p_dsmpls, p_mpls_ilm->pwid);
    SetDsMpls(V, logicPortType_f, p_dsmpls, p_mpls_ilm->logic_port_type);

    if (CTC_FLAG_ISSET(p_mpls_ilm->id_type, CTC_MPLS_ID_VRF))
    {
        SetDsMpls(V, u2_g1_secondFidEn_f, p_dsmpls, 1);
        SetDsMpls(V, u2_g1_secondFid_f, p_dsmpls, p_mpls_ilm->vrf_id);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_l3vpn(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_MPLS_TUNNEL_MODE_UNIFORM != p_mpls_ilm->model)
    {
        SetDsMpls(V, useLabelTtl_f, p_dsmpls, TRUE);
        SetDsMpls(V, ttlUpdate_f, p_dsmpls, TRUE);
    }

    if (CTC_MPLS_INNER_IPV4 == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IPV4);
    }
    else if (CTC_MPLS_INNER_IPV6 == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IPV6);
    }
    else if (CTC_MPLS_INNER_IP == p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_IP);
    }

    SetDsMpls(V, offsetBytes_f, p_dsmpls, 1);
    SetDsMpls(V, sBit_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBitCheckEn_f, p_dsmpls, TRUE);
    SetDsMpls(V, overwritePriority_f, p_dsmpls, TRUE);
    SetDsMpls(V, u4_g2_fidSelect_f, p_dsmpls, 1);
    SetDsMpls(V, u4_g2_fidType_f, p_dsmpls, FALSE);
    SetDsMpls(V, u4_g2_fid_f, p_dsmpls, p_mpls_ilm->vrf_id);
    SetDsMpls(V, innerPacketLookup_f, p_dsmpls, TRUE);
    SetDsMpls(V, bosAction_f, p_dsmpls, 1);
    SetDsMpls(V, logicSrcPort_f, p_dsmpls, p_mpls_ilm->pwid);

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_ilm_vpws(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_set_nhinfo(lchip, p_mpls_ilm, p_dsmpls,p_ilm_info));

    if (CTC_MPLS_L2VPN_RAW == p_mpls_ilm->pw_mode)
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, TRUE);
    }
    else
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, FALSE);
    }

    if (CTC_MPLS_INNER_RAW != p_mpls_ilm->inner_pkt_type)
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_ETH);
    }
    else
    {
        SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_RESERVED);
    }

    if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_SERVICE_ACL_EN))
    {
        SetDsMpls(V, serviceAclQosEn_f, p_dsmpls, 1);
    }

    SetDsMpls(V, useLabelTtl_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBit_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBitCheckEn_f, p_dsmpls, TRUE);

    SetDsMpls(V, overwritePriority_f, p_dsmpls, !p_mpls_ilm->trust_outer_exp);
    SetDsMpls(V, svlanTpidIndex_f, p_dsmpls, p_mpls_ilm->svlan_tpid_index);
    SetDsMpls(V, bosAction_f, p_dsmpls, 1);

    if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_L2VPN_OAM))
    {
        SetDsMpls(V, vplsOam_f, p_dsmpls, 1);
        SetDsMpls(V, mplsOamIndex_f, p_dsmpls, p_mpls_ilm->l2vpn_oam_id);
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_ilm_vpls(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_MPLS_L2VPN_RAW == p_mpls_ilm->pw_mode)
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, TRUE);
    }
    else
    {
        SetDsMpls(V, outerVlanIsCVlan_f, p_dsmpls, FALSE);
    }

    SetDsMpls(V, useLabelTtl_f, p_dsmpls, TRUE);
    SetDsMpls(V, innerPacketLookup_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBit_f, p_dsmpls, TRUE);
    SetDsMpls(V, sBitCheckEn_f, p_dsmpls, TRUE);
    SetDsMpls(V, packetType_f, p_dsmpls, PKT_TYPE_ETH);
    SetDsMpls(V, u4_g2_fidSelect_f, p_dsmpls, 1);
    SetDsMpls(V, u4_g2_fidType_f, p_dsmpls, TRUE);
    SetDsMpls(V, u4_g2_fid_f, p_dsmpls, p_mpls_ilm->fid);
    SetDsMpls(V, logicSrcPort_f, p_dsmpls, p_mpls_ilm->pwid);
    SetDsMpls(V, logicPortType_f, p_dsmpls, p_mpls_ilm->logic_port_type);
    SetDsMpls(V, svlanTpidIndex_f, p_dsmpls, p_mpls_ilm->svlan_tpid_index);
    SetDsMpls(V, overwritePriority_f, p_dsmpls, !p_mpls_ilm->trust_outer_exp);
    SetDsMpls(V, bosAction_f, p_dsmpls, 1);

    if (CTC_FLAG_ISSET(p_mpls_ilm->flag, CTC_MPLS_ILM_FLAG_L2VPN_OAM))
    {
        SetDsMpls(V, vplsOam_f, p_dsmpls, 1);
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_ilm_add_one2one(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    int32 ret = CTC_E_NONE;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_mpls_alloc_index(lchip, &p_ilm_info->ad_offset));

    CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_add_hash(lchip, p_mpls_ilm, p_ilm_info),
                    ret, EXIT_STEP1);

    CTC_ERROR_GOTO(_sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls),
                    ret, EXIT_STEP2);

    CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_write_key(lchip, p_mpls_ilm, p_ilm_info),
                        ret, EXIT_STEP3);
    goto EXIT;

EXIT_STEP3:
    _sys_goldengate_mpls_remove_dsmpls(lchip, p_ilm_info->ad_offset);
EXIT_STEP2:
    _sys_goldengate_mpls_ilm_remove_hash(lchip, p_mpls_ilm);
EXIT_STEP1:
    _sys_goldengate_mpls_free_index(lchip, p_ilm_info->ad_offset);
EXIT:
    p_gg_mpls_master[lchip]->cur_ilm_num ++;
    return ret;

}

STATIC int32
_sys_goldengate_mpls_ilm_update_one2one(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_spool_t* spool_node = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (NULL != p_ilm_info->p_cur_hash->spool_ad_ptr)
    {
        /* if original ilm is N:1, now update to 1:1 */
        spool_node = p_ilm_info->p_cur_hash->spool_ad_ptr;

        /* 1:1 has no spool node, so update the DsMpls tbl first */
        CTC_ERROR_RETURN(_sys_goldengate_mpls_alloc_index(lchip, &p_ilm_info->ad_offset));
        ret = _sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls);
        if (0 > ret)
        {
            _sys_goldengate_mpls_free_index(lchip, p_ilm_info->ad_offset);
            CTC_ERROR_RETURN(ret);
        }
        ret = _sys_goldengate_mpls_ilm_write_key(lchip, p_mpls_ilm, p_ilm_info);
        if (0 > ret)
        {
            _sys_goldengate_mpls_remove_dsmpls(lchip, p_ilm_info->ad_offset);
            _sys_goldengate_mpls_free_index(lchip, p_ilm_info->ad_offset);
            CTC_ERROR_RETURN(ret);
        }
        p_ilm_info->p_cur_hash->spool_ad_ptr = NULL;

        /* for spool ref_cnt > 1 ,so use the function below remove spool node */
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_remove_spool(lchip, spool_node->ad_index, &spool_node->spool_ad));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip, p_mpls_ilm, p_ilm_info));
        CTC_ERROR_RETURN(_sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls));
    }

    p_ilm_info->p_cur_hash->type = p_mpls_ilm->type;
    p_ilm_info->p_cur_hash->id_type = p_mpls_ilm->id_type;
    p_ilm_info->p_cur_hash->model = p_mpls_ilm->model;
    p_ilm_info->p_cur_hash->nh_id = p_mpls_ilm->nh_id;
    p_ilm_info->p_cur_hash->stats_id = p_mpls_ilm->stats_id;

    return ret;
}

STATIC int32
_sys_goldengate_mpls_ilm_write_one2one(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

     if (!p_ilm_info->update)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_add_one2one(lchip, p_mpls_ilm, p_ilm_info, p_dsmpls));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_update_one2one(lchip, p_mpls_ilm, p_ilm_info, p_dsmpls));
    }


    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ad_offset:%u\n", p_ilm_info->ad_offset);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_add_one2n(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_spool_t* p_new_spool_node = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_new_spool_node = _sys_goldengate_mpls_ilm_lookup_spool(lchip, p_dsmpls);

    if (NULL == p_new_spool_node)
    {
        /* write the db spool */
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_add_spool(lchip, p_ilm_info, p_dsmpls));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_db_update_spool(lchip, p_new_spool_node));
        p_ilm_info->ad_offset = p_new_spool_node->ad_index;
        p_ilm_info->p_spool_ad = p_new_spool_node;
    }

    /* write the db hash */
    CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_add_hash(lchip, p_mpls_ilm, p_ilm_info),
                    ret, EXIT_STEP1);
    /* write the hardware ad */
    CTC_ERROR_GOTO(_sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls),
                    ret, EXIT_STEP2);
    /* write the hardware hash */
    CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_write_key(lchip, p_mpls_ilm, p_ilm_info),
                    ret, EXIT_STEP3);

    goto EXIT;

EXIT_STEP3:
    _sys_goldengate_mpls_remove_dsmpls(lchip, p_ilm_info->ad_offset);
EXIT_STEP2:
    _sys_goldengate_mpls_ilm_remove_hash(lchip, p_mpls_ilm);
EXIT_STEP1:
    _sys_goldengate_mpls_ilm_remove_spool(lchip, p_ilm_info->ad_offset, p_dsmpls);
EXIT:
    p_gg_mpls_master[lchip]->cur_ilm_num ++;
    return ret;
}

STATIC int32
_sys_goldengate_mpls_ilm_update_one2n(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{
    int32 ret;
    uint32 old_ad_offset = 0;
    sys_mpls_ilm_spool_t* p_old_spool_node = NULL;
    sys_mpls_ilm_spool_t* p_new_spool_node = NULL;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "ctc_mpls_ilm:%p, sys_mpls_ilm:%p \n", p_mpls_ilm, p_ilm_info);

    p_old_spool_node = p_ilm_info->p_cur_hash->spool_ad_ptr;

    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, p_mpls_ilm, p_ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        /* this is a serious problem that soft tbl and hard tbl are not coincident */
        CTC_ERROR_RETURN(ret);
    }

    /* check the db ad entry */
    p_new_spool_node = _sys_goldengate_mpls_ilm_lookup_spool(lchip, p_dsmpls);

    if (NULL == p_new_spool_node)
    {
        if (NULL != p_old_spool_node)
        {
            CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_add_spool(lchip, p_ilm_info, p_dsmpls));
            p_new_spool_node = p_ilm_info->p_spool_ad;
            CTC_ERROR_GOTO(_sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls),
                            ret, EXIT_STEP1);
            CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_write_key(lchip, p_mpls_ilm, p_ilm_info),
                            ret, EXIT_STEP2);
        }
        else
        {
            /* if original ilm is 1:1, need to add the spool node */
            CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip, p_mpls_ilm, p_ilm_info));
            old_ad_offset = p_ilm_info->ad_offset;
            CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_add_spool(lchip, p_ilm_info, p_dsmpls));
            p_new_spool_node = p_ilm_info->p_spool_ad;
            CTC_ERROR_GOTO(_sys_goldengate_mpls_write_dsmpls(lchip, p_ilm_info->ad_offset, p_dsmpls),
                            ret, EXIT_STEP1);
            CTC_ERROR_GOTO(_sys_goldengate_mpls_remove_dsmpls(lchip, old_ad_offset),
                            ret, EXIT_STEP2);
            CTC_ERROR_RETURN( _sys_goldengate_mpls_free_index(lchip, old_ad_offset));
        }
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_db_update_spool(lchip, p_new_spool_node));
        p_ilm_info->ad_offset = p_new_spool_node->ad_index;
        CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_write_key(lchip, p_mpls_ilm, p_ilm_info),
                            ret, EXIT);
    }
    /* delete the old node */
    if (NULL != p_old_spool_node)
    {
        _sys_goldengate_mpls_ilm_remove_spool(lchip, p_old_spool_node->ad_index,
                                &p_old_spool_node->spool_ad);
    }

    /* update hash's ad pointer */
    p_ilm_info->p_cur_hash->spool_ad_ptr = p_new_spool_node;
    p_ilm_info->p_cur_hash->type = p_mpls_ilm->type;
    p_ilm_info->p_cur_hash->id_type = p_mpls_ilm->id_type;
    p_ilm_info->p_cur_hash->model = p_mpls_ilm->model;
    p_ilm_info->p_cur_hash->nh_id = p_mpls_ilm->nh_id;
    p_ilm_info->p_cur_hash->ad_index = p_ilm_info->ad_offset;
    p_ilm_info->p_cur_hash->stats_id = p_mpls_ilm->stats_id;

    goto EXIT;

EXIT_STEP2:
    _sys_goldengate_mpls_remove_dsmpls(lchip, p_ilm_info->ad_offset);
EXIT_STEP1:
     _sys_goldengate_mpls_ilm_remove_spool(lchip, p_ilm_info->ad_offset, p_dsmpls);
EXIT:
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_write_one2n(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info, DsMpls_m* p_dsmpls)
{

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (!p_ilm_info->update)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_add_one2n(lchip, p_mpls_ilm, p_ilm_info, p_dsmpls));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_update_one2n(lchip, p_mpls_ilm, p_ilm_info, p_dsmpls));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_t ilm_info;
    DsMpls_m dsmpls;
    uint8 alloced_key = 0;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&dsmpls, 0, sizeof(DsMpls_m));
    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));

    /* if hash conflict, it will discover here */
    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, p_mpls_ilm, &ilm_info);
    if (CTC_E_MPLS_NO_RESOURCE == ret)
    {
        CTC_ERROR_RETURN(ret);
    }
    else if (CTC_E_ENTRY_EXIST == ret)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_db_lookup_key_by_label(lchip,p_mpls_ilm,
                                    &ilm_info.key_offset, &ilm_info.ad_offset));
        CTC_ERROR_RETURN(_sys_goldengate_mpls_read_dsmpls(lchip,ilm_info.ad_offset, &dsmpls));
        alloced_key = 0;
    }
    else
    {
        /* if  entry not exit , need alloc key index for the ilm */
        CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_alloc_key_index(lchip,p_mpls_ilm, &ilm_info));
        alloced_key = 1;
    }

     /* lookup for mpls entrise in db */
    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip,p_mpls_ilm);
    if(ilm_info.p_cur_hash == NULL)
    {
        ilm_info.p_cur_hash = mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_ilm_hash_t));
        if(!ilm_info.p_cur_hash)
        {
                return CTC_E_NO_MEMORY;
        }
        sal_memset(ilm_info.p_cur_hash,0,sizeof(sys_mpls_ilm_hash_t));
        ilm_info.update  = 0;
   }
   else
   {
        ilm_info.update  = 1;
   }
     /* business operation */
    CTC_ERROR_GOTO(_sys_goldengate_mpls_ilm_common(lchip,p_mpls_ilm, &ilm_info, &dsmpls), ret, EXIT);

    CTC_ERROR_GOTO(p_gg_mpls_master[lchip]->build_ilm[p_mpls_ilm->type](lchip,p_mpls_ilm, &ilm_info, &dsmpls), ret, EXIT);

    /* write mpls ilm entry */
    CTC_ERROR_GOTO(p_gg_mpls_master[lchip]->write_ilm[p_mpls_ilm->type](lchip,p_mpls_ilm, &ilm_info, &dsmpls), ret, EXIT);

    return CTC_E_NONE;

EXIT:
    if (alloced_key)
    {
        _sys_goldengate_mpls_remove_key(lchip, ilm_info.key_offset);
    }

    if (0 == ilm_info.update)
    {
        mem_free(ilm_info.p_cur_hash);
    }

    return ret;
}

STATIC int32
_sys_goldengate_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_t ilm_info;
    DsMpls_m dsmpls;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&dsmpls, 0, sizeof(DsMpls_m));
    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));

    /* if hash conflict, it will discover here */
    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip,p_mpls_ilm, &ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

     /* lookup for mpls entrise in db */
    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip,p_mpls_ilm);
    if (ilm_info.p_cur_hash == NULL)
    {
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "Entry not exist");
        return CTC_E_NOT_EXIST;
    }
    ilm_info.update = 1;

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip,p_mpls_ilm, &ilm_info));

     /* business operation */
    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_common(lchip,p_mpls_ilm, &ilm_info, &dsmpls));

    CTC_ERROR_RETURN(p_gg_mpls_master[lchip]->build_ilm[p_mpls_ilm->type](lchip,p_mpls_ilm, &ilm_info, &dsmpls));

    /* write mpls ilm entry */
    CTC_ERROR_RETURN(p_gg_mpls_master[lchip]->write_ilm[p_mpls_ilm->type](lchip,p_mpls_ilm, &ilm_info, &dsmpls));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_remove_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    int32 ret = CTC_E_NONE;
    sys_mpls_ilm_t ilm_info;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "label:%u, spaceid:%u\n", p_mpls_ilm->label,p_mpls_ilm->spaceid);

    /* lookup for mpls entrise */
    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip,p_mpls_ilm);
    if (NULL == ilm_info.p_cur_hash)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, p_mpls_ilm, &ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        CTC_ERROR_RETURN(ret);
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip,p_mpls_ilm, &ilm_info));

    CTC_ERROR_RETURN(_sys_goldengate_mpls_remove_key(lchip,ilm_info.key_offset));

    if (NULL != ilm_info.p_cur_hash->spool_ad_ptr)
    {
        ret = _sys_goldengate_mpls_ilm_remove_spool(lchip, ilm_info.ad_offset, &(ilm_info.p_cur_hash->spool_ad_ptr->spool_ad));

        if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
        {
            /* now the space that spool_ad_ptr point is invalid */
            ilm_info.p_cur_hash->spool_ad_ptr = NULL;
        }
    }
    else
    {
        /* for VPLS and VPWS */
        CTC_ERROR_RETURN(_sys_goldengate_mpls_remove_dsmpls(lchip,ilm_info.ad_offset));
        CTC_ERROR_RETURN(_sys_goldengate_mpls_free_index(lchip,ilm_info.ad_offset));
    }


   if(ilm_info.p_cur_hash->bind_dsfwd)
    {
        sys_nh_update_dsnh_param_t update_dsnh;
        update_dsnh.data = NULL;
        update_dsnh.updateAd = NULL;
        CTC_ERROR_RETURN(sys_goldengate_nh_bind_dsfwd_cb(lchip, ilm_info.p_cur_hash->nh_id, &update_dsnh));
    }


    /* delete ref spool */
    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_remove_hash(lchip,p_mpls_ilm));

     mem_free(ilm_info.p_cur_hash);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_set_ilm_property_aps(uint8 lchip,
                                ctc_mpls_property_t *p_prop_info,
                                sys_mpls_ilm_t* p_ilm_info,
                                DsMpls_m* p_dsmpls)
{
    ctc_mpls_ilm_t* p_mpls_ilm = (ctc_mpls_ilm_t*)p_prop_info->value;

    if ((p_mpls_ilm->aps_select_grp_id) != 0xFFFF)
    {
        /* no aps -> aps */
        if ((p_mpls_ilm->aps_select_grp_id) >= TABLE_MAX_INDEX(DsApsBridge_t))
        {
            CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
        }
        SetDsMpls(V, apsSelectValid_f, p_dsmpls, 1);
        if (p_mpls_ilm->aps_select_protect_path)
        {
            SetDsMpls(V, apsSelectProtectingPath_f, p_dsmpls, 1);
        }
        else
        {
            SetDsMpls(V, apsSelectProtectingPath_f, p_dsmpls, 0);
        }

        SetDsMpls(V, apsSelectGroupId_f, p_dsmpls, p_mpls_ilm->aps_select_grp_id & 0x1FFF);
        CTC_SET_FLAG(p_ilm_info->p_cur_hash->id_type, CTC_MPLS_ID_APS_SELECT);
    }
    else
    {
        /* aps -> no aps */

        SetDsMpls(V, apsSelectValid_f, p_dsmpls, 0);
        SetDsMpls(V, apsSelectProtectingPath_f, p_dsmpls, 0);
        SetDsMpls(V, apsSelectGroupId_f, p_dsmpls, 0);

        CTC_UNSET_FLAG(p_ilm_info->p_cur_hash->id_type, CTC_MPLS_ID_APS_SELECT);
    }

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_mpls_set_ilm_bind_route_mac(uint8 lchip, mac_addr_t* p_mac_addr,  DsMpls_m* p_dsmpls)
{
    uint8 route_mac_index = 0;

    route_mac_index = GetDsMpls(V, routerMacProfile_f, p_dsmpls);
    if (route_mac_index) /*update*/
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, route_mac_index));
        CTC_ERROR_RETURN(sys_goldengate_l3if_binding_inner_route_mac(lchip, p_mac_addr, &route_mac_index));
    }
    else /*add*/
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_binding_inner_route_mac(lchip, p_mac_addr, &route_mac_index));
    }

    SetDsMpls(V, routerMacProfile_f, p_dsmpls, route_mac_index);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_mpls_set_ilm_unbind_route_mac(uint8 lchip, mac_addr_t* p_mac_addr, DsMpls_m* p_dsmpls)
{
    uint8 route_mac_index = 0;

    route_mac_index = GetDsMpls(V, routerMacProfile_f, p_dsmpls);
    CTC_ERROR_RETURN(sys_goldengate_l3if_unbinding_inner_route_mac(lchip, route_mac_index));
    SetDsMpls(V,  routerMacProfile_f, p_dsmpls, 0);

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_set_ilm_property_route_mac(uint8 lchip, mac_addr_t* p_mac_addr, DsMpls_m* p_dsmpls)
{
    uint8 mac_0[6] = {0};
    uint8 mac_f[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

    /*
        1. check mac 0.0.0 or ffff.ffff.ffff
        2. if mac invalid ,unbind the reference
        3. if mac valid, bind the reference
    */

    if ((sal_memcmp(p_mac_addr, &mac_0, sizeof(mac_addr_t)))
        && (sal_memcmp(p_mac_addr, &mac_f, sizeof(mac_addr_t))))
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_set_ilm_bind_route_mac(lchip, p_mac_addr, p_dsmpls));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_set_ilm_unbind_route_mac(lchip, p_mac_addr, p_dsmpls));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_set_ilm_property(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, sys_mpls_ilm_t* p_ilm_info)
{
    uint32 cmd      = 0;
    uint32 field    = 0;
    int32 ret = CTC_E_NONE;
    ctc_mpls_property_t* p_prop_info = p_ilm_info->p_prop_info;
    DsMpls_m dsmpls;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "property type:%u\n", p_prop_info->property_type);

    sal_memset(&dsmpls, 0, sizeof(DsMpls_m));

    /* lookup for mpls entrise */
    p_ilm_info->p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip, p_mpls_ilm);
    if (NULL == p_ilm_info->p_cur_hash)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, p_mpls_ilm, p_ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        /* this is a serious problem that soft tbl and hard tbl are not coincident */
        CTC_ERROR_RETURN(ret);
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip, p_mpls_ilm, p_ilm_info));

    cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_info->ad_offset, cmd, &dsmpls));

    switch(p_prop_info->property_type)
    {
        case SYS_MPLS_ILM_QOS_DOMAIN:
            field = *((uint32 *)p_prop_info->value);
            if ( (field > SYS_MPLS_MAX_DOMAIN) &&  (0xFF != field))
            {
                CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
            }
            field = (0xFF == *((uint32 *)p_prop_info->value)) ? 0:(*((uint32 *)p_prop_info->value));
            SetDsMpls(V, qosDomain_f, &dsmpls, field);
            field = (0xFF == *((uint32 *)p_prop_info->value)) ? 0:1;
            SetDsMpls(V, overwritePriority_f, &dsmpls, field);

            break;

        case SYS_MPLS_ILM_APS_SELECT:

            _sys_goldengate_mpls_set_ilm_property_aps(lchip,
                        p_prop_info,
                        p_ilm_info,
                        &dsmpls);
            p_mpls_ilm->id_type = p_ilm_info->p_cur_hash->id_type;

            break;
        case SYS_MPLS_ILM_OAM_LMEN:

            field = *((uint32 *)p_prop_info->value);
            SetDsMpls(V, lmEn_f, &dsmpls, field);

            break;
        case SYS_MPLS_ILM_OAM_IDX:
            field = *((uint32 *)p_prop_info->value);
            SetDsMpls(V, mplsOamIndex_f, &dsmpls, field);

            break;
        case SYS_MPLS_ILM_OAM_CHK:
            field = *((uint32 *)p_prop_info->value);
            if (field > SYS_MPLS_MAX_OAM_CHK_TYPE)
            {
                CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
            }
            SetDsMpls(V, oamCheckType_f, &dsmpls, field);

            break;
        case SYS_MPLS_ILM_OAM_VCCV_BFD:
            field = *((uint32 *)p_prop_info->value);
            SetDsMpls(V, cwExist_f, &dsmpls, field);

            break;
        case SYS_MPLS_ILM_ROUTE_MAC:

            CTC_ERROR_RETURN(_sys_goldengate_mpls_set_ilm_property_route_mac(lchip,
                            (mac_addr_t *)p_prop_info->value, &dsmpls));

            break;

        case SYS_MPLS_ILM_LLSP:
            field = *((uint32 *)p_prop_info->value);
            if (0xFF == field)
            {
                SetDsMpls(V, llspValid_f, &dsmpls, 0);
                SetDsMpls(V, llspPriority_f, &dsmpls, 0);
            }
            else
            {
                if (field > 7)
                {
                    CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
                }
                SetDsMpls(V, llspValid_f, &dsmpls, 1);
                SetDsMpls(V, llspPriority_f, &dsmpls, field);
                SetDsMpls(V, overwritePriority_f, &dsmpls, 1);
            }
            break;

        default:
            break;
    }

    /* for vpws and vpls, there is no spool node write hard tbl directly*/
    if ((CTC_MPLS_LABEL_TYPE_NORMAL == p_ilm_info->p_cur_hash->type)
        || (CTC_MPLS_LABEL_TYPE_L3VPN == p_ilm_info->p_cur_hash->type)
        || (CTC_MPLS_LABEL_TYPE_GATEWAY == p_ilm_info->p_cur_hash->type))
    {
        _sys_goldengate_mpls_ilm_update_one2n(lchip, p_mpls_ilm, p_ilm_info, &dsmpls);
    }

    cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ilm_info->ad_offset, cmd, &dsmpls));


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mpls_ilm_data_check_mask(ctc_mpls_ilm_t* p_mpls_ilm)
{
    /* data check */
    if ((p_mpls_ilm)->id_type >= CTC_MPLS_MAX_ID)
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->id_type & CTC_MPLS_ID_SERVICE
        && (p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_VPLS
        && (p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_VPWS)
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->id_type & CTC_MPLS_ID_APS_SELECT
        && ((p_mpls_ilm)->flw_vrf_srv_aps.aps_select_grp_id >= TABLE_MAX_INDEX(DsApsBridge_t)
        || (p_mpls_ilm)->aps_select_grp_id >= TABLE_MAX_INDEX(DsApsBridge_t)))
    {
        return CTC_E_APS_INVALID_GROUP_ID;
    }
    if ((p_mpls_ilm)->id_type & CTC_MPLS_ID_SERVICE &&
        (p_mpls_ilm)->flw_vrf_srv_aps.service_id == 0)
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->id_type & CTC_MPLS_ID_VRF
        && ((p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_L3VPN
        && p_mpls_ilm->type != CTC_MPLS_LABEL_TYPE_GATEWAY))
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->id_type & CTC_MPLS_ID_VRF &&
        (p_mpls_ilm)->flw_vrf_srv_aps.vrf_id > SYS_MPLS_MAX_L3VPN_VRF_ID)
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->type >= CTC_MPLS_MAX_LABEL_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }
    if ((p_mpls_ilm)->model >= CTC_MPLS_MAX_TUNNEL_MODE)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* data mask */
    if ((p_mpls_ilm)->type == CTC_MPLS_LABEL_TYPE_VPWS
        || (p_mpls_ilm)->type == CTC_MPLS_LABEL_TYPE_VPLS)
    {
        (p_mpls_ilm)->model = CTC_MPLS_TUNNEL_MODE_PIPE;
    }
    if ((p_mpls_ilm)->type == CTC_MPLS_LABEL_TYPE_L3VPN)
    {
        (p_mpls_ilm)->cwen = FALSE;
    }
    if ((((p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_VPLS)
        && ((p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_GATEWAY)
        && ((p_mpls_ilm)->type != CTC_MPLS_LABEL_TYPE_L3VPN))
        || (p_mpls_ilm)->pwid > SYS_MPLS_VPLS_SRC_PORT_NUM)
    {
        (p_mpls_ilm)->pwid = 0xffff;
    }
    if ((p_mpls_ilm)->cwen)
    {
        (p_mpls_ilm)->cwen = TRUE;
    }
    if ((p_mpls_ilm)->logic_port_type)
    {
        (p_mpls_ilm)->logic_port_type = TRUE;
    }

    if ((p_mpls_ilm->fid) && (p_mpls_ilm->nh_id))
    {
        /*
            fid and nexthop share hw tbl feild
        */
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

#define ________API_____________
int32
sys_goldengate_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);

    SYS_MPLS_ILM_KEY_CHECK(p_mpls_ilm);
    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_data_check_mask(p_mpls_ilm));

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (p_gg_mpls_master[lchip]->cur_ilm_num >= SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_MPLS))
    {
        return CTC_E_MPLS_NO_RESOURCE;
    }

    /* check vrf id */
    if (CTC_FLAG_ISSET(p_mpls_ilm->id_type, CTC_MPLS_ID_VRF))
    {
        if (0 != p_mpls_ilm->flw_vrf_srv_aps.vrf_id)
        {
           p_mpls_ilm->vrf_id = p_mpls_ilm->flw_vrf_srv_aps.vrf_id;
        }

        if (p_mpls_ilm->vrf_id >= sys_goldengate_l3if_get_max_vrfid(lchip, MAX_CTC_IP_VER))
        {
            return CTC_E_MPLS_VRF_ID_ERROR;
        }
    }

    SYS_MPLS_LOCK(lchip);

    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_add_ilm(lchip,p_mpls_ilm));

    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_data_check_mask(p_mpls_ilm));
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* check vrf id */
    if (CTC_IS_BIT_SET(p_mpls_ilm->id_type, CTC_MPLS_ID_VRF))
    {
        if (0 != p_mpls_ilm->flw_vrf_srv_aps.vrf_id)
        {
           p_mpls_ilm->vrf_id = p_mpls_ilm->flw_vrf_srv_aps.vrf_id;
        }

        if (p_mpls_ilm->vrf_id >= sys_goldengate_l3if_get_max_vrfid(lchip, MAX_CTC_IP_VER))
        {
            return CTC_E_MPLS_VRF_ID_ERROR;
        }
    }

    SYS_MPLS_LOCK(lchip);

    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_update_ilm(lchip,p_mpls_ilm));

    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
 @brief function of remove mpls ilm entry

 @param[in] p_ipuc_param, parameters used to remove mpls ilm entry

 @return CTC_E_XXX
 */
int32
sys_goldengate_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_ilm);

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);

    SYS_MPLS_LOCK(lchip);

    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_remove_ilm(lchip,p_mpls_ilm));
    (p_gg_mpls_master[lchip]->cur_ilm_num) --;

    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_get_ilm(uint8 lchip, uint32 nh_id[SYS_GG_MAX_ECPN], ctc_mpls_ilm_t* p_mpls_ilm)
{
    uint8 i = 0;
    sys_mpls_ilm_hash_t* p_db_hash = NULL;
    sys_nh_info_dsnh_t nh_info;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_ilm);
    SYS_MPLS_DUMP_ILM(p_mpls_ilm);
    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "nh id[]:%d,%d,%d,%d,%d,%d,%d,%d\n",
                     nh_id[0], nh_id[1], nh_id[2], nh_id[3], nh_id[4], nh_id[5], nh_id[6], nh_id[7]);

    sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

    for (i = 0; i < SYS_GG_MAX_ECPN; i++)
    {
        nh_id[i] = CTC_INVLD_NH_ID;
    }

    SYS_MPLS_LOCK(lchip);
    /* lookup for ipuc entrise */
    p_db_hash = _sys_goldengate_mpls_ilm_get_hash(lchip, p_mpls_ilm);
    if (!p_db_hash)
    {
        CTC_RETURN_MPLS_UNLOCK(lchip, CTC_E_ENTRY_NOT_EXIST);
    }

    _sys_goldengate_mpls_map_ilm_sys2ctc(lchip, p_mpls_ilm ,p_db_hash);

    if ((0 == p_db_hash->nh_id) && (1 == p_mpls_ilm->pop))
    {
        p_db_hash->nh_id = 1;
    }

    if ((CTC_MPLS_LABEL_TYPE_VPLS != p_db_hash->type)
        && (CTC_MPLS_LABEL_TYPE_L3VPN != p_db_hash->type)
        && (CTC_MPLS_LABEL_TYPE_GATEWAY != p_db_hash->type))
    {
        CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, sys_goldengate_nh_get_nhinfo(lchip, p_db_hash->nh_id, &nh_info));
    }

    if (nh_info.ecmp_valid)
    {
        for (i = 0; i < nh_info.valid_cnt; i++)
        {
            nh_id[i] = nh_info.nh_array[i];
        }
    }
    else
    {
        nh_id[0] = p_db_hash->nh_id;
    }

    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro_info)
{
    sys_mpls_ilm_t ilm_info;
    ctc_mpls_ilm_t mpls_ilm;
    uint32 nh_id[SYS_GG_MAX_ECPN] = {0};

    SYS_MPLS_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_mpls_pro_info);

    sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));
    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));

    mpls_ilm.label = p_mpls_pro_info->label;
    mpls_ilm.spaceid = p_mpls_pro_info->space_id;
    ilm_info.p_prop_info = p_mpls_pro_info;

    CTC_ERROR_RETURN(sys_goldengate_mpls_get_ilm(lchip, nh_id, &mpls_ilm));

    SYS_MPLS_LOCK(lchip);

    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_set_ilm_property(lchip, &mpls_ilm, &ilm_info));

    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    ctc_mpls_ilm_t sys_mpls_param;

    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_pw);

    if ((CTC_MPLS_L2VPN_VPWS != p_mpls_pw->l2vpntype) &&
        (CTC_MPLS_L2VPN_VPLS != p_mpls_pw->l2vpntype))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_mpls_pw->igmp_snooping_enable)
    {
        return CTC_E_NOT_SUPPORT;
    }

    sal_memset(&sys_mpls_param, 0 , sizeof(ctc_mpls_ilm_t));
    CTC_ERROR_RETURN(_sys_goldengate_mpls_map_pw_ctc2sys(lchip, p_mpls_pw, &sys_mpls_param));
    SYS_MPLS_LOCK(lchip);
    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_add_ilm(lchip,&sys_mpls_param));
    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_goldengate_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw)
{
    ctc_mpls_ilm_t sys_mpls_param;

    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_pw);

    if ((CTC_MPLS_L2VPN_VPWS != p_mpls_pw->l2vpntype) &&
        (CTC_MPLS_L2VPN_VPLS != p_mpls_pw->l2vpntype))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_map_pw_ctc2sys(lchip, p_mpls_pw, &sys_mpls_param));

    SYS_MPLS_LOCK(lchip);
    CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, _sys_goldengate_mpls_remove_ilm(lchip,&sys_mpls_param));
    SYS_MPLS_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_MPLS_INIT_CHECK(lchip);

    specs_info->used_size = p_gg_mpls_master[lchip]->cur_ilm_num;

    return CTC_E_NONE;
}

int32
_sys_goldengate_mpls_init_gal(uint8 lchip, uint8 is_add)
{
    ctc_mpls_ilm_t mpls_param;

    SYS_MPLS_INIT_CHECK(lchip);

    sal_memset(&mpls_param, 0, sizeof(ctc_mpls_ilm_t));
    mpls_param.label = 13;
    mpls_param.pop = 1;
    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mpls_add_ilm(lchip, &mpls_param));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_del_ilm(lchip, &mpls_param));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info)
{

    uint32 field_val = 0;
    uint32 cmd = 0;
    uint32 ftm_mpls_num = 0;
    ctc_spool_t spool;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    /* get num of mpls resource */
    sys_goldengate_ftm_query_table_entry_num(lchip, DsMpls_t, &ftm_mpls_num);
    if ((0 == SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_MPLS))
        || (0 == ftm_mpls_num))
    {
        return CTC_E_NONE;
    }

    if (p_gg_mpls_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_mpls_master[lchip] = mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_master_t));
    if (NULL == p_gg_mpls_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_mpls_master[lchip], 0, sizeof(sys_mpls_master_t));

    p_gg_mpls_master[lchip]->p_mpls_hash  = ctc_hash_create(
            512,
            ftm_mpls_num/512,
            (hash_key_fn)_sys_goldengate_mpls_hash_make,
            (hash_cmp_fn)_sys_goldengate_mpls_hash_cmp);


    spool.lchip = lchip;
    spool.block_num = 512;
    spool.block_size = ftm_mpls_num/512;
    spool.max_count = ftm_mpls_num;
    spool.user_data_size = sizeof(sys_mpls_ilm_spool_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_mpls_spool_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_mpls_spool_cmp;
    p_gg_mpls_master[lchip]->ad_spool = ctc_spool_create(&spool);

    /* call back function */
    p_gg_mpls_master[lchip]->build_ilm[CTC_MPLS_LABEL_TYPE_NORMAL] = _sys_goldengate_mpls_ilm_normal;
    p_gg_mpls_master[lchip]->build_ilm[CTC_MPLS_LABEL_TYPE_L3VPN]  = _sys_goldengate_mpls_ilm_l3vpn;
    p_gg_mpls_master[lchip]->build_ilm[CTC_MPLS_LABEL_TYPE_VPWS]   = _sys_goldengate_mpls_ilm_vpws;
    p_gg_mpls_master[lchip]->build_ilm[CTC_MPLS_LABEL_TYPE_VPLS]   = _sys_goldengate_mpls_ilm_vpls;
    p_gg_mpls_master[lchip]->build_ilm[CTC_MPLS_LABEL_TYPE_GATEWAY] = _sys_goldengate_mpls_ilm_gateway;

    p_gg_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_NORMAL] = _sys_goldengate_mpls_ilm_write_one2n;
    p_gg_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_L3VPN]  = _sys_goldengate_mpls_ilm_write_one2n;
    p_gg_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_VPWS]   = _sys_goldengate_mpls_ilm_write_one2one;
    p_gg_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_VPLS]   = _sys_goldengate_mpls_ilm_write_one2one;
    p_gg_mpls_master[lchip]->write_ilm[CTC_MPLS_LABEL_TYPE_GATEWAY] = _sys_goldengate_mpls_ilm_write_one2n;

    field_val = 0;
    cmd = DRV_IOW(IpeMplsCtl_t, IpeMplsCtl_globalLabelSpace_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &field_val));

    field_val = 0;
    cmd = DRV_IOR(IpeIntfMapReserved2_t, IpeIntfMapReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &field_val));

    field_val = field_val | 0x6;
    cmd = DRV_IOW(IpeIntfMapReserved2_t, IpeIntfMapReserved2_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &field_val));

    SYS_MPLS_CREAT_LOCK(lchip);
    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_MPLS,  sys_goldengate_mpls_ftm_cb);

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_MPLS, sys_goldengate_mpls_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_mpls_wb_restore(lchip));
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_init_gal(lchip, 1));
    sys_goldengate_ftm_register_rsv_key_cb(lchip, SYS_FTM_HASH_KEY_MPLS, _sys_goldengate_mpls_init_gal);

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_mpls_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_mpls_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free dsmpls data*/
    ctc_spool_free(p_gg_mpls_master[lchip]->ad_spool);

    /*free mpls key*/
    ctc_hash_traverse(p_gg_mpls_master[lchip]->p_mpls_hash, (hash_traversal_fn)_sys_goldengate_mpls_free_node_data, NULL);
    ctc_hash_free(p_gg_mpls_master[lchip]->p_mpls_hash);

    sal_mutex_destroy(p_gg_mpls_master[lchip]->mutex);
    mem_free(p_gg_mpls_master[lchip]);

    return CTC_E_NONE;
}

#define ___________DEBUG________________

STATIC int32
_sys_goldengate_show_ilm_info_brief(sys_mpls_ilm_hash_t* p_ilm_hash, void* p_data)
{
    uint8 lchip = 0;
    ctc_mpls_ilm_t mpls_ilm;
    sys_mpls_ilm_t ilm_info;
    sys_mpls_show_info_t* p_space_info = NULL;
    char *arr_type[] = {
            "Normal",
            "L3VPN",
            "VPWS",
            "VPLS",
            "GATEWAY"
        };
    char *arr_encap[] = {
            "Tagged",
            "Raw",
        };

    CTC_PTR_VALID_CHECK(p_data);

    p_space_info = (sys_mpls_show_info_t*)p_data;
    lchip = p_space_info->lchip;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));
    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));

    mpls_ilm.spaceid = p_ilm_hash->spaceid;
    mpls_ilm.label = p_ilm_hash->label;

    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip,&mpls_ilm);
    if (NULL == ilm_info.p_cur_hash)
    {
        /* continue traverse the hash */
        return CTC_E_NONE;
    }

    _sys_goldengate_mpls_map_ilm_sys2ctc(lchip, &mpls_ilm ,ilm_info.p_cur_hash);

    if ((0 == ilm_info.p_cur_hash->nh_id) && (1 == mpls_ilm.pop))
    {
        ilm_info.p_cur_hash->nh_id = 1;
    }

    /** print one entry */
    SYS_MPLS_DUMP("%-10d%-10d%-10s",
                p_ilm_hash->spaceid, p_ilm_hash->label, arr_type[ilm_info.p_cur_hash->type]);

    if (0 == ilm_info.p_cur_hash->nh_id)
    {
        SYS_MPLS_DUMP("%-10s", "-");
    }
    else
    {
        SYS_MPLS_DUMP("%-10u", ilm_info.p_cur_hash->nh_id);
    }

    if ((CTC_MPLS_LABEL_TYPE_VPWS == mpls_ilm.type)
        || (CTC_MPLS_LABEL_TYPE_VPLS == mpls_ilm.type)
        || (CTC_MPLS_LABEL_TYPE_GATEWAY == mpls_ilm.type))
    {
        SYS_MPLS_DUMP("%-10s", arr_encap[mpls_ilm.pw_mode]);
    }
    else
    {
        SYS_MPLS_DUMP("%-10s", "-");
    }
    if ((0xFFFF == mpls_ilm.fid) || (0 == mpls_ilm.fid) || (0 != ilm_info.p_cur_hash->nh_id))
    {
        SYS_MPLS_DUMP("%-10s", "-");
    }
    else
    {
        SYS_MPLS_DUMP("%-10u", mpls_ilm.fid);
    }

    if (CTC_FLAG_ISSET(mpls_ilm.id_type, CTC_MPLS_ID_VRF))
    {
        SYS_MPLS_DUMP("%-10u",mpls_ilm.vrf_id);
    }
    else
    {
        SYS_MPLS_DUMP(" %-10s", "-");
    }

    if ((0x3fff == mpls_ilm.pwid) || (0 == mpls_ilm.pwid))
    {
        SYS_MPLS_DUMP("%-10s\n", "-");
    }
    else
    {
        SYS_MPLS_DUMP("%-10u\n", mpls_ilm.pwid);
    }
    /** end */

    ++(p_space_info->count);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_show_ilm_info_detail(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    sys_mpls_ilm_hash_t* p_cur_hash = NULL;
    int32 ret = CTC_E_NONE;
    char* arr_type[] = {
            "Normal",
            "L3VPN",
            "VPWS",
            "VPLS",
            "GATEWAY"
        };
    char* arr_model[] = {
            "uniform",
            "short pipe",
            "pipe",
        };
    char* arr_encap[] = {
            "Raw",
            "Tagged",
        };

    sys_mpls_ilm_t ilm_info;

    sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));

    ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip, p_mpls_ilm);
    if (NULL == ilm_info.p_cur_hash)
    {
        /* continue traverse the hash */
        return CTC_E_NONE;
    }
    p_cur_hash = ilm_info.p_cur_hash;

    ret = _sys_goldengate_mpls_ilm_get_key_index(lchip, p_mpls_ilm, &ilm_info);
    if (CTC_E_ENTRY_EXIST != ret)
    {
        CTC_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST);
    }

    CTC_ERROR_RETURN(_sys_goldengate_mpls_ilm_get_ad_offset(lchip, p_mpls_ilm, &ilm_info));

    SYS_MPLS_DUMP("-----------------------Detail Information-------------------\r\n");
    SYS_MPLS_DUMP("%-30s:%-6u \n", "Label", p_mpls_ilm->label);
    SYS_MPLS_DUMP("%-30s:%-6u \n", "Space Id", p_mpls_ilm->spaceid);

    if (ilm_info.p_cur_hash->is_interface)
    {
        SYS_MPLS_DUMP("%-30s:%-6s \n", "Is Interface", "Yes");
    }
    else
    {
        SYS_MPLS_DUMP("%-30s:%-6s \n", "Is Interface", "No");
    }
    SYS_MPLS_DUMP("%-30s:%-6s \n", "Type", arr_type[p_cur_hash->type]);

    if (0 != p_cur_hash->nh_id)
    {
        SYS_MPLS_DUMP("%-30s:%-6u \n", "Nexthop Id", p_cur_hash->nh_id);
    }


    switch(p_cur_hash->type)
    {
        case CTC_MPLS_LABEL_TYPE_NORMAL:
            SYS_MPLS_DUMP("%-30s:%-6s \n", "Model", arr_model[p_cur_hash->model]);

            break;
        case CTC_MPLS_LABEL_TYPE_L3VPN:
            SYS_MPLS_DUMP("%-30s:%-6s \n", "Model", arr_model[p_cur_hash->model]);
            SYS_MPLS_DUMP("%-30s:%-6u \n", "VRF", p_mpls_ilm->vrf_id);
            break;
        case CTC_MPLS_LABEL_TYPE_VPWS:
            SYS_MPLS_DUMP("%-30s:%-6s \n", "Encap Mode", arr_encap[p_mpls_ilm->pw_mode]);
            break;
        case CTC_MPLS_LABEL_TYPE_VPLS:
            SYS_MPLS_DUMP("%-30s:%-6u \n", "Fid", p_mpls_ilm->fid);
            SYS_MPLS_DUMP("%-30s:%-6u \n", "Logic Port", p_mpls_ilm->pwid);
            if (p_mpls_ilm->logic_port_type)
            {
                SYS_MPLS_DUMP("%-30s:%-6s \n", "HVPLS", "No");
            }
            else
            {
                SYS_MPLS_DUMP("%-30s:%-6s \n", "HVPLS", "Yes");
            }

            SYS_MPLS_DUMP("%-30s:%-6s \n", "Encap Mode", arr_encap[p_mpls_ilm->pw_mode]);
            break;
        default:
            break;
    }

    if (p_mpls_ilm->id_type & CTC_MPLS_ID_APS_SELECT)
    {
        SYS_MPLS_DUMP("%-30s:%-6u \n", "APS Group ID", p_mpls_ilm->aps_select_grp_id);
        if (p_mpls_ilm->aps_select_protect_path)
        {
            SYS_MPLS_DUMP("%-30s:%-6s \n", "APS Path Type", "protecting");
        }
        else
        {
            SYS_MPLS_DUMP("%-30s:%-6s \n", "APS Path Type", "working");
        }
    }

    if (p_mpls_ilm->decap)
    {
        SYS_MPLS_DUMP("%-30s:%-6s \n", "Decap", "Yes");
    }

    if (p_mpls_ilm->cwen)
    {
        SYS_MPLS_DUMP("%-30s:%-6u \n", "Control Word", p_mpls_ilm->cwen);
    }

    if (0 != p_cur_hash->stats_id)
    {
        SYS_MPLS_DUMP("%-30s:%-6u \n", "Stats Id", p_cur_hash->stats_id);
    }

    if (0 != p_mpls_ilm->oam_en)
    {
        SYS_MPLS_DUMP("%-30s:%-6s \n", "OAM", "Yes");
    }

    if (0 != p_mpls_ilm->l2vpn_oam_id)
    {
        SYS_MPLS_DUMP("%-30s:%-6u \n", "Oam Id", p_mpls_ilm->l2vpn_oam_id);
    }

    SYS_MPLS_DUMP("%-30s:%-6u \n", "DsUserIdTunnelMplsHashKey", ilm_info.key_offset);
    SYS_MPLS_DUMP("%-30s:%-6u \n", "DsMpls", ilm_info.ad_offset);

    SYS_MPLS_DUMP("----------------------------------------------------------------\r\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_show_ilm_info(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm)
{
    sys_mpls_show_info_t space_info;

    sal_memset(&space_info, 0, sizeof(sys_mpls_show_info_t));

    space_info.lchip = lchip;

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");
    SYS_MPLS_DUMP("%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s\n",
                    "SPACE", "LABEL", "TYPE", "NHID", "PW_Mode", "FID", "VRF", "LOGIC_PORT");
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");

    ctc_hash_traverse_through(p_gg_mpls_master[lchip]->p_mpls_hash,
                (hash_traversal_fn)_sys_goldengate_show_ilm_info_brief,
                &space_info);

    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");
    SYS_MPLS_DUMP("Count:%u\n", space_info.count);
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_show_ilm_info_by_space(sys_mpls_ilm_hash_t* p_ilm_hash, void* p_data)
{
    uint8 lchip = 0;
    sys_mpls_ilm_t ilm_info;
    ctc_mpls_ilm_t mpls_ilm;
    sys_nh_info_dsnh_t nh_info;
    sys_mpls_show_info_t* p_space_info = NULL;
    uint8 i = 0;
    uint32 nh_id[SYS_GG_MAX_ECPN] = {0};
    char *arr_type[] = {
            "Normal",
            "L3VPN",
            "VPWS",
            "VPLS",
            "GATEWAY"
        };
    char *arr_encap[] = {
            "Tagged",
            "Raw",
        };

    if (NULL == p_data)
    {
        return CTC_E_NONE;
    }

    p_space_info = (sys_mpls_show_info_t*)p_data;
    lchip = p_space_info->lchip;

    if (p_ilm_hash->spaceid == p_space_info->space_id)
    {
        sal_memset(&ilm_info, 0, sizeof(sys_mpls_ilm_t));
        sal_memset(&mpls_ilm, 0, sizeof(ctc_mpls_ilm_t));
        sal_memset(&nh_info, 0, sizeof(sys_nh_info_dsnh_t));

        for (i = 0; i < SYS_GG_MAX_ECPN; i++)
        {
            nh_id[i] = CTC_INVLD_NH_ID;
        }

        mpls_ilm.label = p_ilm_hash->label;
        mpls_ilm.spaceid = p_ilm_hash->spaceid;
        ilm_info.p_cur_hash = _sys_goldengate_mpls_ilm_get_hash(lchip, &mpls_ilm);
        if (NULL == ilm_info.p_cur_hash)
        {
            return CTC_E_NONE;
        }

        _sys_goldengate_mpls_map_ilm_sys2ctc(lchip, &mpls_ilm ,ilm_info.p_cur_hash);

        if ((0 == ilm_info.p_cur_hash->nh_id) && (1 == mpls_ilm.pop))
        {
            ilm_info.p_cur_hash->nh_id = 1;
        }

        if ((CTC_MPLS_LABEL_TYPE_VPLS != ilm_info.p_cur_hash->type)
            && (CTC_MPLS_LABEL_TYPE_L3VPN != ilm_info.p_cur_hash->type)
            && (CTC_MPLS_LABEL_TYPE_GATEWAY != ilm_info.p_cur_hash->type))
        {
            CTC_ERROR_RETURN_MPLS_UNLOCK(lchip, sys_goldengate_nh_get_nhinfo(lchip, ilm_info.p_cur_hash->nh_id, &nh_info));
        }

        if (nh_info.ecmp_valid)
        {
            for (i = 0; i < nh_info.valid_cnt; i++)
            {
                nh_id[i] = nh_info.nh_array[i];
            }
        }
        else
        {
            nh_id[0] = ilm_info.p_cur_hash->nh_id;
        }

        /** print one entry */
        SYS_MPLS_DUMP(" %-10d %-10s",
                    p_ilm_hash->label, arr_type[ilm_info.p_cur_hash->type]);

        if (0 == ilm_info.p_cur_hash->nh_id)
        {
            SYS_MPLS_DUMP(" %-10s", "-");
        }
        else
        {
            SYS_MPLS_DUMP(" %-10u", nh_id[0]);
        }

        if ((CTC_MPLS_LABEL_TYPE_VPWS == mpls_ilm.type)
            || (CTC_MPLS_LABEL_TYPE_VPLS == mpls_ilm.type)
            || (CTC_MPLS_LABEL_TYPE_GATEWAY == mpls_ilm.type))
        {
            SYS_MPLS_DUMP(" %-10s", arr_encap[mpls_ilm.pw_mode]);
        }
        else
        {
            SYS_MPLS_DUMP(" %-10s", "-");
        }
        if ((0xFFFF == mpls_ilm.fid) || (0 == mpls_ilm.fid) || (0 != ilm_info.p_cur_hash->nh_id))
        {
            SYS_MPLS_DUMP(" %-10s", "-");
        }
        else
        {
            SYS_MPLS_DUMP(" %-10u", mpls_ilm.fid);
        }

        if (CTC_FLAG_ISSET(mpls_ilm.id_type, CTC_MPLS_ID_VRF))
        {
            SYS_MPLS_DUMP("%-10u",mpls_ilm.vrf_id);
        }
        else
        {
            SYS_MPLS_DUMP(" %-10s", "-");
        }

        if ((0x3fff == mpls_ilm.pwid) ||(0 == mpls_ilm.pwid))
        {
            SYS_MPLS_DUMP(" %-10s\n", "-");
        }
        else
        {
            SYS_MPLS_DUMP(" %-10u\n", mpls_ilm.pwid);
        }
        /** end */

        ++(p_space_info->count);
    }

    return CTC_E_NONE;

}

int32
sys_goldengate_mpls_ilm_show_by_space(uint8 lchip, uint8 space_id)
{
    sys_mpls_show_info_t space_info;

    sal_memset(&space_info, 0, sizeof(sys_mpls_show_info_t));

    space_info.space_id = space_id;
    space_info.lchip = lchip;

    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");
    SYS_MPLS_DUMP("  Ilms In Space %d\n", space_id);
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");
    SYS_MPLS_DUMP(" %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
                    "LABEL", "TYPE", "NHID", "PW_Mode", "FID", "VRF", "LOGIC_PORT");
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");

    ctc_hash_traverse_through(p_gg_mpls_master[lchip]->p_mpls_hash,
                        (hash_traversal_fn)_sys_goldengate_show_ilm_info_by_space,
                        &space_info);
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");
    SYS_MPLS_DUMP(" Count %d\n", space_info.count);
    SYS_MPLS_DUMP("--------------------------------------------------------------------------------\n");

    return CTC_E_NONE;
}

int32
sys_goldengate_mpls_ilm_show(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm, uint8  show_type)
{
    int32 ret = CTC_E_NONE;
    uint32 nh_id[SYS_GG_MAX_ECPN];

    SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_MPLS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mpls_ilm);

    ctc_debug_set_flag("mpls", "mpls", 1, CTC_DEBUG_LEVEL_DUMP, TRUE);

    if (1 == show_type)
    {
        ret = sys_goldengate_mpls_get_ilm(lchip, nh_id, p_mpls_ilm);
        if (ret < 0)
        {
            SYS_MPLS_DUMP("%s\n", "% Entry not exist");
            return CTC_E_NONE;
        }

        _sys_goldengate_show_ilm_info_detail(lchip, p_mpls_ilm);
    }
    else if (2 == show_type)
    {
        sys_goldengate_mpls_ilm_show_by_space(lchip, p_mpls_ilm->spaceid);
    }
    else
    {
        _sys_goldengate_show_ilm_info(lchip, p_mpls_ilm);
    }

    ctc_debug_set_flag("mpls", "mpls", 1, CTC_DEBUG_LEVEL_INFO, FALSE);

    return ret;
}

int32
sys_goldengate_mpls_sqn_show(uint8 lchip, uint8 index)
{
    return CTC_E_NOT_SUPPORT;
}

int32
sys_goldengate_show_mpls_status(uint8 lchip)
{
    uint32 field_val = 0;
    uint32 cmd;

    SYS_MPLS_INIT_CHECK(lchip);

    cmd = DRV_IOR(IpeMplsCtl_t, IpeMplsCtl_globalLabelSpace_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &field_val));

    SYS_MPLS_DUMP("-------------------------Mpls Status---------------------\r\n");
    if (!field_val)
    {
        SYS_MPLS_DUMP("%-30s:%6s \n", "Global Space", "Yes");
    }
    else
    {
        SYS_MPLS_DUMP("%-30s:%6s \n", "Global Space", "No");
    }
    SYS_MPLS_DUMP("%-30s:%6u \n", "Mpls Space Count", CTC_MPLS_SPACE_NUMBER);
    SYS_MPLS_DUMP("%-30s:%6u \n", "Mpls Key Count", SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_MPLS));
    SYS_MPLS_DUMP("%-30s:%6u \n", "Used Key Count", p_gg_mpls_master[lchip]->cur_ilm_num);

    SYS_MPLS_DUMP("---------------------------------------------------------\r\n");

    return CTC_E_NONE;
}

#define ___________WARMBOOT________________

int32
_sys_goldengate_mpls_wb_mapping_info(sys_wb_mpls_ilm_hash_t *p_wb_mpls_info, sys_mpls_ilm_hash_t *p_mpls_info, uint8 sync)
{

    if (sync)
    {
        p_wb_mpls_info->label = p_mpls_info->label;
        p_wb_mpls_info->spaceid = p_mpls_info->spaceid;
        p_wb_mpls_info->is_interface = p_mpls_info->is_interface;
        p_wb_mpls_info->model = p_mpls_info->model;
        p_wb_mpls_info->type = p_mpls_info->type;
        p_wb_mpls_info->id_type = p_mpls_info->id_type;
        p_wb_mpls_info->bind_dsfwd = p_mpls_info->bind_dsfwd;
        p_wb_mpls_info->nh_id = p_mpls_info->nh_id;
        p_wb_mpls_info->stats_id = p_mpls_info->stats_id;
        p_wb_mpls_info->ad_index = p_mpls_info->ad_index;
    }
    else
    {
        p_mpls_info->label = p_wb_mpls_info->label;
        p_mpls_info->spaceid = p_wb_mpls_info->spaceid;
        p_mpls_info->is_interface = p_wb_mpls_info->is_interface;
        p_mpls_info->model = p_wb_mpls_info->model;
        p_mpls_info->type = p_wb_mpls_info->type;
        p_mpls_info->id_type = p_wb_mpls_info->id_type;
        p_mpls_info->bind_dsfwd = p_wb_mpls_info->bind_dsfwd;
        p_mpls_info->nh_id = p_wb_mpls_info->nh_id;
        p_mpls_info->stats_id = p_wb_mpls_info->stats_id;
        p_mpls_info->ad_index = p_wb_mpls_info->ad_index;
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_mpls_wb_sync_info_func(sys_mpls_ilm_hash_t *p_mpls_info, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_mpls_ilm_hash_t  *p_wb_mpls_info;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_mpls_info = (sys_wb_mpls_ilm_hash_t *)wb_data->buffer + wb_data->valid_cnt;

    CTC_ERROR_RETURN(_sys_goldengate_mpls_wb_mapping_info(p_wb_mpls_info, p_mpls_info, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_mpls_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t mpls_hash_data;

     wb_data.buffer = mem_malloc(MEM_MPLS_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);


    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_mpls_ilm_hash_t, CTC_FEATURE_MPLS, SYS_WB_APPID_MPLS_SUBID_ILM_HASH);
    mpls_hash_data.data = &wb_data;

    wb_data.valid_cnt = 0;
    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_mpls_master[lchip]->p_mpls_hash, (hash_traversal_fn) _sys_goldengate_mpls_wb_sync_info_func, (void *)&mpls_hash_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_mpls_wb_restore(uint8 lchip)
{
    uint16 entry_cnt = 0;
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_mpls_ilm_hash_t *p_wb_mpls_info;
    sys_mpls_ilm_hash_t *p_mpls_info;
    sys_mpls_ilm_spool_t* p_mpls_ad_new = NULL;
    sys_mpls_ilm_spool_t* p_mpls_ad_get = NULL;
    DsMpls_m dsmpls;

    wb_query.buffer = mem_malloc(MEM_MPLS_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_mpls_ilm_hash_t, CTC_FEATURE_MPLS, SYS_WB_APPID_MPLS_SUBID_ILM_HASH);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_mpls_info = (sys_wb_mpls_ilm_hash_t *)wb_query.buffer + entry_cnt++;

        p_mpls_info = mem_malloc(MEM_MPLS_MODULE,  sizeof(sys_mpls_ilm_hash_t));
        if (NULL == p_mpls_info)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_mpls_info, 0, sizeof(sys_mpls_ilm_hash_t));

        ret = _sys_goldengate_mpls_wb_mapping_info(p_wb_mpls_info, p_mpls_info, 0);
        if (ret)
        {
            goto done;
        }


        /*add ad spool*/
        p_mpls_ad_new = mem_malloc(MEM_MPLS_MODULE, sizeof(sys_mpls_ilm_spool_t));
        if (NULL == p_mpls_ad_new)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }

        sal_memset(p_mpls_ad_new, 0, sizeof(sys_mpls_ilm_spool_t));

        sal_memset(&dsmpls, 0, sizeof(DsMpls_m));
        cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_mpls_info->ad_index, cmd, &dsmpls));

        p_mpls_ad_new->ad_index = p_mpls_info->ad_index;
        sal_memcpy(&p_mpls_ad_new->spool_ad, &dsmpls, sizeof(DsMpls_m));
        ret = ctc_spool_add(p_gg_mpls_master[lchip]->ad_spool, p_mpls_ad_new, NULL,&p_mpls_ad_get);

        if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
        {
            mem_free(p_mpls_ad_new);
        }

        if (ret < 0)
        {
            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

            goto done;
        }
        else
        {
            if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
            {
                /*alloc ad index from position*/
                ret = sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsMpls_t, 1, 1, p_mpls_info->ad_index);
                if (ret)
                {
                    ctc_spool_remove(p_gg_mpls_master[lchip]->ad_spool, p_mpls_ad_get, NULL);
                    mem_free(p_mpls_ad_new);
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc DsMpls ftm table offset from position %u error! ret: %d.\n", p_mpls_info->ad_index, ret);

                    goto done;
                }

            }
            else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
            {
                mem_free(p_mpls_ad_new);
            }
        }

        p_mpls_info->spool_ad_ptr = p_mpls_ad_get;

         /*add to soft table*/
        ctc_hash_insert(p_gg_mpls_master[lchip]->p_mpls_hash, p_mpls_info);
        p_gg_mpls_master[lchip]->cur_ilm_num ++;

    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}




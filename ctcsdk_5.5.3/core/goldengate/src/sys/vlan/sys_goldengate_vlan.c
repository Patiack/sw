/**
@file sys_goldengate_vlan.c

@date 2011 - 11 - 11

@version v2.0


This file contains all sys APIs of vlan
*/

/***************************************************************
*
* Header Files
*
***************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_spool.h"
#include "ctc_l2.h"

#include "sys_goldengate_ftm.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_packet.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_register.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"
/***************************************************************
*
*  Defines and Macros
*
***************************************************************/
#define SYS_VLAN_BITMAP_NUM            4096
#define SYS_MAX_PROFILE_ID             511
#define SYS_MIN_PROFILE_ID             0
#define SYS_VLAN_STATUS_ENTRY_BITS       128
#define ENTRY_NUM_VLAN                   4096
#define ENTRY_NUM_VLAN_STATUS           2048
#define ENTRY_NUM_VLAN_PROFILE           512
#define IPE_CTL_BITS_NUM               6
#define SYS_VLAN_BLOCK_NUM               4
#define SYS_VLAN_PROFILE_BLOCK_NUM      4
#define MAX_NETWORK_PORT_CHANNEL  64
#define SYS_VLAN_TEMPLATE_NUM     16
#define SYS_VLAN_RESERVE_NUM     4
#define SYS_VLAN_VSI_PARAM_MAX   8
#define SYS_VLAN_PTP_CLOCK_ID_MAX 7


struct sys_sw_vlan_profile_s
{
    uint32 vlan_storm_ctl_en                                                : 1;
    uint32 vlan_storm_ctl_ptr                                               : 7;
    uint32 egress_vlan_acl_label                                            : 8;
    uint32 egress_vlan_acl_routed_only0                                     : 1;
    uint32 egress_vlan_acl_lookup_type0                                     : 2;
    uint32 ingress_vlan_acl_en0                                             : 1;
    uint32 ingress_vlan_acl_en1                                             : 1;
    uint32 ingress_vlan_acl_en2                                             : 1;
    uint32 ingress_vlan_acl_en3                                             : 1;
    uint32 egress_vlan_acl_en0                                              : 1;
    uint32 ingress_vlan_acl_lookup_type0                                    : 2;
    uint32 ingress_vlan_acl_lookup_type1                                    : 2;
    uint32 ingress_vlan_acl_lookup_type2                                    : 2;
    uint32 ingress_vlan_acl_lookup_type3                                    : 2;

    uint32 ingress_vlan_acl_label0                                          : 8;
    uint32 ingress_vlan_acl_label1                                          : 8;
    uint32 ingress_vlan_acl_label2                                          : 8;
    uint32 ingress_vlan_acl_label3                                          : 8;

    uint32 egress_vlan_span_id                                              : 2;
    uint32 ingress_vlan_span_id                                             : 2;
    uint32 fip_exception_type                                               : 2;
    uint32 ingress_vlan_acl_routed_only0                                    : 1;
    uint32 ingress_vlan_acl_routed_only1                                    : 1;
    uint32 ingress_vlan_acl_routed_only2                                    : 1;
    uint32 ingress_vlan_acl_routed_only3                                    : 1;
    uint32 ingress_vlan_span_en                                             : 1;
    uint32 egress_vlan_span_en                                              : 1;
    uint32 force_ipv4_lookup                                                : 1;
    uint32 force_ipv6_lookup                                                : 1;
    uint32 ptp_index                                                     : 3;
    uint32 rsv_0                                                            : 15;
};
typedef struct sys_sw_vlan_profile_s sys_sw_vlan_profile_t;

struct sys_vlan_profile_s
{
    sys_sw_vlan_profile_t sw_vlan_profile;
    uint16 profile_id;
    uint8 lchip ;
};
typedef struct sys_vlan_profile_s sys_vlan_profile_t;

/**
 @brief define overlay tunnel vn_id and fid mapping struct
*/
struct sys_vlan_overlay_mapping_key_s
{
    uint32 vn_id;

    uint16 fid;
};
typedef struct sys_vlan_overlay_mapping_key_s sys_vlan_overlay_mapping_key_t;

struct sys_vlan_master_s
{
    ctc_spool_t*        vprofile_spool;
    uint16*             p_vlan_profile_id;
    uint32              vlan_bitmap[128];
    uint32              vlan_def_bitmap[128];
    sys_vlan_profile_t* p_vlan_profile[512];
    ctc_vlanptr_mode_t  vlan_mode;
    uint8               vlan_port_bitmap_mode;
    uint8               sys_vlan_def_mode;
    uint8               vni_mapping_mode;
    ctc_hash_t*         vni_mapping_hash; /* used to record vn_id and fid map */
    sal_mutex_t*        mutex;
};
typedef struct sys_vlan_master_s sys_vlan_master_t;

#define SYS_VLAN_INIT_XC_DS_VLAN(p_ds_vlan) \
    { \
    SetDsVlan(V, transmitDisable_f, p_ds_vlan, 0);\
    SetDsVlan(V, receiveDisable_f, p_ds_vlan, 0);\
    }

#define SYS_VLAN_FILTER_PORT_CHECK(lport) \
    do \
    { \
        if (lport >= MAX_NETWORK_PORT_CHANNEL) \
        { \
            return CTC_E_VLAN_FILTER_PORT; \
        } \
    } \
    while (0)

#define SYS_VLAN_CREATE_CHECK(vlan_ptr) \
    do \
    { \
        if (!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F))){ \
            return CTC_E_VLAN_NOT_CREATE; } \
    } \
    while (0)

#define SYS_VLAN_PROFILE_ID_VALID_CHECK(profile_id) \
    do \
    { \
        if ((profile_id) >= SYS_MAX_PROFILE_ID || (profile_id) < SYS_MIN_PROFILE_ID){ \
            return CTC_E_VLAN_INVALID_PROFILE_ID; } \
    } \
    while (0)

#define SYS_VLAN_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_vlan_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_VLAN_CREATE_LOCK(lchip)                         \
    do                                                      \
    {                                                       \
        sal_mutex_create(&p_gg_vlan_master[lchip]->mutex);  \
        if (NULL == p_gg_vlan_master[lchip]->mutex)         \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_VLAN_LOCK(lchip) \
    sal_mutex_lock(p_gg_vlan_master[lchip]->mutex)

#define SYS_VLAN_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_vlan_master[lchip]->mutex)

#define CTC_ERROR_RETURN_VLAN_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_vlan_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_VLAN_DBG_OUT(level, FMT, ...) \
    do \
    { \
        CTC_DEBUG_OUT(vlan, vlan, VLAN_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_VLAN_DBG_INFO(FMT, ...) \
    do \
    { \
        SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_VLAN_DBG_DUMP(FMT, ...) \
    do \
    { \
        SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_VLAN_NO_MEMORY_RETURN(op) \
    do \
    { \
        if (NULL == (op)) \
        { \
            return CTC_E_NO_MEMORY; \
        } \
    } \
    while (0)

#define REF_COUNT(data)   ctc_spool_get_refcnt(p_gg_vlan_master[lchip]->vprofile_spool, data)
#define VLAN_PROFILE_SIZE_PER_BUCKET 16
#define SYS_VLAN_OVERLAY_IHASH_MASK         0xFFF

#define ROT(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

#define MIX(a, b, c) \
    do \
    { \
        a -= c;  a ^= ROT(c, 4);  c += b; \
        b -= a;  b ^= ROT(a, 6);  a += c; \
        c -= b;  c ^= ROT(b, 8);  b += a; \
        a -= c;  a ^= ROT(c, 16);  c += b; \
        b -= a;  b ^= ROT(a, 19);  a += c; \
        c -= b;  c ^= ROT(b, 4);  b += a; \
    } while (0)

#define FINAL(a, b, c) \
    { \
        c ^= b; c -= ROT(b, 14); \
        a ^= c; a -= ROT(c, 11); \
        b ^= a; b -= ROT(a, 25); \
        c ^= b; c -= ROT(b, 16); \
        a ^= c; a -= ROT(c, 4);  \
        b ^= a; b -= ROT(a, 14); \
        c ^= b; c -= ROT(b, 24); \
    }

#define SETEGRESSVSI(FID,VN_ID,DS_DATA)                     \
{                                                           \
    if (0 == (FID % 4) )                                    \
    {                                                       \
        SetDsEgressVsi(V, array_0_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else if (1 == (FID % 4))                                \
    {                                                       \
        SetDsEgressVsi(V, array_1_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else if (2 == (FID % 4))                                \
    {                                                       \
        SetDsEgressVsi(V, array_2_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else                                                    \
    {                                                       \
        SetDsEgressVsi(V, array_3_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
}

#define GETEGRESSVSI(FID,VN_ID,DS_DATA)                     \
{                                                           \
    if (0 == (FID % 4) )                                    \
    {                                                       \
        GetDsEgressVsi(A, array_0_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else if (1 == (FID % 4))                                \
    {                                                       \
        GetDsEgressVsi(A, array_1_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else if (2 == (FID % 4))                                \
    {                                                       \
        GetDsEgressVsi(A, array_2_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
    else                                                    \
    {                                                       \
        GetDsEgressVsi(A, array_3_vni_f, DS_DATA, VN_ID);   \
    }                                                       \
}
/*
*
*    byte_offset = vlan_id >> 3 ;
*    bit_offset = vlan_id & 0x7;
*    p_gg_vlan_master->vlan_bitmap[byte_offset] |= 1 >> bit_offset;
*/

static sys_vlan_master_t* p_gg_vlan_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

int32 sys_goldengate_vlan_wb_sync(uint8 lchip);
int32 sys_goldengate_vlan_wb_restore(uint8 lchip);

/**
 *
 *  hw table
 */
STATIC uint32
_sys_goldengate_vlan_overlay_hash_make(sys_vlan_overlay_mapping_key_t* p_mapping_info)
{
    uint32 a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32)96) << 2);

    a += p_mapping_info->vn_id;
    MIX(a, b, c);

    return (c & SYS_VLAN_OVERLAY_IHASH_MASK);
}

STATIC bool
_sys_goldengate_vlan_overlay_hash_cmp(sys_vlan_overlay_mapping_key_t* p_mapping_info1, sys_vlan_overlay_mapping_key_t* p_mapping_info)
{
    if (p_mapping_info1->vn_id != p_mapping_info->vn_id)
    {
        return FALSE;
    }

    return TRUE;
}

STATIC int32
_sys_goldengate_vlan_overlay_set_fid_hw(uint8 lchip, uint16 fid, uint32 vn_id)
{
    uint16 index = 0;
    ds_t ds_data;
    uint32 cmd = 0;

    sal_memset(&ds_data, 0, sizeof(ds_data));
    index = fid >> 2;
    cmd = DRV_IOR(DsEgressVsi_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN( DRV_IOCTL(lchip, index, cmd, &ds_data));
    SETEGRESSVSI(fid, vn_id, ds_data);
    cmd = DRV_IOW(DsEgressVsi_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_data));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_cmp_node_data(void* node_data, void* user_data)
{
    sys_vlan_overlay_mapping_key_t* p_vlan_overlay = (sys_vlan_overlay_mapping_key_t*) node_data;


    if (p_vlan_overlay && (0 == p_vlan_overlay->vn_id) && (*(uint16*)user_data == p_vlan_overlay->fid))
    {
        return CTC_E_EXIST;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_get_vni_fid_mapping_mode(uint8 lchip, uint8* mode)
{
    SYS_VLAN_INIT_CHECK(lchip);
    SYS_VLAN_LOCK(lchip);
    *mode = p_gg_vlan_master[lchip]->vni_mapping_mode;
    SYS_VLAN_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_set_vni_fid_mapping_mode(uint8 lchip, uint8 mode)
{
    SYS_VLAN_INIT_CHECK(lchip);
    SYS_VLAN_LOCK(lchip);
    p_gg_vlan_master[lchip]->vni_mapping_mode = mode;
    SYS_VLAN_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_get_vn_id (uint8 lchip,  uint16 fid, uint32* p_vn_id )
{
    uint32 index = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    uint16 new_fid = fid;
    DsEgressVsi_m ds_egress_vsi;

    /* get vn_id*/
    sal_memset(&ds_egress_vsi, 0, sizeof(ds_egress_vsi));
    index = fid >> 2;
    cmd = DRV_IOR(DsEgressVsi_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_egress_vsi));
    GETEGRESSVSI(fid, p_vn_id, &ds_egress_vsi);

    if (*p_vn_id == 0)
    {
        ret = ctc_hash_traverse(p_gg_vlan_master[lchip]->vni_mapping_hash, (hash_traversal_fn)_sys_goldengate_vlan_cmp_node_data, (void*)(&new_fid));
        if (ret != CTC_E_EXIST)
        {
            return CTC_E_NOT_EXIST;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_add_fid (uint8 lchip,  uint32 vn_id, uint16 fid)
{
    sys_vlan_overlay_mapping_key_t* p_mapping_info = NULL;
    sys_vlan_overlay_mapping_key_t mapping_info;
    int32 ret = 0;
    uint32 old_vni_id = 0;
    uint16 old_fid = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    if ( vn_id > 0xFFFFFF )
    {
        return CTC_E_OVERLAY_TUNNEL_INVALID_VN_ID;
    }
    if (( fid > 0x3FFF )||(0 == fid))
    {
        return CTC_E_OVERLAY_TUNNEL_INVALID_FID;
    }

    ret = sys_goldengate_vlan_overlay_get_vn_id(lchip, fid, &old_vni_id);
    if (((old_vni_id == 0) && (ret == 0)) || (old_vni_id && old_vni_id != vn_id))
    {
        return CTC_E_INVALID_CONFIG;
    }

    sal_memset(&mapping_info, 0, sizeof(sys_vlan_overlay_mapping_key_t));
    mapping_info.vn_id = vn_id;
    SYS_VLAN_LOCK(lchip);
    p_mapping_info = ctc_hash_lookup(p_gg_vlan_master[lchip]->vni_mapping_hash, &mapping_info);
    if ( NULL == p_mapping_info )
    {
        p_mapping_info =  mem_malloc(MEM_VLAN_MODULE,  sizeof(sys_vlan_overlay_mapping_key_t));
        if (NULL == p_mapping_info)
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_mapping_info, 0, sizeof(sys_vlan_overlay_mapping_key_t));
        p_mapping_info->vn_id = vn_id;
        p_mapping_info->fid = fid;
        /* keep setting sw table after set hw table */
        CTC_ERROR_GOTO(_sys_goldengate_vlan_overlay_set_fid_hw(lchip, fid, vn_id), ret, error);
        ctc_hash_insert(p_gg_vlan_master[lchip]->vni_mapping_hash, p_mapping_info);
    }
    else
    {
        if(p_mapping_info->fid != fid)
        {
            old_fid = p_mapping_info->fid;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_overlay_set_fid_hw(lchip, old_fid, 0));
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_overlay_set_fid_hw(lchip, fid, vn_id));
            p_mapping_info->fid = fid;
        }
    }
    SYS_VLAN_UNLOCK(lchip);
    return CTC_E_NONE;
error:
    mem_free(p_mapping_info);
    SYS_VLAN_UNLOCK(lchip);
    return ret;
}


int32
sys_goldengate_vlan_overlay_remove_fid (uint8 lchip,  uint32 vn_id)
{
    sys_vlan_overlay_mapping_key_t* p_mapping_info = NULL;
    sys_vlan_overlay_mapping_key_t mapping_info;

    SYS_VLAN_INIT_CHECK(lchip);
    if ( vn_id > 0xFFFFFF )
    {
        return CTC_E_OVERLAY_TUNNEL_INVALID_VN_ID;
    }

    sal_memset(&mapping_info, 0, sizeof(sys_vlan_overlay_mapping_key_t));
    mapping_info.vn_id = vn_id;

    SYS_VLAN_LOCK(lchip);
    p_mapping_info = ctc_hash_lookup(p_gg_vlan_master[lchip]->vni_mapping_hash, &mapping_info);
    if ( NULL != p_mapping_info )
    {
        CTC_ERROR_RETURN_VLAN_UNLOCK( _sys_goldengate_vlan_overlay_set_fid_hw(lchip, p_mapping_info->fid, 0));
        p_mapping_info->vn_id = vn_id;
        ctc_hash_remove(p_gg_vlan_master[lchip]->vni_mapping_hash, p_mapping_info);
        mem_free(p_mapping_info);
    }
    else
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_OVERLAY_TUNNEL_VN_FID_NOT_EXIST;
    }
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_get_fid (uint8 lchip,  uint32 vn_id, uint16* p_fid )
{
    sys_vlan_overlay_mapping_key_t* p_mapping_info = NULL;
    sys_vlan_overlay_mapping_key_t mapping_info;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_fid);
    if ( vn_id > 0xFFFFFF )
    {
        return CTC_E_OVERLAY_TUNNEL_INVALID_VN_ID;
    }

    if (!p_gg_vlan_master[lchip]->vni_mapping_mode)
    {
        sal_memset(&mapping_info, 0, sizeof(mapping_info));
        mapping_info.vn_id = vn_id;

        SYS_VLAN_LOCK(lchip);
        p_mapping_info = ctc_hash_lookup(p_gg_vlan_master[lchip]->vni_mapping_hash, &mapping_info);
        if (NULL == p_mapping_info)
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_OVERLAY_TUNNEL_VN_FID_NOT_EXIST;
        }
        *p_fid = p_mapping_info->fid;
        SYS_VLAN_UNLOCK(lchip);
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_overlay_set_fid (uint8 lchip,  uint32 vn_id, uint16 fid )
{
    if (!p_gg_vlan_master[lchip]->vni_mapping_mode)
    {
        /* mode 0, vni_id : fid = 1 : 1 */
        if (0 == fid)
        {
            CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_remove_fid (lchip, vn_id));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_vlan_overlay_add_fid (lchip, vn_id, fid));
        }
    }
    else
    {
        /* mode 1, fid : vni_id = N : 1 */
        if(fid == 0)
        {
            return CTC_E_BADID;
        }
        CTC_ERROR_RETURN(_sys_goldengate_vlan_overlay_set_fid_hw(lchip, fid, vn_id));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_vlan_set_default_vlan(uint8 lchip, DsVlan_m* p_ds_vlan)
{
    sal_memset(p_ds_vlan, 0 ,sizeof(DsVlan_m));
    SetDsVlan(V, transmitDisable_f, p_ds_vlan, 1);
    SetDsVlan(V, receiveDisable_f, p_ds_vlan, 1);
    SetDsVlan(V, interfaceId_f, p_ds_vlan, 0);
    SetDsVlan(V, arpExceptionType_f, p_ds_vlan, CTC_EXCP_NORMAL_FWD);
    SetDsVlan(V, dhcpExceptionType_f, p_ds_vlan, CTC_EXCP_NORMAL_FWD);

    return CTC_E_NONE;
}

STATIC int
_sys_goldengate_vlan_profile_write(uint8 lchip, uint16 profile_id, DsVlanProfile_m* p_vlan_profile)
{
    uint32 cmd = 0;
    cmd = DRV_IOW(DsVlanProfile_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, p_vlan_profile));

    return CTC_E_NONE;
}

/**
  @brief judge whether a port is member of vlan
  */
STATIC int32
_sys_goldengate_vlan_is_member_port(uint8 lchip, uint8 field_id, uint32 index)
{
    DsVlanStatus_m ds_vlan_status;
    uint32 cmd = 0;
    uint32 vlan_id_valid = 0;

    sal_memset(&ds_vlan_status, 0, sizeof(DsVlanStatus_m));

    cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
    DRV_GET_FIELD_A(DsVlanStatus_t, field_id, &ds_vlan_status, &vlan_id_valid);

    return vlan_id_valid;
}

STATIC uint32
_sys_goldengate_vlan_hash_make(sys_vlan_profile_t* p_vlan_profile)
{
    uint32 a, b, c;
    uint32* k = (uint32*)&p_vlan_profile->sw_vlan_profile;
    uint8*  k8;
    uint8   length = sizeof(sys_sw_vlan_profile_t);

    /* Set up the internal state */
    a = b = c = 0xdeadbeef;

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
        a += k[0];
        b += k[1];
        c += k[2];
        MIX(a, b, c);
        length -= 12;
        k += 3;
    }

    k8 = (uint8*)k;

    switch (length)
    {
    case 12:
        c += k[2];
        b += k[1];
        a += k[0];
        break;

    case 11:
        c += ((uint8)k8[10]) << 16;       /* fall through */

    case 10:
        c += ((uint8)k8[9]) << 8;         /* fall through */

    case 9:
        c += k8[8];                          /* fall through */

    case 8:
        b += k[1];
        a += k[0];
        break;

    case 7:
        b += ((uint8)k8[6]) << 16;        /* fall through */

    case 6:
        b += ((uint8)k8[5]) << 8;         /* fall through */

    case 5:
        b += k8[4];                          /* fall through */

    case 4:
        a += k[0];
        break;

    case 3:
        a += ((uint8)k8[2]) << 16;        /* fall through */

    case 2:
        a += ((uint8)k8[1]) << 8;         /* fall through */

    case 1:
        a += k8[0];
        break;

    case 0:
        return c;
    }

    FINAL(a, b, c);
    return c;
}

STATIC bool
_sys_goldengate_vlan_hash_compare(sys_vlan_profile_t* p_bucket, sys_vlan_profile_t* p_new)
{
    if (!p_bucket || !p_new)
    {
        return FALSE;
    }

    if (!sal_memcmp(&p_bucket->sw_vlan_profile, &p_new->sw_vlan_profile, sizeof(sys_sw_vlan_profile_t))) /*xscl is same size*/
    {
        return TRUE;
    }

    return FALSE;
}

/* add to software table is ok, no need to write hardware table */
STATIC int32
_sys_goldengate_vlan_profile_static_add(uint8 lchip, sys_vlan_profile_t* p_vlan_profile, uint16* profile_id)
{
    sys_vlan_profile_t*  p_vp = NULL;
    uint32 index = 0;

    CTC_PTR_VALID_CHECK(p_vlan_profile);
    CTC_PTR_VALID_CHECK(profile_id);

    p_vp = mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_profile_t));
    if (!p_vp)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(p_vp, p_vlan_profile, sizeof(sys_vlan_profile_t));
    p_vp->lchip = lchip;
    p_vp->profile_id = (*profile_id);
    index = (*profile_id);

    p_gg_vlan_master[lchip]->p_vlan_profile[index] = p_vp;

    if (ctc_spool_static_add(p_gg_vlan_master[lchip]->vprofile_spool, p_vp) < 0)
    {
        return CTC_E_NO_MEMORY;
    }

    (*profile_id)++;
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_vlan_profile_index_free(uint8 lchip, uint16 index)
{
    sys_goldengate_opf_t  opf;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_VLAN_PROFILE;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, (uint32)index));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_profile_sw_read(uint8 lchip, uint16 profile_id, sys_vlan_profile_t** p_ds_vlan_profile)
{

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    *p_ds_vlan_profile = p_gg_vlan_master[lchip]->p_vlan_profile[profile_id];
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_profile_remove(uint8 lchip, sys_vlan_profile_t*  p_vlan_profile_in)
{
    uint16            index_free = 0;
    int32             ret = 0;

    sys_vlan_profile_t* p_vlan_profile_del;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_vlan_profile_in);

    p_vlan_profile_del = ctc_spool_lookup(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile_in);
    if (!p_vlan_profile_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile_in, NULL);
    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            index_free = p_vlan_profile_del->profile_id;
            /*free ad index*/
            CTC_ERROR_RETURN(_sys_goldengate_vlan_profile_index_free(lchip, index_free));
            SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: db_ad removed. removed DsVlanProfile index = %d\n",  index_free);
            mem_free(p_vlan_profile_del);
            p_gg_vlan_master[lchip]->p_vlan_profile[index_free] = NULL;
        }
        else if (ret == CTC_SPOOL_E_OPERATE_REFCNT)
        {
        }
    }

    return CTC_E_NONE;

}

/* init the vlan module */
int32
sys_goldengate_vlan_init(uint8 lchip, ctc_vlan_global_cfg_t* vlan_global_cfg)
{
    uint16 profile_id = 0;
    uint8  i, j, k;

    uint32 index = 0;
    uint32 cmd = 0;

    uint32 entry_num_vlan = 0;
    uint32 entry_num_vlan_profile = 0;
    DsVlan_m ds_vlan;
    uint32 vlan_tag_bit_map[2] = {0};
    DsVlanTagBitMap_m ds_vlan_tag_bit_map;
    DsVlanStatus_m ds_vlan_status;
    DsVsi_m ds_vlan_vsi;
    DsVlanProfile_m ds_vlan_profile;
    sys_vlan_profile_t vlan_profile;

    sys_goldengate_opf_t opf;
    ctc_spool_t spool;

    LCHIP_CHECK(lchip);
    sal_memset(&ds_vlan, 0, sizeof(DsVlan_m));
    sal_memset(&ds_vlan_tag_bit_map, 0, sizeof(DsVlanTagBitMap_m));
    sal_memset(&ds_vlan_status, 0, sizeof(DsVlanStatus_m));
    sal_memset(&ds_vlan_profile, 0, sizeof(DsVlanProfile_m));
    sal_memset(&ds_vlan_vsi, 0, sizeof(DsVsi_m));
    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsVlan_t, &entry_num_vlan));
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsVlanProfile_t, &entry_num_vlan_profile));

    if (NULL != p_gg_vlan_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(vlan_global_cfg);

    /*init p_gg_vlan_master*/
    SYS_VLAN_NO_MEMORY_RETURN(p_gg_vlan_master[lchip] = \
                                  (sys_vlan_master_t*)mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_master_t)));

    sal_memset(p_gg_vlan_master[lchip], 0, sizeof(sys_vlan_master_t));
    /*vlan mode*/
    p_gg_vlan_master[lchip]->vlan_mode = vlan_global_cfg->vlanptr_mode;

    /*init vlan */
    sal_memset(p_gg_vlan_master[lchip]->vlan_bitmap, 0, sizeof(uint32) * 128);
    sal_memset(p_gg_vlan_master[lchip]->vlan_def_bitmap, 0, sizeof(uint32) * 128);

    SYS_VLAN_NO_MEMORY_RETURN(p_gg_vlan_master[lchip]->p_vlan_profile_id = \
                                  mem_malloc(MEM_VLAN_MODULE, sizeof(uint16) * entry_num_vlan));

    sal_memset(p_gg_vlan_master[lchip]->p_vlan_profile_id, 0, sizeof(uint16) * entry_num_vlan);

    /*init vlan profile*/
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = entry_num_vlan_profile / VLAN_PROFILE_SIZE_PER_BUCKET;
    spool.max_count = entry_num_vlan_profile - 1;
    spool.user_data_size = sizeof(sys_vlan_profile_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_vlan_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_vlan_hash_compare;
    p_gg_vlan_master[lchip]->vprofile_spool = ctc_spool_create(&spool);

    /* vlan filtering/tagged mode 0 :4K vlan * 64 ports per slice*/
    p_gg_vlan_master[lchip]->vlan_port_bitmap_mode = 0;

    /* create user vlan add default entry */
    p_gg_vlan_master[lchip]->sys_vlan_def_mode = 1;

    if (0 != entry_num_vlan_profile)
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_VLAN_PROFILE, 1));

        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type  = OPF_VLAN_PROFILE;
        /*begin with 1, 0 is invalid*/
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset
                             (lchip, &opf, 1 + SYS_VLAN_TEMPLATE_NUM, entry_num_vlan_profile - (1 + SYS_VLAN_TEMPLATE_NUM)));

        CTC_ERROR_RETURN(sys_goldengate_opf_init_reverse_size(lchip, &opf, SYS_VLAN_RESERVE_NUM));
    }

    /*init asic table: dsvlan dsvlanbitmap*/
    _sys_goldengate_vlan_set_default_vlan(lchip, &ds_vlan);
    for (index = 0; index < entry_num_vlan; index++)
    {
        cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);


            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan));

    }

    /*Init DsVlan 0 for port crossconnect or vpls*/
    SetDsVlan(V, transmitDisable_f, &ds_vlan, 0);
    SetDsVlan(V, receiveDisable_f, &ds_vlan, 0);
    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_vlan));


    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        vlan_tag_bit_map[0] = 0xffffffff;
        vlan_tag_bit_map[1] = 0xffffffff;
        SetDsVlanTagBitMap(A, tagPortBitMap_f, &ds_vlan_tag_bit_map, vlan_tag_bit_map);
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_vlan_tag_bit_map));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 4096, cmd, &ds_vlan_tag_bit_map));

        sal_memcpy(&ds_vlan_status, &vlan_tag_bit_map, sizeof(DsVlanStatus_m));
        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_vlan_status));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 4096, cmd, &ds_vlan_status));

    }


    /*vlan profile*/
    for (index = 0; index < entry_num_vlan_profile; index++)
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_profile_write(lchip, index, &ds_vlan_profile));
    }

    SYS_VLAN_CREATE_LOCK(lchip);
    /* init vlan profile template */
    profile_id = 1;
    sal_memset(&vlan_profile, 0, sizeof(sys_vlan_profile_t));
    vlan_profile.sw_vlan_profile.fip_exception_type = 1;

    for (i = 0; i < 4; i++)
    {
        switch (i)
        {
            case 0:
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en0 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en1 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en2 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en3 = 0;
                break;

            case 1:
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en0 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en1 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en2 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en3 = 0;
                break;

            case 2:
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en0 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en1 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en2 = 0;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en3 = 0;
                break;

            case 3:
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en0 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en1 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en2 = 1;
                vlan_profile.sw_vlan_profile.ingress_vlan_acl_en3 = 1;
                break;
        }

        for (j = 0; j < 2; j++)
        {
            switch (j)
            {
                case 0:
                    vlan_profile.sw_vlan_profile.egress_vlan_acl_en0 = 0;
                    break;

                case 1:
                    vlan_profile.sw_vlan_profile.egress_vlan_acl_en0 = 1;
                    break;
            }

            for (k = 0; k < 2; k++)
            {
                switch (k)
                {
                    case 0:
                        vlan_profile.sw_vlan_profile.force_ipv4_lookup = 0;
                        break;

                    case 1:
                        vlan_profile.sw_vlan_profile.force_ipv4_lookup = 1;
                        break;
                }

                CTC_ERROR_RETURN(_sys_goldengate_vlan_profile_static_add(lchip, &vlan_profile, &profile_id));
            }
        }
    }


    /*init Vsi 0, learning disable*/
    SetDsVsi(V, array_0_vsiLearningDisable_f, &ds_vlan_vsi, 1);
    cmd = DRV_IOW(DsVsi_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_vlan_vsi));


    p_gg_vlan_master[lchip]->vni_mapping_hash = ctc_hash_create(1,
                                                                (SYS_VLAN_OVERLAY_IHASH_MASK + 1),
                                                                (hash_key_fn)_sys_goldengate_vlan_overlay_hash_make,
                                                                (hash_cmp_fn)_sys_goldengate_vlan_overlay_hash_cmp);
    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_VLAN, sys_goldengate_vlan_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_vlan_wb_restore(lchip));
    }
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_vlan_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_deinit(uint8 lchip)
{
    uint32 entry_num_vlan_profile = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_vlan_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free vni mapping hash*/
    ctc_hash_traverse(p_gg_vlan_master[lchip]->vni_mapping_hash, (hash_traversal_fn)_sys_goldengate_vlan_free_node_data, NULL);
    ctc_hash_free(p_gg_vlan_master[lchip]->vni_mapping_hash);

    sys_goldengate_ftm_query_table_entry_num(lchip, DsVlanProfile_t, &entry_num_vlan_profile);
    if (0 != entry_num_vlan_profile)
    {
        sys_goldengate_opf_deinit(lchip, OPF_VLAN_PROFILE);
    }

    /*free vlan profile spool*/
    ctc_spool_free(p_gg_vlan_master[lchip]->vprofile_spool);

    mem_free(p_gg_vlan_master[lchip]->p_vlan_profile_id);

    sal_mutex_destroy(p_gg_vlan_master[lchip]->mutex);
    mem_free(p_gg_vlan_master[lchip]);

    return CTC_E_NONE;
}

/**
  @brief The function is to create a vlan

*/
int32
sys_goldengate_vlan_create_vlan(uint8 lchip, uint16 vlan_id)
{
    uint16 vlan_ptr = 0;
    uint8 field_id = 0;
    uint32 index = 0;
    uint8 temp = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    DsVlan_m ds_vlan;
    DsVsi_m ds_vlan_vsi;
    uint32 vlan_tag_bit_map[2] = {0};
    DsVlanTagBitMap_m ds_vlan_tag_bit_map;

    SYS_VLAN_INIT_CHECK(lchip);

    sal_memset(&ds_vlan, 0, sizeof(DsVlan_m));
    sal_memset(&ds_vlan_vsi, 0, sizeof(DsVsi_m));
    sal_memset(&ds_vlan_tag_bit_map, 0, sizeof(DsVlanTagBitMap_m));

#if 0
    if (p_gg_vlan_master->vlan_mode != CTC_VLANPTR_MODE_VLANID)
    {
        return CTC_E_VLAN_ERROR_MODE;
    }
#endif

    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create vlan_id: %d!\n", vlan_id);

    SYS_VLAN_LOCK(lchip);
    if (IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_id >> 5)], (vlan_id & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_VLAN_EXIST;
    }
    /*vlan2ptr vlanid==vlanptr*/
    CTC_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_id >> 5)], (vlan_id & 0x1F));
    SYS_VLAN_UNLOCK(lchip);

    vlan_ptr = vlan_id;

    field_id = (vlan_id & 0x1)? DsVlan2Ptr_array_1_vlanPtr_f : DsVlan2Ptr_array_0_vlanPtr_f;
    cmd = DRV_IOW(DsVlan2Ptr_t, field_id);
    index = (vlan_id >> 1) & 0x7FF;
    value = vlan_ptr;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    CTC_ERROR_RETURN(sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_VLAN_PTR, vlan_id, value));

    /*vlan default set vlanid==fid*/
    _sys_goldengate_vlan_set_default_vlan(lchip, &ds_vlan);
    SetDsVlan(V, transmitDisable_f, &ds_vlan, 0);
    SetDsVlan(V, receiveDisable_f, &ds_vlan, 0);
    SetDsVlan(V, vsiId_f, &ds_vlan, vlan_id);
    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    /*vlan vsi*/
    cmd = DRV_IOR(DsVsi_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (vlan_ptr>>2), cmd, &ds_vlan_vsi));
    cmd = DRV_IOW(DsVsi_t, DRV_ENTRY_FLAG);
    for (temp = 0;temp < SYS_VLAN_VSI_PARAM_MAX; temp++)
    {
        field_id = (vlan_ptr & 0x3)*SYS_VLAN_VSI_PARAM_MAX + temp;
        DRV_SET_FIELD_V(DsVsi_t, field_id, &ds_vlan_vsi, 0);
    }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (vlan_ptr>>2), cmd, &ds_vlan_vsi));


    /*vlan tag*/
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        vlan_tag_bit_map[0] = 0xffffffff;
        vlan_tag_bit_map[1] = 0xffffffff;
        SetDsVlanTagBitMap(A, tagPortBitMap_f, &ds_vlan_tag_bit_map, vlan_tag_bit_map);
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan_tag_bit_map));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr+4096, cmd, &ds_vlan_tag_bit_map));

    }

    return CTC_E_NONE;
}
/**
  @brief The function is to create a vlan with vlanptr

*/
int32
sys_goldengate_vlan_create_uservlan(uint8 lchip, ctc_vlan_uservlan_t* user_vlan)
{
    uint16 vlan_id = 0;
    uint16 vlan_ptr = 0;
    uint16 temp_vlan_ptr = 0;
    uint16 fid = 0;

    uint8 field_id = 0;
    uint8 temp = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 index = 0;
    DsVlan_m ds_vlan;
    DsVsi_m ds_vlan_vsi;
    uint32 vlan_tag_bit_map[2] = {0};
    DsVlanTagBitMap_m  ds_vlan_tag_bit_map;

    CTC_PTR_VALID_CHECK(user_vlan);

    sal_memset(&ds_vlan, 0, sizeof(DsVlan_m));
    sal_memset(&ds_vlan_vsi, 0, sizeof(DsVsi_m));
    sal_memset(&ds_vlan_tag_bit_map, 0, sizeof(DsVlanTagBitMap_m));

    vlan_id = user_vlan->vlan_id;
    vlan_ptr = user_vlan->user_vlanptr;
    fid = user_vlan->fid;

    SYS_VLAN_INIT_CHECK(lchip);

    if (p_gg_vlan_master[lchip]->vlan_mode != CTC_VLANPTR_MODE_USER_DEFINE1)
    {
        return CTC_E_VLAN_ERROR_MODE;
    }

    CTC_VLAN_RANGE_CHECK(vlan_id);

    if ((vlan_ptr) < CTC_MIN_VLAN_ID || (vlan_ptr) > CTC_MAX_VLAN_ID)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_L2_FID_CHECK(fid);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create user vlan_id: %d, vlan_ptr: %d, fid: %d!\n", vlan_id, vlan_ptr, fid);


    /*if vlan_id mapping to vlan_ptr exist,return vlan exist*/
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_ptr(lchip,vlan_id,&temp_vlan_ptr));
    if(0 != temp_vlan_ptr)
    {
        return CTC_E_VLAN_EXIST;
    }

    SYS_VLAN_LOCK(lchip);
    if (IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_VLAN_EXIST;
    }

    CTC_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));

    field_id = (vlan_id & 0x1)? DsVlan2Ptr_array_1_vlanPtr_f : DsVlan2Ptr_array_0_vlanPtr_f;
    cmd = DRV_IOW(DsVlan2Ptr_t, field_id);
    index = (vlan_id >> 1) & 0x7FF;
    value = vlan_ptr;
    CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value));
    CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_VLAN_PTR, vlan_id, value));

    _sys_goldengate_vlan_set_default_vlan(lchip, &ds_vlan);
    SetDsVlan(V, transmitDisable_f, &ds_vlan, 0);
    SetDsVlan(V, receiveDisable_f, &ds_vlan, 0);
    SetDsVlan(V, vsiId_f, &ds_vlan, fid);
    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));
    /*clear vlan vsi*/
    cmd = DRV_IOR(DsVsi_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, (fid>>2), cmd, &ds_vlan_vsi));
    cmd = DRV_IOW(DsVsi_t, DRV_ENTRY_FLAG);
    for (temp = 0;temp < SYS_VLAN_VSI_PARAM_MAX; temp++)
    {
        field_id = (fid & 0x3)*SYS_VLAN_VSI_PARAM_MAX + temp;
        DRV_SET_FIELD_V(DsVsi_t, field_id, &ds_vlan_vsi, 0);
    }

        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, (fid>>2), cmd, &ds_vlan_vsi));

    /*set vlan tag*/
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        vlan_tag_bit_map[0] = 0xffffffff;
        vlan_tag_bit_map[1] = 0xffffffff;
        SetDsVlanTagBitMap(A, tagPortBitMap_f, &ds_vlan_tag_bit_map, vlan_tag_bit_map);
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan_tag_bit_map));
            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr+4096, cmd, &ds_vlan_tag_bit_map));

    }

    if (p_gg_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        ctc_l2dflt_addr_t def;
        sal_memset(&def, 0, sizeof(def));

        CTC_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));

        if (CTC_FLAG_ISSET(user_vlan->flag, CTC_MAC_LEARN_USE_LOGIC_PORT))
        {
            CTC_SET_FLAG(def.flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT);
        }
        def.fid = fid;
        def.l2mc_grp_id = fid;
        CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_l2_add_default_entry(lchip, &def));
    }
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;

}

/**
@brief The function is to remove the vlan

*/
int32
sys_goldengate_vlan_destory_vlan(uint8 lchip, uint16 vlan_id)
{
    uint8 field_id = 0;
    uint16 vlan_ptr = 0;
    uint16 old_profile_id = 0;
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 fid = 0;
    uint32 value = 0;
    DsVlan_m ds_vlan;
    DsVlanTagBitMap_m ds_vlan_tag_bit_map;
    DsVlanStatus_m ds_vlan_status;
    sys_vlan_profile_t* p_old_vlan_profile = NULL;

    sal_memset(&ds_vlan, 0, sizeof(DsVlan_m));
    sal_memset(&ds_vlan_status, 0, sizeof(DsVlanStatus_m));
    sal_memset(&ds_vlan_tag_bit_map, 0, sizeof(DsVlanTagBitMap_m));

    sys_goldengate_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove vlan_id: %d!\n", vlan_id);


    /*get vsiId and reset hw: ds_vlan*/
    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));
    GetDsVlan(A, vsiId_f, &ds_vlan, &fid);

    _sys_goldengate_vlan_set_default_vlan(lchip, &ds_vlan);
    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));


    SYS_VLAN_LOCK(lchip);
    CTC_BIT_UNSET(p_gg_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));

    /* delete vlan profile */
    if(NULL == p_gg_vlan_master[lchip]->p_vlan_profile_id)
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_INVALID_PTR;
    }

    old_profile_id = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
    if (0 != old_profile_id)
    {
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_profile_sw_read(lchip, old_profile_id, &p_old_vlan_profile));
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_profile_remove(lchip, p_old_vlan_profile));
        p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id] = 0;
    }

    /*reset hw: ds_vlan2ptr*/
    field_id = (vlan_id & 0x1)? DsVlan2Ptr_array_1_vlanPtr_f : DsVlan2Ptr_array_0_vlanPtr_f;
    cmd = DRV_IOW(DsVlan2Ptr_t, field_id);
    index = (vlan_id >> 1) & 0x7FF;
    value = 0;
    CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &value));
    CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_packet_tx_set_property(lchip, SYS_PKT_TX_TYPE_VLAN_PTR, vlan_id, value));

    /*vlan tag & vlan filting*/
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan_tag_bit_map));
            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr+4096, cmd, &ds_vlan_tag_bit_map));


        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);

            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan_status));
            CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr+4096, cmd, &ds_vlan_status));

    }

    if(!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    if ((p_gg_vlan_master[lchip]->vlan_mode == CTC_VLANPTR_MODE_USER_DEFINE1) && (p_gg_vlan_master[lchip]->sys_vlan_def_mode == 1))
    {
        int32 ret = 0;
        ctc_l2dflt_addr_t def;
        sal_memset(&def, 0, sizeof(def));
        def.fid = fid;
        ret = sys_goldengate_l2_remove_default_entry(lchip, &def);
        if (CTC_E_ENTRY_NOT_EXIST == ret)
        {
            ret = CTC_E_NONE;
        }
        CTC_ERROR_RETURN_VLAN_UNLOCK(ret);
    }

    CTC_BIT_UNSET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_vlan_ptr(uint8 lchip, uint16 vlan_id, uint16* vlan_ptr)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 value = 0;
    uint8 field_id = 0;

    CTC_PTR_VALID_CHECK(vlan_ptr);

    field_id = (vlan_id & 0x1)? DsVlan2Ptr_array_1_vlanPtr_f : DsVlan2Ptr_array_0_vlanPtr_f;
    cmd = DRV_IOR(DsVlan2Ptr_t, field_id);
    index = (vlan_id >> 1) & 0x7FF;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    *vlan_ptr = value;

    return CTC_E_NONE;
}
/**
@brief the function add ports to vlan
*/
int32
sys_goldengate_vlan_add_ports(uint8 lchip, uint16 vlan_id, ctc_port_bitmap_t port_bitmap)
{
    uint16 lport = 0;
    uint16 vlan_ptr = 0;
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint16 portid = 0;
    uint32 value = 0;
    uint8 gchip = 0;
    uint32 gport = 0;
    uint32 index = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    uint32 ds_vlan_status[2] = {0};

    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (loop1 == 0 || loop1 == 1 || loop1 == 8 || loop1 == 9)
        {
            continue;
        }
        if (port_bitmap[loop1] != 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    vlan_ptr = vlan_id;
    SYS_VLAN_LOCK(lchip);
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = vlan_ptr;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
        ds_vlan_status[0] |= port_bitmap[0];
        ds_vlan_status[1] |= port_bitmap[1];
        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));


        index = vlan_ptr + 4096;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
        ds_vlan_status[0] |= port_bitmap[8];
        ds_vlan_status[1] |= port_bitmap[9];
        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
    }

    if (!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));
    if ((p_gg_vlan_master[lchip]->vlan_mode != CTC_VLANPTR_MODE_USER_DEFINE1) || (p_gg_vlan_master[lchip]->sys_vlan_def_mode != 1))
    {
        return CTC_E_NONE;
    }

    sys_goldengate_get_gchip_id(lchip, &gchip);
    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (port_bitmap[loop1] == 0)
        {
            continue;
        }
        for (loop2 = 0; loop2 < 32; loop2++)
        {
            portid = loop1 * 32 + loop2;
            if (!CTC_BMP_ISSET(port_bitmap, portid))
            {
                continue;
            }
            lport = portid;
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

            l2dflt_addr.fid = (uint16)value;
            l2dflt_addr.member.mem_port = gport;
            ret = sys_goldengate_l2_add_port_to_default_entry(lchip, &l2dflt_addr);
            if (ret < 0)
            {
                    goto roll_back;
            }
        }

    }

    return CTC_E_NONE;

roll_back:
    sys_goldengate_vlan_remove_ports(lchip, vlan_id, port_bitmap);
    return CTC_E_NO_RESOURCE;

}
/**
@brief the function add port to vlan
*/
int32
sys_goldengate_vlan_add_port(uint8 lchip, uint16 vlan_id, uint16 gport)
{

    uint16 lport = 0;
    uint16 vlan_ptr = 0;
    uint8 sclice = 0;
    uint8 field_id = 0;
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    bool   ret = FALSE;
    int32 ret1 = 0;
    ctc_l2dflt_addr_t l2dflt_addr;

    DsVlanStatus_m ds_vlan_status;
    sal_memset(&ds_vlan_status, 0, sizeof(DsVlanStatus_m));
    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add global port:%d, to vlan_id:%d!\n", gport, vlan_id);

    vlan_ptr = vlan_id;

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
        if (FALSE == ret)
        {
            return CTC_E_CHIP_IS_REMOTE;
        }
    }
    sclice = (lport > 255)? 1 : 0;
    lport  = (lport > 255)? ((lport-256)&0xFF) : (lport&0xFF);
    SYS_VLAN_FILTER_PORT_CHECK(lport);

    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = (sclice == 1)? (4096+vlan_ptr) : vlan_ptr;
    }
    field_id =  lport;
    if (1 == _sys_goldengate_vlan_is_member_port(lchip, field_id, index))
    {
        return CTC_E_NONE;
    }

    /*write hw */
    cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));

    DRV_SET_FIELD_V(DsVlanStatus_t, field_id, &ds_vlan_status, 1);
    cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));

    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if ((p_gg_vlan_master[lchip]->vlan_mode == CTC_VLANPTR_MODE_USER_DEFINE1) && (p_gg_vlan_master[lchip]->sys_vlan_def_mode == 1))
    {
        l2dflt_addr.fid = (uint16)value;
        l2dflt_addr.member.mem_port = gport;
        ret1 = sys_goldengate_l2_add_port_to_default_entry(lchip, &l2dflt_addr);
        if (ret1 < 0)
        {
            cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));

            DRV_SET_FIELD_V(DsVlanStatus_t, field_id, &ds_vlan_status, 0);
            cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
            return CTC_E_NO_RESOURCE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_set_tagged_port(uint8 lchip, uint16 vlan_id, uint16 gport, bool tagged)
{

    uint16 lport = 0;
    uint8 sclice = 0;
    uint16 vlan_ptr = 0;
    uint32 index = 0;
    uint32 cmd = 0;

    bool   ret = FALSE;
    uint32 vlan_tag_bit_map[2] = {0};
    DsVlanTagBitMap_m  ds_vlan_tag_bit_map;

    sal_memset(&ds_vlan_tag_bit_map, 0, sizeof(DsVlanTagBitMap_m));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add global port:%d, to vlan_id:%d!\n", gport, vlan_id);

    vlan_ptr = vlan_id;

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
        if (FALSE == ret)
        {
            return CTC_E_CHIP_IS_REMOTE;
        }
    }
    sclice = (lport > 255)? 1 : 0;
    lport  = (lport > 255)? ((lport-256)&0xFF) : (lport&0xFF);
    SYS_VLAN_FILTER_PORT_CHECK(lport);

    cmd = DRV_IOR(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = (sclice == 1)? (4096 + vlan_ptr) : vlan_ptr;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bit_map));
    sal_memcpy(&vlan_tag_bit_map, &ds_vlan_tag_bit_map, sizeof(vlan_tag_bit_map));
    if (tagged)
    {
        if (lport < 32)
        {
            vlan_tag_bit_map[0] = vlan_tag_bit_map[0] | (1 << lport);
        }
        else
        {
            vlan_tag_bit_map[1] = vlan_tag_bit_map[1] | (1 << (lport-32));
        }
        SetDsVlanTagBitMap(A, tagPortBitMap_f, &ds_vlan_tag_bit_map, vlan_tag_bit_map);
    }
    else
    {
        if (lport < 32)
        {
            vlan_tag_bit_map[0] = vlan_tag_bit_map[0] & (~(1 << lport));
        }
        else
        {
            vlan_tag_bit_map[1] = vlan_tag_bit_map[1] & (~(uint32)(1 << (lport-32)));
        }
        SetDsVlanTagBitMap(A, tagPortBitMap_f, &ds_vlan_tag_bit_map, vlan_tag_bit_map);
    }
    cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bit_map));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_set_tagged_ports(uint8 lchip, uint16 vlan_id, ctc_port_bitmap_t port_bitmap)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint16 vlan_ptr = 0;
    uint32 ds_vlan_tag_bit_map[2] = {0};

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    vlan_ptr = vlan_id;
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = vlan_ptr;
        ds_vlan_tag_bit_map[0] = port_bitmap[0];
        ds_vlan_tag_bit_map[1] = port_bitmap[1];
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bit_map));

        index = vlan_ptr + 4096;
        ds_vlan_tag_bit_map[0] = port_bitmap[8];
        ds_vlan_tag_bit_map[1] = port_bitmap[9];
        cmd = DRV_IOW(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bit_map));
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_tagged_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    uint32 index = 0;
    bool   ret = FALSE;
    DsVlanTagBitMap_m ds_vlan_tag_bitmap1;
    DsVlanTagBitMap_m ds_vlan_tag_bitmap2;

    sal_memset(&ds_vlan_tag_bitmap1, 0, sizeof(DsVlanTagBitMap_m));
    sal_memset(&ds_vlan_tag_bitmap2, 0, sizeof(DsVlanTagBitMap_m));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    ret = sys_goldengate_chip_is_local(lchip, gchip);
    if (FALSE == ret)
    {
        return CTC_E_CHIP_IS_REMOTE;
    }
    CTC_PTR_VALID_CHECK(port_bitmap);

    sal_memset(port_bitmap, 0, sizeof(ctc_port_bitmap_t));

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Get tagged ports of vlan:%d!\n", vlan_id);

    vlan_ptr = vlan_id;

    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = vlan_ptr;
        cmd = DRV_IOR(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bitmap1));
        index = vlan_ptr + 4096;
        cmd = DRV_IOR(DsVlanTagBitMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_tag_bitmap2));
    }


    sal_memcpy(port_bitmap, &ds_vlan_tag_bitmap1, 8);
    sal_memcpy((uint8*)port_bitmap + 32, (uint8*)&ds_vlan_tag_bitmap2, 8);

    return CTC_E_NONE;
}

/**
@brief To remove member port from a vlan
*/
int32
sys_goldengate_vlan_remove_port(uint8 lchip, uint16 vlan_id, uint16 gport)
{

    uint16 lport = 0;
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    uint8 sclice = 0;
    uint8 field_id = 0;
    uint32 index = 0;
    uint32 value = 0;
    bool ret = FALSE;
    ctc_l2dflt_addr_t l2dflt_addr;

    DsVlanStatus_m ds_vlan_status;
    sal_memset(&l2dflt_addr, 0, sizeof(l2dflt_addr));
    sal_memset(&ds_vlan_status, 0, sizeof(DsVlanStatus_m));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Remove global port:%d from vlan:%d!\n", gport, vlan_id);

    vlan_ptr = vlan_id;

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport));
        if (FALSE == ret)
        {
            return CTC_E_CHIP_IS_REMOTE;
        }
    }
    sclice = (lport > 255)? 1 : 0;
    lport  = (lport > 255)? ((lport-256)&0xFF) : (lport&0xFF);
    SYS_VLAN_FILTER_PORT_CHECK(lport);

    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = (sclice == 1)? (4096+vlan_ptr) : vlan_ptr;
    }
    field_id =  lport;
    if (0 == _sys_goldengate_vlan_is_member_port(lchip, field_id, index))
    {
        return CTC_E_MEMBER_PORT_NOT_EXIST;
    }

    /*write hw */
    cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
    DRV_SET_FIELD_V(DsVlanStatus_t, field_id, &ds_vlan_status, 0);
    cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));

    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if ((p_gg_vlan_master[lchip]->vlan_mode == CTC_VLANPTR_MODE_USER_DEFINE1) && (p_gg_vlan_master[lchip]->sys_vlan_def_mode == 1))
    {
        l2dflt_addr.fid = (uint16)value;
        l2dflt_addr.member.mem_port = gport;
        sys_goldengate_l2_remove_port_from_default_entry(lchip, &l2dflt_addr);
    }

    return CTC_E_NONE;
}

/**
@brief To remove member ports from a vlan
*/
int32
sys_goldengate_vlan_remove_ports(uint8 lchip, uint16 vlan_id, ctc_port_bitmap_t port_bitmap)
{
    uint32 cmd = 0;
    uint32 gport = 0;
    uint16 index = 0;
    uint8 gchip = 0;
    uint16 lport = 0;
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint16 portid = 0;
    uint16 vlan_ptr = 0;
    uint32 value = 0;

    ctc_l2dflt_addr_t l2dflt_addr;
    uint32 ds_vlan_status[2] = {0};

    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (loop1 == 0 || loop1 == 1 || loop1 == 8 || loop1 == 9)
        {
            continue;
        }
        if (port_bitmap[loop1] != 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    vlan_ptr = vlan_id;
    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = vlan_ptr;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
        ds_vlan_status[0] &= (~port_bitmap[0]);
        ds_vlan_status[1] &= (~port_bitmap[1]);
        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));

        index = vlan_ptr + 4096;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
        ds_vlan_status[0] &= (~port_bitmap[8]);
        ds_vlan_status[1] &= (~port_bitmap[9]);
        cmd = DRV_IOW(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status));
    }
    if (!IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        return CTC_E_NONE;
    }
    /*remove mem_port one by one from vlan_default_entry*/
    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));
    if ((p_gg_vlan_master[lchip]->vlan_mode != CTC_VLANPTR_MODE_USER_DEFINE1) || (p_gg_vlan_master[lchip]->sys_vlan_def_mode != 1))
    {
        return CTC_E_NONE;
    }
    sys_goldengate_get_gchip_id(lchip, &gchip);
    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (port_bitmap[loop1] == 0)
        {
            continue;
        }
        for (loop2 = 0; loop2 < 32; loop2++)
        {
            portid = loop1 * 32 + loop2;
            if (!CTC_BMP_ISSET(port_bitmap, portid))
            {
                continue;
            }
            lport = portid;
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

            l2dflt_addr.fid = (uint16)value;
            l2dflt_addr.member.mem_port = gport;
            sys_goldengate_l2_remove_port_from_default_entry(lchip, &l2dflt_addr);
        }
    }
    return CTC_E_NONE;
}

/**
@brief show vlan's member ports
*/
int32
sys_goldengate_vlan_get_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    uint32 index = 0;
    bool   ret = FALSE;
    DsVlanStatus_m ds_vlan_status1;
    DsVlanStatus_m ds_vlan_status2;

    sal_memset(&ds_vlan_status1, 0, sizeof(DsVlanStatus_m));
    sal_memset(&ds_vlan_status2, 0, sizeof(DsVlanStatus_m));

    SYS_VLAN_INIT_CHECK(lchip);
    ret = sys_goldengate_chip_is_local(lchip, gchip);
    if (FALSE == ret)
    {
        return CTC_E_CHIP_IS_REMOTE;
    }

    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(port_bitmap);

    sal_memset(port_bitmap, 0, sizeof(ctc_port_bitmap_t));

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Get member ports of vlan:%d!\n", vlan_id);

    vlan_ptr = vlan_id;

    if (p_gg_vlan_master[lchip]->vlan_port_bitmap_mode == 0)
    {
        index = vlan_ptr;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status1));
        index = vlan_ptr + 4096;
        cmd = DRV_IOR(DsVlanStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan_status2));
    }

    sal_memcpy(port_bitmap, &ds_vlan_status1, 8);
    sal_memcpy((uint8*)port_bitmap + 32, (uint8*)&ds_vlan_status2, 8);

    return CTC_E_NONE;
}


int32
sys_goldengate_vlan_get_vlan_create_mode(uint8 lchip, uint16 vlan_id, uint8* mode)
{
    uint16 vlan_ptr;

    CTC_PTR_VALID_CHECK(mode);

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_ptr(lchip, vlan_id, & vlan_ptr));

    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_LOCK(lchip);
    if(IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        *mode = 1;
    }
    else
    {
        *mode = 0;
    }
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_set_mac_auth(uint8 lchip, uint16 vlan_id, bool enable)
{
    uint32   fid = 0;
    uint32   cmd = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_id, cmd, &fid));

    CTC_ERROR_RETURN(sys_goldengate_l2_default_entry_set_mac_auth(lchip, fid, enable));
    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_mac_auth(uint8 lchip, uint16 vlan_id, bool* enable)
{
    uint32   fid = 0;
    uint32   cmd = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(enable);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_id, cmd, &fid));

    CTC_ERROR_RETURN(sys_goldengate_l2_default_entry_get_mac_auth(lchip, fid, enable));
    return CTC_E_NONE;
}


#define __VLAN_PROFILE__

STATIC int32
_sys_goldengate_vlan_profile_dump(void* node, void* user_data)
{
    sys_vlan_profile_t* p_vlan_profile = (sys_vlan_profile_t*)(((ctc_spool_node_t*)node)->data);
    uint8 lchip = p_vlan_profile->lchip;

    SYS_VLAN_INIT_CHECK(lchip);

    if (0xFFFFFFFF == REF_COUNT(p_vlan_profile))
    {
        SYS_VLAN_DBG_DUMP("%-12u%s\n", p_vlan_profile->profile_id, "-");
    }
    else
    {
        SYS_VLAN_DBG_DUMP("%-12u%u\n", p_vlan_profile->profile_id, REF_COUNT(p_vlan_profile));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_db_dump_all(uint8 lchip)
{
    SYS_VLAN_INIT_CHECK(lchip);
    SYS_VLAN_DBG_DUMP("%-12s%s\n", "ProfileId", "RefCount");
    SYS_VLAN_DBG_DUMP("----------------------\n");

    SYS_VLAN_LOCK(lchip);
    ctc_spool_traverse(p_gg_vlan_master[lchip]->vprofile_spool,
                       (spool_traversal_fn)_sys_goldengate_vlan_profile_dump, NULL);
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_profile_index_build(uint8 lchip, uint16* p_index, uint8 reverse)
{
    sys_goldengate_opf_t  opf;
    uint32 value_32 = 0;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type  = OPF_VLAN_PROFILE;
    opf.reverse    = reverse;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_index = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_vlan_profile_add(uint8 lchip, sys_sw_vlan_profile_t* p_ds_vlan_profile_in,
                                sys_vlan_profile_t* p_old_profile,
                                uint16* profile_id_out)
{
    int32 ret = 0;
    uint16 new_profile_id = 0;
    uint16 old_profile_id = 0;
    uint16 profile_id_index = 0;
    sys_vlan_profile_t* p_vlan_profile_db_new = NULL;
    sys_vlan_profile_t* p_vlan_profile_db_get = NULL;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_ds_vlan_profile_in);
    CTC_PTR_VALID_CHECK(profile_id_out);

    p_vlan_profile_db_new = mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_profile_t));
    if (NULL == p_vlan_profile_db_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(&p_vlan_profile_db_new->sw_vlan_profile, p_ds_vlan_profile_in, sizeof(sys_sw_vlan_profile_t));
    p_vlan_profile_db_new->lchip = lchip;

    if(p_old_profile)
    {
        if(TRUE == _sys_goldengate_vlan_hash_compare(p_old_profile, p_vlan_profile_db_new))
        {
            *profile_id_out = p_old_profile->profile_id;
            mem_free(p_vlan_profile_db_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_old_profile->profile_id;
        p_vlan_profile_db_new->profile_id = old_profile_id;
    }

    ret = ctc_spool_add(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile_db_new, p_old_profile, &p_vlan_profile_db_get);

    if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(p_vlan_profile_db_new);
    }

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto ERROR_RETURN;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            ret = _sys_goldengate_vlan_profile_index_build(lchip, &new_profile_id, 0);

            if (ret < 0)
            {
                if (p_ds_vlan_profile_in->egress_vlan_span_en || p_ds_vlan_profile_in->ingress_vlan_span_en)
                {
                    ret = _sys_goldengate_vlan_profile_index_build(lchip, &new_profile_id, 1);
                }
            }

            if (ret < 0)
            {
                ctc_spool_remove(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile_db_new, NULL);
                mem_free(p_vlan_profile_db_new);
                goto ERROR_RETURN;
            }

            p_vlan_profile_db_get->profile_id = new_profile_id;
        }

        *profile_id_out = p_vlan_profile_db_get->profile_id;
        profile_id_index = p_vlan_profile_db_get->profile_id;
        p_gg_vlan_master[lchip]->p_vlan_profile[profile_id_index] = p_vlan_profile_db_get;
    }

    /*key is found, so there is an old ad need to be deleted.*/
    /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
    if (p_old_profile && p_vlan_profile_db_get->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_goldengate_vlan_profile_remove(lchip, p_old_profile));
    }

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}

STATIC int32
_sys_goldengate_vlan_profile_hw_write(uint8 lchip, uint16 profile_id, sys_sw_vlan_profile_t* p_ds_vlan_profile)
{
    uint32 cmd = 0;
    DsVlanProfile_m ds_vlan_profile;

    sal_memset(&ds_vlan_profile, 0, sizeof(DsVlanProfile_m));

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SetDsVlanProfile(V, vlanStormCtlEn_f, &ds_vlan_profile, p_ds_vlan_profile->vlan_storm_ctl_en);
    SetDsVlanProfile(V, egressVlanAclLabel0_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_acl_label);
    SetDsVlanProfile(V, ingressVlanAclEn3_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_en3);
    SetDsVlanProfile(V, ingressVlanAclEn2_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_en2);
    SetDsVlanProfile(V, ingressVlanAclEn1_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_en1);
    SetDsVlanProfile(V, ingressVlanAclEn0_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_en0);
    SetDsVlanProfile(V, ingressVlanAclLabel3_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_label3);
    SetDsVlanProfile(V, ingressVlanAclLabel2_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_label2);
    SetDsVlanProfile(V, ingressVlanAclLabel1_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_label1);
    SetDsVlanProfile(V, ingressVlanAclLabel0_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_label0);
    SetDsVlanProfile(V, ingressVlanAclLookupType3_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_lookup_type3);
    SetDsVlanProfile(V, ingressVlanAclLookupType2_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_lookup_type2);
    SetDsVlanProfile(V, ingressVlanAclLookupType1_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_lookup_type1);
    SetDsVlanProfile(V, ingressVlanAclLookupType0_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_lookup_type0);
    SetDsVlanProfile(V, ingressVlanAclRoutedOnly3_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_routed_only3);
    SetDsVlanProfile(V, ingressVlanAclRoutedOnly2_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_routed_only2);
    SetDsVlanProfile(V, ingressVlanAclRoutedOnly1_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_routed_only1);
    SetDsVlanProfile(V, ingressVlanAclRoutedOnly0_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_acl_routed_only0);
    SetDsVlanProfile(V, egressVlanAclRoutedOnly0_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_acl_routed_only0);
    SetDsVlanProfile(V, ingressVlanSpanEn_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_span_en);
    SetDsVlanProfile(V, egressVlanSpanEn_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_span_en);
    SetDsVlanProfile(V, forceIpv6Lookup_f, &ds_vlan_profile, p_ds_vlan_profile->force_ipv6_lookup);
    SetDsVlanProfile(V, forceIpv4Lookup_f, &ds_vlan_profile, p_ds_vlan_profile->force_ipv4_lookup);
    SetDsVlanProfile(V, ingressVlanSpanId_f, &ds_vlan_profile, p_ds_vlan_profile->ingress_vlan_span_id);
    SetDsVlanProfile(V, egressVlanSpanId_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_span_id);
    SetDsVlanProfile(V, fipExceptionType_f, &ds_vlan_profile, p_ds_vlan_profile->fip_exception_type);
    SetDsVlanProfile(V, vlanStormCtlPtr_f, &ds_vlan_profile, p_ds_vlan_profile->vlan_storm_ctl_ptr);
    SetDsVlanProfile(V, egressVlanAclEn0_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_acl_en0);
    SetDsVlanProfile(V, egressVlanAclLookupType0_f, &ds_vlan_profile, p_ds_vlan_profile->egress_vlan_acl_lookup_type0);
    SetDsVlanProfile(V, ptpIndex_f, &ds_vlan_profile, p_ds_vlan_profile->ptp_index);

    cmd = DRV_IOW(DsVlanProfile_t, DRV_ENTRY_FLAG);


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (uint32)profile_id, cmd, &ds_vlan_profile));


    return CTC_E_NONE;
}

/*set profile_id through vlan_id;*/
STATIC int32
_sys_goldengate_vlan_set_vlan_profile(uint8 lchip, uint16 vlan_id, sys_vlan_property_t prop, uint32 value)
{
    int32 ret = 0;
    uint16 old_profile_id = 0;
    uint16 new_profile_id = 0;
    sys_vlan_profile_t* p_old_vlan_profile = NULL;

    sys_sw_vlan_profile_t  new_profile;
    sys_sw_vlan_profile_t  zero_profile;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&zero_profile, 0, sizeof(sys_sw_vlan_profile_t));
    sal_memset(&new_profile, 0, sizeof(sys_sw_vlan_profile_t));

    CTC_PTR_VALID_CHECK(p_gg_vlan_master[lchip]->p_vlan_profile_id);
    old_profile_id = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];

    /*db exists: set 0 or 1 continue.
      db non-ex: set 0 return, set 1 continue.*/

    if (0 == old_profile_id)
    {
        if (0 == value)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        _sys_goldengate_vlan_profile_sw_read(lchip, old_profile_id,  &p_old_vlan_profile);
        sal_memcpy(&new_profile, &(p_old_vlan_profile->sw_vlan_profile), sizeof(sys_sw_vlan_profile_t));
    }

    switch (prop)
    {
        case    SYS_VLAN_PROP_VLAN_STORM_CTL_PTR:
            new_profile.vlan_storm_ctl_ptr           = value;
            break;

        case    SYS_VLAN_PROP_VLAN_STORM_CTL_EN:
            new_profile.vlan_storm_ctl_en            = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID:
            new_profile.ingress_vlan_span_id         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN:
            new_profile.ingress_vlan_span_en         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY0:
            new_profile.ingress_vlan_acl_routed_only0 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY1:
            new_profile.ingress_vlan_acl_routed_only1 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY2:
            new_profile.ingress_vlan_acl_routed_only2 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY3:
            new_profile.ingress_vlan_acl_routed_only3 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL0:
            new_profile.ingress_vlan_acl_label0       = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL1:
            new_profile.ingress_vlan_acl_label1       = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL2:
            new_profile.ingress_vlan_acl_label2       = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL3:
            new_profile.ingress_vlan_acl_label3       = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3:
            new_profile.ingress_vlan_acl_en3         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2:
            new_profile.ingress_vlan_acl_en2         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1:
            new_profile.ingress_vlan_acl_en1         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0:
            new_profile.ingress_vlan_acl_en0         = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0:
            new_profile.ingress_vlan_acl_lookup_type0 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE1:
            new_profile.ingress_vlan_acl_lookup_type1 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE2:
            new_profile.ingress_vlan_acl_lookup_type2 = value;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE3:
            new_profile.ingress_vlan_acl_lookup_type3 = value;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV6LOOKUP:
            new_profile.force_ipv6_lookup            = value;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV4LOOKUP:
            new_profile.force_ipv4_lookup            = value;
            break;

        case    SYS_VLAN_PROP_FIP_EXCEPTION_TYPE:
            new_profile.fip_exception_type           = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID:
            new_profile.egress_vlan_span_id          = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN:
            new_profile.egress_vlan_span_en          = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY:
            new_profile.egress_vlan_acl_routed_only0 = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL:
            new_profile.egress_vlan_acl_label        = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0:
            new_profile.egress_vlan_acl_en0          = value;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0:
            new_profile.egress_vlan_acl_lookup_type0 = value;
            break;

        case    SYS_VLAN_PROP_PTP_CLOCK_ID:
            new_profile.ptp_index = value;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    /*setting 0 leads to zero_profile, then delete this profile.*/
    if ((0 == value) && (!sal_memcmp(&new_profile, &zero_profile, sizeof(sys_sw_vlan_profile_t))))
    { /*del prop*/

        ret = _sys_goldengate_vlan_profile_remove(lchip, p_old_vlan_profile);
        new_profile_id = 0;
        /*no need to del hw*/

    }
    else
    { /*set 1 or 0 to prop*/
        ret = _sys_goldengate_vlan_profile_add(lchip, &new_profile, p_old_vlan_profile, &new_profile_id);
        ret = ret ? ret : _sys_goldengate_vlan_profile_hw_write(lchip, new_profile_id, &new_profile);
    }

    p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id] = new_profile_id;
    return ret;
}

/*get profile_id through vlan_id;*/
STATIC int32
_sys_goldengate_vlan_get_vlan_profile(uint8 lchip, uint16 vlan_id, sys_vlan_property_t prop, uint32* value)
{
    uint16 old_profile_id = 0;
    sys_vlan_profile_t*  p_ds_vlan_profile;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(value);
    CTC_PTR_VALID_CHECK(p_gg_vlan_master[lchip]->p_vlan_profile_id);
    old_profile_id = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];

    _sys_goldengate_vlan_profile_sw_read(lchip, old_profile_id, &p_ds_vlan_profile);

    if (0 == old_profile_id)
    { /*no profile present means all profile prop is zero!*/
        *value = 0;
    }
    else
    {
        switch (prop)
        {
        case    SYS_VLAN_PROP_VLAN_STORM_CTL_PTR:
            *value  = p_ds_vlan_profile->sw_vlan_profile.vlan_storm_ctl_ptr;
            break;

        case    SYS_VLAN_PROP_VLAN_STORM_CTL_EN:
            *value  = p_ds_vlan_profile->sw_vlan_profile.vlan_storm_ctl_en;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_span_id;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_span_en;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_routed_only0;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY1:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_routed_only1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY2:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_routed_only2;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY3:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_routed_only3;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_label0;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL1:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_label1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL2:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_label2;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL3:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_label3;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_en3;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_en2;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_en1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_en0;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_lookup_type0;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE1:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_lookup_type1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE2:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_lookup_type2;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE3:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ingress_vlan_acl_lookup_type3;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV6LOOKUP:
            *value  = p_ds_vlan_profile->sw_vlan_profile.force_ipv6_lookup;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV4LOOKUP:
            *value  = p_ds_vlan_profile->sw_vlan_profile.force_ipv4_lookup;
            break;

        case    SYS_VLAN_PROP_FIP_EXCEPTION_TYPE:
            *value  = p_ds_vlan_profile->sw_vlan_profile.fip_exception_type;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_span_id;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_span_en;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_acl_routed_only0;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_acl_label;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_acl_en0;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0:
            *value  = p_ds_vlan_profile->sw_vlan_profile.egress_vlan_acl_lookup_type0;
            break;

        case    SYS_VLAN_PROP_PTP_CLOCK_ID:
            *value  = p_ds_vlan_profile->sw_vlan_profile.ptp_index;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;

}

/**
@brief    Config DsVlan DsVsi DsVlanProfile properties for sys l3if mirror security stp model
*/
int32
sys_goldengate_vlan_set_internal_property(uint8 lchip, uint16 vlan_id, sys_vlan_property_t vlan_prop, uint32 value)
{
    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    uint32 field_id = 0;
    uint32 index = 0;
    int32 ret = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d, vlan_prop:%d, value:%d \n", vlan_id, vlan_prop, value);
    vlan_ptr = vlan_id;

    if (vlan_prop >= SYS_VLAN_PROP_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    else
    {
        index = vlan_ptr;
        switch (vlan_prop)
        {
            /* vlan proprety for sys layer*/
            case SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD:
                cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
                field_id = DsVsi_array_0_macSecurityVsiDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
                cmd = DRV_IOW(DsVsi_t, field_id);
                value = (value) ? 1 : 0;
                index = (index >> 2) & 0xFFF;
                break;

            case SYS_VLAN_PROP_STP_ID:
                cmd = DRV_IOW(DsVlan_t, DsVlan_stpId_f);
                break;

            case SYS_VLAN_PROP_INTERFACE_ID:
                cmd = DRV_IOW(DsVlan_t, DsVlan_interfaceId_f);
                break;

            case SYS_VLAN_PROP_SRC_QUEUE_SELECT:
                cmd = DRV_IOW(DsVlan_t, DsVlan_srcQueueSelect_f);
                value = (value) ? 1 : 0;
                break;

            case SYS_VLAN_PROP_EGRESS_FILTER_EN:
                cmd = DRV_IOW(DsVlan_t, DsVlan_egressFilteringEn_f);
                value = (value) ? 1 : 0;
                break;

            case SYS_VLAN_PROP_INGRESS_FILTER_EN:
                cmd = DRV_IOW(DsVlan_t, DsVlan_ingressFilteringEn_f);
                value = (value) ? 1 : 0;
                break;

            case SYS_VLAN_PROP_UCAST_DISCARD:
                cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
                field_id = DsVsi_array_0_ucastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
                cmd = DRV_IOW(DsVsi_t, field_id);
                value = (value) ? 1 : 0;
                index = (index >> 2) & 0xFFF;
                break;

            case SYS_VLAN_PROP_MCAST_DISCARD:
                cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
                field_id = DsVsi_array_0_mcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
                cmd = DRV_IOW(DsVsi_t, field_id);
                value = (value) ? 1 : 0;
                index = (index >> 2) & 0xFFF;
                break;

             case SYS_VLAN_PROP_BCAST_DISCARD:
                cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
                field_id = DsVsi_array_0_bcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
                cmd = DRV_IOW(DsVsi_t, field_id);
                value = (value) ? 1 : 0;
                index = (index >> 2) & 0xFFF;
                break;

            case SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION:
                cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
                field_id = DsVsi_array_0_macSecurityVsiExceptionEn_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
                cmd = DRV_IOW(DsVsi_t, field_id);
                value = (value) ? 1 : 0;
                index = (index >> 2) & 0xFFF;
                break;

            default:
                SYS_VLAN_LOCK(lchip);
                ret = _sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, vlan_prop, value);
                CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

                value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
                SYS_VLAN_UNLOCK(lchip);
                cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
                break;

        }
    }

    if (cmd)
    {

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

    }

    return CTC_E_NONE;
}

/**
@brief    Get DsVlan DsVsi DsVlanProfile properties
*/
int32
sys_goldengate_vlan_get_internal_property(uint8 lchip, uint16 vlan_id, sys_vlan_property_t vlan_prop, uint32* value)
{
    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    uint32 field_id = 0;
    uint32 index = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(value);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(value);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d \n", vlan_id, vlan_prop);
    vlan_ptr = vlan_id;

    /* zero value*/
    *value = 0;

    switch (vlan_prop)
    {
    /* vlan proprety for sys layer*/
    case SYS_VLAN_PROP_STP_ID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_stpId_f);
        index = vlan_ptr;
        break;

    case SYS_VLAN_PROP_INTERFACE_ID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_interfaceId_f);
        index = vlan_ptr;
        break;

    case SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_macSecurityVsiDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case SYS_VLAN_PROP_SRC_QUEUE_SELECT:
        cmd = DRV_IOR(DsVlan_t, DsVlan_srcQueueSelect_f);
        index = vlan_ptr;
        break;

    case SYS_VLAN_PROP_EGRESS_FILTER_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_egressFilteringEn_f);
        index = vlan_ptr;
        break;

    case SYS_VLAN_PROP_INGRESS_FILTER_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_ingressFilteringEn_f);
        index = vlan_ptr;
        break;

    case SYS_VLAN_PROP_UCAST_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_ucastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case SYS_VLAN_PROP_MCAST_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_mcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case SYS_VLAN_PROP_BCAST_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_bcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_macSecurityVsiExceptionEn_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    default:
        SYS_VLAN_LOCK(lchip);
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, vlan_prop, value));
        SYS_VLAN_UNLOCK(lchip);
        cmd = 0;
        break;

    }

    if (cmd)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, value));
    }

    return CTC_E_NONE;
}

/**
@brief    Config  vlan properties
*/
int32
sys_goldengate_vlan_set_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32 value)
{

    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    uint32 index = 0;
    uint32 field_id = 0;
    ctc_l2dflt_addr_t def;
    sal_memset(&def, 0, sizeof(def));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d, vlan_prop:%d, value:%d \n", vlan_id, vlan_prop, value);
    vlan_ptr = vlan_id;

    index = vlan_ptr;
    SYS_VLAN_LOCK(lchip);
    switch (vlan_prop)
    {
    /* vlan proprety for ctc layer*/
    case CTC_VLAN_PROP_RECEIVE_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_receiveDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_BRIDGE_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_bridgeDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_TRANSMIT_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_transmitDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_LEARNING_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_vsiLearningDisable_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOW(DsVsi_t, field_id);
        value = (value) ? 0 : 1;
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DHCP_EXCP_TYPE:
        cmd = DRV_IOW(DsVlan_t, DsVlan_dhcpExceptionType_f);
        if (value > CTC_EXCP_DISCARD)
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_VLAN_PROP_ARP_EXCP_TYPE:
        cmd = DRV_IOW(DsVlan_t, DsVlan_arpExceptionType_f);
        if (value > CTC_EXCP_DISCARD)
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_VLAN_PROP_IGMP_SNOOP_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_igmpSnoopEn_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOW(DsVsi_t, field_id);
        value = (value) ? 1 : 0;
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_UCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_ucastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOW(DsVsi_t, field_id);
        value = (value) ? 1 : 0;
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_MCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_mcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOW(DsVsi_t, field_id);
        value = (value) ? 1 : 0;
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_BCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_bcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOW(DsVsi_t, field_id);
        value = (value) ? 1 : 0;
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_l2_set_fid_property(lchip, index, CTC_L2_FID_PROP_UNKNOWN_MCAST_COPY_TO_CPU, value));
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;

    case CTC_VLAN_PROP_FID:
        if (REGULAR_INVALID_FID(value))
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_FDB_INVALID_FID;
        }
        cmd = DRV_IOW(DsVlan_t, DsVlan_vsiId_f);
#if 0
        sys_goldengate_vlan_get_property(lchip, vlan_id, CTC_VLAN_PROP_FID, &fid);
        def.fid = (uint16)fid;
        CTC_ERROR_RETURN(sys_goldengate_l2_remove_default_entry(lchip, &def));

        sal_memset(&def, 0, sizeof(def));
        def.fid = (uint16)value;
        def.l2mc_grp_id = vlan_ptr;/* equal to vlan_ptr. discussed.*/
        CTC_ERROR_RETURN(sys_goldengate_l2_add_default_entry(lchip, &def));
#endif
        break;

    case CTC_VLAN_PROP_IPV4_BASED_L2MC:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV4LOOKUP, value));
        value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
        cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
        break;

    case CTC_VLAN_PROP_IPV6_BASED_L2MC:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV6LOOKUP, value));
        value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
        cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
        break;

    case CTC_VLAN_PROP_FIP_EXCP_TYPE:
        value = value ^ 1;
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FIP_EXCEPTION_TYPE, value));
        value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
        cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
        break;

    case CTC_VLAN_PROP_PTP_EN:
        if (value > SYS_VLAN_PTP_CLOCK_ID_MAX)
        {
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_PTP_CLOCK_ID, value));
        value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
        cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
        break;

    default:
         SYS_VLAN_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_VLAN_UNLOCK(lchip);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32* value)
{
    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    uint32 index = 0;
    uint32 field_id = 0;
    bool vlan_profile_prop = FALSE;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(value);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d \n", vlan_id, vlan_prop);

    vlan_ptr = vlan_id;

    /* zero value*/
    *value = 0;
    index = vlan_ptr;

    switch (vlan_prop)
    {
    /* vlan proprety for ctc layer*/
    case CTC_VLAN_PROP_RECEIVE_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_receiveDisable_f);
        break;

    case CTC_VLAN_PROP_BRIDGE_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_bridgeDisable_f);
        break;

    case CTC_VLAN_PROP_TRANSMIT_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_transmitDisable_f);
        break;

    case CTC_VLAN_PROP_LEARNING_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_vsiLearningDisable_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DHCP_EXCP_TYPE:
        cmd = DRV_IOR(DsVlan_t, DsVlan_dhcpExceptionType_f);
        break;

    case CTC_VLAN_PROP_ARP_EXCP_TYPE:
        cmd = DRV_IOR(DsVlan_t, DsVlan_arpExceptionType_f);
        break;

    case CTC_VLAN_PROP_IGMP_SNOOP_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_igmpSnoopEn_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_UCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_ucastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_MCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_mcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_BCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        field_id = DsVsi_array_0_bcastDiscard_f + (index & 0x3)*SYS_VLAN_VSI_PARAM_MAX ;
        cmd = DRV_IOR(DsVsi_t, field_id);
        index = (index >> 2) & 0xFFF;
        break;

    case CTC_VLAN_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        CTC_ERROR_RETURN_VLAN_UNLOCK(DRV_IOCTL(lchip, vlan_ptr, cmd, &index));
        CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_l2_get_fid_property(lchip, index, CTC_L2_FID_PROP_UNKNOWN_MCAST_COPY_TO_CPU, value));
        return CTC_E_NONE;

    case CTC_VLAN_PROP_FID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_vsiId_f);
        break;

    case CTC_VLAN_PROP_IPV4_BASED_L2MC:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV4LOOKUP, value));
        vlan_profile_prop = TRUE;
        break;

    case CTC_VLAN_PROP_IPV6_BASED_L2MC:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV6LOOKUP, value));
        vlan_profile_prop = TRUE;
        break;

    case CTC_VLAN_PROP_FIP_EXCP_TYPE:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FIP_EXCEPTION_TYPE, value));
        *value = *value ^ 1;
        vlan_profile_prop = TRUE;
        break;

    case CTC_VLAN_PROP_PTP_EN:
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_PTP_CLOCK_ID, value));
        vlan_profile_prop = TRUE;
        break;

    default:
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
    SYS_VLAN_UNLOCK(lchip);

    if (!vlan_profile_prop)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, value));

        if (CTC_VLAN_PROP_BRIDGE_EN == vlan_prop || CTC_VLAN_PROP_LEARNING_EN == vlan_prop ||
            CTC_VLAN_PROP_RECEIVE_EN == vlan_prop || CTC_VLAN_PROP_TRANSMIT_EN == vlan_prop)
        {
            *value = !(*value);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_set_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32  value)
{

    uint32 cmd = 1;
    uint16 vlan_ptr = 0;
    uint32 prop = 0;
    uint32 acl_en = 0;
    uint32 get_value = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                     "vlan_id :%d, vlan_prop:%d, dir:%d ,value:%d \n", vlan_id, vlan_prop, dir, value);
    vlan_ptr = vlan_id;

    SYS_VLAN_LOCK(lchip);
    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (vlan_prop)
        {
        case CTC_VLAN_DIR_PROP_ACL_EN:
            if (value > (CTC_ACL_EN_0 + CTC_ACL_EN_1 + CTC_ACL_EN_2 + CTC_ACL_EN_3))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }

            CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_global_check_acl_memory(lchip, CTC_INGRESS, value));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_1);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_2);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_3);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            cmd = 0;

            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0, &get_value));

            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GG_MAX_ACL_VLAN_CLASSID))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL0;
            }
            else
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_VLAN_ACL_EN_FIRST;
            }
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_1:
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1, &get_value));

            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GG_MAX_ACL_VLAN_CLASSID))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL1;
            }
            else
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_VLAN_ACL_EN_FIRST;
            }
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_2:
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2, &get_value));

            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GG_MAX_ACL_VLAN_CLASSID))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL2;
            }
            else
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_VLAN_ACL_EN_FIRST;
            }
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_3:
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3, &get_value));

            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GG_MAX_ACL_VLAN_CLASSID))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL3;
            }
            else
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_VLAN_ACL_EN_FIRST;
            }
            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            value = value ? 1 : 0;
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            if (value > (CTC_ACL_TCAM_LKUP_TYPE_MAX -1))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_1:
            if (value > (CTC_ACL_TCAM_LKUP_TYPE_MAX -1))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE1;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_2:
            if (value > (CTC_ACL_TCAM_LKUP_TYPE_MAX -1))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE2;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_3:
            if (value > (CTC_ACL_TCAM_LKUP_TYPE_MAX -1))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE3;
            break;

        default:
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, value));
        }
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (vlan_prop)
        {

        case CTC_VLAN_DIR_PROP_ACL_EN:
            if (value > CTC_ACL_EN_0)
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }

            CTC_ERROR_RETURN_VLAN_UNLOCK(sys_goldengate_global_check_acl_memory(lchip, CTC_EGRESS, value));
            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            cmd = 0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:

            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0, &get_value));

            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GG_MAX_ACL_VLAN_CLASSID))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL;
            }
            else
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_VLAN_ACL_EN_FIRST;
            }

            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            value = value ? 1 : 0;
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            if (value > (CTC_ACL_TCAM_LKUP_TYPE_MAX -1))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0;
            break;


        default:
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_set_vlan_profile(lchip, vlan_id, prop, value));
        }
    }

    value = p_gg_vlan_master[lchip]->p_vlan_profile_id[vlan_id];
    SYS_VLAN_UNLOCK(lchip);
    cmd = DRV_IOW(DsVlan_t, DsVlan_profileId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_get_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32* value)
{

    uint32  prop = 0;
    uint32  acl_en = 0;
    uint32  cmd = 1;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);
    CTC_PTR_VALID_CHECK(value);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d dir:%d\n", vlan_id, vlan_prop, dir);

    /* zero value*/
    *value = 0;

    SYS_VLAN_LOCK(lchip);
    if (CTC_INGRESS == dir)
    {
        switch (vlan_prop)
        {
        case CTC_VLAN_DIR_PROP_ACL_EN:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_0);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_1);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_2);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_1:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL1;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_2:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL2;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID_3:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL3;
            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_1:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE1;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_2:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE2;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_3:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE3;
            break;


        default:
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
    }

    if (CTC_EGRESS == dir)
    {
        switch (vlan_prop)
        {
        case CTC_VLAN_DIR_PROP_ACL_EN:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_0);
            }
            cmd = 0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL;
            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY;
            break;

        case CTC_VLAN_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_TCAM_LOOKUP_TYPE0;
            break;

        default:
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
    }

    if (cmd)
    {
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_goldengate_vlan_get_vlan_profile
                             (lchip, vlan_id, prop, value));
    }
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define ____warmboot_____

int32
_sys_goldengate_vlan_profile_hw_read(uint8 lchip, uint16 profile_id, sys_sw_vlan_profile_t* p_ds_vlan_profile)
{
    uint32 cmd = 0;
    DsVlanProfile_m ds_vlan_profile;
    sal_memset(&ds_vlan_profile, 0, sizeof(DsVlanProfile_m));

    cmd = DRV_IOR(DsVlanProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, &ds_vlan_profile));

    p_ds_vlan_profile->vlan_storm_ctl_en = GetDsVlanProfile(V, vlanStormCtlEn_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_acl_label = GetDsVlanProfile(V, egressVlanAclLabel0_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_en3 = GetDsVlanProfile(V, ingressVlanAclEn3_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_en2 = GetDsVlanProfile(V, ingressVlanAclEn2_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_en1 = GetDsVlanProfile(V, ingressVlanAclEn1_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_en0 = GetDsVlanProfile(V, ingressVlanAclEn0_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_label3 = GetDsVlanProfile(V, ingressVlanAclLabel3_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_label2 = GetDsVlanProfile(V, ingressVlanAclLabel2_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_label1 = GetDsVlanProfile(V, ingressVlanAclLabel1_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_label0 = GetDsVlanProfile(V, ingressVlanAclLabel0_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_lookup_type3 = GetDsVlanProfile(V, ingressVlanAclLookupType3_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_lookup_type2 = GetDsVlanProfile(V, ingressVlanAclLookupType2_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_lookup_type1 = GetDsVlanProfile(V, ingressVlanAclLookupType1_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_lookup_type0 = GetDsVlanProfile(V, ingressVlanAclLookupType0_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_routed_only3 = GetDsVlanProfile(V, ingressVlanAclRoutedOnly3_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_routed_only2 = GetDsVlanProfile(V, ingressVlanAclRoutedOnly2_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_routed_only1 = GetDsVlanProfile(V, ingressVlanAclRoutedOnly1_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_acl_routed_only0 = GetDsVlanProfile(V, ingressVlanAclRoutedOnly0_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_acl_routed_only0 = GetDsVlanProfile(V, egressVlanAclRoutedOnly0_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_span_en = GetDsVlanProfile(V, ingressVlanSpanEn_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_span_en = GetDsVlanProfile(V, egressVlanSpanEn_f, &ds_vlan_profile);
    p_ds_vlan_profile->force_ipv6_lookup = GetDsVlanProfile(V, forceIpv6Lookup_f, &ds_vlan_profile);
    p_ds_vlan_profile->force_ipv4_lookup = GetDsVlanProfile(V, forceIpv4Lookup_f, &ds_vlan_profile);
    p_ds_vlan_profile->ingress_vlan_span_id = GetDsVlanProfile(V, ingressVlanSpanId_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_span_id = GetDsVlanProfile(V, egressVlanSpanId_f, &ds_vlan_profile);
    p_ds_vlan_profile->fip_exception_type = GetDsVlanProfile(V, fipExceptionType_f, &ds_vlan_profile);
    p_ds_vlan_profile->vlan_storm_ctl_ptr = GetDsVlanProfile(V, vlanStormCtlPtr_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_acl_en0 = GetDsVlanProfile(V, egressVlanAclEn0_f, &ds_vlan_profile);
    p_ds_vlan_profile->egress_vlan_acl_lookup_type0 = GetDsVlanProfile(V, egressVlanAclLookupType0_f, &ds_vlan_profile);

    return CTC_E_NONE;
}

int32
_sys_goldengate_vlan_overlay_wb_mapping(sys_wb_vlan_overlay_mapping_key_t* p_wb_vlan_overlay, sys_vlan_overlay_mapping_key_t* p_vlan_overlay, uint8 sync)
{
    if (sync)
    {
        p_wb_vlan_overlay->vn_id = p_vlan_overlay->vn_id;
        p_wb_vlan_overlay->fid = p_vlan_overlay->fid;
    }
    else
    {
        p_vlan_overlay->vn_id = p_wb_vlan_overlay->vn_id;
        p_vlan_overlay->fid = p_wb_vlan_overlay->fid;
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_vlan_wb_sync_func(void* bucket_data, void* user_data)
{
    uint16 max_entry_cnt = 0;
    ctc_wb_data_t* p_wb_data = (ctc_wb_data_t*) user_data;
    sys_vlan_overlay_mapping_key_t* p_vlan_overlay = (sys_vlan_overlay_mapping_key_t*) bucket_data;
    sys_wb_vlan_overlay_mapping_key_t* p_wb_vlan_overlay = NULL;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / sizeof(sys_wb_vlan_overlay_mapping_key_t);
    p_wb_vlan_overlay = (sys_wb_vlan_overlay_mapping_key_t*) p_wb_data->buffer + p_wb_data->valid_cnt;
    CTC_ERROR_RETURN(_sys_goldengate_vlan_overlay_wb_mapping(p_wb_vlan_overlay, p_vlan_overlay, 1));
    if (++p_wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(p_wb_data));
        p_wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_vlan_wb_sync(uint8 lchip)
{
    uint8 loop = 0;
    int32 ret = CTC_E_NONE;
    sys_wb_vlan_master_t* p_wb_vlan_master = NULL;
    ctc_wb_data_t wb_data;

    /*sync up master*/
    wb_data.buffer = mem_malloc(MEM_VLAN_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);
    p_wb_vlan_master = (sys_wb_vlan_master_t*)wb_data.buffer;
    p_wb_vlan_master->lchip = lchip;
    for (loop = 0; loop < 128; loop++)
    {
        p_wb_vlan_master->vlan_bitmap[loop] = p_gg_vlan_master[lchip]->vlan_bitmap[loop];
        p_wb_vlan_master->vlan_def_bitmap[loop] = p_gg_vlan_master[lchip]->vlan_def_bitmap[loop];
    }
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_vlan_master_t, CTC_FEATURE_VLAN, SYS_WB_APPID_VLAN_SUBID_MASTER);
    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*sync overlay*/
    /*for different kinds of data stored, the wb_data must be reinited*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_vlan_overlay_mapping_key_t, CTC_FEATURE_VLAN, SYS_WB_APPID_VLAN_SUBID_OVERLAY_MAPPING_KEY);
    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_vlan_master[lchip]->vni_mapping_hash, _sys_goldengate_vlan_wb_sync_func, (void*)(&wb_data)), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }
    return ret;
}

int32
sys_goldengate_vlan_wb_restore(uint8 lchip)
{
    uint8 loop = 0;
    uint16 index = 0;
    uint32 profile_id = 0;
    uint16 entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    ctc_wb_query_t wb_query;
    sys_wb_vlan_master_t* p_wb_vlan_master = NULL;
    sys_wb_vlan_overlay_mapping_key_t* p_wb_vlan_overlay = NULL;
    sys_vlan_overlay_mapping_key_t* p_vlan_overlay = NULL;
    sys_vlan_profile_t* p_vlan_profile = NULL;
    sys_vlan_profile_t* p_vlan_profile_get = NULL;
    sys_goldengate_opf_t opf;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_VLAN_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore master*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_vlan_master_t, CTC_FEATURE_VLAN, SYS_WB_APPID_VLAN_SUBID_MASTER);
    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query vlan master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_vlan_master = (sys_wb_vlan_master_t*) wb_query.buffer;
    for (loop = 0; loop < 128; loop++)
    {
        p_gg_vlan_master[lchip]->vlan_bitmap[loop] = p_wb_vlan_master->vlan_bitmap[loop];
        p_gg_vlan_master[lchip]->vlan_def_bitmap[loop] = p_wb_vlan_master->vlan_def_bitmap[loop];
    }
    /*restore overlay*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_vlan_overlay_mapping_key_t, CTC_FEATURE_VLAN, SYS_WB_APPID_VLAN_SUBID_OVERLAY_MAPPING_KEY);
    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_vlan_overlay = mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_overlay_mapping_key_t));
        if (NULL == p_vlan_overlay)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_vlan_overlay, 0, sizeof(sys_vlan_overlay_mapping_key_t));
        p_wb_vlan_overlay = (sys_wb_vlan_overlay_mapping_key_t*) wb_query.buffer + entry_cnt++;
        CTC_ERROR_GOTO(_sys_goldengate_vlan_overlay_wb_mapping(p_wb_vlan_overlay, p_vlan_overlay, 0), ret, done);
        ctc_hash_insert(p_gg_vlan_master[lchip]->vni_mapping_hash, (void*)(p_vlan_overlay));
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore spool*/
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_VLAN_PROFILE;
    opf.pool_index = 0;
    for (index = 0; index <= CTC_MAX_VLAN_ID; index++)
    {
        profile_id = 0;
        if (CTC_IS_BIT_SET(p_gg_vlan_master[lchip]->vlan_bitmap[(index >> 5)], (index & 0x1F)))
        {
            cmd = DRV_IOR(DsVlan_t, DsVlan_profileId_f);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, index, cmd, &profile_id), ret, done);
        }
        if (0 != profile_id)
        {
            p_vlan_profile = mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_profile_t));
            if (NULL == p_vlan_profile)
            {
                ret = CTC_E_NO_MEMORY;
                goto done;
            }
            sal_memset(p_vlan_profile, 0, sizeof(sys_vlan_profile_t));
            p_vlan_profile->lchip = lchip;
            p_vlan_profile->profile_id = profile_id;
            _sys_goldengate_vlan_profile_hw_read(lchip, profile_id, &(p_vlan_profile->sw_vlan_profile));
            ret = ctc_spool_add(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile, NULL, &p_vlan_profile_get);
            CTC_PTR_VALID_CHECK(p_vlan_profile_get);

            if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
            {
                mem_free(p_vlan_profile);
            }

            if (ret < 0)
            {
                ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                goto done;
            }
            else
            {
                if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
                {
                    ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_vlan_profile_get->profile_id);
                    if (ret)
                    {
                        ctc_spool_remove(p_gg_vlan_master[lchip]->vprofile_spool, p_vlan_profile_get, NULL);
                        mem_free(p_vlan_profile);
                        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc vlan profile id from position %u error! ret: %d.\n",  p_vlan_profile_get->profile_id, ret);
                        goto done;
                    }
                }
                else
                {

                }
                p_gg_vlan_master[lchip]->p_vlan_profile_id[index] = profile_id;
                p_gg_vlan_master[lchip]->p_vlan_profile[profile_id] = p_vlan_profile_get;
            }
        }
    }

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


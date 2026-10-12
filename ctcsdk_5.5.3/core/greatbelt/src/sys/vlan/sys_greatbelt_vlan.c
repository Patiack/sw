/**
@file sys_greatbelt_vlan.c

@date 20011 - 11 - 11

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

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_linkagg.h"
#include "sys_greatbelt_stats.h"

#include "greatbelt/include/drv_io.h"
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
#define SYS_VLAN_TEMPLATE_NUM     32
#define SYS_VLAN_RESERVE_NUM     4
#define SYS_VLAN_BITMAP_INDEX        (ENTRY_NUM_VLAN/32)

struct sys_vlan_master_s
{
    ctc_spool_t*      vprofile_spool;
    uint16*           p_vlan;
    uint8             sys_vlan_def_mode;
    uint32            vlan_bitmap[SYS_VLAN_BITMAP_INDEX];
    ctc_vector_t*     port_vec;
    uint32            vlan_def_bitmap[SYS_VLAN_BITMAP_INDEX];
    uint32            unknown_mcast_tocpu_nhid;
    sal_mutex_t*      mutex;
};
typedef struct sys_vlan_master_s sys_vlan_master_t;

struct sys_vlan_profile_s
{
    ds_vlan_profile_t vlan_profile;
    uint16            profile_id;
    uint16            rsv0;
};
typedef struct sys_vlan_profile_s sys_vlan_profile_t;

struct sys_vlan_oper_prop_s
{
    ds_vlan_profile_t* p_ds_vlan_profile;
    uint16 profile_id;
};
typedef struct sys_vlan_oper_prop_s sys_vlan_oper_prop_t;

#define SYS_VLAN_INIT_DS_VLAN() \
    { \
        ds_vlan.transmit_disable = 1; \
        ds_vlan.receive_disable = 1; \
        ds_vlan.arp_exception_type = CTC_EXCP_NORMAL_FWD; \
        ds_vlan.dhcp_exception_type = CTC_EXCP_NORMAL_FWD; \
        ds_vlan.tag_port_bit_map31_0  = 0xFFFFFFFF; \
        ds_vlan.tag_port_bit_map59_32 = 0xFFFFFFF; \
    }

#define SYS_VLAN_FILTER_PORT_CHECK(lchip, lport) \
    do \
    { \
        if(lport >= SYS_GB_MAX_PHY_PORT)\
        { \
            return CTC_E_VLAN_FILTER_PORT; \
        } \
    } \
    while (0)

#define SYS_VLAN_CREATE_CHECK(vlan_ptr) \
    do \
    { \
        if (!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F))){ \
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
        if (NULL == p_gb_vlan_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_VLAN_CREATE_LOCK(lchip)                         \
    do                                                      \
    {                                                       \
        sal_mutex_create(&p_gb_vlan_master[lchip]->mutex);  \
        if (NULL == p_gb_vlan_master[lchip]->mutex)         \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_VLAN_LOCK(lchip) \
    sal_mutex_lock(p_gb_vlan_master[lchip]->mutex)

#define SYS_VLAN_UNLOCK(lchip) \
    sal_mutex_unlock(p_gb_vlan_master[lchip]->mutex)

#define CTC_ERROR_RETURN_VLAN_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gb_vlan_master[lchip]->mutex); \
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

#define REF_COUNT(data)   ctc_spool_get_refcnt(p_gb_vlan_master[lchip]->vprofile_spool, data)
#define VLAN_PROFILE_SIZE_PER_BUCKET 16

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

/*
*
*    byte_offset = vlan_id >> 3 ;
*    bit_offset = vlan_id & 0x7;
*    p_gb_vlan_master[lchip]->vlan_bitmap[byte_offset] |= 1 >> bit_offset;
*/

static sys_vlan_master_t* p_gb_vlan_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
 void*
 _sys_greatbelt_vlan_port_vec_lkup(uint8 lchip, uint8 lport)
{
    uint32* p_vlan_bmp = NULL;
    p_vlan_bmp = ctc_vector_get(p_gb_vlan_master[lchip]->port_vec, lport);
    return p_vlan_bmp;

}

STATIC int32
_sys_greatbelt_vlan_port_vec_add(uint8 lchip, uint16 vlan_ptr, uint8 lport)
{
    uint32* p_vlan_bmp = NULL;

    CTC_VLAN_RANGE_CHECK(vlan_ptr);

    p_vlan_bmp = _sys_greatbelt_vlan_port_vec_lkup(lchip, lport);
    if (NULL == p_vlan_bmp)
    {
        MALLOC_ZERO(MEM_VLAN_MODULE, p_vlan_bmp, SYS_VLAN_BITMAP_INDEX * sizeof(uint32));

    }
    else if (CTC_IS_BIT_SET(p_vlan_bmp[vlan_ptr/32], vlan_ptr%32))
    {
        return CTC_E_NONE;
    }
    CTC_BIT_SET(p_vlan_bmp[vlan_ptr/32], vlan_ptr%32);
    ctc_vector_add(p_gb_vlan_master[lchip]->port_vec, lport, p_vlan_bmp);


    return CTC_E_NONE;

}
STATIC int32
_sys_greatbelt_vlan_port_vec_remove(uint8 lchip, uint16 vlan_ptr, uint8 lport)
{
    uint32* p_vlan_bmp = NULL;
    uint32 value[SYS_VLAN_BITMAP_INDEX] = {0};

    p_vlan_bmp = _sys_greatbelt_vlan_port_vec_lkup(lchip, lport);
    if (NULL == p_vlan_bmp)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_BIT_UNSET(p_vlan_bmp[vlan_ptr/32], vlan_ptr%32);

    if (!sal_memcmp((uint8*)value, (uint8*)p_vlan_bmp, SYS_VLAN_BITMAP_INDEX*4))
    {
        ctc_vector_del(p_gb_vlan_master[lchip]->port_vec, lport);
        mem_free(p_vlan_bmp);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_profile_remove(uint8 lchip, sys_vlan_profile_t*  p_vlan_profile_in);

/**
 *
 *  hw table
 */

STATIC int32
_sys_greatbelt_vlan_set_default_vlan(uint8 lchip, ds_vlan_t* p_ds)
{
    CTC_PTR_VALID_CHECK(p_ds);

    sal_memset(p_ds, 0, sizeof(ds_vlan_t));

    p_ds->transmit_disable = 1;
    p_ds->receive_disable = 1;
    p_ds->arp_exception_type = CTC_EXCP_NORMAL_FWD;
    p_ds->dhcp_exception_type = CTC_EXCP_NORMAL_FWD;
    p_ds->tag_port_bit_map31_0  = 0xFFFFFFFF;
    p_ds->tag_port_bit_map59_32 = 0xFFFFFFF;

    return CTC_E_NONE;
}

STATIC int
_sys_greatbelt_vlan_profile_write(uint8 lchip, uint16 profile_id, ds_vlan_profile_t* p_vlan_profile)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(DsVlanProfile_t, DRV_ENTRY_FLAG);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, profile_id, cmd, p_vlan_profile));

    return CTC_E_NONE;

}

/**
  @brief judge whether a port is member of vlan
  */
bool
_sys_greatbelt_vlan_is_member_port(uint8 lchip, uint16 vlan_ptr, uint8 lport)
{
    ds_vlan_t ds_vlan;
    uint32 cmd = 0;
    bool vlan_id_valid = FALSE;

    SYS_VLAN_FILTER_PORT_CHECK(lchip, lport);

    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    if (lport >> 5)
    {
        vlan_id_valid = IS_BIT_SET(ds_vlan.vlan_id_valid59_32, lport & 0x1F);
    }
    else
    {
        vlan_id_valid = IS_BIT_SET(ds_vlan.vlan_id_valid31_0, lport & 0x1F);
    }

    return vlan_id_valid;

}

STATIC uint32
_sys_greatbelt_vlan_hash_make(sys_vlan_profile_t* p_vlan_profile)
{
    uint32 a, b, c;
    uint32* k = (uint32*)&p_vlan_profile->vlan_profile;
    uint8*  k8;
    uint8   length = sizeof(ds_vlan_profile_t);

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
_sys_greatbelt_vlan_hash_compare(sys_vlan_profile_t* p_bucket, sys_vlan_profile_t* p_new)
{
    if (!p_bucket || !p_new)
    {
        return FALSE;
    }

    if (!sal_memcmp(&p_bucket->vlan_profile, &p_new->vlan_profile, sizeof(ds_vlan_profile_t))) /*xscl is same size*/
    {
        return TRUE;
    }

    return FALSE;
}

/* add to software table is ok, no need to write hardware table */
STATIC int32
_sys_greatbelt_vlan_profile_static_add(uint8 lchip, ds_vlan_profile_t* vt, uint16* profile_id)
{
    sys_vlan_profile_t*  p_vp = NULL;

    CTC_PTR_VALID_CHECK(vt);
    CTC_PTR_VALID_CHECK(profile_id);

    p_vp = mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_profile_t));
    if (!p_vp)
    {
        return CTC_E_NO_MEMORY;
    }

    p_vp->profile_id = (*profile_id);
    sal_memcpy(&(p_vp->vlan_profile), vt, sizeof(ds_vlan_profile_t));

    if (ctc_spool_static_add(p_gb_vlan_master[lchip]->vprofile_spool, p_vp) < 0)
    {
        return CTC_E_NO_MEMORY;
    }

    (*profile_id)++;
    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_vlan_profile_index_free(uint8 lchip, uint16 index)
{
    sys_greatbelt_opf_t  opf;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_VLAN_PROFILE;

    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, (uint32)index));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_profile_hw_read(uint8 lchip, uint16 profile_id, ds_vlan_profile_t* p_ds_vlan_profile)
{
    uint32 cmd = 0;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOR(DsVlanProfile_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, (uint32)profile_id, cmd, p_ds_vlan_profile));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_profile_remove(uint8 lchip, sys_vlan_profile_t*  p_vlan_profile_in)
{
    uint16            index_free = 0;
    int32             ret = 0;

    sys_vlan_profile_t* p_vlan_profile_del;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_vlan_profile_in);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__);

    p_vlan_profile_del = ctc_spool_lookup(p_gb_vlan_master[lchip]->vprofile_spool, p_vlan_profile_in);
    if (!p_vlan_profile_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_gb_vlan_master[lchip]->vprofile_spool, p_vlan_profile_in, NULL);
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
            CTC_ERROR_RETURN(_sys_greatbelt_vlan_profile_index_free(lchip, index_free));
            SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: db_ad removed. removed dsuserid index = %d\n",  index_free);
            mem_free(p_vlan_profile_del);
        }
        else if (ret == CTC_SPOOL_E_OPERATE_REFCNT)
        {
        }
    }

    return CTC_E_NONE;

}

/* init the vlan module */
int32
sys_greatbelt_vlan_init(uint8 lchip, ctc_vlan_global_cfg_t* vlan_global_cfg)
{

    uint16 profile_id = 0;
    uint8  i, j, k;

    uint32 index = 0;
    uint32 cmd = 0;

    uint32 entry_num_vlan = 0;
    uint32 entry_num_vlan_profile = 0;

    ds_vlan_t ds_vlan;
    ds_vlan_profile_t ds_vlan_profile;

    sys_greatbelt_opf_t opf;
    ctc_spool_t spool;

    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsVlan_t, &entry_num_vlan));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsVlanProfile_t, &entry_num_vlan_profile));

    if (NULL != p_gb_vlan_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(vlan_global_cfg);

    /*init p_gb_vlan_master*/
    SYS_VLAN_NO_MEMORY_RETURN(p_gb_vlan_master[lchip] = \
                                  (sys_vlan_master_t*)mem_malloc(MEM_VLAN_MODULE, sizeof(sys_vlan_master_t)));


    /*init vlan */
    sal_memset(p_gb_vlan_master[lchip], 0, sizeof(sys_vlan_master_t));
    sal_memset(p_gb_vlan_master[lchip]->vlan_bitmap, 0, sizeof(uint32) * 128);
    sal_memset(p_gb_vlan_master[lchip]->vlan_def_bitmap, 0, sizeof(uint32) * 128);

    SYS_VLAN_NO_MEMORY_RETURN(p_gb_vlan_master[lchip]->p_vlan = \
                                  mem_malloc(MEM_VLAN_MODULE, sizeof(uint16) * entry_num_vlan));

    sal_memset(p_gb_vlan_master[lchip]->p_vlan, 0, sizeof(uint16) * entry_num_vlan);

    /*init vlan profile*/
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = entry_num_vlan_profile / VLAN_PROFILE_SIZE_PER_BUCKET;
    spool.max_count = entry_num_vlan_profile - 1; /*for profile = 0 is invalid*/
    spool.user_data_size = sizeof(sys_vlan_profile_t);
    spool.spool_key = (hash_key_fn)_sys_greatbelt_vlan_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_vlan_hash_compare;
    p_gb_vlan_master[lchip]->vprofile_spool = ctc_spool_create(&spool);

    if (0 != entry_num_vlan_profile)
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_VLAN_PROFILE, 1));

        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type  = OPF_VLAN_PROFILE;
        /*begin with 1, 0 is invalid*/
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset
                             (lchip, &opf, 1 + SYS_VLAN_TEMPLATE_NUM, entry_num_vlan_profile - (1 + SYS_VLAN_TEMPLATE_NUM)));

        CTC_ERROR_RETURN(sys_greatbelt_opf_init_reverse_size(lchip, &opf, SYS_VLAN_RESERVE_NUM));
    }

    /*init asic table: dsvlan */
    _sys_greatbelt_vlan_set_default_vlan(lchip, &ds_vlan);

    p_gb_vlan_master[lchip]->sys_vlan_def_mode = 1;

    p_gb_vlan_master[lchip]->port_vec = ctc_vector_init(SYS_VLAN_BLOCK_NUM, 64/SYS_VLAN_BLOCK_NUM);
    if (!p_gb_vlan_master[lchip]->port_vec)
    {
        return  CTC_E_NO_MEMORY;
    }

    for (index = 0; index < entry_num_vlan; index++)
    {
        cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_vlan));
    }

    /*for cross connect*/
    ds_vlan.transmit_disable = 0;
    ds_vlan.receive_disable  = 0;
    ds_vlan.vlan_id_valid31_0  = 0xFFFFFFFF;
    ds_vlan.vlan_id_valid59_32 = 0xFFFFFFF;
    ds_vlan.tag_port_bit_map31_0  = 0xFFFFFFFF;
    ds_vlan.tag_port_bit_map59_32 = 0xFFFFFFF;

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds_vlan));

    /*vlan profile*/
    sal_memset(&ds_vlan_profile, 0, sizeof(ds_vlan_profile_t));

    for (index = 0; index < entry_num_vlan_profile; index++)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_profile_write(lchip, index, &ds_vlan_profile));
    }

    /* init vlan profile template */
    profile_id = 1;

    ds_vlan_profile.fip_exception_type = 1;

    for (i = 0; i < 4; i++)
    {

        switch (i)
        {
        case 0:
            ds_vlan_profile.ingress_vlan_acl_en0 = 0;
            ds_vlan_profile.ingress_vlan_acl_en1 = 0;
            ds_vlan_profile.ingress_vlan_acl_en2 = 0;
            ds_vlan_profile.ingress_vlan_acl_en3 = 0;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en0 = 0;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en1 = 0;
            break;

        case 1:
            ds_vlan_profile.ingress_vlan_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_acl_en1 = 0;
            ds_vlan_profile.ingress_vlan_acl_en2 = 0;
            ds_vlan_profile.ingress_vlan_acl_en3 = 0;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en1 = 0;
            break;

        case 2:
            ds_vlan_profile.ingress_vlan_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_acl_en1 = 1;
            ds_vlan_profile.ingress_vlan_acl_en2 = 0;
            ds_vlan_profile.ingress_vlan_acl_en3 = 0;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en1 = 1;
            break;

        case 3:
            ds_vlan_profile.ingress_vlan_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_acl_en1 = 1;
            ds_vlan_profile.ingress_vlan_acl_en2 = 1;
            ds_vlan_profile.ingress_vlan_acl_en3 = 1;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en0 = 1;
            ds_vlan_profile.ingress_vlan_i_pv6_acl_en1 = 1;
            break;

        }

        for (j = 0; j < 4; j++)
        {
            switch (j)
            {
            case 0:
                ds_vlan_profile.egress_vlan_acl_en0 = 0;
                ds_vlan_profile.egress_vlan_acl_en1 = 0;
                ds_vlan_profile.egress_vlan_acl_en2 = 0;
                ds_vlan_profile.egress_vlan_acl_en3 = 0;
                ds_vlan_profile.egress_vlan_ipv6_acl_en0 = 0;
                ds_vlan_profile.egress_vlan_ipv6_acl_en1 = 0;
                break;

            case 1:
                ds_vlan_profile.egress_vlan_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_acl_en1 = 0;
                ds_vlan_profile.egress_vlan_acl_en2 = 0;
                ds_vlan_profile.egress_vlan_acl_en3 = 0;
                ds_vlan_profile.egress_vlan_ipv6_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_ipv6_acl_en1 = 0;
                break;

            case 2:
                ds_vlan_profile.egress_vlan_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_acl_en1 = 1;
                ds_vlan_profile.egress_vlan_acl_en2 = 0;
                ds_vlan_profile.egress_vlan_acl_en3 = 0;
                ds_vlan_profile.egress_vlan_ipv6_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_ipv6_acl_en1 = 1;
                break;

            case 3:
                ds_vlan_profile.egress_vlan_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_acl_en1 = 1;
                ds_vlan_profile.egress_vlan_acl_en2 = 1;
                ds_vlan_profile.egress_vlan_acl_en3 = 1;
                ds_vlan_profile.egress_vlan_ipv6_acl_en0 = 1;
                ds_vlan_profile.egress_vlan_ipv6_acl_en1 = 1;
                break;
            }

            for (k = 0; k < 2; k++)
            {
                switch (k)
                {
                case 0:
                    ds_vlan_profile.force_ipv4_lookup = 0;
                    break;

                case 1:
                    ds_vlan_profile.force_ipv4_lookup = 1;
                    break;
                }

                CTC_ERROR_RETURN(_sys_greatbelt_vlan_profile_static_add(lchip, &ds_vlan_profile, &profile_id));
            }
        }
    }
    SYS_VLAN_CREATE_LOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_deinit(uint8 lchip)
{
    uint32 entry_num_vlan_profile = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gb_vlan_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_ftm_get_table_entry_num(lchip, DsVlanProfile_t, &entry_num_vlan_profile);
    if (0 != entry_num_vlan_profile)
    {
        sys_greatbelt_opf_deinit(lchip, OPF_VLAN_PROFILE);
    }

    /*free vlan profile spool*/
    ctc_spool_free(p_gb_vlan_master[lchip]->vprofile_spool);

    ctc_vector_traverse(p_gb_vlan_master[lchip]->port_vec, (spool_traversal_fn)_sys_greatbelt_vlan_free_node_data, NULL);
    ctc_vector_release(p_gb_vlan_master[lchip]->port_vec);

    mem_free(p_gb_vlan_master[lchip]->p_vlan);

    sal_mutex_destroy(p_gb_vlan_master[lchip]->mutex);
    mem_free(p_gb_vlan_master[lchip]);

    return CTC_E_NONE;
}

/**
  @brief The function is to create a vlan

*/
int32
sys_greatbelt_vlan_create_vlan(uint8 lchip, uint16 vlan_id)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    ds_vlan_t ds_vlan;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create vlan_id: %d!\n", vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);

    SYS_VLAN_LOCK(lchip);
    if (IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    CTC_BIT_SET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));
    SYS_VLAN_UNLOCK(lchip);

    _sys_greatbelt_vlan_set_default_vlan(lchip, &ds_vlan);

    ds_vlan.transmit_disable = 0;
    ds_vlan.receive_disable = 0;
    ds_vlan.vsi_id = vlan_id;

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    return CTC_E_NONE;

}

/**
  @brief The function is to create a user vlan

*/
int32
sys_greatbelt_vlan_create_uservlan(uint8 lchip, ctc_vlan_uservlan_t* user_vlan)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    uint16 vlan_id = 0;
    uint16 fid = 0;
    ds_vlan_t ds_vlan;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(user_vlan);


    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Create vlan_id: %d!\n", vlan_id);

    vlan_id = user_vlan->vlan_id;
    vlan_ptr = user_vlan->user_vlanptr;
    fid = user_vlan->fid;

    if (vlan_id != vlan_ptr)
    {
        return CTC_E_NOT_SUPPORT;
    }
    CTC_VLAN_RANGE_CHECK(vlan_id);

    if ((vlan_ptr) < CTC_MIN_VLAN_ID || (vlan_ptr) > CTC_MAX_VLAN_ID)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_VLAN_LOCK(lchip);
    if (IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }

    CTC_BIT_SET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));
    SYS_VLAN_UNLOCK(lchip);

    _sys_greatbelt_vlan_set_default_vlan(lchip, &ds_vlan);

    ds_vlan.transmit_disable = 0;
    ds_vlan.receive_disable = 0;
    ds_vlan.vsi_id = fid;

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        ctc_l2dflt_addr_t def;
        sal_memset(&def, 0, sizeof(def));

        SYS_VLAN_LOCK(lchip);
        CTC_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));
        SYS_VLAN_UNLOCK(lchip);

        if (CTC_FLAG_ISSET(user_vlan->flag, CTC_MAC_LEARN_USE_LOGIC_PORT))
        {
            CTC_SET_FLAG(def.flag, CTC_L2_DFT_VLAN_FLAG_USE_LOGIC_PORT);
        }
        def.fid = fid;
        def.l2mc_grp_id = fid;
        CTC_ERROR_RETURN(sys_greatbelt_l2_add_default_entry(lchip, &def));
    }

    return CTC_E_NONE;

}

/**
@brief The function is to remove the vlan

*/
int32
sys_greatbelt_vlan_remove_vlan(uint8 lchip, uint16 vlan_id)
{
    uint16 vlan_ptr = 0;
    uint16 old_profile_id = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint8 idx = 0;
    uint32 vlanId_valid31_0 = 0;
    uint32 vlanId_valid59_32 = 0;
    ds_vlan_t ds_vlan;
    sys_vlan_profile_t tmp_vlan_profile_db;

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove vlan_id: %d!\n", vlan_id);

    _sys_greatbelt_vlan_set_default_vlan(lchip, &ds_vlan);

    cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));
    cmd = DRV_IOR(DsVlan_t, DsVlan_VlanIdValid31_0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &vlanId_valid31_0));
    cmd = DRV_IOR(DsVlan_t, DsVlan_VlanIdValid59_32_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &vlanId_valid59_32));
    /*reset hw: ds_vlan*/

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    SYS_VLAN_LOCK(lchip);
    CTC_BIT_UNSET(p_gb_vlan_master[lchip]->vlan_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));


    if (NULL == p_gb_vlan_master[lchip])
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_INVALID_PTR;
    }
    old_profile_id = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
    if (0 != old_profile_id)
    {
        sal_memset(&tmp_vlan_profile_db, 0, sizeof(sys_vlan_profile_t));
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_profile_hw_read(lchip, old_profile_id,  &tmp_vlan_profile_db.vlan_profile));
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_profile_remove(lchip, &tmp_vlan_profile_db));
        p_gb_vlan_master[lchip]->p_vlan[vlan_id] = 0;
    }

    if(!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        int32 ret = 0;
        ctc_l2dflt_addr_t def;
        sal_memset(&def, 0, sizeof(def));
        def.fid = (uint16)value;
        ret = sys_greatbelt_l2_remove_default_entry(lchip, &def);
        if (CTC_E_ENTRY_NOT_EXIST == ret)
        {
            ret = CTC_E_NONE;
        }
        CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

        for (idx = 0; idx < 32; idx++)
        {
            if (CTC_IS_BIT_SET(vlanId_valid31_0, idx))
            {
                _sys_greatbelt_vlan_port_vec_remove(lchip, vlan_ptr, idx);
            }
        }
        for (idx = 0; idx < 28; idx++)
        {
            if (CTC_IS_BIT_SET(vlanId_valid59_32, idx))
            {
                _sys_greatbelt_vlan_port_vec_remove(lchip, vlan_ptr, idx + 32);
            }
        }

    }

    CTC_BIT_UNSET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F));
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_get_vlan_ptr(uint8 lchip, uint16 vlan_id, uint16* vlan_ptr)
{
    *vlan_ptr = vlan_id;
    return CTC_E_NONE;
}
int32
sys_greatbelt_vlan_add_port_to_dft_entry(uint8 lchip, uint16 vlan_ptr, uint16 gport)
{
    int32 ret = 0;
    uint16 agg_gport = 0;
    ctc_l2dflt_addr_t l2dflt_addr;

    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    l2dflt_addr.fid = vlan_ptr;

    if ((!CTC_IS_LINKAGG_PORT(gport)) && (TRUE == sys_greatbelt_linkagg_port_is_lag_member(lchip, gport, &agg_gport)))
    {
        l2dflt_addr.member.mem_port = agg_gport;
    }
    else
    {
        l2dflt_addr.member.mem_port = gport;
    }

    ret = sys_greatbelt_l2_add_port_to_default_entry(lchip, &l2dflt_addr);
    if (ret < 0)
    {
            return CTC_E_NO_RESOURCE;
    }

    SYS_VLAN_LOCK(lchip);
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        _sys_greatbelt_vlan_port_vec_add(lchip, vlan_ptr, CTC_MAP_GPORT_TO_LPORT(gport));
    }
    SYS_VLAN_UNLOCK(lchip);

    return  CTC_E_NONE;
}

/**
@brief the function add port to vlan
*/
int32
sys_greatbelt_vlan_add_port(uint8 lchip, uint16 vlan_id, uint16 gport)
{
    uint8 lport = 0;
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add global port:%d, to vlan_id:%d!\n", gport, vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_VLAN_FILTER_PORT_CHECK(lchip, lport);

    if (TRUE == _sys_greatbelt_vlan_is_member_port(lchip, vlan_ptr, lport))
    {
        return CTC_E_NONE;
    }

    /*write hw */
    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    if (lport >> 5)
    {
        CTC_BIT_SET(ds_vlan.vlan_id_valid59_32, lport & 0x1F);
    }
    else
    {
        CTC_BIT_SET(ds_vlan.vlan_id_valid31_0, lport & 0x1F);
    }

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        uint32 fid = 0;

        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));

        ret = sys_greatbelt_vlan_add_port_to_dft_entry(lchip, fid, gport);
        if (ret < 0)
        {
            cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

            if (lport >> 5)
            {
                CTC_BIT_UNSET(ds_vlan.vlan_id_valid59_32, lport & 0x1F);
            }
            else
            {
                CTC_BIT_UNSET(ds_vlan.vlan_id_valid31_0, lport & 0x1F);
            }

            cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));
            return ret;
        }
    }

    return CTC_E_NONE;
}

/**
@brief the function add ports to vlan
*/
int32
sys_greatbelt_vlan_add_ports(uint8 lchip, uint16 vlan_id, ctc_port_bitmap_t port_bitmap)
{
    uint8 lport = 0;
    uint16 vlan_ptr = 0;
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint32 cmd = 0;
    uint32 ret = 0;
    uint16 gport =0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add global port:%d, to vlan_id:%d!\n", gport, vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);
    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (loop1 == 0 || loop1 == 1)
        {
            continue;
        }
        if (port_bitmap[loop1] != 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    if (port_bitmap[1] & 0xF0000000)
    {
        return CTC_E_INVALID_PARAM;
    }
    /*write hw */
    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));


    ds_vlan.vlan_id_valid31_0 |= port_bitmap[0];
    ds_vlan.vlan_id_valid59_32 |= port_bitmap[1];

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        uint32 fid = 0;
        uint8  gchip = 0;
        uint8  portid = 0;
        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));
        sys_greatbelt_get_gchip_id(lchip, &gchip);
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
                ret = sys_greatbelt_vlan_add_port_to_dft_entry(lchip, fid, gport);
                if (ret < 0)
                {
                    goto roll_back;
                }
            }
        }

    }

    return CTC_E_NONE;

roll_back:
    sys_greatbelt_vlan_remove_ports(lchip, vlan_id, port_bitmap);
    return CTC_E_NO_RESOURCE;

}

int32
sys_greatbelt_vlan_set_tagged_port(uint8 lchip, uint16 vlan_id, uint16 gport, bool tagged)
{
    uint8 lport = 0;
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add global port:%d, to vlan_id:%d!\n", gport, vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_VLAN_FILTER_PORT_CHECK(lchip, lport);

    if (tagged)
    {
        cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

        if (lport >> 5)
        {
            CTC_BIT_SET(ds_vlan.tag_port_bit_map59_32, lport & 0x1F);
        }
        else
        {
            CTC_BIT_SET(ds_vlan.tag_port_bit_map31_0, lport & 0x1F);
        }

        cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));
    }
    else
    {

        cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

        if (lport >> 5)
        {
            CTC_BIT_UNSET(ds_vlan.tag_port_bit_map59_32, lport & 0x1F);
        }
        else
        {
            CTC_BIT_UNSET(ds_vlan.tag_port_bit_map31_0, lport & 0x1F);

        }

        cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_get_tagged_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip)
    CTC_PTR_VALID_CHECK(port_bitmap);

    sal_memset(port_bitmap, 0, sizeof(ctc_port_bitmap_t));

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Get tagged ports of vlan:%d!\n", vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    port_bitmap[0] = ds_vlan.tag_port_bit_map31_0;
    port_bitmap[1] = ds_vlan.tag_port_bit_map59_32;

    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_remove_port_from_dft_entry(uint8 lchip, uint16 vlan_ptr, uint16 gport)
{
    uint16 agg_gport = 0;
    uint32 linkagg_mem_cnt = 0;
    int32 ret = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));
    l2dflt_addr.fid = vlan_ptr;

    if ((!CTC_IS_LINKAGG_PORT(gport))
        && (TRUE == sys_greatbelt_linkagg_port_is_lag_member(lchip, gport, &agg_gport)))
    {
        linkagg_mem_cnt = sys_greatbelt_linkagg_get_mem_num_in_vlan(lchip, vlan_ptr, agg_gport);
        if (linkagg_mem_cnt == 0)
        {
            l2dflt_addr.member.mem_port = agg_gport;
            sys_greatbelt_l2_remove_port_from_default_entry(lchip, &l2dflt_addr);
        }
    }
    else
    {
        l2dflt_addr.member.mem_port = gport;
        sys_greatbelt_l2_remove_port_from_default_entry(lchip, &l2dflt_addr);
    }

    SYS_VLAN_LOCK(lchip);
    /*remove db*/
    if (!CTC_IS_LINKAGG_PORT(gport))
    {
        ret = _sys_greatbelt_vlan_port_vec_remove(lchip, vlan_ptr, CTC_MAP_GPORT_TO_LPORT(gport));
    }
    SYS_VLAN_UNLOCK(lchip);

    return  ret;

}
/**
@brief To remove member port from a vlan
*/
int32
sys_greatbelt_vlan_remove_port(uint8 lchip, uint16 vlan_id, uint16 gport)
{
    uint8 lport = 0;
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;

    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Remove global port:%d from vlan:%d!\n", gport, vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_VLAN_FILTER_PORT_CHECK(lchip, lport);

    if (FALSE == _sys_greatbelt_vlan_is_member_port(lchip, vlan_ptr, lport))
    {
        return CTC_E_MEMBER_PORT_NOT_EXIST;
    }

    /*write hw */
    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    if (lport >> 5)
    {
        CTC_BIT_UNSET(ds_vlan.vlan_id_valid59_32, lport & 0x1F);
    }
    else
    {
        CTC_BIT_UNSET(ds_vlan.vlan_id_valid31_0, lport & 0x1F);
    }

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        uint32 fid = 0;

        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));
        sys_greatbelt_vlan_remove_port_from_dft_entry(lchip, fid, gport);
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_remove_ports(uint8 lchip, uint16 vlan_id, ctc_port_bitmap_t port_bitmap)
{
    uint8 lport = 0;
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint16 gport = 0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Remove global port:%d from vlan:%d!\n", gport, vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t) / 4; loop1++)
    {
        if (loop1 == 0 || loop1 == 1)
        {
            continue;
        }
        if (port_bitmap[loop1] != 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    if (port_bitmap[1] & 0xF0000000)
    {
        return CTC_E_INVALID_PARAM;
    }
    /*write hw */
    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    ds_vlan.vlan_id_valid31_0 &= (~port_bitmap[0]);
    ds_vlan.vlan_id_valid59_32 &= (~port_bitmap[1]);

    cmd = DRV_IOW(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    SYS_VLAN_LOCK(lchip);
    if(!IS_BIT_SET(p_gb_vlan_master[lchip]->vlan_def_bitmap[(vlan_ptr >> 5)], (vlan_ptr & 0x1F)))
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }
    SYS_VLAN_UNLOCK(lchip);

    if (p_gb_vlan_master[lchip]->sys_vlan_def_mode == 1)
    {
        uint8  gchip = 0;
        uint8  portid = 0;
        uint32 fid = 0;

        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));

        sys_greatbelt_get_gchip_id(lchip, &gchip);
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
                sys_greatbelt_vlan_remove_port_from_dft_entry(lchip, fid, gport);
            }
        }
    }

    return CTC_E_NONE;

}


/**
@brief show vlan's member ports
*/
int32
sys_greatbelt_vlan_get_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap)
{
    uint16 vlan_ptr = 0;
    uint32 cmd = 0;
    ds_vlan_t ds_vlan;

    sal_memset(&ds_vlan, 0, sizeof(ds_vlan_t));

    SYS_VLAN_INIT_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip)
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(port_bitmap);

    sal_memset(port_bitmap, 0, sizeof(ctc_port_bitmap_t));

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "Get member ports of vlan:%d!\n", vlan_id);

    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    cmd = DRV_IOR(DsVlan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &ds_vlan));

    port_bitmap[0] = ds_vlan.vlan_id_valid31_0;
    port_bitmap[1] = ds_vlan.vlan_id_valid59_32;


    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_set_mac_auth(uint8 lchip, uint16 vlan_id, bool enable)
{
    uint32   fid = 0;
    uint32   cmd = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_id, cmd, &fid));

    CTC_ERROR_RETURN(sys_greatbelt_l2_default_entry_set_mac_auth(lchip, fid, enable));
    return CTC_E_NONE;
}

int32
sys_greatbelt_vlan_get_mac_auth(uint8 lchip, uint16 vlan_id, bool* enable)
{
    uint32   fid = 0;
    uint32   cmd = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(enable);
    SYS_VLAN_CREATE_CHECK(vlan_id);

    cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_id, cmd, &fid));

    CTC_ERROR_RETURN(sys_greatbelt_l2_default_entry_get_mac_auth(lchip, fid, enable));
    return CTC_E_NONE;
}

#define __VLAN_PROFILE__

STATIC int32
_sys_greatbelt_vlan_profile_dump(void* node, void* user_data)
{
    sys_vlan_profile_t* p_vlan_profile = (sys_vlan_profile_t*)(((ctc_spool_node_t*)node)->data);
    uint8 lchip = *(uint8 *)user_data;

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
sys_greatbelt_vlan_db_dump_all(uint8 lchip)
{
    SYS_VLAN_INIT_CHECK(lchip);
    LCHIP_CHECK(lchip);
    SYS_VLAN_DBG_DUMP("%-12s%s\n", "ProfileId", "RefCount");
    SYS_VLAN_DBG_DUMP("----------------------\n");

    SYS_VLAN_LOCK(lchip);
    ctc_spool_traverse(p_gb_vlan_master[lchip]->vprofile_spool,
                       (spool_traversal_fn)_sys_greatbelt_vlan_profile_dump, &lchip);

    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_profile_index_build(uint8 lchip, uint16* p_index, uint8 reverse)
{
    sys_greatbelt_opf_t  opf;
    uint32 value_32 = 0;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "  %% FUNC: %s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type  = OPF_VLAN_PROFILE;
    opf.reverse    = reverse;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_index = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_vlan_profile_add(uint8 lchip, ds_vlan_profile_t* p_ds_vlan_profile_in,
                                sys_vlan_profile_t* p_old_profile,
                                uint16* profile_id_out)
{

    int32 ret = 0;
    uint16 new_profile_id = 0;
    uint16 old_profile_id = 0;
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

    sal_memcpy(&p_vlan_profile_db_new->vlan_profile, p_ds_vlan_profile_in, sizeof(ds_vlan_profile_t));

    if(p_old_profile)
    {
        if(TRUE == _sys_greatbelt_vlan_hash_compare(p_old_profile, p_vlan_profile_db_new))
        {
            *profile_id_out = p_old_profile->profile_id;
            mem_free(p_vlan_profile_db_new);
            return CTC_E_NONE;
        }
        old_profile_id = p_old_profile->profile_id;
        p_vlan_profile_db_new->profile_id = old_profile_id;
    }

    ret = ctc_spool_add(p_gb_vlan_master[lchip]->vprofile_spool, p_vlan_profile_db_new, p_old_profile, &p_vlan_profile_db_get);

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
            ret = _sys_greatbelt_vlan_profile_index_build(lchip, &new_profile_id, 0);

            if (ret < 0)
            {
                if (p_ds_vlan_profile_in->egress_vlan_span_en || p_ds_vlan_profile_in->ingress_vlan_span_en)
                {
                    ret = _sys_greatbelt_vlan_profile_index_build(lchip, &new_profile_id, 1);
                }
            }

            if (ret < 0)
            {
                ctc_spool_remove(p_gb_vlan_master[lchip]->vprofile_spool, p_vlan_profile_db_new, NULL);
                mem_free(p_vlan_profile_db_new);
                goto ERROR_RETURN;
            }

            p_vlan_profile_db_get->profile_id = new_profile_id;
        }

        *profile_id_out = p_vlan_profile_db_get->profile_id;
    }

    /*key is found, so there is an old ad need to be deleted.*/
    if (p_old_profile && p_vlan_profile_db_get->profile_id != old_profile_id)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_vlan_profile_remove(lchip, p_old_profile));
    }

    return CTC_E_NONE;

ERROR_RETURN:
    return ret;

}

STATIC int32
_sys_greatbelt_vlan_profile_hw_write(uint8 lchip, uint16 profile_id, ds_vlan_profile_t* p_ds_vlan_profile)
{
    uint32 cmd = 0;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOW(DsVlanProfile_t, DRV_ENTRY_FLAG);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, (uint32)profile_id, cmd, p_ds_vlan_profile));

    return CTC_E_NONE;
}

/*set profile_id through vlan_id;*/
STATIC int32
_sys_greatbelt_vlan_set_vlan_profile(uint8 lchip, uint16 vlan_id, sys_vlan_property_t prop, uint32 value)
{
    int32 ret = 0;
    uint16 old_profile_id = 0;
    uint16 new_profile_id = 0;
    sys_vlan_profile_t tmp_vlan_profile_db;
    sys_vlan_profile_t* p_old_vlan_profile = NULL;

    ds_vlan_profile_t  new_profile;
    ds_vlan_profile_t  zero_profile;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&zero_profile, 0, sizeof(ds_vlan_profile_t));

    CTC_PTR_VALID_CHECK(p_gb_vlan_master[lchip]->p_vlan);
    old_profile_id = p_gb_vlan_master[lchip]->p_vlan[vlan_id];

    /*db exists: set 0 or 1 continue.
      db non-ex: set 0 return, set 1 continue.*/

    if (0 == old_profile_id)
    {
        p_old_vlan_profile = NULL;
        sal_memset(&new_profile, 0, sizeof(ds_vlan_profile_t));
        if (0 == value)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        p_old_vlan_profile = &tmp_vlan_profile_db;

        sal_memset(&tmp_vlan_profile_db, 0, sizeof(sys_vlan_profile_t));
        _sys_greatbelt_vlan_profile_hw_read(lchip, old_profile_id,  &tmp_vlan_profile_db.vlan_profile);
        p_old_vlan_profile->profile_id = old_profile_id;
        sal_memcpy(&new_profile, &tmp_vlan_profile_db.vlan_profile, sizeof(ds_vlan_profile_t));
    }

    switch (prop)
    {
    case    SYS_VLAN_PROP_VLAN_STORM_CTL_PTR:
        new_profile.vlan_storm_ctl_ptr           = value;
        break;

    case    SYS_VLAN_PROP_VLAN_STORM_CTL_EN:
        new_profile.vlan_storm_ctl_en            = value;
        break;

    case    SYS_VLAN_PROP_PTP_EN:
        new_profile.ptp_en                       = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID:
        new_profile.ingress_vlan_span_id         = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN:
        new_profile.ingress_vlan_span_en         = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN1:
        new_profile.ingress_vlan_i_pv6_acl_en1   = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN0:
        new_profile.ingress_vlan_i_pv6_acl_en0   = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY:
        new_profile.ingress_vlan_acl_routed_only = value;
        break;

    case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL:
        new_profile.ingress_vlan_acl_label       = value;
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

    case    SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN1:
        new_profile.egress_vlan_ipv6_acl_en1     = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN0:
        new_profile.egress_vlan_ipv6_acl_en0     = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY:
        new_profile.egress_vlan_acl_routed_only  = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL:
        new_profile.egress_vlan_acl_label        = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN3:
        new_profile.egress_vlan_acl_en3          = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN2:
        new_profile.egress_vlan_acl_en2          = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN1:
        new_profile.egress_vlan_acl_en1          = value;
        break;

    case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0:
        new_profile.egress_vlan_acl_en0          = value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    /*setting 0 leads to zero_profile, then delete this profile.*/
    if ((0 == value) && (!sal_memcmp(&new_profile, &zero_profile, sizeof(ds_vlan_profile_t))))
    { /*del prop*/

        ret = _sys_greatbelt_vlan_profile_remove(lchip, &tmp_vlan_profile_db);
        new_profile_id = 0;
        /*no need to del hw*/

    }
    else
    { /*set 1 or 0 to prop*/
        ret = _sys_greatbelt_vlan_profile_add(lchip, &new_profile, p_old_vlan_profile, &new_profile_id);
        ret = ret ? ret : _sys_greatbelt_vlan_profile_hw_write(lchip, new_profile_id, &new_profile);
    }

    p_gb_vlan_master[lchip]->p_vlan[vlan_id] = new_profile_id;
    return ret;

}

/*get profile_id through vlan_id;*/
STATIC int32
_sys_greatbelt_vlan_get_vlan_profile(uint8 lchip, uint16 vlan_id, sys_vlan_property_t prop, uint32* value)
{
    uint16 old_profile_id = 0;
    sys_vlan_profile_t  tmp_ds_vlan_profile;

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_gb_vlan_master[lchip]->p_vlan);
    old_profile_id = p_gb_vlan_master[lchip]->p_vlan[vlan_id];

    sal_memset(&tmp_ds_vlan_profile, 0, sizeof(sys_vlan_profile_t));
    _sys_greatbelt_vlan_profile_hw_read(lchip, old_profile_id,  &tmp_ds_vlan_profile.vlan_profile);

    if (0 == old_profile_id)
    { /*no profile present means all profile prop is zero!*/
        *value = 0;
    }
    else
    {
        switch (prop)
        {
        case    SYS_VLAN_PROP_VLAN_STORM_CTL_PTR:
            *value  = tmp_ds_vlan_profile.vlan_profile.vlan_storm_ctl_ptr;
            break;

        case    SYS_VLAN_PROP_VLAN_STORM_CTL_EN:
            *value  = tmp_ds_vlan_profile.vlan_profile.vlan_storm_ctl_en;
            break;

        case    SYS_VLAN_PROP_PTP_EN:
            *value  = tmp_ds_vlan_profile.vlan_profile.ptp_en;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_span_id;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_span_en;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN1:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_i_pv6_acl_en1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN0:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_i_pv6_acl_en0;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_routed_only;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_label;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_en3;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_en2;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_en1;
            break;

        case    SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0:
            *value  = tmp_ds_vlan_profile.vlan_profile.ingress_vlan_acl_en0;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV6LOOKUP:
            *value  = tmp_ds_vlan_profile.vlan_profile.force_ipv6_lookup;
            break;

        case    SYS_VLAN_PROP_FORCE_IPV4LOOKUP:
            *value  = tmp_ds_vlan_profile.vlan_profile.force_ipv4_lookup;
            break;

        case    SYS_VLAN_PROP_FIP_EXCEPTION_TYPE:
            *value  = tmp_ds_vlan_profile.vlan_profile.fip_exception_type;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_span_id;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_span_en;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN1:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_ipv6_acl_en1;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN0:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_ipv6_acl_en0;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_routed_only;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_label;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN3:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_en3;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN2:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_en2;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN1:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_en1;
            break;

        case    SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0:
            *value  = tmp_ds_vlan_profile.vlan_profile.egress_vlan_acl_en0;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_set_internal_property(uint8 lchip, uint16 vlan_id, sys_vlan_property_t vlan_prop, uint32 value)
{
    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    int32 ret = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d, vlan_prop:%d, value:%d \n", vlan_id, vlan_prop, value);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    if (vlan_prop >= SYS_VLAN_PROP_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    else
    {
        switch (vlan_prop)
        {
        /* vlan proprety for sys layer*/
        case SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD:
            cmd = DRV_IOW(DsVlan_t, DsVlan_MacSecurityVlanDiscard_f);
            value = (value) ? 1 : 0;
            break;

        case SYS_VLAN_PROP_STP_ID:
            cmd = DRV_IOW(DsVlan_t, DsVlan_StpId_f);
            break;

        case SYS_VLAN_PROP_INTERFACE_ID:
            cmd = DRV_IOW(DsVlan_t, DsVlan_InterfaceId_f);
            break;

        case SYS_VLAN_PROP_SRC_QUEUE_SELECT:
            cmd = DRV_IOW(DsVlan_t, DsVlan_SrcQueueSelect_f);
            value = (value) ? 1 : 0;
            break;

        case SYS_VLAN_PROP_EGRESS_FILTER_EN:
            cmd = DRV_IOW(DsVlan_t, DsVlan_EgressFilteringEn_f);
            value = (value) ? 1 : 0;
            break;

        case SYS_VLAN_PROP_INGRESS_FILTER_EN:
            cmd = DRV_IOW(DsVlan_t, DsVlan_IngressFilteringEn_f);
            value = (value) ? 1 : 0;
            break;

        case SYS_VLAN_PROP_UCAST_DISCARD:
            cmd = DRV_IOW(DsVlan_t, DsVlan_UcastDiscard_f);
            value = (value) ? 1 : 0;
            break;

        case SYS_VLAN_PROP_MCAST_DISCARD:
            cmd = DRV_IOW(DsVlan_t, DsVlan_McastDiscard_f);
            value = (value) ? 1 : 0;
            break;

        default:
            SYS_VLAN_LOCK(lchip);
            ret = _sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, vlan_prop, value);
            CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

            value = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
            SYS_VLAN_UNLOCK(lchip);
            cmd = DRV_IOW(DsVlan_t, DsVlan_ProfileId_f);

        }
    }

    if (cmd)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_get_internal_property(uint8 lchip, uint16 vlan_id, sys_vlan_property_t vlan_prop, uint32* value)
{

    uint32 cmd = 0;
    uint16 vlan_ptr = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d \n", vlan_id, vlan_prop);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    /* zero value*/
    *value = 0;

    switch (vlan_prop)
    {
    /* vlan proprety for sys layer*/
    case SYS_VLAN_PROP_STP_ID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_StpId_f);
        break;

    case SYS_VLAN_PROP_INTERFACE_ID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_InterfaceId_f);
        break;

    case SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_MacSecurityVlanDiscard_f);
        break;

    case SYS_VLAN_PROP_SRC_QUEUE_SELECT:
        cmd = DRV_IOR(DsVlan_t, DsVlan_SrcQueueSelect_f);
        break;

    case SYS_VLAN_PROP_EGRESS_FILTER_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_EgressFilteringEn_f);
        break;

    case SYS_VLAN_PROP_INGRESS_FILTER_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_IngressFilteringEn_f);
        break;

    case SYS_VLAN_PROP_UCAST_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_UcastDiscard_f);
        break;

    case SYS_VLAN_PROP_MCAST_DISCARD:
        cmd = DRV_IOR(DsVlan_t, DsVlan_McastDiscard_f);
        break;

    default:
        SYS_VLAN_LOCK(lchip);
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile(lchip, vlan_id, vlan_prop, value));
        SYS_VLAN_UNLOCK(lchip);
        cmd = 0;
        break;

    }

    if (cmd)
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, value));
    }

    return CTC_E_NONE;

}

/**
@brief    Config  vlan properties
*/
int32
sys_greatbelt_vlan_set_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32 value)
{
    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    int32  ret = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    uint32 unknown_mcast_tocpu_nhid = 0;
    uint32 fid = 0;
    uint32 unknown_mcast_tocpu = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d, vlan_prop:%d, value:%d \n", vlan_id, vlan_prop, value);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    switch (vlan_prop)
    {
    /* vlan proprety for ctc layer*/
    case CTC_VLAN_PROP_RECEIVE_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_ReceiveDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_BRIDGE_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_BridgeDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_TRANSMIT_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_TransmitDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_LEARNING_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_LearningDisable_f);
        value = (value) ? 0 : 1;
        break;

    case CTC_VLAN_PROP_DHCP_EXCP_TYPE:
        cmd = DRV_IOW(DsVlan_t, DsVlan_DhcpExceptionType_f);
        if (value > CTC_EXCP_DISCARD)
        {
            return CTC_E_INVALID_PARAM;
        }

        break;

    case CTC_VLAN_PROP_ARP_EXCP_TYPE:
        cmd = DRV_IOW(DsVlan_t, DsVlan_ArpExceptionType_f);
        if (value > CTC_EXCP_DISCARD)
        {
            return CTC_E_INVALID_PARAM;
        }

        break;

    case CTC_VLAN_PROP_IGMP_SNOOP_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_IgmpsnoopEn_f);
        value = (value) ? 1 : 0;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_UCAST_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_UcastDiscard_f);
        value = (value) ? 1 : 0;
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_MCAST_EN:
        cmd = DRV_IOW(DsVlan_t, DsVlan_McastDiscard_f);
        value = (value) ? 1 : 0;
        break;

    case CTC_VLAN_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));
        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));

        CTC_ERROR_RETURN(sys_greatbelt_l2_get_unknown_mcast_tocpu(lchip, fid, &unknown_mcast_tocpu));
        value = value ? 1 : 0;

        if (value == unknown_mcast_tocpu)
        {
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN(sys_greatbelt_l2_set_unknown_mcast_tocpu(lchip, fid, value));


        SYS_VLAN_LOCK(lchip);
        if (0 == p_gb_vlan_master[lchip]->unknown_mcast_tocpu_nhid)
        {
            CTC_ERROR_RETURN_VLAN_UNLOCK(sys_greatbelt_common_init_unknown_mcast_tocpu(lchip, &unknown_mcast_tocpu_nhid));
            p_gb_vlan_master[lchip]->unknown_mcast_tocpu_nhid = unknown_mcast_tocpu_nhid;
        }

        l2dflt_addr.fid = fid;
        l2dflt_addr.with_nh = 1;
        l2dflt_addr.member.nh_id = p_gb_vlan_master[lchip]->unknown_mcast_tocpu_nhid;
        SYS_VLAN_UNLOCK(lchip);

        if (value)
        {
            CTC_ERROR_RETURN(sys_greatbelt_l2_add_port_to_default_entry(lchip, &l2dflt_addr));
        }
        else
        {
            CTC_ERROR_RETURN(sys_greatbelt_l2_remove_port_from_default_entry(lchip, &l2dflt_addr));
        }

        return CTC_E_NONE;

    case CTC_VLAN_PROP_FID:
        SYS_L2_FID_CHECK(value);
        cmd = DRV_IOW(DsVlan_t, DsVlan_VsiId_f);
        break;

    case CTC_VLAN_PROP_IPV4_BASED_L2MC:
        SYS_VLAN_LOCK(lchip);
        ret = _sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV4LOOKUP, value);
        CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

        value = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
        SYS_VLAN_UNLOCK(lchip);
        cmd = DRV_IOW(DsVlan_t, DsVlan_ProfileId_f);
        break;

    case CTC_VLAN_PROP_IPV6_BASED_L2MC:
        SYS_VLAN_LOCK(lchip);
        ret = _sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV6LOOKUP, value);
        CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

        value = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
        SYS_VLAN_UNLOCK(lchip);
        cmd = DRV_IOW(DsVlan_t, DsVlan_ProfileId_f);
        break;

    case  CTC_VLAN_PROP_PTP_EN:
        SYS_VLAN_LOCK(lchip);
        ret = _sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_PTP_EN, value);
        CTC_ERROR_RETURN_VLAN_UNLOCK(ret);

        value = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
        SYS_VLAN_UNLOCK(lchip);
        cmd = DRV_IOW(DsVlan_t, DsVlan_ProfileId_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_get_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32* value)
{

    uint32 cmd = 0;
    uint16 vlan_ptr = 0;
    bool vlan_profile_prop = FALSE;
    uint32 fid = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d \n", vlan_id, vlan_prop);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    /* zero value*/
    *value = 0;

    switch (vlan_prop)
    {
    /* vlan proprety for ctc layer*/
    case CTC_VLAN_PROP_RECEIVE_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_ReceiveDisable_f);
        break;

    case CTC_VLAN_PROP_BRIDGE_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_BridgeDisable_f);
        break;

    case CTC_VLAN_PROP_TRANSMIT_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_TransmitDisable_f);
        break;

    case CTC_VLAN_PROP_LEARNING_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_LearningDisable_f);
        break;

    case CTC_VLAN_PROP_DHCP_EXCP_TYPE:
        cmd = DRV_IOR(DsVlan_t, DsVlan_DhcpExceptionType_f);
        break;

    case CTC_VLAN_PROP_ARP_EXCP_TYPE:
        cmd = DRV_IOR(DsVlan_t, DsVlan_ArpExceptionType_f);
        break;

    case CTC_VLAN_PROP_IGMP_SNOOP_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_IgmpsnoopEn_f);
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_UCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_UcastDiscard_f);
        break;

    case CTC_VLAN_PROP_DROP_UNKNOWN_MCAST_EN:
        cmd = DRV_IOR(DsVlan_t, DsVlan_McastDiscard_f);
        break;

    case CTC_VLAN_PROP_UNKNOWN_MCAST_COPY_TO_CPU:
        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &fid));

        CTC_ERROR_RETURN(sys_greatbelt_l2_get_unknown_mcast_tocpu(lchip, fid, value));
        return CTC_E_NONE;

    case CTC_VLAN_PROP_FID:
        cmd = DRV_IOR(DsVlan_t, DsVlan_VsiId_f);
        break;

    case CTC_VLAN_PROP_IPV4_BASED_L2MC:
        SYS_VLAN_LOCK(lchip);
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV4LOOKUP, value));
        SYS_VLAN_UNLOCK(lchip);
        vlan_profile_prop = TRUE;
        break;

    case CTC_VLAN_PROP_IPV6_BASED_L2MC:
        SYS_VLAN_LOCK(lchip);
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_FORCE_IPV6LOOKUP, value));
        SYS_VLAN_UNLOCK(lchip);
        vlan_profile_prop = TRUE;
        break;

    case CTC_VLAN_PROP_PTP_EN:
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_VLAN_PROP_PTP_EN == vlan_prop)
    {
        SYS_VLAN_LOCK(lchip);
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile(lchip, vlan_id, SYS_VLAN_PROP_PTP_EN, value));
        SYS_VLAN_UNLOCK(lchip);
    }
    else
    {
        if (!vlan_profile_prop)
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, value));

            if (CTC_VLAN_PROP_BRIDGE_EN == vlan_prop || CTC_VLAN_PROP_LEARNING_EN == vlan_prop ||
                CTC_VLAN_PROP_RECEIVE_EN == vlan_prop || CTC_VLAN_PROP_TRANSMIT_EN == vlan_prop)
            {
                *value = !(*value);
            }
        }
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_set_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32  value)
{
    uint32 cmd = 1;
    uint16 vlan_ptr = 0;
    uint32 prop = 0;
    uint32 acl_en = 0;
    uint32 get_value = 0;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                     "vlan_id :%d, vlan_prop:%d, dir:%d ,value:%d \n", vlan_id, vlan_prop, dir, value);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

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

            SYS_VLAN_LOCK(lchip);
            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            prop = SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_1);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            prop = SYS_VLAN_PROP_INGRESS_VLAN_IPV6_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_2);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_3);
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            SYS_VLAN_UNLOCK(lchip);

            if (0 == value)     /* if acl dis, set vlan label = 0 */
            {
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL;
            }
            else
            {
                cmd = 0;
            }

            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:

            CTC_ERROR_RETURN(sys_greatbelt_vlan_get_direction_property
                                 (lchip, vlan_id, CTC_VLAN_DIR_PROP_ACL_EN, CTC_INGRESS, &get_value));
            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GB_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GB_MAX_ACL_VLAN_CLASSID))
                {
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL;
            }
            else
            {
                return CTC_E_VLAN_ACL_EN_FIRST;
            }

            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            value = value ? 1 : 0;
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            SYS_VLAN_LOCK(lchip);
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, value));
            SYS_VLAN_UNLOCK(lchip);
        }
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (vlan_prop)
        {

        case CTC_VLAN_DIR_PROP_ACL_EN:
            if (value > (CTC_ACL_EN_0 + CTC_ACL_EN_1 + CTC_ACL_EN_2 + CTC_ACL_EN_3))
            {
                SYS_VLAN_UNLOCK(lchip);
                return CTC_E_INVALID_PARAM;
            }

            SYS_VLAN_LOCK(lchip);
            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            prop = SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_1);
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            prop = SYS_VLAN_PROP_EGRESS_VLAN_IPV6_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_2);
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_3);
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, acl_en));
            SYS_VLAN_UNLOCK(lchip);

            if (0 == value)     /* if acl dis, set vlan label = 0 */
            {
                prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL;
            }
            else
            {
                cmd = 0;
            }

            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:

            CTC_ERROR_RETURN(sys_greatbelt_vlan_get_direction_property
                                 (lchip, vlan_id, CTC_VLAN_DIR_PROP_ACL_EN, CTC_EGRESS, &get_value));
            if (get_value)     /* vlan acl enabled */
            {
                if (((value) < SYS_GB_MIN_ACL_VLAN_CLASSID) || ((value) > SYS_GB_MAX_ACL_VLAN_CLASSID))
                {
                    return CTC_E_INVALID_PARAM;
                }
                prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL;
            }
            else
            {
                return CTC_E_VLAN_ACL_EN_FIRST;
            }

            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            value = value ? 1 : 0;
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            SYS_VLAN_LOCK(lchip);
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_set_vlan_profile(lchip, vlan_id, prop, value));
            SYS_VLAN_UNLOCK(lchip);
        }
    }

    SYS_VLAN_LOCK(lchip);
    value = p_gb_vlan_master[lchip]->p_vlan[vlan_id];
    SYS_VLAN_UNLOCK(lchip);

    cmd = DRV_IOW(DsVlan_t, DsVlan_ProfileId_f);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, vlan_ptr, cmd, &value));

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_get_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32* value)
{

    uint16 vlan_ptr = 0;
    uint32  prop = 0;
    uint32  acl_en = 0;
    uint32  cmd = 1;

    SYS_VLAN_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);

    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_VLAN_DBG_OUT(CTC_DEBUG_LEVEL_INFO,
                     "vlan_id :%d vlan_prop:%d dir:%d\n", vlan_id, vlan_prop, dir);
    sys_greatbelt_vlan_get_vlan_ptr(lchip, vlan_id, &vlan_ptr);
    SYS_VLAN_CREATE_CHECK(vlan_ptr);

    /* zero value*/
    *value = 0;

    SYS_VLAN_LOCK(lchip);
    if (CTC_INGRESS == dir)
    {
        switch (vlan_prop)
        {
        case CTC_VLAN_DIR_PROP_ACL_EN:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN0;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_0);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_1);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_2);
            }

            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_LABEL;
            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            prop = SYS_VLAN_PROP_INGRESS_VLAN_ACL_ROUTED_ONLY;
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
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_0);
            }

            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN1;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_1);
            }

            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN2;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_2);
            }

            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_EN3;
            CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                                 (lchip, vlan_id, prop, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_VLAN_DIR_PROP_ACL_CLASSID:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_LABEL;
            break;

        case CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:
            prop = SYS_VLAN_PROP_EGRESS_VLAN_ACL_ROUTED_ONLY;
            break;

        default:
            SYS_VLAN_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
    }

    if (cmd)
    {
        CTC_ERROR_RETURN_VLAN_UNLOCK(_sys_greatbelt_vlan_get_vlan_profile
                             (lchip, vlan_id, prop, value));
    }
    SYS_VLAN_UNLOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_greatbelt_vlan_dft_entry_change_for_linkagg(uint8 lchip, uint16 gport, uint16 agg_port, bool is_add, bool is_agg_destroy)
{
    uint8 idx = 0;
    uint8 vid = 0;
    uint16 vlan_id = 0;
    uint32* p_vlan_bmp = NULL;
    int32 ret = 0;
    ctc_l2dflt_addr_t l2dflt_addr;
    sal_memset(&l2dflt_addr, 0, sizeof(ctc_l2dflt_addr_t));

    if (FALSE == sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(gport)))
    {
        return CTC_E_NONE;
    }

    SYS_VLAN_LOCK(lchip);
    p_vlan_bmp = _sys_greatbelt_vlan_port_vec_lkup(lchip, CTC_MAP_GPORT_TO_LPORT(gport));
    if (NULL == p_vlan_bmp)
    {
        SYS_VLAN_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    for (idx = 0; idx < SYS_VLAN_BITMAP_INDEX; idx++)
    {
        if (p_vlan_bmp[idx] == 0)
        {
            continue;
        }

        for (vid = 0; vid < 32; vid++)
        {
            if (!CTC_IS_BIT_SET(p_vlan_bmp[idx], vid))
            {
               continue;
            }

            vlan_id = idx * 32 + vid;
            l2dflt_addr.fid = vlan_id;
            l2dflt_addr.member.mem_port = gport;

            if (FALSE == is_add)/*remove gport from agg, need add the gport to vlan default entry*/
            {
                CTC_ERROR_RETURN_VLAN_UNLOCK( sys_greatbelt_l2_add_port_to_default_entry(lchip, &l2dflt_addr));
                if ((TRUE == is_agg_destroy) || (0 == sys_greatbelt_linkagg_get_mem_num_in_vlan(lchip, vlan_id, agg_port)))
                {
                    SYS_VLAN_UNLOCK(lchip);
                    sys_greatbelt_vlan_remove_port_from_dft_entry(lchip, vlan_id, agg_port);
                    SYS_VLAN_LOCK(lchip);
                }
            }
            else
            {
                sys_greatbelt_l2_remove_port_from_default_entry(lchip, &l2dflt_addr);
                SYS_VLAN_UNLOCK(lchip);
                ret = sys_greatbelt_vlan_add_port_to_dft_entry(lchip, vlan_id, agg_port);
                SYS_VLAN_LOCK(lchip);
            }

        }
    }
    SYS_VLAN_UNLOCK(lchip);

    return  ret;
}


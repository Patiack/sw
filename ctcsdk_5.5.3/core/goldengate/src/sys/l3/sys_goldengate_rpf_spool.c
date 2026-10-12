/**
 @file sys_goldengate_rpf_spool.c

 @date 2012-09-17

 @version v2.0

 The file contains all rpf share pool related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_debug.h"
#include "ctc_spool.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_rpf_spool.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_lib.h"

struct sys_rpf_master_s
{
    ctc_spool_t*      rpf_spool;
    ctc_vector_t*   rpf_profile_vec;
};
typedef struct sys_rpf_master_s sys_rpf_master_t;

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

static sys_rpf_master_t* p_gg_rpf_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

int32
_sys_goldengate_rpf_set_default(uint8 lchip)
{
    DsRpf_m ds_rpf;
    uint32 cmd = 0;
    uint32 rpf_check_port = 0;

    sal_memset(&ds_rpf, 0, sizeof(DsRpf_m));

    cmd = DRV_IOR(IpeRouteCtl_t, IpeRouteCtl_rpfCheckAgainstPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rpf_check_port));

    if (rpf_check_port)
    {
        SetDsRpf(V, array_0_rpfIfId_f, &ds_rpf, SYS_RSV_PORT_DROP_ID);
    }

    SetDsRpf(V, array_0_rpfIfIdValid_f, &ds_rpf, 1);

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "%s %d ds_rpf index 0x%x.\n", __FUNCTION__, __LINE__, SYS_RPF_PROFILE_DEFAULT_ENTRY);

    cmd = DRV_IOW(DsRpf_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_RPF_PROFILE_DEFAULT_ENTRY, cmd, &ds_rpf));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_rpf_sort_profile(uint8 lchip, sys_rpf_intf_t* p_rpf_profile, uint8* valid_num)
{
    uint8 index = 0;
    uint8 index2 = 0;
    uint8 valid_count = 0;
    uint8 hit = 0;
    sys_rpf_intf_t tmp;

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memcpy(&tmp, p_rpf_profile, sizeof(sys_rpf_intf_t));
    sal_memset(p_rpf_profile, 0, sizeof(sys_rpf_intf_t));

    for (index = 0; index < SYS_GG_MAX_IPMC_RPF_IF; index++)
    {
        if (tmp.rpf_intf_valid[index])
        {
            hit = 0;
            for (index2 = 0; index2 < valid_count; index2++)
            {
                if (tmp.rpf_intf[index] < p_rpf_profile->rpf_intf[index2])
                {
                    sal_memmove(&(p_rpf_profile->rpf_intf[index2 + 1]), &(p_rpf_profile->rpf_intf[index2]), sizeof(uint16) * (valid_count - index2));
                    p_rpf_profile->rpf_intf[index2] = tmp.rpf_intf[index];
                    hit = 1;
                    break;
                }
            }

            if (!hit)
            {
                p_rpf_profile->rpf_intf[index2] = tmp.rpf_intf[index];
            }

            valid_count++;

        }
    }

    sal_memset(p_rpf_profile->rpf_intf_valid, 0, sizeof(p_rpf_profile->rpf_intf_valid));

    for (index = 0; index < valid_count; index++)
    {
        p_rpf_profile->rpf_intf_valid[index] = 1;
    }

    *valid_num = valid_count;

    return CTC_E_NONE;
}

STATIC uint32
_sys_goldengate_rpf_make_hash(sys_rpf_profile_t* p_rpf_profile)
{
    uint32 a, b, c;
    uint32* k = (uint32*)&p_rpf_profile->rpf_intf;
    uint8*  k8;
    uint8   length = sizeof(uint16) * SYS_GG_MAX_IPMC_RPF_IF;

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
_sys_goldengate_rpf_compare_hash(sys_rpf_profile_t* p_bucket, sys_rpf_profile_t* p_new)
{
    if (!p_bucket || !p_new)
    {
        return FALSE;
    }

    if (!sal_memcmp(&p_bucket->rpf_intf, &p_new->rpf_intf, sizeof(uint16) * SYS_GG_MAX_IPMC_RPF_IF))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC int32
_sys_goldengate_rpf_build_index(uint8 lchip, uint16* p_index, sys_rpf_usage_type_t usage)
{
    sys_goldengate_opf_t opf;
    uint32 value_32 = SYS_RPF_INVALID_PROFILE_ID;

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_RPF;

    if (SYS_RPF_USAGE_TYPE_IPMC == usage)
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32));
    }
    else if (SYS_RPF_USAGE_TYPE_IPUC == usage)
    {
        opf.reverse = 1;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32));
    }

    *p_index = value_32;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_rpf_free_index(uint8 lchip, uint16 index)
{
    sys_goldengate_opf_t opf;

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_RPF;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, (uint32)index));

    return CTC_E_NONE;
}

int32
sys_goldengate_rpf_remove_profile(uint8 lchip, uint16 rpf_profile_index)
{
    uint16 index_free = 0;
    int32 ret = 0;
    void* p_vector = NULL;

    sys_rpf_profile_t* p_rpf_profile_del;

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if(SYS_RPF_PROFILE_DEFAULT_ENTRY == rpf_profile_index)
    {
        return CTC_E_NONE;
    }

    p_rpf_profile_del = ctc_vector_get(p_gg_rpf_master[lchip]->rpf_profile_vec, rpf_profile_index);
    if (!p_rpf_profile_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_gg_rpf_master[lchip]->rpf_spool, p_rpf_profile_del, NULL);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            index_free = p_rpf_profile_del->rpf_profile_index;
            p_vector = ctc_vector_del(p_gg_rpf_master[lchip]->rpf_profile_vec, rpf_profile_index);

            if (NULL == p_vector)
            {
                return CTC_E_RPF_SPOOL_REMOVE_VECTOR_FAILED;
            }

            /*free ad index*/
            CTC_ERROR_RETURN(_sys_goldengate_rpf_free_index(lchip, index_free));
            SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: db_ad removed. removed rpf index = %d\n",  index_free);
            mem_free(p_rpf_profile_del);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_rpf_add_profile(uint8 lchip, sys_rpf_info_t* p_rpf_info, sys_rpf_rslt_t* p_rpf_rslt)
{
    int32 ret = 0;
    uint16 old_profile_id = 0;
    uint16 new_profile_id = 0;
    uint8 valid_num = 0;

    sys_rpf_profile_t* p_exist_rpf_profile = NULL;
    sys_rpf_profile_t* p_rpf_profile = NULL;
    sys_rpf_profile_t* p_new_rpf_profile = NULL;

    SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_rpf_info);
    CTC_PTR_VALID_CHECK(p_rpf_rslt);

    if (SYS_RPF_USAGE_TYPE_DEFAULT == p_rpf_info->usage)
    {
        p_rpf_rslt->mode = SYS_RPF_CHK_MODE_PROFILE;
        p_rpf_rslt->id = SYS_RPF_PROFILE_DEFAULT_ENTRY;
        return CTC_E_NONE;
    }

    CTC_PTR_VALID_CHECK(p_rpf_info->intf);
    CTC_ERROR_RETURN(_sys_goldengate_rpf_sort_profile(lchip, p_rpf_info->intf, &valid_num));

    if (0 == valid_num)
    {
        p_rpf_rslt->mode = SYS_RPF_CHK_MODE_IFID;
        p_rpf_rslt->id = SYS_RPF_INVALID_PROFILE_ID;
    }
    else if ((1 == valid_num) && (FALSE == p_rpf_info->force_profile))
    {
        p_rpf_rslt->mode = SYS_RPF_CHK_MODE_IFID;
        p_rpf_rslt->id = p_rpf_info->intf->rpf_intf[0];
    }
    else if (((1 == valid_num) && (TRUE == p_rpf_info->force_profile)) || (valid_num > 1))
    {
        p_rpf_profile = mem_malloc(MEM_RPF_MODULE, sizeof(sys_rpf_profile_t));

        if (NULL == p_rpf_profile)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_rpf_profile, 0, sizeof(sys_rpf_profile_t));

        sal_memcpy(p_rpf_profile->rpf_intf, p_rpf_info->intf->rpf_intf, sizeof(uint16) * valid_num);
        p_rpf_profile->rpf_profile_index = p_rpf_info->profile_index;
        p_exist_rpf_profile = ctc_vector_get(p_gg_rpf_master[lchip]->rpf_profile_vec, p_rpf_profile->rpf_profile_index);

        if(p_exist_rpf_profile)
        {
            if(TRUE == _sys_goldengate_rpf_compare_hash(p_exist_rpf_profile, p_rpf_profile))
            {
                p_rpf_rslt->mode = SYS_RPF_CHK_MODE_PROFILE;
                p_rpf_rslt->id = p_exist_rpf_profile->rpf_profile_index;
                mem_free(p_rpf_profile);
                return CTC_E_NONE;
            }
            old_profile_id = p_exist_rpf_profile->rpf_profile_index;
        }

        ret = ctc_spool_add(p_gg_rpf_master[lchip]->rpf_spool, p_rpf_profile, p_exist_rpf_profile, &p_new_rpf_profile);
        SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: ctc_spool_add.\n");


        if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
        {
            mem_free(p_rpf_profile);
        }

        if (ret == CTC_SPOOL_E_OPERATE_REFCNT || ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
            {
                SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: ad not exists, add a new one.\n");
                ret = _sys_goldengate_rpf_build_index(lchip, &new_profile_id, p_rpf_info->usage);
                if (ret < 0)
                {
                    ctc_spool_remove(p_gg_rpf_master[lchip]->rpf_spool, p_new_rpf_profile, NULL);
                    mem_free(p_rpf_profile);

                    if (CTC_E_NO_RESOURCE == ret)
                    {
                        ret = CTC_E_PROFILE_NO_RESOURCE;
                    }
                    return ret;
                }

                p_new_rpf_profile->rpf_profile_index = new_profile_id;
                ret = ctc_vector_add(p_gg_rpf_master[lchip]->rpf_profile_vec, p_new_rpf_profile->rpf_profile_index, p_new_rpf_profile);

                if (FALSE == ret)
                {
                    _sys_goldengate_rpf_free_index(lchip, new_profile_id);
                    ctc_spool_remove(p_gg_rpf_master[lchip]->rpf_spool, p_new_rpf_profile, NULL);
                    mem_free(p_rpf_profile);
                    return CTC_E_RPF_SPOOL_ADD_VECTOR_FAILED;
                }

                SYS_RPF_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  %% INFO: new_profile_id is %d.\n", new_profile_id);
            }

            /*if profile_id not change, no need to delete old ad(old ad is replaced by new)*/
            if (p_exist_rpf_profile && p_new_rpf_profile->rpf_profile_index != old_profile_id)
            {
                CTC_ERROR_RETURN(sys_goldengate_rpf_remove_profile(lchip, p_rpf_info->profile_index));
            }

            p_rpf_rslt->mode = SYS_RPF_CHK_MODE_PROFILE;
            p_rpf_rslt->id = p_new_rpf_profile->rpf_profile_index;
        }
        else
        {
            return CTC_E_SPOOL_ADD_UPDATE_FAILED;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_rpf_restore_profile(uint8 lchip, uint32 rpf_profile_index)
{
    int32 ret;
    uint32 cmd;
    uint32 value;
    DsRpf_m ds_rpf;
    sys_goldengate_opf_t opf;
    sys_rpf_profile_t* p_rpf_profile = NULL;
    sys_rpf_profile_t* p_new_rpf_profile = NULL;

    p_rpf_profile = mem_malloc(MEM_RPF_MODULE,  sizeof(sys_rpf_profile_t));
    if (NULL == p_rpf_profile)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_rpf_profile, 0, sizeof(sys_rpf_profile_t));

    p_rpf_profile->rpf_profile_index = rpf_profile_index;
    sal_memset(&ds_rpf, 0, sizeof(ds_rpf));
    cmd = DRV_IOR(DsRpf_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, rpf_profile_index, cmd, &ds_rpf), ret, error);

    value = 0;
    GetDsRpf(A, array_0_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[0] = value;
    value = 0;
    GetDsRpf(A, array_1_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[1] = value;
    value = 0;
    GetDsRpf(A, array_2_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[2] = value;
    value = 0;
    GetDsRpf(A, array_3_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[3] = value;
    value = 0;
    GetDsRpf(A, array_4_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[4] = value;
    value = 0;
    GetDsRpf(A, array_5_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[5] = value;
    value = 0;
    GetDsRpf(A, array_6_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[6] = value;
    value = 0;
    GetDsRpf(A, array_7_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[7] = value;
    value = 0;
    GetDsRpf(A, array_8_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[8] = value;
    value = 0;
    GetDsRpf(A, array_9_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[9] = value;
    value = 0;
    GetDsRpf(A, array_10_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[10] = value;
    value = 0;
    GetDsRpf(A, array_11_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[11] = value;
    value = 0;
    GetDsRpf(A, array_12_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[12] = value;
    value = 0;
    GetDsRpf(A, array_13_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[13] = value;
    value = 0;
    GetDsRpf(A, array_14_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[14] = value;
    value = 0;
    GetDsRpf(A, array_15_rpfIfId_f, &ds_rpf, &value);
    p_rpf_profile->rpf_intf[15] = value;

    if (FALSE == ctc_vector_add(p_gg_rpf_master[lchip]->rpf_profile_vec, p_rpf_profile->rpf_profile_index, p_rpf_profile))
    {
        ret = CTC_E_UNEXPECT;
        goto error;
    }

    ret = ctc_spool_add(p_gg_rpf_master[lchip]->rpf_spool, p_rpf_profile, NULL, &p_new_rpf_profile);
    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto error;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            /*alloc rpf profile index from position*/
            opf.pool_type = OPF_RPF;

            ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, rpf_profile_index);
            if (ret)
            {
                ctc_spool_remove(p_gg_rpf_master[lchip]->rpf_spool, p_new_rpf_profile, NULL);

                goto error;
            }
        }
        else
        {
            if (p_rpf_profile)
            {
                mem_free(p_rpf_profile);
            }
        }
    }

    return CTC_E_NONE;

    error:
        if (p_rpf_profile)
        {
            mem_free(p_rpf_profile);
        }
        return ret;
}

/* init the rpf module */
int32
sys_goldengate_rpf_init(uint8 lchip)
{
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 entry_num_rpf = 0;
    uint16 block_size = 0;
    uint8  data_size = 0;
    uint8  resv_num = 0;

    DsRpf_m ds_rpf_profile;
    sys_goldengate_opf_t opf;
    ctc_spool_t spool;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    if (NULL != p_gg_rpf_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsRpf_t, &entry_num_rpf));

    resv_num = SYS_RPF_PROFILE_RESV_TYPE_NUM;

    if (entry_num_rpf > resv_num)
    {
        entry_num_rpf = entry_num_rpf - resv_num;
        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_RPF, 1));

        CTC_ERROR_RETURN(_sys_goldengate_rpf_set_default(lchip));

        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type  = OPF_RPF;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, entry_num_rpf));
        CTC_ERROR_RETURN(sys_goldengate_opf_init_reverse_size(lchip, &opf, (entry_num_rpf / 2)));
    }

    p_gg_rpf_master[lchip] = mem_malloc(MEM_RPF_MODULE, sizeof(sys_rpf_master_t));
    if (NULL == p_gg_rpf_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_rpf_master[lchip], 0, sizeof(sys_rpf_master_t));

    block_size = (entry_num_rpf + 1) & 0xFFFF;
    data_size = sizeof(sys_rpf_profile_t);

    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = block_size;
    spool.max_count = entry_num_rpf;
    spool.user_data_size = data_size;
    spool.spool_key = (hash_key_fn)_sys_goldengate_rpf_make_hash;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_rpf_compare_hash;
    p_gg_rpf_master[lchip]->rpf_spool = ctc_spool_create(&spool);

    p_gg_rpf_master[lchip]->rpf_profile_vec = ctc_vector_init(1, block_size);
    if (NULL == p_gg_rpf_master[lchip]->rpf_profile_vec)
    {
        ctc_vector_release(p_gg_rpf_master[lchip]->rpf_profile_vec);
    }

    /* init for ipmc and ipuc use. */
    sal_memset(&ds_rpf_profile, 0, sizeof(ds_rpf_profile));

    for (index = 0; index < entry_num_rpf; index++)
    {
        cmd = DRV_IOW(DsRpf_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index + SYS_RPF_PROFILE_START_INDEX, cmd, &ds_rpf_profile));
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_rpf_deinit(uint8 lchip)
{
    uint32 entry_num_rpf = 0;
    uint8  resv_num = 0;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_rpf_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*don't need free rpf profile data, because data of vector is same to data of spool*/
    ctc_vector_release(p_gg_rpf_master[lchip]->rpf_profile_vec);

    /*free rpf ad*/
    ctc_spool_free(p_gg_rpf_master[lchip]->rpf_spool);

    sys_goldengate_ftm_query_table_entry_num(lchip, DsRpf_t, &entry_num_rpf);

    resv_num = SYS_RPF_PROFILE_RESV_TYPE_NUM;

    if (entry_num_rpf > resv_num)
    {
        sys_goldengate_opf_deinit(lchip, OPF_RPF);

    }

    mem_free(p_gg_rpf_master[lchip]);

    return CTC_E_NONE;
}


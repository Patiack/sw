/****************************************************************************
 * drv_model_hash.c  Provides driver model hash handle function declaration.
 *
 * Copyright (C) 2012 Centec Networks Inc.  All rights reserved.
 *
 * Modify History:
 * Revision:    V1.0
 * Author       JiangSZ.
 * Date:        2012-11-27.
 * Reason:      Create for AutoGen hash module.
 ****************************************************************************/

#include "goldengate/include/drv_lib.h"

extern uint8 drv_goldengate_get_host_type (uint8 lchip);

#define BYTES_PER_WORD 4
#define MAX_CAM_BYTE   48
#define MAC_BLACKHOLE_HASHKEY_NUM 128
#define MAC_BLACKHOLE_HASH_KEY_BYTE 6
#define AGING_BLOCK_SIZE_64K       ( 64 * 1024 )
#define AGING_BLOCK_SIZE_32K       ( 32 * 1024 )
#define AGING_BLOCK_SIZE_16K       ( 16 * 1024 )

#define FIB_HOST0_HASH_LOOKUP_CTL_BYTES sizeof(FibHost0HashLookupCtl_m)
#define FIB_HOST1_HASH_LOOKUP_CTL_BYTES sizeof(FibHost1HashLookupCtl_m)
#define EGRESS_XC_OAM_HASH_LOOKUP_CTL_BYTES sizeof(EgressXcOamHashLookupCtl_m)
#define FLOW_HASH_LOOKUP_CTL_BYTES sizeof(FlowHashLookupCtl_m)
#define IPFIX_HASH_LOOKUP_CTL_BYTES sizeof(IpfixHashLookupCtl_m)
#define USER_ID_HASH_LOOKUP_CTL_BYTES sizeof(UserIdHashLookupCtl_m)

uint32 gg_hash_debug_on = FALSE;

uint32 gg_hash_key_length[HASH_MODULE_NUM] =
{
    170,    /* HASH_MODULE_EGRESS_XC */
    170,    /* HASH_MODULE_FIB_HOST0 */
    170,    /* HASH_MODULE_FIB_HOST1 */
    340,    /* HASH_MODULE_FLOW */
    340,    /* HASH_MODULE_IPFIX */
    340     /* HASH_MODULE_USERID */
};

STATIC void
_array_shift_left(uint8* dst, uint8* src, uint32 shift, uint32 len)
{
    int i = 0;
    int realshift =0;
    uint8 mask = 0;

    realshift = shift%8;
    mask = (1<<realshift) - 1;

    if(shift != 0)
    {
        dst[0] = (src[0] & mask) << (8-realshift);

        for(i = 1; i< (len -1); i++)
        {
            dst[i] = (((src[i]&mask) << (8-realshift)) | (src[i-1] >> realshift));
        }
        dst[len-1] = (src[len-1-1] >> realshift);
    }
    else
    {
        sal_memcpy(dst, src, len-1);
    }
}

void
_array_un_shift_left(uint8* dst, uint8* src, uint32 shift, uint32 len)
{
    int i = 0;
    int realshift =0;

    realshift = shift%8;

    if(shift != 0)
    {
        for (i = 0; i < (len - 1); i++)
            dst[i] = (src[i] >> (8-realshift)) | (src[i+1] << realshift);
    }
    else
    {
        sal_memcpy(dst, src, len-1);
    }
}

STATIC uint32
_get_hash_lookup_ctl (hash_module_t hash_module, uint32* p_crc_index )
{
    uint32 level;
    switch (hash_module)
    {
        case HASH_MODULE_EGRESS_XC:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;
        case HASH_MODULE_FIB_HOST0:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;
        case HASH_MODULE_FIB_HOST1:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;
        case HASH_MODULE_FLOW:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;
        /*case HASH_MODULE_IPFIX:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;*/
        case HASH_MODULE_USERID:
            for (level=0; level<hash_db[hash_module].level_num;level++)
            {
                p_crc_index[level]= 1;
            }
            break;
        default:
            break;
    }

    return DRV_E_NONE;

}


STATIC int32
_get_key_index_base(uint8 chip_id,hash_module_t hash_module, uint32 hash_type, uint32 level, level_lookup_result_t *level_result)
{
    uint32 index_base = 0;
    uint32 depth = 0;
    int i = 0;
    int32 cmd = 0;
    int32 hash_size_mode = 1;

    switch (hash_module)
    {
        case HASH_MODULE_EGRESS_XC:
            cmd = DRV_IOR(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashSizeMode_f);
            break;

        case HASH_MODULE_FIB_HOST0:
            cmd = DRV_IOR(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0HashSizeMode_f);
            break;

        case HASH_MODULE_FIB_HOST1:
            if (hash_type == FIBHOST1HASHTYPE_IPV4NATDAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV6NATDAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV4MCAST
                || hash_type == FIBHOST1HASHTYPE_IPV6MCAST
                || hash_type == FIBHOST1HASHTYPE_MACIPV4MCAST
                || hash_type == FIBHOST1HASHTYPE_TRILLMCASTVLAN)
            {

                cmd = DRV_IOR(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaHashSizeMode_f);
            }
            else if (hash_type == FIBHOST1HASHTYPE_IPV4NATSAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV6NATSAPORT
                || hash_type == FIBHOST1HASHTYPE_TRILLMCASTVLAN )
            {

                cmd = DRV_IOR(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaHashSizeMode_f);
            }
            break;

        case HASH_MODULE_FLOW:
            cmd = DRV_IOR(FlowHashLookupCtl_t, FlowHashLookupCtl_flowHashSizeMode_f);
            break;

        case HASH_MODULE_IPFIX:
            cmd = DRV_IOR(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixHashSizeMode_f);
            break;

        case HASH_MODULE_USERID:
            cmd = DRV_IOR(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdHashSizeMode_f);
            break;

        default:
            break;
    }

    DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, &hash_size_mode));
    for (i = 0; i < level; i++)
    {
        if(level_result[i].level_en)
        {
            if(hash_module == HASH_MODULE_FLOW
                || hash_module == HASH_MODULE_IPFIX
                || hash_module == HASH_MODULE_EGRESS_XC)
            {
                depth = hash_db[hash_module].p_level[i].depth * 2;
            }
            else
            {
                depth = hash_db[hash_module].p_level[i].depth;
            }
            index_base += depth * hash_db[hash_module].p_level[i].bucket_num << hash_size_mode;
        }
    }

    return index_base;
}

STATIC uint8 _drv_is_host1_sa_lookup(uint32 hash_module, uint32 hash_type)
{
    int32 is_host1_sa_loopup = 0;
    if (hash_module != HASH_MODULE_FIB_HOST1)
    {
        return is_host1_sa_loopup;
    }

    switch (hash_type)
    {
        case FIBHOST1HASHTYPE_IPV4NATDAPORT:
        case FIBHOST1HASHTYPE_IPV6NATDAPORT:
        case FIBHOST1HASHTYPE_IPV4MCAST:
        case FIBHOST1HASHTYPE_IPV6MCAST:
        case FIBHOST1HASHTYPE_MACIPV4MCAST:
        case FIBHOST1HASHTYPE_TRILLMCASTVLAN:
            is_host1_sa_loopup = TRUE;
            break;

        case FIBHOST1HASHTYPE_IPV4NATSAPORT:
        case FIBHOST1HASHTYPE_IPV6NATSAPORT:
        case FIBHOST1HASHTYPE_FCOERPF:
            is_host1_sa_loopup = FALSE;
            break;
        default:
            break;
    }

    return is_host1_sa_loopup;
}


STATIC int32
_get_hash_poly(uint8 chip_id,hash_module_t hash_module, uint32 hash_type, uint32 level,
                    uint32 *hash_poly, uint32 *poly_len)
{
#define MAX_WORD_LEN 12
#define MAX_HASH_LEVEL 6

    uint32 hash_lookup_ctl[MAX_WORD_LEN]={0};
    uint32 cmd = 0;
    uint32 hash_poly_type[MAX_HASH_LEVEL] = {0};
    uint32 hash_size_mode = 0;
    uint32 hash_special_mode = 0;
    uint8 is_sa_lookup = 0;

    if (hash_module == HASH_MODULE_FIB_HOST0)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        cmd = DRV_IOR(FibHost0HashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0HashSizeMode_f,
                      &hash_size_mode, hash_lookup_ctl);

        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level0HashType_f,
                      &hash_poly_type[0], hash_lookup_ctl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level1HashType_f,
                      &hash_poly_type[1], hash_lookup_ctl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level2HashType_f,
                      &hash_poly_type[2], hash_lookup_ctl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level3HashType_f,
                      &hash_poly_type[3], hash_lookup_ctl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level4HashType_f,
                      &hash_poly_type[4], hash_lookup_ctl);

        *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level]].poly;
        *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level]].poly_len;
    }
    else if (hash_module == HASH_MODULE_FIB_HOST1)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        is_sa_lookup = _drv_is_host1_sa_lookup(hash_module,hash_type);

        cmd = DRV_IOR(FibHost1HashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        if (!is_sa_lookup)
        {
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel0HashType_f,
                          &hash_poly_type[0], hash_lookup_ctl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel1HashType_f,
                          &hash_poly_type[1], hash_lookup_ctl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel2HashType_f,
                          &hash_poly_type[2], hash_lookup_ctl);
        }
        else
        {
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel0HashType_f,
                          &hash_poly_type[0], hash_lookup_ctl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel1HashType_f,
                          &hash_poly_type[1], hash_lookup_ctl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel2HashType_f,
                          &hash_poly_type[2], hash_lookup_ctl);
        }
        *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level]].poly;
        *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level]].poly_len;
    }
    else if (hash_module == HASH_MODULE_USERID)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel0HashType_f,
                      &hash_poly_type[0], hash_lookup_ctl);
        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel1HashType_f,
                      &hash_poly_type[1], hash_lookup_ctl);

        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdRamUsageMode_f,
                      &hash_special_mode, hash_lookup_ctl);

        if (hash_special_mode == 0)
        {
            /* level will be 0, 1 */
            *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level]].poly;
            *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level]].poly_len;
        }
        else
        {
            /* level will be 2, 3 */
            *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level-2]].poly;
            *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level-2]].poly_len;
        }
    }
    else if (hash_module == HASH_MODULE_FLOW)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        cmd = DRV_IOR(FlowHashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel0HashType_f,
                      &hash_poly_type[0], hash_lookup_ctl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel1HashType_f,
                      &hash_poly_type[1], hash_lookup_ctl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel2HashType_f,
                      &hash_poly_type[2], hash_lookup_ctl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel3HashType_f,
                      &hash_poly_type[3], hash_lookup_ctl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel4HashType_f,
                      &hash_poly_type[4], hash_lookup_ctl);

        *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level]].poly;
        *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level]].poly_len;
    }
    else if (hash_module == HASH_MODULE_IPFIX)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        cmd = DRV_IOR(IpfixHashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        DRV_IOR_FIELD(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixLevel0HashType_f,
                      &hash_poly_type[0], hash_lookup_ctl);
        DRV_IOR_FIELD(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixLevel1HashType_f,
                      &hash_poly_type[1], hash_lookup_ctl);

        *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[level]].poly;
        *poly_len = hash_db[hash_module].p_crc[hash_poly_type[level]].poly_len;
    }
    else if (hash_module == HASH_MODULE_EGRESS_XC)
    {
        sal_memset(hash_poly_type, 0, sizeof(hash_poly_type));

        sal_memset(hash_lookup_ctl, 0, sizeof(hash_lookup_ctl));
        cmd = DRV_IOR(EgressXcOamHashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, hash_lookup_ctl));

        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel0HashType_f,
                      &hash_poly_type[0], hash_lookup_ctl);
        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel1HashType_f,
                      &hash_poly_type[1], hash_lookup_ctl);

        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashMode_f,
                          &hash_special_mode, hash_lookup_ctl);

        if(level == 0)
        {
            *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[0]].poly;
            *poly_len = hash_db[hash_module].p_crc[hash_poly_type[0]].poly_len;
        }
        else
        {
            /*share level 1 hash type, whatever egressXcOamHashMode is enable */
            *hash_poly = hash_db[hash_module].p_crc[hash_poly_type[1]].poly;
            *poly_len = hash_db[hash_module].p_crc[hash_poly_type[1]].poly_len;
        }
    }

    return DRV_E_NONE;

}

STATIC uint32 _drv_ntoh(uint32 src)
{
    uint32 dst = 0;

    dst = ((src & 0xff000000) >> 24)
        | ((src & 0x00ff0000) >> 8)
        | ((src & 0x0000ff00) << 8)
        | ((src & 0x000000ff) << 24);

    return dst;
}

int32
_drv_goldengate_hash_lookup_convert_ds_endian(uint32 tbl_id, void* p_ds_key)
{
    uint8 word_index;
    uint8 word_num;
    uint32 key_size;
    ds_12word_hash_key_t ds_12word_hash_key;
    host_type_t byte_order;

    DRV_PTR_VALID_CHECK(p_ds_key);

    byte_order = drv_goldengate_get_host_type(0);

    if (HOST_BE == byte_order)
    {
        sal_memset(&ds_12word_hash_key, 0, sizeof(ds_12word_hash_key_t));

        key_size = TABLE_ENTRY_SIZE(tbl_id);

        word_num = key_size / DRV_BYTES_PER_WORD;

        if (key_size > sizeof(ds_12word_hash_key))
        {
            return DRV_E_EXCEED_MAX_SIZE;
        }

        sal_memcpy(&ds_12word_hash_key, p_ds_key, key_size);

        for (word_index = 0; word_index < word_num; word_index++)
        {
            ds_12word_hash_key.field[word_index] = _drv_ntoh(ds_12word_hash_key.field[word_index]);
        }

        sal_memcpy(p_ds_key, &ds_12word_hash_key, key_size);
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_hash_lookup_convert_xbits_key(uint32 tbl_id, void* p_src_key, uint8* p_dst_key, uint32 len)
{
    uint8 entry_index;
    uint8 entry_num;
    uint32 key_size;

    DRV_PTR_VALID_CHECK(p_src_key);
    DRV_PTR_VALID_CHECK(p_dst_key);

    key_size = TABLE_ENTRY_SIZE(tbl_id);

    entry_num = key_size / DRV_BYTES_PER_ENTRY;

    if((len % DRV_HASH_85BIT_KEY_LENGTH) == 0)
    {
        for (entry_index = 0; entry_index < entry_num; entry_index++)
        {
            sal_memcpy((uint8*)p_dst_key + (DRV_HASH_85BIT_KEY_LENGTH * entry_index),
                       (uint8*)((ds_3word_hash_key_t*)p_src_key + entry_index),
                        DRV_HASH_85BIT_KEY_LENGTH);
        }
    }
    else if ((len % DRV_HASH_170BIT_KEY_LENGTH) == 0)
    {
        for (entry_index = 0; entry_index < (entry_num/2); entry_index++)
        {
            sal_memcpy((uint8*)p_dst_key + (DRV_HASH_170BIT_KEY_LENGTH * entry_index),
                       (uint8*)((ds_6word_hash_key_t*)p_src_key + entry_index),
                        DRV_HASH_170BIT_KEY_LENGTH);
        }
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("Invalid hash length.\n");
    }

    return DRV_E_NONE;
}

STATIC int32
_drv_goldengate_hash_lookup_unconvert_xbits_key(uint32 tbl_id, void* p_src_key, uint8* p_dst_key, uint32 len)
{
    uint8 entry_index;
    uint8 entry_num;
    uint32 key_size;

    DRV_PTR_VALID_CHECK(p_src_key);
    DRV_PTR_VALID_CHECK(p_dst_key);

    key_size = TABLE_ENTRY_SIZE(tbl_id);

    entry_num = key_size / DRV_BYTES_PER_ENTRY;

    if((len % DRV_HASH_85BIT_KEY_LENGTH) == 0)
    {
        for (entry_index = 0; entry_index < entry_num; entry_index++)
        {
            sal_memcpy((uint8*)((ds_3word_hash_key_t*)p_dst_key + entry_index),
                (uint8*)p_src_key + (DRV_HASH_85BIT_KEY_LENGTH * entry_index),
                DRV_HASH_85BIT_KEY_LENGTH);
        }
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("Invalid hash length.\n");
    }

    return DRV_E_NONE;
}

STATIC uint32
_get_masked_key( uint8* dest_key, uint8* src_key, uint8* key_mask,uint32 len)
{
    int i = 0;

    for (i = 0; i<len; i++)
    {
        dest_key[i]= src_key[i]&key_mask[i];
    }

    return DRV_E_NONE;
}

STATIC uint32
_get_combined_key( uint8* p_dst_key, uint8* p_src_key, uint32 len, uint32 tbl_id)
{
    uint8 entry_index;
    uint8 entry_num;
    uint32 key_size;
    uint8 i;
    uint8 temp_array[4][DRV_HASH_85BIT_KEY_LENGTH + 1];
    uint32 real_key_len = 0;

    sal_memset(temp_array,0,sizeof(temp_array));

    key_size = TABLE_ENTRY_SIZE(tbl_id);

    entry_num = key_size / DRV_BYTES_PER_ENTRY;

    for (entry_index = 0; entry_index < entry_num; entry_index++)
    {
        _array_shift_left(temp_array[entry_index],(uint8*)p_src_key+(DRV_HASH_85BIT_KEY_LENGTH * entry_index),
                            3*entry_index,DRV_HASH_85BIT_KEY_LENGTH+1);
    }

    real_key_len = BIT2BYTE(len/DRV_HASH_85BIT_KEY_LENGTH*85);

    for(i = 0;i<real_key_len;i++)
    {
        if (i< (DRV_HASH_85BIT_KEY_LENGTH-1))
        {
            p_dst_key[i] = temp_array[0][i];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH-1))
        {
            p_dst_key[i] = temp_array[0][i] | temp_array[1][0] ;
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH) && (i<(DRV_HASH_85BIT_KEY_LENGTH*2-1)))
        {
            p_dst_key[i] = temp_array[1][i-DRV_HASH_85BIT_KEY_LENGTH+1];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*2-1))
        {
            p_dst_key[i] = temp_array[1][DRV_HASH_85BIT_KEY_LENGTH] | temp_array[2][0];
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH*2) && (i<(DRV_HASH_85BIT_KEY_LENGTH*3-2)))
        {
            p_dst_key[i] = temp_array[2][i-DRV_HASH_85BIT_KEY_LENGTH*2+1];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*3-2))
        {
            p_dst_key[i] = temp_array[2][DRV_HASH_85BIT_KEY_LENGTH-1] | temp_array[3][0];
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH*3-1) && (i<(DRV_HASH_85BIT_KEY_LENGTH*4-1-1)))
        {
            p_dst_key[i] = temp_array[3][i-DRV_HASH_85BIT_KEY_LENGTH*3+1+1];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*4-1-1))
        {
            p_dst_key[i] = temp_array[3][DRV_HASH_85BIT_KEY_LENGTH];
        }
    }


    return DRV_E_NONE;
}

STATIC uint32
_get_un_combined_key( uint8* p_dst_key, uint8* p_src_key, uint32 len, uint32 tbl_id)
{
    uint8 entry_index;
    uint8 entry_num;
    uint32 key_size;
    uint8 i;
    uint8 temp_array[4][DRV_HASH_85BIT_KEY_LENGTH + 1];
    uint32 real_key_len = 0;
    uint8 last = 0;

    sal_memset(temp_array,0,sizeof(temp_array));

    key_size = TABLE_ENTRY_SIZE(tbl_id);

    entry_num = key_size / DRV_BYTES_PER_ENTRY;

    real_key_len = BIT2BYTE(len/DRV_HASH_85BIT_KEY_LENGTH*85);

    for(i = 0;i<real_key_len;i++)
    {
        if (i == real_key_len - 1)
        {
            p_src_key[i] = p_src_key[i] & ((1 << (len/DRV_HASH_85BIT_KEY_LENGTH*85)%8) - 1);
            last = 1;
        }
        if (i< (DRV_HASH_85BIT_KEY_LENGTH-1))
        {
            temp_array[0][i] = p_src_key[i];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH-1))
        {
            temp_array[0][i] |= last ? p_src_key[i] : (p_src_key[i] & 0x1F);
            temp_array[1][0] = p_src_key[i] & 0xE0;
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH) && (i<(DRV_HASH_85BIT_KEY_LENGTH*2-1)))
        {
            temp_array[1][i-DRV_HASH_85BIT_KEY_LENGTH+1] = p_src_key[i];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*2-1))
        {
            temp_array[1][DRV_HASH_85BIT_KEY_LENGTH] |= last ? p_src_key[i] : (p_src_key[i] & 0x3);
            temp_array[2][0] =  p_src_key[i] & 0xFC;
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH*2) && (i<(DRV_HASH_85BIT_KEY_LENGTH*3-2)))
        {
            temp_array[2][i-DRV_HASH_85BIT_KEY_LENGTH*2+1] = p_src_key[i];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*3-2))
        {
            temp_array[2][DRV_HASH_85BIT_KEY_LENGTH-1] |= last ? p_src_key[i] : (p_src_key[i] & 0x7F);
            temp_array[3][0] = p_src_key[i] & 0x80;
        }
        else if((i>=DRV_HASH_85BIT_KEY_LENGTH*3-1) && (i<(DRV_HASH_85BIT_KEY_LENGTH*4-1-1)))
        {
            temp_array[3][i-DRV_HASH_85BIT_KEY_LENGTH*3+1+1] = p_src_key[i];
        }
        else if(i == (DRV_HASH_85BIT_KEY_LENGTH*4-1-1))
        {
            temp_array[3][DRV_HASH_85BIT_KEY_LENGTH] |= p_src_key[i];
        }
    }

    for (entry_index = 0; entry_index < entry_num; entry_index++)
    {
        _array_un_shift_left((uint8*)p_dst_key+(DRV_HASH_85BIT_KEY_LENGTH * entry_index),temp_array[entry_index],
                            3*entry_index,DRV_HASH_85BIT_KEY_LENGTH+1);
    }


    return DRV_E_NONE;
}
STATIC uint32
_get_convert_key( uint8* dest_key, uint8* src_key, uint8* key_mask,uint32 len, uint32 tbl_id)
{
    uint8 converted_key[MAX_ENTRY_BYTE] = {0};
    uint8 combined_key[MAX_ENTRY_BYTE] = {0};

    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);
    _drv_goldengate_hash_lookup_convert_xbits_key(tbl_id, src_key, converted_key, len);

    if((len % DRV_HASH_85BIT_KEY_LENGTH) == 0)
    {
        _get_combined_key(combined_key,converted_key,len, tbl_id);
    }
    else if((len % DRV_HASH_170BIT_KEY_LENGTH) == 0)
    {
        sal_memcpy(combined_key,converted_key,len);
    }

    _get_masked_key(dest_key, combined_key,key_mask, len);

    return DRV_E_NONE;
}

STATIC uint32
_get_key_all_level_depths(uint8 chip_id,hash_module_t hash_module,uint32 hash_type, level_lookup_result_t *level_result)
{
    uint32 all_level_depths = 0;
    uint8 max_level = 0;
    int i = 0;
    int32 cmd = 0;
    int32 hash_size_mode = 1;

    switch (hash_module)
    {
        case HASH_MODULE_EGRESS_XC:
            cmd = DRV_IOR(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashSizeMode_f);
            break;

        case HASH_MODULE_FIB_HOST0:
            cmd = DRV_IOR(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0HashSizeMode_f);
            break;

        case HASH_MODULE_FIB_HOST1:
            if (hash_type == FIBHOST1HASHTYPE_IPV4NATDAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV6NATDAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV4MCAST
                || hash_type == FIBHOST1HASHTYPE_IPV6MCAST
                || hash_type == FIBHOST1HASHTYPE_MACIPV4MCAST
                || hash_type == FIBHOST1HASHTYPE_TRILLMCASTVLAN)
            {

                cmd = DRV_IOR(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaHashSizeMode_f);
            }
            else if (hash_type == FIBHOST1HASHTYPE_IPV4NATSAPORT
                || hash_type == FIBHOST1HASHTYPE_IPV6NATSAPORT
                || hash_type == FIBHOST1HASHTYPE_TRILLMCASTVLAN )
            {

                cmd = DRV_IOR(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaHashSizeMode_f);
            }
            break;

        case HASH_MODULE_FLOW:
            cmd = DRV_IOR(FlowHashLookupCtl_t, FlowHashLookupCtl_flowHashSizeMode_f);
            break;

        case HASH_MODULE_IPFIX:
            cmd = DRV_IOR(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixHashSizeMode_f);
            break;

        case HASH_MODULE_USERID:
            cmd = DRV_IOR(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdHashSizeMode_f);
            break;

        default:
            break;
    }

    DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, 0, cmd, &hash_size_mode));

    max_level = hash_db[hash_module].level_num;
    for (i = 0; i <  max_level; i++)
    {
        if(level_result[i].level_en)
        {
            if(hash_module == HASH_MODULE_FLOW
            || hash_module == HASH_MODULE_IPFIX
            || hash_module == HASH_MODULE_EGRESS_XC)
            {
                all_level_depths += hash_db[hash_module].p_level[i].depth * 2
                                    * hash_db[hash_module].p_level[i].bucket_num
                                    << hash_size_mode;
            }
            else
            {
                all_level_depths += hash_db[hash_module].p_level[i].depth
                                    * hash_db[hash_module].p_level[i].bucket_num
                                    << hash_size_mode;
            }

        }

    }

    return all_level_depths;
}

int32
drv_goldengate_hash_lookup_get_key_info_index(hash_module_t hash_module, uint32 hash_type,uint8 * p_index)
{
    uint8 i = 0;

    if (hash_module < HASH_MODULE_NUM)
    {
        for (i = 0;  i < hash_db[hash_module].key_num; i++)
        {
            if ( hash_db[hash_module].key_mask_ad[i].hash_type == hash_type)
            {
                break;
            }
        }
    }

    if ((hash_module >= HASH_MODULE_NUM)||(i == hash_db[hash_module].key_num))
    {
        MODEL_HASH_DEBUG_INFO("Invalid FIB key type.\n");
        return DRV_E_INVALID_HASH_TYPE;
    }

    *p_index = i;

    return DRV_E_NONE;

}



STATIC uint8
_drv_goldengate_hash_lookup_get_key_entry_free(uint8* data_entry,uint32 unit_words,uint32 align_units, hash_free_type_t hash_free_type)
{
    uint8 free = 0;
    uint8 valid0 = 0;
    uint8 valid1 = 0;
    uint8 valid2 = 0;
    uint8 valid3 = 0;
    #define VALID_BIT 0

    if(hash_free_type == HASH_FREE_TYPE_NORMAL)
    {
        if (1 == align_units)
        {
            valid0 = IS_BIT_SET(data_entry[0],VALID_BIT);
            free = !valid0;
        }
        else if (2 == align_units)
        {
            valid0 = IS_BIT_SET(data_entry[0],VALID_BIT);
            valid1 = IS_BIT_SET(data_entry[unit_words*4 + 0], VALID_BIT);
            free = (!valid0)&&(!valid1);
        }
        else if (4 == align_units)
        {
            valid0 = IS_BIT_SET(data_entry[0],VALID_BIT);
            valid1 = IS_BIT_SET(data_entry[unit_words*4 + 0], VALID_BIT);
            valid2 = IS_BIT_SET(data_entry[unit_words*4*2 + 0], VALID_BIT);
            valid3 = IS_BIT_SET(data_entry[unit_words*4*3 + 0], VALID_BIT);
            free = (!valid0)&&(!valid1)&&(!valid2)&&(!valid3);
        }

    }
    else if (hash_free_type == HASH_FREE_TYPE_EGRESSXCOAM)
    {
        if (2 == align_units)
        {
            valid0 = IS_BIT_SET(data_entry[0],VALID_BIT);
            free = !valid0;
        }
        else
        {
            MODEL_HASH_DEBUG_INFO("EgressXcOam Hash align_units should only be 2, not %d!\n", align_units);
        }
    }
    else if (hash_free_type == HASH_FREE_TYPE_FLOW)
    {
        if (2 == align_units)
        {
            valid0 = ((data_entry[0]&0x7) != 0);
            free = (!valid0);
        }
        else if (4 == align_units)
        {
            valid0 = ((data_entry[0]&0x7) != 0);
            valid1 = ((data_entry[unit_words*4*2 + 0] &0x7) != 0);
            free = (!valid0)&&(!valid1);
        }
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("Hash free type is invalid!\n");
        return DRV_E_NONE;
    }

    return free;

}

STATIC bool
_drv_goldengate_hash_lookup_key_match(uint8* src, uint8* dst, uint8* mask, uint16 byte_num)
{
    uint8 i;

    for (i = 0; i < byte_num; i++)
    {
        if ((src[i] & mask[i]) != (dst[i] & mask[i]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

uint32
_drv_goldengate_hash_calculate_index(key_lookup_info_t * key_info, key_lookup_result_t * key_result)
{
    uint32 seed = 0xffffffff;
    uint8 * data = key_info->key_data;
    uint32 data_length = key_info->crc_bits;
    uint32 poly = 0 ;
    uint32 poly_len = 0;
    uint32 bucket_index = 0;
    if (HASH_CRC == key_info->type)
    {
        poly = key_info->polynomial;
        poly_len = key_info->poly_len;
        bucket_index = hash_crc(seed,data,data_length,poly,poly_len);
    }
    else
    {
        bucket_index = hash_xor16((uint16*) data, data_length);
    }

    key_result->bucket_index = bucket_index;
    return DRV_E_NONE;
}

STATIC uint32
_drv_goldengate_hash_model_assign(level_lookup_info_t * p_level_lookup_info,
                             level_lookup_result_t * p_level_result,
                             uint32 hitIndex)
{
    int32 cmd = 0;
    if ( (p_level_lookup_info->hash_module == HASH_MODULE_FIB_HOST0) &&
          (p_level_lookup_info->hash_type == FIBHOST0HASHTYPE_IPV4) )
    {
        cmd = DRV_IOR(DsFibHost0Ipv4HashKey_t, DsFibHost0Ipv4HashKey_pointer_f);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex,
            cmd, &(p_level_result->pointer)));
    }
    else if ( (p_level_lookup_info->hash_module == HASH_MODULE_FIB_HOST0) &&
          (p_level_lookup_info->hash_type == FIBHOST0HASHTYPE_IPV6MCAST) )
    {
        cmd = DRV_IOR(DsFibHost0Ipv6McastHashKey_t, DsFibHost0Ipv6McastHashKey_pointer_f);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex,
            cmd, &(p_level_result->pointer)));
    }
    else if ( (p_level_lookup_info->hash_module == HASH_MODULE_FIB_HOST0) &&
          (p_level_lookup_info->hash_type == FIBHOST0HASHTYPE_MACIPV6MCAST) )
    {
        cmd = DRV_IOR(DsFibHost0MacIpv6McastHashKey_t, DsFibHost0MacIpv6McastHashKey_pointer_f);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex,
            cmd, &(p_level_result->pointer)));
    }
    else if ( (p_level_lookup_info->hash_module == HASH_MODULE_FIB_HOST0) &&
          (p_level_lookup_info->hash_type == FIBHOST0HASHTYPE_MAC) )
    {
        cmd = DRV_IOR(DsFibHost0MacHashKey_t, DsFibHost0MacHashKey_pending_f);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex,
            cmd, &(p_level_result->pending)));
    }
    return DRV_E_NONE;
}
STATIC uint32
_drv_goldengate_hash_level_lookup(level_lookup_info_t * p_level_lookup_info,
                             level_lookup_result_t * p_level_result)
{
    key_lookup_info_t key_lookup_info;
    key_lookup_result_t key_result;
    uint32 bucket_num;
    uint32 bucket_ptr;
    uint8 data_entry[MAX_ENTRY_BYTE] = {0};
    uint8 DsHashKey0[MAX_ENTRY_BYTE] = {0};
    uint8 DsHashKey1[MAX_ENTRY_BYTE] = {0};
    uint8 DsHashKey2[MAX_ENTRY_BYTE] = {0};
    uint8 DsHashKey3[MAX_ENTRY_BYTE] = {0};
    uint8 * hashkey0 = p_level_lookup_info->key_data;
    uint8 mask0[MAX_ENTRY_BYTE] = {0};
    uint8 DsHashKey0_valid = 0;
    uint8 DsHashKey1_valid = 0;
    uint8 DsHashKey2_valid = 0;
    uint8 DsHashKey3_valid = 0;
    uint8 key[MAX_ENTRY_BYTE] = {0};
    uint32 cmd1 = 0;
    uint32 cmd2 = 0;
    tbls_id_t table_id = 0;
    fld_id_t ad_index_field_id = NO_DS_AD_INDEX;
    uint32 hit_index = 0;
    uint8 freeHit0 = 0;
    uint8 freeHit1 = 0;
    uint8 freeHit2 = 0;
    uint8 freeHit3 = 0;
    uint8 keyLengthMode = 0;
    uint32 dsAdIndex0 = 0;
    uint8 key_bytes = BIT2BYTE(p_level_lookup_info->key_bits/p_level_lookup_info->align_unit)
                        *p_level_lookup_info->align_unit ;
    uint8 unit_words = BYTE2WORD(key_bytes/p_level_lookup_info->align_unit);
    uint8 hit0 = 0;
    uint32 hitIndex0 = 0, aging_index = 0;
    hash_free_type_t free_type = HASH_FREE_TYPE_NUM;

    DRV_PTR_VALID_CHECK(p_level_lookup_info);
    DRV_PTR_VALID_CHECK(p_level_lookup_info->key_data);

    /* !!! different from spec for cmodel can not read disabled hash key */
    if (!p_level_result->level_en)
    {
        return DRV_E_NONE;
    }

    key_lookup_info.polynomial  = p_level_lookup_info->polynomial;
    key_lookup_info.poly_len = p_level_lookup_info->poly_len;
    key_lookup_info.key_bits = p_level_lookup_info->key_bits;
    key_lookup_info.type = p_level_lookup_info->type;
    key_lookup_info.key_data = key;
    sal_memcpy(key_lookup_info.key_data,p_level_lookup_info->key_data,
               key_bytes);
    key_lookup_info.crc_bits = gg_hash_key_length[p_level_lookup_info->hash_module];
     /*_get_combined_key(mask0,p_level_lookup_info->mask,key_bytes, p_level_lookup_info->key_table_id);*/

    sal_memcpy(mask0,p_level_lookup_info->mask, key_bytes);

    DRV_IF_ERROR_RETURN(_drv_goldengate_hash_calculate_index(&key_lookup_info, &key_result));
    MODEL_HASH_DEBUG_INFO("key_result.bucket_index:%x\n",key_result.bucket_index);

     /*bucket_ptr = key_result.bucket_index % p_level_lookup_info->depth;*/
    bucket_ptr = key_result.bucket_index;
    keyLengthMode = p_level_lookup_info->align_unit;
    if (p_level_lookup_info->hash_module == HASH_MODULE_FLOW
        || p_level_lookup_info->hash_module == HASH_MODULE_IPFIX)
    {
        bucket_num = p_level_lookup_info->bucket_num * 2;
        free_type = HASH_FREE_TYPE_FLOW;
    }
    else if (p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
    {
        free_type = HASH_FREE_TYPE_EGRESSXCOAM;
        bucket_num = p_level_lookup_info->bucket_num * 2;
    }
    else
    {
        free_type = HASH_FREE_TYPE_NORMAL;
        bucket_num = p_level_lookup_info->bucket_num;
    }

    table_id = p_level_lookup_info->key_table_id;
    ad_index_field_id = p_level_lookup_info->ad_index_field_id;

    cmd1 = DRV_IOR(table_id,DRV_ENTRY_FLAG);

    if ( keyLengthMode == 1 )  /* signle width*/
    {
        /* DsHashKey0 = DshashKeyBucket0*/
        hitIndex0 = p_level_lookup_info->key_index_base + bucket_ptr*bucket_num;
        aging_index = bucket_ptr*bucket_num;
        hit_index = hitIndex0;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey0, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey0_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        /* DsHashKey1 = DshashKeyBucket1*/
        hit_index += keyLengthMode;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey1, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey1_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        /* DsHashKey2 = DshashKeyBucket2*/
        hit_index += keyLengthMode;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey2, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey2_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        /* DsHashKey3 = DshashKeyBucket3*/
        hit_index += keyLengthMode;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey3, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey3_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey0, mask0, key_bytes))
        {
            hit0 = 1;
            hitIndex0 = hitIndex0 & 0xFFFFFFFC;
            aging_index = aging_index & 0xFFFFFFFC;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey1, mask0, key_bytes))
        {
            hit0 = 1;
            hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x1;
            aging_index = (aging_index & 0xFFFFFFFC) | 0x1;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey2, mask0, key_bytes))
        {
            hit0 = 1;
            hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
            aging_index = (aging_index & 0xFFFFFFFC) | 0x2;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey3, mask0, key_bytes))
        {
            hit0 = 1;
            hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x3;
            aging_index = (aging_index & 0xFFFFFFFC) | 0x3;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else
        {
            freeHit0 = DsHashKey0_valid;
            freeHit1 = DsHashKey1_valid;
            freeHit2 = DsHashKey2_valid;
            freeHit3 = DsHashKey3_valid;

            if ( DsHashKey0_valid )
            {
                hitIndex0 = hitIndex0 & 0xFFFFFFFC ;
            }
            else if ( DsHashKey1_valid )
            {
                hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x1;
            }
            else if ( DsHashKey2_valid )
            {
                hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
            }
            else if ( DsHashKey3_valid )
            {
                hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x3;
            }
            aging_index = aging_index;
        }
    }
    else if ( keyLengthMode == 2 )  /* double width*/
    {
        /* DsHashKey0 = DshashKeyBucket0*/
        hitIndex0 = p_level_lookup_info->key_index_base + bucket_ptr*bucket_num;
        hit_index = hitIndex0;
        aging_index = bucket_ptr*bucket_num;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey0, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey0_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        /* DsHashKey1 = DshashKeyBucket1*/
        if(p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
        {
            hit_index += keyLengthMode;
        }
        else
        {
            hit_index += keyLengthMode;
        }
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey1, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey1_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey0, mask0, key_bytes))
        {
            hit0 = 1;
            if(p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
            {
                hitIndex0 = hitIndex0 & 0xFFFFFFFC;
            }
            else
            {
                hitIndex0 = hitIndex0 & 0xFFFFFFFC;
            }
            aging_index = aging_index & 0xFFFFFFFC;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey1, mask0, key_bytes))
        {
            hit0 = 1;
            if(p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
            {
                hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
            }
            else
            {
                hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
            }
            aging_index = (aging_index & 0xFFFFFFFC) | 0x2;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else
        {
            freeHit0 = DsHashKey0_valid;
            freeHit1 = DsHashKey1_valid;

            if ( DsHashKey0_valid )
            {
                if(p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
                {
                    hitIndex0 = hitIndex0 & 0xFFFFFFFC;
                }
                else
                {
                    hitIndex0 = hitIndex0 & 0xFFFFFFFC;
                }
                aging_index = (aging_index & 0xFFFFFFFC);
            }
            else if ( DsHashKey1_valid )
            {
                if(p_level_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
                {
                    hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
                }
                else
                {
                    hitIndex0 = (hitIndex0 & 0xFFFFFFFC) | 0x2;
                }
                aging_index = (aging_index & 0xFFFFFFFC) | 0x2;
            }
        }
    }
    else if ( keyLengthMode == 4 )  /* quad width*/
    {
        /* DsHashKey0 = DshashKeyBucket0*/
        hitIndex0 = p_level_lookup_info->key_index_base + bucket_ptr*bucket_num;
        hit_index = hitIndex0;
        aging_index = bucket_ptr*bucket_num;
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hit_index, cmd1, data_entry));
        _get_convert_key(DsHashKey0, data_entry, p_level_lookup_info->mask,key_bytes, table_id);
        DsHashKey0_valid = _drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,keyLengthMode,free_type);

        if (_drv_goldengate_hash_lookup_key_match(hashkey0, DsHashKey0, mask0, key_bytes))
        {
            hit0 = 1;
            hitIndex0 = hitIndex0 & 0xFFFFFFFC;
            if (ad_index_field_id != NO_DS_AD_INDEX)
            {
                cmd2 = DRV_IOR(table_id,ad_index_field_id);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_level_lookup_info->chip_id, hitIndex0, cmd2, &dsAdIndex0));
                p_level_result->ad_index  = dsAdIndex0;
            }
            _drv_goldengate_hash_model_assign(p_level_lookup_info,p_level_result,hitIndex0);
        }
        else
        {
            freeHit0 = DsHashKey0_valid;
            if ( DsHashKey0_valid )
            {
                hitIndex0 = hitIndex0 & 0xFFFFFFFC;
            }
        }
        aging_index = aging_index;
    }

    if ( hit0 )
    {
        p_level_result->lookup_result_valid = 1;
        p_level_result->key_index = hitIndex0;
        p_level_result->aging_index = aging_index;
    }
    else
    {
        p_level_result->free_num = freeHit0 + freeHit1 + freeHit2 + freeHit3;
        p_level_result->free_valid = (p_level_result->free_num != 0);
        p_level_result->key_index = hitIndex0;
        p_level_result->aging_index = aging_index;
    }

    if (ad_index_field_id == NO_DS_AD_INDEX)
    {
        p_level_result->ad_index = hitIndex0;
    }

    MODEL_HASH_DEBUG_INFO("hitIndex0 add bucket offset = 0x%x!\n", hitIndex0);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");
    MODEL_HASH_DEBUG_INFO("hitIndex0:%d, free_valid:%d, keyIndex:0x%x\n",hit0, p_level_result->free_valid, hitIndex0);

    return DRV_E_NONE;

}

STATIC int32
_drv_goldengate_hash_lookup_register_cam(level_lookup_info_t* p_lookup_info, level_lookup_result_t* p_lookup_result)
{
    uint32 cam_entry_index = 0;
    uint32 cmd = 0;
    tbls_id_t cam_table_id = MaxTblId_t;
    uint8 step = 0;
    uint8 data_entry[MAX_ENTRY_BYTE] = {0};
    uint8 key_match = 0;
    uint8 key_free = 0;
    uint32 free_index = 0xFFFFFFFF;
    uint32 cam_entry_size = 0;
    uint32 max_index = 0;
    uint32 key_bytes = BIT2BYTE(p_lookup_info->key_bits/p_lookup_info->align_unit)
                        *p_lookup_info->align_unit;
    uint32 key_words = BYTE2WORD(key_bytes/p_lookup_info->align_unit)*p_lookup_info->align_unit;
    uint32 units = 0;
    uint32 unit_bytes = 0;
    uint32 unit_words = 0;
    uint32 ad_index_field_id = p_lookup_info->ad_index_field_id;
    uint32 ad_index = 0;
    uint8 converted_data[MAX_ENTRY_BYTE] = {0};
    hash_free_type_t free_type = HASH_FREE_TYPE_NUM;

     /*step = (p_lookup_info->hash_module == HASH_MODULE_EGRESS_XC) ?*/
     /*    p_lookup_info->align_unit*2 : p_lookup_info->align_unit;*/
    step = p_lookup_info->align_unit;
    unit_bytes = BIT2BYTE(p_lookup_info->key_bits/step);
    unit_words = BYTE2WORD(unit_bytes);

    cam_table_id = p_lookup_info->cam_table_id;
    cam_entry_size = TABLE_ENTRY_SIZE(cam_table_id);
    max_index = (TABLE_MAX_INDEX(cam_table_id)*cam_entry_size)/(key_words*4);
    p_lookup_result->cam_depth = max_index;

    if (p_lookup_info->hash_module == HASH_MODULE_FLOW
        || p_lookup_info->hash_module == HASH_MODULE_IPFIX)
    {
        free_type = HASH_FREE_TYPE_FLOW;
    }
    else if(p_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
    {
        free_type = HASH_FREE_TYPE_EGRESSXCOAM;
    }
    else
    {
        free_type = HASH_FREE_TYPE_NORMAL;
    }

    cmd = DRV_IOR(cam_table_id, DRV_ENTRY_FLAG);

    for (cam_entry_index = 0; cam_entry_index < TABLE_MAX_INDEX(cam_table_id); cam_entry_index ++)
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, cam_entry_index, cmd, data_entry));

        for (units = 0; units < 4; units += step)
        {
            /*
            _drv_hash_lookup_convert_xbits_key(p_lookup_info->key_table_id ,
                (data_entry + unit_words*units*4),
                converted_data,
                key_bytes);*/

            _get_convert_key(converted_data, (data_entry + unit_words*units*4), p_lookup_info->mask,key_bytes, p_lookup_info->key_table_id);

            key_match = _drv_goldengate_hash_lookup_key_match(p_lookup_info->key_data,
                                                   converted_data,
                                                   p_lookup_info->mask,
                                                   key_bytes);
            key_free = _drv_goldengate_hash_lookup_get_key_entry_free((data_entry + unit_words*units*4),
                                                            unit_words, step,free_type);
            if (key_match)
            {
                p_lookup_result->lookup_result_valid = TRUE;
                p_lookup_result->key_index = cam_entry_index*4 + units + p_lookup_info->key_index_base;
                p_lookup_result->cam_index = cam_entry_index;
                p_lookup_result->cam_units = units/step;
                if (ad_index_field_id != NO_DS_AD_INDEX)
                {
                    DRV_IOR_FIELD(p_lookup_info->key_table_id, ad_index_field_id, &ad_index,
                        (uint32*)(data_entry+unit_words*units*4));
                    p_lookup_result->ad_index  = ad_index;
                }
                else
                {
                    p_lookup_result->ad_index = p_lookup_result->key_index;
                }

                return DRV_E_NONE;
            }
            else if (key_free && (free_index == 0xFFFFFFFF))
            {
                free_index = cam_entry_index*4 + units;
            }
        }

    }

    if (TABLE_MAX_INDEX(cam_table_id) == cam_entry_index)
    {
        if (0xFFFFFFFF != free_index)
        {
            p_lookup_result->free_num = 1;
            p_lookup_result->free_valid = 1;
            p_lookup_result->key_index = free_index + p_lookup_info->key_index_base;
            p_lookup_result->cam_index = free_index/4;
            p_lookup_result->cam_units = free_index%4;
            p_lookup_result->aging_index = free_index;

        }
        else
        {
            p_lookup_result->free_num = 0;
            p_lookup_result->free_valid = 0;
        }

        p_lookup_result->lookup_result_valid = FALSE;
    }

    return DRV_E_NONE;
}

STATIC uint32
_drv_goldengate_hash_get_level_en(hash_lookup_info_t* p_lookup_info,
                             level_lookup_result_t * p_level_result0,
                             level_lookup_result_t * p_level_result1,
                             level_lookup_result_t * p_level_result2,
                             level_lookup_result_t * p_level_result3,
                             level_lookup_result_t * p_level_result4,
                             level_lookup_result_t * cam_result)
{
    uint32 cmd = 0;
    uint8 is_sa_lookup = _drv_is_host1_sa_lookup(p_lookup_info->hash_module,p_lookup_info->hash_type);
    if (p_lookup_info->hash_module == HASH_MODULE_FIB_HOST0)
    {
        uint32 HshLookupCtl[FIB_HOST0_HASH_LOOKUP_CTL_BYTES/4] = {0};
        cmd = DRV_IOR(FibHost0HashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level0HashEn_f,
            &(p_level_result0->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level1HashEn_f,
            &(p_level_result1->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level2HashEn_f,
            &(p_level_result2->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level3HashEn_f,
            &(p_level_result3->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FibHost0HashLookupCtl_t, FibHost0HashLookupCtl_fibHost0Level4HashEn_f,
            &(p_level_result4->level_en), HshLookupCtl);

        p_level_result0->aging_base = 0;
        p_level_result1->aging_base = AGING_BLOCK_SIZE_64K;
        p_level_result2->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K;
        p_level_result3->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K
            + AGING_BLOCK_SIZE_64K;
        p_level_result4->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K
            + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K;

        if ((p_lookup_info->hash_type == FIBHOST0HASHTYPE_IPV4) ||
            (p_lookup_info->hash_type == FIBHOST0HASHTYPE_IPV6MCAST) ||
            (p_lookup_info->hash_type == FIBHOST0HASHTYPE_MACIPV6MCAST))
        {
            p_level_result0->have_extra = 1;
            p_level_result1->have_extra = 1;
            p_level_result2->have_extra = 1;
            p_level_result3->have_extra = 1;
            p_level_result4->have_extra = 1;
            cam_result->have_extra      = 1;
        }
    }
    else if (p_lookup_info->hash_module == HASH_MODULE_FIB_HOST1)
    {
        uint32 HshLookupCtl[FIB_HOST1_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 size_mode = 0;
        uint32 fib_host1_aging_index_offset = 32 + (64+64+64+64+16)*1024+32;
        uint32 level0_max_index = 0;
        cmd = DRV_IOR(FibHost1HashLookupCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));

        DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaHashSizeMode_f,
                          &size_mode, HshLookupCtl);

        level0_max_index = p_level_result0->level_en * (size_mode ? (32 * 1024) : (16 * 1024));
        if ( is_sa_lookup )
        {
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel0HashEn_f,
                          &(p_level_result0->level_en), HshLookupCtl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel1HashEn_f,
                          &(p_level_result1->level_en), HshLookupCtl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1SaLevel2HashEn_f,
                          &(p_level_result2->level_en), HshLookupCtl);
        }
        else
        {
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel0HashEn_f,
                          &(p_level_result0->level_en), HshLookupCtl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel1HashEn_f,
                          &(p_level_result1->level_en), HshLookupCtl);
            DRV_IOR_FIELD(FibHost1HashLookupCtl_t, FibHost1HashLookupCtl_fibHost1DaLevel2HashEn_f,
                          &(p_level_result2->level_en), HshLookupCtl);
        }
        p_level_result0->aging_base = fib_host1_aging_index_offset;
        p_level_result1->aging_base = fib_host1_aging_index_offset + level0_max_index;
        p_level_result2->aging_base = 32;  /* level-2 use SharedRam0*/
    }
    else if (p_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
    {
        uint32 HshLookupCtl[EGRESS_XC_OAM_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 HashSizeMode = 0;
        uint32 UseMemMode = 0;
        cmd = DRV_IOR(EgressXcOamHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));

        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashSizeMode_f,
            &HashSizeMode, HshLookupCtl);
        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashMode_f,
            &UseMemMode, HshLookupCtl);

        if(UseMemMode)
        {
            DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel0HashEn_f,
                &(p_level_result0->level_en), HshLookupCtl);
            DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel1HashEn_f,
                &(p_level_result1->level_en), HshLookupCtl);
        }
        else
        {
            DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel0HashEn_f,
                &(p_level_result0->level_en), HshLookupCtl);
            DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamLevel1HashEn_f,
                &(p_level_result2->level_en), HshLookupCtl);
        }

        p_level_result0->aging_base = 0x2000 << HashSizeMode;
        p_level_result1->aging_base = 0x4000 << HashSizeMode;
    }
    else if (p_lookup_info->hash_module == HASH_MODULE_FLOW)
    {
        uint32 HshLookupCtl[FLOW_HASH_LOOKUP_CTL_BYTES/4] = {0};
        cmd = DRV_IOR(FlowHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel0HashEn_f,
            &(p_level_result0->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel1HashEn_f,
            &(p_level_result1->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel2HashEn_f,
            &(p_level_result2->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel3HashEn_f,
            &(p_level_result3->level_en), HshLookupCtl);
        DRV_IOR_FIELD(FlowHashLookupCtl_t, FlowHashLookupCtl_flowLevel4HashEn_f,
            &(p_level_result4->level_en), HshLookupCtl);


        p_level_result0->aging_base = 0;
        p_level_result1->aging_base = AGING_BLOCK_SIZE_64K;
        p_level_result2->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K;
        p_level_result3->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K;
        p_level_result4->aging_base = AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_64K;
    }
    else if (p_lookup_info->hash_module == HASH_MODULE_USERID)
    {
        uint32 HshLookupCtl[USER_ID_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 HashSizeMode = 0;
        uint32 UseMemMode = 0;
        cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));

        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdRamUsageMode_f,
            &UseMemMode, HshLookupCtl);
        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdHashSizeMode_f,
            &HashSizeMode, HshLookupCtl);

        if(UseMemMode)
        {
            DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel0HashEn_f,
                &(p_level_result2->level_en), HshLookupCtl);
            DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel1HashEn_f,
                &(p_level_result3->level_en), HshLookupCtl);
        }
        else
        {
            DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel0HashEn_f,
                &(p_level_result0->level_en), HshLookupCtl);
            DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdLevel1HashEn_f,
                &(p_level_result1->level_en), HshLookupCtl);
        }

        p_level_result1->aging_base = 0x2000 << HashSizeMode;
        p_level_result2->aging_base = 0x4000 << HashSizeMode;
    }
    else if (p_lookup_info->hash_module == HASH_MODULE_IPFIX)
    {
        uint32 HshLookupCtl[IPFIX_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 HashSizeMode = 0;
        cmd = DRV_IOR(IpfixHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl));

        DRV_IOR_FIELD(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixHashSizeMode_f,
            &HashSizeMode, HshLookupCtl);

        DRV_IOR_FIELD(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixHashEn_f,
                &(p_level_result0->level_en), HshLookupCtl);
        DRV_IOR_FIELD(IpfixHashLookupCtl_t, IpfixHashLookupCtl_ipfixHashEn_f,
                &(p_level_result1->level_en), HshLookupCtl);
    }
    return DRV_E_NONE;
}

STATIC uint32
_drv_goldengate_hash_result_determine(hash_lookup_info_t* p_lookup_info,
                             level_lookup_result_t * p_level_result0,
                             level_lookup_result_t * p_level_result1,
                             level_lookup_result_t * p_level_result2,
                             level_lookup_result_t * p_level_result3,
                             level_lookup_result_t * p_level_result4,
                             level_lookup_result_t * p_cam_result,
                             level_lookup_result_t * p_blackhole_cam_result,
                             hash_lookup_result_t* p_lookup_result)
{
    uint32 level0HashEn = p_level_result0->level_en;
    uint32 level1HashEn = p_level_result1->level_en;
    uint32 level2HashEn = p_level_result2->level_en;
    uint32 level3HashEn = p_level_result3->level_en;
    uint32 level4HashEn = p_level_result4->level_en;
    uint32 level0Base = p_level_result0->aging_base;
    uint32 level1Base = p_level_result1->aging_base;
    uint32 level2Base = p_level_result2->aging_base;
    uint32 level3Base = p_level_result3->aging_base;
    uint32 level4Base = p_level_result4->aging_base;
    uint32 CamDepth = p_cam_result->cam_depth;
    uint32 * extra = (uint32 *)p_lookup_result->extra;

    if (p_lookup_info->hash_module == HASH_MODULE_FLOW)
    {
        CamDepth = 32;
    }

    p_lookup_result->conflict = 0;
    if (p_blackhole_cam_result->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_blackhole_cam_result->lookup_result_valid;
        p_lookup_result->ad_index            = p_blackhole_cam_result->ad_index;
        p_lookup_result->is_blackhole        = 1;
    }
    else if (level0HashEn && p_level_result0->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_level_result0->lookup_result_valid;
        p_lookup_result->aging_index         = p_level_result0->aging_index + level0Base + CamDepth;
        p_lookup_result->key_index           = p_level_result0->key_index;
        p_lookup_result->ad_index            = p_level_result0->ad_index;
        p_lookup_result->pending             = p_level_result0->pending;
        if ( p_level_result0->have_extra )
        {
            *extra        = p_level_result0->pointer;
        }
    }
    else if (level1HashEn && p_level_result1->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_level_result1->lookup_result_valid;
        p_lookup_result->aging_index         = p_level_result1->aging_index + level1Base + CamDepth;
        p_lookup_result->key_index           = p_level_result1->key_index;
        p_lookup_result->ad_index            = p_level_result1->ad_index;
        p_lookup_result->pending             = p_level_result1->pending;
        if ( p_level_result1->have_extra )
        {
            *extra        = p_level_result1->pointer;
        }
    }
    else if (level2HashEn && p_level_result2->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_level_result2->lookup_result_valid;
        p_lookup_result->aging_index         = p_level_result2->aging_index + level2Base + CamDepth;
        p_lookup_result->key_index           = p_level_result2->key_index;
        p_lookup_result->ad_index            = p_level_result2->ad_index;
        p_lookup_result->pending             = p_level_result2->pending;
        if ( p_level_result2->have_extra )
        {
            *extra        = p_level_result2->pointer;
        }
    }
    else if (level3HashEn && p_level_result3->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_level_result3->lookup_result_valid;
        p_lookup_result->aging_index         = p_level_result3->aging_index + level3Base + CamDepth;
        p_lookup_result->key_index           = p_level_result3->key_index;
        p_lookup_result->ad_index            = p_level_result3->ad_index;
        p_lookup_result->pending             = p_level_result3->pending;
        if ( p_level_result3->have_extra )
        {
            *extra        = p_level_result3->pointer;
        }
    }
    else if (level4HashEn && p_level_result4->lookup_result_valid)
    {
        p_lookup_result->lookup_result_valid = p_level_result4->lookup_result_valid;
        p_lookup_result->aging_index         = p_level_result4->aging_index + level4Base + CamDepth;
        p_lookup_result->key_index           = p_level_result4->key_index;
        p_lookup_result->ad_index            = p_level_result4->ad_index;
        p_lookup_result->pending             = p_level_result4->pending;
        if ( p_level_result4->have_extra )
        {
            *extra        = p_level_result4->pointer;
        }
    }
    else if (p_cam_result->lookup_result_valid)
    {
        if (p_lookup_info->hash_module == HASH_MODULE_FLOW)
        {
            p_lookup_result->aging_index = p_cam_result->aging_index + level3Base + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_16K + CamDepth;
        }
        else
        {
            p_lookup_result->aging_index = p_cam_result->aging_index + level0Base;
        }
        p_lookup_result->lookup_result_valid = p_cam_result->lookup_result_valid;
        p_lookup_result->key_index           = p_cam_result->key_index;
        p_lookup_result->ad_index            = p_cam_result->ad_index;
        p_lookup_result->pending             = p_cam_result->pending;
        p_lookup_result->is_cam              = 1;
        if ( p_cam_result->have_extra )
        {
            *extra        = p_cam_result->pointer;
        }
    }
    else if (level0HashEn && (p_level_result0->free_num == 4) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result0->aging_index + level0Base + CamDepth;
        p_lookup_result->key_index           = p_level_result0->key_index;
        p_lookup_result->free                = p_level_result0->free_valid;
    }
    else if (level1HashEn && (p_level_result1->free_num == 4) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result1->aging_index + level1Base + CamDepth;
        p_lookup_result->key_index           = p_level_result1->key_index;
        p_lookup_result->free                = p_level_result1->free_valid;
    }
    else if (level2HashEn && (p_level_result2->free_num == 4) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result2->aging_index + level2Base + CamDepth;
        p_lookup_result->key_index           = p_level_result2->key_index;
        p_lookup_result->free                = p_level_result2->free_valid;
    }
    else if (level3HashEn && (p_level_result3->free_num == 4) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result3->aging_index + level3Base + CamDepth;
        p_lookup_result->key_index           = p_level_result3->key_index;
        p_lookup_result->free                = p_level_result3->free_valid;
    }
    else if (level4HashEn && (p_level_result4->free_num == 4) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result4->aging_index + level4Base + CamDepth;
        p_lookup_result->key_index           = p_level_result4->key_index;
        p_lookup_result->free                = p_level_result4->free_valid;
    }
    else if (level0HashEn && (p_level_result0->free_num == 3) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result0->aging_index + level0Base + CamDepth;
        p_lookup_result->key_index           = p_level_result0->key_index;
        p_lookup_result->free                = p_level_result0->free_valid;
    }
    else if (level1HashEn && (p_level_result1->free_num == 3) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result1->aging_index + level1Base + CamDepth;
        p_lookup_result->key_index           = p_level_result1->key_index;
        p_lookup_result->free                = p_level_result1->free_valid;
    }
    else if (level2HashEn && (p_level_result2->free_num == 3) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result2->aging_index + level2Base + CamDepth;
        p_lookup_result->key_index           = p_level_result2->key_index;
        p_lookup_result->free                = p_level_result2->free_valid;
    }
    else if (level3HashEn && (p_level_result3->free_num == 3) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result3->aging_index + level3Base + CamDepth;
        p_lookup_result->key_index           = p_level_result3->key_index;
        p_lookup_result->free                = p_level_result3->free_valid;
    }
    else if (level4HashEn && (p_level_result4->free_num == 3) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result4->aging_index + level4Base + CamDepth;
        p_lookup_result->key_index           = p_level_result4->key_index;
        p_lookup_result->free                = p_level_result4->free_valid;
    }
    else if (level0HashEn && (p_level_result0->free_num == 2) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result0->aging_index + level0Base + CamDepth;
        p_lookup_result->key_index           = p_level_result0->key_index;
        p_lookup_result->free                = p_level_result0->free_valid;
    }
    else if (level1HashEn && (p_level_result1->free_num == 2) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result1->aging_index + level1Base + CamDepth;
        p_lookup_result->key_index           = p_level_result1->key_index;
        p_lookup_result->free                = p_level_result1->free_valid;
    }
    else if (level2HashEn && (p_level_result2->free_num == 2) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result2->aging_index + level2Base + CamDepth;
        p_lookup_result->key_index           = p_level_result2->key_index;
        p_lookup_result->free                = p_level_result2->free_valid;
    }
    else if (level3HashEn && (p_level_result3->free_num == 2) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result3->aging_index + level3Base + CamDepth;
        p_lookup_result->key_index           = p_level_result3->key_index;
        p_lookup_result->free                = p_level_result3->free_valid;
    }
    else if (level4HashEn && (p_level_result4->free_num == 2) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result4->aging_index + level4Base + CamDepth;
        p_lookup_result->key_index           = p_level_result4->key_index;
        p_lookup_result->free                = p_level_result4->free_valid;
    }
    else if (level0HashEn && (p_level_result0->free_num == 1) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result0->aging_index + level0Base + CamDepth;
        p_lookup_result->key_index           = p_level_result0->key_index;
        p_lookup_result->free                = p_level_result0->free_valid;
    }
    else if (level1HashEn && (p_level_result1->free_num == 1) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result1->aging_index + level1Base + CamDepth;
        p_lookup_result->key_index           = p_level_result1->key_index;
        p_lookup_result->free                = p_level_result1->free_valid;
    }
    else if (level2HashEn && (p_level_result2->free_num == 1) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result2->aging_index + level2Base + CamDepth;
        p_lookup_result->key_index           = p_level_result2->key_index;
        p_lookup_result->free                = p_level_result2->free_valid;
    }
    else if (level3HashEn && (p_level_result3->free_num == 1) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result3->aging_index + level3Base + CamDepth;
        p_lookup_result->key_index           = p_level_result3->key_index;
        p_lookup_result->free                = p_level_result3->free_valid;
    }
    else if (level4HashEn && (p_level_result4->free_num == 1) )
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->aging_index         = p_level_result4->aging_index + level4Base + CamDepth;
        p_lookup_result->key_index           = p_level_result4->key_index;
        p_lookup_result->free                = p_level_result4->free_valid;
    }
    else if (p_cam_result->free_num == 1)
    {
        if (p_lookup_info->hash_module == HASH_MODULE_FLOW)
        {
            p_lookup_result->aging_index = p_cam_result->aging_index + level3Base + AGING_BLOCK_SIZE_64K + AGING_BLOCK_SIZE_16K;
        }
        else
        {
            p_lookup_result->aging_index = p_cam_result->aging_index + level0Base;
        }
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->key_index           = p_cam_result->key_index;
        p_lookup_result->free                = p_cam_result->free_valid;
        p_lookup_result->cam_index           = p_cam_result->cam_index;
        p_lookup_result->cam_units           = p_cam_result->cam_units;
        p_lookup_result->is_cam              = 1;
    }
    else
    {
        p_lookup_result->lookup_result_valid = 0;
        p_lookup_result->free                = 0;
        p_lookup_result->conflict            = 1;
    }

    MODEL_HASH_DEBUG_INFO("@@ Show lookup result: @@\n");
    MODEL_HASH_DEBUG_INFO("is_conflict = %d\n", p_lookup_result->conflict);
    MODEL_HASH_DEBUG_INFO("key_index = 0x%x!\n", p_lookup_result->key_index);
    MODEL_HASH_DEBUG_INFO("valid = 0x%x!\n", p_lookup_result->lookup_result_valid);
    MODEL_HASH_DEBUG_INFO("aging_index = 0x%x!\n", p_lookup_result->aging_index);
    MODEL_HASH_DEBUG_INFO("ad_index = 0x%x!\n", p_lookup_result->ad_index);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");
    return DRV_E_NONE;
}

int32
_drv_goldengate_hash_lookup_blackhole_cam(level_lookup_info_t * level_info,level_lookup_result_t * blackhole_cam_result)
{
    uint8 idx = 0;
    uint16 byte_num = MAC_BLACKHOLE_HASH_KEY_BYTE;
    uint8 mask[MAC_BLACKHOLE_HASH_KEY_BYTE] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint32 cmd = DRV_IOR(DsFibMacBlackHoleHashKey_t,DRV_ENTRY_FLAG);
    uint32 entry[MAX_ENTRY_BYTE] = {0};
    uint32 data[2] = {0};
    uint8 data_entry[MAC_BLACKHOLE_HASH_KEY_BYTE] = {0};
    uint8 key_entry[MAC_BLACKHOLE_HASH_KEY_BYTE] = {0};
    uint32 valid;
    uint8 i = 0;
    uint32 mac[2]= {0};

    DRV_IOR_FIELD(DsFibHost0MacHashKey_t, DsFibHost0MacHashKey_mappedMac_f, mac, level_info->key_data);

    blackhole_cam_result->lookup_result_valid = 0;
    for ( idx = 0; idx < MAC_BLACKHOLE_HASHKEY_NUM; idx++)
    {
        DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(level_info->chip_id, idx, cmd, entry));
        DRV_IOR_FIELD(DsFibMacBlackHoleHashKey_t, DsFibMacBlackHoleHashKey_mappedMac_f,
            data, entry);
        DRV_IOR_FIELD(DsFibMacBlackHoleHashKey_t, DsFibMacBlackHoleHashKey_valid_f,
                &valid, entry);

        if ( valid )
        {
            for (i = 0; i < MAC_BLACKHOLE_HASH_KEY_BYTE; i++)
            {
                data_entry[i] = (data[i/4] >> ((i%4) * 8) ) & 0xFFFF;
                key_entry[i]  = (mac[i/4] >> ((i%4) * 8) ) & 0xFFFF;
            }
            if ( _drv_goldengate_hash_lookup_key_match(key_entry, data_entry, mask, byte_num) )
            {
                DRV_IOR_FIELD(DsFibMacBlackHoleHashKey_t, DsFibMacBlackHoleHashKey_dsAdIndex_f,
                              &(blackhole_cam_result->ad_index), entry);
                blackhole_cam_result->lookup_result_valid = 1;
                break;
            }
        }
    }
    return 0;
}

STATIC uint32
_drv_goldengate_hash_get_hash_key_type_by_tbl(hash_module_t hash_module, uint32 key_tbl, uint32 * hash_type)
{
    uint8 i = 0;

    if (hash_module < HASH_MODULE_NUM)
    {
        for (i = 0;  i < hash_db[hash_module].key_num; i++)
        {
            if ( hash_db[hash_module].key_mask_ad[i].key_table_id == key_tbl)
            {
                *hash_type = hash_db[hash_module].key_mask_ad[i].hash_type;
                break;
            }
        }
    }

    if ((hash_module >= HASH_MODULE_NUM)||(i == hash_db[hash_module].key_num))
    {
        MODEL_HASH_DEBUG_INFO("Invalid FIB key type.\n");
        return DRV_E_INVALID_HASH_TYPE;
    }

    return DRV_E_NONE;

}
int32
drv_goldengate_model_hash_lookup(hash_lookup_info_t* p_lookup_info, hash_lookup_result_t* p_lookup_result)
{
    uint8 level = 0;
    uint32 level_num = 0;
    hash_module_t hash_module;
    uint32 hash_type = 0;
    uint8 hash_index = 0;
    level_lookup_info_t level_info;
    level_lookup_result_t* p_level_result = NULL;
    level_lookup_result_t cam_result;
    level_lookup_result_t blackhole_cam_result;
    uint32 key_bytes = 0;
    uint32 key_bits = 0;
     uint8 key[MAX_ENTRY_BYTE] = {0};
    uint8 mask[MAX_ENTRY_BYTE] = {0};
    uint32 crc_index[MAX_LEVEL_NUM]= {0};
    tbls_id_t key_table_id = MaxTblId_t;
    uint32 HashMode = 0;
    uint8 level_t = 0;
    uint8 start_level = 0;
    uint32 cam_tbl_id = 0;
    int32 ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(p_lookup_info);
    DRV_PTR_VALID_CHECK(p_lookup_info->p_ds_key);
    hash_module = p_lookup_info->hash_module;
    hash_type = p_lookup_info->hash_type;

    DRV_IF_ERROR_RETURN(drv_goldengate_hash_lookup_get_key_info_index(hash_module, hash_type, &hash_index));
    level_num = hash_db[hash_module].level_num;
    key_table_id = hash_db[hash_module].key_mask_ad[hash_index].key_table_id;

    _get_hash_lookup_ctl (hash_module, crc_index);

    key_bits = hash_db[hash_module].key_mask_ad[hash_index].bits_per_unit *
               hash_db[hash_module].key_mask_ad[hash_index].align_unit;
    key_bytes = BIT2BYTE(hash_db[hash_module].key_mask_ad[hash_index].bits_per_unit)
                *hash_db[hash_module].key_mask_ad[hash_index].align_unit;

    sal_memcpy(mask,hash_db[hash_module].key_mask_ad[hash_index].mask,key_bytes);
    _get_convert_key(key, p_lookup_info->p_ds_key, mask,key_bytes, key_table_id);

    p_level_result = (level_lookup_result_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    if (NULL == p_level_result)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_level_result,0,sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    sal_memset(&cam_result,0,sizeof(cam_result));
    sal_memset(&blackhole_cam_result,0,sizeof(blackhole_cam_result));
    _drv_goldengate_hash_get_level_en(p_lookup_info,
                             &(p_level_result[0]),
                             &(p_level_result[1]),
                             &(p_level_result[2]),
                             &(p_level_result[3]),
                             &(p_level_result[4]),
                             &cam_result);

    /* Egress XcOam need addition process */
    if (p_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
    {
        uint32 HshLookupCtl[EGRESS_XC_OAM_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 cmd = 0;
        cmd = DRV_IOR(EgressXcOamHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_GOTO(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl), ret, out);
        DRV_IOR_FIELD(EgressXcOamHashLookupCtl_t, EgressXcOamHashLookupCtl_egressXcOamHashMode_f,
            &HashMode, HshLookupCtl);
    }

    if (p_lookup_info->hash_module == HASH_MODULE_USERID)
    {
        uint32 HshLookupCtl[USER_ID_HASH_LOOKUP_CTL_BYTES/4] = {0};
        uint32 cmd = 0;
        cmd = DRV_IOR(UserIdHashLookupCtl_t, DRV_ENTRY_FLAG);

        DRV_IF_ERROR_GOTO(drv_goldengate_ioctl(p_lookup_info->chip_id, 0, cmd, HshLookupCtl), ret, out);
        DRV_IOR_FIELD(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdRamUsageMode_f,
            &HashMode, HshLookupCtl);

        if (HashMode == TRUE)
        {
            start_level = 2;
            level_num = 4;
        }
        else
        {
            level_num = 2;
        }
    }

    /*level lookup*/
    for (level = start_level;level < level_num ;level++ )
    {
        level_t = level;
        /* Egress XcOam need addition process !!!TBD!!! */
        if (p_lookup_info->hash_module == HASH_MODULE_EGRESS_XC)
        {
            if( ((level == 1) && (!HashMode) ) || ((level == 2) && HashMode ))
            {
                p_level_result[level].level_en = 0;
                continue;
            }
            else if ( (level == 2) && (!HashMode) )
            {
                level_t = 1;
            }
        }
        sal_memset(&level_info,0,sizeof(level_info));
        level_info.chip_id = p_lookup_info->chip_id;
        level_info.hash_module = hash_module;
        level_info.hash_type = hash_type;
        level_info.mask = mask;
        level_info.key_data = key;
        #if 0
        if (hash_module == HASH_MODULE_EGRESS_XC)
        {
            level_info.align_unit = hash_db[hash_module].key_mask_ad[hash_index].align_unit*2;
        }
        else
        #endif
        {
            level_info.align_unit = hash_db[hash_module].key_mask_ad[hash_index].align_unit;
        }
        level_info.key_bits = key_bits;
        _get_hash_poly(p_lookup_info->chip_id, hash_module, hash_type, level,
                    &level_info.polynomial, &level_info.poly_len);
         /*level_info.polynomial = hash_db[hash_module].p_crc[level].poly;*/
         /*level_info.poly_len = hash_db[hash_module].p_crc[level].poly_len;*/
        level_info.bucket_num = hash_db[hash_module].p_level[level].bucket_num;
        level_info.depth = hash_db[hash_module].p_level[level].depth;
        level_info.type = hash_db[hash_module].p_crc[level].type;
        level_info.key_index_base = _get_key_index_base(p_lookup_info->chip_id,hash_module, hash_type, level_t,p_level_result);
        level_info.key_table_id = key_table_id;
        level_info.ad_index_field_id = hash_db[hash_module].key_mask_ad[hash_index].ad_index_field_id;

        MODEL_HASH_DEBUG_INFO("@@ Show level lookup info: @@\n");
        MODEL_HASH_DEBUG_INFO("hash_module = %d\n", hash_module);
        MODEL_HASH_DEBUG_INFO("hash_type = %d\n", hash_type);
        MODEL_HASH_DEBUG_INFO("level_num = %d\n", level_num);
        MODEL_HASH_DEBUG_INFO("level = %d\n", level);

        _drv_goldengate_hash_level_lookup(&level_info, &(p_level_result[level]));
        sal_memcpy(&(p_lookup_result->level_result[level]), &p_level_result[level], sizeof(level_lookup_result_t));

    }

    cam_tbl_id = hash_db[hash_module].cam_table_id;

    /*cam lookup*/
    p_lookup_result->is_cam = FALSE;
    if (MaxTblId_t != cam_tbl_id)
    {
        sal_memset(&level_info,0,sizeof(level_info));
         /*_drv_hash_lookup_convert_xbits_key(key_table_id, p_lookup_info->p_ds_key, cam_key, key_bytes);*/
        level_info.chip_id = p_lookup_info->chip_id;
        level_info.mask = mask;
        level_info.key_data = key;
        level_info.key_table_id = key_table_id;
         /*if (hash_module == HASH_MODULE_EGRESS_XC)*/
         /*{*/
         /*    level_info.align_unit = hash_db[hash_module].key_mask_ad[hash_index].align_unit*2;*/
         /*}*/
         /*else*/
        {
            level_info.align_unit = hash_db[hash_module].key_mask_ad[hash_index].align_unit;
        }
        level_info.hash_module = hash_module;
        level_info.key_bits = key_bits;
        level_info.key_index_base = _get_key_all_level_depths(p_lookup_info->chip_id,hash_module,hash_type,p_level_result);
        level_info.cam_table_id = cam_tbl_id;
        level_info.ad_index_field_id = hash_db[hash_module].key_mask_ad[hash_index].ad_index_field_id;
        _drv_goldengate_hash_lookup_register_cam(&level_info,&cam_result);
        if (cam_result.lookup_result_valid)
        {
            p_lookup_result->cam_index = cam_result.cam_index;
            p_lookup_result->cam_units = cam_result.cam_units;
        }
    }

    if ((hash_module == HASH_MODULE_FIB_HOST0)
        && (hash_type == FIBHOST0HASHTYPE_MAC)
        && !p_lookup_info->is_write)
    {
        level_info.chip_id = p_lookup_info->chip_id;
        level_info.key_data = p_lookup_info->p_ds_key;
        _drv_goldengate_hash_lookup_convert_ds_endian(DsFibHost0MacHashKey_t, (void*)level_info.key_data);
        _drv_goldengate_hash_lookup_blackhole_cam(&level_info,&blackhole_cam_result);
    }

    p_lookup_result->ad_table_id = hash_db[hash_module].key_mask_ad[hash_index].ad_table_id;

    /*HASH RESULT DETERMINE*/
    _drv_goldengate_hash_result_determine(p_lookup_info,&(p_level_result[0]),&(p_level_result[1]),
                                &(p_level_result[2]),&(p_level_result[3]),&(p_level_result[4]),
                                &cam_result,&blackhole_cam_result,p_lookup_result);
out:
    mem_free(p_level_result);

    return ret;

}



int32
drv_goldengate_model_hash_key_add_by_key(hash_io_by_key_para_t* p_para)
{
    hash_lookup_info_t hash_lookup_info;
    hash_lookup_result_t hash_lookup_result;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint8 hash_index = 0;
    uint32 chip_id = p_para->chip_id;
    tbls_id_t cam_table_id = MaxTblId_t;
    uint8 key[MAX_ENTRY_BYTE] = {0};
    uint8 data_entry[MAX_ENTRY_BYTE] = {0};
    uint32 all_level_depths = 0;
    uint32 unit_bytes = 0;
    uint32 key_bytes = 0;
    uint32 unit_words = 0;
    uint32 extra_value = 0;
    uint32 key_index = 0;
    level_lookup_result_t* p_level_result = NULL;
    level_lookup_result_t cam_result;


    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_para->key);

    hash_lookup_info.is_write = 1;
    DRV_IF_ERROR_RETURN(drv_goldengate_hash_lookup_get_key_info_index(p_para->hash_module, p_para->hash_type, &hash_index));

    unit_bytes = BIT2BYTE(hash_db[p_para->hash_module].key_mask_ad[hash_index].bits_per_unit);
    key_bytes = BIT2BYTE(hash_db[p_para->hash_module].key_mask_ad[hash_index].align_unit
                                * hash_db[p_para->hash_module].key_mask_ad[hash_index].bits_per_unit);
    key_bytes = ((key_bytes%12 > 0?1:0)+key_bytes/12)*12;
    unit_words = BYTE2WORD(unit_bytes);

    tbl_id = hash_db[p_para->hash_module].key_mask_ad[hash_index].key_table_id;
     /*all_level_depths = _get_key_all_level_depths(chip_id,p_para->hash_module,p_para->hash_type);*/

    if (MaxTblId_t == tbl_id)
    {
        MODEL_HASH_DEBUG_INFO("Invalid FIB key type.\n");
        return DRV_E_INVALID_HASH_TYPE;
    }

    sal_memset(&hash_lookup_info, 0, sizeof(hash_lookup_info_t));
    sal_memset(&hash_lookup_result, 0, sizeof(hash_lookup_result_t));

    hash_lookup_info.chip_id = p_para->chip_id;
    hash_lookup_info.hash_module = p_para->hash_module;
    hash_lookup_info.hash_type = p_para->hash_type;
    hash_lookup_info.p_ds_key = key;
    hash_lookup_result.extra = &extra_value;

    sal_memcpy(hash_lookup_info.p_ds_key , p_para->key, key_bytes);

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_lookup(&hash_lookup_info, &hash_lookup_result));

    p_level_result = (level_lookup_result_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    if (NULL == p_level_result)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_level_result,0,sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    sal_memset(&cam_result,0,sizeof(cam_result));
    _drv_goldengate_hash_get_level_en(&hash_lookup_info,
                         &(p_level_result[0]),
                         &(p_level_result[1]),
                         &(p_level_result[2]),
                         &(p_level_result[3]),
                         &(p_level_result[4]),
                         &cam_result);
    all_level_depths = _get_key_all_level_depths(hash_lookup_info.chip_id,hash_lookup_info.hash_module,hash_lookup_info.hash_type,p_level_result);
    mem_free(p_level_result);

    MODEL_HASH_DEBUG_INFO("@@ Show lookup result: @@\n");
    if (hash_lookup_result.key_index >= all_level_depths )
    {
        key_index = hash_lookup_result.key_index - all_level_depths;
        MODEL_HASH_DEBUG_INFO("key is in cam!\n");
    }
    if (hash_lookup_result.is_blackhole)
    {
        MODEL_HASH_DEBUG_INFO("key is in black hole cam!\n");
    }
    MODEL_HASH_DEBUG_INFO("is_conflict = %d\n", hash_lookup_result.conflict);
    MODEL_HASH_DEBUG_INFO("key_index = 0x%x!\n", hash_lookup_result.key_index);
    MODEL_HASH_DEBUG_INFO("valid = 0x%x!\n", hash_lookup_result.lookup_result_valid);
    MODEL_HASH_DEBUG_INFO("ad_index = 0x%x!\n", hash_lookup_result.ad_index);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");

    p_para->is_conflict  = hash_lookup_result.conflict;
    p_para->is_valid     = hash_lookup_result.lookup_result_valid;
    p_para->is_cam       = hash_lookup_result.is_cam;
    p_para->is_blackhole = hash_lookup_result.is_blackhole;

    if (hash_lookup_result.lookup_result_valid && !hash_lookup_result.is_blackhole)
    {
        if (hash_lookup_result.key_index < all_level_depths )
        {
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, hash_lookup_result.key_index, cmd, p_para->key));
            MODEL_HASH_DEBUG_INFO("The hash key, table-id = 0x%X already exist, update ad index.\n", tbl_id);
        }
        else
        {
            key_index = hash_lookup_result.key_index - all_level_depths;
            cam_table_id = hash_db[p_para->hash_module].cam_table_id;

            cmd = DRV_IOR(cam_table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, hash_lookup_result.key_index/4, cmd, data_entry));

            sal_memcpy(data_entry + unit_words*4*(hash_lookup_result.key_index%4),p_para->key, key_bytes);

            cmd = DRV_IOW(cam_table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, hash_lookup_result.key_index/4,cmd, data_entry));

            MODEL_HASH_DEBUG_INFO("The cam key, table-id = 0x%X already exist, update ad index.\n", cam_table_id);
        }
    }
    else
    {
        if (hash_lookup_result.free)
        {
            if (hash_lookup_result.key_index < all_level_depths )
            {
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, hash_lookup_result.key_index, cmd, p_para->key));

                 /*kal_memset(&hash_lookup_result, 0, sizeof(hash_lookup_result));*/
                 /*DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_lookup(&hash_lookup_info,*/
                 /*                                               &hash_lookup_result));*/
                MODEL_HASH_DEBUG_INFO("Add hash key: table-id = 0x%X, key-index = 0x%X.\n",
                                       tbl_id, hash_lookup_result.key_index);
            }
            else
            {
                key_index = hash_lookup_result.key_index - all_level_depths;
                cam_table_id = hash_db[p_para->hash_module].cam_table_id;

                cmd = DRV_IOR(cam_table_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, key_index/4, cmd, data_entry));

                sal_memcpy(data_entry + unit_words*4*(key_index%4),p_para->key, key_bytes);

                cmd = DRV_IOW(cam_table_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, key_index/4,cmd, data_entry));

                 /*DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_lookup(&hash_lookup_info,*/
                 /*                                               &hash_lookup_result));*/
                MODEL_HASH_DEBUG_INFO("Add cam: cam_index = 0x%X, entry-index = 0x%X,ad-index = 0x%X.\n",
                                       key_index/4, key_index  , hash_lookup_result.ad_index);
            }
        }
        else
        {
            MODEL_HASH_DEBUG_INFO("Fail to add new hash key because of conflict.\n");
            return DRV_E_HASH_CONFLICT;
        }
    }

    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_key_delete_by_key(hash_io_by_key_para_t* p_para)
{
    hash_lookup_info_t hash_lookup_info;
    hash_lookup_result_t hash_lookup_result;
    uint32 cmd = 0;
    uint32 tbl_id = MaxTblId_t;
    uint8 key[MAX_ENTRY_BYTE] = {0};
    uint8 data_entry[MAX_ENTRY_BYTE] = {0};
    uint32 chip_id = p_para->chip_id;
    uint32 key_index = 0;
    tbls_id_t cam_table_id = MaxTblId_t;
    uint32 all_level_depths = 0;
    uint8 hash_index = 0;
    uint32 key_units = 0;
    uint32 unit_bytes = 0;
    uint32 key_bytes = 0;
    uint32 unit_words = 0;
    hash_free_type_t free_type = HASH_FREE_TYPE_NUM;
    uint32 pointer = 0;
    level_lookup_result_t* p_level_result = NULL;
    level_lookup_result_t cam_result;


    DRV_PTR_VALID_CHECK(p_para);
    DRV_PTR_VALID_CHECK(p_para->key);

    hash_lookup_info.is_write = 1;
    DRV_IF_ERROR_RETURN(drv_goldengate_hash_lookup_get_key_info_index(p_para->hash_module, p_para->hash_type, &hash_index));

    key_units = hash_db[p_para->hash_module].key_mask_ad[hash_index].align_unit;
    unit_bytes = BIT2BYTE(hash_db[p_para->hash_module].key_mask_ad[hash_index].bits_per_unit);
    key_bytes = BIT2BYTE(hash_db[p_para->hash_module].key_mask_ad[hash_index].align_unit
                       * hash_db[p_para->hash_module].key_mask_ad[hash_index].bits_per_unit);
    key_bytes = ((key_bytes%12 > 0?1:0)+key_bytes/12)*12;
    unit_words = BYTE2WORD(unit_bytes);

    tbl_id = hash_db[p_para->hash_module].key_mask_ad[hash_index].key_table_id;

    key_units = key_units;
     /*all_level_depths = _get_key_all_level_depths(chip_id,p_para->hash_module,p_para->hash_type);*/


    sal_memset(&hash_lookup_info, 0, sizeof(hash_lookup_info));
    sal_memset(&hash_lookup_result, 0, sizeof(hash_lookup_result));

    hash_lookup_info.chip_id = p_para->chip_id;
    hash_lookup_info.hash_module = p_para->hash_module;
    hash_lookup_info.hash_type = p_para->hash_type;
    hash_lookup_info.p_ds_key = key;
    hash_lookup_result.extra = &pointer;

    sal_memcpy(hash_lookup_info.p_ds_key, p_para->key, key_bytes);

    DRV_PTR_VALID_CHECK(drv_gg_io_api.drv_hash_lookup);
    DRV_IF_ERROR_RETURN(drv_gg_io_api.drv_hash_lookup(&hash_lookup_info,
                                                   &hash_lookup_result));

    p_level_result = (level_lookup_result_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    if (NULL == p_level_result)
    {
        return DRV_E_NO_MEMORY;
    }
    sal_memset(p_level_result,0,sizeof(level_lookup_result_t)*MAX_LEVEL_NUM);
    sal_memset(&cam_result,0,sizeof(cam_result));
    _drv_goldengate_hash_get_level_en(&hash_lookup_info,
                         &(p_level_result[0]),
                         &(p_level_result[1]),
                         &(p_level_result[2]),
                         &(p_level_result[3]),
                         &(p_level_result[4]),
                         &cam_result);
    all_level_depths = _get_key_all_level_depths(hash_lookup_info.chip_id,hash_lookup_info.hash_module,hash_lookup_info.hash_type, p_level_result);
    mem_free(p_level_result);

    key_index = hash_lookup_result.key_index;

    if (hash_lookup_info.hash_module == HASH_MODULE_FLOW
        || hash_lookup_info.hash_module == HASH_MODULE_IPFIX)
    {
        free_type = HASH_FREE_TYPE_FLOW;
    }
    else if(hash_lookup_info.hash_module == HASH_MODULE_EGRESS_XC)
    {
        free_type = HASH_FREE_TYPE_EGRESSXCOAM;
    }
    else
    {
        free_type = HASH_FREE_TYPE_NORMAL;
    }

    free_type = free_type;

    MODEL_HASH_DEBUG_INFO("@@ Show lookup result: @@\n");
    if (hash_lookup_result.key_index >= all_level_depths )
    {
        key_index = hash_lookup_result.key_index - all_level_depths;
        MODEL_HASH_DEBUG_INFO("key is in cam!\n");
    }
    if (hash_lookup_result.is_blackhole)
    {
        MODEL_HASH_DEBUG_INFO("key is in black hole cam!\n");
    }
    MODEL_HASH_DEBUG_INFO("is_conflict = %d\n", hash_lookup_result.conflict);
    MODEL_HASH_DEBUG_INFO("key_index = 0x%x!\n", key_index);
    MODEL_HASH_DEBUG_INFO("valid = 0x%x!\n", hash_lookup_result.lookup_result_valid);
    MODEL_HASH_DEBUG_INFO("ad_index = 0x%x!\n", hash_lookup_result.ad_index);
    MODEL_HASH_DEBUG_INFO("@@ -------------------- @@\n");

    if (hash_lookup_result.lookup_result_valid)
    {
        if ( hash_lookup_result.key_index < all_level_depths )
        {
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, hash_lookup_result.key_index,
                                cmd, data_entry));
            MODEL_HASH_DEBUG_INFO("Delete hash key: table-id = 0x%X, key-index = 0x%X, ad-index = 0x%X.\n",
                                  tbl_id, hash_lookup_result.key_index, hash_lookup_result.ad_index);
#if (DRVIER_WORK_PLATFORM == 1)
            uint8 chipid_base = 0;
            DRV_IF_ERROR_RETURN(drv_goldengate_get_chipid_base(&chipid_base));
            drv_goldengate_model_sram_tbl_clear_wbit(chip_id-chipid_base, tbl_id,
                                          hash_lookup_result.key_index);
#endif
        }
        else
        {
            key_index -= all_level_depths;
            cam_table_id = hash_db[p_para->hash_module].cam_table_id;
            cmd = DRV_IOR(cam_table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, key_index/4, cmd, data_entry));

            sal_memset(data_entry + unit_words*4*(key_index%4),0, key_bytes);

            cmd = DRV_IOW(cam_table_id, DRV_ENTRY_FLAG);
            DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(chip_id, key_index/4,cmd, data_entry));


#if (DRVIER_WORK_PLATFORM == 1)
            uint8 chipid_base = 0;

            if (_drv_goldengate_hash_lookup_get_key_entry_free(data_entry,unit_words,key_units,free_type))
            {
                DRV_IF_ERROR_RETURN(drv_goldengate_get_chipid_base(&chipid_base));
                drv_goldengate_model_sram_tbl_clear_wbit(chip_id-chipid_base, cam_table_id, key_index/4);
            }
#endif
            MODEL_HASH_DEBUG_INFO("Delete : cam-index = 0x%X, entry-index = 0x%X, ad-index = 0x%X.\n",
                                  key_index /4, key_index, hash_lookup_result.ad_index);

        }
    }
    else
    {
        MODEL_HASH_DEBUG_INFO("The FIB hash key isn't exist.\n");
    }
    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_key_add_by_index(hash_io_by_idx_para_t* p_para)
{
    uint32 cmd = 0;

    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    cmd = DRV_IOW(p_para->table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_para->chip_id, p_para->table_index, cmd, p_para->key));

    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_key_delete_by_index(hash_io_by_idx_para_t* p_para)
{
    uint32 cmd = 0;
    uint8 chip_id_offset = 0;
    uint32 data_entry[MAX_ENTRY_WORD] = {0};

    DRV_CHIP_ID_VALID_CHECK(p_para->chip_id);

    cmd = DRV_IOW(p_para->table_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(drv_goldengate_ioctl(p_para->chip_id, p_para->table_index, cmd, &data_entry));
    chip_id_offset = p_para->chip_id - drv_gg_init_chip_info.drv_init_chipid_base;

    chip_id_offset = chip_id_offset;
#if (DRVIER_WORK_PLATFORM == 1)
    drv_goldengate_model_sram_tbl_clear_wbit(chip_id_offset, p_para->table_id, p_para->table_index);
#endif
    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_combined_key( uint8* dest_key, uint8* src_key, uint32 len, uint32 tbl_id)
{
    uint8 converted_key[MAX_ENTRY_BYTE] = {0};

    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);
    _drv_goldengate_hash_lookup_convert_xbits_key(tbl_id, src_key, converted_key, len);
    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);

    _get_combined_key(dest_key,converted_key,len, tbl_id);
    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, dest_key);

    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_un_combined_key( uint8* dest_key, uint8* src_key, uint32 len, uint32 tbl_id)
{
    uint8 converted_key[MAX_ENTRY_BYTE] = {0};

    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);
    _get_un_combined_key(converted_key,src_key,len, tbl_id);
    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);

    _drv_goldengate_hash_lookup_unconvert_xbits_key(tbl_id, converted_key,dest_key , len);
    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, dest_key);

    return DRV_E_NONE;
}

int32
drv_goldengate_model_hash_mask_key( uint8* dest_key, uint8* src_key, hash_module_t hash_module, uint32 key_type)
{
    int i = 0;
    uint8 mask[MAX_ENTRY_BYTE] = {0};
    uint8 hash_index = 0;
    uint32 key_bytes = 0;
    uint32 hash_type = 0;
    uint32 tbl_id = 0;

    if ( HASH_MODULE_FIB_HOST0 == hash_module )
    {
        switch (key_type)
        {
            case FIBHOST0PRIMARYHASHTYPE_FCOE:
                hash_type = FIBHOST0HASHTYPE_FCOE;
                tbl_id = DsFibHost0FcoeHashKey_t;
                break;

            case FIBHOST0PRIMARYHASHTYPE_IPV4:
                hash_type = FIBHOST0HASHTYPE_IPV4;
                tbl_id = DsFibHost0Ipv4HashKey_t;
                break;
            case FIBHOST0PRIMARYHASHTYPE_IPV6MCAST:
                hash_type = FIBHOST0HASHTYPE_IPV6MCAST;
                tbl_id = DsFibHost0Ipv6McastHashKey_t;
                break;
            case FIBHOST0PRIMARYHASHTYPE_IPV6UCAST:
                hash_type = FIBHOST0HASHTYPE_IPV6UCAST;
                tbl_id = DsFibHost0Ipv6UcastHashKey_t;
                break;
            case FIBHOST0PRIMARYHASHTYPE_MAC:
                hash_type = FIBHOST0HASHTYPE_MAC;
                tbl_id = DsFibHost0MacHashKey_t;
                break;
            case FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST:
                hash_type = FIBHOST0HASHTYPE_MACIPV6MCAST;
                tbl_id = DsFibHost0MacIpv6McastHashKey_t;
                break;
            case FIBHOST0PRIMARYHASHTYPE_TRILL:
                hash_type = FIBHOST0HASHTYPE_TRILL;
                tbl_id = DsFibHost0TrillHashKey_t;
                break;

            default :
                return DRV_E_INVALID_HASH_TYPE;
        }
    }
    else
    {
        _drv_goldengate_hash_get_hash_key_type_by_tbl(hash_module, key_type, &hash_type);
        tbl_id = key_type;
    }
    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, src_key);
    DRV_IF_ERROR_RETURN(drv_goldengate_hash_lookup_get_key_info_index(hash_module, hash_type, &hash_index));

    key_bytes = BIT2BYTE(hash_db[hash_module].key_mask_ad[hash_index].bits_per_unit)
                *hash_db[hash_module].key_mask_ad[hash_index].align_unit;
    sal_memcpy(mask,hash_db[hash_module].key_mask_ad[hash_index].mask,key_bytes);

    for (i = 0; i<key_bytes; i++)
    {
        dest_key[i]= src_key[i] & mask[i];
    }

    _drv_goldengate_hash_lookup_convert_ds_endian(tbl_id, dest_key);
    return DRV_E_NONE;
}


/**
 @file sys_greatbelt_l3_hash.c

 @date 2012-03-26

 @version v2.0

 The file contains all l3 hash related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/

#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_ipuc.h"
#include "ctc_ipmc.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_ftm.h"

#include "sys_greatbelt_rpf_spool.h"
#include "sys_greatbelt_l3_hash.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_ipuc.h"
#include "sys_greatbelt_ipuc_db.h"
#include "sys_greatbelt_ipmc.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define L3_HASH_DB_MASK         0x3FF
#define L3_HASH_RPF_TABLE_SIZE     63
sys_l3_hash_master_t* p_gb_l3hash_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/* hash table size */
#define HASH_BLOCK_SIZE                 64
#define POINTER_HASH_SIZE               512

#define LPM_POINTER_MID_CRC_ADD         1
#define LPM_POINTER_MID_RETRY_TIMES     4
#define LPM_POINTER_BITS_LEN            13
#define LPM_INVALID_POINTER             ((1 << LPM_POINTER_BITS_LEN) - 1)

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

#define SYS_L3_HASH_INIT_CHECK(lchip) \
    do {    \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (p_gb_l3hash_master[lchip] == NULL){           \
            return CTC_E_NOT_INIT; }                \
    } while (0)


extern sys_ipuc_master_t* p_gb_ipuc_master[CTC_MAX_LOCAL_CHIP_NUM];
extern int32
_sys_greatbelt_l3_hash_free_idx(uint8 lchip, sys_l3_hash_type_t hash_type, void* p_info, uint32 key_index);
extern int32
_sys_greatbelt_lpm_item_clean(uint8 lchip, sys_lpm_tbl_t* p_table);
/****************************************************************************
 *
* Function
*
*****************************************************************************/
#define __0_DUPLICATE_HASH__
STATIC uint32
_sys_greatbelt_l3_hash_duplicate_key_make(sys_l3_hash_counter_t* p_hash_counter)
{
    uint32 a = 0;
    uint32 b = 0;
    uint32 c = 0;

    DRV_PTR_VALID_CHECK(p_hash_counter);

    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    b += p_hash_counter->ip;

    c = (a & 0xfff) ^ ((a >> 12) & 0xfff) ^ ((a >> 24) & 0xff) ^
        (b & 0xfff) ^ ((b >> 12) & 0xfff);

    return (c & L3_HASH_DB_MASK);
}

STATIC int32
_sys_greatbelt_l3_hash_duplicate_key_cmp(sys_l3_hash_counter_t* p_hash_counter1, sys_l3_hash_counter_t* p_hash_counter)
{
    DRV_PTR_VALID_CHECK(p_hash_counter1);
    DRV_PTR_VALID_CHECK(p_hash_counter);

    if (p_hash_counter1->ip != p_hash_counter->ip)
    {
        return FALSE;
    }
    return TRUE;
}

STATIC int32
_sys_greatbelt_l3_hash_lkp_duplicate_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info,
                                        sys_l3_hash_result_t* p_lookup_result,
                                        sys_l3_hash_counter_t** pp_hash_counter)
{

    sys_l3_hash_counter_t* p_hash_counter = NULL;
    sys_l3_hash_counter_t hash_counter;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&hash_counter, 0, sizeof(sys_l3_hash_counter_t));

    hash_counter.ip = p_ipuc_info->ip.ipv6[1];
    p_hash_counter = ctc_hash_lookup(p_gb_l3hash_master[lchip]->duplicate_hash, &hash_counter);
    if (p_hash_counter && p_lookup_result)
    {
        p_lookup_result->free = FALSE;
        p_lookup_result->valid = TRUE;
        p_lookup_result->key_index = p_hash_counter->key_offset;
        if (p_hash_counter->in_tcam)
        {
            CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID);
        }
    }

    if (pp_hash_counter)
    {
        *pp_hash_counter = p_hash_counter;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_remove_duplicate_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_counter_t* p_hash_counter = NULL;

    SYS_IPUC_DBG_FUNC();

    _sys_greatbelt_l3_hash_lkp_duplicate_key(lchip, p_ipuc_info, NULL, &p_hash_counter);
    if (!p_hash_counter)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash_counter is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_hash_counter->counter--;
    if (0 == p_hash_counter->counter)
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove duplicate hash node ip64_33 0x%x\r\n",
                    p_ipuc_info->ip.ipv6[1]);
        ctc_hash_remove(p_gb_l3hash_master[lchip]->duplicate_hash, p_hash_counter);
        mem_free(p_hash_counter);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_add_duplicate_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint32 key_offset)
{
    sys_l3_hash_counter_t* p_hash_counter = NULL;

    SYS_IPUC_DBG_FUNC();

    p_hash_counter = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_counter_t));
    if (NULL == p_hash_counter)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_hash_counter, 0, sizeof(sys_l3_hash_counter_t));
    p_hash_counter->ip = p_ipuc_info->ip.ipv6[1];
    p_hash_counter->key_offset = key_offset;
    p_hash_counter->counter++;
    if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID))
    {
        p_hash_counter->in_tcam = TRUE;
    }

    ctc_hash_insert(p_gb_l3hash_master[lchip]->duplicate_hash, p_hash_counter);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add duplicate hash node ip64_33 0x%x\r\n",
                    p_ipuc_info->ip.ipv6[1]);

    return CTC_E_NONE;
}

#define __1_L3_HASH__
STATIC uint32
_sys_greatbelt_l3_hash_ipv4_key_make(sys_l3_hash_t* p_hash)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;
    uint8 lchip = 0;

    DRV_PTR_VALID_CHECK(p_hash);
    lchip = p_hash->lchip;

    if (p_gb_ipuc_master[lchip]->use_hash8)
    {
        a += (p_hash->ip32 >> 24) & 0xFF;
    }
    else
    {
        a += (p_hash->ip32 >> 16) & 0xFF;
    }
    b += p_hash->vrf_id;
    b += p_hash->l4_dst_port;
    c += 15;
    MIX(a, b, c);

    return c % HASH_BLOCK_SIZE;
}

STATIC int32
_sys_greatbelt_l3_hash_ipv4_key_cmp(sys_l3_hash_t* p_l3_hash_o, sys_l3_hash_t* p_l3_hash)
{
    uint32 mask = 0;
    uint8 lchip = 0;

    DRV_PTR_VALID_CHECK(p_l3_hash_o);
    DRV_PTR_VALID_CHECK(p_l3_hash);
    lchip = p_l3_hash_o->lchip;

    if (p_l3_hash->vrf_id != p_l3_hash_o->vrf_id)
    {
        return FALSE;
    }

    if (p_l3_hash->l4_dst_port != p_l3_hash_o->l4_dst_port)
    {
        return FALSE;
    }

    if (p_l3_hash->is_tcp_port != p_l3_hash_o->is_tcp_port)
    {
        return FALSE;
    }

    if ((p_l3_hash->l4_dst_port + p_l3_hash_o->l4_dst_port) > 0)
    {
        mask = 0xFFFFFFFF;
    }
    else
    {
        mask = p_gb_ipuc_master[lchip]->use_hash8 ? 0xFF000000 : 0xFFFF0000;
    }

    if ((p_l3_hash->ip32 & mask) != (p_l3_hash_o->ip32 & mask))
    {
        return FALSE;
    }

    return TRUE;
}

STATIC uint32
_sys_greatbelt_l3_hash_ipv6_key_make(sys_l3_hash_t* p_hash)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;

    DRV_PTR_VALID_CHECK(p_hash);

    a += (p_hash->ip32) & 0xFF;
    b += (p_hash->ip32 >> 8) & 0xFF;
    c += (p_hash->ip32 >> 16) & 0xFF;
    MIX(a, b, c);

    c += 7;  /* vrfid and masklen and ipv6 route length - 12, unit is byte */

    a += (p_hash->ip32 >> 24) & 0xFF;
    b += p_hash->vrf_id;
    c += 15;
    FINAL(a, b, c);

    return c % HASH_BLOCK_SIZE;
}

STATIC int32
_sys_greatbelt_l3_hash_ipv6_key_cmp(sys_l3_hash_t* p_l3_hash_o, sys_l3_hash_t* p_l3_hash)
{
    DRV_PTR_VALID_CHECK(p_l3_hash_o);
    DRV_PTR_VALID_CHECK(p_l3_hash);

    if (p_l3_hash->vrf_id != p_l3_hash_o->vrf_id)
    {
        return FALSE;
    }

    if (p_l3_hash->ip32 != p_l3_hash_o->ip32)
    {
        return FALSE;
    }

    return TRUE;
}

int32
_sys_greatbelt_l3_hash_get_hash_tbl(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_t** pp_hash)
{
    sys_l3_hash_t t_hash;

    t_hash.vrf_id = p_ipuc_info->vrf_id;
    t_hash.l4_dst_port = p_ipuc_info->l4_dst_port;
    t_hash.is_tcp_port = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 1 : 0;
    t_hash.lchip = lchip;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        t_hash.ip32 = p_ipuc_info->ip.ipv6[3];
    }
    else
    {
        t_hash.ip32 = p_ipuc_info->ip.ipv4[0];
    }

    *pp_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], &t_hash);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;

    SYS_IPUC_DBG_FUNC();

    p_hash = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_t));
    if (NULL == p_hash)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_hash, 0, sizeof(sys_l3_hash_t));
    p_hash->vrf_id = p_ipuc_info->vrf_id;
    p_hash->l4_dst_port = p_ipuc_info->l4_dst_port;
    p_hash->is_tcp_port = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 1 : 0;

    p_hash->hash_offset[LPM_TYPE_NEXTHOP] = INVALID_POINTER_OFFSET;
    p_hash->hash_offset[LPM_TYPE_POINTER] = INVALID_POINTER_OFFSET;
    p_hash->lchip = lchip;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        p_hash->ip32 = p_ipuc_info->ip.ipv6[3];
    }
    else
    {
        p_hash->ip32 = p_ipuc_info->ip.ipv4[0];
    }
    p_hash->masklen = p_ipuc_info->masklen;

    p_hash->n_table.offset = INVALID_POINTER & 0xFFFF;
    p_hash->n_table.sram_index = INVALID_POINTER >> 16;

    ctc_hash_insert(p_gb_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], p_hash);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add l3 hash node ip 0x%x vrfid %d\r\n", p_hash->ip32, p_hash->vrf_id);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_lpm_tbl_t* p_table;
    sys_lpm_tbl_t* p_ntbl;
    sys_l3_hash_t* p_hash;
    uint16 idx;
    uint8 masklen = 0;

    SYS_IPUC_DBG_FUNC();

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        return CTC_E_NONE;
    }

    if (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6)
    {
        masklen = 32;
    }
    else
    {
        masklen = p_gb_ipuc_master[lchip]->use_hash8 ? 8 : 16;
    }

    if (masklen == p_ipuc_info->masklen)
    {
        p_hash->hash_offset[LPM_TYPE_NEXTHOP] = INVALID_POINTER_OFFSET;
    }
    else
    {
        if (p_hash->n_table.count == 0)
        {
            p_hash->hash_offset[LPM_TYPE_POINTER] = INVALID_POINTER_OFFSET;
        }
    }

    if (p_ipuc_info->l4_dst_port > 0)
    {
        p_hash->hash_offset[LPM_TYPE_NEXTHOP] = INVALID_POINTER_OFFSET;
    }

    if ((INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER]) ||
        (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP]))
    {
        return CTC_E_NONE;
    }

    p_table = &p_hash->n_table;
    if ((NULL == p_table) || (0 == p_table->count))
    {
        _sys_greatbelt_lpm_item_clean(lchip, p_table);

        for (idx = 0; idx < LPM_TBL_NUM; idx++)
        {
            TABLE_GET_PTR(p_table, (idx / 2), p_ntbl);
            if (p_ntbl)
            {
                TABLE_FREE_PTR(p_hash->n_table.p_ntable, (idx / 2));
                mem_free(p_ntbl);
            }
        }

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove l3 hash node ip 0x%x vrfid %d\r\n", p_hash->ip32, p_hash->vrf_id);
        ctc_hash_remove(p_gb_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], p_hash);
        mem_free(p_hash);
    }

    return CTC_E_NONE;
}

#define __2_POINTER_HASH__
STATIC uint32
_sys_greatbelt_l3_hash_pointer_key_make(sys_l3_hash_pointer_mid_hash_t* p_node)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;

    DRV_PTR_VALID_CHECK(p_node);

    b += p_node->data->pointer & 0x3FFFF;
    c = (a & 0xfff) ^ ((a >> 12) & 0xfff) ^ ((a >> 24) & 0xff) ^
        (b & 0xfff) ^ ((b >> 12) & 0xfff);

    return c % POINTER_HASH_SIZE;
}

STATIC int32
_sys_greatbelt_l3_hash_pointer_key_cmp(sys_l3_hash_pointer_mid_hash_t* p_node_o, sys_l3_hash_pointer_mid_hash_t* p_node)
{
    DRV_PTR_VALID_CHECK(p_node_o);
    DRV_PTR_VALID_CHECK(p_node);

    if (p_node_o->data->pointer != p_node->data->pointer)
    {
        return FALSE;
    }

    return TRUE;
}


#define __3_MID_HASH__
STATIC uint32
_sys_greatbelt_l3_hash_mid_key_make(sys_l3_hash_pointer_mid_hash_t* p_node)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;

    DRV_PTR_VALID_CHECK(p_node);

    b += p_node->data->mid & 0x3FFF;
    c = (a & 0xfff) ^ ((a >> 12) & 0xfff) ^ ((a >> 24) & 0xff) ^
        (b & 0xfff) ^ ((b >> 12) & 0xfff);

    return c % POINTER_HASH_SIZE;
}

STATIC int32
_sys_greatbelt_l3_hash_mid_key_cmp(sys_l3_hash_pointer_mid_hash_t* p_node_o, sys_l3_hash_pointer_mid_hash_t* p_node)
{
    DRV_PTR_VALID_CHECK(p_node_o);
    DRV_PTR_VALID_CHECK(p_node);

    if (p_node_o->data->mid != p_node->data->mid)
    {
        return FALSE;
    }

    return TRUE;
}



#define __4_POINTER_MID_CALC__
STATIC uint32
_sys_greatbelt_l3_hash_get_mid(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 result = 0;
    uint8 sed[LPM_MID_DATA_WIDTH / 8] = {0};

    CTC_PTR_VALID_CHECK(p_ipuc_info);

    if (p_ipuc_info->masklen <= 64)
    {
        return 0;
    }

    sal_memcpy(sed, &p_ipuc_info->ip.ipv6[1], 4);
    result = drv_greatbelt_hash_calculate_lpm_mid(sed, LPM_MID_BUCKET_WIDTH);

    return result;
}

int32
_sys_greatbelt_l3_hash_get_pointer(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint32* pointer)
{
    sys_l3_hash_t t_hash;
    int8 masklen = 32;
    uint8 i = 0;
    uint16 index;
    uint8 ip8 [4];
    uint8 octo = 3;
    lookup_result_t lookup_result;
    lpm_info_t lpm_info;
    sys_lpm_item_t* p_item;

    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL, NULL, NULL, NULL};
    sys_l3_hash_t* p_hash = NULL;

    *pointer = INVALID_POINTER;

    if ((CTC_IP_VER_6 != SYS_IPUC_VER(p_ipuc_info)) || (p_ipuc_info->masklen <= 64))
    {
        return CTC_E_NONE;
    }

    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));
    sal_memset(&lpm_info, 0, sizeof(lpm_info_t));
    sal_memset(&t_hash, 0, sizeof(sys_l3_hash_t));
    lookup_result.extra = &lpm_info;
    t_hash.vrf_id = p_ipuc_info->vrf_id;
    t_hash.ip32 = p_ipuc_info->ip.ipv6[3];
    t_hash.masklen = p_ipuc_info->masklen;
    ip8[0] = (p_ipuc_info->ip.ipv6[2]) & 0xFF;
    ip8[1] = (p_ipuc_info->ip.ipv6[2] >> 8) & 0xFF;
    ip8[2] = (p_ipuc_info->ip.ipv6[2] >> 16) & 0xFF;
    ip8[3] = (p_ipuc_info->ip.ipv6[2] >> 24) & 0xFF;
    p_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], &t_hash);
    if (!p_hash)
    {
        return CTC_E_NONE;
    }

    p_table[LPM_TABLE_INDEX0] = &p_hash->n_table;

    while (p_table[i])
    {
        if (masklen > 8)
        {
            TABLE_GET_PTR(p_table[i], ip8[octo - i], p_table[i + 1]);
            i++;
            masklen -= 8;
            continue;
        }

        index = SYS_LPM_GET_ITEM_IDX(ip8[octo - i], masklen);
        if (p_table[i])
        {
            ITEM_GET_PTR((p_table[i]->p_item), index, p_item);
            if (NULL != p_item)
            {
                *pointer = p_item->pointer | (p_item->pointer17_16 << 16);
            }
        }

        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_cal_pointer_mid(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint8 sed[LPM_POINTER_DATA_WIDTH / 8] = {0};
    uint32 pointer = LPM_INVALID_POINTER;
    uint8 add_times = 0;
    sys_l3_hash_pointer_mid_hash_t pointer_mid_hash;
    sys_l3_hash_pointer_mid_hash_t* p_pointer_hash = NULL;
    sys_l3_hash_pointer_mid_hash_t* p_mid_hash = NULL;
    sys_l3_hash_pointer_mid_info_t* p_data = NULL;
    sys_l3_hash_pointer_mid_info_t data;
    int32 ret = CTC_E_NONE;

    DRV_PTR_VALID_CHECK(p_ipuc_info);

    sal_memcpy(sed, &p_ipuc_info->ip.ipv6[2], 8);
    sed[8] = p_ipuc_info->vrf_id & 0xFF;
    sed[9] = (p_ipuc_info->vrf_id >> 8) & 0xFF;

    /* lookup for ipv6 pointer */
    CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_get_pointer(lchip, p_ipuc_info, &pointer));
    p_ipuc_info->pointer = (INVALID_POINTER == pointer) ? drv_greatbelt_hash_calculate_lpm_pointer(sed, LPM_POINTER_BITS_LEN)
        : pointer;
    sal_memset(&data, 0, sizeof(sys_l3_hash_pointer_mid_info_t));
    data.pointer = p_ipuc_info->pointer;
    p_ipuc_info->mid = _sys_greatbelt_l3_hash_get_mid(lchip, p_ipuc_info);
    data.mid = p_ipuc_info->mid;

GET_POINTER:
    sal_memset(&pointer_mid_hash, 0, sizeof(sys_l3_hash_pointer_mid_hash_t));
    pointer_mid_hash.data = &data;
    p_pointer_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->pointer_hash, &pointer_mid_hash);
    if (NULL == p_pointer_hash)
    {
        p_pointer_hash = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_pointer_mid_hash_t));
        if (NULL == p_pointer_hash)
        {
            return CTC_E_NO_MEMORY;
        }
        p_data = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_pointer_mid_info_t));
        if (NULL == p_data)
        {
            mem_free(p_pointer_hash);
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_pointer_hash, 0, sizeof(sys_l3_hash_pointer_mid_hash_t));
        sal_memset(p_data, 0, sizeof(sys_l3_hash_pointer_mid_info_t));
        p_data->pointer = p_ipuc_info->pointer;
        p_data->ip128_97_64_33 = p_ipuc_info->ip.ipv6[3];
        p_data->ip96_65 = p_ipuc_info->ip.ipv6[2];
        p_pointer_hash->count = 1;
        p_pointer_hash->data = p_data;
        ctc_hash_insert(p_gb_l3hash_master[lchip]->pointer_hash, p_pointer_hash);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add point hash node pointer 0x%x ip96_65 0x%x ip128_97 0x%x vrfid %d\r\n",
            p_ipuc_info->pointer, p_ipuc_info->ip.ipv6[2], p_ipuc_info->ip.ipv6[3], p_ipuc_info->vrf_id);
    }
    else
    {
        if ((p_ipuc_info->ip.ipv6[3] == p_pointer_hash->data->ip128_97_64_33) &&
            (p_ipuc_info->ip.ipv6[2] == p_pointer_hash->data->ip96_65))
        {
            p_pointer_hash->count++;
        }
        else
        {
            if (INVALID_POINTER != pointer)
            {
                /* hash 64 pointer conflict */
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                                 "%s %d LPM Hash64 pointer conflict!\n", __FUNCTION__, __LINE__);
                CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL);
                return CTC_E_NONE;
            }

            p_ipuc_info->pointer = (p_ipuc_info->pointer + LPM_POINTER_MID_CRC_ADD) % 0x3FFF;
            data.pointer = p_ipuc_info->pointer;
            add_times++;
            if (add_times <= LPM_POINTER_MID_RETRY_TIMES)
            {
                goto GET_POINTER;
            }

            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                             "%s %d LPM Hash64 pointer conflict!\n", __FUNCTION__, __LINE__);
            CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL);
            return CTC_E_NONE;
        }
    }

    add_times = 0;
GET_MID:
    pointer_mid_hash.data = &data;
    p_mid_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->mid_hash, &pointer_mid_hash);
    if (NULL == p_mid_hash)
    {
        p_mid_hash = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_pointer_mid_hash_t));
        p_data = p_mid_hash ? mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_pointer_mid_info_t)) : NULL;
        if (NULL == p_data)
        {
            if (p_mid_hash)
            {
                mem_free(p_mid_hash);
            }
            ret =  CTC_E_NO_MEMORY;
            goto MID_ERROR_RETURN;
        }

        sal_memset(p_mid_hash, 0, sizeof(sys_l3_hash_pointer_mid_hash_t));
        sal_memset(p_data, 0, sizeof(sys_l3_hash_pointer_mid_info_t));
        p_data->mid = p_ipuc_info->mid;
        p_data->ip128_97_64_33 = p_ipuc_info->ip.ipv6[1];
        p_mid_hash->count = 1;
        p_mid_hash->data = p_data;
        ctc_hash_insert(p_gb_l3hash_master[lchip]->mid_hash, p_mid_hash);
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add mid hash node mid 0x%x ip64_33 0x%x\r\n",
                    p_ipuc_info->mid, p_ipuc_info->ip.ipv6[1]);
    }
    else
    {
        if (p_ipuc_info->ip.ipv6[1] == p_mid_hash->data->ip128_97_64_33)
        {
            p_mid_hash->count++;
            return CTC_E_NONE;
        }

        p_ipuc_info->mid = (p_ipuc_info->mid + LPM_POINTER_MID_CRC_ADD) % 0x3FFF;
        data.mid = p_ipuc_info->mid;
        add_times++;
        if (add_times <= LPM_POINTER_MID_RETRY_TIMES)
        {
            goto GET_MID;
        }

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                         "%s %d LPM Hash64 pointer conflict!\n", __FUNCTION__, __LINE__);
        CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL);
        goto MID_ERROR_RETURN;
    }
    return CTC_E_NONE;

MID_ERROR_RETURN:
    if (p_pointer_hash)
    {
        p_pointer_hash->count --;
        if (p_pointer_hash->count == 0)
        {
            ctc_hash_remove(p_gb_l3hash_master[lchip]->pointer_hash, p_pointer_hash);
            mem_free(p_pointer_hash->data);
            mem_free(p_pointer_hash);
        }
    }
    return ret;
}

STATIC int32
_sys_greatblet_l3_hash_release_pointer_mid(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_pointer_mid_info_t data;
    sys_l3_hash_pointer_mid_hash_t pointer_mid_hash;
    sys_l3_hash_pointer_mid_hash_t* p_pointer_mid_hash;

    /* lookup for ipv6 pointer and mid */
    data.pointer = p_ipuc_info->pointer;
    pointer_mid_hash.data = &data;
    p_pointer_mid_hash = &pointer_mid_hash;
    p_pointer_mid_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->pointer_hash, p_pointer_mid_hash);
    if (p_pointer_mid_hash)
    {
        if ((p_pointer_mid_hash->data->ip128_97_64_33 == p_ipuc_info->ip.ipv6[3]) &&
            (p_pointer_mid_hash->data->ip96_65 == p_ipuc_info->ip.ipv6[2]))
        {
            p_pointer_mid_hash->count --;

            if (p_pointer_mid_hash->count == 0)
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove point hash node pointer 0x%x ip96_65 0x%x ip128_97 0x%x vrfid %d\r\n",
                    p_ipuc_info->pointer, p_ipuc_info->ip.ipv6[2], p_ipuc_info->ip.ipv6[3], p_ipuc_info->vrf_id);
                ctc_hash_remove(p_gb_l3hash_master[lchip]->pointer_hash, p_pointer_mid_hash);
                mem_free(p_pointer_mid_hash->data);
                mem_free(p_pointer_mid_hash);
            }
        }
    }

    data.mid = p_ipuc_info->mid;
    pointer_mid_hash.data = &data;
    p_pointer_mid_hash = &pointer_mid_hash;
    p_pointer_mid_hash = ctc_hash_lookup(p_gb_l3hash_master[lchip]->mid_hash, p_pointer_mid_hash);
    if (p_pointer_mid_hash)
    {
        if (p_pointer_mid_hash->data->ip128_97_64_33 == p_ipuc_info->ip.ipv6[1])
        {
            p_pointer_mid_hash->count --;

            if (p_pointer_mid_hash->count == 0)
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove mid hash node mid 0x%x ip64_33 0x%x\r\n",
                    p_ipuc_info->mid, p_ipuc_info->ip.ipv6[1]);
                ctc_hash_remove(p_gb_l3hash_master[lchip]->mid_hash, p_pointer_mid_hash);
                mem_free(p_pointer_mid_hash->data);
                mem_free(p_pointer_mid_hash);
            }
        }
    }

    return CTC_E_NONE;
}

#define __5_HARD_WRITE__
/* swap high key to avoid asic bug, point offset must at low addr */
STATIC int32
_sys_greatbelt_l3_hash_swap_high_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 type)
{
    uint32 cmd = 0;
    ds_lpm_ipv4_hash8_key_t hash8_nexthop;
    ds_lpm_ipv4_hash8_key_t hash8_point;
    ds_lpm_ipv4_hash16_key_t hash16_nexthop;
    ds_lpm_ipv4_hash16_key_t hash16_point;
    ds_lpm_ipv6_hash32_high_key_t hash32_nexthop;
    ds_lpm_ipv6_hash32_high_key_t hash32_point;
    uint32 key_offset = 0;
    sys_l3_hash_t* p_hash = NULL;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&hash8_nexthop, 0, sizeof(ds_lpm_ipv4_hash8_key_t));
    sal_memset(&hash8_point, 0, sizeof(ds_lpm_ipv4_hash8_key_t));
    sal_memset(&hash16_nexthop, 0, sizeof(ds_lpm_ipv4_hash16_key_t));
    sal_memset(&hash16_point, 0, sizeof(ds_lpm_ipv4_hash16_key_t));
    sal_memset(&hash32_nexthop, 0, sizeof(ds_lpm_ipv6_hash32_high_key_t));
    sal_memset(&hash32_point, 0, sizeof(ds_lpm_ipv6_hash32_high_key_t));

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    /* only one hash_offset, no need swap */
    if ((INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            || (INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_POINTER]))
    {
        return CTC_E_NONE;
    }

    /* in tcam, no need swap */
    if (p_hash->in_tcam[LPM_TYPE_NEXTHOP] + p_hash->in_tcam[LPM_TYPE_POINTER])
    {
        return CTC_E_NONE;
    }

    /* point offset at low addr, no need swap */
    if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] > p_hash->hash_offset[LPM_TYPE_POINTER])
    {
        return CTC_E_NONE;
    }

    switch (type)
    {
    case SYS_L3_HASH_TYPE_8:
        {
            cmd = DRV_IOR(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash8_nexthop));

            cmd = DRV_IOR(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash8_point));

            /* do asic swap */
            if (p_ipuc_info->masklen > 8)   /* write nexthop first */
            {
                cmd = DRV_IOW(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash8_nexthop));

                cmd = DRV_IOW(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash8_point));
            }
            else   /* write point first */
            {
                cmd = DRV_IOW(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash8_point));

                cmd = DRV_IOW(DsLpmIpv4Hash8Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash8_nexthop));
            }
        }
        break;

    case SYS_L3_HASH_TYPE_16:
        {
            cmd = DRV_IOR(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash16_nexthop));

            cmd = DRV_IOR(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash16_point));

            /* do asic swap */

            if (p_ipuc_info->masklen > 16)   /* write nexthop first */
            {
                cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash16_nexthop));

                cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash16_point));
            }
            else   /* write point first */
            {
                cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash16_point));

                cmd = DRV_IOW(DsLpmIpv4Hash16Key_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash16_nexthop));
            }

        }
        break;

    case SYS_L3_HASH_TYPE_HIGH32:
        {
            cmd = DRV_IOR(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash32_nexthop));

            cmd = DRV_IOR(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash32_point));

            /* do asic swap */

            if (p_ipuc_info->masklen > 32)   /* write nexthop first */
            {
                cmd = DRV_IOW(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash32_nexthop));

                cmd = DRV_IOW(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash32_point));
            }
            else   /* write point first */
            {
                cmd = DRV_IOW(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &hash32_point));

                cmd = DRV_IOW(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &hash32_nexthop));
            }
        }
        break;

    default:
        return CTC_E_NONE;
    }

    /* do sw swap */
    key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
    p_hash->hash_offset[LPM_TYPE_POINTER] = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
    p_hash->hash_offset[LPM_TYPE_NEXTHOP] = key_offset;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_add_small_tcam(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_type_t type)
{
    uint32 cmd = 0;
    tbl_entry_t tbl_ipkey;
    tbl_entry_t tbl_ipkey_rpf;
    ds_lpm_tcam80_key_t tcam80key, tcam80mask;
    ds_lpm_tcam80_key_t tcam80rpfkey, tcam80rpfmask;
    lpm_tcam_ad_mem_t tcam_ad;
    lpm_tcam_ad_mem_t tcam_ad_r;
    uint32 key_offset = 0;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    uint8 is_pointer = FALSE;
    uint8 is_rpf = 0;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&tcam80key, 0, sizeof(ds_lpm_tcam80_key_t));
    sal_memset(&tcam80mask, 0, sizeof(ds_lpm_tcam80_key_t));
    sal_memset(&tcam80rpfkey, 0, sizeof(ds_lpm_tcam80_key_t));
    sal_memset(&tcam80rpfmask, 0, sizeof(ds_lpm_tcam80_key_t));
    sal_memset(&tcam_ad, 0, sizeof(tcam_ad));
    sal_memset(&tcam_ad_r, 0, sizeof(tcam_ad_r));

    p_lpm_result = p_ipuc_info->lpm_result;
    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);

    is_rpf =  CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
                &&((SYS_L3_HASH_TYPE_8 == type)||(SYS_L3_HASH_TYPE_16 == type));
    /* write tcam key */
    if (SYS_L3_HASH_TYPE_8 == type)
    {
        key_offset = (p_ipuc_info->masklen > 8) ? p_hash->hash_offset[LPM_TYPE_POINTER] :
            p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        is_pointer = (p_ipuc_info->masklen > 8) ? TRUE : FALSE;

        tcam80key.ip = (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31) << 24);
        tcam80key.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH8];
        tcam80key.vrf_id = p_ipuc_info->vrf_id;
        tcam80key.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

        tcam80mask.ip = 0xFF000000;
        tcam80mask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
        tcam80mask.vrf_id = 0x3FFF;
        tcam80mask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;

        if(is_rpf)
        {/*for rpf check bug, need add another entry*/
            tcam80rpfkey.ip = (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31) << 8);
            tcam80rpfkey.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH8];
            tcam80rpfkey.vrf_id = p_ipuc_info->vrf_id;
            tcam80rpfkey.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

            tcam80rpfmask.ip = 0xFFFFFF00;
            tcam80rpfmask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
            tcam80rpfmask.vrf_id = 0x3FFF;
            tcam80rpfmask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;
        }

    }
    else if (SYS_L3_HASH_TYPE_16 == type)
    {
        key_offset = (p_ipuc_info->masklen > 16) ? p_hash->hash_offset[LPM_TYPE_POINTER] :
            p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        is_pointer = (p_ipuc_info->masklen > 16) ? TRUE : FALSE;

        tcam80key.ip = (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31) << 24) |
            (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_16_23) << 16);
        tcam80key.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH16];
        tcam80key.vrf_id = p_ipuc_info->vrf_id;
        tcam80key.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

        tcam80mask.ip = 0xFFFF0000;
        tcam80mask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
        tcam80mask.vrf_id = 0x3FFF;
        tcam80mask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;

        if(is_rpf)
        {
            tcam80rpfkey.ip = (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31) << 8) |
                (SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_16_23) << 0);
            tcam80rpfkey.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV4_HASH16];
            tcam80rpfkey.vrf_id = p_ipuc_info->vrf_id;
            tcam80rpfkey.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

            tcam80rpfmask.ip = 0xFFFFFFFF;
            tcam80rpfmask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
            tcam80rpfmask.vrf_id = 0x3FFF;
            tcam80rpfmask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;
        }

    }
    else if (SYS_L3_HASH_TYPE_HIGH32 == type)
    {
        key_offset = (p_ipuc_info->masklen > 32) ? p_hash->hash_offset[LPM_TYPE_POINTER] :
            p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        is_pointer = (p_ipuc_info->masklen > 32) ? TRUE : FALSE;

        tcam80key.ip = p_ipuc_info->ip.ipv6[3];
        tcam80key.vrf_id = p_ipuc_info->vrf_id & 0x3FFF;
        tcam80key.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_HIGH32];
        tcam80key.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

        tcam80mask.ip = 0xFFFFFFFF;
        tcam80mask.vrf_id = 0x3FFF;
        tcam80mask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
        tcam80mask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;
    }
    else if (SYS_L3_HASH_TYPE_MID32 == type)
    {
        key_offset = p_lpm_result->hash32mid_offset;

        tcam80key.ip = p_ipuc_info->ip.ipv6[1];
        tcam80key.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_MID32];
        tcam80key.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

        tcam80mask.ip = 0xFFFFFFFF;
        tcam80mask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask;
        tcam80mask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;
    }
    else if (SYS_L3_HASH_TYPE_LOW32 == type)
    {
        key_offset = p_lpm_result->hash32low_offset;

        tcam80key.ip = p_ipuc_info->ip.ipv6[0];
        tcam80key.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port[SYS_IPUC_IPV6_HASH_LOW32] | p_ipuc_info->mid;
        tcam80key.vrf_id = p_ipuc_info->pointer & 0x3FFF;
        tcam80key.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id;

        tcam80mask.ip = 0xFFFFFFFF;
        tcam80mask.layer4_port = p_gb_ipuc_master[lchip]->conflict_key_l4_port_mask | 0x1FFF;
        tcam80mask.vrf_id = 0x3FFF;
        tcam80mask.table_id = p_gb_ipuc_master[lchip]->conflict_key_table_id_mask;
    }

    tbl_ipkey.data_entry = (uint32*)(&tcam80key);
    tbl_ipkey.mask_entry = (uint32*)(&tcam80mask);
    cmd = DRV_IOW(p_gb_ipuc_master[lchip]->conflict_key_table, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, key_offset, cmd, &tbl_ipkey);


    if (is_rpf)
    {
        tbl_ipkey_rpf.data_entry = (uint32*)(&tcam80rpfkey);
        tbl_ipkey_rpf.mask_entry = (uint32*)(&tcam80rpfmask);

        DRV_IOCTL(lchip, (key_offset + 1), cmd, &tbl_ipkey_rpf);

    }

    /* write tcam ad */
    sal_memset(&tcam_ad, 0, sizeof(lpm_tcam_ad_mem_t));
    tcam_ad.nexthop = INVALID_NEXTHOP;
    tcam_ad.pointer = INVALID_POINTER;
    switch (type)
    {
        case SYS_L3_HASH_TYPE_8:
        case SYS_L3_HASH_TYPE_16:
        case SYS_L3_HASH_TYPE_HIGH32:
            if (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER])
            {
                p_table = &(p_hash->n_table);
                if (p_table->count == 0)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when add small tcam\r\n");
                    return CTC_E_UNEXPECT;
                }
                tcam_ad.pointer = p_table->offset | (p_table->sram_index << POINTER_OFFSET_BITS_LEN);
                tcam_ad.index_mask = p_table->idx_mask;
            }
            if (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            {
                if (is_pointer)
                {
                    /* need get ad_offset from hardware */
                    cmd = DRV_IOR(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);
                    DRV_IOCTL(lchip, key_offset, cmd, &tcam_ad_r);
                    tcam_ad.nexthop = tcam_ad_r.nexthop;
                }
                else
                {
                    tcam_ad.nexthop = p_ipuc_info->ad_offset;
                }
            }
            break;
        case SYS_L3_HASH_TYPE_MID32:
            tcam_ad.nexthop = p_ipuc_info->mid & 0x1FFF;
            break;
        case SYS_L3_HASH_TYPE_LOW32:
            tcam_ad.nexthop = p_ipuc_info->ad_offset & 0xFFFF;
            break;
        default:
            break;
    }

    tcam_ad.tcam_prefix_length = p_ipuc_info->masklen;
    cmd = DRV_IOW(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);


    DRV_IOCTL(lchip, key_offset, cmd, &tcam_ad);

    if (is_rpf)
    {
        DRV_IOCTL(lchip, key_offset + 1, cmd, &tcam_ad);
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_shift_from_hash_to_tcam(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_type_t type)
{
    uint32 cmd = 0;
    sys_ipuc_info_t ipuc_data;
    uint32 old_key_offset = 0;
    ds_lpm_ipv6_hash32_high_key_t hash32;
    sys_l3_hash_t* p_hash = NULL;

    SYS_IPUC_DBG_FUNC();

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (SYS_L3_HASH_TYPE_8 == type)
    {
        old_key_offset = (p_ipuc_info->masklen > 8) ? p_hash->hash_offset[LPM_TYPE_NEXTHOP] :
            p_hash->hash_offset[LPM_TYPE_POINTER];
    }
    else if (SYS_L3_HASH_TYPE_16 == type)
    {
        old_key_offset = (p_ipuc_info->masklen > 16) ? p_hash->hash_offset[LPM_TYPE_NEXTHOP] :
            p_hash->hash_offset[LPM_TYPE_POINTER];
    }
    else if (SYS_L3_HASH_TYPE_HIGH32 == type)
    {
        old_key_offset = (p_ipuc_info->masklen > 32) ? p_hash->hash_offset[LPM_TYPE_NEXTHOP] :
            p_hash->hash_offset[LPM_TYPE_POINTER];
    }

    /* add all to tcam */
    _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, type);

    /* free old hash key offset */
    sal_memset(&hash32, 0, sizeof(ds_lpm_ipv6_hash32_high_key_t));
    cmd = DRV_IOW(DsLpmIpv6Hash32HighKey_t, DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, old_key_offset, cmd, &hash32);


    sal_memset(&ipuc_data, 0, sizeof(sys_ipuc_info_t));
    _sys_greatbelt_l3_hash_free_idx(lchip, type, &ipuc_data, old_key_offset);

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_l3_hash_remove_hard_key(uint8 lchip, uint32 key_index, uint8 in_tcam)
{
    hash_io_by_idx_para_t hash_para;
    drv_hash_key_t drv_hash_lookup_key;

    SYS_IPUC_DBG_FUNC();

    if (in_tcam)
    {
        DRV_TCAM_TBL_REMOVE(lchip, DsLpmTcam80Key_t, key_index);
    }
    else
    {
        sal_memset(&hash_para, 0, sizeof(hash_io_by_idx_para_t));
        sal_memset(&drv_hash_lookup_key, 0, sizeof(drv_hash_key_t));

        /* remove DsLpmIpv4Hash8Key_t|DsLpmIpv4Hash16Key_t|DsLpmIpv6Hash32HighKey_t|DsLpmIpv6Hash32MidKey_t|DsLpmIpv6Hash32LowKey_t */
        hash_para.table_id = DsLpmIpv4Hash8Key_t;
        hash_para.table_index = key_index;
        hash_para.key = (uint32*)&drv_hash_lookup_key;

        hash_para.chip_id = lchip;
        DRV_HASH_KEY_IOCTL(&hash_para, HASH_OP_TP_DEL_BY_INDEX, NULL);

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_add_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 type)
{
    ds_lpm_ipv4_hash8_key_t hash8;
    ds_lpm_ipv4_hash16_key_t hash16;
    ds_lpm_ipv6_hash32_high_key_t hash32;
    ds_lpm_ipv6_hash32_mid_key_t hash32_mid;
    ds_lpm_ipv6_hash32_low_key_t hash32_low;
    ds_lpm_ipv4_nat_da_port_hash_key_t port_hash;
    uint32 key_offset = 0;
    hash_io_by_idx_para_t hash_info;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table = NULL;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&hash8, 0, sizeof(ds_lpm_ipv4_hash8_key_t));
    sal_memset(&hash16, 0, sizeof(ds_lpm_ipv4_hash16_key_t));
    sal_memset(&hash32, 0, sizeof(ds_lpm_ipv6_hash32_high_key_t));
    sal_memset(&hash_info, 0, sizeof(hash_io_by_idx_para_t));
    sal_memset(&port_hash, 0, sizeof(ds_lpm_ipv4_nat_da_port_hash_key_t));

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_lpm_result = p_ipuc_info->lpm_result;
    p_table = &p_hash->n_table;

    switch (type)
    {
    case SYS_L3_HASH_TYPE_8:
        if ((INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            && (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER])
            && (p_hash->in_tcam[LPM_TYPE_NEXTHOP] != p_hash->in_tcam[LPM_TYPE_POINTER]))
        {
            /* shift old hash key from sram to tcam */
            _sys_greatbelt_l3_hash_shift_from_hash_to_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_8);
            if (p_ipuc_info->masklen > 8)
            {
                p_hash->hash_offset[LPM_TYPE_NEXTHOP] = p_hash->hash_offset[LPM_TYPE_POINTER];
                p_hash->in_tcam[LPM_TYPE_NEXTHOP] = TRUE;
            }
            else
            {
                p_hash->hash_offset[LPM_TYPE_POINTER] = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
                p_hash->in_tcam[LPM_TYPE_POINTER] = TRUE;
            }
            return CTC_E_NONE;
        }

        if (p_hash->in_tcam[LPM_TYPE_NEXTHOP] + p_hash->in_tcam[LPM_TYPE_POINTER])
        {
            _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_8);
        }
        else
        {
            hash8.hash_type = LPM_HASH_TYPE_IPV4_8;
            hash8.vrf_id = p_ipuc_info->vrf_id & 0x3fff;
            hash8.ip = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31);
            hash8.valid = TRUE;
            if (p_ipuc_info->masklen > 8)
            {
                if (!p_table)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when add hash8 hard key\r\n");
                    return CTC_E_UNEXPECT;
                }
                hash8.nexthop = p_table->offset;
                hash8.pointer16 = p_table->sram_index & 0x1;
                hash8.pointer17 = (p_table->sram_index >> 1) & 0x1;
                hash8.index_mask = p_table->idx_mask;
                hash8.type = LPM_HASH_KEY_TYPE_POINTER;
                key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
            else
            {
                hash8.nexthop = p_ipuc_info->ad_offset;
                hash8.type = LPM_HASH_KEY_TYPE_NEXTHOP;
                key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            }
            hash_info.key = (uint32*)&hash8;
            hash_info.table_id = DsLpmIpv4Hash8Key_t;
            hash_info.table_index = key_offset;

            hash_info.chip_id = lchip;
            DRV_HASH_KEY_IOCTL(&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        }
        break;

    case SYS_L3_HASH_TYPE_16:
        if ((INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            && (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER])
            && (p_hash->in_tcam[LPM_TYPE_NEXTHOP] != p_hash->in_tcam[LPM_TYPE_POINTER]))
        {
            /* shift old hash key from sram to tcam */
            _sys_greatbelt_l3_hash_shift_from_hash_to_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_16);
            if (p_ipuc_info->masklen > 16)
            {
                p_hash->hash_offset[LPM_TYPE_NEXTHOP] = p_hash->hash_offset[LPM_TYPE_POINTER];
                p_hash->in_tcam[LPM_TYPE_NEXTHOP] = TRUE;
            }
            else
            {
                p_hash->hash_offset[LPM_TYPE_POINTER] = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
                p_hash->in_tcam[LPM_TYPE_POINTER] = TRUE;
            }
            return CTC_E_NONE;
        }

        if (p_hash->in_tcam[LPM_TYPE_NEXTHOP] + p_hash->in_tcam[LPM_TYPE_POINTER])
        {
            _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_16);
        }
        else
        {
            hash16.hash_type = LPM_HASH_TYPE_IPV4_16;
            hash16.vrf_id = p_ipuc_info->vrf_id & 0x3fff;
            hash16.ip = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31) << 8 |
                        SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_16_23);
            hash16.valid = TRUE;
            if (p_ipuc_info->masklen > 16)
            {
                if (!p_table)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when add hash16 hard key\r\n");
                    return CTC_E_UNEXPECT;
                }
                hash16.nexthop = p_table->offset;
                hash16.pointer16 = p_table->sram_index & 0x1;
                hash16.pointer17 = (p_table->sram_index >> 1) & 0x1;
                hash16.index_mask = p_table->idx_mask;
                hash16.type = LPM_HASH_KEY_TYPE_POINTER;
                key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
            else
            {
                hash16.nexthop = p_ipuc_info->ad_offset;
                hash16.type = LPM_HASH_KEY_TYPE_NEXTHOP;
                key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            }
            hash_info.key = (uint32*)&hash16;
            hash_info.table_id = DsLpmIpv4Hash16Key_t;
            hash_info.table_index = key_offset;


            hash_info.chip_id = lchip;
            DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        }

        break;

    case SYS_L3_HASH_TYPE_HIGH32:
        if ((INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            && (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER])
            && (p_hash->in_tcam[LPM_TYPE_NEXTHOP] != p_hash->in_tcam[LPM_TYPE_POINTER]))
        {
            /* shift old hash key from sram to tcam */
            _sys_greatbelt_l3_hash_shift_from_hash_to_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_HIGH32);
            if (p_ipuc_info->masklen > 32)
            {
                p_hash->hash_offset[LPM_TYPE_NEXTHOP] = p_hash->hash_offset[LPM_TYPE_POINTER];
                p_hash->in_tcam[LPM_TYPE_NEXTHOP] = TRUE;
            }
            else
            {
                p_hash->hash_offset[LPM_TYPE_POINTER] = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
                p_hash->in_tcam[LPM_TYPE_POINTER] = TRUE;
            }
            return CTC_E_NONE;
        }

        if (p_hash->in_tcam[LPM_TYPE_NEXTHOP] + p_hash->in_tcam[LPM_TYPE_POINTER])
        {
            _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_HIGH32);
        }
        else
        {
            hash32.hash_type = LPM_HASH_TYPE_IPV6_HIGH32;
            hash32.vrf_id0 = p_hash->vrf_id & 0xff;
            hash32.vrf_id13_8 = (p_hash->vrf_id >> 8) & 0x3F;
            hash32.ip = p_hash->ip32;
            hash32.valid = TRUE;

            if (p_ipuc_info->masklen > 32)
            {
                if (!p_table)
                {
                    SYS_IPUC_DBG_ERROR("ERROR: p_table is NULL when add hash32 hard key\r\n");
                    return CTC_E_UNEXPECT;
                }
                hash32.nexthop = p_table->offset;
                hash32.pointer16 = p_table->sram_index & 0x1;
                hash32.pointer17_17 = (p_table->sram_index >> 1) & 0x1;
                hash32.index_mask = p_table->idx_mask;
                hash32.type = LPM_HASH_KEY_TYPE_POINTER;
                key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
            else
            {
                hash32.nexthop = p_ipuc_info->ad_offset;
                hash32.type = LPM_HASH_KEY_TYPE_NEXTHOP;
                key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            }

            hash_info.key = (uint32*)&hash32;
            hash_info.table_id = DsLpmIpv6Hash32HighKey_t;
            hash_info.table_index = key_offset;

            hash_info.chip_id = lchip;
            DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        }
        break;

    case SYS_L3_HASH_TYPE_LOW32:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_LOW))
        {
            _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_LOW32);
        }
        else
        {
            sal_memset(&hash32_low, 0, sizeof(ds_lpm_ipv6_hash32_low_key_t));
            sal_memset(&hash_info, 0, sizeof(hash_io_by_idx_para_t));
            hash32_low.pointer2_0 = p_ipuc_info->pointer & 0x7;
            hash32_low.pointer4_3 = (p_ipuc_info->pointer >> 3) & 0x3;
            hash32_low.pointer12_5 = (p_ipuc_info->pointer >> 5) & 0xFF;
            hash32_low.mid = p_ipuc_info->mid;
            hash32_low.valid0 = TRUE;
            hash32_low.ip_da31_0 = p_ipuc_info->ip.ipv6[0];
            hash32_low.nexthop = p_ipuc_info->ad_offset & 0xFFFF;
            hash32_low.hash_type0 = LPM_HASH_TYPE_IPV6_LOW32;
            hash_info.key = (uint32*)&hash32_low;
            hash_info.table_id = DsLpmIpv6Hash32LowKey_t;
            hash_info.table_index = p_lpm_result->hash32low_offset;

            hash_info.chip_id = lchip;
            DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        }
        break;

    case SYS_L3_HASH_TYPE_MID32:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID))
        {
            _sys_greatbelt_l3_hash_add_small_tcam(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_MID32);
        }
        else
        {
            sal_memset(&hash32_mid, 0, sizeof(ds_lpm_ipv6_hash32_mid_key_t));
            sal_memset(&hash_info, 0, sizeof(hash_io_by_idx_para_t));

            hash32_mid.valid = TRUE;
            hash32_mid.type = LPM_HASH_KEY_TYPE_NEXTHOP;
            hash32_mid.ip_da = p_ipuc_info->ip.ipv6[1];
            hash32_mid.nexthop = p_ipuc_info->mid & 0x1FFF;
            hash32_mid.pointer16 = 0;
            hash32_mid.pointer17_17 = 0;
            hash32_mid.hash_type = LPM_HASH_TYPE_IPV6_MID32;

            hash_info.key = (uint32*)&hash32_mid;
            hash_info.table_id = DsLpmIpv6Hash32MidKey_t;
            hash_info.table_index = p_lpm_result->hash32mid_offset;

            hash_info.chip_id = lchip;
            DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        }
        break;

    case SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT:
        port_hash.hash_type = LPM_HASH_TYPE_IPV4_NAT_DA_PORT;
        port_hash.vrf_id7_0 = p_ipuc_info->vrf_id & 0xFF;
        port_hash.l4_dest_port7_0 = p_ipuc_info->l4_dst_port & 0xFF;
        port_hash.l4_dest_port15_8 = (p_ipuc_info->l4_dst_port >> 8) & 0xFF;
        port_hash.ip_da = p_ipuc_info->ip.ipv4[0];
        port_hash.l4_port_type = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 3 : 2;
        port_hash.nexthop = p_ipuc_info->ad_offset;
        port_hash.valid = TRUE;

        hash_info.key = (uint32*)&port_hash;
        hash_info.table_id = DsLpmIpv4NatDaPortHashKey_t;
        hash_info.table_index = p_hash->hash_offset[LPM_TYPE_NEXTHOP];

        hash_info.chip_id = lchip;
        DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

        break;

    default:
        SYS_IPUC_DBG_ERROR("ERROR: invalid hash type %d\r\n", type);
        return CTC_E_UNEXPECT;
    }

    _sys_greatbelt_l3_hash_swap_high_key(lchip, p_ipuc_info, type);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_remove_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_type_t hash_type)
{
    uint32 cmd = 0;
    uint32 key_index = 0;
    sys_lpm_result_t* p_lpm_result = NULL;
    sys_l3_hash_t* p_hash = NULL;
    uint8 in_tcam = FALSE;
    sys_lpm_tbl_t* p_tbl = NULL;
    lpm_tcam_ad_mem_t tcam_ad;
    sys_l3_hash_counter_t* p_hash_counter = NULL;
    uint8 is_pointer = 0;
    uint8 is_rpf_tcam = 0;

    SYS_IPUC_DBG_FUNC();
    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    sal_memset(&tcam_ad, 0, sizeof(lpm_tcam_ad_mem_t));
    p_lpm_result = p_ipuc_info->lpm_result;
    p_tbl = &p_hash->n_table;

    if (SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT == hash_type)
    {
        is_pointer = 0;
    }
    else if (SYS_L3_HASH_TYPE_8 == hash_type)
    {
        is_pointer = (p_ipuc_info->masklen > 8) ? 1 : 0;
    }
    else if (SYS_L3_HASH_TYPE_16 == hash_type)
    {
        is_pointer = (p_ipuc_info->masklen > 16) ? 1 : 0;
    }
    else if (SYS_L3_HASH_TYPE_HIGH32 == hash_type)
    {
        is_pointer = (p_ipuc_info->masklen > 32) ? 1 : 0;
    }

    is_rpf_tcam = CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
                    &&((SYS_L3_HASH_TYPE_8 == hash_type)||(SYS_L3_HASH_TYPE_16 == hash_type));

    switch (hash_type)
    {
    case SYS_L3_HASH_TYPE_8:
    case SYS_L3_HASH_TYPE_16:
    case SYS_L3_HASH_TYPE_HIGH32:
    case SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT:
        /* notice: for high32, must check in_tcam, not SYS_IPUC_CONFLICT_FLAG_HIGH for it may shift */
        if (p_hash->in_tcam[LPM_TYPE_POINTER] + p_hash->in_tcam[LPM_TYPE_NEXTHOP])
        {
            if ((p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
                && (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET))
            {
                /* only need update tcam ad */
                cmd = DRV_IOR(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &tcam_ad);
                if (is_pointer)
                {
                    if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
                    {
                        tcam_ad.pointer = INVALID_POINTER;
                        tcam_ad.index_mask = 0;
                    }
                    else
                    {
                        tcam_ad.pointer = p_tbl->offset | (p_tbl->sram_index << POINTER_OFFSET_BITS_LEN);
                        tcam_ad.index_mask = p_tbl->idx_mask;
                    }
                }
                else
                {
                    tcam_ad.nexthop = INVALID_NEXTHOP;
                }

                cmd = DRV_IOW(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);

                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &tcam_ad);
                if (is_rpf_tcam)
                {
                    DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER] + 1, cmd, &tcam_ad);
                }

            }
            else
            {
                key_index = is_pointer ? p_hash->hash_offset[LPM_TYPE_POINTER] : p_hash->hash_offset[LPM_TYPE_NEXTHOP];
                if (is_pointer)
                {
                    if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
                    {
                        /* remove tcam key */
                        _sys_greatbelt_l3_hash_remove_hard_key(lchip, key_index, TRUE);
                        if (is_rpf_tcam)
                        {
                            _sys_greatbelt_l3_hash_remove_hard_key(lchip, key_index + 1, TRUE);
                        }
                    }
                    else
                    {
                        /* only need update tcam ad */
                        cmd = DRV_IOR(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);
                        DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &tcam_ad);
                        tcam_ad.pointer = p_tbl->offset | (p_tbl->sram_index << POINTER_OFFSET_BITS_LEN);
                        tcam_ad.index_mask = p_tbl->idx_mask;
                    }

                    cmd = DRV_IOW(p_gb_ipuc_master[lchip]->conflict_da_table, DRV_ENTRY_FLAG);

                    DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &tcam_ad);
                    if (is_rpf_tcam)
                    {
                        DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER] + 1, cmd, &tcam_ad);
                    }

                }
                else
                {
                    /* remove tcam key */
                    _sys_greatbelt_l3_hash_remove_hard_key(lchip, key_index, TRUE);
                    if(is_rpf_tcam)
                    {
                        _sys_greatbelt_l3_hash_remove_hard_key(lchip, key_index + 1, TRUE);
                    }
                }
            }
        }
        else
        {
            if (is_pointer)
            {
                if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
                {
                    _sys_greatbelt_l3_hash_remove_hard_key(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], FALSE);
                }
                else
                {
                    _sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, hash_type);
                }
            }
            else
            {
                _sys_greatbelt_l3_hash_remove_hard_key(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], FALSE);
            }
        }
        break;

    case SYS_L3_HASH_TYPE_MID32:
        _sys_greatbelt_l3_hash_lkp_duplicate_key(lchip, p_ipuc_info, NULL, &p_hash_counter);
        if (p_hash_counter)
        {
            if (p_hash_counter->counter == 1)
            {
                if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID))
                {
                    in_tcam = TRUE;
                }
                _sys_greatbelt_l3_hash_remove_hard_key(lchip, p_lpm_result->hash32mid_offset, in_tcam);
            }
        }
        break;

    case SYS_L3_HASH_TYPE_LOW32:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_LOW))
        {
            in_tcam = TRUE;
        }
        _sys_greatbelt_l3_hash_remove_hard_key(lchip, p_lpm_result->hash32low_offset, in_tcam);
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->l4_dst_port > 0)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT));
        }
        else if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_8));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_16));
        }

    }
    else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (CTC_IPV6_ADDR_LEN_IN_BIT == p_ipuc_info->masklen)
        {
            /*add hash LOW 32 key*/
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_LOW32));

            /* add hash MID 32 key*/
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_MID32));
        }

        CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_HIGH32));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->l4_dst_port > 0)
        {
            _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT);
        }
        else if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_8);
        }
        else
        {
            _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_16);
        }
    }
    else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (CTC_IPV6_ADDR_LEN_IN_BIT == p_ipuc_info->masklen)
        {
            _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_LOW32);
            _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_MID32);
        }

        _sys_greatbelt_l3_hash_remove_key_by_type(lchip, p_ipuc_info, SYS_L3_HASH_TYPE_HIGH32);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_add_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    ds_lpm_ipv4_nat_sa_hash_key_t hash;
    ds_lpm_ipv4_nat_sa_port_hash_key_t port_hash;
    hash_io_by_idx_para_t hash_info;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&hash, 0, sizeof(ds_lpm_ipv4_hash8_key_t));
    sal_memset(&port_hash, 0, sizeof(ds_lpm_ipv4_hash16_key_t));
    sal_memset(&hash_info, 0, sizeof(hash_io_by_idx_para_t));

    if (p_nat_info->l4_src_port)    /* NAPT */
    {
        port_hash.hash_type = LPM_HASH_TYPE_IPV4_NAT_SA_PORT;
        port_hash.vrf_id = p_nat_info->vrf_id & 0xFF;
        port_hash.l4_source_port7_0 = p_nat_info->l4_src_port & 0xFF;
        port_hash.l4_source_port15_8 = (p_nat_info->l4_src_port >> 8) & 0xFF;
        port_hash.ip_sa = p_nat_info->ipv4[0];
        port_hash.l4_port_type = p_nat_info->is_tcp_port ? 3 : 2;
        port_hash.nexthop = p_nat_info->ad_offset;
        port_hash.valid = TRUE;

        hash_info.key = (uint32*)&port_hash;
        hash_info.table_id = DsLpmIpv4NatSaPortHashKey_t;
        hash_info.table_index = p_nat_info->key_offset;


        hash_info.chip_id = lchip;
        DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

    }
    else        /* NAT */
    {
        hash.hash_type = LPM_HASH_TYPE_IPV4_NAT_SA;
        hash.vrf_id0 = p_nat_info->vrf_id & 0xFF;
        hash.vrf_id1 = (p_nat_info->vrf_id >> 8) & 0x3F;
        hash.ip_sa = p_nat_info->ipv4[0];
        hash.nexthop = p_nat_info->ad_offset;
        hash.valid = TRUE;

        hash_info.key = (uint32*)&hash;
        hash_info.table_id = DsLpmIpv4NatSaHashKey_t;
        hash_info.table_index = p_nat_info->key_offset;

        hash_info.chip_id = lchip;
        DRV_HASH_KEY_IOCTL((void*)&hash_info, HASH_OP_TP_ADD_BY_INDEX, NULL);

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_remove_nat_sa_key(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    _sys_greatbelt_l3_hash_remove_hard_key(lchip, p_nat_info->key_offset, 0);

    return CTC_E_NONE;
}

#define __6_SOFT_HASH_INDEX__
STATIC int32
_sys_greatbelt_l3_hash_get_bucket_index(uint8 lchip, uint8 type, void* p_info, uint32* bucket_index)
{
    uint8 hashkey[20] = {0};
    key_info_t key_info;
    key_result_t key_result;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_ipuc_nat_sa_info_t* p_nat_info = NULL;
    uint8 l4_port_type = 0;

    SYS_IPUC_DBG_FUNC();
    DRV_PTR_VALID_CHECK(p_info);
    DRV_PTR_VALID_CHECK(bucket_index);

    sal_memset(&key_info, 0, sizeof(key_info_t));
    key_info.hash_module = HASH_MODULE_LPM;
    key_info.key_length = LPM_CRC_DATA_WIDTH;
    key_info.polynomial_index = p_gb_l3hash_master[lchip]->hash_bucket_type;
    sal_memset(hashkey, 0, sizeof(uint8) * 20);

    switch (type)
    {
    case SYS_L3_HASH_TYPE_8:
        p_ipuc_info = p_info;
        /* ((LPM_HASH_TYPE_IPV4_8 & 0xF)<<3) |(valid<<2) */
        hashkey[11] = 0x24;
        hashkey[16] = (p_ipuc_info->vrf_id >> 8) & 0x3F;
        hashkey[17] = p_ipuc_info->vrf_id & 0xFF;
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31);
        break;

    case SYS_L3_HASH_TYPE_16:
        p_ipuc_info = p_info;
        /* ((LPM_HASH_TYPE_IPV4_16 & 0xF)<<3) |(valid<<2) */
        hashkey[11] = 0x4;
        hashkey[16] = (p_ipuc_info->vrf_id >> 8) & 0x3F;
        hashkey[17] = p_ipuc_info->vrf_id & 0xFF;
        hashkey[18] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31);
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_16_23);
        break;

    case SYS_L3_HASH_TYPE_HIGH32:
        p_ipuc_info = p_info;
        hashkey[10] = (p_ipuc_info->vrf_id >> 9) & 0x1F;
        /* (((p_ipuc_info->vrf_id >> 8 )&0x1) << 7) | ((LPM_HASH_TYPE_IPV6_32 & 0xF) <<3) | (valid<< 2)*/
        hashkey[11] = (((p_ipuc_info->vrf_id >> 8) & 0x1) << 7) | 0x34;
        hashkey[14] = p_ipuc_info->vrf_id & 0xFF;
        hashkey[16] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_120_127);
        hashkey[17] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_112_119);
        hashkey[18] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_104_111);
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_96_103);
        break;

    case SYS_L3_HASH_TYPE_MID32:
        p_ipuc_info = p_info;
        /* ((LPM_HASH_TYPE_IPV6_MID32 & 0xF)<<3) |(valid<<2) |(type <<1 )*/
        hashkey[11] = 0x1C;
        hashkey[16] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_56_63);
        hashkey[17] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_48_55);
        hashkey[18] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_40_47);
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_32_39);
        break;

    case SYS_L3_HASH_TYPE_LOW32:
        p_ipuc_info = p_info;
        hashkey[10] = (p_ipuc_info->pointer >> 6) & 0x7F;
        /* (((p_ipuc_info->pointer >> 6 )&0x1) << 7) | ((LPM_HASH_TYPE_IPV6_LOW32 & 0xF) <<3) | (valid<< 2) |pointer3_4*/
        hashkey[11] = (((p_ipuc_info->pointer >> 5) & 0x1) << 7) | 0x14 | ((p_ipuc_info->pointer >> 3) & 0x3);
        hashkey[14] = ((p_ipuc_info->pointer & 0x7) << 5) | ((p_ipuc_info->mid >> 8) & 0x1F);
        hashkey[15] = p_ipuc_info->mid & 0xFF;
        hashkey[16] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_24_31);
        hashkey[17] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_16_23);
        hashkey[18] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_8_15);
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv6, SYS_IP_OCTO_0_7);
        break;

    case SYS_L3_HASH_TYPE_NAT_IPV4_SA:
        p_nat_info = p_info;
        /* (((p_nat_info->vrf_id >> 8 )&0x1) << 7) | ((LPM_HASH_TYPE_IPV4_NAT_SA & 0xF)<<3) |(valid<<2) */
        hashkey[10] = (p_nat_info->vrf_id >> 9) & 0x1F;
        hashkey[11] = (((p_nat_info->vrf_id >> 8) & 0x1) << 7) | 0x5C;
        hashkey[14] = p_nat_info->vrf_id & 0xFF;
        hashkey[16] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_24_31);
        hashkey[17] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_16_23);
        hashkey[18] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_8_15);
        hashkey[19] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_0_7);
        break;

    case SYS_L3_HASH_TYPE_NAT_IPV4_SA_PORT:
        p_nat_info = p_info;
        l4_port_type = p_nat_info->is_tcp_port ? 3 : 2;
        /* (((p_nat_info->vrf_id >> 8 )&0x1) << 7) | ((LPM_HASH_TYPE_IPV4_NAT_SA_PORT & 0xF)<<3) |(valid<<2 | l4_port_type) */
        hashkey[10] = (p_nat_info->l4_src_port >> 9) & 0x7F;
        hashkey[11] = (((p_nat_info->l4_src_port >> 8) & 0x1) << 7) | 0x54 | l4_port_type;
        hashkey[14] = p_nat_info->vrf_id & 0xFF;
        hashkey[15] = p_nat_info->l4_src_port & 0xFF;
        hashkey[16] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_24_31);
        hashkey[17] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_16_23);
        hashkey[18] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_8_15);
        hashkey[19] = SYS_IPUC_OCTO(p_nat_info->ipv4, SYS_IP_OCTO_0_7);
        break;

    case SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT:
        p_ipuc_info = p_info;
        l4_port_type = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 3 : 2;
        /* (((p_ipuc_info->l4_dst_port >> 8 )&0x1) << 7) | ((LPM_HASH_TYPE_IPV4_NAT_DA_PORT & 0xF)<<3) |(valid<<2 | l4_port_type) */
        hashkey[10] = (p_ipuc_info->l4_dst_port >> 9) & 0x7F;
        hashkey[11] = (((p_ipuc_info->l4_dst_port >> 8) & 0x1) << 7) | 0x74 | l4_port_type;
        hashkey[14] = p_ipuc_info->vrf_id & 0xFF;
        hashkey[15] = p_ipuc_info->l4_dst_port & 0xFF;
        hashkey[16] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_24_31);
        hashkey[17] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_16_23);
        hashkey[18] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_8_15);
        hashkey[19] = SYS_IPUC_OCTO(p_ipuc_info->ip.ipv4, SYS_IP_OCTO_0_7);
        break;

    default:
        return CTC_E_NONE;
    }

    key_info.p_hash_key = hashkey;

    sal_memset(&key_result, 0, sizeof(key_result_t));
    CTC_ERROR_RETURN(drv_greatbelt_hash_calculate_index(&key_info, &key_result));

    *bucket_index = key_result.bucket_index;

    switch (p_gb_l3hash_master[lchip]->hash_num_mode)
    {
    case 3:
        /* 2K buckets * 2 entries */
        CLEAR_BIT((*bucket_index), 11);

    case 2:
        /* 4K buckets * 2 entries */
        CLEAR_BIT((*bucket_index), 12);

    case 1:
        /* 8K buckets * 2 entries */
        CLEAR_BIT((*bucket_index), 13);

    default:
        /* 16K buckets * 2 entries */
        break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_resolve_conflict(uint8 lchip, sys_l3_hash_type_t hash_type,
                                        sys_ipuc_info_t* p_ipuc_info,
                                        uint32* p_offset)
{
    SYS_IPUC_DBG_FUNC();

    /* for NAPT, not use small tcam */
    if (SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT == hash_type)
    {
        return CTC_E_NO_RESOURCE;
    }

    if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
        &&((SYS_L3_HASH_TYPE_8 == hash_type)||(SYS_L3_HASH_TYPE_16 == hash_type)))
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsLpmTcam80Key_t, 0, 2, p_offset));
        p_gb_ipuc_master[lchip]->conflict_tcam_counter +=2;
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsLpmTcam80Key_t, 0, 1, p_offset));
        p_gb_ipuc_master[lchip]->conflict_tcam_counter ++;
    }

    switch (hash_type)
    {
    case SYS_L3_HASH_TYPE_8:
    case SYS_L3_HASH_TYPE_16:
        CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_IPV4);
        break;

    case SYS_L3_HASH_TYPE_HIGH32:
        CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_HIGH);
        break;

    case SYS_L3_HASH_TYPE_MID32:
        CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID);

        break;

    case SYS_L3_HASH_TYPE_LOW32:
        CTC_SET_FLAG(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_LOW);
        break;

    default:
        SYS_IPUC_DBG_ERROR("ERROR: invalid hash type %d\r\n", hash_type);
        return CTC_E_UNEXPECT;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_alloc_idx(uint8 lchip, sys_l3_hash_type_t hash_type, void* p_info, sys_l3_hash_result_t* p_lookup_result)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 bucket_index = 0;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    lpm_tcam_ad_mem_t lpm_tcam_ad;
    sys_l3_hash_counter_t* p_hash_counter = NULL;
    uint32 duplicate_key = (SYS_L3_HASH_TYPE_MID32 == hash_type);

    SYS_IPUC_DBG_FUNC();
    DRV_PTR_VALID_CHECK(p_info);
    DRV_PTR_VALID_CHECK(p_lookup_result);

    SYS_L3_HASH_INIT_CHECK(lchip);

    p_lookup_result->free = FALSE;
    p_lookup_result->valid = FALSE;
    p_lookup_result->key_index = INVALID_BUCKET_ENTRY_INDEX;

    p_ipuc_info = (sys_ipuc_info_t*)p_info;

    if (duplicate_key)
    {
        _sys_greatbelt_l3_hash_lkp_duplicate_key(lchip, p_ipuc_info, p_lookup_result, &p_hash_counter);
        if (p_lookup_result->valid)
        {
            p_hash_counter->counter ++;
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d use duplicate_key index %d\r\n", hash_type, p_lookup_result->key_index);
            return CTC_E_NONE;
        }
    }

    ret = _sys_greatbelt_l3_hash_get_bucket_index(lchip, hash_type, p_info, &bucket_index);

    if ((CTC_E_NONE != ret) || (3 == p_gb_l3hash_master[lchip]->hash_status[bucket_index]))
    {
        ret = _sys_greatbelt_l3_hash_resolve_conflict(lchip, hash_type, p_ipuc_info, &p_lookup_result->key_index);

        if (CTC_E_NONE != ret)
        {
            return CTC_E_NONE;
        }
        else
        {
            /* format conflict tcam ad */
            sal_memset(&lpm_tcam_ad, 0, sizeof(lpm_tcam_ad_mem_t));
            lpm_tcam_ad.nexthop = INVALID_NEXTHOP;
            lpm_tcam_ad.pointer = INVALID_POINTER;
            cmd = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);

            DRV_IOCTL(lchip, p_lookup_result->key_index, cmd, &lpm_tcam_ad);

        }
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d alloc small tcam index %d\r\n", hash_type, p_lookup_result->key_index);
    }
    else
    {
        if (0 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
        {
            p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 1;
            p_lookup_result->key_index = bucket_index << 1;
        }
        else if (0x1 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
        {
            p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 2;
            p_lookup_result->key_index = (bucket_index << 1) + 1;
        }
        else if (0x2 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
        {
            p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 1;
            p_lookup_result->key_index = bucket_index << 1;
        }
        p_gb_ipuc_master[lchip]->hash_counter ++;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d alloc hash index %d\r\n", hash_type, p_lookup_result->key_index);
    }

    if (duplicate_key)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add_duplicate_key(lchip, p_ipuc_info, p_lookup_result->key_index));
    }

    p_lookup_result->free = TRUE;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_free_idx(uint8 lchip, sys_l3_hash_type_t hash_type, void* p_info, uint32 key_index)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_l3_hash_counter_t* p_hash_counter = NULL;
    uint16 bucket_idx = 0;
    uint8  entry_idx = 0;
    uint8  in_tcam = 0;

    SYS_IPUC_DBG_FUNC();
    DRV_PTR_VALID_CHECK(p_info);
    p_ipuc_info = (sys_ipuc_info_t*)p_info;

    if (SYS_L3_HASH_INVALID_INDEX == key_index)
    {
        SYS_IPUC_DBG_ERROR("ERROR: invalid key index %d\r\n", key_index);
        return CTC_E_UNEXPECT;
    }

    SYS_L3_HASH_INIT_CHECK(lchip);

    switch (hash_type)
    {
    case SYS_L3_HASH_TYPE_8:
    case SYS_L3_HASH_TYPE_16:
    case SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_IPV4))
        {
            if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, CTC_IPUC_FLAG_RPF_CHECK)
                &&((SYS_L3_HASH_TYPE_8 == hash_type)||(SYS_L3_HASH_TYPE_16 == hash_type)))
            {
                sys_greatbelt_ftm_free_table_offset(lchip, DsLpmTcam80Key_t, 0, 2, key_index);
                in_tcam = 2;
            }
            else
            {
                sys_greatbelt_ftm_free_table_offset(lchip, DsLpmTcam80Key_t, 0, 1, key_index);
                in_tcam = 1;
            }

        }
        else
        {
            bucket_idx = key_index >> 1;
            entry_idx = key_index % 2;
            CTC_BIT_UNSET(p_gb_l3hash_master[lchip]->hash_status[bucket_idx], entry_idx);
            p_gb_ipuc_master[lchip]->hash_counter --;
        }
        break;

    case SYS_L3_HASH_TYPE_HIGH32:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_HIGH))
        {
            sys_greatbelt_ftm_free_table_offset(lchip, DsLpmTcam80Key_t, 0, 1, key_index);
            in_tcam = 1;
        }
        else
        {
            bucket_idx = key_index >> 1;
            entry_idx = key_index % 2;
            CTC_BIT_UNSET(p_gb_l3hash_master[lchip]->hash_status[bucket_idx], entry_idx);
            p_gb_ipuc_master[lchip]->hash_counter --;
        }
        break;

    case SYS_L3_HASH_TYPE_MID32:
        _sys_greatbelt_l3_hash_remove_duplicate_key(lchip, p_ipuc_info);
        _sys_greatbelt_l3_hash_lkp_duplicate_key(lchip, p_ipuc_info, NULL, &p_hash_counter);
        if (NULL == p_hash_counter)
        {
            if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_MID))
            {
                sys_greatbelt_ftm_free_table_offset(lchip, DsLpmTcam80Key_t, 0, 1, key_index);
                in_tcam = 1;
            }
            else
            {
                bucket_idx = key_index >> 1;
                entry_idx = key_index % 2;
                CTC_BIT_UNSET(p_gb_l3hash_master[lchip]->hash_status[bucket_idx], entry_idx);
                p_gb_ipuc_master[lchip]->hash_counter --;
            }
        }
        break;

    case SYS_L3_HASH_TYPE_LOW32:
        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_LOW))
        {
            sys_greatbelt_ftm_free_table_offset(lchip, DsLpmTcam80Key_t, 0, 1, key_index);
            in_tcam = 1;
        }
        else
        {
            bucket_idx = key_index >> 1;
            entry_idx = key_index % 2;
            CTC_BIT_UNSET(p_gb_l3hash_master[lchip]->hash_status[bucket_idx], entry_idx);
            p_gb_ipuc_master[lchip]->hash_counter --;
        }
        break;

    default:
        SYS_IPUC_DBG_ERROR("ERROR: invalid hash type %d\r\n", hash_type);
        return  CTC_E_UNEXPECT;
        break;
    }

    if (in_tcam)
    {
        p_gb_ipuc_master[lchip]->conflict_tcam_counter -= in_tcam;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d free small tcam index %d\r\n", hash_type, key_index);
    }
    else
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d free hash index %d\r\n", hash_type, key_index);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_alloc_high_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_result_t lookup_result;
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_key_type_t key_type = LPM_TYPE_NEXTHOP;
    sys_l3_hash_type_t hash_type = 0;
    uint8 conflict_flag = 0;

    SYS_IPUC_DBG_FUNC();
    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->l4_dst_port > 0)   /* for NAPT */
        {
            hash_type = SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT;
        }
        else if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            hash_type = SYS_L3_HASH_TYPE_8;
            if (p_ipuc_info->masklen > 8)
            {
                key_type = LPM_TYPE_POINTER;
            }
        }
        else
        {
            hash_type = SYS_L3_HASH_TYPE_16;
            if (p_ipuc_info->masklen > 16)
            {
                key_type = LPM_TYPE_POINTER;
            }
        }
        conflict_flag = SYS_IPUC_CONFLICT_FLAG_IPV4;
    }
    else
    {
        hash_type = SYS_L3_HASH_TYPE_HIGH32;
        if (p_ipuc_info->masklen > 32)
        {
            key_type = LPM_TYPE_POINTER;
        }
        conflict_flag = SYS_IPUC_CONFLICT_FLAG_HIGH;
    }

    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_add(lchip, p_ipuc_info));
        _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);

        _sys_greatbelt_l3_hash_alloc_idx(lchip, hash_type, p_ipuc_info, &lookup_result);
        if (FALSE == lookup_result.free)
        {
            p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
            _sys_greatbelt_l3_hash_remove(lchip, p_ipuc_info);
            return CTC_E_NONE;
        }
        else
        {
            p_hash->hash_offset[key_type] = lookup_result.key_index;
            p_hash->in_tcam[key_type] = CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, conflict_flag) ? TRUE : FALSE;
        }
    }
    else
    {
        if (p_hash->in_tcam[LPM_TYPE_POINTER] + p_hash->in_tcam[LPM_TYPE_NEXTHOP])  /* in small tcam */
        {
            p_ipuc_info->conflict_flag |= conflict_flag;
            if (INVALID_POINTER_OFFSET == p_hash->hash_offset[key_type])
            {
                p_hash->hash_offset[key_type] = p_hash->hash_offset[!key_type];
                p_hash->in_tcam[key_type] = TRUE;
            }
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d use exist small tcam index %d\r\n", hash_type, p_hash->hash_offset[key_type]);
        }
        else    /* in hash */
        {
            if (INVALID_POINTER_OFFSET == p_hash->hash_offset[key_type])
            {
                _sys_greatbelt_l3_hash_alloc_idx(lchip, hash_type, p_ipuc_info, &lookup_result);
                if (FALSE == lookup_result.free)
                {
                    p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
                    return CTC_E_NONE;
                }
                else
                {
                    p_hash->hash_offset[key_type] = lookup_result.key_index;
                    p_hash->in_tcam[key_type] = CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, conflict_flag) ? TRUE : FALSE;
                }
            }
            else
            {
                SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d use exist hash index %d\r\n", hash_type, p_hash->hash_offset[key_type]);
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_free_high_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_key_type_t key_type = LPM_TYPE_NEXTHOP;
    sys_l3_hash_type_t hash_type = 0;
    sys_lpm_tbl_t* p_tbl = NULL;

    SYS_IPUC_DBG_FUNC();
    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->l4_dst_port > 0)
        {
            hash_type = SYS_L3_HASH_TYPE_NAT_IPV4_DA_PORT;
        }
        else if (p_gb_ipuc_master[lchip]->use_hash8)
        {
            hash_type = SYS_L3_HASH_TYPE_8;
            if (p_ipuc_info->masklen > 8)
            {
                key_type = LPM_TYPE_POINTER;
            }
        }
        else
        {
            hash_type = SYS_L3_HASH_TYPE_16;
            if (p_ipuc_info->masklen > 16)
            {
                key_type = LPM_TYPE_POINTER;
            }
        }
    }
    else
    {
        hash_type = SYS_L3_HASH_TYPE_HIGH32;
        if (p_ipuc_info->masklen > 32)
        {
            key_type = LPM_TYPE_POINTER;
        }
    }


    _sys_greatbelt_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL when free hash key index\r\n");
        return CTC_E_UNEXPECT;
    }
    else
    {
        p_tbl = &p_hash->n_table;
        if (p_hash->in_tcam[LPM_TYPE_POINTER] + p_hash->in_tcam[LPM_TYPE_NEXTHOP])  /* in small tcam */
        {
            if (key_type == LPM_TYPE_POINTER)
            {
                if (((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
                    && (INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_NEXTHOP]))
                {
                    _sys_greatbelt_l3_hash_free_idx(lchip, hash_type, p_ipuc_info, p_hash->hash_offset[key_type]);
                    p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
                    p_hash->in_tcam[key_type] = FALSE;
                }
            }
            else
            {
                if (INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_POINTER])
                {
                    _sys_greatbelt_l3_hash_free_idx(lchip, hash_type, p_ipuc_info, p_hash->hash_offset[key_type]);
                    p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
                    p_hash->in_tcam[key_type] = FALSE;
                }
            }
        }
        else    /* in hash */
        {
            if (key_type == LPM_TYPE_POINTER)
            {
                if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
                {
                    _sys_greatbelt_l3_hash_free_idx(lchip, hash_type, p_ipuc_info, p_hash->hash_offset[key_type]);
                    p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
                }
            }
            else
            {
                _sys_greatbelt_l3_hash_free_idx(lchip, hash_type, p_ipuc_info, p_hash->hash_offset[key_type]);
                p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
            }
        }
        _sys_greatbelt_l3_hash_remove(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_alloc_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;
    sys_l3_hash_result_t lookup_result;
    sys_lpm_result_t* p_lpm_result = 0;

    SYS_IPUC_DBG_FUNC();
    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_alloc_high_key_index(lchip, p_ipuc_info));
    }
    else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        p_lpm_result = (sys_lpm_result_t*)p_ipuc_info->lpm_result;
        if (CTC_IPV6_ADDR_LEN_IN_BIT == p_ipuc_info->masklen)
        {
            /* build pointer and mid */
            CTC_ERROR_RETURN(_sys_greatbelt_l3_hash_cal_pointer_mid(lchip, p_ipuc_info));
            if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
            {
                return CTC_E_NONE;
            }

            /* build hash low 32 key offset */
            _sys_greatbelt_l3_hash_alloc_idx(lchip, SYS_L3_HASH_TYPE_LOW32, p_ipuc_info, &lookup_result);
            if ((FALSE == lookup_result.valid) && (FALSE == lookup_result.free))
            {
                p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
                _sys_greatblet_l3_hash_release_pointer_mid(lchip, p_ipuc_info);
                return CTC_E_NONE;
            }
            else
            {
                p_lpm_result->hash32low_offset = lookup_result.key_index;
            }

            /* build hash mid 32 key offset */
            _sys_greatbelt_l3_hash_alloc_idx(lchip, SYS_L3_HASH_TYPE_MID32, p_ipuc_info, &lookup_result);
            if ((FALSE == lookup_result.valid) && (FALSE == lookup_result.free))
            {
                p_ipuc_info->conflict_flag |= SYS_IPUC_CONFLICT_FLAG_ALL;
                _sys_greatblet_l3_hash_release_pointer_mid(lchip, p_ipuc_info);
                _sys_greatbelt_l3_hash_free_idx(lchip, SYS_L3_HASH_TYPE_LOW32, p_ipuc_info, p_lpm_result->hash32low_offset);
                return CTC_E_NONE;
            }
            else
            {
                p_lpm_result->hash32mid_offset = lookup_result.key_index;
            }
        }

        /* build hash high 32 key offset */
        ret = _sys_greatbelt_l3_hash_alloc_high_key_index(lchip, p_ipuc_info);
        if ((ret || CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
            && (CTC_IPV6_ADDR_LEN_IN_BIT == p_ipuc_info->masklen))
        {
            _sys_greatblet_l3_hash_release_pointer_mid(lchip, p_ipuc_info);
            _sys_greatbelt_l3_hash_free_idx(lchip, SYS_L3_HASH_TYPE_LOW32, p_ipuc_info, p_lpm_result->hash32low_offset);
            _sys_greatbelt_l3_hash_free_idx(lchip, SYS_L3_HASH_TYPE_MID32, p_ipuc_info, p_lpm_result->hash32mid_offset);
            return ret;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_free_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_lpm_result_t* p_lpm_result = 0;

    SYS_IPUC_DBG_FUNC();
    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        _sys_greatbelt_l3_hash_free_high_key_index(lchip, p_ipuc_info);
    }
    else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        p_lpm_result = (sys_lpm_result_t*)p_ipuc_info->lpm_result;
        if (CTC_IPV6_ADDR_LEN_IN_BIT == p_ipuc_info->masklen)
        {
            /* free pointer and mid */
            _sys_greatblet_l3_hash_release_pointer_mid(lchip, p_ipuc_info);

            /* free hash low 32 key offset */
            _sys_greatbelt_l3_hash_free_idx(lchip, SYS_L3_HASH_TYPE_LOW32, p_ipuc_info, p_lpm_result->hash32low_offset);

            /* free hash mid 32 key offset */
            _sys_greatbelt_l3_hash_free_idx(lchip, SYS_L3_HASH_TYPE_MID32, p_ipuc_info, p_lpm_result->hash32mid_offset);
        }

        /* build hash high 32 key offset */
        _sys_greatbelt_l3_hash_free_high_key_index(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_alloc_nat_key_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    int32 ret = CTC_E_NONE;
    sys_l3_hash_type_t hash_type = 0;
    uint32 bucket_index = 0;

    SYS_IPUC_DBG_FUNC();

    if (p_nat_info->l4_src_port == 0)
    {
        hash_type = SYS_L3_HASH_TYPE_NAT_IPV4_SA;
    }
    else
    {
        hash_type = SYS_L3_HASH_TYPE_NAT_IPV4_SA_PORT;
    }

    ret = _sys_greatbelt_l3_hash_get_bucket_index(lchip, hash_type, p_nat_info, &bucket_index);

    if ((CTC_E_NONE != ret) || (3 == p_gb_l3hash_master[lchip]->hash_status[bucket_index]))
    {
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash %d conflict, need do tcam\r\n", hash_type);
        return CTC_E_IPUC_HASH_CONFLICT;
    }

    if (0 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
    {
        p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 1;
        p_nat_info->key_offset = bucket_index << 1;
    }
    else if (0x1 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
    {
        p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 2;
        p_nat_info->key_offset = (bucket_index << 1) + 1;
    }
    else if (0x2 == p_gb_l3hash_master[lchip]->hash_status[bucket_index])
    {
        p_gb_l3hash_master[lchip]->hash_status[bucket_index] |= 1;
        p_nat_info->key_offset = bucket_index << 1;
    }
    p_gb_ipuc_master[lchip]->hash_counter ++;
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type %d alloc hash index %d\r\n", hash_type, p_nat_info->key_offset);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_l3_hash_free_nat_key_index(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    uint16 bucket_idx = 0;
    uint8  entry_idx = 0;

    SYS_IPUC_DBG_FUNC();
    DRV_PTR_VALID_CHECK(p_nat_info);

    bucket_idx = p_nat_info->key_offset >> 1;
    entry_idx = p_nat_info->key_offset % 2;
    CTC_BIT_UNSET(p_gb_l3hash_master[lchip]->hash_status[bucket_idx], entry_idx);
    p_gb_ipuc_master[lchip]->hash_counter --;

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "L3 hash type NAT free hash index %d\r\n", p_nat_info->key_offset);

    return CTC_E_NONE;
}

#define __8_INIT_AND_SHOW__
int32
sys_greatbelt_l3_hash_init(uint8 lchip)
{
    uint32 cmd = 0;
    fib_engine_hash_ctl_t fib_engine_hash_ctl;
    uint32 size = 0;
    uint32 table_size = 0;

    if (NULL != p_gb_l3hash_master[lchip])
    {
        return CTC_E_NONE;
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmIpv4Hash8Key_t, &table_size));
    if (0 == table_size)
    {
        return CTC_E_NO_RESOURCE;
    }

    p_gb_l3hash_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_l3_hash_master_t));
    if (NULL == p_gb_l3hash_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_l3hash_master[lchip], 0, sizeof(sys_l3_hash_master_t));

    /* Get hash look up number and init hash status*/
    sal_memset(&fib_engine_hash_ctl, 0, sizeof(fib_engine_hash_ctl_t));
    cmd = DRV_IOR(FibEngineHashCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_engine_hash_ctl));
    p_gb_l3hash_master[lchip]->hash_num_mode = fib_engine_hash_ctl.lpm_hash_num_mode;
    p_gb_l3hash_master[lchip]->hash_bucket_type = fib_engine_hash_ctl.lpm_hash_bucket_type;

    p_gb_l3hash_master[lchip]->duplicate_hash = ctc_hash_create(1, (L3_HASH_DB_MASK + 1),
                                               (hash_key_fn)_sys_greatbelt_l3_hash_duplicate_key_make,
                                               (hash_cmp_fn)_sys_greatbelt_l3_hash_duplicate_key_cmp);

    p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4] = ctc_hash_create(p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4] / HASH_BLOCK_SIZE, HASH_BLOCK_SIZE,
                                            (hash_key_fn)_sys_greatbelt_l3_hash_ipv4_key_make,
                                            (hash_cmp_fn)_sys_greatbelt_l3_hash_ipv4_key_cmp);

    p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6] = ctc_hash_create(p_gb_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6] / HASH_BLOCK_SIZE, HASH_BLOCK_SIZE,
                                            (hash_key_fn)_sys_greatbelt_l3_hash_ipv6_key_make,
                                            (hash_cmp_fn)_sys_greatbelt_l3_hash_ipv6_key_cmp);

    p_gb_l3hash_master[lchip]->pointer_hash = ctc_hash_create(1, POINTER_HASH_SIZE,
                                             (hash_key_fn)_sys_greatbelt_l3_hash_pointer_key_make,
                                             (hash_cmp_fn)_sys_greatbelt_l3_hash_pointer_key_cmp);

    p_gb_l3hash_master[lchip]->mid_hash = ctc_hash_create(1, POINTER_HASH_SIZE,
                                             (hash_key_fn)_sys_greatbelt_l3_hash_mid_key_make,
                                             (hash_cmp_fn)_sys_greatbelt_l3_hash_mid_key_cmp);

    switch (p_gb_l3hash_master[lchip]->hash_num_mode)
    {
    case 3:
        /* 2K buckets * 2 entries */
        size = 2 * 1024 * sizeof(uint8);
        break;

    case 2:
        /* 4K buckets * 2 entries */
        size = 4 * 1024 * sizeof(uint8);
        break;

    case 1:
        /* 8K buckets * 2 entries */
        size = 8 * 1024 * sizeof(uint8);
        break;

    default:
        /* 16K buckets * 2 entries */
        size = 16 * 1024 * sizeof(uint8);
        break;
    }

    p_gb_l3hash_master[lchip]->l3_hash_num = size * 2;
    p_gb_l3hash_master[lchip]->hash_status = (uint8*)mem_malloc(MEM_IPUC_MODULE, size);
    if (NULL == p_gb_l3hash_master[lchip]->hash_status)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_l3hash_master[lchip]->hash_status, 0, size);


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_l3_hash_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3_hash_deinit(uint8 lchip)
{
    if (NULL == p_gb_l3hash_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gb_l3hash_master[lchip]->hash_status);

    /*free mid hash*/
    ctc_hash_traverse(p_gb_l3hash_master[lchip]->mid_hash, (hash_traversal_fn)_sys_greatbelt_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gb_l3hash_master[lchip]->mid_hash);

    /*free pointer hash*/
    ctc_hash_traverse(p_gb_l3hash_master[lchip]->pointer_hash, (hash_traversal_fn)_sys_greatbelt_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gb_l3hash_master[lchip]->pointer_hash);

    /*free ipv4 hash*/
    ctc_hash_traverse(p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_greatbelt_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4]);

    /*free ipv6 hash*/
    ctc_hash_traverse(p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_greatbelt_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gb_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6]);

    /*free duplicate hash*/
    ctc_hash_traverse(p_gb_l3hash_master[lchip]->duplicate_hash, (hash_traversal_fn)_sys_greatbelt_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gb_l3hash_master[lchip]->duplicate_hash);

    mem_free(p_gb_l3hash_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_l3_hash_state_show(uint8 lchip)
{
    uint32 i, j, tcam_count, tcam_count2, counter;
    uint8* p_tmp;

    SYS_L3_HASH_INIT_CHECK(lchip);

    switch (p_gb_l3hash_master[lchip]->hash_num_mode)
    {
    case 3:
        /* 2K buckets * 2 entries */
        counter = 2 * 1024;
        break;

    case 2:
        /* 4K buckets * 2 entries */
        counter = 4 * 1024;
        break;

    case 1:
        /* 8K buckets * 2 entries */
        counter = 8 * 1024;
        break;

    default:
        /* 16K buckets * 2 entries */
        counter = 16 * 1024;
        break;
    }

    j = 0;
    tcam_count2 = 0;
    SYS_IPUC_DBG_DUMP("          0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f\r\n");
    p_tmp = p_gb_l3hash_master[lchip]->hash_status;
    SYS_IPUC_DBG_DUMP("0x%03x :", tcam_count2);

    for (i = 0; i < counter; i++)
    {
        tcam_count = *p_tmp;
        p_tmp++;
        tcam_count &= 0xFF;
        SYS_IPUC_DBG_DUMP("0x%02x ", tcam_count);
        j++;
        if ((counter - i != 1) & (j == 16))
        {
            tcam_count2++;
            SYS_IPUC_DBG_DUMP("\r\n0x%03x :", tcam_count2);
            j = 0;
        }
    }

    SYS_IPUC_DBG_DUMP("\r\n");

    return CTC_E_NONE;
}


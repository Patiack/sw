/**
 @file sys_goldengate_l3_hash.c

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

#include "sys_goldengate_chip.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_ftm.h"

#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_l3_hash.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_ipuc.h"
#include "sys_goldengate_ipuc_db.h"
#include "sys_goldengate_ipmc.h"
#include "sys_goldengate_common.h"

#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
extern int32
_sys_goldengate_ipuc_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);


#define L3_HASH_DB_MASK         0x3FF
#define L3_HASH_RPF_TABLE_SIZE     63
sys_l3_hash_master_t* p_gg_l3hash_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

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

#define SYS_L3_HASH_INIT_CHECK() \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_l3hash_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

extern sys_ipuc_master_t* p_gg_ipuc_master[];
extern sys_ipuc_db_master_t* p_gg_ipuc_db_master[];
extern int32
_sys_goldengate_lpm_item_clean(uint8 lchip, sys_lpm_tbl_t* p_table);
/****************************************************************************
 *
* Function
*
*****************************************************************************/

#define __1_L3_HASH__
STATIC uint32
_sys_goldengate_l3_hash_ipv4_key_make(sys_l3_hash_t* p_hash)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;
    uint32 masklen = 0;

    masklen = p_hash->masklen == 8 ? 24: 16;

    DRV_PTR_VALID_CHECK(p_hash);

    a += (p_hash->ip32[0] >> masklen) & 0xFF;
    b += p_hash->vrf_id;
    b += p_hash->l4_dst_port;
    c += 15;
    MIX(a, b, c);

    return c % HASH_BLOCK_SIZE;
}

STATIC int32
_sys_goldengate_l3_hash_ipv4_key_cmp(sys_l3_hash_t* p_l3_hash_o, sys_l3_hash_t* p_l3_hash)
{
    uint32 mask = 0;

    DRV_PTR_VALID_CHECK(p_l3_hash_o);
    DRV_PTR_VALID_CHECK(p_l3_hash);

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
        mask = p_l3_hash->masklen == 16 ? 0xFFFF0000 : 0xFF000000;
    }

    if ((p_l3_hash->ip32[0] & mask) != (p_l3_hash_o->ip32[0] & mask))
    {
        return FALSE;
    }

    return TRUE;
}

STATIC uint32
_sys_goldengate_l3_hash_ipv6_key_make(sys_l3_hash_t* p_hash)
{
    uint32 a = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
    uint32 c = 0;

    DRV_PTR_VALID_CHECK(p_hash);

    a += (p_hash->ip32[0]) & 0xFF;
    b += (p_hash->ip32[0] >> 8) & 0xFF;
    c += (p_hash->ip32[0] >> 16) & 0xFF;
    MIX(a, b, c);

    c += 7;  /* vrfid and masklen and ipv6 route length - 12, unit is byte */

    a += (p_hash->ip32[0] >> 24) & 0xFF;
    b += p_hash->vrf_id;
    c += p_hash->masklen;
    FINAL(a, b, c);

    return c % HASH_BLOCK_SIZE;
}

STATIC int32
_sys_goldengate_l3_hash_ipv6_key_cmp(sys_l3_hash_t* p_l3_hash_o, sys_l3_hash_t* p_l3_hash)
{
    uint32 mask = 0;
    uint8  mask_len = 0;

    DRV_PTR_VALID_CHECK(p_l3_hash_o);
    DRV_PTR_VALID_CHECK(p_l3_hash);

    if (p_l3_hash->vrf_id != p_l3_hash_o->vrf_id)
    {
        return FALSE;
    }

    if (p_l3_hash->masklen != p_l3_hash_o->masklen)
    {
        return FALSE;
    }

    if (p_l3_hash->ip32[0] != p_l3_hash_o->ip32[0])
    {
        return FALSE;
    }

    if (p_l3_hash_o->masklen >= 64)
    {
        if (p_l3_hash->ip32[1] != p_l3_hash_o->ip32[1])
        {
            return FALSE;
        }
    }

    if (p_l3_hash_o->masklen >= 96)
    {
        if (p_l3_hash->ip32[2] != p_l3_hash_o->ip32[2])
        {
            return FALSE;
        }
    }

    mask_len = p_l3_hash_o->masklen % 32;
    IPV4_LEN_TO_MASK(mask, mask_len);
    if ((p_l3_hash->ip32[p_l3_hash->masklen/32] & mask) != (p_l3_hash_o->ip32[p_l3_hash_o->masklen/32] & mask))
    {
        return FALSE;
    }

    return TRUE;
}

int32
_sys_goldengate_l3_hash_get_hash_tbl(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_t** pp_hash)
{
    sys_l3_hash_t t_hash;

    t_hash.vrf_id = p_ipuc_info->vrf_id;
    t_hash.l4_dst_port = p_ipuc_info->l4_dst_port;
    t_hash.is_tcp_port = (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT) ? 1 : 0;

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        t_hash.ip32[0] = p_ipuc_info->ip.ipv6[3];
        t_hash.ip32[1] = p_ipuc_info->ip.ipv6[2];
        t_hash.ip32[2] = p_ipuc_info->ip.ipv6[1];
        t_hash.ip32[3] = p_ipuc_info->ip.ipv6[0];
        t_hash.masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    }
    else
    {
        t_hash.ip32[0] = p_ipuc_info->ip.ipv4[0];
        t_hash.masklen = p_gg_ipuc_master[lchip]->masklen_l;
    }

    *pp_hash = ctc_hash_lookup(p_gg_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], &t_hash);

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
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

    if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        p_hash->ip32[0] = p_ipuc_info->ip.ipv6[3];
        p_hash->ip32[1] = p_ipuc_info->ip.ipv6[2];
        p_hash->ip32[2] = p_ipuc_info->ip.ipv6[1];
        p_hash->ip32[3] = p_ipuc_info->ip.ipv6[0];
        p_hash->masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    }
    else
    {
        p_hash->ip32[0] = p_ipuc_info->ip.ipv4[0];
        p_hash->masklen = p_gg_ipuc_master[lchip]->masklen_l;
    }

    p_hash->ip_ver = SYS_IPUC_VER(p_ipuc_info);

    p_hash->n_table.offset = INVALID_POINTER & POINTER_OFFSET_MASK;
    p_hash->n_table.sram_index = INVALID_POINTER >> POINTER_OFFSET_BITS_LEN;

    ctc_hash_insert(p_gg_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], p_hash);
    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Add l3 hash node ip 0x%x vrfid %d\r\n", p_hash->ip32[0], p_hash->vrf_id);

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_lpm_tbl_t* p_table;
    sys_lpm_tbl_t* p_ntbl;
    sys_l3_hash_t* p_hash;
    uint16 idx;

    SYS_IPUC_DBG_FUNC();

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        return CTC_E_NONE;
    }

    if ((INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_POINTER]) ||
        (INVALID_POINTER_OFFSET != p_hash->hash_offset[LPM_TYPE_NEXTHOP]))
    {
        return CTC_E_NONE;
    }

    p_table = &p_hash->n_table;
    if ((NULL == p_table) || (0 == p_table->count))
    {
        _sys_goldengate_lpm_item_clean(lchip, p_table);

        for (idx = 0; idx < LPM_TBL_NUM; idx++)
        {
            TABLE_GET_PTR(p_table, (idx / 2), p_ntbl);
            if (p_ntbl)
            {
                TABLE_FREE_PTR(p_hash->n_table.p_ntable, (idx / 2));
                mem_free(p_ntbl);
            }
        }

        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Remove l3 hash node ip[0] 0x%x ip[1]  0x%x vrfid %d\r\n", p_hash->ip32[0], p_hash->ip32[1], p_hash->vrf_id);
        ctc_hash_remove(p_gg_l3hash_master[lchip]->l3_hash[SYS_IPUC_VER(p_ipuc_info)], p_hash);
        mem_free(p_hash);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_lpm_traverse_prefix(uint8 lchip,
                uint8 ip_ver,
                hash_traversal_fn   fn,
                void                *data)
{
    return ctc_hash_traverse_through(p_gg_l3hash_master[lchip]->l3_hash[ip_ver], fn, data);
}

#define __5_HARD_WRITE__

int32
_sys_goldengate_l3_hash_add_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 type)
{
    ds_t tcamkey, tcammask;
    ds_t lpmtcam_ad;
    ipv6_addr_t ipv6_data, ipv6_mask;
    tbl_entry_t tbl_ipkey;
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_table = NULL;
    uint32 pointer = 0;
    uint32 key_offset = 0;
    uint8  start_byte = 0;
    uint8  is_pointer = 0;
    uint32 tmp_ad_offset = 0;
    uint32 value;
    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tcamkey, 0, sizeof(tcamkey));
    sal_memset(&tcammask, 0, sizeof(tcammask));
    sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));

    sal_memset(&ipv6_data, 0, sizeof(ipv6_data));
    sal_memset(&ipv6_mask, 0, sizeof(ipv6_mask));

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }
    p_table = &p_hash->n_table;

    /* tcam move for tcam mode 0 */
    if ((!p_gg_ipuc_master[lchip]->tcam_mode) && (p_gg_ipuc_master[lchip]->tcam_move))
    {
        _sys_goldengate_ipuc_shift_key_up(lchip, p_ipuc_info);

        p_gg_ipuc_master[lchip]->tcam_move = 0;
    }

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        is_pointer = (p_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_l) ? 1 : 0;
    }
    else
    {
        is_pointer = (p_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_ipv6_l) ? 1 : 0;
    }

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        /* write ad */
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            if (is_pointer)
            {
                /* need get ad_offset from hardware */
                cmd = DRV_IOR(DsLpmTcamIpv4Route40Ad_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &lpmtcam_ad);
                tmp_ad_offset = GetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad);
            }
            else
            {
                tmp_ad_offset = p_ipuc_info->ad_offset;
            }
            sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));
            SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, tmp_ad_offset);

            key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }
        else
        {
            if (p_hash->is_pushed && p_hash->is_mask_valid
                && (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET))
            {
                /* need get ad_offset from hardware */
                cmd = DRV_IOR(DsLpmTcamIpv4Route40Ad_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);
                value = GetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad);
                sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));
                SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, value);
            }
            else
            {
                SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, INVALID_POINTER_OFFSET);
            }
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            pointer = (p_table->sram_index << POINTER_OFFSET_BITS_LEN) | (p_table->offset & POINTER_OFFSET_MASK);
            SetDsLpmTcamAd0(V, pointer_f, lpmtcam_ad, pointer);
            SetDsLpmTcamAd0(V, lpmPipelineValid_f, lpmtcam_ad, 1);
            SetDsLpmTcamAd0(V, indexMask_f, lpmtcam_ad, p_table->idx_mask);
            SetDsLpmTcamAd0(V, lpmStartByte_f, lpmtcam_ad, (p_gg_ipuc_master[lchip]->use_hash16 ? 2 : 1));

            key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
        }
        else
        {
            SetDsLpmTcamAd0(V, lpmPipelineValid_f, lpmtcam_ad, 0);
        }

        cmd = DRV_IOW(DsLpmTcamIpv4Route40Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad);

        if (p_ipuc_info->is_exist)/*no need to write key*/
        {
            return CTC_E_NONE;
        }
        /* write key */
        tbl_ipkey.data_entry = (uint32*)&tcamkey;
        tbl_ipkey.mask_entry = (uint32*)&tcammask;

        SetDsLpmTcamIpv440Key(V, ipAddr_f, tcamkey, p_ipuc_info->ip.ipv4[0]);
        SetDsLpmTcamIpv440Key(V, vrfId_f, tcamkey, p_ipuc_info->vrf_id);
        SetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, tcamkey, 0);

        if (p_gg_ipuc_master[lchip]->use_hash16)
        {
            SetDsLpmTcamIpv440Key(V, ipAddr_f, tcammask, 0xFFFF0000);
        }
        else
        {
            SetDsLpmTcamIpv440Key(V, ipAddr_f, tcammask, 0xFF000000);
        }
        SetDsLpmTcamIpv440Key(V, vrfId_f, tcammask, 0x7FF);
        SetDsLpmTcamIpv440Key(V, lpmTcamKeyType_f, tcammask, 0x1);

        cmd = DRV_IOW(DsLpmTcamIpv440Key_t, DRV_ENTRY_FLAG);

        DRV_IOCTL(lchip, key_offset, cmd, &tbl_ipkey);
    }
    else if (CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info))
    {
        /* write ad */
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            if (is_pointer)
            {
                /* need get ad_offset from hardware */
                cmd = DRV_IOR(DsLpmTcamIpv4Route160Ad_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_NEXTHOP], cmd, &lpmtcam_ad);
                tmp_ad_offset = GetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad);
            }
            else
            {
                tmp_ad_offset = p_ipuc_info->ad_offset;
            }
            sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));
            SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, tmp_ad_offset);

            key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }
        else
        {
            if (p_hash->is_pushed && p_hash->is_mask_valid
                && (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET))
            {
                /* need get ad_offset from hardware */
                cmd = DRV_IOR(DsLpmTcamIpv4Route160Ad_t, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);
                value = GetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad);
                sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));
                SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, value);
            }
            else
            {
                SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, INVALID_POINTER_OFFSET);
            }
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            pointer = (p_table->sram_index << POINTER_OFFSET_BITS_LEN) | p_table->offset;
            SetDsLpmTcamAd0(V, pointer_f, lpmtcam_ad, pointer);
            SetDsLpmTcamAd0(V, lpmPipelineValid_f, lpmtcam_ad, 1);
            SetDsLpmTcamAd0(V, indexMask_f, lpmtcam_ad, p_table->idx_mask);
            start_byte = p_gg_ipuc_master[lchip]->masklen_ipv6_l/8;
            SetDsLpmTcamAd0(V, lpmStartByte_f, lpmtcam_ad, start_byte);

            key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
        }
        else
        {
            SetDsLpmTcamAd0(V, lpmPipelineValid_f, lpmtcam_ad, 0);
        }

        cmd = DRV_IOW(DsLpmTcamIpv4Route160Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad);

        if (p_ipuc_info->is_exist)/*no need to write key*/
        {
            return CTC_E_NONE;
        }
        /* write key */
        /* DRV_SET_FIELD_A, ipv6_data must use little india */
        tbl_ipkey.data_entry = (uint32*)&tcamkey;
        tbl_ipkey.mask_entry = (uint32*)&tcammask;

        sal_memcpy(&ipv6_data, &(p_ipuc_info->ip.ipv6), sizeof(ipv6_addr_t));
        SetDsLpmTcamIpv6160Key0(A, ipAddr_f, tcamkey, ipv6_data);
        SetDsLpmTcamIpv6160Key0(V, vrfId_f, tcamkey, p_ipuc_info->vrf_id);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, tcamkey, 1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType1_f, tcamkey, 1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType2_f, tcamkey, 1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType3_f, tcamkey, 1);

        IPV6_LEN_TO_MASK(ipv6_mask, p_gg_ipuc_master[lchip]->masklen_ipv6_l);
        SetDsLpmTcamIpv6160Key0(A, ipAddr_f, tcammask, ipv6_mask);
        SetDsLpmTcamIpv6160Key0(V, vrfId_f, tcammask, 0x7FF);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType0_f, tcammask, 0x1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType1_f, tcammask, 0x1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType2_f, tcammask, 0x1);
        SetDsLpmTcamIpv6160Key0(V, lpmTcamKeyType3_f, tcammask, 0x1);

        cmd = DRV_IOW(DsLpmTcamIpv6160Key0_t, DRV_ENTRY_FLAG);
        if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
        {
            key_offset = key_offset/SYS_IPUC_TCAM_6TO4_STEP;
        }
        DRV_IOCTL(lchip, key_offset, cmd, &tbl_ipkey);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_remove_key_by_type(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_l3_hash_type_t hash_type)
{
    uint32 cmd = 0;
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_tbl = NULL;
    uint8 is_pointer = 0;
    uint32 pointer = 0;
    uint32 ad_tbl_id = 0;
    ds_t lpmtcam_ad;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    p_tbl = &p_hash->n_table;

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        is_pointer = (p_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_l) ? 1 : 0;
        ad_tbl_id = DsLpmTcamIpv4Route40Ad_t;
    }
    else
    {
        is_pointer = (p_ipuc_info->masklen > p_gg_ipuc_master[lchip]->masklen_ipv6_l) ? 1 : 0;
        ad_tbl_id = DsLpmTcamIpv4Route160Ad_t;
    }

    if ((p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        && (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET))
    {
        /* only need update tcam ad */
        cmd = DRV_IOR(ad_tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);
        if (is_pointer)
        {
            if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
            {
                SetDsLpmTcamAd0(V, pointer_f, lpmtcam_ad, INVALID_POINTER);
                SetDsLpmTcamAd0(V, indexMask_f, lpmtcam_ad, 0);
            }
            else
            {
                pointer = (p_tbl->sram_index << POINTER_OFFSET_BITS_LEN) | p_tbl->offset;
                SetDsLpmTcamAd0(V, pointer_f, lpmtcam_ad, pointer);
                SetDsLpmTcamAd0(V, indexMask_f, lpmtcam_ad, p_tbl->idx_mask);
            }
        }
        else
        {
            SetDsLpmTcamAd0(V, nexthop_f, lpmtcam_ad, INVALID_POINTER_OFFSET); /*nexthop is 18bit*/
        }

        cmd = DRV_IOW(ad_tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);
    }
    else
    {
        if (is_pointer)
        {
            if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
            {
                /* remove tcam key */
                p_ipuc_info->key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
            else
            {
                /* only need update tcam ad */
                cmd = DRV_IOR(ad_tbl_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);
                pointer = (p_tbl->sram_index << POINTER_OFFSET_BITS_LEN) | p_tbl->offset;
                SetDsLpmTcamAd0(V, pointer_f, lpmtcam_ad, pointer);
                SetDsLpmTcamAd0(V, indexMask_f, lpmtcam_ad, p_tbl->idx_mask);

                cmd = DRV_IOW(ad_tbl_id, DRV_ENTRY_FLAG);
                DRV_IOCTL(lchip, p_hash->hash_offset[LPM_TYPE_POINTER], cmd, &lpmtcam_ad);

                return CTC_E_NONE;
            }
        }
        else
        {
            /* remove tcam key */
            p_ipuc_info->key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }

        /* remove tcam key */
        if (p_gg_ipuc_master[lchip]->tcam_mode)
        {
            _sys_goldengate_ipuc_remove_key(lchip, p_ipuc_info);
        }
        else
        {
            _sys_goldengate_ipuc_shift_key_down(lchip, p_ipuc_info);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_add_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_l3_hash_add_key_by_type(lchip, p_ipuc_info, 0));

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    _sys_goldengate_l3_hash_remove_key_by_type(lchip, p_ipuc_info, 0);

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_update_tcam_ad(uint8 lchip, uint8 ip_ver, uint32 key_offset, uint32 ad_offset)
{
    DsLpmTcamIpv4Route40Ad_m lpmtcam_ad;
    DsLpmTcamIpv4Route160Ad_m lpmtcam_ad_v6;
    uint32 cmd;

    SYS_IPUC_DBG_FUNC();

    if (key_offset == INVALID_POINTER_OFFSET)
    {
        SYS_IPUC_DBG_ERROR("ERROR: key_offset is invalid, ad_offset is %d\r\n", ad_offset);
        return CTC_E_UNEXPECT;
    }

    if (ip_ver == CTC_IP_VER_4)
    {
        sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));

        cmd = DRV_IOR(DsLpmTcamIpv4Route40Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad);

        SetDsLpmTcamAd0(V, nexthop_f, &lpmtcam_ad, ad_offset);

        cmd = DRV_IOW(DsLpmTcamIpv4Route40Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad);
    }
    else
    {
        sal_memset(&lpmtcam_ad_v6, 0, sizeof(lpmtcam_ad_v6));

        cmd = DRV_IOR(DsLpmTcamIpv4Route160Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad_v6);

        SetDsLpmTcamAd0(V, nexthop_f, &lpmtcam_ad_v6, ad_offset);

        cmd = DRV_IOW(DsLpmTcamIpv4Route160Ad_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, key_offset, cmd, &lpmtcam_ad_v6);
    }

    return CTC_E_NONE;
}

#define __6_SOFT_HASH_INDEX__

int32
_sys_goldengate_l3_hash_alloc_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_key_type_t key_type = LPM_TYPE_POINTER;
    int32 ret = CTC_E_NONE;

    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l)
        {
            key_type = LPM_TYPE_NEXTHOP;
        }
    }
    else
    {
        if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l)
        {
            key_type = LPM_TYPE_NEXTHOP;
        }
    }

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        CTC_ERROR_RETURN(_sys_goldengate_l3_hash_add(lchip, p_ipuc_info));
        _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);

        ret = _sys_goldengate_ipuc_alloc_tcam_key_index(lchip, p_ipuc_info);
        if (ret)
        {
            _sys_goldengate_l3_hash_remove(lchip, p_ipuc_info);
            return CTC_E_NO_RESOURCE;
        }

        p_hash->hash_offset[key_type] = p_ipuc_info->key_offset;
        if (!p_gg_ipuc_master[lchip]->tcam_mode)
        {
            p_gg_ipuc_master[lchip]->tcam_move = 1;
        }
        p_ipuc_info->is_exist = 0;
    }
    else
    {
        if (INVALID_POINTER_OFFSET == p_hash->hash_offset[key_type])
        {
            p_hash->hash_offset[key_type] = p_hash->hash_offset[!key_type];
        }
        p_ipuc_info->key_offset = p_hash->hash_offset[key_type];
        p_ipuc_info->is_exist = 1;
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "use exist tcam index %d\r\n", p_hash->hash_offset[key_type]);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_l3_hash_free_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    sys_lpm_tbl_t* p_tbl = NULL;
    sys_lpm_key_type_t key_type = LPM_TYPE_POINTER;

    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_l)
        {
            key_type = LPM_TYPE_NEXTHOP;
        }
    }
    else
    {
        if (p_ipuc_info->masklen == p_gg_ipuc_master[lchip]->masklen_ipv6_l)
        {
            key_type = LPM_TYPE_NEXTHOP;
        }
    }

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL when free hash key index\r\n");
        return CTC_E_UNEXPECT;
    }
    else
    {
        p_tbl = &p_hash->n_table;
        if (key_type == LPM_TYPE_POINTER)
        {
            if ((!p_tbl) || (0 == p_tbl[LPM_TABLE_INDEX0].count))
            {
                p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
            }
        }
        else
        {
            p_hash->hash_offset[key_type] = INVALID_POINTER_OFFSET;
        }

        if ((INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_NEXTHOP])
            && (INVALID_POINTER_OFFSET == p_hash->hash_offset[LPM_TYPE_POINTER]))
        {
            _sys_goldengate_ipuc_free_tcam_ad_index(lchip, p_ipuc_info);
        }

        _sys_goldengate_l3_hash_remove(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

#define __8_INIT_AND_SHOW__
int32
sys_goldengate_l3_hash_init(uint8 lchip, ctc_ip_ver_t ip_version)
{

    if (NULL != p_gg_l3hash_master[lchip])
    {
        return CTC_E_NONE;
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    p_gg_l3hash_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_master_t));
    if (NULL == p_gg_l3hash_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_l3hash_master[lchip], 0, sizeof(sys_ipuc_master_t));

    p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4] = ctc_hash_create(p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4] / HASH_BLOCK_SIZE, HASH_BLOCK_SIZE,
                                            (hash_key_fn)_sys_goldengate_l3_hash_ipv4_key_make,
                                            (hash_cmp_fn)_sys_goldengate_l3_hash_ipv4_key_cmp);

    p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6] = ctc_hash_create(p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_6] / HASH_BLOCK_SIZE, HASH_BLOCK_SIZE,
                                            (hash_key_fn)_sys_goldengate_l3_hash_ipv6_key_make,
                                            (hash_cmp_fn)_sys_goldengate_l3_hash_ipv6_key_cmp);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_l3_hash_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_l3_hash_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_l3hash_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free ipv4 hash*/
    ctc_hash_traverse(p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_goldengate_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_4]);

    /*free ipv6 hash*/
    ctc_hash_traverse(p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_goldengate_l3_hash_free_node_data, NULL);
    ctc_hash_free(p_gg_l3hash_master[lchip]->l3_hash[CTC_IP_VER_6]);

    mem_free(p_gg_l3hash_master[lchip]);

    return CTC_E_NONE;
}


/**
 @file sys_greatbelt_ipuc_db.c

 @date 2009-12-09

 @version v2.0

 The file contains all ipuc related function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_linklist.h"
#include "ctc_vector.h"
#include "ctc_hash.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_sort.h"
#include "sys_greatbelt_rpf_spool.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_l3_hash.h"
#include "sys_greatbelt_ipuc.h"
#include "sys_greatbelt_ipuc_db.h"
#include "sys_greatbelt_lpm.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
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

#define SYS_IPUC_AD_SPOOL_BLOCK_SIZE 1024

#define SYS_IP_CHECK_VERSION(ver)                        \
    {                                                               \
        if ((!p_gb_ipuc_master[lchip]) || (!p_gb_ipuc_db_master[lchip]) || !p_gb_ipuc_master[lchip]->version_en[ver])     \
        {                                                           \
            return CTC_E_IPUC_VERSION_DISABLE;                      \
        }                                                           \
    }

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

extern sys_ipuc_master_t* p_gb_ipuc_master[CTC_MAX_LOCAL_CHIP_NUM];
extern int32
_sys_greatbelt_ipuc_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);
extern int32
_sys_greatbelt_ipuc_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);
extern int32
_sys_greatbelt_ipuc_write_ipda(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt);

extern uint32
_sys_greatbelt_ipuc_do_lpm_check(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, bool* do_lpm);

sys_ipuc_db_master_t* p_gb_ipuc_db_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief function of make hash key for ipuc table

 @param[in] p_ipuc_info, information should be maintained for ipuc

 @return CTC_E_XXX
 */
STATIC uint32
_sys_greatbelt_ipv4_hash_make(sys_ipuc_info_t* p_ipuc_info)
{
    uint32 a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32)96) << 2);

    a += p_ipuc_info->ip.ipv4[0];
    b += p_ipuc_info->vrf_id;
    b += (p_ipuc_info->masklen << 16);
    b += p_ipuc_info->l4_dst_port;
    MIX(a, b, c);

    return (c & IPUC_IPV4_HASH_MASK);
}

STATIC uint32
_sys_greatbelt_ipv6_hash_make(sys_ipuc_info_t* p_ipuc_info)
{
    uint32 a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32)192) << 2);

    a += p_ipuc_info->ip.ipv6[0];
    b += p_ipuc_info->ip.ipv6[1];
    c += p_ipuc_info->ip.ipv6[2];
    MIX(a, b, c);

    a += p_ipuc_info->ip.ipv6[3];
    b += p_ipuc_info->vrf_id;
    b += (p_ipuc_info->masklen << 16);
    MIX(a, b, c);

    FINAL(a, b, c);

    return (c & IPUC_IPV6_HASH_MASK);
}

STATIC uint32
_sys_greatbelt_ipv4_nat_hash_make(sys_ipuc_nat_sa_info_t* p_nat_info)
{
    uint32 a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32)96) << 2);

    a += p_nat_info->ipv4[0];
    b += p_nat_info->vrf_id;
    b += (p_nat_info->l4_src_port << 16);
    c += p_nat_info->is_tcp_port;
    MIX(a, b, c);

    return (c & IPUC_IPV4_HASH_MASK);
}

STATIC uint32
_sys_greatbelt_ipuc_db_rpf_profile_hash_make(sys_ipuc_db_rpf_profile_t* p_rpf_profile)
{
    uint32 a, b, c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32)96) << 2);

    a += p_rpf_profile->key.nh_id;
    b += p_rpf_profile->key.route_flag;
    MIX(a, b, c);

    return (c & IPUC_RPF_PROFILE_HASH_MASK);
}

STATIC bool
_sys_greatbelt_ipv4_hash_cmp(sys_ipuc_info_t* p_ipuc_info1, sys_ipuc_info_t* p_ipuc_info)
{
    if (p_ipuc_info1->vrf_id != p_ipuc_info->vrf_id)
    {
        return FALSE;
    }

    if (p_ipuc_info1->masklen != p_ipuc_info->masklen)
    {
        return FALSE;
    }

    if (p_ipuc_info1->ip.ipv4[0] != p_ipuc_info->ip.ipv4[0])
    {
        return FALSE;
    }

    if (p_ipuc_info1->l4_dst_port != p_ipuc_info->l4_dst_port)
    {
        return FALSE;
    }

    if ((p_ipuc_info1->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT)
        != (p_ipuc_info->route_flag & SYS_IPUC_FLAG_IS_TCP_PORT))
    {
        return FALSE;
    }

    return TRUE;
}

STATIC bool
_sys_greatbelt_ipv4_nat_hash_cmp(sys_ipuc_nat_sa_info_t* p_nat_info1, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    if (p_nat_info1->vrf_id != p_nat_info->vrf_id)
    {
        return FALSE;
    }

    if (p_nat_info1->l4_src_port != p_nat_info->l4_src_port)
    {
        return FALSE;
    }

    if (p_nat_info1->is_tcp_port != p_nat_info->is_tcp_port)
    {
        return FALSE;
    }

    if (p_nat_info1->ipv4[0] != p_nat_info->ipv4[0])
    {
        return FALSE;
    }

    return TRUE;
}

STATIC bool
_sys_greatbelt_ipv6_hash_cmp(sys_ipuc_info_t* p_ipuc_info1, sys_ipuc_info_t* p_ipuc_info)
{
    if (p_ipuc_info1->vrf_id != p_ipuc_info->vrf_id)
    {
        return FALSE;
    }

    if (p_ipuc_info1->masklen != p_ipuc_info->masklen)
    {
        return FALSE;
    }

    if (p_ipuc_info1->ip.ipv6[3] != p_ipuc_info->ip.ipv6[3])
    {
        return FALSE;
    }

    if (p_ipuc_info1->ip.ipv6[2] != p_ipuc_info->ip.ipv6[2])
    {
        return FALSE;
    }

    if (p_ipuc_info1->ip.ipv6[1] != p_ipuc_info->ip.ipv6[1])
    {
        return FALSE;
    }

    if (p_ipuc_info1->ip.ipv6[0] != p_ipuc_info->ip.ipv6[0])
    {
        return FALSE;
    }

    return TRUE;
}

STATIC uint32
_sys_greatbelt_ipuc_lpm_ad_hash_make(sys_ipuc_ad_spool_t* p_ipuc_ad)
{
    uint32 a, b, c;
    uint32* k = (uint32*)p_ipuc_ad;
    uint8*  k8;
    uint8   length = sizeof(p_ipuc_ad->nhid) + sizeof(p_ipuc_ad->route_flag) + sizeof(p_ipuc_ad->l3if);

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
_sys_greatbelt_ipuc_lpm_ad_hash_cmp(sys_ipuc_ad_spool_t* p_bucket, sys_ipuc_ad_spool_t* p_new)
{
    if (!p_bucket || !p_new)
    {
        return FALSE;
    }

    if (p_bucket->nhid != p_new->nhid)
    {
        return FALSE;
    }

    if (p_bucket->route_flag != p_new->route_flag)
    {
        return FALSE;
    }

    if (p_bucket->l3if != p_new->l3if)
    {
        return FALSE;
    }

    return TRUE;
}

int32
_sys_greatbelt_ipuc_lpm_ad_profile_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, int32* ad_spool_ref_cnt)
{
    uint32 table_id = DsIpDa_t;
    sys_ipuc_ad_spool_t* p_ipuc_ad_new = NULL;
    sys_ipuc_ad_spool_t* p_ipuc_ad_get = NULL;
    sys_ipuc_ad_spool_t ipuc_ad_old;
    uint32 ad_offset;
    uint32 obsolete_flag = 0;
    int32 ret = CTC_E_NONE;

    CTC_PTR_VALID_CHECK(p_ipuc_info);

    sal_memset(&ipuc_ad_old, 0, sizeof(ipuc_ad_old));

    p_ipuc_ad_new = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_ad_spool_t));
    if (NULL == p_ipuc_ad_new)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_ipuc_ad_new, 0, sizeof(sys_ipuc_ad_spool_t));
    p_ipuc_ad_new->nhid = p_ipuc_info->nh_id;
    obsolete_flag = CTC_IPUC_FLAG_NEIGHBOR | CTC_IPUC_FLAG_CONNECT | CTC_IPUC_FLAG_STATS_EN
                               | SYS_IPUC_FLAG_MERGE_KEY | SYS_IPUC_FLAG_IS_BIND_NH;
    p_ipuc_ad_new->route_flag = p_ipuc_info->route_flag & (~obsolete_flag);
    p_ipuc_ad_new->l3if = p_ipuc_info->l3if;
    if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH))
    {
        p_ipuc_ad_new->binding_nh  =1;
    }

    ret = ctc_spool_add(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_new, NULL, &p_ipuc_ad_get);
    if (p_ipuc_ad_get)
    {
        p_ipuc_info->binding_nh = p_ipuc_ad_get->binding_nh;
    }
    *ad_spool_ref_cnt = ctc_spool_get_refcnt(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_new);

    if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(p_ipuc_ad_new);
    }

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        return ret;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            ret = sys_greatbelt_ftm_alloc_table_offset(lchip, table_id, 0, 1, &ad_offset);
            if (ret)
            {
                ctc_spool_remove(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_get, NULL);
                mem_free(p_ipuc_ad_new);
                return ret;
            }

            p_ipuc_ad_get->ad_index = ad_offset;
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: alloc ad_offset = %d\n", ad_offset);
        }

        p_ipuc_info->ad_offset = p_ipuc_ad_get->ad_index;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_lpm_ad_profile_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 table_id = DsIpDa_t;
    uint32            ad_offset = 0;
    int32             ret = 0;
    uint32            obsolete_flag = 0;
    sys_ipuc_ad_spool_t ipuc_ad;
    sys_ipuc_ad_spool_t* p_ipuc_ad_del = NULL;

    CTC_PTR_VALID_CHECK(p_ipuc_info);
    sal_memset(&ipuc_ad, 0, sizeof(ipuc_ad));

    ipuc_ad.nhid = p_ipuc_info->nh_id;
    obsolete_flag = CTC_IPUC_FLAG_NEIGHBOR | CTC_IPUC_FLAG_CONNECT | CTC_IPUC_FLAG_STATS_EN
                               | SYS_IPUC_FLAG_MERGE_KEY | SYS_IPUC_FLAG_IS_BIND_NH;
    ipuc_ad.route_flag = p_ipuc_info->route_flag & (~obsolete_flag);
    ipuc_ad.l3if = p_ipuc_info->l3if;

    p_ipuc_ad_del = ctc_spool_lookup(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool, &ipuc_ad);
    if (!p_ipuc_ad_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool, &ipuc_ad, NULL);
    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            /* free ad offset */
            ad_offset = p_ipuc_ad_del->ad_index;
            CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, table_id, 0, 1, ad_offset));
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: remove ad_offset = %d\n",  ad_offset);
            /* free memory */
            mem_free(p_ipuc_ad_del);
        }
    }

    return CTC_E_NONE;

}

STATIC bool
_sys_greatbelt_ipuc_db_rpf_profile_hash_cmp(sys_ipuc_db_rpf_profile_t* p_rpf_profile1,
                                            sys_ipuc_db_rpf_profile_t* p_rpf_profile2)
{
    if (p_rpf_profile1->key.nh_id != p_rpf_profile2->key.nh_id)
    {
        return FALSE;
    }

    if (p_rpf_profile1->key.route_flag != p_rpf_profile2->key.route_flag)
    {
        return FALSE;
    }

    return TRUE;
}

/**
 @brief function of lookup ip route information

 @param[in] pp_ipuc_info, information used for lookup ipuc entry
 @param[out] pp_ipuc_info, information of ipuc entry finded

 @return CTC_E_XXX
 */
int32
_sys_greatbelt_ipuc_db_lookup(uint8 lchip, sys_ipuc_info_t** pp_ipuc_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;

    p_ipuc_info = ctc_hash_lookup(p_gb_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(*pp_ipuc_info)], *pp_ipuc_info);

    *pp_ipuc_info = p_ipuc_info;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_nat_db_lookup(uint8 lchip, sys_ipuc_nat_sa_info_t** pp_ipuc_nat_info)
{
    sys_ipuc_nat_sa_info_t* p_ipuc_nat_info = NULL;

    p_ipuc_nat_info = ctc_hash_lookup(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash, *pp_ipuc_nat_info);

    *pp_ipuc_nat_info = p_ipuc_nat_info;

    return CTC_E_NONE;
}

sys_ipuc_db_rpf_profile_t*
_sys_greatbelt_ipuc_db_rpf_profile_lookup(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile_info)
{
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_result = NULL;

    p_rpf_profile_result = ctc_hash_lookup(p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash, p_rpf_profile_info);

    return p_rpf_profile_result;
}

STATIC int32
_sys_greatbelt_ipuc_db_get_info_cb(void* p_data_o, void* p_data_m)
{
    sys_ipuc_info_list_t** pp_info_list;
    sys_ipuc_info_t* p_ipuc_info = NULL;

    pp_info_list = (sys_ipuc_info_list_t**)p_data_m;
    p_ipuc_info = (sys_ipuc_info_t*)p_data_o;

    (*pp_info_list)->p_ipuc_info = p_ipuc_info;

    (*pp_info_list)->p_next_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_info_list_t));
    if (NULL == (*pp_info_list)->p_next_info)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset((*pp_info_list)->p_next_info, 0, sizeof(sys_ipuc_info_list_t));

    *pp_info_list = (*pp_info_list)->p_next_info;
    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_db_get_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list)
{
    uint8 ip_ver;

    for (ip_ver = CTC_IP_VER_4; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        _sys_greatbelt_ipuc_db_traverse(lchip, ip_ver, _sys_greatbelt_ipuc_db_get_info_cb, &p_info_list);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_db_sort_slist(uint8 lchip, sys_ipuc_arrange_info_t* phead, sys_ipuc_arrange_info_t* pend)
{
    sys_ipuc_arrange_info_t* phead_node = NULL;
    sys_ipuc_arrange_info_t* pcurr_node = NULL;
    sys_ipuc_arrange_info_t* pnext_node = NULL;
    sys_ipuc_arrange_data_t* p_data     = NULL;

    if ((NULL == phead) || (NULL == pend) || (phead == pend))
    {
        return CTC_E_NONE;
    }

    phead_node = phead;

    while(phead_node != pend)
    {
        pcurr_node = phead_node;
        pnext_node = pcurr_node->p_next_info;
        while(pnext_node)
        {
            if (pnext_node->p_data->start_offset <= pcurr_node->p_data->start_offset)
            {
                p_data = pcurr_node->p_data;
                pcurr_node->p_data = pnext_node->p_data;
                pnext_node->p_data = p_data;
            }
            pnext_node = pnext_node->p_next_info;
        }
        phead_node = phead_node->p_next_info;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_db_anylize_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list, sys_ipuc_arrange_info_t** pp_arrange_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX];
    sys_lpm_tbl_t* p_table_now = NULL;
    sys_lpm_tbl_t* p_table_pre = NULL;
    sys_ipuc_arrange_info_t** pp_arrange_tbl;
    sys_ipuc_arrange_info_t* p_arrange_tbl_head[LPM_TABLE_MAX];
    sys_ipuc_arrange_info_t* p_arrange_now = NULL;
    sys_ipuc_arrange_info_t* p_arrange_pre = NULL;
    sys_ipuc_info_list_t* p_info_list_t = NULL;
    sys_ipuc_info_list_t* p_info_list_old = NULL;
    uint8 i;
    uint32 pointer;
    uint8 sram_index;
    uint16 size;

    p_info_list_t = p_info_list;


    pp_arrange_tbl = pp_arrange_info;

    sal_memset(p_arrange_tbl_head, 0, sizeof(sys_ipuc_arrange_info_t*) * LPM_TABLE_MAX);
    sal_memset(p_table, 0, sizeof(sys_lpm_tbl_t*) * LPM_TABLE_MAX);

    /* get all entry info */
    while ((NULL != p_info_list_t) && (NULL != p_info_list_t->p_ipuc_info))
    {
        p_ipuc_info = p_info_list_t->p_ipuc_info;
        p_info_list_old = p_info_list_t;
        p_info_list_t = p_info_list_t->p_next_info;

        if (CTC_FLAG_ISSET(p_ipuc_info->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
        {
            mem_free(p_info_list_old);
            p_info_list_old = NULL;
            continue;
        }

        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_ipuc_info 0x%x  masklen = %d\n",
                             p_ipuc_info->ip.ipv4[0], p_ipuc_info->masklen);
        }
        else
        {
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_ipuc_info %x:%x:%x:%x  masklen = %d\n",
                             p_ipuc_info->ip.ipv6[3], p_ipuc_info->ip.ipv6[2],
                             p_ipuc_info->ip.ipv6[1], p_ipuc_info->ip.ipv6[0], p_ipuc_info->masklen);
        }

        for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
        {
            TABEL_GET_STAGE(i, p_ipuc_info, p_table[i]);
            if (NULL == p_table[i])
            {
                break;
            }

            pointer = p_table[i]->offset | (p_table[i]->sram_index << 16);
            if (INVALID_POINTER == pointer)
            {
                continue;
            }

            sram_index = p_table[i]->sram_index & 0x3;
            if (NULL == pp_arrange_tbl[sram_index])
            {
                pp_arrange_tbl[sram_index] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_info_t));
                if (NULL == pp_arrange_tbl[sram_index])
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(pp_arrange_tbl[sram_index], 0, sizeof(sys_ipuc_arrange_info_t));
                (pp_arrange_tbl[sram_index])->p_data = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_data_t));
                if (NULL == (pp_arrange_tbl[sram_index])->p_data)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset((pp_arrange_tbl[sram_index])->p_data, 0, sizeof(sys_ipuc_arrange_data_t));

                if (NULL == p_arrange_tbl_head[sram_index])
                {
                    p_arrange_tbl_head[sram_index] = pp_arrange_tbl[sram_index];
                }
            }
            else
            {
                (pp_arrange_tbl[sram_index])->p_next_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_info_t));
                if (NULL == (pp_arrange_tbl[sram_index])->p_next_info)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset((pp_arrange_tbl[sram_index])->p_next_info, 0, sizeof(sys_ipuc_arrange_info_t));
                (pp_arrange_tbl[sram_index])->p_next_info->p_data = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_data_t));
                if (NULL == (pp_arrange_tbl[sram_index])->p_next_info->p_data)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset((pp_arrange_tbl[sram_index])->p_next_info->p_data, 0, sizeof(sys_ipuc_arrange_data_t));

                pp_arrange_tbl[sram_index] = (pp_arrange_tbl[sram_index])->p_next_info;
            }

            (pp_arrange_tbl[sram_index])->p_data->idx_mask = p_table[i]->idx_mask;
            (pp_arrange_tbl[sram_index])->p_data->start_offset = pointer & 0xFFFF;
            (pp_arrange_tbl[sram_index])->p_data->p_ipuc_info = p_ipuc_info;
            (pp_arrange_tbl[sram_index])->p_data->stage = i;
            LPM_INDEX_TO_SIZE((pp_arrange_tbl[sram_index])->p_data->idx_mask, size);
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "store ipuc table[%d] sram %d pointer = %4d  size = %d \n",
                             (pp_arrange_tbl[sram_index])->p_data->stage, sram_index, (pp_arrange_tbl[sram_index])->p_data->start_offset, size);

        }

        mem_free(p_info_list_old);
        p_info_list_old = NULL;
    }

    if (p_info_list_t && p_info_list_t->p_next_info)
    {
        mem_free(p_info_list_t->p_next_info);
        p_info_list_t->p_next_info = NULL;
    }

    if (p_info_list_t)
    {
        mem_free(p_info_list_t);
        p_info_list_t = NULL;
    }

    for (i = LPM_TABLE_INDEX0; i < LPM_TABLE_MAX; i++)
    {
        _sys_greatbelt_ipuc_db_sort_slist(lchip, p_arrange_tbl_head[i], pp_arrange_tbl[i]);
        p_arrange_now = p_arrange_tbl_head[i];
        p_arrange_pre = p_arrange_tbl_head[i];
        p_table_pre = NULL;

        while (NULL != p_arrange_now)
        {
            p_ipuc_info = p_arrange_now->p_data->p_ipuc_info;
             /*p_table_now = p_lpm_result->p_table[p_arrange_now->p_data->stage];*/
            TABEL_GET_STAGE(p_arrange_now->p_data->stage, p_ipuc_info, p_table_now);
            if (p_table_pre == p_table_now)
            {
                p_arrange_pre->p_next_info = p_arrange_now->p_next_info;
                mem_free(p_arrange_now->p_data);
                mem_free(p_arrange_now);
                p_arrange_now = p_arrange_pre->p_next_info;
                continue;
            }

            p_arrange_pre = p_arrange_now;
            p_table_pre = p_table_now;

            p_arrange_now = p_arrange_now->p_next_info;
        }

        pp_arrange_tbl[i] = p_arrange_tbl_head[i];
    }

    return CTC_E_NONE;
}

/**
 @brief function of add ip route information

 @param[in] p_ipuc_info, information should be maintained for ipuc

 @return CTC_E_XXX
 */
int32
_sys_greatbelt_ipuc_db_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{

    ctc_hash_insert(p_gb_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_nat_db_add(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{

    ctc_hash_insert(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash, p_nat_info);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_db_add_rpf_profile(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_update_rpf_profile_info,
                                       sys_ipuc_db_rpf_profile_t** pp_rpf_profile_result)
{
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_result = NULL;
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_info = NULL;

    CTC_PTR_VALID_CHECK(p_update_rpf_profile_info);

    p_rpf_profile_result = _sys_greatbelt_ipuc_db_rpf_profile_lookup(lchip, p_update_rpf_profile_info);

    if (NULL == p_rpf_profile_result)
    {
        p_rpf_profile_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_db_rpf_profile_t));

        if (NULL == p_rpf_profile_info)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_rpf_profile_info, 0, sizeof(sys_ipuc_db_rpf_profile_t));

        p_rpf_profile_info->key.nh_id = p_update_rpf_profile_info->key.nh_id;
        p_rpf_profile_info->key.route_flag = p_update_rpf_profile_info->key.route_flag;
        ctc_hash_insert(p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash, p_rpf_profile_info);
        *pp_rpf_profile_result = p_rpf_profile_info;
    }
    else
    {
        p_rpf_profile_result->ad.counter++;
        *pp_rpf_profile_result = p_rpf_profile_result;
    }

    return CTC_E_NONE;
}

/**
 @brief function of add ip route information

 @param[in] p_ipuc_info, information should be maintained for ipuc

 @return CTC_E_XXX
 */
int32
_sys_greatbelt_ipuc_db_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    ctc_hash_remove(p_gb_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_nat_db_remove(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    ctc_hash_remove(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash, p_nat_info);
    mem_free(p_nat_info);

    return CTC_E_NONE;
}


int32
_sys_greatbelt_ipuc_db_remove_rpf_profile(uint8 lchip, sys_ipuc_db_rpf_profile_t* p_rpf_profile_info)
{
    sys_ipuc_db_rpf_profile_t* p_rpf_profile_rslt = NULL;

    p_rpf_profile_rslt = _sys_greatbelt_ipuc_db_rpf_profile_lookup(lchip, p_rpf_profile_info);

    if (0 == p_rpf_profile_rslt->ad.counter)
    {
        ctc_hash_remove(p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash, p_rpf_profile_rslt);
        mem_free(p_rpf_profile_rslt);
    }
    else
    {
        p_rpf_profile_rslt->ad.counter--;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_alloc_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 ad_offset = 0;
    uint16 i = 0;
    sys_ipuc_info_t* p_tmp_ipuc_info = 0;

    if (CTC_FLAG_ISSET(p_gb_ipuc_master[lchip]->lookup_mode[SYS_IPUC_VER(p_ipuc_info)], SYS_IPUC_TCAM_LOOKUP))
    {
        if (p_gb_ipuc_master[lchip]->tcam_mode)
        {
            /*1. init*/
            if ((SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6) && p_gb_ipuc_db_master[lchip]->tcam_share_mode)
            {
                p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)] - p_ipuc_info->masklen + CTC_IPV4_ADDR_LEN_IN_BIT + 1;
            }
            else
            {
                p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)] - p_ipuc_info->masklen;
            }

            /*2. do it*/
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_alloc_offset
                                 (lchip, &p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)], &ad_offset));
            p_ipuc_info->ad_offset = ad_offset;

            SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->ad_offset)
                = p_ipuc_info;
        }
        else
        {
            if ((p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]+1)
                > p_gb_ipuc_db_master[lchip]->tcam_max_count[SYS_IPUC_VER(p_ipuc_info)])
            {
                return CTC_E_NO_RESOURCE;
            }

            for (i = 0; i < p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; i++)
            {
                p_tmp_ipuc_info = p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i];
                if (p_tmp_ipuc_info->masklen > p_ipuc_info->masklen)
                {
                    continue;
                }
                else
                {
                    p_ipuc_info->ad_offset = i;
                    return CTC_E_NONE;
                }
            }

            p_ipuc_info->ad_offset = p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)];
        }
    }
    else
    {
        return CTC_E_NO_RESOURCE;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_free_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 ad_offset = p_ipuc_info->ad_offset;

    if (p_gb_ipuc_master[lchip]->tcam_mode)
    {
        /*1. init*/
        if ((SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_6) && p_gb_ipuc_db_master[lchip]->tcam_share_mode)
        {
            p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
            p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)] - p_ipuc_info->masklen + CTC_IPV4_ADDR_LEN_IN_BIT + 1;
        }
        else
        {
            p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
            p_gb_ipuc_master[lchip]->max_mask_len[SYS_IPUC_VER(p_ipuc_info)] - p_ipuc_info->masklen;
        }

        /*2. do it*/
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->ad_offset) = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_free_offset(lchip, &p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)],  ad_offset));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipv4_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info;
    ds_ip_da_t dsipda;
    uint32 cmdr, cmdw;

    p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset);

    if (NULL == p_ipuc_info)
    {
        return CTC_E_NONE;
    }

    if (new_offset == old_offset)
    {
        return CTC_E_ENTRY_EXIST;
    }

    /* add key to new offset */
    cmdr = DRV_IOR(p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_4], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_4], DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, old_offset, cmdr, &dsipda);
    DRV_IOCTL(lchip, new_offset, cmdw, &dsipda);

    p_ipuc_info->ad_offset = new_offset;
    _sys_greatbelt_ipuc_write_key(lchip, p_ipuc_info);

    /* remove key from old offset */
    p_ipuc_info->ad_offset = old_offset;
    _sys_greatbelt_ipuc_remove_key(lchip, p_ipuc_info);

    p_ipuc_info->ad_offset = new_offset;

    SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], new_offset) =
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset);
    SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset) = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipv6_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info;
    ds_ip_da_t dsipda;
    /* ds_ip_sa_t dsipsa;*/
    uint32 cmdr, cmdw;

    p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
    if (NULL == p_ipuc_info)
    {
        return CTC_E_NONE;
    }

    if (new_offset == old_offset)
    {
        return CTC_E_ENTRY_EXIST;
    }

    /* add key to new offset */
    cmdr = DRV_IOR(p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_6], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[CTC_IP_VER_6], DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, old_offset, cmdr, &dsipda);
    DRV_IOCTL(lchip, new_offset, cmdw, &dsipda);


    p_ipuc_info->ad_offset = new_offset;
    _sys_greatbelt_ipuc_write_key(lchip, p_ipuc_info);

    /* remove key from old offset */
    p_ipuc_info->ad_offset = old_offset;
    _sys_greatbelt_ipuc_remove_key(lchip, p_ipuc_info);

    p_ipuc_info->ad_offset = new_offset;
    SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], new_offset) =
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
    SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset) = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    uint8 is_v6 = 0;

    p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
    if (p_ipuc_info && (p_ipuc_info->ad_offset == old_offset))
    {
        is_v6 = 1;
    }

    if (is_v6)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipv6_syn_key(lchip, new_offset, old_offset));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipv4_syn_key(lchip, new_offset, old_offset));
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_ipuc_key_move(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, uint8 is_up)
{
    ds_ip_da_t dsipda;
    uint32 cmdr, cmdw;
    uint32 new_offset = 0;
    uint32 old_offset = 0;

    /* add key to new offset */
    cmdr = DRV_IOR(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gb_ipuc_master[lchip]->tcam_da_table[SYS_IPUC_VER(p_ipuc_info)], DRV_ENTRY_FLAG);

    old_offset = p_ipuc_info->ad_offset;
    if (is_up)
    {
        new_offset = p_ipuc_info->ad_offset + 1;
    }
    else
    {
        new_offset = p_ipuc_info->ad_offset - 1;
    }

    DRV_IOCTL(lchip, old_offset, cmdr, &dsipda);
    DRV_IOCTL(lchip, new_offset, cmdw, &dsipda);

    p_ipuc_info->ad_offset = new_offset;
    _sys_greatbelt_ipuc_write_key(lchip, p_ipuc_info);

    /* remove key from old offset */
    p_ipuc_info->ad_offset = old_offset;
    _sys_greatbelt_ipuc_remove_key(lchip, p_ipuc_info);

    p_ipuc_info->ad_offset = new_offset;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_write_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, sys_rpf_rslt_t* p_rpf_rslt)
{
    uint16 j = 0;
    sys_ipuc_info_t* p_tmp_ipuc_info = 0;

    SYS_IPUC_DBG_FUNC();

    if (!p_gb_ipuc_master[lchip]->tcam_mode)
    {
        for (j = p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; j > p_ipuc_info->ad_offset; j --)
        {
            p_tmp_ipuc_info = p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][j-1];
            _sys_greatbelt_ipuc_key_move(lchip, p_tmp_ipuc_info, 1);
            p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][j] = p_tmp_ipuc_info;
        }

        p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->ad_offset] = p_ipuc_info;
        p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] ++;
    }

    _sys_greatbelt_ipuc_write_ipda(lchip, p_ipuc_info, p_rpf_rslt);
    _sys_greatbelt_ipuc_write_key(lchip, p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_remove_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    int32 ret = CTC_E_NONE;
    uint16 i = 0;
    uint16 index = 0;
    uint8 flag = 0;
    sys_ipuc_info_t* p_tmp_ipuc_info = 0;

    SYS_IPUC_DBG_FUNC();

    if (p_gb_ipuc_master[lchip]->tcam_mode)
    {
        _sys_greatbelt_ipuc_remove_key(lchip, p_ipuc_info);
    }
    else
    {
        index = p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] - 1;
        for (i = p_ipuc_info->ad_offset; i < index; i ++)
        {
            p_tmp_ipuc_info = p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i+1];
            _sys_greatbelt_ipuc_key_move(lchip, p_tmp_ipuc_info, 0);
            p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i] = p_tmp_ipuc_info;
            flag = 1;
        }

        if (flag == 0)  /* for the max index tcam route */
        {
            p_tmp_ipuc_info = p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][index];
            _sys_greatbelt_ipuc_remove_key(lchip, p_tmp_ipuc_info);
        }
        p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][index] = 0;
        p_gb_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] --;
    }

    return ret;
}

int32
_sys_greatbelt_ipuc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gb_ipuc_db_master[lchip]->ipuc_hash[ip_ver], fn, data);
}

int32
_sys_greatbelt_ipuc_nat_sa_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash, fn, data);
}

STATIC int32
_sys_greatbelt_ipuc_db_init_share_mode(uint8 lchip)
{
    int32 i = 0;
    uint32 ipv6_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint32 table_size = 0;
    uint8 ipv4_block_num = 0;
    uint32 block_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_greatbelt_opf_t opf;
    uint8 total_block_size = 0;

    /* 1. data init */
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4UcastRouteKey_t, &table_size));
    ipv4_table_size = sys_greatbelt_ftm_get_ipuc_v4_number(lchip);
    if (ipv4_table_size <= 0)
    {
        p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = FALSE;
        return CTC_E_INVALID_CONFIG;
    }

    ipv6_table_size = table_size - ipv4_table_size;
    if (ipv6_table_size <= 0)
    {
        p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = FALSE;
        return CTC_E_INVALID_CONFIG;
    }

    p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = TRUE;
    p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = TRUE;

    total_block_size = CTC_IPV4_ADDR_LEN_IN_BIT + CTC_IPV6_ADDR_LEN_IN_BIT + 2;
    p_gb_ipuc_db_master[lchip]->share_blocks = (sys_sort_block_t*)mem_malloc(MEM_IPUC_MODULE, sizeof(sys_sort_block_t)*total_block_size);
    if (NULL == p_gb_ipuc_db_master[lchip]->share_blocks)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_ipuc_db_master[lchip]->share_blocks, 0, sizeof(sys_sort_block_t)*total_block_size);
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_UP;

    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_IPV4_UC_BLOCK, total_block_size));
    opf.pool_type = OPF_IPV4_UC_BLOCK;
    init_info.opf = &opf;
    init_info.max_offset_num = ipv4_table_size + ipv6_table_size;

    block_size = ipv4_table_size/ CTC_IPV4_ADDR_LEN_IN_BIT;
    /* 3) create avl tree for every block */
    /*bolck 0, mask len 32*/
    init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[0];
    init_info.boundary_l = 0;
    init_info.boundary_r = block_size * 5 - 1;
    init_info.is_block_can_shrink = TRUE;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    /*block 1-5, mask len 31 - 27*/
    for (i = 1; i < 6; i++)
    {
        init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[i];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
    }

    /*block 6-7, mask len 26 - 25*/
    for (i = 6; i < 8; i++)
    {
        init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[i];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + block_size - 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
    }

    /*block 8, mask len 24*/
    init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[8];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + (ipv4_table_size - block_size*8 - 28*2) - 1;
    opf.pool_index++;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    /*block 9-15, mask len 23 - 17*/
    for (i = 9; i < 16; i++)
    {
        init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[i];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
    }

    /*block 16, mask len 16*/
    init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[16];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + block_size - 1;
    opf.pool_index++;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    /*block 17-32, mask len 15 - 0*/
    for (i = 17; i < 33; i++)
    {
        init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[i];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
    }

    ipv4_block_num = (CTC_IPV4_ADDR_LEN_IN_BIT + 1);
    init_info.multiple = 2;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    /* bolck 0, mask len 128*/
    init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[ipv4_block_num];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + ipv6_table_size - CTC_IPV6_ADDR_LEN_IN_BIT*2 - 1;
    init_info.is_block_can_shrink = TRUE;
    opf.pool_index++;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    /* block 1-128, mask len 0-127*/
    for (i = 1; i < 129; i++)
    {
        init_info.block = &p_gb_ipuc_db_master[lchip]->share_blocks[i + ipv4_block_num];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
    }

    /* 4) init the offset array */
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_offset_array(lchip,
                                                               &p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], table_size));

    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_offset_array(lchip,
                                                               &p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], table_size));

    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block = p_gb_ipuc_db_master[lchip]->share_blocks;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num = opf.pool_index + 1;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].sort_key_syn_key = _sys_greatbelt_syn_key;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].type = OPF_IPV4_UC_BLOCK;

    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block = p_gb_ipuc_db_master[lchip]->share_blocks;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num = opf.pool_index + 1;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].sort_key_syn_key = _sys_greatbelt_syn_key;
    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].type = OPF_IPV4_UC_BLOCK;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ipv4_db_init(uint8 lchip)
{
    int32 i;
    uint32 table_size = 0;
    uint32 block_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_greatbelt_opf_t opf;

    /* 1. data init */
    sal_memset(p_gb_ipuc_db_master[lchip]->ipv4_blocks, 0, sizeof(p_gb_ipuc_db_master[lchip]->ipv4_blocks));
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4UcastRouteKey_t, &table_size));
    if (table_size <= 0)
    {
        p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = FALSE;
        return CTC_E_NONE;
    }

    p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = TRUE;
    p_gb_ipuc_db_master[lchip]->tcam_max_count[CTC_IP_VER_4] = table_size;

    if (p_gb_ipuc_master[lchip]->tcam_mode)
    {
        sys_greatbelt_opf_init(lchip, OPF_IPV4_UC_BLOCK, CTC_IPV4_ADDR_LEN_IN_BIT + 1);

        block_size = table_size / CTC_IPV4_ADDR_LEN_IN_BIT;

        opf.pool_type = OPF_IPV4_UC_BLOCK;
        init_info.opf = &opf;
        init_info.max_offset_num = table_size - 2;

        /* 3) create avl tree for every block */
        /*bolck 0, mask len 32, offset 0-(ipv4_block_size * 5 - 5)*/
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[0];
        init_info.boundary_l = 0;
        init_info.boundary_r = block_size * 5 - 6;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        /*block 1-5, mask len 31 - 27, offset 1*/
        for (i = 1; i < 6; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /*block 6-7, mask len 26 - 25, offset ipv4_block_size*/
        for (i = 6; i < 8; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + block_size - 1;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /*block 8, mask len 24, offset ipv4_block_size*/
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[8];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + 24 * block_size - 25;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        /*block 9-15, mask len 23 - 17, offset ipv4_block_size*/
        for (i = 9; i < 16; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /*block 16, mask len 16, offset ipv4_block_size*/
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[16];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + block_size - 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        /*block 17-32, mask len 15 - 0, offset ipv4_block_size*/
        for (i = 17; i < 33; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv4_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /* 4) init the offset array */
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_offset_array(lchip,
                             &p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], table_size));
    }
    else
    {
        p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_info_t*)*table_size);
        if (NULL == p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4], 0, sizeof(sys_ipuc_info_t*)*table_size);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipv6_db_init(uint8 lchip)
{
    int32 i;
    uint32 table_size = 0;
    uint32 block_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_greatbelt_opf_t opf;

    /* 1. data init */
    sal_memset(p_gb_ipuc_db_master[lchip]->ipv6_blocks, 0, sizeof(p_gb_ipuc_db_master[lchip]->ipv6_blocks));
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv6UcastRouteKey_t, &table_size));

    if (table_size <= 0)
    {
        p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = FALSE;
        return CTC_E_NONE;
    }

    p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = TRUE;
    p_gb_ipuc_db_master[lchip]->tcam_max_count[CTC_IP_VER_6] = table_size;

    if (p_gb_ipuc_master[lchip]->tcam_mode)
    {
        block_size = table_size / CTC_IPV6_ADDR_LEN_IN_BIT;

        sys_greatbelt_opf_init(lchip, OPF_IPV6_UC_BLOCK, CTC_IPV6_ADDR_LEN_IN_BIT + 1);
        opf.pool_type = OPF_IPV6_UC_BLOCK;
        init_info.opf = &opf;
        init_info.max_offset_num = table_size - 2;

        /* bolck 0, mask len 128, offset 0-(ipv6_block_size * 64 - 64) */
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv6_blocks[0];
        init_info.boundary_l = 0;
        init_info.boundary_r = block_size * 64 - 64;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        /* block 1-63, mask len 65-127, offset 1 */
        for (i = 1; i < 64; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv6_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /*block 64, mask len 64 offset block_size*/
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv6_blocks[64];
        init_info.boundary_l = block_size * 64;
        init_info.boundary_r = block_size * (64 + 1) - 1;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        /*block 65 - 127, mask len 1 - 63, offset 1*/
        for (i = 65; i < 128; i++)
        {
            init_info.block = &p_gb_ipuc_db_master[lchip]->ipv6_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));
        }

        /*block 128, mask len 0, offset block_size block_size * 64 + 64 - 1 ~ last*/
        init_info.block = &p_gb_ipuc_db_master[lchip]->ipv6_blocks[128];
        init_info.boundary_l = block_size * 64 + block_size + (127 - 65 + 1);   /*init_info.boundary_r + 1;*/
        init_info.boundary_r = table_size - 2;
        init_info.dir = SYS_SORT_BLOCK_DIR_UP;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

        CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_offset_array(lchip,
                             (skinfo_t***)&p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], table_size));
    }
    else
    {
        p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_info_t*)*table_size);
        if (NULL == p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6], 0, sizeof(sys_ipuc_info_t*)*table_size);
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_db_init(uint8 lchip)
{
    uint32 ad_table_size = 0;
    ctc_spool_t spool;
    uint32 ipv4_table_size = 0;
    uint32 ipv6_table_size = 0;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    if (NULL == p_gb_ipuc_db_master[lchip])
    {
        p_gb_ipuc_db_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_db_master_t));
        if (!p_gb_ipuc_db_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gb_ipuc_db_master[lchip], 0, sizeof(sys_ipuc_db_master_t));

        /* init ad spool */
        CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpDa_t, &ad_table_size));
        ad_table_size = (ad_table_size + SYS_IPUC_AD_SPOOL_BLOCK_SIZE - 1) /
            SYS_IPUC_AD_SPOOL_BLOCK_SIZE * SYS_IPUC_AD_SPOOL_BLOCK_SIZE;

        spool.lchip = lchip;
        spool.block_num = ad_table_size / SYS_IPUC_AD_SPOOL_BLOCK_SIZE;
        spool.block_size = SYS_IPUC_AD_SPOOL_BLOCK_SIZE;
        spool.max_count = ad_table_size;
        spool.user_data_size = sizeof(sys_ipuc_ad_spool_t);
        spool.spool_key = (hash_key_fn)_sys_greatbelt_ipuc_lpm_ad_hash_make;
        spool.spool_cmp = (hash_cmp_fn)_sys_greatbelt_ipuc_lpm_ad_hash_cmp;
        p_gb_ipuc_db_master[lchip]->ipuc_ad_spool = ctc_spool_create(&spool);
        if (!p_gb_ipuc_db_master[lchip]->ipuc_ad_spool)
        {
            return CTC_E_NO_MEMORY;
        }

        p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash = ctc_hash_create(1,
                                                                  (IPUC_RPF_PROFILE_HASH_MASK + 1),
                                                                  (hash_key_fn)_sys_greatbelt_ipuc_db_rpf_profile_hash_make,
                                                                  (hash_cmp_fn)_sys_greatbelt_ipuc_db_rpf_profile_hash_cmp);
        if (!p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    ipv4_table_size = sys_greatbelt_ftm_get_ipuc_v4_number(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv6UcastRouteKey_t, &ipv6_table_size));
    ipv6_table_size = ipv6_table_size*2 - ipv4_table_size;
    if (p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_6] && (p_gb_ipuc_master[lchip]->tcam_mode)
        &&ipv4_table_size&&ipv6_table_size)
    {
        p_gb_ipuc_db_master[lchip]->tcam_share_mode = sys_greatbelt_ftm_get_ip_tcam_share_mode(lchip);
    }

    /*init for IPv4*/
    if (p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && !p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4])
    {
        p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4] = ctc_hash_create(1,
                                                                    (IPUC_IPV4_HASH_MASK + 1),
                                                                    (hash_key_fn)_sys_greatbelt_ipv4_hash_make,
                                                                    (hash_cmp_fn)_sys_greatbelt_ipv4_hash_cmp);
        if (!p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4])
        {
            return CTC_E_NO_MEMORY;
        }

        p_gb_ipuc_db_master[lchip]->ipuc_nat_hash = ctc_hash_create(1,
                                                        (IPUC_IPV4_HASH_MASK + 1),
                                                        (hash_key_fn)_sys_greatbelt_ipv4_nat_hash_make,
                                                        (hash_cmp_fn)_sys_greatbelt_ipv4_nat_hash_cmp);
        if (!p_gb_ipuc_db_master[lchip]->ipuc_nat_hash)
        {
            return CTC_E_NO_MEMORY;
        }

        if ((0 == p_gb_ipuc_db_master[lchip]->tcam_share_mode) && ipv4_table_size)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipv4_db_init(lchip));
            if (p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] == TRUE)
            {
                if (p_gb_ipuc_master[lchip]->tcam_mode)
                {
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block = p_gb_ipuc_db_master[lchip]->ipv4_blocks;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num = CTC_IPV4_ADDR_LEN_IN_BIT + 1;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].sort_key_syn_key = _sys_greatbelt_ipv4_syn_key;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].type = OPF_IPV4_UC_BLOCK;
                }
            }
        }
    }

    if (p_gb_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && !p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6])
    {
        p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6] = ctc_hash_create(1,
                                                                    (IPUC_IPV6_HASH_MASK + 1),
                                                                    (hash_key_fn)_sys_greatbelt_ipv6_hash_make,
                                                                    (hash_cmp_fn)_sys_greatbelt_ipv6_hash_cmp);
        if (!p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6])
        {
            return CTC_E_NO_MEMORY;
        }

        if ((0 == p_gb_ipuc_db_master[lchip]->tcam_share_mode) && ipv6_table_size)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipv6_db_init(lchip));
            if (p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] == TRUE)
            {
                if (p_gb_ipuc_master[lchip]->tcam_mode)
                {
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block = p_gb_ipuc_db_master[lchip]->ipv6_blocks;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num = CTC_IPV6_ADDR_LEN_IN_BIT + 1;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].sort_key_syn_key = _sys_greatbelt_ipv6_syn_key;
                    p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].type = OPF_IPV6_UC_BLOCK;
                }
            }
        }
    }

    if (p_gb_ipuc_db_master[lchip]->tcam_share_mode && p_gb_ipuc_master[lchip]->tcam_mode)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipuc_db_init_share_mode(lchip));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipuc_db_free_node_data(void* node_data, void* user_data)
{
    uint8 lchip = 0;
    sys_ipuc_info_t* p_ipuc_info = NULL;
    bool do_lpm = TRUE;

    if (user_data)
    {
        lchip = *(uint8*)user_data;

        p_ipuc_info=(sys_ipuc_info_t*)node_data;
        _sys_greatbelt_ipuc_do_lpm_check(lchip, p_ipuc_info, &do_lpm);
        if (do_lpm)
        {
            _sys_greatbelt_lpm_del(lchip, p_ipuc_info);
        }
        if (p_ipuc_info->lpm_result)
        {
            mem_free(p_ipuc_info->lpm_result);
        }
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ipuc_db_deinit(uint8 lchip)
{
    uint8 lchip_id  = lchip;

    if (NULL == p_gb_ipuc_db_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] && (0 == p_gb_ipuc_db_master[lchip]->tcam_share_mode)
        && p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num)
    {
        if (p_gb_ipuc_master[lchip]->tcam_mode)
        {
            mem_free(*p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6]);
            mem_free(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6]);
            sys_greatbelt_opf_deinit(lchip, OPF_IPV6_UC_BLOCK);
        }
        else
        {
            mem_free(p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6]);
        }

        /*free ipuc v6 hash key*/
        ctc_hash_traverse(p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_greatbelt_ipuc_db_free_node_data, &lchip_id);
        ctc_hash_free(p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6]);
    }

    if (p_gb_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] && p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num)
    {
        if (p_gb_ipuc_master[lchip]->tcam_mode)
        {
            mem_free(*p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4]);
            mem_free(p_gb_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4]);
            sys_greatbelt_opf_deinit(lchip, OPF_IPV4_UC_BLOCK);
        }
        else
        {
            mem_free(p_gb_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4]);
        }

        /*free ipuc nat hash*/
        ctc_hash_traverse(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash, (hash_traversal_fn)_sys_greatbelt_ipuc_db_free_node_data, NULL);
        ctc_hash_free(p_gb_ipuc_db_master[lchip]->ipuc_nat_hash);

        /*free ipuc v4 hash key*/
        ctc_hash_traverse(p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_greatbelt_ipuc_db_free_node_data, &lchip_id);
        ctc_hash_free(p_gb_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4]);
    }

    /*free ipuc rpf hash key*/
    ctc_hash_traverse(p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash, (hash_traversal_fn)_sys_greatbelt_ipuc_db_free_node_data, NULL);
    ctc_hash_free(p_gb_ipuc_db_master[lchip]->ipuc_rpf_profile_hash);

    if (p_gb_ipuc_db_master[lchip]->share_blocks)
    {
        mem_free(p_gb_ipuc_db_master[lchip]->share_blocks);
    }

    /*free ipuc ad data*/
    ctc_spool_free(p_gb_ipuc_db_master[lchip]->ipuc_ad_spool);

    mem_free(p_gb_ipuc_db_master[lchip]);

    return CTC_E_NONE;
}

hash_cmp_fn
sys_greatbelt_get_hash_cmp(uint8 lchip, ctc_ip_ver_t ctc_ip_ver)
{
    if (CTC_IP_VER_4 == ctc_ip_ver)
    {
        return (hash_cmp_fn) & _sys_greatbelt_ipv4_hash_cmp;
    }
    else if (CTC_IP_VER_6 == ctc_ip_ver)
    {
        return (hash_cmp_fn) & _sys_greatbelt_ipv6_hash_cmp;
    }
    else
    {
        return NULL;
    }
}

#define __SHOW__
int32
sys_greatbelt_show_ipuc_info(sys_ipuc_info_t* p_ipuc_data, void* data)
{
    uint32 detail = 0;
    uint8   lchip = 0;
    uint16  route_flag = 0;
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};

#define SYS_IPUC_MASK_LEN 7
#define SYS_IPUC_L4PORT_LEN 7
    char buf2[20] = {0};
    char buf3[20] = {0};

    detail = *((uint32*)(((sys_ipuc_traverse_t*)data)->data));
    lchip  = ((sys_ipuc_traverse_t*)data)->lchip;

    sal_sprintf(buf2, "/%d", p_ipuc_data->masklen);
    if (p_ipuc_data->l4_dst_port)
    {
        sal_sprintf(buf3, "/%d", p_ipuc_data->l4_dst_port);
    }

    if (SYS_IPUC_VER(p_ipuc_data) == CTC_IP_VER_4)
    {
        uint32 tempip = sal_ntohl(p_ipuc_data->ip.ipv4[0]);
        sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strcat(buf, buf2);
        sal_strncat(buf, buf3, SYS_IPUC_L4PORT_LEN);
        SYS_IPUC_DBG_DUMP("%-5d %-30s", p_ipuc_data->vrf_id, buf);
    }
    else
    {
        uint32 ipv6_address[4] = {0, 0, 0, 0};

        ipv6_address[0] = sal_ntohl(p_ipuc_data->ip.ipv6[3]);
        ipv6_address[1] = sal_ntohl(p_ipuc_data->ip.ipv6[2]);
        ipv6_address[2] = sal_ntohl(p_ipuc_data->ip.ipv6[1]);
        ipv6_address[3] = sal_ntohl(p_ipuc_data->ip.ipv6[0]);

        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strcat(buf, buf2);
        SYS_IPUC_DBG_DUMP("%-5d %-44s", p_ipuc_data->vrf_id, buf);
    }

    buf2[0] = '\0';
    route_flag = p_ipuc_data->route_flag;
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_IS_IPV6);
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_DEFAULT);
    if (!route_flag)
    {
        sal_strncat(buf2, "O", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        sal_strncat(buf2, "R", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_TTL_CHECK))
    {
        sal_strncat(buf2, "T", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_CHECK))
    {
        sal_strncat(buf2, "I", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_ICMP_ERR_MSG_CHECK))
    {
        sal_strncat(buf2, "E", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_CPU))
    {
        sal_strncat(buf2, "C", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_NEIGHBOR))
    {
        sal_strncat(buf2, "N", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_PROTOCOL_ENTRY))
    {
        sal_strncat(buf2, "P", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_SELF_ADDRESS))
    {
        sal_strncat(buf2, "S", 1);
    }

    if (CTC_FLAG_ISSET(route_flag, SYS_IPUC_FLAG_IS_TCP_PORT))
    {
        sal_strncat(buf2, "U", 1);
    }

    if (CTC_FLAG_ISSET(p_ipuc_data->route_flag, CTC_IPUC_FLAG_CONNECT))
    {
        sal_strncat(buf2, "X", 1);
    }

    SYS_IPUC_DBG_DUMP("  %-6d   %-4d   %-5s   %-7s\n\r",
                      p_ipuc_data->ad_offset,
                      p_ipuc_data->nh_id, buf2,
                      (CTC_FLAG_ISSET(p_ipuc_data->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL)) ? "FALSE" : "TRUE");

    if (detail)
    {
        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_data))
        {
            sys_greatbelt_ipuc_retrieve_ipv4(lchip, p_ipuc_data);
        }
        else
        {
            sys_greatbelt_ipuc_retrieve_ipv6(lchip, p_ipuc_data);
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail)
{
    sys_ipuc_traverse_t travs;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    IPUC_SHOW_HEADER (ip_ver);
    travs.data = &detail;
    travs.lchip = lchip;
    _sys_greatbelt_ipuc_db_traverse(lchip, ip_ver, (hash_traversal_fn)sys_greatbelt_show_ipuc_info, (void*)&travs);
    return CTC_E_NONE;
}

int32
sys_greatbelt_show_ipuc_nat_sa_info(sys_ipuc_nat_sa_info_t* p_nat_info, void* data)
{
    uint32 detail = *((uint32*)data);
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
#define SYS_IPUC_NAT_L4PORT_LEN 7
    uint32 tempip = 0;

    SYS_IPUC_DBG_DUMP("%-5d  ", p_nat_info->vrf_id);

    tempip = sal_ntohl(p_nat_info->ipv4[0]);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPUC_DBG_DUMP("%-15s  ", buf);

    if (p_nat_info->l4_src_port)
    {
        SYS_IPUC_DBG_DUMP("%-8d  ", p_nat_info->l4_src_port);
    }
    else
    {
        SYS_IPUC_DBG_DUMP("%-8s  ", "-");
    }

    tempip = sal_ntohl(p_nat_info->new_ipv4[0]);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPUC_DBG_DUMP("%-15s  ", buf);

    if (p_nat_info->new_l4_src_port)
    {
        SYS_IPUC_DBG_DUMP("%-8d  ", p_nat_info->new_l4_src_port);
    }
    else
    {
        SYS_IPUC_DBG_DUMP("%-8s  ", "-");
    }

    SYS_IPUC_DBG_DUMP("%-7s  %-7s  %-8d\n",
                      (p_nat_info->l4_src_port ? (p_nat_info->is_tcp_port ? "TCP" : "UDP") : "-"),
                      (p_nat_info->in_tcam ? "S" : "T"),(p_nat_info->ad_offset));

    if (detail)
    {
         /*sys_greatbelt_ipuc_retrieve_ipv4(lchip, p_nat_info);*/
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_nat_sa_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail)
{
    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    if (ip_ver == CTC_IP_VER_4)
    {
        SYS_IPUC_DBG_DUMP("In-SRAM: T-TCAM    S-SRAM\n\r");
        SYS_IPUC_DBG_DUMP("\n");
        SYS_IPUC_DBG_DUMP("%-5s  %-15s  %-8s  %-15s  %-8s  %-7s  %-7s  %-8s\n", "VRF","IPSA",
            "Port","New-IPSA","New-Port","TCP/UDP","In-SRAM","AD-Index");
        SYS_IPUC_DBG_DUMP("---------------------------------------------------------------------------------------\n");
    }
    else
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    _sys_greatbelt_ipuc_nat_sa_db_traverse(lchip, ip_ver, (hash_traversal_fn)sys_greatbelt_show_ipuc_nat_sa_info, (void*)&detail);
    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_db_tcam_count(uint8 lchip, sys_ipuc_info_t* p_ipuc_data, uint32* count)
{
    if (CTC_FLAG_ISSET(p_ipuc_data->conflict_flag, SYS_IPUC_CONFLICT_FLAG_ALL))
    {
        *count += 1;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_db_show_count(uint8 lchip)
{
    uint32 i;

    SYS_IPUC_INIT_CHECK(lchip);

    for (i = 0; i < MAX_CTC_IP_VER; i++)
    {
        if ((!p_gb_ipuc_master[lchip]) || (!p_gb_ipuc_db_master[lchip]) || !p_gb_ipuc_master[lchip]->version_en[i])
        {
            SYS_IPUC_DBG_DUMP("SDK is not assigned resource for IPv%c version.\n\r", (i == CTC_IP_VER_4) ? '4' : '6');
            continue;
        }

        if (p_gb_ipuc_db_master[lchip]->ipuc_hash[i])
        {
            SYS_IPUC_DBG_DUMP("IPv%c total %d routes, %d using tcam.\n\r", (i == CTC_IP_VER_4) ? '4' : '6',
                          p_gb_ipuc_db_master[lchip]->ipuc_hash[i]->count, p_gb_ipuc_master[lchip]->tcam_counter[i]);
        }
    }

    /*sys_greatbelt_l3_hash_state_show(lchip);*/

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipuc_offset_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint8 blockid, uint8 detail)
{
    sys_sort_block_t* p_block;
    sys_greatbelt_opf_t opf;
    uint8 max_blockid = 0;
    uint32 all_of_num = 0;
    uint32 used_of_num = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    if (p_gb_ipuc_db_master[lchip]->tcam_share_mode)
    {
        max_blockid = p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num;
    }
    else
    {
        max_blockid = p_gb_ipuc_master[lchip]->max_mask_len[ip_ver];
        if (0 == p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].max_block_num)
        {
            SYS_IPUC_DBG_DUMP("Not alloc resource!!!\n\r");
            return CTC_E_NONE;
        }
    }

    if (blockid > max_blockid)
    {
        SYS_IPUC_DBG_DUMP("Error Block Index\n\r");
        return CTC_E_NONE;
    }

    SYS_IPUC_DBG_DUMP("%-10s %-10s %-10s %-10s %-10s %-10s\n", "ID",  "L", "R", "M", "All", "Used");
    SYS_IPUC_DBG_DUMP("============================================================\n");

    if (detail)
    {
        p_block = &p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].block[blockid];

        SYS_IPUC_DBG_DUMP("%-10d %-10d %-10d %-10d %-10d %-10d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple,p_block->all_of_num, p_block->used_of_num);


        SYS_IPUC_DBG_DUMP("\n");

        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type = p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].type;
        opf.pool_index = blockid;
        sys_greatbelt_opf_print_sample_info(lchip, &opf);
    }
    else
    {
        for (blockid = 0; blockid < max_blockid;  blockid++)
        {
            p_block = &p_gb_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].block[blockid];

            SYS_IPUC_DBG_DUMP("%-10d %-10d %-10d %-10d %-10d %-10d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple,p_block->all_of_num, p_block->used_of_num);
            all_of_num += p_block->all_of_num;
            used_of_num += p_block->used_of_num;
        }
        SYS_IPUC_DBG_DUMP( "============================================================\n");
        SYS_IPUC_DBG_DUMP( "%-10s %-10s %-10s %-10s %-10d %-10d\n", "Total",  "-", "-", "-", all_of_num, used_of_num);
    }

    return CTC_E_NONE;
}


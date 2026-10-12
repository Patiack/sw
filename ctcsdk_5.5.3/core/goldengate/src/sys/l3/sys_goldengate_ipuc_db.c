/**
 @file sys_goldengate_ipuc_db.c

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
#include "ctc_warmboot.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_sort.h"
#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_l3_hash.h"
#include "sys_goldengate_ipuc.h"
#include "sys_goldengate_ipuc_db.h"
#include "sys_goldengate_lpm.h"
#include "sys_goldengate_common.h"

#include "goldengate/include/drv_lib.h"

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
        if ((!p_gg_ipuc_master[lchip]) || (!p_gg_ipuc_db_master[lchip]) || !p_gg_ipuc_master[lchip]->version_en[ver])     \
        {                                                           \
            return CTC_E_IPUC_VERSION_DISABLE;                      \
        }                                                           \
    }
/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

extern sys_ipuc_master_t* p_gg_ipuc_master[];
extern int32
_sys_goldengate_ipuc_write_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);
extern int32
_sys_goldengate_ipuc_remove_key(uint8 lchip, sys_ipuc_info_t* p_ipuc_info);

sys_ipuc_db_master_t* p_gg_ipuc_db_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
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
_sys_goldengate_ipv4_hash_make(sys_ipuc_info_t* p_ipuc_info)
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
_sys_goldengate_ipv6_hash_make(sys_ipuc_info_t* p_ipuc_info)
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
_sys_goldengate_ipv4_nat_hash_make(sys_ipuc_nat_sa_info_t* p_nat_info)
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

STATIC bool
_sys_goldengate_ipv4_hash_cmp(sys_ipuc_info_t* p_ipuc_info1, sys_ipuc_info_t* p_ipuc_info)
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
_sys_goldengate_ipv4_nat_hash_cmp(sys_ipuc_nat_sa_info_t* p_nat_info1, sys_ipuc_nat_sa_info_t* p_nat_info)
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
_sys_goldengate_ipv6_hash_cmp(sys_ipuc_info_t* p_ipuc_info1, sys_ipuc_info_t* p_ipuc_info)
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
_sys_goldengate_ipuc_lpm_ad_hash_make(sys_ipuc_ad_spool_t* p_ipuc_ad)
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
_sys_goldengate_ipuc_lpm_ad_hash_cmp(sys_ipuc_ad_spool_t* p_bucket, sys_ipuc_ad_spool_t* p_new)
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

    if (p_bucket->gport != p_new->gport)
    {
        return FALSE;
    }

    if (CTC_FLAG_ISSET(p_new->route_flag, CTC_IPUC_FLAG_ICMP_CHECK)
        || CTC_FLAG_ISSET(p_new->route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        if (p_bucket->l3if != p_new->l3if)
        {
            return FALSE;
        }

        if (p_bucket->rpf_mode != p_new->rpf_mode)
        {
            return FALSE;
        }
    }

    return TRUE;
}

int32
_sys_goldengate_ipuc_lpm_ad_profile_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info, int32* ad_spool_ref_cnt)
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
    obsolete_flag = CTC_IPUC_FLAG_NEIGHBOR | CTC_IPUC_FLAG_CONNECT | CTC_IPUC_FLAG_STATS_EN | CTC_IPUC_FLAG_AGING_EN
                               | SYS_IPUC_FLAG_MERGE_KEY | SYS_IPUC_FLAG_IS_BIND_NH;
    p_ipuc_ad_new->route_flag = p_ipuc_info->route_flag & (~obsolete_flag);
    p_ipuc_ad_new->l3if = p_ipuc_info->l3if;
    p_ipuc_ad_new->rpf_mode = p_ipuc_info->rpf_mode;
    p_ipuc_ad_new->gport = p_ipuc_info->gport;
    if(CTC_FLAG_ISSET(p_ipuc_info->route_flag, SYS_IPUC_FLAG_IS_BIND_NH))
    {
        p_ipuc_ad_new->binding_nh  =1;
    }

    ret = ctc_spool_add(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_new, NULL,&p_ipuc_ad_get);
    if (p_ipuc_ad_get)
    {
        p_ipuc_info->binding_nh = p_ipuc_ad_get->binding_nh;
    }
    *ad_spool_ref_cnt = ctc_spool_get_refcnt(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_new);

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
            ret = sys_goldengate_ftm_alloc_table_offset(lchip, table_id, 0, 1, &ad_offset);
            if (ret)
            {
                ctc_spool_remove(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, p_ipuc_ad_get, NULL);
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
_sys_goldengate_ipuc_lpm_ad_profile_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
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
    obsolete_flag = CTC_IPUC_FLAG_NEIGHBOR | CTC_IPUC_FLAG_CONNECT | CTC_IPUC_FLAG_STATS_EN | CTC_IPUC_FLAG_AGING_EN
                               | SYS_IPUC_FLAG_MERGE_KEY | SYS_IPUC_FLAG_IS_BIND_NH;
    ipuc_ad.route_flag = p_ipuc_info->route_flag & (~obsolete_flag);
    ipuc_ad.l3if = p_ipuc_info->l3if;
    ipuc_ad.rpf_mode = p_ipuc_info->rpf_mode;
    ipuc_ad.gport = p_ipuc_info->gport;

    p_ipuc_ad_del = ctc_spool_lookup(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, &ipuc_ad);
    if (!p_ipuc_ad_del)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool, &ipuc_ad, NULL);
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
            CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, table_id, 0, 1, ad_offset));
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: remove ad_offset = %d\n",  ad_offset);
            /* free memory */
            mem_free(p_ipuc_ad_del);
        }
    }

    return CTC_E_NONE;
}

/**
 @brief function of lookup ip route information

 @param[in] pp_ipuc_info, information used for lookup ipuc entry
 @param[out] pp_ipuc_info, information of ipuc entry finded

 @return CTC_E_XXX
 */
int32
_sys_goldengate_ipuc_db_lookup(uint8 lchip, sys_ipuc_info_t** pp_ipuc_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;

    p_ipuc_info = ctc_hash_lookup(p_gg_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(*pp_ipuc_info)], *pp_ipuc_info);

    *pp_ipuc_info = p_ipuc_info;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_nat_db_lookup(uint8 lchip, sys_ipuc_nat_sa_info_t** pp_ipuc_nat_info)
{
    sys_ipuc_nat_sa_info_t* p_ipuc_nat_info = NULL;

    p_ipuc_nat_info = ctc_hash_lookup(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, *pp_ipuc_nat_info);

    *pp_ipuc_nat_info = p_ipuc_nat_info;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_db_get_info_cb(void* p_data_o, void* p_data_m)
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
_sys_goldengate_ipuc_db_get_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list)
{
    uint8 ip_ver;

    for (ip_ver = CTC_IP_VER_4; ip_ver < MAX_CTC_IP_VER; ip_ver++)
    {
        _sys_goldengate_ipuc_db_traverse(lchip, ip_ver, _sys_goldengate_ipuc_db_get_info_cb, &p_info_list);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_db_sort_slist(uint8 lchip, sys_ipuc_arrange_info_t* phead, sys_ipuc_arrange_info_t* pend)
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
_sys_goldengate_ipuc_db_anylize_info(uint8 lchip, sys_ipuc_info_list_t* p_info_list, sys_ipuc_arrange_info_t** pp_arrange_info)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_lpm_tbl_t* p_table[LPM_TABLE_MAX] = {NULL};
    sys_lpm_tbl_t* p_table_now = NULL;
    sys_lpm_tbl_t* p_table_pre = NULL;
    sys_ipuc_arrange_info_t* p_arrange_tbl = NULL;
    sys_ipuc_arrange_info_t* p_arrange_tbl_head = NULL;
    sys_ipuc_arrange_info_t* p_arrange_now = NULL;
    sys_ipuc_arrange_info_t* p_arrange_pre = NULL;
    sys_ipuc_info_list_t* p_info_list_t = NULL;
    sys_ipuc_info_list_t* p_info_list_old = NULL;
    uint8 i = 0;
    uint32 pointer = 0;
    uint8 size = 0;

    p_info_list_t = p_info_list;

    /* get all entry info */
    while ((NULL != p_info_list_t) && (NULL != p_info_list_t->p_ipuc_info))
    {
        p_ipuc_info = p_info_list_t->p_ipuc_info;
        p_info_list_old = p_info_list_t;
        p_info_list_t = p_info_list_t->p_next_info;

        if (DO_LPM != p_ipuc_info->route_opt)
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

            pointer = p_table[i]->offset | (p_table[i]->sram_index << POINTER_OFFSET_BITS_LEN);
            if (INVALID_POINTER == pointer)
            {
                continue;
            }

            if (NULL == p_arrange_tbl)
            {
                p_arrange_tbl = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_info_t));
                if (NULL == p_arrange_tbl)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_arrange_tbl, 0, sizeof(sys_ipuc_arrange_info_t));
                p_arrange_tbl->p_data = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_data_t));
                if (NULL == p_arrange_tbl->p_data)
                {
                    mem_free(p_arrange_tbl);
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_arrange_tbl->p_data, 0, sizeof(sys_ipuc_arrange_data_t));

                if (NULL == p_arrange_tbl_head)
                {
                    p_arrange_tbl_head = p_arrange_tbl;
                }
            }
            else
            {
                p_arrange_tbl->p_next_info = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_info_t));
                if (NULL == p_arrange_tbl->p_next_info)
                {
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_arrange_tbl->p_next_info, 0, sizeof(sys_ipuc_arrange_info_t));
                p_arrange_tbl->p_next_info->p_data = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_arrange_data_t));
                if (NULL == p_arrange_tbl->p_next_info->p_data)
                {
                    mem_free(p_arrange_tbl->p_next_info);
                    return CTC_E_NO_MEMORY;
                }

                sal_memset(p_arrange_tbl->p_next_info->p_data, 0, sizeof(sys_ipuc_arrange_data_t));

                p_arrange_tbl = p_arrange_tbl->p_next_info;
            }

            p_arrange_tbl->p_data->idx_mask = p_table[i]->idx_mask;
            p_arrange_tbl->p_data->start_offset = pointer & POINTER_OFFSET_MASK;
            p_arrange_tbl->p_data->p_ipuc_info = p_ipuc_info;
            p_arrange_tbl->p_data->stage = i;
            LPM_INDEX_TO_SIZE(p_arrange_tbl->p_data->idx_mask, size);
            SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "store ipuc table[%d] stage %d pointer = %4d  size = %d \n",
                             p_arrange_tbl->p_data->stage, i, p_arrange_tbl->p_data->start_offset, size);

        }

        mem_free(p_info_list_old);
        p_info_list_old = NULL;
    }

    if (p_info_list_t->p_next_info)
    {
        mem_free(p_info_list_t->p_next_info);
        p_info_list_t->p_next_info = NULL;
    }

    if (p_info_list_t)
    {
        mem_free(p_info_list_t);
        p_info_list_t = NULL;
    }

    /* sort and delete the same node */
    _sys_goldengate_ipuc_db_sort_slist(lchip, p_arrange_tbl_head, p_arrange_tbl);
    p_arrange_now = p_arrange_tbl_head;
    p_arrange_pre = p_arrange_tbl_head;
    p_table_pre = NULL;

    while (NULL != p_arrange_now)
    {
        p_ipuc_info = p_arrange_now->p_data->p_ipuc_info;
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

    *pp_arrange_info = p_arrange_tbl_head;

    return CTC_E_NONE;
}

/**
 @brief function of add ip route information

 @param[in] p_ipuc_info, information should be maintained for ipuc

 @return CTC_E_XXX
 */
int32
_sys_goldengate_ipuc_db_add(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{

    ctc_hash_insert(p_gg_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_nat_db_add(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{

    ctc_hash_insert(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, p_nat_info);

    return CTC_E_NONE;
}

/**
 @brief function of add ip route information

 @param[in] p_ipuc_info, information should be maintained for ipuc

 @return CTC_E_XXX
 */
int32
_sys_goldengate_ipuc_db_remove(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    ctc_hash_remove(p_gg_ipuc_db_master[lchip]->ipuc_hash[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_nat_db_remove(uint8 lchip, sys_ipuc_nat_sa_info_t* p_nat_info)
{
    ctc_hash_remove(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, p_nat_info);
    mem_free(p_nat_info);

    return CTC_E_NONE;
}

int32 masklen_block_map[2][33] = {
            {
               16,15,14,13,12,11,10,9,   /*  0 ~  7 */
                8, 8, 8, 8, 8, 8, 8,8,   /*  8 ~ 15 */
                8, 8, 8, 8, 8, 8, 8,8,   /* 16 ~ 23 */
                8, 7, 6, 5, 4, 3, 2,1,   /* 24 ~ 31 */
                0,                       /* 32 */
            },
            {
               16,15,14,13,12,11,10,9,   /*  0 ~  7 */
                8, 7, 6, 5, 4, 3, 2,1,   /*  8 ~ 15 */
                0, 0, 0, 0, 0, 0, 0,0,   /* 16 ~ 23 */
                0, 0, 0, 0, 0, 0, 0,0,   /* 25 ~ 31 */
                0,                       /* 32 */
            },
    };

int32 masklen_ipv6_block_map[129] = {
               112,  111, 110, 109, 108, 107,106,105,   /* 0  ~ 7  */
               104, 103, 102, 101, 100, 99, 98, 97,  /* 8  ~ 15  */
               96,  95,  94,  93,  92,  91, 90, 89,  /* 16 ~ 23  */
               88,  87,  86,  85,  84,  83, 82, 81,  /* 24 ~ 31  */
               80,  79,  78,  77,  76,  75, 74, 73,  /* 32 ~ 39  */
               72,  71,  70,  69,  68,  67, 66, 65,  /* 40 ~ 47  */
               64,  64,  64,  64,  64,  64, 64, 64,  /* 48 ~ 55  */
               64,  64,  64,  64,  64,  64, 64, 64,  /* 56 ~ 63  */
               64,  63,  62,  61,  60,  59, 58, 57,  /* 64 ~ 71  */
               56,  55,  54,  53,  52,  51, 50, 49,  /* 72 ~ 79  */
               48,  47,  46,  45,  44,  43, 42, 41,  /* 80 ~ 87  */
               40,  39,  38,  37,  36,  35, 34, 33,  /* 88 ~ 95  */
               32,  31,  30,  29,  28,  27, 26, 25,  /* 96 ~ 103 */
               24,  23,  22,  21,  20,  19, 18, 17,  /* 104~ 111 */
               16,  15,  14,  13,  12,  11, 10,  9,  /* 112~  119 */
               8,   7,   6,   5,   4,   3,  2,  1,   /* 120~  127 */
               0,                                    /* 128 */
    };

int32
_sys_goldengate_ipuc_alloc_tcam_key_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint32 key_offset = 0;
    uint16 i = 0;
    sys_ipuc_info_t* p_tmp_ipuc_info = 0;
    sys_l3_hash_t* p_hash = NULL;
    uint8 tmp_masklen = 0;
    uint8 tmp_ipuc_info_masklen = p_ipuc_info->masklen;

    if (p_gg_ipuc_master[lchip]->tcam_mode)
    {
        /*1. init*/
        if (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4)
        {
            p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_block_map[p_gg_ipuc_master[lchip]->use_hash16][p_ipuc_info->masklen];
        }
        else
        {
            if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_ipv6_block_map[p_ipuc_info->masklen] + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
            }
            else
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_ipv6_block_map[p_ipuc_info->masklen];
            }
        }

        /*2. do it*/
        if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
        {
            CTC_ERROR_RETURN(sys_goldengate_sort_key_alloc_offset_from_position
                             (lchip, &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)], 1, p_ipuc_info->key_offset));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_sort_key_alloc_offset
                             (lchip, &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)], &key_offset));
            p_ipuc_info->key_offset = key_offset;
            p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] ++;
        }

        if (DO_LPM == p_ipuc_info->route_opt)
        {
            _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
            if (!p_hash)
            {
                sys_goldengate_sort_key_free_offset(lchip, &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->key_offset);
                if (CTC_WB_STATUS(lchip) != CTC_WB_STATUS_RELOADING)
                {
                    p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] --;
                }
                return CTC_E_UNEXPECT;
            }
            SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->key_offset)
                = p_hash;
            p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->key_offset] = SYS_IPUC_TCAM_FLAG_l3_HASH;
        }
        else
        {
            SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->key_offset)
                = p_ipuc_info;
            p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][p_ipuc_info->key_offset] = SYS_IPUC_TCAM_FLAG_IPUC_INFO;
        }
        SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: alloc tcam key offset = %d\n",
                         ((CTC_IP_VER_6 == SYS_IPUC_VER(p_ipuc_info)) && p_gg_ipuc_db_master[lchip]->tcam_share_mode)?
                         (key_offset / SYS_IPUC_TCAM_6TO4_STEP) : key_offset);
    }
    else
    {
        if ((p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]+1)
            > p_gg_ipuc_master[lchip]->max_tcam_num[SYS_IPUC_VER(p_ipuc_info)])
        {
            return CTC_E_NO_RESOURCE;
        }

        for (i = 0; i < p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; i++)
        {
            if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
            {
                p_tmp_ipuc_info = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].p_info;
                tmp_masklen = p_tmp_ipuc_info->masklen;
                tmp_ipuc_info_masklen = p_ipuc_info->masklen;
            }
            else
            {
                p_hash = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].p_info;
                tmp_masklen = p_hash->masklen;
                if (DO_LPM == p_ipuc_info->route_opt)
                {
                    tmp_ipuc_info_masklen = p_hash->masklen;
                }else{
                    tmp_ipuc_info_masklen = p_ipuc_info->masklen;
                }
            }
            if (tmp_masklen >= tmp_ipuc_info_masklen)
            {
                continue;
            }
            else
            {
                p_ipuc_info->key_offset = i;
                return CTC_E_NONE;
            }
        }

        p_ipuc_info->key_offset = p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)];
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_free_tcam_ad_index(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    uint32 key_offset = p_ipuc_info->key_offset;

    if (p_gg_ipuc_master[lchip]->tcam_mode)
    {
        /*1. init*/
        if (SYS_IPUC_VER(p_ipuc_info) == CTC_IP_VER_4)
        {
            p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_block_map[p_gg_ipuc_master[lchip]->use_hash16][p_ipuc_info->masklen];
        }
        else
        {
            if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_ipv6_block_map[p_ipuc_info->masklen] + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
            }
            else
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block_id =
                masklen_ipv6_block_map[p_ipuc_info->masklen];
            }
        }

        /*2. do it*/
        if (DO_LPM == p_ipuc_info->route_opt)
        {
            _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
            if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
            {
                p_ipuc_info->key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            }

            if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
            {
                p_ipuc_info->key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
        }

        SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], p_ipuc_info->key_offset) = NULL;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_free_offset(lchip, &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)],  key_offset));
        p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] --;

    }
    else
    {
        p_gg_ipuc_master[lchip]->tcam_move = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_key_move_ex(uint8 lchip, uint32 new_offset, uint32 old_offset, uint8 ip_ver)
{
    uint32 cmdr, cmdw;
    sys_ipuc_info_t tmp_ipuc_info;
    ds_t lpmtcam_ad;
    ds_t tcam40key, tcam40mask;
    tbl_entry_t tbl_ipkey;
    uint32 new_offset_tmp = 0;
    uint32 old_offset_tmp = 0;

    sal_memset(&tmp_ipuc_info, 0, sizeof(tmp_ipuc_info));
    sal_memset(&lpmtcam_ad, 0, sizeof(lpmtcam_ad));
    sal_memset(&tcam40key, 0, sizeof(tcam40key));
    sal_memset(&tcam40mask, 0, sizeof(tcam40mask));

    new_offset_tmp =(p_gg_ipuc_db_master[lchip]->tcam_share_mode && (CTC_IP_VER_6 == ip_ver))?( new_offset/SYS_IPUC_TCAM_6TO4_STEP):new_offset;
    old_offset_tmp = (p_gg_ipuc_db_master[lchip]->tcam_share_mode && (CTC_IP_VER_6 == ip_ver))? ( old_offset / SYS_IPUC_TCAM_6TO4_STEP) : old_offset;

    cmdr = DRV_IOR(p_gg_ipuc_master[lchip]->tcam_da_table[ip_ver], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gg_ipuc_master[lchip]->tcam_da_table[ip_ver], DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, old_offset, cmdr, &lpmtcam_ad);
    DRV_IOCTL(lchip, new_offset, cmdw, &lpmtcam_ad);

    cmdr = DRV_IOR(p_gg_ipuc_master[lchip]->tcam_key_table[ip_ver], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gg_ipuc_master[lchip]->tcam_key_table[ip_ver], DRV_ENTRY_FLAG);
    tbl_ipkey.data_entry = (uint32*)&tcam40key;
    tbl_ipkey.mask_entry = (uint32*)&tcam40mask;
    DRV_IOCTL(lchip, old_offset_tmp, cmdr, &tbl_ipkey);
    DRV_IOCTL(lchip, new_offset_tmp, cmdw, &tbl_ipkey);

    /* remove key from old offset */
    tmp_ipuc_info.key_offset = old_offset;
    tmp_ipuc_info.route_flag |= ((ip_ver == CTC_IP_VER_4) ? 0 : SYS_IPUC_FLAG_IS_IPV6);
    _sys_goldengate_ipuc_remove_key(lchip, &tmp_ipuc_info);

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: tcam move from %d->%d \n", old_offset, new_offset);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipv4_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_l3_hash_t* p_hash = NULL;

    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4][old_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset);
        if (NULL == p_ipuc_info)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset);
        if (NULL == p_hash)
        {
            return CTC_E_NONE;
        }
    }

    if (new_offset == old_offset)
    {
        return CTC_E_ENTRY_EXIST;
    }

    _sys_goldengate_ipuc_key_move_ex(lchip, new_offset, old_offset, CTC_IP_VER_4);

    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4][old_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info->key_offset = new_offset;
    }
    else
    {
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_NEXTHOP] = new_offset;
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_POINTER] = new_offset;
        }
    }

    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4][new_offset] = p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4][old_offset];
    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4][old_offset] = 0;

    SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], new_offset) =
        SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset);
    SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], old_offset) = NULL;


    return CTC_E_NONE;
}

int32
_sys_goldengate_ipv6_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_l3_hash_t* p_hash = NULL;

    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][old_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
        if (NULL == p_ipuc_info)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
        if (NULL == p_hash)
        {
            return CTC_E_NONE;
        }
    }

    if (new_offset == old_offset)
    {
        return CTC_E_ENTRY_EXIST;
    }

    _sys_goldengate_ipuc_key_move_ex(lchip, new_offset, old_offset, CTC_IP_VER_6);

    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][old_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info->key_offset = new_offset;
    }
    else
    {
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_NEXTHOP] = new_offset;
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_POINTER] = new_offset;
        }
    }

    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][new_offset] = p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][old_offset];
    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][old_offset] = 0;

    SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], new_offset) =
        SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
    SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset) = NULL;

    return CTC_E_NONE;
}


int32
_sys_goldengate_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipuc_info_t* p_ipuc_info = NULL;
    sys_l3_hash_t* p_hash = NULL;
    uint8 is_v6 = 0;


    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6][old_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
        if (p_ipuc_info && (p_ipuc_info->key_offset == old_offset))
        {
            is_v6 = 1;
        }
    }
    else
    {
        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], old_offset);
        if (p_hash)
        {
            is_v6 = 1;
        }
    }

    if (is_v6)
    {
        CTC_ERROR_RETURN(_sys_goldengate_ipv6_syn_key(lchip, new_offset, old_offset));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_ipv4_syn_key(lchip, new_offset, old_offset));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_key_move(uint8 lchip, sys_ipuc_tcam_manager_t* p_tcam_mng, uint8 is_up)
{
    uint32 new_offset = 0;
    uint32 old_offset = 0;
    uint8 ip_ver = 0;
    sys_l3_hash_t* p_hash = NULL;
    sys_ipuc_info_t* p_ipuc_info = NULL;

    if (p_tcam_mng->type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info = p_tcam_mng->p_info;
        old_offset = p_ipuc_info->key_offset;
        ip_ver = SYS_IPUC_VER(p_ipuc_info);
    }
    else
    {
        p_hash = p_tcam_mng->p_info;
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            old_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            old_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
        }
        ip_ver = p_hash->ip_ver;
    }

    /* add key to new offset */
    if (is_up)
    {
        new_offset = old_offset + 1;
    }
    else
    {
        new_offset = old_offset - 1;
    }

    _sys_goldengate_ipuc_key_move_ex(lchip, new_offset, old_offset, ip_ver);

    /* add key to new offset */
    if (p_tcam_mng->type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
    {
        p_ipuc_info->key_offset = new_offset;
    }
    else
    {
        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_NEXTHOP] = new_offset;
        }

        if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
        {
            p_hash->hash_offset[LPM_TYPE_POINTER] = new_offset;
        }
    }

    SYS_IPUC_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "IPUC: tcam move from %d->%d \n", old_offset, new_offset);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_shift_key_up(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint16 j = 0;
    sys_l3_hash_t* p_hash = NULL;
    sys_ipuc_tcam_manager_t tcam_mng;
    uint32 key_offset = 0;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tcam_mng, 0, sizeof(tcam_mng));

    if (DO_LPM == p_ipuc_info->route_opt)
    {
        _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
        if (!p_hash)
        {
            return CTC_E_UNEXPECT;
        }

        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }
        else
        {
            key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
        }
    }
    else
    {
        key_offset = p_ipuc_info->key_offset;
    }

    for (j = p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; j > key_offset; j --)
    {
        tcam_mng = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][j-1];
        _sys_goldengate_ipuc_key_move(lchip, &tcam_mng, 1);
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][j] = tcam_mng;
    }

    if (DO_LPM == p_ipuc_info->route_opt)
    {
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].p_info = p_hash;
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].type = SYS_IPUC_TCAM_FLAG_l3_HASH;
    }
    else
    {
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].p_info = p_ipuc_info;
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].type = SYS_IPUC_TCAM_FLAG_IPUC_INFO;
    }

    p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] ++;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_write_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (!p_gg_ipuc_master[lchip]->tcam_mode)
    {
        _sys_goldengate_ipuc_shift_key_up(lchip, p_ipuc_info);
    }

    _sys_goldengate_ipuc_write_key(lchip, p_ipuc_info);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_shift_key_down(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint16 i = 0;
    uint16 index = 0;
    uint8 flag = 0;
    uint32 key_offset = 0;
    sys_l3_hash_t* p_hash = NULL;
    sys_ipuc_info_t tmp_ipuc_info;
    sys_ipuc_tcam_manager_t tcam_mng;

    SYS_IPUC_DBG_FUNC();

    sal_memset(&tcam_mng, 0, sizeof(tcam_mng));
    sal_memset(&tmp_ipuc_info, 0, sizeof(tmp_ipuc_info));

    if (DO_LPM == p_ipuc_info->route_opt)
    {
        _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
        if (!p_hash)
        {
            return CTC_E_UNEXPECT;
        }

        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
        {
            key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
        }
        else
        {
            key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
        }
    }
    else
    {
        key_offset = p_ipuc_info->key_offset;
    }

    index = p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] - 1;
    for (i = key_offset; i < index; i ++)
    {
        tcam_mng = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i+1];
        _sys_goldengate_ipuc_key_move(lchip, &tcam_mng, 0);
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i] = tcam_mng;
        flag = 1;
    }

    if (flag == 0)  /* for the max index tcam route */
    {
        tcam_mng = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][index];
        if (tcam_mng.type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
        {
            sal_memcpy(&tmp_ipuc_info, tcam_mng.p_info, p_gg_ipuc_master[lchip]->info_size[SYS_IPUC_VER(p_ipuc_info)]);
        }
        else
        {
            p_hash = tcam_mng.p_info;
            if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
            {
                tmp_ipuc_info.key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
            }

            if (p_hash->hash_offset[LPM_TYPE_POINTER] != INVALID_POINTER_OFFSET)
            {
                tmp_ipuc_info.key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
            }
            tmp_ipuc_info.route_flag |= ((p_hash->ip_ver == CTC_IP_VER_4) ? 0 : SYS_IPUC_FLAG_IS_IPV6);
        }
        _sys_goldengate_ipuc_remove_key(lchip, &tmp_ipuc_info);
    }

    p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][index].p_info = 0;
    p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)] --;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_remove_key_ex(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    SYS_IPUC_DBG_FUNC();

    if (p_gg_ipuc_master[lchip]->tcam_mode)
    {
        _sys_goldengate_ipuc_remove_key(lchip, p_ipuc_info);
    }
    else
    {
        _sys_goldengate_ipuc_shift_key_down(lchip, p_ipuc_info);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_push_itself(uint8 lchip, sys_l3_hash_t* p_hash, uint8 ip_ver, uint16 start_offset)
{
    uint16 i = 0, j = 0;
    sys_ipuc_info_t* p_tmp_ipuc_info = 0;
    uint8 find = 0;
    uint32 ip_mask = 0;
    ipv6_addr_t ipv6_mask;

    SYS_IPUC_DBG_FUNC();

    if(!p_gg_ipuc_master[lchip]->tcam_mode)
    {
        if (CTC_IP_VER_4 == ip_ver)
        {
            /* 1. For masklen 16 or 8, need push itself when removed the route entry */
            for (i = start_offset; i < p_gg_ipuc_db_master[lchip]->tcam_ip_count[ip_ver]; i++)
            {
                if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[ip_ver][i].type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
                {
                    p_tmp_ipuc_info = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[ip_ver][i].p_info;

                    if (p_tmp_ipuc_info->masklen < p_gg_ipuc_master[lchip]->masklen_l)
                    {
                        IPV4_LEN_TO_MASK(ip_mask, p_tmp_ipuc_info->masklen);
                        if (((p_tmp_ipuc_info->ip.ipv4[0] & ip_mask) == (p_hash->ip32[0] & ip_mask))
                            && (p_hash->vrf_id == p_tmp_ipuc_info->vrf_id))
                        {
                            find = 1;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            sal_memset(&ipv6_mask, 0, sizeof(ipv6_mask));

            for (i = start_offset; i < p_gg_ipuc_db_master[lchip]->tcam_ip_count[ip_ver]; i++)
            {
                if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[ip_ver][i].type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
                {
                    p_tmp_ipuc_info = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[ip_ver][i].p_info;

                    if (p_tmp_ipuc_info->masklen < p_gg_ipuc_master[lchip]->masklen_ipv6_l)
                    {
                        IPV6_LEN_TO_MASK(ipv6_mask, p_tmp_ipuc_info->masklen);
                        for (j = 0; j < 4; j ++)
                        {
                            if ((p_tmp_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3-j] & ipv6_mask[j]))
                            {
                                break;
                            }
                        }

                        if (j == 4)
                        {
                            if (p_hash->vrf_id == p_tmp_ipuc_info->vrf_id)
                            {
                                find = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (CTC_IP_VER_4 == ip_ver)
        {
            uint32 block_id_start = 0;
            uint32 block_id_end   = 0;

            if(!p_gg_ipuc_master[lchip]->use_hash16)
            {
                block_id_start = 9;
                block_id_end   = 16 + 1;
            }else{
                block_id_start = 1;
                block_id_end   = 16 + 1;
            }

            for(;block_id_start < block_id_end;block_id_start++)
            {
                uint32 left = 0;
                uint32 right = 0;
                uint32 use_count = 0;
                uint32 check_count = 0;

                left  = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block[block_id_start].all_left_b;
                right = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block[block_id_start].all_right_b;
                use_count = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block[block_id_start].used_of_num;

                for(;(left <= right) && (check_count < use_count);left++)
                {
                    if(NULL == SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[ip_ver], left))
                    {
                        continue;
                    }

                    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[ip_ver][left] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
                    {
                        p_tmp_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[ip_ver], left);
                        if(!p_tmp_ipuc_info)
                            continue;

                        if (p_tmp_ipuc_info->masklen < p_gg_ipuc_master[lchip]->masklen_l)
                        {
                            IPV4_LEN_TO_MASK(ip_mask, p_tmp_ipuc_info->masklen);
                            if (((p_tmp_ipuc_info->ip.ipv4[0] & ip_mask) == (p_hash->ip32[0] & ip_mask))
                                && (p_hash->vrf_id == p_tmp_ipuc_info->vrf_id))
                            {
                                find = 1;
                                break;
                            }
                        }
                    }

                    check_count++;
                }

                if(find)
                    break;
            }
        }
        else
        {
            /* ipv6 */
            uint32 block_id_start = 0;
            uint32 block_id_end   = 0;

            if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
            {
                block_id_start = 65 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
                block_id_end   = 111 + 1 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
            }
            else
            {
                block_id_start = 65;
                block_id_end   = 111 + 1;
            }

            for(;block_id_start < block_id_end;block_id_start++)
            {
                uint32 left = 0;
                uint32 right = 0;
                uint32 use_count = 0;
                uint32 check_count = 0;

                left  = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block[block_id_start].all_left_b;
                right = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block[block_id_start].all_right_b;
                use_count = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block[block_id_start].used_of_num;

                for(;(left <= right) && (check_count < use_count);left++)
                {
                    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[ip_ver][left] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
                    {
                        p_tmp_ipuc_info = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[ip_ver], left);
                        if(!p_tmp_ipuc_info)
                            continue;

                        if (p_tmp_ipuc_info->masklen < p_gg_ipuc_master[lchip]->masklen_ipv6_l)
                        {
                            IPV6_LEN_TO_MASK(ipv6_mask, p_tmp_ipuc_info->masklen);
                            for (j = 0; j < 4; j ++)
                            {
                                if ((p_tmp_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3-j] & ipv6_mask[j]))
                                {
                                    break;
                                }
                            }

                            if (j == 4)
                            {
                                if (p_hash->vrf_id == p_tmp_ipuc_info->vrf_id)
                                {
                                    find = 1;
                                    break;
                                }
                            }
                        }

                        check_count++;
                    }
                }
                if(find)
                    break;
            }

        }
    }
    /* 2. Find, than update the hash key with the ad_offset */
    p_hash->is_pushed = 1;
    if (find)
    {
        _sys_goldengate_l3_hash_update_tcam_ad(lchip, ip_ver, p_hash->hash_offset[LPM_TYPE_POINTER], p_tmp_ipuc_info->ad_offset);
        p_hash->masklen_pushed = p_tmp_ipuc_info->masklen;
        p_hash->is_mask_valid = 1;
    }
    else
    {
        _sys_goldengate_l3_hash_update_tcam_ad(lchip, ip_ver, p_hash->hash_offset[LPM_TYPE_POINTER], INVALID_POINTER_OFFSET);
        p_hash->masklen_pushed = 0;     /* means have push down, but no match key and do nothing */
        p_hash->is_mask_valid = 0;
    }

    return CTC_E_NONE;
}

/* only hash key need push down */
int32
_sys_goldengate_ipuc_push_down_itself(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    uint32 key_offset = 0;

    SYS_IPUC_DBG_FUNC();

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (NULL == p_hash)
    {
        SYS_IPUC_DBG_ERROR("ERROR: p_hash is NULL\r\n");
        return CTC_E_UNEXPECT;
    }

    if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] != INVALID_POINTER_OFFSET)
    {
        key_offset = p_hash->hash_offset[LPM_TYPE_NEXTHOP];
    }
    else
    {
        key_offset = p_hash->hash_offset[LPM_TYPE_POINTER];
    }

    if(!p_gg_ipuc_master[lchip]->tcam_mode)
    {
        if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].type == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
        {
            return CTC_E_NONE;
        }

        p_hash = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][key_offset].p_info;
    }
    else
    {
        if(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][key_offset] == SYS_IPUC_TCAM_FLAG_IPUC_INFO)
        {
            return CTC_E_NONE;
        }

        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], key_offset);
    }

    /* 1. check if the hash key is new */

    if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] == INVALID_POINTER_OFFSET)
    {
        if (p_hash->is_pushed)
        {
            return CTC_E_NONE;
        }
    }
    else
    {
        p_hash->is_pushed = 0;
        p_hash->is_mask_valid = 0;
        p_hash->masklen_pushed = 0;
        return CTC_E_NONE;
    }

    /* 2. For new hash key, need push down itself */
    _sys_goldengate_ipuc_push_itself(lchip, p_hash, SYS_IPUC_VER(p_ipuc_info), 0);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_push_down_all(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint16 i = 0, j = 0;
    sys_l3_hash_t* p_hash = NULL;
    uint32 ip_mask = 0;
    uint8 masklen = 0;
    ipv6_addr_t ipv6_mask;

    SYS_IPUC_DBG_FUNC();
    sal_memset(&ipv6_mask, 0, sizeof(ipv6_mask));

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        IPV4_LEN_TO_MASK(ip_mask, p_ipuc_info->masklen);
        masklen = p_gg_ipuc_master[lchip]->masklen_l;
    }
    else
    {
        IPV6_LEN_TO_MASK(ipv6_mask, p_ipuc_info->masklen);
        masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    }

    /* need push to all tcam key */
    if (p_ipuc_info->masklen < masklen)
    {
        if(!p_gg_ipuc_master[lchip]->tcam_mode)
        {
            for (i = 0; i < p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; i++)
            {
                if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].type == SYS_IPUC_TCAM_FLAG_l3_HASH)
                {
                    p_hash = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].p_info;

                    if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] == INVALID_POINTER_OFFSET)
                    {
                        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
                        {
                            if (((p_hash->ip32[0] & ip_mask) == (p_ipuc_info->ip.ipv4[0] & ip_mask))
                                && (p_hash->vrf_id == p_ipuc_info->vrf_id))
                            {
                                if (p_hash->is_pushed)
                                {
                                    if (p_hash->masklen_pushed <= p_ipuc_info->masklen)
                                    {
                                        _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_4, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                        p_hash->masklen_pushed = p_ipuc_info->masklen;
                                        p_hash->is_mask_valid = 1;
                                    }
                                }
                                else
                                {
                                    _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_4, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                    p_hash->is_pushed = 1;
                                    p_hash->masklen_pushed = p_ipuc_info->masklen;
                                    p_hash->is_mask_valid = 1;
                                }
                            }
                        }
                        else    /* ipv6 */
                        {
                            for (j = 0; j < 4; j ++)
                            {
                                if ((p_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3-j] & ipv6_mask[j]))
                                {
                                    break;
                                }
                            }

                            if (j == 4)
                            {
                                if (p_hash->vrf_id == p_ipuc_info->vrf_id)
                                {
                                    if (p_hash->is_pushed)
                                    {
                                        if (p_hash->masklen_pushed <= p_ipuc_info->masklen)
                                        {
                                            _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_6, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                            p_hash->masklen_pushed = p_ipuc_info->masklen;
                                            p_hash->is_mask_valid = 1;
                                        }
                                    }
                                    else
                                    {
                                        _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_6, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                        p_hash->is_pushed = 1;
                                        p_hash->masklen_pushed = p_ipuc_info->masklen;
                                        p_hash->is_mask_valid = 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            uint32 block_id_start = 0;
            uint32 block_id_end   = 0;

            if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
            {
                /* fix ipv6 block index */
                if (!p_gg_ipuc_master[lchip]->use_hash16)
                {
                    block_id_start = 8;
                    block_id_end   = 8 + 1;
                }
                else
                {
                    block_id_start = 0;
                    block_id_end   = 0 + 1;
                }
            }
            else
            {
                /* fix ipv6 block index */
                if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
                {
                    block_id_start = 64 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
                    block_id_end   = 64 + 1 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
                }
                else
                {
                    block_id_start = 64;
                    block_id_end   = 64 + 1;
                }
            }

            for (; block_id_start < block_id_end; block_id_start++)
            {
                uint32 left = 0;
                uint32 right = 0;
                uint32 use_count = 0;
                uint32 check_count = 0;

                left  = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].all_left_b;
                right = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].all_right_b;
                use_count = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].used_of_num;

                for (; (left <= right) && (check_count < use_count); left++)
                {
                    if (NULL == SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], left))
                    {
                        continue;
                    }

                    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][left] == SYS_IPUC_TCAM_FLAG_l3_HASH)
                    {
                        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], left);

                        if (!p_hash)
                            continue;

                        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] == INVALID_POINTER_OFFSET)
                        {
                            if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
                            {
                                if (((p_hash->ip32[0] & ip_mask) == (p_ipuc_info->ip.ipv4[0] & ip_mask))
                                    && (p_hash->vrf_id == p_ipuc_info->vrf_id))
                                {
                                    if (p_hash->is_pushed)
                                    {
                                        if (p_hash->masklen_pushed <= p_ipuc_info->masklen)
                                        {
                                            _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_4, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                            p_hash->masklen_pushed = p_ipuc_info->masklen;
                                            p_hash->is_mask_valid = 1;
                                        }
                                    }
                                    else
                                    {
                                        _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_4, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                        p_hash->is_pushed = 1;
                                        p_hash->masklen_pushed = p_ipuc_info->masklen;
                                        p_hash->is_mask_valid = 1;
                                    }
                                }
                            }
                            else    /* ipv6 */
                            {
                                for (j = 0; j < 4; j ++)
                                {
                                    if ((p_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3 - j] & ipv6_mask[j]))
                                    {
                                        break;
                                    }
                                }

                                if (j == 4)
                                {
                                    if (p_hash->vrf_id == p_ipuc_info->vrf_id)
                                    {
                                        if (p_hash->is_pushed)
                                        {
                                            if (p_hash->masklen_pushed <= p_ipuc_info->masklen)
                                            {
                                                _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_6, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                                p_hash->masklen_pushed = p_ipuc_info->masklen;
                                                p_hash->is_mask_valid = 1;
                                            }
                                        }
                                        else
                                        {
                                            _sys_goldengate_l3_hash_update_tcam_ad(lchip, CTC_IP_VER_6, p_hash->hash_offset[LPM_TYPE_POINTER], p_ipuc_info->ad_offset);
                                            p_hash->is_pushed = 1;
                                            p_hash->masklen_pushed = p_ipuc_info->masklen;
                                            p_hash->is_mask_valid = 1;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    check_count++;
                }
            }
        }
    }

    return CTC_E_NONE;
}

/* only hash key need push up */
int32
_sys_goldengate_ipuc_push_up_itself(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    sys_l3_hash_t* p_hash = NULL;
    uint8 masklen = 0;

    SYS_IPUC_DBG_FUNC();

    masklen = (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info)) ? p_gg_ipuc_master[lchip]->masklen_l : p_gg_ipuc_master[lchip]->masklen_ipv6_l;

    _sys_goldengate_l3_hash_get_hash_tbl(lchip, p_ipuc_info, &p_hash);
    if (!p_hash)
    {
        return CTC_E_NONE;
    }

    if (p_ipuc_info->masklen != masklen)
    {
        return CTC_E_NONE;
    }

    _sys_goldengate_ipuc_push_itself(lchip, p_hash, SYS_IPUC_VER(p_ipuc_info), 0);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_push_up_all(uint8 lchip, sys_ipuc_info_t* p_ipuc_info)
{
    uint16 i = 0, j = 0;
    uint8  masklen = 0;
    sys_l3_hash_t* p_hash = NULL;
    uint32 ip_mask = 0;
    ipv6_addr_t ipv6_mask;

    SYS_IPUC_DBG_FUNC();

    if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
    {
        IPV4_LEN_TO_MASK(ip_mask, p_ipuc_info->masklen);
        masklen = p_gg_ipuc_master[lchip]->masklen_l;
    }
    else
    {
        sal_memset(&ipv6_mask, 0, sizeof(ipv6_mask));
        IPV6_LEN_TO_MASK(ipv6_mask, p_ipuc_info->masklen);
        masklen = p_gg_ipuc_master[lchip]->masklen_ipv6_l;
    }

    /* need push to all tcam key */
    if (p_ipuc_info->masklen < masklen)
    {
        if(!p_gg_ipuc_master[lchip]->tcam_mode)
        {
            for (i = 0; i < p_gg_ipuc_db_master[lchip]->tcam_ip_count[SYS_IPUC_VER(p_ipuc_info)]; i++)
            {
                if (p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].type == SYS_IPUC_TCAM_FLAG_l3_HASH)
                {
                    p_hash = p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[SYS_IPUC_VER(p_ipuc_info)][i].p_info;

                    if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] == INVALID_POINTER_OFFSET)
                    {
                        if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
                        {
                            if (((p_hash->ip32[0] & ip_mask) == (p_ipuc_info->ip.ipv4[0] & ip_mask))
                                && p_hash->is_pushed
                                && (p_hash->masklen_pushed == p_ipuc_info->masklen))
                            {
                                _sys_goldengate_ipuc_push_itself(lchip, p_hash, CTC_IP_VER_4, p_ipuc_info->key_offset);
                            }
                        }
                        else    /* ipv6 */
                        {
                            for (j = 0; j < 4; j ++)
                            {
                                if ((p_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3-j] & ipv6_mask[j]))
                                {
                                    break;
                                }
                            }

                            if (j == 4)
                            {
                                if (p_hash->is_pushed && (p_hash->masklen_pushed == p_ipuc_info->masklen))
                                {
                                    _sys_goldengate_ipuc_push_itself(lchip, p_hash, CTC_IP_VER_6, p_ipuc_info->key_offset);
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            uint32 block_id_start = 0;
            uint32 block_id_end   = 0;

            if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
            {
                /* fix ipv6 block index */
                if (!p_gg_ipuc_master[lchip]->use_hash16)
                {
                    block_id_start = 8;
                    block_id_end   = 8 + 1;
                }
                else
                {
                    block_id_start = 0;
                    block_id_end   = 0 + 1;
                }
            }
            else
            {
                /* fix ipv6 block index */
                if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
                {
                    block_id_start = 64 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
                    block_id_end   = 64 + 1 + (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
                }
                else
                {
                    block_id_start = 64;
                    block_id_end   = 64 + 1;
                }
            }

            for (; block_id_start < block_id_end; block_id_start++)
            {
                uint32 left = 0;
                uint32 right = 0;
                uint32 use_count = 0;
                uint32 check_count = 0;

                left  = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].all_left_b;
                right = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].all_right_b;
                use_count = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[SYS_IPUC_VER(p_ipuc_info)].block[block_id_start].used_of_num;

                for (; (left <= right) && (check_count < use_count); left++)
                {
                    if (NULL == SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], left))
                    {
                        continue;
                    }

                    if (p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[SYS_IPUC_VER(p_ipuc_info)][left] == SYS_IPUC_TCAM_FLAG_l3_HASH)
                    {
                        p_hash = SORT_KEY_GET_OFFSET_PTR(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[SYS_IPUC_VER(p_ipuc_info)], left);

                        if (!p_hash)
                            continue;

                        if (p_hash->hash_offset[LPM_TYPE_NEXTHOP] == INVALID_POINTER_OFFSET)
                        {
                            if (CTC_IP_VER_4 == SYS_IPUC_VER(p_ipuc_info))
                            {
                                if (((p_hash->ip32[0] & ip_mask) == (p_ipuc_info->ip.ipv4[0] & ip_mask))
                                    && p_hash->is_pushed
                                && (p_hash->masklen_pushed == p_ipuc_info->masklen))
                                {
                                    _sys_goldengate_ipuc_push_itself(lchip, p_hash, CTC_IP_VER_4, p_ipuc_info->key_offset);
                                }
                            }
                            else    /* ipv6 */
                            {
                                for (j = 0; j < 4; j ++)
                                {
                                    if ((p_ipuc_info->ip.ipv6[j] & ipv6_mask[j]) != (p_hash->ip32[3 - j] & ipv6_mask[j]))
                                    {
                                        break;
                                    }
                                }

                                if (j == 4)
                                {
                                    if (p_hash->is_pushed && (p_hash->masklen_pushed == p_ipuc_info->masklen))
                                    {
                                        _sys_goldengate_ipuc_push_itself(lchip, p_hash, CTC_IP_VER_6, p_ipuc_info->key_offset);
                                    }
                                }
                            }
                        }
                    }

                    check_count++;
                }
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gg_ipuc_db_master[lchip]->ipuc_hash[ip_ver], fn, data);
}

STATIC int32
_sys_goldengate_ipuc_nat_sa_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, fn, data);
}

STATIC int32
_sys_goldengate_ipuc_db_init_share_mode(uint8 lchip)
{
    int32 i = 0;
    uint32 ipv6_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint32 table_size = 0;
    uint8 ipv4_block_num = 0;
    uint32 block_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_goldengate_opf_t opf;

    /* 1. data init */
    CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 0, &ipv4_table_size));
    ipv4_table_size = ipv4_table_size*2;
    if (ipv4_table_size <= 0)
    {
        p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = FALSE;
        return CTC_E_INVALID_CONFIG;
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 1, &ipv6_table_size));
    ipv6_table_size = ipv6_table_size*2;
    if (ipv6_table_size <= 0)
    {
        p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = FALSE;
        return CTC_E_INVALID_CONFIG;
    }
    p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = TRUE;
    p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = TRUE;

    p_gg_ipuc_db_master[lchip]->share_blocks = (sys_sort_block_t*)mem_malloc(MEM_IPUC_MODULE, sizeof(sys_sort_block_t)*(CTC_IPV4_ADDR_LEN_IN_BIT+CTC_IPV6_ADDR_LEN_IN_BIT));
    if (NULL == p_gg_ipuc_db_master[lchip]->share_blocks)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_ipuc_db_master[lchip]->share_blocks, 0, sizeof(sys_sort_block_t)*(CTC_IPV4_ADDR_LEN_IN_BIT + CTC_IPV6_ADDR_LEN_IN_BIT));
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_UP;

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV4_UC_BLOCK, CTC_IPV4_ADDR_LEN_IN_BIT + CTC_IPV6_ADDR_LEN_IN_BIT));
    opf.pool_type = OPF_IPV4_UC_BLOCK;
    init_info.opf = &opf;
    init_info.max_offset_num = ipv4_table_size + ipv6_table_size;

    table_size = ipv4_table_size;
    if (1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
    {
        if (p_gg_ipuc_master[lchip]->use_hash16)
        {
            table_size -= 4;
        }
        else
        {
            table_size -= 8;
        }
    }

    if (p_gg_ipuc_master[lchip]->use_hash16)
    {
        /* 3) create avl tree for every block */
        /*bolck 0, mask len > 15, offset 0-(ipv4_block_size - 15 - 1)*/
        init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[0];
        init_info.boundary_l = 0;
        init_info.boundary_r = table_size - 15 * 4 - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));

        /*block 1-16, mask len 15 - 1, offset 4*/
        for (i = 1; i < 16; i++)
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + 4 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }

        if (1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = ipv4_table_size - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }
    }
    else
    {
        /* 3) create avl tree for every block */
        /* bolck 0, mask len == 32, offset 0-1023 */
        init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
        init_info.boundary_l = 0;
        init_info.boundary_r = 1024 - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        table_size -= 1024;

        /* block (1 ~ 7), mask len (31 - 25), offset 4 */
        for (i = 1; i < 8; i++)
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + 4 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            table_size -= 4;
        }

        /* block (8), mask len (8), offset 4 */
        init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[8];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + table_size - 56 - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        table_size = table_size - (table_size - 56);


        /* block (9 ~ 16), mask len (7 - 0), offset 8 */
        for (i = 9; i < 16; i++)
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + 8 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            table_size -= 8;
        }

        if (1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = ipv4_table_size - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }
    }

    ipv4_block_num = (CTC_IPV4_ADDR_LEN_IN_BIT / 2) + (1 == p_gg_ipuc_master[lchip]->default_route_mode);
    table_size = ipv6_table_size;
    block_size = table_size / CTC_IPV6_ADDR_LEN_IN_BIT;
    init_info.multiple = 4;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;


    /* bolck 0, mask len 128, entry block_size */
    init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[ipv4_block_num];
    init_info.boundary_l = ipv4_table_size;
    init_info.boundary_r = ipv4_table_size + block_size - 1;
    init_info.is_block_can_shrink = TRUE;
    opf.pool_index ++;
    CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
    table_size -= 8;

    /* block 1-63, mask len 127-65, entry block_size */
    for (i = 1; i < 64; i++)
    {
        init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i + ipv4_block_num];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + block_size - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        table_size -= 1;
    }

    /* block 64, mask len 48 ~ 64, offset block_size * (64-48) */
    init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[64 + ipv4_block_num];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + block_size * (64 - 48 + 1) - 1;
    init_info.is_block_can_shrink = TRUE;
    opf.pool_index++;
    CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));

    /* block 65, mask len 47 ~ 1, offset block_size */
    for (i = 65; i < 112; i++)
    {
        init_info.block = &p_gg_ipuc_db_master[lchip]->share_blocks[i + ipv4_block_num];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + block_size - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
    }




    /* 4) init the offset array */
    CTC_ERROR_RETURN(sys_goldengate_sort_key_init_offset_array(lchip,
                                                               &p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], (ipv4_table_size + ipv6_table_size)));
    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4] = mem_malloc(MEM_IPUC_MODULE, (ipv4_table_size + ipv6_table_size));
    sal_memset(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4], 0, sizeof(uint8)*(ipv4_table_size + ipv6_table_size));

    CTC_ERROR_RETURN(sys_goldengate_sort_key_init_offset_array(lchip,
                                                               &p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], (ipv4_table_size + ipv6_table_size)));
    p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6] = mem_malloc(MEM_IPUC_MODULE, (ipv4_table_size + ipv6_table_size));
    sal_memset(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6], 0, sizeof(uint8)*(ipv4_table_size + ipv6_table_size));


    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block = p_gg_ipuc_db_master[lchip]->share_blocks;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num = opf.pool_index + 1;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].sort_key_syn_key = _sys_goldengate_syn_key;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].type = OPF_IPV4_UC_BLOCK;

    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block = p_gg_ipuc_db_master[lchip]->share_blocks;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num = opf.pool_index + 1;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].sort_key_syn_key = _sys_goldengate_syn_key;
    p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].type = OPF_IPV4_UC_BLOCK;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_ipv4_db_init(uint8 lchip)
{
    int32 i = 0;
    uint32 tmp_table_size = 0;
    uint32 table_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_goldengate_opf_t opf;

    /* 1. data init */
    sal_memset(p_gg_ipuc_db_master[lchip]->ipv4_blocks, 0, sizeof(p_gg_ipuc_db_master[lchip]->ipv4_blocks));
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv440Key_t, &table_size));
    if (table_size <= 0)
    {
        p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = FALSE;
        return CTC_E_NONE;
    }

    tmp_table_size = table_size;

    p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] = TRUE;

    if (p_gg_ipuc_master[lchip]->tcam_mode)
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV4_UC_BLOCK, CTC_IPV4_ADDR_LEN_IN_BIT + 1));

        opf.pool_type = OPF_IPV4_UC_BLOCK;
        init_info.opf = &opf;
        init_info.max_offset_num = table_size;

        if(1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
        {
            table_size -= p_gg_ipuc_master[lchip]->max_vrfid[CTC_IP_VER_4];
        }

        if(p_gg_ipuc_master[lchip]->use_hash16)
        {
            /* 3) create avl tree for every block */
            /*bolck 0, mask len > 15, offset 0-(ipv4_block_size - 15 - 1)*/
            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[0];
            init_info.boundary_l = 0;
            init_info.boundary_r = table_size - 15 * 4 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = 0;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));

            /*block 1-16, mask len 15 - 1, offset 4*/
            for (i = 1; i < 16; i++)
            {
                init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
                init_info.boundary_l = init_info.boundary_r + 1;
                init_info.boundary_r = init_info.boundary_l + 4 - 1;
                init_info.is_block_can_shrink = TRUE;
                opf.pool_index++;
                CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            }

            if(1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
            {
                init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
                init_info.boundary_l = init_info.boundary_r + 1;
                init_info.boundary_r = tmp_table_size - 1;
                init_info.is_block_can_shrink = FALSE;
                opf.pool_index++;
                CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            }
        }
        else
        {
            /* 3) create avl tree for every block */
            /* bolck 0, mask len == 32, offset 0-1023 */
            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
            init_info.boundary_l = 0;
            init_info.boundary_r = 1024 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index = 0;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            table_size -= 1024;

            /* block (1 ~ 7), mask len (31 - 25), offset 4 */
            for (i = 1; i < 8; i++)
            {
                init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
                init_info.boundary_l = init_info.boundary_r + 1;
                init_info.boundary_r = init_info.boundary_l + 4 - 1;
                init_info.is_block_can_shrink = TRUE;
                opf.pool_index++;
                CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
                table_size -= 4;
            }

            /* block (8), mask len (8), offset 4 */
            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[8];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + table_size - 56 - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            table_size = table_size - (table_size - 56);


            /* block (9 ~ 16), mask len (7 - 0), offset 8 */
            for (i = 9; i < 16; i++)
            {
                init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
                init_info.boundary_l = init_info.boundary_r + 1;
                init_info.boundary_r = init_info.boundary_l + 8 - 1;
                init_info.is_block_can_shrink = TRUE;
                opf.pool_index++;
                CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
                table_size -= 8;
            }

            if(1 == p_gg_ipuc_master[lchip]->default_route_mode) /* TCAM mode */
            {
                init_info.block = &p_gg_ipuc_db_master[lchip]->ipv4_blocks[i];
                init_info.boundary_l = init_info.boundary_r + 1;
                init_info.boundary_r = tmp_table_size - 1;
                init_info.is_block_can_shrink = FALSE;
                opf.pool_index++;
                CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            }
        }

        /* 4) init the offset array */
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_offset_array(lchip,
                             &p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4], tmp_table_size));
        p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4] = mem_malloc(MEM_IPUC_MODULE, tmp_table_size);
        sal_memset(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4],0,sizeof(uint8)*tmp_table_size);
    }
    else
    {
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_tcam_manager_t)*table_size);
        if (NULL == p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset((uint8*)p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4], 0, sizeof(sys_ipuc_tcam_manager_t)*table_size);
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipv6_db_init(uint8 lchip)
{
    int32 i;
    uint32 table_size = 0;
    uint32 tmp_table_size = 0;
    uint32 block_size = 0;
    sys_sort_block_init_info_t init_info;
    sys_goldengate_opf_t opf;

    /* 1. data init */
    sal_memset(p_gg_ipuc_db_master[lchip]->ipv6_blocks, 0, sizeof(p_gg_ipuc_db_master[lchip]->ipv6_blocks));
    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsLpmTcamIpv6160Key0_t, &table_size));

    if (table_size <= 0)
    {
        p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = FALSE;
        return CTC_E_NONE;
    }

    tmp_table_size = table_size;

    p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] = TRUE;

    if (p_gg_ipuc_master[lchip]->tcam_mode)
    {
        block_size = table_size / CTC_IPV6_ADDR_LEN_IN_BIT;

        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV6_UC_BLOCK, CTC_IPV6_ADDR_LEN_IN_BIT + 1));
        opf.pool_type = OPF_IPV6_UC_BLOCK;
        init_info.opf = &opf;
        init_info.max_offset_num = table_size;


        /* bolck 0, mask len 128, entry block_size */
        init_info.block = &p_gg_ipuc_db_master[lchip]->ipv6_blocks[0];
        init_info.boundary_l = 0;
        init_info.boundary_r = block_size - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index = 0;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        table_size -= 8;

        /* block 1-63, mask len 127-65, entry block_size */
        for (i = 1; i < 64; i++)
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv6_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + block_size - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
            table_size -= 1;
        }

        /* block 64, mask len 48 ~ 64, offset block_size * (64-48) */
        init_info.block = &p_gg_ipuc_db_master[lchip]->ipv6_blocks[64];
        init_info.boundary_l = init_info.boundary_r + 1;
        init_info.boundary_r = init_info.boundary_l + block_size * (64-48+1) - 1;
        init_info.is_block_can_shrink = TRUE;
        opf.pool_index++;
        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));

        /* block 65, mask len 47 ~ 1, offset block_size */
        for (i = 65; i < 112; i++)
        {
            init_info.block = &p_gg_ipuc_db_master[lchip]->ipv6_blocks[i];
            init_info.boundary_l = init_info.boundary_r + 1;
            init_info.boundary_r = init_info.boundary_l + block_size - 1;
            init_info.is_block_can_shrink = TRUE;
            opf.pool_index++;
            CTC_ERROR_RETURN(sys_goldengate_sort_key_init_block(lchip, &init_info));
        }

        CTC_ERROR_RETURN(sys_goldengate_sort_key_init_offset_array(lchip,
                             (skinfo_t***)&p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6], tmp_table_size));
        p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6] = mem_malloc(MEM_IPUC_MODULE, tmp_table_size);
        sal_memset(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6],0,sizeof(uint8)*tmp_table_size);
    }
    else
    {
        p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_tcam_manager_t)*table_size);
        if (NULL == p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset((uint8*)p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6], 0, sizeof(sys_ipuc_tcam_manager_t)*table_size);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipuc_db_init(uint8 lchip)
{
    uint32 ad_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint32 ipv6_table_size = 0;
    ctc_spool_t spool;

    if (NULL == p_gg_ipuc_db_master[lchip])
    {
        p_gg_ipuc_db_master[lchip] = mem_malloc(MEM_IPUC_MODULE, sizeof(sys_ipuc_db_master_t));
        if (!p_gg_ipuc_db_master[lchip])
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_gg_ipuc_db_master[lchip], 0, sizeof(sys_ipuc_db_master_t));

        /* init ad spool */
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpDa_t, &ad_table_size));
        ad_table_size = (ad_table_size + SYS_IPUC_AD_SPOOL_BLOCK_SIZE - 1) /
            SYS_IPUC_AD_SPOOL_BLOCK_SIZE * SYS_IPUC_AD_SPOOL_BLOCK_SIZE;

        sal_memset(&spool, 0, sizeof(ctc_spool_t));
        spool.lchip = lchip;
        spool.block_num = ad_table_size / SYS_IPUC_AD_SPOOL_BLOCK_SIZE;
        spool.block_size = SYS_IPUC_AD_SPOOL_BLOCK_SIZE;
        spool.max_count = ad_table_size;
        spool.user_data_size = sizeof(sys_ipuc_ad_spool_t);
        spool.spool_key = (hash_key_fn)_sys_goldengate_ipuc_lpm_ad_hash_make;
        spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_ipuc_lpm_ad_hash_cmp;
        p_gg_ipuc_db_master[lchip]->ipuc_ad_spool = ctc_spool_create(&spool);
        if (!p_gg_ipuc_db_master[lchip]->ipuc_ad_spool)
        {
            return CTC_E_NO_MEMORY;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 0, &ipv4_table_size));
    CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 7, 1, &ipv6_table_size));
    if (p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6]&&(p_gg_ipuc_master[lchip]->tcam_mode
        &&ipv4_table_size&&ipv6_table_size))
    {
        p_gg_ipuc_db_master[lchip]->tcam_share_mode = sys_goldengate_ftm_get_lpm0_share_mode(lchip);
    }


    /*init for IPv4*/
    if (p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && !p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4])
    {
        p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4] = ctc_hash_create(1,
                                                                              (IPUC_IPV4_HASH_MASK + 1),
                                                                              (hash_key_fn)_sys_goldengate_ipv4_hash_make,
                                                                              (hash_cmp_fn)_sys_goldengate_ipv4_hash_cmp);
        if (!p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4])
        {
            return CTC_E_NO_MEMORY;
        }

        p_gg_ipuc_db_master[lchip]->ipuc_nat_hash = ctc_hash_create(1,
                                                                    (IPUC_IPV4_HASH_MASK + 1),
                                                                    (hash_key_fn)_sys_goldengate_ipv4_nat_hash_make,
                                                                    (hash_cmp_fn)_sys_goldengate_ipv4_nat_hash_cmp);
        if (!p_gg_ipuc_db_master[lchip]->ipuc_nat_hash)
        {
            return CTC_E_NO_MEMORY;
        }

        if ((0 == p_gg_ipuc_db_master[lchip]->tcam_share_mode) && ipv4_table_size)
        {
            CTC_ERROR_RETURN(_sys_goldengate_ipv4_db_init(lchip));
            if (p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_4] == TRUE)
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].block = p_gg_ipuc_db_master[lchip]->ipv4_blocks;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num = CTC_IPV4_ADDR_LEN_IN_BIT + 1;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].sort_key_syn_key = _sys_goldengate_ipv4_syn_key;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].type = OPF_IPV4_UC_BLOCK;
            }
        }
    }

    if (p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6] && !p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6])
    {
        p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6] = ctc_hash_create(1,
                                                                              (IPUC_IPV6_HASH_MASK + 1),
                                                                              (hash_key_fn)_sys_goldengate_ipv6_hash_make,
                                                                              (hash_cmp_fn)_sys_goldengate_ipv6_hash_cmp);
        if (!p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6])
        {
            return CTC_E_NO_MEMORY;
        }

        if ((0 == p_gg_ipuc_db_master[lchip]->tcam_share_mode) && ipv6_table_size)
        {
            CTC_ERROR_RETURN(_sys_goldengate_ipv6_db_init(lchip));
            if (p_gg_ipuc_db_master[lchip]->tcam_en[CTC_IP_VER_6] == TRUE)
            {
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].block = p_gg_ipuc_db_master[lchip]->ipv6_blocks;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num = CTC_IPV6_ADDR_LEN_IN_BIT + 1;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].sort_key_syn_key = _sys_goldengate_ipv6_syn_key;
                p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].type = OPF_IPV6_UC_BLOCK;
            }
        }
    }

    if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
    {
        CTC_ERROR_RETURN(_sys_goldengate_ipuc_db_init_share_mode(lchip));
    }



    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_db_free_node_data(void* node_data, void* user_data)
{
    uint8 lchip = 0;
    sys_ipuc_info_t* p_ipuc_info = NULL;

    if (user_data)
    {
        lchip = *(uint8*)user_data;

        p_ipuc_info=(sys_ipuc_info_t*)node_data;
        if (DO_LPM == p_ipuc_info->route_opt)
        {
            _sys_goldengate_lpm_del(lchip, p_ipuc_info);
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
_sys_goldengate_ipuc_db_deinit(uint8 lchip)
{
    uint8 lchip_id = lchip;

    if (NULL == p_gg_ipuc_db_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_6] && p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_6].max_block_num)
    {
        if (p_gg_ipuc_master[lchip]->tcam_mode)
        {
            if (0 == p_gg_ipuc_db_master[lchip]->tcam_share_mode)
            {
                sys_goldengate_opf_deinit(lchip, OPF_IPV6_UC_BLOCK);
            }

            mem_free(*p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6]);
            mem_free(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_6]);
            mem_free(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_6]);
        }
        else
        {
            mem_free(p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_6]);
        }

        /*free ipuc v6 hash key*/
        ctc_hash_traverse(p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_goldengate_ipuc_db_free_node_data, &lchip_id);
        ctc_hash_free(p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_6]);
    }


    if (p_gg_ipuc_master[lchip]->version_en[CTC_IP_VER_4] && p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num)
    {

        if (p_gg_ipuc_master[lchip]->tcam_mode)
        {
            mem_free(*p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4]);
            mem_free(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_array[CTC_IP_VER_4]);
            mem_free(p_gg_ipuc_db_master[lchip]->p_ipuc_offset_type[CTC_IP_VER_4]);
            sys_goldengate_opf_deinit(lchip, OPF_IPV4_UC_BLOCK);
        }
        else
        {
            mem_free(p_gg_ipuc_db_master[lchip]->p_tcam_ipuc_info[CTC_IP_VER_4]);
        }

        /*free ipuc nat hash*/
        ctc_hash_traverse(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash, (hash_traversal_fn)_sys_goldengate_ipuc_db_free_node_data, NULL);
        ctc_hash_free(p_gg_ipuc_db_master[lchip]->ipuc_nat_hash);

        /*free ipuc v4 hash key*/
        ctc_hash_traverse(p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_goldengate_ipuc_db_free_node_data, &lchip_id);
        ctc_hash_free(p_gg_ipuc_db_master[lchip]->ipuc_hash[CTC_IP_VER_4]);
    }

    if (p_gg_ipuc_db_master[lchip]->share_blocks)
    {
        mem_free(p_gg_ipuc_db_master[lchip]->share_blocks);
    }

    /*free ipuc ad data*/
    ctc_spool_free(p_gg_ipuc_db_master[lchip]->ipuc_ad_spool);

    mem_free(p_gg_ipuc_db_master[lchip]);
    return CTC_E_NONE;
}

hash_cmp_fn
sys_goldengate_get_hash_cmp(uint8 lchip, ctc_ip_ver_t ctc_ip_ver)
{
    if (CTC_IP_VER_4 == ctc_ip_ver)
    {
        return (hash_cmp_fn) & _sys_goldengate_ipv4_hash_cmp;
    }
    else if (CTC_IP_VER_6 == ctc_ip_ver)
    {
        return (hash_cmp_fn) & _sys_goldengate_ipv6_hash_cmp;
    }
    else
    {
        return NULL;
    }
}

#define __SHOW__
int32
sys_goldengate_show_ipuc_info(sys_ipuc_info_t* p_ipuc_data, void* data)
{
    uint32 detail = 0;
    uint8   lchip = 0;
    uint32  route_flag = 0;
    char buf[CTC_IPV6_ADDR_STR_LEN];

#define SYS_IPUC_MASK_LEN 5
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
        sal_strncat(buf, buf2, SYS_IPUC_MASK_LEN);
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
        sal_strncat(buf, buf2, SYS_IPUC_MASK_LEN);
        SYS_IPUC_DBG_DUMP("%-5d %-44s", p_ipuc_data->vrf_id, buf);
    }

    buf2[0] = '\0';
    route_flag = p_ipuc_data->route_flag;
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_MERGE_KEY);
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_IS_IPV6);
    CTC_UNSET_FLAG(route_flag, SYS_IPUC_FLAG_DEFAULT);

    if (CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_RPF_CHECK))
    {
        sal_strncat(buf2, "R", 1);
    }

    if (!CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_TTL_NO_CHECK))
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

    if ('\0' == buf2[0])
    {
        sal_strncat(buf2, "O", 1);
    }

    SYS_IPUC_DBG_DUMP("  %-4d   %-10s   %-7s\n\r",
                      p_ipuc_data->nh_id, buf2,
                      (!CTC_FLAG_ISSET(route_flag, CTC_IPUC_FLAG_NEIGHBOR)) ? "FALSE" :
                      (p_ipuc_data->route_opt != DO_HASH) ? "FALSE":"TRUE");

    if (detail)
    {
        sys_goldengate_ipuc_retrieve_ip(lchip, p_ipuc_data);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, sys_ipuc_info_t* p_ipuc_data,uint32 detail)
{
    sys_ipuc_traverse_t travs;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    SYS_IPUC_DBG_DUMP("Offset: T-TCAM    S-SRAM\n\r");
    SYS_IPUC_DBG_DUMP("Flags:  R-RPF check    T-TTL check    I-ICMP redirect check      C-Send to CPU\n\r");
    SYS_IPUC_DBG_DUMP("        N-Neighbor     X-Connect      P-Protocol entry           S-Self address\n\r");
    SYS_IPUC_DBG_DUMP("        U-TCP port     E-ICMP error msg check    O-None flag\n\r");
    SYS_IPUC_DBG_DUMP("---------------------------------------------------------------------------------------\n\r");
    if (ip_ver == CTC_IP_VER_4)
    {
        SYS_IPUC_DBG_DUMP("VRF   Route                           NHID   L3If_flags   In_SRAM\n\r");
    }
    else
    {
        SYS_IPUC_DBG_DUMP("VRF   Route                                         NHID   L3If_flags   In_SRAM\n\r");
    }
    SYS_IPUC_DBG_DUMP("---------------------------------------------------------------------------------------\n\r");

    travs.data = &detail;
    travs.lchip = lchip;

    if(p_ipuc_data)
    {
        sys_goldengate_show_ipuc_info(p_ipuc_data,(void*)&travs);
    }
    else
    {
        _sys_goldengate_ipuc_db_traverse(lchip ,ip_ver, (hash_traversal_fn)sys_goldengate_show_ipuc_info, (void*)&travs);
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_db_show_count(uint8 lchip)
{
    uint32 i;

    SYS_IPUC_INIT_CHECK(lchip);

    for (i = 0; i < MAX_CTC_IP_VER; i++)
    {
        if ((!p_gg_ipuc_master[lchip]) || (!p_gg_ipuc_db_master[lchip]) || !p_gg_ipuc_master[lchip]->version_en[i])
        {
            SYS_IPUC_DBG_DUMP("SDK is not assigned resource for IPv%c version.\n\r", (i == CTC_IP_VER_4) ? '4' : '6');
            continue;
        }

        if (p_gg_ipuc_db_master[lchip]->ipuc_hash[i])
        {
            SYS_IPUC_DBG_DUMP("IPv%c total %d routes.\n\r", (i == CTC_IP_VER_4) ? '4' : '6',
                          p_gg_ipuc_db_master[lchip]->ipuc_hash[i]->count);
        }
    }

    /*sys_goldengate_l3_hash_state_show(lchip);*/

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipuc_show_nat_sa_info(sys_ipuc_nat_sa_info_t* p_nat_info, void* data)
{
    uint32 detail = *((uint32*)data);
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
#define SYS_IPUC_NAT_L4PORT_LEN 7
    uint32 tempip = 0;

    SYS_IPUC_DBG_DUMP("%-8d", p_nat_info->vrf_id);

    tempip = sal_ntohl(p_nat_info->ipv4[0]);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPUC_DBG_DUMP("%-18s", buf);

    if (p_nat_info->l4_src_port)
    {
        SYS_IPUC_DBG_DUMP("%-12u", p_nat_info->l4_src_port);
    }
    else
    {
        SYS_IPUC_DBG_DUMP("%-12s", "-");
    }

    tempip = sal_ntohl(p_nat_info->new_ipv4[0]);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPUC_DBG_DUMP("%-18s", buf);

    if (p_nat_info->new_l4_src_port)
    {
        SYS_IPUC_DBG_DUMP("%-10u", p_nat_info->new_l4_src_port);
    }
    else
    {
        SYS_IPUC_DBG_DUMP("%-10s", "-");
    }

    SYS_IPUC_DBG_DUMP("%-10s%-10s%u\n",
                      (p_nat_info->l4_src_port ? (p_nat_info->is_tcp_port ? "TCP" : "UDP") : "-"),
                      (p_nat_info->in_tcam ? "T" : "S"),(p_nat_info->ad_offset));

    if (detail)
    {
        /* sys_goldengate_ipuc_retrieve_ipv4(lchip, p_nat_info); */
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_show_nat_sa_db(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail)
{
    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    if (ip_ver == CTC_IP_VER_4)
    {
        SYS_IPUC_DBG_DUMP("In-SRAM: T-TCAM    S-SRAM\n\r");
        SYS_IPUC_DBG_DUMP("\n");
        SYS_IPUC_DBG_DUMP("%-8s%-18s%-12s%-18s%-10s%-10s%-10s%s\n",
                          "VRF", "IPSA", "Port", "New-IPSA", "New-Port", "TCP/UDP", "In-SRAM", "AD-Index");
        SYS_IPUC_DBG_DUMP("----------------------------------------------------------------------------------------------\n");
    }
    else
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    _sys_goldengate_ipuc_nat_sa_db_traverse(lchip, ip_ver, (hash_traversal_fn)_sys_goldengate_ipuc_show_nat_sa_info, (void*)&detail);
    return CTC_E_NONE;
}

int32
sys_goldengate_ipuc_offset_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint8 blockid, uint8 detail)
{
    sys_sort_block_t* p_block;
    sys_goldengate_opf_t opf;
    uint8 max_blockid = 0;
    uint32 all_of_num = 0;
    uint32 used_of_num = 0;

    SYS_IPUC_INIT_CHECK(lchip);
    SYS_IP_CHECK_VERSION(ip_ver);

    if (p_gg_ipuc_db_master[lchip]->tcam_share_mode)
    {
        max_blockid = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[CTC_IP_VER_4].max_block_num;
    }
    else
    {
        max_blockid = p_gg_ipuc_master[lchip]->max_mask_len[ip_ver];
        if (0 == p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].max_block_num)
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
        p_block = &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].block[blockid];

        SYS_IPUC_DBG_DUMP("%-10d %-10d %-10d %-10d %-10d %-10d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple,p_block->all_of_num, p_block->used_of_num);


        SYS_IPUC_DBG_DUMP("\n");

        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type = p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].type;
        opf.pool_index = blockid;
        sys_goldengate_opf_print_sample_info(lchip, &opf);
    }
    else
    {
        for (blockid = 0; blockid < max_blockid;  blockid++)
        {
            p_block = &p_gg_ipuc_db_master[lchip]->ipuc_sort_key_info[ip_ver].block[blockid];

            SYS_IPUC_DBG_DUMP("%-10d %-10d %-10d %-10d %-10d %-10d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple,p_block->all_of_num, p_block->used_of_num);
            all_of_num += p_block->all_of_num;
            used_of_num += p_block->used_of_num;
        }
        SYS_IPUC_DBG_DUMP( "============================================================\n");
        SYS_IPUC_DBG_DUMP( "%-10s %-10s %-10s %-10s %-10d %-10d\n", "Total",  "-", "-", "-", all_of_num, used_of_num);
    }

    return CTC_E_NONE;
}




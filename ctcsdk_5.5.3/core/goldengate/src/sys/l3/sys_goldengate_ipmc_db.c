/**
 @file sys_goldengate_ipmc.c

 @date 2010-01-14

 @version v2.0

 The file contains all ipmc related function
*/

#include "sal.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_avl_tree.h"
#include "ctc_hash.h"
#include "ctc_ipmc.h"
#include "ctc_linklist.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_ipmc.h"
#include "sys_goldengate_ipmc_db.h"
#include "sys_goldengate_rpf_spool.h"
#include "sys_goldengate_l3if.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"

#define IPMC_IPV4_HASH_SIZE     1024
#define IPMC_IPV6_HASH_SIZE     1024
#define IPMC_IPV4_HASH_POINTER_SIZE    32768
#define IPMC_IPV6_HASH_POINTER_SIZE    16384
#define IPMC_IPV6_MAC_HASH_POINTER_SIZE    1024

#define IPMC_IPV4_POINTER_HASH_SIZE     8192
#define IPMC_IPV6_POINTER_HASH_SIZE     8192
#define IPMC_IPV6_POINTER_MAC_HASH_SIZE 512

#define SYS_IPMC_DB_INIT_CHECK() \
    do {    \
        if (p_gg_ipmc_db_master[lchip] == NULL){          \
            return CTC_E_NOT_INIT; }                \
    } while (0)

sys_ipmc_db_master_t* p_gg_ipmc_db_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

extern sys_ipmc_master_t* p_gg_ipmc_master[];

struct sys_mc_count_s
{
    uint16 ipmc_sg_count;
    uint16 ipmc_g_count;
    uint16 l2mc_sg_count;
    uint16 l2mc_g_count;
};
typedef struct sys_mc_count_s sys_mc_count_t;

STATIC int32
_sys_goldengate_ipmc_db_show_count(sys_ipmc_group_db_node_t* p_node, sys_mc_count_t* count);

STATIC uint32
_sys_goldengate_ipmc_ipv4_hash_make(sys_ipmc_group_db_node_t* p_node_to_lkp)
{
    uint32 a, b, c;

    CTC_PTR_VALID_CHECK(p_node_to_lkp);

    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = 0;

    a += p_node_to_lkp->address.ipv4.group_addr;
    b += p_node_to_lkp->address.ipv4.vrfid;
    b += p_node_to_lkp->address.ipv4.src_addr << 16;
    c += ((p_node_to_lkp->flag & SYS_IPMC_FLAG_DB_L2MC) ? 1 : 0);
    c += (a & 0x1fff) ^ ((a >> 13) & 0x1fff) ^ ((a >> 26) & 0x3f) ^
        (b & 0x1fff) ^ ((b >> 13) & 0x1ffff);

    return c % IPMC_IPV4_POINTER_HASH_SIZE;
}

STATIC uint32
_sys_goldengate_ipmc_ipv6_hash_make(sys_ipmc_group_db_node_t* p_node_to_lkp)
{

    uint32 a, b, c;

    CTC_PTR_VALID_CHECK(p_node_to_lkp);

    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = 0;

    a += p_node_to_lkp->address.ipv6.group_addr[0];
    b += p_node_to_lkp->address.ipv6.group_addr[1];
    c += p_node_to_lkp->address.ipv6.group_addr[2];
    MIX(a, b, c);

    a += p_node_to_lkp->address.ipv6.group_addr[3];
    b += p_node_to_lkp->address.ipv6.vrfid;
    b += ((p_node_to_lkp->flag & SYS_IPMC_FLAG_DB_L2MC) ? 1 : 0) << 16;
    c += p_node_to_lkp->address.ipv6.src_addr[3];

    MIX(a, b, c);

    a += p_node_to_lkp->address.ipv6.src_addr[0];
    b += p_node_to_lkp->address.ipv6.src_addr[1];
    c += p_node_to_lkp->address.ipv6.src_addr[2];
    MIX(a, b, c);
    return c % IPMC_IPV6_POINTER_HASH_SIZE;

}

int32
_sys_goldengate_ipmc_ipv4_hash_compare(sys_ipmc_group_db_node_t* p_node_in_bucket, sys_ipmc_group_db_node_t* p_node_to_lkp)
{
    CTC_PTR_VALID_CHECK(p_node_in_bucket);
    CTC_PTR_VALID_CHECK(p_node_to_lkp);

    if (p_node_in_bucket->address.ipv4.vrfid != p_node_to_lkp->address.ipv4.vrfid)
    {
        return FALSE;
    }

    if ((p_node_in_bucket->flag & 0x7) != (p_node_to_lkp->flag & 0x7))
    {
        return FALSE;
    }

    if (p_node_in_bucket->address.ipv4.group_addr != p_node_to_lkp->address.ipv4.group_addr)
    {
        return FALSE;
    }

    if (p_node_in_bucket->address.ipv4.src_addr != p_node_to_lkp->address.ipv4.src_addr)
    {
        return FALSE;
    }

    return TRUE;
}

int32
_sys_goldengate_ipmc_ipv6_hash_compare(sys_ipmc_group_db_node_t* p_node_in_bucket, sys_ipmc_group_db_node_t* p_node_to_lkp)
{
    uint8 i;

    CTC_PTR_VALID_CHECK(p_node_in_bucket);
    CTC_PTR_VALID_CHECK(p_node_to_lkp);

    if (p_node_in_bucket->address.ipv6.vrfid != p_node_to_lkp->address.ipv6.vrfid)
    {
        return FALSE;
    }

    if ((p_node_in_bucket->flag & 0x7) != (p_node_to_lkp->flag & 0x7))
    {
        return FALSE;
    }

    for (i = 0; i < 4; i++)
    {
        if (p_node_in_bucket->address.ipv6.group_addr[i] != p_node_to_lkp->address.ipv6.group_addr[i])
        {
            return FALSE;
        }
    }
    for (i = 0; i < 4; i++)
    {
        if (p_node_in_bucket->address.ipv6.src_addr[i] != p_node_to_lkp->address.ipv6.src_addr[i])
        {
            return FALSE;
        }
    }

    return TRUE;
}

int32
_sys_goldengate_ipmc_db_alloc_pointer(uint8 lchip, sys_ipmc_group_db_node_t* p_ipmc_group_db_node, uint16* pointer)
{
    uint8 ip_version = 0;
    uint32 offset;
    uint32 hash_index = 0;
    sys_ipmc_group_db_node_t* p_temp_ipmc_group_db_node = NULL;
    sys_goldengate_opf_t opf;
    ctc_hash_t* hash = NULL;
    uint16 idx_1d = 0;
    uint16 idx_2d = 0;
    uint16 use_opf = 0;
    ctc_hash_bucket_t* bucket = NULL;
    ctc_hash_bucket_t* bucket_next = NULL;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    ip_version = (p_ipmc_group_db_node->flag & SYS_IPMC_FLAG_DB_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    hash = p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_version];
    p_temp_ipmc_group_db_node = ctc_hash_lookup2(hash, p_ipmc_group_db_node, &hash_index);
    if (p_temp_ipmc_group_db_node)
    {
        return CTC_E_ENTRY_EXIST;
    }
    else
    {
        idx_1d  = hash_index / hash->block_size;
        idx_2d  = hash_index % hash->block_size;
        if ((NULL != hash->index[idx_1d]) && (NULL != hash->index[idx_1d][idx_2d]))
        {
            for (bucket = hash->index[idx_1d][idx_2d]; bucket != NULL; bucket = bucket_next)
            {
                bucket_next = bucket->next;
                p_temp_ipmc_group_db_node = (sys_ipmc_group_db_node_t*)bucket->data;
                if (CTC_FLAG_ISSET(p_temp_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION))
                {
                    if (!CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC))
                    {
                        use_opf = !sal_memcmp(&(p_temp_ipmc_group_db_node->address.ipv6.src_addr), &(p_ipmc_group_db_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
                    }
                }
                else
                {
                    use_opf = !sal_memcmp(&(p_temp_ipmc_group_db_node->address.ipv4.src_addr), &(p_ipmc_group_db_node->address.ipv4.src_addr), sizeof(uint32));
                }

                if (1 == use_opf)
                {
                    break;
                }
            }
        }

        if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION) &&
            CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC))
        {
            use_opf = 1;
        }

        if (1 == use_opf)
        {
            if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION))
            {
                if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC))
                {
                    opf.pool_type = OPF_IPV6_L2MC_POINTER_BLOCK;
                }
                else
                {
                    opf.pool_type = OPF_IPV6_MC_POINTER_BLOCK;
                }
            }
            else
            {
                opf.pool_type = OPF_IPV4_MC_POINTER_BLOCK;
            }
            opf.pool_index = 0;

            CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset));
            *pointer = offset;
        }
        else
        {
            *pointer = hash_index+1;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_alloc_pointer_from_position(uint8 lchip, sys_ipmc_group_db_node_t* p_ipmc_group_db_node, uint16 pointer)
{
    uint16 use_opf = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION))
    {
        if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC))
        {
            use_opf = 1;
            opf.pool_type = OPF_IPV6_L2MC_POINTER_BLOCK;
        }
        else
        {
            if (pointer > IPMC_IPV6_POINTER_HASH_SIZE )
            {
                use_opf = 1;
                opf.pool_type = OPF_IPV6_MC_POINTER_BLOCK;
            }
        }
    }
    else
    {
        if (pointer > IPMC_IPV4_POINTER_HASH_SIZE )
        {
            use_opf = 1;
            opf.pool_type = OPF_IPV4_MC_POINTER_BLOCK;
        }
    }

    if ( use_opf )
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, pointer));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_db_free_pointer(uint8 lchip, uint16 pointer, sys_ipmc_group_db_node_t* p_ipmc_group_db_node)
{
    uint8 is_opf = 0;
    uint32 offset;
    sys_goldengate_opf_t opf;
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION))
    {
        if (CTC_FLAG_ISSET(p_ipmc_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC))
        {
            is_opf = 1;
            opf.pool_type = OPF_IPV6_L2MC_POINTER_BLOCK;
        }
        else
        {
            if (pointer > IPMC_IPV6_POINTER_HASH_SIZE )
            {
                is_opf = 1;
                opf.pool_type = OPF_IPV6_MC_POINTER_BLOCK;
            }
        }
    }
    else
    {
        if (pointer > IPMC_IPV4_POINTER_HASH_SIZE )
        {
            is_opf = 1;
            opf.pool_type = OPF_IPV4_MC_POINTER_BLOCK;
        }
    }

    if (is_opf)
    {
        opf.pool_index = 0;
        offset = pointer;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));
    }

    return CTC_E_NONE;
}

uint32 sys_goldengate_ipmc_db_get_cur_total(uint8 lchip)
{
    sys_mc_count_t ip_count;

    sal_memset(&ip_count, 0, sizeof(sys_mc_count_t));

    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_count, &ip_count);
    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_count, &ip_count);

    return ip_count.ipmc_g_count + ip_count.ipmc_sg_count + ip_count.l2mc_g_count + ip_count.l2mc_sg_count;
}

STATIC int32
_sys_goldengate_ipmc_ipv4_db_init(uint8 lchip)
{
    sys_goldengate_opf_t opf;
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    /*ipv4*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV4_MC_POINTER_BLOCK, 1));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_IPV4_MC_POINTER_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, IPMC_IPV4_POINTER_HASH_SIZE+1, IPMC_IPV4_HASH_POINTER_SIZE-IPMC_IPV4_POINTER_HASH_SIZE-1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_ipv6_db_init(uint8 lchip)
{
    sys_goldengate_opf_t opf;
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    /*ipv6*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV6_MC_POINTER_BLOCK, 1));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_IPV6_MC_POINTER_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, IPMC_IPV6_POINTER_HASH_SIZE+1, IPMC_IPV6_HASH_POINTER_SIZE-IPMC_IPV6_POINTER_HASH_SIZE-1));

    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_IPV6_L2MC_POINTER_BLOCK, 1));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_IPV6_L2MC_POINTER_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, IPMC_IPV6_MAC_HASH_POINTER_SIZE - 1));

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);

    specs_info->used_size = sys_goldengate_ipmc_db_get_cur_total(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_init(uint8 lchip)
{
    p_gg_ipmc_db_master[lchip] = mem_malloc(MEM_IPMC_MODULE, sizeof(sys_ipmc_db_master_t));
    if (NULL == p_gg_ipmc_db_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    /*ipv4 hash*/
    p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4] = ctc_hash_create(1, IPMC_IPV4_POINTER_HASH_SIZE,
                                                                (hash_key_fn)_sys_goldengate_ipmc_ipv4_hash_make,
                                                                (hash_cmp_fn)_sys_goldengate_ipmc_ipv4_hash_compare);
    if (!p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4])
    {
        return CTC_E_NO_MEMORY;
    }
    /*ipv6 hash*/
    p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6] = ctc_hash_create(1, IPMC_IPV6_POINTER_HASH_SIZE,
                                                                (hash_key_fn)_sys_goldengate_ipmc_ipv6_hash_make,
                                                                (hash_cmp_fn)_sys_goldengate_ipmc_ipv6_hash_compare);
    if (!p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6])
    {
        return CTC_E_NO_MEMORY;
    }

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_ipv4_db_init(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_ipv6_db_init(lchip));

    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_IPMC,
                sys_goldengate_ipmc_ftm_cb);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_db_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_ipmc_db_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_IPV4_MC_POINTER_BLOCK);
    sys_goldengate_opf_deinit(lchip, OPF_IPV6_MC_POINTER_BLOCK);
    sys_goldengate_opf_deinit(lchip, OPF_IPV6_L2MC_POINTER_BLOCK);

    /*free ipmc ipv4*/
    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_goldengate_ipmc_db_free_node_data, NULL);
    ctc_hash_free(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4]);

    /*free ipmc ipv6*/
    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_goldengate_ipmc_db_free_node_data, NULL);
    ctc_hash_free(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6]);

    /*sal_mutex_destroy(p_gg_ipmc_db_master[lchip]->sys_ipmc_db_mutex);*/

    mem_free(p_gg_ipmc_db_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_set_group_node(uint8 lchip, ctc_ipmc_group_info_t* p_group, sys_ipmc_group_node_t* p_group_node)
{
    uint8 addr_len = 0;
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_PTR_VALID_CHECK(p_group_node);
    p_group_node->extern_flag       = p_group->flag;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->group_id = p_group->group_id;
    p_group_node->stats_id = p_group->stats_id;
    p_group_node->rp_id = p_group->rp_id;
    p_group_node->nexthop_id = CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_WITH_NEXTHOP)?p_group->nhid:0;
    addr_len = (p_group->ip_version == CTC_IP_VER_6)? sizeof(ctc_ipmc_ipv6_addr_t) : sizeof(ctc_ipmc_ipv4_addr_t);
    sal_memcpy(&p_group_node->address, &p_group->address, addr_len);
    if (CTC_IP_VER_6 == (p_group->ip_version))
    {
        SYS_IPMC_ADDRESS_SORT(p_group_node);
    }
    SYS_IPMC_MASK_ADDR(p_group_node->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);
    if ((!(p_group->src_ip_mask_len)) && (p_group->ip_version == CTC_IP_VER_6))
    {
        sal_memset(&(p_group_node->address.ipv6.src_addr), 0, sizeof(ipv6_addr_t));
    }
    else if ((!(p_group->src_ip_mask_len)) && (p_group->ip_version == CTC_IP_VER_4))
    {
        sal_memset(&(p_group_node->address.ipv4.src_addr), 0, sizeof(uint32));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_set_group_db_node(uint8 lchip, sys_ipmc_group_node_t* p_group_node, sys_ipmc_group_db_node_t* p_group_db_node)
{
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    CTC_PTR_VALID_CHECK(p_group_db_node);

    p_group_db_node->group_id = p_group_node->group_id;
    p_group_db_node->nexthop_id = p_group_node->nexthop_id;
    p_group_db_node->flag = (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION)) ? (p_group_db_node->flag | SYS_IPMC_FLAG_DB_VERSION) : p_group_db_node->flag;
    p_group_db_node->flag = (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC)) ? (p_group_db_node->flag | SYS_IPMC_FLAG_DB_L2MC) : p_group_db_node->flag;
    p_group_db_node->flag = (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ? (p_group_db_node->flag | SYS_IPMC_FLAG_DB_SRC_ADDR) : p_group_db_node->flag;
    p_group_db_node->flag = (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_DROP)) ? (p_group_db_node->flag | SYS_IPMC_FLAG_DB_DROP) : p_group_db_node->flag;
    p_group_db_node->flag = (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE)) ? (p_group_db_node->flag | SYS_IPMC_FLAG_DB_RPF_PROFILE) : p_group_db_node->flag;
    p_group_db_node->extern_flag = p_group_node->extern_flag;
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
    {
        p_group_db_node->rpf_index = p_group_node->sa_index;
    }
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION))
    {
         sal_memcpy(&(p_group_db_node->address.ipv6.group_addr), &(p_group_node->address.ipv6.group_addr), sizeof(ipv6_addr_t));
         sal_memcpy(&(p_group_db_node->address.ipv6.src_addr), &(p_group_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
         p_group_db_node->address.ipv6.vrfid = p_group_node->address.ipv6.vrfid;
    }
    else
    {
         sal_memcpy(&(p_group_db_node->address.ipv4.group_addr), &(p_group_node->address.ipv4.group_addr), sizeof(uint32));
         sal_memcpy(&(p_group_db_node->address.ipv4.src_addr), &(p_group_node->address.ipv4.src_addr), sizeof(uint32));
         p_group_db_node->address.ipv4.vrfid = p_group_node->address.ipv4.vrfid;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_add_g_db_node(uint8 lchip, sys_ipmc_group_node_t* p_group_node, sys_ipmc_group_db_node_t** p_group_db_node)
{
    uint8 ip_version = 0;
    uint16 pointer = 0;
    int32 ret = 0;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    sys_ipmc_group_db_node_t* p_insert_group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    {
        p_base_group_node = &base_group_node;
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));
        UNSET_FLAG(p_base_group_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR);
        if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION))
        {
            sal_memset(&(p_base_group_node->address.ipv6.src_addr), 0, sizeof(ipv6_addr_t));
        }
        else
        {
            sal_memset(&(p_base_group_node->address.ipv4.src_addr), 0, sizeof(uint32));
        }
        sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
        if (!p_base_group_node)
        {
            ip_version = CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
            p_insert_group_node = mem_malloc(MEM_IPMC_MODULE,  p_gg_ipmc_master[lchip]->db_info_size[ip_version]);
            if (NULL == p_insert_group_node)
            {
                CTC_ERROR_RETURN(CTC_E_NO_MEMORY);
            }
            sal_memset(p_insert_group_node, 0, p_gg_ipmc_master[lchip]->db_info_size[ip_version]);

            ret = _sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_insert_group_node);
            if (ret != CTC_E_NONE)
            {
                mem_free(p_insert_group_node);
                CTC_ERROR_RETURN(ret);
            }
            UNSET_FLAG(p_insert_group_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR);
            if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION))
            {
                sal_memset(&(p_insert_group_node->address.ipv6.src_addr), 0, sizeof(ipv6_addr_t));
            }
            else
            {
                sal_memset(&(p_insert_group_node->address.ipv4.src_addr), 0, sizeof(uint32));
            }
            ret = _sys_goldengate_ipmc_db_alloc_pointer(lchip, p_insert_group_node, &pointer);
            if (ret != CTC_E_NONE)
            {
                mem_free(p_insert_group_node);
                CTC_ERROR_RETURN(ret);
            }
            SYS_IPMC_DBG_INFO("Alloc pointer:%d\n", pointer);
            p_group_node->pointer = pointer;
            p_group_node->refer_count = 1;
            p_insert_group_node->refer_count = 1;
            /* (**,g) */
            if(CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
            {
                p_insert_group_node->group_id = 0;
                p_insert_group_node->nexthop_id = 0;
            }
            ret = sys_goldengate_ipmc_db_add(lchip, p_insert_group_node);
            if (ret != CTC_E_NONE)
            {
                _sys_goldengate_ipmc_db_free_pointer(lchip, pointer, p_insert_group_node);
                mem_free(p_insert_group_node);
                CTC_ERROR_RETURN(ret);
            }
            p_base_group_node = p_insert_group_node;
            /* write base */
            p_group_node->write_base = TRUE;
        }
        else
        {
            p_base_group_node->refer_count = p_base_group_node->refer_count + 1;
            _sys_goldengate_ipmc_asic_lookup(lchip, p_group_node);
            if (!(CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN)))
            {
                /* write base */
                p_group_node->write_base = TRUE;
                p_base_group_node->group_id   = p_group_node->group_id;
                p_base_group_node->nexthop_id = p_group_node->nexthop_id;
            }
        }
        /* p_base_group_node is the (*,g) node */
    }
    if (!(CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN)))
    {
        CTC_SET_FLAG(p_base_group_node->flag, SYS_IPMC_FLAG_DB_VALID);
    }
    p_base_group_node->flag = (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE)) ? (p_base_group_node->flag | SYS_IPMC_FLAG_DB_RPF_PROFILE) : p_base_group_node->flag;
    p_base_group_node->extern_flag = p_group_node->extern_flag;
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
    {
        p_base_group_node->rpf_index = p_group_node->sa_index;
    }
    *p_group_db_node = p_base_group_node;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_add_sg_db_node(uint8 lchip, sys_ipmc_group_node_t* p_group_node, sys_ipmc_group_db_node_t** p_group_db_node)
{
    uint8 ip_version = 0;
    int32 ret = 0;
    sys_ipmc_group_db_node_t* p_insert_group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    ip_version = CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    p_insert_group_node = mem_malloc(MEM_IPMC_MODULE,  p_gg_ipmc_master[lchip]->db_info_size[ip_version]);
    if (NULL == p_insert_group_node)
    {
        CTC_ERROR_RETURN(CTC_E_NO_MEMORY);
    }
    sal_memset(p_insert_group_node, 0, p_gg_ipmc_master[lchip]->db_info_size[ip_version]);
    *p_group_db_node =  p_insert_group_node;

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_insert_group_node));
    ret = sys_goldengate_ipmc_db_add(lchip, p_insert_group_node);
    return ret;

}

int32
_sys_goldengate_ipmc_construct_ipv6_hash_key(uint8 lchip, sys_ipmc_group_node_t* p_group_node,
                      DsFibHost0Ipv6McastHashKey_m* ds_fib_host0_ipv6_mcast_hash_key,
                      DsFibHost0MacIpv6McastHashKey_m* ds_fib_host0_mac_ipv6_mcast_hash_key)
{
    uint32 tbl_id = 0;
    ipv6_addr_t ipv6_addr;

    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));

    /* (**,g) not found*/
    sal_memcpy(&ipv6_addr, &(p_group_node->address.ipv6.group_addr), sizeof(ipv6_addr_t));
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        tbl_id = DsFibHost0MacIpv6McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_hashType0_f, ds_fib_host0_mac_ipv6_mcast_hash_key, 5);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_hashType1_f, ds_fib_host0_mac_ipv6_mcast_hash_key, 5);
        DRV_SET_FIELD_A(tbl_id, DsFibHost0MacIpv6McastHashKey_ipDa_f, ds_fib_host0_mac_ipv6_mcast_hash_key, ipv6_addr);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_pointer_f, ds_fib_host0_mac_ipv6_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid0_f, ds_fib_host0_mac_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_valid1_f, ds_fib_host0_mac_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0MacIpv6McastHashKey_vsiId_f, ds_fib_host0_mac_ipv6_mcast_hash_key, p_group_node->address.ipv6.vrfid);
    }
    else
    {
        tbl_id = DsFibHost0Ipv6McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_hashType0_f, ds_fib_host0_ipv6_mcast_hash_key, 2);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_hashType1_f, ds_fib_host0_ipv6_mcast_hash_key, 2);
        DRV_SET_FIELD_A(tbl_id, DsFibHost0Ipv6McastHashKey_ipDa_f, ds_fib_host0_ipv6_mcast_hash_key, ipv6_addr);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_pointer_f, ds_fib_host0_ipv6_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid0_f, ds_fib_host0_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_valid1_f, ds_fib_host0_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost0Ipv6McastHashKey_vrfId_f, ds_fib_host0_ipv6_mcast_hash_key, p_group_node->address.ipv6.vrfid);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_asic_ipv4_lookup(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 value = 0;
    uint32 cmd;
    uint32 hashtype = 0;
    uint32 ipv4type = 0;
    uint32 tbl_id = 0;
    uint32 ad_index = 0;
    uint32 default_index = 0;
    drv_cpu_acc_in_t  lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv4HashKey_m ds_fib_host0_ipv4_hash_key;
    DsFibHost1Ipv4McastHashKey_m ds_fib_host1_ipv4_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(&ds_fib_host0_ipv4_hash_key, 0, sizeof(ds_fib_host0_ipv4_hash_key));
    sal_memset(&ds_fib_host1_ipv4_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv4_mcast_hash_key));
    SYS_IPMC_DBG_FUNC();
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
        ((p_gg_ipmc_master[lchip]->ipv4_l2mc_default_index << 8)+p_group_node->address.ipv4.vrfid) : p_gg_ipmc_master[lchip]->ipv4_ipmc_default_index+p_group_node->address.ipv4.vrfid;

    /* (**,g) not found*/
    hashtype = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 1 : 1;
    SetDsFibHost0Ipv4HashKey(V, hashType_f, &ds_fib_host0_ipv4_hash_key, hashtype);
    SetDsFibHost0Ipv4HashKey(V, ipDa_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.group_addr);
    ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
    SetDsFibHost0Ipv4HashKey(V, ipv4Type_f, &ds_fib_host0_ipv4_hash_key, ipv4type);
    SetDsFibHost0Ipv4HashKey(V, valid_f, &ds_fib_host0_ipv4_hash_key, 1);
    SetDsFibHost0Ipv4HashKey(V, vrfId_f, &ds_fib_host0_ipv4_hash_key, p_group_node->address.ipv4.vrfid);

    key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV4;
    key_acc_in.fib.data = (void *)&ds_fib_host0_ipv4_hash_key;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_FIB0, &key_acc_in, &key_acc_out);
    SYS_IPMC_DBG_INFO("asic lookup FibHost0: key_index:%d\n", key_acc_out.fib.key_index);
    if (key_acc_out.fib.hit == FALSE)
    {
        return CTC_E_IPMC_GROUP_NOT_EXIST;
    }

    /* lookup pointer (may be in cam) */
    p_group_node->g_key_index = key_acc_out.fib.key_index;
    key_acc_in.rw.key_index = key_acc_out.fib.key_index;
    key_acc_in.rw.tbl_id = DsFibHost0Ipv4HashKey_t;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &key_acc_in, &key_acc_out);
    p_group_node->pointer = GetDsFibHost0Ipv4HashKey(V, pointer_f, &key_acc_out.read.data);
    SYS_IPMC_DBG_INFO("asic lookup pointer:%d\n", p_group_node->pointer);

    /* (*,g) not found*/
    if (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        ad_index = GetDsFibHost0Ipv4HashKey(V, dsAdIndex_f, &key_acc_out.read.data);
        if (ad_index == default_index)
        {
            return CTC_E_IPMC_GROUP_NOT_EXIST;
        }
        p_group_node->ad_index = ad_index;
        if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));
            p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_CHECK) : p_group_node->extern_flag;
        }
    }
    /* (s,pointer) not found*/
    else
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv4McastHashKey_t : DsFibHost1Ipv4McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_hashType_f, &ds_fib_host1_ipv4_mcast_hash_key, 0);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipSa_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->address.ipv4.src_addr);
        ipv4type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 2 : 1;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_ipv4Type_f, &ds_fib_host1_ipv4_mcast_hash_key, ipv4type);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_pointer_f, &ds_fib_host1_ipv4_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv4McastHashKey_valid_f, &ds_fib_host1_ipv4_mcast_hash_key, 1);

        /* lookup key index */
        lookup_info.data = (void*)(&ds_fib_host1_ipv4_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.hit == FALSE)
        {
            SYS_IPMC_DBG_INFO("(S,G) not found, lookup_result:%d!!\n", lookup_result.key_index);
            return CTC_E_IPMC_GROUP_NOT_EXIST;
        }
        p_group_node->sg_key_index = lookup_result.key_index;
        SYS_IPMC_DBG_INFO("asic lookup FibHost1: key_index:%d\n", lookup_result.key_index);

        /* lookup ad index */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));
        p_group_node->ad_index = GetDsFibHost1Ipv4McastHashKey(V, dsAdIndex_f, &lookup_result.data);

        if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &value));
            p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_CHECK) : p_group_node->extern_flag;
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_asic_ipv6_lookup(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    uint32 value = 0;
    uint32 cmd;
    uint32 ad_index = 0;
    uint32 _type = 0;
    uint32 tbl_id = 0;
    uint32 default_index = 0;
    ipv6_addr_t ipv6_addr;
    drv_cpu_acc_in_t   lookup_info;
    drv_cpu_acc_out_t lookup_result;
    DsFibHost0Ipv6McastHashKey_m ds_fib_host0_ipv6_mcast_hash_key;
    DsFibHost0MacIpv6McastHashKey_m ds_fib_host0_mac_ipv6_mcast_hash_key;
    DsFibHost1Ipv6McastHashKey_m ds_fib_host1_ipv6_mcast_hash_key;
    drv_fib_acc_in_t  key_acc_in;
    drv_fib_acc_out_t key_acc_out;

    sal_memset(&key_acc_in, 0, sizeof(drv_fib_acc_in_t));
    sal_memset(&key_acc_out, 0, sizeof(drv_fib_acc_out_t));
    sal_memset(&lookup_info, 0, sizeof(lookup_info));
    sal_memset(&lookup_result, 0, sizeof(lookup_result));
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));
    sal_memset(&ds_fib_host0_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host0_mac_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host0_mac_ipv6_mcast_hash_key));
    sal_memset(&ds_fib_host1_ipv6_mcast_hash_key, 0, sizeof(ds_fib_host1_ipv6_mcast_hash_key));
    default_index = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? \
        ((p_gg_ipmc_master[lchip]->ipv6_l2mc_default_index << 8)+p_group_node->address.ipv6.vrfid) : p_gg_ipmc_master[lchip]->ipv6_ipmc_default_index+p_group_node->address.ipv6.vrfid;

    SYS_IPMC_DBG_FUNC();
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);

    /* (**,g) not found*/
    _sys_goldengate_ipmc_construct_ipv6_hash_key(lchip, p_group_node,
                                                &ds_fib_host0_ipv6_mcast_hash_key,
                                                &ds_fib_host0_mac_ipv6_mcast_hash_key);
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        tbl_id = DsFibHost0MacIpv6McastHashKey_t;
        key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_MACIPV6MCAST;
        key_acc_in.fib.data = (void *)&ds_fib_host0_mac_ipv6_mcast_hash_key;
    }
    else
    {
        tbl_id = DsFibHost0Ipv6McastHashKey_t;
        key_acc_in.fib.key_type = FIBHOST0PRIMARYHASHTYPE_IPV6MCAST;
        key_acc_in.fib.data = (void *)&ds_fib_host0_ipv6_mcast_hash_key;
    }
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_LKP_FIB0, &key_acc_in, &key_acc_out);
    SYS_IPMC_DBG_INFO("Lookup FibHost0 key_index:%d\n", key_acc_out.fib.key_index);
    if (key_acc_out.fib.hit == FALSE)
    {
        return CTC_E_IPMC_GROUP_NOT_EXIST;
    }

    /* lookup pointer (may be in cam) */
    p_group_node->g_key_index = key_acc_out.fib.key_index;
    key_acc_in.rw.key_index = key_acc_out.fib.key_index;
    key_acc_in.rw.tbl_id = tbl_id;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &key_acc_in, &key_acc_out);
    if (CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
    {
        p_group_node->pointer = GetDsFibHost0MacIpv6McastHashKey(V, pointer_f, &key_acc_out.read.data);
        ad_index = GetDsFibHost0MacIpv6McastHashKey(V, dsAdIndex_f, &key_acc_out.read.data);
    }
    else
    {
        p_group_node->pointer = GetDsFibHost0Ipv6McastHashKey(V, pointer_f, &key_acc_out.read.data);
        ad_index = GetDsFibHost0Ipv6McastHashKey(V, dsAdIndex_f, &key_acc_out.read.data);
    }
    SYS_IPMC_DBG_INFO("p_group_node->pointer:%d\n", p_group_node->pointer);

    /* (*,g) not found*/
    if (!CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        if (ad_index == default_index)
        {
            return CTC_E_IPMC_GROUP_NOT_EXIST;
        }
        p_group_node->ad_index = ad_index;
        if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &value));
            p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_CHECK) : p_group_node->extern_flag;
        }
    }
    /* (s,pointer) not found*/
    else
    {
        tbl_id = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? DsFibHost1MacIpv6McastHashKey_t : DsFibHost1Ipv6McastHashKey_t;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType0_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_hashType1_f, &ds_fib_host1_ipv6_mcast_hash_key, 3);
        sal_memcpy(&ipv6_addr, &(p_group_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
        DRV_SET_FIELD_A(tbl_id, DsFibHost1Ipv6McastHashKey_ipSa_f, &ds_fib_host1_ipv6_mcast_hash_key, ipv6_addr);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_pointer_f, &ds_fib_host1_ipv6_mcast_hash_key, p_group_node->pointer);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid0_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey_valid1_f, &ds_fib_host1_ipv6_mcast_hash_key, 1);
        _type = CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC) ? 3 : 2;
        DRV_SET_FIELD_V(tbl_id, DsFibHost1Ipv6McastHashKey__type_f, &ds_fib_host1_ipv6_mcast_hash_key, _type);

        /* lookup key index */
        lookup_info.data = (void*)(&ds_fib_host1_ipv6_mcast_hash_key);
        lookup_info.tbl_id = tbl_id;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_LOOKUP_ACC_FIB_HOST1, &lookup_info, &lookup_result));
        if (lookup_result.hit == FALSE)
        {
            SYS_IPMC_DBG_INFO("(S,G) not found, lookup_result:%d!!\n", lookup_result.key_index);
            return CTC_E_IPMC_GROUP_NOT_EXIST;
        }
        p_group_node->sg_key_index = lookup_result.key_index;
        SYS_IPMC_DBG_INFO("Lookup FibHost1 key_index:%d\n", lookup_result.key_index);

        /* lookup ad index */
        lookup_info.tbl_id = tbl_id;
        lookup_info.key_index = lookup_result.key_index;
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, DRV_CPU_READ_ACC_FIB_HOST1_BY_IDX, &lookup_info, &lookup_result));
        p_group_node->ad_index = GetDsFibHost1Ipv6McastHashKey(V, dsAdIndex_f, &lookup_result.data);

        if(!CTC_FLAG_ISSET(p_group_node->extern_flag, CTC_IPMC_FLAG_L2MC))
        {
            cmd = DRV_IOR(DsIpDa_t, DsIpDa_rpfCheckEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_group_node->ad_index, cmd, &value));
            p_group_node->extern_flag = (value == TRUE) ? (p_group_node->extern_flag | CTC_IPMC_FLAG_RPF_CHECK) : p_group_node->extern_flag;
        }

    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_asic_lookup(uint8 lchip, sys_ipmc_group_node_t* p_group_node)
{
    int32 ret = CTC_E_NONE;
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    if (CTC_FLAG_ISSET(p_group_node->flag, SYS_IPMC_FLAG_VERSION))
    {
        ret = (_sys_goldengate_ipmc_asic_ipv6_lookup(lchip, p_group_node));
    }
    else
    {
        ret = (_sys_goldengate_ipmc_asic_ipv4_lookup(lchip, p_group_node));
    }

    if (CTC_E_NONE == ret)
    {
        p_group_node->is_group_exist = 1;
    }

    return ret;
}

STATIC int32
_sys_goldengate_ipmc_db_add(uint8 lchip, sys_ipmc_group_db_node_t* p_node)
{
    uint8  ip_version = 0;

    CTC_PTR_VALID_CHECK(p_node);
    ip_version = (p_node->flag & SYS_IPMC_FLAG_DB_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    ctc_hash_insert(p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_version], p_node);
    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_db_remove(uint8 lchip, sys_ipmc_group_db_node_t* p_node)
{
    uint8  ip_version = 0;

    CTC_PTR_VALID_CHECK(p_node);
    ip_version = (p_node->flag & SYS_IPMC_FLAG_DB_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    ctc_hash_remove(p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_version], p_node);
    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_add(uint8 lchip, sys_ipmc_group_db_node_t* p_node)
{
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_db_add(lchip, p_node));

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;
    sys_ipmc_group_db_node_t group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    sal_memset(&group_node, 0, sizeof(group_node));
    p_base_group_node = &base_group_node;
    /* (*,g) node lookup & delete */
    {
        _sys_goldengate_ipmc_set_group_db_node(lchip, p_node, p_base_group_node);
        UNSET_FLAG(p_base_group_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR);
        if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_VERSION))
        {
            sal_memset(&(p_base_group_node->address.ipv6.src_addr), 0, sizeof(ipv6_addr_t));
        }
        else
        {
            sal_memset(&(p_base_group_node->address.ipv4.src_addr), 0, sizeof(uint32));
        }
        sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
        if (p_base_group_node)
        {
            if (!(CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN)))
            {
                p_node->extern_flag = p_base_group_node->extern_flag;
                p_node->flag = CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE)? (p_node->flag | SYS_IPMC_FLAG_RPF_PROFILE) : p_node->flag;
                if (CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE))
                {
                    p_node->sa_index = p_base_group_node->rpf_index;
                }
            }
            p_base_group_node->refer_count = p_base_group_node->refer_count - 1;
            if (p_base_group_node->refer_count == 0)
            {
                _sys_goldengate_ipmc_db_free_pointer(lchip, p_node->pointer, p_base_group_node);
                _sys_goldengate_ipmc_db_remove(lchip, p_base_group_node);
            }
            else if (!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
            {
                p_base_group_node->nexthop_id = 0;
                p_base_group_node->group_id = 0;
                UNSET_FLAG(p_base_group_node->flag, SYS_IPMC_FLAG_DB_VALID);
            }
            /* p_base_group_node is the (*,g) node */
        }
    }

    if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
    {
        p_base_group_node = &group_node;
        _sys_goldengate_ipmc_set_group_db_node(lchip, p_node, p_base_group_node);
        sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
        if (p_base_group_node)
        {
            p_node->extern_flag = p_base_group_node->extern_flag;
            p_node->flag = CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE)? (p_node->flag | SYS_IPMC_FLAG_RPF_PROFILE) : p_node->flag;
            if (CTC_FLAG_ISSET(p_base_group_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE))
            {
                p_node->sa_index = p_base_group_node->rpf_index;
            }
        }
    }
     /*CTC_ERROR_RETURN(_sys_goldengate_ipmc_get_ipda_info(lchip, p_node));*/
    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&
        !CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) &&
        CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
    {
        CTC_ERROR_RETURN(sys_goldengate_rpf_remove_profile(lchip, p_node->sa_index));
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Remove rpf profile index:%d\n", p_node->sa_index);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_sg_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));
    p_base_group_node = &base_group_node;
    /* (s,g) node lookup & delete */
    _sys_goldengate_ipmc_set_group_db_node(lchip, p_node, p_base_group_node);
    sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
    if (p_base_group_node)
    {
        p_node->nexthop_id = p_base_group_node->nexthop_id;
        _sys_goldengate_ipmc_db_remove(lchip, p_base_group_node);
        mem_free(p_base_group_node);
    }
    else
    {
         return CTC_E_IPMC_GROUP_NOT_EXIST;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_ipmc_db_lookup(uint8 lchip, sys_ipmc_group_db_node_t** pp_node)
{
    uint8  ip_version = 0;
    sys_ipmc_group_db_node_t* p_ipmc_group_db_node = NULL;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pp_node);
    ip_version = ((*pp_node)->flag & SYS_IPMC_FLAG_DB_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    p_ipmc_group_db_node = ctc_hash_lookup(p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_version], *pp_node);
    *pp_node = p_ipmc_group_db_node;

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipmc_db_set_group_node(uint8 lchip, sys_ipmc_group_db_node_t* p_group_db_node, sys_ipmc_group_node_t* p_group_node)
{
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group_node);
    CTC_PTR_VALID_CHECK(p_group_db_node);

    p_group_node->group_id = p_group_db_node->group_id;
    p_group_node->nexthop_id = p_group_db_node->nexthop_id;

    p_group_node->flag = (CTC_FLAG_ISSET(p_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION)) \
                          ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : p_group_node->flag;
    p_group_node->extern_flag = (CTC_FLAG_ISSET(p_group_db_node->flag, SYS_IPMC_FLAG_DB_L2MC)) \
                          ? (p_group_node->extern_flag | CTC_IPMC_FLAG_L2MC) : p_group_node->extern_flag;
    p_group_node->flag = (CTC_FLAG_ISSET(p_group_db_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR)) \
                          ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : p_group_node->flag;
    p_group_node->extern_flag = (CTC_FLAG_ISSET(p_group_db_node->flag, SYS_IPMC_FLAG_DB_DROP)) \
                          ? (p_group_node->extern_flag | CTC_IPMC_FLAG_DROP) : p_group_node->extern_flag;
    if (CTC_FLAG_ISSET(p_group_db_node->flag, SYS_IPMC_FLAG_DB_VERSION))
    {
         sal_memcpy(&(p_group_node->address.ipv6.group_addr), &(p_group_db_node->address.ipv6.group_addr), sizeof(ipv6_addr_t));
         sal_memcpy(&(p_group_node->address.ipv6.src_addr), &(p_group_db_node->address.ipv6.src_addr), sizeof(ipv6_addr_t));
         p_group_node->address.ipv6.vrfid = p_group_db_node->address.ipv6.vrfid;
    }
    else
    {
         sal_memcpy(&(p_group_node->address.ipv4.group_addr), &(p_group_db_node->address.ipv4.group_addr), sizeof(uint32));
         sal_memcpy(&(p_group_node->address.ipv4.src_addr), &(p_group_db_node->address.ipv4.src_addr), sizeof(uint32));
         p_group_node->address.ipv4.vrfid = p_group_db_node->address.ipv4.vrfid;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_show_ipmc_info(sys_ipmc_group_db_node_t* p_node, void* data)
{
    char buf[CTC_IPV6_ADDR_STR_LEN];
#define SYS_IPMC_MASK_LEN  5
    char buf2[SYS_IPMC_MASK_LEN];
    uint32 tempip = 0;
    char str[2][8] = {{"(*,G)"}, {"(S,G)"}};
    uint8 value = 0;
    uint8 do_flag = 0;
    sys_ipmc_group_node_t group_node;

#define IPMC_G  0
#define IPMC_SG 1
    uint8   mode = IPMC_G;

    uint8  lchip = 0;

    lchip = ((sys_ipmc_traverse_t*)data)->lchip;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));

    if(CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_DROP))
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s", " - ");
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6u", p_node->group_id);
    }
    
    if (!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_VERSION))
    {
        /* print IPv4 source IP address */
        value = CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR) ? 32 : 0;
        sal_sprintf(buf2, "/%-1u", value);
        tempip = sal_ntohl(p_node->address.ipv4.src_addr);
        sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);

        if(0 != tempip)
        {
            mode = IPMC_SG;
        }

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-21s", buf);

        /* print IPv4 group IP address */
        value = 32;
        sal_sprintf(buf2, "/%-1u", value);
        tempip = sal_ntohl(p_node->address.ipv4.group_addr);
        sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-21s", buf);
        /* print vrfId */
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6u", p_node->address.ipv4.vrfid);
    }
    else
    {
        uint32 ipv6_address[4] = {0, 0, 0, 0};

        /* print IPv6 group IP address */
        value = 128;
        sal_sprintf(buf2, "/%-1u", value);

        ipv6_address[0] = sal_htonl(p_node->address.ipv6.group_addr[3]);
        ipv6_address[1] = sal_htonl(p_node->address.ipv6.group_addr[2]);
        ipv6_address[2] = sal_htonl(p_node->address.ipv6.group_addr[1]);
        ipv6_address[3] = sal_htonl(p_node->address.ipv6.group_addr[0]);

        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-46s", buf);

        /* print IPv6 source IP address */
        value = CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_SRC_ADDR) ? 128 : 0;
        sal_sprintf(buf2, "/%-1u", value);

        ipv6_address[0] = sal_htonl(p_node->address.ipv6.src_addr[3]);
        ipv6_address[1] = sal_htonl(p_node->address.ipv6.src_addr[2]);
        ipv6_address[2] = sal_htonl(p_node->address.ipv6.src_addr[1]);
        ipv6_address[3] = sal_htonl(p_node->address.ipv6.src_addr[0]);

        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);

        if((0 != ipv6_address[0]) || (0 != ipv6_address[1])
            || (0 != ipv6_address[2]) || (0 != ipv6_address[3]))
        {
            mode = IPMC_SG;
        }

        /* print vrfId */
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6u", p_node->address.ipv6.vrfid);
    }

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_db_set_group_node(lchip, p_node, &group_node));
    _sys_goldengate_ipmc_asic_lookup(lchip, &group_node);
    group_node.extern_flag = p_node->extern_flag;
    group_node.flag = CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE)? (group_node.flag | SYS_IPMC_FLAG_RPF_PROFILE) : group_node.flag;
    if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_RPF_PROFILE))
    {
        group_node.sa_index = p_node->rpf_index;
    }

    if (!CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_L2MC))
    {
         /*CTC_ERROR_RETURN(_sys_goldengate_ipmc_get_ipda_info(lchip, &group_node));*/
    }

    buf2[0] = '\0';
    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_DROP))
    {
        sal_strncat(buf2, "D", 1);
        do_flag = 1;
    }
    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
    {
        sal_strncat(buf2, "R", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_TTL_CHECK))
    {
        sal_strncat(buf2, "T", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_COPY_TOCPU))
    {
        sal_strncat(buf2, "C", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU))
    {
        sal_strncat(buf2, "F", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_STATS))
    {
        sal_strncat(buf2, "S", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_PTP_ENTRY))
    {
        sal_strncat(buf2, "P", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_REDIRECT_TOCPU))
    {
        sal_strncat(buf2, "U", 1);
        do_flag = 1;
    }

    if(CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_L2MC))
    {
        sal_strncat(buf2, "L", 1);
        do_flag = 1;
    }

    if (!do_flag)
    {
        sal_strncat(buf2, "-", 1);
    }

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8u%-7s", p_node->nexthop_id, buf2);


    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s\n", str[mode]);


    if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_VERSION))
    {
        if(IPMC_SG == mode)
        {
            SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s", " ");
            SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-46s\n", buf);
        }
    }
    return CTC_E_NONE;

#undef IPMC_G
#undef IPMC_SG
}

int32
sys_goldengate_ipmc_db_show_group_info(uint8 lchip, uint8 ip_version , ctc_ipmc_group_info_t* p_node)
{
    sys_ipmc_group_db_node_t *p_base_group_node = NULL;
    sys_ipmc_group_db_node_t base_group_node;
    sys_ipmc_group_node_t    group_node;
    sys_ipmc_traverse_t      travs = {0};

    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK();
    CTC_IP_VER_CHECK(ip_version);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "flag:      R-RPF check       T-TTL check    L-L2 multicast          D-drop\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "           C-copied to CPU   F-sent to CPU and fallback bridged\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "           S-stats enable    P-PTP entry    U-redirected to CPU\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------------------------------------------------------\n\n");

    travs.lchip = lchip;

    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s%-21s%-21s%-6s%-8s%-7s%s\n", "GID", "IPSA",
                       "Group-IP", "VRF", "NHID", "Flag", "Mode");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                        "------------------------------------------------------------------------------\n");
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s%-46s%-6s%-8s%-7s%s\n", "GID",
                        "Group-IP/IPSA", "VRF", "NHID", "Flag", "Mode");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                        "------------------------------------------------------------------------------\n");
    }

    if(p_node)
    {
        DsRpf_m ds_rpf;
        uint32 cmd = 0;
        sal_memset(&group_node,0,sizeof(group_node));
        sal_memset(&base_group_node,0,sizeof(base_group_node));
        CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_node, &group_node));

        CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_db_node(lchip, &group_node, &base_group_node));
        p_base_group_node = &base_group_node;
        sys_goldengate_ipmc_db_lookup(lchip,&p_base_group_node);
        sys_goldengate_show_ipmc_info(p_base_group_node, &travs);
        _sys_goldengate_ipmc_asic_lookup(lchip, &group_node);

        if (!CTC_FLAG_ISSET(group_node.extern_flag, CTC_IPMC_FLAG_L2MC))
        {
             /*CTC_ERROR_RETURN(_sys_goldengate_ipmc_get_ipda_info(lchip, &group_node));*/
        }

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Detail ipmc group info\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --g_key_index                :%d\n", group_node.g_key_index);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --sg_key_index               :%d\n", group_node.sg_key_index);

        if (CTC_FLAG_ISSET(group_node.flag, SYS_IPMC_FLAG_RPF_PROFILE))
        {
            sal_memset(&ds_rpf, 0, sizeof(ds_rpf));
            cmd = DRV_IOR(DsRpf_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_IPMC_UNLOCK(DRV_IOCTL(lchip, group_node.sa_index, cmd, &ds_rpf));
        }

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\nDetail ipmc table offset\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-----------------------------------\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-30s:%d (*,G)\n", (CTC_IP_VER_4 == ip_version) ? "DsFibHost0Ipv4HashKey" : "DsFibHost0Ipv6McastHashKey", group_node.g_key_index);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " --%-30s:%d (S,G)\n", (CTC_IP_VER_4 == ip_version) ? "DsFibHost1Ipv4McastHashKey" : "DsFibHost1Ipv6McastHashKey", group_node.sg_key_index);

    }
    else
    {
        ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_version], (hash_traversal_fn)sys_goldengate_show_ipmc_info, &travs);
    }

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_db_show_l2mc_count(sys_ipmc_group_db_node_t* p_node, uint16* count)
{
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_L2MC))
    {
        *count = (*count) + 1;
    }

    return ret;
}

STATIC int32
_sys_goldengate_ipmc_db_show_ipmc_count(sys_ipmc_group_db_node_t* p_node, uint16* count)
{
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_L2MC))
    {
        *count = (*count) + 1;
    }

    return ret;
}

int32
sys_goldengate_ipmc_db_show_count(uint8 lchip)
{
    uint32 i;
    uint16 count = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK();

    for (i = 0; i < MAX_CTC_IP_VER; i++)
    {
        ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[i], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_l2mc_count, &count);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MCv%c total %d routes\n", (i == CTC_IP_VER_4) ? '4' : '6', count);
        count = 0;
        ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[i], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_ipmc_count, &count);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMCv%c total %d routes\n", (i == CTC_IP_VER_4) ? '4' : '6', count);
        count = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipmc_db_show_member_info(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{
    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;
    sys_ipmc_group_db_node_t* p_base_group_node;
    sys_ipmc_group_db_node_t base_group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));
    sal_memset(&base_group_node, 0, sizeof(sys_ipmc_group_db_node_t));

    CTC_ERROR_RETURN(_sys_goldengate_ipmc_check(lchip, p_group, 0));

    p_group_node = &group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_node(lchip, p_group, p_group_node));
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_asic_lookup(lchip, p_group_node));
    p_base_group_node = &base_group_node;
    CTC_ERROR_RETURN(_sys_goldengate_ipmc_set_group_db_node(lchip, p_group_node, p_base_group_node));
    sys_goldengate_ipmc_db_lookup(lchip, &p_base_group_node);
    p_group_node->nexthop_id = p_base_group_node->nexthop_id;
    sys_goldengate_nh_dump(lchip, p_group_node->nexthop_id, FALSE);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipmc_db_show_count(sys_ipmc_group_db_node_t* p_node, sys_mc_count_t* count)
{
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_L2MC))
    {
        if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_VALID))
        {
            count->ipmc_g_count = count->ipmc_g_count + 1;
            count->ipmc_sg_count = count->ipmc_sg_count + p_node->refer_count - 1;
        }
        else
        {
            count->ipmc_sg_count = count->ipmc_sg_count + p_node->refer_count;
        }
    }
    else
    {
        if (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_DB_VALID))
        {
            count->l2mc_g_count = count->l2mc_g_count + 1;
            count->l2mc_sg_count = count->l2mc_sg_count + p_node->refer_count - 1;
        }
        else
        {
            count->l2mc_sg_count = count->l2mc_sg_count + p_node->refer_count;
        }
    }

    return ret;
}

int32
sys_goldengate_ipmc_show_status(uint8 lchip)
{
    int32 ret = 0;
    int32 default_num = 0;
    sys_mc_count_t ip_count;

    sal_memset(&ip_count, 0, sizeof(sys_mc_count_t));

    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK();

    /*ipv4*/
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n===========================\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:\n", "IPV4 size");
    default_num = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_4);
    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_count, &ip_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.ipmc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.ipmc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,*) entry count", default_num);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.l2mc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.l2mc_g_count);

    /*ipv6*/
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n===========================\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:\n", "IPV6 size");
    sal_memset(&ip_count, 0, sizeof(sys_mc_count_t));
    default_num = sys_goldengate_l3if_get_max_vrfid(lchip, CTC_IP_VER_6);
    ctc_hash_traverse(p_gg_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_goldengate_ipmc_db_show_count, &ip_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.ipmc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.ipmc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,*) entry count", default_num);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.l2mc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.l2mc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n===========================\n");
    return ret;

}

int32
_sys_goldengate_ipmc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gg_ipmc_db_master[lchip]->ipmc_hash[ip_ver], fn, data);
}


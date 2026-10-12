/**
 @file sys_greatbelt_ipmc.c

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
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_ipmc.h"
#include "sys_greatbelt_ipmc_db.h"
#include "sys_greatbelt_rpf_spool.h"

#include "greatbelt/include/drv_io.h"

#define IPMC_IPV4_HASH_SIZE     4096
#define IPMC_IPV6_HASH_SIZE     4096

#define SYS_IPMC_DB_INIT_CHECK(lchip) \
    do {    \
        if (p_gb_ipmc_db_master[lchip] == NULL){          \
            return CTC_E_NOT_INIT; }                \
    } while (0)

sys_ipmc_db_master_t* p_gb_ipmc_db_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

extern sys_ipmc_master_t* p_gb_ipmc_master[CTC_MAX_LOCAL_CHIP_NUM];

struct sys_mc_count_s
{
    uint16 ipmc_sg_count;
    uint16 ipmc_g_count;
    uint16 ipmc_def_count;
    uint16 l2mc_sg_count;
    uint16 l2mc_g_count;
};
typedef struct sys_mc_count_s sys_mc_count_t;

enum sys_mc_offset_move_mode_e
{
    SYS_MC_MOVE_EASY_ZERO_ONE,                 /* have no free offset, need one offset, easy remove */
    SYS_MC_MOVE_COMPLX_EVEN_ZERO_ONE,          /* have no free offset, need one offset, complex remove */
    SYS_MC_MOVE_EVEN_ZERO_DOUBLE,              /* have no free offset, need double offset */
    SYS_MC_MOVE_ODD_ONE_DOUBLE,                /* have one odd free offset, need double offset */
    SYS_MC_MOVE_EVEN_ONE_DOUBLE,               /* have one even free offset, need double offset */
    SYS_MC_MOVE_MAX
};
typedef enum sys_mc_offset_move_mode_e sys_mc_offset_move_mode_t;

enum sys_mc_offset_move_dir_e
{
    SYS_MC_MOVE_DIR_PREVISE,
    SYS_MC_MOVE_DIR_REVERSE,
    SYS_MC_MOVE_DIR_MAX
};
typedef enum sys_mc_offset_move_dir_e sys_mc_offset_move_dir_t;

STATIC uint32
_sys_greatbelt_ipmc_ipv4_hash_make(sys_ipmc_group_node_t* p_node_to_lkp)
{
    uint32 a, b, c;

    CTC_PTR_VALID_CHECK(p_node_to_lkp);

    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = 0;

    a += p_node_to_lkp->address.ipv4.group_addr;
    b += p_node_to_lkp->address.ipv4.vrfid;
    b += p_node_to_lkp->address.ipv4.src_addr << 16;
    c += ((p_node_to_lkp->extern_flag & CTC_IPMC_FLAG_L2MC) ? 1 : 0);

    c += (a & 0xfff) ^ ((a >> 12) & 0xfff) ^ ((a >> 24) & 0xff) ^
        (b & 0xfff) ^ ((b >> 12) & 0xfff);

    return c % IPMC_IPV4_HASH_SIZE;
}

STATIC uint32
_sys_greatbelt_ipmc_ipv6_hash_make(sys_ipmc_group_node_t* p_node_to_lkp)
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
    b += (p_node_to_lkp->extern_flag & CTC_IPMC_FLAG_L2MC) << 16;
    c += p_node_to_lkp->address.ipv6.src_addr[3];

    MIX(a, b, c);

    a += p_node_to_lkp->address.ipv6.src_addr[0];
    b += p_node_to_lkp->address.ipv6.src_addr[1];
    c += p_node_to_lkp->address.ipv6.src_addr[2];
    MIX(a, b, c);

    return c % IPMC_IPV6_HASH_SIZE;

}

int32
_sys_greatbelt_ipmc_ipv4_hash_compare(sys_ipmc_group_node_t* p_node_in_bucket, sys_ipmc_group_node_t* p_node_to_lkp)
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

    if (p_node_in_bucket->address.ipv4.src_addr != p_node_to_lkp->address.ipv4.src_addr)
    {
        return FALSE;
    }

    if (p_node_in_bucket->address.ipv4.group_addr != p_node_to_lkp->address.ipv4.group_addr)
    {
        return FALSE;
    }

    if ((p_node_in_bucket->extern_flag & CTC_IPMC_FLAG_L2MC) != (p_node_to_lkp->extern_flag & CTC_IPMC_FLAG_L2MC))
    {
        return FALSE;
    }

    /* the wanted ipmc group can't be found in DB */
    return TRUE;
}

int32
_sys_greatbelt_ipmc_ipv6_hash_compare(sys_ipmc_group_node_t* p_node_in_bucket, sys_ipmc_group_node_t* p_node_to_lkp)
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
        if (p_node_in_bucket->address.ipv6.src_addr[i] != p_node_to_lkp->address.ipv6.src_addr[i])
        {
            return FALSE;
        }
    }

    for (i = 0; i < 4; i++)
    {
        if (p_node_in_bucket->address.ipv6.group_addr[i] != p_node_to_lkp->address.ipv6.group_addr[i])
        {
            return FALSE;
        }
    }

    if ((p_node_in_bucket->extern_flag & CTC_IPMC_FLAG_L2MC) != (p_node_to_lkp->extern_flag & CTC_IPMC_FLAG_L2MC))
    {
        return FALSE;
    }

    /* the wanted ipmc group has already exsited in DB */
    return TRUE;
}

int32
_sys_greatbelt_ipmc_syn_key(uint8 lchip, uint32 new_offset, uint32 old_offset)
{
    sys_ipmc_group_node_t* p_group_node;
    ds_ip_da_t dsipda;
    uint32 cmdr, cmdw;

    p_group_node = SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, old_offset);

    if (NULL == p_group_node)
    {
        return CTC_E_NONE;
    }
    if (new_offset == old_offset)
    {
        return CTC_E_ENTRY_EXIST;
    }

    cmdr = DRV_IOR(p_gb_ipmc_master[lchip]->da_tcam_table_id[CTC_IP_VER_4], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gb_ipmc_master[lchip]->da_tcam_table_id[CTC_IP_VER_4], DRV_ENTRY_FLAG);

    DRV_IOCTL(lchip, old_offset, cmdr, &dsipda);
    DRV_IOCTL(lchip, new_offset, cmdw, &dsipda);

    /* add key to new offset */
    p_group_node->ad_index = new_offset;
    _sys_greatbelt_ipmc_write_key(lchip, p_group_node);

    /* remove key from old offset */
    p_group_node->ad_index = old_offset;
    _sys_greatbelt_ipmc_remove_key(lchip, p_group_node);

    p_group_node->ad_index = new_offset;
    SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, new_offset) =
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, old_offset);
    SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, old_offset) = NULL;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_ipmc_ipv4_db_init(uint8 lchip)
{
    uint32 table_size = 0;
    uint8 default_entry_num = 0;
    uint32 table_size_l2mc = 0;
    sys_greatbelt_opf_t opf;

    /*ipv4*/
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4], &table_size));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_l2_table_id[CTC_IP_VER_4], &table_size_l2mc));
    p_gb_ipmc_master[lchip]->is_ipmcv4_key160 = (table_size == table_size_l2mc);
    default_entry_num = 1 + p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_4];
    if (p_gb_ipmc_master[lchip]->is_ipmcv4_key160)
    {
        /*use as 80 bit key size*/
        default_entry_num = default_entry_num * 2;
        table_size  = table_size * 2;
    }

    p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_4] = (sys_ipmc_group_node_t**)mem_malloc(MEM_IPMC_MODULE, (table_size - default_entry_num) * sizeof(sys_ipmc_group_node_t*));
    if (p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_4] == NULL)
    {
        mem_free(p_gb_ipmc_db_master[lchip]);
        mem_free(p_gb_ipmc_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_4], 0, sizeof(sys_ipmc_group_node_t*) * (table_size - default_entry_num));
    p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_4] = table_size - default_entry_num; /* per vrf default adindex base, last one is global default */
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_IPV4_MC_BLOCK, 1));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_IPV4_MC_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, table_size - default_entry_num));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipmc_ipv6_db_init(uint8 lchip)
{
    uint32 table_size;
    sys_greatbelt_opf_t opf;
    uint8 default_entry_num = 0;

    /*ipv6*/
    table_size = 0;
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6], &table_size));
    default_entry_num = 1 + p_gb_ipmc_master[lchip]->max_vrf_num[CTC_IP_VER_6];
    p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_6] = table_size - default_entry_num;/* per vrf default adindex base, last one is global default */
    CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_IPV6_MC_BLOCK, 1));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = OPF_IPV6_MC_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, 0, table_size - default_entry_num));
    p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_6] = (sys_ipmc_group_node_t**)mem_malloc(MEM_IPMC_MODULE, (table_size - default_entry_num) * sizeof(sys_ipmc_group_node_t*));
    if (p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_6] == NULL)
    {
        mem_free(p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_4]);
        mem_free(p_gb_ipmc_db_master[lchip]);
        mem_free(p_gb_ipmc_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_6], 0, sizeof(sys_ipmc_group_node_t*) * (table_size - default_entry_num));
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipmc_db_init(uint8 lchip)/*init sort db for TCAM*/
{
    sys_sort_block_init_info_t init_info;
    sys_greatbelt_opf_t opf;
    uint32 table_size = 0;
    uint32 ipv6_multiple = 2;  /* unit 160*/
    uint32 ipv4_size = 0;
    uint32 ipv6_size = 0;

    sal_memset(&init_info, 0, sizeof(sys_sort_block_init_info_t));
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    init_info.user_data_of = NULL;
    init_info.dir = SYS_SORT_BLOCK_DIR_DOWN;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsIpv4McastRouteKey_t, &table_size));
    if (table_size <= 0)
    {
        return CTC_E_NONE;
    }
    ipv4_size = sys_greatbelt_ftm_get_ipmc_v4_number(lchip);
    ipv6_size = table_size - ipv4_size;

    if (0 == ipv4_size)
    {
        ipv4_size = 32;
        ipv6_size = table_size - ipv4_size;
    }
    else if (0 == ipv6_size)
    {
        ipv6_size = 32;
        ipv4_size = table_size - ipv6_size;
    }
    p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_6] = table_size - ipv6_multiple;
    p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_4] = table_size - ipv6_multiple*2;
    p_gb_ipmc_master[lchip]->is_ipmcv4_key160 = 1;

    sys_greatbelt_opf_init(lchip, OPF_IPV4_MC_BLOCK, SYS_IPMC_SORT_MAX);
    opf.pool_type = OPF_IPV4_MC_BLOCK;
    init_info.opf = &opf;
    init_info.max_offset_num = table_size;

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V4_S_G];
    init_info.boundary_l = 0;
    init_info.boundary_r = (ipv4_size / 2) - 1;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 1;
    opf.pool_index = SYS_IPMC_SORT_V4_S_G;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V4_G];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + (ipv4_size / 2) - 3;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 1;
    opf.pool_index = SYS_IPMC_SORT_V4_G;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V4_DFT];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + 1;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 1;
    opf.pool_index = SYS_IPMC_SORT_V4_DFT;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V6_S_G];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = init_info.boundary_l + (ipv6_size / 2) - 1;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 2;
    opf.pool_index = SYS_IPMC_SORT_V6_S_G;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V6_G];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_4] - 3;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 2;
    opf.pool_index = SYS_IPMC_SORT_V6_G;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    init_info.block = &p_gb_ipmc_db_master[lchip]->blocks[SYS_IPMC_SORT_V6_DFT];
    init_info.boundary_l = init_info.boundary_r + 1;
    init_info.boundary_r = p_gb_ipmc_master[lchip]->default_entry_adindex[CTC_IP_VER_4] - 1;
    init_info.is_block_can_shrink = TRUE;
    init_info.multiple = 2;
    opf.pool_index = SYS_IPMC_SORT_V6_DFT;
    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_block(lchip, &init_info));

    CTC_ERROR_RETURN(sys_greatbelt_sort_key_init_offset_array(lchip,
                             &p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, table_size));

    p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.block = p_gb_ipmc_db_master[lchip]->blocks;
    p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.max_block_num = SYS_IPMC_SORT_MAX;
    p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.sort_key_syn_key = _sys_greatbelt_ipmc_syn_key;
    p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.type = OPF_IPV4_MC_BLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_init(uint8 lchip)
{
    p_gb_ipmc_db_master[lchip] = mem_malloc(MEM_IPMC_MODULE, sizeof(sys_ipmc_db_master_t));
    if (NULL == p_gb_ipmc_db_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4] = ctc_hash_create(1, IPMC_IPV4_HASH_SIZE,
                                                                (hash_key_fn)_sys_greatbelt_ipmc_ipv4_hash_make,
                                                                (hash_cmp_fn)_sys_greatbelt_ipmc_ipv4_hash_compare);

    if (!p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4])
    {
        return CTC_E_NO_MEMORY;

    }

    p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6] = ctc_hash_create(1, IPMC_IPV6_HASH_SIZE,
                                                                (hash_key_fn)_sys_greatbelt_ipmc_ipv6_hash_make,
                                                                (hash_cmp_fn)_sys_greatbelt_ipmc_ipv6_hash_compare);

    if (!p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6])
    {
        return CTC_E_NO_MEMORY;

    }

    if (p_gb_ipmc_master[lchip]->is_sort_mode
        &&(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP))
        &&(CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP)))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_init(lchip));
    }
    else
    {
        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_ipv4_db_init(lchip));
        }

        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_ipv6_db_init(lchip));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_deinit(uint8 lchip)
{
    if (NULL == p_gb_ipmc_db_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (p_gb_ipmc_master[lchip]->is_sort_mode)
    {
        sys_greatbelt_opf_deinit(lchip, OPF_IPV4_MC_BLOCK);
        mem_free(*p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array);
        mem_free(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array);
    }
    else
    {

        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_4], SYS_IPMC_TCAM_LOOKUP))
        {
            sys_greatbelt_opf_deinit(lchip, OPF_IPV4_MC_BLOCK);
            mem_free(p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_4]);
        }
        if (CTC_FLAG_ISSET(p_gb_ipmc_master[lchip]->lookup_mode[CTC_IP_VER_6], SYS_IPMC_TCAM_LOOKUP))
        {
            sys_greatbelt_opf_deinit(lchip, OPF_IPV6_MC_BLOCK);
            mem_free(p_gb_ipmc_master[lchip]->hash_node[CTC_IP_VER_6]);
        }
    }

    /*free ipmc ipv4*/
    ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_greatbelt_ipmc_db_free_node_data, NULL);
    ctc_hash_free(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4]);

    /*free ipmc ipv6*/
    ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_greatbelt_ipmc_db_free_node_data, NULL);
    ctc_hash_free(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6]);

    mem_free(p_gb_ipmc_db_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_add(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    uint8  ip_version = 0;

    CTC_PTR_VALID_CHECK(p_node);
    ip_version = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    ctc_hash_insert(p_gb_ipmc_db_master[lchip]->ipmc_hash[ip_version], p_node);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    uint8  ip_version = 0;

    CTC_PTR_VALID_CHECK(p_node);
    ip_version = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;
    ctc_hash_remove(p_gb_ipmc_db_master[lchip]->ipmc_hash[ip_version], p_node);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_update_key(uint8 lchip, uint8 ip_version, uint32 old_offset, uint32 new_offset)
{
    int32 ret = 0;
    uint32 cmdr, cmdw;
    ds_ip_da_t dsipda;
    sys_ipmc_group_node_t* p_node = NULL;
    uint8 entry_num = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    entry_num = (p_gb_ipmc_master[lchip]->is_ipmcv4_key160 && (ip_version == CTC_IP_VER_4)) ? 2 : 1;

    /* add key to new offset */
    p_node = p_gb_ipmc_master[lchip]->hash_node[ip_version][old_offset];
    if (p_node == NULL)
    {
        return ret;
    }

    p_node->ad_index = new_offset;
    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_write_key(lchip, p_node));
    p_node->ad_index = old_offset;
    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_remove_key(lchip, p_node));
    p_node->ad_index = new_offset;
    p_gb_ipmc_master[lchip]->hash_node[ip_version][new_offset] = p_node;
    if (!(p_node->flag & SYS_IPMC_FLAG_VERSION) &&       \
        (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160))
    {
        p_gb_ipmc_master[lchip]->hash_node[ip_version][new_offset + 1] = p_node;
    }

    p_gb_ipmc_master[lchip]->hash_node[ip_version][old_offset] = NULL;
    if (!(p_node->flag & SYS_IPMC_FLAG_VERSION) &&       \
        (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160))
    {
        p_gb_ipmc_master[lchip]->hash_node[ip_version][old_offset + 1] = NULL;
    }

    /* add ad to new offset */
    cmdr = DRV_IOR(p_gb_ipmc_master[lchip]->da_tcam_table_id[ip_version], DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(p_gb_ipmc_master[lchip]->da_tcam_table_id[ip_version], DRV_ENTRY_FLAG);
    old_offset = old_offset/entry_num;
    new_offset = new_offset/entry_num;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, old_offset, cmdr, &dsipda));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, new_offset, cmdw, &dsipda));

    return ret;
}

STATIC int32
_sys_greatbelt_ipmc_db_offset_remove_double(uint8 lchip, sys_greatbelt_opf_t opf, sys_mc_offset_move_dir_t move_dir, bool no_edge)
{
    int32 ret;
    int32 ret1;
    uint32 new_offset = 0;
    uint32 new_offset_hit1 = 0;
    uint32 new_offset_hit2 = 0;
    uint32 remove_offset = 0;
    uint32 free_offset = 0;
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    uint8 ip_version = CTC_IP_VER_4;

    SYS_IPMC_INIT_CHECK(lchip);

    ret = CTC_E_NONE;
    ret1 = CTC_E_NONE;

    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));

    opf.multiple = 2;
    opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
    ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, 2, &new_offset);
    if (ret == CTC_E_NO_RESOURCE)
    {
        opf.multiple = 1;
        opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &new_offset_hit1));
        ret1 = sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &new_offset_hit2);
        if (ret1 < 0)
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
            return ret1;
        }

        if (((move_dir == SYS_MC_MOVE_DIR_PREVISE) && (new_offset_hit2 > max_left_offset)) \
            || ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (new_offset_hit2 < (min_right_offset - 1))))
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit2);
            return CTC_E_NO_RESOURCE;
        }
        else if ((((move_dir == SYS_MC_MOVE_DIR_PREVISE) && (new_offset_hit2 == max_left_offset)) \
                  || ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (new_offset_hit2 == (min_right_offset - 1)))) \
                 && (no_edge == TRUE))
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit2);
            return CTC_E_NO_RESOURCE;
        }
        else if ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (new_offset_hit2 == (min_right_offset - 1)) \
                 && (new_offset_hit1 == (p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] - 1)) \
                 && ((new_offset_hit2 % 2) == 1) && (no_edge == FALSE))
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
            sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit2);
            return CTC_E_NO_RESOURCE;
        }

        free_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? new_offset_hit1 : new_offset_hit2;
        remove_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? new_offset_hit2 : new_offset_hit1;

        if ((free_offset % 2) == 0)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, free_offset + 1, remove_offset));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, free_offset + 1));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, free_offset - 1, remove_offset));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, free_offset - 1));
        }

        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, free_offset));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 2, new_offset));
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipmc_db_offset_remove_serious(uint8 lchip, sys_greatbelt_opf_t opf, sys_mc_offset_move_dir_t move_dir)
{
    int32 ret = 0;
    uint32 new_offset = 0;
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    uint32 bound_offset = 0;
    uint8 ip_version = CTC_IP_VER_4;

    SYS_IPMC_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_remove_double(lchip, opf, move_dir, TRUE));
    opf.multiple = 2;
    opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
    ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, 2, &new_offset);
    if (ret < 0)
    {
        return CTC_E_IPMC_OFFSET_ALLOC_ERROR;
    }

    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));
    bound_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - 2) : min_right_offset;
    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset, new_offset));
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 2, bound_offset));
    return ret;

}

STATIC int32
_sys_greatbelt_ipmc_db_offset_move(uint8 lchip, sys_greatbelt_opf_t opf, sys_mc_offset_move_mode_t mode, uint8 entry_num, sys_mc_offset_move_dir_t move_dir)
{
    int32 ret;
    int32 ret1;
    uint32 new_offset;
    uint32 new_offset_hit1;
    uint32 new_offset_hit2;
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    uint32 bound_offset = 0;
    uint32 bound_offset1 = 0;
    uint8 ip_version = CTC_IP_VER_4;
    sys_ipmc_group_node_t* p_node = NULL;

    SYS_IPMC_INIT_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));

    ret = CTC_E_NONE;
    ret1 = CTC_E_NONE;

    switch (mode)
    {
    case SYS_MC_MOVE_EASY_ZERO_ONE:
        ip_version = (entry_num == 2) ? CTC_IP_VER_4 : CTC_IP_VER_6;
        opf.multiple = entry_num;
        opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, &new_offset));
        bound_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - entry_num) : min_right_offset;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset, new_offset));
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, bound_offset));
        break;

    case SYS_MC_MOVE_ODD_ONE_DOUBLE:
        opf.multiple = entry_num;
        opf.reverse = 0;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, &new_offset));
        if (new_offset == max_left_offset)
        {
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, new_offset));
            return CTC_E_NO_RESOURCE;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, (max_left_offset - 1), new_offset));
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, (max_left_offset - 1)));
        break;

    case SYS_MC_MOVE_EVEN_ONE_DOUBLE:
        opf.multiple = entry_num;
        opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
        CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, entry_num, &new_offset));
        if ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (new_offset == (min_right_offset - 1)))
        {
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, new_offset));
            return CTC_E_NO_RESOURCE;
        }

        bound_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - 1) : min_right_offset;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset, new_offset));
        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, bound_offset));
        break;

    case SYS_MC_MOVE_COMPLX_EVEN_ZERO_ONE:
        if ((move_dir == SYS_MC_MOVE_DIR_PREVISE) && (max_left_offset <= 0))
        {
            return CTC_E_NO_RESOURCE;
        }

        if ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (min_right_offset >= (p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] - 1)))
        {
            return CTC_E_NO_RESOURCE;
        }

        p_node = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? p_gb_ipmc_master[lchip]->hash_node[ip_version][max_left_offset - 1] : p_gb_ipmc_master[lchip]->hash_node[ip_version][min_right_offset];
        if (!(CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC)))
        {
            opf.multiple = 1;
            opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
            CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &new_offset));
            bound_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - 1) : min_right_offset;
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset, new_offset));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, bound_offset));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_remove_serious(lchip, opf, move_dir));
        }

        break;

    case SYS_MC_MOVE_EVEN_ZERO_DOUBLE:
        if ((move_dir == SYS_MC_MOVE_DIR_PREVISE) && (max_left_offset <= 2))
        {
            return CTC_E_NO_RESOURCE;
        }

        if ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (min_right_offset >= (p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] - 2)))
        {
            return CTC_E_NO_RESOURCE;
        }

        p_node = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? p_gb_ipmc_master[lchip]->hash_node[ip_version][max_left_offset - 1] : p_gb_ipmc_master[lchip]->hash_node[ip_version][min_right_offset];
        if (!(CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC)))
        {
            opf.multiple = 1;
            opf.reverse = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? 0 : 1;
            CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &new_offset_hit1));
            ret1 = sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &new_offset_hit2);
            if (ret1 < 0)
            {
                sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
                return ret1;
            }

            if (((move_dir == SYS_MC_MOVE_DIR_PREVISE) && (new_offset_hit2 >= max_left_offset)) \
                || ((move_dir == SYS_MC_MOVE_DIR_REVERSE) && (new_offset_hit2 < min_right_offset)))
            {
                sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit1);
                sys_greatbelt_opf_free_offset(lchip, &opf, 1, new_offset_hit2);
                return CTC_E_NO_RESOURCE;
            }

            bound_offset = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - 1) : min_right_offset;
            bound_offset1 = (move_dir == SYS_MC_MOVE_DIR_PREVISE) ? (max_left_offset - 2) : (min_right_offset + 1);

            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset, new_offset_hit1));
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, bound_offset1, new_offset_hit2));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, bound_offset));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, bound_offset1));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_remove_serious(lchip, opf, move_dir));
        }

        break;

    default:
        return CTC_E_IPMC_OFFSET_ALLOC_ERROR;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_ipmc_db_ipmc_offset_move(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    sys_mc_offset_move_mode_t mode = SYS_MC_MOVE_MAX;
    sys_mc_offset_move_dir_t move_dir = SYS_MC_MOVE_DIR_MAX;
    sys_greatbelt_opf_t opf;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? OPF_IPV6_MC_BLOCK : OPF_IPV4_MC_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));
    opf.multiple = 1;

    /*ipmc    ^^#' */
    if ((max_left_offset == min_right_offset) \
        && ((max_left_offset % 2) == 1))
    {
        mode = SYS_MC_MOVE_EVEN_ONE_DOUBLE;
        move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_REVERSE : SYS_MC_MOVE_DIR_PREVISE;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, move_dir));
    }
    /*ipmc    ^^# */
    else if ((max_left_offset == min_right_offset) \
             && ((max_left_offset % 2) == 0))
    {
        mode = SYS_MC_MOVE_COMPLX_EVEN_ZERO_ONE;
        move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_REVERSE : SYS_MC_MOVE_DIR_PREVISE;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, move_dir));
    }
    else
    {
        return CTC_E_IPMC_OFFSET_ALLOC_ERROR;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_l2mc_offset_move(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    int32  ret = 0;
    uint32 ret1 = 0;
    uint32 ret2 = 0;
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    uint32 move_offset = 0;
    uint32 free_offset = 0;
    sys_mc_offset_move_mode_t mode = SYS_MC_MOVE_MAX;
    sys_mc_offset_move_dir_t move_dir = SYS_MC_MOVE_DIR_MAX;
    sys_greatbelt_opf_t opf;
    uint8 ip_version;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    ip_version = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? OPF_IPV6_MC_BLOCK : OPF_IPV4_MC_BLOCK;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));
    opf.multiple = 2;

    /*l2mc    ^#'#^  */
    if ((max_left_offset == (min_right_offset - 2)) \
        && ((max_left_offset % 2) == 1))
    {
        free_offset = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? (max_left_offset - 1) : min_right_offset;
        move_offset = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? (min_right_offset - 1) : max_left_offset;
        if (free_offset != p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version])
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_update_key(lchip, ip_version, free_offset, move_offset));
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, free_offset));
            return CTC_E_NONE;
        }

        move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_PREVISE : SYS_MC_MOVE_DIR_REVERSE;

        mode = SYS_MC_MOVE_ODD_ONE_DOUBLE;
        ret = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, SYS_MC_MOVE_DIR_PREVISE);
        if (ret != CTC_E_NO_RESOURCE)
        {
            return ret;
        }

        mode = SYS_MC_MOVE_EVEN_ONE_DOUBLE;
        ret = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, SYS_MC_MOVE_DIR_REVERSE);
        return ret;
    }
/*l2mc    ^#'(#)^ */
    else if (max_left_offset == (min_right_offset - 1))
    {
        move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_PREVISE : SYS_MC_MOVE_DIR_REVERSE;
        ret = _sys_greatbelt_ipmc_db_offset_remove_double(lchip, opf, move_dir, FALSE);
        if (ret != CTC_E_NO_RESOURCE)
        {
            return ret;
        }

        mode = ((max_left_offset % 2) == 1) ? SYS_MC_MOVE_ODD_ONE_DOUBLE : SYS_MC_MOVE_EVEN_ONE_DOUBLE;
        move_dir = ((max_left_offset % 2) == 1) ? SYS_MC_MOVE_DIR_PREVISE : SYS_MC_MOVE_DIR_REVERSE;
        ret = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, move_dir);
        if (ret != CTC_E_NO_RESOURCE)
        {
            return ret;
        }

        if ((((max_left_offset % 2) == 1) && (move_dir == SYS_MC_MOVE_DIR_REVERSE)) \
            || (((max_left_offset % 2) == 0) && (move_dir == SYS_MC_MOVE_DIR_PREVISE)))
        {
            return CTC_E_NO_RESOURCE;
        }

        mode = SYS_MC_MOVE_EVEN_ZERO_DOUBLE;
        move_dir = ((max_left_offset % 2) == 1) ? SYS_MC_MOVE_DIR_REVERSE : SYS_MC_MOVE_DIR_PREVISE;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 2, move_dir));
    }
/*l2mc    ^^#' */
    else if ((max_left_offset == min_right_offset) \
             && ((max_left_offset % 2) == 1))
    {
        move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_PREVISE : SYS_MC_MOVE_DIR_REVERSE;
        ret = _sys_greatbelt_ipmc_db_offset_remove_double(lchip, opf, move_dir, FALSE);
        if (ret != CTC_E_NO_RESOURCE)
        {
            return ret;
        }

        mode = SYS_MC_MOVE_ODD_ONE_DOUBLE;
        ret1 = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, SYS_MC_MOVE_DIR_PREVISE);
        mode = SYS_MC_MOVE_EVEN_ONE_DOUBLE;
        ret2 = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 1, SYS_MC_MOVE_DIR_REVERSE);
        if ((ret1 < 0) && (ret1 != CTC_E_NO_RESOURCE))
        {
            return ret1;
        }
        else if ((ret2 < 0) && (ret2 != CTC_E_NO_RESOURCE))
        {
            return ret2;
        }
        else if ((ret1 == 0) && (ret2 == 0))
        {
            return CTC_E_NONE;
        }
        else if ((ret1 == CTC_E_NO_RESOURCE) && (ret2 == CTC_E_NO_RESOURCE))
        {
            return CTC_E_NO_RESOURCE;
        }

        mode = SYS_MC_MOVE_EVEN_ZERO_DOUBLE;
        move_dir = (ret1 == 0) ? SYS_MC_MOVE_DIR_PREVISE : SYS_MC_MOVE_DIR_REVERSE;
        CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 2, move_dir));
    }
/*l2mc    ^^# */
    else if ((max_left_offset == min_right_offset) \
             && ((max_left_offset % 2) == 0))
    {
        mode = SYS_MC_MOVE_EVEN_ZERO_DOUBLE;
        ret = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 2, SYS_MC_MOVE_DIR_PREVISE);
        if (ret != CTC_E_NO_RESOURCE)
        {
            return ret;
        }

        mode = SYS_MC_MOVE_EVEN_ZERO_DOUBLE;
        ret = _sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, 2, SYS_MC_MOVE_DIR_REVERSE);
        return ret;
    }
    else
    {
        return CTC_E_IPMC_OFFSET_ALLOC_ERROR;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_get_offset(uint8 lchip, sys_ipmc_group_node_t* p_node)
{

    int32 ret = CTC_E_NONE;
    uint8 ip_version;
    sys_greatbelt_opf_t opf;
    sys_mc_offset_move_mode_t mode = SYS_MC_MOVE_MAX;
    uint32 offset = 0;
    uint8 entry_num = 0;
    uint8  multiple = 0;
    sys_mc_offset_move_dir_t move_dir = SYS_MC_MOVE_DIR_MAX;
    uint16 ipmc_160key = p_gb_ipmc_master[lchip]->is_ipmcv4_key160;
    uint32 max_left_offset = 0;
    uint32 min_right_offset = 0;
    uint32 vrfid = 0;
    uint32 block_id = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    ip_version = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (p_gb_ipmc_master[lchip]->is_sort_mode)
    {
        if ((p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) && (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_S_G : SYS_IPMC_SORT_V4_S_G;
        }
        else if (p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN)
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_G : SYS_IPMC_SORT_V4_G;
        }
        else
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_DFT : SYS_IPMC_SORT_V4_DFT;
        }
        p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.block_id = block_id;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_alloc_offset(lchip, &p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info, &offset));
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, offset) = p_node;
        p_node->ad_index = offset;

        return CTC_E_NONE;
    }

    if (!(p_node->flag & SYS_IPMC_FLAG_VERSION) &&
        (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160))
    {
        entry_num = 2;
    }
    else
    {
        entry_num = 1;
    }

    if ((!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN))&&
        (!CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_GROUP_IP_MASK_LEN)))
    {
        vrfid = (CTC_IP_VER_6 == ip_version) ? p_node->address.ipv6.vrfid : p_node->address.ipv4.vrfid;
        offset = p_gb_ipmc_master[lchip]->default_entry_adindex[ip_version] + vrfid * entry_num;
        p_node->ad_index = offset;
        return ret;
    }

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? OPF_IPV6_MC_BLOCK : OPF_IPV4_MC_BLOCK;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_opf_get_bound(lchip, &opf, &max_left_offset, &min_right_offset));

    multiple = entry_num;
    opf.multiple = multiple;
    opf.reverse = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 0 : 1;
    ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, multiple, &offset);

    if (CTC_E_NO_RESOURCE == ret)
    {
        /*move*/
        /*ipv6 & ipv4(160,160)*/
        if ((p_node->flag & SYS_IPMC_FLAG_VERSION) \
            || (!(p_node->flag & SYS_IPMC_FLAG_VERSION) && ipmc_160key))
        {
            mode = SYS_MC_MOVE_EASY_ZERO_ONE;
            move_dir = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? SYS_MC_MOVE_DIR_REVERSE : SYS_MC_MOVE_DIR_PREVISE;
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_offset_move(lchip, opf, mode, entry_num, move_dir));
        }
        /*ipv4(80,160)*/
        else if (1 == entry_num)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_ipmc_offset_move(lchip, p_node));
        }
        else if (2 == entry_num)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_l2mc_offset_move(lchip, p_node));
        }

        /*alloc again*/
        opf.reverse = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 0 : 1;
        ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, multiple, &offset);
        if (ret < 0)
        {
            return CTC_E_IPMC_OFFSET_ALLOC_ERROR;
        }
    }
    else if (ret < 0)
    {
        return ret;
    }

    p_node->ad_index = offset;
    p_gb_ipmc_master[lchip]->hash_node[ip_version][offset] = p_node;
    if (!(p_node->flag & SYS_IPMC_FLAG_VERSION) &&
        (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160))
    {
        p_gb_ipmc_master[lchip]->hash_node[ip_version][offset + 1] = p_node;
    }

    return ret;

}

STATIC int32
_sys_greatbelt_ipmc_db_free_offset(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    uint8  ip_version = 0;
    uint8 entry_num = 0;
    uint32 ipmc_160key = p_gb_ipmc_master[lchip]->is_ipmcv4_key160;
    sys_greatbelt_opf_t opf;
    uint32 block_id = 0;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    ip_version = (p_node->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    if (p_gb_ipmc_master[lchip]->is_sort_mode)
    {
        if ((p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) && (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN))
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_S_G : SYS_IPMC_SORT_V4_S_G;
        }
        else if (p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN)
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_G : SYS_IPMC_SORT_V4_G;
        }
        else
        {
            block_id = (CTC_IP_VER_6 == ip_version)? SYS_IPMC_SORT_V6_DFT : SYS_IPMC_SORT_V4_DFT;
        }

        p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info.block_id = block_id;
        SORT_KEY_GET_OFFSET_PTR(p_gb_ipmc_db_master[lchip]->p_ipmc_offset_array, p_node->ad_index) = NULL;
        CTC_ERROR_RETURN(sys_greatbelt_sort_key_free_offset(lchip, &p_gb_ipmc_db_master[lchip]->ipmc_sort_key_info,  p_node->ad_index));

        goto End;
    }

    if ((CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_SRC_IP_MASK_LEN)) ||
        (CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_GROUP_IP_MASK_LEN)))
    {
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type = (ip_version == CTC_IP_VER_4) ? OPF_IPV4_MC_BLOCK : OPF_IPV6_MC_BLOCK;
        opf.pool_index = 0;

        if (ip_version == CTC_IP_VER_4 &&
            (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || ipmc_160key))
        { /*160 bit key*/
            entry_num = 2;
        }
        else
        {
            entry_num = 1;
        }

        p_gb_ipmc_master[lchip]->hash_node[ip_version][p_node->ad_index] = NULL;
        if (!(p_node->flag & SYS_IPMC_FLAG_VERSION) &&
            (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) || p_gb_ipmc_master[lchip]->is_ipmcv4_key160))
        {
            p_gb_ipmc_master[lchip]->hash_node[ip_version][p_node->ad_index + 1] = NULL;
        }

        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, entry_num, p_node->ad_index));
    }

End:
    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK) &&
        !CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC) &&
        CTC_FLAG_ISSET(p_node->flag, SYS_IPMC_FLAG_RPF_PROFILE))
    {
         /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_greatbelt_rpf_remove_profile\n");*/
        CTC_ERROR_RETURN(sys_greatbelt_rpf_remove_profile(lchip, p_node->sa_index));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_add(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_get_offset(lchip, p_node));
    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_add(lchip, p_node));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_remove(uint8 lchip, sys_ipmc_group_node_t* p_node)
{
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_free_offset(lchip, p_node));

    /* free group node from database, which can be Hash or AVL */
     /*SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_greatbelt_ipmc_db_remove\n");*/
    CTC_ERROR_RETURN(_sys_greatbelt_ipmc_db_remove(lchip, p_node));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_lookup(uint8 lchip, sys_ipmc_group_node_t** pp_node)
{
    uint8  ip_version = 0;
    sys_ipmc_group_node_t* p_ipmc_group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(pp_node);
    ip_version = ((*pp_node)->flag & SYS_IPMC_FLAG_VERSION) ? CTC_IP_VER_6 : CTC_IP_VER_4;

    p_ipmc_group_node = ctc_hash_lookup(p_gb_ipmc_db_master[lchip]->ipmc_hash[ip_version], *pp_node);

    *pp_node = p_ipmc_group_node;

    return CTC_E_NONE;
}

int32
sys_greatbelt_show_ipmc_info(sys_ipmc_group_node_t* p_node, void* data)
{
    char buf[CTC_IPV6_ADDR_STR_LEN];

#define SYS_IPMC_MASK_LEN  5
    char buf2[SYS_IPMC_MASK_LEN];
    uint32 tempip = 0;
    uint8 value = 0;
    uint8 do_flag = 0;
    uint8  lchip = 0;
    uint32 ad_index = 0;
    lchip = ((sys_ipmc_traverse_t*)data)->lchip;
    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_node);

    if(CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_DROP))
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6s", " - ");
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-6u", p_node->group_id);
    }

    if (!(p_node->flag & SYS_IPMC_FLAG_VERSION))
    {
        /* print IPv4 source IP address */
        value = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 32 : 0;
        sal_sprintf(buf2, "/%-3d", value);
        tempip = sal_ntohl(p_node->address.ipv4.src_addr);
        sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-24s", buf);

        /* print IPv4 group IP address */
        value = (p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 32 : 0;
        sal_sprintf(buf2, "/%-3d", value);
        tempip = sal_ntohl(p_node->address.ipv4.group_addr);
        sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-24s", buf);

        /* print vrfId */

        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-5d", p_node->address.ipv4.vrfid);
        if (((p_gb_ipmc_master[lchip]->is_ipmcv4_key160) || (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_L2MC)))
            &&(!p_gb_ipmc_master[lchip]->is_sort_mode))
        {
            ad_index = p_node->ad_index / 2;
        }
        else
        {
            ad_index = p_node->ad_index;
        }
    }
    else
    {
        uint32 ipv6_address[4] = {0, 0, 0, 0};

        /* print IPv6 source IP address */
        value = (p_node->flag & SYS_IPMC_FLAG_SRC_IP_MASK_LEN) ? 128 : 0;
        sal_sprintf(buf2, "/%-3d", value);

        ipv6_address[0] = sal_htonl(p_node->address.ipv6.src_addr[3]);
        ipv6_address[1] = sal_htonl(p_node->address.ipv6.src_addr[2]);
        ipv6_address[2] = sal_htonl(p_node->address.ipv6.src_addr[1]);
        ipv6_address[3] = sal_htonl(p_node->address.ipv6.src_addr[0]);

        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-33s", buf);

        /* print IPv6 group IP address */
        value = (p_node->flag & SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) ? 128 : 0;
        sal_sprintf(buf2, "/%-3d", value);

        ipv6_address[0] = sal_htonl(p_node->address.ipv6.group_addr[3]);
        ipv6_address[1] = sal_htonl(p_node->address.ipv6.group_addr[2]);
        ipv6_address[2] = sal_htonl(p_node->address.ipv6.group_addr[1]);
        ipv6_address[3] = sal_htonl(p_node->address.ipv6.group_addr[0]);

        sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
        sal_strncat(buf, buf2, SYS_IPMC_MASK_LEN);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-33s", buf);

        /* print vrfId */
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-5d", p_node->address.ipv6.vrfid);
        ad_index = (p_gb_ipmc_master[lchip]->is_sort_mode)? (p_node->ad_index / 2) : p_node->ad_index;
    }

    buf2[0] = '\0';
    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_DROP))
    {
        sal_strncat(buf2, "D", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_RPF_CHECK))
    {
        sal_strncat(buf2, "R", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_TTL_CHECK))
    {
        sal_strncat(buf2, "T", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_COPY_TOCPU))
    {
        sal_strncat(buf2, "C", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_RPF_FAIL_BRIDGE_AND_TOCPU))
    {
        sal_strncat(buf2, "F", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_STATS))
    {
        sal_strncat(buf2, "S", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_PTP_ENTRY))
    {
        sal_strncat(buf2, "P", 1);
        do_flag = 1;
    }

    if (CTC_FLAG_ISSET(p_node->extern_flag, CTC_IPMC_FLAG_REDIRECT_TOCPU))
    {
        sal_strncat(buf2, "U", 1);
        do_flag = 1;
    }

    if (!do_flag)
    {
        sal_strncat(buf2, "-", 1);
    }
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-11d %-8s", p_node->nexthop_id, buf2);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-8s",
        CTC_FLAG_ISSET(p_node->extern_flag,CTC_IPMC_FLAG_L2MC) ?  "IP-L2MC" : "IPMC");


    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-8d\n", ad_index);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_show_group_info(uint8 lchip, uint8 ip_version)
{
    sys_ipmc_traverse_t      travs = {0};
    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK(lchip);
    CTC_IP_VER_CHECK(ip_version);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "flag: R-RPF check       T-TTL check    C-copied to CPU\
      F-sent to CPU and fallback bridged\n\
      S-stats enable    P-PTP entry    U-redirected to CPU  D-drop\n");
    if (CTC_IP_VER_4 == ip_version)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s %-24s %-24s %-5s %-11s %-8s %-8s %-8s\n", "Group-ID", "IPSA",
                       "Group-IP", "VRF", "NHID", "Flag", "Mode", "AD-Index");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                       "------------------------------------------------------------------------------------------------------------\n");
    }
    else
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s %-33s %-33s %-5s %-11s %-8s %-8s %-8s\n", "Group-ID", "IPSA",
                       "Group-IP", "VRF", "NHID", "Flag", "Mode", "AD-Index");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,
                       "-------------------------------------------------------------------------------------------------------------------------\n");
    }
    travs.lchip = lchip;
    ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[ip_version], (hash_traversal_fn)sys_greatbelt_show_ipmc_info, &travs);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_show_l2mc_count(sys_ipmc_group_node_t* p_node, uint16* count)
{
    int32 ret = 0;
    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (p_node->extern_flag & CTC_IPMC_FLAG_L2MC)
    {
        *count = (*count) + 1;
    }

    return ret;
}

int32
sys_greatbelt_ipmc_db_show_count(uint8 lchip)
{
    uint32 i;
    uint16 count = 0;
    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK(lchip);

    for (i = 0; i < MAX_CTC_IP_VER; i++)
    {
        ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[i], (hash_traversal_fn)_sys_greatbelt_ipmc_db_show_l2mc_count, &count);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MCv%c total %d routes\n", (i == CTC_IP_VER_4) ? '4' : '6', count);
        count = p_gb_ipmc_db_master[lchip]->ipmc_hash[i]->count - count;
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMCv%c total %d routes\n", (i == CTC_IP_VER_4) ? '4' : '6', count);
        count = 0;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_db_show_member_info(uint8 lchip, ctc_ipmc_group_info_t* p_group)
{

    uint8 ip_version = 0;

    sys_ipmc_group_node_t* p_group_node;
    sys_ipmc_group_node_t group_node;

    SYS_IPMC_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_group);
    CTC_IP_VER_CHECK(p_group->ip_version);
    SYS_IPMC_VERSION_ENABLE_CHECK(p_group->ip_version);

    sal_memset(&group_node, 0, sizeof(sys_ipmc_group_node_t));

    if (!(p_gb_ipmc_master[lchip]->is_ipmcv4_key160 || (p_group->flag & CTC_IPMC_FLAG_L2MC)))
    {
        SYS_IPMC_NO_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    if (!CTC_FLAG_ISSET(p_group->flag, CTC_IPMC_FLAG_L2MC))
    {
        SYS_IPMC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }
    else
    {
        SYS_L2MC_VRFID_CHECK(p_group->address, p_group->ip_version);
    }

    SYS_IPMC_ADDRESS_SORT(p_group);
    SYS_IPMC_MASK_LEN_CHECK(p_group->ip_version, p_group->src_ip_mask_len, p_group->group_ip_mask_len);
    SYS_IPMC_GROUP_ADDRESS_CHECK(p_group->ip_version, p_group->address);
    SYS_IPMC_MASK_ADDR(p_group->address, p_group->src_ip_mask_len, p_group->group_ip_mask_len, p_group->ip_version);

    ip_version = p_group->ip_version;

    p_group_node = &group_node;
    p_group_node->flag = (p_group->ip_version == CTC_IP_VER_6) ? (p_group_node->flag | SYS_IPMC_FLAG_VERSION) : (p_group_node->flag);
    p_group_node->flag = (p_group->src_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_SRC_IP_MASK_LEN) : (p_group_node->flag);
    p_group_node->flag = (p_group->group_ip_mask_len) ? (p_group_node->flag | SYS_IPMC_FLAG_GROUP_IP_MASK_LEN) : (p_group_node->flag);
    sal_memcpy(&p_group_node->address, &p_group->address, p_gb_ipmc_master[lchip]->addr_len[ip_version]);
    p_group_node->extern_flag = p_group->flag;

    SYS_IPMC_ADDRESS_SORT(p_group);
    CTC_ERROR_RETURN(sys_greatbelt_ipmc_db_lookup(lchip, &p_group_node));

    if (!p_group_node)
    {
        /* The group to show is NOT existed*/
        return CTC_E_IPMC_GROUP_NOT_EXIST;
    }
    else
    {
        sys_greatbelt_nh_dump(lchip, p_group_node->nexthop_id, FALSE);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_ipmc_offset_show(uint8 lchip, ctc_ip_ver_t ip_ver)
{
    sys_greatbelt_opf_t opf;
    SYS_IPMC_INIT_CHECK(lchip);
    SYS_IPMC_DB_INIT_CHECK(lchip);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = (ip_ver == CTC_IP_VER_4) ? OPF_IPV4_MC_BLOCK : OPF_IPV6_MC_BLOCK;
    opf.pool_index = 0;
    sys_greatbelt_opf_print_sample_info(lchip, &opf);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipmc_db_show_ipv4_count(sys_ipmc_group_node_t* p_node, sys_mc_count_t* count)
{
    int32 ret = 0;
    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (!(p_node->extern_flag & CTC_IPMC_FLAG_L2MC))
    {
        if ((p_node->flag & 0x3) == 3)
        {
            count->ipmc_sg_count = (count->ipmc_sg_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 1)
        {
            count->ipmc_g_count = (count->ipmc_g_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 0)
        {
            count->ipmc_def_count = (count->ipmc_def_count) + 1;
        }
    }
    else
    {
        if ((p_node->flag & 0x3) == 3)
        {
            count->l2mc_sg_count = (count->l2mc_sg_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 1)
        {
            count->l2mc_g_count = (count->l2mc_g_count) + 1;
        }
    }

    return ret;
}

STATIC int32
_sys_greatbelt_ipmc_db_show_ipv6_count(sys_ipmc_group_node_t* p_node, sys_mc_count_t* count)
{
    int32 ret = 0;
    CTC_PTR_VALID_CHECK(p_node);
    CTC_PTR_VALID_CHECK(count);
    if (!(p_node->extern_flag & CTC_IPMC_FLAG_L2MC))
    {
        if ((p_node->flag & 0x3) == 3)
        {
            count->ipmc_sg_count = (count->ipmc_sg_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 1)
        {
            count->ipmc_g_count = (count->ipmc_g_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 0)
        {
            count->ipmc_def_count = (count->ipmc_def_count) + 1;
        }
    }
    else
    {
        if ((p_node->flag & 0x3) == 3)
        {
            count->l2mc_sg_count = (count->l2mc_sg_count) + 1;
        }
        else if ((p_node->flag & 0x3) == 1)
        {
            count->l2mc_g_count = (count->l2mc_g_count) + 1;
        }
    }

    return ret;
}

int32
sys_greatbelt_ipmc_show_status(uint8 lchip)
{
    int32 ret = 0;
    uint32 table_size = 0;
    uint8 default_num = 0;
    sys_mc_count_t ip_count;

    SYS_IPMC_INIT_CHECK(lchip);
    sal_memset(&ip_count, 0, sizeof(sys_mc_count_t));
    if (p_gb_ipmc_master[lchip]->is_sort_mode)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n==================================\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPV4 and IPV6 is in share mode!");
    }
    /*ipv4*/
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n==================================\n");
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_4], &table_size));
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "IPV4 tcam size", table_size);
    default_num = (!p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_4]) ? 0 : 1;
    table_size = p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4]->count + default_num;
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "Total entry count", table_size);
    ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_4], (hash_traversal_fn)_sys_greatbelt_ipmc_db_show_ipv4_count, &ip_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.ipmc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.ipmc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u\n", "(*,*) entry count", ip_count.ipmc_def_count + default_num);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.l2mc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.l2mc_g_count);

    /*ipv6*/
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n==================================\n");
    table_size = 0;
    sal_memset(&ip_count, 0, sizeof(sys_mc_count_t));
    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, p_gb_ipmc_master[lchip]->key_tcam_table_id[CTC_IP_VER_6], &table_size));
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "IPV6 tcam size", table_size);
    default_num = (!p_gb_ipmc_master[lchip]->version_en[CTC_IP_VER_6]) ? 0 : 1;
    table_size = p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6]->count + default_num;
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "Total entry count", table_size);
    ctc_hash_traverse(p_gb_ipmc_db_master[lchip]->ipmc_hash[CTC_IP_VER_6], (hash_traversal_fn)_sys_greatbelt_ipmc_db_show_ipv6_count, &ip_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "IPMC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.ipmc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.ipmc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u\n", "(*,*) entry count", ip_count.ipmc_def_count + default_num);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "L2MC:\n");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(S,G) entry count", ip_count.l2mc_sg_count);
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s:%4u \n", "(*,G) entry count", ip_count.l2mc_g_count);

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n==================================\n");
    return ret;

}

int32
_sys_greatbelt_ipmc_db_traverse(uint8 lchip, uint8 ip_ver, hash_traversal_fn fn, void* data)
{
    return ctc_hash_traverse_through(p_gb_ipmc_db_master[lchip]->ipmc_hash[ip_ver], fn, data);
}


int32
sys_greatbelt_ipmc_block_show(uint8 lchip, uint8 blockid, uint8 detail)
{
    sys_sort_block_t* p_block;
    sys_greatbelt_opf_t opf;
    uint8 max_blockid = 0;
    uint32 all_of_num = 0;
    uint32 used_of_num = 0;

    SYS_IPMC_INIT_CHECK(lchip);

    max_blockid = SYS_IPMC_SORT_MAX;

    if (blockid >= max_blockid)
    {
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Error Block Index\n\r");
        return CTC_E_NONE;
    }

    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s %-8s %-8s %-8s %-8s %-8s\n", "ID",  "L", "R", "M", "All", "Used");
    SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "=========================================================\n");

    if (detail)
    {
        p_block = &p_gb_ipmc_db_master[lchip]->blocks[blockid];
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d %-8d %-8d %-8d %-8d %-8d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple, p_block->all_of_num, p_block->used_of_num);
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type = OPF_IPV4_MC_BLOCK;
        opf.pool_index = blockid;
        sys_greatbelt_opf_print_sample_info(lchip, &opf);
    }
    else
    {
        for (blockid = 0; blockid < max_blockid;  blockid++)
        {
            p_block = &p_gb_ipmc_db_master[lchip]->blocks[blockid];
            SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d %-8d %-8d %-8d %-8d %-8d\n", blockid,  p_block->all_left_b, p_block->all_right_b, p_block->multiple, p_block->all_of_num, p_block->used_of_num);
            all_of_num += p_block->all_of_num;
            used_of_num += p_block->used_of_num;
        }
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "=========================================================\n");
        SYS_IPMC_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8s %-8s %-8s %-8s %-8d %-8d\n", "Total",  "-", "-", "-", all_of_num, used_of_num);
    }

    return CTC_E_NONE;
}



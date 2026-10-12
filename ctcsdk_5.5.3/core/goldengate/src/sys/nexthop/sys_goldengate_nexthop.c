/**
 @file sys_goldengate_nexthop.c

 @date 2009-09-16

 @version v2.0

 The file contains all nexthop module core logic
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_linklist.h"
#include "ctc_linklist.h"
#include "ctc_warmboot.h"

#include "sys_goldengate_opf.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_api.h"
#include "sys_goldengate_stats.h"

#include "sys_goldengate_cpu_reason.h"

#include "sys_goldengate_vlan.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_aps.h"
#include "sys_goldengate_bpe.h"
#include "sys_goldengate_internal_port.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_wb_nh.h"
#include "sys_goldengate_register.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

#define DS_ARRAY_T(size) struct { uint32 array[size/4];}
typedef DS_ARRAY_T(DS_NEXT_HOP4_W_BYTES) ds_nexthop4w_t;
typedef DS_ARRAY_T(DS_NEXT_HOP8_W_BYTES) ds_nexthop8w_t;

/**********************************/
enum opf_nh_index
{
    OPF_NH_DYN1_SRAM, /*dynamic sram(may be DsFwd) */
    OPF_NH_DYN2_SRAM, /*dynamic sram(may be DsNexthop, DsL2Edit, DsL3Edit, etc) */
    OPF_NH_DYN3_SRAM, /*dynamic sram(may be DsMet) */
    OPF_NH_DYN4_SRAM, /*dynamic sram(may be DsNexthop, DsL2Edit, DsL3Edit, etc) */
    OPF_NH_DYN5_SRAM, /*l2 edit in split mode*/
    OPF_NH_NHID_INTERNAL,  /*internal NHID for DsNexthop*/
    OPF_NH_OUTER_L2EDIT,  /*dsl2outeredit for arp*/
    OPF_NH_OUTER_SPME,  /*dsl2outeredit for spme*/
    OPF_NH_L2EDIT_VLAN_PROFILE,  /*vlan action profile for dsl2edit*/
    OPF_NH_DYN_MAX
};

#define SYS_GOLDENGATE_INVALID_STATS_PTR 0
#define SYS_GOLDENGATE_NH_DROP_DESTMAP          0xFFFE

#define SYS_DSNH4WREG_INDEX_FOR_BRG            0xC      /*must be b1100, for GB is b00, shift is 2*/
#define SYS_DSNH4WREG_INDEX_FOR_MIRROR         0xD      /*must be b1101, for GB is b01, shift is 2*/
#define SYS_DSNH4WREG_INDEX_FOR_BYPASS         0xE      /*must be b1110, for GB is b10, shift is 2, (use 8w)*/

#define SYS_DSNH_FOR_REMOTE_MIRROR  0    /*IngressEdit Mode for remote mirror*/

#define SYS_NH_EXTERNAL_NHID_DEFAUL_NUM 16384
#define SYS_NH_TUNNEL_ID_DEFAUL_NUM     1024
#define SYS_NH_ECMP_GROUP_ID_NUM 1023
#define SYS_NH_ECMP_MEMBER_NUM 16384

#define SYS_NH_ARP_ID_DEFAUL_NUM        (1024*32)

#define SYS_NH_BUILD_INT_OFFSET(offset) \
    (SYS_NH_DSNH_INTERNAL_BASE<<SYS_NH_DSNH_INTERNAL_SHIFT)|offset;


#define SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(_dsnh)    \
    do {                                                      \
       _dsnh.replace_ctag_cos = 0;\
       _dsnh.copy_ctag_cos= 0;    \
       _dsnh.derive_stag_cos= 0;\
       _dsnh.stag_cfi= 0;\
    } while (0)

#define SYS_GOLDENGATE_NH_DSNH_ASSIGN_MACDA(_dsnh, _dsnh_param)         \
    do {                                                               \
         sal_memcpy(_dsnh.mac_da, _dsnh_param.macDa, sizeof(mac_addr_t)); \
    } while (0)

#define SYS_GOLDENGATE_NH_DSNH_BUILD_L2EDITPTR_L3EDITPTR(__dsnh,     \
                                                        __dsnh_l2editptr, __dsnh_l3editptr)     \
    do {                                                            \
    } while (0)

#define SYS_GOLDENGATE_NH_DSNH8W_BUILD_L2EDITPTR_L3EDITPTR(__dsnh,   \
                                                          __dsnh_l2editptr, __dsnh_l3editptr)     \
    do {                                                            \
    } while (0)

#define SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(__dsnh,  flag)  \
    do {                                                                    \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_TTL_FROM_PKT)) \
        { \
            __dsnh.use_packet_ttl = 1;\
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_STAG_COS)) \
        { \
            __dsnh.stag_cfi = 1; \
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_CTAG_COS)) \
        { \
            __dsnh.replace_ctag_cos = 1; \
        } \
        if (CTC_FLAG_ISSET(flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP)) \
        { \
            __dsnh.stag_cos |= 1 << 1; \
        } \
    } while (0)

#define SYS_GOLDENGATE_NH_MAX_ECMP_CHECK(max_ecmp)  \
    do {    \
        if ((max_ecmp%4 != 0) || (max_ecmp > 0 && max_ecmp < 16))    \
        {   \
            return CTC_E_INVALID_PARAM; \
        }   \
        else if (max_ecmp > SYS_GG_MAX_ECPN)   \
        {   \
            return CTC_E_NH_EXCEED_MAX_ECMP_NUM;    \
        }   \
    } while (0)

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

sys_goldengate_nh_master_t* g_gg_nh_master[CTC_MAX_LOCAL_CHIP_NUM] = { NULL };



/****************************************************************************
 *
* Function
*
*****************************************************************************/
int32
_sys_goldengate_nh_api_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_goldengate_nh_master_t* p_nh_master,
                                         sys_nh_info_com_t** pp_nhinfo);

int32
_sys_goldengate_nh_alloc_offset_by_nhtype(uint8 lchip, sys_nh_param_com_t* p_nh_com_para, sys_nh_info_com_t* p_nhinfo);

int32
_sys_goldengate_nh_free_offset_by_nhinfo(uint8 lchip, sys_goldengate_nh_type_t nh_type, sys_nh_info_com_t* p_nhinfo);

extern int32
_sys_goldengate_nh_ipuc_update_dsnh_cb(uint8 lchip, uint32 nh_offset, void* p_arp_db);

extern int32
sys_goldengate_port_set_mirror_en(uint8 lchip, uint16 lport, uint16 fwd_ptr);
#define __________db__________


/**
 @brief This function is used to get nexthop module master data
 */
STATIC int32
_sys_goldengate_nh_get_nh_master(uint8 lchip, sys_goldengate_nh_master_t** p_out_nh_master)
{
    if (NULL == g_gg_nh_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    *p_out_nh_master = g_gg_nh_master[lchip];
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_nh_master(uint8 lchip, sys_goldengate_nh_master_t** p_nh_master)
{
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, p_nh_master));
    return CTC_E_NONE;
}

/**
 @brief This function is used to get nexthop module master data
 */
bool
_sys_goldengate_nh_is_ipmc_logic_rep_enable(uint8 lchip)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    return p_nh_master->ipmc_logic_replication ? TRUE : FALSE;
}




int32
_sys_goldengate_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    p_nh_master->ipmc_logic_replication  = (enable) ? 1 : 0;

    return CTC_E_NONE;
}


/**
 @brief Shared entry lookup in AVL treee
 */
STATIC sys_nh_db_dsl2editeth4w_t*
_sys_goldengate_nh_lkup_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit4w_tree,  p_dsl2edit_4w);

    return avl_node ? avl_node->info : NULL;

}

STATIC sys_nh_db_dsl2editeth8w_t*
_sys_goldengate_nh_lkup_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit8w_tree,  p_dsl2edit_8w);

    return avl_node ? avl_node->info : NULL;

}

STATIC sys_nh_db_dsl2editeth4w_t*
_sys_goldengate_nh_lkup_swap_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit4w_swap_tree,  p_dsl2edit_4w);

    return avl_node ? avl_node->info : NULL;

}


/**
 @brief Shared entry lookup in AVL treee
 */
STATIC sys_nh_db_dsl2editeth8w_t*
_sys_goldengate_nh_lkup_l2edit_6w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit6w_tree,  p_dsl2edit_8w);

    return avl_node ? avl_node->info : NULL;

}


/**
 @brief Shared entry lookup in AVL treee
 */
STATIC sys_nh_db_dsl2editeth4w_t*
_sys_goldengate_nh_lkup_l2edit_3w(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    struct ctc_avl_node* avl_node = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    /*2. Do lookup*/
    avl_node = ctc_avl_search(p_master->dsl2edit3w_tree,  p_dsl2edit_4w);

    return avl_node ? avl_node->info : NULL;
}


/**
 @brief Insert node into AVL tree
 */
STATIC int32
_sys_goldengate_nh_insert_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit4w_tree,  p_dsl2edit_4w);

    return ret;
}

STATIC int32
_sys_goldengate_nh_insert_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit8w_tree,  p_dsl2edit_8w);

    return ret;
}

STATIC int32
_sys_goldengate_nh_insert_swap_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit4w_swap_tree,  p_dsl2edit_4w);

    return ret;
}

/**
 @brief Insert node into AVL tree
 */
STATIC int32
_sys_goldengate_nh_insert_l2edit_6w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit6w_tree,  p_dsl2edit_8w);

    return ret;
}

/**
 @brief Insert node into AVL tree
 */
STATIC int32
_sys_goldengate_nh_insert_l2edit_3w(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);

    /*2. Insert node*/
    ret = ctc_avl_insert(p_master->dsl2edit3w_tree,  p_dsl2edit_4w);

    return ret;
}


STATIC int32
_sys_goldengate_nh_write_entry_dsl2editeth4w(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_ds_l2_edit_4w , uint8 type, uint8 ds_drop)
{

    sys_dsl2edit_t dsl2edit;
    sys_nh_info_dsnh_t nhinfo;
    sys_nh_info_com_t* p_nh_com_info;
    uint8 dest_gchip = 0;
    uint8 mac_0[6] = {0};

    sal_memset(&dsl2edit, 0, sizeof(sys_dsl2edit_t));

    if (SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W == type)
    {
        if (ds_drop)
        {
            dsl2edit.ds_type  = SYS_NH_DS_TYPE_DISCARD;
        }
        else
        {
            dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        }
        dsl2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;
        dsl2edit.offset = p_ds_l2_edit_4w->offset;
        dsl2edit.is_vlan_valid = (p_ds_l2_edit_4w->output_vid != 0);
        dsl2edit.output_vlan_id= p_ds_l2_edit_4w->output_vid;
        dsl2edit.is_6w = 0;
        dsl2edit.fpma = p_ds_l2_edit_4w->fpma;
        if (!sal_memcmp(p_ds_l2_edit_4w->mac_da, mac_0, sizeof(mac_addr_t)))
        {
            dsl2edit.derive_mcast_mac_for_ip = 1;
        }
        sal_memcpy(dsl2edit.mac_da, p_ds_l2_edit_4w->mac_da, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W,
                                                            p_ds_l2_edit_4w->offset / 2,
                                                            &dsl2edit));
    }
    else if (SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP == type)
    {
        dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        dsl2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_INNER_SWAP;
        dsl2edit.offset = p_ds_l2_edit_4w->offset;
        dsl2edit.strip_inner_vlan = p_ds_l2_edit_4w->strip_inner_vlan;
        sal_memcpy(dsl2edit.mac_da, p_ds_l2_edit_4w->mac_da, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP,
                                                            p_ds_l2_edit_4w->offset,
                                                            &dsl2edit));
    }
    else if(SYS_NH_ENTRY_TYPE_L2EDIT_3W == type)
    {
        if (ds_drop)
        {
            dsl2edit.ds_type  = SYS_NH_DS_TYPE_DISCARD;
        }
        else
        {
            dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        }
        dsl2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;
        dsl2edit.offset = p_ds_l2_edit_4w->offset;
        dsl2edit.is_vlan_valid = (p_ds_l2_edit_4w->output_vid != 0);
        dsl2edit.output_vlan_id = p_ds_l2_edit_4w->output_vid;
        dsl2edit.is_6w = 0;
        dsl2edit.dynamic = 1;
        sal_memcpy(dsl2edit.mac_da, p_ds_l2_edit_4w->mac_da, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_3W,
                                                            p_ds_l2_edit_4w->offset,
                                                            &dsl2edit));
    }
    else if (SYS_NH_ENTRY_TYPE_L2EDIT_LPBK == type)
    {
        sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ds_l2_edit_4w->nhid, &nhinfo));
        dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        dsl2edit.l2_rewrite_type = SYS_NH_L2EDIT_TYPE_LOOPBACK;
        dsl2edit.dest_map = SYS_ENCODE_DESTMAP(nhinfo.dest_chipid, nhinfo.dest_id);
        dsl2edit.nexthop_ptr = nhinfo.dsnh_offset;
        dsl2edit.nexthop_ext = nhinfo.nexthop_ext;

        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, p_ds_l2_edit_4w->nhid, (sys_nh_info_com_t**)&p_nh_com_info));
        if (CTC_FLAG_ISSET(p_nh_com_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
        {
            sys_goldengate_get_gchip_id(lchip, &dest_gchip);
            dsl2edit.dest_map = SYS_ENCODE_DESTMAP(dest_gchip, SYS_RSV_PORT_DROP_ID);
        }

        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_LPBK,
                                                            p_ds_l2_edit_4w->offset,
                                                            &dsl2edit));
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_nh_write_entry_dsl2editeth8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_ds_l2_edit_8w, uint8 type)
{

    sys_dsl2edit_t dsl2edit;
    uint8 mac_0[6] = {0};

    sal_memset(&dsl2edit, 0, sizeof(sys_dsl2edit_t));

    if(SYS_NH_ENTRY_TYPE_L2EDIT_6W == type)
    {
        dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        dsl2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_8W;
        dsl2edit.offset = p_ds_l2_edit_8w->offset;
        dsl2edit.update_mac_sa = p_ds_l2_edit_8w->update_mac_sa;
        dsl2edit.strip_inner_vlan = p_ds_l2_edit_8w->strip_inner_vlan;
        dsl2edit.is_vlan_valid = (p_ds_l2_edit_8w->output_vid != 0);
        dsl2edit.output_vlan_id= p_ds_l2_edit_8w->output_vid;
        dsl2edit.is_6w = 1;
        sal_memcpy(dsl2edit.mac_da, p_ds_l2_edit_8w->mac_da, sizeof(mac_addr_t));
        sal_memcpy(dsl2edit.mac_sa, p_ds_l2_edit_8w->mac_sa, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_6W,
                                                            p_ds_l2_edit_8w->offset,
                                                            &dsl2edit));
    }
    else if(SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W == type)
    {
        dsl2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
        dsl2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_8W;
        dsl2edit.offset = p_ds_l2_edit_8w->offset;
        dsl2edit.is_vlan_valid = (p_ds_l2_edit_8w->output_vid != 0);
        dsl2edit.output_vlan_id= p_ds_l2_edit_8w->output_vid;
        dsl2edit.is_6w = 1;
        if (!sal_memcmp(p_ds_l2_edit_8w->mac_da, mac_0, sizeof(mac_addr_t)))
        {
            dsl2edit.derive_mcast_mac_for_ip = 1;
        }
        sal_memcpy(dsl2edit.mac_da, p_ds_l2_edit_8w->mac_da, sizeof(mac_addr_t));
        sal_memcpy(dsl2edit.mac_sa, p_ds_l2_edit_8w->mac_sa, sizeof(mac_addr_t));
        dsl2edit.update_mac_sa = 1;
        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                            SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W,
                                                            p_ds_l2_edit_8w->offset/2,
                                                            &dsl2edit));
    }


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_write_entry_arp(uint8 lchip, sys_nh_db_arp_t* p_ds_arp)
{
    sys_nh_db_dsl2editeth4w_t   dsl2edit_4w;
    uint32 cmd = 0;
    uint16 drv_lport = 0;

    hw_mac_addr_t mac_da;
    DsEgressPortMac_m        dsportmac;

    sal_memset(&dsl2edit_4w, 0, sizeof(dsl2edit_4w));

    if (CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_ECMP_IF))
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_ds_arp->gport, lchip, drv_lport);
        SYS_GOLDENGATE_SET_HW_MAC(mac_da,  p_ds_arp->mac_da);
        SetDsEgressPortMac(A, portMac_f, &dsportmac, mac_da);
        cmd = DRV_IOW(DsEgressPortMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, drv_lport, cmd, &dsportmac));
    }
    else
    {
        if (p_ds_arp->offset)
        {
            if(CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_FLAG_DROP))
            {
                /*write l2edit*/
                dsl2edit_4w.offset = p_ds_arp->offset;
                CTC_ERROR_RETURN(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &dsl2edit_4w, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1));
            }
            else
            {/*fwd same as to cpu*/
                /*write l2edit*/
                dsl2edit_4w.offset = p_ds_arp->offset;
                sal_memcpy(dsl2edit_4w.mac_da, p_ds_arp->mac_da, sizeof(mac_addr_t));
                dsl2edit_4w.output_vlan_is_svlan = p_ds_arp->output_vlan_is_svlan;
                dsl2edit_4w.output_vid = p_ds_arp->output_vid;

                CTC_ERROR_RETURN(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &dsl2edit_4w, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 0));
            }
        }

        if (p_ds_arp->in_l2_offset)
        {
            if(CTC_FLAG_ISSET(p_ds_arp->flag, SYS_NH_ARP_FLAG_DROP))
            {
                /*write l2edit*/
                dsl2edit_4w.offset = p_ds_arp->in_l2_offset;
                CTC_ERROR_RETURN(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &dsl2edit_4w, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1));
            }
            else
            {/*fwd same as to cpu*/
                /*write l2edit*/
                dsl2edit_4w.offset = p_ds_arp->in_l2_offset;
                sal_memcpy(dsl2edit_4w.mac_da, p_ds_arp->mac_da, sizeof(mac_addr_t));
                dsl2edit_4w.output_vlan_is_svlan = p_ds_arp->output_vlan_is_svlan;
                dsl2edit_4w.output_vid = p_ds_arp->output_vid;

                CTC_ERROR_RETURN(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &dsl2edit_4w, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 0));
            }
        }
    }

    return CTC_E_NONE;

}

#define NH_L2EDIT_OUTER
int32
sys_goldengate_nh_add_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t** p_dsl2edit_4w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lkup dsl2edit_4w node, mac:%s!!\n",
                   sys_goldengate_output_mac((*p_dsl2edit_4w)->mac_da));

    if ((*p_dsl2edit_4w)->is_ecmp_if)
    {
        sys_goldengate_nh_get_emcp_if_l2edit(lchip, (void**)&p_new_dsl2edit_4w);
        *p_dsl2edit_4w = p_new_dsl2edit_4w;
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_route_l2edit(lchip, (*p_dsl2edit_4w));

    if (p_new_dsl2edit_4w)
    /*Found node*/
    {
        p_new_dsl2edit_4w->ref_cnt++;
        *p_dsl2edit_4w = p_new_dsl2edit_4w;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Found p_new_dsl2edit_4w node, mac:%s!!\n",
            sys_goldengate_output_mac(p_new_dsl2edit_4w->mac_da));
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = (sys_nh_db_dsl2editeth4w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (p_new_dsl2edit_4w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    /*Insert new one*/
    ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
		return ret;
    }

    p_new_dsl2edit_4w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_4w->offset = new_offset;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l2edit offset=%d\n", new_offset);


	p_new_dsl2edit_4w->output_vid = (*p_dsl2edit_4w)->output_vid;
    sal_memcpy(p_new_dsl2edit_4w->mac_da, (*p_dsl2edit_4w)->mac_da, sizeof(mac_addr_t));
    p_new_dsl2edit_4w->fpma = (*p_dsl2edit_4w)->fpma;
    ret = _sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, p_new_dsl2edit_4w,SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 0);
    ret = ret ? ret : _sys_goldengate_nh_insert_route_l2edit(lchip, p_new_dsl2edit_4w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
    }

    *p_dsl2edit_4w = p_new_dsl2edit_4w;
    return ret;
}

int32
sys_goldengate_nh_update_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w,
                                sys_nh_db_dsl2editeth4w_t** p_update_dsl2edit_4w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    sys_goldengate_nh_master_t* p_master = NULL;

    int32 ret = CTC_E_NONE;


    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_route_l2edit(lchip, p_dsl2edit_4w);

    if (p_new_dsl2edit_4w)
    /*Found node*/
      {

	      /*1.Delete old node */
		  _sys_goldengate_nh_get_nh_master(lchip, &p_master);
          CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit4w_tree, p_dsl2edit_4w));

          /*2.insert new node  & Update Hw table*/
          sal_memcpy(p_new_dsl2edit_4w->mac_da, (*p_update_dsl2edit_4w)->mac_da, sizeof(mac_addr_t));
		  p_new_dsl2edit_4w->output_vid = (*p_update_dsl2edit_4w)->output_vid;
          ret = _sys_goldengate_nh_insert_route_l2edit(lchip, p_new_dsl2edit_4w);
         if(ret != CTC_E_NONE)
         {
            return CTC_E_NO_MEMORY;
         }

        ret = _sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, p_new_dsl2edit_4w,SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 0);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    *p_update_dsl2edit_4w = p_new_dsl2edit_4w;
    return ret;
}

sys_nh_db_dsl2editeth4w_t*
sys_goldengate_nh_lkup_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)  /*DsL2Edit*/
{
    return _sys_goldengate_nh_lkup_route_l2edit(lchip, p_dsl2edit_4w);
}

int32
sys_goldengate_nh_remove_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;

    if (p_dsl2edit_4w->is_ecmp_if)
    {
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_route_l2edit(lchip, p_dsl2edit_4w);

    if (!p_new_dsl2edit_4w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_4w->ref_cnt =  p_new_dsl2edit_4w->ref_cnt ?( p_new_dsl2edit_4w->ref_cnt-1) :0;
    if (p_new_dsl2edit_4w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1, p_new_dsl2edit_4w->offset));

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit4w_tree, p_new_dsl2edit_4w));
    mem_free(p_new_dsl2edit_4w);
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_add_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t** p_dsl2edit_8w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lkup dsl2edit_8w node, mac da:%s , mac sa:%s!!\n",
                   sys_goldengate_output_mac((*p_dsl2edit_8w)->mac_da), sys_goldengate_output_mac((*p_dsl2edit_8w)->mac_sa));

    if ((*p_dsl2edit_8w)->is_ecmp_if)
    {
        sys_goldengate_nh_get_emcp_if_l2edit(lchip, (void**)&p_new_dsl2edit_8w);
        *p_dsl2edit_8w = p_new_dsl2edit_8w;
        return CTC_E_NONE;
    }

    p_new_dsl2edit_8w = _sys_goldengate_nh_lkup_route_l2edit_8w(lchip, (*p_dsl2edit_8w));

    if (p_new_dsl2edit_8w)
    /*Found node*/
    {
        p_new_dsl2edit_8w->ref_cnt++;
        *p_dsl2edit_8w = p_new_dsl2edit_8w;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Found p_new_dsl2edit_8w node, mac da:%s , mac sa:%s!!\n",
                   sys_goldengate_output_mac(p_new_dsl2edit_8w->mac_da), sys_goldengate_output_mac(p_new_dsl2edit_8w->mac_sa));
        return CTC_E_NONE;
    }

    p_new_dsl2edit_8w = (sys_nh_db_dsl2editeth8w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth8w_t));
    if (p_new_dsl2edit_8w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

    /*Insert new one*/
    ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
	    return ret;

    }

    p_new_dsl2edit_8w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_8w->offset = new_offset;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l2edit offset=%d\n", new_offset);


    p_new_dsl2edit_8w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_8w->offset = new_offset;

    sal_memcpy(p_new_dsl2edit_8w->mac_da, (*p_dsl2edit_8w)->mac_da, sizeof(mac_addr_t));
    sal_memcpy(p_new_dsl2edit_8w->mac_sa, (*p_dsl2edit_8w)->mac_sa, sizeof(mac_addr_t));
    p_new_dsl2edit_8w->output_vid = (*p_dsl2edit_8w)->output_vid;
    p_new_dsl2edit_8w->output_vlan_is_svlan = (*p_dsl2edit_8w)->output_vlan_is_svlan;
    p_new_dsl2edit_8w->packet_type          = (*p_dsl2edit_8w)->packet_type;
    ret = _sys_goldengate_nh_write_entry_dsl2editeth8w(lchip, p_new_dsl2edit_8w, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W);
    ret = ret ? ret : _sys_goldengate_nh_insert_route_l2edit_8w(lchip, p_new_dsl2edit_8w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
    }

    *p_dsl2edit_8w = p_new_dsl2edit_8w;
    return ret;
}

sys_nh_db_dsl2editeth8w_t*
sys_goldengate_nh_lkup_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)  /*DsL2Edit*/
{
    return _sys_goldengate_nh_lkup_route_l2edit_8w(lchip, p_dsl2edit_8w);
}


int32
sys_goldengate_nh_remove_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;

    if (p_dsl2edit_8w->is_ecmp_if)
    {
        return CTC_E_NONE;
    }

    p_new_dsl2edit_8w = _sys_goldengate_nh_lkup_route_l2edit_8w(lchip, p_dsl2edit_8w);

    if (!p_new_dsl2edit_8w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_8w->ref_cnt =  p_new_dsl2edit_8w->ref_cnt ?( p_new_dsl2edit_8w->ref_cnt-1) :0;
    if (p_new_dsl2edit_8w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W, 1, p_new_dsl2edit_8w->offset));

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit8w_tree, p_new_dsl2edit_8w));
    mem_free(p_new_dsl2edit_8w);
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_add_loopback_l2edit(uint8 lchip, uint32 nhid, uint8 is_l2edit, uint32* p_edit_ptr)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;
    sys_nh_db_dsl2editeth4w_t sys_nh_db_dsl2editeth4w;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&sys_nh_db_dsl2editeth4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    CTC_SLIST_LOOP(g_gg_nh_master[lchip]->eloop_list, ctc_slistnode)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            *p_edit_ptr = p_eloop_node->edit_ptr;
            p_eloop_node->ref_cnt++;
            return CTC_E_NONE;
        }
    }
    p_eloop_node =  mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_eloop_node_t));
    if (NULL == p_eloop_node)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_eloop_node, 0, sizeof(sys_nh_eloop_node_t));
    p_eloop_node->nhid = nhid;

    CTC_ERROR_GOTO(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_LPBK, 1, &new_offset), ret, Error0);
    sys_nh_db_dsl2editeth4w.nhid = nhid;
    sys_nh_db_dsl2editeth4w.offset = new_offset;
    CTC_ERROR_GOTO(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &sys_nh_db_dsl2editeth4w, SYS_NH_ENTRY_TYPE_L2EDIT_LPBK, 0),ret, Error1);

    p_eloop_node->edit_ptr = new_offset;
    p_eloop_node->ref_cnt++;
    ctc_slist_add_tail(g_gg_nh_master[lchip]->eloop_list, &(p_eloop_node->head));
    *p_edit_ptr = new_offset;
    return CTC_E_NONE;

Error1:
    sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_LPBK, 1, new_offset);
Error0:
    mem_free(p_eloop_node);

    return ret;
}

int32
sys_goldengate_nh_remove_loopback_l2edit(uint8 lchip, uint32 nhid, uint8 is_l2edit)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    ctc_slistnode_t* ctc_slistnode_new = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;

    CTC_SLIST_LOOP_DEL(g_gg_nh_master[lchip]->eloop_list, ctc_slistnode,ctc_slistnode_new)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            p_eloop_node->ref_cnt--;
            if (0 == p_eloop_node->ref_cnt)
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_LPBK, 1, p_eloop_node->edit_ptr));
                ctc_slist_delete_node(g_gg_nh_master[lchip]->eloop_list, &(p_eloop_node->head));
                mem_free(p_eloop_node);
            }
            return CTC_E_NONE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_update_eloop_edit(uint8 lchip, uint32 nhid)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_nh_eloop_node_t *p_eloop_node = NULL;
    sys_nh_db_dsl2editeth4w_t sys_nh_db_dsl2editeth4w;

    sal_memset(&sys_nh_db_dsl2editeth4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));
    CTC_SLIST_LOOP(g_gg_nh_master[lchip]->eloop_list, ctc_slistnode)
    {
        p_eloop_node = _ctc_container_of(ctc_slistnode, sys_nh_eloop_node_t, head);
        if (p_eloop_node->nhid == nhid)
        {
            sys_nh_db_dsl2editeth4w.nhid = nhid;
            sys_nh_db_dsl2editeth4w.offset = p_eloop_node->edit_ptr;
            CTC_ERROR_RETURN(_sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, &sys_nh_db_dsl2editeth4w, SYS_NH_ENTRY_TYPE_L2EDIT_LPBK, 0));
            return CTC_E_NONE;
        }
    }

    return CTC_E_NONE;
}


#define NH_L2EDIT_INNER
int32
sys_goldengate_nh_add_l2edit_6w(uint8 lchip, sys_nh_db_dsl2editeth8w_t** p_dsl2edit_8w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    p_new_dsl2edit_8w = _sys_goldengate_nh_lkup_l2edit_6w(lchip, (*p_dsl2edit_8w));

    if (p_new_dsl2edit_8w)
    /*Found node*/
    {
        p_new_dsl2edit_8w->ref_cnt++;
        *p_dsl2edit_8w = p_new_dsl2edit_8w;

        return CTC_E_NONE;
    }

    p_new_dsl2edit_8w = (sys_nh_db_dsl2editeth8w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth8w_t));
    if (p_new_dsl2edit_8w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_8w, 0, sizeof(sys_nh_db_dsl2editeth8w_t));

    /*Insert new one*/
    ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_6W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
		return ret;
    }

    p_new_dsl2edit_8w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_8w->offset = new_offset;

    sal_memcpy(p_new_dsl2edit_8w->mac_da, (*p_dsl2edit_8w)->mac_da, sizeof(mac_addr_t));
    sal_memcpy(p_new_dsl2edit_8w->mac_sa, (*p_dsl2edit_8w)->mac_sa, sizeof(mac_addr_t));
    p_new_dsl2edit_8w->update_mac_sa = (*p_dsl2edit_8w)->update_mac_sa;
    p_new_dsl2edit_8w->strip_inner_vlan = (*p_dsl2edit_8w)->strip_inner_vlan;
    p_new_dsl2edit_8w->output_vid   = (*p_dsl2edit_8w)->output_vid;
    ret = _sys_goldengate_nh_write_entry_dsl2editeth8w(lchip, p_new_dsl2edit_8w, SYS_NH_ENTRY_TYPE_L2EDIT_6W);
    ret = ret ? ret : _sys_goldengate_nh_insert_l2edit_6w(lchip, p_new_dsl2edit_8w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_8w);
        p_new_dsl2edit_8w = NULL;
    }

    *p_dsl2edit_8w = p_new_dsl2edit_8w;
    return ret;
}

int32
sys_goldengate_nh_remove_l2edit_6w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth8w_t* p_new_dsl2edit_8w = NULL;

    p_new_dsl2edit_8w = _sys_goldengate_nh_lkup_l2edit_6w(lchip, p_dsl2edit_8w);

    if (!p_new_dsl2edit_8w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_8w->ref_cnt--;
    if (p_new_dsl2edit_8w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_6W, 1, p_new_dsl2edit_8w->offset));

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit6w_tree, p_new_dsl2edit_8w));
    mem_free(p_new_dsl2edit_8w);
    return CTC_E_NONE;
}



int32
sys_goldengate_nh_add_l2edit_3w(uint8 lchip, sys_nh_db_dsl2editeth4w_t** p_dsl2edit_4w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lkup dsl2edit_4w node, mac:%s!!\n",
                   sys_goldengate_output_mac((*p_dsl2edit_4w)->mac_da));

    if ((*p_dsl2edit_4w)->is_ecmp_if)
    {
        sys_goldengate_nh_get_emcp_if_l2edit(lchip, (void**)&p_new_dsl2edit_4w);
        *p_dsl2edit_4w = p_new_dsl2edit_4w;
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_l2edit_3w(lchip, (*p_dsl2edit_4w));

    if (p_new_dsl2edit_4w)
    /*Found node*/
    {
        p_new_dsl2edit_4w->ref_cnt++;
        *p_dsl2edit_4w = p_new_dsl2edit_4w;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Found p_new_dsl2edit_4w node, mac:%s!!\n",
            sys_goldengate_output_mac(p_new_dsl2edit_4w->mac_da));
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = (sys_nh_db_dsl2editeth4w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (p_new_dsl2edit_4w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    /*Insert new one*/
    ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
		return ret;
    }

    p_new_dsl2edit_4w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_4w->offset = new_offset;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "l2edit offset=%d\n", new_offset);


	p_new_dsl2edit_4w->output_vid = (*p_dsl2edit_4w)->output_vid;
    sal_memcpy(p_new_dsl2edit_4w->mac_da, (*p_dsl2edit_4w)->mac_da, sizeof(mac_addr_t));
    ret = _sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, p_new_dsl2edit_4w,SYS_NH_ENTRY_TYPE_L2EDIT_3W, 0);
    ret = ret ? ret : _sys_goldengate_nh_insert_l2edit_3w(lchip, p_new_dsl2edit_4w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
    }

    *p_dsl2edit_4w = p_new_dsl2edit_4w;
    return ret;
}


int32
sys_goldengate_nh_remove_l2edit_3w(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_l2edit_3w(lchip, p_dsl2edit_4w);

    if (!p_new_dsl2edit_4w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_4w->ref_cnt--;
    if (p_new_dsl2edit_4w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_3W, 1, p_new_dsl2edit_4w->offset));

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit3w_tree, p_new_dsl2edit_4w));
    mem_free(p_new_dsl2edit_4w);
    return CTC_E_NONE;
}


int32
sys_goldengate_nh_add_swap_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t** p_dsl2edit_4w)  /*DsL2Edit*/
{
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;
    int32 ret = CTC_E_NONE;
    uint32 new_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lkup dsl2edit_4w node, mac:%s!!\n",
                   sys_goldengate_output_mac((*p_dsl2edit_4w)->mac_da));

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_swap_l2edit(lchip, (*p_dsl2edit_4w));

    if (p_new_dsl2edit_4w)
    /*Found node*/
    {
        p_new_dsl2edit_4w->ref_cnt++;
        *p_dsl2edit_4w = p_new_dsl2edit_4w;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Found p_new_dsl2edit_4w node, mac:%s!!\n",
            sys_goldengate_output_mac(p_new_dsl2edit_4w->mac_da));
        return CTC_E_NONE;
    }

    p_new_dsl2edit_4w = (sys_nh_db_dsl2editeth4w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (p_new_dsl2edit_4w == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    /*Insert new one*/
    ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP, 1, &new_offset);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
		return ret;
    }

    p_new_dsl2edit_4w->ref_cnt = 1; /*New create node*/
    p_new_dsl2edit_4w->offset = new_offset;

    p_new_dsl2edit_4w->strip_inner_vlan = (*p_dsl2edit_4w)->strip_inner_vlan;
    sal_memcpy(p_new_dsl2edit_4w->mac_da, (*p_dsl2edit_4w)->mac_da, sizeof(mac_addr_t));
    ret = _sys_goldengate_nh_write_entry_dsl2editeth4w(lchip, p_new_dsl2edit_4w,SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP, 0);
    ret = ret ? ret : _sys_goldengate_nh_insert_swap_l2edit(lchip, p_new_dsl2edit_4w);
    if (ret != CTC_E_NONE)
    {
        mem_free(p_new_dsl2edit_4w);
        p_new_dsl2edit_4w = NULL;
    }

    *p_dsl2edit_4w = p_new_dsl2edit_4w;
    return ret;
}

int32
sys_goldengate_nh_remove_swap_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;

    p_new_dsl2edit_4w = _sys_goldengate_nh_lkup_swap_l2edit(lchip, p_dsl2edit_4w);

    if (!p_new_dsl2edit_4w)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_new_dsl2edit_4w->ref_cnt--;
    if (p_new_dsl2edit_4w->ref_cnt > 0)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip,
                                                   SYS_NH_ENTRY_TYPE_L2EDIT_INNER_SWAP, 1, p_new_dsl2edit_4w->offset));

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    CTC_ERROR_RETURN(ctc_avl_remove(p_master->dsl2edit4w_swap_tree, p_new_dsl2edit_4w));
    mem_free(p_new_dsl2edit_4w);
    return CTC_E_NONE;
}
#define NH_MPLS_TUNNEL
int32
sys_goldengate_nh_lkup_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t** pp_mpls_tunnel)
{

    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (tunnel_id >= p_master->max_tunnel_id)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *pp_mpls_tunnel = ctc_vector_get(p_master->tunnel_id_vec, tunnel_id);

    return CTC_E_NONE;

}

/**
 @brief Insert node into AVL tree
 */
int32
sys_goldengate_nh_add_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t* p_mpls_tunnel)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_add(p_master->tunnel_id_vec, tunnel_id, p_mpls_tunnel))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_goldengate_nh_remove_mpls_tunnel(uint8 lchip, uint16 tunnel_id)
{

    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_del(p_master->tunnel_id_vec, tunnel_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_goldengate_nh_check_mpls_tunnel_exp(uint8 lchip, uint16 tunnel_id, uint8* exp_type)
{
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
    DsL3EditMpls3W_m dsmpls;
    sys_dsmpls_t sys_mpls;

    uint8 table_type = 0;
    uint8 i = 0;
    uint8 j = 0;

    sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel);
    if (NULL == p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
    }

    /*check lsp*/
    table_type = SYS_NH_ENTRY_TYPE_L3EDIT_MPLS;

    for (i = 0; i < 2; i++)
    {

        if (p_mpls_tunnel->lsp_offset[i])
        {
            sal_memset(&dsmpls, 0, sizeof(DsL3EditMpls3W_m));
            sal_memset(&sys_mpls, 0, sizeof(sys_dsmpls_t));
            sys_goldengate_nh_get_asic_table(lchip,
                                     table_type, p_mpls_tunnel->lsp_offset[i], &dsmpls);


            sys_mpls.derive_exp = GetDsL3EditMpls3W(V, deriveExp_f,    &dsmpls);
            sys_mpls.exp = GetDsL3EditMpls3W(V, exp_f,          &dsmpls);

            if((0 == sys_mpls.exp)
                && (1 == sys_mpls.derive_exp))
            {
                *exp_type = 1;
                return CTC_E_NONE;
            }
        }
    }

    /*check spme*/
    table_type = SYS_NH_ENTRY_TYPE_L3EDIT_SPME;
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 2; j++)
        {
            if(p_mpls_tunnel->spme_offset[i][j])
            {
                sal_memset(&dsmpls, 0, sizeof(DsL3EditMpls3W_m));
                sal_memset(&sys_mpls, 0, sizeof(sys_dsmpls_t));
                sys_goldengate_nh_get_asic_table(lchip,
                                         table_type, p_mpls_tunnel->spme_offset[i][j], &dsmpls);

                sys_mpls.derive_exp = GetDsL3EditMpls3W(V, deriveExp_f,    &dsmpls);
                sys_mpls.exp = GetDsL3EditMpls3W(V, exp_f,          &dsmpls);

                if((0 == sys_mpls.exp)
                    && (1 == sys_mpls.derive_exp))
                {
                    *exp_type = 1;
                    return CTC_E_NONE;
                }
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_check_mpls_tunnel_is_spme(uint8 lchip, uint16 tunnel_id, uint8* spme_mode)
{
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;

    sys_goldengate_nh_lkup_mpls_tunnel(lchip, tunnel_id, &p_mpls_tunnel);
    if (NULL == p_mpls_tunnel)
    {
        return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
    }
    if (CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_IS_SPME))
    {
        *spme_mode = 1;
    }
    else
    {
        *spme_mode = 0;
    }
    return CTC_E_NONE;
}

#define ARP_ID

uint32
sys_goldengate_nh_get_arp_num(uint8 lchip)
{
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    _sys_goldengate_nh_get_nh_master(lchip, &p_master);
    return p_master->arp_id_vec->used_cnt;
}

int32
sys_goldengate_nh_lkup_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t** pp_arp)
{
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (arp_id >= SYS_NH_ARP_ID_DEFAUL_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *pp_arp = ctc_vector_get(p_master->arp_id_vec, arp_id);
    if (NULL == *pp_arp)
    {
        return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}


int32
sys_goldengate_nh_add_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t* p_arp)
{
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_add(p_master->arp_id_vec, arp_id, p_arp))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}

int32
sys_goldengate_nh_remove_arp_id(uint8 lchip, uint16 arp_id)
{

    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (FALSE == ctc_vector_del(p_master->arp_id_vec, arp_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return ret;
}


#define NH_MISC
int32
sys_goldengate_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    /* the max ecmp num should be times of 4 */
    SYS_GOLDENGATE_NH_MAX_ECMP_CHECK(max_ecmp);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (p_nh_master->cur_ecmp_cnt != 0)
    {
        return CTC_E_NH_ECMP_IN_USE;
    }

    p_nh_master->max_ecmp = max_ecmp;

    p_nh_master->max_ecmp_group_num = (0 == max_ecmp) ? SYS_NH_ECMP_GROUP_ID_NUM : SYS_NH_ECMP_MEMBER_NUM / max_ecmp;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp)
{

    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *max_ecmp = p_nh_master->max_ecmp;
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_max_ecmp_group_num(uint8 lchip, uint16* max_group_num)
{

    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *max_group_num = p_nh_master->max_ecmp_group_num;
    return CTC_E_NONE;
}

uint32
sys_goldengate_nh_get_dsmet_base(uint8 lchip)
{
    if (g_gg_nh_master[lchip])
    {
        return g_gg_nh_master[lchip]->dsmet_base;
    }
    else
    {
        return 0;
    }
}

int32
sys_goldengate_nh_get_current_ecmp_group_num(uint8 lchip, uint16* cur_group_num)
{

    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *cur_group_num = p_nh_master->cur_ecmp_cnt;
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_dsfwd_mode(uint8 lchip, uint8* merge_dsfwd)
{

    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *merge_dsfwd = p_nh_master->no_dsfwd;
    return CTC_E_NONE;
}
int32
sys_goldengate_nh_set_dsfwd_mode(uint8 lchip, uint8 merge_dsfwd)
{

    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

     p_nh_master->no_dsfwd =  merge_dsfwd;
    return CTC_E_NONE;
}
int32
sys_goldengate_nh_get_mirror_info_by_nhid(uint8 lchip, uint32 nhid, uint32* dsnh_offset, uint32* gport, bool enable)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint8  gchip = 0;

    CTC_PTR_VALID_CHECK(dsnh_offset);
    CTC_PTR_VALID_CHECK(gport);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    CTC_ERROR_RETURN(_sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo));

    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_RSPAN:
       {
           *gport = 0xFFFF;
           *dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
       }
        break;

    case SYS_NH_TYPE_MCAST:
        {
            uint8  idx = 0;
            uint16  mcast_mirror_port = 0;
            static uint8 in_use = 0;
            static uint32 mcast_mirror_nhid = 0;
            sys_nh_info_mcast_t* p_nhinfo_mcast = (sys_nh_info_mcast_t*)p_nhinfo;

            if (enable && in_use && (mcast_mirror_nhid != nhid))
            {
                return CTC_E_NH_MCAST_MIRROR_NH_IN_USE;
            }

            if (enable)
            {
                p_nhinfo_mcast->mirror_ref_cnt++;
                in_use = 1;
            }
            else
            {
                if (p_nhinfo_mcast->mirror_ref_cnt)
                {
                    p_nhinfo_mcast->mirror_ref_cnt--;
                }

                in_use = (p_nhinfo_mcast->mirror_ref_cnt != 0);
            }

            mcast_mirror_nhid = nhid;

            sys_goldengate_internal_port_get_rsv_port(lchip, SYS_INTERNAL_PORT_TYPE_MIRROR,  &mcast_mirror_port);

            for (idx = 0; idx < 2; idx++)
            {
                CTC_ERROR_RETURN(sys_goldengate_port_set_mirror_en(lchip, (SYS_CHIP_PER_SLICE_PORT_NUM * idx + mcast_mirror_port),
                    p_nhinfo->hdr.dsfwd_info.dsfwd_offset));
            }

            sys_goldengate_get_gchip_id(lchip, &gchip);
            *gport  = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RSV_PORT_ILOOP_ID);
            *dsnh_offset = SYS_NH_ENCODE_ILOOP_DSNH(mcast_mirror_port, 0, 0, 0, 0);
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel = (sys_nh_info_ip_tunnel_t*)p_nhinfo;

            *gport = p_ip_tunnel->gport;
             *dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;

             if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
             {
                 CTC_BIT_SET(*dsnh_offset, 31);
             }

        }
        break;

    case  SYS_NH_TYPE_TOCPU:
        sys_goldengate_get_gchip_id(lchip, &gchip);
        *gport = CTC_GPORT_RCPU(gchip);
        *dsnh_offset = SYS_DSNH_INDEX_FOR_NONE;
        break;

    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_spec_info = (sys_nh_info_special_t*)p_nhinfo;

            *gport = p_nh_spec_info->dest_gport;
            *dsnh_offset = p_nh_spec_info->hdr.dsfwd_info.dsnh_offset;
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_misc_info = (sys_nh_info_misc_t*)p_nhinfo;
            *gport = p_nh_misc_info->gport;
            *dsnh_offset = p_nh_misc_info->hdr.dsfwd_info.dsnh_offset;

			if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_BIT_SET(*dsnh_offset, 31);
            }

        }
        break;

    case  SYS_NH_TYPE_ECMP:
        {
           sys_nh_info_ecmp_t* p_ecmp = (sys_nh_info_ecmp_t*)p_nhinfo;
           *gport = p_ecmp->gport;
           *dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;
           CTC_BIT_SET(*dsnh_offset, 31);
         }
         break;
    default:
        return CTC_E_INVALID_PARAM;

    }

    return CTC_E_NONE;
}


/**
 @brief This function is used to get l3ifid

 @param[in] nhid, nexthop id

 @param[out] p_l3ifid, l3 interface id

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_get_l3ifid(uint8 lchip, uint32 nhid, uint16* p_l3ifid)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    sys_nh_info_ipuc_t* p_ipuc_info;

    CTC_PTR_VALID_CHECK(p_l3ifid);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (nhid == SYS_NH_RESOLVED_NHID_FOR_DROP ||
        nhid == SYS_NH_RESOLVED_NHID_FOR_TOCPU)
    {
        *p_l3ifid = SYS_L3IF_INVALID_L3IF_ID;
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_LOCK(p_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo),
        p_nh_master->p_mutex);

    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_IPUC:
        p_ipuc_info = (sys_nh_info_ipuc_t*)(p_nhinfo);
        *p_l3ifid = p_ipuc_info->l3ifid;
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_mpls_info;
            p_mpls_info = (sys_nh_info_mpls_t*)(p_nhinfo);
            *p_l3ifid = p_mpls_info->l3ifid;
            break;
        }

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel_info;
            p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)(p_nhinfo);
            *p_l3ifid = p_ip_tunnel_info->l3ifid;
            break;
        }
    case SYS_NH_TYPE_MISC:
        {

            sys_nh_info_misc_t* p_misc_info;
            p_misc_info = (sys_nh_info_misc_t*)(p_nhinfo);
            *p_l3ifid = p_misc_info->l3ifid;
            break;
        }
    default:
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NH_INVALID_NH_TYPE;
    }

    SYS_NH_UNLOCK(p_nh_master->p_mutex);

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_max_external_nhid(uint8 lchip, uint32* nhid)
{
	sys_goldengate_nh_master_t* p_nh_master = NULL;

	SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
	CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    *nhid = p_nh_master->max_external_nhid;
    return CTC_E_NONE;
}


int32
sys_goldengate_nh_set_pkt_nh_edit_mode(uint8 lchip, uint8 edit_mode)
{

	sys_goldengate_nh_master_t* p_nh_master = NULL;

	CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    p_nh_master->pkt_nh_edit_mode  =  edit_mode ;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_set_ip_use_l2edit(uint8 lchip, uint8 enable)
{

	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    p_nh_master->ip_use_l2edit = (enable) ? 1 : 0;

    return CTC_E_NONE;
}
int32
sys_goldengate_nh_get_ip_use_l2edit(uint8 lchip, uint8* enable)
{

	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    *enable = p_nh_master->ip_use_l2edit;
    return CTC_E_NONE;
}
int32
sys_goldengate_nh_set_reflective_brg_en(uint8 lchip, uint8 enable)
{
	sys_goldengate_nh_master_t* p_nh_master = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    p_nh_master->reflective_brg_en = enable ?1:0;
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_reflective_brg_en(uint8 lchip, uint8 *enable)
{
	sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
     *enable = p_nh_master->reflective_brg_en;
  return CTC_E_NONE;
}


int32
sys_goldengate_nh_get_mcast_nh(uint8 lchip, uint32 group_id, uint32* nhid)
{
    sys_nh_info_mcast_t* p_mcast_db = NULL;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_PTR_VALID_CHECK(nhid);


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    p_mcast_db =  ctc_vector_get(p_nh_master->mcast_group_vec, group_id);
    if (NULL == p_mcast_db)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    *nhid = p_mcast_db->nhid;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_add_mcast_db(uint8 lchip, uint32 group_id, void* data)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (FALSE == ctc_vector_add(p_nh_master->mcast_group_vec, group_id, data))
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_del_mcast_db(uint8 lchip, uint32 group_id)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (FALSE == ctc_vector_del(p_nh_master->mcast_group_vec, group_id))
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_traverse_mcast_db(uint8 lchip, vector_traversal_fn2 fn, void* data)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    ctc_vector_traverse2(p_nh_master->mcast_group_vec, 0, fn, data);

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_mcast_member(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info)
{
    sys_nh_info_com_t* p_com_db = NULL;
    sys_nh_info_mcast_t* p_mcast_db = NULL;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_LOCK(p_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_com_db),
        p_nh_master->p_mutex);

    p_mcast_db = (sys_nh_info_mcast_t*)p_com_db;

    CTC_ERROR_RETURN_WITH_UNLOCK(
        sys_goldengate_nh_get_mcast_member_info(lchip, p_mcast_db, p_nh_info),
        p_nh_master->p_mutex);
    SYS_NH_UNLOCK(p_nh_master->p_mutex);

    return CTC_E_NONE;
}


#define _________api___________

int32
sys_goldengate_nh_get_port(uint8 lchip, uint32 nhid, uint8* aps_en, uint32* gport)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    CTC_ERROR_RETURN(_sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo));

    *aps_en = 0;

    switch (p_nhinfo->hdr.nh_entry_type)
    {
    case SYS_NH_TYPE_BRGUC:
        {
            sys_nh_info_brguc_t* p_brguc_info;
            p_brguc_info = (sys_nh_info_brguc_t*)p_nhinfo;
            if ((p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC || p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT
                 || p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS))
            {
                *gport = p_brguc_info->dest_gport;
            }
            else if (p_brguc_info->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
            {
                *gport = p_brguc_info->dest_gport;
                *aps_en = 1;

            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        break;

    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_info_ipuc_t* p_ipuc_info;
            p_ipuc_info = (sys_nh_info_ipuc_t*)(p_nhinfo);
            if (CTC_FLAG_ISSET(p_ipuc_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
            }
            else
            {
                *gport = p_ipuc_info->gport;
            }

            *aps_en = FALSE;
        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_info_mpls_t* p_mpls_info;
            p_mpls_info = (sys_nh_info_mpls_t*)(p_nhinfo);

            if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
                *aps_en = 0;
            }
            else if (CTC_FLAG_ISSET(p_mpls_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
            {
                *gport = p_mpls_info->aps_group_id;
                *aps_en = 1;
            }
            else
            {
                *gport = p_mpls_info->gport;
                *aps_en = 0;
            }
        }
        break;

    case SYS_NH_TYPE_MCAST:
        {
            *gport = CTC_MAX_UINT16_VALUE;
            *aps_en = 0;
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_info_misc_t* p_nh_info;
            p_nh_info = (sys_nh_info_misc_t*)(p_nhinfo);
            *gport = p_nh_info->gport;
        }
        break;

    case  SYS_NH_TYPE_DROP:
        {
            *gport = CTC_MAX_UINT16_VALUE;
            *aps_en = 0;
        }
        break;

    case  SYS_NH_TYPE_TOCPU:
        {
            sys_goldengate_get_gchip_id(lchip, &gchip);
            *gport = CTC_GPORT_RCPU(gchip);
            *aps_en = 0;
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_info_ip_tunnel_t* p_ip_tunnel_info;
            p_ip_tunnel_info = (sys_nh_info_ip_tunnel_t*)(p_nhinfo);
            if (CTC_FLAG_ISSET(p_ip_tunnel_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
            }
            else
            {
                *gport = p_ip_tunnel_info->gport;
            }

            *aps_en = FALSE;
        }
        break;
    case SYS_NH_TYPE_TRILL:
        {
            sys_nh_info_trill_t* p_trill_info;
            p_trill_info = (sys_nh_info_trill_t*)(p_nhinfo);
            if (CTC_FLAG_ISSET(p_trill_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
            {
                *gport = CTC_MAX_UINT16_VALUE;
            }
            else
            {
                *gport = p_trill_info->gport;
            }
            *aps_en = FALSE;
        }
        break;
    case SYS_NH_TYPE_ILOOP:
        {
            sys_nh_info_special_t* p_nh_info;
            p_nh_info = (sys_nh_info_special_t*)(p_nhinfo);
            *gport = p_nh_info->dest_gport;
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

bool
sys_goldengate_nh_is_cpu_nhid(uint8 lchip, uint32 nhid)
{
    uint8 is_cpu_nh = 0;
    int32 ret = 0;

    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_misc_t* p_nh_misc_info = NULL;

    if (nhid == SYS_NH_RESOLVED_NHID_FOR_TOCPU)
    {
        is_cpu_nh = 1;
    }

    ret = sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nhid, &p_nhinfo);
    if (ret < 0)
    {
        return FALSE;
    }
    if (p_nhinfo->hdr.nh_entry_type == SYS_NH_TYPE_MISC)
    {
        p_nh_misc_info = (sys_nh_info_misc_t*)p_nhinfo;
        if (NULL == p_nh_misc_info)
        {
            return FALSE;
        }
        if (p_nh_misc_info->misc_nh_type == CTC_MISC_NH_TYPE_TO_CPU)
        {
            is_cpu_nh = 1;
        }
    }
    return (is_cpu_nh ? TRUE : FALSE);
}

/**
 @brief This function is used to allocate a new internal nexthop id
 */
STATIC int32
_sys_goldengate_nh_new_nh_id(uint8 lchip, uint32* p_nhid)
{
    sys_goldengate_opf_t opf;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_nhid);
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = OPF_NH_NHID_INTERNAL;
    opf.multiple  = 1;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, p_nhid));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "New allocated nhid = %d\n", *p_nhid);

    return CTC_E_NONE;
}

int32
_sys_goldengate_nh_info_free(uint8 lchip, uint32 nhid)
{
    sys_goldengate_opf_t opf;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Need freed nhid = %d\n", nhid);
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    if ((nhid >= p_nh_master->max_external_nhid) &&
        (nhid < SYS_NH_INTERNAL_NHID_MAX))
    {
        opf.pool_type = OPF_NH_DYN_SRAM;
        opf.pool_index = OPF_NH_NHID_INTERNAL;
        CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, nhid));
        p_nhinfo = (sys_nh_info_com_t*)ctc_vector_del(p_nh_master->internal_nhid_vec,
                                                      (nhid - p_nh_master->max_external_nhid));
    }
    else if (nhid < p_nh_master->max_external_nhid)
    {
        p_nhinfo = (sys_nh_info_com_t*)ctc_vector_del(p_nh_master->external_nhid_vec, nhid);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }
    mem_free(p_nhinfo);
    return CTC_E_NONE;
}


int32
_sys_goldengate_nh_api_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_goldengate_nh_master_t* p_nh_master,
                                         sys_nh_info_com_t** pp_nhinfo)
{
    sys_nh_info_com_t* p_nh_info;

    if (nhid < p_nh_master->max_external_nhid)
    {
        p_nh_info = ctc_vector_get(p_nh_master->external_nhid_vec, nhid);
    }
    else if (nhid < SYS_NH_INTERNAL_NHID_MAX)
    {
        p_nh_info = ctc_vector_get(p_nh_master->internal_nhid_vec,
                                   SYS_NH_GET_VECTOR_INDEX_BY_NHID(nhid));
    }
    else
    {
        return CTC_E_NH_INVALID_NHID;
    }

    if (NULL == p_nh_info)
    {
        return CTC_E_NH_NOT_EXIST;
    }

    *pp_nhinfo = p_nh_info;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_nh_info_com_t** pp_nhinfo)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    sys_nh_info_com_t* p_nhinfo = NULL;

    CTC_PTR_VALID_CHECK(pp_nhinfo);
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    CTC_ERROR_RETURN(_sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo));

    *pp_nhinfo = p_nhinfo;

    return CTC_E_NONE;
}

uint8
sys_goldengate_nh_get_nh_valid(uint8 lchip, uint32 nhid)
{
    int32 ret = CTC_E_NONE;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    ret = _sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo);
    if (ret || (NULL == p_nhinfo))
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

#if 0
STATIC int32
_sys_goldengate_nh_db_get_node_size_by_type(uint8 lchip,
    sys_nh_entry_table_type_t entry_type, uint32* p_size)
{
    sys_goldengate_nh_master_t* p_master;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Entry type = %d\n", entry_type);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    switch (entry_type)
    {
    case SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W:
        *p_size = sizeof(sys_nh_db_dsl2editeth4w_t);
        break;

    case SYS_NH_ENTRY_TYPE_NEXTHOP_4W:
    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}
#endif

/**
 @brief Get nh db entry size
 */
STATIC int32
_sys_goldengate_nh_get_nhentry_size_by_nhtype(uint8 lchip,
    sys_goldengate_nh_type_t nh_type, uint32* p_size)
{

    switch (nh_type)
    {
    case SYS_NH_TYPE_BRGUC:
        *p_size = sizeof(sys_nh_info_brguc_t);
        break;

    case SYS_NH_TYPE_MCAST:
        *p_size = sizeof(sys_nh_info_mcast_t);
        break;

    case SYS_NH_TYPE_IPUC:
        *p_size = sizeof(sys_nh_info_ipuc_t);
        break;

    case SYS_NH_TYPE_ECMP:
        *p_size = sizeof(sys_nh_info_ecmp_t);
        break;

    case SYS_NH_TYPE_MPLS:
        *p_size = sizeof(sys_nh_info_mpls_t);
        break;

    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
        *p_size = sizeof(sys_nh_info_special_t);
        break;

    case SYS_NH_TYPE_RSPAN:
        *p_size = sizeof(sys_nh_info_rspan_t);
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        *p_size = sizeof(sys_nh_info_ip_tunnel_t);
        break;

    case SYS_NH_TYPE_TRILL:
        *p_size = sizeof(sys_nh_info_trill_t);
        break;

    case SYS_NH_TYPE_MISC:
        *p_size = sizeof(sys_nh_info_misc_t);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_nh_api_create(uint8 lchip, sys_nh_param_com_t* p_nh_com_para)
{

    sys_goldengate_nh_type_t nh_type;
    p_sys_nh_create_cb_t nh_sem_map_cb;
    sys_nh_info_com_t* p_nhinfo = NULL;
    int32 ret = CTC_E_NONE;
    uint32 tmp_nh_id, db_entry_size = 0;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /*1. Sanity check and init*/
    if (NULL == p_nh_com_para)
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_LOCK(p_nh_master->p_mutex);

    if (!p_nh_com_para->hdr.is_internal_nh
        && (p_nh_com_para->hdr.nh_param_type != SYS_NH_TYPE_DROP)
        && (p_nh_com_para->hdr.nh_param_type != SYS_NH_TYPE_TOCPU))
    {
        if (p_nh_com_para->hdr.nhid < SYS_NH_RESOLVED_NHID_MAX ||
            p_nh_com_para->hdr.nhid >= (p_nh_master->max_external_nhid))
        {
            SYS_NH_UNLOCK(p_nh_master->p_mutex);
            return CTC_E_NH_INVALID_NHID;
        }
    }

    /*Check If this nexthop is exist*/
    if ((!(p_nh_com_para->hdr.is_internal_nh)) &&
        (NULL != ctc_vector_get(p_nh_master->external_nhid_vec, p_nh_com_para->hdr.nhid)))
    {
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NH_EXIST;
    }

    nh_type = p_nh_com_para->hdr.nh_param_type;
    ret = _sys_goldengate_nh_get_nhentry_size_by_nhtype(lchip, nh_type, &db_entry_size);
    if (ret)
    {
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NH_INVALID_NH_TYPE;
    }

    p_nhinfo = mem_malloc(MEM_NEXTHOP_MODULE, db_entry_size);
    if (NULL == p_nhinfo)
    {
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NO_MEMORY;
    }
    else
    {
        sal_memset(p_nhinfo, 0, db_entry_size);
    }

    ret = _sys_goldengate_nh_alloc_offset_by_nhtype(lchip, p_nh_com_para, p_nhinfo);
    if (ret)
    {

        mem_free(p_nhinfo);
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return ret;
    }

    /*2. Semantic mapping Callback*/
    nh_sem_map_cb = p_nh_master->callbacks_nh_create[nh_type];
    ret = (* nh_sem_map_cb)(lchip, p_nh_com_para, p_nhinfo);
    if (ret)
    {
        _sys_goldengate_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);

        mem_free(p_nhinfo);
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return ret;
    }

    /*3. Store nh infor into nh_array*/
    if (p_nh_com_para->hdr.is_internal_nh)
    {
        /*alloc a new nhid*/
        ret = ret ? ret : _sys_goldengate_nh_new_nh_id(lchip, &tmp_nh_id);
        if (FALSE == ctc_vector_add(p_nh_master->internal_nhid_vec,
                                    (tmp_nh_id - p_nh_master->max_external_nhid), p_nhinfo))
        {
            _sys_goldengate_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);
             mem_free(p_nhinfo);
            SYS_NH_UNLOCK(p_nh_master->p_mutex);
            return CTC_E_NO_MEMORY;
        }

        p_nh_com_para->hdr.nhid = tmp_nh_id; /*Write back the nhid*/
        if ( nh_type == SYS_NH_TYPE_MCAST)
        {
            sys_nh_info_mcast_t* p_mcast_nhinfo = (sys_nh_info_mcast_t*)p_nhinfo;
            p_mcast_nhinfo->nhid = tmp_nh_id;
        }

    }
    else
    {
        if (FALSE == ctc_vector_add(p_nh_master->external_nhid_vec,
                                    (p_nh_com_para->hdr.nhid), p_nhinfo))
        {
            _sys_goldengate_nh_free_offset_by_nhinfo(lchip, nh_type, p_nhinfo);

            mem_free(p_nhinfo);
            SYS_NH_UNLOCK(p_nh_master->p_mutex);
            return CTC_E_NO_MEMORY;
        }
    }

    SYS_NH_UNLOCK(p_nh_master->p_mutex);
    if (ret == CTC_E_NONE && nh_type == SYS_NH_TYPE_ECMP)
    {
        p_nh_master->cur_ecmp_cnt++;
    }
    p_nh_master->nhid_used_cnt[nh_type]++;
    return ret;
}

int32
sys_goldengate_nh_api_delete(uint8 lchip, uint32 nhid, sys_goldengate_nh_type_t nhid_type)
{
    p_sys_nh_delete_cb_t nh_del_cb;
    sys_goldengate_nh_type_t nh_type;
    sys_nh_info_com_t* p_nh_info = NULL;
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_LOCK(p_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nh_info),
        p_nh_master->p_mutex);

    if ((nhid_type != p_nh_info->hdr.nh_entry_type))
    {
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NH_NHID_NOT_MATCH_NHTYPE;
    }

    nh_type = p_nh_info->hdr.nh_entry_type;

    nh_del_cb = p_nh_master->callbacks_nh_delete[nh_type];

    ret = (* nh_del_cb)(lchip, p_nh_info, & nhid);

    ret = ret ? ret : _sys_goldengate_nh_free_offset_by_nhinfo(lchip, nh_type, p_nh_info);

    ret = ret ? ret : _sys_goldengate_nh_info_free(lchip, nhid);

    SYS_NH_UNLOCK(p_nh_master->p_mutex);
    if (ret == CTC_E_NONE && nh_type == SYS_NH_TYPE_ECMP)
    {
        p_nh_master->cur_ecmp_cnt = (p_nh_master->cur_ecmp_cnt > 0) ? (p_nh_master->cur_ecmp_cnt - 1) : p_nh_master->cur_ecmp_cnt;
    }
    if(p_nh_master->nhid_used_cnt[nh_type])
    {
      p_nh_master->nhid_used_cnt[nh_type]--;
    }
    return ret;
}


int32
sys_goldengate_nh_api_update(uint8 lchip, uint32 nhid, sys_nh_param_com_t* p_nh_com_para)
{
    p_sys_nh_update_cb_t nh_update_cb;
    sys_goldengate_nh_type_t nh_type;
    sys_nh_info_com_t* p_nh_info;
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    SYS_NH_LOCK(p_nh_master->p_mutex);

    CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nh_info),
        p_nh_master->p_mutex);

    nh_type = p_nh_com_para->hdr.nh_param_type;
    if ((nh_type != p_nh_info->hdr.nh_entry_type))
    {
        SYS_NH_UNLOCK(p_nh_master->p_mutex);
        return CTC_E_NH_NHID_NOT_MATCH_NHTYPE;
    }
    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV))
    {
        if(p_nh_com_para->hdr.change_type == SYS_NH_CHANGE_TYPE_FWD_TO_UNROV)
        {
            SYS_NH_UNLOCK(p_nh_master->p_mutex);
            return CTC_E_NONE;
        }
    }
    else
    {
        if(p_nh_com_para->hdr.change_type == SYS_NH_CHANGE_TYPE_UNROV_TO_FWD)
        {
           p_nh_com_para->hdr.change_type = SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR;
        }
    }
    nh_update_cb = p_nh_master->callbacks_nh_update[nh_type];
    if(nh_update_cb)
    {
       ret = (* nh_update_cb)(lchip, p_nh_info, p_nh_com_para);
    }

    SYS_NH_UNLOCK(p_nh_master->p_mutex);

    return ret;
}



#define _________offset__________

int32
sys_goldengate_nh_get_dsnh_offset_by_nhid(uint8 lchip, uint32 nhid, uint32 *p_dsnh_offset, uint8* p_use_dsnh8w)
{
    sys_nh_info_com_t* p_nhinfo;
    sys_goldengate_nh_master_t* p_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    CTC_ERROR_RETURN(_sys_goldengate_nh_api_get_nhinfo_by_nhid(lchip, nhid, p_nh_master, &p_nhinfo));


    *p_dsnh_offset = p_nhinfo->hdr.dsfwd_info.dsnh_offset;  /* modified */


    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        *p_use_dsnh8w = TRUE;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_resolved_offset(uint8 lchip, sys_goldengate_nh_res_offset_type_t type, uint32* p_offset)
{
    sys_nh_offset_attr_t* p_res_offset;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    /*Always get chip 0's offset, all chip should be same,
    maybe we don't need to store this value per-chip*/
    p_res_offset = &(p_nh_master->sys_nh_resolved_offset[type]);
    *p_offset = p_res_offset->offset_base;
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Type = %d, resolved offset = 0x%x\n", type, *p_offset);

    return CTC_E_NONE;
}

/**
 @brief This function is used to alloc dynamic offset

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_offset_alloc(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                              uint32 entry_num, uint32* p_offset)
{
    sys_goldengate_opf_t opf;
    uint32 entry_size;
    int32 ret = CTC_E_NONE;
    sys_goldengate_nh_master_t* p_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *p_offset = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_offset);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = p_nh_master->nh_table_info_array[entry_type].opf_pool_type + OPF_NH_DYN1_SRAM;
    entry_size = p_nh_master->nh_table_info_array[entry_type].entry_size;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.multiple = entry_size;
    entry_size = entry_size * entry_num;
    opf.reverse = (p_nh_master->nh_table_info_array[entry_type].alloc_dir == 1) ? 1 : 0;
    ret = (sys_goldengate_opf_alloc_offset(lchip, &opf, entry_size, p_offset));
    if (g_gg_nh_master[lchip]->nexthop_share && (CTC_E_NO_RESOURCE == ret)
        &&((SYS_NH_ENTRY_TYPE_NEXTHOP_4W == entry_type)||(SYS_NH_ENTRY_TYPE_NEXTHOP_8W == entry_type)))
    {
        opf.reverse = opf.reverse? 0 : 1;
        CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, entry_size, p_offset));
    }
    else if(ret)
    {
        return ret;
    }

    if ((SYS_NH_ENTRY_TYPE_MET == entry_type) || (SYS_NH_ENTRY_TYPE_MET_6W == entry_type))
    {
        if (*p_offset >= (64*1024 + sys_goldengate_nh_get_dsmet_base(lchip)))
        {
            CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, entry_size, *p_offset));
            return CTC_E_NO_RESOURCE;
        }
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, offset = 0x%x\n",
                   lchip, entry_type, entry_num, *p_offset);
    p_nh_master->nhtbl_used_cnt[entry_type] += entry_size;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_offset_alloc_with_multiple(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                            uint32 entry_num, uint16 multi, uint32* p_offset)
{
    sys_goldengate_opf_t opf;
    uint32 entry_size;
	sys_goldengate_nh_master_t* p_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_offset);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    entry_size = p_nh_master->nh_table_info_array[entry_type].entry_size;
    opf.pool_index = p_nh_master->nh_table_info_array[entry_type].opf_pool_type + OPF_NH_DYN1_SRAM;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.multiple = multi;
    opf.reverse = (p_nh_master->nh_table_info_array[entry_type].alloc_dir == 1) ? 1 : 0;
    entry_size = entry_size * entry_num;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, entry_size, p_offset));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, multi = %d, offset = 0x%x\n",
                   lchip, entry_type, entry_num, multi, *p_offset);
    p_nh_master->nhtbl_used_cnt[entry_type] += entry_size;
    return CTC_E_NONE;
}


int32
sys_goldengate_nh_offset_alloc_from_position(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                            uint32 entry_num, uint32 start_offset)
{
    sys_goldengate_opf_t opf;
    uint32 entry_size;
	sys_goldengate_nh_master_t* p_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = p_nh_master->nh_table_info_array[entry_type].opf_pool_type + OPF_NH_DYN1_SRAM;
    entry_size = p_nh_master->nh_table_info_array[entry_type].entry_size;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.multiple = entry_size;
    entry_size = entry_size * entry_num;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf,  entry_size, start_offset));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, offset = 0x%x\n",
                   lchip, entry_type, entry_num, start_offset);

    p_nh_master->nhtbl_used_cnt[entry_type] += entry_size;
    return CTC_E_NONE;
}


/**
 @brief This function is used to free dynamic offset

 */
int32
sys_goldengate_nh_offset_free(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                             uint32 entry_num, uint32 offset)
{

    sys_goldengate_opf_t opf;
    uint32 entry_size;
	sys_goldengate_nh_master_t* p_nh_master = NULL;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (offset == 0)
    {/*offset 0 is reserved*/
        return CTC_E_NONE;
    }
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip = %d, entry_type = %d, entry_number = %d, offset = %d\n",
                   lchip, entry_type, entry_num, offset);
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = p_nh_master->nh_table_info_array[entry_type].opf_pool_type + OPF_NH_DYN1_SRAM;
    entry_size = p_nh_master->nh_table_info_array[entry_type].entry_size;
    opf.pool_type = OPF_NH_DYN_SRAM;
    entry_size = entry_size * entry_num;
    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, entry_size, offset));

    if(p_nh_master->nhtbl_used_cnt[entry_type] >= entry_size)
    {
       p_nh_master->nhtbl_used_cnt[entry_type] -= entry_size;
    }
    return CTC_E_NONE;

}


int32
_sys_goldengate_nh_alloc_offset_by_nhtype(uint8 lchip, sys_nh_param_com_t* p_nh_com_para, sys_nh_info_com_t* p_nhinfo)
{
    sys_goldengate_nh_master_t* p_master;

    uint32 dsnh_offset = 0;
    uint8 use_rsv_dsnh = 0, remote_chip = 0,dsnh_entry_num = 0;
    uint8 port_ext_mc_en = 0;
    uint8 exp_type = 0;
    uint8 tunnel_is_spme = 0;
    uint8 pkt_nh_edit_mode  = SYS_NH_IGS_CHIP_EDIT_MODE;
    uint8 dest_gchip = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " entry_type = %d\n", p_nh_com_para->hdr.nh_param_type);

    sys_goldengate_get_gchip_id(lchip, &dest_gchip);
    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    switch (p_nh_com_para->hdr.nh_param_type)
    {
    case SYS_NH_TYPE_BRGUC:
        {
            sys_nh_param_brguc_t* p_nh_param = (sys_nh_param_brguc_t*)p_nh_com_para;
            sys_nh_info_brguc_t* p_brg_nhinfo = (sys_nh_info_brguc_t*)p_nhinfo;
            dsnh_offset = p_nh_param->dsnh_offset;
            p_brg_nhinfo->nh_sub_type = p_nh_param->nh_sub_type;
            if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT)
            {
                if (CTC_FLAG_ISSET(p_nh_param->p_vlan_edit_info->flag, CTC_VLAN_NH_MTU_CHECK_EN))
                {
                   return CTC_E_NOT_SUPPORT;
                }

                if ((0 == p_nh_param->p_vlan_edit_info->flag)
                    &&(0 == p_nh_param->p_vlan_edit_info->edit_flag)
                    &&(0 == p_nh_param->p_vlan_edit_info->loop_nhid)
                    && (p_nh_param->p_vlan_edit_info->svlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE
                        && p_nh_param->p_vlan_edit_info->cvlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE)
                    && (!p_nh_param->p_vlan_edit_nh_param->logic_port)
                    && (0 == p_nh_param->p_vlan_edit_info->user_vlanptr))
                {
                    CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
                                         SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                    use_rsv_dsnh = 1;
                }
                else
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                    if ((p_nh_param->p_vlan_edit_nh_param &&p_nh_param->p_vlan_edit_nh_param->logic_port_valid)
                        ||(p_nh_param->p_vlan_edit_info->loop_nhid))
                    {
                        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                    }
                }
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->gport);
            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT)
            {
                bool share_nh = FALSE;
                CTC_ERROR_RETURN(sys_goldengate_aps_get_share_nh(lchip, p_nh_param->gport, &share_nh));

                if (share_nh)
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 1;
                    if (p_nh_param->p_vlan_edit_info->svlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE
                        && p_nh_param->p_vlan_edit_info->cvlan_edit_type == CTC_VLAN_EGRESS_EDIT_NONE)
                    {
                        CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
                                             SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                        use_rsv_dsnh = 1;
                    }

                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH);
                }
                else
                {
                    p_nhinfo->hdr.dsnh_entry_num  = 2;
                }
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC)
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
                                     SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, &dsnh_offset));
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                use_rsv_dsnh = 1;
            }
            else if (p_nh_param->nh_sub_type == SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS)
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip,
                                     SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, &dsnh_offset));

                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

                p_nhinfo->hdr.dsnh_entry_num  = 1;
                use_rsv_dsnh = 1;
            }
        }
        break;

    case SYS_NH_TYPE_IPUC:
        {
            sys_nh_param_ipuc_t* p_nh_param = (sys_nh_param_ipuc_t*)p_nh_com_para;

            dsnh_offset = p_nh_param->dsnh_offset;
            if(p_nh_param->aps_en)
            {
                p_nhinfo->hdr.dsnh_entry_num  = 2;
            }
            else
            {
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->oif.gport);
            }
            if (p_master->pkt_nh_edit_mode == 1)
            {
                pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            }
        }
        break;

    case SYS_NH_TYPE_IP_TUNNEL:
        {
            sys_nh_param_ip_tunnel_t* p_nh_param = (sys_nh_param_ip_tunnel_t*)p_nh_com_para;
            sys_goldengate_bpe_get_port_extend_mcast_en(lchip, &port_ext_mc_en);
            if (CTC_FLAG_ISSET(p_nh_param->p_ip_nh_param->tunnel_info.flag,CTC_IP_NH_TUNNEL_FLAG_OVERLAY_CROSS_VNI)
                ||  (SYS_NH_IP_TUNNEL_IS_OVERLAY_TUNNEL(p_nh_param->p_ip_nh_param) && port_ext_mc_en)
                || (p_nh_param->p_ip_nh_param->tunnel_info.logic_port)
                ||(p_nh_param->p_ip_nh_param->tunnel_info.inner_packet_type ==PKT_TYPE_IPV4 )
                ||(p_nh_param->p_ip_nh_param->tunnel_info.inner_packet_type ==PKT_TYPE_IPV6 )
                ||CTC_FLAG_ISSET(p_nh_param->p_ip_nh_param->tunnel_info.flag,CTC_IP_NH_TUNNEL_FLAG_XERSPN_WITH_EXT_HDR)
                ||(0 != p_nh_param->p_ip_nh_param->tunnel_info.vn_id))
            {
                CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
            }
            dsnh_offset = p_nh_param->p_ip_nh_param->dsnh_offset;
            p_nhinfo->hdr.dsnh_entry_num  = 1;
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            if (!CTC_FLAG_ISSET(p_nh_param->p_ip_nh_param->tunnel_info.flag, CTC_IP_NH_TUNNEL_FLAG_REROUTE_WITH_TUNNEL_HDR))
            {
                dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->p_ip_nh_param->oif.gport);
            }
        }
        break;

    case SYS_NH_TYPE_TRILL:
        {
            sys_nh_param_trill_t* p_nh_param = (sys_nh_param_trill_t*)p_nh_com_para;
            dsnh_offset = p_nh_param->p_trill_nh_param->dsnh_offset;
            p_nhinfo->hdr.dsnh_entry_num  = 1;
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
            dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->p_trill_nh_param->oif.gport);
        }
        break;

    case SYS_NH_TYPE_MISC:
        {
            sys_nh_param_misc_t* p_nh_param = (sys_nh_param_misc_t*)p_nh_com_para;

            if(CTC_MISC_NH_TYPE_TO_CPU == p_nh_param->p_misc_param->type)
            {
                p_nhinfo->hdr.dsnh_entry_num  = 0;
            }
            else
            {
                dsnh_offset = p_nh_param->p_misc_param->dsnh_offset;
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                if ((CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == p_nh_param->p_misc_param->type) ||
                    (CTC_MISC_NH_TYPE_OVER_L2 == p_nh_param->p_misc_param->type) ||
                    p_nh_param->p_misc_param->misc_param.flex_edit.user_vlanptr)
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                }
                 pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
                 dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_param->p_misc_param->gport);
            }
        }
        break;

    case SYS_NH_TYPE_RSPAN:
        {
            sys_nh_param_rspan_t* p_nh_para = (sys_nh_param_rspan_t*)p_nh_com_para;
            dsnh_offset = p_nh_para->p_rspan_param->dsnh_offset;
            p_nhinfo->hdr.dsnh_entry_num  = 1;
            remote_chip = p_nh_para->p_rspan_param->remote_chip;
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
        }
        break;

    case SYS_NH_TYPE_MPLS:
        {
            sys_nh_param_mpls_t* p_nh_para = (sys_nh_param_mpls_t*)p_nh_com_para;

            ctc_vlan_egress_edit_info_t* p_vlan_info = &p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.nh_com.vlan_info;

            dsnh_offset = p_nh_para->p_mpls_nh_param->dsnh_offset;
            if ((p_nh_para->p_mpls_nh_param->nh_prop == CTC_MPLS_NH_PUSH_TYPE) &&
                (p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.nh_com.opcode == CTC_MPLS_NH_PUSH_OP_L2VPN))
            {

                CTC_ERROR_RETURN(sys_goldengate_nh_check_mpls_tunnel_exp(lchip, p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.tunnel_id, &exp_type));
                CTC_ERROR_RETURN(sys_goldengate_nh_check_mpls_tunnel_is_spme(lchip, p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.tunnel_id, &tunnel_is_spme));
                if((p_vlan_info != NULL)
                    && ((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID))
                     ||((CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
                     ||(CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))))
                     && (!tunnel_is_spme))
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                }

                if ((p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.label_num
                    && (CTC_NH_EXP_SELECT_PACKET == p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.push_label[0].exp_type)))
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                }

                if (exp_type)
                {
                    CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                }

                if (p_nh_para->p_mpls_nh_param->aps_en)
                {
                    if ((p_nh_para->p_mpls_nh_param->nh_p_para.nh_p_param_push.label_num
                        && (CTC_NH_EXP_SELECT_PACKET == p_nh_para->p_mpls_nh_param->nh_p_para.nh_p_param_push.push_label[0].exp_type)))
                    {
                        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                    }

                    CTC_ERROR_RETURN(sys_goldengate_nh_check_mpls_tunnel_exp(lchip, p_nh_para->p_mpls_nh_param->nh_p_para.nh_p_param_push.tunnel_id, &exp_type));

                    if (exp_type)
                    {
                        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
                    }

                }

            }

            if (p_nh_para->p_mpls_nh_param->aps_en)
            {
                p_nhinfo->hdr.dsnh_entry_num  = 2;
            }
            else
            {
                p_nhinfo->hdr.dsnh_entry_num  = 1;
                if (CTC_MPLS_NH_PUSH_TYPE == p_nh_para->p_mpls_nh_param->nh_prop)
                {
                    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel = NULL;
                    CTC_ERROR_RETURN(sys_goldengate_nh_lkup_mpls_tunnel(lchip, p_nh_para->p_mpls_nh_param->nh_para.nh_param_push.tunnel_id, &p_mpls_tunnel));
                    if (!p_mpls_tunnel)
                    {
                        return CTC_E_NH_NOT_EXIST_TUNNEL_LABEL;
                    }
                    if (!CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_APS)
                        && !CTC_FLAG_ISSET(p_mpls_tunnel->flag, SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS))
                    {
                        dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_mpls_tunnel->gport);
                    }
                }
                else if(CTC_MPLS_NH_POP_TYPE == p_nh_para->p_mpls_nh_param->nh_prop)
                {
                    dest_gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nh_para->p_mpls_nh_param->nh_para.nh_param_pop.nh_com.oif.gport);
                }
            }
            pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE;
        }
        break;

    case SYS_NH_TYPE_ECMP:
    case SYS_NH_TYPE_MCAST:
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
        p_nhinfo->hdr.dsnh_entry_num  = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    /*No need alloc  and use dsnh offset*/
    if (0 == p_nhinfo->hdr.dsnh_entry_num)
    {
        return CTC_E_NONE;
    }

    if (use_rsv_dsnh)
    {
        p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSNH);

    }
    else if ((p_master->pkt_nh_edit_mode == 1 && (pkt_nh_edit_mode == SYS_NH_EGS_CHIP_EDIT_MODE) && dsnh_offset)
                    /*misc edit mode*/
                || (p_master->pkt_nh_edit_mode > 1 && (pkt_nh_edit_mode == SYS_NH_EGS_CHIP_EDIT_MODE)))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num *2;
        }
        else
        {
            dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num ;
        }
        p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
        CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);
        if (!SYS_GCHIP_IS_REMOTE(lchip, dest_gchip))
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_check_max_glb_nh_offset(lchip, dsnh_offset));
            CTC_ERROR_RETURN(sys_goldengate_nh_check_glb_nh_offset(lchip, dsnh_offset, dsnh_entry_num, TRUE));
            CTC_ERROR_RETURN(sys_goldengate_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, dsnh_entry_num, TRUE));
        }
    }
    else
    {
        if (p_nh_com_para->hdr.nh_param_type == SYS_NH_TYPE_RSPAN && remote_chip)
        {
            sys_nh_info_rspan_t* p_rspan_nhinfo = (sys_nh_info_rspan_t*)p_nhinfo;
            sys_nh_param_rspan_t* p_nh_para = (sys_nh_param_rspan_t*)p_nh_com_para;

            if (p_master->rspan_nh_in_use
                && (p_master->rspan_vlan_id != p_nh_para->p_rspan_param->rspan_vid))
            {
                return CTC_E_NH_CROSS_CHIP_RSPAN_NH_IN_USE;
            }
            else
            {
                dsnh_offset = SYS_DSNH_FOR_REMOTE_MIRROR;
                p_master->rspan_nh_in_use = 1;
                p_master->rspan_vlan_id  = p_nh_para->p_rspan_param->rspan_vid;
                p_rspan_nhinfo->remote_chip = 1;

            }
        }
        else
        {
            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, &dsnh_offset));
            }

            CTC_SET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);
        }

        p_nhinfo->hdr.dsfwd_info.dsnh_offset = dsnh_offset;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_nh_free_offset_by_nhinfo(uint8 lchip, sys_goldengate_nh_type_t nh_type, sys_nh_info_com_t* p_nhinfo)
{

    sys_goldengate_nh_master_t* p_master;
    uint8 dsnh_entry_num = 0;


    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSNH))
    {
        return CTC_E_NONE;
    }


    if (nh_type == SYS_NH_TYPE_RSPAN)
    {
        sys_nh_info_rspan_t* p_rspan_nhinfo = (sys_nh_info_rspan_t*)p_nhinfo;
        if (p_rspan_nhinfo->remote_chip)
        {
            p_master->rspan_nh_in_use = 0;
            return CTC_E_NONE;
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
    {
            if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
            }
            else
            {
                CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset));
            }


        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH);

    }
    else
    {
      if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
      {
         dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num *2;
      }
      else
      {
         dsnh_entry_num = p_nhinfo->hdr.dsnh_entry_num;
      }
      CTC_ERROR_RETURN(sys_goldengate_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset,dsnh_entry_num , FALSE));
    }


    return CTC_E_NONE;
}



int32
sys_goldengate_nh_get_fatal_excp_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_PTR_VALID_CHECK(p_offset);

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_offset);

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *p_offset = p_nh_master->fatal_excp_base;

    return CTC_E_NONE;
}



int32
sys_goldengate_nh_get_bypass_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    int32 ret;

    CTC_PTR_VALID_CHECK(p_offset);
    ret = sys_goldengate_nh_get_resolved_offset(lchip,
            SYS_NH_RES_OFFSET_TYPE_BYPASS_NH, p_offset);

    return ret;
}

int32
sys_goldengate_nh_get_mirror_dsnh_offset(uint8 lchip, uint32* p_offset)
{
    int32 ret;

    CTC_PTR_VALID_CHECK(p_offset);
    ret = sys_goldengate_nh_get_resolved_offset(lchip,
            SYS_NH_RES_OFFSET_TYPE_MIRROR_NH, p_offset);

    return ret;
}

int32
sys_goldengate_nh_get_ipmc_l2edit_offset(uint8 lchip, uint16* p_l2edit_offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *p_l2edit_offset = p_nh_master->ipmc_resolved_l2edit;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_emcp_if_l2edit(uint8 lchip, void** p_l2edit_ptr)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *p_l2edit_ptr = p_nh_master->ecmp_if_resolved_l2edit;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_reflective_dsfwd_offset(uint8 lchip, uint32* p_dsfwd_offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    *p_dsfwd_offset = p_nh_master->reflective_resolved_dsfwd_offset;

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_get_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid, uint8 mtu_no_chk, uint32* p_dsnh_offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    if ( SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT == l3ifid )
    {
        sys_goldengate_nh_get_resolved_offset(lchip,
                                 SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH, p_dsnh_offset);
        return CTC_E_NONE;
    }

    if (l3ifid > SYS_GG_MAX_CTC_L3IF_ID)
    {
        return CTC_E_L3IF_INVALID_IF_ID;
    }

    if (p_nh_master->ipmc_dsnh_offset[l3ifid] == 0)
    {

        sys_nh_param_dsnh_t dsnh_param;
        sys_l3if_prop_t l3if_prop;

        sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));

        l3if_prop.l3if_id = l3ifid;
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info(lchip, 1, &l3if_prop));
        dsnh_param.l2edit_ptr = 0;
        dsnh_param.l3edit_ptr = 0;
        dsnh_param.dest_vlan_ptr = l3if_prop.vlan_ptr;
        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPMC;
        dsnh_param.mtu_no_chk = mtu_no_chk;

        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1,
                                                       &dsnh_param.dsnh_offset));

        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh4w(lchip, &dsnh_param));
        p_nh_master->ipmc_dsnh_offset[l3ifid] = dsnh_param.dsnh_offset;
        *p_dsnh_offset = dsnh_param.dsnh_offset;

    }
    else
    {
        *p_dsnh_offset = p_nh_master->ipmc_dsnh_offset[l3ifid];
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_del_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    if (p_nh_master->ipmc_dsnh_offset[l3ifid] != 0)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 1,
                                         p_nh_master->ipmc_dsnh_offset[l3ifid]);
        p_nh_master->ipmc_dsnh_offset[l3ifid] = 0;

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_check_max_glb_nh_offset(uint8 lchip, uint32 offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (offset >= p_nh_master->max_glb_nh_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_check_max_glb_met_offset(uint8 lchip, uint32 offset)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (offset >= p_nh_master->max_glb_met_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_check_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                     bool should_not_inuse)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    uint32 i;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (NULL == (p_bmp = p_nh_master->p_occupid_nh_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (should_not_inuse && CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                               (1 << (curr_offset & BITS_MASK_OF_WORD))))
        {
            return CTC_E_NH_GLB_SRAM_IS_INUSE;
        }

        if ((!should_not_inuse) && (!CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                                    (1 << (curr_offset & BITS_MASK_OF_WORD)))))
        {
            return CTC_E_NH_GLB_SRAM_ISNOT_INUSE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_set_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    int32 i;

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if ((start_offset + entry_num - 1) > p_nh_master->max_glb_nh_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    if (NULL == (p_bmp = p_nh_master->p_occupid_nh_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (is_set)
        {
            CTC_SET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                         (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
        else
        {
            CTC_UNSET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                           (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
    }
	if(is_set)
	{
	     p_nh_master->glb_nh_used_cnt += entry_num;
	}
	else if(p_nh_master->glb_nh_used_cnt >= entry_num)
	{
	  	  p_nh_master->glb_nh_used_cnt  -= entry_num;
	}

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_set_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    int32 i;

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if ((start_offset + entry_num - 1) > p_nh_master->max_glb_met_offset)
    {
        return CTC_E_NH_GLB_SRAM_INDEX_EXCEED;
    }

    if (NULL == (p_bmp = p_nh_master->p_occupid_met_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (is_set)
        {
            CTC_SET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                         (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
        else
        {
            CTC_UNSET_FLAG(p_bmp[(curr_offset >> BITS_SHIFT_OF_WORD)],
                           (1 << (curr_offset & BITS_MASK_OF_WORD)));
        }
    }
    if(is_set)
	{
	     p_nh_master->glb_met_used_cnt += entry_num;
	}
	else if(p_nh_master->glb_met_used_cnt >= entry_num)
	{
	  	  p_nh_master->glb_met_used_cnt  -= entry_num;
	}
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_check_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                      bool should_not_inuse)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    uint32* p_bmp, curr_offset;
    uint32 i;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if (NULL == (p_bmp = p_nh_master->p_occupid_met_offset_bmp))
    {
        return CTC_E_NOT_INIT;
    }

    if (0 == entry_num)
    {
        return CTC_E_NONE;
    }

    for (i = 0; i < entry_num; i++)
    {
        curr_offset = start_offset + i;
        if (should_not_inuse && CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                               (1 << (curr_offset & BITS_MASK_OF_WORD))))
        {
            return CTC_E_NH_GLB_SRAM_IS_INUSE;
        }

        if ((!should_not_inuse) && (!CTC_FLAG_ISSET(p_bmp[((curr_offset) >> BITS_SHIFT_OF_WORD)],
                                                    (1 << (curr_offset & BITS_MASK_OF_WORD)))))
        {
            return CTC_E_NH_GLB_SRAM_ISNOT_INUSE;
        }
    }

    return CTC_E_NONE;
}

#define _________ds_write________

/**
 @brief This function is deinit nexthop module   callback function for each nexthop type
 */
int32
sys_goldengate_nh_global_dync_entry_set_default(uint8 lchip, uint32 min_offset, uint32 max_offset)
{
#ifdef NEVER

    uint32 offset;
    ds_met_entry_t dsmet;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* assign default value for met entry. the default value is discarding packets */
    sal_memset(&dsmet, 0, sizeof(ds_met_entry_t));

    for (offset = min_offset; offset <= max_offset; offset++)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_MET,
                                                           offset, &dsmet));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


int32
sys_goldengate_nh_mpls_add_cw(uint8 lchip, uint32 cw,
                                 uint8* cw_index,
                                 bool is_add)
{
    uint8 bit = 0;
    uint32 cmd = 0;
    uint8 free = 0;
    uint32 field_val = 0;
    sys_goldengate_nh_master_t* p_master;

    CTC_ERROR_RETURN(_sys_goldengate_nh_get_nh_master(lchip, &p_master));

    if (is_add)
    {
        for (bit = 1; bit < SYS_NH_CW_NUM; bit++)
        {
            if (p_master->cw_ref_cnt[bit])
            {
                cmd = DRV_IOR(DsControlWord_t, DsControlWord_cwData_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, bit, cmd, &field_val));

                if (field_val == cw)
                {
                    if (0xFFFF == p_master->cw_ref_cnt[bit])
                    {
                        return CTC_E_NO_RESOURCE;
                    }
                    p_master->cw_ref_cnt[bit]++;
                    *cw_index = bit;
                    return CTC_E_NONE;
                }

                break;
            }
            else if (free == 0)
            {
                free = bit;
            }

        }

        if (free != 0)
        {
            field_val = cw;
            cmd = DRV_IOW(DsControlWord_t, DsControlWord_cwData_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, free, cmd, &field_val));
            p_master->cw_ref_cnt[free]++;
            *cw_index = free;
            return CTC_E_NONE;
        }

        return CTC_E_EXCEED_MAX_SIZE;

    }
    else
    {
        bit = *cw_index;
        p_master->cw_ref_cnt[bit]--;
        *cw_index = 0;
        if (p_master->cw_ref_cnt[bit] == 0)
        {
            field_val = 0;
            cmd = DRV_IOW(DsControlWord_t, DsControlWord_cwData_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, bit, cmd, &field_val));
        }
    }

    return CTC_E_NONE;
}



STATIC int32
sys_goldengate_nh_dsnh_build_vlan_info(uint8 lchip, sys_nexthop_t* p_dsnh,
                                      ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    ctc_chip_device_info_t device_info;
    CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, &device_info));

    if (!p_vlan_info)
    {
        return CTC_E_INVALID_PTR;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_VLAN_SWAP_EN))
    {
        /*1. cvlan_tag_op_disable = 1*/
        p_dsnh->cvlan_tag_disable = TRUE;

        /*2. svlan_tag_op_disable = 1*/
        p_dsnh->svlan_tag_disable = TRUE;

        /*3. tagged_mode = 1*/
        p_dsnh->tagged_mode = TRUE;

        /*4. default use mapped cos*/
        p_dsnh->derive_stag_cos  = TRUE;

        /*Unset  parametre's cvid valid bit, other with output
        cvlan id valid will be overwrited in function
        sys_goldengate_nh_dsnh4w_assign_vid, bug11323*/
        CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
        CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);

        /*Swap TPID*/
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_TPID_SWAP_EN))
        {
            p_dsnh->tpid_swap = TRUE;
        }

        /*Swap Cos*/
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_COS_SWAP_EN))
        {
            p_dsnh->cos_swap = TRUE;
        }
    }
    else
    {
        switch (p_vlan_info->svlan_edit_type)
        {
        case CTC_VLAN_EGRESS_EDIT_NONE:
        case CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE:
            p_dsnh->output_svlan_id_valid = FALSE;
            CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->derive_stag_cos  = 1;
            p_dsnh->stag_cfi  = 0;       /*use  raw packet's cos*/
            break;
        case CTC_VLAN_EGRESS_EDIT_INSERT_VLAN:
            if (CTC_VLAN_EGRESS_EDIT_INSERT_VLAN != p_vlan_info->cvlan_edit_type &&
                CTC_VLAN_EGRESS_EDIT_NONE != p_vlan_info->cvlan_edit_type &&
                CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE != p_vlan_info->cvlan_edit_type)
            {
                return CTC_E_NH_VLAN_EDIT_CONFLICT;
            }

            p_dsnh->tagged_mode = TRUE; /*insert stag*/
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->stag_cfi  = 1;                /*use mapped cos*/
            p_dsnh->derive_stag_cos  = 1;
            break;

        case CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN:
            p_dsnh->svlan_tagged = TRUE;
            p_dsnh->stag_cfi  = 1;                /*use mapped cos*/
            p_dsnh->derive_stag_cos  = 1;
            break;

        case CTC_VLAN_EGRESS_EDIT_STRIP_VLAN:
            p_dsnh->svlan_tagged = FALSE;
            break;

        default:
            return CTC_E_NH_INVALID_VLAN_EDIT_TYPE;
        }

       /* stag cos opreation */
        if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_REPLACE_SVLAN_COS))
        {
            if (device_info.version_id > 1)
            {
                p_dsnh->derive_stag_cos     = 0;
                p_dsnh->stag_cos            = p_vlan_info->stag_cos & 0x7;
                p_dsnh->stag_cfi  = 0;
            }
            else
            {
                return CTC_E_NH_INVALID_VLAN_EDIT_TYPE;
            }
        }
        else if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_MAP_SVLAN_COS))
        {
            p_dsnh->derive_stag_cos     = 1;
            p_dsnh->replace_stag_cos    = 1;
        }

        switch (p_vlan_info->cvlan_edit_type)
        {
        case CTC_VLAN_EGRESS_EDIT_NONE:

            /*Don't use DsNexthop's output cvlanid*/
            p_dsnh->output_cvlan_id_valid = FALSE;
            p_dsnh->cvlan_tag_disable = FALSE;
            CTC_UNSET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);
            break;

        case CTC_VLAN_EGRESS_EDIT_KEEP_VLAN_UNCHANGE:
            p_dsnh->cvlan_tag_disable = TRUE;
            break;

        case CTC_VLAN_EGRESS_EDIT_INSERT_VLAN:
            if (CTC_VLAN_EGRESS_EDIT_INSERT_VLAN != p_vlan_info->svlan_edit_type)
            {
                return CTC_E_NH_VLAN_EDIT_CONFLICT;
            }
            p_dsnh->tagged_mode = TRUE;
            break;

        case CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN:
            break;

        case CTC_VLAN_EGRESS_EDIT_STRIP_VLAN:
            return CTC_E_NOT_SUPPORT;
            break;

        default:
            return CTC_E_NH_INVALID_VLAN_EDIT_TYPE;
        }
    }
    if((CTC_VLAN_EGRESS_EDIT_NONE == p_vlan_info->cvlan_edit_type && CTC_VLAN_EGRESS_EDIT_NONE == p_vlan_info->svlan_edit_type)
        && (!CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_VLAN_SWAP_EN)))
    {
        p_dsnh->vlan_xlate_mode = 0;
    }
    return CTC_E_NONE;
}

STATIC int32
sys_goldengate_nh_dsnh4w_assign_vid(uint8 lchip, sys_nexthop_t* p_dsnh,
                                   ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        p_dsnh->output_cvlan_id_valid = TRUE;
        p_dsnh->output_cvlan_id = p_vlan_info->output_cvid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        p_dsnh->output_svlan_id_valid = TRUE;
        p_dsnh->output_svlan_id= p_vlan_info->output_svid;
    }

    return CTC_E_NONE;
}

STATIC int32
sys_goldengate_nh_dsnh8w_assign_vid(uint8 lchip, sys_nexthop_t* p_dsnh8w,
                                   ctc_vlan_egress_edit_info_t* p_vlan_info)
{
    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
    {
        p_dsnh8w->output_cvlan_id_valid = TRUE;
        p_dsnh8w->output_cvlan_id = p_vlan_info->output_cvid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
    {
        p_dsnh8w->output_svlan_id_valid = TRUE;
        p_dsnh8w->output_svlan_id = p_vlan_info->output_svid;
    }

    if (CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID))
    {
        if(p_vlan_info->svlan_tpid_index>3)
        {
            return CTC_E_NH_INVALID_TPID_INDEX;
        }
        p_dsnh8w->svlan_tpid_en = TRUE;
        p_dsnh8w->svlan_tpid = p_vlan_info->svlan_tpid_index;
    }

    return CTC_E_NONE;
}

/*
   inner_edit:ptr_type
   0- L2Edit
   1- L3Edit

  outer_edit: location/type
   0/1   L3Edit (Pipe0) -->( L3Edit (Pipe1),opt)--> L2Edit(Pipe2)
   0/0   L2Edit (Pipe0) --> L3Edit(Pipe1)
   1/0   L2Edit (Pipe2)
   1/1   L3Edit (Pipe1) --> L2Edit(Pipe2)
*/

int32
sys_goldengate_nh_write_entry_dsnh4w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{

    sys_nexthop_t dsnh;
    uint32 op_bridge = SYS_NH_OP_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_dsnh_param);
    sal_memset(&dsnh, 0, sizeof(dsnh));

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "\n\
                   lchip = % d, \n\
                   flag = 0x%x, \n\
                   dsnh_offset = % d, \n\
                   dsnh_type = % d\n, \
                   l2edit_ptr = % d, \n\
                   lspedit_ptr = % d, \n\
                   l3EditPtr = % d \n",
                   lchip,
                   p_dsnh_param->flag,
                   p_dsnh_param->dsnh_offset,
                   p_dsnh_param->dsnh_type,
                   p_dsnh_param->l2edit_ptr,
                   p_dsnh_param->lspedit_ptr,
                   p_dsnh_param->l3edit_ptr);

    if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP))
    {
        dsnh.aps_bridge_en = 1;
    }

    dsnh.stats_ptr  = p_dsnh_param->stats_ptr;

    switch (p_dsnh_param->dsnh_type)
    {
    case SYS_NH_PARAM_DSNH_TYPE_IPUC:
        dsnh.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        /*IPUC only use ROUTE_COMPACT in current GB SDK*/
        if ((p_dsnh_param->l2edit_ptr == 0) && (p_dsnh_param->inner_l2edit_ptr == 0))
        {
            dsnh.payload_operation = SYS_NH_OP_ROUTE_COMPACT;
            dsnh.share_type = SYS_NH_SHARE_TYPE_MAC_DA;
            SYS_GOLDENGATE_NH_DSNH_ASSIGN_MACDA(dsnh, (*p_dsnh_param));
            if(CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL))
            {
                dsnh.ttl_no_dec = TRUE;
            }
            else
            {
                dsnh.ttl_no_dec = FALSE;
            }

            dsnh.is_drop = p_dsnh_param->is_drop;
        }
        else
        {
           CTC_SET_FLAG(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP);
           SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
           if(CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL))
           {
               dsnh.payload_operation = SYS_NH_OP_ROUTE_NOTTL;
           }
           else
           {
               dsnh.payload_operation = SYS_NH_OP_ROUTE;
           }
           dsnh.share_type = SYS_NH_SHARE_TYPE_L2EDIT_VLAN;

           dsnh.outer_edit_valid    = 1;
           dsnh.outer_edit_ptr_type = 0;/*L2edit*/
           if (p_dsnh_param->l2edit_ptr)
           {
               dsnh.outer_edit_location = 1;/*Pipe2*/
               dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;
           }
           else if (p_dsnh_param->inner_l2edit_ptr)
           {
               dsnh.outer_edit_location = 0;/*Pipe0*/
               dsnh.outer_edit_ptr = p_dsnh_param->inner_l2edit_ptr;
           }

           /*for phy if vlan, no use any more*/
           if(p_dsnh_param->p_vlan_info
              && CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
               {
                dsnh.output_svlan_id_valid = p_dsnh_param->p_vlan_info->output_svid != 0;
                dsnh.output_svlan_id = p_dsnh_param->p_vlan_info->output_svid;
               }
        }
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPMC:
        dsnh.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        dsnh.svlan_tagged = 1;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.payload_operation = SYS_NH_OP_ROUTE_COMPACT;
        dsnh.share_type = SYS_NH_SHARE_TYPE_MAC_DA;

        break;

    case SYS_NH_PARAM_DSNH_TYPE_BRGUC:
        /*Set default cos action*/
        SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
        dsnh.svlan_tagged = 1;
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;   /*value 0 means use srcvlanPtr*/
        dsnh.payload_operation = CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_SWAP_MAC) ?
            SYS_NH_OP_NONE : SYS_NH_OP_BRIDGE;

        dsnh.share_type = SYS_NH_SHARE_TYPE_VLANTAG;
        dsnh.vlan_xlate_mode = 1;     /*humber mode*/
        if (p_dsnh_param->p_vlan_info)
        {
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_STATS_EN))
            {
                CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip, p_dsnh_param->p_vlan_info->stats_id, CTC_STATS_STATSID_TYPE_NEXTHOP, &(dsnh.stats_ptr)));
            }
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh.is_leaf = 1;
            }
             if (p_dsnh_param->p_vlan_info->flag & CTC_VLAN_NH_MTU_CHECK_EN)
            {
                dsnh.mtu_check_en = 1;
            }
            if(CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag,CTC_VLAN_NH_HORIZON_SPLIT_EN))
            {
                dsnh.payload_operation = SYS_NH_OP_BRIDGE_VPLS;
            }
            else if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_PASS_THROUGH))
            {
                dsnh.payload_operation = SYS_NH_OP_BRIDGE_INNER;
            }
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, &dsnh, p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh4w_assign_vid(lchip, &dsnh, p_dsnh_param->p_vlan_info));
        }

        break;

    case SYS_NH_PARAM_DSNH_TYPE_BYPASS:
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.payload_operation = SYS_NH_OP_NONE;
        dsnh.vlan_xlate_mode = 1;    /*humber mode*/
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PHP: /*PHP */

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_PHP_SHORT_PIPE))
        {
            dsnh.payload_operation = SYS_NH_OP_ROUTE_NOTTL;
            dsnh.use_packet_ttl = 1;
        }
        else
        {
            dsnh.payload_operation = SYS_NH_OP_ROUTE;
        }

        dsnh.mtu_check_en = TRUE;
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);
        dsnh.share_type = SYS_NH_SHARE_TYPE_L2EDIT_VLAN;

        dsnh.outer_edit_valid    = 1;
        dsnh.outer_edit_ptr_type = 0;/*L2edit*/
        if (p_dsnh_param->l2edit_ptr)
        {
            dsnh.outer_edit_location = 1;/*Pipe2*/
            dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;
        }
        else if (p_dsnh_param->inner_l2edit_ptr)
        {
            dsnh.outer_edit_location = 0;/*Pipe0*/
            dsnh.outer_edit_ptr = p_dsnh_param->inner_l2edit_ptr;
        }
        /*for phy if vlan, no use any more*/
        if(p_dsnh_param->p_vlan_info
              && CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
        {
                dsnh.output_svlan_id_valid = p_dsnh_param->p_vlan_info->output_svid != 0;
                dsnh.output_svlan_id = p_dsnh_param->p_vlan_info->output_svid;
        }
        break;
    case SYS_NH_PARAM_DSNH_TYPE_MPLS_OP_NONE:   /* mpls switch*/
        return CTC_E_NH_SHOULD_USE_DSNH8W;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_NONE: /*Swap  label on LSR /Pop label and do   label  Swap on LER*/
        dsnh.payload_operation = SYS_NH_OP_NONE;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);

        if(p_dsnh_param->lspedit_ptr || (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP)))
        {
            dsnh.outer_edit_location = 0;
            dsnh.outer_edit_ptr_type = 1;/*Pipe0: L3edit*/
            dsnh.outer_edit_ptr = p_dsnh_param->lspedit_ptr;
        }
        else
        {
            dsnh.outer_edit_location = 1;
            dsnh.outer_edit_ptr_type = 0;/*Pipe0: L2edit*/
            dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;
        }

        dsnh.inner_edit_ptr_type = 1;/*L3Edit*/
        dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;


        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_ROUTE: /* (L3VPN/FTN)*/
        dsnh.payload_operation = SYS_NH_OP_ROUTE;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);

        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

        if (dsnh.aps_bridge_en)
        {
            dsnh.outer_edit_location = 0;
            dsnh.outer_edit_ptr_type = 1;
            dsnh.outer_edit_ptr = p_dsnh_param->lspedit_ptr;/*aps group*/
        }
        else
        {
            if (p_dsnh_param->spme_mode)
            {
                dsnh.outer_edit_location = 1;
                dsnh.outer_edit_ptr_type = 1;
                dsnh.outer_edit_ptr = p_dsnh_param->lspedit_ptr;/*Pipe0: L3edit*/
            }
            else
            {
                if(p_dsnh_param->lspedit_ptr) /*LSP+ARP*/
                {
                    dsnh.outer_edit_location = 0;
                    dsnh.outer_edit_ptr_type = 1;
                    dsnh.outer_edit_ptr = p_dsnh_param->lspedit_ptr;/*Pipe0: L3edit*/
                }
                else /*ARP*/
                {
                    dsnh.outer_edit_location = 1;
                    dsnh.outer_edit_ptr_type = 0;
                    dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;/*Pipe3: l2edti*/
                }
            }

        }

        dsnh.inner_edit_ptr_type = 1;/* L3Edit*/
        dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;

        dsnh.mtu_check_en = TRUE;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN:
        if (p_dsnh_param->hvpls)
        {
            op_bridge = SYS_NH_OP_BRIDGE_INNER;
        }
        else
        {
            op_bridge = SYS_NH_OP_BRIDGE_VPLS;
        }

        if (p_dsnh_param->spme_mode)
        {
            op_bridge = SYS_NH_OP_NONE;
        }

        dsnh.vlan_xlate_mode = 1;       /*humber mode*/
        dsnh.payload_operation = op_bridge;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.mtu_check_en = TRUE;
        dsnh.cw_index = p_dsnh_param->cw_index;
        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

        if (p_dsnh_param->spme_mode)
        {
            dsnh.outer_edit_location = 0;
            dsnh.outer_edit_ptr_type = 1;
            dsnh.outer_edit_ptr = p_dsnh_param->l3edit_ptr; /*Pipe0: L3edit*/

            dsnh.inner_edit_ptr_type = 0; /* L3Edit(1)*/
            dsnh.inner_edit_ptr = p_dsnh_param->inner_l2edit_ptr;
        }
        else
        {
            if(p_dsnh_param->lspedit_ptr || (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP)))
            {
                dsnh.outer_edit_location = 0;
                dsnh.outer_edit_ptr_type = 1;
                dsnh.outer_edit_ptr = p_dsnh_param->lspedit_ptr; /*Pipe0: L3edit*/
            }
            else
            {
                dsnh.outer_edit_location = 1;
                dsnh.outer_edit_ptr_type = 0;
                dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr; /*Pipe0: L2edit*/
            }

            dsnh.inner_edit_ptr_type = 1; /* L3Edit(1)*/
            dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;
        }

        if(!p_dsnh_param->spme_mode)
        {
            if (p_dsnh_param->p_vlan_info &&
                ((CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID)) ||
                 (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))))
            {
                return CTC_E_NH_SHOULD_USE_DSNH8W;
            }
            else
            {
                /*Set default cos action*/
                if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
                {
                    dsnh.is_leaf = 1;
                }

                SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);
                CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, &dsnh, p_dsnh_param->p_vlan_info));
            }
        }

        break;

    case SYS_NH_PARAM_DSNH_TYPE_RSPAN:
        dsnh.vlan_xlate_mode = 1;    /*humber mode*/
        dsnh.tagged_mode = 1;
        dsnh.svlan_tagged = 1;
        dsnh.mirror_tag_add = 1;
        dsnh.derive_stag_cos = 1;    /*restore vlantag for mirror*/
        dsnh.replace_ctag_cos = 1;   /*add rspan vlan id*/
        dsnh.output_svlan_id_valid = 1;
        dsnh.output_svlan_id = p_dsnh_param->p_vlan_info->output_svid & 0xFFF;
        dsnh.dest_vlan_ptr = p_dsnh_param->dest_vlan_ptr;
        dsnh.payload_operation = SYS_NH_OP_MIRROR;
        dsnh.share_type = SYS_NH_SHARE_TYPE_VLANTAG;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL:

        dsnh.payload_operation = (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE)) ?
            SYS_NH_OP_NONE  : ((CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL)) ?
            SYS_NH_OP_ROUTE_NOTTL : SYS_NH_OP_ROUTE);

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE))
        {
            dsnh.payload_operation = p_dsnh_param->hvpls?SYS_NH_OP_BRIDGE_INNER: SYS_NH_OP_BRIDGE_VPLS;
        }

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_STRIP_VLAN))
        {
            dsnh.svlan_tagged = FALSE;
            dsnh.vlan_xlate_mode = 1;       /*humber mode*/
        }
        else
        {
           dsnh.svlan_tagged = TRUE;
        }
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);

        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

         if (p_dsnh_param->inner_l2edit_ptr)
        { /*outer L2Edit*/
           dsnh.outer_edit_location = 0;
           dsnh.outer_edit_ptr_type = 1;
           dsnh.outer_edit_ptr = p_dsnh_param->l3edit_ptr;  /*Pipe0 L3edit*/

           dsnh.inner_edit_ptr_type = 0;
           dsnh.inner_edit_ptr = p_dsnh_param->l2edit_ptr;  /*inner L2edit*/

        }
        else
        {
            dsnh.outer_edit_location = 1;
            dsnh.outer_edit_ptr_type = 0;
            dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;  /*outer L2edit*/
            dsnh.inner_edit_ptr_type = 1;
            dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;  /*L3 edit*/
            if (p_dsnh_param->loop_edit)
            {
                dsnh.outer_edit_location = 0;
            }
        }

        dsnh.mtu_check_en = TRUE;
        break;

    case SYS_NH_PARAM_DSNH_TYPE_TRILL:

        dsnh.payload_operation = SYS_NH_OP_BRIDGE_VPLS;
        dsnh.svlan_tagged = TRUE;
        dsnh.mtu_check_en = TRUE;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);

        dsnh.outer_edit_location = 1;
        dsnh.outer_edit_ptr_type = 0;
        dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;  /*outer L2edit*/
        dsnh.inner_edit_ptr_type = 1;
        dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;  /*L3 edit*/
        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL_FOR_MIRROR:
        dsnh.payload_operation = SYS_NH_OP_MIRROR;
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        SYS_GOLDENGATE_NH_DSNH_BUILD_FLAG(dsnh, p_dsnh_param->flag);

        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

        dsnh.outer_edit_location = 1;
        dsnh.outer_edit_ptr_type = 0;   /*Pipe2: L2edit*/
        dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;

        dsnh.inner_edit_ptr_type = 1;   /*L3edit*/
        dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;

        break;
case SYS_NH_PARAM_DSNH_TYPE_OF:
        dsnh.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_MISC_FLEX_PLD_BRIDGE))
        {
            dsnh.payload_operation = SYS_NH_OP_BRIDGE;
            dsnh.svlan_tagged = TRUE;
        }
        else
        {
            dsnh.payload_operation = SYS_NH_OP_NONE;
        }
        dsnh.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

        dsnh.outer_edit_location = 0;
        dsnh.outer_edit_ptr_type = 0; /*Pipe0: L2edit*/
        dsnh.outer_edit_ptr = p_dsnh_param->l2edit_ptr;

        dsnh.inner_edit_ptr_type = 1;/*L3edit*/
        dsnh.inner_edit_ptr = p_dsnh_param->l3edit_ptr;

        break;

    default:
        return CTC_E_NH_INVALID_DSNH_TYPE;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsNH4W :: DestVlanPtr = 0x%x, PldOP = %d ,\n \
                   copyCtagCos = %d, CvlanTagged = %d, DeriveStagCos = %d,\n \
                   MtuCheckEn = %d, OutputCvlanVlid = %d, OutputSvlanVlid = %d,\n \
                   ReplaceCtagCos = %d, StagCfi = %d, StagCos = %d,\n \
                   OutEditLoc = %d, OutEditPtrType = %d, OutEditPtr = %d,\n \
                   InnerEditPtrType = %d, InnerEditPtr = %d\n, \
                   SvlanTagged = %d, TaggedMode = %d, macDa = %s\n ",
                   dsnh.dest_vlan_ptr, dsnh.payload_operation,
                   dsnh.copy_ctag_cos, 0, dsnh.derive_stag_cos,
                   dsnh.mtu_check_en, dsnh.output_cvlan_id_valid, dsnh.output_svlan_id_valid,
                   dsnh.replace_ctag_cos,dsnh.stag_cfi, dsnh.stag_cos,
                   dsnh.outer_edit_location,dsnh.outer_edit_ptr_type, dsnh.outer_edit_ptr,
                   dsnh.inner_edit_ptr_type,dsnh.inner_edit_ptr,
                   dsnh.svlan_tagged, dsnh.tagged_mode, sys_goldengate_output_mac(dsnh.mac_da));

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_dsnh_param->dsnh_offset, &dsnh));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_write_entry_dsnh8w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{
    sys_nexthop_t dsnh_8w;
    uint32 op_bridge = 0;
    uint32 op_route_no_ttl = 0;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsnh_param);
    sal_memset(&dsnh_8w, 0, sizeof(dsnh_8w));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n\
                   lchip = % d, \n\
                   dsnh_offset = % d, \n\
                   dsnh_type = % d\n, \
                   lspedit_ptr = % d, \n\
                   inner_l2edit_ptr = %d, \n\
                   l3EditPtr = % d \n",
                   lchip,
                   p_dsnh_param->dsnh_offset,
                   p_dsnh_param->dsnh_type,
                   p_dsnh_param->lspedit_ptr,
                   p_dsnh_param->inner_l2edit_ptr,
                   p_dsnh_param->l3edit_ptr);


    if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP))
    {
        dsnh_8w.aps_bridge_en = 1;
    }

    dsnh_8w.stats_ptr  = p_dsnh_param->stats_ptr;
    dsnh_8w.stag_cos |= 1;

    switch (p_dsnh_param->dsnh_type)
    {
    case SYS_NH_PARAM_DSNH_TYPE_BRGUC:
        SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh_8w);
        dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE;
        dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh_8w.vlan_xlate_mode = 1;       /*humber mode*/
        dsnh_8w.outer_edit_ptr = p_dsnh_param->l2edit_ptr;

        if(p_dsnh_param->logic_port_check)
        {
            dsnh_8w.logic_dest_port     = p_dsnh_param->logic_port;
            dsnh_8w.logic_port_check    = 1;
        }

        if (p_dsnh_param->p_vlan_info)
        {
            dsnh_8w.share_type = SYS_NH_SHARE_TYPE_VLANTAG;

            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh_8w.is_leaf = 1;
            }
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_PASS_THROUGH))
            {
                dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE_INNER;
            }

            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, (sys_nexthop_t*)(&dsnh_8w), p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
        }
        break;
    case SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN:
        if (p_dsnh_param->hvpls)
        {
            op_bridge = SYS_NH_OP_BRIDGE_INNER;
        }
        else
        {
            op_bridge = SYS_NH_OP_BRIDGE_VPLS;
        }

        if (p_dsnh_param->spme_mode)
        {
            op_bridge = SYS_NH_OP_NONE;
        }

        if (p_dsnh_param->p_vlan_info)
        {
            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh_8w.is_leaf = 1;
            }

            if (CTC_FLAG_ISSET(p_dsnh_param->p_vlan_info->flag, CTC_VLAN_NH_ETREE_LEAF))
            {
                dsnh_8w.is_leaf = 1;
            }


            dsnh_8w.vlan_xlate_mode = 1;       /*humber mode*/
            dsnh_8w.payload_operation = op_bridge;
            dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
            dsnh_8w.mtu_check_en = TRUE;

            dsnh_8w.share_type = SYS_NH_SHARE_TYPE_VLANTAG;

            if (p_dsnh_param->spme_mode)
            {
                dsnh_8w.outer_edit_location = 0;
                dsnh_8w.outer_edit_ptr_type = 1;
                dsnh_8w.outer_edit_ptr = p_dsnh_param->l3edit_ptr;

                dsnh_8w.inner_edit_ptr_type = 0;
                dsnh_8w.inner_edit_ptr = p_dsnh_param->inner_l2edit_ptr;
            }
            else
            {
                if(p_dsnh_param->lspedit_ptr || (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP)))
                {
                    dsnh_8w.outer_edit_location = 0;
                    dsnh_8w.outer_edit_ptr_type = 1;
                    dsnh_8w.outer_edit_ptr = p_dsnh_param->lspedit_ptr; /*Pipe0: L3edit*/
                }
                else
                {
                    dsnh_8w.outer_edit_location = 1;
                    dsnh_8w.outer_edit_ptr_type = 0;
                    dsnh_8w.outer_edit_ptr = p_dsnh_param->l2edit_ptr; /*Pipe0: L2edit*/
                }

                dsnh_8w.inner_edit_ptr_type = 1;
                dsnh_8w.inner_edit_ptr = p_dsnh_param->l3edit_ptr;

                CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, (&dsnh_8w) /*Have same structure*/,
                                                                       p_dsnh_param->p_vlan_info));
                CTC_ERROR_RETURN(sys_goldengate_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
            }

            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsnh8w.dest_vlanptr = %d, payloadOp = %d \n",
                           dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation);

        }
        else
        {
            CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
        }

        dsnh_8w.mtu_check_en = TRUE;
        dsnh_8w.cw_index = p_dsnh_param->cw_index;
        dsnh_8w.logic_dest_port     = p_dsnh_param->logic_port;

        break;

    case SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL:
        op_bridge = (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE)) ? 1: 0;
        op_route_no_ttl = (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ROUTE_NOTTL)) ? 1:0;
        dsnh_8w.payload_operation = op_bridge ? SYS_NH_OP_NONE : SYS_NH_OP_ROUTE;

        if (op_bridge)
        {
            dsnh_8w.payload_operation = p_dsnh_param->hvpls?SYS_NH_OP_BRIDGE_INNER: SYS_NH_OP_BRIDGE_VPLS;
        }
        else
        {
            dsnh_8w.payload_operation = op_route_no_ttl? SYS_NH_OP_ROUTE_NOTTL : SYS_NH_OP_ROUTE;
        }

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_STRIP_VLAN))
        {
            dsnh_8w.svlan_tagged = FALSE;
            dsnh_8w.vlan_xlate_mode = 1;       /*humber mode*/
        }
        else
        {
           dsnh_8w.svlan_tagged = TRUE;
        }
        dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh_8w.mtu_check_en = TRUE;

        dsnh_8w.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

       if (p_dsnh_param->inner_l2edit_ptr)
        { /*outer L2Edit*/
           dsnh_8w.outer_edit_location = 0;
           dsnh_8w.outer_edit_ptr_type = 1;
           dsnh_8w.outer_edit_ptr = p_dsnh_param->l3edit_ptr;  /*Pipe0 L3edit*/

           dsnh_8w.inner_edit_ptr_type = 0;
           dsnh_8w.inner_edit_ptr = p_dsnh_param->inner_l2edit_ptr;  /*inner L2edit*/

        }
        else
        {
            dsnh_8w.outer_edit_location = 1;
            dsnh_8w.outer_edit_ptr_type = 0;
            dsnh_8w.outer_edit_ptr = p_dsnh_param->l2edit_ptr;  /*outer L2edit*/

            dsnh_8w.inner_edit_ptr_type = 1;
            dsnh_8w.inner_edit_ptr = p_dsnh_param->l3edit_ptr;  /*L3 edit*/

        }

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ERSPAN_EN))
        {
            dsnh_8w.fid_valid          = 0;
            dsnh_8w.fid                = p_dsnh_param->span_id;
            dsnh_8w.span_en            = 1;
            dsnh_8w.payload_operation = SYS_NH_OP_MIRROR;

			if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ERSPAN_KEEP_IGS_TS))
            {
                dsnh_8w.keep_igs_ts = 1;
            }

        }
        else
        {
            dsnh_8w.fid_valid          = (0 == p_dsnh_param->fid)? 0 : 1;
            dsnh_8w.fid                = p_dsnh_param->fid;
        }

        if (p_dsnh_param->p_vlan_info)  /*misc over l2 edit with inner vlan edit*/
        {
            dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE_INNER;
            dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
            dsnh_8w.vlan_xlate_mode = 1;       /*humber mode*/
            dsnh_8w.share_type = SYS_NH_SHARE_TYPE_VLANTAG;

            SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh_8w);
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, (sys_nexthop_t*)(&dsnh_8w), p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
        }

        dsnh_8w.is_vxlan_route_op  = (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_IP_TUNNEL_STRIP_INNER)) ?
            0 : 1;
        dsnh_8w.logic_port_check   = (0 == p_dsnh_param->logic_port ) ? 0 : 1;
        dsnh_8w.logic_dest_port    = p_dsnh_param->logic_port;

        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dsnh8w.dest_vlanptr = %d, payloadOp = %d \n",
                       dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation);

        break;


    case SYS_NH_PARAM_DSNH_TYPE_XERSPAN:
        dsnh_8w.payload_operation = SYS_NH_OP_MIRROR;
        dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh_8w.mtu_check_en = TRUE;
        dsnh_8w.xerpsan_en = TRUE;
        dsnh_8w.share_type = SYS_NH_SHARE_TYPE_SPAN_PACKET;
        dsnh_8w.outer_edit_location = 0;
        dsnh_8w.outer_edit_ptr_type = 0;
        dsnh_8w.outer_edit_ptr = p_dsnh_param->l2edit_ptr;

        dsnh_8w.inner_edit_ptr_type = 1;
        dsnh_8w.inner_edit_ptr = p_dsnh_param->l3edit_ptr;
        dsnh_8w.hash_num = p_dsnh_param->hash_num;

        dsnh_8w.fid_valid          = 0;
        dsnh_8w.fid                = p_dsnh_param->span_id;
        dsnh_8w.span_en            = 1;

        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_ERSPAN_KEEP_IGS_TS))
        {
            dsnh_8w.keep_igs_ts = 1;
        }


        break;

    case SYS_NH_PARAM_DSNH_TYPE_OF:
        dsnh_8w.mtu_check_en = !(p_dsnh_param->mtu_no_chk);
        if (CTC_FLAG_ISSET(p_dsnh_param->flag, SYS_NH_PARAM_FLAG_MISC_FLEX_PLD_BRIDGE))
        {
            dsnh_8w.payload_operation = SYS_NH_OP_BRIDGE;
            dsnh_8w.svlan_tagged = TRUE;
        }
        else
        {
            dsnh_8w.payload_operation = SYS_NH_OP_NONE;
        }
        dsnh_8w.dest_vlan_ptr  = p_dsnh_param->dest_vlan_ptr;
        dsnh_8w.share_type = SYS_NH_SHARE_TYPE_L23EDIT;

        dsnh_8w.outer_edit_location = 0;
        dsnh_8w.outer_edit_ptr_type = 0; /*Pipe0: L2edit*/
        dsnh_8w.outer_edit_ptr = p_dsnh_param->l2edit_ptr;

        dsnh_8w.inner_edit_ptr_type = 1;/*L3edit*/
        dsnh_8w.inner_edit_ptr = p_dsnh_param->l3edit_ptr;

        if (p_dsnh_param->p_vlan_info)  /* vlan edit from nexthop not from L2edit */
        {
            dsnh_8w.vlan_xlate_mode = 1;       /*humber mode*/
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh_build_vlan_info(lchip, (sys_nexthop_t*)(&dsnh_8w), p_dsnh_param->p_vlan_info));
            CTC_ERROR_RETURN(sys_goldengate_nh_dsnh8w_assign_vid(lchip, &dsnh_8w, p_dsnh_param->p_vlan_info));
        }
        break;

    default:
        return CTC_E_NH_SHOULD_USE_DSNH4W;
    }

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsNH4W :: DestVlanPtr = 0x%x, PldOP = %d ,\n \
                   copyCtagCos = %d, CvlanTagged = %d, DeriveStagCos = %d,\n \
                   MtuCheckEn = %d, OutputCvlanVlid = %d, OutputSvlanVlid = %d,\n \
                   ReplaceCtagCos = %d, StagCfi = %d, StagCos = %d,\n \
                   OutEditLoc = %d, OutEditPtrType = %d, OutEditPtr = %d,\n \
                   InnerEditPtrType = %d, InnerEditPtr = %d\n, \
                   SvlanTagged = %d, TaggedMode = %d, macDa = %s\n ",
                   dsnh_8w.dest_vlan_ptr, dsnh_8w.payload_operation,
                   dsnh_8w.copy_ctag_cos, 0, dsnh_8w.derive_stag_cos,
                   dsnh_8w.mtu_check_en, dsnh_8w.output_cvlan_id_valid, dsnh_8w.output_svlan_id_valid,
                   dsnh_8w.replace_ctag_cos,dsnh_8w.stag_cfi, dsnh_8w.stag_cos,
                   dsnh_8w.outer_edit_location,dsnh_8w.outer_edit_ptr_type, dsnh_8w.outer_edit_ptr,
                   dsnh_8w.inner_edit_ptr_type,dsnh_8w.inner_edit_ptr,
                   dsnh_8w.svlan_tagged, dsnh_8w.tagged_mode, sys_goldengate_output_mac(dsnh_8w.mac_da));

    dsnh_8w.is_nexthop8_w = TRUE;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,
                                                       SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_dsnh_param->dsnh_offset, &dsnh_8w));

    return CTC_E_NONE;
}

/**
 @brief This function is used to get dsfwd info
 */
int32
sys_goldengate_nh_get_entry_dsfwd(uint8 lchip, uint32 dsfwd_offset, void* p_dsfwd)
{
    DsFwd_m dsfwd;
    sys_fwd_t* p_tmp_fwd = 0;
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsfwd);
    p_tmp_fwd = (sys_fwd_t*)p_dsfwd;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                     dsfwd_offset/2, &dsfwd));

    if (dsfwd_offset%2 == 0)
    {
        p_tmp_fwd->dest_map = GetDsFwd(V, array_0_destMap_f, &dsfwd);
    }
    else
    {
        p_tmp_fwd->dest_map = GetDsFwd(V, array_1_destMap_f, &dsfwd);
    }

    return CTC_E_NONE;
}

/**
 @brief This function is used to build and write dsfwd table
 */
int32
sys_goldengate_nh_write_entry_dsfwd(uint8 lchip, sys_nh_param_dsfwd_t* p_dsfwd_param)
{

    sys_fwd_t sys_fwd;
    uint8 gchip = 0;
    uint8 speed = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_dsfwd_param);

    sal_memset(&sys_fwd, 0, sizeof(sys_fwd));


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n\
        lchip = %d, \n\
        dsfwd_offset = 0x%x, \n\
        dest_chip_id = %d, \n\
        dest_id = 0x%x, \n\
        dropPkt = %d, \n\
        dsnh_offset = 0x%x, \n\
        is_mcast = %d\n",
                   lchip,
                   p_dsfwd_param->dsfwd_offset,
                   p_dsfwd_param->dest_chipid,
                   p_dsfwd_param->dest_id,
                   p_dsfwd_param->drop_pkt,
                   p_dsfwd_param->dsnh_offset,
                   p_dsfwd_param->is_mcast);

    if (p_dsfwd_param->dsnh_offset > 0x3FFFF)
    {
        return CTC_E_NH_EXCEED_MAX_DSNH_OFFSET;
    }

    sys_fwd.aps_bridge_en = (p_dsfwd_param->aps_type == CTC_APS_BRIDGE);
    sys_fwd.force_back_en = p_dsfwd_param->is_reflective;
    sys_fwd.bypass_igs_edit = p_dsfwd_param->is_egress_edit;
    sys_fwd.nexthop_ptr = (p_dsfwd_param->dsnh_offset & 0x3FFFF);
    sys_fwd.nexthop_ext = (p_dsfwd_param->nexthop_ext)?TRUE:FALSE;
    sys_fwd.adjust_len_idx = p_dsfwd_param->adjust_len_idx;
    sys_fwd.cut_through_dis = p_dsfwd_param->cut_through_dis;

    if (p_dsfwd_param->drop_pkt)
    {
        CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
        sys_fwd.nexthop_ptr = 0x3FFFF;
        sys_fwd.dest_map = SYS_RSV_PORT_DROP_ID;
    }

    else
    {
        if (p_dsfwd_param->is_lcpu)
        {
            /*default to CPU by CPU,modify destination by QOS API*/
            sys_fwd.dest_map = SYS_ENCODE_EXCP_DESTMAP(p_dsfwd_param->dest_chipid, SYS_PKT_CPU_QDEST_BY_DMA);

        }
        else if (p_dsfwd_param->is_mcast)
        {
             speed = 0;
            sys_fwd.dest_map = SYS_ENCODE_MCAST_IPE_DESTMAP(speed, p_dsfwd_param->dest_id);
            if (p_dsfwd_param->stats_ptr)
            {
                sys_fwd.nexthop_ptr = p_dsfwd_param->stats_ptr & 0x1FFF;
                sys_fwd.nexthop_ptr |= (1 << 13);
            }
        }
        else
        {
            if (p_dsfwd_param->aps_type == CTC_APS_DISABLE && (p_dsfwd_param->dest_chipid != 0x1F)
                && p_dsfwd_param->is_cpu)
            {
                sys_fwd.dest_map = SYS_ENCODE_EXCP_DESTMAP(p_dsfwd_param->dest_chipid,SYS_PKT_CPU_QDEST_BY_DMA);
                sys_fwd.nexthop_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
            }
            else
            {
                sys_fwd.dest_map = SYS_ENCODE_DESTMAP(p_dsfwd_param->dest_chipid, p_dsfwd_param->dest_id);
            }
        }
    }

    sys_fwd.offset = p_dsfwd_param->dsfwd_offset;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                       p_dsfwd_param->dsfwd_offset/2, &sys_fwd));


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_update_dsfwd_lport_en(uint8 lchip, uint32 fwd_offset, uint8 lport_en)
{
    DsFwd_m dsfwd;
    uint32 cmd = 0;
    uint32 value = 0;

    sal_memset(&dsfwd, 0, sizeof(DsFwd_t));

    if (fwd_offset)
    {
        cmd = DRV_IOR(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset/2, cmd, &dsfwd));
        value = lport_en?1:0;
        SetDsFwd(V, array_0_sendLocalPhyPort_f,    &dsfwd,   value);
        cmd = DRV_IOW(DsFwd_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, fwd_offset/2, cmd, &dsfwd));
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Update dsfwd(0x%x) lport en:%d\n", fwd_offset/2, lport_en);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_update_entry_dsfwd(uint8 lchip, uint32 *offset,
                               uint32 dest_map,
                               uint32 dsnh_offset,
                               uint8 dsnh_8w,
                               uint8 del,
                               uint8 critical_packet,
                               uint8 lport_en)
{

    sys_fwd_t sys_fwd;
    sal_memset(&sys_fwd, 0, sizeof(sys_fwd));


	if((*offset) == 0)
	{
		CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, offset));

	}
	else if(del)
	{
	   CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, *offset));

	}
    sys_fwd.nexthop_ptr = (dsnh_offset & 0x3FFFF);
    sys_fwd.nexthop_ext = dsnh_8w;
	sys_fwd.dest_map  = dest_map;
    sys_fwd.offset = *offset;
    sys_fwd.bypass_igs_edit = 1;
    sys_fwd.critical_packet = critical_packet;
    sys_fwd.lport_en = lport_en;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, sys_fwd.offset / 2, &sys_fwd));
    return CTC_E_NONE;

}



#define __________init___________
/**
 @brief This function is used to init resolved dsnexthop for bridge

 */
 STATIC int32
_sys_goldengate_nh_init_dsmet_base(uint8 lchip, uint32 dsmet_base)
{
    uint32 cmd = 0;
    cmd = DRV_IOW(MetFifoCtl_t, MetFifoCtl_dsMetEntryBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &dsmet_base));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_dsnh_init_for_brg(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{
    uint32 nhp_ptr = 0;
    sys_nexthop_t dsnh;
    uint8 offset = 0;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(dsnh));

    offset = SYS_DSNH4WREG_INDEX_FOR_BRG;
    nhp_ptr = SYS_NH_BUILD_INT_OFFSET(offset);

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    SYS_GOLDENGATE_NH_DSNH_SET_DEFAULT_COS_ACTION(dsnh);

    dsnh.svlan_tagged = TRUE;
    dsnh.payload_operation = SYS_NH_OP_BRIDGE;
    dsnh.dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;
    dsnh.offset = nhp_ptr;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_INTERNAL,
                                                       offset/2, &dsnh));

    return CTC_E_NONE;

}

/**
 @brief This function is used to init resolved dsnexthop for mirror
 */
STATIC int32
_sys_goldengate_nh_dsnh_init_for_mirror(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{
    uint32 nhp_ptr = 0;
    sys_nexthop_t dsnh;
    uint8 offset = 0;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(dsnh));

    offset = SYS_DSNH4WREG_INDEX_FOR_MIRROR;
    nhp_ptr = SYS_NH_BUILD_INT_OFFSET(offset);

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    dsnh.vlan_xlate_mode = TRUE;
    dsnh.payload_operation = SYS_NH_OP_MIRROR;
    dsnh.tagged_mode = TRUE;
    dsnh.share_type = SYS_NH_SHARE_TYPE_VLANTAG;
    dsnh.offset = nhp_ptr;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_INTERNAL,
                                                       offset/2, &dsnh));

    return CTC_E_NONE;

}


/**
 @brief This function is used to init resolved dsnexthop for egress bypass
 */
STATIC int32
_sys_goldengate_nh_dsnh_init_for_bypass(uint8 lchip, sys_nh_offset_attr_t* p_offset_attr)
{
    uint32 nhp_ptr = 0;
    sys_nexthop_t dsnh;
    uint8 offset = 0;

    CTC_PTR_VALID_CHECK(p_offset_attr);
    sal_memset(&dsnh, 0, sizeof(dsnh));

    offset = SYS_DSNH4WREG_INDEX_FOR_BYPASS;
    nhp_ptr = SYS_NH_BUILD_INT_OFFSET(offset);

    p_offset_attr->offset_base = nhp_ptr;
    p_offset_attr->entry_size = 1;
    p_offset_attr->entry_num = 1;

    dsnh.next_hop_bypass = TRUE;
    dsnh.offset = nhp_ptr;
    dsnh.is_nexthop8_w = 1;
    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_INTERNAL,
                                                       offset/2, &dsnh));

    return CTC_E_NONE;

}

/**
 This function is used to init resolved dsFwd for loopback to itself
 */
STATIC int32
_sys_goldengate_nh_dsfwd_init_for_reflective(uint8 lchip, uint16* p_offset_attr)
{
    uint32 dsfwd_ptr = 0;
    uint32 nhp_ptr = 0;
    uint8 offset = 0;
    sys_fwd_t dsfwd;

    offset = SYS_DSNH4WREG_INDEX_FOR_BYPASS;
    nhp_ptr = SYS_NH_BUILD_INT_OFFSET(offset);
    sal_memset(&dsfwd, 0, sizeof(dsfwd));
    dsfwd.force_back_en = 1;
    dsfwd.nexthop_ptr = nhp_ptr;
    dsfwd.nexthop_ext = 1;

    CTC_ERROR_RETURN(
    sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1, &dsfwd_ptr));

    dsfwd.offset = dsfwd_ptr;
    CTC_ERROR_RETURN(
        sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_FWD, dsfwd_ptr/2, &dsfwd));

    *p_offset_attr = dsfwd_ptr;

    return CTC_E_NONE;

}


/**
 This function is used to init resolved dsFwd 0 for drop
*/
STATIC int32
_sys_goldengate_nh_dsfwd_init_for_drop(uint8 lchip)
{
    sys_nh_param_dsfwd_t dsfwd_param;

    sal_memset(&dsfwd_param, 0, sizeof(dsfwd_param));
    dsfwd_param.drop_pkt = 1;
    dsfwd_param.dsfwd_offset = 0;
    dsfwd_param.dest_id = 0xFFFF;
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));

    return CTC_E_NONE;
}

/**
 @brief This function is used to init resolved l2edit  for ipmc
 */
STATIC int32
_sys_goldengate_nh_l2edit_init_for_ipmc(uint8 lchip, uint16* p_offset_attr)
{
    uint32 nhp_ptr = 0;
    sys_dsl2edit_t l2edit;

    sal_memset(&l2edit, 0, sizeof(l2edit));
    l2edit.derive_mcast_mac_for_ip = 1;
    l2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    l2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_4W;

    CTC_ERROR_RETURN(
    sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 1,
                                   &nhp_ptr));
    CTC_ERROR_RETURN(
       sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, nhp_ptr, &l2edit));


    *p_offset_attr = nhp_ptr;

    return CTC_E_NONE;

}

/**
 @brief This function is used to init resolved l2edit6w  for ecmp interface
 */
STATIC int32
_sys_goldengate_nh_l2edit_init_for_ecmp_if(uint8 lchip, void** p_dsl2edit_ptr)
{
    uint32 nhp_ptr = 0;
    sys_dsl2edit_t l2edit;
    sys_nh_db_dsl2editeth4w_t* p_new_dsl2edit_4w = NULL;

    sal_memset(&l2edit, 0, sizeof(l2edit));
    l2edit.ds_type  = SYS_NH_DS_TYPE_L2EDIT;
    l2edit.l2_rewrite_type  = SYS_NH_L2EDIT_TYPE_ETH_8W;
    l2edit.is_6w = 1;
    l2edit.use_port_mac = 1;

    CTC_ERROR_RETURN(
    sys_goldengate_nh_offset_alloc_with_multiple(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 2,
                                   1, &nhp_ptr));
    l2edit.offset = nhp_ptr;
    CTC_ERROR_RETURN(
       sys_goldengate_nh_write_asic_table(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, nhp_ptr/2, &l2edit));

    p_new_dsl2edit_4w = (sys_nh_db_dsl2editeth4w_t*)mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (p_new_dsl2edit_4w == NULL)
    {
        sys_goldengate_nh_offset_free(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W, 2, nhp_ptr);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_new_dsl2edit_4w, 0, sizeof(sys_nh_db_dsl2editeth4w_t));

    p_new_dsl2edit_4w->offset = nhp_ptr;
    *p_dsl2edit_ptr = (void*)p_new_dsl2edit_4w;

    return CTC_E_NONE;

}



/**
 @brief This function is used to initilize dynamic offset, inclue offset pool,
        and resolved offset
*/
STATIC int32
_sys_goldengate_nh_offset_init(uint8 lchip, sys_goldengate_nh_master_t* p_master)
{
    sys_goldengate_opf_t opf;
    uint16 tbl_id = 0;
    uint32 tbl_entry_num[5] = {0};
    uint32 glb_tbl_entry_num[5] = {0};
    uint8  dyn_tbl_idx = 0;
    uint8  max_dyn_tbl_idx = 0;
    uint32  entry_enum = 0, nh_glb_entry_num = 0;
    uint32 nexthop_entry_num = 0;
    uint8   dyn_tbl_opf_type = 0;
    uint8  loop = 0;
    uint32 nh_glb_nexthop_entry_num = 0;
    uint32 nh_glb_met_entry_num = 0;
    uint8 split_en = 0;
    sys_ftm_edit_resource_t edit_resource;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /* DsFwd */
    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_FWD].table_id;
    sys_goldengate_ftm_get_dynamic_table_info(lchip, tbl_id,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_FWD].opf_pool_type = dyn_tbl_opf_type;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    max_dyn_tbl_idx = dyn_tbl_idx;

    /* DsMet */
    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].table_id;
    sys_goldengate_ftm_get_dynamic_table_info(lchip, tbl_id,  &dyn_tbl_idx, &entry_enum, &nh_glb_met_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_MET].opf_pool_type = dyn_tbl_opf_type;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_MET_6W].opf_pool_type = dyn_tbl_opf_type;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    glb_tbl_entry_num[dyn_tbl_idx]  = nh_glb_met_entry_num;
    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    /* DsNexthop */
    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].table_id;
    sys_goldengate_ftm_get_dynamic_table_info(lchip, tbl_id ,  &dyn_tbl_idx, &entry_enum, &nh_glb_nexthop_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].opf_pool_type = dyn_tbl_opf_type;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_8W].opf_pool_type = dyn_tbl_opf_type;
    if (g_gg_nh_master[lchip]->nexthop_share)
    {
        if (nh_glb_nexthop_entry_num % 2)
        {
            return  CTC_E_INVALID_PARAM;
        }
        p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_4W].alloc_dir = 1;
        p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_NEXTHOP_8W].alloc_dir = 1;
    }
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    nexthop_entry_num = entry_enum;
    if (p_master->pkt_nh_edit_mode != SYS_NH_IGS_CHIP_EDIT_MODE)
    {
        glb_tbl_entry_num[dyn_tbl_idx]  = g_gg_nh_master[lchip]->nexthop_share?
                                    nh_glb_nexthop_entry_num + nh_glb_met_entry_num : nh_glb_nexthop_entry_num;
    }
    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    /* DsEdit */
    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L2EDIT_3W].table_id;
    sys_goldengate_ftm_get_dynamic_table_info(lchip, tbl_id,  &dyn_tbl_idx, &entry_enum, &nh_glb_entry_num);
    dyn_tbl_opf_type  = OPF_NH_DYN1_SRAM + dyn_tbl_idx;
    tbl_entry_num[dyn_tbl_idx]  = entry_enum;
    max_dyn_tbl_idx = (dyn_tbl_idx > max_dyn_tbl_idx) ? dyn_tbl_idx : max_dyn_tbl_idx;

    for (loop = SYS_NH_ENTRY_TYPE_L2EDIT_FROM; loop <= SYS_NH_ENTRY_TYPE_L3EDIT_TO; loop++)
    {
        p_master->nh_table_info_array[loop].opf_pool_type = dyn_tbl_opf_type;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_get_edit_resource(lchip, &split_en, &edit_resource));
    if(split_en)
    {
        glb_tbl_entry_num[dyn_tbl_idx] = edit_resource.l3_edit_start;
        glb_tbl_entry_num[OPF_NH_DYN5_SRAM] = edit_resource.l2_edit_start;
        if(edit_resource.l2_edit_start == 0)
        {
            /*l2 edit in the start position*/
            tbl_entry_num[dyn_tbl_idx] = edit_resource.l3_edit_num+edit_resource.l3_edit_start;
            tbl_entry_num[OPF_NH_DYN5_SRAM] = edit_resource.l2_edit_num;
        }
        else
        {
            tbl_entry_num[dyn_tbl_idx] = edit_resource.l3_edit_num;
            tbl_entry_num[OPF_NH_DYN5_SRAM] = edit_resource.l2_edit_num+edit_resource.l2_edit_start;
        }

        max_dyn_tbl_idx = (OPF_NH_DYN5_SRAM > max_dyn_tbl_idx) ? OPF_NH_DYN5_SRAM : max_dyn_tbl_idx;
        for (loop = SYS_NH_ENTRY_TYPE_L2EDIT_FROM; loop <= SYS_NH_ENTRY_TYPE_L2EDIT_TO; loop++)
        {
            p_master->nh_table_info_array[loop].opf_pool_type = OPF_NH_DYN5_SRAM;
        }
    }
    /*Init global dsnexthop & dsMet offset*/
    if (p_master->pkt_nh_edit_mode == SYS_NH_IGS_CHIP_EDIT_MODE || (nh_glb_nexthop_entry_num == 0))
    {
        p_master->max_glb_nh_offset  = 0;
    }
    else
    {
        p_master->max_glb_nh_offset  = nh_glb_nexthop_entry_num;
        p_master->p_occupid_nh_offset_bmp = (uint32*)mem_malloc(MEM_NEXTHOP_MODULE,
                                                                (sizeof(uint32) * (p_master->max_glb_nh_offset / BITS_NUM_OF_WORD + 1)));
        if (NULL == p_master->p_occupid_nh_offset_bmp)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_master->p_occupid_nh_offset_bmp, 0,  (sizeof(uint32) * (p_master->max_glb_nh_offset / BITS_NUM_OF_WORD + 1)));
    }

    if (g_gg_nh_master[lchip]->nexthop_share)
    {
        g_gg_nh_master[lchip]->dsmet_base = p_master->max_glb_nh_offset;
        CTC_ERROR_RETURN(_sys_goldengate_nh_init_dsmet_base(lchip, g_gg_nh_master[lchip]->dsmet_base));
    }

    p_master->max_glb_met_offset  = nh_glb_met_entry_num;
    if (p_master->max_glb_met_offset)
    {
        p_master->p_occupid_met_offset_bmp = (uint32*)mem_malloc(MEM_NEXTHOP_MODULE, (sizeof(uint32) * (p_master->max_glb_met_offset / BITS_NUM_OF_WORD + 1)));
        if (NULL == p_master->p_occupid_met_offset_bmp)
        {
            return CTC_E_NO_MEMORY;
        }
        sal_memset(p_master->p_occupid_met_offset_bmp, 0,  (sizeof(uint32) * (p_master->max_glb_met_offset / BITS_NUM_OF_WORD + 1)));
        /*rsv group 0 for stacking keeplive group*/
        CTC_BIT_SET(p_master->p_occupid_met_offset_bmp[0], 0);
    }

    /* init offset */
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    for (loop = 0; loop <= max_dyn_tbl_idx; loop++)
    {
        dyn_tbl_idx = loop;
        /*local nexthop opf min offset should be equal to 1 */
        if (0 == glb_tbl_entry_num[dyn_tbl_idx])
        {
            glb_tbl_entry_num[dyn_tbl_idx] = 1;
        }
        tbl_entry_num[dyn_tbl_idx] = tbl_entry_num[dyn_tbl_idx] - glb_tbl_entry_num[dyn_tbl_idx];

        opf.pool_type = OPF_NH_DYN_SRAM;
        opf.pool_index = OPF_NH_DYN1_SRAM + loop;

        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, glb_tbl_entry_num[dyn_tbl_idx], tbl_entry_num[dyn_tbl_idx]));
    }

    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W].table_id;
    sys_goldengate_ftm_query_table_entry_num(lchip, tbl_id, &entry_enum);
    dyn_tbl_opf_type = OPF_NH_OUTER_L2EDIT;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W].opf_pool_type = dyn_tbl_opf_type;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W].opf_pool_type = dyn_tbl_opf_type;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = dyn_tbl_opf_type;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, entry_enum*2-1));


    tbl_id = p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L3EDIT_SPME].table_id;
    sys_goldengate_ftm_query_table_entry_num(lchip, tbl_id, &entry_enum);
    dyn_tbl_opf_type = OPF_NH_OUTER_SPME;
    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L3EDIT_SPME].opf_pool_type = dyn_tbl_opf_type;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = dyn_tbl_opf_type;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, entry_enum-1));


    p_master->nh_table_info_array[SYS_NH_ENTRY_TYPE_L2EDIT_VLAN_PROFILE].opf_pool_type = OPF_NH_L2EDIT_VLAN_PROFILE;
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = OPF_NH_L2EDIT_VLAN_PROFILE;
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, 63));

     p_master->internal_nexthop_base = 0xFFF;
    if (nexthop_entry_num >= 64*1024)
    {
        sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, 16, 0xFFF0);
    }

    /*1. DsNexthop offset for bridge, */
    CTC_ERROR_RETURN(_sys_goldengate_nh_dsnh_init_for_brg(lchip,
                                                          &(p_master->sys_nh_resolved_offset \
                                                          [SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH])));

    /*2. DsNexthop for bypassall*/
    CTC_ERROR_RETURN(_sys_goldengate_nh_dsnh_init_for_bypass(lchip,
                                                             &(p_master->sys_nh_resolved_offset \
                                                             [SYS_NH_RES_OFFSET_TYPE_BYPASS_NH])));

    /*3. DsNexthop for mirror*/
    CTC_ERROR_RETURN(_sys_goldengate_nh_dsnh_init_for_mirror(lchip,
                                                             &(p_master->sys_nh_resolved_offset \
                                                             [SYS_NH_RES_OFFSET_TYPE_MIRROR_NH])));

    /*4. l2edit for ipmc phy if*/

    CTC_ERROR_RETURN(_sys_goldengate_nh_l2edit_init_for_ipmc(lchip,
                                                             &(p_master->ipmc_resolved_l2edit)));
    CTC_ERROR_RETURN(_sys_goldengate_nh_l2edit_init_for_ecmp_if(lchip,
                                                             &(p_master->ecmp_if_resolved_l2edit)));

    /*5. dsfwd for reflective (loopback)*/
    CTC_ERROR_RETURN(_sys_goldengate_nh_dsfwd_init_for_reflective(lchip,
                                                                  &(p_master->reflective_resolved_dsfwd_offset)));

    CTC_ERROR_RETURN(_sys_goldengate_nh_dsfwd_init_for_drop(lchip));

    /* init for ip tunnel*/
    CTC_ERROR_RETURN(sys_goldengate_nh_ip_tunnel_init(lchip));

    /* init opf for ecmp groupid */
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_ECMP_GROUP_ID, 1));

    opf.pool_type = OPF_ECMP_GROUP_ID;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 1, SYS_NH_ECMP_GROUP_ID_NUM));

    /* init opf for ecmp member*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_ECMP_MEMBER_ID, 1));

    opf.pool_type = OPF_ECMP_MEMBER_ID;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, 0, SYS_NH_ECMP_MEMBER_NUM));

    /* init for mcast group nhid vector*/
    p_master->mcast_group_vec = ctc_vector_init((p_master->max_glb_met_offset/2 + SYS_NH_MCAST_GROUP_MAX_BLK_SIZE -1)/ SYS_NH_MCAST_GROUP_MAX_BLK_SIZE, SYS_NH_MCAST_GROUP_MAX_BLK_SIZE);
    if (NULL == p_master->mcast_group_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_db_dsl2editeth4w_cmp(void* p_data_new, void* p_data_old)
{
    sys_nh_db_dsl2editeth4w_t* p_new = (sys_nh_db_dsl2editeth4w_t*)p_data_new;
    sys_nh_db_dsl2editeth4w_t* p_old = (sys_nh_db_dsl2editeth4w_t*)p_data_old;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if ((NULL == p_new) || (NULL == p_old))
    {
        return -1;
    }

    if (p_new->output_vid < p_old->output_vid)
    {
        return -1;
    }

    if (p_new->output_vid > p_old->output_vid)
    {
        return 1;
    }


    if (p_new->strip_inner_vlan < p_old->strip_inner_vlan)
    {
        return -1;
    }

    if (p_new->strip_inner_vlan > p_old->strip_inner_vlan)
    {
        return 1;
    }

    if (p_new->fpma < p_old->fpma)
    {
        return - 1;
    }

    if (p_new->fpma > p_old->fpma)
    {
        return 1;
    }

    return sal_memcmp(p_new->mac_da, p_old->mac_da, sizeof(mac_addr_t));

}


STATIC int32
_sys_goldengate_nh_db_dsl2editeth8w_cmp(void* p_data_new, void* p_data_old)
{
    sys_nh_db_dsl2editeth8w_t* p_new = (sys_nh_db_dsl2editeth8w_t*)p_data_new;
    sys_nh_db_dsl2editeth8w_t* p_old = (sys_nh_db_dsl2editeth8w_t*)p_data_old;
    int32 ret = CTC_E_NONE;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if ((NULL == p_new) || (NULL == p_old))
    {
        return -1;
    }


    if (p_new->strip_inner_vlan < p_old->strip_inner_vlan)
    {
        return -1;
    }

    if (p_new->strip_inner_vlan > p_old->strip_inner_vlan)
    {
        return 1;
    }

    ret = sal_memcmp(p_new->mac_da, p_old->mac_da, sizeof(mac_addr_t));
    if (ret)
    {
        return ret;
    }

    ret = sal_memcmp(p_new->mac_sa, p_old->mac_sa, sizeof(mac_addr_t));
    if (ret)
    {
        return ret;
    }

    if (p_new->output_vid < p_old->output_vid)
    {
        return -1;
    }

    if (p_new->output_vid > p_old->output_vid)
    {
        return 1;
    }

    if (p_new->packet_type < p_old->packet_type)
    {
        return -1;
    }

    if (p_new->packet_type > p_old->packet_type)
    {
        return 1;
    }

    if (p_new->output_vlan_is_svlan != p_old->output_vlan_is_svlan)
    {
        return -1;
    }

    return ret;
}

STATIC int32
_sys_goldengate_nh_avl_init(uint8 lchip, sys_goldengate_nh_master_t* p_master)
{
    int32 ret;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    ret = ctc_avl_create(&p_master->dsl2edit4w_tree, 0, _sys_goldengate_nh_db_dsl2editeth4w_cmp);
    if (ret)
    {
        return CTC_E_CANT_CREATE_AVL;
    }

    ret = ctc_avl_create(&p_master->dsl2edit8w_tree, 0, _sys_goldengate_nh_db_dsl2editeth8w_cmp);
    if (ret)
    {
        ctc_avl_tree_free(&(p_master->dsl2edit4w_tree), NULL);
        return CTC_E_CANT_CREATE_AVL;
    }

    ret = ctc_avl_create(&p_master->dsl2edit4w_swap_tree, 0, _sys_goldengate_nh_db_dsl2editeth4w_cmp);
    if (ret)
    {
        ctc_avl_tree_free(&(p_master->dsl2edit4w_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit8w_tree), NULL);
        return CTC_E_CANT_CREATE_AVL;
    }


    ret = ctc_avl_create(&p_master->dsl2edit6w_tree, 0, _sys_goldengate_nh_db_dsl2editeth8w_cmp);
    if (ret)
    {
        ctc_avl_tree_free(&(p_master->dsl2edit4w_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit8w_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit4w_swap_tree), NULL);
        return CTC_E_CANT_CREATE_AVL;
    }

    ret = ctc_avl_create(&p_master->dsl2edit3w_tree, 0, _sys_goldengate_nh_db_dsl2editeth4w_cmp);
    if (ret)
    {
        ctc_avl_tree_free(&(p_master->dsl2edit4w_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit8w_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit4w_swap_tree), NULL);
        ctc_avl_tree_free(&(p_master->dsl2edit3w_tree), NULL);
        return CTC_E_CANT_CREATE_AVL;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_external_id_init(uint8 lchip, ctc_vector_t** pp_nhid_vec, uint32 max_external_nhid)
{
    ctc_vector_t* p_nhid_vec;
    uint32 block_size = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store external nh_info*/
    block_size = max_external_nhid / SYS_NH_EXTERNAL_NHID_MAX_BLK_NUM;
    p_nhid_vec = ctc_vector_init(SYS_NH_EXTERNAL_NHID_MAX_BLK_NUM, block_size);
    if (NULL == p_nhid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_nhid_vec = p_nhid_vec;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_internal_id_init(uint8 lchip, ctc_vector_t** pp_nhid_vec, uint32 start_internal_nhid)
{
    ctc_vector_t* p_nhid_vec;
    sys_goldengate_opf_t opf;
    uint32 nh_id_num;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_nhid_vec = ctc_vector_init(SYS_NH_INTERNAL_NHID_MAX_BLK_NUM, SYS_NH_INTERNAL_NHID_BLK_SIZE);
    if (NULL == p_nhid_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_nhid_vec = p_nhid_vec;

    /*2. Init nhid pool, use opf to alloc/free internal nhid*/
    CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, OPF_NH_DYN_SRAM, OPF_NH_DYN_MAX));
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = OPF_NH_NHID_INTERNAL;
    nh_id_num = (SYS_NH_INTERNAL_NHID_MAX_BLK_NUM * \
                 SYS_NH_INTERNAL_NHID_BLK_SIZE);
    CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf,
                                                   start_internal_nhid, nh_id_num));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_tunnel_id_init(uint8 lchip, ctc_vector_t** pp_tunnel_id_vec, uint16 max_tunnel_id)
{
    ctc_vector_t* p_tunnel_id_vec;
    uint16 block_size = (max_tunnel_id + SYS_NH_TUNNEL_ID_MAX_BLK_NUM - 1) / SYS_NH_TUNNEL_ID_MAX_BLK_NUM;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_tunnel_id_vec = ctc_vector_init(SYS_NH_TUNNEL_ID_MAX_BLK_NUM, block_size);
    if (NULL == p_tunnel_id_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_tunnel_id_vec = p_tunnel_id_vec;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_arp_id_init(ctc_vector_t** pp_arp_id_vec, uint16 arp_number)
{
    ctc_vector_t* p_arp_id_vec;
    uint16 block_size = (arp_number + SYS_NH_ARP_ID_MAX_BLK_NUM - 1) / SYS_NH_ARP_ID_MAX_BLK_NUM;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "\n%s()\n", __FUNCTION__);
    /*1. Creat vector, use vector to store internal nh_info*/
    p_arp_id_vec = ctc_vector_init(SYS_NH_ARP_ID_MAX_BLK_NUM, block_size);
    if (NULL == p_arp_id_vec)
    {
        return CTC_E_NO_MEMORY;
    }

    *pp_arp_id_vec = p_arp_id_vec;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_register_callback(uint8 lchip, sys_goldengate_nh_master_t* p_master, uint16 nh_param_type,
                                    p_sys_nh_create_cb_t nh_create_cb, p_sys_nh_delete_cb_t nh_del_cb,
                                    p_sys_nh_update_cb_t nh_update_cb)
{
    p_master->callbacks_nh_create[nh_param_type] = nh_create_cb;
    p_master->callbacks_nh_delete[nh_param_type] = nh_del_cb;
    p_master->callbacks_nh_update[nh_param_type] = nh_update_cb;
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_callback_init(uint8 lchip, sys_goldengate_nh_master_t* p_master)
{
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);



    /* 1. Ucast bridge */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_BRGUC,
                                                         sys_goldengate_nh_create_brguc_cb,
                                                         sys_goldengate_nh_delete_brguc_cb,
                                                         sys_goldengate_nh_update_brguc_cb));



    /* 2. Mcast bridge */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MCAST,
                                                         &sys_goldengate_nh_create_mcast_cb,
                                                         &sys_goldengate_nh_delete_mcast_cb,
                                                         &sys_goldengate_nh_update_mcast_cb));



    /* 3. IPUC */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_IPUC,
                                                         &sys_goldengate_nh_create_ipuc_cb,
                                                         &sys_goldengate_nh_delete_ipuc_cb,
                                                         &sys_goldengate_nh_update_ipuc_cb));

    /* 4. ECMP */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_ECMP,
                                                         &sys_goldengate_nh_create_ecmp_cb,
                                                         &sys_goldengate_nh_delete_ecmp_cb,
                                                         &sys_goldengate_nh_update_ecmp_cb));

    /* 5. MPLS */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MPLS,
                                                         &sys_goldengate_nh_create_mpls_cb,
                                                         &sys_goldengate_nh_delete_mpls_cb,
                                                         &sys_goldengate_nh_update_mpls_cb));

    /* 6. Drop */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_DROP,
                                                         &sys_goldengate_nh_create_special_cb,
                                                         &sys_goldengate_nh_delete_special_cb, NULL));

    /* 7. ToCPU */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_TOCPU,
                                                         &sys_goldengate_nh_create_special_cb,
                                                         &sys_goldengate_nh_delete_special_cb, NULL));

    /* 8. Unresolve */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_UNROV,
                                                         &sys_goldengate_nh_create_special_cb,
                                                         &sys_goldengate_nh_delete_special_cb, NULL));

    /* 9. ILoop */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_ILOOP,
                                                         &sys_goldengate_nh_create_iloop_cb,
                                                         &sys_goldengate_nh_delete_iloop_cb,
                                                         &sys_goldengate_nh_update_iloop_cb));

    /* 10. rspan */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_RSPAN,
                                                         &sys_goldengate_nh_create_rspan_cb,
                                                         &sys_goldengate_nh_delete_rspan_cb,
                                                         NULL));

    /* 11. ip-tunnel */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_IP_TUNNEL,
                                                         &sys_goldengate_nh_create_ip_tunnel_cb,
                                                         &sys_goldengate_nh_delete_ip_tunnel_cb,
                                                         &sys_goldengate_nh_update_ip_tunnel_cb));

    /* 12. trill-tunnel */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_TRILL,
                                                         &sys_goldengate_nh_create_trill_cb,
                                                         &sys_goldengate_nh_delete_trill_cb,
                                                         &sys_goldengate_nh_update_trill_cb));

    /* 13. misc */
    CTC_ERROR_RETURN(_sys_goldengate_nh_register_callback(lchip, p_master,
                                                         SYS_NH_TYPE_MISC,
                                                         &sys_goldengate_nh_create_misc_cb,
                                                         &sys_goldengate_nh_delete_misc_cb,
                                                         &sys_goldengate_nh_update_misc_cb));
    return CTC_E_NONE;
}

/**
 @brief This function is used to create and init nexthop module master data
 */
STATIC int32
_sys_goldengate_nh_master_new(uint8 lchip, sys_goldengate_nh_master_t** pp_nexthop_master,
                                            uint32 boundary_of_extnl_intnl_nhid, uint16 max_tunnel_id, uint16 arp_num)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    int32 ret = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    if (NULL == pp_nexthop_master || (NULL != *pp_nexthop_master))
    {
        return CTC_E_INVALID_PTR;
    }

    /*1. allocate memory for nexthop master*/
    p_master = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_goldengate_nh_master_t));
    if (NULL == p_master)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_master, 0, sizeof(sys_goldengate_nh_master_t));

    /*2. Create Mutex*/
    sal_mutex_create(&p_master->p_mutex);
    if (NULL == p_master->p_mutex)
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        goto error;
    }

    /*3. AVL tree init*/
    CTC_ERROR_GOTO(_sys_goldengate_nh_avl_init(lchip, p_master), ret, error);

    /*4. Nexthop external vector init*/
    CTC_ERROR_GOTO(_sys_goldengate_nh_external_id_init(lchip, (&p_master->external_nhid_vec), boundary_of_extnl_intnl_nhid), ret, error);

    /*5. Nexthop internal vector init*/
    CTC_ERROR_GOTO(_sys_goldengate_nh_internal_id_init(lchip, &(p_master->internal_nhid_vec), boundary_of_extnl_intnl_nhid), ret, error);

    /*6. Tunnel id vector init*/
    CTC_ERROR_GOTO(_sys_goldengate_nh_tunnel_id_init(lchip, &(p_master->tunnel_id_vec), max_tunnel_id), ret, error);

    /*7. Arp id vector init*/
    CTC_ERROR_GOTO(_sys_goldengate_nh_arp_id_init(&(p_master->arp_id_vec), arp_num), ret, error);

    /*8. Eloop list init*/
    p_master->eloop_list = ctc_slist_new();
    if (p_master->eloop_list == NULL)
    {
        ret = CTC_E_INVALID_PARAM;
        goto error;
    }

    /*Return PTR*/
    *pp_nexthop_master = p_master;

    return CTC_E_NONE;
error:
    if (p_master)
    {
        mem_free(p_master);
    }
    if (p_master->p_mutex)
    {
        SYS_NH_DESTROY_LOCK(p_master->p_mutex);
    }
    return ret;
}

STATIC int32
_sys_goldengate_nh_port_init(uint8 lchip)
{
    uint8 lchip_tmp                = 0;
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    uint8  gchip                   = 0;
    uint16 gport                   = 0;
    uint32 index                   = 0;
    int32  ret                     = CTC_E_NONE;

    lchip_tmp = lchip;
    sys_goldengate_get_gchip_id(lchip_tmp, &gchip);

    for (index = 0; index < SYS_GG_MAX_PORT_NUM_PER_CHIP; index++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(index));

        if ((index & 0xFF) < SYS_GG_MAX_PHY_PORT)
        {
            /*init all chip port nexthop*/
            lchip = lchip_tmp;
            CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
            {
                ret = sys_goldengate_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
                if ((CTC_E_ENTRY_EXIST != ret) && (CTC_E_NONE != ret))
                {
                    return ret;
                }
            }
        }
    }

   return CTC_E_NONE;
}

#define __________warmboot___________

sys_nh_db_dsl2editeth4w_t*
_sys_goldengate_nh_wb_lkup_l2edit_4w(uint8 lchip, sys_wb_nh_dsl2edit4w_tree_key_t *p_l2edit)
{

    sys_nh_db_dsl2editeth4w_t dsl2editeth4w;

    sal_memcpy(&dsl2editeth4w.mac_da[0], &p_l2edit->mac_da[0], sizeof(mac_addr_t));
    dsl2editeth4w.output_vid = p_l2edit->output_vid;
    dsl2editeth4w.strip_inner_vlan = p_l2edit->strip_inner_vlan;
    dsl2editeth4w.fpma = p_l2edit->fpma;
    return sys_goldengate_nh_lkup_route_l2edit( lchip, &dsl2editeth4w);  /*DsL2Edit*/

}

int32 _sys_goldengate_nh_wb_l2edit_4w_mapping(sys_nh_db_dsl2editeth4w_t* p_l2editeth4w,sys_wb_nh_dsl2edit4w_tree_t *p_wb_dsl2edit4w,uint8 sync)
{
      if(sync)
      {
           sal_memcpy(&p_wb_dsl2edit4w->l2edit_key.mac_da[0], &p_l2editeth4w->mac_da[0],  sizeof(mac_addr_t));
           p_wb_dsl2edit4w->offset =  p_l2editeth4w->offset ;
           p_wb_dsl2edit4w->is_ecmp_if =  p_l2editeth4w->is_ecmp_if ;
           p_wb_dsl2edit4w->l2edit_key.strip_inner_vlan =    p_l2editeth4w->strip_inner_vlan ;
           p_wb_dsl2edit4w->ref_cnt =    p_l2editeth4w->ref_cnt ;

           p_wb_dsl2edit4w->l2edit_key.output_vid =  p_l2editeth4w->output_vid ;
           p_wb_dsl2edit4w->output_vlan_is_svlan =  p_l2editeth4w->output_vlan_is_svlan ;
           p_wb_dsl2edit4w->dynamic =   p_l2editeth4w->dynamic ;
           p_wb_dsl2edit4w->l2edit_key.fpma =  p_l2editeth4w->fpma ;

      }
	  else
	  {
            sal_memcpy(&p_l2editeth4w->mac_da[0], &p_wb_dsl2edit4w->l2edit_key.mac_da[0], sizeof(mac_addr_t));
           p_l2editeth4w->offset = p_wb_dsl2edit4w->offset;
           p_l2editeth4w->is_ecmp_if = p_wb_dsl2edit4w->is_ecmp_if;
           p_l2editeth4w->strip_inner_vlan = p_wb_dsl2edit4w->l2edit_key.strip_inner_vlan;
           p_l2editeth4w->ref_cnt = p_wb_dsl2edit4w->ref_cnt;

           p_l2editeth4w->output_vid = p_wb_dsl2edit4w->l2edit_key.output_vid;
           p_l2editeth4w->output_vlan_is_svlan = p_wb_dsl2edit4w->output_vlan_is_svlan;
           p_l2editeth4w->dynamic =  p_wb_dsl2edit4w->dynamic;
           p_l2editeth4w->fpma = p_wb_dsl2edit4w->l2edit_key.fpma;
	  }

    return CTC_E_NONE;
}


int32 sys_goldengate_nh_wb_mapping_ecmp_nhinfo(uint8 lchip, sys_nh_info_com_t* p_nh_info_com,sys_wb_nh_info_com_t *p_wb_nh_info)
{
    sys_nh_info_ecmp_t  *p_nh_info = (sys_nh_info_ecmp_t  *)p_nh_info_com;


    p_wb_nh_info->info.ecmp.ecmp_cnt =  p_nh_info->ecmp_cnt;
    p_wb_nh_info->info.ecmp.valid_cnt =  p_nh_info->valid_cnt;
    p_wb_nh_info->info.ecmp.type =   p_nh_info->type ;
    p_wb_nh_info->info.ecmp.failover_en =   p_nh_info->failover_en;
    p_wb_nh_info->info.ecmp.ecmp_nh_id =  p_nh_info->ecmp_nh_id ;

    p_wb_nh_info->info.ecmp.ecmp_group_id =  p_nh_info->ecmp_group_id;
    p_wb_nh_info->info.ecmp.random_rr_en =  p_nh_info->random_rr_en;
    p_wb_nh_info->info.ecmp.stats_valid =   p_nh_info->stats_valid ;
    p_wb_nh_info->info.ecmp.gport =   p_nh_info->gport;
    p_wb_nh_info->info.ecmp.ecmp_member_base =  p_nh_info->ecmp_member_base ;
	p_wb_nh_info->info.ecmp.l3edit_offset_base =   p_nh_info->l3edit_offset_base;
    p_wb_nh_info->info.ecmp.l2edit_offset_base =  p_nh_info->l2edit_offset_base ;


    sal_memcpy(&p_wb_nh_info->info.ecmp.nh_array[0], p_nh_info->nh_array,p_nh_info->ecmp_cnt*sizeof(uint32));

    if (p_nh_info->type == CTC_NH_ECMP_TYPE_DLB)
    {
        sal_memcpy(&p_wb_nh_info->info.ecmp.member_id_array[0], p_nh_info->member_id_array, g_gg_nh_master[lchip]->max_ecmp*sizeof(uint8));
        sal_memcpy(&p_wb_nh_info->info.ecmp.entry_count_array[0], p_nh_info->entry_count_array, g_gg_nh_master[lchip]->max_ecmp*sizeof(uint8));
    }

    return CTC_E_NONE;
}

int32 sys_goldengate_nh_wb_mapping_misc_nhinfo(sys_nh_info_com_t* p_nh_info_com, sys_wb_nh_info_com_t *p_wb_nh_info,uint8 sync)
{
    sys_nh_info_misc_t  *p_nh_info = (sys_nh_info_misc_t  *)p_nh_info_com;


    if (sync)
    {

        p_wb_nh_info->info.misc.dsl2edit_offset = p_nh_info->dsl2edit_offset ;
        p_wb_nh_info->info.misc.dsl3edit_offset = p_nh_info->dsl3edit_offset ;

        p_wb_nh_info->info.misc.gport =  p_nh_info->gport ;
        p_wb_nh_info->info.misc.is_swap_mac = p_nh_info->is_swap_mac ;
        p_wb_nh_info->info.misc.is_reflective = p_nh_info->is_reflective ;

        p_wb_nh_info->info.misc.misc_nh_type = p_nh_info->misc_nh_type;
        p_wb_nh_info->info.misc.l3edit_entry_type = p_nh_info->l3edit_entry_type;
        p_wb_nh_info->info.misc.l2edit_entry_type = p_nh_info->l2edit_entry_type;
    }
    else
    {
        p_nh_info->dsl2edit_offset = p_wb_nh_info->info.misc.dsl2edit_offset;
        p_nh_info->dsl3edit_offset = p_wb_nh_info->info.misc.dsl3edit_offset;

        p_nh_info->gport = p_wb_nh_info->info.misc.gport;
        p_nh_info->is_swap_mac = p_wb_nh_info->info.misc.is_swap_mac;
        p_nh_info->is_reflective = p_wb_nh_info->info.misc.is_reflective;

        p_nh_info->misc_nh_type = p_wb_nh_info->info.misc.misc_nh_type;
        p_nh_info->l3edit_entry_type = p_wb_nh_info->info.misc.l3edit_entry_type;
        p_nh_info->l2edit_entry_type = p_wb_nh_info->info.misc.l2edit_entry_type;
        ;
    }
    return CTC_E_NONE;

}

int32 sys_goldengate_nh_wb_mapping_brguc_nhinfo(sys_nh_info_com_t* p_nh_info_com, sys_wb_nh_info_com_t *p_wb_nh_info, uint8 sync)
{
    sys_nh_info_brguc_t  *p_nh_info = (sys_nh_info_brguc_t  *)p_nh_info_com;
    if (sync)
    {
        p_wb_nh_info->info.brguc.dest_gport = p_nh_info->dest_gport;
        p_wb_nh_info->info.brguc.nh_sub_type = p_nh_info->nh_sub_type ;
        p_wb_nh_info->info.brguc.dest_logic_port =  p_nh_info->dest_logic_port ;
    }
    else
    {

        p_nh_info->dest_gport = p_wb_nh_info->info.brguc.dest_gport;
        p_nh_info->nh_sub_type = p_wb_nh_info->info.brguc.nh_sub_type;
        p_nh_info->dest_logic_port = p_wb_nh_info->info.brguc.dest_logic_port;
    }

    return CTC_E_NONE;

}

int32 sys_goldengate_nh_wb_mapping_special_nhinfo(sys_nh_info_com_t* p_nh_info_com,sys_wb_nh_info_com_t *p_wb_nh_info,uint8 sync)
{
   sys_nh_info_special_t  *p_nh_info = (sys_nh_info_special_t  *)p_nh_info_com;

   if(sync)
   {
    p_wb_nh_info->info.spec.dest_gport = p_nh_info->dest_gport ;
   }
   else
   {
       p_nh_info->dest_gport = p_wb_nh_info->info.spec.dest_gport;
   }
   return CTC_E_NONE;

}

extern int32
_sys_goldengate_nh_ipuc_update_dsnh_cb(uint8 lchip, uint32 nh_offset, void* p_arp_db);

int32 sys_goldengate_nh_wb_mapping_ipuc_nhinfo(uint8 lchip, sys_nh_info_com_t* p_nh_info_com, sys_wb_nh_info_com_t *p_wb_nh_info, uint8 sync)
{
    sys_nh_info_ipuc_t  *p_nh_info = (sys_nh_info_ipuc_t  *)p_nh_info_com;

    if (sync)
    {
        sal_memcpy(&p_wb_nh_info->info.ipuc.mac_da, &p_nh_info->mac_da[0], sizeof(mac_addr_t));
        p_wb_nh_info->info.ipuc.flag =  p_nh_info->flag;
        p_wb_nh_info->info.ipuc.l3ifid =  p_nh_info->l3ifid;
        p_wb_nh_info->info.ipuc.gport =   p_nh_info->gport ;
        p_wb_nh_info->info.ipuc.arp_id =   p_nh_info->arp_id;
        p_wb_nh_info->info.ipuc.stats_id =  p_nh_info->stats_id ;

        if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            sal_memcpy(&p_wb_nh_info->info.ipuc.l2edit.mac_da[0], &p_nh_info->p_dsl2edit->mac_da[0], sizeof(mac_addr_t));
            p_wb_nh_info->info.ipuc.l2edit.output_vid = p_nh_info->p_dsl2edit->output_vid;
            p_wb_nh_info->info.ipuc.l2edit.strip_inner_vlan = p_nh_info->p_dsl2edit->strip_inner_vlan;
            p_wb_nh_info->info.ipuc.l2edit.fpma = p_nh_info->p_dsl2edit->fpma;
        }

        if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
            {
                sal_memcpy(&p_wb_nh_info->info.ipuc.protect_l2edit.mac_da[0], &p_nh_info->protection_path->p_dsl2edit->mac_da[0], sizeof(mac_addr_t));
                p_wb_nh_info->info.ipuc.protect_l2edit.output_vid = p_nh_info->protection_path->p_dsl2edit->output_vid;
                p_wb_nh_info->info.ipuc.protect_l2edit.strip_inner_vlan = p_nh_info->protection_path->p_dsl2edit->strip_inner_vlan;
                p_wb_nh_info->info.ipuc.protect_l2edit.fpma = p_nh_info->protection_path->p_dsl2edit->fpma;
            }
            p_wb_nh_info->info.ipuc.protection_arp_id = p_nh_info->protection_path->arp_id;
            p_wb_nh_info->info.ipuc.protection_l3ifid = p_nh_info->protection_path->l3ifid;
            sal_memcpy(&p_wb_nh_info->info.ipuc.protection_mac_da[0], &p_nh_info->protection_path->mac_da[0], sizeof(mac_addr_t));
        }

    }
    else
    {
        sys_nh_db_arp_t *p_arp = NULL;
        sal_memcpy(&p_nh_info->mac_da[0], &p_wb_nh_info->info.ipuc.mac_da, sizeof(mac_addr_t));
        p_nh_info->flag = p_wb_nh_info->info.ipuc.flag;
        p_nh_info->l3ifid = p_wb_nh_info->info.ipuc.l3ifid;
        p_nh_info->gport = p_wb_nh_info->info.ipuc.gport;
        p_nh_info->arp_id = p_wb_nh_info->info.ipuc.arp_id;
        p_nh_info->stats_id = p_wb_nh_info->info.ipuc.stats_id;

        p_arp = ctc_vector_get(g_gg_nh_master[lchip]->arp_id_vec, p_nh_info->arp_id);
        if(p_arp)
        {
        	p_arp->updateNhp = (updatenh_fn)_sys_goldengate_nh_ipuc_update_dsnh_cb;
        }

        if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
        {
            p_nh_info->p_dsl2edit = _sys_goldengate_nh_wb_lkup_l2edit_4w( lchip, &p_wb_nh_info->info.ipuc.l2edit);  /*DsL2Edit*/
        }

        if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_APS_EN))
        {
            p_nh_info->protection_path  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_ipuc_edit_info_t));
            if (NULL == p_nh_info->protection_path)
            {
                return CTC_E_NO_MEMORY;
            }
            p_nh_info->protection_path->arp_id = p_wb_nh_info->info.ipuc.protection_arp_id;
            p_nh_info->protection_path->l3ifid = p_wb_nh_info->info.ipuc.protection_l3ifid;
            sal_memcpy(&p_nh_info->protection_path->mac_da[0], &p_wb_nh_info->info.ipuc.protection_mac_da[0], sizeof(mac_addr_t));

            p_nh_info->protection_path->p_dsl2edit = _sys_goldengate_nh_wb_lkup_l2edit_4w(lchip, &p_wb_nh_info->info.ipuc.protect_l2edit);  /*DsL2Edit*/
        }
    }

    return CTC_E_NONE;
}


int32 sys_goldengate_nh_wb_mapping_rspan_nhinfo(sys_nh_info_com_t* p_nh_info_com,sys_wb_nh_info_com_t *p_wb_nh_info,uint8 sync)
{
   sys_nh_info_rspan_t  *p_nh_info = (sys_nh_info_rspan_t  *)p_nh_info_com;
   if(sync)
   {
       p_wb_nh_info->info.rspan.remote_chip = p_nh_info->remote_chip ;
   }
   else
   {
     p_nh_info->remote_chip= p_wb_nh_info->info.rspan.remote_chip;
   }

    return CTC_E_NONE;
}
int32 sys_goldengate_nh_wb_mapping_arp(sys_nh_db_arp_t* p_db_arp, sys_wb_nh_arp_t *p_wb_arp, uint8 sync)
{

    if (sync)
    {
        sal_memcpy(&p_wb_arp->mac_da, &p_db_arp->mac_da, sizeof(mac_addr_t));
        p_wb_arp->flag                 =  p_db_arp->flag ;
        p_wb_arp->l3if_type            =  p_db_arp->l3if_type;
        p_wb_arp->offset               =  p_db_arp->offset ;
        p_wb_arp->in_l2_offset         =  p_db_arp->in_l2_offset;
        p_wb_arp->output_vid           =  p_db_arp->output_vid ;
        p_wb_arp->output_vlan_is_svlan =  p_db_arp->output_vlan_is_svlan;
        p_wb_arp->packet_type          =  p_db_arp->packet_type;
        p_wb_arp->gport                =  p_db_arp->gport ;
        p_wb_arp->nh_offset            =  p_db_arp->nh_offset;
    }
    else
    {
        sal_memcpy(&p_db_arp->mac_da, &p_wb_arp->mac_da, sizeof(mac_addr_t));
        p_db_arp->flag                 = p_wb_arp->flag;
        p_db_arp->l3if_type            = p_wb_arp->l3if_type ;
        p_db_arp->offset               = p_wb_arp->offset ;
        p_db_arp->in_l2_offset         = p_wb_arp->in_l2_offset ;
        p_db_arp->output_vid           = p_wb_arp->output_vid ;
        p_db_arp->output_vlan_is_svlan = p_wb_arp->output_vlan_is_svlan;
        p_db_arp->packet_type          = p_wb_arp->packet_type;
        p_db_arp->gport                = p_wb_arp->gport;
        p_db_arp->nh_offset            = p_wb_arp->nh_offset ;
    }
    return CTC_E_NONE;
}
int32 _sys_goldengate_nh_wb_restore_nhinfo(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

    sys_wb_nh_info_com_t      *p_wb_nh_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    uint32 db_entry_size = 0;
    uint16 entry_cnt = 0;
    int ret = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = OPF_NH_NHID_INTERNAL;
    opf.multiple  = 1;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_info_com_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_INFO_COM);

    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);
    p_wb_nh_info = (sys_wb_nh_info_com_t*)p_wb_query->buffer + entry_cnt++ ;

    _sys_goldengate_nh_get_nhentry_size_by_nhtype(lchip, p_wb_nh_info->nh_type, &db_entry_size);
    p_nhinfo = mem_malloc(MEM_NEXTHOP_MODULE, db_entry_size);
    if (NULL == p_nhinfo)
    {
        return CTC_E_NO_MEMORY;
    }

    p_nhinfo->hdr.adjust_len = p_wb_nh_info->hdr.adjust_len;
    p_nhinfo->hdr.dsnh_entry_num = p_wb_nh_info->hdr.dsnh_entry_num;
    p_nhinfo->hdr.nh_entry_type = p_wb_nh_info->nh_type;
    p_nhinfo->hdr.nh_entry_flags = p_wb_nh_info->hdr.nh_entry_flags;
    p_nhinfo->hdr.dsfwd_info.dsfwd_offset = p_wb_nh_info->hdr.dsfwd_offset;
    p_nhinfo->hdr.dsfwd_info.stats_ptr = p_wb_nh_info->hdr.stats_ptr;
    p_nhinfo->hdr.dsfwd_info.dsnh_offset = p_wb_nh_info->hdr.dsnh_offset;

    if (p_nhinfo->hdr.dsfwd_info.dsnh_offset < g_gg_nh_master[lchip]->max_glb_nh_offset)
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            sys_goldengate_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, p_wb_nh_info->hdr.dsnh_entry_num*2 , TRUE);
        }
        else
        {
            sys_goldengate_nh_set_glb_nh_offset(lchip, p_nhinfo->hdr.dsfwd_info.dsnh_offset, p_wb_nh_info->hdr.dsnh_entry_num , TRUE);

        }
    }
    else if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH))
    {
        if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_8W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset);
        }
        else
        {
            sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W, p_nhinfo->hdr.dsnh_entry_num, p_nhinfo->hdr.dsfwd_info.dsnh_offset);
        }
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                     1, p_nhinfo->hdr.dsfwd_info.dsfwd_offset);
    }
    switch(p_wb_nh_info->nh_type)
    {
    case SYS_NH_TYPE_IPUC:
        sys_goldengate_nh_wb_mapping_ipuc_nhinfo(lchip, p_nhinfo, p_wb_nh_info, 0);
        break;
    case SYS_NH_TYPE_TOCPU:
    case SYS_NH_TYPE_DROP:
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_ILOOP:
        sys_goldengate_nh_wb_mapping_special_nhinfo(p_nhinfo, p_wb_nh_info, 0);
        break;
    case SYS_NH_TYPE_MISC:
        sys_goldengate_nh_wb_mapping_misc_nhinfo(p_nhinfo, p_wb_nh_info, 0);
        break;
    case SYS_NH_TYPE_BRGUC:
        sys_goldengate_nh_wb_mapping_brguc_nhinfo(p_nhinfo, p_wb_nh_info, 0);
        break;
    case SYS_NH_TYPE_RSPAN:
        sys_goldengate_nh_wb_mapping_rspan_nhinfo(p_nhinfo, p_wb_nh_info, 0);
        break;
    default:
        break;
    }

    if (p_wb_nh_info->nh_id <  g_gg_nh_master[lchip]->max_external_nhid )
    {
        ctc_vector_add(g_gg_nh_master[lchip]->external_nhid_vec,  p_wb_nh_info->nh_id, p_nhinfo);
    }
    else
    {
        ctc_vector_add(g_gg_nh_master[lchip]->internal_nhid_vec, (p_wb_nh_info->nh_id - g_gg_nh_master[lchip]->max_external_nhid), p_nhinfo);
        sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_wb_nh_info->nh_id);

    }
    g_gg_nh_master[lchip]->nhid_used_cnt[p_wb_nh_info->nh_type]++;
    CTC_WB_QUERY_ENTRY_END(p_wb_query);

    done:
    return ret;
}
extern int32
sys_goldengate_nh_wb_restore_ecmp_nhinfo(uint8 lchip,  sys_nh_info_com_t* p_nh_info_com,sys_wb_nh_info_com_t *p_wb_nh_info);

int32 _sys_goldengate_nh_wb_restore_ecmp(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

    sys_wb_nh_info_com_t      *p_wb_nh_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    uint32 db_entry_size = 0;
    uint16 entry_cnt = 0;
    int ret = 0;


    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_info_com_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_ECMP);

    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);
    p_wb_nh_info = (sys_wb_nh_info_com_t*)p_wb_query->buffer + entry_cnt++ ;

    _sys_goldengate_nh_get_nhentry_size_by_nhtype(lchip, p_wb_nh_info->nh_type, &db_entry_size);
    p_nhinfo = mem_malloc(MEM_NEXTHOP_MODULE, db_entry_size);
    if (NULL == p_nhinfo)
    {
        return CTC_E_NO_MEMORY;
    }

    p_nhinfo->hdr.adjust_len = p_wb_nh_info->hdr.adjust_len;
    p_nhinfo->hdr.dsnh_entry_num = p_wb_nh_info->hdr.dsnh_entry_num;
    p_nhinfo->hdr.nh_entry_type = p_wb_nh_info->nh_type;
    p_nhinfo->hdr.nh_entry_flags = p_wb_nh_info->hdr.nh_entry_flags;
    p_nhinfo->hdr.dsfwd_info.dsfwd_offset = p_wb_nh_info->hdr.dsfwd_offset;
    p_nhinfo->hdr.dsfwd_info.stats_ptr = p_wb_nh_info->hdr.stats_ptr;
    p_nhinfo->hdr.dsfwd_info.dsnh_offset = p_wb_nh_info->hdr.dsnh_offset;

    sys_goldengate_nh_wb_restore_ecmp_nhinfo(lchip, p_nhinfo, p_wb_nh_info);

    if (p_wb_nh_info->nh_id <  g_gg_nh_master[lchip]->max_external_nhid )
    {
        ctc_vector_add(g_gg_nh_master[lchip]->external_nhid_vec,  p_wb_nh_info->nh_id, p_nhinfo);
        g_gg_nh_master[lchip]->nhid_used_cnt[p_wb_nh_info->nh_type]++;
    }

    CTC_WB_QUERY_ENTRY_END(p_wb_query);

    done:
    return ret;
}
int32 _sys_goldengate_nh_wb_restore_mcast_member(uint8 lchip, sys_nh_info_mcast_t* p_mcast_info,
	                                                         sys_wb_nh_info_mcast_t *p_wb_mcast_info,
	                                                         sys_nh_mcast_meminfo_t* p_member_info,
	                                                         uint16 loop)
{
    /*member info*/
    p_member_info->dsmet.flag = p_wb_mcast_info->flag;
    p_member_info->dsmet.logic_port = p_wb_mcast_info->logic_port;
    p_member_info->dsmet.dsmet_offset = p_wb_mcast_info->dsmet_offset;
    p_member_info->dsmet.next_dsmet_offset = p_wb_mcast_info->next_dsmet_offset;

   if (CTC_FLAG_ISSET(p_wb_mcast_info->flag, SYS_NH_DSMET_FLAG_USE_PBM))
   {
   }
    p_member_info->dsmet.dsnh_offset = p_wb_mcast_info->dsnh_offset;
    p_member_info->dsmet.ref_nhid = p_wb_mcast_info->ref_nhid ;
    p_member_info->dsmet.ucastid = p_wb_mcast_info->ucastid;
    p_member_info->dsmet.replicate_num = p_wb_mcast_info->replicate_num;
	sal_memcpy(p_member_info->dsmet.pbm,p_wb_mcast_info->pbm,4*sizeof(uint32));

    p_member_info->dsmet.free_dsnh_offset_cnt = p_wb_mcast_info->free_dsnh_offset_cnt;
    p_member_info->dsmet.member_type = p_wb_mcast_info->member_type  ;
    p_member_info->dsmet.port_type = p_wb_mcast_info->port_type  ;
    p_member_info->dsmet.entry_type = p_wb_mcast_info->entry_type  ;
    if (p_wb_mcast_info->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
    {
      p_member_info->dsmet.vid_list[loop] = p_wb_mcast_info->vid;
	  p_member_info->dsmet.vid =p_member_info->dsmet.vid_list[0];
    }
	else
	{
	   p_member_info->dsmet.vid = p_wb_mcast_info->vid;
	}

    return CTC_E_NONE;
}
int32 _sys_goldengate_nh_wb_restore_mcast(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{
    sys_wb_nh_info_mcast_t *p_wb_mcast_info = NULL;
    sys_nh_info_mcast_t    *p_mcast_info = NULL;
    sys_nh_mcast_meminfo_t	*p_member_info = NULL;
    uint32 last_met_offset = 0;
    uint32 last_nhid = 0;
    sys_goldengate_opf_t opf;
    uint16 loop = 0;
    uint16 entry_cnt = 0;
	uint16 physical_replication_loop = 0;
	uint8 add_member_flag = 0;
	uint8 entry_num = 0;
    int ret = 0;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_NH_DYN_SRAM;
    opf.pool_index = OPF_NH_NHID_INTERNAL;
    opf.multiple  = 1;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_info_mcast_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_INFO_MCAST);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_mcast_info = (sys_wb_nh_info_mcast_t*)p_wb_query->buffer + entry_cnt++ ;
    if (last_nhid != p_wb_mcast_info->nh_id)
    {
        p_mcast_info = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_info_mcast_t));
        if (NULL == p_mcast_info)
        {
            return CTC_E_NO_MEMORY;
        }
        ctc_list_pointer_init(&p_mcast_info->p_mem_list);
    	if (CTC_FLAG_ISSET(p_wb_mcast_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
        {
            sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_FWD,
                                                         1, p_wb_mcast_info->hdr.dsfwd_offset);
        }
	    physical_replication_loop = 0;
        add_member_flag = 0;
        p_member_info = NULL;
    }

    if ((p_wb_mcast_info->physical_replication_num != 0)
		&& (last_met_offset != p_wb_mcast_info->dsmet_offset))
    {
        p_member_info = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_mcast_meminfo_t));
        if (NULL == p_member_info)
        {
            return CTC_E_NO_MEMORY;
        }

        if ( p_wb_mcast_info->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
        {
            p_member_info->dsmet.vid_list = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(uint16) * p_wb_mcast_info->replicate_num);
            if (NULL == p_member_info->dsmet.vid_list)
            {
                return CTC_E_NO_MEMORY;
            }
        }

        if (p_wb_mcast_info->dsmet_offset != p_wb_mcast_info->basic_met_offset)
        {
            sys_goldengate_nh_offset_alloc_from_position(lchip, p_wb_mcast_info->entry_type, 1, p_wb_mcast_info->dsmet_offset);
        }
        add_member_flag = 0;
        loop = 0;
	}

    p_mcast_info->nhid = p_wb_mcast_info->nh_id;
    p_mcast_info->basic_met_offset = p_wb_mcast_info->basic_met_offset;
    p_mcast_info->physical_replication_num = p_wb_mcast_info->physical_replication_num;
    p_mcast_info->is_mirror = p_wb_mcast_info->is_mirror ;
    p_mcast_info->mirror_ref_cnt = p_wb_mcast_info->mirror_ref_cnt;

    p_mcast_info->hdr.adjust_len = p_wb_mcast_info->hdr.adjust_len;
    p_mcast_info->hdr.dsnh_entry_num = p_wb_mcast_info->hdr.dsnh_entry_num;
    p_mcast_info->hdr.nh_entry_flags = p_wb_mcast_info->hdr.nh_entry_flags;
    p_mcast_info->hdr.nh_entry_type = SYS_NH_TYPE_MCAST;
    p_mcast_info->hdr.dsfwd_info.dsfwd_offset = p_wb_mcast_info->hdr.dsfwd_offset;
    p_mcast_info->hdr.dsfwd_info.stats_ptr = p_wb_mcast_info->hdr.stats_ptr;
    p_mcast_info->hdr.dsfwd_info.dsnh_offset = p_wb_mcast_info->hdr.dsnh_offset;
    p_mcast_info->hdr.dsfwd_info.dsnh_offset = SYS_DSNH_INDEX_FOR_NONE;

    if (p_wb_mcast_info->physical_replication_num != 0)
    {/*mcast group have some members*/
         _sys_goldengate_nh_wb_restore_mcast_member( lchip, p_mcast_info,
                                                   p_wb_mcast_info,
                                                   p_member_info,
                                                   loop);
        if (p_wb_mcast_info->member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
        {
            if ((loop++)  == p_wb_mcast_info->replicate_num)
            {
                physical_replication_loop += 1 ;
				entry_num = p_wb_mcast_info->replicate_num + p_wb_mcast_info->free_dsnh_offset_cnt + 1;
				sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_NEXTHOP_4W,entry_num, p_wb_mcast_info->dsnh_offset);
     			add_member_flag = 1;
            }

        }
        else
        {
            if (CTC_FLAG_ISSET(p_wb_mcast_info->flag, SYS_NH_DSMET_FLAG_USE_PBM))
            {
                physical_replication_loop = physical_replication_loop + p_wb_mcast_info->replicate_num + 1;
            }
            else
            {
                physical_replication_loop += 1 ;
            }
			add_member_flag = 1;
        }
	}
	 /*add one member to mcast group*/
	if(add_member_flag)
	{
       ctc_list_pointer_insert_tail(&p_mcast_info->p_mem_list, &(p_member_info->list_head));
	}
    /*add one mcast group*/
    if (physical_replication_loop == p_wb_mcast_info->physical_replication_num )
    {
        if (p_mcast_info->nhid <  g_gg_nh_master[lchip]->max_external_nhid )
        {
            if (FALSE == ctc_vector_add(g_gg_nh_master[lchip]->external_nhid_vec,  p_mcast_info->nhid, p_mcast_info))
            {
                return CTC_E_NO_MEMORY;
            }
        }
        else
        {
            if (FALSE == ctc_vector_add(g_gg_nh_master[lchip]->internal_nhid_vec, (p_mcast_info->nhid - g_gg_nh_master[lchip]->max_external_nhid), p_mcast_info))
            {
                return CTC_E_NO_MEMORY;
            }
            sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_mcast_info->nhid);

        }
        CTC_ERROR_RETURN(sys_goldengate_nh_set_glb_met_offset(lchip, p_mcast_info->basic_met_offset, 2, TRUE));

        if (FALSE ==  ctc_vector_add(g_gg_nh_master[lchip]->mcast_group_vec, p_mcast_info->basic_met_offset/2, p_mcast_info))
        {
            return CTC_E_NO_MEMORY;
        }
        g_gg_nh_master[lchip]->nhid_used_cnt[SYS_NH_TYPE_MCAST]++;
    }
    last_met_offset = p_wb_mcast_info->dsmet_offset;
    last_nhid = p_wb_mcast_info->nh_id;
    CTC_WB_QUERY_ENTRY_END(p_wb_query);


  done:
    return ret;
}
int32 _sys_goldengate_nh_wb_restore_mpls_tunnel(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

    sys_wb_nh_mpls_tunnel_t    *p_wb_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;

    uint16 loop1, loop2;
    uint16 entry_cnt = 0;
    int ret = 0;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_mpls_tunnel_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_MPLS_TUNNEL);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_mpls_tunnel = (sys_wb_nh_mpls_tunnel_t*)p_wb_query->buffer + entry_cnt++ ;
    p_mpls_tunnel  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_mpls_tunnel_t));
    if (NULL == p_mpls_tunnel)
    {
        return CTC_E_NO_MEMORY;
    }

    p_mpls_tunnel->tunnel_id = p_wb_mpls_tunnel->tunnel_id;
    p_mpls_tunnel->gport = p_wb_mpls_tunnel->gport;
    p_mpls_tunnel->l3ifid =  p_wb_mpls_tunnel->l3ifid;
    p_mpls_tunnel->ref_cnt =  p_wb_mpls_tunnel->ref_cnt;
    p_mpls_tunnel->flag  = p_wb_mpls_tunnel->flag;
    p_mpls_tunnel->label_num  = p_wb_mpls_tunnel->label_num;
    sal_memcpy(&p_mpls_tunnel->l2edit_offset[0][0], &p_wb_mpls_tunnel->l2edit_offset[0][0],
               sizeof(uint16) *SYS_NH_APS_M *SYS_NH_APS_M);
    sal_memcpy(&p_mpls_tunnel->spme_offset[0][0], &p_wb_mpls_tunnel->spme_offset[0][0],
               sizeof(uint16) *SYS_NH_APS_M *SYS_NH_APS_M);
    sal_memcpy(&p_mpls_tunnel->lsp_offset[0], &p_wb_mpls_tunnel->lsp_offset[0],
               sizeof(uint16) *SYS_NH_APS_M);
    sal_memcpy(&p_mpls_tunnel->arp_id[0][0], &p_wb_mpls_tunnel->arp_id[0][0],
               sizeof(uint16) *SYS_NH_APS_M * SYS_NH_APS_M);

    for (loop1 = 0; loop1 < SYS_NH_APS_M; loop1++)
    {
        for (loop2 = 0; loop2 < SYS_NH_APS_M; loop2++)
        {
            p_mpls_tunnel->p_dsl2edit_4w[loop1][loop2] =
            _sys_goldengate_nh_wb_lkup_l2edit_4w(lchip, &p_wb_mpls_tunnel->l2edit[loop1][loop2]);
        }
    }

    ctc_vector_add(g_gg_nh_master[lchip]->tunnel_id_vec, p_mpls_tunnel->tunnel_id, p_mpls_tunnel);
    CTC_WB_QUERY_ENTRY_END(p_wb_query);
 done:
    return ret;
}

int32 _sys_goldengate_nh_wb_restore_arp(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

	sys_nh_db_arp_t* p_db_arp = NULL;
	sys_wb_nh_arp_t *p_wb_arp = NULL;

    uint16 entry_cnt = 0;
    int ret = 0;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_arp_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_ARP);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_arp = (sys_wb_nh_arp_t*)p_wb_query->buffer + entry_cnt++ ;
    p_db_arp  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_arp_t));
    if (NULL == p_db_arp)
    {
        return CTC_E_NO_MEMORY;
    }

    sys_goldengate_nh_wb_mapping_arp(p_db_arp,p_wb_arp,0);

    if(FALSE == ctc_vector_add(g_gg_nh_master[lchip]->arp_id_vec, p_wb_arp->arp_id, p_db_arp))
    {
    	return CTC_E_NO_RESOURCE;
   	}
    CTC_WB_QUERY_ENTRY_END(p_wb_query)
	done:
    return ret;
}

int32 _sys_goldengate_nh_wb_restore_l2edit_4w(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

    sys_nh_db_dsl2editeth4w_t *p_l2editeth4w;
    sys_wb_nh_dsl2edit4w_tree_t  *p_wb_l2edit4w;
    uint16 entry_cnt = 0;
    int ret = 0;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_dsl2edit4w_tree_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_DSL2EDIT4W_TREE);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_l2edit4w = (sys_wb_nh_dsl2edit4w_tree_t*)p_wb_query->buffer + entry_cnt++;
    p_l2editeth4w  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_db_dsl2editeth4w_t));
    if (NULL == p_l2editeth4w)
    {
        return CTC_E_NO_MEMORY;
    }
    _sys_goldengate_nh_wb_l2edit_4w_mapping(p_l2editeth4w, p_wb_l2edit4w, FALSE);
    sys_goldengate_nh_offset_alloc_from_position(lchip, SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W,
                                                 1, p_wb_l2edit4w->offset);
    _sys_goldengate_nh_insert_route_l2edit(lchip, p_l2editeth4w);

    CTC_WB_QUERY_ENTRY_END(p_wb_query);

    done:
    return ret;
}

int32 _sys_goldengate_nh_wb_restore_brguc_node(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

    sys_hbnh_brguc_node_info_t *p_nh_brguc_node;
    sys_wb_nh_brguc_node_t    *p_wb_brguc_node;
    uint16 entry_cnt = 0;
    int ret = 0;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_brguc_node_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_BRGUC_NODE);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_brguc_node = (sys_wb_nh_brguc_node_t*)p_wb_query->buffer + entry_cnt++;
    p_nh_brguc_node  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_hbnh_brguc_node_info_t));
    if (NULL == p_nh_brguc_node)
    {
        return CTC_E_NO_MEMORY;
    }
    p_nh_brguc_node->nhid_brguc = p_wb_brguc_node->nhid_brguc;
    p_nh_brguc_node->nhid_bypass = p_wb_brguc_node->nhid_bypass;

    if(FALSE == ctc_vector_add(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector, p_wb_brguc_node->port_index, p_nh_brguc_node))
    {
    	return CTC_E_NO_RESOURCE;
   	}
	CTC_WB_QUERY_ENTRY_END(p_wb_query);
    done:
    return ret;
}
int32 _sys_goldengate_nh_wb_restore_nh_master(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{
    sys_wb_nh_master_t    *p_nh_master;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_master_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_MASTER);
    CTC_ERROR_RETURN(ctc_wb_query_entry(p_wb_query));

    if (p_wb_query->valid_cnt != 0)
    {
        p_nh_master = (sys_wb_nh_master_t*)p_wb_query->buffer;
        g_gg_nh_master[lchip]->ipmc_logic_replication =  p_nh_master->ipmc_logic_replication;
        g_gg_nh_master[lchip]->ipmc_port_bitmap =  p_nh_master->ipmc_port_bitmap;
        g_gg_nh_master[lchip]->no_dsfwd =  p_nh_master->no_dsfwd;
        g_gg_nh_master[lchip]->rspan_nh_in_use =  p_nh_master->rspan_nh_in_use;
        sal_memcpy(g_gg_nh_master[lchip]->cw_ref_cnt, p_nh_master->cw_ref_cnt, SYS_NH_CW_NUM*sizeof(uint16));
        g_gg_nh_master[lchip]->max_ecmp =  p_nh_master->max_ecmp;
        g_gg_nh_master[lchip]->cur_ecmp_cnt =  p_nh_master->cur_ecmp_cnt;
        g_gg_nh_master[lchip]->rspan_vlan_id =  p_nh_master->rspan_vlan_id;
    }
    return CTC_E_NONE;
}


int32
_sys_goldengate_nh_wb_restore_vni(uint8 lchip,  ctc_wb_query_t *p_wb_query)
{

	sys_nh_vni_mapping_key_t* p_db_vni = NULL;
	sys_wb_nh_vni_fid_t *p_wb_vni = NULL;
    sys_goldengate_opf_t opf;

    uint16 entry_cnt = 0;
    int ret = 0;

    CTC_WB_INIT_QUERY_T(p_wb_query, sys_wb_nh_arp_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_VNI);
    CTC_WB_QUERY_ENTRY_BEGIN(p_wb_query);

    p_wb_vni = (sys_wb_nh_vni_fid_t*)p_wb_query->buffer + entry_cnt++ ;
    p_db_vni  = mem_malloc(MEM_NEXTHOP_MODULE, sizeof(sys_nh_vni_mapping_key_t));
    if (NULL == p_db_vni)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No memory \n");
		return CTC_E_NO_MEMORY;
    }

    p_db_vni->vn_id = p_wb_vni->vn_id;
    p_db_vni->fid = p_wb_vni->fid;
    p_db_vni->ref_cnt = p_wb_vni->ref_cnt;

    if(NULL == ctc_hash_insert(g_gg_nh_master[lchip]->vxlan_vni_hash, p_db_vni))
    {
    	SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " No Memory \n");
    	return CTC_E_NO_MEMORY;
   	}

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_OVERLAY_TUNNEL_INNER_FID;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_wb_vni->fid));
    CTC_WB_QUERY_ENTRY_END(p_wb_query);
 done:
    return ret;
}

int32 sys_goldengate_nh_wb_restore(uint8 lchip)
{
    ctc_wb_query_t    wb_query;
    int32 ret = 0;

   /*restore nh_matser*/
    wb_query.buffer = mem_malloc(MEM_NEXTHOP_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*restore nh_matser*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_nh_master(lchip,&wb_query),ret,done);

   /*restore brguc_node*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_brguc_node(lchip,&wb_query),ret,done);
   /*restore l2Edit*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_l2edit_4w(lchip,&wb_query),ret,done);
   /*restore mpls tunnel*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_mpls_tunnel(lchip,&wb_query),ret,done);
   /*restore arp*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_arp(lchip,&wb_query),ret,done);

    /*restore  nexthop info*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_nhinfo(lchip,&wb_query),ret,done);
   /*restore  mcast info*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_mcast(lchip,&wb_query),ret,done);

   /*restore  ecmp info*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_ecmp(lchip,&wb_query),ret,done);
   /*restore  vni mapping*/
   CTC_ERROR_GOTO(_sys_goldengate_nh_wb_restore_vni(lchip,&wb_query),ret,done);
done:
    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return 0;
}

int32 _sys_goldengate_nh_wb_traverse_sync_l2edit_4w(void *data, void *user_data)
{
    int32 ret;
    sys_nh_db_dsl2editeth4w_t *p_l2editeth4w = (sys_nh_db_dsl2editeth4w_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_nh_dsl2edit4w_tree_t *p_wb_dsl2edit4w;
    uint16 max_buffer_cnt = wb_data->buffer_len/(wb_data->key_len + wb_data->data_len);

    p_wb_dsl2edit4w = (sys_wb_nh_dsl2edit4w_tree_t *)wb_data->buffer + wb_data->valid_cnt;

    _sys_goldengate_nh_wb_l2edit_4w_mapping(p_l2editeth4w,p_wb_dsl2edit4w,TRUE);


    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
            return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32 _sys_goldengate_nh_wb_traverse_sync_brguc_node(void *data,uint32 vec_index, void *user_data)
{
    int32 ret;
    sys_hbnh_brguc_node_info_t *p_nh_brguc_node = (sys_hbnh_brguc_node_info_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_nh_brguc_node_t   *p_wb_brguc_node;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    p_wb_brguc_node = (sys_wb_nh_brguc_node_t *)wb_data->buffer + wb_data->valid_cnt;

    p_wb_brguc_node->port_index = vec_index;
    p_wb_brguc_node->nhid_brguc = p_nh_brguc_node->nhid_brguc;
    p_wb_brguc_node->nhid_bypass = p_nh_brguc_node->nhid_bypass;


    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_nh_wb_traverse_sync_vni(void* bucket_data, void* user_data)
{
    int32 ret;
    sys_traverse_t *wb_traverse = (sys_traverse_t*)user_data;
    sys_nh_vni_mapping_key_t *p_db_vni = (sys_nh_vni_mapping_key_t*)bucket_data;
    ctc_wb_data_t *wb_data = wb_traverse->data;
    sys_wb_nh_vni_fid_t   *p_wb_vni;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    p_wb_vni = (sys_wb_nh_vni_fid_t *)wb_data->buffer + wb_data->valid_cnt;

    p_wb_vni->vn_id = p_db_vni->vn_id;
    p_wb_vni->fid = p_db_vni->fid;
    p_wb_vni->ref_cnt = p_db_vni->ref_cnt;

    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32 _sys_goldengate_nh_wb_traverse_sync_arp(void *data,uint32 vec_index, void *user_data)
{
    int32 ret;
    sys_nh_db_arp_t *p_db_arp = (sys_nh_db_arp_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_nh_arp_t   *p_wb_arp;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    p_wb_arp = (sys_wb_nh_arp_t *)wb_data->buffer + wb_data->valid_cnt;

    sys_goldengate_nh_wb_mapping_arp(p_db_arp,p_wb_arp,1);
    p_wb_arp->arp_id = vec_index;

    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}
int32 _sys_goldengate_nh_wb_traverse_sync_nhinfo(void *data,uint32 vec_index, void *user_data)
{
    int32 ret;
    sys_nh_info_com_t *p_nh_info_com = (sys_nh_info_com_t*)data;
    sys_traverse_t *wb_traverse = (sys_traverse_t*)user_data;
    ctc_wb_data_t *wb_data = wb_traverse->data;
    sys_wb_nh_info_com_t   *p_wb_nh_info;
    uint16 max_buffer_cnt = wb_data->buffer_len/(wb_data->key_len + wb_data->data_len);

    if((wb_traverse->value3 && p_nh_info_com->hdr.nh_entry_type != SYS_NH_TYPE_ECMP)
   	|| (!wb_traverse->value3 && p_nh_info_com->hdr.nh_entry_type == SYS_NH_TYPE_ECMP))
    {

       return CTC_E_NONE;
    }
    p_wb_nh_info = (sys_wb_nh_info_com_t *)wb_data->buffer + wb_data->valid_cnt;

    sal_memset(p_wb_nh_info, 0, sizeof(sys_wb_nh_info_com_t));
    p_wb_nh_info->nh_id   = wb_traverse->value2 ?(vec_index + g_gg_nh_master[wb_traverse->value1]->max_external_nhid) :vec_index;
    p_wb_nh_info->nh_type =  p_nh_info_com->hdr.nh_entry_type;
    p_wb_nh_info->hdr.adjust_len =  p_nh_info_com->hdr.adjust_len;

    p_wb_nh_info->hdr.dsnh_entry_num = p_nh_info_com->hdr.dsnh_entry_num;
    p_wb_nh_info->hdr.nh_entry_flags = p_nh_info_com->hdr.nh_entry_flags;
    p_wb_nh_info->hdr.dsfwd_offset = p_nh_info_com->hdr.dsfwd_info.dsfwd_offset;
    p_wb_nh_info->hdr.stats_ptr = p_nh_info_com->hdr.dsfwd_info.stats_ptr;
    p_wb_nh_info->hdr.dsnh_offset = p_nh_info_com->hdr.dsfwd_info.dsnh_offset;

    switch(p_wb_nh_info->nh_type)
    {
        case SYS_NH_TYPE_IPUC:
            sys_goldengate_nh_wb_mapping_ipuc_nhinfo(wb_traverse->value1,p_nh_info_com, p_wb_nh_info, 1);
            break;
        case SYS_NH_TYPE_BRGUC:
            sys_goldengate_nh_wb_mapping_brguc_nhinfo(p_nh_info_com, p_wb_nh_info, 1);
            break;
        case SYS_NH_TYPE_MISC:
            sys_goldengate_nh_wb_mapping_misc_nhinfo(p_nh_info_com, p_wb_nh_info, 1);
            break;
        case SYS_NH_TYPE_TOCPU:
        case SYS_NH_TYPE_DROP:
        case SYS_NH_TYPE_UNROV:
        case SYS_NH_TYPE_ILOOP:
            sys_goldengate_nh_wb_mapping_special_nhinfo(p_nh_info_com, p_wb_nh_info, 1);
            break;
        case SYS_NH_TYPE_RSPAN:
            sys_goldengate_nh_wb_mapping_rspan_nhinfo(p_nh_info_com, p_wb_nh_info, 1);
            break;
        case SYS_NH_TYPE_ECMP:
            sys_goldengate_nh_wb_mapping_ecmp_nhinfo(wb_traverse->value1,p_nh_info_com, p_wb_nh_info);
            break;
        default:
			return CTC_E_NONE; /*don't sync up*/
            break;
    }

    if (++wb_data->valid_cnt ==  max_buffer_cnt)
    {
        ret = ctc_wb_add_entry(wb_data);
        if ( ret != CTC_E_NONE )
        {
           return ret;
        }
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}
int32 _sys_goldengate_nh_wb_traverse_sync_mcast(void *data,uint32 vec_index, void *user_data)
{
    sys_nh_info_mcast_t *p_nh_info = (sys_nh_info_mcast_t*)data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t*)user_data;
    sys_wb_nh_info_mcast_t   wb_mcast_info;
    sys_nh_mcast_meminfo_t *p_meminfo;
    ctc_list_pointer_node_t* p_list_node;
    uint16 loop = 0,loop_cnt = 0;
    uint16 max_buffer_cnt = wb_data->buffer_len / (wb_data->key_len + wb_data->data_len);

    if((p_nh_info->physical_replication_num == 0)
	  || ctc_list_pointer_empty(&(p_nh_info->p_mem_list)))
  	{ /*no member*/

	   sal_memset(&wb_mcast_info,0,sizeof(sys_wb_nh_info_mcast_t));
       wb_mcast_info.nh_id  = p_nh_info->nhid;
       wb_mcast_info.basic_met_offset = p_nh_info->basic_met_offset;
       wb_mcast_info.dsmet_offset = p_nh_info->basic_met_offset;
       wb_mcast_info.physical_replication_num = 0;
       wb_mcast_info.is_mirror = p_nh_info->is_mirror;
       wb_mcast_info.mirror_ref_cnt = p_nh_info->mirror_ref_cnt;

       wb_mcast_info.hdr.adjust_len = p_nh_info->hdr.adjust_len;
       wb_mcast_info.hdr.dsnh_entry_num = p_nh_info->hdr.dsnh_entry_num;
       wb_mcast_info.hdr.nh_entry_flags = p_nh_info->hdr.nh_entry_flags;
       wb_mcast_info.hdr.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
       wb_mcast_info.hdr.stats_ptr = p_nh_info->hdr.dsfwd_info.stats_ptr;
       wb_mcast_info.hdr.dsnh_offset = p_nh_info->hdr.dsfwd_info.dsnh_offset;

	   sal_memcpy( (uint8*)wb_data->buffer + wb_data->valid_cnt * sizeof(sys_wb_nh_info_mcast_t),
              (uint8*)&wb_mcast_info, sizeof(sys_wb_nh_info_mcast_t));

	   if (++wb_data->valid_cnt ==  max_buffer_cnt)
        {
          if ( ctc_wb_add_entry(wb_data) < 0 )
          {
            return CTC_E_NO_RESOURCE;
          }
          wb_data->valid_cnt = 0;
        }
	   return CTC_E_NONE;
  	}


    CTC_LIST_POINTER_LOOP(p_list_node, &p_nh_info->p_mem_list)
    {
         p_meminfo = _ctc_container_of(p_list_node, sys_nh_mcast_meminfo_t, list_head);

         sal_memset(&wb_mcast_info,0,sizeof(sys_wb_nh_info_mcast_t));
         wb_mcast_info.nh_id  = p_nh_info->nhid;
         wb_mcast_info.basic_met_offset = p_nh_info->basic_met_offset;
         wb_mcast_info.physical_replication_num = p_nh_info->physical_replication_num;
         wb_mcast_info.is_mirror = p_nh_info->is_mirror;
         wb_mcast_info.mirror_ref_cnt = p_nh_info->mirror_ref_cnt;

         wb_mcast_info.hdr.adjust_len = p_nh_info->hdr.adjust_len;
         wb_mcast_info.hdr.dsnh_entry_num = p_nh_info->hdr.dsnh_entry_num;
         wb_mcast_info.hdr.nh_entry_flags = p_nh_info->hdr.nh_entry_flags;
         wb_mcast_info.hdr.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;
         wb_mcast_info.hdr.stats_ptr = p_nh_info->hdr.dsfwd_info.stats_ptr;
         wb_mcast_info.hdr.dsnh_offset = p_nh_info->hdr.dsfwd_info.dsnh_offset;

        /*member info*/

         wb_mcast_info.dsmet_offset = p_meminfo->dsmet.dsmet_offset;
         wb_mcast_info.next_dsmet_offset = p_meminfo->dsmet.next_dsmet_offset;

         wb_mcast_info.flag = p_meminfo->dsmet.flag;
         wb_mcast_info.logic_port = p_meminfo->dsmet.logic_port;

         wb_mcast_info.dsnh_offset = p_meminfo->dsmet.dsnh_offset;
         wb_mcast_info.ref_nhid = p_meminfo->dsmet.ref_nhid;
         wb_mcast_info.ucastid = p_meminfo->dsmet.ucastid;
         wb_mcast_info.replicate_num = p_meminfo->dsmet.replicate_num;
		 sal_memcpy( wb_mcast_info.pbm,p_meminfo->dsmet.pbm,4*sizeof(uint32));

         wb_mcast_info.free_dsnh_offset_cnt = p_meminfo->dsmet.free_dsnh_offset_cnt;
         wb_mcast_info.member_type = p_meminfo->dsmet.member_type;
         wb_mcast_info.port_type = p_meminfo->dsmet.port_type;
         wb_mcast_info.entry_type = p_meminfo->dsmet.entry_type;
        if(wb_mcast_info.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP)
        {
        	loop_cnt = p_meminfo->dsmet.replicate_num + 1;
        }
        else
        {
            loop_cnt  = 1;
        }

        for (loop = 0; loop < loop_cnt; loop++)
        {
            if (wb_mcast_info.member_type == SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP && (p_meminfo->dsmet.replicate_num > 0))
            {
                wb_mcast_info.vid =  p_meminfo->dsmet.vid_list[loop];
            }
            else
            {
                wb_mcast_info.vid = p_meminfo->dsmet.vid;
            }
            sal_memcpy( (uint8*)wb_data->buffer + wb_data->valid_cnt * sizeof(sys_wb_nh_info_mcast_t),
              (uint8*)&wb_mcast_info, sizeof(sys_wb_nh_info_mcast_t));

            if (++wb_data->valid_cnt ==  max_buffer_cnt)
            {
                if ( ctc_wb_add_entry(wb_data) < 0 )
                {
                    return CTC_E_NO_RESOURCE;
                }
                wb_data->valid_cnt = 0;
            }
        }
    }

    return CTC_E_NONE;
}
int32 sys_goldengate_nh_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_nh_master_t  wb_nh_master;
    sys_traverse_t  wb_nh_traverse;

    sal_memset(&wb_nh_master, 0, sizeof(sys_wb_nh_master_t));
    /*syncup  nh_matser*/
    wb_data.buffer = mem_malloc(MEM_NEXTHOP_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_master_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_MASTER);
    wb_nh_master.lchip = lchip;
    wb_nh_master.ipmc_logic_replication = g_gg_nh_master[lchip]->ipmc_logic_replication ;
    wb_nh_master.ipmc_port_bitmap = g_gg_nh_master[lchip]->ipmc_port_bitmap ;
    wb_nh_master.no_dsfwd = g_gg_nh_master[lchip]->no_dsfwd ;
    wb_nh_master.rspan_nh_in_use = g_gg_nh_master[lchip]->rspan_nh_in_use;
    sal_memcpy(wb_nh_master.cw_ref_cnt, g_gg_nh_master[lchip]->cw_ref_cnt,  SYS_NH_CW_NUM*sizeof(uint16));
    wb_nh_master.max_ecmp = g_gg_nh_master[lchip]->max_ecmp ;
    wb_nh_master.cur_ecmp_cnt = g_gg_nh_master[lchip]->cur_ecmp_cnt ;
    wb_nh_master.rspan_vlan_id = g_gg_nh_master[lchip]->rspan_vlan_id ;
    wb_nh_master.rsv0 = 0 ;
    sal_memcpy((uint8*)wb_data.buffer, (uint8*)&wb_nh_master, sizeof(sys_wb_nh_master_t));

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

   	/*sync brguc nodex*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_brguc_node_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_BRGUC_NODE);
 	ctc_vector_traverse2(p_gg_nh_api_master[lchip]->brguc_info.brguc_vector,0,_sys_goldengate_nh_wb_traverse_sync_brguc_node,&wb_data);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncl2Edit*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_dsl2edit4w_tree_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_DSL2EDIT4W_TREE);
  	ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit4w_tree,_sys_goldengate_nh_wb_traverse_sync_l2edit_4w,&wb_data);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

	/*sync arp*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_arp_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_BRGUC_NODE);
 	ctc_vector_traverse2(g_gg_nh_master[lchip]->arp_id_vec,0,_sys_goldengate_nh_wb_traverse_sync_brguc_node,&wb_data);

   /*sync external nexthop info*/
    wb_nh_traverse.data = &wb_data;
    wb_nh_traverse.value1 = lchip;
    wb_nh_traverse.value2 = 0;
    wb_nh_traverse.value3 = 0;
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_info_com_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_INFO_COM);
	ctc_vector_traverse2(g_gg_nh_master[lchip]->external_nhid_vec,CTC_NH_RESERVED_NHID_MAX,_sys_goldengate_nh_wb_traverse_sync_nhinfo,&wb_nh_traverse);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync inernal nexthop info*/
    wb_nh_traverse.value2 = 1;
    wb_nh_traverse.value3 = 0;
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_info_com_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_INFO_COM);
	ctc_vector_traverse2(g_gg_nh_master[lchip]->internal_nhid_vec,0,_sys_goldengate_nh_wb_traverse_sync_nhinfo,&wb_nh_traverse);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*sync ecmp nexthop*/
    wb_nh_traverse.value2 = 0;
    wb_nh_traverse.value3 = 1;

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_info_com_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_ECMP);
	ctc_vector_traverse2(g_gg_nh_master[lchip]->external_nhid_vec,CTC_NH_RESERVED_NHID_MAX,_sys_goldengate_nh_wb_traverse_sync_nhinfo,&wb_nh_traverse);

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }
	/*sync mcast*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_info_mcast_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_INFO_MCAST);
	ctc_vector_traverse2(g_gg_nh_master[lchip]->mcast_group_vec,0,_sys_goldengate_nh_wb_traverse_sync_mcast,&wb_data);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

	/*sync vni*/
    wb_nh_traverse.data = &wb_data;
    wb_nh_traverse.value1 = lchip;
    wb_nh_traverse.value2 = 0;
    wb_nh_traverse.value3 = 0;
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_nh_vni_fid_t, CTC_FEATURE_NEXTHOP, SYS_WB_APPID_NH_SUBID_VNI);
 	ctc_hash_traverse(g_gg_nh_master[lchip]->vxlan_vni_hash, _sys_goldengate_nh_wb_traverse_sync_vni,&wb_nh_traverse);
done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_nh_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg)
{
    sys_goldengate_nh_master_t* p_master = NULL;
    sal_time_t tv;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    LCHIP_CHECK(lchip);
    /*Nexthop module have initialize*/
    if (NULL != g_gg_nh_master[lchip])
    {
        return CTC_E_NONE;
    }

    if (nh_cfg->max_external_nhid == 0)
    {
        nh_cfg->max_external_nhid = SYS_NH_EXTERNAL_NHID_DEFAUL_NUM;
    }

    if (nh_cfg->max_tunnel_id == 0)
    {
        nh_cfg->max_tunnel_id = SYS_NH_TUNNEL_ID_DEFAUL_NUM;
    }

    /*1. Create master*/
    CTC_ERROR_RETURN(_sys_goldengate_nh_master_new(lchip, &p_master, nh_cfg->max_external_nhid, nh_cfg->max_tunnel_id, SYS_NH_ARP_ID_DEFAUL_NUM));

    g_gg_nh_master[lchip] = p_master;
    g_gg_nh_master[lchip]->max_external_nhid = nh_cfg->max_external_nhid;
    g_gg_nh_master[lchip]->max_tunnel_id = nh_cfg->max_tunnel_id;
    g_gg_nh_master[lchip]->acl_redirect_fwd_ptr_num = nh_cfg->acl_redirect_fwd_ptr_num;

   if(nh_cfg->nh_edit_mode == CTC_NH_EDIT_MODE_SINGLE_CHIP)
    {
       g_gg_nh_master[lchip]->pkt_nh_edit_mode = SYS_NH_IGS_CHIP_EDIT_MODE;
    }
    else if(nh_cfg->nh_edit_mode == 2)
    {
        g_gg_nh_master[lchip]->pkt_nh_edit_mode  = SYS_NH_EGS_CHIP_EDIT_MODE ;
    }
    else
    {
        g_gg_nh_master[lchip]->pkt_nh_edit_mode  = SYS_NH_MISC_CHIP_EDIT_MODE ;
    }

    SYS_GOLDENGATE_NH_MAX_ECMP_CHECK(nh_cfg->max_ecmp);
     g_gg_nh_master[lchip]->max_ecmp  = (nh_cfg->max_ecmp > SYS_GG_MAX_ECPN)?SYS_GG_MAX_ECPN :nh_cfg->max_ecmp;
    if (0 == nh_cfg->max_ecmp)
    {
        g_gg_nh_master[lchip]->max_ecmp_group_num = SYS_NH_ECMP_GROUP_ID_NUM;
    }
    else
    {
        g_gg_nh_master[lchip]->max_ecmp_group_num = SYS_NH_ECMP_MEMBER_NUM / nh_cfg->max_ecmp;
        if( g_gg_nh_master[lchip]->max_ecmp_group_num > SYS_NH_ECMP_GROUP_ID_NUM)
        {
             g_gg_nh_master[lchip]->max_ecmp_group_num = SYS_NH_ECMP_GROUP_ID_NUM;
        }
    }

     g_gg_nh_master[lchip]->reflective_brg_en = 0;
     g_gg_nh_master[lchip]->ipmc_logic_replication = 0;

     g_gg_nh_master[lchip]->ip_use_l2edit = 0;
     g_gg_nh_master[lchip]->nexthop_share = sys_goldengate_ftm_get_nexthop_share_mode(lchip);
    /*Set control bridge mcast disable*/
    CTC_ERROR_RETURN(sys_goldengate_bpe_set_port_extend_mcast_en(lchip, 0));

    /*2. Install Semantic callback*/
    if (CTC_E_NONE != _sys_goldengate_nh_callback_init(lchip, g_gg_nh_master[lchip]))
    {
        mem_free( g_gg_nh_master[lchip]);
         g_gg_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    if (CTC_E_NONE != sys_goldengate_nh_table_info_init(lchip))
    {
        mem_free( g_gg_nh_master[lchip]);
         g_gg_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    /*3. Offset  init*/
    if (CTC_E_NONE != _sys_goldengate_nh_offset_init(lchip, p_master))
    {
        mem_free( g_gg_nh_master[lchip]);
         g_gg_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    /*4. global cfg init*/
    if (CTC_E_NONE != sys_goldengate_nh_global_cfg_init(lchip))
    {
        mem_free( g_gg_nh_master[lchip]);
         g_gg_nh_master[lchip] = NULL;
        return CTC_E_NOT_INIT;
    }

    if (CTC_E_NONE != sys_goldengate_nh_misc_init(lchip))
    {
         mem_free( g_gg_nh_master[lchip]);
         g_gg_nh_master[lchip] = NULL;
         return CTC_E_NOT_INIT;
    }

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_NEXTHOP, sys_goldengate_nh_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_wb_restore(lchip));
    }
    if (CTC_WB_STATUS(lchip) != CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(_sys_goldengate_nh_port_init(lchip));
    }
    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_ECMP_GROUP_NUM, g_gg_nh_master[lchip]->max_ecmp_group_num+1));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_ECMP_MEMBER_NUM, g_gg_nh_master[lchip]->max_ecmp));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_EXTERNAL_NEXTHOP_NUM, g_gg_nh_master[lchip]->max_external_nhid));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_GLOBAL_DSNH_NUM, g_gg_nh_master[lchip]->max_glb_nh_offset));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MPLS_TUNNEL_NUM, g_gg_nh_master[lchip]->max_tunnel_id));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_ARP_ID_NUM, SYS_NH_ARP_ID_DEFAUL_NUM));

    sal_time(&tv);
    sal_srand(tv);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_free_node_data(void* node_data, void* user_data)
{
    uint32 free_nh_id_vector = 0;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_mcast_t* p_mcast = NULL;
    sys_nh_info_ipuc_t* p_ipuc = NULL;
    sys_nh_info_ecmp_t* p_ecmp = NULL;
    sys_nh_info_mpls_t* p_mpls = NULL;
    ctc_list_pointer_node_t* p_pos, * p_pos_next;
    sys_nh_mcast_meminfo_t* p_member;

    if (user_data)
    {
        free_nh_id_vector = *(uint32*)user_data;
        if (1 == free_nh_id_vector)
        {
            p_nhinfo=(sys_nh_info_com_t*)node_data;
            switch(p_nhinfo->hdr.nh_entry_type)
            {
                case SYS_NH_TYPE_MCAST:
                    p_mcast = (sys_nh_info_mcast_t*)(p_nhinfo);
                    CTC_LIST_POINTER_LOOP_DEL(p_pos, p_pos_next, &p_mcast->p_mem_list)
                    {
                        p_member = _ctc_container_of(p_pos, sys_nh_mcast_meminfo_t, list_head);
                        if (p_member->dsmet.vid_list)
                        {
                            mem_free(p_member->dsmet.vid_list);
                        }
                        if (p_member)
                        {
                            mem_free(p_member);
                        }
                    }
                    break;
                case SYS_NH_TYPE_IPUC:
                    p_ipuc = (sys_nh_info_ipuc_t*)(p_nhinfo);
                    if (p_ipuc->protection_path)
                    {
                        mem_free(p_ipuc->protection_path);
                    }
                    break;
                case SYS_NH_TYPE_ECMP:
                    if (p_nhinfo->hdr.p_ref_nh_list)
                    {
                        mem_free(p_nhinfo->hdr.p_ref_nh_list);
                    }
                    p_ecmp = (sys_nh_info_ecmp_t*)(p_nhinfo);
                    mem_free(p_ecmp->nh_array);
                    break;
                case SYS_NH_TYPE_MPLS:
                    p_mpls = (sys_nh_info_mpls_t*)(p_nhinfo);
                    if (p_mpls->protection_path)
                    {
                        mem_free(p_mpls->protection_path);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_deinit(uint8 lchip)
{
    uint32 free_nh_id_vector = 1;
    ctc_slistnode_t* node = NULL, * next_node = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == g_gg_nh_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free dsl2edit4w tree*/
    ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit4w_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->dsl2edit4w_tree), NULL);

    /*free dsl2edit8w tree*/
    ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit8w_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->dsl2edit8w_tree), NULL);

    /*free dsl2edit4w swap tree*/
    ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit4w_swap_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->dsl2edit4w_swap_tree), NULL);

    /*free dsl2edit6w tree*/
    ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit6w_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->dsl2edit6w_tree), NULL);

    /*free dsl2edit3w tree*/
    ctc_avl_traverse(g_gg_nh_master[lchip]->dsl2edit3w_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->dsl2edit3w_tree), NULL);

    /*free mpls tunnel tree*/
    /*ctc_avl_traverse(g_gg_nh_master[lchip]->mpls_tunnel_tree, (avl_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_avl_tree_free(&(g_gg_nh_master[lchip]->mpls_tunnel_tree), NULL);*/

    /*free external nhid vector*/
    ctc_vector_traverse(g_gg_nh_master[lchip]->external_nhid_vec, (vector_traversal_fn)_sys_goldengate_nh_free_node_data, &free_nh_id_vector);
    ctc_vector_release(g_gg_nh_master[lchip]->external_nhid_vec);

    /*free internal nhid vector*/
    ctc_vector_traverse(g_gg_nh_master[lchip]->internal_nhid_vec, (vector_traversal_fn)_sys_goldengate_nh_free_node_data, &free_nh_id_vector);
    ctc_vector_release(g_gg_nh_master[lchip]->internal_nhid_vec);

    /*free tunnel id vector*/
    ctc_vector_traverse(g_gg_nh_master[lchip]->tunnel_id_vec, (vector_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_vector_release(g_gg_nh_master[lchip]->tunnel_id_vec);

    /*free arp id vector*/
    ctc_vector_traverse(g_gg_nh_master[lchip]->arp_id_vec, (vector_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_vector_release(g_gg_nh_master[lchip]->arp_id_vec);

    /*free arp id hash*/
    ctc_hash_traverse(g_gg_nh_master[lchip]->vxlan_vni_hash, (hash_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);
    ctc_hash_free(g_gg_nh_master[lchip]->vxlan_vni_hash);

    /*free eloop list*/
    CTC_SLIST_LOOP_DEL(g_gg_nh_master[lchip]->eloop_list, node, next_node)
    {
        ctc_slist_delete_node(g_gg_nh_master[lchip]->eloop_list, node);
        mem_free(node);
    }
    ctc_slist_delete(g_gg_nh_master[lchip]->eloop_list);

    mem_free(g_gg_nh_master[lchip]->ipsa_bmp[CTC_IP_VER_4]);
    mem_free(g_gg_nh_master[lchip]->ipsa_bmp[CTC_IP_VER_6]);

    /*free l2edit data*/
    ctc_spool_free(g_gg_nh_master[lchip]->p_l2edit_vprof);

    /*free mcast group nhid vector*/
    /*ctc_vector_traverse(g_gg_nh_master[lchip]->mcast_group_vec, (vector_traversal_fn)_sys_goldengate_nh_free_node_data, NULL);*/
    ctc_vector_release(g_gg_nh_master[lchip]->mcast_group_vec);

    mem_free(g_gg_nh_master[lchip]->ecmp_if_resolved_l2edit);

    sys_goldengate_opf_deinit(lchip, OPF_ECMP_GROUP_ID);

    sys_goldengate_opf_deinit(lchip, OPF_ECMP_MEMBER_ID);

    sys_goldengate_opf_deinit(lchip, OPF_NH_DYN_SRAM);

    if (g_gg_nh_master[lchip]->max_glb_met_offset)
    {
        mem_free(g_gg_nh_master[lchip]->p_occupid_met_offset_bmp);
    }

    mem_free(g_gg_nh_master[lchip]->p_occupid_nh_offset_bmp);

    sal_mutex_destroy(g_gg_nh_master[lchip]->p_mutex);
    mem_free(g_gg_nh_master[lchip]);

    return CTC_E_NONE;
}


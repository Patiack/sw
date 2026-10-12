/**
 @file sys_goldengate_nexthop_misc.c

 @date 2009-11-25

 @version v2.0

 The file contains all non-layer2 and non-layer3 nexthop related callback function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_spool.h"
#include "ctc_linklist.h"
#include "ctc_packet.h"



#include "sys_goldengate_chip.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_nexthop_hw.h"
#include "sys_goldengate_l3if.h"
#include  "sys_goldengate_cpu_reason.h"
#include  "sys_goldengate_stats.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_NH_ILOOP_MAX_REMOVE_WORDS    3
#define SYS_NH_L2EDIT_VLAN_PROFILE_NUM    64

#define SYS_NH_MISC_FLAG_CONFLICT_CHECK(flag) \
    { \
        if (flag & CTC_MISC_NH_L2_EDIT_SWAP_MAC) \
        { \
            if ((flag & ~CTC_MISC_NH_L2_EDIT_SWAP_MAC) > 0) \
            { \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
    }

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
int32
_sys_goldengate_nh_free_misc_nh_resource(uint8 lchip, sys_nh_info_misc_t* p_nhinfo);
/****************************************************************************
 *
* Function
*
*****************************************************************************/
/**
 @brief Callback function of create unicast bridge nexthop

 @param[in,out] p_com_nh_para, parameters used to create bridge nexthop,
                writeback dsfwd offset array to this param

 @param[out] p_com_db, pointer used to store nexthop data

 @return CTC_E_XXX
 */
int32
sys_goldengate_nh_create_special_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_special_t* p_nh_para;
    sys_nh_info_special_t* p_nhdb;


    sys_nh_param_dsfwd_t dsfwd_param;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_special_t*)p_com_nh_para;
    p_nhdb = (sys_nh_info_special_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    switch (p_nh_para->hdr.nh_param_type)
    {
    case SYS_NH_TYPE_UNROV:
    case SYS_NH_TYPE_DROP:
        dsfwd_param.drop_pkt = TRUE;
        break;

    case SYS_NH_TYPE_TOCPU:
        dsfwd_param.is_lcpu = TRUE;
        break;

    default:
        CTC_ERROR_RETURN(CTC_E_INVALID_PARAM);
    }

    p_nhdb->hdr.nh_entry_type =  \
        (p_nhdb->hdr.nh_entry_type == SYS_NH_TYPE_NULL) ? (p_nh_para->hdr.nh_param_type) : p_nhdb->hdr.nh_entry_type;



   /*Build DsFwd Table*/
    if(!CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                                       &p_nhdb->hdr.dsfwd_info.dsfwd_offset));

    }
    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);

    sys_goldengate_get_gchip_id(lchip, &dsfwd_param.dest_chipid);
    /*Writeback dsfwd offset*/
    dsfwd_param.dsfwd_offset =  p_nhdb->hdr.dsfwd_info.dsfwd_offset;

    dsfwd_param.is_mcast = FALSE;


    if(SYS_NH_TYPE_TOCPU == p_nh_para->hdr.nh_param_type)
    {
        dsfwd_param.dsnh_offset = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_FWD_CPU, 0);
        p_nhdb->hdr.dsfwd_info.dsnh_offset =  dsfwd_param.dsnh_offset;
    }

    /*Write table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_delete_special_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_special_t* nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    nh_info = (sys_nh_info_special_t*)(p_data);


    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    /*Free dsfwd offset*/

    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, \
    SYS_NH_ENTRY_TYPE_FWD, 1, nh_info->hdr.dsfwd_info.dsfwd_offset));

    dsfwd_param.dsfwd_offset = nh_info->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.drop_pkt = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_create_iloop_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_iloop_t* p_nh_para;
    sys_nh_info_special_t* p_nhdb;
    uint32 dsfwd_offset = 0;
    uint8  gchip = 0;
    sys_nh_param_dsfwd_t dsfwd_param;
    uint16 lport = 0;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_iloop_t*)p_com_nh_para;
    CTC_PTR_VALID_CHECK(p_nh_para->p_iloop_param);
    if (p_nh_para->p_iloop_param->words_removed_from_hdr > SYS_NH_ILOOP_MAX_REMOVE_WORDS)
    {
        return CTC_E_INVALID_PARAM;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(p_nh_para->p_iloop_param->lpbk_lport);

    p_nhdb = (sys_nh_info_special_t*)p_com_db;
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    p_nhdb->hdr.nh_entry_type = (p_nh_para->hdr.nh_param_type);
    dsfwd_param.dest_id = ((lport&0x100)|SYS_RSV_PORT_ILOOP_ID);
    dsfwd_param.dsnh_offset = SYS_NH_ENCODE_ILOOP_DSNH(
            (lport&0xFF),
            (p_nh_para->p_iloop_param->logic_port ? \
             1 : 0), p_nh_para->p_iloop_param->customerid_valid,
            p_nh_para->p_iloop_param->words_removed_from_hdr,
            1);

    sys_goldengate_get_gchip_id(lchip, &gchip);
    p_nhdb->dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, dsfwd_param.dest_id);

    if (p_nh_para->p_iloop_param->sequence_en)
    {
        dsfwd_param.sequence_chk_en = TRUE;
        dsfwd_param.stats_ptr = p_nh_para->p_iloop_param->sequence_counter_index;
    }

    if (p_nh_para->p_iloop_param->customerid_valid)
    {
        dsfwd_param.nexthop_ext = 1;
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W);
    }

    if(p_nh_para->p_iloop_param->inner_packet_type_valid)
    {
        CTC_BIT_SET(dsfwd_param.dsnh_offset, 11);
        dsfwd_param.dsnh_offset &= ~(0x7000);
        dsfwd_param.dsnh_offset |= ((p_nh_para->p_iloop_param->inner_packet_type& 0x7) << 12);
    }

    /*Build DsFwd Table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                                    &dsfwd_offset));
    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    p_nhdb->hdr.dsfwd_info.dsfwd_offset = dsfwd_offset;
    p_nh_para->hdr.dsfwd_offset = dsfwd_offset;
    p_nhdb->hdr.dsfwd_info.dsnh_offset = dsfwd_param.dsnh_offset;


    dsfwd_param.dest_chipid = gchip;
    /*Writeback dsfwd offset*/
    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.service_queue_en = p_nh_para->p_iloop_param->service_queue_en;
    dsfwd_param.is_egress_edit = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_delete_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid)
{
    sys_nh_info_special_t* nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_data);
    nh_info = (sys_nh_info_special_t*)(p_data);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_ILOOP, p_data->hdr.nh_entry_type);


    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));

    /*Free dsfwd offset*/

    CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, \
    SYS_NH_ENTRY_TYPE_FWD, 1, nh_info->hdr.dsfwd_info.dsfwd_offset));

    dsfwd_param.dsfwd_offset = nh_info->hdr.dsfwd_info.dsfwd_offset;

    dsfwd_param.drop_pkt = 1;
    /*Write table*/
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));


    return CTC_E_NONE;
}

int32
sys_goldengate_nh_update_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_iloop_info, sys_nh_param_com_t* p_iloop_param)
{

    return CTC_E_NONE;
}

#if 0
/**
 @brief This function is used to config  crosschip rspan dsnexthop
 */
STATIC int32
_sys_goldengate_nh_set_crosschip_rspan(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param)
{
    uint32 cmd;
    DsNextHop4W_m dsnh;
    DsNextHop8W_m dsnh8w;

    sal_memset(&dsnh, 0, sizeof(DsNextHop4W_m));
    sal_memset(&dsnh8w, 0, sizeof(DsNextHop8W_m));

    SetDsNextHop4W(V, vlanXlateMode_f, &dsnh, 1);/*goldengate mode*/
    SetDsNextHop4W(V, u1_g2_taggedMode_f, &dsnh, 1);
    SetDsNextHop4W(V, u1_g2_deriveStagCos_f, &dsnh, 1);/*restore vlantag for mirror*/
    SetDsNextHop4W(V, u1_g2_replaceCtagCos_f, &dsnh, 1);/*add rspan vlan id*/
    SetDsNextHop4W(V, u1_g2_outputSvlanIdValid_f, &dsnh, 1);
    SetDsNextHop4W(V, u1_g2_outputSvlanId_f, &dsnh, p_dsnh_param->p_vlan_info->output_svid & 0xFFF);
    SetDsNextHop4W(V, u1_g2_mirrorTagAdd_f, &dsnh, 1);
    SetDsNextHop4W(V, destVlanPtr_f, &dsnh, p_dsnh_param->dest_vlan_ptr);
    SetDsNextHop4W(V, payloadOperation_f, &dsnh, SYS_NH_OP_MIRROR);
    SetDsNextHop4W(V, isNexthop8W_f, &dsnh, 0);
    SetDsNextHop4W(V, shareType_f, &dsnh, SYS_NH_SHARE_TYPE_VLANTAG);
    SetDsNextHop4W(V, isMet_f, &dsnh, 0);

    cmd = DRV_IOR(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 7, cmd, &dsnh8w));
    sal_memcpy(((uint8*)&dsnh8w + sizeof(DsNextHop4W_m)), &dsnh, sizeof(DsNextHop4W_m));

    cmd = DRV_IOW(EpeNextHopInternal_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 7, cmd, &dsnh8w));

    return CTC_E_NONE;
}
#endif

int32
sys_goldengate_nh_create_rspan_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db)
{

    sys_nh_param_rspan_t* p_nh_para;
    sys_nh_param_dsnh_t dsnh_param;
    ctc_vlan_egress_edit_info_t  vlan_egress_edit_info;
    sys_nh_info_rspan_t* p_nhdb = NULL;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master))

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);
    p_nh_para = (sys_nh_param_rspan_t*)p_com_nh_para;
    p_nhdb = (sys_nh_info_rspan_t*)p_com_db;

    CTC_PTR_VALID_CHECK(p_nh_para->p_rspan_param);
    CTC_VLAN_RANGE_CHECK(p_nh_para->p_rspan_param->rspan_vid);

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    sal_memset(&vlan_egress_edit_info, 0, sizeof(ctc_vlan_egress_edit_info_t));
    vlan_egress_edit_info.output_svid = p_nh_para->p_rspan_param->rspan_vid;

    dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_RSPAN;
    dsnh_param.p_vlan_info = &vlan_egress_edit_info;
    dsnh_param.dsnh_offset = p_nh_para->dsnh_offset;
    dsnh_param.dest_vlan_ptr = p_nh_para->p_rspan_param->rspan_vid;

    p_nhdb->hdr.nh_entry_type = SYS_NH_TYPE_RSPAN;
    dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh4w(lchip, &dsnh_param));

    return CTC_E_NONE;
}

int32
sys_goldengate_nh_delete_rspan_cb(uint8 lchip, sys_nh_info_com_t* p_com_db, uint32* p_nhid)
{
    return CTC_E_NONE;
}


 /* if vlan edit in bucket equals */
STATIC bool
_sys_greatbelt_nh_misc_l2edit_compare_vlan_edit(sys_dsl2edit_vlan_edit_t* pv0,
                                          sys_dsl2edit_vlan_edit_t* pv1)
{
    if (!pv0 || !pv1)
    {
        return FALSE;
    }

    if (!sal_memcmp(pv0, pv1, 4))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC uint32
_sys_greatbelt_nh_misc_l2edit_make_vlan_edit(sys_dsl2edit_vlan_edit_t* pv)
{

    uint8  * k = NULL;
    k    = (uint8 *) pv;

    return ctc_hash_caculate(4, k);
}

/* pv_out can be NULL.*/
STATIC int32
_sys_goldengate_nh_misc_l2edit_add_vlan_edit_spool(uint8 lchip, sys_dsl2edit_vlan_edit_t* pv,
                                             sys_dsl2edit_vlan_edit_t** pv_out)
{
    sys_dsl2edit_vlan_edit_t* pv_new = NULL;
    sys_dsl2edit_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    int32                 ret      = 0;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if(!p_nh_master->p_l2edit_vprof)
    {
       return CTC_E_NO_MEMORY;
    }
    /* malloc sys action */
    MALLOC_ZERO(MEM_NEXTHOP_MODULE, pv_new, sizeof(sys_dsl2edit_vlan_edit_t));
    if (!pv_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pv_new, pv, sizeof(sys_dsl2edit_vlan_edit_t));


    /* -- set vlan edit ptr -- */
    ret = ctc_spool_add(p_nh_master->p_l2edit_vprof, pv_new, NULL, &pv_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto cleanup;
    }
    else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
      /*new profile*/
        uint32 profile_id = 0;

        ret = (ret < 0 )? ret : sys_goldengate_nh_offset_alloc(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_VLAN_PROFILE, 1, &profile_id);

        if (ret < 0)
        {
            ctc_spool_remove(p_nh_master->p_l2edit_vprof, pv_new, NULL);
            goto cleanup;
        }
		pv_get->profile_id = (uint8)profile_id;
    }
    else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    { /*exist old profile*/
        mem_free(pv_new);
    }

    if (pv_out)
    {
        *pv_out = pv_get;
    }

    return CTC_E_NONE;

 cleanup:
    mem_free(pv_new);
    return ret;
}

int32
_sys_goldengate_nh_misc_l2edit_remove_vlan_edit_spool(uint8 lchip,  sys_dsl2edit_vlan_edit_t* pv)

{

    int32                 ret = 0;
    sys_dsl2edit_vlan_edit_t* pv_lkup;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if(NULL == p_nh_master->p_l2edit_vprof)
    {
       return CTC_E_NO_MEMORY;
    }

    ret = ctc_spool_remove(p_nh_master->p_l2edit_vprof, pv, &pv_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    { /*last node*/
      ret = sys_goldengate_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_VLAN_PROFILE, 1, pv->profile_id);

        mem_free(pv_lkup);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_misc_build_l2_edit(uint8 lchip, ctc_misc_nh_param_t* p_edit_param,
                                    sys_nh_info_misc_t* p_nhdb)
{
    uint8 misc_type = 0;
    ctc_misc_nh_flex_edit_param_t  *p_flex_edit         = NULL;
    ctc_misc_nh_over_l2_edit_param_t  *p_over_l2edit    = NULL;

    uint8 entry_type = 0;
    sys_dsl2edit_of6w_t dsl2edit;
    sys_dsl2edit_t ds_eth_l2_edit;
    void *p_dsl2edit = NULL;

    uint32 entry_num = 0;
    uint32 l2_offset = 0;


    misc_type = p_edit_param->type;

    if((CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == misc_type) || (CTC_MISC_NH_TYPE_OVER_L2 == misc_type))
    {
        p_over_l2edit = &p_edit_param->misc_param.over_l2edit;
    }
    else
    {
        p_flex_edit = &p_edit_param->misc_param.flex_edit;
    }

    if(p_flex_edit)
    {
        if (CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_SWAP_MACDA))
        {
            if (CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_VLAN_TAG) && (0 == p_flex_edit->user_vlanptr))
            {
                return CTC_E_FEATURE_NOT_SUPPORT;
            }

            entry_type = SYS_NH_ENTRY_TYPE_L2EDIT_SWAP;
        }
        else
        {
            sys_dsl2edit_vlan_edit_t vlan_edit;
            sys_dsl2edit_vlan_edit_t *p_edit_out;

            dsl2edit.vlan_profile_id = 0;
            dsl2edit.p_flex_edit = p_flex_edit;
            if (CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_VLAN_TAG) && (0 == p_flex_edit->user_vlanptr))
            {
                sal_memset(&vlan_edit,0,sizeof(vlan_edit));
                vlan_edit.new_stpid_idx = p_flex_edit->new_stpid_idx;
                vlan_edit.new_stpid_en = p_flex_edit->new_stpid_en;
                vlan_edit.stag_op = p_flex_edit->stag_op;
                vlan_edit.svid_sl = p_flex_edit->svid_sl;
                vlan_edit.scos_sl = p_flex_edit->scos_sl;
                vlan_edit.ctag_op = p_flex_edit->ctag_op;
                vlan_edit.cvid_sl = p_flex_edit->cvid_sl;
                vlan_edit.ccos_sl = p_flex_edit->ccos_sl;

                CTC_ERROR_RETURN(_sys_goldengate_nh_misc_l2edit_add_vlan_edit_spool(lchip, &vlan_edit, &p_edit_out));
                CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,  SYS_NH_ENTRY_TYPE_L2EDIT_VLAN_PROFILE, p_edit_out->profile_id, &vlan_edit));
                dsl2edit.vlan_profile_id = p_edit_out->profile_id;
                p_nhdb->p_vlan_edit = p_edit_out;
            }
            entry_type = SYS_NH_ENTRY_TYPE_L2EDIT_OF_6W;
        }

        p_dsl2edit = &dsl2edit;
        entry_num = 1;

    }

    if(p_over_l2edit)
    {
        sal_memset(&ds_eth_l2_edit, 0, sizeof(sys_dsl2edit_t));
        entry_type = SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W;


        ds_eth_l2_edit.ds_type = SYS_NH_DS_TYPE_L2EDIT;
        ds_eth_l2_edit.l2_rewrite_type = SYS_NH_L2EDIT_TYPE_ETH_8W;

        ds_eth_l2_edit.is_vlan_valid = (p_over_l2edit->vlan_id != 0);
        ds_eth_l2_edit.output_vlan_id= p_over_l2edit->vlan_id;
        ds_eth_l2_edit.is_6w = 1;

        sal_memcpy(ds_eth_l2_edit.mac_da, p_over_l2edit->mac_da, sizeof(mac_addr_t));
        sal_memcpy(ds_eth_l2_edit.mac_sa, p_over_l2edit->mac_sa, sizeof(mac_addr_t));
        ds_eth_l2_edit.update_mac_sa = 1;
        ds_eth_l2_edit.ether_type    = p_over_l2edit->ether_type;

        if (CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == misc_type)
        {
            ds_eth_l2_edit.is_span_ext_hdr = 1;
        }

        entry_num = 1;
        p_dsl2edit = &ds_eth_l2_edit;

    }

    CTC_ERROR_RETURN(sys_goldengate_nh_offset_alloc(lchip,  entry_type, entry_num,  &p_nhdb->dsl2edit_offset));
    if(p_over_l2edit)
    {
        l2_offset = (p_nhdb->dsl2edit_offset/2);
    }
    else
    {
        l2_offset = (p_nhdb->dsl2edit_offset);
    }
    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
    p_nhdb->l2edit_entry_type = entry_type;

    CTC_ERROR_RETURN(sys_goldengate_nh_write_asic_table(lchip,  entry_type, l2_offset, p_dsl2edit));
    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsL2Edit, offset = %d\n",  l2_offset);

    return CTC_E_NONE;
}
 /* if vlan edit in bucket equals */
static bool
_sys_goldengate_nh_misc_l3edit_compare_edit_of(sys_dsl3edit_of_t* pv0,
                                          sys_dsl3edit_of_t* pv1)
{
    if (!pv0 || !pv1)
    {
        return FALSE;
    }

    if (!sal_memcmp(pv0, pv1, sizeof(sys_dsl3edit_of_t)-4))
    {
        return TRUE;
    }

    return FALSE;
}

static uint32
_sys_goldengate_nh_misc_l3edit_make_edit_of(sys_dsl3edit_of_t* pv)
{

    uint8  * k = NULL;
    k    = (uint8 *) pv;

    return ctc_hash_caculate(sizeof(sys_dsl3edit_of_t)-4, k);
}

static int32
_sys_goldengate_nh_misc_l3edit_add_edit_of_spool(uint8 lchip, void* pv, uint8 entry_type,
                                             sys_nh_info_misc_t* p_nhdb)
{
    sys_dsl3edit_of_t* pv_new = NULL;
    sys_dsl3edit_of_t* pv_get = NULL; /* get from share pool*/
    int32                 ret      = 0;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if(!p_nh_master->p_l3edit_of)
    {
       return CTC_E_NO_MEMORY;
    }
    /* malloc sys action */
    MALLOC_ZERO(MEM_NEXTHOP_MODULE, pv_new, sizeof(sys_dsl3edit_of_t));
    if (!pv_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pv_new, pv, sizeof(sys_dsl3edit_of_t));


    /* -- add l3editof to spool -- */
    ret = ctc_spool_add(p_nh_master->p_l3edit_of, pv_new, NULL, &pv_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto cleanup;
    }
    else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
        /*new offset*/
        ret = (ret < 0 )? ret : sys_goldengate_nh_offset_alloc(lchip,  entry_type, 1,  &pv_get->edit_offset);
        if (ret < 0)
        {
            ctc_spool_remove(p_nh_master->p_l3edit_of, pv_new, NULL);
            goto cleanup;
        }
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "New alloc dsl3editof entry_type %u, offset = %d\n",  entry_type, pv_get->edit_offset);
        ret = sys_goldengate_nh_write_asic_table(lchip,  entry_type, pv_get->edit_offset, pv_new);
        if (ret)
        {
            ctc_spool_remove(p_nh_master->p_l3edit_of, pv_new, NULL);
            sys_goldengate_nh_offset_free(lchip,  entry_type, 1,  pv_get->edit_offset);
            goto cleanup;
        }
    }
    else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    { /*exist old profile*/
        mem_free(pv_new);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Exist dsl3editof entry_type %u, offset = %d\n",  entry_type, pv_get->edit_offset);
    }

    p_nhdb->dsl3edit_offset = pv_get->edit_offset;
    p_nhdb->p_l3edit = pv_get;

    return CTC_E_NONE;

 cleanup:
    mem_free(pv_new);
    return ret;
}

static int32
_sys_goldengate_nh_misc_l3edit_remove_edit_of_spool(uint8 lchip, sys_nh_info_misc_t* p_nhinfo)

{
    int32                 ret = 0;
    sys_dsl3edit_of_t* pv_lkup;
    sys_goldengate_nh_master_t* p_nh_master = NULL;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));
    if(NULL == p_nh_master->p_l3edit_of)
    {
       return CTC_E_NO_MEMORY;
    }

    if (!p_nhinfo->p_l3edit)
    {
        return CTC_E_NONE;
    }

    ret = ctc_spool_remove(p_nh_master->p_l3edit_of, p_nhinfo->p_l3edit, &pv_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    { /*last node*/
        ret = sys_goldengate_nh_offset_free(lchip,  p_nhinfo->l3edit_entry_type, 1, p_nhinfo->dsl3edit_offset);

        mem_free(pv_lkup);
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Free dsl3editof entry_type %u, offset = %d\n", p_nhinfo->l3edit_entry_type, p_nhinfo->dsl3edit_offset);
    }

    return CTC_E_NONE;
}

static INLINE int32
_sys_goldengate_nh_misc_build_l3_edit_of(uint8 lchip, ctc_misc_nh_flex_edit_param_t  *p_flex_edit,
                                    sys_nh_info_misc_t* p_nhdb)
{
    sys_dsl3edit_of_t  dsl3editof;
    sys_dsl3edit_of_t*  p_dsl3editof;
    int32 ret = 0;
    uint32 mask = 0;

    sal_memset(&dsl3editof, 0, sizeof(sys_dsl3edit_of_t));
    dsl3editof.l3_rewrite_type = (SYS_NH_ENTRY_TYPE_L3EDIT_OF_12W == p_nhdb->l3edit_entry_type)? SYS_NH_L3EDIT_TYPE_OF16W:SYS_NH_L3EDIT_TYPE_OF8W;
    dsl3editof.ds_type = SYS_NH_DS_TYPE_L3EDIT;
    dsl3editof.packet_type = p_flex_edit->packet_type;
    sal_memcpy(&dsl3editof.ip_sa, &p_flex_edit->ip_sa, sizeof(ipv6_addr_t));
    sal_memcpy(&dsl3editof.ip_da, &p_flex_edit->ip_da, sizeof(ipv6_addr_t));
    dsl3editof.protocol = p_flex_edit->protocol;
    dsl3editof.ttl = p_flex_edit->ttl;
    dsl3editof.ecn = p_flex_edit->ecn;
    dsl3editof.dscp_select = p_flex_edit->dscp_select;
    dsl3editof.dscp_or_tos = p_flex_edit->dscp_or_tos;
    dsl3editof.icmp_type = p_flex_edit->icmp_type;
    dsl3editof.icmp_code = p_flex_edit->icmp_code;
    dsl3editof.gre_key = p_flex_edit->gre_key;
    dsl3editof.l4_src_port = p_flex_edit->l4_src_port;
    dsl3editof.l4_dst_port = p_flex_edit->l4_dst_port;
    dsl3editof.arp_ht = p_flex_edit->arp_ht;
    dsl3editof.arp_pt = p_flex_edit->arp_pt;
    dsl3editof.arp_halen = p_flex_edit->arp_halen;
    dsl3editof.arp_palen = p_flex_edit->arp_palen;
    dsl3editof.arp_op = p_flex_edit->arp_op;
    dsl3editof.ecn_select = p_flex_edit->ecn_select;
    dsl3editof.flow_label = p_flex_edit->flow_label;

    sal_memcpy(&dsl3editof.arp_sha, &p_flex_edit->arp_sha, sizeof(mac_addr_t));
    sal_memcpy(&dsl3editof.arp_tha, &p_flex_edit->arp_tha, sizeof(mac_addr_t));
    dsl3editof.arp_spa = p_flex_edit->arp_spa;
    dsl3editof.arp_tpa = p_flex_edit->arp_tpa;
    mask = CTC_MISC_NH_FLEX_EDIT_REPLACE_IPSA | CTC_MISC_NH_FLEX_EDIT_REPLACE_IPDA | CTC_MISC_NH_FLEX_EDIT_SWAP_IP
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_TTL | CTC_MISC_NH_FLEX_EDIT_DECREASE_TTL | CTC_MISC_NH_FLEX_EDIT_REPLACE_ECN
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_PROTOCOL | CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_CODE | CTC_MISC_NH_FLEX_EDIT_REPLACE_ICMP_TYPE
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_GRE_KEY | CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_DST_PORT | CTC_MISC_NH_FLEX_EDIT_REPLACE_L4_SRC_PORT
        | CTC_MISC_NH_FLEX_EDIT_SWAP_L4_PORT | CTC_MISC_NH_FLEX_EDIT_REPLACE_TCP_PORT | CTC_MISC_NH_FLEX_EDIT_REPLACE_UDP_PORT
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_FLOW_LABEL | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HDR | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HT
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_PT | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HLEN | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_PLEN
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_OP | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_SHA | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_SPA
        | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_THA | CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_TPA;

    dsl3editof.flag = (p_flex_edit->flag & mask);

    p_dsl3editof = &dsl3editof;
    ret = _sys_goldengate_nh_misc_l3edit_add_edit_of_spool(lchip, (void*)p_dsl3editof, p_nhdb->l3edit_entry_type, p_nhdb);
    if (ret)
    {
        return ret;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_nh_misc_build_l3_edit(uint8 lchip, ctc_misc_nh_param_t* p_edit_param,
                                    sys_nh_info_misc_t* p_nhdb)
{
    ctc_misc_nh_flex_edit_param_t  *p_flex_edit = &p_edit_param->misc_param.flex_edit;
    uint8 entry_type = SYS_NH_ENTRY_TYPE_L3EDIT_OF_6W;
    if (!CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_IPV4)
        || (p_flex_edit->packet_type == CTC_MISC_NH_PACKET_TYPE_ARP && CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HDR)))
    {
        entry_type = SYS_NH_ENTRY_TYPE_L3EDIT_OF_12W;
    }

    CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
	p_nhdb->l3edit_entry_type = entry_type;
    CTC_ERROR_RETURN(_sys_goldengate_nh_misc_build_l3_edit_of(lchip, p_flex_edit, p_nhdb));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_misc_map_dsnh_vlan_info(uint8 lchip, ctc_misc_nh_flex_edit_param_t* p_flex_edit,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info)
{

    CTC_PTR_VALID_CHECK(p_flex_edit);
    CTC_PTR_VALID_CHECK(p_vlan_info);

    /* 1. svlan info map */
    switch (p_flex_edit->stag_op)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_VALID:
        p_vlan_info->svlan_edit_type = CTC_VLAN_EGRESS_EDIT_NONE;
        break;

    case CTC_VLAN_TAG_OP_REP_OR_ADD:
        p_vlan_info->svlan_edit_type = CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN;
        break;

    case CTC_VLAN_TAG_OP_ADD:
        p_vlan_info->svlan_edit_type = CTC_VLAN_EGRESS_EDIT_INSERT_VLAN;
        break;

    case CTC_VLAN_TAG_OP_DEL:
        p_vlan_info->svlan_edit_type = CTC_VLAN_EGRESS_EDIT_STRIP_VLAN;
        break;

    default: /* CTC_VLAN_TAG_OP_REP */
        return CTC_E_INVALID_PARAM;
    }

    if (p_flex_edit->svid_sl == CTC_VLAN_TAG_SL_NEW)
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID);
        p_vlan_info->output_svid = p_flex_edit->new_svid;
    }
    else if ((p_flex_edit->svid_sl == CTC_VLAN_TAG_SL_ALTERNATIVE) && (p_flex_edit->cvid_sl == CTC_VLAN_TAG_SL_ALTERNATIVE))
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_VLAN_SWAP_EN);

        if ((p_flex_edit->scos_sl == CTC_VLAN_TAG_SL_ALTERNATIVE) && (p_flex_edit->ccos_sl == CTC_VLAN_TAG_SL_ALTERNATIVE))
        {
            CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_COS_SWAP_EN);
        }
    }
    else if (p_flex_edit->svid_sl != CTC_VLAN_TAG_SL_AS_PARSE)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_flex_edit->scos_sl == CTC_VLAN_TAG_SL_NEW)
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_REPLACE_SVLAN_COS);
        p_vlan_info->stag_cos = p_flex_edit->new_scos;
    }
    else if (p_flex_edit->scos_sl == CTC_VLAN_TAG_SL_DEFAULT)
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_MAP_SVLAN_COS);
    }
    else if ((p_flex_edit->scos_sl == CTC_VLAN_TAG_SL_ALTERNATIVE) && (!CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_COS_SWAP_EN)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if (p_flex_edit->new_stpid_en)
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUTPUT_SVLAN_TPID_VALID);
        p_vlan_info->svlan_tpid_index = p_flex_edit->new_stpid_idx;
    }


    /* 2. cvlan info map */
    switch (p_flex_edit->ctag_op)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_VALID:
        p_vlan_info->cvlan_edit_type = CTC_VLAN_EGRESS_EDIT_NONE;
        break;

    case CTC_VLAN_TAG_OP_REP_OR_ADD:
        p_vlan_info->cvlan_edit_type = CTC_VLAN_EGRESS_EDIT_REPLACE_VLAN;
        break;

    case CTC_VLAN_TAG_OP_ADD:
        p_vlan_info->cvlan_edit_type = CTC_VLAN_EGRESS_EDIT_INSERT_VLAN;
        break;

    case CTC_VLAN_TAG_OP_DEL:
        p_vlan_info->cvlan_edit_type = CTC_VLAN_EGRESS_EDIT_STRIP_VLAN;
        break;

    default: /* CTC_VLAN_TAG_OP_REP */
        return CTC_E_INVALID_PARAM;
    }

    if (p_flex_edit->cvid_sl == CTC_VLAN_TAG_SL_NEW)
    {
        CTC_SET_FLAG(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID);
        p_vlan_info->output_cvid = p_flex_edit->new_cvid;
    }
    else if ((p_flex_edit->cvid_sl != CTC_VLAN_TAG_SL_AS_PARSE) && (!CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_VLAN_SWAP_EN)))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((p_flex_edit->ccos_sl != CTC_VLAN_TAG_SL_AS_PARSE) && (!CTC_FLAG_ISSET(p_vlan_info->edit_flag, CTC_VLAN_EGRESS_EDIT_COS_SWAP_EN)))
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_misc_write_dsnh(uint8 lchip, sys_nh_param_misc_t* p_nh_misc ,
                                    sys_nh_info_misc_t* p_nhdb)
{
    uint8 gchip = 0;
    sys_nh_param_dsnh_t dsnh_param;
    uint8 misc_type = 0;
    uint16 dest_vlan_ptr = SYS_DSNH_DESTVLANPTR_SPECIAL;
    sys_l3if_prop_t l3if_prop;
    ctc_vlan_egress_edit_info_t vlan_info;
    ctc_misc_nh_flex_edit_param_t  *p_flex_edit = NULL;
    ctc_misc_nh_over_l2_edit_param_t  *p_over_l2_edit = NULL;
    ctc_nh_oif_info_t *p_oif = NULL;

    sal_memset(&l3if_prop, 0, sizeof(l3if_prop));
    sal_memset(&vlan_info, 0, sizeof(vlan_info));

    misc_type =  p_nh_misc->p_misc_param->type;
    if ((CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == misc_type) || (CTC_MISC_NH_TYPE_OVER_L2 == misc_type))
    {
        p_over_l2_edit = &p_nh_misc->p_misc_param->misc_param.over_l2edit;
    }
    else
    {
        p_flex_edit = &p_nh_misc->p_misc_param->misc_param.flex_edit;
    }

    sal_memset(&dsnh_param, 0, sizeof(sys_nh_param_dsnh_t));
    dsnh_param.stats_ptr = p_nh_misc->hdr.stats_ptr;

    if(p_flex_edit)
    {
        if (p_flex_edit->is_reflective)
        {
           p_nh_misc->p_misc_param->gport = CTC_MAP_LPORT_TO_GPORT(gchip, 0);
           p_nhdb->is_reflective = p_flex_edit->is_reflective;
           p_nh_misc->hdr.have_dsfwd = 1;
        }
        else
        {
            p_nhdb->is_reflective = 0;
        }

        dest_vlan_ptr = p_flex_edit->user_vlanptr ? p_flex_edit->user_vlanptr : dest_vlan_ptr;
    }

    p_oif = &(p_nh_misc->p_misc_param->oif);
    p_nhdb->gport = p_nh_misc->p_misc_param->is_oif ? p_oif->gport : p_nh_misc->p_misc_param->gport;
    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE) && (!p_flex_edit || !p_flex_edit->is_reflective))
    {
        gchip = CTC_MAP_GPORT_TO_GCHIP(p_nhdb->gport);

        if(SYS_GCHIP_IS_REMOTE(lchip, gchip))
        {
            return CTC_E_NONE;
        }
    }

    if (((dest_vlan_ptr>>10)&0x7) == 4)
    {
        p_nhdb->l3ifid = (dest_vlan_ptr&0x3ff);
    }

    if (p_nh_misc->p_misc_param->is_oif && (!(CTC_IS_CPU_PORT(p_nh_misc->p_misc_param->oif.gport) ||
                            p_nh_misc->p_misc_param->oif.is_l2_port || SYS_IS_DROP_PORT(p_nh_misc->p_misc_param->oif.gport))))
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(lchip, p_oif->gport, p_oif->vid, &l3if_prop));
        dest_vlan_ptr = l3if_prop.vlan_ptr;
        p_nhdb->l3ifid = l3if_prop.l3if_id;
    }


    if (p_flex_edit)
    {
        if(CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_L2_HDR))
        {
            CTC_ERROR_RETURN(_sys_goldengate_nh_misc_build_l2_edit(lchip, p_nh_misc->p_misc_param,p_nhdb));
            if (CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_OP_BRIDGE))
            {
                CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_MISC_FLEX_PLD_BRIDGE);
            }

            if (CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_VLAN_TAG) && (0 != p_flex_edit->user_vlanptr))
            {
                CTC_ERROR_RETURN(_sys_goldengate_nh_misc_map_dsnh_vlan_info(lchip, p_flex_edit, &vlan_info));
                dsnh_param.p_vlan_info = &vlan_info;
            }
        }

        if(((p_flex_edit->packet_type != CTC_MISC_NH_PACKET_TYPE_ARP)
            && CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_IP_HDR))
            || CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_REPLACE_ARP_HDR))
        {
          CTC_ERROR_RETURN(_sys_goldengate_nh_misc_build_l3_edit(lchip, p_nh_misc->p_misc_param,p_nhdb));
        }

        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_OF;
        dsnh_param.dest_vlan_ptr = dest_vlan_ptr;
        dsnh_param.l2edit_ptr = p_nhdb->dsl2edit_offset;
        dsnh_param.l3edit_ptr = p_nhdb->dsl3edit_offset;
        dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
        dsnh_param.mtu_no_chk = CTC_FLAG_ISSET(p_flex_edit->flag, CTC_MISC_NH_FLEX_EDIT_MTU_CHECK) ? 0 : 1;
        if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh8w(lchip, &dsnh_param));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh4w(lchip, &dsnh_param));
        }
    }

    if (p_over_l2_edit)
    {
        CTC_ERROR_RETURN(_sys_goldengate_nh_misc_build_l2_edit(lchip, p_nh_misc->p_misc_param,p_nhdb));

        dsnh_param.dsnh_type = SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL;
        dsnh_param.dest_vlan_ptr = dest_vlan_ptr;
        dsnh_param.l2edit_ptr  = p_nhdb->dsl2edit_offset;
        dsnh_param.l3edit_ptr  = 0;
        dsnh_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
        dsnh_param.span_id     = p_over_l2_edit->flow_id;
        if (p_over_l2_edit->flag & CTC_MISC_NH_OVER_L2_EDIT_VLAN_EDIT)
        {
            dsnh_param.p_vlan_info = &p_over_l2_edit->vlan_info;
        }

        CTC_SET_FLAG(dsnh_param.flag, SYS_NH_PARAM_FLAG_ERSPAN_EN);
        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsnh8w(lchip, &dsnh_param));
    }


    return CTC_E_NONE;
}

int32
_sys_goldengate_nh_misc_add_dsfwd(uint8 lchip, sys_nh_info_com_t* p_com_db)
{
   sys_nh_param_dsfwd_t dsfwd_param;
   sys_nh_info_misc_t* p_nhdb = (sys_nh_info_misc_t*)(p_com_db);

   int ret = 0;

    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));
    dsfwd_param.dest_chipid = SYS_MAP_CTC_GPORT_TO_GCHIP(p_nhdb->gport);
    dsfwd_param.dest_id = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_nhdb->gport);
    dsfwd_param.is_cpu  = CTC_IS_CPU_PORT(p_nhdb->gport);
    dsfwd_param.is_mcast = FALSE;
    dsfwd_param.is_reflective = p_nhdb->is_reflective;


    if (CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W))
    {
        dsfwd_param.nexthop_ext = 1;
    }

    /*Build DsFwd Table*/
	if (!CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
       ret = sys_goldengate_nh_offset_alloc(lchip, SYS_NH_ENTRY_TYPE_FWD, 1,
                                        &p_nhdb->hdr.dsfwd_info.dsfwd_offset);
	}
    dsfwd_param.dsnh_offset = p_nhdb->hdr.dsfwd_info.dsnh_offset;
    dsfwd_param.dsfwd_offset = p_nhdb->hdr.dsfwd_info.dsfwd_offset;
    dsfwd_param.is_egress_edit =  CTC_FLAG_ISSET(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE);

    /*Write table*/
    ret = ret ? ret : sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param);

	if (ret != CTC_E_NONE)
    {
       sys_goldengate_nh_offset_free(lchip,  SYS_NH_ENTRY_TYPE_FWD, 1, p_nhdb->hdr.dsfwd_info.dsfwd_offset);
    }
    else
    {
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD);
    }
	return ret;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_nh_update_misc_fwd_attr(uint8 lchip, sys_nh_param_misc_t* p_nhpara,
                                            sys_nh_info_misc_t* p_nhinfo)
{
    sys_nh_param_misc_t* p_para = (sys_nh_param_misc_t*)p_nhpara;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_USE_DSNH8W)
       && p_para->p_misc_param
    && ((CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == p_para->p_misc_param->type) ||
         (CTC_MISC_NH_TYPE_OVER_L2 == p_para->p_misc_param->type) ||
        p_para->p_misc_param->misc_param.flex_edit.user_vlanptr)
    && (!CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV)))
    {
       SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [Nexthop] Should use DsNexthop8W table \n");
       return CTC_E_INVALID_CONFIG;
    }

     /*Build nhpara*/
    p_nhpara->hdr.have_dsfwd = CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                                || (NULL != p_nhinfo->hdr.p_ref_nh_list);

    _sys_goldengate_nh_free_misc_nh_resource(lchip, p_nhinfo);
    CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_IS_UNROV);
    CTC_ERROR_RETURN(sys_goldengate_nh_create_misc_cb(lchip, (
                                                              sys_nh_param_com_t*)p_nhpara, (sys_nh_info_com_t*)p_nhinfo));
   return CTC_E_NONE;


}

int32
_sys_goldengate_nh_free_misc_nh_resource(uint8 lchip, sys_nh_info_misc_t* p_nhinfo)
{
    uint32 entry_num = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_nhinfo);

    if (p_nhinfo->p_vlan_edit)
    {
        sys_dsl2edit_vlan_edit_t vlan_edit;
        sal_memcpy(&vlan_edit, p_nhinfo->p_vlan_edit, sizeof(vlan_edit));
        _sys_goldengate_nh_misc_l2edit_remove_vlan_edit_spool(lchip, &vlan_edit);
        p_nhinfo->p_vlan_edit = NULL;
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT))
    {
        entry_num = 1;

        sys_goldengate_nh_offset_free(lchip,  p_nhinfo->l2edit_entry_type, entry_num, p_nhinfo->dsl2edit_offset);
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L2EDIT);
        p_nhinfo->dsl2edit_offset = 0;
    }

    if (CTC_FLAG_ISSET(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT))
    {
        _sys_goldengate_nh_misc_l3edit_remove_edit_of_spool(lchip, p_nhinfo);
        p_nhinfo->p_l3edit = NULL;
        CTC_UNSET_FLAG(p_nhinfo->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_L3EDIT);
        p_nhinfo->dsl3edit_offset = 0;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_create_misc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db)
{
    sys_nh_param_misc_t* p_nh_misc;
    int32 ret = 0;

    sys_nh_info_misc_t* p_nhdb = NULL;
    ctc_misc_nh_param_t* p_tmp_misc_param = NULL;
    ctc_misc_nh_type_t misc_nh_type;
    uint32 dest_port = 0;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    /* 1. sanity check & init */
    CTC_PTR_VALID_CHECK(p_com_nh_para);
    CTC_PTR_VALID_CHECK(p_com_db);

    p_nh_misc = (sys_nh_param_misc_t*)p_com_nh_para;
    p_nhdb    = (sys_nh_info_misc_t*)p_com_db;
    p_tmp_misc_param = p_nh_misc->p_misc_param;
    misc_nh_type = p_tmp_misc_param->type;

    if(CTC_MISC_NH_TYPE_REPLACE_L2HDR == misc_nh_type
		|| CTC_MISC_NH_TYPE_REPLACE_L2_L3HDR == misc_nh_type)
    {
       return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if ((CTC_MISC_NH_TYPE_TO_CPU == misc_nh_type) && p_tmp_misc_param->stats_id)
    {
        SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Feature not supported \n");
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (p_tmp_misc_param->stats_id)
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr(lchip,
                                                           p_tmp_misc_param->stats_id,
                                                           CTC_STATS_STATSID_TYPE_NEXTHOP,
                                                           &p_nh_misc->hdr.stats_ptr));
    }

    if(CTC_MISC_NH_TYPE_FLEX_EDIT_HDR == misc_nh_type)
    {
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.ecn, 3);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.dscp_or_tos, 63);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.new_stpid_idx, 3);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.packet_type, CTC_MISC_NH_PACKET_TYPE_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.stag_op, CTC_VLAN_TAG_OP_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.svid_sl, CTC_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.scos_sl, CTC_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.ctag_op, CTC_VLAN_TAG_OP_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.cvid_sl, CTC_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.ccos_sl, CTC_VLAN_TAG_SL_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.flex_edit.user_vlanptr, 0x1fff);

        if (CTC_VLAN_TAG_SL_NEW == p_tmp_misc_param->misc_param.flex_edit.svid_sl)
        {
            CTC_VLAN_RANGE_CHECK(p_tmp_misc_param->misc_param.flex_edit.new_svid);
        }

        if (CTC_VLAN_TAG_SL_NEW == p_tmp_misc_param->misc_param.flex_edit.cvid_sl)
        {
            CTC_VLAN_RANGE_CHECK(p_tmp_misc_param->misc_param.flex_edit.new_cvid);
        }

        if (CTC_VLAN_TAG_SL_NEW == p_tmp_misc_param->misc_param.flex_edit.scos_sl)
        {
            CTC_COS_RANGE_CHECK(p_tmp_misc_param->misc_param.flex_edit.new_scos);
        }

        if (CTC_VLAN_TAG_SL_NEW == p_tmp_misc_param->misc_param.flex_edit.ccos_sl)
        {
            CTC_COS_RANGE_CHECK(p_tmp_misc_param->misc_param.flex_edit.new_ccos);
        }
    }
    else if((CTC_MISC_NH_TYPE_OVER_L2_WITH_TS == misc_nh_type) || (CTC_MISC_NH_TYPE_OVER_L2 == misc_nh_type))
    {
        if (p_tmp_misc_param->misc_param.over_l2edit.vlan_id)
        {
            CTC_VLAN_RANGE_CHECK(p_tmp_misc_param->misc_param.over_l2edit.vlan_id);
        }
        CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.over_l2edit.flow_id, 0x3FF);

        if (p_tmp_misc_param->misc_param.over_l2edit.flag & CTC_MISC_NH_OVER_L2_EDIT_VLAN_EDIT)
        {
            if (CTC_FLAG_ISSET(p_tmp_misc_param->misc_param.over_l2edit.vlan_info.edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_CVID_VALID))
            {
                CTC_VLAN_RANGE_CHECK(p_tmp_misc_param->misc_param.over_l2edit.vlan_info.output_cvid);
            }

            if (CTC_FLAG_ISSET(p_tmp_misc_param->misc_param.over_l2edit.vlan_info.edit_flag, CTC_VLAN_EGRESS_EDIT_OUPUT_SVID_VALID))
            {
                CTC_VLAN_RANGE_CHECK(p_tmp_misc_param->misc_param.over_l2edit.vlan_info.output_svid);
            }

            CTC_MAX_VALUE_CHECK(p_tmp_misc_param->misc_param.over_l2edit.vlan_info.user_vlanptr, 0x1fff);
        }
    }

    p_nhdb->hdr.nh_entry_type   = SYS_NH_TYPE_MISC;
    p_nhdb->misc_nh_type        = misc_nh_type;


    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "nhType = %d, miscNhType = %d, NHID = %d, isInternaNH = %d, \n\
        gport = %d, dsnh_offset = %d\n",
                   p_nh_misc->hdr.nh_param_type,
                   misc_nh_type,
                   p_nh_misc->hdr.nhid,
                   p_nh_misc->hdr.is_internal_nh,
                   p_nh_misc->p_misc_param->gport,
                   p_nh_misc->p_misc_param->dsnh_offset);


    if (CTC_MISC_NH_TYPE_TO_CPU == misc_nh_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_cpu_reason_get_dsfwd_offset(lchip, p_nh_misc->p_misc_param->misc_param.cpu_reason.cpu_reason_id, p_nh_misc->p_misc_param->misc_param.cpu_reason.lport_en,
                  &p_nhdb->hdr.dsfwd_info.dsfwd_offset,&p_nhdb->hdr.dsfwd_info.dsnh_offset, &dest_port));

        CTC_ERROR_RETURN(sys_goldengate_nh_update_dsfwd_lport_en(lchip, p_nhdb->hdr.dsfwd_info.dsfwd_offset, p_nh_misc->p_misc_param->misc_param.cpu_reason.lport_en));
        CTC_SET_FLAG(p_nhdb->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD);
        p_nhdb->gport = dest_port;
        p_nh_misc->hdr.have_dsfwd = 0;
    }
    else
    {
        /*write l2edit and nexthop*/
        CTC_ERROR_GOTO(_sys_goldengate_nh_misc_write_dsnh(lchip,  p_nh_misc, p_nhdb),ret, error_proc);

    }

	/*3.Create Dwfwd*/
    if(!p_nh_misc->hdr.have_dsfwd)
    {
      return CTC_E_NONE;
    }

	ret = _sys_goldengate_nh_misc_add_dsfwd(lchip, p_com_db);
error_proc:
	if(ret != CTC_E_NONE)
	{
       _sys_goldengate_nh_free_misc_nh_resource(lchip, p_nhdb);
	}
    return ret;

}

int32
sys_goldengate_nh_delete_misc_cb(uint8 lchip, sys_nh_info_com_t* p_flex_info, uint32* p_nhid)
{

    sys_nh_info_misc_t* p_nh_info;
    sys_nh_param_dsfwd_t dsfwd_param;
    sys_nh_ref_list_node_t* p_ref_node, * p_tmp_node;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_flex_info);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_flex_info->hdr.nh_entry_type);

    p_nh_info = (sys_nh_info_misc_t*)(p_flex_info);

    sys_goldengate_nh_update_ecmp_member(lchip, *p_nhid, SYS_NH_CHANGE_TYPE_NH_DELETE);

    /*1. Delete ecmp  list */
    p_ref_node = p_nh_info->hdr.p_ref_nh_list;

    while (p_ref_node)
    {
        /*Remove from db*/
        p_tmp_node = p_ref_node;
        p_ref_node = p_ref_node->p_next;
        mem_free(p_tmp_node);
    }

    p_nh_info->hdr.p_ref_nh_list = NULL;

    /*2. Free DsFwd/DsNexthop/DsL2Edit offset*/
    sal_memset(&dsfwd_param, 0, sizeof(sys_nh_param_dsfwd_t));


    if (CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_offset_free(lchip, \
                                                      SYS_NH_ENTRY_TYPE_FWD, 1, p_nh_info->hdr.dsfwd_info.dsfwd_offset));

        dsfwd_param.drop_pkt  = TRUE;
        dsfwd_param.dsfwd_offset = p_nh_info->hdr.dsfwd_info.dsfwd_offset;

        /*Write table*/
        CTC_ERROR_RETURN(sys_goldengate_nh_write_entry_dsfwd(lchip, &dsfwd_param));
    }

    if (NULL != p_nh_info->updateAd)
    {
         sys_nh_info_dsnh_t dsnh_info;
         sal_memset(&dsnh_info, 0, sizeof(dsnh_info));
         dsnh_info.drop_pkt =1;
         p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
    }

    _sys_goldengate_nh_free_misc_nh_resource(lchip, p_nh_info);
    return CTC_E_NONE;
}

int32
sys_goldengate_nh_update_misc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para)
{
    sys_nh_info_misc_t* p_nh_info;
    sys_nh_param_misc_t* p_nh_para;

    SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(p_para);
    CTC_PTR_VALID_CHECK(p_nh_db);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_para->hdr.nh_param_type);
    CTC_EQUAL_CHECK(SYS_NH_TYPE_MISC, p_nh_db->hdr.nh_entry_type);

    p_nh_info = (sys_nh_info_misc_t*)(p_nh_db);
    p_nh_para = (sys_nh_param_misc_t*)(p_para);

    switch (p_para->hdr.change_type)
    {
    case SYS_NH_CHANGE_TYPE_FWD_TO_UNROV:
        break;
    case SYS_NH_CHANGE_TYPE_UNROV_TO_FWD:
        break;
    case SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR:
        p_nh_para->hdr.have_dsfwd  = CTC_FLAG_ISSET(p_nh_info->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD)
                               || (NULL != p_nh_info->hdr.p_ref_nh_list);

        CTC_ERROR_RETURN(_sys_goldengate_nh_update_misc_fwd_attr(lchip, p_nh_para, p_nh_info));
        break;


    case SYS_NH_CHANGE_TYPE_ADD_DYNTBL:
        {

           if (CTC_FLAG_ISSET(p_nh_db->hdr.nh_entry_flags, SYS_NH_INFO_FLAG_HAVE_DSFWD))
            {
                return CTC_E_NONE;
            }
            _sys_goldengate_nh_misc_add_dsfwd(lchip, p_nh_db);
        }
    	break;
    default:
        return CTC_E_INVALID_PARAM;
    }


	if (p_nh_para->hdr.change_type != SYS_NH_CHANGE_TYPE_ADD_DYNTBL
	 	&&	NULL != p_nh_info->updateAd)
    {
        sys_nh_info_dsnh_t        dsnh_info;
        sal_memset(&dsnh_info, 0, sizeof(sys_nh_info_dsnh_t));
        sys_goldengate_nh_mapping_to_nhinfo(lchip, (sys_nh_info_com_t*)p_nh_info, &dsnh_info);
        p_nh_info->updateAd(lchip, p_nh_info->data, &dsnh_info);
    }

    if (p_nh_info->hdr.p_ref_nh_list)
    {
        int32 ret = 0;
        ret = (sys_goldengate_nh_update_ecmp_member(lchip, p_nh_para->hdr.nhid, p_nh_para->hdr.change_type));
        if (ret)
        {
            SYS_NH_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " [Nexthop] Nexthop update for ecmp fail!, ret:%d \n", ret);
        }
    }

  return CTC_E_NONE;
}



int32
sys_goldengate_nh_misc_init(uint8 lchip)
{
    sys_goldengate_nh_master_t* p_nh_master = NULL;
    ctc_spool_t spool;
    uint32 edit_of6w_size = 0;

    sal_memset(&spool, 0, sizeof(ctc_spool_t));

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nh_master(lchip, &p_nh_master));

    spool.lchip = lchip;
    spool.block_num = 1;
    spool.block_size = SYS_NH_L2EDIT_VLAN_PROFILE_NUM;
    spool.max_count = SYS_NH_L2EDIT_VLAN_PROFILE_NUM;
    spool.user_data_size = sizeof(sys_dsl2edit_vlan_edit_t);
    spool.spool_key = (hash_key_fn) _sys_greatbelt_nh_misc_l2edit_make_vlan_edit;
    spool.spool_cmp = (hash_cmp_fn) _sys_greatbelt_nh_misc_l2edit_compare_vlan_edit;
    p_nh_master->p_l2edit_vprof = ctc_spool_create(&spool);

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsL3EditOf6W_t, &edit_of6w_size));
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    spool.lchip = lchip;
    spool.block_num = (edit_of6w_size)/64;
    spool.block_size = 64;
    spool.max_count = edit_of6w_size;
    spool.user_data_size = sizeof(sys_dsl3edit_of_t);
    spool.spool_key = (hash_key_fn) _sys_goldengate_nh_misc_l3edit_make_edit_of;
    spool.spool_cmp = (hash_cmp_fn) _sys_goldengate_nh_misc_l3edit_compare_edit_of;
    p_nh_master->p_l3edit_of = ctc_spool_create(&spool);

  return CTC_E_NONE;
}



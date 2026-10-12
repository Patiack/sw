/**
 @file ctc_greatbelt_tp_y1731_db.c

 @date 2011-12-11

 @version v2.0

  This file contains oam sys layer function implementation
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_oam.h"
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_linklist.h"
#include "ctc_hash.h"
#include "ctc_parser.h"

#include "sys_greatbelt_common.h"
#include "sys_greatbelt_chip.h"

#include "sys_greatbelt_ftm.h"

#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_mpls.h"

#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_oam_tp_y1731_db.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

extern sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
*
* Function
*
**
***************************************************************************/

#define CHAN "Function Begin"



int32
_sys_greatbelt_tp_chan_update(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp_y1731, bool is_add)
{
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;

    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_mpls_pbt_bfd_oam_chan_t*  p_ds_mpls_oam_tcam_chan = NULL;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;

    ds_src_port_t ds_src_port;

    void* p_chan = NULL;

    uint32 index       = 0;
    uint8  offset      = 0;
    uint32 chan_cmd    = 0;
    uint32 mpls_cmd    = 0;

    uint8  lm_en       = 0;
    uint8  oam_check_type = SYS_OAM_TP_NONE_TYPE;
    uint8        master_chipid = 0;

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port_t));

    SYS_OAM_DBG_FUNC();

    p_sys_chan_tp = (sys_oam_chan_tp_t*)p_sys_lmep_tp_y1731->com.p_sys_chan;
    master_chipid    = p_sys_chan_tp->com.master_chipid;
    /*mep index*/
    if (is_add)
    {
        p_sys_chan_tp->mep_index = p_sys_lmep_tp_y1731->com.lmep_index;
        oam_check_type = SYS_OAM_TP_MEP_TYPE;
    }
    else
    {
        p_sys_chan_tp->mep_index = 0x1FFF;
        oam_check_type = SYS_OAM_TP_NONE_TYPE;
    }

    if ((p_sys_chan_tp->key.section_oam)
        && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
    && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id))))
    {
        return CTC_E_NONE;
    }


    /*lm enable*/
    if (CTC_FLAG_ISSET(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN) && is_add)
    {
        lm_en   = 1;
    }
    else
    {
        lm_en   = 0;
    }

    /*
    if (p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE !=  p_sys_chan_tp->lm_type))
    {
    */
    if (SYS_OAM_KEY_HASH == p_sys_chan_tp->key.com.key_alloc)
    {
        sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
        p_chan                  = &ds_mpls_oam_chan;
        p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)p_chan;
        chan_cmd = DRV_IOR(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
    }
    else
    {
        sal_memset(&lpm_tcam_ad, 0, sizeof(ds_lpm_tcam_ad_t));
        offset = sizeof(ds_lpm_tcam_ad_t) - sizeof(ds_mpls_pbt_bfd_oam_chan_t);
        p_chan                  = &lpm_tcam_ad;
        p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)((uint8*)p_chan + offset);
        chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
    }

    index = p_sys_chan_tp->com.chan_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

    p_ds_mpls_oam_tcam_chan->mep_index      = p_sys_chan_tp->mep_index;
    p_ds_mpls_oam_tcam_chan->mpls_lm_type   = 0;
    p_ds_mpls_oam_tcam_chan->oam_dest_chip_id = master_chipid;

    if (lm_en)
    {
        p_ds_mpls_oam_tcam_chan->lm_index_base  = p_sys_chan_tp->com.lm_index_base;
        p_ds_mpls_oam_tcam_chan->lm_cos         = p_sys_chan_tp->lm_cos;
        p_ds_mpls_oam_tcam_chan->lm_cos_type    = p_sys_chan_tp->lm_cos_type;
        p_ds_mpls_oam_tcam_chan->lm_type        = p_sys_chan_tp->lm_type;
    }
    else
    {
        p_ds_mpls_oam_tcam_chan->lm_type         = CTC_OAM_LM_TYPE_NONE;
    }

    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
    }
    else
    {
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));


    if (!p_sys_chan_tp->key.section_oam)
    {
        ds_mpls_t ds_mpls;
        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
        mpls_cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*index    = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

        ds_mpls.mep_index       = p_sys_chan_tp->mep_index;
        ds_mpls.oam_check_type  = oam_check_type;
        ds_mpls.mpls_lm_type    = 0;
        if (lm_en)
        {
            ds_mpls.lm_base         = p_sys_chan_tp->com.lm_index_base;
            ds_mpls.lm_cos          = p_sys_chan_tp->lm_cos;
            ds_mpls.lm_cos_type     = p_sys_chan_tp->lm_cos_type;
            ds_mpls.lm_type         = p_sys_chan_tp->lm_type;
        }
        else
        {
            ds_mpls.lm_type         = CTC_OAM_LM_TYPE_NONE;
        }

        mpls_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

        /*update nexthop to enbale egress lm to be add*/
        sys_greatbelt_mpls_nh_update_oam_en(lchip, p_sys_lmep_tp_y1731->nhid, p_sys_chan_tp->out_label, lm_en);

    }
    else
    { /*need update port or interface lm enable*/
        uint32 cmd      = 0;
        uint32 field    = 0;
        uint8  gchip    = 0;

        field = CTC_FLAG_ISSET(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);
        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
        {
            index = p_sys_chan_tp->key.gport_l3if_id;
            cmd = DRV_IOR(DsSrcInterface_t, DsSrcInterface_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
            cmd = DRV_IOR(DsDestInterface_t, DsDestInterface_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));

        }
        else
        {
            index = CTC_MAP_GPORT_TO_LPORT(p_sys_chan_tp->key.gport_l3if_id);
            gchip = CTC_MAP_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id);
            if (FALSE == sys_greatbelt_chip_is_local(lchip, gchip))
            {
                return CTC_E_NONE;
            }
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_chan_update_reomte(uint8 lchip, ctc_oam_lmep_t* p_lmep, sys_oam_chan_tp_t* p_sys_chan_tp, bool is_add)
{
    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_mpls_pbt_bfd_oam_chan_t*  p_ds_mpls_oam_tcam_chan = NULL;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;
    ds_mpls_t ds_mpls;

    void* p_chan = NULL;

    uint32 index       = 0;
    uint8  offset      = 0;
    uint32 chan_cmd    = 0;
    uint32 mpls_cmd    = 0;

    SYS_OAM_DBG_FUNC();

    /*mep index*/
    if (is_add)
    {
        p_sys_chan_tp->mep_index = p_lmep->lmep_index;
    }
    else
    {
        p_sys_chan_tp->mep_index = 0x1FFF;
    }

    if (p_sys_chan_tp->key.section_oam)
    {
        if (SYS_OAM_KEY_HASH == p_sys_chan_tp->key.com.key_alloc)
        {
            sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
            p_chan                  = &ds_mpls_oam_chan;
            p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)p_chan;
            chan_cmd = DRV_IOR(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
        }
        else
        {
            sal_memset(&lpm_tcam_ad, 0, sizeof(ds_lpm_tcam_ad_t));
            offset = sizeof(ds_lpm_tcam_ad_t) - sizeof(ds_mpls_pbt_bfd_oam_chan_t);
            p_chan                  = &lpm_tcam_ad;
            p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)((uint8*)p_chan + offset);
            chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }


        index = p_sys_chan_tp->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));
        p_ds_mpls_oam_tcam_chan->mep_index      = p_sys_chan_tp->mep_index;
        if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
        }
        else
        {
            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

    }
    else
    {
        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
        mpls_cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*index    = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

        ds_mpls.mep_index = p_sys_chan_tp->mep_index;

        mpls_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_chan_update_mip(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp, bool is_add)
{
    uint32 oam_check_type   = 0;
    uint32 index            = 0;
    uint32 mip_cmd          = 0;

    SYS_OAM_DBG_FUNC();

    if (!p_sys_chan_tp->key.section_oam)
    {
         /*index = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &index);

        mip_cmd = DRV_IOW(DsMpls_t, DsMpls_OamCheckType_f);

        if (is_add)
        {
            oam_check_type = SYS_OAM_TP_MIP_TYPE;
        }
        else
        {
            oam_check_type = SYS_OAM_TP_NONE_TYPE;
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mip_cmd, &oam_check_type));

        p_sys_chan_tp->key.mip_en = is_add;

    }
    else
    {

    }

    return CTC_E_NONE;
}

#define LMEP "Function Begin"

sys_oam_lmep_y1731_t*
_sys_greatbelt_tp_y1731_lmep_lkup(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{
    SYS_OAM_DBG_FUNC();

    return (sys_oam_lmep_y1731_t*)_sys_greatbelt_oam_lmep_lkup(lchip, &p_sys_chan_tp->com, NULL);
}

int32
_sys_greatbelt_tp_y1731_lmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_lmep_y1731_t* p_lmep_tp_para = (sys_oam_lmep_y1731_t*)p_oam_cmp->p_node_parm;
    sys_oam_lmep_y1731_t* p_lmep_tp_db   = (sys_oam_lmep_y1731_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if ((NULL != p_lmep_tp_db) && (NULL == p_lmep_tp_para))
    {
        return 1;
    }

    return 0;
}

sys_oam_lmep_y1731_t*
_sys_greatbelt_tp_y1731_lmep_build_node(uint8 lchip, ctc_oam_lmep_t* p_lmep_param)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_tp = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_lmep_tp = (sys_oam_lmep_y1731_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_lmep_y1731_t));
    if (NULL != p_sys_lmep_tp)
    {
        sal_memset(p_sys_lmep_tp, 0, sizeof(sys_oam_lmep_y1731_t));
        p_sys_lmep_tp->flag           = p_lmep_param->u.y1731_lmep.flag;

        p_sys_lmep_tp->com.mep_on_cpu = CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU);
        p_sys_lmep_tp->tx_csf_type    = p_lmep_param->u.y1731_lmep.tx_csf_type;
        p_sys_lmep_tp->com.lmep_index = p_lmep_param->lmep_index;
    }

    return p_sys_lmep_tp;
}

int32
_sys_greatbelt_tp_y1731_lmep_free_node(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_lmep_tp);
    p_sys_lmep_tp = NULL;
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_build_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_build_index(lchip, &p_sys_lmep_tp->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_free_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_free_index(lchip, &p_sys_lmep_tp->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_add_to_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_add_to_db(lchip, &p_sys_lmep_tp->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_del_from_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_db(lchip, &p_sys_lmep_tp->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_build_tp_data(uint8 lchip, ctc_oam_y1731_lmep_t* p_tp_y1731_lmep,
                                           sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    p_sys_lmep_tp->mep_id                   = p_tp_y1731_lmep->mep_id;
    p_sys_lmep_tp->ccm_interval             = p_tp_y1731_lmep->ccm_interval;
    p_sys_lmep_tp->ccm_en                   = 0;
    p_sys_lmep_tp->mpls_ttl                 = p_tp_y1731_lmep->mpls_ttl;
    p_sys_lmep_tp->nhid                     = p_tp_y1731_lmep->nhid;
    p_sys_lmep_tp->tx_cos_exp               = p_tp_y1731_lmep->tx_cos_exp;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_add_to_asic(uint8 lchip, ctc_oam_lmep_t* p_lmep_param,
                                         sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    ctc_oam_y1731_lmep_t* p_tp_y1731_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);
    CTC_PTR_VALID_CHECK(p_sys_lmep_tp);

    p_tp_y1731_lmep = &(p_lmep_param->u.y1731_lmep);
    if (CTC_FLAG_ISSET(p_tp_y1731_lmep->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_tp_y1731_lmep_build_tp_data(lchip, p_tp_y1731_lmep, p_sys_lmep_tp));

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_add_tp_y1731_to_asic(lchip, &p_sys_lmep_tp->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    SYS_OAM_DBG_FUNC();

    if (CTC_FLAG_ISSET(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_asic(lchip, &p_sys_lmep_tp->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_lmep_update_asic(uint8 lchip, ctc_oam_update_t* p_lmep_param,
                                         sys_oam_lmep_y1731_t* p_sys_lmep_tp)
{
    ctc_oam_update_t* p_tp_y1731_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);
    CTC_PTR_VALID_CHECK(p_sys_lmep_tp);

    p_tp_y1731_lmep = p_lmep_param;
    p_sys_lmep_tp->update_type = p_tp_y1731_lmep->update_type;

    switch (p_sys_lmep_tp->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_tp_y1731_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        p_sys_lmep_tp->ccm_en = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        p_sys_lmep_tp->tx_cos_exp = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        p_sys_lmep_tp->present_rdi = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        p_sys_lmep_tp->dm_en = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        p_sys_lmep_tp->sf_state  = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        if (1 == p_tp_y1731_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_tp->flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN);
        }

        p_sys_lmep_tp->tx_csf_en = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        p_sys_lmep_tp->tx_csf_type = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_NHOP:
        p_sys_lmep_tp->nhid        = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEL:
        p_sys_lmep_tp->md_level    = p_tp_y1731_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
        if (p_tp_y1731_lmep->update_value == 0xff)
        {
            p_sys_lmep_tp->trpt_en = 0;
        }
        else
        {
            p_sys_lmep_tp->trpt_en = 1;
            p_sys_lmep_tp->trpt_session_id = p_tp_y1731_lmep->update_value;
            if (p_sys_lmep_tp->trpt_session_id >= CTC_OAM_TRPT_SESSION_NUM)
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }
        }
        break;
    case  CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MASTER_GCHIP:
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_master_chip(p_sys_lmep_tp->com.p_sys_chan, &(p_tp_y1731_lmep->update_value)));
            return CTC_E_NONE;
        }
        break;
        
    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_tp_y1731_asic(lchip, &p_sys_lmep_tp->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_update_lmep_lm(uint8 lchip, ctc_oam_update_t* p_lmep_param, sys_oam_lmep_y1731_t* p_sys_lmep_tp_y1731)
{
    void* p_chan        = NULL;
    uint32 index        = 0;
    uint8  offset       = 0;
    uint32 chan_cmd     = 0;
    uint32 mpls_cmd     = 0;
    uint8  lm_en        = 0;

    ctc_oam_update_t* p_lmep = (ctc_oam_update_t*)p_lmep_param;
    sys_oam_chan_tp_t* p_sys_chan_tp          = NULL;

    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_mpls_pbt_bfd_oam_chan_t*  p_ds_mpls_oam_tcam_chan = NULL;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;

    SYS_OAM_DBG_FUNC();

    /*1. lookup chan and check exist*/
    p_sys_chan_tp = (sys_oam_chan_tp_t*)p_sys_lmep_tp_y1731->com.p_sys_chan;

    if (CTC_FLAG_ISSET(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
    {
        if((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE == p_lmep->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE == p_lmep->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS == p_lmep->update_type))
        {
            return CTC_E_OAM_INVALID_CFG_LM;
        }
    }

    /*2. lmep db & asic update*/
    switch (p_lmep->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN:
        if (1 == p_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);

            if (!p_sys_chan_tp->lm_index_alloced)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_tp->com, FALSE));
                p_sys_chan_tp->lm_index_alloced = TRUE;
            }
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);

            if (p_sys_chan_tp->lm_index_alloced)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_tp->com, FALSE));
                p_sys_chan_tp->lm_index_alloced = FALSE;
            }
            p_sys_chan_tp->lm_type = CTC_OAM_LM_TYPE_NONE;
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE:
        p_sys_chan_tp->lm_type = p_lmep->update_value;

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE:
        p_sys_chan_tp->lm_cos_type = p_lmep->update_value;

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
        p_sys_chan_tp->lm_cos = p_lmep->update_value;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    lm_en = CTC_FLAG_ISSET(p_sys_lmep_tp_y1731->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);

    if ((p_sys_chan_tp->key.section_oam)
        && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
    && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id))))
    {
        return CTC_E_NONE;
    }

    if (p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE != p_sys_chan_tp->lm_type))
    {
        if (SYS_OAM_KEY_HASH == p_sys_chan_tp->key.com.key_alloc)
        {
            sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
            p_chan                  = &ds_mpls_oam_chan;
            p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)p_chan;
            chan_cmd = DRV_IOR(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
        }
        else
        {
            sal_memset(&lpm_tcam_ad, 0, sizeof(ds_lpm_tcam_ad_t));
            offset = sizeof(ds_lpm_tcam_ad_t) - sizeof(ds_mpls_pbt_bfd_oam_chan_t);
            p_chan                  = &lpm_tcam_ad;
            p_ds_mpls_oam_tcam_chan = (ds_mpls_pbt_bfd_oam_chan_t*)((uint8*)p_chan + offset);
            chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }


        index = p_sys_chan_tp->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));
        p_ds_mpls_oam_tcam_chan->mpls_lm_type   = 0;

        p_ds_mpls_oam_tcam_chan->lm_index_base  = p_sys_chan_tp->com.lm_index_base;
        p_ds_mpls_oam_tcam_chan->lm_cos         = p_sys_chan_tp->lm_cos;
        p_ds_mpls_oam_tcam_chan->lm_cos_type    = p_sys_chan_tp->lm_cos_type;
        p_ds_mpls_oam_tcam_chan->lm_type        = p_sys_chan_tp->lm_type;

        if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
        }
        else
        {
            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));


    }

    if (!p_sys_chan_tp->key.section_oam)
    {
        ds_mpls_t ds_mpls;
        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
        mpls_cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*index    = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

        ds_mpls.mpls_lm_type    = 0;
        ds_mpls.lm_base         = p_sys_chan_tp->com.lm_index_base;
        ds_mpls.lm_cos          = p_sys_chan_tp->lm_cos;
        ds_mpls.lm_cos_type     = p_sys_chan_tp->lm_cos_type;
        ds_mpls.lm_type         = p_sys_chan_tp->lm_type;

        mpls_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

        /*update nexthop to enbale egress lm to be add*/
        sys_greatbelt_mpls_nh_update_oam_en(lchip, p_sys_lmep_tp_y1731->nhid, p_sys_chan_tp->out_label, lm_en);

    }
    else
    { /*need update port or interface lm enable*/
        uint32 cmd      = 0;
        uint32 field    = 0;

        field = lm_en;
        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
        {
            index = p_sys_chan_tp->key.gport_l3if_id;
            cmd = DRV_IOW(DsSrcInterface_t, DsSrcInterface_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
            cmd = DRV_IOW(DsDestInterface_t, DsDestInterface_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
        }
        else
        {
            index = CTC_MAP_GPORT_TO_LPORT(p_sys_chan_tp->key.gport_l3if_id);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_update_master_gchip(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_tp = _sys_greatbelt_tp_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_tp)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }
    else
    {
        if (p_sys_chan_tp->com.link_oam)
        {
            return CTC_E_INVALID_CONFIG;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_master_chip((sys_oam_chan_com_t*)p_sys_chan_tp, &(p_lmep_param->update_value)));

    return CTC_E_NONE;
}



#define RMEP "Function Begin"

sys_oam_rmep_y1731_t*
_sys_greatbelt_tp_y1731_rmep_lkup(uint8 lchip, uint32 rmep_id, sys_oam_lmep_y1731_t* p_sys_lmep_tp_y1731)
{
    sys_oam_rmep_y1731_t sys_oam_rmep_tp_y1731;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_rmep_tp_y1731, 0, sizeof(sys_oam_rmep_y1731_t));
    sys_oam_rmep_tp_y1731.rmep_id = rmep_id;

    return (sys_oam_rmep_y1731_t*)_sys_greatbelt_oam_rmep_lkup(lchip, &p_sys_lmep_tp_y1731->com, &sys_oam_rmep_tp_y1731.com);
}

int32
_sys_greatbelt_tp_y1731_rmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_rmep_y1731_t* p_rmep_tp_y1731_para = (sys_oam_rmep_y1731_t*)p_oam_cmp->p_node_parm;
    sys_oam_rmep_y1731_t* p_rmep_tp_y1731_db   = (sys_oam_rmep_y1731_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_rmep_tp_y1731_para->rmep_id == p_rmep_tp_y1731_db->rmep_id)
    {
        return 1;
    }

    return 0;
}

sys_oam_rmep_y1731_t*
_sys_greatbelt_tp_y1731_rmep_build_node(uint8 lchip, ctc_oam_rmep_t* p_rmep_param)
{
    sys_oam_rmep_y1731_t* p_sys_oam_rmep_tp_y1731 = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_oam_rmep_tp_y1731 = (sys_oam_rmep_y1731_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_rmep_y1731_t));
    if (NULL != p_sys_oam_rmep_tp_y1731)
    {
        sal_memset(p_sys_oam_rmep_tp_y1731, 0, sizeof(sys_oam_rmep_y1731_t));
        p_sys_oam_rmep_tp_y1731->rmep_id        = p_rmep_param->u.y1731_rmep.rmep_id;
        p_sys_oam_rmep_tp_y1731->com.rmep_index = p_rmep_param->rmep_index;
    }

    return p_sys_oam_rmep_tp_y1731;
}

int32
_sys_greatbelt_tp_y1731_rmep_free_node(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_rmep_tp_y1731);
    p_sys_rmep_tp_y1731 = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_build_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_build_index(lchip, &p_sys_rmep_tp_y1731->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_free_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_free_index(lchip, &p_sys_rmep_tp_y1731->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_add_to_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_add_to_db(lchip, &p_sys_rmep_tp_y1731->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_del_from_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_db(lchip, &p_sys_rmep_tp_y1731->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_build_eth_data(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
                                            sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    ctc_oam_y1731_rmep_t* p_tp_y1731_rmep = NULL;

    SYS_OAM_DBG_FUNC();

    p_tp_y1731_rmep = (ctc_oam_y1731_rmep_t*)&p_rmep_param->u.y1731_rmep;

    p_sys_rmep_tp_y1731->flag                       = p_tp_y1731_rmep->flag;
    p_sys_rmep_tp_y1731->rmep_id                    = p_tp_y1731_rmep->rmep_id;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_add_to_asic(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
                                         sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep_tp_y1731);

    CTC_ERROR_RETURN(_sys_greatbelt_tp_y1731_rmep_build_eth_data(lchip, p_rmep_param, p_sys_rmep_tp_y1731));
    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_add_tp_y1731_to_asic(lchip, &p_sys_rmep_tp_y1731->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_asic(lchip, &p_sys_rmep_tp_y1731->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_y1731_rmep_update_asic(uint8 lchip, ctc_oam_update_t* p_rmep_param,
                                         sys_oam_rmep_y1731_t* p_sys_rmep_tp_y1731)
{
    ctc_oam_update_t* p_tp_y1731_rmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep_tp_y1731);

    p_tp_y1731_rmep = p_rmep_param;
    p_sys_rmep_tp_y1731->update_type = p_tp_y1731_rmep->update_type;

    switch (p_sys_rmep_tp_y1731->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_tp_y1731_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
        }

        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        p_sys_rmep_tp_y1731->sf_state    = p_tp_y1731_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        if (1 == p_tp_y1731_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep_tp_y1731->flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);
        }

        p_sys_rmep_tp_y1731->csf_en = p_tp_y1731_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        p_sys_rmep_tp_y1731->d_csf    = p_tp_y1731_rmep->update_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_update_tp_y1731_asic(lchip, &p_sys_rmep_tp_y1731->com));

    return CTC_E_NONE;
}

#define INIT "Function Begin"

int32
_sys_greatbelt_tp_y1731_register_init(uint8 lchip)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    oam_parser_ether_ctl_t  oam_parser_ether_ctl;
    ipe_mpls_ctl_t          ipe_mpls_ctl;
    epe_acl_qos_ctl_t       epe_acl_qos_ctl;
    oam_ether_tx_ctl_t      oam_ether_tx_ctl;

    /*Parser*/
    field_val = P_COMMON_OAM_MASTER_GLB(lchip).tp_y1731_ach_chan_type;
    cmd = DRV_IOW(ParserLayer4AchCtl_t, ParserLayer4AchCtl_AchY1731Type_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&oam_parser_ether_ctl, 0, sizeof(oam_parser_ether_ctl_t));
    cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_parser_ether_ctl));
    oam_parser_ether_ctl.ach_y1731_chan_type            = P_COMMON_OAM_MASTER_GLB(lchip).tp_y1731_ach_chan_type;
    oam_parser_ether_ctl.mpls_tp_oam_alert_label        = 13;

    oam_parser_ether_ctl.mplstp_lbm_tlv_offset          = 4;
    oam_parser_ether_ctl.mplstp_lbm_tlv_offset_chk      = 1;
    oam_parser_ether_ctl.mplstp_lbm_tlv_sub_type_chk    = 1;
    oam_parser_ether_ctl.mplstp_lbm_tlv_len_chk         = 1;
    oam_parser_ether_ctl.mplstp_lbm_tlv_type_chk        = 1;

    cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_parser_ether_ctl));

    /*ipe lookup*/
    field_val = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_MplsSectionOamUsePort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&ipe_mpls_ctl, 0, sizeof(ipe_mpls_ctl_t));
    cmd = DRV_IOR(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mpls_ctl));
    ipe_mpls_ctl.oam_alert_label1   = 13;
    ipe_mpls_ctl.gal_sbit_check_en  = 1;
    ipe_mpls_ctl.gal_ttl_check_en   = 1;
    cmd = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mpls_ctl));

    sal_memset(&epe_acl_qos_ctl, 0, sizeof(epe_acl_qos_ctl_t));
    cmd = DRV_IOR(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_acl_qos_ctl));
    epe_acl_qos_ctl.oam_alert_label1            = 13;
    epe_acl_qos_ctl.mpls_section_oam_use_port   = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
    cmd = DRV_IOW(EpeAclQosCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_acl_qos_ctl));

    /*ipe/epe oam*/
    field_val = 1;
    cmd = DRV_IOW(IpeOamCtl_t, IpeOamCtl_LmProactiveMplsOamPacket_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(EpeOamCtl_t, EpeOamCtl_LmProactiveMplsOamPacket_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*OAM*/
    field_val = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_PortbasedSectionOam_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&oam_ether_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_tx_ctl));
    oam_ether_tx_ctl.ach_header_l               = 0x1000;
    oam_ether_tx_ctl.ach_y1731_chan_type        = P_COMMON_OAM_MASTER_GLB(lchip).tp_y1731_ach_chan_type;
    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_tx_ctl));

    _sys_greatbelt_mpls_for_tp_oam(lchip, SYS_OAM_TP_MEP_TYPE);

    return CTC_E_NONE;
}


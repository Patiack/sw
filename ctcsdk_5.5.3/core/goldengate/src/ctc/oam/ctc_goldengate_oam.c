/**
 @file ctc_goldengate_oam.c

 @date 2010-3-9

 @version v2.0

  This file contains  OAM (Ethernet OAM/MPLS OAM/PBX OAM and EFM OAM) associated APIs's implementation
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_oam.h"
#include "ctc_error.h"
#include "ctc_goldengate_oam.h"
#include "sys_goldengate_oam.h"
#include "sys_goldengate_chip.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
 *
* Function
*
*****************************************************************************/
int32
ctc_goldengate_oam_add_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN( CTC_E_OAM_MAID_ENTRY_EXIST,
                                  ret,
                                  sys_goldengate_oam_add_maid(lchip, p_maid));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_oam_remove_maid(lchip, p_maid);
    }

    return ret;
}

int32
ctc_goldengate_oam_remove_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN( CTC_E_OAM_MAID_ENTRY_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_remove_maid(lchip, p_maid));
    }

    return ret;
}

int32
ctc_goldengate_oam_add_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_EXIST,
                                  ret,
                                  sys_goldengate_oam_add_lmep(lchip, p_lmep));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_oam_remove_lmep(lchip, p_lmep);
    }

    return ret;

}

int32
ctc_goldengate_oam_remove_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_remove_lmep(lchip, p_lmep));

    }

    return ret;
}

int32
ctc_goldengate_oam_update_lmep(uint8 lchip, ctc_oam_update_t* p_lmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_update_lmep(lchip, p_lmep));
    }

    return ret;
}

int32
ctc_goldengate_oam_add_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_add_rmep(lchip, p_rmep));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_oam_remove_rmep(lchip, p_rmep);
    }

    return ret;
}

int32
ctc_goldengate_oam_remove_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_remove_rmep(lchip, p_rmep));
    }

    return ret;
}

int32
ctc_goldengate_oam_update_rmep(uint8 lchip, ctc_oam_update_t* p_rmep)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_update_rmep(lchip, p_rmep));
    }

    return ret;
}

int32
ctc_goldengate_oam_add_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_ENTRY_EXIST,
                                  ret,
                                  sys_goldengate_oam_add_mip(lchip, p_mip));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_oam_remove_mip(lchip, p_mip);
    }

    return ret;
}

int32
ctc_goldengate_oam_remove_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_ENTRY_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_remove_mip(lchip, p_mip));
    }

    return ret;
}

int32
ctc_goldengate_oam_set_property(uint8 lchip, ctc_oam_property_t* p_prop)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    uint8 all_chip                 = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);
    if (CTC_OAM_PROPERTY_TYPE_COMMON == p_prop->oam_pro_type)
    {
        all_chip = 1;
    }
    else if(CTC_OAM_PROPERTY_TYPE_Y1731 == p_prop->oam_pro_type)
    {
        switch(p_prop->u.y1731.cfg_type)
        {
        case CTC_OAM_Y1731_CFG_TYPE_PORT_OAM_EN:
        case CTC_OAM_Y1731_CFG_TYPE_PORT_TUNNEL_EN:
        case CTC_OAM_Y1731_CFG_TYPE_ADD_EDGE_PORT:
        case CTC_OAM_Y1731_CFG_TYPE_REMOVE_EDGE_PORT:
        case CTC_OAM_Y1731_CFG_TYPE_PORT_LM_EN:
            SYS_MAP_GPORT_TO_LCHIP(p_prop->u.y1731.gport, lchip);
            break;

        case CTC_OAM_Y1731_CFG_TYPE_BIG_CCM_INTERVAL_TO_CPU:
        case CTC_OAM_Y1731_CFG_TYPE_LBM_PROC_IN_ASIC:
        case CTC_OAM_Y1731_CFG_TYPE_LMM_PROC_IN_ASIC:
        case CTC_OAM_Y1731_CFG_TYPE_DM_PROC_IN_ASIC:
        case CTC_OAM_Y1731_CFG_TYPE_SLM_PROC_IN_ASIC:
        case CTC_OAM_Y1731_CFG_TYPE_ALL_TO_CPU:
        case CTC_OAM_Y1731_CFG_TYPE_TX_VLAN_TPID:
        case CTC_OAM_Y1731_CFG_TYPE_RX_VLAN_TPID:
        case CTC_OAM_Y1731_CFG_TYPE_SENDER_ID:
        case CTC_OAM_Y1731_CFG_TYPE_BRIDGE_MAC:
        case CTC_OAM_Y1731_CFG_TYPE_LBR_SA_USE_LBM_DA:
        case CTC_OAM_Y1731_CFG_TYPE_LBR_SA_SHARE_MAC:
        case CTC_OAM_Y1731_CFG_TYPE_L2VPN_OAM_MODE:

        default:
            all_chip = 1;
        }
    }
    else if(CTC_OAM_PROPERTY_TYPE_BFD == p_prop->oam_pro_type)
    {
        all_chip = 1;
    }


    lchip_start = lchip;
    lchip_end   = lchip + 1;
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, all_chip)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_set_property(lchip, p_prop));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_get_defect_info(uint8 lchip, void* p_defect_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_get_defect_info(lchip, p_defect_info));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_get_mep_info(uint8 lchip, ctc_oam_mep_info_t* p_mep_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_get_mep_info(lchip, p_mep_info, 0));
    }

    return CTC_E_NONE;
}


int32
ctc_goldengate_oam_get_mep_info_with_key(uint8 lchip, ctc_oam_mep_info_with_key_t* p_mep_info)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_OAM_CHAN_LMEP_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_get_mep_info(lchip, p_mep_info, 1));
    }

    return ret;
}

int32
ctc_goldengate_oam_get_stats(uint8 lchip, ctc_oam_stats_info_t* p_stat_info)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN( CTC_E_OAM_CHAN_ENTRY_NOT_FOUND,
                                  ret,
                                  sys_goldengate_oam_get_stats_info(lchip, p_stat_info));
    }

    return ret;
}

int32
ctc_goldengate_oam_set_trpt_cfg(uint8 lchip, ctc_oam_trpt_t* p_trpt)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_set_trpt_cfg(lchip, p_trpt));
    }
    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_set_trpt_en(uint8 lchip, uint8 gchip, uint8 session_id, uint8 enable)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);
    CTC_ERROR_RETURN(sys_goldengate_oam_set_trpt_en(lchip, gchip, session_id, enable));

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_get_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id,  ctc_oam_trpt_stats_t* p_trpt_stats)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);
    CTC_ERROR_RETURN(sys_goldengate_oam_get_trpt_stats(lchip, gchip, session_id, p_trpt_stats));

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_clear_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);
    CTC_ERROR_RETURN(sys_goldengate_oam_clear_trpt_stats(lchip, gchip, session_id));

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_init(uint8 lchip, ctc_oam_global_t* p_cfg)
{
    ctc_oam_global_t oam_global;
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    if (NULL == p_cfg)
    {
        sal_memset(&oam_global, 0, sizeof(ctc_oam_global_t));

        /*eth oam global param*/
        oam_global.mep_index_alloc_by_sdk       = 1;

        /*TP Y.1731 OAM*/
        oam_global.tp_y1731_ach_chan_type       = 0x8902;
        oam_global.tp_section_oam_based_l3if    = 0;

        oam_global.tp_csf_ach_chan_type = 9;
        oam_global.tp_csf_los = 0x7;
        oam_global.tp_csf_fdi = 0x1;
        oam_global.tp_csf_rdi = 0x2;
        oam_global.tp_csf_clear = 0x0;
    }
    else
    {
        sal_memcpy(&oam_global, p_cfg, sizeof(ctc_oam_global_t));
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_init(lchip, &oam_global));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_oam_deinit(uint8 lchip)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OAM);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_deinit(lchip));
    }

    return CTC_E_NONE;
}


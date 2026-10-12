/**
 @file ctc_goldengate_oam.c

 @date 2010-3-9

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
#include "sys_goldengate_oam_db.h"
#include "sys_goldengate_oam.h"
#include "sys_goldengate_oam_cfm.h"
#include "sys_goldengate_oam_tp_y1731.h"
#include "sys_goldengate_oam_bfd.h"
#include "sys_goldengate_oam_trpt.h"

#include "sys_goldengate_mpls.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_oam_debug.h"
#include "sys_goldengate_ftm.h"
#include "goldengate/include/drv_io.h"
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

sys_oam_master_t* g_gg_oam_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
/****************************************************************************
*
* Function
*
**
***************************************************************************/

int32
sys_goldengate_oam_add_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_maid);
    oam_type = p_maid->mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_ADD));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_ADD))(lchip, (void*)p_maid);

    return ret;
}

int32
sys_goldengate_oam_remove_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_maid);
    oam_type = p_maid->mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_DEL));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_DEL))(lchip, (void*)p_maid);

    return ret;
}

int32
sys_goldengate_oam_add_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);
    oam_type = p_lmep->key.mep_type;
    CTC_PTR_VALID_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD));

    if (sys_goldengate_oam_get_session_num(lchip) >= SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_OAM))
    {
        return CTC_E_NO_RESOURCE;
    }

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD))(lchip, (void*)p_lmep);

    return ret;
}

int32
sys_goldengate_oam_remove_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);
    oam_type = p_lmep->key.mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_DEL));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_DEL))(lchip, (void*)p_lmep);

    return ret;
}

int32
sys_goldengate_oam_update_lmep(uint8 lchip, ctc_oam_update_t* p_lmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);
    oam_type = p_lmep->key.mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_UPDATE));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_UPDATE))(lchip, (void*)p_lmep);

    return ret;
}

int32
sys_goldengate_oam_add_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rmep);
    oam_type = p_rmep->key.mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_ADD));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_ADD))(lchip, (void*)p_rmep);

    return ret;
}

int32
sys_goldengate_oam_remove_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rmep);
    oam_type = p_rmep->key.mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_DEL));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_DEL))(lchip, (void*)p_rmep);

    return ret;
}

int32
sys_goldengate_oam_update_rmep(uint8 lchip, ctc_oam_update_t* p_rmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_rmep);
    oam_type = p_rmep->key.mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_UPDATE));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_UPDATE))(lchip, (void*)p_rmep);

    return ret;
}

int32
sys_goldengate_oam_add_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
{
    int32 ret       = CTC_E_NONE;
    uint8 oam_type  = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mip);
    oam_type = p_mip->key.mep_type;

    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_ADD));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_ADD))(lchip, (void*)p_mip);
    return ret;
}

int32
sys_goldengate_oam_remove_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
{
    int32 ret       = CTC_E_NONE;
    uint8 oam_type  = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mip);
    oam_type = p_mip->key.mep_type;

    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_DEL));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_DEL))(lchip, (void*)p_mip);

    return ret;
}

int32
sys_goldengate_oam_set_property(uint8 lchip, ctc_oam_property_t* p_prop)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_pro_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);
    oam_pro_type = p_prop->oam_pro_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_pro_type, SYS_OAM_GLOB, SYS_OAM_CONFIG));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_pro_type, SYS_OAM_GLOB, SYS_OAM_CONFIG))(lchip, (void*)p_prop);

    return ret;
}

int32
sys_goldengate_oam_get_defect_info(uint8 lchip, void* p_defect_info)
{
    int32 ret      = CTC_E_NONE;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_defect_info);

    ret = _sys_goldengate_oam_defect_read_defect_status(lchip, p_defect_info);

    return ret;
}

int32
sys_goldengate_oam_get_mep_info(uint8 lchip,  void* p_mep_info, uint8 with_key)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mep_info);

    if(with_key)
    {
        oam_type = ((ctc_oam_mep_info_with_key_t*)p_mep_info)->key.mep_type;
        SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_GET_INFO));

        ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_GET_INFO))(lchip, (void*)p_mep_info);
    }
    else
    {
        ret = _sys_goldengate_oam_get_mep_info(lchip, (ctc_oam_mep_info_t*)p_mep_info);
    }

    return ret;
}

int32
sys_goldengate_oam_get_stats_info(uint8 lchip, ctc_oam_stats_info_t* p_stat_info)
{

    int32 ret       = CTC_E_NONE;
    uint8 oam_type  = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stat_info);
    oam_type = p_stat_info->key.mep_type;

    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_STATS, SYS_OAM_CONFIG));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_STATS, SYS_OAM_CONFIG))(lchip, (void*)p_stat_info);

    return ret;
}


int32
sys_goldengate_oam_init(uint8 lchip, ctc_oam_global_t* p_cfg)
{
    int32 ret      = CTC_E_NONE;

    CTC_PTR_VALID_CHECK(p_cfg);

    if (0 == SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_OAM))
    {
        return CTC_E_NONE;
    }

    if (g_gg_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_oam_db_init(lchip, p_cfg));
    CTC_ERROR_RETURN(sys_goldengate_cfm_init(lchip, p_cfg));

    if (_sys_goldengate_oam_get_mpls_entry_num(lchip))
    {
        CTC_ERROR_RETURN(sys_goldengate_oam_tp_y1731_init(lchip, p_cfg));
        CTC_ERROR_RETURN(sys_goldengate_oam_tp_bfd_init(lchip, p_cfg));
        CTC_ERROR_RETURN(sys_goldengate_oam_mpls_bfd_init(lchip, p_cfg));
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_ip_bfd_init(lchip, p_cfg));

    if (g_gg_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_init(lchip));
    if (NULL == g_gg_oam_master[lchip]->error_cache_cb)
    {
        g_gg_oam_master[lchip]->mep_defect_bitmap  = (uint32*)mem_malloc(MEM_OAM_MODULE, TABLE_MAX_INDEX(DsEthMep_t)*4);
        if (g_gg_oam_master[lchip]->mep_defect_bitmap)
        {
            sal_memset((uint8*)g_gg_oam_master[lchip]->mep_defect_bitmap, 0 , TABLE_MAX_INDEX(DsEthMep_t)*4);
            g_gg_oam_master[lchip]->error_cache_cb = sys_goldengate_isr_oam_process_isr;
        }
    }

    return ret;
}

int32
sys_goldengate_oam_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (0 == SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_OAM))
    {
        return CTC_E_NONE;
    }

    sys_goldengate_oam_trpt_deinit(lchip);
    _sys_goldengate_oam_db_deinit(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_oam_isr_defect(uint8 lchip, uint32 intr, void* p_data)
{
     /*static ctc_oam_defect_info_t  defect_info;*/

    static sys_oam_defect_param_t defect_parm;

    uint8 do_defect_cache_cnt = 0;
    uint8 do_defect_cache_num = 2;
    int32 ret = CTC_E_NONE;

     /*sal_memset(&defect_info,    0, sizeof(ctc_oam_defect_info_t));*/

    SYS_OAM_INIT_CHECK(lchip);
    sal_memset(&defect_parm,    0, sizeof(sys_oam_defect_param_t));

    /* 1. read valid bitmap */
    CTC_ERROR_RETURN(sys_goldengate_oam_get_defect_info(lchip, &defect_parm.defect_info));
    OAM_LOCK(lchip);
    while ((do_defect_cache_cnt < do_defect_cache_num)  && defect_parm.defect_info.valid_cache_num)
    {

        /* 2. call event callback function */
        if(g_gg_oam_master[lchip]->defect_process_cb)
        {
            defect_parm.isr_state = (0 == do_defect_cache_cnt)? 1: 2;
            g_gg_oam_master[lchip]->defect_process_cb(lchip, &defect_parm);
        }

        sal_memset(&defect_parm.defect_info,    0, sizeof(ctc_oam_defect_info_t));
        CTC_ERROR_GOTO(sys_goldengate_oam_get_defect_info(lchip, &defect_parm.defect_info), ret, End);
        do_defect_cache_cnt++;
    }

    /*3. last process*/
    if(g_gg_oam_master[lchip]->defect_process_cb)
    {
        defect_parm.isr_state = 3;
        g_gg_oam_master[lchip]->defect_process_cb(lchip, &defect_parm);
    }

End:
    OAM_UNLOCK(lchip);
    return ret;
}

/**
 @brief  Register oam error cache callback
*/
int32
sys_goldengate_oam_set_defect_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    SYS_OAM_INIT_CHECK(lchip);
    if (g_gg_oam_master[lchip]->mep_defect_bitmap)
    {
        mem_free(g_gg_oam_master[lchip]->mep_defect_bitmap);
    }
    g_gg_oam_master[lchip]->error_cache_cb = cb;
    return CTC_E_NONE;
}


int32
sys_goldengate_oam_get_defect_cb(uint8 lchip, void** cb)
{
    SYS_OAM_INIT_CHECK(lchip);
    *cb = g_gg_oam_master[lchip]->error_cache_cb;
    return CTC_E_NONE;
}

#define __THROUGH_PUT__
/**
@ brief create throughput section
*/
int32
sys_goldengate_oam_set_trpt_cfg(uint8 lchip, ctc_oam_trpt_t* p_trpt)
{
    uint16  lport = 0;
     /*uint8 index = 0;*/
    MsPacketHeader_m bridge_header;
    void* p_pkt_header = NULL;
    int32 ret = 0;

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trpt);

    /* throughput config value check */
    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_glb_cfg_check(lchip, p_trpt));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_trpt->gport, lchip, lport);

    SYS_OAM_DBG_INFO("session_id:%d\ttx_mode:%d\tgport:%d\nvlan_id:%d\tpattern_type:%d\trepeat_pattern:%d\npacket_num:%d\tpkt_size:%d\trate:%d\n",
       p_trpt->session_id, p_trpt->tx_mode, p_trpt->gport, p_trpt->vlan_id, p_trpt->pattern_type,
       p_trpt->repeat_pattern, p_trpt->packet_num, p_trpt->size, p_trpt->rate);

    SYS_OAM_DBG_INFO("user-defined packet length:%d \n", p_trpt->header_len);
/*
    for (index = 0; index < p_trpt->header_len; index++)
    {
        SYS_OAM_DBG_INFO("0x%02x  ", *((uint8*)p_trpt->pkt_header+index));
        if (((index+1) % 0x10) == 0)
        {
            SYS_OAM_DBG_INFO("\n");
        }
    }
    SYS_OAM_DBG_INFO("\n");
*/
    p_pkt_header = (uint8*)mem_malloc(MEM_OAM_MODULE, (p_trpt->header_len + SYS_GG_PKT_HEADER_LEN + 8));
    if (NULL == p_pkt_header)
    {
        return CTC_E_NO_MEMORY;
    }

    /*1. cfg bridge header */
    sal_memset(&bridge_header, 0, sizeof(MsPacketHeader_m));
    ret = sys_goldengate_oam_trpt_set_bridge_hdr(lchip,  p_trpt, &bridge_header);
    if (ret < 0)
    {
        mem_free(p_pkt_header);
        return ret;
    }

    sal_memcpy((uint8*)p_pkt_header, (uint8*)&bridge_header, SYS_GG_PKT_HEADER_LEN);
    sal_memcpy((uint8*)((uint8*)p_pkt_header + SYS_GG_PKT_HEADER_LEN), (uint8*)p_trpt->pkt_header, p_trpt->header_len);

    /*2. cfg user data */
    ret = sys_goldengate_oam_trpt_set_user_data(lchip, p_trpt->session_id,
                                            (void*)p_pkt_header, p_trpt->header_len + SYS_GG_PKT_HEADER_LEN);
    if (ret < 0)
    {
        mem_free(p_pkt_header);
        return ret;
    }

    /* 3. cfg auto_gen rate */
    ret = sys_goldengate_oam_trpt_set_cfg(lchip, p_trpt->session_id, p_trpt);
    if (ret < 0)
    {
        mem_free(p_pkt_header);
        return ret;
    }

    /* free memory */
    mem_free(p_pkt_header);

    return CTC_E_NONE;
}

/**
@ brief create throughput section
*/
int32
sys_goldengate_oam_set_trpt_en(uint8 lchip, uint8 gchip, uint8 session_id, uint8 enable)
{

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_CHIP_IS_REMOTE;
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_set_enable(lchip, session_id, enable));
    return CTC_E_NONE;
}

int32
sys_goldengate_oam_get_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id, ctc_oam_trpt_stats_t* p_trpt_stats)
{

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_trpt_stats);

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (NULL == p_trpt_stats)
    {
        return CTC_E_INVALID_PTR;
    }

    if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_CHIP_IS_REMOTE;
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_get_stats(lchip, session_id, p_trpt_stats));

    return CTC_E_NONE;
}

int32
sys_goldengate_oam_clear_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id)
{

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (FALSE == sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_CHIP_IS_REMOTE;
    }

    CTC_ERROR_RETURN(sys_goldengate_oam_trpt_clear_stats(lchip, session_id));

    return CTC_E_NONE;
}



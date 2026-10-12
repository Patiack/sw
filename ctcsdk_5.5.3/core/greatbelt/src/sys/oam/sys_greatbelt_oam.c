/**
 @file ctc_greatbelt_oam.c

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
#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_cfm.h"
#include "sys_greatbelt_oam_tp_y1731.h"
#include "sys_greatbelt_oam_bfd.h"
#include "sys_greatbelt_oam_trpt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_common.h"

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

sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


/****************************************************************************
*
* Function
*
**
***************************************************************************/

int32
sys_greatbelt_oam_add_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
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
sys_greatbelt_oam_remove_maid(uint8 lchip, ctc_oam_maid_t* p_maid)
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
sys_greatbelt_oam_add_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    int32 ret      = CTC_E_NONE;
    uint8 oam_type = 0;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);
    oam_type = p_lmep->key.mep_type;
    CTC_PTR_VALID_CHECK(SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD));

    ret = (SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD))(lchip, (void*)p_lmep);
    return ret;
}

int32
sys_greatbelt_oam_remove_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep)
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
sys_greatbelt_oam_update_lmep(uint8 lchip, ctc_oam_update_t* p_lmep)
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
sys_greatbelt_oam_add_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
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
sys_greatbelt_oam_remove_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep)
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
sys_greatbelt_oam_update_rmep(uint8 lchip, ctc_oam_update_t* p_rmep)
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
sys_greatbelt_oam_add_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
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
sys_greatbelt_oam_remove_mip(uint8 lchip, ctc_oam_mip_t* p_mip)
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
sys_greatbelt_oam_set_property(uint8 lchip, ctc_oam_property_t* p_prop)
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
sys_greatbelt_oam_get_defect_info(uint8 lchip, void* p_defect_info)
{
    int32 ret      = CTC_E_NONE;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_defect_info);

    ret = _sys_greatbelt_oam_defect_read_defect_status(lchip, p_defect_info);

    return ret;
}

int32
sys_greatbelt_oam_get_mep_info(uint8 lchip,  void* p_mep_info, uint8 with_key)
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
        ret = _sys_greatbelt_oam_get_mep_info(lchip, (ctc_oam_mep_info_t*)p_mep_info);
    }

    return ret;
}

int32
sys_greatbelt_oam_get_stats_info(uint8 lchip, ctc_oam_stats_info_t* p_stat_info)
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
sys_greatbelt_oam_isr_defect(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 gchip = 0;
    ctc_oam_defect_info_t*  defect_info = NULL;
    ctc_oam_mep_info_t*     mep_info = NULL;
    ctc_oam_event_t* event  = NULL;

    uint8 defect_index      = 0;
    uint8 mep_bit_index     = 0;
    uint32 mep_index_bitmap = 0;
    uint32 mep_index_base   = 0;
    uint32 mep_index        = 0;
    int32 ret = 0;

    uint8 do_defect_cache_num   = 2;
    uint8 do_defect_cache_cnt   = 0;
/*
    sal_systime_t tv1;
    sal_systime_t tv2;

    sal_gettime(&tv1);
*/
    SYS_OAM_INIT_CHECK(lchip);

    defect_info = (ctc_oam_defect_info_t*)mem_malloc(MEM_OAM_MODULE, sizeof(ctc_oam_defect_info_t));
    if (NULL == defect_info)
    {
        return CTC_E_NO_MEMORY;
    }
    mep_info = (ctc_oam_mep_info_t*)mem_malloc(MEM_OAM_MODULE, sizeof(ctc_oam_mep_info_t));
    if (NULL == mep_info)
    {
        mem_free(defect_info);
        return CTC_E_NO_MEMORY;
    }
    CTC_ERROR_GOTO(sys_greatbelt_get_gchip_id(lchip, &gchip), ret, error);

    event = (ctc_oam_event_t*)&g_gb_oam_master[lchip]->event;
    sal_memset(mep_info,       0, sizeof(ctc_oam_mep_info_t));
    sal_memset(event,          0, sizeof(ctc_oam_event_t));

ErrorCache:
    /* 1. read valid bitmap */
    sal_memset(defect_info,    0, sizeof(ctc_oam_defect_info_t));
    CTC_ERROR_GOTO(sys_greatbelt_oam_get_defect_info(lchip, defect_info), ret, error);

    /* 2. call event callback function */
    if (g_gb_oam_master[lchip]->error_cache_cb)
    {
        if(do_defect_cache_cnt >= do_defect_cache_num && (defect_info->valid_cache_num == 0))
        {
            if (event->valid_entry_num > 0)
            {
                g_gb_oam_master[lchip]->error_cache_cb(gchip, event);
            }
            /*
            sal_gettime(&tv2);
            printf("Enter sys_greatbelt_oam_isr_defect(): tv_sec; %d, tv_usec; %d\n", (int)tv1.tv_sec, (int)tv1.tv_usec);
            printf("Exit sys_greatbelt_oam_isr_defect(): tv_sec; %d, tv_usec; %d\n", (int)tv2.tv_sec, (int)tv2.tv_usec);
            */

            goto error;
        }
        do_defect_cache_cnt++;

        for (defect_index = 0; defect_index < defect_info->valid_cache_num; defect_index++)
        {
            mep_index_bitmap    = defect_info->mep_index_bitmap[defect_index];
            mep_index_base      = defect_info->mep_index_base[defect_index];
            SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "  mep_index_base      : 0x%-4x\n",  mep_index_base);
            for (mep_bit_index = 0; mep_bit_index < 32; mep_bit_index++)
            {
                if (IS_BIT_SET(mep_index_bitmap, mep_bit_index))
                {
                    mep_index = mep_index_base * 32 + mep_bit_index;
                    mep_info->mep_index = mep_index;
                    ret = _sys_greatbelt_oam_get_mep_info(lchip, mep_info);
                    if(CTC_E_NONE != ret )
                    {
                        continue;
                    }

                    if((CTC_OAM_MEP_TYPE_IP_BFD == mep_info->mep_type)
                        ||(CTC_OAM_MEP_TYPE_MPLS_BFD == mep_info->mep_type)
                        ||(CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_info->mep_type))
                    {
                        /* send message to  bfd state process thread*/
                        if (((g_gb_oam_master[lchip]->write_idx + 1)%SYS_OAM_BFD_RMEP_NUM) == g_gb_oam_master[lchip]->read_idx)
                        {
                            /*error*/
                        }
                        else
                        {
                            if(mep_info->is_rmep)
                            {
                                g_gb_oam_master[lchip]->bfd_rmep_index[g_gb_oam_master[lchip]->write_idx] = mep_info->mep_index;
                            }
                            else
                            {
                                g_gb_oam_master[lchip]->bfd_rmep_index[g_gb_oam_master[lchip]->write_idx] = mep_info->mep_index + 1;
                            }
                            g_gb_oam_master[lchip]->write_idx = (g_gb_oam_master[lchip]->write_idx + 1)%SYS_OAM_BFD_RMEP_NUM;
                            g_gb_oam_master[lchip]->bfd_state_flag = 1;
                            sal_sem_give(g_gb_oam_master[lchip]->bfd_state_sem);                            
                        }
                    }

                    _sys_greatbelt_oam_build_defect_event(lchip, mep_info, event);

                    if (event->valid_entry_num >= CTC_OAM_EVENT_MAX_ENTRY_NUM)
                    {
                        g_gb_oam_master[lchip]->error_cache_cb(gchip, event);
                        sal_memset(event, 0, sizeof(ctc_oam_event_t));
                    }
                }
            }
        }
        goto ErrorCache;
    }

error:

    mem_free(defect_info);
    mem_free(mep_info);

    return ret;
}


int32
sys_greatbelt_oam_isr_get_defect_name(uint8 lchip, ctc_oam_event_entry_t* mep_info)
{

    uint32 mep_defect_bitmap_cmp    = 0;
    uint32 mep_defect_bitmap_temp   = 0;
    uint32 mep_index = 0;
    uint8  index     = 0;

    if (!mep_info->is_remote)
    {
        mep_index = mep_info->lmep_index;
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  mep_index        : 0x%-4x\n",  mep_index);
        mep_defect_bitmap_temp = mep_info->event_bmp;
        mep_defect_bitmap_temp = ((mep_info->event_bmp & CTC_OAM_DEFECT_UNEXPECTED_LEVEL)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_MISMERGE)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_UNEXPECTED_MEP)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_RDI_TX)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_BFD_DOWN)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_BFD_INIT)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_BFD_UP));
    }
    else
    {
        mep_index = mep_info->rmep_index;
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  rmep_index       : 0x%-4x\n",  mep_index);
        mep_defect_bitmap_temp = ((mep_info->event_bmp & CTC_OAM_DEFECT_DLOC)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_UNEXPECTED_PERIOD)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_MAC_STATUS_CHANGE)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_SRC_MAC_MISMATCH)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_EVENT_RDI_RX)
                                  | (mep_info->event_bmp & CTC_OAM_DEFECT_CSF));
    }

    mep_defect_bitmap_cmp = mep_defect_bitmap_temp ^ (g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index]);

    if (0 == mep_defect_bitmap_cmp)
    {
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "  defect name: %s\n", "Defect no name");
    }

    for (index = 0; index < 32; index++)
    {
        if (CTC_IS_BIT_SET(mep_defect_bitmap_cmp, index))
        {
            switch (1 << index)
            {
            case CTC_OAM_DEFECT_EVENT_BFD_DOWN:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_BFD_DOWN))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_DOWN);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_INIT);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_UP);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "BFD state down");
                }
                break;

            case CTC_OAM_DEFECT_EVENT_BFD_INIT:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_BFD_INIT))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_INIT);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_DOWN);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_UP);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "BFD state init");
                }
                break;

            case CTC_OAM_DEFECT_EVENT_BFD_UP:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_BFD_UP))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_UP);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_INIT);
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_BFD_DOWN);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "BFD state up");
                }
                break;

            case CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_RX_FIRST_PKT);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "1st packet set");
                }
                else
                {

                }

                break;

            case CTC_OAM_DEFECT_CSF:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_CSF))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_CSF);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "CSFDefect set");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_CSF);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "CSFDefect: clear");
                }

                break;

            case CTC_OAM_DEFECT_UNEXPECTED_LEVEL:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_UNEXPECTED_LEVEL))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_LEVEL);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "xconCCMdefect: low ccm");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_LEVEL);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "xconCCMdefect: low ccm clear");
                }

                break;

            case CTC_OAM_DEFECT_MISMERGE:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_MISMERGE))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_MISMERGE);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "xconCCMdefect: ma id mismatch");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_MISMERGE);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "xconCCMdefect: ma id mismatch clear");
                }

                break;

            case CTC_OAM_DEFECT_UNEXPECTED_PERIOD:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_UNEXPECTED_PERIOD))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_PERIOD);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "errorCCMdefect: ccm interval mismatch");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_PERIOD);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "errorCCMdefect: ccm interval mismatch clear");
                }

                break;

            case CTC_OAM_DEFECT_DLOC:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_DLOC))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_DLOC);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "SomeRMEPCCMDefect: dloc defect");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_DLOC);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "SomeRMEPCCMDefect: dloc defect clear");
                }

                break;

            case CTC_OAM_DEFECT_UNEXPECTED_MEP:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_UNEXPECTED_MEP))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_MEP);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "errorCCMdefect: rmep not found");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_UNEXPECTED_MEP);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "errorCCMdefect: rmep not found clear");
                }

                break;

            case CTC_OAM_DEFECT_EVENT_RDI_RX:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_RDI_RX))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_RDI_RX);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "SomeRDIdefect: rdi defect");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_RDI_RX);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "SomeRDIdefect: rdi defect clear");
                }

                break;

            case CTC_OAM_DEFECT_MAC_STATUS_CHANGE:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_MAC_STATUS_CHANGE))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_MAC_STATUS_CHANGE);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "SomeMACstatusDefect");
                }

                break;

            case CTC_OAM_DEFECT_SRC_MAC_MISMATCH:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_SRC_MAC_MISMATCH))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_SRC_MAC_MISMATCH);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "RMEP SRC MAC Mismatch");
                }

                break;

            case CTC_OAM_DEFECT_EVENT_RDI_TX:
                if (CTC_FLAG_ISSET(mep_defect_bitmap_temp, CTC_OAM_DEFECT_EVENT_RDI_TX))
                {
                    CTC_SET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_RDI_TX);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "TX RDI set");
                }
                else
                {
                    CTC_UNSET_FLAG(g_gb_oam_master[lchip]->mep_defect_bitmap[mep_index], CTC_OAM_DEFECT_EVENT_RDI_TX);
                    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "TX RDI clear");
                }
                break;

            default:
                SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"  defect name: %s\n", "Defect no name");
                break;
            }
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_isr_oam_process_isr(uint8 gchip, void* p_data)
{
    uint16 mep_num       = 0;
    uint8 lchip = 0;
    ctc_oam_event_t* p_event = NULL;

    CTC_PTR_VALID_CHECK(p_data);

    p_event = (ctc_oam_event_t*)p_data;
    sys_greatbelt_get_local_chip_id(gchip, &lchip);
    for (mep_num = 0; mep_num < p_event->valid_entry_num; mep_num++)
    {
        sys_greatbelt_oam_isr_get_defect_name(lchip, &p_event->oam_event_entry[mep_num]);

        if (p_event->oam_event_entry[mep_num].is_remote)
        {
            p_event->oam_event_entry[mep_num].is_remote = 0;
            sys_greatbelt_oam_isr_get_defect_name(lchip, &p_event->oam_event_entry[mep_num]);
        }
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_oam_init(uint8 lchip, ctc_oam_global_t* p_cfg)
{
    int32 ret      = CTC_E_NONE;

    CTC_PTR_VALID_CHECK(p_cfg);

    if(0xFF == SYS_OAM_CHANNEL_ID)
    {
        return CTC_E_NONE;
    }

    ret = _sys_greatbelt_oam_db_init(lchip, p_cfg);
    if (CTC_E_NONE != ret)
    {
        /*rollback*/
        return ret;
    }

    ret = sys_greatbelt_cfm_init(lchip, p_cfg);
    if (CTC_E_NONE != ret)
    {
        /*rollback*/
    }

    if (_sys_greatbelt_oam_get_mpls_entry_num(lchip))
    {
        ret = sys_greatbelt_oam_tp_y1731_init(lchip, p_cfg);
        if (CTC_E_NONE != ret)
        {

        }

        ret = sys_greatbelt_oam_tp_bfd_init(lchip, p_cfg);
        if (CTC_E_NONE != ret)
        {

        }

        ret = sys_greatbelt_oam_mpls_bfd_init(lchip, p_cfg);
        if (CTC_E_NONE != ret)
        {

        }
    }

    ret = sys_greatbelt_oam_ip_bfd_init(lchip, p_cfg);
    if (CTC_E_NONE != ret)
    {

    }

    if (g_gb_oam_master[lchip] && g_gb_oam_master[lchip]->no_mep_resource)
    {
        return CTC_E_NONE;
    }

    ret = sys_greatbelt_oam_trpt_init(lchip);

    if (g_gb_oam_master[lchip] && (NULL == g_gb_oam_master[lchip]->error_cache_cb))
    {
        ret = sys_greatbelt_oam_set_defect_cb(lchip, sys_greatbelt_isr_oam_process_isr);
    }
    return ret;
}

int32
sys_greatbelt_oam_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if(0xFF == SYS_OAM_CHANNEL_ID)
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_oam_trpt_deinit(lchip);
    _sys_greatbelt_oam_db_deinit(lchip);

    return CTC_E_NONE;
}

/**
 @brief  Register oam error cache callback
*/
int32
sys_greatbelt_oam_set_defect_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    SYS_OAM_INIT_CHECK(lchip);
    g_gb_oam_master[lchip]->error_cache_cb = cb;
    return CTC_E_NONE;
}

#define __THROUGH_PUT__
/**
@ brief create throughput section
*/
int32
sys_greatbelt_oam_set_trpt_cfg(uint8 lchip, ctc_oam_trpt_t* p_trpt)
{
    uint8  lport = 0;
    uint8 index = 0;
    ms_packet_header_t autogen_bridge_header;
    uint8* p_pkt_header = NULL;
    int32 ret = 0;

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    /* throughput config value check */
    CTC_ERROR_RETURN(sys_greatbelt_oam_trpt_glb_cfg_check(lchip, p_trpt));

    SYS_MAP_GPORT_TO_LPORT(p_trpt->gport, lchip, lport);

    SYS_OAM_DBG_INFO("session_id:%d\ttx_mode:%d\tgport:%d\nvlan_id:%d\tpattern_type:%d\trepeat_pattern:%d\npacket_num:%d\tpkt_size:%d\trate:%d\n",
       p_trpt->session_id, p_trpt->tx_mode, p_trpt->gport, p_trpt->vlan_id, p_trpt->pattern_type,
       p_trpt->repeat_pattern, p_trpt->packet_num, p_trpt->size, p_trpt->rate);

    SYS_OAM_DBG_INFO("user-defined packet length:%d \n", p_trpt->header_len);

    for (index = 0; index < p_trpt->header_len; index++)
    {
        SYS_OAM_DBG_INFO("0x%02x  ", *((uint8*)p_trpt->pkt_header+index));
        if (((index+1) % 0x10) == 0)
        {
            SYS_OAM_DBG_INFO("\n");
        }
    }
    SYS_OAM_DBG_INFO("\n");


    sal_memset(&autogen_bridge_header, 0, sizeof(ms_packet_header_t));

    p_pkt_header = (uint8*)mem_malloc(MEM_OAM_MODULE, (p_trpt->header_len+32));
    if (NULL == p_pkt_header)
    {
        return CTC_E_NO_MEMORY;
    }

    /*1. cfg bridge header */
    ret = sys_greatbelt_oam_trpt_set_bridgr_hdr(lchip, p_trpt, &autogen_bridge_header);
    if (ret < 0)
    {
        mem_free(p_pkt_header);
        return ret;
    }

    sal_memcpy((uint8*)p_pkt_header, (uint8*)&autogen_bridge_header, 32);
    sal_memcpy((uint8*)(p_pkt_header+32), (uint8*)p_trpt->pkt_header, p_trpt->header_len);

    /*2. cfg packet header */
    ret = sys_greatbelt_oam_trpt_set_pkthd(lchip, p_trpt->session_id,
                                (void*)p_pkt_header, p_trpt->header_len);
    if (ret < 0)
    {
        mem_free(p_pkt_header);
        return ret;
    }

    /* 3. cfg auto_gen rate */
    ret = sys_greatbelt_oam_trpt_set_rate(lchip, p_trpt->session_id, p_trpt);
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
sys_greatbelt_oam_set_trpt_en(uint8 lchip, uint8 gchip, uint8 session_id, uint8 enable)
{
    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);

    CTC_ERROR_RETURN(sys_greatbelt_oam_trpt_set_enable(lchip, session_id, enable));

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_get_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id, ctc_oam_trpt_stats_t* p_trpt_stats)
{

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (NULL == p_trpt_stats)
    {
        return CTC_E_INVALID_PTR;
    }

    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);

    CTC_ERROR_RETURN(sys_greatbelt_oam_trpt_get_stats(lchip, session_id, p_trpt_stats));

    return CTC_E_NONE;
}

int32
sys_greatbelt_oam_clear_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id)
{

    SYS_OAM_INIT_CHECK(lchip);
    SYS_OAM_DBG_FUNC();

    if (session_id >= CTC_OAM_TRPT_SESSION_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip);

    CTC_ERROR_RETURN(sys_greatbelt_oam_trpt_clear_stats(lchip, session_id));

    return CTC_E_NONE;
}

bool
sys_greatbelt_oam_bfd_init(uint8 lchip)
{
    if (NULL == g_gb_oam_master[lchip])
    {
        return FALSE;
    }

    if (g_gb_oam_master[lchip]->com_oam_global.mep_1ms_num >= 2)
    {
        return TRUE;
    }

    return FALSE;
}


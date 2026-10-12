/**
 @file sys_greatbelt_oam_com.c

 @date 2013-01-18

 @version v2.1

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

#include "greatbelt/include/drv_enum.h"
#include "greatbelt/include/drv_io.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam_debug.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

extern sys_oam_master_t* g_gb_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

extern int32
sys_greatbelt_mpls_calc_real_label_for_label_range(uint8 lchip, uint8 space_id, uint32 in_label,uint32 *out_label);


/****************************************************************************
*
* Function
*
***************************************************************************/

int32
_sys_greatbelt_oam_com_add_maid(uint8 lchip, void* p_maid)
{
    ctc_oam_maid_t* p_maid_param = (ctc_oam_maid_t*)p_maid;
    sys_oam_maid_com_t* p_sys_maid = NULL;
    int32 ret                = CTC_E_NONE;
    uint8 mep_type           = CTC_OAM_MEP_TYPE_MAX;

    SYS_OAM_DBG_FUNC();

    /*1. check maid param valid*/
    CTC_PTR_VALID_CHECK(p_maid_param);
    mep_type = p_maid_param->mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_CHECK_FUNC_TABLE(lchip, mep_type, SYS_OAM_CHECK_MAID));
    CTC_ERROR_RETURN((SYS_OAM_CHECK_FUNC_TABLE(lchip, mep_type, SYS_OAM_CHECK_MAID))(lchip, (void*)p_maid_param, TRUE));

    OAM_LOCK(lchip);

    /*2. lookup maid and check exist*/
    p_sys_maid = _sys_greatbelt_oam_maid_lkup(lchip, p_maid_param);
    if (NULL != p_sys_maid)
    {
        ret = CTC_E_OAM_MAID_ENTRY_EXIST;
        goto RETURN;
    }

    /*3. build maid sys node*/
    p_sys_maid = _sys_greatbelt_oam_maid_build_node(lchip, p_maid_param);
    if (NULL == p_sys_maid)
    {
        SYS_OAM_DBG_ERROR("Maid: build_node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    /*4. build maid index*/
    ret = _sys_greatbelt_oam_maid_build_index(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: build index fail!!\n");
        goto STEP_3;
    }

    /*5 write chan to db*/
    ret = _sys_greatbelt_oam_maid_add_to_db(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: add db fail!!\n");
        goto STEP_4;
    }

    /*6. write key/chan to asic*/
    ret = _sys_greatbelt_oam_maid_add_to_asic(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: add asic fail!!\n");
        goto STEP_5;
    }

    goto RETURN;

STEP_5:
    _sys_greatbelt_oam_maid_del_from_db(lchip, p_sys_maid);
STEP_4:
    _sys_greatbelt_oam_maid_free_index(lchip, p_sys_maid);
STEP_3:
    _sys_greatbelt_oam_maid_free_node(lchip, p_sys_maid);

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}


int32
_sys_greatbelt_oam_com_remove_maid(uint8 lchip, void* p_maid)
{
    ctc_oam_maid_t* p_maid_param = (ctc_oam_maid_t*)p_maid;
    sys_oam_maid_com_t* p_sys_maid = NULL;
    int32 ret                = CTC_E_NONE;
    uint8 mep_type           = CTC_OAM_MEP_TYPE_MAX;

    SYS_OAM_DBG_FUNC();

    /*1. check maid param valid*/
    CTC_PTR_VALID_CHECK(p_maid_param);
    mep_type = p_maid_param->mep_type;
    SYS_OAM_FUNC_TABLE_CHECK(SYS_OAM_CHECK_FUNC_TABLE(lchip, mep_type, SYS_OAM_CHECK_MAID));
    CTC_ERROR_RETURN((SYS_OAM_CHECK_FUNC_TABLE(lchip, mep_type, SYS_OAM_CHECK_MAID))(lchip, (void*)p_maid_param, FALSE));

    OAM_LOCK(lchip);

    /*2. lookup maid and check exist*/
    p_sys_maid = _sys_greatbelt_oam_maid_lkup(lchip, p_maid_param);
    if (NULL == p_sys_maid)
    {
        ret = CTC_E_OAM_MAID_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if (p_sys_maid->ref_cnt)
    {
        ret = CTC_E_OAM_MAID_ENTRY_REF_BY_MEP;
        goto RETURN;
    }

    /*3. delete maid from asic*/
    ret = _sys_greatbelt_oam_maid_del_from_asic(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: del asic fail!!\n");
        goto RETURN;
    }

    /*4. delete maid from db*/
    ret = _sys_greatbelt_oam_maid_del_from_db(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: del db fail!!\n");
        goto RETURN;
    }

    /*5. free maid index*/
    ret = _sys_greatbelt_oam_maid_free_index(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: free index fail!!\n");
        goto RETURN;
    }

    /*6. free maid sys node*/
    ret = _sys_greatbelt_oam_maid_free_node(lchip, p_sys_maid);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Maid: free node fail!!\n");
        goto RETURN;
    }

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}

#define TP_OAM_CHAN  "FUCNTION"

sys_oam_chan_tp_t*
_sys_greatbelt_tp_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm)
{
    sys_oam_chan_tp_t sys_oam_chan;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_chan, 0, sizeof(sys_oam_chan_tp_t));
    sys_oam_chan.key.com.mep_type   = p_key_parm->mep_type;
    sys_oam_chan.key.label          = p_key_parm->u.tp.label;
    sys_oam_chan.key.spaceid        = p_key_parm->u.tp.mpls_spaceid;
    sys_oam_chan.key.gport_l3if_id  = p_key_parm->u.tp.gport_or_l3if_id;
    sys_oam_chan.key.section_oam    = CTC_FLAG_ISSET(p_key_parm->flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
    sys_oam_chan.com.mep_type       = p_key_parm->mep_type;

    return (sys_oam_chan_tp_t*)_sys_greatbelt_oam_chan_lkup(lchip, &sys_oam_chan.com);
}

int32
_sys_greatbelt_tp_chan_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_chan_tp_t* p_chan_para = (sys_oam_chan_tp_t*)p_oam_cmp->p_node_parm;
    sys_oam_chan_tp_t* p_chan_db   = (sys_oam_chan_tp_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_chan_para->key.com.mep_type != p_chan_db->key.com.mep_type)
    {
        return 0;
    }

    if (p_chan_para->key.section_oam != p_chan_db->key.section_oam)
    {
        return 0;
    }

    if (p_chan_para->key.label != p_chan_db->key.label)
    {
        return 0;
    }

    if (p_chan_para->key.spaceid != p_chan_db->key.spaceid)
    {
        return 0;
    }

    if (p_chan_para->key.gport_l3if_id != p_chan_db->key.gport_l3if_id)
    {
        return 0;
    }

    return 1;
}

sys_oam_chan_tp_t*
_sys_greatbelt_tp_chan_build_node(uint8 lchip, ctc_oam_lmep_t* p_chan_param)
{
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;
    uint8 mep_type = CTC_OAM_MEP_TYPE_MAX;
    sys_oam_nhop_info_t nhop_info;
    uint32 b_protection = 0;

    sal_memset(&nhop_info, 0, sizeof(sys_oam_nhop_info_t));

    SYS_OAM_DBG_FUNC();

    mep_type = p_chan_param->key.mep_type;

    p_sys_chan_tp = (sys_oam_chan_tp_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_chan_tp_t));
    if (NULL != p_sys_chan_tp)
    {
        sal_memset(p_sys_chan_tp, 0, sizeof(sys_oam_chan_tp_t));
        p_sys_chan_tp->com.mep_type = p_chan_param->key.mep_type;

        if (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type)
        {
            p_sys_chan_tp->com.master_chipid = p_chan_param->u.y1731_lmep.master_gchip;
        }
        else if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
        {
            p_sys_chan_tp->com.master_chipid = p_chan_param->u.bfd_lmep.master_gchip;
        }
        p_sys_chan_tp->com.lchip = lchip;
        p_sys_chan_tp->com.link_oam     = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        p_sys_chan_tp->key.com.mep_type     = p_chan_param->key.mep_type;
        p_sys_chan_tp->key.label            = p_chan_param->key.u.tp.label;
        p_sys_chan_tp->key.spaceid          = p_chan_param->key.u.tp.mpls_spaceid;
        p_sys_chan_tp->key.gport_l3if_id    = p_chan_param->key.u.tp.gport_or_l3if_id;
        p_sys_chan_tp->key.section_oam      = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);

        if (CTC_OAM_MEP_TYPE_MPLS_TP_Y1731 == mep_type)
        {
            b_protection = CTC_FLAG_ISSET(p_chan_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_PROTECTION_PATH);
            _sys_greatbelt_oam_get_nexthop_info(lchip, p_chan_param->u.y1731_lmep.nhid, b_protection, &nhop_info);
            p_sys_chan_tp->out_label            = nhop_info.mpls_out_label;

            if (CTC_FLAG_ISSET(p_chan_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
            {
                p_sys_chan_tp->lm_type     = p_chan_param->u.y1731_lmep.lm_type;
                p_sys_chan_tp->lm_cos_type = p_chan_param->u.y1731_lmep.lm_cos_type;
                p_sys_chan_tp->lm_cos      = p_chan_param->u.y1731_lmep.lm_cos;
            }
            else
            {
                p_sys_chan_tp->lm_type     = CTC_OAM_LM_TYPE_NONE;
            }
        }
        else if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
        {
            b_protection = CTC_FLAG_ISSET(p_chan_param->u.bfd_lmep.flag, CTC_OAM_BFD_LMEP_FLAG_PROTECTION_PATH);
            _sys_greatbelt_oam_get_nexthop_info(lchip, p_chan_param->u.bfd_lmep.nhid, b_protection, &nhop_info);
            p_sys_chan_tp->out_label            = nhop_info.mpls_out_label;

            if (CTC_FLAG_ISSET(p_chan_param->u.bfd_lmep.flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN))
            {
                p_sys_chan_tp->lm_type     = CTC_OAM_LM_TYPE_SINGLE;
                p_sys_chan_tp->lm_cos_type = p_chan_param->u.bfd_lmep.lm_cos_type;
                p_sys_chan_tp->lm_cos      = p_chan_param->u.bfd_lmep.lm_cos;
            }
            else
            {
                p_sys_chan_tp->lm_type     = CTC_OAM_LM_TYPE_NONE;
            }
        }


    }

    return p_sys_chan_tp;
}

int32
_sys_greatbelt_tp_chan_free_node(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_chan_tp);
    p_sys_chan_tp = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_chan_build_index(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    ds_mpls_oam_label_hash_key_t oam_key;

    uint16 gport        = 0;

    uint32 mpls_key     = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_tp);
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));


    if (p_sys_chan_tp->key.section_oam && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if))
    {
        gport = p_sys_chan_tp->key.gport_l3if_id;
    }

    /*
    if(p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE !=  p_sys_chan_tp->lm_type))
    {
    */
    sal_memset(&oam_key, 0, sizeof(ds_mpls_oam_label_hash_key_t));

    if (p_sys_chan_tp->key.section_oam)
    {
        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
        {
            mpls_key = p_sys_chan_tp->key.gport_l3if_id;
        }
        else
        {
            gport = p_sys_chan_tp->key.gport_l3if_id;
            mpls_key = CTC_MAP_GPORT_TO_LPORT(gport);
        }
    }
    else
    {
        /*get out label according nexthopid*/
        mpls_key = p_sys_chan_tp->out_label;
    }

    oam_key.mpls_label = mpls_key;
    oam_key.hash_type = p_sys_chan_tp->key.section_oam ? USERID_KEY_TYPE_MPLS_SECTION_OAM :
        USERID_KEY_TYPE_MPLS_LABEL_OAM;
    oam_key.valid = 1;
    oam_key.mpls_label_space = 0;

    lookup_info.hash_type = p_sys_chan_tp->key.section_oam ? USERID_HASH_TYPE_MPLS_SECTION_OAM :
        USERID_HASH_TYPE_MPLS_LABEL_OAM;
    lookup_info.hash_module = HASH_MODULE_USERID;
    lookup_info.p_ds_key = &oam_key;

    lookup_info.chip_id = lchip;
    CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &lookup_result));
    if (lookup_result.free)
    {
        p_sys_chan_tp->key.com.key_index = lookup_result.key_index;
        p_sys_chan_tp->key.com.key_alloc = SYS_OAM_KEY_HASH;
        SYS_OAM_DBG_INFO("CHAN(Key): lchip->%d, index->0x%x\n", lchip, lookup_result.key_index);
    }
    else if (lookup_result.conflict)
    {
        uint32 offset = 0;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 0, 1, &offset));
        p_sys_chan_tp->key.com.key_alloc = SYS_OAM_KEY_LTCAM;
        p_sys_chan_tp->key.com.key_index = offset;
        SYS_OAM_DBG_INFO("CHAN(Key LTCAM): Hash lookup confict, using tcam index = %d\n", offset);
    }

    /*Need check 2 chip key alloc not same*/
    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_build_index(lchip, &p_sys_chan_tp->com));
    }
    else
    {
        p_sys_chan_tp->com.chan_index = p_sys_chan_tp->key.com.key_index;
        SYS_OAM_DBG_INFO("CHAN(chan LTCAM): lchip->%d, index->%d\n", lchip, p_sys_chan_tp->com.chan_index);
    }

    if ((CTC_OAM_LM_TYPE_NONE != p_sys_chan_tp->lm_type) && (!p_sys_chan_tp->lm_index_alloced))
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_tp->com, FALSE));
        p_sys_chan_tp->lm_index_alloced = TRUE;
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_tp_chan_free_index(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_tp);

    /*1.free chan index*/
    /*
    if(p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE !=  p_sys_chan_tp->lm_type))
    {
    */
    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_free_index(lchip, &p_sys_chan_tp->com));
    }
    else
    {
        uint32 offset = p_sys_chan_tp->key.com.key_index;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, offset));
    }

    /*2.free lm index*/
    if (TRUE == p_sys_chan_tp->lm_index_alloced)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_tp->com, FALSE));
        p_sys_chan_tp->lm_index_alloced = FALSE;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_tp_chan_add_to_asic(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 key_index  = 0;
    uint32 chan_index = 0;

    uint16 gport      = 0;
    uint32 mpls_key   = 0;

    ds_mpls_oam_label_hash_key_t ds_mpls_oam_key;
    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;

    void* p_chan = NULL;
    ds_fib_user_id80_key_t  data;
    ds_fib_user_id80_key_t  mask;
    tbl_entry_t         tcam_key;
    uint8 offset = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_tp);


    sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
    sal_memset(&lpm_tcam_ad, 0, sizeof(ds_lpm_tcam_ad_t));

    if (p_sys_chan_tp->key.section_oam)
    {
        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
        {
            mpls_key = p_sys_chan_tp->key.gport_l3if_id;
        }
        else
        {
            gport = p_sys_chan_tp->key.gport_l3if_id;
            mpls_key = CTC_MAP_GPORT_TO_LPORT(gport);

            if (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id)))
            {
                return CTC_E_NONE;
            }
        }
    }
    else
    {
        mpls_key = p_sys_chan_tp->out_label;
    }

    /*
    if(p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE !=  p_sys_chan_tp->lm_type))
    {
    */
    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        sal_memset(&ds_mpls_oam_key, 0, sizeof(ds_mpls_oam_label_hash_key_t));
        ds_mpls_oam_key.valid               = 1;
        ds_mpls_oam_key.mpls_label_space    = 0;
        ds_mpls_oam_key.hash_type           = p_sys_chan_tp->key.section_oam ? USERID_KEY_TYPE_MPLS_SECTION_OAM :
            USERID_KEY_TYPE_MPLS_LABEL_OAM;
        ds_mpls_oam_key.mpls_label          = mpls_key;

        SYS_OAM_DBG_INFO("KEY: ds_mpls_oam_key.mpls_label = 0x%x\n",        ds_mpls_oam_key.mpls_label);
        SYS_OAM_DBG_INFO("KEY: ds_mpls_oam_key.mpls_label_space  = 0x%x\n", ds_mpls_oam_key.mpls_label_space);
        SYS_OAM_DBG_INFO("KEY: ds_mpls_oam_key.hash_type = 0x%x\n",         ds_mpls_oam_key.hash_type);
        SYS_OAM_DBG_INFO("KEY: ds_mpls_oam_key.valid = 0x%x\n",             ds_mpls_oam_key.valid);

        ds_mpls_oam_chan.oam_dest_chip_id = p_sys_chan_tp->com.master_chipid;
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
        p_chan = &ds_mpls_oam_chan;

    }
    else
    {
        sal_memset(&data, 0, sizeof(ds_fib_user_id80_key_t));
        sal_memset(&mask, 0, sizeof(ds_fib_user_id80_key_t));
        sal_memset(&tcam_key, 0, sizeof(tbl_entry_t));

        tcam_key.data_entry = (uint32*)&data;
        tcam_key.mask_entry = (uint32*)&mask;

        data.table_id           = 0;
        data.is_user_id         = 1;
        data.is_label           = 0;
        data.direction          = 0;
        data.ip_sa              = ((0 << 20) | (mpls_key & 0xFFFFF));
        data.user_id_hash_type  = p_sys_chan_tp->key.section_oam ? USERID_KEY_TYPE_MPLS_SECTION_OAM :
            USERID_KEY_TYPE_MPLS_LABEL_OAM;
        mask.table_id           = 0x3;
        mask.is_user_id         = 0x1;
        mask.is_label           = 0x1;
        mask.direction          = 0x1;
        mask.global_src_port    = 0x3FFF;
        mask.ip_sa              = 0xFFFFFFFF;
        mask.user_id_hash_type  = 0x1F;

        ds_mpls_oam_chan.oam_dest_chip_id = p_sys_chan_tp->com.master_chipid;
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        p_chan = &lpm_tcam_ad;

        offset = sizeof(ds_lpm_tcam_ad_t) - sizeof(ds_mpls_pbt_bfd_oam_chan_t);
        sal_memcpy((uint8*)p_chan + offset, &ds_mpls_oam_chan, sizeof(ds_mpls_pbt_bfd_oam_chan_t));

    }

    chan_index = p_sys_chan_tp->com.chan_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, p_chan));

    key_index = p_sys_chan_tp->key.com.key_index;

    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        ds_mpls_oam_key.ad_index = chan_index;
        key_cmd = DRV_IOW(DsMplsOamLabelHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &ds_mpls_oam_key));
        SYS_OAM_DBG_INFO("Write DsMplsOamHashKey_t: index = 0x%x\n",  key_index);
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_2 = 0x%x\n",  (*((uint32*)&ds_mpls_oam_key)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_1 = 0x%x\n",  (*((uint32*)&ds_mpls_oam_key + 1)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_0 = 0x%x\n",  (*((uint32*)&ds_mpls_oam_key + 2)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: bucketPtr = 0x%x\n",  key_index >> 2);
    }
    else
    {
        key_cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &tcam_key));
    }


    if (!p_sys_chan_tp->key.section_oam)
    {
        ds_mpls_t ds_mpls;
        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));

        chan_cmd    = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*chan_index  = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &chan_index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_mpls));

        chan_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

        ds_mpls.oam_dest_chipid1_0 = (p_sys_chan_tp->com.master_chipid & 0x3);
        ds_mpls.oam_dest_chipid3_2 = ((p_sys_chan_tp->com.master_chipid >> 2) & 0x3);
        ds_mpls.oam_dest_chipid4   = (p_sys_chan_tp->com.master_chipid >> 4) & 0x1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_mpls));
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_tp_chan_del_from_asic(uint8 lchip, sys_oam_chan_tp_t* p_sys_chan_tp)
{
    ds_mpls_oam_label_hash_key_t ds_mpls_oam_key;
    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;

    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 chan_index = 0;
    uint32 index      = 0;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_tp);


    if ((p_sys_chan_tp->key.section_oam)
        &&(!P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if))
    {
        if (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id)))
        {
            return CTC_E_NONE;
        }
    }

    /*
    if(p_sys_chan_tp->key.section_oam || (CTC_OAM_LM_TYPE_NONE !=  p_sys_chan_tp->lm_type))
    {
    */

    if (p_sys_chan_tp->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        /*mpls section oam key delete*/
        sal_memset(&ds_mpls_oam_key, 0, sizeof(ds_mpls_oam_label_hash_key_t));
        ds_mpls_oam_key.valid = 0;
        key_cmd = DRV_IOW(DsMplsOamLabelHashKey_t, DRV_ENTRY_FLAG);

        /*mpls oam ad chan delete*/
        sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);

        index = p_sys_chan_tp->key.com.key_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, key_cmd, &ds_mpls_oam_key));

        index = p_sys_chan_tp->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, &ds_mpls_oam_chan));
    }
    else
    {
        index = p_sys_chan_tp->key.com.key_index;
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, DsFibUserId80Key_t, index));
    }

    if (!p_sys_chan_tp->key.section_oam)
    {
        ds_mpls_t ds_mpls;
        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));

        chan_cmd    = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*chan_index  = p_sys_chan_tp->key.label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_chan_tp->key.spaceid, p_sys_chan_tp->key.label, &chan_index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_mpls));

        chan_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);

        ds_mpls.oam_dest_chipid1_0 = 0;
        ds_mpls.oam_dest_chipid3_2 = 0;
        ds_mpls.oam_dest_chipid4   = 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_mpls));

    }

    return CTC_E_NONE;
}



int32
_sys_greatbelt_tp_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan)
{
    ctc_oam_lmep_t* p_chan_param   = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;
    int32 ret = CTC_E_NONE;

    /*1. lookup chan and check exist*/
    p_sys_chan_tp = _sys_greatbelt_tp_chan_lkup(lchip, &p_chan_param->key);
    if (NULL != p_sys_chan_tp)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_EXIST;
        (*p_sys_chan) = p_sys_chan_tp;
        goto RETURN;
    }

    /*2. build chan sys node*/
    p_sys_chan_tp = _sys_greatbelt_tp_chan_build_node(lchip, p_chan_param);
    if (NULL == p_sys_chan_tp)
    {
        SYS_OAM_DBG_ERROR("Chan: build node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    /*3. build chan index*/
    ret = _sys_greatbelt_tp_chan_build_index(lchip, p_sys_chan_tp);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: build index fail!!\n");
        goto STEP_3;
    }

    /*4. add chan to db*/
    ret = _sys_greatbelt_oam_chan_add_to_db(lchip, &p_sys_chan_tp->com);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add db fail!!\n");
        goto STEP_4;
    }

    /*5. add key/chan to asic*/
    ret = _sys_greatbelt_tp_chan_add_to_asic(lchip, p_sys_chan_tp);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add asic fail!!\n");
        goto STEP_5;
    }

    (*p_sys_chan) = p_sys_chan_tp;

    goto RETURN;

STEP_5:
    _sys_greatbelt_oam_chan_del_from_db(lchip, &p_sys_chan_tp->com);
STEP_4:
    _sys_greatbelt_tp_chan_free_index(lchip, p_sys_chan_tp);
STEP_3:
    _sys_greatbelt_tp_chan_free_node(lchip, p_sys_chan_tp);

RETURN:
    return ret;
}


int32
_sys_greatbelt_tp_remove_chan(uint8 lchip, void* p_chan)
{
    ctc_oam_lmep_t* p_chan_param = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_tp_t* p_sys_chan_tp = NULL;
    int32 ret = CTC_E_NONE;

    /*1. lookup chan and check exist*/
    p_sys_chan_tp = _sys_greatbelt_tp_chan_lkup(lchip, &p_chan_param->key);
    if (NULL == p_sys_chan_tp)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if (CTC_SLISTCOUNT(p_sys_chan_tp->com.lmep_list) > 0)
    { /*need consider mip*/
        ret = CTC_E_OAM_CHAN_LMEP_EXIST;
        goto RETURN;
    }

    /*2. remove key/chan from asic*/
    ret = _sys_greatbelt_tp_chan_del_from_asic(lchip, p_sys_chan_tp);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del asic fail!!\n");
        goto RETURN;
    }

    /*3. remove chan from db*/
    ret = _sys_greatbelt_oam_chan_del_from_db(lchip, &p_sys_chan_tp->com);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del db fail!!\n");
        goto RETURN;
    }

    /*4. free chan index*/
    ret = _sys_greatbelt_tp_chan_free_index(lchip, p_sys_chan_tp);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

    /*5. free chan node*/
    ret = _sys_greatbelt_tp_chan_free_node(lchip, p_sys_chan_tp);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

RETURN:
    return ret;
}



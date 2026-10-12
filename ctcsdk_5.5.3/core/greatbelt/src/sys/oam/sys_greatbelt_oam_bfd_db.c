/**
 @file ctc_greatbelt_bfd_db.c

 @date 2013-01-24

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
#include "sys_greatbelt_oam_com.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_oam_bfd_db.h"
#include "sys_greatbelt_queue_enq.h"

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

sys_oam_chan_bfd_t*
_sys_greatbelt_ip_bfd_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm)
{
    sys_oam_chan_bfd_t sys_oam_bfd_chan;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_bfd_chan, 0, sizeof(sys_oam_chan_bfd_t));
    sys_oam_bfd_chan.key.com.mep_type   = p_key_parm->mep_type;
    sys_oam_bfd_chan.key.my_discr       = p_key_parm->u.bfd.discr;
    sys_oam_bfd_chan.com.mep_type        = p_key_parm->mep_type;

    return (sys_oam_chan_bfd_t*)_sys_greatbelt_oam_chan_lkup(lchip, &sys_oam_bfd_chan.com);
}

int32
_sys_greatbelt_ip_bfd_chan_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_chan_bfd_t* p_chan_para = (sys_oam_chan_bfd_t*)p_oam_cmp->p_node_parm;
    sys_oam_chan_bfd_t* p_chan_db   = (sys_oam_chan_bfd_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_chan_para->key.com.mep_type != p_chan_db->key.com.mep_type)
    {
        return 0;
    }

    if (p_chan_para->key.my_discr != p_chan_db->key.my_discr)
    {
        return 0;
    }

    return 1;
}

sys_oam_chan_bfd_t*
_sys_greatbelt_ip_bfd_chan_build_node(uint8 lchip, ctc_oam_lmep_t* p_chan_param)
{
    sys_oam_chan_bfd_t* p_sys_chan_bfd = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_bfd = (sys_oam_chan_bfd_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_chan_bfd_t));
    if (NULL != p_sys_chan_bfd)
    {
        sal_memset(p_sys_chan_bfd, 0, sizeof(sys_oam_chan_bfd_t));
        p_sys_chan_bfd->com.mep_type        = p_chan_param->key.mep_type;
        p_sys_chan_bfd->com.master_chipid   = p_chan_param->u.bfd_lmep.master_gchip;
        p_sys_chan_bfd->com.lchip = lchip;

        p_sys_chan_bfd->key.com.mep_type    = p_chan_param->key.mep_type;
        p_sys_chan_bfd->key.my_discr        = p_chan_param->key.u.bfd.discr;
    }

    return p_sys_chan_bfd;
}

int32
_sys_greatbelt_ip_bfd_chan_free_node(uint8 lchip, sys_oam_chan_bfd_t* p_sys_chan_bfd)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_chan_bfd);
    p_sys_chan_bfd = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ip_bfd_chan_build_index(uint8 lchip, sys_oam_chan_bfd_t* p_sys_chan_bfd)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    ds_bfd_oam_hash_key_t oam_key;


    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_bfd);
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    sal_memset(&oam_key, 0, sizeof(ds_bfd_oam_hash_key_t));

    oam_key.hash_type           = USERID_KEY_TYPE_BFD_OAM;
    oam_key.valid               = 1;
    oam_key.my_discriminator    = p_sys_chan_bfd->key.my_discr;

    lookup_info.hash_type   = USERID_HASH_TYPE_BFD_OAM;
    lookup_info.hash_module = HASH_MODULE_USERID;
    lookup_info.p_ds_key    = &oam_key;

    lookup_info.chip_id = lchip;
    CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &lookup_result));
    if (lookup_result.free)
    {
        p_sys_chan_bfd->key.com.key_index = lookup_result.key_index;
        p_sys_chan_bfd->key.com.key_alloc = SYS_OAM_KEY_HASH;
        SYS_OAM_DBG_INFO("CHAN(Key): lchip->%d, index->0x%x\n", lchip, lookup_result.key_index);
    }
    else if (lookup_result.conflict)
    {
        uint32 offset = 0;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 0, 1, &offset));
        p_sys_chan_bfd->key.com.key_alloc = SYS_OAM_KEY_LTCAM;
        p_sys_chan_bfd->key.com.key_index = offset;
        SYS_OAM_DBG_INFO("CHAN(Key LTCAM): Hash lookup confict, using tcam index = %d\n", offset);
    }

    /*Need check 2 chip key alloc not same*/
    if (p_sys_chan_bfd->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_build_index(lchip, &p_sys_chan_bfd->com));
    }
    else
    {
        p_sys_chan_bfd->com.chan_index = p_sys_chan_bfd->key.com.key_index;
        SYS_OAM_DBG_INFO("CHAN(chan LTCAM): lchip->%d, index->%d\n", lchip, p_sys_chan_bfd->com.chan_index);
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_ip_bfd_chan_free_index(uint8 lchip, sys_oam_chan_bfd_t* p_sys_chan_bfd)
{

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_bfd);

    /*1.free chan index*/
    if (p_sys_chan_bfd->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_free_index(lchip, &p_sys_chan_bfd->com));
    }
    else
    {
        uint32 offset = p_sys_chan_bfd->key.com.key_index;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, offset));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ip_bfd_chan_add_to_asic(uint8 lchip, sys_oam_chan_bfd_t* p_sys_chan_bfd)
{
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 key_index  = 0;
    uint32 chan_index = 0;

    ds_bfd_oam_hash_key_t       ds_bfd_oam_key;
    ds_mpls_pbt_bfd_oam_chan_t  ds_mpls_oam_chan;
    ds_lpm_tcam_ad_t            lpm_tcam_ad;

    void* p_chan = NULL;
    ds_fib_user_id80_key_t  data;
    ds_fib_user_id80_key_t  mask;
    tbl_entry_t         tcam_key;
    uint8 offset = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_bfd);

    sal_memset(&ds_mpls_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
    sal_memset(&lpm_tcam_ad, 0, sizeof(ds_lpm_tcam_ad_t));

    if (p_sys_chan_bfd->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        sal_memset(&ds_bfd_oam_key, 0, sizeof(ds_bfd_oam_hash_key_t));
        ds_bfd_oam_key.valid                = 1;
        ds_bfd_oam_key.my_discriminator     = p_sys_chan_bfd->key.my_discr;
        ds_bfd_oam_key.hash_type            = USERID_KEY_TYPE_BFD_OAM;

        SYS_OAM_DBG_INFO("KEY: ds_bfd_oam_key.my_discriminator  = 0x%x\n", ds_bfd_oam_key.my_discriminator);
        SYS_OAM_DBG_INFO("KEY: ds_bfd_oam_key.hash_type = 0x%x\n",         ds_bfd_oam_key.hash_type);
        SYS_OAM_DBG_INFO("KEY: ds_bfd_oam_key.valid = 0x%x\n",             ds_bfd_oam_key.valid);

        ds_mpls_oam_chan.oam_dest_chip_id = p_sys_chan_bfd->com.master_chipid;
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
        data.ip_sa              = p_sys_chan_bfd->key.my_discr;
        data.user_id_hash_type  = USERID_KEY_TYPE_BFD_OAM;
        mask.table_id           = 0x3;
        mask.is_user_id         = 0x1;
        mask.is_label           = 0x1;
        mask.direction          = 0x1;
        mask.global_src_port    = 0x3FFF;
        mask.ip_sa              = 0xFFFFFFFF;
        mask.user_id_hash_type  = 0x1F;

        ds_mpls_oam_chan.oam_dest_chip_id = p_sys_chan_bfd->com.master_chipid;
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        p_chan = &lpm_tcam_ad;

        offset = sizeof(ds_lpm_tcam_ad_t) - sizeof(ds_mpls_pbt_bfd_oam_chan_t);
        sal_memcpy((uint8*)p_chan + offset, &ds_mpls_oam_chan, sizeof(ds_mpls_pbt_bfd_oam_chan_t));

    }

    chan_index = p_sys_chan_bfd->com.chan_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, p_chan));

    key_index = p_sys_chan_bfd->key.com.key_index;

    if (p_sys_chan_bfd->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        ds_bfd_oam_key.ad_index = chan_index;
        key_cmd = DRV_IOW(DsBfdOamHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &ds_bfd_oam_key));
        SYS_OAM_DBG_INFO("Write DsBfdOamHashKey_t: index = 0x%x\n",  key_index);
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_2 = 0x%x\n",  (*((uint32*)&ds_bfd_oam_key)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_1 = 0x%x\n",  (*((uint32*)&ds_bfd_oam_key + 1)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_0 = 0x%x\n",  (*((uint32*)&ds_bfd_oam_key + 2)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: bucketPtr = 0x%x\n",  key_index >> 2);
    }
    else
    {
        key_cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &tcam_key));
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_ip_bfd_chan_del_from_asic(uint8 lchip, sys_oam_chan_bfd_t* p_sys_chan_bfd)
{
    ds_bfd_oam_hash_key_t ds_bfd_oam_key;
    ds_mpls_pbt_bfd_oam_chan_t   ds_bfd_oam_chan;

    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 index      = 0;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_bfd);

    if (p_sys_chan_bfd->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        /*mpls section oam key delete*/
        sal_memset(&ds_bfd_oam_key, 0, sizeof(ds_bfd_oam_hash_key_t));
        ds_bfd_oam_key.valid = 0;
        key_cmd = DRV_IOW(DsBfdOamHashKey_t, DRV_ENTRY_FLAG);

        /*mpls oam ad chan delete*/
        sal_memset(&ds_bfd_oam_chan, 0, sizeof(ds_mpls_pbt_bfd_oam_chan_t));
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);

        index = p_sys_chan_bfd->key.com.key_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, key_cmd, &ds_bfd_oam_key));

        index = p_sys_chan_bfd->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, &ds_bfd_oam_chan));
    }
    else
    {
        index = p_sys_chan_bfd->key.com.key_index;
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, DsFibUserId80Key_t, index));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_ip_bfd_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan)
{
    ctc_oam_lmep_t* p_chan_param   = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_bfd_t* p_sys_chan_bfd = NULL;
    int32 ret = CTC_E_NONE;

    /*1. lookup chan and check exist*/
    p_sys_chan_bfd = _sys_greatbelt_ip_bfd_chan_lkup(lchip, &p_chan_param->key);
    if (NULL != p_sys_chan_bfd)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_EXIST;
        (*p_sys_chan) = p_sys_chan_bfd;
        goto RETURN;
    }

    /*2. build chan sys node*/
    p_sys_chan_bfd = _sys_greatbelt_ip_bfd_chan_build_node(lchip, p_chan_param);
    if (NULL == p_sys_chan_bfd)
    {
        SYS_OAM_DBG_ERROR("Chan: build node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    /*3. build chan index*/
    ret = _sys_greatbelt_ip_bfd_chan_build_index(lchip, p_sys_chan_bfd);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: build index fail!!\n");
        goto STEP_3;
    }

    /*4. add chan to db*/
    ret = _sys_greatbelt_oam_chan_add_to_db(lchip, &p_sys_chan_bfd->com);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add db fail!!\n");
        goto STEP_4;
    }

    /*5. add key/chan to asic*/
    ret = _sys_greatbelt_ip_bfd_chan_add_to_asic(lchip, p_sys_chan_bfd);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add asic fail!!\n");
        goto STEP_5;
    }

    (*p_sys_chan) = p_sys_chan_bfd;

    goto RETURN;

STEP_5:
    _sys_greatbelt_oam_chan_del_from_db(lchip, &p_sys_chan_bfd->com);
STEP_4:
    _sys_greatbelt_ip_bfd_chan_free_index(lchip, p_sys_chan_bfd);
STEP_3:
    _sys_greatbelt_ip_bfd_chan_free_node(lchip, p_sys_chan_bfd);

RETURN:
    return ret;
}


int32
_sys_greatbelt_ip_bfd_remove_chan(uint8 lchip, void* p_chan)
{
    ctc_oam_lmep_t* p_chan_param = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_bfd_t* p_sys_chan = NULL;
    int32 ret = CTC_E_NONE;

    /*1. lookup chan and check exist*/
    p_sys_chan = _sys_greatbelt_ip_bfd_chan_lkup(lchip, &p_chan_param->key);
    if (NULL == p_sys_chan)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if (CTC_SLISTCOUNT(p_sys_chan->com.lmep_list) > 0)
    { /*need consider mip*/
        ret = CTC_E_OAM_CHAN_LMEP_EXIST;
        goto RETURN;
    }

    /*2. remove key/chan from asic*/
    ret = _sys_greatbelt_ip_bfd_chan_del_from_asic(lchip, p_sys_chan);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del asic fail!!\n");
        goto RETURN;
    }

    /*3. remove chan from db*/
    ret = _sys_greatbelt_oam_chan_del_from_db(lchip, &p_sys_chan->com);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del db fail!!\n");
        goto RETURN;
    }

    /*4. free chan index*/
    ret = _sys_greatbelt_ip_bfd_chan_free_index(lchip, p_sys_chan);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

    /*5. free chan node*/
    ret = _sys_greatbelt_ip_bfd_chan_free_node(lchip, p_sys_chan);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

RETURN:
    return ret;
}

int32
_sys_greatbelt_bfd_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan)
{
    ctc_oam_lmep_t *p_chan_param = NULL;
    uint8 mep_type  = CTC_OAM_MEP_TYPE_MAX;
    int32 ret       = CTC_E_NONE;

    p_chan_param    = (ctc_oam_lmep_t *)p_chan;
    mep_type        = p_chan_param->key.mep_type;

    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        ret = _sys_greatbelt_tp_add_chan(lchip, p_chan_param, p_sys_chan);
    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type) || (CTC_OAM_MEP_TYPE_IP_BFD == mep_type))
    {

        ret = _sys_greatbelt_ip_bfd_add_chan(lchip, p_chan_param, p_sys_chan);
    }

    return ret;

}

int32
_sys_greatbelt_bfd_remove_chan(uint8 lchip, void* p_chan)
{
    ctc_oam_lmep_t *p_chan_param = NULL;
    uint8 mep_type  = CTC_OAM_MEP_TYPE_MAX;
    int32 ret       = CTC_E_NONE;

    p_chan_param    = (ctc_oam_lmep_t *)p_chan;
    mep_type        = p_chan_param->key.mep_type;

    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        ret = _sys_greatbelt_tp_remove_chan(lchip, p_chan_param);
    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type) || (CTC_OAM_MEP_TYPE_IP_BFD == mep_type))
    {
        ret = _sys_greatbelt_ip_bfd_remove_chan(lchip, p_chan_param);
    }

    return ret;
}


int32
_sys_greatbelt_bfd_update_chan(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep_bfd, bool is_add)
{
    sys_oam_chan_tp_t*  p_sys_chan_tp   = NULL;
    sys_oam_chan_bfd_t* p_sys_chan_bfd  = NULL;
    sys_oam_chan_com_t* p_sys_chan_com  = NULL;
    sys_oam_key_com_t*  p_key_com       = NULL;

    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_mpls_pbt_bfd_oam_chan_t*  p_ds_mpls_oam_tcam_chan = NULL;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;

    void* p_chan = NULL;

    uint8  mep_type     =  CTC_OAM_MEP_TYPE_MAX;

    uint32 index        = 0;
    uint8  offset       = 0;
    uint32 chan_cmd     = 0;
    uint32 mpls_cmd     = 0;

    uint8  lm_en        = 0;
    uint8  oam_check_type = SYS_OAM_TP_NONE_TYPE;
    uint8        master_chipid = 0;


    SYS_OAM_DBG_FUNC();

    mep_type    = p_sys_lmep_bfd->com.p_sys_chan->mep_type;
    master_chipid    = p_sys_lmep_bfd->com.p_sys_chan->master_chipid;

    p_sys_chan_com = p_sys_lmep_bfd->com.p_sys_chan;

    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        p_sys_chan_tp   = (sys_oam_chan_tp_t*)p_sys_chan_com;
        p_key_com       = &p_sys_chan_tp->key.com;
        /*mep index*/
        if (is_add)
        {
            p_sys_chan_tp->mep_index = p_sys_lmep_bfd->com.lmep_index;
            oam_check_type = SYS_OAM_TP_MEP_TYPE;
        }
        else
        {
            p_sys_chan_tp->mep_index = 0x1FFF;
            oam_check_type = SYS_OAM_TP_NONE_TYPE;
        }
        if ((p_sys_chan_tp->key.section_oam)
            && (!P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if)
        && (!CTC_IS_LINKAGG_PORT(p_sys_chan_tp->key.gport_l3if_id))
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id))))
        {
            return CTC_E_NONE;
        }
    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
    {
        p_sys_chan_bfd  = (sys_oam_chan_bfd_t*)p_sys_chan_com;
        p_key_com       = &p_sys_chan_bfd->key.com;
        /*mep index*/
        if (is_add)
        {
            p_sys_chan_bfd->mep_index = p_sys_lmep_bfd->com.lmep_index;
            oam_check_type = SYS_OAM_TP_MEP_TYPE;
        }
        else
        {
            p_sys_chan_bfd->mep_index = 0x1FFF;
            oam_check_type = SYS_OAM_TP_NONE_TYPE;
        }
    }

    /*lm enable*/
    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN) && is_add)
        {
            lm_en   = 1;
        }
        else
        {
            lm_en   = 0;
        }
    }

    if (SYS_OAM_KEY_HASH == p_key_com->key_alloc)
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


    index = p_sys_chan_com->chan_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

    p_ds_mpls_oam_tcam_chan->mep_index      = p_sys_lmep_bfd->com.lmep_index;
    p_ds_mpls_oam_tcam_chan->mpls_lm_type   = lm_en;
    p_ds_mpls_oam_tcam_chan->bfd_single_hop = CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_IP_SINGLE_HOP);
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

    if (p_key_com->key_alloc == SYS_OAM_KEY_HASH)
    {
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
    }
    else
    {
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));


    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
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
            ds_mpls.mpls_lm_type    = lm_en;
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
            sys_greatbelt_mpls_nh_update_oam_en(lchip, p_sys_lmep_bfd->nhid, p_sys_chan_tp->out_label, lm_en);

        }
        else
        { /*need update port or interface lm enable*/
            uint32 cmd      = 0;
            uint32 field    = 0;
            uint8  gchip    = 0;

            field = CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);
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
    }
    else if (CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
    {

    }
    else if ((CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type) && (0xFFFFFFFF != p_sys_lmep_bfd->mpls_in_label))
    {
        ds_mpls_t ds_mpls;

        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
        mpls_cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*index    = p_sys_lmep_bfd->mpls_in_label;*/

        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_sys_lmep_bfd->spaceid, p_sys_lmep_bfd->mpls_in_label, &index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));
        if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV))
        {
            ds_mpls.mep_index           = 0x1FFF;  /*for learning, update to real value when add rmep*/
        }
        else
        {
            ds_mpls.mep_index           = p_sys_chan_bfd->mep_index;
        }

        if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV_IPV4))
        {
            ds_mpls.oam_check_type      = SYS_OAM_TP_NONE_TYPE;
        }
        else
        {
            ds_mpls.oam_check_type      = oam_check_type;
        }

        ds_mpls.oam_dest_chipid1_0  = (p_sys_chan_bfd->com.master_chipid & 0x3);
        ds_mpls.oam_dest_chipid3_2  = ((p_sys_chan_bfd->com.master_chipid >> 2)& 0x3);
        ds_mpls.oam_dest_chipid4    = CTC_IS_BIT_SET(p_sys_chan_bfd->com.master_chipid, 4);

        mpls_cmd    = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_bfd_chan_update_reomte(uint8 lchip, ctc_oam_lmep_t* p_bfd_lmep, sys_oam_chan_com_t* p_sys_chan_com, bool is_add)
{
    sys_oam_chan_tp_t*  p_sys_chan_tp   = NULL;
    sys_oam_chan_bfd_t* p_sys_chan_bfd  = NULL;
    sys_oam_key_com_t*  p_key_com       = NULL;

    ds_mpls_pbt_bfd_oam_chan_t   ds_mpls_oam_chan;
    ds_mpls_pbt_bfd_oam_chan_t*  p_ds_mpls_oam_tcam_chan = NULL;
    ds_lpm_tcam_ad_t  lpm_tcam_ad;

    void* p_chan = NULL;

    uint8  mep_type     =  CTC_OAM_MEP_TYPE_MAX;

    uint32 index        = 0;
    uint8  offset       = 0;
    uint32 chan_cmd     = 0;
    uint32 mpls_cmd     = 0;

    uint8  oam_check_type = SYS_OAM_TP_NONE_TYPE;


    SYS_OAM_DBG_FUNC();

    mep_type    = p_sys_chan_com->mep_type;

    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        p_sys_chan_tp   = (sys_oam_chan_tp_t*)p_sys_chan_com;
        p_key_com       = &p_sys_chan_tp->key.com;
        /*mep index*/
        if (is_add)
        {
            p_sys_chan_tp->mep_index = p_bfd_lmep->lmep_index;
            oam_check_type = SYS_OAM_TP_MEP_TYPE;
        }
        else
        {
            p_sys_chan_tp->mep_index = 0x1FFF;
            oam_check_type = SYS_OAM_TP_NONE_TYPE;
        }
    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
    {
        p_sys_chan_bfd  = (sys_oam_chan_bfd_t*)p_sys_chan_com;
        p_key_com       = &p_sys_chan_bfd->key.com;
        /*mep index*/
        if (is_add)
        {
            p_sys_chan_bfd->mep_index = p_bfd_lmep->lmep_index;
            oam_check_type = SYS_OAM_TP_MEP_TYPE;
        }
        else
        {
            p_sys_chan_bfd->mep_index = 0x1FFF;
            oam_check_type = SYS_OAM_TP_NONE_TYPE;
        }
    }

    if (SYS_OAM_KEY_HASH == p_key_com->key_alloc)
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


    index = p_sys_chan_com->chan_index;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

    p_ds_mpls_oam_tcam_chan->mep_index      = p_bfd_lmep->lmep_index;
    p_ds_mpls_oam_tcam_chan->bfd_single_hop = CTC_FLAG_ISSET(p_bfd_lmep->u.bfd_lmep.flag, CTC_OAM_BFD_LMEP_FLAG_IP_SINGLE_HOP);

    if (p_key_com->key_alloc == SYS_OAM_KEY_HASH)
    {
        chan_cmd = DRV_IOW(DsMplsPbtBfdOamChan_t, DRV_ENTRY_FLAG);
    }
    else
    {
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));


    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
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

            mpls_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));
        }

    }
    else if (CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
    {

    }
    else if (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
    {
        ds_mpls_t ds_mpls;

        sal_memset(&ds_mpls, 0, sizeof(ds_mpls_t));
        mpls_cmd = DRV_IOR(DsMpls_t, DRV_ENTRY_FLAG);
         /*index    = p_bfd_lmep->u.bfd_lmep.mpls_in_label;*/
        sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, p_bfd_lmep->u.bfd_lmep.mpls_spaceid, p_bfd_lmep->u.bfd_lmep.mpls_in_label, &index);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));
        if (CTC_FLAG_ISSET(p_bfd_lmep->u.bfd_lmep.flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV))
        {
            ds_mpls.mep_index           = 0x1FFF;
        }
        else
        {
            ds_mpls.mep_index           = p_sys_chan_bfd->mep_index;
        }
        ds_mpls.oam_check_type      = oam_check_type;
        ds_mpls.oam_dest_chipid1_0  = (p_sys_chan_bfd->com.master_chipid & 0x3);
        ds_mpls.oam_dest_chipid3_2  = ((p_sys_chan_bfd->com.master_chipid >> 2)& 0x3);
        ds_mpls.oam_dest_chipid4    = CTC_IS_BIT_SET(p_sys_chan_bfd->com.master_chipid, 4);

        mpls_cmd    = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));

    }

    return CTC_E_NONE;
}


sys_oam_chan_com_t*
_sys_greatbelt_bfd_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm)
{
    sys_oam_chan_com_t*     p_sys_oam_chan      = NULL;

    uint8 mep_type = CTC_OAM_MEP_TYPE_MAX;

    SYS_OAM_DBG_FUNC();

    mep_type    = p_key_parm->mep_type;
    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        p_sys_oam_chan  = (sys_oam_chan_com_t*)_sys_greatbelt_tp_chan_lkup(lchip, p_key_parm);
    }
    else if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
    {
        p_sys_oam_chan  = (sys_oam_chan_com_t*)_sys_greatbelt_ip_bfd_chan_lkup(lchip, p_key_parm);
    }

    return p_sys_oam_chan;

}

#define LMEP "Function Begin"

sys_oam_lmep_bfd_t*
_sys_greatbelt_bfd_lmep_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan)
{
    SYS_OAM_DBG_FUNC();

    return (sys_oam_lmep_bfd_t*)_sys_greatbelt_oam_lmep_lkup(lchip, p_sys_chan, NULL);
}

int32
_sys_greatbelt_bfd_lmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_lmep_bfd_t* p_lmep_para = (sys_oam_lmep_bfd_t*)p_oam_cmp->p_node_parm;
    sys_oam_lmep_bfd_t* p_lmep_db   = (sys_oam_lmep_bfd_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if ((NULL == p_lmep_para) && (NULL != p_lmep_db))
    {
        return 1;
    }

    return 0;
}

sys_oam_lmep_bfd_t*
_sys_greatbelt_bfd_lmep_build_node(uint8 lchip, ctc_oam_lmep_t* p_lmep_param)
{
    sys_oam_lmep_bfd_t* p_sys_lmep      = NULL;
    ctc_oam_bfd_lmep_t* p_ctc_bfd_lmep  = NULL;

    SYS_OAM_DBG_FUNC();

    p_ctc_bfd_lmep = &p_lmep_param->u.bfd_lmep;

    p_sys_lmep = (sys_oam_lmep_bfd_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_lmep_bfd_t));
    if (NULL != p_sys_lmep)
    {
        sal_memset(p_sys_lmep, 0, sizeof(sys_oam_lmep_bfd_t));
        p_sys_lmep->flag           = p_ctc_bfd_lmep->flag;

        p_sys_lmep->com.mep_on_cpu = CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_ON_CPU);
        p_sys_lmep->com.lmep_index = p_lmep_param->lmep_index;

         /*p_sys_lmep->actual_tx_interval      = p_ctc_bfd_lmep->actual_tx_interval;*/
        p_sys_lmep->desired_min_tx_interval = p_ctc_bfd_lmep->desired_min_tx_interval;
        if((CTC_OAM_MEP_TYPE_IP_BFD == p_lmep_param->key.mep_type)
            || (CTC_OAM_MEP_TYPE_MPLS_BFD == p_lmep_param->key.mep_type))
        {
            p_sys_lmep->local_discr         = p_lmep_param->key.u.bfd.discr;
        }
        else
        {
            p_sys_lmep->local_discr         = p_ctc_bfd_lmep->local_discr;
        }

        p_sys_lmep->local_state         = p_ctc_bfd_lmep->local_state;
        p_sys_lmep->local_diag          = p_ctc_bfd_lmep->local_diag;
        p_sys_lmep->local_detect_mult   = p_ctc_bfd_lmep->local_detect_mult;
        p_sys_lmep->bfd_src_port        = p_ctc_bfd_lmep->bfd_src_port;

        p_sys_lmep->tx_csf_type         = p_ctc_bfd_lmep->tx_csf_type;
        p_sys_lmep->tx_cos_exp          = p_ctc_bfd_lmep->tx_cos_exp;

        p_sys_lmep->mpls_ttl            = p_ctc_bfd_lmep->ttl;
        p_sys_lmep->nhid                = p_ctc_bfd_lmep->nhid;
        p_sys_lmep->mpls_in_label       = p_ctc_bfd_lmep->mpls_in_label;
        p_sys_lmep->spaceid             = p_ctc_bfd_lmep->mpls_spaceid;

        sal_memcpy(&p_sys_lmep->ip4_sa, &p_ctc_bfd_lmep->ip4_sa, sizeof(p_sys_lmep->ip4_sa));
    }

    return p_sys_lmep;
}

int32
_sys_greatbelt_bfd_lmep_free_node(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_lmep);
    p_sys_lmep = NULL;
    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_build_index(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_build_index(lchip, &p_sys_lmep->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_free_index(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_free_index(lchip, &p_sys_lmep->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_add_to_db(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_add_to_db(lchip, &p_sys_lmep->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_del_from_db(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_db(lchip, &p_sys_lmep->com));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_bfd_lmep_add_tbl_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_chan_tp_t*  p_sys_chan_tp   = NULL;
    sys_oam_lmep_bfd_t* p_sys_bfd_lmep  = NULL;
    sys_oam_maid_com_t* p_sys_maid  = NULL;

    ds_ma_t ds_ma;
    ds_bfd_mep_t ds_bfd_mep;
    ds_bfd_mep_t ds_bfd_mep_mask;
    tbl_entry_t  tbl_entry;

    uint32 mult_group_id = 0;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 lmep_flag  = 0;
    sys_oam_nhop_info_t oam_nhop_info;

    uint8 mep_type = CTC_OAM_MEP_TYPE_MAX;
    uint8 csf_type = 0;
    uint32 value = 0;

    p_sys_bfd_lmep = (sys_oam_lmep_bfd_t*)p_sys_lmep;
    mep_type       = p_sys_lmep->p_sys_chan->mep_type;

    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        p_sys_chan_tp   = (sys_oam_chan_tp_t*)p_sys_lmep->p_sys_chan;
    }

    p_sys_maid  = p_sys_lmep->p_sys_maid;
    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    lmep_flag = p_sys_bfd_lmep->flag;

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    sal_memset(&ds_bfd_mep, 0, sizeof(ds_bfd_mep_t));
    sal_memset(&ds_bfd_mep_mask, 0, sizeof(ds_bfd_mep_t));

    /* set ma table entry */

    ds_ma.ma_id_length_type       = (p_sys_maid == NULL) ? 0 : (p_sys_maid->maid_entry_num >> 1);
    ds_ma.ma_name_index           = (p_sys_maid == NULL) ? 0 : p_sys_maid->maid_index;
    ds_ma.ma_name_len             = (p_sys_maid == NULL) ? 0 : p_sys_maid->maid_len;

    ds_ma.priority_index          = p_sys_bfd_lmep->tx_cos_exp;

    sal_memset(&oam_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
    _sys_greatbelt_oam_get_nexthop_info(lchip, p_sys_bfd_lmep->nhid, CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_PROTECTION_PATH), &oam_nhop_info);

    if (CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
    {
        ds_ma.tx_with_send_id       = 1;  /* UPD header with BFD PDU */
        ds_ma.rx_oam_type           = 2;

        if (oam_nhop_info.is_nat)
        {
            ds_ma.tx_with_if_status     = 1;  /* with IPv4 header, for ip bfd to swap ip da*/
        }

    }
    else if (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
    {
        if ((SYS_NH_TYPE_MPLS == oam_nhop_info.nh_entry_type)&&(0 == oam_nhop_info.mpls_out_label))
        {
            value = 0x0800;
            cmd = DRV_IOW(EpeL2EtherType_t, EpeL2EtherType_EtherType1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV))
        {
            ds_ma.rx_oam_type           = 8;
            ds_ma.tx_untagged_oam       = 1;
            ds_ma.mpls_label_valid      = 0xF;
        }
        else
        {
            ds_ma.tx_with_if_status     = 1;  /* with IPv4 header, only for ip bfd over mpls*/
            ds_ma.tx_with_send_id       = 1;  /* UPD header with BFD PDU */
            ds_ma.rx_oam_type           = 7;
            if(0 == p_sys_bfd_lmep->mpls_ttl)
            {
                p_sys_bfd_lmep->mpls_ttl = 1;
            }

        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        /* MPLS TP OAM with GAL or not */
        ds_ma.tx_untagged_oam       = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_WITHOUT_GAL);
        ds_ma.mpls_label_valid      = p_sys_chan_tp->key.section_oam ? 0 : 0xF;
        ds_ma.linkoam               = p_sys_chan_tp->key.section_oam ? 1 : 0;
        ds_ma.rx_oam_type           = 8;

        ds_ma.ccm_interval          = 1;
    }

    ds_ma.aps_en                    = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_APS_EN);
    ds_ma.sf_fail_while_cfg_type    = 0;
    ds_ma.aps_signal_fail_local     = 0;
    ds_ma.mpls_ttl                  = p_sys_bfd_lmep->mpls_ttl;

    if (CTC_NH_RESERVED_NHID_FOR_DROP == p_sys_bfd_lmep->nhid)
    {
        ds_ma.next_hop_ext          = 0;
        ds_ma.next_hop_ptr          = 0;
        ds_bfd_mep.dest_chip        = 0;
        ds_bfd_mep.dest_id          = SYS_RESERVE_PORT_ID_DROP;
    }
    else
    {
        sys_nh_info_oam_aps_t  sys_nh_info_oam_aps;
        ctc_chip_device_info_t device_info;

        sal_memset(&sys_nh_info_oam_aps, 0, sizeof(sys_nh_info_oam_aps_t));

        p_sys_bfd_lmep->aps_bridge_en   = oam_nhop_info.aps_bridge_en;

        sys_greatbelt_chip_get_device_info(lchip, &device_info);
        if (oam_nhop_info.aps_bridge_en && (1 == device_info.version_id ))
        {
            sys_nh_info_oam_aps.aps_group_id    = oam_nhop_info.dest_id;
            sys_nh_info_oam_aps.nexthop_is_8w   = oam_nhop_info.nexthop_is_8w;
            sys_nh_info_oam_aps.dsnh_offset     = oam_nhop_info.dsnh_offset;
            sys_greatbelt_nh_create_mcast_aps(lchip, &sys_nh_info_oam_aps, &mult_group_id);
            ds_bfd_mep.dest_id                  = mult_group_id;
        }
        else
        {
            ds_ma.next_hop_ext          = oam_nhop_info.nexthop_is_8w;
            ds_ma.next_hop_ptr          = oam_nhop_info.dsnh_offset;
            ds_bfd_mep.dest_chip        = oam_nhop_info.dest_chipid;
            ds_bfd_mep.dest_id          = oam_nhop_info.dest_id;
        }
    }

    ds_ma.md_lvl                    = 0;
    ds_ma.use_vrfid_lkup            = 0;
    ds_ma.tx_with_port_status       = 0;

    /* set mep table entry */

    ds_bfd_mep.active                   = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_MEP_EN);
    ds_bfd_mep.ma_index                 = p_sys_lmep->ma_index;
    ds_bfd_mep.local_stat               = p_sys_bfd_lmep->local_state;
    ds_bfd_mep.local_diag               = p_sys_bfd_lmep->local_diag;
    ds_bfd_mep.local_disc               = p_sys_bfd_lmep->local_discr;

#if (SDK_WORK_PLATFORM == 1)
    /*UML*/
    if (!drv_greatbelt_io_is_asic())
    {
        ds_bfd_mep.desired_min_tx_interval  = 1000;
        ds_bfd_mep.hello_while              = p_sys_bfd_lmep->desired_min_tx_interval;
        ds_bfd_mep.actual_min_tx_interval   = p_sys_bfd_lmep->desired_min_tx_interval;
    }
    else
    {
        ds_bfd_mep.desired_min_tx_interval  = 1000;
        ds_bfd_mep.hello_while              = 1000;
        ds_bfd_mep.actual_min_tx_interval   = 1000;
    }
#else
    ds_bfd_mep.desired_min_tx_interval  = 1000;
    ds_bfd_mep.hello_while              = 1000;
    ds_bfd_mep.actual_min_tx_interval   = 1000;
#endif
    p_sys_bfd_lmep->actual_tx_interval = ds_bfd_mep.actual_min_tx_interval;
    /*
    ds_bfd_mep.hello_while              = p_sys_bfd_lmep->actual_tx_interval;
    ds_bfd_mep.actual_min_tx_interval   = p_sys_bfd_lmep->actual_tx_interval;
    */
    if ((CTC_OAM_MEP_TYPE_IP_BFD == mep_type) || (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type))
    {
        ds_bfd_mep.mep_type                 = 5;
        ds_bfd_mep.bfd_srcport              = p_sys_bfd_lmep->bfd_src_port;

        if ((CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
            && CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV))
        {
            ds_bfd_mep.mep_type             = 7;
        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        ds_bfd_mep.mep_type             = 7;

        ds_bfd_mep.is_csf_tx_en         = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_TX_CSF_EN);
        ds_bfd_mep.is_csf_en            = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN);

        /*6'd0,rxCsfType[2:0],dCsf,csfType[2:0],csfWhile[2:0]*/
        csf_type = _sys_greatbelt_bfd_csf_convert(lchip, p_sys_bfd_lmep->tx_csf_type, TRUE);
        ds_bfd_mep.bfd_srcport          = 0;
        /*tx csf type*/
        ds_bfd_mep.bfd_srcport          |= ((csf_type & 0x7)<< 3);
        /*csfWhile*/
        ds_bfd_mep.bfd_srcport          |= 4;
        /*Should be 4*/

        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)
        {
            ds_bfd_mep.cc_tx_mode               = 1;
            ds_bfd_mep.hello_while              = 4;
            ds_bfd_mep.actual_min_tx_interval   = 4;
            ds_ma.ccm_interval                  = p_sys_bfd_lmep->actual_tx_interval;
        }
    }


    ds_bfd_mep.sf_fail_while            = 1;
    ds_bfd_mep.rx_csf_while             = 14;
    ds_bfd_mep.is_bfd                   = 1;
    ds_bfd_mep.is_remote                = 0;
    ds_bfd_mep.is_up                    = 0;
    ds_bfd_mep.pbit                     = 0;
    ds_bfd_mep.eth_oam_p2_p_mode        = 1;
    ds_bfd_mep.cci_en                   = 0;

    SYS_OAM_DBG_INFO("LMEP: ds_bfd_mep.active            = %d\n", ds_bfd_mep.active);
    SYS_OAM_DBG_INFO("LMEP: ds_bfd_mep.ma_index          = %d\n", ds_bfd_mep.ma_index);
    SYS_OAM_DBG_INFO("LMEP: ds_bfd_mep.is_remote         = %d\n", ds_bfd_mep.is_remote);

    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsBfdMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_mep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_add_to_asic(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_lmep);

    if (CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_bfd_lmep_add_tbl_to_asic(lchip, &p_sys_lmep->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    uint32 index    = 0;
    uint32 cmd      = 0;
    ds_bfd_mep_t ds_bfd_mep;
    ctc_chip_device_info_t device_info;

    SYS_OAM_DBG_FUNC();

    if (CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    sys_greatbelt_chip_get_device_info(lchip, &device_info);

    if (p_sys_lmep->aps_bridge_en && (1== device_info.version_id))
    {/*use multicast to process aps group*/
        sal_memset(&ds_bfd_mep, 0, sizeof(ds_bfd_mep_t));
        lchip = p_sys_lmep->com.lchip;
        index = p_sys_lmep->com.lmep_index;
        cmd = DRV_IOR(DsBfdMep_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_bfd_mep));
        sys_greatbelt_nh_delete_mcast_aps(lchip, ds_bfd_mep.dest_id);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_asic(lchip, &p_sys_lmep->com));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_bfd_lmep_update_tbl_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep)
{
    sys_oam_lmep_bfd_t* p_sys_lmep_bfd  = (sys_oam_lmep_bfd_t*)p_sys_lmep;
    sys_oam_rmep_bfd_t* p_sys_rmep_bfd  = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint8 csf_type    = 0;

    ds_ma_t ds_ma;                    /*DS_MA */
    ds_bfd_mep_t ds_bfd_mep;          /* DS_ETH_MEP */
    ds_bfd_mep_t ds_bfd_mep_mask;          /* DS_ETH_MEP */
    ds_bfd_rmep_t ds_bfd_rmep;
    ds_bfd_rmep_t ds_bfd_rmep_mask;
    tbl_entry_t  tbl_entry;

    sys_oam_rmep_com_t* p_sys_rmep = NULL;
    sys_oam_nhop_info_t oam_nhop_info;

    uint16 desired_min_tx_interval = 0;
    uint16 required_min_rx_interval = 0;
    ctc_oam_bfd_timer_t*  oam_bfd_timer = NULL;
    sal_systime_t tv;
    uint32 interval = 3000000;  /*3s*/
    uint32 tmp = 0;
    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    sal_memset(&ds_bfd_mep, 0, sizeof(ds_bfd_mep_t));
    sal_memset(&ds_bfd_mep_mask, 0xFF, sizeof(ds_bfd_mep_t));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOR(DsBfdMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_bfd_mep));

    sal_memset(&ds_ma, 0, sizeof(ds_ma_t));
    index = p_sys_lmep->ma_index;
    cmd = DRV_IOR(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    p_sys_rmep = (sys_oam_rmep_com_t*)CTC_SLISTHEAD(p_sys_lmep->rmep_list);
    if (NULL == p_sys_rmep)
    {
        return CTC_E_OAM_RMEP_NOT_FOUND;
    }
    p_sys_rmep_bfd  = (sys_oam_rmep_bfd_t*)p_sys_rmep;
    sal_memset(&ds_bfd_rmep, 0, sizeof(ds_bfd_rmep_t));
    sal_memset(&ds_bfd_rmep_mask, 0xFF, sizeof(ds_bfd_rmep_t));
    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOR(DsBfdRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_bfd_rmep));

    switch (p_sys_lmep_bfd->update_type)
    {
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_MEP_EN:
        if (CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_EN))
        {
            ds_bfd_mep.active                   = 1;
        }
        else
        {
            ds_bfd_mep.active                   = 0;
        }
        ds_bfd_mep_mask.active                  = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CC_EN:
        ds_bfd_mep.cci_en               = (*p_sys_lmep_bfd->p_update_value);
        ds_bfd_mep_mask.cci_en          = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CV_EN:
        ds_bfd_rmep.is_cv_en            = (*p_sys_lmep_bfd->p_update_value);
        ds_bfd_rmep_mask.is_cv_en       = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_COS_EXP:
        ds_ma.priority_index                 = p_sys_lmep_bfd->tx_cos_exp;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        ds_ma.aps_signal_fail_local           = (*p_sys_lmep_bfd->p_update_value);
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CSF_EN:
        ds_bfd_mep.is_csf_en    = CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN);
        ds_bfd_rmep.is_csf_en   = CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN);
        ds_bfd_mep_mask.is_csf_en   = 0;
        ds_bfd_rmep_mask.is_csf_en  = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_CSF_EN:
        ds_bfd_mep.is_csf_tx_en = CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_TX_CSF_EN);
        ds_bfd_mep_mask.is_csf_tx_en = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        /*6'd0,rxCsfType[2:0],dCsf,csfType[2:0],csfWhile[2:0]*/
        /*tx csf type*/
        csf_type = _sys_greatbelt_bfd_csf_convert(lchip, p_sys_lmep_bfd->tx_csf_type, TRUE);
        ds_bfd_mep.bfd_srcport          &= 0xFFC7;
        ds_bfd_mep.bfd_srcport          |= ((csf_type & 0x7) << 3);
        ds_bfd_mep_mask.bfd_srcport     &= 0xFFC7;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DCSF:
        /*6'd0,rxCsfType[2:0],dCsf,csfType[2:0],csfWhile[2:0]*/
        /*d csf*/
        ( 1 == *p_sys_lmep_bfd->p_update_value) ? CTC_BIT_SET(ds_bfd_mep.bfd_srcport, 6)
                                                : CTC_BIT_UNSET(ds_bfd_mep.bfd_srcport, 6) ;
        CTC_BIT_UNSET(ds_bfd_mep_mask.bfd_srcport, 6) ;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DIAG:
        ds_bfd_mep.local_diag       = p_sys_lmep_bfd->local_diag;
        ds_bfd_mep_mask.local_diag  = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_STATE:
        ds_bfd_mep.local_stat       = p_sys_lmep_bfd->local_state;
        ds_bfd_mep_mask.local_stat  = 0;
        if (CTC_OAM_BFD_STATE_ADMIN_DOWN == p_sys_lmep_bfd->local_state)
        {
            ds_bfd_mep.hello_while = 0;
            ds_bfd_mep_mask.hello_while = 0;
        }
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DISCR:
        ds_bfd_mep.local_disc       = p_sys_lmep_bfd->local_discr;
        ds_bfd_mep_mask.local_disc  = 0;
        break;
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_RX_TIMER:
        /*update rx timer begin*/
        oam_bfd_timer = (ctc_oam_bfd_timer_t*)p_sys_lmep_bfd->p_update_value;
        required_min_rx_interval = oam_bfd_timer[1].min_interval;
        p_sys_rmep_bfd->remote_detect_mult = oam_bfd_timer[1].detect_mult;
        if(CTC_OAM_BFD_STATE_UP == ds_bfd_rmep.remote_stat)
        {
            sal_gettime(&tv);
            if ((((tv.tv_sec - p_sys_lmep_bfd->tv_p_set.tv_sec)*1000000 + (tv.tv_usec - p_sys_lmep_bfd->tv_p_set.tv_usec)) < interval)
                && !g_gb_oam_master[lchip]->timer_update_disable)
            {
                return CTC_E_HW_BUSY;
            }
            if (required_min_rx_interval > ds_bfd_rmep.actual_rx_interval)
            {
                ds_bfd_rmep.actual_rx_interval          = required_min_rx_interval;
                ds_bfd_rmep_mask.actual_rx_interval     = 0;
            }
        }
        ds_bfd_rmep.required_min_rx_interval        = required_min_rx_interval;
        ds_bfd_rmep_mask.required_min_rx_interval   = 0;
        p_sys_rmep_bfd->required_min_rx_interval = required_min_rx_interval;
        ds_bfd_rmep.defect_mult = p_sys_rmep_bfd->remote_detect_mult;
        ds_bfd_rmep_mask.defect_mult   = 0;
        ds_bfd_rmep.defect_while = ds_bfd_rmep.actual_rx_interval* ds_bfd_rmep.defect_mult;
        ds_bfd_rmep_mask.defect_while   = 0;
        /*update tx timer begin, can not break*/
        p_sys_lmep_bfd->local_detect_mult = oam_bfd_timer[0].detect_mult;
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_TIMER:
        desired_min_tx_interval  = ((ctc_oam_bfd_timer_t*)p_sys_lmep_bfd->p_update_value)->min_interval;

        if (CTC_OAM_BFD_STATE_UP == ds_bfd_mep.local_stat)
        {
            if ((ds_bfd_mep.pbit || ds_bfd_rmep.fbit) && !g_gb_oam_master[lchip]->timer_update_disable)
            {
                return CTC_E_HW_BUSY;
            }
            sal_gettime(&tv);
            if ((((tv.tv_sec - p_sys_lmep_bfd->tv_p_set.tv_sec)*1000000 + (tv.tv_usec - p_sys_lmep_bfd->tv_p_set.tv_usec)) < interval)
                && !g_gb_oam_master[lchip]->timer_update_disable)
            {
                return CTC_E_HW_BUSY;
            }
            sal_gettime(&p_sys_lmep_bfd->tv_p_set);
            ds_bfd_mep.desired_min_tx_interval          = desired_min_tx_interval;
            ds_bfd_mep_mask.desired_min_tx_interval     = 0;

            ds_bfd_mep.pbit                     = 1;
            ds_bfd_mep_mask.pbit                = 0;
            ds_bfd_rmep.fbit                     = 0;
            ds_bfd_rmep_mask.fbit                = 0;
            if ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep_bfd->com.p_sys_chan->mep_type)
                && (P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms))
            {
                ds_ma.ccm_interval                  = p_sys_lmep_bfd->actual_tx_interval;
            }
            else
            {
                if(desired_min_tx_interval < ds_bfd_mep.actual_min_tx_interval)
                {
                    ds_bfd_mep.actual_min_tx_interval       = ((desired_min_tx_interval<=3)  ?
                                                                desired_min_tx_interval : (desired_min_tx_interval*4/5));
                    ds_bfd_mep_mask.actual_min_tx_interval  = 0;
                }
            }
        }
        p_sys_lmep_bfd->desired_min_tx_interval = desired_min_tx_interval;

        ds_bfd_rmep.local_defect_mult           = p_sys_lmep_bfd->local_detect_mult;
        ds_bfd_rmep_mask.local_defect_mult      = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_ACTUAL_TX_TIMER:
        ds_bfd_mep.actual_min_tx_interval       = p_sys_lmep_bfd->actual_tx_interval;
        ds_bfd_mep_mask.actual_min_tx_interval = 0;
        tmp = (p_sys_lmep_bfd->actual_tx_interval > 100) ? 100 : p_sys_lmep_bfd->actual_tx_interval;
        ds_bfd_mep.hello_while = (p_sys_lmep->lmep_index / 2) % tmp;
        ds_bfd_mep_mask.hello_while = 0;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_NHOP:
        sal_memset(&oam_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
        _sys_greatbelt_oam_get_nexthop_info(lchip, p_sys_lmep_bfd->nhid, CTC_FLAG_ISSET(p_sys_lmep_bfd->flag, CTC_OAM_BFD_LMEP_FLAG_PROTECTION_PATH), &oam_nhop_info);

        ds_ma.next_hop_ext          = oam_nhop_info.nexthop_is_8w;
        ds_ma.next_hop_ptr          = oam_nhop_info.dsnh_offset;
        ds_bfd_mep.dest_chip        = oam_nhop_info.dest_chipid;
        ds_bfd_mep.dest_id          = oam_nhop_info.dest_id;
        ds_bfd_mep_mask.dest_chip   = 0;
        ds_bfd_mep_mask.dest_id     = 0;

        if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_lmep_bfd->com.p_sys_chan->mep_type)
        {
            ((sys_oam_chan_tp_t*)(p_sys_lmep_bfd->com.p_sys_chan))->out_label = oam_nhop_info.mpls_out_label;
        }
        ds_bfd_rmep.aps_bridge_en           = oam_nhop_info.aps_bridge_en;
        ds_bfd_rmep_mask.aps_bridge_en      = 0;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }
    if (g_gb_oam_master[lchip]->timer_update_disable)
    {
        ds_bfd_rmep.defect_while = 0x3FFF;
        ds_bfd_rmep_mask.defect_while = 0;
    }
    index = p_sys_lmep->ma_index;
    cmd = DRV_IOW(DsMa_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_ma));

    index = p_sys_lmep->lmep_index;
    cmd = DRV_IOW(DsBfdMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_mep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsBfdRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_rmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_lmep_update_asic(uint8 lchip, ctc_oam_update_t* p_lmep_param,
                                         sys_oam_lmep_bfd_t* p_sys_lmep)
{
    ctc_oam_update_t* p_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);
    CTC_PTR_VALID_CHECK(p_sys_lmep);

    p_lmep = p_lmep_param;
    p_sys_lmep->update_type = p_lmep->update_type;

    /*
    */
    switch (p_sys_lmep->update_type)
    {
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_MEP_EN);
        }

        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CC_EN:
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CV_EN:
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DCSF:
        p_sys_lmep->p_update_value = &p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_TIMER:
        /*
        p_sys_lmep->actual_tx_interval      = ((ctc_oam_bfd_timer_t*)p_lmep->p_update_value)->actual_interval;
        p_sys_lmep->desired_min_tx_interval = ((ctc_oam_bfd_timer_t*)p_lmep->p_update_value)->min_interval;
        */
        p_sys_lmep->local_detect_mult       = ((ctc_oam_bfd_timer_t*)p_lmep->p_update_value)->detect_mult;
        p_sys_lmep->p_update_value          = p_lmep->p_update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_ACTUAL_TX_TIMER:
        p_sys_lmep->actual_tx_interval      = ((ctc_oam_bfd_timer_t*)p_lmep->p_update_value)->min_interval;
        p_sys_lmep->p_update_value          = p_lmep->p_update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_COS_EXP:
        p_sys_lmep->tx_cos_exp = p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_CSF_EN:
        (1 == p_lmep->update_value) ?
            (CTC_SET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN)) : (CTC_UNSET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN));
        break;
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_CSF_EN:
        (1 == p_lmep->update_value) ? (CTC_SET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_TX_CSF_EN)) : (CTC_UNSET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_TX_CSF_EN));
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        p_sys_lmep->tx_csf_type = p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DIAG:
        p_sys_lmep->local_diag  = p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_STATE:
        p_sys_lmep->local_state = p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_DISCR:
        p_sys_lmep->local_discr = p_lmep->update_value;
        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_NHOP:
        p_sys_lmep->nhid        = p_lmep->update_value;
        break;
    case  CTC_OAM_BFD_LMEP_UPDATE_TYPE_MASTER_GCHIP:
        CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_master_chip(p_sys_lmep->com.p_sys_chan, &p_lmep->update_value));
        return CTC_E_NONE;
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_TX_RX_TIMER:
        p_sys_lmep->p_update_value  = p_lmep->p_update_value;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_bfd_lmep_update_tbl_asic(lchip, &p_sys_lmep->com));

    return CTC_E_NONE;
}


int32
_sys_greatbelt_bfd_update_lmep_lm(uint8 lchip, ctc_oam_update_t* p_lmep_param,
                                         sys_oam_lmep_bfd_t* p_sys_lmep)
{
    void* p_chan      = NULL;
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
    p_sys_chan_tp = (sys_oam_chan_tp_t*)p_sys_lmep->com.p_sys_chan;

    if (CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN))
    {
        if((CTC_OAM_BFD_LMEP_UPDATE_TYPE_LM_COS_TYPE == p_lmep->update_type)
            || (CTC_OAM_BFD_LMEP_UPDATE_TYPE_LM_COS == p_lmep->update_type))
        {
            return CTC_E_OAM_INVALID_CFG_LM;
        }
    }

    /*2. lmep db & asic update*/
    switch (p_lmep->update_type)
    {
    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_LM_EN:
        if (1 == p_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN);
            p_sys_chan_tp->lm_type = CTC_OAM_LM_TYPE_SINGLE;
            if (!p_sys_chan_tp->lm_index_alloced)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_tp->com, FALSE));
                p_sys_chan_tp->lm_index_alloced = TRUE;
            }
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN);
            p_sys_chan_tp->lm_type = CTC_OAM_LM_TYPE_NONE;
            if (p_sys_chan_tp->lm_index_alloced)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_tp->com, FALSE));
                p_sys_chan_tp->lm_index_alloced = FALSE;
            }
        }

        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_LM_COS_TYPE:
        p_sys_chan_tp->lm_cos_type = p_lmep->update_value;

        break;

    case CTC_OAM_BFD_LMEP_UPDATE_TYPE_LM_COS:
        p_sys_chan_tp->lm_cos = p_lmep->update_value;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    lm_en = CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_BFD_LMEP_FLAG_LM_EN);

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
        p_ds_mpls_oam_tcam_chan->mpls_lm_type   = lm_en;

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

        ds_mpls.mpls_lm_type    = lm_en;
        ds_mpls.lm_base         = p_sys_chan_tp->com.lm_index_base;
        ds_mpls.lm_cos          = p_sys_chan_tp->lm_cos;
        ds_mpls.lm_cos_type     = p_sys_chan_tp->lm_cos_type;
        ds_mpls.lm_type         = p_sys_chan_tp->lm_type;

        mpls_cmd = DRV_IOW(DsMpls_t, DRV_ENTRY_FLAG);


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, mpls_cmd, &ds_mpls));


        /*update nexthop to enbale egress lm to be add*/
        sys_greatbelt_mpls_nh_update_oam_en(lchip, p_sys_lmep->nhid, p_sys_chan_tp->out_label, lm_en);

    }
    else
    { /*need update port or interface lm enable*/
        uint32 cmd      = 0;
        uint32 field    = 0;
        uint8  gchip    = 0;

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
            gchip = CTC_MAP_GPORT_TO_GCHIP(p_sys_chan_tp->key.gport_l3if_id);
            CTC_ERROR_RETURN(sys_greatbelt_chip_is_local(lchip, gchip));
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MplsSectionLmEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field));
        }
    }

    return CTC_E_NONE;
}


#define RMEP "Function Begin"

sys_oam_rmep_bfd_t*
_sys_greatbelt_bfd_rmep_lkup(uint8 lchip, sys_oam_lmep_bfd_t* p_sys_lmep)
{
    sys_oam_rmep_bfd_t sys_oam_rmep_bfd;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_rmep_bfd, 0, sizeof(sys_oam_rmep_bfd_t));

    return (sys_oam_rmep_bfd_t*)_sys_greatbelt_oam_rmep_lkup(lchip, &p_sys_lmep->com, &sys_oam_rmep_bfd.com);
}

int32
_sys_greatbelt_bfd_rmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_rmep_bfd_t* p_rmep_bfd_db   = (sys_oam_rmep_bfd_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (NULL != p_rmep_bfd_db)
    {
        return 1;
    }

    return 0;
}

sys_oam_rmep_bfd_t*
_sys_greatbelt_bfd_rmep_build_node(uint8 lchip, ctc_oam_rmep_t* p_rmep_param)
{
    sys_oam_rmep_bfd_t* p_sys_oam_rmep_bfd = NULL;
    ctc_oam_bfd_rmep_t* p_ctc_oam_rmep_bfd = NULL;

    SYS_OAM_DBG_FUNC();

    p_ctc_oam_rmep_bfd = &p_rmep_param->u.bfd_rmep;

    p_sys_oam_rmep_bfd = (sys_oam_rmep_bfd_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_rmep_bfd_t));
    if (NULL != p_sys_oam_rmep_bfd)
    {
        sal_memset(p_sys_oam_rmep_bfd, 0, sizeof(sys_oam_rmep_bfd_t));
        p_sys_oam_rmep_bfd->com.rmep_index              = p_rmep_param->rmep_index;

        p_sys_oam_rmep_bfd->flag                        = p_ctc_oam_rmep_bfd->flag;
         /*p_sys_oam_rmep_bfd->actual_rx_interval          = p_ctc_oam_rmep_bfd->actual_rx_interval;*/
        p_sys_oam_rmep_bfd->required_min_rx_interval    = p_ctc_oam_rmep_bfd->required_min_rx_interval;
        p_sys_oam_rmep_bfd->remote_discr                = p_ctc_oam_rmep_bfd->remote_discr;

        p_sys_oam_rmep_bfd->remote_diag                 = p_ctc_oam_rmep_bfd->remote_diag;
        p_sys_oam_rmep_bfd->remote_state                = p_ctc_oam_rmep_bfd->remote_state;
        p_sys_oam_rmep_bfd->remote_detect_mult          = p_ctc_oam_rmep_bfd->remote_detect_mult;
    }

    return p_sys_oam_rmep_bfd;
}

int32
_sys_greatbelt_bfd_rmep_free_node(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_rmep_bfd);
    p_sys_rmep_bfd = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_build_index(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_build_index(lchip, &p_sys_rmep_bfd->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_free_index(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_free_index(lchip, &p_sys_rmep_bfd->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_add_to_db(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_add_to_db(lchip, &p_sys_rmep_bfd->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_del_from_db(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_db(lchip, &p_sys_rmep_bfd->com));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_bfd_rmep_add_tbl_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    uint8 mep_type      = CTC_OAM_MEP_TYPE_MAX;

    ds_bfd_rmep_t ds_bfd_rmep;
    ds_bfd_rmep_t ds_bfd_rmep_mask;
    tbl_entry_t  tbl_entry;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    sys_oam_rmep_bfd_t* p_sys_bfd_rmep = NULL;
    sys_oam_nhop_info_t oam_nhop_info;
    ctc_chip_device_info_t device_info;

    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 rmep_flag    = 0;
    uint32 lmep_flag    = 0;

    p_sys_lmep     = p_sys_rmep->p_sys_lmep;
    p_sys_bfd_rmep = (sys_oam_rmep_bfd_t*)p_sys_rmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        if (0xFFFFFFFF != ((sys_oam_lmep_bfd_t*)p_sys_lmep)->mpls_in_label)
        {
            sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, ((sys_oam_lmep_bfd_t*)p_sys_lmep)->spaceid,
                                                               ((sys_oam_lmep_bfd_t*)p_sys_lmep)->mpls_in_label, &index);
            cmd     = DRV_IOW(DsMpls_t, DsMpls_MepIndex_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &p_sys_lmep->lmep_index));
        }
        goto RETURN;
    }

    mep_type    = p_sys_lmep->p_sys_chan->mep_type;

    rmep_flag   = p_sys_bfd_rmep->flag;
    lmep_flag   = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->flag;

    sal_memset(&ds_bfd_rmep, 0, sizeof(ds_bfd_rmep_t));
    sal_memset(&ds_bfd_rmep_mask, 0, sizeof(ds_bfd_rmep_t));

    ds_bfd_rmep.active      = CTC_FLAG_ISSET(rmep_flag, CTC_OAM_BFD_RMEP_FLAG_MEP_EN);
    ds_bfd_rmep.aps_en      = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_APS_EN);
    ds_bfd_rmep.is_remote               = 1;
    ds_bfd_rmep.is_bfd                  = 1;
    ds_bfd_rmep.eth_oam_p2_p_mode       = 1;
    ds_bfd_rmep.disc_chk_en             = 1;
    ds_bfd_rmep.pbit                    = 0;
    ds_bfd_rmep.fbit                    = 0;
    if (g_gb_oam_master[lchip]->timer_update_disable)
    {
        ds_bfd_rmep.first_pkt_rx            = 1;
    }
    else
    {
        ds_bfd_rmep.first_pkt_rx            = 0;
    }

    ds_bfd_rmep.aps_signal_fail_remote  = 0;
    ds_bfd_rmep.sf_fail_while           = 0;

    ds_bfd_rmep.remote_diag             = p_sys_bfd_rmep->remote_diag;
    ds_bfd_rmep.remote_stat             = p_sys_bfd_rmep->remote_state;
    ds_bfd_rmep.remote_disc             = p_sys_bfd_rmep->remote_discr;
    ds_bfd_rmep.defect_mult             = p_sys_bfd_rmep->remote_detect_mult;
    ds_bfd_rmep.local_defect_mult       = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->local_detect_mult;

    ds_bfd_rmep.required_min_rx_interval  = p_sys_bfd_rmep->required_min_rx_interval;

#if (SDK_WORK_PLATFORM == 1)
    /*UML*/
    if (!drv_greatbelt_io_is_asic())
    {
        ds_bfd_rmep.actual_rx_interval        = p_sys_bfd_rmep->required_min_rx_interval;
    }
    else
    {
        ds_bfd_rmep.actual_rx_interval        = 1000;
    }

#else
    ds_bfd_rmep.actual_rx_interval        = 1000;
#endif

    ds_bfd_rmep.defect_while              = p_sys_bfd_rmep->actual_rx_interval*p_sys_bfd_rmep->remote_detect_mult;

    sal_memset(&oam_nhop_info, 0, sizeof(sys_oam_nhop_info_t));
    _sys_greatbelt_oam_get_nexthop_info(lchip, ((sys_oam_lmep_bfd_t*)p_sys_lmep)->nhid, CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_PROTECTION_PATH), &oam_nhop_info);

    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    sys_greatbelt_chip_get_device_info(lchip, &device_info);

    ds_bfd_rmep.tx_mcast_en             = ((SYS_NH_TYPE_MCAST == oam_nhop_info.nh_entry_type)? 1:0);

    if (oam_nhop_info.aps_bridge_en && (1== device_info.version_id))
    {/*use multicast to process aps group*/
        ds_bfd_rmep.tx_mcast_en             = 1;
    }
    else
    {
        ds_bfd_rmep.aps_bridge_en             = oam_nhop_info.aps_bridge_en;
    }


    if (CTC_OAM_MEP_TYPE_IP_BFD == mep_type)
    {
        ds_bfd_rmep.cc_tx_mode      = 0;
        ds_bfd_rmep.is_multi_hop    = !CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_IP_SINGLE_HOP);
        if (oam_nhop_info.is_nat)
        {
            /*add for ip bfd edit use mpls bfd mode*/
            ds_bfd_rmep.ip_sa           = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->ip4_sa;
        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_BFD == mep_type)
    {
        ds_bfd_rmep.cc_tx_mode      = 0;
        ds_bfd_rmep.ip_sa           = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->ip4_sa;
        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_MPLS_PW_VCCV))
        {
            ds_bfd_rmep.ach_chan_type   = 3;
            if (0xFFFFFFFF != ((sys_oam_lmep_bfd_t*)p_sys_lmep)->mpls_in_label)
            {
                /*update local index in channel*/
                 /*index   = ((sys_oam_lmep_bfd_t*)p_sys_lmep)->mpls_in_label;*/
                sys_greatbelt_mpls_calc_real_label_for_label_range(lchip, ((sys_oam_lmep_bfd_t*)p_sys_lmep)->spaceid,
                                                                   ((sys_oam_lmep_bfd_t*)p_sys_lmep)->mpls_in_label, &index);
                cmd     = DRV_IOW(DsMpls_t, DsMpls_MepIndex_f);


                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &p_sys_lmep->lmep_index));

            }
        }
    }
    else if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        ds_bfd_rmep.is_cv_en        = 0;
        ds_bfd_rmep.is_csf_en       = CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_CSF_EN);
        ds_bfd_rmep.ach_chan_type   = 1;
        /*{5'd0,maIdLengthType[1:0], maNameIndex[13:0], ccmInterval[2:0],cvWhile[2:0], dMisConWhile[3:0],dMisCon}*/
        if (CTC_FLAG_ISSET(lmep_flag, CTC_OAM_BFD_LMEP_FLAG_TP_WITH_MEP_ID))
        {
            ds_bfd_rmep.ip_sa           = 0;
            /*interval to 1s*/
            ds_bfd_rmep.ip_sa           |= ((4 & 0x7)<< 8);
            /*mepid index*/
            ds_bfd_rmep.ip_sa           |= (((p_sys_lmep->p_sys_maid->maid_index) & 0x3FFF) << 11);
            /*mepid length*/
            ds_bfd_rmep.ip_sa           |= (((p_sys_lmep->p_sys_maid->maid_entry_num >> 1) & 0x3) << 25);
            /*cv while*/
            ds_bfd_rmep.ip_sa           |= (4 << 5);
            /*dMisconwhile*/
            ds_bfd_rmep.ip_sa           |= (14 << 1);
        }

        if (P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)
        {
            ds_bfd_rmep.cc_tx_mode              = 1;
            ds_bfd_rmep.actual_rx_interval      = 4;
            ds_bfd_rmep.defect_while            = 4*p_sys_bfd_rmep->remote_detect_mult;
        }

    }

    index = p_sys_rmep->rmep_index;
    cmd = DRV_IOW(DsBfdRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_rmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_add_to_asic(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_rmep_bfd);

    CTC_ERROR_RETURN(_sys_greatbelt_bfd_rmep_add_tbl_to_asic(lchip, &p_sys_rmep_bfd->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep_bfd)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_asic(lchip, &p_sys_rmep_bfd->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_rmep_update_tbl_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep)
{
    sys_oam_rmep_bfd_t* p_sys_rmep_bfd  = (sys_oam_rmep_bfd_t*)p_sys_rmep;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint32 l_index    = 0;
    ds_bfd_rmep_t ds_bfd_rmep;
    ds_bfd_rmep_t ds_bfd_rmep_mask;
    ds_bfd_mep_t ds_bfd_mep;
    ds_bfd_mep_t ds_bfd_mep_mask;
    tbl_entry_t  tbl_entry;
    uint16 required_min_rx_interval = 0;
    sal_systime_t tv;
    uint32 interval = 3000000;  /*3s*/
    sys_oam_lmep_bfd_t* p_sys_lmep_bfd  = NULL;

    p_sys_lmep = p_sys_rmep->p_sys_lmep;
    p_sys_lmep_bfd  = (sys_oam_lmep_bfd_t*)p_sys_lmep;
    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    index = p_sys_rmep->rmep_index;
    l_index = index - 1;
    sal_memset(&ds_bfd_rmep, 0, sizeof(ds_bfd_rmep_t));
    sal_memset(&ds_bfd_rmep_mask, 0xFF, sizeof(ds_bfd_rmep_t));
    cmd = DRV_IOR(DsBfdRmep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_bfd_rmep));

    sal_memset(&ds_bfd_mep, 0, sizeof(ds_bfd_mep_t));
    sal_memset(&ds_bfd_mep_mask, 0xFF, sizeof(ds_bfd_mep_t));
    cmd = DRV_IOR(DsBfdMep_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l_index, cmd, &ds_bfd_mep));

    switch (p_sys_rmep_bfd->update_type)
    {
    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_MEP_EN:

        if (CTC_FLAG_ISSET(p_sys_rmep_bfd->flag, CTC_OAM_BFD_RMEP_FLAG_MEP_EN))
        {
            ds_bfd_rmep.active          = 1;
        }
        else
        {
            ds_bfd_rmep.active          = 0;
        }
        ds_bfd_rmep_mask.active         = 0;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        ds_bfd_rmep.aps_signal_fail_remote      = *p_sys_rmep_bfd->p_update_value;
        ds_bfd_rmep_mask.aps_signal_fail_remote = 0;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_DIAG:
        ds_bfd_rmep.remote_diag                 = p_sys_rmep_bfd->remote_diag;
        ds_bfd_rmep_mask.remote_diag            = 0;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_STATE:
        ds_bfd_rmep.remote_stat                 = p_sys_rmep_bfd->remote_state;
        ds_bfd_rmep_mask.remote_stat            = 0;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_DISCR:
        ds_bfd_rmep.remote_disc         = p_sys_rmep_bfd->remote_discr;
        ds_bfd_rmep_mask.remote_disc    = 0;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_RX_TIMER:
        required_min_rx_interval = ((ctc_oam_bfd_timer_t*)p_sys_rmep_bfd->p_update_value)->min_interval;

        if(CTC_OAM_BFD_STATE_UP == ds_bfd_rmep.remote_stat)
        {
            if ((ds_bfd_mep.pbit || ds_bfd_rmep.fbit) && !g_gb_oam_master[lchip]->timer_update_disable)
            {
                return CTC_E_HW_BUSY;
            }
            sal_gettime(&tv);
            if ((((tv.tv_sec - p_sys_lmep_bfd->tv_p_set.tv_sec)*1000000 + (tv.tv_usec - p_sys_lmep_bfd->tv_p_set.tv_usec)) < interval)
                && !g_gb_oam_master[lchip]->timer_update_disable)
            {
                return CTC_E_HW_BUSY;
            }
            sal_gettime(&p_sys_lmep_bfd->tv_p_set);
            ds_bfd_rmep.required_min_rx_interval        = required_min_rx_interval;
            ds_bfd_rmep_mask.required_min_rx_interval   = 0;
            ds_bfd_mep.pbit         = 1;
            ds_bfd_mep_mask.pbit    = 0;
            ds_bfd_rmep.fbit                     = 0;
            ds_bfd_rmep_mask.fbit                = 0;
            if ((CTC_OAM_MEP_TYPE_MPLS_TP_BFD == p_sys_rmep_bfd->com.p_sys_lmep->p_sys_chan->mep_type)
                && (P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms))
            {

            }
            else
            {
                if(required_min_rx_interval > ds_bfd_rmep.actual_rx_interval)
                {
                    ds_bfd_rmep.actual_rx_interval          = required_min_rx_interval;
                    ds_bfd_rmep_mask.actual_rx_interval     = 0;
                }
            }
        }
        else
        {
            ds_bfd_rmep.required_min_rx_interval        = required_min_rx_interval;
            ds_bfd_rmep_mask.required_min_rx_interval   = 0;
        }
        p_sys_rmep_bfd->required_min_rx_interval = required_min_rx_interval;
        ds_bfd_rmep.defect_mult = p_sys_rmep_bfd->remote_detect_mult;
        ds_bfd_rmep_mask.defect_mult   = 0;
        ds_bfd_rmep.defect_while = ds_bfd_rmep.actual_rx_interval* ds_bfd_rmep.defect_mult;
        ds_bfd_rmep_mask.defect_while   = 0;

        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_ACTUAL_RX_TIMER:
        ds_bfd_rmep.actual_rx_interval        = p_sys_rmep_bfd->actual_rx_interval;
        ds_bfd_rmep_mask.actual_rx_interval   = 0;
        ds_bfd_rmep.defect_while = p_sys_rmep_bfd->actual_rx_interval* ds_bfd_rmep.defect_mult;
        ds_bfd_rmep_mask.defect_while   = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }
    if (g_gb_oam_master[lchip]->timer_update_disable)
    {
        ds_bfd_rmep.defect_while = 0x3FFF;
        ds_bfd_rmep_mask.defect_while = 0;
    }
    cmd = DRV_IOW(DsBfdRmep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_rmep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_rmep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &tbl_entry));

    cmd = DRV_IOW(DsBfdMep_t, DRV_ENTRY_FLAG);
    tbl_entry.data_entry = (uint32*)&ds_bfd_mep;
    tbl_entry.mask_entry = (uint32*)&ds_bfd_mep_mask;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, l_index, cmd, &tbl_entry));
RETURN:

    return CTC_E_NONE;
}


int32
_sys_greatbelt_bfd_rmep_update_asic(uint8 lchip, ctc_oam_update_t* p_rmep_param, sys_oam_rmep_bfd_t* p_sys_rmep)
{
    ctc_oam_update_t* p_rmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep);

    p_rmep = p_rmep_param;
    p_sys_rmep->update_type = p_rmep->update_type;

    switch (p_sys_rmep->update_type)
    {
    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep->flag, CTC_OAM_BFD_RMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep->flag, CTC_OAM_BFD_RMEP_FLAG_MEP_EN);
        }
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        p_sys_rmep->p_update_value  = &p_rmep->update_value;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_DIAG:
        p_sys_rmep->remote_diag     = p_rmep->update_value;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_STATE:
        p_sys_rmep->remote_state    = p_rmep->update_value;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_DISCR:
        p_sys_rmep->remote_discr    = p_rmep->update_value;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_RX_TIMER:
        /*
        p_sys_rmep->actual_rx_interval          = ((ctc_oam_bfd_timer_t*)p_rmep->p_update_value)->actual_interval;
        p_sys_rmep->required_min_rx_interval    = ((ctc_oam_bfd_timer_t*)p_rmep->p_update_value)->min_interval;
        */
        p_sys_rmep->remote_detect_mult          = ((ctc_oam_bfd_timer_t*)p_rmep->p_update_value)->detect_mult;
        p_sys_rmep->p_update_value              = p_rmep->p_update_value;
        break;

    case CTC_OAM_BFD_RMEP_UPDATE_TYPE_ACTUAL_RX_TIMER:
        p_sys_rmep->actual_rx_interval          = ((ctc_oam_bfd_timer_t*)p_rmep->p_update_value)->min_interval;
        p_sys_rmep->p_update_value              = p_rmep->p_update_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_bfd_rmep_update_tbl_asic(lchip, &p_sys_rmep->com));

    return CTC_E_NONE;
}

#define GET_INFO


int32 _sys_greatbelt_bfd_mep_info(uint8 lchip, sys_oam_rmep_bfd_t* p_sys_rmep, ctc_oam_mep_info_with_key_t* mep_info)
{
    uint16 local_index  = 0;
    uint16 remote_index = 0;
    uint8 mep_type  = 0;

    ctc_oam_bfd_rmep_info_t *bfd_rmep_info = NULL;
    ctc_oam_bfd_lmep_info_t *bfd_lmep_info = NULL;
    sys_oam_lmep_bfd_t* p_sys_lmep = NULL;

    ds_bfd_rmep_t ds_bfd_rmep;
    ds_bfd_mep_t ds_bfd_mep;

    uint32 cmd = 0;

    bfd_lmep_info = &mep_info->lmep.bfd_lmep;
    bfd_rmep_info = &mep_info->rmep.bfd_rmep;
    remote_index    = p_sys_rmep->com.rmep_index;
    local_index     = remote_index - 1;
    mep_type        = p_sys_rmep->com.p_sys_lmep->p_sys_chan->mep_type;
    p_sys_lmep      = (sys_oam_lmep_bfd_t*)(p_sys_rmep->com.p_sys_lmep);

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->com.p_sys_chan->master_chipid))
    {
        goto RETURN;
    }

    /*remote*/
    cmd    = DRV_IOR(DsBfdRmep_t,  DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, remote_index, cmd, &ds_bfd_rmep));

    bfd_rmep_info->actual_rx_interval = ds_bfd_rmep.actual_rx_interval;
    bfd_rmep_info->first_pkt_rx       = ds_bfd_rmep.first_pkt_rx;
    bfd_rmep_info->mep_en             = ds_bfd_rmep.active;
    bfd_rmep_info->required_min_rx_interval = ds_bfd_rmep.required_min_rx_interval;
    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        bfd_rmep_info->mis_connect        = CTC_IS_BIT_SET(ds_bfd_rmep.ip_sa, 0);
    }
    bfd_rmep_info->remote_detect_mult = ds_bfd_rmep.defect_mult;
    bfd_rmep_info->remote_diag        = ds_bfd_rmep.remote_diag;
    bfd_rmep_info->remote_discr       = ds_bfd_rmep.remote_disc;
    bfd_rmep_info->remote_state       = ds_bfd_rmep.remote_stat;
    bfd_rmep_info->lmep_index         = local_index;
    bfd_rmep_info->dloc_time          = ds_bfd_rmep.defect_while;

    /*local*/
    cmd     = DRV_IOR(DsBfdMep_t,   DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, local_index, cmd, &ds_bfd_mep));

    bfd_lmep_info->mep_en             = ds_bfd_mep.active;
    bfd_lmep_info->cc_enable          = ds_bfd_mep.cci_en;
    bfd_lmep_info->loacl_state        = ds_bfd_mep.local_stat;
    bfd_lmep_info->local_diag         = ds_bfd_mep.local_diag;
    bfd_lmep_info->local_detect_mult  = ds_bfd_rmep.local_defect_mult;
    bfd_lmep_info->local_discr        = ds_bfd_mep.local_disc;
    bfd_lmep_info->actual_tx_interval = p_sys_lmep->actual_tx_interval;
    bfd_lmep_info->desired_min_tx_interval = ds_bfd_mep.desired_min_tx_interval;
    bfd_lmep_info->single_hop         = !ds_bfd_rmep.is_multi_hop;
    if (CTC_OAM_MEP_TYPE_MPLS_TP_BFD == mep_type)
    {
        bfd_lmep_info->csf_en             = ds_bfd_mep.is_csf_en;
        bfd_lmep_info->d_csf              = CTC_IS_BIT_SET(ds_bfd_mep.bfd_srcport, 6);
        bfd_lmep_info->rx_csf_type        = _sys_greatbelt_bfd_csf_convert(lchip, ((ds_bfd_mep.bfd_srcport >> 7) & 0x7), FALSE);
    }
RETURN:

    return CTC_E_NONE;
}




#define INIT "Function Begin"

STATIC int32
_sys_greatbelt_bfd_register_init(uint8 lchip)
{
    uint32 cmd      = 0;
    uint32 field    = 0;
    parser_layer4_app_ctl_t parser_layer4_app_ctl;
    parser_layer4_ach_ctl_t parser_layer4_ach_ctl;
    oam_parser_ether_ctl_t  oam_parser_ether_ctl;
    ipe_mpls_ctl_t          ipe_mpls_ctl;
    epe_acl_qos_ctl_t       epe_acl_qos_ctl;
    ipe_lookup_ctl_t        ipe_lookup_ctl;
    oam_ether_tx_ctl_t      oam_ether_tx_ctl;
    oam_rx_proc_ether_ctl_t oam_rx_proc_ether_ctl;
    oam_header_edit_ctl_t   oam_header_edit_ctl;
    ds_oam_excp_t           ds_oam_excp;
    /*Parser*/
    sal_memset(&parser_layer4_app_ctl, 0, sizeof(parser_layer4_app_ctl_t));
    cmd = DRV_IOR(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_layer4_app_ctl));
    parser_layer4_app_ctl.bfd_en    = 1;
    parser_layer4_app_ctl.bfd_port0 = 3784;
    parser_layer4_app_ctl.bfd_port1 = 4784;
    cmd = DRV_IOW(ParserLayer4AppCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_layer4_app_ctl));

    sal_memset(&parser_layer4_ach_ctl, 0, sizeof(parser_layer4_ach_ctl_t));
    cmd = DRV_IOR(ParserLayer4AchCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_layer4_ach_ctl));
    parser_layer4_ach_ctl.ach_bfd_cc_type   = 0x0022;
    parser_layer4_ach_ctl.ach_bfd_cv_type   = 0x0023;
    parser_layer4_ach_ctl.ach_dlm_type      = 0x000A;
    parser_layer4_ach_ctl.ach_ilm_type      = 0x000B;
    parser_layer4_ach_ctl.ach_dm_type       = 0x000C;
    parser_layer4_ach_ctl.ach_dlm_dm_type   = 0x000D;
    parser_layer4_ach_ctl.ach_ilm_dm_type   = 0x000E;
    cmd = DRV_IOW(ParserLayer4AchCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_layer4_ach_ctl));

    sal_memset(&oam_parser_ether_ctl, 0, sizeof(oam_parser_ether_ctl_t));
    cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_parser_ether_ctl));
    oam_parser_ether_ctl.ach_bfd_chan_type_cc   = 0x0022;
    oam_parser_ether_ctl.ach_bfd_chan_type_cv   = 0x0023;
    oam_parser_ether_ctl.ach_chan_type_dm       = 0x000C;
    oam_parser_ether_ctl.ach_chan_type_dlmdm    = 0x000D;
    oam_parser_ether_ctl.ach_chan_type_dlm      = 0x000A;
    oam_parser_ether_ctl.ach_chan_type_csf      = P_COMMON_OAM_MASTER_GLB(lchip).tp_csf_ach_chan_type;
    oam_parser_ether_ctl.mpls_tp_oam_alert_label= 13;
    oam_parser_ether_ctl.mplstp_dm_msg_len      = 44;
    oam_parser_ether_ctl.mplstp_dlm_msg_len     = 52;
    oam_parser_ether_ctl.mplstp_dlmdm_msg_len   = 76;

    oam_parser_ether_ctl.bfd_len_check_en               = 1;
    oam_parser_ether_ctl.bfd_p2p_check_en               = 1;
    oam_parser_ether_ctl.bfd_pfbit_conflict_check_en    = 1;
    oam_parser_ether_ctl.bfd_detect_mult_check_en       = 1;
    oam_parser_ether_ctl.bfd_abit_check_en              = 1;
    oam_parser_ether_ctl.bfd_multipoint_chk             = 1;
    oam_parser_ether_ctl.bfd_ach_mep_id_type_chk_en     = 1;
    oam_parser_ether_ctl.bfd_ach_mep_id_max_type        = 2;
    oam_parser_ether_ctl.bfd_ach_mep_id_len_chk_en      = 1;
    oam_parser_ether_ctl.bfd_ach_mep_id_max_len         = 12;
    cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_parser_ether_ctl));

    /*ipe lookup*/
    sal_memset(&ipe_lookup_ctl, 0, sizeof(ipe_lookup_ctl_t));
    cmd = DRV_IOR(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));
    ipe_lookup_ctl.mpls_section_oam_use_port = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
    ipe_lookup_ctl.bfd_single_hop_ttl        = 255;
    ipe_lookup_ctl.ach_cc_use_label          = 1;
    ipe_lookup_ctl.ach_cv_use_label          = 1;
    ipe_lookup_ctl.mpls_bfd_use_label        = 0;
    /*set bit for ignore mpls bfd ttl*/
    CTC_BIT_SET(ipe_lookup_ctl.fcoe_lookup_ctl1, 6);

    cmd = DRV_IOW(IpeLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_ctl));

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
    /*OAM*/
    sal_memset(&oam_rx_proc_ether_ctl, 0, sizeof(oam_rx_proc_ether_ctl_t));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));
    oam_rx_proc_ether_ctl.portbased_section_oam = (P_COMMON_OAM_MASTER_GLB(lchip).tp_section_oam_based_l3if ? 0 : 1);
    oam_rx_proc_ether_ctl.tp_csf_to_cpu         = 0;
    if (P_COMMON_OAM_MASTER_GLB(lchip).tp_bfd_333ms)
    {
        oam_rx_proc_ether_ctl.tp_cv_to_cpu          = 0;
    }
    else
    {
        oam_rx_proc_ether_ctl.tp_cv_to_cpu          = 1;
    }
    oam_rx_proc_ether_ctl.tp_csf_clear              = P_COMMON_OAM_MASTER_GLB(lchip).tp_csf_clear;
    oam_rx_proc_ether_ctl.tp_csf_rdi                = P_COMMON_OAM_MASTER_GLB(lchip).tp_csf_rdi;
    oam_rx_proc_ether_ctl.tp_csf_fdi                = P_COMMON_OAM_MASTER_GLB(lchip).tp_csf_fdi;
    oam_rx_proc_ether_ctl.tp_csf_los                = P_COMMON_OAM_MASTER_GLB(lchip).tp_csf_los;
    oam_rx_proc_ether_ctl.rx_csf_while_cfg          = 14;
    oam_rx_proc_ether_ctl.csf_while_cfg             = 4;
    oam_rx_proc_ether_ctl.d_mis_con_while_cfg       = 14;
     /*oam_rx_proc_ether_ctl.max_bfd_interval          = 1023*1000;*/
    oam_rx_proc_ether_ctl.max_bfd_interval          = 0xFFFFFFFF;
    oam_rx_proc_ether_ctl.big_bfd_interval_to_cpu   = 1;
    oam_rx_proc_ether_ctl.mplstpdlm_outbandto_cpu   = 1;
    cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_rx_proc_ether_ctl));

    field = 4;
    cmd = DRV_IOW(OamUpdateCtl_t, OamUpdateCtl_CvWhileCfg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    sal_memset(&oam_ether_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_tx_ctl));
    oam_ether_tx_ctl.ach_header_l               = 0x1000;
    oam_ether_tx_ctl.bfd_multi_hop_udp_dst_port = 4784;
    oam_ether_tx_ctl.bfd_udp_dst_port           = 3784;
    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_tx_ctl));

    sal_memset(&oam_header_edit_ctl, 0, sizeof(oam_header_edit_ctl_t));
    cmd = DRV_IOR(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));
    oam_header_edit_ctl.mplsdlm_tlv_chk                 = 1;
    oam_header_edit_ctl.mplsdlm_ver_chk                 = 1;
    oam_header_edit_ctl.mplsdlm_query_code_chk          = 1;
    oam_header_edit_ctl.mplsdlm_query_code_max_value    = 2;
    oam_header_edit_ctl.mplsdlm_dflags_chk              = 1;
    cmd = DRV_IOW(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_header_edit_ctl));

    sal_memset(&ds_oam_excp, 0, sizeof(ds_oam_excp_t));
    field = SYS_QOS_CLASS_PRIORITY_MAX;
    cmd = DRV_IOW(DsOamExcp_t, DsOamExcp_Priority_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 30, cmd, &field));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_bfd_mpls_register_init(uint8 lchip)
{
    CTC_ERROR_RETURN(_sys_greatbelt_bfd_register_init(lchip));
    _sys_greatbelt_mpls_for_tp_oam(lchip, SYS_OAM_TP_MEP_TYPE);

    return CTC_E_NONE;
}


int32
_sys_greatbelt_bfd_ip_register_init(uint8 lchip)
{
    return _sys_greatbelt_bfd_register_init(lchip);
}


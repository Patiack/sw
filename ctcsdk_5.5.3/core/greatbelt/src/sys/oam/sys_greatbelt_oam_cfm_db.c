/**
 @file ctc_greatbelt_cfm_db.c

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
#include "ctc_linklist.h"
#include "ctc_hash.h"
#include "ctc_parser.h"

#include "sys_greatbelt_common.h"
#include "sys_greatbelt_chip.h"

#include "sys_greatbelt_ftm.h"

#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"

#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_db.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_oam_cfm_db.h"
#include "sys_greatbelt_oam_cfm.h"

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

sys_oam_chan_eth_t*
_sys_greatbelt_cfm_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm)
{
    sys_oam_chan_eth_t sys_oam_chan_eth;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_chan_eth, 0, sizeof(sys_oam_chan_eth_t));
    sys_oam_chan_eth.key.com.mep_type = p_key_parm->mep_type;
    sys_oam_chan_eth.key.gport        = p_key_parm->u.eth.gport;
    sys_oam_chan_eth.key.vlan_id      = p_key_parm->u.eth.vlan_id;
    sys_oam_chan_eth.key.use_fid      = CTC_FLAG_ISSET(p_key_parm->flag, CTC_OAM_KEY_FLAG_VPLS);
    sys_oam_chan_eth.key.link_oam     = CTC_FLAG_ISSET(p_key_parm->flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
    sys_oam_chan_eth.com.mep_type     = p_key_parm->mep_type;

    return (sys_oam_chan_eth_t*)_sys_greatbelt_oam_chan_lkup(lchip, &sys_oam_chan_eth.com);
}

int32
_sys_greatbelt_cfm_chan_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_chan_eth_t* p_chan_eth_para = (sys_oam_chan_eth_t*)p_oam_cmp->p_node_parm;
    sys_oam_chan_eth_t* p_chan_eth_db   = (sys_oam_chan_eth_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_chan_eth_para->key.com.mep_type != p_chan_eth_db->key.com.mep_type)
    {
        return 0;
    }

    if (p_chan_eth_para->key.use_fid != p_chan_eth_db->key.use_fid)
    {
        return 0;
    }

    if (p_chan_eth_para->key.link_oam != p_chan_eth_db->key.link_oam)
    {
        return 0;
    }

    if (p_chan_eth_para->key.gport != p_chan_eth_db->key.gport)
    {
        return 0;
    }

    if (p_chan_eth_para->key.vlan_id != p_chan_eth_db->key.vlan_id)
    {
        return 0;
    }

    return 1;
}

sys_oam_chan_eth_t*
_sys_greatbelt_cfm_chan_build_node(uint8 lchip, ctc_oam_lmep_t* p_chan_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;

    uint8 gchip = 0;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = (sys_oam_chan_eth_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_chan_eth_t));
    if (NULL != p_sys_chan_eth)
    {
        gchip = p_chan_param->u.y1731_lmep.master_gchip;
        /*sys_greatbelt_chip_is_local(lchip, gchip);*/

        sal_memset(p_sys_chan_eth, 0, sizeof(sys_oam_chan_eth_t));
        p_sys_chan_eth->com.mep_type = p_chan_param->key.mep_type;
        p_sys_chan_eth->com.master_chipid = gchip;
        p_sys_chan_eth->com.lchip = lchip;
        p_sys_chan_eth->com.link_oam = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        p_sys_chan_eth->key.com.mep_type = p_chan_param->key.mep_type;
        p_sys_chan_eth->key.gport = p_chan_param->key.u.eth.gport;
        p_sys_chan_eth->key.vlan_id = p_chan_param->key.u.eth.vlan_id;
        p_sys_chan_eth->key.link_oam = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        p_sys_chan_eth->key.use_fid = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_VPLS);
        p_sys_chan_eth->key.is_up = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_UP_MEP);

        if (CTC_FLAG_ISSET(p_chan_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
        {
            p_sys_chan_eth->lm_type     = p_chan_param->u.y1731_lmep.lm_type;
            p_sys_chan_eth->lm_cos_type = p_chan_param->u.y1731_lmep.lm_cos_type;
            p_sys_chan_eth->lm_cos      = p_chan_param->u.y1731_lmep.lm_cos;
        }
        else
        {
            p_sys_chan_eth->lm_type     = CTC_OAM_LM_TYPE_NONE;
        }
    }

    return p_sys_chan_eth;
}

int32
_sys_greatbelt_cfm_chan_free_node(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_chan_eth);
    p_sys_chan_eth = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_build_index(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    lookup_info_t lookup_info;
    lookup_result_t lookup_result;
    ds_eth_oam_hash_key_t eth_oam_key;
    uint8  use_fid_lkup = 0;
    uint16 gport        = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);
    sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
    sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

    gport = p_sys_chan_eth->key.gport;
    use_fid_lkup = p_sys_chan_eth->key.use_fid;

    if (!p_sys_chan_eth->key.link_oam)
    {
        /*hash key offset alloc*/
        sal_memset(&eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));

        SYS_GREATBELT_GPORT_TO_GPORT14(gport);
        eth_oam_key.global_src_port13 = ((gport >> 13) & 0x1);
        eth_oam_key.global_src_port   = (gport & 0x1FFF);
        eth_oam_key.global_src_port = gport;
        eth_oam_key.vlan_id = p_sys_chan_eth->key.vlan_id;
        eth_oam_key.is_fid = use_fid_lkup;
        eth_oam_key.hash_type = eth_oam_key.is_fid ? USERID_KEY_TYPE_ETHER_FID_OAM :
            USERID_KEY_TYPE_ETHER_VLAN_OAM;

        eth_oam_key.valid = 1;

        lookup_info.hash_type = eth_oam_key.is_fid ? USERID_HASH_TYPE_ETHER_FID_OAM :
            USERID_HASH_TYPE_ETHER_VLAN_OAM;
        lookup_info.hash_module = HASH_MODULE_USERID;
        lookup_info.p_ds_key = &eth_oam_key;

        lookup_info.chip_id = lchip;
        CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &lookup_result));
        if (lookup_result.free)
        {
            p_sys_chan_eth->key.com.key_index = lookup_result.key_index;
            p_sys_chan_eth->key.com.key_alloc = SYS_OAM_KEY_HASH;
            SYS_OAM_DBG_INFO("CHAN(Key): lchip->%d, index->0x%x\n", lchip, lookup_result.key_index);
        }
        else if (lookup_result.conflict)
        {
            uint32 offset = 0;
            CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 0, 1, &offset));
            p_sys_chan_eth->key.com.key_alloc = SYS_OAM_KEY_LTCAM;
            p_sys_chan_eth->key.com.key_index = offset;
            SYS_OAM_DBG_INFO("CHAN(Key LTCAM): Hash lookup confict, using tcam index = %d\n", offset);

        }

        /*chan offset alloc*/
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            if (0 == p_sys_chan_eth->key.service_queue_key.no_need_chan)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_build_index(lchip, &p_sys_chan_eth->com));
            }
        }
        else
        {
            p_sys_chan_eth->com.chan_index = p_sys_chan_eth->key.com.key_index;
            SYS_OAM_DBG_INFO("CHAN(chan LTCAM): lchip->%d, index->%d\n", lchip, p_sys_chan_eth->com.chan_index);
        }

        if ((CTC_OAM_LM_TYPE_NONE != p_sys_chan_eth->lm_type) && (!p_sys_chan_eth->lm_index_alloced))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_eth->com, FALSE));
            p_sys_chan_eth->lm_index_alloced = TRUE;
        }
    }
    else
    {
        if ((CTC_OAM_LM_TYPE_NONE != p_sys_chan_eth->link_lm_type) && (!p_sys_chan_eth->link_lm_index_alloced))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_eth->com, TRUE));
            p_sys_chan_eth->link_lm_index_alloced = TRUE;
        }
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_cfm_chan_free_index(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{


    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);

    if (!p_sys_chan_eth->key.link_oam)
    {
        /*chan offset free*/
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_free_index(lchip, &p_sys_chan_eth->com));
        }
        else
        {
            uint32 offset = p_sys_chan_eth->key.com.key_index;
            CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, offset));
        }
    }

    if (p_sys_chan_eth->link_lm_index_alloced)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_eth->com, TRUE));
        p_sys_chan_eth->link_lm_index_alloced = FALSE;
    }

    if (p_sys_chan_eth->lm_index_alloced)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_eth->com, FALSE));
        p_sys_chan_eth->lm_index_alloced = FALSE;
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_cfm_chan_free_index_for_service(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth, sys_oam_chan_eth_t* p_sys_chan_eth_service)
{
    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);
    CTC_PTR_VALID_CHECK(p_sys_chan_eth_service);

    /*chan offset free*/
    if (SYS_OAM_KEY_HASH == p_sys_chan_eth_service->key.com.key_alloc)
    {
        if (SYS_OAM_KEY_LTCAM == p_sys_chan_eth->key.com.key_alloc)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_free_index(lchip, &p_sys_chan_eth->com));
        }
    }
    else
    {
        uint32 offset = p_sys_chan_eth->key.com.key_index;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, offset));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_add_to_db(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_add_to_db(lchip, &p_sys_chan_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_del_from_db(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_chan_del_from_db(lchip, &p_sys_chan_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_add_to_asic(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 key_index  = 0;
    uint32 chan_index = 0;
    uint16 gport      = 0;
    uint8 is_fid      = 0;
    uint16 vlan_id    = 0;
    ds_eth_oam_hash_key_t ds_eth_oam_key;
    ds_eth_oam_chan_t     ds_eth_oam_chan;
    ds_eth_oam_tcam_chan_t ds_eth_oam_tcam_chan;
    void* p_chan = NULL;
    ds_fib_user_id80_key_t  data;
    ds_fib_user_id80_key_t  mask;
    tbl_entry_t         tcam_key;
    uint8 offset = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);


    sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
    ds_eth_oam_chan.oam_dest_chip_id = p_sys_chan_eth->com.master_chipid;

    sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));

    gport = p_sys_chan_eth->key.gport;
    SYS_GREATBELT_GPORT_TO_GPORT14(gport);
    is_fid = p_sys_chan_eth->key.use_fid;
    vlan_id = p_sys_chan_eth->key.vlan_id;

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            /*eth oam key write*/
            sal_memset(&ds_eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));

            ds_eth_oam_key.global_src_port13 = ((gport >> 13) & 0x1);
            ds_eth_oam_key.global_src_port   = (gport & 0x1FFF);
            ds_eth_oam_key.vlan_id = vlan_id;
            ds_eth_oam_key.is_fid = is_fid;
            ds_eth_oam_key.hash_type = is_fid ? USERID_KEY_TYPE_ETHER_FID_OAM :
                USERID_KEY_TYPE_ETHER_VLAN_OAM;

            ds_eth_oam_key.valid = 1;

            chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            p_chan = &ds_eth_oam_chan;

            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port13 = 0x%x\n", ds_eth_oam_key.global_src_port13);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port = 0x%x\n",   ds_eth_oam_key.global_src_port);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.vlan_id = 0x%x\n",           ds_eth_oam_key.vlan_id);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.is_fid = 0x%x\n",            ds_eth_oam_key.is_fid);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.hash_type = 0x%x\n",         ds_eth_oam_key.hash_type);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.valid = 0x%x\n",             ds_eth_oam_key.valid);
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
            data.global_src_port    = gport;
            data.ip_sa              = ((is_fid << 14) | (vlan_id & 0x3FFF));
            data.user_id_hash_type  = is_fid ? USERID_KEY_TYPE_ETHER_FID_OAM :
                USERID_KEY_TYPE_ETHER_VLAN_OAM;
            mask.table_id           = 0x3;
            mask.is_user_id         = 0x1;
            mask.is_label           = 0x1;
            mask.direction          = 0x1;
            mask.global_src_port    = 0x3FFF;
            mask.ip_sa              = 0xFFFFFFFF;
            mask.user_id_hash_type  = 0x1F;

            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            p_chan = &ds_eth_oam_tcam_chan;

            offset = sizeof(ds_eth_oam_tcam_chan_t) - sizeof(ds_eth_oam_chan);
            sal_memcpy((uint8*)p_chan + offset, &ds_eth_oam_chan, sizeof(ds_eth_oam_chan));
        }


        chan_index = p_sys_chan_eth->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, p_chan));

        key_index = p_sys_chan_eth->key.com.key_index;

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            ds_eth_oam_key.ad_index = chan_index;
            key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &ds_eth_oam_key));
            SYS_OAM_DBG_INFO("Write DsEthOamHashKey_t: index = 0x%x\n",  key_index);
            SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_2 = 0x%x\n",  (*((uint32*)&ds_eth_oam_key)));
            SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_1 = 0x%x\n",  (*((uint32*)&ds_eth_oam_key + 1)));
            SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_0 = 0x%x\n",  (*((uint32*)&ds_eth_oam_key + 2)));
            SYS_OAM_DBG_INFO("KEY Debug Bus: bucketPtr = 0x%x\n",  key_index >> 2);
        }
        else
        {
            key_cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &tcam_key));
        }

    }

    return CTC_E_NONE;

}


int32
_sys_greatbelt_cfm_chan_add_to_asic_for_service(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth, sys_oam_chan_eth_t* p_sys_chan_eth_service)
{
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 key_index  = 0;
    uint32 chan_index = 0;
    uint16 gport      = 0;
    uint8 is_fid      = 0;
    uint16 vlan_id    = 0;
    ds_eth_oam_hash_key_t ds_eth_oam_key;
    ds_eth_oam_chan_t     ds_eth_oam_chan;
    ds_eth_oam_tcam_chan_t ds_eth_oam_tcam_chan;
    ds_fib_user_id80_key_t  data;
    ds_fib_user_id80_key_t  mask;
    tbl_entry_t         tcam_key;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);
    CTC_PTR_VALID_CHECK(p_sys_chan_eth_service);


    sal_memset(&ds_eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));
    sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
    sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));
    sal_memset(&data, 0, sizeof(ds_fib_user_id80_key_t));
    sal_memset(&mask, 0, sizeof(ds_fib_user_id80_key_t));
    sal_memset(&tcam_key, 0, sizeof(tbl_entry_t));

    gport = p_sys_chan_eth_service->key.gport;
    SYS_GREATBELT_GPORT_TO_GPORT14(gport);
    is_fid = p_sys_chan_eth_service->key.use_fid;
    vlan_id = p_sys_chan_eth_service->key.vlan_id;
    key_index = p_sys_chan_eth_service->key.com.key_index;

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth_service->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth_service->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (SYS_OAM_KEY_HASH == p_sys_chan_eth_service->key.com.key_alloc)
    {
        /*eth oam key write*/
        if (SYS_OAM_KEY_LTCAM == p_sys_chan_eth->key.com.key_alloc)/*external port is tcam*/
        {
            ds_eth_oam_chan.oam_dest_chip_id = p_sys_chan_eth->com.master_chipid;
            if (p_sys_chan_eth->key.is_up)
            {
                ds_eth_oam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
            }
            else
            {
                ds_eth_oam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
            }
            ds_eth_oam_chan.lm_type0_0              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 0);
            ds_eth_oam_chan.lm_type1_1              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 1);
            ds_eth_oam_chan.lm_cos_type0_0          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
            ds_eth_oam_chan.lm_cos_type1_1          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
            ds_eth_oam_chan.lm_cos                  = p_sys_chan_eth->lm_cos;

            chan_index = p_sys_chan_eth_service->com.chan_index;
            chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_eth_oam_chan));
            SYS_OAM_DBG_INFO("Write HASH chan: chan index = 0x%x\n",             chan_index);
        }

        ds_eth_oam_key.lm_bitmap           = ((p_sys_chan_eth->lm_bitmap) >> 1);
        ds_eth_oam_key.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
        ds_eth_oam_key.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
        ds_eth_oam_key.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
        ds_eth_oam_key.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
        ds_eth_oam_key.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);
        ds_eth_oam_key.ad_index = p_sys_chan_eth_service->com.chan_index;
        ds_eth_oam_key.global_src_port13 = ((gport >> 13) & 0x1);
        ds_eth_oam_key.global_src_port   = (gport & 0x1FFF);
        ds_eth_oam_key.vlan_id = vlan_id;
        ds_eth_oam_key.is_fid = is_fid;
        ds_eth_oam_key.hash_type = is_fid ? USERID_KEY_TYPE_ETHER_FID_OAM :
        USERID_KEY_TYPE_ETHER_VLAN_OAM;
        ds_eth_oam_key.valid = 1;
        key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &ds_eth_oam_key));
        SYS_OAM_DBG_INFO("Write HASH KEY: key index = 0x%x\n",             key_index);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.ad_index = 0x%x\n",             ds_eth_oam_key.ad_index);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port13 = 0x%x\n", ds_eth_oam_key.global_src_port13);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port = 0x%x\n",   ds_eth_oam_key.global_src_port);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.vlan_id = 0x%x\n",           ds_eth_oam_key.vlan_id);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.is_fid = 0x%x\n",            ds_eth_oam_key.is_fid);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.hash_type = 0x%x\n",         ds_eth_oam_key.hash_type);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.valid = 0x%x\n",             ds_eth_oam_key.valid);
    }
    else
    {
        if (p_sys_chan_eth->key.is_up)
        {
            ds_eth_oam_tcam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
            ds_eth_oam_tcam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
        }
        else
        {
            ds_eth_oam_tcam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
            ds_eth_oam_tcam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
        }
        ds_eth_oam_tcam_chan.lm_bitmap           = ((p_sys_chan_eth->lm_bitmap) >> 1);
        ds_eth_oam_tcam_chan.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
        ds_eth_oam_tcam_chan.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
        ds_eth_oam_tcam_chan.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
        ds_eth_oam_tcam_chan.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
        ds_eth_oam_tcam_chan.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);
        ds_eth_oam_tcam_chan.lm_type            = p_sys_chan_eth->lm_type;
        ds_eth_oam_tcam_chan.lm_cos_type0_0     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
        ds_eth_oam_tcam_chan.lm_cos_type1_1     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
        ds_eth_oam_tcam_chan.lm_cos             = p_sys_chan_eth->lm_cos;

        chan_index = p_sys_chan_eth_service->com.chan_index;
        chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, chan_cmd, &ds_eth_oam_chan));

        tcam_key.data_entry = (uint32*)&data;
        tcam_key.mask_entry = (uint32*)&mask;
        data.table_id           = 0;
        data.is_user_id         = 1;
        data.is_label           = 0;
        data.direction          = 0;
        data.global_src_port    = gport;
        data.ip_sa              = ((is_fid << 14) | (vlan_id & 0x3FFF));
        data.user_id_hash_type  = is_fid ? USERID_KEY_TYPE_ETHER_FID_OAM :
        USERID_KEY_TYPE_ETHER_VLAN_OAM;
        mask.table_id           = 0x3;
        mask.is_user_id         = 0x1;
        mask.is_label           = 0x1;
        mask.direction          = 0x1;
        mask.global_src_port    = 0x3FFF;
        mask.ip_sa              = 0xFFFFFFFF;
        mask.user_id_hash_type  = 0x1F;

        key_cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, key_cmd, &tcam_key));
    }

    return CTC_E_NONE;

}


int32
_sys_greatbelt_cfm_chan_del_from_asic(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    ds_eth_oam_hash_key_t ds_eth_oam_key;
    ds_eth_oam_chan_t ds_eth_oam_chan;
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 index      = 0;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_eth);

    if (p_sys_chan_eth->key.link_oam)
    {
        return CTC_E_NONE;
    }

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        /*eth oam key delete*/
        sal_memset(&ds_eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));
        ds_eth_oam_key.valid = 0;
        key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);

        /*eth oam ad chan delete*/
        sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
        chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);

        index = p_sys_chan_eth->key.com.key_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, key_cmd, &ds_eth_oam_key));

        index = p_sys_chan_eth->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.chan_index, chan_cmd, &ds_eth_oam_chan));
    }
    else
    {
        index = p_sys_chan_eth->key.com.key_index;
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, DsFibUserId80Key_t, index));
    }


    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_del_from_asic_for_service(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth, sys_oam_chan_eth_t* p_sys_chan_eth_service)

{
    ds_eth_oam_hash_key_t ds_eth_oam_key;
    ds_eth_oam_chan_t ds_eth_oam_chan;
    uint32 key_cmd    = 0;
    uint32 chan_cmd   = 0;
    uint32 index      = 0;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_eth);
    CTC_PTR_VALID_CHECK(p_sys_chan_eth_service);

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (SYS_OAM_KEY_HASH == p_sys_chan_eth_service->key.com.key_alloc)
    {
        /*eth oam key delete*/
        sal_memset(&ds_eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));
        ds_eth_oam_key.valid = 0;
        key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
        index = p_sys_chan_eth_service->key.com.key_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, key_cmd, &ds_eth_oam_key));
        SYS_OAM_DBG_INFO("Delete HASH KEY: key index = 0x%x\n",             index);

        if (SYS_OAM_KEY_LTCAM == p_sys_chan_eth->key.com.key_alloc)
        {
            /*eth oam ad chan delete*/
            sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
            chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            index = p_sys_chan_eth_service->com.chan_index;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.chan_index, chan_cmd, &ds_eth_oam_chan));
            SYS_OAM_DBG_INFO("Delete HASH channel : channel index = 0x%x\n",             index);
        }
    }
    else
    {
        index = p_sys_chan_eth_service->key.com.key_index;
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, DsFibUserId80Key_t, index));
    }

    return CTC_E_NONE;
}



int32
_sys_greatbelt_cfm_chan_update(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth, bool is_add)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;

    ds_eth_oam_chan_t ds_eth_oam_chan;
    ds_eth_oam_hash_key_t     ds_eth_oam_hash_key;
    ds_eth_oam_tcam_chan_t ds_eth_oam_tcam_chan;
    void* p_chan = NULL;
    uint32 mep_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8  md_level    = 0;
    uint8  level       = 0;
    uint32 index       = 0;
    uint32 chan_cmd    = 0;
    uint32 key_cmd     = 0;
    uint32 srcport_cmd = 0;
    uint16 gport       = 0;
    uint8  lport       = 0;
    uint8* p_mep_bitmap  = 0;
    uint8* p_lm_bitmap = 0;
    uint8  lm_en       = 0;
    uint8        master_chipid = 0;

    ds_src_port_t ds_src_port;

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port_t));

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = (sys_oam_chan_eth_t*)p_sys_lmep_eth->com.p_sys_chan;
    master_chipid = p_sys_chan_eth->com.master_chipid;
    md_level    = p_sys_lmep_eth->md_level;
    p_lm_bitmap = &(p_sys_chan_eth->lm_bitmap);

    if (p_sys_lmep_eth->is_up)
    {
        p_mep_bitmap = &(p_sys_chan_eth->up_mep_bitmap);
    }
    else
    {
        p_mep_bitmap = &(p_sys_chan_eth->down_mep_bitmap);
    }

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN) && is_add)
    {
        SET_BIT((*p_lm_bitmap), md_level);
        lm_en   = 1;
    }
    else
    {
        CLEAR_BIT((*p_lm_bitmap), md_level);
        lm_en   = 0;
    }

    if (is_add)
    {
        SET_BIT((*p_mep_bitmap), md_level);
        p_sys_chan_eth->mep_index[md_level] = p_sys_lmep_eth->com.lmep_index;
    }
    else
    {
        CLEAR_BIT((*p_mep_bitmap), md_level);
        p_sys_chan_eth->mep_index[md_level] = 0x1FFF; /*invalid */
    }


    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(ds_eth_oam_hash_key_t));

            p_chan = &ds_eth_oam_chan;
            chan_cmd = DRV_IOR(DsEthOamChan_t, DRV_ENTRY_FLAG);

            key_cmd = DRV_IOR(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
        }
        else
        {
            sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));
            p_chan = &ds_eth_oam_tcam_chan;
            chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.chan_index, chan_cmd, p_chan));
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->key.com.key_index, key_cmd, &ds_eth_oam_hash_key));
        }


        for (level = 1; level <= SYS_OAM_MAX_MD_LEVEL; level++)
        {
            if ((IS_BIT_SET(p_sys_chan_eth->down_mep_bitmap, level)) ||
                (IS_BIT_SET(p_sys_chan_eth->up_mep_bitmap, level)))
            {
                mep_index[index++] = p_sys_chan_eth->mep_index[level];
            }
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            if (p_sys_lmep_eth->is_up)
            {
                ds_eth_oam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
            }
            else
            {
                ds_eth_oam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
            }
            ds_eth_oam_chan.oam_dest_chip_id = master_chipid;
            ds_eth_oam_chan.mep_index0      = mep_index[0];
            ds_eth_oam_chan.mep_index1      = mep_index[1];
            ds_eth_oam_chan.mep_index2      = mep_index[2];
            ds_eth_oam_chan.mep_index3_2_0  = mep_index[3] & 0x7;
            ds_eth_oam_chan.mep_index3_12_3 = (mep_index[3] >> 3) & 0x3FF;

            if (lm_en)
            {
                ds_eth_oam_hash_key.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                ds_eth_oam_hash_key.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
                ds_eth_oam_hash_key.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
                ds_eth_oam_hash_key.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
                ds_eth_oam_hash_key.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
                ds_eth_oam_hash_key.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

                ds_eth_oam_chan.lm_type0_0              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 0);
                ds_eth_oam_chan.lm_type1_1              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 1);
                ds_eth_oam_chan.lm_cos_type0_0          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
                ds_eth_oam_chan.lm_cos_type1_1          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
                ds_eth_oam_chan.lm_cos                  = p_sys_chan_eth->lm_cos;
            }
            else
            {
                ds_eth_oam_hash_key.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                if (0 == ds_eth_oam_hash_key.lm_bitmap)
                {
                    ds_eth_oam_chan.lm_type0_0 = 0;
                    ds_eth_oam_chan.lm_type1_1 = 0;
                }
            }
        }
        else
        {
            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            if (p_sys_lmep_eth->is_up)
            {
                ds_eth_oam_tcam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_tcam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
            }
            else
            {
                ds_eth_oam_tcam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_tcam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
            }
            ds_eth_oam_tcam_chan.oam_dest_chip_id = master_chipid;
            ds_eth_oam_tcam_chan.mep_index0      = mep_index[0];
            ds_eth_oam_tcam_chan.mep_index1      = mep_index[1];
            ds_eth_oam_tcam_chan.mep_index2      = mep_index[2];
            ds_eth_oam_tcam_chan.mep_index3_2_0  = mep_index[3] & 0x7;
            ds_eth_oam_tcam_chan.mep_index3_12_3 = (mep_index[3] >> 3) & 0x3FF;

            if (lm_en)
            {
                ds_eth_oam_tcam_chan.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                ds_eth_oam_tcam_chan.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
                ds_eth_oam_tcam_chan.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

                ds_eth_oam_tcam_chan.lm_type            = p_sys_chan_eth->lm_type;
                ds_eth_oam_tcam_chan.lm_cos_type0_0     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
                ds_eth_oam_tcam_chan.lm_cos_type1_1     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
                ds_eth_oam_tcam_chan.lm_cos             = p_sys_chan_eth->lm_cos;
            }
            else
            {
                ds_eth_oam_tcam_chan.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                if (0 == ds_eth_oam_tcam_chan.lm_bitmap)
                {
                    ds_eth_oam_tcam_chan.lm_type = CTC_OAM_LM_TYPE_NONE;
                }
            }
        }


        index = p_sys_chan_eth->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->key.com.key_index, key_cmd, &ds_eth_oam_hash_key));
        }

    }
    else
    {
        /*update mepindex in DsSrcPort*/
        gport = p_sys_chan_eth->key.gport;
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

        srcport_cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, srcport_cmd, &ds_src_port));

        ds_src_port.link_oam_mep_index  = p_sys_chan_eth->mep_index[0];
        ds_src_port.link_oam_en         = (is_add ? 1 : 0);

        if (lm_en)
        {
            ds_src_port.link_lm_type        = p_sys_chan_eth->link_lm_type;
            ds_src_port.link_lm_cos_type    = p_sys_chan_eth->link_lm_cos_type;
            ds_src_port.link_lm_cos         = p_sys_chan_eth->link_lm_cos;
            ds_src_port.link_lm_index_base  = p_sys_chan_eth->com.link_lm_index_base;
        }
        else
        {
            ds_src_port.link_lm_type        = CTC_OAM_LM_TYPE_NONE;
        }

        srcport_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, srcport_cmd, &ds_src_port));

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_update_reomte(uint8 lchip, ctc_oam_lmep_t* p_lmep, sys_oam_chan_eth_t* p_sys_chan_eth, bool is_add)
{
    ds_eth_oam_chan_t ds_eth_oam_chan;
    ds_eth_oam_hash_key_t     ds_eth_oam_hash_key;
    ds_eth_oam_tcam_chan_t ds_eth_oam_tcam_chan;
    void* p_chan = NULL;
    uint32 mep_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8  md_level    = 0;
    uint8  level       = 0;
    uint32 index       = 0;
    uint32 chan_cmd    = 0;
    uint32 key_cmd     = 0;
    uint32 srcport_cmd = 0;
    uint16 gport       = 0;
    uint8  lport       = 0;
    uint8* p_mep_bitmap  = 0;
    uint8* p_lm_bitmap = 0;
    uint8  lm_en       = 0;

    ds_src_port_t ds_src_port;

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port_t));

    SYS_OAM_DBG_FUNC();

    md_level    = p_lmep->key.u.eth.md_level;
    p_lm_bitmap = &(p_sys_chan_eth->lm_bitmap);

    if (CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_UP_MEP))
    {
        p_mep_bitmap = &(p_sys_chan_eth->up_mep_bitmap);
    }
    else
    {
        p_mep_bitmap = &(p_sys_chan_eth->down_mep_bitmap);
    }

    if (CTC_FLAG_ISSET(p_lmep->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN) && is_add)
    {
        SET_BIT((*p_lm_bitmap), md_level);
        lm_en   = 1;
    }
    else
    {
        CLEAR_BIT((*p_lm_bitmap), md_level);
        lm_en   = 0;
    }

    if (is_add)
    {
        SET_BIT((*p_mep_bitmap), md_level);
        p_sys_chan_eth->mep_index[md_level] = p_lmep->lmep_index;
    }
    else
    {
        CLEAR_BIT((*p_mep_bitmap), md_level);
        p_sys_chan_eth->mep_index[md_level] = 0x1FFF; /*invalid */
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_chan, 0, sizeof(ds_eth_oam_chan_t));
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(ds_eth_oam_hash_key_t));

            p_chan = &ds_eth_oam_chan;
            chan_cmd = DRV_IOR(DsEthOamChan_t, DRV_ENTRY_FLAG);

            key_cmd = DRV_IOR(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
        }
        else
        {
            sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));
            p_chan = &ds_eth_oam_tcam_chan;
            chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.chan_index, chan_cmd, p_chan));
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->key.com.key_index, key_cmd, &ds_eth_oam_hash_key));
        }


        for (level = 1; level <= SYS_OAM_MAX_MD_LEVEL; level++)
        {
            if ((IS_BIT_SET(p_sys_chan_eth->down_mep_bitmap, level)) ||
                (IS_BIT_SET(p_sys_chan_eth->up_mep_bitmap, level)))
            {
                mep_index[index++] = p_sys_chan_eth->mep_index[level];
            }
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            chan_cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            key_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            if (CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_UP_MEP))
            {
                ds_eth_oam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
            }
            else
            {
                ds_eth_oam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
            }

            ds_eth_oam_chan.mep_index0      = mep_index[0];
            ds_eth_oam_chan.mep_index1      = mep_index[1];
            ds_eth_oam_chan.mep_index2      = mep_index[2];
            ds_eth_oam_chan.mep_index3_2_0  = mep_index[3] & 0x7;
            ds_eth_oam_chan.mep_index3_12_3 = (mep_index[3] >> 3) & 0x3FF;

            if (lm_en)
            {
                ds_eth_oam_hash_key.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                ds_eth_oam_hash_key.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
                ds_eth_oam_hash_key.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
                ds_eth_oam_hash_key.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
                ds_eth_oam_hash_key.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
                ds_eth_oam_hash_key.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

                ds_eth_oam_chan.lm_type0_0              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 0);
                ds_eth_oam_chan.lm_type1_1              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 1);
                ds_eth_oam_chan.lm_cos_type0_0          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
                ds_eth_oam_chan.lm_cos_type1_1          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
                ds_eth_oam_chan.lm_cos                  = p_sys_chan_eth->lm_cos;
            }
            else
            {
                ds_eth_oam_hash_key.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                if (0 == ds_eth_oam_hash_key.lm_bitmap)
                {
                    ds_eth_oam_chan.lm_type0_0 = 0;
                    ds_eth_oam_chan.lm_type1_1 = 0;
                }
            }
        }
        else
        {
            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            if (CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_UP_MEP))
            {
                ds_eth_oam_tcam_chan.mep_up_bitmap_high = ((p_sys_chan_eth->up_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_tcam_chan.mep_up_bitmap_low = (p_sys_chan_eth->up_mep_bitmap >> 1) & 0xF;
            }
            else
            {
                ds_eth_oam_tcam_chan.mep_down_bitmap_high = ((p_sys_chan_eth->down_mep_bitmap >> 1) >> 4) & 0x7;
                ds_eth_oam_tcam_chan.mep_down_bitmap_low = (p_sys_chan_eth->down_mep_bitmap >> 1) & 0xF;
            }

            ds_eth_oam_tcam_chan.mep_index0      = mep_index[0];
            ds_eth_oam_tcam_chan.mep_index1      = mep_index[1];
            ds_eth_oam_tcam_chan.mep_index2      = mep_index[2];
            ds_eth_oam_tcam_chan.mep_index3_2_0  = mep_index[3] & 0x7;
            ds_eth_oam_tcam_chan.mep_index3_12_3 = (mep_index[3] >> 3) & 0x3FF;

            if (lm_en)
            {
                ds_eth_oam_tcam_chan.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                ds_eth_oam_tcam_chan.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
                ds_eth_oam_tcam_chan.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
                ds_eth_oam_tcam_chan.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

                ds_eth_oam_tcam_chan.lm_type            = p_sys_chan_eth->lm_type;
                ds_eth_oam_tcam_chan.lm_cos_type0_0     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
                ds_eth_oam_tcam_chan.lm_cos_type1_1     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
                ds_eth_oam_tcam_chan.lm_cos             = p_sys_chan_eth->lm_cos;
            }
            else
            {
                ds_eth_oam_tcam_chan.lm_bitmap           = ((*p_lm_bitmap) >> 1);
                if (0 == ds_eth_oam_tcam_chan.lm_bitmap)
                {
                    ds_eth_oam_tcam_chan.lm_type = CTC_OAM_LM_TYPE_NONE;
                }
            }
        }


        index = p_sys_chan_eth->com.chan_index;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_chan));

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->key.com.key_index, key_cmd, &ds_eth_oam_hash_key));
        }

    }
    else
    {
        /*update mepindex in DsSrcPort*/
        gport = p_sys_chan_eth->key.gport;
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

        srcport_cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, srcport_cmd, &ds_src_port));

        ds_src_port.link_oam_mep_index  = p_sys_chan_eth->mep_index[0];
        ds_src_port.link_oam_en         = (is_add ? 1 : 0);

        if (lm_en)
        {
            ds_src_port.link_lm_type        = p_sys_chan_eth->link_lm_type;
            ds_src_port.link_lm_cos_type    = p_sys_chan_eth->link_lm_cos_type;
            ds_src_port.link_lm_cos         = p_sys_chan_eth->link_lm_cos;
            ds_src_port.link_lm_index_base  = p_sys_chan_eth->com.link_lm_index_base;
        }
        else
        {
            ds_src_port.link_lm_type        = CTC_OAM_LM_TYPE_NONE;
        }

        srcport_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, srcport_cmd, &ds_src_port));

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_chan_update_mip(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth, uint8 md_level, bool is_add)
{
    ds_eth_oam_hash_key_t   ds_eth_oam_key;
    ds_eth_oam_tcam_chan_t  ds_eth_oam_tcam_chan;

    void* p_key = NULL;
    uint32 index       = 0;
    uint32 chan_cmd    = 0;

    uint8* p_mip_bitmap  = 0;

    SYS_OAM_DBG_FUNC();


    p_mip_bitmap = &(p_sys_chan_eth->mip_bitmap);

    if (is_add)
    {
        SET_BIT((*p_mip_bitmap), md_level);
    }
    else
    {
        CLEAR_BIT((*p_mip_bitmap), md_level);
    }

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_key, 0, sizeof(ds_eth_oam_hash_key_t));
            p_key = &ds_eth_oam_key;
            chan_cmd = DRV_IOR(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
        }
        else
        {
            sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));
            p_key = &ds_eth_oam_tcam_chan;
            chan_cmd  = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        }


        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            index = p_sys_chan_eth->key.com.key_index;
        }
        else
        {
            index = p_sys_chan_eth->com.chan_index;
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_key));


        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            chan_cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            ds_eth_oam_key.mip_bitmap = (p_sys_chan_eth->mip_bitmap >> 1) & 0x7F;
        }
        else
        {
            chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            ds_eth_oam_tcam_chan.mip_bitmap = (p_sys_chan_eth->mip_bitmap >> 1) & 0x7F;
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            index = p_sys_chan_eth->key.com.key_index;
        }
        else
        {
            index = p_sys_chan_eth->com.chan_index;
        }

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, chan_cmd, p_key));

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_chan_resv(uint8 lchip)
{

    uint32 key_offset = 0;
    uint32 key_cmd  = 0;
    uint32 chan_cmd = 0;

    tbl_entry_t tcam_key;
    ds_fib_user_id80_key_t data;
    ds_fib_user_id80_key_t mask;

    ds_eth_oam_tcam_chan_t ds_eth_oam_tcam_chan;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 1, 1, &key_offset));

    sal_memset(&data, 0, sizeof(ds_fib_user_id80_key_t));
    sal_memset(&mask, 0, sizeof(ds_fib_user_id80_key_t));
    sal_memset(&tcam_key, 0, sizeof(tbl_entry_t));

    tcam_key.data_entry = (uint32*)&data;
    tcam_key.mask_entry = (uint32*)&mask;

    data.table_id           = 0;
    data.is_user_id         = 1;
    data.is_label           = 0;
    data.user_id_hash_type  = USERID_KEY_TYPE_ETHER_VLAN_OAM;

    data.global_src_port    = 0;
    data.ip_sa              = 0;
    data.direction          = 0;


    mask.table_id           = 0x3;
    mask.is_user_id         = 0x1;
    mask.is_label           = 0x1;
    mask.user_id_hash_type  = 0x1F;

    mask.direction          = 0x0;
    mask.global_src_port    = 0x0;
    mask.ip_sa              = 0x0;

    sal_memset(&ds_eth_oam_tcam_chan, 0, sizeof(ds_eth_oam_tcam_chan_t));


    /*key*/
    key_cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_offset, key_cmd, &tcam_key));
    /*ad channel*/
    chan_cmd  = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_offset, chan_cmd, &ds_eth_oam_tcam_chan));


    return CTC_E_NONE;
}

#define LMEP "Function Begin"

sys_oam_lmep_y1731_t*
_sys_greatbelt_cfm_lmep_lkup(uint8 lchip, uint8 md_level, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    sys_oam_lmep_y1731_t sys_oam_lmep_eth;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_lmep_eth, 0, sizeof(sys_oam_lmep_y1731_t));
    sys_oam_lmep_eth.md_level = md_level;

    return (sys_oam_lmep_y1731_t*)_sys_greatbelt_oam_lmep_lkup(lchip, &p_sys_chan_eth->com, &sys_oam_lmep_eth.com);
}

int32
_sys_greatbelt_cfm_lmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_lmep_y1731_t* p_lmep_eth_para = (sys_oam_lmep_y1731_t*)p_oam_cmp->p_node_parm;
    sys_oam_lmep_y1731_t* p_lmep_eth_db   = (sys_oam_lmep_y1731_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_lmep_eth_para->md_level == p_lmep_eth_db->md_level)
    {
        return 1;
    }

    return 0;
}

sys_oam_lmep_y1731_t*
_sys_greatbelt_cfm_lmep_build_node(uint8 lchip, ctc_oam_lmep_t* p_lmep_param)
{
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_lmep_eth = (sys_oam_lmep_y1731_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_lmep_y1731_t));
    if (NULL != p_sys_lmep_eth)
    {
        sal_memset(p_sys_lmep_eth, 0, sizeof(sys_oam_lmep_y1731_t));
        p_sys_lmep_eth->is_up          = CTC_FLAG_ISSET(p_lmep_param->key.flag, CTC_OAM_KEY_FLAG_UP_MEP);
        p_sys_lmep_eth->md_level       = p_lmep_param->key.u.eth.md_level;
        p_sys_lmep_eth->flag           = p_lmep_param->u.y1731_lmep.flag;

        p_sys_lmep_eth->com.mep_on_cpu = CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU);
        p_sys_lmep_eth->tx_csf_type    = p_lmep_param->u.y1731_lmep.tx_csf_type;
        p_sys_lmep_eth->com.lmep_index = p_lmep_param->lmep_index;
    }

    return p_sys_lmep_eth;
}

int32
_sys_greatbelt_cfm_lmep_free_node(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_lmep_eth);
    p_sys_lmep_eth = NULL;
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_build_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_build_index(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_free_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_free_index(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_add_to_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_add_to_db(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_del_from_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_db(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_build_eth_data(uint8 lchip, ctc_oam_y1731_lmep_t* p_eth_lmep,
                                       sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    p_sys_lmep_eth->mep_id                      = p_eth_lmep->mep_id;
     /*p_sys_lmep_eth->md_level                    = p_eth_lmep->md_level;*/
    p_sys_lmep_eth->ccm_interval                = p_eth_lmep->ccm_interval;
    p_sys_lmep_eth->share_mac                   = CTC_FLAG_ISSET(p_eth_lmep->flag, CTC_OAM_Y1731_LMEP_FLAG_SHARE_MAC);
    p_sys_lmep_eth->ccm_en                      = 0;
    p_sys_lmep_eth->tx_cos_exp                  = p_eth_lmep->tx_cos_exp;

    SYS_OAM_DBG_FUNC();

    switch (p_eth_lmep->tpid_index)
    {
    case CTC_PARSER_L2_TPID_SVLAN_TPID_0:
        p_sys_lmep_eth->tpid_type = 0;
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_1:
        p_sys_lmep_eth->tpid_type = 1;
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_2:
        p_sys_lmep_eth->tpid_type = 2;
        break;

    case CTC_PARSER_L2_TPID_SVLAN_TPID_3:
        p_sys_lmep_eth->tpid_type = 3;
        break;

    default:
        break;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_add_to_asic(uint8 lchip, ctc_oam_lmep_t* p_lmep_param,
                                    sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    ctc_oam_y1731_lmep_t* p_eth_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);
    CTC_PTR_VALID_CHECK(p_sys_lmep_eth);

    p_eth_lmep = &(p_lmep_param->u.y1731_lmep);
    if (CTC_FLAG_ISSET(p_eth_lmep->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_cfm_lmep_build_eth_data(lchip, p_eth_lmep, p_sys_lmep_eth));
    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_add_eth_to_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_del_from_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_lmep_update_asic(uint8 lchip, ctc_oam_update_t* p_lmep_param,
                                    sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    ctc_oam_update_t* p_eth_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);
    CTC_PTR_VALID_CHECK(p_sys_lmep_eth);

    p_eth_lmep = p_lmep_param;
    p_sys_lmep_eth->update_type = p_eth_lmep->update_type;


    switch (p_sys_lmep_eth->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_eth_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_EN);
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        p_sys_lmep_eth->ccm_en = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        p_sys_lmep_eth->tx_cos_exp = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        p_sys_lmep_eth->present_rdi = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        p_sys_lmep_eth->dm_en = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        if (1 == p_eth_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_CCM_SEQ_NUM_EN);
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_P2P_TX_USE_UC_DA:
        p_sys_lmep_eth->ccm_p2p_use_uc_da = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        p_sys_lmep_eth->sf_state  = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        if (1 == p_eth_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN);
        }

        p_sys_lmep_eth->tx_csf_en = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        p_sys_lmep_eth->tx_csf_type = p_eth_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA:
        p_sys_lmep_eth->tx_csf_use_uc_da = p_eth_lmep->update_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_update_lmep_port_status(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    ctc_slistnode_t* slist_lmep_node = NULL;
    ctc_slist_t* p_lmep_list = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }
    else
    {
        p_lmep_list = p_sys_chan_eth->com.lmep_list;
        if (NULL == p_lmep_list)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }
        else
        {
            CTC_SLIST_LOOP(p_lmep_list, slist_lmep_node)
            {
                p_sys_lmep = _ctc_container_of(slist_lmep_node, sys_oam_lmep_com_t, head);
                p_sys_lmep_eth = (sys_oam_lmep_y1731_t*)p_sys_lmep;

                if (p_sys_lmep_eth->com.mep_on_cpu)
                {
                    continue;
                }

                p_sys_lmep_eth->update_type = p_lmep_param->update_type;

                switch (p_lmep_param->update_value)
                {
                case CTC_OAM_ETH_PS_BLOCKED:
                    p_sys_lmep_eth->port_status = 1;
                    break;

                case CTC_OAM_ETH_PS_UP:
                    p_sys_lmep_eth->port_status = 2;
                    break;

                default:
                    p_sys_lmep_eth->port_status = 0;
                }

                CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_update_lmep_if_status(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    ctc_oam_update_t* p_eth_lmep_param = p_lmep_param;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint16 gport = 0;
    uint8 intf_status = 0;
    uint8 gchip     = 0;
    uint8 lport = 0;
    uint8 mp_port_id = 0;

    gport = p_eth_lmep_param->key.u.eth.gport;
    intf_status = p_eth_lmep_param->update_value;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        mp_port_id = lport + 128;
    }
    else
    {
        if (FALSE == sys_greatbelt_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport)))
        {
            return CTC_E_NONE;
        }

        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        mp_port_id = lport;
    }

    switch (intf_status)
    {
    case CTC_OAM_ETH_INTF_IS_UP:
        field_value = 1;
        break;

    case CTC_OAM_ETH_INTF_IS_DOWN:
        field_value = 2;
        break;

    case CTC_OAM_ETH_INTF_IS_TESTING:
        field_value = 3;
        break;

    case CTC_OAM_ETH_INTF_IS_UNKNOWN:
        field_value = 4;
        break;

    case CTC_OAM_ETH_INTF_IS_DORMANT:
        field_value = 5;
        break;

    case CTC_OAM_ETH_INTF_IS_NOT_PRESENT:
        field_value = 6;
        break;

    case CTC_OAM_ETH_INTF_IS_LOWER_LAYER_DOWN:
        field_value = 7;
        break;

    default:
        break;
    }

    cmd = DRV_IOW(DsPortProperty_t, DsPortProperty_IfStatus_f);

    if (CTC_IS_LINKAGG_PORT(gport))
    {

        p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
        if (NULL == p_sys_chan_eth)
        {
            return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        }

        sys_greatbelt_get_gchip_id(lchip, &gchip);
        if (gchip == p_sys_chan_eth->com.master_chipid)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mp_port_id, cmd, &field_value));
        }
    }
    else
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mp_port_id, cmd, &field_value));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_update_lmep_lm(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{

    uint32 srcport_cmd      = 0;
    uint32 destport_cmd     = 0;
    uint16 gport            = 0;
    uint8  lport            = 0;
    uint32 field_value      = 0;
    uint32 key_index        = 0;
    uint32 chan_index       = 0;
    uint32 tcam_chan_index  = 0;
    uint32 cmd              = 0;

    ctc_oam_update_t* p_lmep = (ctc_oam_update_t*)p_lmep_param;

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    uint8            md_level = 0;

    ds_eth_oam_hash_key_t   ds_eth_oam_hash_key;
    ds_eth_oam_chan_t       ds_eth_oam_chan;
    ds_eth_oam_tcam_chan_t  ds_eth_oam_tcam_chan;

    SYS_OAM_DBG_FUNC();

    /*1. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }

    md_level = p_lmep->key.u.eth.md_level;

    if (CTC_IS_BIT_SET(p_sys_chan_eth->lm_bitmap, md_level))
    {
        if((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE == p_lmep->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE == p_lmep->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS == p_lmep->update_type))
        {
            return CTC_E_OAM_INVALID_CFG_LM;
        }
    }

    /*2. lookup lmep and check exist*/
    if (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN == p_lmep->update_type)
    {
        p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
        if (NULL == p_sys_lmep_eth)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }

        if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
        {
            return CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
        }
    }


    /*3. lmep db & asic update*/
    switch (p_lmep->update_type)
    {
    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN:
        if (1 == p_lmep->update_value)
        {
            CTC_SET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);
            srcport_cmd     = DRV_IOW(DsSrcPort_t,  DsSrcPort_LinkLmIndexBase_f);
            destport_cmd    = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmIndexBase_f);
            destport_cmd    = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmIndexBase_f);

            CTC_BIT_SET(p_sys_chan_eth->lm_bitmap, md_level);
            if (!p_sys_chan_eth->link_lm_index_alloced && p_sys_chan_eth->key.link_oam)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_eth->com, TRUE));
                p_sys_chan_eth->link_lm_index_alloced = TRUE;
            }

            if (!p_sys_chan_eth->lm_index_alloced && !p_sys_chan_eth->key.link_oam)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_build_index(lchip, &p_sys_chan_eth->com, FALSE));
                p_sys_chan_eth->lm_index_alloced = TRUE;
            }
            field_value = p_sys_chan_eth->com.link_lm_index_base;
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);
            srcport_cmd     = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmType_f);
            destport_cmd    = DRV_IOW(DsDestPort_t,DsDestPort_LinkLmType_f);
            field_value     = CTC_OAM_LM_TYPE_NONE;

            CTC_BIT_UNSET(p_sys_chan_eth->lm_bitmap, md_level);

            if (0 == (p_sys_chan_eth->lm_bitmap >> 1))
            {
                p_sys_chan_eth->lm_type = CTC_OAM_LM_TYPE_NONE;
                if(p_sys_chan_eth->lm_index_alloced)
                {
                    CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_eth->com, FALSE));
                    p_sys_chan_eth->lm_index_alloced = FALSE;
                }
            }

            if (!CTC_IS_BIT_SET(p_sys_chan_eth->lm_bitmap, 0))
            {
                if(p_sys_chan_eth->link_lm_index_alloced)
                {
                    p_sys_chan_eth->link_lm_type = CTC_OAM_LM_TYPE_NONE;
                    CTC_ERROR_RETURN(_sys_greatbelt_oam_lm_free_index(lchip, &p_sys_chan_eth->com, TRUE));
                    p_sys_chan_eth->link_lm_index_alloced = FALSE;
                }
            }
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE:
        p_sys_chan_eth->key.link_oam ?
        (p_sys_chan_eth->link_lm_type = p_lmep->update_value) : (p_sys_chan_eth->lm_type = p_lmep->update_value);

        srcport_cmd     = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmType_f);
        destport_cmd    = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmType_f);
        field_value     = p_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE:

        p_sys_chan_eth->key.link_oam ?
        (p_sys_chan_eth->link_lm_cos_type = p_lmep->update_value) : (p_sys_chan_eth->lm_cos_type = p_lmep->update_value);

        srcport_cmd     = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmCosType_f);
        destport_cmd    = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmCosType_f);
        field_value     = p_lmep->update_value;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
        p_sys_chan_eth->key.link_oam ?
        (p_sys_chan_eth->link_lm_cos = p_lmep->update_value) : (p_sys_chan_eth->lm_cos = p_lmep->update_value);

        srcport_cmd     = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmCos_f);
        destport_cmd    = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmCos_f);
        field_value     = p_lmep->update_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        key_index        = 0;
        chan_index       = 0;
        tcam_chan_index  = 0;

        sal_memset(&ds_eth_oam_hash_key,    0, sizeof(ds_eth_oam_hash_key_t));
        sal_memset(&ds_eth_oam_chan,        0, sizeof(ds_eth_oam_chan_t));
        sal_memset(&ds_eth_oam_tcam_chan,   0, sizeof(ds_eth_oam_tcam_chan_t));

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            key_index   = p_sys_chan_eth->key.com.key_index;
            chan_index  = p_sys_chan_eth->com.chan_index;

            cmd = DRV_IOR(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, cmd, &ds_eth_oam_hash_key));

            cmd = DRV_IOR(DsEthOamChan_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, cmd, &ds_eth_oam_chan));
        }
        else
        {
            tcam_chan_index = p_sys_chan_eth->com.chan_index;
            cmd = DRV_IOR(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tcam_chan_index, cmd, &ds_eth_oam_tcam_chan));
        }


        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            ds_eth_oam_hash_key.lm_bitmap = (p_sys_chan_eth->lm_bitmap >> 1);
            ds_eth_oam_hash_key.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
            ds_eth_oam_hash_key.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
            ds_eth_oam_hash_key.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
            ds_eth_oam_hash_key.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
            ds_eth_oam_hash_key.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

            ds_eth_oam_chan.lm_type0_0              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 0);
            ds_eth_oam_chan.lm_type1_1              = CTC_IS_BIT_SET(p_sys_chan_eth->lm_type, 1);
            ds_eth_oam_chan.lm_cos_type0_0          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
            ds_eth_oam_chan.lm_cos_type1_1          = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
            ds_eth_oam_chan.lm_cos                  = p_sys_chan_eth->lm_cos;
        }
        else
        {
            ds_eth_oam_tcam_chan.lm_bitmap = (p_sys_chan_eth->lm_bitmap >> 1);

            ds_eth_oam_tcam_chan.lm_index_base1_0    = (p_sys_chan_eth->com.lm_index_base & 0x3);
            ds_eth_oam_tcam_chan.lm_index_base4_2    = ((p_sys_chan_eth->com.lm_index_base >> 2) & 0x7);
            ds_eth_oam_tcam_chan.lm_index_base6_5    = ((p_sys_chan_eth->com.lm_index_base >> 5) & 0x3);
            ds_eth_oam_tcam_chan.lm_index_base8_7    = ((p_sys_chan_eth->com.lm_index_base >> 7) & 0x3);
            ds_eth_oam_tcam_chan.lm_index_base13_9   = ((p_sys_chan_eth->com.lm_index_base >> 9) & 0x1F);

            ds_eth_oam_tcam_chan.lm_type            = p_sys_chan_eth->lm_type;
            ds_eth_oam_tcam_chan.lm_cos_type0_0     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 0);
            ds_eth_oam_tcam_chan.lm_cos_type1_1     = CTC_IS_BIT_SET(p_sys_chan_eth->lm_cos_type, 1);
            ds_eth_oam_tcam_chan.lm_cos             = p_sys_chan_eth->lm_cos;
        }


        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            key_index   = p_sys_chan_eth->key.com.key_index;
            chan_index  = p_sys_chan_eth->com.chan_index;
             /*SYS_OAM_DBG_DUMP("key index is %d ,chan index is  %d \n", key_index, chan_index);*/
            cmd = DRV_IOW(DsEthOamHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, key_index, cmd, &ds_eth_oam_hash_key));

            cmd = DRV_IOW(DsEthOamChan_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_index, cmd, &ds_eth_oam_chan));
        }
        else
        {
            tcam_chan_index = p_sys_chan_eth->com.chan_index;
            cmd = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tcam_chan_index, cmd, &ds_eth_oam_tcam_chan));
        }


    }
    else
    {
        /*update mepindex in DsSrcPort*/
        gport = p_sys_chan_eth->key.gport;
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, srcport_cmd, &field_value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, destport_cmd, &field_value));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_update_lmep_trpt(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    ctc_slistnode_t* slist_lmep_node = NULL;
    ctc_slist_t* p_lmep_list = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }
    else
    {
        p_lmep_list = p_sys_chan_eth->com.lmep_list;
        if (NULL == p_lmep_list)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }
        else
        {
            CTC_SLIST_LOOP(p_lmep_list, slist_lmep_node)
            {
                p_sys_lmep = _ctc_container_of(slist_lmep_node, sys_oam_lmep_com_t, head);
                p_sys_lmep_eth = (sys_oam_lmep_y1731_t*)p_sys_lmep;

                if (p_sys_lmep_eth->com.mep_on_cpu)
                {
                    continue;
                }

                p_sys_lmep_eth->update_type = p_lmep_param->update_type;

                if (0xff != p_lmep_param->update_value)
                {
                    /* enable session */
                    if (p_lmep_param->update_value >= CTC_OAM_TRPT_SESSION_NUM)
                    {
                        return CTC_E_EXCEED_MAX_SIZE;
                    }

                    p_sys_lmep_eth->trpt_en = 1;
                    p_sys_lmep_eth->trpt_session_id = p_lmep_param->update_value;
                }
                else
                {
                    p_sys_lmep_eth->trpt_en = 0;
                    p_sys_lmep_eth->trpt_session_id = 0;
                }

                CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_update_master_gchip(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }
    else
    {
        if (p_sys_chan_eth->com.link_oam)
        {
            return CTC_E_INVALID_CONFIG;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_lmep_update_master_chip((sys_oam_chan_com_t*)p_sys_chan_eth, &(p_lmep_param->update_value)));

    return CTC_E_NONE;
}

#define RMEP "Function Begin"

sys_oam_rmep_y1731_t*
_sys_greatbelt_cfm_rmep_lkup(uint8 lchip, uint32 rmep_id, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    sys_oam_rmep_y1731_t sys_oam_rmep_eth;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_rmep_eth, 0, sizeof(sys_oam_rmep_y1731_t));
    sys_oam_rmep_eth.rmep_id = rmep_id;

    return (sys_oam_rmep_y1731_t*)_sys_greatbelt_oam_rmep_lkup(lchip, &p_sys_lmep_eth->com, &sys_oam_rmep_eth.com);
}

int32
_sys_greatbelt_cfm_rmep_lkup_cmp(uint8 lchip, void* p_cmp)
{
    sys_oam_cmp_t* p_oam_cmp = (sys_oam_cmp_t*)p_cmp;
    sys_oam_rmep_y1731_t* p_rmep_eth_para = (sys_oam_rmep_y1731_t*)p_oam_cmp->p_node_parm;
    sys_oam_rmep_y1731_t* p_rmep_eth_db   = (sys_oam_rmep_y1731_t*)p_oam_cmp->p_node_db;

    SYS_OAM_DBG_FUNC();

    if (p_rmep_eth_para->rmep_id == p_rmep_eth_db->rmep_id)
    {
        return 1;
    }

    return 0;
}

sys_oam_rmep_y1731_t*
_sys_greatbelt_cfm_rmep_build_node(uint8 lchip, ctc_oam_rmep_t* p_rmep_param)
{
    sys_oam_rmep_y1731_t* p_sys_rmep_eth = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_rmep_eth = (sys_oam_rmep_y1731_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_rmep_y1731_t));
    if (NULL != p_sys_rmep_eth)
    {
        sal_memset(p_sys_rmep_eth, 0, sizeof(sys_oam_rmep_y1731_t));
        p_sys_rmep_eth->rmep_id = p_rmep_param->u.y1731_rmep.rmep_id;
        /*p_sys_rmep_eth->is_p2p_mode = 0; */
        p_sys_rmep_eth->com.rmep_index = p_rmep_param->rmep_index;
    }

    return p_sys_rmep_eth;
}

int32
_sys_greatbelt_cfm_rmep_free_node(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_rmep_eth);
    p_sys_rmep_eth = NULL;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_build_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    int32 ret = 0;
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_build_index(lchip, &p_sys_rmep_eth->com));

    if (!p_sys_rmep_eth->is_p2p_mode)
    {
        sys_oam_lmep_com_t* p_sys_lmep = NULL;
        ds_eth_oam_rmep_hash_key_t ds_eth_oam_rmep_hash_key;
        lookup_info_t lookup_info;
        lookup_result_t lookup_result;
        sal_memset(&lookup_info, 0, sizeof(lookup_info_t));
        sal_memset(&lookup_result, 0, sizeof(lookup_result_t));

        p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;
        /*HASH*/
        sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));
        ds_eth_oam_rmep_hash_key.valid = 1;
        ds_eth_oam_rmep_hash_key.mep_index = p_sys_lmep->lmep_index;
        ds_eth_oam_rmep_hash_key.rmep_id = p_sys_rmep_eth->rmep_id;
        ds_eth_oam_rmep_hash_key.hash_type = USERID_KEY_TYPE_ETHER_RMEP;
        lookup_info.hash_module = HASH_MODULE_USERID;
        lookup_info.hash_type = USERID_HASH_TYPE_ETHER_RMEP;
        lookup_info.p_ds_key = &ds_eth_oam_rmep_hash_key;
        lookup_info.chip_id = p_sys_lmep->lchip;
        CTC_ERROR_RETURN(DRV_HASH_LOOKUP(&lookup_info, &lookup_result));
        if (lookup_result.free)
        {
            p_sys_rmep_eth->key_index = lookup_result.key_index;
            p_sys_rmep_eth->key_alloc = SYS_OAM_KEY_HASH;
            SYS_OAM_DBG_INFO("Build Hash DsEthOamRmepHashKey_t index:0x%x \n", lookup_result.key_index);
        }
        else
        {
            uint32 offset = 0;
            ret = sys_greatbelt_ftm_alloc_table_offset(lchip, DsFibUserId80Key_t, 0, 1, &offset);
            if (ret < 0)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_free_index(lchip, &p_sys_rmep_eth->com)); /*roll back rmep_index*/
                return ret;
            }

            p_sys_rmep_eth->key_index  = offset;
            p_sys_rmep_eth->key_alloc = SYS_OAM_KEY_LTCAM;
            SYS_OAM_DBG_INFO("Rmep(Key LTCAM): Hash lookup confict, using tcam index = %d\n", offset);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_free_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    /*chan offset free*/
    if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_free_index(lchip, &p_sys_rmep_eth->com));
    }
    else
    {
        uint32 offset = p_sys_rmep_eth->key_index;
        CTC_ERROR_RETURN(sys_greatbelt_ftm_free_table_offset(lchip, DsFibUserId80Key_t, 0, 1, offset));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_add_to_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_add_to_db(lchip, &p_sys_rmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_del_from_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_db(lchip, &p_sys_rmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_build_eth_data(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
                                       sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{

    ctc_oam_y1731_rmep_t* p_eth_rmep = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    SYS_OAM_DBG_FUNC();

    p_eth_rmep = (ctc_oam_y1731_rmep_t*)&p_rmep_param->u.y1731_rmep;
    p_sys_lmep_eth = (sys_oam_lmep_y1731_t*)p_sys_rmep_eth->com.p_sys_lmep;

    p_sys_rmep_eth->flag                       = p_eth_rmep->flag;
    p_sys_rmep_eth->rmep_id                    = p_eth_rmep->rmep_id;
    p_sys_rmep_eth->md_level                   = p_rmep_param->key.u.eth.md_level;
    p_sys_rmep_eth->is_p2p_mode                = CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE);
    sal_memcpy(p_sys_rmep_eth->rmep_mac, p_eth_rmep->rmep_mac, sizeof(mac_addr_t));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_add_to_asic(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
                                    sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    uint32 cmd        = 0;
    uint32 index      = 0;
    uint16 rmep_id    = 0;
    uint16 mep_index  = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep_eth);

    CTC_ERROR_RETURN(_sys_greatbelt_cfm_rmep_build_eth_data(lchip, p_rmep_param, p_sys_rmep_eth));
    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_add_eth_to_asic(lchip, &p_sys_rmep_eth->com));

    if (p_sys_rmep_eth->is_p2p_mode)
    {
        return CTC_E_NONE;
    }

    p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;
    rmep_id = p_sys_rmep_eth->rmep_id;
    mep_index = p_sys_lmep->lmep_index;

    index = p_sys_rmep_eth->key_index;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        return CTC_E_NONE;
    }

    if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
    {
        ds_eth_oam_rmep_hash_key_t ds_eth_oam_rmep_hash_key;
        sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));
        ds_eth_oam_rmep_hash_key.mep_index = mep_index;
        ds_eth_oam_rmep_hash_key.rmep_id = rmep_id;
        ds_eth_oam_rmep_hash_key.hash_type = USERID_KEY_TYPE_ETHER_RMEP;
        ds_eth_oam_rmep_hash_key.ad_index = p_sys_rmep_eth->com.rmep_index;
        ds_eth_oam_rmep_hash_key.valid = 1;

        cmd = DRV_IOW(DsEthOamRmepHashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(p_sys_lmep->lchip, index, cmd, &ds_eth_oam_rmep_hash_key));
        SYS_OAM_DBG_INFO("Write DsEthOamRmepHashKey_t index:0x%x \n", index);
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_2 = 0x%x\n",  (*((uint32*)&ds_eth_oam_rmep_hash_key)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_1 = 0x%x\n",  (*((uint32*)&ds_eth_oam_rmep_hash_key + 1)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: hashKey_0 = 0x%x\n",  (*((uint32*)&ds_eth_oam_rmep_hash_key + 2)));
        SYS_OAM_DBG_INFO("KEY Debug Bus: bucketPtr = 0x%x\n",  index >> 2);

    }
    else
    {
        ds_fib_user_id80_key_t  data;
        ds_fib_user_id80_key_t  mask;
        tbl_entry_t         tcam_key;
        ds_eth_rmep_chan_conflict_tcam_t tcam_chan;
        ds_6word_hash_key_t ltcam_ad;

        sal_memset(&tcam_chan, 0, sizeof(ds_eth_rmep_chan_conflict_tcam_t));
        sal_memset(&ltcam_ad, 0, sizeof(ds_6word_hash_key_t));
        tcam_chan.ad_index = p_sys_rmep_eth->com.rmep_index;
        sal_memcpy((uint8*)&ltcam_ad + sizeof(ds_3word_hash_key_t), &tcam_chan, sizeof(ds_eth_rmep_chan_conflict_tcam_t));
        cmd = DRV_IOW(LpmTcamAdMem_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(p_sys_lmep->lchip, index, cmd, &ltcam_ad));

        sal_memset(&data, 0, sizeof(ds_fib_user_id80_key_t));
        sal_memset(&mask, 0, sizeof(ds_fib_user_id80_key_t));
        sal_memset(&tcam_key, 0, sizeof(tbl_entry_t));
        tcam_key.data_entry = (uint32*)&data;
        tcam_key.mask_entry = (uint32*)&mask;

        data.table_id           = 0;
        data.is_user_id         = 1;
        data.is_label           = 0;
        data.direction          = 0;
        data.global_src_port    = 0;
        data.ip_sa              = ((rmep_id << 14) | (mep_index & 0x3FFF));
        data.user_id_hash_type  = USERID_KEY_TYPE_ETHER_RMEP;
        mask.table_id           = 0x3;
        mask.is_user_id         = 0x1;
        mask.is_label           = 0x1;
        mask.direction          = 0x1;
        mask.global_src_port    = 0x3FFF;
        mask.ip_sa              = 0xFFFFFFFF;
        mask.user_id_hash_type  = 0x1F;

        cmd = DRV_IOW(DsFibUserId80Key_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(p_sys_lmep->lchip, index, cmd, &tcam_key));
        SYS_OAM_DBG_INFO("Write DsFibUserId80Key_t index:%d \n", index);

    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    uint32 index      = 0;
    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_del_from_asic(lchip, &p_sys_rmep_eth->com));

    index = p_sys_rmep_eth->key_index;
    p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_rmep_eth->is_p2p_mode)
    {
        if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
        {
            uint32 cmd        = 0;
            ds_eth_oam_rmep_hash_key_t ds_eth_oam_rmep_hash_key;
            sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));
            ds_eth_oam_rmep_hash_key.mep_index  = 0;
            ds_eth_oam_rmep_hash_key.rmep_id    = 0;
            ds_eth_oam_rmep_hash_key.hash_type  = USERID_KEY_TYPE_ETHER_RMEP;
            ds_eth_oam_rmep_hash_key.ad_index   = 0;
            ds_eth_oam_rmep_hash_key.valid      = 0;
            cmd = DRV_IOW(DsEthOamRmepHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(p_sys_lmep->lchip, index, cmd, &ds_eth_oam_rmep_hash_key));
        }
        else
        {
            CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(p_sys_lmep->lchip, DsFibUserId80Key_t, index));
        }
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_rmep_update_asic(uint8 lchip, ctc_oam_update_t* p_rmep_param,
                                    sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    ctc_oam_update_t* p_eth_rmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep_eth);

    p_eth_rmep = p_rmep_param;
    p_sys_rmep_eth->update_type = p_eth_rmep->update_type;

    switch (p_sys_rmep_eth->update_type)
    {
    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_MEP_EN:
        if (1 == p_eth_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_MEP_EN);
        }

        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_SEQ_FAIL_CLEAR:

        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        if (1 == p_eth_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_CCM_SEQ_NUM_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_CCM_SEQ_NUM_EN);
        }

        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMEP_SF_STATE:
        p_sys_rmep_eth->sf_state    = p_eth_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
        if (1 == p_eth_rmep->update_value)
        {
            CTC_SET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_rmep_eth->flag, CTC_OAM_Y1731_RMEP_FLAG_CSF_EN);
        }

        p_sys_rmep_eth->csf_en = p_eth_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
        p_sys_rmep_eth->d_csf    = p_eth_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_RMAC_MIS:
        p_sys_rmep_eth->src_mac_state = p_eth_rmep->update_value;
        break;

    case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_PORT_INTF_MIS:
        p_sys_rmep_eth->port_intf_state = p_eth_rmep->update_value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_oam_rmep_update_eth_asic(lchip, &p_sys_rmep_eth->com));

    return CTC_E_NONE;
}

#define COMMON_SET "Function Begin"

int32
_sys_greatbelt_cfm_oam_set_port_tunnel_en(uint8 lchip, uint16 gport, uint8 enable)
{
    uint8 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* call the sys interface from port module */

    field_value = enable;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_OamTunnelEn_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    return ret;
}

int32
_sys_greatbelt_cfm_oam_set_port_edge_en(uint8 lchip, uint16 gport, uint8 enable)
{
    uint8 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    field_value = enable;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherOamEdgePort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    field_value = enable;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherOamEdgePort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    return ret;
}

int32
_sys_greatbelt_cfm_oam_set_port_oam_en(uint8 lchip, uint16 gport, uint8 dir, uint8 enable)
{
    uint8 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* call the sys interface from port module */
    switch (dir)
    {
    case CTC_INGRESS:
        field_value = enable;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        break;

    case CTC_EGRESS:
        field_value = enable;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        break;

    case CTC_BOTH_DIRECTION:
        field_value = enable;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherOamValid_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        break;

    default:
        break;
    }

    return ret;
}

int32
_sys_greatbelt_cfm_oam_set_relay_all_to_cpu_en(uint8 lchip, uint8 enable)
{
    uint32 cmd           = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    field_value = enable;
    cmd = DRV_IOW(OamHeaderAdjustCtl_t, OamHeaderAdjustCtl_RelayAllToCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_greatbelt_cfm_oam_set_tx_vlan_tpid(uint8 lchip, uint8 tpid_index, uint16 tpid)
{
    uint32 cmd           = 0;
    oam_ether_tx_ctl_t eth_tx_ctl;

    SYS_OAM_DBG_FUNC();

    if (tpid_index > 1)
    {
        return CTC_E_OAM_INVALID_MEP_TPID_INDEX;
    }


    sal_memset(&eth_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));


    switch (tpid_index)
    {
        case 0:
            eth_tx_ctl.tpid0      = tpid;
            break;

        case 1:
            eth_tx_ctl.tpid1      = tpid;
            break;

        case 2:
            eth_tx_ctl.tpid2      = tpid;
            break;

        case 3:
            eth_tx_ctl.tpid3      = tpid;
            break;

        default:
            break;
    }

    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_rx_vlan_tpid(uint8 lchip, uint8 tpid_index, uint16 tpid)
{
    uint32 cmd           = 0;
    oam_parser_ether_ctl_t par_eth_ctl;

    SYS_OAM_DBG_FUNC();

    if (tpid_index > 1)
    {
        return CTC_E_OAM_INVALID_MEP_TPID_INDEX;
    }


    sal_memset(&par_eth_ctl, 0, sizeof(oam_parser_ether_ctl_t));
    cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));

    switch (tpid_index)
    {
        case 0:
            par_eth_ctl.cvlan_tpid  = tpid;
            break;

        case 1:
            par_eth_ctl.svlan_tpid0 = tpid;
            break;

        case 2:
            par_eth_ctl.svlan_tpid1 = tpid;
            break;

        case 3:
            par_eth_ctl.svlan_tpid2 = tpid;
            break;

        default:
            break;
    }

    cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_send_id(uint8 lchip, ctc_oam_eth_senderid_t* p_sender_id)
{
    uint32 cmd      = 0;

    oam_ether_send_id_t oam_ether_send_id;

    SYS_OAM_DBG_FUNC();


    sal_memset(&oam_ether_send_id, 0, sizeof(oam_ether_send_id));
    cmd = DRV_IOW(OamEtherSendId_t, DRV_ENTRY_FLAG);

    oam_ether_send_id.send_id_length     = p_sender_id->sender_id_len;

    oam_ether_send_id.send_id_byte0to2   = ((p_sender_id->sender_id[0] & 0xFF) << 16) |
    ((p_sender_id->sender_id[1] & 0xFF) << 8) |
    ((p_sender_id->sender_id[2] & 0xFF) << 0);

    oam_ether_send_id.send_id_byte3to6   = ((p_sender_id->sender_id[3] & 0xFF) << 24) |
    ((p_sender_id->sender_id[4] & 0xFF) << 16) |
    ((p_sender_id->sender_id[5] & 0xFF) << 8) |
    ((p_sender_id->sender_id[6] & 0xFF) << 0);

    oam_ether_send_id.send_id_byte7to10  = ((p_sender_id->sender_id[7] & 0xFF) << 24) |
    ((p_sender_id->sender_id[8] & 0xFF) << 16) |
    ((p_sender_id->sender_id[9] & 0xFF) << 8) |
    ((p_sender_id->sender_id[10] & 0xFF) << 0);

    oam_ether_send_id.send_id_byte11to14 = ((p_sender_id->sender_id[11] & 0xFF) << 24) |
    ((p_sender_id->sender_id[12] & 0xFF) << 16) |
    ((p_sender_id->sender_id[13] & 0xFF) << 8) |
    ((p_sender_id->sender_id[14] & 0xFF) << 0);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_send_id));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_big_ccm_interval(uint8 lchip, uint8 big_interval)
{
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(big_interval, 7);

    field_value = big_interval;

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_MinIntervalToCpu_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_port_lm_en(uint8 lchip, uint16 gport, uint8 enable)
{
    uint8 lport     = 0;
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    field_value = enable;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherLmValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_value));

    field_value = enable;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherLmValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_value));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_bridge_mac(uint8 lchip, mac_addr_t* bridge_mac)
{
    uint32 cmd      = 0;
    mac_addr_t  mac;

    oam_rx_proc_ether_ctl_t rx_ether_ctl;
    oam_ether_tx_ctl_t eth_tx_ctl;

    SYS_OAM_DBG_FUNC();

    sal_memcpy(&mac, bridge_mac, sizeof(mac_addr_t));

    sal_memset(&rx_ether_ctl, 0, sizeof(oam_rx_proc_ether_ctl_t));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    sal_memset(&eth_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));

    rx_ether_ctl.bridge_mac_high    = (mac[0] << 8) |
    (mac[1] << 0);

    rx_ether_ctl.bridge_mac_low     = (mac[2] << 24) |
    (mac[3] << 16) |
    (mac[4] << 8) |
    (mac[5] << 0);

    eth_tx_ctl.tx_bridge_mac_high    = (mac[0] << 8) |
    (mac[1] << 0);

    eth_tx_ctl.tx_bridge_mac_low     = (mac[2] << 24) |
    (mac[3] << 16) |
    (mac[4] << 8) |
    (mac[5] << 0);

    cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));


    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_lbm_process_by_asic(uint8 lchip, uint8 enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_LbmProcByCpu_f);

    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_lbr_sa_use_lbm_da(uint8 lchip, uint8 enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_LbrSaUsingLbmDa_f);

    field_value = enable;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_set_lbr_sa_use_bridge_mac(uint8 lchip, uint8 enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_LbrSaType_f);

    field_value = enable;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}


int32
_sys_greatebelt_cfm_oam_set_ach_channel_type(uint8 lchip, uint16 value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    field_val = value;
    cmd = DRV_IOW(ParserLayer4AchCtl_t, ParserLayer4AchCtl_AchY1731Type_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_AchY1731ChanType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(OamEtherTxCtl_t, OamEtherTxCtl_AchY1731ChanType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
_sys_greatebelt_cfm_oam_get_ach_channel_type(uint8 lchip, uint16* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4AchCtl_t, ParserLayer4AchCtl_AchY1731Type_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *value = field_val;

    return CTC_E_NONE;
}

int32
 _sys_greatbelt_cfm_set_if_status(uint8 lchip, uint32 gport, uint8 if_status)
{
    uint16  lport       = 0;
    uint32 field = 0;

    uint32 cmd = 0;

    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Invalid global port \n");
        return CTC_E_INVALID_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    field = if_status & 0xFF;
    cmd = DRV_IOW(DsPortProperty_t, DsPortProperty_IfStatus_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));

    return CTC_E_NONE;


}



#define COMMON_GET "Function Begin"

int32
_sys_greatbelt_cfm_oam_get_port_oam_en(uint8 lchip, uint16 gport, uint8 dir, uint8* enable)
{
    uint8 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* call the sys interface from port module */
    switch (dir)
    {
    case CTC_INGRESS:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_EtherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        *enable = field_value;
        break;

    case CTC_EGRESS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_EtherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        *enable = field_value;
        break;

    case CTC_BOTH_DIRECTION:
        break;

    default:
        break;
    }

    return ret;
}

int32
_sys_greatbelt_cfm_oam_get_port_tunnel_en(uint8 lchip, uint16 gport, uint8* enable)
{
    uint8 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* call the sys interface from port module */

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_OamTunnelEn_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
    *enable = field_value;

    return ret;
}

int32
_sys_greatbelt_cfm_oam_get_big_ccm_interval(uint8 lchip, uint8* big_interval)
{
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();


    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_MinIntervalToCpu_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *big_interval = field_value;


    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_get_port_lm_en(uint8 lchip, uint16 gport, uint8* enable)
{
    uint8 lport     = 0;
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_EtherLmValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_get_lbm_process_by_asic(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_LbmProcByCpu_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;


    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_get_lbr_sa_use_lbm_da(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamHeaderEditCtl_t, OamHeaderEditCtl_LbrSaUsingLbmDa_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_oam_get_if_status(uint8 lchip, uint16 gport, uint8* if_status)
{

    uint16  lport       = 0;
    uint32 field = 0;
    uint32 cmd = 0;

    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Invalid global port \n");
        return CTC_E_INVALID_PORT;
    }

    cmd = DRV_IOR(DsPortProperty_t, DsPortProperty_IfStatus_f);
    DRV_IOCTL(lchip, lport, cmd, &field);
    *if_status = field & 0xFF;

    return CTC_E_NONE;

}




int32
_sys_greatbelt_cfm_register_init(uint8 lchip)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 gchip_id = 0;
    uint8 gchip_id0 = 0;
    uint32 nexthop_ptr_bridge = 0;
    oam_parser_layer2_protocol_cam_t par_l2_cam;
    oam_parser_layer2_protocol_cam_valid_t par_l2_cam_valid;
    oam_parser_ether_ctl_t par_eth_ctl;
    oam_header_adjust_ctl_t adjust_ctl;
    oam_rx_proc_ether_ctl_t rx_ether_ctl;
    uint8 rdi_index       = 0;
    oam_header_edit_ctl_t hdr_edit_ctl;
    oam_ether_tx_ctl_t eth_tx_ctl;
    oam_ether_tx_mac_t oam_ether_tx_mac;
    uint16 mp_port_id = 0;
    ds_port_property_t ds_port_property;

    oam_update_ctl_t oam_update_ctl;
    ipe_oam_ctl_t ipe_oam_ctl;
    epe_oam_ctl_t epe_oam_ctl;

    mac_addr_t bridge_mac;

    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip_id));

    /*Parser*/
    field_val = 1;
    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_EthOamTypeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    sal_memset(&par_l2_cam, 0, sizeof(oam_parser_layer2_protocol_cam_t));
    par_l2_cam.cam_value0         = 0x408902;
    par_l2_cam.cam_mask0          = 0x40FFFF;
    par_l2_cam.additional_offset0 = 0;
    par_l2_cam.cam_layer3_type0   = 9;
    cmd = DRV_IOW(OamParserLayer2ProtocolCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_l2_cam));

    sal_memset(&par_l2_cam_valid, 0, sizeof(oam_parser_layer2_protocol_cam_valid_t));
    par_l2_cam_valid.layer2_cam_entry_valid = 1;
    cmd = DRV_IOW(OamParserLayer2ProtocolCamValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_l2_cam_valid));

    sal_memset(&par_eth_ctl, 0, sizeof(oam_parser_ether_ctl_t));
    cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));
    par_eth_ctl.min_mep_id                     = 0;
    par_eth_ctl.max_mep_id                     = 8191;
    par_eth_ctl.cfm_pdu_mac_da_md_lvl_check_en = 1;
    par_eth_ctl.tlv_length_check_en            = 1;
    par_eth_ctl.allow_non_zero_oui             = 1;
    par_eth_ctl.cfm_pdu_min_length             = 0;
    par_eth_ctl.cfm_pdu_max_length             = 132;
    par_eth_ctl.first_tlv_offset_chk           = 70;
    par_eth_ctl.md_name_length_chk             = 43;
    par_eth_ctl.ma_id_length_chk               = 48;
    par_eth_ctl.min_if_status_tlv_value        = 0;
    par_eth_ctl.max_if_status_tlv_value        = 7;
    par_eth_ctl.min_port_status_tlv_value      = 0;
    par_eth_ctl.max_port_status_tlv_value      = 3;
    par_eth_ctl.ignore_eth_oam_version         = 0;
    par_eth_ctl.invalid_ccm_interval_check_en  = 1;
    par_eth_ctl.max_length_field               = 1536;

    par_eth_ctl.svlan_tpid0                    = 0x8100;
    par_eth_ctl.svlan_tpid1                    = 0x8100;
    par_eth_ctl.svlan_tpid2                    = 0x9100;
    par_eth_ctl.svlan_tpid3                    = 0x88a8;

    cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));

    /*Lookup*/
    field_val = 1;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_OamLookupUserVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_OamLookupUserVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Ipe/Epe process*/
    field_val = 1;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_OamTunnelCheckType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_LinkOamDiscardEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_UntaggedServiceOamEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = gchip_id;              /*global chipid*/
    cmd = DRV_IOW(IpeOamCtl_t, IpeOamCtl_LinkOamDestChipId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_OamBypassPolicingDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_OamBypassStormCtlDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 0xFFFFFFFF;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_OamDiscardBitmap31_0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 0xFFFFFFFF;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_OamDiscardBitmap63_32_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_OamIgnorePayloadOperation_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

#if 0
    field_val = 1;
    cmd = DRV_IOW(EpeOamCtl_t, EpeOamCtl_DiscardL2MatchTxOam_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(EpeOamCtl_t, EpeOamCtl_DiscardLocalOam_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 0;
    cmd = DRV_IOW(EpeOamCtl_t, EpeOamCtl_ExceptionLocalOam_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
#endif

    /*OamEngine Header Adjust*/
    sal_memset(&adjust_ctl, 0, sizeof(oam_header_adjust_ctl_t));
    cmd = DRV_IOR(OamHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));
    adjust_ctl.oam_pdu_bypass_oam_engine = 0;
    adjust_ctl.relay_all_to_cpu          = 0;
    cmd = DRV_IOW(OamHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));

    /*OamEngine Rx Process*/
    sal_memset(&rx_ether_ctl, 0, sizeof(oam_rx_proc_ether_ctl_t));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));
    rx_ether_ctl.min_interval_to_cpu        = 7;
    rx_ether_ctl.lbm_macda_check_en         = 1;
    rx_ether_ctl.lbm_proc_by_cpu            = 0;
    rx_ether_ctl.lmm_proc_by_cpu            = 1;
    rx_ether_ctl.ccm_with_unknown_tlv_tocpu = 0;
    rx_ether_ctl.alarm_src_mac_mismatch     = 1;
    rx_ether_ctl.d_unexp_period_timer_cfg   = 14;
    rx_ether_ctl.d_meg_lvl_timer_cfg        = 14;
    rx_ether_ctl.d_mismerge_timer_cfg       = 14;
    rx_ether_ctl.d_unexp_mep_timer_cfg      = 14;
    rx_ether_ctl.rmep_while_cfg             = 14;

    /*ETH CSF*/
    rx_ether_ctl.eth_csf_los                = 0;
    rx_ether_ctl.eth_csf_fdi                = 1;
    rx_ether_ctl.eth_csf_rdi                = 2;
    rx_ether_ctl.eth_csf_clear              = 3;
    rx_ether_ctl.rx_csf_while_cfg           = 14;
    rx_ether_ctl.csf_while_cfg              = 4;

    /*temp for testing*/
    rx_ether_ctl.seqnum_fail_report_thrd    = 0x1F;

    rx_ether_ctl.ether_defect_to_rdi0       = 0xFFFFFFFF;
    rx_ether_ctl.ether_defect_to_rdi1       = 0x7FFFFFFF;

    rdi_index = (2 << 3) | (4);
    if (rdi_index < 32)
    {
        CTC_BIT_UNSET(rx_ether_ctl.ether_defect_to_rdi0, rdi_index);
    }
    else
    {
        CTC_BIT_UNSET(rx_ether_ctl.ether_defect_to_rdi1, rdi_index);
    }

    rdi_index = (0 << 3) | (7);
    if (rdi_index < 32)
    {
        CTC_BIT_UNSET(rx_ether_ctl.ether_defect_to_rdi0, rdi_index);
    }
    else
    {
        CTC_BIT_UNSET(rx_ether_ctl.ether_defect_to_rdi1, rdi_index);
    }

    cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    /*OamEngine Header Edit*/
    sal_memset(&hdr_edit_ctl, 0, sizeof(oam_header_edit_ctl_t));
    cmd = DRV_IOR(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_edit_ctl));
    hdr_edit_ctl.iloop_chan_id       = SYS_RESERVE_PORT_ID_ILOOP;
    hdr_edit_ctl.lbr_sa_type         = 0;
    hdr_edit_ctl.lbr_sa_using_lbm_da = 1;
    hdr_edit_ctl.cpu_port_id         = SYS_RESERVE_PORT_ID_CPU_OAM;
    sys_greatbelt_get_gchip_id(lchip, &gchip_id0);
    hdr_edit_ctl.local_chip_id       = gchip_id0;         /*local gchipid*/
    hdr_edit_ctl.tx_ccm_by_epe       = 0;
    hdr_edit_ctl.oam_port_id         = SYS_RESERVE_PORT_ID_OAM;
    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
                                                          &nexthop_ptr_bridge));

    hdr_edit_ctl.bypass_next_hop_ptr = nexthop_ptr_bridge;

    cmd = DRV_IOW(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_edit_ctl));

    /*OamEngine Tx*/
    sal_memset(&eth_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));
    eth_tx_ctl.tx_ccm_by_epe           = 0;
    eth_tx_ctl.lkup_link_agg_mem       = 1;
    eth_tx_ctl.iloop_chan_id           = SYS_RESERVE_PORT_ID_ILOOP;
    eth_tx_ctl.oam_chip_id             = gchip_id; /*local gchipid*/
    eth_tx_ctl.ccm_ether_type          = 0x8902;
    eth_tx_ctl.ccm_version             = 0;
    eth_tx_ctl.ccm_opcode              = 1;
    eth_tx_ctl.ccm_first_tlv_offset    = 70;
    eth_tx_ctl.tpid0                   = 0x8100;
    eth_tx_ctl.tpid1                   = 0x8100;
    eth_tx_ctl.tpid2                   = 0x9100;
    eth_tx_ctl.tpid3                   = 0x88a8;

    CTC_ERROR_RETURN(sys_greatbelt_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_OAM_BRIDGE_NH,
                                                          &nexthop_ptr_bridge));

    eth_tx_ctl.bridge_nexthop_ptr_high = (nexthop_ptr_bridge >> 12) & 0x3F;
    eth_tx_ctl.bridge_next_ptr_low     = (nexthop_ptr_bridge & 0xFFF);

    /*mcast mac da 0x0180c2000300(last 3bit is level)*/
    eth_tx_ctl.cfm_mcast_addr_high   = 0x0180c2 & 0xFFFFFF;
    eth_tx_ctl.cfm_mcast_addr_low    = (0x000030 >> 3) & 0x1FFFFF;

    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));

    sal_memset(&oam_ether_tx_mac, 0, sizeof(oam_ether_tx_mac_t));
#if 0
    oam_ether_tx_mac.tx_port_mac_high = (P_EHT_OAM_MASTER_GLB->port_mac[0] << 24) |
        (P_EHT_OAM_MASTER_GLB->port_mac[1] << 16) |
        (P_EHT_OAM_MASTER_GLB->port_mac[2] << 8) |
        (P_EHT_OAM_MASTER_GLB->port_mac[3] << 0);

    cmd = DRV_IOW(OamEtherTxMac_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_tx_mac));
#endif

    bridge_mac[0] = 0x0;
    bridge_mac[1] = 0x11;
    bridge_mac[2] = 0x11;
    bridge_mac[3] = 0x11;
    bridge_mac[4] = 0x11;
    bridge_mac[5] = 0x11;
    _sys_greatbelt_cfm_oam_set_bridge_mac(lchip, &bridge_mac);

    sal_memset(&ds_port_property, 0, sizeof(ds_port_property));
    cmd = DRV_IOW(DsPortProperty_t, DRV_ENTRY_FLAG);

    for (mp_port_id = 0; mp_port_id < 192; mp_port_id++)
    {
        ds_port_property.mac_sa_byte = (0x22 << 8 | mp_port_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mp_port_id, cmd, &ds_port_property));
    }

    /*OamEngine Update*/
 /*#define CHIP_CORE_FRE  450*/

    sal_memset(&oam_update_ctl, 0, sizeof(oam_update_ctl_t));
    cmd = DRV_IOR(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));
    /*!!!!!!!!!!!!!!!!!!need update on board!!!!!!!!!!!!!!!!!!!!*/
    oam_update_ctl.itu_defect_clear_mode    = 1;
    oam_update_ctl.cnt_shift_while_cfg      = 4;
    oam_update_ctl.cci_while_cfg            = 4;
    oam_update_ctl.gen_rdi_by_dloc          = 1;

    cmd = DRV_IOW(OamUpdateCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));

    /*ipe oam register*/
    sal_memset(&ipe_oam_ctl, 0, sizeof(ipe_oam_ctl_t));
    cmd = DRV_IOR(IpeOamCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_oam_ctl));
    ipe_oam_ctl.lm_proactive_ether_oam_packet   = 1;
    ipe_oam_ctl.link_oam_dest_chip_id           = gchip_id;
    ipe_oam_ctl.exception2_discard_down_lm_en   = 1;
    ipe_oam_ctl.exception2_discard_up_lm_en     = 1;
    ipe_oam_ctl.discard_link_lm_en              = 1;
    ipe_oam_ctl.lm_green_packet                 = 0;
    ipe_oam_ctl.discard_down_lm_en31_0          = 0xFFFFFFFF;
    ipe_oam_ctl.discard_down_lm_en63_32         = 0xFFFFFFFF;
    ipe_oam_ctl.discard_up_lm_en31_0            = 0xFFFFFFFF;
    ipe_oam_ctl.discard_up_lm_en63_32           = 0xFFFFFFFF;
    cmd = DRV_IOW(IpeOamCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_oam_ctl));

    /*epe oam register*/
    sal_memset(&epe_oam_ctl, 0, sizeof(epe_oam_ctl_t));
    cmd = DRV_IOR(EpeOamCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_oam_ctl));
    epe_oam_ctl.lm_proactive_ether_oam_packet   = 1;
    epe_oam_ctl.discard_link_lm_en              = 1;
    epe_oam_ctl.lm_green_packet                 = 0;
    epe_oam_ctl.discard_down_lm_en31_0          = 0xFFFFFFFF;
    epe_oam_ctl.discard_down_lm_en63_32         = 0xFFFFFFFF;
    epe_oam_ctl.discard_up_lm_en31_0            = 0xFFFFFFFF;
    epe_oam_ctl.discard_up_lm_en63_32           = 0xFFFFFFFF;
    cmd = DRV_IOW(EpeOamCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_oam_ctl));

    _sys_greatbelt_cfm_chan_resv(lchip);
    return CTC_E_NONE;
}


/**
 @file ctc_goldengate_cfm_db.c

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

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"

#include "sys_goldengate_ftm.h"

#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"

#include "sys_goldengate_oam.h"
#include "sys_goldengate_oam_db.h"
#include "sys_goldengate_oam_debug.h"
#include "sys_goldengate_oam_cfm_db.h"
#include "sys_goldengate_oam_cfm.h"
#include "sys_goldengate_oam_com.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_enum.h"
#include "sys_goldengate_port.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/

extern sys_oam_master_t* g_gg_oam_master[CTC_MAX_LOCAL_CHIP_NUM];

/****************************************************************************
*
* Function
*
**
***************************************************************************/

#define CHAN "Function Begin"

sys_oam_chan_eth_t*
_sys_goldengate_cfm_chan_lkup(uint8 lchip, ctc_oam_key_t* p_key_parm)
{
    sys_oam_chan_eth_t sys_oam_chan_eth;
    uint32 cmd = 0;
    uint32 oam_fid_en = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_oamFidEn_f);
    DRV_IOCTL(lchip, 0, cmd, &oam_fid_en);
    sal_memset(&sys_oam_chan_eth, 0, sizeof(sys_oam_chan_eth_t));
    sys_oam_chan_eth.key.com.mep_type = p_key_parm->mep_type;
    sys_oam_chan_eth.key.gport        = p_key_parm->u.eth.gport;
    sys_oam_chan_eth.key.vlan_id      = p_key_parm->u.eth.vlan_id;
    sys_oam_chan_eth.key.use_fid      = CTC_FLAG_ISSET(p_key_parm->flag, CTC_OAM_KEY_FLAG_L2VPN) && oam_fid_en;
    sys_oam_chan_eth.key.link_oam     = CTC_FLAG_ISSET(p_key_parm->flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
    sys_oam_chan_eth.key.l2vpn_oam_id      = p_key_parm->u.eth.l2vpn_oam_id;
    sys_oam_chan_eth.com.mep_type     = p_key_parm->mep_type;

    return (sys_oam_chan_eth_t*)_sys_goldengate_oam_chan_lkup(lchip, &sys_oam_chan_eth.com);
}

int32
_sys_goldengate_cfm_chan_lkup_cmp(uint8 lchip, void* p_cmp)
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

    if (p_chan_eth_para->key.l2vpn_oam_id != p_chan_eth_db->key.l2vpn_oam_id)
    {
        return 0;
    }

    return 1;
}

sys_oam_chan_eth_t*
_sys_goldengate_cfm_chan_build_node(uint8 lchip, ctc_oam_lmep_t* p_chan_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    uint32 cmd = 0;
    uint32 oam_fid_en = 0;
    uint8 gchip = 0;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = (sys_oam_chan_eth_t*)mem_malloc(MEM_OAM_MODULE, sizeof(sys_oam_chan_eth_t));
    if (NULL != p_sys_chan_eth)
    {
        gchip = p_chan_param->u.y1731_lmep.master_gchip;

        cmd = DRV_IOR(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_oamFidEn_f);
        DRV_IOCTL(lchip, 0, cmd, &oam_fid_en);

        sal_memset(p_sys_chan_eth, 0, sizeof(sys_oam_chan_eth_t));
        p_sys_chan_eth->com.mep_type = p_chan_param->key.mep_type;
        p_sys_chan_eth->com.master_chipid = gchip;
        p_sys_chan_eth->com.lchip = lchip;
        p_sys_chan_eth->com.link_oam = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        p_sys_chan_eth->key.com.mep_type = p_chan_param->key.mep_type;
        p_sys_chan_eth->key.gport = p_chan_param->key.u.eth.gport;
        p_sys_chan_eth->key.vlan_id = p_chan_param->key.u.eth.vlan_id;
        p_sys_chan_eth->key.link_oam = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
        p_sys_chan_eth->key.use_fid = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_L2VPN)&&oam_fid_en;
        p_sys_chan_eth->key.l2vpn_oam_id = p_chan_param->key.u.eth.l2vpn_oam_id;
        if (CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_L2VPN)
            && (!CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_VPLS)))
        {
            p_sys_chan_eth->key.is_vpws = 1;

        }

        if (CTC_FLAG_ISSET(p_chan_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
        {
            if (CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
            {
                p_sys_chan_eth->link_lm_type     = p_chan_param->u.y1731_lmep.lm_type;
                p_sys_chan_eth->link_lm_cos_type = p_chan_param->u.y1731_lmep.lm_cos_type;
                p_sys_chan_eth->link_lm_cos      = p_chan_param->u.y1731_lmep.lm_cos;
            }
            else
            {
                p_sys_chan_eth->lm_type     = p_chan_param->u.y1731_lmep.lm_type;
            }
        }
        else
        {
            p_sys_chan_eth->lm_type     = CTC_OAM_LM_TYPE_NONE;
        }
    }

    return p_sys_chan_eth;
}

int32
_sys_goldengate_cfm_chan_free_node(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_chan_eth);
    p_sys_chan_eth = NULL;

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_build_index(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    DsEgressXcOamEthHashKey_m eth_oam_key;
    uint16 gport        = 0;
    uint8 is_fid = 0;
    uint16 vlan_id        = 0;
    uint8 l2vpn_lkup_mode = 0;
    uint16 vpws_oam_id_base = 2<<12;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);
    gport = p_sys_chan_eth->key.gport;

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }


    if (!p_sys_chan_eth->key.link_oam)
    {
        /*hash key offset alloc*/
        sal_memset(&eth_oam_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
        gport =  SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport);
        is_fid = p_sys_chan_eth->key.use_fid;
        if (is_fid)
        {
            if (p_sys_chan_eth->key.is_vpws)
            {
                vlan_id = vpws_oam_id_base + p_sys_chan_eth->key.l2vpn_oam_id;
            }
            else
            {
                _sys_goldengate_cfm_oam_get_l2vpn_oam_mode(lchip, &l2vpn_lkup_mode);
                if (l2vpn_lkup_mode) /*vlan mode*/
                {
                    vlan_id = p_sys_chan_eth->key.vlan_id;
                }
                else /*fid mode*/
                {
                    vlan_id = p_sys_chan_eth->key.l2vpn_oam_id;
                }
            }
        }
        else
        {
            vlan_id = p_sys_chan_eth->key.vlan_id;
        }

        SetDsEgressXcOamEthHashKey(V, hashType_f,  &eth_oam_key, EGRESSXCOAMHASHTYPE_ETH);
        SetDsEgressXcOamEthHashKey(V, globalSrcPort_f,   &eth_oam_key, gport);
        SetDsEgressXcOamEthHashKey(V, isFid_f,  &eth_oam_key, is_fid);
        SetDsEgressXcOamEthHashKey(V, vlanId_f,   &eth_oam_key, vlan_id);
        SetDsEgressXcOamEthHashKey(V, valid_f,   &eth_oam_key, 1);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port = 0x%x\n",   gport);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.vlan_id = 0x%x\n",           vlan_id);
        SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.is_fid = 0x%x\n",            is_fid);

        CTC_ERROR_RETURN( sys_goldengate_oam_key_lookup_io(lchip, DsEgressXcOamEthHashKey_t,
                                                        & p_sys_chan_eth->key.com.key_index ,
                                                        &eth_oam_key ));

        p_sys_chan_eth->key.com.key_alloc = SYS_OAM_KEY_HASH;
    }


    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_chan_free_index(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    DsEgressXcOamEthHashKey_m ds_eth_oam_key;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_eth);

    if (p_sys_chan_eth->key.link_oam)
    {
        return CTC_E_NONE;
    }

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        /*eth oam key delete*/
        sal_memset(&ds_eth_oam_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
        SetDsEgressXcOamEthHashKey(V, valid_f,                      &ds_eth_oam_key, 0);
        CTC_ERROR_RETURN(sys_goldengate_oam_key_delete_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_key));

    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_add_to_db(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_chan_add_to_db(lchip, &p_sys_chan_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_del_from_db(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_chan_del_from_db(lchip, &p_sys_chan_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_add_to_asic(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    uint16 gport      = 0;
    uint8 is_fid      = 0;
    uint16 vlan_id    = 0;
    uint8 l2vpn_oam_mode = 0;
    uint16 vpws_oam_id_base = 1 << 13;/*8K*/
    DsEgressXcOamEthHashKey_m ds_eth_oam_key;


    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_sys_chan_eth);

    gport = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_sys_chan_eth->key.gport);
    is_fid = p_sys_chan_eth->key.use_fid;
    if (is_fid)
    {
        if (p_sys_chan_eth->key.is_vpws)
        {
            vlan_id = vpws_oam_id_base + p_sys_chan_eth->key.l2vpn_oam_id;
        }
        else
        {
             _sys_goldengate_cfm_oam_get_l2vpn_oam_mode(lchip, &l2vpn_oam_mode);
            if (l2vpn_oam_mode) /*vlan mode*/
            {
                vlan_id = p_sys_chan_eth->key.vlan_id;
            }
            else /*fid mode*/
            {
                vlan_id = p_sys_chan_eth->key.l2vpn_oam_id;
            }
        }
    }
    else
    {
        vlan_id = p_sys_chan_eth->key.vlan_id;
    }

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            /*eth oam key write*/
            sal_memset(&ds_eth_oam_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
            SetDsEgressXcOamEthHashKey(V, hashType_f,               &ds_eth_oam_key, EGRESSXCOAMHASHTYPE_ETH);
            SetDsEgressXcOamEthHashKey(V, globalSrcPort_f,          &ds_eth_oam_key, gport);
            SetDsEgressXcOamEthHashKey(V, isFid_f,                        &ds_eth_oam_key, is_fid);
            SetDsEgressXcOamEthHashKey(V, vlanId_f,                     &ds_eth_oam_key, vlan_id);
            SetDsEgressXcOamEthHashKey(V, valid_f,                      &ds_eth_oam_key, 1);
            SetDsEgressXcOamEthHashKey(V, oamDestChipId_f,       &ds_eth_oam_key, p_sys_chan_eth->com.master_chipid);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.global_src_port = 0x%x\n",   gport);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.vlan_id = 0x%x\n",           vlan_id);
            SYS_OAM_DBG_INFO("KEY: ds_eth_oam_key.is_fid = 0x%x\n",            is_fid);
            CTC_ERROR_RETURN( sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamEthHashKey_t,
                                                                       p_sys_chan_eth->key.com.key_index ,
                                                                       &ds_eth_oam_key));
            SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LOOKUP_KEY, 1, 1);
        }


    }

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_chan_del_from_asic(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    DsEgressXcOamEthHashKey_m ds_eth_oam_key;

    SYS_OAM_DBG_FUNC();
    CTC_PTR_VALID_CHECK(p_sys_chan_eth);

    if (p_sys_chan_eth->key.link_oam)
    {
        return CTC_E_NONE;
    }

    if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
        && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
    {
        /*eth oam key delete*/
        sal_memset(&ds_eth_oam_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
        SetDsEgressXcOamEthHashKey(V, valid_f,                      &ds_eth_oam_key, 0);
        CTC_ERROR_RETURN(sys_goldengate_oam_key_delete_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_key));

        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LOOKUP_KEY, 0, 1);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_update(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth, bool is_add)
{

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    DsEgressXcOamEthHashKey_m     ds_eth_oam_hash_key;
    DsEthLmProfile_m  ds_eth_lm_profile;
    uint32 mep_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint32 lm_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8 lm_cos[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8 lm_cos_type[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8  md_level    = 0;
    uint8  level       = 0;
    uint32 index       = 0;
    uint32 cmd = 0;
    uint16 gport       = 0;
    uint8* p_mep_bitmap  = 0;
    uint8* p_lm_bitmap = 0;
    uint8  lm_en       = 0;
    uint32 value       = 0;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = (sys_oam_chan_eth_t*)p_sys_lmep_eth->com.p_sys_chan;
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
        p_sys_chan_eth->lm_index[md_level] = p_sys_lmep_eth->com.lm_index_base;
        p_sys_chan_eth->lm_cos[md_level] = p_sys_lmep_eth->com.lm_cos;
        p_sys_chan_eth->lm_cos_type[md_level] = p_sys_lmep_eth->com.lm_cos_type;
        lm_en   = 1;
    }
    else
    {
        CLEAR_BIT((*p_lm_bitmap), md_level);
        p_sys_chan_eth->lm_index[md_level] = 0;
        p_sys_chan_eth->lm_cos[md_level] = 0;
        p_sys_chan_eth->lm_cos_type[md_level] = 0;
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

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
            CTC_ERROR_RETURN(sys_goldengate_oam_key_read_io(lchip, DsEgressXcOamEthHashKey_t,
                                                         p_sys_chan_eth->key.com.key_index ,
                                                         &ds_eth_oam_hash_key));
        }


        for (level = 0; level <= SYS_OAM_MAX_MD_LEVEL; level++)
        {
            if ((IS_BIT_SET(p_sys_chan_eth->down_mep_bitmap, level)) ||
                (IS_BIT_SET(p_sys_chan_eth->up_mep_bitmap, level)))
            {
                mep_index[index] = p_sys_chan_eth->mep_index[level];
                if (IS_BIT_SET(p_sys_chan_eth->lm_bitmap, level))
                {
                    lm_index[index] = p_sys_chan_eth->lm_index[level];
                    lm_cos[index] = p_sys_chan_eth->lm_cos[level];
                    lm_cos_type[index] = p_sys_chan_eth->lm_cos_type[level];
                }
                index++;
            }
        }


        if (!CTC_IS_LINKAGG_PORT(p_sys_chan_eth->key.gport)
               && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
        {
            return CTC_E_NONE;
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            if (p_sys_lmep_eth->is_up)
            {
                SetDsEgressXcOamEthHashKey(V, mepUpBitmap_f,   &ds_eth_oam_hash_key, (p_sys_chan_eth->up_mep_bitmap));
            }
            else
            {
                SetDsEgressXcOamEthHashKey(V, mepDownBitmap_f,   &ds_eth_oam_hash_key, (p_sys_chan_eth->down_mep_bitmap));
            }

            SetDsEgressXcOamEthHashKey(V, array_0_mepIndex_f,        &ds_eth_oam_hash_key, mep_index[0]);
            SetDsEgressXcOamEthHashKey(V, array_1_mepIndex_f,        &ds_eth_oam_hash_key, mep_index[1]);
            SetDsEgressXcOamEthHashKey(V, array_2_mepIndex_f,         &ds_eth_oam_hash_key, mep_index[2]);
            SetDsEgressXcOamEthHashKey(V, array_3_mepIndex_f,          &ds_eth_oam_hash_key, mep_index[3]);

            SetDsEgressXcOamEthHashKey(V, lmBitmap_f,              &ds_eth_oam_hash_key, ((*p_lm_bitmap) ));
            SetDsEgressXcOamEthHashKey(V, lmProfileIndex_f,       &ds_eth_oam_hash_key, p_sys_chan_eth->com.lm_index_base);
            SetDsEgressXcOamEthHashKey(V, lmType_f,                   &ds_eth_oam_hash_key, p_sys_chan_eth->lm_type);
            SetDsEgressXcOamEthHashKey(V, array_0_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[0]);
            SetDsEgressXcOamEthHashKey(V, array_1_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[1]);
            SetDsEgressXcOamEthHashKey(V, array_2_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[2]);
            SetDsEgressXcOamEthHashKey(V, array_3_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[3]);

        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_hash_key));
        }

        if (p_sys_chan_eth->lm_bitmap)
        {
            sal_memset(&ds_eth_lm_profile, 0, sizeof(DsEthLmProfile_m));
            cmd = DRV_IOW(DsEthLmProfile_t, DRV_ENTRY_FLAG);
            SetDsEthLmProfile(V, array_0_lmIndexBase_f, &ds_eth_lm_profile, lm_index[0]);
            SetDsEthLmProfile(V, array_1_lmIndexBase_f, &ds_eth_lm_profile, lm_index[1]);
            SetDsEthLmProfile(V, array_2_lmIndexBase_f, &ds_eth_lm_profile, lm_index[2]);
            SetDsEthLmProfile(V, array_3_lmIndexBase_f, &ds_eth_lm_profile, lm_index[3]);
            SetDsEthLmProfile(V, array_0_lmCos_f, &ds_eth_lm_profile, lm_cos[0]);
            SetDsEthLmProfile(V, array_1_lmCos_f, &ds_eth_lm_profile, lm_cos[1]);
            SetDsEthLmProfile(V, array_2_lmCos_f, &ds_eth_lm_profile, lm_cos[2]);
            SetDsEthLmProfile(V, array_3_lmCos_f, &ds_eth_lm_profile, lm_cos[3]);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.lm_index_base, cmd, &ds_eth_lm_profile));
        }

    }
    else
    {
        /*update mepindex in DsSrcPort*/
        gport = p_sys_chan_eth->key.gport;
        if (!CTC_IS_LINKAGG_PORT(gport)
           && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport))))
        {
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_MEP_INDEX,
                p_sys_chan_eth->mep_index[md_level]));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_EN,(is_add ? 1 : 0)));
        value = md_level;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_LEVEL,value));
        value = lm_en?p_sys_chan_eth->link_lm_type:CTC_OAM_LM_TYPE_NONE;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_LINK_LM_TYPE,
            CTC_BOTH_DIRECTION, value));
        value = lm_en?p_sys_chan_eth->link_lm_cos_type:0;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE,
            CTC_BOTH_DIRECTION, value));
        value = lm_en?p_sys_chan_eth->link_lm_cos:0;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_LINK_LM_COS,
            CTC_BOTH_DIRECTION, value));
        value = (lm_en)?p_sys_chan_eth->com.link_lm_index_base:0;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE,
            CTC_BOTH_DIRECTION, value));
    }


    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_update_reomte(uint8 lchip, ctc_oam_lmep_t* p_lmep, sys_oam_chan_eth_t* p_sys_chan_eth, bool is_add)
{
    DsEgressXcOamEthHashKey_m     ds_eth_oam_hash_key;
    uint32 mep_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8  md_level    = 0;
    uint8  level       = 0;
    uint32 index       = 0;
    uint16 gport       = 0;
    uint8* p_mep_bitmap  = 0;
    uint8* p_lm_bitmap = 0;

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
    }
    else
    {
        CLEAR_BIT((*p_lm_bitmap), md_level);
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
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
            CTC_ERROR_RETURN(sys_goldengate_oam_key_read_io(lchip, DsEgressXcOamEthHashKey_t,
                                                         p_sys_chan_eth->key.com.key_index ,
                                                         &ds_eth_oam_hash_key));
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
            if (CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_UP_MEP))
            {
                SetDsEgressXcOamEthHashKey(V, mepUpBitmap_f,       &ds_eth_oam_hash_key, (p_sys_chan_eth->up_mep_bitmap));
            }
            else
            {
                 SetDsEgressXcOamEthHashKey(V, mepDownBitmap_f,    &ds_eth_oam_hash_key, (p_sys_chan_eth->down_mep_bitmap));
            }

            SetDsEgressXcOamEthHashKey(V, array_0_mepIndex_f,              &ds_eth_oam_hash_key, mep_index[0]);
            SetDsEgressXcOamEthHashKey(V, array_1_mepIndex_f,            &ds_eth_oam_hash_key, mep_index[1]);
            SetDsEgressXcOamEthHashKey(V, array_2_mepIndex_f,            &ds_eth_oam_hash_key, mep_index[2]);
            SetDsEgressXcOamEthHashKey(V, array_3_mepIndex_f,            &ds_eth_oam_hash_key, mep_index[3]);
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            CTC_ERROR_RETURN(sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_hash_key));
        }

    }
    else
    {
        /*update mepindex in DsSrcPort*/
        uint32 value = 0;
        gport = p_sys_chan_eth->key.gport;

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_MEP_INDEX,
                p_sys_chan_eth->mep_index[md_level]));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_EN,(is_add ? 1 : 0)));
        value = md_level;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_LINK_OAM_LEVEL,value));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_chan_update_mip(uint8 lchip, sys_oam_chan_eth_t* p_sys_chan_eth, uint8 md_level, bool is_add)
{
    DsEgressXcOamEthHashKey_m     ds_eth_oam_hash_key;
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
        && (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(p_sys_chan_eth->key.gport))))
    {
        return CTC_E_NONE;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {
        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
            CTC_ERROR_RETURN(sys_goldengate_oam_key_read_io(lchip, DsEgressXcOamEthHashKey_t,
                                                         p_sys_chan_eth->key.com.key_index ,
                                                         &ds_eth_oam_hash_key));
            SetDsEgressXcOamEthHashKey(V, mipBitmap_f,   &ds_eth_oam_hash_key, (p_sys_chan_eth->mip_bitmap) & 0x7F);
            CTC_ERROR_RETURN(sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_hash_key));

        }
    }


    return CTC_E_NONE;
}

#define LMEP "Function Begin"

sys_oam_lmep_y1731_t*
_sys_goldengate_cfm_lmep_lkup(uint8 lchip, uint8 md_level, sys_oam_chan_eth_t* p_sys_chan_eth)
{
    sys_oam_lmep_y1731_t sys_oam_lmep_eth;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_lmep_eth, 0, sizeof(sys_oam_lmep_y1731_t));
    sys_oam_lmep_eth.md_level = md_level;

    return (sys_oam_lmep_y1731_t*)_sys_goldengate_oam_lmep_lkup(lchip, &p_sys_chan_eth->com, &sys_oam_lmep_eth.com);
}

int32
_sys_goldengate_cfm_lmep_lkup_cmp(uint8 lchip, void* p_cmp)
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
_sys_goldengate_cfm_lmep_build_node(uint8 lchip, ctc_oam_lmep_t* p_lmep_param)
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
        if((!CTC_FLAG_ISSET(p_lmep_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
            &&CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
        {
            p_sys_lmep_eth->com.lm_cos  = p_lmep_param->u.y1731_lmep.lm_cos;
            p_sys_lmep_eth->com.lm_cos_type  = p_lmep_param->u.y1731_lmep.lm_cos_type;
        }
    }

    return p_sys_lmep_eth;
}

int32
_sys_goldengate_cfm_lmep_free_node(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_lmep_eth);
    p_sys_lmep_eth = NULL;
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_build_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_build_index(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_free_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_free_index(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_add_to_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_add_to_db(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_del_from_db(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_del_from_db(lchip, &p_sys_lmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_build_eth_data(uint8 lchip, ctc_oam_y1731_lmep_t* p_eth_lmep,
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
_sys_goldengate_cfm_lmep_add_to_asic(uint8 lchip, ctc_oam_lmep_t* p_lmep_param,
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

    CTC_ERROR_RETURN(_sys_goldengate_cfm_lmep_build_eth_data(lchip, p_eth_lmep, p_sys_lmep_eth));
    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_add_eth_to_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    SYS_OAM_DBG_FUNC();

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_del_from_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_update_asic(uint8 lchip, ctc_oam_update_t* p_lmep_param,
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

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_build_lm_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    sys_oam_chan_eth_t* sys_oam_chan_eth = NULL;

    SYS_OAM_DBG_FUNC();

    sys_oam_chan_eth = (sys_oam_chan_eth_t*)p_sys_lmep_eth->com.p_sys_chan;
    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
    {
        if (!sys_oam_chan_eth->key.link_oam)
        {
            if (p_sys_lmep_eth->com.p_sys_chan->lm_num >= 3)
            {
                return CTC_E_OAM_LM_NUM_RXCEED;
            }
            CTC_ERROR_RETURN(_sys_goldengate_oam_lm_build_index(lchip, &sys_oam_chan_eth->com, FALSE, p_sys_lmep_eth->md_level));
        }
        else if(!sys_oam_chan_eth->link_lm_index_alloced)
        {
            CTC_ERROR_RETURN(_sys_goldengate_oam_lm_build_index(lchip, &sys_oam_chan_eth->com, TRUE, p_sys_lmep_eth->md_level));
            sys_oam_chan_eth->link_lm_index_alloced = TRUE;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_lmep_free_lm_index(uint8 lchip, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{

    sys_oam_chan_eth_t* sys_oam_chan_eth = NULL;
    SYS_OAM_DBG_FUNC();

    sys_oam_chan_eth = (sys_oam_chan_eth_t*)p_sys_lmep_eth->com.p_sys_chan;

    if (!sys_oam_chan_eth->key.link_oam)
    {
        CTC_ERROR_RETURN(_sys_goldengate_oam_lm_free_index(lchip, &sys_oam_chan_eth->com, FALSE, p_sys_lmep_eth->md_level));
    }
    else if (sys_oam_chan_eth->link_lm_index_alloced)
    {
        CTC_ERROR_RETURN(_sys_goldengate_oam_lm_free_index(lchip, &sys_oam_chan_eth->com, TRUE, p_sys_lmep_eth->md_level));
        sys_oam_chan_eth->link_lm_index_alloced= FALSE;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_update_lmep_port_status(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    ctc_slistnode_t* slist_lmep_node = NULL;
    ctc_slist_t* p_lmep_list = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_goldengate_cfm_chan_lkup(lchip, &p_lmep_param->key);
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

                CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));
            }
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_update_lmep_if_status(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    ctc_oam_update_t* p_eth_lmep_param = p_lmep_param;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    uint32 field_value = 0;
    uint16 gport = 0;
    uint8 intf_status = 0;
    uint8 gchip     = 0;
    uint16 lport = 0;
    uint16 mp_port_id = 0;

    gport = p_eth_lmep_param->key.u.eth.gport;
    intf_status = p_eth_lmep_param->update_value;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)


    if (CTC_IS_LINKAGG_PORT(gport))
    {
        lport = CTC_MAP_GPORT_TO_LPORT(gport);
        mp_port_id = lport + 448;
    }
    else
    {
        if (FALSE == sys_goldengate_chip_is_local(lchip, SYS_MAP_CTC_GPORT_TO_GCHIP(gport)))
        {
            return CTC_E_NONE;
        }

        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
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

    if (CTC_IS_LINKAGG_PORT(gport))
    {

        p_sys_chan_eth = _sys_goldengate_cfm_chan_lkup(lchip, &p_lmep_param->key);
        if (NULL == p_sys_chan_eth)
        {
            return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        }

        sys_goldengate_get_gchip_id(lchip, &gchip);
        if (gchip == p_sys_chan_eth->com.master_chipid)
        {
            CTC_ERROR_RETURN(sys_goldengate_port_set_if_status(lchip, mp_port_id, field_value));
        }
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_port_set_if_status(lchip, lport, field_value));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_update_lmep_lm(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    uint32 cmd = 0;
    uint16 gport            = 0;
    uint32 field_value      = 0;
    DsEthLmProfile_m  ds_eth_lm_profile;
    uint32 lm_index[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8 lm_cos[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8 lm_cos_type[SYS_OAM_MAX_MEP_NUM_PER_CHAN] = {0};
    uint8 is_ether_service = 0;
    uint8 index = 0;
    uint8 level = 0;
    uint8 is_link = 0;
    uint8 port_prop = 0;

    ctc_oam_update_t* p_lmep = (ctc_oam_update_t*)p_lmep_param;

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    uint8            md_level = 0;

    DsEgressXcOamEthHashKey_m   ds_eth_oam_hash_key;

    SYS_OAM_DBG_FUNC();

    if (!CTC_IS_LINKAGG_PORT(p_lmep->key.u.eth.gport) &&
        FALSE == sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(p_lmep->key.u.eth.gport)))
    {
        return CTC_E_NONE;
    }

    /*1. lookup chan and check exist*/
    p_sys_chan_eth = _sys_goldengate_cfm_chan_lkup(lchip, &p_lmep->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }

    md_level = p_lmep->key.u.eth.md_level;
    is_link = CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
    if (CTC_IS_BIT_SET(p_sys_chan_eth->lm_bitmap, md_level))
    {
        if((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE == p_lmep->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS == p_lmep->update_type))
        {
            return CTC_E_OAM_INVALID_CFG_LM;
        }
    }

    /*2. lookup lmep and check exist*/
    if ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN == p_lmep->update_type)
        ||(CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE == p_lmep->update_type)
        ||(CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS == p_lmep->update_type))
    {
        p_sys_lmep_eth = _sys_goldengate_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
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
            CTC_BIT_SET(p_sys_chan_eth->lm_bitmap, md_level);
            if (!p_sys_chan_eth->link_lm_index_alloced && p_sys_chan_eth->key.link_oam)
            {
                CTC_ERROR_RETURN(_sys_goldengate_oam_lm_build_index(lchip, &p_sys_chan_eth->com, TRUE, md_level));
                p_sys_chan_eth->link_lm_index_alloced = TRUE;
            }

            if (!p_sys_chan_eth->key.link_oam)
            {
                CTC_ERROR_RETURN(_sys_goldengate_oam_lm_build_index(lchip, &p_sys_chan_eth->com, FALSE, md_level));
                is_ether_service = 1;
                p_sys_chan_eth->lm_index[md_level] = p_sys_lmep_eth->com.lm_index_base;
            }
            field_value = p_sys_chan_eth->com.link_lm_index_base;
            port_prop = SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE;
        }
        else
        {
            CTC_UNSET_FLAG(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN);
            field_value     = CTC_OAM_LM_TYPE_NONE;

            CTC_BIT_UNSET(p_sys_chan_eth->lm_bitmap, md_level);

            if (!is_link)
            {
                if (0 == p_sys_chan_eth->lm_bitmap)
                {
                    p_sys_chan_eth->lm_type = CTC_OAM_LM_TYPE_NONE;
                }
                CTC_ERROR_RETURN(_sys_goldengate_oam_lm_free_index(lchip, &p_sys_chan_eth->com, FALSE, md_level));
                is_ether_service = 1;
                p_sys_chan_eth->lm_index[md_level] = 0;
                p_sys_chan_eth->lm_cos[md_level] = 0;
                p_sys_chan_eth->lm_cos_type[md_level] = 0;
            }
            else
            {
                if(p_sys_chan_eth->link_lm_index_alloced)
                {
                    p_sys_chan_eth->link_lm_type = CTC_OAM_LM_TYPE_NONE;
                    CTC_ERROR_RETURN(_sys_goldengate_oam_lm_free_index(lchip, &p_sys_chan_eth->com, TRUE, md_level));
                    p_sys_chan_eth->link_lm_index_alloced = FALSE;
                }
            }
            port_prop = SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE;
        }

        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE:
        p_sys_chan_eth->key.link_oam ?
        (p_sys_chan_eth->link_lm_type = p_lmep->update_value) : (p_sys_chan_eth->lm_type = p_lmep->update_value);

        field_value     = p_lmep->update_value;
        port_prop = SYS_PORT_DIR_PROP_LINK_LM_TYPE;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE:

        if(p_sys_chan_eth->key.link_oam )
        {
            p_sys_chan_eth->link_lm_cos_type = p_lmep->update_value;
        }
        else
        {
            p_sys_chan_eth->lm_cos_type[md_level] = p_lmep->update_value;
            p_sys_lmep_eth->com.lm_cos_type = p_lmep->update_value;
        }
        field_value     = p_lmep->update_value;
        port_prop = SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE;
        break;

    case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
        if (p_sys_chan_eth->key.link_oam )
        {
            p_sys_chan_eth->link_lm_cos = p_lmep->update_value;
        }
        else
        {
            p_sys_chan_eth->lm_cos[md_level] = p_lmep->update_value;
            p_sys_lmep_eth->com.lm_cos = p_lmep->update_value;
        }
        field_value     = p_lmep->update_value;
        port_prop = SYS_PORT_DIR_PROP_LINK_LM_COS;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (!p_sys_chan_eth->key.link_oam)
    {

        for (level = 0; level <= SYS_OAM_MAX_MD_LEVEL; level++)
        {
            if ((IS_BIT_SET(p_sys_chan_eth->down_mep_bitmap, level)) ||
                    (IS_BIT_SET(p_sys_chan_eth->up_mep_bitmap, level)))
            {
                if (IS_BIT_SET(p_sys_chan_eth->lm_bitmap, level))
                {
                    lm_index[index] = p_sys_chan_eth->lm_index[level];
                    lm_cos[index] = p_sys_chan_eth->lm_cos[level];
                    lm_cos_type[index] = p_sys_chan_eth->lm_cos_type[level];
                }
                index++;
            }
        }

        if (p_sys_chan_eth->key.com.key_alloc == SYS_OAM_KEY_HASH)
        {
            sal_memset(&ds_eth_oam_hash_key, 0, sizeof(DsEgressXcOamEthHashKey_m));
            CTC_ERROR_RETURN(  sys_goldengate_oam_key_read_io(lchip, DsEgressXcOamEthHashKey_t,
                                                           p_sys_chan_eth->key.com.key_index ,
                                                           &ds_eth_oam_hash_key));

            SetDsEgressXcOamEthHashKey(V, lmBitmap_f,                 &ds_eth_oam_hash_key, (p_sys_chan_eth->lm_bitmap));
            SetDsEgressXcOamEthHashKey(V, lmProfileIndex_f,            &ds_eth_oam_hash_key, p_sys_chan_eth->com.lm_index_base);
            SetDsEgressXcOamEthHashKey(V, lmType_f,                     &ds_eth_oam_hash_key, p_sys_chan_eth->lm_type);
            SetDsEgressXcOamEthHashKey(V, array_0_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[0]);
            SetDsEgressXcOamEthHashKey(V, array_1_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[1]);
            SetDsEgressXcOamEthHashKey(V, array_2_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[2]);
            SetDsEgressXcOamEthHashKey(V, array_3_lmCosType_f,    &ds_eth_oam_hash_key, lm_cos_type[3]);

            CTC_ERROR_RETURN(sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamEthHashKey_t,
                                                          p_sys_chan_eth->key.com.key_index ,
                                                          &ds_eth_oam_hash_key));
        }

        if (is_ether_service)
        {
            sal_memset(&ds_eth_lm_profile, 0, sizeof(DsEthLmProfile_m));
            cmd = DRV_IOW(DsEthLmProfile_t, DRV_ENTRY_FLAG);
            SetDsEthLmProfile(V, array_0_lmIndexBase_f, &ds_eth_lm_profile, lm_index[0]);
            SetDsEthLmProfile(V, array_1_lmIndexBase_f, &ds_eth_lm_profile, lm_index[1]);
            SetDsEthLmProfile(V, array_2_lmIndexBase_f, &ds_eth_lm_profile, lm_index[2]);
            SetDsEthLmProfile(V, array_3_lmIndexBase_f, &ds_eth_lm_profile, lm_index[3]);
            SetDsEthLmProfile(V, array_0_lmCos_f, &ds_eth_lm_profile, lm_cos[0]);
            SetDsEthLmProfile(V, array_1_lmCos_f, &ds_eth_lm_profile, lm_cos[1]);
            SetDsEthLmProfile(V, array_2_lmCos_f, &ds_eth_lm_profile, lm_cos[2]);
            SetDsEthLmProfile(V, array_3_lmCos_f, &ds_eth_lm_profile, lm_cos[3]);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_sys_chan_eth->com.lm_index_base, cmd, &ds_eth_lm_profile));
        }
    }
    else
    {
        /*update mepindex in DsSrcPort*/
        gport = p_sys_chan_eth->key.gport;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, port_prop,CTC_BOTH_DIRECTION, field_value));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_update_lmep_trpt(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;

    uint8 md_level = 0;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_goldengate_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }
    else
    {
        md_level = p_lmep_param->key.u.eth.md_level;
        p_sys_lmep_eth = _sys_goldengate_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
        if (NULL == p_sys_lmep_eth)
        {
            return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        }
        else
        {
            if (p_sys_lmep_eth->com.mep_on_cpu)
            {
                return CTC_E_NONE;
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

            CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_update_eth_asic(lchip, &p_sys_lmep_eth->com));
        }
    }

    return CTC_E_NONE;
}


int32
_sys_goldengate_cfm_update_master_gchip(uint8 lchip, ctc_oam_update_t* p_lmep_param)
{
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;

    SYS_OAM_DBG_FUNC();

    p_sys_chan_eth = _sys_goldengate_cfm_chan_lkup(lchip, &p_lmep_param->key);
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

    CTC_ERROR_RETURN(_sys_goldengate_oam_lmep_update_master_chip((sys_oam_chan_com_t*)p_sys_chan_eth, &(p_lmep_param->update_value)));

    return CTC_E_NONE;
}


#define RMEP "Function Begin"

sys_oam_rmep_y1731_t*
_sys_goldengate_cfm_rmep_lkup(uint8 lchip, uint32 rmep_id, sys_oam_lmep_y1731_t* p_sys_lmep_eth)
{
    sys_oam_rmep_y1731_t sys_oam_rmep_eth;

    SYS_OAM_DBG_FUNC();

    sal_memset(&sys_oam_rmep_eth, 0, sizeof(sys_oam_rmep_y1731_t));
    sys_oam_rmep_eth.rmep_id = rmep_id;

    return (sys_oam_rmep_y1731_t*)_sys_goldengate_oam_rmep_lkup(lchip, &p_sys_lmep_eth->com, &sys_oam_rmep_eth.com);
}

int32
_sys_goldengate_cfm_rmep_lkup_cmp(uint8 lchip, void* p_cmp)
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
_sys_goldengate_cfm_rmep_build_node(uint8 lchip, ctc_oam_rmep_t* p_rmep_param)
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
_sys_goldengate_cfm_rmep_free_node(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    mem_free(p_sys_rmep_eth);
    p_sys_rmep_eth = NULL;

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_build_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    int32 ret = 0;
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_build_index(lchip, &p_sys_rmep_eth->com));

    if (!p_sys_rmep_eth->is_p2p_mode)
    {
        sys_oam_lmep_com_t* p_sys_lmep = NULL;
        DsEgressXcOamRmepHashKey_m  ds_eth_oam_rmep_hash_key;

        p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;
        /*HASH*/
        sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));

        SetDsEgressXcOamRmepHashKey(V, hashType_f,  &ds_eth_oam_rmep_hash_key, EGRESSXCOAMHASHTYPE_RMEP);
        SetDsEgressXcOamRmepHashKey(V, mepIndex_f,   &ds_eth_oam_rmep_hash_key,  p_sys_lmep->lmep_index);
        SetDsEgressXcOamRmepHashKey(V, rmepId_f,  &ds_eth_oam_rmep_hash_key, p_sys_rmep_eth->rmep_id);
        SetDsEgressXcOamRmepHashKey(V, valid_f,   &ds_eth_oam_rmep_hash_key, 1);

        ret = ( sys_goldengate_oam_key_lookup_io(lchip, DsEgressXcOamRmepHashKey_t,
                                                        &p_sys_rmep_eth->key_index ,
                                                        &ds_eth_oam_rmep_hash_key ));
        if(ret < 0)
        {
            CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_free_index(lchip, &p_sys_rmep_eth->com)); /*roll back rmep_index*/
            return ret;
        }

        p_sys_rmep_eth->key_alloc = SYS_OAM_KEY_HASH;


    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_free_index(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{

    SYS_OAM_DBG_FUNC();

    /*chan offset free*/
    if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
    {
        CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_free_index(lchip, &p_sys_rmep_eth->com));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_add_to_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_add_to_db(lchip, &p_sys_rmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_del_from_db(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_del_from_db(lchip, &p_sys_rmep_eth->com));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_build_eth_data(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
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
_sys_goldengate_cfm_rmep_add_to_asic(uint8 lchip, ctc_oam_rmep_t* p_rmep_param,
                                    sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);
    CTC_PTR_VALID_CHECK(p_sys_rmep_eth);

    p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_cfm_rmep_build_eth_data(lchip, p_rmep_param, p_sys_rmep_eth));
    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_add_eth_to_asic(lchip, &p_sys_rmep_eth->com));

    if (p_sys_rmep_eth->is_p2p_mode)
    {
        return CTC_E_NONE;
    }

    if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
    {
        DsEgressXcOamRmepHashKey_m ds_eth_oam_rmep_hash_key;
        sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));

        SetDsEgressXcOamRmepHashKey(V, hashType_f,  &ds_eth_oam_rmep_hash_key, EGRESSXCOAMHASHTYPE_RMEP);
        SetDsEgressXcOamRmepHashKey(V, mepIndex_f,   &ds_eth_oam_rmep_hash_key,  p_sys_lmep->lmep_index);
        SetDsEgressXcOamRmepHashKey(V, rmepId_f,  &ds_eth_oam_rmep_hash_key, p_sys_rmep_eth->rmep_id);
        SetDsEgressXcOamRmepHashKey(V, rmepIndex_f,   &ds_eth_oam_rmep_hash_key,  p_sys_rmep_eth->com.rmep_index);
        SetDsEgressXcOamRmepHashKey(V, valid_f,   &ds_eth_oam_rmep_hash_key, 1);

        CTC_ERROR_RETURN( sys_goldengate_oam_key_write_io(lchip, DsEgressXcOamRmepHashKey_t,
                                                        p_sys_rmep_eth->key_index ,
                                                        &ds_eth_oam_rmep_hash_key ));
        SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LOOKUP_KEY, 1, 1);
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_y1731_t* p_sys_rmep_eth)
{
    sys_oam_lmep_com_t* p_sys_lmep = NULL;
    SYS_OAM_DBG_FUNC();

    p_sys_lmep = p_sys_rmep_eth->com.p_sys_lmep;
    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_lmep->p_sys_chan->master_chipid))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_del_from_asic(lchip, &p_sys_rmep_eth->com));

    if (!p_sys_rmep_eth->is_p2p_mode)
    {
        if (p_sys_rmep_eth->key_alloc == SYS_OAM_KEY_HASH)
        {
            DsEgressXcOamRmepHashKey_m ds_eth_oam_rmep_hash_key;
            sal_memset(&ds_eth_oam_rmep_hash_key, 0, sizeof(ds_eth_oam_rmep_hash_key));
            CTC_ERROR_RETURN( sys_goldengate_oam_key_delete_io(lchip, DsEgressXcOamRmepHashKey_t,
                                                           p_sys_rmep_eth->key_index ,
                                                           &ds_eth_oam_rmep_hash_key ));
            SYS_OAM_TBL_CNT(lchip, SYS_OAM_TBL_LOOKUP_KEY, 0, 1);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_rmep_update_asic(uint8 lchip, ctc_oam_update_t* p_rmep_param,
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

    CTC_ERROR_RETURN(_sys_goldengate_oam_rmep_update_eth_asic(lchip, &p_sys_rmep_eth->com));

    return CTC_E_NONE;
}

#define COMMON_SET "Function Begin"

int32
_sys_goldengate_cfm_oam_set_port_tunnel_en(uint8 lchip, uint16 gport, uint8 enable)
{
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    /* call the sys interface from port module */
    field_value = enable;
    ret = sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_OAM_TUNNEL_EN, field_value);

    return ret;

}

int32
_sys_goldengate_cfm_oam_set_port_edge_en(uint8 lchip, uint16 gport, uint8 enable)
{
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }
    field_value = enable;
    ret = sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT, CTC_BOTH_DIRECTION, field_value);

    return ret;

}

int32
_sys_goldengate_cfm_oam_set_port_oam_en(uint8 lchip, uint16 gport, uint8 dir, uint8 enable)
{

    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    SYS_OAM_DBG_INFO("gport:%d, dir:%d, enable:%d\n", gport, dir, enable);

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    /* call the sys interface from port module */
    switch (dir)
    {
    case CTC_INGRESS:
        field_value = enable;
        break;

    case CTC_EGRESS:
        field_value = enable;
        break;

    case CTC_BOTH_DIRECTION:
        field_value = enable;
        break;

    default:
        break;
    }

    ret = sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_OAM_VALID, dir, field_value);
    return ret;

}

int32
_sys_goldengate_cfm_oam_set_relay_all_to_cpu_en(uint8 lchip, uint8 enable)
{
    uint32 cmd           = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    field_value = enable;
    cmd = DRV_IOW(OamHeaderAdjustCtl_t, OamHeaderAdjustCtl_relayAllToCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_tx_vlan_tpid(uint8 lchip, uint8 tpid_index, uint16 tpid)
{
    uint32 cmd           = 0;
    OamEtherTxCtl_m eth_tx_ctl;

    SYS_OAM_DBG_FUNC();

    if (tpid_index > 3)
    {
        return CTC_E_OAM_INVALID_MEP_TPID_INDEX;
    }

    sal_memset(&eth_tx_ctl, 0, sizeof(OamEtherTxCtl_m));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));

    DRV_SET_FIELD_V(OamEtherTxCtl_t, OamEtherTxCtl_tpid0_f + tpid_index , &eth_tx_ctl, tpid);
    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_rx_vlan_tpid(uint8 lchip, uint8 tpid_index, uint16 tpid)
{
    uint32 cmd           = 0;
    OamParserEtherCtl_m par_eth_ctl;

    SYS_OAM_DBG_FUNC();

    if (tpid_index > 3)
    {
        return CTC_E_OAM_INVALID_MEP_TPID_INDEX;
    }


    sal_memset(&par_eth_ctl, 0, sizeof(oam_parser_ether_ctl_t));
    cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));

    DRV_SET_FIELD_V(OamParserEtherCtl_t, OamParserEtherCtl_gTpid_0_svlanTpid_f + tpid_index , &par_eth_ctl, tpid);
    cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_send_id(uint8 lchip, ctc_oam_eth_senderid_t* p_sender_id)
{
    uint32 cmd      = 0;

    oam_ether_send_id_t oam_ether_send_id;

    SYS_OAM_DBG_FUNC();

    sal_memset(&oam_ether_send_id, 0, sizeof(oam_ether_send_id));
    cmd = DRV_IOW(OamEtherSendId_t, DRV_ENTRY_FLAG);


    DRV_SET_FIELD_V(OamEtherSendId_t, OamEtherSendId_sendIdLength_f , &oam_ether_send_id,
                        p_sender_id->sender_id_len);
    SetOamEtherSendId(V, sendIdByte0to2_f, &oam_ether_send_id,
                        ( ((p_sender_id->sender_id[0] & 0xFF) << 16) |
                        ((p_sender_id->sender_id[1] & 0xFF) << 8) |
                        ((p_sender_id->sender_id[2] & 0xFF) << 0) ));
    DRV_SET_FIELD_V(OamEtherSendId_t, OamEtherSendId_sendIdByte3to6_f , &oam_ether_send_id,
                        (((p_sender_id->sender_id[3] & 0xFF) << 24) |
                        ((p_sender_id->sender_id[4] & 0xFF) << 16) |
                        ((p_sender_id->sender_id[5] & 0xFF) << 8) |
                        ((p_sender_id->sender_id[6] & 0xFF) << 0)));
    DRV_SET_FIELD_V(OamEtherSendId_t, OamEtherSendId_sendIdByte7to10_f , &oam_ether_send_id,
                        (((p_sender_id->sender_id[7] & 0xFF) << 24) |
                        ((p_sender_id->sender_id[8] & 0xFF) << 16) |
                        ((p_sender_id->sender_id[9] & 0xFF) << 8) |
                        ((p_sender_id->sender_id[10] & 0xFF) << 0)));
    DRV_SET_FIELD_V(OamEtherSendId_t, OamEtherSendId_sendIdByte11to14_f , &oam_ether_send_id,
                        (((p_sender_id->sender_id[11] & 0xFF) << 24) |
                        ((p_sender_id->sender_id[12] & 0xFF) << 16) |
                        ((p_sender_id->sender_id[13] & 0xFF) << 8) |
                        ((p_sender_id->sender_id[14] & 0xFF) << 0)));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_ether_send_id));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_big_ccm_interval(uint8 lchip, uint8 big_interval)
{
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(big_interval, 7);

    field_value = big_interval;

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_minIntervalToCpu_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_port_lm_en(uint8 lchip, uint16 gport, uint8 enable)
{
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    field_value = enable;

    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_ETHER_LM_VALID,
            CTC_BOTH_DIRECTION, field_value));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_bridge_mac(uint8 lchip, mac_addr_t* bridge_mac)
{
    uint32 cmd      = 0;
    mac_addr_t  mac;
    hw_mac_addr_t  src_mac;
    OamRxProcEtherCtl_m rx_ether_ctl;
    OamEtherTxCtl_m eth_tx_ctl;

    SYS_OAM_DBG_FUNC();

    sal_memcpy(&mac, bridge_mac, sizeof(mac_addr_t));

    sal_memset(&rx_ether_ctl, 0, sizeof(oam_rx_proc_ether_ctl_t));
    cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    sal_memset(&eth_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));

    SetOamRxProcEtherCtl(V, bridgeMacHigh_f, &rx_ether_ctl,  (mac[0] << 8) |  (mac[1] << 0));
    SetOamRxProcEtherCtl(V, bridgeMacLow_f,
                        &rx_ether_ctl, (mac[2] << 24) | (mac[3] << 16) |  (mac[4] << 8) |  (mac[5] << 0));

    SYS_GOLDENGATE_SET_HW_MAC(src_mac, mac);
    drv_goldengate_set_field(OamEtherTxCtl_t, OamEtherTxCtl_txBridgeMac_f, &eth_tx_ctl, src_mac);

    cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

    cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));


    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_get_bridge_mac(uint8 lchip, mac_addr_t* bridge_mac)
{
    uint32 cmd      = 0;
    mac_addr_t  mac;
    hw_mac_addr_t  hw_mac;
    OamEtherTxCtl_m eth_tx_ctl;

    SYS_OAM_DBG_FUNC();

    sal_memset(&eth_tx_ctl, 0, sizeof(oam_ether_tx_ctl_t));
    cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));
    drv_goldengate_get_field(OamEtherTxCtl_t, OamEtherTxCtl_txBridgeMac_f, &eth_tx_ctl, (uint32*)&hw_mac);

    SYS_GOLDENGATE_SET_USER_MAC(mac, hw_mac);
    sal_memcpy(bridge_mac, &mac, sizeof(mac_addr_t));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_ach_channel_type(uint8 lchip, uint16 value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    field_val = value;
    cmd = DRV_IOW(ParserLayer4AchCtl_t, ParserLayer4AchCtl_achY1731Type_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_achY1731ChanType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(OamEtherTxCtl_t, OamEtherTxCtl_achY1731ChanType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_get_ach_channel_type(uint8 lchip, uint16* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    cmd = DRV_IOR(ParserLayer4AchCtl_t, ParserLayer4AchCtl_achY1731Type_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *value = field_val;

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_set_lbm_process_by_asic(uint8 lchip, uint8 enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_lbmProcByCpu_f);

    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_set_lmm_process_by_asic(uint8 lchip, uint8 enable)
{

    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_lmmProcByCpu_f);

    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_set_dm_process_by_asic(uint8 lchip, uint8 enable)
{

    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_dmProcByCpu_f);

    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_set_slm_process_by_asic(uint8 lchip, uint8 enable)
{

    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamRxProcEtherCtl_t, OamRxProcEtherCtl_smProcByCpu_f);

    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}


int32
_sys_goldengate_cfm_oam_set_lbr_sa_use_lbm_da(uint8 lchip, uint8 enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_lbrSaUsingLbmDa_f);

    field_value = enable;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_set_lbr_sa_use_bridge_mac(uint8 lchip, uint8 enable)
{

    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_lbrSaType_f);

    field_value = enable;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_set_l2vpn_oam_mode(uint8 lchip, uint8 enable)
{

    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();


    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_vplsOamUseVlanWaive_f);
    field_value = enable ? 1 : 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_vplsOamUseVlanWaive_f);
    field_value = enable ? 1 : 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_oamFidEn_f);
    field_value = enable ? 0 : 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;

}

int32
 _sys_goldengate_cfm_set_if_status(uint8 lchip, uint32 gport, uint8 if_status)
{
    uint16  lport       = 0;
    uint32 field = 0;

    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Invalid global port \n");
        return CTC_E_INVALID_PORT;
    }
    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);

    field = if_status & 0xFF;

    CTC_ERROR_RETURN(sys_goldengate_port_set_if_status(lchip, lport, field));

    return CTC_E_NONE;


}


#define COMMON_GET "Function Begin"

int32
_sys_goldengate_cfm_oam_get_port_oam_en(uint8 lchip, uint16 gport, uint8 dir, uint8* enable)
{


    uint16 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* call the sys interface from port module */
    switch (dir)
    {
    case CTC_INGRESS:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_etherOamValid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        *enable = field_value;
        break;

    case CTC_EGRESS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_etherOamValid_f);
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
_sys_goldengate_cfm_oam_get_port_tunnel_en(uint8 lchip, uint16 gport, uint8* enable)
{

    uint16 lport = 0;
    uint32 cmd  = 0;
    int32 ret = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* call the sys interface from port module */

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_oamTunnelEn_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
    *enable = field_value;

    return ret;

}

int32
_sys_goldengate_cfm_oam_get_big_ccm_interval(uint8 lchip, uint8* big_interval)
{

    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();


    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_minIntervalToCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *big_interval = field_value;

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_get_port_lm_en(uint8 lchip, uint16 gport, uint8* enable)
{

    uint16 lport     = 0;
    uint32 cmd      = 0;
    uint32 field_value    = 0;

    SYS_OAM_DBG_FUNC();

    CTC_GLOBAL_PORT_CHECK(gport)

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_etherLmValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_get_lbm_process_by_asic(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_lbmProcByCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;

}


int32
_sys_goldengate_cfm_oam_get_lmm_process_by_asic(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_lmmProcByCpu_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_get_dm_process_by_asic(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_dmProcByCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;

}

int32
_sys_goldengate_cfm_oam_get_slm_process_by_asic(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamRxProcEtherCtl_t, OamRxProcEtherCtl_smProcByCpu_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;

}


int32
_sys_goldengate_cfm_oam_get_lbr_sa_use_lbm_da(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();

    cmd = DRV_IOR(OamHeaderEditCtl_t, OamHeaderEditCtl_lbrSaUsingLbmDa_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_get_l2vpn_oam_mode(uint8 lchip, uint8* enable)
{
    uint32 cmd      = 0;
    uint32 field_value = 0;

    SYS_OAM_DBG_FUNC();
    cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_vplsOamUseVlanWaive_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    *enable = field_value;

    return CTC_E_NONE;
}

int32
_sys_goldengate_cfm_oam_get_if_status(uint8 lchip, uint16 gport, uint8* if_status)
{

    uint16  lport       = 0;
    uint32 field = 0;
    uint32 cmd = 0;

    SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, " Invalid global port \n");
        return CTC_E_INVALID_PORT;
    }

    cmd = DRV_IOR(DsPortProperty_t, DsPortProperty_ifStatus_f);
    DRV_IOCTL(lchip, lport, cmd, &field);
    *if_status = field & 0xFF;

    return CTC_E_NONE;

}


int32
_sys_goldengate_cfm_register_init(uint8 lchip)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 gchip_id = 0;
    uint32 nexthop_ptr_bridge = 0;
    uint8 rdi_index       = 0;
    uint16 mp_port_id = 0;
    uint32 discard_map_val[2];
    uint32 discard_up_lm_en[2];
    uint32 discard_down_lm_en[2];
    uint32 ether_defect_to_rdi0 = 0;
    uint32 ether_defect_to_rdi1  = 0;
    mac_addr_t bridge_mac;
    uint32 ccm_mac_da[2];
    uint8 i = 0;
    uint8  defect_type      = 0;
    uint8  defect_sub_type  = 0;
    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));

    /*Parser*/
    field_val = 1;
    cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_ethOamTypeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    {
        OamParserLayer2ProtocolCam_m par_l2_cam;
        sal_memset(&par_l2_cam, 0, sizeof(OamParserLayer2ProtocolCam_m));
        SetOamParserLayer2ProtocolCam(V, camValue0_f, &par_l2_cam, 0x408902);
        SetOamParserLayer2ProtocolCam(V, camMask0_f, &par_l2_cam, 0x40FFFF);
        SetOamParserLayer2ProtocolCam(V, additionalOffset0_f, &par_l2_cam, 0);
        SetOamParserLayer2ProtocolCam(V, camLayer3Type0_f, &par_l2_cam, 8);
        cmd = DRV_IOW(OamParserLayer2ProtocolCam_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_l2_cam));
    }

    {
        OamParserLayer2ProtocolCamValid_m par_l2_cam_valid;
        sal_memset(&par_l2_cam_valid, 0, sizeof(OamParserLayer2ProtocolCamValid_m));
        SetOamParserLayer2ProtocolCamValid(V, layer2CamEntryValid_f, &par_l2_cam_valid, 1);
        cmd = DRV_IOW(OamParserLayer2ProtocolCamValid_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_l2_cam_valid));
    }

    {
        OamParserEtherCtl_m par_eth_ctl;
        sal_memset(&par_eth_ctl, 0, sizeof(OamParserEtherCtl_m));
        cmd = DRV_IOR(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));
        SetOamParserEtherCtl(V, minMepId_f, &par_eth_ctl, 0);
        SetOamParserEtherCtl(V, maxMepId_f, &par_eth_ctl, 8191);
        SetOamParserEtherCtl(V, cfmPduMacDaMdLvlCheckEn_f, &par_eth_ctl, 1);
        SetOamParserEtherCtl(V, tlvLengthCheckEn_f, &par_eth_ctl, 1);
        SetOamParserEtherCtl(V, allowNonZeroOui_f, &par_eth_ctl, 1);
        SetOamParserEtherCtl(V, cfmPduMinLength_f, &par_eth_ctl, 0);
        SetOamParserEtherCtl(V, cfmPduMaxLength_f, &par_eth_ctl, 132);
        SetOamParserEtherCtl(V, firstTlvOffsetChk_f, &par_eth_ctl, 70);
        SetOamParserEtherCtl(V, mdNameLengthChk_f, &par_eth_ctl, 43);
        SetOamParserEtherCtl(V, maIdLengthChk_f, &par_eth_ctl, 48);
        SetOamParserEtherCtl(V, minIfStatusTlvValue_f, &par_eth_ctl, 0);
        SetOamParserEtherCtl(V, maxIfStatusTlvValue_f, &par_eth_ctl, 7);
        SetOamParserEtherCtl(V, minPortStatusTlvValue_f, &par_eth_ctl, 0);
        SetOamParserEtherCtl(V, maxPortStatusTlvValue_f, &par_eth_ctl, 3);
        SetOamParserEtherCtl(V, ignoreEthOamVersion_f, &par_eth_ctl, 1);
        SetOamParserEtherCtl(V, invalidCcmIntervalCheckEn_f, &par_eth_ctl, 1);
        SetOamParserEtherCtl(V, maxLengthField_f, &par_eth_ctl, 1536);
        SetOamParserEtherCtl(V, gTpid_0_svlanTpid_f, &par_eth_ctl, 0x8100);
        SetOamParserEtherCtl(V, gTpid_1_svlanTpid_f, &par_eth_ctl, 0x8100);
        SetOamParserEtherCtl(V, gTpid_2_svlanTpid_f, &par_eth_ctl, 0x9100);
        SetOamParserEtherCtl(V, gTpid_3_svlanTpid_f, &par_eth_ctl, 0x88a8);
        SetOamParserEtherCtl(V, cvlanTpid_f, &par_eth_ctl, 0x8100);
        SetOamParserEtherCtl(V,  y1731MccOpcode_f , &par_eth_ctl, 41);
        SetOamParserEtherCtl(V,  y1731SccOpcode_f , &par_eth_ctl, 50);
        SetOamParserEtherCtl(V,  y1731SlmOpcode_f , &par_eth_ctl, 55);
        SetOamParserEtherCtl(V,  y1731SlrOpcode_f , &par_eth_ctl, 54);

        cmd = DRV_IOW(OamParserEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &par_eth_ctl));
    }

    /*linkoam and service oam with tagged*/
    field_val = 0;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_taggedLinkOamEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_untaggedServiceOamEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Lookup*/
    field_val = 1;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_oamLookupUserVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(EpeAclQosCtl_t, EpeAclQosCtl_oamLookupUserVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(EpeHdrAdjustCtl_t, EpeHdrAdjustCtl_oamFidEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Ipe/Epe process*/
    field_val = 1;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_oamTunnelCheckType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_untaggedServiceOamEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = gchip_id;              /*global chipid*/
    cmd = DRV_IOW(IpeOamCtl_t, IpeOamCtl_linkOamDestChipId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_oamBypassPolicingDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_oamBypassStormCtlDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    discard_map_val[0] = 0xFFFFFFFF;
    CTC_BIT_UNSET(discard_map_val[0], 27);/*IPEDISCARDTYPE_DS_FWD_DESTID_DIS for mpls bfd, spec bug7449*/
    discard_map_val[1] = 0xFFFFFFFF;
    {
        IpeFwdCtl_m ipe_fwd_ctl;
        sal_memset(&ipe_fwd_ctl,0,sizeof(IpeFwdCtl_m));
        cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));
        SetIpeFwdCtl(A, oamDiscardBitmap_f, &ipe_fwd_ctl, discard_map_val);
        cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));
    }

    discard_map_val[0] = 0xFFFFFFFF;
    discard_map_val[1] = 0xFFFFFFFF;
    {
        EpeHeaderEditCtl_m epe_header_edit_ctl;
        sal_memset(&epe_header_edit_ctl,0,sizeof(EpeHeaderEditCtl_m));
        cmd = DRV_IOR(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));
        SetEpeHeaderEditCtl(A, oamDiscardBitmap_f, &epe_header_edit_ctl, discard_map_val);
        cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));
    }

    field_val = 1;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_oamIgnorePayloadOperation_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_linkOamDiscardEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*OamEngine Header Adjust*/
    {
        OamHeaderAdjustCtl_m adjust_ctl;
        sal_memset(&adjust_ctl, 0, sizeof(OamHeaderAdjustCtl_m));
        cmd = DRV_IOR(OamHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));
        SetOamHeaderAdjustCtl(V, oamPduBypassOamEngine_f, &adjust_ctl, 0);
        SetOamHeaderAdjustCtl(V, relayAllToCpu_f, &adjust_ctl, 0);
        cmd = DRV_IOW(OamHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &adjust_ctl));
    }

    /*OamEngine Rx Process*/
    {
        OamRxProcEtherCtl_m rx_ether_ctl;
        sal_memset(&rx_ether_ctl, 0, sizeof(OamRxProcEtherCtl_m));
        cmd = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));

        SetOamRxProcEtherCtl(V, macStatusChangeChkEn_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, minIntervalToCpu_f, &rx_ether_ctl, 7);
        SetOamRxProcEtherCtl(V, lbmMacdaCheckEn_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, lmmMacdaCheckEn_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, dmMacDaCheckEn_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, lbmProcByCpu_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, lmmProcByCpu_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, dmProcByCpu_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, smProcByCpu_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, ccmWithUnknownTlvTocpu_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, alarmSrcMacMismatch_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, dUnexpPeriodTimerCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, dMegLvlTimerCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, dMismergeTimerCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, dUnexpMepTimerCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, rmepWhileCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, ethCsfLos_f, &rx_ether_ctl, 0);
        SetOamRxProcEtherCtl(V, ethCsfFdi_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, ethCsfRdi_f, &rx_ether_ctl, 2);
        SetOamRxProcEtherCtl(V, ethCsfClear_f, &rx_ether_ctl, 3);
        SetOamRxProcEtherCtl(V, array2_0_rxCsfWhileCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, array2_1_rxCsfWhileCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, array2_2_rxCsfWhileCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, array2_3_rxCsfWhileCfg_f, &rx_ether_ctl, 14);
        SetOamRxProcEtherCtl(V, array_0_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_1_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_2_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_3_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_4_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_5_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_6_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, array_7_csfWhileCfg_f, &rx_ether_ctl, 1);
        SetOamRxProcEtherCtl(V, seqnumFailReportThrd_f, &rx_ether_ctl, 0x1F);
        SetOamRxProcEtherCtl(V, unknownCcmThrd_f, &rx_ether_ctl, 3);

        ether_defect_to_rdi0 = 0;
        ether_defect_to_rdi1 = 0;
        for (i = 0; i < CTC_GOLDENGATE_OAM_DEFECT_NUM; i++)
        {
            _sys_goldengate_oam_get_rdi_defect_type(lchip, (1 << i), &defect_type, &defect_sub_type);
            rdi_index = (defect_type << 3) | defect_sub_type;
            if (rdi_index < 32)
            {
                CTC_BIT_SET(ether_defect_to_rdi0, rdi_index);
            }
            else
            {
                CTC_BIT_SET(ether_defect_to_rdi1, (rdi_index - 32));
            }
        }

        rdi_index = (0 << 3) | 1;/*CTC_OAM_DEFECT_MAC_STATUS_CHANGE*/
        if (rdi_index < 32)
        {
            CTC_BIT_UNSET(ether_defect_to_rdi0, rdi_index);
        }
        else
        {
            CTC_BIT_UNSET(ether_defect_to_rdi1, (rdi_index - 32));
        }
        rdi_index = (0 << 3) | 7;/*CTC_OAM_DEFECT_SRC_MAC_MISMATCH*/
        if (rdi_index < 32)
        {
            CTC_BIT_UNSET(ether_defect_to_rdi0, rdi_index);
        }
        else
        {
            CTC_BIT_UNSET(ether_defect_to_rdi1, (rdi_index - 32));
        }

        SetOamRxProcEtherCtl(V, etherDefectToRdi0_f, &rx_ether_ctl, ether_defect_to_rdi0);
        SetOamRxProcEtherCtl(V, etherDefectToRdi1_f, &rx_ether_ctl, ether_defect_to_rdi1);

        cmd = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &rx_ether_ctl));
    }

    /*OamEngine Header Edit*/
    {
        OamHeaderEditCtl_m hdr_edit_ctl;
        sal_memset(&hdr_edit_ctl, 0, sizeof(OamHeaderEditCtl_m));
        cmd = DRV_IOR(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_edit_ctl));
        SetOamHeaderEditCtl(V, iLoopDestMap_f, &hdr_edit_ctl, SYS_ENCODE_DESTMAP(gchip_id, SYS_RSV_PORT_ILOOP_ID));
        SetOamHeaderEditCtl(V, lbrSaType_f, &hdr_edit_ctl, 0);
        SetOamHeaderEditCtl(V, lbrSaUsingLbmDa_f, &hdr_edit_ctl, 1);
        SetOamHeaderEditCtl(V, localChipId_f, &hdr_edit_ctl, gchip_id);
        SetOamHeaderEditCtl(V, txCcmByEpe_f, &hdr_edit_ctl, 0);

        CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
                                                               &nexthop_ptr_bridge));
        SetOamHeaderEditCtl(V, bypassNextHopPtr_f, &hdr_edit_ctl, nexthop_ptr_bridge);
        cmd = DRV_IOW(OamHeaderEditCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hdr_edit_ctl));
    }

    /*OamEngine Tx*/
    {
        OamEtherTxCtl_m eth_tx_ctl;
        sal_memset(&eth_tx_ctl, 0, sizeof(OamEtherTxCtl_m));
        cmd = DRV_IOR(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));
        SetOamEtherTxCtl(V, txCcmByEpe_f, &eth_tx_ctl, 0);
        SetOamEtherTxCtl(V, lkupLinkAggMem_f, &eth_tx_ctl, 1);
        SetOamEtherTxCtl(V, iloopChanId_f, &eth_tx_ctl,  SYS_RSV_PORT_ILOOP_ID);
        SetOamEtherTxCtl(V, oamChipId_f, &eth_tx_ctl, gchip_id);
        SetOamEtherTxCtl(V, ccmEtherType_f, &eth_tx_ctl, 0x8902);
        SetOamEtherTxCtl(V, ccmVersion_f, &eth_tx_ctl, 0);
        SetOamEtherTxCtl(V, ccmOpcode_f, &eth_tx_ctl, 1);
        SetOamEtherTxCtl(V, ccmFirstTlvOffset_f, &eth_tx_ctl, 70);
        SetOamEtherTxCtl(V, tpid0_f, &eth_tx_ctl, 0x8100);
        SetOamEtherTxCtl(V, tpid1_f, &eth_tx_ctl, 0x8100);
        SetOamEtherTxCtl(V, tpid2_f, &eth_tx_ctl, 0x9100);
        SetOamEtherTxCtl(V, tpid3_f, &eth_tx_ctl, 0x88a8);
        SetOamEtherTxCtl(V, oamDropPort_f, &eth_tx_ctl, SYS_RSV_PORT_DROP_ID);

        CTC_ERROR_RETURN(sys_goldengate_nh_get_resolved_offset(lchip, SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH,
                                                              &nexthop_ptr_bridge));
        SetOamEtherTxCtl(V, bridgeNexthopPtr_f, &eth_tx_ctl, nexthop_ptr_bridge);


        /*mcast mac da 0x0180c2000300(last 3bit is level)*/
        ccm_mac_da[1] = 0x30;
        ccm_mac_da[0]= 0x18400006;
        drv_goldengate_set_field(OamEtherTxCtl_t, OamEtherTxCtl_cfmMcastAddr_f, &eth_tx_ctl, ccm_mac_da);

        cmd = DRV_IOW(OamEtherTxCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &eth_tx_ctl));
    }

    bridge_mac[0] = 0x0;
    bridge_mac[1] = 0x11;
    bridge_mac[2] = 0x11;
    bridge_mac[3] = 0x11;
    bridge_mac[4] = 0x11;
    bridge_mac[5] = 0x11;
    _sys_goldengate_cfm_oam_set_bridge_mac(lchip, &bridge_mac);

    {
        DsPortProperty_m ds_port_property;
        sal_memset(&ds_port_property, 0, sizeof(DsPortProperty_m));
        cmd = DRV_IOW(DsPortProperty_t, DRV_ENTRY_FLAG);

        for (mp_port_id = 0; mp_port_id < 192; mp_port_id++)
        {
            SetDsPortProperty(V, macSaByte_f, &ds_port_property, ((0x22 << 8 | mp_port_id)));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, mp_port_id, cmd, &ds_port_property));
        }
    }

    /*OamEngine Update*/
     /*#define CHIP_CORE_FRE  450*/
    {
        OamUpdateCtl_m oam_update_ctl;
        sal_memset(&oam_update_ctl, 0, sizeof(OamUpdateCtl_m));
        cmd = DRV_IOR(OamUpdateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));
        SetOamUpdateCtl(V, ituDefectClearMode_f, &oam_update_ctl, 1);
        SetOamUpdateCtl(V, cntShiftWhileCfg_f, &oam_update_ctl, 4);
        SetOamUpdateCtl(V, cciWhileCfg_f, &oam_update_ctl, 4);
        SetOamUpdateCtl(V, genRdiByDloc_f, &oam_update_ctl, 1);

        cmd = DRV_IOW(OamUpdateCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &oam_update_ctl));
    }

    /*ipe oam register*/
    {
        IpeOamCtl_m ipe_oam_ctl;
        sal_memset(&ipe_oam_ctl, 0, sizeof(IpeOamCtl_m));
        cmd = DRV_IOR(IpeOamCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_oam_ctl));
        discard_up_lm_en[0] = 0xFFFFFFFF;
        discard_up_lm_en[1] = 0xFFFFFFFF;
        discard_down_lm_en[0] = 0xFFFFFFFF;
        discard_down_lm_en[1] = 0xFFFFFFFF;
        SetIpeOamCtl(V, lmProactiveEtherOamPacket_f, &ipe_oam_ctl, 1);
        SetIpeOamCtl(V, linkOamDestChipId_f, &ipe_oam_ctl, gchip_id);
        SetIpeOamCtl(V, exception2DiscardDownLmEn_f, &ipe_oam_ctl, 1);
        SetIpeOamCtl(V, exception2DiscardUpLmEn_f, &ipe_oam_ctl, 1);
        SetIpeOamCtl(V, discardLinkLmEn_f, &ipe_oam_ctl, 1);
        SetIpeOamCtl(V, lmGreenPacket_f, &ipe_oam_ctl, 0);
        SetIpeOamCtl(A, discardDownLmEn_f, &ipe_oam_ctl, discard_up_lm_en);
        SetIpeOamCtl(A, discardUpLmEn_f, &ipe_oam_ctl, discard_down_lm_en);

        cmd = DRV_IOW(IpeOamCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_oam_ctl));
    }

    /*epe oam register*/
    {
        EpeOamCtl_m epe_oam_ctl;
        sal_memset(&epe_oam_ctl, 0, sizeof(EpeOamCtl_m));
        cmd = DRV_IOR(EpeOamCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_oam_ctl));
        SetEpeOamCtl(V, lmProactiveEtherOamPacket_f, &epe_oam_ctl, 1);
        SetEpeOamCtl(V, discardLinkLmEn_f, &epe_oam_ctl, 1);
        SetEpeOamCtl(V, lmGreenPacket_f, &epe_oam_ctl, 0);
        SetEpeOamCtl(A, discardDownLmEn_f, &epe_oam_ctl, discard_down_lm_en);
        SetEpeOamCtl(A, discardUpLmEn_f, &epe_oam_ctl, discard_up_lm_en);

        cmd = DRV_IOW(EpeOamCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_oam_ctl));
    }

    /*for spec bug7441*/
    field_val = 1;
    cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_srcDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_RSV_PORT_DROP_ID, cmd, &field_val));

    return CTC_E_NONE;
}


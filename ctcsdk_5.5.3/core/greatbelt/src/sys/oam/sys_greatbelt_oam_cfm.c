/**
 @file ctc_greatbelt_oam_cfm.c

 @date 2011-11-9

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
#include "ctc_parser.h"

#include "sys_greatbelt_chip.h"

#include "sys_greatbelt_oam.h"
#include "sys_greatbelt_oam_com.h"
#include "sys_greatbelt_oam_cfm_db.h"
#include "sys_greatbelt_oam_debug.h"
#include "sys_greatbelt_oam_cfm.h"
#include "sys_greatbelt_port.h"
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
***************************************************************************/

STATIC int32
_sys_greatbelt_cfm_maid_check_param(uint8 lchip, void* p_maid_param, bool is_add)
{
    ctc_oam_maid_t* p_eth_maid = NULL;
    uint8 maid_entry_num = 0;

    CTC_PTR_VALID_CHECK(p_maid_param);
    p_eth_maid = (ctc_oam_maid_t*)p_maid_param;

    SYS_OAM_DBG_PARAM("Maid: '%s', len: 0x%x !!\n",
                      p_eth_maid->maid, p_eth_maid->maid_len);

    /* get the entry number and check the length and length type */
    switch (P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format)
    {
    case CTC_OAM_MAID_LEN_16BYTES:
        maid_entry_num = 2;
        break;

    case CTC_OAM_MAID_LEN_32BYTES:
        maid_entry_num = 4;
        break;

    case CTC_OAM_MAID_LEN_48BYTES:
        maid_entry_num = 6;
        break;

    default:
        break;
    }

    if (0 == P_COMMON_OAM_MASTER_GLB(lchip).maid_len_format)
    {
        if (p_eth_maid->maid_len == 0)
        {
            SYS_OAM_DBG_ERROR("Invalid maid_len: 0x%x\n", p_eth_maid->maid_len);
            return CTC_E_OAM_MAID_LENGTH_INVALID;
        }
    }
    else
    {
        if (p_eth_maid->maid_len > maid_entry_num * 8)
        {
            SYS_OAM_DBG_ERROR("Invalid maid_len: 0x%x\n", p_eth_maid->maid_len);
            return CTC_E_OAM_MAID_LENGTH_INVALID;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_chan_check_param(uint8 lchip, ctc_oam_lmep_t* p_chan_param, bool is_add)
{
    uint16 gport       = 0;
    uint8  is_up       = 0;
    uint8  is_link_oam = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_chan_param);

    SYS_OAM_DBG_PARAM("Chan: gport->0x%x, vlan->%d, flag->0x%x!!\n",
                      p_chan_param->key.u.eth.gport, p_chan_param->key.u.eth.vlan_id, p_chan_param->key.flag);

    gport = p_chan_param->key.u.eth.gport;
    is_link_oam = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM);
    is_up = CTC_FLAG_ISSET(p_chan_param->key.flag, CTC_OAM_KEY_FLAG_UP_MEP);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, gport));

    if (is_link_oam)
    {
        /* gport must be local physical port */
        if (CTC_IS_LINKAGG_PORT(gport))
        {
            return CTC_E_INVALID_GLOBAL_PORT;
        }


        if (is_up)
        {
            return CTC_E_OAM_CHAN_NOT_DOWN_DIRECTION;
        }
    }
    else
    {   /* check vlan id if not link oam */
        CTC_VLAN_RANGE_CHECK(p_chan_param->key.u.eth.vlan_id);
    }

    if (is_add)
    {
        SYS_GLOBAL_CHIPID_CHECK(p_chan_param->u.y1731_lmep.master_gchip);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_lmep_check_param(uint8 lchip, ctc_oam_lmep_t* p_lmep_param, bool is_add)
{
    ctc_oam_y1731_lmep_t* p_eth_lmep = NULL;
    uint32 max_mep_index = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_lmep_param);

    p_eth_lmep = &(p_lmep_param->u.y1731_lmep);

    SYS_OAM_DBG_PARAM("Lmep: gport->0x%x, vlan->%d, flag->0x%x!!\n",
                      p_lmep_param->key.u.eth.gport, p_lmep_param->key.u.eth.vlan_id, p_lmep_param->key.flag);

    SYS_OAM_DBG_PARAM("Lmep: level->%d !!\n", p_lmep_param->key.u.eth.md_level);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_lmep_param->key.u.eth.gport));
    /* check md level */
    if (CTC_FLAG_ISSET(p_lmep_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
    {
        if (0 != p_lmep_param->key.u.eth.md_level) /* linkoam always level 0*/
        {
            return CTC_E_OAM_INVALID_MD_LEVEL;
        }
    }
    else /* service oam */
    {
        if (p_lmep_param->key.u.eth.md_level > SYS_OAM_MAX_MD_LEVEL
            || p_lmep_param->key.u.eth.md_level < 1)
        {
            return CTC_E_OAM_INVALID_MD_LEVEL;
        }
    }

    if (is_add)
    {
        SYS_OAM_DBG_PARAM("Lmep: maid->'%s', len: 0x%x !!\n",
                          p_lmep_param->maid.maid, p_lmep_param->maid.maid_len);

        SYS_OAM_DBG_PARAM("Lmep: mep id->%d, interval->%d, ethTypeIndex->%d!!\n",
                          p_eth_lmep->mep_id, p_eth_lmep->ccm_interval, p_eth_lmep->tpid_index);

        /* check tipid */
        if (p_eth_lmep->tpid_index < CTC_PARSER_L2_TPID_SVLAN_TPID_0
            || p_eth_lmep->tpid_index > CTC_PARSER_L2_TPID_SVLAN_TPID_1)
        {
            return CTC_E_OAM_INVALID_MEP_TPID_INDEX;
        }

        /* check ccm interval */
        if (p_eth_lmep->ccm_interval < SYS_OAM_MIN_CCM_INTERVAL
            || p_eth_lmep->ccm_interval > SYS_OAM_MAX_CCM_INTERVAL)
        {
            return CTC_E_OAM_INVALID_MEP_CCM_INTERVAL;
        }

        /* check mep id */
        if (p_eth_lmep->mep_id < SYS_OAM_MIN_MEP_ID
            || p_eth_lmep->mep_id > SYS_OAM_MAX_MEP_ID)
        {
            return CTC_E_OAM_INVALID_MEP_ID;
        }

        /* check lm value*/
        if (CTC_FLAG_ISSET(p_lmep_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_LM_EN))
        {
            if(p_lmep_param->u.y1731_lmep.lm_type >= CTC_OAM_LM_TYPE_MAX)
            {
                return CTC_E_OAM_INVALID_LM_TYPE;
            }

            if(p_lmep_param->u.y1731_lmep.lm_cos_type >= CTC_OAM_LM_COS_TYPE_MAX)
            {
                return CTC_E_OAM_INVALID_LM_COS_TYPE;
            }

            CTC_COS_RANGE_CHECK(p_lmep_param->u.y1731_lmep.lm_cos);
        }

        /* check csf value*/
        if (CTC_FLAG_ISSET(p_lmep_param->u.y1731_lmep.flag, CTC_OAM_Y1731_LMEP_FLAG_TX_CSF_EN))
        {
            if(p_lmep_param->u.y1731_lmep.tx_csf_type >= CTC_OAM_CSF_TYPE_MAX)
            {
                return CTC_E_OAM_INVALID_CSF_TYPE;
            }
        }

        /* check mep index*/
        if (!P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
        {
            max_mep_index = _sys_greatbelt_oam_get_mep_entry_num(lchip);
            if (max_mep_index&&((p_lmep_param->lmep_index < SYS_OAM_MIN_MEP_INDEX)
                || (p_lmep_param->lmep_index >= max_mep_index)))
            {
                return CTC_E_OAM_INVALID_MEP_INDEX;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_rmep_check_param(uint8 lchip, ctc_oam_rmep_t* p_rmep_param, bool is_add)
{
    ctc_oam_y1731_rmep_t* p_eth_rmep = NULL;
    uint32 max_mep_index = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_rmep_param);

    SYS_OAM_DBG_PARAM("Lmep: gport->0x%x, vlan->%d, flag->0x%x!!\n",
                      p_rmep_param->key.u.eth.gport, p_rmep_param->key.u.eth.vlan_id, p_rmep_param->key.flag);

    p_eth_rmep = &(p_rmep_param->u.y1731_rmep);
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_rmep_param->key.u.eth.gport));

    SYS_OAM_DBG_PARAM("Rmep: level->%d !!\n", p_rmep_param->key.u.eth.md_level);

    /* check md level */
    if (CTC_FLAG_ISSET(p_rmep_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
    {
        if (0 != p_rmep_param->key.u.eth.md_level) /* linkoam always level 0*/
        {
            return CTC_E_OAM_INVALID_MD_LEVEL;
        }
    }
    else /* service oam */
    {
        if (p_rmep_param->key.u.eth.md_level > SYS_OAM_MAX_MD_LEVEL
            || p_rmep_param->key.u.eth.md_level < 1)
        {
            return CTC_E_OAM_INVALID_MD_LEVEL;
        }
    }

    if (is_add)
    {
        SYS_OAM_DBG_PARAM("Rmep: rmep id->%d !!\n", p_eth_rmep->rmep_id);

        /* check mep id */
        if (p_eth_rmep->rmep_id < SYS_OAM_MIN_MEP_ID
            || p_eth_rmep->rmep_id > SYS_OAM_MAX_MEP_ID)
        {
            return CTC_E_OAM_INVALID_MEP_ID;
        }

        /* check mep index*/
        if (!P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)
        {
            max_mep_index = _sys_greatbelt_oam_get_mep_entry_num(lchip);
            if ((p_rmep_param->rmep_index < SYS_OAM_MIN_MEP_INDEX)
                || (p_rmep_param->rmep_index >= max_mep_index))
            {
                return CTC_E_OAM_INVALID_MEP_INDEX;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_check_update_param(uint8 lchip, ctc_oam_update_t* p_update_param, bool is_local)
{
    ctc_oam_update_t* p_eth_update = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_update_param);

    p_eth_update = p_update_param;

    SYS_OAM_DBG_PARAM("Lmep: gport->0x%x, vlan->%d, flag->0x%x!!\n",
                      p_eth_update->key.u.eth.gport, p_eth_update->key.u.eth.vlan_id, p_eth_update->key.flag);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_eth_update->key.u.eth.gport));

    SYS_OAM_DBG_PARAM("Rmep: level->%d !!\n", p_eth_update->key.u.eth.md_level);

    /* check is local or remote*/
    if (p_eth_update->is_local != is_local)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* check md level */
    if (p_eth_update->is_local && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_PORT_STATUS != p_eth_update->update_type)
        && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_IF_STATUS != p_eth_update->update_type)
        && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE != p_eth_update->update_type)
        && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE != p_eth_update->update_type)
        && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS != p_eth_update->update_type)
        && (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT != p_eth_update->update_type))
    {
        if (CTC_FLAG_ISSET(p_eth_update->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
        {
            if (0 != p_eth_update->key.u.eth.md_level) /* linkoam always level 0*/
            {
                return CTC_E_OAM_INVALID_MD_LEVEL;
            }
        }
        else /* service oam */
        {
            if ((p_eth_update->key.u.eth.md_level > SYS_OAM_MAX_MD_LEVEL
                || p_eth_update->key.u.eth.md_level < 1)
                &&(p_eth_update->update_type != CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MASTER_GCHIP))
            {
                return CTC_E_OAM_INVALID_MD_LEVEL;
            }
        }
    }

    if (!is_local)
    {
        SYS_OAM_DBG_PARAM("Rmep: rmep id->%d !!\n", p_eth_update->rmep_id);

        /* check mep id */
        if (p_eth_update->rmep_id < SYS_OAM_MIN_MEP_ID
            || p_eth_update->rmep_id > SYS_OAM_MAX_MEP_ID)
        {
            return CTC_E_OAM_INVALID_MEP_ID;
        }
    }

    if (CTC_OAM_MEP_TYPE_ETH_Y1731 == p_eth_update->key.mep_type)
    {
        if (p_eth_update->is_local)
        {
            switch (p_eth_update->update_type)
            {
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_PORT_STATUS:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_IF_STATUS:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
                return CTC_E_OAM_INVALID_CFG_IN_MEP_TYPE;

            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
                CTC_COS_RANGE_CHECK(p_eth_update->update_value);
                break;
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MASTER_GCHIP:
                SYS_GLOBAL_CHIPID_CHECK(p_eth_update->update_value);
                break;
            default:
                break;
            }
        }
        else
        {
            switch (p_eth_update->update_type)
            {
            case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_SEQ_FAIL_CLEAR:
            case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
            case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_PORT_INTF_MIS:
                return CTC_E_OAM_INVALID_CFG_IN_MEP_TYPE;

            default:
                break;
            }
        }
    }
    else if (CTC_OAM_MEP_TYPE_ETH_1AG == p_eth_update->key.mep_type)
    {
        if (p_eth_update->is_local)
        {
            switch (p_eth_update->update_type)
            {
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE:
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
                return CTC_E_OAM_INVALID_CFG_IN_MEP_TYPE;
            case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MASTER_GCHIP:
                SYS_GLOBAL_CHIPID_CHECK(p_eth_update->update_value);
                break;
            default:
                break;
            }
        }
        else
        {
            switch (p_eth_update->update_type)
            {
            case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN:
            case CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF:
                return CTC_E_OAM_INVALID_CFG_IN_MEP_TYPE;

            default:
                break;
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_mip_check_param(uint8 lchip, ctc_oam_mip_t* p_mip_param, bool is_add)
{

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_mip_param);

    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_mip_param->key.u.eth.gport));

    /* check vlan id if not link oam */
    CTC_VLAN_RANGE_CHECK(p_mip_param->key.u.eth.vlan_id);

    if (CTC_FLAG_ISSET(p_mip_param->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
    {
        return CTC_E_OAM_INVALID_MD_LEVEL;
    }
    else /* service oam */
    {
        if (p_mip_param->key.u.eth.md_level > SYS_OAM_MAX_MD_LEVEL
            || p_mip_param->key.u.eth.md_level < 1)
        {
            return CTC_E_OAM_INVALID_MD_LEVEL;
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_cfm_add_chan(uint8 lchip, void* p_chan, void** p_sys_chan)
{
    ctc_oam_lmep_t* p_chan_param   = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    int32 ret = CTC_E_NONE;

    /*1. check chan param valid*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_chan_check_param(lchip, p_chan_param, TRUE));

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_chan_param->key);
    if (NULL != p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_EXIST;
        *p_sys_chan = p_sys_chan_eth;
        goto RETURN;
    }

    /*3. build chan sys node*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_build_node(lchip, p_chan_param);
    if (NULL == p_sys_chan_eth)
    {
        SYS_OAM_DBG_ERROR("Chan: build node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    /*4. build chan index*/
    ret = _sys_greatbelt_cfm_chan_build_index(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: build index fail!!\n");
        goto STEP_3;
    }

    /*5. add chan to db*/
    ret = _sys_greatbelt_cfm_chan_add_to_db(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add db fail!!\n");
        goto STEP_4;
    }

    /*6. add key/chan to asic*/
    ret = _sys_greatbelt_cfm_chan_add_to_asic(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: add asic fail!!\n");
        goto STEP_5;
    }

    *p_sys_chan = p_sys_chan_eth;

    goto RETURN;

STEP_5:
    _sys_greatbelt_cfm_chan_del_from_db(lchip, p_sys_chan_eth);
STEP_4:
    _sys_greatbelt_cfm_chan_free_index(lchip, p_sys_chan_eth);
STEP_3:
    _sys_greatbelt_cfm_chan_free_node(lchip, p_sys_chan_eth);

RETURN:
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_remove_chan(uint8 lchip, void* p_chan)
{
    ctc_oam_lmep_t* p_chan_param = (ctc_oam_lmep_t*)p_chan;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    int32 ret = CTC_E_NONE;

    /*1. check param valid*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_chan_check_param(lchip, p_chan_param, FALSE));

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_chan_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if ((CTC_SLISTCOUNT(p_sys_chan_eth->com.lmep_list) > 0) || p_sys_chan_eth->mip_bitmap
        || p_sys_chan_eth->down_mep_bitmap || p_sys_chan_eth->up_mep_bitmap)
    {
        ret = CTC_E_OAM_CHAN_LMEP_EXIST;
        goto RETURN;
    }

    /*3. remove key/chan from asic*/
    ret = _sys_greatbelt_cfm_chan_del_from_asic(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del asic fail!!\n");
        goto RETURN;
    }

    /*4. remove chan from db*/
    ret = _sys_greatbelt_cfm_chan_del_from_db(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: del db fail!!\n");
        goto RETURN;
    }

    /*5. free chan index*/
    ret = _sys_greatbelt_cfm_chan_free_index(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

    /*6. free chan node*/
    ret = _sys_greatbelt_cfm_chan_free_node(lchip, p_sys_chan_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Chan: free index fail!!\n");
        goto RETURN;
    }

RETURN:
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_add_lmep(uint8 lchip, void* p_lmep)
{
    ctc_oam_lmep_t* p_lmep_param = (ctc_oam_lmep_t*)p_lmep;
    sys_oam_maid_com_t* p_sys_maid_eth = NULL;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    int32            ret = CTC_E_NONE;
    uint8            md_level = 0;

    SYS_OAM_DBG_FUNC();

    /*1. check param valid (lmep)*/

    ret = _sys_greatbelt_cfm_lmep_check_param(lchip, p_lmep_param, TRUE);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

    OAM_LOCK(lchip);

    /*1. check chan exist or add chan */
    ret = _sys_greatbelt_cfm_add_chan(lchip, p_lmep_param, (void**)&p_sys_chan_eth);
    if ((CTC_E_NONE != ret) && (CTC_E_OAM_CHAN_ENTRY_EXIST != ret))
    {
        goto RETURN;
    }

    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    /*2. lookup maid and check exist*/
    p_lmep_param->maid.mep_type = p_lmep_param->key.mep_type;
    p_sys_maid_eth = _sys_greatbelt_oam_maid_lkup(lchip, &p_lmep_param->maid);
    if ((NULL == p_sys_maid_eth) && (!g_gb_oam_master[lchip]->no_mep_resource))
    {
        ret = CTC_E_OAM_MAID_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    /*3. lookup chan and check exist*/
    md_level = p_lmep_param->key.u.eth.md_level;
    if (IS_BIT_SET((p_sys_chan_eth->down_mep_bitmap), md_level) ||
        IS_BIT_SET((p_sys_chan_eth->up_mep_bitmap), md_level) ||
        IS_BIT_SET((p_sys_chan_eth->mip_bitmap), md_level))
    {
        ret = CTC_E_OAM_CHAN_MD_LEVEL_EXIST;
        goto RETURN;
    }

    if (CTC_SLISTCOUNT(p_sys_chan_eth->com.lmep_list) >= SYS_OAM_MAX_MEP_NUM_PER_CHAN)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NUM_EXCEED;
        goto RETURN;
    }

    /*4. lookup lmep and check exist*/

    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL != p_sys_lmep_eth)
    {
        ret = CTC_E_OAM_CHAN_LMEP_EXIST;
        goto RETURN;
    }

    /*5. build lmep sys node*/
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_build_node(lchip, p_lmep_param);
    if (NULL == p_sys_lmep_eth)
    {
        SYS_OAM_DBG_ERROR("Lmep: build node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    p_sys_lmep_eth->com.p_sys_maid = p_sys_maid_eth;
    p_sys_lmep_eth->com.p_sys_chan = &p_sys_chan_eth->com;
    if (p_sys_maid_eth)
    {
        p_sys_maid_eth->ref_cnt++;
    }
    /*6. build lmep index */
    ret = _sys_greatbelt_cfm_lmep_build_index(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: build index fail!!\n");
        goto STEP_5;
    }

    /*7. add lmep to db */
    ret = _sys_greatbelt_cfm_lmep_add_to_db(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: add db fail!!\n");
        goto STEP_6;
    }

    /*8. write lmep asic*/
    ret = _sys_greatbelt_cfm_lmep_add_to_asic(lchip, p_lmep_param, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: add asic fail!!\n");
        goto STEP_7;
    }

    /*9. update chan*/
    ret = _sys_greatbelt_cfm_chan_update(lchip, p_sys_lmep_eth, TRUE);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: update chan fail!!\n");
        goto STEP_8;
    }

    /*10. copy mep index*/
    p_lmep_param->lmep_index = p_sys_lmep_eth->com.lmep_index;
     /*SYS_OAM_SET_ID_BY_IDX(p_sys_lmep_eth->com.lmep_index, p_lmep_param->endpoint_id);*/

    goto RETURN;

STEP_8:
    _sys_greatbelt_cfm_lmep_del_from_asic(lchip, p_sys_lmep_eth);
STEP_7:
    _sys_greatbelt_cfm_lmep_del_from_db(lchip, p_sys_lmep_eth);
STEP_6:
    _sys_greatbelt_cfm_lmep_free_index(lchip, p_sys_lmep_eth);
STEP_5:
    _sys_greatbelt_cfm_lmep_free_node(lchip, p_sys_lmep_eth);
    if (p_sys_maid_eth)
    {
        p_sys_maid_eth->ref_cnt--;
    }
RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_remove_lmep(uint8 lchip, void* p_lmep)
{
    ctc_oam_lmep_t* p_lmep_param = (ctc_oam_lmep_t*)p_lmep;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    sys_oam_maid_com_t* p_sys_maid     = NULL;
    int32           ret = CTC_E_NONE;
    uint8           md_level = 0;

    uint32          lmep_index = 0;

    SYS_OAM_DBG_FUNC();

    /*1. check param valid (lmep)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_lmep_check_param(lchip, p_lmep_param, FALSE));

    OAM_LOCK(lchip);

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }
    if (p_sys_chan_eth->key.service_queue_key.have_service_key)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_EXIST;
        goto RETURN;
    }

    md_level = p_lmep_param->key.u.eth.md_level;

    if (!IS_BIT_SET((p_sys_chan_eth->down_mep_bitmap), md_level) &&
        !IS_BIT_SET((p_sys_chan_eth->up_mep_bitmap), md_level) &&
        !IS_BIT_SET((p_sys_chan_eth->mip_bitmap), md_level))
    {
        ret = CTC_E_OAM_CHAN_MD_LEVEL_NOT_EXIST;
        goto RETURN;
    }


    /*3. lookup lmep and check exist*/

    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        ret = CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        goto RETURN;
    }

    /*4 check if rmep list */
    if (CTC_SLISTCOUNT(p_sys_lmep_eth->com.rmep_list) > 0)
    {
        ret = CTC_E_OAM_RMEP_EXIST;
        goto RETURN;
    }

    p_sys_maid = p_sys_lmep_eth->com.p_sys_maid;
    lmep_index = p_sys_lmep_eth->com.lmep_index;

    /*5. update chan*/
    ret = _sys_greatbelt_cfm_chan_update(lchip, p_sys_lmep_eth, FALSE);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: update chan fail!!\n");
        goto RETURN;
    }

    /*6. remove lmep from asic*/
    ret = _sys_greatbelt_cfm_lmep_del_from_asic(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: del asic fail!!\n");
        goto RETURN;
    }

    /*7. remove lmep from db*/
    ret = _sys_greatbelt_cfm_lmep_del_from_db(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: del db fail!!\n");
        goto RETURN;
    }

    /*8. free lmep index*/
    ret = _sys_greatbelt_cfm_lmep_free_index(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: free index fail!!\n");
        goto RETURN;
    }

    /*9. free lmep node*/
    ret = _sys_greatbelt_cfm_lmep_free_node(lchip, p_sys_lmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Lmep: free node fail!!\n");
        goto RETURN;
    }
    if (p_sys_maid)
    {
        p_sys_maid->ref_cnt--;
    }
    /*10. update chan exist or remove chan */
    ret = _sys_greatbelt_cfm_remove_chan(lchip, p_lmep_param);
    if ((CTC_E_OAM_CHAN_LMEP_EXIST == ret) || (CTC_E_OAM_CHAN_ENTRY_NOT_FOUND == ret))
    {
        ret = CTC_E_NONE;
    }

     /*SYS_OAM_SET_ID_BY_IDX(lmep_index, 0);*/
    p_lmep_param->lmep_index = lmep_index;

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_update_lmep_assigned_level(uint8 lchip, void* p_lmep)
{
    ctc_oam_update_t* p_lmep_param = (ctc_oam_update_t*)p_lmep;

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    uint8            md_level = 0;
    int32            ret = CTC_E_NONE;

    SYS_OAM_DBG_FUNC();

    /*1. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }

    /*2. lookup lmep and check exist*/
    md_level = p_lmep_param->key.u.eth.md_level;
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
    }

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
    }

    /*3. check p2p mode not support update */
    if ((!CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE))
        && ((CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN == p_lmep_param->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE == p_lmep_param->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA == p_lmep_param->update_type)
            || (CTC_OAM_Y1731_LMEP_UPDATE_TYPE_P2P_TX_USE_UC_DA == p_lmep_param->update_type)))
    {
        return CTC_E_OAM_INVALID_OPERATION_ON_P2P_MEP;
    }

    /*4. update lmep db and asic*/
    ret = _sys_greatbelt_cfm_lmep_update_asic(lchip, p_lmep_param, p_sys_lmep_eth);

    return ret;
}

STATIC int32
_sys_greatbelt_cfm_update_lmep(uint8 lchip, void* p_lmep)
{
    ctc_oam_update_t* p_lmep_update = (ctc_oam_update_t*)p_lmep;

    int32            ret = CTC_E_NONE;

    SYS_OAM_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_greatbelt_cfm_check_update_param(lchip, p_lmep_update, TRUE));

    if (p_lmep_update->is_local)
    {
        OAM_LOCK(lchip);

        switch (p_lmep_update->update_type)
        {
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MEP_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_DM_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_COS_EXP:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_RDI:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LMEP_SF_STATE:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_P2P_TX_USE_UC_DA:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_CCM_SEQ_NUM_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_TYPE:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TX_CSF_USE_UC_DA:
            ret = _sys_greatbelt_cfm_update_lmep_assigned_level(lchip, (void*)p_lmep_update);
            break;

        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_PORT_STATUS:
            ret = _sys_greatbelt_cfm_update_lmep_port_status(lchip, p_lmep_update);
            break;

        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_IF_STATUS:
            ret = _sys_greatbelt_cfm_update_lmep_if_status(lchip, p_lmep_update);
            break;

        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_EN:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_TYPE:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS_TYPE:
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_LM_COS:
            ret = _sys_greatbelt_cfm_update_lmep_lm(lchip, p_lmep_update);
            break;

        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_TRPT:
            ret = _sys_greatbelt_cfm_update_lmep_trpt(lchip, p_lmep_update);
            break;
        case CTC_OAM_Y1731_LMEP_UPDATE_TYPE_MASTER_GCHIP:
            ret = _sys_greatbelt_cfm_update_master_gchip(lchip, p_lmep_update);
            break;

        default:
            ret = CTC_E_INVALID_PARAM;
        }

        OAM_UNLOCK(lchip);
    }
    else
    {
        ret = CTC_E_INVALID_PARAM;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_cfm_add_rmep(uint8 lchip, void* p_rmep)
{
    ctc_oam_rmep_t* p_rmep_param = (ctc_oam_rmep_t*)p_rmep;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    sys_oam_rmep_y1731_t* p_sys_rmep_eth = NULL;
    int32 ret           = CTC_E_NONE;
    uint8 md_level      = 0;
    uint32 rmep_id      = 0;

    SYS_OAM_DBG_FUNC();

    /*1. check param valid (rmep)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_rmep_check_param(lchip, p_rmep_param, TRUE));

    OAM_LOCK(lchip);

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_rmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    /*3. lookup lmep and check exist*/
    md_level = p_rmep_param->key.u.eth.md_level;
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        ret = CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        goto RETURN;
    }

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        ret = CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
        goto RETURN;
    }

    /*4. lookup rmep and check exist*/
    rmep_id = p_rmep_param->u.y1731_rmep.rmep_id;
    p_sys_rmep_eth = _sys_greatbelt_cfm_rmep_lkup(lchip, rmep_id, p_sys_lmep_eth);
    if (NULL != p_sys_rmep_eth)
    {
        ret = CTC_E_OAM_RMEP_EXIST;
        goto RETURN;
    }

    /*5. build rmep sys node*/
    p_sys_rmep_eth = _sys_greatbelt_cfm_rmep_build_node(lchip, p_rmep_param);
    if (NULL == p_sys_rmep_eth)
    {
        SYS_OAM_DBG_ERROR("Rmep: build node fail!!\n");
        ret = CTC_E_NO_MEMORY;
        goto RETURN;
    }

    p_sys_rmep_eth->com.p_sys_lmep = &p_sys_lmep_eth->com;
    p_sys_rmep_eth->is_p2p_mode = CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE);

    /*6. build rmep index */
    ret = _sys_greatbelt_cfm_rmep_build_index(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: build index fail!!\n");
        goto STEP_5;
    }

    /*7. add rmep to db */
    ret = _sys_greatbelt_cfm_rmep_add_to_db(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: add db fail!!\n");
        goto STEP_6;
    }

    /*8. write rmep asic*/
    ret = _sys_greatbelt_cfm_rmep_add_to_asic(lchip, p_rmep_param, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: add asic fail!!\n");
        goto STEP_7;
    }

    /*9. copy mep index*/
    p_rmep_param->rmep_index = p_sys_rmep_eth->com.rmep_index;
     /*SYS_OAM_SET_ID_BY_IDX(p_sys_rmep_eth->com.rmep_index, p_rmep_param->endpoint_id);*/

    goto RETURN;

STEP_7:
    _sys_greatbelt_cfm_rmep_del_from_db(lchip, p_sys_rmep_eth);
STEP_6:
    _sys_greatbelt_cfm_rmep_free_index(lchip, p_sys_rmep_eth);
STEP_5:
    _sys_greatbelt_cfm_rmep_free_node(lchip, p_sys_rmep_eth);

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_remove_rmep(uint8 lchip, void* p_rmep)
{
    ctc_oam_rmep_t* p_rmep_param = (ctc_oam_rmep_t*)p_rmep;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    sys_oam_rmep_y1731_t* p_sys_rmep_eth = NULL;
    int32 ret           = CTC_E_NONE;
    uint8 md_level  = 0;
    uint32 rmep_id  = 0;
    uint32 rmep_index = 0;

    SYS_OAM_DBG_FUNC();

    /*1. check param valid (rmep)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_rmep_check_param(lchip, p_rmep_param, FALSE));

    OAM_LOCK(lchip);

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_rmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    /*3. lookup lmep and check exist*/
    md_level = p_rmep_param->key.u.eth.md_level;
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        ret = CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        goto RETURN;
    }

    /*4. lookup rmep and check exist*/
    rmep_id = p_rmep_param->u.y1731_rmep.rmep_id;
    p_sys_rmep_eth = _sys_greatbelt_cfm_rmep_lkup(lchip, rmep_id, p_sys_lmep_eth);
    if (NULL == p_sys_rmep_eth)
    {
        ret = CTC_E_OAM_RMEP_NOT_FOUND;
        goto RETURN;
    }

    rmep_index = p_sys_rmep_eth->com.rmep_index;
    /*5. remove rmep from asic*/
    ret = _sys_greatbelt_cfm_rmep_del_from_asic(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: del asic fail!!\n");
        goto RETURN;
    }

    /*6. remove rmep from db*/
    ret = _sys_greatbelt_cfm_rmep_del_from_db(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: del db fail!!\n");
        goto RETURN;
    }

    /*7. free rmep index*/
    ret = _sys_greatbelt_cfm_rmep_free_index(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: free index fail!!\n");
        goto RETURN;
    }

    /*8. free rmep node*/
    ret = _sys_greatbelt_cfm_rmep_free_node(lchip, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: free node fail!!\n");
        goto RETURN;
    }

     /*SYS_OAM_SET_ID_BY_IDX(rmep_index, 0);*/

RETURN:
    p_rmep_param->rmep_index = rmep_index;
    OAM_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_update_rmep(uint8 lchip, void* p_rmep)
{
    ctc_oam_update_t* p_rmep_param = (ctc_oam_update_t*)p_rmep;

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    sys_oam_rmep_y1731_t* p_sys_rmep_eth = NULL;
    uint8 md_level  = 0;
    uint32 rmep_id  = 0;
    int32 ret = CTC_E_NONE;

    SYS_OAM_DBG_FUNC();

    /*1. check param valid (chan)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_check_update_param(lchip, p_rmep_param, FALSE));

    OAM_LOCK(lchip);

    /*2. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_rmep_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    /*3. lookup lmep and check exist*/
    md_level = p_rmep_param->key.u.eth.md_level;
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        ret = CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        goto RETURN;
    }

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        ret = CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
        goto RETURN;
    }

    if ((!(CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_P2P_MODE)))
        && ((CTC_OAM_Y1731_RMEP_UPDATE_TYPE_CSF_EN == p_rmep_param->update_type)
            || (CTC_OAM_Y1731_RMEP_UPDATE_TYPE_DCSF == p_rmep_param->update_type)))
    {
        ret = CTC_E_OAM_INVALID_OPERATION_ON_P2P_MEP;
        goto RETURN;
    }

    /*4. lookup rmep and check exist*/
    rmep_id = p_rmep_param->rmep_id;
    p_sys_rmep_eth = _sys_greatbelt_cfm_rmep_lkup(lchip, rmep_id, p_sys_lmep_eth);
    if (NULL == p_sys_rmep_eth)
    {
        ret = CTC_E_OAM_RMEP_NOT_FOUND;
        goto RETURN;
    }

    /*5. update lmep db and asic*/
    ret = _sys_greatbelt_cfm_rmep_update_asic(lchip, p_rmep_param, p_sys_rmep_eth);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Rmep: update asic fail!!\n");
        goto RETURN;
    }

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_greatbelt_cfm_add_mip(uint8 lchip, void* p_mip)
{
    ctc_oam_mip_t* p_mip_param = NULL;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    ctc_oam_lmep_t      chan_param;

    uint8 md_level      = 0;
    int32 ret = CTC_E_NONE;

    p_mip_param = p_mip;

    sal_memset(&chan_param, 0, sizeof(ctc_oam_lmep_t));
    sal_memcpy(&chan_param.key, &p_mip_param->key, sizeof(ctc_oam_key_t));
    chan_param.u.y1731_lmep.master_gchip = p_mip_param->master_gchip;

    md_level = p_mip_param->key.u.eth.md_level;
    /*1. check param valid (mip)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_mip_check_param(lchip, p_mip_param, TRUE));

    /*2. check & add chan*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_mip_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = _sys_greatbelt_cfm_add_chan(lchip, &chan_param, (void**)&p_sys_chan_eth);
        if (CTC_E_NONE != ret)
        {
            goto RETURN;
        }
    }

    if (IS_BIT_SET((p_sys_chan_eth->down_mep_bitmap), md_level) ||
        IS_BIT_SET((p_sys_chan_eth->up_mep_bitmap), md_level) ||
        IS_BIT_SET((p_sys_chan_eth->mip_bitmap), md_level))
    {
        ret = CTC_E_OAM_CHAN_MD_LEVEL_EXIST;
        goto RETURN;
    }

    /*3.update db&asic*/
    ret = _sys_greatbelt_cfm_chan_update_mip(lchip, p_sys_chan_eth, md_level, TRUE);

RETURN:

    return ret;
}

STATIC int32
_sys_greatbelt_cfm_remove_mip(uint8 lchip, void* p_mip)
{
    ctc_oam_mip_t* p_mip_param = NULL;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    ctc_oam_lmep_t      chan_param;
    uint8 md_level      = 0;

    int32 ret = CTC_E_NONE;

    p_mip_param = p_mip;

    sal_memset(&chan_param, 0, sizeof(ctc_oam_lmep_t));
    sal_memcpy(&chan_param.key, &p_mip_param->key, sizeof(ctc_oam_key_t));
    chan_param.u.y1731_lmep.master_gchip = p_mip_param->master_gchip;

    md_level = p_mip_param->key.u.eth.md_level;
    /*1. check param valid (mip)*/
    CTC_ERROR_RETURN(_sys_greatbelt_cfm_mip_check_param(lchip, p_mip_param, FALSE));

    /*2. check chan*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_mip_param->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if (!IS_BIT_SET((p_sys_chan_eth->down_mep_bitmap), md_level) &&
        !IS_BIT_SET((p_sys_chan_eth->up_mep_bitmap), md_level) &&
        !IS_BIT_SET((p_sys_chan_eth->mip_bitmap), md_level))
    {
        ret = CTC_E_OAM_CHAN_MD_LEVEL_NOT_EXIST;
        goto RETURN;
    }

    /*3. update db&asic*/
    _sys_greatbelt_cfm_chan_update_mip(lchip, p_sys_chan_eth, md_level, FALSE);

    /*4. remove chan*/
    ret = _sys_greatbelt_cfm_remove_chan(lchip, &chan_param);
    if (CTC_E_OAM_CHAN_LMEP_EXIST == ret)
    {
        ret = CTC_E_NONE;
    }

RETURN:

    return ret;

}

STATIC int32
_sys_greatbelt_cfm_set_property(uint8 lchip, void* p_prop)
{
    ctc_oam_property_t* p_prop_param = (ctc_oam_property_t*)p_prop;
    ctc_oam_y1731_prop_t* p_eth_prop   = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_prop);
    p_eth_prop   = &p_prop_param->u.y1731;

    switch (p_eth_prop->cfg_type)
    {
    case CTC_OAM_Y1731_CFG_TYPE_PORT_OAM_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_port_oam_en(lchip, p_eth_prop->gport, p_eth_prop->dir, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_TUNNEL_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_port_tunnel_en(lchip, p_eth_prop->gport, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_ALL_TO_CPU:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_relay_all_to_cpu_en(lchip, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_ADD_EDGE_PORT:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_port_edge_en(lchip, p_eth_prop->gport, 1));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_REMOVE_EDGE_PORT:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_port_edge_en(lchip, p_eth_prop->gport, 0));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_TX_VLAN_TPID:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_tx_vlan_tpid(lchip, (p_eth_prop->value >> 16) & 0xFFFF, p_eth_prop->value & 0xFFFF));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_RX_VLAN_TPID:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_rx_vlan_tpid(lchip, (p_eth_prop->value >> 16) & 0xFFFF, p_eth_prop->value & 0xFFFF));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_SENDER_ID:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_send_id(lchip, (ctc_oam_eth_senderid_t*)p_eth_prop->p_value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_BIG_CCM_INTERVAL_TO_CPU:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_big_ccm_interval(lchip, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_LM_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_port_lm_en(lchip, p_eth_prop->gport, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_BRIDGE_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_bridge_mac(lchip, (mac_addr_t*)p_eth_prop->p_value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_LBM_PROC_IN_ASIC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_lbm_process_by_asic(lchip, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_LBR_SA_USE_LBM_DA:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_lbr_sa_use_lbm_da(lchip, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_LBR_SA_SHARE_MAC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_set_lbr_sa_use_bridge_mac(lchip, p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_TP_ACH_CHAN_TYPE:
        CTC_ERROR_RETURN(
            _sys_greatebelt_cfm_oam_set_ach_channel_type(lchip, (uint16)p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_IF_STATUS:
        CTC_MAX_VALUE_CHECK(p_eth_prop->value, 7);
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_set_if_status(lchip, p_eth_prop->gport, p_eth_prop->value));


    default:

        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_cfm_get_property(uint8 lchip, void* p_prop)
{
    ctc_oam_property_t* p_prop_param = (ctc_oam_property_t*)p_prop;
    ctc_oam_y1731_prop_t* p_eth_prop   = NULL;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_prop);
    p_eth_prop   = &p_prop_param->u.y1731;

    switch (p_eth_prop->cfg_type)
    {
    case CTC_OAM_Y1731_CFG_TYPE_PORT_OAM_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_port_oam_en(lchip, p_eth_prop->gport, p_eth_prop->dir, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_TUNNEL_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_port_tunnel_en(lchip, p_eth_prop->gport, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_BIG_CCM_INTERVAL_TO_CPU:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_big_ccm_interval(lchip, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_PORT_LM_EN:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_port_lm_en(lchip, p_eth_prop->gport, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_LBM_PROC_IN_ASIC:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_lbm_process_by_asic(lchip, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_LBR_SA_USE_LBM_DA:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_lbr_sa_use_lbm_da(lchip, (uint8*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_TP_ACH_CHAN_TYPE:
        CTC_ERROR_RETURN(
            _sys_greatebelt_cfm_oam_get_ach_channel_type(lchip, (uint16*)&p_eth_prop->value));
        break;

    case CTC_OAM_Y1731_CFG_TYPE_IF_STATUS:
        CTC_ERROR_RETURN(
            _sys_greatbelt_cfm_oam_get_if_status(lchip, p_eth_prop->gport, (uint8*)&p_eth_prop->value));
         break;

    default:

        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_cfm_get_stats_info(uint8 lchip, void* p_stat_info)
{
    ctc_oam_stats_info_t* p_sys_stat = NULL;

    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_lmep_y1731_t* p_sys_lmep_eth = NULL;
    uint8            md_level = 0;
    sys_oam_lm_com_t sys_oam_lm_com;
    uint8   lm_type         = 0;
    uint8   lm_cos_type     = 0;
    uint16  lm_base         = 0;
    uint32  lm_index        = 0;
    uint8   cnt             = 0;
    uint8   lvl             = 0;

    SYS_OAM_DBG_FUNC();

    CTC_PTR_VALID_CHECK(p_stat_info);

    p_sys_stat = (ctc_oam_stats_info_t*)p_stat_info;

    /*1. lookup chan and check exist*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_sys_stat->key);
    if (NULL == p_sys_chan_eth)
    {
        return CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
    }

    /*2. lookup lmep and check exist*/
    md_level = p_sys_stat->key.u.eth.md_level;
    p_sys_lmep_eth = _sys_greatbelt_cfm_lmep_lkup(lchip, md_level, p_sys_chan_eth);
    if (NULL == p_sys_lmep_eth)
    {
        return CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
    }

    if (CTC_FLAG_ISSET(p_sys_lmep_eth->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        return CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
    }

    /*3. get mep lm index base, lm type, lm cos type, lm cos*/
    if (!CTC_IS_BIT_SET(p_sys_chan_eth->lm_bitmap, md_level))
    {
        return CTC_E_OAM_MEP_LM_NOT_EN;
    }

    if (CTC_FLAG_ISSET(p_sys_stat->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM))
    {
        lm_cos_type = p_sys_chan_eth->link_lm_cos_type;
        lm_type     = p_sys_chan_eth->link_lm_type;
        lm_index    = p_sys_chan_eth->com.link_lm_index_base;
    }
    else
    {
        lm_cos_type = p_sys_chan_eth->lm_cos_type;
        lm_type     = p_sys_chan_eth->lm_type;
        lm_base     = p_sys_chan_eth->com.lm_index_base;

        for (lvl = 1; lvl <= md_level; lvl++)
        {
            if ((CTC_IS_BIT_SET(p_sys_chan_eth->down_mep_bitmap, lvl))
                ||(CTC_IS_BIT_SET(p_sys_chan_eth->up_mep_bitmap, lvl)))
            {
                cnt++;
            }
        }

        if (cnt > 3)
        {
            return CTC_E_OAM_LM_NUM_RXCEED;
        }

        if ((CTC_OAM_LM_COS_TYPE_ALL_COS == lm_cos_type)
            || (CTC_OAM_LM_COS_TYPE_SPECIFIED_COS == lm_cos_type))
        {
            lm_index = lm_base + 1 * (cnt - 1);
        }
        else if (CTC_OAM_LM_COS_TYPE_PER_COS == lm_cos_type)
        {
            lm_index = lm_base + 8 * (cnt - 1);
        }
    }

    sys_oam_lm_com.lchip = p_sys_lmep_eth->com.lchip;
    sys_oam_lm_com.lm_cos_type  = lm_cos_type;
    sys_oam_lm_com.lm_index     = lm_index;
    sys_oam_lm_com.lm_type      = lm_type;
    _sys_greatbelt_oam_get_stats_info(lchip, &sys_oam_lm_com, p_stat_info);

    return CTC_E_NONE;
}

int32
sys_greatbelt_cfm_add_sevice_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep, ctc_oam_lmep_t* p_lmep_service)
{
    int32 ret = CTC_E_NONE;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_chan_eth_t sys_chan_eth_service;

    SYS_OAM_DBG_FUNC();
    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);
    CTC_PTR_VALID_CHECK(p_lmep_service);

    sal_memset(&sys_chan_eth_service, 0 , sizeof(sys_oam_chan_eth_t));

    /* check */
    CTC_ERROR_RETURN(sys_greatbelt_port_dest_gport_check(lchip, p_lmep_service->key.u.eth.gport));
    if ((p_lmep->key.u.eth.gport == p_lmep_service->key.u.eth.gport)
        || (CTC_FLAG_ISSET(p_lmep->key.flag, CTC_OAM_KEY_FLAG_LINK_SECTION_OAM)))
    {
        return CTC_E_INVALID_PARAM;
    }

    OAM_LOCK(lchip);
    /* 1. lookup DB by external port*/
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }
    if (p_sys_chan_eth->key.service_queue_key.have_service_key)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_EXIST;
        goto RETURN;
    }

    /* 2. mapping chan and lmep */
    sal_memcpy(&sys_chan_eth_service, p_sys_chan_eth, sizeof(sys_oam_chan_eth_t));
    sys_chan_eth_service.key.gport = p_lmep_service->key.u.eth.gport;
    sys_chan_eth_service.lm_index_alloced     = 1;/* no need to alloc lm resource at _sys_greatbelt_cfm_chan_build_index*/
    if (SYS_OAM_KEY_HASH == p_sys_chan_eth->key.com.key_alloc)
    {
        sys_chan_eth_service.key.service_queue_key.no_need_chan = 1;/* no need to alloc chan when add to hash at _sys_greatbelt_cfm_chan_build_index*/
    }

    /* 3. build key index by internal port*/
    ret = _sys_greatbelt_cfm_chan_build_index(lchip, &sys_chan_eth_service);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

    /* 4. write hw by internal port*/
    ret = _sys_greatbelt_cfm_chan_add_to_asic_for_service(lchip, p_sys_chan_eth, &sys_chan_eth_service);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

    /* 5. save internal port info to external port DB*/
    p_sys_chan_eth->key.service_queue_key.have_service_key = 1;
    p_sys_chan_eth->key.service_queue_key.key_alloc = sys_chan_eth_service.key.com.key_alloc;
    p_sys_chan_eth->key.service_queue_key.key_index = sys_chan_eth_service.key.com.key_index;
    p_sys_chan_eth->key.service_queue_key.chan_index = sys_chan_eth_service.com.chan_index;
    p_sys_chan_eth->key.service_queue_key.gport = p_lmep_service->key.u.eth.gport;
    SYS_OAM_DBG_INFO("chan DB: have_internal_key = 0x%x\n", p_sys_chan_eth->key.service_queue_key.have_service_key);
    SYS_OAM_DBG_INFO("chan DB: key_alloc_internal = 0x%x\n", p_sys_chan_eth->key.service_queue_key.key_alloc);
    SYS_OAM_DBG_INFO("chan DB: key_index_internal = 0x%x\n", p_sys_chan_eth->key.service_queue_key.key_index);
    goto RETURN;

RETURN :
    OAM_UNLOCK(lchip);
    return ret;
}

int32
sys_greatbelt_cfm_update_sevice_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    int32 ret = CTC_E_NONE;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_chan_eth_t sys_chan_eth_service;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);

    sal_memset(&sys_chan_eth_service, 0 , sizeof(sys_oam_chan_eth_t));

    OAM_LOCK(lchip);
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }
    if (0 == p_sys_chan_eth->key.service_queue_key.have_service_key)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }
    sal_memcpy(&sys_chan_eth_service, p_sys_chan_eth, sizeof(sys_oam_chan_eth_t));
    sys_chan_eth_service.key.gport = sys_chan_eth_service.key.service_queue_key.gport;
    sys_chan_eth_service.key.com.key_alloc = p_sys_chan_eth->key.service_queue_key.key_alloc;
    sys_chan_eth_service.key.com.key_index = p_sys_chan_eth->key.service_queue_key.key_index;
    sys_chan_eth_service.com.chan_index = p_sys_chan_eth->key.service_queue_key.chan_index;

    ret = _sys_greatbelt_cfm_chan_add_to_asic_for_service(lchip, p_sys_chan_eth, &sys_chan_eth_service);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}


int32
sys_greatbelt_cfm_remove_service_queue_lmep_for_lm_dm(uint8 lchip, ctc_oam_lmep_t* p_lmep)
{
    int32 ret = CTC_E_NONE;
    sys_oam_chan_eth_t* p_sys_chan_eth = NULL;
    sys_oam_chan_eth_t sys_chan_eth_service;

    SYS_OAM_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_lmep);

    sal_memset(&sys_chan_eth_service, 0 , sizeof(sys_oam_chan_eth_t));
    OAM_LOCK(lchip);
    p_sys_chan_eth = _sys_greatbelt_cfm_chan_lkup(lchip, &p_lmep->key);
    if (NULL == p_sys_chan_eth)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }
    if (0 == p_sys_chan_eth->key.service_queue_key.have_service_key)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    sal_memcpy(&sys_chan_eth_service, p_sys_chan_eth, sizeof(sys_oam_chan_eth_t));
    sys_chan_eth_service.key.gport = sys_chan_eth_service.key.service_queue_key.gport;
    sys_chan_eth_service.key.com.key_alloc = p_sys_chan_eth->key.service_queue_key.key_alloc;
    sys_chan_eth_service.key.com.key_index = p_sys_chan_eth->key.service_queue_key.key_index;
    sys_chan_eth_service.com.chan_index = p_sys_chan_eth->key.service_queue_key.chan_index;

    ret = _sys_greatbelt_cfm_chan_del_from_asic_for_service(lchip, p_sys_chan_eth, &sys_chan_eth_service);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

    ret = _sys_greatbelt_cfm_chan_free_index_for_service(lchip, p_sys_chan_eth, &sys_chan_eth_service);
    if (CTC_E_NONE != ret)
    {
        goto RETURN;
    }

    p_sys_chan_eth->key.service_queue_key.have_service_key = 0;

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}


STATIC int32
_sys_greatbelt_cfm_get_mep_info(uint8 lchip, void* p_mep_info)
{
    ctc_oam_mep_info_with_key_t* p_mep_param = (ctc_oam_mep_info_with_key_t*)p_mep_info;

    sys_oam_chan_eth_t*     p_sys_chan  = NULL;
    sys_oam_lmep_y1731_t*   p_sys_lmep  = NULL;
    sys_oam_rmep_y1731_t*   p_sys_rmep  = NULL;
    int32 ret = CTC_E_NONE;
    ctc_oam_mep_info_t mep_info;
    sal_memset(&mep_info, 0, sizeof(ctc_oam_mep_info_t));

    SYS_OAM_DBG_FUNC();

    OAM_LOCK(lchip);

    /*2. lookup chan and check exist*/
    p_sys_chan = _sys_greatbelt_cfm_chan_lkup(lchip, &p_mep_param->key);
    if (NULL == p_sys_chan)
    {
        ret = CTC_E_OAM_CHAN_ENTRY_NOT_FOUND;
        goto RETURN;
    }

    if (SYS_OAM_NO_MEP_OPERATION(lchip, p_sys_chan->com.master_chipid))
    {
        ret = CTC_E_NONE;
        goto RETURN;
    }

    /*3. lookup lmep and check exist*/
    p_sys_lmep = _sys_greatbelt_cfm_lmep_lkup(lchip, p_mep_param->key.u.eth.md_level, p_sys_chan);
    if (NULL == p_sys_lmep)
    {
        ret = CTC_E_OAM_CHAN_LMEP_NOT_FOUND;
        goto RETURN;
    }

    if (CTC_FLAG_ISSET(p_sys_lmep->flag, CTC_OAM_Y1731_LMEP_FLAG_MEP_ON_CPU))
    {
        ret = CTC_E_OAM_INVALID_OPERATION_ON_CPU_MEP;
        goto RETURN;
    }

    /*4. lookup rmep and check exist*/
    p_sys_rmep = _sys_greatbelt_cfm_rmep_lkup(lchip, p_mep_param->rmep_id, p_sys_lmep);
    if (NULL == p_sys_rmep)
    {
        ret = CTC_E_OAM_RMEP_NOT_FOUND;
        goto RETURN;
    }

    mep_info.mep_index = p_sys_rmep->com.rmep_index;
    ret = _sys_greatbelt_oam_get_mep_info(lchip, &mep_info);
    if (CTC_E_NONE != ret)
    {
        SYS_OAM_DBG_ERROR("Get mep info fail!!\n");
        goto RETURN;
    }
    sal_memcpy(&p_mep_param->lmep, &mep_info.lmep, sizeof(ctc_oam_lmep_info_t));
    sal_memcpy(&p_mep_param->rmep, &mep_info.rmep, sizeof(ctc_oam_rmep_info_t));

RETURN:
    OAM_UNLOCK(lchip);
    return ret;
}




int32
sys_greatbelt_cfm_init(uint8 lchip, ctc_oam_global_t* p_eth_glb)
{
    uint8 oam_type = CTC_OAM_MEP_TYPE_ETH_1AG;

    /*************************************************
    *init global param
    *************************************************/
    if (NULL == g_gb_oam_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_cfm_register_init(lchip));

    /*************************************************
    *init callback fun
    *************************************************/

    /*check*/
    SYS_OAM_CHECK_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHECK_MAID)     = _sys_greatbelt_cfm_maid_check_param;

    /*op*/
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_ADD)    = _sys_greatbelt_oam_com_add_maid;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_DEL)    = _sys_greatbelt_oam_com_remove_maid;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_UPDATE) = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_CMP)    = NULL;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_ADD)    = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_DEL)    = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_UPDATE) = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_CMP)    = _sys_greatbelt_cfm_chan_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_UPDATE) = _sys_greatbelt_cfm_update_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_CMP)    = _sys_greatbelt_cfm_lmep_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_UPDATE) = _sys_greatbelt_cfm_update_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_CMP)    = _sys_greatbelt_cfm_rmep_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_mip;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_mip;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_GET_INFO)    = _sys_greatbelt_cfm_get_mep_info;

    oam_type = CTC_OAM_MEP_TYPE_ETH_Y1731;

    /*check*/
    SYS_OAM_CHECK_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHECK_MAID)     = _sys_greatbelt_cfm_maid_check_param;

    /*op*/
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_ADD)    = _sys_greatbelt_oam_com_add_maid;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_DEL)    = _sys_greatbelt_oam_com_remove_maid;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_UPDATE) = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MAID, SYS_OAM_CMP)    = NULL;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_ADD)    = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_DEL)    = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_UPDATE) = NULL;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_CHAN, SYS_OAM_CMP)    = _sys_greatbelt_cfm_chan_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_UPDATE) = _sys_greatbelt_cfm_update_lmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_LMEP, SYS_OAM_CMP)    = _sys_greatbelt_cfm_lmep_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_UPDATE) = _sys_greatbelt_cfm_update_rmep;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_RMEP, SYS_OAM_CMP)    = _sys_greatbelt_cfm_rmep_lkup_cmp;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_ADD)    = _sys_greatbelt_cfm_add_mip;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_MIP, SYS_OAM_DEL)    = _sys_greatbelt_cfm_remove_mip;

    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_STATS, SYS_OAM_CONFIG)    = _sys_greatbelt_cfm_get_stats_info;
    SYS_OAM_FUNC_TABLE(lchip, oam_type, SYS_OAM_GLOB, SYS_OAM_GET_INFO)    = _sys_greatbelt_cfm_get_mep_info;

    SYS_OAM_FUNC_TABLE(lchip, CTC_OAM_PROPERTY_TYPE_Y1731, SYS_OAM_GLOB, SYS_OAM_CONFIG) = _sys_greatbelt_cfm_set_property;

    return CTC_E_NONE;
}


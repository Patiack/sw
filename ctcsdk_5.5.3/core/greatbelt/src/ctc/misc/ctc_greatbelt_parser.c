/**
 @file ctc_greatbelt_parser.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-12-15

 @version v2.0

This file contains parser module function
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_greatbelt_parser.h"
#include "sys_greatbelt_parser.h"
#include "sys_greatbelt_chip.h"

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
/**
 @brief set tpid

 @param[in] type tpid_type CTC_PARSER_L2_TPID_XXX

 @param[in] tpid tpid value

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16 tpid)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_tpid(lchip, type, tpid));
    }
    return CTC_E_NONE;
}

/**
 @brief get tpid with some type

 @param[in] type tpid_type CTC_PARSER_L2_TPID_XXX

 @param[out] tpid tpid value

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_tpid(uint8 lchip, ctc_parser_l2_tpid_t type, uint16* tpid)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_tpid(lchip, type, tpid));
    return CTC_E_NONE;
}

/**
 @brief set max_length,based on the value differentiate type or length field in ethernet frame

 @param[in] max_length max_length value

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_max_length_field(uint8 lchip, uint16 max_length)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_max_length_filed(lchip, max_length));
    }

    return CTC_E_NONE;
}

/**
 @brief get max_length value

 @param[out] max_length max_length value

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_max_length_filed(uint8 lchip, uint16* max_length)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_max_length_filed(lchip, max_length));

    return CTC_E_NONE;
}

/**
 @brief set vlan parser num

 @param[in] vlan_num vlan parser num

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_vlan_parser_num(uint8 lchip, uint8 vlan_num)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
         CTC_ERROR_RETURN(sys_greatbelt_parser_set_vlan_parser_num(lchip, vlan_num));
    }
    return CTC_E_NONE;
}

/**
 @brief get vlan parser num

 @param[out] vlan_num returned value

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_vlan_parser_num(uint8 lchip, uint8* vlan_num)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_vlan_parser_num(lchip, vlan_num));

    return CTC_E_NONE;
}

/**
 @brief set pbb parser header info

 @param[in] pbb_header  pbb parser header info

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* pbb_header)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
         CTC_ERROR_RETURN(sys_greatbelt_parser_set_pbb_header(lchip, pbb_header));
    }

    return CTC_E_NONE;
}

/**
 @brief get pbb parser header info

 @param[in] pbb_header  pbb parser header info

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_pbb_header(uint8 lchip, ctc_parser_pbb_header_t* pbb_header)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_pbb_header(lchip, pbb_header));

    return CTC_E_NONE;
}

/**
 @brief set l2 flex header info

 @param[in] l2flex_header  l2 flex header info

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_l2_flex_header(uint8 lchip, ctc_parser_l2flex_header_t* l2flex_header)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_l2flex_header(lchip, l2flex_header));
    }

    return CTC_E_NONE;
}

/**
 @brief get l2 flex header info

 @param[out] l2flex_header  l2 flex header info

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_l2_flex_header(uint8 lchip, ctc_parser_l2flex_header_t* l2flex_header)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_l2flex_header(lchip, l2flex_header));

    return CTC_E_NONE;
}

/**
 @brief mapping l3type

 @param[in] index  the entry index

 @param[in] entry  the entry

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_map_l3_type(uint8 lchip, uint8 index, ctc_parser_l2_protocol_entry_t* entry)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_mapping_l3_type(lchip, index, entry));
    }

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index

 @param[in] index  the entry index

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_unmap_l3_type(uint8 lchip, uint8 index)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_unmapping_l3_type(lchip, index));
    }

    return CTC_E_NONE;
}

/**
 @brief enable or disable parser layer3 type

 @param[in] l3_type  the layer3 type in ctc_parser_l3_type_t need to enable or disable, 2-15

 @param[in] enable  enable parser layer3 type

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_enable_l3_type(uint8 lchip, ctc_parser_l3_type_t l3_type, bool enable)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_enable_l3_type(lchip, l3_type, enable));
    }

    return CTC_E_NONE;
}

/**
 @brief set trill header info

 @param[in] trill_header  trill header parser info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_trill_header(uint8 lchip, ctc_parser_trill_header_t* trill_header)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_trill_header(lchip, trill_header));
    }

    return CTC_E_NONE;
}

/**
 @brief get trill header info

 @param[out] trill_header  trill header parser info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_trill_header(uint8 lchip, ctc_parser_trill_header_t* trill_header)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_trill_header(lchip, trill_header));

    return CTC_E_NONE;
}

/**
 @brief set parser l3flex header info

 @param[in] l3flex_header  parser l3flex header

 @param[in] index  entry index

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_set_l3_flex_header(uint8 lchip, ctc_parser_l3flex_header_t* l3flex_header)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_l3flex_header(lchip, l3flex_header));
    }

    return CTC_E_NONE;
}

/**
 @brief get parser l3flex l3flex_header info

 @param[in] l3flex_header  parser l3flex header info

 @param[in] index  entry index

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_get_l3_flex_header(uint8 lchip, ctc_parser_l3flex_header_t* l3flex_header)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_l3flex_header(lchip, l3flex_header));

    return CTC_E_NONE;
}

/**
 @brief add l3type,can add a new l3type,addition offset for the type,can get the layer4 type

 @param[in] index  the entry index

 @param[in] entry  the entry

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_map_l4_type(uint8 lchip, uint8 index, ctc_parser_l3_protocol_entry_t* entry)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_mapping_l4_type(lchip, index, entry));
    }

    return CTC_E_NONE;
}

/**
 @brief set the entry invalid based on the index

 @param[in] index  the entry index

 @return SDK_E_XXX

*/
int32
ctc_greatbelt_parser_unmap_l4_type(uint8 lchip, uint8 index)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_unmapping_l4_type(lchip, index));
    }

    return CTC_E_NONE;
}

/**
 @brief set layer4 flex header

 @param[in] l4flex_header  l4 flex header info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_l4_flex_header(uint8 lchip, ctc_parser_l4flex_header_t* l4flex_header)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_l4flex_header(lchip, l4flex_header));
    }

    return CTC_E_NONE;
}

/**
 @brief get layer4 flex header

 @param[out] l4flex_header  l4 flex header info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_l4_flex_header(uint8 lchip, ctc_parser_l4flex_header_t* l4flex_header)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_l4flex_header(lchip, l4flex_header));

    return CTC_E_NONE;
}

/**
 @brief set layer4 app control

 @param[in] l4app_ctl  l4 app ctl info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_l4_app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* l4app_ctl)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_l4app_ctl(lchip, l4app_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief get layer4 app control

 @param[out] l4app_ctl  l4 app ctl info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_l4_app_ctl(uint8 lchip, ctc_parser_l4app_ctl_t* l4app_ctl)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_l4app_ctl(lchip, l4app_ctl));

    return CTC_E_NONE;
}

/**
 @brief set layer4 app data control info,for parser application

 @param[in] index application entry index

 @param[in] entry  the entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_l4_app_data_ctl(uint8 lchip, uint8 index, ctc_parser_l4_app_data_entry_t* entry)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_l4_app_data_ctl(lchip, index, entry));
    }

    return CTC_E_NONE;
}

/**
 @brief get layer4 app data control info,for parser application

 @param[in] index application entry index

 @param[in] entry  the entry

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_l4_app_data_ctl(uint8 lchip, uint8 index, ctc_parser_l4_app_data_entry_t* entry)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_l4_app_data_ctl(lchip, index, entry));

    return CTC_E_NONE;
}

/**
 @brief set ecmp hash field

 @param[in] hash_ctl  the filed of ecmp hash

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_ecmp_hash_field(lchip, hash_ctl));
    }

    return CTC_E_NONE;
}

/**
 @brief get ecmp hash field

 @param[out] hash_ctl  the filed of ecmp hash

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_ecmp_hash_field(uint8 lchip, ctc_parser_ecmp_hash_ctl_t* hash_ctl)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_ecmp_hash_field(lchip, hash_ctl));

    return CTC_E_NONE;
}

/**
 @brief set parser global config info

 @param[in] global_cfg  the field of parser global config info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_set_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_set_global_cfg(lchip, global_cfg));
    }

    return CTC_E_NONE;
}

/**
 @brief get parser global config info

 @param[out] global_cfg  the field of parser global config info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_get_global_cfg(uint8 lchip, ctc_parser_global_cfg_t* global_cfg)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    CTC_ERROR_RETURN(sys_greatbelt_parser_get_global_cfg(lchip, global_cfg));

    return CTC_E_NONE;
}

/**
 @brief init parser module

 @param[in] parser_global_cfg  the field of parser global config info

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_parser_init(uint8 lchip, void* parser_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_parser_global_cfg_t  parser_cfg;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);

    if (parser_global_cfg == NULL)
    {
        /*set default value*/
        sal_memset(&parser_cfg, 0, sizeof(ctc_parser_global_cfg_t));

        /* ipv4 small fragment offset, 0-3,means 0,8,16,24 bytes of small fragment length*/
        parser_cfg.small_frag_offset = 0;

        /* default use crc hash algorithm, if set 0, use xor */
        parser_cfg.ecmp_hash_type = 1;
        parser_cfg.linkagg_hash_type = 1;
        parser_global_cfg = &parser_cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_init(lchip, parser_global_cfg));
    }
    return CTC_E_NONE;
}

int32
ctc_greatbelt_parser_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_PACKET);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_parser_deinit(lchip));
    }

    return CTC_E_NONE;
}


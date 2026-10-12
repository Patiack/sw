/**
 @file ctc_goldengate_l3if.c

 @date 2009-12-10

 @version v2.0

This file contains all L3 interface related Humber APIs implemention.

*/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_goldengate_l3if.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_l3if.h"
/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/

int32
ctc_goldengate_l3if_init(uint8 lchip, void* l3if_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;
    ctc_l3if_global_cfg_t global_cfg;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    if (NULL == l3if_global_cfg)
    {
        sal_memset(&global_cfg, 0, sizeof(ctc_l3if_global_cfg_t));
        global_cfg.max_vrfid_num = 64;
        global_cfg.ipv4_vrf_en = TRUE;
        global_cfg.ipv6_vrf_en = TRUE;
    }
    else
    {
        sal_memcpy(&global_cfg, l3if_global_cfg, sizeof(ctc_l3if_global_cfg_t));
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_init(lchip, &global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_deinit(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;
    int32 ret          = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_L3IF_EXIST,
            ret,
            sys_goldengate_l3if_create(lchip, l3if_id, l3_if));
    }

    /* rollback if error exist */
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_l3if_delete(lchip,l3if_id,l3_if);
    }

    return ret;
}

int32
ctc_goldengate_l3if_destory(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;
    int32 ret          = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_L3IF_NOT_EXIST,
            ret,
            sys_goldengate_l3if_delete(lchip, l3if_id, l3_if));
    }

    return ret;
}

int32
ctc_goldengate_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* p_l3_if, uint16* l3if_id)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_l3if_get_l3if_id(lchip, p_l3_if, l3if_id));

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_set_property(lchip, l3if_id, l3if_prop, value));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* value)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_l3if_get_property(lchip, l3if_id, l3if_prop, value));

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_set_router_mac(lchip, mac_addr));
    }
    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_l3if_get_router_mac(lchip, mac_addr));

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_set_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t router_mac)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_set_interface_router_mac(lchip, l3if_id, router_mac));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_get_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t* router_mac)
{

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_l3if_get_interface_router_mac(lchip, l3if_id, router_mac));

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_set_vrf_stats_en(lchip, p_vrf_stats));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_create_ecmp_if(uint8 lchip, ctc_l3if_ecmp_if_t* p_ecmp_if)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;
    int32 ret          = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ecmp_if);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
            ret,
            sys_goldengate_l3if_create_ecmp_if(lchip, p_ecmp_if));
    }

    /* rollback if error exist */
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_l3if_destory_ecmp_if(lchip, p_ecmp_if->ecmp_if_id);
    }

    return ret;
}

int32
ctc_goldengate_l3if_destory_ecmp_if(uint8 lchip, uint16 ecmp_if_id)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_l3if_destory_ecmp_if(lchip, ecmp_if_id));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_l3if_add_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;
    int32 ret          = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_MEMBER_EXIST,
            ret,
            sys_goldengate_l3if_add_ecmp_if_member(lchip, ecmp_if_id, l3if_id));
    }

    /* rollback if error exist */
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_l3if_remove_ecmp_if_member(lchip, ecmp_if_id, l3if_id);
    }

    return ret;
}

int32
ctc_goldengate_l3if_remove_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id)
{
    uint8 lchip_start  = 0;
    uint8 lchip_end    = 0;
    int32 ret          = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_L3IF);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_MEMBER_NOT_EXIST,
            ret,
            sys_goldengate_l3if_remove_ecmp_if_member(lchip, ecmp_if_id, l3if_id));
    }

    return ret;
}


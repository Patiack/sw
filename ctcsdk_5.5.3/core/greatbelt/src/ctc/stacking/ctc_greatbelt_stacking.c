/**
 @file ctc_greatbelt_stacking.c

 @date 2012-3-14

 @version v2.0
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "ctc_greatbelt_stacking.h"
#include "sys_greatbelt_stacking.h"
#include "sys_greatbelt_chip.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

int32
ctc_greatbelt_stacking_create_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_create_trunk(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_destroy_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_destroy_trunk(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_add_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_port(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_remove_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_remove_trunk_port(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_replace_trunk_ports(uint8 lchip, ctc_stacking_trunk_t* p_trunk, uint32* gports, uint16 mem_ports)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_replace_trunk_ports(lchip, p_trunk->trunk_id, gports, mem_ports));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_get_member_ports(uint8 lchip, uint8 trunk_id, uint32* p_gports, uint8* cnt)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_member_ports(lchip, trunk_id, p_gports, cnt));

    return CTC_E_NONE;
}


int32
ctc_greatbelt_stacking_add_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_add_trunk_rchip(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_remove_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_remove_trunk_rchip(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_get_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk)
{

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_trunk);

    if (p_trunk->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_trunk->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_trunk_rchip(lchip, p_trunk));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_set_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_set_property(lchip, p_prop));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_get_property(uint8 lchip, ctc_stacking_property_t* p_prop)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_property(lchip, p_prop));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_create_keeplive_group(uint8 lchip, uint16 group_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_stacking_create_keeplive_group(lchip, group_id));

    }

    return ret;
}

int32
ctc_greatbelt_stacking_destroy_keeplive_group(uint8 lchip, uint16 group_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_stacking_destroy_keeplive_group(lchip, group_id));
    }

    return ret;
}

int32
ctc_greatbelt_stacking_keeplive_add_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    uint8 loop1 = 0;
    int32 ret   = 0;
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);
    if (p_keeplive->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_keeplive->extend.gchip, lchip)
    }

    if (p_keeplive->trunk_bmp_en)
    {
        if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_PORT)
        {
            return CTC_E_NOT_SUPPORT;
        }
        for (loop1 = 0; loop1 < CTC_STK_TRUNK_BMP_NUM * 32; loop1++)
        {
            if (!CTC_IS_BIT_SET(p_keeplive->trunk_bitmap[loop1 / 32], loop1 % 32))
            {
                continue;
            }
            p_keeplive->trunk_id = loop1;
            ret = sys_greatbelt_stacking_add_keeplive_member(lchip, p_keeplive);
            if ((ret < 0) && (ret != CTC_E_MEMBER_EXIST))
            {
                goto Error;
            }
        }
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_add_keeplive_member(lchip, p_keeplive));
    }

    return CTC_E_NONE;

Error:

    sys_greatbelt_stacking_remove_keeplive_member(lchip, p_keeplive);
    return ret;
}

int32
ctc_greatbelt_stacking_keeplive_remove_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{
    uint8 loop1 = 0;
    int32 ret   = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);
    if (p_keeplive->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_keeplive->extend.gchip, lchip)
    }

    if (p_keeplive->trunk_bmp_en)
    {
        if (p_keeplive->mem_type == CTC_STK_KEEPLIVE_MEMBER_PORT)
        {
            return CTC_E_NOT_SUPPORT;
        }
        for (loop1 = 0; loop1 < CTC_STK_TRUNK_BMP_NUM * 32; loop1++)
        {
            if (!CTC_IS_BIT_SET(p_keeplive->trunk_bitmap[loop1 / 32], loop1 % 32))
            {
                continue;
            }
            p_keeplive->trunk_id = loop1;
            ret = sys_greatbelt_stacking_remove_keeplive_member(lchip, p_keeplive);
            if ((ret < 0) && (ret != CTC_E_MEMBER_NOT_EXIST))
            {
                return ret;
            }
        }
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_remove_keeplive_member(lchip, p_keeplive));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_keeplive_get_members(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive)
{

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_keeplive);

    if (p_keeplive->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_keeplive->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_keeplive_members(lchip, p_keeplive));
    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_set_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);

    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mcast_profile);

    if (p_mcast_profile->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_mcast_profile->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_set_trunk_mcast_profile(lchip, p_mcast_profile));

    return CTC_E_NONE;
}


int32
ctc_greatbelt_stacking_get_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);

    LCHIP_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mcast_profile);

    if (p_mcast_profile->extend.enable)
    {
        SYS_MAP_GCHIP_TO_LCHIP(p_mcast_profile->extend.gchip, lchip)
    }

    CTC_ERROR_RETURN(sys_greatbelt_stacking_get_trunk_mcast_profile(lchip, p_mcast_profile));


    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_init(uint8 lchip, void* p_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_init(lchip, p_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_stacking_deinit(uint8 lchip)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_STACKING);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stacking_deinit(lchip));
    }

    return CTC_E_NONE;
}


/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "sys_greatbelt_sync_ether.h"
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
 @brief  Initialize SyncE module

 @param[in]  sync_ether_global_cfg

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_sync_ether_init(uint8 lchip, void* sync_ether_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_PTP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_sync_ether_init(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_sync_ether_deinit(uint8 lchip)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_PTP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_sync_ether_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief Set SyncE module config

 @param[in]  lchip Local chip ID

 @param[in]  synce_clock_id  <0-3>

 @param[in]  p_synce_cfg  Configuration of SyncE

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_sync_ether_set_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_PTP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_sync_ether_set_cfg(lchip, sync_ether_clock_id, p_sync_ether_cfg));
    }

    return CTC_E_NONE;
}

/**
 @brief Get SyncE module config

 @param[in]  lchip Local chip ID

 @param[in]  synce_clock_id  <0-3>

 @param[out]  p_synce_cfg  Configuration of SyncE

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_sync_ether_get_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_PTP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_sync_ether_get_cfg(lchip, sync_ether_clock_id, p_sync_ether_cfg));
    }

    return CTC_E_NONE;
}


/**
 @file

 @date 2009-10-20

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_const.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_stp.h"
#include "sys_greatbelt_vlan.h"

#include "greatbelt/include/drv_io.h"

/***************************************************************
*
*  Defines and Macros
*
***************************************************************/

#define SYS_STP_DBG_OUT(level, FMT, ...) \
    do \
    { \
        CTC_DEBUG_OUT(l2, stp, L2_STP_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_STP_PORT_NUM 64 /**< according to IpeDsVlanCtl.dsStpStateShift[1:0] */

#define SYS_STP_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
    } while(0)

/**
 @brief initialize the stp module
*/

/**
 @brief   the function is to clear all instances belonged to one port
*/

int32
sys_greatbelt_stp_clear_all_inst_state(uint8 lchip, uint16 gport)
{
    uint32 index    = 0;
    uint32 state    = 0;
    uint32 cmd      = 0;
    uint32 loop     = 0;
    uint8 lport     = 0;

    SYS_STP_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if (lport >= SYS_STP_PORT_NUM)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    index = (lport << SYS_STP_STATE_SHIFT);

    cmd = DRV_IOW(DsStpState_t, DRV_ENTRY_FLAG);

    for (loop = 0; loop < SYS_STP_STATE_ENTRY_NUM_PER_PORT; loop++, index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &state));
    }

    return CTC_E_NONE;
}

/**
     @brief the function is to set stp id (MSTI instance) for vlan
*/

int32
sys_greatbelt_stp_set_vlan_stpid(uint8 lchip, uint16 vlan_id,  uint8 stpid)
{

    SYS_STP_INIT_CHECK(lchip);
    SYS_STP_ID_VALID_CHECK(stpid);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id :%d  stpid:%d \n", vlan_id, stpid);

    CTC_ERROR_RETURN(sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_STP_ID, stpid));
    return CTC_E_NONE;
}

/**
     @brief the function is to set stp id (MSTI instance) for vlan
*/

int32
sys_greatbelt_stp_get_vlan_stpid(uint8 lchip, uint16 vlan_id,  uint8* stpid)
{
    uint32 value_32 = 0;

    SYS_STP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stpid);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id :%d\n", vlan_id);

    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_STP_ID, &value_32));
    *stpid = value_32 & 0xff;

    return CTC_E_NONE;
}

/**
 @brief    the function is to update the state
*/
int32
sys_greatbelt_stp_set_state(uint8 lchip, uint16 gport,  uint8 stpid, uint8 state)
{
    uint8 lport         = 0;
    uint32 index        = 0;
    uint32 cmd          = 0;
    uint32 field_val    = 0;
    uint32 field_id     = DsStpState_StpState0_f;

    SYS_STP_INIT_CHECK(lchip);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:%d stp id:%d state:%d\n", gport, stpid, state);
    SYS_STP_ID_VALID_CHECK(stpid);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if (lport >= SYS_STP_PORT_NUM)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    index = (lport << SYS_STP_STATE_SHIFT) + ((stpid >> 4) & 0x7);

    field_id += (stpid & 0xF);
    cmd = DRV_IOW(DsStpState_t, field_id);

    CTC_MAX_VALUE_CHECK(state, 2);
    field_val = state;
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    return CTC_E_NONE;
}

/**
 @brief the function is to get the instance of the port
*/
int32
sys_greatbelt_stp_get_state(uint8 lchip, uint16 gport, uint8 stpid, uint8* state)
{
    uint8 lport         = 0;
    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 field_val    = 0;
    uint32 field_id     = DsStpState_StpState0_f;

    SYS_STP_INIT_CHECK(lchip);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport%d stp id:%d state\n", gport, stpid);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if (lport >= SYS_STP_PORT_NUM)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    SYS_STP_ID_VALID_CHECK(stpid);
    CTC_PTR_VALID_CHECK(state);

    index = (lport << SYS_STP_STATE_SHIFT) + ((stpid >> 4) & 0x7);
    field_id += stpid & 0xF;

    cmd = DRV_IOR(DsStpState_t, field_id);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    *state = field_val & 0x3;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stp_init(uint8 lchip)
{
    uint16 gport = 0;
    uint16 lport = 0;
    uint8 gchip = 0;

    LCHIP_CHECK(lchip);

    for (lport = 0; lport < SYS_STP_PORT_NUM; lport++)
    {
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        sys_greatbelt_stp_clear_all_inst_state(lchip, gport);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stp_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);

    return CTC_E_NONE;
}


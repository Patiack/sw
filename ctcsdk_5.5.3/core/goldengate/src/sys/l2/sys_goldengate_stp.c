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

#include "sys_goldengate_chip.h"
#include "sys_goldengate_stp.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_register.h"

#include "goldengate/include/drv_io.h"

/***************************************************************
*
*  Defines and Macros
*
***************************************************************/

#define SYS_STP_STATE_ENTRY_NUM   512     /* DsStpState entry num of one slice */

#define SYS_STP_ID_VALID_CHECK(stp_id)          \
{                                               \
    if ((stp_id) >= p_gg_stp_master[lchip]->instance_num) \
        return CTC_E_STP_INVALID_STP_ID;        \
}

#define SYS_STP_DBG_OUT(level, FMT, ...) CTC_DEBUG_OUT(l2, stp, L2_STP_SYS, level, FMT, ##__VA_ARGS__)

struct sys_stp_master_s
{
    uint16 port_num; /* Stp port num in one slice */
    uint8 instance_num;
    uint8 shift_num; /* Lport shift num for DsStpState index */
};
typedef struct sys_stp_master_s sys_stp_master_t;

/****************************************************************************
Global and Declaration
*****************************************************************************/

sys_stp_master_t* p_gg_stp_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/**********************************************************************************
Define API function interfaces
***********************************************************************************/

/**
 @brief   the function is to clear all instances belonged to one port
*/
int32
sys_goldengate_stp_clear_all_inst_state(uint8 lchip, uint16 gport)
{
    uint8  slice    = 0;
    uint16 lport    = 0;
    uint16 entry_num = 0;
    uint32 index    = 0;
    uint32 cmd      = 0;
    uint32 loop     = 0;
    DsStpState_m ds_stp_state;

    SYS_STP_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:0x%04X \n", gport);

    slice = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
    lport = lport % SYS_CHIP_PER_SLICE_PORT_NUM;

    if (lport >= p_gg_stp_master[lchip]->port_num)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    entry_num = SYS_STP_STATE_ENTRY_NUM / p_gg_stp_master[lchip]->port_num;
    index = ((lport << p_gg_stp_master[lchip]->shift_num) & 0x1FF) + (slice * SYS_STP_STATE_ENTRY_NUM);

    sal_memset(&ds_stp_state, 0, sizeof(DsStpState_m));

    cmd = DRV_IOW(DsStpState_t, DRV_ENTRY_FLAG);
    for (loop = 0; loop < entry_num; loop++, index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_stp_state));
    }

    return CTC_E_NONE;
}

/**
 @brief the function is to set stp id (MSTI instance) for vlan
*/
int32
sys_goldengate_stp_set_vlan_stpid(uint8 lchip, uint16 vlan_id,  uint8 stpid)
{
    SYS_STP_INIT_CHECK(lchip);
    SYS_STP_ID_VALID_CHECK(stpid);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id :%d  stpid:%d \n", vlan_id, stpid);

    CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_STP_ID, stpid));
    return CTC_E_NONE;
}

/**
 @brief the function is to set stp id (MSTI instance) for vlan
*/
int32
sys_goldengate_stp_get_vlan_stpid(uint8 lchip, uint16 vlan_id,  uint8* stpid)
{
    uint32 value_32 = 0;

    SYS_STP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stpid);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id :%d\n", vlan_id);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_STP_ID, &value_32));
    *stpid = value_32 & 0xff;

    return CTC_E_NONE;
}

/**
 @brief    the function is to update the state
*/
int32
sys_goldengate_stp_set_state(uint8 lchip, uint16 gport,  uint8 stpid, uint8 state)
{
    uint16 lport        = 0;
    uint32 index        = 0;
    uint32 cmd          = 0;
    uint32 field_val    = 0;
    uint32 field_id     = 0;
    uint8  slice        = 0;

    SYS_STP_INIT_CHECK(lchip);
    SYS_STP_ID_VALID_CHECK(stpid);
    CTC_MAX_VALUE_CHECK(state, 2);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, stp_id:%d, state:%d\n", gport, stpid, state);

    slice = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
    lport = lport % SYS_CHIP_PER_SLICE_PORT_NUM;

    if (lport >= p_gg_stp_master[lchip]->port_num)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    field_id = DsStpState_array_0_stpState_f + (stpid & 0xF);
    cmd = DRV_IOW(DsStpState_t, field_id);

    field_val = state;

    index = ((lport << p_gg_stp_master[lchip]->shift_num) & 0x1FF) + ((stpid >> 4) & 0x7) + (slice * SYS_STP_STATE_ENTRY_NUM);

    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    return CTC_E_NONE;
}

/**
 @brief the function is to get the instance of the port
*/
int32
sys_goldengate_stp_get_state(uint8 lchip, uint16 gport, uint8 stpid, uint8* state)
{
    uint16 lport        = 0;
    uint32 cmd          = 0;
    uint32 index        = 0;
    uint32 field_val    = 0;
    uint32 field_id     = 0;
    uint8  slice        = 0;

    SYS_STP_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    SYS_STP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X, stp id:%d, state\n", gport, stpid);

    slice = SYS_MAP_DRV_LPORT_TO_SLICE(lport);
    lport = lport % SYS_CHIP_PER_SLICE_PORT_NUM;

    if (lport >= p_gg_stp_master[lchip]->port_num)
    {
        return CTC_E_EXCEED_STP_MAX_LPORT;
    }

    SYS_STP_ID_VALID_CHECK(stpid);
    CTC_PTR_VALID_CHECK(state);

    field_id = DsStpState_array_0_stpState_f + (stpid & 0xF);
    cmd = DRV_IOR(DsStpState_t, field_id);

    index = (((lport << p_gg_stp_master[lchip]->shift_num) & 0x1FF) & 0x1FF) + ((stpid >> 4) & 0x7) + (slice * SYS_STP_STATE_ENTRY_NUM);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    *state = field_val & 0x3;

    return CTC_E_NONE;
}

/**
 @brief initialize the stp module
*/
int32
sys_goldengate_stp_init(uint8 lchip, uint8 stp_mode)
{
    uint8  slice = 0;
    uint8  gchip = 0;
    uint16 gport = 0;
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field = 0;

    p_gg_stp_master[lchip] = (sys_stp_master_t*)mem_malloc(MEM_FDB_MODULE, sizeof(sys_stp_master_t));
    if (NULL == p_gg_stp_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_stp_master[lchip], 0, sizeof(sys_stp_master_t));

    switch(stp_mode)
    {
        /* 32 stp instance, 256 port per slice, 256*2 port per chip */
        case CTC_STP_MODE_32:
            p_gg_stp_master[lchip]->instance_num = 32;
            p_gg_stp_master[lchip]->port_num = 256;
            p_gg_stp_master[lchip]->shift_num = 1;
            break;
        /* 64 stp instance, 128 port per slice, 128*2 port per chip */
        case CTC_STP_MODE_64:
            p_gg_stp_master[lchip]->instance_num = 64;
            p_gg_stp_master[lchip]->port_num = 128;
            p_gg_stp_master[lchip]->shift_num = 2;
            break;
        /* 128 stp instance, 64 port per slice, 64*2 port per chip */
        case CTC_STP_MODE_128:
            p_gg_stp_master[lchip]->instance_num = 128;
            p_gg_stp_master[lchip]->port_num = 64;
            p_gg_stp_master[lchip]->shift_num = 3;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    sys_goldengate_get_gchip_id(lchip, &gchip);

    field = p_gg_stp_master[lchip]->shift_num;
    cmd = DRV_IOW(IpeDsVlanCtl_t, IpeDsVlanCtl_dsStpStateShift_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    field = p_gg_stp_master[lchip]->shift_num;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_dsStpStateShift_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));

    for (slice = 0; slice < SYS_MAX_LOCAL_SLICE_NUM; slice++)
    {
        for (lport = 0; lport < p_gg_stp_master[lchip]->port_num; lport++)
        {
            gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport + slice * SYS_CHIP_PER_SLICE_PORT_NUM);
            CTC_ERROR_RETURN(sys_goldengate_stp_clear_all_inst_state(lchip, gport));
        }
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_STP_INSTANCE_NUM, p_gg_stp_master[lchip]->instance_num));

    return CTC_E_NONE;
}

int32
sys_goldengate_stp_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_stp_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gg_stp_master[lchip]);

    return CTC_E_NONE;
}


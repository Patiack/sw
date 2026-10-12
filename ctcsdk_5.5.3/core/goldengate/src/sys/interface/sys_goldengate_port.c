/**
 @file sys_goldengate_port.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-22

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
#include "ctc_interrupt.h"
#include "ctc_warmboot.h"

#include "sys_goldengate_register.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_datapath.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_vlan_mapping.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_queue_api.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_MAX_FRAME_VALUE (16 * 1024)

#define SYS_SGMAC_RESET_INT_STEP   (ResetIntRelated_ResetSgmac1_f - ResetIntRelated_ResetSgmac0_f)

#define SYS_SGMAC_EN_PCS_CLK_STEP  (ModuleGatedClkCtl_EnClkSupSgmac1Pcs_f - ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f)

#define SYS_SGMAC_XGMAC_PCS_CLK_STEP  (ModuleGatedClkCtl_EnClkSupSgmac5XgmacPcs_f - ModuleGatedClkCtl_EnClkSupSgmac4XgmacPcs_f)

#define SYS_GMAC_RESET_INT_STEP  (ResetIntRelated_ResetQuadMac1Reg_f - ResetIntRelated_ResetQuadMac0Reg_f)

#define GMAC_FLOW_CTL_REG_ID_INTERVAL 20
#define SGMAC_FLOW_CTL_REG_ID_INTERVAL 42
#define XGMAC_FLOW_CTL_REG_ID_INTERVAL 36

#define SYS_MAX_RANDOM_LOG_RATE     0x8000      /*<[GG] random log rate is 15 bit, and from 0x2 ~ 0x7ffE under shift is 5>*/
#define SYS_MAX_RANDOM_LOG_THRESHOD 0x100000
#define SYS_RANDOM_LOG_THRESHOD_PER 330
#define SYS_MAX_ISOLUTION_ID_NUM 31
#define SYS_MAX_PVLAN_COMMUNITY_ID_NUM 15
#define SYS_MAX_NO_RANDOM_DATA_INDEX 10

#define VLAN_RANGE_ENTRY_NUM 64

#define SYS_VLAN_RANGE_INFO_CHECK(vrange_info) \
    { \
        CTC_BOTH_DIRECTION_CHECK(vrange_info->direction); \
        if (VLAN_RANGE_ENTRY_NUM <= vrange_info->vrange_grpid) \
        {    return CTC_E_INVALID_PARAM; } \
    }

#define PORT_DB_SET(old, cur) \
    do \
    { \
        if (cur == old) \
        { \
            PORT_UNLOCK; \
            return CTC_E_NONE; \
        } \
        else \
        { \
            old = cur; \
        } \
    } while (0)

#define PORT_DB_GET(cur, old) \
    do \
    { \
        *cur = old; \
    } while (0)

#define SYS_PORT_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(port, port, PORT_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_CL73_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(port, cl73, CL73_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_LINKAGG_MC_MAP_PBM_PORTID(lport, port_type, port_id)\
    do{\
      port_type = (lport >> 6)&0x3;\
      port_id   = (lport>>8)?((lport&0x3F) + 64):(lport&0x3F);\
    }while(0)
enum sys_port_egress_restriction_type_e
{
    SYS_PORT_RESTRICTION_NONE = 0,                    /**< restriction disable */
    SYS_PORT_RESTRICTION_PVLAN = 1,                   /**< private vlan enable */
    SYS_PORT_RESTRICTION_BLOCKING = 2,                /**< port blocking enable */
    SYS_PORT_RESTRICTION_ISOLATION = 4                /**< port isolation enable */
};
typedef enum sys_port_egress_restriction_type_e sys_port_egress_restriction_type_t;

/* Port 802.3ap port ability flags */
enum sys_port_cl73_ability_e
{
    SYS_PORT_CL73_10GBASE_KR       = (1 << 23),
    SYS_PORT_CL73_40GBASE_KR4      = (1 << 24),
    SYS_PORT_CL73_40GBASE_CR4      = (1 << 25),
    SYS_PORT_CL73_100GBASE_KR4     = (1 << 28),
    SYS_PORT_CL73_100GBASE_CR4     = (1 << 29),
    SYS_PORT_CL73_FEC_ABILITY      = (1 << 30),
    SYS_PORT_CL73_FEC_REQUESTED    = (1 << 31)
};
typedef enum sys_port_cl73_ability_e sys_port_cl73_ability_t;

enum sys_port_framesize_type_e
{
    SYS_PORT_FRAMESIZE_TYPE_MIN,
    SYS_PORT_FRAMESIZE_TYPE_MAX,
    SYS_PORT_FRAMESIZE_TYPE_NUM
};
typedef enum sys_port_framesize_type_e sys_port_framesize_type_t;

enum sys_scl_key_mapping_type_e
{
    SYS_PORT_KEY_MAPPING_TYPE_DRV2CTC,
    SYS_PORT_KEY_MAPPING_TYPE_CTC2DRV,
    SYS_PORT_KEY_MAPPING_TYPE_NUM
};
typedef enum sys_scl_key_mapping_type_e sys_scl_key_mapping_type_t;

enum sys_port_preamble_type_e
{
    SYS_PORT_PREAMBLE_TYPE_8BYTE,
    SYS_PORT_PREAMBLE_TYPE_4BYTE,
    SYS_PORT_PREAMBLE_TYPE_NUM
};
typedef enum sys_port_preamble_type_e sys_port_preamble_type_t;

struct sys_scl_type_map_s
{
    uint8 hash_en;
    uint8 use_macda;
    uint8 vlan_high_priority;
    uint8 drv_type;
    uint8 ctc_type;
};
typedef struct sys_scl_type_map_s sys_scl_type_map_t;

struct sys_scl_type_para_s
{
    uint8 tunnel_en        :1;
    uint8 auto_tunnel      :1;
    uint8 nvgre_en         :1;
    uint8 vxlan_en         :1;
    uint8 tunnel_rpf_check :1;
    uint8 use_macda        :1;
    uint8 tcam_en          :1;
    uint8 vlan_high_priority              :1;
    uint8 drv_hash_type;
    uint8 ctc_hash_type;
    uint8 drv_tcam_type;
    uint8 ctc_tcam_type;
};
typedef struct sys_scl_type_para_s sys_scl_type_para_t;

typedef struct
{
    uint32 hash_type;
    uint32 tcam_type;
    uint32 tcam_en;
    uint32 vlan_high_priority;
    uint8  use_label;
    uint8  label;
    uint8  is_userid;
    uint8  use_macda;
    uint8  ipv4_tunnel_en;
    uint8  ipv4_gre_tunnel_en;
    uint8  rpf_check_en;
    uint8  auto_tunnel;
    uint8  nvgre_en;
    uint8  v6_nvgre_en;
    uint8  v4_vxlan_en;
    uint8  v6_vxlan_en;
} sys_goldengate_igs_port_scl_map_t;

typedef struct
{
    uint32 hash_type;
    uint8  use_label;
    uint8  label;
} sys_goldengate_egs_port_scl_map_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_port_master_t* p_gg_port_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define TCAML2KEY    0
#define TCAML2L3KEY  1
#define TCAML3KEY    2
#define TCAMVLANKEY  3
#define TCAMDISABLE  4
extern int32
sys_goldengate_ip_source_guard_init_default_entry(uint8 lchip);
/*SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS SYS_SCL_LABEL_RESERVE_FOR_IPSG*/
int32
sys_goldengate_port_wb_sync(uint8 lchip);

int32
sys_goldengate_port_wb_restore(uint8 lchip);

STATIC int32
_sys_goldengate_port_set_chan_framesize_index(uint8 lchip, uint8, uint8, uint8, sys_port_framesize_type_t);

int32
sys_goldengate_port_set_3ap_training_en(uint8 lchip, uint16 gport, uint32 cfg_flag, uint32 is_normal);

STATIC int32
_sys_goldengate_select_chan_framesize_index(uint8 lchip, uint16, sys_port_framesize_type_t, uint8*);

int32
_sys_goldengate_port_set_link_intr(uint8 lchip, uint16 gport, bool enable);

int32
sys_goldengate_port_clear_link_intr(uint8 lchip, uint16 gport);

int32
_sys_goldengate_port_get_unidir_en(uint8 lchip, uint16 gport, uint32* p_value);

STATIC int32
_sys_goldengate_port_set_pcs_reset(uint8 lchip, sys_datapath_lport_attr_t* port_attr, uint8 dir, bool enable);

int32
sys_goldengate_port_set_mcast_member_down(uint8 lchip, uint16 gport, uint8 value);

STATIC int32
_sys_goldengate_port_get_fec_en(uint8 lchip, uint16 gport, uint32* p_enable);
/****************************************************************************
 *
* Function
*
*****************************************************************************/

STATIC int32
_sys_goldengate_port_set_bitmap_status(uint8 lchip, uint16 gport, bool enable)
{
    uint8 port_type                = 0;
    uint16 port_id                 = 0;
    uint32 field_val[4]            = {0};

    uint16 lport                   = 0;
    uint32 cmd                     = 0;
    MetFifoPortStatusCtl_m     port_status;

    sal_memset(&port_status, 0, sizeof(port_status));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    port_type = ((lport >> 6)&0x3);
    port_id  = ((lport >> 8)?((lport&0x3F) + 64):(lport&0x3F));

    cmd = DRV_IOR(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));

    GetMetFifoPortStatusCtl(A, baseArray_0_portStatus_f + port_type, &port_status, field_val);

    if (enable)
    {
        CTC_BIT_UNSET(field_val[port_id >> 5], port_id&0x1F);
    }
    else
    {
        CTC_BIT_SET(field_val[port_id >> 5], port_id&0x1F);
    }

    SetMetFifoPortStatusCtl(A, baseArray_0_portStatus_f + port_type, &port_status, field_val);

    cmd = DRV_IOW(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));

     return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_port_blocking(uint8 lchip, uint16 gport, ctc_port_blocking_pkt_type_t type)
{

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    DsDestPort_m ds_dest_port;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&ds_dest_port, 0, sizeof(DsDestPort_m));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));

    value = CTC_FLAG_ISSET(type, CTC_PORT_BLOCKING_UNKNOW_UCAST) ? 1 : 0;
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "unknown ucast flooding = %d\n", value);
    SetDsDestPort(V, ucastFloodingDisable_f, &ds_dest_port, value);

    value = CTC_FLAG_ISSET(type, CTC_PORT_BLOCKING_UNKNOW_MCAST) ? 1 : 0;
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "unknown mcast flooding = %d\n", value);
    SetDsDestPort(V, mcastFloodingDisable_f, &ds_dest_port, value);

    value = CTC_FLAG_ISSET(type, CTC_PORT_BLOCKING_KNOW_UCAST) ? 1 : 0;
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "known ucast flooding = %d\n", value);
    SetDsDestPort(V, knownUcastFloodingDisable_f, &ds_dest_port, value);

    value = CTC_FLAG_ISSET(type, CTC_PORT_BLOCKING_KNOW_MCAST) ? 1 : 0;
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "known mcast flooding = %d\n", value);
    SetDsDestPort(V, knownMcastFloodingDisable_f, &ds_dest_port, value);

    value = CTC_FLAG_ISSET(type, CTC_PORT_BLOCKING_BCAST) ? 1 : 0;
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "bcast flooding = %d\n", value);
    SetDsDestPort(V, bcastFloodingDisable_f, &ds_dest_port, value);

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_port_blocking(uint8 lchip, uint16 gport, uint16* p_type)
{

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    DsDestPort_m ds_dest_port;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&ds_dest_port, 0, sizeof(DsDestPort_m));
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));

    GetDsDestPort(A, ucastFloodingDisable_f, &ds_dest_port, &value);
    if (value)
    {
        CTC_SET_FLAG(*p_type, CTC_PORT_BLOCKING_UNKNOW_UCAST);
    }

    GetDsDestPort(A, mcastFloodingDisable_f, &ds_dest_port, &value);
    if (value)
    {
        CTC_SET_FLAG(*p_type, CTC_PORT_BLOCKING_UNKNOW_MCAST);
    }

    GetDsDestPort(A, knownUcastFloodingDisable_f, &ds_dest_port, &value);
    if (value)
    {
        CTC_SET_FLAG(*p_type, CTC_PORT_BLOCKING_KNOW_UCAST);
    }

    GetDsDestPort(A, knownMcastFloodingDisable_f, &ds_dest_port, &value);
    if (value)
    {
        CTC_SET_FLAG(*p_type, CTC_PORT_BLOCKING_KNOW_MCAST);
    }

    GetDsDestPort(A, bcastFloodingDisable_f, &ds_dest_port, &value);
    if (value)
    {
        CTC_SET_FLAG(*p_type, CTC_PORT_BLOCKING_BCAST);
    }

    return CTC_E_NONE;
}


/* static int32 just fix warning */
int32
_sys_goldengate_port_get_port_isolation(uint8 lchip, uint16 gport, uint16 isolation_id, uint16* p_type, uint16* p_isolate_group)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 isolate_group = 0;
    DsSrcPort_m ds_src_port;
    DsPortIsolation_m ds_port_isolation;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&ds_src_port, 0, sizeof(DsSrcPort_m));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, gport, cmd, &ds_src_port));

    GetDsSrcPort(A, srcPortIsolateId_f, &ds_src_port, &isolate_group);
    *p_isolate_group = isolate_group;

    sal_memset(&ds_port_isolation, 0, sizeof(DsPortIsolation_m));
    cmd = DRV_IOR(DsPortIsolation_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, isolate_group, cmd, &ds_port_isolation));

    GetDsPortIsolation(A, unknownUcIsolate_f, &ds_port_isolation, &value);
    CTC_BIT_SET(*p_type, CTC_IS_BIT_SET(value, isolation_id) ? CTC_PORT_ISOLATION_UNKNOW_UCAST : 0);

    GetDsPortIsolation(A, unknownMcIsolate_f, &ds_port_isolation, &value);
    CTC_BIT_SET(*p_type, CTC_IS_BIT_SET(value, isolation_id) ? CTC_PORT_ISOLATION_UNKNOW_MCAST : 0);

    GetDsPortIsolation(A, knownMcIsolate_f, &ds_port_isolation, &value);
    CTC_BIT_SET(*p_type, CTC_IS_BIT_SET(value, isolation_id) ? CTC_PORT_ISOLATION_KNOW_MCAST : 0);

    GetDsPortIsolation(A, bcIsolate_f, &ds_port_isolation, &value);
    CTC_BIT_SET(*p_type, CTC_IS_BIT_SET(value, isolation_id) ? CTC_PORT_ISOLATION_BCAST : 0);

    GetDsPortIsolation(A, knownUcIsolate_f, &ds_port_isolation, &value);
    CTC_BIT_SET(*p_type, CTC_IS_BIT_SET(value, isolation_id) ? CTC_PORT_ISOLATION_KNOW_UCAST : 0);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_framesize_init(uint8 lchip)
{
    uint8  idx = 0;

    for (idx = 0; idx < CTC_FRAME_SIZE_MAX; idx++)
    {
        CTC_ERROR_RETURN(sys_goldengate_set_min_frame_size(lchip, idx, SYS_GOLDENGATE_MIN_FRAMESIZE_DEFAULT_VALUE));
        CTC_ERROR_RETURN(sys_goldengate_set_max_frame_size(lchip, idx, SYS_GOLDENGATE_MAX_FRAMESIZE_DEFAULT_VALUE));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chan_init(uint8 lchip)
{
    uint8  index = 0;

    uint8  slice = 0;
    uint16 lport = 0;
    uint32 field = 0;
    uint32 cmd = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;
    uint32 net_rx_ctl_tbl[2] = {NetRxCtl0_t, NetRxCtl1_t};
    uint32 net_tx_misc_ctl_tbl[2] = {NetTxMiscCtl0_t, NetTxMiscCtl1_t};
    uint32 epe_hdr_adjust_misc_ctl_tbl[2] = {EpeHdrAdjustMiscCtl0_t, EpeHdrAdjustMiscCtl1_t};
    NetRxCtl0_m net_rx_ctl;
    NetTxMiscCtl0_m net_tx_misc_ctl;
    int32 ret = 0;

    CTC_ERROR_RETURN(_sys_goldengate_framesize_init(lchip));

    for (slice = 0; slice < 2; slice++)
    {
        for (lport = 0; lport < SYS_CHIP_PER_SLICE_PORT_NUM; lport++)
        {
            ret = (sys_goldengate_common_get_port_capability(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT((slice * 256) + lport),
                        &p_port_cap));
            if (ret == CTC_E_MAC_NOT_USED)
            {
                continue;
            }

            if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                continue;
            }


            CTC_ERROR_RETURN(_sys_goldengate_port_set_chan_framesize_index(lchip, p_port_cap->slice_id, p_port_cap->chan_id, index, SYS_PORT_FRAMESIZE_TYPE_MAX));
            CTC_ERROR_RETURN(_sys_goldengate_port_set_chan_framesize_index(lchip, p_port_cap->slice_id, p_port_cap->chan_id, index, SYS_PORT_FRAMESIZE_TYPE_MIN));

            field = lport & 0x3F;
            cmd = DRV_IOW(EpeHeaderAdjustPhyPortMap_t, EpeHeaderAdjustPhyPortMap_localPhyPort_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (p_port_cap->chan_id+64*slice), cmd, &field));
        }

        sal_memset(&net_rx_ctl, 0, sizeof(NetRxCtl0_m));
        cmd = DRV_IOR(net_rx_ctl_tbl[slice], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_rx_ctl));

        SetNetRxCtl0(V, underLenChkEn_f, &net_rx_ctl, 1);
        SetNetRxCtl0(V, overLenChkEn_f, &net_rx_ctl, 1);
        SetNetRxCtl0(V, bpduChkEn_f, &net_rx_ctl, 0);

        cmd = DRV_IOW(net_rx_ctl_tbl[slice], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_rx_ctl));

        sal_memset(&net_tx_misc_ctl, 0, sizeof(NetTxMiscCtl0_m));
        cmd = DRV_IOR(net_tx_misc_ctl_tbl[slice], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_misc_ctl));

        SetNetTxMiscCtl0(V, runtDropEn_f, &net_tx_misc_ctl, 1);
        /* RTL logic is discard packet, when packet length <= cfgMinPktLen */
        SetNetTxMiscCtl0(V, cfgMinPktLen_f, &net_tx_misc_ctl, 0);

        cmd = DRV_IOW(net_tx_misc_ctl_tbl[slice], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_misc_ctl));

        /* RTL logic is discard packet, when egressHeaderAdjust output packet's length <= minPktLen */
        field = 5;
        cmd = DRV_IOW(epe_hdr_adjust_misc_ctl_tbl[slice], EpeHdrAdjustMiscCtl0_minPktLen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    }

    return CTC_E_NONE;
}

int32 sys_goldengate_port_flow_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ds_t ds;
    uint32 field_val = 0;
    uint32 tbl_id = 0;
    uint32 vlan_base = 0;
    uint8 slice_id = 0;
    uint8 channe_id = 0;
	NetTxPauseQuantaCfg0_m quanta;

	/*Reset the pulse for pause timer*/
    field_val = 0;
    cmd = DRV_IOW(RefDivNetPausePulse_t, RefDivNetPausePulse_cfgResetDivNetPausePulse_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

	/*Configure the quanta for the pulse of pause timer*/
    cmd = DRV_IOR(NetTxPauseQuantaCfg0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quanta));
    /*10G*/
    SetNetTxPauseQuantaCfg0(V, pauseQuanta0Low_f, &quanta, 0xFFFB);
    /*40G*/
    SetNetTxPauseQuantaCfg0(V, pauseQuanta1Low_f, &quanta, 0xFFF1);
    /*100G*/
    SetNetTxPauseQuantaCfg0(V, pauseQuanta2Low_f, &quanta, 0xFF00);
    /*1G*/
    SetNetTxPauseQuantaCfg0(V, pauseQuanta3Low_f, &quanta, 1);
    SetNetTxPauseQuantaCfg0(V, pauseQuanta3High_f, &quanta, 1);
    /*2.5G*/
    SetNetTxPauseQuantaCfg0(V, pauseQuanta4Low_f, &quanta, 0xFFF0);
    cmd = DRV_IOW(NetTxPauseQuantaCfg0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quanta));
    cmd = DRV_IOW(NetTxPauseQuantaCfg1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quanta));


    field_val = 0xffffffff;
    cmd = DRV_IOW(NetTxNetRxPauseReqMaskBmp0_t, NetTxNetRxPauseReqMaskBmp0_netRxPauseMaskEn31To0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xffffffff;
    cmd = DRV_IOW(NetTxNetRxPauseReqMaskBmp0_t, NetTxNetRxPauseReqMaskBmp0_netRxPauseMaskEn63To32_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xffffffff;
    cmd = DRV_IOW(NetTxNetRxPauseReqMaskBmp1_t, NetTxNetRxPauseReqMaskBmp1_netRxPauseMaskEn31To0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xffffffff;
    cmd = DRV_IOW(NetTxNetRxPauseReqMaskBmp1_t, NetTxNetRxPauseReqMaskBmp1_netRxPauseMaskEn63To32_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    for(slice_id = 0; slice_id < SYS_MAX_LOCAL_SLICE_NUM; slice_id++)
    {
        for(channe_id = 0; channe_id < SYS_MAX_CHANNEL_ID; channe_id++)
        {
            sal_memset(ds, 0, sizeof(ds));
            tbl_id = DsChannelizeMode0_t + slice_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channe_id, cmd, ds));
            vlan_base = GetDsChannelizeMode0(V, vlanBase_f, ds);
            CTC_BIT_SET(vlan_base, 11);
            SetDsChannelizeMode0(V, vlanBase_f, ds, vlan_base);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channe_id, cmd, ds));
            sal_memset(ds, 0, sizeof(ds));
            tbl_id = NetRxPfcCfg0_t + slice_id;
            cmd = DRV_IOW(tbl_id,DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channe_id, cmd, ds));
        }
        field_val = 0xffff;
        for(channe_id = 0; channe_id < (SYS_MAX_CHANNEL_ID/16); channe_id++)
        {
            sal_memset(ds, 0, sizeof(ds));
            tbl_id = NetRxPauseCtl0_t + slice_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
            SetNetRxPauseCtl0(V, cfgNormPauseEn0_f + channe_id, ds, field_val);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_monitor_rlt(uint8 lchip)
{
    uint16 index = 0;

    sys_goldengate_scan_log_t* p_scan_log = NULL;

    SYS_PORT_INIT_CHECK(lchip);

    p_scan_log = p_gg_port_master[lchip]->scan_log;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-8s  %-8s %-50s\n", "Type", "GPORT", "System Time");
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------------------------\n");

    for (index = 0; index < SYS_PORT_MAX_LOG_NUM; index++)
    {
        if (p_scan_log[index].valid)
        {
            SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-5d  %5d %-50s\n", p_scan_log[index].type,
               p_scan_log[index].gport, p_scan_log[index].time_str);
        }
    }

    return CTC_E_NONE;
}

void
_sys_goldengate_port_recover_process(uint8 lchip, uint16 gport)
{
    uint16 lport     = 0;

    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 num = 0;
    uint8 index = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 value = 0;
    SharedPcsSoftRst0_m pcs_rst;
    CgPcsSoftRst0_m cg_rst;
    uint8 mac_id = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return;
    }
    else
    {
        slice_id = port_attr->slice_id;
        chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;

        sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &num);

        for (index = 0; index < num; index++)
        {
            /*do dfe reset */
            sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 1);
            sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 0);
        }
    }

    mac_id = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /*do pcssoftrst*/
    switch (port_attr->pcs_mode)
    {
        case CTC_CHIP_SERDES_SGMII_MODE:
        case CTC_CHIP_SERDES_2DOT5G_MODE:
        case CTC_CHIP_SERDES_XFI_MODE:
            tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
            field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
            value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            DRV_IOCTL(lchip, 0, cmd, &value);

            value = 0;
            cmd = DRV_IOW(tbl_id, field_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        break;

        case CTC_CHIP_SERDES_XAUI_MODE:
        case CTC_CHIP_SERDES_DXAUI_MODE:
        case CTC_CHIP_SERDES_XLG_MODE:
            tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcs_rst);
            SetSharedPcsSoftRst0(V,softRstXlgRx_f, &pcs_rst, 1);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcs_rst);

            SetSharedPcsSoftRst0(V,softRstXlgRx_f, &pcs_rst, 0);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcs_rst);
        break;

        case CTC_CHIP_SERDES_CG_MODE:
            tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_rst);
            SetCgPcsSoftRst0(V,softRstPcsRx_f, &cg_rst, 1);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_rst);

            SetCgPcsSoftRst0(V,softRstPcsRx_f, &cg_rst, 0);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_rst);

            tbl_id = RlmNetCtlReset0_t + slice_id;
            field_id = RlmNetCtlReset0_resetCoreCgmac0_f + ((mac_id==36)?0:2);
            cmd = DRV_IOW(tbl_id, field_id);
            value = 1;
            (DRV_IOCTL(lchip, 0, cmd, &value));

            value = 0;
            (DRV_IOCTL(lchip, 0, cmd, &value));
            break;

        default:
            break;
    }

    return;
}

/* get serdes no random data flag by gport */
STATIC int32
sys_goldengate_port_get_no_random_data_flag(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if (CTC_CHIP_SERDES_DXAUI_MODE == port_attr->pcs_mode)
    {
        *p_value = 0;
        return CTC_E_NONE;
    }
    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    /* XAUI, DXAUI, XLG, CG serdes_num is 4 */
    if(1 == serdes_num)
    {
        *p_value = sys_goldengate_datapath_get_serdes_no_random_data_flag(lchip, serdes_id);
    }
    else /* seedes_num == 4 */
    {
        if((1 == sys_goldengate_datapath_get_serdes_no_random_data_flag(lchip, serdes_id))
            ||(1 == sys_goldengate_datapath_get_serdes_no_random_data_flag(lchip, serdes_id + 1))
            ||(1 == sys_goldengate_datapath_get_serdes_no_random_data_flag(lchip, serdes_id + 2))
            ||(1 == sys_goldengate_datapath_get_serdes_no_random_data_flag(lchip, serdes_id + 3)))
        {
            *p_value = 1;
        }
        else
        {
            *p_value = 0;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value)
{
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 lport = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_NOT_READY;
    }

    *p_value = port_attr->code_err_count;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_3ap_aec_lock(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    /* CG serdes_num is 4 */
    if((1 == sys_goldengate_datapath_get_serdes_aec_lock(lchip, serdes_id))
        && (1 == sys_goldengate_datapath_get_serdes_aec_lock(lchip, serdes_id + 1))
        && (1 == sys_goldengate_datapath_get_serdes_aec_lock(lchip, serdes_id + 2))
        && (1 == sys_goldengate_datapath_get_serdes_aec_lock(lchip, serdes_id + 3)))
    {
        *p_value = 1;
    }
    else
    {
        *p_value = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_cg_fec_recover(uint8 lchip, uint32 gport, uint32 aec_lck)
{
    uint8  index     = 0;
    uint16 index_dfe = 0;
    uint32 value     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint32 field_id  = 0;
    uint32 is_up = 0;
    uint16 lport = 0;
    uint16 chip_port = 0;
    uint16 serdes_id = 0;
    uint8 num = 0;
    uint32 fec_error = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint8 serdes_id_tmp = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    tbl_id = CgPcsStatus0_t + ((port_attr->mac_id==36)?0:1)+2*port_attr->slice_id;
    cmd = DRV_IOR(tbl_id, CgPcsStatus0_alignStatus_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &is_up));
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - port_attr->slice_id*256;
    sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, port_attr->slice_id, &serdes_id, &num);

    if (aec_lck || is_up)
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " cg fec recover gport %x  aec_lck %d link_up %d\n", gport, aec_lck, is_up);

        for (index = 0; index < num; index++)
        {
            for (index_dfe=0; index_dfe<1000; index_dfe++)
            {
                serdes_id_tmp = serdes_id+index;
                fec_error = sys_goldengate_datapath_get_serdes_fec_work_round_flag(lchip, serdes_id_tmp);
                if (1 == fec_error)
                {
                    /*do dfe reset */
                    sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 1);
                    sal_task_sleep(1);
                    sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 0);
                    sal_task_sleep(400);
                    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " cg fec recover gport %x  serdes_id %d fec_error %d\n", gport, serdes_id_tmp, fec_error);
                    continue;
                }
                else
                {
                    break;
                }
            }
        }

        /* cfg RlmCsCtlReset resetCoreCgPcs_f, release mac/pcs reset */
        tbl_id = RlmCsCtlReset0_t + port_attr->slice_id;
        field_id = RlmCsCtlReset0_resetCoreCgPcs0_f + ((port_attr->mac_id==36)?0:2);
        value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        value = 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DIS, 0);
        port_attr->pcs_reset_en = 0;
    }
    else
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " cg fec recover gport %x  serdes %d\n", gport, serdes_id);

        for (index = 0; index < num; index++)
        {
            /*do dfe reset */
            sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 1);
            sys_goldengate_datapath_set_dfe_reset(lchip, (serdes_id+index), 0);
        }
    }

    return CTC_E_NONE;
}

void
_sys_goldengate_port_monitor_thread(void* para)
{
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 mac_en = 0;
    uint32 is_detect = 0;
    uint32 train_status = 0;
    uint32 is_up = 0;
    uint32 code_err = 0;
    uint32 random_data_flag =0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    sys_goldengate_scan_log_t* p_log = NULL;
    char* p_str = NULL;
    sal_time_t tv;
    uint8 lchip = (uintptr)para;
    uint8 type = 0;
    uint8 index = 0;
    uint32 fec_en = 0;
    uint32 aec_lck = 0;
    uint32 auto_neg_en = 0;

    while(1)
    {
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
        sys_goldengate_get_gchip_id(lchip, &gchip);

        for (lport = 0; lport < 512; lport++)
        {
            SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
            if (p_gg_port_master[lchip]->polling_status == 0)
            {
                continue;
            }
            is_detect = 0;
            mac_en = 0;
            code_err = 0;
            is_up = 0;
            type = 0;
            train_status = 0;
            random_data_flag = 0;

            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

            /*check mac used */
            sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                continue;
            }

            /*check mac enable */
            sys_goldengate_port_get_mac_en(lchip, gport, &mac_en);
            if (!mac_en)
            {
                continue;
            }

            /*check signal detect */
            sys_goldengate_port_get_mac_signal_detect(lchip, gport, &is_detect);
            if (!is_detect)
            {
                continue;
            }

            sys_goldengate_port_get_mac_link_up(lchip, gport, &is_up, 0);

            p_gg_port_master[lchip]->igs_port_prop[lport].link_status = is_up?1:0;


            if (port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
            {
                sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en);
                _sys_goldengate_port_get_fec_en(lchip, gport, &fec_en);
                if ((0 == auto_neg_en) && fec_en)
                {
                    if ((SYS_PORT_3AP_TRAINING_INIT == p_gg_port_master[lchip]->igs_port_prop[lport].training_status))
                    {
                        _sys_goldengate_port_get_3ap_aec_lock(lchip, gport, &aec_lck);
                        _sys_goldengate_port_cg_fec_recover(lchip, gport, aec_lck);
                    }
                    continue;
                }
            }

            train_status = p_gg_port_master[lchip]->igs_port_prop[lport].training_status;
            /* 3ap training done, Note, this is used for cl73 auto-neg */
		    if(SYS_PORT_3AP_TRAINING_INIT == train_status)
            {
                sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DONE, 1);
            }

            train_status = p_gg_port_master[lchip]->igs_port_prop[lport].training_status;
            if(SYS_PORT_3AP_TRAINING_DONE == train_status)
            {
                p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status = 2;
            }

            /* check, if do training, should not do recover process */
            if ((0 != p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status)&&(2 != p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status))
            {
                continue;
            }

            /*check code error & link status*/
            sys_goldengate_port_get_mac_code_err(lchip, gport, &code_err);

            /*check no-random-data flag*/
            for (index=0;index<SYS_MAX_NO_RANDOM_DATA_INDEX;index++)
            {
                sys_goldengate_port_get_no_random_data_flag(lchip, gport, &random_data_flag);
                if (!random_data_flag)
                {
                    break;
                }
            }

            if (code_err > 100)
            {
                type |= 0x01;
                port_attr->code_err_count++;
            }

            if (!is_up)
            {
                type |= 0x02;
            }

            if (1 == random_data_flag)
            {
                type |= 0x04;
            }

            if (type)
            {
                /*do mac recover*/
                _sys_goldengate_port_recover_process(lchip, gport);

                /*log event*/
                if (p_gg_port_master[lchip]->cur_log_index >= SYS_PORT_MAX_LOG_NUM)
                {
                    p_gg_port_master[lchip]->cur_log_index = 0;
                }

                p_log = &(p_gg_port_master[lchip]->scan_log[p_gg_port_master[lchip]->cur_log_index]);
                p_log->valid = 1;
                p_log->type = type;
                p_log->gport = gport;
                sal_time(&tv);
                p_str = sal_ctime(&tv);
                sal_strncpy(p_log->time_str, p_str, 50);
                p_gg_port_master[lchip]->cur_log_index++;
            }
        }

        sal_task_sleep(1000);
    }
    return;
}

int32
sys_goldengate_port_set_monitor_link_enable(uint8 lchip, uint32 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    enable = enable?1:0;

    p_gg_port_master[lchip]->polling_status = enable;

    field_val = enable;
    cmd = DRV_IOW(OobFcReserved0_t, OobFcReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_monitor_link_enable(uint8 lchip, uint32* p_value)
{
    *p_value = p_gg_port_master[lchip]->polling_status;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_monitor_link(uint8 lchip)
{
    drv_work_platform_type_t platform_type;
    int32 ret = 0;
    uintptr chip_id = lchip;
    uint32 cmd = 0;
    uint32 field_val = 0;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    sal_memset(p_gg_port_master[lchip]->scan_log, 0, sizeof(sys_goldengate_scan_log_t)*SYS_PORT_MAX_LOG_NUM);

    sal_sprintf(buffer, "ctclnkMon-%d", lchip);
    ret = sal_task_create(&(p_gg_port_master[lchip]->p_monitor_scan), buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_goldengate_port_monitor_thread, (void*)chip_id);
    if (ret < 0)
    {
        return CTC_E_NOT_INIT;
    }

    field_val = 1;
    cmd = DRV_IOW(OobFcReserved0_t, OobFcReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    p_gg_port_master[lchip]->polling_status = 1;


    return CTC_E_NONE;
}

void
_sys_goldengate_port_restart_auto_neg_thread(void* para)
{
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 mac_en = 0;
    uint32 auto_neg_en = 0;
    uint32 is_up = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint8 lchip = (uintptr)para;

    while(1)
    {
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
        sys_goldengate_get_gchip_id(lchip, &gchip);

        for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
        {
            SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
            if (p_gg_port_master[lchip]->auto_neg_restart_status == 0)
            {
                return;
            }
            auto_neg_en = 0;
            mac_en = 0;
            is_up = 0;

            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

            /*check mac used */
            sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
            if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
            {
                continue;
            }

            /*check cl73 auto-neg*/
            if ((CTC_CHIP_SERDES_XLG_MODE != port_attr->pcs_mode) && (CTC_CHIP_SERDES_CG_MODE != port_attr->pcs_mode)
                && (CTC_CHIP_SERDES_XFI_MODE != port_attr->pcs_mode))
            {
                continue;
            }

            /*check auto-neg enable */
            sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en);
            if (!auto_neg_en)
            {
                continue;
            }

            /*check mac enable */
            sys_goldengate_port_get_mac_en(lchip, gport, &mac_en);
            if (!mac_en)
            {
                continue;
            }

            sys_goldengate_port_get_mac_link_up(lchip, gport, &is_up, 0);
            if(!is_up)
            {
                /*From auto to training, need wait more.*/
                if((auto_neg_en == 2)&&(p_gg_port_master[lchip]->igs_port_prop[lport].cl73_old_status == 1))
                {
                    p_gg_port_master[lchip]->igs_port_prop[lport].cl73_old_status = 2;
                }
                else
                {
                   /*restart auto-negotiation*/
                   sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, 0);
                   sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, 1);
                   p_gg_port_master[lchip]->igs_port_prop[lport].cl73_old_status  = 1;
                }
            }
            else
            {
                p_gg_port_master[lchip]->igs_port_prop[lport].cl73_old_status = 3;
            }
        }

        sal_task_sleep(2600);
    }
    return;
}

int32
sys_goldengate_port_set_auto_neg_restart_enable(uint8 lchip, uint32 enable)
{
    int32 ret = 0;
    uintptr chip_id = lchip;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};

    SYS_PORT_INIT_CHECK(lchip);
    enable = enable?1:0;

    if (p_gg_port_master[lchip]->auto_neg_restart_status != enable)
    {
        p_gg_port_master[lchip]->auto_neg_restart_status = enable;
        if (enable)
        {
            sal_sprintf(buffer, "ctcResAutoNeg-%d", lchip);
            ret = sal_task_create(&(p_gg_port_master[lchip]->p_auto_neg_restart), buffer,
                                  SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_goldengate_port_restart_auto_neg_thread, (void*)chip_id);
            if (ret < 0)
            {
                return CTC_E_NOT_INIT;
            }
        }
        else
        {
            sal_task_destroy(p_gg_port_master[lchip]->p_auto_neg_restart);
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_auto_neg_restart_enable(uint8 lchip, uint32* p_value)
{
    SYS_PORT_INIT_CHECK(lchip);

    *p_value = p_gg_port_master[lchip]->auto_neg_restart_status;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_restart_auto_neg(uint8 lchip)
{
    uint16 lport = 0;
    drv_work_platform_type_t platform_type;
    sys_datapath_lport_attr_t* p_port_attr = NULL;

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    p_gg_port_master[lchip]->auto_neg_restart_status = 0;

    for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
    {
        /*check mac used */
        sys_goldengate_common_get_port_capability(lchip, lport, &p_port_attr);
        if (p_port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
        {
            continue;
        }
        if ((p_port_attr->pcs_mode == CTC_CHIP_SERDES_XLG_MODE) || (p_port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
            || (p_port_attr->pcs_mode == CTC_CHIP_SERDES_XFI_MODE))
        {
            break;
        }
    }
    if (lport != SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        sys_goldengate_port_set_auto_neg_restart_enable(lchip, TRUE);
    }

    return CTC_E_NONE;
}

/**
 @brief initialize the port module
*/
int32
sys_goldengate_port_init(uint8 lchip, ctc_port_global_cfg_t* p_port_global_cfg)
{

    uint8  gchip = 0;
    int32  ret = CTC_E_NONE;
    uint16 gport = 0;
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 val = 0;
    DsSrcPort_m ds_src_port;
    DsPhyPort_m ds_phy_port;
    DsDestPort_m ds_dest_port;
    DsPhyPortExt_m ds_phy_port_ext;

    if (p_gg_port_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }


    /*alloc&init DB and mutex*/
    p_gg_port_master[lchip] = (sys_port_master_t*)mem_malloc(MEM_PORT_MODULE, sizeof(sys_port_master_t));

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_port_master[lchip], 0, sizeof(sys_port_master_t));
    ret = sal_mutex_create(&(p_gg_port_master[lchip]->p_port_mutex));
    if (ret || !(p_gg_port_master[lchip]->p_port_mutex))
    {
        mem_free(p_gg_port_master[lchip]);

        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gg_port_master[lchip]->igs_port_prop = (sys_igs_port_prop_t*)mem_malloc(MEM_PORT_MODULE, SYS_GG_MAX_PORT_NUM_PER_CHIP * sizeof(sys_igs_port_prop_t));

    if (NULL == p_gg_port_master[lchip]->igs_port_prop)
    {
        sal_mutex_destroy(p_gg_port_master[lchip]->p_port_mutex);
        mem_free(p_gg_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_port_master[lchip]->igs_port_prop, 0, sizeof(sys_igs_port_prop_t) * SYS_GG_MAX_PORT_NUM_PER_CHIP);

    p_gg_port_master[lchip]->egs_port_prop = (sys_egs_port_prop_t*)mem_malloc(MEM_PORT_MODULE, SYS_GG_MAX_PORT_NUM_PER_CHIP * sizeof(sys_egs_port_prop_t));

    if (NULL == p_gg_port_master[lchip]->egs_port_prop)
    {
        mem_free(p_gg_port_master[lchip]->igs_port_prop);
        sal_mutex_destroy(p_gg_port_master[lchip]->p_port_mutex);
        mem_free(p_gg_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_port_master[lchip]->egs_port_prop, 0, sizeof(sys_egs_port_prop_t) * SYS_GG_MAX_PORT_NUM_PER_CHIP);

    /*init asic table*/
    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));
    sal_memset(&ds_phy_port_ext, 0, sizeof(DsPhyPortExt_m));
    sal_memset(&ds_src_port, 0, sizeof(DsSrcPort_m));
    sal_memset(&ds_dest_port, 0, sizeof(DsDestPort_m));

    SetDsPhyPort(V, outerVlanIsCVlan_f, &ds_phy_port, 0);
    SetDsPhyPort(V, packetTypeValid_f, &ds_phy_port, 0);
    SetDsPhyPort(V, fwdHashGenDis_f, &ds_phy_port, 0);

    SetDsPhyPortExt(V, srcOuterVlanIsSvlan_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, defaultVlanId_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, useDefaultVlanLookup_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, hash1Priority_f, &ds_phy_port_ext, 0);
    SetDsPhyPortExt(V, tcam1Priority_f, &ds_phy_port_ext, 0);
    SetDsPhyPortExt(V, hash2Priority_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, tcam2Priority_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, hashDefaultEntryValid_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, qosPolicy_f, &ds_phy_port_ext, 1); /*default trust port*/
    SetDsPhyPortExt(V, userIdTcamCustomerIdEn_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, exception2Discard_f, &ds_phy_port_ext, 0x80000000);  /*action index 31 used as pause identify*/

    SetDsSrcPort(V, receiveDisable_f, &ds_src_port, 0);
    SetDsSrcPort(V, bridgeEn_f, &ds_src_port, 1);
    SetDsSrcPort(V, portSecurityEn_f, &ds_src_port, 0);
    SetDsSrcPort(V, routeDisable_f, &ds_src_port, 0);
    /* depend on vlan ingress filter */
    SetDsSrcPort(V, ingressFilteringMode_f, &ds_src_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);
    SetDsSrcPort(V, fastLearningEn_f, &ds_src_port, 1);
    SetDsSrcPort(V, aclUseBitmap_f, &ds_src_port, 1);
    SetDsSrcPort(V, discardAllowLearning_f, &ds_src_port, 0);
    SetDsSrcPort(V, forceSecondParser_f, &ds_src_port, 0);

    SetDsDestPort(V, aclUseBitmap_f, &ds_dest_port, 1);
    SetDsDestPort(V, defaultVlanId_f, &ds_dest_port, 1);
    SetDsDestPort(V, untagDefaultSvlan_f, &ds_dest_port, 1);
    SetDsDestPort(V, untagDefaultVlanId_f, &ds_dest_port, 1);
    SetDsDestPort(V, bridgeEn_f, &ds_dest_port, 1);
    SetDsDestPort(V, transmitDisable_f, &ds_dest_port, 0);
    SetDsDestPort(V, dot1QEn_f, &ds_dest_port, CTC_DOT1Q_TYPE_BOTH);
    SetDsDestPort(V, stpCheckEn_f, &ds_dest_port, 1);
    SetDsDestPort(V, svlanTpidValid_f, &ds_dest_port, 0);
    SetDsDestPort(V, cvlanSpace_f, &ds_dest_port, 0);
    SetDsDestPort(V, vlanHash1DefaultEntryValid_f, &ds_dest_port, 1);
    SetDsDestPort(V, vlanHash2DefaultEntryValid_f, &ds_dest_port, 1);
    SetDsDestPort(V, replaceStagTpid_f, &ds_dest_port, 1);
    SetDsDestPort(V, stagOperationDisable_f, &ds_dest_port, 0);
    SetDsDestPort(V, sourcePortToHeader_f, &ds_dest_port, 1);

     /* depend on vlan ingress filter */
    SetDsDestPort(V, egressFilteringMode_f, &ds_dest_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);

    sys_goldengate_get_gchip_id(lchip, &gchip);

    p_gg_port_master[lchip]->use_logic_port_check = (p_port_global_cfg->default_logic_port_en)? TRUE : FALSE;

    for (index = 0; index < SYS_GG_MAX_PORT_NUM_PER_CHIP; index++)
    {
        if (0 == ((index >> 6) & 3)) /* x00xxxxxx*/
        {
            SetDsSrcPort(V, aclPortNum_f, &ds_src_port, (64 * (index >> 8)) + (index & 0x3F));
            SetDsDestPort(V, aclPortNum_f, &ds_dest_port, (64 *(index >> 8)) + (index & 0x3F));
        }

        cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_phy_port));

        cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_phy_port_ext));

        cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_src_port));

        cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_dest_port));

        gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(index));
        CTC_ERROR_RETURN(_sys_goldengate_port_set_port_blocking(lchip, gport, 0));

        if ((index & 0xFF) < 128)
        {
            val = index & 0x7F ;
            cmd = DRV_IOW(DsPortLinkAgg_t, DsPortLinkAgg_u1_g2_sourceChannel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &val));
        }

        sys_goldengate_port_set_global_port(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(index), gport, FALSE);
    }

    /* during porting phase suppose all port is gport */
    /* !!!!!!!!!!!it should config mac according to datapath later!!!!!!!!!*/
    CTC_ERROR_RETURN(_sys_goldengate_chan_init(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_port_restart_auto_neg(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_port_monitor_link(lchip));

    CTC_ERROR_RETURN(sys_goldengate_port_flow_ctl_init(lchip));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_PORT, sys_goldengate_port_wb_sync));

    if (CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_port_wb_restore(lchip));
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_LCHIP_NUM, SYS_GG_MAX_LOCAL_CHIP_NUM));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_PHY_PORT_NUM, SYS_GG_MAX_PHY_PORT));
    CTC_ERROR_RETURN(sys_goldengate_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_PORT_NUM, SYS_GG_MAX_PORT_NUM_PER_CHIP));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_remove_auto_neg_restart(uint8 lchip)
{
    if (1 == p_gg_port_master[lchip]->auto_neg_restart_status)
    {
        p_gg_port_master[lchip]->auto_neg_restart_status = 0;
        sal_task_destroy(p_gg_port_master[lchip]->p_auto_neg_restart);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_remove_port_monitor_link(uint8 lchip)
{
    drv_work_platform_type_t platform_type;

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    sal_memset(p_gg_port_master[lchip]->scan_log, 0, sizeof(sys_goldengate_scan_log_t)*SYS_PORT_MAX_LOG_NUM);

    sal_task_destroy(p_gg_port_master[lchip]->p_monitor_scan);

    return CTC_E_NONE;
}

int32
sys_goldengate_port_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NONE;
    }

    _sys_goldengate_port_remove_auto_neg_restart(lchip);
    _sys_goldengate_remove_port_monitor_link(lchip);
    mem_free(p_gg_port_master[lchip]->egs_port_prop);
    mem_free(p_gg_port_master[lchip]->igs_port_prop);

    sal_mutex_destroy(p_gg_port_master[lchip]->p_port_mutex);
    mem_free(p_gg_port_master[lchip]);
    return CTC_E_NONE;
}

/**
 @brief set the port default configure
*/
int32
sys_goldengate_port_set_default_cfg(uint8 lchip, uint16 gport)
{
    int32 ret = CTC_E_NONE;

    uint32 lport = 0;
    uint32 field = 0;
    DsPhyPort_m ds_phy_port;
    DsPhyPortExt_m ds_phy_port_ext;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X default configure!\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /*init asic table*/
    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));
    sal_memset(&ds_phy_port_ext, 0, sizeof(DsPhyPortExt_m));
    sal_memset(&ds_src_port, 0, sizeof(DsSrcPort_m));
    sal_memset(&ds_dest_port, 0, sizeof(DsDestPort_m));

    SetDsPhyPort(V, outerVlanIsCVlan_f, &ds_phy_port, 0);
    SetDsPhyPort(V, packetTypeValid_f, &ds_phy_port, 0);

    SetDsPhyPortExt(V, globalSrcPort_f, &ds_phy_port_ext, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
    SetDsPhyPortExt(V, srcOuterVlanIsSvlan_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, defaultVlanId_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, useDefaultVlanLookup_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, exception2Discard_f, &ds_phy_port_ext, 0x80000001);
    SetDsPhyPortExt(V, exception2En_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, hash2Priority_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, hashDefaultEntryValid_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, qosPolicy_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, tcam2Priority_f, &ds_phy_port_ext, 1);
    SetDsPhyPortExt(V, userIdTcamCustomerIdEn_f, &ds_phy_port_ext, 1);

    SetDsSrcPort(V, receiveDisable_f, &ds_src_port, 0);
    SetDsSrcPort(V, bridgeEn_f, &ds_src_port, 1);
    SetDsSrcPort(V, routeDisable_f, &ds_src_port, 0);
    SetDsSrcPort(V, ingressFilteringMode_f, &ds_src_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);
    SetDsSrcPort(V, aclUseBitmap_f, &ds_src_port, 1);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_fastLearningEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field));
    SetDsSrcPort(V, fastLearningEn_f, &ds_src_port, field);

    SetDsDestPort(V, globalDestPort_f, &ds_dest_port, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));
    SetDsDestPort(V, defaultVlanId_f, &ds_dest_port, 1);
    SetDsDestPort(V, untagDefaultSvlan_f, &ds_dest_port, 1);
    SetDsDestPort(V, untagDefaultVlanId_f, &ds_dest_port, 1);
    SetDsDestPort(V, bridgeEn_f, &ds_dest_port, 1);
    SetDsDestPort(V, transmitDisable_f, &ds_dest_port, 0);
    SetDsDestPort(V, dot1QEn_f, &ds_dest_port, CTC_DOT1Q_TYPE_BOTH);
    SetDsDestPort(V, stpCheckEn_f, &ds_dest_port, 1);
    SetDsDestPort(V, svlanTpidValid_f, &ds_dest_port, 0);
    SetDsDestPort(V, cvlanSpace_f, &ds_dest_port, 0);
    SetDsDestPort(V, egressFilteringMode_f, &ds_dest_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);
    SetDsDestPort(V, aclUseBitmap_f, &ds_dest_port, 1);
    SetDsDestPort(V, replaceStagTpid_f, &ds_dest_port, 1);
    SetDsDestPort(V, vlanHash1DefaultEntryValid_f, &ds_dest_port, 1);
    SetDsDestPort(V, vlanHash2DefaultEntryValid_f, &ds_dest_port, 1);
    SetDsDestPort(V, sourcePortToHeader_f, &ds_dest_port, 1);

    if (0 == ((lport >> 6) & 3))
    {
        SetDsSrcPort(V, aclPortNum_f, &ds_src_port, (64 * (lport >> 8)) + (lport & 0x3F));
        SetDsDestPort(V, aclPortNum_f, &ds_dest_port, (64 * (lport >> 8)) + (lport & 0x3F));
    }

    /*do write table*/
    PORT_LOCK;
    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port), p_gg_port_master[lchip]->p_port_mutex);

    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port_ext), p_gg_port_master[lchip]->p_port_mutex);

    cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_src_port), p_gg_port_master[lchip]->p_port_mutex);

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port), p_gg_port_master[lchip]->p_port_mutex);

    p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].global_src_port = gport;
    PORT_UNLOCK;

    sys_goldengate_port_set_global_port(lchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport), gport, FALSE);
    ret = sys_goldengate_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    if (CTC_E_ENTRY_EXIST != ret && CTC_E_NONE != ret)
    {
        return ret;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_update_mc_linkagg(uint8 lchip, uint8 tid, uint16 lport, bool is_add)
{

    uint8 mc_port_id = 0;
    uint8 mc_port_type = 0;
    uint32 cmd = 0;
    uint16 bitmap_index = 0;
    uint16 port_index = 0;
    uint32 bmp[4] = {0};
    DsMetLinkAggregatePort_m ds_met_link_aggregate_port;



    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "tid = %d, lport=%d, is_add=%d\n", tid, lport, is_add);

    SYS_LINKAGG_MC_MAP_PBM_PORTID(lport, mc_port_type, mc_port_id);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mc_port_type = %d, mc_port_id=%d\n", mc_port_type, mc_port_id);

    bitmap_index = (tid << 2) + mc_port_type;
    cmd = DRV_IOR(DsMetLinkAggregateBitmap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, bitmap_index, cmd, bmp));

    if (is_add)
    {
        CTC_BMP_SET(bmp, mc_port_id);
    }
    else
    {
        CTC_BMP_UNSET(bmp, mc_port_id);
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsMetLinkAggregateBitmap_t index = %d, bmp:0x%x:%x:%x:%x\n",
                        bitmap_index, bmp[3], bmp[2], bmp[1], bmp[0]);


    port_index = mc_port_id + (mc_port_type<<7);


    cmd = DRV_IOR(DsMetLinkAggregatePort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_index, cmd, &ds_met_link_aggregate_port));

    if (is_add)
    {
        cmd = DRV_IOW(DsMetLinkAggregateBitmap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, bitmap_index, cmd, bmp));

        DRV_SET_FIELD_V(DsMetLinkAggregatePort_t,
                            DsMetLinkAggregatePort_linkAggregationEn_f,
                            &ds_met_link_aggregate_port,
                            TRUE);

        DRV_SET_FIELD_V(DsMetLinkAggregatePort_t,
                            DsMetLinkAggregatePort_linkAggregationId_f,
                            &ds_met_link_aggregate_port,
                            tid);

        cmd = DRV_IOW(DsMetLinkAggregatePort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_index, cmd, &ds_met_link_aggregate_port));

    }
    else
    {
        DRV_SET_FIELD_V(DsMetLinkAggregatePort_t,
                            DsMetLinkAggregatePort_linkAggregationEn_f,
                            &ds_met_link_aggregate_port,
                            FALSE);

        DRV_SET_FIELD_V(DsMetLinkAggregatePort_t,
                            DsMetLinkAggregatePort_linkAggregationId_f,
                            &ds_met_link_aggregate_port,
                            0);

        cmd = DRV_IOW(DsMetLinkAggregatePort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_index, cmd, &ds_met_link_aggregate_port));

        cmd = DRV_IOW(DsMetLinkAggregateBitmap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, bitmap_index, cmd, bmp));
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "DsMetLinkAggregatePort_t index = %d\n", port_index);

    return CTC_E_NONE;
}
/**
 @brief set the port global_src_port and global_dest_port in system, the lport means ctc/sys level lport
*/
int32
sys_goldengate_port_set_global_port(uint8 lchip, uint16 lport, uint16 gport, bool update_mc_linkagg)
{
    int32  ret = CTC_E_NONE;
    uint32 field_value = 0;
    uint32 cmd = 0;
    uint16 old_gport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                     "Set Global port, lchip=%u, lport=%u, gport=0x%04X\n", lchip, lport, gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);

    if (SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) > SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    field_value = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport);

    PORT_LOCK;
    /*do write table*/
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_globalSrcPort_f);
    ret = DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_value);

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_globalDestPort_f);
    ret = ret ? ret : DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_value);

    if (p_gg_port_master[lchip]->use_logic_port_check)
    {
        field_value = 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_useDefaultLogicSrcPort_f);
        ret = DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_value);

        /*for learning use logic port
          Logic port:
          0-(MAX_PORT_NUM_PER_CHIP-1)
              -----------> local phy port
          MAX_PORT_NUM_PER_CHIP - (MAX_PORT_NUM_PER_CHIP+CTC_MAX_LINKAGG_GROUP_NUM-1)
              ---------> linkagg 0-(CTC_MAX_LINKAGG_GROUP_NUM-1), base:MAX_PORT_NUM_PER_CHIP
          MAX_PORT_NUM_PER_CHIP+CTC_MAX_LINKAGG_GROUP_NUM -
              -------> global logic port for userid and vpls learing, base:MAX_PORT_NUM_PER_CHIP+CTC_MAX_LINKAGG_GROUP_NUM
        */
        if (CTC_IS_LINKAGG_PORT(gport))
        {
            field_value = CTC_GPORT_LINKAGG_ID(gport) + SYS_GG_MAX_PORT_NUM_PER_CHIP - 1;
        }
        else
        {
            field_value  = lport;
        }

        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultLogicSrcPort_f);
        ret = DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_value);

        field_value  = lport;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_logicDestPort_f);
        ret = ret ? ret : DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &field_value);
    }

    if (ret)
    {
        PORT_UNLOCK;
        return ret;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "igs_port_prop[%d].global_src_port(drv_port) = 0x%x.\n", lport, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport));

    old_gport = p_gg_port_master[lchip]->igs_port_prop[lport].global_src_port;
    p_gg_port_master[lchip]->igs_port_prop[lport].global_src_port = gport;

    if (CTC_IS_LINKAGG_PORT(gport) && update_mc_linkagg)
    {
        /*update mcast linkagg bitmap*/
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_update_mc_linkagg(lchip, CTC_GPORT_LINKAGG_ID(gport), SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), TRUE));
    }
    else if (CTC_IS_LINKAGG_PORT(old_gport) && update_mc_linkagg)
    {
        /*update mcast linkagg bitmap*/
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_update_mc_linkagg(lchip, CTC_GPORT_LINKAGG_ID(old_gport), SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), FALSE));
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief get the port global_src_port and global_dest_port in system. Src and dest are equal.
*/
int32
sys_goldengate_port_get_global_port(uint8 lchip, uint16 lport, uint16* p_gport)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                     "Get Global port, lchip=%d, lport=%d\n", lchip, lport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_gport);

    *p_gport = p_gg_port_master[lchip]->igs_port_prop[lport].global_src_port;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_port_en(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint16 chan_id = 0;

    ctc_chip_device_info_t dev_info;
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, port enable:%d!\n", gport, enable);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    value = (TRUE == enable) ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_receiveDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    value = (TRUE == enable) ? 0 : 1;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_transmitDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    /* clear link status to up*/
    if (TRUE == enable)
    {
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
        if (0xFF != chan_id)
        {
            value = 0;
            cmd = DRV_IOW(QMgrEnqLinkState_t, QMgrEnqLinkState_linkState_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &value));
            cmd = DRV_IOW(DlbChanStateCtl_t, DlbChanStateCtl_linkState_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &value));
        }
    }

    /* check chip version */
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id <= 1)
    {
        if (p_gg_port_master[lchip]->igs_port_prop[lport].link_intr_en)
        {
            _sys_goldengate_port_set_link_intr(lchip, gport, enable);
        }
    }
#if 0
    ret = sys_goldengate_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    if (CTC_E_ENTRY_EXIST != ret && CTC_E_NONE != ret)
    {
        return ret;
    }
#endif

    if (!p_gg_port_master[lchip]->mlag_isolation_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_port_set_mcast_member_down(lchip, gport, enable ? 0 : 1));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_port_en(uint8 lchip, uint16 gport, uint32* p_enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 receive = 0;
    uint32 transmit = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X, port enable!\n", gport);

    CTC_PTR_VALID_CHECK(p_enable);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_receiveDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &receive));

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_transmitDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &transmit));

    if ((0 == receive) && (0 == transmit))
    {
        *p_enable = TRUE;
    }
    else
    {
        *p_enable = FALSE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_cut_through_en(uint8 lchip, uint16 gport,uint32 value)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 speed = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X!\n", gport);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    speed = value? sys_goldengate_get_cut_through_speed(lchip, gport) :0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_speed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &speed));

    cmd = DRV_IOW(DsDestPortSpeed_t, DsDestPortSpeed_speed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &speed));
    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_port_get_cut_through_en(uint8 lchip, uint16 gport,uint32 *p_value)
{
    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 speed = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X!\n", gport);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_speed_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &speed));

    *p_value = (speed !=0);

    return CTC_E_NONE;
}

/**
 @brief decide the port whether is routed port
*/
STATIC int32
_sys_goldengate_port_set_routed_port(uint8 lchip, uint16 gport, uint32 is_routed)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = (TRUE == is_routed) ? 1 : 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, routed port:%d!\n", gport, is_routed);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /*do write table*/
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_routedPort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_routedPort_f);
    ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

    return ret;
}

/**
 @brief get the port whether is routed port
*/
STATIC int32
_sys_goldengate_port_get_routed_port(uint8 lchip, uint16 gport, bool* p_is_routed)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value1 = 0;
    uint32 value2 = 0;

    CTC_PTR_VALID_CHECK(p_is_routed);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X!\n", gport);

    /*do write table*/
    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_routedPort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &value1);

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_routedPort_f);
    ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value2);

    if (value1 && value2)
    {
        *p_is_routed = TRUE;
    }
    else
    {
        *p_is_routed = FALSE;
    }

    return ret;
}

/**
 @brief bind phy_if to port
*/
int32
sys_goldengate_port_set_phy_if_en(uint8 lchip, uint16 gport, bool enable)
{
    int32  ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 field_value = 0;
    uint32 cmd = 0;
    uint32 is_phy = FALSE;
    uint16 global_src_port = 0;
    sys_l3if_prop_t l3if_prop;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, phy_if enable:%d\n", gport, enable);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_GLOBAL_PORT_CHECK(gport);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    PORT_LOCK;

    global_src_port = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].global_src_port;
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));

    l3if_prop.gport = global_src_port;
    l3if_prop.l3if_type = CTC_L3IF_TYPE_PHY_IF;
    l3if_prop.vaild     = 1;

    is_phy = sys_goldengate_l3if_is_port_phy_if(lchip, global_src_port);

    if ((TRUE == enable) && (FALSE == is_phy))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_NOT_EXIST, p_gg_port_master[lchip]->p_port_mutex);
    }

#if 0 /*allow destroy interfaced first, then disable in port*/
    if ((FALSE == enable) && (TRUE == is_phy))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_EXIST, p_gg_port_master[lchip]->p_port_mutex);
    }

#endif

    if (TRUE == enable)
    {
        ret = _sys_goldengate_port_set_routed_port(lchip, gport, TRUE);
        ret = ret ? ret : sys_goldengate_l3if_get_l3if_info(lchip, 0, &l3if_prop);

        field_value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_dot1QEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = l3if_prop.l3if_id;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_interfaceId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_defaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        /* enable phy if should disable bridge */
        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gg_port_master[lchip]->p_port_mutex);
    }
    else
    {
        ret = _sys_goldengate_port_set_routed_port(lchip, gport, FALSE);

        field_value = CTC_DOT1Q_TYPE_BOTH;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_dot1QEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_interfaceId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_stpDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_defaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        /* disable phy if should enable bridge */
        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gg_port_master[lchip]->p_port_mutex);
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief get phy_if whether enable on port
*/

int32
sys_goldengate_port_get_phy_if_en(uint8 lchip, uint16 gport, uint16* p_l3if_id, bool* p_enable)
{
    bool   is_routed = FALSE;

    int32  ret = CTC_E_NONE;
    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint32 is_phy = 0;
    DsSrcPort_m ds_src_port;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X, phyical interface!\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    CTC_PTR_VALID_CHECK(p_l3if_id);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    is_phy = sys_goldengate_l3if_is_port_phy_if(lchip, gport);
    CTC_ERROR_RETURN(_sys_goldengate_port_get_routed_port(lchip, gport, &is_routed));

    if ((TRUE == is_routed) && (TRUE == is_phy))
    {
        *p_enable = TRUE;
    }
    else
    {
        *p_enable = FALSE;
    }

    sal_memset(&ds_src_port, 0, sizeof(DsSrcPort_m));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_src_port));

    GetDsSrcPort(A, interfaceId_f, &ds_src_port, &value);
    *p_l3if_id = value;

    return ret;
}

int32
sys_goldengate_port_set_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool enable)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    bool is_svlan = FALSE;
    uint8 direcion = p_vrange_info->direction;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, direction:%d, vrange_grpid:%d\n",
                     gport, p_vrange_info->direction, p_vrange_info->vrange_grpid);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(p_vrange_info);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_vlan_range_type(lchip, p_vrange_info, &is_svlan));

    if (CTC_INGRESS == direcion)
    {
        if (!enable)
        {
            field_val = 0;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_vlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
        else
        {
            if (is_svlan)
            {
                field_val = 1;
            }
            else
            {
                field_val = 0;
            }

            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_vlanRangeType_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = 1;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_vlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = p_vrange_info->vrange_grpid;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_vlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
    }
    else
    {
        if (!enable)
        {
            field_val = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_vlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
        else
        {
            if (is_svlan)
            {
                field_val = 1;
            }
            else
            {
                field_val = 0;
            }

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_vlanRangeType_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_vlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = p_vrange_info->vrange_grpid;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_vlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
    }

    return ret;
}

int32
sys_goldengate_port_get_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool* p_enable)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 direction = p_vrange_info->direction;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X, vlan range!\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    CTC_PTR_VALID_CHECK(p_vrange_info);
    CTC_BOTH_DIRECTION_CHECK(direction);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_vlanRangeProfileEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        if (0 == field_val)
        {
            *p_enable = FALSE;
        }
        else
        {
            cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_vlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
            p_vrange_info->vrange_grpid = field_val;
            *p_enable = TRUE;

        }
    }
    else
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_vlanRangeProfileEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        if (0 == field_val)
        {
            *p_enable = FALSE;
        }
        else
        {
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_vlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
            p_vrange_info->vrange_grpid = field_val;
            *p_enable = TRUE;
        }
    }

    return ret;
}

STATIC int32
_sys_goldengate_port_set_scl_key_type_ingress(uint8 lchip, uint16 lport, uint8 scl_id, ctc_port_scl_key_type_t type)
{
    DsPhyPortExt_m ds;
    uint32 cmd = 0;

    sys_goldengate_igs_port_scl_map_t scl_map[CTC_SCL_KEY_TYPE_MAX] =
    {
        /* hash_type                       , tcam_type   ,en,high,ul,lab,ad,da,v4,gre,rpf,auto,nvgre,v6_nvgre,v4_vxlan,v6_vxlan*/
        {USERIDPORTHASHTYPE_DISABLE        , TCAMDISABLE , 0, 0 , 0 , 0 , 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_DISABLE                */
        {USERIDPORTHASHTYPE_PORT           , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_PORT      */
        {USERIDPORTHASHTYPE_CVLANPORT      , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID      */
        {USERIDPORTHASHTYPE_SVLANPORT      , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID      */
        {USERIDPORTHASHTYPE_CVLANCOSPORT   , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID_CCOS */
        {USERIDPORTHASHTYPE_SVLANCOSPORT   , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID_SCOS */
        {USERIDPORTHASHTYPE_DOUBLEVLANPORT , TCAMVLANKEY , 1, 0 , 0 , 0 , 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_DVID      */
        {USERIDPORTHASHTYPE_MAC            , TCAML2KEY   , 1, 1 , 1 , 62, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_CLASS_MAC_SA      */
        {USERIDPORTHASHTYPE_MAC            , TCAML2KEY   , 1, 1 , 1 , 62, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_CLASS_MAC_DA      */
        {USERIDPORTHASHTYPE_IPSA           , TCAML3KEY   , 1, 1 , 1 , 62, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_CLASS_IPV4        */
        {USERIDPORTHASHTYPE_IPSA           , TCAML3KEY   , 1, 1 , 1 , 62, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_VLAN_CLASS_IPV6        */
        {USERIDPORTHASHTYPE_MACPORT        , TCAML2KEY   , 1, 1 , 0 , 63, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPSG_PORT_MAC          */
        {USERIDPORTHASHTYPE_IPSAPORT       , TCAML3KEY   , 1, 1 , 0 , 63, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPSG_PORT_IP           */
        {USERIDPORTHASHTYPE_IPSA           , TCAML3KEY   , 1, 1 , 0 , 63, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPSG_IPV6              */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 1, 1, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPV4_TUNNEL            */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 1, 1, 0, 1, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPV4_TUNNEL_AUTO       */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPV6_TUNNEL            */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 1, 1, 1, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF   */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 0, 0, 1, 0, 0, 0, 0, 0} , /*CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF   */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 0, 1, 0, 0, 1, 1, 0, 0} , /*CTC_SCL_KET_TYPE_NVGRE                  */
        {USERIDPORTHASHTYPE_TUNNEL         , TCAML3KEY   , 1, 0 , 0 , 0 , 0, 0, 0, 0, 0, 0, 0, 0, 1, 1} , /*CTC_SCL_KET_TYPE_VXLAN                  */
    };

    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &ds));
    if (0 == scl_id)
    {
        p_gg_port_master[lchip]->igs_port_prop[lport].flag = type;
    }
    else
    {
        p_gg_port_master[lchip]->igs_port_prop[lport].flag_ext = type;
    }

    {
        if (CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF == type)
        {
            if (0 == scl_id)
            {
                return CTC_E_INVALID_PARAM;
            }
        }

        if (CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF == type)
        {
            if (0 == scl_id)
            {
                return CTC_E_INVALID_PARAM;
            }
        }
    }

    if (scl_id == 0)
    {
        SetDsPhyPortExt(V, hash1UseDa_f, &ds, scl_map[type].use_macda);
        SetDsPhyPortExt(V, userIdPortHash1Type_f, &ds, scl_map[type].hash_type);
        SetDsPhyPortExt(V, userIdTcam1En_f, &ds, scl_map[type].tcam_en);
        SetDsPhyPortExt(V, userIdTcam1Type_f, &ds, scl_map[type].tcam_type);
        SetDsPhyPortExt(V, hashLookup1UseLabel_f , &ds, scl_map[type].use_label);
        SetDsPhyPortExt(V, userIdLabel1_f, &ds, scl_map[type].label);

        SetDsPhyPortExt(V, ipv4TunnelHashEn1_f, &ds, scl_map[type].ipv4_tunnel_en);
        SetDsPhyPortExt(V, ipv4GreTunnelHashEn1_f, &ds, scl_map[type].ipv4_gre_tunnel_en);
        SetDsPhyPortExt(V, autoTunnelEn_f, &ds, scl_map[type].auto_tunnel);
        SetDsPhyPortExt(V, autoTunnelForHash1_f, &ds, scl_map[type].auto_tunnel);
        SetDsPhyPortExt(V, nvgreEnable_f, &ds, scl_map[type].nvgre_en);
        SetDsPhyPortExt(V, ipv4VxlanTunnelHashEn1_f, &ds, scl_map[type].v4_vxlan_en);
        SetDsPhyPortExt(V, ipv6NvgreTunnelHashEn1_f, &ds, scl_map[type].v6_nvgre_en);
        SetDsPhyPortExt(V, ipv6VxlanTunnelHashEn1_f, &ds, scl_map[type].v6_vxlan_en);

        SetDsPhyPortExt(V, tcam1IsUserId_f, &ds, scl_map[type].is_userid);
        SetDsPhyPortExt(V, tcam1IsSclFlow_f, &ds, 0);
        SetDsPhyPortExt(V, sclFlowHashEn_f , &ds, 0);
    }
    else
    {
        SetDsPhyPortExt(V, hash2UseDa_f, &ds, scl_map[type].use_macda);
        SetDsPhyPortExt(V, userIdPortHash2Type_f, &ds, scl_map[type].hash_type);
        SetDsPhyPortExt(V, userIdTcam2En_f, &ds, scl_map[type].tcam_en);
        SetDsPhyPortExt(V, userIdTcam2Type_f, &ds, scl_map[type].tcam_type);
        SetDsPhyPortExt(V, hashLookup2UseLabel_f , &ds, scl_map[type].use_label);
        SetDsPhyPortExt(V, userIdLabel2_f, &ds, scl_map[type].label);

        SetDsPhyPortExt(V, ipv4TunnelHashEn2_f, &ds, scl_map[type].ipv4_tunnel_en);
        SetDsPhyPortExt(V, ipv4GreTunnelHashEn2_f, &ds, scl_map[type].ipv4_gre_tunnel_en);
        SetDsPhyPortExt(V, autoTunnelEn_f, &ds, scl_map[type].auto_tunnel);
        SetDsPhyPortExt(V, tunnelRpfCheck_f, &ds, scl_map[type].rpf_check_en);
        SetDsPhyPortExt(V, autoTunnelForHash1_f, &ds, !(scl_map[type].auto_tunnel));
        SetDsPhyPortExt(V, nvgreEnable_f, &ds, scl_map[type].nvgre_en);
        SetDsPhyPortExt(V, ipv4VxlanTunnelHashEn2_f, &ds, scl_map[type].v4_vxlan_en);
        SetDsPhyPortExt(V, ipv6NvgreTunnelHashEn2_f, &ds, scl_map[type].v6_nvgre_en);
        SetDsPhyPortExt(V, ipv6VxlanTunnelHashEn2_f, &ds, scl_map[type].v6_vxlan_en);

        SetDsPhyPortExt(V, tcam2IsUserId_f, &ds, scl_map[type].is_userid);
        SetDsPhyPortExt(V, tcam2IsSclFlow_f, &ds, 0);
    }

    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &ds));
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ingressTagHighPriority_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &scl_map[type].vlan_high_priority));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_scl_key_type_egress(uint8 lchip, uint16 lport, uint8 scl_id, ctc_port_scl_key_type_t type)
{
    DsDestPort_m ds;
    uint32 cmd = 0;
    sys_goldengate_egs_port_scl_map_t scl_map[CTC_SCL_KEY_TYPE_MAX] =
    {
        /* hash_type                      ,ul ,lab*/
        {EGRESSXCOAMHASHTYPE_DISABLE       , 0 , 0 } , /*CTC_SCL_KEY_TYPE_DISABLE                */
        {EGRESSXCOAMHASHTYPE_PORT          , 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_PORT      */
        {EGRESSXCOAMHASHTYPE_CVLANPORT     , 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID      */
        {EGRESSXCOAMHASHTYPE_SVLANPORT     , 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID      */
        {EGRESSXCOAMHASHTYPE_CVLANCOSPORT  , 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID_CCOS */
        {EGRESSXCOAMHASHTYPE_SVLANCOSPORT  , 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID_SCOS */
        {EGRESSXCOAMHASHTYPE_DOUBLEVLANPORT, 0 , 0 } , /*CTC_SCL_KEY_TYPE_VLAN_MAPPING_DVID      */
    };
    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &ds));
    if (0 == scl_id)
    {
        p_gg_port_master[lchip]->egs_port_prop[lport].flag = type;
    }
    else
    {
        p_gg_port_master[lchip]->egs_port_prop[lport].flag_ext= type;
    }


    if (scl_id == 0)
    {
        SetDsDestPort(V, vlanHash1Type_f, &ds, scl_map[type].hash_type);
    }
    else
    {
        SetDsDestPort(V, vlanHash2Type_f, &ds, scl_map[type].hash_type);
    }
    SetDsDestPort(V, vlanHashUseLabel_f, &ds, scl_map[type].use_label);
    SetDsDestPort(V, vlanHashLabel_f , &ds, scl_map[type].label);

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport), cmd, &ds));
    return CTC_E_NONE;
}
/**
 @brief bind phy_if to port
*/
int32
sys_goldengate_port_set_sub_if_en(uint8 lchip, uint16 gport, bool enable)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 field_value = 0;
    uint32 cmd = 0;
    uint32 is_sub = FALSE;
    uint16 global_src_port = 0;
    sys_l3if_prop_t l3if_prop;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, phy_if enable:%d\n", gport, enable);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    PORT_LOCK;

    global_src_port = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].global_src_port;
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
    l3if_prop.gport     = global_src_port;
    l3if_prop.l3if_type = CTC_L3IF_TYPE_SUB_IF;
    l3if_prop.vaild     = 1;

    is_sub = sys_goldengate_l3if_is_port_sub_if(lchip, global_src_port);

    if ((TRUE == enable) && (FALSE == is_sub))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_NOT_EXIST, p_gg_port_master[lchip]->p_port_mutex);
    }


    if (TRUE == enable)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_port_set_scl_key_type_ingress(lchip,
                                                     CTC_MAP_GPORT_TO_LPORT(gport),
                                                     0,
                                                     CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID),
                                                     p_gg_port_master[lchip]->p_port_mutex);

        ret = ret ? ret : _sys_goldengate_port_set_routed_port(lchip, gport, TRUE);


          /* enable sub if should disable bridge */
        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gg_port_master[lchip]->p_port_mutex);

        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].subif_en = TRUE;


    }
    else if (p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].subif_en)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_goldengate_port_set_scl_key_type_ingress(lchip,
                                                     CTC_MAP_GPORT_TO_LPORT(gport),
                                                     0,
                                                     CTC_SCL_KEY_TYPE_DISABLE),
                                                     p_gg_port_master[lchip]->p_port_mutex);

        ret = ret ? ret : _sys_goldengate_port_set_routed_port(lchip, gport, FALSE);

        /* enable sub if should disable bridge */
        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gg_port_master[lchip]->p_port_mutex);

        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].subif_en = FALSE;
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief get phy_if whether enable on port
*/
int32
sys_goldengate_port_get_sub_if_en(uint8 lchip, uint16 gport,  bool* p_enable)
{

    uint16 lport = 0;

    CTC_PTR_VALID_CHECK(p_enable);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    *p_enable = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].subif_en;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 scl_id, ctc_port_scl_key_type_t type)
{

    uint16 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, direction:%d, scl_id:%d, scl key type:%d\n",
                     gport, direction, scl_id, type);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_BOTH_DIRECTION_CHECK(direction);
    CTC_MAX_VALUE_CHECK(scl_id, 1);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (type >= CTC_SCL_KEY_TYPE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_INGRESS == direction)
    {
        CTC_ERROR_RETURN(_sys_goldengate_port_set_scl_key_type_ingress(lchip, CTC_MAP_GPORT_TO_LPORT(gport), scl_id, type));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_port_set_scl_key_type_egress(lchip, CTC_MAP_GPORT_TO_LPORT(gport), scl_id, type));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 scl_id, ctc_port_scl_key_type_t* p_type)
{

    uint16 lport = 0;
    uint8* p_flag = NULL;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get gport:0x%04X, direction:%d, scl_id:%d\n", gport, direction, scl_id);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_type);
    CTC_BOTH_DIRECTION_CHECK(direction);
    CTC_MAX_VALUE_CHECK(scl_id, 1);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (CTC_INGRESS == direction)
    {
        if (0 == scl_id)
        {
            p_flag = &p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].flag;
        }
        else
        {
            p_flag = &p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].flag_ext;
        }
    }
    else
    {
        if (0 == scl_id)
        {
            p_flag = &p_gg_port_master[lchip]->egs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].flag;
        }
        else
        {
            p_flag = &p_gg_port_master[lchip]->egs_port_prop[CTC_MAP_GPORT_TO_LPORT(gport)].flag_ext;
        }
    }

    if (*p_flag >= CTC_SCL_KEY_TYPE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    else
    {
        *p_type = *p_flag;
    }

    return CTC_E_NONE;
}

/*-------------------------mac serdes--------------------------------------*/
int32
sys_goldengate_port_get_quad_use_num(uint8 lchip, uint8 gmac_id, uint8* p_use_num)
{
    uint8 quad_id = 0;
    uint8 temp = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_use_num);

    quad_id = gmac_id / 4;

    if (p_gg_port_master[lchip]->igs_port_prop[quad_id * 4].port_mac_en)
    {
        temp++;
    }

    if (p_gg_port_master[lchip]->igs_port_prop[quad_id * 4 + 1].port_mac_en)
    {
        temp++;
    }

    if (p_gg_port_master[lchip]->igs_port_prop[quad_id * 4 + 2].port_mac_en)
    {
        temp++;
    }

    if (p_gg_port_master[lchip]->igs_port_prop[quad_id * 4 + 3].port_mac_en)
    {
        temp++;
    }

    *p_use_num = temp;

    return CTC_E_NONE;
}

/**
 @brief retrive mac config
*/
int32
_sys_goldengate_port_retrieve_gmac_cfg(uint8 lchip, uint8 gmac_id)
{
#ifdef NEVER
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    bool enable = FALSE;
    uint16 mtu1 = 0;
    uint16 mtu2 = 0;
    uint8 index = 0;

    index = (gmac_id>>2);

    mtu1 = (uint16)p_gg_port_master[lchip]->pp_mtu1[lchip][index];
    mtu2 = p_gg_port_master[lchip]->pp_mtu2[lchip][index];

    /* retrieve mac mut1 */
    field_value = (uint32)mtu1;
    tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
    tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
    cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_PacketLenMtu1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve mac mut2 */
    field_value = (uint32)mtu2;
    tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
    tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
    cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_PacketLenMtu2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve saturate enable */
    CTC_ERROR_RETURN(sys_goldengate_stats_get_saturate_en(lchip, CTC_STATS_TYPE_GMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
        cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_IncrSaturate_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* retrieve clear_after_read */
    CTC_ERROR_RETURN(sys_goldengate_stats_get_clear_after_read_en(lchip, CTC_STATS_TYPE_GMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
        cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_ClearOnRead_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


/**
 @brief retrive sgmac config
*/
int32
_sys_goldengate_port_retrieve_sgmac_cfg(uint8 lchip, uint8 sgmac_id)
{
#ifdef NEVER
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    bool enable = FALSE;
    uint16 mtu1 = 0;
    uint16 mtu2 = 0;

    mtu1 = p_gg_port_master[lchip]->pp_mtu1[lchip][SYS_STATS_SGMAC_STATS_RAM0+sgmac_id];
    mtu2 = p_gg_port_master[lchip]->pp_mtu2[lchip][SYS_STATS_SGMAC_STATS_RAM0+sgmac_id];

    /* retrieve mac mut1 */
    field_value = (uint32)mtu1;
    tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
    tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
    cmd = DRV_IOW(tbl_id, SgmacStatsCfg_PacketLenMtu1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve mac mut2 */
    field_value = (uint32)mtu2;
    tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
    tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
    cmd = DRV_IOW(tbl_id, SgmacStatsCfg_PacketLenMtu2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve saturate enable */
    CTC_ERROR_RETURN(sys_goldengate_stats_get_saturate_en(lchip, CTC_STATS_TYPE_SGMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
        cmd = DRV_IOW(tbl_id, SgmacStatsCfg_IncrSaturate_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* retrieve clear_after_read */
    CTC_ERROR_RETURN(sys_goldengate_stats_get_clear_after_read_en(lchip, CTC_STATS_TYPE_SGMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
        cmd = DRV_IOW(tbl_id, SgmacStatsCfg_ClearOnRead_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


/**
 @brief set sgmac enable
*/
int32
_sys_goldengate_port_release_sgmac(uint8 lchip, uint8 sgmac_id)
{
#ifdef NEVER

    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 pcs_mode = 0;

    if (sgmac_id > 11)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_goldengate_get_sgmac_info(lchip, sgmac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs_mode:%d\n", pcs_mode);

    /* 1. release sgmac reg reset */
    field_id = ResetIntRelated_ResetSgmac0Reg_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 2. cfg sgmac related reg */
    tbl_id = SgmacCfg0_t + sgmac_id;
    if (DRV_SERDES_XGSGMII_MODE == pcs_mode)
    {
        field_value = 1;
    }
    else if (DRV_SERDES_XFI_MODE == pcs_mode)
    {
        field_value = 2;
    }
    else if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        field_value = 3;
    }
    cmd = DRV_IOW(tbl_id, SgmacCfg_PcsMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    cmd = DRV_IOW(tbl_id, SgmacCfg_PtpEn_f);
    field_value = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* need config sgmac mode: xaui+ or xaui2 or xaui, use default value for xaui */

    /* 3. enable sgmac clock */
    tbl_id = ModuleGatedClkCtl_t;
    field_value = 1;
    if (DRV_SERDES_XGSGMII_MODE == pcs_mode)
    {
        field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f + SYS_SGMAC_EN_PCS_CLK_STEP * sgmac_id;
    }
    else if (DRV_SERDES_XFI_MODE == pcs_mode)
    {
        field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f + SYS_SGMAC_EN_PCS_CLK_STEP * sgmac_id;
    }
    else if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        if (sgmac_id < 4)
        {
            return CTC_E_INVALID_PARAM;
        }

        field_id = ModuleGatedClkCtl_EnClkSupSgmac4XgmacPcs_f + SYS_SGMAC_XGMAC_PCS_CLK_STEP * (sgmac_id - 4);
    }

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 4. cfg PCS related reg */
    /* use default value for pcs configuration */

    /* 5. release sgmac logic reset */
    field_id = ResetIntRelated_ResetSgmac0_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    field_value = 0;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 6. release pcs rx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 7. relaese xaui serdes rx soft reset */
    if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        tbl_id = SgmacSoftRst4_t + (sgmac_id - 4);
        field_value = 0;

        for (index = 0; index < 4; index++)
        {
            field_id = SgmacSoftRst_SerdesRx0SoftRst_f + index;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }

    /* 8. de-assert flush op */
    tbl_id = NetTxForceTxCfg_t;
    field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value &= (~(1 << (sgmac_id + 16)));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 10. enable the port in NET TX */
    tbl_id = NetTxChannelEn_t;
    field_id = NetTxChannelEn_PortEnNet59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value |= (1 << (sgmac_id + 16));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 11. release pcs tx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsTxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    CTC_ERROR_RETURN(_sys_goldengate_port_retrieve_sgmac_cfg(lchip, sgmac_id));

    /*delay 10ms, wait mac status ready */
    sal_task_sleep(100);

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

/**
 @brief set sgmac disable
*/
int32
_sys_goldengate_port_reset_sgmac(uint8 lchip, uint8 sgmac_id)
{
#ifdef NEVER
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 pcs_mode = 0;

    if (sgmac_id > 11)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_goldengate_get_sgmac_info(lchip, sgmac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs_mode:%d\n", pcs_mode);

    /* 0. diable sgmac ts */
    tbl_id = SgmacCfg0_t + sgmac_id;
    cmd = DRV_IOW(tbl_id, SgmacCfg_PtpEn_f);
    field_value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    /* 1. set xaui serdes rx soft reset */
    if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        tbl_id = SgmacSoftRst4_t + (sgmac_id - 4);
        field_value = 1;

        for (index = 0; index < 4; index++)
        {
            field_id = SgmacSoftRst_SerdesRx0SoftRst_f + index;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }

    /* 2. set pcs rx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 3. set pcs tx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsTxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 4. set sgmac reg reset */
    field_id = ResetIntRelated_ResetSgmac0Reg_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 5. set sgmac reset */
    field_id = ResetIntRelated_ResetSgmac0_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 6. disable sgmc clock */
    tbl_id = ModuleGatedClkCtl_t;
    field_value = 0;
    if (DRV_SERDES_XGSGMII_MODE == pcs_mode)
    {
        field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f + SYS_SGMAC_EN_PCS_CLK_STEP * sgmac_id;
    }
    else if (DRV_SERDES_XFI_MODE == pcs_mode)
    {
        field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f + SYS_SGMAC_EN_PCS_CLK_STEP * sgmac_id;
    }
    else if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        if (sgmac_id < 4)
        {
            return CTC_E_INVALID_PARAM;
        }

        field_id = ModuleGatedClkCtl_EnClkSupSgmac4XgmacPcs_f + SYS_SGMAC_XGMAC_PCS_CLK_STEP * (sgmac_id - 4);
    }

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 8. flush the residual packet in net tx packet buffer */
    tbl_id = NetTxForceTxCfg_t;
    field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value |= (1 << (sgmac_id + 16));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 9. disable the port in net tx */
    tbl_id = NetTxChannelEn_t;
    field_id = NetTxChannelEn_PortEnNet59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value &= (~(1 << (sgmac_id + 16)));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 11. wait flush, 5ms */
    sal_task_sleep(5);

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

/**
 @brief set gmac enable
*/
int32
_sys_goldengate_port_release_gmac(uint8 lchip, uint8 gmac_id, ctc_port_speed_t mac_speed)
{
#ifdef NEVER
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 speed_value = 0;
    uint8 pcs_mode = 0;

    switch (mac_speed)
    {
    case CTC_PORT_SPEED_10M:
        speed_value = 0;
        break;

    case CTC_PORT_SPEED_100M:
        speed_value = 1;
        break;

    case CTC_PORT_SPEED_1G:
        speed_value = 2;
        break;

    case CTC_PORT_SPEED_2G5:
        speed_value = 3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_goldengate_get_gmac_info(lchip, gmac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, " pcs_mode;%d\n", pcs_mode);

    /* 1. release gmac reg reset */
    field_id = ResetIntRelated_ResetQuadMac0Reg_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 2. cfg gmac relate reg */
    tbl_step1 = QuadMacGmac1Mode0_t - QuadMacGmac0Mode0_t;
    tbl_step2 = QuadMacGmac0Mode1_t - QuadMacGmac0Mode0_t;
    tbl_id = QuadMacGmac0Mode0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
    field_id = QuadMacGmac0Mode_Gmac0SpeedMode_f;
    field_value = speed_value;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 3. enable gmac clock */
    field_id  = ModuleGatedClkCtl_EnClkSupGmac0_f + gmac_id;
    field_value = 1;
    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 4. enable pcs clock */
    tbl_id = ModuleGatedClkCtl_t;
    field_value = 1;
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        if (gmac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }

        field_id = ModuleGatedClkCtl_EnClkSupPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 5. release pcs reg reset */
    tbl_id = ResetIntRelated_t;
    field_value = 0;
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        field_id = ResetIntRelated_ResetQuadPcs0Reg_f + gmac_id / 4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 6. cfg pcs relate reg */
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        tbl_id = QuadPcsPcs0AnegCfg0_t + (gmac_id / 4) * QUADPCS_QUAD_STEP + (gmac_id % 4) * QUADPCS_PCS_STEP;
        field_id = QuadPcsPcs0AnegCfg_Pcs0SpeedMode_f;
    }
    else
    {
        tbl_id = QsgmiiPcs0AnegCfg0_t + (gmac_id / 4) * QSGMIIPCS_QUAD_STEP + (gmac_id % 4) * QSGMIIPCS_PCS_STEP;
        field_id = QsgmiiPcs0AnegCfg_Pcs0SpeedMode_f;
    }

    field_value = speed_value;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 7. release QuadMacApp logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetQuadMac0App_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 8. release pcs logic reset */
    tbl_id = ResetIntRelated_t;
    field_value = 0;
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        if (gmac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }

        field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 9. release gmac logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetGmac0_f + gmac_id;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    if (gmac_id < 32)
    {
        /* 10. De-assert flush operation */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << gmac_id));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 11. enable transmission to phyport right away in NetTx, no need to do */

        /* 12. enable port in NetTx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << gmac_id);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        /* 10. De-assert flush operation */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (gmac_id - 32)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 11. enable transmission to phyport right away in NetTx, no need to do */

        /* 12. enable port in NetTx */
        tbl_id= NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << (gmac_id - 32));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 13. release pcs tx soft reset */
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
        tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
        tbl_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
        field_value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 14. release gmac tx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiTxSoftRst_f;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 15. release gmac rx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 16. release pcs rx soft reset */
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
        tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
        tbl_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
        field_value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 17. enalbe gmac ts*/
    field_value = 1;
    tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
    tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
    tbl_id = QuadMacGmac0PtpCfg0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
    cmd = DRV_IOW(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* set quadmacinitdone */
    tbl_id = QuadMacInitDone0_t + (gmac_id /4);
    field_id = QuadMacInitDone_QuadMacInitDone_f;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* when set mac enable, need retrieve mac configure */
    CTC_ERROR_RETURN(_sys_goldengate_port_retrieve_gmac_cfg(lchip, gmac_id));

    /*delay 10ms, wait mac status ready */
    sal_task_sleep(100);
    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

/**
 @brief set gmac disable
*/
int32
_sys_goldengate_port_reset_gmac(uint8 lchip, uint8 gmac_id)
{
#ifdef NEVER
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8  used_num = 0;
    uint8 pcs_mode = 0;

    /* 0. check whether to reset quadmac/quadpcs */
    CTC_ERROR_RETURN(sys_goldengate_port_get_quad_use_num(lchip, gmac_id, &used_num));
    CTC_ERROR_RETURN(drv_goldengate_get_gmac_info(lchip, gmac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "used_num:%d, pcs_mode;%d\n", used_num, pcs_mode);

    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        if (gmac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    /* disable gmac ts */
    field_value = 0;
    tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
    tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
    tbl_id = QuadMacGmac0PtpCfg0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
    cmd = DRV_IOW(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    if (used_num <= 1)
    {
        /* 1. set quad mac app logic reset */
        tbl_id  = ResetIntRelated_t;
        field_id = ResetIntRelated_ResetQuadMac0App_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 2. set gmac tx soft reset */
        tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
        tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
        tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadMacGmac0SoftRst_Gmac0SgmiiTxSoftRst_f;
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 3. set pcs soft reset */
        if (DRV_SERDES_SGMII_MODE == pcs_mode)
        {
            tbl_id = QuadPcsPcs0SoftRst0_t + gmac_id / 4;
            field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }

        /* 4. set gmac rx soft reset */
        tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
        tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
        tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 5. set gmac reg reset */
        field_id = ResetIntRelated_ResetQuadMac0Reg_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
        cmd = DRV_IOW(ResetIntRelated_t, field_id);
        field_value = 1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 6. set pcs reg reset */
        tbl_id = ResetIntRelated_t;
        field_value = 1;
        if (DRV_SERDES_SGMII_MODE == pcs_mode)
        {
            field_id = ResetIntRelated_ResetQuadPcs0Reg_f + gmac_id / 4;
            cmd = DRV_IOW(ResetIntRelated_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }

    /* 7. set gmac logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetGmac0_f + gmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 8.set Pcs Logic reset */
    tbl_id = ResetIntRelated_t;
    field_value = 1;
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 9. disable gmac clock */
    field_id  = ModuleGatedClkCtl_EnClkSupGmac0_f + gmac_id;
    field_value = 0;
    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 10. disable pcs clock */
    tbl_id = ModuleGatedClkCtl_t;
    field_value = 0;
    if (DRV_SERDES_SGMII_MODE == pcs_mode)
    {
        field_id = ModuleGatedClkCtl_EnClkSupPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    if (gmac_id < 32)
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << gmac_id);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 13. disable port in Net Tx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << gmac_id));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << (gmac_id - 32));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 13. disable port in Net Tx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChanTxDisCfg_PortTxDisNet59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (gmac_id - 32)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 12. wait flush, 5ms */
    sal_task_sleep(5);

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_port_set_xqmac_xaui_en(uint8 lchip, uint8 slice_id, uint16 mac_id, bool enable)
{
    uint32 value    = 0;
    uint8  index    = 0;
    uint32 cmd      = 0;
    uint32 tbl_id   = 0;
    uint32 field_id = 0;
    uint16 step     = 0;
    uint16 temp_mac_id = mac_id;
    SharedPcsCfg0_m shared_pcs_cfg;
    XqmacCfg0_m xqmac_cfg;
    uint8 sub_idx = 0;
    XqmacInterruptNormal0_m xqmac_intr;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* 1. mac enable */
    if(enable)
    {
        /* cfg SharedPcsCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
        value = 1;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        DRV_IOW_FIELD(tbl_id, SharedPcsCfg0_xauiMode_f, &value, &shared_pcs_cfg);
        DRV_IOW_FIELD(tbl_id, SharedPcsCfg0_xlgMode_f, &value, &shared_pcs_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        value = 0;
        cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_sgmiiModeRx0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_sgmiiModeTx0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg XqmacCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
        value = 1;
        tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        DRV_IOW_FIELD(tbl_id, XqmacCfg0_xlgMode_f, &value, &xqmac_cfg);
        DRV_IOW_FIELD(tbl_id, XqmacCfg0_xauiMode_f, &value, &xqmac_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        /* cfg SharedPcsXfiCfg txLinkFaultDropEn_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            value = 1;
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            value = 0x0a;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        /*just need cfg first PcsCfg*/
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsDsfCfg cfgDsfDepth_f */
        tbl_id = SharedPcsDsfCfg0_t + (mac_id + slice_id * 48)/4;
        value = 7;
        cmd = DRV_IOW(tbl_id, SharedPcsDsfCfg0_cfgDsfDepth_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* RELEASE */
        /* cfg SharedPcsSoftRst softRstXlgTx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        value  = 0;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgTx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset resetCoreXlg0Mac_f */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreXlg0Mac_f + mac_id/4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst softRstXlgRx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*for 10g/1g port release xlg overrun interrupt in XqmacInterruptNormal */
        tbl_id = XqmacInterruptNormal0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &xqmac_intr));
        SetXqmacInterruptNormal0(V, xlgRxFifoOverrun_f, &xqmac_intr, 1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &xqmac_intr));
    }
    /* mac disable */
    else
    {
        /* cfg SharedPcsSoftRst softRstXlgRx_f */
        value = 1;
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset resetCoreXlg0Mac_f */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreXlg0Mac_f + mac_id/4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst softRstXlgTx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgTx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_port_set_xqmac_xfi_en(uint8 lchip, uint8 slice_id, uint16 mac_id, bool enable)
{
    uint32  value    = 0;
    uint8  index    = 0;
    uint32 cmd      = 0;
    uint32 tbl_id   = 0;
    uint32 field_id = 0;
    uint16 step     = 0;
    uint16 temp_mac_id = mac_id;
    XqmacSgmac1Cfg0_m sgmac_cfg;
    XqmacCfg0_m xqmac_cfg;
    SharedPcsCfg0_m shared_pcs_cfg;
    XqmacInterruptNormal0_m xqmac_intr;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* mac enable */
    if (enable)
    {
        /* cfg SharedPcsCfg */
        value = 0;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        field_id = SharedPcsCfg0_sgmiiModeRx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
        field_id = SharedPcsCfg0_sgmiiModeTx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
        DRV_IOW_FIELD(tbl_id, SharedPcsCfg0_xauiMode_f, &value, &shared_pcs_cfg);
        DRV_IOW_FIELD(tbl_id, SharedPcsCfg0_xlgMode_f, &value, &shared_pcs_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        /* cfg XqmacSgmacCfg */
        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeRx0_f, &value, &sgmac_cfg);
        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeTx0_f, &value, &sgmac_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        /* cfg XqmacCfg */
        tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        DRV_IOW_FIELD(tbl_id, XqmacCfg0_xlgMode_f, &value, &xqmac_cfg);
        DRV_IOW_FIELD(tbl_id, XqmacCfg0_xauiMode_f, &value, &xqmac_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        /* cfg SharedPcsXfiCfg, set mac/pcs misc cfg */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        value = 0x0a;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        /*not xlg mode shoud config 1*/
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* RELEASE */
        /* cfg RlmHsCtlReset or RlmCsCtlReset, relese pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg SharedPcsSoftRst, relese pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t +(mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset, relese mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, relese pcs rx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*for 10g/1g port mask xlg overrun interrupt in XqmacInterruptNormal */
        tbl_id = XqmacInterruptNormal0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));
        SetXqmacInterruptNormal0(V, xlgRxFifoOverrun_f, &xqmac_intr, 1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));
    }
    /* mac disable*/
    else
    {
        /* cfg SharedPcsSoftRst, set pcs rx soft reset */
        value = 1;
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset, set mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, set pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t +(mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmHsCtlReset, set pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
    }

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_port_set_xqmac_xlg_en(uint8 lchip, uint8 slice_id, uint16 mac_id, bool enable)
{

    uint8  index     = 0;
    uint32  value     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint32 field_id  = 0;
    uint16 step      = 0;
    uint8 sub_idx = 0;
    uint16 temp_mac_id = mac_id;
    XqmacCfg0_m xqmac_cfg;
    XqmacInterruptNormal0_m xqmac_intr;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* mac enable*/
    if (enable)
    {
        /* cfg SharedPcsCfg xlgMode_f */
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsCfg0_xlgMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsCfg xauiMode_f sgmiiModeRx_f sgmiiModeTx_f */
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        value = 0;
        cmd = DRV_IOW(tbl_id, SharedPcsCfg0_xauiMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        for(sub_idx = 0; sub_idx <4; sub_idx++)
        {
            cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_sgmiiModeRx0_f + sub_idx));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
            cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_sgmiiModeTx0_f + sub_idx));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg XqmacCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
        tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));
        SetXqmacCfg0(V, xlgMode_f, &xqmac_cfg, 1);
        SetXqmacCfg0(V, xauiMode_f, &xqmac_cfg, 0);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        /* cfg SharedPcsXfiCfg txLinkFaultDropEn_f, set mac/pcs misc cfg */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            value = 1;
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            value = 0x0a;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /*just need cfg first PcsCfg*/
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4;
        value = 2;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* RELEASE*/
        /* cfg RlmHsCtlReset RlmCsCtlReset0 */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 2;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 3;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 2;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 3;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg SharedPcsSoftRst softRstXlgTx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgTx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset resetCoreXlg0Mac_f */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreXlg0Mac_f + mac_id/4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst softRstXlgRx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*for 10g/1g port release xlg overrun interrupt in XqmacInterruptNormal */
        tbl_id = XqmacInterruptNormal0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &xqmac_intr));
        SetXqmacInterruptNormal0(V, xlgRxFifoOverrun_f, &xqmac_intr, 1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 3, cmd, &xqmac_intr));
    }
    else
    {
        /* cfg SharedPcsSoftRst softRstXlgRx_f */
        value = 1;
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*just need cfg first PcsCfg*/
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset resetCoreXlg0Mac_f */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreXlg0Mac_f + mac_id/4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst softRstXlgTx_f */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgTx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmHsCtlReset RlmCsCtlReset0 */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            value = 1;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 2;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id + 3;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            value = 1;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 2;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32) + 3;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_mac_mode(uint8 lchip, uint8 slice_id, uint16 mac_id, uint8 mode)
{

    uint8  index            = 0;
    uint32 value            = 0;
    uint32 cmd              = 0;
    uint32 tbl_id           = 0;
    uint32 field_id         = 0;
    uint8  step             = 0;
    XqmacSgmac1Cfg0_m sgmac_cfg;
    SharedPcsCfg0_m shared_pcs_cfg;
    XqmacCfg0_m xqmac_cfg;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, mode:%d\n", slice_id, mac_id, mode);

    /* cfg SharedPcsCfg sgmiiModeRx_f, sgmiiModeTx_f. And cfg sgmiiRepeatCnt_f for 10M/100M/1G */
    value = 1;
    tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

    value = (mode == CTC_CHIP_SERDES_SGMII_MODE)?1:0;
    field_id = SharedPcsCfg0_sgmiiModeRx0_f + mac_id%4;
    DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
    field_id = SharedPcsCfg0_sgmiiModeTx0_f + mac_id%4;
    DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
    value = ((mode == CTC_CHIP_SERDES_XAUI_MODE) || (mode == CTC_CHIP_SERDES_DXAUI_MODE))?1:0;
    SetSharedPcsCfg0(V, xauiMode_f, &shared_pcs_cfg, value);
    value = ((mode == CTC_CHIP_SERDES_XLG_MODE) || (mode == CTC_CHIP_SERDES_XAUI_MODE) || (mode == CTC_CHIP_SERDES_DXAUI_MODE))?1:0;
    SetSharedPcsCfg0(V, xlgMode_f, &shared_pcs_cfg, value);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

    /* cfg XqmacSgmacCfg sgmiiModeRx_f, sgmiiModeTx_f. And cfg sgmiiSampleCnt_f, sgmiiSampleCnt_f for 10M/100M/1G */
    value = 1;
    step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
    tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));
    value = (mode == CTC_CHIP_SERDES_SGMII_MODE)?1:0;
    DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeRx0_f, &value, &sgmac_cfg);
    DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeTx0_f, &value, &sgmac_cfg);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

    /* cfg XqmacCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
    tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));
    value = ((mode == CTC_CHIP_SERDES_XLG_MODE) || (mode == CTC_CHIP_SERDES_XAUI_MODE) || (mode == CTC_CHIP_SERDES_DXAUI_MODE))?1:0;
    SetXqmacCfg0(V, xlgMode_f, &xqmac_cfg, value);
    value = ((mode == CTC_CHIP_SERDES_XAUI_MODE) || (mode == CTC_CHIP_SERDES_DXAUI_MODE))?1:0;
    SetXqmacCfg0(V, xauiMode_f, &xqmac_cfg, value);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_full_thrd(uint8 lchip, uint8 slice_id, uint16 mac_id, uint8 mode)
{

    uint8  index            = 0;
    uint32 value            = 0;
    uint32 cmd              = 0;
    uint32 tbl_id           = 0;
    uint8  step             = 0;
    uint8 sub_idx           = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, mode:%d\n", slice_id, mac_id, mode);


    if (CTC_CHIP_SERDES_SGMII_MODE == mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsXfiCfg txAsyncFifoFullThrd_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg011_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
        value = 1;
        tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOW(tbl_id, SharedPcsSgmiiCfg00_ignoreLinkFailure0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }
    else if (CTC_CHIP_SERDES_2DOT5G_MODE == mode)
    {
        /* cfg SharedPcsXfiCfg txAsyncFifoFullThrd_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg011_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }
    else if (CTC_CHIP_SERDES_XLG_MODE == mode)
    {
        /* cfg SharedPcsXfiCfg txLinkFaultDropEn_f, set mac/pcs misc cfg */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            value = 1;
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            value = 0x0a;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /*just need cfg first PcsCfg*/
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4;
        value = 2;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

    }
    else if (CTC_CHIP_SERDES_XFI_MODE == mode)
    {
        /* cfg SharedPcsXfiCfg, set mac/pcs misc cfg */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        value = 0x0a;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }
    else if ((CTC_CHIP_SERDES_XAUI_MODE == mode) || (CTC_CHIP_SERDES_DXAUI_MODE == mode))
    {
        /* cfg SharedPcsXfiCfg txLinkFaultDropEn_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            value = 1;
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            value = 0x0a;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txAsyncFifoFullThrd0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg SharedPcsDsfCfg cfgDsfDepth_f */
        tbl_id = SharedPcsDsfCfg0_t + (mac_id + slice_id * 48)/4;
        value = 7;
        cmd = DRV_IOW(tbl_id, SharedPcsDsfCfg0_cfgDsfDepth_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_xqmac_sgmii_en(uint8 lchip, uint8 slice_id, uint16 mac_id, uint8 speed_mode, bool enable)
{

    uint8  index            = 0;
    uint32 value            = 0;
    uint32 cmd              = 0;
    uint32 tbl_id           = 0;
    uint32 field_id         = 0;
    uint8  step             = 0;
    uint16 temp_mac_id = mac_id;
    XqmacSgmac1Cfg0_m sgmac_cfg;
    SharedPcsCfg0_m shared_pcs_cfg;
    XqmacCfg0_m xqmac_cfg;
    XqmacInterruptNormal0_m xqmac_intr;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* 1. mac enable*/
    if(enable)
    {
        /* cfg SharedPcsCfg sgmiiModeRx_f, sgmiiModeTx_f. And cfg sgmiiRepeatCnt_f for 10M/100M/1G */
        value = 1;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        field_id = SharedPcsCfg0_sgmiiModeRx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
        field_id = SharedPcsCfg0_sgmiiModeTx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);

        value = (speed_mode == CTC_PORT_SPEED_10M)?99:((speed_mode == CTC_PORT_SPEED_100M)?9:0);
        field_id = SharedPcsCfg0_sgmiiRepeatCnt0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);

        SetSharedPcsCfg0(V, xauiMode_f, &shared_pcs_cfg, 0);
        SetSharedPcsCfg0(V, xlgMode_f, &shared_pcs_cfg, 0);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        /* cfg XqmacSgmacCfg sgmiiModeRx_f, sgmiiModeTx_f. And cfg sgmiiSampleCnt_f, sgmiiSampleCnt_f for 10M/100M/1G */
        value = 1;
        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeRx0_f, &value, &sgmac_cfg);
        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeTx0_f, &value, &sgmac_cfg);

        value = (speed_mode == CTC_PORT_SPEED_10M)?99:((speed_mode == CTC_PORT_SPEED_100M)?9:0);
        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiRepeatCnt0_f, &value, &sgmac_cfg);
        value = (speed_mode == CTC_PORT_SPEED_10M)?49:((speed_mode == CTC_PORT_SPEED_100M)?4:0);
        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiSampleCnt0_f, &value, &sgmac_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        /* cfg XqmacCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
        tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));
        SetXqmacCfg0(V, xlgMode_f, &xqmac_cfg, 0);
        SetXqmacCfg0(V, xauiMode_f, &xqmac_cfg, 0);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txLinkFaultDropEn0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        /*not xlg mode shoud config 1*/
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsXfiCfg txAsyncFifoFullThrd_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg011_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
        value = 1;
        tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOW(tbl_id, SharedPcsSgmiiCfg00_ignoreLinkFailure0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* RELEASE */
        /* cfg RlmHsCtlReset or RlmCsCtlReset, relese pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg SharedPcsSoftRst, relese pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset, relese mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, relese pcs rx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*for 10g/1g port mask xlg overrun interrupt in XqmacInterruptNormal */
        tbl_id = XqmacInterruptNormal0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));
        SetXqmacInterruptNormal0(V, xlgRxFifoOverrun_f, &xqmac_intr, 1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));

        /* when auto-neg disable, need enable unidirectionen*/
        step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
        tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOR(tbl_id, SharedPcsSgmiiCfg00_anEnable0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        value = value?0:1;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_unidirectionEn0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    /* 2. mac disable*/
    else
    {
        /* cfg SharedPcsSoftRst, set pcs rx soft reset */
        value = 1;
        tbl_id = SharedPcsSoftRst0_t +(mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset, set mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, set pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmHsCtlReset or RlmCsCtlReset, set pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* mac disable, need disable unidirectionen*/
        value = 0;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_unidirectionEn0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_xqmac_2dot5_en(uint8 lchip, uint8 slice_id, uint16 mac_id, bool enable)
{

    uint8  index            = 0;
    uint32 value            = 0;
    uint8  step             = 0;
    uint32 cmd              = 0;
    uint32 tbl_id           = 0;
    uint32 field_id         = 0;
    uint16 temp_mac_id = mac_id;
    XqmacSgmac1Cfg0_m sgmac_cfg;
    SharedPcsCfg0_m shared_pcs_cfg;
    XqmacCfg0_m xqmac_cfg;
    XqmacInterruptNormal0_m xqmac_intr;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* 1. mac enable*/
    if(enable)
    {
        /* cfg SharedPcsCfg sgmiiModeRx_f, sgmiiModeTx_f. */
        sal_memset(&shared_pcs_cfg, 0, sizeof(SharedPcsCfg0_m));
        value = 1;
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        field_id = SharedPcsCfg0_sgmiiModeRx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);
        field_id = SharedPcsCfg0_sgmiiModeTx0_f + mac_id%4;
        DRV_IOW_FIELD(tbl_id, field_id, &value, &shared_pcs_cfg);

        SetSharedPcsCfg0(V, xauiMode_f, &shared_pcs_cfg, 0);
        SetSharedPcsCfg0(V, xlgMode_f, &shared_pcs_cfg, 0);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &shared_pcs_cfg));

        /* cfg XqmacSgmacCfg sgmiiModeRx_f, sgmiiModeTx_f. */
        sal_memset(&sgmac_cfg, 0, sizeof(XqmacSgmac1Cfg0_m));
        value = 1;
        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeRx0_f, &value, &sgmac_cfg);
        DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_sgmiiModeTx0_f, &value, &sgmac_cfg);

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &sgmac_cfg));

        /* cfg XqmacCfg xauiMode_f, xlgMode_f, set mac/pcs xaui mode */
        tbl_id = XqmacCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));
        SetXqmacCfg0(V, xlgMode_f, &xqmac_cfg, 0);
        SetXqmacCfg0(V, xauiMode_f, &xqmac_cfg, 0);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &xqmac_cfg));

        /* cfg SharedPcsXfiCfg txAsyncFifoFullThrd_f */
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg011_txAsyncFifoFullThrd0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        /*not xlg mode shoud config 1*/
        value = 1;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_txThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* RELEASE */
        /* cfg RlmHsCtlReset or RlmCsCtlReset, relese pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            value = 0;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        /* cfg SharedPcsSoftRst, relese pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset, relese mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, relese pcs rx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /*for 10g/1g port mask xlg overrun interrupt in XqmacInterruptNormal */
        tbl_id = XqmacInterruptNormal0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));
        SetXqmacInterruptNormal0(V, xlgRxFifoOverrun_f, &xqmac_intr, 1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 2, cmd, &xqmac_intr));
    }
    /* 2. mac disable*/
    else
    {
        /* cfg SharedPcsSoftRst, set pcs rx soft reset */
        value = 1;
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset, set mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg SharedPcsSoftRst, set pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmHsCtlReset or RlmCsCtlReset, set pcs core reset */
        if (mac_id < 32)
        {
            tbl_id = RlmHsCtlReset0_t + slice_id;
            field_id = RlmHsCtlReset0_resetCorePcs0_f + mac_id;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else
        {
            tbl_id = RlmCsCtlReset0_t + slice_id;
            field_id = RlmCsCtlReset0_resetCorePcs32_f + (mac_id-32);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_cgmac_en(uint8 lchip, uint8 slice_id, uint16 mac_id, bool enable)
{

    uint8  index     = 0;
    uint32 value     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint32 field_id  = 0;
    uint16 temp_mac_id = mac_id;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"slice_id:%d, mac_id:%d, enable:%d\n", slice_id, mac_id, enable);

    if(enable)
    {
        /* cfg RlmNetCtlReset resetCoreCgmacReg_f, release reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreCgmac0Reg_f + ((mac_id==36)?0:2);
        value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmCsCtlReset resetCoreCgPcsReg_f, release reset */
        tbl_id = RlmCsCtlReset0_t + slice_id;
        field_id = RlmCsCtlReset0_resetCoreCgPcs0Reg_f + ((mac_id==36)?0:2);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset resetCoreCxqm_f, release reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreCxqm0_f + ((mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg CgPcsSoftRst softRstPcsTx_f, release pcs tx soft reset */
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsTx_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmCsCtlReset resetCoreCgPcs_f, release mac/pcs reset */
        tbl_id = RlmCsCtlReset0_t + slice_id;
        field_id = RlmCsCtlReset0_resetCoreCgPcs0_f + ((mac_id==36)?0:2);
        value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        CTC_ERROR_RETURN(sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, slice_id, temp_mac_id));

        /* cfg RlmNetCtlReset resetCoreCgmac_f, release mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreCgmac0_f + ((mac_id==36)?0:2);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg CgPcsSoftRst softRstPcsRx_f, release pcs rx soft reset */
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsRx_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }
    else
    {
        /* cfg RlmCsCtlReset resetCoreCgPcs_f, release mac/pcs reset */
        tbl_id = RlmCsCtlReset0_t + slice_id;
        field_id = RlmCsCtlReset0_resetCoreCgPcs0_f + ((mac_id==36)?0:2);
        value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg CgPcsSoftRst softRstPcsRx_f, reset pcs rx softrst */
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsRx_f;
        value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg RlmNetCtlReset resetCoreCgmac_f, release mac core reset */
        tbl_id = RlmNetCtlReset0_t + slice_id;
        field_id = RlmNetCtlReset0_resetCoreCgmac0_f + ((mac_id==36)?0:2);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

        /* cfg CgPcsSoftRst softRstPcsTx_f, release pcs tx soft reset */
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsTx_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_pause_speed_mode(uint8 lchip, uint8 slice_id, uint8 mac_id, ctc_port_speed_t speed_mode)
{
    uint32 cmd  = 0;
    uint32 tb_id = 0;
    NetRxPauseTimerMem0_m rx_pause;
    uint32 mode = 0;

    tb_id = NetRxPauseTimerMem0_t + slice_id;

    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &rx_pause));

    switch (speed_mode)
    {
        case CTC_PORT_SPEED_10M:
        case CTC_PORT_SPEED_100M:
        case CTC_PORT_SPEED_1G:
            mode = 0x3;
            break;
        case CTC_PORT_SPEED_2G5:
            mode = 0x4;
            break;
        case CTC_PORT_SPEED_10G:
            mode = 0x0;
            break;
        case CTC_PORT_SPEED_20G:
        case CTC_PORT_SPEED_40G:
            mode = 0x1;
            break;
        case CTC_PORT_SPEED_100G:
            mode = 0x2;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    SetNetRxPauseTimerMem0(V, speedMode_f, &rx_pause, mode);

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &rx_pause));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_fec_en(uint8 lchip, uint16 gport, bool enable)
{
    uint16 lport     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint32 auto_neg_en = 0;
    CgPcsCfg0_m  pcscfg;
    CgPcsFecCtl0_m fecctl;
    sys_datapath_lport_attr_t* port_attr = NULL;

    ctc_chip_device_info_t dev_info;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:0x%04X, enable:%d\n", gport, enable);

    /* check, GG chip version 1 not support FEC */
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id <= 1)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* check init */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    /* get info from gport */
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if ((36 != port_attr->mac_id) && (52 != port_attr->mac_id))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* do fec cfg */
    if((CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode))
    {
        tbl_id = CgPcsCfg0_t + (port_attr->slice_id*2) + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pcscfg));

        tbl_id = CgPcsFecCtl0_t + (port_attr->slice_id*2) + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fecctl));

        if(enable)
        {
            SetCgPcsCfg0(V, rxAmInterval_f, &pcscfg, 81919);
            SetCgPcsCfg0(V, cgfecEn_f, &pcscfg, 1);

            SetCgPcsFecCtl0(V, corrCwCntRst_f, &fecctl, 0);
            SetCgPcsFecCtl0(V, symErrCntRst_f, &fecctl, 0);
            SetCgPcsFecCtl0(V, uncoCwCntRst_f, &fecctl, 0);
        }
        else
        {
            SetCgPcsCfg0(V, cgfecEn_f, &pcscfg, 0);

            SetCgPcsFecCtl0(V, corrCwCntRst_f, &fecctl, 1);
            SetCgPcsFecCtl0(V, symErrCntRst_f, &fecctl, 0xf);
            SetCgPcsFecCtl0(V, uncoCwCntRst_f, &fecctl, 1);
        }

        tbl_id = CgPcsCfg0_t + (port_attr->slice_id*2) + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pcscfg));

        tbl_id = CgPcsFecCtl0_t + (port_attr->slice_id*2) + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fecctl));

        sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en);
        if (enable && (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode) && (0 == auto_neg_en))
        {
            sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, port_attr->slice_id, port_attr->mac_id);
            sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_EN, 0);
            sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_INIT, 0);
            port_attr->pcs_reset_en = 1;
        }
        else if (1 == port_attr->pcs_reset_en)
        {
            sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DIS, 0);
            port_attr->pcs_reset_en = 0;
        }

    }
    else
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_port_get_fec_en(uint8 lchip, uint16 gport, uint32* p_enable)
{
    uint16 lport     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    CgPcsCfg0_m  pcscfg;
    sys_datapath_lport_attr_t* port_attr = NULL;

    ctc_chip_device_info_t dev_info;

    /* check, GG chip version 1 not support FEC */
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id <= 1)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    CTC_PTR_VALID_CHECK(p_enable);
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if ((36 != port_attr->mac_id) && (52 != port_attr->mac_id))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* do fec cfg */
    if((CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode))
    {
        tbl_id = CgPcsCfg0_t + (port_attr->slice_id)*2 + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pcscfg));

        (*p_enable) = GetCgPcsCfg0(V, cgfecEn_f, &pcscfg);
    }
    else
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_eee_en(uint8 lchip, uint16 gport, bool enable)
{
    uint16 lport     = 0;
    uint16 tbl_id    = 0;
    uint8  slice_id  = 0;
    uint8  step      = 0;
    uint8  mac_id    = 0;
    uint32 value     = 0;
    uint32 cmd       = 0;
    uint8  index     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "gport: %d, enable: %d\n", gport, enable);
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    slice_id = port_attr->slice_id;

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    mac_id  = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    /* cfg SharedPcsEeeCtl */
    step = SharedPcsEeeCtl10_t - SharedPcsEeeCtl00_t;
    tbl_id = SharedPcsEeeCtl00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
    value = 0;
    cmd = DRV_IOW(tbl_id, SharedPcsEeeCtl00_rxEeeEn0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    cmd = DRV_IOW(tbl_id, SharedPcsEeeCtl00_txEeeEn0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

    /* cfg SharedPcsSgmiiCfg localEeeAbility0_f */
    step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
    tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+(port_attr->slice_id)*48) / 4 + ((mac_id+slice_id*48)%4)*step;
    value = enable?1:0;
    cmd = DRV_IOW(tbl_id, SharedPcsSgmiiCfg00_localEeeAbility0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

    /* cfg NetTxEEECfg, NOTE: bitmap for mac_id */
    tbl_id = NetTxEEECfg0_t+slice_id;
    mac_id = port_attr->mac_id;
    if(mac_id < 32)
    {
        cmd = DRV_IOR(tbl_id, NetTxEEECfg0_eeeCtlEn31To0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        if(enable)
        {
            value = value | (1<<mac_id);
        }
        else
        {
            value = value & (~(1<<mac_id));
        }
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac_id: %d, value: 0x%X\n", mac_id, value);
        cmd = DRV_IOW(tbl_id, NetTxEEECfg0_eeeCtlEn31To0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }
    else
    {
        cmd = DRV_IOR(tbl_id, NetTxEEECfg0_eeeCtlEn63To32_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        if(enable)
        {
            value = value | (1<<(mac_id-32));
        }
        else
        {
            value = value & (~(1<<(mac_id-32)));
        }
        cmd = DRV_IOW(tbl_id, NetTxEEECfg0_eeeCtlEn63To32_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_port_set_mac_en(uint8 lchip, uint16 gport, bool enable)
{
    uint16 lport     = 0;

    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    drv_work_platform_type_t platform_type;
    uint16 serdes_id = 0;
    uint8 num = 0;
    uint8 index = 0;
    ctc_chip_serdes_prbs_t prbs;
    uint32 fec_en = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&prbs, 0, sizeof(ctc_chip_serdes_prbs_t));

    drv_goldengate_get_platform_type(&platform_type);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"lport:%d, port_type:%d, mac_id:%d, pcs_mode:%d, serdes_id:%d, need_ext:%d\n",
        lport, port_attr->port_type, port_attr->mac_id, port_attr->pcs_mode, port_attr->serdes_id, port_attr->need_ext);

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        /* if mac already is enabled, no need to do enable again */
        if (p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en && enable)
        {
            return CTC_E_NONE;
        }

        slice_id = port_attr->slice_id;
        chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;

        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &num));
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"chip_port:%d, serdes_id:%d, num:%d\n", chip_port, serdes_id, num);

        /* if mac disable, need disable serdes tx and before enable serdes tx, need enable prbs15+ for sgmii*/
        if(!enable)
        {
           for (index = 0; index < num; index++)
           {
               if (CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
               {
                   prbs.mode = 1;
                   prbs.serdes_id = serdes_id+index;
                   prbs.polynome_type = CTC_CHIP_SERDES_PRBS15_SUB;
                   prbs.value = 1;
                   CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_prbs_tx(lchip, &prbs));
                   /* at less 10ms before close serdes tx*/
                   sal_task_sleep(35);
               }
               CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_tx_en(lchip, serdes_id+index, FALSE));
           }
        }

        switch (port_attr->pcs_mode)
        {
            case CTC_CHIP_SERDES_SGMII_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_sgmii_en(lchip, port_attr->slice_id, port_attr->mac_id, port_attr->speed_mode, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_2DOT5G_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_2dot5_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_XAUI_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_xaui_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_DXAUI_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_xaui_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_XFI_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_xfi_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_XLG_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_xlg_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            case CTC_CHIP_SERDES_CG_MODE:
                CTC_ERROR_RETURN(_sys_goldengate_port_set_cgmac_en(lchip, port_attr->slice_id, port_attr->mac_id, enable));
                 _sys_goldengate_port_get_fec_en(lchip, gport, &fec_en);
                if (!enable && fec_en && (0 == p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status))
                {
                    sys_goldengate_datapath_serdes_tx_en_with_mac(lchip, port_attr->slice_id, port_attr->mac_id);
                    sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_EN, 0);
                    sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_INIT, 0);
                    port_attr->pcs_reset_en = 1;
                }
                p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((enable)?TRUE:FALSE);
                break;

            default:
                break;
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_port_set_pause_speed_mode(lchip, port_attr->slice_id, port_attr->mac_id,
        port_attr->speed_mode));

    /*temp for simulation link up/down*/
    if (platform_type == SW_SIM_PLATFORM)
    {
        uint32 cmd = 0;
        uint32 value = 0;
        uint16 index = 0;

        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);
        index = ((lport >> 8) << 6) + (lport & 0x3F);

        value = !enable;        /* 0 means link up*/
        cmd = DRV_IOW(QMgrEnqLinkState_t, QMgrEnqLinkState_linkState_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        cmd = DRV_IOW(DlbChanStateCtl_t, DlbChanStateCtl_linkState_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
    }

    /*set cut through*/
    _sys_goldengate_port_set_cut_through_en(lchip, gport,1);

    if(!enable)
    {
        /* if mac disable, after reset, need disable prbs15+ for sgmii */
        for (index = 0; index < num; index++)
        {
            if (CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
            {
                prbs.mode = 1;
                prbs.serdes_id = serdes_id+index;
                prbs.polynome_type = CTC_CHIP_SERDES_PRBS15_SUB;
                prbs.value = 0;
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_prbs_tx(lchip, &prbs));
            }
        }
        port_attr->reset_app = 0;
        port_attr->switch_num = 0;
    }

    CTC_ERROR_RETURN(sys_goldengate_qos_set_fc_default_profile(lchip, gport));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_mac_en(uint8 lchip, uint16 gport, uint32* p_enable)
{
    uint16  lport   = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    CTC_PTR_VALID_CHECK(p_enable);

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    *p_enable = p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en ? TRUE : FALSE;

    return CTC_E_NONE;
}

/*set speed fun olny support SGMII mode, 10M 100M 1G change*/
STATIC int32
_sys_goldengate_port_set_speed(uint8 lchip, uint16 gport, ctc_port_speed_t speed_mode)
{
    uint16 lport      = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8  serdes_num = 0;
    uint8  cnt = 0;
    uint8 mode = 0;
    uint32 enable = 0;
    uint16 drv_port = 0;
    uint8 chan_id = 0;
    uint8 gchip_id = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if ((port_attr->pcs_mode == CTC_CHIP_SERDES_SGMII_MODE) && (speed_mode == CTC_PORT_SPEED_1G || speed_mode == CTC_PORT_SPEED_100M || speed_mode == CTC_PORT_SPEED_10M))
    {
        /* if mac enable: first mac disable, then mac enable again */
        if (p_gg_port_master[lchip]->igs_port_prop[lport].port_mac_en)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_sgmii_en(lchip, port_attr->slice_id, port_attr->mac_id, 0, FALSE));
            CTC_ERROR_RETURN(_sys_goldengate_port_set_xqmac_sgmii_en(lchip, port_attr->slice_id, port_attr->mac_id, speed_mode, TRUE));
            port_attr->speed_mode = speed_mode;
        }
        else
        {
            port_attr->speed_mode = speed_mode;
        }
    }
    else
    {
        sys_goldengate_port_get_mac_en(lchip, gport, &enable);
        if (enable)
        {
            return CTC_E_DATAPATH_MAC_ENABLE;
        }
        port_attr->pcs_init_mode = port_attr->pcs_mode;
        port_attr->switch_num = 0;
        port_attr->reset_app = 0;
        if (speed_mode == CTC_PORT_SPEED_100G)
        {
            mode = CTC_CHIP_SERDES_CG_MODE;
        }
        else if (speed_mode == CTC_PORT_SPEED_40G)
        {
            mode = CTC_CHIP_SERDES_XLG_MODE;
        }
        else if (speed_mode == CTC_PORT_SPEED_10G)
        {
            /* xaui mode must be chip serdes api to set*/
            mode = CTC_CHIP_SERDES_XFI_MODE;
        }
        else if (speed_mode == CTC_PORT_SPEED_1G)
        {
            mode = CTC_CHIP_SERDES_SGMII_MODE;
        }
        else
        {
            return CTC_E_INVALID_PORT_SPEED_MODE;
        }

        slice_id = port_attr->slice_id;
        chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));
        for(cnt = 0; cnt < serdes_num; cnt++)
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_mode(lchip, serdes_id+cnt, mode, 0));

            if (CTC_CHIP_SERDES_NONE_MODE != mode)
            {
                CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, serdes_id+cnt, &p_serdes));

                slice_id = (serdes_id+cnt >= SERDES_NUM_PER_SLICE) ? 1 : 0;
                drv_port = p_serdes->lport + SYS_CHIP_PER_SLICE_PORT_NUM * slice_id;
                CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
                gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, drv_port);
                chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
                CTC_ERROR_RETURN(sys_goldengate_chip_set_dlb_chan_type(lchip, chan_id));
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_speed(uint8 lchip, uint16 gport, ctc_port_speed_t* p_speed_mode)
{
    uint16 lport      = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    *p_speed_mode = port_attr->speed_mode;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_3ap_training_en(uint8 lchip, uint16 gport, uint32* training_state)
{
    uint16 lport   = 0;

    /* 1.get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    *training_state = p_gg_port_master[lchip]->igs_port_prop[lport].training_status;

    return CTC_E_NONE;
}

/* support 802.3ap, Auto-Negotiation for Backplane Ethernet */
int32
sys_goldengate_port_set_3ap_training_en(uint8 lchip, uint16 gport, uint32 cfg_flag, uint32 is_normal)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    uint32 train_ok;

    bool  enable;
    uint8 cnt = 0;
    uint16 mac_id;
    ctc_chip_device_info_t dev_info;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, cfg_flag:%d\n", gport, cfg_flag);

    /* 1.get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    mac_id = port_attr->mac_id;
    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* 2.cfg 802.3ap by cfg_flag */
    switch(cfg_flag)
    {
        case SYS_PORT_3AP_TRAINING_DIS:
            enable = FALSE;
            if(SYS_PORT_3AP_TRAINING_EN == p_gg_port_master[lchip]->igs_port_prop[lport].training_status)
            {
                for(cnt=0; cnt<serdes_num; cnt++)
                {
                    CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_aet_en(lchip, (serdes_id+cnt), SYS_PORT_3AP_TRAINING_EN, enable, is_normal));
                }
            }
            if((SYS_PORT_3AP_TRAINING_INIT == p_gg_port_master[lchip]->igs_port_prop[lport].training_status)||
                (SYS_PORT_3AP_TRAINING_DONE == p_gg_port_master[lchip]->igs_port_prop[lport].training_status))
            {
                for(cnt=0; cnt<serdes_num; cnt++)
                {
                    CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_aet_en(lchip, (serdes_id+cnt), SYS_PORT_3AP_TRAINING_EN, enable, is_normal));
                    CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_aet_en(lchip, (serdes_id+cnt), SYS_PORT_3AP_TRAINING_INIT, enable, is_normal));
                }
            }
            p_gg_port_master[lchip]->igs_port_prop[lport].training_status = SYS_PORT_3AP_TRAINING_DIS;
            break;

        case SYS_PORT_3AP_TRAINING_EN:
            enable = TRUE;
            for(cnt=0; cnt<serdes_num; cnt++)
            {
                ctc_chip_serdes_ffe_t ffe;
                sal_memset(&ffe, 0, sizeof(ctc_chip_serdes_ffe_t));
                ffe.mode = CTC_CHIP_SERDES_FFE_MODE_3AP;
                ffe.serdes_id = serdes_id+cnt;
                if(SYS_DATAPATH_SERDES_IS_HSS28G(ffe.serdes_id))
                {
                    ffe.coefficient[0] = 0x0;
                    ffe.coefficient[1] = 0x0;
                    ffe.coefficient[2] = 0x2b;
                    ffe.coefficient[3] = 0x0;
                }
                else
                {
                    ffe.coefficient[0] = 0x0;
                    ffe.coefficient[1] = 0x2b;
                    ffe.coefficient[2] = 0x0;
                    ffe.coefficient[3] = 0x0;
                }
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_ffe(lchip, &ffe));
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_aet_en(lchip, (serdes_id+cnt), SYS_PORT_3AP_TRAINING_EN, enable, is_normal));
            }
            p_gg_port_master[lchip]->igs_port_prop[lport].training_status = SYS_PORT_3AP_TRAINING_EN;

            break;

        case SYS_PORT_3AP_TRAINING_INIT:
            if(p_gg_port_master[lchip]->igs_port_prop[lport].training_status == SYS_PORT_3AP_TRAINING_EN)
            {
                enable = TRUE;
                for(cnt=0; cnt < serdes_num; cnt++)
                {
                    CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_aet_en(lchip, (serdes_id+cnt), SYS_PORT_3AP_TRAINING_INIT, enable, is_normal));
                }
            }
            else
            {
                return CTC_E_INVALID_EXP_VALUE;
            }
            p_gg_port_master[lchip]->igs_port_prop[lport].training_status = SYS_PORT_3AP_TRAINING_INIT;
            break;

        case SYS_PORT_3AP_TRAINING_DONE:
            sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
            sys_goldengate_chip_get_device_info(lchip, &dev_info);
            if(p_gg_port_master[lchip]->igs_port_prop[lport].training_status == SYS_PORT_3AP_TRAINING_INIT)
            {
                if (dev_info.version_id <= 1)
                {
                    _sys_goldengate_port_set_pcs_reset(lchip, port_attr, 1, 1);
                }
                if(1 == serdes_num)
                {
                    train_ok = sys_goldengate_datapath_set_serdes_aec_aet_done(lchip, serdes_id);
                }
                else /* seedes_num == 4 */
                {
                    if((1 == sys_goldengate_datapath_set_serdes_aec_aet_done(lchip, serdes_id))
                        &&(1 == sys_goldengate_datapath_set_serdes_aec_aet_done(lchip, serdes_id + 1))
                        &&(1 == sys_goldengate_datapath_set_serdes_aec_aet_done(lchip, serdes_id + 2))
                        &&(1 == sys_goldengate_datapath_set_serdes_aec_aet_done(lchip, serdes_id + 3)))
                    {
                        train_ok = 1;
                    }
                    else
                    {
                        train_ok = 0;
                    }
                }
                if (dev_info.version_id <= 1)
                {
                    _sys_goldengate_port_set_pcs_reset(lchip, port_attr, 1, 0);
                }
            }
            else
            {
                return CTC_E_INVALID_EXP_VALUE;
            }

            sys_goldengate_queue_set_port_drop_en(lchip, gport, FALSE);
            if(train_ok)
            {
                /* updata sw */
                p_gg_port_master[lchip]->igs_port_prop[lport].training_status = SYS_PORT_3AP_TRAINING_DONE;
            }
            else
            {
                SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"Training failed! gport:%d\n", gport);
                return CTC_E_TRAININT_FAIL;
            }
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}

/*dir: 0-rx, 1-tx*/
STATIC int32
_sys_goldengate_port_set_pcs_reset(uint8 lchip, sys_datapath_lport_attr_t* port_attr, uint8 dir, bool enable)
{
    uint8  mac_id = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 value = 0;
    uint32 cmd   = 0;
    uint8  slice_id  = 0;
    SharedPcsSoftRst0_m pcs_rst;
    CgPcsSoftRst0_m cg_rst;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"enable:%d, mac_id:%d, slice_id:%d:\n", enable, port_attr->mac_id, port_attr->slice_id);

    slice_id = port_attr->slice_id;
    mac_id   = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /* reset value=1, relese value=0 */
    value = (enable)?1:0;

    switch (port_attr->pcs_mode)
    {
        case CTC_CHIP_SERDES_SGMII_MODE:
        case CTC_CHIP_SERDES_2DOT5G_MODE:
        case CTC_CHIP_SERDES_XFI_MODE:
            tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
            if (dir)
            {
                field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
            }
            else
            {
                field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
            }

            cmd = DRV_IOW(tbl_id, field_id);
            DRV_IOCTL(lchip, 0, cmd, &value);
        break;

        case CTC_CHIP_SERDES_XAUI_MODE:
        case CTC_CHIP_SERDES_DXAUI_MODE:
        case CTC_CHIP_SERDES_XLG_MODE:
            tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcs_rst);
            if (dir)
            {
                SetSharedPcsSoftRst0(V,softRstXlgTx_f, &pcs_rst, value);
            }
            else
            {
                SetSharedPcsSoftRst0(V,softRstXlgRx_f, &pcs_rst, value);
            }
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &pcs_rst);
        break;

        case CTC_CHIP_SERDES_CG_MODE:
            tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_rst);
            if (dir)
            {
                SetCgPcsSoftRst0(V,softRstPcsTx_f, &cg_rst, value);
            }
            else
            {
                SetCgPcsSoftRst0(V,softRstPcsRx_f, &cg_rst, value);
            }
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            DRV_IOCTL(lchip, 0, cmd, &cg_rst);

            /* cfg RlmCsCtlReset resetCoreCgPcs_f, release mac/pcs reset */
            tbl_id = RlmCsCtlReset0_t + slice_id;
            field_id = RlmCsCtlReset0_resetCoreCgPcs0_f + ((mac_id==36)?0:2);
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}

/* value: 3 is 12Bytes, 2 is 8Bytes, 1 is 4Bytes */
int32
sys_goldengate_port_mac_set_ipg(uint8 lchip, uint16 gport, uint32 value)
{
    uint16 lport     = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 mac_id = 0;
    uint16 step   = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, Ipg:%d\n", gport, value);

    if ((4 != value) && (8 != value) && (12 != value))
    {
        return CTC_E_INVALID_PARAM;
    }

    /* get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    mac_id = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    slice_id = port_attr->slice_id;
    value = value>>2;
    /* only support xfi 2.5G and sgmii */
    if((CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)||(CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ipgCfg0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_mac_get_ipg(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport     = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 mac_id = 0;
    uint16 step   = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 field = 0;

    /* get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    mac_id = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    slice_id = port_attr->slice_id;

    /* only support xfi 2.5G and sgmii */
    if((CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)||(CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;

        cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ipgCfg0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
        *p_value = field<<2;
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_oversub(uint8 lchip, uint16 gport, uint32 value)
{
    uint16 lport = 0;
    uint8 mac_id = 0;
    uint8 lmac_id = 0;
    uint8 slice_id = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint32 field_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, oversub:%d\n", gport, value);

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_MAX_VALUE_CHECK(value, 1);

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    mac_id  = port_attr->mac_id;
    lmac_id =  mac_id&0x3F;

    tbl_id = NetRxPortPriCtl0_t + slice_id;
    field_id = NetRxPortPriCtl0_cfgPortHighPriEn0_f + lmac_id/16;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    (value == 1) ? CTC_BIT_SET(field_val, lmac_id%16) : CTC_BIT_UNSET(field_val, lmac_id%16);

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_oversub(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport = 0;
    uint8 mac_id = 0;
    uint8 lmac_id = 0;
    uint8 slice_id = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint32 field_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    mac_id  = port_attr->mac_id;
    lmac_id =  mac_id&0x3F;

    tbl_id = NetRxPortPriCtl0_t + slice_id;
    field_id = NetRxPortPriCtl0_cfgPortHighPriEn0_f + lmac_id/16;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_value = CTC_IS_BIT_SET(field_val, lmac_id%16);

   return CTC_E_NONE;
}


/*Only user for 100g Auto neg, enable: 1-switch to 40g, 0-back to 100G*/
STATIC int32
_sys_goldengate_port_cl73_dynamic_switch(uint8 lchip, uint16 lport, uint8 enable)
{
    int32 ret = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8  serdes_num = 0;
    uint8  cnt = 0;

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    if(enable)
    {
        if(CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
        {
        for(cnt = 0; cnt < serdes_num; cnt++)
        {
                CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_mode(lchip, serdes_id+cnt, CTC_CHIP_SERDES_XLG_MODE, 0));
            }
        }
    }
    else
    {
        for(cnt = 0; cnt < serdes_num; cnt++)
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_mode(lchip, serdes_id+cnt, CTC_CHIP_SERDES_CG_MODE, 0));
        }
    }

    return ret;
}

STATIC int32
_sys_goldengate_port_set_cl73_aec_rx_en(uint8 lchip, uint16 gport, uint32 enable)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    uint8 cnt = 0;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, enable:%d\n", gport, enable?TRUE:FALSE);

    /* 1.get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    for(cnt=0; cnt<serdes_num; cnt++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_aec_rx_en(lchip, serdes_id+cnt, enable));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_dfe_en(uint8 lchip, uint16 gport, uint32 enable)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    uint8 cnt = 0;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, enable:%d\n", gport, enable?TRUE:FALSE);

    /* 1.get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    for(cnt=0; cnt<serdes_num; cnt++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_dfe_en(lchip, serdes_id+cnt, enable));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_auto_neg_bypass_en(uint8 lchip, uint16 gport, uint32 enable)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    uint8 cnt = 0;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, enable:%d\n", gport, enable?TRUE:FALSE);

    /* 1.get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    for(cnt=0; cnt<serdes_num; cnt++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_auto_neg_bypass_en(lchip, serdes_id+cnt, enable));
    }

    return CTC_E_NONE;
}

/* support 802.3ap, Auto-Negotiation for Backplane Ethernet */
STATIC int32
_sys_goldengate_port_set_cl73_auto_neg_en(uint8 lchip, uint16 gport, uint32 enable)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8  serdes_num = 0;
    uint32 mac_enable = 0;
    uint8  cnt = 0;
    uint32 value = 0;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Enter %s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, enable:0x%X\n", gport, enable);

    /* check, if cfg cl73, should keep mac enable */
    sys_goldengate_port_get_mac_en(lchip, gport, &mac_enable);
    if (FALSE == mac_enable)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /* get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    /* auto-neg already enable, cann't set enable again*/
    if ((enable && p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status)
        ||((0 == enable) && (0 == p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status)))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    if (enable)
    {
        port_attr->pcs_init_mode = port_attr->pcs_mode;
        sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DIS, 0);
        port_attr->pcs_reset_en = 0;
    }
    p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status = 1;
    port_attr->is_first = 0;
    port_attr->switch_num = 0;
    port_attr->reset_app = 0;

    /* Note: CG mode port, should dynamic swicth to 40G */
    if(enable)
    {
        /*do pcs tx and rx reset release */
        CTC_ERROR_RETURN(_sys_goldengate_port_set_pcs_reset(lchip, port_attr, 0, 1));
        CTC_ERROR_RETURN(_sys_goldengate_port_set_pcs_reset(lchip, port_attr, 1, 1));
        /*disable dfe*/
        CTC_ERROR_RETURN(_sys_goldengate_port_set_dfe_en(lchip, gport, 0));

        /*do dynamic switch, for 40G port only do check*/
            CTC_ERROR_RETURN(_sys_goldengate_port_cl73_dynamic_switch(lchip, lport, enable));
    }
    else
    {
        if(CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode && CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_init_mode)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_cl73_dynamic_switch(lchip, lport, enable));
        }

        /*do pcs tx and rx reset release */
        CTC_ERROR_RETURN(_sys_goldengate_port_set_pcs_reset(lchip, port_attr, 0, 0));
        CTC_ERROR_RETURN(_sys_goldengate_port_set_pcs_reset(lchip, port_attr, 1, 0));

        /* 1. if disable, need do 3ap trainging disable first */
        CTC_ERROR_RETURN(sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DIS, 1));

        /*enable dfe*/
        CTC_ERROR_RETURN(_sys_goldengate_port_set_dfe_en(lchip, gport, 1));
    }

    value = enable?0:1;
    CTC_ERROR_RETURN(_sys_goldengate_port_set_cl73_aec_rx_en(lchip, gport, value));
    /* 2. enable 3ap auto-neg */
    for(cnt = 0; cnt < serdes_num; cnt++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_auto_neg_en(lchip, serdes_id+cnt, enable));
    }

    if(!enable)
    {
        p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status = 0;
    }
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Exit %s\n", __FUNCTION__);

    return CTC_E_NONE;
}

/* support 802.3ap, Auto-Negotiation for Backplane Ethernet */
int32
sys_goldengate_port_set_cl73_ability(uint8 lchip, uint16 gport, uint32 ability)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8  serdes_num = 0;
    uint32 chip_ability = 0;
    uint8  cnt = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, ability:0x%X\n", gport, ability);

    /* get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    /* 1. get ability */
    if(ability & CTC_PORT_CL73_10GBASE_KR)
    {
        chip_ability |= SYS_PORT_CL73_10GBASE_KR;
    }
    if(ability & CTC_PORT_CL73_40GBASE_KR4)
    {
        chip_ability |= SYS_PORT_CL73_40GBASE_KR4;
    }
    if(ability & CTC_PORT_CL73_40GBASE_CR4)
    {
        chip_ability |= SYS_PORT_CL73_40GBASE_CR4;
    }
    if(ability & CTC_PORT_CL73_100GBASE_KR4)
    {
        chip_ability |= SYS_PORT_CL73_100GBASE_KR4;
    }
    if(ability & CTC_PORT_CL73_100GBASE_CR4)
    {
        chip_ability |= SYS_PORT_CL73_100GBASE_CR4;
    }
    if(ability & CTC_PORT_CL73_FEC_ABILITY)
    {
        chip_ability |= SYS_PORT_CL73_FEC_ABILITY;
    }
    if(ability & CTC_PORT_CL73_FEC_REQUESTED)
    {
        chip_ability |= SYS_PORT_CL73_FEC_REQUESTED;
    }
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d, chip_ability:0x%X\n", gport, chip_ability);

    /* 2. cfg ability */
    for(cnt=0; cnt<serdes_num; cnt++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_auto_neg_ability(lchip, serdes_id+cnt, chip_ability));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_cl73_ability(uint8 lchip, uint16 gport, uint32 type, uint32* p_ability)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8  slice_id  = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    uint8 cnt = 0;
    uint32 serdes_ability[4] = {0};

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"gport:%d\n", gport);

    /* 1. get port info from sw table */
    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    /* if auto_neg_ok, should get ability */
    if (type)
    {
        for(cnt=0; cnt<serdes_num; cnt++)
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_auto_neg_remote_ability(lchip, serdes_id+cnt, &serdes_ability[cnt]));
        }
    }
    else
    {
        for(cnt=0; cnt<serdes_num; cnt++)
        {
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_auto_neg_local_ability(lchip, serdes_id+cnt, &serdes_ability[cnt]));
        }
    }
    *p_ability = (serdes_ability[0]| serdes_ability[1]| serdes_ability[2]| serdes_ability[3]);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "port %s ability :0x%X\n", type?"remote":"local", *p_ability);

    return CTC_E_NONE;
}
/* get serdes signal detect by gport, used for port enable and disable */
int32
sys_goldengate_port_get_mac_signal_detect(uint8 lchip, uint16 gport, uint32* p_is_detect)
{
    uint16 lport     = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));

    /* XAUI, DXAUI, XLG, CG serdes_num is 4 */
    if(1 == serdes_num)
    {
        *p_is_detect = (uint32)sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id);
    }
    else /* seedes_num == 4 */
    {
        if((1 == sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id))
            &&(1 == sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id + 1))
            &&(1 == sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id + 2))
            &&(1 == sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id + 3)))
        {
            *p_is_detect = 1;
        }
        else
        {
            *p_is_detect = 0;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_link_intr(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint16 mac_id = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 tb_index = 0;
    uint32 value = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    mac_id = port_attr->mac_id;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port link interrupt, gport:%d, mac_id:%d, enable:%d\n", \
                     gport, mac_id, enable);
    if (enable)
    {
        tb_index = 3;
        sys_goldengate_port_clear_link_intr(lchip, gport);
    }
    else
    {
        tb_index = 2;
    }

    if (mac_id < 32)
    {
        tb_id = RlmHsCtlInterruptFunc0_t + port_attr->slice_id;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        value = 1<<mac_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
    }
    else
    {
        if ((36 == mac_id) || (52 == mac_id))
        {
            tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            value = 1 << ((mac_id == 36) ? 16 :17);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
        }

        tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        if (mac_id < 40)
        {
            value = 1 << (mac_id-32);
        }
        else
        {
            value = 1 << (mac_id-40);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_link_intr(uint8 lchip, uint16 gport, uint32* enable)
{

    uint16 lport = 0;
    uint16 mac_id = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 tb_index = 0;
    uint32 value = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    mac_id = port_attr->mac_id;

    tb_index = 2;

    if (mac_id < 32)
    {
        tb_id = RlmHsCtlInterruptFunc0_t + port_attr->slice_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
        *enable = (value & (1<<mac_id))?0:1;
    }
    else
    {
        if (port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
        {
            tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            *enable = (value & (1 << ((mac_id == 36) ? 16 :17)))?0:1;
        }
        else
        {
            tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));

            if (mac_id < 40)
            {
                *enable =  (value & (1 << (mac_id-32)))?0:1;
            }
            else
            {
                *enable = (value & (1 << (mac_id-40)))?0:1;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_clear_link_intr(uint8 lchip, uint16 gport)
{

    uint16 lport = 0;
    uint16 mac_id = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 tb_index = 0;
    uint32 value = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    mac_id = port_attr->mac_id;

    tb_index = 1;

    if (mac_id < 32)
    {
        tb_id = RlmHsCtlInterruptFunc0_t + port_attr->slice_id;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        value = 1<<mac_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
    }
    else
    {
        if ((36 == mac_id) || (52 == mac_id))
        {
            tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            value = 1 << ((mac_id == 36) ? 16 :17);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
        }

        tb_id = RlmCsCtlInterruptFunc0_t + port_attr->slice_id;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        if (mac_id < 40)
        {
            value = 1 << (mac_id-32);
        }
        else
        {
            value = 1 << (mac_id-40);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_auto_neg(uint8 lchip, uint16 gport, uint8 type, uint32 value)
{
    uint16 lport     = 0;

    uint16 mac_id    = 0;
    uint8  index     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint16 step      = 0;
    uint32 mode_value= 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    /* 10M/100M/1G port auto neg */
    if ((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
        mac_id = (port_attr->mac_id < 40)?port_attr->mac_id:(port_attr->mac_id-8);

        /* cfg SharedPcsSgmiiCfg anEnable_f */
        step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
        tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+(port_attr->slice_id)*48) / 4 + ((mac_id+(port_attr->slice_id)*48)%4)*step;
        if(CTC_PORT_PROP_AUTO_NEG_EN == type)
        {
            cmd = DRV_IOW(tbl_id, SharedPcsSgmiiCfg00_anEnable0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));

            /* when auto-neg disable, need enable unidirectionen*/
            value = value?0:1;
            tbl_id = SharedPcsCfg0_t + (mac_id + port_attr->slice_id * 48)/4;
            cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_unidirectionEn0_f + mac_id%4));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        }
        else if(CTC_PORT_PROP_AUTO_NEG_MODE == type)
        {
            /* sdk only support two mode, 1000Base-X(2'b00), SGMII-Slaver(2'b10) */
            if(CTC_PORT_AUTO_NEG_MODE_1000BASE_X == value)
            {
                mode_value = 0;
            }
            else if(CTC_PORT_AUTO_NEG_MODE_SGMII_MASTER == value)
            {
                mode_value = 1;
            }
            else if(CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == value)
            {
                mode_value = 2;
            }
            cmd = DRV_IOW(tbl_id, SharedPcsSgmiiCfg00_anegMode0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &mode_value));
        }
    }
    /* 802.3 cl73 auto neg */
    else if ((CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode))
    {
        CTC_ERROR_RETURN(_sys_goldengate_port_set_cl73_auto_neg_en(lchip, gport, value));
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_auto_neg(uint8 lchip, uint16 gport, uint8 type, uint32* p_value)
{
    uint16 lport     = 0;

    uint16 mac_id    = 0;
    uint8  index     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint16 step      = 0;
    uint32 value     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    /* auto_neg only support sgmii mode */
    if ((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
        mac_id = (port_attr->mac_id < 40)?port_attr->mac_id:(port_attr->mac_id-8);
        /* read SharedPcsSgmiiCfg anEnable_f */
        step = SharedPcsSgmiiCfg10_t - SharedPcsSgmiiCfg00_t;
        tbl_id = SharedPcsSgmiiCfg00_t + (mac_id+(port_attr->slice_id)*48) / 4 + ((mac_id+(port_attr->slice_id)*48)%4)*step;

        if(CTC_PORT_PROP_AUTO_NEG_EN == type)
        {
            cmd = DRV_IOR(tbl_id, SharedPcsSgmiiCfg00_anEnable0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }
        else if(CTC_PORT_PROP_AUTO_NEG_MODE == type)
        {
            cmd = DRV_IOR(tbl_id, SharedPcsSgmiiCfg00_anegMode0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
        }

        *p_value = value;
    }
    else if ((CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
        || (CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode))
    {
        *p_value = p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status;
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_set_min_frame_size(uint8 lchip, ctc_frame_size_t index, uint16 value)
{
    int32  step = 0;

    uint8  slice = 0;
    uint32 cmd;
    uint32 field = 0;
    uint32 net_rx_len_chk_ctl_tbl[2] = {NetRxLenChkCtl0_t, NetRxLenChkCtl1_t};

    if (index > CTC_FRAME_SIZE_MAX)
    {
        return CTC_E_INVALID_FRAMESIZE_INDEX;
    }

    if ((value < SYS_GOLDENGATE_MIN_FRAMESIZE_MIN_VALUE)
       || (value > SYS_GOLDENGATE_MIN_FRAMESIZE_MAX_VALUE))
    {
        return CTC_E_INVALID_MIN_FRAMESIZE;
    }

    for (slice = 0; slice < 2; slice++)
    {
        field = value;
        step = NetRxLenChkCtl0_cfgMinLen1_f - NetRxLenChkCtl0_cfgMinLen0_f;
        cmd = DRV_IOW(net_rx_len_chk_ctl_tbl[slice], NetRxLenChkCtl0_cfgMinLen0_f + (step * index));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_get_min_frame_size(uint8 lchip, ctc_frame_size_t index, uint16* p_value)
{
    int32  step = 0;

    uint32 cmd;
    uint32 field = 0;

    if (index > CTC_FRAME_SIZE_MAX)
    {
        return CTC_E_INVALID_FRAMESIZE_INDEX;
    }

    CTC_PTR_VALID_CHECK(p_value);

    step = NetRxLenChkCtl0_cfgMinLen1_f - NetRxLenChkCtl0_cfgMinLen0_f;
    cmd = DRV_IOR(NetRxLenChkCtl0_t, NetRxLenChkCtl0_cfgMinLen0_f + (step * index));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    *p_value = field;

    return CTC_E_NONE;
}

int32
sys_goldengate_set_max_frame_size(uint8 lchip, ctc_frame_size_t index, uint16 value)
{
    int32  step = 0;

    uint8  slice = 0;
    uint32 cmd;
    uint32 field = 0;
    uint32 net_rx_len_chk_ctl_tbl[2] = {NetRxLenChkCtl0_t, NetRxLenChkCtl1_t};
    uint16 max_size = 0;
    ctc_chip_device_info_t device_info;

    max_size = SYS_GOLDENGATE_MAX_FRAMESIZE_MAX_VALUE;
    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, &device_info));

    /*for queue cnt overturn*/
    if (device_info.version_id <= 1)
    {
        max_size = SYS_GOLDENGATE_MAX_FRAMESIZE_DEFAULT_VALUE;
    }

    if ((value < SYS_GOLDENGATE_MAX_FRAMESIZE_MIN_VALUE) || (value > max_size))
    {
        return CTC_E_INVALID_MAX_FRAMESIZE;
    }

    for (slice = 0; slice < 2; slice++)
    {
        field = value;
        step = NetRxLenChkCtl0_cfgMaxLen1_f - NetRxLenChkCtl0_cfgMaxLen0_f;
        cmd = DRV_IOW(net_rx_len_chk_ctl_tbl[slice], NetRxLenChkCtl0_cfgMaxLen0_f + (step * index));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_get_max_frame_size(uint8 lchip, ctc_frame_size_t index, uint16* p_value)
{
    int32  step = 0;

    uint32 cmd;
    uint32 field = 0;

    CTC_PTR_VALID_CHECK(p_value);

    step = NetRxLenChkCtl0_cfgMaxLen1_f - NetRxLenChkCtl0_cfgMaxLen0_f;
    cmd = DRV_IOR(NetRxLenChkCtl0_t, NetRxLenChkCtl0_cfgMaxLen0_f + (step * index));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field));
    *p_value = field;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_min_framesize_index(uint8 lchip, uint16 size, uint8* p_index)
{
    uint8  idx = 0;
    uint16 value = 0;

    *p_index = CTC_FRAME_SIZE_MAX;

    for (idx = 0; idx < CTC_FRAME_SIZE_MAX; idx++)
    {
        CTC_ERROR_RETURN(sys_goldengate_get_min_frame_size(lchip, idx, &value));

        if (size == value)
        {
            *p_index = idx;
            break;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_max_framesize_index(uint8 lchip, uint16 size, uint8* p_index)
{
    uint8  idx = 0;
    uint16 value = 0;

    *p_index = CTC_FRAME_SIZE_MAX;

    for (idx = 0; idx < CTC_FRAME_SIZE_MAX; idx++)
    {
        CTC_ERROR_RETURN(sys_goldengate_get_max_frame_size(lchip, idx, &value));

        if (size == value)
        {
            *p_index = idx;
            break;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_select_chan_framesize_index(uint8 lchip, uint16 value, sys_port_framesize_type_t type, uint8* p_index)
{
    if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_get_min_framesize_index(lchip, value, p_index));
        if (CTC_FRAME_SIZE_MAX == *p_index)
        {
            return CTC_E_INVALID_MIN_FRAMESIZE;
        }
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_get_max_framesize_index(lchip, value, p_index));
        if (CTC_FRAME_SIZE_MAX == *p_index)
        {
            return CTC_E_INVALID_MAX_FRAMESIZE;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_chan_framesize_index(uint8 lchip, uint8 slice_id, uint8 mac_id, uint8 index, sys_port_framesize_type_t type)
{
    uint32 vlan_base = 0;
    uint32 cmd = 0;
    uint32 channelize_en = 0;
    uint32 tbl_channelize_mode[2] = {DsChannelizeMode0_t, DsChannelizeMode1_t};
    uint32 tbl_channelize_ing_fc[2] = {DsChannelizeIngFc0_t, DsChannelizeIngFc1_t};
    uint32 drv_index = index;
    DsChannelizeMode0_m ds_channelize_mode;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set index:%d, type:%d.\n", index, type);

    sal_memset(&ds_channelize_mode, 0, sizeof(DsChannelizeMode0_m));
    cmd = DRV_IOR(tbl_channelize_mode[slice_id], DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mac_id & 0x3F), cmd, &ds_channelize_mode));
    GetDsChannelizeMode0(A, channelizeEn_f, &ds_channelize_mode, &channelize_en);

    if (channelize_en)
    {
        if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
        {
            cmd = DRV_IOW(tbl_channelize_ing_fc[slice_id], DsChannelizeIngFc0_chanMinLenSelId_f);
        }
        else
        {
            cmd = DRV_IOW(tbl_channelize_ing_fc[slice_id], DsChannelizeIngFc0_chanMaxLenSelId_f);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mac_id & 0x3F), cmd, &drv_index));
    }
    else
    {
        GetDsChannelizeMode0(A, vlanBase_f, &ds_channelize_mode, &vlan_base);
        if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
        {
            vlan_base &= ~(((uint16)0x7) << 5);
            vlan_base |= (index & 0x7) << 5;
        }
        else
        {
            vlan_base &= ~(((uint16)0x7) << 8);
            vlan_base |= (index & 0x7) << 8;
        }
        SetDsChannelizeMode0(A, vlanBase_f, &ds_channelize_mode, &vlan_base);
        cmd = DRV_IOW(tbl_channelize_mode[slice_id], DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mac_id & 0x3F), cmd, &ds_channelize_mode));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_chan_framesize_index(uint8 lchip, uint8 slice_id, uint8 mac_id, uint8* p_index, sys_port_framesize_type_t type)
{
    uint32  index = 0;
    uint32 vlan_base = 0;
    uint32 cmd = 0;
    uint32 channelize_en = 0;
    uint32 tbl_channelize_mode[2] = {DsChannelizeMode0_t, DsChannelizeMode1_t};
    uint32 tbl_channelize_ing_fc[2] = {DsChannelizeIngFc0_t, DsChannelizeIngFc1_t};
    DsChannelizeMode0_m ds_channelize_mode;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&ds_channelize_mode, 0, sizeof(DsChannelizeMode0_m));
    cmd = DRV_IOR(tbl_channelize_mode[slice_id], DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mac_id & 0x3F), cmd, &ds_channelize_mode));
    GetDsChannelizeMode0(A, channelizeEn_f, &ds_channelize_mode, &channelize_en);

    if (channelize_en)
    {
        if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
        {
            cmd = DRV_IOR(tbl_channelize_ing_fc[slice_id], DsChannelizeIngFc0_chanMinLenSelId_f);
        }
        else
        {
            cmd = DRV_IOR(tbl_channelize_ing_fc[slice_id], DsChannelizeIngFc0_chanMaxLenSelId_f);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, (mac_id & 0x3F), cmd, &index));
    }
    else
    {
        GetDsChannelizeMode0(A, vlanBase_f, &ds_channelize_mode, &vlan_base);
        if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
        {
            index = (vlan_base >> 5) & 0x7;
        }
        else
        {
            index = (vlan_base >> 8) & 0x7;
        }
    }
    *p_index = index;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_port_set_framesize(uint8 lchip, uint16 gport, uint16 value, sys_port_framesize_type_t type)
{
    uint8  index = 0;

    uint16 lport = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port_cap));

    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    CTC_ERROR_RETURN(_sys_goldengate_select_chan_framesize_index(lchip, value, type, &index));
    CTC_ERROR_RETURN(_sys_goldengate_port_set_chan_framesize_index(lchip, p_port_cap->slice_id, p_port_cap->mac_id, index, type));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_framesize(uint8 lchip, uint16 gport, uint16* p_value, sys_port_framesize_type_t type)
{

    uint8  index = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port_cap));

    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    CTC_ERROR_RETURN(_sys_goldengate_port_get_chan_framesize_index(lchip, p_port_cap->slice_id, p_port_cap->mac_id, &index, type));

    if (SYS_PORT_FRAMESIZE_TYPE_MIN == type)
    {
        CTC_ERROR_RETURN(sys_goldengate_get_min_frame_size(lchip, index, p_value));
    }
    else
    {
        CTC_ERROR_RETURN(sys_goldengate_get_max_frame_size(lchip, index, p_value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_sgmii_link_status(uint8 lchip, uint8 type, uint8 slice_id, uint8 mac_id, uint32* p_value, uint32 undir_en)
{
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;
    uint8 index = 0;

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    quad_idx = (mac_id+ slice_id * 48)/4;
    sub_idx = (mac_id+ slice_id * 48)%4;
    tb_id = SharedPcsSgmiiStatus00_t + (SharedPcsSgmiiStatus01_t - SharedPcsSgmiiStatus00_t)*quad_idx +
        (SharedPcsSgmiiStatus10_t - SharedPcsSgmiiStatus00_t)*sub_idx;

    if (type == SYS_PORT_MAC_STATUS_TYPE_LINK)
    {
        cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_anLinkStatus0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        *p_value = (value)?TRUE:FALSE;
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_CODE_ERR)
    {
        /* read 3 times */
        for (index = 0; index < 3; index++)
        {
            cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_codeErrCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

            *p_value = value;
            if (0 == (*p_value))
            {
                break;
            }
            else
            {
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "sgmii index %d slice %d mac_id %d code error!\n",index, slice_id, mac_id);
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "value= %d\n", value);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_xaui_link_status(uint8 lchip, uint8 type, uint8 slice_id, uint8 mac_id, uint32* p_value, uint32 unidir_en)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value[4] = {0};
    uint8 index = 0;
    SharedPcsXauiStatus0_m shared_xaui_status;

    if (mac_id%4)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_LINK)
    {
        /*Xaui and Xlg using the same align status*/
        tb_id = SharedPcsXlgStatus0_t + (mac_id + slice_id*48)/4;

        cmd = DRV_IOR(tb_id, SharedPcsXlgStatus0_alignStatus_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value[0]));

        if (unidir_en)
        {
            *p_value = value[0]?TRUE:FALSE;
        }
        else
        {
            /*check link fault state*/
            cmd = DRV_IOR(tb_id, SharedPcsXlgStatus0_linkFaultState_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value[1]));

            *p_value = (value[0])?(value[1]?FALSE:TRUE):FALSE;
        }
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_CODE_ERR)
    {
        tb_id = SharedPcsXauiStatus0_t + (mac_id + slice_id*48)/4;

        for (index = 0; index < 3; index++)
        {
            sal_memset(&shared_xaui_status, 0, sizeof(shared_xaui_status));
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &shared_xaui_status));

            value[0] = GetSharedPcsXauiStatus0(V, codeErrorCnt0_f, &shared_xaui_status);
            value[1] = GetSharedPcsXauiStatus0(V, codeErrorCnt1_f, &shared_xaui_status);
            value[2] = GetSharedPcsXauiStatus0(V, codeErrorCnt2_f, &shared_xaui_status);
            value[3] = GetSharedPcsXauiStatus0(V, codeErrorCnt3_f, &shared_xaui_status);

            *p_value = (value[0]+value[1]+value[2]+value[3]);

            if (0 == (*p_value))
            {
                break;
            }
            else
            {
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "xaui index %d slice %d mac_id %d code error!\n",index, slice_id, mac_id);
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "value[0]= %d, value[1]= %d, value[2]= %d, value[3]= %d\n",  \
                    value[0], value[1], value[2], value[3]);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_xfi_link_status(uint8 lchip, uint8 type, uint8 slice_id, uint8 mac_id, uint32* p_value, uint32 unidir_en)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;
    uint32 hiber = 0;
    uint32 fault = 0;
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint8 index = 0;

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    quad_idx = (mac_id+ slice_id * 48)/4;
    sub_idx = (mac_id+ slice_id * 48)%4;
    tb_id = SharedPcsXfiStatus00_t + (SharedPcsXfiStatus01_t - SharedPcsXfiStatus00_t)*quad_idx +
        (SharedPcsXfiStatus10_t - SharedPcsXfiStatus00_t)*sub_idx;

    if (type == SYS_PORT_MAC_STATUS_TYPE_LINK)
    {
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_rxBlockLock0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_hiBer0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hiber));

        if (unidir_en)
        {
            if (value && !hiber)
            {
                *p_value = TRUE;
            }
            else
            {
                *p_value = FALSE;
            }
        }
        else
        {
            /*check link fault state*/
            cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_linkFaultState0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fault));

            if (value && !hiber && !fault)
            {
                *p_value = TRUE;
            }
            else
            {
                *p_value = FALSE;
            }
        }
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_CODE_ERR)
    {
        for (index = 0; index < 3; index++)
        {
            cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_errBlockCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            *p_value = value;
            if (0 == (*p_value))
            {
                break;
            }
            else
            {
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "xfi index %d slice %d mac_id %d code error!\n",index, slice_id, mac_id);
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "value= %d\n", value);
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_xlg_link_status(uint8 lchip, uint8 type, uint8 slice_id, uint8 mac_id, uint32* p_value, uint32 unidir_en)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value[4] = {0};
    uint32 value_sub[4] = {0};
    uint32 tb_id_sub = 0;
    uint8 quad_idx = 0;
    uint8 index = 0;
    uint8 step = 0;
    SharedPcsXlgStatus0_m xlg_status;

    if (mac_id%4)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    quad_idx = (mac_id+ slice_id * 48)/4;

    /*Xaui and Xlg using the same align status*/
    tb_id = SharedPcsXlgStatus0_t + quad_idx;
    tb_id_sub = SharedPcsXfiStatus00_t + (SharedPcsXfiStatus01_t - SharedPcsXfiStatus00_t)*quad_idx;
    step = (SharedPcsXfiStatus10_t - SharedPcsXfiStatus00_t);

    if (type == SYS_PORT_MAC_STATUS_TYPE_LINK)
    {
        cmd = DRV_IOR(tb_id, SharedPcsXlgStatus0_alignStatus_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value[0]));

        if (unidir_en)
        {
            *p_value = (value[0])?TRUE:FALSE;
        }
        else
        {
            /*check link fault state*/
            cmd = DRV_IOR(tb_id, SharedPcsXlgStatus0_linkFaultState_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value[1]));

            *p_value = (value[0])?(value[1]?FALSE:TRUE):FALSE;
        }
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_CODE_ERR)
    {
        for (index = 0; index < 3; index++)
        {
            sal_memset(&xlg_status, 0, sizeof(xlg_status));
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlg_status));

            value[0] = GetSharedPcsXlgStatus0(V, bipErrCnt0_f, &xlg_status);
            cmd = DRV_IOR(tb_id_sub, SharedPcsXfiStatus00_errBlockCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_sub[0]));

            value[1] = GetSharedPcsXlgStatus0(V, bipErrCnt1_f, &xlg_status);
            cmd = DRV_IOR(tb_id_sub+step, SharedPcsXfiStatus00_errBlockCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_sub[1]));

            value[2] = GetSharedPcsXlgStatus0(V, bipErrCnt2_f, &xlg_status);
            cmd = DRV_IOR(tb_id_sub+step*2, SharedPcsXfiStatus00_errBlockCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_sub[2]));

            value[3] = GetSharedPcsXlgStatus0(V, bipErrCnt3_f, &xlg_status);
            cmd = DRV_IOR(tb_id_sub+step*3, SharedPcsXfiStatus00_errBlockCnt0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value_sub[3]));

            *p_value = (value[0]+value[1]+value[2]+value[3]+value_sub[0]+value_sub[1]+ value_sub[2]+value_sub[3]);

            if (0 == (*p_value))
            {
                break;
            }
            else
            {
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "xlg index %d slice %d mac_id %d code error!\n", index, slice_id, mac_id);
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "value[0]= %d, value[1]= %d, value[2]= %d, value[3]= %d, value_sub[0]= %d, value_sub[1]= %d, value_sub[2]= %d, value_sub[3]= %d\n",  \
                    value[0], value[1], value[2], value[3], value_sub[0], value_sub[1], value_sub[2], value_sub[3]);
            }
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_cg_link_status(uint8 lchip, uint8 type, uint8 slice_id, uint8 mac_id, uint32* p_value, uint32 unidir_en)
{
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value[4] = {0};
    uint32 tb_id_db = 0;
    CgPcsDebugStats0_m cg_status;
    uint8 index = 0;

    if (mac_id == 36)
    {
        tb_id = CgPcsStatus0_t + 2*slice_id;
        tb_id_db = CgPcsDebugStats0_t + 2*slice_id;
    }
    else if (mac_id == 52)
    {
        tb_id = CgPcsStatus1_t + 2*slice_id;
        tb_id_db = CgPcsDebugStats1_t + 2*slice_id;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_LINK)
    {
        cmd = DRV_IOR(tb_id, CgPcsStatus0_alignStatus_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value[0]));

        tb_id = CsMacroCfg0_t + ((mac_id==36)?0:1) + 2*slice_id;
        cmd = DRV_IOR(tb_id, CsMacroCfg0_cfgAMode16GFc_f);
        DRV_IOCTL(lchip, 0, cmd, &value[1]);

        *p_value = (value[1])?FALSE:((value[0])?TRUE:FALSE);
    }

    if (type == SYS_PORT_MAC_STATUS_TYPE_CODE_ERR)
    {
        for (index = 0; index < 3; index++)
        {
            sal_memset(&cg_status, 0, sizeof(cg_status));
            cmd = DRV_IOR(tb_id_db, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cg_status));

            value[0] = GetCgPcsDebugStats0(V, errBlockCnt0_f, &cg_status);
            value[1] = GetCgPcsDebugStats0(V, errBlockCnt1_f, &cg_status);
            value[2] = GetCgPcsDebugStats0(V, errBlockCnt2_f, &cg_status);
            value[3] = GetCgPcsDebugStats0(V, errBlockCnt3_f, &cg_status);

            *p_value = (value[0]+value[1]+value[2]+value[3]);

            if (0 == (*p_value))
            {
                break;
            }
            else
            {
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "cg index %d slice %d mac_id %d code error!\n",index, slice_id, mac_id);
                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "value[0]= %d, value[1]= %d, value[2]= %d, value[3]= %d\n",  \
                    value[0], value[1], value[2], value[3]);
            }
        }
    }

    return CTC_E_NONE;
}

/*return 15bit:1-link up 0-link down, 11-10bit:10-1000M 01-100M 00-10M*/
int32
sys_goldengate_port_get_sgmii_remote_info(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;
    uint8 slice_id = 0;
    uint8 mac_id = 0;
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    slice_id = port_attr->slice_id;
    mac_id = port_attr->mac_id;

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    quad_idx = (mac_id+ slice_id * 48)/4;
    sub_idx = (mac_id+ slice_id * 48)%4;
    tb_id = SharedPcsSgmiiStatus00_t + (SharedPcsSgmiiStatus01_t - SharedPcsSgmiiStatus00_t)*quad_idx +
        (SharedPcsSgmiiStatus10_t - SharedPcsSgmiiStatus00_t)*sub_idx;


    cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_anRxRemoteCfg0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    *p_value = value;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_sgmii_remote_link_status(uint8 lchip, uint16 gport, uint32 auto_neg_en, uint32 auto_neg_mode, uint32* p_value)
{
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 value = 0;
    uint8 slice_id = 0;
    uint8 mac_id = 0;
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    *p_value = TRUE;
    if (0 == auto_neg_en)
    {
        return CTC_E_NONE;
    }
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    slice_id = port_attr->slice_id;
    mac_id = port_attr->mac_id;

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    quad_idx = (mac_id+ slice_id * 48)/4;
    sub_idx = (mac_id+ slice_id * 48)%4;
    tb_id = SharedPcsSgmiiStatus00_t + (SharedPcsSgmiiStatus01_t - SharedPcsSgmiiStatus00_t)*quad_idx +
        (SharedPcsSgmiiStatus10_t - SharedPcsSgmiiStatus00_t)*sub_idx;

    cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_anRxRemoteCfg0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    if (CTC_PORT_AUTO_NEG_MODE_1000BASE_X == auto_neg_mode)
    {
        value = (value>>0xB)&0x3;
        if (value)
        {
            *p_value = FALSE;
        }
        else
        {
            *p_value = TRUE;
        }
    }
    else
    {
        *p_value = (value>>0xF)&0x1;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_mac_link_up(uint8 lchip, uint16 gport, uint32* p_is_up, uint32 is_port)
{
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    int32 ret = 0;
    uint32 unidir_en = 0;
    uint32 remote_link = 0;
    uint32 auto_neg_en = 0;
    uint32 auto_neg_mode = 0;
    uint32 fec_en = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sys_goldengate_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en);
    sys_goldengate_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, &auto_neg_mode);

    PORT_LOCK;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        PORT_UNLOCK;
        return CTC_E_MAC_NOT_USED;
    }

    if (((port_attr->pcs_mode == CTC_CHIP_SERDES_XLG_MODE) || (port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE))
        && (port_attr->pcs_reset_en == 1))
    {
        _sys_goldengate_port_get_fec_en(lchip, gport, &fec_en);
        if (fec_en && (port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE))
        {
            *p_is_up = FALSE;
        }
        else
        {
            *p_is_up = TRUE;
        }
        PORT_UNLOCK;
        return CTC_E_NONE;
    }

    _sys_goldengate_port_get_unidir_en(lchip, gport, &unidir_en);

    switch (port_attr->pcs_mode)
    {
        case CTC_CHIP_SERDES_SGMII_MODE:
        case CTC_CHIP_SERDES_2DOT5G_MODE:
            ret = _sys_goldengate_port_get_sgmii_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id,  port_attr->mac_id, p_is_up, unidir_en);
            if (is_port)
            {
                ret = sys_goldengate_port_get_sgmii_remote_link_status(lchip, gport, auto_neg_en, auto_neg_mode, &remote_link);
                if (FALSE == remote_link)
                {
                    *p_is_up = FALSE;
                }
            }
            break;

        case CTC_CHIP_SERDES_XAUI_MODE:
            ret = _sys_goldengate_port_get_xaui_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id, port_attr->mac_id, p_is_up, unidir_en);
            break;

        case CTC_CHIP_SERDES_DXAUI_MODE:
            ret = _sys_goldengate_port_get_xaui_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id, port_attr->mac_id, p_is_up, unidir_en);
            break;

        case CTC_CHIP_SERDES_XFI_MODE:
            ret = _sys_goldengate_port_get_xfi_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id, port_attr->mac_id, p_is_up, unidir_en);
            break;

        case CTC_CHIP_SERDES_XLG_MODE:
            ret = _sys_goldengate_port_get_xlg_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id, port_attr->mac_id, p_is_up, unidir_en);
            break;

        case CTC_CHIP_SERDES_CG_MODE:
            ret = _sys_goldengate_port_get_cg_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_LINK,
                port_attr->slice_id, port_attr->mac_id, p_is_up, unidir_en);
            break;

        default:
            break;
    }

    PORT_UNLOCK;

    return ret;
}

int32
sys_goldengate_port_get_mac_code_err(uint8 lchip, uint16 gport, uint32* code_err)
{
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    int32 ret = 0;
    uint32 unidir_en = 0;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    _sys_goldengate_port_get_unidir_en(lchip, gport, &unidir_en);
    if (unidir_en)
    {
        *code_err = 0;
        return CTC_E_NONE;
    }

    switch (port_attr->pcs_mode)
    {
        case CTC_CHIP_SERDES_SGMII_MODE:
        case CTC_CHIP_SERDES_2DOT5G_MODE:
            ret = _sys_goldengate_port_get_sgmii_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_CODE_ERR,
                port_attr->slice_id,  port_attr->mac_id, code_err, 0);
            break;

        case CTC_CHIP_SERDES_XAUI_MODE:
            ret = _sys_goldengate_port_get_xaui_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_CODE_ERR,
                port_attr->slice_id, port_attr->mac_id, code_err, 0);
            break;

        case CTC_CHIP_SERDES_XFI_MODE:
            ret = _sys_goldengate_port_get_xfi_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_CODE_ERR,
                port_attr->slice_id, port_attr->mac_id, code_err, 0);
            break;

        case CTC_CHIP_SERDES_XLG_MODE:
            ret = _sys_goldengate_port_get_xlg_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_CODE_ERR,
                port_attr->slice_id, port_attr->mac_id, code_err, 0);
            break;

        case CTC_CHIP_SERDES_CG_MODE:
            ret = _sys_goldengate_port_get_cg_link_status(lchip, SYS_PORT_MAC_STATUS_TYPE_CODE_ERR,
                port_attr->slice_id, port_attr->mac_id, code_err, 0);
            break;

        default:
            break;
    }

    return ret;
}

int32
sys_goldengate_port_set_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop)
{
    uint16 gport                    = 0;
    uint8 dir                       = 0;

    uint16 lport                    = 0;
    uint8 chan_id                   = 0;
    uint8 lchan_id                  = 0;
    uint8 mac_id                    = 0;
    uint8 lmac_id                   = 0;
    uint8 slice_id                  = 0;
    uint32 cmd                      = 0;
    uint32 tbl_id                   = 0;
    uint32 tbl_id1                  = 0;
    uint32 field_val                = 0;
    uint32 rx_pause_en              = 0;
    uint32 pfc_pause_en             = 0;
    uint32 tmp_pfc_pause_en             = 0;
    uint32 vlan_base                = 0;
    uint32 chan_en_bitmap[2];
    uint32 stall_en_bitmap[2];
    uint32 old_pfc_class = 0;
    uint32 old_pfc_class_bmp = 0;
    uint32 pri = 0;
    uint32 index = 0;

    ds_t ds;
    ds_t ds1;
    BufStorePfcChanEn_m pfc_chan_en;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X \n", fc_prop->gport);

    gport = fc_prop->gport;
    dir = fc_prop->dir;
    CTC_MAX_VALUE_CHECK(fc_prop->priority_class, 7);
    CTC_MAX_VALUE_CHECK(fc_prop->pfc_class, 7);

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chan_id = ((slice_id << 6)|port_attr->chan_id);
    mac_id  = port_attr->mac_id;
    lchan_id = chan_id&0x3F;
    lmac_id =  mac_id&0x3F;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_id:%d, lmac_id:%d, slice_id:%d\n", chan_id, lmac_id, slice_id);

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        sal_memset(ds, 0, sizeof(ds));

        tbl_id = NetTxNetRxPauseTypeCtl0_t + slice_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

        /*Rx port pfc enable*/
        if (fc_prop->is_pfc)
        {
            if (lmac_id < 32)
            {
                rx_pause_en = GetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn31To0_f, ds);
                fc_prop->enable?CTC_BIT_SET(rx_pause_en, lmac_id&0x1F):CTC_BIT_UNSET(rx_pause_en, lmac_id&0x1F);
                SetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn31To0_f, ds, rx_pause_en);
            }
            else
            {
                rx_pause_en = GetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn63To32_f, ds);
                fc_prop->enable?CTC_BIT_SET(rx_pause_en, lmac_id&0x1F):CTC_BIT_UNSET(rx_pause_en, lmac_id&0x1F);
                SetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn63To32_f, ds, rx_pause_en);
            }
        }
        else
        {
            if (lmac_id < 32)
            {
                rx_pause_en = GetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn31To0_f, ds);
                CTC_BIT_UNSET(rx_pause_en, lmac_id&0x1F);
                SetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn31To0_f, ds, rx_pause_en);
            }
            else
            {
                rx_pause_en = GetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn63To32_f, ds);
                CTC_BIT_UNSET(rx_pause_en, lmac_id&0x1F);
                SetNetTxNetRxPauseTypeCtl0(V, netRxPfcPauseEn63To32_f, ds, rx_pause_en);
            }
        }
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


        sal_memset(ds, 0, sizeof(ds));
        tbl_id = DsChannelizeMode0_t + slice_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, ds));
        vlan_base = GetDsChannelizeMode0(V, vlanBase_f, ds);
        sal_memset(ds1, 0, sizeof(ds1));
        tbl_id1 = NetRxPfcCfg0_t + slice_id;
        cmd = DRV_IOR(tbl_id1, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, ds1));
        tmp_pfc_pause_en = GetNetRxPfcCfg0(V, cfgPriorityEn_f, ds1);

        /*PFC enable/disable*/
        if (fc_prop->is_pfc)
        {
            if (fc_prop->enable)
            {
                /*enable PFC cos rx*/
                pfc_pause_en = GetDsChannelizeMode0(V, localPhyPortBase_f, ds);
                CTC_BIT_SET(pfc_pause_en, fc_prop->priority_class);
                CTC_BIT_SET(tmp_pfc_pause_en, fc_prop->priority_class);
                SetDsChannelizeMode0(V, localPhyPortBase_f, ds, pfc_pause_en);
                SetNetRxPfcCfg0(V, cfgPriorityEn_f, ds1, tmp_pfc_pause_en);
                CTC_BIT_SET(vlan_base, 11);
            }
            else
            {
                /*disable PFC cos rx*/
                pfc_pause_en = GetDsChannelizeMode0(V, localPhyPortBase_f, ds);
                CTC_BIT_UNSET(pfc_pause_en, fc_prop->priority_class);
                CTC_BIT_UNSET(tmp_pfc_pause_en, fc_prop->priority_class);
                SetDsChannelizeMode0(V, localPhyPortBase_f, ds, pfc_pause_en);
                SetNetRxPfcCfg0(V, cfgPriorityEn_f, ds1, tmp_pfc_pause_en);
                if (0 == pfc_pause_en)
                {
                    CTC_BIT_UNSET(vlan_base, 11);
                }
            }
        }
        else  /*Normal Pause enable/disable*/
        {
            if (fc_prop->enable)
            {
                CTC_BIT_SET(vlan_base, 11);
                SetNetRxPfcCfg0(V, cfgPriorityEn_f, ds1, 0x80);
            }
            else
            {
                SetNetRxPfcCfg0(V, cfgPriorityEn_f, ds1, 0x00);
            }
        }

        SetDsChannelizeMode0(V, vlanBase_f, ds, vlan_base);
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, ds));
        cmd = DRV_IOW(tbl_id1, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, ds1));

        sal_memset(ds, 0, sizeof(ds));
        tbl_id = NetRxPauseCtl0_t + slice_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

        /*PFC enable/disable*/
        if (fc_prop->is_pfc)
        {
            rx_pause_en = GetNetRxPauseCtl0(V, cfgPfcPauseEn0_f + lmac_id / 16, ds);
            if (fc_prop->enable)
            {
                CTC_BIT_SET(rx_pause_en, lmac_id % 16);
            }
            SetNetRxPauseCtl0(V, cfgPfcPauseEn0_f + mac_id / 16, ds, rx_pause_en);
        }
        else
        {
            rx_pause_en = GetNetRxPauseCtl0(V, cfgNormPauseEn0_f + lmac_id / 16, ds);
            pfc_pause_en = GetNetRxPauseCtl0(V, cfgPfcPauseEn0_f + lmac_id / 16, ds);
            if (fc_prop->enable)
            {
                CTC_BIT_SET(rx_pause_en, lmac_id % 16);
                CTC_BIT_UNSET(pfc_pause_en, lmac_id % 16);
            }
            SetNetRxPauseCtl0(V, cfgPfcPauseEn0_f + mac_id/16, ds, pfc_pause_en);
            SetNetRxPauseCtl0(V, cfgNormPauseEn0_f + lmac_id/16, ds, rx_pause_en);
        }

        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

        /*stall enable*/
        if(!fc_prop->is_pfc)
        {
            sal_memset(ds, 0, sizeof(ds));
            tbl_id = NetTxNetRxStallEnBmp0_t + slice_id;

            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
            GetNetTxNetRxStallEnBmp0(A, netRxStallEnBmp_f, ds, stall_en_bitmap);
            if (fc_prop->enable)
            {
                CTC_BIT_SET(stall_en_bitmap[lmac_id >> 5], (lmac_id&0x1F));
            }
            else if(0 == pfc_pause_en)
            {
                CTC_BIT_UNSET(stall_en_bitmap[lmac_id >> 5], (lmac_id&0x1F));
            }
            SetNetTxNetRxStallEnBmp0(A, netRxStallEnBmp_f, ds, stall_en_bitmap);
            cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        }

    }

    /* egress only global contral */
    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        /*Init the pfc cos*/
        if (fc_prop->is_pfc)
        {
            cmd = DRV_IOR(DsIgrPortTcPriMap_t, DsIgrPortTcPriMap_pfcPriBmp0_f + fc_prop->priority_class);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &old_pfc_class_bmp));

            for (old_pfc_class = 0; old_pfc_class < 8; old_pfc_class ++)
            {
                if (CTC_IS_BIT_SET(old_pfc_class_bmp, old_pfc_class))
                {
                    break;
                }
            }

            if (old_pfc_class_bmp && (old_pfc_class != fc_prop->pfc_class) && fc_prop->is_pfc && fc_prop->enable)
            {
                CTC_ERROR_RETURN(sys_goldengate_qos_flow_ctl_profile(lchip, gport,
                                                                 fc_prop->priority_class,
                                                                 old_pfc_class,
                                                                 fc_prop->is_pfc,
                                                                 0));

                cmd = DRV_IOR(NetTxPauseCurReqTab0_t + slice_id, NetTxPauseCurReqTab0_pri_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmac_id, cmd, &pri));

                pri &= ~old_pfc_class_bmp;

                cmd = DRV_IOW(NetTxPauseCurReqTab0_t + slice_id, NetTxPauseCurReqTab0_pri_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lmac_id, cmd, &pri));


                tbl_id = PfcPriorityEnTableSlice0_t + slice_id;
                cmd = DRV_IOR(tbl_id, PfcPriorityEnTableSlice0_priorityEn_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));

                CTC_BIT_UNSET(field_val, old_pfc_class);

                cmd = DRV_IOW(tbl_id, PfcPriorityEnTableSlice0_priorityEn_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));
                /*
                CTC_ERROR_RETURN(sys_goldengate_qos_flow_ctl_profile(lchip, gport,
                                                                 fc_prop->priority_class,
                                                                 fc_prop->is_pfc,
                                                                 1));
                 */

            }

            if (old_pfc_class_bmp && fc_prop->is_pfc && !fc_prop->enable)
            {
                fc_prop->pfc_class = old_pfc_class;
            }

            field_val = (1 << fc_prop->pfc_class);
            cmd = DRV_IOW(DsIgrPortTcPriMap_t, DsIgrPortTcPriMap_pfcPriBmp0_f + fc_prop->priority_class);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
        }

        /*change to xon/xoff profile*/
        CTC_ERROR_RETURN(sys_goldengate_qos_flow_ctl_profile(lchip, gport,
                                                             fc_prop->priority_class,
                                                             fc_prop->pfc_class,
                                                             fc_prop->is_pfc,
                                                             fc_prop->enable));


        tbl_id = NetPauseRecordSlice0_t + slice_id;
        if (!fc_prop->enable)
        {
            if (fc_prop->is_pfc)
            {
                cmd = DRV_IOR(tbl_id, NetPauseRecordSlice0_pfcStall_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));

                while (CTC_IS_BIT_SET(field_val, fc_prop->pfc_class))
                {
                    sal_task_sleep(1);
                    if ((index++) > 50)
                    {
                        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Cannot clear stall status\n");
                        return CTC_E_HW_FAIL;
                    }
                    cmd = DRV_IOR(tbl_id, NetPauseRecordSlice0_pfcStall_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));
                }
            }
            else
            {
                cmd = DRV_IOR(tbl_id, NetPauseRecordSlice0_normalStall_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));

                while (field_val)
                {
                    sal_task_sleep(1);
                    if ((index++) > 50)
                    {
                        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Cannot clear stall status\n");
                        return CTC_E_HW_FAIL;
                    }
                    cmd = DRV_IOR(tbl_id, NetPauseRecordSlice0_normalStall_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));
                }
            }
        }

        sal_memset(&pfc_chan_en, 0, sizeof(pfc_chan_en));
        cmd = DRV_IOR(BufStorePfcChanEn_t , DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pfc_chan_en));

        /*Rx port pfc enable*/
        if (fc_prop->is_pfc)
        {
            GetBufStorePfcChanEn(A, pfcChanEnSlice0_f + slice_id, &pfc_chan_en, chan_en_bitmap);
            CTC_BIT_SET(chan_en_bitmap[lchan_id >> 5], (lchan_id&0x1F));
            SetBufStorePfcChanEn(A, pfcChanEnSlice0_f + slice_id, &pfc_chan_en, chan_en_bitmap);
        }
        else
        {
            GetBufStorePfcChanEn(A, pfcChanEnSlice0_f + slice_id, &pfc_chan_en, chan_en_bitmap);
            CTC_BIT_UNSET(chan_en_bitmap[lchan_id >> 5], (lchan_id&0x1F));
            SetBufStorePfcChanEn(A, pfcChanEnSlice0_f + slice_id, &pfc_chan_en, chan_en_bitmap);
        }

        cmd = DRV_IOW(BufStorePfcChanEn_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &pfc_chan_en));

        tbl_id = PfcPriorityEnTableSlice0_t + slice_id;
        cmd = DRV_IOR(tbl_id, PfcPriorityEnTableSlice0_priorityEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));
        fc_prop->pfc_class = (fc_prop->is_pfc)?fc_prop->pfc_class:0;

        if (fc_prop->enable)
        {
            CTC_BIT_SET(field_val, fc_prop->pfc_class);
        }
        else
        {
            CTC_BIT_UNSET(field_val, fc_prop->pfc_class);
        }

        cmd = DRV_IOW(tbl_id, PfcPriorityEnTableSlice0_priorityEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));

        tbl_id = NetTxMiscCtl0_t + slice_id;
        field_val = 1;
        cmd = DRV_IOW(tbl_id, NetTxMiscCtl0_pauseEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop)
{
    int32 ret = CTC_E_NONE;

    uint16 gport                    = 0;
    uint8 dir                       = 0;

    uint16 lport                    = 0;
    uint8 chan_id                   = 0;
    uint8 lchan_id                  = 0;
    uint8 mac_id                    = 0;
    uint8 lmac_id                   = 0;
    uint8 slice_id                  = 0;
    uint32 cmd                      = 0;
    uint32 tbl_id                   = 0;
    uint32 field_val                = 0;
    uint32 rx_pause_en              = 0;
    ds_t ds;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint32 stall_en_bitmap[2];

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X \n", fc_prop->gport);

    gport = fc_prop->gport;
    dir = fc_prop->dir;
    CTC_MAX_VALUE_CHECK(fc_prop->priority_class, 7);
    CTC_MAX_VALUE_CHECK(fc_prop->pfc_class, 7);

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    chan_id = ((slice_id << 6)|port_attr->chan_id);
    mac_id  = port_attr->mac_id;
    lchan_id = chan_id&0x3F;
    lmac_id =  mac_id&0x3F;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chan_id:%d, lmac_id:%d, slice_id:%d\n", chan_id, lmac_id, slice_id);

    if (CTC_BOTH_DIRECTION == dir)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_INGRESS == dir)
    {
        sal_memset(ds, 0, sizeof(ds));
        tbl_id = DsChannelizeMode0_t + slice_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, ds));


        if (fc_prop->is_pfc)
        {
            rx_pause_en = GetDsChannelizeMode0(V, localPhyPortBase_f, ds);
            fc_prop->enable = CTC_IS_BIT_SET(rx_pause_en, (fc_prop->priority_class));
        }
        else
        {
            sal_memset(ds, 0, sizeof(ds));
            tbl_id = NetTxNetRxStallEnBmp0_t + slice_id;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
            GetNetTxNetRxStallEnBmp0(A, netRxStallEnBmp_f, ds, stall_en_bitmap);
            fc_prop->enable = CTC_IS_BIT_SET(stall_en_bitmap[lmac_id >> 5], (lmac_id&0x1F));
        }
    }

    /* egress only global contral */
    if (CTC_EGRESS == dir)
    {
        tbl_id = PfcPriorityEnTableSlice0_t + slice_id;
        cmd = DRV_IOR(tbl_id, PfcPriorityEnTableSlice0_priorityEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lchan_id, cmd, &field_val));
        fc_prop->pfc_class = (fc_prop->is_pfc)?fc_prop->pfc_class:0;

        fc_prop->enable = CTC_IS_BIT_SET(field_val, fc_prop->pfc_class);
    }

    return ret;
}

STATIC int32
_sys_goldengate_port_get_mac_tbl(uint8 lchip, uint16 lport, uint32* p_tbl_id)
{
    int8 step = 0;
    uint8 slice_id = 0;
    sys_datapath_lport_attr_t* p_port_cap = NULL;
    uint16 drv_port = 0;
    uint16 mac_id = 0;

    drv_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);
    slice_id = SYS_MAP_DRV_LPORT_TO_SLICE(drv_port);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &p_port_cap));
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(p_port_cap->mac_id > 39)
    {
        mac_id = p_port_cap->mac_id - 8;
    }
    else
    {
        mac_id = p_port_cap->mac_id;
    }

    if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
        ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
    {
        step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
        *p_tbl_id = XqmacSgmac0Cfg0_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
    }
    else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
        || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
    {
        *p_tbl_id = XqmacXlgmacCfg0_t + (mac_id+slice_id*48)/4;
    }
    else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
    {
        *p_tbl_id = CgmacCfg0_t + slice_id*2 + ((mac_id == 36)?0:1);
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac_id: %u, *p_tbl_id: %u\n", mac_id, *p_tbl_id);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_mac_field(uint8 lchip, uint16 lport, sys_port_mac_property_t property, uint32* p_filed_id)
{
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    /* get field ID */
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &p_port_cap));
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    switch (property)
    {
        case SYS_PORT_MAC_PROPERTY_PADDING:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_padEnable0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_padEnable_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = CgmacCfg0_padEnable_f;
            }
            break;

        case SYS_PORT_MAC_PROPERTY_PREAMBLE:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_preamble4Bytes0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_preamble4Bytes_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = XqmacXlgmacCfg0_preamble4Bytes_f;
            }
            break;
        case SYS_PORT_MAC_PROPERTY_CHKCRC:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_crcChkEn0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_crcChkEn_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = CgmacCfg0_crcChkEn_f;
            }
            break;
        case SYS_PORT_MAC_PROPERTY_STRIPCRC:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_stripCrcEn0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_stripCrcEn_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = CgmacCfg0_stripCrcEn_f;
            }
            break;
        case SYS_PORT_MAC_PROPERTY_APPENDCRC:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_appendCrcEn0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_appendCrcEn_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = CgmacCfg0_appendCrcEn_f;
            }
            break;
        case SYS_PORT_MAC_PROPERTY_ENTXERR:
            if ((CTC_CHIP_SERDES_SGMII_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XFI_MODE == p_port_cap->pcs_mode)
            ||(CTC_CHIP_SERDES_2DOT5G_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacSgmac0Cfg0_errorMaskEn0_f;
            }
            else if ((CTC_CHIP_SERDES_XLG_MODE == p_port_cap->pcs_mode) || (CTC_CHIP_SERDES_XAUI_MODE == p_port_cap->pcs_mode)
                || (CTC_CHIP_SERDES_DXAUI_MODE == p_port_cap->pcs_mode))
            {
                *p_filed_id = XqmacXlgmacCfg0_errorMaskEn_f;
            }
            else if (CTC_CHIP_SERDES_CG_MODE == p_port_cap->pcs_mode)
            {
                *p_filed_id = CgmacCfg0_errorMaskEn_f;
            }
            break;
        default:
            break;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_mac_set_property(uint8 lchip, uint16 gport, sys_port_mac_property_t property, uint32 value)
{

    uint16 lport = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "gport %u, value %u\n", gport, value);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (SYS_PORT_MAC_PROPERTY_PREAMBLE == property)
    {
        CTC_MAX_VALUE_CHECK(value, SYS_PORT_PREAMBLE_TYPE_NUM - 1);
    }

    CTC_ERROR_RETURN(_sys_goldengate_port_get_mac_tbl(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &tbl_id));
    CTC_ERROR_RETURN(_sys_goldengate_port_get_mac_field(lchip, CTC_MAP_GPORT_TO_LPORT(gport), property, &field_id));
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tbl_id %u, field_id %u\n", tbl_id, field_id);

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_mac_get_property(uint8 lchip, uint16 gport, sys_port_mac_property_t property, uint32* p_value)
{

    uint16 lport = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    SYS_PORT_INIT_CHECK(lchip);
    switch(property)
    {
        case SYS_PORT_MAC_PROPERTY_PADDING:
        case SYS_PORT_MAC_PROPERTY_PREAMBLE:
        case SYS_PORT_MAC_PROPERTY_CHKCRC:
        case SYS_PORT_MAC_PROPERTY_STRIPCRC:
        case SYS_PORT_MAC_PROPERTY_APPENDCRC:
        case SYS_PORT_MAC_PROPERTY_ENTXERR:
            SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
            CTC_ERROR_RETURN(_sys_goldengate_port_get_mac_tbl(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &tbl_id));
            CTC_ERROR_RETURN(_sys_goldengate_port_get_mac_field(lchip, CTC_MAP_GPORT_TO_LPORT(gport), property, &field_id));

            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, p_value));
            break;

        case SYS_PORT_MAC_PROPERTY_CODEERR:
            CTC_ERROR_RETURN(sys_goldengate_port_get_mac_code_err(lchip, gport, p_value));
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_max_frame(uint8 lchip, uint16 gport, uint32 value)
{
    CTC_ERROR_RETURN(_sys_goldengate_port_set_framesize(lchip, gport, value, SYS_PORT_FRAMESIZE_TYPE_MAX));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_max_frame(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 value = 0;

    CTC_ERROR_RETURN(_sys_goldengate_port_get_framesize(lchip, gport, &value, SYS_PORT_FRAMESIZE_TYPE_MAX));
    *p_value = value;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_min_frame_size(uint8 lchip, uint16 gport, uint8 size)
{
    CTC_ERROR_RETURN(_sys_goldengate_port_set_framesize(lchip, gport, size, SYS_PORT_FRAMESIZE_TYPE_MIN));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_min_frame_size(uint8 lchip, uint16 gport, uint8* p_size)
{
    uint16 size = 0;

    CTC_ERROR_RETURN(_sys_goldengate_port_get_framesize(lchip, gport, &size, SYS_PORT_FRAMESIZE_TYPE_MIN));
    *p_size = size;

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_maping_to_local_phy_port(uint8 lchip, uint16 gport, uint16 local_phy_port)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = local_phy_port;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, PORT_CASECADE_INDEX(lport, IpeHeaderAdjustPhyPortMap_t), cmd, &field_value));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_lbk_port_property(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk, uint16 inter_lport, uint16 dsfwd_offset)
{

    uint8  enable = p_port_lbk->lbk_enable ? 1 : 0;
    uint16 src_lport = 0;
    uint16 inter_dlport = 0;
    uint16 src_gport = 0;
    uint32 cmd = 0;
    DsPhyPortExt_m inter_phy_port_ext;
    DsSrcPort_m inter_src_port;
    DsDestPort_m inter_dest_port;

    CTC_PTR_VALID_CHECK(p_port_lbk);

    src_gport = p_port_lbk->src_gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(src_gport, lchip, src_lport);
    inter_dlport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(inter_lport);

    sal_memset(&inter_src_port,     0, sizeof(DsSrcPort_m));
    sal_memset(&inter_dest_port,    0, sizeof(DsDestPort_m));
    sal_memset(&inter_phy_port_ext, 0, sizeof(DsPhyPortExt_m));

    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_src_port));

    SetDsSrcPort(V, portCrossConnect_f, &inter_src_port, (enable ? 1 : 0));
    SetDsSrcPort(V, receiveDisable_f, &inter_src_port, 0);
    SetDsSrcPort(V, stpDisable_f, &inter_src_port, (enable ? 1 : 0));
    SetDsSrcPort(V, addDefaultVlanDisable_f, &inter_src_port, (enable ? 1 : 0));
    SetDsSrcPort(V, routeDisable_f, &inter_src_port, (enable ? 1 : 0));
    SetDsSrcPort(V, learningDisable_f, &inter_src_port, (enable ? 1 : 0));
    SetDsSrcPort(V, ingressFilteringMode_f, &inter_src_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);

    /*l2pdu same to src port*/
    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_phy_port_ext));

    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_dest_port));

    SetDsPhyPortExt(V, u1_g2_dsFwdPtr_f, &inter_phy_port_ext, dsfwd_offset);

    if (enable)
    {
        SetDsSrcPort(V, bridgeEn_f, &inter_src_port, 0);
        SetDsPhyPortExt(V, defaultVlanId_f, &inter_phy_port_ext, 0);
        SetDsDestPort(V, stpCheckEn_f, &inter_dest_port, 0);
        if ((p_port_lbk->src_gport == p_port_lbk->dst_gport) && p_port_lbk->lbk_mode)
        {
            SetDsPhyPortExt(V, efmLoopbackEn_f, &inter_phy_port_ext, 1);
            SetDsDestPort(V, discardNon8023Oam_f, &inter_dest_port, 1);
        }
    }
    else
    {
        SetDsSrcPort(V, bridgeEn_f, &inter_src_port, 1);
        SetDsPhyPortExt(V, exception2En_f, &inter_phy_port_ext, 0);
        SetDsPhyPortExt(V, exception2Discard_f, &inter_phy_port_ext, 0);
        SetDsPhyPortExt(V, efmLoopbackEn_f, &inter_phy_port_ext, 0);
        SetDsPhyPortExt(V, defaultVlanId_f, &inter_phy_port_ext, 1);
        SetDsDestPort(V, discardNon8023Oam_f, &inter_dest_port, 0);
        SetDsDestPort(V, reflectiveBridgeEn_f, &inter_dest_port, (enable ? 1 : 0));
        SetDsDestPort(V, egressFilteringMode_f, &inter_dest_port, SYS_PORT_VLAN_FILTER_MODE_VLAN);
        SetDsDestPort(V, stpCheckEn_f, &inter_dest_port, 1);
    }

    cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_src_port));

    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_phy_port_ext));

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_dlport, cmd, &inter_dest_port));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_cross_connect(uint8 lchip, uint16 cc_gport, uint32 nhid)
{

    uint16  lport = 0;
    uint32  cmd = 0;
    uint32  field_val = 0;
    bool enable = FALSE;
    uint32 dsfwd_offset = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(cc_gport, lchip, lport);

    /* when efm loopback enabled on src port, cc should not be enabled.*/
    if (p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(cc_gport)].lbk_en)
    {
        return CTC_E_PORT_HAS_OTHER_FEATURE;
    }


    if (SYS_NH_INVALID_NHID != nhid)
    {
        CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, nhid, &dsfwd_offset));
        enable = TRUE;
    }

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_portCrossConnect_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));


    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_routedPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_learningDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = dsfwd_offset;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_u1_g2_dsFwdPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(cc_gport)].nhid = nhid;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_cross_connect(uint8 lchip, uint16 cc_gport, uint32* p_value)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 bridge_en = 0;
    uint32 port_cross_connect = 0;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(cc_gport, lchip, lport);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_bridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &bridge_en));

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portCrossConnect_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &port_cross_connect));

    if ((0 == bridge_en) && (1 == port_cross_connect))
    {
        *p_value = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(cc_gport)].nhid;
    }
    else
    {
        *p_value = SYS_NH_INVALID_NHID;
    }

    return CTC_E_NONE;
}

/* set mac TAP session */
STATIC int32
_sys_goldengate_port_set_mac_tap(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk)
{
    uint16 src_lport     = 0;
    uint16 dst_lport     = 0;
    uint32 cmd           = 0;
    uint32 tbl_id        = 0;
    uint32 mac_bm[2]     = {0};
    uint32 cxqm_bm       = 0;
    uint16 gport_base    = 0;
    uint8  cnt           = 0;
    NetRxSpanCtl0_m tap_src;
    NetTxNetRxSpanPortMap0_m tap_session;
    NetTxSpanCtl0_m tap_dst;
    sys_datapath_lport_attr_t* src_port_attr = NULL;
    sys_datapath_lport_attr_t* dst_port_attr = NULL;

    /* sanity check and debug */
    CTC_PTR_VALID_CHECK(p_port_lbk);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);

    src_lport = CTC_MAP_GPORT_TO_LPORT(p_port_lbk->src_gport);
    dst_lport = CTC_MAP_GPORT_TO_LPORT(p_port_lbk->dst_gport);

    /* get lport attribute */
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, src_lport, &src_port_attr));
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, dst_lport, &dst_port_attr));
    if ((SYS_DATAPATH_NETWORK_PORT != dst_port_attr->port_type)||(SYS_DATAPATH_NETWORK_PORT != src_port_attr->port_type))
    {
        return CTC_E_MAC_NOT_USED;
    }

    /* 1. check for chip limit */
    /* src and dst mac should in one slice */
    if(src_port_attr->slice_id != dst_port_attr->slice_id)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* src and dst mac should not in the same group */
    if((src_port_attr->mac_id <= 15) && (dst_port_attr->mac_id <= 15))
    {
        return CTC_E_INVALID_PARAM;
    }
    else if(((src_port_attr->mac_id >= 16) && (src_port_attr->mac_id <= 31))
            &&((dst_port_attr->mac_id >= 16) && (dst_port_attr->mac_id <= 31)))
    {
        return CTC_E_INVALID_PARAM;
    }
    else if(((src_port_attr->mac_id >= 32) && (src_port_attr->mac_id <= 39))
            &&((dst_port_attr->mac_id >= 32) && (dst_port_attr->mac_id <= 39)))
    {
        return CTC_E_INVALID_PARAM;
    }
    else if(((src_port_attr->mac_id >= 48) && (src_port_attr->mac_id <= 55))
            &&((dst_port_attr->mac_id >= 48) && (dst_port_attr->mac_id <= 55)))
    {
        return CTC_E_INVALID_PARAM;
    }

    /* src and dst mac should have the same speed mode */
    if((src_port_attr->pcs_mode != dst_port_attr->pcs_mode) ||(src_port_attr->speed_mode != dst_port_attr->speed_mode))
    {
        return CTC_E_INVALID_PARAM;
    }

    /* 2. cfg drop, divide dest-gport into groups by mac group, and port-disable */
    if(p_port_lbk->lbk_enable)
    {
        /* if port is not tap dest, disable it */
        if(dst_port_attr->mac_id <= 15)
        {
            gport_base = (dst_port_attr->slice_id)?0x8000:0x0000;
            for(cnt=0; cnt<16; cnt++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 0));
            }
        }
        else if((dst_port_attr->mac_id >= 16) && (dst_port_attr->mac_id <= 31))
        {
            gport_base = (dst_port_attr->slice_id)?0x8010:0x0010;
            for(cnt=0; cnt<16; cnt++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 0));
            }
        }
        else if((dst_port_attr->mac_id >= 32) && (dst_port_attr->mac_id <= 39))
        {
            gport_base = (dst_port_attr->slice_id)?0x8020:0x0020;
            for(cnt=0; cnt<8; cnt++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 0));
            }
        }
        else if((dst_port_attr->mac_id >= 48) && (dst_port_attr->mac_id <= 55))
        {
            gport_base = (dst_port_attr->slice_id)?0x8028:0x0028;
            for(cnt=0; cnt<8; cnt++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 0));
            }
        }
    }

    /* 3. cfg TAP session */
    /* cfg src */
    tbl_id = NetRxSpanCtl0_t + src_port_attr->slice_id;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tap_src));
    GetNetRxSpanCtl0(A, cfgSpanEn_f, &tap_src, mac_bm);
    if(src_port_attr->mac_id < 32)
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(mac_bm[0], src_port_attr->mac_id);
        }
        else
        {
            CTC_BIT_UNSET(mac_bm[0], src_port_attr->mac_id);
        }
    }
    else
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(mac_bm[1], (src_port_attr->mac_id-32));
        }
        else
        {
            CTC_BIT_UNSET(mac_bm[1], (src_port_attr->mac_id-32));
        }
    }
    SetNetRxSpanCtl0(A, cfgSpanEn_f, &tap_src, mac_bm);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tap_src));

    /* mapping src to dest */
    sal_memset(&tap_session, 0, sizeof(NetTxNetRxSpanPortMap0_m));
    tbl_id = NetTxNetRxSpanPortMap0_t + src_port_attr->slice_id;
    SetNetTxNetRxSpanPortMap0(V, portId_f, &tap_session, (p_port_lbk->lbk_enable)?dst_port_attr->mac_id : 0);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_port_attr->mac_id, cmd, &tap_session));

    /* cfg dest */
    tbl_id = NetTxSpanCtl0_t + dst_port_attr->slice_id;
    sal_memset(&tap_dst, 0, sizeof(NetTxSpanCtl0_m));
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tap_dst));
    GetNetTxSpanCtl0(A, cfgCxqmSpanEn_f, &tap_dst, &cxqm_bm);
    if(dst_port_attr->mac_id <= 15)
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(cxqm_bm, 0);
            CTC_BIT_SET(p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id);
        }
        else
        {
            CTC_BIT_UNSET(p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id);
            if(0 == (p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)]&0xffff))
            {
                CTC_BIT_UNSET(cxqm_bm, 0);
            }
        }
    }
    else if((dst_port_attr->mac_id >= 16) && (dst_port_attr->mac_id <= 31))
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(cxqm_bm, 1);
            CTC_BIT_SET(p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id);
        }
        else
        {
            CTC_BIT_UNSET(p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id);
            if(0 == (p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)]&0xffff0000))
            {
               CTC_BIT_UNSET(cxqm_bm, 1);
            }
        }
    }
    else if((dst_port_attr->mac_id >= 32) && (dst_port_attr->mac_id <= 39))
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(cxqm_bm, 2);
            CTC_BIT_SET(p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id>>5);
        }
        else
        {
            CTC_BIT_UNSET(p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id>>5);
            if(0 == (p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)]&0x00ff))
            {
                CTC_BIT_UNSET(cxqm_bm, 2);
            }
        }
    }
    else if((dst_port_attr->mac_id >= 48) && (dst_port_attr->mac_id <= 55))
    {
        if(p_port_lbk->lbk_enable)
        {
            CTC_BIT_SET(cxqm_bm, 3);
            CTC_BIT_SET(p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id>>5);
        }
        else
        {
            CTC_BIT_UNSET(p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)], dst_port_attr->mac_id>>5);
            if(0 == (p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)]&0xff0000))
            {
                CTC_BIT_UNSET(cxqm_bm, 3);
            }
        }
    }
    SetNetTxSpanCtl0(V, cfgCxqmSpanEn_f, &tap_dst, cxqm_bm);
    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tap_dst));

    /* disable drop */
    if(0 == (p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)]&0xffff))
    {
        gport_base = (dst_port_attr->slice_id)?0x8000:0x0000;
        for(cnt=0; cnt<16; cnt++)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 1));
        }
    }
    if(0 == (p_gg_port_master[lchip]->mac_bmp[0+(2*dst_port_attr->slice_id)]&0xffff0000))
    {
        gport_base = (dst_port_attr->slice_id)?0x8010:0x0010;
        for(cnt=0; cnt<16; cnt++)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 1));
        }
    }
    if(0 == (p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)]&0x00ff))
    {
        gport_base = (dst_port_attr->slice_id)?0x8020:0x0020;
        for(cnt=0; cnt<8; cnt++)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 1));
        }
    }
    if(0 == (p_gg_port_master[lchip]->mac_bmp[1+(2*dst_port_attr->slice_id)]&0xff0000))
    {
        gport_base = (dst_port_attr->slice_id)?0x8028:0x0028;
        for(cnt=0; cnt<8; cnt++)
        {
            CTC_ERROR_RETURN(_sys_goldengate_port_set_port_en(lchip, (gport_base+cnt), 1));
        }
    }

    return CTC_E_NONE;
}

/* efm and crossconnect loopback */
STATIC int32
_sys_goldengate_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk)
{
    uint16  sys_internal_gport = 0;
    uint16  sys_internal_lport = 0;
    uint16  drv_internal_lport = 0;
    uint16  drv_src_lport = 0;
    uint16  sys_src_gport   = 0;
    uint16  sys_dst_gport   = 0;
    uint32  dsfwd_offset = 0;
    uint32  nhid        = 0;
    uint8   gchip       = 0;
    uint8   chan        = 0;
    uint8 enq_mode = 0;
    uint16 max_extend_num = 0;

    int32  ret         = CTC_E_NONE;
    ctc_misc_nh_param_t nh_param;
    ctc_internal_port_assign_para_t port_assign;

#define RET_PROCESS_WITH_ERROR(func) ret = ret ? ret : (func)

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_lbk);
    CTC_GLOBAL_PORT_CHECK(p_port_lbk->src_gport);
    CTC_GLOBAL_PORT_CHECK(p_port_lbk->dst_gport);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sys_src_gport = p_port_lbk->src_gport;
    sys_dst_gport = p_port_lbk->dst_gport;

    /*not support linkagg*/
    if (CTC_IS_LINKAGG_PORT(sys_src_gport))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(sys_src_gport, lchip, drv_src_lport);

    gchip = CTC_MAP_GPORT_TO_GCHIP(sys_src_gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);
    sal_memset(&nh_param,   0, sizeof(ctc_misc_nh_param_t));
    sal_memset(&port_assign,    0, sizeof(ctc_internal_port_assign_para_t));


    CTC_ERROR_RETURN(_sys_goldengate_port_get_cross_connect(lchip, sys_src_gport, &nhid));

    /* cc is enabled on src port */
    if (SYS_NH_INVALID_NHID != nhid)
    {
        return CTC_E_PORT_HAS_OTHER_FEATURE;
    }

    nhid = 0;
    CTC_ERROR_RETURN(sys_goldengate_queue_get_enqueue_info(lchip, &max_extend_num, &enq_mode));
    if (p_port_lbk->lbk_enable)
    {
        if (p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].lbk_en)
        {
            return CTC_E_ENTRY_EXIST;
        }

        /* allocate internal port */
        port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
        port_assign.gchip       = gchip;
        if (CTC_PORT_LBK_MODE_EFM == p_port_lbk->lbk_mode)
        {
            port_assign.fwd_gport       = sys_src_gport;
        }
        else if(CTC_PORT_LBK_MODE_CC == p_port_lbk->lbk_mode)
        {
            port_assign.fwd_gport       = sys_dst_gport;
        }

        CTC_ERROR_RETURN(sys_goldengate_internal_port_allocate(lchip, &port_assign));

        sys_internal_lport = port_assign.inter_port;

        sys_internal_gport = CTC_MAP_LPORT_TO_GPORT(gchip, sys_internal_lport);

        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "source gport:0x%04X, dest gport:0x%04X, internal lport = %u!\n",\
                                               sys_src_gport, sys_dst_gport, sys_internal_lport);

        /* map network port to internal port */
        drv_internal_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(sys_internal_lport);
        RET_PROCESS_WITH_ERROR(_sys_goldengate_port_maping_to_local_phy_port(lchip, sys_src_gport, drv_internal_lport));

        if (CTC_PORT_LBK_MODE_EFM == p_port_lbk->lbk_mode)
        {
            /*efm loopback*/
            uint8 chan = SYS_GET_CHANNEL_ID(lchip, sys_src_gport);
            RET_PROCESS_WITH_ERROR(sys_goldengate_add_port_to_channel(lchip, drv_internal_lport, chan));
            RET_PROCESS_WITH_ERROR(sys_goldengate_add_port_to_channel(lchip, drv_src_lport, SYS_DROP_CHANNEL_ID));
            /*get reflect nexthop*/
            RET_PROCESS_WITH_ERROR(sys_goldengate_nh_get_reflective_dsfwd_offset(lchip, &dsfwd_offset));
        }
        else if(CTC_PORT_LBK_MODE_CC == p_port_lbk->lbk_mode)
        {
            /*
            if (src_gport == dst_gport)
            {
                RET_PROCESS_WITH_ERROR(sys_goldengate_add_port_to_channel(lchip, inter_gport, src_lport));
                RET_PROCESS_WITH_ERROR(sys_goldengate_add_port_to_channel(lchip, src_gport, SYS_DROP_CHANNEL_ID));
            }
            */

            if (CTC_PORT_LBK_TYPE_SWAP_MAC == p_port_lbk->lbk_type)
            {
                nh_param.type           = CTC_MISC_NH_TYPE_FLEX_EDIT_HDR;
                nh_param.gport          = sys_dst_gport;
                nh_param.dsnh_offset    = 0;

                nh_param.misc_param.l2edit.is_reflective    = 0;
                nh_param.misc_param.l2edit.flag             |= CTC_MISC_NH_FLEX_EDIT_REPLACE_L2_HDR;
                nh_param.misc_param.l2edit.flag             |= CTC_MISC_NH_FLEX_EDIT_SWAP_MACDA;

                sys_goldengate_nh_add_misc(lchip, &nhid, &nh_param, TRUE);
                sys_goldengate_nh_get_dsfwd_offset(lchip, nhid, &dsfwd_offset);

            }
            else if (CTC_PORT_LBK_TYPE_BYPASS == p_port_lbk->lbk_type)
            {
                uint32 bypass_nhid    = 0;
                sys_goldengate_l2_get_ucast_nh(lchip, sys_dst_gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS, &bypass_nhid);

                sys_goldengate_nh_get_dsfwd_offset(lchip, bypass_nhid, &dsfwd_offset);

            }
        }

        /*set lbk port property*/
        ret = sys_goldengate_port_set_lbk_port_property(lchip, p_port_lbk, sys_internal_lport, dsfwd_offset);

        if (CTC_E_NONE != ret)
        {
            if (CTC_PORT_LBK_MODE_EFM == p_port_lbk->lbk_mode)
            {
                /*resverv channel*/
                chan = SYS_GET_CHANNEL_ID(lchip, sys_src_gport);
                RET_PROCESS_WITH_ERROR(sys_goldengate_remove_port_from_channel(lchip, drv_internal_lport, chan));
                RET_PROCESS_WITH_ERROR(sys_goldengate_remove_port_from_channel(lchip, drv_src_lport, SYS_DROP_CHANNEL_ID));
                RET_PROCESS_WITH_ERROR(sys_goldengate_add_port_to_channel(lchip, drv_src_lport, chan));
            }
            else if(CTC_PORT_LBK_MODE_CC == p_port_lbk->lbk_mode)
            { /*release nexthop*/
                if (nhid)
                {
                    sys_goldengate_nh_remove_misc(lchip, nhid);
                }
            }

            /* Release internal port */
            port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
            port_assign.gchip       = gchip;
            sys_internal_lport = SYS_MAP_DRV_LPORT_TO_SYS_LPORT((SYS_MAP_CTC_GPORT_TO_DRV_LPORT(port_assign.inter_port) & 0xFF));
            port_assign.inter_port  = sys_internal_lport;
            CTC_ERROR_RETURN(sys_goldengate_internal_port_release(lchip, &port_assign));

            return ret;
        }

        p_port_lbk->lbk_gport = sys_internal_gport;

        /*save the port*/
        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].inter_lport = sys_internal_lport;
        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].lbk_en   = 1;
        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].nhid     = nhid;
    }
    else
    {
        if (!p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].lbk_en)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }

        p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].lbk_en = 0;
        sys_internal_lport = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].inter_lport;
        nhid        = p_gg_port_master[lchip]->igs_port_prop[CTC_MAP_GPORT_TO_LPORT(sys_src_gport)].nhid;

        drv_internal_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(sys_internal_lport);
        CTC_ERROR_RETURN(_sys_goldengate_port_maping_to_local_phy_port(lchip, sys_src_gport, drv_src_lport));
        sys_internal_gport = CTC_MAP_LPORT_TO_GPORT(gchip, sys_internal_lport);
        CTC_ERROR_RETURN(sys_goldengate_port_set_lbk_port_property(lchip, p_port_lbk, sys_internal_lport, 0));

        /* valid internal port not suppose to be 0 */
        if (sys_internal_lport)
        {
            sys_internal_gport = CTC_MAP_LPORT_TO_GPORT(gchip, sys_internal_lport);

            if (CTC_PORT_LBK_MODE_EFM == p_port_lbk->lbk_mode)
            {
                chan = SYS_GET_CHANNEL_ID(lchip, sys_src_gport);
                CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel(lchip, drv_internal_lport, chan));
                CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel(lchip, drv_internal_lport, SYS_DROP_CHANNEL_ID));
                CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, drv_src_lport, chan));
            }
            else if(CTC_PORT_LBK_MODE_CC == p_port_lbk->lbk_mode)
            {
                /*
                if (src_gport == dst_gport)
                {
                    CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel(lchip, inter_gport, src_lport));
                    CTC_ERROR_RETURN(sys_goldengate_remove_port_from_channel(lchip, src_gport, SYS_DROP_CHANNEL_ID));
                    CTC_ERROR_RETURN(sys_goldengate_add_port_to_channel(lchip, src_gport, src_lport));
                }
                */
                /*release nexthop*/
                if (nhid)
                {
                    sys_goldengate_nh_remove_misc(lchip, nhid);
                }
            }

            /* Release internal port */
            port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
            port_assign.gchip       = gchip;
            sys_internal_lport = SYS_MAP_DRV_LPORT_TO_SYS_LPORT((SYS_MAP_SYS_LPORT_TO_DRV_LPORT(sys_internal_lport) & 0xFF));
            port_assign.inter_port  = sys_internal_lport;

            CTC_ERROR_RETURN(sys_goldengate_internal_port_release(lchip, &port_assign));
        }
    }

    return CTC_E_NONE;
}

/* Port support 3 loopback modes, efm loopback crossconnect loopback and TAP. Note, when TAP, chip divide mac into groups, */
/* if use one mac as TAP dest, all mac included in the group which has the TAP dest won't do normal forwarding. */
/* Src_mac and dst_mac should have the same speed mode, src_mac and dest_mac should one-to-one mapping. */
/* Meanwhile TAP src_mac dst_mac should both in the same slice. */
/* Slice0:                                              Slice1:                                                       */
/* | GROUP |  Txqm0  |  Txqm1  |  Cxqm0  |  Cxqm1  |    | GROUP |  Txqm0  |  Txqm1  |  Cxqm0  |  Cxqm1  |    */
/* |-------|---------|---------|---------|---------|    |-------|---------|---------|---------|---------|    */
/* |  MAC  |   0~15  |  16~31  |  32~39  |  48~55  |    |  MAC  |   0~15  |  16~31  |  32~39  |  48~55  |    */

int32
sys_goldengate_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk)
{
    /* sanity check */
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_lbk);

    /* debug */
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"src_gport%d, dst_gport:%d, lbk_enable:%d, lbk_mode:%d\n",p_port_lbk->src_gport,
                            p_port_lbk->dst_gport, p_port_lbk->lbk_enable, p_port_lbk->lbk_mode);

    PORT_LOCK;
    /* set loopback by lbk_mode */
    if(CTC_PORT_LBK_MODE_TAP == p_port_lbk->lbk_mode)
    {
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_mac_tap(lchip, p_port_lbk));
    }
    else  /* efm and crossconnect loopback */
    {
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_loopback(lchip, p_port_lbk));
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_default_vlan(uint8 lchip, uint16 gport, uint32 value)
{

    uint16  lport = 0;
    uint32  cmd = 0;
    uint32  default_vlan_id = value;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, default vlan:%d \n", gport, value);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &default_vlan_id));

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_defaultVlanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &default_vlan_id));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_raw_packet_type(uint8 lchip, uint16 gport, uint32 value)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    DsPhyPort_m ds_phy_port;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:0x%04X, packet type:%d \n", gport, value);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(value, CTC_PORT_RAW_PKT_IPV6);

    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));

    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port));

    if (CTC_PORT_RAW_PKT_NONE == value)
    {
        SetDsPhyPort(V, packetTypeValid_f, &ds_phy_port, 0);
    }
    else
    {
        SetDsPhyPort(V, packetTypeValid_f, &ds_phy_port, 1);

        switch (value)
        {
        case CTC_PORT_RAW_PKT_ETHERNET:
            SetDsPhyPort(V, packetType_f, &ds_phy_port, 0);
            break;

        case CTC_PORT_RAW_PKT_IPV4:
            SetDsPhyPort(V, packetType_f, &ds_phy_port, 1);
            break;

        case CTC_PORT_RAW_PKT_IPV6:
            SetDsPhyPort(V, packetType_f, &ds_phy_port, 3);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_raw_packet_type(uint8 lchip, uint16 gport, uint32* p_value)
{

    uint16  lport = 0;
    uint32  cmd = 0;
    uint32  packet_type_valid = 0;
    uint32  packet_type = 0;
    DsPhyPort_m ds_phy_port;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(p_value);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));
    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port));

    CTC_ERROR_RETURN(GetDsPhyPort(A, packetTypeValid_f, &ds_phy_port, &packet_type_valid));
    CTC_ERROR_RETURN(GetDsPhyPort(A, packetType_f, &ds_phy_port, &packet_type));

    if (0 == packet_type_valid)
    {
        *p_value = CTC_PORT_RAW_PKT_NONE;
    }
    else
    {
        switch (packet_type)
        {
        case 0:
            *p_value = CTC_PORT_RAW_PKT_ETHERNET;
            break;

        case 1:
            *p_value = CTC_PORT_RAW_PKT_IPV4;
            break;

        case 3:
            *p_value = CTC_PORT_RAW_PKT_IPV6;
            break;
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_rx_pause_type(uint8 lchip, uint16 gport, uint32 pasue_type)
{

    uint16 lport = 0;
    int32  step = 0;
    uint32 cmd = 0;
    uint32 tbl_net_rx_pause_ctl[2] = {NetRxPauseCtl0_t, NetRxPauseCtl1_t};
    NetRxPauseCtl0_m net_rx_pause_ctl;
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port_cap));
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    sal_memset(&net_rx_pause_ctl, 0, sizeof(NetRxPauseCtl0_m));
    cmd = DRV_IOR(tbl_net_rx_pause_ctl[p_port_cap->slice_id], DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_ctl));

    step = NetRxPauseCtl0_cfgNormPauseEn1_f - NetRxPauseCtl0_cfgNormPauseEn0_f;
    if (pasue_type & SYS_PORT_PAUSE_FRAME_TYPE_NORMAL)
    {
        SetNetRxPauseCtl0(V, cfgNormPauseEn0_f + (step * ((p_port_cap->mac_id) >> 4)), &net_rx_pause_ctl, 1);
    }

    step = NetRxPauseCtl0_cfgPfcPauseEn1_f - NetRxPauseCtl0_cfgPfcPauseEn0_f;
    if (pasue_type & SYS_PORT_PAUSE_FRAME_TYPE_PFC)
    {
        SetNetRxPauseCtl0(V, cfgPfcPauseEn0_f + (step * ((p_port_cap->mac_id) >> 4)), &net_rx_pause_ctl, 1);
    }

    cmd = DRV_IOW(tbl_net_rx_pause_ctl[p_port_cap->slice_id], DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_ctl));

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_aps_failover_en(uint8 lchip, uint16 gport, uint32* enable)
{
    DsApsChannelMap_m ds_aps_channel_map;
    uint8 channel_id = 0;
    uint32 cmd =0;

    /*uint8 lport = 0;*/

    SYS_PORT_INIT_CHECK(lchip);

    /*lport = CTC_MAP_GPORT_TO_LPORT(gport);*/

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOR(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));

    *enable = GetDsApsChannelMap(V, linkChangeEn_f, &ds_aps_channel_map);

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_linkagg_failover_en(uint8 lchip, uint16 gport, uint32* enable)
{
    DsLinkAggregateChannel_m linkagg_channel;
    uint8 channel_id = 0;
    uint32 cmd =0;

    /*uint8 lport = 0;*/

    SYS_PORT_INIT_CHECK(lchip);

    /*lport = CTC_MAP_GPORT_TO_LPORT(gport);*/

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*get ds_aps_channel_map.link_change_en for scio scan down notify aps model.*/
    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    *enable =  GetDsLinkAggregateChannel(V, linkChangeEn_f, &linkagg_channel);

    return CTC_E_NONE;
}

/* two levels to control failover function, one level to control scan with port, one control group with linkagg/stacking */
int32
_sys_goldengate_port_set_linkagg_failover_en(uint8 lchip, uint16 gport, bool enable)
{

    /*uint16 lport = 0;*/
    uint32 channel_id = 0;
    uint32 cmd =0;
    DsLinkAggregateChannel_m linkagg_channel;

    SYS_PORT_INIT_CHECK(lchip);

    sal_memset(&linkagg_channel, 0, sizeof(DsLinkAggregateChannel_m));

    /*lport = CTC_MAP_GPORT_TO_LPORT(gport);*/

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*set linkagg_channel.link_change_en  for scio scan down notify linkagg model.*/
    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    SetDsLinkAggregateChannel(V, linkChangeEn_f, &linkagg_channel, enable);
    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_aps_failover_en(uint8 lchip, uint16 gport, bool enable)
{
    DsApsChannelMap_m ds_aps_channel_map;
    uint32 channel_id = 0;
    uint32 cmd =0;

    uint16 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);

    /*lport = CTC_MAP_GPORT_TO_LPORT(gport);*/

     /*linkagg port check return.*/
    if(CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_APS_DONT_SUPPORT_HW_BASED_APS_FOR_LINKAGG;
    }

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    /*Set ds_aps_channel_map.link_change_en for scio scan down notify aps model.*/
    cmd = DRV_IOR(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));
    SetDsApsChannelMap(V, linkChangeEn_f, &ds_aps_channel_map, enable);
    SetDsApsChannelMap(V, localPhyPort_f, &ds_aps_channel_map, lport);
    cmd = DRV_IOW(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_aps_select_grp_id(uint8 lchip, uint16 gport, uint32* grp_id)
{
    uint32 cmd = 0;
    uint32 value = 0;

    uint16 lport = 0;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_apsSelectValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    if (0 == value)
    {
        *grp_id = 0xFFFFFFFF;
    }
    else
    {
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_apsSelectGroupId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, grp_id));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_error_check(uint8 lchip, uint16 gport, bool enable)
{
    uint16 lport     = 0;
    uint8 mac_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 step = 0;
    uint32 tbl_id = 0;
    XqmacSgmac0Cfg0_m sgmac_cfg;
    XqmacXlgmacCfg0_m xlgmac_cfg;
    CgmacCfg0_m cgmac_cfg;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    drv_goldengate_get_platform_type(&platform_type);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    value = (enable)?1:0;
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        mac_id = port_attr->mac_id;
        if(mac_id > 39)
        {
            mac_id = mac_id - 8;
        }

        switch (port_attr->pcs_mode)
        {
            case CTC_CHIP_SERDES_SGMII_MODE:
            case CTC_CHIP_SERDES_2DOT5G_MODE:
            case CTC_CHIP_SERDES_XFI_MODE:
                step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
                tbl_id = XqmacSgmac0Cfg0_t + (mac_id+port_attr->slice_id*48) / 4 + ((mac_id+port_attr->slice_id*48)%4)*step;

                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
                DRV_IOW_FIELD(tbl_id, XqmacSgmac0Cfg0_crcChkEn0_f, &value, &sgmac_cfg);
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
                break;

            case CTC_CHIP_SERDES_XLG_MODE:
            case CTC_CHIP_SERDES_XAUI_MODE:
            case CTC_CHIP_SERDES_DXAUI_MODE:
                tbl_id = XqmacXlgmacCfg0_t + (mac_id+port_attr->slice_id*48)/4;
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlgmac_cfg));
                DRV_IOW_FIELD(tbl_id, XqmacXlgmacCfg0_crcChkEn_f, &value, &xlgmac_cfg);
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlgmac_cfg));
                break;

            case CTC_CHIP_SERDES_CG_MODE:
                tbl_id = CgmacCfg0_t + port_attr->slice_id*2 + ((mac_id == 36)?0:1);
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgmac_cfg));
                DRV_IOW_FIELD(tbl_id, CgmacCfg0_crcChkEn_f, &value, &cgmac_cfg);
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgmac_cfg));
                break;

            default:
                break;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_port_mac_set_property(lchip, gport, SYS_PORT_MAC_PROPERTY_STRIPCRC, (enable ? 1 : 0)));
    CTC_ERROR_RETURN(sys_goldengate_port_mac_set_property(lchip, gport, SYS_PORT_MAC_PROPERTY_APPENDCRC, (enable ? 1 : 0)));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_error_check(uint8 lchip, uint16 gport, uint32* enable)
{
    uint16 lport     = 0;
    uint8 mac_id = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 step = 0;
    uint32 tbl_id = 0;
    XqmacSgmac0Cfg0_m sgmac_cfg;
    XqmacXlgmacCfg0_m xlgmac_cfg;
    CgmacCfg0_m cgmac_cfg;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    drv_goldengate_get_platform_type(&platform_type);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        mac_id = port_attr->mac_id;
        if(mac_id > 39)
        {
            mac_id = mac_id - 8;
        }

        switch (port_attr->pcs_mode)
        {
            case CTC_CHIP_SERDES_SGMII_MODE:
            case CTC_CHIP_SERDES_2DOT5G_MODE:
            case CTC_CHIP_SERDES_XFI_MODE:
                step = XqmacSgmac1Cfg0_t - XqmacSgmac0Cfg0_t;
                tbl_id = XqmacSgmac0Cfg0_t + (mac_id+port_attr->slice_id*48) / 4 + ((mac_id+port_attr->slice_id*48)%4)*step;
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
                value = GetXqmacSgmac0Cfg0(V, crcChkEn0_f, &sgmac_cfg);
                break;

            case CTC_CHIP_SERDES_XLG_MODE:
            case CTC_CHIP_SERDES_XAUI_MODE:
            case CTC_CHIP_SERDES_DXAUI_MODE:
                tbl_id = XqmacXlgmacCfg0_t + (mac_id+port_attr->slice_id*48)/4;
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlgmac_cfg));
                value = GetXqmacXlgmacCfg0(V, crcChkEn_f, &xlgmac_cfg);
                break;

            case CTC_CHIP_SERDES_CG_MODE:
                tbl_id = CgmacCfg0_t + port_attr->slice_id*2 + ((mac_id == 36)?0:1);
                cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgmac_cfg));
                value = GetCgmacCfg0(V, crcChkEn_f, &xlgmac_cfg);
                break;

            default:
                break;
        }
    }

    *enable = value;

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_rx_pause_type(uint8 lchip, uint16 gport, uint32* p_pasue_type)
{
    int32  step = 0;

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_net_rx_pause_ctl[2] = {NetRxPauseCtl0_t, NetRxPauseCtl1_t};
    NetRxPauseCtl0_m net_rx_pause_ctl;
    sys_datapath_lport_attr_t* p_port_cap = NULL;

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, CTC_MAP_GPORT_TO_LPORT(gport), &p_port_cap));
    if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    PORT_LOCK;
    sal_memset(&net_rx_pause_ctl, 0, sizeof(NetRxPauseCtl0_m));
    cmd = DRV_IOR(tbl_net_rx_pause_ctl[p_port_cap->slice_id], DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK((DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_ctl)), p_gg_port_master[lchip]->p_port_mutex);

    step = NetRxPauseCtl0_cfgNormPauseEn1_f - NetRxPauseCtl0_cfgNormPauseEn0_f;
    GetNetRxPauseCtl0(A, cfgNormPauseEn0_f + (step * ((p_port_cap->mac_id) >> 4)), &net_rx_pause_ctl, &value);

    if (value)
    {
        CTC_SET_FLAG(*p_pasue_type, SYS_PORT_PAUSE_FRAME_TYPE_NORMAL);
    }

    step = NetRxPauseCtl0_cfgPfcPauseEn1_f - NetRxPauseCtl0_cfgPfcPauseEn0_f;
    GetNetRxPauseCtl0(A, cfgPfcPauseEn0_f + (step * ((p_port_cap->mac_id) >> 4)), &net_rx_pause_ctl, &value);

    if (value)
    {
        CTC_SET_FLAG(*p_pasue_type, SYS_PORT_PAUSE_FRAME_TYPE_PFC);
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_unidir_en(uint8 lchip, uint16 gport, bool enable)
{
    uint8  slice_id   = 0;
    uint8  mac_id     = 0;
    uint8  step       = 0;
    uint16 lport      = 0;
    uint32 tbl_id     = 0;
    uint32 cmd        = 0;
    uint8 sub_idx     = 0;
    uint32 value = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master)
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    mac_id   = port_attr->mac_id;
    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    value = enable?1:0;
    if(CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
    {
        tbl_id = CgPcsCfg0_t + slice_id*2 + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLinkIntFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLocalFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreRemoteFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else if(CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        }
    }
    else if(CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else if((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, (SharedPcsCfg0_unidirectionEn0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_unidir_en(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8  slice_id   = 0;
    uint8  mac_id     = 0;
    uint8  step       = 0;
    uint16 lport      = 0;
    uint32 tbl_id     = 0;
    uint32 cmd        = 0;
    uint8 sub_idx     = 0;
    uint32 value1      = 0;
    uint32 value2      = 0;
    uint32 value3      = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    if (NULL == p_gg_port_master)
    {
        return CTC_E_NOT_INIT;
    }

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    slice_id = port_attr->slice_id;
    mac_id   = port_attr->mac_id;
    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    if(CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
    {
        tbl_id = CgPcsCfg0_t + slice_id*2 + ((port_attr->mac_id==36)?0:1);
        cmd = DRV_IOR(tbl_id, CgPcsCfg0_ignoreLinkIntFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        cmd = DRV_IOR(tbl_id, CgPcsCfg0_ignoreLocalFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
        cmd = DRV_IOR(tbl_id, CgPcsCfg0_ignoreRemoteFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value3));
        if (value1 && value2 && value3)
        {
            *p_value = TRUE;
        }
        else
        {
            *p_value = FALSE;
        }
    }
    else if(CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
            cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
            cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value3));
            if (value1 && value2 && value3)
            {
                *p_value = TRUE;
            }
            else
            {
                *p_value = FALSE;
            }
        }
    }
    else if(CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + ((mac_id+slice_id*48)%4)*step;
        cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
        cmd = DRV_IOR(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value3));
        if (value1 && value2 && value3)
        {
            *p_value = TRUE;
        }
        else
        {
            *p_value = FALSE;
        }
    }
    else if((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))
    {
        tbl_id = SharedPcsCfg0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, (SharedPcsCfg0_unidirectionEn0_f + mac_id%4));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        if (value1)
        {
            *p_value = TRUE;
        }
        else
        {
            *p_value = FALSE;
        }
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_mux_type(uint8 lchip, uint16 gport, uint32 value)
{
    uint16 chan_id = 0;
    uint32 cmd  = 0;
    uint8 mux_type = 0;
    uint32 field_val = 0;

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (chan_id == 0xff)
    {
        return CTC_E_INVALID_PARAM;
    }

    switch (value)
    {
    case CTC_PORT_MUX_TYPE_NONE:
        mux_type = 0;
        break;
    case CTC_PORT_MUX_TYPE_CLOUD_L2_HDR:
        mux_type = 7;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    field_val = mux_type;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_get_mux_type(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 chan_id = 0;
    uint32 cmd  = 0;
    uint32 field_val = 0;

    CTC_PTR_VALID_CHECK(p_value);
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (chan_id == 0xff)
    {
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOR(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_val));
    switch (field_val)
    {
    case 0:
        *p_value = CTC_PORT_MUX_TYPE_NONE;
        break;
    case 7:
        *p_value = CTC_PORT_MUX_TYPE_CLOUD_L2_HDR;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_station_move(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value)
{
    uint32  cmd = 0;
    int32   ret = 0;
    uint32  field = 0;
    uint8   gchip = 0;

    sys_goldengate_get_gchip_id(lchip, &gchip);

    if (port_prop == CTC_PORT_PROP_STATION_MOVE_PRIORITY)
    {
        CTC_MAX_VALUE_CHECK(value, 1);
        field = value;
        CTC_ERROR_RETURN(sys_goldengate_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, field));
    }
    else if(port_prop == CTC_PORT_PROP_STATION_MOVE_ACTION)
    {
        CTC_MAX_VALUE_CHECK(value, CTC_PORT_STATION_MOVE_ACTION_TYPE_MAX-1);
        field = (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU) ? 1 : 0;
        if (field && (gchip != CTC_MAP_GPORT_TO_GCHIP(gport)))
        {
            return CTC_E_NOT_SUPPORT;
        }

        CTC_ERROR_RETURN(sys_goldengate_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, value));

        if (gchip == CTC_MAP_GPORT_TO_GCHIP(gport))
        {
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcMismatchExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, CTC_MAP_GPORT_TO_LPORT(gport), cmd, &field));
        }
    }

    return ret;
}
/**
@brief   Config port's properties
*/
int32
sys_goldengate_port_set_property(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value)
{
    int32   ret = CTC_E_NONE;

    uint16  lport = 0;
    uint8   value8 = 0;
    uint32  field = 0;
    uint32  cmd = 0;
    uint16 chan_id = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:0x%04X, property:%d, value:%d\n", \
                     gport, port_prop, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    PORT_LOCK;
    switch (port_prop)
    {
    case CTC_PORT_PROP_RAW_PKT_TYPE:
        ret = _sys_goldengate_port_set_raw_packet_type(lchip, gport, value);
        break;

    case CTC_PORT_PROP_VLAN_DOMAIN:
        field = (CTC_DOT1Q_TYPE_CVLAN == value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_outerVlanIsCVlan_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        field = (CTC_DOT1Q_TYPE_CVLAN == value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_cvlanSpace_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        field = (CTC_DOT1Q_TYPE_CVLAN == value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_flowL2KeyUseCvlan_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        field = (CTC_DOT1Q_TYPE_CVLAN == value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_sclFlowL2KeyUseCvlan_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PORT_EN:
        if (value)
        {
            sys_goldengate_port_link_up_event(lchip, gport);
        }
        else
        {
            sys_goldengate_port_link_down_event(lchip, gport);
        }
        ret = _sys_goldengate_port_set_port_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_TRANSMIT_EN:     /**< set port whether the tranmist is enable */
        field = (value) ? 0 : 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_transmitDisable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);

        if (!p_gg_port_master[lchip]->mlag_isolation_en)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_set_mcast_member_down(lchip, gport, (value) ? 0 : 1));
        }

        /* clear link status to up*/
        if (value)
        {
            chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
            if (0xFF != chan_id)
            {
                value = 0;
                cmd = DRV_IOW(QMgrEnqLinkState_t, QMgrEnqLinkState_linkState_f);
                ret = ret ? ret : DRV_IOCTL(lchip, chan_id, cmd, &value);
                cmd = DRV_IOW(DlbChanStateCtl_t, DlbChanStateCtl_linkState_f);
                ret = ret ? ret : DRV_IOCTL(lchip, chan_id, cmd, &value);
            }
        }

        break;

    case CTC_PORT_PROP_RECEIVE_EN:      /**< set port whether the receive is enable */
        field = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_receiveDisable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_BRIDGE_EN:       /**< set port whehter layer2 bridge function is enable */
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_bridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_DEFAULT_VLAN:     /**< set default vlan id of packet which receive from this port */
        if ((value) < CTC_MIN_VLAN_ID || (value) > CTC_MAX_VLAN_ID)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        ret = _sys_goldengate_port_set_default_vlan(lchip, gport, value);
        break;

    case CTC_PORT_PROP_UNTAG_PVID_TYPE:
        switch(value)
        {
        case CTC_PORT_UNTAG_PVID_TYPE_NONE:
            field = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);
            break;

        case CTC_PORT_UNTAG_PVID_TYPE_SVLAN:
            field = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);
            field = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultSvlan_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
            break;

        case CTC_PORT_UNTAG_PVID_TYPE_CVLAN:
            field = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);
            field = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultSvlan_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
            break;

        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        break;

    case CTC_PORT_PROP_VLAN_CTL:     /**< set port's vlan tag control mode */
        if (value >= MAX_CTC_VLANTAG_CTL)
        {
            PORT_UNLOCK;
            return CTC_E_VLAN_EXCEED_MAX_VLANCTL;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_vlanTagCtl_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_CROSS_CONNECT_EN:     /**< Set port cross connect */
        ret = _sys_goldengate_port_set_cross_connect(lchip, gport, value);
        break;

    case CTC_PORT_PROP_LEARNING_EN:     /**< Set learning enable/disable on port */
        field = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_learningDisable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_DOT1Q_TYPE:     /**< Set port dot1q type */
        if ((value) > (CTC_DOT1Q_TYPE_BOTH))
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        field = value;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_dot1QEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_USE_OUTER_TTL:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_useOuterTtl_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PROTOCOL_VLAN_EN:     /**< set protocol vlan enable on port */
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_protocolVlanEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_MAC_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_goldengate_port_set_mac_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_EEE_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_goldengate_port_set_eee_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_FEC_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_goldengate_port_set_fec_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_CL73_ABILITY:
        ret = sys_goldengate_port_set_cl73_ability(lchip, gport, value);
        break;

    case CTC_PORT_PROP_AUTO_NEG_EN:
        value = (value) ? TRUE : FALSE;
        ret = sys_goldengate_port_set_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, value);
        break;

    case CTC_PORT_PROP_AUTO_NEG_MODE:
        ret = sys_goldengate_port_set_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, value);
        break;

    case CTC_PORT_PROP_SPEED:
        ret = _sys_goldengate_port_set_speed(lchip, gport, value);
        break;

    case CTC_PORT_PROP_MAX_FRAME_SIZE:
        ret = _sys_goldengate_port_set_max_frame(lchip, gport, value);
        break;

    case CTC_PORT_PROP_PREAMBLE:
        ret = sys_goldengate_port_mac_set_property(lchip, gport, SYS_PORT_MAC_PROPERTY_PREAMBLE, value);
        break;

    case CTC_PORT_PROP_MIN_FRAME_SIZE:
        value8 = value;
        ret = _sys_goldengate_port_set_min_frame_size(lchip, gport, value8);
        break;

    case CTC_PORT_PROP_ERROR_CHECK:
        ret = _sys_goldengate_port_set_error_check(lchip, gport, value);
        break;

    case CTC_PORT_PROP_PADING_EN:
        ret = sys_goldengate_port_mac_set_property(lchip, gport, SYS_PORT_MAC_PROPERTY_PADDING, value);
        break;

    case CTC_PORT_PROP_RX_PAUSE_TYPE:
        ret = _sys_goldengate_port_set_rx_pause_type(lchip, gport, value);
        break;

    case CTC_PORT_PROP_SRC_DISCARD_EN:     /**< set port whether the src_discard is enable */
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_srcDiscard_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PORT_CHECK_EN:     /**< set port whether the src port match check is enable */
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_portCheckEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_HW_LEARN_EN:     /**< Hardware learning enable*/
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_fastLearningEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_SCL_IPV6_LOOKUP_EN:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_IPV4_LOOKUP_EN:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_USE_DEFAULT_LOOKUP:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_useDefaultVlanLookup_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV6_TO_MAC:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV4_TO_MAC:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_TRILL_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_trillEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;
    case CTC_PORT_PROP_TRILL_MCAST_DECAP_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_trillMcastLoopback_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_DISCARD_NON_TRIL_PKT:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_discardNonTrill_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_DISCARD_TRIL_PKT:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_discardTrill_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_REFLECTIVE_BRIDGE_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_reflectiveBridgeEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_FCOE_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_fcoeEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_RPF_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_portBasedRpfEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_FCMAP:
        if (value < 0x0EFC00 || value > 0x0EFCFF)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        field = value & 0xFF;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_fcoeOuiIndex_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_COS:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_replaceStagCos_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_TPID:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_replaceStagTpid_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_REPLACE_CTAG_COS:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_replaceCtagCos_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_REPLACE_DSCP_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_replaceDscp_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_L3PDU_ARP_ACTION:
        if (value >= CTC_PORT_ARP_ACTION_TYPE_MAX)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_arpExceptionType_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_L3PDU_DHCP_ACTION:
        if (value >= CTC_PORT_DHCP_ACTION_TYPE_MAX)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_dhcpExceptionType_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_TUNNEL_RPF_CHECK:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_tunnelRpfCheck_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PTP_EN:
        if (value > SYS_PTP_MAX_PTP_ID)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ptpIndex_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_RPF_TYPE:
        if (value >= CTC_PORT_RPF_TYPE_MAX)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_rpfType_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_IS_LEAF:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_isLeaf_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PKT_TAG_HIGH_PRIORITY:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ingressTagHighPriority_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_ROUTE_EN:
        field = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_routeDisable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_PRIORITY_TAG_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_priorityTagEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_IPG:
        if (value >= CTC_IPG_SIZE_MAX)
        {
            PORT_UNLOCK;
            return CTC_E_VLAN_EXCEED_MAX_VLANCTL;
        }
        field = value;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ipgIndex_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ipgIndex_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        break;

    case CTC_PORT_PROP_DEFAULT_PCP:
        if (value > 7)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultPcp_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        field = value;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_defaultPcp_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_DEFAULT_DEI:
        if (value > 1)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultDei_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        field = value;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_defaultCfi_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_NVGRE_MCAST_NO_DECAP:
        if (value > 1)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_nvgreMcastLoopbackMode_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_VXLAN_MCAST_NO_DECAP:
        if (value > 1)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_vxlanMcastLoopbackMode_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_QOS_POLICY:
        if (value >= CTC_QOS_TRUST_MAX)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        sys_goldengate_common_map_qos_policy(lchip, value, &field);
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_qosPolicy_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_SCL_HASH_FIELD_SEL_ID:
        if (value > 3)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_sclFlowHashFieldSel_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_SNOOPING_PARSER:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_forceSecondParser_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_FLOW_LKUP_BY_OUTER_HEAD:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_aclQosUseOuterInfo_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_AWARE_TUNNEL_INFO_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_keyMergeInnerAndOuterHdr_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_NVGRE_ENABLE:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_nvgreEnable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_METADATA_OVERWRITE_PORT:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_metadataOverwritePort_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_METADATA_OVERWRITE_UDF:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_metadataOverwriteUdf_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcMismatchExceptionEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_STATION_MOVE_PRIORITY:
        if (value > 1)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        field = value;
        ret = sys_goldengate_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, field);
        break;

    case CTC_PORT_PROP_STATION_MOVE_ACTION:
        CTC_MAX_VALUE_CHECK(value, CTC_PORT_STATION_MOVE_ACTION_TYPE_MAX-1);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, value));
        field = (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcMismatchExceptionEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_EFD_EN:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(IpePortEfdCtl_t, IpePortEfdCtl_array_0_efdEnable_f+(lport&0xFF));
        ret = DRV_IOCTL(lchip, (lport>>8), cmd, &field);
        break;

    case CTC_PORT_PROP_ADD_DEFAULT_VLAN_DIS:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_LOGIC_PORT:
        if (!value)
        {
            field  = 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_useDefaultLogicSrcPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);

            field = 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultLogicSrcPort_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);

            field  = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_logicDestPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);

            field  = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_logicPortCheckEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        }
        else
        {
            if (value > 0x3FFF)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            field  = 1;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_useDefaultLogicSrcPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);

            field = value;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultLogicSrcPort_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);

            field  = value;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_logicDestPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);

            field  = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_logicPortCheckEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
        }

        break;

    case CTC_PORT_PROP_LINKSCAN_EN:
        break;

    case CTC_PORT_PROP_LINK_INTRRUPT_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_goldengate_port_set_link_intr(lchip, gport, value);
        p_gg_port_master[lchip]->igs_port_prop[lport].link_intr_en = (value)?1:0;

        break;

    case CTC_PORT_PROP_APS_FAILOVER_EN:
        value = (value) ? 1 : 0;
        ret = _sys_goldengate_port_set_aps_failover_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_LINKAGG_FAILOVER_EN:
        value = (value) ? 1 : 0;
        ret = _sys_goldengate_port_set_linkagg_failover_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_APS_SELECT_GRP_ID:
        if (0xFFFFFFFF == value)
        {
            field = 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_apsSelectValid_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field);
        }
        else
        {
            if (value >= TABLE_MAX_INDEX(DsApsBridge_t))
            {
                ret = CTC_E_APS_INVALID_GROUP_ID;
            }
            else
            {
                field = value;
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_apsSelectGroupId_f);
                ret = DRV_IOCTL(lchip, lport, cmd, &field);
                field = 1;
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_apsSelectValid_f);
                ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field);
            }
        }
        break;

    case CTC_PORT_PROP_APS_SELECT_WORKING:
        field = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_apsSelectProtectingPath_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;

    case CTC_PORT_PROP_UNIDIR_EN:
        value = (value) ? 1 : 0;
        ret = _sys_goldengate_port_set_unidir_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_MAC_TX_IPG:
        ret = sys_goldengate_port_mac_set_ipg(lchip, gport, value);
        break;

    case CTC_PORT_PROP_OVERSUB_PRI:
        ret = _sys_goldengate_port_set_oversub(lchip, gport, value);
        break;
    case CTC_PORT_PROP_CUT_THROUGH_EN:
        ret = _sys_goldengate_port_set_cut_through_en(lchip,gport,value);
        break;
    case CTC_PORT_PROP_LOOP_WITH_SRC_PORT:
        field = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_sourcePortToHeader_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field);
        break;
    case CTC_PORT_PROP_MUX_TYPE:
        ret = _sys_goldengate_port_set_mux_type(lchip, gport, value);
        break;
    default:
        PORT_UNLOCK;
        return CTC_E_NOT_SUPPORT;
    }

    if (ret != CTC_E_NONE)
    {
        PORT_UNLOCK;
        return ret;
    }
    PORT_UNLOCK;
    return CTC_E_NONE;
}

int32 _sys_goldengate_port_get_mac_id(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport = 0;
    int32 ret = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport);

    ret = sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
    if (ret < 0)
    {
        return CTC_E_INVALID_PARAM;
    }

    *p_value = (port_attr->mac_id + 64 * port_attr->slice_id);

    return CTC_E_NONE;
}

/**
@brief    Get port's properties according to gport id
*/
int32
sys_goldengate_port_get_property(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32* p_value)
{
    uint8  value8 = 0;

    uint8  inverse_value = 0;
    int32  ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint16 lport = 0;
    uint32 value = 0;
    ctc_port_speed_t port_speed = CTC_PORT_SPEED_1G;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port property, gport:0x%04X, property:%d!\n", gport, port_prop);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    switch (port_prop)
    {
    case CTC_PORT_PROP_RAW_PKT_TYPE:
        ret = _sys_goldengate_port_get_raw_packet_type(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_VLAN_DOMAIN:
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_outerVlanIsCVlan_f);
        break;

    case CTC_PORT_PROP_PORT_EN:
        ret = _sys_goldengate_port_get_port_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_TRANSMIT_EN:      /**< get port whether the tranmist is enable */
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_transmitDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_RECEIVE_EN:       /**< get port whether the receive is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_receiveDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_BRIDGE_EN:        /**< get port whehter layer2 bridge function is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_bridgeEn_f);
        break;

    case CTC_PORT_PROP_DEFAULT_VLAN:     /**< Get default vlan id on the Port */
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_defaultVlanId_f);
        break;

    case CTC_PORT_PROP_UNTAG_PVID_TYPE:
        {
            uint32 r_value = 0;
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &r_value));
            if (!r_value) /* none */
            {
                *p_value = CTC_PORT_UNTAG_PVID_TYPE_NONE;
            }
            else /* s or c */
            {
                cmd = DRV_IOR(DsDestPort_t, DsDestPort_untagDefaultSvlan_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &r_value));
                if (r_value) /* s */
                {
                    *p_value = CTC_PORT_UNTAG_PVID_TYPE_SVLAN;
                }
                else
                {
                    *p_value = CTC_PORT_UNTAG_PVID_TYPE_CVLAN;
                }
            }

            return CTC_E_NONE;

        }
        break;

    case CTC_PORT_PROP_VLAN_CTL:     /**< Get port's vlan tag control mode */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_vlanTagCtl_f);
        break;

    case CTC_PORT_PROP_CROSS_CONNECT_EN:     /**< Get port cross connect */
        ret = _sys_goldengate_port_get_cross_connect(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LEARNING_EN:     /**< Get learning enable/disable on port */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_learningDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
        *p_value = *p_value ? FALSE : TRUE;
        return CTC_E_NONE;

    case CTC_PORT_PROP_DOT1Q_TYPE:     /**< Get port dot1q type */
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_dot1QEn_f);
        break;

    case CTC_PORT_PROP_USE_OUTER_TTL:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_useOuterTtl_f);
        break;

    case CTC_PORT_PROP_PROTOCOL_VLAN_EN:     /**< Get protocol vlan enable on port */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_protocolVlanEn_f);
        break;

    case CTC_PORT_PROP_MAC_EN:
        ret = sys_goldengate_port_get_mac_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_FEC_EN:
        ret = _sys_goldengate_port_get_fec_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_SIGNAL_DETECT:
        ret = sys_goldengate_port_get_mac_signal_detect(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINK_INTRRUPT_EN:
        *p_value = p_gg_port_master[lchip]->igs_port_prop[lport].link_intr_en? TRUE : FALSE;
        break;

    case CTC_PORT_PROP_AUTO_NEG_EN:
        ret = sys_goldengate_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, p_value);
        break;

    case CTC_PORT_PROP_AUTO_NEG_MODE:
        ret = sys_goldengate_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, p_value);
        break;

    case CTC_PORT_PROP_SPEED:
        ret = _sys_goldengate_port_get_speed(lchip, gport, &port_speed);
        if (CTC_E_NONE == ret)
        {
            *p_value = port_speed;
        }
        break;

    case CTC_PORT_PROP_MAX_FRAME_SIZE:
        ret = _sys_goldengate_port_get_max_frame(lchip, gport, (ctc_frame_size_t*)p_value);
        break;

    case CTC_PORT_PROP_PREAMBLE:
        ret = sys_goldengate_port_mac_get_property(lchip, gport, SYS_PORT_MAC_PROPERTY_PREAMBLE, p_value);
        break;

    case CTC_PORT_PROP_MIN_FRAME_SIZE:
        ret = _sys_goldengate_port_get_min_frame_size(lchip, gport, &value8);
        *p_value = value8;
        break;

    case CTC_PORT_PROP_ERROR_CHECK:
        ret = _sys_goldengate_port_get_error_check(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_PADING_EN:
        ret = sys_goldengate_port_mac_get_property(lchip, gport, SYS_PORT_MAC_PROPERTY_PADDING, p_value);
        break;

    case CTC_PORT_PROP_RX_PAUSE_TYPE:
        ret = _sys_goldengate_port_get_rx_pause_type(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_SRC_DISCARD_EN:     /**< set port whether the src_discard is enable */
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_srcDiscard_f);
        break;

    case CTC_PORT_PROP_PORT_CHECK_EN:     /**< set port whether the src port match check is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portCheckEn_f);
        break;

    /*new properties for GB*/
    case CTC_PORT_PROP_HW_LEARN_EN:     /**< hardware learning enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_fastLearningEn_f);
        break;

    case CTC_PORT_PROP_SCL_IPV6_LOOKUP_EN:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_IPV4_LOOKUP_EN:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_USE_DEFAULT_LOOKUP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_useDefaultVlanLookup_f);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV6_TO_MAC:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV4_TO_MAC:
        ret = CTC_E_NOT_SUPPORT;
        break;

    case CTC_PORT_PROP_TRILL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_trillEn_f);
        break;
    case CTC_PORT_PROP_TRILL_MCAST_DECAP_EN:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_trillMcastLoopback_f);
        break;

    case CTC_PORT_PROP_DISCARD_NON_TRIL_PKT:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_discardNonTrill_f);
        break;

    case CTC_PORT_PROP_DISCARD_TRIL_PKT:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_discardTrill_f);
        break;

    case CTC_PORT_PROP_REFLECTIVE_BRIDGE_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_reflectiveBridgeEn_f);
        break;

    case CTC_PORT_PROP_FCOE_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_fcoeEn_f);
        break;

    case CTC_PORT_PROP_RPF_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portBasedRpfEn_f);
        break;

    case CTC_PORT_PROP_FCMAP:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_fcoeOuiIndex_f);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_COS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_replaceStagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_CTAG_COS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_replaceCtagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_TPID:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_replaceStagTpid_f);
        break;

    case CTC_PORT_PROP_REPLACE_DSCP_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_replaceDscp_f);
        break;

    case CTC_PORT_PROP_L3PDU_ARP_ACTION:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_arpExceptionType_f);
        break;

    case CTC_PORT_PROP_L3PDU_DHCP_ACTION:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_dhcpExceptionType_f);
        break;

    case CTC_PORT_PROP_TUNNEL_RPF_CHECK:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_tunnelRpfCheck_f);
        break;

    case CTC_PORT_PROP_PTP_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ptpIndex_f);
        break;

    case CTC_PORT_PROP_RPF_TYPE:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_rpfType_f);
        break;

    case CTC_PORT_PROP_IS_LEAF:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_isLeaf_f);
        break;

    case CTC_PORT_PROP_PKT_TAG_HIGH_PRIORITY:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ingressTagHighPriority_f);
        break;

    case CTC_PORT_PROP_ROUTE_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_routeDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_PRIORITY_TAG_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_priorityTagEn_f);
        break;

    case CTC_PORT_PROP_IPG:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ipgIndex_f);
        break;

    case CTC_PORT_PROP_DEFAULT_PCP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_defaultPcp_f);
        break;

    case CTC_PORT_PROP_DEFAULT_DEI:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_defaultDei_f);
        break;

    case CTC_PORT_PROP_NVGRE_MCAST_NO_DECAP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_nvgreMcastLoopbackMode_f);
        break;

    case CTC_PORT_PROP_VXLAN_MCAST_NO_DECAP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_vxlanMcastLoopbackMode_f);
        break;

    case CTC_PORT_PROP_QOS_POLICY:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_qosPolicy_f);
        break;

    case CTC_PORT_PROP_SCL_HASH_FIELD_SEL_ID:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_sclFlowHashFieldSel_f);
        break;

    case CTC_PORT_PROP_SNOOPING_PARSER:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_forceSecondParser_f);
        break;

    case CTC_PORT_PROP_FLOW_LKUP_BY_OUTER_HEAD:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_aclQosUseOuterInfo_f);
        break;

    case CTC_PORT_PROP_AWARE_TUNNEL_INFO_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_keyMergeInnerAndOuterHdr_f);
        break;

    case CTC_PORT_PROP_NVGRE_ENABLE:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_nvgreEnable_f);
        break;

    case CTC_PORT_PROP_METADATA_OVERWRITE_PORT:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_metadataOverwritePort_f);
        break;

    case CTC_PORT_PROP_METADATA_OVERWRITE_UDF:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_metadataOverwriteUdf_f);
        break;

    case CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_srcMismatchExceptionEn_f);
        break;

    case CTC_PORT_PROP_STATION_MOVE_PRIORITY:
        ret = sys_goldengate_l2_get_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, p_value);
        break;

    case CTC_PORT_PROP_STATION_MOVE_ACTION:
        CTC_ERROR_RETURN(sys_goldengate_l2_get_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, &value));

        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_srcMismatchExceptionEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

        if (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD)
        {
            *p_value = CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD;
        }
        else if (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD)
        {
            *p_value = *p_value ? CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU : CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD;
        }

        return CTC_E_NONE;

    case CTC_PORT_PROP_EFD_EN:
        cmd = DRV_IOR(IpePortEfdCtl_t, IpePortEfdCtl_array_0_efdEnable_f+(lport&0xFF));
        ret = DRV_IOCTL(lchip, (lport>>8), cmd, p_value);
        cmd = 0;
        break;

    case CTC_PORT_PROP_ADD_DEFAULT_VLAN_DIS:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_addDefaultVlanDisable_f);
        break;

    case CTC_PORT_PROP_LOGIC_PORT:
         cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_defaultLogicSrcPort_f);
         break;

    case CTC_PORT_PROP_LINKSCAN_EN:
        *p_value = 1;
        break;

    case CTC_PORT_PROP_APS_FAILOVER_EN:
        ret = _sys_goldengate_port_get_aps_failover_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINKAGG_FAILOVER_EN:
        ret = _sys_goldengate_port_get_linkagg_failover_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_APS_SELECT_GRP_ID:
        ret = _sys_goldengate_port_get_aps_select_grp_id(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_APS_SELECT_WORKING:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_apsSelectProtectingPath_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_UNIDIR_EN:
        ret = _sys_goldengate_port_get_unidir_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_MAC_TX_IPG:
        ret = sys_goldengate_port_mac_get_ipg(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_OVERSUB_PRI:
        ret = _sys_goldengate_port_get_oversub(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINK_UP:
        ret = sys_goldengate_port_get_mac_link_up(lchip, gport, p_value, 1);
        break;
    case CTC_PORT_PROP_CUT_THROUGH_EN:
        ret = _sys_goldengate_port_get_cut_through_en(lchip,gport,p_value);
        break;
    case CTC_PORT_PROP_LOOP_WITH_SRC_PORT:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_sourcePortToHeader_f);
        break;
    case CTC_PORT_PROP_MUX_TYPE:
        ret = _sys_goldengate_port_get_mux_type(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_MAC_ID:
        ret = _sys_goldengate_port_get_mac_id(lchip, gport, p_value);
        break;

    default:
        return CTC_E_NOT_SUPPORT;
    }

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    if (cmd == 0)
    {
        /*when cmd == 0, get value from  software memory*/
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

    if(CTC_PORT_PROP_QOS_POLICY == port_prop)
    {
        uint32 trust;
        sys_goldengate_common_unmap_qos_policy(lchip, *p_value, &trust);
        *p_value = trust;
    }

    *p_value = inverse_value ? (!(*p_value)) : (*p_value);

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_mlag_isolation(uint8 lchip, ctc_port_isolation_t* p_port_isolation, bool enable)
{
    uint16 lport = 0;
    uint16 drv_port = 0;
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 i = 0;
    MetFifoPortStatusCtl_m     port_status;
    uint32 bitmap[4]            = {0};


    if (enable)
    {
        CTC_PTR_VALID_CHECK(p_port_isolation);
        /*Set ingress source port*/
        if (!CTC_IS_LINKAGG_PORT(p_port_isolation->gport))
        {
            SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_port_isolation->gport, lchip, lport);
        }

        drv_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_port_isolation->gport);
        field_val = drv_port;
        CTC_BIT_SET(field_val, 15);

        cmd = DRV_IOW(MetFifoReserved_t, MetFifoReserved_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*Set egress port*/
        sal_memset(&port_status, 0, sizeof(port_status));
        cmd = DRV_IOR(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));

        for (i = 0; i < 4; i++)
        {
            bitmap[0] = ~(p_port_isolation->pbm[0 + i*2]);
            bitmap[1] = ~(p_port_isolation->pbm[1 + i*2]);
            bitmap[2] = ~(p_port_isolation->pbm[8 + i*2]);
            bitmap[3] = ~(p_port_isolation->pbm[9 + i*2]);
            SetMetFifoPortStatusCtl(A, baseArray_0_portStatus_f + i, &port_status, bitmap);
        }

        SetMetFifoPortStatusCtl(V, bitmapPortStatusCheckEn_f, &port_status, 0);
        cmd = DRV_IOW(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));
        PORT_LOCK;
        p_gg_port_master[lchip]->mlag_isolation_en = 1;
        PORT_UNLOCK;
    }
    else
    {
        if (!p_gg_port_master[lchip]->mlag_isolation_en)
        {
            return CTC_E_NONE;
        }
        field_val = 0;
        cmd = DRV_IOW(MetFifoReserved_t, MetFifoReserved_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        sal_memset(&port_status, 0, sizeof(port_status));
        cmd = DRV_IOR(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));
        for (i = 0; i < 4; i++)
        {
            bitmap[0] = 0xFFFFFFFF;
            bitmap[1] = 0xFFFFFFFF;
            bitmap[2] = 0xFFFFFFFF;
            bitmap[3] = 0x3FFFFFFF;
            SetMetFifoPortStatusCtl(A, baseArray_0_portStatus_f + i, &port_status, bitmap);
        }

        SetMetFifoPortStatusCtl(V, bitmapPortStatusCheckEn_f, &port_status, 1);
        cmd = DRV_IOW(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));

        PORT_LOCK;
        p_gg_port_master[lchip]->mlag_isolation_en = 0;
        PORT_UNLOCK;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_mcast_member_down(uint8 lchip, uint16 gport, uint8 value)
{
    uint32 cmd       = 0;
    uint32 field_val = 0;

    CTC_MAX_VALUE_CHECK(value, 1);
    field_val = 1;
    cmd = DRV_IOW(MetFifoPortStatusCtl_t, MetFifoPortStatusCtl_bitmapPortStatusCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    CTC_ERROR_RETURN(_sys_goldengate_port_set_bitmap_status(lchip, gport, value));
    return CTC_E_NONE;
}

int32
_sys_goldengate_port_check_restriction_mode(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint16 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:0x%04X\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    p_restriction->mode = CTC_PORT_RESTRICTION_DISABLE;

    if (CTC_INGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else if (CTC_EGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_destPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    if (0 != value)
    {
        if (0x30 == (value & 0x38))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
            p_restriction->type = CTC_PORT_PVLAN_PROMISCUOUS;
        }
        else if  (0x38 == (value & 0x38))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
            p_restriction->type = CTC_PORT_PVLAN_ISOLATED;
        }
        else if (0x20 == (value & 0x30))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
            p_restriction->type = CTC_PORT_PVLAN_COMMUNITY;
            p_restriction->isolated_id = value & (~((uint32)0x30));
        }
        else
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PORT_ISOLATION;

            if (CTC_INGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
            }
            else if (CTC_EGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
            }
        }
    }
    return CTC_E_NONE;
}

/**
@brief    Set port's restriction
*/
int32
sys_goldengate_port_set_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{
    int32  ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    ctc_port_restriction_t restriction;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:0x%04X, mode:%d, isolated_id:%d, dir:%d\n", \
                     gport, p_restriction->mode, p_restriction->isolated_id, p_restriction->dir);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);


    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&restriction, 0, sizeof(ctc_port_restriction_t));
    restriction.dir = CTC_INGRESS;
    CTC_ERROR_RETURN(_sys_goldengate_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode))
       || ((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        return CTC_E_PORT_HAS_OTHER_RESTRICTION;
    }

    restriction.dir = CTC_EGRESS;
    CTC_ERROR_RETURN(_sys_goldengate_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode))
       || ((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        return CTC_E_PORT_HAS_OTHER_RESTRICTION;
    }

    PORT_LOCK;
    switch (p_restriction->mode)
    {
    case CTC_PORT_RESTRICTION_PVLAN:

        if (CTC_PORT_PVLAN_PROMISCUOUS == p_restriction->type)
        {
            /* port_isolate_id[5:0] = 6'b110XXX */
            value = 0x30;
        }
        else if (CTC_PORT_PVLAN_ISOLATED == p_restriction->type)
        {
            /* port_isolate_id[5:0] = 6'b111XXX */
            value = 0x38;
        }
        else if (CTC_PORT_PVLAN_COMMUNITY == p_restriction->type)
        {
            value = p_restriction->isolated_id;
            if (value > SYS_MAX_PVLAN_COMMUNITY_ID_NUM)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            /* port_isolate_id[5:0] = 6'b10XXXX */
            value = (value | 0x20);
        }
        else if (CTC_PORT_PVLAN_NONE == p_restriction->type)
        {
            value = 0;
        }
        else
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));

        cmd = DRV_IOW(DsDestPort_t, DsDestPort_destPortIsolateId_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        break;

    case CTC_PORT_RESTRICTION_PORT_ISOLATION:
        if (p_restriction->isolated_id > (SYS_GOLDENGATE_ISOLATION_ID_NUM - 1))
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        if ((CTC_INGRESS == p_restriction->dir) || (CTC_BOTH_DIRECTION == p_restriction->dir))
        {
            value = p_restriction->isolated_id;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        }
        if ((CTC_EGRESS == p_restriction->dir) || (CTC_BOTH_DIRECTION == p_restriction->dir))
        {
            value = p_restriction->isolated_id;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_destPortIsolateId_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        }
        break;

    case CTC_PORT_RESTRICTION_PORT_BLOCKING:
        if (p_restriction->type > (CTC_PORT_ISOLATION_ALL + CTC_PORT_ISOLATION_UNKNOW_UCAST+
            CTC_PORT_ISOLATION_UNKNOW_MCAST + CTC_PORT_ISOLATION_KNOW_UCAST+CTC_PORT_ISOLATION_KNOW_MCAST + CTC_PORT_ISOLATION_BCAST))
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_port_blocking(lchip, gport, p_restriction->type));
        break;

    case CTC_PORT_RESTRICTION_DISABLE:
        value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_destPortIsolateId_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));

        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_port_blocking(lchip, gport, 0));
        break;

    default:
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    PORT_UNLOCK;
    return ret;
}

/**
@brief    Get port's restriction
*/
int32
sys_goldengate_port_get_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint16 block_type = 0;
    ctc_port_restriction_t restriction;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:0x%04X\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    sal_memset(&restriction, 0, sizeof(ctc_port_restriction_t));

    restriction.dir = CTC_INGRESS;
    CTC_ERROR_RETURN(_sys_goldengate_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode)))
    {
        p_restriction->isolated_id = 0;
        return CTC_E_NONE;
    }
    else if (((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        p_restriction->type = CTC_PORT_PVLAN_NONE;
        return CTC_E_NONE;
    }

    restriction.dir = CTC_EGRESS;
    CTC_ERROR_RETURN(_sys_goldengate_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode)))
    {
        p_restriction->isolated_id = 0;
        return CTC_E_NONE;
    }
    else if (((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        p_restriction->type = CTC_PORT_PVLAN_NONE;
        return CTC_E_NONE;
    }

    if (CTC_INGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else if (CTC_EGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_destPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    switch (p_restriction->mode)
    {
        case CTC_PORT_RESTRICTION_DISABLE:
            break;
        case CTC_PORT_RESTRICTION_PVLAN:
            if (0x30 == (value & 0x38))
            {
                p_restriction->type = CTC_PORT_PVLAN_PROMISCUOUS;
            }
            else if  (0x38 == (value & 0x38))
            {
                p_restriction->type = CTC_PORT_PVLAN_ISOLATED;
            }
            else if (0x20 == (value & 0x30))
            {
                p_restriction->type = CTC_PORT_PVLAN_COMMUNITY;
                p_restriction->isolated_id = value & (~((uint32)0x30));
            }
            break;
        case CTC_PORT_RESTRICTION_PORT_BLOCKING:
            CTC_ERROR_RETURN(_sys_goldengate_port_get_port_blocking(lchip, gport, &block_type));
            if (0 != block_type)
            {
                p_restriction->type = block_type;
            }
            break;
        case CTC_PORT_RESTRICTION_PORT_ISOLATION:

            if (CTC_INGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
            }
            else if (CTC_EGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
            }
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_port_set_isolation_id(uint8 lchip, ctc_port_isolation_t* p_port_isolation, uint32 field,
                                      uint32* p_isolation_group, uint32* p_pbm)
{
    uint8  igs_isolation_id = 0;
    uint32 igs_isolation_bitmap = 0;
    uint32 egs_isolation_bitmap = 0;
    uint16 lport = 0;
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint8* p_tmp_isolation = NULL;
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 old_bitmap = 0;
    int32  ret = 0;

    /***************************************
     Ingress isolation resource
     ***************************************/
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_port_isolation->gport, lchip, lport);

    igs_isolation_bitmap = p_gg_port_master[lchip]->igs_isloation_bitmap;

    if (0 == p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id)
    {
        uint8 bit = 0;

        for (bit = 1; bit < SYS_GOLDENGATE_ISOLATION_GROUP_NUM; bit++)
        {
            if (!CTC_IS_BIT_SET(igs_isolation_bitmap, bit))
            {
                igs_isolation_id = bit;
                break;
            }
        }

        if (SYS_GOLDENGATE_ISOLATION_GROUP_NUM == bit)
        {
            return CTC_E_NO_RESOURCE;
        }

		CTC_BIT_SET(igs_isolation_bitmap, bit);

        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Alloc source src-port: %d, igs_isolation_bitmap: 0x%x, bit:%d \n",
                         lport, igs_isolation_bitmap, bit);
    }
    else
    {
        igs_isolation_id = p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id;
        cmd = DRV_IOR(DsPortIsolation_t, field);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, igs_isolation_id, cmd, &old_bitmap));

		SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Use old source igs_isolation_id:%d, fld: %d, old_bitmap:0x%x\n", igs_isolation_id, field, old_bitmap);
    }

    /****************************************
     Egress isolation resource
     ****************************************/

    p_tmp_isolation = mem_malloc(MEM_PORT_MODULE, MAX_PORT_NUM_PER_CHIP);
    if (NULL == p_tmp_isolation)
    {
       return CTC_E_NO_MEMORY;
    }
	sal_memset(p_tmp_isolation, 0, MAX_PORT_NUM_PER_CHIP);

    egs_isolation_bitmap = p_gg_port_master[lchip]->egs_isloation_bitmap;
    /* free egress resource */
    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t)/sizeof(uint32); loop1++)
    {
        for (loop2 = 0; loop2 < CTC_UINT32_BITS; loop2++)
        {
            if (CTC_IS_BIT_SET(p_port_isolation->pbm[loop1], loop2))
            {
                continue;
            }
            lport = loop1 * CTC_UINT32_BITS + loop2;

            if (p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt
			   && CTC_IS_BIT_SET(old_bitmap, p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id))
            {
                p_tmp_isolation[lport] = 255; /*free resource*/

                if (1 == p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt)
                {
                    CTC_BIT_UNSET(egs_isolation_bitmap, p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id);
                    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free id:%d \n", p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id);
                }
            }
        }
    }

    /* alloc egress resource */
    for (loop1 = 0; loop1 < sizeof(ctc_port_bitmap_t)/sizeof(uint32); loop1++)
    {
        if (p_port_isolation->pbm[loop1] == 0)
        {
            continue;
        }

        for (loop2 = 0; loop2 < CTC_UINT32_BITS; loop2++)
        {
            if (!CTC_IS_BIT_SET(p_port_isolation->pbm[loop1], loop2))
            {
                continue;
            }
            lport = loop1 * CTC_UINT32_BITS + loop2;

            if (0 == p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id)
            {
                uint8 bit = 0;

                for (bit = 1; bit < SYS_GOLDENGATE_ISOLATION_ID_NUM; bit++)
                {
                    if (!CTC_IS_BIT_SET(egs_isolation_bitmap, bit))
                    {
						break;
                    }
                }

                if (SYS_GOLDENGATE_ISOLATION_ID_NUM == bit)
                {
                    mem_free(p_tmp_isolation);
                    return CTC_E_NO_RESOURCE;
                }

				CTC_BIT_SET(egs_isolation_bitmap, bit);
				p_tmp_isolation[lport] = bit; /*alloc resource*/

                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "alloc id:%d \n", bit);

            }
            else
            {
 				p_tmp_isolation[lport] = p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id; /*update ref cnt*/
            }
        }
    }

    /****************************************
     Egress isolation
     ****************************************/
    for (lport = 0; lport < MAX_PORT_NUM_PER_CHIP; lport++)
    {
        if (0 == p_tmp_isolation[lport])
        {
            continue;
        }

        if (255 == p_tmp_isolation[lport]) /*remove*/
        {
            if (1 == p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt)
            {
                p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt = 0;
                p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id = 0;
            }
            else
            {
                p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt--;
            }

                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lport:%d  id:%d, ref cnt:%d \n",
					 lport,
					 p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id,
					 p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt);

        }
        else /*add*/
        {
            if (0 == p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt)
            {
                p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt = 1;
                p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id = p_tmp_isolation[lport];
            }
            else
            {
                if (!CTC_IS_BIT_SET(old_bitmap, p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id))
                {
                    p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt++;
                }
            }

                SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lport:%d  id:%d, ref cnt:%d \n",
					 lport,
					 p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id,
					 p_gg_port_master[lchip]->egs_port_prop[lport].isolation_ref_cnt);

            CTC_BIT_SET(*p_pbm, p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id);
        }

        /*IO*/
        field_value = p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_destPortIsolateId_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_value), ret, error_return);
    }

    p_gg_port_master[lchip]->egs_isloation_bitmap = egs_isolation_bitmap;

    /****************************************
     Ingress isolation
     ****************************************/
    lport = CTC_MAP_GPORT_TO_LPORT(p_port_isolation->gport);

    if (0 == *p_pbm)
    {
        DsPortIsolation_m isolation;
        DsPortIsolation_m isolation_zero;
        cmd = DRV_IOR(DsPortIsolation_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, igs_isolation_id, cmd, &isolation), ret, error_return);
        DRV_SET_FIELD_V(DsPortIsolation_t, field, &isolation, 0);

		sal_memset(&isolation_zero, 0, sizeof(isolation_zero));

        if (!sal_memcmp(&isolation, &isolation_zero, sizeof(isolation)))
        {
            CTC_BIT_UNSET(igs_isolation_bitmap, igs_isolation_id);
            p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id = 0;
        }
    }
    else if (igs_isolation_id)
    {
        p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id = igs_isolation_id;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lport:%d src id:%d, pbm:0x%x\n", lport, p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id, *p_pbm);

    /*IO*/
    field_value = p_gg_port_master[lchip]->igs_port_prop[lport].isolation_id;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_value), ret, error_return);

    *p_isolation_group = igs_isolation_id;
    p_gg_port_master[lchip]->igs_isloation_bitmap = igs_isolation_bitmap;

error_return:
    mem_free(p_tmp_isolation);

	return CTC_E_NONE;
}

int32
sys_goldengate_port_set_isolation(uint8 lchip, ctc_port_isolation_t* p_port_isolation)
{
    uint32 cmd = 0;
    uint32 isolation_group = 0;
    uint32 pbm = 0;
	uint32 field = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_isolation);

    if (p_port_isolation->mlag_en)
    {
        if (p_port_isolation->use_isolation_id)
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_mlag_isolation(lchip, p_port_isolation, TRUE));
        return CTC_E_NONE;
    }

    if (p_port_isolation->use_isolation_id)
    {
        if (0 == p_port_isolation->gport || 1 == p_port_isolation->pbm[0])
        {
            return CTC_E_INVALID_PARAM;
        }
        isolation_group = p_port_isolation->gport;
        CTC_MAX_VALUE_CHECK(isolation_group, (SYS_GOLDENGATE_ISOLATION_GROUP_NUM - 1));
        pbm = p_port_isolation->pbm[0];

        p_gg_port_master[lchip]->use_isolation_id = 1;
    }

    PORT_LOCK;
    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_UNKNOW_UCAST))
    {
        field = DsPortIsolation_unknownUcIsolate_f;

		if (0 == p_port_isolation->use_isolation_id)
        {
            pbm = 0;

            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_isolation_id(lchip, p_port_isolation, field,  &isolation_group, &pbm));
        }

        cmd = DRV_IOW(DsPortIsolation_t, field);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, isolation_group, cmd, &pbm));

    }

    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_KNOW_UCAST))
    {

        field = DsPortIsolation_knownUcIsolate_f;
        if (0 == p_port_isolation->use_isolation_id)
        {
            pbm = 0;
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_isolation_id(lchip, p_port_isolation,  field, &isolation_group, &pbm));
        }
        cmd = DRV_IOW(DsPortIsolation_t, field);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, isolation_group, cmd, &pbm));
    }

    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_UNKNOW_MCAST))
    {

        field = DsPortIsolation_unknownMcIsolate_f;
		if (0 == p_port_isolation->use_isolation_id)
        {
            pbm = 0;
             CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_isolation_id(lchip, p_port_isolation,  field, &isolation_group, &pbm));
        }

        cmd = DRV_IOW(DsPortIsolation_t, field);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, isolation_group, cmd, &pbm));
    }

    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_KNOW_MCAST))
    {

        field = DsPortIsolation_knownMcIsolate_f;
        if (0 == p_port_isolation->use_isolation_id)
        {
            pbm = 0;
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_isolation_id(lchip, p_port_isolation,  field, &isolation_group, &pbm));
        }

        cmd = DRV_IOW(DsPortIsolation_t, field);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, isolation_group, cmd, &pbm));
    }


    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_BCAST))
    {

        field = DsPortIsolation_bcIsolate_f;

        if (0 == p_port_isolation->use_isolation_id)
        {
            pbm = 0;
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_isolation_id(lchip, p_port_isolation,  field, &isolation_group, &pbm));
        }
        cmd = DRV_IOW(DsPortIsolation_t, field);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, isolation_group, cmd, &pbm));
    }


    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_isolation(uint8 lchip, ctc_port_isolation_t* p_port_isolation)
{
    uint32 cmd_r = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_isolation);

    if (p_port_isolation->mlag_en)
    {
        uint16 drv_port = 0;
        uint32 field_val = 0;
        uint32 cmd = 0;
        uint8 i = 0;
        MetFifoPortStatusCtl_m     port_status;
        uint32 bitmap[4]=  {0};

        cmd = DRV_IOR(MetFifoReserved_t, MetFifoReserved_reserved_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        CTC_BIT_UNSET(field_val, 15);
        drv_port = field_val;
        p_port_isolation->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_port);

        if (1 == p_gg_port_master[lchip]->mlag_isolation_en)
        {
            /*Get egress port*/
            sal_memset(&port_status, 0, sizeof(port_status));

            cmd = DRV_IOR(MetFifoPortStatusCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_status));
            for (i = 0; i < 4; i++)
            {
                GetMetFifoPortStatusCtl(A, baseArray_0_portStatus_f + i, &port_status, bitmap);
                p_port_isolation->pbm[0 + i*2] = ~(bitmap[0]);
                p_port_isolation->pbm[1 + i*2] = ~(bitmap[1]);
                p_port_isolation->pbm[8 + i*2] = ~(bitmap[2]);
                p_port_isolation->pbm[9 + i*2] = ~(bitmap[3]|0xC0000000);
            }
        }

        return CTC_E_NONE;
    }




    if ( CTC_PORT_ISOLATION_ALL == p_port_isolation->isolation_pkt_type
        || CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_UNKNOW_UCAST))
    {
        cmd_r = DRV_IOR(DsPortIsolation_t, DsPortIsolation_unknownUcIsolate_f);
    }

    if (CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_KNOW_UCAST))
    {
        cmd_r = DRV_IOR(DsPortIsolation_t, DsPortIsolation_knownUcIsolate_f);
    }

    if (CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_UNKNOW_MCAST))
    {

        cmd_r = DRV_IOR(DsPortIsolation_t, DsPortIsolation_unknownMcIsolate_f);
    }

    if (CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_KNOW_MCAST))
    {
        cmd_r = DRV_IOR(DsPortIsolation_t, DsPortIsolation_knownMcIsolate_f);
    }

    if (CTC_FLAG_ISSET(p_port_isolation->isolation_pkt_type, CTC_PORT_ISOLATION_BCAST))
    {
        cmd_r = DRV_IOR(DsPortIsolation_t, DsPortIsolation_bcIsolate_f);
    }


    if (p_gg_port_master[lchip]->use_isolation_id)
    {
        uint32 isolation_group = 0;

        isolation_group = p_port_isolation->gport;
        CTC_MAX_VALUE_CHECK(isolation_group, (SYS_GOLDENGATE_ISOLATION_GROUP_NUM - 1));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, isolation_group, cmd_r, &p_port_isolation->pbm[0]));

		p_port_isolation->use_isolation_id = 1;
    }
    else
    {
        uint32 cmd = 0;
        uint32 isolation_group = 0;
        uint16 lport = 0;
        uint32 pbm = 0;
        uint8 bit = 0;

        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_port_isolation->gport, lchip, lport);

        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_srcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &isolation_group));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, isolation_group, cmd_r, &pbm));

        for (bit = 1; bit < 32; bit++)
        {
            if (!CTC_IS_BIT_SET(pbm, bit))
            {
                continue;
            }

            for (lport = 0; lport < 512; lport++)
            {
                if (p_gg_port_master[lchip]->egs_port_prop[lport].isolation_id == bit)
                {
                    CTC_BMP_SET(p_port_isolation->pbm, lport);
                }
            }
            p_port_isolation->use_isolation_id = 0;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_set_random_log_percent(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32 percent)
{

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port gport:0x%04X, direction:%d, percent:%d\n", \
                     gport, dir, percent);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(percent, 100);
    /* randomLogShift = 5 */
    if (100 > percent)
    {
        value = ((SYS_MAX_RANDOM_LOG_THRESHOD/100)/32) * percent;
    }
    else
    {
        value = 0x7FFF;
    }

    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_randomThreshold_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_randomThreshold_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_get_random_log_percent(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32* p_percent)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port gport:0x%04X, direction:%d\n", gport, dir);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_randomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        if (CTC_E_NONE == ret)
        {
            /* randomLogShift = 5 */
            *p_percent = value  / ((SYS_MAX_RANDOM_LOG_THRESHOD/100)/32);
        }
    }
    else if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_randomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        if (CTC_E_NONE == ret)
        {
            /* randomLogShift = 5 */
            *p_percent = value  / ((SYS_MAX_RANDOM_LOG_THRESHOD/100)/32);
        }
    }

    return ret;
}

STATIC int32
_sys_goldengate_port_set_random_log_rate(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32 rate)
{
    int32 ret = CTC_E_NONE;

    uint16 lport = 0;
    uint32 cmd = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port gport:0x%04X, direction:%d, rate:%d\n", \
                     gport, dir, rate);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    CTC_MAX_VALUE_CHECK(rate, SYS_MAX_RANDOM_LOG_RATE - 1);

    /* randomLogShift = 5 */
    rate = rate ;
    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_randomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &rate);
    }
    else if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_randomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &rate);
    }

    return ret;
}

/**
@brief  Set port's internal properties with direction according to gport id
*/
int32
sys_goldengate_port_set_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32 value)
{
    int32 ret = CTC_E_NONE;

    uint16 drv_lport = 0;
    uint32 cmd = 0;
    uint32 acl_en = 0;
    uint32 temp = value;
    uint32 valid = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property with direction, gport:0x%04X, property:%d, dir:%d,\
                                            value:%d\n", gport, port_prop, dir, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, drv_lport);

    PORT_LOCK;
    /*do write*/
    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            if (value > 3 && (0xff != value))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_svlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            value = (value) ? SYS_PORT_VLAN_FILTER_MODE_ENABLE : SYS_PORT_VLAN_FILTER_MODE_DISABLE;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ingressFilteringMode_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            value = (value) ? 1 : 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_randomLogEn_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_srcPortStatsEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_goldengate_port_set_random_log_percent(lchip, gport, CTC_INGRESS, value);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            ret = _sys_goldengate_port_set_random_log_rate(lchip, gport, CTC_INGRESS, value);
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            if (value > (CTC_ACL_EN_0 + CTC_ACL_EN_1 + CTC_ACL_EN_2 + CTC_ACL_EN_3))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_global_check_acl_memory(lchip, CTC_INGRESS, value));
            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclEn0_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_1);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclEn1_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_2);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclEn2_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_3);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclEn3_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &acl_en));

            if (0 == value)     /* if acl dis, set l2 label = 0 */
            {
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel0_f);
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel1_f);
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel2_f);
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel3_f);
            }
            else
            {
                cmd = 0;
            }
            break;
        case CTC_PORT_DIR_PROP_ACL_HASH_FIELD_SEL_ID:
            if (value > 0xF)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_aclFlowHashFieldSel_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
        case CTC_PORT_DIR_PROP_ACL_CLASSID_0:
            if ((value < SYS_GG_MIN_ACL_PORT_CLASSID) || (value > SYS_GG_MAX_ACL_PORT_CLASSID))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_1:
            if ((value < SYS_GG_MIN_ACL_PORT_CLASSID) || (value > SYS_GG_MAX_ACL_PORT_CLASSID))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel1_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_2:
            if ((value < SYS_GG_MIN_ACL_PORT_CLASSID) || (value > SYS_GG_MAX_ACL_PORT_CLASSID))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel2_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_3:
            if ((value < SYS_GG_MIN_ACL_PORT_CLASSID) || (value > SYS_GG_MAX_ACL_PORT_CLASSID))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLabel3_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            value = value ? 0 : 1;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_aclUseBitmap_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_HASH_LKUP_TYPE:
            if (value > CTC_ACL_HASH_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_aclFlowHashType_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            if (value > CTC_ACL_TCAM_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLookupType0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_1:
            if (value > CTC_ACL_TCAM_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLookupType1_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_2:
            if (value > CTC_ACL_TCAM_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLookupType2_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_3:
            if (value > CTC_ACL_TCAM_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_l2AclLookupType3_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_MAPPED_VLAN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_aclUsePIVlan_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_serviceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            if (value > SYS_GG_MAX_ACL_PORT_BITMAP_NUM -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_aclPortNum_f);
            break;
/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            if (value > 7)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_qosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_portPolicerValid_f);
            break;
        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
        }
    }

    value = temp;

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            if (value > 3 && (0xff != value))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            valid = (0xff == value) ? 0 : 1;
            value = (0xff == value) ? 0 : value;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_svlanTpidValid_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &valid));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_svlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            value = (value) ? SYS_PORT_VLAN_FILTER_MODE_ENABLE : SYS_PORT_VLAN_FILTER_MODE_DISABLE;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_egressFilteringMode_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            value = (value) ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_randomLogEn_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_destPortStatsEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_goldengate_port_set_random_log_percent(lchip, gport, CTC_EGRESS, value);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            ret = _sys_goldengate_port_set_random_log_rate(lchip, gport, CTC_EGRESS, value);
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            if (value > CTC_ACL_EN_0)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_global_check_acl_memory(lchip, CTC_EGRESS, value));
            acl_en = CTC_FLAG_ISSET(value, CTC_ACL_EN_0);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2AclEn0_f);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &acl_en));

            if (0 == value)     /* if acl dis, set l2 label = 0 */
            {
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2AclLabel0_f);
            }
            else
            {
                cmd = 0;
            }
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
        case CTC_PORT_DIR_PROP_ACL_CLASSID_0:
            if ((value < SYS_GG_MIN_ACL_PORT_CLASSID) || (value > SYS_GG_MAX_ACL_PORT_CLASSID))
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2AclLabel0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            if (((drv_lport&0xFF) >= (SYS_GG_MAX_ACL_PORT_BITMAP_NUM/2)) && (!value))     /*use bitmap only in 0~127 port*/
            {
                PORT_UNLOCK;
                return CTC_E_ACL_PORT_BITMAP_OVERFLOW;
            }

            value = value ? 0 : 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_aclUseBitmap_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_serviceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            if (value > CTC_ACL_TCAM_LKUP_TYPE_MAX -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2AclLookupType0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            if (value > SYS_GG_MAX_ACL_PORT_BITMAP_NUM -1)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_aclPortNum_f);
            break;
/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            if (value > 7)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_qosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_portPolicerValid_f);
            break;

        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, drv_lport, cmd, &value));
        }
    }

    PORT_UNLOCK;

    CTC_ERROR_RETURN(ret);

    return CTC_E_NONE;
}

/**
@brief  Get port's properties with direction according to gport id
*/
int32
sys_goldengate_port_get_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    int32  ret = CTC_E_NONE;
    uint32 acl_en = 0;
    uint8  inverse_value = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port property with direction, gport:0x%04X, property:%d, dir:%d" \
                     , gport, port_prop, dir);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    /*do read,only get value by CTC_INGRESS or CTC_EGRESS,no CTC_BOTH_DIRECTION*/
    if (CTC_INGRESS == dir)
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_svlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ingressFilteringMode_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_randomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_goldengate_port_get_random_log_percent(lchip, gport, dir, p_value);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_randomThreshold_f);
            /* randomLogShift = 5 */
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
            *p_value = *p_value ;
            cmd = 0;
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_0);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_1);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclEn2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_2);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclEn3_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
        case CTC_PORT_DIR_PROP_ACL_CLASSID_0:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLabel0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_1:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLabel1_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_2:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLabel2_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID_3:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLabel3_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            inverse_value = 1;
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_aclUseBitmap_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_HASH_FIELD_SEL_ID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_aclFlowHashFieldSel_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_serviceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_HASH_LKUP_TYPE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_aclFlowHashType_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLookupType0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_1:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLookupType1_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_2:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLookupType2_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_3:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_l2AclLookupType3_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_MAPPED_VLAN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_aclUsePIVlan_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_aclPortNum_f);
            break;
/********** ACL property end **********/
        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_qosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portPolicerValid_f);
            break;
        default:
            return CTC_E_INVALID_PARAM;

        }
    }
    else if (CTC_EGRESS == dir)
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_svlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_egressFilteringMode_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_randomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_goldengate_port_get_random_log_percent(lchip, gport, dir, p_value);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_randomThreshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
            /* randomLogShift = 5 */
            *p_value = *p_value;
            cmd = 0;
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_l2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_0);
            }

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_l2AclLabel0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            inverse_value = 1;
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_aclUseBitmap_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_serviceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_TCAM_LKUP_TYPE_0:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_l2AclLookupType0_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_aclPortNum_f);
            break;
/********** ACL property end **********/
        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_qosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_portPolicerValid_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(ret);

    if (cmd)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
    }

    if (inverse_value)
    {
        *p_value = !(*p_value);
    }

    return CTC_E_NONE;
}

/**
@brief  Config port's internal properties
*/
int32
sys_goldengate_port_set_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32 value)
{
    uint8  idx = 0;
    uint8  num = 1;

    uint16 lport = 0;
    uint32 cmd[2] = {0};

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port internal property, gport:0x%04X, property:%d, value:%d\n", \
                     gport, port_prop, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    PORT_LOCK;
    switch (port_prop)
    {
    case SYS_PORT_PROP_DEFAULT_PCP:
        cmd[0] = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultPcp_f);
        break;

    case SYS_PORT_PROP_DEFAULT_DEI:
        cmd[0] = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_defaultDei_f);
        break;

    case SYS_PORT_PROP_OAM_TUNNEL_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_oamTunnelEn_f);
        break;

    case SYS_PORT_PROP_OAM_VALID:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_etherOamValid_f);
        value = (value) ? 1 : 0;
        cmd[1] = DRV_IOW(DsDestPort_t, DsDestPort_etherOamValid_f);
        num++;
        break;

    case SYS_PORT_PROP_DISCARD_NONE_8023OAM_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestPort_t, DsDestPort_discardNon8023Oam_f);
        break;

    case SYS_PORT_PROP_REPLACE_TAG_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_defaultReplaceTagEn_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_EN:
        cmd[0] = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_exception2En_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_DISCARD:
        cmd[0] = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_exception2Discard_f);
        break;

    case SYS_PORT_PROP_SECURITY_EXCP_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_portSecurityExceptionEn_f);
        break;

    case SYS_PORT_PROP_SECURITY_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_portSecurityEn_f);
        break;

    case SYS_PORT_PROP_MAC_SECURITY_DISCARD:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_macSecurityDiscard_f);
        break;

    case SYS_PORT_PROP_REPLACE_DSCP_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestPort_t, DsDestPort_replaceDscp_f);
        break;

    case SYS_PORT_PROP_STMCTL_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_portStormCtlEn_f);
        break;

    /*new properties for GB*/
    case SYS_PORT_PROP_LINK_OAM_EN:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_linkOamEn_f);
        cmd[1] = DRV_IOW(DsDestPort_t, DsDestPort_linkOamEn_f);
        num++;
        break;

    case SYS_PORT_PROP_LINK_OAM_MEP_INDEX:
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_linkOamMepIndex_f);
        break;

    case SYS_PORT_PROP_SERVICE_POLICER_VALID:
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsDestPort_t, DsDestPort_servicePolicerValid_f);
        break;

    case SYS_PORT_PROP_SPEED_MODE:
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_speed_f);
        break;
    case SYS_PORT_PROP_PKT_TYPE:
        cmd[0] = DRV_IOW(DsPhyPort_t, DsPhyPort_packetType_f);
        DRV_IOCTL(lchip, lport, cmd[0], &value);
        value = (value) ? 1 : 0;
        cmd[0] = DRV_IOW(DsPhyPort_t, DsPhyPort_packetTypeValid_f);
        DRV_IOCTL(lchip, lport, cmd[0], &value);
        num = 0;
        break;
    case SYS_PORT_PROP_ILOOP_EN:
        {
            DsSrcPort_m src_port;
            DsPhyPortExt_m phy_port_ext;
            cmd[0] = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd[0], &src_port));
            value = value? 1 : 0;
            SetDsSrcPort(V, routeDisable_f, &src_port, value);
            SetDsSrcPort(V, stpDisable_f, &src_port, value);
            SetDsSrcPort(V, addDefaultVlanDisable_f, &src_port, value);
            cmd[0] = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd[0], &src_port));

            cmd[0] = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd[0], &phy_port_ext));
            value = value? 0 : 1;
            SetDsPhyPortExt(V, defaultVlanId_f, &phy_port_ext, value);
            cmd[0] = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd[0], &phy_port_ext));
            num = 0;
            break;
        }
    case SYS_PORT_PROP_HASH_GEN_DIS:
        cmd[0] = DRV_IOW(DsPhyPort_t, DsPhyPort_fwdHashGenDis_f);
        break;
    case SYS_PORT_PROP_LINK_OAM_LEVEL:
        cmd[0] = DRV_IOW(DsSrcPort_t, DsSrcPort_linkOamLevel_f);
        cmd[1] = DRV_IOW(DsDestPort_t, DsDestPort_linkOamLevel_f);
        num++;
        break;
    case SYS_PORT_PROP_VLAN_OPERATION_DIS:
        cmd[0] = DRV_IOW(DsDestPort_t, DsDestPort_ctagOperationDisable_f);
        cmd[1] = DRV_IOW(DsDestPort_t, DsDestPort_stagOperationDisable_f);
        num++;
        break;
    case SYS_PORT_PROP_UNTAG_DEF_VLAN_EN:
        cmd[0] = DRV_IOW(DsDestPort_t, DsDestPort_untagDefaultVlanId_f);
        break;
    default:
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    for (idx = 0; idx < num; idx++)
    {
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd[idx], &value));
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
@brief  Get port's internal properties according to gport id
*/
int32
sys_goldengate_port_get_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32* p_value)
{
    uint32 cmd = 0;

    uint16 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port internal property, gport:0x%04X, property:%d!\n", gport, port_prop);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    switch (port_prop)
    {
    case SYS_PORT_PROP_DEFAULT_DEI:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_defaultDei_f);
        break;

    case SYS_PORT_PROP_OAM_TUNNEL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_oamTunnelEn_f);
        break;

    case SYS_PORT_PROP_OAM_VALID:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_etherOamValid_f);
        break;

    case SYS_PORT_PROP_REPLACE_TAG_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_defaultReplaceTagEn_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_EN:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_exception2En_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_DISCARD:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_exception2Discard_f);
        break;

    case SYS_PORT_PROP_SECURITY_EXCP_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portSecurityExceptionEn_f);
        break;

    case SYS_PORT_PROP_SECURITY_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portSecurityEn_f);
        break;

    case SYS_PORT_PROP_MAC_SECURITY_DISCARD:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_macSecurityDiscard_f);
        break;

    case SYS_PORT_PROP_REPLACE_DSCP_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_replaceDscp_f);
        break;

    case SYS_PORT_PROP_STMCTL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portStormCtlEn_f);
        break;

    /*new properties for GB*/
    case SYS_PORT_PROP_LINK_OAM_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkOamEn_f);
        break;

    case SYS_PORT_PROP_LINK_OAM_MEP_INDEX:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkOamMepIndex_f);
        break;

    case SYS_PORT_PROP_SERVICE_POLICER_VALID:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_servicePolicerValid_f);
        break;

    case SYS_PORT_PROP_SPEED_MODE:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_speed_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

    return CTC_E_NONE;
}

/**
@brief  Set port's internal properties with direction according to gport id
*/
int32
sys_goldengate_port_set_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_direction_property_t port_prop, ctc_direction_t dir, uint32 value)
{

    uint16 lport = 0;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port internal property with direction, gport:0x%04X, property:%d, dir:%d,\
                                              value:%d\n", gport, port_prop, dir, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    PORT_LOCK;
    /*do write*/
    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_l2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_l2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_etherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_linkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            if (value > 7)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_linkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_linkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:

            if (value > 2047)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_linkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_etherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_mplsSectionLmEn_f);
            break;
        case SYS_PORT_DIR_PROP_IPFIX_LKUP_TYPE:
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ipfixHashType_f);
            break;
        case SYS_PORT_DIR_PROP_IPFIX_SELECT_ID:
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ipfixHashFieldSel_f);
            break;
        case SYS_PORT_DIR_PROP_OAM_VALID:
            value = (value) ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_etherOamValid_f);
            break;
        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;

        }

        if (cmd)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        }
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_l2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_etherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_linkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            if (value > 7)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_linkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            if (value > 3)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_linkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            if (value > 2047)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_linkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_etherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_mplsSectionLmEn_f);
            break;
        case SYS_PORT_DIR_PROP_IPFIX_LKUP_TYPE:
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_portFlowHashType_f);
            break;
        case SYS_PORT_DIR_PROP_IPFIX_SELECT_ID:
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_portFlowHashFieldSel_f);
            break;
        case SYS_PORT_DIR_PROP_OAM_VALID:
            value = (value) ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_etherOamValid_f);
            break;
        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &value));
        }
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
@brief  Get port's internal properties with direction according to gport id
*/
int32
sys_goldengate_port_get_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value)
{

    uint16 lport = 0;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port internal property with direction, gport:0x%04X, property:%d, dir:%d" \
                     , gport, port_prop, dir);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    /*do read,only get value by CTC_INGRESS or CTC_EGRESS,no CTC_BOTH_DIRECTION*/
    if (CTC_INGRESS == dir)
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_l2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_l2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_etherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_linkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_etherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_mplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;

        }
    }
    else if (CTC_EGRESS == dir)
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_l2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_l2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_etherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_linkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_linkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_linkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_linkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_etherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_mplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_mirror_en(uint8 lchip, uint16 lport, uint16 fwd_ptr)
{
    uint32 cmd = 0;
    DsPhyPortExt_m phy_port_ext;
    DsPhyPort_m phy_port;
    DsSrcPort_m src_port;

    PORT_LOCK;
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &src_port));

    SetDsSrcPort(V, portCrossConnect_f, &src_port, 1);
    SetDsSrcPort(V, receiveDisable_f, &src_port, 0);
    SetDsSrcPort(V, bridgeEn_f, &src_port, 0);
    SetDsSrcPort(V, addDefaultVlanDisable_f, &src_port, 1);
    SetDsSrcPort(V, routeDisable_f, &src_port, 1);

    cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &src_port));

    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

    SetDsPhyPortExt(V, u1_g2_dsFwdPtr_f, &phy_port_ext, fwd_ptr);
    SetDsPhyPortExt(V, defaultVlanId_f, &phy_port_ext, 0);

    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &phy_port));

    SetDsPhyPort(V, packetTypeValid_f, &phy_port, 1);
    SetDsPhyPort(V, packetType_f, &phy_port, 7);
    SetDsPhyPort(V, fwdHashGenDis_f, &phy_port, 1);

    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &phy_port));
    PORT_UNLOCK;
    return CTC_E_NONE;
}
/**
@brief  Set logic port check enable/disable
*/
int32
sys_goldengate_set_logic_port_check_en(uint8 lchip, bool enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set logic port check enable:%d\n", enable);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    p_gg_port_master[lchip]->use_logic_port_check = (enable == TRUE) ? 1 : 0;
    return CTC_E_NONE;
}

/**
@brief  Get logic port check enable/disable
*/
int32
sys_goldengate_get_logic_port_check_en(uint8 lchip, bool* p_enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    *p_enable = p_gg_port_master[lchip]->use_logic_port_check ? TRUE : FALSE;

    return CTC_E_NONE;
}

/**
@brief  Set ipg size
*/
int32
sys_goldengate_set_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8 size)
{
    uint32 cmd_i = 0;
    uint32 cmd_e = 0;
    uint32 field_val = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipg size index:%d ipg size:%d\n", index, size);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    field_val = size;

    switch (index)
    {
    case CTC_IPG_SIZE_0:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_array_0_ipg_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_array_0_ipg_f);
        break;

    case CTC_IPG_SIZE_1:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_array_1_ipg_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_array_1_ipg_f);
        break;

    case CTC_IPG_SIZE_2:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_array_2_ipg_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_array_2_ipg_f);
        break;

    case CTC_IPG_SIZE_3:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_array_3_ipg_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_array_3_ipg_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_i, &field_val));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_e, &field_val));

    return CTC_E_NONE;
}

/**
@brief  Get ipg size
*/
int32
sys_goldengate_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipg size index:%d\n", index);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_size);

    switch (index)
    {
    case CTC_IPG_SIZE_0:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_array_0_ipg_f);
        break;

    case CTC_IPG_SIZE_1:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_array_1_ipg_f);
        break;

    case CTC_IPG_SIZE_2:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_array_2_ipg_f);
        break;

    case CTC_IPG_SIZE_3:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_array_3_ipg_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *p_size = field_val;

    return CTC_E_NONE;
}

/**
@brief  Set Port MAC PostFix
*/
int32
sys_goldengate_port_set_port_mac_postfix(uint8 lchip, uint16 gport, ctc_port_mac_postfix_t* p_port_mac)
{

    uint16 lport     = 0;
    uint32 cmd       = 0;
    uint32 field_val = 0;
    uint8 slice_id   = 0;
    DsPhyPort_m ds_phy_port;
    DsPortProperty_m ds_port_property;
    DsEgressPortMac_m        dsportmac;
    hw_mac_addr_t mac;
    int32 ret = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_mac);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    slice_id = SYS_MAP_DRV_LPORT_TO_SLICE(lport);

    if (CTC_PORT_MAC_PREFIX_48BIT != p_port_mac->prefix_type)
    {
        return CTC_E_NOT_SUPPORT;
    }

    SYS_GOLDENGATE_SET_HW_MAC(mac, p_port_mac->port_mac);

    PORT_LOCK;
    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));
    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port), ret, error);
    SetDsPhyPort(A, portMac_f, &ds_phy_port,  mac);
    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port), ret, error);

    sal_memset(&ds_port_property, 0, sizeof(ds_port_property));
    cmd = DRV_IOR(DsPortProperty_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_port_property), ret, error);
    SetDsPortProperty(A, macSaByte_f, &ds_port_property,  mac);
    cmd = DRV_IOW(DsPortProperty_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_port_property), ret, error);

    field_val = (mac[1]>>8);
    cmd = DRV_IOW(NetTxMiscCtl0_t + slice_id , NetTxMiscCtl0_pausePortMac47_40_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, error);
    field_val = ((mac[1]&0xFF)<<24|(mac[0]>>8));
    cmd = DRV_IOW(NetTxMiscCtl0_t + slice_id, NetTxMiscCtl0_pausePortMac39_8_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, error);

    if((lport&0xFF) < 64)
    {
        field_val = (mac[0]&0xFF);
        cmd = DRV_IOW(NetTxPauseMac0_t + slice_id, NetTxPauseMac0_portMacLabel_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport&0xFF, cmd, &field_val), ret, error);
    }

    sal_memset(&dsportmac, 0, sizeof(dsportmac));
    SetDsEgressPortMac(A, portMac_f, &dsportmac, mac);
    cmd = DRV_IOW(DsEgressPortMac_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, lport, cmd, &dsportmac);

    cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_globalSrcPort_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &field_val), ret, error);
    if (SYS_DRV_IS_LINKAGG_PORT(field_val)) /*only for OAM when linkagg, index base is 448*/
    {
        lport = 448 + (field_val&0x3F);
        sal_memset(&ds_port_property, 0, sizeof(ds_port_property));
        cmd = DRV_IOR(DsPortProperty_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_port_property), ret, error);
        SetDsPortProperty(A, macSaByte_f, &ds_port_property,  mac);
        cmd = DRV_IOW(DsPortProperty_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, lport, cmd, &ds_port_property), ret, error);
    }

error:
    PORT_UNLOCK;

    return ret;
}

int32
sys_goldengate_port_set_if_status(uint8 lchip, uint16 lport, uint32 value)
{
    uint32 cmd = 0;
    int32 ret = 0;

    PORT_LOCK;
    cmd = DRV_IOW(DsPortProperty_t, DsPortProperty_ifStatus_f);
    ret = (DRV_IOCTL(lchip, lport, cmd, &value));
    PORT_UNLOCK;

    return ret;
}
/**
@brief  Get Port MAC
*/
int32
sys_goldengate_port_get_port_mac(uint8 lchip, uint16 gport, mac_addr_t* p_port_mac)
{

    uint16 lport = 0;
    uint32 cmd  = 0;
    DsPhyPort_m ds_phy_port;
    hw_mac_addr_t hw_mac;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_mac);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    sal_memset(&ds_phy_port, 0, sizeof(DsPhyPort_m));
    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port));

    sal_memset(&hw_mac, 0, sizeof(hw_mac_addr_t));
    GetDsPhyPort(A, portMac_f, &ds_phy_port, hw_mac);

    SYS_GOLDENGATE_SET_USER_MAC(*p_port_mac, hw_mac);

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_reflective_bridge_en(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    PORT_LOCK;
    field_val = (TRUE == enable) ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_portCheckEn_f);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = (TRUE == enable) ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_reflectiveBridgeEn_f);
    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &field_val));
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_reflective_bridge_en(uint8 lchip, uint16 gport, bool* p_enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val1 = 0;
    uint32 field_val2 = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    *p_enable = FALSE;

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_portCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val1));

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_reflectiveBridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val2));

    *p_enable = ((!field_val1) && field_val2) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_link_status_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    if (p_gg_port_master[lchip])
    {
        p_gg_port_master[lchip]->link_status_change_cb = cb;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_port_isr_cl73_complete_isr(uint8 lchip, void* p_data, void* p_data1)
{
    uint16 gport = 0;
    uint16 lport = 0;
    uint8  cl73_ok = 0;
    uint8  serdes_id = 0;
    uint8  gchip = 0;
    uint32 remote_ability = 0;
    uint32 local_ability = 0;
    uint32 ability = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    cl73_ok = *((uint8*)p_data1);
    serdes_id = *((uint8*)p_data);

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Enter %s\n", __FUNCTION__);
    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, cl73_ok:%d\n", serdes_id, cl73_ok);

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    /* get port info from sw table */
    sys_goldengate_get_gchip_id(lchip, &gchip);
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_gport_with_serdes(lchip, serdes_id, &gport));

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    PORT_LOCK;
    port_attr->reset_app = 0;
    port_attr->switch_num = 0;
    if(cl73_ok)
    {
        if (0 == port_attr->is_first)
        {
            /* 1. recover bypass, get ability */
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_get_cl73_ability(lchip, gport, 0, &local_ability));
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_get_cl73_ability(lchip, gport, 1, &remote_ability));
            ability = (local_ability&remote_ability);
            if (CTC_FLAG_ISSET(ability, SYS_PORT_CL73_100GBASE_KR4)
                || CTC_FLAG_ISSET(ability, SYS_PORT_CL73_100GBASE_CR4))
            {
                /* Note: recover CG mode port */
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_cl73_dynamic_switch(lchip, lport, 0));
                if (CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_init_mode)
                {
                    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_cgmac_en(lchip, port_attr->slice_id, port_attr->mac_id, TRUE));
                    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_pause_speed_mode(lchip, port_attr->slice_id, port_attr->mac_id, port_attr->speed_mode));
                }
            }
            else if (CTC_FLAG_ISSET(ability, SYS_PORT_CL73_40GBASE_KR4)
                || CTC_FLAG_ISSET(ability, SYS_PORT_CL73_40GBASE_CR4))
            {
                /*1. auto enable have swith to 40G already*/

                /*2. cfg pcs*/
                if (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_init_mode)
                {
                    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_xqmac_xlg_en(lchip, port_attr->slice_id, port_attr->mac_id, TRUE));
                    CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_pause_speed_mode(lchip, port_attr->slice_id, port_attr->mac_id, port_attr->speed_mode));
                }
            }
            else if (CTC_FLAG_ISSET(ability, SYS_PORT_CL73_10GBASE_KR))
            {
                /*2. cfg pcs*/
            }
            else
            {
                PORT_UNLOCK;
                return CTC_E_NONE;
            }

            SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"ability:0x%X\n", remote_ability);

            /* 2. set aec rx enable */
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_auto_neg_bypass_en(lchip, gport, 1));
            /*CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_cl73_aec_rx_en(lchip, gport, TRUE));*/  /*modify*/
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_set_dfe_en(lchip, gport, 1));

            /* 3. FEC enable */
            /*if(CTC_FLAG_ISSET(ability, CTC_PORT_CL73_FEC_ABILITY)&& CTC_FLAG_ISSET(ability, CTC_PORT_CL73_FEC_REQUESTED))
            {
                CTC_ERROR_RETURN(_sys_goldengate_port_set_fec_en(lchip, gport, TRUE));
            }*/

            /* 4. 3ap enable and init */
            if(CTC_FLAG_ISSET(remote_ability, SYS_PORT_CL73_10GBASE_KR) || CTC_FLAG_ISSET(remote_ability, SYS_PORT_CL73_40GBASE_KR4)||
               CTC_FLAG_ISSET(remote_ability, SYS_PORT_CL73_40GBASE_CR4)|| CTC_FLAG_ISSET(remote_ability, SYS_PORT_CL73_100GBASE_CR4)||
               CTC_FLAG_ISSET(remote_ability, SYS_PORT_CL73_100GBASE_KR4))
            {

                /* 3ap enable */
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_EN, 1));

                /* 3ap init */
                CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_INIT, 1));

                /* 3ap done */
                 /*CTC_ERROR_RETURN(sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DONE));*/
            }

            /*5. Do pcs release*/
            _sys_goldengate_port_set_pcs_reset(lchip, port_attr, 0, 0);
            _sys_goldengate_port_set_pcs_reset(lchip, port_attr, 1, 0);
            p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status = 2;
            port_attr->is_first = 1;
        }
    }
    else
    {
        /* Note: recover CG mode port */
        if (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_init_mode)
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(_sys_goldengate_port_cl73_dynamic_switch(lchip, lport, 0));
        }
        p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status = 1;
    }
    PORT_UNLOCK;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "Exit %s\n", __FUNCTION__);

    return CTC_E_NONE;
}
STATIC void
_sys_goldengate_port_scl_type(uint8 lchip, uint8 dir, sys_scl_key_mapping_type_t mapping_type, sys_scl_type_para_t* p_scl_type_para)
{
    uint8 i = 0;

    sys_scl_type_map_t scl_hash_type_map[][CTC_PORT_IGS_SCL_HASH_TYPE_MAX] =
    {
        {
             {0, 0, 0, USERIDPORTHASHTYPE_DISABLE,        CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE         },
             {1, 0, 0, USERIDPORTHASHTYPE_DOUBLEVLANPORT, CTC_PORT_IGS_SCL_HASH_TYPE_PORT_2VLAN      },
             {1, 0, 0, USERIDPORTHASHTYPE_SVLANPORT,      CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN      },
             {1, 0, 0, USERIDPORTHASHTYPE_CVLANPORT,      CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN      },
             {1, 0, 0, USERIDPORTHASHTYPE_SVLANCOSPORT,   CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN_COS  },
             {1, 0, 0, USERIDPORTHASHTYPE_CVLANCOSPORT,   CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN_COS  },
             {1, 0, 1, USERIDPORTHASHTYPE_MAC,            CTC_PORT_IGS_SCL_HASH_TYPE_MAC_SA          },
             {1, 0, 0, USERIDPORTHASHTYPE_MACPORT,        CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_SA     },
             {1, 1, 1, USERIDPORTHASHTYPE_MAC,            CTC_PORT_IGS_SCL_HASH_TYPE_MAC_DA          },
             {1, 1, 0, USERIDPORTHASHTYPE_MACPORT,        CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_DA     },
             {1, 0, 1, USERIDPORTHASHTYPE_IPSA,           CTC_PORT_IGS_SCL_HASH_TYPE_IP_SA           },
             {1, 0, 0, USERIDPORTHASHTYPE_IPSAPORT,       CTC_PORT_IGS_SCL_HASH_TYPE_PORT_IP_SA      },
             {1, 0, 0, USERIDPORTHASHTYPE_PORT,           CTC_PORT_IGS_SCL_HASH_TYPE_PORT            },
             {1, 0, 0, USERIDPORTHASHTYPE_SCLFLOW,        CTC_PORT_IGS_SCL_HASH_TYPE_L2              },
             {1, 0, 0, USERIDPORTHASHTYPE_TUNNEL,         CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL          },
             {1, 0, 0, USERIDPORTHASHTYPE_TUNNEL,         CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF      },
             {1, 0, 0, USERIDPORTHASHTYPE_TUNNEL,         CTC_PORT_IGS_SCL_HASH_TYPE_IPV4_TUNNEL_AUTO},
             {1, 0, 0, USERIDPORTHASHTYPE_TUNNEL,         CTC_PORT_IGS_SCL_HASH_TYPE_NVGRE           },
             {1, 0, 0, USERIDPORTHASHTYPE_TUNNEL,         CTC_PORT_IGS_SCL_HASH_TYPE_VXLAN           },
             {1, 0, 0, USERIDPORTHASHTYPE_TRILL,          CTC_PORT_IGS_SCL_HASH_TYPE_TRILL           }
        },
        {
            {0, 0, 0, EGRESSXCOAMHASHTYPE_DISABLE,       CTC_PORT_EGS_SCL_HASH_TYPE_DISABLE         },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_DOUBLEVLANPORT,CTC_PORT_EGS_SCL_HASH_TYPE_PORT_2VLAN      },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_SVLANPORT,     CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN      },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_CVLANPORT,     CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN      },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_SVLANCOSPORT,  CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN_COS  },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_CVLANCOSPORT,  CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN_COS  },
            {0, 0, 0, EGRESSXCOAMHASHTYPE_PORT,          CTC_PORT_EGS_SCL_HASH_TYPE_PORT            }
        }
    };

    sys_scl_type_map_t scl_tcam_type_map[CTC_PORT_IGS_SCL_TCAM_TYPE_MAX] =
    {
        {0, 0, 0, 0xFF,        CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE   },
        {1, 0, 1, TCAML2KEY,   CTC_PORT_IGS_SCL_TCAM_TYPE_MAC       },
        {1, 0, 1, TCAML2L3KEY, CTC_PORT_IGS_SCL_TCAM_TYPE_IP        },
        {1, 0, 1, TCAML3KEY,   CTC_PORT_IGS_SCL_TCAM_TYPE_IP_SINGLE },
        {1, 0, 0, TCAMVLANKEY, CTC_PORT_IGS_SCL_TCAM_TYPE_VLAN      },
        {1, 0, 0, TCAML3KEY,   CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL    },
        {1, 0, 0, TCAML3KEY,   CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL_RPF}
    };

    if (SYS_PORT_KEY_MAPPING_TYPE_DRV2CTC == mapping_type)
    {
        if (CTC_INGRESS == dir)
        {
            p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE;
            if (USERIDPORTHASHTYPE_DISABLE == p_scl_type_para->drv_hash_type)
            {
                p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE;
            }
            else if (USERIDPORTHASHTYPE_TUNNEL != p_scl_type_para->drv_hash_type)
            {
                for (i = 0; i < CTC_PORT_IGS_SCL_HASH_TYPE_MAX; i++)
                {
                    if ((scl_hash_type_map[CTC_INGRESS][i].drv_type == p_scl_type_para->drv_hash_type)
                        && (USERIDPORTHASHTYPE_TRILL == p_scl_type_para->drv_hash_type))
                    {
                        p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_TRILL;
                    }
                    else if ((scl_hash_type_map[CTC_INGRESS][i].drv_type == p_scl_type_para->drv_hash_type)
                       && (scl_hash_type_map[CTC_INGRESS][i].use_macda == p_scl_type_para->use_macda))
                    {
                        p_scl_type_para->ctc_hash_type = scl_hash_type_map[CTC_INGRESS][i].ctc_type;
                        break;
                    }
                }
            }
            else
            {
                if (p_scl_type_para->tunnel_en)
                {
                    p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL;
                }

                if (p_scl_type_para->tunnel_en && p_scl_type_para->tunnel_rpf_check)
                {
                    p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF;
                }

                if (p_scl_type_para->auto_tunnel)
                {
                    p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_IPV4_TUNNEL_AUTO;
                }

                if (p_scl_type_para->nvgre_en)
                {
                    p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_NVGRE;
                }

                if (p_scl_type_para->vxlan_en)
                {
                    p_scl_type_para->ctc_hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_VXLAN;
                }
            }

            if (p_scl_type_para->tcam_en)
            {
                for (i = 0; i < CTC_PORT_IGS_SCL_TCAM_TYPE_MAX; i++)
                {
                    if (scl_tcam_type_map[i].drv_type == p_scl_type_para->drv_tcam_type)
                    {
                        p_scl_type_para->ctc_tcam_type = scl_tcam_type_map[i].ctc_type;
                        break;
                    }
                }
            }
        }
        else if (CTC_EGRESS == dir)
        {
            for (i = 0; i < CTC_PORT_EGS_SCL_HASH_TYPE_MAX; i++)
            {
                if (scl_hash_type_map[CTC_EGRESS][i].drv_type == p_scl_type_para->drv_hash_type)
                {
                    p_scl_type_para->ctc_hash_type = scl_hash_type_map[CTC_EGRESS][i].ctc_type;
                    break;
                }
            }
            p_scl_type_para->drv_tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE;
        }
    }
    else if (SYS_PORT_KEY_MAPPING_TYPE_CTC2DRV == mapping_type)
    {
        p_scl_type_para->drv_hash_type = scl_hash_type_map[dir][p_scl_type_para->ctc_hash_type].drv_type;

        if (CTC_INGRESS == dir)
        {
            p_scl_type_para->drv_hash_type = scl_hash_type_map[dir][p_scl_type_para->ctc_hash_type].drv_type;
            p_scl_type_para->use_macda = scl_hash_type_map[dir][p_scl_type_para->ctc_hash_type].use_macda;
            p_scl_type_para->vlan_high_priority = scl_hash_type_map[dir][p_scl_type_para->ctc_hash_type].vlan_high_priority;
            if (CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE == p_scl_type_para->ctc_tcam_type)
            {
                p_scl_type_para->tcam_en = 0;
            }
            else
            {
                p_scl_type_para->tcam_en = 1;
                p_scl_type_para->drv_tcam_type = scl_tcam_type_map[p_scl_type_para->ctc_tcam_type].drv_type;
            }
        }
        else if (CTC_EGRESS == dir)
        {
            p_scl_type_para->drv_hash_type = scl_hash_type_map[dir][p_scl_type_para->ctc_hash_type].drv_type;
            p_scl_type_para->tcam_en = 0;
        }
    }
}

STATIC int32
_sys_goldengate_port_unmap_scl_type(uint8 lchip, uint8 dir, sys_scl_type_para_t* p_scl_type_para)
{
    if (CTC_INGRESS == dir)
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_hash_type, USERIDPORTHASHTYPE_TRILL);
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_tcam_type, TCAMVLANKEY);
    }
    else
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_hash_type, EGRESSXCOAMHASHTYPE_RMEP);
    }

    _sys_goldengate_port_scl_type(lchip, dir, SYS_PORT_KEY_MAPPING_TYPE_DRV2CTC, p_scl_type_para);

    if (CTC_INGRESS == dir)
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_hash_type, CTC_PORT_IGS_SCL_HASH_TYPE_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_tcam_type, CTC_PORT_IGS_SCL_TCAM_TYPE_MAX - 1);
    }
    else
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_hash_type, CTC_PORT_EGS_SCL_HASH_TYPE_MAX - 1);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_map_scl_type(uint8 lchip, uint8 dir, sys_scl_type_para_t* p_scl_type_para)
{
    if (CTC_INGRESS == dir)
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_hash_type, CTC_PORT_IGS_SCL_HASH_TYPE_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_tcam_type, CTC_PORT_IGS_SCL_TCAM_TYPE_MAX - 1);
    }
    else
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->ctc_hash_type, CTC_PORT_EGS_SCL_HASH_TYPE_MAX - 1);
    }

    _sys_goldengate_port_scl_type(lchip, dir, SYS_PORT_KEY_MAPPING_TYPE_CTC2DRV, p_scl_type_para);

    if (CTC_INGRESS == dir)
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_hash_type, USERIDPORTHASHTYPE_TRILL);
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_tcam_type, TCAMVLANKEY);
    }
    else
    {
        CTC_MAX_VALUE_CHECK(p_scl_type_para->drv_hash_type, EGRESSXCOAMHASHTYPE_RMEP);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* p_prop)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint8 ipv4_tunnel_en = 0;
    uint8 ipv4_gre_tunnel_en = 0;
    uint8 auto_tunnel = 0;
    uint8 v4_vxlan_en = 0;
    uint8 v6_nvgre_en = 0;
    uint8 v6_vxlan_en = 0;
    uint8 rpf_check_en = 0;
    uint32 vlan_high_priority = 0;
    sys_scl_type_para_t scl_type_para;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(p_prop->direction, (CTC_BOTH_DIRECTION - 1));
    CTC_MAX_VALUE_CHECK(p_prop->scl_id, 1);
    CTC_MAX_VALUE_CHECK(p_prop->class_id, SYS_GG_MAX_SCL_PORT_CLASSID);

    sal_memset(&scl_type_para, 0, sizeof(sys_scl_type_para_t));
    scl_type_para.ctc_hash_type = p_prop->hash_type;
    scl_type_para.ctc_tcam_type = p_prop->tcam_type;
    CTC_ERROR_RETURN(_sys_goldengate_port_map_scl_type(lchip, p_prop->direction, &scl_type_para));

    PORT_LOCK;
    if (CTC_INGRESS == p_prop->direction)
    {
        DsPhyPortExt_m ds;
        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds));

        if (CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL == p_prop->hash_type)
        {
            ipv4_gre_tunnel_en = 1;
            ipv4_tunnel_en = 1;
        }
        else if (CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF == p_prop->hash_type)
        {
            ipv4_gre_tunnel_en = 1;
            ipv4_tunnel_en = 1;
            rpf_check_en = 1;
        }
        else if (CTC_PORT_IGS_SCL_HASH_TYPE_IPV4_TUNNEL_AUTO == p_prop->hash_type)
        {
            ipv4_gre_tunnel_en = 1;
            ipv4_tunnel_en = 1;
            auto_tunnel = 1;
        }
        else if (CTC_PORT_IGS_SCL_HASH_TYPE_NVGRE == p_prop->hash_type)
        {
            ipv4_gre_tunnel_en = 1;
            v6_nvgre_en = 1;
        }
        else if (CTC_PORT_IGS_SCL_HASH_TYPE_VXLAN == p_prop->hash_type)
        {
            v4_vxlan_en = 1;
            v6_vxlan_en = 1;
        }

        if (0 == p_prop->scl_id)
        {
            SetDsPhyPortExt(V, userIdPortHash1Type_f, &ds, scl_type_para.drv_hash_type);
            SetDsPhyPortExt(V, hash1UseDa_f, &ds, scl_type_para.use_macda);
            SetDsPhyPortExt(V, userIdTcam1En_f, &ds, scl_type_para.tcam_en);
            SetDsPhyPortExt(V, userIdTcam1Type_f, &ds, scl_type_para.drv_tcam_type);
            SetDsPhyPortExt(V, hashLookup1UseLabel_f , &ds, (p_prop->class_id_en ? 1 : 0));
            SetDsPhyPortExt(V, userIdLabel1_f, &ds, p_prop->class_id);
            SetDsPhyPortExt(V, userId1UseLogicPort_f , &ds, (p_prop->use_logic_port_en ? 1 : 0));
            SetDsPhyPortExt(V, tcamUseLogicPort_f , &ds, (p_prop->use_logic_port_en ? 1 : 0));

            SetDsPhyPortExt(V, ipv4TunnelHashEn1_f, &ds, ipv4_tunnel_en);
            SetDsPhyPortExt(V, ipv4GreTunnelHashEn1_f, &ds, ipv4_gre_tunnel_en);
            SetDsPhyPortExt(V, autoTunnelEn_f, &ds, auto_tunnel);
            SetDsPhyPortExt(V, autoTunnelForHash1_f, &ds, auto_tunnel);
            SetDsPhyPortExt(V, ipv4VxlanTunnelHashEn1_f, &ds, v4_vxlan_en);
            SetDsPhyPortExt(V, ipv6NvgreTunnelHashEn1_f, &ds, v6_nvgre_en);
            SetDsPhyPortExt(V, ipv6VxlanTunnelHashEn1_f, &ds, v6_vxlan_en);
        }
        else
        {
            if(CTC_PORT_IGS_SCL_HASH_TYPE_L2 == p_prop->hash_type)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_PARAM;
            }

            SetDsPhyPortExt(V, userIdPortHash2Type_f, &ds, scl_type_para.drv_hash_type);
            SetDsPhyPortExt(V, hash2UseDa_f, &ds, scl_type_para.use_macda);
            SetDsPhyPortExt(V, userIdTcam2En_f, &ds, scl_type_para.tcam_en);
            SetDsPhyPortExt(V, userIdTcam2Type_f, &ds, scl_type_para.drv_tcam_type);
            SetDsPhyPortExt(V, hashLookup2UseLabel_f , &ds, (p_prop->class_id_en ? 1 : 0));
            SetDsPhyPortExt(V, userIdLabel2_f, &ds, p_prop->class_id);
            SetDsPhyPortExt(V, userId2UseLogicPort_f , &ds, (p_prop->use_logic_port_en ? 1 : 0));
            SetDsPhyPortExt(V, tcamUseLogicPort_f , &ds, (p_prop->use_logic_port_en ? 1 : 0));

            SetDsPhyPortExt(V, ipv4TunnelHashEn2_f, &ds, ipv4_tunnel_en);
            SetDsPhyPortExt(V, ipv4GreTunnelHashEn2_f, &ds, ipv4_gre_tunnel_en);
            SetDsPhyPortExt(V, autoTunnelEn_f, &ds, auto_tunnel);
            SetDsPhyPortExt(V, tunnelRpfCheck_f, &ds, rpf_check_en);
            SetDsPhyPortExt(V, autoTunnelForHash1_f, &ds, !auto_tunnel);
            SetDsPhyPortExt(V, ipv4VxlanTunnelHashEn2_f, &ds, v4_vxlan_en);
            SetDsPhyPortExt(V, ipv6NvgreTunnelHashEn2_f, &ds, v6_nvgre_en);
            SetDsPhyPortExt(V, ipv6VxlanTunnelHashEn2_f, &ds, v6_vxlan_en);
        }

        switch (p_prop->action_type)
        {
            case CTC_PORT_SCL_ACTION_TYPE_SCL:
                if (0 == p_prop->scl_id)
                {
                    SetDsPhyPortExt(V, tcam1IsUserId_f, &ds, 1);
                    SetDsPhyPortExt(V, tcam1IsSclFlow_f, &ds, 0);
                    SetDsPhyPortExt(V, sclFlowHashEn_f, &ds, 0);
                }
                else
                {
                    SetDsPhyPortExt(V, tcam2IsUserId_f, &ds, 1);
                    SetDsPhyPortExt(V, tcam2IsSclFlow_f, &ds, 0);
                }
                break;
            case CTC_PORT_SCL_ACTION_TYPE_FLOW:
                if (0 == p_prop->scl_id)
                {
                    SetDsPhyPortExt(V, tcam1IsUserId_f, &ds, 0);
                    SetDsPhyPortExt(V, tcam1IsSclFlow_f, &ds, 1);
                    SetDsPhyPortExt(V, sclFlowHashEn_f, &ds, 1);
                }
                else
                {
                    if (p_prop->hash_type != CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE)
                    {
                        return CTC_E_INVALID_PARAM;
                    }
                    SetDsPhyPortExt(V, tcam2IsUserId_f, &ds, 0);
                    SetDsPhyPortExt(V, tcam2IsSclFlow_f, &ds, 1);
                }
                break;
            case CTC_PORT_SCL_ACTION_TYPE_TUNNEL:
                if (0 == p_prop->scl_id)
                {
                    SetDsPhyPortExt(V, tcam1IsUserId_f, &ds, 0);
                    SetDsPhyPortExt(V, tcam1IsSclFlow_f, &ds, 0);
                    SetDsPhyPortExt(V, sclFlowHashEn_f, &ds, 0);
                }
                else
                {
                    SetDsPhyPortExt(V, tcam2IsUserId_f, &ds, 0);
                    SetDsPhyPortExt(V, tcam2IsSclFlow_f, &ds, 0);
                }
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }

        vlan_high_priority = scl_type_para.vlan_high_priority;
        cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds));
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ingressTagHighPriority_f);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &vlan_high_priority));

        if(p_prop->class_id == CTC_PORT_CLASS_ID_RSV_IP_SRC_GUARD && \
            (p_prop->hash_type != CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE || p_prop->tcam_type != CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE))
        {
            CTC_ERROR_RETURN_WITH_PORT_UNLOCK(sys_goldengate_ip_source_guard_init_default_entry(lchip));
        }
    }
    else if (CTC_EGRESS == p_prop->direction)
    {
        DsDestPort_m ds;

        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds));

        if (0 == p_prop->scl_id)
        {
            SetDsDestPort(V, vlanHash1Type_f, &ds, scl_type_para.drv_hash_type);
        }
        else
        {
            SetDsDestPort(V, vlanHash2Type_f, &ds, scl_type_para.drv_hash_type);
            SetDsDestPort(V, vlanHash1DefaultEntryValid_f, &ds, (scl_type_para.drv_hash_type?0:1));
        }

        SetDsDestPort(V, vlanHashLabel_f, &ds, p_prop->class_id);
        SetDsDestPort(V, vlanHashUseLabel_f, &ds, (p_prop->class_id_en ? 1 : 0));
        SetDsDestPort(V, vlanHashUseLogicPort_f, &ds, (p_prop->use_logic_port_en ? 1 : 0));
        cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_PORT_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds));
    }
    PORT_UNLOCK;
    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* p_prop)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint8 tcam_is_userid = 0;
    uint8 tcam_is_scl_flow = 0;
    sys_scl_type_para_t scl_type_para;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_prop);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(p_prop->direction, CTC_BOTH_DIRECTION - 1);
    CTC_MAX_VALUE_CHECK(p_prop->scl_id, 1);

    sal_memset(&scl_type_para, 0, sizeof(sys_scl_type_para_t));

    if (CTC_INGRESS == p_prop->direction)
    {
        DsPhyPortExt_m ds;

        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds));

        if (0 == p_prop->scl_id)
        {
            scl_type_para.drv_hash_type = GetDsPhyPortExt(V, userIdPortHash1Type_f, &ds);
            scl_type_para.use_macda = GetDsPhyPortExt(V, hash1UseDa_f, &ds);
            scl_type_para.tcam_en = GetDsPhyPortExt(V, userIdTcam1En_f, &ds);
            scl_type_para.drv_tcam_type = GetDsPhyPortExt(V, userIdTcam1Type_f, &ds);
            scl_type_para.tunnel_en = GetDsPhyPortExt(V, ipv4TunnelHashEn1_f, &ds) && GetDsPhyPortExt(V, ipv4GreTunnelHashEn1_f, &ds);
            scl_type_para.auto_tunnel = GetDsPhyPortExt(V, autoTunnelEn_f, &ds);
            scl_type_para.auto_tunnel &= GetDsPhyPortExt(V, autoTunnelForHash1_f, &ds);
            /* nvgre full condition */
            scl_type_para.nvgre_en = GetDsPhyPortExt(V, nvgreEnable_f, &ds);
            scl_type_para.nvgre_en &= (GetDsPhyPortExt(V, ipv4GreTunnelHashEn1_f, &ds) || GetDsPhyPortExt(V, ipv6NvgreTunnelHashEn1_f, &ds));
            /* vxlan full condition */
            scl_type_para.vxlan_en = GetDsPhyPortExt(V, ipv4VxlanTunnelHashEn1_f, &ds) || GetDsPhyPortExt(V, ipv6VxlanTunnelHashEn1_f, &ds);
            p_prop->class_id_en = GetDsPhyPortExt(V, hashLookup1UseLabel_f , &ds);
            p_prop->class_id = GetDsPhyPortExt(V, userIdLabel1_f , &ds);
            p_prop->use_logic_port_en = GetDsPhyPortExt(V, userId1UseLogicPort_f , &ds);
            tcam_is_userid = GetDsPhyPortExt(V, tcam1IsUserId_f, &ds);
            tcam_is_scl_flow = GetDsPhyPortExt(V, tcam1IsSclFlow_f, &ds);
        }
        else
        {
            if(CTC_PORT_IGS_SCL_HASH_TYPE_L2 == p_prop->hash_type)
            {
                return CTC_E_INVALID_PARAM;
            }

            scl_type_para.drv_hash_type = GetDsPhyPortExt(V, userIdPortHash2Type_f, &ds);
            scl_type_para.use_macda = GetDsPhyPortExt(V, hash2UseDa_f, &ds);
            scl_type_para.tcam_en = GetDsPhyPortExt(V, userIdTcam2En_f, &ds);
            scl_type_para.drv_tcam_type = GetDsPhyPortExt(V, userIdTcam2Type_f, &ds);
            scl_type_para.tunnel_en = GetDsPhyPortExt(V, ipv4TunnelHashEn2_f, &ds) && GetDsPhyPortExt(V, ipv4GreTunnelHashEn2_f, &ds);
            scl_type_para.tunnel_rpf_check = scl_type_para.tunnel_en && GetDsPhyPortExt(V, tunnelRpfCheck_f, &ds);
            scl_type_para.auto_tunnel = GetDsPhyPortExt(V, autoTunnelEn_f, &ds);
            scl_type_para.auto_tunnel &= (!GetDsPhyPortExt(V, autoTunnelForHash1_f, &ds));
            /* nvgre full condition */
            scl_type_para.nvgre_en = GetDsPhyPortExt(V, nvgreEnable_f, &ds);
            scl_type_para.nvgre_en &= (GetDsPhyPortExt(V, ipv4GreTunnelHashEn2_f, &ds) || GetDsPhyPortExt(V, ipv6NvgreTunnelHashEn2_f, &ds));
            /* vxlan full condition */
            scl_type_para.vxlan_en = GetDsPhyPortExt(V, ipv4VxlanTunnelHashEn2_f, &ds) || GetDsPhyPortExt(V, ipv6VxlanTunnelHashEn2_f, &ds);
            p_prop->class_id_en = GetDsPhyPortExt(V, hashLookup2UseLabel_f , &ds);
            p_prop->class_id = GetDsPhyPortExt(V, userIdLabel2_f , &ds);
            p_prop->use_logic_port_en = GetDsPhyPortExt(V, userId2UseLogicPort_f , &ds);
            tcam_is_userid = GetDsPhyPortExt(V, tcam2IsUserId_f, &ds);
            tcam_is_scl_flow = GetDsPhyPortExt(V, tcam2IsSclFlow_f, &ds);
        }

        if (tcam_is_scl_flow)
        {
            p_prop->action_type = CTC_PORT_SCL_ACTION_TYPE_FLOW;
        }
        else if (tcam_is_userid)
        {
            p_prop->action_type = CTC_PORT_SCL_ACTION_TYPE_SCL;
        }
        else
        {
            p_prop->action_type = CTC_PORT_SCL_ACTION_TYPE_TUNNEL;
        }
    }
    else if (CTC_EGRESS == p_prop->direction)
    {
        DsDestPort_m ds;

        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds));

        if (0 == p_prop->scl_id)
        {
            scl_type_para.drv_hash_type = GetDsDestPort(V, vlanHash1Type_f, &ds);
        }
        else
        {
            scl_type_para.drv_hash_type = GetDsDestPort(V, vlanHash2Type_f, &ds);
        }

        p_prop->class_id = GetDsDestPort(V, vlanHashLabel_f, &ds);
        p_prop->class_id_en = GetDsDestPort(V, vlanHashUseLabel_f, &ds);
        p_prop->use_logic_port_en = GetDsDestPort(V, vlanHashUseLogicPort_f, &ds);
    }

    CTC_ERROR_RETURN(_sys_goldengate_port_unmap_scl_type(lchip, p_prop->direction, &scl_type_para));

    p_prop->hash_type = scl_type_para.ctc_hash_type;
    p_prop->tcam_type = scl_type_para.ctc_tcam_type;

    return CTC_E_NONE;
}


int32
sys_goldengate_port_set_service_policer_en(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;


    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    field_val = (TRUE == enable) ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_servicePolicerValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_isr_link_status_change_isr(uint8 lchip, void* p_data)
{
    uint16 gport = 0;
    uint8 gchip = 0;
    uint16 lport = 0;
    uint32 is_up = 0;
    uint32 train_status = 0;
    uint32 temp = 0;

    sys_datapath_lport_attr_t* port_attr = NULL;

    ctc_port_link_status_t link_status;

    SYS_PORT_INIT_CHECK(lchip);
    gport = *((uint16*)p_data);

    sys_goldengate_get_gchip_id(lchip, &gchip);

    link_status.gport = gport;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
    is_up = p_gg_port_master[lchip]->igs_port_prop[lport].link_status;  /*pre link status, 1: up->down, 0:down->up*/

    sys_goldengate_port_get_mac_link_up(lchip, gport, &temp, 0);

    PORT_LOCK;
    p_gg_port_master[lchip]->igs_port_prop[lport].link_status = temp;

    train_status = p_gg_port_master[lchip]->igs_port_prop[lport].training_status;
    /* 3ap training done, Note, this is used for cl73 auto-neg */
    if(SYS_PORT_3AP_TRAINING_INIT == train_status)
    {
        sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_DONE, 1);
    }

    train_status = p_gg_port_master[lchip]->igs_port_prop[lport].training_status;

    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ori link:%d, current link:%d cl73_status:%d, train:%d\n", is_up, temp,
        p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status, train_status);

    if (port_attr)
    {
        if (port_attr->port_type == SYS_DATAPATH_NETWORK_PORT)
        {
            /*check 40g or 100g*/
            if ((port_attr->speed_mode == CTC_PORT_SPEED_40G) || (port_attr->speed_mode == CTC_PORT_SPEED_100G))
            {
                /*check auto neg enable*/
                if (((p_gg_port_master[lchip]->igs_port_prop[lport].cl73_status == 2) || (train_status == SYS_PORT_3AP_TRAINING_DONE)) && is_up)
                {
                    SYS_CL73_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Do AutoNeg Restart! port:%d \n", gport);

                    /*do auto disable and enable to restart autoneg*/
                    _sys_goldengate_port_set_cl73_auto_neg_en(lchip, gport, 0);
                    _sys_goldengate_port_set_cl73_auto_neg_en(lchip, gport, 1);
                }
            }
        }
    }
    PORT_UNLOCK;

    if (p_gg_port_master[lchip]->link_status_change_cb)
    {
        p_gg_port_master[lchip]->link_status_change_cb(gchip, &link_status);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_show_port_maclink(uint8 lchip)
{
    uint8 i = 0;
    uint8 gchip_id = 0;
    uint16 lport = 0;
    uint16 gport = 0;
    uint16 drv_lport = 0;
    uint32 value_link_up = FALSE;
    uint32 value_mac_en = FALSE;
    uint32 value_32 = 0;
    char value_c[32] = {0};
    char* p_unit[2] = {NULL, NULL};
    int32 ret = 0;
    uint64 rate[CTC_STATS_MAC_STATS_MAX] = {0};
    sys_datapath_lport_attr_t* port_attr = NULL;
    char  str[50] = {0};
    char* bw_unit[] = {"bps", "Kbps", "Mbps", "Gbps", "Tbps"};

    SYS_PORT_INIT_CHECK(lchip);
    sal_task_sleep(100);

    SYS_PORT_DBG_DUMP("\n");

    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
    SYS_PORT_DBG_DUMP(" Show Local Chip %d Port Link:\n", lchip);

    SYS_PORT_DBG_DUMP(" ------------------------------------------------------------------------------------------\n");
    SYS_PORT_DBG_DUMP(" %-8s%-6s%-6s%-7s%-9s%-7s%-8s%-10s%-11s%-10s%s\n",
                      "GPort", "LPort", "MAC", "Link", "MAC-EN", "Speed", "Duplex", "Auto-Neg", "Interface", " Rx-Rate", " Tx-Rate");
    SYS_PORT_DBG_DUMP(" ------------------------------------------------------------------------------------------\n");

    for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
    {
        gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

        ret = sys_goldengate_common_get_port_capability(lchip, lport, &port_attr);
        if (ret < 0)
        {
            continue;
        }

        if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
        {
            continue;
        }

        ret = sys_goldengate_port_get_mac_en(lchip, gport, &value_mac_en);
        if (ret < 0)
        {
            continue;
        }

        ret = sys_goldengate_port_get_mac_link_up(lchip, gport, &value_link_up, 0);
        if (ret < 0 )
        {
            continue;
        }

        drv_lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);
        SYS_PORT_DBG_DUMP(" 0x%04X  %-6u", gport, drv_lport);
        SYS_PORT_DBG_DUMP("%-6u", (port_attr->mac_id + 64*port_attr->slice_id));
        if (value_link_up)
        {
            SYS_PORT_DBG_DUMP("%-7s", "up");
        }
        else
        {
            SYS_PORT_DBG_DUMP("%-7s", "down");
        }

        SYS_PORT_DBG_DUMP("%-9s", (value_mac_en ? "TRUE":"FALSE"));

        ret = sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_SPEED, (uint32*)&value_32);
        if (ret < 0)
        {
            SYS_PORT_DBG_DUMP("%-7s", "-");
        }
        else
        {
            sal_memset(value_c, 0, sizeof(value_c));
            if (CTC_PORT_SPEED_1G == value_32)
            {
                sal_memcpy(value_c, "1G", sal_strlen("1G"));
            }
            else if (CTC_PORT_SPEED_100M == value_32)
            {
                sal_memcpy(value_c, "100M", sal_strlen("100M"));
            }
            else if (CTC_PORT_SPEED_10M == value_32)
            {
                sal_memcpy(value_c, "10M", sal_strlen("10M"));
            }
            else if (CTC_PORT_SPEED_2G5 == value_32)
            {
                sal_memcpy(value_c, "2.5G", sal_strlen("2.5G"));
            }
            else if (CTC_PORT_SPEED_10G == value_32)
            {
                sal_memcpy(value_c, "10G", sal_strlen("10G"));
            }
            else if (CTC_PORT_SPEED_20G == value_32)
            {
                sal_memcpy(value_c, "20G", sal_strlen("20G"));
            }
            else if (CTC_PORT_SPEED_40G == value_32)
            {
                sal_memcpy(value_c, "40G", sal_strlen("10G"));
            }
            else if (CTC_PORT_SPEED_100G == value_32)
            {
                sal_memcpy(value_c, "100G", sal_strlen("100G"));
            }
            SYS_PORT_DBG_DUMP("%-7s", value_c);
        }

        SYS_PORT_DBG_DUMP("%-8s", "FD");

        ret = sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &value_32);
        if (ret < 0)
        {
            SYS_PORT_DBG_DUMP("%-10s", "-");
        }
        else
        {
            SYS_PORT_DBG_DUMP("%-10s", (value_32 ? "TRUE" : "FALSE"));
        }

        if (CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "SGMII");
        }
        else if (CTC_CHIP_SERDES_XFI_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "XFI");
        }
        else if (CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "XLG");
        }
        else if (CTC_CHIP_SERDES_XAUI_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "XAUI");
        }
        else if (CTC_CHIP_SERDES_DXAUI_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "DXAUI");
        }
        else if (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "CG");
        }
        else if (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode)
        {
            SYS_PORT_DBG_DUMP("%-11s", "2.5G");
        }
        else
        {
            SYS_PORT_DBG_DUMP("%-11s", "-");
        }

        sys_goldengate_stats_get_port_rate(lchip, gport, rate);

        for (i = 0; i < CTC_STATS_MAC_STATS_MAX; i++)
        {
            if (rate[i] < (1000ULL / 8U))
            {
                p_unit[i] = bw_unit[0];
                rate[i] = rate[i] * 8U;
            }
            else if (rate[i] < ((1000ULL * 1000ULL) / 8U))
            {
                p_unit[i] = bw_unit[1];
                rate[i] = rate[i] / (1000ULL / 8U);
            }
            else if (rate[i] < ((1000ULL * 1000ULL * 1000ULL) / 8U))
            {
                p_unit[i] = bw_unit[2];
                rate[i] = rate[i] / ((1000ULL * 1000ULL) / 8U);
            }
            else if (rate[i] < ((1000ULL * 1000ULL * 1000ULL * 1000ULL) / 8U))
            {
                p_unit[i] = bw_unit[3];
                rate[i] = rate[i] / ((1000ULL * 1000ULL * 1000ULL) / 8U);
            }
            else if (rate[i] < ((1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL) / 8U))
            {
                p_unit[i] = bw_unit[4];
                rate[i] = rate[i] / ((1000ULL * 1000ULL * 1000ULL * 1000ULL) / 8U);
            }
            else
            {
                p_unit[i] = bw_unit[0];
                rate[i] = 0;
            }
            sal_sprintf(str, "%3"PRIu64"%s%-6s", rate[i], " ", p_unit[i]);
            SYS_PORT_DBG_DUMP("%-10s", str);
        }
        SYS_PORT_DBG_DUMP("\n");
    }

    SYS_PORT_DBG_DUMP(" ------------------------------------------------------------------------------------------\n");
    SYS_PORT_DBG_DUMP(" \n");

    return CTC_E_NONE;
}

int32
sys_goldengate_port_wb_sync(uint8 lchip)
{
    uint16 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_port_master_t  *p_wb_port_master;
    sys_wb_port_prop_t port_prop;
    uint32  max_entry_cnt = 0;

    /*syncup  port matser*/
    wb_data.buffer = mem_malloc(MEM_PORT_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_port_master_t, CTC_FEATURE_PORT, SYS_WB_APPID_PORT_SUBID_MASTER);

    p_wb_port_master = (sys_wb_port_master_t  *)wb_data.buffer;

    p_wb_port_master->lchip = lchip;

    sal_memcpy(p_wb_port_master->mac_bmp, p_gg_port_master[lchip]->mac_bmp, 4 * sizeof(uint32));
    p_wb_port_master->use_logic_port_check = p_gg_port_master[lchip]->use_logic_port_check;
    p_wb_port_master->mlag_isolation_en = p_gg_port_master[lchip]->mlag_isolation_en;
    p_wb_port_master->use_isolation_id = p_gg_port_master[lchip]->use_isolation_id;
    p_wb_port_master->igs_isloation_bitmap = p_gg_port_master[lchip]->igs_isloation_bitmap;
    p_wb_port_master->egs_isloation_bitmap = p_gg_port_master[lchip]->egs_isloation_bitmap;

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);


    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_port_prop_t, CTC_FEATURE_PORT, SYS_WB_APPID_PORT_SUBID_PROP);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH/(wb_data.key_len + wb_data.data_len);
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    sal_memset(&port_prop, 0, sizeof(sys_wb_port_prop_t));
    do
    {
        port_prop.lchip = lchip;
        port_prop.lport = loop;
        sal_memcpy((uint8*)&port_prop.igs_port_prop, (uint8*)&p_gg_port_master[lchip]->igs_port_prop[loop], sizeof(sys_igs_port_prop_t));
        sal_memcpy((uint8*)&port_prop.egs_port_prop, (uint8*)&p_gg_port_master[lchip]->egs_port_prop[loop], sizeof(sys_egs_port_prop_t));

        sal_memcpy((uint8*)wb_data.buffer + wb_data.valid_cnt * sizeof(sys_wb_port_prop_t),  (uint8*)&port_prop, sizeof(sys_wb_port_prop_t));
        if (++wb_data.valid_cnt == max_entry_cnt)
        {
            CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
            wb_data.valid_cnt = 0;
        }
    }while(++loop < SYS_GG_MAX_PORT_NUM_PER_CHIP);

    if(wb_data.valid_cnt)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
    }

    CTC_ERROR_GOTO(sys_goldengate_internal_port_wb_sync(lchip), ret, done);

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_port_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_port_master_t  *p_wb_port_master;
    sys_wb_port_prop_t* p_wb_port_prop = NULL;
    uint32 entry_cnt = 0;
    uint32 lport = 0;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_PORT_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_port_master_t, CTC_FEATURE_PORT, SYS_WB_APPID_PORT_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  port master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query port master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_port_master = (sys_wb_port_master_t *)wb_query.buffer;

    sal_memcpy(p_gg_port_master[lchip]->mac_bmp, p_wb_port_master->mac_bmp, 4 * sizeof(uint32));
    p_gg_port_master[lchip]->use_logic_port_check = p_wb_port_master->use_logic_port_check;
    p_gg_port_master[lchip]->mlag_isolation_en = p_wb_port_master->mlag_isolation_en;
    p_gg_port_master[lchip]->use_isolation_id = p_wb_port_master->use_isolation_id;
    p_gg_port_master[lchip]->igs_isloation_bitmap = p_wb_port_master->igs_isloation_bitmap;
    p_gg_port_master[lchip]->egs_isloation_bitmap = p_wb_port_master->egs_isloation_bitmap;


    CTC_WB_INIT_QUERY_T((&wb_query),sys_wb_port_prop_t, CTC_FEATURE_PORT, SYS_WB_APPID_PORT_SUBID_PROP);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_port_prop = (sys_wb_port_prop_t*)wb_query.buffer + entry_cnt++;
        lport = p_wb_port_prop->lport;
        sal_memcpy((uint8*)&p_gg_port_master[lchip]->igs_port_prop[lport], (uint8*)&p_wb_port_prop->igs_port_prop, sizeof(sys_igs_port_prop_t));
        sal_memcpy((uint8*)&p_gg_port_master[lchip]->egs_port_prop[lport], (uint8*)&p_wb_port_prop->egs_port_prop, sizeof(sys_egs_port_prop_t));
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}

int32
sys_goldengate_port_get_mac_status(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8 mac_id = 0;
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 step = 0;
    uint32 tbl_id = 0;
    XqmacSgmac0Status0_m sgmac_status;
    XqmacXlgmacStatus0_m xlgmac_status;
    CgDebugStats0_m cgmac_stats;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    drv_goldengate_get_platform_type(&platform_type);
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    *p_value = 0;
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        mac_id = port_attr->mac_id;
        if(mac_id > 39)
        {
            mac_id = mac_id - 8;
        }
        if (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
        {
            tbl_id = CgDebugStats0_t + port_attr->slice_id*2 + ((mac_id == 36)?0:1);
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cgmac_stats));
            *p_value = GetCgDebugStats0(V, cgmacRxState_f, &cgmac_stats);
        }
        else if (CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode)
        {
            tbl_id = XqmacXlgmacStatus0_t + (mac_id+port_attr->slice_id*48)/4;
            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlgmac_status));
            *p_value = GetXqmacXlgmacStatus0(V, xlgmacRxState_f, &xlgmac_status);
        }
        else
        {
            step = XqmacSgmac1Status0_t - XqmacSgmac0Status0_t;
            tbl_id = XqmacSgmac0Status0_t + (mac_id+port_attr->slice_id*48) / 4 + ((mac_id+port_attr->slice_id*48)%4)*step;

            cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_status));
            *p_value = GetXqmacSgmac0Status0(V, sgmacRxState0_f, &sgmac_status);
        }
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_port_reset_mac_en(uint8 lchip, uint16 gport, uint32 value)
{
    uint16 lport     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint16 mac_id = 0;
    uint8 slice_id = 0;
    uint8  index = 0;

    if (NULL == p_gg_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    drv_goldengate_get_platform_type(&platform_type);
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    value = value?1:0;
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        mac_id = port_attr->mac_id;
        slice_id = port_attr->slice_id;
        switch (port_attr->pcs_mode)
        {
            case CTC_CHIP_SERDES_XFI_MODE:
            case CTC_CHIP_SERDES_SGMII_MODE:
            case CTC_CHIP_SERDES_2DOT5G_MODE:
                if(mac_id > 39)
                {
                    mac_id = mac_id - 8;
                }

                tbl_id = RlmNetCtlReset0_t + slice_id;
                field_id = RlmNetCtlReset0_resetCoreSgmac0_f + mac_id;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
                break;
            case CTC_CHIP_SERDES_XAUI_MODE:
            case CTC_CHIP_SERDES_XLG_MODE:
                if(mac_id > 39)
                {
                    mac_id = mac_id - 8;
                }

                tbl_id = RlmNetCtlReset0_t + slice_id;
                field_id = RlmNetCtlReset0_resetCoreXlg0Mac_f + mac_id/4;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
                break;
            case CTC_CHIP_SERDES_CG_MODE:
                tbl_id = RlmNetCtlReset0_t + slice_id;
                field_id = RlmNetCtlReset0_resetCoreCgmac0_f + ((mac_id==36)?0:2);
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &value));
                break;
            default:
                break;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_set_mac_auth(uint8 lchip, uint16 gport, bool enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN, enable));
    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, enable));

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_mac_auth(uint8 lchip, uint16 gport, bool* enable)
{
    uint32 value1 = 0;
    uint32 value2 = 0;

    CTC_PTR_VALID_CHECK(enable);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN, &value1));
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, &value2));

    *enable = ((value1 & value2) == 1) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_get_capability(uint8 lchip, uint16 gport, ctc_port_capability_type_t type, void* p_value)
{
    uint16 lport = 0;
    uint16 chip_port;
    uint8 slice_id = 0;
    uint16 serdes_id = 0;
    uint8 serdes_num = 0;
    ctc_port_serdes_info_t* p_serdes_port_t = NULL;
    sys_datapath_lport_attr_t* port_attr = NULL;
    ctc_chip_serdes_info_t serdes_info;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port capability, gport:0x%04X, type:%d!\n", gport, type);

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    /* zero value */
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    switch (type)
    {
        case CTC_PORT_CAP_TYPE_SERDES_INFO:
            p_serdes_port_t = (ctc_port_serdes_info_t*)p_value;
            slice_id = port_attr->slice_id;
            chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - slice_id*256;
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &serdes_num));
            p_serdes_port_t->serdes_id = serdes_id;
            p_serdes_port_t->serdes_num = serdes_num;
            sal_memset(&serdes_info, 0, sizeof(serdes_info));
            serdes_info.serdes_id = p_serdes_port_t->serdes_id;
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_mode(lchip, &serdes_info));
            p_serdes_port_t->overclocking_speed = serdes_info.overclocking_speed;
            p_serdes_port_t->serdes_mode = serdes_info.serdes_mode;
            break;

        case CTC_PORT_CAP_TYPE_MAC_ID:
            *(uint32*)p_value = port_attr->mac_id+ 64*port_attr->slice_id;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_store_led_mode_to_lport_attr(uint8 lchip, uint8 mac_id, uint8 first_led_mode, uint8 sec_led_mode)
{
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    /*get gport from datapath*/
    lport = sys_goldengate_common_get_lport_with_mac(lchip, mac_id);
    if (lport == SYS_COMMON_USELESS_MAC)
    {
        return CTC_E_INVALID_PARAM;
    }

    lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    port_attr->first_led_mode = first_led_mode;
    port_attr->sec_led_mode = sec_led_mode;

    return CTC_E_NONE;
}

/*enable 1:link up txactiv  0:link down, foreoff*/
int32
sys_goldengate_port_restore_led_mode(uint8 lchip, uint32 gport, uint32 enable)
{
    ctc_chip_led_para_t led_para;
    ctc_chip_mac_led_type_t led_type;
    uint8 first_led_mode = 0;
    uint8 sec_led_mode = 0;
    uint8 is_first_need = 0;
    uint8 is_sec_need = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    first_led_mode = port_attr->first_led_mode;
    sec_led_mode = port_attr->sec_led_mode;

    if ((CTC_CHIP_TXLINK_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_RXLINK_BIACTIVITY_MODE == first_led_mode) ||
        (CTC_CHIP_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_BIACTIVITY_MODE == first_led_mode))
    {
        is_first_need = 1;
    }

    if ((CTC_CHIP_TXLINK_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_RXLINK_BIACTIVITY_MODE == first_led_mode) ||
        (CTC_CHIP_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_BIACTIVITY_MODE == first_led_mode))
    {
        is_sec_need = 1;
    }

    if (is_first_need || is_sec_need)
    {
        sal_memset(&led_para, 0, sizeof(ctc_chip_led_para_t));
        led_para.ctl_id = port_attr->slice_id;
        led_para.lchip = lchip;
        led_para.port_id = port_attr->mac_id;
        led_para.op_flag |= CTC_CHIP_LED_MODE_SET_OP;
        led_para.first_mode = (is_first_need? (enable?first_led_mode:CTC_CHIP_FORCE_OFF_MODE):first_led_mode);
        led_para.sec_mode = (is_sec_need?(enable?sec_led_mode:CTC_CHIP_FORCE_OFF_MODE):sec_led_mode);
        led_type = (sec_led_mode !=  CTC_CHIP_MAC_LED_MODE)?CTC_CHIP_USING_TWO_LED:CTC_CHIP_USING_ONE_LED;
        CTC_ERROR_RETURN(sys_goldengate_chip_set_mac_led_mode(lchip, &led_para, led_type, 1));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_pcs_link_fault_reset(uint8 lchip, uint32 gport)
{
    uint8 slice_id = 0;
    uint8 mac_id = 0;
    uint16 lport     = 0;
    uint32 cmd       = 0;
    uint32 tbl_id    = 0;
    uint32 value     = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint32 fec_en = 0;
    uint8  step      = 0;
    uint8  sub_idx   = 0;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if ((CTC_CHIP_SERDES_CG_MODE != port_attr->pcs_mode)
        && (CTC_CHIP_SERDES_XLG_MODE != port_attr->pcs_mode))
    {
        return CTC_E_NONE;
    }

    slice_id = port_attr->slice_id;
    mac_id   = port_attr->mac_id;
    /* mac id range 1-39, 48-55. if mac id bigger than 39, should be converted */
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }

    /*get fec ,if fec enable return */
    _sys_goldengate_port_get_fec_en(lchip, gport, &fec_en);
    if (fec_en && (CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode))
    {
        return CTC_E_NONE;
    }

    port_attr->pcs_reset_en = 1;
    if(CTC_CHIP_SERDES_CG_MODE == port_attr->pcs_mode)
    {
        /*reset*/
        value = 1;
        tbl_id = CgPcsCfg0_t + slice_id*2 + ((mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLocalFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLinkIntFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreRemoteFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, CgPcsSoftRst0_softRstPcsRx_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        /*release*/
        value = 0;
        tbl_id = CgPcsSoftRst0_t + slice_id*2 + ((mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, CgPcsSoftRst0_softRstPcsRx_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
         /*sal_task_sleep(1);*/
        tbl_id = CgPcsCfg0_t + slice_id*2 + ((mac_id==36)?0:1);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLocalFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreLinkIntFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(tbl_id, CgPcsCfg0_ignoreRemoteFault_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
    }
    else if(CTC_CHIP_SERDES_XLG_MODE == port_attr->pcs_mode)
    {
        step = SharedPcsXfiCfg10_t - SharedPcsXfiCfg00_t;
        /*reset*/
        value = 1;
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

        /*release*/
        value = 0;
        tbl_id = SharedPcsSoftRst0_t + (mac_id + slice_id * 48)/4;
        cmd = DRV_IOW(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        DRV_IOCTL(lchip, 0, cmd, &value);

         /*sal_task_sleep(1);*/
        for (sub_idx = 0; sub_idx < 4; sub_idx++)
        {
            tbl_id = SharedPcsXfiCfg00_t + (mac_id+slice_id*48) / 4 + sub_idx*step;
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLocalFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreLinkIntFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
            cmd = DRV_IOW(tbl_id, SharedPcsXfiCfg00_ignoreRemoteFault0_f);
            DRV_IOCTL(lchip, 0, cmd, &value);
        }
    }
    port_attr->pcs_reset_en = 0;

    return CTC_E_NONE;
}

int32
_sys_goldengate_port_cg_fec_enable(uint8 lchip, uint32 gport)
{
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;
    uint32 fec_en = 0;
    uint32 auto_neg_en = 0;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        return CTC_E_MAC_NOT_USED;
    }

    if (CTC_CHIP_SERDES_CG_MODE != port_attr->pcs_mode)
    {
        return CTC_E_NONE;
    }

    /*get fec ,if fec enable return */
    _sys_goldengate_port_get_fec_en(lchip, gport, &fec_en);
    sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en);
    if (fec_en && (0 == auto_neg_en))
    {
        sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_EN, 0);
        sys_goldengate_port_set_3ap_training_en(lchip, gport, SYS_PORT_3AP_TRAINING_INIT, 0);
        port_attr->pcs_reset_en = 1;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_link_down_event(uint8 lchip, uint32 gport)
{
    uint16 lport = 0;

    _sys_goldengate_port_cg_fec_enable(lchip, gport);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    /*disable interrupt*/
    CTC_ERROR_RETURN(_sys_goldengate_port_set_link_intr(lchip, gport, 0));
    CTC_ERROR_RETURN(sys_goldengate_port_restore_led_mode(lchip, gport, FALSE));
    p_gg_port_master[lchip]->igs_port_prop[lport].link_intr_en = 0;

    return CTC_E_NONE;
}

int32
sys_goldengate_port_link_up_event(uint8 lchip, uint32 gport)
{
    uint32 value = 0;
    uint32 remote_link = 0;
    uint32 auto_neg_en = 0;
    uint32 auto_neg_mode = 0;
    uint8 speed = 0;
    uint8 index = 0;
    uint16 lport = 0;

    CTC_ERROR_RETURN(_sys_goldengate_port_pcs_link_fault_reset(lchip, gport));

    /*reset mac */
    for (index=0;index<10;index++)
    {
        sys_goldengate_port_get_mac_status(lchip, gport, &value);
        if (value)
        {
            sal_task_sleep(10);
            CTC_ERROR_RETURN(sys_goldengate_port_reset_mac_en(lchip, gport, TRUE));
            CTC_ERROR_RETURN(sys_goldengate_port_reset_mac_en(lchip, gport, FALSE));
        }
        else
        {
            break;
        }
    }
    if (10 == index)
    {
        SYS_PORT_DBG_DUMP("Reset Mac Over ten times!\n");
    }

    /*set speed 10M/100M/1G*/
    CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_SPEED, &value));
    if ((CTC_PORT_SPEED_1G == value) || (CTC_PORT_SPEED_100M == value) || (CTC_PORT_SPEED_10M == value))
    {
        CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en));
        CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, &auto_neg_mode));
        if (auto_neg_en && (CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == auto_neg_mode))
        {
            sys_goldengate_port_get_sgmii_remote_info(lchip, gport, &value);
            sys_goldengate_port_get_sgmii_remote_link_status(lchip, gport, auto_neg_en, auto_neg_mode, &remote_link);
            if (remote_link)
            {
                speed = (value>>10)&0x3;
                if (0x00 == speed)
                {
                    CTC_ERROR_RETURN(_sys_goldengate_port_set_speed(lchip, gport, CTC_PORT_SPEED_10M));
                }
                else if (0x01 == speed)
                {
                    CTC_ERROR_RETURN(_sys_goldengate_port_set_speed(lchip, gport, CTC_PORT_SPEED_100M));
                }
                else if (0x02 == speed)
                {
                    CTC_ERROR_RETURN(_sys_goldengate_port_set_speed(lchip, gport, CTC_PORT_SPEED_1G));
                }
            }
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_port_restore_led_mode(lchip, gport, TRUE));

    /*enable interrupt*/
    CTC_ERROR_RETURN(_sys_goldengate_port_set_link_intr(lchip, gport, TRUE));
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    p_gg_port_master[lchip]->igs_port_prop[lport].link_intr_en = 1;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_port_module_reset_check(uint8 lchip, sys_datapath_lport_attr_t* port_attr)
{
    uint32 value            = 0;
    uint32 value1           = 0;
    uint32 cmd              = 0;
    uint32 tbl_id           = 0;
    uint32 field_id         = 0;
    char* p_table_id        = NULL;
    char* p_table_id1       = NULL;
    uint8 mac_id            = 0;

    mac_id   = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    if(port_attr->pcs_mode == CTC_CHIP_SERDES_SGMII_MODE || port_attr->pcs_mode == CTC_CHIP_SERDES_2DOT5G_MODE ||port_attr->pcs_mode == CTC_CHIP_SERDES_XFI_MODE)
    {
        /* cfg SharedPcsSoftRst, relese pcs Tx soft reset */
        tbl_id = SharedPcsSoftRst0_t + (mac_id + port_attr->slice_id * 48)/4;
        field_id = SharedPcsSoftRst0_softRstPcsTx0_f + mac_id%4;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_table_id = value ? "SharedPcsSoftRst.softRstPcsTx" : NULL;


        /* cfg SharedPcsSoftRst, relese pcs rx soft reset */
        field_id = SharedPcsSoftRst0_softRstPcsRx0_f + mac_id%4;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        p_table_id1 = value1 ? "SharedPcsSoftRst.softRstPcsRx" : NULL;
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_XAUI_MODE || port_attr->pcs_mode == CTC_CHIP_SERDES_DXAUI_MODE ||port_attr->pcs_mode == CTC_CHIP_SERDES_XLG_MODE)
    {
        tbl_id = SharedPcsSoftRst0_t + (mac_id + port_attr->slice_id * 48)/4;
        cmd = DRV_IOR(tbl_id, SharedPcsSoftRst0_softRstXlgTx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_table_id = value ? "SharedPcsSoftRst.softRstXlgTx" : NULL;

        /* cfg SharedPcsSoftRst softRstXlgRx_f */
        cmd = DRV_IOR(tbl_id, SharedPcsSoftRst0_softRstXlgRx_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        p_table_id1 = value1 ? "SharedPcsSoftRst.softRstXlgRx" : NULL;
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE )
    {
        /* cfg CgPcsSoftRst softRstPcsTx_f, release pcs tx soft reset */
        tbl_id = CgPcsSoftRst0_t + port_attr->slice_id*2 + ((mac_id ==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsTx_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_table_id = value ? "CgPcsSoftRst_softRstPcsTx" : NULL;

        /* cfg CgPcsSoftRst softRstPcsRx_f, release pcs rx soft reset */
        tbl_id = CgPcsSoftRst0_t + port_attr->slice_id*2 + ((mac_id ==36)?0:1);
        field_id = CgPcsSoftRst0_softRstPcsRx_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        p_table_id1 = value1 ? "CgPcsSoftRst_softRstPcsRx" : NULL;
    }
    else
    {
        SYS_PORT_DBG_DUMP("Module Reset check    --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  Pcs mode is error\n");
        return CTC_E_NONE;
    }
    if(value1 == 0 && value == 0)
    {
        SYS_PORT_DBG_DUMP("Module Reset check    --------------   Pass\n");
    }
    else
    {
        SYS_PORT_DBG_DUMP("Module Reset check    --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
        if(value)
        {
            SYS_PORT_DBG_DUMP("  %s:%d\n",p_table_id,value);
        }
        if(value1)
        {
            SYS_PORT_DBG_DUMP("  %s:%d\n",p_table_id1,value1);
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_hss_cs_macrco_check(uint8 lchip, uint8* lane_id, uint32 gport, uint8 hss_id,uint8 loopback_mode,uint8 hss_type,uint8 serdes_num)
{
    HsMacroMiscMon0_m hs_macmon;
    CsMacroMiscMon0_m cs_macmon;
    HsMon0_m hs_mon;
    CsMon0_m cs_mon;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 ability = 0;
    uint32 value = 0;
    uint32 value1 = 0;
    uint32 value2 = 0;
    uint32 value3 = 0;
    uint8 lane[4] = {0};
    char* p_table_id        = NULL;
    char* p_table_id1       = NULL;
    uint8 flag = 0;
    uint8 step = 0;
    uint8 loop = 0;
    uint8 cl73_en = 0;
    uint8 flag1 = 0;
    char* sigdet[] = {"monRxASigdet","monRxBSigdet","monRxCSigdet","monRxDSigdet","monRxESigdet","monRxFSigdet","monRxGSigdet","monRxHSigdet"};

    if(hss_type == SYS_DATAPATH_HSS_TYPE_15G)
    {
        tb_id = HsMacroMiscMon0_t + hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_macmon);
        value = GetHsMacroMiscMon0(V,monHssPrtReadyA_f, &hs_macmon);
        value1 = GetHsMacroMiscMon0(V,monHssPrtReadyB_f, &hs_macmon);
        p_table_id = "HsMacroMiscMon.monHssPrtReadyA:0  HsMacroMiscMon.monHssPrtReadyB:0";

        tb_id = HsMon0_t + hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &hs_mon);
        value2 = GetHsMon0(V,monHssPllLockA_f, &hs_mon);
        value3 = GetHsMon0(V,monHssPllLockB_f, &hs_mon);
        p_table_id1 = "HsMon.monHssPllLockA:0  HsMon.monHssPllLockB:0";
    }
    else
    {
        tb_id = CsMacroMiscMon0_t + hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_macmon);
        value = GetCsMacroMiscMon0(V,monHssPrtReadyA_f, &cs_macmon);
        value1 = GetCsMacroMiscMon0(V,monHssPrtReadyB_f, &cs_macmon);
        p_table_id = "CsMacroMiscMon.monHssPrtReadyA:0  CsMacroMiscMon.monHssPrtReadyB:0";

        tb_id = CsMon0_t + hss_id;
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &cs_mon);
        value2 = GetCsMon0(V,monHssPllLockA_f, &cs_mon);
        value3 = GetCsMon0(V,monHssPllLockB_f, &cs_mon);
        p_table_id1 = "CsMon.monHssPllLockA:0  CsMon.monHssPllLockB:0";
    }
    if((value == 0 && value1 == 0) ||(value2 == 0 && value3 == 0))
    {
        flag = 1;
        SYS_PORT_DBG_DUMP("HSS/CS Macrco check   --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
        if(value == 0 && value1 == 0)
        {
            SYS_PORT_DBG_DUMP("  %s\n",p_table_id);
        }
        if((value2 == 0 && value3 == 0))
        {
            SYS_PORT_DBG_DUMP("  %s\n",p_table_id1);
        }
    }

    if(loopback_mode == 1 && flag == 0)
    {
        SYS_PORT_DBG_DUMP("HSS/CS Macrco check   --------------   Pass\n");
        return CTC_E_NONE;
    }
    else if(loopback_mode == 1 && flag == 1)
    {
        return CTC_E_NONE;
    }
    CTC_ERROR_RETURN(sys_goldengate_port_get_cl73_ability(lchip, gport, 0, &ability));
    if(CTC_FLAG_ISSET(ability, SYS_PORT_CL73_10GBASE_KR)||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_40GBASE_KR4)||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_40GBASE_CR4)
        ||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_100GBASE_KR4)||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_100GBASE_CR4)||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_FEC_ABILITY)
        ||CTC_FLAG_ISSET(ability, SYS_PORT_CL73_FEC_REQUESTED))
    {
        cl73_en = 1;
    }
    if(hss_type == SYS_DATAPATH_HSS_TYPE_15G)
    {
        step = HsMon0_monRxBSigdet_f - HsMon0_monRxASigdet_f;
        for(loop = 0; loop < serdes_num; loop ++)
        {
            lane[loop] = GetHsMon0(V,monRxASigdet_f + step*lane_id[loop], &hs_mon);
        }
    }
    else
    {
        step = CsMon0_monRxBSigdet_f - CsMon0_monRxASigdet_f;
        for(loop = 0; loop < serdes_num; loop ++)
        {
            lane[loop] = GetCsMon0(V,monRxASigdet_f + step*lane_id[loop], &cs_mon);
        }
    }

    if(cl73_en)
    {
        for(loop = 0; loop < serdes_num; loop ++)
        {
            if(lane[loop] == 1)
            {
                break;
            }
            if(loop == (serdes_num-1) && flag == 0)
            {
                SYS_PORT_DBG_DUMP("HSS/CS Macrco check   --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
                for(loop = 0; loop < serdes_num; loop ++)
                {
                    SYS_PORT_DBG_DUMP("  %s_%s:0\n",(hss_type == SYS_DATAPATH_HSS_TYPE_15G)?"HsMon":"CsMon",sigdet[lane_id[loop]]);
                }
            }
            else if(loop == (serdes_num-1) && flag == 1)
            {
                for(loop = 0; loop < serdes_num; loop ++)
                {
                    SYS_PORT_DBG_DUMP("  %s_%s:0\n",(hss_type == SYS_DATAPATH_HSS_TYPE_15G)?"HsMon":"CsMon",sigdet[lane_id[loop]]);
                }
            }
        }
        if(flag == 0 && loop != serdes_num)
        {
            SYS_PORT_DBG_DUMP("Module Reset check   --------------   Pass\n");
        }
    }
    else
    {
        for(loop = 0; loop < serdes_num; loop ++)
        {
            if(lane[loop] == 0 && flag == 0 && flag1 == 0)
            {
                flag1 = 1;
                SYS_PORT_DBG_DUMP("HSS/CS Macrco check   --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
                SYS_PORT_DBG_DUMP("  %s_%s:0\n",(hss_type == SYS_DATAPATH_HSS_TYPE_15G)?"HsMon":"CsMon",sigdet[lane_id[loop]]);
            }
            else if(lane[loop] == 0)
            {
                SYS_PORT_DBG_DUMP("  %s_%s:0\n",(hss_type == SYS_DATAPATH_HSS_TYPE_15G)?"HsMon":"CsMon",sigdet[lane_id[loop]]);
            }
            else if(loop == (serdes_num-1) && flag1 == 0 && flag == 0)
            {
                SYS_PORT_DBG_DUMP("HSS/CS Macrco check   --------------   Pass\n");
            }
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_pcs_status_check(uint8 lchip, sys_datapath_lport_attr_t* port_attr,uint32 gport, uint8 auto_neg_en)
{
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint8 mac_id = 0;
    uint32 value = 0;
    uint32 value1 = 0;
    uint32 value2 = 0;
    uint32 value3 = 0;
    uint32 value4 = 0;
    uint8 loop = 0;
    uint8 loop1 = 0;
    uint8 flag = 0;
    uint8 flag1 = 0;
    uint8 flag2 = 0;
    uint32 enable = 0;
    uint32 tb_id1 = 0;
    uint32 tb_id2 = 0;
    uint32 corr_cnt[2] = {0};
    uint32 unco_cnt[2] = {0};
    uint32 array[4] = {0};
    SharedPcsXauiStatus0_m shared_xaui_status;
    SharedPcsXlgStatus0_m xlg_status;
    CgPcsDebugStats0_m cg_debug_status;
    CgPcsStatus0_m cg_status;
    CgPcsFecStatus0_m cg_fec_status;

    mac_id = port_attr->mac_id;
    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    if(port_attr->pcs_mode == CTC_CHIP_SERDES_SGMII_MODE)
    {
        quad_idx = (mac_id+ port_attr->slice_id * 48)/4;
        sub_idx = (mac_id+ port_attr->slice_id * 48)%4;
        tb_id = SharedPcsSgmiiStatus00_t + (SharedPcsSgmiiStatus01_t - SharedPcsSgmiiStatus00_t)*quad_idx +
            (SharedPcsSgmiiStatus10_t - SharedPcsSgmiiStatus00_t)*sub_idx;

        cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_codeErrCnt0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_sgmiiSyncStatus0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_anegState0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
        cmd = DRV_IOR(tb_id, SharedPcsSgmiiStatus00_anLinkStatus0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value4));
        if(value == 0 && value1 == 1 && value4 == 1 && (!auto_neg_en || (auto_neg_en && value2 == 6 )))
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
        }
        else
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            if(value != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsSgmiiStatus.codeErrCnt:%d\n",value);
            }
            if(value1 != 1)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsSgmiiStatus.sgmiiSyncStatus:%d\n",value1);
            }
            if(value4 != 1)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsSgmiiStatus.anLinkStatus:%d\n",value4);
            }
            if(auto_neg_en && value2 != 6)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsSgmiiStatus.anegState:%d\n",value2);
            }
        }
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_XAUI_MODE || port_attr->pcs_mode == CTC_CHIP_SERDES_DXAUI_MODE)
    {
        if (port_attr->mac_id%4)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("MAC ID is not right\n");
            return CTC_E_NONE;
        }
        tb_id = SharedPcsXauiStatus0_t + (mac_id + port_attr->slice_id*48)/4;
        sal_memset(&shared_xaui_status, 0, sizeof(shared_xaui_status));
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &shared_xaui_status));
        for(loop = 0; loop < 4; loop ++)
        {
            value = GetSharedPcsXauiStatus0(V, codeErrorCnt0_f + loop, &shared_xaui_status);
            value1 = GetSharedPcsXauiStatus0(V, xauiSyncStatus0_f + loop, &shared_xaui_status);
            if((value != 0 ||value1 != 1)&& flag == 0)
            {
                flag = 1;
                SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            }
            if(value != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXauiStatus.codeErrorCnt%d:%d\n",loop,value);
            }
            if(value1 != 1)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXauiStatus.xauiSyncStatus%d:%d\n",loop,value1);
            }
        }
        if(flag == 0)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
        }
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_XFI_MODE)
    {
        quad_idx = (mac_id+ port_attr->slice_id * 48)/4;
        sub_idx = (mac_id+ port_attr->slice_id * 48)%4;
        tb_id = SharedPcsXfiStatus00_t + (SharedPcsXfiStatus01_t - SharedPcsXfiStatus00_t)*quad_idx +
                (SharedPcsXfiStatus10_t - SharedPcsXfiStatus00_t)*sub_idx;
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_errBlockCnt0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_badBerCnt0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_rxBlockLock0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_hiBer0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value3));
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_linkFaultState0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value4));
        if(value == 0 && value1 == 0 && value3 == 0 && value2 == 1 && value4 == 0)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
        }
        else
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            if(value != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXfiStatus.errBlockCnt:%d\n",value);
            }
            if(value1 != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXfiStatus.badBerCnt:%d\n",value1);
            }
            if(value4 != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXfiStatus.linkFaultState:%d\n",value4);
            }
            if(value2 != 1)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXfiStatus.rxBlockLock:%d\n",value2);
            }
            if(value3 != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXfiStatus.hiBer:%d\n",value3);
            }
        }
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_XLG_MODE)
    {
        if (port_attr->mac_id%4)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("MAC ID is not right\n");
            return CTC_E_NONE;
        }
        quad_idx = (mac_id + port_attr->slice_id * 48)/4;
        tb_id = SharedPcsXlgStatus0_t + quad_idx;
        sal_memset(&xlg_status, 0, sizeof(xlg_status));
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &xlg_status));
        value3 = GetSharedPcsXlgStatus0(V, alignStatus_f, &xlg_status);
        value4 = GetSharedPcsXlgStatus0(V, linkFaultState_f, &xlg_status);
        for(loop = 0; loop < 4; loop ++)
        {
            value = GetSharedPcsXlgStatus0(V, bipErrCnt0_f+loop, &xlg_status);
            array[loop] = GetSharedPcsXlgStatus0(V, rxAmLockLane0_f+loop, &xlg_status);
            value2 = GetSharedPcsXlgStatus0(V, rxAmLock0_f+loop, &xlg_status);
            if((value != 0 || value2 != 1 || value3 != 1 || value4 != 0)&& flag == 0)
            {
                flag = 1;
                SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            }
            if(value != 0)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXlgStatus.bipErrCnt%d:%d\n",loop,value);
            }
            if(value2 != 1)
            {
                SYS_PORT_DBG_DUMP("  SharedPcsXlgStatus.rxAmLock%d:%d\n",loop,value2);
            }
        }
        for(loop = 0; loop < 4; loop ++)
        {
            for(loop1 = 0; loop1 < 4; loop1 ++)
            {
                if(loop == array[loop1])
                {
                    break;
                }
                if(loop1 == 3)
                {
                    flag1 = 1;
                }
            }
        }
        if(flag1 == 1 && flag == 0)
        {
            flag = 1;
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
        }
        if(flag1 == 1)
        {
            SYS_PORT_DBG_DUMP("  SharedPcsXlgStatus.rxAmLockLane value is error\n");
        }
        if(value3 != 1)
        {
            SYS_PORT_DBG_DUMP("  SharedPcsXlgStatus.alignStatus:%d\n",value3);
        }
        if(value4 != 0)
        {
            SYS_PORT_DBG_DUMP("  SharedPcsXlgStatus.linkFaultState:%d\n",value4);
        }
        if(flag == 0)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
        }
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE)
    {
        _sys_goldengate_port_get_fec_en(lchip, gport, &enable);
        if (port_attr->mac_id == 36)
        {
            tb_id = CgPcsStatus0_t + 2*port_attr->slice_id;
            tb_id1 = CgPcsDebugStats0_t + 2*port_attr->slice_id;
            tb_id2 = CgPcsFecStatus0_t + 2*port_attr->slice_id;
        }
        else if (port_attr->mac_id == 52)
        {
            tb_id = CgPcsStatus1_t + 2*port_attr->slice_id;
            tb_id1 = CgPcsDebugStats1_t + 2*port_attr->slice_id;
            tb_id2 = CgPcsFecStatus1_t + 2*port_attr->slice_id;
        }
        else
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
            SYS_PORT_DBG_DUMP("MAC ID is not right\n");
            return CTC_E_NONE;
        }
        sal_memset(&cg_debug_status, 0, sizeof(cg_debug_status));
        cmd = DRV_IOR(tb_id1, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cg_debug_status));
        sal_memset(&cg_status, 0, sizeof(cg_status));
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cg_status));
        sal_memset(&cg_fec_status, 0, sizeof(cg_fec_status));
        cmd = DRV_IOR(tb_id2, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cg_fec_status));
        for(loop = 0; loop < 4; loop ++)
        {
            value = GetCgPcsDebugStats0(V, badBerCnt0_f+loop, &cg_debug_status);
            value1 = GetCgPcsDebugStats0(V, errBlockCnt0_f+loop, &cg_debug_status);
            if((value != 0 ||value1 != 0) && flag == 0)
            {
                flag = 1;
                SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            }
            if(value != 0)
            {
                SYS_PORT_DBG_DUMP("  CgPcsDebugStats.badBerCnt%d:%d\n",loop,value);
            }
            if(value1 != 0)
            {
                SYS_PORT_DBG_DUMP("  CgPcsDebugStats.errBlockCntk%d:%d\n",loop,value1);
            }
        }
        value = GetCgPcsStatus0(V, alignStatus_f, &cg_status);
        for(loop = 0; loop < 20; loop ++)
        {
            value1 = GetCgPcsStatus0(V, rxBlockLock0_f+loop, &cg_status);
            value2 = GetCgPcsStatus0(V, rxAmLock0_f+loop, &cg_status);
            if((value != 1 ||value1 != 1 || value2 != 1) &&(flag == 0 && flag1 == 0))
            {
                flag1 = 1;
                SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            }
            if(value1 != 1)
            {
                SYS_PORT_DBG_DUMP("  CgPcsStatus.rxBlockLock%d:%d\n",loop,value1);
            }
            if(value2 != 1)
            {
                SYS_PORT_DBG_DUMP("  CgPcsStatus.rxAmLock%d:%d\n",loop,value2);
            }
        }
        if(value != 1)
        {
            SYS_PORT_DBG_DUMP("  CgPcsStatus.alignStatus:%d\n",value);
        }
        if(!enable && !flag && !flag1)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
            return CTC_E_NONE;
        }
        else if(!enable)
        {
            return CTC_E_NONE;
        }
        value = GetCgPcsFecStatus0(V, fecAlignStatus_f, &cg_fec_status);
        for(loop = 0; loop < 2; loop ++)
        {
            corr_cnt[loop] = GetCgPcsFecStatus0(V, corrCwCnt_f, &cg_fec_status);
            unco_cnt[loop] = GetCgPcsFecStatus0(V, uncoCwCnt_f, &cg_fec_status);
            sal_task_sleep(1000);
            if(loop == 1)
            {
                value1 = corr_cnt[1] - corr_cnt[0];
                value2 = unco_cnt[1] - unco_cnt[0];
            }
            if((value != 1 ||value1 != 0 || value2 != 0) &&(flag == 0 && flag1 == 0 && flag2 == 0))
            {
                flag2 = 1;
                SYS_PORT_DBG_DUMP("PCS Status check      --------------   Fail\n");
                SYS_PORT_DBG_DUMP("  Correlative Status List:\n");
            }
            if(value1 != 0)
            {
                SYS_PORT_DBG_DUMP("  CgPcsFecStatus.corrCwCnt:%d\n",value1);
            }
            if(value2 != 0)
            {
                SYS_PORT_DBG_DUMP("  CgPcsFecStatus.uncoCwCnt:%d\n",value2);
            }
        }
        if(value != 1)
        {
            SYS_PORT_DBG_DUMP("  CgPcsFecStatus.fecAlignStatus:%d\n",value);
        }
        if(flag == 0 && flag1 == 0 && flag2 == 0)
        {
            SYS_PORT_DBG_DUMP("PCS Status check      --------------   Pass\n");
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_port_monitor_get_temperature(uint8 lchip, int32* temperature)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 timeout = 10;
    uint32 temp_value = 0;

    value = 0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgSleepSen_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    value = 0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgSenSv_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    value = 1;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgResetSen_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    value = 0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgResetSen_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    cmd = DRV_IOR(TvSensorMon_t, TvSensorMon_monOutputValidSen_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    while ((value == 0) && (timeout--))
    {
        DRV_IOCTL(lchip, 0, cmd, &value);
        sal_task_sleep(100);
    }

    if (value == 1)
    {
        cmd = DRV_IOR(TvSensorMon_t, TvSensorMon_monOutputSen_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        if ((value & 0xff) >= 118)
        {
            temp_value = (value & 0xff) - 118;
        }
        else
        {
            temp_value = 118 - (value & 0xff) + (1 << 31);
        }
        if (CTC_IS_BIT_SET(temp_value, 31))
        {
            *temperature = 0 - (temp_value&0x7FFFFFFF);
        }
        else
        {
            *temperature = temp_value;
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_port_self_checking(uint8 lchip, uint32 gport)
{
    uint16 lport = 0;
    char* mac_en = NULL;
    char* auto_neg = NULL;
    char* link_up = "-";
    uint32 value_link_up = FALSE;
    uint32 value_mac_en = FALSE;
    uint32 value_32 = 0;
    uint32 value_64 = 0;
    char value_c[32] = {0};
    sys_datapath_lport_attr_t* port_attr = NULL;
    char* p_fault = NULL;
    char* auto_neg_mod[] = {"1000BASE-X", "SGMII master", "SGMII slaver"};
    char* ffe_mod[] = {"3AP  ", "user define  "};
    ctc_chip_serdes_loopback_t loopback_inter = {0};
    ctc_chip_serdes_loopback_t loopback_exter = {0};
    char* p_loopback_mode = NULL;
    int32 temperature = 0;
    uint16 coefficient[SYS_GG_CHIP_FFE_PARAM_NUM] = {0};
    uint8 status = 0;
    uint8 channel = 0;
    uint16 sigdet = 0;
    uint32 value = 0;
    uint8 mac_id = 0;
    uint8 quad_idx = 0;
    uint8 sub_idx = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    int32 ret = 0;
    uint8 enable[4] = {0};
    uint16 chip_port;
    uint8 serdes_num = 0;
    uint16 serdes_id = 0;
    uint8 serdes_num_tmp = 0;
    uint8 hss_id = 0;
    uint8 auto_neg_en = 0;
    uint8 hss_type = 0;
    uint8 lane_id[4] = {0};
    ctc_mac_stats_t mac_stats;

    SYS_PORT_INIT_CHECK(lchip);
    SYS_PORT_DBG_DUMP("Mac Information:\n");
    SYS_PORT_DBG_DUMP("------------------------------------------------------------------------------------------\n");
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    /* get serdes num */
    chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport) - port_attr->slice_id*256;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, port_attr->slice_id, &serdes_id, &serdes_num));
    /* get mac en */
    CTC_ERROR_RETURN(sys_goldengate_port_get_mac_en(lchip, gport, &value_mac_en));
    mac_en = value_mac_en ? "Enable" : "Disable";
    /* get mac link up status */
    CTC_ERROR_RETURN(sys_goldengate_port_get_mac_link_up(lchip, gport, &value_link_up, 0));
    link_up = value_link_up ? "Up" : "Down";
    /* get loopback status */
    loopback_inter.serdes_id = serdes_id;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_loopback(lchip, &loopback_inter));
    loopback_exter.serdes_id = serdes_id;
    loopback_exter.mode = 1;
    CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_loopback(lchip, &loopback_exter));
    if(loopback_inter.enable)
    {
        p_loopback_mode = "internal";
    }
    else if(loopback_exter.enable)
    {
        p_loopback_mode = "external";
    }
    else
    {
        p_loopback_mode = "-";
    }
    /* get asic temperature */
    CTC_ERROR_RETURN(_sys_goldengate_port_monitor_get_temperature(lchip, &temperature));

    CTC_ERROR_RETURN(sys_goldengate_get_channel_by_port(lchip, gport, &channel));
    /* get port speed */
    ret = sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_SPEED, (uint32*)&value_32);
    if (ret < 0)
    {
        SYS_PORT_DBG_DUMP("%-7s", "-");
    }
    else
    {
        sal_memset(value_c, 0, sizeof(value_c));
        if (CTC_PORT_SPEED_1G == value_32)
        {
            sal_memcpy(value_c, "1G", sal_strlen("1G"));
        }
        else if (CTC_PORT_SPEED_100M == value_32)
        {
            sal_memcpy(value_c, "100M", sal_strlen("100M"));
        }
        else if (CTC_PORT_SPEED_10M == value_32)
        {
            sal_memcpy(value_c, "10M", sal_strlen("10M"));
        }
        else if (CTC_PORT_SPEED_2G5 == value_32)
        {
            sal_memcpy(value_c, "2.5G", sal_strlen("2.5G"));
        }
        else if (CTC_PORT_SPEED_10G == value_32)
        {
            sal_memcpy(value_c, "10G", sal_strlen("10G"));
        }
        else if (CTC_PORT_SPEED_20G == value_32)
        {
            sal_memcpy(value_c, "20G", sal_strlen("20G"));
        }
        else if (CTC_PORT_SPEED_40G == value_32)
        {
            sal_memcpy(value_c, "40G", sal_strlen("10G"));
        }
        else if (CTC_PORT_SPEED_100G == value_32)
        {
            sal_memcpy(value_c, "100G", sal_strlen("100G"));
        }
    }

    SYS_PORT_DBG_DUMP("%s %-12d %s %-15s %s %-16s %s %-12d\n",
                          "Mac ID   :",(port_attr->mac_id+port_attr->slice_id*64), "Mac-En  :",mac_en, "Link    :",link_up, "Channel ID :",channel);
    /* get Auto-Neg mode */
    ret = sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &value_32);
    auto_neg_en = value_32;
    if (ret < 0)
    {
        auto_neg = "-";
    }
    else if(value_32)
    {
        sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, &value_32);
        auto_neg =  ((CTC_CHIP_SERDES_SGMII_MODE == port_attr->pcs_mode) || (CTC_CHIP_SERDES_2DOT5G_MODE == port_attr->pcs_mode))?
                     auto_neg_mod[value_32] : "TURE";
    }
    else
    {
        auto_neg = "FALSE";
    }
    if(serdes_num == 1)
    {
        SYS_PORT_DBG_DUMP("%s %-12d %s %-15s %s %-16s %s %-12s\n",
                          "Serdes ID:",serdes_id, "Auto-Neg:",auto_neg, "Loopback:",p_loopback_mode, "Speed      :",value_c);
    }
    else
    {
        SYS_PORT_DBG_DUMP("%s %d/%d/%d/%-3d %s %-15s %s %-16s %s %-12s\n",
                          "Serdes ID:",serdes_id,serdes_id+1,serdes_id+2,serdes_id+3, "Auto-Neg:",auto_neg,"Loopback:",p_loopback_mode, "Speed      :",value_c);
    }
    /* get serdes sigdet */
    sigdet = sys_goldengate_datapath_get_serdes_sigdet(lchip, serdes_id);
    /* get phrb_status */
    for(serdes_num_tmp = 0; serdes_num_tmp < serdes_num; serdes_num_tmp ++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_prbs_status(lchip, serdes_id+serdes_num_tmp,&enable[serdes_num_tmp],&hss_id,&hss_type,&lane_id[serdes_num_tmp]));
    }
    /* get fault status */
    mac_id = port_attr->mac_id;

    if(mac_id > 39)
    {
        mac_id = mac_id - 8;
    }
    if(port_attr->pcs_mode == CTC_CHIP_SERDES_XFI_MODE)
    {
        quad_idx = (mac_id+ port_attr->slice_id * 48)/4;
        sub_idx = (mac_id+ port_attr->slice_id * 48)%4;
        tb_id = SharedPcsXfiStatus00_t + (SharedPcsXfiStatus01_t - SharedPcsXfiStatus00_t)*quad_idx +
                (SharedPcsXfiStatus10_t - SharedPcsXfiStatus00_t)*sub_idx;
        cmd = DRV_IOR(tb_id, SharedPcsXfiStatus00_linkFaultState0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_fault = (value == 0) ? "No":"Yes";
    }
    else if(port_attr->pcs_mode == CTC_CHIP_SERDES_XLG_MODE)
    {
        tb_id = SharedPcsXlgStatus0_t + (mac_id + port_attr->slice_id*48)/4;
        cmd = DRV_IOR(tb_id, SharedPcsXlgStatus0_linkFaultState_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        p_fault = (value == 0) ? "No":"Yes";
    }
    else
    {
        p_fault = "-";
    }
    if(serdes_num == 1)
    {
        SYS_PORT_DBG_DUMP("%s %-12d %s %-15d %s %-16s %s %-12d\n",
                          "Sigdet   :",sigdet, "PRBS    :",enable[0], "Fault   :",p_fault, "Temperature:",temperature);
    }
    else
    {
        SYS_PORT_DBG_DUMP("%s %-12d %s %d/%d/%d/%-9d %s %-16s %s %-12d\n",
                          "Sigdet   :",sigdet, "PRBS    :",enable[0],enable[1],enable[2],enable[3], "Fault   :",p_fault, "Temperature:",temperature);
    }
    /* get ffe mode and value */
    for(serdes_num_tmp = 0; serdes_num_tmp < serdes_num; serdes_num_tmp ++)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe(lchip, serdes_id +serdes_num_tmp,coefficient,CTC_CHIP_SERDES_FFE_MODE_3AP,&status));
        if(status == 1 && serdes_num_tmp == 0)
        {
            SYS_PORT_DBG_DUMP("%s %s/%d/%d/%d/%d",
                          "FFE       :",ffe_mod[0], coefficient[0],coefficient[1], coefficient[2],coefficient[3]);
        }
        else if(status == 1)
        {
            SYS_PORT_DBG_DUMP("  %d/%d/%d/%d", coefficient[0],coefficient[1], coefficient[2],coefficient[3]);
        }
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe(lchip, serdes_id +serdes_num_tmp,coefficient,CTC_CHIP_SERDES_FFE_MODE_DEFINE,&status));
        if(status == 1 && serdes_num_tmp == 0)
        {
            SYS_PORT_DBG_DUMP("%s %s/%d/%d/%d/%d",
                          "FFE      :",ffe_mod[1], coefficient[0],coefficient[1], coefficient[2],coefficient[3]);
        }
        else if(status == 1)
        {
            SYS_PORT_DBG_DUMP("  %d/%d/%d/%d", coefficient[0],coefficient[1], coefficient[2],coefficient[3]);
        }
    }
    SYS_PORT_DBG_DUMP("\n");
    SYS_PORT_DBG_DUMP("------------------------------------------------------------------------------------------\n");
    SYS_PORT_DBG_DUMP("Check  Process:\n");
    CTC_ERROR_RETURN(_sys_goldengate_port_module_reset_check(lchip, port_attr));
    CTC_ERROR_RETURN(_sys_goldengate_port_hss_cs_macrco_check(lchip, lane_id, gport, hss_id,loopback_inter.enable,hss_type,serdes_num));
    CTC_ERROR_RETURN(_sys_goldengate_port_pcs_status_check(lchip, port_attr, gport, auto_neg_en));

    sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_rx_stats(lchip, gport, &mac_stats));
    if(mac_stats.u.stats_detail.stats.rx_stats.fcs_error_bytes == 0)
    {
        SYS_PORT_DBG_DUMP("RX Status check       --------------   Pass\n");
    }
    else
    {
        SYS_PORT_DBG_DUMP("RX Status check       --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  Crc Error,SI problem\n");
    }

    sal_memset(&mac_stats, 0, sizeof(ctc_mac_stats_t));
    mac_stats.stats_mode = CTC_STATS_MODE_DETAIL;
    CTC_ERROR_RETURN(sys_goldengate_stats_get_mac_tx_stats(lchip, gport, &mac_stats));

    tb_id = NetTxPortDynamicInfo0_t + port_attr->slice_id;
    cmd = DRV_IOR(tb_id, NetTxPortDynamicInfo0_writeInProc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, (port_attr->mac_id%64), cmd, &value));
    value_64 = mac_stats.u.stats_detail.stats.tx_stats.good_bcast_bytes + mac_stats.u.stats_detail.stats.tx_stats.good_mcast_bytes
               + mac_stats.u.stats_detail.stats.tx_stats.good_ucast_bytes + mac_stats.u.stats_detail.stats.tx_stats.good_pause_bytes
               + mac_stats.u.stats_detail.stats.tx_stats.good_control_bytes + mac_stats.u.stats_detail.stats.tx_stats.bytes_63
               + mac_stats.u.stats_detail.stats.tx_stats.bytes_64 + mac_stats.u.stats_detail.stats.tx_stats.bytes_65_to_127
               + mac_stats.u.stats_detail.stats.tx_stats.bytes_128_to_255 + mac_stats.u.stats_detail.stats.tx_stats.bytes_256_to_511
               + mac_stats.u.stats_detail.stats.tx_stats.bytes_512_to_1023 + mac_stats.u.stats_detail.stats.tx_stats.bytes_1024_to_1518
               + mac_stats.u.stats_detail.stats.tx_stats.bytes_1519 + mac_stats.u.stats_detail.stats.tx_stats.jumbo_bytes
               + mac_stats.u.stats_detail.stats.tx_stats.fcs_error_bytes;
    if(mac_stats.u.stats_detail.stats.tx_stats.mac_underrun_bytes != 0)
    {
        SYS_PORT_DBG_DUMP("TX Status check       --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  There is MAC underrun packet\n")
    }
    else if(value && value_64 == 0)
    {
        SYS_PORT_DBG_DUMP("TX Status check       --------------   Fail\n");
        SYS_PORT_DBG_DUMP("  NetTx send no packet to Mac\n");
    }
    else
    {
        SYS_PORT_DBG_DUMP("TX Status check       --------------   Pass\n");
    }
    return CTC_E_NONE;
}

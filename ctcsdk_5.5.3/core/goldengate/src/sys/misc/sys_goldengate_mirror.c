/**
 @file sys_goldengate_mirror.c

 @date 2009-10-21

 @version v2.0
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_common.h"
#include "ctc_const.h"
#include "ctc_cpu_traffic.h"
#include "ctc_packet.h"

#include "sys_goldengate_chip.h"
#include "sys_goldengate_mirror.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_l3if.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_internal_port.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_cpu_reason.h"
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

#define MAX_SYS_MIRROR_SESSION_TYPE (MAX_CTC_MIRROR_SESSION_TYPE + SYS_MIRROR_ACL_ID_MAX)
#define SYS_MIRROR_ACL_ID_MAX 4

#define SYS_MIRROR_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_mirror_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_MIRROR_CREATE_LOCK(lchip)                           \
    do                                                          \
    {                                                           \
        sal_mutex_create(&p_gg_mirror_master[lchip]->mutex);    \
        if (NULL == p_gg_mirror_master[lchip]->mutex)           \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_MIRROR_LOCK(lchip) \
    sal_mutex_lock(p_gg_mirror_master[lchip]->mutex)

#define SYS_MIRROR_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_mirror_master[lchip]->mutex)

#define CTC_ERROR_RETURN_MIRROR_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_mirror_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define MIRROR_SET_MAC(d, s)       sal_memcpy(d, s, sizeof(mac_addr_t))
#define MIRROR_SET_HW_MAC(d, s)    SYS_GOLDENGATE_SET_HW_MAC(d, s)

/* mirror's seesion-id correspond to per acl lookup's priority-id, and mirror's priority-id correspond to acl lookup index */
#define SYS_MIRROR_ACL_LOG_EXCEPTION_IDX(dir, session_id, priority_id)\
        ((CTC_INGRESS == dir)\
        ? (SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE + (priority_id * MAX_CTC_MIRROR_SESSION_ID) + session_id)\
        : (SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE + (priority_id * MAX_CTC_MIRROR_SESSION_ID) + session_id))

struct sys_mirror_master_s
{
    uint32 mirror_nh[MAX_CTC_MIRROR_SESSION_ID][MAX_SYS_MIRROR_SESSION_TYPE][CTC_BOTH_DIRECTION];
    uint8 external_nh[MAX_CTC_MIRROR_SESSION_ID][MAX_SYS_MIRROR_SESSION_TYPE][CTC_BOTH_DIRECTION];
    sal_mutex_t    * mutex;
};
typedef struct sys_mirror_master_s sys_mirror_master_t;

sys_mirror_master_t* p_gg_mirror_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
* Function
*
*****************************************************************************/
/*
 @brief This function is to write chip to set the destination port
*/

int32
sys_goldengate_mirror_wb_sync(uint8 lchip);

int32
sys_goldengate_mirror_wb_restore(uint8 lchip);

STATIC int32
_sys_goldengate_mirror_set_iloop_port(uint8 lchip, uint32 nh_id, bool is_add)
{
    sys_nh_info_com_t* p_nhinfo = NULL;
    uint16 lport = 0;
    uint32 value = 0;
    uint32 gport = 0;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, nh_id, &p_nhinfo));
    if (p_nhinfo->hdr.nh_entry_type != SYS_NH_TYPE_ILOOP)
    {
        return CTC_E_NONE;
    }

    if (CTC_IS_BIT_SET(p_nhinfo->hdr.dsfwd_info.dsnh_offset, 11)
        && (( 7 == ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7))
        || (0 == ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7))))
    {
        sys_goldengate_get_gchip_id(lchip, &gchip);
        lport = (p_nhinfo->hdr.dsfwd_info.dsnh_offset & 0x7F) | (((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 16)&0x1) << 7);
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_ILOOP_EN, (is_add?1:0));

        value = is_add? ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7) : 0;
        sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_PKT_TYPE, value);

        if (7 == ((p_nhinfo->hdr.dsfwd_info.dsnh_offset >> 12) & 0x7))
        {
            sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_HASH_GEN_DIS, (is_add? 1 : 0));
        }
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mirror_set_dest_write_chip(uint8 lchip, uint32 excp_index, uint32 dest_gport, uint32 nh_ptr, uint8 truncation_en)
{
    uint8  gchip_id = 0;
    uint16 lport = 0;
    uint16 lport_slice = 0;
    uint32 destmap = 0;
    uint32 cmd = 0;
    uint8  sub_queue_id = 0;
    DsMetFifoExcp_m met_excp;
    DsBufRetrvExcp_m bufrev_exp;
    BufferRetrieveTruncationCtl_m TruncationCtl;
    uint32 value[8] = {0};

    sal_memset(&met_excp, 0, sizeof(DsMetFifoExcp_m));
    sal_memset(&bufrev_exp, 0, sizeof(DsBufRetrvExcp_m));
    sal_memset(&TruncationCtl, 0, sizeof(BufferRetrieveTruncationCtl_m));

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dest_gport);
    lport_slice = lport & 0xFF;
    if (CTC_IS_LINKAGG_PORT(dest_gport))
    {
        destmap = SYS_ENCODE_MIRROR_DESTMAP(CTC_LINKAGG_CHIPID, CTC_GPORT_LINKAGG_ID(dest_gport));

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "excp_index = %d, dest_gport = %d, nh_ptr = %d\n", excp_index, dest_gport, nh_ptr&0x7FFFFFFF);

        /*read DsMetFifoExcp_t table*/
        cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
        SetDsMetFifoExcp(V, destMap_f, &met_excp, destmap);
        SetDsMetFifoExcp(V, nextHopExt_f, &met_excp, CTC_IS_BIT_SET(nh_ptr, 31));

        /*write DsMetFifoExcp_t table*/
        cmd = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));

        /*read DsBufRetrvExcp_t table, 0 is slice 0, 1 is slice 1*/
        cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (0 * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (1 * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));

        SetDsBufRetrvExcp(V, nextHopPtr_f, &bufrev_exp, nh_ptr);

        /*write DsBufRetrvExcp_t table, 0 is slice 0, 1 is slice 1*/
        cmd = DRV_IOW(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (0 * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (1 * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));

    }
    else
    {
        gchip_id = SYS_MAP_CTC_GPORT_TO_GCHIP(dest_gport);

        if (SYS_RSV_PORT_DROP_ID == lport_slice)
        {
            sys_goldengate_get_gchip_id(lchip, &gchip_id);
            destmap = SYS_ENCODE_DESTMAP( gchip_id, SYS_RSV_PORT_DROP_ID);
        }
        else if (CTC_IS_CPU_PORT(dest_gport))
        {
            CTC_ERROR_RETURN(sys_goldengate_get_sub_queue_id_by_cpu_reason(lchip, CTC_PKT_CPU_REASON_MIRRORED_TO_CPU, &sub_queue_id));
            destmap = SYS_ENCODE_EXCP_DESTMAP(gchip_id, sub_queue_id);
            nh_ptr = CTC_PKT_CPU_REASON_BUILD_NHPTR(CTC_PKT_CPU_REASON_MIRRORED_TO_CPU, 0);
        }
        else  if (lport_slice  > SYS_PHY_PORT_NUM_PER_SLICE)
        {
            destmap = SYS_ENCODE_DESTMAP(gchip_id, lport);
        }
        else
        {
            destmap = SYS_ENCODE_MIRROR_DESTMAP(gchip_id, lport);
        }

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "excp_index = %d, dest_gport = %d, nh_ptr = %d\n", excp_index, dest_gport, nh_ptr&0x7FFFFFFF);

        /*read DsMetFifoExcp_t table*/
        cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
        SetDsMetFifoExcp(V, destMap_f, &met_excp, destmap);
        SetDsMetFifoExcp(V, nextHopExt_f, &met_excp, CTC_IS_BIT_SET(nh_ptr, 31));
        /*write DsMetFifoExcp_t table*/
        cmd = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));

        /*read DsBufRetrvExcp_t table*/
        cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index , cmd, &bufrev_exp));
        SetDsBufRetrvExcp(V, nextHopPtr_f, &bufrev_exp, nh_ptr&0x7FFFFFFF);
        /*write DsBufRetrvExcp_t table*/
        cmd = DRV_IOW(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd, &bufrev_exp));


    }

    /*set truncation enable*/
    cmd = DRV_IOR(BufferRetrieveTruncationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &TruncationCtl));
    GetBufferRetrieveTruncationCtl(A, truncationEn_f,  &TruncationCtl, &value);
    if (truncation_en)
    {
        CTC_BIT_SET(value[excp_index / 32], excp_index % 32);
    }
    else
    {
        CTC_BIT_UNSET(value[excp_index / 32], excp_index % 32);
    }
    SetBufferRetrieveTruncationCtl(A, truncationEn_f,  &TruncationCtl, &value);
    cmd = DRV_IOW(BufferRetrieveTruncationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &TruncationCtl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &TruncationCtl));

    return CTC_E_NONE;

}

/*
 @brief This function is to initialize the mirror module
*/
int32
sys_goldengate_mirror_init(uint8 lchip)
{
    uint8  session_id = 0;
    uint8  gchip_id = 0;
    uint8  idx = 0;
    uint8  cpu_eth_en = 0;
    uint8  cpu_channel = 0;
    uint32 cmd1 = 0;
    uint32 cmd2 = 0;
    uint32 excp_index = 0;
    uint32 nh_ptr = 0;
    uint32 cmd = 0;
    uint32 dest_map = 0;
    mac_addr_t mac_addr;
    DsMetFifoExcp_m met_excp;
    DsBufRetrvExcp_m bufrev_exp;
    EpeMirrorEscapeCam_m mirror_escape;
    BufferStoreCtl_m buffer_store_ctl;
    BufStoreChanIdCfg_m buf_store_chan_id_cfg;
    EpeNextHopCtl_m ctl;

    if (p_gg_mirror_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_memset(&met_excp, 0, sizeof(DsMetFifoExcp_m));
    sal_memset(&bufrev_exp, 0, sizeof(DsBufRetrvExcp_m));
    cmd1 = DRV_IOW(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    cmd2 = DRV_IOW(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(sys_goldengate_nh_get_mirror_dsnh_offset(lchip, &nh_ptr));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    dest_map = SYS_ENCODE_DESTMAP( gchip_id, SYS_RSV_PORT_DROP_ID);

    SetDsMetFifoExcp(V, destMap_f, &met_excp, dest_map);
    SetDsBufRetrvExcp(V, nextHopPtr_f, &bufrev_exp, nh_ptr);
    SetDsBufRetrvExcp(V, packetOffsetReset_f, &bufrev_exp, 1);

    for (session_id = 0; session_id < MAX_CTC_MIRROR_SESSION_ID; session_id++)
    {
        /*port session ingress*/
        excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        /*port session egress*/
        excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        /*vlan session ingress*/
        excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        /*vlan session egress*/
        excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        /*acl session ingress*/
        excp_index = SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 4 + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 8 + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + 12 + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

        /*acl session egress*/
        excp_index = SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE + session_id;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

    }


    /*Rx cpu session*/
    excp_index = SYS_MIRROR_CPU_RX_SPAN_INDEX;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

    /*Tx cpu session*/
    excp_index = SYS_MIRROR_CPU_TX_SPAN_INDEX;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd1, &met_excp));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd2, &bufrev_exp));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + SYS_CHIP_PER_SLICE_PORT_NUM, cmd2, &bufrev_exp));

    p_gg_mirror_master[lchip] = (sys_mirror_master_t*)mem_malloc(MEM_MIRROR_MODULE, sizeof(sys_mirror_master_t));
    if (NULL == p_gg_mirror_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_mirror_master[lchip], 0, sizeof(sys_mirror_master_t));
    sal_memset(&mirror_escape, 0, sizeof(EpeMirrorEscapeCam_m));

    sal_memset(&mac_addr, 0xFF, sizeof(mac_addr_t));
    SetEpeMirrorEscapeCam(A, array_0_macDaMask_f, &mirror_escape, &mac_addr);
    SetEpeMirrorEscapeCam(A, array_1_macDaMask_f, &mirror_escape, &mac_addr);

    SetEpeMirrorEscapeCam(V, array_0_valid_f, &mirror_escape, 0);
    SetEpeMirrorEscapeCam(V, array_1_valid_f, &mirror_escape, 0);

    cmd = DRV_IOW(EpeMirrorEscapeCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mirror_escape));

    sal_memset(&buffer_store_ctl, 0, sizeof(BufferStoreCtl_m));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

    SetBufferStoreCtl(V, ingressExceptionMirrorBitmap_f, &buffer_store_ctl, 0x1F9);
    SetBufferStoreCtl(V, egressExceptionMirrorBitmap_f, &buffer_store_ctl, 0x1F9);
    SetBufferStoreCtl(V, cpuRxPortModeEn_f, &buffer_store_ctl, 0);
    SetBufferStoreCtl(V, cpuRxQueueModeEn_f, &buffer_store_ctl, 1);
    SetBufferStoreCtl(V, cpuQueueSelType_f, &buffer_store_ctl, 1);

    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

    for (idx = 0; idx < 2; idx++)
    {
        sal_memset(&buf_store_chan_id_cfg, 0, sizeof(BufStoreChanIdCfg_m));
        cmd = DRV_IOR(BufStoreChanIdCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, idx, cmd, &buf_store_chan_id_cfg));

        SetBufStoreChanIdCfg(V, cfgBsCpuChanId_f, &buf_store_chan_id_cfg, SYS_DMA_CHANNEL_ID);
        CTC_ERROR_RETURN(sys_goldengate_get_chip_cpu_eth_en(lchip, &cpu_eth_en, &cpu_channel));
        if (!cpu_eth_en)
        {
            cpu_channel = 0x3F;
        }
        SetBufStoreChanIdCfg(V, networkPortCpuChanId0_f, &buf_store_chan_id_cfg, cpu_channel);
        SetBufStoreChanIdCfg(V, networkPortCpuChanId1_f, &buf_store_chan_id_cfg, 0x3F);
        SetBufStoreChanIdCfg(V, networkPortCpuChanId2_f, &buf_store_chan_id_cfg, 0x3F);
        SetBufStoreChanIdCfg(V, networkPortCpuChanId3_f, &buf_store_chan_id_cfg, 0x3F);
        SetBufStoreChanIdCfg(V, cfgBsDmaChanId_f, &buf_store_chan_id_cfg, SYS_DMA_CHANNEL_ID);

        cmd = DRV_IOW(BufStoreChanIdCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, idx, cmd, &buf_store_chan_id_cfg));
    }

    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &ctl));
    SetEpeNextHopCtl(V, symmetricalHashCtl_0_hashEn_f, &ctl, 1);
    SetEpeNextHopCtl(V, symmetricalHashCtl_1_hashEn_f, &ctl, 1);
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &ctl));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_MIRROR, sys_goldengate_mirror_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_mirror_wb_restore(lchip));
    }

    SYS_MIRROR_CREATE_LOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_mirror_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gg_mirror_master[lchip]->mutex);
    mem_free(p_gg_mirror_master[lchip]);

    return CTC_E_NONE;
}

/*
 @brief This function is to set port able to mirror
*/
int32
sys_goldengate_mirror_set_port_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool enable, uint8 session_id)
{
    uint16 lport = 0;
    uint32 cmd = 0;
    BufferStoreCtl_m buffer_store_ctl;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_IS_CPU_PORT(gport))
    {
        sal_memset(&buffer_store_ctl, 0, sizeof(BufferStoreCtl_m));

        cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            SetBufferStoreCtl(V, cpuRxExceptionEn0_f, &buffer_store_ctl, ((enable == TRUE)? 1 : 0));
            SetBufferStoreCtl(V, cpuRxExceptionEn1_f, &buffer_store_ctl, ((enable == TRUE)? 1 : 0));
        }
        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            SetBufferStoreCtl(V, cpuTxExceptionEn_f, &buffer_store_ctl, ((enable == TRUE)? 1 : 0));
        }

        cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        return CTC_E_NONE;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (TRUE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = 0x%X, direction = %d, span_id = %d\n", gport, dir, session_id);

        if (session_id >= MAX_CTC_MIRROR_SESSION_ID)
        {
            return CTC_E_MIRROR_EXCEED_SESSION;
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, session_id));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, TRUE));
    }
    else if (FALSE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = 0x%X, direction = %d\n", gport, dir);

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, FALSE));
    }

    return CTC_E_NONE;
}

/*
 @brief This function is to get the information of port mirror
*/
int32
sys_goldengate_mirror_get_port_info(uint8 lchip, uint16 gport, ctc_direction_t dir, bool* enable, uint8* session_id)
{
    uint32 value = 0;
    uint32 value_enable = 0;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport = %d, direction = %d\n", gport, dir);

    CTC_PTR_VALID_CHECK(enable);
    CTC_PTR_VALID_CHECK(session_id);

    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &value_enable));
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &value));
    *session_id = value;
    *enable = value_enable ? TRUE : FALSE;

    return CTC_E_NONE;
}

/*
 @brief This function is to set vlan able to mirror
*/
int32
sys_goldengate_mirror_set_vlan_en(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool enable, uint8 session_id)
{

    int32 ret = 0;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    if (TRUE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d, span_id = %d\n", vlan_id, dir, session_id);

        CTC_VLAN_RANGE_CHECK(vlan_id);

        if (session_id >= MAX_CTC_MIRROR_SESSION_ID)
        {
            return CTC_E_MIRROR_EXCEED_SESSION;
        }

        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, session_id);
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, 1);
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, session_id);
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, 1);
        }
    }
    else if (FALSE == enable)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d\n", vlan_id, dir);

        CTC_VLAN_RANGE_CHECK(vlan_id);

        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, 0);
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            ret = ret ? ret : sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, 0);
        }
    }

    return ret;

}

/*
 @brief This function is to get the information of vlan mirror
*/
int32
sys_goldengate_mirror_get_vlan_info(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool* enable, uint8* session_id)
{
    int32           ret = 0;
    uint32          value = 0;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan_id = %d, direction = %d\n", vlan_id, dir);

    CTC_VLAN_RANGE_CHECK(vlan_id);
    CTC_PTR_VALID_CHECK(enable);
    CTC_PTR_VALID_CHECK(session_id);

    if (dir == CTC_INGRESS)
    {
        ret = ret ? ret : sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, &value);
        *session_id = value;
        ret = ret ? ret : sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, &value);
        *enable = value;
    }
    else if (dir == CTC_EGRESS)
    {
        ret = ret ? ret : sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, &value);
        *session_id = value;
        ret = ret ? ret : sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, &value);
        *enable = value;
    }
    else
    {
        return CTC_E_INVALID_DIR;
    }

    return ret;

}

STATIC int32
_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(uint8 lchip, ctc_mirror_dest_t* mirror, uint32 dest_gport, uint32 nh_ptr)
{
    uint32 excp_index = 0;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "direction = %d, dest_gport = %d, session_id = %d, nh_ptr = %d\n", mirror->dir, dest_gport, mirror->session_id, nh_ptr);

    CTC_MAX_VALUE_CHECK(mirror->dir, CTC_EGRESS);

    if (mirror->type == CTC_MIRROR_L2SPAN_SESSION)    /*port mirror*/
    {
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + mirror->session_id;
        }
        else if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + mirror->session_id;
        }

        CTC_ERROR_RETURN(_sys_goldengate_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr, mirror->truncation_en));
    }
    else if (mirror->type == CTC_MIRROR_L3SPAN_SESSION)   /*vlan mirror*/
    {
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + mirror->session_id;
        }
        else if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + mirror->session_id;
        }
        CTC_ERROR_RETURN(_sys_goldengate_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr, mirror->truncation_en));
    }
    else if (mirror->type == CTC_MIRROR_ACLLOG_SESSION)  /*acllog0 mirror set dest*/
    {
        CTC_MAX_VALUE_CHECK(mirror->session_id, MAX_CTC_MIRROR_SESSION_ID - 1);

        excp_index = SYS_MIRROR_ACL_LOG_EXCEPTION_IDX(mirror->dir, mirror->session_id, mirror->acl_priority);
        CTC_ERROR_RETURN(_sys_goldengate_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr, mirror->truncation_en));
    }
    else if (mirror->type == CTC_MIRROR_CPU_SESSION)
    {
        if (CTC_INGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_CPU_RX_SPAN_INDEX;
        }
        else if (CTC_EGRESS == mirror->dir)
        {
            excp_index = SYS_MIRROR_CPU_TX_SPAN_INDEX;
        }
        CTC_ERROR_RETURN(_sys_goldengate_mirror_set_dest_write_chip(lchip, excp_index, dest_gport, nh_ptr, mirror->truncation_en));
    }

    return CTC_E_NONE;
}

/*
 @brief This function is to set remote mirror destination port
*/
STATIC int32
_sys_goldengate_mirror_rspan_set_dest(uint8 lchip, ctc_mirror_dest_t* mirror)
{
    uint32 dsnh_offset = 0;
    uint8 type = 0;
    uint32 temp_gport = 0;
    int32 ret = CTC_E_NONE;
    uint32 nh_id = CTC_MAX_UINT32_VALUE;
    ctc_rspan_nexthop_param_t  temp_p_nh_param;

    sal_memset(&temp_p_nh_param, 0, sizeof(ctc_rspan_nexthop_param_t));

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dest_gport = %d, direction = %d, session_id = %d",
                       mirror->dest_gport, mirror->dir, mirror->session_id);

    CTC_GLOBAL_PORT_CHECK(mirror->dest_gport);

    if (1 == mirror->vlan_valid)
    {
        CTC_VLAN_RANGE_CHECK(mirror->rspan.vlan_id);
        if (!sys_goldengate_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(mirror->dest_gport)) && !CTC_IS_LINKAGG_PORT(mirror->dest_gport))
        {
            temp_p_nh_param.remote_chip = TRUE;
        }

        temp_p_nh_param.rspan_vid = mirror->rspan.vlan_id;
        CTC_ERROR_RETURN(sys_goldengate_rspan_nh_create(lchip, &nh_id, CTC_NH_RESERVED_DSNH_OFFSET_FOR_RSPAN, &temp_p_nh_param));
    }
    else
    {
        nh_id = mirror->rspan.nh_id;
    }

    CTC_ERROR_RETURN(sys_goldengate_nh_get_mirror_info_by_nhid(lchip, nh_id, &dsnh_offset, &temp_gport, TRUE));

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    if (temp_gport == CTC_MAX_UINT16_VALUE)
    {
        temp_gport = mirror->dest_gport;
    }

    CTC_ERROR_RETURN(_sys_goldengate_mirror_set_iloop_port(lchip, nh_id, TRUE));
    CTC_ERROR_RETURN(_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(lchip, mirror, temp_gport, dsnh_offset));
    type = mirror->type;
    if (mirror->type == CTC_MIRROR_ACLLOG_SESSION)
    {
        type = CTC_MIRROR_ACLLOG_SESSION + mirror->acl_priority;
    }
    else if (mirror->type > CTC_MIRROR_ACLLOG_SESSION)
    {
        type = mirror->type + SYS_MIRROR_ACL_ID_MAX  - 1;
    }

    SYS_MIRROR_LOCK(lchip);
    p_gg_mirror_master[lchip]->mirror_nh[mirror->session_id][type][mirror->dir] = nh_id;
    if (0 == mirror->vlan_valid)
    {
        p_gg_mirror_master[lchip]->external_nh[mirror->session_id][type][mirror->dir] = 1;
    }
    SYS_MIRROR_UNLOCK(lchip);

    return CTC_E_NONE;

}

/*
 @brief This function is to set local mirror destination port
*/
int32
sys_goldengate_mirror_set_dest(uint8 lchip, ctc_mirror_dest_t* p_mirror)
{
    uint8  type = 0;
    uint32 dsnh_offset = 0;
    uint32 temp_gport = 0;
    uint32 nh_ptr = 0;
    uint32 nh_id = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dest_gport = %d, direction = %d, session_id = %d\n",
                       p_mirror->dest_gport, p_mirror->dir, p_mirror->session_id);

    if (p_mirror->type >= MAX_CTC_MIRROR_SESSION_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_MIRROR_CPU_SESSION == p_mirror->type)
    {
        p_mirror->session_id = 0;
    }

    if (p_mirror->session_id >= MAX_CTC_MIRROR_SESSION_ID)
    {
        return CTC_E_MIRROR_EXCEED_SESSION;
    }

    type = p_mirror->type;
    if (p_mirror->type == CTC_MIRROR_ACLLOG_SESSION)
    {
        if (p_mirror->acl_priority < SYS_MIRROR_ACL_ID_MAX)
        {
            type = CTC_MIRROR_ACLLOG_SESSION + p_mirror->acl_priority;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_mirror->type > CTC_MIRROR_ACLLOG_SESSION)
    {
        type = p_mirror->type + SYS_MIRROR_ACL_ID_MAX - 1;
    }


    if (p_mirror->dir > CTC_BOTH_DIRECTION)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (((CTC_MIRROR_ACLLOG_SESSION == p_mirror->type) && (0 != p_mirror->acl_priority))
        && ((CTC_BOTH_DIRECTION == p_mirror->dir) || (CTC_EGRESS == p_mirror->dir)))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MIRROR_LOCK(lchip);
    /*delete external nexthop mirror session*/
    if ((p_mirror->dir == CTC_INGRESS || p_mirror->dir == CTC_BOTH_DIRECTION)
        && p_gg_mirror_master[lchip]->external_nh[p_mirror->session_id][type][CTC_INGRESS])
    {
        nh_id = p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_INGRESS];
        CTC_ERROR_RETURN_MIRROR_UNLOCK( _sys_goldengate_mirror_set_iloop_port(lchip, nh_id, FALSE));
        CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_goldengate_nh_get_mirror_info_by_nhid(lchip, nh_id, &dsnh_offset, &temp_gport, FALSE));
        p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_INGRESS] = 0;
        p_gg_mirror_master[lchip]->external_nh[p_mirror->session_id][type][CTC_INGRESS] = 0;
    }

    if ((p_mirror->dir == CTC_EGRESS || p_mirror->dir == CTC_BOTH_DIRECTION)
        && p_gg_mirror_master[lchip]->external_nh[p_mirror->session_id][type][CTC_EGRESS])
    {
        nh_id = p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_EGRESS];
        CTC_ERROR_RETURN_MIRROR_UNLOCK( _sys_goldengate_mirror_set_iloop_port(lchip, nh_id, FALSE));
        CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_goldengate_nh_get_mirror_info_by_nhid(lchip, nh_id, &dsnh_offset, &temp_gport, FALSE));
        p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_EGRESS] = 0;
        p_gg_mirror_master[lchip]->external_nh[p_mirror->session_id][type][CTC_EGRESS] = 0;
    }

    /*delete old mirror session*/
    if ((p_mirror->dir == CTC_INGRESS || p_mirror->dir == CTC_BOTH_DIRECTION))
    {
        nh_id = p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_INGRESS];
        if (nh_id != 0)
        {
            CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_goldengate_rspan_nh_delete(lchip, nh_id));
            p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_INGRESS] = 0;
        }
    }

    if ((p_mirror->dir == CTC_EGRESS || p_mirror->dir == CTC_BOTH_DIRECTION))
    {
        nh_id = p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_EGRESS];
        if (nh_id != 0)
        {
            CTC_ERROR_RETURN_MIRROR_UNLOCK(sys_goldengate_rspan_nh_delete(lchip, nh_id));
            p_gg_mirror_master[lchip]->mirror_nh[p_mirror->session_id][type][CTC_EGRESS] = 0;
        }
    }
    SYS_MIRROR_UNLOCK(lchip);

    /*add new mirror session*/
    if (p_mirror->is_rspan)
    {
        if (p_mirror->dir != CTC_BOTH_DIRECTION)
        {
            CTC_ERROR_RETURN(_sys_goldengate_mirror_rspan_set_dest(lchip, p_mirror));
        }
        else if (p_mirror->dir == CTC_BOTH_DIRECTION)
        {
            p_mirror->dir = CTC_INGRESS;
            CTC_ERROR_RETURN(_sys_goldengate_mirror_rspan_set_dest(lchip, p_mirror));
            p_mirror->dir = CTC_EGRESS;
            CTC_ERROR_RETURN(_sys_goldengate_mirror_rspan_set_dest(lchip, p_mirror));
            p_mirror->dir = CTC_BOTH_DIRECTION;
        }
    }
    else
    {
        CTC_GLOBAL_PORT_CHECK(p_mirror->dest_gport);
        CTC_ERROR_RETURN(sys_goldengate_nh_get_mirror_dsnh_offset(lchip, &nh_ptr));

        if (p_mirror->dir == CTC_INGRESS)
        {
            CTC_ERROR_RETURN(_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(lchip, p_mirror, p_mirror->dest_gport, nh_ptr));
        }
        else if (p_mirror->dir == CTC_BOTH_DIRECTION)
        {
            p_mirror->dir = CTC_INGRESS;
            CTC_ERROR_RETURN(_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(lchip, p_mirror, p_mirror->dest_gport, nh_ptr));
            p_mirror->dir = CTC_BOTH_DIRECTION;
        }

        if (p_mirror->dir == CTC_EGRESS)
        {
            CTC_ERROR_RETURN(_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(lchip, p_mirror, p_mirror->dest_gport, nh_ptr));
        }
        else if (p_mirror->dir == CTC_BOTH_DIRECTION)
        {
            p_mirror->dir = CTC_EGRESS;
            CTC_ERROR_RETURN(_sys_goldengate_mirror_set_excp_index_and_dest_and_nhptr(lchip, p_mirror, p_mirror->dest_gport, nh_ptr));
            p_mirror->dir = CTC_BOTH_DIRECTION;
        }

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_rspan_escape_en(uint8 lchip, bool enable)
{
    uint32 cmd;
    uint32 field_val;

    SYS_MIRROR_INIT_CHECK(lchip);

    field_val = (TRUE == enable) ? 1 : 0;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_mirrorEscapeCamEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_rspan_escape_mac(uint8 lchip, ctc_mirror_rspan_escape_t* p_escape)
{
    uint32 cmd = 0;
    hw_mac_addr_t mac_addr;
    EpeMirrorEscapeCam_m mirror_escape;

    SYS_MIRROR_INIT_CHECK(lchip);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    MIRROR_SET_HW_MAC(mac_addr, p_escape->mac0);
    SetEpeMirrorEscapeCam(A, array_0_macDaValue_f, &mirror_escape, &mac_addr);
    MIRROR_SET_HW_MAC(mac_addr, p_escape->mac_mask0);
    SetEpeMirrorEscapeCam(A, array_0_macDaMask_f, &mirror_escape, &mac_addr);

    MIRROR_SET_HW_MAC(mac_addr, p_escape->mac1);
    SetEpeMirrorEscapeCam(A, array_1_macDaValue_f, &mirror_escape, &mac_addr);
    MIRROR_SET_HW_MAC(mac_addr, p_escape->mac_mask1);
    SetEpeMirrorEscapeCam(A, array_1_macDaMask_f, &mirror_escape, &mac_addr);

    SetEpeMirrorEscapeCam(V, array_0_valid_f, &mirror_escape, 1);
    SetEpeMirrorEscapeCam(V, array_1_valid_f, &mirror_escape, 1);

    cmd = DRV_IOW(EpeMirrorEscapeCam_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mirror_escape));

    return CTC_E_NONE;
}

/*
 @brief This function is to remove mirror destination port
*/
int32
sys_goldengate_mirror_unset_dest(uint8 lchip, ctc_mirror_dest_t* p_mirror)
{
    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_mirror);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "session_id = %d, direction = %d, session type = %d\n",
                       p_mirror->session_id, p_mirror->dir, p_mirror->type);
    p_mirror->dest_gport = SYS_RSV_PORT_DROP_ID;
    p_mirror->is_rspan = 0;
    CTC_ERROR_RETURN(sys_goldengate_mirror_set_dest(lchip, p_mirror));

    return CTC_E_NONE;
}

/*
  @brief This functions is used to set packet enable or not to log if the packet is discarded on EPE process.
*/
int32
sys_goldengate_mirror_set_mirror_discard(uint8 lchip, ctc_direction_t dir, uint16 discard_flag, bool enable)
{
    uint32 cmd;
    uint32 logon_discard;
    uint16 discard_flag_temp = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "discard_flag = %d, direction = %d, enable = %d\n", discard_flag, dir, enable);
    SYS_MIRROR_DISCARD_CHECK(discard_flag);

    if (discard_flag & CTC_MIRROR_L2SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L2SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_L3SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L3SPAN_DISCARD);
    }

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
        {
            CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_INGRESS_ACLLOG_PRI_DISCARD);
        }

        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));

        if (TRUE == enable)
        {
            CTC_SET_FLAG(logon_discard, discard_flag_temp);
        }
        else
        {
            CTC_UNSET_FLAG(logon_discard, discard_flag_temp);
        }

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
        {
            CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_EGRESS_ACLLOG_PRI_DISCARD);
        }

        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));

        if (TRUE == enable)
        {
            CTC_SET_FLAG(logon_discard, discard_flag_temp);
        }
        else
        {
            CTC_UNSET_FLAG(logon_discard, discard_flag_temp);
        }

        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &logon_discard));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_get_mirror_discard(uint8 lchip, ctc_direction_t dir, ctc_mirror_discard_t discard_flag, bool* p_enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 discard_flag_temp = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "discard_flag = %d, direction = %d\n", discard_flag, dir);

    CTC_PTR_VALID_CHECK(p_enable);
    SYS_MIRROR_DISCARD_CHECK(discard_flag);

    switch (discard_flag)
    {
    case CTC_MIRROR_L2SPAN_DISCARD:
    case CTC_MIRROR_L3SPAN_DISCARD:
    case CTC_MIRROR_ACLLOG_PRI_DISCARD:
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (discard_flag & CTC_MIRROR_L2SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L2SPAN_DISCARD);
    }

    if (discard_flag & CTC_MIRROR_L3SPAN_DISCARD)
    {
        CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_L3SPAN_DISCARD);
    }

    switch (dir)
    {
    case CTC_INGRESS:
        if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
        {
            CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_INGRESS_ACLLOG_PRI_DISCARD);
        }
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_logOnDiscard_f);
        break;

    case CTC_EGRESS:
        if (discard_flag & CTC_MIRROR_ACLLOG_PRI_DISCARD)
        {
            CTC_SET_FLAG(discard_flag_temp, SYS_MIRROR_EGRESS_ACLLOG_PRI_DISCARD);
        }
        cmd = DRV_IOR(EpeHeaderEditCtl_t, EpeHeaderEditCtl_logOnDiscard_f);
        break;

    default:
        return CTC_E_INVALID_DIR;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_enable = CTC_FLAG_ISSET(field_val, discard_flag_temp) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_set_erspan_psc(uint8 lchip, ctc_mirror_erspan_psc_t* psc)
{
    uint32 value = 0;
    EpeNextHopCtl_m ctl;
    uint32 cmd = 0;

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(psc);

    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd , &ctl));

    if(CTC_FLAG_ISSET(psc->type, CTC_MIRROR_ERSPAN_PSC_TYPE_IPV4))
    {
        value = CTC_FLAG_ISSET(psc->ipv4_flag, CTC_MIRROR_ERSPAN_PSC_IPV4_FLAG_L4_PORT);
        SetEpeNextHopCtl(V,symmetricalHashCtl_0_l4PortEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv4_flag, CTC_MIRROR_ERSPAN_PSC_IPV4_FLAG_IP_ADDR);
        SetEpeNextHopCtl(V,symmetricalHashCtl_0_addrEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv4_flag, CTC_MIRROR_ERSPAN_PSC_IPV4_FLAG_IS_TCP);
        SetEpeNextHopCtl(V,symmetricalHashCtl_0_tcpEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv4_flag, CTC_MIRROR_ERSPAN_PSC_IPV4_FLAG_IS_UDP);
        SetEpeNextHopCtl(V,symmetricalHashCtl_0_udpEn_f, &ctl, value);
    }

    if(CTC_FLAG_ISSET(psc->type, CTC_MIRROR_ERSPAN_PSC_TYPE_IPV6))
    {
        value = CTC_FLAG_ISSET(psc->ipv6_flag, CTC_MIRROR_ERSPAN_PSC_IPV6_FLAG_L4_PORT);
        SetEpeNextHopCtl(V,symmetricalHashCtl_1_l4PortEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv6_flag, CTC_MIRROR_ERSPAN_PSC_IPV6_FLAG_IP_ADDR);
        SetEpeNextHopCtl(V,symmetricalHashCtl_1_addrEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv6_flag, CTC_MIRROR_ERSPAN_PSC_IPV6_FLAG_IS_TCP);
        SetEpeNextHopCtl(V,symmetricalHashCtl_1_tcpEn_f, &ctl, value);

        value = CTC_FLAG_ISSET(psc->ipv6_flag, CTC_MIRROR_ERSPAN_PSC_IPV6_FLAG_IS_UDP);
        SetEpeNextHopCtl(V,symmetricalHashCtl_1_udpEn_f, &ctl, value);
    }

    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mirror_show_port_info(uint8 lchip, uint32 gport)
{
    uint16 drv_gport = 0;
    uint16 lport = 0;
    uint16 dst_gport = 0;
    uint32 value = 0;
    uint32 nh_ptr = 0;
    uint32 excp_index = 0;
    uint32 cmd = 0;
    uint32 session_id = 0;
    uint32 enable = FALSE;
    ctc_direction_t dir;
    DsMetFifoExcp_m met_excp;
    DsBufRetrvExcp_m bufrev_exp;
    BufferStoreCtl_m buffer_store_ctl;

    sal_memset(&met_excp, 0, sizeof(DsMetFifoExcp_m));
    sal_memset(&bufrev_exp, 0, sizeof(DsBufRetrvExcp_m));

    SYS_MIRROR_INIT_CHECK(lchip);

    if (CTC_IS_CPU_PORT(gport))
    {
        dir = CTC_INGRESS;
        cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        GetBufferStoreCtl(A, cpuRxExceptionEn1_f, &buffer_store_ctl, &enable);

        excp_index = SYS_MIRROR_CPU_RX_SPAN_INDEX;
        cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
        GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
        drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
        dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dst_gport);
        cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
        GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
        nh_ptr = value;

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Port=0x%.4x \n", gport);

        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

        if (enable == TRUE)
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :cpu mirror don't care session id \n");
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
        }
        else
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");
        }

        enable = FALSE;
        dir = CTC_EGRESS;
        cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
        GetBufferStoreCtl(A, cpuTxExceptionEn_f, &buffer_store_ctl, &enable);

        excp_index = SYS_MIRROR_CPU_TX_SPAN_INDEX;
        cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
        GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
        drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
        dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

        lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dst_gport);
        cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
        GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
        nh_ptr = value;

        if (enable == TRUE)
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :cpu mirror don't care session id \n");
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
        }
        else
        {
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");
        }

        return CTC_E_NONE;
    }


    dir = CTC_INGRESS;
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &enable));
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &session_id));

    excp_index = SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
    drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
    dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dst_gport);
    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
    GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
    nh_ptr = value;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Port=0x%.4x \n", gport);

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    if (enable == TRUE)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :seesion Id: %d \n", session_id);
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");
    }

    enable = FALSE;
    dir = CTC_EGRESS;
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_EN, dir, &enable));
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_L2_SPAN_ID, dir, &session_id));

    excp_index = SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
    drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
    dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
    GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
    nh_ptr = value;

    if (enable == TRUE)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :seesion Id: %d \n", session_id);
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");
    }

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mirror_show_vlan_info(uint8 lchip, uint16 vlan_id)
{
    uint16 drv_gport = 0;
    uint16 dst_gport = 0;
    uint16 lport = 0;
    uint32 nh_ptr = 0;
    uint32 excp_index = 0;
    uint32 cmd = 0;
    uint32 session_id = 0;
    uint32 enable = FALSE;
    uint32 value = 0;
    DsMetFifoExcp_m met_excp;
    DsBufRetrvExcp_m bufrev_exp;

    sal_memset(&met_excp, 0, sizeof(DsMetFifoExcp_m));
    sal_memset(&bufrev_exp, 0, sizeof(DsBufRetrvExcp_m));

    SYS_MIRROR_INIT_CHECK(lchip);
    CTC_VLAN_RANGE_CHECK(vlan_id);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_ID, &session_id));
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INGRESS_VLAN_SPAN_EN, &enable));

    excp_index = SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
    drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
    dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

    lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(dst_gport);
    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
    GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);

    nh_ptr = value;

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Source Vlan=%d \n", vlan_id);
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    if (enable == TRUE)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror :seesion Id: %d \n", session_id);
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ingress Mirror is disable\n");
    }

    enable = FALSE;
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_ID, &session_id));
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_EGRESS_VLAN_SPAN_EN, &enable));

    excp_index = SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE + session_id;
    cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
    GetDsMetFifoExcp(A, destMap_f, &met_excp, &value);
    drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(value);
    dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

    cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index + (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * SYS_CHIP_PER_SLICE_PORT_NUM), cmd, &bufrev_exp));
    GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
    nh_ptr = value;

    if (enable == TRUE)
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror :seesion Id: %d \n", session_id);
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", dst_gport, nh_ptr);
    }
    else
    {
        SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Egress Mirror is disable\n");
    }

    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "===================================== \n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mirror_show_acl_info(uint8 lchip, uint16 log_id)
{
    uint8  priority_id = 0;
    uint16 drv_gport = 0;
    uint16 dst_gport = 0;
    uint32 nh_ptr = 0;
    uint32 excp_index = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 dest_map = 0;
    uint32 direction = 0;
    DsMetFifoExcp_m met_excp;
    DsBufRetrvExcp_m bufrev_exp;
    char* desc[10] = {"Ingress", "Egress"};
    uint32 acl_log_base[2] = {SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE, SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE};

    sal_memset(&met_excp, 0, sizeof(DsMetFifoExcp_m));
    sal_memset(&bufrev_exp, 0, sizeof(DsBufRetrvExcp_m));

    SYS_MIRROR_INIT_CHECK(lchip);

    if (log_id >= SYS_MIRROR_ACL_LOG_ID_NUM)
    {
        return CTC_E_MIRROR_INVALID_MIRROR_LOG_ID;
    }

    for (direction = 0; direction < 2; direction++)
    {
        if(direction == 0)
        {
            for (priority_id = 0; priority_id < SYS_MIRROR_ACL_LOG_PRIORITY_NUM; priority_id++)
            {
                excp_index = acl_log_base[direction] + (SYS_MIRROR_ACL_LOG_PRIORITY_NUM * priority_id) + log_id;
                cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
                GetDsMetFifoExcp(A, destMap_f, &met_excp, &dest_map);
                drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
                dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

                cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
                GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
                nh_ptr = value;

                SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "========================================= \n");
                SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s Mirror Acl Log Id: %d\n", desc[direction], log_id);

                if (SYS_RSV_PORT_DROP_ID != (dest_map & 0xFF))
                {
                    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Priority Id: %d DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", priority_id, dst_gport, nh_ptr);
                }
                else
                {
                    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s Mirror Priority Id: %d is disable\n", desc[direction], priority_id);
                }
            }
        }
        if(direction == 1)
        {
            priority_id = 0;
            excp_index = acl_log_base[direction] + priority_id + log_id;
            cmd = DRV_IOR(DsMetFifoExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &met_excp));
            GetDsMetFifoExcp(A, destMap_f, &met_excp, &dest_map);
            drv_gport = SYS_GOLDENGATE_DESTMAP_TO_DRV_GPORT(dest_map);
            dst_gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(drv_gport);

            cmd = DRV_IOR(DsBufRetrvExcp_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_index, cmd, &bufrev_exp));
            GetDsBufRetrvExcp(A, nextHopPtr_f, &bufrev_exp, &value);
            nh_ptr = value;

            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "========================================= \n");
            SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s Mirror Acl Log Id: %d\n", desc[direction], log_id);

            if (SYS_RSV_PORT_DROP_ID != (dest_map & 0xFF))
            {
                SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mirror Priority Id: %d DestGport: 0x%.4x  NexthopPtr: 0x%.4x\n", priority_id, dst_gport, nh_ptr);
            }
            else
            {
                SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%s Mirror Priority Id: %d is disable\n", desc[direction], priority_id);
                }
        }
    }
    SYS_MIRROR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "========================================= \n");

    return CTC_E_NONE;
}

int32
sys_goldengate_mirror_show_info(uint8 lchip, ctc_mirror_info_type_t type, uint32 value)
{
    SYS_MIRROR_INIT_CHECK(lchip);

    if (CTC_MIRROR_INFO_PORT == type)
    {
        SYS_MAP_GPORT_TO_LCHIP(value, lchip);
        CTC_ERROR_RETURN(_sys_goldengate_mirror_show_port_info(lchip, value));
    }
    else if (CTC_MIRROR_INFO_VLAN == type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mirror_show_vlan_info(lchip, value));
    }
    else if (CTC_MIRROR_INFO_ACL == type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mirror_show_acl_info(lchip, value));
    }
    else
    {
        return CTC_E_MIRROR_INVALID_MIRROR_INFO_TYPE;
    }

    return CTC_E_NONE;
}





#define ____warmboot_____
int32
sys_goldengate_mirror_wb_sync(uint8 lchip)
{
    uint8 loop1 = 0;
    uint8 loop2 = 0;
    uint8 loop3 = 0;
    uint16 max_entry_cnt = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_mirror_dest_t* p_wb_mirror_dest;

    /*sync up mirror dest*/
    wb_data.buffer = mem_malloc(MEM_MIRROR_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_mirror_dest_t, CTC_FEATURE_MIRROR, SYS_WB_APPID_MIRROR_SUBID_DEST);
    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data.key_len + wb_data.data_len);

    for (loop1 = 0; loop1 < MAX_CTC_MIRROR_SESSION_ID; loop1++)
    {
        for (loop2 = 0; loop2 < MAX_SYS_MIRROR_SESSION_TYPE; loop2++)
        {
            for (loop3 = 0; loop3 < CTC_BOTH_DIRECTION; loop3++)
            {
                p_wb_mirror_dest = (sys_wb_mirror_dest_t *)wb_data.buffer + wb_data.valid_cnt;
                p_wb_mirror_dest->session_id = loop1;
                p_wb_mirror_dest->type = loop2;
                p_wb_mirror_dest->dir = loop3;
                p_wb_mirror_dest->nh_id = p_gg_mirror_master[lchip]->mirror_nh[loop1][loop2][loop3];
                p_wb_mirror_dest->is_external = p_gg_mirror_master[lchip]->external_nh[loop1][loop2][loop3];
                if (++wb_data.valid_cnt == max_entry_cnt)
                {
                    CTC_ERROR_RETURN(ctc_wb_add_entry(&wb_data));
                    wb_data.valid_cnt = 0;
                }
            }
        }
    }

    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(&wb_data));
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_mirror_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_mirror_dest_t* p_wb_mirror_dest;
    uint16 entry_cnt = 0;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    /*restore mirror dest*/
    wb_query.buffer = mem_malloc(MEM_MIRROR_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_mirror_dest_t, CTC_FEATURE_MIRROR, SYS_WB_APPID_MIRROR_SUBID_DEST);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_mirror_dest = (sys_wb_mirror_dest_t*)wb_query.buffer + entry_cnt++;/*after reading entry from sqlite, wb_query.buffer is still the first address of the space*/
        p_gg_mirror_master[lchip]->mirror_nh[p_wb_mirror_dest->session_id][p_wb_mirror_dest->type][p_wb_mirror_dest->dir] = p_wb_mirror_dest->nh_id;
        p_gg_mirror_master[lchip]->external_nh[p_wb_mirror_dest->session_id][p_wb_mirror_dest->type][p_wb_mirror_dest->dir] = p_wb_mirror_dest->is_external;
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


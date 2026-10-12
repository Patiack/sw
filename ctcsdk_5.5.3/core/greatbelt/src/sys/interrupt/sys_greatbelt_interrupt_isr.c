/**
 @file sys_greatbelt_interrupt_isr.c

 @date 2012-11-01

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_interrupt.h"
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_interrupt_priv.h"
#include "greatbelt/include/drv_lib.h"

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

/* L2's ISR is in sys_greatbelt_l2_fdb.c */
extern int32
sys_greatbelt_learning_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_aging_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_hw_learning_aging_isr(uint8 lchip, uint32 intr, void* p_data);

/* OAM's ISR is in sys_greatbelt_oam.c */
extern int32
sys_greatbelt_oam_isr_defect(uint8 lchip, uint32 intr, void* p_data);

/* PTP's ISR is in sys_greatbelt_ptp.c */
extern int32
sys_greatbelt_ptp_isr_rx_ts_capture(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_ptp_isr_tx_ts_capture(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_ptp_isr_tod_pulse_in(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_ptp_isr_tod_code_rdy(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_ptp_isr_sync_pulse_in(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_ptp_isr_sync_code_rdy(uint8 lchip, uint32 intr, void* p_data);

/* DMA's ISR is in sys_greatbelt_dma.c */
extern int32
sys_greatbelt_dma_isr_func(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_dma_isr_normal(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_port_link_status_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_linkagg_failover_detec(uint8 lchip, uint8 linkdown_chan);

extern int32
sys_greatbelt_stats_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_greatbelt_port_link_status_from_pcs(uint8 lchip, void* p_data);

extern int32
sys_greatbelt_stacking_failover_detect(uint8 lchip, uint8 linkdown_chan);
/**
 @brief handle sup fatal interrupt
*/
int32
sys_greatbelt_intr_isr_sup_fatal(uint8 lchip, uint32 intr, void* p_data)
{
    uint32* p_sup_fatal = (uint32*)p_data;   /* gb_sup_interrupt_fatal_t */
    ctc_intr_type_t type;
    uint32 idx = 0;
    uint32 offset = 0;
    uint32 i = 0;
    uint32 ctc_intr = 0;
    uint8 gchip = 0;
    ctc_interrupt_abnormal_status_t abnormal_status;
    ctc_intr_status_t* p_status = &(abnormal_status.status);
    int32 ret = CTC_E_NONE;

    sal_memset(&abnormal_status, 0, sizeof(abnormal_status));

    CTC_ERROR_RETURN(sys_greatbelt_interrupt_type_unmapping(lchip, intr, &ctc_intr));

    type.intr = ctc_intr;

    abnormal_status.type.intr = ctc_intr;

    /* 2. get & clear sub interrupt status */
    for (i = 0; i < SYS_INTR_GB_SUB_FATAL_MAX; i++)
    {
        idx = i / CTC_UINT32_BITS;
        offset = i % CTC_UINT32_BITS;

        if (0 == p_sup_fatal[idx])
        {
            /* bypass 32Bit 0 */
            i = (idx + 1) * CTC_UINT32_BITS - 1;
            continue;
        }

        if (CTC_IS_BIT_SET(p_sup_fatal[idx], offset))
        {
            type.sub_intr = i;
            /* get sub interrupt status */
            sys_greatbelt_interrupt_get_status(lchip, &type, p_status);

            if (p_gb_intr_master[lchip]->abnormal_intr_cb)
            {
                ret = sys_greatbelt_get_gchip_id(lchip, &gchip);
                if (0 == ret)
                {
                    abnormal_status.type.sub_intr = i;
                    abnormal_status.action = CTC_INTERRUPT_FATAL_INTR_RESET;
                    p_gb_intr_master[lchip]->abnormal_intr_cb(gchip, &abnormal_status);
                }
            }

            /* clear sub interrupt status */
            sys_greatbelt_interrupt_clear_status(lchip, &type, FALSE);
        }
    }

    return CTC_E_NONE;
}

STATIC bool
_sys_greatbelt_intr_isr_is_func(uint8 lchip, uint32 idx, uint32 offset)
{
    if ((idx == 0) && ((offset >= SYS_INTR_GB_SUB_NORMAL_QUAD_PCS0) && (offset <= SYS_INTR_GB_SUB_NORMAL_QUAD_PCS5)))
    {
        return TRUE;
    }
    else if ((idx == 0) && ((offset >= SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII0) && (offset <= SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII11)))
    {
        return TRUE;
    }
    else if ((idx == 0) && ((offset >= SYS_INTR_GB_SUB_NORMAL_SGMAC0) && (offset <= SYS_INTR_GB_SUB_NORMAL_SGMAC11)))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

STATIC bool
_sys_greatbelt_intr_isr_is_ecc_error(uint8 lchip, uint32 idx, uint32 offset, uint32* p_bmp)
{
    sys_interrupt_type_gb_sub_normal_t sub_normal_intr = idx * CTC_UINT32_BITS + offset;

    if (((SYS_INTR_GB_SUB_NORMAL_BUF_STORE == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrCondDisProfId)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPortTcMinProfId)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPortTcPriMap)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPortTcThrdProfId)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPortTcThrdProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPortThrdProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IgrPriToTcMap)))
        || ((SYS_INTR_GB_SUB_NORMAL_DYNAMIC_DS == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram0)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram1)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram2)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram3)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram4)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram5)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram6)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram7)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Edram8)))
        || ((SYS_INTR_GB_SUB_NORMAL_EPE_HDR_EDIT == sub_normal_intr)
        && CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PacketHeaderEditTunnel))
        || ((SYS_INTR_GB_SUB_NORMAL_EPE_HDR_PROC == sub_normal_intr)
        && CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Ipv6NatPrefix))
        || ((SYS_INTR_GB_SUB_NORMAL_EPE_NEXT_HOP == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DestChannel)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DestInterface)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DestPort)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_EgressVlanRangeProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_EpeEditPriorityMap)))
        || ((SYS_INTR_GB_SUB_NORMAL_IPE_FORWARD == sub_normal_intr)
        && CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IpeFwdDsQcnRam))
        || ((SYS_INTR_GB_SUB_NORMAL_IPE_HDR_ADJ == sub_normal_intr)
        && CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PhyPort))
        || ((SYS_INTR_GB_SUB_NORMAL_IPE_INTF_MAP == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_MplsCtl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PhyPortExt)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_RouterMac)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_SrcInterface)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_SrcPort)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_VlanActionProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_VlanRangeProfile)))
        || ((SYS_INTR_GB_SUB_NORMAL_IPE_PKT_PROC == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_EcmpGroup)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Rpf)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_SrcChannel)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_IpeClassificationDscpMap)))
        || ((SYS_INTR_GB_SUB_NORMAL_MET_FIFO == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_ApsBridge)))
        || ((SYS_INTR_GB_SUB_NORMAL_NET_RX == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_ChannelizeIngFc)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_ChannelizeMode)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PauseTimerMemRd)))
        || ((SYS_INTR_GB_SUB_NORMAL_Q_MGR_ENQ == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_EgrResrcCtl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_HeadHashMod)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_LinkAggregateMember)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_LinkAggregateMemberSet)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_LinkAggregateSgmacMember)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_LinkAggregationPort)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueThrdProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueueHashKey)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueueNumGenCtl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueueSelectMap)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_SgmacHeadHashMod)))
        || ((SYS_INTR_GB_SUB_NORMAL_SHARED_DS == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_StpState)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_Vlan)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_VlanProfile)))
        || ((SYS_INTR_GB_SUB_NORMAL_TCAM_DS == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DsTcamAd0)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DsTcamAd1)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DsTcamAd2)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_DsTcamAd3)))
        || ((SYS_INTR_GB_SUB_NORMAL_POLICING == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PolicerControl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PolicerProfile0)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PolicerProfile1)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PolicerProfile2)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_PolicerProfile3)))
        || ((SYS_INTR_GB_SUB_NORMAL_IPE_FIB == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_lpmTcamAdMem)))
        || ((SYS_INTR_GB_SUB_NORMAL_Q_MGR_DEQ == sub_normal_intr)
        && (CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_ChanShpProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_GrpShpProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_GrpShpWfqCtl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueMap)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueShpCtl)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueShpProfile)
        || CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_QueShpWfqCtl)))
        || ((SYS_INTR_GB_SUB_NORMAL_BUF_RETRV == sub_normal_intr)
        && CTC_BMP_ISSET(p_bmp, DRV_ECC_INTR_BufRetrvExcp)))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

STATIC int32
_sys_greatbelt_intr_ecc_proc(uint8 lchip, ctc_interrupt_abnormal_status_t* p_status)
{
    tbls_id_t tblid = MaxTblId_t;
    drv_ecc_intr_param_t intr_param;

    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"### Parity Error Interrupt ###\n");
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Type          :   %u\n", p_status->type.intr);
    SYS_INTETRUPT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Sub Type      :   %u\n", p_status->type.sub_intr);

    switch (p_status->type.sub_intr)
    {
        case SYS_INTR_GB_SUB_NORMAL_BUF_STORE:
            tblid = BufStoreInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_DYNAMIC_DS:
            tblid = DynamicDsInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_EPE_HDR_EDIT:
            tblid = EpeHdrEditInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_EPE_HDR_PROC:
            tblid = EpeHdrProcInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_EPE_NEXT_HOP:
            tblid = EpeNextHopInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_IPE_FORWARD:
            tblid = IpeFwdInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_IPE_HDR_ADJ:
            tblid = IpeHdrAdjInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_IPE_INTF_MAP:
            tblid = IpeIntfMapInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_IPE_PKT_PROC:
            tblid = IpePktProcInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_MET_FIFO:
            tblid = MetFifoInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_NET_RX:
            tblid = NetRxInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_Q_MGR_ENQ:
            tblid = QMgrEnqInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_SHARED_DS:
            tblid = SharedDsInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_TCAM_DS:
            tblid = TcamDsInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_POLICING:
            tblid = PolicingInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_IPE_FIB:
            tblid = IpeFibInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_Q_MGR_DEQ:
            tblid = QMgrDeqInterruptNormal_t;
            break;

        case SYS_INTR_GB_SUB_NORMAL_BUF_RETRV:
            tblid = BufRetrvInterruptNormal_t;
            break;

        default:
            return CTC_E_NONE;
    }

    sal_memset(&intr_param, 0, sizeof(drv_ecc_intr_param_t));
    intr_param.p_bmp = p_status->status.bmp;
    intr_param.intr_tblid = tblid;
    intr_param.total_bit_num = CTC_INTR_STAT_SIZE * CTC_UINT32_BITS;
    intr_param.valid_bit_count = p_status->status.bit_count;

    CTC_ERROR_RETURN(drv_greatbelt_ecc_recover_restore(lchip, &intr_param));

    return CTC_E_NONE;
}

sys_intr_sub_normal_bmp_t sys_intr_sub_normal_bmp[] =
{
{SYS_INTR_GB_SUB_NORMAL_CPU_MAC,       0x0000001F},
{SYS_INTR_GB_SUB_NORMAL_EPE_ACL_QOS,   0x00000001},
{SYS_INTR_GB_SUB_NORMAL_EPE_NEXT_HOP,  0x00000200},
{SYS_INTR_GB_SUB_NORMAL_IPE_FORWARD,   0x00000010},
{SYS_INTR_GB_SUB_NORMAL_IPE_INTF_MAP,  0x00034080},
{SYS_INTR_GB_SUB_NORMAL_IPE_LKUP_MGR,  0x00000001},
{SYS_INTR_GB_SUB_NORMAL_IPE_PKT_PROC,  0x0000000F},
{SYS_INTR_GB_SUB_NORMAL_OAM_PROC,      0x00000008},
};


STATIC bool
_sys_greatbelt_intr_isr_need_reset(uint8 lchip, uint32 sub_intr, ctc_intr_status_t* p_status)
{
    uint32 idx = 0;
    uint32 count = 0;
    uint32 sub_intr_bmp = 0;

    count = COUNTOF(sys_intr_sub_normal_bmp);

    for (idx = 0; idx < count; idx++ )
    {
        if (sub_intr == (sys_intr_sub_normal_bmp[idx].sub_intr))
        {
            sub_intr_bmp = sys_intr_sub_normal_bmp[idx] .reset_intr_bmp;
            if(sub_intr_bmp & (p_status->bmp[0]))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}


/**
 @brief handle sup normal interrupt
*/
int32
sys_greatbelt_intr_isr_sup_normal(uint8 lchip, uint32 intr, void* p_data)
{
    uint32* p_sup_normal = (uint32*)p_data;   /* gb_sup_interrupt_normal_t */
    ctc_intr_type_t type;
    uint32 idx = 0;
    uint32 offset = 0;
    uint32 i = 0;
    uint32 ctc_intr = 0;
    uint8 gchip = 0;
    uint8 ecc_recover_en = sys_greatbelt_chip_get_ecc_recover_en(lchip);
    ctc_interrupt_abnormal_status_t abnormal_status;
    ctc_intr_status_t* p_status = &(abnormal_status.status);
    int32 ret = CTC_E_NONE;
    sal_memset(&abnormal_status, 0, sizeof(abnormal_status));

    CTC_ERROR_RETURN(sys_greatbelt_interrupt_type_unmapping(lchip, intr, &ctc_intr));

    type.intr = ctc_intr;

    /* 2. get & clear sub interrupt status */
    for (i = 0; i < SYS_INTR_GB_SUB_NORMAL_MAX; i++)
    {
        idx = i / CTC_UINT32_BITS;
        offset = i % CTC_UINT32_BITS;

        if (0 == p_sup_normal[idx])
        {
            /* bypass 32Bit 0 */
            i = (idx + 1) * CTC_UINT32_BITS - 1;
            continue;
        }

        if (CTC_IS_BIT_SET(p_sup_normal[idx], offset))
        {
            type.sub_intr = i;
            /* get sub interrupt status */
            sys_greatbelt_interrupt_get_status(lchip, &type, p_status);

            /* function interrupt for quadpcs linkup/linkdown */
            if (_sys_greatbelt_intr_isr_is_func(lchip, idx, offset))
            {
                /* clear sub interrupt status */
                sys_greatbelt_interrupt_clear_status(lchip, &type, FALSE);

                abnormal_status.type.intr = ctc_intr;
                abnormal_status.type.sub_intr = i;
                abnormal_status.action = CTC_INTERRUPT_FATAL_INTR_NULL;
                sys_greatbelt_port_link_status_from_pcs(lchip, &abnormal_status);
                continue;
            }

            /* function interrupt for ecc error recover */
            if (ecc_recover_en && _sys_greatbelt_intr_isr_is_ecc_error(lchip, idx, offset, abnormal_status.status.bmp))
            {
                abnormal_status.type.intr = ctc_intr;
                abnormal_status.type.sub_intr = i;
                abnormal_status.action = CTC_INTERRUPT_FATAL_INTR_NULL;
                _sys_greatbelt_intr_ecc_proc(lchip, &abnormal_status);

                /* clear sub interrupt status */
                sys_greatbelt_interrupt_clear_status(lchip, &type, TRUE);
                continue;
            }

            if (p_gb_intr_master[lchip]->abnormal_intr_cb)
            {
                ret = sys_greatbelt_get_gchip_id(lchip, &gchip);
                if (0 == ret)
                {
                    abnormal_status.type.intr = ctc_intr;
                    abnormal_status.type.sub_intr = i;
                    abnormal_status.action = CTC_INTERRUPT_FATAL_INTR_LOG;
                    if (TRUE == _sys_greatbelt_intr_isr_need_reset(lchip, i, p_status))
                    {
                        abnormal_status.action = CTC_INTERRUPT_FATAL_INTR_RESET;
                    }
                    p_gb_intr_master[lchip]->abnormal_intr_cb(gchip, &abnormal_status);
                }
            }

          /* clear sub interrupt status */
          sys_greatbelt_interrupt_clear_status(lchip, &type, FALSE);

        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_linkdown_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 cmd = 0;
    q_mgr_enq_scan_link_down_chan_record_t chan_down;
    uint8 index = 0;
    uint8 linkdown_chan = 0;
    ctc_intr_type_t type;
    epe_hdr_adjust_ctl_t     epe_hdr_adjust_ctl;

    type.intr = CTC_INTR_GB_FUNC_LINKAGG_FAILOVER;
    type.sub_intr = 0;

    /* mask linkdown interrupt */
    sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);

    sal_memset(&chan_down, 0, sizeof(q_mgr_enq_scan_link_down_chan_record_t));

    cmd = DRV_IOR(QMgrEnqScanLinkDownChanRecord_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));

    cmd = DRV_IOR(EpeHdrAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adjust_ctl));

    for (index = 0; index < 64; index++)
    {
        if (index < 32)
        {
            if ((chan_down.link_down_scan_chan_record31_to0 >>index) & 0x1)
            {
                linkdown_chan = index;
                if (CTC_IS_BIT_SET(epe_hdr_adjust_ctl.packet_header_en31_0, linkdown_chan))
                {
                    sys_greatbelt_stacking_failover_detect(lchip, linkdown_chan);
                }
                else
                {
                    sys_greatbelt_linkagg_failover_detec(lchip, linkdown_chan);
                }
                chan_down.link_down_scan_chan_record31_to0 |= (1<<linkdown_chan);
            }
        }
        else
        {
            if ((chan_down.link_down_scan_chan_record63_to32 >>(index-32)) & 0x1)
            {
                linkdown_chan = index;
                if (CTC_IS_BIT_SET(epe_hdr_adjust_ctl.packet_header_en63_32, (linkdown_chan-32)))
                {
                    sys_greatbelt_stacking_failover_detect(lchip, linkdown_chan);
                }
                else
                {
                    sys_greatbelt_linkagg_failover_detec(lchip, linkdown_chan);
                }
                chan_down.link_down_scan_chan_record63_to32 |= (1<<(linkdown_chan-32));
            }
        }

    }

    cmd = DRV_IOW(QMgrEnqScanLinkDownChanRecord_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));

    /* release linkdown interrupt */
    sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

    return CTC_E_NONE;
}


/* default interrupt configuration */
sys_intr_mapping_t sys_greatbelt_intr_default[] =
{
    {3,    SYS_INTR_GB_CHIP_FATAL,                 "Chip Fatal",          sys_greatbelt_intr_isr_sup_fatal},
    {3,    SYS_INTR_GB_CHIP_NORMAL,                "Chip Normal",         sys_greatbelt_intr_isr_sup_normal},
    {INVG, SYS_INTR_GB_FUNC_PTP_TS_CAPTURE,        "PTP RX Ts capture",   sys_greatbelt_ptp_isr_rx_ts_capture},
    {0,    SYS_INTR_GB_FUNC_PTP_MAC_TX_TS_CAPTURE, "PTP TX Ts capture",   sys_greatbelt_ptp_isr_tx_ts_capture},
    {0,    SYS_INTR_GB_FUNC_PTP_TOD_PULSE_IN,      "PTP TOD Pulse In",    sys_greatbelt_ptp_isr_tod_pulse_in},
    {0,    SYS_INTR_GB_FUNC_PTP_TOD_CODE_IN_RDY,   "PTP TOD Code Ready",  sys_greatbelt_ptp_isr_tod_code_rdy},
    {INVG, SYS_INTR_GB_FUNC_PTP_SYNC_PULSE_IN,     "PTP Sync Pulse In",   sys_greatbelt_ptp_isr_sync_pulse_in},
    {INVG, SYS_INTR_GB_FUNC_PTP_SYNC_CODE_IN_RDY,  "PTP Sync Code Ready", sys_greatbelt_ptp_isr_sync_code_rdy},
    {INVG, SYS_INTR_GB_FUNC_OAM_TX_PKT_STATS,      "OAM TX Pkt Stats",    NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_TX_OCTET_STATS,    "OAM TX Octet Stats",  NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_RX_PKT_STATS,      "OAM RX Pkt Stats",    NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_RX_OCTET_STATS,    "OAM RX Octet Stats",  NULL},
    {1,    SYS_INTR_GB_FUNC_OAM_DEFECT_CACHE,      "OAM Defect",          sys_greatbelt_oam_isr_defect},
    {INVG, SYS_INTR_GB_FUNC_OAM_CLEAR_EN_3,        "OAM Clear En 3",      NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_CLEAR_EN_2,        "OAM Clear En 2",      NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_CLEAR_EN_1,        "OAM Clear En 1",      NULL},
    {INVG, SYS_INTR_GB_FUNC_OAM_CLEAR_EN_0,        "OAM Clear En 0",      NULL},
    {0,    SYS_INTR_GB_FUNC_MDIO_XG_CHANGE,        "Link State Change (XG)",  sys_greatbelt_port_link_status_isr},
    {0,    SYS_INTR_GB_FUNC_MDIO_1G_CHANGE,        "Link State Change (1G)",  sys_greatbelt_port_link_status_isr},
    {0,    SYS_INTR_GB_FUNC_LINKAGG_FAILOVER,      "Linkagg Failover",    sys_greatbelt_port_linkdown_isr},
    {0,    SYS_INTR_GB_FUNC_IPE_LEARN_CACHE,       "SW Learning",         sys_greatbelt_learning_isr},
    {0,    SYS_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO,    "HW Learning-Aging",   sys_greatbelt_hw_learning_aging_isr},
    {0,    SYS_INTR_GB_FUNC_IPE_AGING_FIFO,        "SW Aging",            sys_greatbelt_aging_isr},
    {0,    SYS_INTR_GB_FUNC_STATS_FIFO,            "Stats",               sys_greatbelt_stats_isr},
    {INVG, SYS_INTR_GB_FUNC_APS_FAILOVER,          "HW Based APS Failover",  NULL},
    {2,    SYS_INTR_GB_DMA_FUNC,                   "DMA Function",        sys_greatbelt_dma_isr_func},
    {3,    SYS_INTR_GB_DMA_NORMAL,                 "DMA Normal",          sys_greatbelt_dma_isr_normal},
    {INVG, SYS_INTR_GB_PCIE_SECOND,                "PCIe Secondary",      NULL},
    {INVG, SYS_INTR_GB_PCIE_PRIMARY,               "PCIe Primary",        NULL}
};



/* default interrupt group configuration, need to change the IRQ and priority of groups based on your requirement
 * the priority range is [1, 139] for linux, thereinto [1, 99] is realtime schedule; and [0, 255] for vxworks
 */
ctc_intr_group_t sys_greatbelt_intr_group_default[] =
{
    {0, 16, SAL_TASK_PRIO_DEF,  "Function normal group"},
    {1, 17, 100,                "Function critical group"},
    {2, 18, SAL_TASK_PRIO_DEF,  "Function DMA group"},
    {3, 19, SAL_TASK_PRIO_DEF,  "Chip abnormal group"}
};

/**
 @brief get default interrupt global configuration
*/
int32
sys_greatbelt_interrupt_get_default_global_cfg(uint8 lchip, sys_intr_global_cfg_t* p_intr_cfg)
{
    uint32 group_count = 0;
    uint32 intr_count = 0;

    /* use default global configuration in sys */
    group_count = COUNTOF(sys_greatbelt_intr_group_default);
    intr_count = COUNTOF(sys_greatbelt_intr_default);
    if ((group_count > SYS_GB_MAX_INTR_GROUP) || (group_count > CTC_INTR_GB_MAX_GROUP))
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((intr_count > CTC_INTR_MAX_INTR) || (intr_count > CTC_INTR_GB_MAX_INTR))
    {
        return CTC_E_INVALID_PARAM;
    }

    p_intr_cfg->group_count = group_count;
    p_intr_cfg->intr_count = intr_count;
    sal_memcpy(&(p_intr_cfg->group), sys_greatbelt_intr_group_default, sizeof(sys_greatbelt_intr_group_default));
    sal_memcpy(&(p_intr_cfg->intr), sys_greatbelt_intr_default, sizeof(sys_greatbelt_intr_default));

    return CTC_E_NONE;
}


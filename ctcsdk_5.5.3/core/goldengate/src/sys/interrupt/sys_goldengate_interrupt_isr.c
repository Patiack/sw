/**
 @file sys_goldengate_interrupt_isr.c

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
#include "ctc_goldengate_interrupt.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_interrupt_priv.h"
#include "goldengate/include/drv_lib.h"

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

/* L2's ISR is in sys_goldengate_l2_fdb.c */
extern int32
sys_goldengate_learning_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_aging_isr(uint8 lchip, uint32 intr, void* p_data);

/* OAM's ISR is in sys_goldengate_oam.c */
extern int32
sys_goldengate_oam_isr_defect(uint8 lchip, uint32 intr, void* p_data);

/* PTP's ISR is in sys_goldengate_ptp.c */
extern int32
sys_goldengate_ptp_isr_rx_ts_capture(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_ptp_isr_tx_ts_capture(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_ptp_isr_tod_pulse_in(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_ptp_isr_tod_code_rdy(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_ptp_isr_sync_pulse_in(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_ptp_isr_sync_code_rdy(uint8 lchip, uint32 intr, void* p_data);

/* DMA's ISR is in sys_goldengate_dma.c */
extern int32
sys_goldengate_dma_isr_func(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_port_isr_link_status_change_isr(uint8 lchip, void* p_data);

extern int32
sys_goldengate_port_isr_cl73_complete_isr(uint8 lchip, void* p_data, void* p_data1);

extern int32
sys_goldengate_stats_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_stats_log_isr(uint8 lchip, uint32 intr, void* p_data);

extern int32
_sys_goldengate_interrupt_clear_status_entry(uint8 lchip, ctc_intr_type_t* p_type, uint32* p_bmp);

extern int32
sys_goldengate_ipfix_entry_usage_overflow(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_interrupt_get_info(uint8 lchip, ctc_intr_type_t* p_ctc_type, uint32 intr_bit, tbls_id_t* p_intr_tbl, uint8* p_action_type, uint8* p_ecc_or_parity);

extern int32
sys_goldengate_interrupt_proc_ecc_or_parity(uint8 lchip, tbls_id_t intr_tbl, uint32 intr_bit);

extern int32
sys_goldengate_aps_failover_detect(uint8 lchip, uint32 intr, void* p_data);

extern int32
sys_goldengate_linkagg_failover_detect(uint8 lchip, uint8 linkdown_chan);

extern int32
sys_goldengate_stacking_failover_detect(uint8 lchip, uint8 linkdown_chan);
/**
 @brief handle sup fatal interrupt
*/
int32
sys_goldengate_intr_isr_sup_fatal(uint8 lchip, uint32 intr, void* p_data)
{
    uint32* p_sup_fatal = (uint32*)p_data;   /* gb_sup_interrupt_fatal_t */
    ctc_intr_type_t type;
    uint32 idx = 0;
    uint32 offset = 0;
    uint32 i = 0, j = 0;
    uint32 bmp[CTC_INTR_STAT_SIZE] = {0};
    uint8  gchip = 0, action_type = 0;
    ctc_interrupt_abnormal_status_t abnormal_status;
    ctc_intr_status_t* p_status = &(abnormal_status.status);
    int32 ret = CTC_E_NONE;
    uint8 ecc_or_parity = 0;
    uint8 ecc_recover_en = drv_goldengate_ecc_recover_get_enable();
    tbls_id_t intr_tbl = MaxTblId_t;

    sal_memset(&abnormal_status, 0, sizeof(abnormal_status));
    type.intr = CTC_INTR_CHIP_FATAL;

    abnormal_status.type.intr = CTC_INTR_CHIP_FATAL;

    /* 2. get & clear sub interrupt status */
    for (i = 0; i < SYS_INTR_GG_SUB_FATAL_MAX; i++)
    {
        idx = i / CTC_UINT32_BITS;
        offset = i % CTC_UINT32_BITS;

        if (0 == p_sup_fatal[idx])
        {
            /* bypass 32Bit 0 */
            i = (idx + 1) * CTC_UINT32_BITS - 1;
            continue;
        }

        if ((idx >= SYS_INTR_GG_SUB_FATAL_REQ_PCIE_0_FIFO_OVERRUN) &&
            (idx <= SYS_INTR_GG_SUB_FATAL_REQ_I2C_FIFO_OVERRUN))
        {
            continue;
        }

        if (CTC_IS_BIT_SET(p_sup_fatal[idx], offset))
        {
            type.sub_intr = i;
            type.low_intr = 0;

            /* get sub interrupt status */
            sys_goldengate_interrupt_get_status(lchip, &type, p_status);

            if (!p_status->bmp[0] && !p_status->bmp[1] && !p_status->bmp[2])
            {
                _sys_goldengate_interrupt_clear_status_entry(lchip, &type, p_status->bmp);
            }
            else
            {
                for (j = 0; j < CTC_INTR_STAT_SIZE * CTC_UINT32_BITS; j++)
                {
                    if (CTC_BMP_ISSET(p_status->bmp, j))
                    {
                        sal_memset(bmp, 0, sizeof(bmp));
                        CTC_BMP_SET(bmp, j);

                        sys_goldengate_interrupt_get_info(lchip, &type, j, &intr_tbl, &action_type, &ecc_or_parity);

                        if (ecc_recover_en && ecc_or_parity)
                        {
                            /* function interrupt for process ecc or parity */
                            sys_goldengate_interrupt_proc_ecc_or_parity(lchip, intr_tbl, j);
                        }
                        else if (p_gg_intr_master[lchip]->abnormal_intr_cb)
                        {
                            ret = sys_goldengate_get_gchip_id(lchip, &gchip);
                            if (0 == ret)
                            {
                                abnormal_status.type.intr = CTC_INTR_CHIP_FATAL;
                                abnormal_status.type.sub_intr = i;
                                abnormal_status.type.low_intr = p_status->low_intr;
                                abnormal_status.action = action_type;
                                p_gg_intr_master[lchip]->abnormal_intr_cb(gchip, &abnormal_status);
                            }
                        }

                        /* clear sub interrupt status */
                        _sys_goldengate_interrupt_clear_status_entry(lchip, &type, bmp);
                    }
                }
            }
        }
    }

    return CTC_E_NONE;
}

/**
 @brief handle sup normal interrupt
*/
int32
sys_goldengate_intr_isr_sup_normal(uint8 lchip, uint32 intr, void* p_data)
{
    uint32* p_sup_normal = (uint32*)p_data;   /* gb_sup_interrupt_normal_t */
    ctc_intr_type_t type;
    uint32 idx = 0;
    uint32 offset = 0;
    uint32 i = 0;
    uint32 j = 0;
    uint8 gchip = 0, action_type = 0;
    ctc_interrupt_abnormal_status_t abnormal_status;
    ctc_intr_status_t* p_status = &(abnormal_status.status);
    int32 ret = CTC_E_NONE;
    uint32  bmp[CTC_INTR_STAT_SIZE] = {0};
    uint8 ecc_or_parity = 0;
    uint8 ecc_recover_en = drv_goldengate_ecc_recover_get_enable();
    tbls_id_t intr_tbl = MaxTblId_t;
     /**/
    uint32 tbl_id_low = 0;
    uint32 cmd = 0;
    uint8  serdes_id = 0;
    uint8  cl73_ok = 0;
    HssAnethWrapperInterruptNormal0_m auto_neg_intr;

    sal_memset(&abnormal_status, 0, sizeof(abnormal_status));

    type.intr = CTC_INTR_CHIP_NORMAL;

    /* 2. get & clear sub interrupt status */
    for (i = 0; i < SYS_INTR_GG_SUB_NORMAL_MAX; i++)
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
            type.low_intr = 0;

            /* get sub interrupt status */
            sys_goldengate_interrupt_get_status(lchip, &type, p_status);

            if (!p_status->bmp[0] && !p_status->bmp[1] && !p_status->bmp[2])
            {
                /*avoid low interrupt clear unexpect, interrupt cannot clear*/
                _sys_goldengate_interrupt_clear_status_entry(lchip, &type, p_status->bmp);
            }
            else if ((SYS_INTR_GG_SUB_NORMAL_RLM_CS_0 == type.sub_intr) || (SYS_INTR_GG_SUB_NORMAL_RLM_CS_1 == type.sub_intr)
             ||(SYS_INTR_GG_SUB_NORMAL_RLM_HS_0 == type.sub_intr) || (SYS_INTR_GG_SUB_NORMAL_RLM_HS_1 == type.sub_intr))
            {
                tbl_id_low = sys_goldengate_interrupt_get_tbl_id(lchip, &type);

                cmd = DRV_IOR(tbl_id_low, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_SET, cmd, &auto_neg_intr));

                if ((tbl_id_low >= HssAnethWrapperInterruptNormal0_t) && (tbl_id_low <= HssAnethWrapperInterruptNormal95_t))
                {
                    serdes_id = tbl_id_low - HssAnethWrapperInterruptNormal0_t;

                    /* get cl73 status */
                    sal_memset(bmp, 0, sizeof(bmp));
                    if(GetHssAnethWrapperInterruptNormal0(V, anLinkGoodIntr_f, &auto_neg_intr))
                    {
                        cl73_ok = 1;
                        CTC_BMP_SET(bmp, 0); /* bit0 */
                    }
                    if(GetHssAnethWrapperInterruptNormal0(V, anIncompatibleLinkIntr_f, &auto_neg_intr))
                    {
                        cl73_ok = 0;
                        CTC_BMP_SET(bmp, 4); /* bit4 */
                    }
                    sys_goldengate_port_isr_cl73_complete_isr(lchip, &serdes_id, &cl73_ok);
                }
                else
                {
                    bmp[0] = 0x3f;
                }

                /* clear sub interrupt status */
                _sys_goldengate_interrupt_clear_status_entry(lchip, &type, bmp);

            }
            else
            {
                for (j = 0; j < CTC_INTR_STAT_SIZE * CTC_UINT32_BITS; j++)
                {
                    if (CTC_BMP_ISSET(p_status->bmp, j))
                    {
                        sal_memset(bmp, 0, sizeof(bmp));
                        CTC_BMP_SET(bmp, j);

                        sys_goldengate_interrupt_get_info(lchip, &type, j, &intr_tbl, &action_type, &ecc_or_parity);

                        if (ecc_recover_en && ecc_or_parity)
                        {
                            /* function interrupt for process ecc or parity */
                            sys_goldengate_interrupt_proc_ecc_or_parity(lchip, intr_tbl, j);
                        }
                        else if (p_gg_intr_master[lchip]->abnormal_intr_cb)
                        {
                            ret = sys_goldengate_get_gchip_id(lchip, &gchip);
                            if (0 == ret)
                            {
                                abnormal_status.type.intr = CTC_INTR_CHIP_NORMAL;
                                abnormal_status.type.sub_intr = i;
                                abnormal_status.type.low_intr = p_status->low_intr;
                                abnormal_status.action = action_type;
                                p_gg_intr_master[lchip]->abnormal_intr_cb(gchip, &abnormal_status);
                            }
                        }

                        /* clear sub interrupt status */
                        _sys_goldengate_interrupt_clear_status_entry(lchip, &type, bmp);
                    }
                }
            }

        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_intr_pcs_link_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 link_status = 0;
    uint16 fst_mac = 0;
    uint8 index = 0;
    uint16 mac_id = 0;
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 gchip_id = 0;

    link_status = *((uint32*)p_data);

    switch (intr)
    {
        case SYS_INTR_GG_PCS_LINK_31_0:
            fst_mac = 0;
            break;

        case SYS_INTR_GG_PCS_LINK_47_32:
            fst_mac = 32;
            break;

        case SYS_INTR_GG_PCS_LINK_79_48:
            fst_mac = 64;
            break;

        case SYS_INTR_GG_PCS_LINK_95_80:
            fst_mac = 96;
            break;
        default:
            break;
    }

    for (index = 0; index < 32; index++)
    {
        if ((link_status >> index) & 0x01)
        {
            if (fst_mac == 32)
            {
                if (index < 8)
                {
                    mac_id = fst_mac + index;
                }
                else if (index < 16)
                {
                     mac_id = 48 + (index - 8);
                }
                else
                {
                    mac_id = (index == 16)?36:52;
                }
            }
            else if (fst_mac == 96)
            {
                if (index < 8)
                {
                    mac_id = fst_mac + index;
                }
                else if (index < 16)
                {
                     mac_id = 64 + 48 + (index - 8);
                }
                else
                {
                    mac_id = ((index == 16)?36:52)+64;
                }
            }
            else
            {
                mac_id = fst_mac + index;
            }

            /*get gport from datapath*/
            lport = sys_goldengate_common_get_lport_with_mac(lchip, mac_id);
            if (lport == SYS_COMMON_USELESS_MAC)
            {
                continue;
            }

            lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);
            sys_goldengate_get_gchip_id(lchip, &gchip_id);
            gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

            sys_goldengate_port_isr_link_status_change_isr(lchip, &gport);
        }
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_intr_failover_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 cmd = 0;
    QMgrEnqScanLinkDownChanRecord_m chan_down;
    uint16 index = 0;
    uint8 linkdown_chan = 0;
    sys_intr_type_t type;
    uint32 link_state[4] = {0};
    uint8 slice_id    = 0;
    EpeHdrAdjustChanCtl_m    epe_hdr_adjust_chan_ctl;
    uint32 packet_header_en[2];

    type.intr = SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN;
    type.sub_intr = 0;

    /* mask linkdown interrupt */
    sys_goldengate_interrupt_set_en_internal(lchip, &type, FALSE);

    sal_memset(&chan_down, 0, sizeof(chan_down));

    cmd = DRV_IOR(QMgrEnqScanLinkDownChanRecord_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));
    GetQMgrEnqScanLinkDownChanRecord(A, linkDownScanChanRecord_f, &chan_down, &(link_state[0]));


    for (index = 0; index < 128; index++)
    {
        if ((link_state[index/32] >> (index % 32)) & 0x1)
        {
            linkdown_chan = index;
            slice_id = SYS_MAP_CHANID_TO_SLICE(linkdown_chan);
            cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &epe_hdr_adjust_chan_ctl));
            GetEpeHdrAdjustChanCtl(A, packetHeaderEn_f, &epe_hdr_adjust_chan_ctl, packet_header_en);

            if (CTC_IS_BIT_SET(packet_header_en[(linkdown_chan-slice_id*64)>>5], linkdown_chan&0x1F))
            {
                /*stacking trunk*/
                sys_goldengate_stacking_failover_detect(lchip, linkdown_chan);
            }
            else
            {
                /*normal trunk*/
                sys_goldengate_linkagg_failover_detect(lchip, linkdown_chan);
            }
        }
    }

    cmd = DRV_IOW(QMgrEnqScanLinkDownChanRecord_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &chan_down));

    /* release linkdown interrupt */
    sys_goldengate_interrupt_set_en_internal(lchip, &type, TRUE);

    return CTC_E_NONE;
}

/* default interrupt configuration */
sys_intr_mapping_t sys_goldengate_intr_default[] =
{
    {0,    SYS_INTR_GG_CHIP_FATAL,                  "Chip Fatal",           sys_goldengate_intr_isr_sup_fatal},
    {0,    SYS_INTR_GG_CHIP_NORMAL,                 "Chip Normal",          sys_goldengate_intr_isr_sup_normal},
    {0,    SYS_INTR_GG_PCS_LINK_31_0,               "MAC PCS Link",         sys_goldengate_intr_pcs_link_isr},
    {0,    SYS_INTR_GG_PCS_LINK_47_32,              "MAC PCS Link",         sys_goldengate_intr_pcs_link_isr},
    {0,    SYS_INTR_GG_PCS_LINK_79_48,              "MAC PCS Link",         sys_goldengate_intr_pcs_link_isr},
    {0,    SYS_INTR_GG_PCS_LINK_95_80,              "MAC PCS Link",         sys_goldengate_intr_pcs_link_isr},
    {0,    SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_0,      "SW Learning 0",        sys_goldengate_learning_isr},
    {0,    SYS_INTR_GG_FUNC_IPE_LEARN_CACHE_1,      "SW Learning 1",        sys_goldengate_learning_isr},
    {0,    SYS_INTR_GG_FUNC_CHAN_LINKDOWN_SCAN,     "Chan Link Scan",       _sys_goldengate_intr_failover_isr},
    {0,    SYS_INTR_GG_FUNC_MDIO_CHANGE_0,          "MDIO Change 0",        NULL},
    {0,    SYS_INTR_GG_FUNC_MDIO_CHANGE_1,          "MDIO Change 1",        NULL},
    {0,    SYS_INTR_GG_FUNC_IPFIX_USEAGE_OVERFLOW,  "IPfix Useage",         sys_goldengate_ipfix_entry_usage_overflow},
    {0,    SYS_INTR_GG_FUNC_AGING_FIFO,             "SW Aging",             sys_goldengate_aging_isr},
    {0,    SYS_INTR_GG_FUNC_STATS_FIFO,             "Statistics",           sys_goldengate_stats_isr},
    {0,    SYS_INTR_GG_FUNC_PORT_LOG_STATS_FIFO,    "Port Log Statistics",  sys_goldengate_stats_log_isr},
    {0,    SYS_INTR_GG_FUNC_PTP_TOD_CODE_IN_RDY,    "PTP TOD Code Ready",   sys_goldengate_ptp_isr_tod_code_rdy},
    {0,    SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_RDY,   "PTP Sync Code Ready",  sys_goldengate_ptp_isr_sync_code_rdy},
    {0,    SYS_INTR_GG_FUNC_PTP_SYNC_CODE_IN_ACC,   "PTP Sync Code ACC",    NULL},
    {0,    SYS_INTR_GG_FUNC_PTP_MAC_TX_TS_CAPTURE,  "PTP MAC TX Ts capture",sys_goldengate_ptp_isr_tx_ts_capture},
    {0,    SYS_INTR_GG_FUNC_PTP_TX_TS_CAPTURE,      "PTP TX Ts capture",    NULL},
    {0,    SYS_INTR_GG_FUNC_MET_LINK_SCAN_DONE,     "HW Based APS Failover",sys_goldengate_aps_failover_detect},
    {INVG, SYS_INTR_GG_FUNC_OAM_CLEAR_EN,           "OAM Clear En",         NULL},
    {0,    SYS_INTR_GG_FUNC_OAM_DEFECT_CACHE,       "OAM Defect",           sys_goldengate_oam_isr_defect},
    {INVG, SYS_INTR_GG_FUNC_OAM_STATS,              "OAM Statistics",       NULL},
    {0,    SYS_INTR_GG_FUNC_BSR_CONGESTION_1,       "BSR Congestion 1",     NULL},
    {0,    SYS_INTR_GG_FUNC_BSR_CONGESTION_0,       "BSR Congestion 0",     NULL},
    {INVG,    SYS_INTR_GG_FUNC_RESERVED1,           "BSR Congestion 1",     NULL},
    {INVG,    SYS_INTR_GG_FUNC_RESERVED2,           "BSR Congestion 0",     NULL},
    {0,    SYS_INTR_GG_PCIE_BURST_DONE,             "PCIe Burst Done",      NULL},
    {0,    SYS_INTR_GG_DMA_0,                       "DMA 0",                sys_goldengate_dma_isr_func},
    {0,    SYS_INTR_GG_DMA_1,                       "DMA 1",                sys_goldengate_dma_isr_func}
};

/* default interrupt group configuration, need to change the IRQ and priority of groups based on your requirement
 * the priority range is [1, 139] for linux, thereinto [1, 99] is realtime schedule; and [0, 255] for vxworks
 */
ctc_intr_group_t sys_goldengate_intr_group_default[] =
{
 /*-    {0, 16, SAL_TASK_PRIO_DEF,  "Function normal group"}    //for pin*/
    {0, 0, SAL_TASK_PRIO_DEF,  "Function normal group"}    /*for msi*/
};

/**
 @brief get default interrupt global configuration
*/
int32
sys_goldengate_interrupt_get_default_global_cfg(uint8 lchip, sys_intr_global_cfg_t* p_intr_cfg)
{
    uint32 group_count = 0;
    uint32 intr_count = 0;

    /* use default global configuration in sys */
    group_count = COUNTOF(sys_goldengate_intr_group_default);
    intr_count = COUNTOF(sys_goldengate_intr_default);
    if (group_count > SYS_GG_MAX_INTR_GROUP)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (intr_count > CTC_INTR_MAX_INTR)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_intr_cfg->group_count = group_count;
    p_intr_cfg->intr_count = intr_count;
    sal_memcpy(&(p_intr_cfg->group), sys_goldengate_intr_group_default, sizeof(sys_goldengate_intr_group_default));
    sal_memcpy(&(p_intr_cfg->intr), sys_goldengate_intr_default, sizeof(sys_goldengate_intr_default));
    p_intr_cfg->intr_mode  = 0;

    return CTC_E_NONE;
}


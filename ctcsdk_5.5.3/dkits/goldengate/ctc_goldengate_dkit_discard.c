#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_discard.h"
#include "ctc_goldengate_dkit_discard_type.h"
#include "ctc_goldengate_dkit_bus_info.h"
#include "ctc_goldengate_dkit_captured_info.h"



#define ADD_DISCARD_REASON(p_discard_info,id,discard_module,port_num,slice_id, cnt)    \
            if (p_discard_info->discard_count < CTC_DKIT_DISCARD_REASON_NUM)\
            {\
                uint32 disacrd_bitmap = *(((uint32*)g_gg_dkit_master[lchip]->discard_enable_bitmap) + (id/32));\
                if(DKITS_IS_BIT_SET(disacrd_bitmap, id%32)) {\
                p_discard_info->discard_reason[p_discard_info->discard_count].reason_id = id;\
                p_discard_info->discard_reason[p_discard_info->discard_count].module = discard_module;\
                p_discard_info->discard_reason[p_discard_info->discard_count].port = port_num;\
                p_discard_info->discard_reason[p_discard_info->discard_count].slice = slice_id;\
                p_discard_info->discard_reason[p_discard_info->discard_count].stats = cnt;\
                p_discard_info->slice_ipe = slice_id;\
                p_discard_info->slice_epe = slice_id;\
                p_discard_info->discard_count++;}\
            }\

extern ctc_dkit_master_t* g_gg_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM];



STATIC int32
_ctc_goldengate_dkit_discard_display_reason(ctc_dkit_discard_reason_t* discard_reason, sal_file_t p_wf)
{
    DKITS_PTR_VALID_CHECK(discard_reason);

    switch (discard_reason->module)
    {
        case CTC_DKIT_DISCARD_FLAG_IPE:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at IPE on slice %d, reason id %d, discard id %d, stats %d: %s\n", discard_reason->slice,
                               discard_reason->reason_id, discard_reason->reason_id - CTC_DKIT_IPE_USERID_BINDING_DIS,
                               discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_EPE:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at EPE on slice %d, reason id %d, discard id %d, stats %d: %s\n", discard_reason->slice,
                    discard_reason->reason_id, discard_reason->reason_id - CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS,
                            discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_BSR:
            {
                if (discard_reason->port != CTC_DKIT_INVALID_PORT)
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at BSR on channel %d, reason id %d, stats %d: %s\n", discard_reason->port,
                        discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
                }
                else
                {
                    CTC_DKIT_PRINT("Packet discard at BSR on slice %d, reason id %d, stats %d: %s\n", discard_reason->slice,
                        discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
                }
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_OAM:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at OAM engine on slice %d, reason id %d, stats %d: %s\n", discard_reason->slice,
                    discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_NET_RX:
            {
                if (discard_reason->port != CTC_DKIT_INVALID_PORT)
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETRX on channel %d, reason id %d, stats %d: %s\n", discard_reason->port,
                        discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
                }
                else
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETRX on slice %d, reason id %d, stats %d: %s\n", discard_reason->slice,
                        discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
                }
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_NET_TX:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETTX on slice %d, reason id %d, stats %d: %s\n", discard_reason->slice,
                     discard_reason->reason_id, discard_reason->stats, ctc_goldengate_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        default:
            break;
    }
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_discard_display_bus_reason(ctc_dkit_discard_info_t* p_discard_info, sal_file_t p_wf)
{
    DKITS_PTR_VALID_CHECK(p_discard_info);
    if ((p_discard_info->discard_reason_bus.gport != CTC_DKIT_INVALID_PORT)&&(1 == p_discard_info->discard_count))
    {
         /*CTC_DKIT_PRINT("\n## DETAIL INFO: discard on port 0x%04x\n", p_discard_info->discard_reason_bus.gport);*/
        CTC_DKITS_PRINT_FILE(p_wf, "\n## DETAIL INFO:\n");
    }
    else if (1 == p_discard_info->discard_count)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n## DETAIL INFO:\n");
    }
    else
    {
        return CLI_SUCCESS;
    }


    if (p_discard_info->discard_reason_bus.reason_id)
    {
        if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_NONE)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_goldengate_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id));
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_1)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_goldengate_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
                                        p_discard_info->discard_reason_bus.value[0]);
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_2)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d and %d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_goldengate_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
                                        p_discard_info->discard_reason_bus.value[0],
                                        p_discard_info->discard_reason_bus.value[1]);
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_3)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d, %d and %d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_goldengate_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
                                        p_discard_info->discard_reason_bus.value[0],
                                        p_discard_info->discard_reason_bus.value[1],
                                        p_discard_info->discard_reason_bus.value[2]);
        }
        else
        {
            CTC_DKITS_PRINT_FILE(p_wf, "   N/A");
        }
    }
    else
    {
        CTC_DKITS_PRINT_FILE(p_wf, "   N/A");
    }

    return CLI_SUCCESS;
}
STATIC int32
_ctc_goldengate_dkit_discard_display(ctc_dkit_discard_info_t* p_discard_info, sal_file_t p_wf)
{
    char yes_no[2][4]={" -", "YES"};
    uint8 i = 0;

    DKITS_PTR_VALID_CHECK(p_discard_info);

    /*1. display total*/
    CTC_DKITS_PRINT_FILE(p_wf, "\n");
    CTC_DKITS_PRINT_FILE(p_wf, "TIME: %s", p_discard_info->systime_str);
    CTC_DKITS_PRINT_FILE(p_wf, "============================================================\n");
    CTC_DKITS_PRINT_FILE(p_wf, " %-10s %-10s %-10s %-10s %-10s %-10s\n", "NETRX", "IPE",  "BSR", "EPE", "NETTX", "OAM");
    CTC_DKITS_PRINT_FILE(p_wf, "------------------------------------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, " %-10s %-10s %-10s %-10s %-10s %-10s\n",
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX)],
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_IPE)],
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR)],
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_EPE)],
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX)],
                yes_no[DKITS_FLAG_ISSET(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_OAM)]);
    CTC_DKITS_PRINT_FILE(p_wf, "============================================================\n");

    /*2. display by reason id*/
    if (p_discard_info->discard_count > 0)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n## PRIMARY:\n");
        for (i = 0; i < p_discard_info->discard_count; i++)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "%d. ", i + 1);
            _ctc_goldengate_dkit_discard_display_reason(&p_discard_info->discard_reason[i], p_wf);
        }
    }

    /*3. display reason from bus info*/
   if (p_discard_info->discard_reason_bus.valid)
   {
       _ctc_goldengate_dkit_discard_display_bus_reason(p_discard_info, p_wf);
   }

    /*4. display reason from captured info*/
     /*if (p_discard_info->discard_reason_captured.reason_id)*/
     /*{*/
     /*    CTC_DKIT_PRINT("\n## FROM CAPTURED INFO:\n");*/
     /*    CTC_DKIT_PRINT("1. Reason id %d: %s\n", p_discard_info->discard_reason_captured.reason_id, ctc_dkit_get_sub_reason_desc(p_discard_info->discard_reason_captured.reason_id));*/
     /*}*/

    CTC_DKITS_PRINT_FILE(p_wf, "\n");

    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_discard_get_stats(uint8 lchip, ctc_dkit_discard_stats_t* p_stats, uint32  fliter_flag)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = 0;
    uint8 slice = 0;
    uint16 i =0;
    EpeHdrAdjDebugStats0_m epe_hdr_adj_debug_stats;
    EpeHdrEditDebugStats0_m epe_hdr_edit_debug_stats;
    EpeHdrProcDebugStats0_m epe_hdr_proc_debug_stats;
    PktErrStatsMem_m pkt_err_stats_mem_slice;
    BufStoreStallDropDebugStats_m buf_store_stall_drop_debug_stats;
    QMgrEnqDebugStats_m q_mgr_enq_debug_stats;
    NetTxDebugStats0_m net_tx_debug_stats;
    NetRxDebugStats0_m net_rx_debug_stats;
    NetRxDebugStatsTable0_m net_rx_debug_stats_table;

    DKITS_PTR_VALID_CHECK(p_stats);

    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {

        /*1. read ipe discard stats*/
        /*1.1 future discard*/
        tbl_id = IpeFwdDiscardTypeStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpeFwdDiscardTypeStats0_cnt_f);
        for (i = 0; i < CTC_DKIT_IPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
            p_stats->stats[slice][CTC_DKIT_IPE_USERID_BINDING_DIS + i] = value;
        }
        /*1.2 asic hard error*/
        tbl_id = IpeFwdDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpeFwdDebugStats0_txPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_PKT_FWD] = value;

        tbl_id = IpeHdrAdjDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpeHdrAdjDebugStats0_txHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_HAR_ADJ] = value;

        tbl_id = IpeIntfMapDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpeIntfMapDebugStats0_txPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_INT_MAP] = value;

        tbl_id = IpeLkupMgrDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpeLkupMgrDebugStats0_qosToPktProcPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_LKUP_QOS] = value;

        cmd = DRV_IOR(tbl_id, IpeLkupMgrDebugStats0_keyGenToPktProcPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_LKUP_KEY_GEN] = value;

        tbl_id = IpePktProcDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, IpePktProcDebugStats0_txPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_IPE_HW_PKT_PROC] = value;

        /*2. read epe discard stats*/
        /*2.1 future discard*/
        tbl_id = EpeHdrEditDiscardTypeStats0_t + slice;
        cmd = DRV_IOR(tbl_id, EpeHdrEditDiscardTypeStats0_count_f);
        for(i = 0; i < CTC_DKIT_EPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
            p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS + i] = value;
        }
        /*2.2 asci hard error*/
        sal_memset(&epe_hdr_adj_debug_stats, 0, sizeof(epe_hdr_adj_debug_stats));
        tbl_id = EpeHdrAdjDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &epe_hdr_adj_debug_stats);
        GetEpeHdrAdjDebugStats0(A, rxDestIdDiscardCnt_f, &epe_hdr_adj_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID] = value;
        GetEpeHdrAdjDebugStats0(A, minPktLenErrorCnt_f, &epe_hdr_adj_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_MIN_PKT_LEN] = value;
        GetEpeHdrAdjDebugStats0(A, txPiHardErrorCnt_f, &epe_hdr_adj_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_TX_PI_HARD] = value;
        GetEpeHdrAdjDebugStats0(A, txPiDiscardCnt_f, &epe_hdr_adj_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_TX_PI_DISCARD] = value;
        GetEpeHdrAdjDebugStats0(A, rcvPktErrorCnt_f, &epe_hdr_adj_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_ADJ_RCV_PKT_ERROR] = value;

        sal_memset(&epe_hdr_edit_debug_stats, 0, sizeof(epe_hdr_edit_debug_stats));
        tbl_id = EpeHdrEditDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &epe_hdr_edit_debug_stats);
        GetEpeHdrEditDebugStats0(A, completeDiscardCnt_f, &epe_hdr_edit_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_EDIT_COMPLETE_DISCARD] = value;
        GetEpeHdrEditDebugStats0(A, txPiDiscardCnt_f, &epe_hdr_edit_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_EDIT_TX_PI_DISCARD] = value;
        GetEpeHdrEditDebugStats0(A, epeTxDataErrorCnt_f, &epe_hdr_edit_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_EDIT_EPE_TX_DATA] = value;

        sal_memset(&epe_hdr_proc_debug_stats, 0, sizeof(epe_hdr_proc_debug_stats));
        tbl_id = EpeHdrProcDebugStats0_t +slice;
        cmd =DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &epe_hdr_proc_debug_stats);
        GetEpeHdrProcDebugStats0(A, l2EditHardErrorCnt_f, &epe_hdr_proc_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_PROC_L2_EDIT] = value;
        GetEpeHdrProcDebugStats0(A, l3EditHardErrorCnt_f, &epe_hdr_proc_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_PROC_L3_EDIT] = value;
        GetEpeHdrProcDebugStats0(A, innerL2HardErrorCnt_f, &epe_hdr_proc_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_PROC_INNER_L2] = value;
        GetEpeHdrProcDebugStats0(A, payloadHardErrorCnt_f, &epe_hdr_proc_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_HDR_PROC_PAYLOAD] = value;

        tbl_id = EpeNextHopDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, EpeNextHopDebugStats0_piHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_OTHER_HW_NEXT_HOP] = value;
        tbl_id = EpeAclQosDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, EpeAclQosDebugStats0_txAclHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_OTHER_HW_ACL_OAM] = value;
        tbl_id = EpeClaDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, EpeClaDebugStats0_txClaHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_OTHER_HW_CLASS] = value;
        tbl_id = EpeOamDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, EpeOamDebugStats0_oamTxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->stats[slice][CTC_DKIT_EPE_OTHER_HW_OAM] = value;

    }

    /*3. read bsr discard stats*/
    /*3.1 bufstore discard*/
    for(i = 0; i< CTC_DKIT_CHANNEL_NUM; i++)
    {
        sal_memset(&pkt_err_stats_mem_slice, 0, sizeof(pkt_err_stats_mem_slice));
        tbl_id = PktErrStatsMem_t;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, i, cmd, &pkt_err_stats_mem_slice);
        GetPktErrStatsMem(A, pktSilentDropResrcFailCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktSilentDropSelfOrigDiscardCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktSilentDropHardDiscardCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktSilentDropChipIdMismatchCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktSilentDropNoBufCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktSilentDropDataErrorCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortUnderLenError_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortMetFifoDropCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortNoBufCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortFramingErrorCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortOverLenErrorCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
        GetPktErrStatsMem(A, pktAbortDataErrorCnt_f, &pkt_err_stats_mem_slice, &value);
        p_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL] = value;
    }

    sal_memset(&buf_store_stall_drop_debug_stats, 0, sizeof(buf_store_stall_drop_debug_stats));
    tbl_id = BufStoreStallDropDebugStats_t;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &buf_store_stall_drop_debug_stats);
    for(slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        GetBufStoreStallDropDebugStats(A, ipeHdrUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, ipeHdrMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_IPE_HDR_MCAST] = value;
        GetBufStoreStallDropDebugStats(A, ipeUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_IPE_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, ipeMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_IPE_MCAST] = value;
        GetBufStoreStallDropDebugStats(A, eLoopUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_ELOOP_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, eLoopMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_ELOOP_MCAST] = value;
        GetBufStoreStallDropDebugStats(A, qcnUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_QCN_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, qcnMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_QCN_MCAST] = value;
        GetBufStoreStallDropDebugStats(A, oamUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_OAM_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, oamMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_OAM_MCAST] = value;
        GetBufStoreStallDropDebugStats(A, dmaUcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_DMA_UCAST] = value;
        GetBufStoreStallDropDebugStats(A, dmaMcastDropCntSlice0_f + slice, &buf_store_stall_drop_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_BSR_BUFSTORE_DMA_MCAST] = value;
    }

    /*3.2 qmgrenq discard*/
    sal_memset(&q_mgr_enq_debug_stats, 0, sizeof(q_mgr_enq_debug_stats));
    tbl_id = QMgrEnqDebugStats_t;
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_debug_stats);
    GetQMgrEnqDebugStats(A, spanOnDropCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP]= value;
    GetQMgrEnqDebugStats(A, dropForEnqDiscardCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_ENQ_DISCARD]= value;
    GetQMgrEnqDebugStats(A, dropForFreeUsedUpCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_FREE_USED]= value;
    GetQMgrEnqDebugStats(A, dropForWredCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_WRED_DISCARD]= value;
    GetQMgrEnqDebugStats(A, dropForEgressResrcMgrCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_EGR_RESRC_MGR]= value;
    GetQMgrEnqDebugStats(A, dropForReservChanIdCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_RESERVE_CHANNEL]= value;
    GetQMgrEnqDebugStats(A, dropCriticalPktCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_CRITICAL_PKT]= value;
    GetQMgrEnqDebugStats(A, dropC2cPktCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_C2C_PKT]= value;
    GetQMgrEnqDebugStats(A, noChanLinkAggMemDiscardCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_NO_CHANNEL_LINKAGG]= value;
    GetQMgrEnqDebugStats(A, noPortLinkAggMemberDiscardCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_NO_PORT_LINKAGG]= value;
    GetQMgrEnqDebugStats(A, mcastLinkAggDiscardCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_MCAST_LINKAGG]= value;
    GetQMgrEnqDebugStats(A, stackingDiscardCnt_f, &q_mgr_enq_debug_stats, &value);
    p_stats->stats[0][CTC_DKIT_BSR_QMGRENQ_STACKING_DISCARD]= value;

    /*3.3 bsr_other_hw discard*/
    tbl_id = MetFifoDebugStats_t;
    cmd = DRV_IOR(tbl_id, MetFifoDebugStats_toEnqDiscardCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->stats[0][CTC_DKIT_BSR_OTHER_HW_ENQ_DISCARD]= value;

    tbl_id = QMgrDeqSliceDebugStats0_t;
    cmd = DRV_IOR(tbl_id, QMgrDeqSliceDebugStats0_ageDiscardCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->stats[0][CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD]= value;

    /*4. read net rx discard stats*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        sal_memset(&net_rx_debug_stats, 0, sizeof(net_rx_debug_stats));
        tbl_id = NetRxDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &net_rx_debug_stats);
        GetNetRxDebugStats0(A, rxBpduDropCnt0_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_BPDU0_ERROR] = value;
        GetNetRxDebugStats0(A, rxBpduDropCnt1_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_BPDU1_ERROR] = value;
        GetNetRxDebugStats0(A, rxBpduDropCnt2_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_BPDU2_ERROR] = value;
        GetNetRxDebugStats0(A, rxBpduDropCnt3_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_BPDU3_ERROR] = value;
        GetNetRxDebugStats0(A, rxPauseDropCnt0_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_PAUSE0_ERROR] = value;
        GetNetRxDebugStats0(A, rxPauseDropCnt1_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_PAUSE1_ERROR] = value;
        GetNetRxDebugStats0(A, rxPauseDropCnt2_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_PAUSE2_ERROR] = value;
        GetNetRxDebugStats0(A, rxPauseDropCnt3_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_PAUSE3_ERROR] = value;
        GetNetRxDebugStats0(A, txErrCnt_f, &net_rx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETRX_TX_ERROR] = value;
    }
    for (i = 0; i < CTC_DKIT_CHANNEL_NUM; i++)
    {
        sal_memset(&net_rx_debug_stats_table, 0, sizeof(net_rx_debug_stats_table));
        tbl_id = NetRxDebugStatsTable0_t + i / (CTC_DKIT_CHANNEL_NUM / 2);
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, (i % (CTC_DKIT_CHANNEL_NUM / 2)), cmd, &net_rx_debug_stats_table);
        GetNetRxDebugStatsTable0(A, lenErrDropCnt_f, &net_rx_debug_stats_table, &value);
        p_stats->netrx_stats[i][CTC_DKIT_NETRX_LEN_ERROR - CTC_DKIT_NETRX_LEN_ERROR] = value;
        GetNetRxDebugStatsTable0(A, frameErrDropCnt_f, &net_rx_debug_stats_table, &value);
        p_stats->netrx_stats[i][CTC_DKIT_NETRX_FRAME_ERROR - CTC_DKIT_NETRX_LEN_ERROR] = value;
        GetNetRxDebugStatsTable0(A, pktErrDropCnt_f, &net_rx_debug_stats_table, &value);
        p_stats->netrx_stats[i][CTC_DKIT_NETRX_PKT_ERROR - CTC_DKIT_NETRX_LEN_ERROR] = value;
        GetNetRxDebugStatsTable0(A, fullDropCnt_f, &net_rx_debug_stats_table, &value);
        p_stats->netrx_stats[i][CTC_DKIT_NETRX_FULL_DROP - CTC_DKIT_NETRX_LEN_ERROR] = value;
    }

    /*5. read net tx discard stats*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        sal_memset(&net_tx_debug_stats, 0, sizeof(net_tx_debug_stats));
        tbl_id = NetTxDebugStats0_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &net_tx_debug_stats);
        GetNetTxDebugStats0(A, epeNoBufDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_EPE_NO_BUF] = value;
        GetNetTxDebugStats0(A, epeNoEopDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_EPE_NO_EOP] = value;
        GetNetTxDebugStats0(A, epeNoSopDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_EPE_NO_SOP] = value;
        GetNetTxDebugStats0(A, infoDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_INFO] = value;
        GetNetTxDebugStats0(A, epeMinLenDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_EPE_MIN_LEN] = value;
        GetNetTxDebugStats0(A, spanMinLenDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_SPAN_MIN_LEN] = value;
        GetNetTxDebugStats0(A, spanNoBufDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_SPAN_NO_BUF] = value;
        GetNetTxDebugStats0(A, spanNoEopDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_SPAN_NO_EOP] = value;
        GetNetTxDebugStats0(A, spanNoSopDropCnt_f, &net_tx_debug_stats, &value);
        p_stats->stats[slice][CTC_DKIT_NETTX_SPAN_NO_SOP] = value;
    }

    /*6. read oam discard stats*/
    cmd = DRV_IOR(OamProcDebugStats_t, OamProcDebugStats_linkAggMemDiscardCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->stats[0][CTC_DKIT_OAM_LINK_AGG_NO_MEMBER] = value;
    cmd = DRV_IOR(OamHdrEditDebugStats_t, OamHdrEditDebugStats_asicHardDiscardCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->stats[0][CTC_DKIT_OAM_ASIC_ERROR] = value;
    cmd = DRV_IOR(OamHdrEditDebugStats_t, OamHdrEditDebugStats_exceptionDiscardCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->stats[0][CTC_DKIT_OAM_EXCEPTION] = value;

    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_discard_get_reason(uint8 lchip, const ctc_dkit_discard_stats_t* p_last_stats,
                                                                       const ctc_dkit_discard_stats_t* p_cur_stats, ctc_dkit_discard_info_t* p_discard_info)
{
    uint16 i = 0;
    uint8 slice = 0;
    sal_time_t tv;
    char* p_time_str = NULL;

    DKITS_PTR_VALID_CHECK(p_last_stats);
    DKITS_PTR_VALID_CHECK(p_cur_stats);
    DKITS_PTR_VALID_CHECK(p_discard_info);

    /*get systime*/
    sal_time(&tv);
    p_time_str = sal_ctime(&tv);
    if (p_time_str)
    {
        sal_strncpy(p_discard_info->systime_str, p_time_str, 50);
    }

    /*1. check IPE discard*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        for (i = CTC_DKIT_IPE_USERID_BINDING_DIS; i < CTC_DKIT_IPE_MAX; i++)
        {
            if (p_last_stats->stats[slice][i] != p_cur_stats->stats[slice][i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_IPE);
                ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice, p_cur_stats->stats[slice][i]);
            }
        }
    }

    /*2. check EPE discard*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        for (i = CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS; i < CTC_DKIT_EPE_MAX; i++)
        {
            if (p_last_stats->stats[slice][i] != p_cur_stats->stats[slice][i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_EPE);
                ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice, p_cur_stats->stats[slice][i]);
            }
        }
    }

    /*3. check BSR discard*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        for (i = CTC_DKIT_BSR_BUFSTORE_IPE_HDR_UCAST; i < CTC_DKIT_BSR_MAX; i++)
        {
            if (p_last_stats->stats[slice][i] != p_cur_stats->stats[slice][i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
                ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, slice, p_cur_stats->stats[slice][i]);
            }
        }
    }

    for(i = 0; i < CTC_DKIT_CHANNEL_NUM; i++)
    {
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_SELF_ORIG - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_HARD_DISCARD - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_CHIP_MISMATCH - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_SILENT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_UNDER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_MET_FIFO - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_NO_BUF - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_FRAME_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_OVER_LEN - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
        if(p_last_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]
            != p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR, CTC_DKIT_DISCARD_FLAG_BSR,
                i, 0, p_cur_stats->bsr_stats[i][CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL]);
        }
    }

    /*4. check NET-RX discard*/
    for(slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        for (i = CTC_DKIT_NETRX_BPDU0_ERROR; i < CTC_DKIT_NETRX_MAX; i++)
        {
            if (p_last_stats->stats[slice][i] != p_cur_stats->stats[slice][i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
                ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_NET_RX, CTC_DKIT_INVALID_PORT, slice, p_cur_stats->stats[slice][i]);
            }
        }
    }
    for(i= 0; i < CTC_DKIT_CHANNEL_NUM; i++)
    {
        if(p_last_stats->netrx_stats[i][CTC_DKIT_NETRX_LEN_ERROR - CTC_DKIT_NETRX_LEN_ERROR]
            != p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_LEN_ERROR - CTC_DKIT_NETRX_LEN_ERROR])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_LEN_ERROR, CTC_DKIT_DISCARD_FLAG_NET_RX,
                i, 0, p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_LEN_ERROR - CTC_DKIT_NETRX_LEN_ERROR]);
        }
        if(p_last_stats->netrx_stats[i][CTC_DKIT_NETRX_FRAME_ERROR - CTC_DKIT_NETRX_LEN_ERROR]
            != p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_FRAME_ERROR - CTC_DKIT_NETRX_LEN_ERROR])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_FRAME_ERROR, CTC_DKIT_DISCARD_FLAG_NET_RX,
                i, 0, p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_FRAME_ERROR - CTC_DKIT_NETRX_LEN_ERROR]);
        }
        if(p_last_stats->netrx_stats[i][CTC_DKIT_NETRX_PKT_ERROR - CTC_DKIT_NETRX_LEN_ERROR]
            != p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_PKT_ERROR - CTC_DKIT_NETRX_LEN_ERROR])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_PKT_ERROR, CTC_DKIT_DISCARD_FLAG_NET_RX,
                i, 0, p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_PKT_ERROR - CTC_DKIT_NETRX_LEN_ERROR]);
        }
        if(p_last_stats->netrx_stats[i][CTC_DKIT_NETRX_FULL_DROP - CTC_DKIT_NETRX_LEN_ERROR]
            != p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_FULL_DROP - CTC_DKIT_NETRX_LEN_ERROR])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_FULL_DROP, CTC_DKIT_DISCARD_FLAG_NET_RX,
                i, 0, p_cur_stats->netrx_stats[i][CTC_DKIT_NETRX_FULL_DROP - CTC_DKIT_NETRX_LEN_ERROR]);
        }
    }

    /*5. check NET-TX discard*/
    for(slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        for (i = CTC_DKIT_NETTX_EPE_NO_BUF; i < CTC_DKIT_NETTX_MAX; i++)
        {
            if (p_last_stats->stats[slice][i] != p_cur_stats->stats[slice][i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX);
                ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_NET_TX, CTC_DKIT_INVALID_PORT, slice, p_cur_stats->stats[slice][i]);
            }
        }
    }

    /*6. check oam discard stats*/
    for (i = CTC_DKIT_OAM_LINK_AGG_NO_MEMBER; i < CTC_DKIT_OAM_MAX; i++)
    {
        if (p_last_stats->stats[0][i] != p_cur_stats->stats[0][i])
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_OAM);
            ADD_DISCARD_REASON(p_discard_info, i, CTC_DKIT_DISCARD_FLAG_OAM, CTC_DKIT_INVALID_PORT, 0, p_cur_stats->stats[0][i]);
        }
    }

    return CLI_SUCCESS;
}

#if 0
STATIC int32
_ctc_goldengate_dkit_discard_get_sub_reason(ctc_dkit_discard_para_t* p_discard_para, ctc_dkit_discard_info_t* p_discard_info)
{
    int32 ret = CLI_SUCCESS;

    DKITS_PTR_VALID_CHECK(p_discard_info);
    DKITS_PTR_VALID_CHECK(p_discard_para);

    if (p_discard_info->discard_flag)
    {
        /*1. Get discard reason by bus info*/
        if ((!p_discard_para->on_line) || (p_discard_info->discard_count == 1))/*multi discard occured, can not use bus info!!!*/
        {
            ret = ctc_goldengate_dkit_bus_get_discard_sub_reason(p_discard_para, p_discard_info);
        }

        /*2. Get discard reason by captured info*/
         /*if (p_discard_para->captured)     not support!!!*/
         /*{*/
         /*    ret = ctc_dkit_captured_get_discard_sub_reason(p_discard_para, p_discard_info);*/
         /*}*/
    }

    return ret;
}
#endif
/*
STATIC int32
_ctc_goldengate_dkit_discard_get_stats_by_reason(uint8 lchip, uint32 reason_id, uint32*  stats)
{




    return CLI_SUCCESS;
}
*/
int32
ctc_goldengate_dkit_clear_feature_discard_stats(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = 0;
    uint8 slice = 0;
    uint8 i = 0;

    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {

        tbl_id = IpeFwdDiscardTypeStats0_t + slice;
        cmd = DRV_IOW(tbl_id, IpeFwdDiscardTypeStats0_cnt_f);
        for (i = 0; i < CTC_DKIT_IPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
        }

        tbl_id = EpeHdrEditDiscardTypeStats0_t + slice;
        cmd = DRV_IOW(tbl_id, EpeHdrEditDiscardTypeStats0_count_f);
        for (i = 0; i < CTC_DKIT_EPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
        }
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_set_assign_port(uint8 lchip, uint8 assign_port, uint16 gport)
{
    uint32 cmd = 0;
    uint32 value = 0;

    value = assign_port;
    cmd = DRV_IOR(IpeDiscardTypeStatsCtl_t, IpeDiscardTypeStatsCtl_statsOnAssignedPort_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    cmd = DRV_IOR(EpeDiscardTypeStatsCtl_t, EpeDiscardTypeStatsCtl_statsOnAssignedPort_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    value = CTC_DKIT_CTC_GPORT_TO_DRV_GPORT(gport);
    cmd = DRV_IOR(IpeDiscardTypeStatsCtl_t, IpeDiscardTypeStatsCtl_globalSrcPort_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    cmd = DRV_IOR(EpeDiscardTypeStatsCtl_t, EpeDiscardTypeStatsCtl_globalDestPort_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_process(void* p_para)
{
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    ctc_dkit_discard_stats_t* cur_stats = NULL;
    ctc_dkit_discard_info_t* discard_info = NULL;
    sal_file_t  p_wf = p_discard_para->p_wf;
    uint8 front = 0;
    uint8 lchip = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_discard_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);
    DKITS_PTR_VALID_CHECK(g_gg_dkit_master[lchip]);

    cur_stats = (ctc_dkit_discard_stats_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_discard_stats_t));
    if (NULL == cur_stats)
    {
        return CLI_ERROR;
    }

    discard_info = (ctc_dkit_discard_info_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_discard_info_t));
    if (NULL == discard_info)
    {
        mem_free(cur_stats);
        return CLI_ERROR;
    }


    sal_memset(cur_stats, 0, sizeof(ctc_dkit_discard_stats_t));
    sal_memset(discard_info, 0, sizeof(ctc_dkit_discard_info_t));

    if (p_discard_para->history)/*show history*/
    {
        front = g_gg_dkit_master[lchip]->discard_history.front;
        while (g_gg_dkit_master[lchip]->discard_history.rear != front)
        {
            _ctc_goldengate_dkit_discard_display(&(g_gg_dkit_master[lchip]->discard_history.discard[front++]), p_wf);
            front %= CTC_DKIT_DISCARD_HISTORY_NUM;
        }
    }
    else
    {

        if ((g_gg_dkit_master[lchip]->discard_stats.match_port != p_discard_para->match_port)
            || (p_discard_para->match_port && ((g_gg_dkit_master[lchip]->discard_stats.gport != p_discard_para->gport))))
        {
            ctc_goldengate_dkit_discard_set_assign_port(lchip, p_discard_para->match_port, p_discard_para->gport);
            ctc_goldengate_dkit_clear_feature_discard_stats(lchip);

            g_gg_dkit_master[lchip]->discard_stats.gport = p_discard_para->gport;
            g_gg_dkit_master[lchip]->discard_stats.match_port = p_discard_para->match_port;
            sal_memset(&(g_gg_dkit_master[lchip]->discard_stats.stats[0][CTC_DKIT_IPE_USERID_BINDING_DIS]), 0,
                       sizeof(uint16)*CTC_DKIT_SLICE_NUM*(CTC_DKIT_IPE_HW_HAR_ADJ - CTC_DKIT_IPE_USERID_BINDING_DIS));
            sal_memset(&(g_gg_dkit_master[lchip]->discard_stats.stats[0][CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS]), 0,
                       sizeof(uint16)*CTC_DKIT_SLICE_NUM*(CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID - CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS));
            goto End;
        }

        /*1. read discard stats*/
        _ctc_goldengate_dkit_discard_get_stats(lchip,cur_stats, 0);

        /*2. compare discard stats and get reason id*/
        _ctc_goldengate_dkit_discard_get_reason(lchip, &(g_gg_dkit_master[lchip]->discard_stats), cur_stats,  discard_info);
        cur_stats->match_port = g_gg_dkit_master[lchip]->discard_stats.match_port;
        cur_stats->gport = g_gg_dkit_master[lchip]->discard_stats.gport;
        sal_memcpy(&(g_gg_dkit_master[lchip]->discard_stats), cur_stats, sizeof(ctc_dkit_discard_stats_t));
#if 0
/*discard process should not read bus info*/
        if (discard_info->discard_flag)
        {
            /*if some discard occured, analyse discard reason from bus and captured info*/
            _ctc_goldengate_dkit_discard_get_sub_reason(p_discard_para, discard_info);
        }
#endif
        /*3. display result*/
        _ctc_goldengate_dkit_discard_display(discard_info, p_wf);

        /*4. store result history*/
        if (discard_info->discard_flag != 0)
        {
            sal_memcpy(&(g_gg_dkit_master[lchip]->discard_history.discard[g_gg_dkit_master[lchip]->discard_history.rear++]),
                       discard_info, sizeof(ctc_dkit_discard_info_t));
            g_gg_dkit_master[lchip]->discard_history.rear %= CTC_DKIT_DISCARD_HISTORY_NUM;
            if (g_gg_dkit_master[lchip]->discard_history.rear == g_gg_dkit_master[lchip]->discard_history.front)
            {
                g_gg_dkit_master[lchip]->discard_history.front++;
                g_gg_dkit_master[lchip]->discard_history.front %= CTC_DKIT_DISCARD_HISTORY_NUM;
            }
        }
    }

End:
    mem_free(cur_stats);
    mem_free(discard_info);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_discard_print_type(uint32 reason_id, char* module)
{
    char buff[256] = {0};
    sal_sprintf(buff, "%s", ctc_goldengate_dkit_get_reason_desc(reason_id));
    CTC_DKIT_PRINT(" %-10d %-9s %s\n", reason_id, module, buff);
    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_show_type(void* p_para)
{
    uint32 i = 0;
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    char* module = "  -";


    DKITS_PTR_VALID_CHECK(p_para);

    CTC_DKIT_PRINT(" %-10s %-9s %s\n", "ReasonId", "Module", "Description");
    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    if (0xFFFFFFFF == p_discard_para->reason_id) /*all*/
    {
        for (i = CTC_DKIT_IPE_USERID_BINDING_DIS; i < CTC_DKIT_IPE_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "IPE");
        }
        for (i = CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS; i < CTC_DKIT_EPE_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "EPE");
        }
        for (i = CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL; i < CTC_DKIT_BSR_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "BSR");
        }
        for (i = CTC_DKIT_NETRX_LEN_ERROR; i < CTC_DKIT_NETRX_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "NETRX");
        }
        for (i = CTC_DKIT_NETTX_EPE_NO_BUF; i < CTC_DKIT_NETTX_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "NETTX");
        }
        for (i = CTC_DKIT_OAM_LINK_AGG_NO_MEMBER; i < CTC_DKIT_OAM_MAX; i++)
        {
            _ctc_goldengate_dkit_discard_print_type(i, "OAM");
        }
    }
    else
    {
        if ((p_discard_para->reason_id >= CTC_DKIT_IPE_USERID_BINDING_DIS) && (p_discard_para->reason_id < CTC_DKIT_IPE_MAX))
        {
            module = "IPE";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS) && (p_discard_para->reason_id < CTC_DKIT_EPE_MAX))
        {
            module = "EPE";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL) && (p_discard_para->reason_id < CTC_DKIT_BSR_MAX))
        {
            module = "BSR";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_NETRX_LEN_ERROR) && (p_discard_para->reason_id < CTC_DKIT_NETRX_MAX))
        {
            module = "NETRX";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_NETTX_EPE_NO_BUF) && (p_discard_para->reason_id < CTC_DKIT_NETTX_MAX))
        {
            module = "NETTX";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_OAM_LINK_AGG_NO_MEMBER) && (p_discard_para->reason_id < CTC_DKIT_OAM_MAX))
        {
            module = "OAM";
        }
        _ctc_goldengate_dkit_discard_print_type(p_discard_para->reason_id, module);
    }
    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    return CLI_SUCCESS;
}



STATIC int32
_ctc_goldengate_dkit_discard_print_stats(uint8 lchip, uint32 reason_id, char* module)
{
#define BUF_LEN 128
    char buff[BUF_LEN] = {0};
    char port_str[BUF_LEN] = "-       ";
    const char* desc = NULL;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint16 stats = 0;
    uint8 chan = 0;
    uint8 i = 0;
    uint32 cmd = 0;
    uint8 slice = 0;
    uint32 local_phy_port = 0;
    ctc_dkit_discard_stats_t* cur_stats = NULL;

    cur_stats = (ctc_dkit_discard_stats_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_discard_stats_t));
    if (NULL == cur_stats)
    {
        return CLI_ERROR;
    }
    sal_memset(cur_stats, 0, sizeof(ctc_dkit_discard_stats_t));
    _ctc_goldengate_dkit_discard_get_stats(lchip,cur_stats, 0);

    desc = ctc_goldengate_dkit_get_reason_desc(reason_id);
    CTC_DKIT_GET_GCHIP(lchip, gchip);
    for (i = 0; i < BUF_LEN; i++)
    {
        if (desc[i] == ',')
        {
            break;
        }
        buff[i] = desc[i];
    }

    cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
    if (reason_id >= CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL && reason_id <= CTC_DKIT_BSR_PKTERR_ABORT_DATA_ERROR)
    {
        for (chan = 0; chan < CTC_DKIT_CHANNEL_NUM; chan++ )
        {
            slice = (chan >= (CTC_DKIT_CHANNEL_NUM / 2))? 1 : 0;
            DRV_IOCTL(lchip, chan, cmd, &local_phy_port);
            gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
            sal_sprintf(port_str, "0x%04x  ", gport);
            stats = cur_stats->bsr_stats[chan][reason_id - CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL];
            if (stats)
            {
                CTC_DKIT_PRINT(" %-10d %-8s %-8d %s %-8d %s\n", reason_id, module, slice, port_str, stats, buff);
            }
        }
        goto End;
    }
    else if (reason_id >= CTC_DKIT_NETRX_LEN_ERROR && reason_id <= CTC_DKIT_NETRX_FULL_DROP)
    {
        for (chan = 0; chan < CTC_DKIT_CHANNEL_NUM; chan++ )
        {
            slice = (chan >= (CTC_DKIT_CHANNEL_NUM / 2))? 1 : 0;
            DRV_IOCTL(lchip, chan, cmd, &local_phy_port);
            gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));
            sal_sprintf(port_str, "0x%04x  ", gport);
            stats = cur_stats->netrx_stats[chan][reason_id - CTC_DKIT_NETRX_LEN_ERROR];
            if (stats)
            {
                CTC_DKIT_PRINT(" %-10d %-8s %-8d %s %-8d %s\n", reason_id, module, slice, port_str, stats, buff);
            }
        }
        goto End;
    }
    else if ((reason_id >= CTC_DKIT_IPE_HW_HAR_ADJ && reason_id <= CTC_DKIT_IPE_HW_PKT_FWD)
        || (reason_id >= CTC_DKIT_EPE_HDR_ADJ_RX_DEST_ID && reason_id <= CTC_DKIT_EPE_OTHER_HW_OAM)
        || (reason_id >= CTC_DKIT_NETRX_BPDU0_ERROR && reason_id <= CTC_DKIT_NETRX_TX_ERROR)
        || (reason_id >= CTC_DKIT_NETTX_EPE_NO_BUF && reason_id <= CTC_DKIT_NETTX_SPAN_NO_SOP)
        || (reason_id >= CTC_DKIT_OAM_LINK_AGG_NO_MEMBER && reason_id <= CTC_DKIT_OAM_EXCEPTION))
    {
        sal_sprintf(port_str, "%s", "-       ");
    }
    else if (g_gg_dkit_master[lchip]->discard_stats.match_port)
    {
        sal_sprintf(port_str, "0x%04x  ", g_gg_dkit_master[lchip]->discard_stats.gport);
    }
    else
    {
        sal_sprintf(port_str, "%s", "-       ");
    }

    stats = cur_stats->stats[0][reason_id];
    if (stats)
    {
        if ((reason_id >= CTC_DKIT_BSR_QMGRENQ_SPAN_ON_DROP && reason_id <= CTC_DKIT_BSR_OTHER_HW_AGE_DISCARD)
            || (reason_id >= CTC_DKIT_OAM_LINK_AGG_NO_MEMBER && reason_id <= CTC_DKIT_OAM_EXCEPTION))
        {
            CTC_DKIT_PRINT(" %-10d %-8s %-8s %s %-8d %s\n", reason_id, module, "-", port_str, stats, buff);
        }
        else
        {
            CTC_DKIT_PRINT(" %-10d %-8s %-8d %s %-8d %s\n", reason_id, module, 0, port_str, stats, buff);
        }

    }
    stats = cur_stats->stats[1][reason_id];
    if (stats)
    {
        CTC_DKIT_PRINT(" %-10d %-8s %-8d %s %-8d %s\n", reason_id, module, 1, port_str, stats, buff);
    }
End:
    mem_free(cur_stats);
    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_show_stats(void* p_para)
{
    uint32 i = 0;
    uint8 lchip = 0;
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;

    DKITS_PTR_VALID_CHECK(p_para);

    lchip = p_discard_para->lchip;
    CTC_DKIT_PRINT(" %-10s %-8s %-8s %-8s %-8s %s\n", "ReasonId", "Module", "Slice", "Port", "Stats", "Type");
    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    for (i = CTC_DKIT_IPE_USERID_BINDING_DIS; i < CTC_DKIT_IPE_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "IPE");
    }
    for (i = CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS; i < CTC_DKIT_EPE_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "EPE");
    }
    for (i = CTC_DKIT_BSR_PKTERR_SILENT_RESRC_FAIL; i < CTC_DKIT_BSR_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "BSR");
    }
    for (i = CTC_DKIT_NETRX_LEN_ERROR; i < CTC_DKIT_NETRX_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "NETRX");
    }
    for (i = CTC_DKIT_NETTX_EPE_NO_BUF; i < CTC_DKIT_NETTX_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "NETTX");
    }
    for (i = CTC_DKIT_OAM_LINK_AGG_NO_MEMBER; i < CTC_DKIT_OAM_MAX; i++)
    {
        _ctc_goldengate_dkit_discard_print_stats(lchip, i, "OAM");
    }

    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_to_cpu_en(void* p_para)
{
    uint8 lchip = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint8 all_discard = 0;
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    DKITS_PTR_VALID_CHECK(p_para);

    lchip = p_discard_para->lchip;
    if (0xFFFFFFFF == p_discard_para->reason_id)
    {
        all_discard = 1;
    }
    else
    {
        if (!((p_discard_para->reason_id >= CTC_DKIT_IPE_USERID_BINDING_DIS && p_discard_para->reason_id <= CTC_DKIT_IPE_RESERVED2)
            || (p_discard_para->reason_id >= CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS && p_discard_para->reason_id <= CTC_DKIT_EPE_OAM_TO_LOCAL_DIS)))
        {
            CTC_DKIT_PRINT("Not support!!!\n");
            return CLI_ERROR;
        }
    }

    if ((p_discard_para->reason_id >= CTC_DKIT_IPE_USERID_BINDING_DIS && p_discard_para->reason_id <= CTC_DKIT_IPE_RESERVED2)
        ||all_discard)
    {
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardPacketExceptionEn_f);
        value = p_discard_para->discard_to_cpu_en;
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_discardTypeException_f);
        value = all_discard? 0x3F :(p_discard_para->reason_id - CTC_DKIT_IPE_USERID_BINDING_DIS);
        DRV_IOCTL(lchip, 0, cmd, &value);
    }

    if ((p_discard_para->reason_id >= CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS && p_discard_para->reason_id <= CTC_DKIT_EPE_OAM_TO_LOCAL_DIS)
        || all_discard)
    {
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardPacketExceptionEn_f);
        value = p_discard_para->discard_to_cpu_en;
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_discardTypeException_f);
        value = all_discard? 0x3F :(p_discard_para->reason_id - CTC_DKIT_EPE_HDR_ADJ_DESTID_DIS);
        DRV_IOCTL(lchip, 0, cmd, &value);
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_discard_display_en(void* p_para)
{
    uint8 lchip = 0;
    uint32* disacrd_bitmap = NULL;
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    DKITS_PTR_VALID_CHECK(p_para);

    lchip = p_discard_para->lchip;
    if (0xFFFFFFFF == p_discard_para->reason_id)
    {
        if (p_discard_para->discard_display_en)
        {
            sal_memset(g_gg_dkit_master[lchip]->discard_enable_bitmap, 0xFF , ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);
        }
        else
        {
            sal_memset(g_gg_dkit_master[lchip]->discard_enable_bitmap, 0 , ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);
        }
    }
    else
    {
        disacrd_bitmap = (((uint32*)g_gg_dkit_master[lchip]->discard_enable_bitmap) + (p_discard_para->reason_id / 32));
        if (p_discard_para->discard_display_en)
        {
            DKITS_BIT_SET(*disacrd_bitmap, p_discard_para->reason_id % 32);
        }
        else
        {
            DKITS_BIT_UNSET(*disacrd_bitmap, p_discard_para->reason_id % 32);
        }
    }

    return CLI_SUCCESS;
}



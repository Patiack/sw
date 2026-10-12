#include "greatbelt/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_discard.h"
#include "ctc_greatbelt_dkit_discard_type.h"



#define ADD_DISCARD_REASON(p_discard_info,id,discard_module,port_num,slice_id)    \
            if (p_discard_info->discard_count < CTC_DKIT_DISCARD_REASON_NUM)\
            {\
                uint32 disacrd_bitmap = *(((uint32*)g_gb_dkit_master[lchip]->discard_enable_bitmap) + (id/32));\
                if(DKITS_IS_BIT_SET(disacrd_bitmap, id%32)) {\
                p_discard_info->discard_reason[p_discard_info->discard_count].reason_id = id;\
                p_discard_info->discard_reason[p_discard_info->discard_count].module = discard_module;\
                p_discard_info->discard_reason[p_discard_info->discard_count].port = port_num;\
                p_discard_info->slice_ipe = slice_id;\
                p_discard_info->slice_epe = slice_id;\
                p_discard_info->discard_count++;}\
            }\

extern ctc_dkit_master_t* g_gb_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM];



STATIC int32
_ctc_greatbelt_dkit_discard_display_reason(ctc_dkit_discard_reason_t* discard_reason, sal_file_t p_wf)
{
    DKITS_PTR_VALID_CHECK(discard_reason);

    switch (discard_reason->module)
    {
        case CTC_DKIT_DISCARD_FLAG_IPE:
            {
                if (discard_reason->port != CTC_DKIT_INVALID_PORT)
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at IPE on port %d, reason id %d, discard id %d: %s\n", discard_reason->port,
                                discard_reason->reason_id, discard_reason->reason_id - CTC_DKIT_IPE_USER_ID_BINDING_DISCARD , ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
                else
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at IPE, reason id %d, discard id %d: %s\n", discard_reason->reason_id, discard_reason->reason_id - CTC_DKIT_IPE_USER_ID_BINDING_DISCARD,
                                ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_EPE:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at EPE, reason id %d, discard id %d: %s\n",  discard_reason->reason_id, discard_reason->reason_id - CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD,
                            ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_BSR:
            {
                if (discard_reason->port != CTC_DKIT_INVALID_PORT)
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at BSR on channel %d, reason id %d: %s\n", discard_reason->port,
                                discard_reason->reason_id, ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
                else
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at BSR, reason id %d: %s\n", discard_reason->reason_id,
                                ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_OAM:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at OAM engine, reason id %d: %s\n",  discard_reason->reason_id,
                            ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_NET_RX:
            {
                if (discard_reason->port != CTC_DKIT_INVALID_PORT)
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETRX on channel %d, reason id %d: %s\n", discard_reason->port,
                                discard_reason->reason_id, ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
                else
                {
                    CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETRX, reason id %d: %s\n",  discard_reason->reason_id,
                                ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
                }
            }
            break;
        case CTC_DKIT_DISCARD_FLAG_NET_TX:
            {
                CTC_DKITS_PRINT_FILE(p_wf, "Packet discard at NETTX, reason id %d: %s\n",  discard_reason->reason_id,
                            ctc_greatbelt_dkit_get_reason_desc(discard_reason->reason_id));
            }
            break;
        default:
            break;
    }
    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_discard_display_bus_reason(ctc_dkit_discard_info_t* p_discard_info, sal_file_t p_wf)
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
                                        ctc_greatbelt_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id));
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_1)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_greatbelt_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
                                        p_discard_info->discard_reason_bus.value[0]);
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_2)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d and %d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_greatbelt_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
                                        p_discard_info->discard_reason_bus.value[0],
                                        p_discard_info->discard_reason_bus.value[1]);
        }
        else if (p_discard_info->discard_reason_bus.value_type == CTC_DKIT_DISCARD_VALUE_TYPE_3)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "1. Sub reason id %d: %s%d, %d and %d\n", p_discard_info->discard_reason_bus.reason_id,
                                        ctc_greatbelt_dkit_get_sub_reason_desc(p_discard_info->discard_reason_bus.reason_id),
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
_ctc_greatbelt_dkit_discard_display(ctc_dkit_discard_info_t* p_discard_info, sal_file_t p_wf)
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
            _ctc_greatbelt_dkit_discard_display_reason(&p_discard_info->discard_reason[i], p_wf);
        }
    }

    /*3. display reason from bus info*/
   if (p_discard_info->discard_reason_bus.valid)
   {
       _ctc_greatbelt_dkit_discard_display_bus_reason(p_discard_info, p_wf);
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
_ctc_greatbelt_dkit_discard_get_stats(uint8 lchip, ctc_dkit_discard_stats_t* p_stats, uint32  fliter_flag)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = 0;
    uint8 slice = 0;
    uint8 i =0;
    pkt_err_stats_mem_t pkt_err_stats_mem_slice;
    q_mgr_enq_debug_stats_t q_mgr_enq_debug_stats;
    net_tx_debug_stats_t net_tx_debug_stats;
    net_rx_debug_stats_t net_rx_debug_stats;

    DKITS_PTR_VALID_CHECK(p_stats);

    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        /*1. read ipe discard stats*/
        /*1.1 asic hard error*/
        tbl_id = IpeFwdDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpeFwdDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->ipe[slice].hw_discard.total = value;

        tbl_id = IpeHdrAdjDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpeHdrAdjDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->ipe[slice].hw_discard.har_adj = value;

        tbl_id = IpeIntfMapInfoOutStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpeIntfMapInfoOutStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->ipe[slice].hw_discard.intf_map = value;

        tbl_id = IpeLkupMgrDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpeLkupMgrDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->ipe[slice].hw_discard.lkup_mgr = value;

        tbl_id = IpePktProcDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpePktProcDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->ipe[slice].hw_discard.pkt_proc = value;


        /*1.2 future discard*/
        tbl_id = IpeFwdDiscardTypeStats_t + slice;
        cmd = DRV_IOR(tbl_id, IpeFwdDiscardTypeStats_DiscardPktCnt_f);
        for (i = 0; i < CTC_DKIT_IPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
            p_stats->ipe[slice].future_discard[i] = value;
        }

        /*2. read epe discard stats*/
        /*2.1 asci hard error*/
        tbl_id = EpeHdrEditDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeHdrEditDebugStats_TxPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->epe[slice].hw_discard.total = value;

        tbl_id = EpeHdrAdjDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeHdrAdjDebugStats_TransPiHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->epe[slice].hw_discard.har_adj = value;

        tbl_id = EpeNextHopDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeNextHopDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->epe[slice].hw_discard.next_hop = value;

        tbl_id = EpeAclQosDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeAclQosDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->epe[slice].hw_discard.acl_qos = value;

        tbl_id = EpeOamDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeOamDebugStats_TxHardErrorCnt_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        p_stats->epe[slice].hw_discard.oam = value;

        /*2.2 future discard*/
        tbl_id = EpeHdrEditDiscardTypeStats_t + slice;
        cmd = DRV_IOR(tbl_id, EpeHdrEditDiscardTypeStats_DiscardPktCnt_f);
        for (i = 0; i < CTC_DKIT_EPE_DISCARD_TYPE_NUM; i++)
        {
            DRV_IOCTL(lchip, i, cmd, &value);
            p_stats->epe[slice].future_discard[i] = value;
        }
    }

    /*3. read bsr discard stats*/
    /*3.1 bufstore discard*/
    for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM; i++)
    {
        sal_memset(&pkt_err_stats_mem_slice, 0, sizeof(pkt_err_stats_mem_slice));
        tbl_id = PktErrStatsMem_t;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, i, cmd, &pkt_err_stats_mem_slice);
        p_stats->bsr.bufstore[i].abort_total = pkt_err_stats_mem_slice.abort_cnt;
        p_stats->bsr.bufstore[i].abort_data_error = pkt_err_stats_mem_slice.abort_data_error_cnt;
        p_stats->bsr.bufstore[i].abort_frame_error = pkt_err_stats_mem_slice.abort_framing_error_cnt;
        p_stats->bsr.bufstore[i].abort_over_len_error = pkt_err_stats_mem_slice.abort_over_len_error_cnt;
        p_stats->bsr.bufstore[i].abort_under_len = pkt_err_stats_mem_slice.abort_under_len_error_cnt;
        p_stats->bsr.bufstore[i].abort_met_fifo_drop = pkt_err_stats_mem_slice.abort_met_fifo_drop_cnt;
        p_stats->bsr.bufstore[i].abort_no_buf = pkt_err_stats_mem_slice.abort_no_buf_cnt;

        p_stats->bsr.bufstore[i].silent_total = pkt_err_stats_mem_slice.silent_drop_cnt;
        p_stats->bsr.bufstore[i].silent_hard_discard = pkt_err_stats_mem_slice.silent_drop_hard_discard_cnt;
        p_stats->bsr.bufstore[i].silent_data_error = pkt_err_stats_mem_slice.silent_drop_data_error_cnt;
        p_stats->bsr.bufstore[i].silent_chip_mismatch = pkt_err_stats_mem_slice.silent_drop_chip_id_mismatch_cnt;
        p_stats->bsr.bufstore[i].silent_no_buf = pkt_err_stats_mem_slice.silent_drop_no_buf_cnt;
    }

    /*3.2 qmgr discard*/
    sal_memset(&q_mgr_enq_debug_stats, 0, sizeof(q_mgr_enq_debug_stats));
    cmd = DRV_IOR(QMgrEnqDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_debug_stats);
    p_stats->bsr.qmgr.total = q_mgr_enq_debug_stats.drop_enq_msg_cnt;
    p_stats->bsr.qmgr.enq_discard = q_mgr_enq_debug_stats.drop4_enq_discard_cnt;
    p_stats->bsr.qmgr.egr_resrc_mgr = q_mgr_enq_debug_stats.drop4_egress_resrc_mgr_cnt;
    p_stats->bsr.qmgr.critical_pkt = q_mgr_enq_debug_stats.drop_critical_pkt_cnt;
    p_stats->bsr.qmgr.c2c_pkt = q_mgr_enq_debug_stats.drop_c2c_pkt_cnt;

    /*4. read net rx discard stats*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        sal_memset(&net_rx_debug_stats, 0, sizeof(net_rx_debug_stats));
        tbl_id = NetRxDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &net_rx_debug_stats);
        p_stats->netrx.data_err = net_rx_debug_stats.from_mac_mux_data_error_cnt;
        p_stats->netrx.no_buf = net_rx_debug_stats.from_int_lk_get_no_buf_error_cnt;
        p_stats->netrx.sop_err = net_rx_debug_stats.from_mac_mux_no_sop_error_cnt;
        p_stats->netrx.eop_err = net_rx_debug_stats.from_mac_mux_no_eop_error_cnt;
        p_stats->netrx.over_len = net_rx_debug_stats.from_mac_mux_overlen_error_cnt;
        p_stats->netrx.under_len = net_rx_debug_stats.from_int_lk_underlen_error_cnt;
        p_stats->netrx.pkt_drop = net_rx_debug_stats.from_mac_mux_pkt_drop_cnt;
    }


    /*5. read net tx discard stats*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        sal_memset(&net_tx_debug_stats, 0, sizeof(net_tx_debug_stats));
        tbl_id = NetTxDebugStats_t + slice;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, 0, cmd, &net_tx_debug_stats);

        p_stats->nettx[slice].epe_min_len_drop = net_tx_debug_stats.min_len_drop_cnt;
        p_stats->nettx[slice].info_drop = net_tx_debug_stats.info_drop_cnt;
        p_stats->nettx[slice].epe_no_buf_drop = net_tx_debug_stats.no_buf_drop_cnt;
        p_stats->nettx[slice].epe_no_eop_drop = net_tx_debug_stats.no_eop_drop_cnt;
        p_stats->nettx[slice].epe_no_sop_drop = net_tx_debug_stats.no_sop_drop_cnt;
    }

    /*6. read oam discard stats*/
    cmd = DRV_IOR(OamProcDebugStats_t, OamProcDebugStats_TxMsgDropCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &value);
    p_stats->oam.hw_error = value;

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_discard_get_reason(uint8 lchip, const ctc_dkit_discard_stats_t* p_last_stats,
                                                                       const ctc_dkit_discard_stats_t* p_cur_stats, ctc_dkit_discard_info_t* p_discard_info)
{
    uint16 ipe_disacrd_reason_cnt = 0;
    uint8 i = 0;
    uint8 slice = 0;
    sal_time_t tv;
    char* p_time_str = NULL;

    DKITS_PTR_VALID_CHECK(p_last_stats);
    DKITS_PTR_VALID_CHECK(p_cur_stats);
    DKITS_PTR_VALID_CHECK(p_discard_info);

    /*get systime*/
    sal_time(&tv);
    p_time_str = sal_ctime(&tv);
    sal_strncpy(p_discard_info->systime_str, p_time_str, 50);

    /*1. check IPE discard*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        if (p_last_stats->ipe[slice].hw_discard.total != p_cur_stats->ipe[slice].hw_discard.total)
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_IPE);
            if (p_last_stats->ipe[slice].hw_discard.har_adj != p_cur_stats->ipe[slice].hw_discard.har_adj)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_IPE_HW_HAR_ADJ,
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
            if (p_last_stats->ipe[slice].hw_discard.intf_map != p_cur_stats->ipe[slice].hw_discard.intf_map)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_IPE_HW_INT_MAP,
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
            if (p_last_stats->ipe[slice].hw_discard.lkup_mgr != p_cur_stats->ipe[slice].hw_discard.lkup_mgr)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_IPE_HW_LKUP,
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
            if (p_last_stats->ipe[slice].hw_discard.pkt_proc != p_cur_stats->ipe[slice].hw_discard.pkt_proc)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_IPE_HW_PKT_PROC,
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
            else
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_IPE_HW_PKT_FWD,
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
        }
        for (i = 0; i < CTC_DKIT_IPE_DISCARD_TYPE_NUM; i++)
        {
            if (p_last_stats->ipe[slice].future_discard[i] != p_cur_stats->ipe[slice].future_discard[i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_IPE);
                ADD_DISCARD_REASON(p_discard_info, (i + CTC_DKIT_IPE_USER_ID_BINDING_DISCARD),
                                   CTC_DKIT_DISCARD_FLAG_IPE, CTC_DKIT_INVALID_PORT, slice);
                ipe_disacrd_reason_cnt++;
            }
        }
    }

    /*2. check BSR discard*/
    if(p_last_stats->bsr.qmgr.total !=p_cur_stats->bsr.qmgr.total)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
        if (p_last_stats->bsr.qmgr.critical_pkt != p_cur_stats->bsr.qmgr.critical_pkt)
        {
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_QMGR_CRITICAL,
                               CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, 0);
        }
        if (p_last_stats->bsr.qmgr.c2c_pkt != p_cur_stats->bsr.qmgr.c2c_pkt)
        {
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_QMGR_C2C,
                               CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, 0);
        }
        if (p_last_stats->bsr.qmgr.enq_discard != p_cur_stats->bsr.qmgr.enq_discard)
        {
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_QMGR_EN_Q,
                               CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, 0);
        }
        if (p_last_stats->bsr.qmgr.egr_resrc_mgr != p_cur_stats->bsr.qmgr.egr_resrc_mgr)
        {
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_QMGR_EGR_RESRC,
                               CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, 0);
        }
        else
        {
            ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_QMGR_OTHER,
                               CTC_DKIT_DISCARD_FLAG_BSR, CTC_DKIT_INVALID_PORT, 0);
        }
    }

     for (i = 0; i < CTC_DKIT_CHANNEL_NUM; i++)
     {
         if(p_last_stats->bsr.bufstore[i].silent_total != p_cur_stats->bsr.bufstore[i].silent_total)
         {
             DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);
             if (p_last_stats->bsr.bufstore[i].silent_data_error  != p_cur_stats->bsr.bufstore[i].silent_data_error)
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_DATA_ERR,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             if (p_last_stats->bsr.bufstore[i].silent_chip_mismatch  != p_cur_stats->bsr.bufstore[i].silent_chip_mismatch)
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_CHIP_MISMATCH,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             if (p_last_stats->bsr.bufstore[i].silent_no_buf  != p_cur_stats->bsr.bufstore[i].silent_no_buf)
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_NO_BUFF,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             else
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_SILENT,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
         }
         if (p_last_stats->bsr.bufstore[i].abort_total != p_cur_stats->bsr.bufstore[i].abort_total)
         {
             DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_BSR);

             if ((p_last_stats->bsr.bufstore[i].abort_over_len_error != p_cur_stats->bsr.bufstore[i].abort_over_len_error)
                 || (p_last_stats->bsr.bufstore[i].abort_under_len != p_cur_stats->bsr.bufstore[i].abort_under_len))
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_LEN_ERROR,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             if (p_last_stats->bsr.bufstore[i].abort_no_buf != p_cur_stats->bsr.bufstore[i].abort_no_buf)
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_NO_BUFF,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             if ((p_last_stats->bsr.bufstore[i].abort_data_error != p_cur_stats->bsr.bufstore[i].abort_data_error)
                 || (p_last_stats->bsr.bufstore[i].abort_frame_error != p_cur_stats->bsr.bufstore[i].abort_frame_error))
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_DATA_ERR,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
             else
             {
                 ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_BSR_BUFSTORE_HW_ABORT,
                                    CTC_DKIT_DISCARD_FLAG_BSR, i, 0);
             }
         }
    }

    /*3. check EPE discard*/
    for (slice = 0; slice < CTC_DKIT_SLICE_NUM; slice++)
    {
        if (p_last_stats->epe[slice].hw_discard.total != p_cur_stats->epe[slice].hw_discard.total)
        {
            DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_EPE);
            if (p_last_stats->epe[slice].hw_discard.har_adj != p_cur_stats->epe[slice].hw_discard.har_adj)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_EPE_HW_HAR_ADJ,
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
            if (p_last_stats->epe[slice].hw_discard.next_hop != p_cur_stats->epe[slice].hw_discard.next_hop)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_EPE_HW_NEXT_HOP,
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
            if (p_last_stats->epe[slice].hw_discard.acl_qos != p_cur_stats->epe[slice].hw_discard.acl_qos)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_EPE_HW_ACL_QOS,
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
            if (p_last_stats->epe[slice].hw_discard.oam != p_cur_stats->epe[slice].hw_discard.oam)
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_EPE_HW_OAM,
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
            else
            {
                ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_EPE_HW_EDIT,
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
        }
        for (i = 0; i < CTC_DKIT_EPE_DISCARD_TYPE_NUM; i++)
        {
            if (p_last_stats->epe[slice].future_discard[i] != p_cur_stats->epe[slice].future_discard[i])
            {
                DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_EPE);
                ADD_DISCARD_REASON(p_discard_info, (i + CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD),
                                   CTC_DKIT_DISCARD_FLAG_EPE, CTC_DKIT_INVALID_PORT, slice);
            }
        }
    }
    /*4. check net rx discard stats*/
    if ((p_last_stats->netrx.data_err != p_cur_stats->netrx.data_err)
        ||(p_last_stats->netrx.pkt_drop != p_cur_stats->netrx.pkt_drop))
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_PKT_ERROR,
                           CTC_DKIT_DISCARD_FLAG_NET_RX, CTC_DKIT_INVALID_PORT, 0);
    }
    if (p_last_stats->netrx.no_buf != p_cur_stats->netrx.no_buf)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_NO_BUFFER,
                           CTC_DKIT_DISCARD_FLAG_NET_RX, CTC_DKIT_INVALID_PORT, 0);
    }
    if ((p_last_stats->netrx.over_len != p_cur_stats->netrx.over_len)
        ||(p_last_stats->netrx.under_len != p_cur_stats->netrx.under_len))
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_LEN_ERROR,
                           CTC_DKIT_DISCARD_FLAG_NET_RX, CTC_DKIT_INVALID_PORT, 0);
    }
    if ((p_last_stats->netrx.sop_err != p_cur_stats->netrx.sop_err)
        ||(p_last_stats->netrx.eop_err != p_cur_stats->netrx.eop_err))
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_RX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETRX_SOP_EOP,
                           CTC_DKIT_DISCARD_FLAG_NET_RX, CTC_DKIT_INVALID_PORT, 0);
    }


    /*5. check net tx discard stats*/
    if (p_last_stats->nettx[0].epe_min_len_drop != p_cur_stats->nettx[0].epe_min_len_drop)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETTX_MIN_LEN,
                           CTC_DKIT_DISCARD_FLAG_NET_TX, CTC_DKIT_INVALID_PORT, 0);
    }
    if (p_last_stats->nettx[0].epe_no_buf_drop != p_cur_stats->nettx[0].epe_no_buf_drop)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETTX_NO_BUFFER,
                           CTC_DKIT_DISCARD_FLAG_NET_TX, CTC_DKIT_INVALID_PORT, 0);
    }
    if ((p_last_stats->nettx[0].epe_no_eop_drop != p_cur_stats->nettx[0].epe_no_eop_drop)
      || (p_last_stats->nettx[0].epe_no_sop_drop != p_cur_stats->nettx[0].epe_no_sop_drop))
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETTX_SOP_EOP,
                           CTC_DKIT_DISCARD_FLAG_NET_TX, CTC_DKIT_INVALID_PORT, 0);
    }
    if (p_last_stats->nettx[0].info_drop != p_cur_stats->nettx[0].info_drop)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_NET_TX);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_NETTX_PI_ERROR,
            CTC_DKIT_DISCARD_FLAG_NET_TX, CTC_DKIT_INVALID_PORT, 0);
    }

    /*6. check oam discard stats*/
    if (p_last_stats->oam.hw_error != p_cur_stats->oam.hw_error)
    {
        DKITS_SET_FLAG(p_discard_info->discard_flag, CTC_DKIT_DISCARD_FLAG_OAM);
        ADD_DISCARD_REASON(p_discard_info, CTC_DKIT_OAM_HW_ERROR,
                           CTC_DKIT_DISCARD_FLAG_OAM, CTC_DKIT_INVALID_PORT, 0);
    }

    return CLI_SUCCESS;
}

#if 0
STATIC int32
_ctc_greatbelt_dkit_discard_get_sub_reason(ctc_dkit_discard_para_t* p_discard_para, ctc_dkit_discard_info_t* p_discard_info)
{
    int32 ret = CLI_SUCCESS;

    DKITS_PTR_VALID_CHECK(p_discard_info);
    DKITS_PTR_VALID_CHECK(p_discard_para);

    if (p_discard_info->discard_flag)
    {

    }

    return ret;
}
#endif

int32
ctc_greatbelt_dkit_discard_process(void* p_para)
{
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    ctc_dkit_discard_stats_t* cur_stats = NULL;
    ctc_dkit_discard_info_t* discard_info = NULL;
    sal_file_t p_wf = p_discard_para->p_wf;
    uint8 front = 0;
    uint8 lchip = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_discard_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);
    DKITS_PTR_VALID_CHECK(g_gb_dkit_master[lchip]);

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
        front = g_gb_dkit_master[lchip]->discard_history.front;
        while (g_gb_dkit_master[lchip]->discard_history.rear != front)
        {
            _ctc_greatbelt_dkit_discard_display(&(g_gb_dkit_master[lchip]->discard_history.discard[front++]), p_wf);
            front %= CTC_DKIT_DISCARD_HISTORY_NUM;
        }
    }
    else
    {
        /*1. read discard stats*/
        _ctc_greatbelt_dkit_discard_get_stats(lchip, cur_stats, 0);

        /*2. compare discard stats and get reason id*/
        _ctc_greatbelt_dkit_discard_get_reason(lchip, &(g_gb_dkit_master[lchip]->discard_stats), cur_stats,  discard_info);
        sal_memcpy(&(g_gb_dkit_master[lchip]->discard_stats), cur_stats, sizeof(ctc_dkit_discard_stats_t));
#if 0
        if (discard_info->discard_flag)
        {
            /*if some discard occured, analyse discard reason from bus and captured info*/
            _ctc_greatbelt_dkit_discard_get_sub_reason(p_discard_para, discard_info);
        }
#endif
        /*3. display result*/
        _ctc_greatbelt_dkit_discard_display(discard_info, p_wf);

        /*4. store result history*/
        if (discard_info->discard_flag != 0)
        {
            sal_memcpy(&(g_gb_dkit_master[lchip]->discard_history.discard[g_gb_dkit_master[lchip]->discard_history.rear++]),
                       discard_info, sizeof(ctc_dkit_discard_info_t));
            g_gb_dkit_master[lchip]->discard_history.rear %= CTC_DKIT_DISCARD_HISTORY_NUM;
            if (g_gb_dkit_master[lchip]->discard_history.rear == g_gb_dkit_master[lchip]->discard_history.front)
            {
                g_gb_dkit_master[lchip]->discard_history.front++;
                g_gb_dkit_master[lchip]->discard_history.front %= CTC_DKIT_DISCARD_HISTORY_NUM;
            }
        }
    }

    mem_free(cur_stats);
    mem_free(discard_info);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_discard_print_type(uint32 reason_id, char* module)
{
    char buff[256] = {0};
    sal_sprintf(buff, "%s", ctc_greatbelt_dkit_get_reason_desc(reason_id));
    CTC_DKIT_PRINT(" %-10d %-9s %s\n", reason_id, module, buff);
    return CLI_SUCCESS;
}


int32
ctc_greatbelt_dkit_discard_show_type(void* p_para)
{
    uint32 i = 0;
    ctc_dkit_discard_para_t* p_discard_para = (ctc_dkit_discard_para_t *)p_para;
    char* module = "  -";


    DKITS_PTR_VALID_CHECK(p_para);

    CTC_DKIT_PRINT(" %-10s %-9s %s\n", "ReasonId", "Module", "Description");
    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    if (0xFFFFFFFF == p_discard_para->reason_id) /*all*/
    {
        for (i = CTC_DKIT_IPE_USER_ID_BINDING_DISCARD; i < CTC_DKIT_IPE_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "IPE");
        }
        for (i = CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD;i < CTC_DKIT_EPE_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "EPE");
        }
        for (i = CTC_DKIT_BSR_BUFSTORE_HW_ABORT; i < CTC_DKIT_BSR_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "BSR");
        }
        for (i = CTC_DKIT_NETRX_NO_BUFFER; i < CTC_DKIT_NETRX_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "NETRX");
        }
        for (i = CTC_DKIT_NETTX_MIN_LEN; i < CTC_DKIT_NETTX_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "NETTX");
        }
        for (i = CTC_DKIT_OAM_HW_ERROR; i < CTC_DKIT_OAM_MAX; i++)
        {
            _ctc_greatbelt_dkit_discard_print_type(i, "OAM");
        }
    }
    else
    {
        if ((p_discard_para->reason_id >= CTC_DKIT_IPE_USER_ID_BINDING_DISCARD) && (p_discard_para->reason_id < CTC_DKIT_IPE_MAX))
        {
            module = "IPE";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_EPE_EPE_HDR_ADJ_DEST_ID_DISCARD) && (p_discard_para->reason_id < CTC_DKIT_EPE_MAX))
        {
            module = "EPE";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_BSR_BUFSTORE_HW_ABORT) && (p_discard_para->reason_id < CTC_DKIT_BSR_MAX))
        {
            module = "BSR";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_NETRX_NO_BUFFER) && (p_discard_para->reason_id < CTC_DKIT_NETRX_MAX))
        {
            module = "NETRX";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_NETTX_MIN_LEN) && (p_discard_para->reason_id < CTC_DKIT_NETTX_MAX))
        {
            module = "NETTX";
        }
        else if ((p_discard_para->reason_id >= CTC_DKIT_OAM_HW_ERROR) && (p_discard_para->reason_id < CTC_DKIT_OAM_MAX))
        {
            module = "OAM";
        }
        _ctc_greatbelt_dkit_discard_print_type(p_discard_para->reason_id, module);
    }
    CTC_DKIT_PRINT("--------------------------------------------------------------------------------\n");

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dkit_discard_display_en(void* p_para)
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
            sal_memset(g_gb_dkit_master[lchip]->discard_enable_bitmap, 0xFF , ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);
        }
        else
        {
            sal_memset(g_gb_dkit_master[lchip]->discard_enable_bitmap, 0 , ((CTC_DKIT_DISCARD_MAX / 32) + 1)*4);
        }
    }
    else
    {
        disacrd_bitmap = (((uint32*)g_gb_dkit_master[lchip]->discard_enable_bitmap) + (p_discard_para->reason_id / 32));
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



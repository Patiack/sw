#include "greatbelt/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_path.h"


enum ctc_dkit_path_module_e
{
    CTC_DKIT_DROP_IN_MAC,
    CTC_DKIT_DROP_IN_NETRX,
    CTC_DKIT_DROP_IN_IPE_HDADJ,
    CTC_DKIT_DROP_IN_BUF_STORE,
    CTC_DKIT_DROP_IN_METFIFO,
    CTC_DKIT_DROP_IN_QMRG,
    CTC_DKIT_DROP_IN_BUF_RETRV,
    CTC_DKIT_DROP_IN_EPE,
    CTC_DKIT_DROP_IN_NET_TX,

    CTC_DKIT_MAX_DROP_CHECK_POINT
};
typedef enum ctc_dkit_path_module_e ctc_dkit_path_module_t;


extern ctc_dkit_master_t* g_gb_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM];

int32
_ctc_greatbelt_dkit_path_packet_flow_stats_process(uint8 lchip, uint32* drop_flag)
{
    uint32 cmd = 0;
    ctc_dkit_path_stats_info_t ctc_dkit_path_stats_info;

    sal_memset(&ctc_dkit_path_stats_info, 0, sizeof(ctc_dkit_path_stats_info_t));

    /* 1. check net rx stats */
    cmd = DRV_IOR(NetRxDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.net_rx_stats);

    if ((ctc_dkit_path_stats_info.net_rx_stats.from_mac_mux_sop_cnt != g_gb_dkit_master[lchip]->path_stats.net_rx_stats.from_mac_mux_sop_cnt) &&
        (ctc_dkit_path_stats_info.net_rx_stats.from_mac_mux_eop_cnt !=  g_gb_dkit_master[lchip]->path_stats.net_rx_stats.from_mac_mux_eop_cnt) &&
        (ctc_dkit_path_stats_info.net_rx_stats.from_mac_mux_pkt_good_cnt != g_gb_dkit_master[lchip]->path_stats.net_rx_stats.from_mac_mux_pkt_good_cnt))
    {
           /*mac not drop */
    }
    else
    {
        /* drop in mac */
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_MAC);
        goto END;
    }

    /* 2. check ipe headadj stats */
    cmd = DRV_IOR(IpeHdrAdjDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.ipe_headadj_stats);

    if ((ctc_dkit_path_stats_info.ipe_headadj_stats.fr_net_rx_sop_cnt !=  g_gb_dkit_master[lchip]->path_stats.ipe_headadj_stats.fr_net_rx_sop_cnt) &&
        (ctc_dkit_path_stats_info.ipe_headadj_stats.fr_net_rx_eop_cnt != g_gb_dkit_master[lchip]->path_stats.ipe_headadj_stats.fr_net_rx_eop_cnt))
    {
           /*net rx not drop */
    }
    else
    {
        /* drop in net rx */
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_NETRX);
        goto END;
    }

    /* 3. check buffer store stats */
    cmd = DRV_IOR(BufStoreInputDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.bufstr_input_stats);

    if ((ctc_dkit_path_stats_info.bufstr_input_stats.fr_ipe_hdr_adj_sop_cnt != g_gb_dkit_master[lchip]->path_stats.bufstr_input_stats.fr_ipe_hdr_adj_sop_cnt) &&
        (ctc_dkit_path_stats_info.bufstr_input_stats.fr_ipe_hdr_adj_eop_cnt !=  g_gb_dkit_master[lchip]->path_stats.bufstr_input_stats.fr_ipe_hdr_adj_eop_cnt) &&
        (ctc_dkit_path_stats_info.bufstr_input_stats.fr_ipe_fwd_valid_cnt != g_gb_dkit_master[lchip]->path_stats.bufstr_input_stats.fr_ipe_fwd_valid_cnt))
    {
           /*ipe not drop */
    }
    else
    {
        /* drop in ipe */
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_IPE_HDADJ);
        goto END;
    }

    /* 4. check metfifo stats */
    cmd = DRV_IOR(MetFifoDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.met_fifo_stats);

    if (ctc_dkit_path_stats_info.met_fifo_stats.fr_bs_msg_cnt != g_gb_dkit_master[lchip]->path_stats.met_fifo_stats.fr_bs_msg_cnt)
    {
        /*buffer store have output */
    }
    else
    {
        /* drop in buffer store */
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_BUF_STORE);
        goto END;
    }

    if (ctc_dkit_path_stats_info.met_fifo_stats.to_q_mgr_enq_msg_cnt != g_gb_dkit_master[lchip]->path_stats.met_fifo_stats.to_q_mgr_enq_msg_cnt)
    {
           /*metfifo not drop */
    }
    else
    {
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_METFIFO);
        goto END;
    }

    /* 5. check buffer retrv stats */
    cmd = DRV_IOR(BufRetrvInputDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.bufrtv_input_stats);

    if (ctc_dkit_path_stats_info.bufrtv_input_stats.fr_q_mgr_msg_cnt != g_gb_dkit_master[lchip]->path_stats.bufrtv_input_stats.fr_q_mgr_msg_cnt)
    {
           /*qmanage not drop */
    }
    else
    {
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_QMRG);
        goto END;
    }

    cmd = DRV_IOR(BufRetrvOutputPktDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.bufrtv_output_stats);

    if ((ctc_dkit_path_stats_info.bufrtv_output_stats.to_epe_sop_cnt != g_gb_dkit_master[lchip]->path_stats.bufrtv_output_stats.to_epe_sop_cnt) &&
         (ctc_dkit_path_stats_info.bufrtv_output_stats.to_epe_eop_cnt != g_gb_dkit_master[lchip]->path_stats.bufrtv_output_stats.to_epe_eop_cnt))
    {
           /*bufrtv not drop */
    }
    else
    {
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_BUF_RETRV);
        goto END;
    }

    /* 6. check net tx stats */
    cmd = DRV_IOR(NetTxDebugStats_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &ctc_dkit_path_stats_info.net_tx_stats);

    if ((ctc_dkit_path_stats_info.net_tx_stats.fr_epe_sop_cnt != g_gb_dkit_master[lchip]->path_stats.net_tx_stats.fr_epe_sop_cnt) &&
        (ctc_dkit_path_stats_info.net_tx_stats.fr_epe_eop_cnt != g_gb_dkit_master[lchip]->path_stats.net_tx_stats.fr_epe_eop_cnt))
    {
           /*epe not drop */
    }
    else
    {
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_EPE);
        goto END;
    }

    if ((ctc_dkit_path_stats_info.net_tx_stats.to_net_sop_cnt != g_gb_dkit_master[lchip]->path_stats.net_tx_stats.to_net_sop_cnt) &&
         (ctc_dkit_path_stats_info.net_tx_stats.to_net_eop_cnt != g_gb_dkit_master[lchip]->path_stats.net_tx_stats.to_net_eop_cnt))
    {
           /*epe not drop */
    }
    else
    {
        *drop_flag |= (1<<CTC_DKIT_DROP_IN_NET_TX);
    }

END:
    sal_memcpy(&(g_gb_dkit_master[lchip]->path_stats), &ctc_dkit_path_stats_info, sizeof(ctc_dkit_path_stats_info_t));
    return DRV_E_NONE;
}

int32
_ctc_greatbelt_dkit_path_show_drop_info(ctc_dkit_path_module_t drop_id)
{
    switch(drop_id)
    {
        case CTC_DKIT_DROP_IN_MAC:
            CTC_DKIT_PRINT("Packet Drop In Mac Module! \n");
            break;
        case CTC_DKIT_DROP_IN_NETRX:
            CTC_DKIT_PRINT("Packet Drop In Net Rx Module! \n");
            break;
        case CTC_DKIT_DROP_IN_IPE_HDADJ:
            CTC_DKIT_PRINT("Packet Drop In IPE Module! \n");
            break;
        case CTC_DKIT_DROP_IN_BUF_STORE:
            CTC_DKIT_PRINT("Packet Drop In Buffer Store Module! \n");
            break;
        case CTC_DKIT_DROP_IN_METFIFO:
            CTC_DKIT_PRINT("Packet Drop In MetFifo Module! \n");
            break;
        case CTC_DKIT_DROP_IN_QMRG:
            CTC_DKIT_PRINT("Packet Drop In Qmgr Module! \n");
            break;
        case CTC_DKIT_DROP_IN_BUF_RETRV:
            CTC_DKIT_PRINT("Packet Drop In Buffer Retrv Module! \n");
            break;
        case CTC_DKIT_DROP_IN_EPE:
            CTC_DKIT_PRINT("Packet Drop In EPE Module! \n");
            break;
        case CTC_DKIT_DROP_IN_NET_TX:
            CTC_DKIT_PRINT("Packet Drop In Net Tx Module! \n");
            break;

        default:
            return DRV_E_EXCEED_MAX_SIZE;
    }

    return DRV_E_NONE;
}


int32
ctc_greatbelt_dkit_path_process(void* p_para)
{
    ctc_dkit_path_para_t* p_path_para = (ctc_dkit_path_para_t*)p_para;
    sal_time_t tv;
    uint32 drop_flag = 0;
    uint8 index = 0;
    uint8 lchip = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_path_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);
    DKITS_PTR_VALID_CHECK(g_gb_dkit_master[lchip]);


    /*get systime*/
    sal_time(&tv);
    CTC_DKIT_PRINT("\n");
    CTC_DKIT_PRINT("TIME: %s", sal_ctime(&tv));
    CTC_DKIT_PRINT("================================================================\n");
    CTC_DKIT_PRINT("MACRX->NETRX->IPE->BUFSTORE->METFIFO->QMRG->BUFRETRV->EPE->NETTX\n");
    CTC_DKIT_PRINT("================================================================\n");

    _ctc_greatbelt_dkit_path_packet_flow_stats_process(lchip, &drop_flag);
    for (index = 0; index < CTC_DKIT_MAX_DROP_CHECK_POINT; index++)
    {
        if ((drop_flag >> index) & 0x01)
        {
            _ctc_greatbelt_dkit_path_show_drop_info(index);
        }
        else
        {
            continue;
        }
    }

    CTC_DKIT_PRINT("============================================================\n");

    return CLI_SUCCESS;
}



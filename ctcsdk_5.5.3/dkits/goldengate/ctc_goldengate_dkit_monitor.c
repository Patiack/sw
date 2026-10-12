#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_monitor.h"

#define CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM  7
#define CTC_DKIT_MONITOR_TC_NUM  8

extern ctc_dkit_master_t* g_gg_dkit_master[CTC_DKITS_MAX_LOCAL_CHIP_NUM];
STATIC uint8
_ctc_goldengate_dkit_monitor_get_congest_level(uint8 lchip, bool is_igress, uint8 sc)
{
    uint8 i = 0;
    uint32 total = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = MaxTblId_t;
    uint32 field_id = 0;

    if (is_igress)
    {
        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_scCnt0Slice0_f );
        DRV_IOCTL(lchip, 0, cmd, &value);
        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_scCnt0Slice1_f );
        DRV_IOCTL(lchip, 0, cmd, &total);
        total = total + value;
        tbl_id = IgrCongestLevelThrd_t;
        field_id = IgrCongestLevelThrd_sc0ThrdLvl0_f;
    }
    else
    {
        cmd = DRV_IOR(EgrScCnt_t, EgrScCnt_scCnt0_f + sc);
        DRV_IOCTL(lchip, 0, cmd, &total);
        tbl_id = EgrCongestLevelThrd_t;
        field_id = EgrCongestLevelThrd_sc0Thrd0_f + sc*CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM;
    }

    for (i = 0; i < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM; i++)
    {
        cmd = DRV_IOR(tbl_id, field_id + i);
        DRV_IOCTL(lchip, 0, cmd, &value);
        if ((total>>6) < value)
        {
            return i;
        }
    }

    return i;
}

STATIC uint32
_ctc_goldengate_dkit_monitor_get_channel_by_gport(uint8 lchip, uint16 gport)
{
    uint32 lport = 0;
    uint8 local_phy_port = 0;
    uint32 channel = 0xFFFFFFFF;
    uint8 slice = 0;
    uint8 i = 0;
    uint32 cmd = 0;
    uint32 value = 0;

    lport = CTC_DKIT_CTC_GPORT_TO_DRV_LPORT(gport);
    local_phy_port = lport % CTC_DKIT_ONE_SLICE_PORT_NUM;
    slice = (lport >= CTC_DKIT_ONE_SLICE_PORT_NUM)? 1 : 0;

    for (i = (CTC_DKIT_ONE_SLICE_CHANNEL_NUM *slice); i < (CTC_DKIT_ONE_SLICE_CHANNEL_NUM + CTC_DKIT_ONE_SLICE_CHANNEL_NUM *slice); i++)
    {
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
        DRV_IOCTL(lchip, i, cmd, &value);
        if (value == local_phy_port)
        {
            channel = i;
            break;
        }
    }

    return channel;
}

int32
ctc_goldengate_dkit_monitor_show_queue_id(void* p_para)
{
    uint32 cmd = 0;
    uint32 qid = 0;
    uint32 chanid = 0;
    uint8 i = 0;
    uint32 tbl_idx = 0;
    uint8 slice = 0;
    uint32 local_phy_port = 0;
    uint16 gport  = 0;
    uint8 gchip = 0;
    uint8 lchip = 0;
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_monitor_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);

    CTC_DKIT_GET_GCHIP(lchip, gchip);
    CTC_DKIT_PRINT("%-6s %-10s %-10s %-10s\n", "NO.", "QueID", "ChanId","Port");
    CTC_DKIT_PRINT("-----------------------------------\n");
    for (i = 0; i < 8; i++)
    {
        cmd = DRV_IOR(QMgrQueueIdMon_t, QMgrQueueIdMon_queueIdMon0_f + i);
        DRV_IOCTL(lchip, 0, cmd, &qid);
        cmd = DRV_IOR(RaGrpMap_t, RaGrpMap_chanId_f);
        DRV_IOCTL(lchip, (qid >> 3), cmd, &chanid);

        slice = DKITS_IS_BIT_SET(qid, 10);
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
        tbl_idx = chanid + (CTC_DKIT_ONE_SLICE_CHANNEL_NUM *slice);
        DRV_IOCTL(lchip, tbl_idx, cmd, &local_phy_port);
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));

        CTC_DKIT_PRINT("%-6d %-10d %-10d 0x%04x\n", i + 1, qid, tbl_idx, gport);
    }

    CTC_DKIT_PRINT("-----------------------------------\n");
    CTC_DKIT_PRINT("Tips: NO.1 is the newest QueueID\n\n");

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_igr_summary(uint8 lchip, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint8 i = 0;
    uint32 value = 0;
    uint32 value0 = 0;
    uint32 value1 = 0;
    char desc[16] = {0};
    uint32 total = 0;
    uint8 congest_level = 0;

    uint32 c2c_packet_cnt_slice0 = 0;
    uint32 c2c_packet_cnt_slice1 = 0;
    uint32 critical_packet_cntSlice0 = 0;
    uint32 critical_packet_cntSlice1 = 0;
    uint32 igr_total_cnt_slice0 = 0;
    uint32 igr_total_cnt_slice1 = 0;
    BufStoreTotalResrcInfo_m buf_store_total_resrc_info;

    uint32 c2c_packet_thrd = 0;
    uint32 critical_packet_thrd = 0;
    uint32 igr_total_thrd = 0;
    IgrResrcMgrMiscCtl_m igr_resrc_mgr_misc_ctl;

    sal_memset(&buf_store_total_resrc_info, 0, sizeof(BufStoreTotalResrcInfo_m));
    cmd = DRV_IOR(BufStoreTotalResrcInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &buf_store_total_resrc_info);
    GetBufStoreTotalResrcInfo(A, c2cPacketCntSlice0_f, &buf_store_total_resrc_info, &c2c_packet_cnt_slice0);
    GetBufStoreTotalResrcInfo(A, c2cPacketCntSlice1_f, &buf_store_total_resrc_info, &c2c_packet_cnt_slice1);
    GetBufStoreTotalResrcInfo(A, criticalPacketCntSlice0_f, &buf_store_total_resrc_info, &critical_packet_cntSlice0);
    GetBufStoreTotalResrcInfo(A, criticalPacketCntSlice1_f, &buf_store_total_resrc_info, &critical_packet_cntSlice1);
    GetBufStoreTotalResrcInfo(A, igrTotalCntSlice0_f, &buf_store_total_resrc_info, &igr_total_cnt_slice0);
    GetBufStoreTotalResrcInfo(A, igrTotalCntSlice1_f, &buf_store_total_resrc_info, &igr_total_cnt_slice1);
    total = igr_total_cnt_slice0 + igr_total_cnt_slice1+c2c_packet_cnt_slice0 + c2c_packet_cnt_slice1;

    sal_memset(&igr_resrc_mgr_misc_ctl, 0, sizeof(IgrResrcMgrMiscCtl_m));
    cmd = DRV_IOR(IgrResrcMgrMiscCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &igr_resrc_mgr_misc_ctl);
    GetIgrResrcMgrMiscCtl(A, c2cPacketThrd_f, &igr_resrc_mgr_misc_ctl, &c2c_packet_thrd);
    GetIgrResrcMgrMiscCtl(A, criticalPacketThrd_f, &igr_resrc_mgr_misc_ctl, &critical_packet_thrd);
    GetIgrResrcMgrMiscCtl(A, igrTotalThrd_f, &igr_resrc_mgr_misc_ctl, &igr_total_thrd);

    congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    CTC_DKITS_PRINT_FILE(p_wf, "\n----------congest config-----------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s\n", "level", "Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
    for (i = 0; i < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM; i++)
    {
        cmd = DRV_IOR(IgrCongestLevelThrd_t, IgrCongestLevelThrd_sc0ThrdLvl0_f + i);
        DRV_IOCTL(lchip, 0, cmd, &value0);
        if (i == congest_level)
        {
            sal_sprintf(desc, "%d%s", i, " (*)");
        }
        else
        {
            sal_sprintf(desc, "%d", i);
        }
        CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d\n", desc, value0<<6);
    }

    CTC_DKITS_PRINT_FILE(p_wf, "\n-----------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s %-10s\n", "Type", "Depth", "Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Total", total, igr_total_thrd+c2c_packet_thrd);

     if (((igr_total_cnt_slice0 + igr_total_cnt_slice1) >= (critical_packet_cntSlice0 + critical_packet_cntSlice1))
         && (igr_total_thrd >= critical_packet_thrd))
     {
         CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Normal",
                        (igr_total_cnt_slice0 + igr_total_cnt_slice1 - critical_packet_cntSlice0 - critical_packet_cntSlice1),
                        igr_total_thrd - critical_packet_thrd);
     }
    for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
    {
        cmd = DRV_IOR(IgrTcCnt_t, IgrTcCnt_tcCnt0Slice0_f + i*2);
        DRV_IOCTL(lchip, 0, cmd, &value0);
        cmd = DRV_IOR(IgrTcCnt_t, IgrTcCnt_tcCnt0Slice0_f + i*2 + 1);
        DRV_IOCTL(lchip, 0, cmd, &value1);
        sal_sprintf(desc,"TC%d",i);

        cmd = DRV_IOR(IgrTcThrd_t, IgrTcThrd_g_0_tcThrd_f + i);
        DRV_IOCTL(lchip, congest_level, cmd, &value);

        CTC_DKITS_PRINT_FILE(p_wf, "   --%-9s %-10d %-10d \n", desc, (value0 + value1), (value<<4));
    }
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Critical", (critical_packet_cntSlice0 + critical_packet_cntSlice1), critical_packet_thrd);
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "C2C", (c2c_packet_cnt_slice0 + c2c_packet_cnt_slice1), c2c_packet_thrd);
    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_igr_detail_port_tc(uint8 lchip, uint16 channel, sal_file_t p_wf)
{
    uint8 i = 0;
    char desc[16] = {0};
    uint32 depth = 0 ;
    uint32 index= 0;
    uint32 index1= 0;
    uint8 slice = 0;
    uint32 cmd = 0;
    uint8 congest_level = 0;
    uint32 profid_high = 0;
    uint32 port_tc_thrd = 0;

    congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s\n", "TC",  "Depth", "Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
    slice = (channel >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)? 1 : 0;
    index =(channel%CTC_DKIT_ONE_SLICE_CHANNEL_NUM)*8 + slice*(TABLE_MAX_INDEX(DsIgrPortTcCnt_t) / 2);
    index1 =(channel%CTC_DKIT_ONE_SLICE_CHANNEL_NUM) + slice*(TABLE_MAX_INDEX(DsIgrPortTcThrdProfId_t) / 2);
    for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
    {
        cmd = DRV_IOR(DsIgrPortTcCnt_t, DsIgrPortTcCnt_portTcCnt_f);
        DRV_IOCTL(lchip, index + i, cmd, &depth);

        cmd = DRV_IOR(DsIgrPortTcThrdProfId_t, i);
        DRV_IOCTL(lchip, index1, cmd, &profid_high);

        cmd = DRV_IOR(DsIgrPortTcThrdProfile_t, DsIgrPortTcThrdProfile_portTcThrd_f);
        DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_tc_thrd);

        sal_sprintf(desc, "TC%d", i);
        CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10d%-10d\n", desc,  depth, port_tc_thrd);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_igr_detail(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 channel = 0;
    uint32 index = 0;
    uint32 depth = 0;
    uint32 local_phy_port = 0;
    uint32 gchip = 0;
    uint32 i = 0;
    uint8 slice = 0;
    uint8 congest_level = 0;
    uint32 profid_high = 0;
    uint32 port_thrd = 0;
    bool b_all_zero = TRUE;


    congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    if (0xFFFF == gport)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
        CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s%-10s\n",
                       "Port", "Channel", "Depth", "Thrd");
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
        for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM*2; i++)
        {
            slice = (i >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)? 1 : 0;
            cmd = DRV_IOR(DsIgrPortCnt_t, DsIgrPortCnt_portCnt_f);
            DRV_IOCTL(lchip, i % CTC_DKIT_ONE_SLICE_CHANNEL_NUM + (slice*(TABLE_MAX_INDEX(DsIgrPortCnt_t) / 2)), cmd, &depth);
            if (depth)
            {
                cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
                DRV_IOCTL(lchip, i, cmd, &local_phy_port);
                CTC_DKIT_GET_GCHIP(lchip, gchip);

                cmd = DRV_IOR(DsIgrPortThrdProfId_t, i % 8);
                DRV_IOCTL(lchip, i % CTC_DKIT_ONE_SLICE_CHANNEL_NUM / 8 + (slice*(TABLE_MAX_INDEX(DsIgrPortThrdProfId_t) / 2)), cmd, &profid_high);

                cmd = DRV_IOR(DsIgrPortThrdProfile_t, DsIgrPortThrdProfile_portThrd_f);
                DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "0x%04x    %-10d%-10d%-10d\n",
                               CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM),
                               i + slice*CTC_DKIT_ONE_SLICE_CHANNEL_NUM, depth, (port_thrd<<4));
                b_all_zero = FALSE;
            }

        }
        if (b_all_zero)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "All port depth is zero!!!\n");
        }
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
    }
    else
    {
        channel = _ctc_goldengate_dkit_monitor_get_channel_by_gport(lchip, gport);
        if (channel != 0xFFFFFFFF)
        {
            slice = (channel >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)? 1 : 0;
            cmd = DRV_IOR(DsIgrPortCnt_t, DsIgrPortCnt_portCnt_f);
            if (channel >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)
            {
                index = channel % CTC_DKIT_ONE_SLICE_CHANNEL_NUM + TABLE_MAX_INDEX(DsIgrPortCnt_t) / 2;
            }
            else
            {
                index = channel;
            }
            DRV_IOCTL(lchip, index, cmd, &depth);

            cmd = DRV_IOR(DsIgrPortThrdProfId_t, channel % CTC_DKIT_MONITOR_TC_NUM);
            DRV_IOCTL(lchip, channel % CTC_DKIT_ONE_SLICE_CHANNEL_NUM / CTC_DKIT_MONITOR_TC_NUM + (slice*(TABLE_MAX_INDEX(DsIgrPortThrdProfId_t) / 2)), cmd, &profid_high);

            cmd = DRV_IOR(DsIgrPortThrdProfile_t, DsIgrPortThrdProfile_portThrd_f);
            DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_thrd);

            CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s%-10s\n", "Port", "Channel", "Depth", "Thrd");
            CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "0x%04x    %-10d%-10d%-10d\n", gport, channel, depth, (port_thrd<<4));

            _ctc_goldengate_dkit_monitor_q_depth_igr_detail_port_tc(lchip, channel, p_wf);

        }
        else
        {
            CTC_DKITS_PRINT_FILE(p_wf, "Invalid Gport:0x%x!!!\n", gport);
        }
    }


    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_igr(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 resrc_mgr_disable = 0;

    cmd = DRV_IOR(BufStoreMiscCtl_t, BufStoreMiscCtl_resrcMgrDisable_f);
    DRV_IOCTL(lchip, 0, cmd, &resrc_mgr_disable);
    if (resrc_mgr_disable)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "Ingress Resouce Mamager is Disable!!!\n");
        return CLI_SUCCESS;
    }

    if (0xFFFF == gport)
    {
        _ctc_goldengate_dkit_monitor_q_depth_igr_summary(lchip, p_wf);
    }
    _ctc_goldengate_dkit_monitor_q_depth_igr_detail(lchip, gport, p_wf);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_egr_summary(uint8 lchip, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint8 i = 0, j = 0;
    uint32 value = 0;
    uint8 congest_level = 0;
    char desc[16] = {0};

    uint32 totalCnt = 0;
    uint32 egr_total_thrd = 0;
    uint32 tc_cnt = 0;
    uint32 tc_thrd = 0;

    cmd = DRV_IOR(EgrTotalCnt_t, EgrTotalCnt_totalCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &totalCnt);
    cmd = DRV_IOR(EgrResrcMgrCtl_t, EgrResrcMgrCtl_egrTotalThrd_f);
    DRV_IOCTL(lchip, 0, cmd, &egr_total_thrd);

    for (j = 0; j <= 3; j++)
    {
        congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, FALSE, j);
        CTC_DKITS_PRINT_FILE(p_wf, "\n-------SC%d congest config---------\n", j);
        CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s\n", "level", "Thrd");
        CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
        for (i = 0; i < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM; i++)
        {
            cmd = DRV_IOR(EgrCongestLevelThrd_t, EgrCongestLevelThrd_sc0Thrd0_f + j*CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM + i);
            DRV_IOCTL(lchip, 0, cmd, &value);
            if (i == congest_level)
            {
                sal_sprintf(desc, "%d%s", i, " (*)");
            }
            else
            {
                sal_sprintf(desc, "%d", i);
            }
            CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d\n", desc, value<<6);

            if (congest_level < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM)
            {
                CTC_DKITS_PRINT_FILE(p_wf, "-------TC infomation for SC%d------\n", j);
                CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s %-10s\n", "Type", "Depth", "Drop-Thrd");
                CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
                CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Total", totalCnt, egr_total_thrd);

                for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
                {
                    cmd = DRV_IOR(EgrTcCnt_t, EgrTcCnt_tcCnt0_f + i);
                    DRV_IOCTL(lchip, 0, cmd, &tc_cnt);
                    sal_sprintf(desc, "TC%d", i);
                    cmd = DRV_IOR(EgrTcThrd_t, EgrTcThrd_tcThrd0_f + i);
                    DRV_IOCTL(lchip, congest_level, cmd, &tc_thrd);
                    CTC_DKITS_PRINT_FILE(p_wf, "   --%-9s %-10d %-10d \n", desc, tc_cnt, (tc_thrd << 4));
                }
            }

        }
    }



        return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_egr_detail_queue(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 channel = 0;
    uint32 chan_id = 0;
    uint32 que_inst_cnt = 0;
    uint32 i = 0, j = 0;
    uint32 q_id = 0;
    uint32 sc = 0;
    uint8 congest_level = 0;
    uint32 sc_thrd = 0;


    channel = _ctc_goldengate_dkit_monitor_get_channel_by_gport(lchip, gport);
    for (i = 0; i < TABLE_MAX_INDEX(RaGrpMap_t); i++)
    {

        if (((i >= 98) && (i <= 127)) || ((i >= 226) && (i <= 255)))/*!!!!!!!!!!!!!!!!!!*/
        {
            continue;
        }

        cmd = DRV_IOR(RaGrpMap_t, RaGrpMap_chanId_f);
        DRV_IOCTL(lchip, i , cmd, &chan_id);

        if (i >= (TABLE_MAX_INDEX(RaGrpMap_t) / 2))/*slice1*/
        {
            chan_id = chan_id + CTC_DKIT_ONE_SLICE_CHANNEL_NUM;
        }

        if (chan_id == channel)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "\n------------------QGroup%d---------------------\n", i);
            CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-6s%-10s%-10s\n", "Q-Id", "Depth", "SC", "SC-Level", "SC-Drop-Thrd");
            CTC_DKITS_PRINT_FILE(p_wf, "------------------------------------------------\n");
            for (j = 0; j < CTC_DKIT_MONITOR_TC_NUM; j++)
            {
                q_id = i*CTC_DKIT_MONITOR_TC_NUM + j;
                cmd = DRV_IOR(RaQueCnt_t, RaQueCnt_queInstCnt_f);
                DRV_IOCTL(lchip, q_id , cmd, &que_inst_cnt);
                cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_mappedSc_f);
                DRV_IOCTL(lchip, q_id , cmd, &sc);
                congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, FALSE, sc);
                cmd = DRV_IOR(EgrScThrd_t, EgrScThrd_scThrd0_f + sc);
                DRV_IOCTL(lchip, 0 , cmd, &sc_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "%-10d%-10d%-6d%-10d%-10d\n", q_id, que_inst_cnt, sc, congest_level, (sc_thrd<<4));
            }

        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_egr_detail(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 channel = 0;
    uint32 depth = 0;
    uint32 local_phy_port = 0;
    uint32 gchip = 0;
    uint32 i = 0;
    uint8 slice = 0;

    uint8 congest_level = 0;
    uint32 port_thrd = 0;
    uint32 drop_prof_id = 0;
    bool b_all_zero = TRUE;

    congest_level = _ctc_goldengate_dkit_monitor_get_congest_level(lchip, FALSE, 0);
    if (0xFFFF == gport)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
        CTC_DKITS_PRINT_FILE(p_wf, "%-12s%-12s%-12s\n",
                       "Port", "Channel", "Depth");
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
        for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM*2; i++)
        {
            slice = (i >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)? 1 : 0;
            cmd = DRV_IOR(RaEgrPortCnt_t, RaEgrPortCnt_portCnt_f);
            DRV_IOCTL(lchip, i , cmd, &depth);
            if (depth)
            {
                cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
                DRV_IOCTL(lchip, i, cmd, &local_phy_port);
                CTC_DKIT_GET_GCHIP(lchip, gchip);

                cmd = DRV_IOR(RaEgrPortCtl_t, RaEgrPortCtl_dropProfId_f);
                DRV_IOCTL(lchip, i, cmd, &drop_prof_id);

                cmd = DRV_IOR(RaEgrPortDropProfile_t, RaEgrPortDropProfile_portDropThrd_f);
                DRV_IOCTL(lchip, (((drop_prof_id&0x7) << 3) + congest_level), cmd, &port_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "0x%04x      %-12d%-12d\n",
                               CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM),
                               i + slice*CTC_DKIT_ONE_SLICE_CHANNEL_NUM, depth);
                b_all_zero = FALSE;
            }
        }
        if (b_all_zero)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "All port depth is zero!!!\n");
        }
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
    }
    else
    {
        channel = _ctc_goldengate_dkit_monitor_get_channel_by_gport(lchip, gport);
        if (channel != 0xFFFFFFFF)
        {
            slice = (channel >= CTC_DKIT_ONE_SLICE_CHANNEL_NUM)? 1 : 0;
            cmd = DRV_IOR(RaEgrPortCnt_t, RaEgrPortCnt_portCnt_f);
            DRV_IOCTL(lchip, channel, cmd, &depth);

            cmd = DRV_IOR(RaEgrPortCtl_t, RaEgrPortCtl_dropProfId_f);
            DRV_IOCTL(lchip, channel, cmd, &drop_prof_id);

            cmd = DRV_IOR(RaEgrPortDropProfile_t, RaEgrPortDropProfile_portDropThrd_f);
            DRV_IOCTL(lchip, (((drop_prof_id&0x7) << 3) + congest_level), cmd, &port_thrd);

            CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "%-12s%-12s%-12s\n", "Port", "Channel", "Depth");
            CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "0x%04x      %-12d%-12d\n", gport, channel, depth);

            _ctc_goldengate_dkit_monitor_q_depth_egr_detail_queue(lchip, channel, p_wf);
        }
        else
        {
            CTC_DKITS_PRINT_FILE(p_wf, "Invalid Gport:0x%x!!!\n", gport);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_q_depth_egr(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 egr_resrc_mgr_en = 0;

    cmd = DRV_IOR(EgrResrcMgrCtl_t, EgrResrcMgrCtl_egrResrcMgrEn_f);
    DRV_IOCTL(lchip, 0, cmd, &egr_resrc_mgr_en);
    if (0 == egr_resrc_mgr_en)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "Egress Resouce Mamager is Disable!!!\n");
        return CLI_SUCCESS;
    }

    if (0xFFFF == gport)
    {
        _ctc_goldengate_dkit_monitor_q_depth_egr_summary(lchip, p_wf);
    }
    _ctc_goldengate_dkit_monitor_q_depth_egr_detail(lchip, gport, p_wf);

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_monitor_show_queue_depth(void* p_para)
{
    uint32 entry_num = 0;
    uint32 cmd = 0;
    uint32 idx = 0;
    uint32 value = 0;
    bool b_all_zero = TRUE;
    uint8 lchip = 0;
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;
    sal_file_t p_wf = p_monitor_para->p_wf;

    DKITS_PTR_VALID_CHECK(p_para);
    lchip = p_monitor_para->lchip;
    CTC_DKIT_LCHIP_CHECK(lchip);

    if (CTC_DKIT_INGRESS == p_monitor_para->dir)
    {
        _ctc_goldengate_dkit_monitor_q_depth_igr(lchip, p_monitor_para->gport, p_wf);
    }
    else if(CTC_DKIT_EGRESS == p_monitor_para->dir)
    {
        _ctc_goldengate_dkit_monitor_q_depth_egr(lchip, p_monitor_para->gport, p_wf);
    }
    else
    {
        CTC_DKITS_PRINT_FILE(p_wf, "%-20s %-10s\n", "QueueID", "Depth(unit:288B)");
        CTC_DKITS_PRINT_FILE(p_wf, "---------------------------------------------\n");

        entry_num = TABLE_MAX_INDEX(RaQueCnt_t);
        cmd = DRV_IOR(RaQueCnt_t, RaQueCnt_queInstCnt_f);
        for (idx = 0; idx < entry_num; idx++)
        {

            DRV_IOCTL(lchip, idx, cmd, &value);
            if (0 != value)
            {
                CTC_DKITS_PRINT_FILE(p_wf, "%-20d %-10d\n", idx, value);
                b_all_zero = FALSE;
            }
        }
        if (b_all_zero)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "All Queue depth is zero!!!\n");
        }
        CTC_DKITS_PRINT_FILE(p_wf, "---------------------------------------------\n");
    }
    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_monitor_get_temperature(uint8 lchip, uint32* temperature)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 timeout = 10;

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
            *temperature = (value & 0xff) - 118;
        }
        else
        {
            *temperature = 118 - (value & 0xff) + (1 << 31);
        }
    }
    else
    {
        *temperature = 0xFFFFFFFF;
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_monitor_show_sensor_temperature(uint8 lchip)
{
    uint32 temp_value = 0;

    _ctc_goldengate_dkit_monitor_get_temperature(lchip, &temp_value);

    if (temp_value != 0xFFFFFFFF)
    {
        if (DKITS_IS_BIT_SET(temp_value, 31))
        {
            CTC_DKIT_PRINT("t = -%d\n", temp_value&0x7FFFFFFF);
        }
        else
        {
            CTC_DKIT_PRINT("t = %d\n", temp_value);
        }
    }
    else
    {
        CTC_DKIT_PRINT("Read temperature fail!!!\n");
    }

    return CLI_SUCCESS;
}


STATIC int32
_ctc_goldengate_dkit_monitor_show_sensor_voltage(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 timeout = 10;

    value = 0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgSleepSen_f);
    DRV_IOCTL(lchip, 0, cmd, &value);

    value = 1;
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
    while ((value == 0) && (--timeout))
    {
        DRV_IOCTL(lchip, 0, cmd, &value);
        sal_task_sleep(100);
    }

    if (value == 1)
    {
        cmd = DRV_IOR(TvSensorMon_t, TvSensorMon_monOutputSen_f);
        DRV_IOCTL(lchip, 0, cmd, &value);
        value = (value*38 +5136)/10;
        CTC_DKIT_PRINT("Voltage: %dmV\n", value);
    }
    else
    {
        CTC_DKIT_PRINT("Read Voltage timeout. \n");
    }

    return CLI_SUCCESS;
}


STATIC void
_ctc_goldengate_dkit_monitor_temperature_handler(void* arg)
{
    sal_time_t tv;
    char* p_time_str = NULL;
    sal_file_t p_file = NULL;
    uint32 temperature = 0;
    ctc_dkit_monitor_para_t* para = (ctc_dkit_monitor_para_t*)arg;
    uint8 lchip = para->lchip;


    if (para->log)
    {
        p_file = sal_fopen(para->str, "wt");
        if (!p_file)
        {
            CTC_DKIT_PRINT("Open file %s fail!!!\n", para->str);
            return;
        }
        sal_fclose(p_file);
        p_file = NULL;
    }

    while (1)
    {
        if (para->log && (NULL == p_file))
        {
            p_file = sal_fopen(para->str, "a");
        }
        /*get systime*/
        sal_time(&tv);
        p_time_str = sal_ctime(&tv);
        _ctc_goldengate_dkit_monitor_get_temperature(lchip, &temperature);
        if (temperature == 0xFFFFFFFF)
        {
            CTC_DKITS_PRINT_FILE(p_file, "Read temperature fail!!\n");
        }
        else if (temperature >= para->temperature)
        {
            CTC_DKITS_PRINT_FILE(p_file, "t = %-4d, %s", temperature, p_time_str);
        }

        if ((temperature >= para->power_off_temp)&&(temperature != 0xFFFFFFFF)) /*power off*/
        {

            CTC_DKITS_PRINT_FILE(p_file, "Power off!!!\n");
            goto END;
        }
        sal_task_sleep(para->interval*1000);

        if(0 == para->enable)
        {
            goto END;
        }
    }

END:
    if (p_file)
    {
        sal_fclose(p_file);
        p_file = NULL;
    }
    return;
}


STATIC int32
_ctc_goldengate_dkit_monitor_temperature(void* p_para)
{
    int ret = 0;
    uint8 lchip = 0;
    uint8 task_id = CTC_DKIT_MONITOR_TASK_TEMPERATURE;
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};
    DKITS_PTR_VALID_CHECK(p_monitor_para);
    lchip = p_monitor_para->lchip;
    DKITS_PTR_VALID_CHECK(g_gg_dkit_master[lchip]);

    if ((p_monitor_para->enable) && (NULL == g_gg_dkit_master[lchip]->monitor_task[task_id].monitor_task))
    {
        if (NULL == g_gg_dkit_master[lchip]->monitor_task[task_id].para)
        {
            g_gg_dkit_master[lchip]->monitor_task[task_id].para
                   = (ctc_dkit_monitor_para_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_dkit_monitor_para_t));
            if (NULL == g_gg_dkit_master[lchip]->monitor_task[task_id].para)
            {
                return CLI_ERROR;
            }
        }
        sal_memcpy(g_gg_dkit_master[lchip]->monitor_task[task_id].para, p_para , sizeof(ctc_dkit_monitor_para_t));

        sal_sprintf(buffer, "Temperature-%d", lchip);
        ret = sal_task_create(&g_gg_dkit_master[lchip]->monitor_task[task_id].monitor_task,
                                                  buffer,
                                                  SAL_DEF_TASK_STACK_SIZE,
                                                  SAL_TASK_PRIO_DEF,
                                                  _ctc_goldengate_dkit_monitor_temperature_handler,
                                                  g_gg_dkit_master[lchip]->monitor_task[task_id].para);

        if (0 != ret)
        {
            CTC_DKIT_PRINT("Temperature monitor task create fail!\n");
            return CLI_ERROR;
        }
    }
    else if(0 == p_monitor_para->enable)
    {
        if (g_gg_dkit_master[lchip]->monitor_task[task_id].monitor_task)
        {
            sal_task_destroy(g_gg_dkit_master[lchip]->monitor_task[task_id].monitor_task);
            g_gg_dkit_master[lchip]->monitor_task[task_id].monitor_task = NULL;
        }
        if (g_gg_dkit_master[lchip]->monitor_task[task_id].para)
        {
            mem_free(g_gg_dkit_master[lchip]->monitor_task[task_id].para);
        }
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_monitor_show_sensor_result(void* p_para)
{
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;
    DKITS_PTR_VALID_CHECK(p_para);
    CTC_DKIT_LCHIP_CHECK(p_monitor_para->lchip);

    if (CTC_DKIT_MONITOR_SENSOR_TEMP == p_monitor_para->sensor_mode)
    {
        _ctc_goldengate_dkit_monitor_show_sensor_temperature(p_monitor_para->lchip);
    }
    else if (CTC_DKIT_MONITOR_SENSOR_VOL == p_monitor_para->sensor_mode)
    {
        _ctc_goldengate_dkit_monitor_show_sensor_voltage(p_monitor_para->lchip);
    }
    else if(CTC_DKIT_MONITOR_SENSOR_TEMP_NOMITOR == p_monitor_para->sensor_mode)
    {
        _ctc_goldengate_dkit_monitor_temperature(p_para);
    }

    return CLI_SUCCESS;
}


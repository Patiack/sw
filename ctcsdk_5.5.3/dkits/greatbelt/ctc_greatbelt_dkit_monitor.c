#include "greatbelt/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_greatbelt_dkit.h"
#include "ctc_greatbelt_dkit_monitor.h"

#define CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM  7
#define CTC_DKIT_MONITOR_TC_NUM  8

STATIC uint8
_ctc_greatbelt_dkit_monitor_get_congest_level(uint8 lchip, bool is_igress, uint8 sc)
{
    uint8 i = 0;
    uint32 total = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = MaxTblId_t;
    uint32 field_id = 0;

    if (is_igress)
    {
        cmd = DRV_IOR(IgrScCnt_t, IgrScCnt_ScCnt0_f);
        DRV_IOCTL(lchip, 0, cmd, &total);

        tbl_id = IgrCongestLevelThrd_t;
        field_id = IgrCongestLevelThrd_Sc0ThrdLvl0_f;
    }
    else
    {
        cmd = DRV_IOR(EgrScCnt_t, EgrScCnt_ScCnt0_f+ sc);
        DRV_IOCTL(lchip, 0, cmd, &total);
        tbl_id = EgrCongestLevelThrd_t;
        field_id = EgrCongestLevelThrd_Sc0Thrd0_f+ sc*CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM;
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
_ctc_greatbelt_dkit_monitor_get_channel_by_gport(uint8 lchip, uint16 gport)
{
    uint32 lport = 0;
    uint32 channel = 0xFFFFFFFF;
    uint8 i = 0;
    uint32 cmd = 0;
    uint32 value = 0;

    lport = CTC_DKIT_MAP_GPORT_TO_LPORT(gport);

    for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM; i++)
    {
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_LocalPhyPort_f);
        DRV_IOCTL(lchip, i, cmd, &value);
        if (value == lport)
        {
            channel = i;
            break;
        }
    }

    return channel;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_igr_summary(uint8 lchip, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint8 i = 0;
    uint32 value = 0;
    uint32 value0 = 0;
    char desc[16] = {0};
    uint32 total = 0;
    uint8 congest_level = 0;

    uint32 c2c_packet_cnt = 0;
    uint32 critical_packet_cnt = 0;
    uint32 igr_total_cnt = 0;
    buf_store_total_resrc_info_t buf_store_total_resrc_info;

    uint32 c2c_packet_thrd = 0;
    uint32 critical_packet_thrd = 0;
    uint32 igr_total_thrd = 0;
    igr_resrc_mgr_misc_ctl_t igr_resrc_mgr_misc_ctl;

    sal_memset(&buf_store_total_resrc_info, 0, sizeof(buf_store_total_resrc_info_t));
    cmd = DRV_IOR(BufStoreTotalResrcInfo_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &buf_store_total_resrc_info);
    c2c_packet_cnt = buf_store_total_resrc_info.c2c_packet_cnt;
    critical_packet_cnt = buf_store_total_resrc_info.critical_packet_cnt;
    igr_total_cnt = buf_store_total_resrc_info.igr_total_cnt;
    igr_total_thrd = buf_store_total_resrc_info.igr_total_thrd;

    total = igr_total_cnt + c2c_packet_cnt;

    sal_memset(&igr_resrc_mgr_misc_ctl, 0, sizeof(igr_resrc_mgr_misc_ctl_t));
    cmd = DRV_IOR(IgrResrcMgrMiscCtl_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, 0, cmd, &igr_resrc_mgr_misc_ctl);
    c2c_packet_thrd = igr_resrc_mgr_misc_ctl.c2c_packet_thrd;
    critical_packet_thrd = igr_resrc_mgr_misc_ctl.critical_packet_thrd;

    congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    CTC_DKITS_PRINT_FILE(p_wf, "\n----------congest config-----------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s\n", "level", "Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
    for (i = 0; i < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM; i++)
    {
        cmd = DRV_IOR(IgrCongestLevelThrd_t, IgrCongestLevelThrd_Sc0ThrdLvl0_f+ i);
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

     if ((igr_total_cnt >= critical_packet_cnt) && (igr_total_thrd >= critical_packet_thrd))
     {
         CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Normal",
                        (igr_total_cnt - critical_packet_cnt),
                        igr_total_thrd - critical_packet_thrd);
     }
    for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
    {
        cmd = DRV_IOR(IgrTcCnt_t, IgrTcCnt_TcCnt0_f + i);
        DRV_IOCTL(lchip, 0, cmd, &value0);
        sal_sprintf(desc,"TC%d",i);

        cmd = DRV_IOR(IgrTcThrd_t, IgrTcThrd_TcThrd0_f + i);
        DRV_IOCTL(lchip, congest_level, cmd, &value);

        CTC_DKITS_PRINT_FILE(p_wf, "   --%-9s %-10d %-10d \n", desc, value0, (value<<4));
    }
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Critical", critical_packet_cnt, critical_packet_thrd);
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "C2C", c2c_packet_cnt, c2c_packet_thrd);
    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_igr_detail_port_tc(uint8 lchip, uint16 channel, sal_file_t p_wf)
{
    uint8 i = 0;
    char desc[16] = {0};
    uint32 depth = 0 ;
    uint32 index= 0;
    uint32 index1= 0;
    uint32 cmd = 0;
    uint8 congest_level = 0;
    uint32 profid_high = 0;
    uint32 port_tc_thrd = 0;

    congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s\n", "TC",  "Depth", "Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
    index =channel*8;
    index1 =channel*2;
    for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
    {
        cmd = DRV_IOR(DsIgrPortTcCnt_t, DsIgrPortTcCnt_PortTcCnt_f);
        DRV_IOCTL(lchip, index + i, cmd, &depth);

        cmd = DRV_IOR(DsIgrPortTcThrdProfId_t, i%4);
        DRV_IOCTL(lchip, index1, cmd, &profid_high);

        cmd = DRV_IOR(DsIgrPortTcThrdProfile_t, DsIgrPortTcThrdProfile_PortTcThrd_f);
        DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_tc_thrd);

        sal_sprintf(desc, "TC%d", i);
        CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10d%-10d\n", desc,  depth, port_tc_thrd);
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_igr_detail(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 channel = 0;
    uint32 depth = 0;
    uint32 local_phy_port = 0;
    uint32 gchip = 0;
    uint32 i = 0;
    uint8 congest_level = 0;
    uint32 profid_high = 0;
    uint32 port_thrd = 0;
    bool b_all_zero = TRUE;


    congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, TRUE, 0);
    if (0xFFFF == gport)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
        CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s%-10s\n",
                       "Port", "Channel", "Depth", "Thrd");
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
        for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM; i++)
        {
            cmd = DRV_IOR(DsIgrPortCnt_t, DsIgrPortCnt_PortCnt_f);
            DRV_IOCTL(lchip, i, cmd, &depth);
            if (depth)
            {
                cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_LocalPhyPort_f);
                DRV_IOCTL(lchip, i, cmd, &local_phy_port);
                CTC_DKIT_GET_GCHIP(lchip, gchip);

                cmd = DRV_IOR(DsIgrPortThrdProfId_t, i % CTC_DKIT_MONITOR_TC_NUM);
                DRV_IOCTL(lchip, i / CTC_DKIT_MONITOR_TC_NUM , cmd, &profid_high);

                cmd = DRV_IOR(DsIgrPortThrdProfile_t, DsIgrPortThrdProfile_PortThrd_f);
                DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "0x%04x    %-10d%-10d%-10d\n", CTC_MAP_LPORT_TO_GPORT(gchip, local_phy_port), i , depth, (port_thrd<<4));
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
        channel = _ctc_greatbelt_dkit_monitor_get_channel_by_gport(lchip, gport);
        if (channel != 0xFFFFFFFF)
        {
            cmd = DRV_IOR(DsIgrPortCnt_t, DsIgrPortCnt_PortCnt_f);
            DRV_IOCTL(lchip, channel, cmd, &depth);

            cmd = DRV_IOR(DsIgrPortThrdProfId_t, channel % CTC_DKIT_MONITOR_TC_NUM);
            DRV_IOCTL(lchip, channel / CTC_DKIT_MONITOR_TC_NUM, cmd, &profid_high);

            cmd = DRV_IOR(DsIgrPortThrdProfile_t, DsIgrPortThrdProfile_PortThrd_f);
            DRV_IOCTL(lchip, (((profid_high&0x7) << 3) + congest_level), cmd, &port_thrd);

            CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-10s%-10s\n", "Port", "Channel", "Depth", "Thrd");
            CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "0x%04x    %-10d%-10d%-10d\n", gport, channel, depth, (port_thrd<<4));

            _ctc_greatbelt_dkit_monitor_q_depth_igr_detail_port_tc(lchip, channel, p_wf);
        }
        else
        {
            CTC_DKITS_PRINT_FILE(p_wf, "Invalid Gport:0x%x!!!\n", gport);
        }
    }


    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_igr(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 resrc_mgr_disable = 0;

    cmd = DRV_IOR(BufStoreMiscCtl_t, BufStoreMiscCtl_ResrcMgrDisable_f);
    DRV_IOCTL(lchip, 0, cmd, &resrc_mgr_disable);
    if (resrc_mgr_disable)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "Ingress Resouce Mamager is Disable!!!\n");
        return CLI_SUCCESS;
    }

    if (0xFFFF == gport)
    {
        _ctc_greatbelt_dkit_monitor_q_depth_igr_summary(lchip, p_wf);
    }
    _ctc_greatbelt_dkit_monitor_q_depth_igr_detail(lchip, gport, p_wf);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_egr_summary(uint8 lchip, sal_file_t p_wf)
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

    cmd = DRV_IOR(EgrTotalCnt_t, EgrTotalCnt_TotalCnt_f);
    DRV_IOCTL(lchip, 0, cmd, &totalCnt);
    cmd = DRV_IOR(EgrResrcMgrCtl_t, EgrResrcMgrCtl_EgrTotalThrd_f);
    DRV_IOCTL(lchip, 0, cmd, &egr_total_thrd);

    for (j = 0; j <= 3; j++)
    {
        congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, FALSE, j);
        CTC_DKITS_PRINT_FILE(p_wf, "\n--------SC%d congest config--------\n", j);
        CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s\n", "level", "Thrd");
        CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
        for (i = 0; i < CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM; i++)
        {
            cmd = DRV_IOR(EgrCongestLevelThrd_t, EgrCongestLevelThrd_Sc0Thrd0_f+ j*CTC_DKIT_MONITOR_CONGEST_LEVEL_NUM + i);
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
        }
    }

    CTC_DKITS_PRINT_FILE(p_wf, "\n-----------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10s %-10s\n", "Type", "Depth", "Drop-Thrd");
    CTC_DKITS_PRINT_FILE(p_wf, "-----------------------------------\n");
    CTC_DKITS_PRINT_FILE(p_wf, "%-14s %-10d %-10d\n", "Total", totalCnt, egr_total_thrd);

    for (i = 0; i < CTC_DKIT_MONITOR_TC_NUM; i++)
    {
        cmd = DRV_IOR(EgrTcCnt_t, EgrTcCnt_TcCnt0_f+ i);
        DRV_IOCTL(lchip, 0, cmd, &tc_cnt);
        sal_sprintf(desc, "TC%d", i);
        cmd = DRV_IOR(EgrTcThrd_t, EgrTcThrd_TcThrd0_f+ i);
        DRV_IOCTL(lchip, 0, cmd, &tc_thrd);
        CTC_DKITS_PRINT_FILE(p_wf, "   --%-9s %-10d %-10d \n", desc, tc_cnt, (tc_thrd<<4));
    }

        return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_egr_detail_queue(uint8 lchip, uint16 gport, sal_file_t p_wf)
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


    channel = _ctc_greatbelt_dkit_monitor_get_channel_by_gport(lchip, gport);
    for (i = 0; i < TABLE_MAX_INDEX(DsQueMap_t); i++)
    {
        cmd = DRV_IOR(DsQueMap_t, DsQueMap_ChanId_f);
        DRV_IOCTL(lchip, i , cmd, &chan_id);

        if (chan_id == channel)
        {
            CTC_DKITS_PRINT_FILE(p_wf, "\n------------------QGroup%d---------------------\n", i);
            CTC_DKITS_PRINT_FILE(p_wf, "%-10s%-10s%-6s%-10s%-10s\n", "Q-Id", "Depth", "SC", "SC-Level", "SC-Drop-Thrd");
            CTC_DKITS_PRINT_FILE(p_wf, "------------------------------------------------\n");
            for (j = 0; j < CTC_DKIT_MONITOR_TC_NUM; j++)
            {
                q_id = i*CTC_DKIT_MONITOR_TC_NUM + j;
                cmd = DRV_IOR(DsQueCnt_t, DsQueCnt_QueInstCnt_f);
                DRV_IOCTL(lchip, q_id , cmd, &que_inst_cnt);
                cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_MappedSc_f);
                DRV_IOCTL(lchip, q_id , cmd, &sc);
                congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, FALSE, sc);
                cmd = DRV_IOR(EgrScThrd_t, EgrScThrd_ScThrd0_f+ sc);
                DRV_IOCTL(lchip, 0 , cmd, &sc_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "%-10d%-10d%-6d%-10d%-10d\n", q_id, que_inst_cnt, sc, congest_level, (sc_thrd<<4));
            }

        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_egr_detail(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 channel = 0;
    uint32 depth = 0;
    uint32 local_phy_port = 0;
    uint32 gchip = 0;
    uint32 i = 0;

    uint8 congest_level = 0;
    uint32 port_thrd = 0;
    uint32 drop_prof_id = 0;
    bool b_all_zero = TRUE;

    congest_level = _ctc_greatbelt_dkit_monitor_get_congest_level(lchip, FALSE, 0);
    if (0xFFFF == gport)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
        CTC_DKITS_PRINT_FILE(p_wf, "%-12s%-12s%-12s\n",
                       "Port", "Channel", "Depth");
        CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
        for (i = 0; i < CTC_DKIT_ONE_SLICE_CHANNEL_NUM; i++)
        {
            cmd = DRV_IOR(DsEgrPortCnt_t, DsEgrPortCnt_PortCnt_f);
            DRV_IOCTL(lchip, i , cmd, &depth);
            if (depth)
            {
                cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_LocalPhyPort_f);
                DRV_IOCTL(lchip, i, cmd, &local_phy_port);
                CTC_DKIT_GET_GCHIP(lchip, gchip);

                cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_GrpDropProfIdHigh_f);
                DRV_IOCTL(lchip, i, cmd, &drop_prof_id);

                cmd = DRV_IOR(DsEgrPortDropProfile_t, DsEgrPortDropProfile_PortDropThrd_f);
                DRV_IOCTL(lchip, (((drop_prof_id&0x7) << 3) + congest_level), cmd, &port_thrd);
                CTC_DKITS_PRINT_FILE(p_wf, "0x%04x      %-12d%-12d\n", CTC_MAP_LPORT_TO_GPORT(gchip, local_phy_port),  i, depth);
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
        channel = _ctc_greatbelt_dkit_monitor_get_channel_by_gport(lchip, gport);
        if (channel != 0xFFFFFFFF)
        {
            cmd = DRV_IOR(DsEgrPortCnt_t, DsEgrPortCnt_PortCnt_f);
            DRV_IOCTL(lchip, channel, cmd, &depth);

            cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_GrpDropProfIdHigh_f);
            DRV_IOCTL(lchip, channel, cmd, &drop_prof_id);

            cmd = DRV_IOR(DsEgrPortDropProfile_t, DsEgrPortDropProfile_PortDropThrd_f);
            DRV_IOCTL(lchip, (((drop_prof_id&0x7) << 3) + congest_level), cmd, &port_thrd);

            CTC_DKITS_PRINT_FILE(p_wf, "\n--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "%-12s%-12s%-12s\n", "Port", "Channel", "Depth");
            CTC_DKITS_PRINT_FILE(p_wf, "--------------------------------------\n");
            CTC_DKITS_PRINT_FILE(p_wf, "0x%04x      %-12d%-12d\n", gport, channel, depth);

            _ctc_greatbelt_dkit_monitor_q_depth_egr_detail_queue(lchip, channel, p_wf);
        }
        else
        {
            CTC_DKITS_PRINT_FILE(p_wf, "Invalid Gport:0x%x!!!\n", gport);
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_greatbelt_dkit_monitor_q_depth_egr(uint8 lchip, uint16 gport, sal_file_t p_wf)
{
    uint32 cmd = 0;
    uint32 egr_resrc_mgr_en = 0;

    cmd = DRV_IOR(EgrResrcMgrCtl_t, EgrResrcMgrCtl_EgrResrcMgrEn_f);
    DRV_IOCTL(lchip, 0, cmd, &egr_resrc_mgr_en);
    if (0 == egr_resrc_mgr_en)
    {
        CTC_DKITS_PRINT_FILE(p_wf, "Egress Resouce Mamager is Disable!!!\n");
        return CLI_SUCCESS;
    }

    if (0xFFFF == gport)
    {
        _ctc_greatbelt_dkit_monitor_q_depth_egr_summary(lchip, p_wf);
    }
    _ctc_greatbelt_dkit_monitor_q_depth_egr_detail(lchip, gport, p_wf);

    return CLI_SUCCESS;
}


int32
ctc_greatbelt_dkit_monitor_show_queue_depth(void* p_para)
{
    uint32 entry_num = 0;
    uint32 cmd = 0;
    uint32 idx = 0;
    uint32 value = 0;
    bool b_all_zero = TRUE;
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;
    sal_file_t p_wf = p_monitor_para->p_wf;

    DKITS_PTR_VALID_CHECK(p_para);
    CTC_DKIT_LCHIP_CHECK(p_monitor_para->lchip);
    if (CTC_DKIT_INGRESS == p_monitor_para->dir)
    {
        _ctc_greatbelt_dkit_monitor_q_depth_igr(p_monitor_para->lchip, p_monitor_para->gport, p_wf);
    }
    else if(CTC_DKIT_EGRESS == p_monitor_para->dir)
    {
        _ctc_greatbelt_dkit_monitor_q_depth_egr(p_monitor_para->lchip, p_monitor_para->gport, p_wf);
    }
    else
    {
        CTC_DKITS_PRINT_FILE(p_wf, "%-20s %-10s\n", "QueueID", "Depth");
        CTC_DKITS_PRINT_FILE(p_wf, "---------------------------------------------\n");

        entry_num = TABLE_MAX_INDEX(DsQueCnt_t);
        cmd = DRV_IOR(DsQueCnt_t, DsQueCnt_QueInstCnt_f);
        for (idx = 0; idx < entry_num; idx++)
        {

            DRV_IOCTL(p_monitor_para->lchip, idx, cmd, &value);
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


int32
ctc_greatbelt_dkit_monitor_show_queue_id(void* p_para)
{
    uint32 cmd = 0;
    uint32 qid = 0;
    uint8 i = 0;
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;

    DKITS_PTR_VALID_CHECK(p_monitor_para);
    CTC_DKIT_LCHIP_CHECK(p_monitor_para->lchip);
    CTC_DKIT_PRINT("%-6s %-10s\n", "NO.", "QueID");
    CTC_DKIT_PRINT("-----------------------------------\n");
    for (i = 0; i < 4; i++)
    {
        cmd = DRV_IOR(QMgrQueueIdMon_t, QMgrQueueIdMon_QueueId0_f + i);
        DRV_IOCTL(p_monitor_para->lchip, 0, cmd, &qid);
        CTC_DKIT_PRINT("%-6d %-10d\n", i + 1, qid);
    }

    CTC_DKIT_PRINT("-----------------------------------\n");
    CTC_DKIT_PRINT("Tips: NO.1 is the newest QueueID\n\n");

    return CLI_SUCCESS;
}


STATIC int32
_ctc_greatbelt_dkit_monitor_show_sensor_temperature(uint8 chip_id)
{
    uint32 cmd = 0;
    uint32 value = 0;
    int32 temp_value = 0;
    uint32 timeout = 256;
    gb_sensor_ctl_t sensor_ctl;

    sal_memset(&sensor_ctl, 0, sizeof(gb_sensor_ctl_t));

    cmd = DRV_IOR(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    sensor_ctl.cfg_sensor_sleep = 0;
    sensor_ctl.cfg_sensor_volt = 0;
    cmd = DRV_IOW(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));

    value = 1;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &value));

    value = 0;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &value));

    cmd = DRV_IOR(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    while ((sensor_ctl.sensor_valid == 0) && (--timeout))
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    }

    if (sensor_ctl.sensor_valid == 1)
    {
        temp_value = sensor_ctl.sensor_value / 2 - 90;
        CTC_DKIT_PRINT("t = %d\n", temp_value);
    }
    else
    {
        CTC_DKIT_PRINT("Read temperature fail!!!\n");
        return DRV_E_TIME_OUT;
    }

    return DRV_E_NONE;
}


STATIC int32
_ctc_greatbelt_dkit_monitor_show_sensor_voltage(uint8 chip_id)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 timeout = 256;
    gb_sensor_ctl_t sensor_ctl;

    sal_memset(&sensor_ctl, 0, sizeof(gb_sensor_ctl_t));

    cmd = DRV_IOR(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    sensor_ctl.cfg_sensor_sleep = 0;
    sensor_ctl.cfg_sensor_volt = 1;
    cmd = DRV_IOW(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));

    value = 1;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &value));

    value = 0;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &value));

    cmd = DRV_IOR(GbSensorCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    while ((sensor_ctl.sensor_valid == 0) && (--timeout))
    {
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &sensor_ctl));
    }

    if (sensor_ctl.sensor_valid == 1)
    {
        CTC_DKIT_PRINT("Voltage: %dmV\n", value);
    }
    else
    {
        CTC_DKIT_PRINT("Read Voltage timeout. \n");
        return DRV_E_TIME_OUT;
    }

    return DRV_E_NONE;
}



int32
ctc_greatbelt_dkit_monitor_show_sensor_result(void* p_para)
{
    ctc_dkit_monitor_para_t* p_monitor_para = (ctc_dkit_monitor_para_t*)p_para;
    DKITS_PTR_VALID_CHECK(p_monitor_para);
    CTC_DKIT_LCHIP_CHECK(p_monitor_para->lchip);

    if (CTC_DKIT_MONITOR_SENSOR_TEMP == p_monitor_para->sensor_mode)
    {
        _ctc_greatbelt_dkit_monitor_show_sensor_temperature(p_monitor_para->lchip);
    }
    else if (CTC_DKIT_MONITOR_SENSOR_VOL == p_monitor_para->sensor_mode)
    {
        _ctc_greatbelt_dkit_monitor_show_sensor_voltage(p_monitor_para->lchip);
    }
    else if(CTC_DKIT_MONITOR_SENSOR_TEMP_NOMITOR == p_monitor_para->sensor_mode)
    {
        CTC_DKIT_PRINT("Function not support in this chip!!!\n");
    }

    return CLI_SUCCESS;
}


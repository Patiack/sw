#include "goldengate/include/drv_lib.h"
#include "ctc_cli.h"
#include "ctc_dkit.h"
#include "ctc_goldengate_dkit.h"
#include "ctc_goldengate_dkit_interface.h"

#define CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE (CTC_DKIT_INTERFACE_MAC_NUM / 2)

uint32 gg_xqmac_tbl_offset[32] =
{
      0,   1,   2,   3, /* XQMac0~3 */
      4,   5,   6,   7, /* XQMac0~3 */
      8,   9,   0,   0, /* XQMac0~1 */
    10, 11,   0,   0, /* XQMac0~1 */
    12, 13, 14, 15, /* XQMac0~3 */
    16, 17, 18, 19, /* XQMac0~3 */
    20, 21,   0,   0, /* XQMac0~1 */
    22, 23,   0,   0   /* XQMac0~1 */
};

uint32 gg_shared_pcs_sgmii_status[4] =
{
    SharedPcsSgmiiStatus00_t,
    SharedPcsSgmiiStatus10_t,
    SharedPcsSgmiiStatus20_t,
    SharedPcsSgmiiStatus30_t
};

uint32 gg_shared_pcs_xfi_status[4] =
{
    SharedPcsXfiStatus00_t,
    SharedPcsXfiStatus10_t,
    SharedPcsXfiStatus20_t,
    SharedPcsXfiStatus30_t
};


STATIC int32
_ctc_goldengate_dkit_interface_mac_to_gport(uint8 lchip,ctc_dkit_interface_status_t* status)
{
    uint8 slice = 0;
    uint32 cmd = 0;
    uint8 mac_id = 0;
    uint8 mac_id_one_slice = 0;
    uint32 tbl_id = 0;
    uint32 tbl_idx = 0;
    uint8 gchip = 0;
    uint32 channel_id = 0;
    uint32 local_phy_port = 0;
    uint16 gport  = 0;

    CTC_DKIT_GET_GCHIP(lchip, gchip);
    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        slice = mac_id / (CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE);
        mac_id_one_slice = mac_id % CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE;
        /* mac to channel */
        tbl_id = NetRxChannelMap0_t + slice;
        cmd = DRV_IOR(tbl_id, NetRxChannelMap0_cfgChanMapping_f);
        tbl_idx = mac_id_one_slice;
        DRV_IOCTL(lchip, tbl_idx, cmd, &channel_id);

        /* channel to lport */
        cmd = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_localPhyPort_f);
        tbl_idx = channel_id + (CTC_DKIT_ONE_SLICE_CHANNEL_NUM *slice);
        DRV_IOCTL(lchip, tbl_idx, cmd, &local_phy_port);
        gport = CTC_DKIT_DRV_LPORT_TO_CTC_GPORT(gchip, (local_phy_port + slice*CTC_DKIT_ONE_SLICE_PORT_NUM));

        if (((mac_id_one_slice >= 40) && (mac_id_one_slice <= 47))
            || (mac_id_one_slice > 55))/* invalid mac id */
        {
            continue;
        }
        else
        {
            status->gport[mac_id] = gport;
            status->valid[mac_id] = 1;
        }
    }
    return CLI_SUCCESS;
}

int32
_ctc_goldengate_dkit_interface_get_mac_mode(uint8 lchip,ctc_dkit_interface_status_t* status)
{
    uint8 slice = 0;
    uint32 cmd = 0;
    uint8 mac_id = 0;
    uint8 mac_id_one_slice = 0;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;
    uint8 xlgmac_index = 0;
    uint32 xlg_mode = 0;
    uint32 cg_mode = 0;
    uint32 sgmii_mode_rx = 0;
    uint32 xaui_mode = 0;
    uint32 xqmac_xgmac_cfg[4] =
    {
        XqmacSgmac0Cfg0_t,
        XqmacSgmac1Cfg0_t,
        XqmacSgmac2Cfg0_t,
        XqmacSgmac3Cfg0_t
    };

    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        if (0 == status->valid[mac_id])/* invalid mac id */
        {
            continue;
        }

        slice = mac_id / (CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE);
        mac_id_one_slice = mac_id % CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE;
        if (0  == (mac_id % 4))
        {
            xlgmac_index = mac_id / 4;
            if ((36 == mac_id_one_slice) || (52 == mac_id_one_slice))
            {
                tbl_id = NetRxCgCfg0_t + slice;
                cmd = DRV_IOR(tbl_id, (36 == mac_id_one_slice)? NetRxCgCfg0_cgMode0_f : NetRxCgCfg0_cgMode1_f);
                DRV_IOCTL(lchip, 0, cmd, &cg_mode);
                if (cg_mode)/* one 100G */
                {
                    status->mac_mode[mac_id] = CTC_DKIT_INTERFACE_CG;
                    status->valid[mac_id + 1] = 0;
                    status->valid[mac_id + 2] = 0;
                    status->valid[mac_id + 3] = 0;
                    continue;
                }
            }

            tbl_id = XqmacCfg0_t + gg_xqmac_tbl_offset[xlgmac_index];
            fld_id = XqmacCfg0_xlgMode_f;
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &xlg_mode);
            tbl_id = XqmacCfg0_t + gg_xqmac_tbl_offset[xlgmac_index];
            fld_id = XqmacCfg0_xauiMode_f;
            cmd = DRV_IOR(tbl_id, fld_id);
            DRV_IOCTL(lchip, 0, cmd, &xaui_mode);
            if (xlg_mode&&(!xaui_mode))/* one 40G */
            {
                status->mac_mode[mac_id] = CTC_DKIT_INTERFACE_XLG;
                status->valid[mac_id + 1] = 0;
                status->valid[mac_id + 2] = 0;
                status->valid[mac_id + 3] = 0;
                continue;
            }
            else if (xaui_mode&&xlg_mode)
            {
                status->mac_mode[mac_id] = CTC_DKIT_INTERFACE_XAUI;
                status->valid[mac_id + 1] = 0;
                status->valid[mac_id + 2] = 0;
                status->valid[mac_id + 3] = 0;
                continue;
            }
        }


        tbl_id = xqmac_xgmac_cfg[mac_id % 4] + gg_xqmac_tbl_offset[xlgmac_index];
        fld_id = XqmacSgmac0Cfg0_sgmiiModeRx0_f;
        cmd = DRV_IOR(tbl_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &sgmii_mode_rx);
        if (sgmii_mode_rx)/* 1G */
        {
            status->mac_mode[mac_id] = CTC_DKIT_INTERFACE_SGMII;
        }
        else/* 10G */
        {
            status->mac_mode[mac_id] = CTC_DKIT_INTERFACE_XFI;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_interface_get_mac_auto_neg(uint8 lchip,ctc_dkit_interface_status_t* status)
{
    uint32 cmd = 0;
    uint8 mac_id = 0;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;
    uint32 is_auto_neg = 0;
    uint32 xlgmac_index = 0;
    uint32 shared_pcs_sgmii_cfg[4] =
    {
        SharedPcsSgmiiCfg00_t,
        SharedPcsSgmiiCfg10_t,
        SharedPcsSgmiiCfg20_t,
        SharedPcsSgmiiCfg30_t
    };

    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        if (0 == status->valid[mac_id])/* invalid mac id */
        {
            continue;
        }
        if (CTC_DKIT_INTERFACE_SGMII != status->mac_mode[mac_id])/* only 1g mac has auto_neg function */
        {
            continue;
        }
        xlgmac_index = mac_id / 4;
        tbl_id = shared_pcs_sgmii_cfg[mac_id % 4] + gg_xqmac_tbl_offset[xlgmac_index];
        fld_id = SharedPcsSgmiiCfg00_anEnable0_f;
        cmd = DRV_IOR(tbl_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &is_auto_neg);
        if (is_auto_neg)
        {
            status->auto_neg[mac_id] = 1;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_interface_get_mac_en(uint8 lchip,ctc_dkit_interface_status_t* status)
{
    uint32 cmd = 0;
    uint8 mac_id = 0;
    uint8 mac_id_one_slice = 0;
    uint32 tbl_id = 0;
    uint32 fld_id = 0;
    uint32 mac_reset = 0;
    uint8 slice = 0;
    uint32 reset_core_pcs[64] =
    {
        RlmHsCtlReset0_resetCorePcs0_f,
        RlmHsCtlReset0_resetCorePcs1_f,
        RlmHsCtlReset0_resetCorePcs2_f,
        RlmHsCtlReset0_resetCorePcs3_f,
        RlmHsCtlReset0_resetCorePcs4_f,
        RlmHsCtlReset0_resetCorePcs5_f,
        RlmHsCtlReset0_resetCorePcs6_f,
        RlmHsCtlReset0_resetCorePcs7_f,
        RlmHsCtlReset0_resetCorePcs8_f,
        RlmHsCtlReset0_resetCorePcs9_f,
        RlmHsCtlReset0_resetCorePcs10_f,
        RlmHsCtlReset0_resetCorePcs11_f,
        RlmHsCtlReset0_resetCorePcs12_f,
        RlmHsCtlReset0_resetCorePcs13_f,
        RlmHsCtlReset0_resetCorePcs14_f,
        RlmHsCtlReset0_resetCorePcs15_f,
        RlmHsCtlReset0_resetCorePcs16_f,
        RlmHsCtlReset0_resetCorePcs17_f,
        RlmHsCtlReset0_resetCorePcs18_f,
        RlmHsCtlReset0_resetCorePcs19_f,
        RlmHsCtlReset0_resetCorePcs20_f,
        RlmHsCtlReset0_resetCorePcs21_f,
        RlmHsCtlReset0_resetCorePcs22_f,
        RlmHsCtlReset0_resetCorePcs23_f,
        RlmHsCtlReset0_resetCorePcs24_f,
        RlmHsCtlReset0_resetCorePcs25_f,
        RlmHsCtlReset0_resetCorePcs26_f,
        RlmHsCtlReset0_resetCorePcs27_f,
        RlmHsCtlReset0_resetCorePcs28_f,
        RlmHsCtlReset0_resetCorePcs29_f,
        RlmHsCtlReset0_resetCorePcs30_f,
        RlmHsCtlReset0_resetCorePcs31_f,
        RlmCsCtlReset0_resetCorePcs32_f,
        RlmCsCtlReset0_resetCorePcs33_f,
        RlmCsCtlReset0_resetCorePcs34_f,
        RlmCsCtlReset0_resetCorePcs35_f,
        RlmCsCtlReset0_resetCorePcs36_f,
        RlmCsCtlReset0_resetCorePcs37_f,
        RlmCsCtlReset0_resetCorePcs38_f,
        RlmCsCtlReset0_resetCorePcs39_f,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        RlmCsCtlReset0_resetCorePcs40_f,/* mac48 */
        RlmCsCtlReset0_resetCorePcs41_f,/* mac49 */
        RlmCsCtlReset0_resetCorePcs42_f,/* mac50 */
        RlmCsCtlReset0_resetCorePcs43_f,/* mac51 */
        RlmCsCtlReset0_resetCorePcs44_f,/* mac52 */
        RlmCsCtlReset0_resetCorePcs45_f,/* mac53 */
        RlmCsCtlReset0_resetCorePcs46_f,/* mac54 */
        RlmCsCtlReset0_resetCorePcs47_f,/* mac55 */
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    };
    uint32 resetCoreXlgPcs[16] =
    {
        RlmHsCtlReset0_resetCoreXlgPcs0_f, /* mac0 */
        RlmHsCtlReset0_resetCoreXlgPcs1_f, /* mac4 */
        RlmHsCtlReset0_resetCoreXlgPcs2_f, /* mac8 */
        RlmHsCtlReset0_resetCoreXlgPcs3_f, /* mac12 */
        RlmHsCtlReset0_resetCoreXlgPcs4_f, /* mac16 */
        RlmHsCtlReset0_resetCoreXlgPcs5_f, /* mac20 */
        RlmHsCtlReset0_resetCoreXlgPcs6_f, /* mac24 */
        RlmHsCtlReset0_resetCoreXlgPcs7_f, /* mac28 */
        RlmCsCtlReset0_resetCoreXlgPcs8_f, /* mac32 */
        RlmCsCtlReset0_resetCoreXlgPcs9_f, /* mac36 */
        0,
        0,
        RlmCsCtlReset0_resetCoreXlgPcs10_f, /* mac48 */
        RlmCsCtlReset0_resetCoreXlgPcs11_f, /* mac52 */
        0,
        0,
    };
    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        if (0 == status->valid[mac_id])/* invalid mac id */
        {
            continue;
        }

        slice = mac_id / (CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE);
        mac_id_one_slice = mac_id % CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE;
        if((CTC_DKIT_INTERFACE_SGMII == status->mac_mode[mac_id])
            || (CTC_DKIT_INTERFACE_XFI == status->mac_mode[mac_id]))
        {
            tbl_id = ((mac_id_one_slice < 32)? RlmHsCtlReset0_t : RlmCsCtlReset0_t) + slice;
            fld_id = reset_core_pcs[mac_id_one_slice];
        }
        else if ((CTC_DKIT_INTERFACE_XLG == status->mac_mode[mac_id])
            ||(CTC_DKIT_INTERFACE_XAUI == status->mac_mode[mac_id]))
        {
            tbl_id = ((mac_id_one_slice < 32)? RlmHsCtlReset0_t : RlmCsCtlReset0_t) + slice;
            fld_id = resetCoreXlgPcs[mac_id_one_slice / 4 ];
        }
        else if (CTC_DKIT_INTERFACE_CG == status->mac_mode[mac_id])
        {
            if (32 == mac_id_one_slice)
            {
                tbl_id = RlmCsCtlReset0_t + slice;
                fld_id = RlmCsCtlReset0_resetCoreCgPcs0_f;
            }
            else if (52 == mac_id_one_slice)
            {
                tbl_id = RlmCsCtlReset0_t + slice;
                fld_id = RlmCsCtlReset0_resetCoreCgPcs1_f;
            }
        }

        cmd = DRV_IOR(tbl_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &mac_reset);
        if (0 == mac_reset)/* mac enable */
        {
            status->mac_en[mac_id] = 1;
        }
    }

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_interface_get_link_status(uint8 lchip,ctc_dkit_interface_status_t* status)
{
    uint32 cmd = 0;
    uint8 mac_id = 0;
    /*uint8 mac_id_one_slice = 0;*/
    uint32 tbl_id = 0;
    uint32 fld_id = 0;
    uint32 fld_id1 = 0;
    uint8 xlgmac_index = 0;
    uint32 link_status = 0;
    uint32 hi_ber0 = 0;
    /*uint8 slice = 0;*/


    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        if (0 == status->valid[mac_id])/* invalid mac id */
        {
            continue;
        }

        /*slice = mac_id / (CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE);*/
        /*mac_id_one_slice = mac_id % CTC_DKIT_INTERFACE_MAC_NUM_PER_SLICE;*/
        xlgmac_index = mac_id / 4;
        if (CTC_DKIT_INTERFACE_SGMII == status->mac_mode[mac_id])
        {
            tbl_id = gg_shared_pcs_sgmii_status[mac_id % 4] + gg_xqmac_tbl_offset[xlgmac_index];
            fld_id = SharedPcsSgmiiStatus00_anLinkStatus0_f;
        }
        else if (CTC_DKIT_INTERFACE_XFI == status->mac_mode[mac_id])
        {
            tbl_id = gg_shared_pcs_xfi_status[mac_id % 4] + gg_xqmac_tbl_offset[xlgmac_index];
            fld_id = SharedPcsXfiStatus00_rxBlockLock0_f;
            fld_id1 = SharedPcsXfiStatus00_hiBer0_f;
        }
        else if ((CTC_DKIT_INTERFACE_XLG == status->mac_mode[mac_id])
            ||(CTC_DKIT_INTERFACE_XAUI == status->mac_mode[mac_id]))
        {
            tbl_id = SharedPcsXlgStatus0_t + gg_xqmac_tbl_offset[xlgmac_index];
            fld_id = SharedPcsXlgStatus0_alignStatus_f;
        }
        else if (CTC_DKIT_INTERFACE_CG == status->mac_mode[mac_id])
        {
            if (9 == xlgmac_index)
            {
                tbl_id = CgPcsStatus0_t;
            }
            else if (13 == xlgmac_index)
            {
                tbl_id = CgPcsStatus1_t;
            }
            else if (25 == xlgmac_index)
            {
                tbl_id = CgPcsStatus2_t;
            }
            else if (29 == xlgmac_index)
            {
                tbl_id = CgPcsStatus3_t;
            }
            fld_id = CgPcsStatus0_alignStatus_f;
        }
        cmd = DRV_IOR(tbl_id, fld_id);
        DRV_IOCTL(lchip, 0, cmd, &link_status);
        if((CTC_DKIT_INTERFACE_XFI == status->mac_mode[mac_id]))
        {
            cmd = DRV_IOR(tbl_id, fld_id1);
            DRV_IOCTL(lchip, 0, cmd, &hi_ber0);
            if ((1 == link_status)&&(0==hi_ber0))/* mac enable */
            {
                status->link_up[mac_id] = 1;
            }
        }
        else
        {
            if (1 == link_status)/* mac enable */
            {
                status->link_up[mac_id] = 1;
            }
        }
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_dkit_interface_get_mac_status(uint8 lchip, ctc_dkit_interface_status_t* status)
{
    _ctc_goldengate_dkit_interface_mac_to_gport(lchip, status);
    _ctc_goldengate_dkit_interface_get_mac_mode(lchip, status);
    _ctc_goldengate_dkit_interface_get_mac_auto_neg(lchip, status);
    _ctc_goldengate_dkit_interface_get_mac_en(lchip, status);
    _ctc_goldengate_dkit_interface_get_link_status(lchip, status);

    return CLI_SUCCESS;
}

STATIC int32
_ctc_goldengate_dkit_interface_is_print(ctc_dkit_interface_para_t* p_interface_para, ctc_dkit_interface_status_t* status, uint8 mac_id)
{
    uint8 is_print = 0;
    switch (p_interface_para->type)
    {
        case CTC_DKIT_INTERFACE_ALL:
            is_print = 1;
            break;
        case CTC_DKIT_INTERFACE_MAC_ID:
            if (p_interface_para->value >= CTC_DKIT_INTERFACE_MAC_NUM)
            {
                CTC_DKIT_PRINT("Mac id is out of range 0~%d!!!", CTC_DKIT_INTERFACE_MAC_NUM);
            }
            else if (p_interface_para->value == mac_id)
            {
                is_print = 1;
            }
            break;
        case CTC_DKIT_INTERFACE_GPORT:
            if (p_interface_para->value == status->gport[mac_id])
            {
                is_print = 1;
            }
            break;
        case CTC_DKIT_INTERFACE_MAC_EN:
            if (p_interface_para->value == status->mac_en[mac_id])
            {
                is_print = 1;
            }
            break;
        case CTC_DKIT_INTERFACE_LINK_STATUS:
            if (p_interface_para->value == status->link_up[mac_id])
            {
                is_print = 1;
            }
            break;
        case CTC_DKIT_INTERFACE_SPEED:
            if ((p_interface_para->value == status->mac_mode[mac_id])
                ||((CTC_DKIT_INTERFACE_XLG == p_interface_para->value)&&(CTC_DKIT_INTERFACE_XAUI == status->mac_mode[mac_id])))
            {
                is_print = 1;
            }
            break;
        default:
            break;
    }
    return is_print;
}

int32
ctc_goldengate_dkit_interface_show_mac_status(void* p_para)
{
    ctc_dkit_interface_para_t* p_interface_para = (ctc_dkit_interface_para_t*)p_para;
    ctc_dkit_interface_status_t status;
    uint8 mac_id = 0;
    uint16 gport = 0;
    char* mac_en = NULL;
    char* link_up = "-";
    char* type[CTC_DKIT_INTERFACE_MAC_MAX] = {"SGMII", "XFI", "XAUI", "XLG", "CG"};
    char* auto_neg = "-";
    char* speed[CTC_DKIT_INTERFACE_MAC_MAX]  = {"1G", "10G", "10G", "40G", "100G"};
    uint8 is_print = 0;

    DKITS_PTR_VALID_CHECK(p_para);
    CTC_DKIT_LCHIP_CHECK(p_interface_para->lchip);
    sal_memset(&status, 0 , sizeof(ctc_dkit_interface_status_t));
    ctc_goldengate_dkit_interface_get_mac_status(p_interface_para->lchip, &status);

    CTC_DKIT_PRINT("%-8s %-10s %-8s %-8s %-8s %-8s %-8s %-8s\n",
                          "Mac", "Port", "MacEn", "Link", "Speed", "Duplex", "AutoNeg", "Type");
    CTC_DKIT_PRINT("---------------------------------------------------------------------------\n");

    for (mac_id = 0; mac_id < CTC_DKIT_INTERFACE_MAC_NUM; mac_id++)
    {
        if (0 == status.valid[mac_id])/* invalid mac id */
        {
            continue;
        }

        is_print = _ctc_goldengate_dkit_interface_is_print(p_interface_para, &status, mac_id);
        if (is_print)
        {
            gport = status.gport[mac_id];
            mac_en = status.mac_en[mac_id]? "Enable" : "Disable";

            link_up = "-";
            if (mac_en)
            {
                link_up = status.link_up[mac_id]? "Up" : "Down";
            }

             auto_neg = "-";
             if (CTC_DKIT_INTERFACE_SGMII == status.mac_mode[mac_id])
             {
                 auto_neg = status.auto_neg[mac_id]? "Yes" : "No";
             }
            CTC_DKIT_PRINT("%-8d 0x%04x     %-8s %-8s %-8s %-8s %-8s %-8s\n",
                                   mac_id, gport, mac_en, link_up, speed[status.mac_mode[mac_id]], "FD", auto_neg, type[status.mac_mode[mac_id]]);
        }
    }
    CTC_DKIT_PRINT("---------------------------------------------------------------------------\n");
    return CLI_SUCCESS;
}


int32
_ctc_goldengate_dkit_memory_read_table(ctc_dkit_memory_para_t* p_memory_para);

STATIC int32
_ctc_goldengate_dkit_interface_print_pcs_table(uint8 lchip, uint32 tbl_id, uint32 index)
{
    char tbl_name[32] = {0};
    ctc_dkit_memory_para_t* ctc_dkit_memory_para = NULL;

    ctc_dkit_memory_para = (ctc_dkit_memory_para_t*)sal_malloc(sizeof(ctc_dkit_memory_para_t));
    if(NULL == ctc_dkit_memory_para)
    {
        return CLI_ERROR;
    }
    sal_memset(ctc_dkit_memory_para, 0 , sizeof(ctc_dkit_memory_para_t));

    ctc_dkit_memory_para->param[1] = lchip;
    ctc_dkit_memory_para->param[2] = index;
    ctc_dkit_memory_para->param[3] = tbl_id;
    ctc_dkit_memory_para->param[4] = 1;

    drv_goldengate_get_tbl_string_by_id(tbl_id , tbl_name);
    CTC_DKIT_PRINT("Table name:%s\n", tbl_name);
    CTC_DKIT_PRINT("-------------------------------------------------------------\n");

    _ctc_goldengate_dkit_memory_read_table(ctc_dkit_memory_para);

    if(NULL != ctc_dkit_memory_para)
    {
        sal_free(ctc_dkit_memory_para);
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_dkit_interface_show_pcs_status(uint8 lchip, uint16 mac_id)
{
    uint32 mac_index = 0;
    uint32 tbl_id = MaxTblId_t;
    uint32 tbl_id_1 = MaxTblId_t;
    uint8 i = 0;
    char* mac_mode = NULL;

    ctc_dkit_interface_status_t status;

    sal_memset(&status, 0 , sizeof(status));

    CTC_DKIT_LCHIP_CHECK(lchip);
    if (mac_id >= CTC_DKIT_INTERFACE_MAC_NUM)
    {
        CTC_DKIT_PRINT("Mac id is out of range 0~%d!!!", CTC_DKIT_INTERFACE_MAC_NUM);
        return CLI_ERROR;
    }
    _ctc_goldengate_dkit_interface_mac_to_gport(lchip, &status);
    _ctc_goldengate_dkit_interface_get_mac_mode(lchip, &status);
    if (!status.valid[mac_id])
    {
        CTC_DKIT_PRINT("Mac%d is unused!\n", mac_id);
        return CLI_ERROR;
    }

    mac_index = mac_id / 4;
    switch (status.mac_mode[mac_id])
    {
        case  CTC_DKIT_INTERFACE_SGMII:
            mac_mode = "SGMII";
            tbl_id = gg_shared_pcs_sgmii_status[mac_id % 4] + gg_xqmac_tbl_offset[mac_index];
            break;
        case  CTC_DKIT_INTERFACE_XFI:
            mac_mode = "XFI";
            tbl_id = gg_shared_pcs_xfi_status[mac_id % 4] + gg_xqmac_tbl_offset[mac_index];
            break;
        case  CTC_DKIT_INTERFACE_XAUI:
            mac_mode = "XAUI";
            tbl_id = SharedPcsXlgStatus0_t + gg_xqmac_tbl_offset[mac_index];
            tbl_id_1 = SharedPcsXauiStatus0_t + gg_xqmac_tbl_offset[mac_index];
            break;
        case  CTC_DKIT_INTERFACE_XLG:
            mac_mode = "XLG";
            tbl_id = SharedPcsXlgStatus0_t + gg_xqmac_tbl_offset[mac_index];
            tbl_id_1 = gg_shared_pcs_xfi_status[mac_id % 4] + gg_xqmac_tbl_offset[mac_index];
            break;
        case  CTC_DKIT_INTERFACE_CG:
            mac_mode = "CG";
            if (9 == mac_index)
            {
                tbl_id = CgPcsStatus0_t;
                tbl_id_1 = CgPcsDebugStats0_t;
            }
            else if (13 == mac_index)
            {
                tbl_id = CgPcsStatus1_t;
                tbl_id_1 = CgPcsDebugStats1_t;
            }
            else if (25 == mac_index)
            {
                tbl_id = CgPcsStatus2_t;
                tbl_id_1 = CgPcsDebugStats2_t;
            }
            else if (29 == mac_index)
            {
                tbl_id = CgPcsStatus3_t;
                tbl_id_1 = CgPcsDebugStats3_t;
            }
            break;
        default:
            break;
    }

    if (tbl_id != MaxTblId_t)
    {
        CTC_DKIT_PRINT("%s mac pcs status---->\n", mac_mode);
        _ctc_goldengate_dkit_interface_print_pcs_table(lchip, tbl_id, 0);
    }
    if (tbl_id_1 != MaxTblId_t)
    {

        if (CTC_DKIT_INTERFACE_XLG == status.mac_mode[mac_id])
        {
            for (i = 0; i < 4; i++)
            {
                _ctc_goldengate_dkit_interface_print_pcs_table(lchip, tbl_id_1 + ((SharedPcsXfiStatus023_t-SharedPcsXfiStatus00_t + 1)*i), 0);
            }
        }
        else
        {
            _ctc_goldengate_dkit_interface_print_pcs_table(lchip, tbl_id_1, 0);
        }
    }

    if ((MaxTblId_t == tbl_id) && (MaxTblId_t == tbl_id_1))
    {
        CTC_DKIT_PRINT("Can match mac mode!!!");
    }

    return CLI_SUCCESS;
}




/**
 @file sys_greatbelt_register.c

 @date 2009-11-6

 @version v2.0


*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_pdu.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_pdu.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_stp.h"
#include "sys_greatbelt_parser.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_nexthop_api.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"
#include "greatbelt/include/drv_data_path.h"



sys_register_master_t* p_gb_register_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/
int32
_sys_greatbelt_reg_mdio_scan_init(uint8 lchip)
{
    ctc_chip_phy_mapping_para_t* p_phy_mapping = NULL;
    ctc_chip_phy_scan_ctrl_t scan_ctl;
    uint32 use_phy0_31 = 0;
    uint32 use_phy32_59 = 0;
    uint32 phy_idx = 0;
    uint8 mdio_bus = 0;
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    int32 ret = CTC_E_NONE;

    p_phy_mapping = (ctc_chip_phy_mapping_para_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ctc_chip_phy_mapping_para_t));
    if (NULL == p_phy_mapping)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_phy_mapping, 0, sizeof(ctc_chip_phy_mapping_para_t));
    sal_memset(&scan_ctl, 0, sizeof(ctc_chip_phy_scan_ctrl_t));

    CTC_ERROR_GOTO(sys_greatbelt_chip_get_phy_mapping(lchip, p_phy_mapping), ret, out);
    CTC_ERROR_GOTO(sys_greatbelt_get_gchip_id(lchip, &gchip), ret, out);

       /*2. cfg mdio use phy or not to select link status source */
       for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
       {
           if (p_phy_mapping->port_phy_mapping_tbl[lport] != 0xff)
           {
               gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
               mac_id = SYS_GET_MAC_ID(lchip, gport);
               if (0xFF == mac_id)
               {
                   continue;
               }
               if (mac_id < 32)
               {
                   use_phy0_31 |= (1 << mac_id);
               }
               else
               {
                   use_phy32_59 |= (1 << (mac_id - 32));
               }
           }
       }

       /* cfg mdio usephy */
       scan_ctl.op_flag = CTC_CHIP_USE_PHY_OP;
       scan_ctl.mdio_use_phy0 = use_phy0_31;
       scan_ctl.mdio_use_phy1 = use_phy32_59;
       CTC_ERROR_GOTO(sys_greatbelt_chip_set_phy_scan_para(lchip, &scan_ctl), ret, out);

       if ((use_phy0_31 == 0) && (use_phy32_59 == 0))
       {
           /* not use GB mdio interface to monitor link status, no need to enable mdio auto scan */
           //SYS_LINKAGG_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Not use mdio interface!! \n");
           ret = CTC_E_NONE;
           goto out;
       }

       /* cfg scan interval */
       scan_ctl.op_flag = CTC_CHIP_INTERVAL_OP;
       scan_ctl.scan_interval = 100;
       CTC_ERROR_GOTO(sys_greatbelt_chip_set_phy_scan_para(lchip, &scan_ctl), ret, out);

       /* cfg phy mac mapping */
       cmd = DRV_IOW(PortMap_t, PortMap_MacId_f);

       for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
       {
           phy_idx = p_phy_mapping->port_phy_mapping_tbl[lport];
           gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
           mac_id = SYS_GET_MAC_ID(lchip, gport);
           if (0xFF == mac_id)
           {
               continue;
           }

           field_val = mac_id;
           mdio_bus = p_phy_mapping->port_mdio_mapping_tbl[lport];

           if (mdio_bus < 2)
           {
               phy_idx = mdio_bus*32 + phy_idx;
           }
           else
           {
               phy_idx = 64 + (mdio_bus - 2)*12;
           }

           if (mdio_bus != 0xff)
           {
               CTC_ERROR_GOTO(DRV_IOCTL(lchip, phy_idx, cmd, &field_val), ret, out);
           }
       }

out:
    if (p_phy_mapping)
    {
        mem_free(p_phy_mapping);
    }

    return ret;
}


STATIC int32
_sys_greatbelt_lkp_ctl_init(uint8 lchip)
{

        CTC_ERROR_RETURN(sys_greatbelt_ftm_lkp_register_init(lchip));


    return CTC_E_NONE;
}

int32
sys_greatbelt_lkup_ttl_index(uint8 lchip, uint8 ttl, uint32* ttl_index)
{
    uint32 cmd;
    uint8 index = 0;
    uint32 mpls_ttl = 0;
    uint8 bfind = 0;

    for (index = 0; index < 16; index++)
    {
        cmd = DRV_IOR(EpeL3EditMplsTtl_t, EpeL3EditMplsTtl_Ttl0_f + index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mpls_ttl));

        if (mpls_ttl >= ttl)
        {
            *ttl_index = index;
            bfind = 1;
            break;
        }
    }

    if (bfind)
    {
        return CTC_E_NONE;
    }

    return CTC_E_INVALID_TTL;

}

int32
sys_greatbelt_lkup_ttl(uint8 lchip, uint8 ttl_index, uint32* ttl)
{
    uint32 cmd;

    if (ttl_index < 16)
    {
        cmd = DRV_IOR(EpeL3EditMplsTtl_t, EpeL3EditMplsTtl_Ttl0_f + ttl_index);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ttl));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mpls_ctl_init(uint8 lchip)
{
    uint32 cmd;
    epe_l3_edit_mpls_ttl_t l3_edit_mpls_ttl;

    sal_memset(&l3_edit_mpls_ttl, 0, sizeof(l3_edit_mpls_ttl));

    cmd = DRV_IOW(EpeL3EditMplsTtl_t, DRV_ENTRY_FLAG);

        l3_edit_mpls_ttl.ttl0  = 0;
        l3_edit_mpls_ttl.ttl1  = 1;
        l3_edit_mpls_ttl.ttl2  = 2;
        l3_edit_mpls_ttl.ttl3  = 8;
        l3_edit_mpls_ttl.ttl4  = 15;
        l3_edit_mpls_ttl.ttl5  = 16;
        l3_edit_mpls_ttl.ttl6  = 31;
        l3_edit_mpls_ttl.ttl7  = 32;
        l3_edit_mpls_ttl.ttl8  = 60;
        l3_edit_mpls_ttl.ttl9  = 63;
        l3_edit_mpls_ttl.ttl10 = 64;
        l3_edit_mpls_ttl.ttl11 = 65;
        l3_edit_mpls_ttl.ttl12 = 127;
        l3_edit_mpls_ttl.ttl13 = 128;
        l3_edit_mpls_ttl.ttl14 = 254;
        l3_edit_mpls_ttl.ttl15 = 255;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &l3_edit_mpls_ttl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_nexthop_ctl_init(uint8 lchip)
{
    /* POP DS */
    uint32 cmd = 0;
    uint32 field_val = 0;

    field_val = 1;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_MacDaBit40Mode_f);

        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_excp_ctl_init(uint8 lchip)
{
    /* POP DS */
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 excp_idx = 0;
    uint8 lgchip  = 0;
        sys_greatbelt_get_gchip_id(lchip, &lgchip);

        for (excp_idx = 0; excp_idx < 256; excp_idx++)
        {
            field_val = (lgchip << 16) | SYS_RESERVE_PORT_ID_DROP;
            cmd = DRV_IOW(DsMetFifoExcp_t, DsMetFifoExcp_DestMap_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));

            cmd = DRV_IOW(DsBufRetrvExcp_t, DsBufRetrvExcp_NextHopPtr_f);
            field_val = excp_idx;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, excp_idx, cmd, &field_val));
        }

    return CTC_E_NONE;

}

int32
sys_greatbelt_tcam_edram_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

        /*init edram*/
        field_val = 1;
        cmd = DRV_IOW(DynamicDsEdramInitCtl_t, DynamicDsEdramInitCtl_EDramInitEnable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init sram*/
        field_val = 1;
        cmd = DRV_IOW(DynamicDsSramInit_t, DynamicDsSramInit_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init big tcam*/
        field_val = 0;
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitStartAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 0x1FFF;  /*Tcam support 8K*/
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitEndAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 1;
        cmd = DRV_IOW(TcamCtlIntInitCtrl_t, TcamCtlIntInitCtrl_CfgInitEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init big tcamAd*/
        field_val = 1;
        cmd = DRV_IOW(TcamDsRamInitCtl_t, TcamDsRamInitCtl_DsRamInit_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init lpm tcam*/
        field_val = 1;
        cmd = DRV_IOW(FibTcamInitCtl_t, FibTcamInitCtl_TcamInitEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*init lpm tcamAd*/
        field_val = 1;
        cmd = DRV_IOW(FibInitCtl_t, FibInitCtl_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_net_rx_tx_init(uint8 lchip)
{
    /* only work on UML */
    if (SDK_WORK_PLATFORM == 1)
    {
    uint32 cmd = 0;
    uint16 chan_id = 0;
    net_rx_channel_map_t net_rx_channel_map;
    net_tx_port_id_map_t net_tx_port_map;
    net_tx_chan_id_map_t net_tx_channel_map;
    buf_retrv_chan_id_cfg_t buf_retrv_chan_id_cfg;
    net_tx_cfg_chan_id_t net_tx_cfg_chan_id;

    sal_memset(&net_rx_channel_map, 0, sizeof(net_rx_channel_map_t));
    sal_memset(&net_tx_port_map, 0, sizeof(net_tx_port_id_map_t));
    sal_memset(&net_tx_channel_map, 0, sizeof(net_tx_chan_id_map_t));
    sal_memset(&net_tx_cfg_chan_id, 0, sizeof(net_tx_cfg_chan_id_t));

    sal_memset(&buf_retrv_chan_id_cfg, 0, sizeof(buf_retrv_chan_id_cfg_t));
    buf_retrv_chan_id_cfg.cfg_int_lk_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_int_lk_chan_id_int     = SYS_INTLK_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_i_loop_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_i_loop_chan_id_int     = SYS_ILOOP_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_cpu_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_cpu_chan_id_int        = SYS_CPU_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_dma_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_dma_chan_id_int        = SYS_DMA_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_oam_chan_en_int        = 1;
    buf_retrv_chan_id_cfg.cfg_oam_chan_id_int        = SYS_OAM_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_e_loop_chan_en_int     = 1;
    buf_retrv_chan_id_cfg.cfg_e_loop_chan_id_int     = SYS_ELOOP_CHANNEL_ID;
    buf_retrv_chan_id_cfg.cfg_net1_g_chan_en63_to32  = 0xFFFF;
    buf_retrv_chan_id_cfg.cfg_net1_g_chan_en31_to0   = 0xFFFFFFFF;
    buf_retrv_chan_id_cfg.cfg_net_s_g_chan_en63_to32 = 0x00FF0000;
    buf_retrv_chan_id_cfg.cfg_net_s_g_chan_en31_to0  = 0x0;

    net_tx_cfg_chan_id.cfg_int_lk_chan_en_int        = 1;
    net_tx_cfg_chan_id.cfg_int_lk_chan_id_int        = SYS_INTLK_CHANNEL_ID;

        for (chan_id = 0; chan_id < 60; chan_id++)
        {
            if (chan_id <= 59 && chan_id >= 56)
            {
                net_rx_channel_map.chan_id = chan_id - 2;
            }
            else
            {
                net_rx_channel_map.chan_id = chan_id;
            }

            cmd = DRV_IOW(NetRxChannelMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_rx_channel_map));

            net_tx_port_map.chan_id = chan_id;
            cmd = DRV_IOW(NetTxPortIdMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_tx_port_map));

            net_tx_channel_map.port_id = chan_id;
            cmd = DRV_IOW(NetTxChanIdMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &net_tx_channel_map));
        }

        cmd = DRV_IOW(BufRetrvChanIdCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_retrv_chan_id_cfg));

        cmd = DRV_IOW(NetTxCfgChanId_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cfg_chan_id));

    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_mux_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_phy_port_mux_ctl_t ipe_phy_port_mux_ctl;

    sal_memset(&ipe_phy_port_mux_ctl, 0, sizeof(ipe_phy_port_mux_ctl_t));
        cmd = DRV_IOW(IpePhyPortMuxCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_phy_port_mux_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_loopback_hdr_adjust_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_loopback_header_adjust_ctl_t ipe_loopback_header_adjust_ctl;

    sal_memset(&ipe_loopback_header_adjust_ctl, 0, sizeof(ipe_loopback_header_adjust_ctl_t));
    ipe_loopback_header_adjust_ctl.loopback_from_fabric_en = 1;
    ipe_loopback_header_adjust_ctl.always_use_header_logic_port = 1;

        cmd = DRV_IOW(IpeLoopbackHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_loopback_header_adjust_ctl));

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_ipe_hdr_adjust_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_mux_header_adjust_ctl_t ipe_mux_header_adjust_ctl;

    sal_memset(&ipe_mux_header_adjust_ctl, 0, sizeof(ipe_mux_header_adjust_ctl_t));
    ipe_mux_header_adjust_ctl.channel_crc_valid31_0 = 0xFFFFFFFF;
    ipe_mux_header_adjust_ctl.channel_crc_valid63_32 = 0xFFFFFFFF;
        cmd = DRV_IOW(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mux_header_adjust_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_bpdu_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_bpdu_escape_ctl_t ipe_bpdu_escape_ctl;

    sal_memset(&ipe_bpdu_escape_ctl, 0, sizeof(ipe_bpdu_escape_ctl_t));
    ipe_bpdu_escape_ctl.bpdu_escape_en                    = 0x6;
    ipe_bpdu_escape_ctl.bpdu_exception_en                 = 1;
    ipe_bpdu_escape_ctl.bpdu_exception_sub_index          = 17;
    ipe_bpdu_escape_ctl.lldp_exception_en                 = 1;
    ipe_bpdu_escape_ctl.lldp_exception_sub_index          = 18;
    ipe_bpdu_escape_ctl.eapol_exception_en                = 1;
    ipe_bpdu_escape_ctl.eapol_exception_sub_index         = 19;
    ipe_bpdu_escape_ctl.slow_protocol_exception_en        = 1;
    ipe_bpdu_escape_ctl.slow_protocol_exception_sub_index = 20;
        cmd = DRV_IOW(IpeBpduEscapeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bpdu_escape_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_ds_vlan_ctl(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_ds_vlan_ctl_t ipe_ds_vlan_ctl;

    sal_memset(&ipe_ds_vlan_ctl, 0, sizeof(ipe_ds_vlan_ctl_t));
        cmd = DRV_IOR(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));

        ipe_ds_vlan_ctl.ds_stp_state_shift = 0x3;

        cmd = DRV_IOW(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_usrid_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_user_id_ctl_t ipe_user_id_ctl;

    sal_memset(&ipe_user_id_ctl, 0, sizeof(ipe_user_id_ctl_t));
    ipe_user_id_ctl.user_id_mac_key_svlan_first = 1;
    ipe_user_id_ctl.arp_force_ipv4              = 1;
    ipe_user_id_ctl.parser_length_error_mode    = 0;
    ipe_user_id_ctl.layer2_protocol_tunnel_lm_disable = 1;
    ipe_user_id_ctl.force_mac_key = 1;
        cmd = DRV_IOW(IpeUserIdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_user_id_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_ifmap_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_intf_mapper_ctl_t ipe_intf_mapper_ctl;

    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl_t));
    ipe_intf_mapper_ctl.arp_ip_check_en                    = 0;
    ipe_intf_mapper_ctl.arp_dest_mac_check_en              = 0;
    ipe_intf_mapper_ctl.arp_src_mac_check_en               = 0;
    ipe_intf_mapper_ctl.arp_check_exception_en             = 0;
    ipe_intf_mapper_ctl.arp_exception_sub_index            = CTC_L3PDU_ACTION_INDEX_ARP_V2;
    ipe_intf_mapper_ctl.system_mac_en                      = 0;
    ipe_intf_mapper_ctl.system_mac_high                    = 0;
    ipe_intf_mapper_ctl.system_mac_low                     = 0;
    ipe_intf_mapper_ctl.use_port_ptp                       = 0;
    ipe_intf_mapper_ctl.arp_broadcast_routed_port_discard  = 1;
    ipe_intf_mapper_ctl.arp_unicast_discard                = 0;
    ipe_intf_mapper_ctl.arp_unicast_exception_disable      = 1;
    ipe_intf_mapper_ctl.dhcp_broadcast_routed_port_discard = 0;
    ipe_intf_mapper_ctl.dhcp_exception_sub_index           = CTC_L3PDU_ACTION_INDEX_DHCP_V2;
    ipe_intf_mapper_ctl.dhcp_unicast_discard               = 0;
    ipe_intf_mapper_ctl.dhcp_unicast_exception_disable     = 1;
    ipe_intf_mapper_ctl.discard_same_ip_addr               = 0;
    ipe_intf_mapper_ctl.discard_same_mac_addr              = 1;
    ipe_intf_mapper_ctl.i_tag_valid_check                  = 0;
    ipe_intf_mapper_ctl.nca_value                          = 0;
    ipe_intf_mapper_ctl.pbb_bsi_oam_on_igs_cbp_en          = 0;
    ipe_intf_mapper_ctl.pbb_oam_en                         = 0;
    ipe_intf_mapper_ctl.pbb_oui_value                      = 0;
    ipe_intf_mapper_ctl.pbb_tci_nca_check_en               = 0;
    ipe_intf_mapper_ctl.pbb_tci_nca_exception_en           = 0;
    ipe_intf_mapper_ctl.pbb_tci_res2_check_en              = 0;
    ipe_intf_mapper_ctl.pip_invalid_mac_da_check           = 0;
    ipe_intf_mapper_ctl.pip_loop_mac_check                 = 0;
    ipe_intf_mapper_ctl.pip_mac_sa_high                    = 0;
    ipe_intf_mapper_ctl.pip_mac_sa_low                     = 0;
        cmd = DRV_IOW(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_route_mac_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    ipe_router_mac_ctl_t ipe_router_mac_ctl;

    sal_memset(&ipe_router_mac_ctl, 0, sizeof(ipe_router_mac_ctl_t));
    ipe_router_mac_ctl.router_mac_type0_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type0_39_8  = 0x000AFFFF;
    ipe_router_mac_ctl.router_mac_type1_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type1_39_8  = 0x005E0001;
    ipe_router_mac_ctl.router_mac_type2_47_40 = 0;
    ipe_router_mac_ctl.router_mac_type2_39_8  = 0x005E0002;

        cmd = DRV_IOW(IpeRouterMacCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_router_mac_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_mpls_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    ipe_mpls_ctl_t ipe_mpls_ctl;

    sal_memset(&ipe_mpls_ctl, 0, sizeof(ipe_mpls_ctl_t));
    ipe_mpls_ctl.label_base_global                  = 0;
    ipe_mpls_ctl.label_base_global_mcast            = 0;
    ipe_mpls_ctl.label_space_size_type_global       = 6;
    ipe_mpls_ctl.label_space_size_type_global_mcast = 6;
    ipe_mpls_ctl.min_interface_label                = 0;
    ipe_mpls_ctl.min_interface_label_mcast          = 0;
    ipe_mpls_ctl.mpls_ttl_decrement                 = 0;
    ipe_mpls_ctl.mpls_ttl_limit                     = 1;
    ipe_mpls_ctl.oam_alert_label0                   = 14;
    ipe_mpls_ctl.oam_alert_label1                   = 13;
    ipe_mpls_ctl.mpls_offset_bytes_shift            = 2;
    ipe_mpls_ctl.use_first_label_exp                = 1;
    ipe_mpls_ctl.use_first_label_ttl                = 0;
    ipe_mpls_ctl.gal_sbit_check_en                  = 1;
    ipe_mpls_ctl.entropy_label_sbit_check_en        = 1;
    ipe_mpls_ctl.gal_ttl_check_en                   = 1;
    ipe_mpls_ctl.entropy_label_ttl_check_en         = 1;
        cmd = DRV_IOW(IpeMplsCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mpls_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_mpls_exp_map_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_mpls_exp_map_t ipe_mpls_exp_map;

    sal_memset(&ipe_mpls_exp_map, 0, sizeof(ipe_mpls_exp_map_t));
    ipe_mpls_exp_map.priority0 = 0;
    ipe_mpls_exp_map.color0    = 3;
    ipe_mpls_exp_map.priority1 = 8;
    ipe_mpls_exp_map.color1    = 3;
    ipe_mpls_exp_map.priority2 = 16;
    ipe_mpls_exp_map.color2    = 3;
    ipe_mpls_exp_map.priority3 = 24;
    ipe_mpls_exp_map.color3    = 3;
    ipe_mpls_exp_map.priority4 = 32;
    ipe_mpls_exp_map.color4    = 3;
    ipe_mpls_exp_map.priority5 = 40;
    ipe_mpls_exp_map.color5    = 3;
    ipe_mpls_exp_map.priority6 = 48;
    ipe_mpls_exp_map.color6    = 3;
    ipe_mpls_exp_map.priority7 = 56;
    ipe_mpls_exp_map.color7    = 3;
        cmd = DRV_IOW(IpeMplsExpMap_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_mpls_exp_map));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_tunnel_id_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_tunnel_id_ctl_t ipe_tunnel_id_ctl;

    sal_memset(&ipe_tunnel_id_ctl, 0, sizeof(ipe_tunnel_id_ctl_t));
    ipe_tunnel_id_ctl.capwap_dtls_exception_en = 0;
    ipe_tunnel_id_ctl.route_obey_stp           = 1;
        cmd = DRV_IOW(IpeTunnelIdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_tunnel_id_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_mcast_force_route_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_ipv4_mcast_force_route_t ipe_ipv4_mcast_force_route;
    ipe_ipv6_mcast_force_route_t ipe_ipv6_mcast_force_route;

    sal_memset(&ipe_ipv4_mcast_force_route, 0, sizeof(ipe_ipv4_mcast_force_route_t));
    ipe_ipv4_mcast_force_route.addr0_value  = 0;
    ipe_ipv4_mcast_force_route.addr0_mask   = 0xFFFFFFFF;
    ipe_ipv4_mcast_force_route.addr1_value  = 0;
    ipe_ipv4_mcast_force_route.addr1_mask   = 0xFFFFFFFF;

    sal_memset(&ipe_ipv6_mcast_force_route, 0, sizeof(ipe_ipv6_mcast_force_route_t));
    ipe_ipv6_mcast_force_route.addr0_value0 = 0;
    ipe_ipv6_mcast_force_route.addr0_value1 = 0;
    ipe_ipv6_mcast_force_route.addr0_value2 = 0;
    ipe_ipv6_mcast_force_route.addr0_value3 = 0;
    ipe_ipv6_mcast_force_route.addr0_mask0  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask1  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask2  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr0_mask3  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_value0 = 0;
    ipe_ipv6_mcast_force_route.addr1_value1 = 0;
    ipe_ipv6_mcast_force_route.addr1_value2 = 0;
    ipe_ipv6_mcast_force_route.addr1_value3 = 0;
    ipe_ipv6_mcast_force_route.addr1_mask0  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask1  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask2  = 0xFFFFFFFF;
    ipe_ipv6_mcast_force_route.addr1_mask3  = 0xFFFFFFFF;
        cmd = DRV_IOW(IpeIpv4McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv4_mcast_force_route));

        cmd = DRV_IOW(IpeIpv6McastForceRoute_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipv6_mcast_force_route));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_route_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_lookup_route_ctl_t ipe_lookup_route_ctl;
    ipe_route_martian_addr_t ipe_route_martian_addr;
    ipe_route_ctl_t ipe_route_ctl;

    sal_memset(&ipe_route_ctl, 0, sizeof(ipe_route_ctl_t));
    ipe_route_ctl.rpf_type_for_mcast                = 0;
    ipe_route_ctl.exception2_discard                = 0;
    ipe_route_ctl.gre_option2_check_en              = 0;
    ipe_route_ctl.exception3_discard_disable        = 1;
    ipe_route_ctl.exception_sub_index               = 1;
    ipe_route_ctl.mcast_escape_to_cpu               = 0;
    ipe_route_ctl.mcast_rpf_fail_cpu_en             = 1;
    ipe_route_ctl.mcast_address_match_check_disable = 0;
    ipe_route_ctl.ip_options_escape_disable         = 0;
    ipe_route_ctl.offset_byte_shift                 = 0;
    ipe_route_ctl.ip_ttl_limit                      = 2;
    ipe_route_ctl.icmp_check_rpf_en                 = 1;
    ipe_route_ctl.ivi_use_da_info                   = 1;
    ipe_route_ctl.pt_icmp_escape                    = 1;

    sal_memset(&ipe_lookup_route_ctl, 0, sizeof(ipe_lookup_route_ctl_t));
    ipe_lookup_route_ctl.martian_check_en_low          = 0xF;
    ipe_lookup_route_ctl.martian_check_en_high         = 0;
    ipe_lookup_route_ctl.martian_address_check_disable = 0;

    sal_memset(&ipe_route_martian_addr, 0, sizeof(ipe_route_martian_addr_t));

        cmd = DRV_IOW(IpeRouteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_route_ctl));

        cmd = DRV_IOW(IpeLookupRouteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_lookup_route_ctl));

        cmd = DRV_IOW(IpeRouteMartianAddr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_route_martian_addr));
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_excp_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    ipe_exception3_ctl_t ipe_exception3_ctl;
    ipe_exception3_cam_t ipe_exception3_cam;
    ipe_exception3_cam2_t ipe_exception3_cam2;

    sal_memset(&ipe_exception3_ctl, 0, sizeof(ipe_exception3_ctl_t));
    ipe_exception3_ctl.bgp_exception_en         = 1;
    ipe_exception3_ctl.bgp_exception_sub_index  = 22;
    ipe_exception3_ctl.ldp_exception_en         = 1;
    ipe_exception3_ctl.ldp_exception_sub_index  = 23;
    ipe_exception3_ctl.msdp_exception_en        = 1;
    ipe_exception3_ctl.msdp_exception_sub_index = 24;
    ipe_exception3_ctl.ospf_exception_en        = 1;
    ipe_exception3_ctl.ospf_exception_sub_index = 25;
    ipe_exception3_ctl.pim_exception_en         = 1;
    ipe_exception3_ctl.pim_exception_sub_index  = 26;
    ipe_exception3_ctl.rip_exception_en         = 1;
    ipe_exception3_ctl.rip_exception_sub_index  = 27;
    ipe_exception3_ctl.rsvp_exception_en        = 1;
    ipe_exception3_ctl.rsvp_exception_sub_index = 28;
    ipe_exception3_ctl.vrrp_exception_en        = 1;
    ipe_exception3_ctl.vrrp_exception_sub_index = 29;
    ipe_exception3_ctl.exception_cam_en         = 1;
    ipe_exception3_ctl.exception_cam_en2        = 1;

    sal_memset(&ipe_exception3_cam, 0, sizeof(ipe_exception3_cam_t));

    sal_memset(&ipe_exception3_cam2, 0, sizeof(ipe_exception3_cam2_t));
        cmd = DRV_IOW(IpeException3Ctl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_ctl));

        cmd = DRV_IOW(IpeException3Cam_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_cam));

        cmd = DRV_IOW(IpeException3Cam2_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_exception3_cam2));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_brg_stmctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_bridge_storm_ctl_t ipe_bridge_storm_ctl;

    sal_memset(&ipe_bridge_storm_ctl, 0, sizeof(ipe_bridge_storm_ctl_t));
        cmd = DRV_IOW(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_storm_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipe_brg_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    ipe_bridge_ctl_t ipe_bridge_ctl;

    sal_memset(&ipe_bridge_ctl, 0, sizeof(ipe_bridge_ctl_t));
    ipe_bridge_ctl.pbb_mode                       = 0;
    ipe_bridge_ctl.cbp_use_cmac_hash              = 0;
    ipe_bridge_ctl.pnp_use_cmac_hash              = 0;
    ipe_bridge_ctl.discard_force_bridge           = 0;
    ipe_bridge_ctl.multicast_storm_control_mode   = 1;
    ipe_bridge_ctl.unicast_storm_control_mode     = 1;
    ipe_bridge_ctl.use_ip_hash                    = 0;
    ipe_bridge_ctl.protocol_exception             = 0x1440;
    ipe_bridge_ctl.protocol_exception_sub_index0  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index1  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index2  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index3  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index4  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index5  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index6  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index7  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index8  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index9  = 32;
    ipe_bridge_ctl.protocol_exception_sub_index10 = 32;
    ipe_bridge_ctl.protocol_exception_sub_index11 = 32;
    ipe_bridge_ctl.protocol_exception_sub_index12 = 32;
    ipe_bridge_ctl.protocol_exception_sub_index13 = 32;
    ipe_bridge_ctl.protocol_exception_sub_index14 = 32;
    ipe_bridge_ctl.protocol_exception_sub_index15 = 32;
        cmd = DRV_IOW(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_ctl));

    field_val = 0x1a;
    cmd = DRV_IOW(IpeBridgeEopMsgFifo_FifoAlmostFullThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0x1a;
    cmd = DRV_IOW(IpeClaEopMsgFifo_FifoAlmostFullThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_cos_map_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint16 index = 0;
    uint8 priority0 = 0;
    uint8 priority1 = 0;
    uint8 priority2 = 0;
    uint8 priority3 = 0;

    ipe_classification_cos_map_t ipe_classification_cos_map;
    ipe_classification_precedence_map_t ipe_classification_precedence_map;
    ipe_classification_dscp_map_t ipe_classification_dscp_map;
    ipe_classification_ctl_t ipe_classification_ctl;

    sal_memset(&ipe_classification_cos_map, 0, sizeof(ipe_classification_cos_map_t));
    sal_memset(&ipe_classification_precedence_map, 0, sizeof(ipe_classification_precedence_map_t));
    sal_memset(&ipe_classification_dscp_map, 0, sizeof(ipe_classification_dscp_map_t));
    sal_memset(&ipe_classification_ctl, 0, sizeof(ipe_classification_ctl_t));
        for (index = 0; index < 32; index++)
        {
            priority0 = (index % 4) * 8;
            priority1 = priority0;
            priority2 = (index % 4 + 1) * 8;
            priority3 = priority2;
            ipe_classification_cos_map.priority0 = priority0;
            ipe_classification_cos_map.color0    = 3;
            ipe_classification_cos_map.priority1 = priority1;
            ipe_classification_cos_map.color1    = 3;
            ipe_classification_cos_map.priority2 = priority2;
            ipe_classification_cos_map.color2    = 3;
            ipe_classification_cos_map.priority3 = priority3;
            ipe_classification_cos_map.color3    = 3;
            cmd = DRV_IOW(IpeClassificationCosMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ipe_classification_cos_map));
        }

        for (index = 0; index < 16; index++)
        {
            priority0 = (index % 2) * 8;
            priority1 = (index % 2 + 1) * 8;
            priority2 = (index % 2 + 2) * 8;
            priority3 = (index % 2 + 3) * 8;
            ipe_classification_precedence_map.priority0 = priority0;
            ipe_classification_precedence_map.color0    = 3;
            ipe_classification_precedence_map.priority1 = priority1;
            ipe_classification_precedence_map.color1    = 3;
            ipe_classification_precedence_map.priority2 = priority2;
            ipe_classification_precedence_map.color2    = 3;
            ipe_classification_precedence_map.priority3 = priority3;
            ipe_classification_precedence_map.color3    = 3;
            cmd = DRV_IOW(IpeClassificationPrecedenceMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ipe_classification_precedence_map));
        }

        for (index = 0; index < 512; index++)
        {
            priority0 = index % 64;
            priority1 = priority0;
            priority2 = priority0;
            priority3 = priority0;
            ipe_classification_dscp_map.priority0 = priority0;
            ipe_classification_dscp_map.color0    = 3;
            ipe_classification_dscp_map.priority1 = priority1;
            ipe_classification_dscp_map.color1    = 3;
            ipe_classification_dscp_map.priority2 = priority2;
            ipe_classification_dscp_map.color2    = 3;
            ipe_classification_dscp_map.priority3 = priority3;
            ipe_classification_dscp_map.color3    = 3;
            cmd = DRV_IOW(IpeClassificationDscpMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ipe_classification_dscp_map));
        }

        cmd = DRV_IOW(IpeClassificationCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_classification_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_fwd_ctl_init(uint8 lchip)
{
    uint8 lgchip = 0;
    uint32 cmd = 0;

    ipe_fwd_ctl_t ipe_fwd_ctl;

    sal_memset(&ipe_fwd_ctl, 0, sizeof(ipe_fwd_ctl_t));
    ipe_fwd_ctl.flow_stats2_discard_stats_en = 1;
    ipe_fwd_ctl.flow_stats1_discard_stats_en = 1;
    ipe_fwd_ctl.flow_stats0_discard_stats_en = 1;
    ipe_fwd_ctl.stats_mode                   = 0;
    ipe_fwd_ctl.stacking_en                  = 0;
    ipe_fwd_ctl.ecn_priority_critical        = 0;
    ipe_fwd_ctl.oam_discard_bitmap31_0       = 0xFFFFFFFF;
    ipe_fwd_ctl.oam_discard_bitmap63_32      = 0xFFFFFFFF;

    /* init mpls sbit error discard and
     * isatap src check discard
     * according to sys_fatal_exception_t */
    ipe_fwd_ctl.discard_fatal                = 0x208;
    ipe_fwd_ctl.exception2_priority_en       = 0;
    ipe_fwd_ctl.force_except_local_phy_port  = 1;
    ipe_fwd_ctl.header_hash_mode             = 0;
    ipe_fwd_ctl.link_oam_color               = 0;
    ipe_fwd_ctl.link_oam_priority            = 0;
    ipe_fwd_ctl.log_on_discard               = 0x7F;
    ipe_fwd_ctl.oam_bypass_policing_discard  = 0;
    ipe_fwd_ctl.rx_ether_oam_critical        = 0;
    ipe_fwd_ctl.port_extender_mcast_en       = 0;

    ipe_fwd_ctl.cut_through_en               = sys_greatbelt_get_cut_through_en(lchip);
        sys_greatbelt_get_gchip_id(lchip, &lgchip);
        ipe_fwd_ctl.chip_id                      = lgchip;

        cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_tunnell_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    ipe_tunnel_decap_ctl_t ipe_tunnel_decap_ctl;

    sal_memset(&ipe_tunnel_decap_ctl, 0, sizeof(ipe_tunnel_decap_ctl_t));
    ipe_tunnel_decap_ctl.gre_flex_payload_packet_type      = 0;
    ipe_tunnel_decap_ctl.gre_flex_protocol                 = 0;
    ipe_tunnel_decap_ctl.gre_option2_check_en              = 1;
    ipe_tunnel_decap_ctl.ip_ttl_limit                      = 1;
    ipe_tunnel_decap_ctl.offset_byte_shift                 = 1;
    ipe_tunnel_decap_ctl.tunnel_martian_address_check_en   = 1;
    ipe_tunnel_decap_ctl.mcast_address_match_check_disable = 0;
    ipe_tunnel_decap_ctl.trill_ttl                         = 1;
    ipe_tunnel_decap_ctl.trillversion_check_en             = 1;
    ipe_tunnel_decap_ctl.trill_version                     = 0;
    ipe_tunnel_decap_ctl.trill_bfd_check_en                = 1;
    ipe_tunnel_decap_ctl.trill_bfd_hop_count               = 0x30;

        cmd = DRV_IOW(IpeTunnelDecapCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_tunnel_decap_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ipe_trill_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    parser_trill_ctl_t parser_trill_ctl;
    ipe_trill_ctl_t ipe_trill_ctl;

    sal_memset(&parser_trill_ctl, 0, sizeof(parser_trill_ctl_t));
    parser_trill_ctl.trill_inner_tpid                = 0x8200;
    parser_trill_ctl.inner_vlan_tpid_mode            = 1;
    parser_trill_ctl.r_bridge_channel_ether_type     = 0x8946;
    parser_trill_ctl.trill_bfd_oam_channel_protocol0 = 2;
    parser_trill_ctl.trill_bfd_echo_channel_protocol = 3;
    parser_trill_ctl.trill_bfd_oam_channel_protocol1 = 0xFFF;

    sal_memset(&ipe_trill_ctl, 0, sizeof(ipe_trill_ctl_t));
    ipe_trill_ctl.version_check_en        = 1;
    ipe_trill_ctl.trill_bypass_deny_route = 0;

        cmd = DRV_IOW(ParserTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &parser_trill_ctl));

        cmd = DRV_IOW(IpeTrillCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_trill_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_epe_nexthop_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    epe_next_hop_ctl_t epe_next_hop_ctl;

    sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl_t));
    epe_next_hop_ctl.tag_port_bit_map_en             = 1;
    epe_next_hop_ctl.discard_reflective_bridge       = 1;
    epe_next_hop_ctl.force_bridge_l3_match           = 1;
    epe_next_hop_ctl.ds_stp_state_shift              = 3;
    epe_next_hop_ctl.discard_logic_tunnel_match      = 1;
    epe_next_hop_ctl.route_obey_isolate              = 0;
    epe_next_hop_ctl.mirror_obey_discard             = 0;
    epe_next_hop_ctl.deny_duplicate_mirror           = 1;
    epe_next_hop_ctl.parser_error_ignore             = 0;
    epe_next_hop_ctl.vlan_stats_en                   = 0;
    epe_next_hop_ctl.discard_same_ip_addr            = 0;
    epe_next_hop_ctl.discard_same_mac_addr           = 1;
    epe_next_hop_ctl.cbp_tci_res2_check_en           = 0;
    epe_next_hop_ctl.pbb_bsi_oam_on_egs_cbp_en       = 0;
    epe_next_hop_ctl.terminate_pbb_bv_oam_on_egs_pip = 0;
    epe_next_hop_ctl.pbb_bv_oam_on_egs_pip_en        = 0;
    epe_next_hop_ctl.cbp_tci_res2_check_en           = 0;
    epe_next_hop_ctl.stacking_en                     = 0;
    epe_next_hop_ctl.route_obey_stp                  = 1;
    epe_next_hop_ctl.parser_length_error_mode        = 0x3;
    epe_next_hop_ctl.oam_ignore_payload_operation    = 1;
    epe_next_hop_ctl.ether_oam_use_payload_operation = 1;

        cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_epe_hdr_edit_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 gchip = 0;

    epe_header_edit_ctl_t epe_header_edit_ctl;

    sal_memset(&epe_header_edit_ctl, 0, sizeof(epe_header_edit_ctl_t));
    epe_header_edit_ctl.log_on_discard               = 0x7F;
    epe_header_edit_ctl.log_port_discard             = 0;
    epe_header_edit_ctl.stats_mode                   = 0;
    epe_header_edit_ctl.flow_stats0_discard_stats_en = 1;
    epe_header_edit_ctl.flow_stats1_discard_stats_en = 1;
    epe_header_edit_ctl.flow_stats2_discard_stats_en = 1;
    epe_header_edit_ctl.rx_ether_oam_critical        = 1;
    epe_header_edit_ctl.stacking_en                  = 0;
    epe_header_edit_ctl.evb_tpid                     = 0;
    epe_header_edit_ctl.oam_discard_bitmap63_32      = 0xFFFFFFFF;
    epe_header_edit_ctl.oam_discard_bitmap31_0       = 0xFFFFFFFF;
    epe_header_edit_ctl.loopback_use_source_port     = 0;
        sys_greatbelt_get_gchip_id(lchip, &gchip);
        epe_header_edit_ctl.chip_id      = gchip;
        cmd = DRV_IOW(EpeHeaderEditCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_header_edit_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_epe_pkt_proc_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    epe_pkt_proc_ctl_t epe_pkt_proc_ctl;

    sal_memset(&epe_pkt_proc_ctl, 0, sizeof(epe_pkt_proc_ctl_t));
    epe_pkt_proc_ctl.discard_route_ttl0           = 1;
    epe_pkt_proc_ctl.discard_mpls_ttl0            = 1;
    epe_pkt_proc_ctl.discard_mpls_tag_ttl0        = 1;
    epe_pkt_proc_ctl.discard_tunnel_ttl0          = 1;
    epe_pkt_proc_ctl.discard_trill_ttl0           = 1;
    epe_pkt_proc_ctl.use_global_ctag_cos          = 0;
    epe_pkt_proc_ctl.mirror_escape_cam_en         = 0;
    epe_pkt_proc_ctl.icmp_err_msg_check_en        = 1;
    epe_pkt_proc_ctl.global_ctag_cos              = 0;
    epe_pkt_proc_ctl.capwap_roaming_state_bits    = 0;
    epe_pkt_proc_ctl.pt_multicast_address_en      = 0;
    epe_pkt_proc_ctl.pt_udp_checksum_zero_discard = 1;
    epe_pkt_proc_ctl.next_hop_stag_ctl_en         = 1;

        cmd = DRV_IOW(EpePktProcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_pkt_proc_ctl));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_epe_port_mac_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    epe_l2_port_mac_sa_t epe_l2_port_mac_sa;
    net_tx_force_tx_cfg_t net_tx_force;

    sal_memset(&epe_l2_port_mac_sa, 0, sizeof(epe_l2_port_mac_sa_t));
    sal_memset(&net_tx_force, 0, sizeof(net_tx_force_tx_cfg_t));

        cmd = DRV_IOW(EpeL2PortMacSa_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_l2_port_mac_sa));
        net_tx_force.force_tx_port31_to0 = 0xffffffff;
        net_tx_force.force_tx_port59_to32 = 0xfffffff;
        cmd = DRV_IOW(NetTxForceTxCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_force));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_epe_cos_map_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint16 index = 0;
    uint8 dscp = 0;
    uint8 exp = 0;
    uint8 cos = 0;
    uint8 i_value = 0;
    epe_edit_priority_map_t epe_edit_priority_map;

    sal_memset(&epe_edit_priority_map, 0, sizeof(epe_edit_priority_map_t));
        for (index = 0; index < 2048; index++)
        {
            if (0 == i_value % 256)
            {
                i_value = 0;
            }

            dscp = i_value / 4;
            exp  = i_value / 32;
            cos  = i_value / 32;
            i_value++;

            epe_edit_priority_map.exp  = exp;
            epe_edit_priority_map.dscp = dscp;
            epe_edit_priority_map.cfi  = 0;
            epe_edit_priority_map.cos  = cos;

            cmd = DRV_IOW(EpeEditPriorityMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &epe_edit_priority_map));
        }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_queue_hash_ctl_init(uint8 lchip)
{
    uint32 cmd = 0;

    q_hash_cam_ctl_t q_hash_cam_ctl;
    q_mgr_enq_length_adjust_t q_mgr_enq_length_adjust;

    sal_memset(&q_hash_cam_ctl, 0, sizeof(q_hash_cam_ctl_t));
    q_hash_cam_ctl.valid0        = 0;
    q_hash_cam_ctl.valid1        = 0;
    q_hash_cam_ctl.dest_chip_id0 = 0;
    q_hash_cam_ctl.dest_chip_id1 = 0;
    q_hash_cam_ctl.valid2        = 0;
    q_hash_cam_ctl.valid3        = 0;
    q_hash_cam_ctl.dest_chip_id2 = 0;
    q_hash_cam_ctl.dest_chip_id3 = 0;
    q_hash_cam_ctl.valid4        = 0;
    q_hash_cam_ctl.valid5        = 0;
    q_hash_cam_ctl.dest_chip_id4 = 0;
    q_hash_cam_ctl.dest_chip_id5 = 0;
    q_hash_cam_ctl.valid6        = 0;
    q_hash_cam_ctl.valid7        = 0;
    q_hash_cam_ctl.dest_chip_id6 = 0;
    q_hash_cam_ctl.dest_chip_id7 = 0;

    sal_memset(&q_mgr_enq_length_adjust, 0, sizeof(q_mgr_enq_length_adjust_t));
    q_mgr_enq_length_adjust.adjust_length1 = 18;
        cmd = DRV_IOW(QHashCamCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_hash_cam_ctl));

        cmd = DRV_IOW(QMgrEnqLengthAdjust_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_length_adjust));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_buffse_store_ctl_init(uint8 lchip)
{
    uint8 lgchip = 0;
    uint32 cmd = 0;

    buffer_store_ctl_t buffer_store_ctl;
    buffer_retrieve_ctl_t buffer_retrieve_ctl;
    buffer_store_force_local_ctl_t buffer_store_force_local_ctl;
    met_fifo_ctl_t met_fifo_ctl;
    q_write_ctl_t q_write_ctl;
    uint32 filed_value      = 0;

    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl_t));
    buffer_store_ctl.chip_id_check_disable            = 0;

    buffer_store_ctl.resrc_id_use_local_phy_port31_0  = 0;
    buffer_store_ctl.resrc_id_use_local_phy_port63_32 = 0;
    buffer_store_ctl.local_switching_disable          = 0;
    buffer_store_ctl.mcast_met_fifo_enable            = 1;
    buffer_store_ctl.cpu_port                         = SYS_RESERVE_PORT_ID_CPU;
    buffer_store_ctl.cpu_rx_exception_en0             = 0;
    buffer_store_ctl.cpu_rx_exception_en1             = 0;
    buffer_store_ctl.cpu_tx_exception_en              = 0;

    sal_memset(&buffer_retrieve_ctl, 0, sizeof(buffer_retrieve_ctl_t));
    buffer_retrieve_ctl.color_map_en = 0;
    buffer_retrieve_ctl.exception_follow_mirror = 0;
    sal_memset(&met_fifo_ctl, 0, sizeof(met_fifo_ctl_t));
    met_fifo_ctl.port_check_en    = 1;

    sal_memset(&q_write_ctl, 0, sizeof(q_write_ctl_t));
    q_write_ctl.random_drop_threshold = 0;
    /*use 7 queue select types*/
    q_write_ctl.queue_sel_type_bits = 1;
    /*for oam enqueue*/

    q_write_ctl.link_change_en = 1;
    q_write_ctl.gen_que_id_rx_ether_oam         = 1;
    q_write_ctl.rx_ether_oam_queue_select_shift = 3;
    q_write_ctl.rx_ether_oam_queue_base0        = 480;
    q_write_ctl.rx_ether_oam_queue_base1        = 480;
    q_write_ctl.rx_ether_oam_queue_base2        = 480;
    q_write_ctl.rx_ether_oam_queue_base3        = 480;
    q_write_ctl.rx_ether_oam_queue_base4        = 480;
    q_write_ctl.rx_ether_oam_queue_base5        = 480;
    q_write_ctl.rx_ether_oam_queue_base6        = 480;
    q_write_ctl.rx_ether_oam_queue_base7        = 480;

    sal_memset(&buffer_store_force_local_ctl, 0, sizeof(buffer_store_force_local_ctl_t));
    buffer_store_force_local_ctl.dest_map_mask0  = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_value0 = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_mask1  = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_value1 = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_mask2  = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_value2 = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_mask3  = 0x3FFFFF;
    buffer_store_force_local_ctl.dest_map_value3 = 0x3FFFFF;
        sys_greatbelt_get_gchip_id(lchip, &lgchip);

        buffer_store_ctl.chip_id      = lgchip;
        buffer_retrieve_ctl.chip_id   = lgchip;
        met_fifo_ctl.chip_id          = lgchip;
        q_write_ctl.chip_id           = lgchip;

        cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

        cmd = DRV_IOW(BufferStoreForceLocalCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_force_local_ctl));

        cmd = DRV_IOW(BufferRetrieveCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_retrieve_ctl));

        cmd = DRV_IOW(MetFifoCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &met_fifo_ctl));

        cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));

        filed_value = 12;
        cmd = DRV_IOW(BufStoreMiscCtl_t, BufStoreMiscCtl_MinPktSize_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &filed_value));

    return CTC_E_NONE;

}

/* add default entry adindex=0 for ipuc */
int32
_sys_greatbelt_mac_default_entry_init(uint8 lchip)
{
    ds_mac_t macda;
    uint32 cmd = 0;

    sal_memset(&macda, 0, sizeof(ds_mac_t));

    macda.ds_fwd_ptr = 0xFFFF;
        /* build DsMac entry */
        cmd = DRV_IOW(DsMac_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &macda));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ipg_init(uint8 lchip)
{
    uint32 cmd = 0;
    ipe_ipg_ctl_t  ipe_ipg_ctl;
    epe_ipg_ctl_t  epe_ipg_ctl;
    sal_memset(&ipe_ipg_ctl, 0, sizeof(ipe_ipg_ctl));
    sal_memset(&epe_ipg_ctl, 0, sizeof(epe_ipg_ctl));

    ipe_ipg_ctl.ipg0 = 20;
    ipe_ipg_ctl.ipg1 = 20;
    ipe_ipg_ctl.ipg2 = 20;
    ipe_ipg_ctl.ipg3 = 20;

    epe_ipg_ctl.ipg0 = 20;
    epe_ipg_ctl.ipg1 = 20;
    epe_ipg_ctl.ipg2 = 20;
    epe_ipg_ctl.ipg3 = 20;

        cmd = DRV_IOW(IpeIpgCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ipg_ctl));

        cmd = DRV_IOW(EpeIpgCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_ipg_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_peri_init(uint8 lchip)
{
    uint32 core_freq = 0;
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 index = 0;
    mac_led_blink_cfg_t led_blink;
    mdio_xg_port_chan_id_with_out_phy_t mdio_mapping;
    uint8 chan_id[12] = {0};

    sal_memset(&mdio_mapping, 0, sizeof(mdio_xg_port_chan_id_with_out_phy_t));

    /* 2. init mdio clock */
        field_val = 1;
        cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetMdio_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        core_freq = sys_greatbelt_get_core_freq(lchip);
        field_val = (core_freq*10)/25;
        cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_Mdio1GClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = field_val;
        cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_MdioXGClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = 0;
        cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdio1GClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = 0;
        cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdioXGClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = 0;
        cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetMdio_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* 3. init MdioUsePhy bitmap to control mac connect phy or sfp */
    /* 4. init port to phy, port to channel mapping, this is only use for APS */
    CTC_ERROR_RETURN(_sys_greatbelt_reg_mdio_scan_init(lchip));
    /* start auto scan */
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_scan_en(lchip, TRUE));

        for (index = 0; index < 12; index++)
        {
            cmd = DRV_IOR(NetRxChannelMap_t, NetRxChannelMap_ChanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (index+SYS_MAX_GMAC_PORT_NUM), cmd, &field_val));
            chan_id[index] = (uint8)field_val;
        }

        mdio_mapping.sgmac0_chan_id = chan_id[0];
        mdio_mapping.sgmac1_chan_id = chan_id[1];
        mdio_mapping.sgmac2_chan_id = chan_id[2];
        mdio_mapping.sgmac3_chan_id = chan_id[3];
        mdio_mapping.sgmac4_chan_id = chan_id[4];
        mdio_mapping.sgmac5_chan_id = chan_id[5];
        mdio_mapping.sgmac6_chan_id = chan_id[6];
        mdio_mapping.sgmac7_chan_id = chan_id[7];
        mdio_mapping.sgmac8_chan_id = chan_id[8];
        mdio_mapping.sgmac9_chan_id = chan_id[9];
        mdio_mapping.sgmac10_chan_id = chan_id[10];
        mdio_mapping.sgmac11_chan_id = chan_id[11];

        cmd = DRV_IOW(MdioXgPortChanIdWithOutPhy_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mdio_mapping));

    /* 5. cfg mac led default interval, notes: do not change these interval, it is based on led clock and mac led number */
    sal_memset(&led_blink, 0, sizeof(mac_led_blink_cfg_t));
        led_blink.blink_on_interval = 0x7E25BBC;
        led_blink.blink_off_interval = 0x7E25BBC;
        cmd = DRV_IOW(MacLedBlinkCfg_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_blink));

        field_val = 0xFC4B778;
        cmd = DRV_IOW(MacLedSampleInterval_t, MacLedSampleInterval_SampleInterval_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = 0x0800000;
        cmd = DRV_IOW(MacLedRefreshInterval_t, MacLedRefreshInterval_RefreshInterval_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* 6. cfg sensor sleep */
        field_val = 0;
        cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorSleep_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    /* 7 cfg clock divider */

        field_val = 0x2710;
        cmd = DRV_IOW(I2CMasterCfg_t, I2CMasterCfg_ClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));



    return CTC_E_NONE;
}

int32
sys_greatbelt_device_feature_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint8  gchip = 0;
    uint16 gport = 0;
    ctc_chip_device_info_t device_info;
    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));

    CTC_ERROR_RETURN(sys_greatbelt_chip_get_device_info(lchip, &device_info));

    if ((SYS_CHIP_DEVICE_ID_GB_5160 == device_info.device_id) ||
        (SYS_CHIP_DEVICE_ID_RM_5120 == device_info.device_id))
    {
        /* all feature support */
    }
    else if ((SYS_CHIP_DEVICE_ID_GB_5162 == device_info.device_id) ||
             (SYS_CHIP_DEVICE_ID_GB_5163 == device_info.device_id))
    {
        field_value = 0;
        /* disable tp1731 */
        cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_AchY1731ChanType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


        /* disable bfd */
        cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_AchBfdChanTypeCc_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        cmd = DRV_IOW(OamParserEtherCtl_t, OamParserEtherCtl_AchBfdChanTypeCv_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_BfdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


        /* disable ptp */
        cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_Ieee1588TypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* disable sync-E */
        field_value = 1;
        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg0EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg1EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg2EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg3EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    }
    else if ((SYS_CHIP_DEVICE_ID_RT_3162 == device_info.device_id) ||
             (SYS_CHIP_DEVICE_ID_RT_3163 == device_info.device_id))
    {
        /* disable gre, V4InV6, V4InV4, V6InV4, V6InV6 */
        field_value = 0;
        cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, ParserLayer3ProtocolCtl_GreTypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, ParserLayer3ProtocolCtl_IpinipTypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, ParserLayer3ProtocolCtl_Ipinv6TypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, ParserLayer3ProtocolCtl_V6inipTypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(ParserLayer3ProtocolCtl_t, ParserLayer3ProtocolCtl_V6inv6TypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* disable mpls */
        cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_MplsTypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* disable oam */
        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, SYS_RESERVE_PORT_ID_DROP);
        field_value = SYS_GET_CHANNEL_ID(lchip, gport);
        cmd = DRV_IOW(BufRetrvChanIdCfg_t, BufRetrvChanIdCfg_CfgOamChanIdInt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


        /* disable ptp */
        field_value = 0;
        cmd = DRV_IOW(ParserEthernetCtl_t, ParserEthernetCtl_Ieee1588TypeEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        cmd = DRV_IOW(ParserLayer4AppCtl_t, ParserLayer4AppCtl_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* disable sync-E */
        field_value = 1;
        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg0EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg1EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg2EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(SyncEthernetClkCfg_t, SyncEthernetClkCfg_Cfg3EtherUserOff_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_capability_init(uint8 lchip)
{
    uint32 size = 0;
    uint8 index = 0;

    /* init to invalid value 0xFFFFFFFF */
    for (index = 0; index < CTC_GLOBAL_CAPABILITY_MAX; index++)
    {
        p_gb_register_master[lchip]->chip_capability[index] = 0xFFFFFFFF;
    }

    size = TABLE_MAX_INDEX(DsUserIdVlanKey_t)/2 + TABLE_MAX_INDEX(DsUserIdMacKey_t) + TABLE_MAX_INDEX(DsUserIdIpv4Key_t)*2 + TABLE_MAX_INDEX(DsUserIdIpv6Key_t)*4 +
        TABLE_MAX_INDEX(DsTunnelIdIpv4Key_t) + TABLE_MAX_INDEX(DsTunnelIdIpv6Key_t)*4;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL_HASH_ENTRY_NUM] = TABLE_MAX_INDEX(DsUserIdPortHashKey_t)/2;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL_TCAM_ENTRY_NUM] = size;

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_dyn_entry_size(lchip, SYS_FTM_DYN_ENTRY_GLB_MET, &size));
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MCAST_GROUP_NUM] = size;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_SCL_HASH_ENTRY_NUM] = TABLE_MAX_INDEX(DsUserIdPortHashKey_t)/2;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_L2HDR_PROTO_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_L2HDR_PTL_ENTRY;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_MACDA_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_MACDA_ENTRY;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_MACDA_LOW24_ENTRY_NUM] = MAX_SYS_L2PDU_BASED_MACDA_LOW24_ENTRY;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L2PDU_L2CP_MAX_ACTION_INDEX] = SYS_MAX_L2PDU_PER_PORT_ACTION_INDEX;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_L3HDR_PROTO_ENTRY_NUM] = MAX_SYS_L3PDU_BASED_L3HDR_PROTO;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_L4PORT_ENTRY_NUM] = MAX_SYS_L3PDU_BASED_PORT;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_IPDA_ENTRY_NUM] = 0;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3PDU_MAX_ACTION_INDEX] = SYS_MAX_L3PDU_PER_L3IF_ACTION_INDEX;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL_HASH_ENTRY_NUM] = TABLE_MAX_INDEX(DsAclQosIpv4HashKey_t);
    size = 2*TABLE_MAX_INDEX(DsAclMacKey0_t)+2*TABLE_MAX_INDEX(DsAclIpv4Key0_t)+2*TABLE_MAX_INDEX(DsAclMplsKey0_t)+4*TABLE_MAX_INDEX(DsAclIpv6Key0_t);
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_IGS_TCAM_ENTRY_NUM] = size;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL0_EGS_TCAM_ENTRY_NUM] = size;
    size = 2*TABLE_MAX_INDEX(DsAclMacKey1_t)+4*TABLE_MAX_INDEX(DsAclIpv6Key1_t);
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL1_IGS_TCAM_ENTRY_NUM] = size;
    size = 2*TABLE_MAX_INDEX(DsAclMacKey2_t);
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL2_IGS_TCAM_ENTRY_NUM] = size;
    size = 2*TABLE_MAX_INDEX(DsAclMacKey3_t);
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ACL3_IGS_TCAM_ENTRY_NUM] = size;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_VLAN_NUM] = CTC_MAX_VLAN_ID+1;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_VLAN_RANGE_GROUP_NUM] = TABLE_MAX_INDEX(DsVlanRangeProfile_t);

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MPLS_ENTRY_NUM] = TABLE_MAX_INDEX(DsMpls_t);

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_OAM_SESSION_NUM] =  TABLE_MAX_INDEX(DsEthMep_t) / 2;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_APS_GROUP_NUM] = SYS_GB_MAX_APS_GROUP_NUM;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_NPM_SESSION_NUM] = 4;

    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MAC_ENTRY_NUM] = TABLE_MAX_INDEX(DsMacHashKey_t);
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_STP_INSTANCE_NUM] = 128;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_ROUTE_MAC_ENTRY_NUM] = 256*2;
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_L3IF_NUM] = SYS_GB_MAX_CTC_L3IF_ID+1;/*0 is used to disable L3if in asic*/
    p_gb_register_master[lchip]->chip_capability[CTC_GLOBAL_CAPABILITY_MAX_CHIP_NUM] = 32;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatblet_register_oam_init(uint8 lchip)/*for c2c*/
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 gchip_id = 0;

    field_val = 0xFFFFFFFF;
    cmd = DRV_IOW(OamHeaderEditCtl_t, OamHeaderEditCtl_CpuExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip_id));
    field_val = gchip_id;              /*global chipid*/
    cmd = DRV_IOW(IpeOamCtl_t, IpeOamCtl_LinkOamDestChipId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_register_init(uint8 lchip)
{
    if (NULL != p_gb_register_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_ZERO(MEM_SYSTEM_MODULE, p_gb_register_master[lchip], sizeof(sys_register_master_t));

    if (NULL == p_gb_register_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

#if 1 /*For Co-Sim fast*/
      /*edram and sram clear */
    CTC_ERROR_RETURN(sys_greatbelt_tcam_edram_init(lchip));
#endif

    /* mpls */
    CTC_ERROR_RETURN(_sys_greatbelt_mpls_ctl_init(lchip));

    /* nexthop */
    CTC_ERROR_RETURN(_sys_greatbelt_nexthop_ctl_init(lchip));

    /* exception */
    CTC_ERROR_RETURN(_sys_greatbelt_excp_ctl_init(lchip));

    /* net tx and rx */
    CTC_ERROR_RETURN(_sys_greatbelt_net_rx_tx_init(lchip));

    /* mux ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_mux_ctl_init(lchip));

    /* ipe hdr adjust ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_hdr_adjust_ctl_init(lchip));

    /* ipe loopback hdr adjust ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_loopback_hdr_adjust_ctl_init(lchip));

    /* bpdu ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_bpdu_ctl_init(lchip));

    /* ipe dsvlan ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_ds_vlan_ctl(lchip));

    /* ipe userid ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_usrid_ctl_init(lchip));

    /* ipe ifmap ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_ifmap_ctl_init(lchip));

    /* ipe route mac ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_route_mac_ctl_init(lchip));

    /* ipe mpls ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_mpls_ctl_init(lchip));

    /* ipe mpls exp map */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_mpls_exp_map_init(lchip));

    /* ipe tunnel id ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_tunnel_id_ctl_init(lchip));

    /* ipe mcast force route ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_mcast_force_route_init(lchip));

    /* ipe route ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_route_ctl_init(lchip));

    /* ipe exception ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_excp_ctl_init(lchip));

    /* ipe brige  storm ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_brg_stmctl_init(lchip));

    /* ipe brige ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_brg_ctl_init(lchip));

    /* ipe cos mapping */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_cos_map_init(lchip));

    /* ipe fwd ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_fwd_ctl_init(lchip));

    /* ipe tunnel ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_tunnell_ctl_init(lchip));

    /* ipe trill ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_ipe_trill_ctl_init(lchip));

    /* epe nexthop ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_epe_nexthop_ctl_init(lchip));

    /* epe hdr edit ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_epe_hdr_edit_ctl_init(lchip));

    /* epe pkt proc ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_epe_pkt_proc_ctl_init(lchip));

    /* epe port mac ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_epe_port_mac_ctl_init(lchip));

    /* epe cos mapping ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_epe_cos_map_init(lchip));

    /* queue hash ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_queue_hash_ctl_init(lchip));

    /* buff store ctl */
    CTC_ERROR_RETURN(_sys_greatbelt_buffse_store_ctl_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_mac_default_entry_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_lkp_ctl_init(lchip));

    /* Ipg init */
    CTC_ERROR_RETURN(_sys_greatbelt_ipg_init(lchip));

    /* peri io init */
    CTC_ERROR_RETURN(_sys_greatbelt_peri_init(lchip));

    CTC_ERROR_RETURN(_sys_greatbelt_chip_capability_init(lchip));

    CTC_ERROR_RETURN(_sys_greatblet_register_oam_init(lchip));

    return CTC_E_NONE;
}

int32
sys_greatbelt_register_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_register_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gb_register_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_global_get_chip_capability(uint8 lchip, uint32* p_capability)
{
    CTC_PTR_VALID_CHECK(p_capability);

    sal_memcpy(p_capability, p_gb_register_master[lchip]->chip_capability, CTC_GLOBAL_CAPABILITY_MAX*sizeof(uint32));

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_global_ctl_l4_check_en(uint8 lchip,  ctc_global_control_type_t type, uint32* value, uint8 is_set)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8 bit = 0;

    switch (type)
    {
        case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
            bit = 0;
            break;
        case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
            bit = 1;
            break;
        case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
            bit = 2;
            break;
        case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
            bit = 3;
            break;
        case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
            bit = 4;
            break;
        case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
            bit = 5;
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }

    cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_Layer4SecurityCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    if (is_set)
    {
        if (*value)
        {
            CTC_BIT_SET(field_val, bit);
        }
        else
        {
            CTC_BIT_UNSET(field_val, bit);
        }
        cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_Layer4SecurityCheckEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        *value = CTC_IS_BIT_SET(field_val, bit)? 1 : 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_global_ctl_set_arp_check_en(uint8 lchip, ctc_global_control_type_t type, uint32* value)
{
    uint32 cmd = 0;
    switch (type)
    {
        case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpDestMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpSrcMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_IP_CHECK_EN:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpIpCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpCheckExceptionEn_f);
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_global_ctl_get_arp_check_en(uint8 lchip, ctc_global_control_type_t type, uint32* value)
{
    uint32 cmd = 0;

    switch (type)
    {
        case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpDestMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpSrcMacCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_IP_CHECK_EN:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpIpCheckEn_f);
            break;
        case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
            cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpCheckExceptionEn_f);
            break;
        default:
            return CTC_E_NOT_SUPPORT;
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    return CTC_E_NONE;

}


int32
sys_greatbelt_global_ctl_set(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint8  enable = 0;

    switch(type)
    {
    case CTC_GLOBAL_DISCARD_SAME_MACDASA_PKT:
        field_val = *(bool *)value ? 1 : 0;
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_DiscardSameMacAddr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_DiscardSameMacAddr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_DISCARD_SAME_IPDASA_PKT:
        field_val = *(bool *)value ? 1 : 0;
            cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_DiscardSameIpAddr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_DiscardSameIpAddr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
    case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
    case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
    case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
    case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
    case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
        CTC_ERROR_RETURN(_sys_greatbelt_global_ctl_l4_check_en(lchip, type, ((uint32*)value), 1));
        break;

    case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
    case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
    case CTC_GLOBAL_ARP_IP_CHECK_EN:
    case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
        CTC_ERROR_RETURN(_sys_greatbelt_global_ctl_set_arp_check_en(lchip, type, ((uint32*)value)));
        break;

    case CTC_GLOBAL_LINKAGG_FLOW_INACTIVE_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFF);
        field_val = (*(uint32*)value);
        cmd = DRV_IOW(QLinkAggTimerCtl0_t, QLinkAggTimerCtl0_TsThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_GLOBAL_STACKING_TRUNK_FLOW_INACTIVE_INTERVAL:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0xFF);
        field_val = (*(uint32*)value);
        cmd = DRV_IOW(QLinkAggTimerCtl2_t, QLinkAggTimerCtl2_TsThreshold2_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_PIM_SNOOPING_MODE:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 1);
        field_val = (*(uint32*)value) ? 0 : 1;
        cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_PimSnoopingEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_NH_FORCE_BRIDGE_DISABLE:
        field_val = (*(uint32*)value)?0:1;
        cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_ForceBridgeL3Match_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;
    case CTC_GLOBAL_NH_MCAST_LOGIC_REP_EN:
        enable = *(bool *)value ? 1 : 0;
        CTC_ERROR_RETURN(sys_greatbelt_nh_set_ipmc_logic_replication(lchip, enable));
        break;
    default:
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;

}


STATIC int32
_sys_greatbelt_global_get_panel_ports(uint8 lchip, ctc_global_panel_ports_t* phy_info)
{
    uint8 mac_id = 0;
    uint16 lport = 0;
    uint32 gport = 0;
    uint8 count = 0;

    lchip = phy_info->lchip;
    for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
    {
        gport = (lchip << 8) | lport;
        mac_id = sys_greatebelt_common_get_mac_id(lchip, gport);

        if (0xFF == mac_id)
        {
            continue;
        }

        phy_info->lport[count++] = lport;

    }
    phy_info->count = count;

    return CTC_E_NONE;
}


int32
sys_greatbelt_global_ctl_get(uint8 lchip, ctc_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    switch(type)
    {
    case CTC_GLOBAL_DISCARD_SAME_MACDASA_PKT:
        cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_DiscardSameMacAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_GLOBAL_DISCARD_SAME_IPDASA_PKT:
        cmd = DRV_IOR(IpeIntfMapperCtl_t, IpeIntfMapperCtl_DiscardSameIpAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_GLOBAL_DISCARD_TCP_SYN_0_PKT:
    case CTC_GLOBAL_DISCARD_TCP_NULL_PKT:
    case CTC_GLOBAL_DISCARD_TCP_XMAS_PKT:
    case CTC_GLOBAL_DISCARD_TCP_SYN_FIN_PKT:
    case CTC_GLOBAL_DISCARD_SAME_L4_PORT_PKT:
    case CTC_GLOBAL_DISCARD_ICMP_FRAG_PKT:
        CTC_ERROR_RETURN(_sys_greatbelt_global_ctl_l4_check_en(lchip, type, ((uint32*)value), 0));
        break;

    case CTC_GLOBAL_ARP_MACDA_CHECK_EN:
    case CTC_GLOBAL_ARP_MACSA_CHECK_EN:
    case CTC_GLOBAL_ARP_IP_CHECK_EN:
    case CTC_GLOBAL_ARP_CHECK_FAIL_TO_CPU:
        CTC_ERROR_RETURN(_sys_greatbelt_global_ctl_get_arp_check_en(lchip, type, ((uint32*)value)));
        break;

    case CTC_GLOBAL_CHIP_CAPABILITY:
        CTC_ERROR_RETURN(_sys_greatbelt_global_get_chip_capability(lchip, (uint32*)value));
        break;

    case CTC_GLOBAL_LINKAGG_FLOW_INACTIVE_INTERVAL:
        cmd = DRV_IOR(QLinkAggTimerCtl0_t, QLinkAggTimerCtl0_TsThreshold0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;

    case CTC_GLOBAL_STACKING_TRUNK_FLOW_INACTIVE_INTERVAL:
        cmd = DRV_IOR(QLinkAggTimerCtl2_t, QLinkAggTimerCtl2_TsThreshold2_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val;
        break;

    case CTC_GLOBAL_PIM_SNOOPING_MODE:
        cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_PimSnoopingEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32*)value = field_val ? 0 : 1;
        break;


    case CTC_GLOBAL_PANEL_PORTS:
        _sys_greatbelt_global_get_panel_ports(lchip, (ctc_global_panel_ports_t*)value);
        break;
    case CTC_GLOBAL_NH_FORCE_BRIDGE_DISABLE:
        cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_ForceBridgeL3Match_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = (field_val)?FALSE:TRUE;
        break;
    case CTC_GLOBAL_NH_MCAST_LOGIC_REP_EN:
        *(bool *)value = sys_greatbelt_nh_is_ipmc_logic_rep_enable(lchip);
        break;
    default:
        return CTC_E_NOT_SUPPORT;

    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_global_set_chip_capability(uint8 lchip, ctc_global_capability_type_t type, uint32 value)
{

    p_gb_register_master[lchip]->chip_capability[type] = value;

    return CTC_E_NONE;
}

int32
sys_greatbelt_global_set_xgpon_en(uint8 lchip, uint8 enable)
{

    if (NULL == p_gb_register_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    p_gb_register_master[lchip]->xgpon_en = enable;

    return CTC_E_NONE;
}

int32
sys_greatbelt_global_get_xgpon_en(uint8 lchip, uint8* enable)
{
    if (NULL == p_gb_register_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_PTR_VALID_CHECK(enable);


    *enable = p_gb_register_master[lchip]->xgpon_en;

    return CTC_E_NONE;
}


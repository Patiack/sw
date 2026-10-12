/**
 @file sys_goldengate_chip.c

 @date 2009-10-19

 @version v2.0

 The file define APIs of chip of sys layer
*/
/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/
#include "ctc_error.h"
#include "ctc_warmboot.h"
#include "ctc_register.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_oam.h"
#include "sys_goldengate_packet.h"
#include "sys_goldengate_fdb_sort.h"
#include "sys_goldengate_l2_fdb.h"

#include "goldengate/include/drv_lib.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_chip_ctrl.h"
#include "dal.h"

/* feature support array, 1 means enable, 0 means disable */
uint8 gg_feature_support_arr[CTC_FEATURE_MAX] =
    /*PORT VLAN LINKAGG CHIP FTM NEXTHOP L2 L3IF IPUC IPMC IP_TUNNEL SCL ACL QOS SECURITY STATS MPLS OAM APS PTP DMA INTERRUPT PACKET PDU MIRROR BPE STACKING OVERLAY IPFIX EFD MONITOR FCOE TRILL*/
    {1,     1,     1,   1,   1,  1,      1,   1,   1,  1,  1,        1,  1,  1,       1,  1,    1,   1,  1,  1,  1,   1,       1,     1,  1,     1,  1,       1,      1,    1,  1,      1,    1  };


struct sys_chip_serdes_mac_info_s
{
    char* p_datapath_mode;
    uint8 serdes_id[60];
};
typedef struct sys_chip_serdes_mac_info_s sys_chip_serdes_mac_info_t;

struct sys_chip_i2c_slave_bitmap_s
{
    uint32 slave_bitmap1;
    uint32 slave_bitmap2;
};
typedef struct sys_chip_i2c_slave_bitmap_s sys_chip_i2c_slave_bitmap_t;

enum sys_chip_sfp_scan_op_e
{
    SYS_CHIP_SFP_INTERVAL_OP = 0x01,
    SYS_CHIP_SFP_BITMAP_OP = 0x02,
    SYS_CHIP_SFP_SCAN_REG_OP = 0x04
};
typedef enum sys_chip_sfp_scan_op_e sys_chip_sfp_scan_op_t;

enum sys_chip_mac_led_op_e
{
    SYS_CHIP_LED_MODE_SET_OP = 0x01,
    SYS_CHIP_LED_POLARITY_SET_OP = 0x02
};
typedef enum sys_chip_mac_led_op_e sys_chip_mac_led_op_t;

#define SYS_CHIP_I2C_MAX_BITAMP     32
#define SYS_CHIP_I2C_READ_MAX_LENGTH    384
#define SYS_CHIP_I2C_LOCK(lchip) sal_mutex_lock(p_gg_chip_master[lchip]->p_i2c_mutex)
#define SYS_CHIP_I2C_UNLOCK(lchip) sal_mutex_unlock(p_gg_chip_master[lchip]->p_i2c_mutex)
#define SYS_CHIP_SMI_LOCK(lchip) sal_mutex_lock(p_gg_chip_master[lchip]->p_smi_mutex)
#define SYS_CHIP_SMI_UNLOCK(lchip) sal_mutex_unlock(p_gg_chip_master[lchip]->p_smi_mutex)

#define SYS_CHIP_LED_FORCE_OFF      0
#define SYS_CHIP_LED_ON_RX_LINK     1
#define SYS_CHIP_LED_ON_TX_LINK     2
#define SYS_CHIP_LED_FORCE_ON       3

#define SYS_CHIP_LED_BLINK_OFF      0
#define SYS_CHIP_LED_BLINK_RX_ACT   1
#define SYS_CHIP_LED_BLINK_TX_ACT   2
#define SYS_CHIP_LED_BLINK_ON       3

#define SYS_CHIP_1G_MDIO_READ       0x2
#define SYS_CHIP_XG_MDIO_READ       0x3

#define SYS_CHIP_MDIO_WRITE         0x1
#define SYS_CHIP_MAX_GPIO_NUM       16
#define SYS_CHIP_MAX_PHY_PORT       56

#define SYS_CHIP_INVALID_SWITCH_ID  0
#define SYS_CHIP_MIN_SWITCH_ID      1
#define SYS_CHIP_MAX_SWITCH_ID      16

#define SYS_CHIP_CONTROL0_ID         0
#define SYS_CHIP_CONTROL1_ID         1
#define SYS_CHIP_CONTROL_NUM         2

#define SYS_CHIP_MDIO_1GBUS0_ID     0
#define SYS_CHIP_MDIO_1GBUS1_ID     1
#define SYS_CHIP_MDIO_XGBUS0_ID     2
#define SYS_CHIP_MDIO_XGBUS1_ID     3

#define SYS_CHIP_MAX_GPIO           9

#define SYS_CHIP_MAX_TIME_OUT       256

#define SYS_CHIP_MAC_GPIO_ID        9

#define SYS_CHIPMDIOXG1_ADDR 0x40001268
#define SYS_CHIPMDIOXG0_ADDR 0x20001268

#define SUP_RESET_CTL_ADDR     0x00020070
#define PLL_CORE_CFG_ADDR      0x00000020

#define SYS_CHIP_FLAG_ISSET(VAL, FLAG)        (((VAL)&(FLAG)) == (FLAG))
#define SYS_CHIP_FLAG_ISZERO(VAL)        ((VAL) == 0)

#define SetI2CMasterBmpCfg(ctl_id, p_bitmap_ctl, p_slave_bitmap)\
    if (ctl_id)\
    {\
        SetI2CMasterBmpCfg1(A, slaveBitmap_f, p_bitmap_ctl, p_slave_bitmap);\
    }\
    else\
    {\
        SetI2CMasterBmpCfg0(A, slaveBitmap_f, p_bitmap_ctl, p_slave_bitmap);\
    }

#define SetI2CMasterReadCfg(ctl_id, field, p_master_rd, value)\
    if(ctl_id)\
    {\
        SetI2CMasterReadCfg1(V, field, p_master_rd, value);\
    }\
    else\
    {\
        SetI2CMasterReadCfg0(V, field, p_master_rd, value);\
    }

#define SetI2CMasterReadCtl(ctl_id, field, p_read_ctl, value)\
    if (ctl_id)\
    {\
        SetI2CMasterReadCtl1(V, field, p_read_ctl, value);\
    }\
    else\
    {\
        SetI2CMasterReadCtl0(V, field, p_read_ctl, value);\
    }

#define GetI2CMasterStatus(ctl_id, field, p_master_status, value)\
    if (ctl_id)\
    {\
        value = GetI2CMasterStatus1(V, field, p_master_status);\
    }\
    else\
    {\
        value = GetI2CMasterStatus0(V, field, p_master_status);\
    }

#define SetI2CMasterWrCfg(ctl_id, field, p_master_wr, value)\
    if (ctl_id)\
    {\
        SetI2CMasterWrCfg1(V, field, p_master_wr, value);\
    }\
    else\
    {\
        SetI2CMasterWrCfg0(V, field, p_master_wr, value);\
    }

#define GetI2CMasterWrCfg(ctl_id, field, p_master_wr, value)\
    if (ctl_id)\
    {\
        value = GetI2CMasterWrCfg1(V, field, p_master_wr);\
    }\
    else\
    {\
        value = GetI2CMasterWrCfg0(V, field, p_master_wr);\
    }

#define GetI2CMasterDebugState(ctl_id, field, p_master_dbg, value)\
    if (ctl_id)\
    {\
        value = GetI2CMasterDebugState1(V, field, p_master_dbg);\
    }\
    else\
    {\
        value = GetI2CMasterDebugState0(V, field, p_master_dbg);\
    }

#define GetMdioStatus1G(master_id, field, p_mdio_status, value)\
    if (master_id)\
    {\
        value = GetMdioStatus1G1(V, field, p_mdio_status);\
    }\
    else\
    {\
        value = GetMdioStatus1G0(V, field, p_mdio_status);\
    }

#define GetMdioStatusXG(master_id, field, p_mdio_status, value)\
    if (master_id)\
    {\
        value = GetMdioStatusXG1(V, field, p_mdio_status);\
    }\
    else\
    {\
        value = GetMdioStatusXG0(V, field, p_mdio_status);\
    }

#define GetI2CMasterReadStatus(ctl_id, p_read_status, arr_read_status)\
    if (ctl_id)\
    {\
        GetI2CMasterReadStatus1(A, readStatusBmp_f, p_read_status, arr_read_status);\
    }\
    else\
    {\
        GetI2CMasterReadStatus0(A, readStatusBmp_f, p_read_status, arr_read_status);\
    }
#define SetI2CMasterPollingCfg(ctl_id, field, p_master_wr, value)\
    if (ctl_id)\
    {\
        SetI2CMasterPollingCfg1(V, field, p_master_wr, value);\
    }\
    else\
    {\
        SetI2CMasterPollingCfg0(V, field, p_master_wr, value);\
    }

#define SetMdioCmd1G(master_id, field, p_mdio_cmd, value)\
    if (master_id)\
    {\
        SetMdioCmd1G1(V, field, p_mdio_cmd, value);\
    }\
    else\
    {\
        SetMdioCmd1G0(V, field, p_mdio_cmd, value);\
    }

#define SetMdioCmdXG(master_id, field, p_mdio_cmd, value)\
    if (master_id)\
    {\
        SetMdioCmdXG1(V, field, p_mdio_cmd, value);\
    }\
    else\
    {\
        SetMdioCmdXG0(V, field, p_mdio_cmd, value);\
    }

#define SetMacLedSampleInterval(ctl_id, field, p_led_sample, value)\
    if (ctl_id)\
    {\
        SetMacLedSampleInterval1(V, field, p_led_sample, value);\
    }\
    else\
    {\
        SetMacLedSampleInterval0(V, field, p_led_sample, value);\
    }

#define SetMacLedPortRange(ctl_id, field, p_port_range, value)\
    if (ctl_id)\
    {\
        SetMacLedPortRange1(V, field, p_port_range, value);\
    }\
    else\
    {\
        SetMacLedPortRange0(V, field, p_port_range, value);\
    }

#define SetMacLedCfgPortSeqMap(ctl_id, field, p_seq_map, value)\
    if (ctl_id)\
    {\
        SetMacLedCfgPortSeqMap1(V, field, p_seq_map, value);\
    }\
    else\
    {\
        SetMacLedCfgPortSeqMap1(V, field, p_seq_map, value);\
    }

#define SetMacLedCfgPortMode(ctl_id, field, p_led_mode, value)\
    if (ctl_id)\
    {\
        SetMacLedCfgPortMode1(V, field, p_led_mode, value);\
    }\
    else\
    {\
        SetMacLedCfgPortMode0(V, field, p_led_mode, value);\
    }

#define SetMacLedPolarityCfg(ctl_id, field, p_led_polarit, value)\
    if (ctl_id)\
    {\
        SetMacLedPolarityCfg1(V, field, p_led_polarit, value);\
    }\
    else\
    {\
         SetMacLedPolarityCfg0(V, field, p_led_polarit, value);\
    }

#define SetMdioScanCtl(ctl_id, field, p_scan_para, value)\
    if (ctl_id)\
    {\
        SetMdioScanCtl1(V, field, p_scan_para, value);\
    }\
    else\
    {\
        SetMdioScanCtl0(V, field, p_scan_para, value);\
    }

#define SetMdioUsePhy(ctl_id, field, p_use_phy, arr_value)\
    if (ctl_id)\
    {\
        SetMdioUsePhy1(A, field, p_use_phy, arr_value);\
    }\
    else\
    {\
        SetMdioUsePhy0(A, field, p_use_phy, arr_value);\
    }

#define SetRlmNetCtlReset(ctl_id, field, p_use_phy, arr_value)\
    if (ctl_id)\
    {\
        SetRlmNetCtlReset1(A, field, p_use_phy, arr_value);\
    }\
    else\
    {\
        SetRlmNetCtlReset0(A, field, p_use_phy, arr_value);\
    }

/****************************************************************************
 *
 * Global and static
 *
 *****************************************************************************/
sys_chip_master_t* p_gg_chip_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern uint8 g_lchip_num;

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

extern int32 drv_goldengate_init(uint8 chip_num, uint8 chip_id_base);
STATIC int32 _sys_goldengate_chip_ecc_recover_init(uint8 lchip, ctc_chip_global_cfg_t* p_cfg);
extern int32 sys_goldengate_datapath_get_gport_with_serdes(uint8 lchip, uint16 serdes_id, uint16* p_gport);
extern int32 dal_get_chip_number(uint8* p_num);
/**
 @brief The function is to initialize the chip module and set the local chip number of the linecard
*/
int32
sys_goldengate_chip_init(uint8 lchip, uint8 lchip_num)
{
    int32 ret;
    uint8 pcie_sel = 0;
    if ((lchip >= CTC_MAX_LOCAL_CHIP_NUM)
        || (lchip_num > SYS_GG_MAX_LOCAL_CHIP_NUM)
        || (lchip_num > CTC_MAX_LOCAL_CHIP_NUM))
    {
        return CTC_E_INVALID_CHIP_NUM;
    }

    if (NULL != p_gg_chip_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_chip_set_active(lchip, TRUE));

    p_gg_chip_master[lchip] = (sys_chip_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_chip_master_t));

    if (NULL == p_gg_chip_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_chip_master[lchip], 0, sizeof(sys_chip_master_t));

    /* create mutex for chip module */
    ret = sal_mutex_create(&(p_gg_chip_master[lchip]->p_chip_mutex));
    if (ret || (!p_gg_chip_master[lchip]->p_chip_mutex))
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        mem_free(p_gg_chip_master[lchip]);
        return ret;
    }

    /* create mutex for dynamic switch */
    ret = sal_mutex_create(&(p_gg_chip_master[lchip]->p_switch_mutex));
    if (ret || (!p_gg_chip_master[lchip]->p_switch_mutex))
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        mem_free(p_gg_chip_master[lchip]);
        return ret;
    }

    /* create mutex for I2C */
    ret = sal_mutex_create(&(p_gg_chip_master[lchip]->p_i2c_mutex));
    if (ret || (!p_gg_chip_master[lchip]->p_i2c_mutex))
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        mem_free(p_gg_chip_master[lchip]);
        return ret;
    }

    /* create mutex for SMI */
    ret = sal_mutex_create(&(p_gg_chip_master[lchip]->p_smi_mutex));
    if (ret || (!p_gg_chip_master[lchip]->p_smi_mutex))
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        mem_free(p_gg_chip_master[lchip]);
        return ret;
    }

    g_lchip_num = lchip_num;
    /*
        drv-io ,which inits in datapath, depends drv-init
    */
    ret = drv_goldengate_init(lchip_num, 0);
    if (ret < 0)
    {
        mem_free(p_gg_chip_master[lchip]);
        ret = CTC_E_DRV_FAIL;
    }

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
       drv_goldengate_set_warmboot_status(lchip, CTC_WB_STATUS_RELOADING);
    }

    CTC_ERROR_RETURN(sys_goldengate_chip_get_pcie_select(lchip, &pcie_sel));
    if (pcie_sel == 1)
    {
        uint32      value = 0;
        /*enable i2c access*/
        drv_goldengate_i2c_read_local(lchip, 4,  &value);
        value |= 1;
        drv_goldengate_i2c_write_local(lchip, 4,  value);

        /*release pci0 reset*/
        drv_goldengate_i2c_read_chip(lchip, 0xac78, &value);
        value |= 0x2000;
        drv_goldengate_i2c_write_chip(lchip, 0xac78, value);
    }
    return ret;
}

int32
sys_goldengate_chip_deinit(uint8 lchip)
{
    CTC_ERROR_RETURN(sys_goldengate_chip_set_active(lchip, FALSE));

    if (NULL == p_gg_chip_master[lchip])
    {
        return CTC_E_NONE;
    }

#if 1
    /*release smi mutex*/
    sal_mutex_destroy(p_gg_chip_master[lchip]->p_smi_mutex);

    /*release i2c mutex*/
    sal_mutex_destroy(p_gg_chip_master[lchip]->p_i2c_mutex);

    /*release switch mutex*/
    sal_mutex_destroy(p_gg_chip_master[lchip]->p_switch_mutex);

    /*release chip mutex*/
    sal_mutex_destroy(p_gg_chip_master[lchip]->p_chip_mutex);
    mem_free(p_gg_chip_master[lchip]);
#endif

    return CTC_E_NONE;
}

/*
    Called in register-init, for it need drv-io
*/
int32
sys_goldengate_mdio_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    ctc_chip_phy_scan_ctrl_t scan_ctl;
    uint32 detect_en[2] = {0};
    uint16 mac_id = 0;
    MdioChanMap0_m mdio_map0;
    MdioChanMap1_m mdio_map1;
    uint32 value = 0;

    sal_memset(&scan_ctl , 0, sizeof(ctc_chip_phy_scan_ctrl_t));
    detect_en[0] = 0xffffffff;
    detect_en[1] = 0xffffffff;

    /*
        #3, Cfg SMI 1G interface MDC clock Frequency=2.5MHz,@clockSlow=125MHz
    */
    field_value = 50;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgClkMdioDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        #4, Toggle the clockMdio divider reset
    */
    field_value = 1;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value = 0;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        #P2.Configure MDCXG clock
        #1, Cfg SMI 1G interface MDCXG clock Frequency=25MHz,@clockSlow=125MHz
    */

    field_value = 63;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgClkMdioXgDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        #2, Toggle the clockMdio divider reset
    */
    field_value = 1;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioXgDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value = 0;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioXgDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        reset MDIO module in slice0 and slice1
    */
    field_value = 1;
    cmd = DRV_IOW(RlmNetCtlReset0_t, RlmNetCtlReset0_resetCoreMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    cmd = DRV_IOW(RlmNetCtlReset1_t, RlmNetCtlReset0_resetCoreMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    cmd = DRV_IOW(RlmNetCtlReset0_t, RlmNetCtlReset0_resetCoreMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    cmd = DRV_IOW(RlmNetCtlReset1_t, RlmNetCtlReset0_resetCoreMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*
        MdioLinkDownDetecEn0_linkDownDetcEn_f
    */
    cmd = DRV_IOW(MdioLinkDownDetecEn0_t, MdioLinkDownDetecEn0_linkDownDetcEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, detect_en));
    cmd = DRV_IOW(MdioLinkDownDetecEn1_t, MdioLinkDownDetecEn1_linkDownDetcEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, detect_en));


    /*
        Init mdio scan. For no phy on demo board, just set interval
    */
    scan_ctl.op_flag = CTC_CHIP_INTERVAL_OP;
    scan_ctl.scan_interval = 100;
    scan_ctl.ctl_id = 0;
    CTC_ERROR_RETURN(sys_goldengate_chip_set_phy_scan_para(lchip, &scan_ctl));

    scan_ctl.op_flag = CTC_CHIP_INTERVAL_OP;
    scan_ctl.scan_interval = 100;
    scan_ctl.ctl_id = 1;
    CTC_ERROR_RETURN(sys_goldengate_chip_set_phy_scan_para(lchip, &scan_ctl));

    /*cfg mdio channel map*/
    for (mac_id = 0; mac_id < 64; mac_id++)
    {
        sal_memset(&mdio_map0, 0, sizeof(MdioChanMap0_m));
        sal_memset(&mdio_map1, 0, sizeof(MdioChanMap1_m));

        if (mac_id > 48)
        {
            value = mac_id - 8;
        }
        else
        {
            value = mac_id;
        }

        SetMdioChanMap0(V, chanId_f, &mdio_map0, value);
        cmd = DRV_IOW((MdioChanMap0_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mac_id, cmd, &mdio_map0);

        SetMdioChanMap1(V, chanId_f, &mdio_map1, value);
        cmd = DRV_IOW((MdioChanMap1_t), DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, mac_id, cmd, &mdio_map1);
    }

    sys_goldengate_chip_set_phy_scan_en(lchip, TRUE);

    return CTC_E_NONE;
}

/**
 @brief The function is to get the local chip num
*/
uint8
sys_goldengate_get_local_chip_num(void)
{
    return g_lchip_num;
}

/**
 @brief The function is to set chip's global chip id
*/
int32
sys_goldengate_set_gchip_id(uint8 lchip, uint8 gchip_id)
{
    if (NULL == p_gg_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_GLOBAL_CHIPID_CHECK(gchip_id);

    p_gg_chip_master[lchip]->g_chip_id = gchip_id;

    return CTC_E_NONE;
}

/**
 @brief The function is to get chip's global chip id
*/
int32
sys_goldengate_get_gchip_id(uint8 lchip, uint8* gchip_id)
{
    if (NULL == p_gg_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_PTR_VALID_CHECK(gchip_id);

    *gchip_id = p_gg_chip_master[lchip]->g_chip_id;

    return CTC_E_NONE;
}

/**
 @brief The function is to judge whether the chip is local
*/
bool
sys_goldengate_chip_is_local(uint8 lchip, uint8 gchip_id)
{
    bool ret        = FALSE;

    if ((NULL != p_gg_chip_master[lchip])
        && (p_gg_chip_master[lchip]->g_chip_id == gchip_id))
    {
        ret     = TRUE;
    }

    return ret;
}

/* function that call sys_goldengate_get_local_chip_id needs to check return */
int32
sys_goldengate_get_local_chip_id(uint8 gchip_id, uint8* lchip_id)
{
    uint8 i = 0;
    uint8 is_find = 0;

    CTC_PTR_VALID_CHECK(lchip_id);

    /* different chips have same lchip num */
    for (i = 0; i < g_lchip_num; i++)
    {
        if ((NULL != p_gg_chip_master[i])
            && (p_gg_chip_master[i]->g_chip_id == gchip_id))
        {
            is_find = 1;
            break;
        }
    }

    if (is_find)
    {
        *lchip_id = i;
        return CTC_E_NONE;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }
}

int32
sys_goldengate_chip_set_eth_cpu_cfg(uint8 lchip)

{
    uint32 cmd = 0;
    uint8 chan_id = 0;
    uint8 lchan_id = 0;

    uint8 slice_id = 0;
    uint32 tocpu_bmp[4] = {0};
    ds_t ds;
    hw_mac_addr_t hw_mac_da;
    hw_mac_addr_t hw_mac_sa;
    IpeHeaderAdjustCtl_m ipe_header_adjust_ctl;
    EpeNextHopCtl_m epe_nhop_ctl;
    uint32 field_value = 0;

    chan_id =  p_gg_chip_master[lchip]->cpu_channel;
    if (chan_id >= 128)
    {
        return CTC_E_INVALID_PARAM;
    }

    slice_id = SYS_MAP_CHANID_TO_SLICE(chan_id);
    lchan_id = chan_id&0x3F;

    CTC_BIT_SET(tocpu_bmp[lchan_id >> 5], (lchan_id&0x1F));

    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeHeaderAdjustCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));
    SetIpeHeaderAdjustCpuChanCtl(A, fromCpuEn_f, &ds, tocpu_bmp);
    cmd = DRV_IOW(IpeHeaderAdjustCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));

    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(IpeFwdCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));
    SetIpeFwdCpuChanCtl(A, fromCpuEn_f, &ds, tocpu_bmp);
    cmd = DRV_IOW(IpeFwdCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));

    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));
    SetEpeHdrAdjustChanCtl(A, toCpuEn_f, &ds, tocpu_bmp);
    cmd = DRV_IOW(EpeHdrAdjustChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));

    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(EpeHeaderEditCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));
    SetEpeHeaderEditCpuChanCtl(A, toCpuEn_f, &ds, tocpu_bmp);
    cmd = DRV_IOW(EpeHeaderEditCpuChanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, slice_id, cmd, &ds));

    sal_memset(tocpu_bmp, 0, sizeof(tocpu_bmp));
    CTC_BIT_SET(tocpu_bmp[chan_id >> 5], (chan_id&0x1F));
    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds));
    SetBufferStoreCtl(A, fromCpuEn_f, &ds, tocpu_bmp);
    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ds));

    SYS_GOLDENGATE_SET_HW_MAC(hw_mac_da, p_gg_chip_master[lchip]->cpu_mac_da[0]);
    SYS_GOLDENGATE_SET_HW_MAC(hw_mac_sa, p_gg_chip_master[lchip]->cpu_mac_sa);

    sal_memset(&ds, 0, sizeof(ds));
    cmd = DRV_IOR(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds));
    SetDsPacketHeaderEditTunnel(A, macSa_f, &ds, hw_mac_sa);
    SetDsPacketHeaderEditTunnel(V, packetHeaderL2En_f, &ds, 1);
    SetDsPacketHeaderEditTunnel(V, vlanIdValid_f, &ds, 1);
    SetDsPacketHeaderEditTunnel(V, vlanId_f, &ds, p_gg_chip_master[lchip]->vlan_id);
    SetDsPacketHeaderEditTunnel(A, macDa_f, &ds, hw_mac_da);
    cmd = DRV_IOW(DsPacketHeaderEditTunnel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &ds));

    field_value = p_gg_chip_master[lchip]->tpid;
    cmd = DRV_IOW(EpePktHdrCtl_t, EpePktHdrCtl_tpid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_value));

    field_value = 0x55aa;
    cmd = DRV_IOW(EpePktHdrCtl_t, EpePktHdrCtl_headerEtherType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_value));

#if 0
    field_value = 47;
    cmd = DRV_IOW(BufRetrvChanIdCfg_t, BufRetrvChanIdCfg_cfgCpuChanId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif


    field_value = 7;
    cmd = DRV_IOW(IpePhyPortMuxCtl_t, IpePhyPortMuxCtl_muxType_f );
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &field_value));

    sal_memset(&ipe_header_adjust_ctl, 0, sizeof(ipe_header_adjust_ctl));
    cmd = DRV_IOR(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));
    SetIpeHeaderAdjustCtl(V, headerMacDaCheckEn_f, &ipe_header_adjust_ctl,0);
    SetIpeHeaderAdjustCtl(V, headerTpid_f, &ipe_header_adjust_ctl, p_gg_chip_master[lchip]->tpid);
    SetIpeHeaderAdjustCtl(V, headerEtherTypeCheckDisable_f, &ipe_header_adjust_ctl, 1);
    SetIpeHeaderAdjustCtl(V, headerEtherType_f, &ipe_header_adjust_ctl,0x55aa);
    SetIpeHeaderAdjustCtl(V, packetHeaderBypassAll_f, &ipe_header_adjust_ctl,1);
    SetIpeHeaderAdjustCtl(V, fromCpuBypassAll_f , &ipe_header_adjust_ctl,1);
    cmd = DRV_IOW(IpeHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_adjust_ctl));

    sal_memset(&epe_nhop_ctl, 0, sizeof(epe_nhop_ctl));
    cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_nhop_ctl));
    SetEpeNextHopCtl(V, toCpuPktDoNotBypassNhp_f, &epe_nhop_ctl, 1);
    cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_nhop_ctl));

    return CTC_E_NONE;
}

/**
 @brief The function is to set chip's global cfg
*/
int32
sys_goldengate_set_chip_global_cfg(uint8 lchip, ctc_chip_global_cfg_t* chip_cfg)
{
    p_gg_chip_master[lchip]->cut_through_en       = chip_cfg->cut_through_en;
    p_gg_chip_master[lchip]->cut_through_speed    = chip_cfg->cut_through_speed;

    p_gg_chip_master[lchip]->cpu_eth_en = chip_cfg->cpu_port_en;
    p_gg_chip_master[lchip]->cpu_channel = SYS_GET_CHANNEL_ID(lchip, chip_cfg->cpu_port);
    if ((0xFF ==  p_gg_chip_master[lchip]->cpu_channel) && (chip_cfg->cpu_port_en))
    {
        return CTC_E_INVALID_PARAM;
    }

    p_gg_chip_master[lchip]->tpid = chip_cfg->tpid;
    p_gg_chip_master[lchip]->vlan_id = chip_cfg->vlanid;
    sal_memcpy(p_gg_chip_master[lchip]->cpu_mac_sa, chip_cfg->cpu_mac_sa, sizeof(mac_addr_t));
    sal_memcpy(p_gg_chip_master[lchip]->cpu_mac_da[0], chip_cfg->cpu_mac_da[0], sizeof(mac_addr_t));

    CTC_ERROR_RETURN(_sys_goldengate_chip_ecc_recover_init(lchip, chip_cfg));

    p_gg_chip_master[lchip]->gb_gg_interconnect_en = chip_cfg->gb_gg_interconnect_en;
    drv_goldengate_set_gb_gg_interconnect_en(chip_cfg->gb_gg_interconnect_en);

    return CTC_E_NONE;
}

int32
sys_goldengate_get_chip_clock(uint8 lchip, uint16* freq)
{
    *freq = sys_goldengate_get_core_freq(lchip, 0);

    return CTC_E_NONE;
}

int32
sys_goldengate_get_chip_cpumac(uint8 lchip, uint8* mac_sa, uint8* mac_da)
{
    sal_memcpy(mac_sa, p_gg_chip_master[lchip]->cpu_mac_sa, sizeof(mac_addr_t));
    sal_memcpy(mac_da, p_gg_chip_master[lchip]->cpu_mac_da[0], sizeof(mac_addr_t));

    return CTC_E_NONE;
}
bool
sys_goldengate_chip_is_eth_cpu_port(uint8 lchip, uint16 lport)
{
    uint16 sys_port     = 0;

    if (p_gg_chip_master[lchip]->cpu_eth_en)
    {
        sys_port = SYS_GET_LPORT_ID_WITH_CHAN(lchip, p_gg_chip_master[lchip]->cpu_channel);
        if (lport == SYS_MAP_SYS_LPORT_TO_DRV_LPORT(sys_port))
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
}

int32
sys_goldengate_get_chip_cpu_eth_en(uint8 lchip, uint8 *enable, uint8* cpu_eth_chan)
{
    *enable = p_gg_chip_master[lchip]->cpu_eth_en;
    *cpu_eth_chan = p_gg_chip_master[lchip]->cpu_channel;

    return CTC_E_NONE;
}


#define __CUT_THROUGH__
int32
sys_goldengate_get_cut_through_en(uint8 lchip)
{
    return p_gg_chip_master[lchip]->cut_through_en;
}

int32
sys_goldengate_get_cut_through_speed(uint8 lchip, uint16 gport)
{
    int32 speed = 0;
    uint16 lport = 0;

    sys_datapath_lport_attr_t* p_port_cap = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    if(1 == p_gg_chip_master[lchip]->cut_through_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &p_port_cap));
        if (p_port_cap->port_type != SYS_DATAPATH_NETWORK_PORT)
        {
            return 0;
        }

        if(3 == p_gg_chip_master[lchip]->cut_through_speed)
        {
            if (CTC_PORT_SPEED_1G == p_port_cap->speed_mode)
            {
                speed = 1;
            }
            else  if (CTC_PORT_SPEED_10G == p_port_cap->speed_mode)
            {
                speed = 2;
            }
            else if ((CTC_PORT_SPEED_20G == p_port_cap->speed_mode))
            {
                speed = 0;
            }
            else if ((CTC_PORT_SPEED_40G == p_port_cap->speed_mode))
            {
                speed = 3;
            }
            else if (CTC_PORT_SPEED_100G == p_port_cap->speed_mode)
            {
                speed = 0;
            }
        }
        else if(1 == p_gg_chip_master[lchip]->cut_through_speed)
        {
            if (CTC_PORT_SPEED_1G == p_port_cap->speed_mode)
            {
                speed = 0;
            }
            else  if (CTC_PORT_SPEED_10G == p_port_cap->speed_mode)
            {
                speed = 1;
            }
            else if ((CTC_PORT_SPEED_20G == p_port_cap->speed_mode))
            {
                speed = 0;
            }
            else if ((CTC_PORT_SPEED_40G == p_port_cap->speed_mode))
            {
                speed = 2;
            }
            else if (CTC_PORT_SPEED_100G == p_port_cap->speed_mode)
            {
                speed = 3;
            }
        }
        else if(2 == p_gg_chip_master[lchip]->cut_through_speed)
        {
            if (CTC_PORT_SPEED_1G == p_port_cap->speed_mode)
            {
                speed = 1;
            }
            else  if (CTC_PORT_SPEED_10G == p_port_cap->speed_mode)
            {
                speed = 2;
            }
            else if ((CTC_PORT_SPEED_20G == p_port_cap->speed_mode))
            {
                speed = 0;
            }
            else if ((CTC_PORT_SPEED_40G == p_port_cap->speed_mode))
            {
                speed = 0;
            }
            else if (CTC_PORT_SPEED_100G == p_port_cap->speed_mode)
            {
                speed = 3;
            }
        }
        else
        {
            /*not support speed*/
            speed = 0;
        }
    }

    return speed;
}

int32
sys_goldengate_get_gb_gg_interconnect_en(uint8 lchip)
{
    return p_gg_chip_master[lchip]->gb_gg_interconnect_en;
}

#define __ECC_RECOVER__

STATIC int32
_sys_goldengate_chip_ecc_recover_init(uint8 lchip, ctc_chip_global_cfg_t* p_cfg)
{
    drv_ecc_recover_init_t init;

    sal_memset(&init, 0, sizeof(drv_ecc_recover_init_t));
    init.ecc_recover_en     = p_cfg->ecc_recover_en;
    init.tcam_scan_en       = p_cfg->tcam_scan_en;
    init.sbe_scan_en        = p_cfg->tcam_scan_en; /* tcam and sbe scan be regard as memory scan */
    init.tcam_scan_burst_entry_num = 4096;
    init.tcam_scan_interval = 0; /* Not start tcam key scan thread when init */
    init.sbe_scan_interval  = 0; /* Not start single bit error scan thread when init */

    if (init.ecc_recover_en || init.tcam_scan_en || init.sbe_scan_en)
    {
        CTC_ERROR_RETURN(drv_goldengate_ecc_recover_init(&init));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_chip_set_tcam_scan_cfg(uint8 lchip, uint32 burst_entry_num, uint32 burst_interval)
{
    CTC_ERROR_RETURN(drv_goldengate_ecc_recover_set_tcam_scan_cfg(burst_entry_num, burst_interval));

    return CTC_E_NONE;
}

#define __SHOW_INFO__
int32
sys_goldengate_chip_show_ecc_recover_status(uint8 lchip, uint8 is_all)
{
    uint32 mem_type = 0;
    uint32 cnt = 0;
    uint32 rcv_cnt = 0;
    uint32 tcam_cnt = 0;
    uint32 sbe_cnt = 0;
    uint32 ign_cnt = 0;
    char* status[] = {"StopAfterOnce", "Continuous"};
    ctc_chip_mem_scan_cfg_t cfg;
    LCHIP_CHECK(lchip);
    if (is_all)
    {
        CTC_ERROR_RETURN(drv_goldengate_ecc_recover_show_status(lchip, is_all));
        return CTC_E_NONE;
    }

    for (mem_type = 0; mem_type <= DRV_ECC_MEM_SBE; mem_type++)
    {
        CTC_ERROR_RETURN(drv_goldengate_ecc_recover_get_status(lchip, mem_type, &cnt));

        if ((mem_type >= DRV_ECC_MEM_TCAM_KEY0) && (mem_type <= DRV_ECC_MEM_TCAM_KEY8))
        {
            tcam_cnt += cnt;
        }
        else if (DRV_ECC_MEM_IGNORE == mem_type)
        {
            ign_cnt += cnt;
        }
        else if (DRV_ECC_MEM_SBE == mem_type)
        {
            sbe_cnt += cnt;
        }
        else
        {
            rcv_cnt += cnt;
        }
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " EccParityReocver:%s\n", drv_goldengate_ecc_recover_get_enable() ? "Enable" : "Disable");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " ------------------------------------\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-15s%-8s%-16s\n", "Type", "Count", "RecoverStatus");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " ------------------------------------\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-15s%-8u%-16s\n", "RecoverError", rcv_cnt, "SDK Recover");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-15s%-8u%-16s\n", "OtherIgnore", ign_cnt, "NO  Recover");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " ------------------------------------\n");

    CTC_ERROR_RETURN(sys_goldengate_chip_get_property(lchip, CTC_CHIP_PROP_MEM_SCAN, &cfg));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " MemoryScan:%s\n", 0xFF != cfg.tcam_scan_mode ? "Enable" : "Disable");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " -----------------------------------------------------\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-7s%-16s%-8s%-14s\n", "Type", "ScanStatus", "Count", "RecoverMode");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " -----------------------------------------------------\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-7s%-16s%-8u%-14s\n", "TCAM", ((0xFF == cfg.tcam_scan_mode) || (0 == cfg.tcam_scan_interval))
                                                  ? "Stop" : status[cfg.tcam_scan_mode], tcam_cnt, "SDK Recover");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " %-7s%-16s%-8u%-14s\n", "SBE", (0xFF == cfg.sbe_scan_mode) || (0 == cfg.sbe_scan_interval)
                                                 ? "Stop" : status[cfg.sbe_scan_mode], sbe_cnt,  "HW Self-healing");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " -----------------------------------------------------\n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

/**
 @brief show chip status
*/
int32
sys_goldengate_chip_show_status(void)
{
    uint8 lchip = 0;
    uint8 gchip = 0;
    uint8 chip_num = 0;
    int32 ret = 0;
    ctc_chip_access_type_t access_type;
    uint8 pcie_sel = 0;

    chip_num = sys_goldengate_get_local_chip_num();
    sys_goldengate_chip_get_pcie_select(0, &pcie_sel);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n The lchip number is: %d\n", chip_num);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n Active Pcie        : %s\n", (pcie_sel)?"Pcie1":"Pcie0");

    for (lchip = 0; lchip < SYS_GG_MAX_LOCAL_CHIP_NUM; lchip++)
    {
        ret = sys_goldengate_chip_check_active(lchip);
        if (ret < 0)
        {
            continue;
        }
        CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The global chip id for lchip %d is: %d\n", lchip, gchip);
    }

    CTC_ERROR_RETURN(sys_goldengate_chip_get_access_type(lchip, &access_type));
    if (access_type == CTC_CHIP_PCI_ACCESS)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The current access mode is : PCIe! \n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The current access mode is :I2C! \n");
    }

    for (lchip = 0; lchip < SYS_GG_MAX_LOCAL_CHIP_NUM; lchip++)
    {
        ret = sys_goldengate_chip_check_active(lchip);
        if (ret < 0)
        {
            continue;
        }
        sys_goldengate_get_gchip_id(lchip, &gchip);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n GCHIP 0x%04X recover status:\n", gchip);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " -----------------------------------------------------\n");
        CTC_ERROR_RETURN(sys_goldengate_chip_show_ecc_recover_status(lchip, 0));
    }

    return CTC_E_NONE;
}

#define __MDIO_INTERFACE__
/**
 @brief set phy to port mapping
*/
int32
sys_goldengate_chip_set_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para)
{
    if (NULL == phy_mapping_para)
    {
        return CTC_E_INVALID_PTR;
    }

    sal_memcpy(p_gg_chip_master[lchip]->port_mdio_mapping_tbl, phy_mapping_para->port_mdio_mapping_tbl,
               SYS_GG_MAX_PHY_PORT);

    sal_memcpy(p_gg_chip_master[lchip]->port_phy_mapping_tbl, phy_mapping_para->port_phy_mapping_tbl,
               SYS_GG_MAX_PHY_PORT);

    return CTC_E_NONE;
}

/**
 @brief get phy to port mapping
*/
int32
sys_goldengate_chip_get_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* p_phy_mapping_para)
{
    if (NULL == p_phy_mapping_para)
    {
        return CTC_E_INVALID_PTR;
    }

    sal_memcpy(p_phy_mapping_para->port_mdio_mapping_tbl,  p_gg_chip_master[lchip]->port_mdio_mapping_tbl,
               SYS_GG_MAX_PHY_PORT);

    sal_memcpy(p_phy_mapping_para->port_phy_mapping_tbl,  p_gg_chip_master[lchip]->port_phy_mapping_tbl,
               SYS_GG_MAX_PHY_PORT);

    return CTC_E_NONE;
}
/**
 @brief smi interface for set auto scan para
*/
int32
sys_goldengate_chip_set_phy_scan_para(uint8 lchip, ctc_chip_phy_scan_ctrl_t* p_scan_para)
{
    uint32 cmd = 0;
    uint32 arr_phy[2] = {0};
    MdioScanCtl0_m scan_ctrl;
    MdioUsePhy0_m use_phy;

    CTC_PTR_VALID_CHECK(p_scan_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Para op_flag:0x%x ge_bitmap0:0x%x  ge_bitmap1:0x%x interval:0x%x   \
        xgbitmap0:0x%x xgbitmap1:0x%x bitmask:0x%x scan_twince:%d\n",
                     p_scan_para->op_flag, p_scan_para->scan_gephy_bitmap0, p_scan_para->scan_gephy_bitmap1, p_scan_para->scan_interval,
                     p_scan_para->scan_xgphy_bitmap0, p_scan_para->scan_xgphy_bitmap1, p_scan_para->xgphy_link_bitmask, p_scan_para->xgphy_scan_twice);



    cmd = DRV_IOR(MdioScanCtl0_t + p_scan_para->ctl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &scan_ctrl));

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_INTERVAL_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        SetMdioScanCtl(p_scan_para->ctl_id, scanInterval_f,
                    &scan_ctrl, p_scan_para->scan_interval);
    }

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_GE_BITMAP_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        SetMdioScanCtl(p_scan_para->ctl_id, mdio1gScanBmp0_f,
                    &scan_ctrl, p_scan_para->scan_gephy_bitmap0);
        SetMdioScanCtl(p_scan_para->ctl_id, mdio1gScanBmp1_f,
                    &scan_ctrl, p_scan_para->scan_gephy_bitmap1);
    }

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_XG_BITMAP_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        SetMdioScanCtl(p_scan_para->ctl_id, mdioXgScanBmp0_f,
                    &scan_ctrl, p_scan_para->scan_xgphy_bitmap0);
        SetMdioScanCtl(p_scan_para->ctl_id, mdioXgScanBmp1_f,
                    &scan_ctrl, p_scan_para->scan_xgphy_bitmap1);
    }

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_XG_LINK_MASK_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        SetMdioScanCtl(p_scan_para->ctl_id, xgLinkBmpMask_f,
                    &scan_ctrl, p_scan_para->xgphy_link_bitmask);
    }

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_XG_TWICE_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        SetMdioScanCtl(p_scan_para->ctl_id, scanXgTwice_f,
                    &scan_ctrl, p_scan_para->xgphy_scan_twice);
    }

    cmd = DRV_IOW(MdioScanCtl0_t + p_scan_para->ctl_id, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &scan_ctrl));

    if (SYS_CHIP_FLAG_ISSET(p_scan_para->op_flag, CTC_CHIP_USE_PHY_OP)
        || SYS_CHIP_FLAG_ISZERO(p_scan_para->op_flag))
    {
        cmd = DRV_IOR(MdioUsePhy0_t + p_scan_para->ctl_id, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &use_phy));
        arr_phy[0] = p_scan_para->mdio_use_phy0;
        arr_phy[1] = p_scan_para->mdio_use_phy1;
        SetMdioUsePhy(p_scan_para->ctl_id, usePhy_f,
                &use_phy, arr_phy);

        cmd = DRV_IOW(MdioUsePhy0_t + p_scan_para->ctl_id, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &use_phy));
    }

    return CTC_E_NONE;
}

/**
 @brief smi interface for set auto scan enable or not
*/
int32
sys_goldengate_chip_set_phy_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0, cmd1 = 0;
    uint32 field_value = (TRUE == enable) ? 1 : 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));

    cmd = DRV_IOW(MdioScanCtl0_t, MdioScanCtl0_scanStart_f);
    cmd1 = DRV_IOW(MdioScanCtl1_t, MdioScanCtl1_scanStart_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &field_value));

    return CTC_E_NONE;
}

/**
 @brief mdio interface for read ge phy
*/
STATIC int32
_sys_goldengate_chip_read_gephy_reg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmd1G0_m mdio_cmd;
    MdioStatus1G0_m mdio_status;
    uint8 mdio1_g_cmd_done = 0;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    uint32 cmd = 0;
    int32 ret = 0;

    DRV_PTR_VALID_CHECK(p_para);
    if ((SYS_CHIP_MDIO_1GBUS0_ID != p_para->bus)
        && (SYS_CHIP_MDIO_1GBUS1_ID != p_para->bus))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmd1G0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatus1G0_m));

    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->reg);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, SYS_CHIP_1G_MDIO_READ);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x1);

    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
                    &mdio_status, mdio1_g_cmd_done);

    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
                    &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* read data */
    GetMdioStatus1G(p_para->ctl_id, mdio1GReadData_f,
                &mdio_status, p_para->value);

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}

/**
 @brief mdio interface for read xg phy
*/
STATIC int32
_sys_goldengate_chip_read_xgphy_reg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmdXG0_m mdio_cmd;
    MdioStatusXG0_m mdio_status;
    uint32 cmd = 0;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    int32 ret = 0;
    uint8 mdio_x_g_cmd_done= 0;

    DRV_PTR_VALID_CHECK(p_para);

    if ((SYS_CHIP_MDIO_XGBUS0_ID != p_para->bus)
        && (SYS_CHIP_MDIO_XGBUS1_ID != p_para->bus))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmdXG0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatusXG0_m));

    SetMdioCmdXG(p_para->ctl_id, devAddCmdXg_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmdXG(p_para->ctl_id, intfSelCmdXg_f, &mdio_cmd, p_para->bus - 2);
    SetMdioCmdXG(p_para->ctl_id, opCodeCmdXg_f, &mdio_cmd, SYS_CHIP_XG_MDIO_READ);
    SetMdioCmdXG(p_para->ctl_id, portAddCmdXg_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmdXG(p_para->ctl_id, regAddCmdXg_f, &mdio_cmd, p_para->reg);
    SetMdioCmdXG(p_para->ctl_id, startCmdXg_f, &mdio_cmd, 0);

    cmd = DRV_IOW(MdioCmdXG0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatusXG0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatusXG(p_para->ctl_id, mdioXGCmdDone_f, &mdio_status, mdio_x_g_cmd_done);

    while ((mdio_x_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatusXG(p_para->ctl_id, mdioXGCmdDone_f, &mdio_status, mdio_x_g_cmd_done);
    }

    if (mdio_x_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* read data */
    GetMdioStatusXG(p_para->ctl_id, mdioXGReadData_f, &mdio_status, p_para->value);

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}


STATIC int32
_sys_goldengate_chip_read_gephy_xgreg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmd1G0_m mdio_cmd;
    MdioStatus1G0_m mdio_status;
    uint32 cmd = 0;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    int32 ret = 0;
    uint8 mdio1_g_cmd_done = 0;

    DRV_PTR_VALID_CHECK(p_para);

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmd1G0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatus1G0_m));

    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, 0x0);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x0);
    SetMdioCmd1G(p_para->ctl_id, dataCmd1g_f, &mdio_cmd, p_para->reg);

    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
                    &mdio_status, mdio1_g_cmd_done);

    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
                    &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, SYS_CHIP_XG_MDIO_READ);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x0);

    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
                    &mdio_status, mdio1_g_cmd_done);

    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f,
             &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }
    /* read data */
    GetMdioStatus1G(p_para->ctl_id, mdio1GReadData_f,
                &mdio_status, p_para->value);

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief mac led interface
*/
int32
sys_goldengate_chip_mdio_read(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    CTC_PTR_VALID_CHECK(p_para);

    LCHIP_CHECK(lchip);

    switch(type)
    {
        case CTC_CHIP_MDIO_GE:
            CTC_ERROR_RETURN(_sys_goldengate_chip_read_gephy_reg(lchip, p_para));
            break;

        case CTC_CHIP_MDIO_XG:
            CTC_ERROR_RETURN(_sys_goldengate_chip_read_xgphy_reg(lchip, p_para));
            break;

        case CTC_CHIP_MDIO_XGREG_BY_GE:
            CTC_ERROR_RETURN(_sys_goldengate_chip_read_gephy_xgreg(lchip, p_para));

        default:
            break;
    }

    return CTC_E_NONE;
}

/**
 @brief mdio interface for write ge phy
*/
STATIC int32
_sys_goldengate_chip_write_gephy_reg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmd1G0_m mdio_cmd;
    MdioStatus1G0_m mdio_status;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    uint32 cmd = 0;
    int32 ret = 0;
    uint8 mdio1_g_cmd_done = 0;

    if (p_para->bus> 1)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmd1G0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatus1G0_m));

    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->reg);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, SYS_CHIP_MDIO_WRITE);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x1);
    SetMdioCmd1G(p_para->ctl_id, dataCmd1g_f, &mdio_cmd, p_para->value);

    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);
    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief mdio interface for write xg phy
*/
STATIC int32
_sys_goldengate_chip_write_xgphy_reg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmdXG0_m mdio_cmd;
    MdioStatusXG0_m mdio_status;
    uint32 cmd = 0;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    int32 ret = 0;
    uint8 mdio_x_g_cmd_done = 0;

    if ((2 != p_para->bus) && (3 != p_para->bus))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmdXG0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatusXG0_m));

    /*
        write data fistly, because it's at the high address
    */

    if(p_para->ctl_id)
    {
        /* for slice1 */
        drv_goldengate_chip_write(lchip, SYS_CHIPMDIOXG1_ADDR + 4, p_para->value);
    }
    else
    {
        /* for slice0 */
        drv_goldengate_chip_write(lchip, SYS_CHIPMDIOXG0_ADDR + 4, p_para->value);
    }

    SetMdioCmdXG(p_para->ctl_id, intfSelCmdXg_f, &mdio_cmd, p_para->bus - 2);
    SetMdioCmdXG(p_para->ctl_id, startCmdXg_f, &mdio_cmd, 0);
    SetMdioCmdXG(p_para->ctl_id, opCodeCmdXg_f, &mdio_cmd, SYS_CHIP_MDIO_WRITE);
    SetMdioCmdXG(p_para->ctl_id, portAddCmdXg_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmdXG(p_para->ctl_id, devAddCmdXg_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmdXG(p_para->ctl_id, regAddCmdXg_f, &mdio_cmd, p_para->reg);
    SetMdioCmdXG(p_para->ctl_id, dataCmdXg_f, &mdio_cmd, p_para->value);

    cmd = DRV_IOW(MdioCmdXG0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatusXG0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }
    GetMdioStatusXG(p_para->ctl_id, mdioXGCmdDone_f, &mdio_status, mdio_x_g_cmd_done);
    while ((mdio_x_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatusXG(p_para->ctl_id, mdioXGCmdDone_f, &mdio_status, mdio_x_g_cmd_done);
    }

    if (mdio_x_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief mdio interface for write xg phy
*/
STATIC int32
_sys_goldengate_chip_write_gephy_xgreg(uint8 lchip, ctc_chip_mdio_para_t* p_para)
{
    MdioCmd1G0_m mdio_cmd;
    MdioStatus1G0_m mdio_status;
    uint32 cmd = 0;
    uint32 timeout = SYS_CHIP_MAX_TIME_OUT;
    int32 ret = 0;
    uint8 mdio1_g_cmd_done = 0;

    SYS_CHIP_SMI_LOCK(lchip);

    sal_memset(&mdio_cmd, 0, sizeof(MdioCmd1G0_m));
    sal_memset(&mdio_status, 0, sizeof(MdioStatus1G0_m));

    /*Send address*/
    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, 0x0);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x0);
    SetMdioCmd1G(p_para->ctl_id, dataCmd1g_f, &mdio_cmd, p_para->reg);

    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);

    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    /* send write data */
    SetMdioCmd1G(p_para->ctl_id, intfSelCmd1g_f, &mdio_cmd, p_para->bus);
    SetMdioCmd1G(p_para->ctl_id, devAddCmd1g_f, &mdio_cmd, p_para->dev_no);
    SetMdioCmd1G(p_para->ctl_id, opCodeCmd1g_f, &mdio_cmd, SYS_CHIP_MDIO_WRITE);
    SetMdioCmd1G(p_para->ctl_id, portAddCmd1g_f, &mdio_cmd, p_para->phy_addr);
    SetMdioCmd1G(p_para->ctl_id, startCmd1g_f, &mdio_cmd, 0x0);
    SetMdioCmd1G(p_para->ctl_id, dataCmd1g_f, &mdio_cmd, p_para->value);


    cmd = DRV_IOW(MdioCmd1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_cmd);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    /* check status done */
    cmd = DRV_IOR(MdioStatus1G0_t + p_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
    if (ret < 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_ERROR;
    }

    GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);

    while ((mdio1_g_cmd_done == 0) && (--timeout))
    {
        ret = DRV_IOCTL(lchip, 0, cmd, &mdio_status);
        if (ret < 0)
        {
            SYS_CHIP_SMI_UNLOCK(lchip);
            return DRV_E_MDIO_CMD_ERROR;
        }
        GetMdioStatus1G(p_para->ctl_id, mdio1GCmdDone_f, &mdio_status, mdio1_g_cmd_done);
    }

    if (mdio1_g_cmd_done == 0)
    {
        SYS_CHIP_SMI_UNLOCK(lchip);
        return DRV_E_MDIO_CMD_TIMEOUT;
    }

    SYS_CHIP_SMI_UNLOCK(lchip);

    return DRV_E_NONE;
}


/**
 @brief mac led interface
*/
int32
sys_goldengate_chip_mdio_write(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    CTC_PTR_VALID_CHECK(p_para);
    LCHIP_CHECK(lchip);

    switch(type)
    {
        case CTC_CHIP_MDIO_GE:
            CTC_ERROR_RETURN(_sys_goldengate_chip_write_gephy_reg(lchip, p_para));
            break;

        case CTC_CHIP_MDIO_XG:
            CTC_ERROR_RETURN(_sys_goldengate_chip_write_xgphy_reg(lchip, p_para));
            break;

        case CTC_CHIP_MDIO_XGREG_BY_GE:
            CTC_ERROR_RETURN(_sys_goldengate_chip_write_gephy_xgreg(lchip, p_para));
        default:
            break;
    }
    return CTC_E_NONE;
}

/**
 @brief mdio interface to set mdio clock frequency
*/
int32
sys_goldengate_chip_set_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16 freq)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 core_freq = 0;

    if (type > CTC_CHIP_MDIO_XGREG_BY_GE)
    {
        return CTC_E_INVALID_PARAM;
    }


    if ((type == CTC_CHIP_MDIO_GE) || (type == CTC_CHIP_MDIO_XGREG_BY_GE))
    {
        /* for 1g mdio bus */
        core_freq = sys_goldengate_get_core_freq(lchip, 1);
        CTC_MIN_VALUE_CHECK(freq, 1);
        field_val = (core_freq*1000)/freq;
        cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgClkMdioDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = 1;
        cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioDiv_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 0;
        cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioDiv_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        /* for xg mdio bus */
        if(TRUE)
        {
            core_freq = sys_goldengate_get_core_freq(lchip, 1);
             /*field_val = (core_freq*1000)/freq;*/
            field_val = 0x50;
            cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgClkMdioXgDiv_f);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        field_val = 1;
        cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioXgDiv_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        field_val = 0;
        cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgResetClkMdioXgDiv_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    return CTC_E_NONE;
}


/**
 @brief mdio interface to set mdio clock frequency
*/
int32
sys_goldengate_chip_get_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16* p_freq)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 core_freq = 0;

    if (type > CTC_CHIP_MDIO_XGREG_BY_GE)
    {
        return CTC_E_INVALID_PARAM;
    }

    core_freq = sys_goldengate_get_core_freq(lchip, 1);

    if ((type == CTC_CHIP_MDIO_GE) || (type == CTC_CHIP_MDIO_XGREG_BY_GE))
    {
        /* for 1g mdio bus */
        cmd = DRV_IOR(SupMiscCfg_t, SupMiscCfg_cfgClkMdioDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        /* for xg mdio bus */
        cmd = DRV_IOR(SupMiscCfg_t, SupMiscCfg_cfgClkMdioXgDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    *p_freq = core_freq*1000/field_val;

    return CTC_E_NONE;
}

#define __I2C_MASTER_INTERFACE__

STATIC int32
_sys_goldengate_chip_i2c_set_switch(uint8 lchip, uint8 ctl_id, uint8 i2c_switch_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;

    field_value = (SYS_CHIP_INVALID_SWITCH_ID != i2c_switch_id)? 1 : 0;
    cmd = DRV_IOW(I2CMasterCfg0_t + ctl_id, I2CMasterCfg0_switchEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    if (i2c_switch_id)
    {
        field_value = i2c_switch_id - 1 ;
        cmd = DRV_IOW(I2CMasterCfg0_t + ctl_id, I2CMasterCfg0_switchAddr_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_set_i2c_byte_access(uint8 lchip, uint8 ctl_id, bool enable)
{

    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 tbl_id = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set byte access enable:%d\n", (enable ? 1 : 0));

    tbl_id = ctl_id ? I2CMasterCfg1_t : I2CMasterCfg0_t ;

    field_value = enable ? 1 : 0;
    cmd = DRV_IOW(tbl_id, I2CMasterCfg0_currentByteAcc_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_set_bitmap_ctl(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    sys_chip_i2c_slave_bitmap_t slave_bitmap = {0};
    I2CMasterBmpCfg0_m bitmap_ctl;
    uint32 cmd = 0;
    int32 ret = 0;

    sal_memset(&bitmap_ctl, 0, sizeof(I2CMasterBmpCfg0_m));

    if (SYS_CHIP_I2C_MAX_BITAMP > p_i2c_para->slave_dev_id)
    {
        slave_bitmap.slave_bitmap1 = (0x1 << p_i2c_para->slave_dev_id);
    }
    else
    {
        slave_bitmap.slave_bitmap2 = (0x1 << (p_i2c_para->slave_dev_id - SYS_CHIP_I2C_MAX_BITAMP));
    }

    SetI2CMasterBmpCfg(p_i2c_para->ctl_id, &bitmap_ctl, &slave_bitmap);
    cmd = DRV_IOW(I2CMasterBmpCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &bitmap_ctl);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    return DRV_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_set_master_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    int32 ret = 0;
    uint32 cmd = 0;
    I2CMasterReadCfg0_m master_rd;
    I2CMasterReadCtl0_m read_ctl;

    sal_memset(&master_rd, 0, sizeof(I2CMasterReadCfg0_m));
    sal_memset(&read_ctl, 0, sizeof(I2CMasterReadCtl0_m));

    SetI2CMasterReadCfg(p_i2c_para->ctl_id, slaveAddr_f, &master_rd, p_i2c_para->dev_addr);
    SetI2CMasterReadCfg(p_i2c_para->ctl_id, offset_f, &master_rd, p_i2c_para->offset);
    SetI2CMasterReadCfg(p_i2c_para->ctl_id, length_f, &master_rd, p_i2c_para->length);
    cmd = DRV_IOW(I2CMasterReadCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_rd);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    SetI2CMasterReadCtl(p_i2c_para->ctl_id, pollingSel_f, &read_ctl, 0);
    SetI2CMasterReadCtl(p_i2c_para->ctl_id, readEn_f, &read_ctl, 1);
    cmd = DRV_IOW(I2CMasterReadCtl0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &read_ctl);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    return DRV_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_check_master_status(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    uint32 cmd = 0;
    int32 ret = 0;
    uint32 timeout = 0;
    uint32 field_value = 0;
    I2CMasterStatus0_m master_status;

    sal_memset(&master_status, 0, sizeof(I2CMasterStatus0_m));
    timeout = 4 * p_i2c_para->length;

    cmd = DRV_IOR(I2CMasterStatus0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);

    do
    {
        sal_task_sleep(1);
        ret = DRV_IOCTL(lchip, 0, cmd, &master_status);
        if (ret < 0)
        {
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
        GetI2CMasterStatus(p_i2c_para->ctl_id, triggerReadValid_f,
                        &master_status, field_value);
    }while ((field_value == 0) && (--timeout));

    if (0 == field_value)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "i2c master read cmd not done! line = %d\n",  __LINE__);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    return DRV_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_read_data(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    uint8 index = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    uint32 field_value = 0;
    I2CMasterDataMem0_m data_buf;

    sal_memset(&data_buf, 0, sizeof(I2CMasterDataMem0_m));

    cmd = DRV_IOR(I2CMasterDataMem0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);

    for (index = 0; index < p_i2c_para->length; index++)
    {
        ret = DRV_IOCTL(lchip, index, cmd, &data_buf);
        if (ret < 0)
        {
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }

        if (p_i2c_para->ctl_id)
        {
            p_i2c_para->p_buf[index] = GetI2CMasterDataMem1(V, data_f, &data_buf);
        }
        else
        {
            p_i2c_para->p_buf[index] = GetI2CMasterDataMem0(V, data_f, &data_buf);
        }
    }

    /* clear read cmd */
    field_value = 0;
    cmd = DRV_IOW(I2CMasterReadCtl0_t + p_i2c_para->ctl_id, I2CMasterReadCtl0_readEn_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    return DRV_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_check_data(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)

{
    uint32 cmd = 0;
    int32 ret = 0;
    uint32 arr_read_status[2] = {0};
    I2CMasterReadStatus0_m read_status;

    sal_memset(&read_status, 0, sizeof(I2CMasterReadStatus0_m));

    /*check read status */
    cmd = DRV_IOR(I2CMasterReadStatus0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &read_status);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    GetI2CMasterReadStatus(p_i2c_para->ctl_id, &read_status, arr_read_status);

    if ((0 != arr_read_status[0]) || (0 != arr_read_status[1]))
    {

        if (p_i2c_para->p_buf[0] == 0xff)
        {
            return DRV_E_I2C_MASTER_NACK_ERROR;
        }
        else if (p_i2c_para->p_buf[0] == 0)
        {
            return DRV_E_I2C_MASTER_CRC_ERROR;
        }
        else
        {
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
    }

    return CTC_E_NONE;
}

/**
 @brief i2c master for read sfp
*/
int32
sys_goldengate_chip_i2c_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_i2c_para);
    CTC_PTR_VALID_CHECK(p_i2c_para->p_buf);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "read sfp, bitmap:0x%x addr:0x%x  offset:0x%x length:0x%x \n",
                     p_i2c_para->slave_bitmap, p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->length);

    if ((p_i2c_para->buf_length == 0)
        || (p_i2c_para->buf_length < p_i2c_para->length)
        || ((SYS_CHIP_CONTROL0_ID != p_i2c_para->ctl_id)
            && (SYS_CHIP_CONTROL1_ID != p_i2c_para->ctl_id)))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_CHIP_I2C_LOCK(lchip);

    if (p_i2c_para->access_switch)
    {
        p_i2c_para->i2c_switch_id = 0;
        p_i2c_para->length = 1;
        CTC_ERROR_GOTO(_sys_goldengate_chip_set_i2c_byte_access(lchip, p_i2c_para->ctl_id, TRUE), ret, EXIT);
    }

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_set_switch(lchip, p_i2c_para->ctl_id, p_i2c_para->i2c_switch_id),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_set_bitmap_ctl(lchip, p_i2c_para),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_set_master_read(lchip, p_i2c_para),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_check_master_status(lchip, p_i2c_para),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_read_data(lchip, p_i2c_para),
        ret, error_0);

    /*check read status */
    CTC_ERROR_GOTO( _sys_goldengate_chip_i2c_check_data(lchip, p_i2c_para),
        ret, error_0);

error_0:
    if (p_i2c_para->access_switch)
    {
        CTC_ERROR_GOTO(_sys_goldengate_chip_set_i2c_byte_access(lchip, p_i2c_para->ctl_id, FALSE), ret, EXIT);
    }
EXIT:
    SYS_CHIP_I2C_UNLOCK(lchip);
    return ret;
}

STATIC int32
_sys_goldengate_chip_i2c_check_write_valid(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{
    uint32 cmd = 0;
    int32 ret = 0;
    uint32 field_value = 0;
    I2CMasterStatus0_m master_status;

    sal_memset(&master_status, 0, sizeof(I2CMasterStatus0_m));

    /* check status */
    if (p_i2c_para->ctl_id)
    {
        cmd = DRV_IOR(I2CMasterReadCtl1_t, I2CMasterReadCtl1_readEn_f);
    }
    else
    {
        cmd = DRV_IOR(I2CMasterReadCtl0_t, I2CMasterReadCtl0_readEn_f);
    }

    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    if (field_value != 0)
    {
        return DRV_E_I2C_MASTER_READ_NOT_CLEAR;
    }

    #if 0
    /*
        hardware is not sure to set pollingDone_f 1
    */
    cmd = DRV_IOR(I2CMasterStatus0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(p_i2c_para->lchip, 0, cmd, &master_status);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    GetI2CMasterStatus(p_i2c_para->ctl_id, pollingDone_f, &master_status, polling_done);

    if (polling_done != 1)
    {
        /* chip init status polling_done is set */
        DRV_DBG_INFO("i2c master write check polling fail! line = %d\n",  __LINE__);
        return DRV_E_I2C_MASTER_POLLING_NOT_DONE;
    }
    #endif

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_write_data(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{
    uint32 cmd = 0;
    int32 ret = 0;
    sys_chip_i2c_slave_bitmap_t slave_bitmap = {0};
    I2CMasterBmpCfg0_m bitmap_ctl;
    I2CMasterWrCfg0_m master_wr;
    I2CMasterReadCfg0_m master_rd;

    sal_memset(&master_wr, 0, sizeof(I2CMasterWrCfg0_m));
    sal_memset(&master_rd, 0, sizeof(I2CMasterReadCfg0_m));
    sal_memset(&bitmap_ctl, 0, sizeof(I2CMasterBmpCfg0_m));

    if (SYS_CHIP_I2C_MAX_BITAMP > p_i2c_para->slave_id)
    {
        slave_bitmap.slave_bitmap1 = (0x1 << p_i2c_para->slave_id);
    }
    else
    {
        slave_bitmap.slave_bitmap2 = (0x1 << (p_i2c_para->slave_id - SYS_CHIP_I2C_MAX_BITAMP));
    }

    SetI2CMasterBmpCfg(p_i2c_para->ctl_id, &bitmap_ctl, &slave_bitmap);

    cmd = DRV_IOW(I2CMasterBmpCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &bitmap_ctl);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    SetI2CMasterReadCfg(p_i2c_para->ctl_id, slaveAddr_f, &master_rd, p_i2c_para->dev_addr);
    SetI2CMasterReadCfg(p_i2c_para->ctl_id, offset_f, &master_rd, p_i2c_para->offset);
    cmd = DRV_IOW(I2CMasterReadCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_rd);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    SetI2CMasterWrCfg(p_i2c_para->ctl_id, wrData_f, &master_wr, p_i2c_para->data);
    SetI2CMasterWrCfg(p_i2c_para->ctl_id, wrEn_f, &master_wr, 1);
    cmd = DRV_IOW(I2CMasterWrCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &master_wr);
    if (ret < 0)
    {
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_i2c_check_write_finish(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{
    uint32 cmd_cfg = 0;
    uint32 cmd_dbg = 0;
    int32 ret = 0;
    uint32 timeout = 4;
    I2CMasterWrCfg0_m master_wr;
    I2CMasterDebugState0_m master_dbg;
    uint8 i2c_wr_en = 0;
    uint32 i2c_status = 0;

    sal_memset(&master_wr, 0, sizeof(I2CMasterWrCfg0_m));
    sal_memset(&master_dbg, 0, sizeof(I2CMasterDebugState0_m));

    /* wait write op done */
    cmd_cfg = DRV_IOR(I2CMasterWrCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
    cmd_dbg = DRV_IOR(I2CMasterDebugState0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);

    do
    {
        sal_task_sleep(1);
        ret = DRV_IOCTL(lchip, 0, cmd_cfg, &master_wr);
        if (ret < 0)
        {
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
        GetI2CMasterWrCfg(p_i2c_para->ctl_id, wrEn_f, &master_wr, i2c_wr_en );

        ret = DRV_IOCTL(lchip, 0, cmd_dbg, &master_dbg);
        if (ret < 0)
        {
            return DRV_E_I2C_MASTER_CMD_ERROR;
        }
        GetI2CMasterDebugState(p_i2c_para->ctl_id, i2cAccState_f, &master_dbg, i2c_status);
    }while (((1 == i2c_wr_en) || (0 != i2c_status)) && (--timeout));

    if (((1 == i2c_wr_en) || (0 != i2c_status)))
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "i2c master write cmd not done! line = %d\n \
                    wrEn:%u, i2cAccState:%u \n",  __LINE__, i2c_wr_en, i2c_status);
        return DRV_E_I2C_MASTER_CMD_ERROR;
    }
    return CTC_E_NONE;
}

/**
 @brief i2c master for write sfp
*/
int32
sys_goldengate_chip_i2c_write(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{


    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_i2c_para);

    if (((SYS_CHIP_CONTROL0_ID != p_i2c_para->ctl_id)
            && (SYS_CHIP_CONTROL1_ID != p_i2c_para->ctl_id)))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "write sfp, slave_id:0x%x addr:0x%x  offset:0x%x data:0x%x \n",
                     p_i2c_para->slave_id, p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->data);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "chip-id:%u \n",
                     p_i2c_para->lchip);

    DRV_PTR_VALID_CHECK(p_i2c_para);

    SYS_CHIP_I2C_LOCK(lchip);
    if (p_i2c_para->access_switch)
    {
        p_i2c_para->i2c_switch_id = 0;
        CTC_ERROR_GOTO(_sys_goldengate_chip_set_i2c_byte_access(lchip, p_i2c_para->ctl_id, TRUE), ret, EXIT);
    }

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_set_switch(lchip, p_i2c_para->ctl_id, p_i2c_para->i2c_switch_id),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_check_write_valid(lchip, p_i2c_para),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_write_data(lchip, p_i2c_para),
        ret, error_0);

    CTC_ERROR_GOTO(_sys_goldengate_chip_i2c_check_write_finish(lchip, p_i2c_para),
        ret, error_0);

error_0:
    if (p_i2c_para->access_switch)
    {
        CTC_ERROR_GOTO(_sys_goldengate_chip_set_i2c_byte_access(lchip, p_i2c_para->ctl_id, FALSE), ret, EXIT);
    }
EXIT:
    SYS_CHIP_I2C_UNLOCK(lchip);
    return CTC_E_NONE;
}


/**
 @brief set i2c polling read para
*/
STATIC int32
_sys_goldengate_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    I2CMasterReadCfg0_m master_rd;
    I2CMasterReadCtl0_m read_ctl;
    I2CMasterBmpCfg0_m bitmap_ctl;
    I2CMasterPollingCfg0_m polling_cfg;  /*check*/
    uint8 slave_num = 0;
    uint8 index = 0;

    DRV_PTR_VALID_CHECK(p_i2c_para);

    sal_memset(&master_rd, 0, sizeof(I2CMasterReadCfg0_m));
    sal_memset(&read_ctl, 0, sizeof(I2CMasterReadCtl0_m));
    sal_memset(&bitmap_ctl, 0, sizeof(I2CMasterBmpCfg0_m));
    sal_memset(&polling_cfg, 0, sizeof(I2CMasterPollingCfg0_m));

    CTC_ERROR_RETURN(_sys_goldengate_chip_i2c_set_switch(lchip, p_i2c_para->ctl_id, p_i2c_para->i2c_switch_id));

    if (SYS_CHIP_FLAG_ISSET(p_i2c_para->op_flag, SYS_CHIP_SFP_BITMAP_OP)
        || SYS_CHIP_FLAG_ISZERO(p_i2c_para->op_flag))
    {
        SetI2CMasterBmpCfg(p_i2c_para->ctl_id, &bitmap_ctl, p_i2c_para->slave_bitmap);
        cmd = DRV_IOW(I2CMasterBmpCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &bitmap_ctl));
        for (index = 0; index < 32; index++)
        {
            if (CTC_IS_BIT_SET(p_i2c_para->slave_bitmap[p_i2c_para->ctl_id], index))
            {
                slave_num++;
            }
        }
    }

    if (SYS_CHIP_FLAG_ISSET(p_i2c_para->op_flag, SYS_CHIP_SFP_SCAN_REG_OP)
        || SYS_CHIP_FLAG_ISZERO(p_i2c_para->op_flag))
    {
        if ((slave_num*p_i2c_para->length) > SYS_CHIP_I2C_READ_MAX_LENGTH)
        {
            return CTC_E_INVALID_PARAM;
        }
        SetI2CMasterReadCfg(p_i2c_para->ctl_id, slaveAddr_f,
                        &master_rd, p_i2c_para->dev_addr);
        SetI2CMasterReadCfg(p_i2c_para->ctl_id, length_f,
                        &master_rd, p_i2c_para->length);
        SetI2CMasterReadCfg(p_i2c_para->ctl_id, offset_f,
                        &master_rd, p_i2c_para->offset);
        cmd = DRV_IOW(I2CMasterReadCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &master_rd));
    }

    if (SYS_CHIP_FLAG_ISSET(p_i2c_para->op_flag, SYS_CHIP_SFP_INTERVAL_OP)
        || SYS_CHIP_FLAG_ISZERO(p_i2c_para->op_flag))
    {
        SetI2CMasterPollingCfg(p_i2c_para->ctl_id, pollingInterval_f,
                            &polling_cfg, p_i2c_para->interval);
        cmd = DRV_IOW(I2CMasterPollingCfg0_t + p_i2c_para->ctl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &polling_cfg));
    }

    field_value = 1;
    if (p_i2c_para->ctl_id)
    {
        cmd = DRV_IOW(I2CMasterReadCtl1_t, I2CMasterReadCtl1_pollingSel_f);
    }
    else
    {
        cmd = DRV_IOW(I2CMasterReadCtl0_t, I2CMasterReadCtl0_pollingSel_f);
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return DRV_E_NONE;
}


/**
 @brief i2c master for polling read
*/
int32
sys_goldengate_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para)
{

    CTC_PTR_VALID_CHECK(p_i2c_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "scan sfp, op_flag:0x%x, bitmap:0x%x addr:0x%x  offset:0x%x length:0x%x intreval:0x%x\n",
                     p_i2c_para->op_flag, p_i2c_para->slave_bitmap[0], p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->length, p_i2c_para->interval);

    CTC_ERROR_RETURN(_sys_goldengate_chip_set_i2c_scan_para(lchip, p_i2c_para));

    return CTC_E_NONE;
}

/**
 @brief i2c master for polling read start
*/
int32
sys_goldengate_chip_set_i2c_scan_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_value = (TRUE == enable) ? 0 : 1;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));

    cmd = DRV_IOW(I2CMasterReadCtl0_t, I2CMasterReadCtl0_readEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

/**
 @brief interface for read i2c databuf, usual used for i2c master for polling read
*/
int32
sys_goldengate_chip_read_i2c_buf(uint8 lchip, ctc_chip_i2c_scan_read_t *p_i2c_scan_read)
{
    uint32 index = 0;
    uint32 cmd = 0;
    I2CMasterDataMem0_m data_buf;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "read sfp buf, lchip:%d len:%d\n",
                                                lchip, p_i2c_scan_read->len);

    CTC_PTR_VALID_CHECK(p_i2c_scan_read);
    CTC_PTR_VALID_CHECK(p_i2c_scan_read->p_buf);

    if (p_i2c_scan_read->len > SYS_CHIP_I2C_READ_MAX_LENGTH)
    {
        return DRV_E_ILLEGAL_LENGTH;
    }

    if (TRUE != sys_goldengate_chip_is_local(lchip, p_i2c_scan_read->gchip))
    {
        return CTC_E_INVALID_LOCAL_CHIPID;
    }

    sal_memset(&data_buf, 0, sizeof(I2CMasterDataMem0_m));

    cmd = DRV_IOR(I2CMasterDataMem0_t + p_i2c_scan_read->ctl_id, DRV_ENTRY_FLAG);

    for (index = 0; index < p_i2c_scan_read->len; index++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &data_buf));

        p_i2c_scan_read->p_buf[index] = GetI2CMasterDataMem0(V, data_f, &data_buf);
    }

    return CTC_E_NONE;
}

#define __MAC_LED__


/**
 @brief mac led interface to get mac led mode cfg
*/
STATIC int32
_sys_chip_get_mac_led_mode_cfg(ctc_chip_led_mode_t mode, uint8* p_value)
{
    uint8 temp = 0;

    switch(mode)
    {
        case CTC_CHIP_RXLINK_MODE:
            temp = (SYS_CHIP_LED_ON_RX_LINK & 0x3)
                | ((SYS_CHIP_LED_BLINK_OFF & 0x3) << 2);
            break;

        case CTC_CHIP_TXLINK_MODE:
            temp = (SYS_CHIP_LED_ON_TX_LINK & 0x3)
                | ((SYS_CHIP_LED_BLINK_OFF & 0x3) << 2);
            break;

        case CTC_CHIP_RXLINK_RXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_ON_RX_LINK & 0x3)
                | ((SYS_CHIP_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

        case CTC_CHIP_TXLINK_TXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_ON_TX_LINK & 0x3)
                | ((SYS_CHIP_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

        case CTC_CHIP_RXLINK_BIACTIVITY_MODE:
            temp = (SYS_CHIP_LED_ON_RX_LINK & 0x3)
                | ((SYS_CHIP_LED_BLINK_ON & 0x3) << 2);
            break;

        case CTC_CHIP_TXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_OFF & 0x3)
                | ((SYS_CHIP_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

        case CTC_CHIP_RXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_OFF & 0x3)
                | ((SYS_CHIP_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

        case CTC_CHIP_BIACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_OFF & 0x3)
                | ((SYS_CHIP_LED_BLINK_ON & 0x3) << 2);
            break;

        case CTC_CHIP_FORCE_ON_MODE:
            temp = (SYS_CHIP_LED_FORCE_ON & 0x3)
                | ((SYS_CHIP_LED_BLINK_OFF & 0x3) << 2);
            break;

        case CTC_CHIP_FORCE_OFF_MODE:
            temp = (SYS_CHIP_LED_FORCE_OFF & 0x3)
                | ((SYS_CHIP_LED_BLINK_OFF & 0x3) << 2);
            break;

       case CTC_CHIP_FORCE_ON_TXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_ON & 0x3)
                | ((SYS_CHIP_LED_BLINK_TX_ACT & 0x3) << 2);
            break;

       case CTC_CHIP_FORCE_ON_RXACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_ON & 0x3)
                | ((SYS_CHIP_LED_BLINK_RX_ACT & 0x3) << 2);
            break;

       case CTC_CHIP_FORCE_ON_BIACTIVITY_MODE:
            temp = (SYS_CHIP_LED_FORCE_ON & 0x3)
                | ((SYS_CHIP_LED_BLINK_ON & 0x3) << 2);
            break;

        default:
            return DRV_E_INVALID_PARAMETER;
    }

    *p_value = temp;

    return CTC_E_NONE;
}



/**
 @brief mac led interface
*/
int32
sys_goldengate_chip_set_mac_led_mode(uint8 lchip, ctc_chip_led_para_t* p_led_para, ctc_chip_mac_led_type_t led_type, uint8 inner)
{
    uint32 cmd = 0;
    uint32 field_index = 0;
    MacLedCfgPortMode0_m led_mode;
    MacLedPolarityCfg0_m led_polarity;
    uint8 led_cfg = 0;

    CTC_PTR_VALID_CHECK(p_led_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&led_mode, 0, sizeof(MacLedCfgPortMode0_m));
    sal_memset(&led_polarity, 0, sizeof(MacLedPolarityCfg0_m));

    DRV_PTR_VALID_CHECK(p_led_para);

    if (led_type >= CTC_CHIP_MAX_LED_TYPE)
    {
        return DRV_E_INVALID_PARAMETER;
    }

    if (SYS_CHIP_FLAG_ISSET(p_led_para->op_flag, SYS_CHIP_LED_MODE_SET_OP)
        || SYS_CHIP_FLAG_ISZERO(p_led_para->op_flag))
    {
        CTC_ERROR_RETURN(_sys_chip_get_mac_led_mode_cfg(p_led_para->first_mode, &led_cfg));

        SetMacLedCfgPortMode(p_led_para->ctl_id, primaryLedMode_f, &led_mode, led_cfg);

        if (led_type == CTC_CHIP_USING_TWO_LED)
        {
            CTC_ERROR_RETURN(_sys_chip_get_mac_led_mode_cfg(p_led_para->sec_mode, &led_cfg));
            SetMacLedCfgPortMode(p_led_para->ctl_id, secondaryLedMode_f, &led_mode, led_cfg);
            SetMacLedCfgPortMode(p_led_para->ctl_id, secondaryLedModeEn_f, &led_mode, 1);
        }

        cmd = DRV_IOW(MacLedCfgPortMode0_t + p_led_para->ctl_id, DRV_ENTRY_FLAG);
        field_index = p_led_para->port_id;
        if (field_index >= 48)
        {
            /* for GG, if mac-id bigger than 48, the relation channel-id = mac-id - 8 */
            field_index = field_index - 8;
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_index, cmd, &led_mode));

        if (0 == inner)
        {
            CTC_ERROR_RETURN(sys_goldengate_port_store_led_mode_to_lport_attr(lchip, p_led_para->port_id + p_led_para->ctl_id*64, p_led_para->first_mode, (led_type == CTC_CHIP_USING_TWO_LED)?p_led_para->sec_mode:CTC_CHIP_MAC_LED_MODE));
        }
    }

    if (SYS_CHIP_FLAG_ISSET(p_led_para->op_flag, SYS_CHIP_LED_POLARITY_SET_OP)
        || SYS_CHIP_FLAG_ISZERO(p_led_para->op_flag))
    {
        /* set polarity for driving led 1: low driver led 0: high driver led */
        SetMacLedPolarityCfg(p_led_para->ctl_id, polarityInv_f, &led_polarity, p_led_para->polarity);
        cmd = DRV_IOW(MacLedPolarityCfg0_t + p_led_para->ctl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_polarity));

    }

    return CTC_E_NONE;
}

/**
 @brief mac led interface for mac and led mapping
*/
int32
sys_goldengate_chip_set_mac_led_mapping(uint8 lchip, ctc_chip_mac_led_mapping_t* p_led_map)
{
    uint32 index = 0;
    uint32 cmd = 0;

    MacLedCfgPortSeqMap0_m seq_map;
    MacLedPortRange0_m port_range;


    CTC_PTR_VALID_CHECK(p_led_map);
    CTC_PTR_VALID_CHECK(p_led_map->p_mac_id);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac num:%d \n", p_led_map->mac_led_num);

    for (index = 0; index < p_led_map->mac_led_num; index++)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac seq %d:%d;  ", index, p_led_map->p_mac_id[index]);
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n");

    sal_memset(&seq_map, 0, sizeof(MacLedCfgPortSeqMap0_m));
    sal_memset(&port_range, 0, sizeof(MacLedPortRange0_m));

    if ((p_led_map->mac_led_num > SYS_CHIP_MAX_PHY_PORT) || (p_led_map->mac_led_num < 1))
    {
        return DRV_E_INVALID_PARAMETER;
    }

    /* set portseq */
    cmd = DRV_IOW(MacLedCfgPortSeqMap0_t + p_led_map->ctl_id, DRV_ENTRY_FLAG);

    for (index = 0; index < p_led_map->mac_led_num; index++)
    {
        if (p_led_map->p_mac_id[index] >= 48)
        {
            /* for GG, if mac-id bigger than 48, the relation channel-id = mac-id - 8 */
            p_led_map->p_mac_id[index] = p_led_map->p_mac_id[index] - 8;
        }
        SetMacLedCfgPortSeqMap(p_led_map->ctl_id, macId_f, &seq_map,
                    *((uint8*)p_led_map->p_mac_id + index));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &seq_map));
    }

    /* set MacLedPortRange */
    SetMacLedPortRange(p_led_map->ctl_id, portStartIndex_f, &port_range, 0);
    SetMacLedPortRange(p_led_map->ctl_id, portEndIndex_f,
                        &port_range, p_led_map->mac_led_num - 1);
    cmd = DRV_IOW(MacLedPortRange0_t + p_led_map->ctl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &port_range));

    return CTC_E_NONE;
}

/**
 @brief begin mac led function
*/
int32
sys_goldengate_chip_set_mac_led_en(uint8 lchip, bool enable)
{
    uint8 index = 0;
    uint32 cmd = 0;
    MacLedSampleInterval0_m led_sample;
    MacLedRawStatusCfg0_m raw_cfg;
    MacLedCfgCalCtl0_m cal_ctrl;
    uint32 field_value = (TRUE == enable) ? 1 : 0;
    uint32 refresh_interval = 0x3fffff;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac led Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));

    sal_memset(&led_sample, 0, sizeof(MacLedSampleInterval0_m));
    sal_memset(&raw_cfg, 0, sizeof(MacLedRawStatusCfg0_m));
    sal_memset(&cal_ctrl, 0, sizeof(MacLedCfgCalCtl0_m));

    for (index = 0; index < SYS_CHIP_CONTROL_NUM; ++index)
    {
        cmd = DRV_IOW(MacLedCfgCalCtl0_t + index, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cal_ctrl));

        cmd = DRV_IOW(MacLedRawStatusCfg0_t + index, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &raw_cfg));

        cmd = DRV_IOR(MacLedSampleInterval0_t + index, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_sample));

        SetMacLedSampleInterval(index, histogramEn_f, &led_sample, field_value);
        SetMacLedSampleInterval(index, sampleEn_f, &led_sample, field_value);

        SetMacLedSampleInterval(index, sampleInterval_f, &led_sample, 0x019f6300);

        cmd = DRV_IOW(MacLedSampleInterval0_t + index, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &led_sample));

        cmd = DRV_IOW(MacLedRefreshInterval0_t + index, MacLedRefreshInterval0_refreshEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(MacLedRefreshInterval0_t + index, MacLedRefreshInterval0_refreshInterval_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &refresh_interval));
    }

    return CTC_E_NONE;
}

/**
 @brief gpio interface
*/
int32
sys_goldengate_chip_set_gpio_mode(uint8 lchip, uint8 gpio_id, ctc_chip_gpio_mode_t mode)
{
    uint32 cmd = 0;
    uint32 arr_index[] = {
            GpioMacroCtl_gpio0OutEn_f,
            GpioMacroCtl_gpio1OutEn_f,
            GpioMacroCtl_gpio2OutEn_f,
            GpioMacroCtl_gpio3OutEn_f,
            GpioMacroCtl_gpio4OutEn_f,
            GpioMacroCtl_gpio5OutEn_f,
            GpioMacroCtl_gpio6OutEn_f,
            GpioMacroCtl_gpio7OutEn_f,
            GpioMacroCtl_gpio8OutEn_f,
            GpioMacroCtl_gpio9OutEn_f
        };

    if ((gpio_id > SYS_CHIP_MAC_GPIO_ID)
        || ((CTC_CHIP_INPUT_MODE != mode) && (CTC_CHIP_OUTPUT_MODE != mode)))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set gpio mode, gpio_id:%d mode:%d\n", gpio_id, mode);

    cmd = DRV_IOW(GpioMacroCtl_t, arr_index[gpio_id - 1]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mode));

    return CTC_E_NONE;
}

/**
 @brief gpio output
*/
int32
sys_goldengate_chip_set_gpio_output(uint8 lchip, uint8 gpio_id, uint8 out_para)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 arr_index[] = {
            GpioMacroCtl_gpio0Out_f,
            GpioMacroCtl_gpio1Out_f,
            GpioMacroCtl_gpio2Out_f,
            GpioMacroCtl_gpio3Out_f,
            GpioMacroCtl_gpio4Out_f,
            GpioMacroCtl_gpio5Out_f,
            GpioMacroCtl_gpio6Out_f,
            GpioMacroCtl_gpio7Out_f,
            GpioMacroCtl_gpio8Out_f,
            GpioMacroCtl_gpio9Out_f
        };

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set gpio out, gpio_id:%d out_para:%d\n", gpio_id, out_para);

    value = out_para;

    cmd = DRV_IOW(GpioMacroCtl_t, arr_index[gpio_id - 1]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    return CTC_E_NONE;
}


/**
 @brief gpio input
*/
int32
sys_goldengate_chip_get_gpio_input(uint8 lchip, uint8 gpio_id, uint8* in_value)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 arr_index[] = {
            GpioMacroMon_gpio0In_f,
            GpioMacroMon_gpio1In_f,
            GpioMacroMon_gpio2In_f,
            GpioMacroMon_gpio3In_f,
            GpioMacroMon_gpio4In_f,
            GpioMacroMon_gpio5In_f,
            GpioMacroMon_gpio6In_f,
            GpioMacroMon_gpio7In_f,
            GpioMacroMon_gpio8In_f,
            GpioMacroMon_gpio9In_f
        };

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOR(GpioMacroMon_t, arr_index[gpio_id - 1]);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    *in_value = (uint8)value;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "get gpio in, gpio_id:%d in_para:%d\n", gpio_id, *in_value);
    return CTC_E_NONE;
}


/**
 @brief access type switch interface
*/
int32
sys_goldengate_chip_set_access_type(uint8 lchip, ctc_chip_access_type_t access_type)
{
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set access, access_type:%d\n", access_type);

    if (access_type >= CTC_CHIP_MAX_ACCESS_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_goldengate_chip_set_access_type(access_type));
    return CTC_E_NONE;
}

/**
 @brief access type switch interface
*/
int32
sys_goldengate_chip_get_access_type(uint8 lchip, ctc_chip_access_type_t* p_access_type)
{
    drv_access_type_t drv_access;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_ERROR_RETURN(drv_goldengate_chip_get_access_type(&drv_access));
    *p_access_type = (ctc_chip_access_type_t)drv_access;
    return CTC_E_NONE;
}

#define __DERDES_OPERATE__

/**
 @brief get serdes info from lport
*/
int32
sys_goldengate_chip_get_serdes_info(uint8 lchip, uint8 lport, uint8* macro_idx, uint8* link_idx)
{
#ifdef NEVER
    if (0 == SDK_WORK_PLATFORM)
    {
        uint8 sgmac_id = 0;
        uint8 serdes_id = 0;

        if (lport >= 60)
        {
            return CTC_E_INVALID_PARAM;
        }

        if (lport < 48)
        {
            /* gmac serdes info */
            CTC_ERROR_RETURN(drv_goldengate_get_gmac_info(lchip, lport, DRV_CHIP_MAC_SERDES_INFO, (void*)&serdes_id));
        }
        else
        {
            /* sgmac serdes info */
            sgmac_id = lport - 48;
            CTC_ERROR_RETURN(drv_goldengate_get_sgmac_info(lchip, sgmac_id, DRV_CHIP_MAC_SERDES_INFO, (void*)&serdes_id));
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "serdes_id:%d \n",serdes_id);

        /* get macro idx */
        CTC_ERROR_RETURN(drv_goldengate_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_HSSID_INFO, (void*)macro_idx));

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "macro_idx:%d \n",*macro_idx);

        /* get link idx */
        CTC_ERROR_RETURN(drv_goldengate_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, (void*)link_idx));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "link_idx:%d \n",*link_idx);

    }

    return CTC_E_NONE;
#endif /* never */
    return CTC_E_NONE;
}

uint16
sys_goldengate_get_core_freq(uint8 lchip, uint8 type)
{
    return sys_goldengate_datapath_get_core_clock(lchip, type);
}

#define __DYNAMIC_SWITCH__


int32
sys_goldengate_chip_get_device_info(uint8 lchip, ctc_chip_device_info_t* p_device_info)
{
    uint32      cmd = 0;
    DevId_m    read_device_id;
    uint32      tmp_device_id = 0;
    uint32      get_device_id = 0;
    uint32      get_version_id = 0;

    CTC_PTR_VALID_CHECK(p_device_info);


    cmd = DRV_IOR(DevId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &read_device_id));

    tmp_device_id = GetDevId(V,deviceId_f,&read_device_id);

    get_version_id = tmp_device_id & 0x0F;
    get_device_id  = (tmp_device_id >> 4) & 0x0F;


    switch(get_device_id)
    {
        case 0x06:
                p_device_info->device_id = SYS_CHIP_DEVICE_ID_GG_CTC8096;
                sal_strcpy(p_device_info->chip_name, "CTC8096");
                break;
        default:
                p_device_info->device_id  = SYS_CHIP_DEVICE_ID_INVALID;
                sal_strcpy(p_device_info->chip_name, "Not Support Chip");
                break;
    }
    p_device_info->version_id = get_version_id;

    return CTC_E_NONE;
}


int32
sys_goldengate_chip_set_dlb_chan_type(uint8 lchip, uint8 chan_id)
{
    uint32 value = 0;
    uint32 cmd = 0;
    uint16 lport = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;


    if ( chan_id >= (SYS_MAX_CHANNEL_ID*2))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* get local phy port by chan_id */
    lport = SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id);

    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, lport, &port_attr));
    if (port_attr->port_type == SYS_DATAPATH_NETWORK_PORT)
    {
        switch (port_attr->pcs_mode)
        {
            case CTC_CHIP_SERDES_SGMII_MODE:
            case CTC_CHIP_SERDES_2DOT5G_MODE:
                value = 0;  /* 1G, get channel type by datapath */
                break;

            case CTC_CHIP_SERDES_XFI_MODE:
            case CTC_CHIP_SERDES_XAUI_MODE:
            case CTC_CHIP_SERDES_DXAUI_MODE:
                value = 1;  /* 10G, get channel type by datapath */
                break;

            case CTC_CHIP_SERDES_XLG_MODE:
                value = 2;  /* 40G, get channel type by datapath */
                break;

            case CTC_CHIP_SERDES_CG_MODE:
                value = 3;  /* 100G, get channel type by datapath */
                break;

            default:
                break;
        }

        cmd = DRV_IOW(DlbChanMaxLoadType_t, DlbChanMaxLoadType_loadType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &value));
    }

    return CTC_E_NONE;
}

/* dynamic switch */
int32
sys_goldengate_chip_set_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info)
{
    uint16 drv_port = 0;
    uint16 gport = 0;
    uint8 slice_id = 0;
    uint8 chan_id = 0;
    uint8 gchip_id = 0;
    sys_datapath_serdes_info_t* p_serdes = NULL;

    CTC_PTR_VALID_CHECK(p_serdes_info);

    if (p_serdes_info->serdes_id >= SYS_GG_DATAPATH_SERDES_NUM)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_serdes_info->serdes_mode >= CTC_CHIP_MAX_SERDES_MODE)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SWITCH_LOCK(lchip);

    CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_datapath_set_serdes_mode(lchip, p_serdes_info->serdes_id,
        p_serdes_info->serdes_mode, p_serdes_info->overclocking_speed), p_gg_chip_master[lchip]->p_switch_mutex);

    SWITCH_UNLOCK(lchip);

    if (CTC_CHIP_SERDES_NONE_MODE != p_serdes_info->serdes_mode)
    {
        CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_info(lchip, p_serdes_info->serdes_id, &p_serdes));

        slice_id = (p_serdes_info->serdes_id >= SERDES_NUM_PER_SLICE) ? 1 : 0;
        drv_port = p_serdes->lport + SYS_CHIP_PER_SLICE_PORT_NUM * slice_id;
        CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip_id));
        gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, drv_port);
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
        CTC_ERROR_RETURN(sys_goldengate_chip_set_dlb_chan_type(lchip, chan_id));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_set_mem_scan_mode(uint8 lchip, ctc_chip_mem_scan_cfg_t* p_cfg)
{
    if (0xFF != p_cfg->tcam_scan_mode)
    {
        drv_goldengate_ecc_recover_set_mem_scan_mode(DRV_ECC_SCAN_TYPE_TCAM, p_cfg->tcam_scan_mode, p_cfg->tcam_scan_interval);
    }

    if (0xFF != p_cfg->sbe_scan_mode)
    {
        drv_goldengate_ecc_recover_set_mem_scan_mode(DRV_ECC_SCAN_TYPE_SBE, p_cfg->sbe_scan_mode, p_cfg->sbe_scan_interval);
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_get_mem_scan_mode(uint8 lchip, ctc_chip_mem_scan_cfg_t* p_cfg)
{
    drv_goldengate_ecc_recover_get_mem_scan_mode(DRV_ECC_SCAN_TYPE_TCAM, &p_cfg->tcam_scan_mode, &p_cfg->tcam_scan_interval);
    drv_goldengate_ecc_recover_get_mem_scan_mode(DRV_ECC_SCAN_TYPE_SBE, &p_cfg->sbe_scan_mode, &p_cfg->sbe_scan_interval);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_chip_set_global_eee_en(uint8 lchip, uint32* enable)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 tbl_id = 0;

    /* cfg NetTxMiscCtl */
    value = *enable?1:0;

    tbl_id = NetTxMiscCtl0_t;
    cmd = DRV_IOW(tbl_id, NetTxMiscCtl0_eeeCtlEnable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    tbl_id = NetTxMiscCtl1_t;
    cmd = DRV_IOW(tbl_id, NetTxMiscCtl0_eeeCtlEnable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

/**
 @brief begin memory BIST function
*/
int32
_sys_goldengate_chip_set_mem_bist(uint8 lchip, ctc_chip_mem_bist_t* p_value)
{
    uint8 index = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    CTC_PTR_VALID_CHECK(p_value);

    if (1 == SDK_WORK_PLATFORM)
    {
        return CTC_E_NONE;
    }

    /*deinit*/
    CTC_ERROR_RETURN(sys_goldengate_chip_set_active(lchip, FALSE));
    if (TRUE == sys_goldengate_l2_fdb_trie_sort_en(lchip))
    {
        CTC_ERROR_RETURN(sys_goldengate_fdb_sort_deinit(lchip));
    }
    CTC_ERROR_RETURN(sys_goldengate_interrupt_deinit(lchip));
    CTC_ERROR_RETURN(sys_goldengate_dma_deinit(lchip));
    CTC_ERROR_RETURN(sys_goldengate_learning_aging_deinit(lchip));
    CTC_ERROR_RETURN(sys_goldengate_packet_deinit(lchip));
    CTC_ERROR_RETURN(sys_goldengate_port_deinit(lchip));

    field_val = 0;
    cmd = DRV_IOW(SysMabistGo_t, SysMabistGo_sysMabistGo_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*set supResetCtl*/
    drv_goldengate_chip_write(lchip, SUP_RESET_CTL_ADDR, 0x00000000);

    /*set pll core cfg*/
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR, 0x101300a1);
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR+4, 0x001c001d);
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR+8, 0x05040000);
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR+0xc, 0x1e000000);
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR+0x10, 0x304800f8);


    sal_task_sleep(2);
    drv_goldengate_chip_write(lchip, PLL_CORE_CFG_ADDR, 0x101300a0);
    sal_task_sleep(2);

    /*wait pll lock*/
    cmd = DRV_IOR(PllCoreMon_t, PllCoreMon_monPllCoreLock_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    while (field_val == 0)
    {
        sal_task_sleep(1);
        if ((index++) > 50)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mbist cannot lock\n");
            return CTC_E_HW_FAIL;
        }
        cmd = DRV_IOR(PllCoreMon_t, PllCoreMon_monPllCoreLock_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /*wait mbist ready*/
    sal_task_sleep(2);
    cmd = DRV_IOR(SysMabistReady_t, SysMabistReady_sysMabistReady_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    while (field_val != 0x7FFFF)
    {
        sal_task_sleep(1);
        if ((index++) > 50)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mbist cannot ready\n");
            return CTC_E_HW_FAIL;
        }
        cmd = DRV_IOR(SysMabistReady_t, SysMabistReady_sysMabistReady_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /*set mbist repair*/
    field_val = 0;
    cmd = DRV_IOW(SysMabistRepair_t, SysMabistRepair_sysMabistRepair_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*set mbist go*/
    sal_task_sleep(2);
    field_val = 0x7FFFF;
    cmd = DRV_IOW(SysMabistGo_t, SysMabistGo_sysMabistGo_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*wait mbist done*/
    sal_task_sleep(1000);
    cmd = DRV_IOR(SysMabistDone_t, SysMabistDone_sysMabistDone_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    while (field_val != 0x7FFFF)
    {
        sal_task_sleep(1);
        if ((index++) > 50)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Mbist cannot done\n");
            return CTC_E_HW_FAIL;
        }

        cmd = DRV_IOR(SysMabistDone_t, SysMabistDone_sysMabistDone_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /*wait mbist result*/
    cmd = DRV_IOR(SysMabistFail_t, SysMabistFail_sysMabistFail_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    CTC_ERROR_RETURN(sys_goldengate_chip_set_active(lchip, TRUE));

    if (field_val == 0)
    {
        p_value->status = 0;
    }
    else
    {
        p_value->status = 1;
    }
    return CTC_E_NONE;

}

int32
sys_goldengate_chip_set_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value)
{
    ctc_chip_gpio_params_t* p_gpio_param = NULL;
    ctc_chip_phy_mapping_para_t* p_phy_mapping = NULL;

    CTC_PTR_VALID_CHECK(p_value);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d chip_prop:0x%x \n", lchip, chip_prop);


    switch(chip_prop)
    {
        case CTC_CHIP_PROP_EEE_EN:
            CTC_ERROR_RETURN(_sys_goldengate_chip_set_global_eee_en(lchip, (uint32*)p_value));
            break;
        case CTC_CHIP_PROP_SERDES_PRBS:
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_prbs(lchip, p_value));
            break;
        case CTC_CHIP_PROP_SERDES_FFE:
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_ffe(lchip, p_value));
            break;
        case CTC_CHIP_PEOP_SERDES_POLARITY:
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_polarity(lchip, p_value));
            break;
        case CTC_CHIP_PROP_SERDES_LOOPBACK:
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_loopback(lchip, p_value));
            break;
        case CTC_CHIP_MAC_LED_EN:
            CTC_ERROR_RETURN(sys_goldengate_chip_set_mac_led_en(lchip, *((bool *)p_value)));
            break;
        case CTC_CHIP_PROP_MEM_SCAN:
            CTC_ERROR_RETURN(_sys_goldengate_chip_set_mem_scan_mode(lchip, p_value));
            break;
        case CTC_CHIP_PROP_SERDES_P_FLAG:
        case CTC_CHIP_PROP_SERDES_PEAK:
        case CTC_CHIP_PROP_SERDES_DPC:
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            CTC_ERROR_RETURN(sys_goldengate_datapath_set_serdes_cfg(lchip, chip_prop, p_value));
            break;
        case CTC_CHIP_PROP_GPIO_MODE:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_chip_set_gpio_mode(lchip,
                                    p_gpio_param->gpio_id, p_gpio_param->value));
            break;
        case CTC_CHIP_PROP_GPIO_OUT:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_chip_set_gpio_output(lchip,
                                    p_gpio_param->gpio_id, p_gpio_param->value));
            break;
        case CTC_CHIP_PROP_PHY_MAPPING:
            p_phy_mapping= (ctc_chip_phy_mapping_para_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_chip_set_phy_mapping(lchip, p_phy_mapping));
            break;
        case CTC_CHIP_PROP_MEM_BIST:
            CTC_ERROR_RETURN(_sys_goldengate_chip_set_mem_bist(lchip, (ctc_chip_mem_bist_t*)p_value));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_chip_get_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value)
{
    ctc_chip_serdes_ffe_t* p_ffe = NULL;
    ctc_port_serdes_info_t* p_serdes_port = NULL;
    ctc_chip_gpio_params_t* p_gpio_param = NULL;
    ctc_chip_serdes_info_t serdes_info;
    uint16 coefficient[SYS_GG_CHIP_FFE_PARAM_NUM] = {0};
    uint8 index = 0;
    uint8 value = 0;
    uint16 gport = 0;

    CTC_PTR_VALID_CHECK(p_value);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_FFE:
            p_ffe = (ctc_chip_serdes_ffe_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_ffe(lchip, p_ffe->serdes_id, coefficient, p_ffe->mode, &value));
            for (index = 0; index < SYS_GG_CHIP_FFE_PARAM_NUM; index++)
            {
                p_ffe->coefficient[index] = coefficient[index];
            }
            p_ffe->status = value;
            break;
        case CTC_CHIP_PEOP_SERDES_POLARITY:
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_polarity(lchip, p_value));
            break;
        case CTC_CHIP_PROP_DEVICE_INFO:
            CTC_ERROR_RETURN(sys_goldengate_chip_get_device_info(lchip, (ctc_chip_device_info_t*)p_value));
            break;
        case CTC_CHIP_PROP_SERDES_LOOPBACK:
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_loopback(lchip, p_value));
            break;
        case CTC_CHIP_MAC_LED_EN:
            break;
        case CTC_CHIP_PROP_MEM_SCAN:
            CTC_ERROR_RETURN(_sys_goldengate_chip_get_mem_scan_mode(lchip, p_value));
            break;
        case CTC_CHIP_PROP_SERDES_P_FLAG:
        case CTC_CHIP_PROP_SERDES_PEAK:
        case CTC_CHIP_PROP_SERDES_DPC:
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_cfg(lchip, chip_prop, p_value));
            break;
        case CTC_CHIP_PROP_GPIO_IN:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_chip_get_gpio_input(lchip,
                                    p_gpio_param->gpio_id, &(p_gpio_param->value)));
            break;
        case CTC_CHIP_PROP_SERDES_ID_TO_GPORT:
            p_serdes_port = (ctc_port_serdes_info_t*)p_value;
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_gport_with_serdes(lchip, p_serdes_port->serdes_id, &gport));
            p_serdes_port->gport = gport;
            sal_memset(&serdes_info, 0, sizeof(serdes_info));
            serdes_info.serdes_id = p_serdes_port->serdes_id;
            CTC_ERROR_RETURN(sys_goldengate_datapath_get_serdes_mode(lchip, &serdes_info));
            p_serdes_port->overclocking_speed = serdes_info.overclocking_speed;
            p_serdes_port->serdes_mode = serdes_info.serdes_mode;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_get_chip_sensor(uint8 lchip, ctc_chip_sensor_type_t type, uint32* p_value)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 timeout = 20;

    CTC_PTR_VALID_CHECK(p_value);

    if ((CTC_CHIP_SENSOR_TEMP != type) && (CTC_CHIP_SENSOR_VOLTAGE != type))
    {
        return CTC_E_INVALID_PARAM;
    }

    field_val =  0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgSleepSen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* set sensor type*/
    field_val =  (CTC_CHIP_SENSOR_TEMP == type)?0:1;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgSenSv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* reset sensor */
    field_val =  1;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgResetSen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    sal_task_sleep(10);
    field_val =  0;
    cmd = DRV_IOW(TvSensorCfg_t, TvSensorCfg_cfgResetSen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    sal_task_sleep(3);

    cmd = DRV_IOR(TvSensorMon_t, TvSensorMon_monOutputValidSen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    while ((field_val == 0) && (timeout--))
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        sal_task_sleep(1);
    }

    if (field_val)
    {
        cmd = DRV_IOR(TvSensorMon_t, TvSensorMon_monOutputSen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Sensor is not Ready!! \n");
        return CTC_E_INVALID_EXP_VALUE;
    }

    if (CTC_CHIP_SENSOR_TEMP == type)
    {
        if ((field_val & 0xff) >= 118)
        {
            *p_value = (field_val & 0xff) - 118;
        }
        else
        {
            *p_value = 118 - (field_val & 0xff) + (1 << 31);
        }
    }
    else
    {
        *p_value =  (field_val*38 +5136)/10;
    }

    return CTC_E_NONE;
}

bool
sys_goldengate_chip_check_feature_support(uint8 feature)
{

    if (gg_feature_support_arr[feature])
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

int32 sys_goldengate_chip_read_pcie_phy_register(uint8 lchip, uint32 addr, uint32* p_value)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    /*write HssRegCtl.cfgHssRegAddr as 0x2000*/
    field_val = addr;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegAddr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*write HssRegCtl.cfgHssRegWrite as 0 to indicate read operation*/
    field_val = 0;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegWrite_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*write HssRegCtl.cfgHssRegReq as 1 to trigger read*/
    field_val = 1;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegReq_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*read HssRegCtl.hssRegReadData to get the read data from Receiver*/
    cmd = DRV_IOR(HssRegCtl_t, HssRegCtl_hssRegReadData_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_value = field_val;

    return CTC_E_NONE;
}

int32 sys_goldengate_chip_write_pcie_phy_register(uint8 lchip, uint32 addr, uint32 value)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    /*write HssRegCtl.cfgHssRegAddr as 0x1007*/
    field_val = addr;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegAddr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*read HssRegCtl.cfgHssRegWrData to set the write data to Receiver*/
    field_val = value;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegWrData_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*write HssRegCtl.cfgHssRegWrite as 1 to indicate write operation*/
    field_val = 1;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegWrite_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*write HssRegCtl.cfgHssRegReq as 1 to trigger write*/
    field_val = 1;
    cmd = DRV_IOW(HssRegCtl_t, HssRegCtl_cfgHssRegReq_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}
int32
sys_goldengate_chip_get_pcie_select(uint8 lchip, uint8* pcie_sel)
{
    uint32 dev_id = 0;
    uint8 pcie_id = 0;
    uint8 chip_num = 0;
    uint8 pcie0_exist = 0;
    uint8 pcie1_exist = 0;
    uint8 index = 0;

    *pcie_sel = 0;
    dal_get_chip_number(&chip_num);
    for (index = 0; index < chip_num; index++)
    {
        dal_get_chip_dev_id(index, &dev_id);
        if ((DAL_GOLDENGATE_DEVICE_ID == dev_id) && (!pcie0_exist))
        {
            pcie0_exist = 1;
        }
        if ((DAL_GOLDENGATE_DEVICE_ID1 == dev_id) && (!pcie1_exist))
        {
            pcie1_exist = 1;
        }
    }

    if (pcie0_exist)
    {
        pcie_id = 0;
    }
    else if (pcie1_exist)
    {
        pcie_id = 1;
    }
    else
    {
        return CTC_E_INVALID_CHIP_ID;
    }
    *pcie_sel = pcie_id;

    return CTC_E_NONE;
}

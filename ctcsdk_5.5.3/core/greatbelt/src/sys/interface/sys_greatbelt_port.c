/**
 @file sys_greatbelt_port.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-22

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_interrupt.h"
#include "ctc_greatbelt_interrupt.h"

#include "sys_greatbelt_register.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_vlan_mapping.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_drop.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_l2_fdb.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_data_path.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_MAX_FRAME_VALUE (16 * 1024)
#define SYS_MAX_PREAMBLE_FOR_GMAC 15
#define SYS_MIN_PREAMBLE_FOR_GMAC 2
#define SYS_MAX_PREAMBLE_FOR_SGMAC 8
#define SYS_MIN_PREAMBLE_FOR_SGMAC 4
#define SYS_MIN_FRAME_SIZE_MIN_LENGTH_FOR_GMAC 18
#define SYS_MIN_FRAME_SIZE_MAX_LENGTH_FOR_GMAC 64
#define SYS_SGMAC_RESET_INT_STEP   (ResetIntRelated_ResetSgmac1_f - ResetIntRelated_ResetSgmac0_f)

#define SYS_SGMAC_EN_PCS_CLK_STEP  (ModuleGatedClkCtl_EnClkSupSgmac1Pcs_f - ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f)

#define SYS_SGMAC_XGMAC_PCS_CLK_STEP  (ModuleGatedClkCtl_EnClkSupSgmac5XgmacPcs_f - ModuleGatedClkCtl_EnClkSupSgmac4XgmacPcs_f)

#define SYS_GMAC_RESET_INT_STEP  (ResetIntRelated_ResetQuadMac1Reg_f - ResetIntRelated_ResetQuadMac0Reg_f)

#define GMAC_FLOW_CTL_REG_ID_INTERVAL 20
#define SGMAC_FLOW_CTL_REG_ID_INTERVAL 42
#define XGMAC_FLOW_CTL_REG_ID_INTERVAL 36

#define SYS_MAX_RANDOM_LOG_THRESHOD 0x7FFF
#define SYS_MAX_ISOLUTION_ID_NUM 31
#define SYS_MAX_PVLAN_COMMUNITY_ID_NUM 15

#define VLAN_RANGE_ENTRY_NUM 64

#define FLOW_CONTROL_RX_EN_BIT 11
#define FLOW_CONTROL_PAUSE_EN_BIT 10

#define SYS_VLAN_RANGE_INFO_CHECK(vrange_info) \
    { \
        CTC_BOTH_DIRECTION_CHECK(vrange_info->direction); \
        if (VLAN_RANGE_ENTRY_NUM <= vrange_info->vrange_grpid) \
        {    return CTC_E_INVALID_PARAM; } \
    }

#define PORT_DB_SET(old, cur) \
    do \
    { \
        if (cur == old) \
        { \
            PORT_UNLOCK; \
            return CTC_E_NONE; \
        } \
        else \
        { \
            old = cur; \
        } \
    } while (0)

#define PORT_DB_GET(cur, old) \
    do \
    { \
        *cur = old; \
    } while (0)

#define SYS_PORT_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(port, port, PORT_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_PORT_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);       \
        if (NULL == p_gb_port_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)


#define SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, is_sgmac, mac_id, pcs_mode) \
    do { \
        drv_datapath_port_capability_t port_cap; \
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t)); \
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap); \
        if (port_cap.valid) \
        {\
            if (is_sgmac) \
            { \
                mac_id = (0xFF != port_cap.mac_id)? ((port_cap.mac_id) - SYS_MAX_GMAC_PORT_NUM):(port_cap.mac_id); \
            } \
            else \
            { \
                mac_id = (port_cap.mac_id); \
            } \
            pcs_mode = port_cap.pcs_mode; \
        }\
        else \
        { \
            mac_id = 0xFF; \
        }\
    } while (0)

/**
 @brief define tcam lookup type for scl
*/
enum sys_port_scl_tcam_type_e
{
    SYS_PORT_TCAM_SCL_MAC,
    SYS_PORT_TCAM_SCL_IP,
    SYS_PORT_TCAM_SCL_VLAN,
    SYS_PORT_TCAM_SCL_IP_TUNNEL,
    SYS_PORT_TCAM_SCL_PBB,
    SYS_PORT_TCAM_SCL_WLAN,
    SYS_PORT_TCAM_SCL_TRILL,
    SYS_PORT_TCAM_SCL_MAX
};
typedef enum sys_port_scl_tcam_type_e sys_port_scl_tcam_type_t;

/**
 @brief define hash lookup type for scl
*/
enum sys_port_scl_hash_type_e
{
    SYS_PORT_HASH_SCL_DISABLE         = 0x0,    /**< Disable */
    SYS_PORT_HASH_SCL_TWO_VLAN        = 0x1,    /**< Port+svid+cvid */
    SYS_PORT_HASH_SCL_SVLAN           = 0x2,    /**< Port+svid */
    SYS_PORT_HASH_SCL_CVLAN           = 0x3,    /**< Port+cvid */
    SYS_PORT_HASH_SCL_SVLAN_COS       = 0x4,    /**< Port+svid+scos */
    SYS_PORT_HASH_SCL_CVLAN_COS       = 0x5,    /**< Port+cvid+ccos */
    SYS_PORT_HASH_SCL_MAC_SA          = 0x6,    /**< MACSA */
    SYS_PORT_HASH_SCL_PORT_MAC_SA     = 0x7,    /**< Port+MACSA 160bit */
    SYS_PORT_HASH_SCL_IP              = 0x8,    /**< Ip4SA /Ipv6*/
    SYS_PORT_HASH_SCL_PORT_IPV4_SA    = 0x9,    /**< Port+IPv4SA */
    SYS_PORT_HASH_SCL_PORT            = 0xA,    /**< Port */
    SYS_PORT_HASH_SCL_L2              = 0xB,    /**< L2 160bit */
    SYS_PORT_HASH_SCL_RSV             = 0xC,    /**< Ipv6SA 160bit */
    SYS_PORT_HASH_SCL_TUNNEL          = 0xD,    /**< Tunnel */
    SYS_PORT_HASH_SCL_TRILL           = 0xE,    /**< Trill */
    SYS_PORT_HASH_SCL_MAX
};
typedef enum sys_port_scl_hash_type_e sys_port_scl_hash_type_t;

/**
 @brief define hash lookup type for egress scl
*/
enum sys_port_egress_scl_hash_type_e
{
    SYS_PORT_HASH_EGRESS_SCL_DISABLE         = 0x0,    /**< Disable */
    SYS_PORT_HASH_EGRESS_SCL_TWO_VLAN        = 0x1,    /**< Port+svid+cvid */
    SYS_PORT_HASH_EGRESS_SCL_SVLAN           = 0x2,    /**< Port+svid */
    SYS_PORT_HASH_EGRESS_SCL_CVLAN           = 0x3,    /**< Port+cvid */
    SYS_PORT_HASH_EGRESS_SCL_SVLAN_COS       = 0x4,    /**< Port+svid+scos */
    SYS_PORT_HASH_EGRESS_SCL_CVLAN_COS       = 0x5,    /**< Port+cvid+ccos */

    SYS_PORT_HASH_EGRESS_SCL_PORT_VLAN_CROSS = 0x8,    /**< Port vlan cross */
    SYS_PORT_HASH_EGRESS_SCL_PORT_CROSS      = 0x9,    /**< Port cross */
    SYS_PORT_HASH_EGRESS_SCL_PORT            = 0xA,    /**< Port */

    SYS_PORT_HASH_EGRESS_SCL_PBB             = 0xD,    /**< Pbb */
    SYS_PORT_HASH_EGRESS_SCL_MAX
};
typedef enum sys_port_egress_scl_hash_type_e sys_port_egress_scl_hash_type_t;

enum sys_port_egress_restriction_type_e
{
    SYS_PORT_RESTRICTION_NONE,                    /**< restriction disable */
    SYS_PORT_RESTRICTION_PVLAN,                   /**< private vlan enable */
    SYS_PORT_RESTRICTION_BLOCKING,                /**< port blocking enable */
    SYS_PORT_RESTRICTION_ISOLATION               /**< port isolation enable */
};
typedef enum sys_port_egress_restriction_type_e sys_port_egress_restriction_type_t;


typedef struct
{
    uint32 hash_type;
    uint32 tcam_type;
    uint8  vlan_high_priority;
    uint8  use_label;
    uint8  label;
} sys_greatbelt_port_scl_map_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
sys_port_master_t* p_gb_port_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_chip_master_t* p_gb_chip_master;
extern sys_intr_sub_reg_t g_intr_sub_reg_sup_normal[SYS_INTR_GB_SUB_NORMAL_MAX];

static sys_greatbelt_port_scl_map_t g_sys_port_scl_map[CTC_SCL_KEY_TYPE_MAX] =
{
    {SYS_PORT_HASH_SCL_DISABLE,      SYS_PORT_TCAM_SCL_MAX,       FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_PORT,         SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_CVLAN,        SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_SVLAN,        SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_CVLAN_COS,    SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_SVLAN_COS,    SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_TWO_VLAN,     SYS_PORT_TCAM_SCL_VLAN,      FALSE, FALSE, SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE},
    {SYS_PORT_HASH_SCL_MAC_SA,       SYS_PORT_TCAM_SCL_MAC,       TRUE,  TRUE,  SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS},
    {SYS_PORT_HASH_SCL_MAC_SA,       SYS_PORT_TCAM_SCL_MAC,       TRUE,  TRUE,  SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS},
    {SYS_PORT_HASH_SCL_IP,           SYS_PORT_TCAM_SCL_IP,        TRUE,  TRUE,  SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS},
    {SYS_PORT_HASH_SCL_IP,           SYS_PORT_TCAM_SCL_IP,        TRUE,  TRUE,  SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS},
    {SYS_PORT_HASH_SCL_PORT_MAC_SA,  SYS_PORT_TCAM_SCL_MAC,       TRUE,  FALSE, SYS_SCL_LABEL_RESERVE_FOR_IPSG},
    {SYS_PORT_HASH_SCL_PORT_IPV4_SA, SYS_PORT_TCAM_SCL_IP,        TRUE,  FALSE, SYS_SCL_LABEL_RESERVE_FOR_IPSG},
    {SYS_PORT_HASH_SCL_IP,           SYS_PORT_TCAM_SCL_IP,        TRUE,  FALSE, SYS_SCL_LABEL_RESERVE_FOR_IPSG},
    {SYS_PORT_HASH_SCL_TUNNEL,       SYS_PORT_TCAM_SCL_IP_TUNNEL, FALSE, FALSE, 0},
    {SYS_PORT_HASH_SCL_TUNNEL,       SYS_PORT_TCAM_SCL_IP_TUNNEL, FALSE, FALSE, 0},
    {SYS_PORT_HASH_SCL_TUNNEL,       SYS_PORT_TCAM_SCL_IP_TUNNEL, FALSE, FALSE, 0},
    {SYS_PORT_HASH_SCL_TUNNEL,       SYS_PORT_TCAM_SCL_IP_TUNNEL, FALSE, FALSE, 0}
};

STATIC int32
_sys_greatbelt_port_set_min_frame_size(uint8 lchip, uint16 gport, uint8 size);
STATIC int32
_sys_greatbelt_port_init_nexthop(uint8 lchip);
int32
_sys_greatbelt_port_release_sgmac(uint8 lchip, uint8 lport, uint8 flush_dis);
int32
_sys_greatbelt_port_reset_sgmac(uint8 lchip, uint8 lport);
int32
_sys_greatbelt_port_get_unidir_en(uint8 lchip, uint16 gport, uint32* p_value);

uint8 g_cur_log_index[CTC_MAX_LOCAL_CHIP_NUM] = {0};

int32
sys_greatbelt_port_get_monitor_rlt(uint8 lchip)
{
    uint16 index = 0;

    sys_greatbelt_scan_log_t *p_scan_log;

    SYS_PORT_INIT_CHECK(lchip);

    p_scan_log = p_gb_port_master[lchip]->scan_log;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"%-5s %-8s %-50s\n", "Type", "Gport", "System Time");
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"------------------------------------------\n");

    for (index = 0; index < SYS_PORT_MAX_LOG_NUM; index++)
    {
        if (p_scan_log[index].valid)
        {
            SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-5d %-8d %-50s\n",p_scan_log[index].type, p_scan_log[index].gport, p_scan_log[index].time_str);
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_code_err(uint8 lchip, uint16 gport, uint32* code_err)
{
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint8 tbl_step1 = 0;
    uint8 tbl_step2 = 0;
    uint8 mac_id = 0;
    uint16 lport = 0;
    uint32 unidir_en = 0;
    qsgmii_pcs_code_err_cnt_t qsgmii_errcnt;
    quad_pcs_pcs0_err_cnt_t quad_errcnt;
    sgmac_pcs_err_cnt_t sgmac_errcnt;
    sgmac_xfi_debug_stats_t xfi_errcnt;
    drv_datapath_port_capability_t port_cap;

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, lport, &port_cap));
    mac_id = port_cap.mac_id;

    _sys_greatbelt_port_get_unidir_en(lchip, gport, &unidir_en);
    if (unidir_en)
    {
        *code_err = 0;
        return CTC_E_NONE;
    }

    switch(port_cap.pcs_mode)
    {
        case DRV_SERDES_SGMII_MODE:

            if (mac_id >= 24)
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }

            tbl_step1 = QuadPcsPcs1ErrCnt0_t - QuadPcsPcs0ErrCnt0_t;
            tbl_step2 = QuadPcsPcs0ErrCnt1_t - QuadPcsPcs0ErrCnt0_t;
            tb_id = QuadPcsPcs0ErrCnt0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            (DRV_IOCTL(lchip, 0, cmd, &quad_errcnt));
            (DRV_IOCTL(lchip, 0, cmd, &quad_errcnt));
            *code_err = quad_errcnt.pcs0_code_err_cnt;

            break;

        case DRV_SERDES_QSGMII_MODE:
            tb_id = QsgmiiPcsCodeErrCnt0_t + (mac_id/4);
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            (DRV_IOCTL(lchip, 0, cmd, &qsgmii_errcnt));
            (DRV_IOCTL(lchip, 0, cmd, &qsgmii_errcnt));
            *code_err = qsgmii_errcnt.code_err_cnt;
            break;

        case DRV_SERDES_XGSGMII_MODE:
            tb_id = SgmacPcsErrCnt0_t + (mac_id - 48);
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
            (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
            *code_err = sgmac_errcnt.code_err_cnt0;
            break;

        case DRV_SERDES_XFI_MODE:
            tb_id = SgmacXfiDebugStats0_t + (mac_id - 48);
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            (DRV_IOCTL(lchip, 0, cmd, &xfi_errcnt));
            (DRV_IOCTL(lchip, 0, cmd, &xfi_errcnt));
            *code_err = xfi_errcnt.xfi_err_block_cnt;
            break;

        case DRV_SERDES_XAUI_MODE:
            tb_id = SgmacPcsErrCnt0_t + (mac_id - 48);
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
            (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
            *code_err = sgmac_errcnt.code_err_cnt0 + sgmac_errcnt.code_err_cnt1 +
                sgmac_errcnt.code_err_cnt2 + sgmac_errcnt.code_err_cnt3;
            break;

        case DRV_SERDES_2DOT5_MODE:
            if (mac_id < 48)
            {
                if (mac_id >= 24)
                {
                    return CTC_E_EXCEED_MAX_SIZE;
                }

                tbl_step1 = QuadPcsPcs1ErrCnt0_t - QuadPcsPcs0ErrCnt0_t;
                tbl_step2 = QuadPcsPcs0ErrCnt1_t - QuadPcsPcs0ErrCnt0_t;
                tb_id = QuadPcsPcs0ErrCnt0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
                cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                (DRV_IOCTL(lchip, 0, cmd, &quad_errcnt));
                (DRV_IOCTL(lchip, 0, cmd, &quad_errcnt));
                *code_err = quad_errcnt.pcs0_code_err_cnt;
            }
            else
            {
                tb_id = SgmacPcsErrCnt0_t + (mac_id - 48);
                cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
                (DRV_IOCTL(lchip, 0, cmd, &sgmac_errcnt));
                *code_err = sgmac_errcnt.code_err_cnt0;
            }
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value)
{
    drv_datapath_port_capability_t* p_port_attr=NULL;
    uint16 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_ERROR_RETURN(drv_greatbelt_get_port_attr(lchip, lport, &p_port_attr));

    if (!p_port_attr->valid)
    {
        return CTC_E_NOT_READY;
    }

    *p_value = p_port_attr->code_err_count;

    return CTC_E_NONE;
}

void
_sys_greatbelt_port_monitor_thread(void* para)
{
    uint8 lport = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 mac_en = 0;
    drv_datapath_port_capability_t port_cap;
    drv_datapath_port_capability_t* p_port_attr = NULL;
    uint32 tb_id = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;
    uint32 errcnt = 0;
    sgmac_pcs_status_t sgmac_status;
    uint8 index = 0;
    sys_greatbelt_scan_log_t* p_log = NULL;
    char* p_str = NULL;
    sal_time_t tv;
    uint8 lchip = (uintptr)para;
    sgmac_soft_rst_t sgmac_rst;
    uint8 type = 0;
    uint32 is_up = 0;

    while(1)
    {
            SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);
            sys_greatbelt_get_gchip_id(lchip, &gchip);
            for (lport = 0; lport < 60; lport++)
            {
                type = 0;
                is_up = 0;

                if (p_gb_port_master[lchip]->polling_status == 0)
                {
                    continue;
                }
                sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
                port_cap.pcs_mode = DRV_SERDES_NONE_MODE;
                drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
                if (!port_cap.valid || (DRV_SERDES_NONE_MODE == port_cap.pcs_mode))
                {
                    continue;
                }
                drv_greatbelt_get_port_attr(lchip, lport, &p_port_attr);

                gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
                sys_greatbelt_port_get_mac_en(lchip, gport, &mac_en);
                if (!mac_en)
                {
                    continue;
                }

                if (port_cap.pcs_mode == DRV_SERDES_INTLK_MODE)
                {
                    continue;
                }

                sys_greatbelt_port_get_mac_link_up(lchip, gport, &is_up, 0);
                if (port_cap.pcs_mode == DRV_SERDES_XAUI_MODE)
                {
                    sys_greatbelt_port_get_code_err(lchip, gport, &errcnt);
                    if (errcnt)
                    {
                        type |= (1<<0);
                        p_port_attr->code_err_count++;
                        for (index = 0; index < 4; index++)
                        {
                            sys_greatbelt_chip_reset_rx_serdes(lchip, (port_cap.serdes_id+index), 1);
                        }

                        for (index = 0; index < 4; index++)
                        {
                            sys_greatbelt_chip_reset_rx_serdes(lchip, (port_cap.serdes_id+index), 0);
                        }
                    }
                    else
                    {
                        mac_id = port_cap.mac_id - 48;
                        tb_id = SgmacPcsStatus0_t + mac_id;
                        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                        (DRV_IOCTL(lchip, 0, cmd, &sgmac_status));

                        if (sgmac_status.pcs_sync_status0 && sgmac_status.pcs_sync_status1
                        && sgmac_status.pcs_sync_status2 && sgmac_status.pcs_sync_status3
                        && !sgmac_status.xgmac_pcs_align_status)
                        {
                            type |= (1<<0);
                            p_port_attr->code_err_count++;
                            tb_id = SgmacSoftRst0_t + mac_id;
                            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                            (DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
                            sgmac_rst.serdes_rx0_soft_rst = 1;
                            sgmac_rst.serdes_rx1_soft_rst = 1;
                            sgmac_rst.serdes_rx2_soft_rst = 1;
                            sgmac_rst.serdes_rx3_soft_rst = 1;
                            sgmac_rst.pcs_rx_soft_rst = 1;
                            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                            (DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));

                            tb_id = SgmacSoftRst0_t + mac_id;
                            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                            (DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
                            sgmac_rst.serdes_rx0_soft_rst = 0;
                            sgmac_rst.serdes_rx1_soft_rst = 0;
                            sgmac_rst.serdes_rx2_soft_rst = 0;
                            sgmac_rst.serdes_rx3_soft_rst = 0;
                            sgmac_rst.pcs_rx_soft_rst = 0;
                            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                            (DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
                        }
                    }

                    if (!is_up)
                    {
                        type |= (1<<1);
                        for (index = 0; index < 4; index++)
                        {
                            sys_greatbelt_chip_reset_dfe(lchip, (port_cap.serdes_id+index), 1);
                        }

                        for (index = 0; index < 4; index++)
                        {
                            sys_greatbelt_chip_reset_dfe(lchip, (port_cap.serdes_id+index), 0);
                        }
                    }

                    if (type)
                    {
                        if (g_cur_log_index[lchip] >= SYS_PORT_MAX_LOG_NUM)
                        {
                            g_cur_log_index[lchip] = 0;
                        }

                        p_log = &(p_gb_port_master[lchip]->scan_log[g_cur_log_index[lchip]]);
                        p_log->type = type;
                        p_log->valid = 1;
                        p_log->gport = gport;
                        sal_time(&tv);
                        p_str = sal_ctime(&tv);
                        sal_strncpy(p_log->time_str, p_str, 50);
                        g_cur_log_index[lchip]++;
                    }
                }
                else
                {
                    /* check codeerror */
                    sys_greatbelt_port_get_code_err(lchip, gport, &errcnt);

                    if (errcnt)
                    {
                        type |= (1<<0);
                        p_port_attr->code_err_count++;
                    }

                    if (!is_up)
                    {
                        if (port_cap.pcs_mode != DRV_SERDES_QSGMII_MODE)
                        {
                            type |= (1<<1);
                        }
                    }

                    if (type)
                    {
                        sys_greatbelt_chip_reset_dfe(lchip, port_cap.serdes_id, 1);
                        sys_greatbelt_chip_reset_dfe(lchip, port_cap.serdes_id, 0);
                        sys_greatbelt_chip_reset_rx_serdes(lchip, port_cap.serdes_id, 1);
                        sys_greatbelt_chip_reset_rx_serdes(lchip, port_cap.serdes_id, 0);
                        if (g_cur_log_index[lchip] >= SYS_PORT_MAX_LOG_NUM)
                        {
                            g_cur_log_index[lchip] = 0;
                        }

                        p_log = &(p_gb_port_master[lchip]->scan_log[g_cur_log_index[lchip]]);
                        p_log->type = type;
                        p_log->valid = 1;
                        p_log->gport = gport;
                        sal_time(&tv);
                        p_str = sal_ctime(&tv);
                        sal_strncpy(p_log->time_str, p_str, 50);

                        g_cur_log_index[lchip]++;
                    }
                }
            }


        sal_task_sleep(1000);
    }

    return;
}

int32
sys_greatbelt_port_set_monitor_link_enable(uint8 lchip, uint32 enable)
{
    enable = enable?1:0;

    p_gb_port_master[lchip]->polling_status = enable;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_monitor_link_enable(uint8 lchip, uint32* p_value)
{
    *p_value = p_gb_port_master[lchip]->polling_status;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_monitor_link(uint8 lchip)
{
    drv_work_platform_type_t platform_type;
    char buffer[SAL_TASK_MAX_NAME_LEN]={0};
    int32 ret = 0;

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    sal_memset(p_gb_port_master[lchip]->scan_log, 0, sizeof(sys_greatbelt_scan_log_t)*SYS_PORT_MAX_LOG_NUM);

    sal_sprintf(buffer, "ctclnkMon-%d", lchip);
    ret = sal_task_create(&(p_gb_port_master[lchip]->p_monitor_scan), buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_greatbelt_port_monitor_thread, NULL);
    if (ret < 0)
    {
        return CTC_E_NOT_INIT;
    }

    p_gb_port_master[lchip]->polling_status = 1;

    return CTC_E_NONE;
}


int32
sys_greatbelt_port_pfc_init(uint8 lchip)
{
    uint8 mac_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 core_freq = 0;
    uint8 cal_entry = 0;
    net_tx_cal_ctl_t net_tx_cal_ctl;
    net_rx_pause_ctl_t net_rx_pause_ctl;
    net_rx_pause_timer_mem_t  net_rx_pause_timer_mem;
    drv_datapath_port_capability_t port_cap;
    uint8 lport = 0;
    uint8 cal_num_sg = 0;
    uint64 tmp = 0;

    sal_memset(&net_tx_cal_ctl, 0, sizeof(net_tx_cal_ctl_t));
    sal_memset(&net_rx_pause_ctl, 0, sizeof(net_rx_pause_ctl));

    for (mac_id = 0; mac_id < SYS_MAX_GMAC_PORT_NUM; mac_id++)
    {
        /*gmac use pfc quanta time cfg 0*/
        field_val = 1;
        cmd = DRV_IOW(NetTxPortModeTab_t, NetTxPortModeTab_Data_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &field_val));
    }

    for (mac_id = SYS_MAX_GMAC_PORT_NUM; mac_id < (SYS_MAX_GMAC_PORT_NUM + SYS_MAX_SGMAC_PORT_NUM); mac_id++)
    {
        /*sgmac use pfc quanta time cfg 0*/
        field_val = 0;
        cmd = DRV_IOW(NetTxPortModeTab_t, NetTxPortModeTab_Data_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &field_val));
    }

    /*pfc quanta time cfg */
    cmd = DRV_IOR(NetTxCalCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_cal_ctl));
    if (!net_tx_cal_ctl.cal_entry_sel)
    {
        cal_entry = net_tx_cal_ctl.walker_end0;
    }
    else
    {
        cal_entry = net_tx_cal_ctl.walker_end1;
    }
    core_freq = sys_greatbelt_get_core_freq(lchip);

    cmd = DRV_IOW(NetRxPauseCtl_t, DRV_ENTRY_FLAG);

    net_rx_pause_ctl.upd_interval       = (512*core_freq+999) /1000 - 1;
    if(0 == (((net_rx_pause_ctl.upd_interval+1)*1000*10000/core_freq) -512*10000))
    {
        net_rx_pause_ctl.comps_timer_thrd = 0;
    }
    else
    {
        net_rx_pause_ctl.comps_timer_thrd   = (512*10000)/(((net_rx_pause_ctl.upd_interval+1)*1000*10000/core_freq) -512*10000);
    }
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_ctl));

    for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
    {
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if ((!port_cap.valid))
        {
            continue;
        }

        cmd = DRV_IOR(NetRxPauseTimerMem_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd, &net_rx_pause_timer_mem));
        if(0 == net_rx_pause_ctl.comps_timer_thrd)
        {
            net_rx_pause_timer_mem.need_comp = 0;
        }
        else
        {
            net_rx_pause_timer_mem.need_comp = 1;
        }
        cmd = DRV_IOW(NetRxPauseTimerMem_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd, &net_rx_pause_timer_mem));
    }

    /*pfc quanta time cfg */
    if (cal_entry)
    {
        /*0, 10G; 1, 1G; 2, 100M; 3, 10G->1G*/
        sys_greatbelt_chip_get_net_tx_cal_10g_entry(lchip, &cal_num_sg);
        /*10G*/
        tmp = core_freq;
        tmp = ((65535*512*tmp) / ((cal_entry+1)*1000)) + (512*4*core_freq)/(1000*(cal_entry+1)) + 1;
        field_val = (uint32)tmp;
        field_val = (field_val * 10/ cal_num_sg) + 1;
        cmd = DRV_IOW(NetTxPauseQuantaCfg_t, NetTxPauseQuantaCfg_PauseQuanta0_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*1G*/
        tmp = core_freq;
        tmp = ((65535*512*tmp) / ((cal_entry+1)*1000)) + (512*4*core_freq)/(1000*(cal_entry+1)) + 1;
        field_val = (uint32)tmp;
        cmd = DRV_IOW(NetTxPauseQuantaCfg_t, NetTxPauseQuantaCfg_PauseQuanta1_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*100M*/
        field_val *= 10;
        cmd = DRV_IOW(NetTxPauseQuantaCfg_t, NetTxPauseQuantaCfg_PauseQuanta2_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        /*10G->1G mode*/
        tmp = core_freq;
        tmp = ((65535*512*tmp) / ((cal_entry+1)*1000)) + (512*4*core_freq)/(1000*(cal_entry+1)) + 1;
        field_val = (uint32)tmp;
        field_val = (field_val / cal_num_sg) + 1;
        cmd = DRV_IOW(NetTxPauseQuantaCfg_t, NetTxPauseQuantaCfg_PauseQuanta3_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;

}


int32
sys_port_isr_link_status_change_isr(uint8 lchip, uint8 gchip, void* p_data)
{
    ctc_port_link_status_t* p_status = NULL;
    uint8 index = 0;
    uint32 intr_status = 0;
    uint16 gport = 0;
    uint32 is_link = 0;
    uint8 lport = 0;
    uint8 is_sgmac = 0;
    uint16 cnt = 0;
    uint16 link_cnt = 0;
    uint32 pre_link = 0;
    uint8 mac_id = 0;
    uint8 is_find = 0;

    CTC_PTR_VALID_CHECK(p_data);
    p_status = (ctc_port_link_status_t*)p_data;

    /* 1g port link status change */
    for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        mac_id = SYS_GET_MAC_ID(lchip, gport);

        if (mac_id < 32)
        {
            intr_status = p_status->status_1g[0];
            index = mac_id;
        }
        else if (mac_id < 48)
        {
            intr_status = p_status->status_1g[1];
            index = mac_id - 32;
        }
        else
        {
            intr_status = p_status->status_xg[0];
            index = mac_id - SYS_MAX_GMAC_PORT_NUM;
            is_sgmac = 1;
        }

        if ((intr_status >> index) & 0x01)
        {
            p_status->gport = gport;
            is_find = 1;
            /* for after sgmac link interrupt occur, sgmac pcs need have some time to sync link status */
            if (is_sgmac)
            {
                for (cnt = 0; cnt < 0x1fff; cnt++)
                {
                    sys_greatbelt_port_get_mac_link_up(lchip, gport, &is_link, 0);
                    if (pre_link == is_link)
                    {
                        link_cnt++;
                    }
                    else
                    {
                        link_cnt = 0;
                    }

                    pre_link = is_link;

                    /* link status does not change 300 times consecutivly */
                    if (300 == link_cnt)
                    {
                        break;
                    }
                }
            }
        }
    }

    if (0 == is_find)
    {
        return CTC_E_INVALID_PORT;
    }

     /* how to get speed/duplex call ctc_port_set_mac_en*/
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_init_nexthop(uint8 lchip)
{
    uint8 lchip_tmp = 0;
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 index = 0;
    uint32 ret = CTC_E_NONE;

    lchip_tmp = lchip;
    sys_greatbelt_get_gchip_id(lchip_tmp, &gchip);
    for (index = 0; index < SYS_GB_MAX_PORT_NUM_PER_CHIP; index++)
    {
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, index);
        if ((index & 0xFF) < SYS_GB_MAX_PORT_NUM_PER_CHIP)
        {
           /*init all chip port nexthop*/
           lchip = lchip_tmp;
           CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
            {
                ret = sys_greatbelt_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
                if ((CTC_E_ENTRY_EXIST != ret) && (CTC_E_NONE != ret))
                {
                    return ret;
                }
            }
        }
    }
    return CTC_E_NONE;
}
/**
 @brief initialize the port module
*/
int32
sys_greatbelt_port_init(uint8 lchip, ctc_port_global_cfg_t* p_port_global_cfg)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint32 index = 0;
    uint8 lport = 0;
    uint8 chan_id = 0;
    uint32 cmd_ipe = 0;
    uint32 cmd_epe = 0;
    uint32 phy_cmd = 0;
    uint32 phy_ext_cmd = 0;
    uint32 src_cmd = 0;
    uint32 dest_cmd = 0;
    uint32 channel_cmd = 0;
    uint32 buf_store_misc_ctl_cmd = 0;
    uint32 max_frame_size = 0;



    ds_src_port_t src_port;
    ds_phy_port_t phy_port;
    ds_dest_port_t dest_port;
    ds_phy_port_ext_t phy_port_ext;
    ds_channelize_mode_t ds_channelize_mode;
    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    epe_header_adjust_phy_port_map_t epe_header_adjust_phyport_map;
    drv_datapath_port_capability_t port_cap;
    mdio_link_status_t link_status;
    uint32 cmd = 0;

    if (p_gb_port_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }


    /*alloc&init DB and mutex*/
    p_gb_port_master[lchip] = (sys_port_master_t*)mem_malloc(MEM_PORT_MODULE, sizeof(sys_port_master_t));

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_port_master[lchip], 0, sizeof(sys_port_master_t));
    ret = sal_mutex_create(&(p_gb_port_master[lchip]->p_port_mutex));

    if (ret || !(p_gb_port_master[lchip]->p_port_mutex))
    {
        mem_free(p_gb_port_master[lchip]);

        return CTC_E_FAIL_CREATE_MUTEX;
    }

    p_gb_port_master[lchip]->igs_port_prop = (sys_igs_port_prop_t*)mem_malloc(MEM_PORT_MODULE,sizeof(sys_igs_port_prop_t) * SYS_GB_MAX_PORT_NUM_PER_CHIP);

    if (NULL == p_gb_port_master[lchip]->igs_port_prop)
    {
        sal_mutex_destroy(p_gb_port_master[lchip]->p_port_mutex);
        mem_free(p_gb_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_port_master[lchip]->igs_port_prop, 0, sizeof(sys_igs_port_prop_t) * SYS_GB_MAX_PORT_NUM_PER_CHIP);
    p_gb_port_master[lchip]->egs_port_prop = (sys_egs_port_prop_t*)mem_malloc(MEM_PORT_MODULE, SYS_GB_MAX_PORT_NUM_PER_CHIP * sizeof(sys_egs_port_prop_t));

    if (NULL == p_gb_port_master[lchip]->egs_port_prop)
    {
        mem_free(p_gb_port_master[lchip]->igs_port_prop);
        sal_mutex_destroy(p_gb_port_master[lchip]->p_port_mutex);
        mem_free(p_gb_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_port_master[lchip]->egs_port_prop, 0, sizeof(sys_egs_port_prop_t) * SYS_GB_MAX_PORT_NUM_PER_CHIP);
    p_gb_port_master[lchip]->pp_mtu1 = (uint16*)mem_malloc(MEM_PORT_MODULE, SYS_GB_MAX_PORT_NUM_PER_CHIP * sizeof(uint16));
    if (NULL == p_gb_port_master[lchip]->pp_mtu1)
    {
        mem_free(p_gb_port_master[lchip]->igs_port_prop);
        mem_free(p_gb_port_master[lchip]->egs_port_prop);
        sal_mutex_destroy(p_gb_port_master[lchip]->p_port_mutex);
        mem_free(p_gb_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset( p_gb_port_master[lchip]->pp_mtu1, 0, sizeof(uint16)*SYS_GB_MAX_PORT_NUM_PER_CHIP);
    p_gb_port_master[lchip]->pp_mtu2 = (uint16*)mem_malloc(MEM_PORT_MODULE, SYS_GB_MAX_PORT_NUM_PER_CHIP*sizeof(uint16));
    if (NULL == p_gb_port_master[lchip]->pp_mtu2)
    {
        mem_free(p_gb_port_master[lchip]->igs_port_prop);
        mem_free(p_gb_port_master[lchip]->egs_port_prop);
        mem_free(p_gb_port_master[lchip]->pp_mtu1);
        sal_mutex_destroy(p_gb_port_master[lchip]->p_port_mutex);
        mem_free(p_gb_port_master[lchip]);

        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_port_master[lchip]->pp_mtu2, 0, sizeof(uint16)*SYS_GB_MAX_PORT_NUM_PER_CHIP);
    for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
    {
        p_gb_port_master[lchip]->pp_mtu1[index] = SYS_STATS_MTU1_PKT_DFT_LENGTH;
        p_gb_port_master[lchip]->pp_mtu2[index] = SYS_STATS_MTU2_PKT_DFT_LENGTH;
    }
    p_gb_port_master[lchip]->use_logic_port_check = (p_port_global_cfg->default_logic_port_en)? TRUE : FALSE;

        /*init asic table*/
        sal_memset(&phy_port, 0, sizeof(ds_phy_port_t));
        sal_memset(&phy_port_ext, 0, sizeof(ds_phy_port_ext_t));
        sal_memset(&src_port, 0, sizeof(ds_src_port_t));
        sal_memset(&dest_port, 0, sizeof(ds_dest_port_t));
        sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));
        sal_memset(&epe_header_adjust_phyport_map, 0, sizeof(epe_header_adjust_phy_port_map_t));
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));


        phy_port.outer_vlan_is_cvlan = 0;
        phy_port.packet_type_valid = 0;

        phy_port_ext.src_outer_vlan_is_svlan = 1;
        phy_port_ext.default_vlan_id = 1;
        phy_port_ext.user_id_hash_depend = 1;
        phy_port_ext.use_default_vlan_lookup = 1;
        phy_port_ext.exception2_discard = 0x80000000;

        src_port.receive_disable = 0;
        src_port.bridge_en = 1;
        src_port.route_disable = 0;
        src_port.ingress_filtering_disable = 0;
        src_port.fast_learning_en = 1;
        dest_port.default_vlan_id = 1;
        dest_port.untag_default_svlan = 1;
        dest_port.untag_default_vlan_id  = 1;
        dest_port.bridge_en = 1;
        dest_port.transmit_disable = 0;
        dest_port.dot1_q_en = CTC_DOT1Q_TYPE_BOTH;
        dest_port.stp_check_en = 1;
        dest_port.svlan_tpid_valid = 0;
        dest_port.cvlan_space = 0;
        dest_port.egress_filtering_disable = 0;

        phy_cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
        phy_ext_cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        src_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
        dest_cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);

        sys_greatbelt_get_gchip_id(lchip, &gchip);

        /*DsPhyPort, DsPhyPortExt, DsSrcPort, DsDestPort from 256 cost to 128*/
        for (index = 0; index < SYS_GB_MAX_PORT_NUM_PER_CHIP; index++)
        {
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, index);
            if (index >= SYS_GB_MAX_ACL_PORT_BITMAP_NUM) /* port0~55 use bitmap */
            {
                src_port.ipv6_key_use_label  = 1;
                src_port.ipv4_key_use_label  = 1;
                src_port.mpls_key_use_label  = 1;
                src_port.mac_key_use_label   = 1;

                dest_port.ipv6_key_use_label  = 1;
                dest_port.ipv4_key_use_label  = 1;
                dest_port.mpls_key_use_label  = 1;
                dest_port.mac_key_use_label  = 1;
            }

            if (index >= SYS_GB_MAX_PHY_PORT)
            {
                src_port.learning_disable       = 1;
                src_port.add_default_vlan_disable = 1;
                src_port.stp_disable            = 1;
                dest_port.stp_check_en          = 0;
            }

            if (index < SYS_GB_MAX_PHY_PORT)
            {
                if(sys_greatbelt_get_cut_through_en(lchip))
                {
                    src_port.speed = sys_greatbelt_get_cut_through_speed(lchip, gport);
                }
            }

            src_port.acl_port_num = index & 0x3f;     /*//Max Number is 63//*/
            dest_port.acl_port_num = index & 0x3f;

            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, phy_cmd, &phy_port));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, phy_ext_cmd, &phy_port_ext));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, src_cmd, &src_port));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, dest_cmd, &dest_port));

            sys_greatbelt_port_set_global_port(lchip, index, gport);
            p_gb_port_master[lchip]->igs_port_prop[index].port_mac_type = CTC_PORT_MAC_MAX;
        }

        /* set max frame size */
        for (index = 0; index < DS_CHANNELIZE_MODE_MAX_INDEX; index++)
        {
            sal_memset(&ds_channelize_mode, 0, sizeof(ds_channelize_mode_t));
            channel_cmd = DRV_IOR(DsChannelizeMode_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, channel_cmd, &ds_channelize_mode));

            ds_channelize_mode.channelize_en = 0;
            ds_channelize_mode.bpdu_check_en = 1;
            ds_channelize_mode.channelize_en = 0;
            ds_channelize_mode.min_pkt_len = SYS_MIN_FRAME_SIZE_MAX_LENGTH_FOR_GMAC;
            ds_channelize_mode.max_pkt_len = SYS_GREATBELT_DEFAULT_FRAME_SIZE;
            ds_channelize_mode.vlan_base = 1 << FLOW_CONTROL_RX_EN_BIT;

            channel_cmd = DRV_IOW(DsChannelizeMode_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, channel_cmd, &ds_channelize_mode));
        }

        max_frame_size = SYS_GREATBELT_DEFAULT_FRAME_SIZE;
        buf_store_misc_ctl_cmd = DRV_IOW(BufStoreMiscCtl_t, BufStoreMiscCtl_MaxPktSize_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, buf_store_misc_ctl_cmd, &max_frame_size));


        cmd_ipe = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        cmd_epe = DRV_IOW(EpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
        for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
        {
            drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
            if ((!port_cap.valid))
            {
                continue;
            }

            if (DRV_PORT_TYPE_1G == port_cap.port_type)
            {
                p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type = CTC_PORT_MAC_GMAC;
            }
            else if (DRV_PORT_TYPE_SG == port_cap.port_type)
            {
                p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type = CTC_PORT_MAC_SGMAC;
            }

            gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
            chan_id = port_cap.chan_id;

            ipe_header_adjust_phyport_map.local_phy_port = lport;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd_ipe, &ipe_header_adjust_phyport_map));

            epe_header_adjust_phyport_map.local_phy_port = lport;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd_epe, &epe_header_adjust_phyport_map));

            /* set min frame size */
            CTC_ERROR_RETURN(_sys_greatbelt_port_set_min_frame_size(lchip, gport, SYS_MIN_FRAME_SIZE_MAX_LENGTH_FOR_GMAC));
        }

        p_gb_port_master[lchip]->igs_port_prop[SYS_RESERVE_PORT_ID_CPU].port_mac_type = CTC_PORT_MAC_CPUMAC;

        /*init port pfc*/
        CTC_ERROR_RETURN(sys_greatbelt_port_pfc_init(lchip));


        /* init all internal port stp disable, 60-127 */
        for (lport = SYS_GB_MAX_PHY_PORT; lport < SYS_GB_MAX_PORT_NUM_PER_CHIP; lport++)
        {
            src_cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, src_cmd, &src_port));
            src_port.stp_disable = 1;
            src_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, src_cmd, &src_port));
        }

        /* get port link status */
        sal_memset(&link_status, 0, sizeof(link_status));
        cmd = DRV_IOR(MdioLinkStatus_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &link_status));

        p_gb_port_master[lchip]->status_1g[0] = link_status.mdio0_link_status;
        p_gb_port_master[lchip]->status_1g[1] = link_status.mdio1_link_status;
        p_gb_port_master[lchip]->status_xg[0] = link_status.mdio2_link_status;
        p_gb_port_master[lchip]->status_xg[1] = link_status.mdio3_link_status;

    _sys_greatbelt_port_monitor_link(lchip);
    CTC_ERROR_RETURN(_sys_greatbelt_port_init_nexthop(lchip));

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_LCHIP_NUM, SYS_GB_MAX_LOCAL_CHIP_NUM));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_PHY_PORT_NUM, SYS_GB_MAX_PHY_PORT));
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_MAX_PORT_NUM, SYS_GB_MAX_PORT_NUM_PER_CHIP));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_remove_port_monitor_link(uint8 lchip)
{
    drv_work_platform_type_t platform_type;

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));
    if (platform_type != HW_PLATFORM)
    {
        return CTC_E_NONE;
    }

    sal_task_destroy(p_gb_port_master[lchip]->p_monitor_scan);

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NONE;
    }
    _sys_greatbelt_remove_port_monitor_link(lchip);

    mem_free(p_gb_port_master[lchip]->pp_mtu2);
    mem_free(p_gb_port_master[lchip]->pp_mtu1);
    mem_free(p_gb_port_master[lchip]->egs_port_prop);
    mem_free(p_gb_port_master[lchip]->igs_port_prop);

    sal_mutex_destroy(p_gb_port_master[lchip]->p_port_mutex);
    mem_free(p_gb_port_master[lchip]);
    return CTC_E_NONE;
}

/**
 @brief set the port default configure
*/
int32
sys_greatbelt_port_set_default_cfg(uint8 lchip, uint16 gport)
{
    int32 ret = CTC_E_NONE;
    uint32 lport = 0;
    ds_phy_port_t phy_port;
    ds_phy_port_ext_t phy_port_ext;
    ds_src_port_t src_port;
    ds_dest_port_t dest_port;
    uint32 phy_cmd = 0;
    uint32 phy_ext_cmd = 0;
    uint32 src_cmd = 0;
    uint32 dest_cmd = 0;
    uint32 value    = 0;

    if (p_gb_port_master[lchip] == NULL)
    {
        return CTC_E_NOT_INIT;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d default configure!\n", gport);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    src_cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_FastLearningEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, src_cmd, &value));

    /*init asic table*/
    sal_memset(&phy_port, 0, sizeof(ds_phy_port_t));
    sal_memset(&phy_port_ext, 0, sizeof(ds_phy_port_ext_t));
    sal_memset(&src_port, 0, sizeof(ds_src_port_t));
    sal_memset(&dest_port, 0, sizeof(ds_dest_port_t));

    phy_port.outer_vlan_is_cvlan = 0;
    phy_port.packet_type_valid = 0;

    phy_port_ext.global_src_port = gport;
    phy_port_ext.src_outer_vlan_is_svlan = 1;
    phy_port_ext.default_vlan_id = 1;
    phy_port_ext.use_default_vlan_lookup = 1;
    phy_port_ext.user_id_hash_depend = 1;
    phy_port_ext.exception2_discard = 0x80000000;
    phy_port_ext.exception2_en = 0;

    src_port.receive_disable = 0;
    src_port.bridge_en = 1;
    src_port.route_disable = 0;
    src_port.ingress_filtering_disable = 0;
    src_port.fast_learning_en = value;

    dest_port.global_dest_port = gport;
    dest_port.default_vlan_id = 1;
    dest_port.untag_default_svlan = 1;
    dest_port.untag_default_vlan_id  = 1;
    dest_port.bridge_en = 1;
    dest_port.transmit_disable = 0;
    dest_port.dot1_q_en = CTC_DOT1Q_TYPE_BOTH;
    dest_port.stp_check_en = 1;
    dest_port.svlan_tpid_valid = 0;
    dest_port.cvlan_space = 0;
    dest_port.egress_filtering_disable = 0;

    if (lport >= SYS_GB_MAX_ACL_PORT_BITMAP_NUM) /* port0~51 use bitmap. 52~ use class*/
    {
        src_port.ipv6_key_use_label  = 1;
        src_port.ipv4_key_use_label  = 1;
        src_port.mpls_key_use_label  = 1;
        src_port.mac_key_use_label   = 1;

        dest_port.ipv6_key_use_label  = 1;
        dest_port.ipv4_key_use_label  = 1;
        dest_port.mpls_key_use_label  = 1;
        dest_port.mac_key_use_label  = 1;
    }

    src_port.acl_port_num = lport & 0x3f;
    dest_port.acl_port_num = lport & 0x3f;

    phy_cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    phy_ext_cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    src_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    dest_cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);

    /*do write table*/
    PORT_LOCK;
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, phy_cmd, &phy_port), p_gb_port_master[lchip]->p_port_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, phy_ext_cmd, &phy_port_ext), p_gb_port_master[lchip]->p_port_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, src_cmd, &src_port), p_gb_port_master[lchip]->p_port_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port), p_gb_port_master[lchip]->p_port_mutex);

    p_gb_port_master[lchip]->igs_port_prop[lport].global_src_port = gport;
    p_gb_port_master[lchip]->igs_port_prop[lport].src_outer_is_svlan = 1;
    p_gb_port_master[lchip]->igs_port_prop[lport].default_vlan = 1;
    p_gb_port_master[lchip]->igs_port_prop[lport].receive_disable = 0;
    p_gb_port_master[lchip]->igs_port_prop[lport].bridge_en = 1;
    p_gb_port_master[lchip]->egs_port_prop[lport].default_vlan = 1;
    p_gb_port_master[lchip]->egs_port_prop[lport].bridge_en = 1;
    p_gb_port_master[lchip]->egs_port_prop[lport].transmit_disable = 0;
    p_gb_port_master[lchip]->egs_port_prop[lport].dot1q_type = CTC_DOT1Q_TYPE_BOTH;
    PORT_UNLOCK;

    sys_greatbelt_port_set_global_port(lchip, lport, gport);
    ret = sys_greatbelt_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    if (CTC_E_ENTRY_EXIST != ret && CTC_E_NONE != ret)
    {
        return ret;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_get_aps_failover_en(uint8 lchip, uint16, uint32*);
/**
 @brief set the port global_src_port and global_dest_port in system
*/
int32
sys_greatbelt_port_set_global_port(uint8 lchip, uint8 lport, uint16 gport)
{

    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    uint32 field_value = gport;
    /*uint32 u_enable = 0;*/

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                     "Set Global port, chip_id=%d, lport=%d, gport=%d\n", lchip, lport, gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

   /*Check aps port*/
    #if 0
    CTC_ERROR_RETURN(_sys_greatbelt_port_get_aps_failover_en(lchip, gport,&u_enable));
    if(u_enable)
    {
        return CTC_E_APS_DONT_SUPPORT_HW_BASED_APS_FOR_LINKAGG;
    }
    #endif

    CTC_GLOBAL_PORT_CHECK(gport);
    SYS_GREATBELT_GPORT_TO_GPORT14(field_value);
    /*do write table*/
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_GlobalSrcPort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_GlobalDestPort_f);
    ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

    if (p_gb_port_master[lchip]->use_logic_port_check)
    {
        if (lport < SYS_GB_MAX_PHY_PORT)
        {
            field_value = 1;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultLogicSrcPort_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &field_value);
        }

        /*for learning use logic port
        Logic port:
        0-127 -----------> local phy port
        128 - 191 ---------> linkagg 0-63, base:128
        192 - (192 + 2K)-------> global logic port for userid and vpls learing
        base:192
        */

        if (CTC_IS_LINKAGG_PORT(gport))
        {
            field_value = (gport&0xff) + 127;
        }
        else
        {
            field_value  = lport;
        }

        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultLogicSrcPort_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value  = lport;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_LogicDestPort_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

    }

    CTC_ERROR_RETURN(ret);

    p_gb_port_master[lchip]->igs_port_prop[lport].global_src_port = gport;

    return CTC_E_NONE;

}

/**
 @brief get the port global_src_port and global_dest_port in system. Src and dest are equal.
*/
int32
sys_greatbelt_port_get_global_port(uint8 lchip, uint8 lport, uint16* p_gport)
{

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, \
                     "Get Global port, chip_id=%d, lport=%d\n", lchip, lport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_gport);

    *p_gport = p_gb_port_master[lchip]->igs_port_prop[lport].global_src_port;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_mac_flush_en(uint8 lchip, uint16 gport, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    uint8 sgmac_id = 0;
    uint8 mac_id = 0;

    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;
    if(CTC_PORT_MAC_SGMAC == type)
    {
        mac_id = SYS_GET_MAC_ID(lchip, gport);
        if (0xFF == mac_id)
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }

        sgmac_id = mac_id - SYS_MAX_GMAC_PORT_NUM;
        if(TRUE == enable)
        {
            tbl_id = NetTxForceTxCfg_t;
            field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            field_value &= (~(1 << (sgmac_id + 16)));
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_port_reset_sgmac(lchip, lport));
            CTC_ERROR_RETURN(_sys_greatbelt_port_release_sgmac(lchip, lport, FALSE));
        }

    }
    return ret;
}

STATIC int32
_sys_greatbelt_port_set_port_en(uint8 lchip, uint16 gport, bool enable)
{
    uint8 lport = 0;
    uint32 src_cmd = 0;
    uint32 dest_cmd = 0;

    ds_src_port_t src_port;
    ds_dest_port_t dest_port;
    ctc_chip_device_info_t device_info;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d port enable:%d!\n", gport, enable);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&src_port, 0, sizeof(src_port));
    sal_memset(&dest_port, 0, sizeof(dest_port));
    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));
    src_cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    dest_cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    PORT_LOCK;
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, src_cmd, &src_port), p_gb_port_master[lchip]->p_port_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port), p_gb_port_master[lchip]->p_port_mutex);
    if(src_port.ether_oam_valid)
    {
        src_port.receive_disable   = 0;
        dest_port.transmit_disable = 0;
    }
    else
    {
        src_port.receive_disable   = enable ? 0 : 1;
        dest_port.transmit_disable = enable ? 0 : 1;
    }

    src_cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    dest_cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);

    if (0 == p_gb_port_master[lchip]->egs_port_prop[lport].unidir_en )
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, src_cmd, &src_port), p_gb_port_master[lchip]->p_port_mutex);
    }
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port), p_gb_port_master[lchip]->p_port_mutex);

    sys_greatbelt_chip_get_device_info(lchip, &device_info);
    if (device_info.version_id < 3)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_port_mac_flush_en(lchip, gport, enable), p_gb_port_master[lchip]->p_port_mutex);
    }

     /*CTC_ERROR_RETURN(sys_greatbelt_queue_set_port_drop_en(gport, !enable, SYS_QUEUE_DROP_TYPE_ALL));*/
#if 0
    ret = sys_greatbelt_brguc_nh_create(lchip, gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BASIC);
    if (CTC_E_ENTRY_EXIST != ret && CTC_E_NONE != ret)
    {
        PORT_UNLOCK;
        return ret;
    }
#endif
    PORT_UNLOCK;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_port_en(uint8 lchip, uint16 gport, uint32* p_enable)
{
    uint8 lport = 0;
    ds_src_port_t src_port;
    ds_dest_port_t dest_port;
    uint32 src_cmd = 0;
    uint32 dest_cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d port enable!\n", gport);

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&src_port, 0, sizeof(src_port));
    sal_memset(&dest_port, 0, sizeof(dest_port));

    src_cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    dest_cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, src_cmd, &src_port));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port));

    if ((0 == src_port.receive_disable) && (0 == dest_port.transmit_disable))
    {
        *p_enable = TRUE;
    }
    else
    {
        *p_enable = FALSE;
    }

    return CTC_E_NONE;

}

/**
 @brief decide the port whether is routed port
*/
STATIC int32
_sys_greatbelt_port_set_routed_port(uint8 lchip, uint16 gport, uint32 is_routed)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = (TRUE == is_routed) ? 1 : 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d routed port:%d!\n", gport, is_routed);

    /*do write table*/
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RoutedPort_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_value);

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_RoutedPort_f);
    ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

    return ret;
}

/**
 @brief bind phy_if to port
*/
int32
sys_greatbelt_port_set_phy_if_en(uint8 lchip, uint16 gport, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint32 field_value = 0;
    uint32 cmd = 0;
    uint32 is_phy = FALSE;
    uint16 global_src_port = 0;
    sys_l3if_prop_t l3if_prop;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d phy_if enable:%d\n", gport, enable);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;

    global_src_port = p_gb_port_master[lchip]->igs_port_prop[lport].global_src_port;
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
    l3if_prop.gport     = global_src_port;
    l3if_prop.l3if_type = CTC_L3IF_TYPE_PHY_IF;
    l3if_prop.vaild     = 1;

    is_phy = sys_greatbelt_l3if_is_port_phy_if(lchip, global_src_port);

    if ((TRUE == enable) && (FALSE == is_phy))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_NOT_EXIST, p_gb_port_master[lchip]->p_port_mutex);
    }

#if 0 /*allow destroy interfaced first, then disable in port*/
    if ((FALSE == enable) && (TRUE == is_phy))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_EXIST, p_gb_port_master[lchip]->p_port_mutex);
    }

#endif

    if (TRUE == enable)
    {
        ret = _sys_greatbelt_port_set_routed_port(lchip, gport, TRUE);
        ret = ret ? ret : sys_greatbelt_l3if_get_l3if_info(lchip, 0, &l3if_prop);

        field_value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = l3if_prop.l3if_id;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_InterfaceId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        /* enable phy if should disable bridge */
        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gb_port_master[lchip]->p_port_mutex);
        p_gb_port_master[lchip]->igs_port_prop[lport].route_enable = TRUE;

    }
    else if (p_gb_port_master[lchip]->igs_port_prop[lport].route_enable)
    {
        ret = _sys_greatbelt_port_set_routed_port(lchip, gport, FALSE);

        field_value = CTC_DOT1Q_TYPE_BOTH;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_InterfaceId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_StpDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        field_value = 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultVlanId_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        /* enable phy if should disable bridge */
        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gb_port_master[lchip]->p_port_mutex);
        p_gb_port_master[lchip]->igs_port_prop[lport].route_enable = FALSE;

    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief get phy_if whether enable on port
*/

int32
sys_greatbelt_port_get_phy_if_en(uint8 lchip, uint16 gport, uint16* p_l3if_id, bool* p_enable)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 lport = 0;
    uint32 field_val = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_enable);
    CTC_PTR_VALID_CHECK(p_l3if_id);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d phyical interface!\n", gport);

    /*do write table*/
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    *p_enable = (TRUE == p_gb_port_master[lchip]->igs_port_prop[lport].route_enable) ? TRUE : FALSE;

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_InterfaceId_f);
    ret = DRV_IOCTL(lchip, lport, cmd, &field_val);
    *p_l3if_id = field_val;

    return ret;
}

int32
sys_greatbelt_port_set_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    bool is_svlan = FALSE;
    uint8 direcion = p_vrange_info->direction;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_vrange_info);
    SYS_VLAN_RANGE_INFO_CHECK(p_vrange_info);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d  direction:%d vrange_grpid:%d\n",
                     gport, p_vrange_info->direction, p_vrange_info->vrange_grpid);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(sys_greatbelt_vlan_get_vlan_range_type(lchip, p_vrange_info, &is_svlan));

    if (CTC_INGRESS == direcion)
    {
        if (!enable)
        {
            field_val = 0;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_VlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
        else
        {
            if (is_svlan)
            {
                field_val = 1;
            }
            else
            {
                field_val = 0;
            }

            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_VlanRangeType_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = 1;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_VlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = p_vrange_info->vrange_grpid;
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_VlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
    }
    else
    {
        if (!enable)
        {
            field_val = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_VlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
        else
        {
            if (is_svlan)
            {
                field_val = 1;
            }
            else
            {
                field_val = 0;
            }

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_VlanRangeType_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_VlanRangeProfileEn_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);

            field_val = p_vrange_info->vrange_grpid;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_VlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        }
    }

    return ret;

}

int32
sys_greatbelt_port_get_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool* p_enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 direction = p_vrange_info->direction;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    CTC_PTR_VALID_CHECK(p_vrange_info);
    CTC_BOTH_DIRECTION_CHECK(direction);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d vlan range!\n", gport);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (CTC_INGRESS == direction)
    {
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_VlanRangeProfileEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        if (0 == field_val)
        {
            *p_enable = FALSE;
        }
        else
        {
            cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_VlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
            p_vrange_info->vrange_grpid = field_val;
            *p_enable = TRUE;

        }
    }
    else
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_VlanRangeProfileEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
        if (0 == field_val)
        {
            *p_enable = FALSE;
        }
        else
        {
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_VlanRangeProfile_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_val);
            p_vrange_info->vrange_grpid = field_val;
            *p_enable = TRUE;
        }
    }

    return ret;

}

STATIC int32
_sys_greatbelt_port_set_scl_key_type_ingress(uint8 lchip, uint8 lport, uint8 scl_id, ctc_port_scl_key_type_t type)
{
    ds_phy_port_ext_t phy_port_ext;
    uint32 cmd = 0;
    uint32 hash_type = 0;
    uint32 tcam_type = 0;
    uint32 packet_vlan_high_priority = 0;
    uint16* p_flag = NULL;
    uint8 label = 0;
    uint8 no_feature = TRUE;
    uint8 use_label = FALSE;

    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
    if (0 == scl_id)
    {
        p_flag = &p_gb_port_master[lchip]->igs_port_prop[lport].flag;
    }
    else
    {
        p_flag = &p_gb_port_master[lchip]->igs_port_prop[lport].flag_ext;
    }

    if (0 == *p_flag)
    {
        /*no feature*/
        no_feature = TRUE;

        /* check rpf parameter */

        if ((p_gb_port_master[lchip]->igs_port_prop[lport].flag_ext == CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF)
            && (type != CTC_SCL_KEY_TYPE_IPV4_TUNNEL))
        { /* scl is 0. if scl1 is v4 rpf, scl0 only has one choice*/
            return CTC_E_PORT_FEATURE_MISMATCH;
        }

        if ((CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF == p_gb_port_master[lchip]->igs_port_prop[lport].flag_ext)
            && (type != CTC_SCL_KEY_TYPE_IPV6_TUNNEL))
        { /* scl is 0. if scl1 is v6 rpf, scl0 only has one choice */
            return CTC_E_PORT_FEATURE_MISMATCH;
        }

        if (CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF == type)
        {
            if (0 == scl_id)
            {
                return CTC_E_INVALID_PARAM;
            }

            if (p_gb_port_master[lchip]->igs_port_prop[lport].flag
                && (p_gb_port_master[lchip]->igs_port_prop[lport].flag != CTC_SCL_KEY_TYPE_IPV4_TUNNEL))
            {
                return CTC_E_PORT_FEATURE_MISMATCH;
            }
        }

        if (CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF == type)
        {
            if (0 == scl_id)
            {
                return CTC_E_INVALID_PARAM;
            }

            if (p_gb_port_master[lchip]->igs_port_prop[lport].flag
                && (p_gb_port_master[lchip]->igs_port_prop[lport].flag != CTC_SCL_KEY_TYPE_IPV6_TUNNEL))
            {
                return CTC_E_PORT_FEATURE_MISMATCH;
            }
        }

        hash_type = g_sys_port_scl_map[type].hash_type;
        tcam_type = g_sys_port_scl_map[type].tcam_type;
        packet_vlan_high_priority = g_sys_port_scl_map[type].vlan_high_priority;
        use_label = g_sys_port_scl_map[type].use_label;
        label = g_sys_port_scl_map[type].label;

        switch (type)
        {
        case CTC_SCL_KEY_TYPE_VLAN_CLASS_MAC_DA:
            if (0 == scl_id)
            {
                phy_port_ext.hash1_use_da = 1;
            }
            else
            {
                phy_port_ext.hash2_use_da = 1;
            }

            break;

        case CTC_SCL_KEY_TYPE_IPV4_TUNNEL:
            if (0 == scl_id)
            {
                phy_port_ext.ipv4_tunnel_hash_en1 = 1;
                phy_port_ext.ipv4_gre_tunnel_hash_en1 = 1;
            }
            else
            {
                phy_port_ext.ipv4_tunnel_hash_en2 = 1;
                phy_port_ext.ipv4_gre_tunnel_hash_en2 = 1;
            }

            break;

        /* outer RPF check */
        case CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF:
            phy_port_ext.ipv4_tunnel_hash_en2 = 1;
            phy_port_ext.ipv4_gre_tunnel_hash_en2 = 1;
            phy_port_ext.tunnel_rpf_check = 1;
            break;

        case CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF:
            phy_port_ext.tunnel_rpf_check = 1;
            break;

        case CTC_SCL_KEY_TYPE_DISABLE:
            if (0 == scl_id)
            {
                phy_port_ext.hash1_use_da = 0;
                phy_port_ext.ipv4_tunnel_hash_en1 = 0;
                phy_port_ext.ipv4_gre_tunnel_hash_en1 = 0;
            }
            else
            {
                phy_port_ext.hash2_use_da = 0;
                phy_port_ext.ipv4_tunnel_hash_en2 = 0;
                phy_port_ext.ipv4_gre_tunnel_hash_en2 = 0;
                phy_port_ext.tunnel_rpf_check = 0;
            }

            break;

        default:
            break;
        }

        *p_flag = type;
    }
    else
    {
        /*alread hash feature*/
        no_feature = FALSE;
        if (CTC_SCL_KEY_TYPE_DISABLE == type)
        {
            *p_flag = 0;
            hash_type = SYS_PORT_HASH_SCL_DISABLE;
            tcam_type = SYS_PORT_TCAM_SCL_MAX;
            if (0 == scl_id)
            {
                phy_port_ext.hash1_use_da = 0;
                phy_port_ext.ipv4_tunnel_hash_en1 = 0;
                phy_port_ext.ipv4_gre_tunnel_hash_en1 = 0;
            }
            else
            {
                phy_port_ext.hash2_use_da = 0;
                phy_port_ext.ipv4_tunnel_hash_en2 = 0;
                phy_port_ext.ipv4_gre_tunnel_hash_en2 = 0;
                phy_port_ext.tunnel_rpf_check = 0;
            }
        }
        else if (type != (*p_flag))
        {
            return CTC_E_PORT_HAS_OTHER_FEATURE;
        }
        else
        {
            return CTC_E_NONE;
        }
    }

    if ((no_feature) || (CTC_SCL_KEY_TYPE_DISABLE == type))
    {
        if (scl_id == 0)
        {
            phy_port_ext.user_id_hash1_type      = hash_type;
            phy_port_ext.user_id_tcam1_type      = (tcam_type == SYS_PORT_TCAM_SCL_MAX) ? 0 : tcam_type;
            phy_port_ext.user_id_tcam1_en        = (tcam_type == SYS_PORT_TCAM_SCL_MAX) ? 0 : 1;
            phy_port_ext.hash_lookup1_use_label  = use_label;
            phy_port_ext.user_id_label1          = label;
        }
        else
        {
            phy_port_ext.user_id_hash2_type      = hash_type;
            phy_port_ext.user_id_tcam2_type      = (tcam_type == SYS_PORT_TCAM_SCL_MAX) ? 0 : tcam_type;
            phy_port_ext.user_id_tcam2_en        = (tcam_type == SYS_PORT_TCAM_SCL_MAX) ? 0 : 1;
            phy_port_ext.hash_lookup2_use_label  = use_label;
            phy_port_ext.user_id_label2          = label;
        }
    }

    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_IngressTagHighPriority_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &packet_vlan_high_priority));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_scl_key_type_egress(uint8 lchip, uint8 lport, uint8 scl_id, ctc_port_scl_key_type_t type)
{
    ds_dest_port_t dest_port;
    uint32 cmd = 0;
    uint32 hash_type = 0;
    uint16* p_flag = NULL;
    uint8 no_feature = FALSE;

    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));
    if (0 == scl_id)
    {
        p_flag = &p_gb_port_master[lchip]->egs_port_prop[lport].flag;
    }
    else
    {
        p_flag = &p_gb_port_master[lchip]->egs_port_prop[lport].flag_ext;
    }

    if (0 == *p_flag)
    {
        no_feature = TRUE;

        switch (type)
        {
        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_PORT:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_PORT;
            break;

        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_CVLAN;
            break;

        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_SVLAN;
            break;

        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID_CCOS:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_CVLAN_COS;
            break;

        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID_SCOS:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_SVLAN_COS;
            break;

        case CTC_SCL_KEY_TYPE_VLAN_MAPPING_DVID:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_TWO_VLAN;
            break;

        case CTC_SCL_KEY_TYPE_DISABLE:
            hash_type = SYS_PORT_HASH_EGRESS_SCL_DISABLE;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        *p_flag = type;
    }
    else
    {
        no_feature = FALSE;
        if (CTC_SCL_KEY_TYPE_DISABLE == type)
        {
            *p_flag = 0;
            /*config hash userid scl_id*/
            hash_type = SYS_PORT_HASH_EGRESS_SCL_DISABLE;
        }
        else if (type != *p_flag)
        {
            return CTC_E_PORT_HAS_OTHER_FEATURE;
        }
        else
        {
            return CTC_E_NONE;
        }
    }

    if ((no_feature) || (CTC_SCL_KEY_TYPE_DISABLE == type))
    {
        if (0 == scl_id)
        {
            dest_port.vlan_hash1_type = hash_type;
        }
        else
        {
            dest_port.vlan_hash2_type = hash_type;
        }
    }

    dest_port.vlan_hash_use_label = FALSE;
    dest_port.vlan_hash_label = 0;

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));

    return CTC_E_NONE;
}

/**
 @brief bind phy_if to port
*/
int32
sys_greatbelt_port_set_sub_if_en(uint8 lchip, uint16 gport, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint32 field_value = 0;
    uint32 cmd = 0;
    uint32 is_sub = FALSE;
    uint16 global_src_port = 0;
    sys_l3if_prop_t l3if_prop;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d phy_if enable:%d\n", gport, enable);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;

    global_src_port = p_gb_port_master[lchip]->igs_port_prop[lport].global_src_port;
    sal_memset(&l3if_prop, 0, sizeof(sys_l3if_prop_t));
    l3if_prop.gport     = global_src_port;
    l3if_prop.l3if_type = CTC_L3IF_TYPE_SUB_IF;
    l3if_prop.vaild     = 1;

    is_sub = sys_greatbelt_l3if_is_port_sub_if(lchip, global_src_port);

    if ((TRUE == enable) && (FALSE == is_sub))
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(CTC_E_L3IF_NOT_EXIST, p_gb_port_master[lchip]->p_port_mutex);
    }


    if (TRUE == enable)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_port_set_scl_key_type_ingress(lchip,
                                                     lport,
                                                     0,
                                                     CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID),
                                                     p_gb_port_master[lchip]->p_port_mutex);

        ret = ret ? ret : _sys_greatbelt_port_set_routed_port(lchip, gport, TRUE);


          /* enable sub if should disable bridge */
        field_value = 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gb_port_master[lchip]->p_port_mutex);

        p_gb_port_master[lchip]->igs_port_prop[lport].subif_en = TRUE;


    }
    else if (p_gb_port_master[lchip]->igs_port_prop[lport].subif_en)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(
        _sys_greatbelt_port_set_scl_key_type_ingress(lchip,
                                                     lport,
                                                     0,
                                                     CTC_SCL_KEY_TYPE_DISABLE),
                                                     p_gb_port_master[lchip]->p_port_mutex);

        ret = ret ? ret : _sys_greatbelt_port_set_routed_port(lchip, gport, FALSE);

        /* enable sub if should disable bridge */
        field_value = 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BridgeEn_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &field_value);

        CTC_ERROR_RETURN_WITH_UNLOCK(ret, p_gb_port_master[lchip]->p_port_mutex);

        p_gb_port_master[lchip]->igs_port_prop[lport].subif_en = FALSE;
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

/**
 @brief get phy_if whether enable on port
*/

int32
sys_greatbelt_port_get_sub_if_en(uint8 lchip, uint16 gport,  bool* p_enable)
{
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_enable);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d phyical interface!\n", gport);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    *p_enable = (TRUE == p_gb_port_master[lchip]->igs_port_prop[lport].subif_en) ? TRUE : FALSE;

    return ret;
}

int32
sys_greatbelt_port_set_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 scl_id, ctc_port_scl_key_type_t type)
{
    uint8 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_BOTH_DIRECTION_CHECK(direction);
    CTC_MAX_VALUE_CHECK(scl_id, 1);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set gport:%d  direction:%d  scl_id:%d  vlan mapping type:%d\n",
                     gport, direction, scl_id, type);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (type >= CTC_SCL_KEY_TYPE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_INGRESS == direction)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_scl_key_type_ingress(lchip, lport, scl_id, type));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_scl_key_type_egress(lchip, lport, scl_id, type));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 scl_id, ctc_port_scl_key_type_t* p_type)
{
    uint8 lport = 0;
    uint16* p_flag = NULL;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d  direction:%d  scl_id:%d\n", gport, direction, scl_id);

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_type);
    CTC_BOTH_DIRECTION_CHECK(direction);
    CTC_MAX_VALUE_CHECK(scl_id, 1);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (CTC_INGRESS == direction)
    {
        if (0 == scl_id)
        {
            p_flag = &p_gb_port_master[lchip]->igs_port_prop[lport].flag;
        }
        else
        {
            p_flag = &p_gb_port_master[lchip]->igs_port_prop[lport].flag_ext;
        }
    }
    else
    {
        if (0 == scl_id)
        {
            p_flag = &p_gb_port_master[lchip]->egs_port_prop[lport].flag;
        }
        else
        {
            p_flag = &p_gb_port_master[lchip]->egs_port_prop[lport].flag_ext;
        }
    }

    if (*p_flag >= CTC_SCL_KEY_TYPE_MAX)
    {
        return CTC_E_INVALID_PARAM;
    }
    else
    {
        *p_type = *p_flag;
    }

    return CTC_E_NONE;
}

/*-------------------------mac serdes--------------------------------------*/
int32
sys_greatbelt_port_get_quad_use_num(uint8 lchip, uint8 gmac_id, uint8* p_use_num)
{
    uint8 temp = 0;
    uint8 lport = 0;
    uint8 quad_id = 0;
    uint8 mac_id = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_use_num);

    quad_id = gmac_id / 4;
    mac_id  = quad_id * 4;

    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
    {
        temp++;
    }

    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (mac_id + 1));
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
    {
        temp++;
    }

    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (mac_id + 2));
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
    {
        temp++;
    }

    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (mac_id + 3));
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
    {
        temp++;
    }

    *p_use_num = temp;

    return CTC_E_NONE;
}

/**
 @brief retrive mac config
*/
int32
_sys_greatbelt_port_retrieve_gmac_cfg(uint8 lchip, uint8 gmac_id)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    bool enable = FALSE;
    uint16 mtu1 = 0;
    uint16 mtu2 = 0;
    uint8 index = 0;

    index = (gmac_id>>2);

    mtu1 = (uint16)p_gb_port_master[lchip]->pp_mtu1[index];
    mtu2 = p_gb_port_master[lchip]->pp_mtu2[index];

    /* retrieve mac mut1 */
    field_value = (uint32)mtu1;
    tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
    tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
    cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_PacketLenMtu1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve mac mut2 */
    field_value = (uint32)mtu2;
    tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
    tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
    cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_PacketLenMtu2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve saturate enable */
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_saturate_en(lchip, CTC_STATS_TYPE_GMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
        cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_IncrSaturate_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* retrieve clear_after_read */
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_clear_after_read_en(lchip, CTC_STATS_TYPE_GMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        tbl_id = QuadMacStatsCfg0_t +  (gmac_id/4)* tbl_step1;
        cmd = DRV_IOW(tbl_id, QuadMacStatsCfg_ClearOnRead_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return CTC_E_NONE;
}


/**
 @brief retrive sgmac config
*/
int32
_sys_greatbelt_port_retrieve_sgmac_cfg(uint8 lchip, uint8 sgmac_id)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    bool enable = FALSE;
    uint16 mtu1 = 0;
    uint16 mtu2 = 0;

    mtu1 = p_gb_port_master[lchip]->pp_mtu1[SYS_STATS_SGMAC_STATS_RAM0+sgmac_id];
    mtu2 = p_gb_port_master[lchip]->pp_mtu2[SYS_STATS_SGMAC_STATS_RAM0+sgmac_id];

    /* retrieve mac mut1 */
    field_value = (uint32)mtu1;
    tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
    tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
    cmd = DRV_IOW(tbl_id, SgmacStatsCfg_PacketLenMtu1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve mac mut2 */
    field_value = (uint32)mtu2;
    tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
    tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
    cmd = DRV_IOW(tbl_id, SgmacStatsCfg_PacketLenMtu2_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* retrieve saturate enable */
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_saturate_en(lchip, CTC_STATS_TYPE_SGMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
        cmd = DRV_IOW(tbl_id, SgmacStatsCfg_IncrSaturate_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* retrieve clear_after_read */
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_clear_after_read_en(lchip, CTC_STATS_TYPE_SGMAC, &enable));
    if (enable)
    {
        field_value = 1;
        tbl_step1 = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        tbl_id = SgmacStatsCfg0_t + sgmac_id * tbl_step1;
        cmd = DRV_IOW(tbl_id, SgmacStatsCfg_ClearOnRead_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_pause_speed_mode(uint8 lchip, uint8 lport, ctc_port_speed_t speed_mode)
{
    drv_datapath_port_capability_t port_cap;
    net_rx_pause_timer_mem_t net_rx_pause_timer_mem;
    net_tx_port_mode_tab_t   net_tx_port_mode_tab;
    uint32 cmd_rx = 0;
    uint32 cmd_tx = 0;
    uint8 support_switch = 0;

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

    if(!port_cap.valid)
    {
        return CTC_E_NONE;
    }

    cmd_rx = DRV_IOR(NetRxPauseTimerMem_t, DRV_ENTRY_FLAG);
    cmd_tx = DRV_IOR(NetTxPortModeTab_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd_rx, &net_rx_pause_timer_mem));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd_tx, &net_tx_port_mode_tab));
    if (CTC_PORT_MAC_GMAC == p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type)
    {
        /*tx 0, 10G; 1, 1G; 2, 100M; 3, 10M*/
        if (CTC_PORT_SPEED_1G == speed_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 0;
            net_tx_port_mode_tab.data         = 1;
        }
        else if (CTC_PORT_SPEED_100M == speed_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 3;
            net_tx_port_mode_tab.data         = 2;
        }
        else if (CTC_PORT_SPEED_10M == speed_mode)
        {
            /*not support*/
        }
    }
    else if(CTC_PORT_MAC_SGMAC == p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type)
    {
        /*Rx 3'd0: 1G; 3'd1: 10G; 3'd2: 10M; 3'd3: 100M; 3'd4: 2.5G*/
        /*Tx 0, 10G; 1, 1G; 2, 100M; 3, 10G->1G*/
        if (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 0;
            CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, port_cap.serdes_id, DRV_CHIP_SERDES_SWITCH_INFO, &support_switch));
            if(1 == support_switch)
            {
                net_tx_port_mode_tab.data         = 3;
            }
            else
            {
                net_tx_port_mode_tab.data         = 1;
            }

        }
        else if (DRV_SERDES_XFI_MODE == port_cap.pcs_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 1;
            net_tx_port_mode_tab.data         = 0;
        }
        else if (DRV_SERDES_XAUI_MODE == port_cap.pcs_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 1;
            net_tx_port_mode_tab.data         = 0;
        }
        else if (DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode)
        {
            net_rx_pause_timer_mem.speed_mode = 4;
            net_tx_port_mode_tab.data         = 1;
        }
    }

    cmd_rx = DRV_IOW(NetRxPauseTimerMem_t, DRV_ENTRY_FLAG);
    cmd_tx = DRV_IOW(NetTxPortModeTab_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd_rx, &net_rx_pause_timer_mem));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd_tx, &net_tx_port_mode_tab));
    return CTC_E_NONE;
}

/**
 @brief set sgmac enable
*/
int32
_sys_greatbelt_port_release_sgmac(uint8 lchip, uint8 lport, uint8 flush_dis)
{

    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
 /*    uint8 index = 0;*/
    uint8 pcs_mode = 0;
    uint8 sgmac_id = 0;
    uint8 mac_id = 0;
    sgmac_soft_rst_t sgmac_rst;
    ctc_chip_device_info_t device_info;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 1, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        return CTC_E_NONE;
    }

    sal_memset(&sgmac_rst, 0, sizeof(sgmac_soft_rst_t));

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs_mode:%d\n", pcs_mode);
    sgmac_id = mac_id;
    tbl_id = NetTxForceTxCfg_t;
    field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value |= (1 << (sgmac_id + 16));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 9. disable the port in net tx */
    tbl_id = NetTxChannelEn_t;
    field_id = NetTxChannelEn_PortEnNet59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value &= (~(1 << (sgmac_id + 16)));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    /* 1. release sgmac reg reset */
    field_id = ResetIntRelated_ResetSgmac0Reg_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 2. cfg sgmac related reg */
    tbl_id = SgmacCfg0_t + sgmac_id;
    if ((DRV_SERDES_XGSGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        field_value = 1;
    }
    else if (DRV_SERDES_XFI_MODE == pcs_mode)
    {
        field_value = 2;
    }
    else if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        field_value = 3;
    }
    cmd = DRV_IOW(tbl_id, SgmacCfg_PcsMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    sys_greatbelt_chip_get_device_info(lchip, &device_info);
    if(device_info.version_id > 2)
    {
        field_value = 1;
        cmd = DRV_IOW(tbl_id, SgmacCfg_IfsStretchMode_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    /* need config sgmac mode: xaui+ or xaui2 or xaui, use default value for xaui */

    /* 3. enable sgmac clock */

    /* 4. cfg PCS related reg */
    /* use default value for pcs configuration */

    /* 5. release sgmac logic reset */
    field_id = ResetIntRelated_ResetSgmac0_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    field_value = 0;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 11. release pcs tx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsTxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 7. relaese xaui serdes rx soft reset */
    if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        tbl_id = SgmacSoftRst4_t + (sgmac_id - 4);
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
        sgmac_rst.pcs_rx_soft_rst = 0;
        sgmac_rst.serdes_rx0_soft_rst = 0;
        sgmac_rst.serdes_rx1_soft_rst = 0;
        sgmac_rst.serdes_rx2_soft_rst = 0;
        sgmac_rst.serdes_rx3_soft_rst = 0;
        cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
    }
    else
    {
        /* 6. release pcs rx soft reset */
        tbl_id = SgmacSoftRst0_t + sgmac_id;
        field_value = 0;
        cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 8. de-assert flush op */
    if(TRUE == flush_dis)
    {
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (sgmac_id + 16)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    CTC_ERROR_RETURN(sys_greatbelt_chip_serdes_tx_en_with_mac(lchip, SYS_MAX_GMAC_PORT_NUM+mac_id));

    /* 10. enable the port in NET TX */
    tbl_id = NetTxChannelEn_t;
    field_id = NetTxChannelEn_PortEnNet59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value |= (1 << (sgmac_id + 16));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
/*
    CTC_ERROR_RETURN(_sys_greatbelt_port_retrieve_sgmac_cfg(lchip, sgmac_id));
*/
    /*delay 2ms, wait mac status ready */
    sal_task_sleep(2);

    /*set rx pause speed*/
    if(TRUE == flush_dis)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_set_pause_speed_mode(lchip, lport, CTC_PORT_SPEED_MAX));
    }

    return CTC_E_NONE;
}

/**
 @brief set sgmac disable
*/
int32
_sys_greatbelt_port_reset_sgmac(uint8 lchip, uint8 lport)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 pcs_mode = 0;
    uint8 sgmac_id = 0;
    uint8 mac_id = 0;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 1, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        return CTC_E_NONE;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs_mode:%d\n", pcs_mode);

    sgmac_id = mac_id;

    /* 1. set xaui serdes rx soft reset */
    if (DRV_SERDES_XAUI_MODE == pcs_mode)
    {
        tbl_id = SgmacSoftRst4_t + (sgmac_id - 4);
        field_value = 1;

        for (index = 0; index < 4; index++)
        {
            field_id = SgmacSoftRst_SerdesRx0SoftRst_f + index;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }

    /* 2. set pcs rx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 3. set pcs tx soft reset */
    tbl_id = SgmacSoftRst0_t + sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsTxSoftRst_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* support port link interrupt need remove reg reset*/
#if 0
    /* 4. set sgmac reg reset */
    field_id = ResetIntRelated_ResetSgmac0Reg_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif

    /* 5. set sgmac reset */
    field_id = ResetIntRelated_ResetSgmac0_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    field_value = 1;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 6. disable sgmc clock */

    /* 8. flush the residual packet in net tx packet buffer */
    tbl_id = NetTxForceTxCfg_t;
    field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value |= (1 << (sgmac_id + 16));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#if 0
    /* 9. disable the port in net tx */
    tbl_id = NetTxChannelEn_t;
    field_id = NetTxChannelEn_PortEnNet59To32_f;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    field_value &= (~(1 << (sgmac_id + 16)));
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif
    /* 11. wait flush, 5ms */
    sal_task_sleep(5);

    return CTC_E_NONE;
}

/**
 @brief set gmac enable
*/
int32
_sys_greatbelt_port_release_gmac(uint8 lchip, uint8 lport, ctc_port_speed_t mac_speed)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint32 speed_value = 0;
    uint8 pcs_mode = 0;
    uint8 gmac_id = 0;
    uint8 mac_id = 0;
    uint8 serdes_id, used_num;
    uint32 tmp_value = 0;
    drv_datapath_port_capability_t port_cap;
    ctc_chip_device_info_t device_info;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 0, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac is not valid\n");
        return CTC_E_NONE;
    }


    gmac_id = mac_id;

    switch (mac_speed)
    {
    case CTC_PORT_SPEED_10M:
        speed_value = 0;
        break;

    case CTC_PORT_SPEED_100M:
        speed_value = 1;
        break;

    case CTC_PORT_SPEED_1G:
        speed_value = 2;
        break;

    case CTC_PORT_SPEED_2G5:
        speed_value = 3;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "pcs_mode:%d\n", pcs_mode);

    if (gmac_id < 32)
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << gmac_id);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 13. disable port in Net Tx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << gmac_id));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << (gmac_id - 32));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 13. disable port in Net Tx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (gmac_id - 32)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    }

    /* 1. release gmac reg reset */
    field_id = ResetIntRelated_ResetQuadMac0Reg_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    field_value = 0;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 2. cfg gmac relate reg */
    tbl_step1 = QuadMacGmac1Mode0_t - QuadMacGmac0Mode0_t;
    tbl_step2 = QuadMacGmac0Mode1_t - QuadMacGmac0Mode0_t;
    tbl_id = QuadMacGmac0Mode0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
    field_id = QuadMacGmac0Mode_Gmac0SpeedMode_f;
    field_value = speed_value;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 3. enable gmac clock */

    /* 4. enable pcs clock */

    /* 5. release pcs reg reset */
    tbl_id = ResetIntRelated_t;
    field_value = 0;
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        field_id = ResetIntRelated_ResetQuadPcs0Reg_f + gmac_id / 4;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 6. cfg pcs relate reg */
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        tbl_id = QuadPcsPcs0AnegCfg0_t + (gmac_id / 4) * QUADPCS_QUAD_STEP + (gmac_id % 4) * QUADPCS_PCS_STEP;
        field_id = QuadPcsPcs0AnegCfg_Pcs0SpeedMode_f;
    }
    else
    {
        tbl_id = QsgmiiPcs0AnegCfg0_t + (gmac_id / 4) * QSGMIIPCS_QUAD_STEP + (gmac_id % 4) * QSGMIIPCS_PCS_STEP;
        field_id = QsgmiiPcs0AnegCfg_Pcs0SpeedMode_f;
    }

    field_value = speed_value;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 7. release QuadMacApp logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetQuadMac0App_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 8. release pcs logic reset */
    tbl_id = ResetIntRelated_t;
    field_value = 0;
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        if (gmac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }

        field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 9. release gmac logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetGmac0_f + gmac_id;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    if (gmac_id < 32)
    {
        /* 10. De-assert flush operation */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << gmac_id));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 11. enable transmission to phyport right away in NetTx, no need to do */

        /* 12. enable port in NetTx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << gmac_id);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        /* 10. De-assert flush operation */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (gmac_id - 32)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 11. enable transmission to phyport right away in NetTx, no need to do */

        /* 12. enable port in NetTx */
        tbl_id= NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << (gmac_id - 32));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 13. release pcs tx soft reset */
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
        tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
        tbl_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
        field_value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
    {
        sys_greatbelt_chip_get_device_info(lchip, &device_info);
        if (device_info.version_id >= 2)
        {
            tbl_id = QsgmiiPcs0AnegCfg0_t + (gmac_id / 4) * QSGMIIPCS_QUAD_STEP + (gmac_id % 4) * QSGMIIPCS_PCS_STEP;
            field_id = QsgmiiPcs0AnegCfg_Pcs0AnEnable_f;
            /*read first*/
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_value));
            /*reset autolink*/
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            field_value = 0;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            /*write back*/
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp_value));
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_chip_serdes_tx_en_with_mac(lchip, mac_id));

    /* 14. release gmac tx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiTxSoftRst_f;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 15. release gmac rx soft reset */
    tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
    tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
    tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
    field_value = 0;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 16. release pcs rx soft reset */
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
        tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
        tbl_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
        field_value = 0;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* set quadmacinitdone */
    tbl_id = QuadMacInitDone0_t + (gmac_id /4);
    field_id = QuadMacInitDone_QuadMacInitDone_f;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#if 0
    /* when set mac enable, need retrieve mac configure */
    CTC_ERROR_RETURN(_sys_greatbelt_port_retrieve_gmac_cfg(lchip, gmac_id));
#endif
    /*delay 2ms, wait mac status ready */
    sal_task_sleep(2);

    if(DRV_SERDES_QSGMII_MODE == pcs_mode)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_get_quad_use_num(lchip, gmac_id, &used_num));
        if (used_num == 0)
        {
            if (TRUE == SYS_MAC_IS_VALID(lchip, gmac_id))
            {
                lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, gmac_id);
                if (0xFF == lport)
                {
                    return CTC_E_INVALID_PARAM;
                }
                sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
                drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
                if (port_cap.valid)
                {
                    serdes_id = port_cap.serdes_id;
                    sys_greatbelt_chip_reset_rx_serdes(lchip, serdes_id, 1);
                    sal_task_sleep(5);
                    sys_greatbelt_chip_reset_rx_serdes(lchip, serdes_id, 0);
                }
            }
        }
    }

    /*set rx pause speed*/
    CTC_ERROR_RETURN(_sys_greatbelt_port_set_pause_speed_mode(lchip, lport, mac_speed));
    return CTC_E_NONE;
}

/**
 @brief set gmac disable
*/
int32
_sys_greatbelt_port_reset_gmac(uint8 lchip, uint8 lport)
{
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8  used_num = 0;
    uint8 pcs_mode = 0;
    uint8 gmac_id = 0;
    uint8 mac_id = 0;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 0, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac is not valid\n");
        return CTC_E_NONE;
    }

    gmac_id = mac_id;

    /* 0. check whether to reset quadmac/quadpcs */
    CTC_ERROR_RETURN(sys_greatbelt_port_get_quad_use_num(lchip, gmac_id, &used_num));


    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "used_num:%d, pcs_mode;%d\n", used_num, pcs_mode);

    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        if (gmac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
#if 0
    if (used_num <= 1)
    {
        /* 1. set quad mac app logic reset */
        tbl_id  = ResetIntRelated_t;
        field_id = ResetIntRelated_ResetQuadMac0App_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
#endif
        /* 2. set gmac tx soft reset */
        tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
        tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
        tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadMacGmac0SoftRst_Gmac0SgmiiTxSoftRst_f;
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 3. set pcs soft reset */
        if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
        {
            tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
            tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
            tbl_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

            field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
            field_value = 1;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
        {

        }

        /* 4. set gmac rx soft reset */
        tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
        tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
        tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;
        field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
        field_value = 1;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

#if 0

        /* 5. set gmac reg reset */
        field_id = ResetIntRelated_ResetQuadMac0Reg_f + SYS_GMAC_RESET_INT_STEP * (gmac_id / 4);
        cmd = DRV_IOW(ResetIntRelated_t, field_id);
        field_value = 1;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* need support link interrupt */

        /* 6. set pcs reg reset */
        tbl_id = ResetIntRelated_t;
        field_value = 1;
        if (DRV_SERDES_SGMII_MODE == pcs_mode)
        {
            field_id = ResetIntRelated_ResetQuadPcs0Reg_f + gmac_id / 4;
            cmd = DRV_IOW(ResetIntRelated_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
#endif


    /* 7. set gmac logic reset */
    tbl_id  = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetGmac0_f + gmac_id;
    field_value = 1;
    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* 8.set Pcs Logic reset */
    tbl_id = ResetIntRelated_t;
    field_value = 1;
    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    /* 9. disable gmac clock */

    /* 10. disable pcs clock */

    if (gmac_id < 32)
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << gmac_id);
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#if 0
        /* 13. disable port in Net Tx */
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChannelEn_PortEnNet31To0_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << gmac_id));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif
    }
    else
    {
        /* 12. assert flush operatiom */
        tbl_id = NetTxForceTxCfg_t;
        field_id = NetTxForceTxCfg_ForceTxPort59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value |= (1 << (gmac_id - 32));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        /* 13. disable port in Net Tx */
#if 0
        tbl_id = NetTxChannelEn_t;
        field_id = NetTxChanTxDisCfg_PortTxDisNet59To32_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        field_value &= (~(1 << (gmac_id - 32)));
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif
    }

    /* 12. wait flush, 5ms */
    sal_task_sleep(5);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_gmac_en(uint8 lchip, uint8 lport, bool enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (TRUE == enable)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_release_gmac(lchip, lport,
                                                          p_gb_port_master[lchip]->igs_port_prop[lport].speed_mode));
    }
    else
    {
        /*if mac already is disabled, no need to do */
        if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en == FALSE)
        {
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_port_reset_gmac(lchip, lport));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_sgmac_en(uint8 lchip, uint8 lport, bool enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);


     /*CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_by_lport(lchip, lport, &sgmac_id))*/

    if (TRUE == enable)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_port_release_sgmac(lchip, lport, TRUE));
    }
    else
    {
        /*if mac already is disabled, no need to do */
        if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en == FALSE)
        {
            return CTC_E_NONE;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_port_reset_sgmac(lchip, lport));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_cpu_mac_en(uint8 lchip, uint8 lport, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_val = (TRUE == enable) ? 1 : 0;

    if (SYS_RESERVE_PORT_ID_CPU != lport)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOW(CpuMacRxCtl_t, CpuMacRxCtl_RxEnable_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_val);

    if (DRV_E_NONE != ret)
    {
        return ret;
    }

    cmd = DRV_IOW(CpuMacTxCtl_t, CpuMacTxCtl_TxEnable_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_val);

    if (DRV_E_NONE == ret)
    {
        p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en = ((TRUE == enable) ? TRUE : FALSE);
    }

    return ret;
}

STATIC int32
_sys_greatbelt_port_set_mac_en(uint8 lchip, uint16 gport, bool enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint8 serdes_num = 0;
    uint8 serdes_id  = 0;
    uint8 index = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    drv_work_platform_type_t platform_type = HW_PLATFORM;
    drv_datapath_port_capability_t port_cap;
    ctc_chip_serdes_prbs_t prbs;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d mac enable:%d\n", gport, enable);

    sal_memset(&prbs, 0, sizeof(ctc_chip_serdes_prbs_t));
    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));

    if ((HW_PLATFORM == platform_type) || (drv_greatbelt_io_is_asic()))
    {
        if (NULL == p_gb_port_master[lchip])
        {
            return CTC_E_NOT_INIT;
        }

        /* get serdes info by gport */
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
        if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en == enable)
        {
            return CTC_E_NONE;
        }
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if (port_cap.valid)
        {
            serdes_id = port_cap.serdes_id;
            if((DRV_SERDES_XAUI_MODE==port_cap.pcs_mode)||(DRV_SERDES_QSGMII_MODE==port_cap.pcs_mode))
            {
                serdes_num = 4;
            }
            else
            {
                serdes_num = 1;
            }
        }

        /* if mac disable, disable serdes tx and before enable serdes tx, need enable prbs15+ for sgmii*/
        if(!enable)
        {
            if((DRV_SERDES_XAUI_MODE != port_cap.pcs_mode) && (DRV_SERDES_QSGMII_MODE != port_cap.pcs_mode))
            {
                for (index = 0; index < serdes_num; index++)
                {
                    if ((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode) || (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode))
                    {
                        prbs.mode = 1;
                        prbs.serdes_id = serdes_id+index;
                        prbs.polynome_type = CTC_CHIP_SERDES_PRBS15_SUB;
                        prbs.value = 1;
                        CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_prbs_tx(lchip, &prbs));
                        /* at less 10ms before close serdes tx*/
                        sal_task_sleep(35);
                    }
                    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_tx_en(lchip, (serdes_id+index), FALSE));
                }
            }
        }

        PORT_LOCK;
        type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

        switch (type)
        {
            case CTC_PORT_MAC_GMAC:
                ret = _sys_greatbelt_port_set_gmac_en(lchip, lport, enable);
                break;

            case CTC_PORT_MAC_SGMAC:
                ret = _sys_greatbelt_port_set_sgmac_en(lchip, lport, enable);
                break;

            default:
                ret = CTC_E_NONE;
        }

        if (CTC_E_NONE == ret)
        {
            p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en  = enable;
        }

        PORT_UNLOCK;

        if(!enable)
        {
            /* if mac disable, after reset, need disable prbs15+ for sgmii*/
            for (index = 0; index < serdes_num; index++)
            {
                if ((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode) || (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode))
                {
                    prbs.mode = 1;
                    prbs.serdes_id = serdes_id+index;
                    prbs.polynome_type = CTC_CHIP_SERDES_PRBS15_SUB;
                    prbs.value = 0;
                    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_prbs_tx(lchip, &prbs));
                }
            }
        }
    }

    return ret;
}

int32
sys_greatbelt_port_get_mac_en(uint8 lchip, uint16 gport, uint32* p_enable)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;
    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    if (type != CTC_PORT_MAC_MAX)
    {
        *p_enable = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en ? TRUE : FALSE;
    }
    else
    {
        ret = CTC_E_NONE;
    }

    PORT_UNLOCK;

    return ret;

}

STATIC int32
_sys_greatbelt_port_get_gmac_link_up(uint8 lchip, uint8 lport, uint32* p_is_up, uint32 unidir_en, uint32 is_port)
{
    uint32 auto_enable = 0;
    uint32 auto_complete = 0;
    uint32 auto_mode = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 pcs_mode = 0;
    uint8 mac_id = 0;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 0, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        return CTC_E_NONE;
    }

     /*CTC_ERROR_RETURN(drv_greatbelt_get_gmac_info(lchip, lport, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));*/

    if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
    {
        /* QuadPCS Mode*/
        if (mac_id > 23)
        {
            return CTC_E_INVALID_PARAM;
        }

        /* adjust pcs reset is release or not */
        tbl_id = ResetIntRelated_t;
        field_id = ResetIntRelated_ResetQuadPcs0Reg_f + mac_id / 4;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        if (field_value)
        {
            /* pcs reg is reset */
          *p_is_up = 0;
           return CTC_E_NONE;
        }

        tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
        field_id = QuadPcsPcs0AnegCfg_Pcs0AnEnable_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_enable));

        field_id = QuadPcsPcs0AnegCfg_Pcs0AnegMode_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_mode));

        if (auto_enable)
        {
            /* get link status from auto-neg status register*/
            tbl_id = QuadPcsPcs0AnegStatus0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
            field_id = QuadPcsPcs0AnegStatus_Pcs0LinkStatus_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            if (is_port && field_value && (CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == auto_mode))
            {
                field_id = QuadPcsPcs0AnegStatus_Pcs0AnComplete_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_complete));
                if (auto_complete)
                {
                    field_id = QuadPcsPcs0AnegStatus_Pcs0LinkUp_f;
                    cmd = DRV_IOR(tbl_id, field_id);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                }
            }
        }
        else
        {
            /* get link status from sysc status */
            tbl_id = QuadPcsPcs0Status0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
            field_id = QuadPcsPcs0Status_Pcs0SyncStatus_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }
    else
    {
        /* QuadSGMII Mode*/
        tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
        field_id = QsgmiiPcs0AnegCfg_Pcs0AnEnable_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_enable));

        if (auto_enable)
        {
            /* get link status from auto-neg status register*/
            tbl_id = QsgmiiPcs0AnegStatus0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
            field_id = QsgmiiPcs0AnegStatus_Pcs0LinkStatus_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            if (is_port && field_value)
            {
                field_id = QsgmiiPcs0AnegStatus_Pcs0AnComplete_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_complete));
                if (auto_complete)
                {
                    field_id = QsgmiiPcs0AnegStatus_Pcs0LinkUp_f;
                    cmd = DRV_IOR(tbl_id, field_id);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                }
            }
        }
        else
        {
            /* get link status from sysc status */
            tbl_id = QsgmiiPcsStatus0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP;
            field_id = QsgmiiPcsStatus_Pcs0SyncStatus_f + (mac_id % 4);
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }

    if (field_value)
    {
        *p_is_up = 1;
    }
    else
    {
        *p_is_up = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_sgmac_link_up(uint8 lchip, uint8 lport, uint32* p_is_up, uint32 unidir_en, uint32 is_port)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 pcs_mode = 0;
    uint8 sgmac_id = 0;
    uint8 mac_id = 0;
    uint32 value = 0;
    uint32 auto_complete = 0;
    uint32 auto_mode = 0;
    sgmac_pcs_status_t sgmac_pcs;

    SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 1, mac_id, pcs_mode);
    /* mac is not used */
    if (0xFF == mac_id)
    {
        return CTC_E_NONE;
    }

    sgmac_id = mac_id;

     /*CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_by_lport(lchip, lport, &sgmac_id))*/
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port mac link:sgmac_id:%d \n", sgmac_id);


    /* adjust pcs reset is release or not */
    tbl_id = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetSgmac0Reg_f + SYS_SGMAC_RESET_INT_STEP * sgmac_id;
    cmd = DRV_IOR(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    if (field_value)
    {
        /* pcs reg is reset */
      *p_is_up = 0;
       return CTC_E_NONE;
    }

     /*CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_info(lchip, sgmac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));*/
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port mac link:pcs_mode:%d \n", pcs_mode);

    sal_memset(&sgmac_pcs, 0, sizeof(sgmac_pcs_status_t));

    if (pcs_mode == DRV_SERDES_XFI_MODE)
    {
        tbl_id = SgmacPcsStatus0_t + sgmac_id;
        cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_pcs));
        {
            if ((sgmac_pcs.xfi_block_lock) && (!sgmac_pcs.xfi_hi_ber))
            {
                *p_is_up = 1;
                return CTC_E_NONE;
            }
            else
            {
                *p_is_up = 0;
                return CTC_E_NONE;
            }
        }

    }
    else if (pcs_mode == DRV_SERDES_XAUI_MODE)
    {
        tbl_id = SgmacPcsStatus0_t + sgmac_id;
        field_id = SgmacPcsStatus_XgmacPcsAlignStatus_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        if (field_value && (!unidir_en))
        {
            tbl_id = SgmacDebugStatus0_t + sgmac_id;
            field_id = SgmacDebugStatus_XgmiiRxFaultType_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            field_value = value ? FALSE : TRUE;
        }
    }
    else if ((pcs_mode == DRV_SERDES_XGSGMII_MODE) || (pcs_mode == DRV_SERDES_2DOT5_MODE))
    {
        tbl_id = SgmacPcsAnegStatus0_t + sgmac_id;
        field_id = SgmacPcsAnegStatus_AnLinkStatus_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        tbl_id = SgmacPcsAnegCfg0_t + sgmac_id;
        field_id = SgmacPcsAnegCfg_AnegMode_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_mode));
        if (is_port && field_value && (CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == auto_mode))
        {
            field_id = SgmacPcsAnegStatus_AnComplete_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &auto_complete));
            if (auto_complete)
            {
                field_id = SgmacPcsAnegStatus_AnLinkUp_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            }
        }
    }

    if (field_value)
    {
        *p_is_up = 1;
    }
    else
    {
        *p_is_up = 0;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_port_speed(uint8 lchip, uint32 gport, ctc_port_speed_t * p_speed_mode)
{
    uint8 lport = 0;
    int32 ret = 0;
    uint32 cmd = 0;
    uint8 pcs_mode = 0;
    uint8 mac_id = 0;
    cpu_mac_misc_ctl_t cpu_mac_misc_ctl;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 field_value = 0;
    drv_datapath_port_capability_t port_cap;

    sal_memset(&cpu_mac_misc_ctl, 0, sizeof(cpu_mac_misc_ctl_t));
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    if(CTC_IS_CPU_PORT(gport))
    {
        lport = SYS_RESERVE_PORT_ID_CPU;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(gport)), lchip);
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    }

    pcs_mode = pcs_mode;
    PORT_LOCK;
    switch (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type)
    {
    case CTC_PORT_MAC_GMAC:
        /*Only when port enable, cfg real speed; else just store speed mode, and will cfg when enable*/
        if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
        {
            SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 0, mac_id, pcs_mode);
            if (0xFF == mac_id)
            {
                PORT_UNLOCK;
                return CTC_E_INVALID_LOCAL_PORT;
            }

            tbl_step1 = QuadMacGmac1Mode0_t - QuadMacGmac0Mode0_t;
            tbl_step2 = QuadMacGmac0Mode1_t - QuadMacGmac0Mode0_t;
            tbl_id = QuadMacGmac0Mode0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
            field_id = QuadMacGmac0Mode_Gmac0SpeedMode_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_value),p_gb_port_master[lchip]->p_port_mutex);
            switch (field_value)
            {
            case 0 :
                *p_speed_mode = CTC_PORT_SPEED_10M;
                break;

            case 1 :
                *p_speed_mode = CTC_PORT_SPEED_100M;
                break;

            case 2 :
                *p_speed_mode = CTC_PORT_SPEED_1G;
                break;

            case 3:
                *p_speed_mode = CTC_PORT_SPEED_2G5;
                break;

            default:
                PORT_UNLOCK;
                return CTC_E_INVALID_LOCAL_PORT;
            }
        }
        else
        {
            *p_speed_mode = p_gb_port_master[lchip]->igs_port_prop[lport].speed_mode;
        }

        break;


    case CTC_PORT_MAC_SGMAC:
        SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 1, mac_id, pcs_mode);
        if (0xFF == mac_id)
        {
            PORT_UNLOCK;
            return CTC_E_INVALID_LOCAL_PORT;
        }

        tbl_id = SgmacCfg0_t + mac_id;
        cmd = DRV_IOR(tbl_id, SgmacCfg_PcsMode_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_value),p_gb_port_master[lchip]->p_port_mutex);
        switch (field_value)
        {
            case 1 :
                *p_speed_mode = CTC_PORT_SPEED_1G;
                drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
                if (port_cap.pcs_mode == DRV_SERDES_2DOT5_MODE)
                {
                    *p_speed_mode = CTC_PORT_SPEED_2G5;
                }
                break;

            default:
                *p_speed_mode = CTC_PORT_SPEED_10G;
                break;
        }
        break;

    case CTC_PORT_MAC_CPUMAC:

        cmd = DRV_IOR(CpuMacMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_misc_ctl),p_gb_port_master[lchip]->p_port_mutex);

        switch (cpu_mac_misc_ctl.speed_mode)
        {
            case 0 :
                *p_speed_mode = CTC_PORT_SPEED_10M;
                break;
            case 1 :
                *p_speed_mode = CTC_PORT_SPEED_100M;
                break;
            case 2 :
                *p_speed_mode = CTC_PORT_SPEED_1G;
                break;
            default:
                *p_speed_mode = CTC_PORT_SPEED_1G;
                break;
        }

        break;
    default:
        ret = CTC_E_INVALID_LOCAL_PORT;
    }

    PORT_UNLOCK;

    return ret;
}
STATIC int32
_sys_greatbelt_port_set_speed(uint8 lchip, uint32 gport, ctc_port_speed_t speed_mode)
{
    uint8 lport = 0;
    int32 ret = 0;
    uint32 cmd = 0;
    ctc_port_mac_type_t mac_type = CTC_PORT_MAC_MAX;
    cpu_mac_misc_ctl_t cpu_mac_misc_ctl;
    sal_memset(&cpu_mac_misc_ctl, 0, sizeof(cpu_mac_misc_ctl_t));

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    if(CTC_IS_CPU_PORT(gport))
    {
        lport = SYS_RESERVE_PORT_ID_CPU;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(gport)), lchip);
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    }

    PORT_LOCK;
    mac_type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (mac_type)
    {
    case CTC_PORT_MAC_GMAC:
        /*Only when port enable, cfg real speed; else just store speed mode, and will cfg when enable*/
        if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_en)
        {
            ret = _sys_greatbelt_port_reset_gmac(lchip, lport);
            ret = ret ? ret : _sys_greatbelt_port_release_gmac(lchip, lport, speed_mode);
        }
        else
        {
            ret = CTC_E_NONE;
        }

        break;

    case CTC_PORT_MAC_CPUMAC:

        cmd = DRV_IOR(CpuMacMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_misc_ctl), p_gb_port_master[lchip]->p_port_mutex);

        switch (speed_mode)
        {
            case CTC_PORT_SPEED_10M:
                cpu_mac_misc_ctl.speed_mode = 0;
                break;
            case CTC_PORT_SPEED_100M:
                cpu_mac_misc_ctl.speed_mode = 1;
                break;
            case CTC_PORT_SPEED_1G:
                cpu_mac_misc_ctl.speed_mode = 2;
                break;
            default:
                cpu_mac_misc_ctl.speed_mode = 2;
                break;
        }

        cmd = DRV_IOW(CpuMacMiscCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_misc_ctl), p_gb_port_master[lchip]->p_port_mutex);

        break;
    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    if (CTC_E_NONE == ret)
    {
        p_gb_port_master[lchip]->igs_port_prop[lport].speed_mode = speed_mode;
    }

    PORT_UNLOCK;

    return ret;
}

int32
sys_greatbelt_set_cpu_mac_en(uint8 lchip, bool enable)
{



        CTC_ERROR_RETURN(_sys_greatbelt_port_set_cpu_mac_en(lchip, SYS_RESERVE_PORT_ID_CPU, enable));


    return CTC_E_NONE;
}

int32
sys_greatbelt_get_cpu_mac_en(uint8 lchip, bool* p_enable)
{
    uint8  gchip_id = 0;
    uint16 gport = 0;
    uint32 value = 0;

    CTC_PTR_VALID_CHECK(p_enable);
    *p_enable = 1;


        sys_greatbelt_get_gchip_id(lchip, &gchip_id);
        gport = CTC_MAP_LPORT_TO_GPORT(gchip_id, SYS_RESERVE_PORT_ID_CPU);
        sys_greatbelt_port_get_mac_en(lchip, gport, &value);
        *p_enable &= value;

    return CTC_E_NONE;
}

/*return speed mode, 11-10bit:10-1000M 01-100M 00-10M*/
int32
sys_greatbelt_port_get_sgmii_auto_neg_speed(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 pcs_mode = 0;
    uint8 mac_id = 0;
    uint16 lport = 0;
    uint8 type = 0;

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sgmii auto neg speed:gport:%d type:%d\n", gport, type);

    if (CTC_PORT_MAC_GMAC == type)
    {
        SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 0, mac_id, pcs_mode);
        /* mac is not used */
        if (0xFF == mac_id)
        {
            return CTC_E_NONE;
        }

        if (DRV_SERDES_SGMII_MODE == pcs_mode)
        {
            tbl_id = QuadPcsPcs0AnegStatus0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
            field_id = QuadPcsPcs0AnegStatus_Pcs0LinkSpeedMode_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
        else
        {
            tbl_id = QsgmiiPcs0AnegStatus0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
            field_id = QsgmiiPcs0AnegStatus_Pcs0LinkSpeedMode_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }
    }
    else if (CTC_PORT_MAC_SGMAC == type)
    {
        SYS_GREATBELT_PORT_GET_MAC_PCS(lchip, lport, 1, mac_id, pcs_mode);
        /* mac is not used */
        if (0xFF == mac_id)
        {
            return CTC_E_NONE;
        }
        tbl_id = SgmacPcsAnegStatus0_t + mac_id;
        field_id = SgmacPcsAnegStatus_AnLinkSpeedMode_f;
        cmd = DRV_IOR(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        return CTC_E_INVALID_PORT_MAC_TYPE;
    }

    *p_value = field_value;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_mac_link_up(uint8 lchip, uint32 gport, uint32* p_is_up, uint32 is_port)
{
    int32 ret = 0;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    uint32 unidir_en = 0;

    CTC_PTR_VALID_CHECK(p_is_up);

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if(CTC_IS_CPU_PORT(gport))
    {
        lport = SYS_RESERVE_PORT_ID_CPU;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(gport)), lchip);
    }
    else
    {
        SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    }
    _sys_greatbelt_port_get_unidir_en(lchip, gport, &unidir_en);
    PORT_LOCK;
    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port mac link:gport:%d type:%d\n", gport, type);

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_get_gmac_link_up(lchip, lport, p_is_up, unidir_en, is_port);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_get_sgmac_link_up(lchip, lport, p_is_up, unidir_en, is_port);
        break;

    case CTC_PORT_MAC_CPUMAC:
        *p_is_up = 1;      /* always up */
        ret = CTC_E_NONE;
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    PORT_UNLOCK;

    return ret;
}


int32
sys_greatbelt_port_set_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop)
{
    uint16 gport;
    uint8 dir;
    uint8 lport = 0;
    uint8 chan_id = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    buf_store_net_pfc_req_ctl_t  pfc_ctl;
    net_rx_pause_type_t rx_pause_type;
    ds_channelize_mode_t ds_channel_mode;
    drv_datapath_port_capability_t port_cap;
    sys_qos_fc_prop_t flow_ctl_prop;


    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    gport = fc_prop->gport;
    dir = fc_prop->dir;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(fc_prop->priority_class, 7);

    PORT_LOCK;

    /* rtl support max port num is 60 */
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        PORT_UNLOCK;
        return CTC_E_PORT_MAC_IS_DISABLE;
    }


    chan_id = port_cap.chan_id;
    mac_id  = port_cap.mac_id;

    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        sal_memset(&rx_pause_type, 0, sizeof(rx_pause_type));
        cmd = DRV_IOR(NetRxPauseType_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &rx_pause_type),p_gb_port_master[lchip]->p_port_mutex);

        /*Rx port pfc enable*/
        if (fc_prop->is_pfc)
        {
            if (mac_id < 32)
            {
                CTC_BIT_SET(rx_pause_type.pause_type_low, mac_id);
            }
            else
            {
                CTC_BIT_SET(rx_pause_type.pause_type_hi, mac_id - 32);
            }
        }
        else
        {
            if (mac_id < 32)
            {
                CTC_BIT_UNSET(rx_pause_type.pause_type_low, mac_id);
            }
            else
            {
                CTC_BIT_UNSET(rx_pause_type.pause_type_hi, mac_id - 32);
            }
        }

        cmd = DRV_IOW(NetRxPauseType_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &rx_pause_type),p_gb_port_master[lchip]->p_port_mutex);

        sal_memset(&ds_channel_mode, 0, sizeof(ds_channel_mode));
        cmd = DRV_IOR(DsChannelizeMode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, mac_id, cmd, &ds_channel_mode),p_gb_port_master[lchip]->p_port_mutex);

        /*PFC enable/disable*/
        /*vlan_base: when channelizeEn, it is vlanBase, otherwise it is {rxEn,pauseFrameEn,10d`0}*/
        if (fc_prop->is_pfc)
        {
            CTC_BIT_SET(ds_channel_mode.vlan_base, FLOW_CONTROL_RX_EN_BIT);
            if (fc_prop->enable)
            {
                /*enable PFC cos rx*/
                CTC_BIT_SET(ds_channel_mode.local_phy_port_base, fc_prop->priority_class);
                CTC_BIT_SET(ds_channel_mode.vlan_base, FLOW_CONTROL_PAUSE_EN_BIT);
            }
            else
            {
                /*disable PFC cos rx*/
                CTC_BIT_UNSET(ds_channel_mode.local_phy_port_base, fc_prop->priority_class);
                if (0 == ds_channel_mode.local_phy_port_base)
                {
                    CTC_BIT_UNSET(ds_channel_mode.vlan_base, FLOW_CONTROL_PAUSE_EN_BIT);
                }
            }
        }
        else  /*Normal Pause enable/disable*/
        {
            CTC_BIT_SET(ds_channel_mode.vlan_base, FLOW_CONTROL_RX_EN_BIT);
            if (fc_prop->enable)
            {
                CTC_BIT_SET(ds_channel_mode.vlan_base, FLOW_CONTROL_PAUSE_EN_BIT);
            }
            else
            {
                CTC_BIT_UNSET(ds_channel_mode.vlan_base, FLOW_CONTROL_PAUSE_EN_BIT);
            }
        }
        cmd = DRV_IOW(DsChannelizeMode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, mac_id, cmd, &ds_channel_mode),p_gb_port_master[lchip]->p_port_mutex);



    }

    /* egress only global contral */
    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        flow_ctl_prop.cos = fc_prop->priority_class;
        flow_ctl_prop.enable = fc_prop->enable;
        flow_ctl_prop.gport  = fc_prop->gport;
        flow_ctl_prop.is_pfc = fc_prop->is_pfc;
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_qos_flow_ctl_profile(lchip, &flow_ctl_prop),p_gb_port_master[lchip]->p_port_mutex);

        sal_memset(&pfc_ctl, 0, sizeof(pfc_ctl));
        cmd = DRV_IOR(BufStoreNetPfcReqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &pfc_ctl),p_gb_port_master[lchip]->p_port_mutex);

        /*Rx port pfc enable*/
        if (fc_prop->is_pfc)
        {
            if (chan_id < 32)
            {
                CTC_BIT_SET(pfc_ctl.pfc_chan_en_low, chan_id);
            }
            else
            {
                CTC_BIT_SET(pfc_ctl.pfc_chan_en_high, chan_id - 32);
            }
        }
        else
        {
            if (chan_id < 32)
            {
                CTC_BIT_UNSET(pfc_ctl.pfc_chan_en_low, chan_id);
            }
            else
            {
                CTC_BIT_UNSET(pfc_ctl.pfc_chan_en_high, chan_id - 32);
            }
        }

        cmd = DRV_IOW(BufStoreNetPfcReqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &pfc_ctl),p_gb_port_master[lchip]->p_port_mutex);

        cmd = DRV_IOR(BufStoreNetPfcReqCtl_t, BufStoreNetPfcReqCtl_PfcPriorityEnChan0_f + chan_id);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_val),p_gb_port_master[lchip]->p_port_mutex);

        fc_prop->priority_class = (fc_prop->is_pfc)?fc_prop->priority_class:0;

        if (fc_prop->enable)
        {
            CTC_BIT_SET(field_val, fc_prop->priority_class);
        }
        else
        {
            CTC_BIT_UNSET(field_val, fc_prop->priority_class);
        }

        cmd = DRV_IOW(BufStoreNetPfcReqCtl_t, BufStoreNetPfcReqCtl_PfcPriorityEnChan0_f + chan_id);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_val),p_gb_port_master[lchip]->p_port_mutex);


        cmd = DRV_IOR(DsIgrPortTcPriMap_t, DsIgrPortTcPriMap_PfcPriBmp0_f + fc_prop->priority_class);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &field_val),p_gb_port_master[lchip]->p_port_mutex);
        if (fc_prop->enable)
        {
            CTC_BIT_SET(field_val, fc_prop->priority_class);
        }
        else
        {
            CTC_BIT_UNSET(field_val, fc_prop->priority_class);
        }
        cmd = DRV_IOW(DsIgrPortTcPriMap_t, DsIgrPortTcPriMap_PfcPriBmp0_f + fc_prop->priority_class);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, chan_id, cmd, &field_val),p_gb_port_master[lchip]->p_port_mutex);


        field_val = 1;
        cmd = DRV_IOW(NetTxMiscCtl_t, NetTxMiscCtl_PauseEn_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &field_val),p_gb_port_master[lchip]->p_port_mutex);
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop)
{
    uint16 gport;
    uint8 dir;
    uint8 lport = 0;
    uint8 chan_id = 0;
    uint8 mac_id = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_val = 0;
    ds_channelize_mode_t ds_channel_mode;
    drv_datapath_port_capability_t port_cap;


    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    gport = fc_prop->gport;
    dir = fc_prop->dir;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(fc_prop->priority_class, 7);

    PORT_LOCK;

    /* rtl support max port num is 60 */
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        PORT_UNLOCK;
        return CTC_E_PORT_MAC_IS_DISABLE;
    }


    chan_id = port_cap.chan_id;
    mac_id  = port_cap.mac_id;

    if (CTC_BOTH_DIRECTION == dir)
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    if (CTC_INGRESS == dir)
    {
        sal_memset(&ds_channel_mode, 0, sizeof(ds_channel_mode));
        cmd = DRV_IOR(DsChannelizeMode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, mac_id, cmd, &ds_channel_mode), ret, exit_unlock);

        /*PFC enable/disable*/
        if (fc_prop->is_pfc)
        {
           fc_prop->enable = CTC_IS_BIT_SET(ds_channel_mode.local_phy_port_base, fc_prop->priority_class);
        }
        else  /*Normal Pause enable/disable*/
        {
            fc_prop->enable = CTC_IS_BIT_SET(ds_channel_mode.vlan_base, FLOW_CONTROL_PAUSE_EN_BIT);
        }

    }

    /* egress only global contral */
    if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOR(BufStoreNetPfcReqCtl_t, BufStoreNetPfcReqCtl_PfcPriorityEnChan0_f + chan_id);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &field_val), ret, exit_unlock);


        fc_prop->priority_class = (fc_prop->is_pfc)?fc_prop->priority_class:0;

        fc_prop->enable = CTC_IS_BIT_SET(field_val, fc_prop->priority_class);


    }

exit_unlock:
    PORT_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_gmac_preamble(uint8 lchip, uint8 lport, uint8 pre_bytes)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_val = pre_bytes;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    CTC_VALUE_RANGE_CHECK(pre_bytes, SYS_MIN_PREAMBLE_FOR_GMAC, SYS_MAX_PREAMBLE_FOR_GMAC);

    tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
    tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
    tbl_id = QuadMacGmac0TxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

    cmd = DRV_IOW(tbl_id, QuadMacGmac0TxCtrl_Gmac0PreLength_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_gmac_preamble(uint8 lchip, uint8 lport, uint8* p_pre_bytes)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_val = 0;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_PTR_VALID_CHECK(p_pre_bytes);

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
    tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
    tbl_id = QuadMacGmac0TxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

    cmd = DRV_IOR(tbl_id, QuadMacGmac0TxCtrl_Gmac0PreLength_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_pre_bytes = (uint8)field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_sgmac_preamble(uint8 lchip, uint8 lport, uint8 pre_bytes)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*only 4Bytes and 8Bytes can be configed to sgmac*/
    if (SYS_MIN_PREAMBLE_FOR_SGMAC == pre_bytes)
    {
        field_val = 1;
    }
    else if (SYS_MAX_PREAMBLE_FOR_SGMAC == pre_bytes)
    {
        field_val = 0;
    }
    else
    {
        return CTC_E_INVALID_PREAMBLE;
    }

    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);

    cmd = DRV_IOW(tbl_id, SgmacCfg_Preamble4Bytes_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_sgmac_preamble(uint8 lchip, uint8 lport, uint8* p_pre_bytes)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_PTR_VALID_CHECK(p_pre_bytes);

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);

    cmd = DRV_IOR(tbl_id, SgmacCfg_Preamble4Bytes_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*only 4Bytes and 8Bytes can be configed to sgmac*/
    if (1 == field_val)
    {
        *p_pre_bytes = SYS_MIN_PREAMBLE_FOR_SGMAC;
    }
    else if (0 == field_val)
    {
        *p_pre_bytes = SYS_MAX_PREAMBLE_FOR_SGMAC;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_preamble(uint8 lchip, uint16 gport, uint8 pre_bytes)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d preamble bytes:%d\n", gport, pre_bytes);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_set_gmac_preamble(lchip, lport, pre_bytes);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_set_sgmac_preamble(lchip, lport, pre_bytes);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_port_get_preamble(uint8 lchip, uint16 gport, uint8* p_pre_bytes)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_get_gmac_preamble(lchip, lport, p_pre_bytes);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_get_sgmac_preamble(lchip, lport, p_pre_bytes);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    PORT_UNLOCK;

    return ret;
}

STATIC int32
_sys_greatbelt_port_set_max_frame(uint8 lchip, uint16 gport, uint32 value)
{
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 max_value = 0;
    uint32 index = 0;

    if (value > SYS_GREATBELT_MAX_FRAME_SIZE)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    cmd = DRV_IOW(DsChannelizeMode_t, DsChannelizeMode_MaxPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &value));

    for (index = 0; index < SYS_GB_MAX_PHY_PORT; index++)
    {
        cmd = DRV_IOR(DsChannelizeMode_t, DsChannelizeMode_MaxPktLen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_value));
        max_value = (max_value < field_value) ? field_value : max_value;
    }

    cmd = DRV_IOW(BufStoreMiscCtl_t, BufStoreMiscCtl_MaxPktSize_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &max_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_max_frame(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    cmd = DRV_IOR(DsChannelizeMode_t, DsChannelizeMode_MaxPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, p_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_channel_min_frame_size(uint8 lchip, uint8 lport, uint8 size)
{
    uint32 cmd = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint32 ptp_en = 0;
    uint8 mac_id = 0;

    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    CTC_VALUE_RANGE_CHECK(size, SYS_MIN_FRAME_SIZE_MIN_LENGTH_FOR_GMAC, SYS_MIN_FRAME_SIZE_MAX_LENGTH_FOR_GMAC);

    mac_id  = port_cap.mac_id;

    /* get ptp enable */
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type == CTC_PORT_MAC_GMAC)
    {
        tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
        tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
        tbl_id = QuadMacGmac0PtpCfg0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

        cmd = DRV_IOR(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_en));
    }
    else if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type == CTC_PORT_MAC_SGMAC)
    {
        tbl_id = SgmacCfg0_t + (mac_id-SYS_MAX_GMAC_PORT_NUM);
        cmd = DRV_IOR(tbl_id, SgmacCfg_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_en));
    }
    else
    {
        return CTC_E_INVALID_PORT_MAC_TYPE;
    }

    cmd = DRV_IOR(BufStoreMiscCtl_t, BufStoreMiscCtl_MinPktSize_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    if (field_val > size)
    {
        field_val = size;
        cmd = DRV_IOW(BufStoreMiscCtl_t, BufStoreMiscCtl_MinPktSize_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    cmd = DRV_IOR(NetTxMiscCtl_t, NetTxMiscCtl_CfgMinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    if (field_val > size)
    {
        field_val = size;
        cmd = DRV_IOW(NetTxMiscCtl_t, NetTxMiscCtl_CfgMinPktLen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    field_val = size;
    if (ptp_en)
    {
        field_val += 8;
    }

    cmd = DRV_IOW(DsChannelizeIngFc_t, DsChannelizeIngFc_MinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    cmd = DRV_IOW(DsChannelizeMode_t, DsChannelizeMode_MinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_channel_min_frame_size(uint8 lchip, uint8 lport, uint8* size)
{
    uint32 cmd = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint32 ptp_en = 0;
    uint8 mac_id = 0;

    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

    /* do not return error when mac is not used */
    if (!port_cap.valid)
    {
        return CTC_E_NONE;
    }

    mac_id  = port_cap.mac_id;

    /* need to modify after use new api. should use channel */
    cmd = DRV_IOR(DsChannelizeMode_t, DsChannelizeMode_MinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &field_val));

    *size = field_val;

    /* get ptp enable */
    if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type == CTC_PORT_MAC_GMAC)
    {
        tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
        tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
        tbl_id = QuadMacGmac0PtpCfg0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

        cmd = DRV_IOR(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_en));
    }
    else if (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type == CTC_PORT_MAC_SGMAC)
    {
        tbl_id = SgmacCfg0_t + (mac_id-SYS_MAX_GMAC_PORT_NUM);
        cmd = DRV_IOR(tbl_id, SgmacCfg_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_en));
    }
    else
    {
        return CTC_E_INVALID_PORT_MAC_TYPE;
    }


    if (ptp_en)
    {
        *size = field_val - 8;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_port_set_gmac_min_frame_size(uint8 lchip, uint8 lport, uint8 size)
{
    uint32 cmd = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    int32 ret = CTC_E_NONE;
    uint8 mac_id = 0;
    uint8 gchip = 0;
    uint16 gport = 0;


    CTC_VALUE_RANGE_CHECK(size, SYS_MIN_FRAME_SIZE_MIN_LENGTH_FOR_GMAC, SYS_MIN_FRAME_SIZE_MAX_LENGTH_FOR_GMAC);

    ret = _sys_greatbelt_port_set_channel_min_frame_size(lchip, lport, size);
    /* do not set min frame size if mac is not used */
    if (CTC_E_PORT_MAC_IS_DISABLE == ret)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if(0xFF == mac_id)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    tbl_step1 = QuadMacGmac1RxCtrl0_t - QuadMacGmac0RxCtrl0_t;
    tbl_step2 = QuadMacGmac0RxCtrl1_t - QuadMacGmac0RxCtrl0_t;
    tbl_id = QuadMacGmac0RxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

    field_val = size;
    cmd = DRV_IOW(tbl_id, QuadMacGmac0RxCtrl_Gmac0MinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0;
    cmd = DRV_IOW(tbl_id, QuadMacGmac0RxCtrl_Gmac0RuntRcvEnable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}
#if 0
STATIC int32
_sys_greatbelt_port_get_gmac_min_frame_size(uint8 lchip, uint8 lport, uint8* p_size)
{
    uint32 cmd = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;

    tbl_step1 = QuadMacGmac1RxCtrl0_t - QuadMacGmac0RxCtrl0_t;
    tbl_step2 = QuadMacGmac0RxCtrl1_t - QuadMacGmac0RxCtrl0_t;
    tbl_id = QuadMacGmac0RxCtrl0_t + (lport % 4) * tbl_step1 + (lport / 4) * tbl_step2;

    cmd = DRV_IOR(tbl_id, QuadMacGmac0RxCtrl_Gmac0MinPktLen_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_size = (uint8)field_val;

    return CTC_E_NONE;
}
#endif
STATIC int32
_sys_greatbelt_port_set_min_frame_size(uint8 lchip, uint16 gport, uint8 size)
{
    int32 ret = 0;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d min frame size:%d\n", gport, size);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_set_gmac_min_frame_size(lchip, lport, size);
        break;

    /* only support config sgmac min pktlen on channel */
    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_set_channel_min_frame_size(lchip, lport, size);
        /* do not set min frame size if mac is not used */
        if (CTC_E_PORT_MAC_IS_DISABLE == ret)
        {
            ret = CTC_E_NONE;
        }
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_port_get_min_frame_size(uint8 lchip, uint16 gport, uint8* p_size)
{
    int32 ret = 0;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
         /*ret = _sys_greatbelt_port_get_gmac_min_frame_size(lchip, lport, p_size);*/
        ret = _sys_greatbelt_port_get_channel_min_frame_size(lchip, lport, p_size);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_get_channel_min_frame_size(lchip, lport, p_size);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    PORT_UNLOCK;

    return ret;
}

STATIC int32
_sys_greatbelt_port_set_gmac_ipg(uint8 lchip, uint8 lport, uint32 value)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_val = value;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
    tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
    tbl_id = QuadMacGmac0TxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

    cmd = DRV_IOW(tbl_id, QuadMacGmac0TxCtrl_Gmac0IpgCfg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_gmac_ipg(uint8 lchip, uint8 lport, uint32* p_value)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 field_val = 0;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_PTR_VALID_CHECK(p_value);

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
    tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
    tbl_id = QuadMacGmac0TxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;

    cmd = DRV_IOR(tbl_id, QuadMacGmac0TxCtrl_Gmac0IpgCfg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_value = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_sgmac_ipg(uint8 lchip, uint8 lport, uint32 value)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = value;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);

    cmd = DRV_IOW(tbl_id, SgmacCfg_IpgCfg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_sgmac_ipg(uint8 lchip, uint8 lport, uint32* p_value)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_val = 0;
    uint16 gport = 0;
    uint8 mac_id = 0;
    uint8 gchip = 0;

    CTC_PTR_VALID_CHECK(p_value);

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);

    cmd = DRV_IOR(tbl_id, SgmacCfg_IpgCfg_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_value = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_mac_ipg(uint8 lchip, uint16 gport, uint32 value)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d Ipg bytes:%d\n", gport, value);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if ((4 != value) && (8 != value) && (12 != value))
    {
        return CTC_E_INVALID_PARAM;
    }
    value = value>>2;
    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_set_gmac_ipg(lchip, lport, value);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_set_sgmac_ipg(lchip, lport, value);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_port_get_mac_ipg(uint8 lchip, uint16 gport, uint32* p_value)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    uint32 value = 0;
\
    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    PORT_LOCK;

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    switch (type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_get_gmac_ipg(lchip, lport, &value);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_get_sgmac_ipg(lchip, lport, &value);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    PORT_UNLOCK;

    *p_value = value<<2;

    return ret;
}

#if 0
STATIC int32
_sys_greatbelt_port_set_sgmac_stretch_mode_en(uint8 lchip, uint16 gport, bool enable)
{
    uint32 cmd = 0;
    uint8  lchip = 0;
    uint8  mac_id = 0;
    uint32 tbl_id = 0;
    sgmac_cfg_t sgmac_cfg;

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);

    sal_memset(&sgmac_cfg, 0, sizeof(sgmac_cfg_t));
    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

    if (FALSE == enable)
    {
        sgmac_cfg.ifs_stretch_mode = 0;
        sgmac_cfg.ifs_stretch_ratio = 0;
        sgmac_cfg.ifs_stretch_size_init = 0;
        sgmac_cfg.dic_cnt_enable = 1;
    }
    else
    {
        sgmac_cfg.ifs_stretch_mode = 1;
        sgmac_cfg.ifs_stretch_ratio = 13;
        sgmac_cfg.ifs_stretch_size_init = 12;
        sgmac_cfg.dic_cnt_enable = 0;
    }

    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

    return CTC_E_NONE;
}
#endif

STATIC int32
_sys_greatbelt_port_set_pading_en(uint8 lchip, uint16 gport, bool enable)
{
    int32  ret = CTC_E_NONE;
    uint8  lport = 0;
    uint8  mac_id = 0;
    uint8  tbl_step1 = 0;
    uint8  tbl_step2 = 0;
    uint16 tbl_id = 0;
    uint16 field_id = 0;
    uint32 cmd = 0;
    uint32 field_value = 1;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    if (mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
        tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
        tbl_id    = QuadMacGmac0TxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
        field_id  = QuadMacGmac0TxCtrl_Gmac0PadEnable_f;
    }
    else
    {
        tbl_step1 = SgmacCfg1_t - SgmacCfg0_t;
        tbl_id    = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM) * tbl_step1;
        field_id  = SgmacCfg_PadEnable_f;
    }

    field_value = enable ? 1 : 0;

    PORT_LOCK;
    cmd = DRV_IOW(tbl_id, field_id);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);

    if (CTC_E_NONE == ret)
    {
        p_gb_port_master[lchip]->egs_port_prop[lport].pading_en = enable ? 1 : 0;
    }

    PORT_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_gmac_tx_threshold(uint8 lchip, uint8 lport, uint8 tx_threshold)
{
    uint8  tbl_step1 = 0;
    uint8  tbl_step2 = 0;
    uint16 tbl_id = 0;
    uint16 field_id = 0;
    uint32 cmd = 0;
    uint32 field_value = tx_threshold;

    tbl_step1 = QuadMacGmac1TxCtrl0_t - QuadMacGmac0TxCtrl0_t;
    tbl_step2 = QuadMacGmac0TxCtrl1_t - QuadMacGmac0TxCtrl0_t;
    tbl_id = QuadMacGmac0TxCtrl0_t + (lport % 4) * tbl_step1 + (lport / 4) * tbl_step2;
    field_id = QuadMacGmac0TxCtrl_Gmac0TxThreshold_f;

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_sgmac_tx_threshold(uint8 lchip, uint8 lport, uint8 tx_threshold)
{
    uint8 mac_id = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 field_val = tx_threshold;

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);
    field_id = SgmacCfg_TxThreshold_f;

    cmd = DRV_IOW(tbl_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_tx_threshold(uint8 lchip, uint16 gport, uint8 tx_threshold)
{
    uint8 lport = 0;
    int32 ret = CTC_E_NONE;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (CTC_PORT_MAC_GMAC == p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type)
    {
        /*tx_threshold of gmac has 7 bits*/
        if (tx_threshold > 0x7f)
        {
            return CTC_E_INVALID_TX_THRESHOLD;
        }
    }
    else
    {
        /*tx_threshold of sgmac has 6 bits*/
        if (tx_threshold > 0x3f)
        {
            return CTC_E_INVALID_TX_THRESHOLD;
        }
    }

    PORT_LOCK;
    switch (p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type)
    {
    case CTC_PORT_MAC_GMAC:
        ret = _sys_greatbelt_port_set_gmac_tx_threshold(lchip, lport, tx_threshold);
        break;

    case CTC_PORT_MAC_SGMAC:
        ret = _sys_greatbelt_port_set_sgmac_tx_threshold(lchip, lport, tx_threshold);
        break;

    default:
        ret = CTC_E_INVALID_PORT_MAC_TYPE;
    }

    if (CTC_E_NONE == ret)
    {
        p_gb_port_master[lchip]->egs_port_prop[lport].tx_threshold = tx_threshold;
    }
    PORT_UNLOCK;

    return ret;
}

int32
sys_greatbelt_port_get_tx_threshold(uint8 lchip, uint16 gport, uint8* p_tx_threshold)
{
    uint8 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port:%d tx threshold\n", gport);

    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_tx_threshold);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    *p_tx_threshold = p_gb_port_master[lchip]->egs_port_prop[lport].tx_threshold;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_maping_to_local_phy_port(uint8 lchip, uint8 lport, uint8 local_phy_port)
{
    uint32 cmd = 0;
    uint32 field_value = local_phy_port;

    cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_LocalPhyPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_lbk_port_property(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk, uint8 inter_lport, uint16 dsfwd_offset)
{
    uint8 src_lport = 0;
    uint8 enable = p_port_lbk->lbk_enable ? 1 : 0;
    uint16 src_gport = 0;
    uint32 cmd = 0;
    ds_phy_port_ext_t inter_phy_port_ext;
    ds_src_port_t inter_src_port;
    ds_dest_port_t inter_dest_port;

    CTC_PTR_VALID_CHECK(p_port_lbk);

    src_gport = p_port_lbk->src_gport;
    SYS_MAP_GPORT_TO_LPORT(src_gport, lchip, src_lport);

    sal_memset(&inter_src_port,     0, sizeof(ds_src_port_t));
    sal_memset(&inter_dest_port,    0, sizeof(ds_dest_port_t));
    sal_memset(&inter_phy_port_ext, 0, sizeof(ds_phy_port_ext_t));

    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_src_port));
    inter_src_port.port_cross_connect       = enable;
    inter_src_port.receive_disable          = (enable ? 0 : 1);
    inter_src_port.bridge_en                = 0;
    inter_src_port.stp_disable              = (enable ? 1 : 0);
    inter_src_port.add_default_vlan_disable = (enable ? 1 : 0);
    inter_src_port.route_disable            = (enable ? 1 : 0);
    inter_src_port.learning_disable         = (enable ? 1 : 0);
    cmd = DRV_IOW(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_src_port));

    /*l2pdu same to src port*/
    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_phy_port_ext));

    cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_dest_port));

    inter_phy_port_ext.ds_fwd_ptr = dsfwd_offset;
    if (enable && (p_port_lbk->src_gport == p_port_lbk->dst_gport) && p_port_lbk->lbk_mode)
    {
        inter_phy_port_ext.efm_loopback_en      = 1;
        inter_dest_port.discard_non8023_o_a_m   = 1;
    }
    else
    {
        inter_phy_port_ext.exception2_en        = 0;
        inter_phy_port_ext.exception2_discard   = 0;
        inter_phy_port_ext.efm_loopback_en      = 0;
        inter_dest_port.discard_non8023_o_a_m   = 0;
        inter_dest_port.reflective_bridge_en    = (enable ? 1 : 0);
    }

    inter_phy_port_ext.default_vlan_id = 0;
    cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_phy_port_ext));

    cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, inter_lport, cmd, &inter_dest_port));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_cross_connect(uint8 lchip, uint16 cc_gport, uint32 nhid)
{
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    bool enable = FALSE;
    uint32 fwd_offset = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(cc_gport, lchip, lport);

    /* when efm loopback enabled on src port, cc should not be enabled.*/
    if (p_gb_port_master[lchip]->igs_port_prop[lport].lbk_en)
    {
        return CTC_E_PORT_HAS_OTHER_FEATURE;
    }


    if (SYS_NH_INVALID_NHID != nhid)
    {
        CTC_ERROR_RETURN(sys_greatbelt_nh_get_dsfwd_offset(lchip, nhid, &fwd_offset));
        enable = TRUE;
    }

    field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortCrossConnect_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = enable ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

	field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RoutedPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

	field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

	field_val = enable ? 1 : 0;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LearningDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = fwd_offset;
    cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DsFwdPtr_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    p_gb_port_master[lchip]->igs_port_prop[lport].nhid = nhid;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_cross_connect(uint8 lchip, uint16 cc_gport, uint32* p_value)
{
    uint8 lport = 0;
    uint32 cmd = 0;
    ds_phy_port_ext_t ds_phy_port_ext;
    ds_src_port_t ds_src_port;

    SYS_MAP_GPORT_TO_LPORT(cc_gport, lchip, lport);

    sal_memset(&ds_phy_port_ext, 0, sizeof(ds_phy_port_ext_t));
    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port_ext));

    sal_memset(&ds_src_port, 0, sizeof(ds_src_port_t));
    cmd = DRV_IOR(DsSrcPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_src_port));

    if ((!ds_src_port.bridge_en) && ds_src_port.port_cross_connect)
    {
        *p_value = p_gb_port_master[lchip]->igs_port_prop[lport].nhid;
    }
    else
    {
        *p_value = 0xFFFFFFFF;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk)
{
    uint16 inter_gport = 0;
    uint8  inter_lport = 0;
    uint16 src_gport   = 0;
    uint8  src_lport   = 0;
    uint16 dst_gport   = 0;
    uint16 dsfwd_offset = 0;
    uint32 nhid        = 0;
    uint8  gchip       = 0;
    uint32 fwd_offset  = 0;
    int32  ret         = CTC_E_NONE;
    ctc_misc_nh_param_t nh_param;
    ctc_internal_port_assign_para_t port_assign;
    uint8 chan_id = 0;

#define RET_PROCESS_WITH_ERROR(func) ret = ret ? ret : (func)

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_port_lbk);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    src_gport = p_port_lbk->src_gport;
    dst_gport = p_port_lbk->dst_gport;
    /*not support linkagg*/
    if (CTC_IS_LINKAGG_PORT(src_gport))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MAP_GPORT_TO_LPORT(src_gport, lchip, src_lport);
    gchip = CTC_MAP_GPORT_TO_GCHIP(src_gport);
    SYS_GLOBAL_CHIPID_CHECK(gchip);
    sal_memset(&nh_param,   0, sizeof(ctc_misc_nh_param_t));
    sal_memset(&port_assign,    0, sizeof(ctc_internal_port_assign_para_t));

    CTC_ERROR_RETURN(_sys_greatbelt_port_get_cross_connect(lchip, src_gport, &nhid));

    /* cc is enabled on src port */
    if (0xFFFFFFFF != nhid)
    {
        return CTC_E_PORT_HAS_OTHER_FEATURE;
    }

    nhid = 0;

    /*get channel from port */
    chan_id = SYS_GET_CHANNEL_ID(lchip, src_gport);
    if (0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (p_port_lbk->lbk_enable)
    {
        if (p_gb_port_master[lchip]->igs_port_prop[src_lport].lbk_en)
        {
            return CTC_E_ENTRY_EXIST;
        }

        /* allocate internal port */
        port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
        port_assign.gchip       = gchip;
        if (p_port_lbk->lbk_mode)
        {
            port_assign.fwd_gport       = src_gport;
        }
        else
        {
            port_assign.fwd_gport       = dst_gport;
        }
        CTC_ERROR_RETURN(sys_greatbelt_internal_port_allocate(lchip, &port_assign));
        inter_lport = port_assign.inter_port;

        inter_gport = CTC_MAP_LPORT_TO_GPORT(gchip, inter_lport);

        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "src_gport:0x%x, dst_gport:0x%x, inter_lport= %d!\n", src_gport, dst_gport, inter_lport);
        /* map to internal port*/
        RET_PROCESS_WITH_ERROR(_sys_greatbelt_port_maping_to_local_phy_port(lchip, chan_id, inter_lport));

        if (p_port_lbk->lbk_mode)
        { /*efm loopback*/
            RET_PROCESS_WITH_ERROR(sys_greatbelt_add_port_to_channel(lchip, inter_gport, chan_id));
            RET_PROCESS_WITH_ERROR(sys_greatbelt_queue_set_port_drop_en(lchip, src_gport, TRUE, SYS_QUEUE_DROP_TYPE_PROFILE));
            /*get reflect nexthop*/
            RET_PROCESS_WITH_ERROR(sys_greatbelt_nh_get_reflective_dsfwd_offset(lchip, &dsfwd_offset));

        }
        else
        {
            /*
            if (src_gport == dst_gport)
            {
                RET_PROCESS_WITH_ERROR(sys_greatbelt_add_port_to_channel(lchip, inter_gport, src_lport));
                RET_PROCESS_WITH_ERROR(sys_greatbelt_add_port_to_channel(lchip, src_gport, SYS_DROP_CHANNEL_ID_START));
            }
            */

            if (CTC_PORT_LBK_TYPE_SWAP_MAC == p_port_lbk->lbk_type)
            {
                nh_param.type           = CTC_MISC_NH_TYPE_REPLACE_L2HDR;
                nh_param.gport          = dst_gport;
                nh_param.dsnh_offset    = 0;

                nh_param.misc_param.l2edit.is_reflective    = 0;
                nh_param.misc_param.l2edit.flag             = CTC_MISC_NH_L2_EDIT_SWAP_MAC;

                sys_greatbelt_nh_add_misc(lchip, &nhid, &nh_param, TRUE);

                sys_greatbelt_nh_get_dsfwd_offset(lchip, nhid, &fwd_offset);
                dsfwd_offset = fwd_offset;

            }
            else if (CTC_PORT_LBK_TYPE_BYPASS == p_port_lbk->lbk_type)
            {
                uint32 bypass_nhid    = 0;
                sys_greatbelt_l2_get_ucast_nh(lchip, dst_gport, CTC_NH_PARAM_BRGUC_SUB_TYPE_BYPASS, &bypass_nhid);

                sys_greatbelt_nh_get_dsfwd_offset(lchip, bypass_nhid, &fwd_offset);
                dsfwd_offset = fwd_offset;
            }
        }

        /*set lbk port property*/
        ret = sys_greatbelt_port_set_lbk_port_property(lchip, p_port_lbk, inter_lport, dsfwd_offset);

        if (CTC_E_NONE != ret)
        {
            if (p_port_lbk->lbk_mode)
            { /*resverv channel*/
                RET_PROCESS_WITH_ERROR(sys_greatbelt_remove_port_from_channel(lchip, inter_gport, chan_id));
                RET_PROCESS_WITH_ERROR(sys_greatbelt_queue_set_port_drop_en(lchip, src_gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE));
                RET_PROCESS_WITH_ERROR(sys_greatbelt_add_port_to_channel(lchip, src_gport, chan_id));
            }
            else
            { /*release nexthop*/
                if (nhid)
                {
                    sys_greatbelt_nh_remove_misc(lchip, nhid);
                }
            }

            /* Release internal port */
            port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
            port_assign.gchip       = gchip;
            port_assign.inter_port  = inter_lport;
            CTC_ERROR_RETURN(sys_greatbelt_internal_port_release(lchip, &port_assign));

            return ret;
        }

        p_port_lbk->lbk_gport = inter_gport;
        /*save the port*/
        PORT_LOCK;
        p_gb_port_master[lchip]->igs_port_prop[src_lport].inter_lport = inter_lport;
        p_gb_port_master[lchip]->igs_port_prop[src_lport].lbk_en   = 1;
        p_gb_port_master[lchip]->igs_port_prop[src_lport].nhid     = nhid;
        PORT_UNLOCK;

    }
    else
    {

        if (!p_gb_port_master[lchip]->igs_port_prop[src_lport].lbk_en)
        {
            return CTC_E_ENTRY_NOT_EXIST;
        }

        PORT_LOCK;
        p_gb_port_master[lchip]->igs_port_prop[src_lport].lbk_en = 0;
        inter_lport = p_gb_port_master[lchip]->igs_port_prop[src_lport].inter_lport;
        nhid        = p_gb_port_master[lchip]->igs_port_prop[src_lport].nhid;
        PORT_UNLOCK;

        CTC_ERROR_RETURN(_sys_greatbelt_port_maping_to_local_phy_port(lchip, chan_id, src_lport));

        inter_gport = CTC_MAP_LPORT_TO_GPORT(gchip, inter_lport);

        CTC_ERROR_RETURN(sys_greatbelt_port_set_lbk_port_property(lchip, p_port_lbk, inter_lport, 0));

        if (inter_lport)
        {

            inter_gport = CTC_MAP_LPORT_TO_GPORT(gchip, inter_lport);

            if (p_port_lbk->lbk_mode)
            {
                CTC_ERROR_RETURN(sys_greatbelt_remove_port_from_channel(lchip, inter_gport, chan_id));
                CTC_ERROR_RETURN(sys_greatbelt_queue_set_port_drop_en(lchip, src_gport, FALSE, SYS_QUEUE_DROP_TYPE_PROFILE));
                CTC_ERROR_RETURN(sys_greatbelt_add_port_to_channel(lchip, src_gport, chan_id));
            }
            else
            {
                /*
                if (src_gport == dst_gport)
                {
                    CTC_ERROR_RETURN(sys_greatbelt_remove_port_from_channel(lchip, inter_gport, src_lport));
                    CTC_ERROR_RETURN(sys_greatbelt_remove_port_from_channel(lchip, src_gport, SYS_DROP_CHANNEL_ID_START));
                    CTC_ERROR_RETURN(sys_greatbelt_add_port_to_channel(lchip, src_gport, src_lport));
                }
                */
                /*release nexthop*/
                if (nhid)
                {
                    sys_greatbelt_nh_remove_misc(lchip, nhid);
                }
            }

            /* Release internal port */
            port_assign.type        = CTC_INTERNAL_PORT_TYPE_FWD;
            port_assign.gchip       = gchip;
            port_assign.inter_port  = inter_lport;
            CTC_ERROR_RETURN(sys_greatbelt_internal_port_release(lchip, &port_assign));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_default_vlan(uint8 lchip, uint16 gport, uint32 value)
{
    uint8 lport = 0;
    ds_phy_port_ext_t phy_port_ext;
    ds_dest_port_t dest_port;
    uint32 phy_ext_cmd = 0;
    uint32 dest_cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d default vlan:%d \n", gport, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    phy_ext_cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    dest_cmd    = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, phy_ext_cmd, &phy_port_ext));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port));

    /**
    dest_port.untag_default_svlan    = 1;
    dest_port.untag_default_vlan_id  = 1;
    **/
    dest_port.default_vlan_id        = value;
    phy_port_ext.default_vlan_id     = value;

    phy_ext_cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    dest_cmd    = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, phy_ext_cmd, &phy_port_ext));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, dest_cmd, &dest_port));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_port_set_raw_packet_type(uint8 lchip, uint16 gport, uint32 value)
{
    uint8 lport = 0;
    ds_phy_port_t phy_port;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d packet type:%d \n", gport, value);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(value, CTC_PORT_RAW_PKT_IPV6);

    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));

    if (CTC_PORT_RAW_PKT_NONE == value)
    {
        phy_port.packet_type_valid = 0;
    }
    else
    {
        phy_port.packet_type_valid = 1;

        switch (value)
        {
        case CTC_PORT_RAW_PKT_ETHERNET:
            phy_port.packet_type = 0;
            break;

        case CTC_PORT_RAW_PKT_IPV4:
            phy_port.packet_type = 1;
            break;

        case CTC_PORT_RAW_PKT_IPV6:
            phy_port.packet_type = 3;
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }

    cmd = DRV_IOW(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_port_get_raw_packet_type(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8 lport = 0;
    ds_phy_port_t phy_port;
    uint32 cmd = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_PTR_VALID_CHECK(p_value);

    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port));

    if (0 == phy_port.packet_type_valid)
    {
        *p_value = CTC_PORT_RAW_PKT_NONE;
    }
    else
    {
        switch (phy_port.packet_type)
        {
        case 0:
            *p_value = CTC_PORT_RAW_PKT_ETHERNET;
            break;

        case 1:
            *p_value = CTC_PORT_RAW_PKT_IPV4;
            break;

        case 3:
            *p_value = CTC_PORT_RAW_PKT_IPV6;
            break;
        }
    }

    return CTC_E_NONE;

}

int32
_sys_greatbelt_port_set_rx_pause_type(uint8 lchip, uint16 gport, uint32 pasue_type)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;
    uint32 value = pasue_type ? 1 : 0;
    net_rx_pause_type_t net_rx_pause_type;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* rtl support max port num is 60 */
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    PORT_LOCK;
    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sal_memset(&net_rx_pause_type, 0, sizeof(net_rx_pause_type_t));
    cmd = DRV_IOR(NetRxPauseType_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_type);

    if (mac_id <= 31)
    {
        net_rx_pause_type.pause_type_low &= (~((uint32)1 << mac_id));
        net_rx_pause_type.pause_type_low |= (value << mac_id);
    }
    else
    {
        net_rx_pause_type.pause_type_hi &= (~((uint32)1 << (mac_id - 32)));
        net_rx_pause_type.pause_type_hi |= (value << (mac_id - 32));
    }

    cmd = DRV_IOW(NetRxPauseType_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_type);

    PORT_UNLOCK;

    return ret;
}

int32
_sys_greatbelt_port_get_rx_pause_type(uint8 lchip, uint16 gport, uint32* p_pasue_type)
{
    int32 ret = CTC_E_NONE;
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint32 cmd = 0;
    net_rx_pause_type_t net_rx_pause_type;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_PTR_VALID_CHECK(p_pasue_type);
    *p_pasue_type = SYS_PORT_PAUSE_FRAME_TYPE_NUM;

    /* rtl support max port num is 60 */
    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    PORT_LOCK;
    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_LOCAL_PORT;
    }

    sal_memset(&net_rx_pause_type, 0, sizeof(net_rx_pause_type_t));
    cmd = DRV_IOR(NetRxPauseType_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &net_rx_pause_type);

    if (DRV_E_NONE == ret)
    {
        if (mac_id <= 31)
        {
            *p_pasue_type = (net_rx_pause_type.pause_type_low & (~((uint32)1 << mac_id)))
                             ? SYS_PORT_PAUSE_FRAME_TYPE_PFC : SYS_PORT_PAUSE_FRAME_TYPE_NORMAL;
        }
        else
        {
            *p_pasue_type = (net_rx_pause_type.pause_type_hi & (~((uint32)1 << (mac_id - 32))))
                            ? SYS_PORT_PAUSE_FRAME_TYPE_PFC : SYS_PORT_PAUSE_FRAME_TYPE_NORMAL;
        }
    }
    PORT_UNLOCK;

    return ret;
}

int32
_sys_greatbelt_port_set_auto_neg(uint8 lchip, uint16 gport, uint8 type, uint32 value)
{
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint8 pcs_mode = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;
    uint32 mode_value= 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

    /* Cfg auto neg enable or disable*/
    if(CTC_PORT_PROP_AUTO_NEG_EN == type)
    {
        /* get pcs type */
        if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
        {
            mac_id = port_cap.mac_id;
            pcs_mode = port_cap.pcs_mode;

            if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
            {
                tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
                field_id = QuadPcsPcs0AnegCfg_Pcs0AnEnable_f;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
            else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
            {
                tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
                field_id = QsgmiiPcs0AnegCfg_Pcs0AnEnable_f;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
        }
        else
        {
            mac_id = port_cap.mac_id - SYS_MAX_GMAC_PORT_NUM;
            tbl_id = SgmacPcsAnegCfg0_t + mac_id;
            field_id = SgmacPcsAnegCfg_AnEnable_f;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        }
    }
    /* Cfg auto MODE */
    else if(CTC_PORT_PROP_AUTO_NEG_MODE == type)
    {
        /* sdk only support two mode, 1000Base-X(2'b00), SGMII-Slaver(2'b10) */
        if(CTC_PORT_AUTO_NEG_MODE_1000BASE_X == value)
        {
            mode_value = 0;
        }
        else if(CTC_PORT_AUTO_NEG_MODE_SGMII_MASTER == value)
        {
            mode_value = 1;
        }
        else if(CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == value)
        {
            mode_value = 2;
        }

        /* get pcs type */
        if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
        {
            mac_id = port_cap.mac_id;
            pcs_mode = port_cap.pcs_mode;

            if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
            {
                tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
                field_id = QuadPcsPcs0AnegCfg_Pcs0AnegMode_f;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mode_value));
            }
            else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
            {
                tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
                field_id = QsgmiiPcs0AnegCfg_Pcs0AnegMode_f;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mode_value));
            }
        }
        else
        {
            mac_id = port_cap.mac_id - SYS_MAX_GMAC_PORT_NUM;
            tbl_id = SgmacPcsAnegCfg0_t + mac_id;
            field_id = SgmacPcsAnegCfg_AnegMode_f;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &mode_value));
        }
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_get_auto_neg(uint8 lchip, uint16 gport, uint8 type, uint32* p_value)
{
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint8 pcs_mode = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

    if(CTC_PORT_PROP_AUTO_NEG_EN == type)
    {
        /* get pcs type */
        if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
        {
            mac_id = port_cap.mac_id;
            pcs_mode = port_cap.pcs_mode;

            if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
            {
                tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
                field_id = QuadPcsPcs0AnegCfg_Pcs0AnEnable_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
            else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
            {
                tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
                field_id = QsgmiiPcs0AnegCfg_Pcs0AnEnable_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
        }
        else
        {
            mac_id = port_cap.mac_id - SYS_MAX_GMAC_PORT_NUM;
            tbl_id = SgmacPcsAnegCfg0_t + mac_id;
            field_id = SgmacPcsAnegCfg_AnEnable_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        }
    }
    else if(CTC_PORT_PROP_AUTO_NEG_MODE == type)
    {
        /* get pcs type */
        if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
        {
            mac_id = port_cap.mac_id;
            pcs_mode = port_cap.pcs_mode;

            if ((DRV_SERDES_SGMII_MODE == pcs_mode) || (DRV_SERDES_2DOT5_MODE == pcs_mode))
            {
                tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
                field_id = QuadPcsPcs0AnegCfg_Pcs0AnegMode_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
            else if (DRV_SERDES_QSGMII_MODE == pcs_mode)
            {
                tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * QSGMIIPCS_QUAD_STEP + (mac_id % 4) * QSGMIIPCS_PCS_STEP;
                field_id = QsgmiiPcs0AnegCfg_Pcs0AnegMode_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            }
        }
        else
        {
            mac_id = port_cap.mac_id - SYS_MAX_GMAC_PORT_NUM;
            tbl_id = SgmacPcsAnegCfg0_t + mac_id;
            field_id = SgmacPcsAnegCfg_AnegMode_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        }
    }

    *p_value = value;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_set_link_intr(uint8 lchip, uint16 gport, bool enable)
{

    uint8 lport = 0;
    uint8 mac_id = 0;
    uint8 pcs_mode = 0;
    uint32 cmd = 0;
    uint32 tb_id = 0;
    uint32 tb_index = 0;
    uint32 value = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        mac_id = port_cap.mac_id;
         /*CTC_ERROR_RETURN(drv_greatbelt_get_gmac_info(lchip, mac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));*/
    }
    else
    {
        mac_id = port_cap.mac_id - SYS_MAX_GMAC_PORT_NUM;
         /*CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_info(lchip, mac_id, DRV_CHIP_MAC_PCS_INFO, &pcs_mode));*/
    }

    pcs_mode = port_cap.pcs_mode;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port link interrupt, gport:%d, mac_id:%d, enable:%d\n", \
                     gport, mac_id, enable);
    if (enable)
    {
        tb_index = INTR_INDEX_MASK_RESET;
    }
    else
    {
        tb_index = INTR_INDEX_MASK_SET;
    }


    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port link interrupt pcs type:%d\n", \
                     pcs_mode);

    switch (pcs_mode)
    {
        case DRV_SERDES_SGMII_MODE:

            tb_id = QuadPcsInterruptNormal0_t + mac_id/4;
            if (enable)
            {
                /* clear interrupt */
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                value = 1<<((mac_id%4)+4);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value));
            }
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            value = 1<<((mac_id%4)+4);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            break;

        case DRV_SERDES_QSGMII_MODE:

            tb_id = QsgmiiInterruptNormal0_t + mac_id/4;
            if (enable)
            {
                /* clear interrupt */
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                value = 1<<((mac_id%4)+4);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value));
            }
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            value = 1<<((mac_id%4)+4);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            break;

        case DRV_SERDES_XAUI_MODE:
        case DRV_SERDES_XGSGMII_MODE:
        case DRV_SERDES_XFI_MODE:

            tb_id = SgmacInterruptNormal0_t + mac_id;
            if (enable)
            {
                /* clear interrupt */
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                value = 0x02;
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value));
            }
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            value = 0x02;
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            break;

         case DRV_SERDES_2DOT5_MODE:
            if (mac_id < 48)
            {
                tb_id = QuadPcsInterruptNormal0_t + mac_id/4;
                if (enable)
                {
                    /* clear interrupt */
                    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                    value = 1<<((mac_id%4)+4);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value));
                }
                cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                value = 1<<((mac_id%4)+4);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
                break;
            }
            else
            {
                tb_id = SgmacInterruptNormal0_t + mac_id;
                if (enable)
                {
                    /* clear interrupt */
                    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                    value = 0x02;
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &value));
                }
                cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
                value = 0x02;
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, tb_index, cmd, &value));
            }
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

     p_gb_port_master[lchip]->igs_port_prop[lport].link_intr = (enable)?1:0;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_get_mac_ts_en(uint8 lchip, uint16 gport, uint32* enable)
{

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint8  mac_id = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    if (mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
        tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
        tbl_id = QuadMacGmac0PtpCfg0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
        cmd = DRV_IOR(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        tbl_id = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM);
        cmd = DRV_IOR(tbl_id, SgmacCfg_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

    if (field_value)
    {
        *enable = TRUE;
    }
    else
    {
        *enable = FALSE;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_set_mac_ts_en(uint8 lchip, uint16 gport, bool enable)
{

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint8 size = 0;
    bool is_enable = 0;
    uint8 mac_id = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_port_get_mac_ts_en(lchip, gport, (uint32*)&is_enable));
    if (is_enable == enable)
    {
        return CTC_E_NONE;
    }

    field_value = (enable)?1:0;

    CTC_ERROR_RETURN(_sys_greatbelt_port_get_min_frame_size(lchip, gport, &size));

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    /* 1. set mac ptp enable */
    if (mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        tbl_step1 = QuadMacGmac1PtpCfg0_t - QuadMacGmac0PtpCfg0_t;
        tbl_step2 = QuadMacGmac0PtpCfg1_t - QuadMacGmac0PtpCfg0_t;
        tbl_id = QuadMacGmac0PtpCfg0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
        cmd = DRV_IOW(tbl_id, QuadMacGmac0PtpCfg_Gmac0PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        tbl_id = SgmacCfg0_t + (mac_id-SYS_MAX_GMAC_PORT_NUM);
        cmd = DRV_IOW(tbl_id, SgmacCfg_PtpEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }

     /* 2. update mac min frame size */
     CTC_ERROR_RETURN(_sys_greatbelt_port_set_min_frame_size(lchip, gport, size));

    return CTC_E_NONE;
}


/* flag : 1-add, 0-del */
STATIC int32
_sys_greatbelt_port_mdio_scan_bit_cfg(uint8 lchip, uint8 lport, uint8 flag)
{
    uint32 cmd  =0;
    ctc_chip_phy_mapping_para_t* p_phy_mapping = NULL;
    mdio_link_down_detec_en_t link_detec;
    mdio_scan_ctl_t scan_ctl;
    uint8 phy_addr = 0;
    uint8 mdio_bus = 0;
    uint8 gchip = 0;
    uint8 mac_id = 0;
    uint16 gport = 0;
    int32 ret = CTC_E_NONE;

    sal_memset(&link_detec, 0, sizeof(mdio_link_down_detec_en_t));
    sal_memset(&scan_ctl, 0, sizeof(mdio_scan_ctl_t));

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOR(MdioScanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &scan_ctl));

    cmd = DRV_IOR(MdioLinkDownDetecEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &link_detec));

    p_phy_mapping = mem_malloc(MEM_PORT_MODULE, sizeof(ctc_chip_phy_mapping_para_t));
    if (NULL == p_phy_mapping)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_phy_mapping, 0, sizeof(ctc_chip_phy_mapping_para_t));
    CTC_ERROR_GOTO(sys_greatbelt_chip_get_phy_mapping(lchip, p_phy_mapping), ret, out);
    if (p_phy_mapping->port_mdio_mapping_tbl[lport] != 0xff)
    {
        phy_addr = p_phy_mapping->port_phy_mapping_tbl[lport];
        mdio_bus = p_phy_mapping->port_mdio_mapping_tbl[lport];

        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "phy_addr = 0x%x mdio_bus =  %d \n", phy_addr, mdio_bus);

        if (mdio_bus == 0)
        {
            if (flag)
            {
                scan_ctl.mdio1g_scan_bmp0 |= (1<<phy_addr);
            }
            else
            {
                scan_ctl.mdio1g_scan_bmp0 &= ~(1<<phy_addr);
            }
        }
        else if (mdio_bus == 1)
        {
            if (flag)
            {
                scan_ctl.mdio1g_scan_bmp1 |= (1<<phy_addr);
            }
            else
            {
                scan_ctl.mdio1g_scan_bmp1 &= ~(1<<phy_addr);
            }
        }
        else if (mdio_bus == 2)
        {
            if (flag)
            {
                scan_ctl.mdio_xg_scan_bmp0 |= (1<<phy_addr);
            }
            else
            {
                scan_ctl.mdio_xg_scan_bmp0 &= ~(1<<phy_addr);
            }
        }
        else if (mdio_bus == 3)
        {
            if (flag)
            {
                scan_ctl.mdio_xg_scan_bmp1 |= (1<<phy_addr);
            }
            else
            {
                scan_ctl.mdio_xg_scan_bmp1 &= ~(1<<phy_addr);
            }
        }
        else
        {
            ret = CTC_E_INVALID_PARAM;
            goto out;
        }

        cmd = DRV_IOW(MdioScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &scan_ctl), ret, out);

    }


    /* linkdown status transfer to aps module */
    if (mac_id < 32)
    {
        /* for mac 0~31 */
        if (flag)
        {
             link_detec.link_down_detc_en31_to0 |= (1<<mac_id);
        }
         else
        {
             link_detec.link_down_detc_en31_to0 &= ~(1<<mac_id);
        }
    }
    else if (mac_id < 60)
    {
        /* for mac 32~55 */
        if (flag)
        {
            link_detec.link_down_detc_en59_to32 |= (1<<(mac_id-32));
        }
        else
        {
            link_detec.link_down_detc_en59_to32 &= ~(1<<(mac_id-32));
        }
    }

    cmd = DRV_IOW(MdioLinkDownDetecEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &link_detec), ret, out);

out:
    if (p_phy_mapping)
    {
        mem_free(p_phy_mapping);
    }

    return ret;
}

int32
_sys_greatbelt_port_set_failover_en(uint8 lchip, uint16 gport, bool enable)
{

    uint8 lport = 0;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_ERROR_RETURN(_sys_greatbelt_port_mdio_scan_bit_cfg(lchip, lport, enable));
    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_get_failover_en(uint8 lchip, uint16 gport, uint32* enable)
{

    uint8 lport = 0;
    uint8 mac_id = 0;
    uint32 cmd  =0;
    mdio_link_down_detec_en_t link_detec;

    sal_memset(&link_detec, 0, sizeof(mdio_link_down_detec_en_t));
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(MdioLinkDownDetecEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &link_detec));
    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if(mac_id<32)
    {
        *enable = ((link_detec.link_down_detc_en31_to0) &(1 << (mac_id)))?1:0;
    }
    else if(mac_id<60)
    {
        *enable = ((link_detec.link_down_detc_en59_to32) &(1 << (mac_id-32)))?1:0;
    }

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_set_aps_failover_en(uint8 lchip, uint16 gport, bool enable)
{
    ds_aps_channel_map_t ds_aps_channel_map;
    uint8 channel_id = 0;
    uint32 cmd =0;

    uint8 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

     /*linkagg port check return.*/
    if(CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_APS_DONT_SUPPORT_HW_BASED_APS_FOR_LINKAGG;
    }

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    /*Set ds_aps_channel_map.link_change_en for scio scan down notify aps model.*/
    cmd = DRV_IOR(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));
    ds_aps_channel_map.link_change_en = enable;
    ds_aps_channel_map.local_phy_port = lport;
    cmd = DRV_IOW(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_get_aps_failover_en(uint8 lchip, uint16 gport, uint32* enable)
{
    ds_aps_channel_map_t ds_aps_channel_map;
    uint8 channel_id = 0;
    uint32 cmd =0;

    uint8 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    /*set ds_aps_channel_map.link_change_en for scio scan down notify aps model.*/
    cmd = DRV_IOR(DsApsChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &ds_aps_channel_map));
    *enable = ds_aps_channel_map.link_change_en;

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_set_linkagg_failover_en(uint8 lchip, uint16 gport, bool enable)
{

    uint8 lport = 0;
    uint8 channel_id = 0;
    uint32 cmd =0;
    ds_link_aggregate_channel_t linkagg_channel;

    SYS_PORT_INIT_CHECK(lchip);

    sal_memset(&linkagg_channel, 0, sizeof(ds_link_aggregate_channel_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*set linkagg_channel.link_change_en  for scio scan down notify linkagg model.*/
    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    linkagg_channel.group_en = 1;
    linkagg_channel.link_change_en = enable;
    cmd = DRV_IOW(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_get_linkagg_failover_en(uint8 lchip, uint16 gport, uint32* enable)
{
    ds_link_aggregate_channel_t linkagg_channel;
    uint8 channel_id = 0;
    uint32 cmd =0;

    uint8 lport = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*gport get channel id.*/
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == channel_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /*get ds_aps_channel_map.link_change_en for scio scan down notify aps model.*/
    cmd = DRV_IOR(DsLinkAggregateChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &linkagg_channel));
    *enable = linkagg_channel.link_change_en;

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_set_eee_en(uint8 lchip, uint16 gport, bool enable)
{
    uint32 cmd = 0;
    uint8 mac_id = 0;

    uint8 lport = 0;
    net_tx_e_e_e_cfg_t net_tx_e_e_e_cfg;
    sal_memset(&net_tx_e_e_e_cfg, 0, sizeof(net_tx_e_e_e_cfg_t));

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(NetTxEEECfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_e_e_e_cfg));


    if (mac_id < 32)
    {
        /* for mac 0~31 */
        if (TRUE == enable)
        {
             net_tx_e_e_e_cfg.eee_ctl_en31_to0 |= (1<<mac_id);
        }
        else
        {
             net_tx_e_e_e_cfg.eee_ctl_en31_to0  &= ~(1<<mac_id);
        }
    }
    else if (mac_id < 60)
    {
        /* for mac 32~59 */
        if (TRUE == enable)
        {
             net_tx_e_e_e_cfg.eee_ctl_en59_to32 |= (1<<(mac_id-32));
        }
         else
        {
             net_tx_e_e_e_cfg.eee_ctl_en59_to32 &= ~(1<<(mac_id-32));
        }
    }

    cmd = DRV_IOW(NetTxEEECfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_e_e_e_cfg));

    return CTC_E_NONE;
}


int32
_sys_greatbelt_port_get_eee_en(uint8 lchip, uint16 gport, uint32* enable)
{
    uint32 cmd = 0;
    uint8 mac_id = 0;

    uint8 lport = 0;
    net_tx_misc_ctl_t net_tx_misc_ctl;
    net_tx_e_e_e_cfg_t net_tx_e_e_e_cfg;
    sal_memset(&net_tx_e_e_e_cfg, 0, sizeof(net_tx_e_e_e_cfg_t));
    sal_memset(&net_tx_misc_ctl, 0, sizeof(net_tx_misc_ctl_t));

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    cmd = DRV_IOR(NetTxMiscCtl_t, NetTxMiscCtl_EeeCtlEnable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_misc_ctl));

    /* global eee is not enable */
    if (!(net_tx_misc_ctl.eee_ctl_enable))
    {
        *enable = 0;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    cmd = DRV_IOR(NetTxEEECfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &net_tx_e_e_e_cfg));


    if (mac_id < 32)
    {
        /* for mac 0~31 */
        *enable = ((net_tx_e_e_e_cfg.eee_ctl_en31_to0) &(1 << (mac_id)))?1:0;
    }
    else if (mac_id < 60)
    {
        /* for mac 32~59 */
        *enable = ((net_tx_e_e_e_cfg.eee_ctl_en59_to32) &(1 << (mac_id-32)))?1:0;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_set_unidir_en(uint8 lchip, uint16 gport, bool enable)
{
    uint8  mac_id     = 0;
    uint16 lport      = 0;
    uint32 tbl_id     = 0;
    uint32 cmd        = 0;
    drv_datapath_port_capability_t port_cap;
    uint32 value = 0;

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, lport, &port_cap));

    mac_id   = port_cap.mac_id;

    if(DRV_SERDES_XFI_MODE == port_cap.pcs_mode)
    {
        /*sgmac*/
        value = enable?1:0;
        tbl_id = SgmacXauiCfg0_t + mac_id - SYS_MAX_GMAC_PORT_NUM;
        cmd = DRV_IOW(tbl_id, SgmacXauiCfg_IgnoreLinkIntFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, SgmacXauiCfg_IgnoreLocalFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
        cmd = DRV_IOW(tbl_id, SgmacXauiCfg_IgnoreRemoteFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }
    else if((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode)
        || (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode)
        || (DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode))
    {
        /*gmac*/
        /* SGMII use sys_greatbelt_port_set_sgmii_unidir_en to set unidir*/
        return CTC_E_NOT_SUPPORT;
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_port_get_unidir_en(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint8  mac_id     = 0;
    uint16 lport      = 0;
    uint32 tbl_id     = 0;
    uint32 cmd        = 0;
    drv_datapath_port_capability_t port_cap;
    uint32 value1     = 0;
    uint32 value2     = 0;
    uint32 value3     = 0;

    if (NULL == p_gb_port_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, lport, &port_cap));

    mac_id   = port_cap.mac_id;

    if(DRV_SERDES_XFI_MODE == port_cap.pcs_mode)
    {
        /*sgmac*/
        tbl_id = SgmacXauiCfg0_t + mac_id - SYS_MAX_GMAC_PORT_NUM;
        cmd = DRV_IOR(tbl_id, SgmacXauiCfg_IgnoreLinkIntFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value1));
        cmd = DRV_IOR(tbl_id, SgmacXauiCfg_IgnoreLocalFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value2));
        cmd = DRV_IOR(tbl_id, SgmacXauiCfg_IgnoreRemoteFault_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value3));
        if (value1 && value2 && value3)
        {
            *p_value = TRUE;
        }
        else
        {
            *p_value = FALSE;
        }
    }
    else if((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode)
        || (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode)
        || (DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode))
    {
        /*gmac*/
        /* SGMII use sys_greatbelt_port_get_sgmii_unidir_en to get unidir*/
        *p_value = FALSE;
        return CTC_E_NOT_SUPPORT;
    }
    else
    {
        *p_value = FALSE;
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_set_error_check(uint8 lchip, uint16 gport, bool enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint8 mac_id = 0;
    uint8  tbl_step1 = 0;
    uint8  tbl_step2 = 0;
    uint16 tbl_id = 0;
    uint16 field_id = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port:%d crc-check:%d\n", gport, enable);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;
    switch (type)
    {
        case CTC_PORT_MAC_GMAC:
            value = enable ? 0 : 1;
            tbl_step1 = QuadMacGmac1RxCtrl0_t - QuadMacGmac0RxCtrl0_t;
            tbl_step2 = QuadMacGmac0RxCtrl1_t - QuadMacGmac0RxCtrl0_t;
            tbl_id    = QuadMacGmac0RxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
            field_id  = QuadMacGmac0RxCtrl_Gmac0CrcErrorMask_f;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            break;

        case CTC_PORT_MAC_SGMAC:
            value = enable ? 1 : 0;
            tbl_step1 = SgmacCfg1_t - SgmacCfg0_t;
            tbl_id    = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM) * tbl_step1;
            field_id  = SgmacCfg_CrcChkEnable_f;
            cmd = DRV_IOW(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            break;

        default:
            return CTC_E_INVALID_PORT_MAC_TYPE;
            break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_error_check(uint8 lchip, uint16 gport, uint32* enable)
{

    uint16 lport = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint8 mac_id = 0;
    uint8  tbl_step1 = 0;
    uint8  tbl_step2 = 0;
    uint16 tbl_id = 0;
    uint16 field_id = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;
    switch (type)
    {
        case CTC_PORT_MAC_GMAC:
            tbl_step1 = QuadMacGmac1RxCtrl0_t - QuadMacGmac0RxCtrl0_t;
            tbl_step2 = QuadMacGmac0RxCtrl1_t - QuadMacGmac0RxCtrl0_t;
            tbl_id    = QuadMacGmac0RxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
            field_id  = QuadMacGmac0RxCtrl_Gmac0CrcErrorMask_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            *enable = value?0:1;
            break;

        case CTC_PORT_MAC_SGMAC:
            tbl_step1 = SgmacCfg1_t - SgmacCfg0_t;
            tbl_id    = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM) * tbl_step1;
            field_id  = SgmacCfg_CrcChkEnable_f;
            cmd = DRV_IOR(tbl_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
            *enable = value?1:0;
            break;

        default:
            return CTC_E_INVALID_PORT_MAC_TYPE;
            break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_station_move(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value)
{
    uint32  cmd = 0;
    int32   ret = 0;
    uint32  field = 0;
    uint8   gchip = 0;

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (port_prop == CTC_PORT_PROP_STATION_MOVE_PRIORITY)
    {
        CTC_MAX_VALUE_CHECK(value, 1);
        field = value;
        CTC_ERROR_RETURN(sys_greatbelt_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, field));
    }
    else if (port_prop == CTC_PORT_PROP_STATION_MOVE_ACTION)
    {
        CTC_MAX_VALUE_CHECK(value, CTC_PORT_STATION_MOVE_ACTION_TYPE_MAX-1);
        field = (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU) ? 1 : 0;
        if (field && (gchip != CTC_MAP_GPORT_TO_GCHIP(gport)))
        {
            return CTC_E_NOT_SUPPORT;
        }

        CTC_ERROR_RETURN(sys_greatbelt_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, value));
        if (gchip == CTC_MAP_GPORT_TO_GCHIP(gport))
        {
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, CTC_MAP_GPORT_TO_LPORT(gport), cmd, &field));
        }
    }

    return ret;
}

/**
@brief   Config port's properties
*/
int32
sys_greatbelt_port_set_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32 value)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint8 value8 = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:%d, property:%d, value:%d\n", \
                     gport, port_prop, value);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    switch (port_prop)
    {
    case CTC_PORT_PROP_RAW_PKT_TYPE:
        ret = _sys_greatbelt_port_set_raw_packet_type(lchip, gport, value);
        break;

    case CTC_PORT_PROP_VLAN_DOMAIN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_OuterVlanIsCvlan_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_CvlanSpace_f);
        break;

    case CTC_PORT_PROP_PORT_EN:
        if (value)
        {
            sys_greatbelt_port_link_up_event(lchip, gport);
        }
        else
        {
            sys_greatbelt_port_link_down_event(lchip, gport);
        }
        ret = _sys_greatbelt_port_set_port_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_TRANSMIT_EN:     /**< set port whether the tranmist is enable */
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_TransmitDisable_f);
        break;

    case CTC_PORT_PROP_RECEIVE_EN:     /**< set port whether the receive is enable */
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ReceiveDisable_f);
        break;

    case CTC_PORT_PROP_BRIDGE_EN:     /**< set port whehter layer2 bridge function is enable */
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BridgeEn_f);
        break;

    case CTC_PORT_PROP_DEFAULT_VLAN:     /**< set default vlan id of packet which receive from this port */
        CTC_VLAN_RANGE_CHECK(value);
        ret = _sys_greatbelt_port_set_default_vlan(lchip, gport, value);
        break;

    case CTC_PORT_PROP_UNTAG_PVID_TYPE:
        switch(value)
        {
        case CTC_PORT_UNTAG_PVID_TYPE_NONE:
            value = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            cmd = 0; /* skip */
            break;
        case CTC_PORT_UNTAG_PVID_TYPE_SVLAN:
            value = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            value = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultSvlan_f);
            break;
        case CTC_PORT_UNTAG_PVID_TYPE_CVLAN:
            value = 1;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            value = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UntagDefaultSvlan_f);
            break;
        default:
            return CTC_E_INVALID_PARAM;

        }
        break;

    case CTC_PORT_PROP_VLAN_CTL:     /**< set port's vlan tag control mode */
        if (value >= MAX_CTC_VLANTAG_CTL)
        {
            return CTC_E_VLAN_EXCEED_MAX_VLANCTL;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_VlanTagCtl_f);
        break;

    case CTC_PORT_PROP_CROSS_CONNECT_EN:     /**< Set port cross connect */
        ret = _sys_greatbelt_port_set_cross_connect(lchip, gport, value);
        break;

    case CTC_PORT_PROP_LEARNING_EN:     /**< Set learning enable/disable on port */
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LearningDisable_f);
        break;

    case CTC_PORT_PROP_DOT1Q_TYPE:     /**< Set port dot1q type */
        CTC_MAX_VALUE_CHECK(value, CTC_DOT1Q_TYPE_BOTH);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_Dot1QEn_f);
        break;

    case CTC_PORT_PROP_USE_OUTER_TTL:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_UseOuterTtl_f);
        break;

    case CTC_PORT_PROP_PROTOCOL_VLAN_EN:     /**< set protocol vlan enable on port */
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ProtocolVlanEn_f);
        break;

    case CTC_PORT_PROP_MAC_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_greatbelt_port_set_mac_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_SPEED:
        ret = _sys_greatbelt_port_set_speed(lchip, gport, value);
        break;

    case CTC_PORT_PROP_MAX_FRAME_SIZE:
        ret = _sys_greatbelt_port_set_max_frame(lchip, gport, value);
        break;

    case CTC_PORT_PROP_PREAMBLE:
        value8 = value;
        ret = _sys_greatbelt_port_set_preamble(lchip, gport, value8);
        break;

    case CTC_PORT_PROP_ERROR_CHECK:
        ret = _sys_greatbelt_port_set_error_check(lchip, gport, value);
        break;

    case CTC_PORT_PROP_MIN_FRAME_SIZE:
        value8 = value;
        ret = _sys_greatbelt_port_set_min_frame_size(lchip, gport, value8);
        break;

    case CTC_PORT_PROP_PADING_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_greatbelt_port_set_pading_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_RX_PAUSE_TYPE:
        ret = _sys_greatbelt_port_set_rx_pause_type(lchip, gport, value);
        break;

    case CTC_PORT_PROP_SRC_DISCARD_EN:     /**< set port whether the src_discard is enable */
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_SrcDiscard_f);
        break;

    case CTC_PORT_PROP_PORT_CHECK_EN:     /**< set port whether the src port match check is enable */
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortCheckEn_f);
        break;

    case CTC_PORT_PROP_HW_LEARN_EN:     /**< Hareware learning enable*/
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_FastLearningEn_f);
        break;

    case CTC_PORT_PROP_SCL_IPV6_LOOKUP_EN:
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DisableUserIdIpv6_f);
        break;

    case CTC_PORT_PROP_SCL_IPV4_LOOKUP_EN:
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DisableUserIdIpv4_f);
        break;

    case CTC_PORT_PROP_SCL_USE_DEFAULT_LOOKUP:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultVlanLookup_f);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV6_TO_MAC:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_ForceUserIdIpv6ToMacKey_f);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV4_TO_MAC:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_ForceUserIdIpv4ToMacKey_f);
        break;

    case CTC_PORT_PROP_TRILL_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_TrillEn_f);
        break;

    case CTC_PORT_PROP_DISCARD_NON_TRIL_PKT:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_DiscardNonTrill_f);
        break;

    case CTC_PORT_PROP_DISCARD_TRIL_PKT:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_DiscardTrill_f);
        break;

    case CTC_PORT_PROP_REFLECTIVE_BRIDGE_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReflectiveBridgeEn_f);
        break;

    case CTC_PORT_PROP_FCOE_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_FcoeEn_f);
        break;

    case CTC_PORT_PROP_RPF_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_FcoeRpfEn_f);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_COS:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReplaceStagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_CTAG_COS:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReplaceCtagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_DSCP_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReplaceDscp_f);
        break;

    case CTC_PORT_PROP_L3PDU_ARP_ACTION:
        if (value >= CTC_PORT_ARP_ACTION_TYPE_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ArpExceptionType_f);
        break;

    case CTC_PORT_PROP_L3PDU_DHCP_ACTION:
        if (value >= CTC_PORT_DHCP_ACTION_TYPE_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_DhcpExceptionType_f);
        break;

    case CTC_PORT_PROP_TUNNEL_RPF_CHECK:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_TunnelRpfCheck_f);
        break;

    case CTC_PORT_PROP_PTP_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PtpEn_f);
        break;

    case CTC_PORT_PROP_RPF_TYPE:
        if (value >= CTC_PORT_RPF_TYPE_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RpfType_f);
        break;

    case CTC_PORT_PROP_IS_LEAF:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_IsLeaf_f);
        break;

    case CTC_PORT_PROP_PKT_TAG_HIGH_PRIORITY:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_IngressTagHighPriority_f);
        break;

    case CTC_PORT_PROP_ROUTE_EN:
        value = (value) ? 0 : 1;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_RouteDisable_f);
        break;

    case CTC_PORT_PROP_PRIORITY_TAG_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_PriorityTagEn_f);
        break;

    case CTC_PORT_PROP_IPG:
        if (value >= CTC_IPG_SIZE_MAX)
        {
            return CTC_E_VLAN_EXCEED_MAX_VLANCTL;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_IpgIndex_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_IpgIndex_f);
        break;

    case CTC_PORT_PROP_DEFAULT_PCP:
        CTC_MAX_VALUE_CHECK(value, 7);
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultPcp_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_DefaultPcp_f);
        break;

    case CTC_PORT_PROP_DEFAULT_DEI:
        CTC_MAX_VALUE_CHECK(value, 1);
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultDei_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        break;

    case CTC_PORT_PROP_QOS_POLICY:
        if (value >= CTC_QOS_TRUST_MAX)
        {
            return CTC_E_INVALID_PARAM;
        }

        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_QosPolicy_f);
        break;

    case CTC_PORT_PROP_ADD_DEFAULT_VLAN_DIS:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
        break;

    case CTC_PORT_PROP_LOGIC_PORT:
        if (value == 0xFFFF)
        {
            value  = 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultLogicSrcPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
            value = 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultLogicSrcPort_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &value);
            value  = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LogicDestPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        }
        else
        {
            CTC_MAX_VALUE_CHECK(value, 0x3FFF);

            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultLogicSrcPort_f);
            ret = DRV_IOCTL(lchip, lport, cmd, &value);

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LogicDestPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);

            value  = 1;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_UseDefaultLogicSrcPort_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        }
        break;

    case CTC_PORT_PROP_AUTO_NEG_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_greatbelt_port_set_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, value);
        break;

    case CTC_PORT_PROP_AUTO_NEG_MODE:
        ret = _sys_greatbelt_port_set_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, value);
        break;

    case CTC_PORT_PROP_LINK_INTRRUPT_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_greatbelt_port_set_link_intr(lchip, gport, value);
        break;

    case CTC_PORT_PROP_MAC_TS_EN:
        value = (value) ? TRUE : FALSE;
        ret = _sys_greatbelt_port_set_mac_ts_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_LINKSCAN_EN:
        value = (value) ? 1 : 0;
        ret = _sys_greatbelt_port_set_failover_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_APS_FAILOVER_EN:
        value = (value) ? 1 : 0;
        ret = _sys_greatbelt_port_set_aps_failover_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_LINKAGG_FAILOVER_EN:
        value = (value) ? 1 : 0;
        ret = _sys_greatbelt_port_set_linkagg_failover_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_EEE_EN:
        value = (value) ? 1 : 0;
        ret = _sys_greatbelt_port_set_eee_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_UNIDIR_EN:
        value = (value) ? 1 : 0;
        ret = _sys_greatbelt_port_set_unidir_en(lchip, gport, value);
        break;

    case CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        break;

    case CTC_PORT_PROP_STATION_MOVE_PRIORITY:
        CTC_MAX_VALUE_CHECK(value, 1);
        ret = sys_greatbelt_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, value);
        break;

    case CTC_PORT_PROP_STATION_MOVE_ACTION:
        CTC_MAX_VALUE_CHECK(value, CTC_PORT_STATION_MOVE_ACTION_TYPE_MAX-1);
        ret = sys_greatbelt_l2_set_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, value);
        value = (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        break;

    case CTC_PORT_PROP_MAC_TX_IPG:
        ret = _sys_greatbelt_port_set_mac_ipg(lchip, gport, value);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    if (cmd == 0)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    return CTC_E_NONE;
}

/**
@brief    Get port's properties according to gport id
*/
int32
sys_greatbelt_port_get_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32* p_value)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint8 value8 = 0;
    uint8 inverse_value = 0;
    uint32 value = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_value);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port property, gport:%d, property:%d!\n", gport, port_prop);

    /*do read*/
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    switch (port_prop)
    {
    case CTC_PORT_PROP_RAW_PKT_TYPE:
        ret = _sys_greatbelt_port_get_raw_packet_type(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_VLAN_DOMAIN:
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_OuterVlanIsCvlan_f);
        break;

    case CTC_PORT_PROP_PORT_EN:
        ret = _sys_greatbelt_port_get_port_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_TRANSMIT_EN:     /**< get port whether the tranmist is enable */
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_TransmitDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_RECEIVE_EN:     /**< get port whether the receive is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ReceiveDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_BRIDGE_EN:     /**< get port whehter layer2 bridge function is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_BridgeEn_f);
        break;

    case CTC_PORT_PROP_DEFAULT_VLAN:     /**< Get default vlan id on the Port */
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DefaultVlanId_f);
        break;

    case CTC_PORT_PROP_UNTAG_PVID_TYPE:
        {
            uint32 r_value = 0;
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_UntagDefaultVlanId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &r_value));
            if (!r_value) /* none */
            {
                *p_value = CTC_PORT_UNTAG_PVID_TYPE_NONE;
            }
            else /* s or c */
            {
                cmd = DRV_IOR(DsDestPort_t, DsDestPort_UntagDefaultSvlan_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &r_value));
                if (r_value) /* s */
                {
                    *p_value = CTC_PORT_UNTAG_PVID_TYPE_SVLAN;
                }
                else
                {
                    *p_value = CTC_PORT_UNTAG_PVID_TYPE_CVLAN;
                }
            }

            return CTC_E_NONE;

        }
        break;

    case CTC_PORT_PROP_VLAN_CTL:     /**< Get port's vlan tag control mode */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_VlanTagCtl_f);
        break;

    case CTC_PORT_PROP_CROSS_CONNECT_EN:     /**< Get port cross connect */
        ret = _sys_greatbelt_port_get_cross_connect(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LEARNING_EN:     /**< Get learning enable/disable on port */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LearningDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
        *p_value = *p_value ? FALSE : TRUE;
        return CTC_E_NONE;

    case CTC_PORT_PROP_DOT1Q_TYPE:     /**< Get port dot1q type */
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_Dot1QEn_f);
        break;

    case CTC_PORT_PROP_USE_OUTER_TTL:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_UseOuterTtl_f);
        break;

    case CTC_PORT_PROP_PROTOCOL_VLAN_EN:     /**< Get protocol vlan enable on port */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ProtocolVlanEn_f);
        break;

    case CTC_PORT_PROP_MAC_EN:
        ret = sys_greatbelt_port_get_mac_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_SPEED:
        ret = _sys_greatbelt_port_get_port_speed(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_MAX_FRAME_SIZE:
        ret = _sys_greatbelt_port_get_max_frame(lchip, gport, (uint32*)p_value);
        break;

    case CTC_PORT_PROP_PREAMBLE:
        ret = _sys_greatbelt_port_get_preamble(lchip, gport, &value8);
        *p_value = value8;
        break;

    case CTC_PORT_PROP_MIN_FRAME_SIZE:
        ret = _sys_greatbelt_port_get_min_frame_size(lchip, gport, &value8);
        *p_value = value8;
        break;

    case CTC_PORT_PROP_ERROR_CHECK:
        ret = _sys_greatbelt_port_get_error_check(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_PADING_EN:
        if (lport >= SYS_GB_MAX_PHY_PORT)
        {
            return CTC_E_INVALID_LOCAL_PORT;
        }

        *p_value = p_gb_port_master[lchip]->egs_port_prop[lport].pading_en ? TRUE : FALSE;
        break;

    case CTC_PORT_PROP_RX_PAUSE_TYPE:
        ret = _sys_greatbelt_port_get_rx_pause_type(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_SRC_DISCARD_EN:     /**< set port whether the src_discard is enable */
        cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_SrcDiscard_f);
        break;

    case CTC_PORT_PROP_PORT_CHECK_EN:     /**< set port whether the src port match check is enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortCheckEn_f);
        break;

    /*new properties for GB*/
    case CTC_PORT_PROP_HW_LEARN_EN:     /**< hardware learning enable */
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_FastLearningEn_f);
        break;

    case CTC_PORT_PROP_SCL_IPV6_LOOKUP_EN:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DisableUserIdIpv6_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_SCL_IPV4_LOOKUP_EN:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DisableUserIdIpv4_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_SCL_USE_DEFAULT_LOOKUP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_UseDefaultVlanLookup_f);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV6_TO_MAC:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_ForceUserIdIpv6ToMacKey_f);
        break;

    case CTC_PORT_PROP_SCL_FORCE_IPV4_TO_MAC:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_ForceUserIdIpv4ToMacKey_f);
        break;

    case CTC_PORT_PROP_TRILL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_TrillEn_f);
        break;

    case CTC_PORT_PROP_DISCARD_NON_TRIL_PKT:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_DiscardNonTrill_f);
        break;

    case CTC_PORT_PROP_DISCARD_TRIL_PKT:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_DiscardTrill_f);
        break;

    case CTC_PORT_PROP_REFLECTIVE_BRIDGE_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReflectiveBridgeEn_f);
        break;

    case CTC_PORT_PROP_FCOE_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_FcoeEn_f);
        break;

    case CTC_PORT_PROP_RPF_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_FcoeRpfEn_f);
        break;

    case CTC_PORT_PROP_REPLACE_STAG_COS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReplaceStagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_CTAG_COS:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReplaceCtagCos_f);
        break;

    case CTC_PORT_PROP_REPLACE_DSCP_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReplaceDscp_f);
        break;

    case CTC_PORT_PROP_L3PDU_ARP_ACTION:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ArpExceptionType_f);
        break;

    case CTC_PORT_PROP_L3PDU_DHCP_ACTION:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_DhcpExceptionType_f);
        break;

    case CTC_PORT_PROP_TUNNEL_RPF_CHECK:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_TunnelRpfCheck_f);
        break;

    case CTC_PORT_PROP_PTP_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PtpEn_f);
        break;

    case CTC_PORT_PROP_RPF_TYPE:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_RpfType_f);
        break;

    case CTC_PORT_PROP_IS_LEAF:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_IsLeaf_f);
        break;

    case CTC_PORT_PROP_PKT_TAG_HIGH_PRIORITY:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_IngressTagHighPriority_f);
        break;

    case CTC_PORT_PROP_ROUTE_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_RouteDisable_f);
        inverse_value = 1;
        break;

    case CTC_PORT_PROP_PRIORITY_TAG_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_PriorityTagEn_f);
        break;

    case CTC_PORT_PROP_IPG:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_IpgIndex_f);
        break;

    case CTC_PORT_PROP_DEFAULT_PCP:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DefaultPcp_f);
        break;

    case CTC_PORT_PROP_DEFAULT_DEI:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DefaultDei_f);
        break;

    case CTC_PORT_PROP_QOS_POLICY:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_QosPolicy_f);
        break;

    case CTC_PORT_PROP_ADD_DEFAULT_VLAN_DIS:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_AddDefaultVlanDisable_f);
        break;

    case CTC_PORT_PROP_LOGIC_PORT:
         cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DefaultLogicSrcPort_f);
         break;

    case CTC_PORT_PROP_AUTO_NEG_EN:
        ret = _sys_greatbelt_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, p_value);
        break;

	case CTC_PORT_PROP_AUTO_NEG_MODE:
        ret = _sys_greatbelt_port_get_auto_neg(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, p_value);
        break;

    case CTC_PORT_PROP_LINK_INTRRUPT_EN:
        *p_value = p_gb_port_master[lchip]->igs_port_prop[lport].link_intr? TRUE : FALSE;
        break;

    case CTC_PORT_PROP_MAC_TS_EN:
        ret = _sys_greatbelt_port_get_mac_ts_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINKSCAN_EN:
        ret = _sys_greatbelt_port_get_failover_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_APS_FAILOVER_EN:
        ret = _sys_greatbelt_port_get_aps_failover_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINKAGG_FAILOVER_EN:
        ret = _sys_greatbelt_port_get_linkagg_failover_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_EEE_EN:
        ret = _sys_greatbelt_port_get_eee_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_UNIDIR_EN:
        ret = _sys_greatbelt_port_get_unidir_en(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        break;

    case CTC_PORT_PROP_STATION_MOVE_PRIORITY:
        ret = sys_greatbelt_l2_get_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_PRIORITY, p_value);
        break;

    case CTC_PORT_PROP_STATION_MOVE_ACTION:
        CTC_ERROR_RETURN(sys_greatbelt_l2_get_station_move(lchip, gport, SYS_L2_STATION_MOVE_OP_ACTION, &value));

        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

        if (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD)
        {
            *p_value = CTC_PORT_STATION_MOVE_ACTION_TYPE_FWD;
        }
        else if (value == CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD)
        {
            *p_value = *p_value ? CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD_TOCPU : CTC_PORT_STATION_MOVE_ACTION_TYPE_DISCARD;
        }

        return CTC_E_NONE;

    case CTC_PORT_PROP_MAC_TX_IPG:
        ret = _sys_greatbelt_port_get_mac_ipg(lchip, gport, p_value);
        break;

    case CTC_PORT_PROP_LINK_UP:
        ret = sys_greatbelt_port_get_mac_link_up(lchip, gport, p_value, 1);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    if (cmd == 0)
    {
        /*when cmd == 0, get value from  software memory*/
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

    *p_value = inverse_value ? (!(*p_value)) : (*p_value);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_reset_restriction(uint8 lchip, uint16 gport)
{
    uint32 value = 0;
    uint32 cmd = 0;

    uint8 lport = 0;

    value = 0;
    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type = SYS_PORT_RESTRICTION_NONE;
    return CTC_E_NONE;
}

STATIC uint32
_sys_greatbelt_port_get_flood_discard_type(uint8 lchip, uint16 lport)
{
    uint32 value = 0;
    uint32 cmd = 0;
    cmd = DRV_IOR(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    return value;
}

int32
_sys_greatbelt_port_check_restriction_mode(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{
    uint32 cmd = 0;
    uint32 value = 0;
    uint16 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:0x%04X\n", gport);

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    p_restriction->mode = CTC_PORT_RESTRICTION_DISABLE;

    if (CTC_INGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else if (CTC_EGRESS == p_restriction->dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    if (0 != value)
    {
        if (0x30 == (value & 0x38))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
        }
        else if  (0x38 == (value & 0x38))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
        }
        else if (0x20 == (value & 0x30))
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PVLAN;
        }
        else
        {
            p_restriction->mode = CTC_PORT_RESTRICTION_PORT_ISOLATION;
        }
    }
    return CTC_E_NONE;
}


/**
@brief    Set port's restriction
*/
int32
sys_greatbelt_port_set_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint8 pvlan_type = 0;
    uint32 value = 0;
    uint32 ingress_isolated_id = 0;
    uint32 egress_isolated_id = 0;
    uint32 cmd = 0;
    ctc_port_restriction_t restriction;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:%d, mode:%d, isolated_id:%d, dir:%d\n", \
                     gport, p_restriction->mode, p_restriction->isolated_id, p_restriction->dir);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    sal_memset(&restriction, 0, sizeof(ctc_port_restriction_t));

    if (p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type != CTC_PORT_RESTRICTION_DISABLE)
    {
        if ((CTC_PORT_RESTRICTION_PORT_BLOCKING == p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type)
            && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode))
        {
            if ((CTC_INGRESS != p_restriction->dir) && (CTC_PORT_ISOLATION_ALL != p_restriction->type))
            {
                return CTC_E_PORT_HAS_OTHER_RESTRICTION;
            }
        }
        else if ((CTC_PORT_RESTRICTION_PORT_ISOLATION == p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type)
            && (CTC_PORT_RESTRICTION_PORT_BLOCKING == p_restriction->mode))
        {
            if (1 == _sys_greatbelt_port_get_flood_discard_type(lchip, lport))
            {
                return CTC_E_PORT_HAS_OTHER_RESTRICTION;
            }
        }
        else if ((p_restriction->mode != p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type)
            && (p_restriction->mode != CTC_PORT_RESTRICTION_DISABLE))
        {
            return CTC_E_PORT_HAS_OTHER_RESTRICTION;
        }
    }

    switch (p_restriction->mode)
    {
    case CTC_PORT_RESTRICTION_PVLAN:

        if (CTC_PORT_PVLAN_PROMISCUOUS == p_restriction->type)
        {
            value = 0x30;
            pvlan_type = CTC_PORT_PVLAN_PROMISCUOUS;
        }
        else if (CTC_PORT_PVLAN_ISOLATED == p_restriction->type)
        {
            value = 0x38;
            pvlan_type = CTC_PORT_PVLAN_ISOLATED;
        }
        else if (CTC_PORT_PVLAN_COMMUNITY == p_restriction->type)
        {
            value = p_restriction->isolated_id;
            CTC_MAX_VALUE_CHECK(value, SYS_MAX_PVLAN_COMMUNITY_ID_NUM);
            value = value | 0x20;
            pvlan_type = CTC_PORT_PVLAN_COMMUNITY;
        }
        else if (CTC_PORT_PVLAN_NONE == p_restriction->type)
        {
            value = 0;
            pvlan_type = CTC_PORT_PVLAN_NONE;
            CTC_ERROR_RETURN(_sys_greatbelt_port_reset_restriction(lchip, gport));
            p_gb_port_master[lchip]->igs_port_prop[lport].pvlan_type = pvlan_type;
            p_gb_port_master[lchip]->egs_port_prop[lport].pvlan_type = pvlan_type;
            return CTC_E_NONE;
        }
        else
        {
            return CTC_E_INVALID_PARAM;
        }

        p_gb_port_master[lchip]->igs_port_prop[lport].pvlan_type = pvlan_type;
        p_gb_port_master[lchip]->egs_port_prop[lport].pvlan_type = pvlan_type;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

        value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
        ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);

        p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type = SYS_PORT_RESTRICTION_PVLAN;
        break;

    case CTC_PORT_RESTRICTION_PORT_ISOLATION:

        if (CTC_BOTH_DIRECTION < p_restriction->dir)
        {
            return CTC_E_INVALID_PARAM;
        }

        if (((CTC_EGRESS == p_restriction->dir) || (CTC_BOTH_DIRECTION == p_restriction->dir))
            && ((p_restriction->type != 0)
            && (!CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_UCAST)
            && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_MCAST)
            && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_KNOW_MCAST)
            && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_BCAST))))
        {
            return CTC_E_INVALID_PARAM;
        }

        value = p_restriction->isolated_id;
        CTC_MAX_VALUE_CHECK(value, SYS_MAX_ISOLUTION_ID_NUM);

        if ((CTC_INGRESS == p_restriction->dir) || (CTC_BOTH_DIRECTION == p_restriction->dir))
        {
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        }

        if ((CTC_EGRESS == p_restriction->dir) || (CTC_BOTH_DIRECTION == p_restriction->dir))
        {
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            if (p_restriction->type == CTC_PORT_ISOLATION_ALL)
            {
                value = (p_restriction->type == CTC_PORT_ISOLATION_ALL) ? 0 : 1;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            }
            else
            {
                value = (p_restriction->type == CTC_PORT_ISOLATION_ALL) ? 0 : 1;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                value = (p_restriction->type & CTC_PORT_ISOLATION_UNKNOW_UCAST) ? 1 : 0;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                value = (p_restriction->type & CTC_PORT_ISOLATION_UNKNOW_MCAST) ? 1 : 0;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                value = (p_restriction->type & CTC_PORT_ISOLATION_KNOW_MCAST) ? 1 : 0;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                value = (p_restriction->type & CTC_PORT_ISOLATION_BCAST) ? 1 : 0;
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            }
        }

        p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type = SYS_PORT_RESTRICTION_ISOLATION;

        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ingress_isolated_id));
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &egress_isolated_id));
        if ((ingress_isolated_id == 0) && (egress_isolated_id == 0))
        {
            CTC_ERROR_RETURN(_sys_greatbelt_port_reset_restriction(lchip, gport));
            return CTC_E_NONE;
        }
        else if (egress_isolated_id == 0)
        {
            value = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
            ret = ret ? ret : DRV_IOCTL(lchip, lport, cmd, &value);
        }

        break;

    case CTC_PORT_RESTRICTION_PORT_BLOCKING:

        if ((p_restriction->type != 0)
           && (!CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_UCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_MCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_KNOW_UCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_KNOW_MCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_BCAST)))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (!CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_UCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_UNKNOW_MCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_KNOW_MCAST)
           && !CTC_FLAG_ISSET(p_restriction->type, CTC_PORT_BLOCKING_BCAST))
        {
            value = 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            return CTC_E_NONE;
        }

        value = 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

        value = (p_restriction->type & CTC_PORT_BLOCKING_UNKNOW_UCAST) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        value = (p_restriction->type & CTC_PORT_BLOCKING_UNKNOW_MCAST) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        value = (p_restriction->type & CTC_PORT_BLOCKING_KNOW_MCAST) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        value = (p_restriction->type & CTC_PORT_BLOCKING_BCAST) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

        p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type = SYS_PORT_RESTRICTION_BLOCKING;
        break;

    case CTC_PORT_RESTRICTION_DISABLE:

        CTC_ERROR_RETURN(_sys_greatbelt_port_reset_restriction(lchip, gport));

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return ret;

}

/**
@brief    Get port's restriction
*/
int32
sys_greatbelt_port_get_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction)
{
    uint8 lport = 0;
    uint32 value = 0;
    uint32 value_igs = 0;
    uint32 value_egs = 0;
    uint32 cmd = 0;
    ctc_port_restriction_t restriction;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_restriction);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property, gport:%d\n", gport);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    sal_memset(&restriction, 0, sizeof(ctc_port_restriction_t));

    if ((CTC_PORT_RESTRICTION_PORT_ISOLATION == p_gb_port_master[lchip]->egs_port_prop[lport].restriction_type)
        && (CTC_PORT_RESTRICTION_PORT_BLOCKING == p_restriction->mode))
    {
        if (1 == _sys_greatbelt_port_get_flood_discard_type(lchip, lport))
        {
            p_restriction->type = 0;
            return CTC_E_NONE;
        }
    }

    restriction.dir = CTC_INGRESS;
    CTC_ERROR_RETURN(_sys_greatbelt_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode)))
    {
        p_restriction->isolated_id = 0;
        return CTC_E_NONE;
    }
    else if (((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        p_restriction->type = CTC_PORT_PVLAN_NONE;
        return CTC_E_NONE;
    }

    restriction.dir = CTC_EGRESS;
    CTC_ERROR_RETURN(_sys_greatbelt_port_check_restriction_mode(lchip, gport, &restriction));
    if (((CTC_PORT_RESTRICTION_PVLAN == restriction.mode) && (CTC_PORT_RESTRICTION_PORT_ISOLATION == p_restriction->mode)))
    {
        p_restriction->isolated_id = 0;
        return CTC_E_NONE;
    }
    else if (((CTC_PORT_RESTRICTION_PORT_ISOLATION == restriction.mode) && (CTC_PORT_RESTRICTION_PVLAN == p_restriction->mode)))
    {
        p_restriction->type = CTC_PORT_PVLAN_NONE;
        return CTC_E_NONE;
    }

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_SrcPortIsolateId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_igs));
    cmd = DRV_IOR(DsDestPort_t, DsDestPort_DestPortIsolateId_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_egs));
    if (CTC_INGRESS == p_restriction->dir)
    {
        value = value_igs;
    }
    else if (CTC_EGRESS == p_restriction->dir)
    {
        value = value_egs;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    switch (p_restriction->mode)
    {
        case CTC_PORT_RESTRICTION_DISABLE:
            break;
        case CTC_PORT_RESTRICTION_PVLAN:
            if (0x30 == (value & 0x38))
            {
                p_restriction->type = CTC_PORT_PVLAN_PROMISCUOUS;
            }
            else if  (0x38 == (value & 0x38))
            {
                p_restriction->type = CTC_PORT_PVLAN_ISOLATED;
            }
            else if (0x20 == (value & 0x30))
            {
                p_restriction->type = CTC_PORT_PVLAN_COMMUNITY;
                p_restriction->isolated_id = value & (~((uint32)0x30));
            }
            break;
        case CTC_PORT_RESTRICTION_PORT_BLOCKING:
            p_restriction->type = 0;
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_UNKNOW_UCAST : p_restriction->type;

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_UNKNOW_MCAST : p_restriction->type;

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_KNOW_MCAST : p_restriction->type;

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
            p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_BCAST : p_restriction->type;
            break;
        case CTC_PORT_RESTRICTION_PORT_ISOLATION:
            if (p_restriction->dir >= CTC_BOTH_DIRECTION)
            {
                return CTC_E_INVALID_PARAM;
            }
            if (CTC_INGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
                p_restriction->type = 0;
            }
            else if (CTC_EGRESS == p_restriction->dir)
            {
                p_restriction->isolated_id = value;
                cmd = DRV_IOR(DsDestPort_t, DsDestPort_FloodingDiscardType_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                p_restriction->type = (value == 0) ? CTC_PORT_ISOLATION_ALL : 0xF;
                if (p_restriction->type == 0xF)
                {
                    p_restriction->type = 0;
                    cmd = DRV_IOR(DsDestPort_t, DsDestPort_UcastFloodingDisable_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                    p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_UNKNOW_UCAST : p_restriction->type;

                    cmd = DRV_IOR(DsDestPort_t, DsDestPort_McastFloodingDisable_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                    p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_UNKNOW_MCAST : p_restriction->type;

                    cmd = DRV_IOR(DsDestPort_t, DsDestPort_KnownMcastFloodingDisable_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                    p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_KNOW_MCAST : p_restriction->type;

                    cmd = DRV_IOR(DsDestPort_t, DsDestPort_BcastFloodingDisable_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
                    p_restriction->type = (value) ? p_restriction->type | CTC_PORT_ISOLATION_BCAST : p_restriction->type;
                }
            }
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_port_set_random_log_percent(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32 percent)
{
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint32 value = 0;
    uint32 cmd = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port gport:%d, direction:%d, percent:%d\n", \
                     gport, dir, percent);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_MAX_VALUE_CHECK(percent, 100);
    value = (SYS_MAX_RANDOM_LOG_THRESHOD * percent) / 100;

    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_RandomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        if (CTC_E_NONE == ret)
        {
            PORT_LOCK;
            p_gb_port_master[lchip]->igs_port_prop[lport].random_log_percent = percent;
            PORT_UNLOCK;
        }
    }
    else if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_RandomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &value);
        if (CTC_E_NONE == ret)
        {
            PORT_LOCK;
            p_gb_port_master[lchip]->egs_port_prop[lport].random_log_percent = percent;
            PORT_UNLOCK;
        }
    }

    return ret;
}

STATIC int32
_sys_greatbelt_port_set_random_log_rate(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32 rate)
{
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint32 cmd = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port gport:%d, direction:%d, rate:%d\n", \
                     gport, dir, rate);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_MAX_VALUE_CHECK(rate, SYS_MAX_RANDOM_LOG_THRESHOD);

    if (CTC_INGRESS == dir)
    {
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_RandomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &rate);
        if (CTC_E_NONE == ret)
        {
            PORT_LOCK;
            p_gb_port_master[lchip]->igs_port_prop[lport].random_log_percent = (rate*100)/SYS_MAX_RANDOM_LOG_THRESHOD;
            PORT_UNLOCK;
        }
    }
    else if (CTC_EGRESS == dir)
    {
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_RandomThreshold_f);
        ret = DRV_IOCTL(lchip, lport, cmd, &rate);
        if (CTC_E_NONE == ret)
        {
            PORT_LOCK;
            p_gb_port_master[lchip]->egs_port_prop[lport].random_log_percent = (rate*100)/SYS_MAX_RANDOM_LOG_THRESHOD;
            PORT_UNLOCK;
        }
    }

    return ret;
}


/**
@brief  Set port's internal properties with direction according to gport id
*/
int32
sys_greatbelt_port_set_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32 value)
{
    int32 ret = CTC_E_NONE;

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 acl_en = 0;
    uint32 value_t = 0;
    uint32 valid = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port property with direction, gport:%d, property:%d, dir:%d,\
                                              value:%d\n", gport, port_prop, dir, value);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*do write*/
    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        value_t = value;
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            if (value > 3 && (0xff != value))
            {
                return CTC_E_INVALID_PARAM;
            }
            cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_SvlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            value_t = (value_t) ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_IngressFilteringEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            value_t = (value_t) ? 1 : 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_RandomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_greatbelt_port_set_random_log_percent(lchip, gport, CTC_INGRESS, value_t);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            ret = _sys_greatbelt_port_set_random_log_rate(lchip, gport, CTC_INGRESS, value_t);
            break;


/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            CTC_MAX_VALUE_CHECK(value_t, CTC_ACL_EN_0 + CTC_ACL_EN_1 + CTC_ACL_EN_2 + CTC_ACL_EN_3);

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_0);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2Ipv6AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_1);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2Ipv6AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_2);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclEn2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_3);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclEn3_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            if (0 == value_t)     /* if acl dis, set l2 label = 0 */
            {
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclLabel_f);
                p_gb_port_master[lchip]->igs_port_prop[lport].acl_en = 0;
            }
            else
            {
                cmd = 0;
                p_gb_port_master[lchip]->igs_port_prop[lport].acl_en = 1;
            }

            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
            if (p_gb_port_master[lchip]->igs_port_prop[lport].acl_en)
            {
                SYS_ACL_PORT_CLASS_ID_CHECK(value_t);
                cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_L2AclLabel_f);
            }
            else
            {
                return CTC_E_PORT_ACL_EN_FIRST;
            }

            break;

        case CTC_PORT_DIR_PROP_ACL_IPV4_FORCE_MAC:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ForceAclQosIpv4toMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV6_FORCE_MAC:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ForceAclQosIpv6toMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_FORCE_USE_IPV6:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ForceIpv6Key_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MacKeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_Ipv4KeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_Ipv6KeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MplsKeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ServiceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            CTC_MAX_VALUE_CHECK(value, SYS_GB_MAX_ACL_PORT_BITMAP_NUM -1);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_AclPortNum_f);
            break;

/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            CTC_MAX_VALUE_CHECK(value_t, 7);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_QosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortPolicerValid_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));
        }
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        value_t = value;
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            if (value > 3 && (0xff != value))
            {
                return CTC_E_INVALID_PARAM;
            }
            valid = (0xff == value) ? 0 : 1;
            value_t = (0xff == value) ? 0 : value;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_SvlanTpidValid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &valid));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_SvlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            value_t = (value_t) ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_EgressFilteringEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            value_t = (value_t) ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_RandomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            ret = _sys_greatbelt_port_set_random_log_percent(lchip, gport, CTC_EGRESS, value_t);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            ret = _sys_greatbelt_port_set_random_log_rate(lchip, gport, CTC_EGRESS, value_t);
            break;


/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            CTC_MAX_VALUE_CHECK(value_t, CTC_ACL_EN_0 + CTC_ACL_EN_1 + CTC_ACL_EN_2 + CTC_ACL_EN_3);

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_0);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2Ipv6AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_1);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2Ipv6AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_2);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclEn2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            acl_en = CTC_FLAG_ISSET(value_t, CTC_ACL_EN_3);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclEn3_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));

            if (0 == value_t)     /* if acl dis, set l2 label = 0 */
            {
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclLabel_f);
                p_gb_port_master[lchip]->egs_port_prop[lport].acl_en = 0;
            }
            else
            {
                cmd = 0;
                p_gb_port_master[lchip]->egs_port_prop[lport].acl_en = 1;
            }

            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:

            if (p_gb_port_master[lchip]->egs_port_prop[lport].acl_en)
            {
                SYS_ACL_PORT_CLASS_ID_CHECK(value_t);
                cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2AclLabel_f);
            }
            else
            {
                return CTC_E_PORT_ACL_EN_FIRST;
            }

            break;

        case CTC_PORT_DIR_PROP_ACL_IPV4_FORCE_MAC:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_ForceIpv4ToMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV6_FORCE_MAC:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_ForceIpv6ToMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_FORCE_USE_IPV6:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_ForceIpv6Key_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MacKeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_Ipv4KeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_Ipv6KeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MplsKeyUseLabel_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_ServiceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            CTC_MAX_VALUE_CHECK(value, SYS_GB_MAX_ACL_PORT_BITMAP_NUM -1);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_AclPortNum_f);
            break;
/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            CTC_MAX_VALUE_CHECK(value_t, 7);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_QosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            value_t = value_t ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortPolicerValid_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value_t));
        }
    }

    CTC_ERROR_RETURN(ret);

    return CTC_E_NONE;

}

/**
@brief  Get port's properties with direction according to gport id
*/
int32
sys_greatbelt_port_get_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value)
{

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 acl_en = 0;
    int32 ret = CTC_E_NONE;

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(p_value);
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port property with direction, gport:%d, property:%d, dir:%d" \
                     , gport, port_prop, dir);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    /*do read,only get value by CTC_INGRESS or CTC_EGRESS,no CTC_BOTH_DIRECTION*/
    if (CTC_INGRESS == dir)
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            cmd = DRV_IOR(DsPhyPort_t, DsPhyPort_SvlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_IngressFilteringEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_RandomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            *p_value = p_gb_port_master[lchip]->igs_port_prop[lport].random_log_percent;
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_RandomThreshold_f);
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_L2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_0);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_L2AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_1);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_L2AclEn2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_2);
            }

            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_L2AclEn3_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_L2AclLabel_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV4_FORCE_MAC:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ForceAclQosIpv4toMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV6_FORCE_MAC:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ForceAclQosIpv6toMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_FORCE_USE_IPV6:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ForceIpv6Key_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            /* get one is enough */
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_MacKeyUseLabel_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_ServiceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_AclPortNum_f);
            break;

/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_QosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortPolicerValid_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;

        }
    }
    else if (CTC_EGRESS == dir)
    {
        switch (port_prop)
        {
        case CTC_PORT_DIR_PROP_STAG_TPID_INDEX:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_SvlanTpidIndex_f);
            break;

        case CTC_PORT_DIR_PROP_VLAN_FILTER_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_EgressFilteringEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_RandomLogEn_f);
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_PERCENT:
            *p_value = p_gb_port_master[lchip]->egs_port_prop[lport].random_log_percent;
            break;

        case CTC_PORT_DIR_PROP_RANDOM_LOG_RATE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_RandomThreshold_f);
            break;

/********** ACL property begin **********/
        case CTC_PORT_DIR_PROP_ACL_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2AclEn0_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_0);
            }

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2AclEn1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_1);
            }

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2AclEn2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_2);
            }

            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2AclEn3_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &acl_en));
            if (acl_en)
            {
                CTC_SET_FLAG(*p_value, CTC_ACL_EN_3);
            }

            cmd = 0;
            break;

        case CTC_PORT_DIR_PROP_ACL_CLASSID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2AclLabel_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV4_FORCE_MAC:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_ForceIpv4ToMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_IPV6_FORCE_MAC:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_ForceIpv6ToMacKey_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_FORCE_USE_IPV6:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_ForceIpv6Key_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_USE_CLASSID:
            /* get one is enough */
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_MacKeyUseLabel_f);
            break;

        case CTC_PORT_DIR_PROP_SERVICE_ACL_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_ServiceAclQosEn_f);
            break;

        case CTC_PORT_DIR_PROP_ACL_PORT_BITMAP_ID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_AclPortNum_f);
            break;

/********** ACL property end **********/

        case CTC_PORT_DIR_PROP_QOS_DOMAIN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_QosDomain_f);
            break;

        case CTC_PORT_DIR_PROP_PORT_POLICER_VALID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_PortPolicerValid_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(ret);

    if (cmd)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
    }

    return CTC_E_NONE;
}

/**
@brief  Config port's internal properties
*/
int32
sys_greatbelt_port_set_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32 value)
{
    uint32 cmd = 0;

    uint8 lport = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port internal property, gport:%d, property:%d, value:%d\n", \
                     gport, port_prop, value);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    switch (port_prop)
    {
    case SYS_PORT_PROP_DEFAULT_DEI:
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_DefaultDei_f);
        break;

    case SYS_PORT_PROP_OAM_TUNNEL_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_OamTunnelEn_f);
        break;

    case SYS_PORT_PROP_IGS_OAM_VALID:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherOamValid_f);
        break;

    case SYS_PORT_PROP_EGS_OAM_VALID:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherOamValid_f);
        break;

    case SYS_PORT_PROP_DISCARD_NONE_8023OAM_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_DiscardNon8023OAM_f);
        break;

    case SYS_PORT_PROP_REPLACE_TAG_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_DefaultReplaceTagEn_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_EN:
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_Exception2En_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_DISCARD:
        cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_Exception2Discard_f);
        break;

    case SYS_PORT_PROP_SECURITY_EXCP_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        break;

    case SYS_PORT_PROP_SECURITY_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortSecurityEn_f);
        break;

    case SYS_PORT_PROP_MAC_SECURITY_DISCARD:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MacSecurityDiscard_f);
        break;

    case SYS_PORT_PROP_REPLACE_DSCP_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReplaceDscp_f);
        break;

    case SYS_PORT_PROP_STMCTL_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortStormCtlEn_f);
        break;

    case SYS_PORT_PROP_STMCTL_OFFSET:
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortStormCtlPtr_f);
        break;

    /*new properties for GB*/
    case SYS_PORT_PROP_LINK_OAM_EN:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkOamEn_f);
        break;

    case SYS_PORT_PROP_LINK_OAM_MEP_INDEX:
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkOamMepIndex_f);
        break;

    case SYS_PORT_PROP_SERVICE_POLICER_VALID:
        value = (value) ? 1 : 0;
        cmd = DRV_IOW(DsDestPort_t, DsDestPort_ServicePolicerValid_f);
        break;

    case SYS_PORT_PROP_SPEED_MODE:
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_Speed_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    return CTC_E_NONE;
}

/**
@brief  Get port's internal properties according to gport id
*/
int32
sys_greatbelt_port_get_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32* p_value)
{
    uint32 cmd = 0;

    uint8 lport = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_value);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port internal property, gport:%d, property:%d!\n", gport, port_prop);

    /*do read*/
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    switch (port_prop)
    {
    case SYS_PORT_PROP_DEFAULT_DEI:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_DefaultDei_f);
        break;

    case SYS_PORT_PROP_OAM_TUNNEL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_OamTunnelEn_f);
        break;

    case SYS_PORT_PROP_IGS_OAM_VALID:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_EtherOamValid_f);
        break;

    case SYS_PORT_PROP_EGS_OAM_VALID:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_EtherOamValid_f);
        break;

    case SYS_PORT_PROP_REPLACE_TAG_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_DefaultReplaceTagEn_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_EN:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_Exception2En_f);
        break;

    case SYS_PORT_PROP_EXCEPTION_DISCARD:
        cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_Exception2Discard_f);
        break;

    case SYS_PORT_PROP_SECURITY_EXCP_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortSecurityExceptionEn_f);
        break;

    case SYS_PORT_PROP_SECURITY_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortSecurityEn_f);
        break;

    case SYS_PORT_PROP_MAC_SECURITY_DISCARD:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_MacSecurityDiscard_f);
        break;

    case SYS_PORT_PROP_REPLACE_DSCP_EN:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReplaceDscp_f);
        break;

    case SYS_PORT_PROP_STMCTL_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortStormCtlEn_f);
        break;

    case SYS_PORT_PROP_STMCTL_OFFSET:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortStormCtlPtr_f);
        break;

    /*new properties for GB*/
    case SYS_PORT_PROP_LINK_OAM_EN:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkOamEn_f);
        break;

    case SYS_PORT_PROP_LINK_OAM_MEP_INDEX:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkOamMepIndex_f);
        break;

    case SYS_PORT_PROP_SERVICE_POLICER_VALID:
        cmd = DRV_IOR(DsDestPort_t, DsDestPort_ServicePolicerValid_f);
        break;

    case SYS_PORT_PROP_SPEED_MODE:
        cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_Speed_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, bool enable)
{
    uint8  mac_id     = 0;
    uint16 lport      = 0;
    uint32 tbl_id     = 0;
    uint32 field_id   = 0;
    uint32 cmd        = 0;
    drv_datapath_port_capability_t port_cap;
    uint32 value = 0;
    ctc_chip_serdes_loopback_t loopback;

    SYS_PORT_INIT_CHECK(lchip);

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    CTC_BOTH_DIRECTION_CHECK(dir);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, lport, &port_cap));

    mac_id   = port_cap.mac_id;

    if ((CTC_EGRESS == dir) && (0 ==p_gb_port_master[lchip]->igs_port_prop[lport].unidir_en))
    {
        loopback.serdes_id = port_cap.serdes_id;
        loopback.enable = enable;
        loopback.mode = 0;
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_property(lchip, CTC_CHIP_PROP_SERDES_LOOPBACK, (void*)&loopback));

        value = enable?1:0;
        cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_ReceiveDisable_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        p_gb_port_master[lchip]->egs_port_prop[lport].unidir_en = value;
    }
    else if ((CTC_INGRESS == dir) && (0 ==p_gb_port_master[lchip]->egs_port_prop[lport].unidir_en))
    {
        value = enable?1:0;
        p_gb_port_master[lchip]->igs_port_prop[lport].unidir_en = value;
    }
    else
    {
        return CTC_E_PORT_FEATURE_MISMATCH;
    }

    if((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode)
        || ((DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode) && (mac_id < 48)))
    {
        /*gmac*/
        value = enable?0:1;
        tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * QUADPCS_QUAD_STEP + (mac_id % 4) * QUADPCS_PCS_STEP;
        field_id = QuadPcsPcs0AnegCfg_Pcs0AnEnable_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    }
    else if ((DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode)
        || ((DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode) && (mac_id >= 48)))
    {
        /*sgmac*/
        value = enable?0:1;
        tbl_id = SgmacPcsAnegCfg0_t + mac_id - SYS_MAX_GMAC_PORT_NUM;
        field_id = SgmacPcsAnegCfg_AnEnable_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32* p_value)
{
    uint16 lport      = 0;
    drv_datapath_port_capability_t port_cap;
    uint32 value     = 0;
    SYS_PORT_INIT_CHECK(lchip);

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    CTC_BOTH_DIRECTION_CHECK(dir);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, lport, &port_cap));

    if((DRV_SERDES_SGMII_MODE == port_cap.pcs_mode)
        || (DRV_SERDES_2DOT5_MODE == port_cap.pcs_mode) || (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode))
    {
        if (CTC_EGRESS ==  dir)
        {
            value = p_gb_port_master[lchip]->egs_port_prop[lport].unidir_en;
        }
        else
        {
            value = p_gb_port_master[lchip]->igs_port_prop[lport].unidir_en;
        }
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    *p_value = value;

    return CTC_E_NONE;
}

/**
@brief  Set port's internal properties with direction according to gport id
*/
int32
sys_greatbelt_port_set_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, ctc_direction_t dir, uint32 value)
{

    uint8 lport = 0;
    uint32 cmd = 0;

    /*Sanity check*/
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port internal property with direction, gport:%d, property:%d, dir:%d,\
                                              value:%d\n", gport, port_prop, dir, value);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*do write*/
    if ((CTC_INGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_L2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsPhyPortExt_t, DsPhyPortExt_L2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            CTC_MAX_VALUE_CHECK(value, 7);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            CTC_MAX_VALUE_CHECK(value, 16383);
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_LinkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_EtherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_MplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;

        }

        if (cmd)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        }
    }

    if ((CTC_EGRESS == dir) || (CTC_BOTH_DIRECTION == dir))
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_L2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            CTC_MAX_VALUE_CHECK(value, 7);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            CTC_MAX_VALUE_CHECK(value, 3);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            CTC_MAX_VALUE_CHECK(value, 16383);
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_LinkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_EtherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            value = value ? 1 : 0;
            cmd = DRV_IOW(DsDestPort_t, DsDestPort_MplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }

        if (cmd)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));
        }
    }

    return CTC_E_NONE;

}

/**
@brief  Get port's internal properties with direction according to gport id
*/
int32
sys_greatbelt_port_get_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, ctc_direction_t dir, uint32* p_value)
{

    uint8 lport = 0;
    uint32 cmd = 0;

    /*Sanity check*/
    CTC_PTR_VALID_CHECK(p_value);
    SYS_PORT_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port internal property with direction, gport:%d, property:%d, dir:%d" \
                     , gport, port_prop, dir);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /* zero value */
    *p_value = 0;

    /*do read,only get value by CTC_INGRESS or CTC_EGRESS,no CTC_BOTH_DIRECTION*/
    if (CTC_INGRESS == dir)
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_L2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            cmd = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_L2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_EtherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_LinkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_EtherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_MplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;

        }
    }
    else if (CTC_EGRESS == dir)
    {
        switch (port_prop)
        {
        case SYS_PORT_DIR_PROP_L2_SPAN_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2SpanEn_f);
            break;

        case SYS_PORT_DIR_PROP_L2_SPAN_ID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_L2SpanId_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_EtherOamEdgePort_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_TYPE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_LinkLmType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_LinkLmCos_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_LinkLmCosType_f);
            break;

        case SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_LinkLmIndexBase_f);
            break;

        case SYS_PORT_DIR_PROP_ETHER_LM_VALID:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_EtherLmValid_f);
            break;

        case SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN:
            cmd = DRV_IOR(DsDestPort_t, DsDestPort_MplsSectionLmEn_f);
            break;

        default:
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, p_value));
    return CTC_E_NONE;
}

/**
@brief  Set logic port check enable/disable
*/
int32
sys_greatbelt_set_logic_port_check_en(uint8 lchip, bool enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set logic port check enable:%d\n", enable);

    SYS_PORT_INIT_CHECK(lchip);

    p_gb_port_master[lchip]->use_logic_port_check = (enable == TRUE) ? 1 : 0;
    return CTC_E_NONE;
}

/**
@brief  Get logic port check enable/disable
*/
int32
sys_greatbelt_get_logic_port_check_en(uint8 lchip, bool* p_enable)
{
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_PTR_VALID_CHECK(p_enable);
    SYS_PORT_INIT_CHECK(lchip);

    *p_enable = p_gb_port_master[lchip]->use_logic_port_check ? TRUE : FALSE;

    return CTC_E_NONE;
}

/**
@brief  Set ipg size
*/
int32
sys_greatbelt_set_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8 size)
{

    uint32 cmd_i = 0;
    uint32 cmd_e = 0;
    uint32 field_val = 0;

    SYS_PORT_INIT_CHECK(lchip);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipg size index:%d ipg size:%d\n", index, size);

    field_val = size;

    switch (index)
    {
    case CTC_IPG_SIZE_0:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_Ipg0_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_Ipg0_f);
        break;

    case CTC_IPG_SIZE_1:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_Ipg1_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_Ipg1_f);
        break;

    case CTC_IPG_SIZE_2:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_Ipg2_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_Ipg2_f);
        break;

    case CTC_IPG_SIZE_3:
        cmd_i = DRV_IOW(IpeIpgCtl_t, IpeIpgCtl_Ipg3_f);
        cmd_e = DRV_IOW(EpeIpgCtl_t, EpeIpgCtl_Ipg3_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_i, &field_val));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_e, &field_val));

    return CTC_E_NONE;
}

/**
@brief  Get ipg size
*/
int32
sys_greatbelt_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_PORT_INIT_CHECK(lchip);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipg size index:%d\n", index);

    CTC_PTR_VALID_CHECK(p_size);

    switch (index)
    {
    case CTC_IPG_SIZE_0:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_Ipg0_f);
        break;

    case CTC_IPG_SIZE_1:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_Ipg1_f);
        break;

    case CTC_IPG_SIZE_2:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_Ipg2_f);
        break;

    case CTC_IPG_SIZE_3:
        cmd = DRV_IOR(IpeIpgCtl_t, IpeIpgCtl_Ipg3_f);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *p_size = field_val;

    return CTC_E_NONE;

}

/**
@brief  Set Port MAC PreFix
*/
int32
sys_greatbelt_port_set_port_mac_prefix(uint8 lchip, ctc_port_mac_prefix_t* p_port_mac)
{


    uint32 cmd_ipe0 = 0;
    uint32 cmd_ipe1 = 0;
    uint32 cmd_epe = 0;
    uint32 cmd_oam_rx = 0;
    uint32 cmd_oam_tx = 0;
    uint32 cmd_qcn_tx = 0;
    mac_addr_t mac_prefix;

    ipe_port_mac_ctl_t      ipe_port_mac_ctl;
    ipe_port_mac_ctl1_t     ipe_port_mac_ctl_1;
    epe_l2_port_mac_sa_t    epe_l2_port_mac_sa;
    oam_rx_proc_ether_ctl_t oam_rx_proc_ether_ctl;
    oam_ether_tx_mac_t      oam_ether_tx_mac;
    net_tx_qcn_ctl_t        net_tx_qcn_ctl;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_mac);
    /*1. check prefix mac*/

    if (CTC_PORT_MAC_PREFIX_MAC_NONE == p_port_mac->prefix_type)
    {
        return CTC_E_NONE;
    }

    if (CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_1)
        && CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_0))
    {
        if (0 != sal_memcmp(p_port_mac->port_mac[0], p_port_mac->port_mac[1], 4))
        {
            return CTC_E_PORT_INVALID_PORT_MAC;
        }
    }

    sal_memset(&mac_prefix, 0, sizeof(mac_addr_t));
    sal_memset(&ipe_port_mac_ctl,       0, sizeof(ipe_port_mac_ctl_t));
    sal_memset(&ipe_port_mac_ctl_1,       0, sizeof(ipe_port_mac_ctl1_t));
    sal_memset(&epe_l2_port_mac_sa,     0, sizeof(epe_l2_port_mac_sa_t));
    sal_memset(&oam_rx_proc_ether_ctl,  0, sizeof(oam_rx_proc_ether_ctl_t));
    sal_memset(&oam_ether_tx_mac,       0, sizeof(oam_ether_tx_mac_t));
    sal_memset(&net_tx_qcn_ctl,       0, sizeof(net_tx_qcn_ctl_t));


    cmd_ipe0 = DRV_IOR(IpePortMacCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe1 = DRV_IOR(IpePortMacCtl1_t, DRV_ENTRY_FLAG);
    cmd_epe = DRV_IOR(EpeL2PortMacSa_t, DRV_ENTRY_FLAG);
    cmd_oam_rx = DRV_IOR(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    cmd_oam_tx = DRV_IOR(OamEtherTxMac_t, DRV_ENTRY_FLAG);
    cmd_qcn_tx = DRV_IOR(NetTxQcnCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe0, &ipe_port_mac_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe1, &ipe_port_mac_ctl_1));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_epe, &epe_l2_port_mac_sa));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_oam_rx, &oam_rx_proc_ether_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_oam_tx, &oam_ether_tx_mac));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_qcn_tx, &net_tx_qcn_ctl));

    if (CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_0))
    {
        sal_memcpy(&mac_prefix, p_port_mac->port_mac[0], sizeof(mac_addr_t));
        ipe_port_mac_ctl.port_mac_type0_47_40   = p_port_mac->port_mac[0][0];
        ipe_port_mac_ctl.port_mac_type0_39_8    = ((p_port_mac->port_mac[0][1] << 24) | (p_port_mac->port_mac[0][2] << 16)
                                                   | (p_port_mac->port_mac[0][3] << 8) | p_port_mac->port_mac[0][4]);

        ipe_port_mac_ctl_1.port_mac_type0_47_40 = ipe_port_mac_ctl.port_mac_type0_47_40;
        ipe_port_mac_ctl_1.port_mac_type0_39_8 = ipe_port_mac_ctl.port_mac_type0_39_8;

        epe_l2_port_mac_sa.port_mac_sa0_47_32   = ((p_port_mac->port_mac[0][0] << 8) | p_port_mac->port_mac[0][1]);
        epe_l2_port_mac_sa.port_mac_sa0_31_8    = ((p_port_mac->port_mac[0][2] << 16) | (p_port_mac->port_mac[0][3] << 8)
                                                   | p_port_mac->port_mac[0][4]);

        oam_rx_proc_ether_ctl.port_mac          = ((p_port_mac->port_mac[0][0] << 24) | (p_port_mac->port_mac[0][1] << 16)
                                                   | (p_port_mac->port_mac[0][2] << 8) | p_port_mac->port_mac[0][3]);

        oam_ether_tx_mac.tx_port_mac_high       = ((p_port_mac->port_mac[0][0] << 24) | (p_port_mac->port_mac[0][1] << 16)
                                                   | (p_port_mac->port_mac[0][2] << 8) | p_port_mac->port_mac[0][3]);

        net_tx_qcn_ctl.qcn_mac47_40 = ipe_port_mac_ctl.port_mac_type0_47_40;
        net_tx_qcn_ctl.qcn_mac39_8  = ipe_port_mac_ctl.port_mac_type0_39_8;

    }

    if (CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_1))
    {
        sal_memcpy(&mac_prefix, p_port_mac->port_mac[1], sizeof(mac_addr_t));
        ipe_port_mac_ctl.port_mac_type1_47_40   = p_port_mac->port_mac[1][0];
        ipe_port_mac_ctl.port_mac_type1_39_8    = ((p_port_mac->port_mac[1][1] << 24) | (p_port_mac->port_mac[1][2] << 16)
                                                   | (p_port_mac->port_mac[1][3] << 8) | p_port_mac->port_mac[1][4]);

        ipe_port_mac_ctl_1.port_mac_type1_47_40 = ipe_port_mac_ctl.port_mac_type1_47_40;
        ipe_port_mac_ctl_1.port_mac_type1_39_8 = ipe_port_mac_ctl.port_mac_type1_39_8;

        epe_l2_port_mac_sa.port_mac_sa1_47_32   = ((p_port_mac->port_mac[1][0] << 8) | p_port_mac->port_mac[1][1]);
        epe_l2_port_mac_sa.port_mac_sa1_31_8    = ((p_port_mac->port_mac[1][2] << 16) | (p_port_mac->port_mac[1][3] << 8)
                                                   | p_port_mac->port_mac[1][4]);

        oam_rx_proc_ether_ctl.port_mac          = ((p_port_mac->port_mac[1][0] << 24) | (p_port_mac->port_mac[1][1] << 16)
                                                   | (p_port_mac->port_mac[1][2] << 8) | p_port_mac->port_mac[1][3]);
        oam_ether_tx_mac.tx_port_mac_high       = ((p_port_mac->port_mac[1][0] << 24) | (p_port_mac->port_mac[1][1] << 16)
                                                   | (p_port_mac->port_mac[1][2] << 8) | p_port_mac->port_mac[1][3]);


        net_tx_qcn_ctl.qcn_mac47_40 = ipe_port_mac_ctl.port_mac_type0_47_40;
        net_tx_qcn_ctl.qcn_mac39_8  = ipe_port_mac_ctl.port_mac_type0_39_8;

    }

    cmd_ipe0 = DRV_IOW(IpePortMacCtl_t, DRV_ENTRY_FLAG);
    cmd_ipe1 = DRV_IOW(IpePortMacCtl1_t, DRV_ENTRY_FLAG);
    cmd_epe = DRV_IOW(EpeL2PortMacSa_t, DRV_ENTRY_FLAG);
    cmd_oam_rx = DRV_IOW(OamRxProcEtherCtl_t, DRV_ENTRY_FLAG);
    cmd_oam_tx = DRV_IOW(OamEtherTxMac_t, DRV_ENTRY_FLAG);
    cmd_qcn_tx = DRV_IOW(NetTxQcnCtl_t, DRV_ENTRY_FLAG);



    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe0, &ipe_port_mac_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_ipe1, &ipe_port_mac_ctl_1));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_epe, &epe_l2_port_mac_sa));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_oam_rx, &oam_rx_proc_ether_ctl));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_oam_tx, &oam_ether_tx_mac));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_qcn_tx, &net_tx_qcn_ctl));


    return CTC_E_NONE;

}

/**
@brief  Set Port MAC PostFix
*/
int32
sys_greatbelt_port_set_port_mac_postfix(uint8 lchip, uint16 gport, ctc_port_mac_postfix_t* p_port_mac)
{

    uint8 lport = 0;
    uint32 cmd  = 0;
    uint16 mac  = 0;
    uint32 field_val = 0;
    ipe_port_mac_ctl_t ipe_port_mac_ctl;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_mac);

    if ((p_port_mac->prefix_type == CTC_PORT_MAC_PREFIX_MAC_NONE)
        || (p_port_mac->prefix_type == CTC_PORT_MAC_PREFIX_MAC_ALL))
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&ipe_port_mac_ctl, 0, sizeof(ipe_port_mac_ctl_t));
    cmd = DRV_IOR(IpePortMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_port_mac_ctl));

    /*ipe & epe*/
    if (CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_0))
    {
        field_val = 0;
        mac = ((ipe_port_mac_ctl.port_mac_type0_39_8 & 0xFF) << 8);
    }
    else if (CTC_FLAG_ISSET(p_port_mac->prefix_type, CTC_PORT_MAC_PREFIX_MAC_1))
    {
        field_val = 1;
        mac = ((ipe_port_mac_ctl.port_mac_type1_39_8 & 0xFF) << 8);
    }

    cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_PortMacType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortMacSaType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = p_port_mac->low_8bits_mac;
    cmd = DRV_IOW(DsPhyPort_t, DsPhyPort_PortMacLabel_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    cmd = DRV_IOW(DsDestPort_t, DsDestPort_PortMacSa_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    if (lport < DS_QCN_PORT_MAC_MAX_INDEX)
    {
        /*Qcn,PFC*/
        cmd = DRV_IOW(DsQcnPortMac_t, DsQcnPortMac_PortMacLabel_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));
    }

    /*oam*/
    field_val = (mac | p_port_mac->low_8bits_mac);
    cmd = DRV_IOW(DsPortProperty_t, DsPortProperty_MacSaByte_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));



    return CTC_E_NONE;

}

/**
@brief  Get Port MAC
*/
int32
sys_greatbelt_port_get_port_mac(uint8 lchip, uint16 gport, mac_addr_t* p_port_mac)
{

    uint8 lport = 0;
    uint32 cmd  = 0;

    ds_phy_port_t       ds_phy_port;
    ipe_port_mac_ctl_t  ipe_port_mac_ctl;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_port_mac);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&ipe_port_mac_ctl, 0, sizeof(ipe_port_mac_ctl_t));
    cmd = DRV_IOR(IpePortMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_port_mac_ctl));

    cmd = DRV_IOR(DsPhyPort_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_phy_port));
    if (0 == ds_phy_port.port_mac_type)
    {
        (*p_port_mac)[0] = ipe_port_mac_ctl.port_mac_type0_47_40;
        (*p_port_mac)[1] = ((ipe_port_mac_ctl.port_mac_type0_39_8 >> 24) & 0xFF);
        (*p_port_mac)[2] = ((ipe_port_mac_ctl.port_mac_type0_39_8 >> 16) & 0xFF);
        (*p_port_mac)[3] = ((ipe_port_mac_ctl.port_mac_type0_39_8 >> 8) & 0xFF);
        (*p_port_mac)[4] = (ipe_port_mac_ctl.port_mac_type0_39_8 & 0xFF);
        (*p_port_mac)[5] = ds_phy_port.port_mac_label;
    }
    else if (1 == ds_phy_port.port_mac_type)
    {
        (*p_port_mac)[0] = ipe_port_mac_ctl.port_mac_type1_47_40;
        (*p_port_mac)[1] = ((ipe_port_mac_ctl.port_mac_type1_39_8 >> 24) & 0xFF);
        (*p_port_mac)[2] = ((ipe_port_mac_ctl.port_mac_type1_39_8 >> 16) & 0xFF);
        (*p_port_mac)[3] = ((ipe_port_mac_ctl.port_mac_type1_39_8 >> 8) & 0xFF);
        (*p_port_mac)[4] = (ipe_port_mac_ctl.port_mac_type1_39_8 & 0xFF);
        (*p_port_mac)[5] = ds_phy_port.port_mac_label;
    }

    return CTC_E_NONE;

}

int32
sys_greatbelt_port_set_reflective_bridge_en(uint8 lchip, uint16 gport, bool enable)
{

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    field_val = (TRUE == enable) ? 0 : 1;
    cmd = DRV_IOW(DsSrcPort_t, DsSrcPort_PortCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    field_val = (TRUE == enable) ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_ReflectiveBridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_reflective_bridge_en(uint8 lchip, uint16 gport, bool* p_enable)
{

    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val1 = 0;
    uint32 field_val2 = 0;

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    CTC_PTR_VALID_CHECK(p_enable);
    *p_enable = FALSE;

    cmd = DRV_IOR(DsSrcPort_t, DsSrcPort_PortCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val1));

    cmd = DRV_IOR(DsDestPort_t, DsDestPort_ReflectiveBridgeEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val2));

    *p_enable = ((!field_val1) && field_val2) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_link_status_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 gchip = 0;
    uint32 cmd = 0;
    uint32 intr_status = 0;
    uint8  index = 0;
    uint16 lport = 0;
    ctc_port_link_status_t ctc_link_status;
    mdio_link_status_t link_status;
    mdio_link_status_t link_status_tmp;

    SYS_PORT_INIT_CHECK(lchip);

    sal_memset(&ctc_link_status, 0, sizeof(ctc_link_status));
    sal_memset(&link_status_tmp, 0, sizeof(link_status_tmp));

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    cmd = DRV_IOR(MdioLinkStatus_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &link_status));

    /* get which bit change */
    link_status_tmp.mdio0_link_status = link_status.mdio0_link_status ^ p_gb_port_master[lchip]->status_1g[0];
    link_status_tmp.mdio1_link_status = link_status.mdio1_link_status ^ p_gb_port_master[lchip]->status_1g[1];
    link_status_tmp.mdio2_link_status = link_status.mdio2_link_status ^ p_gb_port_master[lchip]->status_xg[0];
    link_status_tmp.mdio3_link_status = link_status.mdio3_link_status ^ p_gb_port_master[lchip]->status_xg[1];

    /* get which bit change from 0 to 1, is link down */
    link_status_tmp.mdio0_link_status = link_status_tmp.mdio0_link_status & p_gb_port_master[lchip]->status_1g[0];
    link_status_tmp.mdio1_link_status = link_status_tmp.mdio1_link_status & p_gb_port_master[lchip]->status_1g[1];
    link_status_tmp.mdio2_link_status = link_status_tmp.mdio2_link_status & p_gb_port_master[lchip]->status_xg[0];
    link_status_tmp.mdio3_link_status = link_status_tmp.mdio3_link_status & p_gb_port_master[lchip]->status_xg[1];

    /* updata ctc_link_status */
    ctc_link_status.status_1g[0] = link_status_tmp.mdio0_link_status;
    ctc_link_status.status_1g[1] = link_status_tmp.mdio1_link_status;
    ctc_link_status.status_xg[0] = link_status_tmp.mdio2_link_status << 12 | link_status_tmp.mdio3_link_status;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "link status change!! \n");

    /* get gport, call event callback function return ctc_link_status */
    if (p_gb_port_master[lchip]->link_status_change_cb)
    {
        if(link_status_tmp.mdio0_link_status > 0)
        {
            for (index = 0; index < 32; index++)
            {
                intr_status = link_status_tmp.mdio0_link_status;
                if ((intr_status >> index) & 0x01)
                {
                    sys_greatbelt_chip_get_lport(lchip, 0, index, &lport);
                    ctc_link_status.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
                    p_gb_port_master[lchip]->link_status_change_cb(gchip, &ctc_link_status);
                }
            }
        }

        if(link_status_tmp.mdio1_link_status > 0)
        {
            for (index = 0; index < 32; index++)
            {
                intr_status = link_status_tmp.mdio1_link_status;
                if ((intr_status >> index) & 0x01)
                {
                    sys_greatbelt_chip_get_lport(lchip, 1, index, &lport);
                    ctc_link_status.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
                    p_gb_port_master[lchip]->link_status_change_cb(gchip, &ctc_link_status);
                }
            }
        }

        if(link_status_tmp.mdio2_link_status > 0)
        {
            for (index = 0; index < 12; index++)
            {
                intr_status = link_status_tmp.mdio2_link_status;
                if ((intr_status >> index) & 0x01)
                {
                    sys_greatbelt_chip_get_lport(lchip, 2, index, &lport);
                    ctc_link_status.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
                    p_gb_port_master[lchip]->link_status_change_cb(gchip, &ctc_link_status);
                }
            }
        }

        if(link_status_tmp.mdio3_link_status > 0)
        {
            for (index = 0; index < 12; index++)
            {
                intr_status = link_status_tmp.mdio3_link_status;
                if ((intr_status >> index) & 0x01)
                {
                    sys_greatbelt_chip_get_lport(lchip, 3, index, &lport);
                    ctc_link_status.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
                    p_gb_port_master[lchip]->link_status_change_cb(gchip, &ctc_link_status);
                }
            }
        }
    }

    /* updata sw */
    p_gb_port_master[lchip]->status_1g[0] = link_status.mdio0_link_status;
    p_gb_port_master[lchip]->status_1g[1] = link_status.mdio1_link_status;
    p_gb_port_master[lchip]->status_xg[0] = link_status.mdio2_link_status;
    p_gb_port_master[lchip]->status_xg[1] = link_status.mdio3_link_status;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_port_get_pcs_type(uint8 lchip, uint32  sub_intr, sys_port_pcs_type_t* p_type)
{
    if ((sub_intr >= SYS_INTR_GB_SUB_NORMAL_QUAD_PCS0) && (sub_intr <= SYS_INTR_GB_SUB_NORMAL_QUAD_PCS5))
    {
        *p_type = SYS_PORT_FROM_QUAD_PCS;
    }
    else if ((sub_intr >= SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII0) && (sub_intr <= SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII11))
    {
        *p_type = SYS_PORT_FROM_QSGMII_PCS;
    }
    else if ((sub_intr >= SYS_INTR_GB_SUB_NORMAL_SGMAC0) && (sub_intr <= SYS_INTR_GB_SUB_NORMAL_SGMAC11))
    {
        *p_type = SYS_PORT_FROM_SGMAC_PCS;
    }
    else
    {
        *p_type = SYS_PORT_PCS_MAX_TYPE;
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_port_link_status_from_pcs(uint8 lchip, void* p_data)
{
    uint8 gchip = 0;
    ctc_interrupt_abnormal_status_t* p_status = NULL;
    ctc_port_link_status_t ctc_link_status;
    sys_port_pcs_type_t pcs_type;
    uint32 status = 0;
    uint16 index = 0;
    uint8  mac_id = 0;
    uint32 mask_status = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    sal_memset(&ctc_link_status, 0, sizeof(ctc_port_link_status_t));

    p_status = (ctc_interrupt_abnormal_status_t*)p_data;
    status = p_status->status.bmp[0];

    _sys_greatbelt_port_get_pcs_type(lchip, p_status->type.sub_intr, &pcs_type);

    tbl_id = g_intr_sub_reg_sup_normal[p_status->type.sub_intr].tbl_id;

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, INTR_INDEX_MASK_SET, cmd, &mask_status));

    switch(pcs_type)
    {
        case SYS_PORT_FROM_QUAD_PCS:

            /* now only process link event */
            for (index = 4; index < 8; index++)
            {
                if (((status >> index)&0x01) && !((mask_status >> index)&0x01))
                {
                    /*sgmii mode only have 24 ports*/
                    mac_id = (p_status->type.sub_intr-SYS_INTR_GB_SUB_NORMAL_QUAD_PCS0)*4 + (index - 4);

                    if (mac_id < 32)
                    {
                        CTC_BIT_SET(ctc_link_status.status_1g[0], mac_id);
                    }
                    else
                    {
                        CTC_BIT_SET(ctc_link_status.status_1g[1], (mac_id-32));
                    }
                }
            }

            break;

        case SYS_PORT_FROM_QSGMII_PCS:

             /* now only process link event */
            for (index = 4; index < 8; index++)
            {
                if (((status >> index)&0x01) && !((mask_status >> index)&0x01))
                {
                    mac_id = (p_status->type.sub_intr-SYS_INTR_GB_SUB_NORMAL_QUAD_SGMII0)*4 + (index - 4);

                    if (mac_id < 32)
                    {
                        CTC_BIT_SET(ctc_link_status.status_1g[0], mac_id);
                    }
                    else
                    {
                        CTC_BIT_SET(ctc_link_status.status_1g[1], (mac_id-32));
                    }
                }
            }
            break;

        case SYS_PORT_FROM_SGMAC_PCS:

            if((status & 0x2)&&!(mask_status & 0x2))
            {
                mac_id = p_status->type.sub_intr - SYS_INTR_GB_SUB_NORMAL_SGMAC0 + SYS_MAX_GMAC_PORT_NUM;

                CTC_BIT_SET(ctc_link_status.status_xg[0], (mac_id-SYS_MAX_GMAC_PORT_NUM));
            }
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_port_isr_link_status_change_isr(lchip, gchip, &ctc_link_status));

    if (p_gb_port_master[lchip]->link_status_change_cb)
    {
        p_gb_port_master[lchip]->link_status_change_cb(gchip, &ctc_link_status);
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_link_status_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    p_gb_port_master[lchip]->link_status_change_cb = cb;
    return CTC_E_NONE;
}

enum ds_phy_port_ext_user_id_hash_type_e
{
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_DISABLE,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_TWO_VLAN,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN_COS,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN_COS,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_MAC_SA,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_MAC_SA,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_IP_SA,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_IP_SA,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_L2,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_RES,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_TUNNEL,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_TRILL,
    DS_PHY_PORT_EXT_SCL_HASH_TYPE_NUM
};

enum userid_port_tcam_type_e
{
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_MAC_SA = 0x0,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP    = 0x1,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_VLAN = 0x2,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP_TUNNEL = 0x3,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_PBB = 0x4,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_WLAN = 0x5,
    DS_PHY_PORT_EXT_SCL_TCAM_TYPE_TRILL = 0x6
};

enum ds_dest_port_vlan_hash_type_e
{
    DS_DEST_PORT_VLAN_HASH_TYPE_DISABLE          = 0x0,
    DS_DEST_PORT_VLAN_HASH_TYPE_TWO_VLAN         = 0x1,
    DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN            = 0x2,
    DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN            = 0x3,
    DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN_COS        = 0x4,
    DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN_COS        = 0x5,
    DS_DEST_PORT_VLAN_HASH_TYPE_PORT             = 0xA,
    DS_DEST_PORT_VLAN_HASH_TYPE_PORT_IP_SA       = 0x9
};


STATIC int32
_sys_greatbelt_port_unmap_scl_type(uint8 lchip, uint8 dir,uint8 scl_id,
                                   uint8 asic_hash, uint8 asic_tcam, uint8 asic_tcam_en, uint8 hash_use_da,
                                   uint8 tunnel_hash_en, uint8 rpf_en,
                                   uint8* ctc_hash, uint8* ctc_tcam)
{
    if(CTC_INGRESS == dir)
    {
        switch(asic_hash)
        {
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_DISABLE:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE ;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_TWO_VLAN:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_2VLAN;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN_COS:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN_COS;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN_COS:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN_COS;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_MAC_SA:
            if (hash_use_da)
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_MAC_DA;
            }
            else
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_MAC_SA;
            }
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_MAC_SA:
            if (hash_use_da)
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_DA;
            }
            else
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_SA;
            }
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_IP_SA:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_IP_SA;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_IP_SA:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_IP_SA;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_PORT;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_L2:
            *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_L2;
            break;
        case DS_PHY_PORT_EXT_SCL_HASH_TYPE_TUNNEL:
            if ( (1 == scl_id) && (rpf_en))
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF;
            }
            else
            {
                *ctc_hash = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL;
            }
            break;
        default:
            break;
        }

        if (asic_tcam_en)
        {
            switch(asic_tcam)
            {
            case DS_PHY_PORT_EXT_SCL_TCAM_TYPE_MAC_SA:
                *ctc_tcam  = CTC_PORT_IGS_SCL_TCAM_TYPE_MAC;
                 break;
            case DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP:
                *ctc_tcam = CTC_PORT_IGS_SCL_TCAM_TYPE_IP;
                 break;
            case DS_PHY_PORT_EXT_SCL_TCAM_TYPE_VLAN:
                *ctc_tcam = CTC_PORT_IGS_SCL_TCAM_TYPE_VLAN;
                 break;
            case DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP_TUNNEL:
            if ( (1 == scl_id) && (rpf_en))
            {
                *ctc_tcam = CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL_RPF;
            }
            else
            {
                *ctc_tcam = CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL;
            }
            break;
            default:
                 break;
            }
        }
        else
        {
            *ctc_tcam = CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE;
        }
    }
    else /* egress */
    {
        switch(asic_hash)
        {
        case DS_DEST_PORT_VLAN_HASH_TYPE_DISABLE:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_DISABLE;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_TWO_VLAN:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_2VLAN;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN_COS:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN_COS;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN_COS:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN_COS;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_PORT:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT;
            break;
        case DS_DEST_PORT_VLAN_HASH_TYPE_PORT_IP_SA:
            *ctc_hash = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_XC;
            break;

        default:
            break;
        }
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_greatbelt_port_map_scl_type(uint8 lchip, uint8 dir, uint8 ctc_hash, uint8 ctc_tcam,
                                 uint8* asic_hash, uint8* asic_tcam, uint8* asic_tcam_en, uint8* hash_use_da,
                                 uint8* tunnel_hash_en, uint8* rpf_en)
{
    if(CTC_INGRESS == dir)
    {
        switch(ctc_hash)
        {
        case CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_DISABLE;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_2VLAN:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_TWO_VLAN;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN_COS:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_SVLAN_COS;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN_COS:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_CVLAN_COS;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_MAC_SA:
        case CTC_PORT_IGS_SCL_HASH_TYPE_MAC_DA:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_MAC_SA;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_SA:
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_DA:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_MAC_SA;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_IP_SA:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_IP_SA;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT_IP_SA:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT_IP_SA;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_PORT:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_PORT;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_L2:
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_L2;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL:
            *tunnel_hash_en = 1;
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_TUNNEL;
            break;
        case CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF:
            *tunnel_hash_en = 1;
            *rpf_en = 1;
            *asic_hash = DS_PHY_PORT_EXT_SCL_HASH_TYPE_TUNNEL;
            break;
        default:
            break;
        }

        switch(ctc_tcam)
        {
        case CTC_PORT_IGS_SCL_TCAM_TYPE_MAC:
            *asic_tcam = DS_PHY_PORT_EXT_SCL_TCAM_TYPE_MAC_SA;
             break;
        case CTC_PORT_IGS_SCL_TCAM_TYPE_IP:
            *asic_tcam = DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP;
             break;
        case CTC_PORT_IGS_SCL_TCAM_TYPE_VLAN:
            *asic_tcam = DS_PHY_PORT_EXT_SCL_TCAM_TYPE_VLAN;
             break;
        case CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL:
            *asic_tcam = DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP_TUNNEL;
             break;
        case CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL_RPF:
            *rpf_en = 1;
            *asic_tcam = DS_PHY_PORT_EXT_SCL_TCAM_TYPE_IP_TUNNEL;
             break;
        default:
             break;
        }

        /* set asic tcam en */
        if (CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE == ctc_tcam)
        {
            *asic_tcam_en = 0;
        }
        else
        {
            *asic_tcam_en = 1;
        }

        /* set hash use da*/
        if ((CTC_PORT_IGS_SCL_HASH_TYPE_MAC_DA == ctc_hash)
            ||(CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_DA == ctc_hash))
        {
            *hash_use_da = 1;
        }
        else
        {
            *hash_use_da = 0;
        }
    }
    else /* egress */
    {
        switch(ctc_hash)
        {
        case CTC_PORT_EGS_SCL_HASH_TYPE_DISABLE:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_DISABLE;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_2VLAN:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_TWO_VLAN;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN_COS:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_SVLAN_COS;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN_COS:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_CVLAN_COS;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_PORT;
            break;
        case CTC_PORT_EGS_SCL_HASH_TYPE_PORT_XC:
            *asic_hash = DS_DEST_PORT_VLAN_HASH_TYPE_PORT_IP_SA;
            break;
        default:
            break;
        }
    }
    return CTC_E_NONE;

}

int32
sys_greatbelt_port_set_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop)
{

    uint8 lport = 0;
    uint32 cmd = 0;

    uint8 asic_hash_type = 0;
    uint8 asic_tcam_en   = 0;
    uint8 asic_tcam_type = 0;
    uint8 hash_use_da    = 0;
    uint8 tunnel_hash_en = 0;
    uint8 rpf_en         = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(prop);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(prop->direction, CTC_BOTH_DIRECTION -1);
    CTC_MAX_VALUE_CHECK(prop->scl_id, 1);

    /* tunnel rpf check only valid in scl id 1 */
    if ( (0 == prop->scl_id)
        && (CTC_INGRESS == prop->direction)
        && (CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF == prop->hash_type ||
        CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL_RPF == prop->tcam_type))
    {
        return CTC_E_INVALID_PARAM;
    }


    _sys_greatbelt_port_map_scl_type(lchip, prop->direction, prop->hash_type, prop->tcam_type,
                                           &asic_hash_type, &asic_tcam_type, &asic_tcam_en, &hash_use_da,
                                           &tunnel_hash_en, &rpf_en);

    if (CTC_INGRESS == prop->direction)
    {
        ds_phy_port_ext_t phy_port_ext;

        CTC_MAX_VALUE_CHECK(prop->hash_type, CTC_PORT_IGS_SCL_HASH_TYPE_MAX -1);
        CTC_MAX_VALUE_CHECK(prop->tcam_type, CTC_PORT_IGS_SCL_TCAM_TYPE_MAX -1);

        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

        if(prop->class_id_en)
        {
            SYS_SCL_PORT_CLASS_ID_CHECK(prop->class_id);
        }

        if (0 == prop->scl_id)
        {
            phy_port_ext.user_id_hash1_type         = asic_hash_type;
            phy_port_ext.hash1_use_da               = hash_use_da;
            phy_port_ext.user_id_tcam1_en           = asic_tcam_en;
            phy_port_ext.user_id_tcam1_type         = asic_tcam_type;
            phy_port_ext.hash_lookup1_use_label     = prop->class_id_en ? 1:0;
            phy_port_ext.user_id_label1             = prop->class_id;
            phy_port_ext.ipv4_tunnel_hash_en1       = tunnel_hash_en;
            phy_port_ext.ipv4_gre_tunnel_hash_en1   = tunnel_hash_en;
            phy_port_ext.tunnel_rpf_check           = rpf_en;
            phy_port_ext.user_id1_use_logic_port = prop->use_logic_port_en ? 1:0;
        }
        else
        {
            phy_port_ext.user_id_hash2_type         = asic_hash_type;
            phy_port_ext.hash2_use_da               = hash_use_da;
            phy_port_ext.user_id_tcam2_en           = asic_tcam_en;
            phy_port_ext.user_id_tcam2_type         = asic_tcam_type;
            phy_port_ext.hash_lookup2_use_label     = prop->class_id_en ? 1:0;
            phy_port_ext.user_id_label2             = prop->class_id;
            phy_port_ext.ipv4_tunnel_hash_en2       = tunnel_hash_en;
            phy_port_ext.ipv4_gre_tunnel_hash_en2   = tunnel_hash_en;
            phy_port_ext.tunnel_rpf_check           = rpf_en;
            phy_port_ext.user_id2_use_logic_port = prop->use_logic_port_en ? 1:0;
        }

        cmd = DRV_IOW(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
    }

    if (CTC_EGRESS == prop->direction)
    {
        ds_dest_port_t dest_port;

        CTC_MAX_VALUE_CHECK(prop->hash_type, CTC_PORT_EGS_SCL_HASH_TYPE_MAX -1);

        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));

        if (0 == prop->scl_id)
        {
            dest_port.vlan_hash1_type     = asic_hash_type;
        }
        else
        {
            dest_port.vlan_hash2_type     = asic_hash_type;
        }

        dest_port.vlan_hash_label     = prop->class_id;
        dest_port.vlan_hash_use_label = prop->class_id_en ? 1:0;
        dest_port.vlan_hash_use_logic_port = prop->use_logic_port_en ? 1:0;
        cmd = DRV_IOW(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop)
{

    uint8 lport = 0;
    uint32 cmd = 0;

    uint8 asic_hash_type = 0;
    uint8 asic_tcam_en   = 0;
    uint8 asic_tcam_type = 0;
    uint8 hash_use_da    = 0;
    uint8 tunnel_hash_en = 0;
    uint8 rpf_en         = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(prop);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    CTC_MAX_VALUE_CHECK(prop->direction, CTC_BOTH_DIRECTION - 1);
    CTC_MAX_VALUE_CHECK(prop->scl_id, 1);

    if (CTC_INGRESS == prop->direction)
    {
        ds_phy_port_ext_t phy_port_ext;

        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));

        if (0 == prop->scl_id)
        {
            asic_hash_type   = phy_port_ext.user_id_hash1_type;
            asic_tcam_en     = phy_port_ext.user_id_tcam1_en;
            asic_tcam_type   = phy_port_ext.user_id_tcam1_type;
            hash_use_da      = phy_port_ext.hash1_use_da;
            tunnel_hash_en   = phy_port_ext.ipv4_tunnel_hash_en1;
            rpf_en           = phy_port_ext.tunnel_rpf_check;
            prop->class_id   = phy_port_ext.user_id_label1;
            prop->class_id_en = phy_port_ext.hash_lookup1_use_label;
            prop->use_logic_port_en = phy_port_ext.user_id1_use_logic_port;
        }
        else
        {
            asic_hash_type  = phy_port_ext.user_id_hash2_type;
            asic_tcam_en    = phy_port_ext.user_id_tcam2_en;
            asic_tcam_type  = phy_port_ext.user_id_tcam2_type;
            hash_use_da      = phy_port_ext.hash2_use_da;
            tunnel_hash_en   = phy_port_ext.ipv4_tunnel_hash_en2;
            rpf_en           = phy_port_ext.tunnel_rpf_check;
            prop->class_id  = phy_port_ext.user_id_label2;
            prop->class_id_en = phy_port_ext.hash_lookup2_use_label;
            prop->use_logic_port_en = phy_port_ext.user_id2_use_logic_port;
        }
    }

    if (CTC_EGRESS == prop->direction)
    {
        ds_dest_port_t dest_port;

        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &dest_port));

        if (0 == prop->scl_id)
        {
            asic_hash_type = dest_port.vlan_hash1_type;
        }
        else
        {
            asic_hash_type = dest_port.vlan_hash2_type;
        }

        prop->class_id      = dest_port.vlan_hash_label;
        prop->class_id_en   = dest_port.vlan_hash_use_label;
        prop->use_logic_port_en = dest_port.vlan_hash_use_logic_port;
    }

    _sys_greatbelt_port_unmap_scl_type(lchip, prop->direction,prop->scl_id,
                                       asic_hash_type, asic_tcam_type, asic_tcam_en, hash_use_da,tunnel_hash_en,rpf_en,
                                       &prop->hash_type, &prop->tcam_type);

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_port_type(uint8 lchip, uint16 lport,  uint8* p_type)
{
    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_type);

    *p_type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_switch_src_port(uint8 lchip, uint8 lport_valid, uint16 serdes_idx, uint8 serdes_count)
{
    uint8 src_chan_id = 0;
    uint8 index1 = 0;
    uint8 index2 = 0;
    uint8 port_count = 0;
    uint16 src_lport = 0;
    drv_datapath_port_capability_t src_port_cap;
    int32 ret = CTC_E_NONE;

    /*if (0 == mode)
    {
        return CTC_E_NONE;
    }*/

    for (index1 = 0; index1<serdes_count; index1++)
    {
        ret = drv_greatbelt_get_port_with_serdes(lchip, serdes_idx+index1, &src_lport);
        if (ret < 0)
        {
            continue;
        }
        /* get src port capabiltiy */
        sal_memset(&src_port_cap, 0, sizeof(drv_datapath_port_capability_t));
        drv_greatbelt_get_port_capability(lchip, src_lport, &src_port_cap);
        src_chan_id   = src_port_cap.chan_id;
        if (src_port_cap.pcs_mode == DRV_SERDES_QSGMII_MODE)
        {
            port_count = 4;
        }
        else
        {
            port_count = 1;
        }

        for (index2=0; index2<port_count;index2++)
        {
            src_port_cap.chan_id   = src_chan_id;
            src_port_cap.mac_id    = 0xFF;
            src_port_cap.port_type = DRV_PORT_TYPE_NONE;
            drv_greatbelt_set_port_capability(lchip, src_lport+index2, src_port_cap);
            p_gb_port_master[lchip]->igs_port_prop[src_lport+index2].port_mac_type = DRV_PORT_TYPE_NONE;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_switch_dest_port(uint8 lchip, uint8 lport_valid, uint8 mac_id, uint16 dest_lport)
{
    uint32 cmd = 0;
    uint8 channel_id = 0;
    uint8 gchip_id = 0;
    uint32 dest_gport = 0;
    drv_datapath_port_capability_t dst_port_cap;
    net_rx_channel_map_t net_rx_channel_map;
    net_tx_port_id_map_t net_tx_port_map;
    net_tx_chan_id_map_t net_tx_channel_map;

    if (0 == lport_valid)
    {
        dest_lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
         /*return CTC_E_NONE;*/
    }
    sys_greatbelt_get_gchip_id(lchip, &gchip_id);
    dest_gport = CTC_MAP_LPORT_TO_GPORT(gchip_id, dest_lport);

    sal_memset(&dst_port_cap, 0, sizeof(drv_datapath_port_capability_t));
    sal_memset(&net_rx_channel_map, 0, sizeof(net_rx_channel_map_t));
    sal_memset(&net_tx_port_map, 0, sizeof(net_tx_port_id_map_t));
    sal_memset(&net_tx_channel_map, 0, sizeof(net_tx_chan_id_map_t));

    channel_id = SYS_GET_CHANNEL_ID(lchip, dest_gport);
    if (0xFF == channel_id)
    {
        channel_id = dest_lport;
    }
    /* 3. swap src and dst chan map and set serdes mode */
    /* src_chan -> dst_lport */
    net_rx_channel_map.chan_id = channel_id;
    cmd = DRV_IOW(NetRxChannelMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &net_rx_channel_map));

    net_tx_port_map.chan_id = channel_id;
    cmd = DRV_IOW(NetTxPortIdMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, mac_id, cmd, &net_tx_port_map));

    net_tx_channel_map.port_id = mac_id;
    cmd = DRV_IOW(NetTxChanIdMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &net_tx_channel_map));

    /* update dst port capbility */
    dst_port_cap.valid     = TRUE;
    dst_port_cap.chan_id   = channel_id;
    dst_port_cap.mac_id    = mac_id;
    if (mac_id < GMAC_NUM)
    {/*1G MAC must be 0~47*/

        dst_port_cap.port_type  = DRV_PORT_TYPE_1G;
    }
    else
    {/*10G MAC must be 48~59*/

        dst_port_cap.port_type  = DRV_PORT_TYPE_SG;
    }
    drv_greatbelt_set_port_capability(lchip, dest_lport, dst_port_cap);
    if (DRV_PORT_TYPE_1G == dst_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[dest_lport].port_mac_type = CTC_PORT_MAC_GMAC;
    }
    else if (DRV_PORT_TYPE_SG == dst_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[dest_lport].port_mac_type = CTC_PORT_MAC_SGMAC;
    }

   return CTC_E_NONE;
}

/* only need to support SGMII <-> XFI */
int32
sys_greatbelt_port_swap(uint8 lchip, uint8 src_lport, uint8 dst_lport)
{
    uint8 gchip = 0;
    uint8 serdes_idx = 0;
    uint8 src_mac_id = 0;
    uint8 src_chan_id = 0;
    uint8 src_port_type = 0;
    uint8 dst_mac_id = 0;
    uint8 dst_chan_id = 0;
    uint8 dst_port_type = 0;
    uint8 dst_serdes_mode = 0;
    uint32 cmd_ipe = 0;
    uint32 cmd_epe = 0;
    uint16 gport = 0;
    ctc_chip_serdes_info_t serdes_info;
    drv_datapath_port_capability_t src_port_cap;
    drv_datapath_port_capability_t dst_port_cap;
    ipe_header_adjust_phy_port_map_t ipe_header_adjust_phyport_map;
    epe_header_adjust_phy_port_map_t epe_header_adjust_phyport_map;

    SYS_PORT_INIT_CHECK(lchip);
    sal_memset(&serdes_info, 0, sizeof(ctc_chip_serdes_info_t));
    sal_memset(&src_port_cap, 0, sizeof(drv_datapath_port_capability_t));
    sal_memset(&dst_port_cap, 0, sizeof(drv_datapath_port_capability_t));
    sal_memset(&ipe_header_adjust_phyport_map, 0, sizeof(ipe_header_adjust_phy_port_map_t));
    sal_memset(&epe_header_adjust_phyport_map, 0, sizeof(epe_header_adjust_phy_port_map_t));

    /* get src port capabiltiy */
    drv_greatbelt_get_port_capability(lchip, src_lport, &src_port_cap);
    src_mac_id    = src_port_cap.mac_id;
    src_chan_id   = src_port_cap.chan_id;
    src_port_type = src_port_cap.port_type;
    serdes_idx = src_port_cap.serdes_id;

    /* get dst port capabiltiy */
    drv_greatbelt_get_port_capability(lchip, dst_lport, &dst_port_cap);
    dst_mac_id    = dst_port_cap.mac_id;
    dst_chan_id   = dst_port_cap.chan_id;
    dst_port_type = dst_port_cap.port_type;



    if ((0xFF == src_mac_id) || (0xFF == dst_mac_id))
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    if (((DRV_PORT_TYPE_1G == src_port_type) && (DRV_PORT_TYPE_1G == dst_port_type)) ||
        ((DRV_PORT_TYPE_SG == src_port_type) && (DRV_PORT_TYPE_SG == dst_port_type)))
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No need to switch!\n");
        return CTC_E_INVALID_PARAM;
    }

    if (DRV_PORT_TYPE_1G == dst_port_type)
    {
        dst_serdes_mode = CTC_CHIP_SERDES_SGMII_MODE;
    }
    else if (DRV_PORT_TYPE_SG == dst_port_type)
    {
        dst_serdes_mode = CTC_CHIP_SERDES_XFI_MODE;
    }
    else
    {
        return CTC_E_INVALID_PORT_MAC_TYPE;
    }



/* 1. src queue drop enable */
/* 2. flush src mac and queue */
    /* flush queue and set queue drop enable*/
    CTC_ERROR_RETURN(sys_greatbelt_port_queue_flush(lchip, src_lport, TRUE));

    /* flush mac */
    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, src_lport);
    CTC_ERROR_RETURN(_sys_greatbelt_port_mac_flush_en(lchip, gport, TRUE));


/* 3. swap src and dst chan map and set serdes mode */
    /* dst_chan -> src_lport */
    cmd_ipe = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dst_chan_id, cmd_ipe, &ipe_header_adjust_phyport_map));
    ipe_header_adjust_phyport_map.local_phy_port = src_lport;
    cmd_ipe = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dst_chan_id, cmd_ipe, &ipe_header_adjust_phyport_map));

    cmd_epe = DRV_IOR(EpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dst_chan_id, cmd_epe, &epe_header_adjust_phyport_map));
    epe_header_adjust_phyport_map.local_phy_port = src_lport;
    cmd_epe = DRV_IOW(EpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, dst_chan_id, cmd_epe, &epe_header_adjust_phyport_map));


    /* src_chan -> dst_lport */
    cmd_ipe = DRV_IOR(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_chan_id, cmd_ipe, &ipe_header_adjust_phyport_map));
    ipe_header_adjust_phyport_map.local_phy_port = dst_lport;
    cmd_ipe = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_chan_id, cmd_ipe, &ipe_header_adjust_phyport_map));

    cmd_epe = DRV_IOR(EpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_chan_id, cmd_epe, &epe_header_adjust_phyport_map));
    epe_header_adjust_phyport_map.local_phy_port = dst_lport;
    cmd_epe = DRV_IOW(EpeHeaderAdjustPhyPortMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_chan_id, cmd_epe, &epe_header_adjust_phyport_map));


    /* update dst port capbility */
    dst_port_cap.chan_id   = src_chan_id;
    dst_port_cap.mac_id    = src_mac_id;
    dst_port_cap.port_type = src_port_type;
    drv_greatbelt_set_port_capability(lchip, dst_lport, dst_port_cap);
    if (DRV_PORT_TYPE_1G == dst_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[dst_lport].port_mac_type = CTC_PORT_MAC_GMAC;
    }
    else if (DRV_PORT_TYPE_SG == dst_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[dst_lport].port_mac_type = CTC_PORT_MAC_SGMAC;
    }

    /* update src port capbility */
    src_port_cap.chan_id   = dst_chan_id;
    src_port_cap.mac_id    = dst_mac_id;
    src_port_cap.port_type = dst_port_type;
    drv_greatbelt_set_port_capability(lchip, src_lport, src_port_cap);
    if (DRV_PORT_TYPE_1G == src_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[src_lport].port_mac_type = CTC_PORT_MAC_GMAC;
    }
    else if (DRV_PORT_TYPE_SG == src_port_cap.port_type)
    {
        p_gb_port_master[lchip]->igs_port_prop[src_lport].port_mac_type = CTC_PORT_MAC_SGMAC;
    }


    /* dynamic switch */
    serdes_info.serdes_id = serdes_idx;
    serdes_info.serdes_mode = dst_serdes_mode;
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_mode(lchip, &serdes_info));



/* 4. swap src and dst queue base */
    sys_greatbelt_port_queue_swap(lchip, src_lport, dst_lport);


/* 5. src queue drop disable */
    CTC_ERROR_RETURN(sys_greatbelt_port_queue_flush(lchip, src_lport, FALSE));


    return CTC_E_NONE;
}

int32
sys_greatbelt_port_mapping_lport(uint8 lchip, uint16 gport, uint8 lport)
{
    uint8 src_lport = 0;
    uint8 chan_id   = 0;
    uint32 cmd      = 0;
    uint32 value    = 0;

    drv_datapath_port_capability_t port_cap;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, src_lport);

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    CTC_ERROR_RETURN(drv_greatbelt_get_port_capability(lchip, src_lport, &port_cap));
    chan_id     = port_cap.chan_id;
    value       = lport;

    cmd = DRV_IOW(IpeHeaderAdjustPhyPortMap_t, IpeHeaderAdjustPhyPortMap_LocalPhyPort_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &value));
    return CTC_E_NONE;
}

int32
sys_greatbelt_port_dest_gport_check(uint8 lchip, uint32 gport)
{
#define SYS_PORT_MAX_DEST_PORT_VALUE 0xFFFF

    uint16 max_port_num = 0;

    if ((CTC_IS_LINKAGG_PORT(gport) && ((gport & CTC_LOCAL_PORT_MASK) > SYS_GB_MAX_LINKAGG_ID))
        || ((!(CTC_IS_LINKAGG_PORT(gport))) && CTC_MAP_GPORT_TO_GCHIP(gport) > SYS_GB_MAX_GCHIP_CHIP_ID))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    if (TRUE == sys_greatbelt_chip_is_local(lchip, CTC_MAP_GPORT_TO_GCHIP(gport)))
    {
        max_port_num = SYS_GB_MAX_PORT_NUM_PER_CHIP;
    }
    else
    {
        max_port_num = SYS_PORT_MAX_DEST_PORT_VALUE;
    }

    if ((CTC_MAP_GPORT_TO_LPORT(gport) >= max_port_num) && (!CTC_IS_CPU_PORT(gport)))
    {
        return CTC_E_INVALID_GLOBAL_PORT;
    }

    return CTC_E_NONE;
}



int32
sys_greatbelt_port_set_service_policer_en(uint8 lchip, uint16 gport, bool enable)
{
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    field_val = (TRUE == enable) ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_ServicePolicerValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_mac_auth(uint8 lchip, uint16 gport, bool enable)
{
    SYS_PORT_INIT_CHECK(lchip);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN, enable));
    CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, enable));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_mac_auth(uint8 lchip, uint16 gport, bool* enable)
{
    uint32 value1 = 0;
    uint32 value2 = 0;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(enable);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_SRC_MISMATCH_EXCEPTION_EN, &value1));
    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, &value2));

    *enable = ((value1 & value2) == 1) ? TRUE : FALSE;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_capability(uint8 lchip, uint16 gport, ctc_port_capability_type_t type, void* p_value)
{
    uint16 lport = 0;
    ctc_port_serdes_info_t* p_serdes_port_t = NULL;
    drv_datapath_port_capability_t port_cap;
    ctc_chip_serdes_info_t serdes_info;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get port capability, gport:0x%04X, type:%d!\n", gport, type);

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);

    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if(!port_cap.valid)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }
    switch (type)
    {
        case CTC_PORT_CAP_TYPE_SERDES_INFO:
            p_serdes_port_t = (ctc_port_serdes_info_t*)p_value;
            p_serdes_port_t->serdes_id = port_cap.serdes_id;
            if (DRV_SERDES_XAUI_MODE != port_cap.pcs_mode)
            {
                p_serdes_port_t->serdes_num = 1;
            }
            else
            {
                p_serdes_port_t->serdes_num = 4;
            }
            sal_memset(&serdes_info, 0, sizeof(serdes_info));
            serdes_info.serdes_id = port_cap.serdes_id;
            CTC_ERROR_RETURN(sys_greatbelt_chip_get_serdes_mode(lchip, &serdes_info));
            p_serdes_port_t->serdes_mode = serdes_info.serdes_mode;
            p_serdes_port_t->overclocking_speed = 0;
            break;

        case CTC_PORT_CAP_TYPE_MAC_ID:
            *(uint32*)p_value = port_cap.mac_id;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_get_mac_status(uint8 lchip, uint16 gport, uint32* p_value)
{
    uint16 lport     = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 sgmac_id = 0;
    uint32 value = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    drv_datapath_port_capability_t port_cap;

    SYS_PORT_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_value);
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        return CTC_E_NOT_READY;
    }
    else
    {
        type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;
        switch (type)
        {
            case CTC_PORT_MAC_GMAC:
                return CTC_E_NOT_SUPPORT;
                break;

            case CTC_PORT_MAC_SGMAC:
                sgmac_id = (0xFF != port_cap.mac_id)? ((port_cap.mac_id) - SYS_MAX_GMAC_PORT_NUM):(port_cap.mac_id);
                tbl_id = SgmacDebugStatus0_t + sgmac_id;
                field_id = SgmacDebugStatus_XgmiiRxState_f;
                cmd = DRV_IOR(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
                break;

            default:
                return CTC_E_INVALID_PARAM;
        }
    }
    *p_value = value;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_reset_mac_en(uint8 lchip, uint16 gport, uint32 value)
{
    uint16 lport     = 0;
    uint32 field_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint32 tbl_id = 0;
    uint32 cmd = 0;
    uint8 pcs_mode = 0;
    uint8 sgmac_id = 0;
    uint8 gmac_id = 0;
    ctc_port_mac_type_t type = CTC_PORT_MAC_MAX;
    drv_datapath_port_capability_t port_cap;
    sgmac_soft_rst_t sgmac_rst;

    SYS_PORT_INIT_CHECK(lchip);
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    value = value?1:0;
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        return CTC_E_NOT_READY;
    }
    else
    {
        pcs_mode = port_cap.pcs_mode;
        type = p_gb_port_master[lchip]->igs_port_prop[lport].port_mac_type;
        switch (type)
        {
            case CTC_PORT_MAC_GMAC:
                gmac_id = port_cap.mac_id;
                tbl_step1 = QuadMacGmac1SoftRst0_t - QuadMacGmac0SoftRst0_t;
                tbl_step2 = QuadMacGmac0SoftRst1_t - QuadMacGmac0SoftRst0_t;
                tbl_id = QuadMacGmac0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

                field_id = QuadMacGmac0SoftRst_Gmac0SgmiiRxSoftRst_f;
                cmd = DRV_IOW(tbl_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
                break;

            case CTC_PORT_MAC_SGMAC:
                sgmac_id = (0xFF != port_cap.mac_id)? ((port_cap.mac_id) - SYS_MAX_GMAC_PORT_NUM):(port_cap.mac_id);
                if (DRV_SERDES_XAUI_MODE == pcs_mode)
                {
                    tbl_id = SgmacSoftRst4_t + (sgmac_id - 4);
                    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
                    sgmac_rst.pcs_rx_soft_rst = value;
                    sgmac_rst.serdes_rx0_soft_rst = value;
                    sgmac_rst.serdes_rx1_soft_rst = value;
                    sgmac_rst.serdes_rx2_soft_rst = value;
                    sgmac_rst.serdes_rx3_soft_rst = value;
                    cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
                }
                else
                {
                    tbl_id = SgmacSoftRst0_t + sgmac_id;
                    cmd = DRV_IOW(tbl_id, SgmacSoftRst_PcsRxSoftRst_f);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
                }
                break;

            default:
                return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_store_led_mode_to_lport_attr(uint8 lchip, uint8 mac_id, uint8 first_led_mode, uint8 sec_led_mode)
{
    uint16 lport = 0;
    drv_datapath_port_capability_t* p_port_attr=NULL;

    /*get gport from datapath*/
    lport = sys_greatebelt_common_get_lport_with_mac(lchip, mac_id);
    if (lport == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }

    drv_greatbelt_get_port_attr(lchip, lport, &p_port_attr);
    if (!p_port_attr->valid)
    {
        return CTC_E_MAC_NOT_USED;
    }

    p_port_attr->first_led_mode = first_led_mode;
    p_port_attr->sec_led_mode = sec_led_mode;

    return CTC_E_NONE;
}


/*enable 1:link up txactiv  0:link down, foreoff*/
int32
sys_greatbelt_port_restore_led_mode(uint8 lchip, uint32 gport, uint32 enable)
{
    ctc_chip_led_para_t led_para;
    ctc_chip_mac_led_type_t led_type;
    uint8 first_led_mode = 0;
    uint8 sec_led_mode = 0;
    uint8 is_first_need = 0;
    uint8 is_sec_need = 0;
    uint16 lport = 0;
    drv_datapath_port_capability_t* p_port_attr=NULL;

    SYS_PORT_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    drv_greatbelt_get_port_attr(lchip, lport, &p_port_attr);
    if (!p_port_attr->valid)
    {
        return CTC_E_MAC_NOT_USED;
    }

    first_led_mode = p_port_attr->first_led_mode;
    sec_led_mode = p_port_attr->sec_led_mode;
    if ((CTC_CHIP_TXLINK_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_RXLINK_BIACTIVITY_MODE == first_led_mode) ||
        (CTC_CHIP_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_BIACTIVITY_MODE == first_led_mode))
    {
        is_first_need = 1;
    }

    if ((CTC_CHIP_TXLINK_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_RXLINK_BIACTIVITY_MODE == first_led_mode) ||
        (CTC_CHIP_TXACTIVITY_MODE == first_led_mode) || (CTC_CHIP_BIACTIVITY_MODE == first_led_mode))
    {
        is_sec_need = 1;
    }

    if (is_first_need || is_sec_need)
    {
        sal_memset(&led_para, 0, sizeof(ctc_chip_led_para_t));
        led_para.ctl_id = 0;
        led_para.lchip = lchip;
        led_para.port_id = p_port_attr->mac_id;
        led_para.op_flag |= CTC_CHIP_LED_MODE_SET_OP;
        led_para.first_mode = (is_first_need? (enable?first_led_mode:CTC_CHIP_FORCE_OFF_MODE):first_led_mode);
        led_para.sec_mode = (is_sec_need?(enable?sec_led_mode:CTC_CHIP_FORCE_OFF_MODE):sec_led_mode);
        led_type = (sec_led_mode !=  CTC_CHIP_MAC_LED_MODE)?CTC_CHIP_USING_TWO_LED:CTC_CHIP_USING_ONE_LED;
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_mac_led_mode(lchip, &led_para, led_type, 1));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_link_down_event(uint8 lchip, uint32 gport)
{

    /*disable interrupt*/
    CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LINK_INTRRUPT_EN, FALSE));

    CTC_ERROR_RETURN(sys_greatbelt_port_restore_led_mode(lchip, gport, FALSE));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_link_up_event(uint8 lchip, uint32 gport)
{
    uint32 value = 0;
    uint32 auto_neg_en = 0;
    uint32 auto_neg_mode = 0;
    uint8 speed = 0;
    uint8 index = 0;

    /*enable interrupt*/
    CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LINK_INTRRUPT_EN, TRUE));

    /*reset mac */
    for (index=0;index<10;index++)
    {
        sys_greatbelt_port_get_mac_status(lchip, gport, &value);
        if (value)
        {
            sal_task_sleep(10);
            CTC_ERROR_RETURN(sys_greatbelt_port_reset_mac_en(lchip, gport, TRUE));
            CTC_ERROR_RETURN(sys_greatbelt_port_reset_mac_en(lchip, gport, FALSE));
        }
        else
        {
            break;
        }
    }
    if (10 == index)
    {
        SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Reset Mac Over ten times!\n");
    }

    /*set speed 10M/100M/1G*/
    CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_SPEED, &value));
    if ((CTC_PORT_SPEED_1G == value) || (CTC_PORT_SPEED_100M == value) || (CTC_PORT_SPEED_10M == value))
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &auto_neg_en));
        CTC_ERROR_RETURN(sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_MODE, &auto_neg_mode));
        if (auto_neg_en && (CTC_PORT_AUTO_NEG_MODE_SGMII_SLAVER == auto_neg_mode))
        {
            sys_greatbelt_port_get_sgmii_auto_neg_speed(lchip, gport, &value);
            speed = value&0x3;
            if (0x00 == speed)
            {
                CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_SPEED, CTC_PORT_SPEED_10M));
            }
            else if (0x01 == speed)
            {
                CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_SPEED, CTC_PORT_SPEED_100M));
            }
            else if (0x02 == speed)
            {
                CTC_ERROR_RETURN(sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_SPEED, CTC_PORT_SPEED_1G));
            }
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_port_restore_led_mode(lchip, gport, TRUE));

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_mac_rx_en(uint8 lchip, uint32 gport, uint32 value)
{
    int32  ret = CTC_E_NONE;
    uint8  lport = 0;
    uint8  mac_id = 0;
    uint8  tbl_step1 = 0;
    uint8  tbl_step2 = 0;
    uint16 tbl_id = 0;
    uint16 field_id = 0;
    uint32 cmd = 0;
    uint32 field_value = 1;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if (lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    mac_id = SYS_GET_MAC_ID(lchip, gport);
    if (0xFF == mac_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }


    if (mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        tbl_step1 = QuadMacGmac1RxCtrl0_t - QuadMacGmac0RxCtrl0_t;
        tbl_step2 = QuadMacGmac0RxCtrl1_t - QuadMacGmac0RxCtrl0_t;
        tbl_id    = QuadMacGmac0RxCtrl0_t + (mac_id % 4) * tbl_step1 + (mac_id / 4) * tbl_step2;
        field_id  = QuadMacGmac0RxCtrl_Gmac0RxEnable_f;
    }
    else
    {
        tbl_step1 = SgmacCfg1_t - SgmacCfg0_t;
        tbl_id    = SgmacCfg0_t + (mac_id - SYS_MAX_GMAC_PORT_NUM) * tbl_step1;
        field_id  = SgmacCfg_RxEnable_f;
    }

    field_value = value ? 1 : 0;

    cmd = DRV_IOW(tbl_id, field_id);
    ret = DRV_IOCTL(lchip, 0, cmd, &field_value);

    return ret;
}

int32
sys_greatbelt_qmac_is_enable(uint8 lchip, uint8 index)
{
    uint8 sub_idx = 0;
    for (sub_idx = 0; sub_idx < 4; sub_idx++)
    {
        if (TRUE == SYS_MAC_IS_VALID(lchip, (index*4+sub_idx)))
        {
            return TRUE;
        }
    }

    return FALSE;
}

int32
sys_greatbelt_sgmac_is_enable(uint8 lchip, uint8 index)
{
    if (TRUE == SYS_MAC_IS_VALID(lchip, (index+SYS_MAX_GMAC_PORT_NUM)))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}
int32
sys_greatbelt_port_set_mac_stats_incrsaturate(uint8 lchip, uint8 stats_type, uint32 value)
{
    uint32 index = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    uint32 cmd = 0;

    PORT_LOCK;
    switch (stats_type)
    {
        case CTC_STATS_TYPE_FWD:
            /*DS STATS*/
            cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_SaturateEn_f);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
            break;

        case CTC_STATS_TYPE_GMAC:
            /*GMAC*/
            for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
            {
                if (sys_greatbelt_qmac_is_enable(lchip, index))
                {
                    reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
                    reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
                    cmd = DRV_IOW(reg_id, \
                                  QuadMacStatsCfg_IncrSaturate_f);
                    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
                }
            }
            break;

        case CTC_STATS_TYPE_SGMAC:
            /*SGMAC*/
            for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
            {
                if (sys_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
                {
                    reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
                    reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
                    cmd = DRV_IOW(reg_id, \
                                  SgmacStatsCfg_IncrSaturate_f);
                    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
                }
            }
            break;

        case CTC_STATS_TYPE_CPUMAC:
            /*CPU MAC*/
            cmd = DRV_IOW(CpuMacStatsUpdateCtl_t, CpuMacStatsUpdateCtl_IncrSaturate_f);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
            break;

        default:
            PORT_UNLOCK;
            return CTC_E_INVALID_PARAM;
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_set_mac_stats_clear(uint8 lchip, uint8 stats_type, uint32 value)
{
    uint32 index = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    uint32 cmd = 0;

    PORT_LOCK;
    switch (stats_type)
    {
    case CTC_STATS_TYPE_FWD:
        /*DS STATS*/
        cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_ClearOnRead_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
        break;

    case CTC_STATS_TYPE_GMAC:

        /*GMAC*/
        for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
        {
            if (sys_greatbelt_qmac_is_enable(lchip, index))
            {
                reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
                reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOW(reg_id, \
                              QuadMacStatsCfg_ClearOnRead_f);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
            }
        }

        break;

    case CTC_STATS_TYPE_SGMAC:

        /*SGMAC*/
        for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
        {
            if (sys_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
            {
                reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
                reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOW(reg_id, \
                              SgmacStatsCfg_ClearOnRead_f);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
            }
        }

        break;

    case CTC_STATS_TYPE_CPUMAC:
        /*CPU MAC*/
        cmd = DRV_IOW(CpuMacStatsUpdateCtl_t, CpuMacStatsUpdateCtl_ClearOnRead_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gb_port_master[lchip]->p_port_mutex);
        break;

    default:
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}
int32
sys_greatbelt_port_set_logic_port_check_en(uint8 lchip, uint32 gport, uint32 value)
{
    uint32 cmd = 0;
    uint16 lport = 0;

    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PORT_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set port 0x%xlogic port check value:%u\n", gport, value);

    SYS_PORT_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    value = (value) ? 1 : 0;
    cmd = DRV_IOW(DsDestPort_t, DsDestPort_LogicPortCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &value));

    return CTC_E_NONE;
}


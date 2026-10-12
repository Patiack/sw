/**
 @file sys_greatbelt_stats.c

 @date 2009-12-22

 @version v2.0

*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_stats.h"
#include "ctc_hash.h"
#include "sys_greatbelt_dma.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_l3if.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_register.h"

#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"
#include "greatbelt/include/drv_chip_info.h"
#include "greatbelt/include/drv_data_path.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_STATS_DEFAULT_WRR 2
#define SYS_STATS_DEFAULT_FIFO_DEPTH_THRESHOLD      15
#define SYS_STATS_DEFAULT_BYTE_THRESHOLD 0x1ff
#define SYS_STATS_DEFAULT_PACKET_THRESHOLD     0x1ff

#define SYS_STATS_MTU_PKT_MIN_LENGTH 1024
#define SYS_STATS_MTU_PKT_MAX_LENGTH 16383


#define SYS_STATS_MAX_FIFO_DEPTH 32
#define SYS_STATS_MAX_BYTE_CNT   4096
#define SYS_STATS_MAX_PKT_CNT    4096
#define SYS_STATS_MAX_NUM_FOR_14BIT 0x3FFF

#define SYS_STATS_COS_NUM       8

#define SYS_STATS_NO_PHB_SIZE           1
#define SYS_STATS_PHB_COLOR_SIZE        2
#define SYS_STATS_PHB_COS_SIZE          8
#define SYS_STATS_PHB_COLOR_COS_SIZE    16

#define SYS_STATS_FLOW_ENTRY_HASH_BLOCK_SIZE    256

#define IS_GMAC_STATS(mac_ram_type) \
    ((mac_ram_type == SYS_STATS_MAC_STATS_RAM0) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM1) || \
     (mac_ram_type == SYS_STATS_MAC_STATS_RAM2) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM3) || \
     (mac_ram_type == SYS_STATS_MAC_STATS_RAM4) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM5) || \
     (mac_ram_type == SYS_STATS_MAC_STATS_RAM6) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM7) || \
     (mac_ram_type == SYS_STATS_MAC_STATS_RAM8) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM9) || \
     (mac_ram_type == SYS_STATS_MAC_STATS_RAM10) || (mac_ram_type == SYS_STATS_MAC_STATS_RAM11))

#define IS_SGMAC_STATS(mac_ram_type) \
    ((mac_ram_type == SYS_STATS_SGMAC_STATS_RAM0) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM1) || \
     (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM2) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM3) || \
     (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM4) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM5) || \
     (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM6) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM7) || \
     (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM8) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM9) || \
     (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM10) || (mac_ram_type == SYS_STATS_SGMAC_STATS_RAM11))


#define SYS_STATS_TYPE_CHECK(type) \
    if ((type < CTC_STATS_TYPE_FWD) \
        || (type >= CTC_STATS_TYPE_MAX)) \
        return CTC_E_FEATURE_NOT_SUPPORT

/**
 @brief  Define PHB flag
*/
enum ctc_stats_phb_bitmap_e
{
    CTC_STATS_WITH_COLOR        = 0x0001,            /**< [GB] do stats distinguish color */
    CTC_STATS_WITH_COS          = 0x0002             /**< [GB] do stats distinguish cos */
};
typedef enum ctc_stats_phb_bitmap_e ctc_stats_phb_bitmap_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

union sys_macstats_u
{
    quad_mac_stats_ram_t gmac_stats;
    sgmac_stats_ram_t sgmac_stats;
};
typedef union sys_macstats_u sys_macstats_t;

struct sys_mac_stats_rx_s
{
    uint64 mac_stats_rx_bytes[SYS_STATS_MAC_RCV_MAX];
    uint64 mac_stats_rx_pkts[SYS_STATS_MAC_RCV_MAX];
};
typedef  struct sys_mac_stats_rx_s sys_mac_stats_rx_t;

struct sys_mac_stats_tx_s
{
    uint64 mac_stats_tx_bytes[SYS_STATS_MAC_SEND_MAX];
    uint64 mac_stats_tx_pkts[SYS_STATS_MAC_SEND_MAX];
};
typedef struct sys_mac_stats_tx_s sys_mac_stats_tx_t;

struct sys_gmac_stats_s
{
    sys_mac_stats_rx_t mac_stats_rx[GMAC_STATS_PORT_MAX];
    sys_mac_stats_tx_t mac_stats_tx[GMAC_STATS_PORT_MAX];
};
typedef  struct sys_gmac_stats_s sys_gmac_stats_t;

struct sys_sgmac_stats_s
{
    sys_mac_stats_rx_t mac_stats_rx;
    sys_mac_stats_tx_t mac_stats_tx;
};
typedef  struct sys_sgmac_stats_s sys_sgmac_stats_t;

struct sys_cpu_mac_stats_s
{
    uint64 cpu_mac_stats_data[SYS_STATS_CPU_MAC_MAX];
};
typedef struct sys_cpu_mac_stats_s sys_cpu_mac_stats_t;

struct sys_stats_statsid_s
{
    uint16 stats_ptr;
    uint32 stats_id;
    uint8  stats_id_type;
};
typedef struct sys_stats_statsid_s sys_stats_statsid_t;

struct sys_stats_master_s
{
    sal_mutex_t* p_stats_mutex;
    uint32 stats_bitmap;
    uint8 phb_bitmap;
    uint8 flow_stats_size;
    uint8 dma_enable;
    uint8 stats_mode;
    uint8 query_mode;

    uint16 igs_port_log_base;
    uint16 egs_port_log_base;

    uint32 policer_stats_confirm_base;
    uint32 policer_stats_exceed_base;
    uint32 policer_stats_violate_base;

    uint16 dequeue_stats_base;
    uint16 enqueue_stats_base;

    uint16 vrf_stats_base;
    uint8 rsv1[2];

    uint32 ingress_vlan_stats_base;
    uint32 egress_vlan_stats_base;

    uint8 saturate_en[CTC_STATS_TYPE_MAX];
    uint8 hold_en[CTC_STATS_TYPE_MAX];
    uint8 clear_read_en[CTC_STATS_TYPE_MAX];
    uint8 rsv2;

    uint8 stats_stored_in_sw;   /* only for mac stats */
    uint8 fifo_depth_threshold;
    uint16 pkt_cnt_threshold;
    uint16 byte_cnt_threshold;

    void *userdata;

    sys_gmac_stats_t gmac_stats_table[GMAC_STATS_RAM_MAX];
    sys_sgmac_stats_t sgmac_stats_table[SGMAC_STATS_RAM_MAX];
    sys_cpu_mac_stats_t cpu_mac_stats_table;
    ctc_stats_sync_fn_t dam_sync_mac_stats;

    ctc_hash_t* flow_stats_hash;
    ctc_hash_t* stats_statsid_hash;
};
typedef struct sys_stats_master_s sys_stats_master_t;

sys_stats_master_t* gb_stats_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_STATS_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == gb_stats_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)


extern int32
_sys_greatbelt_stats_get_flow_stats_without_phb(uint8 lchip,
                                                uint16 stats_ptr,
                                                ctc_stats_basic_t* p_stats);
extern int32
_sys_greatbelt_stats_clear_flow_stats_without_phb(uint8 lchip, uint16 stats_ptr);
/****************************************************************************
 *
* Function
*
*****************************************************************************/

#define ___________MAC_STATS_FUNCTION________________________
int32
drv_greatbelt_qmac_is_enable(uint8 lchip, uint8 index)
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
drv_greatbelt_sgmac_is_enable(uint8 lchip, uint8 index)
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

STATIC int32
_sys_greatbelt_stats_get_mac_ram_type(uint8 lchip, uint16 gport, uint8* ram_type)
{
    uint8 lport = 0;
    uint8 sgmac_id = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

     /*if (FALSE == IS_MAC_PORT(lchip, lport))*/
/*    if (FALSE == drv_greatbelt_is_mac_port(lchip, lport))
    {
        return CTC_E_STATS_PORT_NOT_ENABLE;
    }
*/
    if ((DRV_PORT_TYPE_1G != port_cap.port_type) &&
        (DRV_PORT_TYPE_SG != port_cap.port_type) &&
        (DRV_PORT_TYPE_CPU != port_cap.port_type))
    {
        return CTC_E_STATS_PORT_NOT_ENABLE;
    }


     /*if (TRUE == IS_GMAC_PORT(lchip, lport))*/
     /*if (TRUE == drv_greatbelt_is_gmac_port(lchip, lport))*/
    if (DRV_PORT_TYPE_1G == port_cap.port_type)
    {
        if (drv_greatbelt_qmac_is_enable(lchip, (port_cap.mac_id >> 2)))
        {
            *ram_type = port_cap.mac_id >> 2;
        }
        else
        {
            return CTC_E_STATS_PORT_NOT_ENABLE;
        }
    }
    else
    {
         /*if (TRUE == IS_SGMAC_PORT(lchip, lport))    // need consider 12 sgmac */
         /*if (TRUE == drv_greatbelt_is_sgmac_port(lchip, lport))*/
        if(DRV_PORT_TYPE_SG == port_cap.port_type)
        {
             /*CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_by_lport(lchip, lport, &sgmac_id))*/
            sgmac_id = port_cap.mac_id - 48;
             /*if (drv_greatbelt_sgmac_is_enable(lchip, sgmac_id))*/
            if (port_cap.valid)
            {
                *ram_type = sgmac_id + SYS_STATS_SGMAC_STATS_RAM0;
            }
            else
            {
                return CTC_E_STATS_PORT_NOT_ENABLE;
            }
        }
        else
        {
            *ram_type = SYS_STATS_CPUMAC_STATS_RAM;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16 length)
{
    uint8 lport = 0;
    uint8 mac_ram_type = 0;
    uint16 mtu2_len = 0;
    uint32 cmd = 0;
    uint32 tmp = 0;

    uint32 reg_id = 0;
    uint32 reg_step = 0;

    SYS_STATS_INIT_CHECK(lchip);
    tmp = length;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    CTC_MIN_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MIN_LENGTH);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    CTC_ERROR_RETURN(sys_greatbelt_stats_get_mac_packet_length_mtu2(lchip, gport, &mtu2_len));
    if (tmp >= mtu2_len)
    {
        return CTC_E_STATS_MTU1_GREATER_MTU2;
    }

    CTC_MAX_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MAX_LENGTH);

    PORT_LOCK;  /* must use port lock to protect from mac operation */
    if (mac_ram_type <= SYS_STATS_MAC_STATS_RAM11)
    {
        reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        reg_id = QuadMacStatsCfg0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu1_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
    }
    else if ((SYS_STATS_SGMAC_STATS_RAM0 <= mac_ram_type) && (mac_ram_type <= SYS_STATS_SGMAC_STATS_RAM11))
    {
        reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        reg_id = SgmacStatsCfg0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu1_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
    }
    else
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    p_gb_port_master[lchip]->pp_mtu1[mac_ram_type] = tmp;
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_mac_packet_length_mtu1(uint8 lchip, uint16 gport, uint16* p_length)
{
    uint8 lport = 0;
    uint8 mac_ram_type = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_length);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    PORT_LOCK;  /* must use port lock to protect from mac operation */
    if (mac_ram_type < (SYS_STATS_MAC_STATS_RAM_MAX - 1))
    {
        *p_length = p_gb_port_master[lchip]->pp_mtu1[mac_ram_type];
    }
    else
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16 length)
{
    uint8 lport = 0;
    uint8 mac_ram_type = 0;
    uint16 mtu1_len = 0;
    uint32 cmd = 0;
    uint32 tmp = 0;

    uint32 reg_id = 0;
    uint32 reg_step = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    tmp = length;

    CTC_MIN_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MIN_LENGTH);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    /*mtu2 length must greater than mtu1 length*/
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_mac_packet_length_mtu1(lchip, gport, &mtu1_len));
    if (tmp <= mtu1_len)
    {
        return CTC_E_STATS_MTU2_LESS_MTU1;
    }

    CTC_MAX_VALUE_CHECK(tmp, SYS_STATS_MTU_PKT_MAX_LENGTH);

    PORT_LOCK;  /* must use port lock to protect from mac operation */
    if (mac_ram_type <= SYS_STATS_MAC_STATS_RAM11)
    {
        reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
        reg_id = QuadMacStatsCfg0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu2_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
    }
    else if ((SYS_STATS_SGMAC_STATS_RAM0 <= mac_ram_type) && (mac_ram_type <= SYS_STATS_SGMAC_STATS_RAM11))
    {
        reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
        reg_id = SgmacStatsCfg0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu2_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
    }
    else
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    p_gb_port_master[lchip]->pp_mtu2[mac_ram_type] = tmp;
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_mac_packet_length_mtu2(uint8 lchip, uint16 gport, uint16* p_length)
{
    uint8 lport = 0;
    uint8 mac_ram_type = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_length);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    PORT_LOCK;  /* must use port lock to protect from mac operation */
    if (mac_ram_type < (SYS_STATS_MAC_STATS_RAM_MAX - 1))
    {
        *p_length = p_gb_port_master[lchip]->pp_mtu2[mac_ram_type];
    }
    else
    {
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }
    PORT_UNLOCK;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_mac_stats_to_basic(uint8 lchip, bool is_gmac, void* p_stats_ram, ctc_stats_basic_t* basic_stats)
{
    uint64 tmp = 0;
    sgmac_stats_ram_t* p_sgmac_stats_ram = NULL;
    quad_mac_stats_ram_t* p_quad_mac_stats_ram = NULL;

    CTC_PTR_VALID_CHECK(basic_stats);

    /*judge gmac or xgmac,sgmac through mac ram type*/
    if (TRUE == is_gmac)
    {
        p_quad_mac_stats_ram = (quad_mac_stats_ram_t*)p_stats_ram;

        tmp = p_quad_mac_stats_ram->frame_cnt_data_lo;
        basic_stats->packet_count = tmp;

        tmp = 0;
        tmp = p_quad_mac_stats_ram->byte_cnt_data_hi;
        tmp <<= 32;
        tmp |= p_quad_mac_stats_ram->byte_cnt_data_lo;
        basic_stats->byte_count = tmp;
    }
    else
    {
        p_sgmac_stats_ram = (sgmac_stats_ram_t*)p_stats_ram;

        tmp = p_sgmac_stats_ram->frame_cnt_data_hi;
        tmp <<= 32;
        tmp |= p_sgmac_stats_ram->frame_cnt_data_lo;
        basic_stats->packet_count = tmp;

        tmp = 0;
        tmp = p_sgmac_stats_ram->byte_cnt_data_hi;
        tmp <<= 32;
        tmp |= p_sgmac_stats_ram->byte_cnt_data_lo;
        basic_stats->byte_count = tmp;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_get_mac_stats_offset(uint8 lchip, uint8 sys_type, sys_stats_mac_cnt_type_t cnt_type, uint8* p_offset)
{
    /* The table mapper offset value against ctc_stats_mac_rec_t variable address based on stats_type and direction. */
    uint8 rx_mac_stats[SYS_STATS_MAC_RCV_NUM][SYS_STATS_MAC_CNT_TYPE_NUM] =
    {
        {SYS_STATS_MAP_MAC_RX_GOOD_UC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_UC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_MC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_MC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_BC_PKT,           SYS_STATS_MAP_MAC_RX_GOOD_BC_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_PKT, SYS_STATS_MAP_MAC_RX_GOOD_NORMAL_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_PKT,    SYS_STATS_MAP_MAC_RX_GOOD_PFC_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_PKT,      SYS_STATS_MAP_MAC_RX_GOOD_CONTROL_BYTE},
        {SYS_STATS_MAP_MAC_RX_FCS_ERROR_PKT,         SYS_STATS_MAP_MAC_RX_FCS_ERROR_BYTE},
        {SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_PKT,       SYS_STATS_MAP_MAC_RX_MAC_OVERRUN_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_63B_PKT,          SYS_STATS_MAP_MAC_RX_GOOD_63B_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_63B_PKT,           SYS_STATS_MAP_MAC_RX_BAD_63B_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_1519B_PKT,        SYS_STATS_MAP_MAC_RX_GOOD_1519B_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_1519B_PKT,         SYS_STATS_MAP_MAC_RX_BAD_1519B_BYTE},
        {SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_PKT,        SYS_STATS_MAP_MAC_RX_GOOD_JUMBO_BYTE},
        {SYS_STATS_MAP_MAC_RX_BAD_JUMBO_PKT,         SYS_STATS_MAP_MAC_RX_BAD_JUMBO_BYTE},
        {SYS_STATS_MAP_MAC_RX_64B_PKT,               SYS_STATS_MAP_MAC_RX_64B_BYTE},
        {SYS_STATS_MAP_MAC_RX_127B_PKT,              SYS_STATS_MAP_MAC_RX_127B_BYTE},
        {SYS_STATS_MAP_MAC_RX_255B_PKT,              SYS_STATS_MAP_MAC_RX_255B_BYTE},
        {SYS_STATS_MAP_MAC_RX_511B_PKT,              SYS_STATS_MAP_MAC_RX_511B_BYTE},
        {SYS_STATS_MAP_MAC_RX_1023B_PKT,             SYS_STATS_MAP_MAC_RX_1023B_BYTE},
        {SYS_STATS_MAP_MAC_RX_1518B_PKT,             SYS_STATS_MAP_MAC_RX_1518B_BYTE}
    };

    /* The table mapper offset value against ctc_stats_mac_snd_t variable address based on stats_type and direction. */
    uint8 tx_mac_stats[SYS_STATS_MAC_SEND_NUM][SYS_STATS_MAC_CNT_TYPE_NUM] =
    {
        {SYS_STATS_MAP_MAC_TX_GOOD_UC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_UC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_MC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_MC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_BC_PKT,      SYS_STATS_MAP_MAC_TX_GOOD_BC_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_PKT,   SYS_STATS_MAP_MAC_TX_GOOD_PAUSE_BYTE},
        {SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_PKT, SYS_STATS_MAP_MAC_TX_GOOD_CONTROL_BYTE},
        {SYS_STATS_MAP_MAC_TX_FCS_ERROR_PKT,    SYS_STATS_MAP_MAC_TX_FCS_ERROR_BYTE},
        {SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_PKT, SYS_STATS_MAP_MAC_TX_MAC_UNDERRUN_BYTE},
        {SYS_STATS_MAP_MAC_TX_63B_PKT,          SYS_STATS_MAP_MAC_TX_63B_BYTE},
        {SYS_STATS_MAP_MAC_TX_64B_PKT,          SYS_STATS_MAP_MAC_TX_64B_BYTE},
        {SYS_STATS_MAP_MAC_TX_127B_PKT,         SYS_STATS_MAP_MAC_TX_127B_BYTE},
        {SYS_STATS_MAP_MAC_TX_225B_PKT,         SYS_STATS_MAP_MAC_TX_225B_BYTE},
        {SYS_STATS_MAP_MAC_TX_511B_PKT,         SYS_STATS_MAP_MAC_TX_511B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1023B_PKT,        SYS_STATS_MAP_MAC_TX_1023B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1518B_PKT,        SYS_STATS_MAP_MAC_TX_1518B_BYTE},
        {SYS_STATS_MAP_MAC_TX_1519B_PKT,        SYS_STATS_MAP_MAC_TX_1519B_BYTE},
        {SYS_STATS_MAP_MAC_TX_JUMBO_PKT,        SYS_STATS_MAP_MAC_TX_JUMBO_BYTE}
    };

    if (sys_type < SYS_STATS_MAC_RCV_MAX)
    {
        *p_offset = rx_mac_stats[sys_type][cnt_type];
    }
    else
    {
        *p_offset = tx_mac_stats[sys_type - SYS_STATS_MAC_SEND_UCAST][cnt_type];
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_stats_get_mac_stats_tbl(uint8 lchip, uint8 mac_type, uint8 stats_type, uint16 lport, void** pp_mac_stats)
{
    if (IS_GMAC_STATS(mac_type))
    {
        if (stats_type < SYS_STATS_MAC_RCV_MAX)
        {
            *pp_mac_stats = &gb_stats_master[lchip]->gmac_stats_table[mac_type].mac_stats_rx[lport % 4];
        }
        else
        {
            *pp_mac_stats = &gb_stats_master[lchip]->gmac_stats_table[mac_type].mac_stats_tx[lport % 4];
        }
    }
    else if (IS_SGMAC_STATS(mac_type))
    {
        if (stats_type < SYS_STATS_MAC_RCV_MAX)
        {
            *pp_mac_stats = &gb_stats_master[lchip]->sgmac_stats_table[mac_type - GMAC_STATS_RAM_MAX].mac_stats_rx;
        }
        else
        {
            *pp_mac_stats = &gb_stats_master[lchip]->sgmac_stats_table[mac_type - GMAC_STATS_RAM_MAX].mac_stats_tx;
        }
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_write_sys_mac_stats(uint8 lchip, uint8 stats_type, void* p_mac_stats, ctc_stats_basic_t* p_stats_basic)
{
    sys_mac_stats_rx_t* p_mac_stats_rx = NULL;
    sys_mac_stats_tx_t* p_mac_stats_tx = NULL;

    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        p_mac_stats_rx = (sys_mac_stats_rx_t*)p_mac_stats;

        p_mac_stats_rx->mac_stats_rx_pkts[stats_type] += p_stats_basic->packet_count;
        p_mac_stats_rx->mac_stats_rx_bytes[stats_type] += p_stats_basic->byte_count;
    }
    else
    {
        p_mac_stats_tx = (sys_mac_stats_tx_t*)p_mac_stats;
        p_mac_stats_tx->mac_stats_tx_pkts[stats_type - SYS_STATS_MAC_SEND_UCAST] += p_stats_basic->packet_count;
        p_mac_stats_tx->mac_stats_tx_bytes[stats_type - SYS_STATS_MAC_SEND_UCAST] += p_stats_basic->byte_count;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_write_ctc_mac_stats(uint8 lchip, uint8 sys_type, void* p_ctc_stats, ctc_stats_basic_t* p_stats_basic)
{
    uint8  offset = 0;
    uint8  cnt_type = 0;
    uint64* p_val[SYS_STATS_MAC_CNT_TYPE_NUM] = {&p_stats_basic->packet_count, &p_stats_basic->byte_count};

    for (cnt_type = 0; cnt_type < SYS_STATS_MAC_CNT_TYPE_NUM; cnt_type++)
    {
        _sys_greatbelt_stats_get_mac_stats_offset(lchip, sys_type, cnt_type, &offset);
        ((uint64*)p_ctc_stats)[offset] = *(p_val[cnt_type]);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_sync_ctc_mac_stats(uint8 lchip, uint8 stats_type, void* p_ctc_stats, void* p_mac_stats)
{
    ctc_stats_basic_t stats_basic;

    sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));
    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        stats_basic.packet_count = ((sys_mac_stats_rx_t*)p_mac_stats)->mac_stats_rx_pkts[stats_type];
        stats_basic.byte_count = ((sys_mac_stats_rx_t*)p_mac_stats)->mac_stats_rx_bytes[stats_type];
    }
    else
    {
        stats_basic.packet_count = ((sys_mac_stats_tx_t*)p_mac_stats)->mac_stats_tx_pkts[stats_type - SYS_STATS_MAC_RCV_MAX];
        stats_basic.byte_count = ((sys_mac_stats_tx_t*)p_mac_stats)->mac_stats_tx_bytes[stats_type - SYS_STATS_MAC_RCV_MAX];
    }
    CTC_ERROR_RETURN(_sys_greatbelt_stats_write_ctc_mac_stats(lchip, stats_type, p_ctc_stats, &stats_basic));

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_mac_rx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats)
{
    bool   is_gmac = FALSE;
    void*  p_mac_stats = NULL;
    uint8  lport = 0;
    uint8  mac_ram_type = 0;
    uint8  stats_type;
    uint32 base = 0;
    uint32 cmdr = 0;
    uint32 cmdw = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step = 0;
    sys_macstats_t mac_stats;
    ctc_stats_basic_t stats_basic;
    ctc_stats_basic_t zero_stats_basic;
    quad_mac_stats_ram_t quad_mac_stats_ram;
    drv_work_platform_type_t platform_type = 0;
    ctc_stats_mac_rec_t stats_mac_rec;
    ctc_stats_mac_rec_t* p_stats_mac_rec = NULL;
    ctc_stats_mac_rec_plus_t* p_rx_stats_plus = NULL;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    STATS_LOCK;

    sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
    sal_memset(&stats_mac_rec, 0, sizeof(ctc_stats_mac_rec_t));
    sal_memset(&quad_mac_stats_ram, 0, sizeof(quad_mac_stats_ram_t));
    sal_memset(&p_stats->u, 0, sizeof(p_stats->u));
    p_stats_mac_rec = &(p_stats->u.stats_detail.stats.rx_stats);
    p_rx_stats_plus = &(p_stats->u.stats_plus.stats.rx_stats_plus);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    if (IS_GMAC_STATS(mac_ram_type))
    {
        tbl_step = QuadMacStatsRam1_t - QuadMacStatsRam0_t;
        tbl_id = QuadMacStatsRam0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * tbl_step;
    }
    else if (IS_SGMAC_STATS(mac_ram_type))
    {
        tbl_step = SgmacStatsRam1_t - SgmacStatsRam0_t;
        tbl_id = SgmacStatsRam0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * tbl_step;
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, base);

    base = (IS_GMAC_STATS(mac_ram_type)) ? (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (MAC_BASED_STATS_GMAC_RAM_DEPTH / 4) : 0;

    is_gmac = (IS_GMAC_STATS(mac_ram_type)) ? TRUE : FALSE;
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(drv_greatbelt_get_platform_type(&platform_type));

    for (stats_type = SYS_STATS_MAC_RCV_GOOD_UCAST; stats_type < SYS_STATS_MAC_RCV_MAX; stats_type++)
    {
        sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));
        sal_memset(&zero_stats_basic, 0, sizeof(ctc_stats_basic_t));

        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + base, cmdr, &mac_stats));

        /* for uml, add this code to support clear after read */
        if (SW_SIM_PLATFORM == platform_type)
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + base, cmdw, &quad_mac_stats_ram));
            SYS_STATS_DBG_INFO("clear %s[%d] by software in uml\n", TABLE_NAME(tbl_id), stats_type + base);
        }

        if ((TRUE == gb_stats_master[lchip]->stats_stored_in_sw) && (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO))
        {
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, p_mac_stats));

        }
        else if ((TRUE == gb_stats_master[lchip]->stats_stored_in_sw) && (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_POLL))
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &zero_stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, p_mac_stats));
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
        }
        else
        {
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_ctc_mac_stats(lchip, stats_type, &stats_mac_rec, &stats_basic));
        }
    }

    /*plus*/
    p_rx_stats_plus->ucast_pkts = stats_mac_rec.good_ucast_pkts;
    p_rx_stats_plus->all_octets = stats_mac_rec.good_63_bytes + \
                                  stats_mac_rec.bad_63_bytes + \
                                  stats_mac_rec.bytes_64 + \
                                  stats_mac_rec.bytes_65_to_127 + \
                                  stats_mac_rec.bytes_128_to_255 + \
                                  stats_mac_rec.bytes_256_to_511 + \
                                  stats_mac_rec.bytes_512_to_1023 + \
                                  stats_mac_rec.bytes_1024_to_1518 + \
                                  stats_mac_rec.good_1519_bytes + \
                                  stats_mac_rec.bad_1519_bytes + \
                                  stats_mac_rec.good_jumbo_bytes + \
                                  stats_mac_rec.bad_jumbo_bytes;
    p_rx_stats_plus->all_pkts = stats_mac_rec.good_63_pkts + \
                                stats_mac_rec.bad_63_pkts + \
                                stats_mac_rec.pkts_64 + \
                                stats_mac_rec.pkts_65_to_127 + \
                                stats_mac_rec.pkts_128_to_255 + \
                                stats_mac_rec.pkts_256_to_511 + \
                                stats_mac_rec.pkts_512_to_1023 + \
                                stats_mac_rec.pkts_1024_to_1518 + \
                                stats_mac_rec.good_1519_pkts + \
                                stats_mac_rec.bad_1519_pkts + \
                                stats_mac_rec.good_jumbo_pkts + \
                                stats_mac_rec.bad_jumbo_pkts;
    p_rx_stats_plus->bcast_pkts = stats_mac_rec.good_bcast_pkts;
    p_rx_stats_plus->crc_pkts = stats_mac_rec.fcs_error_pkts;
    p_rx_stats_plus->drop_events = stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->error_pkts = stats_mac_rec.fcs_error_pkts + \
                                  stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->fragments_pkts = stats_mac_rec.good_63_pkts + stats_mac_rec.bad_63_pkts;
    p_rx_stats_plus->giants_pkts = stats_mac_rec.good_1519_pkts+ stats_mac_rec.good_jumbo_pkts;
    p_rx_stats_plus->jumbo_events = stats_mac_rec.good_jumbo_pkts + stats_mac_rec.bad_jumbo_pkts;
    p_rx_stats_plus->mcast_pkts = stats_mac_rec.good_mcast_pkts;
    p_rx_stats_plus->overrun_pkts = stats_mac_rec.mac_overrun_pkts;
    p_rx_stats_plus->pause_pkts = stats_mac_rec.good_normal_pause_pkts + stats_mac_rec.good_pfc_pause_pkts;
    p_rx_stats_plus->runts_pkts = stats_mac_rec.good_63_pkts;

    /*detail*/
    sal_memcpy(p_stats_mac_rec, &stats_mac_rec, sizeof(ctc_stats_mac_rec_t));
    p_stats_mac_rec->good_oversize_pkts = p_stats_mac_rec->good_1519_pkts + p_stats_mac_rec->good_jumbo_pkts;
    p_stats_mac_rec->good_oversize_bytes = p_stats_mac_rec->good_1519_bytes + p_stats_mac_rec->good_jumbo_bytes;
    p_stats_mac_rec->good_undersize_pkts = p_stats_mac_rec->good_63_pkts;
    p_stats_mac_rec->good_undersize_bytes = p_stats_mac_rec->good_63_bytes;
    p_stats_mac_rec->good_pause_pkts = p_stats_mac_rec->good_normal_pause_pkts + p_stats_mac_rec->good_pfc_pause_pkts;
    p_stats_mac_rec->good_pause_bytes = p_stats_mac_rec->good_normal_pause_bytes + p_stats_mac_rec->good_pfc_pause_bytes;

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_mac_rx_stats(uint8 lchip, uint16 gport)
{
    uint8  lport = 0;
    uint8  mac_ram_type = 0;
    int32  base = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    sgmac_stats_ram_t sgmac_stats_ram;
    quad_mac_stats_ram_t quad_mac_stats_ram;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    STATS_LOCK;

    SYS_STATS_DBG_FUNC();

    sal_memset(&quad_mac_stats_ram, 0, sizeof(quad_mac_stats_ram_t));
    sal_memset(&sgmac_stats_ram, 0, sizeof(sgmac_stats_ram_t));

    /*get mac index ,port id of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    if (IS_GMAC_STATS(mac_ram_type))
    {
        reg_step = QuadMacStatsRam1_t - QuadMacStatsRam0_t;
        reg_id = QuadMacStatsRam0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);

        base = (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (MAC_BASED_STATS_GMAC_RAM_DEPTH / 4);
        SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, base);

        for (index = SYS_STATS_MAC_RCV_GOOD_UCAST + base; index < SYS_STATS_MAC_RCV_MAX + base; index++)
        {
            if (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &quad_mac_stats_ram));
            }
        }
        sal_memset(&gb_stats_master[lchip]->gmac_stats_table[mac_ram_type].mac_stats_rx[gport % 4], 0, sizeof(sys_mac_stats_rx_t));
    }
    else if (IS_SGMAC_STATS(mac_ram_type))
    {
        reg_step = SgmacStatsRam1_t - SgmacStatsRam0_t;
        reg_id = SgmacStatsRam0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);

        SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, 0);

        for (index = SYS_STATS_MAC_RCV_GOOD_UCAST; index < SYS_STATS_MAC_RCV_MAX; index++)
        {
            if (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &sgmac_stats_ram));
            }
        }
        sal_memset(&gb_stats_master[lchip]->sgmac_stats_table[mac_ram_type - GMAC_STATS_RAM_MAX].mac_stats_rx, 0, sizeof(sys_mac_stats_rx_t));
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_mac_tx_stats(uint8 lchip, uint16 gport, ctc_mac_stats_t* p_stats)
{
    bool   is_gmac = FALSE;
    void*  p_mac_stats = NULL;
    uint8  stats_type = 0;
    uint8  lport = 0;
    uint8  mac_ram_type = 0;
    int32  base = 0;
    uint32 cmdr = 0;
    uint32 cmdw = 0;
    uint32 tbl_id = 0;
    uint32 tbl_step = 0;
    sys_macstats_t mac_stats;
    ctc_stats_basic_t stats_basic;
    ctc_stats_basic_t zero_stats_basic;
    quad_mac_stats_ram_t quad_mac_stats_ram;
    drv_work_platform_type_t platform_type = 0;
    ctc_stats_mac_snd_t stats_mac_snd;
    ctc_stats_mac_snd_t* p_stats_mac_snd = NULL;
    ctc_stats_mac_snd_plus_t* p_tx_stats_plus = NULL;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    STATS_LOCK;

    sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
    sal_memset(&stats_mac_snd, 0, sizeof(ctc_stats_mac_snd_t));
    sal_memset(&quad_mac_stats_ram, 0, sizeof(quad_mac_stats_ram_t));
    sal_memset(&p_stats->u, 0, sizeof(p_stats->u));
    p_stats_mac_snd = &(p_stats->u.stats_detail.stats.tx_stats);
    p_tx_stats_plus = &(p_stats->u.stats_plus.stats.tx_stats_plus);

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    if (IS_GMAC_STATS(mac_ram_type))
    {
        tbl_step = QuadMacStatsRam1_t - QuadMacStatsRam0_t;
        tbl_id = QuadMacStatsRam0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * tbl_step;
    }
    else if (IS_SGMAC_STATS(mac_ram_type))
    {
        tbl_step = SgmacStatsRam1_t - SgmacStatsRam0_t;
        tbl_id = SgmacStatsRam0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * tbl_step;
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    cmdr = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    cmdw = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, base);

     base = (IS_GMAC_STATS(mac_ram_type)) ? (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (MAC_BASED_STATS_GMAC_RAM_DEPTH / 4) : 0;
    is_gmac = (IS_GMAC_STATS(mac_ram_type)) ? TRUE : FALSE;
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(drv_greatbelt_get_platform_type(&platform_type));

    for (stats_type = SYS_STATS_MAC_SEND_UCAST; stats_type < SYS_STATS_MAC_SEND_MAX; stats_type++)
    {
        sal_memset(&mac_stats, 0, sizeof(sys_macstats_t));
        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));
        sal_memset(&zero_stats_basic, 0, sizeof(ctc_stats_basic_t));

        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + base, cmdr, &mac_stats));

        /* for uml, add this code to support clear after read */
        if (SW_SIM_PLATFORM == platform_type)
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, stats_type + base, cmdw, &quad_mac_stats_ram));
            SYS_STATS_DBG_INFO("clear %s[%d] by software in uml\n", TABLE_NAME(tbl_id),  stats_type + base);
        }


        if ((TRUE == gb_stats_master[lchip]->stats_stored_in_sw) && (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO))
        {
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, p_mac_stats));
        }
        else if ((TRUE == gb_stats_master[lchip]->stats_stored_in_sw) && (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_POLL))
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &zero_stats_basic));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_sync_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, p_mac_stats));
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_ram_type, stats_type, gport, &p_mac_stats));
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
        }
        else
        {
            _sys_greatbelt_stats_mac_stats_to_basic(lchip, is_gmac, &mac_stats, &stats_basic);
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_ctc_mac_stats(lchip, stats_type, &stats_mac_snd, &stats_basic));
        }
    }

    /*plus*/
    p_tx_stats_plus->all_octets = stats_mac_snd.bytes_63 + \
                                  stats_mac_snd.bytes_64 + \
                                  stats_mac_snd.bytes_65_to_127 + \
                                  stats_mac_snd.bytes_128_to_255 + \
                                  stats_mac_snd.bytes_256_to_511 + \
                                  stats_mac_snd.bytes_512_to_1023 + \
                                  stats_mac_snd.bytes_1024_to_1518 + \
                                  stats_mac_snd.bytes_1519 + \
                                  stats_mac_snd.jumbo_bytes;
    p_tx_stats_plus->all_pkts = stats_mac_snd.pkts_63 + \
                                stats_mac_snd.pkts_64 + \
                                stats_mac_snd.pkts_65_to_127 + \
                                stats_mac_snd.pkts_128_to_255 + \
                                stats_mac_snd.pkts_256_to_511 + \
                                stats_mac_snd.pkts_512_to_1023 + \
                                stats_mac_snd.pkts_1024_to_1518 + \
                                stats_mac_snd.pkts_1519 + \
                                stats_mac_snd.jumbo_pkts;
    p_tx_stats_plus->bcast_pkts = stats_mac_snd.good_bcast_pkts;
    p_tx_stats_plus->error_pkts = stats_mac_snd.fcs_error_pkts;
    p_tx_stats_plus->jumbo_events = stats_mac_snd.jumbo_pkts;
    p_tx_stats_plus->mcast_pkts = stats_mac_snd.good_mcast_pkts;
    p_tx_stats_plus->ucast_pkts = stats_mac_snd.good_ucast_pkts;
    p_tx_stats_plus->underruns_pkts = stats_mac_snd.mac_underrun_pkts;

    /*detail*/
    sal_memcpy(p_stats_mac_snd, &stats_mac_snd, sizeof(ctc_stats_mac_snd_t));
    /* some indirect plus or minus*/

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_mac_tx_stats(uint8 lchip, uint16 gport)
{
    uint8  lport = 0;
    uint8  mac_ram_type = 0;
    int32  base = 0;
    uint16 index = 0;
    uint32 cmd = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;
    sgmac_stats_ram_t sgmac_stats_ram;
    quad_mac_stats_ram_t quad_mac_stats_ram;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    STATS_LOCK;

    sal_memset(&quad_mac_stats_ram, 0, sizeof(quad_mac_stats_ram_t));
    sal_memset(&sgmac_stats_ram, 0, sizeof(sgmac_stats_ram_t));

    SYS_STATS_DBG_FUNC();

    /*get mac index ,channel of lport from function to justify mac ram*/
    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_ram_type(lchip, gport, &mac_ram_type));

    if (IS_GMAC_STATS(mac_ram_type))
    {
        reg_step = QuadMacStatsRam1_t - QuadMacStatsRam0_t;
        reg_id = QuadMacStatsRam0_t + (mac_ram_type - SYS_STATS_MAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);

        base = (SYS_GET_MAC_ID(lchip, gport) & 0x3) * (MAC_BASED_STATS_GMAC_RAM_DEPTH / 4);
        SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, base);

        for (index = SYS_STATS_MAC_SEND_UCAST + base; index < SYS_STATS_MAC_SEND_MAX + base; index++)
        {
            if (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &quad_mac_stats_ram));
            }
        }
        sal_memset(&gb_stats_master[lchip]->gmac_stats_table[mac_ram_type].mac_stats_tx[gport % 4], 0, sizeof(sys_mac_stats_tx_t));
    }
    else if (IS_SGMAC_STATS(mac_ram_type))
    {
        reg_step = SgmacStatsRam1_t - SgmacStatsRam0_t;
        reg_id = SgmacStatsRam0_t + (mac_ram_type - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
        cmd = DRV_IOR(reg_id, DRV_ENTRY_FLAG);

        SYS_STATS_DBG_INFO("mac_ram_type:%d, lport:%d, base:%d\n", mac_ram_type, lport, 0);

        for (index = SYS_STATS_MAC_SEND_UCAST; index < SYS_STATS_MAC_SEND_MAX; index++)
        {
            if (gb_stats_master[lchip]->query_mode == CTC_STATS_QUERY_MODE_IO)
            {
                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(DRV_IOCTL(lchip, index, cmd, &sgmac_stats_ram));
            }
        }
        sal_memset(&gb_stats_master[lchip]->sgmac_stats_table[mac_ram_type - GMAC_STATS_RAM_MAX].mac_stats_tx, 0, sizeof(sys_mac_stats_tx_t));
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_cpu_mac_stats(uint8 lchip, uint16 gport, ctc_cpu_mac_stats_t* p_stats)
{
    uint32 cmd = 0;
    cpu_mac_stats_t cpu_mac_stats;
    drv_work_platform_type_t platform_type = 0;
    sys_cpu_mac_stats_t* p_cpu_mac_stats_tbl = NULL;
    ctc_stats_cpu_mac_t* p_cpu_mac = &(p_stats->cpu_mac_stats);

    sal_memset(p_cpu_mac, 0, sizeof(ctc_stats_cpu_mac_t));

    SYS_STATS_INIT_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(CTC_MAP_GPORT_TO_GCHIP(gport), lchip);

    sal_memset(&cpu_mac_stats, 0, sizeof(cpu_mac_stats_t));
    cmd = DRV_IOR(CpuMacStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_stats));

    /* for uml, add this code to support clear after read */
    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));
    if (SW_SIM_PLATFORM == platform_type)
    {
        sal_memset(&cpu_mac_stats, 0, sizeof(cpu_mac_stats_t));
        cmd = DRV_IOR(CpuMacStats_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_stats));
        SYS_STATS_DBG_INFO("clear %s by software in uml\n", TABLE_NAME(CpuMacStats_t));
    }

    if (TRUE == gb_stats_master[lchip]->stats_stored_in_sw)
    {
        p_cpu_mac_stats_tbl = &gb_stats_master[lchip]->cpu_mac_stats_table;

        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_GOOD_PKT] += cpu_mac_stats.mac_rx_good_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_GOOD_BYTE] += cpu_mac_stats.mac_rx_good_bytes_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_BAD_PKT] += cpu_mac_stats.mac_rx_bad_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_BAD_BYTE] += cpu_mac_stats.mac_rx_bad_bytes_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_FCS_ERROR] += cpu_mac_stats.mac_rx_fcs_error_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_FRAGMENT] += cpu_mac_stats.mac_rx_fragment_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_OVERRUN] += cpu_mac_stats.mac_rx_overrun_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_TOTAL_PKT] += cpu_mac_stats.mac_tx_total_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_TOTAL_BYTE] += cpu_mac_stats.mac_tx_total_bytes_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_FCS_ERROR] += cpu_mac_stats.mac_tx_fcs_error_frames_cnt;
        p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_UNDERRUN] += cpu_mac_stats.mac_tx_underrun_frames_cnt;

        p_cpu_mac->cpu_mac_rx_good_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_GOOD_PKT];
        p_cpu_mac->cpu_mac_rx_good_bytes = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_GOOD_BYTE];
        p_cpu_mac->cpu_mac_rx_bad_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_BAD_PKT];
        p_cpu_mac->cpu_mac_rx_bad_bytes = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_BAD_BYTE];
        p_cpu_mac->cpu_mac_rx_fcs_error_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_FCS_ERROR];
        p_cpu_mac->cpu_mac_rx_fragment_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_FRAGMENT];
        p_cpu_mac->cpu_mac_rx_overrun_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_RCV_OVERRUN];
        p_cpu_mac->cpu_mac_tx_total_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_TOTAL_PKT];
        p_cpu_mac->cpu_mac_tx_total_bytes = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_TOTAL_BYTE];
        p_cpu_mac->cpu_mac_tx_fcs_error_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_FCS_ERROR];
        p_cpu_mac->cpu_mac_tx_underrun_pkts = p_cpu_mac_stats_tbl->cpu_mac_stats_data[SYS_STATS_CPU_MAC_SEND_UNDERRUN];
    }
    else
    {
        p_cpu_mac->cpu_mac_rx_good_pkts = cpu_mac_stats.mac_rx_good_frames_cnt;
        p_cpu_mac->cpu_mac_rx_good_bytes = cpu_mac_stats.mac_rx_good_bytes_cnt;
        p_cpu_mac->cpu_mac_rx_bad_pkts = cpu_mac_stats.mac_rx_bad_frames_cnt;
        p_cpu_mac->cpu_mac_rx_bad_bytes = cpu_mac_stats.mac_rx_bad_bytes_cnt;
        p_cpu_mac->cpu_mac_rx_fcs_error_pkts = cpu_mac_stats.mac_rx_fcs_error_frames_cnt;
        p_cpu_mac->cpu_mac_rx_fragment_pkts = cpu_mac_stats.mac_rx_fragment_frames_cnt;
        p_cpu_mac->cpu_mac_rx_overrun_pkts = cpu_mac_stats.mac_rx_overrun_frames_cnt;
        p_cpu_mac->cpu_mac_tx_total_pkts = cpu_mac_stats.mac_tx_total_frames_cnt;
        p_cpu_mac->cpu_mac_tx_total_bytes = cpu_mac_stats.mac_tx_total_bytes_cnt;
        p_cpu_mac->cpu_mac_tx_fcs_error_pkts = cpu_mac_stats.mac_tx_fcs_error_frames_cnt;
        p_cpu_mac->cpu_mac_tx_underrun_pkts = cpu_mac_stats.mac_tx_underrun_frames_cnt;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_cpu_mac_stats(uint8 lchip, uint16 gport)
{
    uint32 cmd = 0;
    cpu_mac_stats_t cpu_mac_stats;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_MAP_GCHIP_TO_LCHIP(CTC_MAP_GPORT_TO_GCHIP(gport), lchip);

    sal_memset(&cpu_mac_stats, 0, sizeof(cpu_mac_stats_t));

    /* CpuMacStats_t is read only, use read on clear instead of write */
    cmd = DRV_IOW(CpuMacStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cpu_mac_stats));

    sal_memset(&gb_stats_master[lchip]->cpu_mac_stats_table, 0, sizeof(sys_cpu_mac_stats_t));

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_register_cb(uint8 lchip, ctc_stats_sync_fn_t cb, void* userdata)
{
    gb_stats_master[lchip]->dam_sync_mac_stats = cb;
    gb_stats_master[lchip]->userdata = userdata;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_get_dam_sync_mac_stats(uint8 lchip, uint8 mac_type, uint8 stats_type, ctc_mac_stats_t* sys_dma_stats_sync, ctc_stats_basic_t* p_stats_basic)
{
    ctc_stats_mac_rec_t* p_rx_mac_stats = NULL;
    ctc_stats_mac_snd_t* p_tx_mac_stats = NULL;
    if (stats_type < SYS_STATS_MAC_RCV_MAX)
    {
        p_rx_mac_stats = &(sys_dma_stats_sync->u.stats_detail.stats.rx_stats);
        CTC_ERROR_RETURN(_sys_greatbelt_stats_write_ctc_mac_stats(lchip, stats_type, p_rx_mac_stats, p_stats_basic));
    }
    else
    {
        p_tx_mac_stats = &(sys_dma_stats_sync->u.stats_detail.stats.tx_stats);
        CTC_ERROR_RETURN(_sys_greatbelt_stats_write_ctc_mac_stats(lchip, stats_type, p_tx_mac_stats, p_stats_basic));
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_stats_sync_dma_mac_stats(uint8  lchip, sys_stats_dma_sync_t* p_dma_sync, uint16 bitmap)
{
    void*  p_addr = NULL;
    void*  p_mac_stats = NULL;
    uint8  mac_type = 0;
    uint8  mac_idx = 0;
    uint8  quad_idx = 0;
    uint8  stats_type = 0;
    uint8  quad_count = 0;
    uint8  sgmac_count = 0;
    uint16 mac_offset = 0;
    uint16 stats_offset = 0;
    uint16 lport = 0;
    uint16 port_id = 0;
    uint8  gchip_id = 0;
    uint8  port_cnt = 0;
    uint8  dma_sync_en = 0;
    uint8  mac_id = 0;
    sgmac_stats_ram_t    sgmac_stats_ram;
    ctc_stats_basic_t    stats_basic;
    quad_mac_stats_ram_t quad_mac_stats_ram;
    static ctc_stats_mac_stats_sync_t system_dma_stats;
    drv_datapath_port_capability_t port_cap;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_dma_sync);

    STATS_LOCK;

    sal_memset(&system_dma_stats, 0, sizeof(ctc_stats_mac_stats_sync_t));
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    dma_sync_en = (gb_stats_master[lchip]->dam_sync_mac_stats)? 1 : 0;

    if (CTC_STATS_TYPE_GMAC == p_dma_sync->stats_type)
    {
        for (quad_idx = 0; quad_idx < GMAC_STATS_RAM_MAX; quad_idx++)
        {
            mac_type = SYS_STATS_MAC_STATS_RAM0 + quad_idx;
            if (IS_BIT_SET(bitmap, quad_idx))
            {
                p_addr = (uint8*)p_dma_sync->p_base_addr + quad_count * (MAC_BASED_STATS_GMAC_RAM_DEPTH * DRV_ADDR_BYTES_PER_ENTRY);

                for (mac_idx = 0; mac_idx < GMAC_STATS_PORT_MAX; mac_idx++)
                {
                    mac_offset = mac_idx * (MAC_BASED_STATS_GMAC_RAM_DEPTH / GMAC_STATS_PORT_MAX) * DRV_ADDR_BYTES_PER_ENTRY;
                    lport = quad_idx * GMAC_STATS_RAM_MAX + mac_idx;
                    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

                    if (dma_sync_en)
                    {
                        mac_id = GMAC_STATS_PORT_MAX * quad_idx + mac_idx;
                        port_id = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
                        if (0xFF == port_id)
                        {
                            continue;
                        }
                    }

                    for (stats_type = 0; stats_type < SYS_STATS_MAC_STATS_TYPE_NUM; stats_type++)
                    {
                        stats_offset = stats_type * DRV_ADDR_BYTES_PER_ENTRY;

                        sal_memcpy(&quad_mac_stats_ram, (uint8*)p_addr + mac_offset + stats_offset, sizeof(quad_mac_stats_ram_t));
                        sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));

                        _sys_greatbelt_stats_mac_stats_to_basic(lchip, TRUE, &quad_mac_stats_ram, &stats_basic);
                        if (dma_sync_en)
                        {
                            drv_greatbelt_get_port_capability(lchip, port_id, &port_cap);
                            if (port_cap.valid)
                            {
                                system_dma_stats.stats[port_cnt].stats_mode = 1;
                                sys_greatbelt_get_gchip_id(lchip, &gchip_id);
                                system_dma_stats.gport[port_cnt] = CTC_MAP_LPORT_TO_GPORT(gchip_id, port_id);
                                CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_dam_sync_mac_stats \
                                                 (lchip, p_dma_sync->stats_type, stats_type, &(system_dma_stats.stats[port_cnt]), &stats_basic));
                            }
                        }
                        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_type, stats_type, lport, &p_mac_stats));
                        CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));

                    }
                    port_cnt = port_cnt + 1;
                    system_dma_stats.valid_cnt = port_cnt;
                }
                quad_count++;
            }
        }
        if (dma_sync_en)
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(gb_stats_master[lchip]->dam_sync_mac_stats(&system_dma_stats, gb_stats_master[lchip]->userdata));
        }
    }
    else if (CTC_STATS_TYPE_SGMAC == p_dma_sync->stats_type)
    {
        for (mac_idx = 0; mac_idx < SGMAC_STATS_RAM_MAX; mac_idx++)
        {
            if (IS_BIT_SET(bitmap, mac_idx))
            {
                mac_offset = sgmac_count * MAC_BASED_STATS_SGMAC_RAM_DEPTH * DRV_ADDR_BYTES_PER_ENTRY;
                p_addr = (uint8*)p_dma_sync->p_base_addr + mac_offset;
                sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

                if (dma_sync_en)
                {
                    mac_id = SYS_MAX_GMAC_PORT_NUM + mac_idx;
                    port_id = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
                    if (0xFF == port_id)
                    {
                        continue;
                    }
                }

                for (stats_type = 0; stats_type < SYS_STATS_MAC_STATS_TYPE_NUM; stats_type++)
                {
                    lport = quad_idx * GMAC_STATS_PORT_MAX + mac_idx;
                    mac_type = SYS_STATS_SGMAC_STATS_RAM0 + mac_idx;
                    stats_offset = stats_type * DRV_ADDR_BYTES_PER_ENTRY;

                    sal_memcpy(&sgmac_stats_ram, (uint8*)p_addr + stats_offset, sizeof(sgmac_stats_ram_t));
                    sal_memset(&stats_basic, 0, sizeof(ctc_stats_basic_t));
                    _sys_greatbelt_stats_mac_stats_to_basic(lchip, FALSE, &sgmac_stats_ram, &stats_basic);
                    if (dma_sync_en)
                    {
                        drv_greatbelt_get_port_capability(lchip, port_id, &port_cap);
                        if (port_cap.valid)
                        {
                            system_dma_stats.stats[port_cnt].stats_mode = 1;
                            sys_greatbelt_get_gchip_id(lchip, &gchip_id);
                            system_dma_stats.gport[port_cnt] = CTC_MAP_LPORT_TO_GPORT(gchip_id, port_id);
                            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_dam_sync_mac_stats \
                                             (lchip, p_dma_sync->stats_type, stats_type, &(system_dma_stats.stats[port_cnt]), &stats_basic));

                        }
                    }
                    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_get_mac_stats_tbl(lchip, mac_type, stats_type, lport, &p_mac_stats));
                    CTC_ERROR_RETURN_WITH_STATS_UNLOCK(_sys_greatbelt_stats_write_sys_mac_stats(lchip, stats_type, p_mac_stats, &stats_basic));
                }
                port_cnt = port_cnt + 1;
                system_dma_stats.valid_cnt = port_cnt;
                sgmac_count++;
            }
        }
        if (dma_sync_en)
        {
            CTC_ERROR_RETURN_WITH_STATS_UNLOCK(gb_stats_master[lchip]->dam_sync_mac_stats(&system_dma_stats, gb_stats_master[lchip]->userdata));
        }
    }
    else
    {
        STATS_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_stats_interval(uint8 lchip, uint32  stats_interval)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_dma_set_stats_interval(lchip, stats_interval));
    return CTC_E_NONE;
}

#define ___________FLOW_STATS_FUNCTION________________________
#define __1_STATS_DB__
STATIC uint32
_sys_greatbelt_stats_hash_key_make(sys_stats_fwd_stats_t* p_flow_hash)
{
    uint32 size;
    uint8*  k;

    CTC_PTR_VALID_CHECK(p_flow_hash);

    size = 2;
    k = (uint8*)p_flow_hash;

    return ctc_hash_caculate(size, k);
}

STATIC int32
_sys_greatbelt_stats_hash_key_cmp(sys_stats_fwd_stats_t* p_flow_hash1, sys_stats_fwd_stats_t* p_flow_hash)
{
    CTC_PTR_VALID_CHECK(p_flow_hash1);
    CTC_PTR_VALID_CHECK(p_flow_hash);

    if (p_flow_hash1->stats_ptr != p_flow_hash->stats_ptr)
    {
        return FALSE;
    }

    return TRUE;
}

STATIC int32
_sys_greatbelt_stats_ds_stats_to_basic(uint8 lchip, ds_stats_t ds_stats, ctc_stats_basic_t* basic_stats)
{

    uint64 tmp = 0;

    CTC_PTR_VALID_CHECK(basic_stats);

    basic_stats->packet_count = ds_stats.packet_count;

    tmp |= ds_stats.byte_count39_32;
    tmp <<= 32;
    tmp |= ds_stats.byte_count31_0;
    basic_stats->byte_count = tmp;

    return CTC_E_NONE;
}

STATIC int32
_sys_stats_fwd_stats_entry_create(uint8 lchip, uint16 stats_ptr)
{
    sys_stats_fwd_stats_t* p_flow_stats;

    p_flow_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
    if (NULL == p_flow_stats)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_flow_stats, 0, sizeof(sys_stats_fwd_stats_t));
    p_flow_stats->stats_ptr = stats_ptr;

    /* add it to fwd stats hash */
    ctc_hash_insert(gb_stats_master[lchip]->flow_stats_hash, p_flow_stats);

    return CTC_E_NONE;
}

STATIC int32
_sys_stats_fwd_stats_entry_delete(uint8 lchip, uint16 stats_ptr)
{
    sys_stats_fwd_stats_t flow_stats;
    sys_stats_fwd_stats_t* p_flow_stats = NULL;

    sal_memset(&flow_stats, 0, sizeof(flow_stats));

    flow_stats.stats_ptr = stats_ptr;
    p_flow_stats = ctc_hash_lookup(gb_stats_master[lchip]->flow_stats_hash, &flow_stats);
    if (p_flow_stats)
    {
        ctc_hash_remove(gb_stats_master[lchip]->flow_stats_hash, p_flow_stats);
        mem_free(p_flow_stats);
    }
    else
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_stats_flow_stats_entry_lookup(uint8 lchip, uint16 stats_ptr, sys_stats_fwd_stats_t** pp_fwd_stats)
{
    sys_stats_fwd_stats_t flow_stats;
    sys_stats_fwd_stats_t* p_flow_stats = NULL;

    CTC_PTR_VALID_CHECK(pp_fwd_stats);
    sal_memset(&flow_stats, 0, sizeof(flow_stats));

    *pp_fwd_stats = NULL;

    flow_stats.stats_ptr = stats_ptr;
    p_flow_stats = ctc_hash_lookup(gb_stats_master[lchip]->flow_stats_hash, &flow_stats);
    if (p_flow_stats)
    {
        *pp_fwd_stats = p_flow_stats;
    }

    return CTC_E_NONE;
}

#define __2_PORT_LOG_STATS__
int32
sys_greatbelt_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable)
{

    uint32 cmd = 0, tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);

    if (0 != (bitmap & CTC_STATS_RANDOM_LOG_DISCARD_STATS))
    {
        tmp = (enable) ? 1 : 0;

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_LogPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_LogPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    }

    tmp = 0;
    if (0 != (bitmap & CTC_STATS_FLOW_DISCARD_STATS))
    {
        tmp = (enable) ? 1 : 0;

        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_FlowStats0DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_FlowStats1DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_FlowStats2DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_FlowStats0DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_FlowStats1DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_FlowStats2DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_port_log_discard_stats_enable(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable)
{
    uint32 cmd = 0, tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(enable);

    *enable = 0;

    switch (bitmap & (CTC_STATS_RANDOM_LOG_DISCARD_STATS | CTC_STATS_FLOW_DISCARD_STATS))
    {
    case CTC_STATS_RANDOM_LOG_DISCARD_STATS:
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_LogPortDiscard_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        *enable = (tmp) ? TRUE : FALSE;
        break;

    case CTC_STATS_FLOW_DISCARD_STATS:
        cmd = DRV_IOR(IpeFwdCtl_t, IpeFwdCtl_FlowStats0DiscardStatsEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        *enable = (tmp) ? TRUE : FALSE;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_igs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats)
{
    uint8 lport = 0;
    uint16 stats_ptr = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&basic_stats, 0, sizeof(basic_stats));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    stats_ptr = lport + gb_stats_master[lchip]->igs_port_log_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, stats_ptr, &basic_stats));
    p_stats->packet_count = basic_stats.packet_count;
    p_stats->byte_count = basic_stats.byte_count;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_igs_port_log_stats(uint8 lchip, uint16 gport)
{
    uint8 lport = 0;
    uint16 stats_ptr = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    stats_ptr = lport + gb_stats_master[lchip]->igs_port_log_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, stats_ptr));

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_egs_port_log_stats(uint8 lchip, uint16 gport, ctc_stats_basic_t* p_stats)
{
    uint8 lport = 0;
    uint16 stats_ptr = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    sal_memset(&basic_stats, 0, sizeof(basic_stats));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    stats_ptr = lport + gb_stats_master[lchip]->egs_port_log_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, stats_ptr, &basic_stats));
    p_stats->packet_count = basic_stats.packet_count;
    p_stats->byte_count = basic_stats.byte_count;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_egs_port_log_stats(uint8 lchip, uint16 gport)
{
    uint8 lport = 0;
    uint16 stats_ptr = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    stats_ptr = lport + gb_stats_master[lchip]->egs_port_log_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, stats_ptr));

    return CTC_E_NONE;
}
#define __3_FLOW_STATS__
int32
sys_greatbelt_stats_alloc_flow_stats_ptr(uint8 lchip, sys_stats_flow_type_t flow_stats_type, uint16* p_stats_ptr)
{
    uint32 offset = 0;
    uint8 i = 0;
    sys_greatbelt_opf_t opf;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats_ptr);
    CTC_MAX_VALUE_CHECK(flow_stats_type, (SYS_STATS_FLOW_TYPE_MAX - 1));

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_type = FLOW_STATS_SRAM;
    opf.pool_index = 0;

    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, gb_stats_master[lchip]->flow_stats_size, &offset));

    if ((SYS_STATS_FLOW_TYPE_MPLS == flow_stats_type)
        || (SYS_STATS_FLOW_TYPE_IP_TUNNEL == flow_stats_type)
        || (SYS_STATS_FLOW_TYPE_SCL == flow_stats_type)
        || (SYS_STATS_FLOW_TYPE_NEXTHOP == flow_stats_type)
        || (SYS_STATS_FLOW_TYPE_L3EDIT_MPLS == flow_stats_type))
    {
        if (offset > SYS_STATS_MAX_NUM_FOR_14BIT)
        {
            CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, gb_stats_master[lchip]->flow_stats_size, offset));
            return CTC_E_NO_RESOURCE;
        }
    }

    *p_stats_ptr = offset;
    SYS_STATS_DBG_INFO("alloc flow stats stats_ptr:%d\n", offset);

    /* add to fwd stats list */
    for (i = 0; i < gb_stats_master[lchip]->flow_stats_size; i++)
    {
        _sys_stats_fwd_stats_entry_create(lchip, offset + i);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_free_flow_stats_ptr(uint8 lchip, sys_stats_flow_type_t flow_stats_type, uint16 stats_ptr)
{
    uint32 offset = 0;
    uint8 i = 0;
    uint32 cmd = 0;
    sys_greatbelt_opf_t opf;
    ds_stats_t ds_stats;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_INFO("free flow stats stats_ptr:%d\n", stats_ptr);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    sal_memset(&ds_stats, 0, sizeof(ds_stats_t));

    offset = stats_ptr;
    opf.pool_type = FLOW_STATS_SRAM;
    opf.pool_index = 0;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, gb_stats_master[lchip]->flow_stats_size, offset));

    /*  remove from fwd stats list */
    for (i = 0; i < gb_stats_master[lchip]->flow_stats_size; i++)
    {
        _sys_stats_fwd_stats_entry_delete(lchip, stats_ptr+i);

        /* clear hardware stats */
        cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, stats_ptr + i, cmd, &ds_stats);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_flow_stats(uint8 lchip,
                                   uint16 stats_ptr,
                                   ctc_stats_phb_t* phb,
                                   ctc_stats_basic_t* p_stats)
{
    uint16 offset = 0;
    uint8 color = 0, cos = 0;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);
    CTC_MIN_VALUE_CHECK(stats_ptr, 1)

    if (phb)
    {
        color = phb->color;
        cos = phb->cos;
    }
    CTC_MAX_VALUE_CHECK(color, CTC_STATS_COLOR_MAX - 1);
    CTC_MAX_VALUE_CHECK(cos, SYS_STATS_COS_NUM - 1);

    switch (gb_stats_master[lchip]->flow_stats_size)
    {
    case SYS_STATS_NO_PHB_SIZE:
        offset = stats_ptr;
        break;

    case SYS_STATS_PHB_COLOR_SIZE:
        offset = stats_ptr + color;
        break;

    case SYS_STATS_PHB_COS_SIZE:
        offset = stats_ptr + cos;
        break;

    case SYS_STATS_PHB_COLOR_COS_SIZE:
        offset = stats_ptr + (cos << 1) + color;
        break;

    default:
        offset = stats_ptr;
        break;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, offset, p_stats));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_stats_get_flow_stats_without_phb(uint8 lchip,
                                                uint16 stats_ptr,
                                                ctc_stats_basic_t* p_stats)
{
    uint32 cmd = 0;
    ds_stats_t ds_stats;
    sys_stats_fwd_stats_t* fwd_stats;
    drv_work_platform_type_t platform_type = 0;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    fwd_stats = NULL;

    sal_memset(&ds_stats, 0, sizeof(ds_stats_t));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    cmd = DRV_IOR(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));
    _sys_greatbelt_stats_ds_stats_to_basic(lchip, ds_stats, p_stats);

    /* for uml, add this code to support clear after read */
    drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type == SW_SIM_PLATFORM)
    {
        sal_memset(&ds_stats, 0, sizeof(ds_stats_t));
        cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats);
        SYS_STATS_DBG_INFO("clear stats:%d by software in uml\n", stats_ptr);
    }

    CTC_ERROR_RETURN(_sys_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
    if (NULL != fwd_stats)
    {
        /*add to db*/
        if (gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_FWD])
        {
            fwd_stats->packet_count += p_stats->packet_count;
            fwd_stats->byte_count += p_stats->byte_count;

            p_stats->packet_count = fwd_stats->packet_count;
            p_stats->byte_count = fwd_stats->byte_count;
        }
        else
        {
            fwd_stats->packet_count = p_stats->packet_count;
            fwd_stats->byte_count = p_stats->byte_count;
        }
    }
    else
    {
        fwd_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
        if (!fwd_stats)
        {
            return CTC_E_NO_MEMORY;
        }

        fwd_stats->stats_ptr = stats_ptr;
        fwd_stats->packet_count = p_stats->packet_count;
        fwd_stats->byte_count = p_stats->byte_count;

        /* add it to flow stats hash */
        ctc_hash_insert(gb_stats_master[lchip]->flow_stats_hash, fwd_stats);
    }

    SYS_STATS_DBG_INFO("get flow stats stats_ptr:%d, packets: %lld, bytes: %lld\n", stats_ptr, p_stats->packet_count, p_stats->byte_count);

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_flow_stats(uint8 lchip,
                                     uint16 stats_ptr,
                                     ctc_stats_phb_t* phb)
{
    ds_stats_t ds_stats;
    uint16 offset = 0;
    uint8 color = 0, cos = 0;
    uint8 is_all_phb = 1;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);

    if (phb)
    {
        color = phb->color;
        cos = phb->cos;
        is_all_phb = phb->is_all_phb;
    }
    CTC_MAX_VALUE_CHECK(color, CTC_STATS_COLOR_MAX - 1);
    CTC_MAX_VALUE_CHECK(cos, SYS_STATS_COS_NUM - 1);

    sal_memset(&ds_stats, 0, sizeof(ds_stats_t));

    if (is_all_phb)
    {
        for (offset = 0; offset < gb_stats_master[lchip]->flow_stats_size; offset++)
        {
            _sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, (stats_ptr + offset));
        }
    }
    else
    {
        switch (gb_stats_master[lchip]->flow_stats_size)
        {
        case SYS_STATS_NO_PHB_SIZE:
            offset = stats_ptr;
            break;

        case SYS_STATS_PHB_COLOR_SIZE:
            offset = stats_ptr + color;
            break;

        case SYS_STATS_PHB_COS_SIZE:
            offset = stats_ptr + cos;
            break;

        case SYS_STATS_PHB_COLOR_COS_SIZE:
            offset = stats_ptr + (cos << 1) + color;
            break;

        default:
            offset = stats_ptr;
            break;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, offset));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_stats_clear_flow_stats_without_phb(uint8 lchip, uint16 stats_ptr)
{
    uint32 cmd = 0;
    ds_stats_t ds_stats;
    sys_stats_fwd_stats_t* fwd_stats;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_INFO("clear flow stats stats_ptr:%d\n", stats_ptr);

    sal_memset(&ds_stats, 0, sizeof(ds_stats_t));

    cmd = DRV_IOW(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));

    CTC_ERROR_RETURN(_sys_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
    if(NULL != fwd_stats)
    {
        fwd_stats->packet_count = 0;
        fwd_stats->byte_count = 0;
    }

    return CTC_E_NONE;
}

#define __4_POLICER_STATS__
int32
sys_greatbelt_stats_set_policing_en(uint8 lchip, uint8 enable)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(enable, 1);

    field_val = enable;

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_StatsEnConfirm_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_StatsEnNotConfirm_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(IpePolicingCtl_t, IpePolicingCtl_StatsEnViolate_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_policing_en(uint8 lchip, uint8* p_enable)
{
    uint32 field_val = 0;
    uint32 cmd = 0;

    SYS_STATS_INIT_CHECK(lchip);

    cmd = DRV_IOR(IpePolicingCtl_t, IpePolicingCtl_StatsEnConfirm_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_enable = field_val;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_policing_stats(uint8 lchip, uint16 stats_ptr, sys_stats_policing_t* p_stats)
{
    uint32 index = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&basic_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(p_stats, 0, sizeof(sys_stats_policing_t));

    /*green*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_confirm_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
    p_stats->policing_confirm_pkts = basic_stats.packet_count;
    p_stats->policing_confirm_bytes = basic_stats.byte_count;

    /*yellow*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_exceed_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
    p_stats->policing_exceed_pkts = basic_stats.packet_count;
    p_stats->policing_exceed_bytes = basic_stats.byte_count;

    /*red*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_violate_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
    p_stats->policing_violate_pkts = basic_stats.packet_count;
    p_stats->policing_violate_bytes = basic_stats.byte_count;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_policing_stats(uint8 lchip, uint16 stats_ptr)
{
    uint32 index = 0;

    SYS_STATS_INIT_CHECK(lchip);

    /*green*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_confirm_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));

    /*yellow*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_exceed_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));

    /*red*/
    index = stats_ptr + gb_stats_master[lchip]->policer_stats_violate_base;
    CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));

    return CTC_E_NONE;
}

#define __5_QUEUE_STATS__
int32
sys_greatbelt_stats_set_queue_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint16 queue = 0;

    /* SYS_STATS_INIT_CHECK(lchip); */

    field_val = enable ? 1 : 0;

    for (queue = 0; queue < SYS_MAX_QUEUE_NUM; queue++)
    {
        cmd = DRV_IOW(DsEgrResrcCtl_t, DsEgrResrcCtl_StatsUpdEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue, cmd, &field_val));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_queue_en(uint8 lchip, uint8* p_enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_STATS_INIT_CHECK(lchip);

    cmd = DRV_IOR(DsEgrResrcCtl_t, DsEgrResrcCtl_StatsUpdEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    *p_enable = field_val;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_queue_stats(uint8 lchip, uint16 stats_ptr, sys_stats_queue_t* p_stats)
{
    uint32 index = 0;
    ctc_stats_basic_t enq_stats;
    ctc_stats_basic_t deq_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&enq_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(&deq_stats, 0, sizeof(ctc_stats_basic_t));
    sal_memset(p_stats, 0, sizeof(sys_stats_queue_t));

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_QUEUE_DROP_STATS)
    {
        /* enqueue */
        index = stats_ptr + gb_stats_master[lchip]->enqueue_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &enq_stats));
    }

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_QUEUE_DEQ_STATS)
    {
        /* dequeue */
        index = stats_ptr + gb_stats_master[lchip]->dequeue_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &deq_stats));
        p_stats->queue_deq_pkts = deq_stats.packet_count;
        p_stats->queue_deq_bytes = deq_stats.byte_count;

        /*drop*/
        if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_QUEUE_DROP_STATS)
        {
            if (enq_stats.packet_count >= p_stats->queue_deq_pkts)
            {
                p_stats->queue_drop_pkts = enq_stats.packet_count - p_stats->queue_deq_pkts;
                p_stats->queue_drop_bytes = enq_stats.byte_count - p_stats->queue_deq_bytes;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_queue_stats(uint8 lchip, uint16 stats_ptr)
{
    uint32 index = 0;

    SYS_STATS_INIT_CHECK(lchip);

    /* should clear enqueue stats first ? */
    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_QUEUE_DEQ_STATS)
    {
        /* dequeue */
        index = stats_ptr + gb_stats_master[lchip]->dequeue_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));
    }

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_QUEUE_DROP_STATS)
    {
        /* enqueue */
        index = stats_ptr + gb_stats_master[lchip]->enqueue_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));
    }

    return CTC_E_NONE;
}

#define __6_VLAN_STATS__
int32
sys_greatbelt_stats_get_vlan_stats(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, ctc_stats_basic_t* p_stats)
{
    uint32 index = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(p_stats, 0, sizeof(sys_stats_queue_t));

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VLAN_STATS)
    {
        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            sal_memset(&basic_stats, 0, sizeof(ctc_stats_basic_t));
            index = vlan_id + gb_stats_master[lchip]->ingress_vlan_stats_base;
            CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
            p_stats->byte_count = basic_stats.byte_count;
            p_stats->packet_count = basic_stats.packet_count;
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            sal_memset(&basic_stats, 0, sizeof(ctc_stats_basic_t));
            index = vlan_id + gb_stats_master[lchip]->egress_vlan_stats_base;
            CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
            p_stats->byte_count += basic_stats.byte_count;
            p_stats->packet_count += basic_stats.packet_count;
        }
    }
    else
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_vlan_stats(uint8 lchip, uint16 vlan_id, ctc_direction_t dir)
{
    uint32 index = 0;

    SYS_STATS_INIT_CHECK(lchip);

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VLAN_STATS)
    {
        if ((dir == CTC_INGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            index = vlan_id + gb_stats_master[lchip]->ingress_vlan_stats_base;
            CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));
        }

        if ((dir == CTC_EGRESS) || (dir == CTC_BOTH_DIRECTION))
        {
            index = vlan_id + gb_stats_master[lchip]->egress_vlan_stats_base;
            CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));
        }
    }
    else
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    return CTC_E_NONE;
}

#define __7_VRF_STATS__
int32
sys_greatbelt_stats_get_vrf_stats(uint8 lchip, uint16 vrfid, ctc_stats_basic_t* p_stats)
{
    uint32 index = 0;
    ctc_stats_basic_t basic_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VRF_STATS)
    {
        sal_memset(&basic_stats, 0, sizeof(ctc_stats_basic_t));
        index = vrfid + gb_stats_master[lchip]->vrf_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats_without_phb(lchip, index, &basic_stats));
        p_stats->byte_count += basic_stats.byte_count;
        p_stats->packet_count += basic_stats.packet_count;
    }
    else
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_clear_vrf_stats(uint8 lchip, uint16 vrfid)
{
    uint32 index = 0;

    SYS_STATS_INIT_CHECK(lchip);

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VRF_STATS)
    {
        index = vrfid + gb_stats_master[lchip]->vrf_stats_base;
        CTC_ERROR_RETURN(_sys_greatbelt_stats_clear_flow_stats_without_phb(lchip, index));
    }
    else
    {
        return CTC_E_STATS_NOT_ENABLE;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_vrf_stats_global_en(uint8 lchip, uint8* enable)
{
    SYS_STATS_INIT_CHECK(lchip);

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VRF_STATS)
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
sys_greatbelt_stats_get_vrf_statsptr(uint8 lchip, uint16 vrfid, uint16* stats_ptr)
{
    SYS_STATS_INIT_CHECK(lchip);

    if (gb_stats_master[lchip]->stats_bitmap & CTC_STATS_VRF_STATS)
    {
        *stats_ptr = vrfid + gb_stats_master[lchip]->vrf_stats_base;
    }
    else
    {
        *stats_ptr = 0;
    }

    return CTC_E_NONE;
}

#define __8_STATS_GLOBAL_CONFIG__
int32
sys_greatbelt_stats_set_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("lchip:%d, enable:%d\n", lchip, enable);

    CTC_ERROR_RETURN(sys_greatbelt_port_set_mac_stats_incrsaturate(lchip, stats_type, tmp));

    STATS_LOCK;
    gb_stats_master[lchip]->saturate_en[stats_type] = tmp;
    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_saturate_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    STATS_LOCK;
    *p_enable = gb_stats_master[lchip]->saturate_en[stats_type];
    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;
    uint32 index = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("lchip:%d, enable:%d\n", lchip, enable);

    PORT_LOCK;  /* must use port lock to protect from mac operation */

    switch (stats_type)
    {
    case CTC_STATS_TYPE_FWD:
        /*DS STATS*/
        cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_StatsHold_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
        break;

    case CTC_STATS_TYPE_GMAC:

        /*GMAC*/
        for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
        {
            if (drv_greatbelt_qmac_is_enable(lchip, index))
            {
                reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
                reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOW(reg_id, \
                              QuadMacStatsCfg_IncrHold_f);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
            }
        }

        break;

    case CTC_STATS_TYPE_SGMAC:

        /*SGMAC*/
        for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
        {
            if (drv_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
            {
                reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
                reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOW(reg_id, \
                              SgmacStatsCfg_IncrHold_f);
                CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
            }
        }

        break;

    case CTC_STATS_TYPE_CPUMAC:
        /*CPU MAC*/
        cmd = DRV_IOW(CpuMacStatsUpdateCtl_t, CpuMacStatsUpdateCtl_IncrHold_f);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &tmp), p_gb_port_master[lchip]->p_port_mutex);
        break;

    default:
        PORT_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }


    gb_stats_master[lchip]->hold_en[stats_type] = tmp;
    PORT_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_hold_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    *p_enable = gb_stats_master[lchip]->hold_en[stats_type];

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool enable)
{
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    SYS_STATS_TYPE_CHECK(stats_type);

    tmp = (FALSE == enable) ? 0 : 1;

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("lchip:%d, enable:%d\n", lchip, enable);

    CTC_ERROR_RETURN(sys_greatbelt_port_set_mac_stats_clear(lchip,stats_type,enable));

    STATS_LOCK;
    gb_stats_master[lchip]->clear_read_en[stats_type] = tmp;
    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_clear_after_read_en(uint8 lchip, ctc_stats_type_t stats_type, bool* p_enable)
{
    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);
    SYS_STATS_TYPE_CHECK(stats_type);

    STATS_LOCK;
    *p_enable = gb_stats_master[lchip]->clear_read_en[stats_type];
    STATS_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_pkt_cnt_threshold(uint8 lchip, uint16 threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("packet count threshold:%d\n", threshold);

    if (threshold > SYS_STATS_MAX_PKT_CNT - 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    tmp = threshold;


    cmd = DRV_IOW(GlobalStatsPktCntThreshold_t, GlobalStatsPktCntThreshold_PktCntThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_pkt_cnt_threshold(uint8 lchip, uint16* p_threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_threshold);

    cmd = DRV_IOR(GlobalStatsPktCntThreshold_t, GlobalStatsPktCntThreshold_PktCntThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    *p_threshold = tmp;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_byte_cnt_threshold(uint8 lchip, uint16 threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("byte count threshold:%d\n", threshold);
    if (threshold > SYS_STATS_MAX_BYTE_CNT - 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    tmp = threshold;


    cmd = DRV_IOW(GlobalStatsByteCntThreshold_t, GlobalStatsByteCntThreshold_ByteCntThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_byte_cnt_threshold(uint8 lchip, uint16* p_threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_threshold);

    cmd = DRV_IOR(GlobalStatsByteCntThreshold_t, GlobalStatsByteCntThreshold_ByteCntThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    *p_threshold = tmp;

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_set_fifo_depth_threshold(uint8 lchip, uint8 threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);

    SYS_STATS_DBG_FUNC();
    SYS_STATS_DBG_INFO("fifo depth threshold:%d\n", threshold);

    if (threshold > SYS_STATS_MAX_FIFO_DEPTH - 1)
    {
        return CTC_E_INVALID_PARAM;
    }

    tmp = threshold;

    cmd = DRV_IOW(GlobalStatsSatuAddrFifoDepthThreshold_t, GlobalStatsSatuAddrFifoDepthThreshold_SatuAddrFifoDepthThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_get_fifo_depth_threshold(uint8 lchip, uint8* p_threshold)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_threshold);
    cmd = DRV_IOR(GlobalStatsSatuAddrFifoDepthThreshold_t, GlobalStatsSatuAddrFifoDepthThreshold_SatuAddrFifoDepthThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    *p_threshold = tmp;

    return CTC_E_NONE;
}

#define __8_STATS_INTERRUPT__
STATIC int32
_sys_greatbelt_stats_get_flow_stats(uint8 lchip, uint16 stats_ptr, ctc_stats_basic_t* p_stats)
{

    uint32 cmd = 0;
    ds_stats_t ds_stats;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&ds_stats, 0, sizeof(ds_stats_t));
    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    cmd = DRV_IOR(DsStats_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stats_ptr, cmd, &ds_stats));
    _sys_greatbelt_stats_ds_stats_to_basic(lchip, ds_stats, p_stats);

    return CTC_E_NONE;

}

int32
sys_greatbelt_stats_isr(uint8 lchip, uint32 intr, void* p_data)
{
    uint8  i = 0;
    uint32 cmd = 0;
    uint32 depth = 0;
    uint32 stats_ptr = 0;
    ctc_stats_basic_t stats;
    sys_stats_fwd_stats_t* fwd_stats = NULL;

    if (NULL == gb_stats_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    cmd = DRV_IOR(GlobalStatsSatuAddrFifoDepth_t, GlobalStatsSatuAddrFifoDepth_SatuAddrFifoDepth_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &depth));

    if (depth > SYS_STATS_MAX_FIFO_DEPTH)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_STATS_DBG_INFO("stats isr occur, depth:%d\n", depth);

    /*get stats ptr from fifo*/
    for (i = 0; i < depth; i++)
    {
        cmd = DRV_IOR(GlobalStatsSatuAddr_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &stats_ptr));

        SYS_STATS_DBG_INFO("get stats:%d to db\n", stats_ptr);

        /*get stats from stats ptr*/
        CTC_ERROR_RETURN(_sys_greatbelt_stats_get_flow_stats(lchip, stats_ptr, &stats));

        CTC_ERROR_RETURN(_sys_stats_flow_stats_entry_lookup(lchip, stats_ptr, &fwd_stats));
        if (NULL == fwd_stats)
        {
            fwd_stats = (sys_stats_fwd_stats_t*)mem_malloc(MEM_STATS_MODULE, sizeof(sys_stats_fwd_stats_t));
            if (!fwd_stats)
            {
                return CTC_E_NO_MEMORY;
            }

            fwd_stats->stats_ptr = stats_ptr;
            fwd_stats->packet_count = stats.packet_count;
            fwd_stats->byte_count = stats.byte_count;

            /* add it to flow stats hash */
            ctc_hash_insert(gb_stats_master[lchip]->flow_stats_hash, fwd_stats);
        }
        else
        {
            /*add to db*/
            if (gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_FWD])
            {
                fwd_stats->packet_count += stats.packet_count;
                fwd_stats->byte_count += stats.byte_count;
            }
            else
            {
                fwd_stats->packet_count = stats.packet_count;
                fwd_stats->byte_count = stats.byte_count;
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_intr_callback_func(uint8 lchip, uint8* p_gchip)
{
    SYS_STATS_INIT_CHECK(lchip);

    if (TRUE != sys_greatbelt_chip_is_local(lchip, *p_gchip))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_stats_isr(lchip, 0, NULL));

    return CTC_E_NONE;
}

#define __9_STATS_STATSID__
STATIC int32
_sys_greatbelt_stats_hash_make_statsid(sys_stats_statsid_t* statsid)
{
    return statsid->stats_id;
}

STATIC int32
_sys_greatbelt_stats_hash_key_cmp_statsid(sys_stats_statsid_t* statsid_0, sys_stats_statsid_t* statsid_1)
{
    return (statsid_0->stats_id == statsid_1->stats_id);
}

STATIC int32
_sys_greatbelt_stats_alloc_statsptr_by_statsid(uint8 lchip, ctc_stats_statsid_t statsid, sys_stats_statsid_t* sw_stats_statsid)
{

    uint8 stats_type;
    uint32 vlan_base = 0;
    uint32 ret = CTC_E_NONE;

    sw_stats_statsid->stats_id = statsid.stats_id;
    sw_stats_statsid->stats_id_type = statsid.type;


    if (statsid.type == CTC_STATS_STATSID_TYPE_VLAN)
    {
        if (statsid.dir >= CTC_BOTH_DIRECTION)
        {
            return CTC_E_INVALID_PARAM;
        }
        CTC_VLAN_RANGE_CHECK(statsid.statsid.vlan_id);

        vlan_base = (statsid.dir == CTC_INGRESS)? gb_stats_master[lchip]->ingress_vlan_stats_base : gb_stats_master[lchip]->egress_vlan_stats_base;
        sw_stats_statsid->stats_ptr = vlan_base + statsid.statsid.vlan_id;

        return CTC_E_NONE;
    }

    if (statsid.type == CTC_STATS_STATSID_TYPE_VRF)
    {
        if (statsid.statsid.vrf_id >= sys_greatbelt_l3if_get_max_vrfid(lchip, MAX_CTC_IP_VER))
        {
            return CTC_E_INVALID_PARAM;
        }

        sw_stats_statsid->stats_ptr = gb_stats_master[lchip]->vrf_stats_base + statsid.statsid.vrf_id;

        return CTC_E_NONE;
    }

    switch (statsid.type)
    {
    case CTC_STATS_STATSID_TYPE_ACL:
        stats_type = SYS_STATS_FLOW_TYPE_ACL;
        break;

    case CTC_STATS_STATSID_TYPE_IPMC:
    case CTC_STATS_STATSID_TYPE_ECMP:
        stats_type = SYS_STATS_FLOW_TYPE_DSFWD;
        break;

    case CTC_STATS_STATSID_TYPE_MPLS:
        stats_type = SYS_STATS_FLOW_TYPE_MPLS;
        break;

    case CTC_STATS_STATSID_TYPE_TUNNEL:
        stats_type = SYS_STATS_FLOW_TYPE_IP_TUNNEL;
        break;

    case CTC_STATS_STATSID_TYPE_SCL:
        stats_type = SYS_STATS_FLOW_TYPE_SCL;
        break;

    case CTC_STATS_STATSID_TYPE_NEXTHOP:
        stats_type = SYS_STATS_FLOW_TYPE_NEXTHOP;
        break;

    case CTC_STATS_STATSID_TYPE_L3IF:
        stats_type = SYS_STATS_FLOW_TYPE_SCL;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_stats_alloc_flow_stats_ptr(lchip, stats_type, &(sw_stats_statsid->stats_ptr)));


    return ret;
}

STATIC int32
_sys_greatbelt_stats_free_statsptr_by_statsid(uint8 lchip, sys_stats_statsid_t* sw_stats_statsid)
{

    uint8 stats_type;
    uint32 ret = CTC_E_NONE;

    if ((sw_stats_statsid->stats_id_type == CTC_STATS_STATSID_TYPE_VLAN) \
       || (sw_stats_statsid->stats_id_type == CTC_STATS_STATSID_TYPE_VRF))
    {
        return CTC_E_NONE;
    }

    switch (sw_stats_statsid->stats_id_type)
    {
    case CTC_STATS_STATSID_TYPE_ACL:
        stats_type = SYS_STATS_FLOW_TYPE_ACL;
        break;

    case CTC_STATS_STATSID_TYPE_IPMC:
        stats_type = SYS_STATS_FLOW_TYPE_DSFWD;
        break;

    case CTC_STATS_STATSID_TYPE_MPLS:
        stats_type = SYS_STATS_FLOW_TYPE_MPLS;
        break;

    case CTC_STATS_STATSID_TYPE_TUNNEL:
        stats_type = SYS_STATS_FLOW_TYPE_IP_TUNNEL;
        break;

    case CTC_STATS_STATSID_TYPE_SCL:
        stats_type = SYS_STATS_FLOW_TYPE_SCL;
        break;

    case CTC_STATS_STATSID_TYPE_NEXTHOP:
        stats_type = SYS_STATS_FLOW_TYPE_NEXTHOP;
        break;

    case CTC_STATS_STATSID_TYPE_L3IF:
        stats_type = SYS_STATS_FLOW_TYPE_SCL;

    default:
        return CTC_E_INVALID_PARAM;
    }


    if (0 != sw_stats_statsid->stats_ptr)
    {
        CTC_ERROR_RETURN(sys_greatbelt_stats_free_flow_stats_ptr(lchip, stats_type, sw_stats_statsid->stats_ptr));
    }


    return ret;
}

int32
sys_greatbelt_stats_get_statsptr(uint8 lchip, uint32 stats_id, uint16* stats_ptr)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stats_ptr);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gb_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    *stats_ptr = sys_stats_id_rs->stats_ptr;
    return CTC_E_NONE;
}
int32
sys_greatbelt_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid)
{
    ctc_stats_statsid_t sys_stats_id_user;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    sys_stats_statsid_t* sys_stats_id_insert;
    sys_greatbelt_opf_t opf;
    int32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(statsid);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));

    sys_stats_id_insert = mem_malloc(MEM_STATS_MODULE,  sizeof(sys_stats_statsid_t));
    if (sys_stats_id_insert == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    /*statsid maybe alloc*/
    if (gb_stats_master[lchip]->stats_mode == CTC_STATS_MODE_USER)
    {
        sys_stats_id_lk.stats_id = statsid->stats_id;
        sys_stats_id_rs = ctc_hash_lookup(gb_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
        if (sys_stats_id_rs != NULL)
        {
            mem_free(sys_stats_id_insert);
            return CTC_E_ENTRY_EXIST;
        }
    }
    else
    {
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_index = 0;
        opf.pool_type  = OPF_STATS_STATSID;

        ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, &(statsid->stats_id));
        if (ret != CTC_E_NONE)
        {
            mem_free(sys_stats_id_insert);
            return ret;
        }
        sys_stats_id_lk.stats_id = statsid->stats_id;
    }

    /*stats alloc by statsid*/
    sal_memcpy(&sys_stats_id_user, statsid, sizeof(ctc_stats_statsid_t));
    ret = _sys_greatbelt_stats_alloc_statsptr_by_statsid(lchip, sys_stats_id_user, &sys_stats_id_lk);
    if (ret != CTC_E_NONE)
    {
        if (gb_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
        {
            sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
            opf.pool_index = 0;
            opf.pool_type  = OPF_STATS_STATSID;

            sys_greatbelt_opf_free_offset(lchip, &opf, 1, statsid->stats_id);
        }
        mem_free(sys_stats_id_insert);
        return ret;
    }

    /*statsid and stats sw*/
    sal_memcpy(sys_stats_id_insert, &sys_stats_id_lk, sizeof(sys_stats_statsid_t));
    ctc_hash_insert(gb_stats_master[lchip]->stats_statsid_hash, sys_stats_id_insert);

    return ret;
}

int32
sys_greatbelt_stats_destroy_statsid(uint8 lchip, uint32 stats_id)
{
    sys_greatbelt_opf_t opf;
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gb_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    ctc_hash_remove(gb_stats_master[lchip]->stats_statsid_hash, sys_stats_id_rs);

    _sys_greatbelt_stats_free_statsptr_by_statsid(lchip, sys_stats_id_rs);

    mem_free(sys_stats_id_rs);

    if (gb_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_index = 0;
        opf.pool_type  = OPF_STATS_STATSID;

        CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, 1, stats_id));
    }

    return ret;
}

int32
sys_greatbelt_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats)
{

    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    ctc_stats_basic_t stats;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stats);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gb_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    sal_memset(p_stats, 0, sizeof(ctc_stats_basic_t));

    sal_memset(&stats, 0, sizeof(ctc_stats_basic_t));
    CTC_ERROR_RETURN(sys_greatbelt_stats_get_flow_stats(lchip, sys_stats_id_rs->stats_ptr, 0, &stats));
    p_stats->byte_count += stats.byte_count;
    p_stats->packet_count += stats.packet_count;

    return ret;
}

int32
sys_greatbelt_stats_clear_stats(uint8 lchip, uint32 stats_id)
{
    sys_stats_statsid_t sys_stats_id_lk;
    sys_stats_statsid_t* sys_stats_id_rs;
    uint32 ret = CTC_E_NONE;

    SYS_STATS_INIT_CHECK(lchip);

    sal_memset(&sys_stats_id_lk, 0, sizeof(sys_stats_id_lk));
    sys_stats_id_lk.stats_id = stats_id;
    sys_stats_id_rs = ctc_hash_lookup(gb_stats_master[lchip]->stats_statsid_hash, &sys_stats_id_lk);
    if (sys_stats_id_rs == NULL)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }


    CTC_ERROR_RETURN(sys_greatbelt_stats_clear_flow_stats(lchip, sys_stats_id_rs->stats_ptr, 0));


    return ret;
}

#define ___________STATS_INIT________________________

STATIC int32
_sys_greatbelt_stats_init_start(uint8 lchip)
{
    drv_work_platform_type_t platform_type;

    drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        uint32 cmd = 0;
        uint32 tmp = 1;

        /* global stats: write GlobalStatsRamInitCtl to init*/
        cmd = DRV_IOW(GlobalStatsRamInitCtl_t, GlobalStatsRamInitCtl_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_deinit_start(uint8 lchip)
{
    uint32 reg_step = 0;
    uint32 reg_id = 0;
    uint32 cmd = 0;
    uint32 tmp = 0;
    uint32 index = 0;
    drv_work_platform_type_t platform_type;

    drv_greatbelt_get_platform_type(&platform_type);

    if (platform_type == HW_PLATFORM)
    {

        /* global stats: write GlobalStatsRamInitCtl to init*/
        cmd = DRV_IOW(GlobalStatsRamInitCtl_t, GlobalStatsRamInitCtl_Init_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    }



    /* gmac */
    for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
    {
        if (drv_greatbelt_qmac_is_enable(lchip, index))
        {
            /*mtu1 default value set*/
            tmp = 0;
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*mtu2 default value set*/
            tmp = 0;
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*QuadMacInit*/
            reg_step = QuadMacInit1_t - QuadMacInit0_t;
            reg_id = QuadMacInit0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            tmp = 0;
            cmd = DRV_IOW(reg_id, QuadMacInit_QuadMacInit_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
            tmp = 0;
            cmd = DRV_IOW(reg_id, QuadMacInit_MaxInitCnt_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }

    /* Sgmac */
    for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
    {
        if (drv_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
        {
            /*mtu1 default value set*/
            tmp = 0;
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*mtu2 default value set*/
            tmp = 0;
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*SgmacStatsInit*/
            reg_step = SgmacStatsInit1_t - SgmacStatsInit0_t;
            reg_id = SgmacStatsInit0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            tmp = 0;
            cmd = DRV_IOW(reg_id, SgmacStatsInit_SgmacInit_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }


    CTC_ERROR_RETURN(sys_greatbelt_opf_free(lchip, FLOW_STATS_SRAM, 1));
    CTC_ERROR_RETURN(sys_greatbelt_opf_free(lchip, OPF_STATS_STATSID, 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_init_done(uint8 lchip)
{
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 tmp = 0;
    uint32 reg_step = 0;
    uint32 reg_id = 0;
    uint32 index = 0;

    drv_greatbelt_get_platform_type(&platform_type);

    if (platform_type == HW_PLATFORM)
    {
        /*GMAC to decide whether init has done*/
        for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
        {
            if (drv_greatbelt_qmac_is_enable(lchip, index))
            {
                reg_step = QuadMacInitDone1_t - QuadMacInitDone0_t;
                reg_id = QuadMacInitDone0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOR(reg_id, QuadMacInitDone_QuadMacInitDone_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
                if (0 == tmp)
                {
                    return CTC_E_NOT_INIT;
                }
            }
        }

        /*SGMAC to decide whether init has done*/
        for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
        {
            if (drv_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
            {
                reg_step = SgmacStatsInitDone1_t - SgmacStatsInitDone0_t;
                reg_id = SgmacStatsInitDone0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
                cmd = DRV_IOR(reg_id, SgmacStatsInitDone_SgmacInitDone_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
                if (0 == tmp)
                {
                    return CTC_E_NOT_INIT;
                }
            }
        }

        /*global stats: read  GlobalStatsRamInitCtl initdone to decide whether init has done*/
        cmd = DRV_IOR(GlobalStatsRamInitCtl_t, GlobalStatsRamInitCtl_InitDone_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        if (0 == tmp)
        {
            return CTC_E_NOT_INIT;
        }

    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_mac_stats_init(uint8 lchip)
{
    uint32 reg_step = 0;
    uint32 reg_id = 0;
    uint32 tmp = 0;
    uint32 cmd = 0;
    uint32 index = 0;

    /* gmac */
    for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
    {
        if (drv_greatbelt_qmac_is_enable(lchip, index))
        {
            /*mtu1 default value set*/
            tmp = SYS_STATS_MTU1_PKT_DFT_LENGTH;
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*mtu2 default value set*/
            tmp = SYS_STATS_MTU2_PKT_DFT_LENGTH;
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, QuadMacStatsCfg_PacketLenMtu2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*QuadMacInit*/
            reg_step = QuadMacInit1_t - QuadMacInit0_t;
            reg_id = QuadMacInit0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            tmp = 1;
            cmd = DRV_IOW(reg_id, QuadMacInit_QuadMacInit_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
            tmp = 0x90;
            cmd = DRV_IOW(reg_id, QuadMacInit_MaxInitCnt_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }

    /* Sgmac */
    for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
    {
        if (drv_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
        {
            /*mtu1 default value set*/
            tmp = SYS_STATS_MTU1_PKT_DFT_LENGTH;
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu1_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*mtu2 default value set*/
            tmp = SYS_STATS_MTU2_PKT_DFT_LENGTH;
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, SgmacStatsCfg_PacketLenMtu2_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

            /*SgmacStatsInit*/
            reg_step = SgmacStatsInit1_t - SgmacStatsInit0_t;
            reg_id = SgmacStatsInit0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            tmp = 1;
            cmd = DRV_IOW(reg_id, SgmacStatsInit_SgmacInit_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_stats_statsid_init(uint8 lchip)
{
    uint32 max_stats_index = 0;
    sys_greatbelt_opf_t opf;
    uint32 start_offset = 0;
    uint32 entry_num    = 0;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsStats_t,  &max_stats_index);
    if (0 == max_stats_index)
    {
        return CTC_E_NO_RESOURCE;
    }
    #define SYS_STATS_HASH_BLOCK_SIZE           1024
    gb_stats_master[lchip]->stats_statsid_hash = ctc_hash_create(1,
                                        SYS_STATS_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_greatbelt_stats_hash_make_statsid,
                                        (hash_cmp_fn) _sys_greatbelt_stats_hash_key_cmp_statsid);

    if (gb_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        opf.pool_type = OPF_STATS_STATSID;
        start_offset  = 0;
        entry_num     = CTC_STATS_MAX_STATSID;
        CTC_ERROR_RETURN(sys_greatbelt_opf_init(lchip, OPF_STATS_STATSID, 1));
        CTC_ERROR_RETURN(sys_greatbelt_opf_init_offset(lchip, &opf, start_offset, entry_num));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_init(uint8 lchip, ctc_stats_global_cfg_t* stats_global_cfg)
{

    uint32 cmd = 0;
    uint32 tmp = 0;
    int32 ret = CTC_E_NONE;
    sys_greatbelt_opf_t opf;
    uint32 size = 0, tmp_size = 0;
    uint32 tmp_offset = 0, vlan_offset = 0;
    uint32 step = 0;
    uint32 max_stats_index = 0;
    uint32 max_flow_stats_index = 0;
    ipe_ds_vlan_ctl_t ipe_ds_vlan_ctl;
    epe_next_hop_ctl_t epe_next_hop_ctl;
    uint32 index = 0;
    uint32 reg_id = 0;
    uint32 reg_step = 0;

    LCHIP_CHECK(lchip);
    /*init global variable*/
    if (NULL != gb_stats_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_stats_init_start(lchip));

    sys_greatbelt_ftm_get_table_entry_num(lchip, DsStats_t,  &max_stats_index);

    if (0 == max_stats_index)
    {
        return CTC_E_NO_RESOURCE;
    }


    MALLOC_POINTER(sys_stats_master_t, gb_stats_master[lchip]);
    if (NULL == gb_stats_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gb_stats_master[lchip], 0, sizeof(sys_stats_master_t));

    sal_mutex_create(&(gb_stats_master[lchip]->p_stats_mutex));


    gb_stats_master[lchip]->stats_stored_in_sw = 1;   /* default use db */


    gb_stats_master[lchip]->stats_bitmap = stats_global_cfg->stats_bitmap;
    gb_stats_master[lchip]->stats_mode = stats_global_cfg->stats_mode;
    gb_stats_master[lchip]->query_mode = stats_global_cfg->query_mode;

    /* PHB init */
    gb_stats_master[lchip]->phb_bitmap = 0;
    gb_stats_master[lchip]->flow_stats_size = 1;

    if (gb_stats_master[lchip]->phb_bitmap & CTC_STATS_WITH_COLOR)
    {
        gb_stats_master[lchip]->flow_stats_size = gb_stats_master[lchip]->flow_stats_size * 2;
    }

    if (gb_stats_master[lchip]->phb_bitmap & CTC_STATS_WITH_COS)
    {
        gb_stats_master[lchip]->flow_stats_size = gb_stats_master[lchip]->flow_stats_size * 8;
    }

    /* End PHB init */

    if ((ret = sys_greatbelt_opf_init(lchip, FLOW_STATS_SRAM,  1)) < 0)
    {
        goto error;
    }


    size = 0;
    step = 0;
    /* flow stats hash init */
    gb_stats_master[lchip]->flow_stats_hash = ctc_hash_create(1, SYS_STATS_FLOW_ENTRY_HASH_BLOCK_SIZE,
                                           (hash_key_fn)_sys_greatbelt_stats_hash_key_make,
                                           (hash_cmp_fn)_sys_greatbelt_stats_hash_key_cmp);

    /* PHB init */
    switch (gb_stats_master[lchip]->flow_stats_size)
    {
    case SYS_STATS_NO_PHB_SIZE:
        tmp = 0;
        break;

    case SYS_STATS_PHB_COLOR_SIZE:
        tmp = 1;
        break;

    case SYS_STATS_PHB_COS_SIZE:
        tmp = 2;
        break;

    case SYS_STATS_PHB_COLOR_COS_SIZE:
        tmp = 3;
        break;

    default:
        break;
    }

    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_StatsMode_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_StatsMode_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    /* default stats WRR configuration */
    tmp = SYS_STATS_DEFAULT_WRR;
    cmd = DRV_IOW(GlobalStatsWrrCfg_t, GlobalStatsWrrCfg_EpeWrrCredit_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    cmd = DRV_IOW(GlobalStatsWrrCfg_t, GlobalStatsWrrCfg_IpeWrrCredit_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    cmd = DRV_IOW(GlobalStatsWrrCfg_t, GlobalStatsWrrCfg_PolicingWrrCredit_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    cmd = DRV_IOW(GlobalStatsWrrCfg_t, GlobalStatsWrrCfg_QMgrWrrCredit_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    /* default stats interrupt configuration */
    tmp = SYS_STATS_DEFAULT_FIFO_DEPTH_THRESHOLD;
    cmd = DRV_IOW(GlobalStatsSatuAddrFifoDepth_t, GlobalStatsSatuAddrFifoDepth_SatuAddrFifoDepth_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    tmp = SYS_STATS_DEFAULT_BYTE_THRESHOLD;
    cmd = DRV_IOW(GlobalStatsByteCntThreshold_t, GlobalStatsByteCntThreshold_ByteCntThreshold_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    tmp = SYS_STATS_DEFAULT_PACKET_THRESHOLD;
    cmd = DRV_IOW(GlobalStatsPktCntThreshold_t, GlobalStatsPktCntThreshold_PktCntThreshold_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }

    /* global stats pool init */
    /* ingress random log */
    tmp = 0;
    cmd = DRV_IOW(IpeFwdCtl_t, IpeFwdCtl_PortLogStatsBase_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }
    gb_stats_master[lchip]->igs_port_log_base = 0;
    size += 128;

    /* egress random log */
    tmp = size >> 6;
    cmd = DRV_IOW(EpeHeaderEditCtl_t, EpeHeaderEditCtl_PortLogStatsBase_f);
    if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
    {
        goto error;
    }
    gb_stats_master[lchip]->egs_port_log_base = size;
    size += 128;

    /* Dequeue */
    if (stats_global_cfg->stats_bitmap & CTC_STATS_QUEUE_DEQ_STATS)
    {
        tmp = size >> 6;
        cmd = DRV_IOW(QMgrQueDeqStatsBase_t, QMgrQueDeqStatsBase_DeqStatsBase_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
        {
            goto error;
        }

        gb_stats_master[lchip]->dequeue_stats_base = size;
        size += 1024;
    }

    /* Drop queue */
    if ((stats_global_cfg->stats_bitmap & CTC_STATS_QUEUE_DROP_STATS)
        && (stats_global_cfg->stats_bitmap & CTC_STATS_QUEUE_DEQ_STATS))
    {
        tmp = size >> 6;
        cmd = DRV_IOW(QMgrQueEnqStatsBase_t, QMgrQueEnqStatsBase_EnqStatsBase_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
        {
            goto error;
        }

        gb_stats_master[lchip]->enqueue_stats_base = size;
        size += 1024;
    }
    else
    {
        CTC_UNSET_FLAG(stats_global_cfg->stats_bitmap, CTC_STATS_QUEUE_DROP_STATS);
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_QUEUE_STATS_NUM, size-128-128));

    /* Policer */
    if (stats_global_cfg->stats_bitmap & CTC_STATS_FLOW_POLICER_STATS)
    {
        stats_global_cfg->policer_stats_num = (stats_global_cfg->policer_stats_num + 63) / 64 * 64;
        step += stats_global_cfg->policer_stats_num;

        gb_stats_master[lchip]->policer_stats_confirm_base = size;
        tmp = size >> 6;
        cmd = DRV_IOW(PolicingMiscCtl_t, PolicingMiscCtl_StatsConfirmBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
        {
            goto error;
        }

        size += step;

        gb_stats_master[lchip]->policer_stats_exceed_base = size;
        tmp = size >> 6;
        cmd = DRV_IOW(PolicingMiscCtl_t, PolicingMiscCtl_StatsNotConfirmBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
        {
            goto error;
        }

        size += step;

        gb_stats_master[lchip]->policer_stats_violate_base = size;
        tmp = size >> 6;
        cmd = DRV_IOW(PolicingMiscCtl_t, PolicingMiscCtl_StatsViolateBasePtr_f);
        if ((ret = DRV_IOCTL(lchip, 0, cmd, &tmp)) < 0)
        {
            goto error;
        }

        size += step;

        /* set chip_capability */
        CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_POLICER_STATS_NUM, step*3));
    }

    /* vrf */
    if (stats_global_cfg->stats_bitmap & CTC_STATS_VRF_STATS)
    {
        gb_stats_master[lchip]->vrf_stats_base = size;
        step = sys_greatbelt_l3if_get_max_vrfid(lchip, MAX_CTC_IP_VER);
        size += step;
    }

    /* flow stats */
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_type = FLOW_STATS_SRAM;
    opf.pool_index = 0;

    if (size >= max_stats_index)
    {
        ret = CTC_E_NO_RESOURCE;
        goto error;
    }

    max_flow_stats_index = max_stats_index - size;
    if ((ret = sys_greatbelt_opf_init_offset(lchip, &opf, size,  max_flow_stats_index)) < 0)
    {
        goto error;
    }

    /* set chip_capability */
    CTC_ERROR_RETURN(sys_greatbelt_global_set_chip_capability(lchip, CTC_GLOBAL_CAPABILITY_TOTAL_STATS_NUM, max_stats_index));

    /* vlan stats, must align with 4K */
    if (stats_global_cfg->stats_bitmap & CTC_STATS_VLAN_STATS)
    {
        if (gb_stats_master[lchip]->phb_bitmap)
        {
            return CTC_E_STATS_VLAN_STATS_CONFLICT_WITH_PHB;
        }

        gb_stats_master[lchip]->ingress_vlan_stats_base = (size + 4095) / 4096 * 4096;
        gb_stats_master[lchip]->egress_vlan_stats_base = gb_stats_master[lchip]->ingress_vlan_stats_base + 4096;
        tmp_size = gb_stats_master[lchip]->ingress_vlan_stats_base - size;

        if ((ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, tmp_size, &tmp_offset)) < 0)
        {
            goto error;
        }

        if ((ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, 4096*2, &vlan_offset)) < 0)
        {
            goto error;
        }

        sys_greatbelt_opf_free_offset(lchip, &opf, tmp_size, tmp_offset);

        sal_memset(&ipe_ds_vlan_ctl, 0, sizeof(ipe_ds_vlan_ctl));
        cmd = DRV_IOR(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));
        ipe_ds_vlan_ctl.stats_en = 1;
        ipe_ds_vlan_ctl.stats_mode = 0;
        ipe_ds_vlan_ctl.vlan_stats_ucast_ptr_bit = gb_stats_master[lchip]->ingress_vlan_stats_base >> 12;
        ipe_ds_vlan_ctl.vlan_stats_mcast_ptr_bit = gb_stats_master[lchip]->ingress_vlan_stats_base >> 12;
        ipe_ds_vlan_ctl.vlan_stats_bcast_ptr_bit = gb_stats_master[lchip]->ingress_vlan_stats_base >> 12;
        cmd = DRV_IOW(IpeDsVlanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ds_vlan_ctl));

        sal_memset(&epe_next_hop_ctl, 0, sizeof(epe_next_hop_ctl));
        cmd = DRV_IOR(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));
        epe_next_hop_ctl.vlan_stats_en = 1;
        epe_next_hop_ctl.stats_mode = 0;
        epe_next_hop_ctl.vlan_stats_ucast_ptr_bit = gb_stats_master[lchip]->egress_vlan_stats_base >> 12;
        epe_next_hop_ctl.vlan_stats_mcast_ptr_bit = gb_stats_master[lchip]->egress_vlan_stats_base >> 12;
        epe_next_hop_ctl.vlan_stats_bcast_ptr_bit = gb_stats_master[lchip]->egress_vlan_stats_base >> 12;
        cmd = DRV_IOW(EpeNextHopCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &epe_next_hop_ctl));

    }

    /* mac stats */
    if ((ret = _sys_greatbelt_stats_mac_stats_init(lchip)) < 0)
    {
        goto error;
    }
    tmp = 1;
    cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_SaturateEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    gb_stats_master[lchip]->saturate_en[CTC_STATS_TYPE_FWD] = 1;

    for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
    {
        /*GMAC*/
        if (drv_greatbelt_qmac_is_enable(lchip, index))
        {
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, \
                          QuadMacStatsCfg_IncrSaturate_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }
    gb_stats_master[lchip]->saturate_en[CTC_STATS_TYPE_GMAC] = 1;

    for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
    {
        /*SGMAC*/
        if (drv_greatbelt_sgmac_is_enable(lchip, index - SYS_STATS_SGMAC_STATS_RAM0))
        {
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, \
                          SgmacStatsCfg_IncrSaturate_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }
    gb_stats_master[lchip]->saturate_en[CTC_STATS_TYPE_SGMAC] = 1;

    /*CPU MAC*/
    cmd = DRV_IOW(CpuMacStatsUpdateCtl_t, CpuMacStatsUpdateCtl_IncrSaturate_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    gb_stats_master[lchip]->saturate_en[CTC_STATS_TYPE_CPUMAC] = 1;

    /*DS STATS*/
    cmd = DRV_IOW(GlobalStatsCtl_t, GlobalStatsCtl_ClearOnRead_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_FWD] = 1;

    for (index = SYS_STATS_MAC_STATS_RAM0; index <= SYS_STATS_MAC_STATS_RAM11; index++)
    {
        /*GMAC*/
        if (drv_greatbelt_qmac_is_enable(lchip, index))
        {
            reg_step = QuadMacStatsCfg1_t - QuadMacStatsCfg0_t;
            reg_id = QuadMacStatsCfg0_t + (index - SYS_STATS_MAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, \
                          QuadMacStatsCfg_ClearOnRead_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }
    gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_GMAC] = 1;

    for (index = SYS_STATS_SGMAC_STATS_RAM0; index <= SYS_STATS_SGMAC_STATS_RAM11; index++)
    {
        /*SGMAC*/
        if (drv_greatbelt_sgmac_is_enable(lchip, index-SYS_STATS_SGMAC_STATS_RAM0))
        {
            reg_step = SgmacStatsCfg1_t - SgmacStatsCfg0_t;
            reg_id = SgmacStatsCfg0_t + (index - SYS_STATS_SGMAC_STATS_RAM0) * reg_step;
            cmd = DRV_IOW(reg_id, \
                          SgmacStatsCfg_ClearOnRead_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
        }
    }
    gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_SGMAC] = 1;

    /*CPU MAC*/
    cmd = DRV_IOW(CpuMacStatsUpdateCtl_t, CpuMacStatsUpdateCtl_ClearOnRead_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));
    gb_stats_master[lchip]->clear_read_en[CTC_STATS_TYPE_CPUMAC] = 1;

    if ((ret = _sys_greatbelt_stats_init_done(lchip)) < 0)
    {
        goto error;
    }

    if ((ret = _sys_greatbelt_stats_statsid_init(lchip)) < 0)
    {
        goto error;
    }

    return CTC_E_NONE;

error:
    _sys_greatbelt_stats_deinit_start(lchip);

    return ret;
}

STATIC int32
_sys_greatbelt_stats_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_stats_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == gb_stats_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, FLOW_STATS_SRAM);

    if (gb_stats_master[lchip]->stats_mode == CTC_STATS_MODE_DEFINE)
    {
        sys_greatbelt_opf_deinit(lchip, OPF_STATS_STATSID);
    }

    /*free stats statsid*/
    ctc_hash_traverse(gb_stats_master[lchip]->stats_statsid_hash, (hash_traversal_fn)_sys_greatbelt_stats_free_node_data, NULL);
    ctc_hash_free(gb_stats_master[lchip]->stats_statsid_hash);

    /*free flow stats*/
    ctc_hash_traverse(gb_stats_master[lchip]->flow_stats_hash, (hash_traversal_fn)_sys_greatbelt_stats_free_node_data, NULL);
    ctc_hash_free(gb_stats_master[lchip]->flow_stats_hash);

    sal_mutex_destroy(gb_stats_master[lchip]->p_stats_mutex);
    mem_free(gb_stats_master[lchip]);

    return CTC_E_NONE;
}


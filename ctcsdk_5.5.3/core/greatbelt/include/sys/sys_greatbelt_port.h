/**
 @file sys_greatbelt_port.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-9

 @version v2.0

*/

#ifndef _SYS_GREATBELT_PORT_H
#define _SYS_GREATBELT_PORT_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"

#include "ctc_const.h"
#include "ctc_vlan.h"
#include "ctc_port.h"
#include "ctc_security.h"
#include "ctc_interrupt.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
/**
  @brief   Define port property flags
*/
enum sys_port_internal_property_e
{
    SYS_PORT_PROP_DEFAULT_PCP = 0,
    SYS_PORT_PROP_DEFAULT_DEI = 1,
    SYS_PORT_PROP_OAM_TUNNEL_EN = 2,
    SYS_PORT_PROP_IGS_OAM_VALID = 3,
    SYS_PORT_PROP_EGS_OAM_VALID = 4,
    SYS_PORT_PROP_DISCARD_NONE_8023OAM_EN = 5,
    SYS_PORT_PROP_REPLACE_TAG_EN = 6,
    SYS_PORT_PROP_USE_INNER_COS = 7,
    SYS_PORT_PROP_EXCEPTION_EN = 8,
    SYS_PORT_PROP_EXCEPTION_DISCARD = 9,
    SYS_PORT_PROP_SECURITY_EXCP_EN = 10,
    SYS_PORT_PROP_SECURITY_EN = 11,
    SYS_PORT_PROP_MAC_SECURITY_DISCARD = 12,
    SYS_PORT_PROP_QOS_POLICY,
    SYS_PORT_PROP_REPLACE_DSCP_EN,
    SYS_PORT_PROP_STMCTL_EN,
    SYS_PORT_PROP_STMCTL_OFFSET,
    SYS_PORT_PROP_LINK_OAM_EN,
    SYS_PORT_PROP_LINK_OAM_MEP_INDEX,
    SYS_PORT_PROP_SERVICE_POLICER_VALID,
    SYS_PORT_PROP_SPEED_MODE,
    SYS_PORT_PROP_NUM
};
typedef enum sys_port_internal_property_e  sys_port_internal_property_t;

enum sys_port_internal_direction_property_e
{
    SYS_PORT_DIR_PROP_L2_SPAN_EN,
    SYS_PORT_DIR_PROP_L2_SPAN_ID,
    SYS_PORT_DIR_PROP_ETHER_OAM_EDGE_PORT,
    SYS_PORT_DIR_PROP_LINK_LM_TYPE,
    SYS_PORT_DIR_PROP_LINK_LM_COS,
    SYS_PORT_DIR_PROP_LINK_LM_COS_TYPE,
    SYS_PORT_DIR_PROP_LINK_LM_INDEX_BASE,
    SYS_PORT_DIR_PROP_ETHER_LM_VALID,
    SYS_PORT_DIR_PROP_MPLS_SECTION_LM_EN,
    SYS_PORT_DIR_PROP_NUM
};
typedef enum sys_port_internal_direction_property_e  sys_port_internal_direction_property_t;

enum sys_port_pause_frame_type_e
{
    SYS_PORT_PAUSE_FRAME_TYPE_NORMAL,
    SYS_PORT_PAUSE_FRAME_TYPE_PFC,
    SYS_PORT_PAUSE_FRAME_TYPE_NUM
};
typedef enum sys_port_pause_frame_type_e sys_port_pause_frame_type_t;

#define QUADPCS_QUAD_STEP                (QuadPcsPcs0AnegCfg1_t - QuadPcsPcs0AnegCfg0_t)
#define QUADPCS_PCS_STEP                 (QuadPcsPcs1AnegCfg0_t - QuadPcsPcs0AnegCfg0_t)
#define QSGMIIPCS_QUAD_STEP              (QsgmiiPcs0AnegCfg1_t - QsgmiiPcs0AnegCfg0_t)
#define QSGMIIPCS_PCS_STEP               (QsgmiiPcs1AnegCfg0_t - QsgmiiPcs0AnegCfg0_t)
#define SYS_GREATBELT_MAX_FRAME_SIZE     16128
#define SYS_GREATBELT_DEFAULT_FRAME_SIZE 9600u
#define SYS_PORT_MAX_LOG_NUM 32


struct sys_igs_port_prop_s
{
    uint32 default_vlan       :1;
    uint32 port_mac_en        :1;
    uint32 lbk_en             :1;
    uint32 acl_en             :1;
    uint32 subif_en           :1;
    uint32 route_enable       :1;
    uint32 bridge_en            :1;
    uint32 receive_disable    :1;
    uint32 routed_port        :1;
    uint32 src_outer_is_svlan :1;
    uint32 pvlan_type         :2;
    uint32 speed_mode         :3;
    uint32 port_mac_type      :3;
    uint32 random_log_percent :8;
    uint32 stretch_en         :1;
    uint32 aneg_en            :1;
    uint32 link_intr             :1;
    uint32 unidir_en            :1;
    uint32 rsv0               :9;

    uint32 inter_lport        :8;
    uint32 rsv1               :24;

    uint16 global_src_port;
    uint16 l3if_id;

    uint16 flag;
    uint16 flag_ext;

    uint32 nhid;
};
typedef struct sys_igs_port_prop_s sys_igs_port_prop_t;

struct sys_egs_port_prop_s
{
    uint32 acl_en             :1;
    uint32 bridge_en          :1;
    uint32 pading_en          :1;
    uint32 transmit_disable   :1;
    uint32 routed_port        :1;
    uint32 dot1q_type         :2;
    uint32 restriction_type   :2;
    uint32 pvlan_type         :2;
    uint32 tx_threshold       :8;
    uint32 default_vlan       :1;
    uint32 random_log_percent :8;
    uint32 unidir_en            :1;
    uint32 rsv                :3;

    uint16 flag;
    uint16 flag_ext;
};
typedef struct sys_egs_port_prop_s sys_egs_port_prop_t;

struct sys_greatbelt_scan_log_s
{
    uint8 valid;
    uint8 type;  /*bit1:link down, bit0:code error*/
    uint16 gport;
    char time_str[50];
};
typedef struct sys_greatbelt_scan_log_s sys_greatbelt_scan_log_t;

struct sys_port_master_s
{
    sys_igs_port_prop_t* igs_port_prop;
    sys_egs_port_prop_t* egs_port_prop;
    sal_mutex_t*          p_port_mutex;
    uint8                 use_logic_port_check;
    uint8                 polling_status;
    uint8                 rsv[2];

    uint16*                pp_mtu1;
    uint16*                pp_mtu2;

    CTC_INTERRUPT_EVENT_FUNC link_status_change_cb;
    sal_task_t*  p_monitor_scan;
    sys_greatbelt_scan_log_t scan_log[SYS_PORT_MAX_LOG_NUM];

    uint32  status_1g[2];    /* link status of 1G port */
    uint32  status_xg[2];    /* link status of XG port */
};
typedef struct sys_port_master_s sys_port_master_t;

enum sys_port_pcs_type_s
{
    SYS_PORT_FROM_QUAD_PCS,
    SYS_PORT_FROM_QSGMII_PCS,
    SYS_PORT_FROM_SGMAC_PCS,

    SYS_PORT_PCS_MAX_TYPE
};
typedef enum sys_port_pcs_type_s sys_port_pcs_type_t;


extern sys_port_master_t* p_gb_port_master[CTC_MAX_LOCAL_CHIP_NUM];
#define PORT_LOCK \
    if (p_gb_port_master[lchip]->p_port_mutex) sal_mutex_lock(p_gb_port_master[lchip]->p_port_mutex)
#define PORT_UNLOCK \
    if (p_gb_port_master[lchip]->p_port_mutex) sal_mutex_unlock(p_gb_port_master[lchip]->p_port_mutex)

/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_greatbelt_port_init(uint8 lchip, ctc_port_global_cfg_t* p_port_global_cfg);

/**
 @brief De-Initialize port module
*/
extern int32
sys_greatbelt_port_deinit(uint8 lchip);
extern int32
sys_greatbelt_port_set_default_cfg(uint8 lchip, uint16 gport);

/*external property*/
extern int32
sys_greatbelt_port_set_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32 value);
extern int32
sys_greatbelt_port_get_property(uint8 lchip, uint32 gport, ctc_port_property_t port_prop, uint32* p_value);
extern int32
sys_greatbelt_port_set_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction);
extern int32
sys_greatbelt_port_get_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction);
extern int32
sys_greatbelt_port_set_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32 value);
extern int32
sys_greatbelt_port_get_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value);
/*internal property*/
extern int32
sys_greatbelt_port_set_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32 value);
extern int32
sys_greatbelt_port_get_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32* p_value);
extern int32
sys_greatbelt_port_set_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, ctc_direction_t dir, uint32 value);
extern int32
sys_greatbelt_port_get_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, ctc_direction_t dir, uint32* p_value);
int32
sys_greatbelt_port_get_mac_en(uint8 lchip, uint16 gport, uint32* p_enable);
extern int32
sys_greatbelt_port_set_global_port(uint8 lchip, uint8 lport, uint16 gl_port);
extern int32
sys_greatbelt_port_get_global_port(uint8 lchip, uint8 lport, uint16* p_gl_port);

extern int32
sys_greatbelt_port_set_phy_if_en(uint8 lchip, uint16 gport, bool enable);
extern int32
sys_greatbelt_port_get_phy_if_en(uint8 lchip, uint16 gport, uint16* p_l3if_id, bool* p_enable);

extern int32
sys_greatbelt_port_set_sub_if_en(uint8 lchip, uint16 gport, bool enable);
extern int32
sys_greatbelt_port_get_sub_if_en(uint8 lchip, uint16 gport, bool* p_enable);

extern int32
sys_greatbelt_port_set_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 sal_id, ctc_port_scl_key_type_t type);
extern int32
sys_greatbelt_port_get_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 sal_id, ctc_port_scl_key_type_t* p_type);

extern int32
sys_greatbelt_port_set_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool enable);
extern int32
sys_greatbelt_port_get_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool* p_enable);

extern int32
sys_greatbelt_port_get_mac_link_up(uint8 lchip, uint32 gport, uint32* p_is_up, uint32 is_port);

extern int32
sys_greatbelt_port_set_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop);
extern int32
sys_greatbelt_port_get_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop);

extern int32
sys_greatbelt_set_cpu_mac_en(uint8 lchip, bool enable);
extern int32
sys_greatbelt_get_cpu_mac_en(uint8 lchip, bool* p_enable);
extern int32
sys_greatbelt_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk);

extern int32
sys_greatbelt_set_logic_port_check_en(uint8 lchip, bool enable);
extern int32
sys_greatbelt_get_logic_port_check_en(uint8 lchip, bool* p_enable);

extern int32
sys_greatbelt_set_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8 size);
extern int32
sys_greatbelt_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size);

extern int32
sys_greatbelt_port_get_quad_use_num(uint8 lchip, uint8 gmac_id, uint8* p_use_num);

extern int32
sys_greatbelt_port_set_port_mac_prefix(uint8 lchip, ctc_port_mac_prefix_t* p_port_mac);

extern int32
sys_greatbelt_port_set_port_mac_postfix(uint8 lchip, uint16 gport, ctc_port_mac_postfix_t* p_port_mac);

extern int32
sys_greatbelt_port_get_port_mac(uint8 lchip, uint16 gport, mac_addr_t* p_port_mac);

extern int32
sys_greatbelt_port_set_reflective_bridge_en(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_greatbelt_port_get_reflective_bridge_en(uint8 lchip, uint16 gport, bool* p_enable);

extern int32
sys_greatbelt_port_set_port_cross_connect(uint8 lchip, uint16 gport, uint32 nhid);

extern int32
sys_greatbelt_port_get_port_cross_connect(uint8 lchip, uint16 gport, uint32* p_value);

extern int32
sys_greatbelt_port_set_link_status_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb);


extern int32
sys_greatbelt_port_set_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop);

extern int32
sys_greatbelt_port_get_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop);

extern int32
sys_greatbelt_port_get_port_type(uint8 lchip, uint16 lport,  uint8* p_type);

extern int32
sys_greatbelt_port_swap(uint8 lchip, uint8 src_lport, uint8 dst_lport);

extern int32
sys_greatbelt_port_get_monitor_rlt(uint8 lchip);

extern int32
sys_greatbelt_port_dest_gport_check(uint8 lchip, uint32 gport);

extern int32
sys_greatbelt_port_set_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, bool enable);

extern int32
sys_greatbelt_port_get_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32* p_value);

extern int32
sys_greatbelt_port_set_mac_auth(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_greatbelt_port_get_mac_auth(uint8 lchip, uint16 gport, bool* enable);

extern int32
sys_greatbelt_port_get_capability(uint8 lchip, uint16 gport, ctc_port_capability_type_t type, void* p_value);

extern int32
sys_greatbelt_port_get_mac_status(uint8 lchip, uint16 gport, uint32* p_value);

extern int32
sys_greatbelt_port_reset_mac_en(uint8 lchip, uint16 gport, uint32 value);

extern int32
sys_greatbelt_port_set_monitor_link_enable(uint8 lchip, uint32 enable);

extern int32
sys_greatbelt_port_get_monitor_link_enable(uint8 lchip, uint32* p_value);

extern int32
sys_greatbelt_port_store_led_mode_to_lport_attr(uint8 lchip, uint8 mac_id, uint8 first_led_mode, uint8 sec_led_mode);

extern int32
sys_greatbelt_port_link_down_event(uint8 lchip, uint32 gport);

extern int32
sys_greatbelt_port_link_up_event(uint8 lchip, uint32 gport);

extern int32
sys_greatbelt_port_set_station_move(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value);

extern int32
sys_greatbelt_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value);

extern int32
sys_greatbelt_port_switch_src_port(uint8 lchip, uint8 lport_valid, uint16 serdes_idx, uint8 serdes_count);

extern int32
sys_greatbelt_port_switch_dest_port(uint8 lchip, uint8 lport_valid, uint8 mac_id, uint16 dest_lport);

extern int32
sys_greatbelt_port_set_mac_stats_incrsaturate(uint8 lchip, uint8 stats_type, uint32 value);

extern int32
sys_greatbelt_port_set_mac_stats_clear(uint8 lchip, uint8 stats_type, uint32 value);

#ifdef __cplusplus
}
#endif

#endif


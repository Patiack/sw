/**
 @file sys_goldengate_port.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2011-11-9

 @version v2.0

*/

#ifndef _SYS_GOLDENGATE_PORT_H
#define _SYS_GOLDENGATE_PORT_H
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
    SYS_PORT_PROP_OAM_VALID = 3,
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
    SYS_PORT_PROP_PKT_TYPE,
    SYS_PORT_PROP_ILOOP_EN,
    SYS_PORT_PROP_HASH_GEN_DIS,
    SYS_PORT_PROP_LINK_OAM_LEVEL,
    SYS_PORT_PROP_VLAN_OPERATION_DIS,
    SYS_PORT_PROP_UNTAG_DEF_VLAN_EN,
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
    SYS_PORT_DIR_PROP_IPFIX_LKUP_TYPE,
    SYS_PORT_DIR_PROP_IPFIX_SELECT_ID,
    SYS_PORT_DIR_PROP_OAM_VALID,
    SYS_PORT_DIR_PROP_NUM
};
typedef enum sys_port_internal_direction_property_e  sys_port_internal_direction_property_t;

enum sys_port_pause_frame_type_e
{
    SYS_PORT_PAUSE_FRAME_TYPE_NORMAL = 0x1,
    SYS_PORT_PAUSE_FRAME_TYPE_PFC = 0x2,
    SYS_PORT_PAUSE_FRAME_TYPE_NUM = 2
};
typedef enum sys_port_pause_frame_type_e sys_port_pause_frame_type_t;

enum sys_port_mac_property_e
{
    SYS_PORT_MAC_PROPERTY_PADDING,
    SYS_PORT_MAC_PROPERTY_PREAMBLE,
    SYS_PORT_MAC_PROPERTY_CHKCRC,
    SYS_PORT_MAC_PROPERTY_STRIPCRC,
    SYS_PORT_MAC_PROPERTY_APPENDCRC,
    SYS_PORT_MAC_PROPERTY_ENTXERR,
    SYS_PORT_MAC_PROPERTY_CODEERR,
    SYS_PORT_MAC_PROPERTY_NUM
};
typedef enum sys_port_mac_property_e sys_port_mac_property_t;

enum sys_port_3ap_e
{
    SYS_PORT_3AP_TRAINING_DIS,                    /**< 802.3ap training disable */
    SYS_PORT_3AP_TRAINING_EN,                     /**< when use 802.3ap training, need to cfg enable firstly */
    SYS_PORT_3AP_TRAINING_INIT,                   /**< when use 802.3ap training, need to cfg init secondly */
    SYS_PORT_3AP_TRAINING_DONE                    /**< when use 802.3ap training, need to cfg done thirdly */
};
typedef enum sys_port_3ap_e sys_port_3ap_t;

enum sys_port_vlan_filter_mode_e
{
    SYS_PORT_VLAN_FILTER_MODE_VLAN,
    SYS_PORT_VLAN_FILTER_MODE_ENABLE,
    SYS_PORT_VLAN_FILTER_MODE_DISABLE,
    SYS_PORT_VLAN_FILTER_MODE_NUM
};
typedef enum sys_port_vlan_filter_mode_e sys_port_vlan_filter_mode_t;

enum sys_port_mac_status_type_e
{
    SYS_PORT_MAC_STATUS_TYPE_LINK,
    SYS_PORT_MAC_STATUS_TYPE_CODE_ERR
};
typedef enum sys_port_mac_status_type_e sys_port_mac_status_type_t;

#define QUADPCS_QUAD_STEP                       (QuadPcsPcs0AnegCfg1_t - QuadPcsPcs0AnegCfg0_t)
#define QUADPCS_PCS_STEP                        (QuadPcsPcs1AnegCfg0_t - QuadPcsPcs0AnegCfg0_t)
#define QSGMIIPCS_QUAD_STEP                     (QsgmiiPcs0AnegCfg1_t - QsgmiiPcs0AnegCfg0_t)
#define QSGMIIPCS_PCS_STEP                      (QsgmiiPcs1AnegCfg0_t - QsgmiiPcs0AnegCfg0_t)

#define SYS_MAX_GMAC_NUM                              128
#define SYS_MAX_SGMAC_NUM                             48
#define SYS_GOLDENGATE_ISOLATION_GROUP_NUM            32
#define SYS_GOLDENGATE_ISOLATION_ID_NUM               32

#define SYS_GOLDENGATE_MIN_FRAMESIZE_MIN_VALUE        34
#define SYS_GOLDENGATE_MIN_FRAMESIZE_MAX_VALUE        127 /*GB is 64, 127 for cover emu test */
#define SYS_GOLDENGATE_MIN_FRAMESIZE_DEFAULT_VALUE    64

#define SYS_GOLDENGATE_MAX_FRAMESIZE_MIN_VALUE        128
#define SYS_GOLDENGATE_MAX_FRAMESIZE_MAX_VALUE        16127
#define SYS_GOLDENGATE_MAX_FRAMESIZE_DEFAULT_VALUE    9600u
#define SYS_PORT_MAX_LOG_NUM 32
#define PORT_CASECADE_INDEX(lport, tbl_name) ((lport & 0xFF) +  (SYS_MAP_DRV_LPORT_TO_SLICE(lport) * (TABLE_MAX_INDEX(tbl_name) / 2)))

struct sys_igs_port_prop_s
{
    uint32 port_mac_en        :1;
    uint32 lbk_en             :1;
    uint32 speed_mode         :3;
    uint32 subif_en           :1;
    uint32 training_status    :3;
    uint32 cl73_status        :2;   /* 0-dis, 1-en, 2-finish */
    uint32 cl73_old_status    :2;
    uint32 link_intr_en       :1;
    uint32 link_status        :1;
    uint32 rsv0               :17;

    uint32 inter_lport        :16;
    uint32 global_src_port    :16;

    uint8 flag;
    uint8 flag_ext;

    uint8 isolation_id;
    uint8 rsv1;

    uint32 nhid;
};
typedef struct sys_igs_port_prop_s sys_igs_port_prop_t;

struct sys_egs_port_prop_s
{
    uint8 flag;
    uint8 flag_ext;

    uint8 isolation_id;
    uint8 isolation_ref_cnt;
};
typedef struct sys_egs_port_prop_s sys_egs_port_prop_t;

struct sys_goldengate_scan_log_s
{
    uint8 valid;
    uint16 gport;
    uint8 type;  /*bit1:link down, bit0:code error*/
    char time_str[50];
};
typedef struct sys_goldengate_scan_log_s sys_goldengate_scan_log_t;

struct sys_port_master_s
{
    sys_igs_port_prop_t* igs_port_prop;
    sys_egs_port_prop_t* egs_port_prop;
    sal_mutex_t*          p_port_mutex;
    uint8                 use_logic_port_check;
    uint8                 polling_status;
    uint8                 auto_neg_restart_status;
    uint8                 mlag_isolation_en;
    CTC_INTERRUPT_EVENT_FUNC link_status_change_cb;
    sal_task_t*  p_monitor_scan;
    sal_task_t*  p_auto_neg_restart;
    sys_goldengate_scan_log_t scan_log[SYS_PORT_MAX_LOG_NUM];
    uint8 cur_log_index;
    uint8 use_isolation_id;
    uint32 mac_bmp[4]; /* used for TAP, record tap dest, 0~1 for slice0, 2~3 for slice1 */
    uint32 igs_isloation_bitmap;
    uint32 egs_isloation_bitmap;

};
typedef struct sys_port_master_s sys_port_master_t;

extern sys_port_master_t* p_gg_port_master[CTC_MAX_LOCAL_CHIP_NUM];
#define PORT_LOCK \
    if (p_gg_port_master[lchip]->p_port_mutex) sal_mutex_lock(p_gg_port_master[lchip]->p_port_mutex)
#define PORT_UNLOCK \
    if (p_gg_port_master[lchip]->p_port_mutex) sal_mutex_unlock(p_gg_port_master[lchip]->p_port_mutex)

#define SYS_PORT_INIT_CHECK(lchip)          \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_port_master[lchip]){  \
            return CTC_E_NOT_INIT; }        \
    } while (0)

#define CTC_ERROR_RETURN_WITH_PORT_UNLOCK(op) \
    do { \
        int32 rv; \
        if ((rv = (op)) < 0) \
        { \
            sal_mutex_unlock(p_gg_port_master[lchip]->p_port_mutex); \
            return (rv); \
        } \
    } while (0)

#define SYS_PORT_DBG_DUMP(FMT, ...)                                                   \
    {                                                                                \
        CTC_DEBUG_OUT(port, port, PORT_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);  \
    }

/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_port_init(uint8 lchip, ctc_port_global_cfg_t* p_port_global_cfg);

/**
 @brief De-Initialize port module
*/
extern int32
sys_goldengate_port_deinit(uint8 lchip);

extern int32
sys_goldengate_port_set_default_cfg(uint8 lchip, uint16 gport);

/*external property*/
extern int32
sys_goldengate_port_set_property(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value);

extern int32
sys_goldengate_port_get_property(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32* p_value);

extern int32
sys_goldengate_port_set_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction);

extern int32
sys_goldengate_port_get_restriction(uint8 lchip, uint16 gport, ctc_port_restriction_t* p_restriction);

extern int32
sys_goldengate_port_set_isolation(uint8 lchip, ctc_port_isolation_t* p_restriction);

extern int32
sys_goldengate_port_get_isolation(uint8 lchip, ctc_port_isolation_t* p_restriction);

extern int32
sys_goldengate_port_set_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32 value);

extern int32
sys_goldengate_port_get_direction_property(uint8 lchip, uint16 gport, ctc_port_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value);
/*internal property*/

extern int32
sys_goldengate_port_set_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32 value);

extern int32
sys_goldengate_port_get_internal_property(uint8 lchip, uint16 gport, sys_port_internal_property_t port_prop, uint32* p_value);

extern int32
sys_goldengate_port_set_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_direction_property_t port_prop, ctc_direction_t dir, uint32 value);

extern int32
sys_goldengate_port_get_internal_direction_property(uint8 lchip, uint16 gport, sys_port_internal_direction_property_t port_prop, ctc_direction_t dir, uint32* p_value);

extern int32
sys_goldengate_port_get_mac_en(uint8 lchip, uint16 gport, uint32* p_enable);

extern int32
sys_goldengate_port_update_mc_linkagg(uint8 lchip, uint8 tid, uint16 lport, bool is_add);

extern int32
sys_goldengate_port_set_global_port(uint8 lchip, uint16 lport, uint16 gl_port, bool update_mc_linkagg);

extern int32
sys_goldengate_port_get_global_port(uint8 lchip, uint16 lport, uint16* p_gl_port);

extern int32
sys_goldengate_port_set_phy_if_en(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_goldengate_port_get_phy_if_en(uint8 lchip, uint16 gport, uint16* p_l3if_id, bool* p_enable);

extern int32
sys_goldengate_port_set_sub_if_en(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_goldengate_port_get_sub_if_en(uint8 lchip, uint16 gport, bool* p_enable);

extern int32
sys_goldengate_port_set_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 sal_id, ctc_port_scl_key_type_t type);

extern int32
sys_goldengate_port_get_scl_key_type(uint8 lchip, uint16 gport, ctc_direction_t direction, uint8 sal_id, ctc_port_scl_key_type_t* p_type);

extern int32
sys_goldengate_port_set_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool enable);

extern int32
sys_goldengate_port_get_vlan_range(uint8 lchip, uint16 gport, ctc_vlan_range_info_t* p_vrange_info, bool* p_enable);

extern int32
sys_goldengate_port_get_mac_link_up(uint8 lchip, uint16 gport, uint32* p_is_up, uint32 is_port);

extern int32
sys_goldengate_port_set_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop);

extern int32
sys_goldengate_port_get_flow_ctl_en(uint8 lchip, ctc_port_fc_prop_t *fc_prop);

extern int32
sys_goldengate_port_set_loopback(uint8 lchip, ctc_port_lbk_param_t* p_port_lbk);

extern int32
sys_goldengate_set_logic_port_check_en(uint8 lchip, bool enable);
extern int32
sys_goldengate_get_logic_port_check_en(uint8 lchip, bool* p_enable);

extern int32
sys_goldengate_set_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8 size);

extern int32
sys_goldengate_get_ipg_size(uint8 lchip, ctc_ipg_size_t index, uint8* p_size);

extern int32
sys_goldengate_port_get_quad_use_num(uint8 lchip, uint8 gmac_id, uint8* p_use_num);

extern int32
sys_goldengate_port_set_port_mac_prefix(uint8 lchip, ctc_port_mac_prefix_t* p_port_mac);

extern int32
sys_goldengate_port_set_port_mac_postfix(uint8 lchip, uint16 gport, ctc_port_mac_postfix_t* p_port_mac);

extern int32
sys_goldengate_port_get_port_mac(uint8 lchip, uint16 gport, mac_addr_t* p_port_mac);

extern int32
sys_goldengate_port_set_reflective_bridge_en(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_goldengate_port_get_reflective_bridge_en(uint8 lchip, uint16 gport, bool* p_enable);

extern int32
sys_goldengate_port_set_port_cross_connect(uint8 lchip, uint16 gport, uint32 nhid);

extern int32
sys_goldengate_port_get_port_cross_connect(uint8 lchip, uint16 gport, uint32* p_value);

extern int32
sys_goldengate_port_set_link_status_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb);

extern int32
sys_goldengate_port_set_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop);

extern int32
sys_goldengate_port_get_scl_property(uint8 lchip, uint16 gport, ctc_port_scl_property_t* prop);

extern int32
sys_goldengate_set_min_frame_size(uint8 lchip, ctc_frame_size_t index, uint16 value);

extern int32
sys_goldengate_get_min_frame_size(uint8 lchip, ctc_frame_size_t index, uint16* p_value);

extern int32
sys_goldengate_set_max_frame_size(uint8 lchip, ctc_frame_size_t index, uint16 value);

extern int32
sys_goldengate_get_max_frame_size(uint8 lchip, ctc_frame_size_t index, uint16* p_value);

extern int32
sys_goldengate_port_mac_set_property(uint8 lchip, uint16 gport, sys_port_mac_property_t property, uint32 value);

extern int32
sys_goldengate_port_mac_get_property(uint8 lchip, uint16 gport, sys_port_mac_property_t property, uint32* p_value);

extern int32
sys_goldengate_port_get_mac_signal_detect(uint8 lchip, uint16 gport, uint32* p_is_detect);

extern int32
sys_goldengate_port_get_mac_code_err(uint8 lchip, uint16 gport, uint32* code_err);

extern int32
sys_goldengate_port_show_port_maclink(uint8 lchip);

extern int32
sys_goldengate_port_get_3ap_training_en(uint8 lchip, uint16 gport, uint32* training_state);

extern int32
sys_goldengate_port_isr_cl73_complete_isr(uint8 lchip, void* p_data, void* p_data1);

/* ipg is mac tx property. value: 3 is 12Bytes, 2 is 8Bytes, 1 is 4Bytes, default is 12Bytes */
extern int32
sys_goldengate_port_mac_set_ipg(uint8 lchip, uint16 gport, uint32 value);

extern int32
sys_goldengate_port_get_mac_status(uint8 lchip, uint16 gport, uint32* p_value);

extern int32
sys_goldengate_port_reset_mac_en(uint8 lchip, uint16 gport, uint32 value);

extern int32
sys_goldengate_port_set_mac_auth(uint8 lchip, uint16 gport, bool enable);

extern int32
sys_goldengate_port_get_mac_auth(uint8 lchip, uint16 gport, bool* enable);

extern int32
sys_goldengate_port_get_capability(uint8 lchip, uint16 gport, ctc_port_capability_type_t type, void* p_value);

extern int32
sys_goldengate_port_set_monitor_link_enable(uint8 lchip, uint32 enable);

extern int32
sys_goldengate_port_get_monitor_link_enable(uint8 lchip, uint32* p_value);

extern int32
sys_goldengate_port_store_led_mode_to_lport_attr(uint8 lchip, uint8 mac_id, uint8 first_led_mode, uint8 sec_led_mode);

extern int32
sys_goldengate_port_link_down_event(uint8 lchip, uint32 gport);

extern int32
sys_goldengate_port_link_up_event(uint8 lchip, uint32 gport);

extern int32
sys_goldengate_port_set_station_move(uint8 lchip, uint16 gport, ctc_port_property_t port_prop, uint32 value);

extern int32
sys_goldengate_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value);
extern int32
sys_goldengate_port_set_if_status(uint8 lchip, uint16 lport, uint32 value);
#ifdef __cplusplus
}
#endif

#endif


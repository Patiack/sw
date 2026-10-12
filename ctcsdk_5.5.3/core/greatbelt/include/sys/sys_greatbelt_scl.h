
#if 1
/**
   @file sys_greatbelt_scl.h

   @date 2013-02-21

   @version v2.0

   This file is to define the API and the data structure used in scl
 */
#ifndef _SYS_GREATBELT_SCL_H
#define _SYS_GREATBELT_SCL_H

#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
*
* Header Files
*
***************************************************************/
#include "sal.h"

#include "ctc_const.h"
#include "ctc_stats.h"

#include "ctc_scl.h"
#include "sys_greatbelt_fpa.h"


/****************************************************************
*
* Defines and Macros
*
****************************************************************/
#define SYS_SCL_IPV4_ICMP                                 1
#define SYS_SCL_IPV4_IGMP                                 2
#define SYS_SCL_TCP                                       6
#define SYS_SCL_UDP                                       17
#define SYS_SCL_IPV6_ICMP                                 58
#define SYS_SCL_BY_PASS_VLAN_PTR                          0x1500
#define SYS_SCL_MPLS_DECAP_VLAN_PTR                       0xFFFF


#define SYS_SCL_DEFAULT_ENTRY_PORT_NUM                    128
#define SYS_SCL_LABEL_MAX                                 64
#define SYS_SCL_LABEL_RESERVE_FOR_IPSG                    63
#define SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS              62
#define SYS_SCL_LABEL_RESERVE_FOR_PORT_TYPE               0
#define SYS_SCL_VEDIT_PRFID_SVLAN_CLASS                   3
#define SYS_SCL_VEDIT_PRFID_SVLAN_SCOS_CLASS              2
#define SYS_SCL_VEDIT_PRFID_SVLAN_DOMAIN                  1

#define SYS_SCL_TRAVERSE_ALL                              0
#define SYS_SCL_TRAVERSE_BY_PORT                          1
#define SYS_SCL_TRAVERSE_BY_GROUP                         2

#define SYS_SCL_MIN_INNER_ENTRY_ID                        0x80000000 /* min sdk entry id*/
#define SYS_SCL_MAX_INNER_ENTRY_ID                        0xFFFFFFFF /* max sdk entry id*/

#define   SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL           0xFFFF000D /*(CTC_SCL_GROUP_ID_HASH_MAX+1),*/
#define   SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL           0xFFFF000E /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD        0xFFFF000F
#define   SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD        0xFFFF0010 /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS    0xFFFF0011
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS    0xFFFF0012
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS    0xFFFF0013
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS    0xFFFF0014
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC      0xFFFF0016
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4     0xFFFF0017
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6     0xFFFF0018
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC      0xFFFF0019 /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4     0xFFFF001A /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6     0xFFFF001B /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS          0xFFFF001C /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_CUSTOMER_ID              0xFFFF001D /* tcam*/
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE   0xFFFF0020 /* tcam */


#define   SYS_SCL_GROUP_ID_MAX                            0xFFFFFFFF
#define SCL_BLOCK_ID_NUM    2

enum sys_scl_app_e
{
    SYS_SCL_APP_IP_SRC_GUARD,
    SYS_SCL_APP_VLAN_CLASS,
    SYS_SCL_APP_VLAN_MAPPING,
    SYS_SCL_APP_IP_TUNNEL,
    SYS_SCL_APP_MAX
};


#define SCL_PORT_CLASS_ID_NUM    60


#define ____SYS_SCL_ACTION______

#define  KEY_TYPE
typedef enum
{
    SYS_SCL_KEY_TCAM_VLAN,
    SYS_SCL_KEY_TCAM_MAC,
    SYS_SCL_KEY_TCAM_IPV4,
    SYS_SCL_KEY_TCAM_IPV6,
    SYS_SCL_KEY_TCAM_TUNNEL_IPV4,
    SYS_SCL_KEY_TCAM_TUNNEL_IPV6,
    SYS_SCL_KEY_HASH_PORT,
    SYS_SCL_KEY_HASH_PORT_CVLAN,
    SYS_SCL_KEY_HASH_PORT_SVLAN,
    SYS_SCL_KEY_HASH_PORT_2VLAN,
    SYS_SCL_KEY_HASH_PORT_CVLAN_COS,
    SYS_SCL_KEY_HASH_PORT_SVLAN_COS,
    SYS_SCL_KEY_HASH_MAC,
    SYS_SCL_KEY_HASH_PORT_MAC,
    SYS_SCL_KEY_HASH_IPV4,
    SYS_SCL_KEY_HASH_PORT_IPV4,
    SYS_SCL_KEY_HASH_IPV6,
    SYS_SCL_KEY_HASH_L2,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF,
    SYS_SCL_KEY_PORT_DEFAULT_INGRESS,
    SYS_SCL_KEY_PORT_DEFAULT_EGRESS,
    SYS_SCL_KEY_NUM
}sys_scl_key_type_t;

#define ACTION_TYPE

typedef enum
{
    SYS_SCL_ACTION_INGRESS,
    SYS_SCL_ACTION_EGRESS,
    SYS_SCL_ACTION_TUNNEL,
    SYS_SCL_ACTION_NUM
}sys_scl_action_type_t;


typedef struct
{
    uint32 ip_sa;
    uint32 ip_da;

    uint8  l4_type;               /**< Layer 4 type. Refer to ctc_parser_l4_type_t */
    uint8  rsv0[3];
}sys_scl_hash_tunnel_ipv4_key_t;

typedef struct
{
    uint32 ip_sa;
    uint32 ip_da;
    uint32 gre_key;

    uint8  l4_type;              /**< Layer 4 type. Refer to ctc_parser_l4_type_t */
    uint8  rsv0[3];
}sys_scl_hash_tunnel_ipv4_gre_key_t;

typedef struct
{
    uint32 ip_sa;                       /**< IP-SA */
}sys_scl_hash_tunnel_ipv4_rpf_key_t;


typedef enum
{
    SYS_SCL_TUNNEL_GRE_KEY_VALID = 0x01,      /**<If set gre key is valid */
    SYS_SCL_TUNNEL_IPSA_VALID    = 0x02,      /**<If set ipsa is valid */
    SYS_SCL_TUNNEL_MAX
}sys_scl_tunnel_valid_flag_t;

typedef struct
{
    uint32 ipv4da;
    uint32 ipv4sa;
    uint32 gre_key;

    uint32 l4type : 4;
    uint32 flag   : 28;                 /* sys_scl_tunnel_valid_flag_t */
}sys_scl_tcam_tunnel_ipv4_key_t;

typedef struct
{
    uint32 ipv6_sa[4];
    uint32 ipv6_da[4];
    uint32 gre_key;

    uint32 l4type : 4;
    uint32 flag   : 28;                 /* sys_scl_tunnel_valid_flag_t */
}sys_scl_tcam_tunnel_ipv6_key_t;



typedef struct
{
    uint32 binding_data               : 16;
    uint32 aps_select_protecting_path : 1;
    uint32 service_policer_valid      : 1;
    uint32 aps_select_valid           : 1;
    uint32 rsv0                       : 1;
    uint32 aps_select_group_id        : 11;
    uint32 service_policer_mode       : 1;
}sys_scl_tunnel_binding_data_h_t;

typedef struct
{
    uint32 binding_data     : 32;

    uint32 rsv0             : 3;
    uint32 logic_src_port   : 14;
    uint32 rsv1             : 2;
    uint32 flow_policer_ptr : 13;   /* not used, must use chip.policer_ptr[lchip] */
}sys_scl_tunnel_binding_data_l_t;

typedef struct
{
    uint16 stats_ptr;
    uint16 policer_ptr;
}sys_scl_tunnel_action_chip_t;

typedef struct
{
    uint32                          trill_ttl_check_en         : 1;
    uint32                          trill_bfd_en               : 1;
    uint32                          pip_bypass_learning        : 1;
    uint32                          rpf_check_en               : 1;
    uint32                          tunnel_rpf_check_request   : 1;
    uint32                          logic_port_type            : 1;
    uint32                          service_acl_qos_en         : 1;
    uint32                          igmp_snoop_en              : 1; /* 8  */
    uint32                          isid_valid                 : 1;
    uint32                          binding_en                 : 1;
    uint32                          binding_mac_sa             : 1;
    uint32                          is_tunnel                  : 1;
    uint32                          tunnel_id_exception_en     : 1;
    uint32                          trill_bfd_echo_en          : 1;
    uint32                          src_queue_select           : 1;
    uint32                          trill_option_escape_en     : 1; /* 16 */
    uint32                          vsi_learning_disable       : 1;
    uint32                          mac_security_vsi_discard   : 1;
    uint32                          tunnel_packet_type         : 3; /* 21 */
    uint32                          tunnel_payload_offset      : 4; /* 25 */
    uint32                          use_default_vlan_tag       : 1;
    uint32                          ttl_check_en               : 1;
    uint32                          tunnel_gre_options         : 3; /* 30 */
    uint32                          isatap_check_en            : 1;
    uint32                          tunnel_ptp_en              : 1;

    uint32                          pbb_mcast_decap            : 1;
    uint32                          aging_valid                : 1;
    uint32                          aging_index                : 2;
    uint32                          deny_learning              : 1;
    uint32                          deny_bridge                : 1;
    uint32                          fid_type                   : 1;
    uint32                          inner_packet_lookup        : 1; /* 8  */
    uint32                          outer_vlan_is_cvlan        : 1;
    uint32                          acl_qos_use_outer_info     : 1;
    uint32                          ds_fwd_ptr_valid           : 1;
    uint32                          wlan_tunnel_type         : 1;
    uint32                          svlan_tpid_index           : 2; /* 14  */
    uint32                          fid                        : 16;
    uint32                          pbb_outer_learning_disable : 1;
    uint32                          pbb_outer_learning_enable  : 1;

    uint32                          stats_ptr                  : 14; /* not used, must use chip.stats_ptr[lchip] */
    uint32                          isid                       : 15;
    uint32                          user_default_cos           : 3;

    uint32                          ttl_update                 : 1;
    uint32                          user_default_cfi           : 1;
    uint32                          trill_multi_rpf_check      : 1;
    uint32                          trill_decap_without_loop   : 1;
    uint32                          trill_channel_en           : 1;
    uint32                          exception_sub_index        : 6;
    uint32                          esadi_check_en             : 1;
    uint32                          route_disable              : 1; /* 13  */
    uint32                          rsv0                       : 19;

    sys_scl_tunnel_binding_data_h_t binding_data_high;
    sys_scl_tunnel_binding_data_l_t binding_data_low;
    sys_scl_tunnel_action_chip_t    chip;
}sys_scl_tunnel_action_t;

typedef struct
{
    uint16 gport;
    uint16 rsv0;
}sys_scl_port_default_key_t;

typedef struct
{
    sys_scl_key_type_t type;                 /**< [HB.GB] CTC_SCL_XXX_KEY */

    union
    {
        ctc_scl_tcam_vlan_key_t            tcam_vlan_key;          /**< SCL VLAN key content */
        ctc_scl_tcam_mac_key_t             tcam_mac_key;           /**< SCL MAC  key content */
        ctc_scl_tcam_ipv4_key_t            tcam_ipv4_key;          /**< SCL IPv4 key content */
        ctc_scl_tcam_ipv6_key_t            tcam_ipv6_key;          /**< SCL IPv6 key content */

        ctc_scl_hash_port_key_t            hash_port_key;
        ctc_scl_hash_port_cvlan_key_t      hash_port_cvlan_key;
        ctc_scl_hash_port_svlan_key_t      hash_port_svlan_key;
        ctc_scl_hash_port_2vlan_key_t      hash_port_2vlan_key;
        ctc_scl_hash_port_cvlan_cos_key_t  hash_port_cvlan_cos_key;
        ctc_scl_hash_port_svlan_cos_key_t  hash_port_svlan_cos_key;
        ctc_scl_hash_port_mac_key_t        hash_port_mac_key;
        ctc_scl_hash_port_ipv4_key_t       hash_port_ipv4_key;
        ctc_scl_hash_mac_key_t             hash_mac_key;
        ctc_scl_hash_ipv4_key_t            hash_ipv4_key;
        ctc_scl_hash_ipv6_key_t            hash_ipv6_key;
        ctc_scl_hash_l2_key_t              hash_l2_key;

        sys_scl_port_default_key_t         port_default_key;
        sys_scl_tcam_tunnel_ipv4_key_t     tcam_tunnel_ipv4_key;
        sys_scl_tcam_tunnel_ipv6_key_t     tcam_tunnel_ipv6_key;
        sys_scl_hash_tunnel_ipv4_key_t     hash_tunnel_ipv4_key;
        sys_scl_hash_tunnel_ipv4_gre_key_t hash_tunnel_ipv4_gre_key;
        sys_scl_hash_tunnel_ipv4_rpf_key_t hash_tunnel_ipv4_rpf_key;
    } u;
}sys_scl_key_t;



typedef struct
{
    sys_scl_action_type_t type;
    uint32 action_flag;         /**< [GB.GG]original action flag in vlan mapping struct*/
    union
    {
        ctc_scl_igs_action_t    igs_action;
        ctc_scl_egs_action_t    egs_action;
        sys_scl_tunnel_action_t tunnel_action;
    } u;
}sys_scl_action_t;


/** @brief  scl entry struct */
typedef struct
{
    sys_scl_key_t    key;           /**< [HB.GB] scl key struct*/
    sys_scl_action_t action;        /**< [HB.GB] scl action struct*/

    uint32           entry_id;      /**< [HB.GB] scl action id. 0xFFFFFFFF indicates get entry id */
    uint8            priority_valid;
    uint32           priority;
}sys_scl_entry_t;




extern int32
sys_greatbelt_scl_init(uint8 lchip, void* scl_global_cfg);

/**
 @brief De-Initialize scl module
*/
extern int32
sys_greatbelt_scl_deinit(uint8 lchip);

extern int32
sys_greatbelt_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_greatbelt_scl_destroy_group(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_greatbelt_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_greatbelt_scl_uninstall_group(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_greatbelt_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_greatbelt_scl_add_entry(uint8 lchip, uint32 group_id, sys_scl_entry_t* scl_entry, uint8 inner);

extern int32
sys_greatbelt_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_greatbelt_scl_install_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_greatbelt_scl_uninstall_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_greatbelt_scl_remove_all_entry(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_greatbelt_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner);

extern int32
sys_greatbelt_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query, uint8 inner);

extern int32
sys_greatbelt_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry, uint8 inner);

extern int32
sys_greatbelt_scl_update_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner);

extern int32
sys_greatbelt_scl_set_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action);

extern int32
sys_greatbelt_scl_get_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action);

extern int32
sys_greatbelt_scl_show_status(uint8 lchip);

extern int32
sys_greatbelt_scl_get_hash_count(uint8 lchip);

/*internal use*/
extern int32
sys_greatbelt_scl_remove_all_inner_hash_vlan(uint8 lchip, uint16 gport, uint8 dir);

extern int32
sys_greatbelt_scl_get_entry_id(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid);

extern int32
sys_greatbelt_scl_get_entry(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid);

extern int32
sys_greatbelt_scl_get_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner);

extern int32
sys_greatbelt_scl_show_entry(uint8 lchip, uint8 type, uint32 param, sys_scl_key_type_t key_type, uint8 detail);
#endif



#ifdef __cplusplus
}
#endif
#endif


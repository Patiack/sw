
#if 1
/**
   @file sys_goldengate_scl.h

   @date 2013-02-21

   @version v2.0

   This file is to define the API and the data structure used in scl
 */
#ifndef _SYS_GOLDENGATE_SCL_H
#define _SYS_GOLDENGATE_SCL_H

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
#include "sys_goldengate_fpa.h"


/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define SYS_SCL_DEFAULT_ENTRY_PORT_BASE                   256
#define SYS_SCL_DEFAULT_ENTRY_BASE                   512
#define SYS_SCL_LABEL_MAX                                 64
#define SYS_SCL_LABEL_RESERVE_FOR_IPSG                    63
#define SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS              62
#define SYS_SCL_VEDIT_PRFID_SVLAN_CLASS                   3
#define SYS_SCL_VEDIT_PRFID_SVLAN_SCOS_CLASS              2
#define SYS_SCL_VEDIT_PRFID_SVLAN_DOMAIN                  1

#define SYS_SCL_TRAVERSE_ALL                              0
#define SYS_SCL_TRAVERSE_BY_PORT                          1
#define SYS_SCL_TRAVERSE_BY_GROUP                         2

#define SYS_SCL_MIN_INNER_ENTRY_ID                        0x80000000 /* min sdk entry id*/
#define SYS_SCL_MAX_INNER_ENTRY_ID                        0xFFFFFFFF /* max sdk entry id*/

#define SYS_SCL_GROUP_LIST_NUM                         3

/* please put hash in below range. AND STEP IS +1. */
#define   SYS_SCL_GROUP_ID_INNER_HASH_BEGIN               CTC_SCL_GROUP_ID_HASH_PORT
#define   SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD        0xFFFF000E    /*(CTC_SCL_GROUP_ID_HASH_MAX+1),*/
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS    0xFFFF000F
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS    0xFFFF0010
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC      0xFFFF0011
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4     0xFFFF0012
#define   SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6     0xFFFF0013
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS    0xFFFF0014
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS    0xFFFF0015
#define   SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL      0xFFFF0016
#define   SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL           0xFFFF0017
#define   SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL        0xFFFF0018
#define   SYS_SCL_GROUP_ID_INNER_HASH_END                 SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL

/* please put tcam in below range. AND STEP IS +2. */
#define   SYS_SCL_GROUP_ID_INNER_TCAM_BEGIN               0xFFFF0031
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC      0xFFFF0031
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4     0xFFFF0033
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6     0xFFFF0035
#define   SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS          0xFFFF0037
#define   SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD        0xFFFF0039
#define   SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL           0xFFFF003B
#define   SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL   0xFFFF003D
#define   SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE      0xFFFF003F
#define   SYS_SCL_GROUP_ID_MAX                            0xFFFFFFFF

#define SCL_BLOCK_ID_NUM    2

enum sys_scl_alloc_id
{
    SCL_ALLOC_ID_160,
    SCL_ALLOC_ID_320,
    SCL_ALLOC_ID_640,
    SCL_ALLOC_ID_NUM,
};

typedef struct
{
    uint16 class_id_data;
    uint16 class_id_mask;
    uint16 gport;      /*gport or logic_port*/
    uint16 gport_mask;

    uint8  gport_type; /*[0]:portType [1]:c/s */
    uint8  gport_type_mask;
}drv_scl_group_info_t;

#define ____SYS_SCL_ACTION______

#define  KEY_TYPE
enum sys_scl_key_type_e
{
    SYS_SCL_KEY_TCAM_VLAN,
    SYS_SCL_KEY_TCAM_MAC,
    SYS_SCL_KEY_TCAM_IPV4,
    SYS_SCL_KEY_TCAM_IPV4_SINGLE,
    SYS_SCL_KEY_TCAM_IPV6,
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
    SYS_SCL_KEY_HASH_PORT_IPV6,
    SYS_SCL_KEY_HASH_L2,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF,
    SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA,
    SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1,  /* ipdaindex + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_V4_MODE1,     /* used by both uc and mc, ipda + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1,  /* ipda + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1,  /* ipda + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1,  /* ipdaindex + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_V4_MODE1,     /* used by both uc and mc, ipda + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1,  /* ipda + ipsa + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0,  /* ipda + vn_id*/
    SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1,  /* ipda + ipsa + vn_id*/

    SYS_SCL_KEY_HASH_TRILL_UC,  /* egressNickname + ingressNickname*/
    SYS_SCL_KEY_HASH_TRILL_UC_RPF,  /* ingressNickname*/
    SYS_SCL_KEY_HASH_TRILL_MC,  /* egressNickname + ingressNickname*/
    SYS_SCL_KEY_HASH_TRILL_MC_RPF,  /* egressNickname + ingressNickname*/
    SYS_SCL_KEY_HASH_TRILL_MC_ADJ,  /* egressNickname + globalSrcPort*/

    SYS_SCL_KEY_PORT_DEFAULT_INGRESS,
    SYS_SCL_KEY_PORT_DEFAULT_EGRESS,
    SYS_SCL_KEY_NUM
};
typedef enum sys_scl_key_type_e sys_scl_key_type_t;

#define ACTION_TYPE

enum sys_scl_action_type_e
{
    SYS_SCL_ACTION_INGRESS,
    SYS_SCL_ACTION_EGRESS,
    SYS_SCL_ACTION_TUNNEL,
    SYS_SCL_ACTION_FLOW,
    SYS_SCL_ACTION_NUM
};
typedef enum sys_scl_action_type_e sys_scl_action_type_t;

enum sys_scl_tunnel_type_e
{
    SYS_SCL_TUNNEL_TYPE_NONE,
    SYS_SCL_TUNNEL_TYPE_IPV4_IN4,
    SYS_SCL_TUNNEL_TYPE_IPV6_IN4,
    SYS_SCL_TUNNEL_TYPE_GRE_IN4,
    SYS_SCL_TUNNEL_TYPE_NVGRE_IN4,
    SYS_SCL_TUNNEL_TYPE_VXLAN_IN4,
    SYS_SCL_TUNNEL_TYPE_GENEVE_IN4,
    SYS_SCL_TUNNEL_TYPE_IPV4_IN6,
    SYS_SCL_TUNNEL_TYPE_IPV6_IN6,
    SYS_SCL_TUNNEL_TYPE_GRE_IN6,
    SYS_SCL_TUNNEL_TYPE_NVGRE_IN6,
    SYS_SCL_TUNNEL_TYPE_VXLAN_IN6,
    SYS_SCL_TUNNEL_TYPE_GENEVE_IN6,
    SYS_SCL_TUNNEL_TYPE_RPF,
    SYS_SCL_TUNNEL_TYPE_TRILL,
    SYS_SCL_TUNNEL_TYPE_NUM
};
typedef enum sys_scl_tunnel_type_e sys_scl_tunnel_type_t;

struct sys_scl_hash_tunnel_ipv4_key_s
{
    uint32 ip_sa;
    uint32 ip_da;

    uint8  l4_type;               /**< Layer 4 type. Refer to ctc_parser_l4_type_t */
    uint8  rsv0[3];
};
typedef struct sys_scl_hash_tunnel_ipv4_key_s sys_scl_hash_tunnel_ipv4_key_t;

struct sys_scl_hash_tunnel_ipv4_gre_key_s
{
    uint32 ip_sa;
    uint32 ip_da;
    uint32 gre_key;

    uint8  l4_type;              /**< Layer 4 type. Refer to ctc_parser_l4_type_t */
    uint8  rsv0[3];
};
typedef struct sys_scl_hash_tunnel_ipv4_gre_key_s sys_scl_hash_tunnel_ipv4_gre_key_t;

struct sys_scl_hash_tunnel_ipv4_rpf_key_s
{
    uint32 ip_sa;                       /**< IP-SA */
};
typedef struct sys_scl_hash_tunnel_ipv4_rpf_key_s sys_scl_hash_tunnel_ipv4_rpf_key_t;

struct sys_scl_hash_overlay_tunnel_v4_key_s
{
    uint32 ip_da; /* used to record ipda or ipda index */
    uint32 ip_sa;
    uint32 vn_id;

};
typedef struct sys_scl_hash_overlay_tunnel_v4_key_s sys_scl_hash_overlay_tunnel_v4_key_t;

struct sys_scl_hash_overlay_tunnel_v6_key_s
{
    ipv6_addr_t ip_da;
    ipv6_addr_t ip_sa;
    uint32 vn_id;
} ;
typedef struct sys_scl_hash_overlay_tunnel_v6_key_s sys_scl_hash_overlay_tunnel_v6_key_t;

struct sys_scl_hash_trill_key_s
{
    uint16 egress_nickname;
    uint16 ingress_nickname;
    uint16 gport;
    uint8 rsv[2];
} ;
typedef struct sys_scl_hash_trill_key_s sys_scl_hash_trill_key_t;


enum sys_scl_tunnel_valid_flag_e
{
    SYS_SCL_TUNNEL_GRE_KEY_VALID = 0x01,      /**<If set gre key is valid */
    SYS_SCL_TUNNEL_IPSA_VALID    = 0x02,      /**<If set ipsa is valid */
    SYS_SCL_TUNNEL_MAX
};
typedef enum sys_scl_tunnel_valid_flag_e sys_scl_tunnel_valid_flag_t;

struct sys_scl_tunnel_binding_data_l_s
{
    uint32 binding_data           : 32;

    uint32 logic_port_security_en : 1;
    uint32 rsv0                   : 2;
    uint32 logic_src_port         : 14;
    uint32 metadataType           : 2;
    uint32 flow_policer_ptr       : 13;   /* not used, must use chip.policer_ptr[lchip] */
};
typedef struct sys_scl_tunnel_binding_data_l_s sys_scl_tunnel_binding_data_l_t;

struct sys_scl_tunnel_action_chip_s
{
    uint16 stats_ptr;
    uint16 policer_ptr;
};
typedef struct sys_scl_tunnel_action_chip_s sys_scl_tunnel_action_chip_t;

struct sys_scl_tunnel_action_s
{
    uint32                          trill_ttl_check_en         : 1;
    uint32                          trill_bfd_en               : 1;
    uint32                          pip_bypass_learning        : 1;
    uint32                          rpf_check_en               : 1;
    uint32                          tunnel_rpf_check_request   : 1;
    uint32                          logic_port_type            : 1;
    uint32                          service_acl_qos_en         : 1;
    uint32                                                     : 1; /* 8  */
    uint32                          isid_valid                 : 1;
    uint32                          binding_en                 : 1;
    uint32                          binding_mac_sa             : 1;
    uint32                          is_tunnel                  : 1;
    uint32                          tunnel_id_exception_en     : 1;
    uint32                          trill_bfd_echo_en          : 1;
    uint32                          src_queue_select           : 1;
    uint32                          trill_option_escape_en     : 1; /* 16 */
    uint32                                                     : 1;
    uint32                                                     : 1;
    uint32                          tunnel_packet_type         : 3; /* 21 */
    uint32                          tunnel_payload_offset      : 4; /* 25 */
    uint32                          use_default_vlan_tag       : 1;
    uint32                          ttl_check_en               : 1;
    uint32                          tunnel_gre_options         : 3; /* 30 */
    uint32                          isatap_check_en            : 1;
    uint32                                                     : 1;

    uint32                          pbb_mcast_decap            : 1;
    uint32                          user_default_cos           : 3;
    uint32                          deny_learning              : 1;
    uint32                          deny_bridge                : 1;
    uint32                          fid_type                   : 1;
    uint32                          inner_packet_lookup        : 1; /* 8  */
    uint32                          outer_vlan_is_cvlan        : 1;
    uint32                          acl_qos_use_outer_info     : 1;
    uint32                          ds_fwd_ptr_valid           : 1;
    uint32                          ecmp_en                    : 1;
    uint32                          svlan_tpid_index           : 2; /* 14  */
    uint32                          pbb_outer_learning_disable : 1;
    uint32                          pbb_outer_learning_enable  : 1;
    uint32                                                     : 14;
    uint32                          ttl_update                 : 1;
    uint32                          outer_ttl_check            : 1;

    uint32                          user_default_cfi           : 1;
    uint32                          trill_multi_rpf_check      : 1;
    uint32                          trill_decap_without_loop   : 1;
    uint32                          trill_channel_en           : 1;
    uint32                          exception_sub_index        : 6;
    uint32                          esadi_check_en             : 1;
    uint32                          route_disable              : 1; /* 12  */
    uint32                          router_mac_profile         : 6;
    uint32                          router_mac_profile_en      : 1;
    uint32                          inner_packet_pbr_en        : 1;
    uint32                          inner_packet_nat_en        : 1;
    uint32                          softwire_en                : 1;
    uint32                          classify_use_outer_info    : 1;
    uint32                          inner_vtag_check_mode      : 2;
    uint32                          discard                    : 1;
    uint32                          arp_exception_type         : 2;
    uint32                          arp_ctl_en                 : 1; /* 29 */
    uint32                          rsv0                       : 3;

    union
    {
        uint32          data[4];
        struct
        {
            uint32 aps_select_protecting_path : 1;
            uint32 service_policer_valid      : 1;
            uint32 aps_select_valid           : 1;
            uint32 aps_select_group_id        : 11;
            uint32 logic_port_security_en     : 1;
            uint32 service_policer_mode       : 1;
            uint32 rsv0                       : 16;

            uint32 logic_src_port             : 14;
            uint32                            : 13;
            uint32 metadata_type              : 2;
            uint32 rsv1                       : 3;
        }g2;
    }u1;

    union
    {
        uint16 fid;
        uint16 dsfwdptr;
        uint16 ecmp_group_id;
    }u2;

    union
    {
        uint32 isid;
        uint32 user_default_vlan;
        struct
        {
            uint32 second_fid                : 14;
            uint32 second_fid_en             : 1;
            uint32 rsv0                      : 17;
        }g3;
    }u3;

    union
    {
        struct
        {
            uint8 exist_check_en;
            uint8 is_not_exist;
        }vxlan;
        struct
        {
            uint8 exist_check_en;
            uint8 is_not_exist;
        }nvgre;
    }u4;

    sys_scl_tunnel_action_chip_t    chip;
    mac_addr_t          mac_sa;
};
typedef struct sys_scl_tunnel_action_s sys_scl_tunnel_action_t;

struct sys_scl_port_default_key_s
{
    uint16 gport;
    uint16 rsv0;
};
typedef struct sys_scl_port_default_key_s sys_scl_port_default_key_t;

struct sys_scl_key_s
{
    sys_scl_key_type_t type;                 /**< [HB.GB.GG] CTC_SCL_XXX_KEY */
    sys_scl_tunnel_type_t tunnel_type;  /**< [GG] SYS_SCL_TUNNEL_TYPE_XXX */

    union
    {
        ctc_scl_tcam_vlan_key_t              tcam_vlan_key;          /**< SCL VLAN key content */
        ctc_scl_tcam_mac_key_t               tcam_mac_key;           /**< SCL MAC  key content */
        ctc_scl_tcam_ipv4_key_t              tcam_ipv4_key;          /**< SCL IPv4 key content */
        ctc_scl_tcam_ipv6_key_t              tcam_ipv6_key;          /**< SCL IPv6 key content */

        ctc_scl_hash_port_key_t              hash_port_key;
        ctc_scl_hash_port_cvlan_key_t        hash_port_cvlan_key;
        ctc_scl_hash_port_svlan_key_t        hash_port_svlan_key;
        ctc_scl_hash_port_2vlan_key_t        hash_port_2vlan_key;
        ctc_scl_hash_port_cvlan_cos_key_t    hash_port_cvlan_cos_key;
        ctc_scl_hash_port_svlan_cos_key_t    hash_port_svlan_cos_key;
        ctc_scl_hash_port_mac_key_t          hash_port_mac_key;
        ctc_scl_hash_port_ipv4_key_t         hash_port_ipv4_key;
        ctc_scl_hash_mac_key_t               hash_mac_key;
        ctc_scl_hash_ipv4_key_t              hash_ipv4_key;
        ctc_scl_hash_ipv6_key_t              hash_ipv6_key;
        ctc_scl_hash_port_ipv6_key_t    hash_port_ipv6_key;
        ctc_scl_hash_l2_key_t                hash_l2_key;

        sys_scl_port_default_key_t           port_default_key;
        sys_scl_hash_tunnel_ipv4_key_t       hash_tunnel_ipv4_key;
        sys_scl_hash_tunnel_ipv4_gre_key_t   hash_tunnel_ipv4_gre_key;
        sys_scl_hash_tunnel_ipv4_rpf_key_t   hash_tunnel_ipv4_rpf_key;
        sys_scl_hash_overlay_tunnel_v4_key_t hash_overlay_tunnel_v4_key;
        sys_scl_hash_overlay_tunnel_v6_key_t hash_overlay_tunnel_v6_key;
        sys_scl_hash_trill_key_t hash_trill_key;
    } u;
};
typedef struct sys_scl_key_s sys_scl_key_t;



struct sys_scl_action_s
{
    sys_scl_action_type_t type;
    uint32 action_flag;         /**< [GB.GG]original action flag in vlan mapping struct*/
    union
    {
        ctc_scl_igs_action_t    igs_action;
        ctc_scl_egs_action_t    egs_action;
        ctc_scl_flow_action_t    flow_action;
        sys_scl_tunnel_action_t tunnel_action;
    } u;
};
typedef struct sys_scl_action_s sys_scl_action_t;


/** @brief  scl entry struct */
struct sys_scl_entry_s
{
    sys_scl_key_t    key;           /**< [HB.GB] scl key struct*/
    sys_scl_action_t action;        /**< [HB.GB] scl action struct*/

    uint32           entry_id;      /**< [HB.GB] scl action id. 0xFFFFFFFF indicates get entry id */
    uint8            priority_valid;
    uint32           priority;
};
typedef struct sys_scl_entry_s sys_scl_entry_t;


struct binding_data_s
{
    uint32 aps_select_protecting_path : 1;
    uint32 service_policer_valid      : 1;
    uint32 aps_select_valid           : 1;
    uint32 aps_select_group_id        : 12;
    uint32 logic_port_security_en     : 1;
    uint32 logic_src_port             : 14;
    uint32 logic_port_type            : 1;
    uint32 service_acl_qos_en         : 1;

    uint16 service_policer_mode       : 1;
    uint16 flow_policer_ptr           : 13;
    uint16 rsv0                       : 2;
};
typedef struct binding_data_s binding_data_t;


/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
struct sys_scl_sw_igs_action_chip_s
{
    uint16 profile_id;
    uint32 offset_array;
    uint16 service_policer_ptr;
    uint16 stats_ptr;
};
typedef struct sys_scl_sw_igs_action_chip_s sys_scl_sw_igs_action_chip_t;


/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
struct sys_scl_sw_egs_action_chip_s
{
    uint16 stats_ptr;
};
typedef struct sys_scl_sw_egs_action_chip_s sys_scl_sw_egs_action_chip_t;

/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
struct sys_scl_sw_flow_action_chip_s
{
    uint16 stats_ptr;
    uint32 fwdptr;
    uint16 micro_policer_ptr;
    uint16 macro_policer_ptr;
};
typedef struct sys_scl_sw_flow_action_chip_s sys_scl_sw_flow_action_chip_t;

struct sys_scl_sw_vlan_edit_chip_s
{
    /*chip related property only this. so compare no need use 2*/
    uint16 profile_id;
};
typedef struct sys_scl_sw_vlan_edit_chip_s sys_scl_sw_vlan_edit_chip_t;



struct sys_scl_sw_vlan_edit_s
{
    uint32 stag_op     : 3,
           svid_sl     : 2,
           scos_sl     : 2,
           scfi_sl     : 2,
           ctag_op     : 3,
           cvid_sl     : 2,
           ccos_sl     : 2,
           ccfi_sl     : 2,
           vlan_domain : 2,
           tpid_index  : 2,
           rsv         : 10;
    sys_scl_sw_vlan_edit_chip_t chip;
    uint8 lchip;
};
typedef struct sys_scl_sw_vlan_edit_s sys_scl_sw_vlan_edit_t;


struct sys_scl_sw_igs_action_s
{
    uint32 flag;     /* the same as ctc flag */
    uint32 sub_flag; /* the same as ctc sub_flag */

    uint32 fid_type                  : 1;
    uint32 oam_tunnel_en             : 1;
    uint32 priority                  : 6; /*8*/
    uint32 ds_fwd_ptr_valid          : 1;
    uint32 binding_en                : 1;
    uint32 binding_mac_sa            : 1;
    uint32 is_leaf                   : 1;
    uint32 rsv      : 1;
    uint32 bypass_all                : 1;
    uint32 src_queue_select          : 1;
    uint32 deny_bridge               : 1; /*16*/
    uint32 user_vlan_ptr             : 13;
    uint32                           : 2;
    uint32 vlan_action_profile_valid : 1; /*32*/

    uint32                           : 1;
    uint32 aging_valid               : 1;
    uint32 cvlan_tag_operation_valid : 1;
    uint32 svlan_tag_operation_valid : 1;
    uint32 bind_type                 : 4;
    uint32                           : 1;
    uint32 stp_id                       : 7;  /*16*/
    uint32 service_policer_id        : 16; /*32*/

    uint32 fid                       : 16; /*16*/
    uint32 color                     : 2;
    uint32 dscp                      : 6;
    uint32                              : 8; /*32*/

    uint32 nh_id;
    uint32 stats_id;

    uint32 svid : 12;
    uint32 scos : 3;
    uint32 scfi : 1;                 /*16*/
    uint32 cvid : 12;
    uint32 ccos : 3;
    uint32 ccfi : 1;                 /*32*/

    uint32 l2vpn_oam_id : 16;
    uint32                             : 16;

    union
    {
        mac_addr_t mac_sa;
        uint32     ipv4_sa;
        uint16     gport;
        uint16     vlan_id;
        struct
        {
            uint32 ipv4_sa;
            uint16 vlan_id;
        }              ip_vlan;
        uint16         data;
        binding_data_t bds;                     /* binding data struture */
    }                            bind;

    sys_scl_sw_igs_action_chip_t chip;

    sys_scl_sw_vlan_edit_t       * vlan_edit; /* keep at tail, so won't involve hash cmp, hash make */
};
typedef struct sys_scl_sw_igs_action_s sys_scl_sw_igs_action_t;

struct sys_scl_sw_flow_action_s
{
    uint32 flag;     /* the same as ctc flag */

    uint32 postcard_en : 1;
    uint32 acl_flow_tcam_lookup_type : 2;
    uint32 acl_flow_tcam_label : 8;
    uint32 overwrite_acl_flow_tcam : 1;
    uint32 acl_flow_tcam_en : 1;
    uint32 overwrite_acl_flow_hash : 1;
    uint32 acl_flow_hash_type : 2;  /*16*/
    uint32 acl_flow_hash_field_sel : 4;
    uint32 deny_bridge             : 1;
    uint32 deny_learning             : 1;
    uint32 deny_route             : 1;
    uint32 force_bridge             : 1;
    uint32 force_learning             : 1;
    uint32 force_route             : 1;
    uint32 discard            : 1;
    uint32 color                     : 2;
    uint32 qos_policy            : 3;   /*32*/

    uint32 metadata            : 14;
    uint32 metadata_type            : 2;    /*16*/
    uint32 cvlan_tag_operation_valid : 1;
    uint32 svlan_tag_operation_valid : 1;
    uint32 priority              : 6;
    uint32 ipfix_hash_type              : 2;
    uint32 ipfix_hash_field_sel         : 4;
    uint32 ipfix_en                     : 1;
    uint32 overwrite_ipfix_hash         : 1;   /*32*/

    uint32 nh_id;
    uint32 stats_id;

    uint32 svid : 12;
    uint32 scos : 3;
    uint32 scfi : 1;                 /*16*/
    uint32 cvid : 12;
    uint32 ccos : 3;
    uint32 ccfi : 1;                 /*32*/

    uint32 inner_lookup             : 1;
    uint32 payload_offset_start_point : 2;
    uint32 packet_type : 3;
    uint32 payload_offset : 4;
    uint32 rsv0 : 22;             /*32*/

    uint16 micro_policer_id;
    uint16 macro_policer_id;
    uint16 ecmp_group_id;
    uint16 fid;
    uint16 vrfid;
    uint8   rsv[2];
    sys_scl_sw_flow_action_chip_t chip;
};
typedef struct sys_scl_sw_flow_action_s sys_scl_sw_flow_action_t;


struct sys_scl_sw_egs_action_s
{
    uint32                       flag; /* the same as ctc flag */

    uint32                       stag_op : 3;
    uint32                       svid_sl : 2;
    uint32                       scos_sl : 2;
    uint32                       scfi_sl : 2;
    uint32                       ctag_op : 3;
    uint32                       cvid_sl : 2;
    uint32                       ccos_sl : 2;
    uint32                       ccfi_sl : 2;
    uint32                       tpid_index : 2;
    uint32                       rsv0    : 12;

    uint32                       svid    : 12;
    uint32                       scos    : 3;
    uint32                       scfi    : 1;
    uint32                       cvid    : 12;
    uint32                       ccos    : 3;
    uint32                       ccfi    : 1;

    uint32                       stats_id;

    sys_scl_sw_egs_action_chip_t chip;
};
typedef struct sys_scl_sw_egs_action_s sys_scl_sw_egs_action_t;


union sys_scl_sw_action_union_u
{
    sys_scl_sw_igs_action_t  ia;
    sys_scl_sw_egs_action_t  ea;
    sys_scl_tunnel_action_t  ta;   /* sw struct is same as sys struct */
    sys_scl_sw_flow_action_t fa;   /* sw struct is same as sys struct */
};
typedef union sys_scl_sw_action_union_u sys_scl_sw_action_union_t;


struct sys_scl_sw_action_s
{
    uint32                    ad_index; /* not in hash compare*/
    uint32                    action_flag; /*original action flag in vlan mapping struct*/
    uint8                     type;
    uint8                     lchip;
    uint8                     rsv[2];

    sys_scl_sw_action_union_t u0;
};
typedef struct sys_scl_sw_action_s sys_scl_sw_action_t;

#define __SYS_SCL_TCAM_KEY__


struct sys_scl_sw_tcam_key_mac_s
{
    uint32     flag;

    uint32     l2_type       : 2;
    uint32     l2_type_mask  : 2;
    uint32     rsv0          : 28;

    uint32     svlan         : 12;
    uint32     stag_cos      : 3;
    uint32     stag_cfi      : 1;
    uint32     cvlan         : 12;
    uint32     ctag_cos      : 3;
    uint32     ctag_cfi      : 1;

    uint32     svlan_mask    : 12;
    uint32     stag_cos_mask : 3;
    uint32     cvlan_mask    : 12;
    uint32     ctag_cos_mask : 3;
    uint32     stag_valid    : 1;
    uint32     ctag_valid    : 1;

    uint16     eth_type;
    uint16     eth_type_mask;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    drv_scl_group_info_t port_info;
};
typedef  struct sys_scl_sw_tcam_key_mac_s sys_scl_sw_tcam_key_mac_t;

struct sys_scl_sw_tcam_key_vlan_s
{
    uint32 flag;

    uint32 svlan         : 12;
    uint32 stag_cos      : 3;
    uint32 stag_cfi      : 1;
    uint32 cvlan         : 12;
    uint32 ctag_cos      : 3;
    uint32 ctag_cfi      : 1;

    uint32 svlan_mask    : 12;
    uint32 stag_cos_mask : 3;
    uint32 cvlan_mask    : 12;
    uint32 ctag_cos_mask : 3;
    uint32 stag_valid    : 1;
    uint32 ctag_valid    : 1;
    uint32 customer_id;
    uint32 customer_id_mask;

    drv_scl_group_info_t port_info;
};
typedef  struct sys_scl_sw_tcam_key_vlan_s sys_scl_sw_tcam_key_vlan_t;

struct sys_scl_sw_tcam_key_ipv4_s
{
    uint32     flag;
    uint32     sub_flag;
    uint32     sub1_flag;
    uint32     sub2_flag;
    uint8      key_size;

    uint32     svlan         : 12;
    uint32     stag_cos      : 3;
    uint32     stag_cfi      : 1;
    uint32     cvlan         : 12;
    uint32     ctag_cos      : 3;
    uint32     ctag_cfi      : 1;

    uint32     svlan_mask    : 12;
    uint32     stag_cos_mask : 3;
    uint32     cvlan_mask    : 12;
    uint32     ctag_cos_mask : 3;
    uint32     stag_valid    : 1;
    uint32     ctag_valid    : 1;

    uint32     l2_type       : 2;
    uint32     l2_type_mask  : 2;
    uint32     unique_l3_type: 4;
    uint32     l3_type       : 4;
    uint32     l3_type_mask  : 4;
    uint32     rsv2          : 16;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    drv_scl_group_info_t port_info;

    union
    {
        struct
        {
            uint32 rsv1               : 1;
            uint32 flag_l4_type       : 1;
            uint32 flag_l4info_mapped : 1;     /* sw only */
            uint32 ip_option          : 1;
            uint32 frag_info          : 2;
            uint32 frag_info_mask     : 2;
            uint32 dscp               : 6;
            uint32 dscp_mask          : 6;
            uint32 ecn                : 2;
            uint32 ecn_mask           : 2;
            uint32 l4_type            : 4;
            uint32 l4_type_mask       : 4;

            uint32 ip_sa;
            uint32 ip_sa_mask;

            uint32 ip_da;
            uint32 ip_da_mask;

            uint32 l4info_mapped         : 12;
            uint32 l4info_mapped_mask    : 12;
            uint32 l4_compress_type      : 2;
            uint32 l4_compress_type_mask : 2;
            uint32 rsv2                  : 4;

            uint32 l4_src_port           : 16;
            uint32 l4_src_port_mask      : 16;
            uint32 l4_dst_port           : 16;
            uint32 l4_dst_port_mask      : 16;
        }ip;

        struct
        {
            uint16 op_code;
            uint16 op_code_mask;

            uint16 protocol_type;
            uint16 protocol_type_mask;

            uint32 target_ip;
            uint32 target_ip_mask;

            uint32 sender_ip;
            uint32 sender_ip_mask;
        }arp;

        struct
        {
            uint16 eth_type;
            uint16 eth_type_mask;
        }other;
    }u0;
};
typedef  struct sys_scl_sw_tcam_key_ipv4_s sys_scl_sw_tcam_key_ipv4_t;

struct sys_scl_sw_tcam_key_ipv6_s
{
    uint32     flag;
    uint32     sub_flag;

    uint32     svlan           : 12;
    uint32     stag_cos        : 3;
    uint32     stag_cfi        : 1;
    uint32     cvlan           : 12;
    uint32     ctag_cos        : 3;
    uint32     ctag_cfi        : 1;

    uint32     svlan_mask      : 12;
    uint32     stag_cos_mask   : 3;
    uint32     cvlan_mask      : 12;
    uint32     ctag_cos_mask   : 3;
    uint32     stag_valid      : 1;
    uint32     ctag_valid      : 1;

    uint32     flow_label      : 20;
    uint32     l2_type         : 4;
    uint32     l2_type_mask    : 4;
    uint32     rsv1            : 4;

    uint32     flow_label_mask : 20;
    uint32     l3_type         : 4;
    uint32     l3_type_mask    : 4;
    uint32     rsv2            : 4;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    drv_scl_group_info_t port_info;

    union
    {
        struct
        {
            uint32      dscp               : 6;
            uint32      dscp_mask          : 6;
            uint32      l4_type            : 4;
            uint32      l4_type_mask       : 4;
            uint32      frag_info          : 2;
            uint32      frag_info_mask     : 2;
            uint32      ip_option          : 1;
            uint32      flag_l4info_mapped : 1; /* sw only */
            uint32      rsv3               : 2;
            uint32      ecn                : 2;
            uint32      ecn_mask           : 2;

            uint32      l4_src_port        : 16;
            uint32      l4_src_port_mask   : 16;

            uint32      l4_dst_port        : 16;
            uint32      l4_dst_port_mask   : 16;

            uint16      l4info_mapped;       /* sw only */
            uint16      l4info_mapped_mask;  /* sw only */

            ipv6_addr_t ip_sa;
            ipv6_addr_t ip_sa_mask;

            ipv6_addr_t ip_da;
            ipv6_addr_t ip_da_mask;
        }ip;

        struct
        {
            uint16 eth_type;
            uint16 eth_type_mask;
        }other;
    }u0;
} ;
typedef  struct sys_scl_sw_tcam_key_ipv6_s sys_scl_sw_tcam_key_ipv6_t;



#define __SYS_SCL_HASH_KEY__
/*for port | default */
struct sys_scl_sw_hash_key_port_s
{
    uint16 gport;
    uint8  gport_is_classid : 1;
    uint8  gport_is_logic   : 1;
    uint8  rsv0             : 6;
    uint8  dir;
};
typedef struct sys_scl_sw_hash_key_port_s sys_scl_sw_hash_key_port_t;

struct sys_scl_sw_hash_key_vlan_s
{
    uint32 gport            : 16;
    uint32 vid              : 12;
    uint32 dir              : 1;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;
    uint32 rsv0             : 1;
};
typedef struct sys_scl_sw_hash_key_vlan_s sys_scl_sw_hash_key_vlan_t;

struct sys_scl_sw_hash_key_vtag_s
{
    uint32 gport            : 16;
    uint32 svid             : 12;
    uint32 ccos             : 3;
    uint32 dir              : 1;

    uint32 scos             : 3;
    uint32 cvid             : 12;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;  /* new, add later*/
    uint32 rsv0             : 15;
} ;
typedef struct sys_scl_sw_hash_key_vtag_s sys_scl_sw_hash_key_vtag_t;

struct sys_scl_sw_hash_key_mac_s
{
    uint32     gport            : 16;
    uint32     use_macda        : 1;
    uint32     gport_is_classid : 1;
    uint32     gport_is_logic   : 1;
    uint32     rsv0             : 13;
    mac_addr_t mac;
};
typedef struct sys_scl_sw_hash_key_mac_s sys_scl_sw_hash_key_mac_t;

struct sys_scl_sw_hash_key_ipv4_s
{
    uint32 ip_sa;

    uint32 gport            : 16;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;
    uint32 rsv0             : 14;
};
typedef struct sys_scl_sw_hash_key_ipv4_s sys_scl_sw_hash_key_ipv4_t;

struct sys_scl_sw_hash_key_cross_s
{
    uint32 global_src_port  : 16;
    uint32 global_dest_port : 16;

    uint32 vid              : 12;
    uint32 rsv1             : 20;
};
typedef struct sys_scl_sw_hash_key_cross_s sys_scl_sw_hash_key_cross_t;

struct sys_scl_sw_hash_key_ipv6_s
{
    ipv6_addr_t ip_sa;
    uint32 gport            : 16;
    uint32 gport_is_classid : 1;
    uint32 gport_is_logic   : 1;
    uint32 rsv0             : 14;
};
typedef struct sys_scl_sw_hash_key_ipv6_s sys_scl_sw_hash_key_ipv6_t;

struct sys_scl_sw_hash_key_l2_s
{
    uint32 svid       : 12;
    uint32 scos       : 3;
    uint32 scfi       : 1;
    uint32 stag_exist : 1;
    uint32 field_sel  : 2;
    uint32 class_id   : 8;
    uint32 rsv0         : 5;

    uint16 ether_type;
    uint8 rsv1[2];
    mac_addr_t macsa;
    mac_addr_t macda;
};
typedef struct sys_scl_sw_hash_key_l2_s sys_scl_sw_hash_key_l2_t;


#define __SYS_SCL_KEY__


union sys_scl_sw_hash_key_union_u
{
    sys_scl_sw_hash_key_port_t           port;
    sys_scl_sw_hash_key_vlan_t           vlan;
    sys_scl_sw_hash_key_vtag_t           vtag;
    sys_scl_sw_hash_key_mac_t            mac;
    sys_scl_sw_hash_key_ipv4_t           ipv4;
    sys_scl_sw_hash_key_cross_t          cross;
    sys_scl_sw_hash_key_ipv6_t           ipv6;
    sys_scl_sw_hash_key_l2_t             l2;
    sys_scl_hash_tunnel_ipv4_key_t       tnnl_ipv4;      /* sw key is same as sys key*/
    sys_scl_hash_tunnel_ipv4_gre_key_t   tnnl_ipv4_gre;  /* sw key is same as sys key*/
    sys_scl_hash_tunnel_ipv4_rpf_key_t   tnnl_ipv4_rpf;  /* sw key is same as sys key*/
    sys_scl_hash_overlay_tunnel_v4_key_t ol_tnnl_v4;     /* sw key is same as sys key*/
    sys_scl_hash_overlay_tunnel_v6_key_t ol_tnnl_v6;     /* sw key is same as sys key*/
    sys_scl_hash_trill_key_t trill;                      /* sw key is same as sys key*/
};
typedef union sys_scl_sw_hash_key_union_u sys_scl_sw_hash_key_union_t;


struct sys_scl_sw_hash_key_s
{
    sys_scl_sw_hash_key_union_t u0;
};
typedef struct sys_scl_sw_hash_key_s sys_scl_sw_hash_key_t;

union sys_scl_sw_key_union_u
{
    sys_scl_sw_tcam_key_mac_t  mac_key;
    sys_scl_sw_tcam_key_vlan_t vlan_key;
    sys_scl_sw_tcam_key_ipv4_t ipv4_key;
    sys_scl_sw_tcam_key_ipv6_t ipv6_key;
    sys_scl_sw_hash_key_t      hash;
};
typedef union sys_scl_sw_key_union_u sys_scl_sw_key_union_t;

struct sys_scl_sw_key_s
{
    uint8                  type; /* sys_scl_key_type_t */
    uint8                  tunnel_type; /* sys_scl_tunnel_type_t */
    sys_scl_sw_key_union_t u0;
};
typedef struct sys_scl_sw_key_s sys_scl_sw_key_t;


#define __SYS_SCL_SW_NODE__


typedef struct
{
    uint8 type;
    uint8 block_id; /* block the group belong. For hash, 0xFF. */
    union
    {
        uint16 port_class_id;
        uint16 gport;       /* logic_port or gport*/
    }u0;
}sys_scl_sw_group_info_t;


typedef struct
{
    uint32                  group_id; /* keep group_id top, never move it.*/

    uint8                   flag;     /* group flag*/
    uint8                   rsv0[3];

    uint32                  entry_count;


    ctc_slist_t             * entry_list;   /* a list that link entries belong to this group */
    sys_scl_sw_group_info_t group_info;     /* group info */
} sys_scl_sw_group_t;


typedef struct
{
    ctc_fpa_block_t fpab;
}sys_scl_sw_block_t;

typedef struct
{
    ctc_slistnode_t    head;                /* keep head top!! */
    ctc_fpa_entry_t    fpae;                /* keep fpae at second place!! */


    sys_scl_sw_group_t * group;                /* pointer to group node*/
    sys_scl_sw_action_t* action;               /* pointer to action node*/
    uint8    lchip;
    uint8    rsv[3];

    sys_scl_sw_key_t   key;                    /* keep key at tail !! */


}sys_scl_sw_entry_t;


extern int32
sys_goldengate_scl_init(uint8 lchip, void* scl_global_cfg);

/**
 @brief De-Initialize scl module
*/
extern int32
sys_goldengate_scl_deinit(uint8 lchip);

extern int32
sys_goldengate_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_goldengate_scl_destroy_group(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_goldengate_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_goldengate_scl_uninstall_group(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_goldengate_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner);

extern int32
sys_goldengate_scl_add_entry(uint8 lchip, uint32 group_id, sys_scl_entry_t* scl_entry, uint8 inner);

extern int32
sys_goldengate_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_goldengate_scl_install_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_goldengate_scl_uninstall_entry(uint8 lchip, uint32 entry_id, uint8 inner);

extern int32
sys_goldengate_scl_remove_all_entry(uint8 lchip, uint32 group_id, uint8 inner);

extern int32
sys_goldengate_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner);

extern int32
sys_goldengate_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query, uint8 inner);

extern int32
sys_goldengate_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry, uint8 inner);

extern int32
sys_goldengate_scl_update_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner);

extern int32
sys_goldengate_scl_set_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action);

extern int32
sys_goldengate_scl_get_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action);

extern int32
sys_goldengate_scl_set_hash_field_sel(uint8 lchip, ctc_scl_hash_field_sel_t* field_sel);

extern int32
sys_goldengate_scl_show_status(uint8 lchip);

extern int32
sys_goldengate_scl_show_tunnel_status(uint8 lchip);

/*internal use*/
extern int32
sys_goldengate_scl_remove_all_inner_hash_vlan(uint8 lchip, uint16 gport, uint8 dir);

extern int32
sys_goldengate_scl_get_entry_id(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid);

extern int32
sys_goldengate_scl_get_entry(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid);

extern int32
sys_goldengate_scl_get_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner);

extern int32
sys_goldengate_scl_show_entry(uint8 lchip, uint8 type, uint32 param, sys_scl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_scl_show_vlan_mapping_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_scl_show_vlan_class_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_scl_show_tunnel_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_scl_set_default_tunnel_action(uint8 lchip, uint8 key_type, sys_scl_action_t* action);
#endif



#ifdef __cplusplus
}
#endif
#endif


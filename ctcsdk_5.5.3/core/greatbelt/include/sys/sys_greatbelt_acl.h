/**
   @file sys_greatbelt_acl.h

   @date 2012-10-17

   @version v2.0

 */
#ifndef _SYS_GREATBELT_ACL_H
#define _SYS_GREATBELT_ACL_H

#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
*
* Header Files
*
***************************************************************/
#include "ctc_const.h"

#include "ctc_acl.h"
#include "sys_greatbelt_fpa.h"

/***************************************************************
*
*  Defines and Macros
*
***************************************************************/

#define ACL_PORT_CLASS_ID_NUM    1024
#define ACL_VLAN_CLASS_ID_NUM    1024
#define ACL_SERVICE_ID_NUM       0xFFFF

typedef struct
{
    uint32 discard                    : 1,
           deny_bridge                : 1,
           deny_learning              : 1,
           deny_route                 : 1,
           stats                      : 1,
           priority_and_color         : 1,
           trust                      : 1,
           micro_flow_policer         : 1,
           random_log                 : 1,
           copy_to_cpu                : 1,
           redirect                   : 1,
           dscp                       : 1,
           copy_to_cpu_with_timestamp : 1,
           qos_domain                 : 1,
           macro_flow_policer         : 1,
           aging                      : 1,
           vlan_edit                  : 1,
           pbr_fwd                    : 1,
           pbr_ttl_check              : 1,
           pbr_icmp_check             : 1,
           pbr_ecmp                   : 1,                  /*indicates if user use ecmp nexthop directly*/
           pbr_cpu                    : 1,
           pbr_deny                   : 1,
           rsv                        : 9;
}sys_acl_action_flag_t;

typedef struct
{
    uint32 svid    : 12,
           scos    : 3,
           scfi    : 1,
           cvid    : 12,
           ccos    : 3,
           ccfi    : 1;

    uint32 stag_op : 2,
           svid_sl : 2,
           scos_sl : 2,
           scfi_sl : 2,
           ctag_op : 2,
           cvid_sl : 2,
           ccos_sl : 2,
           ccfi_sl : 2,
           tpid_index :2,
           rsv     : 14;
}sys_acl_vlan_edit_t;

typedef struct
{
/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
    uint32 offset;
    uint16 micro_policer_ptr;
    uint16 macro_policer_ptr;

    uint16 stats_ptr;
/* Below are fields not in hash compare */
    uint32 ad_index;
}sys_acl_action_with_chip_t;

typedef struct
{
    sys_acl_action_flag_t flag;

    uint32                nh_id; /* saved to check if need update. 32 bit*/


    uint16                micro_policer_id;
    uint16                macro_policer_id;

    uint32                priority               : 6,
                          dscp                   : 6,
                          random_threshold_shift : 4,
                          qos_domain             : 3,
                          trust                  : 3,
                          color                  : 2,
                          acl_log_id             : 2,
                          rsv1                   : 6;

    sys_acl_vlan_edit_t        vlan_edit;

    uint8                      ecpn;  /* for acl ecmp */
    uint16                     l3if_id;
    uint8                      lchip;

    sys_acl_action_with_chip_t chip;  /* keep it at tail. */

}sys_acl_action_t;

/**
   @brief acl/qos mac key flag
 */
typedef struct
{
    uint32 mac_sa         : 1,
           mac_da         : 1,
           vlan_ptr       : 1,
           cvlan          : 1,
           ctag_cos       : 1,
           ctag_cfi       : 1,
           svlan          : 1,
           stag_cos       : 1, /* 1-8 */
           stag_cfi       : 1,
           eth_type       : 1,
           l2_type        : 1,
           l3_type        : 1,
           arp_op_code    : 1,      /* gb add below  */
           ip_sa          : 1,
           ip_da          : 1,
           svlan_id_valid : 1,  /* 1-16*/
           cvlan_id_valid : 1,
           rsv            : 15; /* 1-32*/
}sys_acl_mac_key_flag_t;


typedef struct
{
    uint32 ip_da             : 1,
           ip_sa             : 1,
           l4info_mapped     : 1,
           is_tcp            : 1,
           is_udp            : 1,
           l4_src_port       : 1,
           l4_dst_port       : 1,
           dscp              : 1,
           frag_info         : 1,
           ip_option         : 1,
           ip_hdr_error      : 1,
           routed_packet     : 1,
           mac_da            : 1,
           mac_sa            : 1,
           cvlan             : 1,
           ctag_cos          : 1, /* 1-16*/
           ctag_cfi          : 1,
           svlan             : 1,
           stag_cos          : 1,
           stag_cfi          : 1,
           l2_type           : 1,
           svlan_id_valid    : 1,        /* gb add below */
           cvlan_id_valid    : 1,
           ipv4_packet       : 1,        /* 17-24 */
           arp_packet        : 1,
           eth_type          : 1,
           l4_src_port_range : 1,
           l4_dst_port_range : 1,
           tcp_flags         : 1,
           rsv               : 3;
}sys_acl_ipv4_key_flag_t;

typedef struct
{
    uint32 mac_da        : 1,
           mac_sa        : 1,
           cvlan         : 1,
           ctag_cos      : 1,
           ctag_cfi      : 1,
           svlan         : 1,
           stag_cos      : 1,
           stag_cfi      : 1,
           mpls_label0   : 1,
           mpls_label1   : 1,
           mpls_label2   : 1,
           mpls_label3   : 1,
           routed_packet : 1,
           rsv           : 19;
/*  gb add nothing */
}sys_acl_mpls_key_flag_t;

typedef struct
{
    uint32 ip_da             : 1,
           ip_sa             : 1,
           l4info_mapped     : 1,
           is_tcp            : 1,
           is_udp            : 1,
           l4_src_port       : 1,
           l4_dst_port       : 1,
           dscp              : 1,
           frag_info         : 1,
           ip_option         : 1,
           ip_hdr_error      : 1, /*no use*/
           routed_packet     : 1,
           flow_label        : 1,
           mac_da            : 1,
           mac_sa            : 1,
           vlan_ptr          : 1, /*no use,   1-16*/
           cvlan             : 1,
           ctag_cos          : 1,
           svlan             : 1,
           stag_cos          : 1,
           eth_type          : 1,
           l2_type           : 1,
           l3_type           : 1,
           ctag_cfi          : 1, /*1-24*/
           stag_cfi          : 1,
           svlan_id_valid    : 1, /*gb add*/
           cvlan_id_valid    : 1, /*gb add*/
           tcp_flags         : 1,
           l4_src_port_range : 1,
           l4_dst_port_range : 1,
           rsv0              : 2;
}sys_acl_ipv6_key_flag_t;

typedef struct
{
    uint32 ip_da             : 1,
           ip_sa             : 1,
           l4info_mapped     : 1,
           is_tcp            : 1,
           is_udp            : 1,
           l4_src_port       : 1,
           l4_dst_port       : 1,
           dscp              : 1,
           frag_info         : 1,
           l4_src_port_range : 1,
           l4_dst_port_range : 1,
           tcp_flags         : 1,
           vrfid             : 1,
           rsv               : 19;
}sys_acl_pbr_ipv4_key_flag_t;

typedef struct
{
    uint32 ip_da             : 1,
           ip_sa             : 1,
           l4info_mapped     : 1,
           is_tcp            : 1,
           is_udp            : 1,
           l4_src_port       : 1,
           l4_dst_port       : 1,
           dscp              : 1,
           frag_info         : 1,
           ip_hdr_error      : 1, /*no use*/
           routed_packet     : 1, /*no use*/
           flow_label        : 1,
           mac_da            : 1,
           mac_sa            : 1,
           vlan_ptr          : 1, /*no use,   1-16*/
           cvlan             : 1,
           ctag_cos          : 1,
           svlan             : 1,
           stag_cos          : 1,
           eth_type          : 1,
           l2_type           : 1,
           l3_type           : 1,
           ctag_cfi          : 1, /*1-24*/
           stag_cfi          : 1,
           tcp_flags         : 1,
           l4_src_port_range : 1,
           l4_dst_port_range : 1,
           vrfid             : 1,
           rsv0              : 4;
}sys_acl_pbr_ipv6_key_flag_t;

typedef struct
{
    uint8  is_label;
    uint8  is_label_mask;

    uint8  use_group;
    uint8  use_group_mask;
    uint8  rsv;
    uint8  valid;

    uint32 label[2];
    uint32 mask[2];
}sys_acl_port_t;

typedef struct
{
    sys_acl_mac_key_flag_t flag;

    mac_addr_t             mac_da;
    mac_addr_t             mac_da_mask;
    mac_addr_t             mac_sa;
    mac_addr_t             mac_sa_mask;

    uint32                 vlan_ptr       : 13, /*no use*/
                           svlan_id_valid : 1,
                           cvlan_id_valid : 1,
                           ctag_cos_mask  : 3,
                           stag_cos_mask  : 3,
                           l2_type_mask   : 4,
                           l3_type_mask   : 4,
                           rsv0           : 3;

    uint32 cvlan                          : 12,
           svlan                          : 12,
           stag_cos                       : 3,
           stag_cfi                       : 1,
           ctag_cos                       : 3,
           ctag_cfi                       : 1;

    uint32 cvlan_mask                     : 12,
           svlan_mask                     : 12,
           l2_type                        : 4,
           l3_type                        : 4;

    uint16 eth_type;
    uint16 eth_type_mask;

/* gb add these */
    uint16 arp_op_code;
    uint16 arp_op_code_mask;
    uint32 ip_sa;
    uint32 ip_sa_mask;
    uint32 ip_da;
    uint32 ip_da_mask;
    sys_acl_port_t port;
}sys_acl_mac_key_t;

typedef struct
{
    sys_acl_ipv4_key_flag_t flag;

    uint32                  ip_sa;
    uint32                  ip_sa_mask;

    uint32                  ip_da;
    uint32                  ip_da_mask;

    uint16                  l4_src_port;
    uint16                  l4_src_port_mask;

    uint16                  l4_dst_port;
    uint16                  l4_dst_port_mask;

    uint16                  l4info_mapped;
    uint16                  l4info_mapped_mask;

    mac_addr_t              mac_sa;
    mac_addr_t              mac_sa_mask;

    mac_addr_t              mac_da;
    mac_addr_t              mac_da_mask;

    uint32                  frag_info      : 2,
                            frag_info_mask : 2,
                            dscp           : 6,
                            dscp_mask      : 6, /* 16 */
                            is_tcp         : 1,
                            is_udp         : 1,
                            routed_packet  : 1,
                            ip_hdr_error   : 1,
                            ip_option      : 1, /* gb add */
                            stag_cos_mask  : 3,
                            ctag_cos_mask  : 3,
                            l2_type_mask   : 4,
                            rsv1           : 1;

    uint32 svlan                           : 12,
           cvlan                           : 12,
           stag_cos                        : 3,
           stag_cfi                        : 1,
           ctag_cos                        : 3,
           ctag_cfi                        : 1;

    uint32 cvlan_mask                      : 12,
           svlan_mask                      : 12,
           l2_type                         : 4,
           svlan_id_valid                  : 1, /* gb add */
           cvlan_id_valid                  : 1, /* gb add */
           ipv4_packet                     : 1, /* gb add */
           arp_packet                      : 1; /* gb add */

    uint32 l4_src_port_range_idx           : 3,
           l4_dst_port_range_idx           : 3,
           tcp_flags_idx                   : 2,
           rsv2                            : 24;

/* gb add these */
    uint16 eth_type;
    uint16 eth_type_mask;
    sys_acl_port_t port;
}sys_acl_ipv4_key_t;

typedef struct
{
    sys_acl_mpls_key_flag_t flag;

    mac_addr_t              mac_da;
    mac_addr_t              mac_da_mask;

    mac_addr_t              mac_sa;
    mac_addr_t              mac_sa_mask;

    uint32                  svlan    : 12,
                            cvlan    : 12,
                            stag_cos : 3,
                            stag_cfi : 1,
                            ctag_cos : 3,
                            ctag_cfi : 1;

    uint32 cvlan_mask                : 12,
           svlan_mask                : 12,
           routed_packet             : 1,
           stag_cos_mask             : 3,
           ctag_cos_mask             : 3,
           rsv1                      : 1;

    uint32 mpls_label0;
    uint32 mpls_label0_mask;
    uint32 mpls_label1;
    uint32 mpls_label1_mask;
    uint32 mpls_label2;
    uint32 mpls_label2_mask;
    uint32 mpls_label3;
    uint32 mpls_label3_mask;
    sys_acl_port_t port;
}sys_acl_mpls_key_t;

typedef struct
{
    sys_acl_ipv6_key_flag_t flag;

    ipv6_addr_t             ip_sa;
    ipv6_addr_t             ip_sa_mask;

    ipv6_addr_t             ip_da;
    ipv6_addr_t             ip_da_mask;

    uint16                  l4_src_port;
    uint16                  l4_src_port_mask;

    uint16                  l4_dst_port;
    uint16                  l4_dst_port_mask;

    uint16                  l4info_mapped;
    uint16                  l4info_mapped_mask;

    mac_addr_t              mac_sa;
    mac_addr_t              mac_sa_mask;
    mac_addr_t              mac_da;
    mac_addr_t              mac_da_mask;

    uint32                  flow_label : 20,
                            frag_info  : 2,
                            l2_type    : 4,
                            l3_type    : 4,
                            is_tcp     : 1,
                            is_udp     : 1;

    uint32 flow_label_mask             : 20,
           frag_info_mask              : 2,
           ip_option                   : 1,
           l2_type_mask                : 4,
           l3_type_mask                : 4,
           rsv0                        : 1;

    uint32 dscp                        : 6,
           dscp_mask                   : 6,
           routed_packet               : 1,
           ip_hdr_error                : 1,
           stag_cos_mask               : 3,
           ctag_cos_mask               : 3,
           l4_src_port_range_idx       : 3,
           l4_dst_port_range_idx       : 3,
           tcp_flags_idx               : 2,
           rsv1                        : 10;

    uint16 eth_type;
    uint16 eth_type_mask;

    uint32 svlan          : 12,
           cvlan          : 12,
           stag_cos       : 3,
           stag_cfi       : 1,
           ctag_cos       : 3,
           ctag_cfi       : 1;

    uint32 cvlan_mask     : 12,
           svlan_mask     : 12,
           svlan_id_valid : 1,
           cvlan_id_valid : 1,
           rsv2           : 6;
    sys_acl_port_t port;
}sys_acl_ipv6_key_t;

typedef struct
{
    sys_acl_pbr_ipv4_key_flag_t flag;

    uint32                      ip_sa;
    uint32                      ip_sa_mask;

    uint32                      ip_da;
    uint32                      ip_da_mask;

    uint16                      l4_src_port;
    uint16                      l4_src_port_mask;

    uint16                      l4_dst_port;
    uint16                      l4_dst_port_mask;

    uint16                      l4info_mapped;
    uint16                      l4info_mapped_mask;

    uint16                      vrfid;
    uint16                      vrfid_mask;

    uint32                      frag_info             : 2,
                                frag_info_mask        : 2,
                                dscp                  : 6,
                                dscp_mask             : 6, /* 16 */
                                is_tcp                : 1,
                                is_udp                : 1,
                                l4_src_port_range_idx : 3,
                                l4_dst_port_range_idx : 3,
                                tcp_flags_idx         : 2,
                                rsv0                  : 6;
    sys_acl_port_t port;
}sys_acl_pbr_ipv4_key_t;

typedef struct
{
    sys_acl_pbr_ipv6_key_flag_t flag;

    ipv6_addr_t                 ip_sa;
    ipv6_addr_t                 ip_sa_mask;

    ipv6_addr_t                 ip_da;
    ipv6_addr_t                 ip_da_mask;

    uint16                      l4_src_port;
    uint16                      l4_src_port_mask;

    uint16                      l4_dst_port;
    uint16                      l4_dst_port_mask;

    uint16                      l4info_mapped;
    uint16                      l4info_mapped_mask;

    mac_addr_t                  mac_sa;
    mac_addr_t                  mac_sa_mask;
    mac_addr_t                  mac_da;
    mac_addr_t                  mac_da_mask;

    uint32                      flow_label : 20,
                                frag_info  : 2,
                                l2_type    : 4,
                                l3_type    : 4,
                                is_tcp     : 1,
                                is_udp     : 1;

    uint32 flow_label_mask                 : 20,
           frag_info_mask                  : 2,
           l2_type_mask                    : 4,
           l3_type_mask                    : 4,
           rsv0                            : 2;

    uint32 dscp                            : 6,
           dscp_mask                       : 6,
           ip_hdr_error                    : 1,
           stag_cos_mask                   : 3,
           ctag_cos_mask                   : 3,
           l4_src_port_range_idx           : 3,
           l4_dst_port_range_idx           : 3,
           tcp_flags_idx                   : 2,
           rsv1                            : 11;

    uint16 eth_type;
    uint16 eth_type_mask;

    uint16 vrfid;
    uint16 vrfid_mask;

    uint32 svlan      : 12,
           cvlan      : 12,
           stag_cos   : 3,
           stag_cfi   : 1,
           ctag_cos   : 3,
           ctag_cfi   : 1;

    uint32 cvlan_mask : 12,
           svlan_mask : 12,
           rsv2       : 8;
    sys_acl_port_t port;
}sys_acl_pbr_ipv6_key_t;

typedef struct
{
    mac_addr_t mac_da;          /**< [GB] MAC-DA */
    uint16     eth_type;        /**< [GB] Ethernet type */

    uint32     gport         : 13,
               cos           : 3,
               is_logic_port : 1,
               ipv4_packet   : 1,
               vlan_id       : 12,
               rsv0          : 2;
}sys_acl_hash_mac_key_t;

typedef struct
{
    uint32 ip_sa;                       /**< [GB] IP-SA */
    uint32 ip_da;                       /**< [GB] IP-DA */

    uint16 l4_src_port;                 /**< [GB] Layer 4 source port.*/
    uint16 l4_dst_port;                 /**< [GB] Layer 4 dest port */

    uint32 gport         : 13,
           dscp          : 6,
           is_logic_port : 1,
           l4_protocol   : 8,
           ipv4_packet   : 1,
           arp_packet    : 1,
           rsv0          : 2;
}sys_acl_hash_ipv4_key_t;



typedef struct
{
    ctc_direction_t dir;
    uint8           type;
    uint8           block_id; /* block the group belong. For hash, 0xFF. */
    uint8           rsv0;

    union
    {
        ctc_port_bitmap_t port_bitmap;
        uint16            port_class_id;
        uint16            vlan_class_id;
        uint16            service_id;
        uint8             pbr_class_id;
    } un;
}sys_acl_group_info_t;

typedef struct
{
    uint32               group_id; /* keep group_id top! */

    uint8                flag;     /* group flag*/
    uint8                rsv0[2];
    uint8                lchip;
    uint32               entry_count;

#if 0
    uint32               prio_min; /* Min entry priority in group*/
    uint32               prio_max; /* Max entry priority in group*/
#endif

    ctc_slist_t          * entry_list;      /* a list that link entries belong to this group */
    sys_acl_group_info_t group_info;        /* group info */
}sys_acl_group_t;

typedef union
{
    sys_acl_hash_mac_key_t  hash_mac_key;
    sys_acl_hash_ipv4_key_t hash_ipv4_key;
}sys_acl_hash_key_union_t;


typedef struct
{
    uint8                    lchip; /* lchip for hash entry (0xFF means 2chips), tcam's lchip stored in group. */
    uint8                    rsv0[3];
    sys_acl_hash_key_union_t u0;
}sys_acl_hash_key_t;

typedef union
{
    sys_acl_mac_key_t      mac_key;
    sys_acl_mpls_key_t     mpls_key;
    sys_acl_ipv4_key_t     ipv4_key;
    sys_acl_ipv6_key_t     ipv6_key;
    sys_acl_hash_key_t     hash;
    sys_acl_pbr_ipv4_key_t pbr_ipv4_key;
    sys_acl_pbr_ipv6_key_t pbr_ipv6_key;
}sys_acl_key_union_t;

typedef struct
{
    ctc_acl_key_type_t  type;
    sys_acl_key_union_t u0;
}sys_acl_key_t;

typedef struct
{
    ctc_slistnode_t head;                   /* keep head top!! */
    ctc_fpa_entry_t fpae;                   /* keep fpae at second place!! */


    sys_acl_group_t * group;                /* pointer to group node*/
    sys_acl_action_t* action;               /* pointer to action node*/

    sys_acl_key_t   key;                    /* keep key at tail !! */
}sys_acl_entry_t;



typedef struct
{
    ctc_fpa_block_t fpab;
}sys_acl_block_t;

/***********************************
   Below For Internal test
 ************************************/

typedef struct
{
    uint32 merge_mac_ip                : 1,
           ingress_use_mapped_vlan     : 1,
           trill_use_ipv6              : 1,
           arp_use_ipv6                : 1,
           non_ipv4_mpls_use_ipv6      : 1,
           ingress_port_service_acl_en : 1,
           ingress_vlan_service_acl_en : 1,
           egress_port_service_acl_en  : 1,
           egress_vlan_service_acl_en  : 1,
           hash_acl_en                 : 1,
           hash_mac_key_flag           : 1,
           hash_ipv4_key_flag          : 1,
           priority_bitmap_of_stats    : 1,
           rsv0                        : 19;

}sys_acl_global_cfg_flag_t;

typedef struct
{
    sys_acl_global_cfg_flag_t flag;

    uint32                    value;
}sys_acl_global_cfg_t;

extern int32
sys_greatbelt_acl_set_global_cfg(uint8 lchip, sys_acl_global_cfg_t* p_cfg);
/***********************************
   For Internal test End
 ************************************/

extern int32
sys_greatbelt_acl_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg);

/**
 @brief De-Initialize acl module
*/
extern int32
sys_greatbelt_acl_deinit(uint8 lchip);

extern int32
sys_greatbelt_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_greatbelt_acl_destroy_group(uint8 lchip, uint32 group_id);

extern int32
sys_greatbelt_acl_install_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_greatbelt_acl_uninstall_group(uint8 lchip, uint32 group_id);

extern int32
sys_greatbelt_acl_get_group_info(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_greatbelt_acl_add_entry(uint8 lchip, uint32 group_id, ctc_acl_entry_t* acl_entry);

extern int32
sys_greatbelt_acl_remove_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_greatbelt_acl_install_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_greatbelt_acl_uninstall_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_greatbelt_acl_remove_all_entry(uint8 lchip, uint32 group_id);

extern int32
sys_greatbelt_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority);

extern int32
sys_greatbelt_acl_get_multi_entry(uint8 lchip, ctc_acl_query_t* query);

extern int32
sys_greatbelt_acl_update_action(uint8 lchip, uint32 entry_id, ctc_acl_action_t* action);

extern int32
sys_greatbelt_acl_copy_entry(uint8 lchip, ctc_acl_copy_entry_t* copy_entry);

extern int32
sys_greatbelt_acl_show_status(uint8 lchip);

extern int32
sys_greatbelt_acl_show_entry(uint8 lchip, uint8 type, uint32 param, ctc_acl_key_type_t key_type, uint8 detail);

extern int32
sys_greatbelt_acl_show_tcp_flags(uint8 lchip);

extern int32
sys_greatbelt_acl_show_port_range(uint8 lchip);
#ifdef __cplusplus
}
#endif
#endif



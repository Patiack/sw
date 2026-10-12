/**
   @file sys_goldengate_acl.h

   @date 2012-10-17

   @version v2.0

 */
#ifndef _SYS_GOLDENGATE_ACL_H
#define _SYS_GOLDENGATE_ACL_H

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
#include "sys_goldengate_fpa.h"

/***************************************************************
*
*  Defines and Macros
*
***************************************************************/

#define ACL_PORT_CLASS_ID_NUM    1024
#define ACL_VLAN_CLASS_ID_NUM    1024
#define ACL_SERVICE_ID_NUM       0xFFFF

#define ACL_IGS_BLOCK_ID_NUM    4      /* ingress acl */
#define ACL_EGS_BLOCK_ID_NUM    1      /* egress acl */
#define ACL_PBR_BLOCK_ID_NUM    1      /* pbr */
#define ACL_BLOCK_ID_NUM        (ACL_IGS_BLOCK_ID_NUM + ACL_EGS_BLOCK_ID_NUM + ACL_PBR_BLOCK_ID_NUM)
#define SYS_ACL_L4_PORT_NUM             8
#define SYS_ACL_TCP_FLAG_NUM            4

enum sys_acl_count_type_e
{
    ACL_ALLOC_COUNT_TCAM_160,
    ACL_ALLOC_COUNT_TCAM_320,
    ACL_ALLOC_COUNT_TCAM_640,
    ACL_ALLOC_COUNT_PBR_160,
    ACL_ALLOC_COUNT_PBR_320,
    ACL_ALLOC_COUNT_HASH,
    ACL_ALLOC_COUNT_NUM
};
typedef enum sys_acl_count_type_e sys_acl_count_type_t;

struct sys_acl_tcp_flag_s
{
    uint16 ref;

    uint8  match_any; /* 0: match all , 1: match any */
    uint8  tcp_flags; /* bitmap of CTC_ACLQOS_TCP_XXX_FLAG */
};
typedef struct sys_acl_tcp_flag_s   sys_acl_tcp_flag_t;

struct sys_acl_l4port_op_s
{
    uint16 ref;
    uint8  op_dest_port;
    uint8  rsv;

    uint16 port_min;
    uint16 port_max;
};
typedef struct sys_acl_l4port_op_s   sys_acl_l4port_op_t;

struct sys_acl_register_s
{
    uint8  ingress_port_service_acl_en;
    uint8  ingress_vlan_service_acl_en;
    uint8  egress_port_service_acl_en;
    uint8  egress_vlan_service_acl_en;
    uint8  egress_per_port_sel_en;
    uint8  ingress_vlan_acl_num;
    uint8  egress_vlan_acl_num;
    uint8  ipv4_160bit_mode_en;
    uint8  pkt_len_range_en;
};
typedef struct sys_acl_register_s   sys_acl_register_t;

struct sys_acl_action_with_chip_s
{
/* KEEP CHIP_NUM related field below !! Below are fields not in hash caculation. */
    uint32 fwdptr;
    uint16 micro_policer_ptr;
    uint16 macro_policer_ptr;

    uint16 stats_ptr;
    uint16 profile_id;

/* Below are fields not in hash compare */
    uint32 ad_index;
};
typedef struct sys_acl_action_with_chip_s sys_acl_action_with_chip_t;


struct sys_acl_vlan_edit_s
{
    uint8 stag_op;
    uint8 svid_sl;
    uint8 scos_sl;
    uint8 scfi_sl;

    uint8 ctag_op;
    uint8 cvid_sl;
    uint8 ccos_sl ;
    uint8 ccfi_sl ;

    uint32 tpid_index :2;
    uint32 rsv1 :30;

    uint16 profile_id; /* excluded in hash calculation */
    uint8 lchip;
    uint8 rsv;
};
typedef struct sys_acl_vlan_edit_s sys_acl_vlan_edit_t;

struct sys_acl_packet_strip_s
{
    uint32  start_packet_strip   : 2;  /* strip start type. CTC_ACL_STRIP_START_TO_XX */
    uint32 packet_type          : 3;  /* payload packet type. PKT_TYPE_XXX */
    uint32 strip_extra_len      : 7;  /* stript extra length, unit: byte*/
    uint32 rsv                  : 20;
};
typedef struct sys_acl_packet_strip_s sys_acl_packet_strip_t;

struct sys_acl_action_s
{
    uint64                flag;

    uint32                nh_id; /* saved to check if need update. 32 bit*/
    uint32                dsnh_offset;
    uint8                 merge_dsfwd;
    uint8                 dest_chipid;
    uint16                dest_id;     /**< used to construct destmap, drv port, for dest port*/
    uint16                ecmp_group_id;

    uint16                micro_policer_id;
    uint16                macro_policer_id;

    uint16                stats_id;

    uint32               metadata;
    uint16               fid;
    uint16               vrfid;

    uint32              priority               : 6;
    uint32              dscp                   : 6;
    uint32             random_threshold_shift : 4;
    uint32              qos_domain             : 3;
    uint32              trust                  : 3;
    uint32              color                  : 2;
    uint32              acl_log_id             : 2;
    uint32              vlan_edit_valid        : 1;
    uint32              is_ecmp_intf             : 1;
    uint32              metadata_type       : 2;
    uint32              is_mcast                  : 1;
    uint32              aps_en                   : 1;

    uint32 svid    : 12;
    uint32 scos    : 3;
    uint32 scfi    : 1;
    uint32 cvid    : 12;
    uint32 ccos    : 3;
    uint32 ccfi    : 1;

    uint8                      pbr_ecpn;
    uint8                      adjust_len;
    uint16                     l3if_id;
    uint8                      lchip;

    sys_acl_packet_strip_t packet_strip;

    sys_acl_action_with_chip_t chip;  /* keep it at tail. */

    sys_acl_vlan_edit_t       * vlan_edit; /* keep at tail, so won't involve hash cmp, hash make */
};
typedef struct sys_acl_action_s sys_acl_action_t;


struct sys_acl_l4_s
{
    uint32 l4_src_port_range_idx  : 3;
    uint32 l4_dst_port_range_idx  : 3;
    uint32 tcp_flags_idx          : 2;
    uint32 flag_tcp_flags         : 1;
    uint32 flag_l4_src_port_range : 1;
    uint32 flag_l4_dst_port_range : 1;
    uint32 flag_pkt_len_range     : 1;
    uint32 rsv3                   : 6;
    uint32 flag_l4_type           : 1;
    uint32 flag_l4info_mapped     : 1; /* sw only */
    uint32 l4_type                : 4;
    uint32 l4_type_mask           : 4;
    uint32 l4_compress_type       : 2;
    uint32 l4_compress_type_mask  : 2;

    uint16 l4info_mapped;
    uint16 l4info_mapped_mask;

    uint32 l4_src_port      : 16;
    uint32 l4_src_port_mask : 16;
    uint32 l4_dst_port      : 16;
    uint32 l4_dst_port_mask : 16;
};
typedef  struct sys_acl_l4_s sys_acl_l4_t;


struct sys_acl_port_s
{
    uint8      gport_type;
    uint8      gport_type_mask;

    uint8      bitmap_base;
    uint8      bitmap_base_mask;

    uint16     gport;
    uint16     gport_mask;

    uint16     class_id_data;
    uint16     class_id_mask;

    uint16     bitmap_16;
    uint16     valid;

    uint32     bitmap_64[2];
};
typedef struct sys_acl_port_s sys_acl_port_t;

struct sys_acl_sw_tcam_key_mac_s
{
    uint32     flag;

    uint32     l2_type       : 2;
    uint32     l2_type_mask  : 2;
    uint32     vlan_num      : 2;
    uint32     vlan_num_mask : 2;
    uint32     l3_type       : 4;
    uint32     l3_type_mask  : 4;
    uint32     rsv0          : 16;

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

    uint16     metadata;
    uint16     metadata_mask;

    sys_acl_port_t port;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;
} ;
typedef  struct sys_acl_sw_tcam_key_mac_s sys_acl_sw_tcam_key_mac_t;

struct sys_acl_sw_tcam_key_ipv4_s
{
    uint32     flag;
    uint32     sub_flag;
    uint32     sub1_flag;
    uint32     sub2_flag;
    uint32     sub3_flag;
    uint8      key_size;
    uint8      udf_type;

    uint32     svlan          : 12;
    uint32     stag_cos       : 3;
    uint32     stag_cfi       : 1;
    uint32     cvlan          : 12;
    uint32     ctag_cos       : 3;
    uint32     ctag_cfi       : 1;

    uint32     svlan_mask     : 12;
    uint32     stag_cos_mask  : 3;
    uint32     cvlan_mask     : 12;
    uint32     ctag_cos_mask  : 3;
    uint32     stag_valid     : 1;
    uint32     ctag_valid     : 1;

    uint32     l2_type        : 2;
    uint32     l2_type_mask   : 2;
    uint32     l3_type        : 4;
    uint32     l3_type_mask   : 4;
    uint32     routed_packet  : 1;
    uint32     unique_l3_type : 4;
    uint32     vlan_num       : 2;
    uint32     vlan_num_mask  : 2;
    uint32     ip_header_error : 1;
    uint32     rsv0           : 10;

    uint16     metadata;
    uint16     metadata_mask;

    uint32     udf;
    uint32     udf_mask;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    union
    {
        struct
        {
            sys_acl_l4_t l4;

            uint32       rsv1           : 11;
            uint32       ip_option      : 1;
            uint32       frag_info      : 2;
            uint32       frag_info_mask : 2;
            uint32       dscp           : 6;
            uint32       dscp_mask      : 6;
            uint32       ecn            : 2;
            uint32       ecn_mask       : 2;

            uint32       ip_sa;
            uint32       ip_sa_mask;

            uint32       ip_da;
            uint32       ip_da_mask;
        }ip;

        struct
        {
            uint8  label_num;
            uint8  label_num_mask;

            uint32 mpls_label0;
            uint32 mpls_label0_mask;
            uint32 mpls_label1;
            uint32 mpls_label1_mask;
            uint32 mpls_label2;
            uint32 mpls_label2_mask;
        }mpls;

        struct
        {
            uint8 ethoam_level;
            uint8 ethoam_level_mask;

            uint8 ethoam_version;
            uint8 ethoam_version_mask;

            uint8 ethoam_op_code;
            uint8 ethoam_op_code_mask;

            uint8 ethoam_is_y1731;
            uint8 ethoam_is_y1731_mask;
        }ethoam;

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
    sys_acl_port_t port;
};
typedef  struct sys_acl_sw_tcam_key_ipv4_s sys_acl_sw_tcam_key_ipv4_t;

struct sys_acl_sw_tcam_key_ipv6_s
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
    uint32     l2_type         : 2;
    uint32     l2_type_mask    : 2;
    uint32     vlan_num        : 2;
    uint32     vlan_num_mask   : 2;
    uint32     routed_packet   : 1;
    uint32     ip_header_error : 1;
    uint32     rsv1            : 2;

    uint32     flow_label_mask : 20;
    uint32     l3_type         : 4;
    uint32     l3_type_mask    : 4;
    uint32     rsv2            : 4;

    uint16     metadata;
    uint16     metadata_mask;

    uint32     udf;
    uint32     udf_mask;

    uint8       udf_type;

    mac_addr_t mac_da;
    mac_addr_t mac_da_mask;
    mac_addr_t mac_sa;
    mac_addr_t mac_sa_mask;

    union
    {
        struct
        {
            sys_acl_l4_t l4;

            uint32       rsv1           : 11;
            uint32       ip_option      : 1;
            uint32       frag_info      : 2;
            uint32       frag_info_mask : 2;
            uint32       dscp           : 6;
            uint32       dscp_mask      : 6;
            uint32       ecn            : 2;
            uint32       ecn_mask       : 2;


            ipv6_addr_t  ip_sa;
            ipv6_addr_t  ip_sa_mask;

            ipv6_addr_t  ip_da;
            ipv6_addr_t  ip_da_mask;
        }ip;

        struct
        {
            uint16 eth_type;
            uint16 eth_type_mask;
        }other;
    }u0;
    sys_acl_port_t port;
} ;
typedef  struct sys_acl_sw_tcam_key_ipv6_s sys_acl_sw_tcam_key_ipv6_t;


struct sys_acl_pbr_ipv4_key_s
{
    uint32 flag;
    uint32 sub_flag;

    uint16 vrfid;
    uint16 vrfid_mask;

    union
    {
        struct
        {
            sys_acl_l4_t l4;

            uint32       rsv1           : 15;
            uint32       ip_option      : 1;
            uint32       frag_info      : 2;
            uint32       frag_info_mask : 2;
            uint32       dscp           : 6;
            uint32       dscp_mask      : 6;

            uint32       ip_sa;
            uint32       ip_sa_mask;

            uint32       ip_da;
            uint32       ip_da_mask;
        } ip;
    }u0;
    sys_acl_port_t port;
};
typedef struct sys_acl_pbr_ipv4_key_s sys_acl_pbr_ipv4_key_t;


struct sys_acl_pbr_ipv6_key_s
{
    uint32 flag;
    uint32 sub_flag;

    uint32 flow_label;
    uint32 flow_label_mask;

    uint16 vrfid;
    uint16 vrfid_mask;

    union
    {
        struct
        {
            sys_acl_l4_t l4;

            uint32       rsv1           : 15;
            uint32       ip_option      : 1; /* not support */
            uint32       frag_info      : 2;
            uint32       frag_info_mask : 2;
            uint32       dscp           : 6;
            uint32       dscp_mask      : 6;


            ipv6_addr_t  ip_sa;
            ipv6_addr_t  ip_sa_mask;

            ipv6_addr_t  ip_da;
            ipv6_addr_t  ip_da_mask;
        }ip;
    }u0;
    sys_acl_port_t port;
};
typedef struct sys_acl_pbr_ipv6_key_s sys_acl_pbr_ipv6_key_t;

struct sys_acl_hash_mac_key_s
{
    mac_addr_t mac_da;
    mac_addr_t mac_sa;

    uint16     eth_type;
    uint8      field_sel_id;

    uint16     gport;
    uint32     cos           : 3;
    uint32   vlan_id       : 12;
    uint32   cfi           : 1;
    uint32  tag_exist     : 1;
    uint32  is_ctag       : 1;
    uint32  rsv           : 14;
};
typedef struct sys_acl_hash_mac_key_s sys_acl_hash_mac_key_t;

struct sys_acl_hash_ipv4_key_s
{
    uint32 ip_sa;
    uint32 ip_da;

    uint16 l4_src_port;
    uint16 l4_dst_port;

    uint32 gport         : 16;
    uint32 dscp          : 6;
    uint32 l4_protocol   : 8;
    uint32 ecn           : 2;

    uint8  field_sel_id;
    uint8  rsv1;
    uint16 metadata;
    uint32 vni;
    uint32 gre_key;
};
typedef struct sys_acl_hash_ipv4_key_s sys_acl_hash_ipv4_key_t;

struct sys_acl_hash_mpls_key_s
{
    uint16 metadata;
    uint16 gport;

    uint8  field_sel_id;
    uint8  label_num;
    uint16 rsv0;

    uint32 mpls_label0  :20;
    uint32 mpls_exp0    :3;
    uint32 mpls_s0      :1;
    uint32 mpls_ttl0    :8;

    uint32 mpls_label1  :20;
    uint32 mpls_exp1    :3;
    uint32 mpls_s1      :1;
    uint32 mpls_ttl1    :8;

    uint32 mpls_label2  :20;
    uint32 mpls_exp2    :3;
    uint32 mpls_s2      :1;
    uint32 mpls_ttl2    :8;
};
typedef struct sys_acl_hash_mpls_key_s sys_acl_hash_mpls_key_t;

struct sys_acl_hash_ipv6_key_s
{
    uint8 field_sel_id;
	uint8 rsv0;
    uint16 gport;

    ipv6_addr_t ip_sa;
    ipv6_addr_t ip_da;

    uint8  dscp;
    uint8  l4_type;
	uint16 rsv1;

    uint16 l4_src_port;
    uint16 l4_dst_port;
    uint32 vni;
    uint32 gre_key;
};
typedef struct sys_acl_hash_ipv6_key_s sys_acl_hash_ipv6_key_t;

struct sys_acl_hash_l2_l3_key_s
{
    uint8  field_sel_id;
    uint8 rsv0;
    uint16 eth_type;

    uint16 gport;
    uint16 metadata;

    mac_addr_t mac_da;
    mac_addr_t mac_sa;

    uint32 stag_cos:3;
    uint32 stag_vid:12;
    uint32 stag_cfi:1;
    uint32 ctag_cos:3;
    uint32 ctag_vid:12;
    uint32 ctag_cfi:1;

    uint8 ctag_valid;
    uint8 stag_valid;
    uint8 l3_type;
    uint8 l4_type;

    uint16 l4_src_port;
    uint16 l4_dst_port;
    uint32 vni;
    uint32 gre_key;
    union
    {
        struct
        {
            uint32 ip_sa;
            uint32 ip_da;

            uint8  dscp;
            uint8  ecn;
            uint8  ttl;
        }ipv4;

        struct
        {
            uint8  label_num;

            uint32 mpls_label0  :20;
            uint32 mpls_exp0    :3;
            uint32 mpls_s0      :1;
            uint32 mpls_ttl0    :8;

            uint32 mpls_label1  :20;
            uint32 mpls_exp1    :3;
            uint32 mpls_s1      :1;
            uint32 mpls_ttl1    :8;

            uint32 mpls_label2  :20;
            uint32 mpls_exp2    :3;
            uint32 mpls_s2      :1;
            uint32 mpls_ttl2    :8;

        }mpls;
    }l3;

};
typedef struct sys_acl_hash_l2_l3_key_s sys_acl_hash_l2_l3_key_t;


#define ACL_BITMAP_STATUS_64    0 /* if only ipv6 exist*/
#define ACL_BITMAP_STATUS_16    1 /* if non-ipv6 exist*/

typedef struct
{
    ctc_direction_t dir;
    uint8           type;

    uint8           block_id;      /* block the group belong. For hash, 0xFF. */
    uint8           bitmap_status; /**/

    union
    {
        ctc_port_bitmap_t port_bitmap;
        uint16            port_class_id;
        uint16            vlan_class_id;
        uint16            service_id;
        uint8             pbr_class_id;
        uint16            gport;
    } un;
}sys_acl_group_info_t;

typedef struct
{
    uint32               group_id; /* keep group_id top! */
    uint32               entry_count;
    uint8                lchip;
    uint8                rsv0[3];
    ctc_slist_t          * entry_list;      /* a list that link entries belong to this group */
    sys_acl_group_info_t group_info;        /* group info */
}sys_acl_group_t;

union sys_acl_hash_key_union_u
{
    sys_acl_hash_mac_key_t  hash_mac_key;
    sys_acl_hash_ipv4_key_t hash_ipv4_key;
    sys_acl_hash_l2_l3_key_t hash_l2_l3_key;
    sys_acl_hash_mpls_key_t hash_mpls_key;
    sys_acl_hash_ipv6_key_t hash_ipv6_key;
};
typedef union sys_acl_hash_key_union_u sys_acl_hash_key_union_t;


struct sys_acl_hash_key_s
{
    sys_acl_hash_key_union_t u0;
};
typedef struct sys_acl_hash_key_s sys_acl_hash_key_t;

union sys_acl_key_union_u
{
    sys_acl_sw_tcam_key_mac_t  mac_key;
    sys_acl_sw_tcam_key_ipv4_t ipv4_key;
    sys_acl_sw_tcam_key_ipv6_t ipv6_key;
    sys_acl_hash_key_t         hash;
    sys_acl_pbr_ipv4_key_t     pbr_ipv4_key;
    sys_acl_pbr_ipv6_key_t     pbr_ipv6_key;
};
typedef union sys_acl_key_union_u sys_acl_key_union_t;

struct sys_acl_key_s
{
    ctc_acl_key_type_t  type;
    sys_acl_key_union_t u0;
};
typedef struct sys_acl_key_s sys_acl_key_t;

typedef struct
{
    ctc_slistnode_t head;                   /* keep head top!! */
    ctc_fpa_entry_t fpae;                   /* keep fpae at second place!! */


    sys_acl_group_t * group;                /* pointer to group node*/
    sys_acl_action_t* action;               /* pointer to action node*/
    uint8 lchip;
    sys_acl_key_t   key;                    /* keep key at tail !! */
}sys_acl_entry_t;



typedef struct
{
    ctc_fpa_block_t fpab;
}sys_acl_block_t;

/***********************************
   Below For Internal test
 ************************************/

enum sys_acl_global_property_e
{
    SYS_ACL_IGS_PORT_SERVICE_ACL_EN,
    SYS_ACL_IGS_VLAN_SERVICE_ACL_EN,
    SYS_ACL_IGS_VLAN_ACL_NUM,
    SYS_ACL_EGS_PORT_SERVICE_ACL_EN,
    SYS_ACL_EGS_VLAN_SERVICE_ACL_EN,
    SYS_ACL_EGS_VLAN_ACL_NUM
};
typedef enum sys_acl_global_property_e sys_acl_global_property_t;

extern int32
sys_goldengate_acl_set_global_cfg(uint8 lchip, sys_acl_global_property_t property, uint32 value);
/***********************************
   For Internal test End
 ************************************/

extern int32
sys_goldengate_acl_init(uint8 lchip, ctc_acl_global_cfg_t* acl_global_cfg);

/**
 @brief De-Initialize acl module
*/
extern int32
sys_goldengate_acl_deinit(uint8 lchip);

extern int32
sys_goldengate_acl_create_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_goldengate_acl_destroy_group(uint8 lchip, uint32 group_id);

extern int32
sys_goldengate_acl_install_group(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_goldengate_acl_uninstall_group(uint8 lchip, uint32 group_id);

extern int32
sys_goldengate_acl_get_group_info(uint8 lchip, uint32 group_id, ctc_acl_group_info_t* group_info);

extern int32
sys_goldengate_acl_add_entry(uint8 lchip, uint32 group_id, ctc_acl_entry_t* acl_entry);

extern int32
sys_goldengate_acl_remove_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_goldengate_acl_install_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_goldengate_acl_uninstall_entry(uint8 lchip, uint32 entry_id);

extern int32
sys_goldengate_acl_remove_all_entry(uint8 lchip, uint32 group_id);

extern int32
sys_goldengate_acl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority);

extern int32
sys_goldengate_acl_get_multi_entry(uint8 lchip, ctc_acl_query_t* query);

extern int32
sys_goldengate_acl_update_action(uint8 lchip, uint32 entry_id, ctc_acl_action_t* action);

extern int32
sys_goldengate_acl_copy_entry(uint8 lchip, ctc_acl_copy_entry_t* copy_entry);

extern int32
sys_goldengate_acl_set_hash_field_sel(uint8 lchip, ctc_acl_hash_field_sel_t* field_sel);

extern int32
sys_goldengate_acl_show_status(uint8 lchip);

extern int32
sys_goldengate_acl_show_entry(uint8 lchip, uint8 type, uint32 param, ctc_acl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_acl_show_tcp_flags(uint8 lchip);

extern int32
sys_goldengate_acl_show_port_range(uint8 lchip);

extern int32
sys_goldengate_acl_show_group(uint8 lchip, uint8 type);

extern int32
sys_goldengate_acl_set_pkt_length_range(uint8 lchip, uint8 num, uint32* p_array);

extern int32
sys_goldengate_acl_set_pkt_length_range_en(uint8 lchip, uint8 enable);

#ifdef __cplusplus
}
#endif
#endif



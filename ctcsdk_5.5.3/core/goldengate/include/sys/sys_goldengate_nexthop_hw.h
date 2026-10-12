#include "ctc_nexthop.h"

struct sys_nexthop_s
{

    uint8 update_type;
    uint32 offset;


    uint8  is_nexthop8_w;
    uint8  share_type;
    uint8  payload_operation;
    uint8  next_hop_bypass;

    uint8  is_leaf;
    uint8  aps_bridge_en;
    uint8  mtu_check_en;
    uint8  mirror_tag_add;

    uint8  vlan_xlate_mode;
    uint8  svlan_tagged;
    uint8  svlan_tag_disable;
    uint8  copy_dscp;
    uint8  replace_dscp;
    uint8  stag_cos;
    uint8  svlan_tpid;
    uint8  tagged_mode;
    uint8  output_svlan_id_valid;
    uint8  replace_stag_cos;
    uint8  derive_stag_cos;
    uint8  stag_cfi;
    uint8  svlan_tpid_en;
    uint8  cvlan_tag_disable;
    uint8  ctag_share_mode;
    uint8  output_cvlan_id_valid;
    uint8  replace_ctag_cos;
    uint8  copy_ctag_cos;
    uint8  tpid_swap;
    uint8  cos_swap;
    uint16 output_svlan_id;
    uint16 output_cvlan_id;

    uint16 stats_ptr;
    uint16 dest_vlan_ptr;

    uint8  fid_valid;
    uint8  stp_id_valid;
    uint8  logic_port_check;
    uint8  stp_id;
    uint16 fid;
    uint16 logic_dest_port;

    uint8  use_packet_ttl;
    uint8  ttl_no_dec;
    uint8  outer_edit_valid;
    uint8  outer_edit_location;
    uint8  outer_edit_ptr_type;
    uint8  inner_edit_ptr_type;
    uint8  is_vxlan_route_op;


    uint8 hash_num;
    uint8 xerpsan_en;
    uint8 span_en;
    uint8 keep_igs_ts;
    uint8 cw_index;

    uint32 inner_edit_ptr;
    uint32 outer_edit_ptr;

    mac_addr_t mac_da;
    uint8 is_drop;

};
typedef struct sys_nexthop_s  sys_nexthop_t;

struct sys_fwd_s
{

    uint32 offset;

    uint8 force_back_en;
    uint8 bypass_igs_edit;
    uint8 nexthop_ext;
    uint8 aps_bridge_en;
    uint8 adjust_len_idx;
    uint8 cut_through_dis;
    uint8 critical_packet;
    uint8 lport_en;
    uint32 dest_map;
    uint32 nexthop_ptr;
};
typedef struct sys_fwd_s  sys_fwd_t;

struct sys_met_s
{
    uint8 is_6w;
    uint8 mcast_mode;
    uint32 next_hop_ptr;

    uint32 port_bitmap[4];
    uint8   port_bitmap_base;

    uint8 force_back_en;
    uint16 logic_dest_port;
    uint8 leaf_check_en;
    uint8 logic_port_check_en;
    uint8 logic_port_type_check;
    uint8 aps_bridge_en;
    uint16 dest_id;
    uint16 replicate_num;

    uint8 is_agg;
    uint8 phy_port_chk_discard;
    uint8 remote_chip;
    uint8 end_local_rep;
    uint16 next_met_entry_ptr;
    uint8 next_met_entry_type;
    uint8 next_hop_ext;
};
typedef struct sys_met_s  sys_met_t;

struct sys_dsl2edit_s
{

    uint32 offset;

    uint8 ds_type;
    uint8 l2_rewrite_type;
    uint8 derive_mcast_mac_for_ip;
    uint8 is_vlan_valid;

    uint16 output_vlan_id;
    uint8 is_6w;
    uint8 dynamic;
    uint8 use_port_mac;
    mac_addr_t mac_da;
    mac_addr_t mac_sa;
    uint8 update_mac_sa;
    uint8 strip_inner_vlan;

    uint16  ether_type;
    uint8  is_span_ext_hdr;
    uint8 fpma;

    uint32 dest_map;
    uint32 nexthop_ptr;
    uint8  nexthop_ext;

};
typedef struct sys_dsl2edit_s  sys_dsl2edit_t;



struct sys_dsmpls_s
{

    uint32 offset;

    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 next_editptr_valid;
    uint8 outer_editptr_type;
    uint16 outer_editptr;

    uint16 logic_dest_port;
    uint8 mpls_oam_index;
    uint8 oam_en;
    uint16 statsptr;

    uint32 label;
    uint8 label_type;
    uint8 mcast_label;
    uint8 entropy_label_en;
    uint8 src_dscp_type;
    uint8 derive_label;
    uint8 derive_exp;
    uint8 exp;
    uint8 map_ttl_mode;
    uint8 map_ttl;
    uint8 ttl_index;

};
typedef struct sys_dsmpls_s  sys_dsmpls_t;


struct sys_dsl3edit_tunnelv4_s
{

    uint8 share_type;

    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 map_ttl;
    uint8 ttl;
    uint8 copy_dont_frag;
    uint8 dont_frag;
    uint8 derive_dscp;
    uint8 dscp;

    uint8 ip_protocol_type;
    uint8 mtu_check_en;
    uint16 mtu_size;

    uint32 ipda;
    uint32 ipsa;
    uint32 ipsa_6to4;
    uint8 ipsa_valid;
    uint8 ipsa_index;

    uint8 inner_header_valid;
    uint8 inner_header_type;
    uint16 stats_ptr;

    uint16 gre_protocol;
    uint8 gre_version;
    uint8 gre_flags;
    uint32 gre_key;

    uint8 isatp_tunnel;

    uint8 tunnel6_to4_da;
    uint8 tunnel6_to4_sa;
    uint8 tunnel6_to4_da_ipv4_prefixlen;
    uint8 tunnel6_to4_da_ipv6_prefixlen;
    uint8 tunnel6_to4_sa_ipv4_prefixlen;
    uint8 tunnel6_to4_sa_ipv6_prefixlen;

    uint8 out_l2edit_valid;
    uint8 xerspan_hdr_en;
    uint8 xerspan_hdr_with_hash_en;

    uint32 l2edit_ptr;

    uint8 is_evxlan;
    uint8 evxlan_protocl_index; /* use for getting protocl from EpePktProcCtl.array[evxlan_protocl_index].vxlanProtocolType,
       how to config the index refer to sys_goldengate_overlay_tunnel_init(lchip)*/

    uint8 is_geneve;
    uint8 udp_src_port_en;  /* used for vxlan udp src port */
    uint16 udp_src_port;
	uint8 is_gre_auto;


};
typedef struct sys_dsl3edit_tunnelv4_s  sys_dsl3edit_tunnelv4_t;


struct sys_dsl3edit_tunnelv6_s
{

    uint8 share_type;

    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 map_ttl;
    uint8 ttl;
    uint8 derive_dscp;
    uint8 tos;

    uint8 ip_protocol_type;
    uint8 mtu_check_en;
    uint16 mtu_size;

    ipv6_addr_t ipda;
    uint8 ipsa_index;

    uint8 inner_header_valid;
    uint8 inner_header_type;
    uint16 stats_ptr;

    uint16 gre_protocol;
    uint8 gre_version;
    uint8 gre_flags;
    uint32 gre_key;

    uint8 new_flow_label_valid;
    uint8 new_flow_label_mode;
    uint32 flow_label;
    uint8 out_l2edit_valid;

    uint8 xerspan_hdr_en;
    uint8 xerspan_hdr_with_hash_en;
    uint32 l2edit_ptr;

    uint8 is_evxlan;
    uint8 evxlan_protocl_index; /* use for getting protocl from EpePktProcCtl.array[evxlan_protocl_index].vxlanProtocolType,
       how to config the index refer to sys_goldengate_overlay_tunnel_init(lchip)*/

    uint8 is_geneve;
    uint8 udp_src_port_en;  /* used for vxlan udp src port */
    uint16 udp_src_port;
	uint8 is_gre_auto;
};
typedef struct sys_dsl3edit_tunnelv6_s  sys_dsl3edit_tunnelv6_t;

struct sys_dsl3edit_nat4w_s
{
    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 ip_da_prefix_length;
    uint8 ipv4_embeded_mode;

    uint32 ipda;
    uint16 l4_dest_port;
    uint8 replace_ip_da;
    uint8 replace_l4_dest_port;
};
typedef struct sys_dsl3edit_nat4w_s  sys_dsl3edit_nat4w_t;

struct sys_dsl3edit_nat8w_s
{
    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 ip_da_prefix_length;
    uint8 ipv4_embeded_mode;

    ipv6_addr_t ipda;
};
typedef struct sys_dsl3edit_nat8w_s  sys_dsl3edit_nat8w_t;

struct sys_dsl3edit_trill_s
{
    uint8 ds_type;
    uint8 l3_rewrite_type;
    uint8 ttl;
    uint8 mtu_check_en;

    uint16 mtu_size;
    uint8 rsv[2];

    uint16 ingress_nickname;
    uint16 egress_nickname;

    uint16 stats_ptr;
};
typedef struct sys_dsl3edit_trill_s  sys_dsl3edit_trill_t;

struct sys_dsl3edit_of_s
{
    uint8 ds_type;
    uint8 l3_rewrite_type;

    uint8 packet_type;
    uint32 flag;

    union
    {
        ip_addr_t   ipv4;
        ipv6_addr_t ipv6;
    } ip_sa;

    union
    {
        ip_addr_t   ipv4;
        ipv6_addr_t ipv6;
    } ip_da;

    uint8  protocol;
    uint8  ttl;
    uint8  ecn;
    uint8  ecn_select;
    uint8  dscp_select;
    uint8  dscp_or_tos;
    uint32 flow_label;

    /*UDP/TCP Port*/
    uint16 l4_src_port;
    uint16 l4_dst_port;

    /*GRE KEY*/
    uint32 gre_key;

    /*ICMP*/
    uint8  icmp_type;
    uint8  icmp_code;

    uint16  arp_ht;
    uint16  arp_pt;
    uint8   arp_halen;
    uint8   arp_palen;
    uint16  arp_op;
    mac_addr_t   arp_sha;
    mac_addr_t   arp_tha;
    uint32  arp_spa;
    uint32  arp_tpa;

    uint32 edit_offset;
};
typedef struct sys_dsl3edit_of_s  sys_dsl3edit_of_t;

struct sys_dsl2edit_of6w_s
{
  uint8    vlan_profile_id;
  ctc_misc_nh_flex_edit_param_t* p_flex_edit;

};
typedef struct sys_dsl2edit_of6w_s  sys_dsl2edit_of6w_t;


struct sys_dsl2edit_vlan_edit_s
{
    uint32 stag_op     : 3,
           svid_sl     : 2,
           scos_sl     : 2,
           scfi_sl     : 2,
           ctag_op     : 3,
           cvid_sl     : 2,
           ccos_sl     : 2,
           ccfi_sl     : 2,
           new_stpid_idx :2,
           new_stpid_en:1,
           rsv         : 11;
	uint8 profile_id;
	uint8 rsv2[3];

};
typedef struct sys_dsl2edit_vlan_edit_s sys_dsl2edit_vlan_edit_t;

struct sys_dsecmp_group_s
{
    uint8 dlb_en;
    uint8 resilient_hash_en;
    uint8 rr_en;
    uint8 member_num;
    uint16 member_base;
    uint8 stats_valid;
};
typedef struct sys_dsecmp_group_s  sys_dsecmp_group_t;

struct sys_dsecmp_member_s
{
    uint8 valid0;
    uint16 dsfwdptr0;
    uint8 dest_channel0;

    uint8 valid1;
    uint16 dsfwdptr1;
    uint8 dest_channel1;

    uint8 valid2;
    uint16 dsfwdptr2;
    uint8 dest_channel2;

    uint8 valid3;
    uint16 dsfwdptr3;
    uint8 dest_channel3;
};
typedef struct sys_dsecmp_member_s  sys_dsecmp_member_t;

struct sys_dsecmp_rr_s
{
    uint8 random_rr_en;
    uint8 member_num;
};
typedef struct sys_dsecmp_rr_s  sys_dsecmp_rr_t;



int32
sys_goldengate_nh_write_asic_table(uint8 lchip, uint8 table_type, uint32 offset, void* value);
int32
sys_goldengate_nh_update_asic_table(uint8 lchip, uint8 table_type, uint32 offset, void* value);

int32
sys_goldengate_nh_get_asic_table(uint8 lchip, uint8 table_type, uint32 offset, void* value);

int32
sys_goldengate_nh_set_asic_table(uint8 lchip, uint8 table_type, uint32 offset, void* value);

int32
sys_goldengate_nh_table_info_init(uint8 lchip);

int32
sys_goldengate_nh_global_cfg_init(uint8 lchip);



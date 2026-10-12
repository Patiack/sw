/**
 @file sys_goldengate_wb_nh.h

 @date 2010-01-13

 @version v2.0

 The file defines queue api
*/

#ifndef _SYS_GOLDENGATE_WB_NH_H_
#define _SYS_GOLDENGATE_WB_NH_H_
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
#include "ctc_macro.h"
#include "ctc_debug.h"
#include "ctc_warmboot.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
/**
 @brief  define l2 FDB entry sync flags
*/

enum sys_wb_appid_nh_subid_e
{
    SYS_WB_APPID_NH_SUBID_MASTER   ,
    SYS_WB_APPID_NH_SUBID_BRGUC_NODE,
    SYS_WB_APPID_NH_SUBID_INFO_COM,
    SYS_WB_APPID_NH_SUBID_MPLS_TUNNEL,
    SYS_WB_APPID_NH_SUBID_DSL2EDIT8W_TREE,
    SYS_WB_APPID_NH_SUBID_L2EDIT6w,
    SYS_WB_APPID_NH_SUBID_DSL2EDIT4W_TREE,
    SYS_WB_APPID_NH_SUBID_L2EDIT4w_SWAP,
    SYS_WB_APPID_NH_SUBID_L2EDIT3w,
    SYS_WB_APPID_NH_SUBID_ECMP,
    SYS_WB_APPID_NH_SUBID_INFO_MCAST,
    SYS_WB_APPID_NH_SUBID_ARP,
    SYS_WB_APPID_NH_SUBID_VNI
};
typedef enum sys_wb_appid_nh_subid_e sys_wb_appid_nh_subid_t;

#define SYS_WB_GG_MAX_ECPN  64
#define  SYS_WB_NH_APS_M 2
#define SYS_WB_NH_CW_NUM    16

struct sys_wb_nh_dsl2edit4w_tree_key_s
{
    mac_addr_t mac_da;
    uint16    output_vid;
    uint8     strip_inner_vlan;
    uint8     fpma;
};
typedef struct sys_wb_nh_dsl2edit4w_tree_key_s  sys_wb_nh_dsl2edit4w_tree_key_t;


struct sys_wb_nh_info_special_s
{
    uint16 dest_gport;
    uint16 rsv;
};
typedef struct sys_wb_nh_info_special_s sys_wb_nh_info_special_t;

struct sys_wb_nh_info_rspan_s
{
    uint8 remote_chip;
    uint8 rsv[3];
};
typedef struct sys_wb_nh_info_rspan_s sys_wb_nh_info_rspan_t;

struct sys_wb_nh_info_brguc_s
{
    uint16  dest_gport;
    uint8   nh_sub_type; /*sys_nh_param_brguc_sub_type_t*/
    uint8   rsv0;
    uint16 dest_logic_port;
};
typedef struct sys_wb_nh_info_brguc_s sys_wb_nh_info_brguc_t;


struct sys_wb_nh_info_ipuc_s
{
    uint16 flag;
    uint16 l3ifid;
    uint16 gport;
    uint16 oam_ref;
    uint32 stats_id;
    uint16 arp_id;
    uint16 protection_arp_id;
    uint16      protection_l3ifid;
    mac_addr_t  mac_da;
    mac_addr_t  protection_mac_da;
    sys_wb_nh_dsl2edit4w_tree_key_t l2edit;
    sys_wb_nh_dsl2edit4w_tree_key_t protect_l2edit;
    /*not support ipuc callback function*/
};
typedef struct sys_wb_nh_info_ipuc_s sys_wb_nh_info_ipuc_t;


struct sys_wb_nh_info_ecmp_s
{
    uint8    ecmp_cnt;    /*include unresoved nh member*/
    uint8    valid_cnt;   /*valid member*/
    uint8    type;        /*ctc_nh_ecmp_type_t*/
    uint8    failover_en;
    uint32   ecmp_nh_id;
    uint16   ecmp_group_id;
    uint8    random_rr_en;
    uint8    stats_valid;
    uint32   nh_array[SYS_WB_GG_MAX_ECPN];
    uint8    member_id_array[SYS_WB_GG_MAX_ECPN];     /* member id of each ecmp member entry for dlb */
    uint8    entry_count_array[SYS_WB_GG_MAX_ECPN]; /* ecmp member entry count of each member id for dlb */
    uint16 gport;
    uint16 ecmp_member_base;
    uint8   mem_num;  /*max member num in group*/
    uint8   rsv[3];
    uint32 l3edit_offset_base;
    uint32 l2edit_offset_base;
};
typedef struct sys_wb_nh_info_ecmp_s sys_wb_nh_info_ecmp_t;

struct sys_wb_nh_vni_fid_s
{
    /*key*/
    uint32 vn_id;
    uint32  calc_key_len[0];

    /*data*/
    uint16 fid;
    uint16 ref_cnt;
};
typedef struct sys_wb_nh_vni_fid_s sys_wb_nh_vni_fid_t;

struct sys_wb_nh_info_mpls_s
{
    uint16   aps_group_id;
    uint16   gport;
    uint16   l3ifid;
    uint16   dest_logic_port; /*H-vpls*/
    uint8    cw_index;
    sys_wb_nh_dsl2edit4w_tree_key_t l2edit_key;

};
typedef struct sys_wb_nh_info_mpls_s sys_wb_nh_info_mpls_t;



struct sys_wb_nh_info_ip_tunnel_s
{
    uint16 l3ifid;
    uint16 ecmp_if_id;
    uint16 gport;
    uint32 dsl3edit_offset;
    uint8  flag; /* sys_nh_ip_tunnel_flag_t */
    uint8  sa_index;

    uint16 dest_vlan_ptr;
    uint16 span_id;
    sys_wb_nh_dsl2edit4w_tree_key_t l2edit_key;

    uint32    vn_id;
    uint16   dest_logic_port; /*H-overlay*/
    uint8    rsv[2];
};
typedef struct sys_wb_nh_info_ip_tunnel_s sys_wb_nh_info_ip_tunnel_t;

struct sys_wb_nh_info_trill_s
{

    uint16 ingress_nickname;
    uint16 egress_nickname;
    uint16 l3ifid;
    uint16 gport;
    uint32 dsl3edit_offset;
    sys_wb_nh_dsl2edit4w_tree_key_t l2edit_key;
    uint16 dest_vlan_ptr;
};
typedef struct sys_wb_nh_info_trill_s sys_wb_nh_info_trill_t;

struct sys_wb_nh_info_misc_s
{
    uint32 dsl2edit_offset;
    uint32 dsl3edit_offset;

    uint32 gport;
    uint8  is_swap_mac;
    uint8  is_reflective;

    uint8  misc_nh_type;
    uint8  l3edit_entry_type;
    uint8  l2edit_entry_type;
    uint8 rsv;
};
typedef struct sys_wb_nh_info_misc_s sys_wb_nh_info_misc_t;



struct sys_wb_nh_info_com_hdr_s
{
    uint8 dsnh_entry_num;
    uint8 adjust_len;
    uint8 rsv[2];
    uint32 nh_entry_flags; /*sys_nh_info_flag_type_t*/
    uint32 dsfwd_offset;
    uint32 stats_ptr;
    uint32 dsnh_offset;
     /*  sys_nh_ref_list_node_t* p_ref_nh_list;   //sys_nh_ref_list_node_t*/
};
typedef struct sys_wb_nh_info_com_hdr_s sys_wb_nh_info_com_hdr_t;

struct sys_wb_nh_dsl2edit4w_tree_s
{
    /*key*/
    sys_wb_nh_dsl2edit4w_tree_key_t l2edit_key;
    uint32 calc_key_len[0];

    /*data*/
    uint32 offset;
    uint8 is_ecmp_if;
    uint8  output_vlan_is_svlan;
    uint16 ref_cnt;
    uint8  dynamic;
    uint8  is_share_mac;
    uint8 rsv[2];
};
typedef struct sys_wb_nh_dsl2edit4w_tree_s  sys_wb_nh_dsl2edit4w_tree_t;
typedef struct sys_wb_nh_dsl2edit4w_tree_s  sys_wb_nh_dsl2edit4w_swap_tree_t;
typedef struct sys_wb_nh_dsl2edit4w_tree_s  sys_wb_nh_dsl2edit3w_tree_t;

struct sys_wb_nh_dsl2edit8w_tree_key_s
{
    mac_addr_t mac_da;
    mac_addr_t mac_sa;
    uint8 strip_inner_vlan;
    uint16 output_vid;
    uint8 output_vlan_is_svlan;
    uint8 packet_type;
};
typedef struct sys_wb_nh_dsl2edit8w_tree_key_s  sys_wb_nh_dsl2edit8w_tree_key_t;

struct sys_wb_nh_dsl2edit8w_tree_s
{
    /*key*/
    sys_wb_nh_dsl2edit8w_tree_key_t  l2edit_key;
    uint32 calc_key_len[0];

    /*data*/
    uint32 offset;
    uint8 update_mac_sa;
    uint16 ref_cnt;
    uint8 is_ecmp_if;
    uint8 rsv0[3];
};
typedef struct sys_wb_nh_dsl2edit8w_tree_s  sys_wb_nh_dsl2edit8w_tree_t;
typedef struct sys_wb_nh_dsl2edit8w_tree_s  sys_wb_nh_dsl2edit6w_tree_t;


struct sys_wb_nh_arp_s
{

    /*key*/
    uint16  arp_id;
	uint16  rsv;
    uint32  calc_key_len[0];

    /*data*/

    mac_addr_t mac_da;
    uint8  flag;
    uint8  l3if_type; /**< the type of l3interface , CTC_L3IF_TYPE_XXX */
    uint32 offset;          /*outer l2 edit for mpls/tunnel vlan & mac edit*/
    uint32 in_l2_offset;    /*inner l2 edit for ipuc vlan edit*/

    uint16 output_vid;
    uint8  output_vlan_is_svlan;
    uint8  packet_type;

    uint32 gport;
    uint32 nh_offset;

};
typedef struct sys_wb_nh_arp_s sys_wb_nh_arp_t;


struct sys_wb_nh_mpls_tunnel_s
{
    /*key*/
    uint16  tunnel_id;
    uint32 calc_key_len[0];

    /*data*/
    uint16  gport; /*if SYS_NH_MPLS_TUNNEL_FLAG_APS is set, it is aps group id*/
    uint16  l3ifid;
    uint8    label_num;
    uint8   flag; /*sys_nh_dsmet_flag_t,SYS_NH_MPLS_TUNNEL_FLAG_XX*/
    uint16  ref_cnt;
    uint16  l2edit_offset[SYS_WB_NH_APS_M][SYS_WB_NH_APS_M];
    uint16  spme_offset[SYS_WB_NH_APS_M][SYS_WB_NH_APS_M];
    uint16  arp_id[SYS_WB_NH_APS_M][SYS_WB_NH_APS_M];
    uint16  lsp_offset[SYS_WB_NH_APS_M];

    uint16  stats_ptr ;
    uint16  p_stats_ptr;

    sys_wb_nh_dsl2edit4w_tree_key_t l2edit[SYS_WB_NH_APS_M][SYS_WB_NH_APS_M];
};
typedef struct sys_wb_nh_mpls_tunnel_s  sys_wb_nh_mpls_tunnel_t;


struct sys_wb_nh_info_mcast_s
{
   /*key*/
    uint32   basic_met_offset;
    uint32   dsmet_offset;
    uint16   vid;
    uint32   calc_key_len[0];

     /*data*/
    sys_wb_nh_info_com_hdr_t hdr;
   /*group info*/
    uint32  nh_id;
    uint16   physical_replication_num;
    uint8    is_mirror;
    uint8    mirror_ref_cnt;

    /*member info*/

    uint32   next_dsmet_offset;
    uint16   flag; /*sys_nh_dsmet_flag_t,SYS_NH_DSMET_FLAG_XX*/
    uint16   logic_port;

    uint32   dsnh_offset;
    uint16   ref_nhid;
    uint16   ucastid;


    uint16   replicate_num; /*replicate_num for IPMC; port num of l2mc port bitmap*/
    uint32   pbm[4];

    uint8    free_dsnh_offset_cnt;
    uint8    member_type;
    uint8    port_type;
    uint8    entry_type;
};
typedef struct sys_wb_nh_info_mcast_s sys_wb_nh_info_mcast_t;

struct sys_wb_nh_info_com_s
{
    /*key*/
    uint32  nh_id;
    uint32  nh_type; /*sys_goldengate_nh_type_t*/
    uint32  calc_key_len[0];

    /*data*/
    sys_wb_nh_info_com_hdr_t hdr;
    union {
        sys_wb_nh_info_misc_t    misc;
        sys_wb_nh_info_trill_t   trill;
        sys_wb_nh_info_ip_tunnel_t ip_tunnel;
        sys_wb_nh_info_mpls_t    mpls;
        sys_wb_nh_info_ecmp_t    ecmp;
        sys_wb_nh_info_special_t spec;
        sys_wb_nh_info_rspan_t   rspan;
        sys_wb_nh_info_brguc_t   brguc;
        sys_wb_nh_info_ipuc_t    ipuc;
    }info;
};
typedef struct sys_wb_nh_info_com_s sys_wb_nh_info_com_t;


struct sys_wb_nh_master_s
{
    /*key*/
    uint32 lchip;   /*lchip*/
    uint32 calc_key_len[0];

    /**data */
    uint8      ipmc_logic_replication;
    uint8      ipmc_port_bitmap;
    uint8      no_dsfwd ;
    uint8      rspan_nh_in_use; /**<if set,rsapn nexthop have already used */
    uint16     cw_ref_cnt[SYS_WB_NH_CW_NUM];
    uint16     max_ecmp;               /**<  the maximum ECMP paths allowed for a route */
    uint16     cur_ecmp_cnt;
    uint16     rspan_vlan_id; /**<rsapn vlan id*/
	uint16     rsv0;
};
typedef struct sys_wb_nh_master_s sys_wb_nh_master_t;

struct sys_wb_nh_brguc_node_s
{
    /*key*/
    uint32 port_index;   /*lchip*/
    uint32 calc_key_len[0];

    /*data*/
    uint32 nhid_brguc;
    uint32 nhid_bypass;
};
typedef struct sys_wb_nh_brguc_node_s sys_wb_nh_brguc_node_t;


#ifdef __cplusplus
}
#endif

#endif

#ifndef _SYS_GREATBELT_NEXTHOP_H_
#define _SYS_GREATBELT_NEXTHOP_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_const.h"
#include "ctc_avl_tree.h"
#include "ctc_aps.h"
#include "ctc_vector.h"
#include "ctc_debug.h"
#include "ctc_vlan.h"
#include "ctc_nexthop.h"
#include "ctc_stats.h"

#define SYS_NH_EXTERNAL_NHID_MAX_BLK_NUM     16
#define SYS_NH_INTERNAL_NHID_MAX_BLK_NUM     16
#define SYS_NH_INTERNAL_NHID_BLK_SIZE        2048
#define SYS_NH_INTERNAL_NHID_MAX \
    ((p_gb_nh_master->max_external_nhid) + (SYS_NH_INTERNAL_NHID_MAX_BLK_NUM * SYS_NH_INTERNAL_NHID_BLK_SIZE))
#define SYS_NH_ENCODE_DESTMAP(is_mcast, gchip, port) (((is_mcast) << 21) | ((gchip) << 16) | (port))
#define SYS_NH_GET_VECTOR_INDEX_BY_NHID(nhid)  (nhid - p_gb_nh_master->max_external_nhid)
#define SYS_DSNH_DESTVLANPTR_SPECIAL 0
#define SYS_DSNH_INDEX_FOR_NONE 1
#define SYS_NH_ROUTED_PORT_VID    0xFFF

#define SYS_NH_TUNNEL_ID_MAX_BLK_NUM     16
#define SYS_NH_MCAST_ID_MAX_BLK_NUM     16
#define SYS_NH_ARP_ID_MAX_BLK_NUM       16
#define SYS_NH_DSNH_OFFSET_MODEROUTED_PORT_VID    0xFFF

#define SYS_NH_IGS_CHIP_EDIT_MODE  0
#define SYS_NH_EGS_CHIP_EDIT_MODE  1
#define SYS_NH_MISC_CHIP_EDIT_MODE 2

enum sys_nh_entry_table_type_e
{
    SYS_NH_ENTRY_TYPE_NULL,

    SYS_NH_ENTRY_TYPE_FWD = SYS_NH_ENTRY_TYPE_NULL,
    SYS_NH_ENTRY_TYPE_MET,
    SYS_NH_ENTRY_TYPE_MET_FOR_IPMCBITMAP,
    SYS_NH_ENTRY_TYPE_NEXTHOP_4W,
    SYS_NH_ENTRY_TYPE_NEXTHOP_8W,

    /* +++ l2edit +++ */
    SYS_NH_ENTRY_TYPE_L2EDIT_FROM,
    SYS_NH_ENTRY_TYPE_L2EDIT_ETH_4W = SYS_NH_ENTRY_TYPE_L2EDIT_FROM,
    SYS_NH_ENTRY_TYPE_L2EDIT_ETH_8W,

    SYS_NH_ENTRY_TYPE_L2EDIT_FLEX_8W,
    SYS_NH_ENTRY_TYPE_L2EDIT_LPBK,
    SYS_NH_ENTRY_TYPE_L2EDIT_PBB_4W,
    SYS_NH_ENTRY_TYPE_L2EDIT_PBB_8W,
    SYS_NH_ENTRY_TYPE_L2EDIT_SWAP,
    SYS_NH_ENTRY_TYPE_L2EDIT_TO = SYS_NH_ENTRY_TYPE_L2EDIT_SWAP,
    /* --- l2edit --- */

    /* +++ l3edit +++ */
    SYS_NH_ENTRY_TYPE_L3EDIT_FROM,
    SYS_NH_ENTRY_TYPE_L3EDIT_FLEX= SYS_NH_ENTRY_TYPE_L3EDIT_FROM,
    SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_4W,
    SYS_NH_ENTRY_TYPE_L3EDIT_MPLS_8W,
    SYS_NH_ENTRY_TYPE_L3EDIT_NAT_4W,
    SYS_NH_ENTRY_TYPE_L3EDIT_NAT_8W,
    SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4,
    SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6,
    SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V4_IPSA,
    SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6_IPSA,
    SYS_NH_ENTRY_TYPE_L3EDIT_TO = SYS_NH_ENTRY_TYPE_L3EDIT_TUNNEL_V6_IPSA,
    /* --- l3edit --- */
    SYS_NH_ENTRY_TYPE_MAX

};
typedef enum sys_nh_entry_table_type_e sys_nh_entry_table_type_t;

enum sys_nh_entry_pld_op_type_e
{
    SYS_NH_OP_NONE = 0,
    SYS_NH_OP_ROUTE,
    SYS_NH_OP_BRIDGE,
    SYS_NH_OP_BRIDGE_VPLS,             /* BRIDGE_VPLS_VC for SGMAC */
    SYS_NH_OP_BRIDGE_INNER,
    SYS_NH_OP_MIRROR,
    SYS_NH_OP_ROUTE_NOTTL,
    SYS_NH_OP_ROUTE_COMPACT   /*L2Edit merge to Nexthop ipuc/ipmc*/
};
typedef enum sys_nh_entry_pld_op_type_e sys_nh_entry_pld_op_type_t;

enum sys_nh_entry_l2edit_type_e
{
    SYS_NH_L2EDIT_TYPE_NONE =0,
    SYS_NH_L2EDIT_TYPE_LOOPBACK,
    SYS_NH_L2EDIT_TYPE_ETH_4W,
    SYS_NH_L2EDIT_TYPE_ETH_8W,
    SYS_NH_L2EDIT_TYPE_MAC_SWAP,
    SYS_NH_L2EDIT_TYPE_FLEX_8W,
    SYS_NH_L2EDIT_TYPE_PBB_4W,
    SYS_NH_L2EDIT_TYPE_PBB_8W,
    SYS_NH_L2EDIT_TYPE_INVALID,

    SYS_NH_L2EDIT_TYPE_MAX
};
typedef enum sys_nh_entry_l2edit_type_e sys_nh_entry_l2edit_type_t;

enum sys_nh_l2_edit_extra_header_type_e
{
    SYS_L2EDIT_EXTRA_HEADER_TYPE_NONE = 0,
    SYS_L2EDIT_EXTRA_HEADER_TYPE_ETHERNET,
    SYS_L2EDIT_EXTRA_HEADER_TYPE_MPLS,
    SYS_L2EDIT_EXTRA_HEADER_TYPE_NO_L2_EDIT
};
typedef enum sys_nh_l2_edit_extra_header_type_e sys_nh_l2_edit_extra_header_type_t;

enum sys_nh_ds_type_e
{
    SYS_NH_DS_TYPE_FWD = 0,
    SYS_NH_DS_TYPE_L2EDIT,
    SYS_NH_DS_TYPE_L3EDIT,
    SYS_NH_DS_TYPE_DISCARD,
    DS_TYPE_RESERVED
};
typedef enum sys_nh_ds_type_e sys_nh_ds_type_t;

enum sys_nh_entry_l3edit_type_e
{
    SYS_NH_L3EDIT_TYPE_NONE =0,
    SYS_NH_L3EDIT_TYPE_MPLS_4W,
    SYS_NH_L3EDIT_TYPE_MPLS_8W,
    SYS_NH_L3EDIT_TYPE_NAT_4W,
    SYS_NH_L3EDIT_TYPE_TUNNEL_V4,
    SYS_NH_L3EDIT_TYPE_TUNNEL_V6,
    SYS_NH_L3EDIT_TYPE_FLEX,
    SYS_NH_L3EDIT_TYPE_NAT_8W,
    SYS_NH_L3EDIT_TYPE_LOOPBACK,
    SYS_NH_L3EDIT_TYPE_RESERVED,

    SYS_NH_L3EDIT_TYPE_MAX
};
typedef enum sys_nh_entry_l3edit_type_e sys_nh_entry_l3edit_type_t;

enum sys_greatbelt_nh_res_offset_type_e
{
    SYS_NH_RES_OFFSET_TYPE_NONE = 0,

    SYS_NH_RES_OFFSET_TYPE_BRIDGE_NH = SYS_NH_RES_OFFSET_TYPE_NONE,  /*port tagged mode*/
    SYS_NH_RES_OFFSET_TYPE_BYPASS_NH,
    SYS_NH_RES_OFFSET_TYPE_MIRROR_NH,
    SYS_NH_RES_OFFSET_TYPE_OAM_BRIDGE_NH,
    SYS_NH_RES_OFFSET_TYPE_MAX
};
typedef enum sys_greatbelt_nh_res_offset_type_e sys_greatbelt_nh_res_offset_type_t;

struct sys_nh_offset_attr_s
{
    uint16 offset_base;
    uint16 entry_size;
    uint16 entry_num;
};
typedef struct sys_nh_offset_attr_s sys_nh_offset_attr_t;

struct sys_nh_table_entry_info_s
{
    uint32 table_id;
    uint8  entry_size;
    uint8  opf_pool_type;
    uint8  alloc_dir; /*0:forward  alloc ,1,reverse alloc*/
    uint8  table_8w;
};
typedef struct sys_nh_table_entry_info_s sys_nh_table_entry_info_t;

enum sys_greatbelt_nh_type_e
{
    SYS_NH_TYPE_NULL,
    SYS_NH_TYPE_MCAST,
    SYS_NH_TYPE_BRGUC,
    SYS_NH_TYPE_IPUC,
    SYS_NH_TYPE_MPLS,
    SYS_NH_TYPE_ECMP, /*For IPUC, MPLS, etc*/
    SYS_NH_TYPE_DROP,
    SYS_NH_TYPE_TOCPU,
    SYS_NH_TYPE_UNROV,
    SYS_NH_TYPE_ILOOP,
    SYS_NH_TYPE_ELOOP,
    SYS_NH_TYPE_RSPAN,
    SYS_NH_TYPE_MISC,
    SYS_NH_TYPE_IP_TUNNEL, /*Added by IP Tunnel*/
    SYS_NH_TYPE_MAX
};
typedef enum sys_greatbelt_nh_type_e sys_greatbelt_nh_type_t;

enum sys_greatbelt_nh_param_type_e
{
    SYS_NH_PARAM_DSNH_TYPE_IPUC,
    SYS_NH_PARAM_DSNH_TYPE_BRGUC,
    SYS_NH_PARAM_DSNH_TYPE_BYPASS,
    SYS_NH_PARAM_DSNH_TYPE_IPMC,
    SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_NONE,
    SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_ROUTE,
    SYS_NH_PARAM_DSNH_TYPE_MPLS_PUSH_OP_L2VPN,
    SYS_NH_PARAM_DSNH_TYPE_MPLS_PHP,
    SYS_NH_PARAM_DSNH_TYPE_RSPAN,
    SYS_NH_PARAM_DSNH_TYPE_MPLS_OP_NONE,
    SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL,
    SYS_NH_PARAM_DSNH_TYPE_IPTUNNEL_FOR_MIRROR,
    MAX_SYS_NH_PARAM_DSNH_TYPE
}
;
typedef enum sys_greatbelt_nh_param_type_e sys_greatbelt_nh_param_type_t;

enum sys_greatbelt_nh_param_flag_e
{
    SYS_NH_PARAM_FLAG_USE_TTL_FROM_PKT      = 0x0001,    /*PIPE/uniform  .uniform:SYS_NH_PARAM_FLAG_USE_TTL_FROM_PKT  =1*/
    SYS_NH_PARAM_FLAG_USE_MAPPED_STAG_COS   = 0x0002,   /*if set, cos will use qos mapped  cos,else use packet parsered cos or user defined */
    SYS_NH_PARAM_FLAG_USE_MAPPED_CTAG_COS   = 0x0004,   /*if set, dscp will use qos mapped  cos,else use packet parsered cos */
    SYS_NH_PARAM_FLAG_USE_MAPPED_DSCP       = 0x0008,     /*if set, dscp will use qos mapped  dscp,else use packet parsered dscp */
    SYS_NH_PARAM_FLAG_VPLS_NEXTHOP          = 0x0010,
    SYS_NH_PARAM_FLAG_LOGIC_PORT_CHECK      = 0x0020,
    SYS_NH_PARAM_FLAG_L2EDIT_AS_APS_GROUP   = 0x0040,
    SYS_NH_PARAM_FLAG_SWAP_MAC              = 0x0080,
    SYS_NH_PARAM_FLAG_IP_TUNNEL_PLD_BRIDGE  = 0x0100,
    SYS_NH_PARAM_FLAG_PHP_SHORT_PIPE        = 0x0200,    /*PHP Shore Pipe*/
    SYS_NH_PARAM_FLAG_PW_BFD_PLD_ROUTE      = 0x0400,
    SYS_NH_PARAM_FLAG_STATS_EN              = 0x0800,
    SYS_NH_PARAM_FLAG_ROUTE_NOTTL           = 0x1000,
    MAX_SYS_NH_PARAM_FLAG
};
typedef enum sys_greatbelt_nh_param_flag_e sys_greatbelt_nh_param_flag_t;

struct sys_nh_param_dsnh_s
{
    uint32 dsnh_offset;
    uint32 l2edit_ptr;
    uint32 l3edit_ptr;
    uint16 stats_ptr;
    uint8 mtu_no_chk;
    uint8  rsv[1];
    ctc_vlan_egress_edit_info_t* p_vlan_info;
    uint16  dest_vlan_ptr;

    uint8   dsnh_type;    /*sys_greatbelt_nh_param_type_t*/
    uint16  flag;
    uint8   use_ttl_from_pkt;
    uint8   hvpls;
    uint8   logic_port_check;
    uint8   dscp_en;
    uint16 logic_port;
    mac_addr_t macDa;        /**< 802.3 MAC address */
};
typedef struct sys_nh_param_dsnh_s sys_nh_param_dsnh_t;
struct sys_nh_param_dsfwd_s
{
    uint32 dsfwd_offset;
    uint32 dsnh_offset;
    uint8 is_mcast;
    uint8 is_reflective;
    uint8 drop_pkt;
    uint8 nexthop_ext;
    uint8 service_queue_en;
    uint8 sequence_chk_en;
    uint8 length_adjust_type;
    ctc_aps_type_t aps_type;
    uint16 dest_id; /*UcastId or McastId*/
    uint8 dest_chipid; /*Destination global chip id*/
     /*This entry's target local chipid*/
    uint32 stats_ptr; /*This entry's target local chipid*/
    uint8  is_to_cpu;   /*ingress edit mode,bypass ingress edit, and to cpu should use qsel*/
    uint32 dest_map;
    uint8  bypass_ingress_edit;
    uint8 lport_en;
};
typedef struct sys_nh_param_dsfwd_s sys_nh_param_dsfwd_t;
struct sys_nh_param_hdr_s
{
    sys_greatbelt_nh_type_t nh_param_type;
    sys_nh_entry_change_type_t change_type;
    uint32 nhid;
    uint32 stats_id;
    uint8  is_internal_nh;
    uint8  have_dsfwd;
    uint8  stats_valid;
    uint16   stats_ptr;

};
typedef struct sys_nh_param_hdr_s sys_nh_param_hdr_t;

struct sys_nh_param_com_s
{
    sys_nh_param_hdr_t hdr;
};
typedef struct sys_nh_param_com_s sys_nh_param_com_t;

struct sys_nh_param_special_s
{
    sys_nh_param_hdr_t hdr;
};
typedef struct sys_nh_param_special_s sys_nh_param_special_t;

struct sys_nh_param_brguc_s
{
    sys_nh_param_hdr_t hdr;
    sys_nh_param_brguc_sub_type_t nh_sub_type;
    ctc_vlan_egress_edit_info_t* p_vlan_edit_info;
    ctc_vlan_egress_edit_info_t* p_vlan_edit_info_prot_path;
    uint32 dsnh_offset; /*Used for vlan editing*/
    uint16 gport; /*gport or aps bridge group id*/
    uint16 dest_vlan_ptr;
    uint32 loop_nhid;
    uint32    fwd_offset;
    ctc_vlan_edit_nh_param_t* p_vlan_edit_nh_param;
};
typedef struct sys_nh_param_brguc_s sys_nh_param_brguc_t;

enum sys_nh_param_mcast_opcode_e
{
    SYS_NH_PARAM_MCAST_ADD_MEMBER            = 1,
    SYS_NH_PARAM_MCAST_DEL_MEMBER            = 2
};
typedef enum sys_nh_param_mcast_opcode_e sys_nh_param_mcast_opcode_t;

struct sys_nh_param_mcast_s
{
    sys_nh_param_hdr_t hdr;
    uint32 groupid;
    uint8 opcode;
    uint8 is_mirror;
    uint32             fwd_offset;
    sys_nh_param_mcast_member_t* p_member;

};
typedef struct sys_nh_param_mcast_s sys_nh_param_mcast_t;

struct sys_nh_param_ipuc_s
{
    sys_nh_param_hdr_t hdr;
    mac_addr_t mac;
    mac_addr_t p_mac;
    uint16 dsnh_offset;
    uint16   aps_bridge_group_id;
    bool   is_unrov_nh;

    bool   is_dsl2edit;
    uint8   aps_en;
    uint8   ttl_no_dec;
    uint8   strip_l3;
    uint8 mtu_no_chk;
    ctc_nh_oif_info_t oif;
    ctc_nh_oif_info_t p_oif;
    uint16 arp_id;
    uint16 p_arp_id;
    uint32 loop_nhid;
    uint8  merge_dsfwd;
};
typedef struct sys_nh_param_ipuc_s sys_nh_param_ipuc_t;

struct sys_nh_param_ecmp_s
{
    sys_nh_param_hdr_t hdr;
    ctc_nh_ecmp_nh_param_t* p_ecmp_param;
};
typedef struct sys_nh_param_ecmp_s sys_nh_param_ecmp_t;

struct sys_nh_param_mpls_s
{
    sys_nh_param_hdr_t hdr;
    ctc_mpls_nexthop_param_t* p_mpls_nh_param;
};
typedef struct sys_nh_param_mpls_s sys_nh_param_mpls_t;

/* IP Tunnel nexthop info*/
struct sys_nh_param_ip_tunnel_s
{
    sys_nh_param_hdr_t hdr;
    ctc_ip_tunnel_nh_param_t* p_ip_nh_param;
};
typedef struct sys_nh_param_ip_tunnel_s sys_nh_param_ip_tunnel_t;
struct sys_nh_param_iloop_s
{
    sys_nh_param_hdr_t hdr;
    ctc_loopback_nexthop_param_t* p_iloop_param;
};
typedef struct sys_nh_param_iloop_s sys_nh_param_iloop_t;

struct sys_nh_param_rspan_s
{
    sys_nh_param_hdr_t hdr;
    ctc_rspan_nexthop_param_t* p_rspan_param;
    uint32 dsnh_offset;
};
typedef struct sys_nh_param_rspan_s sys_nh_param_rspan_t;

struct sys_nh_param_crscnt_s
{
    bool swap_mac;
    uint16 srcport;
    uint16 destport;
};
typedef struct sys_nh_param_crscnt_s sys_nh_param_crscnt_t;

struct sys_nh_param_misc_s
{

    sys_nh_param_hdr_t hdr;
    uint8 is_dsfwd;

    ctc_misc_nh_param_t* p_misc_param;
};
typedef struct sys_nh_param_misc_s sys_nh_param_misc_t;

/*=== DB  entry struct  ===*/
struct sys_nh_db_dsl2editeth4w_s
{
    mac_addr_t mac_da;
    uint32 offset;
    uint8 phy_if;

    uint16 ref_cnt;

    uint16 output_vid;
    uint8  output_vlan_is_svlan;
    uint8  packet_type;

    uint32 nh_id;
};
typedef struct sys_nh_db_dsl2editeth4w_s sys_nh_db_dsl2editeth4w_t;

struct sys_nh_db_dsl2editeth8w_s
{
    mac_addr_t mac_da;
    uint32 offset;
    uint8 phy_if;

    uint16 ref_cnt;

    uint16 output_vid;
    uint8 output_vlan_is_svlan;
    uint8 packet_type;

    uint32 ip0_31;
    uint32 ip32_63;

    uint16 ip64_79;
};
typedef struct sys_nh_db_dsl2editeth8w_s sys_nh_db_dsl2editeth8w_t;


enum sys_nh_arp_flag_e
{
    SYS_NH_ARP_FLAG_DROP                =  0x01,
    SYS_NH_ARP_FLAG_REDIRECT_TO_CPU     =  0x02,
    SYS_NH_ARP_VLAN_VALID               =  0x04,
    SYS_NH_ARP_MAX
};
typedef enum sys_nh_arp_flag_e sys_nh_arp_flag_t;


struct sys_nh_db_arp_s
{
    mac_addr_t mac_da;
    uint8 flag;

    uint32 offset;
    uint16 ref_cnt;  /*sys_nh_arp_flag_t,SYS_NH_ARP_FLAG_XX*/

    uint16 output_vid;
    uint8  output_vlan_is_svlan;
    uint8  packet_type;
};
typedef struct sys_nh_db_arp_s sys_nh_db_arp_t;
/**
 @brief  Define flex nexthop parameter structure
*/
struct sys_misc_nh_edit_param_s
{
    uint32       flag;                          /**< [HB.GB] CTC_MISC_NH_L2_EDIT_XXX */
    mac_addr_t   mac_da;                        /**< [HB.GB] Mac destination address */
    mac_addr_t   mac_sa;                        /**< [HB.GB] Mac source address */

    uint16       dst_port;                      /**< [HB.GB] dst-port */
    uint16       stats_ptr;                        /**< [HB.GB] reserved */
    uint8        is_reflective;
    uint32       ipda;                          /**< [HB.GB] ipda */
    ctc_vlan_egress_edit_info_t vlan_edit_info;   /**< [GB] while egress valn nexthop : Egress vlan edit information ; while ASP Egress Vlan Editing nexthop : Egress vlan edit information of working path*/
};
typedef struct sys_misc_nh_edit_param_s sys_misc_nh_edit_param_t;

enum sys_nh_info_flag_type_e
{
    SYS_NH_INFO_FLAG_IS_UNROV = 0x0001,
    SYS_NH_INFO_FLAG_USE_DSNH8W = 0x0002,
    SYS_NH_INFO_FLAG_IS_APS_EN = 0x0004,
    SYS_NH_INFO_FLAG_HAVE_DSFWD = 0x0008,
    SYS_NH_INFO_FLAG_STATS_VALID = 0x0010,      /* not used now */
    SYS_NH_INFO_FLAG_HAVE_L2EDIT = 0x0020,
    SYS_NH_INFO_FLAG_HAVE_L3EDIT = 0x0040,
    SYS_NH_INFO_FLAG_HAVE_INT_ALLOC_DSNH = 0x0080,
    SYS_NH_INFO_FLAG_MPLS_POP_NH = 0x0100,
    SYS_NH_INFO_FLAG_IS_APS_SHARE_DSNH = 0x0200,
    SYS_NH_INFO_FLAG_HAVE_RSV_DSNH = 0x0400,
    SYS_NH_INFO_FLAG_IS_DISCARD = 0x0800,
    SYS_NH_INFO_FLAG_LOGIC_PORT = 0x1000,
    SYS_NH_INFO_FLAG_HORIZON_SPLIT_EN = 0x2000,
    SYS_NH_INFO_FLAG_LENGTH_ADJUST_EN = 0x4000,
    SYS_NH_INFO_FLAG_HAVE_RSV_DSFWD = 0x8000,
    SYS_NH_INFO_FLAG_EGRESS_EDIT_MODE = 0x10000,
    SYS_NH_INFO_FLAG_LOGIC_REPLI_EN = 0x20000
};
typedef enum sys_nh_info_flag_type_e sys_nh_info_flag_type_t;

struct sys_nh_info_dsfwd_s
{
    uint32 dsfwd_offset;
    uint16 stats_ptr;
    uint16 dsnh_offset;

};
typedef struct sys_nh_info_dsfwd_s sys_nh_info_dsfwd_t;

struct sys_nh_ref_list_node_s
{
    struct sys_nh_info_com_s* p_ref_nhinfo;
    struct sys_nh_ref_list_node_s* p_next;
};
typedef struct sys_nh_ref_list_node_s sys_nh_ref_list_node_t;

struct sys_nh_info_com_hdr_s
{
    uint8 nh_entry_type; /*sys_greatbelt_nh_type_t*/
    uint8 dsnh_entry_num;
    uint8 rsv[2];
    uint32 nh_entry_flags; /*sys_nh_info_flag_type_t*/
    sys_nh_info_dsfwd_t dsfwd_info;
    sys_nh_ref_list_node_t* p_ref_nh_list;   /*sys_nh_ref_list_node_t*/
    void* p_data;
};
typedef struct sys_nh_info_com_hdr_s sys_nh_info_com_hdr_t;

struct sys_nh_info_com_s
{
    sys_nh_info_com_hdr_t hdr;
    uint32 data;
};
typedef struct sys_nh_info_com_s sys_nh_info_com_t;

struct sys_nh_info_special_s
{
    sys_nh_info_com_hdr_t hdr;
    uint16 dest_gport;
    uint8 rsv[2];
};
typedef struct sys_nh_info_special_s sys_nh_info_special_t;

struct sys_nh_info_rspan_s
{
    sys_nh_info_com_hdr_t hdr;
    uint8 remote_chip;
    uint8 rsv[3];
};
typedef struct sys_nh_info_rspan_s sys_nh_info_rspan_t;

struct sys_nh_info_brguc_s
{
    sys_nh_info_com_hdr_t hdr;
    union
    {
        uint16 dest_gport;
        uint16 aps_bridge_group_id;
    } dest;
    uint8 nh_sub_type; /*sys_nh_param_brguc_sub_type_t*/
    uint8 rsv0;
    uint32 loop_nhid;
    uint16 dest_logic_port;
    uint16 rsv1;
};
typedef struct sys_nh_info_brguc_s sys_nh_info_brguc_t;

struct sys_nh_info_ipuc_edit_info_s
{
    uint16  l3ifid;
    uint16  arp_id;
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit;
};
typedef struct sys_nh_info_ipuc_edit_info_s sys_nh_info_ipuc_edit_info_t;


enum sys_nh_ipuc_flag_e
{
    SYS_NH_IPUC_FLAG_PW_BFD_PLD_ROUTE   = 0x0001,
    SYS_NH_IPUC_FLAG_LOOP_NH            = 0x0002,
    SYS_NH_IPUC_FLAG_STATS_EN           = 0x0004,
    SYS_NH_IPUC_FLAG_MERGE_DSFWD        = 0x0008
};
typedef enum sys_nh_ipuc_flag_e sys_nh_ipuc_flag_t;

struct sys_nh_info_ipuc_s
{
    sys_nh_info_com_hdr_t             hdr;
    uint16  l3ifid;
    uint8 rsv[2];
    uint32  gport;
    uint16  flag;         /*sys_nh_ipuc_flag_t*/
    uint16  arp_id;

    uint32  stats_id;
    sys_nh_info_ipuc_edit_info_t   *protection_path;
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit;
    void* data;
    uint32 (* updateAd) (uint8 lchip, void* ipuc_node, void* dsnh_info);
    uint32 loop_nhid;

};
typedef struct sys_nh_info_ipuc_s sys_nh_info_ipuc_t;

enum sys_nh_dsmet_flag_e
{
    SYS_NH_DSMET_FLAG_IS_LINKAGG              =  0x0001,
    SYS_NH_DSMET_FLAG_ENDLOCAL                =  0x0002,
    SYS_NH_DSMET_FLAG_PORT_CHK_DISCARD        =  0x0004,
    SYS_NH_DSMET_FLAG_LOGIC_PORT_CHK          =  0x0008,
    SYS_NH_DSMET_FLAG_USE_DSNH8W              =  0x0010,
    SYS_NH_DSMET_FLAG_USE_PBM                 =  0x0020,
    SYS_NH_DSMET_FLAG_USE_RSV_NH              =  0x0040,
    SYS_NH_DSMET_FLAG_APS_SHARE_NH            =  0x0080,
    SYS_NH_DSMET_FLAG_HORIZON_SPLIT_EN        =  0x0100,
    SYS_NH_DSMET_FLAG_MAX
};
typedef enum sys_nh_dsmet_flag_e sys_nh_dsmet_flag_t;

struct sys_nh_info_dsmet_s
{
    uint16   flag; /*sys_nh_dsmet_flag_t,SYS_NH_DSMET_FLAG_XX*/
    uint16   logic_port;
    uint16   dsmet_offset;
    uint16   next_dsmet_offset;
    uint16   dsnh_offset;
    uint16   ref_nhid;
    uint16   ucastid;
    uint16   vid; /*For IPMC*/
    uint8    replicate_num; /*replicate_num for IPMC; port num of l2mc port bitmap*/
    uint8    member_type;
    uint16* vid_list;
    uint8    free_dsnh_offset_cnt;
    uint32  pbm0;
    uint32  pbm1;
};
typedef struct sys_nh_info_dsmet_s sys_nh_info_dsmet_t;
struct sys_nh_mcast_meminfo_s
{
    ctc_list_pointer_node_t list_head;
    sys_nh_info_dsmet_t dsmet;
};
typedef struct sys_nh_mcast_meminfo_s sys_nh_mcast_meminfo_t;

struct sys_nh_info_mcast_s
{
    sys_nh_info_com_hdr_t hdr;
    uint32 basic_met_offset;
    uint16  nhid;
    uint32  physical_replication_num : 8;
    uint32  mirror_ref_cnt           : 8;
    uint32  is_mirror                : 1;
    uint32  is_logic_rep_en          : 1;
    uint32  rsv                      : 14;
    uint8  first_phy_if_ucastid;    /* the first phyif ucast id in ipmc*/
    uint8  first_phy_if_is_linkagg; /* the first phyif ucast id in ipmc*/
    uint16 stacking_met_offset;
    uint16 stacking_mcast_profile_id;
    ctc_list_pointer_t   p_mem_list; /*sys_nh_mcast_meminfo_t*/
};
typedef struct sys_nh_info_mcast_s sys_nh_info_mcast_t;

struct sys_nh_info_ecmp_s
{
    sys_nh_info_com_hdr_t hdr;
    uint8          ecmp_cnt;    /*include unresoved nh member*/
    uint8          valid_cnt;   /*valid member*/
    uint8          is_dlb;
    uint8          mem_num;  /*max member num in group*/
    uint32         ecmp_nh_id;
    uint32* nh_array;
};
typedef struct sys_nh_info_ecmp_s sys_nh_info_ecmp_t;

enum sys_nh_mpls_tunnel_flag_e
{
    SYS_NH_MPLS_TUNNEL_FLAG_STATS          =  0x01,
    SYS_NH_MPLS_TUNNEL_FLAG_APS  =  0x02,
    SYS_NH_MPLS_TUNNEL_FLAG_TWO_LAYER_APS  =  0x04,
    SYS_NH_MPLS_TUNNEL_FLAG_WORKING  =  0x08,
    SYS_NH_MPLS_TUNNEL_FLAG_PROTECTION  =  0x10,
    SYS_NH_MPLS_TUNNEL_FLAG_MAX
};

struct sys_nh_db_mpls_tunnel_s
{
    uint16  gport; /*if SYS_NH_MPLS_TUNNEL_FLAG_APS is set, it is aps group id*/
    uint16  l3ifid;
    uint16  tunnel_id;
    uint8   flag; /*sys_nh_dsmet_flag_t,SYS_NH_MPLS_TUNNEL_FLAG_XX*/
    uint16  ref_cnt;
    uint16  dsl2edit_offset;
    uint16  p_dsl2edit_offset;
    uint16  stats_ptr;
    uint16  p_stats_ptr;
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit;
    sys_nh_db_dsl2editeth4w_t* p_p_dsl2edit;
};
typedef struct sys_nh_db_mpls_tunnel_s sys_nh_db_mpls_tunnel_t;

struct sys_nh_info_mpls_edit_info_s
{
    sys_nh_db_mpls_tunnel_t* p_mpls_tunnel;
    sys_nh_db_mpls_tunnel_t* p_protect_mpls_tunnel;
    uint16 dsl3edit_offset;
    uint16 stats_ptr;
};
typedef struct sys_nh_info_mpls_edit_info_s sys_nh_info_mpls_edit_info_t;

struct sys_nh_info_mpls_s
{
    sys_nh_info_com_hdr_t hdr;
    uint16   aps_group_id;
    uint16   gport;    /*only apply to pop nexthop*/
    uint16   l3ifid;   /*only apply to pop nexthop*/
    uint16   dest_logic_port; /*H-vpls*/
    sys_nh_info_mpls_edit_info_t working_path;
    sys_nh_info_mpls_edit_info_t* protection_path;  /*when pw aps enbale*/
    sys_nh_db_dsl2editeth4w_t* p_dsl2edit;
};
typedef struct sys_nh_info_mpls_s sys_nh_info_mpls_t;

enum sys_nh_ip_tunnel_flag_e
{
    SYS_NH_IP_TUNNEL_FLAG_IN_V4       =  0x0001,
    SYS_NH_IP_TUNNEL_FLAG_IN_V6       =  0x0002,
    SYS_NH_IP_TUNNEL_FLAG_IVI_6TO4    =  0x0004,
    SYS_NH_IP_TUNNEL_FLAG_IVI_4TO6    =  0x0008,
    SYS_NH_IP_TUNNEL_FLAG_NAT_V4      =  0x0010,
    SYS_NH_IP_TUNNEL_FLAG_L2EDIT8W    =  0x0020,
    SYS_NH_IP_TUNNEL_REROUTE          =  0x0040,
    SYS_NH_IP_TUNNEL_LOOP_NH          =  0x0080,

    SYS_NH_IP_TUNNEL_FLAG_MAX
};
typedef enum sys_nh_ip_tunnel_flag_e sys_nh_ip_tunnel_flag_t;


struct sys_nh_info_ip_tunnel_s
{
    sys_nh_info_com_hdr_t hdr;
    mac_addr_t mac;
    uint32 ip0_31;
    uint32 ip32_63;
    uint16 l3ifid;
    uint16 gport;
    uint32 dsl3edit_offset;

    uint16 ip64_79;
    uint8  flag; /* sys_nh_ip_tunnel_flag_t */
    uint8 sa_index;

    uint8 phy_if;
    uint8 packet_type;
    uint8 rsv[2];

    uint32 loop_nhid;
};
typedef struct sys_nh_info_ip_tunnel_s sys_nh_info_ip_tunnel_t;

struct sys_nh_info_misc_s
{
    sys_nh_info_com_hdr_t hdr;
    uint32 dsl2edit_offset;
    uint32 dsl3edit_offset;
    uint32 gport;
    uint8  is_swap_mac;
    uint8  is_reflective;
    uint8 resv;
    uint8 stats_valid;
    uint32 stats_id;
    ctc_misc_nh_type_t misc_nh_type;
};
typedef struct sys_nh_info_misc_s sys_nh_info_misc_t;


typedef int32 (* p_sys_nh_create_cb_t)(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, \
                                       sys_nh_info_com_t* p_com_db);
typedef int32 (* p_sys_nh_delete_cb_t)(uint8 lchip, sys_nh_info_com_t* p_data, \
                                       uint32* p_nhid);
typedef int32 (* p_sys_nh_update_cb_t)(uint8 lchip, sys_nh_info_com_t* p_data, \
                                       sys_nh_param_com_t* p_com_nh_para);

struct sys_greatbelt_nh_master_s
{
    sal_mutex_t*         p_mutex;
    p_sys_nh_create_cb_t callbacks_nh_create[SYS_NH_TYPE_MAX];
    p_sys_nh_delete_cb_t callbacks_nh_delete[SYS_NH_TYPE_MAX];
    p_sys_nh_update_cb_t callbacks_nh_update[SYS_NH_TYPE_MAX];
    uint32 max_external_nhid;
    uint16 max_tunnel_id;
    uint16 max_arp_id;
    ctc_vector_t* external_nhid_vec;
    sys_nh_offset_attr_t sys_nh_resolved_offset[SYS_NH_RES_OFFSET_TYPE_MAX];
    uint16         ipmc_dsnh_offset[SYS_GB_MAX_CTC_L3IF_ID + 1];
    uint16         ipmc_resolved_l2edit;
    uint16         reflective_resolved_dsfwd_offset;
    uint16         ipmc_bitmap_met_num;
    uint16         ipmc_bitmap_nh_base;
    ctc_avl_tree_t*   dsl2edit4w_tree;
    ctc_avl_tree_t*   dsl2edit8w_tree;
    ctc_avl_tree_t*   mpls_tunnel_tree;
    ctc_vector_t*     internal_nhid_vec;
    ctc_vector_t*     tunnel_id_vec;
    ctc_vector_t*     mcast_nhid_vec;
    ctc_vector_t*     arp_id_vec;
    ctc_slist_t*      eloop_list;
    ctc_slist_t*      repli_offset_list;

    uint32 fatal_excp_base;
    uint32     max_glb_nh_offset;
    uint32     max_glb_met_offset;
    uint32     glb_nh_used_cnt;
    uint32     glb_met_used_cnt;
    uint32* p_occupid_nh_offset_bmp;       /*Occupid global DsNexthop  offset bitmap, */
    uint32* p_occupid_met_offset_bmp;      /*Occupid global DsMET  offset bitmap, */

    uint8      ipmc_logic_replication;
    uint8      ipmc_port_bitmap;
    uint8      pkt_nh_edit_mode;   /*sys_nh_edit_mode_t*/

    uint8      ecmp_mode;           /**<if set,DsIPDa/DsMPLS's equal_cost_path_num should be use actual member number of a ECMP group ,
                                                    so user must update DsIPDa/DsMPLS when ecmp member change by call ctc_ipuc_add() and ctc_mpls_update_ilm().
                                                    in this case(case 1), ecmp should be in balance ;
                                                    else DsIPDa/DsMPLS's equal_cost_path_num is fixed (configured max_ecmp),ecmp members of the changes will
                                                    not affect the DsIPDa/DsMPLS,but in this case(case 2),ecmp may not be in balance.
                                                    <By default: it use case 2. >*/
    uint32     acl_redirect_fwd_ptr_num;    /**<The number of ds_fwd_ptr reserved for acl redirect function*/

    uint16     max_ecmp;               /**<  the maximum ECMP paths allowed for a route */
    uint16     cur_ecmp_cnt;
    uint8      rspan_nh_in_use; /**<if set,rsapn nexthop have already used */
    uint8      reflective_brg_en;
    uint8      ip_use_l2edit;
    uint8      no_dsfwd;

    uint32     xlate_nh_offset;  /**< xgpon resevr nexhtop offset base */

    uint16     rspan_vlan_id; /**<rsapn vlan id*/
    uint16     nhid_used_cnt[SYS_NH_TYPE_MAX];
    uint16     nhtbl_used_cnt[SYS_NH_ENTRY_TYPE_MAX];


};
typedef struct sys_greatbelt_nh_master_s sys_greatbelt_nh_master_t;

#define SYS_NH_CREAT_LOCK(mutex_type)                   \
    do                                                  \
    {                                                   \
        sal_mutex_create(&mutex_type);                   \
        if (NULL == mutex_type){                         \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX); }  \
    } while (0)

#define SYS_NH_LOCK(mutex_type) \
    sal_mutex_lock(mutex_type)

#define SYS_NH_UNLOCK(mutex_type) \
    sal_mutex_unlock(mutex_type)

#define SYS_NH_DESTROY_LOCK(mutex_type) \
    sal_mutex_destroy(mutex_type)

#define SYS_NH_EDIT_MODE()   p_gb_nh_master[lchip]->pkt_nh_edit_mode

#define SYS_NH_DBG_OUT(level, FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT(nexthop, nexthop, NH_SYS, level, FMT, ##__VA_ARGS__);  \
    } while (0)

/*
   1. DsFwd.nexthopPtr[15]: If set, CustomerId is valid  & bytes_to_remove ;
   2. DsFwd.nexthopPtr[10]: vpls port type;
   3. DsFwd.nexthopPtr[9:8]:bytes_to_remove; if CustomerId is valid & cm_packet_header.next_hop_ex
   4. DsFwd.nexthopPtr[6:0]: Used to store loopback localPhyPort;
*/
#define SYS_NH_ENCODE_ILOOP_DSNH(lport, logic_port, cid_valid, remove_words) \
    ((lport & 0x7F) | ((remove_words & 0x3) << 8) | (logic_port << 10) | (cid_valid << 14))

extern sys_greatbelt_nh_master_t* p_gb_nh_master[CTC_MAX_LOCAL_CHIP_NUM];

extern int32
sys_greatbelt_nh_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg);

extern int32
sys_greatbelt_nh_deinit(uint8 lchip);

extern int32
sys_greatbelt_nh_write_asic_table(uint8 lchip,
                                  sys_nh_entry_table_type_t table_type, uint32 offset, void* value);
extern int32
sys_greatbelt_nh_write_entry_dsnh4w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param);
extern int32
sys_greatbelt_nh_write_entry_dsnh8w(uint8 lchip, sys_nh_param_dsnh_t* p_dsnh_param);

extern int32
sys_greatbelt_nh_write_entry_dsfwd(uint8 lchip, sys_nh_param_dsfwd_t* p_dsfwd_param);

extern int32
sys_greatbelt_nh_update_dsfwd_speed(uint8 lchip, uint32 offset_array, uint8 speed);

extern int32
sys_greatbelt_nh_update_dsfwd_lport_en(uint8 lchip, uint32 fwd_offset, uint8 lport_en);

extern int32
sys_greatbelt_nh_update_entry_dsfwd(uint8 lchip, uint32 *offset,
                                                 uint32 dest_map,uint32 dsnh_offset, uint8 dsnh_8w, uint8 del, uint8 lport_en);

extern int32
sys_greatbelt_nh_check_max_glb_met_offset(uint8 lchip, uint32 offset);

extern int32
sys_greatbelt_nh_check_max_glb_nh_offset(uint8 lchip, uint32 offset);

extern int32
sys_greatbelt_nh_get_max_glb_nh_offset(uint8 lchip, uint32* offset);

extern int32
sys_greatbelt_nh_set_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set);

extern int32
sys_greatbelt_nh_set_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num, bool is_set);

extern int32
sys_greatbelt_nh_check_glb_met_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                      bool should_not_inuse);

extern int32
sys_greatbelt_nh_check_glb_nh_offset(uint8 lchip, uint32 start_offset, uint32 entry_num,
                                     bool should_not_inuse);

extern int32
sys_greatbelt_nh_get_resolved_offset(uint8 lchip, sys_greatbelt_nh_res_offset_type_t type,
                                     uint32* p_offset);
extern int32
sys_greatbelt_nh_get_reflective_dsfwd_offset(uint8 lchip, uint16* p_dsfwd_offset);

extern int32
sys_greatbelt_nh_get_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid, uint8 mtu_no_chk, uint32* p_dsnh_offset);
extern int32
sys_greatbelt_nh_del_ipmc_dsnh_offset(uint8 lchip, uint16 l3ifid);
extern int32
sys_greatbelt_nh_get_nhinfo_by_nhid(uint8 lchip, uint32 nhid, sys_nh_info_com_t** pp_nhinfo);
extern uint8
sys_greatbelt_nh_get_nh_valid(uint8 lchip, uint32 nhid);
extern int32
sys_greatbelt_nh_get_mirror_info_by_nhid(uint8 lchip, uint32 nhid, uint16* dsnh_offset, uint32* gport, bool enable);
extern int32
sys_greatbelt_nh_get_fatal_excp_dsnh_offset(uint8 lchip, uint32* p_offset);

extern int32
sys_greatbelt_nh_global_dync_entry_set_default(uint8 lchip, uint32 min_offset, uint32 max_offset);
extern int32
sys_greatbelt_nh_offset_alloc(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                              uint32 entry_num, uint32* p_offset);
extern int32
sys_greatbelt_nh_offset_alloc_with_multiple(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                            uint32 entry_num, uint16 multi, uint32* p_offset);
extern int32
sys_greatbelt_nh_reverse_offset_alloc(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                      uint32 entry_num, uint32* p_offset);
extern int32
sys_greatbelt_nh_reverse_offset_alloc_with_multiple(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                                                    uint32 entry_num, uint16 multi, uint32* p_offset);

extern int32
sys_greatbelt_nh_offset_free(uint8 lchip, sys_nh_entry_table_type_t entry_type,
                             uint32 entry_num, uint32 offset);
extern int32
sys_greatbelt_nh_create_brguc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_brguc_cb(uint8 lchip, sys_nh_info_com_t* data, uint32* p_nhid);

extern int32
sys_greatbelt_nh_create_downmep_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                   sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_downmep_cb(uint8 lchip, sys_nh_info_com_t* data);

extern int32
sys_greatbelt_nh_create_mcast_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_mcast_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);
extern int32
sys_greatbelt_nh_update_mcast_cb(uint8 lchip, sys_nh_info_com_t* p_nh_info,
                                 sys_nh_param_com_t* p_para /*Member info*/);
extern int32
sys_greatbelt_nh_get_mcast_member(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info);

extern int32
sys_greatbelt_nh_get_mcast_member_info(uint8 lchip, sys_nh_info_mcast_t* p_mcast_db, ctc_nh_info_t* p_nh_info);

extern int32
sys_greatbelt_nh_create_ipuc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db);

extern int32
sys_greatbelt_nh_delete_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);
extern int32
sys_greatbelt_nh_update_ipuc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para);

extern int32
sys_greatbelt_nh_create_special_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                   sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_special_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);

extern int32
sys_greatbelt_nh_update_ecmp_member(uint8 lchip, uint32 nhid, uint8 change_type);

extern int32
sys_greatbelt_nh_create_ecmp_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db);

extern int32
sys_greatbelt_nh_delete_ecmp_cb(uint8 lchip, sys_nh_info_com_t* p_ecmp_param, uint32* p_nhid);

extern int32
sys_greatbelt_nh_update_ecmp_cb(uint8 lchip, sys_nh_info_com_t* p_ecmp_info, sys_nh_param_com_t* p_ecmp_param);

extern int32
sys_greatbelt_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp);
extern int32
sys_greatbelt_nh_get_ecmp_mode(uint8 lchip, bool* ecmp_mode);
extern int32
sys_greatbelt_nh_remove_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t* p_dsl2edit_4w);

extern int32
sys_greatbelt_nh_remove_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t* p_dsl2edit_8w);

extern int32
sys_greatbelt_nh_add_route_l2edit(uint8 lchip, sys_nh_db_dsl2editeth4w_t** p_dsl2edit_4w);   /*DsL2Edit*/

extern int32
sys_greatbelt_nh_add_route_l2edit_8w(uint8 lchip, sys_nh_db_dsl2editeth8w_t** p_dsl2edit_8w);

extern int32
sys_greatbelt_nh_write_entry_arp(uint8 lchip, sys_nh_db_arp_t* p_ds_arp);

extern int32
sys_greatbelt_nh_lkup_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t** pp_mpls_tunnel);

int32
sys_greatbelt_nh_add_mpls_tunnel(uint8 lchip, uint16 tunnel_id, sys_nh_db_mpls_tunnel_t* p_mpls_tunnel);
int32
sys_greatbelt_nh_remove_mpls_tunnel(uint8 lchip, uint16 tunnel_id);

extern int32
sys_greatbelt_nh_lkup_mcast_id(uint8 lchip, uint16 mcast_id, sys_nh_info_mcast_t** pp_mcast);

extern int32
sys_greatbelt_nh_get_mcast_nh(uint8 lchip, uint32 mcast_id, uint32* nhid);

extern int32
sys_greatbelt_nh_add_mcast_id(uint8 lchip, uint16 mcast_id, sys_nh_info_mcast_t* p_macst);

extern int32
sys_greatbelt_nh_remove_mcast_id(uint8 lchip, uint16 mcast_id);

extern int32
sys_greatbelt_nh_traverse_mcast_db(uint8 lchip, vector_traversal_fn2 fn, void* data);

extern uint32
sys_greatbelt_nh_get_arp_num(uint8 lchip);

extern int32
sys_greatbelt_nh_lkup_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t** pp_arp);

extern int32
sys_greatbelt_nh_add_arp_id(uint8 lchip, uint16 arp_id, sys_nh_db_arp_t* p_arp);

extern int32
sys_greatbelt_nh_remove_arp_id(uint8 lchip, uint16 arp_id);

extern int32
sys_greatbelt_nh_create_mpls_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);
extern int32
sys_greatbelt_nh_update_mpls_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                sys_nh_param_com_t* p_para);
extern int32
sys_greatbelt_nh_create_iloop_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);
extern int32
sys_greatbelt_nh_update_iloop_cb(uint8 lchip, sys_nh_info_com_t* p_iloop_info, sys_nh_param_com_t* p_iloop_param);

extern int32
sys_greatbelt_nh_create_rspan_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                 sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_rspan_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);

extern int32
sys_greatbelt_nh_create_misc_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para, sys_nh_info_com_t* p_com_db);
extern int32
sys_greatbelt_nh_delete_misc_cb(uint8 lchip, sys_nh_info_com_t* p_flex_info, uint32* p_nhid);
extern int32
sys_greatbelt_nh_update_misc_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db, sys_nh_param_com_t* p_para);

extern int32
sys_greatbelt_nh_api_create(uint8 lchip, sys_nh_param_com_t* p_nh_com_para);
extern int32
sys_greatbelt_nh_api_delete(uint8 lchip, uint32 nhid, sys_greatbelt_nh_type_t nhid_type);
extern int32
sys_greatbelt_nh_api_update(uint8 lchip, uint32 nhid, sys_nh_param_com_t* p_nh_com_para);
extern int32
sys_greatbelt_nh_api_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg);
extern int32
sys_greatbelt_nh_api_deinit(uint8 lchip);

extern int32
sys_greatbelt_nh_get_entry_dsfwd(uint8 lchip, uint32 dsfwd_offset, void* p_dsfwd);

extern bool
sys_greatbelt_nh_is_ipmc_bitmap_enable(uint8 lchip);
extern int32
sys_greatbelt_nh_get_reflective_brg_en(uint8 lchip, uint8 *enable);


extern int32
sys_greatbelt_nh_set_ip_use_l2edit(uint8 lchip, uint8 enable);
extern int32
sys_greatbelt_nh_get_ip_use_l2edit(uint8 lchip, uint8* enable);

extern int32
sys_greatbelt_nh_get_ipmc_bitmap_dsnh_offset(uint8 lchip, uint32 met_offset, uint32* dsnh_offset);

extern int32
sys_greatbelt_nh_add_stats_action(uint8 lchip, uint32 nhid, uint16 vrfid);

extern int32
sys_greatbelt_nh_del_stats_action(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_get_stats_result(uint8 lchip, ctc_nh_stats_info_t* stats_info);

extern int32
sys_greatbelt_nh_reset_stats_result(uint8 lchip, ctc_nh_stats_info_t* stats_info);

extern int32
sys_greatbelt_nh_get_max_external_nhid(uint8 lchip, uint32* nhid);

extern int32
sys_greatbelt_nh_create_ip_tunnel_cb(uint8 lchip, sys_nh_param_com_t* p_com_nh_para,
                                     sys_nh_info_com_t* p_com_db);

extern int32
sys_greatbelt_nh_delete_ip_tunnel_cb(uint8 lchip, sys_nh_info_com_t* p_data, uint32* p_nhid);

extern int32
sys_greatbelt_nh_update_ip_tunnel_cb(uint8 lchip, sys_nh_info_com_t* p_nh_db,
                                     sys_nh_param_com_t* p_para);

extern int32
sys_greatbelt_nh_ip_tunnel_init(uint8 lchip);

extern int32
sys_greatbelt_nh_ip_tunnel_deinit(uint8 lchip);

extern int32
sys_greatbelt_nh_dump_resource_usage(uint8 lchip);

extern int32
sys_greatbelt_nh_swap_nexthop_offset(uint8 lchip, uint16 nhid, uint16 nhid2);
extern bool
_sys_greatbelt_nh_is_ipmc_logic_rep_enable(uint8 lchip);
extern int32
_sys_greatbelt_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable);


#ifdef __cplusplus
}
#endif

#endif /*_SYS_HUMBER_NEXTHOP_INTERNAL_H_*/


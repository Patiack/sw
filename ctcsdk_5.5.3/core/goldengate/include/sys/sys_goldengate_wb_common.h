/**
 @file sys_goldengate_wb_ipuc.h

 @date 2016-04-20

 @version v1.0

 The file defines queue api
*/

#ifndef _SYS_GOLDENGATE_WB_COMMON_H_
#define _SYS_GOLDENGATE_WB_COMMON_H_
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

#include "sys_goldengate_scl.h"
#include "sys_goldengate_acl.h"

/****************************************************************
*
* ipuc module
*
****************************************************************/
typedef uint32 wb_ipv4_addr_t[1];

enum sys_wb_appid_ipuc_subid_e
{
    SYS_WB_APPID_IPUC_SUBID_MASTER,
    SYS_WB_APPID_IPUC_SUBID_INFO,
    SYS_WB_APPID_IPUC_SUBID_NAT_SA_INFO,
    SYS_WB_APPID_IPUC_SUBID_MAX
};
typedef enum sys_wb_appid_ipuc_subid_e sys_wb_appid_ipuc_subid_t;

enum wb_lpm_sram_table_index_e
{
    WB_LPM_TABLE_INDEX0,
    WB_LPM_TABLE_INDEX1,
    WB_LPM_TABLE_MAX
};
typedef enum wb_lpm_sram_table_index_e wb_lpm_sram_table_index_t;

struct sys_wb_lpm_info_s
{
    uint32 idx_mask[WB_LPM_TABLE_MAX];
    uint32 offset[WB_LPM_TABLE_MAX];
    uint8   masklen_pushed;             /* pushed masklen */
    uint8   is_mask_valid;              /* 0 means have push down, but no match key and do nothing */
    uint8   is_pushed;                  /* if new, need push */
    uint8   rsv;
};
typedef struct sys_wb_lpm_info_s sys_wb_lpm_info_t;

/* Warning!!! If modify this struct must sync to sys_ipv4_info_t and sys_ipv6_info_t */
struct sys_wb_ipuc_info_s
{
    /*key*/
    uint8  ip_ver;
    uint8  masklen;
    uint16 vrf_id;
    uint16 l4_dst_port;
    uint32 ip[4];
    uint32 calc_key_len[0];

    /*data*/
    uint32 nh_id;
    uint32 key_offset;  /* for l3 hash, must get key_offset from p_hash. Because tcam may shift by _sys_goldengate_ipv4_syn_key */
    uint32 l3if;        /* used to store rpf_id */

    uint32 route_flag;  /* sys_ipuc_flag_e  ctc_ipuc_flag_e*/
    uint32 gport;           /* assign output port */
    uint16 ad_offset;
    uint8  conflict_flag;
    uint8  rpf_mode;        /* refer to sys_rpf_chk_mode_t */
    uint8  route_opt;
    sys_wb_lpm_info_t lpm_info;
};
typedef struct sys_wb_ipuc_info_s sys_wb_ipuc_info_t;

struct sys_wb_ipuc_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];

    /*data*/
    uint32 tcam_mode;

    uint16 napt_hash_counter;
    uint32 snat_hash_counter;
    uint32 snat_tcam_counter;

    uint32 host_counter[MAX_CTC_IP_VER];
    uint32 lpm_counter[MAX_CTC_IP_VER];
    uint32 hash_counter[MAX_CTC_IP_VER];
    uint16 tcam_ip_count[MAX_CTC_IP_VER];
    uint32 alpm_counter[MAX_CTC_IP_VER];
    uint32 alpm_usage[WB_LPM_TABLE_MAX];
    uint16 conflict_tcam_counter[MAX_CTC_IP_VER];
    uint16 conflict_alpm_counter[MAX_CTC_IP_VER];

    uint32  ipv4_block_left[CTC_IPV4_ADDR_LEN_IN_BIT + 1];
    uint32  ipv4_block_right[CTC_IPV4_ADDR_LEN_IN_BIT + 1];
    uint32  ipv6_block_left[CTC_IPV6_ADDR_LEN_IN_BIT + 1];
    uint32  ipv6_block_right[CTC_IPV6_ADDR_LEN_IN_BIT + 1];
    uint32  share_block_left[CTC_IPV4_ADDR_LEN_IN_BIT+CTC_IPV6_ADDR_LEN_IN_BIT];
    uint32  share_block_right[CTC_IPV4_ADDR_LEN_IN_BIT+CTC_IPV6_ADDR_LEN_IN_BIT];
};
typedef struct sys_wb_ipuc_master_s sys_wb_ipuc_master_t;

struct sys_wb_ipuc_nat_sa_info_s
{
    /*key*/
    wb_ipv4_addr_t ipv4;
    uint16 l4_src_port;
    uint16 vrf_id;
    uint8  is_tcp_port;
    uint32 calc_key_len[0];

    /*data*/
    uint16 ad_offset;
    uint16 key_offset;
    wb_ipv4_addr_t new_ipv4;
    uint16 new_l4_src_port;
    uint8  in_tcam;
};
typedef struct sys_wb_ipuc_nat_sa_info_s sys_wb_ipuc_nat_sa_info_t;
/****************************************************************
*
* ipmc module
*
****************************************************************/
enum sys_wb_appid_ipmc_subid_e
{
    SYS_WB_APPID_IPMC_SUBID_MASTER,
    SYS_WB_APPID_IPMC_SUBID_GROUP_NODE,
    SYS_WB_APPID_IPMC_SUBID_MAX
};
typedef enum sys_wb_appid_ipuc_subid_e sys_wb_appid_ipmc_subid_t;

struct sys_wb_ipmc_group_node_s
{
    /*key*/
    uint8  ip_ver;
    uint8  vrfid;              /**< [HB.GB.GG] VRF Id ,if IP-L2MC,used as FID */
    uint16 group_id;
    uint32 group_addr[4];         /**< [HB.GB.GG] IPv4 Group address */
    uint32 src_addr[4];           /**< [HB.GB.GG] IPv4 Source address */
    uint32 calc_key_len[0];

    /*data*/
    uint16  nexthop_id;
    uint16  refer_count;
    uint32  ad_index;
    uint32  sa_index;
    uint32  pointer;
    uint8   flag;
    uint8   rsv[3];
    uint32 extern_flag;
    uint16 rpf_index;
};
typedef struct sys_wb_ipmc_group_node_s sys_wb_ipmc_group_node_t;

struct sys_wb_ipmc_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];

    /*data*/
    uint32 ipmc_group_cnt;
};
typedef struct sys_wb_ipmc_master_s sys_wb_ipmc_master_t;


/****************************************************************
*
* port module
*
****************************************************************/
#define SYS_WB_MAX_PORT_NUM_PER_CHIP            512

enum sys_wb_appid_port_subid_e
{
    SYS_WB_APPID_PORT_SUBID_MASTER,
    SYS_WB_APPID_PORT_SUBID_PROP,
    SYS_WB_APPID_INTERPORT_SUBID_MASTER,
    SYS_WB_APPID_PORT_SUBID_MAX
};
typedef enum sys_wb_appid_port_subid_e sys_wb_appid_port_subid_t;

struct sys_wb_igs_port_prop_s
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
typedef struct sys_wb_igs_port_prop_s sys_wb_igs_port_prop_t;

struct sys_wb_egs_port_prop_s
{
    uint8 flag;
    uint8 flag_ext;

    uint8 isolation_id;
    uint8 isolation_ref_cnt;
};
typedef struct sys_wb_egs_port_prop_s sys_wb_egs_port_prop_t;

struct sys_wb_port_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    int32       mac_bmp[4]; /* used for TAP, record tap dest, 0~1 for slice0, 2~3 for slice1 */
    uint8       use_logic_port_check;
    uint8       mlag_isolation_en;
    uint8       use_isolation_id;
    uint8       rsv;
    uint32     igs_isloation_bitmap;
    uint32     egs_isloation_bitmap;
};
typedef struct sys_wb_port_master_s sys_wb_port_master_t;

struct sys_wb_port_prop_s
{
    /*key*/
    uint8  lchip;
    uint8  rsv;
    uint16 lport;
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_igs_port_prop_t igs_port_prop;
    sys_wb_egs_port_prop_t egs_port_prop;
};
typedef struct sys_wb_port_prop_s sys_wb_port_prop_t;

struct sys_wb_interport_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint32 is_used_igs[SYS_WB_MAX_PORT_NUM_PER_CHIP / BITS_NUM_OF_WORD];
    uint32 is_used_egs[SYS_WB_MAX_PORT_NUM_PER_CHIP / BITS_NUM_OF_WORD];
};
typedef struct sys_wb_interport_master_s sys_wb_interport_master_t;


/****************************************************************
*
* linkagg module
*
****************************************************************/
enum sys_wb_appid_linkagg_subid_e
{
    SYS_WB_APPID_LINKAGG_SUBID_MASTER,
    SYS_WB_APPID_LINKAGG_SUBID_GROUP,
    SYS_WB_APPID_LINKAGG_SUBID_PORT,
    SYS_WB_APPID_LINKAGG_SUBID_CHANGROUP,
    SYS_WB_APPID_LINKAGG_SUBID_CHANPORT,
    SYS_WB_APPID_LINKAGG_SUBID_MAX
};
typedef enum sys_wb_appid_linkagg_subid_e sys_wb_appid_linkagg_subid_t;

struct sys_wb_linkagg_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint8 mem_port_num[64];            /* port num of linkagg group */
};
typedef struct sys_wb_linkagg_master_s sys_wb_linkagg_master_t;

struct sys_wb_linkagg_port_s
{
    /*key*/
    uint8 tid;              /* linkAggId */
    uint8 rsv0;
    uint16 index;
    uint32 calc_key_len[0];
    /*data*/
    uint8 valid;        /* 1: member port is in linkAgg group */
    uint8 pad_flag;     /* member is padding memeber*/
    uint16 gport;       /* member port */
};
typedef struct sys_wb_linkagg_port_s sys_wb_linkagg_port_t;

struct sys_wb_linkagg_group_s
{
    /*key*/
    uint8 tid;              /* linkAggId */
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint8 port_cnt;         /* member num of linkAgg group */
    uint8 linkagg_mode;     /* ctc_linkagg_group_mode_t */
    uint8 need_pad;
    uint8 ref_cnt;   /*for bpe cb cascade*/
};
typedef struct sys_wb_linkagg_group_s sys_wb_linkagg_group_t;

/****************************************************************
*
* l3if module
*
****************************************************************/
#define SYS_WB_MAX_ECPN                         64

enum sys_wb_appid_l3if_subid_e
{
    SYS_WB_APPID_L3IF_SUBID_PROP,
    SYS_WB_APPID_L3IF_SUBID_ROUTER_MAC,
    SYS_WB_APPID_L3IF_SUBID_ECMP_IF
};
typedef enum sys_wb_appid_l3if_subid_e sys_wb_appid_l3if_subid_t;

struct sys_wb_l3if_prop_s
{
    /*key*/
    uint32 l3if_id;   /*lchip*/
    uint32 calc_key_len[0];

    /**data */
    uint16  vlan_id;
    uint16  gport;
    uint8   vaild;
    uint8   l3if_type; /**< the type of l3interface , CTC_L3IF_TYPE_XXX */
    uint8   rtmac_index;
    uint8   rtmac_bmp;
    uint16  vlan_ptr; /**< Vlanptr */
};
typedef struct sys_wb_l3if_prop_s sys_wb_l3if_prop_t;

struct sys_wb_l3if_router_mac_s
{
    /*key*/
    uint32 profile_id;
    uint8  is_inner;
    uint8  rsv[3];
    uint32 calc_key_len[0];

    /**data */
    mac_addr_t mac;
    uint8 ref;
    uint8 rsv0[3];
};
typedef struct sys_wb_l3if_router_mac_s sys_wb_l3if_router_mac_t;

struct sys_wb_l3if_ecmp_if_s
{
    /*key*/
    uint32 ecmp_if_id;   /*lchip*/
    uint32 calc_key_len[0];

    /**data */
    uint16 hw_group_id;
    uint8  intf_count;
    uint8  failover_en;
    uint16 intf_array[SYS_WB_MAX_ECPN];
    uint32 dsfwd_offset[SYS_WB_MAX_ECPN];
    uint16 ecmp_group_type;     /* refer to ctc_nh_ecmp_type_t */
    uint16 ecmp_member_base;
    uint32 stats_id;
};
typedef struct sys_wb_l3if_ecmp_if_s sys_wb_l3if_ecmp_if_t;

/****************************************************************
*
* datapath module
*
****************************************************************/
#define SYS_WB_DATAPATH_CORE_NUM 2
#define SYS_WB_HSS15G_LANE_NUM 8

enum sys_wb_appid_datapath_subid_e
{
    SYS_WB_APPID_DATAPATH_SUBID_MASTER,
    SYS_WB_APPID_DATAPATH_SUBID_HSS_ATTRIBUTE,
    SYS_WB_APPID_DATAPATH_SUBID_MAX
};
typedef enum sys_wb_appid_datapath_subid_e sys_wb_appid_datapath_subid_t;

struct sys_wb_datapath_lport_attr_s
{
    uint8 port_type;  /*refer to sys_datapath_lport_type_t */
    uint8 mac_id;
    uint8 chan_id;
    uint8 speed_mode;  /*refer to ctc_port_speed_t, only usefull for network port*/
    uint8 slice_id;
    uint8 cal_index;  /*Only usefull for network port, means calendar entry index*/
    uint8 need_ext;
    uint8 pcs_mode; /*refer to ctc_chip_serdes_mode_t*/

    uint8 pcs_init_mode; /*init mode, can not modify. refer to ctc_chip_serdes_mode_t*/
    uint8 serdes_id;
    uint8 is_first;   /* 40G/100G, 4 interrupts will process the first*/
    uint8 switch_num;

    uint8 reset_app;
    uint8 first_led_mode;  /* when led_mode tx activ, when port link down, need to set force led off mode */
    uint8 sec_led_mode;
    uint8 rsv;
};
typedef struct sys_wb_datapath_lport_attr_s sys_wb_datapath_lport_attr_t;

struct sys_wb_datapath_master_s
{
    /*key*/
    uint8 slice_id;
    uint8 rsv0;
    uint16 lport;
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_datapath_lport_attr_t port_attr;
};
typedef struct sys_wb_datapath_master_s sys_wb_datapath_master_t;

struct sys_wb_datapath_serdes_info_s
{
    uint8 mode;        /*pcs mode, refer to ctc_chip_serdes_mode_t*/
    uint8 pll_sel;       /* 0: disable,1:plla. 2:pllb */
    uint8 rate_div;     /*0: full, 1:1/2, 2:1/4, 3:1/8*/
    uint8 bit_width;   /* bit width*/
    uint16 lport;        /*chip local phy port*/
    uint8 rx_polarity;  /*0:normal, 1:reverse*/
    uint8 tx_polarity;  /*0:normal, 1:reverse*/
    uint8 lane_id;  /*serdes internal lane id */
    uint8 is_dyn;
    uint8 rsv;
    uint16 overclocking_speed; /*serdes over clock*/
};
typedef struct sys_wb_datapath_serdes_info_s   sys_wb_datapath_serdes_info_t;

struct sys_wb_datapath_hss_attribute_s
{
    /*key*/
    uint32 hss_idx;
    uint32 calc_key_len[0];
    /*data*/
    uint8 hss_type;   /*0:Hss15G, 1:Hss28G, refer sys_datapath_hss_type_t*/
    uint8 hss_id;        /*for hss15g is 0~9, for hss28g is 0~3*/
    uint8 plla_mode;  /*refer to sys_datapath_hssclk_t */
    uint8 pllb_mode;  /*refer to sys_datapath_hssclk_t */
    uint8 core_div_a[SYS_WB_DATAPATH_CORE_NUM];  /*refer to sys_datapath_clktree_div_t, usefull plla is used*/
    uint8 core_div_b[SYS_WB_DATAPATH_CORE_NUM];  /*refer to sys_datapath_clktree_div_t, usefull plla is used*/
    uint8 intf_div_a;   /*refer to sys_datapath_clktree_div_t, usefull pllb is used*/
    uint8 intf_div_b;   /*refer to sys_datapath_clktree_div_t, usefull pllb is used*/
    sys_wb_datapath_serdes_info_t  serdes_info[SYS_WB_HSS15G_LANE_NUM];   /*hss28G only 4 serdes info is valid, refer to sys_datapath_serdes_info_t  */
    uint8 plla_ref_cnt;      /*indicate used lane number for the plla, if the num decrease to 0, set plla to disable*/
    uint8 pllb_ref_cnt;      /*indicate used lane number for the pllb, if the num decrease to 0, set pllb to disable*/
    uint16 clktree_bitmap; /*indicate clktree need cfg, refer to  sys_datapath_clktree_op_t */
};
typedef struct sys_wb_datapath_hss_attribute_s sys_wb_datapath_hss_attribute_t;


/****************************************************************
*
* security module
*
****************************************************************/
#define SYS_WB_SECURITY_PORT_UNIT_INDEX_MAX  80
#define SYS_WB_SECURITY_VLAN_UNIT_INDEX_MAX  20

enum sys_wb_appid_security_subid_e
{
    SYS_WB_APPID_SECURITY_SUBID_MASTER,
    SYS_WB_APPID_SECURITY_SUBID_LIMIT,
    SYS_WB_APPID_SECURITY_SUBID_MAX
};
typedef enum sys_wb_appid_security_subid_e sys_wb_appid_security_subid_t;

struct sys_wb_security_stmctl_glb_cfg_s
{
    uint8 ipg_en;
    uint8 ustorm_ctl_mode;
    uint8 mstorm_ctl_mode;
    uint8 rsv;
    uint16 granularity;
};
typedef struct sys_wb_security_stmctl_glb_cfg_s sys_wb_security_stmctl_glb_cfg_t;

struct sys_wb_security_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_security_stmctl_glb_cfg_t stmctl_cfg;
    uint8 flag;
    uint8 ip_src_guard_def;
    uint8 rsv[2];
    uint32 stmctl_granularity; /* (100/1000/10000) from ctc_security_storm_ctl_granularity_t */
    uint32 port_unit_mode[SYS_WB_SECURITY_PORT_UNIT_INDEX_MAX];
    uint32 vlan_unit_mode[SYS_WB_SECURITY_VLAN_UNIT_INDEX_MAX];
};
typedef struct sys_wb_security_master_s sys_wb_security_master_t;

struct sys_wb_learn_limit_trhold_node_s
{
    /*key*/
    uint32 value;
    uint32 calc_key_len[0];
    /*data*/
    uint32 cnt;
    uint32 index; /*learn limit offset*/
};
typedef struct sys_wb_learn_limit_trhold_node_s sys_wb_learn_limit_trhold_node_t;

/****************************************************************
*
* stats module
*
****************************************************************/

enum sys_wb_stats_type_e
{
    SYS_WB_STATS_TYPE_IGS_PORT_PHB,
    SYS_WB_STATS_TYPE_EGS_PORT_PHB,
    SYS_WB_STATS_TYPE_IGS_GLOBAL_FWD,
    SYS_WB_STATS_TYPE_EGS_GLOBAL_FWD,
    SYS_WB_STATS_TYPE_FWD,
    SYS_WB_STATS_TYPE_GMAC,
    SYS_WB_STATS_TYPE_XGMAC,
    SYS_WB_STATS_TYPE_SGMAC,
    SYS_WB_STATS_TYPE_XQMAC,
    SYS_WB_STATS_TYPE_CGMAC,
    SYS_WB_STATS_TYPE_CPUMAC,

    SYS_WB_STATS_TYPE_MAX
};
typedef enum sys_wb_stats_type_e sys_wb_stats_type_t;

#define SYS_WB_STATS_TYPE_USED_COUNT_MAX           (SYS_WB_STATS_TYPE_MAX + 5)

enum sys_wb_stats_cache_mode_opf_type_e
{
    SYS_WB_STATS_CACHE_MODE_POLICER,
    SYS_WB_STATS_CACHE_MODE_IPE_IF_EPE_FWD,    /* for ipe if & epe fwd stats */
    SYS_WB_STATS_CACHE_MODE_EPE_IF_IPE_FWD,    /* for ipe fwd & epe if stats */
    SYS_WB_STATS_CACHE_MODE_IPE_ACL0,
    SYS_WB_STATS_CACHE_MODE_IPE_ACL1,
    SYS_WB_STATS_CACHE_MODE_IPE_ACL2,
    SYS_WB_STATS_CACHE_MODE_IPE_ACL3,
    SYS_WB_STATS_CACHE_MODE_EPE_ACL0,

    SYS_WB_STATS_CACHE_MODE_MAX
};
typedef enum sys_wb_stats_cache_mode_opf_type_e sys_wb_stats_cache_mode_opf_type_t;


enum sys_wb_appid_stats_subid_e
{
    SYS_WB_APPID_STATS_SUBID_MASTER,
    SYS_WB_APPID_STATS_SUBID_STATSID,
    SYS_WB_APPID_STATS_SUBID_MAX
};
typedef enum sys_wb_appid_stats_subid_e sys_wb_appid_stats_subid_t;

struct sys_wb_stats_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint8 saturate_en[SYS_WB_STATS_TYPE_MAX];
    uint8 hold_en[SYS_WB_STATS_TYPE_MAX];
    uint16 used_count[SYS_WB_STATS_CACHE_MODE_MAX];
    uint16 type_used_count[SYS_WB_STATS_TYPE_USED_COUNT_MAX];   /* +4 for l3if egress, fid egress, mpls vc label, tunnel egress */
};
typedef struct sys_wb_stats_master_s sys_wb_stats_master_t;

struct sys_wb_stats_statsid_s
{
    /*key*/
    uint32 stats_id;
    uint32 calc_key_len[0];
    /*data*/
    uint16 stats_ptr;
    uint8 rsv[2];
    uint8  stats_id_type;
    uint8  dir;
    uint8  acl_priority;
    uint8  is_vc_label;
};
typedef struct sys_wb_stats_statsid_s sys_wb_stats_statsid_t;

/****************************************************************
*
* mirror module
*
****************************************************************/
enum sys_wb_appid_mirror_subid_e
{
    SYS_WB_APPID_MIRROR_SUBID_DEST,
    SYS_WB_APPID_MIRROR_SUBID_MAX
};
typedef enum sys_wb_appid_mirror_subid_e sys_wb_appid_mirror_subid_t;

struct sys_wb_mirror_dest_s
{
   /*key*/
   uint8 type;
   uint8 dir;
   uint8 session_id;
   uint8 rsv;
   uint32 calc_key_len[0];

   /*data*/
   uint32 nh_id;
   uint8 is_external;
   uint8 rsv1[3];
};
typedef struct sys_wb_mirror_dest_s sys_wb_mirror_dest_t;

/****************************************************************
*
* vlan module
*
****************************************************************/
enum sys_wb_appid_vlan_subid_e
{
    SYS_WB_APPID_VLAN_SUBID_MASTER,
    SYS_WB_APPID_VLAN_SUBID_OVERLAY_MAPPING_KEY,
    SYS_WB_APPID_VLAN_SUBID_MAX
};
typedef enum sys_wb_appid_vlan_subid_e sys_wb_appid_vlan_subid_t;

struct sys_wb_vlan_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint32 vlan_bitmap[128];
    uint32 vlan_def_bitmap[128];
};
typedef struct sys_wb_vlan_master_s sys_wb_vlan_master_t;

struct sys_wb_vlan_overlay_mapping_key_s
{
    /*key*/
    uint32 vn_id;
    uint32 calc_key_len[0];
    /*data*/
    uint16 fid;
};
typedef struct sys_wb_vlan_overlay_mapping_key_s sys_wb_vlan_overlay_mapping_key_t;

/****************************************************************
*
* scl module
*
****************************************************************/
#define SYS_WB_SCL_BLOCK_ID_NUM    2
#define    SYS_WB_FPA_KEY_SIZE_160 0
#define    SYS_WB_FPA_KEY_SIZE_320 1
#define   SYS_WB_FPA_KEY_SIZE_640  2
#define   SYS_WB_FPA_KEY_SIZE_NUM 3

enum sys_wb_appid_scl_subid_e
{
    SYS_WB_APPID_SCL_SUBID_MASTER,
    SYS_WB_APPID_SCL_SUBID_GROUP,
    SYS_WB_APPID_SCL_SUBID_ENTRY,
    SYS_WB_APPID_SCL_SUBID_MAX
};
typedef enum sys_wb_appid_scl_subid_e sys_wb_appid_scl_subid_t;

struct sys_wb_scl_sw_block_s
{
    uint16      start_offset[SYS_WB_FPA_KEY_SIZE_NUM];
    uint16      sub_entry_count[SYS_WB_FPA_KEY_SIZE_NUM];
    uint16      sub_free_count[SYS_WB_FPA_KEY_SIZE_NUM];
};
typedef struct sys_wb_scl_sw_block_s sys_wb_scl_sw_block_t;

struct sys_wb_scl_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_scl_sw_block_t block[SYS_WB_SCL_BLOCK_ID_NUM];
};
typedef struct sys_wb_scl_master_s sys_wb_scl_master_t;

struct sys_wb_scl_sw_group_info_s
{
    uint8 type;
    uint8 block_id; /* block the group belong. For hash, 0xFF. */
    union
    {
        uint16 port_class_id;
        uint16 gport;       /* logic_port or gport*/
    }u0;
};
typedef struct sys_wb_scl_sw_group_info_s sys_wb_scl_sw_group_info_t;

struct sys_wb_scl_sw_group_s
{
    /*key*/
    uint32  group_id; /* keep group_id top, never move it.*/
    uint32  calc_key_len[0];
    /*data*/
    uint8   flag;     /* group flag*/
    uint8   rsv0[3];
    sys_wb_scl_sw_group_info_t group_info;     /* group info */
};
typedef struct sys_wb_scl_sw_group_s sys_wb_scl_sw_group_t;

struct sys_wb_scl_sw_entry_s
{
    /*key*/
    ctc_fpa_entry_t    fpae;
    uint32 calc_key_len[0];
    /*data*/
    sys_scl_sw_action_t  action;
    uint32 group_id;
    sys_scl_sw_key_t   key;                    /* keep key at tail !! */
};
typedef struct sys_wb_scl_sw_entry_s sys_wb_scl_sw_entry_t;
/****************************************************************
*
* fdb module
*
****************************************************************/
enum sys_wb_appid_l2_subid_e
{
    SYS_WB_APPID_L2_SUBID_MASTER,
    SYS_WB_APPID_L2_SUBID_TCAM,
    SYS_WB_APPID_L2_SUBID_FID_NODE,
    SYS_WB_APPID_L2_SUBID_VPORT_2_NHID,
    SYS_WB_APPID_L2_SUBID_MCAST_2_NHID,
    SYS_WB_APPID_L2_SUBID_NODE,
    SYS_WB_APPID_L2_SUBID_MAX
};
typedef enum sys_wb_appid_l2_subid_e sys_wb_appid_l2_subid_t;

struct sys_wb_l2_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint8    static_fdb_limit;
    uint8   trie_sort_en;
    uint8   rsv1[2];
    uint32  unknown_mcast_tocpu_nhid;
};
typedef struct sys_wb_l2_master_s   sys_wb_l2_master_t;

struct sys_wb_l2_flag_node_s
{
/* below are not include hash compare */
    uint32  remote_dynamic     : 1;  /*not ad*/
    uint32  ecmp_valid         : 1;  /*not ad*/
    uint32  is_tcam            : 1;  /*not ad*/
    uint32 rsv_ad_index       : 1;  /*not ad*/
    uint32 type_default       : 1;  /*not ad*/
    uint32 type_l2mc          : 1;  /*not ad*/
    uint32 system_mac         : 1;  /*not ad*/
    uint32 aps_valid          : 1;  /*not ad*/
    uint32 white_list         : 1; /*not ad*/
    uint32 mc_nh              : 1;  /*not ad, for Uc using  mcast nexthop*/
    uint32 aging_disable      : 1;  /*not ad, disable aging*/
    uint32 is_ecmp_intf       : 1;  /*not ad, disable aging*/
    uint32 limit_exempt       : 1;
    uint32 rsv1               : 19;

};
typedef struct sys_wb_l2_flag_node_s sys_wb_l2_flag_node_t;

struct sys_wb_l2_tcam_s
{
    /*key*/
    mac_addr_t mac;
    uint8 rsv0[2];
    uint32 calc_key_len[0];
    /*data*/
    uint32  key_index;
    uint32  ad_index;
    sys_wb_l2_flag_node_t flag_node;
};
typedef struct sys_wb_l2_tcam_s sys_wb_l2_tcam_t;

struct sys_wb_l2_fid_node_s
{
    /*key*/
    uint16  fid;
    uint8 rsv0[2];
    uint32  calc_key_len[0];
    /*data*/
    uint32  nhid;
    uint16  mc_gid;
    uint16  flag;         /**<ctc_l2_dft_vlan_flag_t */
    uint8   create;       /* create by default entry */
    uint8   share_grp_en; /* for remove default entry, if set, not remove nh*/
    uint8   unknown_mcast_tocpu;
    uint8   rsv1;
};
typedef struct sys_wb_l2_fid_node_s sys_wb_l2_fid_node_t;

struct sys_wb_l2_vport_2_nhid_s
{
    /*key*/
    uint16  vport;
    uint8   rsv[2];
    uint32  calc_key_len[0];
    /*data*/
    uint32  nhid;
    uint32  ad_idx;
};
typedef struct sys_wb_l2_vport_2_nhid_s sys_wb_l2_vport_2_nhid_t;

struct sys_wb_l2_mcast_2_nhid_s
{
    /*key*/
    uint32  group_id;
    uint32  calc_key_len[0];
    /*data*/
    uint32  nhid;
    uint32 key_index;
    uint32 ref_cnt;
};
typedef struct sys_wb_l2_mcast_2_nhid_s sys_wb_l2_mcast_2_nhid_t;

struct sys_wb_l2_key_s
{
    mac_addr_t mac;
    uint16     fid;
};
typedef struct sys_wb_l2_key_s sys_wb_l2_key_t;

struct sys_wb_l2_node_s
{
    /*key*/
    sys_wb_l2_key_t   key;
    uint32  calc_key_len[0];
    /*data*/
    uint32  key_index;       /**< key key_index for ucast and mcast, adptr->index for default entry*/
    uint32 nhid;   /*for mc, def. because they don't have fwdptr.*/
    uint32 ad_index;
    uint16 dest_gport;
    uint8   rsv[2];
    sys_wb_l2_flag_node_t  flag;
};
typedef struct sys_wb_l2_node_s   sys_wb_l2_node_t;


/****************************************************************
*
* advance vlan module
*
****************************************************************/
#define SYS_WB_VLAN_RANGE_ENTRY_NUM            64

enum sys_wb_appid_adv_vlan_subid_e
{
    SYS_WB_APPID_ADV_SUBID_VLAN_MASTER,
    SYS_WB_APPID_ADV_SUBID_VLAN_MAX
};
typedef enum sys_wb_appid_adv_vlan_subid_e sys_wb_appid_adv_vlan_subid_t;

struct sys_wb_adv_vlan_master_s
{
   /*key*/
   uint8 lchip;
   uint8 rsv0[3];
   uint32 calc_key_len[0];

   /*data*/
   uint8  vlan_class_def;  /* add default entry when add first entry */
   uint8  rsv[3];
   uint16 ether_type[8];
   uint32 mac_eid[2];      /*mac key default entry id of userid0, userid1*/
   uint32 ipv4_eid[2];
   uint32 ipv6_eid[2];

   uint16 vrange[SYS_WB_VLAN_RANGE_ENTRY_NUM * 2];
};
typedef struct sys_wb_adv_vlan_master_s sys_wb_adv_vlan_master_t;

/****************************************************************
*
* ACL module
*
****************************************************************/
enum sys_wb_appid_acl_subid_e
{
    SYS_WB_APPID_ACL_SUBID_MASTER,
    SYS_WB_APPID_ACL_SUBID_GROUP,
    SYS_WB_APPID_ACL_SUBID_ENTRY,
    SYS_WB_APPID_ACL_SUBID_MAX
};
typedef enum sys_wb_appid_acl_subid_e sys_wb_appid_acl_subid_t;

#define SYS_WB_ACL_IGS_BLOCK_ID_NUM    4      /* ingress acl */
#define SYS_WB_ACL_EGS_BLOCK_ID_NUM    1      /* egress acl */
#define SYS_WB_ACL_PBR_BLOCK_ID_NUM    1      /* pbr */
#define SYS_WB_ACL_BLOCK_ID_NUM        (SYS_WB_ACL_IGS_BLOCK_ID_NUM + SYS_WB_ACL_EGS_BLOCK_ID_NUM + SYS_WB_ACL_PBR_BLOCK_ID_NUM)
#define SYS_WB_ACL_L4_PORT_NUM             8
#define SYS_WB_ACL_TCP_FLAG_NUM            4

enum sys_wb_acl_count_type_e
{
    SYS_WB_ACL_ALLOC_COUNT_TCAM_160,
    SYS_WB_ACL_ALLOC_COUNT_TCAM_320,
    SYS_WB_ACL_ALLOC_COUNT_TCAM_640,
    SYS_WB_ACL_ALLOC_COUNT_PBR_160,
    SYS_WB_ACL_ALLOC_COUNT_PBR_320,
    SYS_WB_ACL_ALLOC_COUNT_HASH,
    SYS_WB_ACL_ALLOC_COUNT_NUM
};
typedef enum sys_wb_acl_count_type_e sys_wb_acl_count_type_t;

struct sys_wb_acl_block_s
{
    uint16      entry_count;  /* entry count on each block. uint16 is enough.*/
    uint16      free_count;   /* entry count left on each block*/
    uint16      start_offset[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_entry_count[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_free_count[CTC_FPA_KEY_SIZE_NUM];
    uint16      sub_rsv_count[CTC_FPA_KEY_SIZE_NUM];    /*rsv count can not be taken when adjust resource*/
};
typedef struct sys_wb_acl_block_s sys_wb_acl_block_t;

struct sys_wb_acl_tcp_flag_s
{
    uint16 ref;

    uint8  match_any; /* 0: match all , 1: match any */
    uint8  tcp_flags; /* bitmap of CTC_ACLQOS_TCP_XXX_FLAG */
};
typedef struct sys_wb_acl_tcp_flag_s   sys_wb_acl_tcp_flag_t;

struct sys_wb_acl_l4port_op_s
{
    uint16 ref;
    uint8  op_dest_port;
    uint8  rsv;

    uint16 port_min;
    uint16 port_max;
};
typedef struct sys_wb_acl_l4port_op_s   sys_wb_acl_l4port_op_t;

struct sys_wb_acl_register_s
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
typedef struct sys_wb_acl_register_s   sys_wb_acl_register_t;

struct sys_wb_acl_group_info_s
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
};
typedef struct sys_wb_acl_group_info_s sys_wb_acl_group_info_t;

struct sys_wb_acl_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_acl_block_t     block[SYS_WB_ACL_BLOCK_ID_NUM];
    sys_wb_acl_tcp_flag_t  tcp_flag[SYS_WB_ACL_TCP_FLAG_NUM];
    sys_wb_acl_l4port_op_t l4_port[SYS_WB_ACL_L4_PORT_NUM];
    uint8               l4_port_free_cnt;
    uint8               l4_port_range_num;
    uint8               tcp_flag_free_cnt;
    uint32              key_count[SYS_WB_ACL_ALLOC_COUNT_NUM];
    sys_wb_acl_register_t acl_register;
};
typedef struct sys_wb_acl_master_s sys_wb_acl_master_t;

struct sys_wb_acl_group_s
{
    /*key*/
    uint32 group_id;
    uint32  calc_key_len[0];
    /*data*/
    uint32  lchip;
    sys_wb_acl_group_info_t group_info;        /* group info */
};
typedef struct sys_wb_acl_group_s sys_wb_acl_group_t;

struct sys_wb_acl_entry_s
{
    /*key*/
    ctc_fpa_entry_t fpae;
    uint32  calc_key_len[0];
    /*data*/
    uint32 lchip;
    uint32  group_id;
    sys_acl_action_t action;               /* pointer to action node*/
    sys_acl_key_t   key;                    /* keep key at tail !! */
};
typedef struct sys_wb_acl_entry_s sys_wb_acl_entry_t;

/****************************************************************
*
* qos module
*
****************************************************************/
#define SYS_WB_RESRC_IGS_MAX_PORT_MIN_PROFILE   8

enum sys_wb_qos_egs_resrc_pool_type_e
{
    SYS_WB_QOS_EGS_RESRC_DEFAULT_POOL,
    SYS_WB_QOS_EGS_RESRC_NON_DROP_POOL,
    SYS_WB_QOS_EGS_RESRC_SPAN_POOL,
    SYS_WB_QOS_EGS_RESRC_CONTROL_POOL,
    SYS_WB_QOS_EGS_RESRC_MIN_POOL,
    SYS_WB_QOS_EGS_RESRC_C2C_POOL,

    SYS_WB_QOS_EGS_RESRC_POOL_MAX
};
typedef enum sys_wb_qos_egs_resrc_pool_type_e sys_wb_qos_egs_resrc_pool_type_t;

enum sys_wb_queue_type_e
{
    SYS_WB_QUEUE_TYPE_NORMAL,     /**< normal en queue */
    SYS_WB_QUEUE_TYPE_EXCP,       /**< exception to cpu by DMA*/
    SYS_WB_QUEUE_TYPE_RSV_PORT,   /**< reserved port */
    SYS_WB_QUEUE_TYPE_EXTENDER_WITH_Q,
    SYS_WB_QUEUE_TYPE_OAM,
    SYS_WB_QUEUE_TYPE_EXCP_BY_ETH,       /**< exception to cpu by network port*/

    SYS_WB_QUEUE_TYPE_MAX
};
typedef enum sys_wb_queue_type_e sys_wb_queue_type_t;

enum sys_wb_appid_qos_subid_e
{
    SYS_WB_APPID_QOS_SUBID_POLICER_MASTER,
    SYS_WB_APPID_QOS_SUBID_POLICER,
    SYS_WB_APPID_QOS_SUBID_QUEUE_MASTER,
    SYS_WB_APPID_QOS_SUBID_QUEUE_NODE,
    SYS_WB_APPID_QOS_SUBID_QUEUE_GROUP_NODE,
    SYS_WB_APPID_QOS_SUBID_CPU_REASON,
    SYS_WB_APPID_QOS_SUBID_MAX
};
typedef enum sys_wb_appid_qos_subid_e sys_wb_appid_qos_subid_t;

enum sys_wb_policer_cnt_e
{
    SYS_WB_POLICER_CNT_PORT_POLICER,
    SYS_WB_POLICER_CNT_FLOW_POLICER,
    SYS_WB_POLICER_CNT_MAX
};
typedef enum sys_wb_policer_cnt_e sys_wb_policer_cnt_t;

struct sys_wb_qos_policer_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint16  egs_port_policer_base;
    uint16  update_gran_base;
    uint16  policer_count[SYS_WB_POLICER_CNT_MAX];
    uint8  max_cos_level;
    uint8  policer_gran_mode;
    uint8  rsv[2];
};
typedef struct sys_wb_qos_policer_master_s sys_wb_qos_policer_master_t;

struct sys_wb_qos_policer_s
{
    /*key*/
    uint8  type; /*ctc_qos_policer_type_t*/
    uint8  dir;   /*ctc_direction_t */
    uint16 id;
    uint32  calc_key_len[0];
    /*data*/
    uint8  cos_bitmap; /*bitmap of hbwp enable*/
    uint8  triple_play; /*bitmap of hbwp enable*/
    uint8  entry_size;
    uint8  rsv[3];
    uint16 policer_ptr;
};
typedef struct sys_wb_qos_policer_s sys_wb_qos_policer_t;

struct sys_wb_qos_queue_cpu_reason_s
{
    uint8    dest_type;
    uint8    is_user_define;
    uint8    rsv[2];
    uint32   dsfwd_offset;
    uint32   dest_port;
    uint16   sub_queue_id;
    uint16   dest_map;
};
typedef struct sys_wb_qos_queue_cpu_reason_s sys_wb_qos_queue_cpu_reason_t;

struct sys_wb_qos_resc_egs_pool_s
{
   uint8 egs_congest_level_num;
   uint8 default_profile_id;
};
typedef struct sys_wb_qos_resc_egs_pool_s sys_wb_qos_resc_egs_pool_t;

struct sys_wb_qos_resc_igs_port_min_s
{
   uint16 ref;
   uint16 min;
};
typedef struct sys_wb_qos_resc_igs_port_min_s sys_wb_qos_resc_igs_port_min_t;

struct sys_wb_qos_queue_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv0[3];
    uint32 calc_key_len[0];
    /*data*/
    uint8 queue_num_per_chanel;
    uint8 enq_mode;
    uint8 queue_num_for_cpu_reason;
    uint8 c2c_group_base; /* c2c group base for CPU queue*/
    uint16  queue_base[SYS_WB_QUEUE_TYPE_MAX];
    uint16 wrr_weight_mtu;
    uint8 shp_pps_en;
    uint8  rsv[3];
    sys_wb_qos_resc_egs_pool_t egs_pool[SYS_WB_QOS_EGS_RESRC_POOL_MAX];
    sys_wb_qos_resc_igs_port_min_t igs_port_min[SYS_WB_RESRC_IGS_MAX_PORT_MIN_PROFILE];
};
typedef struct sys_wb_qos_queue_master_s sys_wb_qos_queue_master_t;

struct sys_wb_qos_queue_node_s
{
    /*key*/
    uint16 queue_id;   /*0~2047*/
    uint8  rsv[2];
    uint32 calc_key_len[0];
    /*data*/
    uint8  type;
    uint8  offset;
    uint8  shp_en;
    uint8  is_cpu_que_prof; /* for sys_queue_shp_profile_t */
};
typedef struct sys_wb_qos_queue_node_s sys_wb_qos_queue_node_t;

struct sys_wb_qos_queue_group_node_s
{
    /*key*/
    uint16 group_id;      /*super group; 0~255 ,include slice info.*/
    uint8  rsv[2];
    uint32 calc_key_len[0];
    /*data*/
    uint8  chan_id;       /*-0~127*/
    uint8  rsv1;
    uint16 sub_group_id;    /*0, 8 queue mode; 1, 4 queue mode.*/
    uint16 shp_bitmap[2];
};
typedef struct sys_wb_qos_queue_group_node_s sys_wb_qos_queue_group_node_t;

struct sys_wb_qos_cpu_reason_s
{
    /*key*/
    uint16 reason_id;
    uint8  rsv[2];
    uint32 calc_key_len[0];
    /*data*/
    sys_wb_qos_queue_cpu_reason_t  cpu_reason;
};
typedef struct sys_wb_qos_cpu_reason_s sys_wb_qos_cpu_reason_t;

/****************************************************************
*
* mpls module
*
****************************************************************/

enum sys_wb_appid_mpls_subid_e
{
    SYS_WB_APPID_MPLS_SUBID_ILM_HASH,
    SYS_WB_APPID_MPLS_SUBID_MAX
};
typedef enum sys_wb_appid_mpls_subid_e sys_wb_appid_mpls_subid_t;

struct sys_wb_mpls_ilm_hash_s
{
    /*key*/
    uint32 label;
    uint8 spaceid;
    uint8 rsv[3];
    uint32 calc_key_len[0];

    /*data*/
    uint8 is_interface;
    uint8 model;
    uint8 type;
    uint8 id_type;
    uint8 bind_dsfwd;
    uint8 rsv1[3];
    uint32 nh_id;
    uint32 stats_id;
    uint32 ad_index;
};
typedef struct sys_wb_mpls_ilm_hash_s sys_wb_mpls_ilm_hash_t;

/****************************************************************
*
* ipfix module
*
****************************************************************/
enum sys_wb_appid_ipfix_subid_e
{
    SYS_WB_APPID_IPFIX_SUBID_MASTER,
    SYS_WB_APPID_IPFIX_SUBID_MAX
};
typedef enum sys_wb_appid_ipfix_subid_e sys_wb_appid_ipfix_subid_t;

struct sys_wb_ipfix_master_s
{
    /*key*/
    uint8   lchip;
    uint8   rsv[3];
    uint32  calc_key_len[0];

    /*data*/
    uint16  igr_sip_refer_cnt[4];
    uint16  egr_sip_refer_cnt[4];
};
typedef struct sys_wb_ipfix_master_s sys_wb_ipfix_master_t;

/****************************************************************
*
* overlay tunnel module
*
****************************************************************/
#define SYS_WB_OVERLAT_TUNNEL_MAX_IP_INDEX       4

enum sys_wb_appid_overlay_tunnel_subid_e
{
    SYS_WB_APPID_OVERLAY_SUBID_TUNNEL_MASTER,
    SYS_WB_APPID_OVERLAY_SUBID_TUNNEL_MAX
};
typedef enum sys_wb_appid_overlay_tunnel_subid_e sys_wb_appid_overlay_tunnel_subid_t;

struct sys_wb_overlay_tunnel_master_s
{
    /*key*/
    uint8   lchip;
    uint8   rsv[3];
    uint32 calc_key_len[0];

    /*data*/
    uint32 ipda_index_cnt[SYS_WB_OVERLAT_TUNNEL_MAX_IP_INDEX];
};
typedef struct sys_wb_overlay_tunnel_master_s sys_wb_overlay_tunnel_master_t;


/****************************************************************
*
* aps module
*
****************************************************************/
enum sys_wb_appid_aps_subid_e
{
    SYS_WB_APPID_APS_SUBID_NODE,
    SYS_WB_APPID_APS_SUBID_MCAST_NODE,
    SYS_WB_APPID_APS_SUBID_MAX
};
typedef enum sys_wb_appid_aps_subid_e sys_wb_appid_aps_subid_t;

enum sys_wb_aps_flag_e
{
    SYS_WB_APS_FLAG_PROTECT_EN                  = 1U<<0,   /**< If fast_x_aps_en is set, protect_en is egnored, the bit must from hardware */
    SYS_WB_APS_FLAG_NEXT_W_APS_EN               = 1U<<1,   /**< Use next_w_aps neglect working_gport */
    SYS_WB_APS_FLAG_NEXT_P_APS_EN               = 1U<<2,   /**< Use next_w_aps neglect protection_gport */
    SYS_WB_APS_FLAG_L2_APS_PHYSICAL_ISOLATED    = 1U<<3,   /**< Indicate port aps use physical isolated path */
    SYS_WB_APS_FLAG_HW_BASED_FAILOVER_EN        = 1U<<4,   /**< Used for 1-level link fast aps, by hardware switch path when link down */
    SYS_WB_APS_FLAG_RAPS                        = 1U<<5,   /**< Used for raps */
    MAX_SYS_WB_APS_FLAG
};
typedef enum sys_wb_aps_flag_e sys_wb_aps_flag_t;

struct sys_wb_aps_node_s
{
    /*key*/
    uint16 group_id;
    uint8   rsv[2];
    uint32 calc_key_len[0];

    /*data*/
    uint16 w_l3if_id;                   /**< working l3if id */
    uint16 p_l3if_id;                   /**< protection l3if id */
    uint16 raps_group_id;
    uint16 flag;                /**< it's the sys_wb_aps_flag_t value */
};
typedef struct sys_wb_aps_node_s sys_wb_aps_node_t;

struct sys_wb_aps_mcast_node_s
{
    /*key*/
    uint16 group_id;    /* mcast_group_id */
    uint8   rsv[2];
    uint32 calc_key_len[0];

    /*data*/
    uint32 raps_nhid;  /* mcast_nhid */
};
typedef struct sys_wb_aps_mcast_node_s sys_wb_aps_mcast_node_t;

/****************************************************************
*
* stacking module
*
****************************************************************/
#define SYS_WB_STK_MAX_TRUNK_ID  63
#define SYS_WB_STK_TRUNK_BMP_NUM   ((SYS_WB_STK_MAX_TRUNK_ID + CTC_UINT32_BITS) / CTC_UINT32_BITS)

enum sys_wb_appid_stacking_subid_e
{
    SYS_WB_APPID_STACKING_SUBID_MASTER,
    SYS_WB_APPID_STACKING_SUBID_TRUNK,
    SYS_WB_APPID_STACKING_SUBID_MCAST_DB,
    SYS_WB_APPID_STACKING_SUBID_MAX
};
typedef enum sys_wb_appid_stacking_subid_e sys_wb_appid_stacking_subid_t;


struct sys_wb_stacking_master_s
{
    /*key*/
    uint8 lchip;
    uint8 rsv[3];
    uint32 calc_key_len[0];

    /*data*/
    uint8 ipv4_encap;
    uint8 binding_trunk;
    uint8 bind_mcast_en;
    uint8 rsv1;
    uint32 stacking_mcast_offset;
};
typedef struct sys_wb_stacking_master_s sys_wb_stacking_master_t;

struct sys_wb_stacking_mcast_db_s
{
    /*key*/
    uint8   type;
    uint8   rsv;
    uint16  id;
    uint32 calc_key_len[0];

    /*data*/
    uint32   head_met_offset;
    uint32   tail_met_offset;

    uint8 append_en;
    uint8 alloc_id;
    uint8 rsv1[2];
    uint32 last_tail_offset;

    uint32   trunk_bitmap[SYS_WB_STK_TRUNK_BMP_NUM];
};
typedef struct sys_wb_stacking_mcast_db_s sys_wb_stacking_mcast_db_t;

struct sys_wb_stacking_hdr_encap_s
{
    uint32 hdr_flag;   /**< encap cloud header type, refer to ctc_stacking_hdr_flag_t*/

    mac_addr_t mac_da; /**< cloud header mac da if exist l2 header*/
    mac_addr_t mac_sa; /**< cloud header mac sa if exist l2 header*/

    uint8  vlan_valid; /**< if set, cloud header will encap vlan when using l2 header*/
    uint8  rsv0;
    uint16 vlan_id;   /**< cloud header vlan id if exist vlan */

    union
    {
        ip_addr_t ipv4;             /**< cloud heade ipv4 destination address*/
        ipv6_addr_t ipv6;           /**< cloud heade ipv6 destination address*/
    } ipda;

};
typedef struct sys_wb_stacking_hdr_encap_s sys_wb_stacking_hdr_encap_t;

struct sys_wb_stacking_trunk_s
{
    /*key*/
    uint8 trunk_id;
    uint8 rsv[3];
    uint32 calc_key_len[0];

    /*data*/
    uint8 max_mem_cnt;
    uint8 mode;
    uint8 replace_en;
    uint8 rsv1;
    sys_wb_stacking_hdr_encap_t encap_hdr;
};
typedef struct sys_wb_stacking_trunk_s sys_wb_stacking_trunk_t;


/****************************************************************
*
* ptp module
*
****************************************************************/
enum sys_wb_appid_ptp_subid_e
{
    SYS_WB_APPID_PTP_SUBID_MASTER,
    SYS_WB_APPID_PTP_SUBID_MAX
};
typedef enum sys_wb_appid_ptp_subid_e sys_wb_appid_ptp_subid_t;

enum sys_wb_ptp_device_type_e
{
    SYS_WB_PTP_DEVICE_NONE = 0,
    SYS_WB_PTP_DEVICE_OC,
    SYS_WB_PTP_DEVICE_BC,
    SYS_WB_PTP_DEVICE_E2E_TC,
    SYS_WB_PTP_DEVICE_P2P_TC,

    MAX_SYS_WB_PTP_DEVICE
};
typedef enum sys_wb_ptp_device_type_e sys_wb_ptp_device_type_t;

struct sys_wb_ptp_master_s
{
    /*key*/
    uint8   lchip;
    uint8   rsv[3];
    uint32 calc_key_len[0];

    /*data*/
    uint16 cache_aging_time;
    uint8 rsv1[2];
    uint32 tod_1pps_delay;
    sys_wb_ptp_device_type_t device_type;  /* ctc_ptp_device_type_t */
};
typedef struct sys_wb_ptp_master_s sys_wb_ptp_master_t;

/****************************************************************
*
* sync_ether module
*
****************************************************************/
#define SYS_WB_SYNCETHER_CLOCK_MAX 2

enum sys_wb_appid_syncether_subid_e
{
    SYS_WB_APPID_SYNCETHER_SUBID_MASTER,
    SYS_WB_APPID_SYNCETHER_SUBID_MAX
};
typedef enum sys_wb_appid_syncether_subid_e sys_wb_appid_syncether_subid_t;

struct sys_wb_syncether_master_s
{
    /* key */
    uint32 lchip;
    uint32 calc_key_len[0];

    /* data */
    uint8 recovered_clock_lport[SYS_WB_SYNCETHER_CLOCK_MAX];
    uint8 rsv1[2];
};
typedef struct sys_wb_syncether_master_s sys_wb_syncether_master_t;

#ifdef __cplusplus
}
#endif

#endif

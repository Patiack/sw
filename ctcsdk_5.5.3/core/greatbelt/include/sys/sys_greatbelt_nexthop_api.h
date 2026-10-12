/**
 @file sys_greatbelt_nexthop_api.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GREATBELT_NEXTHOP_API_H_
#define _SYS_GREATBELT_NEXTHOP_API_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_linklist.h"
#include "ctc_hash.h"
#include "ctc_vlan.h"
#include "ctc_nexthop.h"
#include "ctc_stats.h"



/*global reserved nexthop */
#define SYS_NH_RESOLVED_NHID_FOR_NONE            0
#define SYS_NH_RESOLVED_NHID_FOR_DROP            1
#define SYS_NH_RESOLVED_NHID_FOR_TOCPU           2

#define SYS_NH_RESOLVED_NHID_MAX              3

#define SYS_NHID_EXTERNAL_MAX                  8192
#define SYS_NHID_INTERNAL_START  (SYS_NHID_EXTERNAL_MAX)
#define SYS_NH_INVALID_OFFSET        0x7FFF
#define SYS_NH_INVALID_NHID            0xFFFFFFFF

enum sys_nh_param_mcast_member_type_s
{
    SYS_NH_PARAM_BRGMC_MEM_LOCAL = 0,
    SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE,
    SYS_NH_PARAM_IPMC_MEM_LOCAL,
    SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH,
    SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID,
    SYS_NH_PARAM_MCAST_MEM_REMOTE,
    SYS_NH_PARAM_BRGMC_MEM_RAPS,
    SYS_NH_PARAM_MCAST_MEM_LOCAL_LOGIC_REP,
    SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP,
    SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH,
    SYS_NH_PARAM_MCAST_MEM_INVALID
};
typedef enum sys_nh_param_mcast_member_type_s sys_nh_param_mcast_member_type_t;

struct sys_nh_param_mcast_member_s
{
    sys_nh_param_mcast_member_type_t member_type;
    uint32  ref_nhid; /*Reference other nhid, eg egress vlan translation nhid*/
    uint16  destid; /*local portid/LAGID without 0x1F(eg.0x1F01)/aps bridge groupid/trunk id(stacking)*/
    uint16  vid; /*For IPMC*/
    uint8   is_linkagg;

    uint8   l3if_type; /*ctc_l3if_type_t*/
    uint8   is_protection_path; /*Used for aps bridge*/
    uint16  logic_port;
    uint8   is_logic_port; /*if set,Met will do logic port check,apply to VPLS/WLAN/PBB/TRILL,etc.*/
    uint8   is_reflective;      /**< [GB] Mcast reflective enable ,defualt not en */
    uint8   is_mirror;
    uint8   is_horizon_split;
    uint8   mtu_no_chk;
    uint8   rsv[1];
};
typedef struct sys_nh_param_mcast_member_s sys_nh_param_mcast_member_t;

enum sys_nh_param_brguc_sub_type_e
{
    SYS_NH_PARAM_BRGUC_SUB_TYPE_NONE =  0,
    SYS_NH_PARAM_BRGUC_SUB_TYPE_BASIC = 1,
    SYS_NH_PARAM_BRGUC_SUB_TYPE_BYPASS = 2,

    /*Output vlan edit*/
    SYS_NH_PARAM_BRGUC_SUB_TYPE_VLAN_EDIT = 3,
    SYS_NH_PARAM_BRGUC_SUB_TYPE_APS_VLAN_EDIT = 4,

    SYS_NH_PARAM_BRGUC_SUB_TYPE_MAX
};
typedef enum sys_nh_param_brguc_sub_type_e sys_nh_param_brguc_sub_type_t;

struct sys_nh_param_mcast_group_s
{
    uint32                      nhid;
    uint32                      opcode;  /*sys_nh_param_mcast_opcode_t */
    sys_nh_param_mcast_member_t mem_info;
    uint32             fwd_offset;
    uint16             stats_ptr;
    uint32                      stats_id;
    uint8                       stats_valid;
    uint8                       dsfwd_valid;
    uint8                       is_nhid_valid;
    uint8                       is_mirror;
};
typedef struct sys_nh_param_mcast_group_s sys_nh_param_mcast_group_t;

enum sys_nh_entry_change_type_e
{
    SYS_NH_CHANGE_TYPE_NULL,

    SYS_NH_CHANGE_TYPE_FWD_TO_UNROV,
    SYS_NH_CHANGE_TYPE_UNROV_TO_FWD,
    SYS_NH_CHANGE_TYPE_UPDATE_FWD_ATTR,

    SYS_NH_CHANGE_TYPE_FWD_TO_DISCARD,
    SYS_NH_CHANGE_TYPE_DISCARD_TO_FWD,

    SYS_NH_CHANGE_TYPE_ADD_DYNTBL,

    SYS_NH_CHANGE_TYPE_NH_DELETE,
    SYS_NH_CHANGE_TYPE_INVALID
};
typedef enum sys_nh_entry_change_type_e sys_nh_entry_change_type_t;

struct sys_nh_brguc_nhid_info_s
{
    uint32 nh_type_flag;
    uint32 brguc_untagged_nhid;
    uint32 brguc_nhid;
    uint32 bypass_nhid;
    uint32 ram_pkt_nhid;
    uint32 srv_queue_nhid;
};
typedef struct sys_nh_brguc_nhid_info_s sys_nh_brguc_nhid_info_t;

struct sys_nh_info_dsnh_s
{
    uint16 dsnh_offset;

    uint8    merge_dsfwd;/*0:may need dsfwd; 1.don't dsfwd;2.must need dsfwd*/
    uint8    is_mcast;
    uint16   dest_id; /*UcastId or McastId*/

    uint8    drop_pkt;
    uint8    nexthop_ext;
    uint8    dest_chipid; /*Destination global chip id*/
    uint8    dsfwd_valid;

    uint8    aps_en;     /*if enable, destid is aps group id*/
    uint8    stats_en;   /*if enable , stats is enable */
    uint8    rsv0[2];

    uint32   b_protection;     /*if enable, destid is aps group id*/
    uint32   mpls_out_label;   /*mpls out label*/
    /*ecmp info*/
    uint8    ecmp_valid;
    uint8    ecmp_cnt;    /*include valid & unresoved nh member*/
    uint16   ecmp_num;    /* == max ecmp number (sys_nh_get_max_ecmp()) in (ecmp_mode == 1)or valid ecmp number (valid_cnt)*/

    uint8    valid_cnt;     /*valid member*/
    uint8    nh_entry_type; /*nexthop type*/
    uint8    is_ivi;        /*sub type is ivi of ip-tunnel*/
    uint8    get_by_oam;
    uint32   nh_array[SYS_GB_MAX_ECPN]; /*valid nh array[valid_cnt] -->unresoved nh*/
    uint8    is_nat;

    /* ip-tunnel info */
    uint8   re_route;
    uint8   rev1[2];

    uint32 gport;

};
typedef struct sys_nh_info_dsnh_s sys_nh_info_dsnh_t;

struct sys_nh_update_dsnh_param_s
{
    uint8     isAddDsFwd;
    uint8     isAddDsL2Edit;
    uint8     stats_en;
    uint8     nh_entry_type;
    uint32    stats_id;
    void*     data;
    uint32 (* updateAd) (uint8 lchip, void* data, void* change_nh_param);

};
typedef struct sys_nh_update_dsnh_param_s sys_nh_update_dsnh_param_t;


struct sys_nh_info_oam_aps_s
{
    uint16  aps_group_id;
    uint8   nexthop_is_8w;
    uint32  dsnh_offset;
};
typedef struct sys_nh_info_oam_aps_s sys_nh_info_oam_aps_t;


struct sys_nh_eloop_node_s
{
    ctc_slistnode_t head;
    uint16 nhid;
    uint8 is_l2edit;
    uint8 rsv[1];
    uint16 edit_ptr;
    uint16 ref_cnt;
};
typedef struct sys_nh_eloop_node_s sys_nh_eloop_node_t;

struct sys_nh_repli_offset_node_s
{
    ctc_slistnode_t head;
	uint32 start_offset;
    uint32 bitmap;
    uint32 dest_gport;
    uint32 group_id;
};
typedef struct sys_nh_repli_offset_node_s sys_nh_repli_offset_node_t;


/*Common Functions*/
extern int32
sys_greatbelt_nh_api_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg);
extern int32
sys_greatbelt_nh_api_deinit(uint8 lchip);

extern int32
sys_greatbelt_nh_get_dsfwd_offset(uint8 lchip, uint32 nhid, uint32* p_dsfwd_offset);
extern int32
sys_greatbelt_nh_get_bypass_dsnh_offset(uint8 lchip, uint32* p_offset);

extern int32
sys_greatbelt_nh_get_mirror_dsnh_offset(uint8 lchip, uint32* p_offset);
extern int32
sys_greatbelt_nh_get_ipmc_l2edit_offset(uint8 lchip, uint16* p_l2edit_offset);

/*Ucast bridge functions*/
extern int32
sys_greatbelt_brguc_nh_create(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type);
extern int32
sys_greatbelt_brguc_nh_delete(uint8 lchip, uint16 gport);

extern int32
sys_greatbelt_brguc_get_nhid(uint8 lchip, uint16 gport, sys_nh_brguc_nhid_info_t* p_brguc_nhid_info);

extern int32
sys_greatbelt_l2_get_ucast_nh(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid);

extern int32
sys_greatbelt_brguc_get_dsfwd_offset(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type,
                                     uint32* fwd_offset);
extern int32
sys_greatbelt_brguc_update_speed(uint8 lchip, uint16 gport, uint8 speed);

extern int32
sys_greatbelt_nh_get_dsnh_offset_by_nhid(uint8 lchip, uint32 nhid,  uint16* dsnh_offset, uint8 * p_use_dsnh8w);

extern int32
_sys_greatbelt_nh_get_info_by_nhid(uint8 lchip, uint32 nhid, uint8* p_use_hvpls);

extern int32
sys_greatbelt_nh_get_l3ifid(uint8 lchip, uint32 nhid, uint16* p_l3ifid);
extern int32
sys_greatbelt_nh_get_port(uint8 lchip, uint32 nhid, uint8* aps_brg_en, uint16* gport);

extern int32
sys_greatbelt_nh_is_cpu_nhid(uint8 lchip, uint32 nhid, uint8 *is_cpu_nh);

extern int32
sys_greatbelt_nh_is_logic_repli_en(uint8 lchip, uint32 nhid, uint8 *is_repli_en);

extern int32
sys_greatbelt_nh_get_xgpon_en(uint8 lchip, uint8* enable);

extern int32
sys_greatbelt_nh_get_statsptr(uint8 lchip, uint32 nhid,  uint16* stats_ptr);

/*Egress Vlan translation*/
extern int32
sys_greatbelt_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid, uint16 gport,
                                         ctc_vlan_egress_edit_info_t* p_vlan_info, uint32 dsnh_offset,
                                         ctc_vlan_edit_nh_param_t* p_nh_param);
extern int32
sys_greatbelt_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid);

/*APS Egress Vlan translation*/
extern int32
sys_greatbelt_aps_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid,
                                             uint32 dsnh_offset, uint16 aps_bridge_id,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_working_path,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_protection_path);
extern int32
sys_greatbelt_aps_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid);

/*Mcast bridge*/
extern int32
sys_greatbelt_mcast_nh_get_nhid(uint8 lchip, uint32 group_id, uint32* nhid);
extern int32
sys_greatbelt_mcast_nh_create(uint8 lchip, uint32 groupid,  sys_nh_param_mcast_group_t* p_nh_mcast_group);

extern int32
sys_greatbelt_mcast_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_mcast_nh_update(uint8 lchip, sys_nh_param_mcast_group_t* p_nh_mcast_group);

/*IPUC*/
extern int32
sys_greatbelt_ipuc_nh_create(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param);
extern int32
sys_greatbelt_ipuc_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_ipuc_nh_update(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param, sys_nh_entry_change_type_t update_type);

extern int32
sys_greatbelt_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param);

extern int32
sys_greatbelt_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id);

extern int32
sys_greatbelt_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param);

extern int32
sys_greatbelt_nh_update_nhInfo(uint8 lchip, uint32 nhid, sys_nh_update_dsnh_param_t* p_update_dsnh);

extern int32
sys_greatbelt_nh_get_nhinfo(uint8 lchip, uint32 nhid, sys_nh_info_dsnh_t* p_nhinfo);

extern int32
sys_greatbelt_nh_set_nh_drop(uint8 lchip, uint32 nhid, ctc_nh_drop_info_t *p_nh_drop);

extern int32
sys_greatbelt_mpls_nh_create_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param);
extern int32
sys_greatbelt_mpls_nh_remove_tunnel_label(uint8 lchip, uint16 tunnel_id);
extern int32
sys_greatbelt_mpls_nh_update_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param);

extern int32
sys_greatbelt_mpls_nh_swap_tunnel(uint8 lchip, uint16 old_tunnel_id,uint16 new_tunnel_id);

extern int32
sys_greatbelt_mpls_nh_create(uint8 lchip, uint32 nhid,
                             ctc_mpls_nexthop_param_t* p_nh_param);
extern int32
sys_greatbelt_mpls_nh_delete(uint8 lchip, uint32 nhid);
extern int32
sys_greatbelt_mpls_nh_update(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param,
                             sys_nh_entry_change_type_t change_type);

extern int32
sys_greatbelt_mpls_nh_update_oam_en(uint8 lchip, uint32 nhid, uint32 outer_label, uint8 oam_en);

extern int32
sys_greatbelt_iloop_nh_create(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param);
extern int32
sys_greatbelt_iloop_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_rspan_nh_create(uint8 lchip, uint32* nhid, uint32 dsnh_offset,
                              ctc_rspan_nexthop_param_t* p_nh_param);
extern int32
sys_greatbelt_rspan_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_dump(uint8 lchip, uint32 nhid, bool detail);

extern int32
sys_greatbelt_ecmp_nh_create(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* pdata);
extern int32
sys_greatbelt_ecmp_nh_delete(uint8 lchip, uint32 nhid);
extern int32
sys_greatbelt_ecmp_nh_update(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* pdata);

extern int32
sys_greatbelt_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp);

extern int32
sys_greatbelt_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp);

extern int32
sys_greatbelt_nh_set_dsfwd_mode(uint8 lchip, uint8 have_dsfwd);
extern int32
sys_greatbelt_nh_get_dsfwd_mode(uint8 lchip, uint8* have_dsfwd);

extern int32
sys_greatbelt_nh_add_stats(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_del_stats(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_ip_tunnel_nh_create(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

extern int32
sys_greatbelt_ip_tunnel_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_ip_tunnel_nh_update(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

extern int32
sys_greatbelt_nh_add_misc(uint8 lchip, uint32* nhid, ctc_misc_nh_param_t* p_nh_param, bool is_internal_nh);
extern int32
sys_greatbelt_nh_remove_misc(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_create_mcast_aps(uint8 lchip, sys_nh_info_oam_aps_t* p_oam_aps , uint32* group_id);

extern int32
sys_greatbelt_nh_delete_mcast_aps(uint8 lchip, uint32 group_id);


extern int32
sys_greatbelt_nh_add_eloop_edit(uint8 lchip, uint32 nhid, bool is_l2edit, uint32* p_edit_ptr);

extern int32
sys_greatbelt_nh_remove_eloop_edit(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_update_eloop_edit(uint8 lchip, uint32 nhid);

extern int32
sys_greatbelt_nh_free_dsfwd_offset(uint8 lchip, uint32 nhid);

extern bool
sys_greatbelt_nh_is_ipmc_logic_rep_enable(uint8 lchip);

extern int32
sys_greatbelt_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable);


/*#endif*/
#ifdef __cplusplus
}
#endif

#endif /*_SYS_NEXTHOP_H_*/


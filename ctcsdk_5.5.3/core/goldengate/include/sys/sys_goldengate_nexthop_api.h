/**
 @file sys_goldengate_nexthop_api.h

 @date 2011-11-07

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_NEXTHOP_API_H_
#define _SYS_GOLDENGATE_NEXTHOP_API_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"
#include "ctc_linklist.h"
#include "ctc_hash.h"
#include "ctc_vlan.h"
#include "ctc_nexthop.h"
#include "ctc_stats.h"
#include "ctc_vector.h"

/*global reserved nexthop */
#define SYS_NH_RESOLVED_NHID_FOR_NONE            0
#define SYS_NH_RESOLVED_NHID_FOR_DROP            1
#define SYS_NH_RESOLVED_NHID_FOR_TOCPU           2

#define SYS_NH_RESOLVED_NHID_MAX              3

#define SYS_NHID_EXTERNAL_MAX                  8192
#define SYS_NHID_INTERNAL_START  (SYS_NHID_EXTERNAL_MAX)
#define SYS_NH_INVALID_OFFSET        0xFFFF
#define SYS_NH_INVALID_NHID            0xFFFFFFFF
#define SYS_NH_MET_DROP_UCAST_ID          SYS_RSV_PORT_DROP_ID

enum sys_nh_param_mcast_member_type_s
{
    SYS_NH_PARAM_BRGMC_MEM_LOCAL = 0,
    SYS_NH_PARAM_BRGMC_MEM_LOCAL_WITH_APS_BRIDGE,
    SYS_NH_PARAM_IPMC_MEM_LOCAL,
    SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH,
    SYS_NH_PARAM_MCAST_MEM_LOCAL_WITH_NH_DESTID,
    SYS_NH_PARAM_MCAST_MEM_REMOTE,
    SYS_NH_PARAM_BRGMC_MEM_RAPS,
    SYS_NH_PARAM_IPMC_MEM_LOCAL_LOGIC_REP,
    SYS_NH_PARAM_MCAST_MEM_MIRROR_WITH_NH,
    SYS_NH_PARAM_IPMC_MEM_LOOP_BACK,
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
    uint8   lchip; /*Local chip this member will be added to */
    uint8   l3if_type; /*ctc_l3if_type_t*/
    uint8   is_protection_path; /*Used for aps bridge*/
    uint16  logic_port;
    uint8   is_logic_port; /*if set,Met will do logic port check,apply to VPLS/WLAN/PBB/TRILL,etc.*/
    uint8   is_reflective;      /**< [GB] Mcast reflective */
    uint8   is_mirror;
    uint8   is_horizon_split;
    uint8   is_src_port_check_dis;
    uint8   leaf_check_en;
    uint8   mtu_no_chk;
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
    uint32                     fwd_offset;
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
    uint8    nh_entry_type; /*nexthop type*/
    uint8    dest_chipid; /*Destination global chip id*/
    uint16   dest_id; /*dest_id of destmap ,UcastId/McastId/aps group id*/
    uint32   dsnh_offset;
    uint32   dsfwd_offset;
    uint8    nexthop_ext;
    uint8    adjust_len;
    uint8    is_mcast;
    uint8    drop_pkt;
    uint32   gport; /*ucast output port */

    uint8    dsfwd_valid;
    uint8    aps_en;     /*if enable, destid is aps group id*/
    uint8    merge_dsfwd; /*0:may need dsfwd; 1.don't dsfwd;2.must need dsfwd */
    uint8    is_ivi;        /*sub type is ivi of ip-tunnel*/
    uint8    stats_en;

    /*ecmp info*/
    uint8    ecmp_valid;
    uint8    is_ecmp_intf;
    uint16   ecmp_group_id;
    uint8    ecmp_num;
    uint8    valid_cnt;   /*valid ecmp member*/
    uint8    ecmp_cnt;    /*include valid & unresoved ecmp member*/
    uint32   nh_array[SYS_GG_MAX_ECPN]; /*valid nh array[valid_cnt] -->unresoved nh*/

    /*oam aps info*/
    uint8    protection_path;   /*[input]mep on PW label*/
    uint8    mep_on_tunnel;	    /*[output]mep on tunnel label*/
    uint16   oam_aps_group_id;  /*if mep on PW label,oam_aps_group_id is tunnel aps group id;
    if mep on tunnel label, oam_aps_group_id is tunnel group id */
    uint16   oam_aps_en;
    uint8 have_l2edit;

    /* ip-tunnel info */
    uint8   re_route;
    
    uint8   individual_nexthop;/*if set, means two dsnexthop for aps*/

};
typedef struct sys_nh_info_dsnh_s sys_nh_info_dsnh_t;

struct sys_nh_update_dsnh_param_s
{

    void*     data;
    int32 (* updateAd) (uint8 lchip, void* data, void* change_nh_param);

};
typedef struct sys_nh_update_dsnh_param_s sys_nh_update_dsnh_param_t;


struct sys_nh_update_oam_info_s
{
    uint8    is_protection_path;
    uint8    lock_en;
	uint8    lm_en;  /*if lm_en,indicate oam_mep_index is MEP identify */
	uint16   oam_mep_index;
};
typedef struct sys_nh_update_oam_info_s sys_nh_update_oam_info_t;


struct sys_hbnh_brguc_node_info_s
{
    uint32 nhid_brguc;
    uint32 nhid_bypass;

};
typedef struct sys_hbnh_brguc_node_info_s sys_hbnh_brguc_node_info_t;

struct sys_goldengate_brguc_info_s
{
    ctc_vector_t* brguc_vector; /*Maintains ucast bridge DB(no egress vlan trans)*/
    sal_mutex_t*  brguc_mutex;
};
typedef struct sys_goldengate_brguc_info_s sys_goldengate_brguc_info_t;

struct sys_nh_eloop_node_s
{
    ctc_slistnode_t head;
    uint32 nhid;
    uint32 edit_ptr;
    uint8 is_l2edit;
    uint8 rsv[1];
    uint16 ref_cnt;
};
typedef struct sys_nh_eloop_node_s sys_nh_eloop_node_t;

struct sys_goldengate_nh_api_master_s
{
    sys_goldengate_brguc_info_t brguc_info;
    uint32 max_external_nhid;
};
typedef struct sys_goldengate_nh_api_master_s sys_goldengate_nh_api_master_t;

extern sys_goldengate_nh_api_master_t* p_gg_nh_api_master[CTC_MAX_LOCAL_CHIP_NUM];

/*Common Functions*/
extern int32
sys_goldengate_nh_api_init(uint8 lchip, ctc_nh_global_cfg_t* nh_cfg);
extern int32
sys_goldengate_nh_api_deinit(uint8 lchip);

extern int32
sys_goldengate_nh_get_dsfwd_offset(uint8 lchip, uint32 nhid, uint32* p_offset);
extern int32
sys_goldengate_nh_get_bypass_dsnh_offset(uint8 lchip, uint32* p_offset);

extern int32
sys_goldengate_nh_get_mirror_dsnh_offset(uint8 lchip, uint32* p_offset);
extern int32
sys_goldengate_nh_get_ipmc_l2edit_offset(uint8 lchip, uint16* p_l2edit_offset);
extern int32
sys_goldengate_nh_get_emcp_if_l2edit(uint8 lchip, void** p_l2edit_ptr);

/*Ucast bridge functions*/
extern int32
sys_goldengate_brguc_nh_create(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type);
extern int32
sys_goldengate_brguc_nh_delete(uint8 lchip, uint16 gport);

extern int32
sys_goldengate_l2_get_ucast_nh(uint8 lchip, uint16 gport, ctc_nh_param_brguc_sub_type_t nh_type, uint32* nhid);

extern int32
sys_goldengate_nh_get_mcast_nh(uint8 lchip, uint32 group_id, uint32* nhid);

extern int32
sys_goldengate_nh_get_dsnh_offset_by_nhid(uint8 lchip, uint32 nhid,  uint32 *p_dsnh_offset, uint8 * p_use_dsnh8w);


extern int32
sys_goldengate_nh_get_l3ifid(uint8 lchip, uint32 nhid, uint16* p_l3ifid);
extern int32
sys_goldengate_nh_get_mcast_member(uint8 lchip, uint32 nhid, ctc_nh_info_t* p_nh_info);

extern int32
sys_goldengate_nh_get_port(uint8 lchip, uint32 nhid, uint8* aps_brg_en, uint32* gport);

extern bool
sys_goldengate_nh_is_cpu_nhid(uint8 lchip, uint32 nhid);

/*Egress Vlan translation*/
extern int32
sys_goldengate_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid, uint16 gport,
                                         ctc_vlan_egress_edit_info_t* p_vlan_info,
                                         uint32 dsnh_offset,
                                         ctc_vlan_edit_nh_param_t* p_nh_param);
extern int32
sys_goldengate_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid);

/*APS Egress Vlan translation*/
extern int32
sys_goldengate_aps_egress_vlan_edit_nh_create(uint8 lchip, uint32 nhid,
                                             uint32 dsnh_offset, uint16 aps_bridge_id,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_working_path,
                                             ctc_vlan_egress_edit_info_t* p_vlan_info_protection_path);
extern int32
sys_goldengate_aps_egress_vlan_edit_nh_delete(uint8 lchip, uint32 nhid);

/*Mcast bridge*/
extern int32
sys_goldengate_mcast_nh_create(uint8 lchip, uint32 groupid,  sys_nh_param_mcast_group_t* p_nh_mcast_group);

extern int32
sys_goldengate_mcast_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_mcast_nh_update(uint8 lchip, sys_nh_param_mcast_group_t* p_nh_mcast_group);

/*
 Next Hop Router Mac
*/
extern int32
sys_goldengate_nh_add_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_param);
extern int32
sys_goldengate_nh_remove_nexthop_mac(uint8 lchip, uint16 arp_id);
extern int32
sys_goldengate_nh_update_nexthop_mac(uint8 lchip, uint16 arp_id, ctc_nh_nexthop_mac_param_t* p_new_param);

/*IPUC*/
extern int32
sys_goldengate_ipuc_nh_create(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param);
extern int32
sys_goldengate_ipuc_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_ipuc_nh_update(uint8 lchip, uint32 nhid, ctc_ip_nh_param_t* p_nh_param, sys_nh_entry_change_type_t update_type);

extern int32
sys_goldengate_nh_bind_dsfwd_cb(uint8 lchip, uint32 nhid, sys_nh_update_dsnh_param_t* p_update_dsnh);

extern int32
sys_goldengate_nh_get_aps_working_path(uint8 lchip, uint32 nhid, uint32* gport, uint32* nexthop_ptr, bool* p_protect_en);
extern int32
sys_goldengate_nh_get_nhinfo(uint8 lchip, uint32 nhid, sys_nh_info_dsnh_t* p_nhinfo);

extern int32
sys_goldengate_mpls_nh_create_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param);
extern int32
sys_goldengate_mpls_nh_remove_tunnel_label(uint8 lchip, uint16 tunnel_id);
extern int32
sys_goldengate_mpls_nh_update_tunnel_label(uint8 lchip, uint16 tunnel_id, ctc_mpls_nexthop_tunnel_param_t* p_tunnel_param);

extern int32
sys_goldengate_mpls_nh_swap_tunnel(uint8 lchip, uint16 old_tunnel_id,uint16 new_tunnel_id);

extern int32
sys_goldengate_mpls_nh_create(uint8 lchip, uint32 nhid,
                             ctc_mpls_nexthop_param_t* p_nh_param);
extern int32
sys_goldengate_mpls_nh_delete(uint8 lchip, uint32 nhid);
extern int32
sys_goldengate_mpls_nh_update(uint8 lchip, uint32 nhid, ctc_mpls_nexthop_param_t* p_nh_param,
                             sys_nh_entry_change_type_t change_type);

extern int32
sys_goldengate_mpls_nh_update_oam_en(uint8 lchip, uint32 nhid,sys_nh_update_oam_info_t *p_oam_info);

extern int32
sys_goldengate_iloop_nh_create(uint8 lchip, uint32 nhid, ctc_loopback_nexthop_param_t* p_nh_param);
extern int32
sys_goldengate_iloop_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_rspan_nh_create(uint8 lchip, uint32* nhid, uint32 dsnh_offset,
                              ctc_rspan_nexthop_param_t* p_nh_param);
extern int32
sys_goldengate_rspan_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_nh_dump_mcast(uint8 lchip, uint32 nhid, bool detail);

extern int32
sys_goldengate_nh_dump(uint8 lchip, uint32 nhid, bool detail);

extern int32
sys_goldengate_ecmp_nh_create(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* pdata);
extern int32
sys_goldengate_ecmp_nh_delete(uint8 lchip, uint32 nhid);
extern int32
sys_goldengate_ecmp_nh_update(uint8 lchip, uint32 nhid, ctc_nh_ecmp_nh_param_t* pdata);

extern int32
sys_goldengate_nh_set_max_ecmp(uint8 lchip, uint16 max_ecmp);

extern int32
sys_goldengate_nh_get_max_ecmp(uint8 lchip, uint16* max_ecmp);

extern int32
sys_goldengate_nh_set_dsfwd_mode(uint8 lchip, uint8 have_dsfwd);
extern int32
sys_goldengate_nh_get_dsfwd_mode(uint8 lchip, uint8* have_dsfwd);


extern int32
sys_goldengate_ip_tunnel_nh_create(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

extern int32
sys_goldengate_ip_tunnel_nh_delete(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_ip_tunnel_nh_update(uint8 lchip, uint32 nhid, ctc_ip_tunnel_nh_param_t* p_nh_param);

extern int32
sys_goldengate_nh_add_misc(uint8 lchip, uint32* nhid, ctc_misc_nh_param_t* p_nh_param, bool is_internal_nh);
extern int32
sys_goldengate_nh_remove_misc(uint8 lchip, uint32 nhid);

extern int32
sys_goldengate_nh_trill_create(uint8 lchip, uint32 nhid, ctc_nh_trill_param_t* p_nh_param);
extern int32
sys_goldengate_nh_trill_remove(uint8 lchip, uint32 nhid);
extern int32
sys_goldengate_nh_trill_update(uint8 lchip, uint32 nhid, ctc_nh_trill_param_t* p_nh_param);
extern int32
sys_goldengate_nh_vxlan_vni_init(uint8 lchip, uint32 max_fid);

extern bool
sys_goldengate_nh_is_ipmc_logic_rep_enable(uint8 lchip);

extern int32
sys_goldengate_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable);
extern int32
sys_goldengate_nh_update_misc(uint8 lchip, uint32 nhid, ctc_misc_nh_param_t* p_nh_param, sys_nh_entry_change_type_t update_type);

/*#endif*/
#ifdef __cplusplus
}
#endif

#endif /*_SYS_NEXTHOP_H_*/


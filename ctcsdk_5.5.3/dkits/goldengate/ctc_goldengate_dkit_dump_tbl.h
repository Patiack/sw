#ifndef _CTC_GOLDENGATE_DKIT_TBL_H_
#define _CTC_GOLDENGATE_DKIT_TBL_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"

struct ctc_dkits_tcam_key_inf_s
{
    tbls_id_t tblid;
    uint8     key_type;
};
typedef struct ctc_dkits_tcam_key_inf_s ctc_dkits_tcam_key_inf_t;

struct ctc_dkits_gg_tcam_resc_inf_s
{
    tbls_id_t tblid;
    uint32    key_type;
    uint32    sid;
};
typedef struct ctc_dkits_gg_tcam_resc_inf_s ctc_dkits_gg_tcam_resc_inf_t;

struct ctc_dkits_hash_tbl_info_s
{
    uint32                         valid;
    uint32                         hash_type;
    tbls_id_t                      ad_tblid;
    fld_id_t                       ad_idx_fldid;    /*GB/GG*/
};
typedef struct ctc_dkits_hash_tbl_info_s ctc_dkits_hash_tbl_info_t;

extern tbls_id_t gg_fib_hash_bridge_key_tbl[];
extern tbls_id_t gg_fib_hash_ipuc_ipv4_key_tbl[];
extern tbls_id_t gg_fib_hash_ipuc_ipv6_key_tbl[];
extern tbls_id_t gg_fib_hash_ipmc_ipv4_key_tbl[];
extern tbls_id_t gg_fib_hash_ipmc_ipv6_key_tbl[];
extern tbls_id_t gg_fib_hash_fcoe_key_tbl[];
extern tbls_id_t gg_fib_hash_trill_key_tbl[];
extern tbls_id_t gg_scl_hash_bridge_key_tbl[];
extern tbls_id_t gg_scl_hash_ipv4_key_tbl[];
extern tbls_id_t gg_scl_hash_ipv6_key_tbl[];
extern tbls_id_t gg_scl_hash_iptunnel_key_tbl[];
extern tbls_id_t gg_scl_hash_nvgre_tunnel_key_tbl[];
extern tbls_id_t gg_scl_hash_vxlan_tunnel_key_tbl[];
extern tbls_id_t gg_scl_hash_mpls_tunnel_key_tbl[];
extern tbls_id_t gg_scl_hash_pbb_tunnel_key_tbl[];
extern tbls_id_t gg_scl_hash_trill_tunnel_key_tbl[];
extern tbls_id_t gg_ipfix_hash_bridge_key_tbl[];
extern tbls_id_t gg_ipfix_hash_l2l3_key_tbl[];
extern tbls_id_t gg_ipfix_hash_ipv4_key_tbl[];
extern tbls_id_t gg_ipfix_hash_ipv6_key_tbl[];
extern tbls_id_t gg_ipfix_hash_mpls_key_tbl[];
extern tbls_id_t gg_flow_hash_bridge_key_tbl[];
extern tbls_id_t gg_flow_hash_l2l3_key_tbl[];
extern tbls_id_t gg_flow_hash_ipv4_key_tbl[];
extern tbls_id_t gg_flow_hash_ipv6_key_tbl[];
extern tbls_id_t gg_flow_hash_mpls_key_tbl[];
extern tbls_id_t gg_xcoam_hash_scl_tbl[];
extern tbls_id_t gg_xcoam_hash_tunnel_tbl[];
extern tbls_id_t gg_xcoam_hash_gg_oam_tbl[];
extern tbls_id_t gg_oam_tbl[];
extern ctc_dkits_gg_tcam_resc_inf_t gg_tcam_resc_inf[DKITS_TCAM_BLOCK_TYPE_NUM][DKITS_TCAM_PER_TYPE_LKP_NUM];
extern ctc_dkits_tcam_key_inf_t gg_acl_tcam_l2l3_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_acl_tcam_ipv4_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_acl_tcam_ipv6_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_acl_tcam_bridge_key_inf[];
extern tbls_id_t gg_acl_tcam_l2l3_ad_tbl[];
extern tbls_id_t gg_acl_tcam_ipv4_ad_tbl[];
extern tbls_id_t gg_acl_tcam_ipv6_ad_tbl[];
extern tbls_id_t gg_acl_tcam_bridge_ad_tbl[];
extern ctc_dkits_tcam_key_inf_t gg_scl_tcam_l2l3_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_scl_tcam_ipv4_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_scl_tcam_ipv6_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_scl_tcam_bridge_key_inf[];
extern tbls_id_t gg_scl_tcam_l2l3_ad_tbl[];
extern tbls_id_t gg_scl_tcam_ipv4_ad_tbl[];
extern tbls_id_t gg_scl_tcam_ipv6_ad_tbl[];
extern tbls_id_t gg_scl_tcam_bridge_ad_tbl[];
extern ctc_dkits_tcam_key_inf_t gg_ip_tcam_prefix_key_inf[];
extern tbls_id_t gg_ip_tcam_prefix_ad_tbl[];
extern ctc_dkits_tcam_key_inf_t gg_ip_tcam_nat_ipv4_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_ip_tcam_nat_ipv6_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_ip_tcam_pbr_ipv4_key_inf[];
extern ctc_dkits_tcam_key_inf_t gg_ip_tcam_pbr_ipv6_key_inf[];
extern tbls_id_t gg_ip_tcam_nat_ipv4_ad_tbl[];
extern tbls_id_t gg_ip_tcam_nat_ipv6_ad_tbl[];
extern tbls_id_t gg_ip_tcam_pbr_ipv4_ad_tbl[];
extern tbls_id_t gg_ip_tcam_pbr_ipv6_ad_tbl[];
extern tbls_id_t gg_nh_fwd_tbl[];
extern tbls_id_t gg_nh_met_tbl[];
extern tbls_id_t gg_nh_nexthop_tbl[];
extern tbls_id_t gg_nh_edit_tbl[];
extern tbls_id_t gg_auto_genernate_tbl[];
extern tbls_id_t gg_exclude_tbl[];
extern tbls_id_t gg_serdes_tbl[];
extern tbls_id_t gg_static_tbl_ipe[];
extern tbls_id_t gg_static_tbl_bsr[];
extern tbls_id_t gg_static_tbl_epe[];

#ifdef __cplusplus
}
#endif

#endif


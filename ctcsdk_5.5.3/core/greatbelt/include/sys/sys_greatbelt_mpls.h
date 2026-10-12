/**
 @file sys_greatbelt_mpls.h

 @date 2010-03-11

 @version v2.0

*/
#ifndef _SYS_GREATBELT_MPLS_H
#define _SYS_GREATBELT_MPLS_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"

#include "ctc_mpls.h"
#include "ctc_const.h"
#include "ctc_stats.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_MPLS_TBL_BLOCK_SIZE   64
#define SYS_MPLS_MAX_L3VPN_VRF_ID 0xBFFE
#define SYS_MPLS_VPLS_SRC_PORT_NUM 0xFFF
#define SYS_MPLS_MAX_LABEL_RANGE 128*1024
#define SYS_MPLS_120K_LABEL_RANGE 120*1024
#define SYS_MPLS_MAX_CHECK_LABEL 1024*1024

#define SYS_MPLS_INIT_CHECK(lchip)          \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gb_mpls_master[lchip]){  \
            return CTC_E_NOT_INIT; }        \
    } while (0)

#define SYS_MPLS_ILM_SPACE_CHECK(spaceid)         \
    do                                                \
    {                                                 \
        if (!p_gb_mpls_master[lchip]->space[spaceid].enable){ \
            return CTC_E_MPLS_SPACE_ERROR; }               \
    } while (0)

#define SYS_MPLS_ILM_LABEL_CHECK(spaceid, label)         \
    do                                                \
    {                                                 \
        if (!spaceid)         \
        { \
            if (!p_gb_mpls_master[lchip]->end_label0&&((label) >= p_gb_mpls_master[lchip]->space[spaceid].size)){ \
                return CTC_E_MPLS_LABEL_ERROR; }               \
            else if(p_gb_mpls_master[lchip]->end_label0&&!p_gb_mpls_master[lchip]->start_label1&&!p_gb_mpls_master[lchip]->start_label2&&((label) >p_gb_mpls_master[lchip]->end_label0)){\
                return CTC_E_MPLS_LABEL_ERROR; }               \
            else if (p_gb_mpls_master[lchip]->start_label1&&((label)<p_gb_mpls_master[lchip]->start_label1)&&((label) >p_gb_mpls_master[lchip]->end_label0)){\
                return CTC_E_MPLS_LABEL_ERROR; }               \
            else if(p_gb_mpls_master[lchip]->start_label1&&p_gb_mpls_master[lchip]->end_label1&&!p_gb_mpls_master[lchip]->start_label2&&((label) >p_gb_mpls_master[lchip]->end_label1)){\
                return CTC_E_MPLS_LABEL_ERROR; }               \
            else if((p_gb_mpls_master[lchip]->start_label2&&((label)<p_gb_mpls_master[lchip]->start_label2))&&((label) >p_gb_mpls_master[lchip]->end_label1)){\
                return CTC_E_MPLS_LABEL_ERROR; }               \
            else if(p_gb_mpls_master[lchip]->end_label2&&((label) >p_gb_mpls_master[lchip]->end_label2)){\
                return CTC_E_MPLS_LABEL_ERROR; }\
        } \
        else if ((label) < p_gb_mpls_master[lchip]->min_int_label || \
                 (label) - p_gb_mpls_master[lchip]->min_int_label >= p_gb_mpls_master[lchip]->space[spaceid].size){ \
            return CTC_E_MPLS_LABEL_ERROR; }               \
    } while (0)

#define SYS_MPLS_NHID_EXTERNAL_VALID_CHECK(nhid)           \
    do {  \
        if (nhid >= p_gb_mpls_master[lchip]->max_external_nhid){  \
            return CTC_E_NH_INVALID_NHID;        \
        };  \
    } while (0)

#define SYS_MPLS_ILM_DATA_CHECK(p_ilm)         \
    do                                                \
    {                                                 \
        if ((p_ilm)->id_type >= CTC_MPLS_MAX_ID){   \
            return CTC_E_INVALID_PARAM; }               \
        if (((p_ilm)->id_type & CTC_MPLS_ID_SERVICE) && \
            (p_ilm)->type != CTC_MPLS_LABEL_TYPE_VPLS && \
            (p_ilm)->type != CTC_MPLS_LABEL_TYPE_VPWS){ \
            return CTC_E_INVALID_PARAM; }  \
        if (((p_ilm)->id_type & CTC_MPLS_ID_APS_SELECT) && \
            (p_ilm)->flw_vrf_srv_aps.aps_select_grp_id >= SYS_GB_MAX_APS_GROUP_NUM){ \
            return CTC_E_APS_INVALID_GROUP_ID; }  \
        if (((p_ilm)->id_type & CTC_MPLS_ID_VRF) && \
            (p_ilm)->type != CTC_MPLS_LABEL_TYPE_L3VPN){ \
            return CTC_E_INVALID_PARAM; }  \
        if (((p_ilm)->id_type & CTC_MPLS_ID_VRF) && \
            (p_ilm)->flw_vrf_srv_aps.vrf_id > SYS_MPLS_MAX_L3VPN_VRF_ID){   \
            return CTC_E_INVALID_PARAM; }               \
        if ((p_ilm)->type >= CTC_MPLS_MAX_LABEL_TYPE){         \
            return CTC_E_INVALID_PARAM; }               \
        if ((p_ilm)->model >= CTC_MPLS_MAX_TUNNEL_MODE){         \
            return CTC_E_INVALID_PARAM; }               \
    } while (0)

#define SYS_MPLS_ILM_DATA_MASK(p_ilm)         \
    do                                                \
    {                                                 \
        if ((p_ilm)->type == CTC_MPLS_LABEL_TYPE_VPWS || (p_ilm)->type == CTC_MPLS_LABEL_TYPE_VPLS) \
        { \
            (p_ilm)->model = CTC_MPLS_TUNNEL_MODE_PIPE; \
        } \
        if ((p_ilm)->type == CTC_MPLS_LABEL_TYPE_L3VPN) \
        { \
            (p_ilm)->cwen = FALSE; \
        } \
        if ((p_ilm)->pwid > SYS_MPLS_VPLS_SRC_PORT_NUM) \
        { \
            (p_ilm)->pwid = 0xffff; \
        } \
        if ((p_ilm)->cwen) \
        { \
            (p_ilm)->cwen = TRUE; \
        } \
        if ((p_ilm)->logic_port_type) \
        { \
            (p_ilm)->logic_port_type = TRUE; \
        } \
    } while (0)

#define SYS_MPLS_KEY_MAP(p_ctc_mpls, p_sys_mpls)   \
    do                                                 \
    {                                                  \
        (p_sys_mpls)->spaceid = (p_ctc_mpls)->spaceid; \
        (p_sys_mpls)->label = (p_ctc_mpls)->label;     \
    } while (0)

#define SYS_MPLS_DATA_MAP(p_ctc_mpls, p_sys_mpls)  \
    do                                             \
    {                                              \
        (p_sys_mpls)->nh_id = (p_ctc_mpls)->nh_id; \
        (p_sys_mpls)->pwid = (p_ctc_mpls)->pwid;   \
        (p_sys_mpls)->id_type = (p_ctc_mpls)->id_type; \
        (p_sys_mpls)->type = (p_ctc_mpls)->type;   \
        (p_sys_mpls)->model = (p_ctc_mpls)->model;   \
        (p_sys_mpls)->cwen = (p_ctc_mpls)->cwen;   \
        (p_sys_mpls)->pop = (p_ctc_mpls)->pop;     \
        (p_sys_mpls)->decap = (p_ctc_mpls)->decap;     \
        (p_sys_mpls)->logic_port_type = (p_ctc_mpls)->logic_port_type;     \
        (p_sys_mpls)->oam_en = (p_ctc_mpls)->oam_en;     \
        (p_sys_mpls)->qos_use_outer_info = (p_ctc_mpls)->qos_use_outer_info;     \
    } while (0)

#define SYS_MPLS_VC_CHECK(p_vc)         \
    do                                                 \
    {                                                 \
        if ((p_vc)->bindtype >= CTC_MPLS_MAX_BIND_TYPE){         \
            return CTC_E_INVALID_PARAM; }               \
        if ((p_vc)->bindtype != CTC_MPLS_BIND_ETHERNET && \
            (p_vc)->vlanid > CTC_MAX_VLAN_ID){ \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    } while (0)

#define SYS_MPLS_L2VPN_DATA_CHECK(p_vc)         \
    do                                                \
    {                                                 \
        if ((p_vc)->l2vpntype >= CTC_MPLS_MAX_L2VPN_TYPE){         \
            return CTC_E_INVALID_PARAM; }               \
        if ((p_vc)->l2vpntype == CTC_MPLS_L2VPN_VPWS) \
        { \
            if (((p_vc)->u.pw_nh_id) < SYS_HUMBER_NH_RESOLVED_NHID_MAX ||      \
                ((p_vc)->u.pw_nh_id) >= (p_gb_mpls_master[lchip]->max_external_nhid)){        \
                return CTC_E_NH_INVALID_NHID; }  \
        } \
        else \
        { \
            if (((p_vc)->u.vpls_info.fid) < SYS_HUMBER_NH_RESOLVED_NHID_MAX ||      \
                ((p_vc)->u.vpls_info.fid) >= (p_gb_mpls_master[lchip]->max_external_nhid)){        \
                return CTC_E_NH_INVALID_NHID; }  \
            if ((p_vc)->u.vpls_info.logic_port > SYS_MPLS_VPLS_SRC_PORT_NUM) \
            { \
                (p_vc)->u.vpls_info.logic_port = 0xffff; \
            } \
            if ((p_vc)->u.vpls_info.logic_port_type) \
            { \
                (p_vc)->u.vpls_info.logic_port_type = TRUE; \
            } \
        } \
    } while (0)

#define SYS_MPLS_L2VPN_AC_DATA_CHECK(p_ac)         \
    do                                                \
    {                                                 \
        if (((p_ac))->l2vpntype >= CTC_MPLS_MAX_L2VPN_TYPE){         \
            return CTC_E_INVALID_PARAM; }               \
        if (((p_ac))->l2vpntype == CTC_MPLS_L2VPN_VPWS) \
        { \
            if (((p_ac)->u.pw_nh_id) < SYS_HUMBER_NH_RESOLVED_NHID_MAX ||      \
                (((p_ac))->u.pw_nh_id) >= (p_gb_mpls_master[lchip]->max_external_nhid)){        \
                return CTC_E_NH_INVALID_NHID; }                    \
        } \
        else \
        { \
            CTC_FID_RANGE_CHECK(((p_ac))->u.fid); \
        } \
    } while (0)

#define SYS_MPLS_DBG_DUMP(FMT, ...)                      \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT_INFO(mpls, mpls, MPLS_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DUMP(FMT, ...)                    \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT(mpls, mpls, MPLS_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define DebugTrace_Mpls(FMT, ...)  \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT(mpls, mpls, MPLS_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(mpls, mpls, MPLS_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MPLS_DUMP_ILM(p_mpls_ilm) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_ilm :: nh_id = %d, spaceid = %d , label = %d,\n" \
                         "    id_type = %d, 0-NULL,1-FLOW,2-VRF,3-SERVICE,4-APS,5-STATS,6-MAX,\n" \
                         "    label type = %d, 0-NORMAL LABEL,1-L3VPN LABEL,2-VPWS,3-VPLS,4-MAX,\n" \
                         "    tunnel model = %d, 0-UNIFORM,1-SHORT PIPE,2-PIPE,3-MAX,\n" \
                         "    pop = %d, cwen = %d, aps_select_protect_path = %d,  oam_en = %d,\n" \
                         "    pwid = %d, logic_port_type = %d,\n" \
                         "    flw_vrf_srv_aps = %d, flow_id   vrf_id   service_id   aps_select_grp_id\n\n", \
                         p_mpls_ilm->nh_id, p_mpls_ilm->spaceid, p_mpls_ilm->label, \
                         p_mpls_ilm->id_type, \
                         p_mpls_ilm->type, \
                         p_mpls_ilm->model, \
                         p_mpls_ilm->pop, p_mpls_ilm->cwen, p_mpls_ilm->aps_select_protect_path, p_mpls_ilm->oam_en, \
                         p_mpls_ilm->pwid, p_mpls_ilm->logic_port_type, \
                         p_mpls_ilm->flw_vrf_srv_aps.flow_id); \
    } while (0)

#define SYS_MPLS_DUMP_MPLS_DB_INFO(p_mpls_info) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_info :: key_offset = %d, spaceid = %d ," \
                         "label = %d,nh_id = %d,pop = %d\n" \
                         "    id_type = %d, 0x0-NULL,0x1-FLOW,0x2-VRF,0x4-SERVICE,0x8-APS_SELECT,0x10-STATS,0xFFFF-MAX,\n" \
                         "    label type = %d, 0-NORMAL LABEL,1-L3VPN LABEL,2-VPWS,3-VPLS,4-MAX,\n" \
                         "    tunnel model = %d, 0-UNIFORM,1-SHORT PIPE,2-PIPE,3-MAX,\n" \
                         "    pwid = %d, logic_port_type = %d,\n" \
                         "    fid = %d, pw_mode = %d  0-Tagged PW    1-Raw PW,\n" \
                         "    learn_disable = %d, igmp_snooping_enable =%d, maclimit_enable=%d, service_aclqos_enable = %d,\n" \
                         "    cwen = %d, oam_en = %d,stats_ptr = %d,\n" \
                         "    flw_vrf_srv = %d, aps_group_id = %d, ecpn= %d, aps_select_ppath =%d\n\n", \
                         p_mpls_info->key_offset, p_mpls_info->spaceid, p_mpls_info->label, p_mpls_info->nh_id, p_mpls_info->pop, \
                         p_mpls_info->id_type, \
                         p_mpls_info->type, \
                         p_mpls_info->model, \
                         p_mpls_info->pwid, p_mpls_info->logic_port_type, \
                         p_mpls_info->fid, p_mpls_info->pw_mode, \
                         p_mpls_info->learn_disable, p_mpls_info->igmp_snooping_enable, p_mpls_info->maclimit_enable, p_mpls_info->service_aclqos_enable, \
                         p_mpls_info->cwen, p_mpls_info->oam_en, p_mpls_info->stats_ptr, \
                         p_mpls_info->flw_vrf_srv, p_mpls_info->aps_group_id, p_mpls_info->ecpn, p_mpls_info->aps_select_ppath); \
    } while (0)

#define SYS_MPLS_DUMP_VPWS_PARAM(p_mpls_pw) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_pw :: label = %d, l2vpntype = %d,\n" \
                         "    pw_mode = %d,  \n" \
                         "    learn_disable = %d, maclimit_enable = %d, service_aclqos_enable = %d, igmp_snooping_enable = %d\n" \
                         "    pw_nh_id = %d\n\n", \
                         p_mpls_pw->label, p_mpls_pw->l2vpntype, p_mpls_pw->pw_mode, \
                         p_mpls_pw->learn_disable, p_mpls_pw->maclimit_enable, p_mpls_pw->service_aclqos_enable, p_mpls_pw->igmp_snooping_enable, \
                         p_mpls_pw->u.pw_nh_id); \
    } while (0)

#define SYS_MPLS_DUMP_VPLS_PARAM(p_mpls_pw) \
    do { \
        SYS_MPLS_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "    p_mpls_pw :: label = %d, l2vpntype = %d,\n" \
                         "    pw_mode = %d,  \n" \
                         "    learn_disable = %d, maclimit_enable = %d, service_aclqos_enable = %d, igmp_snooping_enable = %d\n" \
                         "    fid = %d, logic_src_port = %d, " \
                         "    logic_port_type= %d 0-H_VPLS 1-Normal_VPLS \n\n", \
                         p_mpls_pw->label, p_mpls_pw->l2vpntype, p_mpls_pw->pw_mode, \
                         p_mpls_pw->learn_disable, p_mpls_pw->maclimit_enable, p_mpls_pw->service_aclqos_enable, p_mpls_pw->igmp_snooping_enable, \
                         p_mpls_pw->u.vpls_info.fid, p_mpls_pw->logic_port, p_mpls_pw->u.vpls_info.logic_port_type); \
    } while (0)

struct sys_mpls_ilm_s
{
    uint32 key_offset;
    uint32 label;
    uint32 nh_id;
    uint16 service_id;
    uint16 pwid;
    uint16 flw_vrf_srv;
    uint16 flow_id;
    uint16 vrf_id;
    uint16 aps_group_id;
    uint8 id_type;
    uint8 spaceid;
    uint8 type;
    uint8 model;
    uint8 ecpn;
    uint8 inner_pkt_type;
    uint8 pw_mode;
    uint8 out_intf_spaceid;
    uint16 stats_ptr;
    uint16 fid;
    uint16 policer_id;
    uint8  rsv1[2];
    uint16 cwen                 :1;
    uint16 pop                  :1;
    uint16 decap                :1;
    uint16 aps_select_ppath     :1;
    uint16 logic_port_type      :1;
    uint16 oam_en               :1;
    uint16 learn_disable        :1;
    uint16 igmp_snooping_enable :1;
    uint16 maclimit_enable      :1;
    uint16 service_aclqos_enable:1;
    uint16 trust_outer_exp      :1;
    uint16 qos_use_outer_info   :1;
    uint16 service_policer_en   :1;
    uint16 service_queue_en     :1;
    uint16 use_merged_nh        :1;
    uint16 rsv                  :1;
};
typedef struct sys_mpls_ilm_s sys_mpls_ilm_t;

struct sys_mpls_space_s
{
    ctc_vector_t* p_vet;
    uint32 size;
    uint32 base;
    uint8 enable;
};
typedef struct sys_mpls_space_s sys_mpls_space_t;

typedef int32 (* mpls_write_ilm_t)(uint8 lchip, sys_mpls_ilm_t* p_ilm_info, void* dsmpls);
typedef int32 (* mpls_write_pw_t)(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

struct sys_mpls_master_s
{
    sal_mutex_t* mutex;
    mpls_write_ilm_t write_ilm[CTC_MPLS_MAX_LABEL_TYPE];
    mpls_write_pw_t write_pw[CTC_MPLS_MAX_L2VPN_TYPE];
    uint32 min_int_label;
    sys_mpls_space_t space[CTC_MPLS_SPACE_NUMBER];
    uint32 max_external_nhid;
    uint8 lm_datapkt_fwd; /* for lm packet, if merged dsfwd will use dsmpls.mepindex as destmap, but mepindex will modify by oam cfg*/
    uint8 label_range_mode; /* if set means mpls support label range separation */
    uint32 start_label0;
    uint32 end_label0;
    uint32 start_label1;
    uint32 end_label1;
    uint32 start_label2;
    uint32 end_label2;
    uint32 min_label;
    uint8 decap_mode;         /* 0:normal mode, 1:iloop mode */
    uint8 rsv[3];
};
typedef struct sys_mpls_master_s sys_mpls_master_t;

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_greatbelt_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info);

/**
 @brief De-Initialize mpls module
*/
extern int32
sys_greatbelt_mpls_deinit(uint8 lchip);

extern int32
sys_greatbelt_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);
extern int32
sys_greatbelt_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);
extern int32
sys_greatbelt_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);

extern int32
sys_greatbelt_mpls_get_ilm(uint8 lchip, uint32 nh_id[SYS_GB_MAX_ECPN], ctc_mpls_ilm_t * p_mpls_ilm);

extern int32
sys_greatbelt_mpls_add_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index);

extern int32
sys_greatbelt_mpls_del_stats(uint8 lchip, ctc_mpls_stats_index_t* stats_index);

extern int32
sys_greatbelt_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

extern int32
sys_greatbelt_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

extern int32
_sys_greatbelt_mpls_for_tp_oam(uint8 lchip, uint32 gal_check);

int32
sys_greatbelt_mpls_set_label_range_mode(uint8 lchip, bool mode_en);

int32
sys_greatbelt_mpls_set_label_range(uint8 lchip, uint8 block,uint32 start_label,uint32 size);

extern int32
sys_greatbelt_mpls_calc_real_label_for_label_range(uint8 lchip, uint8 space_id, uint32 in_label,uint32 *out_label);

extern int32
sys_greatbelt_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro_info);

#ifdef __cplusplus
}
#endif

#endif


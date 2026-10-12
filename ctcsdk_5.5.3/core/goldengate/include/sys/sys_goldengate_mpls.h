/**
 @file sys_goldengate_mpls.h

 @date 2010-03-11

 @version v2.0

*/
#ifndef _SYS_GOLDENGATE_MPLS_H
#define _SYS_GOLDENGATE_MPLS_H
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
#include "ctc_hash.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

enum sys_mpls_ilm_property_type_e
{
    SYS_MPLS_ILM_QOS_DOMAIN,
    SYS_MPLS_ILM_APS_SELECT,
    SYS_MPLS_ILM_OAM_LMEN,
    SYS_MPLS_ILM_OAM_IDX,
    SYS_MPLS_ILM_OAM_CHK,
    SYS_MPLS_ILM_OAM_VCCV_BFD,
    SYS_MPLS_ILM_ROUTE_MAC,
    SYS_MPLS_ILM_LLSP,
    SYS_MPLS_ILM_MAX
};
typedef enum sys_mpls_ilm_property_type_e sys_mpls_ilm_property_type_t;
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
extern int32
sys_goldengate_mpls_init(uint8 lchip, ctc_mpls_init_t* p_mpls_info);

/**
 @brief De-Initialize mpls module
*/
extern int32
sys_goldengate_mpls_deinit(uint8 lchip);

extern int32
sys_goldengate_mpls_add_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);
int32
sys_goldengate_mpls_update_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);
extern int32
sys_goldengate_mpls_del_ilm(uint8 lchip, ctc_mpls_ilm_t* p_mpls_ilm);

extern int32
sys_goldengate_mpls_get_ilm(uint8 lchip, uint32 nh_id[SYS_GG_MAX_ECPN], ctc_mpls_ilm_t * p_mpls_ilm);

extern int32
sys_goldengate_mpls_add_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

extern int32
sys_goldengate_mpls_del_l2vpn_pw(uint8 lchip, ctc_mpls_l2vpn_pw_t* p_mpls_pw);

extern int32
sys_goldengate_mpls_set_ilm_property(uint8 lchip, ctc_mpls_property_t* p_mpls_pro_info);


#ifdef __cplusplus
}
#endif

#endif


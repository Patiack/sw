/**
 @file sys_goldengate_oam_db.h

 @date 2010-1-19

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GOLDENGATE_OAM_DBD_H
#define _SYS_GOLDENGATE_OAM_DBD_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/
#include "sal.h"
#include "ctc_hash.h"
#include "ctc_linklist.h"
#include "ctc_vector.h"
#include "ctc_interrupt.h"
#include "sys_goldengate_chip.h"

/****************************************************************************
*
* Defines and Macros
*
****************************************************************************/

#define SYS_OAM_MIN_CCM_INTERVAL 1
#define SYS_OAM_MAX_CCM_INTERVAL 7

#define SYS_OAM_MIN_BFD_INTERVAL 3
#define SYS_OAM_MAX_BFD_INTERVAL 1023

#define SYS_OAM_MIN_BFD_UDP_PORT 49152
#define SYS_OAM_MAX_BFD_UDP_PORT 65535

#define SYS_OAM_MIN_BFD_DETECT_MULT 1
#define SYS_OAM_MAX_BFD_DETECT_MULT 15

#define SYS_OAM_MIN_MEP_ID 1
#define SYS_OAM_MAX_MEP_ID 8191

#define SYS_OAM_MIN_MEP_INDEX 2

#define SYS_OAM_BFD_INVALID_IPV6_NUM   0xFF
#define SYS_OAM_BFD_IPV6_MAX_IPSA_NUM  128

#define CTC_GOLDENGATE_OAM_DEFECT_NUM 20

/***************************************************************
*
*           OAM FUNCTION DB
*
*****************************************************************/
#define SYS_OAM_MAX_MAID_LEN 48

enum sys_oam_module_e
{
    SYS_OAM_CHAN,
    SYS_OAM_MAID,
    SYS_OAM_LMEP,
    SYS_OAM_RMEP,
    SYS_OAM_MIP,
    SYS_OAM_GLOB,
    SYS_OAM_STATS,
    SYS_OAM_MODULE_MAX
};
typedef enum sys_oam_module_e sys_oam_module_t;

enum sys_oam_op_e
{
    SYS_OAM_ADD,
    SYS_OAM_DEL,
    SYS_OAM_UPDATE,
    SYS_OAM_CMP,
    SYS_OAM_CONFIG,
    SYS_OAM_GET_INFO,
    SYS_OAM_OP_MAX
};
typedef enum sys_oam_op_e sys_oam_op_t;

enum sys_oam_check_type_e
{
    SYS_OAM_CHECK_MAID,
    SYS_OAM_CHECK_LMEP,
    SYS_OAM_CHECK_RMEP,
    SYS_OAM_CHECK_MEP_UPDATE,
    SYS_OAM_CHECK_MIP,
    SYS_OAM_CHECK_MAX
};
typedef enum sys_oam_check_type_e sys_oam_check_type_t;


enum sys_oam_key_alloc_e
{
    SYS_OAM_KEY_HASH,
    SYS_OAM_KEY_LTCAM,
    SYS_OAM_KEY_MAX
};
typedef enum sys_oam_key_alloc_e sys_oam_key_alloc_t;

enum sys_oam_tp_check_type_e
{
    SYS_OAM_TP_NONE_TYPE,
    SYS_OAM_TP_MEP_TYPE,
    SYS_OAM_TP_MIP_TYPE,
    SYS_OAM_TP_OAM_DISCARD_TYPE,
    SYS_OAM_TP_TO_CPU_TYPE,
    SYS_OAM_TP_DATA_DISCARD_TYPE
};
typedef enum sys_oam_tp_check_type_e sys_oam_tp_check_type_t;

enum sys_oam_exception_type_e
{
    SYS_OAM_EXCP_NONE                   = 0,
    SYS_OAM_EXCP_ETH_INVALID            = 1,
    SYS_OAM_EXCP_ETH_LB                 = 2,
    SYS_OAM_EXCP_ETH_LT                 = 3,
    SYS_OAM_EXCP_ETH_LM                 = 4,
    SYS_OAM_EXCP_ETH_DM                 = 5,
    SYS_OAM_EXCP_ETH_TST                = 6,
    SYS_OAM_EXCP_ETH_APS                = 7,
    SYS_OAM_EXCP_ETH_SCC                = 8,
    SYS_OAM_EXCP_ETH_MCC                = 9,
    SYS_OAM_EXCP_ETH_CSF                = 10,
    SYS_OAM_EXCP_ETH_BIG_CCM            = 11,
    SYS_OAM_EXCP_ETH_LEARN_CCM          = 12,
    SYS_OAM_EXCP_ETH_DEFECT             = 13,
    SYS_OAM_EXCP_PBX_OAM                = 14,
    SYS_OAM_EXCP_ETH_HIGH_VER           = 15,
    SYS_OAM_EXCP_ETH_TLV                = 16,
    SYS_OAM_EXCP_ETH_OTHER              = 17,
    SYS_OAM_EXCP_BFD_INVALID            = 18,
    SYS_OAM_EXCP_BFD_LEARN              = 19,
    SYS_OAM_EXCP_BIG_BFD                = 20,
    SYS_OAM_EXCP_BFD_TIMER_NEG          = 21,
    SYS_OAM_EXCP_TP_SCC                 = 22,
    SYS_OAM_EXCP_TP_MCC                 = 23,
    SYS_OAM_EXCP_TP_CSF                 = 24,
    SYS_OAM_EXCP_TP_LB                  = 25,
    SYS_OAM_EXCP_TP_DLM                 = 26,
    SYS_OAM_EXCP_TP_DM                  = 27,
    SYS_OAM_EXCP_TP_FM                  = 28,
    SYS_OAM_EXCP_BFD_OTHER              = 29,
    SYS_OAM_EXCP_BFD_DISC_MISMATCH      = 30,
    SYS_OAM_EXCP_ETH_SM                 = 31,
    SYS_OAM_EXCP_TP_CV                  = 32
};
typedef enum sys_oam_exception_type_e sys_oam_exception_type_t;

enum sys_gg_oam_tbl_type_e
{
    SYS_OAM_TBL_MEP,
    SYS_OAM_TBL_MA,
    SYS_OAM_TBL_MANAME,
    SYS_OAM_TBL_LM,
    SYS_OAM_TBL_LOOKUP_KEY,
    SYS_OAM_TBL_MAX
};
typedef enum sys_gg_oam_tbl_type_e sys_gg_oam_tbl_type_t;

enum sys_oam_session_type_e
{
    SYS_OAM_SESSION_Y1731_ETH_P2P,
    SYS_OAM_SESSION_Y1731_ETH_P2MP,
    SYS_OAM_SESSION_Y1731_TP_SECTION,
    SYS_OAM_SESSION_Y1731_TP_MPLS,
    SYS_OAM_SESSION_BFD_IPv4,
    SYS_OAM_SESSION_BFD_IPv6,
    SYS_OAM_SESSION_BFD_MPLS,
    SYS_OAM_SESSION_BFD_TP_SECTION,
    SYS_OAM_SESSION_BFD_TP_MPLS,
    SYS_OAM_SESSION_MAX
};
typedef enum sys_oam_session_type_e sys_oam_session_type_t;

typedef int32 (* SYS_OAM_CALL_FUNC_P)(uint8 lchip, void* p_para);

typedef int32 (* SYS_OAM_CHECK_CALL_FUNC_P)(uint8 lchip, void* p_para, bool is_add);

typedef int32 (* SYS_OAM_DEFECT_PROCESS_FUNC)(uint8 lchip, void* p_data);

struct sys_oam_cmp_s
{
    void* p_node_parm;
    void* p_node_db;
};
typedef struct sys_oam_cmp_s sys_oam_cmp_t;

struct sys_oam_master_s
{
    SYS_OAM_CALL_FUNC_P* p_fun_table;

    SYS_OAM_CHECK_CALL_FUNC_P *p_check_fun_table;

    ctc_hash_t*  maid_hash;
    ctc_hash_t*  chan_hash;

    ctc_vector_t* mep_vec;
    sal_mutex_t*  oam_mutex;

    ctc_oam_global_t com_oam_global;

    CTC_INTERRUPT_EVENT_FUNC error_cache_cb;

    SYS_OAM_DEFECT_PROCESS_FUNC defect_process_cb;

    uint32 oam_reource_num[SYS_OAM_TBL_MAX];
    uint32 oam_session_num[SYS_OAM_SESSION_MAX];

    void* ipv6_bmp;

    uint32* mep_defect_bitmap;

    uint8 timer_update_disable;
    uint8 no_mep_resource;
};
typedef struct sys_oam_master_s sys_oam_master_t;

#define SYS_OAM_FUNC_TABLE(lchip, type, module, op) \
    *(SYS_OAM_CALL_FUNC_P*) \
    (g_gg_oam_master[lchip]->p_fun_table + (type * (SYS_OAM_MODULE_MAX * SYS_OAM_OP_MAX) + module * SYS_OAM_MODULE_MAX + op))

#define SYS_OAM_CHECK_FUNC_TABLE(lchip, type, module) \
    *(SYS_OAM_CHECK_CALL_FUNC_P*) \
    (g_gg_oam_master[lchip]->p_check_fun_table + ((type * (SYS_OAM_CHECK_MAX)) + module ))


#define SYS_OAM_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == g_gg_oam_master[lchip]) \
        { \
            return CTC_E_OAM_NOT_INIT; \
        } \
    } while (0)

#define SYS_OAM_FUNC_TABLE_CHECK(p_func) \
    do { \
        if (NULL == p_func) \
        { \
            return CTC_E_NOT_SUPPORT; \
        } \
    } while (0)

#define P_COMMON_OAM_MASTER(lchip) g_gg_oam_master[lchip]

#define P_COMMON_OAM_MASTER_GLB(lchip) (g_gg_oam_master[lchip]->com_oam_global)

#define OAM_LOCK(lchip) \
    do { \
        if (g_gg_oam_master[lchip]->oam_mutex) \
        { \
            sal_mutex_lock(g_gg_oam_master[lchip]->oam_mutex); \
        } \
    } while (0)

#define OAM_UNLOCK(lchip) \
    do { \
        if (g_gg_oam_master[lchip]->oam_mutex) \
        { \
            sal_mutex_unlock(g_gg_oam_master[lchip]->oam_mutex); \
        } \
    } while (0)
    
#define SYS_OAM_NO_MEP_OPERATION(lchip, master_gchip)\
(((FALSE == sys_goldengate_chip_is_local(lchip, master_gchip))\
        && (1 == (P_COMMON_OAM_MASTER_GLB(lchip).mep_index_alloc_by_sdk)))\
        || g_gg_oam_master[lchip]->no_mep_resource)\


#define SYS_OAM_SESSION_CNT(lchip, id, add) (add ? g_gg_oam_master[lchip]->oam_session_num[id]++ : g_gg_oam_master[lchip]->oam_session_num[id]--)
#define SYS_OAM_TBL_CNT(lchip, id, add, step) (add ? (g_gg_oam_master[lchip]->oam_reource_num[id]+= step) : (g_gg_oam_master[lchip]->oam_reource_num[id]-=step))

/***************************************************************
*
*           OAM DB COM HEADER
*
*****************************************************************/

struct sys_oam_maid_com_s
{
    uint8    mep_type;
    uint8    maid_len;
    uint8    maid_entry_num;
    uint8    ref_cnt;
    uint32   maid_index;
    uint8    maid[SYS_OAM_MAX_MAID_LEN];
};
typedef struct sys_oam_maid_com_s sys_oam_maid_com_t;

struct sys_oam_key_com_s
{
    uint8       mep_type;
    uint8       key_alloc;  /*sys_oam_key_alloc_t*/
    uint8       key_exit;
    uint8       rsv;
    uint32      key_index;
};
typedef struct sys_oam_key_com_s sys_oam_key_com_t;

struct sys_oam_chan_com_s
{
    ctc_slist_t* lmep_list;
    uint8        mep_type;
    uint8        master_chipid;
    uint8        lm_num; /*lm number for ether, not include link*/
    uint8        lchip;
    uint16       chan_index;
    uint16       lm_index_base; /*lm profile index when ether*/
    uint16       link_lm_index_base;
    uint8        link_oam;
    uint8        resv;

};
typedef struct sys_oam_chan_com_s sys_oam_chan_com_t;

struct sys_oam_lmep_com_s
{
    ctc_slistnode_t head;
    ctc_slist_t* rmep_list;
    sys_oam_maid_com_t* p_sys_maid;
    sys_oam_chan_com_t* p_sys_chan;
    uint32 lmep_index;
    uint16 ma_index;
    uint8 lchip;
    uint8 mep_on_cpu;

    uint8  lm_index_alloced; /*used for ether*/
    uint8 resv[3];

    uint8 lm_cos;
    uint8 lm_cos_type;
    uint16  lm_index_base;   /*lm index*/

};
typedef struct sys_oam_lmep_com_s sys_oam_lmep_com_t;

struct sys_oam_rmep_com_s
{
    ctc_slistnode_t head;
    sys_oam_maid_com_t* p_sys_maid;
    sys_oam_lmep_com_t* p_sys_lmep;
    uint32          rmep_index;
};
typedef struct sys_oam_rmep_com_s sys_oam_rmep_com_t;

/***************************************************************
*
*           Eth OAM DB
*
*****************************************************************/
#define ETHERNET_OAM "ETHERNET OAM Begin"

#define SYS_OAM_MAX_MD_LEVEL 7
#define SYS_OAM_MAX_MEP_NUM_PER_CHAN 4

struct sys_oam_key_eth_s
{
    sys_oam_key_com_t com;

    uint8     resv;
    uint8     use_fid;
    uint8     link_oam;
    uint8     mip_en;

    uint16    gport;     /**<Ethoam*/
    uint16    vlan_id;   /**<Ethoam vlanID*/

    uint16    l2vpn_oam_id;    /**< vpws oam id or vpls fid  */
    uint8      is_vpws;
    uint8      rsv0[1];
};
typedef struct sys_oam_key_eth_s sys_oam_key_eth_t;

struct sys_oam_chan_eth_s
{
    sys_oam_chan_com_t com;

    sys_oam_key_eth_t key;
    uint8  up_mep_bitmap;
    uint8  down_mep_bitmap;
    uint8  mip_bitmap;
    uint8  lm_bitmap;

    uint8  lm_type;
    uint8  link_lm_type;

    uint8  link_lm_cos_type;
    uint8  link_lm_cos;
    uint8  lm_index_alloced; /*lm profile when ether*/
    uint8  link_lm_index_alloced;

    uint32 mep_index[SYS_OAM_MAX_MD_LEVEL + 1];
    uint16 lm_index[SYS_OAM_MAX_MD_LEVEL + 1];/*lm index for ether*/
    uint8 lm_cos[SYS_OAM_MAX_MD_LEVEL + 1];
    uint8 lm_cos_type[SYS_OAM_MAX_MD_LEVEL + 1];
};
typedef struct sys_oam_chan_eth_s sys_oam_chan_eth_t;


struct sys_oam_lmep_y1731_s
{
    /*common*/
    sys_oam_lmep_com_t com;

    uint32 flag;
    uint32 update_type;

    uint16 mep_id;
    uint8  ccm_interval;
    uint8  ccm_en;

    uint8  tx_cos_exp;
    uint8  present_rdi;
    uint8  tx_csf_type;
    uint8  tx_csf_en;

    uint8  sf_state;
    uint8  dm_en;
    uint8  trpt_en;
    uint8  trpt_session_id;

    /*eth*/
    uint8  tpid_type;
    uint8  md_level;
    uint8  share_mac;
    uint8  rsv0;

    uint16 ccm_gport_id;
    uint8  port_status;
    uint8  is_up;

    uint8  tx_csf_use_uc_da;
    uint8  ccm_p2p_use_uc_da;
    /*tp*/
    uint8  without_gal;
    uint8  mpls_ttl;
    uint8 lock_en;
    uint8  rsv1;

    uint32 nhid;
};
typedef struct sys_oam_lmep_y1731_s sys_oam_lmep_y1731_t;

struct sys_oam_rmep_y1731_s
{
    sys_oam_rmep_com_t com;
    /*y1731 common*/
    uint32 flag;
    uint32 update_type;

    uint32 key_index;
    uint8  key_alloc; /*sys_oam_key_alloc_t*/
    uint8   csf_en;
    uint8   d_csf;
    uint8   sf_state;

    uint32  rmep_id;

    /*eth*/
    mac_addr_t rmep_mac;
    uint8   src_mac_state;
    uint8   is_p2p_mode;
    uint8   port_intf_state;
    uint8   md_level;
};
typedef struct sys_oam_rmep_y1731_s sys_oam_rmep_y1731_t;

/***************************************************************
*
*           MPLS-TP Y.1731 OAM DB
*
*****************************************************************/
#define MPLS_TP_Y1731 "MPLS-TP Y.1731 Begin"

struct sys_oam_key_tp_s
{
    sys_oam_key_com_t com;

    uint8     section_oam;
    uint8     mip_en;
    uint16    gport_l3if_id;
    uint32    label;
    uint8     spaceid;
    uint8     resv[3];
};
typedef struct sys_oam_key_tp_s sys_oam_key_tp_t;

struct sys_oam_chan_tp_s
{
    sys_oam_chan_com_t com;

    sys_oam_key_tp_t key;

    uint8  lm_type;
    uint8  lm_cos_type;
    uint8  lm_cos;
    uint8  lm_index_alloced;

    uint32 mep_index;

    uint16 mep_index_in_key;
    uint8 mep_on_cpu;
    uint8 rsv;
};
typedef struct sys_oam_chan_tp_s sys_oam_chan_tp_t;


/***************************************************************
*
*           BFD OAM DB
*
*****************************************************************/
struct sys_oam_key_bfd_s
{
    sys_oam_key_com_t com;

    uint32    my_discr;
};
typedef struct sys_oam_key_bfd_s sys_oam_key_bfd_t;

struct sys_oam_chan_bfd_s
{
    sys_oam_chan_com_t com;

    sys_oam_key_bfd_t key;

    uint8  lm_type;
    uint8  lm_cos_type;
    uint8  lm_cos;
    uint8  lm_index_alloced;

    uint32 mep_index;
};
typedef struct sys_oam_chan_bfd_s sys_oam_chan_bfd_t;


struct sys_oam_lmep_bfd_s
{
    sys_oam_lmep_com_t com;
    sal_systime_t tv_p;           /*used for pf process by sw, indicate rx pbit pkt timestamp*/
    sal_systime_t tv_f;           /*used for pf process by sw, indicate rx fbit pkt timestamp*/
    sal_systime_t tv_p_set;       /*record the TS when set pbit, the interval to set pbit must over 1 second, only used for master device*/
    /*bfd common*/
    uint32 flag;
    uint32 update_type;
    uint32 *p_update_value;

    uint8  tx_cos_exp;
    uint8  tx_csf_type;

    uint16 bfd_src_port;
    uint16 actual_tx_interval;

    uint16 desired_min_tx_interval;
    uint32 local_discr;

    uint8  local_diag;
    uint8  local_state;
    uint8  local_detect_mult;
    uint8  aps_bridge_en;
    /*tp*/
    uint8  without_gal;
    uint8  mpls_ttl;
    uint8 lock_en;
    uint8  spaceid;

    uint32 nhid;
    uint32 mpls_in_label;

    union
    {
        ip_addr_t ipv4;	  /**< [GG]IPv4 destination address */
        ipv6_addr_t ipv6;  /**< [GG]IPv6 destination address */
    } ip_sa;

    union
    {
        ip_addr_t ipv4;   /**< [GG]IPv4 destination address */
        ipv6_addr_t ipv6;  /**< [GG]IPv6 destination address */
    } ip_da;

    uint8 ipv6_sa_index;
    uint8 ipv6_da_index;
};
typedef struct sys_oam_lmep_bfd_s sys_oam_lmep_bfd_t;

struct sys_oam_rmep_bfd_s
{
    sys_oam_rmep_com_t com;
    /*bfd common*/
    uint32 flag;
    uint32 update_type;
    uint32 *p_update_value;

    uint16 actual_rx_interval;
    uint16 required_min_rx_interval;

    uint32 remote_discr;

    uint8  remote_diag;
    uint8  remote_state;
    uint8  remote_detect_mult;
};
typedef struct sys_oam_rmep_bfd_s sys_oam_rmep_bfd_t;


/***************************************************************
*
*           MISC OAM
*
*****************************************************************/
struct sys_oam_nhop_info_s
{
    uint32  dsnh_offset;

    uint16  dest_id;            /*UcastId or McastId*/
    uint8   dest_chipid;        /*Destination global chip id*/
    uint8   nexthop_is_8w;
    uint8   aps_bridge_en;
    uint8   nh_entry_type;
    uint8   mep_on_tunnel;
    uint8   have_l2edit;
    uint8   multi_aps_en;

    uint32 dest_map;
};
typedef struct sys_oam_nhop_info_s sys_oam_nhop_info_t;

struct sys_oam_lm_com_s
{
    uint8 lm_type;
    uint8 lm_cos_type;
    uint16 lm_index;
    uint8 lchip;
};
typedef struct sys_oam_lm_com_s sys_oam_lm_com_t;

struct sys_oam_defect_info_s
{
    char  *defect_name;
    uint8 to_rdi;
    uint8 to_event;
};
typedef struct sys_oam_defect_info_s sys_oam_defect_info_t;

struct sys_oam_defect_param_s
{
    ctc_oam_defect_info_t  defect_info;
    uint8 isr_state; /*1, start, 2, process, 3 ,end*/
};
typedef struct sys_oam_defect_param_s sys_oam_defect_param_t;

/****************************************************************************
 *
* Function
*
****************************************************************************/

/*MAID DB API*/
sys_oam_maid_com_t*
_sys_goldengate_oam_maid_lkup(uint8 lchip, ctc_oam_maid_t* p_ctc_maid);

sys_oam_maid_com_t*
_sys_goldengate_oam_maid_build_node(uint8 lchip, ctc_oam_maid_t* p_maid_param);

int32
_sys_goldengate_oam_maid_free_node(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_build_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_free_index(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_add_to_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_del_from_db(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_add_to_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

int32
_sys_goldengate_oam_maid_del_from_asic(uint8 lchip, sys_oam_maid_com_t* p_sys_maid);

/*CHAN DB API*/
sys_oam_chan_com_t*
_sys_goldengate_oam_chan_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_oam_chan_build_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_oam_chan_free_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_oam_chan_add_to_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_oam_chan_del_from_db(uint8 lchip, sys_oam_chan_com_t* p_sys_chan);

int32
_sys_goldengate_oam_lm_build_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec, uint8 md_level);

int32
_sys_goldengate_oam_lm_free_index(uint8 lchip, sys_oam_chan_com_t* p_sys_chan, uint8 is_link_sec, uint8 md_level);

/*LMEP DB API*/
sys_oam_lmep_com_t*
_sys_goldengate_oam_lmep_lkup(uint8 lchip, sys_oam_chan_com_t* p_sys_chan,
                             sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_build_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_free_index(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_add_to_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_del_from_db(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_add_eth_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_del_from_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_update_eth_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_update_tp_y1731_asic(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep);

int32
_sys_goldengate_oam_lmep_update_master_chip(sys_oam_chan_com_t* p_sys_chan, uint32* master_chipid);

/*RMEP DB API*/
sys_oam_rmep_com_t*
_sys_goldengate_oam_rmep_lkup(uint8 lchip, sys_oam_lmep_com_t* p_sys_lmep,
                             sys_oam_rmep_com_t* p_sys_rmep);
int32
_sys_goldengate_oam_rmep_build_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_free_index(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_add_to_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_del_from_db(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_add_eth_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_add_tp_y1731_to_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);


int32
_sys_goldengate_oam_rmep_del_from_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_update_eth_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_rmep_update_tp_y1731_asic(uint8 lchip, sys_oam_rmep_com_t* p_sys_rmep);

int32
_sys_goldengate_oam_get_mep_entry_num(uint8 lchip);

int32
_sys_goldengate_oam_get_mpls_entry_num(uint8 lchip);

uint8
_sys_goldengate_bfd_csf_convert(uint8 lchip, uint8 type, bool to_asic);

int32
_sys_goldengate_oam_get_nexthop_info(uint8 lchip, uint32 nhid, uint32 b_protection, sys_oam_nhop_info_t* p_nhop_info);

int32
_sys_goldengate_oam_check_nexthop_type (uint8 lchip, uint32 nhid, uint32 b_protection, uint8 mep_type);

int32
_sys_goldengate_oam_defect_read_defect_status(uint8 lchip, void* p_defect_info);
int32
_sys_goldengate_oam_get_mep_info(uint8 lchip, ctc_oam_mep_info_t* mep_info);

int32
_sys_goldengate_oam_get_stats_info(uint8 lchip, sys_oam_lm_com_t* p_sys_oam_lm, ctc_oam_stats_info_t* p_stat_info);

int32
_sys_goldengate_oam_build_defect_event(uint8 lchip, ctc_oam_mep_info_t* p_mep_info, ctc_oam_event_t* p_event);

int32
_sys_goldengate_oam_db_init(uint8 lchip, ctc_oam_global_t* p_com_glb);

int32
_sys_goldengate_oam_db_deinit(uint8 lchip);

int32
sys_goldengate_oam_get_session_type(uint8 lchip, sys_oam_lmep_com_t *p_oam_lmep, uint32 *session_type);

int32
sys_goldengate_oam_get_session_num(uint8 lchip);

int32
sys_goldengate_oam_get_defect_type_config(uint8 lchip ,ctc_oam_defect_t defect, sys_oam_defect_info_t *p_defect);

int32
_sys_goldengate_oam_get_rdi_defect_type(uint8 lchip, uint32 defect, uint8* defect_type, uint8* defect_sub_type);

#ifdef __cplusplus
}
#endif

#endif


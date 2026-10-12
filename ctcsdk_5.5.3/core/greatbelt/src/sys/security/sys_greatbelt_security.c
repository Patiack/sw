/**
 @file sys_greatbelt_security.c

 @date 2010-2-26

 @version v2.0

---file comments----
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_port.h"
#include "ctc_vector.h"
#include "sys_greatbelt_opf.h"
#include "ctc_linklist.h"

#include "sys_greatbelt_security.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_scl.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_l2_fdb.h"

#include "greatbelt/include/drv_tbl_reg.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_enum.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/

/**
 @brief define hash lookup type for scl
*/
enum sys_port_scl_hash_type_e
{
    SYS_PORT_HASH_SCL_MAC_SA          = 0x6,    /**< MACSA */
    SYS_PORT_HASH_SCL_PORT_MAC_SA     = 0x7,    /**< Port+MACSA 160bit */
    SYS_PORT_HASH_SCL_IPV4_SA         = 0x8,    /**< Ip4SA */
    SYS_PORT_HASH_SCL_PORT_IPV4_SA    = 0x9,    /**< Port+IPv4SA */
    SYS_PORT_HASH_SCL_MAX
};
typedef enum sys_port_scl_hash_type_e sys_port_scl_hash_type_t;

#define SYS_SECURITY_STMCTL_DEF_UPDATE_EN     1
#define SYS_SECURITY_STMCTL_DEF_MAX_ENTRY_NUM      (core_frequency - 1)
#define SYS_SECURITY_STMCTL_DEF_MAX_UPDT_PORT_NUM       319
#define SYS_SECURITY_STMCTL_DEF_UPDATE_THREHOLD (1000000 / (gb_security_master[lchip]->stmctl_granularity))
#define SYS_SECURITY_STMCTL_MAX_PORT_NUM     0x3FFF
#define SYS_SECURITY_STMCTL_UNIT     0  /* shift left 1 bit */

#define SYS_SECURITY_PORT_ISOLATION_MAX_GROUP_ID      31
#define SYS_SECURITY_PORT_ISOLATION_MIN_GROUP_ID      0

#define SYS_SECURITY_STMCTL_MAX_PORT_VLAN_NUM 64

#define SYS_SECURITY_STMCTL_DISABLE_THRD     0xFFFFFF;

#define SYS_SECURITY_LEARN_LIMIT_MAX    0x3FFFF

#define SYS_STMCTL_CHK_UCAST(mode, type) \
    { \
        if (mode) \
        { \
            if (CTC_SECURITY_STORM_CTL_ALL_UCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
        else \
        { \
            if (CTC_SECURITY_STORM_CTL_KNOWN_UCAST == type \
                || CTC_SECURITY_STORM_CTL_UNKNOWN_UCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
    }

#define SYS_STMCTL_CHK_MCAST(mode, type) \
    { \
        if (mode) \
        { \
            if (CTC_SECURITY_STORM_CTL_ALL_MCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
        else \
        { \
            if (CTC_SECURITY_STORM_CTL_KNOWN_MCAST == type \
                || CTC_SECURITY_STORM_CTL_UNKNOWN_MCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_INVALID_PARAM; \
            } \
        } \
    }

#define SYS_LEARN_LIMIT_NUM_CHECK(LIMIT_NUM)\
    do {    \
            if (((LIMIT_NUM) > SYS_SECURITY_LEARN_LIMIT_MAX ) && ((LIMIT_NUM) != 0xFFFFFFFF)) { \
                return CTC_E_INVALID_PARAM; }\
    } while (0)

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
enum sys_greatbelt_security_ipsg_flag_e
{
    SYS_SECURITY_IPSG_MAC_SUPPORT = 0x01,
    SYS_SECURITY_IPSG_IPV4_SUPPORT = 0x02,
    SYS_SECURITY_IPSG_IPV6_SUPPORT = 0x04,
    SYS_SECURITY_IPSG_MAX
};
typedef enum sys_greatbelt_security_ipsg_flag_e sys_greatbelt_security_ipsg_flag_t;

struct sys_stmctl_node_s
{
    ctc_slistnode_t head;
    uint16 gport;
    uint16 vlan_id;
    uint8  op;
    uint8  stmctl_en;
    uint8  type_bitmap;
    uint8  rsv;
    uint32  stmctl_offset;
};
typedef struct sys_stmctl_node_s sys_stmctl_node_t;

typedef struct
{
    uint32 running_count; /** running count*/
    uint32 limit_num;   /** limit_num */
    uint32 limit_action;
}sys_security_port_learn_limit_t;

typedef struct
{
    uint32 running_count;
    uint32 limit_num;   /** limit_num */
    uint32 limit_action;
}sys_security_vlan_learn_limit_t;
typedef struct
{
    uint32 running_count;
    uint32 limit_num;   /** limit_num */
    uint32 limit_action;
    uint8  limit_en;
}sys_security_system_learn_limit_t;

struct sys_security_master_s
{
    ctc_security_stmctl_glb_cfg_t stmctl_cfg;
    ctc_slist_t*  p_stmctl_list;
    uint8 flag;
    uint32 stmctl_granularity; /* (100/1000/10000) from ctc_security_storm_ctl_granularity_t */
    ctc_vector_t* port_limit;   /** node is sys_security_port_learn_limit_t,key is port */
    ctc_vector_t* vlan_limit;   /** node is sys_security_port_learn_limit_t,key is vlan */
    sys_security_system_learn_limit_t  system_limit; /** system_limit_num*/
    uint32 mac_eid;
    uint32 ipv4_eid;
    uint32 ipv6_eid;
    sal_mutex_t *mutex;
};

typedef struct sys_security_master_s sys_security_master_t;

sys_security_master_t* gb_security_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_SECURITY_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == gb_security_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_SECURITY_CREATE_LOCK(lchip)                         \
    do                                                          \
    {                                                           \
        sal_mutex_create(&gb_security_master[lchip]->mutex);    \
        if (NULL == gb_security_master[lchip]->mutex)           \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_SECURITY_LOCK(lchip) \
    sal_mutex_lock(gb_security_master[lchip]->mutex)

#define SYS_SECURITY_UNLOCK(lchip) \
    sal_mutex_unlock(gb_security_master[lchip]->mutex)

#define CTC_ERROR_RETURN_SECURITY_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(gb_security_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/


sys_stmctl_node_t*
_sys_greatbelt_stmctl_db_get_node(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg)
{
    ctc_slistnode_t* ctc_slistnode = NULL;
    sys_stmctl_node_t* p_stmctl_node = NULL;

    CTC_SLIST_LOOP(gb_security_master[lchip]->p_stmctl_list, ctc_slistnode)
    {
        p_stmctl_node = _ctc_container_of(ctc_slistnode, sys_stmctl_node_t, head);
        if (NULL != p_stmctl_node)
        {
            if((p_stmctl_cfg->op == p_stmctl_node->op)
             &&(((p_stmctl_cfg->gport == p_stmctl_node->gport)&& (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op))
                || ((p_stmctl_cfg->vlan_id == p_stmctl_node->vlan_id)&& (CTC_SECURITY_STORM_CTL_OP_VLAN == p_stmctl_cfg->op))))
            {
                return p_stmctl_node;
            }
        }
    }

    return NULL;

}

uint32
_sys_greatbelt_stmctl_db_add_node(uint8 lchip,
                                  ctc_security_stmctl_cfg_t* p_stmctl_cfg,
                                  uint32* p_ofsset)
{
    sys_greatbelt_opf_t opf;
    sys_stmctl_node_t* p_stmctl_node = NULL;
    ctc_security_stmctl_cfg_t stmctl_cfg;
    uint8 need_alloc = 1;
    int32 ret = CTC_E_NONE;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&stmctl_cfg, 0, sizeof(stmctl_cfg));

    SYS_SECURITY_LOCK(lchip);
    p_stmctl_node = _sys_greatbelt_stmctl_db_get_node(lchip, p_stmctl_cfg);

    if (NULL != p_stmctl_node)
    {
        *p_ofsset = p_stmctl_node->stmctl_offset;
        CTC_BIT_SET(p_stmctl_node->type_bitmap, p_stmctl_cfg->type);

        SYS_SECURITY_UNLOCK(lchip);
        SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Entry(p_stmctl_node) Already Exist\n");
        return CTC_E_NONE;
    }

    p_stmctl_node = mem_malloc(MEM_STMCTL_MODULE, sizeof(sys_stmctl_node_t));
    if (NULL == p_stmctl_node)
    {
        SYS_SECURITY_UNLOCK(lchip);
        SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "No memory!!!\n");
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_stmctl_node, 0, sizeof(sys_stmctl_node_t));
    p_stmctl_node->gport = p_stmctl_cfg->gport;
    p_stmctl_node->vlan_id = p_stmctl_cfg->vlan_id;
    p_stmctl_node->op = p_stmctl_cfg->op;
    p_stmctl_node->stmctl_en  = p_stmctl_cfg->storm_en;
    CTC_BIT_SET(p_stmctl_node->type_bitmap, p_stmctl_cfg->type);
#if 0
    if ((p_stmctl_cfg->op == CTC_SECURITY_STORM_CTL_OP_PORT) && (lchip_num > 1))
    {
        sal_memcpy(&stmctl_cfg, p_stmctl_cfg, sizeof(stmctl_cfg));
        sys_greatbelt_get_gchip_id(lchip, &gchip1);
        stmctl_cfg.gport &= 0xFF;
        stmctl_cfg.gport |= gchip1 << 8;
        p_stmctl_node_ex = _sys_greatbelt_stmctl_db_get_node(lchip, &stmctl_cfg);
        if (NULL != p_stmctl_node_ex)
        {
            p_stmctl_node->stmctl_offset = p_stmctl_node_ex->stmctl_offset;
            need_alloc = 0;
        }
    }
#endif
    if (need_alloc)
    {
        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type = OPF_STORM_CTL;
        opf.pool_index = 0;
        ret = sys_greatbelt_opf_alloc_offset(lchip, &opf, 1, p_ofsset);
        if (CTC_E_NONE != ret)
        {
            SYS_SECURITY_UNLOCK(lchip);
            mem_free(p_stmctl_node);
            return ret;
        }

        p_stmctl_node->stmctl_offset = *p_ofsset;

        SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "alloc stmctl_offset: %d \n", *p_ofsset);
    }

    ctc_slist_add_tail(gb_security_master[lchip]->p_stmctl_list, &(p_stmctl_node->head));
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;

}

uint32
_sys_greatbelt_stmctl_db_del_node(uint8 lchip,
                                  ctc_security_stmctl_cfg_t* p_stmctl_cfg,
                                  uint32* p_ofsset)
{
    sys_greatbelt_opf_t opf;
    sys_stmctl_node_t* p_stmctl_node = NULL;
    ctc_security_stmctl_cfg_t stmctl_cfg;
    uint8 need_free = 1;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&stmctl_cfg, 0, sizeof(stmctl_cfg));

    SYS_SECURITY_LOCK(lchip);
    p_stmctl_node = _sys_greatbelt_stmctl_db_get_node(lchip, p_stmctl_cfg);
    if (NULL == p_stmctl_node)
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_BIT_UNSET(p_stmctl_node->type_bitmap, p_stmctl_cfg->type);

    if (p_stmctl_node->type_bitmap > 0)
    {
        *p_ofsset = p_stmctl_node->stmctl_offset;
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_NONE;
    }
#if 0
    if ((p_stmctl_cfg->op == CTC_SECURITY_STORM_CTL_OP_PORT) && (lchip_num > 1))
    {
        sal_memcpy(&stmctl_cfg, p_stmctl_cfg, sizeof(stmctl_cfg));
        sys_greatbelt_get_gchip_id(lchip, &gchip1);
        stmctl_cfg.gport &= 0xFF;
        stmctl_cfg.gport |= gchip1 << 8;
        p_stmctl_node_ex = _sys_greatbelt_stmctl_db_get_node(lchip, &stmctl_cfg);
        if (NULL != p_stmctl_node_ex)
        {
            need_free = 0;
        }
    }
#endif
    if (need_free)
    {
        *p_ofsset = p_stmctl_node->stmctl_offset;

        sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
        opf.pool_type = OPF_STORM_CTL;
        opf.pool_index = 0;
        sys_greatbelt_opf_free_offset(lchip, &opf, 1, *p_ofsset);

        SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free stmctl_offset: %d \n", *p_ofsset);
    }

    ctc_slist_delete_node(gb_security_master[lchip]->p_stmctl_list, &(p_stmctl_node->head));
    SYS_SECURITY_UNLOCK(lchip);

    mem_free(p_stmctl_node);

    return CTC_E_NONE;
}

/*mac security*/
int32
sys_greatbelt_mac_security_set_port_security(uint8 lchip, uint16 gport, bool enable)
{
    uint32 value = enable;

    SYS_SECURITY_INIT_CHECK(lchip);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_mac_security_get_port_security(uint8 lchip, uint16 gport, bool* p_enable)
{
    uint32 value = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, &value));
    *p_enable = (value) ? 1 : 0;

    return CTC_E_NONE;
}

int32
sys_greatbelt_mac_security_set_port_mac_limit(uint8 lchip, uint16 gport, ctc_maclimit_action_t action)
{
    int32 ret = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:          /*learning enable, forwarding, not discard, learnt*/
        ret = sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, FALSE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_FWD:           /*learning disable, forwarding*/
        ret = sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, FALSE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, FALSE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:       /*learning enable, discard, not learnt*/
        ret = sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, TRUE);
        ret = ret ? ret : sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:         /*learning enable, discard and to cpu, not learnt*/
        ret = sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, TRUE);
        ret = ret ? ret : sys_greatbelt_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : sys_greatbelt_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, TRUE);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return ret;
}

int32
sys_greatbelt_mac_security_get_port_mac_limit(uint8 lchip, uint16 gport, ctc_maclimit_action_t* action)
{
    uint32 port_learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);

    ret = sys_greatbelt_port_get_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, &port_learn_en);
    ret |= sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, &mac_security_discard_en);
    ret |= sys_greatbelt_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, &security_excp_en);

    if (port_learn_en == TRUE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_NONE;
    }
    else if (port_learn_en == FALSE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_FWD;
    }
    else if (port_learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else if (port_learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == TRUE)
    {
        *action = CTC_MACLIMIT_ACTION_TOCPU;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return ret;
}

int32
_sys_greatbelt_set_vlan_security_excep_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;


    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_VlanSecurityExceptionEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));


    return CTC_E_NONE;
}

int32
_sys_greatbelt_get_vlan_security_excep_en(uint8 lchip, uint32* value)
{
    uint32 cmd = 0;

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_VlanSecurityExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    return CTC_E_NONE;
}

int32
sys_greatbelt_mac_security_set_vlan_mac_limit(uint8 lchip, uint16 vlan_id, ctc_maclimit_action_t action)
{
    int32 ret = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:      /*learning eanble, forwarding, not discard, learnt*/
        ret = sys_greatbelt_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, FALSE);
        ret = ret ? ret : _sys_greatbelt_set_vlan_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_FWD:       /*learning disable, forwarding*/
        ret = sys_greatbelt_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, FALSE);
        ret = ret ? ret : sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, FALSE);
        ret = ret ? ret : _sys_greatbelt_set_vlan_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:   /*learning enable, discard, not learnt*/
        ret =   sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, TRUE);
        ret = ret ? ret : sys_greatbelt_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_vlan_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:     /*learning enable, discard and to cpu, not learnt*/
        ret = sys_greatbelt_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, TRUE);
        ret = ret ? ret : sys_greatbelt_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_vlan_security_excep_en(lchip, TRUE);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return ret;
}

int32
sys_greatbelt_mac_security_get_vlan_mac_limit(uint8 lchip, uint16 vlan_id, ctc_maclimit_action_t* action)
{
    uint32 learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);

    ret = sys_greatbelt_vlan_get_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, &learn_en);
    ret |= sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, &mac_security_discard_en);
    ret |= _sys_greatbelt_get_vlan_security_excep_en(lchip, &security_excp_en);

    if (learn_en == TRUE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_NONE;
    }
    else if (learn_en == FALSE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_FWD;
    }
    else if (learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else if (learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == TRUE)
    {
        *action = CTC_MACLIMIT_ACTION_TOCPU;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_set_syetem_security_learn_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;

    value = (value) ? 0 : 1;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_LearningDisable_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_get_system_security_learn_en(uint8 lchip, uint32* value)
{
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);
    cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_LearningDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    *value = (*value) ? 0 : 1;
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_set_syetem_security_excep_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_SystemSecurityExceptionEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_get_system_security_excep_en(uint8 lchip, uint32* value)
{
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);
    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_SystemSecurityExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_set_system_mac_security_discard_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;

    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_SystemMacSecurityDiscard_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_get_system_mac_security_discard_en(uint8 lchip, uint32* value)
{
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);
    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_SystemMacSecurityDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    return CTC_E_NONE;
}

int32
sys_greatbelt_mac_security_set_system_mac_limit(uint8 lchip, ctc_maclimit_action_t action)
{
    int32 ret = CTC_E_NONE;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:      /*learning eanble, forwarding, not discard, learnt*/
        ret = _sys_greatbelt_set_syetem_security_learn_en(lchip, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_system_mac_security_discard_en(lchip, FALSE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_FWD:       /*learning disable, forwarding*/
        ret = _sys_greatbelt_set_syetem_security_learn_en(lchip, FALSE);
        ret = ret ? ret : _sys_greatbelt_set_system_mac_security_discard_en(lchip, FALSE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:   /*learning enable, discard, not learnt*/
        ret = _sys_greatbelt_set_system_mac_security_discard_en(lchip, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_learn_en(lchip, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_excep_en(lchip, FALSE);
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:     /*learning enable, discard and to cpu, not learnt*/
        ret = _sys_greatbelt_set_system_mac_security_discard_en(lchip, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_learn_en(lchip, TRUE);
        ret = ret ? ret : _sys_greatbelt_set_syetem_security_excep_en(lchip, TRUE);
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return ret;

}

int32
sys_greatbelt_mac_security_get_system_mac_limit(uint8 lchip, ctc_maclimit_action_t* action)
{
    uint32 learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);
    ret = _sys_greatbelt_get_system_security_learn_en(lchip, &learn_en);
    ret |= _sys_greatbelt_get_system_mac_security_discard_en(lchip, &mac_security_discard_en);
    ret |= _sys_greatbelt_get_system_security_excep_en(lchip, &security_excp_en);

    if (learn_en == TRUE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_NONE;
    }
    else if (learn_en == FALSE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_FWD;
    }
    else if (learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else if (learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == TRUE)
    {
        *action = CTC_MACLIMIT_ACTION_TOCPU;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return ret;
}

STATIC int32
_sys_greatbelt_ip_source_guard_init_default_entry(uint8 lchip)
{
    sys_scl_entry_t def_entry;
    uint32 mac_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint32 ipv6_table_size = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);

    sal_memset(&def_entry, 0, sizeof(def_entry));
    def_entry.action.type = SYS_SCL_ACTION_INGRESS;
    CTC_SET_FLAG(def_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD);

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdMacKey_t, &mac_table_size));
    if (mac_table_size != 0)
    {
        def_entry.key.type = SYS_SCL_KEY_TCAM_MAC;

        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, def_entry.entry_id,0,1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, def_entry.entry_id, 1));
        gb_security_master[lchip]->flag = gb_security_master[lchip]->flag | SYS_SECURITY_IPSG_MAC_SUPPORT;
        gb_security_master[lchip]->mac_eid = def_entry.entry_id;
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv4Key_t, &ipv4_table_size));
    if (ipv4_table_size != 0)
    {
        def_entry.key.type = SYS_SCL_KEY_TCAM_IPV4;

        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, def_entry.entry_id,0,1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, def_entry.entry_id, 1));
        gb_security_master[lchip]->flag = gb_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV4_SUPPORT;
        gb_security_master[lchip]->ipv4_eid = def_entry.entry_id;
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv6Key_t, &ipv6_table_size));
    if (ipv6_table_size != 0)
    {
        def_entry.key.type = SYS_SCL_KEY_TCAM_IPV6;

        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_set_entry_priority(lchip, def_entry.entry_id,0,1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, def_entry.entry_id, 1));
        gb_security_master[lchip]->flag = gb_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV6_SUPPORT;
        gb_security_master[lchip]->ipv6_eid = def_entry.entry_id;
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_ip_source_guard_remove_default_entry(uint8 lchip)
{
    uint32 mac_table_size = 0;
    uint32 ipv4_table_size = 0;
    uint32 ipv6_table_size = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);


    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdMacKey_t, &mac_table_size));
    if (mac_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, gb_security_master[lchip]->mac_eid, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, gb_security_master[lchip]->mac_eid, 1));
        gb_security_master[lchip]->mac_eid = 0;
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv4Key_t, &ipv4_table_size));
    if (ipv4_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, gb_security_master[lchip]->ipv4_eid, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, gb_security_master[lchip]->ipv4_eid, 1));
        gb_security_master[lchip]->ipv4_eid = 0;
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdIpv6Key_t, &ipv6_table_size));
    if (ipv6_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, gb_security_master[lchip]->ipv6_eid, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, gb_security_master[lchip]->ipv6_eid, 1));
        gb_security_master[lchip]->ipv6_eid = 0;
    }

    return CTC_E_NONE;

}

/*ip source guard*/
int32
sys_greatbelt_ip_source_guard_add_entry(uint8 lchip, ctc_security_ip_source_guard_elem_t* p_elem)
{
    sys_scl_entry_t ipv4_entry;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_elem);
    CTC_GLOBAL_PORT_CHECK(p_elem->gport);

    sal_memset(&ipv4_entry, 0, sizeof(ipv4_entry));

    if (p_elem->ip_src_guard_type == CTC_SECURITY_BINDING_TYPE_IPV6_MAC)
    {
        if (!(CTC_FLAG_ISSET(gb_security_master[lchip]->flag, SYS_SECURITY_IPSG_IPV6_SUPPORT)))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        ipv4_entry.key.type = SYS_SCL_KEY_HASH_IPV6;
        ipv4_entry.action.type = SYS_SCL_ACTION_INGRESS;
        sal_memcpy(ipv4_entry.key.u.hash_ipv6_key.ip_sa, p_elem->ipv6_sa, sizeof(ipv6_addr_t));
        CTC_SET_FLAG(ipv4_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        ipv4_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_MACSA;
        sal_memcpy(ipv4_entry.action.u.igs_action.bind.mac_sa , p_elem->mac_sa, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD, &ipv4_entry, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, ipv4_entry.entry_id, 1));
        return CTC_E_NONE;
    }

    if (CTC_IP_VER_4 != p_elem->ipv4_or_ipv6)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (((p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_MAC)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_VLAN))) \
        && (!(CTC_FLAG_ISSET(gb_security_master[lchip]->flag, SYS_SECURITY_IPSG_IPV4_SUPPORT))))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (((p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_MAC)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_MAC_VLAN))) \
        && (!(CTC_FLAG_ISSET(gb_security_master[lchip]->flag, SYS_SECURITY_IPSG_MAC_SUPPORT))))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:%d\n", p_elem->gport);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ip source guard type:%d\n", p_elem->ip_src_guard_type);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4 or ipv6:%s\n", (CTC_IP_VER_4 == p_elem->ipv4_or_ipv6) ? "ipv4" : "ipv6");

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4sa:%d.%d.%d.%d\n", (p_elem->ipv4_sa >> 24) & 0xFF, (p_elem->ipv4_sa >> 16) & 0xFF, \
                         (p_elem->ipv4_sa >> 8) & 0xFF, p_elem->ipv4_sa & 0xFF);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac:%x:%x:%x:%x:%x:%x\n", p_elem->mac_sa[0], p_elem->mac_sa[1], p_elem->mac_sa[2], \
                         p_elem->mac_sa[3], p_elem->mac_sa[4], p_elem->mac_sa[5]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan:%d\n", p_elem->vid);




    ipv4_entry.action.type = SYS_SCL_ACTION_INGRESS;
    switch (p_elem->ip_src_guard_type)
    {
    case CTC_SECURITY_BINDING_TYPE_IP:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        ipv4_entry.key.u.hash_port_ipv4_key.gport   = p_elem->gport;
        ipv4_entry.key.u.hash_port_ipv4_key.ip_sa   = p_elem->ipv4_sa;

        /* do nothing */
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        ipv4_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        ipv4_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;

        CTC_SET_FLAG(ipv4_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        ipv4_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_MACSA;
        sal_memcpy(ipv4_entry.action.u.igs_action.bind.mac_sa, p_elem->mac_sa, sizeof(mac_addr_t));

        break;

    case CTC_SECURITY_BINDING_TYPE_IP_VLAN:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        ipv4_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        ipv4_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(ipv4_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        ipv4_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_VLAN;
        ipv4_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(ipv4_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        ipv4_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        ipv4_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(ipv4_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        ipv4_entry.action.u.igs_action.bind.type        = CTC_SCL_BIND_TYPE_IPV4SA_VLAN;
        ipv4_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        ipv4_entry.action.u.igs_action.bind.ipv4_sa =  p_elem->ipv4_sa;
        break;

    case CTC_SECURITY_BINDING_TYPE_MAC:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(ipv4_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        ipv4_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        ipv4_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        /* do nothing */
        break;

    case CTC_SECURITY_BINDING_TYPE_MAC_VLAN:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(ipv4_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        ipv4_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        ipv4_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(ipv4_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        ipv4_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_VLAN;
        ipv4_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_greatbelt_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD, &ipv4_entry, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_install_entry(lchip, ipv4_entry.entry_id, 1));

    return CTC_E_NONE;
}

int32
sys_greatbelt_ip_source_guard_remove_entry(uint8 lchip, ctc_security_ip_source_guard_elem_t* p_elem)
{
    uint8 lport = 0;
    uint8 is_linkagg = 0;
    sys_scl_entry_t ipv4_entry;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_elem);
    CTC_GLOBAL_PORT_CHECK(p_elem->gport);

    sal_memset(&ipv4_entry, 0, sizeof(ipv4_entry));

    if (p_elem->ip_src_guard_type == CTC_SECURITY_BINDING_TYPE_IPV6_MAC)
    {
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_IPV6;
        sal_memcpy(ipv4_entry.key.u.hash_ipv6_key.ip_sa, p_elem->ipv6_sa, sizeof(ipv6_addr_t));

        CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &ipv4_entry, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD));
        CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, ipv4_entry.entry_id, 1));
        CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, ipv4_entry.entry_id, 1));

        return CTC_E_NONE;
    }


    is_linkagg = CTC_IS_LINKAGG_PORT(p_elem->gport);
    if (!is_linkagg)   /*not linkagg*/
    {
        SYS_MAP_GPORT_TO_LPORT(p_elem->gport, lchip, lport);
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:%d\n", p_elem->gport);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ip source guard type:%d\n", p_elem->ip_src_guard_type);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4 or ipv6:%d\n", p_elem->ipv4_or_ipv6);
    if (CTC_IP_VER_4 != p_elem->ipv4_or_ipv6)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4sa:%d.%d.%d.%d\n", (p_elem->ipv4_sa >> 24) & 0xFF, (p_elem->ipv4_sa >> 16) & 0xFF, \
                         (p_elem->ipv4_sa >> 8) & 0xFF, p_elem->ipv4_sa & 0xFF);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac:%x %x %x %x %x %x\n", p_elem->mac_sa[0], p_elem->mac_sa[1], p_elem->mac_sa[2], \
                         p_elem->mac_sa[3], p_elem->mac_sa[4], p_elem->mac_sa[5]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan:%d\n", p_elem->vid);

    switch (p_elem->ip_src_guard_type)
    {
    case CTC_SECURITY_BINDING_TYPE_IP:
    case CTC_SECURITY_BINDING_TYPE_IP_MAC:
    case CTC_SECURITY_BINDING_TYPE_IP_VLAN:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        ipv4_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        ipv4_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN:
    case CTC_SECURITY_BINDING_TYPE_MAC:
    case CTC_SECURITY_BINDING_TYPE_MAC_VLAN:
        ipv4_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(ipv4_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        ipv4_entry.key.u.hash_port_mac_key.gport= p_elem->gport;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_scl_get_entry_id(lchip, &ipv4_entry, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD));
    CTC_ERROR_RETURN(sys_greatbelt_scl_uninstall_entry(lchip, ipv4_entry.entry_id, 1));
    CTC_ERROR_RETURN(sys_greatbelt_scl_remove_entry(lchip, ipv4_entry.entry_id, 1));
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_storm_ctl_set_mode(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg, uint16* p_stmctl_offset)
{
    uint32 stmctl_ptr = 0;
    uint32 stmctl_offset = 0;
    ds_storm_ctl_t storm_ctl;
    uint32 core_frequency;
    uint32 unit = 0;
    uint32 cmd = 0;
    uint32 update_count_per_sec = 0;

    CTC_PTR_VALID_CHECK(p_stmctl_cfg);
    CTC_PTR_VALID_CHECK(p_stmctl_offset);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (p_stmctl_cfg->storm_en)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stmctl_db_add_node(lchip, p_stmctl_cfg, &stmctl_offset));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_stmctl_db_del_node(lchip, p_stmctl_cfg, &stmctl_offset));
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "stmctl_offset: %d \n", stmctl_offset);
    *p_stmctl_offset = stmctl_offset;

    switch (p_stmctl_cfg->type)
    {
    case CTC_SECURITY_STORM_CTL_KNOWN_MCAST:
        stmctl_ptr = (1 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_UNKNOWN_MCAST:
    case CTC_SECURITY_STORM_CTL_ALL_MCAST:
        stmctl_ptr = (2 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_KNOWN_UCAST:
        stmctl_ptr = (3 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_UNKNOWN_UCAST:
    case CTC_SECURITY_STORM_CTL_ALL_UCAST:
        stmctl_ptr = (4 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_BCAST:
        stmctl_ptr = (0 << 6) | (stmctl_offset & 0x3F);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&storm_ctl, 0, sizeof(ds_storm_ctl_t));
    cmd = DRV_IOR(DsStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_ptr, cmd, &storm_ctl));
    storm_ctl.exception_en = p_stmctl_cfg->discarded_to_cpu ? 1 : 0;
    storm_ctl.use_packet_count = p_stmctl_cfg->mode == CTC_SECURITY_STORM_CTL_MODE_PPS ? 1 : 0;
    storm_ctl.running_count = 0;    /* must clear */

    core_frequency = sys_greatbelt_get_core_freq(0);

    unit = 1 << SYS_SECURITY_STMCTL_UNIT;
    update_count_per_sec = core_frequency * 1000000 / SYS_SECURITY_STMCTL_DEF_UPDATE_THREHOLD / (SYS_SECURITY_STMCTL_DEF_MAX_ENTRY_NUM+1);
    if (p_stmctl_cfg->storm_en)
    {
        storm_ctl.threshold = p_stmctl_cfg->threshold / update_count_per_sec / unit;
    }
    else
    {
         storm_ctl.threshold = SYS_SECURITY_STMCTL_DISABLE_THRD;
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "StormCtlPtr:%d, unit: %d, DstormCtl.threshold: %d\n", stmctl_ptr,  unit, storm_ctl.threshold);
    cmd = DRV_IOW(DsStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_ptr, cmd, &storm_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_storm_ctl_get_mode(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg)
{
    sys_stmctl_node_t* p_stmctl_node = NULL;
    uint32 stmctl_ptr = 0;
    uint32 stmctl_offset = 0;
    ds_storm_ctl_t storm_ctl;
    uint32 core_frequency;
    uint32 update_count_per_sec = 0;
    uint32 unit = 0;
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(p_stmctl_cfg);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_LOCK(lchip);
    p_stmctl_node = _sys_greatbelt_stmctl_db_get_node(lchip, p_stmctl_cfg);

    if (NULL == p_stmctl_node)
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    stmctl_offset = p_stmctl_node->stmctl_offset;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "stmctl_offset: %d \n", stmctl_offset);

    switch (p_stmctl_cfg->type)
    {
    case CTC_SECURITY_STORM_CTL_KNOWN_MCAST:
        stmctl_ptr = (1 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_UNKNOWN_MCAST:
    case CTC_SECURITY_STORM_CTL_ALL_MCAST:
        stmctl_ptr = (2 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_KNOWN_UCAST:
        stmctl_ptr = (3 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_UNKNOWN_UCAST:
    case CTC_SECURITY_STORM_CTL_ALL_UCAST:
        stmctl_ptr = (4 << 6) | (stmctl_offset & 0x3F);
        break;

    case CTC_SECURITY_STORM_CTL_BCAST:
        stmctl_ptr = (0 << 6) | (stmctl_offset & 0x3F);
        break;

    default:
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&storm_ctl, 0, sizeof(ds_storm_ctl_t));
    cmd = DRV_IOR(DsStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_SECURITY_UNLOCK(DRV_IOCTL(lchip, stmctl_ptr, cmd, &storm_ctl));

    core_frequency = sys_greatbelt_get_core_freq(0);

    unit = 1 << SYS_SECURITY_STMCTL_UNIT;
    update_count_per_sec = core_frequency * 1000000 / SYS_SECURITY_STMCTL_DEF_UPDATE_THREHOLD / (SYS_SECURITY_STMCTL_DEF_MAX_ENTRY_NUM+1);
    p_stmctl_cfg->storm_en = CTC_IS_BIT_SET(p_stmctl_node->type_bitmap,p_stmctl_cfg->type);

    p_stmctl_cfg->discarded_to_cpu = storm_ctl.exception_en;
    p_stmctl_cfg->mode = storm_ctl.use_packet_count ? CTC_SECURITY_STORM_CTL_MODE_PPS : CTC_SECURITY_STORM_CTL_MODE_BPS;
    p_stmctl_cfg->threshold = p_stmctl_cfg->storm_en ? unit * storm_ctl.threshold * update_count_per_sec : 0;
    SYS_SECURITY_UNLOCK(lchip);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "StormCtlPtr:%d, unit: %d, DsStormCtl.threshold: %d\n", stmctl_ptr, unit, storm_ctl.threshold);

    return CTC_E_NONE;
}




/*storm control*/
int32
sys_greatbelt_storm_ctl_set_cfg(uint8 lchip, ctc_security_stmctl_cfg_t* stmctl_cfg)
{
    uint8 lport;
    uint16 stmctl_offset = 0;
    sys_stmctl_node_t* p_stmctl_node = NULL;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stmctl_cfg);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                         "type:%d , mode:%d, gport :0x%x ,vlan:%d, op:%d, storm_en:%d, discarded_to_cpu:%d ,threshold :%d \n",
                         stmctl_cfg->type, stmctl_cfg->mode, stmctl_cfg->gport, stmctl_cfg->vlan_id,
                         stmctl_cfg->op, stmctl_cfg->storm_en, stmctl_cfg->discarded_to_cpu, stmctl_cfg->threshold)

    SYS_SECURITY_LOCK(lchip);
    /*ucast mode judge*/
    SYS_STMCTL_CHK_UCAST(gb_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode, stmctl_cfg->type);

    /*Mcast mode judge*/
    SYS_STMCTL_CHK_MCAST(gb_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode, stmctl_cfg->type);
    SYS_SECURITY_UNLOCK(lchip);

    if (CTC_SECURITY_STORM_CTL_OP_PORT == stmctl_cfg->op)
    {
        SYS_MAP_GPORT_TO_LPORT(stmctl_cfg->gport, lchip, lport);
        CTC_ERROR_RETURN(_sys_greatbelt_storm_ctl_set_mode(lchip, stmctl_cfg, &stmctl_offset));

        /*port storm ctl en*/
        if (stmctl_cfg->storm_en)
        {
            sys_greatbelt_port_set_internal_property(lchip, stmctl_cfg->gport, SYS_PORT_PROP_STMCTL_OFFSET, stmctl_offset);
            sys_greatbelt_port_set_internal_property(lchip, stmctl_cfg->gport, SYS_PORT_PROP_STMCTL_EN, stmctl_cfg->storm_en);
        }
        else
        {
            p_stmctl_node = _sys_greatbelt_stmctl_db_get_node(lchip, stmctl_cfg);
            if (!p_stmctl_node)
            {
                sys_greatbelt_port_set_internal_property(lchip, stmctl_cfg->gport, SYS_PORT_PROP_STMCTL_EN, stmctl_cfg->storm_en);
            }
        }

    }
    else if (CTC_SECURITY_STORM_CTL_OP_VLAN == stmctl_cfg->op)
    {
        CTC_VLAN_RANGE_CHECK(stmctl_cfg->vlan_id);

        CTC_ERROR_RETURN(_sys_greatbelt_storm_ctl_set_mode(lchip, stmctl_cfg, &stmctl_offset));

        if (stmctl_cfg->storm_en)
        {
            CTC_ERROR_RETURN(sys_greatbelt_vlan_set_internal_property
                                 (lchip, stmctl_cfg->vlan_id, SYS_VLAN_PROP_VLAN_STORM_CTL_EN, stmctl_cfg->storm_en));
            CTC_ERROR_RETURN(sys_greatbelt_vlan_set_internal_property
                                 (lchip, stmctl_cfg->vlan_id, SYS_VLAN_PROP_VLAN_STORM_CTL_PTR, stmctl_offset));
        }
        else
        {
            SYS_SECURITY_LOCK(lchip);
            p_stmctl_node = _sys_greatbelt_stmctl_db_get_node(lchip, stmctl_cfg);
            if (!p_stmctl_node)
            {
                sys_greatbelt_vlan_set_internal_property(lchip, stmctl_cfg->vlan_id, SYS_VLAN_PROP_VLAN_STORM_CTL_EN, stmctl_cfg->storm_en);
            }
            SYS_SECURITY_UNLOCK(lchip);
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_storm_ctl_get_cfg(uint8 lchip, ctc_security_stmctl_cfg_t* stmctl_cfg)
{
    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(stmctl_cfg);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                         "type:%d , mode:%d, gport :0x%x ,vlan:%d, op:%d, storm_en:%d, discarded_to_cpu:%d ,threshold :%d \n",
                         stmctl_cfg->type, stmctl_cfg->mode, stmctl_cfg->gport, stmctl_cfg->vlan_id,
                         stmctl_cfg->op, stmctl_cfg->storm_en, stmctl_cfg->discarded_to_cpu, stmctl_cfg->threshold);

    SYS_SECURITY_LOCK(lchip);
    /*ucast mode judge*/
    SYS_STMCTL_CHK_UCAST(gb_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode, stmctl_cfg->type);

    /*Mcast mode judge*/
    SYS_STMCTL_CHK_MCAST(gb_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode, stmctl_cfg->type);
    SYS_SECURITY_UNLOCK(lchip);

    CTC_ERROR_RETURN(_sys_greatbelt_storm_ctl_get_mode(lchip, stmctl_cfg));

    return CTC_E_NONE;

}

int32
sys_greatbelt_storm_ctl_set_global_cfg(uint8 lchip, ctc_security_stmctl_glb_cfg_t* p_glb_cfg)
{

    uint32 cmd = 0;
    uint32 value = 0;
    uint32 update_threshold = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_glb_cfg);

    SYS_SECURITY_DBG_FUNC();
    SYS_SECURITY_DBG_INFO("ipg_en%d ustorm_ctl_mode:%d,mstorm_ctl_mode:%d\n", p_glb_cfg->ipg_en, p_glb_cfg->ustorm_ctl_mode, p_glb_cfg->mstorm_ctl_mode);

    SYS_SECURITY_LOCK(lchip);
    /* can not configure global granularity if created storm control node */
    if (!CTC_SLIST_ISEMPTY(gb_security_master[lchip]->p_stmctl_list) && (p_glb_cfg->granularity!= gb_security_master[lchip]->stmctl_cfg.granularity))
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }

    gb_security_master[lchip]->stmctl_cfg.granularity = p_glb_cfg->granularity;

    switch (gb_security_master[lchip]->stmctl_cfg.granularity)
    {
        case CTC_SECURITY_STORM_CTL_GRANU_100:
            gb_security_master[lchip]->stmctl_granularity = 100;
            break;
        case CTC_SECURITY_STORM_CTL_GRANU_1000:
            gb_security_master[lchip]->stmctl_granularity = 1000;
            break;
        case CTC_SECURITY_STORM_CTL_GRANU_10000:
            gb_security_master[lchip]->stmctl_granularity = 10000;
            break;
        default:
            gb_security_master[lchip]->stmctl_granularity = 100;
            break;
    }


    update_threshold = SYS_SECURITY_STMCTL_DEF_UPDATE_THREHOLD;

    value = p_glb_cfg->ipg_en ? 1 : 0;
    cmd = DRV_IOW(IpeBridgeStormCtl_t, IpeBridgeStormCtl_IpgEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    value = p_glb_cfg->ustorm_ctl_mode ? 1 : 0;
    cmd = DRV_IOW(IpeBridgeCtl_t, IpeBridgeCtl_UnicastStormControlMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    value = p_glb_cfg->mstorm_ctl_mode ? 1 : 0;
    cmd = DRV_IOW(IpeBridgeCtl_t, IpeBridgeCtl_MulticastStormControlMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    cmd = DRV_IOW(IpeBridgeStormCtl_t, IpeBridgeStormCtl_StormCtlUpdateThreshold_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &update_threshold));


    gb_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode = p_glb_cfg->ustorm_ctl_mode;
    gb_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode = p_glb_cfg->mstorm_ctl_mode;
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_storm_ctl_get_global_cfg(uint8 lchip, ctc_security_stmctl_glb_cfg_t* p_glb_cfg)
{
    uint32 cmd = 0;
    uint32 value = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_glb_cfg);

    SYS_SECURITY_DBG_FUNC();

    cmd = DRV_IOR(IpeBridgeStormCtl_t, IpeBridgeStormCtl_IpgEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_glb_cfg->ipg_en = value;

    cmd = DRV_IOR(IpeBridgeCtl_t, IpeBridgeCtl_UnicastStormControlMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_glb_cfg->ustorm_ctl_mode = value;

    cmd = DRV_IOR(IpeBridgeCtl_t, IpeBridgeCtl_MulticastStormControlMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_glb_cfg->mstorm_ctl_mode = value;

    SYS_SECURITY_LOCK(lchip);
    p_glb_cfg->granularity = gb_security_master[lchip]->stmctl_cfg.granularity;
    SYS_SECURITY_UNLOCK(lchip);

    SYS_SECURITY_DBG_INFO("ipg_en:%d, ustorm_ctl_mode:%d, mstorm_ctl_mode:%d, granularity:%d\n", \
                           p_glb_cfg->ipg_en, p_glb_cfg->ustorm_ctl_mode, p_glb_cfg->mstorm_ctl_mode, p_glb_cfg->granularity);

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_isolation_set_route_obey_isolated_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);

    tmp = enable ? 1 : 0;

    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_RouteObeyIsolate_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_greatbelt_port_isolation_get_route_obey_isolated_en(uint8 lchip, bool* p_enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_RouteObeyIsolate_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *p_enable = (field_val == 1) ? TRUE : FALSE;

    return CTC_E_NONE;
}

/*arp snooping*/
int32
sys_greatbelt_arp_snooping_set_address_check_en(uint8 lchip, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_INIT_CHECK(lchip);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    tmp = enable ? 1 : 0;

    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpSrcMacCheckEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_greatbelt_arp_snooping_set_address_check_exception_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_INIT_CHECK(lchip);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    tmp = enable ? 1 : 0;

    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_ArpCheckExceptionEn_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));


    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_greatbelt_security_tcam_reinit(uint8 lchip, uint8 is_add)
{
    if (is_add)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ip_source_guard_init_default_entry(lchip));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_ip_source_guard_remove_default_entry(lchip));
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_security_init(uint8 lchip)
{

    uint32 cmd = 0;
    uint32 core_frequency = 0;
    uint32 field_val = 0;
    ipe_bridge_storm_ctl_t storm_brg_ctl;
    sys_greatbelt_opf_t opf;
    uint32 stmctl_offset = 0;
    ds_storm_ctl_t storm_ctl;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    LCHIP_CHECK(lchip);

    if (NULL != gb_security_master[lchip])
    {
        return CTC_E_NONE;
    }

    gb_security_master[lchip] = (sys_security_master_t*)mem_malloc(MEM_STMCTL_MODULE, sizeof(sys_security_master_t));
    if (NULL == gb_security_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gb_security_master[lchip], 0, sizeof(sys_security_master_t));

    gb_security_master[lchip]->p_stmctl_list = ctc_slist_new();
    if (NULL == gb_security_master[lchip]->p_stmctl_list)
    {
        mem_free(gb_security_master[lchip]);
        gb_security_master[lchip] = NULL;
        return CTC_E_NO_MEMORY;
    }

    /*storm ctl support known unicast/mcast mode*/
    gb_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode = 1;
    gb_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode = 1;
    gb_security_master[lchip]->stmctl_cfg.ipg_en = 0;
    gb_security_master[lchip]->stmctl_cfg.granularity     = CTC_SECURITY_STORM_CTL_GRANU_100;
    gb_security_master[lchip]->stmctl_granularity = 100;

    /*init storm port and vlan base ptr offset*/
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    sys_greatbelt_opf_init(lchip, OPF_STORM_CTL, 1);
    opf.pool_type = OPF_STORM_CTL;
    opf.pool_index = 0;
    sys_greatbelt_opf_init_offset(lchip, &opf, 0, SYS_SECURITY_STMCTL_MAX_PORT_VLAN_NUM);

    /*init default storm cfg*/
    core_frequency = sys_greatbelt_get_core_freq(0);

    sal_memset(&storm_ctl, 0, sizeof(storm_ctl));
    for (stmctl_offset = 0; stmctl_offset < 320; stmctl_offset++)
    {
        storm_ctl.threshold = SYS_SECURITY_STMCTL_DISABLE_THRD;
        cmd = DRV_IOW(DsStormCtl_t, DRV_ENTRY_FLAG);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_offset, cmd, &storm_ctl));
    }


    cmd = DRV_IOR(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &storm_brg_ctl));
    storm_brg_ctl.ipg_en = 0;
    storm_brg_ctl.storm_ctl_max_port_num = SYS_SECURITY_STMCTL_DEF_MAX_ENTRY_NUM;
    storm_brg_ctl.storm_ctl_max_update_port_num = SYS_SECURITY_STMCTL_DEF_MAX_UPDT_PORT_NUM;
    storm_brg_ctl.storm_ctl_update_en = 1;
    storm_brg_ctl.oam_obey_storm_ctl = 0;
    storm_brg_ctl.storm_ctl_update_threshold = SYS_SECURITY_STMCTL_DEF_UPDATE_THREHOLD;
    storm_brg_ctl.storm_ctl_en31_0   = 0xFFFFFFFF;
    storm_brg_ctl.storm_ctl_en63_32  = 0xFFFFFFFF;
    storm_brg_ctl.packet_shift = SYS_SECURITY_STMCTL_UNIT;
    storm_brg_ctl.byte_shift = SYS_SECURITY_STMCTL_UNIT;
    cmd = DRV_IOW(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &storm_brg_ctl));



    /*open port isolation & pvlan function*/
    cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_IsolatedType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = 1;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_IsolatedType_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));


    CTC_ERROR_RETURN(_sys_greatbelt_ip_source_guard_init_default_entry(lchip));

    gb_security_master[lchip]->system_limit.limit_en= 0;
    gb_security_master[lchip]->port_limit = ctc_vector_init(4, SYS_GB_MAX_PORT_NUM_PER_CHIP/4);
    if (gb_security_master[lchip]->port_limit == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    gb_security_master[lchip]->vlan_limit = ctc_vector_init(4, (CTC_MAX_VLAN_ID + 1)/4);
    if (gb_security_master[lchip]->vlan_limit == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    SYS_SECURITY_CREATE_LOCK(lchip);
    sys_greatbelt_ftm_tcam_cb_register(lchip, SYS_FTM_TCAM_KEY_SECURITY, sys_greatbelt_security_tcam_reinit);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_security_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_security_deinit(uint8 lchip)
{
    ctc_slistnode_t* node = NULL, *next_node = NULL;

    LCHIP_CHECK(lchip);
    if (NULL == gb_security_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_greatbelt_opf_deinit(lchip, OPF_STORM_CTL);

    ctc_vector_traverse(gb_security_master[lchip]->port_limit, (vector_traversal_fn)_sys_greatbelt_security_free_node_data, NULL);
    ctc_vector_release(gb_security_master[lchip]->port_limit);

    ctc_vector_traverse(gb_security_master[lchip]->vlan_limit, (vector_traversal_fn)_sys_greatbelt_security_free_node_data, NULL);
    ctc_vector_release(gb_security_master[lchip]->vlan_limit);

    /*free stormctl slist*/
    CTC_SLIST_LOOP_DEL(gb_security_master[lchip]->p_stmctl_list, node, next_node)
    {
        ctc_slist_delete_node(gb_security_master[lchip]->p_stmctl_list, node);
        mem_free(node);
    }
    ctc_slist_delete(gb_security_master[lchip]->p_stmctl_list);

    sal_mutex_destroy(gb_security_master[lchip]->mutex);
    mem_free(gb_security_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_set_port_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    uint8 lport = 0;
    sys_security_port_learn_limit_t* p_node = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT((uint16)p_learning_limit->gport);
    if (lport >= SYS_GB_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_INVALID_PORT;
    }
    p_node = ctc_vector_get(gb_security_master[lchip]->port_limit, lport);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_security_port_learn_limit_t));
        ctc_vector_add(gb_security_master[lchip]->port_limit, lport, p_node);
        if (p_node == NULL)
        {
            mem_free(p_node);
            return CTC_E_NO_MEMORY;
        }
    }
    p_node->limit_num = p_learning_limit->limit_num;
    p_node->limit_action = p_learning_limit->limit_action;

    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        p_node->limit_num = SYS_SECURITY_LEARN_LIMIT_MAX;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_get_port_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    uint8 lport = 0;
    sys_security_port_learn_limit_t* p_node = NULL;

    lport = CTC_MAP_GPORT_TO_LPORT((uint16)p_learning_limit->gport);
    if (lport >= SYS_GB_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_INVALID_PORT;
    }

    p_node = ctc_vector_get(gb_security_master[lchip]->port_limit, CTC_MAP_GPORT_TO_LPORT((uint16)p_learning_limit->gport));
    if (NULL == p_node)
    {
        p_learning_limit->limit_num = SYS_SECURITY_LEARN_LIMIT_MAX;
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_NONE;
    }
    else
    {
        p_learning_limit->limit_num = p_node->limit_num;
        p_learning_limit->limit_action = p_node->limit_action;
        p_learning_limit->limit_count = p_node->running_count;
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_set_vlan_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    sys_security_vlan_learn_limit_t* p_node = NULL;

    if ((p_learning_limit->vlan < CTC_MIN_VLAN_ID) || (p_learning_limit->vlan > CTC_MAX_VLAN_ID))
    {
        return CTC_E_VLAN_INVALID_VLAN_ID;
    }

    p_node = ctc_vector_get(gb_security_master[lchip]->vlan_limit, p_learning_limit->vlan);
    if (NULL == p_node)
    {
        MALLOC_ZERO(MEM_FDB_MODULE, p_node, sizeof(sys_security_port_learn_limit_t));
        ctc_vector_add(gb_security_master[lchip]->vlan_limit, p_learning_limit->vlan, p_node);
        if (p_node == NULL)
        {
            mem_free(p_node);
            return CTC_E_NO_MEMORY;
        }
    }
    p_node->limit_num = p_learning_limit->limit_num;
    p_node->limit_action = p_learning_limit->limit_action;
    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        p_node->limit_num = SYS_SECURITY_LEARN_LIMIT_MAX;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_get_vlan_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    sys_security_vlan_learn_limit_t* p_node = NULL;

    if ((p_learning_limit->vlan < CTC_MIN_VLAN_ID) || (p_learning_limit->vlan > CTC_MAX_VLAN_ID))
    {
        return CTC_E_VLAN_INVALID_VLAN_ID;
    }

    p_node = ctc_vector_get(gb_security_master[lchip]->vlan_limit, p_learning_limit->vlan);
    if (NULL == p_node)
    {
        p_learning_limit->limit_num = SYS_SECURITY_LEARN_LIMIT_MAX;
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_NONE;
    }
    else
    {
        p_learning_limit->limit_num = p_node->limit_num;
        p_learning_limit->limit_action = p_node->limit_action;
        p_learning_limit->limit_count = p_node->running_count;
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_set_system_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    gb_security_master[lchip]->system_limit.limit_num = p_learning_limit->limit_num;
    gb_security_master[lchip]->system_limit.limit_action = p_learning_limit->limit_action;
    gb_security_master[lchip]->system_limit.limit_en= 1;

    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        gb_security_master[lchip]->system_limit.limit_num= SYS_SECURITY_LEARN_LIMIT_MAX;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_mac_security_get_system_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    p_learning_limit->limit_num = gb_security_master[lchip]->system_limit.limit_num;
    p_learning_limit->limit_action = gb_security_master[lchip]->system_limit.limit_action;
    p_learning_limit->limit_count = gb_security_master[lchip]->system_limit.running_count;

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_mac_security_set_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    if (CTC_SECURITY_LEARN_LIMIT_TYPE_PORT == p_learning_limit->limit_type)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_mac_security_set_port_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_greatbelt_mac_security_set_vlan_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_greatbelt_mac_security_set_system_learn_limit(lchip, p_learning_limit));
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_greatbelt_mac_security_set_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_learning_limit);
    SYS_LEARN_LIMIT_NUM_CHECK(p_learning_limit->limit_num);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_LOCK(lchip);
    CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_greatbelt_mac_security_set_learn_limit(lchip, p_learning_limit));
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;

}

int32
sys_greatbelt_mac_security_get_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit, uint32* p_running_num)
{

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_learning_limit);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_LOCK(lchip);
    if (CTC_SECURITY_LEARN_LIMIT_TYPE_PORT == p_learning_limit->limit_type)
    {
        CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_greatbelt_mac_security_get_port_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_greatbelt_mac_security_get_vlan_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_greatbelt_mac_security_get_system_learn_limit(lchip, p_learning_limit));
    }
    else
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_INTR_INVALID_PARAM;
    }

    if (p_learning_limit->limit_num == SYS_SECURITY_LEARN_LIMIT_MAX)
    {
        p_learning_limit->limit_num = 0xFFFFFFFF;
    }

    if (p_running_num)
    {
        *p_running_num = p_learning_limit->limit_count;
    }
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;

}
STATIC bool
_sys_greatbelt_mac_security_port_limit_handler(uint8 lchip, uint16 port_id, uint32 port_cnt, uint8 is_update)
{
    uint32 action = 0;
    uint8  gchip = 0;
    uint8  lport = 0;
    bool  is_max = FALSE;
    sys_security_port_learn_limit_t* p_port_node = NULL;

    SYS_MAP_GPORT_TO_LPORT(port_id, lchip, lport);
    p_port_node = ctc_vector_get(gb_security_master[lchip]->port_limit, lport);
    if (p_port_node == NULL)
    {
        return FALSE;
    }

    sys_greatbelt_get_gchip_id(lchip,&gchip);
    if (port_cnt >= p_port_node->limit_num)
    {
        sys_greatbelt_mac_security_set_port_mac_limit(lchip, port_id, p_port_node->limit_action);
        p_port_node->running_count = p_port_node->limit_num;
        is_max = (port_cnt > p_port_node->limit_num) ? TRUE:FALSE;
    }
    else
    {
        if (is_update)
        {
            action = 0;
            sys_greatbelt_mac_security_get_port_mac_limit(lchip, port_id, &action);
            if (action != CTC_MACLIMIT_ACTION_NONE)
            {
                sys_greatbelt_mac_security_set_port_mac_limit(lchip, port_id, CTC_MACLIMIT_ACTION_NONE);
            }
        }
        p_port_node->running_count = port_cnt;
        is_max = FALSE;
    }
    return is_max;

}

STATIC bool
_sys_greatbelt_mac_security_vlan_limit_handler(uint8 lchip, uint16 vlan_id, uint32 vlan_cnt, uint8 is_update)
{
    uint32 action = 0;
    bool   is_max = FALSE;
    sys_security_vlan_learn_limit_t* p_vlan_node = NULL;
    if (vlan_id < CTC_MIN_VLAN_ID || vlan_id > CTC_MAX_VLAN_ID)
    {
        return FALSE;
    }

    p_vlan_node = ctc_vector_get(gb_security_master[lchip]->vlan_limit, vlan_id);
    if (p_vlan_node == NULL)
    {
        return FALSE;
    }

    if (vlan_cnt >= p_vlan_node->limit_num)
    {
        sys_greatbelt_mac_security_set_vlan_mac_limit(lchip, vlan_id, p_vlan_node->limit_action);
        p_vlan_node->running_count = p_vlan_node->limit_num;
        is_max = (vlan_cnt > p_vlan_node->limit_num) ? TRUE:FALSE;
    }
    else
    {
        if (is_update)
        {
            action = 0;
            sys_greatbelt_mac_security_get_vlan_mac_limit(lchip, vlan_id, &action);
            if (action != CTC_MACLIMIT_ACTION_NONE)
            {
                sys_greatbelt_mac_security_set_vlan_mac_limit(lchip, vlan_id, CTC_MACLIMIT_ACTION_NONE);
            }
        }
        p_vlan_node->running_count = vlan_cnt;
        is_max = FALSE;
    }
    return is_max;

}

STATIC bool
_sys_greatbelt_mac_security_system_limit_handler(uint8 lchip, uint32 system_cnt, uint8 is_update)
{
    uint32 action = 0;
    bool   is_max = FALSE;

    if (gb_security_master[lchip]->system_limit.limit_en)
    {

        if (system_cnt >= gb_security_master[lchip]->system_limit.limit_num)
        {
            sys_greatbelt_mac_security_set_system_mac_limit(lchip, gb_security_master[lchip]->system_limit.limit_action);
            gb_security_master[lchip]->system_limit.running_count = gb_security_master[lchip]->system_limit.limit_num;
            is_max = (system_cnt > gb_security_master[lchip]->system_limit.limit_num)? TRUE:FALSE;
        }
        else
        {
            if (is_update)
            {
                action = 0;
                sys_greatbelt_mac_security_get_system_mac_limit(lchip, &action);
                if (action != CTC_MACLIMIT_ACTION_NONE)
                {
                    sys_greatbelt_mac_security_set_system_mac_limit(lchip, CTC_MACLIMIT_ACTION_NONE);
                }
            }
            gb_security_master[lchip]->system_limit.running_count = system_cnt;
            is_max = FALSE;
        }
    }
    return is_max;
}

bool
sys_greatbelt_mac_security_learn_limit_handler(uint8 lchip, uint32* port_cnt, uint32* vlan_cnt, uint32 system_cnt,
                                                        ctc_l2_addr_t* l2_addr, uint8 learn_limit_remove_fdb_en, uint8 is_update)
{
    bool is_port_limit  = FALSE;
    bool is_vlan_limit  = FALSE;
    bool is_system_limit  = FALSE;
    bool is_max = FALSE;
    uint16  gport = 0;
    uint16  index = 0;
    uint8   gchip = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (gb_security_master[lchip] == NULL)
    {
        return FALSE;
    }

    sys_greatbelt_get_gchip_id(lchip, &gchip);
    if (is_update)
    {
        for (index = 0; index < SYS_GB_MAX_PORT_NUM_PER_CHIP; index++)
        {
            gport = CTC_MAP_LPORT_TO_GPORT(gchip, index);
            _sys_greatbelt_mac_security_port_limit_handler(lchip, gport, port_cnt[gchip * SYS_GB_MAX_PORT_NUM_PER_CHIP + index], TRUE);
        }

        for (index = CTC_MIN_VLAN_ID; index <= CTC_MAX_VLAN_ID; index++)
        {
            _sys_greatbelt_mac_security_vlan_limit_handler(lchip, index, vlan_cnt[index], TRUE);
        }
        _sys_greatbelt_mac_security_system_limit_handler(lchip, system_cnt, TRUE);
    }
    else
    {
        is_port_limit = (gchip == CTC_MAP_GPORT_TO_GCHIP(l2_addr->gport)) ?
                        _sys_greatbelt_mac_security_port_limit_handler(lchip, l2_addr->gport, *port_cnt, FALSE) : FALSE;
        is_vlan_limit = _sys_greatbelt_mac_security_vlan_limit_handler(lchip, l2_addr->fid, *vlan_cnt, FALSE);
        is_system_limit = _sys_greatbelt_mac_security_system_limit_handler(lchip, system_cnt, FALSE);

        if ((is_port_limit || is_vlan_limit || is_system_limit) && learn_limit_remove_fdb_en)
        {
            /*over the threshold, if remove FDB or not*/
            for (index = 0; index < SYS_GB_MAX_LOCAL_CHIP_NUM; index++)
            {
                sys_greatbelt_l2_remove_fdb(index, l2_addr);
            }
            is_max = TRUE;

        }
    }
    return is_max;
}


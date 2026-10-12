/**
 @file sys_goldengate_security.c

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
#include "sys_goldengate_opf.h"
#include "ctc_linklist.h"
#include "ctc_spool.h"
#include "ctc_hash.h"

#include "sys_goldengate_security.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_vlan.h"
#include "sys_goldengate_scl.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"

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

enum sys_stmctl_action_e
{
    SYS_STMCTL_ACTION_ADD,
    SYS_STMCTL_ACTION_DEL,
    SYS_STMCTL_ACTION_GET,
    SYS_STMCTL_ACTION_NUM
};
typedef enum sys_stmctl_action_e sys_stmctl_action_t;

#define SYS_SECURITY_PORT_ISOLATION_MAX_GROUP_ID    31
#define SYS_SECURITY_PORT_ISOLATION_MIN_GROUP_ID    0

#define SYS_SECURITY_STMCTL_DEF_UPDATE_EN           1
#define SYS_SECURITY_STMCTL_DEF_MAX_UPDT_PORT_NUM   2559
#define SYS_SECURITY_STMCTL_DEF_MAX_UPDT_VLAN_NUM   639
#define SYS_SECURITY_STMCTL_MAX_PORT_NUM            0x3FFF
#define SYS_SECURITY_STMCTL_UNIT                    0  /* shift left 1 bit */

#define SYS_SECURITY_STMCTL_ENTRY_MAX_NUM           3000                /* bigger than portStormCtlMaxUpdatePortNum and vlanStormCtlMaxUpdatePortNum */
/* note:freq 600M < 2^32 - 1 */
#define SYS_SECURITY_STMCTL_GET_UPDATE(freq, max_entry_num)        (((freq*1000000)/(gg_security_master[lchip]->stmctl_granularity))/max_entry_num)

#define SYS_SECURITY_STMCTL_MAX_PORT_VLAN_NUM       128
#define SYS_SECURITY_STMCTL_DISABLE_THRD            0xFFFFFF
#define SYS_SECUIRTY_STMCTL_MAX_THRD                0xFFFFFF
#define SYS_SECUIRTY_STMCTL_MAX_KTHRD               25000000

#define SYS_LEARN_LIMIT_BLOCK_SIZE  4

#define SYS_STMCTL_CHK_UCAST(mode, type) \
    { \
        if (mode) \
        { \
            if (CTC_SECURITY_STORM_CTL_ALL_UCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_STMCTL_INVALID_UC_MODE; \
            } \
        } \
        else \
        { \
            if (CTC_SECURITY_STORM_CTL_KNOWN_UCAST == type \
                || CTC_SECURITY_STORM_CTL_UNKNOWN_UCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_STMCTL_INVALID_UC_MODE; \
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
                return CTC_E_STMCTL_INVALID_MC_MODE; \
            } \
        } \
        else \
        { \
            if (CTC_SECURITY_STORM_CTL_KNOWN_MCAST == type \
                || CTC_SECURITY_STORM_CTL_UNKNOWN_MCAST == type) \
            { \
                SYS_SECURITY_UNLOCK(lchip); \
                return CTC_E_STMCTL_INVALID_MC_MODE; \
            } \
        } \
    }

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
enum sys_goldengate_security_ipsg_flag_e
{
    SYS_SECURITY_IPSG_MAC_SUPPORT = 0x01,
    SYS_SECURITY_IPSG_IPV4_SUPPORT = 0x02,
    SYS_SECURITY_IPSG_IPV6_SUPPORT = 0x04,
    SYS_SECURITY_IPSG_MAX
};
typedef enum sys_goldengate_security_ipsg_flag_e sys_goldengate_security_ipsg_flag_t;

enum sys_goldengate_security_stmctl_mode_e
{
    SYS_GOLDENGATE_SECURITY_MODE_MERGE,
    SYS_GOLDENGATE_SECURITY_MODE_SEPERATE,
    SYS_GOLDENGATE_SECURITY_MODE_NUM,
};
typedef enum sys_goldengate_security_stmctl_mode_e sys_goldengate_security_stmctl_mode_t;

struct sys_stmctl_para_s
{
    uint16 vid_or_lport;
    uint16 stmctl_offset;
    uint8  stmctl_en;
    uint8  pkt_type;
};
typedef struct sys_stmctl_para_s sys_stmctl_para_t;

struct sys_learn_limit_trhold_node_s
{
    uint32 value;
    uint32 cnt;

    uint32  index;
};
typedef struct sys_learn_limit_trhold_node_s sys_learn_limit_trhold_node_t;

struct sys_security_master_s
{
    ctc_security_stmctl_glb_cfg_t stmctl_cfg;
    uint8 flag;
    uint8 ip_src_guard_def;    /* add default entry when add first entry */
    uint8 rsv[2];
    uint32 stmctl_granularity; /* (100/1000/10000) from ctc_security_storm_ctl_granularity_t */
    ctc_spool_t * trhold_spool;
    uint32 port_unit_mode[SYS_SECURITY_PORT_UNIT_INDEX_MAX];
    uint32 vlan_unit_mode[SYS_SECURITY_VLAN_UNIT_INDEX_MAX];
    sal_mutex_t *mutex;
};
typedef struct sys_security_master_s sys_security_master_t;

sys_security_master_t* gg_security_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_SECURITY_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == gg_security_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_SECURITY_CREATE_LOCK(lchip)                         \
    do                                                          \
    {                                                           \
        sal_mutex_create(&gg_security_master[lchip]->mutex);    \
        if (NULL == gg_security_master[lchip]->mutex)           \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_SECURITY_LOCK(lchip) \
    sal_mutex_lock(gg_security_master[lchip]->mutex)

#define SYS_SECURITY_UNLOCK(lchip) \
    sal_mutex_unlock(gg_security_master[lchip]->mutex)

#define CTC_ERROR_RETURN_SECURITY_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(gg_security_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

#define SYS_LEARN_LIMIT_NUM_CHECK(LIMIT_NUM)\
    do {    \
            if (((LIMIT_NUM) > SYS_SECURITY_LEARN_LIMIT_MAX ) && ((LIMIT_NUM) != 0xFFFFFFFF)) { \
                return CTC_E_INVALID_PARAM; }\
    } while (0)

#define MIX(a,b,c)                  \
do                                  \
{                                   \
    a -= b; a -= c; a ^= (c>>13);   \
    b -= c; b -= a; b ^= (a<<8);    \
    c -= a; c -= b; c ^= (b>>13);   \
    a -= b; a -= c; a ^= (c>>12);   \
    b -= c; b -= a; b ^= (a<<16);   \
    c -= a; c -= b; c ^= (b>>5);    \
    a -= b; a -= c; a ^= (c>>3);    \
    b -= c; b -= a; b ^= (a<<10);   \
    c -= a; c -= b; c ^= (b>>15);   \
} while(0)

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
int32
sys_goldengate_security_wb_sync(uint8 lchip);

int32
sys_goldengate_security_wb_restore(uint8 lchip);

STATIC uint32
_sys_goldengate_learn_limit_hash_make(sys_learn_limit_trhold_node_t* p_node)
{
    uint32 a, b, c;

    a = b = c = 0xdeadbeef + (((uint32)96) << 2);

    a += p_node->value;
    MIX(a, b, c);

    return (c);
}

STATIC bool
_sys_goldengate_learn_limit_hash_cmp(sys_learn_limit_trhold_node_t* p_bucket, sys_learn_limit_trhold_node_t* p_new)
{
    if (!p_bucket || !p_new)
    {
        return FALSE;
    }

    if (p_bucket->value != p_new->value)
    {
        return FALSE;
    }

    return TRUE;
}

STATIC int32
_sys_goldengate_stmctl_get_pkt_type_stmctl_base(uint8 lchip, ctc_security_storm_ctl_type_t type, uint32* p_stmctl_base)
{
    if (CTC_SECURITY_STORM_CTL_BCAST == type)
    {
        *p_stmctl_base = 0;
    }
    else if (CTC_SECURITY_STORM_CTL_KNOWN_MCAST == type)
    {
        *p_stmctl_base = 1;
    }
    else if ((CTC_SECURITY_STORM_CTL_UNKNOWN_MCAST == type)
            || (CTC_SECURITY_STORM_CTL_ALL_MCAST == type))
    {
        *p_stmctl_base = 2;
    }
    else if (CTC_SECURITY_STORM_CTL_KNOWN_UCAST == type)
    {
        *p_stmctl_base = 3;
    }
    else if ((CTC_SECURITY_STORM_CTL_UNKNOWN_UCAST == type)
            || (CTC_SECURITY_STORM_CTL_ALL_UCAST == type))
    {
        *p_stmctl_base = 4;
    }
    else
    {
        return CTC_E_STMCTL_INVALID_PKT_TYPE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stmctl_alloc_vlan_stmctl_offset(uint8 lchip, uint16 vid, uint32* p_offset)
{
    uint32 stmctl_en = 0;
    int32  ret = CTC_E_NONE;
    sys_goldengate_opf_t opf;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vid, SYS_VLAN_PROP_VLAN_STORM_CTL_EN, &stmctl_en));

    if (stmctl_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vid, SYS_VLAN_PROP_VLAN_STORM_CTL_PTR, p_offset));
    }
    else
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type = OPF_STORM_CTL;
        opf.pool_index = 0;
        ret = sys_goldengate_opf_alloc_offset(lchip, &opf, 1, p_offset);
        if (CTC_E_NONE != ret)
        {
            return ret;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stmctl_free_vlan_stmctl_offset(uint8 lchip, uint32 offset)
{
    uint32 base = 0;
    uint32 pkt_type = 0;
    uint32 value = 0;
    uint32 cmd = 0;
    uint32 stmctl_ptr = 0;
    sys_goldengate_opf_t opf;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOR(DsVlanStormCtl_t, DsVlanStormCtl_threshold_f);
    for (pkt_type = CTC_SECURITY_STORM_CTL_BCAST; pkt_type < CTC_SECURITY_STORM_CTL_MAX; pkt_type++)
    {
        CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_pkt_type_stmctl_base(lchip, pkt_type, &base));
        stmctl_ptr = (base << 7) | (offset & 0x7F);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_ptr, cmd, &value));

        if (SYS_SECURITY_STMCTL_DISABLE_THRD != value)
        {
            break;
        }
    }

    if (CTC_SECURITY_STORM_CTL_MAX == pkt_type)
    {
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_type = OPF_STORM_CTL;
        opf.pool_index = 0;
        sys_goldengate_opf_free_offset(lchip, &opf, 1, offset);

        SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "free vlan stmctl ptr: 0x%X \n", offset);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stmctl_get_vlan_stmctl_offset(uint8 lchip, uint16 vid, uint32* p_offset)
{
    uint32 stmctl_en = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vid, SYS_VLAN_PROP_VLAN_STORM_CTL_EN, &stmctl_en));

    if (stmctl_en)
    {
        CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vid, SYS_VLAN_PROP_VLAN_STORM_CTL_PTR, p_offset));
    }
    else
    {
        *p_offset = CTC_MAX_UINT32_VALUE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_stmctl_action(uint8 lchip, sys_stmctl_para_t* p_stmctl_para,
                              ctc_security_storm_ctl_op_t op, sys_stmctl_action_t action)
{
    uint32 base = 0;
    uint32 offset = 0;
    uint32 value = 0;
    uint32 cmd = 0;

    CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_pkt_type_stmctl_base(lchip, p_stmctl_para->pkt_type, &base));

    CTC_MAX_VALUE_CHECK(op, (CTC_SECURITY_STORM_CTL_OP_MAX - 1));
    CTC_MAX_VALUE_CHECK(action, (SYS_STMCTL_ACTION_NUM - 1));

    if (CTC_SECURITY_STORM_CTL_OP_PORT == op)
    {
        offset = p_stmctl_para->vid_or_lport;
        p_stmctl_para->stmctl_offset = (base << 8) | (offset & 0xFF);
        p_stmctl_para->stmctl_offset += SYS_MAP_DRV_LPORT_TO_SLICE(p_stmctl_para->vid_or_lport) * 1280;
        if (SYS_STMCTL_ACTION_ADD == action)
        {
            p_stmctl_para->stmctl_en = 1;
        }
        else if (SYS_STMCTL_ACTION_DEL == action)
        {
            p_stmctl_para->stmctl_en = 0;
        }
        else
        {
            cmd = DRV_IOR(DsPortStormCtl_t, DsPortStormCtl_threshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &value));

            p_stmctl_para->stmctl_en = (SYS_SECURITY_STMCTL_DISABLE_THRD == value) ? 0 : 1;
        }
    }
    else
    {
        if (SYS_STMCTL_ACTION_ADD == action)
        {
            CTC_ERROR_RETURN(_sys_goldengate_stmctl_alloc_vlan_stmctl_offset(lchip, p_stmctl_para->vid_or_lport, &offset));
            p_stmctl_para->stmctl_offset = (base << 7) | (offset & 0x7F);
            p_stmctl_para->stmctl_en = 1;
        }
        else if (SYS_STMCTL_ACTION_DEL == action)
        {
            CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_vlan_stmctl_offset(lchip, p_stmctl_para->vid_or_lport, &offset));

            if (CTC_MAX_UINT32_VALUE == offset)
            {
                return CTC_E_ENTRY_NOT_EXIST;
            }

            p_stmctl_para->stmctl_offset = (base << 7) | (offset & 0x7F);

            CTC_ERROR_RETURN(_sys_goldengate_stmctl_free_vlan_stmctl_offset(lchip, offset));

            p_stmctl_para->stmctl_en = 0;
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_vlan_stmctl_offset(lchip, p_stmctl_para->vid_or_lport, &offset));

            if (CTC_MAX_UINT32_VALUE == offset)
            {
                p_stmctl_para->stmctl_en = 0;
            }
            else
            {
                p_stmctl_para->stmctl_offset = (base << 7) | (offset & 0x7F);
                value = SYS_SECURITY_STMCTL_DISABLE_THRD;
                cmd = DRV_IOR(DsVlanStormCtl_t, DsVlanStormCtl_threshold_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &value));

                p_stmctl_para->stmctl_en = (SYS_SECURITY_STMCTL_DISABLE_THRD != value) ? 1 : 0;
            }
        }
    }

    return CTC_E_NONE;
}

/*mac security*/
int32
sys_goldengate_mac_security_set_port_security(uint8 lchip, uint16 gport, bool enable)
{
    uint32 value = enable;

    SYS_SECURITY_INIT_CHECK(lchip);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, value));

    return CTC_E_NONE;
}

/*mac limit mode: 0:Hw, 1:Sw*/
int32
sys_goldengate_mac_limit_set_mode(uint8 lchip, uint8 mode)
{
    uint32 cmd = 0;
    IpeLearningCtl_m ipe_learn_ctl;
    SYS_SECURITY_INIT_CHECK(lchip);

    sal_memset(&ipe_learn_ctl, 0, sizeof(IpeLearningCtl_m));

    cmd = DRV_IOR(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_learn_ctl));

    SetIpeLearningCtl(V, hwPortMacLimitEn_f, &ipe_learn_ctl, (mode)?0:1);
    SetIpeLearningCtl(V, hwVsiMacLimitEn_f, &ipe_learn_ctl, (mode)?0:1);

    cmd = DRV_IOW(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_learn_ctl));

    return CTC_E_NONE;
}

/*mac limit mode: 0:Hw, 1:Sw*/
int32
sys_goldengate_mac_limit_get_mode(uint8 lchip, uint8* p_mode)
{
    uint32 cmd = 0;
    IpeLearningCtl_m ipe_learn_ctl;

    sal_memset(&ipe_learn_ctl, 0, sizeof(IpeLearningCtl_m));

    cmd = DRV_IOR(IpeLearningCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_learn_ctl));

    *p_mode = !(GetIpeLearningCtl(V, hwPortMacLimitEn_f, &ipe_learn_ctl));

    return CTC_E_NONE;
}

int32
sys_goldengate_mac_security_get_port_security(uint8 lchip, uint16 gport, bool* p_enable)
{
    uint32 value = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EN, &value));
    *p_enable = (value) ? 1 : 0;

    return CTC_E_NONE;
}

int32
sys_goldengate_mac_security_set_port_mac_limit(uint8 lchip, uint16 gport, ctc_maclimit_action_t action)
{
    uint8 limit_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:          /*learning enable, forwarding, not discard, learnt*/
        /*Only sw mode have none action */
        if (!limit_mode)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, FALSE));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_FWD:           /*learning disable, forwarding*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
            CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, FALSE));
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, FALSE));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:       /*learning enable, discard, not learnt*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
            CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE));
        }
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:         /*learning enable, discard and to cpu, not learnt*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
        CTC_ERROR_RETURN(sys_goldengate_port_set_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, TRUE));
        }

        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, TRUE));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mac_security_get_port_mac_limit(uint8 lchip, uint16 gport, ctc_maclimit_action_t* action)
{
    uint32 port_learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;
    uint8 limit_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));

    CTC_ERROR_RETURN(sys_goldengate_port_get_property(lchip, gport, CTC_PORT_PROP_LEARNING_EN, &port_learn_en));
    CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, gport, SYS_PORT_PROP_MAC_SECURITY_DISCARD, &mac_security_discard_en));
   CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, gport, SYS_PORT_PROP_SECURITY_EXCP_EN, &security_excp_en));

    if ((port_learn_en == TRUE) && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = (limit_mode)?CTC_MACLIMIT_ACTION_NONE:CTC_MACLIMIT_ACTION_FWD;
    }
    else if ((port_learn_en == FALSE || (!limit_mode)) && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_FWD;
    }
    else if ((port_learn_en == TRUE || (!limit_mode)) && mac_security_discard_en == TRUE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else if ((port_learn_en == TRUE || (!limit_mode)) && mac_security_discard_en == TRUE && security_excp_en == TRUE)
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
sys_goldengate_mac_security_set_vlan_mac_limit(uint8 lchip, uint16 vlan_id, ctc_maclimit_action_t action)
{
    uint8 limit_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:      /*learning eanble, forwarding, not discard, learnt*/
        /*Only sw mode have none action */
        if (!limit_mode)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        CTC_ERROR_RETURN(sys_goldengate_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, FALSE));
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_FWD:       /*learning disable, forwarding*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, FALSE));
        }
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, FALSE));
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:   /*learning enable, discard, not learnt*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE));
        }

        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:     /*learning enable, discard and to cpu, not learnt*/
        /*Only sw mode need set learning enable/disable */
        if (limit_mode)
        {
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, TRUE));
        }

        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, TRUE));
        CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION, TRUE));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;;
}

int32
sys_goldengate_mac_security_get_vlan_mac_limit(uint8 lchip, uint16 vlan_id, ctc_maclimit_action_t* action)
{
    uint32 learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;
    uint8 limit_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_property(lchip, vlan_id, CTC_VLAN_PROP_LEARNING_EN, &learn_en));
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, &mac_security_discard_en));
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION, &security_excp_en));

    if ((learn_en == TRUE )  && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = (limit_mode)?CTC_MACLIMIT_ACTION_NONE:CTC_MACLIMIT_ACTION_FWD;
    }
    else if ((learn_en == FALSE|| !limit_mode) && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_FWD;
    }
    else if (learn_en == TRUE && mac_security_discard_en == TRUE && security_excp_en == FALSE)
    {
        *action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else if ((learn_en == TRUE|| !limit_mode) && mac_security_discard_en == TRUE && security_excp_en == TRUE)
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
_sys_goldengate_set_syetem_security_learn_en(uint8 lchip, uint32 value)
{
    uint32 cmd = 0;

    value = (value) ? 0 : 1;
    cmd = DRV_IOW(IpeLookupCtl_t, IpeLookupCtl_learningDisable_f);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_system_security_learn_en(uint8 lchip, uint32* value)
{
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);
    cmd = DRV_IOR(IpeLookupCtl_t, IpeLookupCtl_learningDisable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    *value = (*value) ? 0 : 1;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_set_syetem_security_excep_en(uint8 lchip, uint32 value)
{

    uint32 cmd = 0;

    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_systemSecurityExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_system_security_excep_en(uint8 lchip, uint32* value)
{

    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_systemSecurityExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_set_system_mac_security_discard_en(uint8 lchip, uint32 value, uint8 limit_mode)
{

    uint32 cmd = 0;

    /*Sw mode*/
    if (limit_mode)
    {
        cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_systemMacSecurityDiscard_f);
    }
    else
    {
        cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_systemMacSecurityEn_f);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_get_system_mac_security_discard_en(uint8 lchip, uint32* value)
{

    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(value);

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_systemMacSecurityDiscard_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    return CTC_E_NONE;
}

int32
sys_goldengate_mac_security_set_system_mac_limit(uint8 lchip, ctc_maclimit_action_t action)
{
    int32 ret = CTC_E_NONE;
    uint8 sec_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &sec_mode));

    switch (action)
    {
    case CTC_MACLIMIT_ACTION_NONE:      /*learning eanble, forwarding, not discard, learnt*/
        /*Only sw mode have none action */
        if (!sec_mode)
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_learn_en(lchip, TRUE));
        CTC_ERROR_RETURN(_sys_goldengate_set_system_mac_security_discard_en(lchip, FALSE, sec_mode));
        CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_excep_en(lchip, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_FWD:       /*learning disable, forwarding*/
        if (sec_mode)
        {
            CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_learn_en(lchip, FALSE));
        }

        CTC_ERROR_RETURN(_sys_goldengate_set_system_mac_security_discard_en(lchip, FALSE, sec_mode));
        CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_excep_en(lchip, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_DISCARD:   /*learning enable, discard, not learnt*/
        if (sec_mode)
        {
            CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_learn_en(lchip, TRUE));
        }

        CTC_ERROR_RETURN(_sys_goldengate_set_system_mac_security_discard_en(lchip, TRUE, sec_mode));
        CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_excep_en(lchip, FALSE));
        break;

    case CTC_MACLIMIT_ACTION_TOCPU:     /*learning enable, discard and to cpu, not learnt*/
        if (sec_mode)
        {
            CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_learn_en(lchip, TRUE));
        }

        CTC_ERROR_RETURN(_sys_goldengate_set_system_mac_security_discard_en(lchip, TRUE, sec_mode));
        CTC_ERROR_RETURN(_sys_goldengate_set_syetem_security_excep_en(lchip, TRUE));
        break;

    default:
        return CTC_E_INVALID_PARAM;

    }

    return ret;

}

int32
sys_goldengate_mac_security_get_system_mac_limit(uint8 lchip, ctc_maclimit_action_t* action)
{
    uint32 learn_en;
    uint32 mac_security_discard_en;
    uint32 security_excp_en;
    int32 ret = CTC_E_NONE;
    uint8 limit_mode = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_PTR_VALID_CHECK(action);
    CTC_ERROR_RETURN(_sys_goldengate_get_system_security_learn_en(lchip, &learn_en));
    CTC_ERROR_RETURN(_sys_goldengate_get_system_mac_security_discard_en(lchip, &mac_security_discard_en));
    CTC_ERROR_RETURN(_sys_goldengate_get_system_security_excep_en(lchip, &security_excp_en));

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));

    if (learn_en == TRUE && mac_security_discard_en == FALSE && security_excp_en == FALSE)
    {
        *action = (limit_mode)?CTC_MACLIMIT_ACTION_NONE:CTC_MACLIMIT_ACTION_FWD;
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
_sys_goldengate_mac_security_add_learn_limit_threshold(uint8 lchip, uint32 limit, uint32 * threshold)
{
    int32 ret = 0;
    sys_learn_limit_trhold_node_t * p_new = NULL;
    sys_learn_limit_trhold_node_t * p_get = NULL;
    uint32 offset;
    sys_goldengate_opf_t opf;
    uint32 cmd = 0;


    if (0xFFFFFFFF == limit)
    {
        * threshold = 0;
        return CTC_E_NONE;
    }
    p_new = mem_malloc(MEM_STMCTL_MODULE, sizeof(sys_learn_limit_trhold_node_t));
    if (NULL == p_new)
    {
        return CTC_E_NO_MEMORY;
    }

    p_new->value = limit;
    SYS_SECURITY_LOCK(lchip);
    ret = ctc_spool_add(gg_security_master[lchip]->trhold_spool, p_new, NULL,&p_get);

    if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
    {
        mem_free(p_new);
    }

    if (ret < 0)
    {
        SYS_SECURITY_UNLOCK(lchip);
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        return ret;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
            opf.pool_type = OPF_SECURITY_LEARN_LIMIT_THRHOLD;
            opf.pool_index = 0;
            ret = sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &offset);
            if (ret)
            {
                ctc_spool_remove(gg_security_master[lchip]->trhold_spool, p_get, NULL);
                SYS_SECURITY_UNLOCK(lchip);
                mem_free(p_new);
                return ret;
            }

            p_get->index = offset;
            cmd = DRV_IOW(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
            ret = DRV_IOCTL(lchip, offset, cmd, &limit);
        }

        *threshold = p_get->index;
    }
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_remove_learn_limit_threshold(uint8 lchip, uint32 limit)
{
    int32 ret = 0;
    sys_learn_limit_trhold_node_t node;
    sys_learn_limit_trhold_node_t * p_get = NULL;
    uint32 offset;
    sys_goldengate_opf_t opf;
    uint32 cmd = 0;

    uint32 max_limit = SYS_SECURITY_LEARN_LIMIT_MAX;

    sal_memset(&node,0,sizeof(sys_learn_limit_trhold_node_t));
    node.value = limit;

    SYS_SECURITY_LOCK(lchip);
    p_get = ctc_spool_lookup(gg_security_master[lchip]->trhold_spool, &node);
    if (!p_get)
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    ret = ctc_spool_remove(gg_security_master[lchip]->trhold_spool, &node, NULL);
    if (ret < 0)
    {
        SYS_SECURITY_UNLOCK(lchip);
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else
    {
        if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
        {
            /* free ad offset */
            offset = p_get->index;
            sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
            opf.pool_type = OPF_SECURITY_LEARN_LIMIT_THRHOLD;
            opf.pool_index = 0;
            CTC_ERROR_RETURN_SECURITY_UNLOCK(sys_goldengate_opf_free_offset(lchip, &opf, 1, offset));
            /* free memory */
            mem_free(p_get);
            cmd = DRV_IOW(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
            CTC_ERROR_RETURN_SECURITY_UNLOCK(DRV_IOCTL(lchip, offset, cmd, &max_limit));
        }
    }
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_set_port_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 index = 0;
    uint16 lport = 0;
    uint32 thrhld_index = 0;
    uint32 limit = 0;
    uint16 gport = 0;
    ctc_security_learn_limit_t runing_limit;
    uint32 runing_cnt = 0;

    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));

    if (!CTC_IS_LINKAGG_PORT(p_learning_limit->gport))
    {
        SYS_MAP_GCHIP_TO_LCHIP(CTC_MAP_GPORT_TO_GCHIP(p_learning_limit->gport), lchip);
        CTC_ERROR_RETURN(sys_goldengate_port_get_global_port(lchip, CTC_MAP_GPORT_TO_LPORT(p_learning_limit->gport), &gport));
    }
    else
    {
        gport = p_learning_limit->gport;
    }

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        index =  gport & 0x3F;
    }
    else
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_learning_limit->gport, lchip, lport);
        index = lport + 64;
    }

    cmd = DRV_IOR(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));

    /**max num 0xFFFFFFFF means disable*/
    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        thrhld_index = 0;
    }

    if (0 != thrhld_index)
    {
        cmd = DRV_IOR(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, thrhld_index, cmd, &limit));
    }
    else
    {
        limit = SYS_SECURITY_LEARN_LIMIT_MAX;
    }

    if (limit == p_learning_limit->limit_num)
    {
        return CTC_E_NONE;
    }

    if (SYS_SECURITY_LEARN_LIMIT_MAX != limit)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_remove_learn_limit_threshold(lchip, limit));
    }

    CTC_ERROR_RETURN(_sys_goldengate_mac_security_add_learn_limit_threshold(lchip, p_learning_limit->limit_num,
            &thrhld_index));

    cmd = DRV_IOW(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));

    /* clear discard bit */
    if (limit < p_learning_limit->limit_num)
    {
        in.mac_limit.security_status        = 0;
        in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
        in.mac_limit.mac_security_update_en = 1;
        in.mac_limit.mac_securiy_index      = index >> 5;
        in.mac_limit.mac_securiy_array_index= index & 0x1F;
        in.mac_limit.mac_limit_index        = index;
        in.mac_limit.mac_limit_update_en        = 0;
        in.mac_limit.mac_security_update_en        = 1;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
    }
    else
    {
        /* get running count */
        sal_memset(&runing_limit, 0, sizeof(ctc_security_learn_limit_t));
        runing_limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_PORT;
        runing_limit.gport = p_learning_limit->gport;
        CTC_ERROR_RETURN(sys_goldengate_mac_security_get_learn_limit(lchip, &runing_limit, &runing_cnt));

        if (runing_cnt >= p_learning_limit->limit_num)
        {
            in.mac_limit.security_status        = 1;
            in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
            in.mac_limit.mac_security_update_en = 1;
            in.mac_limit.mac_securiy_index      = index >> 5;
            in.mac_limit.mac_securiy_array_index= index & 0x1F;
            in.mac_limit.mac_limit_index        = index;
            in.mac_limit.mac_limit_update_en        = 0;
            in.mac_limit.mac_security_update_en        = 1;
            drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
        }
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_get_port_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 index = 0;
    uint16 lport = 0;
    uint32 thrhld_index = 0;
    uint32 limit = 0;
    uint32 discard_en = 0;
    uint32 except_en = 0;
    uint16 gport = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));

    if (!CTC_IS_LINKAGG_PORT(p_learning_limit->gport))
    {
        SYS_MAP_GCHIP_TO_LCHIP(CTC_MAP_GPORT_TO_GCHIP(p_learning_limit->gport), lchip);
        CTC_ERROR_RETURN(sys_goldengate_port_get_global_port(lchip, CTC_MAP_GPORT_TO_LPORT(p_learning_limit->gport), &gport));
    }
    else
    {
        gport = p_learning_limit->gport;
    }

    if (CTC_IS_LINKAGG_PORT(gport))
    {
        index =  gport & 0x3F;
    }
    else
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_learning_limit->gport, lchip, lport);
        index = lport + 64;
    }

    in.mac_limit.type                       = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
    in.mac_limit.mac_securiy_index          = index >> 5;
    in.mac_limit.mac_securiy_array_index    = index & 0x1F;
    in.mac_limit.mac_limit_index            = index;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_MAC_LIMIT, &in, &out);

    p_learning_limit->limit_count = out.mac_limit.counter;

    cmd = DRV_IOR(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));

    cmd = DRV_IOR(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, thrhld_index, cmd, &limit));
    p_learning_limit->limit_num = limit;

    if (!CTC_IS_LINKAGG_PORT(p_learning_limit->gport))
    {
        CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, p_learning_limit->gport,
                         SYS_PORT_PROP_MAC_SECURITY_DISCARD, &discard_en));
        CTC_ERROR_RETURN(sys_goldengate_port_get_internal_property(lchip, p_learning_limit->gport,
                         SYS_PORT_PROP_SECURITY_EXCP_EN, &except_en));
    }

    if (discard_en && except_en)
    {
        p_learning_limit->limit_action= CTC_MACLIMIT_ACTION_TOCPU;
    }
    else if (discard_en)
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_FWD;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_set_vlan_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 index = 0;
    uint32 thrhld_index = 0;
    uint32 limit = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;
    ctc_security_learn_limit_t runing_limit;
    uint32 runing_cnt = 0;

    index = 64 + 512 + (p_learning_limit->vlan & 0xFFF);

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));

    cmd = DRV_IOR(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));
    /**max num 0xFFFFFFFF means disable*/
    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        thrhld_index = 0;
    }

    if (0 != thrhld_index)
    {
        cmd = DRV_IOR(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, thrhld_index, cmd, &limit));
    }
    else
    {
        limit = SYS_SECURITY_LEARN_LIMIT_MAX;
    }

    if (limit == p_learning_limit->limit_num)
    {
        return CTC_E_NONE;
    }

    if (SYS_SECURITY_LEARN_LIMIT_MAX != limit)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_remove_learn_limit_threshold(lchip, limit));
    }

    CTC_ERROR_RETURN(_sys_goldengate_mac_security_add_learn_limit_threshold(lchip, p_learning_limit->limit_num,
            &thrhld_index));

    cmd = DRV_IOW(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));

    /* clear discard bit */
    if (limit < p_learning_limit->limit_num)
    {
        in.mac_limit.security_status        = 0;
        in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
        in.mac_limit.mac_security_update_en = 1;
        in.mac_limit.mac_securiy_index      = index >> 5;
        in.mac_limit.mac_securiy_array_index= index & 0x1F;
        in.mac_limit.mac_limit_index        = index;
        in.mac_limit.mac_limit_update_en        = 0;
        in.mac_limit.mac_security_update_en        = 1;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
    }
    else
    {
        /* get running count */
        sal_memset(&runing_limit, 0, sizeof(ctc_security_learn_limit_t));
        runing_limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN;
        runing_limit.vlan = p_learning_limit->vlan;
        CTC_ERROR_RETURN(sys_goldengate_mac_security_get_learn_limit(lchip, &runing_limit, &runing_cnt));

        if (runing_cnt >= p_learning_limit->limit_num)
        {
            in.mac_limit.security_status        = 1;
            in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
            in.mac_limit.mac_security_update_en = 1;
            in.mac_limit.mac_securiy_index      = index >> 5;
            in.mac_limit.mac_securiy_array_index= index & 0x1F;
            in.mac_limit.mac_limit_index        = index;
            in.mac_limit.mac_limit_update_en        = 0;
            in.mac_limit.mac_security_update_en        = 1;
            drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
        }
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_get_vlan_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 index = 0;
    uint32 thrhld_index = 0;
    uint32 limit = 0;
    uint32 discard_en = 0;
    uint32 except_en = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    index = 64 + 512 + (p_learning_limit->vlan & 0xFFF);

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));


    in.mac_limit.type                       = DRV_FIB_ACC_UPDATE_PORT_VLAN_MAC_LIMIT;
    in.mac_limit.mac_securiy_index          = index >> 5;
    in.mac_limit.mac_securiy_array_index    = index & 0x1F;
    in.mac_limit.mac_limit_index            = index;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_MAC_LIMIT, &in, &out);

    p_learning_limit->limit_count= out.mac_limit.counter;

    cmd = DRV_IOR(DsMacLimitThreshold_t, DsMacLimitThreshold_thresholdIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &thrhld_index));

    cmd = DRV_IOR(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, thrhld_index, cmd, &limit));
    p_learning_limit->limit_num = limit;

    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, p_learning_limit->vlan,
        SYS_VLAN_PROP_MAC_SECURITY_VLAN_DISCARD, &discard_en));
    CTC_ERROR_RETURN(sys_goldengate_vlan_get_internal_property(lchip, p_learning_limit->vlan,
        SYS_VLAN_PROP_MAC_SECURITY_VLAN_EXCEPTION,&except_en));

    if (discard_en && except_en)
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_TOCPU;
    }
    else if (discard_en)
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_FWD;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_set_system_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 value = 0;
    uint32 limit = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;
    ctc_security_learn_limit_t runing_limit;
    uint32 runing_cnt = 0;

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));

    cmd = DRV_IOR(MacLimitSystem_t, MacLimitSystem_dynamicMaxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &limit));


    if (p_learning_limit->limit_num == 0xFFFFFFFF)
    {
        value = SYS_SECURITY_LEARN_LIMIT_MAX;
    }
    else
    {
        value = p_learning_limit->limit_num;
    }
    cmd = DRV_IOW(MacLimitSystem_t, MacLimitSystem_dynamicMaxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /* clear discard bit */
    if ( limit < value )
    {
        in.mac_limit.security_status        = 0;
        in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT;
        in.mac_limit.mac_security_update_en = 1;
        in.mac_limit.mac_limit_update_en = 0;
        drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
    }
    else
    {
        /* get running count */
        sal_memset(&runing_limit, 0, sizeof(ctc_security_learn_limit_t));
        runing_limit.limit_type = CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM;
        CTC_ERROR_RETURN(sys_goldengate_mac_security_get_learn_limit(lchip, &runing_limit, &runing_cnt));

        if (runing_cnt >= value)
        {
            in.mac_limit.security_status        = 1;
            in.mac_limit.type                   = DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT;
            in.mac_limit.mac_security_update_en = 1;
            in.mac_limit.mac_limit_update_en = 0;
            drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_UPDATE_MAC_LIMIT, &in, &out);
        }
    }
    return CTC_E_NONE;
}

int32
_sys_goldengate_mac_security_get_system_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{

    uint32 cmd = 0;
    uint32 discard_en = 0;
    uint32 except_en = 0;
    uint32 value = 0;
    drv_fib_acc_in_t  in;
    drv_fib_acc_out_t out;

    sal_memset(&in,0,sizeof(drv_fib_acc_in_t));
    sal_memset(&out,0,sizeof(drv_fib_acc_out_t));


    in.mac_limit.type = DRV_FIB_ACC_UPDATE_DYNAMIC_MAC_LIMIT;
    drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_MAC_LIMIT, &in, &out);

    p_learning_limit->limit_count = out.mac_limit.counter;

    cmd = DRV_IOR(MacLimitSystem_t, MacLimitSystem_dynamicMaxCount_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_learning_limit->limit_num= value;

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_systemMacSecurityEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &discard_en));

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_systemSecurityExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &except_en));

    if (discard_en && except_en)
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_TOCPU;
    }
    else if (discard_en)
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_DISCARD;
    }
    else
    {
        p_learning_limit->limit_action = CTC_MACLIMIT_ACTION_FWD;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mac_security_set_learn_limit_num(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    uint8 limit_mode = 0;

    CTC_ERROR_RETURN(sys_goldengate_mac_limit_get_mode(lchip, &limit_mode));
    if (limit_mode)
    {
        /*Software do count, donot set limit num*/
        return CTC_E_NONE;
    }

    if (CTC_SECURITY_LEARN_LIMIT_TYPE_PORT == p_learning_limit->limit_type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_set_port_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_set_vlan_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_set_system_learn_limit(lchip, p_learning_limit));
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_mac_security_set_learn_limit_action(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    if (CTC_SECURITY_LEARN_LIMIT_TYPE_PORT == p_learning_limit->limit_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_mac_security_set_port_mac_limit(lchip, p_learning_limit->gport, p_learning_limit->limit_action));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN == p_learning_limit->limit_type )
    {
        CTC_VLAN_RANGE_CHECK(p_learning_limit->vlan);

        CTC_ERROR_RETURN(sys_goldengate_mac_security_set_vlan_mac_limit(lchip, p_learning_limit->vlan, p_learning_limit->limit_action));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(sys_goldengate_mac_security_set_system_mac_limit(lchip, p_learning_limit->limit_action));
    }
    else
    {
        return CTC_E_INTR_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_mac_security_set_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit)
{
    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_learning_limit);
    SYS_LEARN_LIMIT_NUM_CHECK(p_learning_limit->limit_num);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(_sys_goldengate_mac_security_set_learn_limit_action(lchip, p_learning_limit));

    CTC_ERROR_RETURN(_sys_goldengate_mac_security_set_learn_limit_num(lchip, p_learning_limit));

    return CTC_E_NONE;

}

int32
sys_goldengate_mac_security_get_learn_limit(uint8 lchip, ctc_security_learn_limit_t* p_learning_limit, uint32* p_running_num)
{

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_learning_limit);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_SECURITY_LEARN_LIMIT_TYPE_PORT == p_learning_limit->limit_type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_get_port_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_VLAN == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_get_vlan_learn_limit(lchip, p_learning_limit));
    }
    else if (CTC_SECURITY_LEARN_LIMIT_TYPE_SYSTEM == p_learning_limit->limit_type )
    {
        CTC_ERROR_RETURN(_sys_goldengate_mac_security_get_system_learn_limit(lchip, p_learning_limit));
    }
    else
    {
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

    return CTC_E_NONE;

}

STATIC int32
_sys_goldengate_ip_source_guard_init_default_entry(uint8 lchip)
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

    /* default entry in tcam0 and tcam1. */
    def_entry.priority_valid = 1;
    def_entry.priority       = 0;
    def_entry.key.type = SYS_SCL_KEY_TCAM_MAC;
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId0MacKey160_t, &mac_table_size));
    if (mac_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_MAC_SUPPORT;
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId1MacKey160_t, &mac_table_size));
    if (mac_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_MAC_SUPPORT;
    }

    def_entry.key.type = SYS_SCL_KEY_TCAM_IPV4_SINGLE;
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId0L3Key160_t, &ipv4_table_size));
    if (ipv4_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV4_SUPPORT;
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId1L3Key160_t, &ipv4_table_size));
    if (ipv4_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV4_SUPPORT;
    }

    def_entry.key.type = SYS_SCL_KEY_TCAM_IPV6;
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId0Ipv6Key640_t, &ipv6_table_size));
    if (ipv6_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV6_SUPPORT;
    }
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId1Ipv6Key640_t, &ipv6_table_size));
    if (ipv6_table_size != 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1, &def_entry, 1));
        gg_security_master[lchip]->flag = gg_security_master[lchip]->flag | SYS_SECURITY_IPSG_IPV6_SUPPORT;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD, NULL, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1, NULL, 1));

    return CTC_E_NONE;

}

/*ip source guard*/
int32
sys_goldengate_ip_source_guard_add_entry(uint8 lchip, ctc_security_ip_source_guard_elem_t* p_elem)
{
    sys_scl_entry_t scl_entry;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_elem);
    CTC_GLOBAL_PORT_CHECK(p_elem->gport);

    SYS_SECURITY_LOCK(lchip);
    if(0 == gg_security_master[lchip]->ip_src_guard_def)
    {
        CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_goldengate_ip_source_guard_init_default_entry(lchip));
        gg_security_master[lchip]->ip_src_guard_def = 1;
    }
    SYS_SECURITY_UNLOCK(lchip);

    sal_memset(&scl_entry, 0, sizeof(scl_entry));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:0x%04X\n", p_elem->gport);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ip source guard type:%d\n", p_elem->ip_src_guard_type);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4 or ipv6:%s\n", (CTC_IP_VER_4 == p_elem->ipv4_or_ipv6) ? "ipv4" : "ipv6");

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4sa:%d.%d.%d.%d\n", (p_elem->ipv4_sa >> 24) & 0xFF, (p_elem->ipv4_sa >> 16) & 0xFF, \
                         (p_elem->ipv4_sa >> 8) & 0xFF, p_elem->ipv4_sa & 0xFF);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv6sa:%x%x%x%x\n", p_elem->ipv6_sa[0], p_elem->ipv6_sa[1], \
                         p_elem->ipv6_sa[2], p_elem->ipv6_sa[3]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac:%x:%x:%x:%x:%x:%x\n", p_elem->mac_sa[0], p_elem->mac_sa[1], p_elem->mac_sa[2], \
                         p_elem->mac_sa[3], p_elem->mac_sa[4], p_elem->mac_sa[5]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan:%d\n", p_elem->vid);
    if (p_elem->ip_src_guard_type == CTC_SECURITY_BINDING_TYPE_IPV6_MAC)
    {
        if (!(CTC_FLAG_ISSET(gg_security_master[lchip]->flag, SYS_SECURITY_IPSG_IPV6_SUPPORT)))
        {
            return CTC_E_FEATURE_NOT_SUPPORT;
        }

        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV6;
        scl_entry.action.type = SYS_SCL_ACTION_INGRESS;
        scl_entry.key.u.hash_port_ipv6_key.gport = p_elem->gport;
        sal_memcpy(scl_entry.key.u.hash_port_ipv6_key.ip_sa, p_elem->ipv6_sa, sizeof(ipv6_addr_t));
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        scl_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_MACSA;
        sal_memcpy(scl_entry.action.u.igs_action.bind.mac_sa , p_elem->mac_sa, sizeof(mac_addr_t));

        CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD, &scl_entry, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1));
        return CTC_E_NONE;
    }

    if (CTC_IP_VER_4 != p_elem->ipv4_or_ipv6)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (((p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_MAC)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_VLAN))) \
        && (!(CTC_FLAG_ISSET(gg_security_master[lchip]->flag, SYS_SECURITY_IPSG_IPV4_SUPPORT))))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    if (((p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_MAC)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN)) \
        ||(p_elem->ip_src_guard_type == (CTC_SECURITY_BINDING_TYPE_MAC_VLAN))) \
        && (!(CTC_FLAG_ISSET(gg_security_master[lchip]->flag, SYS_SECURITY_IPSG_MAC_SUPPORT))))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }


    scl_entry.action.type = SYS_SCL_ACTION_INGRESS;
    switch (p_elem->ip_src_guard_type)
    {
    case CTC_SECURITY_BINDING_TYPE_IP:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        scl_entry.key.u.hash_port_ipv4_key.gport   = p_elem->gport;
        scl_entry.key.u.hash_port_ipv4_key.ip_sa   = p_elem->ipv4_sa;

        /* do nothing */
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        scl_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        scl_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;

        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        scl_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_MACSA;
        sal_memcpy(scl_entry.action.u.igs_action.bind.mac_sa, p_elem->mac_sa, sizeof(mac_addr_t));

        break;

    case CTC_SECURITY_BINDING_TYPE_IP_VLAN:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        scl_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        scl_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        scl_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_VLAN;
        scl_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(scl_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        scl_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        scl_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        scl_entry.action.u.igs_action.bind.type        = CTC_SCL_BIND_TYPE_IPV4SA_VLAN;
        scl_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        scl_entry.action.u.igs_action.bind.ipv4_sa =  p_elem->ipv4_sa;
        break;

    case CTC_SECURITY_BINDING_TYPE_MAC:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(scl_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        scl_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        scl_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        /* do nothing */
        break;

    case CTC_SECURITY_BINDING_TYPE_MAC_VLAN:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(scl_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        scl_entry.key.u.hash_port_mac_key.gport    = p_elem->gport;
        scl_entry.key.u.hash_port_mac_key.mac_is_da = 0;

        CTC_VLAN_RANGE_CHECK(p_elem->vid);
        CTC_SET_FLAG(scl_entry.action.u.igs_action.flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN);
        scl_entry.action.u.igs_action.bind.type    = CTC_SCL_BIND_TYPE_VLAN;
        scl_entry.action.u.igs_action.bind.vlan_id =  p_elem->vid;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD, &scl_entry, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_install_entry(lchip, scl_entry.entry_id, 1));

    return CTC_E_NONE;
}

int32
sys_goldengate_ip_source_guard_remove_entry(uint8 lchip, ctc_security_ip_source_guard_elem_t* p_elem)
{
    uint16 lport = 0;

    uint8 is_linkagg = 0;
    sys_scl_entry_t scl_entry;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_elem);
    CTC_GLOBAL_PORT_CHECK(p_elem->gport);

    sal_memset(&scl_entry, 0, sizeof(scl_entry));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "gport:0x%04X\n", p_elem->gport);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ip source guard type:%d\n", p_elem->ip_src_guard_type);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4 or ipv6:%d\n", p_elem->ipv4_or_ipv6);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv4sa:%d.%d.%d.%d\n", (p_elem->ipv4_sa >> 24) & 0xFF, (p_elem->ipv4_sa >> 16) & 0xFF, \
                         (p_elem->ipv4_sa >> 8) & 0xFF, p_elem->ipv4_sa & 0xFF);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipv6sa:%x%x%x%x\n", p_elem->ipv6_sa[0], p_elem->ipv6_sa[1], \
                         p_elem->ipv6_sa[2], p_elem->ipv6_sa[3]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac:%x %x %x %x %x %x\n", p_elem->mac_sa[0], p_elem->mac_sa[1], p_elem->mac_sa[2], \
                         p_elem->mac_sa[3], p_elem->mac_sa[4], p_elem->mac_sa[5]);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "vlan:%d\n", p_elem->vid);

    if (p_elem->ip_src_guard_type == CTC_SECURITY_BINDING_TYPE_IPV6_MAC)
    {
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV6;
        sal_memcpy(scl_entry.key.u.hash_ipv6_key.ip_sa, p_elem->ipv6_sa, sizeof(ipv6_addr_t));
        scl_entry.key.u.hash_port_ipv6_key.gport = p_elem->gport;

        CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD));
        CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));

        return CTC_E_NONE;
    }


    is_linkagg = CTC_IS_LINKAGG_PORT(p_elem->gport);
    if (!is_linkagg)   /*not linkagg*/
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_elem->gport, lchip, lport);
    }


    if (CTC_IP_VER_4 != p_elem->ipv4_or_ipv6)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }


    switch (p_elem->ip_src_guard_type)
    {
    case CTC_SECURITY_BINDING_TYPE_IP:
    case CTC_SECURITY_BINDING_TYPE_IP_MAC:
    case CTC_SECURITY_BINDING_TYPE_IP_VLAN:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_IPV4;
        scl_entry.key.u.hash_port_ipv4_key.ip_sa= p_elem->ipv4_sa;
        scl_entry.key.u.hash_port_ipv4_key.gport= p_elem->gport;
        break;

    case CTC_SECURITY_BINDING_TYPE_IP_MAC_VLAN:
    case CTC_SECURITY_BINDING_TYPE_MAC:
    case CTC_SECURITY_BINDING_TYPE_MAC_VLAN:
        scl_entry.key.type = SYS_SCL_KEY_HASH_PORT_MAC;
        sal_memcpy(scl_entry.key.u.hash_port_mac_key.mac, p_elem->mac_sa, sizeof(mac_addr_t));
        scl_entry.key.u.hash_port_mac_key.gport= p_elem->gport;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, &scl_entry, SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD));
    CTC_ERROR_RETURN(sys_goldengate_scl_uninstall_entry(lchip, scl_entry.entry_id, 1));
    CTC_ERROR_RETURN(sys_goldengate_scl_remove_entry(lchip, scl_entry.entry_id, 1));
    return CTC_E_NONE;
}
int32
sys_goldengate_ip_source_guard_init_default_entry(uint8 lchip)
{
    int32 ret = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_INIT_CHECK(lchip);

    SYS_SECURITY_LOCK(lchip);
    if(0 == gg_security_master[lchip]->ip_src_guard_def)
    {
        CTC_ERROR_RETURN_SECURITY_UNLOCK(_sys_goldengate_ip_source_guard_init_default_entry(lchip));
        gg_security_master[lchip]->ip_src_guard_def = 1;
    }

    SYS_SECURITY_UNLOCK(lchip);
    return ret;
}
STATIC int32
_sys_goldengate_storm_ctl_set_mode(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg, sys_stmctl_para_t* p_stmctl_para)
{

    uint16 lport = 0;
    uint32 unit = 0;
    uint32 cmd = 0;
    uint64 temp_threshold = 0;
    uint32 threshold = 0;
    uint32 last_threshold = 0;
    uint32 base = 0;
    uint32 offset = 0;
    DsStormCtl_m       storm_ctl;
    DsStormCount_m     storm_count;

    CTC_PTR_VALID_CHECK(p_stmctl_cfg);
    CTC_PTR_VALID_CHECK(p_stmctl_para);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_stmctl_cfg->gport, lchip, lport);
        p_stmctl_para->vid_or_lport = lport;
    }
    else
    {
        p_stmctl_para->vid_or_lport = p_stmctl_cfg->vlan_id;
    }
    p_stmctl_para->stmctl_en = p_stmctl_cfg->storm_en;
    p_stmctl_para->pkt_type = p_stmctl_cfg->type;

    unit = 1 << SYS_SECURITY_STMCTL_UNIT;

    if ((CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode)
        || (CTC_SECURITY_STORM_CTL_MODE_KBPS == p_stmctl_cfg->mode))
    {
        if (p_stmctl_cfg->threshold > SYS_SECUIRTY_STMCTL_MAX_KTHRD)
        {
            return CTC_E_STMCTL_INVALID_THRESHOLD;
        }
        temp_threshold = (uint64)p_stmctl_cfg->threshold * 1000;
    }
    else
    {
        temp_threshold = (uint64)p_stmctl_cfg->threshold;
    }

    if (p_stmctl_cfg->storm_en)
    {
        SYS_SECURITY_LOCK(lchip);
        threshold = (uint32)(temp_threshold / gg_security_master[lchip]->stmctl_granularity / unit);
        SYS_SECURITY_UNLOCK(lchip);
        if (threshold >= SYS_SECUIRTY_STMCTL_MAX_THRD)
        {
            return CTC_E_STMCTL_INVALID_THRESHOLD;
        }
    }
    else
    {
        threshold = SYS_SECURITY_STMCTL_DISABLE_THRD;
    }

    /* write to asic */
    if (p_stmctl_para->stmctl_en)
    {
        CTC_ERROR_RETURN(_sys_goldengate_stmctl_action(lchip, p_stmctl_para, p_stmctl_cfg->op, SYS_STMCTL_ACTION_ADD));

        sal_memset(&storm_ctl, 0, sizeof(DsStormCtl_m));
        if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
        {
            cmd = DRV_IOR(DsPortStormCtl_t, DsPortStormCtl_threshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &last_threshold));

            if (last_threshold > threshold)/*disable storm ctl to clear running count*/
            {
                last_threshold = 0xFFFFFF;
                cmd = DRV_IOW(DsPortStormCtl_t, DsPortStormCtl_threshold_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &last_threshold));
                sal_task_sleep(10);
            }

            SetDsPortStormCtl(V, exceptionEn_f, &storm_ctl, (p_stmctl_cfg->discarded_to_cpu ? 1 : 0));
            if((CTC_SECURITY_STORM_CTL_MODE_PPS == p_stmctl_cfg->mode)
                || (CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode))
            {
                SetDsPortStormCtl(V, usePacketCount_f, &storm_ctl, 1);
            }
            else
            {
                SetDsPortStormCtl(V, usePacketCount_f, &storm_ctl, 0);
            }
            SetDsPortStormCtl(V, threshold_f, &storm_ctl, threshold);
            SetDsPortStormCount(V, runningCount_f, &storm_count, 0); /* must clear */

            cmd = DRV_IOW(DsPortStormCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &storm_ctl));

            cmd = DRV_IOW(DsPortStormCount_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &storm_count));
            if ((CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode)
            || (CTC_SECURITY_STORM_CTL_MODE_KBPS == p_stmctl_cfg->mode))
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_SET(gg_security_master[lchip]->port_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
            else
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_UNSET(gg_security_master[lchip]->port_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
        }
        else
        {
            cmd = DRV_IOR(DsVlanStormCtl_t, DsVlanStormCtl_threshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &last_threshold));

            if (last_threshold > threshold)/*disable storm ctl to clear running count*/
            {
                last_threshold = 0xFFFFFF;
                cmd = DRV_IOW(DsVlanStormCtl_t, DsVlanStormCtl_threshold_f);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &last_threshold));
                sal_task_sleep(10);
            }

            SetDsVlanStormCtl(V, exceptionEn_f, &storm_ctl, (p_stmctl_cfg->discarded_to_cpu ? 1 : 0));
            if((CTC_SECURITY_STORM_CTL_MODE_PPS == p_stmctl_cfg->mode)
                || (CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode))
            {
                SetDsPortStormCtl(V, usePacketCount_f, &storm_ctl, 1);
            }
            else
            {
                SetDsPortStormCtl(V, usePacketCount_f, &storm_ctl, 0);
            }
            SetDsVlanStormCtl(V, threshold_f, &storm_ctl, threshold);
            SetDsVlanStormCount(V, runningCount_f, &storm_count, 0); /* must clear */

            cmd = DRV_IOW(DsVlanStormCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &storm_ctl));

            cmd = DRV_IOW(DsVlanStormCount_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &storm_count));
            if ((CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode)
                || (CTC_SECURITY_STORM_CTL_MODE_KBPS == p_stmctl_cfg->mode))
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_SET(gg_security_master[lchip]->vlan_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
            else
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_UNSET(gg_security_master[lchip]->vlan_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
        }
    }
    else
    {
        CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_pkt_type_stmctl_base(lchip, p_stmctl_para->pkt_type, &base));

        if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
        {
            offset = p_stmctl_para->vid_or_lport;
            p_stmctl_para->stmctl_offset = (base << 8) | (offset & 0xFF);
            p_stmctl_para->stmctl_offset += SYS_MAP_DRV_LPORT_TO_SLICE(p_stmctl_para->vid_or_lport) * 1280;

            threshold = SYS_SECURITY_STMCTL_DISABLE_THRD;
            cmd = DRV_IOW(DsPortStormCtl_t, DsPortStormCtl_threshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &threshold));
            if ((CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode)
                || (CTC_SECURITY_STORM_CTL_MODE_KBPS == p_stmctl_cfg->mode))
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_UNSET(gg_security_master[lchip]->port_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_stmctl_get_vlan_stmctl_offset(lchip, p_stmctl_para->vid_or_lport, &offset));
            if (CTC_MAX_UINT32_VALUE == offset)
            {
                return CTC_E_ENTRY_NOT_EXIST;
            }
            p_stmctl_para->stmctl_offset = (base << 7) | (offset & 0x7F);

            threshold = SYS_SECURITY_STMCTL_DISABLE_THRD;
            cmd = DRV_IOW(DsVlanStormCtl_t, DsVlanStormCtl_threshold_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_stmctl_para->stmctl_offset, cmd, &threshold));
            if ((CTC_SECURITY_STORM_CTL_MODE_KPPS == p_stmctl_cfg->mode)
                || (CTC_SECURITY_STORM_CTL_MODE_KBPS == p_stmctl_cfg->mode))
            {
                SYS_SECURITY_LOCK(lchip);
                CTC_BIT_UNSET(gg_security_master[lchip]->vlan_unit_mode[p_stmctl_para->stmctl_offset / 32]
                    ,(p_stmctl_para->stmctl_offset % 32));
                SYS_SECURITY_UNLOCK(lchip);
            }
        }
        /*free stmctl_offset should after clear threshold*/
        CTC_ERROR_RETURN(_sys_goldengate_stmctl_action(lchip, p_stmctl_para, p_stmctl_cfg->op, SYS_STMCTL_ACTION_DEL));
    }
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "StormCtlPtr:%d, unit: %d, DstormCtl.threshold: %d\n",
                         p_stmctl_para->stmctl_offset,  unit, threshold);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_storm_ctl_get_mode(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg)
{

    uint16 lport = 0;
    uint32 unit = 0;
    uint32 unit_mode = 0;
    uint32 cmd = 0;
    uint32 stmctl_ptr = 0;
    uint64 threshold = 0;
    sys_stmctl_para_t stmctl_para;
    DsStormCtl_m ctl;

    CTC_PTR_VALID_CHECK(p_stmctl_cfg);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&stmctl_para, 0, sizeof(sys_stmctl_para_t));
    if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_stmctl_cfg->gport, lchip, lport);
        stmctl_para.vid_or_lport = lport;
    }
    else
    {
        stmctl_para.vid_or_lport = p_stmctl_cfg->vlan_id;
    }
    stmctl_para.pkt_type = p_stmctl_cfg->type;

    CTC_ERROR_RETURN(_sys_goldengate_stmctl_action(lchip, &stmctl_para, p_stmctl_cfg->op, SYS_STMCTL_ACTION_GET));

    if (!stmctl_para.stmctl_en)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    unit = 1 << SYS_SECURITY_STMCTL_UNIT;
    p_stmctl_cfg->storm_en = stmctl_para.stmctl_en;

    sal_memset(&ctl, 0, sizeof(DsStormCtl_m));

    if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
    {
        cmd = DRV_IOR(DsPortStormCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_para.stmctl_offset, cmd, &ctl));
        SYS_SECURITY_LOCK(lchip);
        unit_mode = CTC_IS_BIT_SET(gg_security_master[lchip]->port_unit_mode[stmctl_para.stmctl_offset / 32]
            ,(stmctl_para.stmctl_offset % 32));
        SYS_SECURITY_UNLOCK(lchip);
    }
    else
    {
        cmd = DRV_IOR(DsVlanStormCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, stmctl_para.stmctl_offset, cmd, &ctl));
        SYS_SECURITY_LOCK(lchip);
        unit_mode = CTC_IS_BIT_SET(gg_security_master[lchip]->vlan_unit_mode[stmctl_para.stmctl_offset / 32]
            ,(stmctl_para.stmctl_offset % 32));
        SYS_SECURITY_UNLOCK(lchip);
    }

    p_stmctl_cfg->discarded_to_cpu = GetDsStormCtl(V, exceptionEn_f, &ctl);

    threshold = GetDsStormCtl(V, threshold_f, &ctl);
    if (unit_mode)
    {
        p_stmctl_cfg->mode = GetDsStormCtl(V, usePacketCount_f, &ctl) ? CTC_SECURITY_STORM_CTL_MODE_KPPS : CTC_SECURITY_STORM_CTL_MODE_KBPS;
        SYS_SECURITY_LOCK(lchip);
        p_stmctl_cfg->threshold = (uint32)(unit * threshold * gg_security_master[lchip]->stmctl_granularity / 1000);
        SYS_SECURITY_UNLOCK(lchip);
    }
    else
    {
        p_stmctl_cfg->mode = GetDsStormCtl(V, usePacketCount_f, &ctl) ? CTC_SECURITY_STORM_CTL_MODE_PPS : CTC_SECURITY_STORM_CTL_MODE_BPS;
        SYS_SECURITY_LOCK(lchip);
        p_stmctl_cfg->threshold = (uint32)(unit * threshold * gg_security_master[lchip]->stmctl_granularity);
        SYS_SECURITY_UNLOCK(lchip);
    }



    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "StormCtlPtr:%d, unit: %d, DsStormCtl.threshold: %d\n",
                         stmctl_ptr, unit, (uint32)threshold);

    return CTC_E_NONE;
}

/*storm control*/
int32
sys_goldengate_storm_ctl_set_cfg(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg)
{

    uint16 lport = 0;
    sys_stmctl_para_t stmctl_para;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stmctl_cfg);

    sal_memset(&stmctl_para, 0, sizeof(sys_stmctl_para_t));

    if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
    {
        CTC_GLOBAL_PORT_CHECK(p_stmctl_cfg->gport);
    }
    else
    {
        CTC_VLAN_RANGE_CHECK(p_stmctl_cfg->vlan_id);
    }

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                         "type:%d, mode:%d, gport:0x%04X, vlan:%d, op:%d, storm_en:%d, discarded_to_cpu:%d, threshold :%d \n",
                         p_stmctl_cfg->type, p_stmctl_cfg->mode, p_stmctl_cfg->gport, p_stmctl_cfg->vlan_id,
                         p_stmctl_cfg->op, p_stmctl_cfg->storm_en, p_stmctl_cfg->discarded_to_cpu, p_stmctl_cfg->threshold)

    SYS_SECURITY_LOCK(lchip);
    /*ucast mode judge*/
    SYS_STMCTL_CHK_UCAST(gg_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode, p_stmctl_cfg->type);

    /*Mcast mode judge*/
    SYS_STMCTL_CHK_MCAST(gg_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode, p_stmctl_cfg->type);
    SYS_SECURITY_UNLOCK(lchip);

    CTC_ERROR_RETURN(_sys_goldengate_storm_ctl_set_mode(lchip, p_stmctl_cfg, &stmctl_para));


    if (CTC_SECURITY_STORM_CTL_OP_PORT == p_stmctl_cfg->op)
    {
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_stmctl_cfg->gport, lchip, lport);
        /*port storm ctl en*/
        if (p_stmctl_cfg->storm_en)
        {
            CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, p_stmctl_cfg->gport,\
                             SYS_PORT_PROP_STMCTL_EN, p_stmctl_cfg->storm_en));
        }
        else
        {
            for (stmctl_para.pkt_type = CTC_SECURITY_STORM_CTL_BCAST; stmctl_para.pkt_type < CTC_SECURITY_STORM_CTL_MAX; stmctl_para.pkt_type++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_stmctl_action(lchip, &stmctl_para, p_stmctl_cfg->op, SYS_STMCTL_ACTION_GET));
                if (stmctl_para.stmctl_en)
                {
                    break;
                }
            }

            if (CTC_SECURITY_STORM_CTL_MAX == stmctl_para.pkt_type)
            {
                CTC_ERROR_RETURN(sys_goldengate_port_set_internal_property(lchip, p_stmctl_cfg->gport,\
                                 SYS_PORT_PROP_STMCTL_EN, p_stmctl_cfg->storm_en));
            }
        }
    }
    else if (CTC_SECURITY_STORM_CTL_OP_VLAN == p_stmctl_cfg->op)
    {
        if (p_stmctl_cfg->storm_en)
        {
            CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, p_stmctl_cfg->vlan_id,
                             SYS_VLAN_PROP_VLAN_STORM_CTL_EN, p_stmctl_cfg->storm_en));
            CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, p_stmctl_cfg->vlan_id,
                             SYS_VLAN_PROP_VLAN_STORM_CTL_PTR, stmctl_para.stmctl_offset));
        }
        else
        {
            for (stmctl_para.pkt_type = CTC_SECURITY_STORM_CTL_BCAST; stmctl_para.pkt_type < CTC_SECURITY_STORM_CTL_MAX; stmctl_para.pkt_type++)
            {
                CTC_ERROR_RETURN(_sys_goldengate_stmctl_action(lchip, &stmctl_para, p_stmctl_cfg->op, SYS_STMCTL_ACTION_GET));
                if (stmctl_para.stmctl_en)
                {
                    break;
                }
            }

            if (CTC_SECURITY_STORM_CTL_MAX == stmctl_para.pkt_type)
            {
                CTC_ERROR_RETURN(sys_goldengate_vlan_set_internal_property(lchip, p_stmctl_cfg->vlan_id,
                                 SYS_VLAN_PROP_VLAN_STORM_CTL_EN, p_stmctl_cfg->storm_en));
            }
        }
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_storm_ctl_get_cfg(uint8 lchip, ctc_security_stmctl_cfg_t* p_stmctl_cfg)
{

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_stmctl_cfg);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,
                         "type:%d, mode:%d, gport:0x%04X ,vlan:%d, op:%d, storm_en:%d, discarded_to_cpu:%d ,threshold:%d\n",
                         p_stmctl_cfg->type, p_stmctl_cfg->mode, p_stmctl_cfg->gport, p_stmctl_cfg->vlan_id,
                         p_stmctl_cfg->op, p_stmctl_cfg->storm_en, p_stmctl_cfg->discarded_to_cpu, p_stmctl_cfg->threshold);

    SYS_SECURITY_LOCK(lchip);
    /*ucast mode judge*/
    SYS_STMCTL_CHK_UCAST(gg_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode, p_stmctl_cfg->type);

    /*Mcast mode judge*/
    SYS_STMCTL_CHK_MCAST(gg_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode, p_stmctl_cfg->type);
    SYS_SECURITY_UNLOCK(lchip);

    CTC_ERROR_RETURN(_sys_goldengate_storm_ctl_get_mode(lchip, p_stmctl_cfg));

    return CTC_E_NONE;
}

int32
sys_goldengate_storm_ctl_set_global_cfg(uint8 lchip, ctc_security_stmctl_glb_cfg_t* p_glb_cfg)
{

    uint32 cmd = 0;
    uint32 value = 0;
    uint32 update_threshold = 0;
    uint32 max_entry_num = 0;
    uint32 core_frequency = 0;
    IpeBridgeCtl_m ipe_bridge_ctl;
    IpeBridgeStormCtl_m ipe_bridge_storm_ctl;
    StormCtlCtl_m storm_ctl_ctl;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_glb_cfg);

    SYS_SECURITY_DBG_FUNC();
    SYS_SECURITY_DBG_INFO("ipg_en%d ustorm_ctl_mode:%d,mstorm_ctl_mode:%d\n",\
                          p_glb_cfg->ipg_en, p_glb_cfg->ustorm_ctl_mode, p_glb_cfg->mstorm_ctl_mode);

    /* can not configure global granularity if created storm control node */

    SYS_SECURITY_LOCK(lchip);
    gg_security_master[lchip]->stmctl_cfg.granularity = p_glb_cfg->granularity;

    switch (gg_security_master[lchip]->stmctl_cfg.granularity)
    {
        case CTC_SECURITY_STORM_CTL_GRANU_100:
            gg_security_master[lchip]->stmctl_granularity = 100;
            break;
        case CTC_SECURITY_STORM_CTL_GRANU_1000:
            gg_security_master[lchip]->stmctl_granularity = 1000;
            break;
        case CTC_SECURITY_STORM_CTL_GRANU_10000:
            SYS_SECURITY_UNLOCK(lchip);
            return CTC_E_FEATURE_NOT_SUPPORT;
            break;
        case CTC_SECURITY_STORM_CTL_GRANU_5000:
            gg_security_master[lchip]->stmctl_granularity = 5000;
            break;
        default:
            gg_security_master[lchip]->stmctl_granularity = 100;
            break;
    }

    core_frequency = sys_goldengate_get_core_freq(lchip, 0);

    if ((550 == core_frequency) || (600 == core_frequency))
    {
        max_entry_num = (core_frequency * 10) / 2;
    }
    else
    {
        /* just for 500M */
        max_entry_num = 4000;
    }

    update_threshold = SYS_SECURITY_STMCTL_GET_UPDATE(core_frequency, max_entry_num);

    sal_memset(&ipe_bridge_storm_ctl, 0, sizeof(IpeBridgeStormCtl_m));
    sal_memset(&ipe_bridge_ctl, 0, sizeof(IpeBridgeCtl_m));
    sal_memset(&storm_ctl_ctl, 0, sizeof(StormCtlCtl_m));

    value = p_glb_cfg->ipg_en ? 1 : 0;
    SetIpeBridgeStormCtl(V, ipgEn_f, &ipe_bridge_storm_ctl, value);

    value = p_glb_cfg->ustorm_ctl_mode ? 1 : 0;
    SetIpeBridgeCtl(V, unicastStormControlMode_f, &ipe_bridge_ctl, value);

    value = p_glb_cfg->mstorm_ctl_mode ? 1 : 0;
    SetIpeBridgeCtl(V, multicastStormControlMode_f, &ipe_bridge_ctl, value);

    cmd = DRV_IOW(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_storm_ctl));

    cmd = DRV_IOW(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_ctl));

    cmd = DRV_IOW(StormCtlCtl_t, StormCtlCtl_stormCtlUpdateThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &update_threshold));

    gg_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode = p_glb_cfg->ustorm_ctl_mode;
    gg_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode = p_glb_cfg->mstorm_ctl_mode;
    SYS_SECURITY_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_storm_ctl_get_global_cfg(uint8 lchip, ctc_security_stmctl_glb_cfg_t* p_glb_cfg)
{

    uint32 cmd = 0;
    IpeBridgeCtl_m ipe_bridge_ctl;
    IpeBridgeStormCtl_m ipe_bridge_storm_ctl;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_glb_cfg);

    SYS_SECURITY_DBG_FUNC();

    sal_memset(&ipe_bridge_ctl, 0, sizeof(IpeBridgeCtl_m));
    sal_memset(&ipe_bridge_storm_ctl, 0, sizeof(IpeBridgeStormCtl_m));

    cmd = DRV_IOR(IpeBridgeCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_ctl));

    cmd = DRV_IOR(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_storm_ctl));

    p_glb_cfg->ipg_en = GetIpeBridgeStormCtl(V, ipgEn_f, &ipe_bridge_storm_ctl);
    p_glb_cfg->ustorm_ctl_mode = GetIpeBridgeCtl(V, unicastStormControlMode_f, &ipe_bridge_ctl);
    p_glb_cfg->mstorm_ctl_mode = GetIpeBridgeCtl(V, multicastStormControlMode_f, &ipe_bridge_ctl);
    SYS_SECURITY_LOCK(lchip);
    p_glb_cfg->granularity = gg_security_master[lchip]->stmctl_cfg.granularity;
    SYS_SECURITY_UNLOCK(lchip);

    SYS_SECURITY_DBG_INFO("ipg_en%d ustorm_ctl_mode:%d,mstorm_ctl_mode:%d\n",\
                           p_glb_cfg->ipg_en, p_glb_cfg->ustorm_ctl_mode, p_glb_cfg->mstorm_ctl_mode);

    return CTC_E_NONE;
}

int32
sys_goldengate_port_isolation_set_route_obey_isolated_en(uint8 lchip, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SECURITY_INIT_CHECK(lchip);

    tmp = enable ? 1 : 0;
    cmd = DRV_IOW(EpeNextHopCtl_t, EpeNextHopCtl_routeObeyIsolate_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_goldengate_port_isolation_get_route_obey_isolated_en(uint8 lchip, bool* p_enable)
{

    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_enable);

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    cmd = DRV_IOR(EpeNextHopCtl_t, EpeNextHopCtl_routeObeyIsolate_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    *p_enable = (field_val == 1) ? TRUE : FALSE;

    return CTC_E_NONE;
}

/*arp snooping*/
int32
sys_goldengate_arp_snooping_set_address_check_en(uint8 lchip, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    tmp = enable ? 1 : 0;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpSrcMacCheckEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_goldengate_arp_snooping_set_address_check_exception_en(uint8 lchip, bool enable)
{

    uint32 cmd = 0;
    uint32 tmp = 0;

    SYS_SECURITY_INIT_CHECK(lchip);
    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    tmp = enable ? 1 : 0;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, IpeIntfMapperCtl_arpCheckExceptionEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "enable:%d\n", enable);

    return CTC_E_NONE;
}

int32
sys_goldengate_security_init(uint8 lchip)
{

    uint32 cmd = 0;
    uint32 core_frequency = 0;
    StormCtlCtl_m storm_ctl_ctl;
    IpeBridgeStormCtl_m ipe_bridge_storm_ctl;
    sys_goldengate_opf_t opf;
    ctc_spool_t spool;
    uint32 stmctl_offset = 0;
    uint32 max_entry_num = 0;
    DsStormCtl_m ds_storm_ctl;
    ShareStormCtlReserved_m storm_ctl_rev;
    ctc_chip_device_info_t dev_info;
    uint32 max_limit = SYS_SECURITY_LEARN_LIMIT_MAX;
    uint32 value = 0;
    uint32 index = 0;
    int32 ret;

    sal_memset(&storm_ctl_ctl, 0, sizeof(storm_ctl_ctl));
    sal_memset(&ipe_bridge_storm_ctl, 0, sizeof(ipe_bridge_storm_ctl));
    sal_memset(&ds_storm_ctl, 0, sizeof(DsStormCtl_m));
    sal_memset(&spool, 0, sizeof(ctc_spool_t));
    sal_memset(&storm_ctl_rev, 0, sizeof(ShareStormCtlReserved_m));
    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));

    SYS_SECURITY_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (NULL != gg_security_master[lchip])
    {
        return CTC_E_NONE;
    }

    gg_security_master[lchip] = (sys_security_master_t*)mem_malloc(MEM_STMCTL_MODULE, sizeof(sys_security_master_t));
    if (NULL == gg_security_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(gg_security_master[lchip], 0, sizeof(sys_security_master_t));

    /* enable port/vlan/system learn limit global and enable station move global*/
    value = 1;
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_dynamicMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_profileMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_hwSystemDynamicMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_systemMacSecurityEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_vlanMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_hwVlanMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(IpeLearningCtl_t,IpeLearningCtl_hwVsiMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_portMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(FibAccelerationCtl_t, FibAccelerationCtl_hwPortMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);
    cmd = DRV_IOW(IpeLearningCtl_t,IpeLearningCtl_hwPortMacLimitEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_stationMoveLearnEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_securityMode_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    cmd = DRV_IOW(FibAccelerationCtl_t,FibAccelerationCtl_dynamicMacDecreaseEn_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &value),ret,ERROR_FREE);

    /* creat spool and opf for learn limit theshold */
    spool.lchip = lchip;
    spool.block_num = SYS_SECURITY_LEARN_LIMIT_THRESHOLD_MAX / SYS_LEARN_LIMIT_BLOCK_SIZE;
    spool.block_size = SYS_LEARN_LIMIT_BLOCK_SIZE;
    spool.max_count = SYS_SECURITY_LEARN_LIMIT_THRESHOLD_MAX;
    spool.user_data_size = sizeof(sys_learn_limit_trhold_node_t);
    spool.spool_key = (hash_key_fn)_sys_goldengate_learn_limit_hash_make;
    spool.spool_cmp = (hash_cmp_fn)_sys_goldengate_learn_limit_hash_cmp;
    gg_security_master[lchip]->trhold_spool = ctc_spool_create(&spool);
    if (NULL == gg_security_master[lchip]->trhold_spool)
    {
        mem_free(gg_security_master[lchip]);
        return CTC_E_NO_MEMORY;
    }

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    CTC_ERROR_GOTO(sys_goldengate_opf_init(lchip, OPF_SECURITY_LEARN_LIMIT_THRHOLD, 1),ret,ERROR_FREE);
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_type = OPF_SECURITY_LEARN_LIMIT_THRHOLD;
    opf.pool_index = 0;
    /* start offset is 1 , 0 is reserve for unlimit :SYS_SECURITY_LEARN_LIMIT_MAX */
    CTC_ERROR_GOTO(sys_goldengate_opf_init_offset(lchip, &opf, 1, SYS_SECURITY_LEARN_LIMIT_THRESHOLD_MAX),ret,ERROR_FREE);
    cmd = DRV_IOW(MacLimitThreshold_t, MacLimitThreshold_maxCount_f);
    for (index = 0; index < SYS_SECURITY_LEARN_LIMIT_THRESHOLD_MAX; index++)
    {
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, index, cmd, &max_limit),ret,ERROR_FREE);
    }

    /* init system learn limit to max */
    cmd = DRV_IOW(MacLimitSystem_t, MacLimitSystem_dynamicMaxCount_f);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &max_limit),ret,ERROR_FREE);

    /*storm ctl support known unicast/mcast mode*/
    gg_security_master[lchip]->stmctl_cfg.ustorm_ctl_mode = SYS_GOLDENGATE_SECURITY_MODE_SEPERATE;
    gg_security_master[lchip]->stmctl_cfg.mstorm_ctl_mode = SYS_GOLDENGATE_SECURITY_MODE_SEPERATE;
    gg_security_master[lchip]->stmctl_cfg.ipg_en = CTC_SECURITY_STORM_CTL_MODE_PPS;
    gg_security_master[lchip]->stmctl_cfg.granularity = CTC_SECURITY_STORM_CTL_GRANU_1000;
    gg_security_master[lchip]->stmctl_granularity = 1000;

    /*init storm port and vlan base ptr offset*/
    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    sys_goldengate_opf_init(lchip, OPF_STORM_CTL, 1);
    opf.pool_type = OPF_STORM_CTL;
    opf.pool_index = 0;
    sys_goldengate_opf_init_offset(lchip, &opf, 0, SYS_SECURITY_STMCTL_MAX_PORT_VLAN_NUM);

    /*init default storm cfg*/
    core_frequency = sys_goldengate_get_core_freq(lchip, 0);

    SetDsStormCtl(V, threshold_f, &ds_storm_ctl, SYS_SECURITY_STMCTL_DISABLE_THRD);
    SetDsStormCtl(V, exceptionEn_f, &ds_storm_ctl, 0);

    cmd = DRV_IOW(DsVlanStormCtl_t, DRV_ENTRY_FLAG);
    for (stmctl_offset = 0; stmctl_offset < TABLE_MAX_INDEX(DsVlanStormCtl_t); stmctl_offset++)
    {
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, stmctl_offset, cmd, &ds_storm_ctl),ret,ERROR_FREE);
    }

    cmd = DRV_IOW(DsPortStormCtl_t, DRV_ENTRY_FLAG);
    for (stmctl_offset = 0; stmctl_offset < TABLE_MAX_INDEX(DsPortStormCtl_t); stmctl_offset++)
    {
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, stmctl_offset, cmd, &ds_storm_ctl),ret,ERROR_FREE);
    }

    SetIpeBridgeStormCtl(V, ipgEn_f, &ipe_bridge_storm_ctl, 0);
    SetIpeBridgeStormCtl(V, oamObeyStormCtl_f, &ipe_bridge_storm_ctl, 0);

    cmd = DRV_IOW(IpeBridgeStormCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &ipe_bridge_storm_ctl),ret,ERROR_FREE);

    if ((550 == core_frequency) || (600 == core_frequency))
    {
        max_entry_num = (core_frequency * 10) / 2;
    }
    else
    {
        /* just for 500M */
        max_entry_num = 4000;
    }
    /* Tunit = 1/granularity = stormCtlUpdateThreshold_f * stormCtlMaxPortNum_f / core_frequency */
    SetStormCtlCtl(V, stormCtlMaxPortNum_f, &storm_ctl_ctl, max_entry_num);
    SetStormCtlCtl(V, portStormCtlMaxUpdatePortNum_f, &storm_ctl_ctl, SYS_SECURITY_STMCTL_DEF_MAX_UPDT_PORT_NUM);
    SetStormCtlCtl(V, vlanStormCtlMaxUpdatePortNum_f, &storm_ctl_ctl, SYS_SECURITY_STMCTL_DEF_MAX_UPDT_VLAN_NUM);
    SetStormCtlCtl(V, stormCtlUpdateEn_f, &storm_ctl_ctl, 1);
    /* stormCtlUpdateThreshold_f can not small than 100 */
    SetStormCtlCtl(V, stormCtlUpdateThreshold_f, &storm_ctl_ctl, SYS_SECURITY_STMCTL_GET_UPDATE(core_frequency, max_entry_num));
    SetStormCtlCtl(V, packetShift_f, &storm_ctl_ctl, SYS_SECURITY_STMCTL_UNIT);
    SetStormCtlCtl(V, byteShift_f, &storm_ctl_ctl, SYS_SECURITY_STMCTL_UNIT);
    SetStormCtlCtl(V, dropCompensate_f, &storm_ctl_ctl, 1);

    cmd = DRV_IOW(StormCtlCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &storm_ctl_ctl),ret,ERROR_FREE);

    /* set ShareStormCtlReserved */
    sys_goldengate_chip_get_device_info(lchip, &dev_info);
    if (dev_info.version_id > 1)
    {
        SetShareStormCtlReserved(V, reserved_f, &storm_ctl_rev, 1);
        cmd = DRV_IOW(ShareStormCtlReserved_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &storm_ctl_rev),ret,ERROR_FREE);
    }

    gg_security_master[lchip]->ip_src_guard_def = 0;

    SYS_SECURITY_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_SECURITY, sys_goldengate_security_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
	  CTC_ERROR_RETURN(sys_goldengate_security_wb_restore(lchip));
    }

    /* init learn limit cfg , enable learn limit */
    return CTC_E_NONE;

ERROR_FREE :

    if (NULL != gg_security_master[lchip]->trhold_spool)
    {
        mem_free(gg_security_master[lchip]->trhold_spool);
    }

    if (NULL != gg_security_master[lchip])
    {
        mem_free(gg_security_master[lchip]);
    }

    return ret;
}

int32
sys_goldengate_security_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == gg_security_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_STORM_CTL);
    sys_goldengate_opf_deinit(lchip, OPF_SECURITY_LEARN_LIMIT_THRHOLD);

    /*free trhold spool*/
    ctc_spool_free(gg_security_master[lchip]->trhold_spool);

    sal_mutex_destroy(gg_security_master[lchip]->mutex);
    mem_free(gg_security_master[lchip]);

    return CTC_E_NONE;
}

int32
_sys_goldengate_learn_limit_wb_mapping(sys_wb_learn_limit_trhold_node_t *p_wb_limit, sys_learn_limit_trhold_node_t *p_limit, uint8 sync)
{
    if (sync)
    {
        p_wb_limit->value 	= p_limit->value;
        p_wb_limit->cnt 		= p_limit->cnt;
        p_wb_limit->index 	= p_limit->index;
    }
    else
    {
        p_limit->value	= p_wb_limit->value;
        p_limit->cnt		= p_wb_limit->cnt;
        p_limit->index	= p_wb_limit->index;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_learn_limit_wb_sync_func(void* node, void* user_data)
{
    uint16 max_entry_cnt = 0;
    sys_learn_limit_trhold_node_t *p_limit = (sys_learn_limit_trhold_node_t *)(((ctc_spool_node_t*)node)->data);
    sys_wb_learn_limit_trhold_node_t  *p_wb_limit;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)user_data;

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_limit = (sys_wb_learn_limit_trhold_node_t *)wb_data->buffer + wb_data->valid_cnt;

    CTC_ERROR_RETURN(_sys_goldengate_learn_limit_wb_mapping(p_wb_limit, p_limit, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_security_wb_sync(uint8 lchip)
{
    uint16 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_security_master_t  *p_wb_gg_security_master;

    /*syncup security matser*/
    wb_data.buffer = mem_malloc(MEM_STMCTL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_security_master_t, CTC_FEATURE_SECURITY, SYS_WB_APPID_SECURITY_SUBID_MASTER);

    p_wb_gg_security_master = (sys_wb_security_master_t  *)wb_data.buffer;
    sal_memset(p_wb_gg_security_master, 0, sizeof(sys_wb_security_master_t));

    p_wb_gg_security_master->lchip = lchip;
    sal_memcpy(&p_wb_gg_security_master->stmctl_cfg, &gg_security_master[lchip]->stmctl_cfg, sizeof(ctc_security_stmctl_glb_cfg_t));
    p_wb_gg_security_master->flag = gg_security_master[lchip]->flag;
    p_wb_gg_security_master->stmctl_granularity = gg_security_master[lchip]->stmctl_granularity;
    p_wb_gg_security_master->ip_src_guard_def = gg_security_master[lchip]->ip_src_guard_def;

    for (loop = 0; loop < SYS_SECURITY_PORT_UNIT_INDEX_MAX; loop++)
    {
        p_wb_gg_security_master->port_unit_mode[loop] = gg_security_master[lchip]->port_unit_mode[loop];
    }

    for (loop = 0; loop < SYS_SECURITY_VLAN_UNIT_INDEX_MAX; loop++)
    {
        p_wb_gg_security_master->vlan_unit_mode[loop] = gg_security_master[lchip]->vlan_unit_mode[loop];
    }

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup learn limit*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_learn_limit_trhold_node_t, CTC_FEATURE_SECURITY, SYS_WB_APPID_SECURITY_SUBID_LIMIT);

    CTC_ERROR_GOTO(ctc_spool_traverse(gg_security_master[lchip]->trhold_spool, _sys_goldengate_learn_limit_wb_sync_func, (void *)&wb_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }

    return ret;
}

int32
sys_goldengate_security_wb_restore(uint8 lchip)
{
    uint16 loop1 = 0;
    uint16 loop2 = 0;
    uint16 entry_cnt = 0;
    uint8 stmctl_offset = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_security_master_t  *p_wb_gg_security_master;
    sys_wb_learn_limit_trhold_node_t *p_wb_limit;
    sys_learn_limit_trhold_node_t *p_limit;
    sys_learn_limit_trhold_node_t *p_limit_get;
    sys_goldengate_opf_t opf;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_STMCTL_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_security_master_t, CTC_FEATURE_SECURITY, SYS_WB_APPID_SECURITY_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore security master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query security master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_gg_security_master = (sys_wb_security_master_t *)wb_query.buffer;

    sal_memcpy(&gg_security_master[lchip]->stmctl_cfg, &p_wb_gg_security_master->stmctl_cfg, sizeof(ctc_security_stmctl_glb_cfg_t));
    gg_security_master[lchip]->flag = p_wb_gg_security_master->flag;
    gg_security_master[lchip]->stmctl_granularity = p_wb_gg_security_master->stmctl_granularity;
    gg_security_master[lchip]->ip_src_guard_def = p_wb_gg_security_master->ip_src_guard_def;

    for (loop1 = 0; loop1 < SYS_SECURITY_PORT_UNIT_INDEX_MAX; loop1++)
    {
        gg_security_master[lchip]->port_unit_mode[loop1] = p_wb_gg_security_master->port_unit_mode[loop1];
    }

    for (loop1 = 0; loop1 < SYS_SECURITY_VLAN_UNIT_INDEX_MAX; loop1++)
    {
        gg_security_master[lchip]->vlan_unit_mode[loop1] = p_wb_gg_security_master->vlan_unit_mode[loop1];
        for(loop2 = 0; loop2 < BITS_NUM_OF_WORD; loop2++)
        {
            if(IS_BIT_SET(gg_security_master[lchip]->vlan_unit_mode[loop1], loop2))
            {
                sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                opf.pool_type = OPF_STORM_CTL;
                opf.pool_index = 0;
                stmctl_offset = (loop1 * 32 + loop2) & 0x7f;
                CTC_ERROR_GOTO(sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, stmctl_offset), ret, done);
            }
        }
    }

    /*restore learn limit*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_learn_limit_trhold_node_t, CTC_FEATURE_SECURITY, SYS_WB_APPID_SECURITY_SUBID_LIMIT);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_limit = (sys_wb_learn_limit_trhold_node_t *)wb_query.buffer + entry_cnt++;

        p_limit = mem_malloc(MEM_STMCTL_MODULE,  sizeof(sys_learn_limit_trhold_node_t));
        if (NULL == p_limit)
        {
            ret = CTC_E_NO_MEMORY;
            goto done;
        }
        sal_memset(p_limit, 0, sizeof(sys_learn_limit_trhold_node_t));

        ret = _sys_goldengate_learn_limit_wb_mapping(p_wb_limit, p_limit, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ret = ctc_spool_add(gg_security_master[lchip]->trhold_spool, p_limit, NULL, &p_limit_get);

        if (ret != CTC_SPOOL_E_OPERATE_MEMORY)
        {
            mem_free(p_limit);
        }

        if (ret < 0)
        {
            CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctc_spool_add error! ret: %d.\n", ret);

            continue;
        }
        else
        {
            if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
            {
                /*alloc learn limit index from position*/
                sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                opf.pool_type = OPF_SECURITY_LEARN_LIMIT_THRHOLD;
                opf.pool_index = 0;
                ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_limit_get->index);
                if (ret)
                {
                    ctc_spool_remove(gg_security_master[lchip]->trhold_spool, p_limit_get, NULL);
                    mem_free(p_limit);
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc learn limit index from position %u error! ret: %d.\n", p_limit_get->index, ret);

                    continue;
                }
            }
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}


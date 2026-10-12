/**
   @file sys_goldengate_scl.c

   @date 2011-11-19

   @version v2.0

   The file contains all scl APIs of sys layer

 */
/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "sal.h"

#include "ctc_error.h"
#include "ctc_hash.h"
#include "ctc_spool.h"
#include "ctc_const.h"
#include "ctc_vlan.h"
#include "ctc_qos.h"
#include "ctc_linklist.h"
#include "ctc_debug.h"


#include "sys_goldengate_scl.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_port.h"
#include "sys_goldengate_opf.h"
#include "sys_goldengate_stats.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_qos_policer.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_queue_enq.h"
#include "sys_goldengate_aps.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_nexthop.h"
#include "sys_goldengate_wb_common.h"

#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
 * Defines and Macros
 *
 *****************************************************************************/

#define SCL_SET_MAC(d, s)       sal_memcpy(d, s, sizeof(mac_addr_t))
#define SCL_SET_HW_MAC(d, s)    SYS_GOLDENGATE_SET_HW_MAC(d, s)
#define SCL_SET_HW_IP6(d, s)    SYS_GOLDENGATE_REVERT_IP6(d, s)


#define NORMAL_AD_SHARE_POOL     0
#define DEFAULT_AD_SHARE_POOL    1

#define SCL_INNER_ENTRY_ID(eid)          ((eid >= SYS_SCL_MIN_INNER_ENTRY_ID) && (eid <= SYS_SCL_MAX_INNER_ENTRY_ID))
#define SCL_TCAM_DEFAUT_GROUP_ID(gid)                           \
    ((gid == SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD)          \
     || (gid == SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS)         \
     || (gid == (SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1)) \
     || (gid == (SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS + 1)))

#define SCL_GET_TABLE_ENTYR_NUM(t, n)    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(t, n))

/* used by show only */
#define SYS_SCL_SHOW_IN_HEX   0xFFFFF

/*Get step by fpa_size, step is 1 for hash*/
#define SYS_SCL_MAP_FPA_SIZE_TO_STEP(fpa_size)  \
    (CTC_FPA_KEY_SIZE_160==(fpa_size))?1:((CTC_FPA_KEY_SIZE_320==(fpa_size))?2:((CTC_FPA_KEY_SIZE_640==(fpa_size)))?4:1)

/****************************************************************
*
* Global and Declaration
*
****************************************************************/
#define SYS_SCL_VLAN_ACTION_RESERVE_NUM    63      /* include 0. */


/*software hash size, determined by scl owner.*/
#define SCL_HASH_BLOCK_SIZE            1024
#define VLAN_ACTION_SIZE_PER_BUCKET    16


typedef enum sys_scl_error_label_1_e
{
    OUTPUT_INDEX_NEW_TCAM,
    OUTPUT_INDEX_OLD_TCAM,
    OUTPUT_INDEX_NEW_HASH,
    OUTPUT_INDEX_OLD_HASH
} sys_scl_error_label_1_t;

/****************************************************************
*
* Functions
*
****************************************************************/

#define ___________SCL_INNER_FUNCTION________________________

#define __4_SCL_HW__


#define __________________________TOTAL_NEW_______________
/**
   @file sys_goldengate_scl.c

   @date 2012-09-19

   @version v2.0

 */

/****************************************************************************
*
* Header Files
*
****************************************************************************/

#define SYS_SCL_ENTRY_OP_FLAG_ADD       1
#define SYS_SCL_ENTRY_OP_FLAG_DELETE    2

#define SYS_SCL_IS_DEFAULT(key_type)    ((key_type == SYS_SCL_KEY_PORT_DEFAULT_INGRESS) || (key_type == SYS_SCL_KEY_PORT_DEFAULT_EGRESS))

struct sys_scl_master_s
{
    ctc_hash_t         * group;                               /* Hash table save group by gid.*/
    ctc_fpa_t          * fpa;                                 /* Fast priority arrangement. no array. */
    ctc_hash_t         * entry;                               /* Hash table save hash|tcam entry by eid */
    ctc_hash_t         * entry_by_key;                        /* Hash table save hash entry by key */
    ctc_spool_t        * ad_spool[2]; /* Share pool save hash action & default action  */
    ctc_spool_t        * vlan_edit_spool;                     /* Share pool save vlan edit  */
    sys_scl_sw_block_t block[SCL_BLOCK_ID_NUM];
    ctc_linklist_t     * group_list[SYS_SCL_GROUP_LIST_NUM];                       /* outer tcam , outer hash, inner */

    uint32             max_entry_priority;

    uint8              alloc_id[SYS_SCL_KEY_NUM];

    uint8              entry_size[SYS_SCL_KEY_NUM];
    uint8              key_size[SYS_SCL_KEY_NUM];
    uint8              hash_igs_action_size;
    uint8              hash_egs_action_size;
    uint8              hash_tunnel_action_size;
    uint8              hash_flow_action_size;
    uint8              vlan_edit_size;
    uint8              ad_type[SYS_SCL_ACTION_NUM];
    uint8              mac_tcam_320bit_mode_en;

    uint32             tunnel_count[SYS_SCL_TUNNEL_TYPE_NUM];
    uint32             def_eid[CTC_BOTH_DIRECTION][SYS_GG_MAX_PORT_NUM_PER_CHIP];
    uint32             igs_default_base;
    uint32             tunnel_default_base;

    uint32             action_count[SYS_SCL_ACTION_NUM];
    uint32             egs_entry_num;
    sal_mutex_t        * mutex;
};
typedef struct sys_scl_master_s   sys_scl_master_t;
sys_scl_master_t* p_gg_scl_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


#define SYS_SCL_DBG_OUT(level, FMT, ...)    CTC_DEBUG_OUT(scl, scl, SCL_SYS, level, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_DUMP(FMT, ...)          SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_PARAM(FMT, ...)         SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_INFO(FMT, ...)          SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_ERROR(FMT, ...)         SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ## __VA_ARGS__)
#define SYS_SCL_DBG_FUNC()                  SYS_SCL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)

#define SYS_SCL_INIT_CHECK(lchip)           \
    do {                                    \
        SYS_LCHIP_CHECK_ACTIVE(lchip);           \
        if (NULL == p_gg_scl_master[lchip]) {  \
            return CTC_E_NOT_INIT; }        \
    } while(0)

#define SYS_SCL_CREATE_LOCK(lchip)                          \
    do                                                      \
    {                                                       \
        sal_mutex_create(&p_gg_scl_master[lchip]->mutex);   \
        if (NULL == p_gg_scl_master[lchip]->mutex)          \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_SCL_LOCK(lchip) \
    sal_mutex_lock(p_gg_scl_master[lchip]->mutex)

#define SYS_SCL_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_scl_master[lchip]->mutex)

#define CTC_ERROR_RETURN_SCL_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_scl_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

/* input rsv group id , return failed */
#define SYS_SCL_IS_RSV_GROUP(gid) \
    ((gid) > CTC_SCL_GROUP_ID_MAX_NORMAL && (gid) <= SYS_SCL_GROUP_ID_MAX)

#define SYS_SCL_IS_RSV_TCAM_GROUP(gid)    ((gid) >= SYS_SCL_GROUP_ID_INNER_TCAM_BEGIN)
#define SYS_SCL_IS_TCAM_GROUP(gid)        (!SYS_SCL_IS_RSV_GROUP(gid) || SYS_SCL_IS_RSV_TCAM_GROUP(gid))


#define SYS_SCL_CHECK_OUTER_GROUP_ID(gid)        \
    {                                            \
        if ((gid) > CTC_SCL_GROUP_ID_HASH_MAX) { \
            return CTC_E_SCL_GROUP_ID; }         \
    }


#define SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(gid)   \
    {                                              \
        if ((gid) > CTC_SCL_GROUP_ID_MAX_NORMAL) { \
            return CTC_E_SCL_GROUP_ID; }           \
    }

#define SYS_SCL_CHECK_GROUP_TYPE(type)        \
    {                                         \
        if (type >= CTC_SCL_GROUP_TYPE_MAX) { \
            return CTC_E_SCL_GROUP_TYPE; }    \
    }

#define SYS_SCL_CHECK_GROUP_PRIO(priority)           \
    {                                                \
        if (priority >= SCL_BLOCK_ID_NUM) { \
            return CTC_E_SCL_GROUP_PRIORITY; }       \
    }

#define SYS_SCL_CHECK_ENTRY_ID(eid)            \
    {                                          \
        if (eid > CTC_SCL_MAX_USER_ENTRY_ID) { \
            return CTC_E_INVALID_PARAM; }      \
    }

#define SYS_SCL_CHECK_ENTRY_PRIO(priority)  \
    {                                          \
        if (priority > FPA_PRIORITY_HIGHEST) { \
            return CTC_E_INVALID_PARAM; }      \
    }

#define SYS_SCL_CHECK_GROUP_UNEXIST(pg)     \
    {                                       \
        if (!pg)                            \
        {                                   \
            SYS_SCL_UNLOCK(lchip);          \
            return CTC_E_SCL_GROUP_UNEXIST; \
        }                                   \
    }


#define SYS_SCL_SET_MAC_HIGH(dest_h, src)    \
    {                                        \
        dest_h = ((src[0] << 8) | (src[1])); \
    }

#define SYS_SCL_SET_MAC_LOW(dest_l, src)                                       \
    {                                                                          \
        dest_l = ((src[2] << 24) | (src[3] << 16) | (src[4] << 8) | (src[5])); \
    }

#define SYS_SCL_VLAN_ID_CHECK(vlan_id)           \
    {                                            \
        if ((vlan_id) > CTC_MAX_VLAN_ID) {       \
            return CTC_E_VLAN_INVALID_VLAN_ID; } \
    }                                            \

#define SCL_ENTRY_IS_TCAM(type)                                                   \
    ((SYS_SCL_KEY_TCAM_VLAN == type) || (SYS_SCL_KEY_TCAM_MAC == type) ||         \
     (SYS_SCL_KEY_TCAM_IPV4 == type) || (SYS_SCL_KEY_TCAM_IPV4_SINGLE == type) || \
     (SYS_SCL_KEY_TCAM_IPV6 == type))

#define SCL_PRINT_ENTRY_ROW(eid)               SYS_SCL_DBG_DUMP("\n>>entry-id %u : \n", (eid))
#define SCL_PRINT_GROUP_ROW(gid)               SYS_SCL_DBG_DUMP("\n>>group-id %u (first in goes last, installed goes tail) :\n", (gid))
#define SCL_PRINT_PRIO_ROW(prio)               SYS_SCL_DBG_DUMP("\n>>group-prio %u (sort by block_idx ) :\n", (prio))
#define SCL_PRINT_TYPE_ROW(type)               SYS_SCL_DBG_DUMP("\n>>type %u (sort by block_idx ) :\n", (type))
#define SCL_PRINT_ALL_SORT_SEP_BY_ROW(type)    SYS_SCL_DBG_DUMP("\n>>Separate by %s. \n", (type) ? "group" : "priority")

#define SCL_PRINT_COUNT(n)                     SYS_SCL_DBG_DUMP("NO.%04d  ", n)
#define SYS_SCL_ALL_KEY     SYS_SCL_KEY_NUM
#define FIB_KEY_TYPE_SCL    0x1F

#define SCL_GET_TABLE_ENTYR_NUM(t, n)    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(t, n))

typedef struct
{
    uint16             count;
    uint8              detail;
    uint8              rsv;
    sys_scl_key_type_t key_type;
} _scl_cb_para_t;

#define FAIL(ret)              (ret < 0)

#define SCL_OUTER_ENTRY(pe)    ((sys_scl_sw_entry_t *) ((uint8 *) pe - sizeof(ctc_slistnode_t)))

#define ___________SCL_INNER_FUNCTION________________________



#define ___________SCL_TRAVERSE_ALL_________________________

int32
sys_goldengate_scl_show_tunnel_status(uint8 lchip)
{
    uint8 idx = 0;
    uint32 count = 0;

    static char *tunnel_type[SYS_SCL_TUNNEL_TYPE_NUM] =
    {
        "None",
        "IPV4 in IPV4",
        "IPV6 in IPV4",
        "GRE in IPV4",
        "NVGRE in IPV4",
        "VXLAN in IPV4",
        "GENEVE in IPV4",
        "IPV4 in IPV6",
        "IPV6 in IPV6",
        "GRE in IPV6",
        "NVGRE in IPV6",
        "VXLAN in IPV6",
        "GENEVE in IPV6",
        "RPF",
        "TRILL",
    };

    SYS_SCL_INIT_CHECK(lchip);
    SYS_SCL_DBG_FUNC();



    SYS_SCL_DBG_DUMP("%-7s%-17s%s\n", "No.", "TUNNEL_TYPE", "COUNT");
    SYS_SCL_DBG_DUMP("---------------------------------\n");

    SYS_SCL_LOCK(lchip);
    for(idx = 1; idx < SYS_SCL_TUNNEL_TYPE_NUM; idx++)
    {
        SYS_SCL_DBG_DUMP("%-7u%-17s%u\n", idx, tunnel_type[idx], p_gg_scl_master[lchip]->tunnel_count[idx]);
        count = count + p_gg_scl_master[lchip]->tunnel_count[idx];
    }
    SYS_SCL_UNLOCK(lchip);

    SYS_SCL_DBG_DUMP("\nTotal count : %u\n", count);
    return CTC_E_NONE;
}


#define __SCL_GET_FROM_SW__

/* only hash will call this fuction
 * pe must not NULL
 * pg can be NULL
 */
STATIC int32
_sys_goldengate_scl_get_nodes_by_key(uint8 lchip, sys_scl_sw_entry_t*  pe_in,
                                     sys_scl_sw_entry_t** pe,
                                     sys_scl_sw_group_t** pg)
{
    sys_scl_sw_entry_t* pe_lkup = NULL;
    sys_scl_sw_group_t* pg_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe_in);
    CTC_PTR_VALID_CHECK(pe);

    SYS_SCL_DBG_FUNC();

    pe_lkup = ctc_hash_lookup(p_gg_scl_master[lchip]->entry_by_key, pe_in);
    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;
    }

    *pe = pe_lkup;
    if (pg)
    {
        *pg = pg_lkup;
    }

    return CTC_E_NONE;
}

/*
 * get sys entry node by entry id
 * pg, pb if be NULL, won't care.
 * pe cannot be NULL.
 */
STATIC int32
_sys_goldengate_scl_get_nodes_by_eid(uint8 lchip, uint32 eid,
                                     sys_scl_sw_entry_t** pe,
                                     sys_scl_sw_group_t** pg,
                                     sys_scl_sw_block_t** pb)
{
    sys_scl_sw_entry_t sys_entry;
    sys_scl_sw_entry_t * pe_lkup = NULL;
    sys_scl_sw_group_t * pg_lkup = NULL;
    sys_scl_sw_block_t * pb_lkup = NULL;

    CTC_PTR_VALID_CHECK(pe);
    SYS_SCL_DBG_FUNC();


    sal_memset(&sys_entry, 0, sizeof(sys_scl_sw_entry_t));
    sys_entry.fpae.entry_id = eid;
    sys_entry.lchip = lchip;
    pe_lkup = ctc_hash_lookup(p_gg_scl_master[lchip]->entry, &sys_entry);

    if (pe_lkup)
    {
        pg_lkup = pe_lkup->group;

        /* get block */
        if (SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
        {
            pb_lkup  = &p_gg_scl_master[lchip]->block[pg_lkup->group_info.block_id];
        }
    }


    *pe = pe_lkup;
    if (pg)
    {
        *pg = pg_lkup;
    }
    if (pb)
    {
        *pb = pb_lkup;
    }

    return CTC_E_NONE;
}

/*
 * get sys group node by group id
 */
STATIC int32
_sys_goldengate_scl_get_group_by_gid(uint8 lchip, uint32 gid, sys_scl_sw_group_t** sys_group_out)
{
    sys_scl_sw_group_t * p_sys_group_lkup = NULL;
    sys_scl_sw_group_t sys_group;

    CTC_PTR_VALID_CHECK(sys_group_out);
    SYS_SCL_DBG_FUNC();

    sal_memset(&sys_group, 0, sizeof(sys_scl_sw_group_t));
    sys_group.group_id = gid;

    p_sys_group_lkup = ctc_hash_lookup(p_gg_scl_master[lchip]->group, &sys_group);

    *sys_group_out = p_sys_group_lkup;

    return CTC_E_NONE;
}


/*
 * below is build hw struct
 */
#define __SCL_OPERATE_ON_HW__

/*
 * build driver group info based on sys group info
 */
STATIC int32
_sys_goldengate_scl_build_ds_group_info(uint8 lchip, sys_scl_sw_group_info_t* pg_sys, drv_scl_group_info_t* pg_drv)
{
    CTC_PTR_VALID_CHECK(pg_drv);
    CTC_PTR_VALID_CHECK(pg_sys);

    SYS_SCL_DBG_FUNC();
    switch (pg_sys->type)
    {
    case     CTC_SCL_GROUP_TYPE_HASH: /* won't use it anyway.*/
    case     CTC_SCL_GROUP_TYPE_GLOBAL:
    case     CTC_SCL_GROUP_TYPE_NONE:
        pg_drv->class_id_data = 0;
        pg_drv->class_id_mask = 0;

        pg_drv->gport      = 0;
        pg_drv->gport_mask = 0;

        pg_drv->gport_type      = 0;
        pg_drv->gport_type_mask = 0;
        break;
    case     CTC_SCL_GROUP_TYPE_PORT:
        pg_drv->class_id_data = 0;
        pg_drv->class_id_mask = 0;

        pg_drv->gport      = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pg_sys->u0.gport);
        pg_drv->gport_mask = 0x3FFF;

        pg_drv->gport_type      = 0;
        pg_drv->gport_type_mask = 1;
        break;
    case     CTC_SCL_GROUP_TYPE_LOGIC_PORT:
        pg_drv->class_id_data = 0;
        pg_drv->class_id_mask = 0;

        pg_drv->gport      = pg_sys->u0.gport;
        pg_drv->gport_mask = 0x3FFF;

        pg_drv->gport_type      = 1;
        pg_drv->gport_type_mask = 1;
        break;

    case     CTC_SCL_GROUP_TYPE_PORT_CLASS:
        pg_drv->class_id_data = pg_sys->u0.port_class_id;
        pg_drv->class_id_mask = 0xFF;

        pg_drv->gport      = 0;
        pg_drv->gport_mask = 0;

        pg_drv->gport_type      = 0;
        pg_drv->gport_type_mask = 0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

 /*a: fld_id.  b: ds.  c: value.*/
#define SET_VLANXC   SetDsEgressXcOamDoubleVlanPortHashKey


STATIC uint32
_sys_goldengate_scl_sum(uint8 lchip, uint32 a, ...)
{
    va_list list;
    uint32  v   = a;
    uint32  sum = 0;
    va_start(list, a);
    while (v != -1)
    {
        sum += v;
        v    = va_arg(list, int32);
    }
    va_end(list);

    return sum;
}


#define SCL_SUM(...)    _sys_goldengate_scl_sum(__VA_ARGS__, -1)

int32
sys_goldengate_scl_wb_sync(uint8 lchip);

int32
sys_goldengate_scl_wb_restore(uint8 lchip);

STATIC int32
_sys_goldengate_scl_build_hash_l2(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    hw_mac_addr_t               hw_mac = { 0 };
    sys_scl_sw_hash_key_l2_t* pl2 = &(psys->u0.hash.u0.l2);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdSclFlowL2HashKey_m));

    SetDsUserIdSclFlowL2HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_SCLFLOWL2);
    SetDsUserIdSclFlowL2HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_SCLFLOWL2);
    SetDsUserIdSclFlowL2HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdSclFlowL2HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdSclFlowL2HashKey(V, dsAdIndex_f, pu32, index);

    SetDsUserIdSclFlowL2HashKey(V, flowFieldSel_f, pu32, pl2->field_sel);
    SetDsUserIdSclFlowL2HashKey(V, userIdLabel_f, pu32, pl2->class_id);
    SetDsUserIdSclFlowL2HashKey(V, etherType_f, pu32, pl2->ether_type);
    SetDsUserIdSclFlowL2HashKey(V, svlanIdValid_f, pu32, pl2->stag_exist);
    SetDsUserIdSclFlowL2HashKey(V, stagCos_f, pu32, pl2->scos);
    SetDsUserIdSclFlowL2HashKey(V, svlanId_f, pu32, pl2->svid);
    SetDsUserIdSclFlowL2HashKey(V, stagCfi_f, pu32, pl2->scfi);

    SCL_SET_HW_MAC(hw_mac, pl2->macda);
    SetDsUserIdSclFlowL2HashKey(A, macDa_f, pu32, hw_mac);
    SCL_SET_HW_MAC(hw_mac, pl2->macsa);
    SetDsUserIdSclFlowL2HashKey(A, macSa_f, pu32, hw_mac);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_2vlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_vtag_t * pvtag = &(psys->u0.hash.u0.vtag);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pvtag->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdDoubleVlanPortHashKey_m));
        SetDsUserIdDoubleVlanPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_DOUBLEVLANPORT);
        SetDsUserIdDoubleVlanPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdDoubleVlanPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdDoubleVlanPortHashKey(V, cvlanId_f, pu32, pvtag->cvid);
        SetDsUserIdDoubleVlanPortHashKey(V, svlanId_f, pu32, pvtag->svid);
        SetDsUserIdDoubleVlanPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsUserIdDoubleVlanPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsUserIdDoubleVlanPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsUserIdDoubleVlanPortHashKey(V, globalSrcPort_f, pu32, pvtag->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamDoubleVlanPortHashKey_m));
        SetDsEgressXcOamDoubleVlanPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_DOUBLEVLANPORT);
        SetDsEgressXcOamDoubleVlanPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamDoubleVlanPortHashKey(V, cvlanId_f, pu32, pvtag->cvid);
        SetDsEgressXcOamDoubleVlanPortHashKey(V, svlanId_f, pu32, pvtag->svid);
        SetDsEgressXcOamDoubleVlanPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsEgressXcOamDoubleVlanPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsEgressXcOamDoubleVlanPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsEgressXcOamDoubleVlanPortHashKey(V, globalDestPort_f, pu32, pvtag->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_port(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_port_t* pport = &(psys->u0.hash.u0.port);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pport->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdPortHashKey_m));
        SetDsUserIdPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_PORT);
        SetDsUserIdPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdPortHashKey(V, isLogicPort_f, pu32, pport->gport_is_logic);
        SetDsUserIdPortHashKey(V, isLabel_f, pu32, pport->gport_is_classid);
        if (!pport->gport_is_logic && !pport->gport_is_classid)
        {
            SetDsUserIdPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pport->gport));
        }
        else
        {
            SetDsUserIdPortHashKey(V, globalSrcPort_f, pu32, pport->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamPortHashKey_m));
        SetDsEgressXcOamPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_PORT);
        SetDsEgressXcOamPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamPortHashKey(V, isLogicPort_f, pu32, pport->gport_is_logic);
        SetDsEgressXcOamPortHashKey(V, isLabel_f, pu32, pport->gport_is_classid);
        if (!pport->gport_is_logic && !pport->gport_is_classid)
        {
            SetDsEgressXcOamPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pport->gport));
        }
        else
        {
            SetDsEgressXcOamPortHashKey(V, globalDestPort_f, pu32, pport->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_cvlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_vlan_t * pvlan = &(psys->u0.hash.u0.vlan);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pvlan->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdCvlanPortHashKey_m));
        SetDsUserIdCvlanPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_CVLANPORT);
        SetDsUserIdCvlanPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdCvlanPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdCvlanPortHashKey(V, isLogicPort_f, pu32, pvlan->gport_is_logic);
        SetDsUserIdCvlanPortHashKey(V, isLabel_f, pu32, pvlan->gport_is_classid);
        SetDsUserIdCvlanPortHashKey(V, cvlanId_f, pu32, pvlan->vid);
        if (!pvlan->gport_is_logic && !pvlan->gport_is_classid)
        {
            SetDsUserIdCvlanPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvlan->gport));
        }
        else
        {
            SetDsUserIdCvlanPortHashKey(V, globalSrcPort_f, pu32, pvlan->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamCvlanPortHashKey_m));
        SetDsEgressXcOamCvlanPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_CVLANPORT);
        SetDsEgressXcOamCvlanPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamCvlanPortHashKey(V, isLogicPort_f, pu32, pvlan->gport_is_logic);
        SetDsEgressXcOamCvlanPortHashKey(V, isLabel_f, pu32, pvlan->gport_is_classid);
        SetDsEgressXcOamCvlanPortHashKey(V, cvlanId_f, pu32, pvlan->vid);
        if (!pvlan->gport_is_logic && !pvlan->gport_is_classid)
        {
            SetDsEgressXcOamCvlanPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvlan->gport));
        }
        else
        {
            SetDsEgressXcOamCvlanPortHashKey(V, globalDestPort_f, pu32, pvlan->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_svlan(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_vlan_t * pvlan = &(psys->u0.hash.u0.vlan);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pvlan->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdSvlanPortHashKey_m));
        SetDsUserIdSvlanPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_SVLANPORT);
        SetDsUserIdSvlanPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdSvlanPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdSvlanPortHashKey(V, isLogicPort_f, pu32, pvlan->gport_is_logic);
        SetDsUserIdSvlanPortHashKey(V, isLabel_f, pu32, pvlan->gport_is_classid);
        SetDsUserIdSvlanPortHashKey(V, svlanId_f, pu32, pvlan->vid);
        if (!pvlan->gport_is_logic && !pvlan->gport_is_classid)
        {
            SetDsUserIdSvlanPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvlan->gport));
        }
        else
        {
            SetDsUserIdSvlanPortHashKey(V, globalSrcPort_f, pu32, pvlan->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamSvlanPortHashKey_m));
        SetDsEgressXcOamSvlanPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_SVLANPORT);
        SetDsEgressXcOamSvlanPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamSvlanPortHashKey(V, isLogicPort_f, pu32, pvlan->gport_is_logic);
        SetDsEgressXcOamSvlanPortHashKey(V, isLabel_f, pu32, pvlan->gport_is_classid);
        SetDsEgressXcOamSvlanPortHashKey(V, svlanId_f, pu32, pvlan->vid);
        if (!pvlan->gport_is_logic && !pvlan->gport_is_classid)
        {
            SetDsEgressXcOamSvlanPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvlan->gport));
        }
        else
        {
            SetDsEgressXcOamSvlanPortHashKey(V, globalDestPort_f, pu32, pvlan->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_cvlan_cos(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_vtag_t * pvtag = &(psys->u0.hash.u0.vtag);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pvtag->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdCvlanCosPortHashKey_m));
        SetDsUserIdCvlanCosPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_CVLANCOSPORT);
        SetDsUserIdCvlanCosPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdCvlanCosPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdCvlanCosPortHashKey(V, ctagCos_f, pu32, pvtag->ccos);
        SetDsUserIdCvlanCosPortHashKey(V, cvlanId_f, pu32, pvtag->cvid);
        SetDsUserIdCvlanCosPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsUserIdCvlanCosPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsUserIdCvlanCosPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsUserIdCvlanCosPortHashKey(V, globalSrcPort_f, pu32, pvtag->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamCvlanCosPortHashKey_m));
        SetDsEgressXcOamCvlanCosPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_CVLANCOSPORT);
        SetDsEgressXcOamCvlanCosPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamCvlanCosPortHashKey(V, ctagCos_f, pu32, pvtag->ccos);
        SetDsEgressXcOamCvlanCosPortHashKey(V, cvlanId_f, pu32, pvtag->cvid);
        SetDsEgressXcOamCvlanCosPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsEgressXcOamCvlanCosPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsEgressXcOamCvlanCosPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsEgressXcOamCvlanCosPortHashKey(V, globalDestPort_f, pu32, pvtag->gport);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_svlan_cos(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_vtag_t * pvtag = &(psys->u0.hash.u0.vtag);

    CTC_PTR_VALID_CHECK(pu32);
    if (CTC_INGRESS == pvtag->dir)
    {
        sal_memset(pu32, 0, sizeof(DsUserIdSvlanCosPortHashKey_m));
        SetDsUserIdSvlanCosPortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_SVLANCOSPORT);
        SetDsUserIdSvlanCosPortHashKey(V, dsAdIndex_f, pu32, index);
        SetDsUserIdSvlanCosPortHashKey(V, valid_f, pu32, 1);
        SetDsUserIdSvlanCosPortHashKey(V, stagCos_f, pu32, pvtag->scos);
        SetDsUserIdSvlanCosPortHashKey(V, svlanId_f, pu32, pvtag->svid);
        SetDsUserIdSvlanCosPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsUserIdSvlanCosPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsUserIdSvlanCosPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsUserIdSvlanCosPortHashKey(V, globalSrcPort_f, pu32, pvtag->gport);
        }
    }
    else
    {
        sal_memset(pu32, 0, sizeof(DsEgressXcOamSvlanCosPortHashKey_m));
        SetDsEgressXcOamSvlanCosPortHashKey(V, hashType_f, pu32, EGRESSXCOAMHASHTYPE_SVLANCOSPORT);
        SetDsEgressXcOamSvlanCosPortHashKey(V, valid_f, pu32, 1);
        SetDsEgressXcOamSvlanCosPortHashKey(V, stagCos_f, pu32, pvtag->scos);
        SetDsEgressXcOamSvlanCosPortHashKey(V, svlanId_f, pu32, pvtag->svid);
        SetDsEgressXcOamSvlanCosPortHashKey(V, isLogicPort_f, pu32, pvtag->gport_is_logic);
        SetDsEgressXcOamSvlanCosPortHashKey(V, isLabel_f, pu32, pvtag->gport_is_classid);
        if (!pvtag->gport_is_logic && !pvtag->gport_is_classid)
        {
            SetDsEgressXcOamSvlanCosPortHashKey(V, globalDestPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pvtag->gport));
        }
        else
        {
            SetDsEgressXcOamSvlanCosPortHashKey(V, globalDestPort_f, pu32, pvtag->gport);
        }
    }


    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_port_ipv6(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    ipv6_addr_t               hw_ip6;

    sys_scl_sw_hash_key_ipv6_t* pipv6 = &(psys->u0.hash.u0.ipv6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdIpv6PortHashKey_m));

    if (!pipv6->gport_is_logic && !pipv6->gport_is_classid)
    {
        SetDsUserIdIpv6PortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pipv6->gport));
    }
    else
    {
        SetDsUserIdIpv6PortHashKey(V, globalSrcPort_f, pu32, pipv6->gport);
    }
    SetDsUserIdIpv6PortHashKey(V, isLogicPort_f, pu32, pipv6->gport_is_logic);
    SetDsUserIdIpv6PortHashKey(V, isLabel_f, pu32, pipv6->gport_is_classid);
    SetDsUserIdIpv6PortHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdIpv6PortHashKey(V, hashType0_f, pu32, USERIDHASHTYPE_IPV6PORT);
    SetDsUserIdIpv6PortHashKey(V, hashType1_f, pu32, USERIDHASHTYPE_IPV6PORT);
    SetDsUserIdIpv6PortHashKey(V, hashType2_f, pu32, USERIDHASHTYPE_IPV6PORT);
    SetDsUserIdIpv6PortHashKey(V, hashType3_f, pu32, USERIDHASHTYPE_IPV6PORT);
    SetDsUserIdIpv6PortHashKey(V, valid0_f, pu32, 1);
    SetDsUserIdIpv6PortHashKey(V, valid1_f, pu32, 1);
    SetDsUserIdIpv6PortHashKey(V, valid2_f, pu32, 1);
    SetDsUserIdIpv6PortHashKey(V, valid3_f, pu32, 1);
    SCL_SET_HW_IP6(hw_ip6, pipv6->ip_sa);
    SetDsUserIdIpv6PortHashKey(A, ipSa_f, pu32, hw_ip6);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_ipv6(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    ipv6_addr_t               hw_ip6;

    sys_scl_sw_hash_key_ipv6_t* pipv6 = &(psys->u0.hash.u0.ipv6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdIpv6SaHashKey_m));

    SetDsUserIdIpv6SaHashKey(V, hashType0_f, pu32, USERIDHASHTYPE_IPV6SA);
    SetDsUserIdIpv6SaHashKey(V, hashType1_f, pu32, USERIDHASHTYPE_IPV6SA);
    SetDsUserIdIpv6SaHashKey(V, valid0_f, pu32, 1);
    SetDsUserIdIpv6SaHashKey(V, valid1_f, pu32, 1);
    SetDsUserIdIpv6SaHashKey(V, dsAdIndex_f, pu32, index);
    SCL_SET_HW_IP6(hw_ip6, pipv6->ip_sa);
    SetDsUserIdIpv6SaHashKey(A, ipSa_f, pu32, hw_ip6);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_mac(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    hw_mac_addr_t               hw_mac = { 0 };
    sys_scl_sw_hash_key_mac_t* pmac = &(psys->u0.hash.u0.mac);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdMacHashKey_m));

    SetDsUserIdMacHashKey(V, hashType_f, pu32, USERIDHASHTYPE_MAC);
    SetDsUserIdMacHashKey(V, valid_f, pu32, 1);
    SetDsUserIdMacHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdMacHashKey(V, isMacDa_f, pu32, pmac->use_macda);
    SCL_SET_HW_MAC(hw_mac, pmac->mac);
    SetDsUserIdMacHashKey(A, macSa_f, pu32, hw_mac);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_port_mac(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    hw_mac_addr_t                hw_mac = { 0 };
    sys_scl_sw_hash_key_mac_t * pmac = &(psys->u0.hash.u0.mac);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdMacPortHashKey_m));

    SetDsUserIdMacPortHashKey(V, hashType0_f, pu32, USERIDHASHTYPE_MACPORT);
    SetDsUserIdMacPortHashKey(V, hashType1_f, pu32, USERIDHASHTYPE_MACPORT);
    SetDsUserIdMacPortHashKey(V, valid0_f, pu32, 1);
    SetDsUserIdMacPortHashKey(V, valid1_f, pu32, 1);
    SetDsUserIdMacPortHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdMacPortHashKey(V, isMacDa_f, pu32, pmac->use_macda);
    SCL_SET_HW_MAC(hw_mac, pmac->mac);
    SetDsUserIdMacPortHashKey(A, macSa_f, pu32, hw_mac);
    SetDsUserIdMacPortHashKey(V, isLogicPort_f, pu32, pmac->gport_is_logic);
    SetDsUserIdMacPortHashKey(V, isLabel_f, pu32, pmac->gport_is_classid);
    if (!pmac->gport_is_logic && !pmac->gport_is_classid)
    {
        SetDsUserIdMacPortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pmac->gport));
    }
    else
    {
        SetDsUserIdMacPortHashKey(V, globalSrcPort_f, pu32, pmac->gport);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_port_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_ipv4_t * pipv4 = &(psys->u0.hash.u0.ipv4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdIpv4PortHashKey_m));

    if (!pipv4->gport_is_logic && !pipv4->gport_is_classid)
    {
        SetDsUserIdIpv4PortHashKey(V, globalSrcPort_f, pu32, SYS_MAP_CTC_GPORT_TO_DRV_GPORT(pipv4->gport));
    }
    else
    {
        SetDsUserIdIpv4PortHashKey(V, globalSrcPort_f, pu32, pipv4->gport);
    }
    SetDsUserIdIpv4PortHashKey(V, isLogicPort_f, pu32, pipv4->gport_is_logic);
    SetDsUserIdIpv4PortHashKey(V, isLabel_f, pu32, pipv4->gport_is_classid);
    SetDsUserIdIpv4PortHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdIpv4PortHashKey(V, hashType_f, pu32, USERIDHASHTYPE_IPV4PORT);
    SetDsUserIdIpv4PortHashKey(V, valid_f, pu32, 1);
    SetDsUserIdIpv4PortHashKey(V, ipSa_f, pu32, pipv4->ip_sa);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_sw_hash_key_ipv4_t* pipv4 = &(psys->u0.hash.u0.ipv4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdIpv4SaHashKey_m));

    SetDsUserIdIpv4SaHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdIpv4SaHashKey(V, hashType_f, pu32, USERIDHASHTYPE_IPV4SA);
    SetDsUserIdIpv4SaHashKey(V, valid_f, pu32, 1);
    SetDsUserIdIpv4SaHashKey(V, ipSa_f, pu32, pipv4->ip_sa);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_build_hash_tunnel_ipv4(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_key_t* pt4 = &(psys->u0.hash.u0.tnnl_ipv4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4HashKey_m));

    SetDsUserIdTunnelIpv4HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV4);
    SetDsUserIdTunnelIpv4HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV4);
    SetDsUserIdTunnelIpv4HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv4HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv4HashKey(V, udpSrcPort_f, pu32, 0);
    SetDsUserIdTunnelIpv4HashKey(V, udpDestPort_f, pu32, 0);
    SetDsUserIdTunnelIpv4HashKey(V, layer4Type_f, pu32, pt4->l4_type);
    SetDsUserIdTunnelIpv4HashKey(V, ipSa_f, pu32, pt4->ip_sa);
    SetDsUserIdTunnelIpv4HashKey(V, ipDa_f, pu32, pt4->ip_da);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_tunnel_ipv4_da(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_key_t* pt4 = &(psys->u0.hash.u0.tnnl_ipv4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4DaHashKey_m));

    SetDsUserIdTunnelIpv4DaHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4DaHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4DA);
    SetDsUserIdTunnelIpv4DaHashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4DaHashKey(V, layer4Type_f, pu32, pt4->l4_type);
    SetDsUserIdTunnelIpv4DaHashKey(V, ipDa_f, pu32, pt4->ip_da);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_build_hash_tunnel_ipv4_gre(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_gre_key_t* pgre = &(psys->u0.hash.u0.tnnl_ipv4_gre);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4GreKeyHashKey_m));

    SetDsUserIdTunnelIpv4GreKeyHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV4GREKEY);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV4GREKEY);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, greKey_f, pu32, pgre->gre_key);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, layer4Type_f, pu32, pgre->l4_type);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, ipSa_f, pu32, pgre->ip_sa);
    SetDsUserIdTunnelIpv4GreKeyHashKey(V, ipDa_f, pu32, pgre->ip_da);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_tunnel_ipv4_rpf(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_tunnel_ipv4_rpf_key_t* prpf = &(psys->u0.hash.u0.tnnl_ipv4_rpf);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4RpfHashKey_m));

    SetDsUserIdTunnelIpv4RpfHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4RpfHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4RPF);
    SetDsUserIdTunnelIpv4RpfHashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4RpfHashKey(V, ipSa_f, pu32, prpf->ip_sa);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4UcNvgreMode0HashKey_m));

    SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE0);
    SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4UcNvgreMode1HashKey_m));

    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE1);
    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, ipDaIndex_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_mc_ipv4_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4McNvgreMode0HashKey_m));

    SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4MCNVGREMODE0);
    SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_ipv4_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4NvgreMode1HashKey_m));

    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV4NVGREMODE1);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV4NVGREMODE1);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6UcNvgreMode0HashKey_m));

    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6UcNvgreMode1HashKey_m));

    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(A, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6McNvgreMode0HashKey_m));

    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6McNvgreMode1HashKey_m));

    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(A, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4UcVxlanMode0HashKey_m));

    SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE0);
    SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4UcVxlanMode1HashKey_m));

    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE1);
    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, ipDaIndex_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_mc_ipv4_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4McVxlanMode0HashKey_m));

    SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELIPV4MCVXLANMODE0);
    SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, valid_f, pu32, 1);
    SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_ipv4_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v4);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv4VxlanMode1HashKey_m));

    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV4VXLANMODE1);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV4VXLANMODE1);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6UcVxlanMode0HashKey_m));

    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6UcVxlanMode1HashKey_m));

    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(A, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode0(sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6McVxlanMode0HashKey_m));

    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE0);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode1(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo = &(psys->u0.hash.u0.ol_tnnl_v6);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelIpv6McVxlanMode1HashKey_m));

    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, hashType0_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, hashType1_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, hashType2_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, hashType3_f, pu32, USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid0_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid1_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid2_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid3_f, pu32, 1);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(A, ipDa_f, pu32, pinfo->ip_da);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(A, ipSa_f, pu32, pinfo->ip_sa);
    SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, vni_f, pu32, pinfo->vn_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_trill_uc(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_trill_key_t* pinfo = &(psys->u0.hash.u0.trill);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelTrillUcDecapHashKey_m));

    SetDsUserIdTunnelTrillUcDecapHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelTrillUcDecapHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELTRILLUCDECAP);
    SetDsUserIdTunnelTrillUcDecapHashKey(V, egressNickname_f, pu32, pinfo->egress_nickname);
    SetDsUserIdTunnelTrillUcDecapHashKey(V, ingressNickname_f, pu32, pinfo->ingress_nickname);
    SetDsUserIdTunnelTrillUcDecapHashKey(V, valid_f, pu32, 1);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_build_hash_trill_mc(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_trill_key_t* pinfo = &(psys->u0.hash.u0.trill);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelTrillMcDecapHashKey_m));

    SetDsUserIdTunnelTrillMcDecapHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelTrillMcDecapHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELTRILLMCDECAP);
    SetDsUserIdTunnelTrillMcDecapHashKey(V, egressNickname_f, pu32, pinfo->egress_nickname);
    SetDsUserIdTunnelTrillMcDecapHashKey(V, ingressNickname_f, pu32, pinfo->ingress_nickname);
    SetDsUserIdTunnelTrillMcDecapHashKey(V, valid_f, pu32, 1);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_trill_uc_rpf(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_trill_key_t* pinfo = &(psys->u0.hash.u0.trill);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelTrillUcRpfHashKey_m));

    SetDsUserIdTunnelTrillUcRpfHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelTrillUcRpfHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELTRILLUCRPF);
    SetDsUserIdTunnelTrillUcRpfHashKey(V, ingressNickname_f, pu32, pinfo->ingress_nickname);
    SetDsUserIdTunnelTrillUcRpfHashKey(V, valid_f, pu32, 1);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_trill_mc_rpf(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_trill_key_t* pinfo = &(psys->u0.hash.u0.trill);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelTrillMcRpfHashKey_m));

    SetDsUserIdTunnelTrillMcRpfHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelTrillMcRpfHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELTRILLMCRPF);
    SetDsUserIdTunnelTrillMcRpfHashKey(V, egressNickname_f, pu32, pinfo->egress_nickname);
    SetDsUserIdTunnelTrillMcRpfHashKey(V, ingressNickname_f, pu32, pinfo->ingress_nickname);
    SetDsUserIdTunnelTrillMcRpfHashKey(V, valid_f, pu32, 1);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_hash_trill_mc_adj(uint8 lchip, sys_scl_sw_key_t* psys, void* pu32, uint16 index)
{
    sys_scl_hash_trill_key_t* pinfo = &(psys->u0.hash.u0.trill);

    CTC_PTR_VALID_CHECK(pu32);
    sal_memset(pu32, 0, sizeof(DsUserIdTunnelTrillMcAdjHashKey_m));

    SetDsUserIdTunnelTrillMcAdjHashKey(V, dsAdIndex_f, pu32, index);
    SetDsUserIdTunnelTrillMcAdjHashKey(V, hashType_f, pu32, USERIDHASHTYPE_TUNNELTRILLMCADJ);
    SetDsUserIdTunnelTrillMcAdjHashKey(V, egressNickname_f, pu32, pinfo->egress_nickname);
    SetDsUserIdTunnelTrillMcAdjHashKey(V, globalSrcPort_f, pu32, pinfo->gport);
    SetDsUserIdTunnelTrillMcAdjHashKey(V, valid_f, pu32, 1);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_tcam_vlan_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_vlan, sys_scl_sw_tcam_key_vlan_t* p_key, uint8 act_type)
{
    uint32 flag     = 0;
    uint16 ether_type = 0;
    uint16 ether_type_mask = 0;
    void   * p_data = p_vlan->data_entry;
    void   * p_mask = p_vlan->mask_entry;
    mac_addr_t macda = {0};
    mac_addr_t macda_mask = {0};
    hw_mac_addr_t hw_macda = {0};
    hw_mac_addr_t hw_macda_mask = {0};

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_vlan);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag = p_key->flag;

	sal_memset(macda,0,sizeof(macda));
	sal_memset(macda_mask,0,sizeof(macda_mask));
	sal_memset(hw_macda,0,sizeof(hw_macda));
	sal_memset(hw_macda_mask,0,sizeof(hw_macda_mask));
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN))
    {
        SetDsUserId0MacKey160(V, svlanId_f, p_data, p_key->svlan);
        SetDsUserId0MacKey160(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS))
    {
        SetDsUserId0MacKey160(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsUserId0MacKey160(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_CFI))
    {
        SetDsUserId0MacKey160(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsUserId0MacKey160(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN))
    {
        /*macda[47]:cfi [46:44]:cos [43:32]:vlanid*/
        macda[0] |= p_key->cvlan >> 8;
        macda_mask[0] |= p_key->cvlan_mask >> 8;

        macda[1] |= p_key->cvlan & 0xFF;
        macda_mask[1] |= p_key->cvlan_mask & 0xFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS))
    {
        /*macda[47]:cfi [46:44]:cos [43:32]:vlanid*/
        macda[0] |= p_key->ctag_cos << 4;
        macda_mask[0] |= p_key->ctag_cos_mask << 4;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_CFI))
    {
        /*macda[47]:cfi [46:44]:cos [43:32]:vlanid*/
        macda[0] |= p_key->ctag_cfi << 7;
        macda_mask[0] |= 1 << 7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID))
    {
        /* [31:0]:customer id */
        macda[2] = (p_key->customer_id >> 24) & 0xFF;
        macda[3] = (p_key->customer_id >> 16) & 0xFF;
        macda[4] = (p_key->customer_id >> 8) & 0xFF;
        macda[5] = (p_key->customer_id) & 0xFF;

        macda_mask[2] = (p_key->customer_id_mask >> 24) & 0xFF;
        macda_mask[3] = (p_key->customer_id_mask >> 16) & 0xFF;
        macda_mask[4] = (p_key->customer_id_mask >> 8) & 0xFF;
        macda_mask[5] = (p_key->customer_id_mask) & 0xFF;

        /*etherType[1]: customerIdValid*/
        ether_type = 2;
        ether_type_mask = 2;
    }

    SCL_SET_HW_MAC(hw_macda,  macda)
    SCL_SET_HW_MAC(hw_macda_mask,  macda_mask)
    SetDsUserId0MacKey160(A, macDa_f, p_data, hw_macda);
    SetDsUserId0MacKey160(A, macDa_f, p_mask, hw_macda_mask);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        /*etherType[0]: cvlanIdValid*/
        ether_type |= p_key->ctag_valid;
        ether_type_mask |= 1;
    }

    SetDsUserId0MacKey160(V, etherType_f, p_data, ether_type);
    SetDsUserId0MacKey160(V, etherType_f, p_mask, ether_type_mask);

    SetDsUserId0MacKey160(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0MacKey160(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0MacKey160(V, globalPortType_f, p_data, p_info->gport_type);
    SetDsUserId0MacKey160(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0MacKey160(V, isVlanKey_f , p_data, 1);
    SetDsUserId0MacKey160(V, isVlanKey_f , p_mask, 1);

    SetDsUserId0MacKey160(V, aclQosKeyType_f, p_data, DRV_TCAMKEYTYPE_MACKEY160);
    SetDsUserId0MacKey160(V, aclQosKeyType_f, p_mask, 0x3);

    SetDsUserId0MacKey160(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0MacKey160(V, sclKeyType_f, p_mask, 3);

    SetDsUserId0MacKey160(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0MacKey160(V, globalPort_f, p_mask, p_info->gport_mask);

/*
   layer2Type_f (vlan_num)
   macSa_f
 */

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_build_tcam_mac_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_mac, sys_scl_sw_tcam_key_mac_t* p_key, uint8 act_type)
{
    uint32     flag     = 0;
    void       * p_data = p_mac->data_entry;
    void       * p_mask = p_mac->mask_entry;
    hw_mac_addr_t hw_mac   = { 0 };
    uint8 gport_type = 0;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_mac);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag = p_key->flag;
    gport_type = p_info->gport_type;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN))
    {
        SetDsUserId0MacKey160(V, svlanId_f, p_data, p_key->svlan);
        SetDsUserId0MacKey160(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS))
    {
        SetDsUserId0MacKey160(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsUserId0MacKey160(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI))
    {
        SetDsUserId0MacKey160(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsUserId0MacKey160(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN))
    {
        SetDsUserId0MacKey160(V, svlanId_f, p_data, p_key->cvlan);
        SetDsUserId0MacKey160(V, svlanId_f, p_mask, p_key->cvlan_mask);
        gport_type = 2 + p_info->gport_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS))
    {
        SetDsUserId0MacKey160(V, stagCos_f, p_data, p_key->ctag_cos);
        SetDsUserId0MacKey160(V, stagCos_f, p_mask, p_key->ctag_cos_mask);
        gport_type = 2 + p_info->gport_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI))
    {
        SetDsUserId0MacKey160(V, stagCfi_f, p_data, p_key->ctag_cfi);
        SetDsUserId0MacKey160(V, stagCfi_f, p_mask, 1);
        gport_type = 2 + p_info->gport_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_VALID))
    {
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_VALID))
    {
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsUserId0MacKey160(V, svlanIdValid_f, p_mask, 1);
        gport_type = 2 + p_info->gport_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L2_TYPE))
    {
        SetDsUserId0MacKey160(V, layer2Type_f, p_data, p_key->l2_type);
        SetDsUserId0MacKey160(V, layer2Type_f, p_mask, p_key->l2_type_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsUserId0MacKey160(A, macSa_f, p_data, hw_mac);
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsUserId0MacKey160(A, macSa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsUserId0MacKey160(A, macDa_f, p_data, hw_mac);
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsUserId0MacKey160(A, macDa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_ETH_TYPE))
    {
        SetDsUserId0MacKey160(V, etherType_f, p_data, p_key->eth_type);
        SetDsUserId0MacKey160(V, etherType_f, p_mask, p_key->eth_type_mask);
    }


    SetDsUserId0MacKey160(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0MacKey160(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0MacKey160(V, globalPortType_f, p_data, gport_type);
    SetDsUserId0MacKey160(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0MacKey160(V, isVlanKey_f , p_data, 0);
    SetDsUserId0MacKey160(V, isVlanKey_f , p_mask, 1);

    SetDsUserId0MacKey160(V, aclQosKeyType_f, p_data, DRV_TCAMKEYTYPE_MACKEY160);
    SetDsUserId0MacKey160(V, aclQosKeyType_f, p_mask, 0x3);

    SetDsUserId0MacKey160(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0MacKey160(V, sclKeyType_f, p_mask, 3);

    SetDsUserId0MacKey160(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0MacKey160(V, globalPort_f, p_mask, p_info->gport_mask);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_build_tcam_mac_key_double(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_mac, sys_scl_sw_tcam_key_mac_t* p_key, uint8 act_type)
{
    uint32     flag     = 0;
    void       * p_data = p_mac->data_entry;
    void       * p_mask = p_mac->mask_entry;
    hw_mac_addr_t hw_mac   = { 0 };

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_mac);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag = p_key->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN))
    {
        SetDsUserId0L3Key320(V, svlanId_f, p_data, p_key->svlan);
        SetDsUserId0L3Key320(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS))
    {
        SetDsUserId0L3Key320(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsUserId0L3Key320(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI))
    {
        SetDsUserId0L3Key320(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsUserId0L3Key320(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN))
    {
        SetDsUserId0L3Key320(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsUserId0L3Key320(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS))
    {
        SetDsUserId0L3Key320(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsUserId0L3Key320(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI))
    {
        SetDsUserId0L3Key320(V, ctagCfi_f, p_data, p_key->ctag_cfi);
        SetDsUserId0L3Key320(V, ctagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_VALID))
    {
        SetDsUserId0L3Key320(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsUserId0L3Key320(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_VALID))
    {
        SetDsUserId0L3Key320(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsUserId0L3Key320(V, cvlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L2_TYPE))
    {
        SetDsUserId0L3Key320(V, layer2Type_f, p_data, p_key->l2_type);
        SetDsUserId0L3Key320(V, layer2Type_f, p_mask, p_key->l2_type_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsUserId0L3Key320(A, macSa_f, p_data, hw_mac);
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsUserId0L3Key320(A, macSa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsUserId0L3Key320(A, macDa_f, p_data, hw_mac);
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsUserId0L3Key320(A, macDa_f, p_mask, hw_mac);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_ETH_TYPE))
    {
        SetDsUserId0L3Key320(V, u1_g10_etherType_f, p_data, p_key->eth_type);
        SetDsUserId0L3Key320(V, u1_g10_etherType_f, p_mask, p_key->eth_type_mask);
    }

    SetDsUserId0L3Key320(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0L3Key320(V, globalPort_f, p_mask, p_info->gport_mask);

    SetDsUserId0L3Key320(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0L3Key320(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0L3Key320(V, globalPortType_f, p_data, p_info->gport_type);
    SetDsUserId0L3Key320(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0L3Key320(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsUserId0L3Key320(V, aclQosKeyType0_f, p_mask, 0x3);

    SetDsUserId0L3Key320(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsUserId0L3Key320(V, aclQosKeyType1_f, p_mask, 0x3);

    SetDsUserId0L3Key320(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0L3Key320(V, sclKeyType_f, p_mask, 3);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_build_tcam_ipv4_key_single(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_ipv4, sys_scl_sw_tcam_key_ipv4_t* p_key, uint8 act_type)
{
    uint32 flag;
    uint32 sub_flag;
    void   * p_data = p_ipv4->data_entry;
    void   * p_mask = p_ipv4->mask_entry;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_ipv4);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag      = p_key->flag;
    sub_flag  = p_key->sub_flag;

    SetDsUserId0L3Key160(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0L3Key160(V, globalPort_f, p_mask, p_info->gport_mask);

    SetDsUserId0L3Key160(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0L3Key160(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0L3Key160(V, globalPortType_f, p_data, p_info->gport_type);
    SetDsUserId0L3Key160(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0L3Key160(V, aclQosKeyType_f, p_data, DRV_TCAMKEYTYPE_L3KEY160);
    SetDsUserId0L3Key160(V, aclQosKeyType_f, p_mask, 0x3);

    SetDsUserId0L3Key160(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0L3Key160(V, sclKeyType_f, p_mask, 3);


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L2_TYPE))
    {
        SetDsUserId0L3Key160(V, layer2Type_f, p_data, p_key->l2_type);
        SetDsUserId0L3Key160(V, layer2Type_f, p_mask, p_key->l2_type_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE))
    {
        SetDsUserId0L3Key160(V, layer3Type_f, p_data, p_key->l3_type);
        SetDsUserId0L3Key160(V, layer3Type_f, p_mask, p_key->l3_type_mask);
    }
    switch (p_key->unique_l3_type)
    {
    case CTC_PARSER_L3_TYPE_IPV4:
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
        {
            SetDsUserId0L3Key160(V, u1_g1_ipDa_f, p_data, p_key->u0.ip.ip_da);
            SetDsUserId0L3Key160(V, u1_g1_ipDa_f, p_mask, p_key->u0.ip.ip_da_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
        {
            SetDsUserId0L3Key160(V, u1_g1_ipSa_f, p_data, p_key->u0.ip.ip_sa);
            SetDsUserId0L3Key160(V, u1_g1_ipSa_f, p_mask, p_key->u0.ip.ip_sa_mask);
        }


        if ((CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP))
            || (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE)))
        {
            SetDsUserId0L3Key160(V, u1_g1_dscp_f, p_data, p_key->u0.ip.dscp);
            SetDsUserId0L3Key160(V, u1_g1_dscp_f, p_mask, p_key->u0.ip.dscp_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG))
        {
            SetDsUserId0L3Key160(V, u1_g1_fragInfo_f, p_data, p_key->u0.ip.frag_info);
            SetDsUserId0L3Key160(V, u1_g1_fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_OPTION))
        {
            SetDsUserId0L3Key160(V, u1_g1_ipOptions_f, p_data, p_key->u0.ip.ip_option);
            SetDsUserId0L3Key160(V, u1_g1_ipOptions_f, p_mask, 1);
        }

        /*ipv4key.routed_packet always = 0*/
        if (p_key->u0.ip.flag_l4_type)
        {
            SetDsUserId0L3Key160(V, u1_g1_layer4Type_f, p_data, p_key->u0.ip.l4_type);
            SetDsUserId0L3Key160(V, u1_g1_layer4Type_f, p_mask, p_key->u0.ip.l4_type_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ECN))
        {
            SetDsUserId0L3Key160(V, u1_g1_ecn_f, p_data, p_key->u0.ip.ecn);
            SetDsUserId0L3Key160(V, u1_g1_ecn_f, p_mask, p_key->u0.ip.ecn_mask);
        }

        if (p_key->u0.ip.flag_l4info_mapped)
        {
            SetDsUserId0L3Key160(V, u1_g1_l4InfoMapped_f, p_data, p_key->u0.ip.l4info_mapped);
            SetDsUserId0L3Key160(V, u1_g1_l4InfoMapped_f, p_mask, p_key->u0.ip.l4info_mapped_mask);
        }

        if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_CODE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_IGMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsUserId0L3Key160(V, u1_g1_l4SourcePort_f, p_data, p_key->u0.ip.l4_src_port);
            SetDsUserId0L3Key160(V, u1_g1_l4SourcePort_f, p_mask, p_key->u0.ip.l4_src_port_mask);
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsUserId0L3Key160(V, u1_g1_l4DestPort_f, p_data, p_key->u0.ip.l4_dst_port);
            SetDsUserId0L3Key160(V, u1_g1_l4DestPort_f, p_mask, p_key->u0.ip.l4_dst_port_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_ARP:
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
        {
            SetDsUserId0L3Key160(V, u1_g2_arpOpCode_f, p_data, p_key->u0.arp.op_code);
            SetDsUserId0L3Key160(V, u1_g2_arpOpCode_f, p_mask, p_key->u0.arp.op_code_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
        {
            SetDsUserId0L3Key160(V, u1_g2_protocolType_f, p_data, p_key->u0.arp.protocol_type);
            SetDsUserId0L3Key160(V, u1_g2_protocolType_f, p_mask, p_key->u0.arp.protocol_type_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
        {
            SetDsUserId0L3Key160(V, u1_g2_targetIp_f, p_data, p_key->u0.arp.target_ip);
            SetDsUserId0L3Key160(V, u1_g2_targetIp_f, p_mask, p_key->u0.arp.target_ip_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
        {
            SetDsUserId0L3Key160(V, u1_g2_senderIp_f, p_data, p_key->u0.arp.sender_ip);
            SetDsUserId0L3Key160(V, u1_g2_senderIp_f, p_mask, p_key->u0.arp.sender_ip_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_NONE:
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ETH_TYPE))
        {
            SetDsUserId0L3Key160(V, u1_g10_etherType_f, p_data, p_key->u0.other.eth_type);
            SetDsUserId0L3Key160(V, u1_g10_etherType_f, p_mask, p_key->u0.other.eth_type_mask);
        }
        break;
    default:
        break;
#if 0
    case CTC_PARSER_L3_TYPE_MPLS:
    case CTC_PARSER_L3_TYPE_MPLS_MCAST:
        u1_g3_labelNum_f
        u1_g3_mplsLabel2_f
        u1_g3_mplsLabel1_f
            u1_g3_mplsLabel0_f
        break;
    case CTC_PARSER_L3_TYPE_ETHER_OAM:
        u1_g7_etherOamLevel_f
        u1_g7_etherOamVersion_f
        u1_g7_etherOamOpCode_f
            u1_g7_isY1731Oam_f
        break;
    case CTC_PARSER_L3_TYPE_SLOW_PROTO:
        u1_g8_slowProtocolSubType_f
        u1_g8_slowProtocolFlags_f
            u1_g8_slowProtocolCode_f
        break;
    case CTC_PARSER_L3_TYPE_PTP:
        u1_g9_ptpVersion_f
            u1_g9_ptpMessageType_f
        break;
    case CTC_PARSER_L3_TYPE_FCOE:
        u1_g4_fcoeDid_f
            u1_g4_fcoeSid_f
        break;
#endif
    }


/*
   isMetadata_f
   udf_f
 */

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_build_tcam_ipv4_key_double(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_ipv4, sys_scl_sw_tcam_key_ipv4_t* p_key, uint8 act_type)
{
    uint32     flag;
    uint32     sub_flag;
    hw_mac_addr_t hw_mac   = { 0 };
    void       * p_data = p_ipv4->data_entry;
    void       * p_mask = p_ipv4->mask_entry;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_ipv4);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag      = p_key->flag;
    sub_flag  = p_key->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsUserId0L3Key320(A, macDa_f, p_data, hw_mac);

        SCL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsUserId0L3Key320(A, macDa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsUserId0L3Key320(A, macSa_f, p_data, hw_mac);

        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsUserId0L3Key320(A, macSa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CVLAN))
    {
        SetDsUserId0L3Key320(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsUserId0L3Key320(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_COS))
    {
        SetDsUserId0L3Key320(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsUserId0L3Key320(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_CFI))
    {
        SetDsUserId0L3Key320(V, ctagCfi_f, p_data, p_key->ctag_cfi);
        SetDsUserId0L3Key320(V, ctagCfi_f, p_mask, 1);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_SVLAN))
    {
        SetDsUserId0L3Key320(V, svlanId_f, p_data, p_key->svlan);
        SetDsUserId0L3Key320(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_COS))
    {
        SetDsUserId0L3Key320(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsUserId0L3Key320(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_CFI))
    {
        SetDsUserId0L3Key320(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsUserId0L3Key320(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_VALID))
    {
        SetDsUserId0L3Key320(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsUserId0L3Key320(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_VALID))
    {
        SetDsUserId0L3Key320(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsUserId0L3Key320(V, cvlanIdValid_f, p_mask, 1);
    }

    SetDsUserId0L3Key320(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0L3Key320(V, globalPort_f, p_mask, p_info->gport_mask);

    SetDsUserId0L3Key320(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0L3Key320(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0L3Key320(V, globalPortType_f, p_data, p_info->gport_type);
    SetDsUserId0L3Key320(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0L3Key320(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsUserId0L3Key320(V, aclQosKeyType0_f, p_mask, 0x3);

    SetDsUserId0L3Key320(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_MACL3KEY320);
    SetDsUserId0L3Key320(V, aclQosKeyType1_f, p_mask, 0x3);

    SetDsUserId0L3Key320(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0L3Key320(V, sclKeyType_f, p_mask, 3);


    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L2_TYPE))
    {
        SetDsUserId0L3Key320(V, layer2Type_f, p_data, p_key->l2_type);
        SetDsUserId0L3Key320(V, layer2Type_f, p_mask, p_key->l2_type_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE))
    {
        SetDsUserId0L3Key320(V, layer3Type_f, p_data, p_key->l3_type);
        SetDsUserId0L3Key320(V, layer3Type_f, p_mask, p_key->l3_type_mask);
    }
    switch (p_key->unique_l3_type)
    {
    case CTC_PARSER_L3_TYPE_IPV4:
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
        {
            SetDsUserId0L3Key320(V, u1_g1_ipDa_f, p_data, p_key->u0.ip.ip_da);
            SetDsUserId0L3Key320(V, u1_g1_ipDa_f, p_mask, p_key->u0.ip.ip_da_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
        {
            SetDsUserId0L3Key320(V, u1_g1_ipSa_f, p_data, p_key->u0.ip.ip_sa);
            SetDsUserId0L3Key320(V, u1_g1_ipSa_f, p_mask, p_key->u0.ip.ip_sa_mask);
        }


        if ((CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP))
            || (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE)))
        {
            SetDsUserId0L3Key320(V, u1_g1_dscp_f, p_data, p_key->u0.ip.dscp);
            SetDsUserId0L3Key320(V, u1_g1_dscp_f, p_mask, p_key->u0.ip.dscp_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG))
        {
            SetDsUserId0L3Key320(V, u1_g1_fragInfo_f, p_data, p_key->u0.ip.frag_info);
            SetDsUserId0L3Key320(V, u1_g1_fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_OPTION))
        {
            SetDsUserId0L3Key320(V, u1_g1_ipOptions_f, p_data, p_key->u0.ip.ip_option);
            SetDsUserId0L3Key320(V, u1_g1_ipOptions_f, p_mask, 1);
        }

        /*ipv4key.routed_packet always = 0*/
        if (p_key->u0.ip.flag_l4_type)
        {
            SetDsUserId0L3Key320(V, u1_g1_l4CompressType_f, p_data, p_key->u0.ip.l4_compress_type);
            SetDsUserId0L3Key320(V, u1_g1_l4CompressType_f, p_mask, p_key->u0.ip.l4_compress_type_mask);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ECN))
        {
            SetDsUserId0L3Key320(V, u1_g1_ecn_f, p_data, p_key->u0.ip.ecn);
            SetDsUserId0L3Key320(V, u1_g1_ecn_f, p_mask, p_key->u0.ip.ecn_mask);
        }

        if (p_key->u0.ip.flag_l4info_mapped)
        {
            SetDsUserId0L3Key320(V, u1_g1_l4InfoMapped_f, p_data, p_key->u0.ip.l4info_mapped);
            SetDsUserId0L3Key320(V, u1_g1_l4InfoMapped_f, p_mask, p_key->u0.ip.l4info_mapped_mask);
        }

        if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_CODE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_IGMP_TYPE)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsUserId0L3Key320(V, u1_g1_l4SourcePort_f, p_data, p_key->u0.ip.l4_src_port);
            SetDsUserId0L3Key320(V, u1_g1_l4SourcePort_f, p_mask, p_key->u0.ip.l4_src_port_mask);
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
            (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)))
        {
            SetDsUserId0L3Key320(V, u1_g1_l4DestPort_f, p_data, p_key->u0.ip.l4_dst_port);
            SetDsUserId0L3Key320(V, u1_g1_l4DestPort_f, p_mask, p_key->u0.ip.l4_dst_port_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_ARP:
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
        {
            SetDsUserId0L3Key320(V, u1_g2_arpOpCode_f, p_data, p_key->u0.arp.op_code);
            SetDsUserId0L3Key320(V, u1_g2_arpOpCode_f, p_mask, p_key->u0.arp.op_code_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
        {
            SetDsUserId0L3Key320(V, u1_g2_protocolType_f, p_data, p_key->u0.arp.protocol_type);
            SetDsUserId0L3Key320(V, u1_g2_protocolType_f, p_mask, p_key->u0.arp.protocol_type_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
        {
            SetDsUserId0L3Key320(V, u1_g2_targetIp_f, p_data, p_key->u0.arp.target_ip);
            SetDsUserId0L3Key320(V, u1_g2_targetIp_f, p_mask, p_key->u0.arp.target_ip_mask);
        }
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
        {
            SetDsUserId0L3Key320(V, u1_g2_senderIp_f, p_data, p_key->u0.arp.sender_ip);
            SetDsUserId0L3Key320(V, u1_g2_senderIp_f, p_mask, p_key->u0.arp.sender_ip_mask);
        }
        break;
    case CTC_PARSER_L3_TYPE_NONE:
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ETH_TYPE))
        {
            SetDsUserId0L3Key320(V, u1_g10_etherType_f, p_data, p_key->u0.other.eth_type);
            SetDsUserId0L3Key320(V, u1_g10_etherType_f, p_mask, p_key->u0.other.eth_type_mask);
        }
        break;
    default:
        break;
#if 0
    case CTC_PARSER_L3_TYPE_MPLS:
    case CTC_PARSER_L3_TYPE_MPLS_MCAST:
        u1_g3_labelNum_f
        u1_g3_mplsLabel2_f
        u1_g3_mplsLabel1_f
            u1_g3_mplsLabel0_f
        break;
    case CTC_PARSER_L3_TYPE_ETHER_OAM:
        u1_g7_etherOamLevel_f
        u1_g7_etherOamVersion_f
        u1_g7_etherOamOpCode_f
            u1_g7_isY1731Oam_f
        break;
    case CTC_PARSER_L3_TYPE_SLOW_PROTO:
        u1_g8_slowProtocolSubType_f
        u1_g8_slowProtocolFlags_f
            u1_g8_slowProtocolCode_f
        break;
    case CTC_PARSER_L3_TYPE_PTP:
        u1_g9_ptpVersion_f
            u1_g9_ptpMessageType_f
        break;
    case CTC_PARSER_L3_TYPE_FCOE:
        u1_g4_fcoeDid_f
            u1_g4_fcoeSid_f
        break;
#endif
    }


/*
   isMetadata_f
   udf_f
 */

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_build_tcam_ipv6_key(uint8 lchip, drv_scl_group_info_t* p_info, tbl_entry_t* p_ipv6, sys_scl_sw_tcam_key_ipv6_t* p_key, uint8 act_type)
{
    uint32      flag;
    uint32      sub_flag;
    ipv6_addr_t hw_ip6;
    hw_mac_addr_t  hw_mac   = { 0 };
    void        * p_data = p_ipv6->data_entry;
    void        * p_mask = p_ipv6->mask_entry;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(p_ipv6);
    CTC_PTR_VALID_CHECK(p_key);

    SYS_SCL_DBG_FUNC();

    flag     = p_key->flag;
    sub_flag = p_key->sub_flag;
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_DA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_da);
        SetDsUserId0Ipv6Key640(A, macDa_f, p_data, hw_mac);

        SCL_SET_HW_MAC(hw_mac, p_key->mac_da_mask);
        SetDsUserId0Ipv6Key640(A, macDa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_SA))
    {
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa);
        SetDsUserId0Ipv6Key640(A, macSa_f, p_data, hw_mac);
        SCL_SET_HW_MAC(hw_mac, p_key->mac_sa_mask);
        SetDsUserId0Ipv6Key640(A, macSa_f, p_mask, hw_mac);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CVLAN))
    {
        SetDsUserId0Ipv6Key640(V, cvlanId_f, p_data, p_key->cvlan);
        SetDsUserId0Ipv6Key640(V, cvlanId_f, p_mask, p_key->cvlan_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_COS))
    {
        SetDsUserId0Ipv6Key640(V, ctagCos_f, p_data, p_key->ctag_cos);
        SetDsUserId0Ipv6Key640(V, ctagCos_f, p_mask, p_key->ctag_cos_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_CFI))
    {
        SetDsUserId0Ipv6Key640(V, ctagCfi_f, p_data, p_key->ctag_cfi);
        SetDsUserId0Ipv6Key640(V, ctagCfi_f, p_mask, 1);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_SVLAN))
    {
        SetDsUserId0Ipv6Key640(V, svlanId_f, p_data, p_key->svlan);
        SetDsUserId0Ipv6Key640(V, svlanId_f, p_mask, p_key->svlan_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_COS))
    {
        SetDsUserId0Ipv6Key640(V, stagCos_f, p_data, p_key->stag_cos);
        SetDsUserId0Ipv6Key640(V, stagCos_f, p_mask, p_key->stag_cos_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_CFI))
    {
        SetDsUserId0Ipv6Key640(V, stagCfi_f, p_data, p_key->stag_cfi);
        SetDsUserId0Ipv6Key640(V, stagCfi_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_VALID))
    {
        SetDsUserId0Ipv6Key640(V, svlanIdValid_f, p_data, p_key->stag_valid);
        SetDsUserId0Ipv6Key640(V, svlanIdValid_f, p_mask, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_VALID))
    {
        SetDsUserId0Ipv6Key640(V, cvlanIdValid_f, p_data, p_key->ctag_valid);
        SetDsUserId0Ipv6Key640(V, cvlanIdValid_f, p_mask, 1);
    }

    SetDsUserId0Ipv6Key640(V, globalPort_f, p_data, p_info->gport);
    SetDsUserId0Ipv6Key640(V, globalPort_f, p_mask, p_info->gport_mask);

    SetDsUserId0Ipv6Key640(V, aclLabel_f, p_data, p_info->class_id_data);
    SetDsUserId0Ipv6Key640(V, aclLabel_f, p_mask, p_info->class_id_mask);

    SetDsUserId0Ipv6Key640(V, globalPortType_f, p_data, p_info->gport_type);
    SetDsUserId0Ipv6Key640(V, globalPortType_f, p_mask, p_info->gport_type_mask);

    SetDsUserId0Ipv6Key640(V, aclQosKeyType0_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType0_f, p_mask, 0x3);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType1_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType1_f, p_mask, 0x3);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType2_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType2_f, p_mask, 0x3);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType3_f, p_data, DRV_TCAMKEYTYPE_IPV6KEY640);
    SetDsUserId0Ipv6Key640(V, aclQosKeyType3_f, p_mask, 0x3);

    SetDsUserId0Ipv6Key640(V, sclKeyType_f, p_data, p_gg_scl_master[lchip]->ad_type[act_type]);
    SetDsUserId0Ipv6Key640(V, sclKeyType_f, p_mask, 3);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
    {
        SCL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_da);
        SetDsUserId0Ipv6Key640(A, ipDa_f, p_data, hw_ip6);
        SCL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_da_mask);
        SetDsUserId0Ipv6Key640(A, ipDa_f, p_mask, hw_ip6);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
    {
        SCL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_sa);
        SetDsUserId0Ipv6Key640(A, ipSa_f, p_data, hw_ip6);
        SCL_SET_HW_IP6(hw_ip6, p_key->u0.ip.ip_sa_mask);
        SetDsUserId0Ipv6Key640(A, ipSa_f, p_mask, hw_ip6);
    }


    if ((CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP))
        || (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE)))
    {
        SetDsUserId0Ipv6Key640(V, dscp_f, p_data, p_key->u0.ip.dscp);
        SetDsUserId0Ipv6Key640(V, dscp_f, p_mask, p_key->u0.ip.dscp_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_FRAG))
    {
        SetDsUserId0Ipv6Key640(V, fragInfo_f, p_data, p_key->u0.ip.frag_info);
        SetDsUserId0Ipv6Key640(V, fragInfo_f, p_mask, p_key->u0.ip.frag_info_mask);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_OPTION))
    {
        SetDsUserId0Ipv6Key640(V, ipOptions_f, p_data, p_key->u0.ip.ip_option);
        SetDsUserId0Ipv6Key640(V, ipOptions_f, p_mask, 1);
    }

    /*ipv4key.routed_packet always = 0*/
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE))
    {
        SetDsUserId0Ipv6Key640(V, layer4Type_f, p_data, p_key->u0.ip.l4_type);
        SetDsUserId0Ipv6Key640(V, layer4Type_f, p_mask, p_key->u0.ip.l4_type_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L2_TYPE))
    {
        SetDsUserId0Ipv6Key640(V, layer2Type_f, p_data, p_key->l2_type);
        SetDsUserId0Ipv6Key640(V, layer2Type_f, p_mask, p_key->l2_type_mask);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        SetDsUserId0Ipv6Key640(V, ipv6FlowLabel_f, p_data, p_key->flow_label);
        SetDsUserId0Ipv6Key640(V, ipv6FlowLabel_f, p_mask, p_key->flow_label_mask);
    }
    /* ipHeaderError_f */

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_ECN))
    {
        SetDsUserId0Ipv6Key640(V, ecn_f, p_data, p_key->u0.ip.ecn);
        SetDsUserId0Ipv6Key640(V, ecn_f, p_mask, p_key->u0.ip.ecn_mask);
    }

    if (p_key->u0.ip.flag_l4info_mapped)
    {
        SetDsUserId0Ipv6Key640(V, l4InfoMapped_f, p_data, p_key->u0.ip.l4info_mapped);
        SetDsUserId0Ipv6Key640(V, l4InfoMapped_f, p_mask, p_key->u0.ip.l4info_mapped_mask);
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_TYPE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_CODE)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI)))
    {
        SetDsUserId0Ipv6Key640(V, l4SourcePort_f, p_data, p_key->u0.ip.l4_src_port);
        SetDsUserId0Ipv6Key640(V, l4SourcePort_f, p_mask, p_key->u0.ip.l4_src_port_mask);
    }

    if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY)) ||
        (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI)))
    {
        SetDsUserId0Ipv6Key640(V, l4DestPort_f, p_data, p_key->u0.ip.l4_dst_port);
        SetDsUserId0Ipv6Key640(V, l4DestPort_f, p_mask, p_key->u0.ip.l4_dst_port_mask);
    }
    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_vlan_tag_op_translate(uint8 lchip, uint8 op_in, uint8* op_out, uint8* mode_out)
{
    if (CTC_VLAN_TAG_OP_REP_OR_ADD == op_in)
    {
        *mode_out = 1;
    }
    else
    {
        *mode_out = 0;
    }

    switch (op_in)
    {
    case CTC_VLAN_TAG_OP_NONE:
    case CTC_VLAN_TAG_OP_VALID:
        *op_out = 0;
        break;
    case CTC_VLAN_TAG_OP_REP:
    case CTC_VLAN_TAG_OP_REP_OR_ADD:
        *op_out = 1;
        break;
    case CTC_VLAN_TAG_OP_ADD:
        *op_out = 2;
        break;
    case CTC_VLAN_TAG_OP_DEL:
        *op_out = 3;
        break;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_egs_action(uint8 lchip, void* ds, sys_scl_sw_egs_action_t* p_action)
{
    uint32 flag;
    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(ds);

    SYS_SCL_DBG_FUNC();

    flag = p_action->flag;

/*
   left:
    logId_f
    logEn_f
    userIdExceptionEn_f
    svlanTpidIndexEn_f
    ctagAddMode_f
    isidValid_f
    svlanTpidIndex_f
    vlanXlatePortIsolateEn_f
    discardKnownUcast_f
    discardUnknownUcast_f
    discardKnownMcast_f
    discardUnknownMcast_f
    discardBcast_f
 */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        SET_VLANXC(V, discard_f, ds, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        SET_VLANXC(V, statsPtr_f, ds, p_action->chip.stats_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 op = 0;
        uint8 mo;
        SET_VLANXC(V, userSVlanId_f, ds, p_action->svid);
        SET_VLANXC(V, userSCos_f, ds, p_action->scos);
        SET_VLANXC(V, userSCfi_f, ds, p_action->scfi);
        SET_VLANXC(V, userCVlanId_f, ds, p_action->cvid);
        SET_VLANXC(V, userCCos_f, ds, p_action->ccos);
        SET_VLANXC(V, userCCfi_f, ds, p_action->ccfi);
        SET_VLANXC(V, sVlanIdAction_f, ds, p_action->svid_sl);
        SET_VLANXC(V, cVlanIdAction_f, ds, p_action->cvid_sl);
        SET_VLANXC(V, sCosAction_f, ds, p_action->scos_sl);
        SET_VLANXC(V, cCosAction_f, ds, p_action->ccos_sl);

        _sys_goldengate_scl_vlan_tag_op_translate(lchip, p_action->stag_op, &op, &mo);
        SET_VLANXC(V, sTagAction_f, ds, op);
        SET_VLANXC(V, stagModifyMode_f, ds, mo);

        _sys_goldengate_scl_vlan_tag_op_translate(lchip, p_action->ctag_op, &op, &mo);
        SET_VLANXC(V, cTagAction_f, ds, op);
        SET_VLANXC(V, ctagModifyMode_f, ds, mo);

        SET_VLANXC(V, sCfiAction_f, ds, p_action->scfi_sl);
        SET_VLANXC(V, cCfiAction_f, ds, p_action->ccfi_sl);
        SET_VLANXC(V, svlanXlateValid_f, ds, p_action->stag_op ? 1 : 0);
        SET_VLANXC(V, cvlanXlateValid_f, ds, p_action->ctag_op ? 1 : 0);
        SET_VLANXC(V, svlanTpidIndexEn_f, ds, 1 );
        SET_VLANXC(V, svlanTpidIndex_f, ds, p_action->tpid_index);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_vlan_edit(uint8 lchip, void* p_ds_edit, sys_scl_sw_vlan_edit_t* p_vlan_edit)
{
    uint8 op = 0;
    uint8 mo;
    uint8 st = 0;

    _sys_goldengate_scl_vlan_tag_op_translate(lchip, p_vlan_edit->stag_op, &op, &mo);
    SetDsVlanActionProfile(V, sTagAction_f, p_ds_edit, op);
    SetDsVlanActionProfile(V, stagModifyMode_f, p_ds_edit, mo);
    _sys_goldengate_scl_vlan_tag_op_translate(lchip, p_vlan_edit->ctag_op, &op, &mo);
    SetDsVlanActionProfile(V, cTagAction_f, p_ds_edit, op);
    SetDsVlanActionProfile(V, ctagModifyMode_f, p_ds_edit, mo);

    SetDsVlanActionProfile(V, sVlanIdAction_f, p_ds_edit, p_vlan_edit->svid_sl);
    SetDsVlanActionProfile(V, sCosAction_f, p_ds_edit, p_vlan_edit->scos_sl);
    SetDsVlanActionProfile(V, sCfiAction_f, p_ds_edit, p_vlan_edit->scfi_sl);

    SetDsVlanActionProfile(V, cVlanIdAction_f, p_ds_edit, p_vlan_edit->cvid_sl);
    SetDsVlanActionProfile(V, cCosAction_f, p_ds_edit, p_vlan_edit->ccos_sl);
    SetDsVlanActionProfile(V, cCfiAction_f, p_ds_edit, p_vlan_edit->ccfi_sl);
    SetDsVlanActionProfile(V, svlanTpidIndexEn_f, p_ds_edit, 1);
    SetDsVlanActionProfile(V, svlanTpidIndex_f, p_ds_edit, p_vlan_edit->tpid_index);

    switch (p_vlan_edit->vlan_domain)
    {
    case CTC_SCL_VLAN_DOMAIN_SVLAN:
        st = 2;
        break;
    case CTC_SCL_VLAN_DOMAIN_CVLAN:
        st = 1;
        break;
    case CTC_SCL_VLAN_DOMAIN_UNCHANGE:
        st = 0;
        break;
    default:
        break;
    }
    SetDsVlanActionProfile(V, outerVlanStatus_f, p_ds_edit, st);
/*
   ctagAddMode_f
   svlanTpidIndexEn_f
   svlanTpidIndex_f
   ds_vlan_action_profile_t
 */
    return CTC_E_NONE;
}




STATIC int32
_sys_goldengate_scl_build_igs_action(uint8 lchip, void* ds_userid, void* ds_edit, sys_scl_sw_igs_action_t* p_action)
{
    uint32 flag;
    uint32 sub_flag;

    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(ds_edit);
    CTC_PTR_VALID_CHECK(ds_userid);

    SYS_SCL_DBG_FUNC();
    flag     = p_action->flag;
    sub_flag = p_action->sub_flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        SetDsUserId(V, dsFwdPtrValid_f, ds_userid, 1);
        SetDsUserId(V, u2_g2_dsFwdPtr_f, ds_userid, 0xFFFF);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_ETREE_LEAF))
    {
        SetDsUserId(V, isLeaf_f, ds_userid, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        SetDsUserId(V, statsPtr_f, ds_userid, p_action->chip.stats_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STP_ID))
    {
        SetDsUserId(V, stpId_f, ds_userid, p_action->stp_id);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_DSCP))
    {
        SetDsUserId(V, dscpValid_f, ds_userid, 1);
        SetDsUserId(V, dscp_f, ds_userid, p_action->dscp);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY))
    {
        SetDsUserId(V, userPriorityValid_f, ds_userid, 1);
        SetDsUserId(V, userPriority_f, ds_userid, p_action->priority);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_COLOR))
    {
        SetDsUserId(V, userColor_f, ds_userid, p_action->color);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SetDsUserId(V, userPriorityValid_f, ds_userid, 1);
        SetDsUserId(V, userPriority_f, ds_userid, p_action->priority);
        SetDsUserId(V, userColor_f, ds_userid, p_action->color);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        SetDsUserId(V, userIdExceptionEn_f, ds_userid, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
    {   /*
           p_ds->aging_valid = 1;
           p_ds->aging_index = SYS_AGING_TIMER_INDEX_SERVICE;*/
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_FID)
        ||CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        uint16 fid = 0;
        SetDsUserId(V, dsFwdPtrValid_f, ds_userid, 0);
        SetDsUserId(V, ecmpEn_f, ds_userid, 0);

        fid  = p_action->fid;
        fid += (p_action->fid_type ? 2 : 1) << 14;  /*2:vssid. 1:vrfid*/
        SetDsUserId(V, u2_g1_fid_f, ds_userid, fid);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        SetDsUserId(V, userCcfi_f, ds_userid, p_action->ccfi);
        SetDsUserId(V, userCcos_f, ds_userid, p_action->ccos);
        SetDsUserId(V, userCvlanId_f, ds_userid, p_action->cvid);
        SetDsUserId(V, userScfi_f, ds_userid, p_action->scfi);
        SetDsUserId(V, userScos_f, ds_userid, p_action->scos);
        SetDsUserId(V, userSvlanId_f, ds_userid, p_action->svid);
        SetDsUserId(V, vlanActionProfileId_f, ds_userid, p_action->chip.profile_id);
        SetDsUserId(V, svlanTagOperationValid_f, ds_userid, p_action->vlan_edit->stag_op ? 1 : 0);
        SetDsUserId(V, cvlanTagOperationValid_f, ds_userid, p_action->vlan_edit->ctag_op ? 1 : 0);
        SetDsUserId(V, vlanActionProfileValid_f, ds_userid, p_action->vlan_action_profile_valid);
        _sys_goldengate_scl_build_vlan_edit(lchip, ds_edit, p_action->vlan_edit);
    }

    {   /* binding_data */
        SetDsUserId(V, bindingEn_f, ds_userid, p_action->binding_en);
        SetDsUserId(V, bindingMacSa_f, ds_userid, p_action->binding_mac_sa);

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
        {
            SetDsUserId(V, userVlanPtr_f, ds_userid, p_action->user_vlan_ptr);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_APS))
        {
            SetDsUserId(V, userVlanPtr_f, ds_userid, p_action->user_vlan_ptr);
            SetDsUserId(V, u1_g2_apsSelectValid_f, ds_userid, p_action->bind.bds.aps_select_valid);
            SetDsUserId(V, u1_g2_apsSelectProtectingPath_f, ds_userid, p_action->bind.bds.aps_select_protecting_path);
            SetDsUserId(V, u1_g2_apsSelectGroupId_f, ds_userid, p_action->bind.bds.aps_select_group_id);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
        {
            uint32 bind_data[2] = { 0 };

            switch (p_action->bind_type)
            {
            case CTC_SCL_BIND_TYPE_PORT:
                bind_data[1] = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_action->bind.gport);
                bind_data[0] = 0;
                break;
            case CTC_SCL_BIND_TYPE_VLAN:
                bind_data[1] = p_action->bind.vlan_id << 4;
                bind_data[0] = 1;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA:
                bind_data[1] = (p_action->bind.ipv4_sa >> 28);
                bind_data[0] = ((p_action->bind.ipv4_sa & 0xFFFFFFF) << 4) | 2;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA_VLAN:
                bind_data[1] = (p_action->bind.ip_vlan.ipv4_sa >> 28) | (p_action->bind.ip_vlan.vlan_id << 4);
                bind_data[0] = ((p_action->bind.ip_vlan.ipv4_sa & 0xFFFFFFF) << 4) | 3;
                break;
            case CTC_SCL_BIND_TYPE_MACSA:
            {
                hw_mac_addr_t mac;
                SCL_SET_HW_MAC(mac, p_action->bind.mac_sa);
                bind_data[0] = mac[0];
                bind_data[1] = mac[1];
                SetDsUserId(V, bindingMacSa_f, ds_userid, 1);
            }
            break;
            default:
                return CTC_E_INVALID_PARAM;
            }
            SetDsUserId(A, u1_g1_bindingData_f, ds_userid, bind_data);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
        {
            SetDsUserId(V, userVlanPtr_f, ds_userid, p_action->user_vlan_ptr);
            SetDsUserId(V, dsFwdPtrValid_f, ds_userid, 1);
            SetDsUserId(V, u2_g2_dsFwdPtr_f, ds_userid, p_action->chip.offset_array);
            SetDsUserId(V, denyBridge_f, ds_userid, 1);
            SetDsUserId(V, bypassAll_f, ds_userid, p_action->bypass_all);
        }

        if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
        {
            SetDsUserId(V, u1_g2_logicSrcPort_f, ds_userid, p_action->bind.bds.logic_src_port);
            SetDsUserId(V, u1_g2_logicPortType_f, ds_userid, p_action->bind.bds.logic_port_type);
            SetDsUserId(V, u1_g2_logicPortSecurityEn_f, ds_userid, p_action->bind.bds.logic_port_security_en);
        }

        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
        {
            SetDsUserId(V, u1_g2_serviceAclQosEn_f, ds_userid, p_action->bind.bds.service_acl_qos_en);
        }

        /* must put this in the end. */
        if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
        {
            /* flow_policer_ptr at low 13bist*/
            SetDsUserId(V, u1_g2_servicePolicerValid_f, ds_userid, p_action->bind.bds.service_policer_valid);
            SetDsUserId(V, u1_g2_servicePolicerMode_f, ds_userid, p_action->bind.bds.service_policer_mode);
            SetDsUserId(V, u1_g2_flowPolicerPtr_f, ds_userid, p_action->chip.service_policer_ptr & 0x1FFF);
        }
/*
   ptpIndex_f
   oamTunnelEn_f
   ecmpEn_f
   srcQueueSelect_f
   u2_g3_ecmpGroupId_f
 */
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM))
    {
        SetDsUserId(V, vplsOamEn_f, ds_userid, 1);
        if (!(CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM)))
        {
            SetDsUserId(V, userVlanPtr_f, ds_userid, p_action->l2vpn_oam_id);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_flow_action(uint8 lchip, void* ds_scl, sys_scl_sw_flow_action_t* p_action)
{
    uint32 flag;
    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(ds_scl);

    SYS_SCL_DBG_FUNC();

    flag = p_action->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_DISCARD))
    {
        SetDsSclFlow(V, discardPacket_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_BRIDGE))
    {
        SetDsSclFlow(V, denyBridge_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_LEARNING))
    {
        SetDsSclFlow(V, denyLearning_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_ROUTE))
    {
        SetDsSclFlow(V, denyRoute_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_BRIDGE))
    {
        SetDsSclFlow(V, forceBridge_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_LEARNING))
    {
        SetDsSclFlow(V, forceLearning_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_ROUTE))
    {
        SetDsSclFlow(V, forceRoute_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_COPY_TO_CPU))
    {
        SetDsSclFlow(V, exceptionToCpu_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_STATS))
    {
        SetDsSclFlow(V, statsPtr_f, ds_scl, p_action->chip.stats_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        SetDsSclFlow(V, isAggFlowPolicer_f, ds_scl, 0);
        SetDsSclFlow(V, flowPolicerPtr_f, ds_scl, p_action->chip.micro_policer_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SetDsSclFlow(V, isAggFlowPolicer_f, ds_scl, 1);
        SetDsSclFlow(V, flowPolicerPtr_f, ds_scl, p_action->chip.macro_policer_ptr);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_PRIORITY))
    {
        SetDsSclFlow(V, priorityValid_f, ds_scl, 1);
        SetDsSclFlow(V, _priority_f, ds_scl, p_action->priority);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_COLOR))
    {
        SetDsSclFlow(V, color_f, ds_scl, p_action->color);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_TRUST))
    {
        SetDsSclFlow(V, qosPolicy_f, ds_scl, p_action->qos_policy);
    }
    else
    {
        SetDsSclFlow(V, qosPolicy_f, ds_scl, 0);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_REDIRECT))
    {
        if (p_action->ecmp_group_id)
        {
            SetDsSclFlow(V, ecmpEn_f, ds_scl, 1);
            SetDsSclFlow(V, u1_g1_ecmpGroupId_f, ds_scl, p_action->ecmp_group_id);
        }
        else
        {
            SetDsSclFlow(V, u1_g2_dsFwdPtr_f, ds_scl, p_action->chip.fwdptr);
        }
        SetDsSclFlow(V, denyBridge_f, ds_scl, 1);
        SetDsSclFlow(V, denyRoute_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_POSTCARD_EN))
    {
        SetDsSclFlow(V, postcardEn_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_METADATA))
    {
        SetDsSclFlow(V, metadata_f, ds_scl, p_action->metadata);
        SetDsSclFlow(V, metadataType_f, ds_scl, p_action->metadata_type);
    }
    else if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_FID))
    {
        SetDsSclFlow(V, metadata_f, ds_scl, p_action->fid);
        SetDsSclFlow(V, metadataType_f, ds_scl, p_action->metadata_type);
    }
    else if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_VRFID))
    {
        SetDsSclFlow(V, metadata_f, ds_scl, p_action->vrfid);
        SetDsSclFlow(V, metadataType_f, ds_scl, p_action->metadata_type);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_HASH_EN))
    {
        SetDsSclFlow(V, aclFlowHashType_f, ds_scl, p_action->acl_flow_hash_type);
        SetDsSclFlow(V, aclFlowHashFieldSel_f, ds_scl, p_action->acl_flow_hash_field_sel);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_TCAM_EN))
    {
        SetDsSclFlow(V, aclFlowTcamEn_f, ds_scl, 1);
        SetDsSclFlow(V, aclFlowTcamLookupType_f, ds_scl, p_action->acl_flow_tcam_lookup_type);
        SetDsSclFlow(V, aclFlowTcamLabel_f, ds_scl, p_action->acl_flow_tcam_label);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_OVERWRITE_ACL_FLOW_HASH))
    {
        SetDsSclFlow(V, overwriteAclFlowHash_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_OVERWRITE_ACL_FLOW_TCAM))
    {
        SetDsSclFlow(V, overwritePortAclFlowTcam_f, ds_scl, 1);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_OVERWRITE_PORT_IPFIX))
    {
        SetDsSclFlow(V, ipfixEn_f, ds_scl, 1);
        SetDsSclFlow(V, u2_gIpfix_overwriteIpfixHash_f, ds_scl, 1);
        SetDsSclFlow(V, u2_gIpfix_ipfixHashType_f, ds_scl, p_action->ipfix_hash_type);
        SetDsSclFlow(V, u2_gIpfix_ipfixHashFieldSel_f, ds_scl, p_action->ipfix_hash_field_sel);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_DECAP))
    {
        SetDsSclFlow(V, innerPacketLookup_f, ds_scl, p_action->inner_lookup);
        SetDsSclFlow(V, payloadOffsetStartPoint_f, ds_scl, p_action->payload_offset_start_point);
        SetDsSclFlow(V, packetType_f, ds_scl, p_action->packet_type);
        SetDsSclFlow(V, payloadOffset_f, ds_scl, p_action->payload_offset);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_build_tunnel_action(uint8 lchip, void* p_ad, sys_scl_tunnel_action_t* p_action)
{
    uint32 bind_data[2] =  { 0 };
    uint8 arp_exception_type;
    hw_mac_addr_t mac;
    CTC_PTR_VALID_CHECK(p_action);
    CTC_PTR_VALID_CHECK(p_ad);

    SYS_SCL_DBG_FUNC();

    if(p_action->rpf_check_en)
    {
        SetDsTunnelIdRpf(V, rpfCheckEn_f, p_ad, p_action->rpf_check_en);
        SetDsTunnelIdRpf(V, tunnelRpfCheckEn_f, p_ad, p_action->rpf_check_en);
        SetDsTunnelIdRpf(V, tunnelRpfCheckType_f, p_ad, 0);
        SetDsTunnelIdRpf(V, tunnelMoreRpfIf_f, p_ad, ((p_action->u1.data[0]) >> 15)& 0x1);
        SetDsTunnelIdRpf(V, array_0_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[0]) >> 14) & 0x1);
        SetDsTunnelIdRpf(V, array_0_rpfCheckId_f, p_ad, (p_action->u1.data[0]) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_1_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[0]) >> 30) & 0x1);
        SetDsTunnelIdRpf(V, array_1_rpfCheckId_f, p_ad, ((p_action->u1.data[0]) >> 16) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_2_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[1]) >> 14) & 0x1);
        SetDsTunnelIdRpf(V, array_2_rpfCheckId_f, p_ad, (p_action->u1.data[1]) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_3_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[1]) >> 30) & 0x1);
        SetDsTunnelIdRpf(V, array_3_rpfCheckId_f, p_ad, ((p_action->u1.data[1]) >> 16) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_4_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[2]) >> 14) & 0x1);
        SetDsTunnelIdRpf(V, array_4_rpfCheckId_f, p_ad, (p_action->u1.data[2]) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_5_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[2]) >> 30) & 0x1);
        SetDsTunnelIdRpf(V, array_5_rpfCheckId_f, p_ad, ((p_action->u1.data[2]) >> 16) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_6_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[3]) >> 14) & 0x1);
        SetDsTunnelIdRpf(V, array_6_rpfCheckId_f, p_ad, (p_action->u1.data[3]) & 0x3FFF);
        SetDsTunnelIdRpf(V, array_7_rpfCheckIdEn_f, p_ad, ((p_action->u1.data[3]) >> 30) & 0x1);
        SetDsTunnelIdRpf(V, array_7_rpfCheckId_f, p_ad, ((p_action->u1.data[3]) >> 16) & 0x3FFF);
    }
    else
    {
        SetDsTunnelId(V, srcQueueSelect_f, p_ad, p_action->src_queue_select);
        SetDsTunnelId(V, trillBfdEchoEn_f, p_ad, p_action->trill_bfd_echo_en);
        SetDsTunnelId(V, tunnelIdExceptionEn_f, p_ad, p_action->tunnel_id_exception_en);
        SetDsTunnelId(V, isTunnel_f, p_ad, p_action->is_tunnel);
        SetDsTunnelId(V, bindingMacSa_f, p_ad, p_action->binding_mac_sa);
        SetDsTunnelId(V, bindingEn_f, p_ad, p_action->binding_en);
        SetDsTunnelId(V, isidValid_f, p_ad, p_action->isid_valid);
        SetDsTunnelId(V, serviceAclQosEn_f, p_ad, p_action->service_acl_qos_en);
        SetDsTunnelId(V, logicPortType_f, p_ad, p_action->logic_port_type);
        SetDsTunnelId(V, tunnelRpfCheckRequest_f, p_ad, p_action->tunnel_rpf_check_request);
        SetDsTunnelId(V, rpfCheckEn_f, p_ad, p_action->rpf_check_en);
        SetDsTunnelId(V, pipBypassLearning_f, p_ad, p_action->pip_bypass_learning);
        SetDsTunnelId(V, trillBfdEn_f, p_ad, p_action->trill_bfd_en);
        SetDsTunnelId(V, trillTtlCheckEn_f, p_ad, p_action->trill_ttl_check_en);
        SetDsTunnelId(V, isatapCheckEn_f, p_ad, p_action->isatap_check_en);
        SetDsTunnelId(V, tunnelGreOptions_f, p_ad, p_action->tunnel_gre_options);
        SetDsTunnelId(V, ttlCheckEn_f, p_ad, p_action->ttl_check_en);
        SetDsTunnelId(V, useDefaultVlanTag_f, p_ad, p_action->use_default_vlan_tag);
        SetDsTunnelId(V, tunnelPayloadOffset_f, p_ad, p_action->tunnel_payload_offset);
        SetDsTunnelId(V, tunnelPacketType_f, p_ad, p_action->tunnel_packet_type);
        SetDsTunnelId(V, trillOptionEscapeEn_f, p_ad, p_action->trill_option_escape_en);
        SetDsTunnelId(V, svlanTpidIndex_f, p_ad, p_action->svlan_tpid_index);
        SetDsTunnelId(V, dsFwdPtrValid_f, p_ad, p_action->ds_fwd_ptr_valid);
        SetDsTunnelId(V, aclQosUseOuterInfo_f, p_ad, p_action->acl_qos_use_outer_info);
        SetDsTunnelId(V, outerVlanIsCVlan_f, p_ad, p_action->outer_vlan_is_cvlan);
        SetDsTunnelId(V, innerPacketLookup_f, p_ad, p_action->inner_packet_lookup);
        SetDsTunnelId(V, fidType_f, p_ad, p_action->fid_type);
        SetDsTunnelId(V, denyBridge_f, p_ad, p_action->deny_bridge);
        SetDsTunnelId(V, denyLearning_f, p_ad, p_action->deny_learning);
        SetDsTunnelId(V, pbbMcastDecap_f, p_ad, p_action->pbb_mcast_decap);
        SetDsTunnelId(V, statsPtr_f, p_ad, p_action->chip.stats_ptr);
        SetDsTunnelId(V, pbbOuterLearningEnable_f, p_ad, p_action->pbb_outer_learning_enable);
        SetDsTunnelId(V, pbbOuterLearningDisable_f, p_ad, p_action->pbb_outer_learning_disable);
        SetDsTunnelId(V, routeDisable_f, p_ad, p_action->route_disable);
        SetDsTunnelId(V, esadiCheckEn_f, p_ad, p_action->esadi_check_en);
        SetDsTunnelId(V, exceptionSubIndex_f, p_ad, p_action->exception_sub_index);
        SetDsTunnelId(V, trillChannelEn_f, p_ad, p_action->trill_channel_en);
        SetDsTunnelId(V, trillDecapWithoutLoop_f, p_ad, p_action->trill_decap_without_loop);
        SetDsTunnelId(V, trillMultiRpfCheck_f, p_ad, p_action->trill_multi_rpf_check);
        SetDsTunnelId(V, userDefaultCfi_f, p_ad, p_action->user_default_cfi);
        SetDsTunnelId(V, userDefaultCos_f, p_ad, p_action->user_default_cos);
        SetDsTunnelId(V, ttlUpdate_f, p_ad, p_action->ttl_update);
        SetDsTunnelId(V, ecmpEn_f, p_ad, p_action->ecmp_en);
        SetDsTunnelId(V, arpCtlEn_f, p_ad, p_action->arp_ctl_en);

        switch (p_action->arp_exception_type)
        {
            case CTC_EXCP_FWD_AND_TO_CPU:
                arp_exception_type = 0;
                break;
            case CTC_EXCP_NORMAL_FWD:
                arp_exception_type = 1;
                break;
            case CTC_EXCP_DISCARD_AND_TO_CPU:
                arp_exception_type = 2;
                break;
            case CTC_EXCP_DISCARD:
                arp_exception_type = 3;
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }
        SetDsTunnelId(V, arpExceptionType_f, p_ad, arp_exception_type);

        if (p_action->binding_en)
        {
            if (p_action->binding_mac_sa)
            {
                SCL_SET_HW_MAC(mac, p_action->mac_sa);
                bind_data[0] = mac[0];
                bind_data[1] = mac[1];
                SetDsTunnelId(A, u1_g1_bindingData_f, p_ad, bind_data);
            }
            else
            {
                SetDsTunnelId(A, u1_g1_bindingData_f, p_ad, p_action->u1.data);
            }
        }
        else
        {
            SetDsTunnelId(V, u1_g2_apsSelectProtectingPath_f, p_ad, p_action->u1.g2.aps_select_protecting_path);
            SetDsTunnelId(V, u1_g2_servicePolicerValid_f, p_ad, p_action->u1.g2.service_policer_valid);
            SetDsTunnelId(V, u1_g2_apsSelectValid_f, p_ad, p_action->u1.g2.aps_select_valid);
            SetDsTunnelId(V, u1_g2_apsSelectGroupId_f, p_ad, p_action->u1.g2.aps_select_group_id);
            SetDsTunnelId(V, u1_g2_servicePolicerMode_f, p_ad, p_action->u1.g2.service_policer_mode);
            SetDsTunnelId(V, u1_g2_logicPortSecurityEn_f, p_ad, p_action->u1.g2.logic_port_security_en);
            SetDsTunnelId(V, u1_g2_logicSrcPort_f, p_ad, p_action->u1.g2.logic_src_port);
            SetDsTunnelId(V, u1_g2_flowPolicerPtr_f, p_ad, p_action->chip.policer_ptr);
            SetDsTunnelId(V, u1_g2_metadataType_f, p_ad, p_action->u1.g2.metadata_type);
        }

        if (p_action->ds_fwd_ptr_valid)
        {
            SetDsTunnelId(V, u2_g2_dsFwdPtr_f, p_ad, p_action->u2.dsfwdptr);
        }
        else if (p_action->ecmp_en)
        {
            SetDsTunnelId(V, u2_g3_ecmpGroupId_f, p_ad, p_action->u2.ecmp_group_id);
        }
        else
        {
            SetDsTunnelId(V, u2_g1_fid_f, p_ad, p_action->u2.fid);
        }

        if (!p_action->use_default_vlan_tag && !p_action->trill_multi_rpf_check)
        {
            SetDsTunnelId(V, u3_g3_secondFidEn_f, p_ad, p_action->u3.g3.second_fid_en);
            SetDsTunnelId(V, u3_g3_secondFid_f, p_ad, p_action->u3.g3.second_fid);
        }
        else if (!p_action->use_default_vlan_tag)
        {
            SetDsTunnelId(V, u3_g1_isid_f, p_ad, p_action->u3.isid);
        }
        else
        {
            SetDsTunnelId(V, u3_g2_userDefaultVlanId_f, p_ad, p_action->u3.user_default_vlan);
        }


        SetDsTunnelId(V, u4_g1_vxlanTunnelExistCheckEn_f, p_ad, p_action->u4.vxlan.exist_check_en);
        SetDsTunnelId(V, u4_g1_vxlanTunnelIsNotExist_f, p_ad, p_action->u4.vxlan.is_not_exist);

        SetDsTunnelId(V, u4_g2_nvgreTunnelExistCheckEn_f, p_ad, p_action->u4.nvgre.exist_check_en);
        SetDsTunnelId(V, u4_g2_nvgreTunnelIsNotExist_f, p_ad, p_action->u4.nvgre.is_not_exist);

        SetDsTunnelId(V, classifyUseOuterInfo_f, p_ad, p_action->classify_use_outer_info);
        SetDsTunnelId(V, innerVtagCheckMode_f, p_ad, p_action->inner_vtag_check_mode);
        SetDsTunnelId(V, routerMacProfileEn_f, p_ad, p_action->router_mac_profile_en);
        SetDsTunnelId(V, routerMacProfile_f, p_ad, p_action->router_mac_profile);
        /*
           discard_f
           softwireEn_f
           innerPacketNatEn_f
           innerPacketPbrEn_f
           outerTtlCheck_f
         */
    }




    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_get_table_id(uint8 lchip, uint8 block_id, uint8 key_type, uint8 act_type,
                                 uint32* key_id, uint32* act_id)
{
    uint8 distance = (DsUserId1MacKey160_t - DsUserId0MacKey160_t) * block_id;
    uint8 ad_distance = (DsUserId1Ipv6640TcamAd_t - DsUserId0Ipv6640TcamAd_t) * block_id;
    uint8 dir      = 0;
    CTC_PTR_VALID_CHECK(key_id);
    CTC_PTR_VALID_CHECK(act_id);

    *key_id = 0;
    *act_id = 0;
    switch (act_type)
    {
    case SYS_SCL_ACTION_INGRESS:
        *act_id = DsUserId_t;
        break;
    case SYS_SCL_ACTION_EGRESS:
        *act_id = 0xFFFFFFFF; /*invalid*/
        dir     = 1;
        break;
    case SYS_SCL_ACTION_TUNNEL:
        *act_id = DsTunnelId_t;
        break;
    case SYS_SCL_ACTION_FLOW:
        *act_id = DsSclFlow_t;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    switch (key_type)
    {
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        *key_id = (0 == dir) ? DsUserIdDoubleVlanPortHashKey_t : DsEgressXcOamDoubleVlanPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT:
        *key_id = (0 == dir) ? DsUserIdPortHashKey_t : DsEgressXcOamPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        *key_id = (0 == dir) ? DsUserIdCvlanPortHashKey_t : DsEgressXcOamCvlanPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        *key_id = (0 == dir) ? DsUserIdSvlanPortHashKey_t : DsEgressXcOamSvlanPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        *key_id = (0 == dir) ? DsUserIdCvlanCosPortHashKey_t : DsEgressXcOamCvlanCosPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        *key_id = (0 == dir) ? DsUserIdSvlanCosPortHashKey_t : DsEgressXcOamSvlanCosPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_MAC:
        *key_id = DsUserIdMacHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_MAC:
        *key_id = DsUserIdMacPortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        *key_id = DsUserIdIpv4SaHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV4:
        *key_id = DsUserIdIpv4PortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_IPV6:
        *key_id = DsUserIdIpv6SaHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV6:
        *key_id = DsUserIdIpv6PortHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_L2:
        *key_id = DsUserIdSclFlowL2HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        *key_id = DsUserIdTunnelIpv4HashKey_t;
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
        *key_id = DsUserIdTunnelIpv4DaHashKey_t;
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        *key_id = DsUserIdTunnelIpv4GreKeyHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        *key_id = DsUserIdTunnelIpv4RpfHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
        *key_id = DsUserIdTunnelIpv4UcNvgreMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
        *key_id = DsUserIdTunnelIpv4UcNvgreMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
        *key_id = DsUserIdTunnelIpv4McNvgreMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
        *key_id = DsUserIdTunnelIpv4NvgreMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
        *key_id = DsUserIdTunnelIpv6UcNvgreMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
        *key_id = DsUserIdTunnelIpv6UcNvgreMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
        *key_id = DsUserIdTunnelIpv6McNvgreMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
        *key_id = DsUserIdTunnelIpv6McNvgreMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
        *key_id = DsUserIdTunnelIpv4UcVxlanMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
        *key_id = DsUserIdTunnelIpv4UcVxlanMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
        *key_id = DsUserIdTunnelIpv4McVxlanMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
        *key_id = DsUserIdTunnelIpv4VxlanMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
        *key_id = DsUserIdTunnelIpv6UcVxlanMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
        *key_id = DsUserIdTunnelIpv6UcVxlanMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
        *key_id = DsUserIdTunnelIpv6McVxlanMode0HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
        *key_id = DsUserIdTunnelIpv6McVxlanMode1HashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TRILL_UC:
        *key_id = DsUserIdTunnelTrillUcDecapHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC:
        *key_id = DsUserIdTunnelTrillMcDecapHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
        *key_id = DsUserIdTunnelTrillUcRpfHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
        *key_id = DsUserIdTunnelTrillMcRpfHashKey_t;
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        *key_id = DsUserIdTunnelTrillMcAdjHashKey_t;
        break;

    case SYS_SCL_KEY_TCAM_MAC:
        if (p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en)
        {
            *key_id = DsUserId0L3Key320_t + distance;
            *act_id = DsUserId0L3320TcamAd_t + ad_distance;
        }
        else
        {
            *key_id = DsUserId0MacKey160_t + distance;
            *act_id = DsUserId0Mac160TcamAd_t + ad_distance;
        }
        break;

    case SYS_SCL_KEY_TCAM_VLAN:
        *key_id = DsUserId0MacKey160_t + distance;
        *act_id = DsUserId0Mac160TcamAd_t + ad_distance;
        break;

    case SYS_SCL_KEY_TCAM_IPV4:
        *key_id = DsUserId0L3Key320_t + distance;
        *act_id = DsUserId0L3320TcamAd_t + ad_distance;
        break;

    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
        *key_id = DsUserId0L3Key160_t + distance;
        *act_id = DsUserId0L3160TcamAd_t + ad_distance;
        break;

    case SYS_SCL_KEY_TCAM_IPV6:
        *key_id = DsUserId0Ipv6Key640_t + distance;
        *act_id = DsUserId0Ipv6640TcamAd_t + ad_distance;
        break;

    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        *act_id = DsUserId_t;
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        *act_id = DsVlanXlateDefault_t;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_add_hw(uint8 lchip, uint32 eid)
{
    uint32               p_key_index;
    uint32               p_ad_index;

    uint32               cmd_ds  = 0;
    uint32               cmd_key = 0;

    tbl_entry_t          tcam_key;

    uint32               key_id;
    uint32               act_id;
    uint8                key_type;
    uint8                act_type;
    uint8                step;

    static uint32               key[MAX_ENTRY_WORD]       = { 0 };
    static uint32               ds[MAX_ENTRY_WORD]        = { 0 };
    static uint32               ds_edit[MAX_ENTRY_WORD]   = { 0 };
    static uint32               tcam_data[MAX_ENTRY_WORD] = { 0 };
    static uint32               tcam_mask[MAX_ENTRY_WORD] = { 0 };

    sys_scl_sw_group_t   * pg         = NULL;
    sys_scl_sw_entry_t   * pe         = NULL;
    sys_scl_sw_key_t     * pk         = NULL;
    sys_scl_sw_action_t  * pa         = NULL;
    void                 * p_key_void = key;
    drv_scl_group_info_t drv_info;

    SYS_SCL_DBG_FUNC();

    sal_memset(key,0,sizeof(key));
    sal_memset(ds,0,sizeof(ds));
    sal_memset(ds_edit,0,sizeof(ds_edit));
    sal_memset(tcam_data,0,sizeof(tcam_data));
    sal_memset(tcam_mask,0,sizeof(tcam_mask));

    tcam_key.data_entry = (uint32 *) &tcam_data;
    tcam_key.mask_entry = (uint32 *) &tcam_mask;

    /* get sys entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    key_type = pe->key.type;
    act_type = pe->action->type;
    pk       = &pe->key;
    pa       = pe->action;

    /* build drv group_info */
    CTC_ERROR_RETURN(_sys_goldengate_scl_build_ds_group_info(lchip, &(pg->group_info), &drv_info));

    if (SCL_ENTRY_IS_TCAM(key_type) && CTC_SCL_GROUP_TYPE_NONE == pg->group_info.type)
    {
        switch (key_type)
        {
            case SYS_SCL_KEY_TCAM_VLAN:
                sal_memcpy(&drv_info, &pk->u0.vlan_key.port_info, sizeof(drv_scl_group_info_t));
                break;
            case SYS_SCL_KEY_TCAM_MAC:
                sal_memcpy(&drv_info, &pk->u0.mac_key.port_info, sizeof(drv_scl_group_info_t));
                break;
            case SYS_SCL_KEY_TCAM_IPV4:
            case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
                sal_memcpy(&drv_info, &pk->u0.ipv4_key.port_info, sizeof(drv_scl_group_info_t));
                break;
            case SYS_SCL_KEY_TCAM_IPV6:
                sal_memcpy(&drv_info, &pk->u0.ipv6_key.port_info, sizeof(drv_scl_group_info_t));
                break;
            default:
                return CTC_E_INVALID_PARAM;
                break;
        }
    }

    if ((SCL_ENTRY_IS_TCAM(key_type)) || (SYS_SCL_IS_DEFAULT(key_type)))  /* default index also get from key_index */
    {
        step = SYS_SCL_MAP_FPA_SIZE_TO_STEP(p_gg_scl_master[lchip]->alloc_id[pe->key.type]);
        p_key_index = pe->fpae.offset_a / step;
        p_ad_index  = pe->fpae.offset_a;
    }
    else
    {
        p_key_index = pe->fpae.offset_a;
        p_ad_index = pe->action->ad_index;
    }

    switch (key_type)
    {
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        _sys_goldengate_scl_build_hash_2vlan(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT:
        _sys_goldengate_scl_build_hash_port(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        _sys_goldengate_scl_build_hash_cvlan(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        _sys_goldengate_scl_build_hash_svlan(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        _sys_goldengate_scl_build_hash_cvlan_cos(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        _sys_goldengate_scl_build_hash_svlan_cos(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_MAC:
        _sys_goldengate_scl_build_hash_mac(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_MAC:
        _sys_goldengate_scl_build_hash_port_mac(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        _sys_goldengate_scl_build_hash_ipv4(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV4:
        _sys_goldengate_scl_build_hash_port_ipv4(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_IPV6:
        _sys_goldengate_scl_build_hash_ipv6(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV6:
        _sys_goldengate_scl_build_hash_port_ipv6(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_L2:
        _sys_goldengate_scl_build_hash_l2(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        _sys_goldengate_scl_build_hash_tunnel_ipv4(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_da(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_gre(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_rpf(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv4_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_ipv4_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv4_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_ipv4_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode1(lchip, pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode0(pk, key, p_ad_index);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode1(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_HASH_TRILL_UC:
        _sys_goldengate_scl_build_hash_trill_uc(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_HASH_TRILL_MC:
        _sys_goldengate_scl_build_hash_trill_mc(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
        _sys_goldengate_scl_build_hash_trill_uc_rpf(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
        _sys_goldengate_scl_build_hash_trill_mc_rpf(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        _sys_goldengate_scl_build_hash_trill_mc_adj(lchip, pk, key, p_ad_index);
        break;
    case SYS_SCL_KEY_TCAM_MAC:
        if (p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en)
        {
            CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_mac_key_double(lchip, &drv_info, &tcam_key, &pk->u0.mac_key, act_type));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_mac_key(lchip, &drv_info, &tcam_key, &pk->u0.mac_key, act_type));
        }
        p_key_void = &tcam_key;
        break;

    case SYS_SCL_KEY_TCAM_VLAN:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_vlan_key(lchip, &drv_info, &tcam_key, &pk->u0.vlan_key, act_type));
        p_key_void = &tcam_key;
        break;

    case SYS_SCL_KEY_TCAM_IPV4:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_ipv4_key_double(lchip, &drv_info, &tcam_key, &pk->u0.ipv4_key, act_type));
        p_key_void = &tcam_key;
        break;

    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_ipv4_key_single(lchip, &drv_info, &tcam_key, &pk->u0.ipv4_key, act_type));
        p_key_void = &tcam_key;
        break;

    case SYS_SCL_KEY_TCAM_IPV6:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_tcam_ipv6_key(lchip, &drv_info, &tcam_key, &pk->u0.ipv6_key, act_type));
        p_key_void = &tcam_key;
        break;

    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }



    switch (act_type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_igs_action
                             (lchip, ds, ds_edit, &pa->u0.ia));
        break;
    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_egs_action
                             (lchip, ds, &pa->u0.ea));
        sal_memcpy(key + 3, ds + 3, sizeof(DsVlanXlateDefault_m));

        /* egress default entry DsVlanXlateDefault */
        if(SYS_SCL_KEY_PORT_DEFAULT_EGRESS == key_type)
        {
            sal_memmove(ds, ds + 3, sizeof(DsVlanXlateDefault_m));
        }
        break;
    case SYS_SCL_ACTION_TUNNEL:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_tunnel_action
                             (lchip, ds, &pa->u0.ta));
        break;
    case SYS_SCL_ACTION_FLOW:
        CTC_ERROR_RETURN(_sys_goldengate_scl_build_flow_action
                             (lchip, ds, &pa->u0.fa));
        break;
    }

    if (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint32 cmd_edit = DRV_IOW(DsVlanActionProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, pa->u0.ia.chip.profile_id, cmd_edit, ds_edit));
    }

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, pg->group_info.block_id, key_type, act_type, &key_id, &act_id));

    if (0xFFFFFFFF != act_id) /* normal egress ad DsVlanXlate is in key */
    {
        cmd_ds = DRV_IOW(act_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ad_index, cmd_ds, ds));
    }

    if ((SYS_SCL_KEY_PORT_DEFAULT_EGRESS != key_type) &&
        (SYS_SCL_KEY_PORT_DEFAULT_INGRESS != key_type))
    {
        cmd_key = DRV_IOW(key_id, DRV_ENTRY_FLAG);

        if (SCL_ENTRY_IS_TCAM(key_type))
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key_index, cmd_key, p_key_void));
        }
        else
        {
            uint8 acc_type = (act_type == SYS_SCL_ACTION_EGRESS) ?
                DRV_CPU_ADD_ACC_XC_OAM_BY_IDX : DRV_CPU_ADD_ACC_USER_ID_BY_IDX;
            drv_cpu_acc_in_t  in;
            drv_cpu_acc_out_t out;

            sal_memset(&in, 0, sizeof(in));
            sal_memset(&out, 0, sizeof(out));

            in.tbl_id = key_id;
            in.data   = p_key_void;
            in.key_index  = p_key_index;
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, &out));
        }
    }
#if 0
    {   /* set aging status */
        uint8 aging_type;

        if (SCL_ENTRY_IS_TCAM(key_type))
        {
            aging_type = SYS_AGING_DOMAIN_TCAM;
        }
        else
        {
            return CTC_E_NONE;
        }


        if (((SYS_SCL_ACTION_INGRESS == act_type) && CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
            || ((SYS_SCL_ACTION_EGRESS == act_type) && CTC_FLAG_ISSET(pa->u0.ea.flag, CTC_SCL_EGS_ACTION_FLAG_AGING)))
        {
            CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status(lchip, aging_type,
                                                                   p_key_index[lchip], TRUE));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status(lchip, aging_type,
                                                                   p_key_index[lchip], FALSE));
        }

    }
#endif
    return CTC_E_NONE;
}

/*delete key is enough*/
STATIC int32
_sys_goldengate_scl_remove_hw(uint8 lchip, uint32 eid)
{
    uint32                 p_key_index = 0;
    uint32                 p_ad_index  = 0;
    uint8                  step        = 0;
    uint32                 key_id        = 0;
    uint32                 act_id        = 0;
    uint32               pu32[MAX_ENTRY_WORD]       = { 0 };
    void                 * p_key_void = pu32;

    sys_scl_sw_group_t     * pg = NULL;
    sys_scl_sw_entry_t     * pe = NULL;

    /* 10 is enough */
    uint32 mm[10] = { 0 };

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid: %u \n", eid);

    /* get sys entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* get block_id, p_key_index */
    p_key_index = pe->fpae.offset_a;
    p_ad_index = pe->fpae.offset_a;

    /* get group_info */
    CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, pg->group_info.block_id, pe->key.type,
                                                      pe->action->type, &key_id, &act_id));

    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* tcam entry need delete action too */
        step = SYS_SCL_MAP_FPA_SIZE_TO_STEP(p_gg_scl_master[lchip]->alloc_id[pe->key.type]);
        p_key_index = p_key_index / step;
        CTC_ERROR_RETURN(DRV_TCAM_TBL_REMOVE(lchip, key_id, p_key_index));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ad_index, DRV_IOW(act_id, DRV_ENTRY_FLAG), mm));
    }
    else
    {
        uint8 acc_type = (pe->action->type == SYS_SCL_ACTION_EGRESS) ?
            DRV_CPU_ADD_ACC_XC_OAM_BY_IDX : DRV_CPU_ADD_ACC_USER_ID_BY_IDX;

        /* -- set hw valid bit  -- */

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_HASH_PORT_2VLAN:
                if(CTC_INGRESS == pe->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdDoubleVlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamDoubleVlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT:
                if(CTC_INGRESS == pe->key.u0.hash.u0.port.dir)
                {
                    SetDsUserIdPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN:
                if(CTC_INGRESS == pe->key.u0.hash.u0.vlan.dir)
                {
                    SetDsUserIdCvlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamCvlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN:
                if(CTC_INGRESS == pe->key.u0.hash.u0.vlan.dir)
                {
                    SetDsUserIdSvlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamSvlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
                if(CTC_INGRESS == pe->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdCvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamCvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
                if(CTC_INGRESS == pe->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdSvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamSvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_MAC:
                SetDsUserIdMacHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_MAC:
                SetDsUserIdMacPortHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdMacPortHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_IPV4:
                SetDsUserIdIpv4SaHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV4:
                SetDsUserIdIpv4PortHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_IPV6:
                SetDsUserIdIpv6SaHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdIpv6SaHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV6:
                SetDsUserIdIpv6PortHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid1_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid2_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_L2:
                SetDsUserIdSclFlowL2HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdSclFlowL2HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
                SetDsUserIdTunnelIpv4HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
                SetDsUserIdTunnelIpv4DaHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
                SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
                SetDsUserIdTunnelIpv4RpfHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
                SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
                SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
                SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
                SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
                SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
                SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
                SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
                SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid3_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_UC:
                SetDsUserIdTunnelTrillUcDecapHashKey(V, valid_f, pu32, 1);
                break;
           case SYS_SCL_KEY_HASH_TRILL_MC:
                SetDsUserIdTunnelTrillMcDecapHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
                SetDsUserIdTunnelTrillUcRpfHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
                SetDsUserIdTunnelTrillMcRpfHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
                SetDsUserIdTunnelTrillMcAdjHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
            case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }

        if ((SYS_SCL_KEY_PORT_DEFAULT_EGRESS != pe->key.type) &&
            (SYS_SCL_KEY_PORT_DEFAULT_INGRESS != pe->key.type))
        {
            drv_cpu_acc_in_t  in;

            sal_memset(&in, 0, sizeof(in));

            in.tbl_id = key_id;
            in.data   = p_key_void;
            in.key_index  = p_key_index;
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, NULL));
        }
    }

    return CTC_E_NONE;
}


/*
 * install entry to hardware table
 */
STATIC int32
_sys_goldengate_scl_install_entry(uint8 lchip, uint32 eid, uint8 flag, uint8 move_sw)
{

    sys_scl_sw_group_t* pg = NULL;
    sys_scl_sw_entry_t* pe = NULL;

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    /* get sys entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, eid, &pe, &pg, NULL);

    if(!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }
    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (SYS_SCL_ENTRY_OP_FLAG_ADD == flag)
    {
      /* when install group may install all entry include installed entry*/

        CTC_ERROR_RETURN(_sys_goldengate_scl_add_hw(lchip, eid));

        pe->fpae.flag = FPA_ENTRY_FLAG_INSTALLED;

        if (move_sw)
        {
            /* installed entry move to tail,keep uninstalled entry before installed in group entry list */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));

            if(NULL == ctc_slist_add_tail(pg->entry_list, &(pe->head)))
            {
                return CTC_E_INVALID_PARAM;
            }
        }
    }
    else if (SYS_SCL_ENTRY_OP_FLAG_DELETE == flag)
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_remove_hw(lchip, eid));

        pe->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

        if (move_sw)
        {
            /* move to head */
            ctc_slist_delete_node(pg->entry_list, &(pe->head));

            if(NULL == ctc_slist_add_head(pg->entry_list, &(pe->head)))
            {
                return CTC_E_INVALID_PARAM;
            }
        }
    }

    return CTC_E_NONE;
}

#define __SCL_FPA_CALLBACK__

STATIC int32
_sys_goldengate_scl_get_block_by_pe_fpa(ctc_fpa_entry_t* pe, ctc_fpa_block_t** pb)
{
    uint8             block_id;
    uint8             lchip = 0;
    sys_scl_sw_entry_t* entry;

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pb);

    *pb = NULL;
    lchip = pe->lchip;

    entry = SCL_OUTER_ENTRY(pe);
    CTC_PTR_VALID_CHECK(entry->group);

    block_id = entry->group->group_info.block_id;


    *pb = &(p_gg_scl_master[lchip]->block[block_id].fpab);

    return CTC_E_NONE;
}


/*
 * move entry in hardware table to an new index.
 */
STATIC int32
_sys_goldengate_scl_entry_move_hw_fpa(ctc_fpa_entry_t* pe, int32 tcam_idx_new)
{
    uint8  lchip = 0;
    uint32 tcam_idx_old = pe->offset_a;
    uint32 eid;

    lchip = pe->lchip;

    CTC_PTR_VALID_CHECK(pe);
    SYS_SCL_DBG_FUNC();

    eid = pe->entry_id;

    /* add first */
    pe->offset_a = tcam_idx_new;
    CTC_ERROR_RETURN(_sys_goldengate_scl_add_hw(lchip, eid));

    /* then delete */
    pe->offset_a = tcam_idx_old;
    CTC_ERROR_RETURN(_sys_goldengate_scl_remove_hw(lchip, eid));

    /* set new_index */
    pe->offset_a = tcam_idx_new;

    return CTC_E_NONE;
}

/*
 * below is build sw struct
 */
#define __SCL_HASH_CALLBACK__


STATIC uint32
_sys_goldengate_scl_hash_make_group(sys_scl_sw_group_t* pg)
{
    return pg->group_id;
}

STATIC bool
_sys_goldengate_scl_hash_compare_group(sys_scl_sw_group_t* pg0, sys_scl_sw_group_t* pg1)
{
    return(pg0->group_id == pg1->group_id);
}

STATIC uint32
_sys_goldengate_scl_hash_make_entry(sys_scl_sw_entry_t* pe)
{
    return pe->fpae.entry_id;
}

STATIC bool
_sys_goldengate_scl_hash_compare_entry(sys_scl_sw_entry_t* pe0, sys_scl_sw_entry_t* pe1)
{
    return(pe0->fpae.entry_id == pe1->fpae.entry_id);
}

STATIC uint32
_sys_goldengate_scl_hash_make_entry_by_key(sys_scl_sw_entry_t* pe)
{
    uint32 size;
    uint8  * k;
    uint8  lchip = 0;
    lchip = pe->lchip;
    size = p_gg_scl_master[lchip]->key_size[pe->key.type];
    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        k = (uint8 *) (&pe->key.u0);
    }
    else
    {
        k = (uint8 *) (&pe->key.u0.hash.u0);
    }
    return ctc_hash_caculate(size, k);
}
STATIC bool
_sys_goldengate_scl_hash_compare_entry_by_key(sys_scl_sw_entry_t* pe0, sys_scl_sw_entry_t* pe1)
{
    uint8 lchip;
    lchip = pe0->lchip;
    if (!pe0 || !pe1)
    {
        return FALSE;
    }

    if ((pe0->key.type != pe1->key.type) ||
        (pe0->group->group_info.type != pe1->group->group_info.type))
    {
        return FALSE;
    }

    if (SCL_ENTRY_IS_TCAM(pe0->key.type))
    {
        if (!sal_memcmp(&pe0->key.u0, &pe1->key.u0, p_gg_scl_master[lchip]->key_size[pe0->key.type]))
        {
            if (CTC_SCL_GROUP_TYPE_PORT_CLASS == pe0->group->group_info.type)
            {   /* for ip src guard. vlan class default entry. the key is the same, but class_id is not. */
                if (pe0->group->group_info.u0.port_class_id == pe1->group->group_info.u0.port_class_id)
                {
                    return TRUE;
                }
            }
            else if ((CTC_SCL_GROUP_TYPE_PORT == pe0->group->group_info.type) || (CTC_SCL_GROUP_TYPE_LOGIC_PORT == pe0->group->group_info.type))
            {   /* for vlan mapping flex entry. the key is the same, but port is not. */
                if (pe0->group->group_info.u0.gport == pe1->group->group_info.u0.gport)
                {
                    return TRUE;
                }
            }
            else
            {
                return TRUE;
            }
        }
    }
    else /* hash */
    {
        if (!sal_memcmp(&pe0->key.u0.hash.u0, &pe1->key.u0.hash.u0, p_gg_scl_master[lchip]->key_size[pe0->key.type]))
        {
            return TRUE;
        }
    }

    return FALSE;
}
STATIC uint32
_sys_goldengate_scl_hash_make_action(sys_scl_sw_action_t* pa)
{
    uint32 size = 0;
    uint8  * k;
    uint8  lchip;

    lchip = pa->lchip;

    k = (uint8 *) (&pa->u0);
    switch (pa->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        size = p_gg_scl_master[lchip]->hash_igs_action_size;
        break;
    case SYS_SCL_ACTION_EGRESS:
        size = p_gg_scl_master[lchip]->hash_egs_action_size;
        break;
    case SYS_SCL_ACTION_TUNNEL:
        size = p_gg_scl_master[lchip]->hash_tunnel_action_size;
        break;
    case SYS_SCL_ACTION_FLOW:
        size = p_gg_scl_master[lchip]->hash_flow_action_size;
        break;
    }
    return ctc_hash_caculate(size, k);
}

STATIC bool
_sys_goldengate_scl_hash_compare_action0(sys_scl_sw_action_t* pa0, sys_scl_sw_action_t* pa1)
{
    uint8 lchip;
    lchip = pa0->lchip;
    if (!pa0 || !pa1)
    {
        return FALSE;
    }

    if (pa0->type != pa1->type)
    {
        return FALSE;
    }
    switch (pa0->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        if ((pa0->u0.ia.chip.profile_id == pa1->u0.ia.chip.profile_id)
            && (pa0->u0.ia.chip.offset_array == pa1->u0.ia.chip.offset_array)
            && (pa0->u0.ia.chip.service_policer_ptr == pa1->u0.ia.chip.service_policer_ptr)
            && (pa0->u0.ia.chip.stats_ptr == pa1->u0.ia.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ia, &pa1->u0.ia, p_gg_scl_master[lchip]->hash_igs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_EGRESS:
        if ((pa0->u0.ea.chip.stats_ptr == pa1->u0.ea.chip.stats_ptr)
            && (!sal_memcmp(&pa0->u0.ea, &pa1->u0.ea, p_gg_scl_master[lchip]->hash_egs_action_size)))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_TUNNEL:
        if (!sal_memcmp(&pa0->u0.ta, &pa1->u0.ta, p_gg_scl_master[lchip]->hash_tunnel_action_size))
        {
            return TRUE;
        }
        break;
    case SYS_SCL_ACTION_FLOW:
        if (!sal_memcmp(&pa0->u0.fa, &pa1->u0.fa, p_gg_scl_master[lchip]->hash_flow_action_size))
        {
            return TRUE;
        }
        break;
    }


    return FALSE;
}



/* if vlan edit in bucket equals */
STATIC bool
_sys_goldengate_scl_hash_compare_vlan_edit(sys_scl_sw_vlan_edit_t* pv0,
                                           sys_scl_sw_vlan_edit_t* pv1)
{
    uint32 size = 0;
    uint8 lchip;
    lchip = pv0->lchip;
    if (!pv0 || !pv1)
    {
        return FALSE;
    }

    size = p_gg_scl_master[lchip]->vlan_edit_size;
    if (!sal_memcmp(pv0, pv1, size))
    {
        return TRUE;
    }

    return FALSE;
}

STATIC uint32
_sys_goldengate_scl_hash_make_vlan_edit(sys_scl_sw_vlan_edit_t* pv)
{
    uint32 size = 0;

    uint8  * k = NULL;

    uint8 lchip;
    lchip = pv->lchip;

    CTC_PTR_VALID_CHECK(pv);
    size = p_gg_scl_master[lchip]->vlan_edit_size;
    k    = (uint8 *) pv;

    return ctc_hash_caculate(size, k);
}

#define __SCL_MAP_UNMAP__

STATIC int32
_sys_goldengate_scl_map_tcam_key_port(uint8 lchip, ctc_field_port_t port_data, ctc_field_port_t port_mask, drv_scl_group_info_t *drv_port_info)
{
    CTC_PTR_VALID_CHECK(drv_port_info);

    switch (port_data.type)
    {
        case CTC_FIELD_PORT_TYPE_GPORT:
            drv_port_info->class_id_data = 0;
            drv_port_info->class_id_mask = 0;

            drv_port_info->gport      = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(port_data.gport);
            drv_port_info->gport_mask = port_mask.gport;

            drv_port_info->gport_type      = 0;
            drv_port_info->gport_type_mask = 1;
            break;
        case CTC_FIELD_PORT_TYPE_LOGIC_PORT:
            drv_port_info->class_id_data = 0;
            drv_port_info->class_id_mask = 0;

            drv_port_info->gport      = port_data.logic_port;
            drv_port_info->gport_mask = port_mask.logic_port;

            drv_port_info->gport_type      = 1;
            drv_port_info->gport_type_mask = 1;
            break;
        case CTC_FIELD_PORT_TYPE_PORT_CLASS:
            drv_port_info->class_id_data = port_data.port_class_id;
            drv_port_info->class_id_mask = port_mask.port_class_id;

            drv_port_info->gport      = 0;
            drv_port_info->gport_mask = 0;

            drv_port_info->gport_type      = 0;
            drv_port_info->gport_type_mask = 0;
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_get_ip_frag(uint8 lchip, uint8 ctc_ip_frag, uint8* frag_info, uint8* frag_info_mask)
{
    switch (ctc_ip_frag)
    {
    case CTC_IP_FRAG_NON:
        /* Non fragmented */
        *frag_info      = 0;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_FIRST:
        /* Fragmented, and first fragment */
        *frag_info      = 1;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_NON_OR_FIRST:
        /* Non fragmented OR Fragmented and first fragment */
        *frag_info      = 0;
        *frag_info_mask = 2;     /* mask frag_info as 0x */
        break;

    case CTC_IP_FRAG_SMALL:
        /* Small fragment */
        *frag_info      = 2;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_NOT_FIRST:
        /* Not first fragment (Fragmented of cause) */
        *frag_info      = 3;
        *frag_info_mask = 3;
        break;

    case CTC_IP_FRAG_YES:
        /* Any Fragmented */
        *frag_info      = 1;
        *frag_info_mask = 1;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_check_vlan_edit(uint8 lchip, ctc_scl_vlan_edit_t*   p_sys, uint8* do_nothing)
{
    /* check tag op */
    CTC_MAX_VALUE_CHECK(p_sys->vlan_domain, CTC_SCL_VLAN_DOMAIN_MAX - 1);
    CTC_MAX_VALUE_CHECK(p_sys->stag_op, CTC_VLAN_TAG_OP_MAX - 1);
    CTC_MAX_VALUE_CHECK(p_sys->ctag_op, CTC_VLAN_TAG_OP_MAX - 1);

    if ((CTC_VLAN_TAG_OP_NONE == p_sys->stag_op)
        && (CTC_VLAN_TAG_OP_NONE == p_sys->ctag_op))
    {
        *do_nothing = 1;
        return CTC_E_NONE;
    }

    *do_nothing = 0;

    if ((CTC_VLAN_TAG_OP_ADD == p_sys->stag_op) \
        || (CTC_VLAN_TAG_OP_REP == p_sys->stag_op)
        || (CTC_VLAN_TAG_OP_REP_OR_ADD == p_sys->stag_op))
    {
        /* check stag select */
        if ((p_sys->svid_sl > CTC_VLAN_TAG_SL_MAX)    \
            || (p_sys->scos_sl > CTC_VLAN_TAG_SL_MAX) \
            || (p_sys->scfi_sl > CTC_VLAN_TAG_SL_MAX))
        {
            return CTC_E_SCL_VLAN_EDIT_INVALID;
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->svid_sl)
        {
            if (p_sys->svid_new > CTC_MAX_VLAN_ID)
            {
                return CTC_E_VLAN_INVALID_VLAN_ID;
            }
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->scos_sl)
        {
            CTC_COS_RANGE_CHECK(p_sys->scos_new);
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->scfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_sys->scfi_new, 1);
        }
    }

    if ((CTC_VLAN_TAG_OP_ADD == p_sys->ctag_op) \
        || (CTC_VLAN_TAG_OP_REP == p_sys->ctag_op)
        || (CTC_VLAN_TAG_OP_REP_OR_ADD == p_sys->ctag_op))
    {
        /* check ctag select */
        if ((p_sys->cvid_sl > CTC_VLAN_TAG_SL_MAX)    \
            || (p_sys->ccos_sl > CTC_VLAN_TAG_SL_MAX) \
            || (p_sys->ccfi_sl > CTC_VLAN_TAG_SL_MAX))
        {
            return CTC_E_SCL_VLAN_EDIT_INVALID;
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->cvid_sl)
        {
            if (p_sys->cvid_new > CTC_MAX_VLAN_ID)
            {
                return CTC_E_VLAN_INVALID_VLAN_ID;
            }
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->ccos_sl)
        {
            CTC_COS_RANGE_CHECK(p_sys->ccos_new);
        }
        if (CTC_VLAN_TAG_SL_NEW == p_sys->ccfi_sl)
        {
            CTC_MAX_VALUE_CHECK(p_sys->ccfi_new, 1);
        }
    }
    CTC_MAX_VALUE_CHECK(p_sys->tpid_index, 3);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_map_igs_action(uint8 lchip,
                                   ctc_scl_igs_action_t* p_ctc_action,
                                   sys_scl_sw_igs_action_t* pia,
                                   sys_scl_sw_vlan_edit_t* pv)
{

    uint16          policer_ptr;
    uint32          flag;
    uint8           is_bwp;
    uint8           triple_play;
    uint32          dsfwd_offset;
    ctc_chip_device_info_t dev_info;
    sys_nh_info_com_t* p_nhinfo = NULL;
    sys_nh_info_mcast_t* p_mcast_info = NULL;

    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(pia);

    SYS_SCL_DBG_FUNC();



    sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
    sys_goldengate_chip_get_device_info(lchip, &dev_info);

    /* do disrad, deny bridge, copy to copy, aging when install */

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID)) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD)
            + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID)) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    /* bind en collision */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
            || (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
            || (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
            || (CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN)))
        {
            return CTC_E_SCL_FLAG_COLLISION;
        }
    }

    /* user vlan ptr collision */
    if (((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
            + (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
        && (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM))
    && (!(CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM))))
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if (((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
         + (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))) >= 2)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    flag = p_ctc_action->flag;

    if(flag & (CTC_SCL_IGS_ACTION_FLAG_AGING + CTC_SCL_IGS_ACTION_FLAG_VPLS + CTC_SCL_IGS_ACTION_FLAG_IGMP_SNOOPING))
    {
        return CTC_E_NOT_SUPPORT;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr
                             (lchip, p_ctc_action->stats_id, CTC_STATS_STATSID_TYPE_SCL, &pia->chip.stats_ptr));
        pia->stats_id = p_ctc_action->stats_id;
    }



    /* service flow policer */
    if (p_ctc_action->policer_id > 0)
    {
        CTC_ERROR_RETURN(sys_goldengate_qos_policer_index_get(lchip, p_ctc_action->policer_id, &policer_ptr, &is_bwp, &triple_play));
        if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
        {
            return CTC_E_INVALID_PARAM;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
        {
            pia->bind.bds.service_policer_valid = 1;

            /* check chip version */
            if (dev_info.version_id <= 1)
            {
                if (is_bwp && (!triple_play))
                {
                    pia->bind.bds.service_policer_mode = 0;
                }
                else
                {
                    pia->bind.bds.service_policer_mode = 1;
                }
            }
            else
            {
                if (triple_play)
                {
                    pia->bind.bds.service_policer_mode = 1;
                }
                else
                {
                    pia->bind.bds.service_policer_mode = 0;
                }
            }
        }
        else
        {
            if (is_bwp || triple_play)
            {
                return CTC_E_INVALID_PARAM;
            }
        }
        pia->chip.service_policer_ptr = policer_ptr;
        pia->service_policer_id = p_ctc_action->policer_id;
    }

     if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT)
        && CTC_FLAG_ISSET(p_ctc_action->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
    {
        p_ctc_action->logic_port.logic_port = p_ctc_action->logic_port.logic_port;
        pia->binding_en                    = 0;
        pia->binding_mac_sa                = 0;
        pia->bind.bds.service_acl_qos_en = 1;
     }

    /* priority */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        pia->priority = p_ctc_action->priority;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_COLOR))
    {
        CTC_NOT_ZERO_CHECK(p_ctc_action->color);
        CTC_MAX_VALUE_CHECK(p_ctc_action->color, MAX_CTC_QOS_COLOR - 1);
        pia->color = p_ctc_action->color;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, SYS_QOS_CLASS_PRIORITY_MAX);
        pia->priority = p_ctc_action->priority;
        CTC_NOT_ZERO_CHECK(p_ctc_action->color);
        CTC_MAX_VALUE_CHECK(p_ctc_action->color, MAX_CTC_QOS_COLOR - 1);
        pia->color = p_ctc_action->color;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_STP_ID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->stp_id, 127);
        pia->stp_id= p_ctc_action->stp_id;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->dscp, 0x3F);
        pia->dscp = p_ctc_action->dscp;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        SYS_L2_FID_CHECK(p_ctc_action->fid);
        pia->fid      = p_ctc_action->fid;
        pia->fid_type = 1;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        pia->fid      = p_ctc_action->vrf_id;
        pia->fid_type = 0;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        pia->user_vlan_ptr = p_ctc_action->user_vlanptr;
    }

    {
        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
        {
            if (p_ctc_action->aps.protected_vlan_valid)
            {
                CTC_VLAN_RANGE_CHECK(p_ctc_action->aps.protected_vlan);
            }
            pia->user_vlan_ptr = p_ctc_action->aps.protected_vlan;

            pia->bind.bds.aps_select_valid = 1;

            SYS_APS_GROUP_ID_CHECK(p_ctc_action->aps.aps_select_group_id);
            CTC_MAX_VALUE_CHECK(p_ctc_action->aps.is_working_path, 1);
            pia->bind.bds.aps_select_group_id        = p_ctc_action->aps.aps_select_group_id;
            pia->bind.bds.aps_select_protecting_path = !p_ctc_action->aps.is_working_path;
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
        {
            pia->nh_id = p_ctc_action->nh_id;

            CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo_by_nhid(lchip, pia->nh_id, &p_nhinfo));
            CTC_PTR_VALID_CHECK(p_nhinfo);

            if (SYS_NH_TYPE_MCAST == p_nhinfo->hdr.nh_entry_type)
            {
                p_mcast_info = (sys_nh_info_mcast_t*)p_nhinfo;
                pia->bypass_all = p_mcast_info->is_mirror ? 1 : 0;
            }

            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, pia->nh_id, &dsfwd_offset));

            pia->chip.offset_array = dsfwd_offset;

            if(0 == p_ctc_action->user_vlanptr)
            {
                pia->user_vlan_ptr = SYS_SCL_BY_PASS_VLAN_PTR;
            }
        }

        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
        {
            SYS_LOGIC_PORT_CHECK(p_ctc_action->logic_port.logic_port);
            CTC_MAX_VALUE_CHECK(p_ctc_action->logic_port.logic_port_type, 1);
            pia->bind.bds.logic_src_port  = p_ctc_action->logic_port.logic_port;
            pia->bind.bds.logic_port_type = p_ctc_action->logic_port.logic_port_type;
        }


        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
        {
            pia->binding_en = 1;
            pia->bind_type  = p_ctc_action->bind.type;
            switch (p_ctc_action->bind.type)
            {
            case CTC_SCL_BIND_TYPE_PORT:
                CTC_GLOBAL_PORT_CHECK(p_ctc_action->bind.gport);
                pia->bind.gport = p_ctc_action->bind.gport; /*SYS_MAP_CTC_GPORT_TO_DRV_GPORT() & 0x3FFF;*/
                break;
            case CTC_SCL_BIND_TYPE_VLAN:
                CTC_VLAN_RANGE_CHECK(p_ctc_action->bind.vlan_id);
                pia->bind.vlan_id = p_ctc_action->bind.vlan_id;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA:
                pia->bind.ipv4_sa = p_ctc_action->bind.ipv4_sa;
                break;
            case CTC_SCL_BIND_TYPE_IPV4SA_VLAN:
                CTC_VLAN_RANGE_CHECK(p_ctc_action->bind.vlan_id);
                pia->bind.ip_vlan.ipv4_sa = p_ctc_action->bind.ipv4_sa;
                pia->bind.ip_vlan.vlan_id = p_ctc_action->bind.vlan_id;
                break;
            case CTC_SCL_BIND_TYPE_MACSA:
                pia->binding_mac_sa = 1;
                SCL_SET_MAC(pia->bind.mac_sa, p_ctc_action->bind.mac_sa);
                break;
            default:
                return CTC_E_INVALID_PARAM;
            }
        }
    }


    /* map vlan edit */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 no_vlan_edit = 1;
        CTC_ERROR_RETURN(_sys_goldengate_scl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &no_vlan_edit));
        pv->stag_op = p_ctc_action->vlan_edit.stag_op;
        pv->ctag_op = p_ctc_action->vlan_edit.ctag_op;
        pv->svid_sl = p_ctc_action->vlan_edit.svid_sl;
        pv->scos_sl = p_ctc_action->vlan_edit.scos_sl;
        pv->scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
        pv->cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
        pv->ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
        pv->ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;

        pia->svid = p_ctc_action->vlan_edit.svid_new;
        pia->scos = p_ctc_action->vlan_edit.scos_new;
        pia->scfi = p_ctc_action->vlan_edit.scfi_new;
        pia->cvid = p_ctc_action->vlan_edit.cvid_new;
        pia->ccos = p_ctc_action->vlan_edit.ccos_new;
        pia->ccfi = p_ctc_action->vlan_edit.ccfi_new;

        pv->vlan_domain = p_ctc_action->vlan_edit.vlan_domain;
        pv->tpid_index = p_ctc_action->vlan_edit.tpid_index;
    }

    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM))
    {
        CTC_SET_FLAG(flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM);
        if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM))
        {
            if ((p_ctc_action->l2vpn_oam_id != p_ctc_action->fid)
                || (p_ctc_action->l2vpn_oam_id > 0x1FFF))
            {
                return CTC_E_INVALID_PARAM;
            }
            CTC_SET_FLAG(flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM);
        }
        else
        {
            if (p_ctc_action->l2vpn_oam_id > 0xFFF)
            {
                return CTC_E_INVALID_PARAM;
            }
            CTC_UNSET_FLAG(flag, CTC_SCL_IGS_ACTION_FLAG_VPLS_OAM);
        }
        pia->l2vpn_oam_id = p_ctc_action->l2vpn_oam_id;
    }



    pia->flag     = flag;
    pia->sub_flag = p_ctc_action->sub_flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_egs_action(uint8 lchip, ctc_scl_egs_action_t* p_ctc_action,
                                   sys_scl_sw_egs_action_t* p_sys_action)
{
    uint32 flag;

    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_SCL_DBG_FUNC();

    flag = p_ctc_action->flag;

    /* do discard when install*/
    /* do aging when install*/
    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr
                             (lchip, p_ctc_action->stats_id, CTC_STATS_STATSID_TYPE_SCL, &p_sys_action->chip.stats_ptr));
        p_sys_action->stats_id = p_ctc_action->stats_id;
    }

    /* vlan edit */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        uint8 do_nothing;
        CTC_ERROR_RETURN(_sys_goldengate_scl_check_vlan_edit(lchip, &p_ctc_action->vlan_edit, &do_nothing));

        if (do_nothing)
        {
            CTC_UNSET_FLAG(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT);
        }
        else
        {
            p_sys_action->stag_op = p_ctc_action->vlan_edit.stag_op;
            p_sys_action->ctag_op = p_ctc_action->vlan_edit.ctag_op;
            p_sys_action->svid_sl = p_ctc_action->vlan_edit.svid_sl;
            p_sys_action->scos_sl = p_ctc_action->vlan_edit.scos_sl;
            p_sys_action->scfi_sl = p_ctc_action->vlan_edit.scfi_sl;
            p_sys_action->cvid_sl = p_ctc_action->vlan_edit.cvid_sl;
            p_sys_action->ccos_sl = p_ctc_action->vlan_edit.ccos_sl;
            p_sys_action->ccfi_sl = p_ctc_action->vlan_edit.ccfi_sl;

            p_sys_action->svid = p_ctc_action->vlan_edit.svid_new;
            p_sys_action->scos = p_ctc_action->vlan_edit.scos_new;
            p_sys_action->tpid_index = p_ctc_action->vlan_edit.tpid_index;

            if ((CTC_VLAN_TAG_OP_DEL == p_ctc_action->vlan_edit.stag_op) && (CTC_VLAN_TAG_SL_NEW != p_ctc_action->vlan_edit.scfi_sl))
            {
                p_sys_action->scfi = 1;  /* default 1: clear stag info for egress acl/ipfix lookup */
            }
            else
            {
                p_sys_action->scfi = p_ctc_action->vlan_edit.scfi_new;
            }

            p_sys_action->cvid = p_ctc_action->vlan_edit.cvid_new;
            p_sys_action->ccos = p_ctc_action->vlan_edit.ccos_new;
            p_sys_action->ccfi = p_ctc_action->vlan_edit.ccfi_new;
        }
    }



    p_sys_action->flag = flag;


    return CTC_E_NONE;
}

/* later consider */
STATIC int32
_sys_goldengate_scl_map_flow_action(uint8 lchip, ctc_scl_flow_action_t* p_ctc_action,
                                    sys_scl_sw_flow_action_t* p_sys_action)
{
    uint32 flag;
    uint16 policer_ptr;
    uint8 is_bwp;
    uint8 triple_play;
    uint32 dsfwd_offset;
    uint32 cmd = 0;
    uint32 move_bit = 0;
    uint32 value = 0;
    sys_nh_info_dsnh_t nhinfo;

    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_SCL_DBG_FUNC();

    flag = p_ctc_action->flag;
    p_sys_action->flag = p_ctc_action->flag;

    if ((CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_METADATA)
         + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_FID)
         + CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_VRFID)) > 1)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_FLOW_ACTION_FLAG_STATS))
    {
        CTC_ERROR_RETURN(sys_goldengate_stats_get_statsptr
                             (lchip, p_ctc_action->stats_id, CTC_STATS_STATSID_TYPE_SCL, &p_sys_action->chip.stats_ptr));
        p_sys_action->stats_id = p_ctc_action->stats_id;
    }

     /* micro flow policer */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        CTC_NOT_EQUAL_CHECK(p_ctc_action->micro_policer_id, 0);

        CTC_ERROR_RETURN(sys_goldengate_qos_policer_index_get(lchip, p_ctc_action->micro_policer_id, &policer_ptr, &is_bwp, &triple_play));


        if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
        {
            return CTC_E_INVALID_PARAM;
        }

        if(is_bwp || triple_play)
        {
            return CTC_E_INVALID_PARAM;
        }

        p_sys_action->chip.micro_policer_ptr = policer_ptr;

        p_sys_action->micro_policer_id = p_ctc_action->micro_policer_id;
    }

    /* macro flow policer */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        CTC_NOT_EQUAL_CHECK(p_ctc_action->macro_policer_id, 0);

        CTC_ERROR_RETURN(sys_goldengate_qos_policer_index_get(lchip, p_ctc_action->macro_policer_id, &policer_ptr, &is_bwp, &triple_play));

        if ((policer_ptr > 0x1FFF) || (policer_ptr == 0))
        {
            return CTC_E_INVALID_PARAM;
        }

        if(is_bwp || triple_play)
        {
            return CTC_E_INVALID_PARAM;
        }

        p_sys_action->chip.macro_policer_ptr = policer_ptr;

        p_sys_action->macro_policer_id = p_ctc_action->macro_policer_id;
    }

     /* priority */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_PRIORITY))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->priority, 63);
        p_sys_action->priority = p_ctc_action->priority;
    }

    /* color */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_COLOR))
    {
        CTC_VALUE_RANGE_CHECK(p_ctc_action->color, 1, MAX_CTC_QOS_COLOR - 1);
        p_sys_action->color = p_ctc_action->color;
    }

    /* qos policy (trust) */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_TRUST))
    {
        uint32 trust;
        CTC_MAX_VALUE_CHECK(p_ctc_action->trust, CTC_QOS_TRUST_MAX - 1);
        sys_goldengate_common_map_qos_policy(lchip, p_ctc_action->trust, &trust);
        p_sys_action->qos_policy = trust;
    }

    /* ds forward */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_REDIRECT))
    {
        p_sys_action->nh_id = p_ctc_action->nh_id;

        /* get nexthop information */
        sal_memset(&nhinfo, 0, sizeof(sys_nh_info_dsnh_t));
        CTC_ERROR_RETURN(sys_goldengate_nh_get_nhinfo(lchip, p_ctc_action->nh_id, &nhinfo));

        /* for scl ecmp */
        if (nhinfo.ecmp_valid)
        {
            p_sys_action->ecmp_group_id = nhinfo.ecmp_group_id;
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, p_sys_action->nh_id, &dsfwd_offset));
            p_sys_action->chip.fwdptr = dsfwd_offset;
        }
    }

    /* acl hash */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_HASH_EN))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->acl.acl_hash_lkup_type, CTC_ACL_HASH_LKUP_TYPE_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->acl.acl_hash_field_sel_id, 15);
        p_sys_action->acl_flow_hash_type = p_ctc_action->acl.acl_hash_lkup_type;
        p_sys_action->acl_flow_hash_field_sel = p_ctc_action->acl.acl_hash_field_sel_id;
    }

    /* acl tcam */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_TCAM_EN))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->acl.acl_tcam_lkup_type, CTC_ACL_TCAM_LKUP_TYPE_MAX - 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->acl.acl_tcam_lkup_class_id, 0xFF);
        p_sys_action->acl_flow_tcam_lookup_type = p_ctc_action->acl.acl_tcam_lkup_type;
        p_sys_action->acl_flow_tcam_label = p_ctc_action->acl.acl_tcam_lkup_class_id;
    }

    /* metadata */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_METADATA))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->metadata, 0x3FFF);
        p_sys_action->metadata = p_ctc_action->metadata;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_METADATA;
    }
    else if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_FID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->fid, 0x3FFF);
        p_sys_action->fid = p_ctc_action->fid;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_FID;
    }
    else if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_VRFID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->vrf_id, 0x3FFF);
        p_sys_action->vrfid = p_ctc_action->vrf_id;
        p_sys_action->metadata_type = CTC_METADATA_TYPE_VRFID;
    }

    /* ipfix */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_OVERWRITE_PORT_IPFIX))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->ipfix_lkup_type, 3);
        CTC_MAX_VALUE_CHECK(p_ctc_action->ipfix_field_sel_id, 15);
        p_sys_action->ipfix_hash_type = p_ctc_action->ipfix_lkup_type;
        p_sys_action->ipfix_hash_field_sel = p_ctc_action->ipfix_field_sel_id;
    }

    /* force decap */
    if (CTC_FLAG_ISSET(p_ctc_action->flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_DECAP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_action->force_decap.force_decap_en, 1);
        CTC_MAX_VALUE_CHECK(p_ctc_action->force_decap.offset_base_type, CTC_SCL_OFFSET_BASE_TYPE_MAX - 1);
        /* removeByteShift init to 1, the max extend length is 30 bytes */
        cmd = DRV_IOR(IpeSclFlowCtl_t, IpeSclFlowCtl_removeByteShift_f);
        DRV_IOCTL(lchip, 0, cmd, &move_bit);
        value = 1 << move_bit;
        CTC_MAX_VALUE_CHECK(p_ctc_action->force_decap.ext_offset, 15 * value);
        if(p_ctc_action->force_decap.ext_offset % 2)
        {
            return CTC_E_INVALID_PARAM;
        }
        CTC_MAX_VALUE_CHECK(p_ctc_action->force_decap.payload_type, 7);

        p_sys_action->inner_lookup = p_ctc_action->force_decap.force_decap_en;

        if (CTC_SCL_OFFSET_BASE_TYPE_L2 == p_ctc_action->force_decap.offset_base_type)
        {
            p_sys_action->payload_offset_start_point = 1;
        }
        else if (CTC_SCL_OFFSET_BASE_TYPE_L3 == p_ctc_action->force_decap.offset_base_type)
        {
            p_sys_action->payload_offset_start_point = 2;
        }
        else if (CTC_SCL_OFFSET_BASE_TYPE_L4 == p_ctc_action->force_decap.offset_base_type)
        {
            p_sys_action->payload_offset_start_point = 3;
        }

        p_sys_action->packet_type = p_ctc_action->force_decap.payload_type;
        p_sys_action->payload_offset = (p_ctc_action->force_decap.ext_offset) / value;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_map_tunnel_action(uint8 lchip, sys_scl_tunnel_action_t* p_ctc_action,
                                      sys_scl_tunnel_action_t* p_sys_action)
{
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    /* just memory copy. tunnel action is internal */
    sal_memcpy(p_sys_action, p_ctc_action, sizeof(sys_scl_tunnel_action_t));
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_action(uint8 lchip,
                               sys_scl_action_t* p_ctc_action,
                               sys_scl_sw_action_t* p_sys_action,
                               sys_scl_sw_vlan_edit_t* p_sw_vlan_edit)
{
    CTC_PTR_VALID_CHECK(p_ctc_action);
    CTC_PTR_VALID_CHECK(p_sys_action);

    SYS_SCL_DBG_FUNC();

    switch (p_ctc_action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_igs_action(lchip, &p_ctc_action->u.igs_action, &p_sys_action->u0.ia, p_sw_vlan_edit));
        break;

    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_egs_action(lchip, &p_ctc_action->u.egs_action, &p_sys_action->u0.ea));
        break;

    case SYS_SCL_ACTION_TUNNEL:
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_tunnel_action(lchip, &p_ctc_action->u.tunnel_action, &p_sys_action->u0.ta));
        break;

    case SYS_SCL_ACTION_FLOW:
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_flow_action(lchip, &p_ctc_action->u.flow_action, &p_sys_action->u0.fa));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_check_mac_key(uint8 lchip, uint32 flag)
{
    uint32 obsolete_flag = 0;
    uint32 sflag         = 0;
    uint32 cflag         = 0;

if (!p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en)
{
    sflag = SCL_SUM(CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_VALID);

    cflag = SCL_SUM(CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI,
                    CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_VALID);

    if ((flag & sflag) && (flag & cflag))
    {
        return CTC_E_INVALID_PARAM;
    }
}

    obsolete_flag = SCL_SUM(lchip, CTC_SCL_TCAM_MAC_KEY_FLAG_L3_TYPE);

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_check_ipv4_key(uint8 lchip, uint32 flag, uint32 sub_flag, uint8 key_size)
{
    uint32 obsolete_flag = 0;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    obsolete_flag = SCL_SUM(lchip, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE);

    if (CTC_SCL_KEY_SIZE_SINGLE == key_size)
    {
        obsolete_flag = SCL_SUM(CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_CVLAN,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_COS,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_SVLAN,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_COS,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_CFI,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_CFI,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_VALID,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_VALID,
                                CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_TYPE);
    }

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_check_ipv6_key(uint8 lchip, uint32 flag, uint32 sub_flag)
{
    uint32 obsolete_flag = 0;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP)
        && CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE))
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    if ((CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY)
         + CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI)) > 1)
    {
        return CTC_E_SCL_FLAG_COLLISION;
    }

    obsolete_flag = SCL_SUM(lchip, CTC_SCL_TCAM_IPV6_KEY_FLAG_ETH_TYPE,
                            CTC_SCL_TCAM_IPV6_KEY_FLAG_L3_TYPE);

    if (flag & obsolete_flag)
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_map_tcam_mac_key(uint8 lchip, ctc_scl_tcam_mac_key_t* p_ctc_key,
                                     sys_scl_sw_tcam_key_mac_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_scl_check_mac_key(lchip, p_ctc_key->flag));

    p_sys_key->flag = p_ctc_key->flag;
    flag            = p_ctc_key->flag;

    /* port */
    if (CTC_FIELD_PORT_TYPE_NONE != p_ctc_key->port_data.type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_tcam_key_port(lchip, p_ctc_key->port_data, p_ctc_key->port_mask, &p_sys_key->port_info));
    }
    /* mac da */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    /* mac sa */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    /* cvlan */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    /* ctag cos */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    /* ctag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    /* svlan */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    /* stag cos */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    /* stag cfi */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    /* l2 type */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    /* eth type */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_MAC_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->eth_type      = p_ctc_key->eth_type;
        p_sys_key->eth_type_mask = p_ctc_key->eth_type_mask;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_map_tcam_ipv4_key(uint8 lchip, ctc_scl_tcam_ipv4_key_t* p_ctc_key,
                                      sys_scl_sw_tcam_key_ipv4_t* p_sys_key)
{
    uint32 flag          = 0;
    uint32 sub_flag      = 0;
    uint32 l3_type_match = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_scl_check_ipv4_key(lchip, p_ctc_key->flag, p_ctc_key->sub_flag, p_ctc_key->key_size));

    p_sys_key->key_size  = p_ctc_key->key_size;
    p_sys_key->flag      = p_ctc_key->flag;
    p_sys_key->sub_flag  = p_ctc_key->sub_flag;

    flag      = p_ctc_key->flag;
    sub_flag  = p_ctc_key->sub_flag;

    /* port */
    if (CTC_FIELD_PORT_TYPE_NONE != p_ctc_key->port_data.type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_tcam_key_port(lchip, p_ctc_key->port_data, p_ctc_key->port_mask, &p_sys_key->port_info));
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = (p_ctc_key->l2_type_mask) & 0xF;
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = (p_ctc_key->l3_type_mask) & 0xF;
    }
    else /* if l3_type not set, give it a default value.*/
    {
        p_sys_key->l3_type      = CTC_PARSER_L3_TYPE_IPV4;
        p_sys_key->l3_type_mask = 0xF;
    }

    l3_type_match = p_sys_key->l3_type & p_sys_key->l3_type_mask;

    /* l3_type is not supposed to match more than once. --> Fullfill this will make things easy.
     * But still, l3_type_mask can be any value, if user wants to only match it. --> Doing this make things complicate.
     * If further l3_info requires to be matched, only one l3_info is promised to work.
     */
    p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_NONE;
    if (0xF == p_sys_key->l3_type_mask)
    {
        if (l3_type_match == CTC_PARSER_L3_TYPE_IPV4)
        {
            p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_IPV4;
            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
            {
                p_sys_key->u0.ip.ip_da      = p_ctc_key->ip_da;
                p_sys_key->u0.ip.ip_da_mask = p_ctc_key->ip_da_mask;
            }

            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
            {
                p_sys_key->u0.ip.ip_sa      = p_ctc_key->ip_sa;
                p_sys_key->u0.ip.ip_sa_mask = p_ctc_key->ip_sa_mask;
            }

            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_FRAG))
            {
                uint8 d;
                uint8 m;
                CTC_ERROR_RETURN(_sys_goldengate_scl_get_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
                p_sys_key->u0.ip.frag_info      = d;
                p_sys_key->u0.ip.frag_info_mask = m;
            }

            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_OPTION))
            {
                CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
                p_sys_key->u0.ip.ip_option = p_ctc_key->ip_option;
            }

            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ECN))
            {
                CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 3);
                p_sys_key->u0.ip.ecn      = p_ctc_key->ecn;
                p_sys_key->u0.ip.ecn_mask = p_ctc_key->ecn_mask;
            }

            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_DSCP))
            {
                CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
                p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp);
                p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask) & 0x3F;
            }
            else if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_PRECEDENCE))
            {
                CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
                p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
                p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
            }
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_ARP)
        {
            p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_ARP;
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_OP_CODE))
            {
                p_sys_key->u0.arp.op_code      = p_ctc_key->arp_op_code;
                p_sys_key->u0.arp.op_code_mask = p_ctc_key->arp_op_code_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_SENDER_IP))
            {
                p_sys_key->u0.arp.sender_ip      = p_ctc_key->sender_ip;
                p_sys_key->u0.arp.sender_ip_mask = p_ctc_key->sender_ip_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_TARGET_IP))
            {
                p_sys_key->u0.arp.target_ip      = p_ctc_key->target_ip;
                p_sys_key->u0.arp.target_ip_mask = p_ctc_key->target_ip_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ARP_PROTOCOL_TYPE))
            {
                p_sys_key->u0.arp.protocol_type      = p_ctc_key->arp_protocol_type;
                p_sys_key->u0.arp.protocol_type_mask = p_ctc_key->arp_protocol_type_mask;
            }
        }
        else if ((l3_type_match == CTC_PARSER_L3_TYPE_MPLS) ||
                 (l3_type_match == CTC_PARSER_L3_TYPE_MPLS_MCAST))
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_FCOE)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_TRILL)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_ETHER_OAM)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_SLOW_PROTO)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_CMAC)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_PTP)
        {
        }
        else if (l3_type_match == CTC_PARSER_L3_TYPE_NONE)
        {
            p_sys_key->unique_l3_type = CTC_PARSER_L3_TYPE_NONE;
            if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_ETH_TYPE))
            {
                p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
                p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
            }
        }
        else if ((l3_type_match == CTC_PARSER_L3_TYPE_RSV_USER_FLEXL3) ||
                 (l3_type_match == CTC_PARSER_L3_TYPE_IP) ||
                 (l3_type_match == CTC_PARSER_L3_TYPE_IPV6) ||
                 (l3_type_match == CTC_PARSER_L3_TYPE_RSV_USER_DEFINE0) ||
                 (l3_type_match == CTC_PARSER_L3_TYPE_RSV_USER_DEFINE1))
        {
            /* never do anything */
        }
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL))
    {
        switch (p_ctc_key->l4_protocol & p_ctc_key->l4_protocol_mask)
        {
        case SYS_L4_PROTOCOL_IPV4_ICMP:
            p_sys_key->u0.ip.flag_l4info_mapped = 1;
            p_sys_key->u0.ip.l4info_mapped      = p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask = p_ctc_key->l4_protocol_mask << 4;

            /* l4_src_port for flex-l4 (including icmp), it's type|code */

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->u0.ip.l4_src_port      |= (p_ctc_key->icmp_type) << 8;
                p_sys_key->u0.ip.l4_src_port_mask |= (p_ctc_key->icmp_type_mask) << 8;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->u0.ip.l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->u0.ip.l4_src_port_mask |= (p_ctc_key->icmp_code_mask) & 0x00FF;
            }
            break;
        case SYS_L4_PROTOCOL_IPV4_IGMP:
            p_sys_key->u0.ip.flag_l4info_mapped = 1;
            p_sys_key->u0.ip.l4info_mapped      = p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask = p_ctc_key->l4_protocol_mask << 4;

            /* l4_src_port for flex-l4 (including igmp), it's type|..*/
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_IGMP_TYPE))
            {
                p_sys_key->u0.ip.l4_src_port      |= (p_ctc_key->igmp_type << 8) & 0xFF00;
                p_sys_key->u0.ip.l4_src_port_mask |= (p_ctc_key->igmp_type_mask << 8) & 0xFF00;
            }
            break;
        case SYS_L4_PROTOCOL_TCP:
        case SYS_L4_PROTOCOL_UDP:
            if (SYS_L4_PROTOCOL_TCP == p_ctc_key->l4_protocol)
            {
                p_sys_key->u0.ip.flag_l4_type          = 1;
                p_sys_key->u0.ip.l4_type               = CTC_PARSER_L4_TYPE_TCP;
                p_sys_key->u0.ip.l4_type_mask          = 15;
                p_sys_key->u0.ip.l4_compress_type      = 2; /*CTC_PARSER_L4_TYPE_TCP;*/
                p_sys_key->u0.ip.l4_compress_type_mask = 3;
            }
            else
            {
                p_sys_key->u0.ip.flag_l4_type          = 1;
                p_sys_key->u0.ip.l4_type               = CTC_PARSER_L4_TYPE_UDP;
                p_sys_key->u0.ip.l4_type_mask          = 15;
                p_sys_key->u0.ip.l4_compress_type      = 1; /*CTC_PARSER_L4_TYPE_UDP;*/
                p_sys_key->u0.ip.l4_compress_type_mask = 3;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI))
            {
                CTC_MAX_VALUE_CHECK(p_ctc_key->vni, 0xFFFFFF);
                CTC_MAX_VALUE_CHECK(p_ctc_key->vni_mask, 0xFFFFFF);

                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->vni & 0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->vni_mask & 0xFFFF;

                p_sys_key->u0.ip.l4_src_port      = (p_ctc_key->vni >> 16) & 0xFF;
                p_sys_key->u0.ip.l4_src_port_mask = (p_ctc_key->vni_mask>> 16) & 0xFF;

                p_sys_key->u0.ip.flag_l4info_mapped = 1;
                p_sys_key->u0.ip.l4info_mapped      |= SYS_L4_USER_UDP_TYPE_VXLAN << 8;
                p_sys_key->u0.ip.l4info_mapped_mask |= 0xF << 8;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            {
                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
            break;
        case SYS_L4_PROTOCOL_GRE:
            p_sys_key->u0.ip.flag_l4_type          = 1;
            p_sys_key->u0.ip.l4_type               = CTC_PARSER_L4_TYPE_GRE;
            p_sys_key->u0.ip.l4_type_mask          = 15;
            p_sys_key->u0.ip.l4_compress_type      = CTC_PARSER_L4_TYPE_GRE;
            p_sys_key->u0.ip.l4_compress_type_mask = 3;

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY))
            {
                p_sys_key->u0.ip.flag_l4info_mapped = 1;
                CTC_BIT_UNSET(p_sys_key->u0.ip.l4info_mapped, 11);
                CTC_BIT_SET(p_sys_key->u0.ip.l4info_mapped_mask, 11);

                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->gre_key >> 16;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->gre_key_mask >> 16;

                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->gre_key & 0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->gre_key_mask & 0xFFFF;
            }
            else if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY))
            {
                p_sys_key->u0.ip.flag_l4info_mapped = 1;
                p_sys_key->u0.ip.l4info_mapped      |= 1 << 11;
                p_sys_key->u0.ip.l4info_mapped_mask |= 1 << 11;

                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->gre_key >> 24;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->gre_key_mask >> 24;

                p_sys_key->u0.ip.l4_dst_port      = (p_ctc_key->gre_key >> 8)&0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = (p_ctc_key->gre_key_mask>> 8) & 0xFFFF;
            }
            break;
        case SYS_L4_PROTOCOL_SCTP:
            p_sys_key->u0.ip.flag_l4_type          = 1;
            p_sys_key->u0.ip.l4_type               = CTC_PARSER_L4_TYPE_SCTP;
            p_sys_key->u0.ip.l4_type_mask          = 15;
            p_sys_key->u0.ip.l4_compress_type      = 0;
            p_sys_key->u0.ip.l4_compress_type_mask = 3;

            p_sys_key->u0.ip.flag_l4info_mapped = 1;
            p_sys_key->u0.ip.l4info_mapped      = p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask = p_ctc_key->l4_protocol_mask << 4;

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))
            {
                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
            break;
        default:
            p_sys_key->u0.ip.flag_l4info_mapped  = 1;
            p_sys_key->u0.ip.l4info_mapped      |= p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask |= p_ctc_key->l4_protocol_mask << 4;
            break;
        }
    }



    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_map_tcam_vlan_key(uint8 lchip, ctc_scl_tcam_vlan_key_t* p_ctc_key,
                                      sys_scl_sw_tcam_key_vlan_t* p_sys_key)
{
    uint32 flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    p_sys_key->flag = p_ctc_key->flag;
    flag            = p_ctc_key->flag;

    /* port */
    if (CTC_FIELD_PORT_TYPE_NONE != p_ctc_key->port_data.type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_tcam_key_port(lchip, p_ctc_key->port_data, p_ctc_key->port_mask, &p_sys_key->port_info));
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CUSTOMER_ID))
    {
        p_sys_key->customer_id = p_ctc_key->customer_id;
        p_sys_key->customer_id_mask = p_ctc_key->customer_id_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_map_tcam_ipv6_key(uint8 lchip, ctc_scl_tcam_ipv6_key_t* p_ctc_key,
                                      sys_scl_sw_tcam_key_ipv6_t* p_sys_key)
{
    uint32 flag     = 0;
    uint32 sub_flag = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(_sys_goldengate_scl_check_ipv6_key(lchip, p_ctc_key->flag, p_ctc_key->sub_flag));

    flag                = p_ctc_key->flag;
    sub_flag            = p_ctc_key->sub_flag;

    /* port */
    if (CTC_FIELD_PORT_TYPE_NONE != p_ctc_key->port_data.type)
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_tcam_key_port(lchip, p_ctc_key->port_data, p_ctc_key->port_mask, &p_sys_key->port_info));
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_da, p_ctc_key->ip_da, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_da_mask, p_ctc_key->ip_da_mask, sizeof(ipv6_addr_t));
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
    {
        sal_memcpy(p_sys_key->u0.ip.ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));
        sal_memcpy(p_sys_key->u0.ip.ip_sa_mask, p_ctc_key->ip_sa_mask, sizeof(ipv6_addr_t));
    }

    /* ip fragment */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_FRAG))
    {
        uint8 d;
        uint8 m;
        CTC_ERROR_RETURN(_sys_goldengate_scl_get_ip_frag(lchip, p_ctc_key->ip_frag, &d, &m));
        p_sys_key->u0.ip.frag_info      = d;
        p_sys_key->u0.ip.frag_info_mask = m;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_DSCP))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 0x3F);
        p_sys_key->u0.ip.dscp      = p_ctc_key->dscp;
        p_sys_key->u0.ip.dscp_mask = p_ctc_key->dscp_mask;
    }
    else if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_PRECEDENCE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->dscp, 7);
        p_sys_key->u0.ip.dscp      = (p_ctc_key->dscp) << 3;
        p_sys_key->u0.ip.dscp_mask = (p_ctc_key->dscp_mask & 0x7) << 3;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_OPTION))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ip_option, 1);
        p_sys_key->u0.ip.ip_option = p_ctc_key->ip_option;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->flow_label, 0xFFFFF);
        p_sys_key->flow_label      = p_ctc_key->flow_label;
        p_sys_key->flow_label_mask = p_ctc_key->flow_label_mask & 0xFFFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_DA))
    {
        sal_memcpy(p_sys_key->mac_da, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_da_mask, p_ctc_key->mac_da_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_MAC_SA))
    {
        sal_memcpy(p_sys_key->mac_sa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
        sal_memcpy(p_sys_key->mac_sa_mask, p_ctc_key->mac_sa_mask, CTC_ETH_ADDR_LEN);
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
        p_sys_key->cvlan      = p_ctc_key->cvlan;
        p_sys_key->cvlan_mask = p_ctc_key->cvlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
        p_sys_key->ctag_cos      = p_ctc_key->ctag_cos;
        p_sys_key->ctag_cos_mask = (p_ctc_key->ctag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cfi, 0x1);
        p_sys_key->ctag_cfi = p_ctc_key->ctag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_CTAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_valid, 0x1);
        p_sys_key->ctag_valid = p_ctc_key->ctag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_VALID))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_valid, 0x1);
        p_sys_key->stag_valid = p_ctc_key->stag_valid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_ECN))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->ecn, 0x3);
        p_sys_key->u0.ip.ecn      = p_ctc_key->ecn;
        p_sys_key->u0.ip.ecn_mask = p_ctc_key->ecn_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_SVLAN))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
        p_sys_key->svlan      = p_ctc_key->svlan;
        p_sys_key->svlan_mask = p_ctc_key->svlan_mask & 0xFFF;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_COS))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
        p_sys_key->stag_cos      = p_ctc_key->stag_cos;
        p_sys_key->stag_cos_mask = (p_ctc_key->stag_cos_mask) & 0x7;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_STAG_CFI))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cfi, 0x1);
        p_sys_key->stag_cfi = p_ctc_key->stag_cfi;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_ETH_TYPE))
    {
        p_sys_key->u0.other.eth_type      = p_ctc_key->eth_type;
        p_sys_key->u0.other.eth_type_mask = p_ctc_key->eth_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L2_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l2_type, 0xF);
        p_sys_key->l2_type      = p_ctc_key->l2_type;
        p_sys_key->l2_type_mask = p_ctc_key->l2_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L3_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l3_type, 0xF);
        p_sys_key->l3_type      = p_ctc_key->l3_type;
        p_sys_key->l3_type_mask = p_ctc_key->l3_type_mask;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->l4_type, 0xF);
        p_sys_key->u0.ip.l4_type      = p_ctc_key->l4_type;
        p_sys_key->u0.ip.l4_type_mask = p_ctc_key->l4_type_mask;
    }

    /* layer 4 information */
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_PROTOCOL))
    {
        switch (p_ctc_key->l4_protocol)
        {
        case SYS_L4_PROTOCOL_IPV6_ICMP:
            p_sys_key->u0.ip.flag_l4info_mapped = 1;
            p_sys_key->u0.ip.l4info_mapped      = p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask = p_ctc_key->l4_protocol_mask << 4;

            /* icmp type */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_TYPE))
            {
                p_sys_key->u0.ip.l4_src_port      |= (p_ctc_key->icmp_type) << 8;
                p_sys_key->u0.ip.l4_src_port_mask |= (p_ctc_key->icmp_type_mask) << 8;
            }

            /* icmp code */
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_ICMP_CODE))
            {
                p_sys_key->u0.ip.l4_src_port      |= p_ctc_key->icmp_code & 0x00FF;
                p_sys_key->u0.ip.l4_src_port_mask |= (p_ctc_key->icmp_code_mask) & 0x00FF;
            }
            break;
        case SYS_L4_PROTOCOL_TCP:
        case SYS_L4_PROTOCOL_UDP:
            if (SYS_L4_PROTOCOL_TCP == p_ctc_key->l4_protocol) /* tcp upd, get help from l4_type. */
            {
                p_sys_key->u0.ip.l4_type      = CTC_PARSER_L4_TYPE_TCP;
                p_sys_key->u0.ip.l4_type_mask = 15;
                CTC_SET_FLAG(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE);
            }
            else
            {
                p_sys_key->u0.ip.l4_type      = CTC_PARSER_L4_TYPE_UDP;
                p_sys_key->u0.ip.l4_type_mask = 15;
                CTC_SET_FLAG(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE);
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI))
            {
                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->vni & 0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->vni_mask & 0xFFFF;

                p_sys_key->u0.ip.l4_src_port      = (p_ctc_key->vni >> 16) & 0xFF;
                p_sys_key->u0.ip.l4_src_port_mask = (p_ctc_key->vni_mask>> 16) & 0xFF;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
            {
                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
            break;
        case SYS_L4_PROTOCOL_GRE:
            p_sys_key->u0.ip.l4_type      = CTC_PARSER_L4_TYPE_GRE;
            p_sys_key->u0.ip.l4_type_mask = 15;
            CTC_SET_FLAG(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE);

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY))
            {
                p_sys_key->u0.ip.flag_l4info_mapped = 1;
                CTC_BIT_UNSET(p_sys_key->u0.ip.l4info_mapped, 11);
                CTC_BIT_SET(p_sys_key->u0.ip.l4info_mapped_mask, 11);

                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->gre_key >> 16;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->gre_key_mask >> 16;

                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->gre_key & 0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->gre_key_mask & 0xFFFF;
            }
            else if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY))
            {
                p_sys_key->u0.ip.flag_l4info_mapped = 1;
                p_sys_key->u0.ip.l4info_mapped      |= 1 << 11;
                p_sys_key->u0.ip.l4info_mapped_mask |= 1 << 11;

                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->gre_key >> 24;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->gre_key_mask >> 24;

                p_sys_key->u0.ip.l4_dst_port      = (p_ctc_key->gre_key >> 8)&0xFFFF;
                p_sys_key->u0.ip.l4_dst_port_mask = (p_ctc_key->gre_key_mask>> 8) & 0xFFFF;
            }
            break;
        case SYS_L4_PROTOCOL_SCTP:
            p_sys_key->u0.ip.l4_type      = CTC_PARSER_L4_TYPE_SCTP;
            p_sys_key->u0.ip.l4_type_mask = 15;
            CTC_SET_FLAG(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE);

            p_sys_key->u0.ip.flag_l4info_mapped = 1;
            p_sys_key->u0.ip.l4info_mapped      = p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask = p_ctc_key->l4_protocol_mask << 4;

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
            {
                p_sys_key->u0.ip.l4_src_port      = p_ctc_key->l4_src_port;
                p_sys_key->u0.ip.l4_src_port_mask = p_ctc_key->l4_src_port_mask;
            }

            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
            {
                p_sys_key->u0.ip.l4_dst_port      = p_ctc_key->l4_dst_port;
                p_sys_key->u0.ip.l4_dst_port_mask = p_ctc_key->l4_dst_port_mask;
            }
            break;
        default:
            p_sys_key->u0.ip.flag_l4info_mapped  = 1;
            p_sys_key->u0.ip.l4info_mapped      |= p_ctc_key->l4_protocol << 4;
            p_sys_key->u0.ip.l4info_mapped_mask |= p_ctc_key->l4_protocol_mask << 4;
            break;
        }
    }

    p_sys_key->flag = flag;
    p_sys_key->sub_flag = p_ctc_key->sub_flag;

    return CTC_E_NONE;
}

typedef struct
{
    uint16 gport;   /*gport or logic_port or class_id*/
    uint16 is_class;
    uint16 is_logic;
}sys_scl_port_t;


STATIC int32
_sys_goldengate_scl_get_gport(uint8 lchip, uint8 gport_type, uint16 gport, sys_scl_port_t* port)
{
    switch (gport_type)
    {
    case CTC_SCL_GPROT_TYPE_PORT:
        port->is_logic = 0;
        port->is_class = 0;
        break;
    case CTC_SCL_GPROT_TYPE_LOGIC_PORT:
        port->is_logic = 1;
        port->is_class = 0;
        break;
    case CTC_SCL_GPROT_TYPE_PORT_CLASS:
        port->is_logic = 0;
        port->is_class = 1;
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (port->is_class)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(gport);
    }
    else if (port->is_logic)
    {
        SYS_LOGIC_PORT_CHECK(gport);
    }
    else
    {
        CTC_GLOBAL_PORT_CHECK(gport);
    }
    port->gport = gport;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_hash_l2_key(uint8 lchip, ctc_scl_hash_l2_key_t* p_ctc_key,
                                          sys_scl_sw_hash_key_l2_t* p_sys_key)
{
    SclFlowHashFieldSelect_m   ds_sel;
    uint32 cmd = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_MAX_VALUE_CHECK(p_ctc_key->field_sel_id, 3);
    p_sys_key->field_sel = p_ctc_key->field_sel_id;

    cmd = DRV_IOR(SclFlowHashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_ctc_key->field_sel_id, cmd, &ds_sel));

    if (GetSclFlowHashFieldSelect(V, macDaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->macda, p_ctc_key->mac_da, CTC_ETH_ADDR_LEN);
    }

    if (GetSclFlowHashFieldSelect(V, macSaEn_f, &ds_sel))
    {
        sal_memcpy(p_sys_key->macsa, p_ctc_key->mac_sa, CTC_ETH_ADDR_LEN);
    }

    if (GetSclFlowHashFieldSelect(V, userIdLabelEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->gport, 0xFF);
        p_sys_key->class_id = p_ctc_key->gport;
    }

    if (GetSclFlowHashFieldSelect(V, etherTypeEn_f, &ds_sel))
    {
        p_sys_key->ether_type = p_ctc_key->eth_type;
    }

    if (GetSclFlowHashFieldSelect(V, stagCosEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->cos, 7);
        p_sys_key->scos = p_ctc_key->cos;
    }

    if (GetSclFlowHashFieldSelect(V, svlanIdEn_f, &ds_sel))
    {
        SYS_SCL_VLAN_ID_CHECK(p_ctc_key->vlan_id);
        p_sys_key->svid = p_ctc_key->vlan_id;
    }

    if (GetSclFlowHashFieldSelect(V, svlanIdValidEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->tag_valid, 1);
        p_sys_key->stag_exist = p_ctc_key->tag_valid;
    }

    if (GetSclFlowHashFieldSelect(V, stagCfiEn_f, &ds_sel))
    {
        CTC_MAX_VALUE_CHECK(p_ctc_key->cfi, 1);
        p_sys_key->scfi = p_ctc_key->cfi;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_hash_port_key(uint8 lchip, ctc_scl_hash_port_key_t* p_ctc_key,
                                      sys_scl_sw_hash_key_port_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);


    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;


    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_cvlan_key(uint8 lchip, ctc_scl_hash_port_cvlan_key_t* p_ctc_key,
                                            sys_scl_sw_hash_key_vlan_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->vid = p_ctc_key->cvlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_svlan_key(uint8 lchip, ctc_scl_hash_port_svlan_key_t* p_ctc_key,
                                            sys_scl_sw_hash_key_vlan_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    p_sys_key->vid = p_ctc_key->svlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_2vlan_key(uint8 lchip, ctc_scl_hash_port_2vlan_key_t* p_ctc_key,
                                            sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->svid = p_ctc_key->svlan;
    p_sys_key->cvid = p_ctc_key->cvlan;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_cvlan_cos_key(uint8 lchip, ctc_scl_hash_port_cvlan_cos_key_t* p_ctc_key,
                                                sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->cvlan);
    p_sys_key->cvid = p_ctc_key->cvlan;

    /* cos */
    CTC_MAX_VALUE_CHECK(p_ctc_key->ctag_cos, 0x7);
    p_sys_key->ccos = p_ctc_key->ctag_cos;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_svlan_cos_key(uint8 lchip, ctc_scl_hash_port_svlan_cos_key_t* p_ctc_key,
                                                sys_scl_sw_hash_key_vtag_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* vid */
    SYS_SCL_VLAN_ID_CHECK(p_ctc_key->svlan);
    p_sys_key->svid = p_ctc_key->svlan;

    /* cos */
    CTC_MAX_VALUE_CHECK(p_ctc_key->stag_cos, 0x7);
    p_sys_key->scos = p_ctc_key->stag_cos;

    /* dir */
    CTC_MAX_VALUE_CHECK(p_ctc_key->dir, CTC_BOTH_DIRECTION - 1);
    p_sys_key->dir = p_ctc_key->dir;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_mac_key(uint8 lchip, ctc_scl_hash_mac_key_t* p_ctc_key,
                                     sys_scl_sw_hash_key_mac_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* mac */
    SCL_SET_MAC(p_sys_key->mac, p_ctc_key->mac);

    /* mac is da */
    CTC_MAX_VALUE_CHECK(p_ctc_key->mac_is_da, 1);
    p_sys_key->use_macda = p_ctc_key->mac_is_da;

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_port_mac_key(uint8 lchip, ctc_scl_hash_port_mac_key_t* p_ctc_key,
                                          sys_scl_sw_hash_key_mac_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    /* mac */
    SCL_SET_MAC(p_sys_key->mac, p_ctc_key->mac);

    /* mac is da */
    CTC_MAX_VALUE_CHECK(p_ctc_key->mac_is_da, 1);
    p_sys_key->use_macda = p_ctc_key->mac_is_da;

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_map_hash_ipv4_key(uint8 lchip, ctc_scl_hash_ipv4_key_t* p_ctc_key,
                                      sys_scl_sw_hash_key_ipv4_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip_sa */
    p_sys_key->ip_sa = p_ctc_key->ip_sa;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_hash_port_ipv4_key(uint8 lchip, ctc_scl_hash_port_ipv4_key_t* p_ctc_key,
                                           sys_scl_sw_hash_key_ipv4_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip_sa */
    p_sys_key->ip_sa = p_ctc_key->ip_sa;

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_hash_port_ipv6_key(uint8 lchip, ctc_scl_hash_port_ipv6_key_t* p_ctc_key,
                                           sys_scl_sw_hash_key_ipv6_t* p_sys_key)
{
    sys_scl_port_t port;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip_sa */
    sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_gport(lchip, p_ctc_key->gport_type, p_ctc_key->gport, &port));
    p_sys_key->gport            = port.gport;
    p_sys_key->gport_is_classid = port.is_class;
    p_sys_key->gport_is_logic   = port.is_logic;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_map_hash_ipv6_key(uint8 lchip, ctc_scl_hash_ipv6_key_t* p_ctc_key,
                                      sys_scl_sw_hash_key_ipv6_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    /* ip sa */
    sal_memcpy(p_sys_key->ip_sa, p_ctc_key->ip_sa, sizeof(ipv6_addr_t));

    return CTC_E_NONE;
}


/* Tunnel key just memory copy */



STATIC int32
_sys_goldengate_scl_map_hash_tunnel_ipv4_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_key_t* p_ctc_key,
                                             sys_scl_hash_tunnel_ipv4_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_tunnel_ipv4_gre_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_gre_key_t* p_ctc_key,
                                                 sys_scl_hash_tunnel_ipv4_gre_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_tunnel_ipv4_rpf_key(uint8 lchip, sys_scl_hash_tunnel_ipv4_rpf_key_t* p_ctc_key,
                                                 sys_scl_hash_tunnel_ipv4_rpf_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_overlay_tunnel_v4_key(uint8 lchip, sys_scl_hash_overlay_tunnel_v4_key_t* p_ctc_key,
                                                 sys_scl_hash_overlay_tunnel_v4_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_overlay_tunnel_v4_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_overlay_tunnel_v6_key(uint8 lchip, sys_scl_hash_overlay_tunnel_v6_key_t* p_ctc_key,
                                                 sys_scl_hash_overlay_tunnel_v6_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_overlay_tunnel_v6_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_hash_trill_key(uint8 lchip, sys_scl_hash_trill_key_t* p_ctc_key,
                                                 sys_scl_hash_trill_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    sal_memcpy(p_sys_key, p_ctc_key, sizeof(sys_scl_hash_trill_key_t));

    return CTC_E_NONE;
}
STATIC int32
_sys_goldengate_scl_map_port_default_key(uint8 lchip, sys_scl_port_default_key_t* p_ctc_key,
                                         sys_scl_sw_hash_key_port_t* p_sys_key)
{
    uint8  gchip = 0;
    uint16 lport = 0;

    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    gchip = CTC_MAP_GPORT_TO_GCHIP(p_ctc_key->gport);
    lport = CTC_MAP_GPORT_TO_LPORT(p_ctc_key->gport);
    if (!sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }

    if (lport >= SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_SCL_INVALID_DEFAULT_PORT;
    }

    p_sys_key->gport = p_ctc_key->gport;

    return CTC_E_NONE;
}

/*
 * get sys key based on ctc key
 */
STATIC int32
_sys_goldengate_scl_map_key(uint8 lchip, sys_scl_key_t* p_ctc_key,
                            sys_scl_sw_key_t* p_sys_key)
{
    CTC_PTR_VALID_CHECK(p_ctc_key);
    CTC_PTR_VALID_CHECK(p_sys_key);

    SYS_SCL_DBG_FUNC();

    p_sys_key->type = p_ctc_key->type;

    CTC_MAX_VALUE_CHECK(p_ctc_key->tunnel_type, SYS_SCL_TUNNEL_TYPE_NUM - 1);
    p_sys_key->tunnel_type= p_ctc_key->tunnel_type;

    switch (p_ctc_key->type)
    {
    case SYS_SCL_KEY_TCAM_MAC:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_tcam_mac_key(lchip, &p_ctc_key->u.tcam_mac_key, &p_sys_key->u0.mac_key));
        break;

    case SYS_SCL_KEY_TCAM_IPV4:
    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_tcam_ipv4_key(lchip, &p_ctc_key->u.tcam_ipv4_key, &p_sys_key->u0.ipv4_key));
        break;

    case SYS_SCL_KEY_TCAM_VLAN:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_tcam_vlan_key(lchip, &p_ctc_key->u.tcam_vlan_key, &p_sys_key->u0.vlan_key));
        break;

    case SYS_SCL_KEY_TCAM_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_tcam_ipv6_key(lchip, &p_ctc_key->u.tcam_ipv6_key, &p_sys_key->u0.ipv6_key));
        break;

    case SYS_SCL_KEY_HASH_MAC:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_mac_key(lchip, &p_ctc_key->u.hash_mac_key, &p_sys_key->u0.hash.u0.mac));
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_ipv4_key(lchip, &p_ctc_key->u.hash_ipv4_key, &p_sys_key->u0.hash.u0.ipv4));
        break;
    case SYS_SCL_KEY_HASH_PORT:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_key(lchip, &p_ctc_key->u.hash_port_key, &p_sys_key->u0.hash.u0.port));
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_cvlan_key(lchip, &p_ctc_key->u.hash_port_cvlan_key, &p_sys_key->u0.hash.u0.vlan));
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_svlan_key(lchip, &p_ctc_key->u.hash_port_svlan_key, &p_sys_key->u0.hash.u0.vlan));
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_2vlan_key(lchip, &p_ctc_key->u.hash_port_2vlan_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_cvlan_cos_key(lchip, &p_ctc_key->u.hash_port_cvlan_cos_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_svlan_cos_key(lchip, &p_ctc_key->u.hash_port_svlan_cos_key, &p_sys_key->u0.hash.u0.vtag));
        break;
    case SYS_SCL_KEY_HASH_PORT_MAC:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_mac_key(lchip, &p_ctc_key->u.hash_port_mac_key, &p_sys_key->u0.hash.u0.mac));
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_ipv4_key(lchip, &p_ctc_key->u.hash_port_ipv4_key, &p_sys_key->u0.hash.u0.ipv4));
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_port_ipv6_key(lchip, &p_ctc_key->u.hash_port_ipv6_key, &p_sys_key->u0.hash.u0.ipv6));
        break;
    case SYS_SCL_KEY_HASH_IPV6:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_ipv6_key(lchip, &p_ctc_key->u.hash_ipv6_key, &p_sys_key->u0.hash.u0.ipv6));
        break;
    case SYS_SCL_KEY_HASH_L2:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_l2_key(lchip, &p_ctc_key->u.hash_l2_key, &p_sys_key->u0.hash.u0.l2));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA: /* use same struct */
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_tunnel_ipv4_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_key, &p_sys_key->u0.hash.u0.tnnl_ipv4));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_tunnel_ipv4_gre_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_gre_key, &p_sys_key->u0.hash.u0.tnnl_ipv4_gre));
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_tunnel_ipv4_rpf_key(lchip, &p_ctc_key->u.hash_tunnel_ipv4_rpf_key, &p_sys_key->u0.hash.u0.tnnl_ipv4_rpf));
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_overlay_tunnel_v4_key(lchip, &p_ctc_key->u.hash_overlay_tunnel_v4_key, &p_sys_key->u0.hash.u0.ol_tnnl_v4));
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_hash_overlay_tunnel_v6_key(lchip, &p_ctc_key->u.hash_overlay_tunnel_v6_key, &p_sys_key->u0.hash.u0.ol_tnnl_v6));
        break;
    case SYS_SCL_KEY_HASH_TRILL_UC:
    case SYS_SCL_KEY_HASH_TRILL_MC:
    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        CTC_ERROR_RETURN(_sys_goldengate_scl_map_hash_trill_key(lchip, &p_ctc_key->u.hash_trill_key, &p_sys_key->u0.hash.u0.trill));
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        CTC_ERROR_RETURN(
            _sys_goldengate_scl_map_port_default_key(lchip, &p_ctc_key->u.port_default_key, &p_sys_key->u0.hash.u0.port));
        break;

    default:
        return CTC_E_SCL_INVALID_KEY_TYPE;
    }

    return CTC_E_NONE;
}

/*
 * check group info if it is valid.
 */
STATIC int32
_sys_goldengate_scl_check_group_info(uint8 lchip, ctc_scl_group_info_t* pinfo, uint8 type, uint8 is_create)
{
    CTC_PTR_VALID_CHECK(pinfo);
    SYS_SCL_DBG_FUNC();

    CTC_MAX_VALUE_CHECK(pinfo->type, CTC_SCL_GROUP_TYPE_MAX - 1);

    if (is_create) /* create will check gchip. */
    {
      /*   if ((CTC_SCL_GROUP_TYPE_PORT_CLASS == type) )*/
      /*   {*/
      /*       return CTC_E_INVALID_PARAM;*/
      /*   }*/

        SYS_SCL_CHECK_GROUP_PRIO(pinfo->priority);
    }

    if (CTC_SCL_GROUP_TYPE_PORT_CLASS == type)
    {
        SYS_SCL_PORT_CLASS_ID_CHECK(pinfo->un.port_class_id);
    }

    return CTC_E_NONE;
}

/*
 * check if install group info is right, and if it is new.
 */
STATIC bool
_sys_goldengate_scl_is_group_info_new(uint8 lchip, sys_scl_sw_group_info_t* sys,
                                      ctc_scl_group_info_t* ctc)
{
    uint8 equal = 0;
    switch (ctc->type)
    {
    case  CTC_SCL_GROUP_TYPE_HASH:
    case  CTC_SCL_GROUP_TYPE_GLOBAL: /* hash port global group always is not new */
        equal = 1;
        break;
    case  CTC_SCL_GROUP_TYPE_PORT:
        equal = (ctc->un.gport == sys->u0.gport);
        break;
    case  CTC_SCL_GROUP_TYPE_LOGIC_PORT:
        equal = (ctc->un.logic_port == sys->u0.gport);
        break;
    case  CTC_SCL_GROUP_TYPE_PORT_CLASS:
        equal = (ctc->un.port_class_id == sys->u0.port_class_id);
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (!equal)
    {
        return TRUE;
    }

    return FALSE;
}

/*
 * get sys group info based on ctc group info
 */
STATIC int32
_sys_goldengate_scl_map_group_info(uint8 lchip, sys_scl_sw_group_info_t* sys, ctc_scl_group_info_t* ctc, uint8 is_create)
{
    if (is_create) /* create group care type, priority*/
    {
        sys->type     = ctc->type;
        sys->block_id = ctc->priority;
        if (ctc->type == CTC_SCL_GROUP_TYPE_PORT_CLASS)
        {
            sys->u0.port_class_id = ctc->un.port_class_id;
        }
        else if (ctc->type == CTC_SCL_GROUP_TYPE_PORT)
        {
            CTC_GLOBAL_PORT_CHECK(ctc->un.gport)
            sys->u0.gport = ctc->un.gport;
        }
        else if (ctc->type == CTC_SCL_GROUP_TYPE_LOGIC_PORT)
        {
            SYS_LOGIC_PORT_CHECK(ctc->un.logic_port);
            sys->u0.gport = ctc->un.logic_port;
        }
    }
    else    /*type and priority can't be changed when install group*/
    {
        if (sys->type == CTC_SCL_GROUP_TYPE_PORT_CLASS)
        {
            sys->u0.port_class_id = ctc->un.port_class_id;
        }
        else if (sys->type == CTC_SCL_GROUP_TYPE_PORT)
        {
            CTC_GLOBAL_PORT_CHECK(ctc->un.gport)
            sys->u0.gport = ctc->un.gport;
        }
        else if (sys->type == CTC_SCL_GROUP_TYPE_LOGIC_PORT)
        {
            SYS_LOGIC_PORT_CHECK(ctc->un.logic_port);
            sys->u0.gport = ctc->un.logic_port;
        }
    }

    return CTC_E_NONE;
}


/*
 * get ctc group info based on sys group info
 */
STATIC int32
_sys_goldengate_scl_unmap_group_info(uint8 lchip, ctc_scl_group_info_t* ctc, sys_scl_sw_group_info_t* sys)
{
    ctc->type             = sys->type;
    ctc->lchip            = lchip;

    ctc->priority         = sys->block_id;
    ctc->un.port_class_id = sys->u0.port_class_id;

    return CTC_E_NONE;
}


/*
 * remove accessory property
 */
STATIC int32
_sys_goldengate_scl_remove_accessory_property(uint8 lchip, sys_scl_sw_entry_t* pe, sys_scl_sw_action_t* pa)
{

    CTC_PTR_VALID_CHECK(pe);
    CTC_PTR_VALID_CHECK(pa);

    /* remove statsptr */
    switch (pa->type)
    {
    case SYS_SCL_ACTION_INGRESS:
    {
        sys_scl_sw_igs_action_t* pia = NULL;
        pia = &pa->u0.ia;
        if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
        {
            pia->chip.stats_ptr = 0;

        }
    }
    break;
    case SYS_SCL_ACTION_EGRESS:
    {
        sys_scl_sw_egs_action_t* pea = NULL;
        pea = &pa->u0.ea;
        if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
        {

            pea->chip.stats_ptr = 0;

        }
    }
    break;
    case SYS_SCL_ACTION_FLOW:
    {
        sys_scl_sw_flow_action_t* pfa = NULL;
        pfa = &pa->u0.fa;
        if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_STATS))
        {
            pfa->chip.stats_ptr = 0;
        }
    }
    break;
    default:
        break;
    }

    return CTC_E_NONE;
}

#define  __SCL_BUILD_FREE_INDEX__






/*----reserv 64 patterns-----*/
STATIC int32
_sys_goldengate_scl_vlan_edit_index_build(uint8 lchip, uint16* p_profile_id)
{
    sys_goldengate_opf_t opf;
    uint32               value_32 = 0;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &value_32));
    *p_profile_id = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_vlan_edit_index_free(uint8 lchip, uint16 iv_edit_index_free)
{
    sys_goldengate_opf_t opf;

    SYS_SCL_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
    opf.pool_index = 0;
    opf.pool_type  = OPF_SCL_VLAN_ACTION;

    CTC_ERROR_RETURN(sys_goldengate_opf_free_offset(lchip, &opf, 1, (uint32) iv_edit_index_free));

    return CTC_E_NONE;
}



/* free index of HASH ad */
STATIC int32
_sys_goldengate_scl_free_hash_ad_index(uint8 lchip, uint16 ad_index)
{
    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_ftm_free_table_offset(lchip, DsUserId_t, 0, 1, (uint32) ad_index));

    return CTC_E_NONE;
}



/* build index of HASH ad */
STATIC int32
_sys_goldengate_scl_build_hash_ad_index(uint8 lchip, uint16* p_ad_index)
{
    uint32  value_32 = 0;

    SYS_SCL_DBG_FUNC();

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsUserId_t, 0, 1, &value_32));
    *p_ad_index = value_32 & 0xFFFF;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_asic_hash_lkup(uint8 lchip, sys_scl_sw_entry_t* pe, uint8 act_type, uint8* conflict, uint32* index)
{
    uint32            key[MAX_ENTRY_WORD] = { 0 };
    sys_scl_sw_key_t  * psys              = NULL;
    drv_cpu_acc_in_t  in;
    drv_cpu_acc_out_t out;
    uint8             acc_type = DRV_CPU_ALLOC_ACC_USER_ID;
    uint32            key_id   = 0;
    uint32            act_id   = 0;
    uint16            lport    = 0;

    CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, 0, pe->key.type, act_type, &key_id, &act_id));

    CTC_PTR_VALID_CHECK(pe);
    SYS_SCL_DBG_FUNC();

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    psys = &pe->key;
    switch (pe->key.type)
    {
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        _sys_goldengate_scl_build_hash_2vlan(lchip, psys, key, 0);
        if (psys->u0.hash.u0.vtag.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_PORT:
        _sys_goldengate_scl_build_hash_port(lchip, psys, key, 0);
        if (psys->u0.hash.u0.port.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        _sys_goldengate_scl_build_hash_cvlan(lchip, psys, key, 0);
        if (psys->u0.hash.u0.vlan.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        _sys_goldengate_scl_build_hash_svlan(lchip, psys, key, 0);
        if (psys->u0.hash.u0.vlan.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        _sys_goldengate_scl_build_hash_cvlan_cos(lchip, psys, key, 0);
        if (psys->u0.hash.u0.vtag.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        _sys_goldengate_scl_build_hash_svlan_cos(lchip, psys, key, 0);
        if (psys->u0.hash.u0.vtag.dir == CTC_EGRESS)
        {
            acc_type = DRV_CPU_ALLOC_ACC_XC_OAM;
        }
        break;

    case SYS_SCL_KEY_HASH_MAC:
        _sys_goldengate_scl_build_hash_mac(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_PORT_MAC:
        _sys_goldengate_scl_build_hash_port_mac(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_IPV4:
        _sys_goldengate_scl_build_hash_ipv4(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV4:
        _sys_goldengate_scl_build_hash_port_ipv4(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_IPV6:
        _sys_goldengate_scl_build_hash_ipv6(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_PORT_IPV6:
        _sys_goldengate_scl_build_hash_port_ipv6(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_L2:
        _sys_goldengate_scl_build_hash_l2(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        _sys_goldengate_scl_build_hash_tunnel_ipv4(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_da(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_gre(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        _sys_goldengate_scl_build_hash_tunnel_ipv4_rpf(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv4_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv4_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_ipv4_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_uc_ipv6_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
        _sys_goldengate_scl_build_hash_nvgre_mc_ipv6_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv4_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv4_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_ipv4_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_uc_ipv6_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode0(psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
        _sys_goldengate_scl_build_hash_vxlan_mc_ipv6_mode1(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TRILL_UC:
        _sys_goldengate_scl_build_hash_trill_uc(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC:
        _sys_goldengate_scl_build_hash_trill_mc(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
        _sys_goldengate_scl_build_hash_trill_uc_rpf(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
        _sys_goldengate_scl_build_hash_trill_mc_rpf(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        _sys_goldengate_scl_build_hash_trill_mc_adj(lchip, psys, key, 0);
        break;

    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(psys->u0.hash.u0.port.gport, lchip, lport);
        out.hit       = 1;
        out.key_index = lport;
        break;

    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(psys->u0.hash.u0.port.gport, lchip, lport);
        out.hit       = 1;
        out.key_index = lport + p_gg_scl_master[lchip]->igs_default_base;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    /* get index of Key */
    in.data   = key;
    in.tbl_id = key_id;
    if (!out.hit) /* exclude ingress egressd default. */
    {
        CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, &out));
    }
    *conflict = out.conflict;
    *index    = out.key_index;


    return CTC_E_NONE;
}


#define  __SCL_ESSENTIAL_API__

STATIC int32
_sys_goldengate_scl_egs_hash_ad_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                    sys_scl_sw_action_t* pa,
                                    sys_scl_sw_action_t** pa_out)
{
    sys_scl_sw_action_t* pa_new = NULL;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));
    pa_new->lchip = lchip;
    pa_new->ad_index = pe->fpae.offset_a;

    *pa_out = pa_new;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_hash_ad_spool_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                      sys_scl_sw_action_t* pa,
                                      sys_scl_sw_action_t** pa_out)
{
    sys_scl_sw_action_t* pa_new = NULL;
    sys_scl_sw_action_t* pa_get = NULL; /* get from share pool*/
    int32              ret      = 0;
    uint16             ad_index_get;

    uint8              array_idx = NORMAL_AD_SHARE_POOL;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));
    pa_new->lchip = lchip;

    if (SYS_SCL_IS_DEFAULT(pe->key.type))
    {
        array_idx = DEFAULT_AD_SHARE_POOL;
    }

        /* -- set action ptr -- */
    ret = ctc_spool_add(p_gg_scl_master[lchip]->ad_spool[array_idx], pa_new, NULL, &pa_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto cleanup;
    }
    else if ((CTC_SPOOL_E_OPERATE_MEMORY == ret) && (NORMAL_AD_SHARE_POOL == array_idx))
    {
        ret = _sys_goldengate_scl_build_hash_ad_index(lchip, &ad_index_get);
        if (ret < 0)
        {
            ctc_spool_remove(p_gg_scl_master[lchip]->ad_spool[array_idx], pa_new, NULL);
            goto cleanup;
        }

        pa_get->ad_index = ad_index_get;
    }
    else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    {
        mem_free(pa_new);
    }



    *pa_out = pa_get;

    return CTC_E_NONE;

 cleanup:
    mem_free(pa_new);
    return ret;
}

STATIC int32
_sys_goldengate_scl_hash_ad_spool_remove(uint8 lchip, sys_scl_sw_entry_t* pe,
                                         sys_scl_sw_action_t* pa)

{
    uint16             index_free = 0;

    int32              ret       = 0;
    uint8              array_idx = NORMAL_AD_SHARE_POOL;

    sys_scl_sw_action_t* pa_lkup;

    if (SYS_SCL_IS_DEFAULT(pe->key.type))
    {
        array_idx = DEFAULT_AD_SHARE_POOL;
    }

    ret = ctc_spool_remove(p_gg_scl_master[lchip]->ad_spool[array_idx], pa, &pa_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        if (NORMAL_AD_SHARE_POOL == array_idx)
        {
            index_free = pa_lkup->ad_index;

            CTC_ERROR_RETURN(_sys_goldengate_scl_free_hash_ad_index(lchip, index_free));

            SYS_SCL_DBG_INFO("  %% INFO: scl action removed: index = %d\n", index_free);
        }
        mem_free(pa_lkup);
    }



    return CTC_E_NONE;
}

/* pv_out can be NULL.*/
STATIC int32
_sys_goldengate_scl_hash_vlan_edit_spool_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                             sys_scl_sw_vlan_edit_t* pv,
                                             sys_scl_sw_vlan_edit_t** pv_out)
{
    sys_scl_sw_vlan_edit_t* pv_new = NULL;
    sys_scl_sw_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    int32                 ret      = 0;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pv_new, sizeof(sys_scl_sw_vlan_edit_t));
    if (!pv_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pv_new, pv, sizeof(sys_scl_sw_vlan_edit_t));
    pv_new->lchip = lchip;

    /* -- set vlan edit ptr -- */
    ret = ctc_spool_add(p_gg_scl_master[lchip]->vlan_edit_spool, pv_new, NULL, &pv_get);

    if (ret < 0)
    {
        ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
        goto cleanup;
    }
    else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
    {
        uint16 profile_id = 0;
        ret                            = (ret < 0) ? ret : _sys_goldengate_scl_vlan_edit_index_build(lchip, &profile_id);
        pv_get->chip.profile_id = profile_id;

        if (ret < 0)
        {
            ctc_spool_remove(p_gg_scl_master[lchip]->vlan_edit_spool, pv_new, NULL);
            goto cleanup;
        }
    }
    else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
    {
        mem_free(pv_new);
    }

    if (pv_out)
    {
        *pv_out = pv_get;
    }

    return CTC_E_NONE;

 cleanup:
    mem_free(pv_new);
    return ret;
}

STATIC int32
_sys_goldengate_scl_hash_vlan_edit_spool_remove(uint8 lchip, sys_scl_sw_entry_t* pe,
                                                sys_scl_sw_vlan_edit_t* pv)

{
    uint16                index_free = 0;

    int32                 ret = 0;
    sys_scl_sw_vlan_edit_t* pv_lkup;

    ret = ctc_spool_remove(p_gg_scl_master[lchip]->vlan_edit_spool, pv, &pv_lkup);

    if (ret < 0)
    {
        return CTC_E_SPOOL_REMOVE_FAILED;
    }
    else if (ret == CTC_SPOOL_E_OPERATE_MEMORY)
    {
        index_free = pv_lkup->chip.profile_id;

        CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_index_free(lchip, index_free));
        mem_free(pv_lkup);
    }

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_hash_key_hash_add(uint8 lchip, sys_scl_sw_entry_t* pe,
                                      sys_scl_sw_entry_t** pe_out, uint8 act_type)
{
    uint8             conflict   = 0;
    uint8             count_size  = 0;
    int32             ret;
    uint32            key_index                           = 0;
    uint32            key_id                           = 0;
    uint32            act_id                           = 0;
    uint32            block_index =  0 ;
    sys_scl_sw_entry_t* pe_new                            = NULL;


    /* cacluate asic hash index */
        /* ASIC hash lookup, check if hash conflict */
    CTC_ERROR_RETURN(_sys_goldengate_scl_asic_hash_lkup(lchip, pe, act_type, &conflict, &key_index));

    if (conflict)
    {
        return CTC_E_SCL_HASH_CONFLICT;
    }
    else
    {
        block_index = key_index;
    }

    /* malloc sys entry */
    MALLOC_ZERO(MEM_SCL_MODULE, pe_new, p_gg_scl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, p_gg_scl_master[lchip]->entry_size[pe->key.type]);
    pe_new->lchip = lchip;
    pe_new->fpae.lchip = lchip;

    if(0 == SYS_SCL_IS_DEFAULT(pe->key.type))
    {
        CTC_ERROR_GOTO(_sys_goldengate_scl_get_table_id(lchip, 0, pe->key.type, act_type, &key_id, &act_id), ret, error);
        count_size = TABLE_ENTRY_SIZE(key_id) / 12;
        p_gg_scl_master[lchip]->action_count[act_type] = p_gg_scl_master[lchip]->action_count[act_type] + count_size;
    }

        /* set block index */
    pe_new->fpae.offset_a = block_index;


    *pe_out = pe_new;
    return CTC_E_NONE;

error:
    mem_free(pe_new);
    return ret;
}




STATIC int32
_sys_goldengate_scl_hash_key_hash_remove(uint8 lchip, sys_scl_sw_entry_t* pe_del, uint8 action_type)
{
    uint8             count_size  = 0;
    uint32            key_id = 0;
    uint32            act_id = 0;
    uint32            p_key_index = 0;

    if(0 == SYS_SCL_IS_DEFAULT(pe_del->key.type))
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, 0, pe_del->key.type, action_type, &key_id, &act_id));

        /* clear hw valid bit */
        {
            uint8 acc_type = (action_type == SYS_SCL_ACTION_EGRESS) ?
            DRV_CPU_DEL_ACC_XC_OAM_BY_IDX : DRV_CPU_DEL_ACC_USER_ID_BY_IDX;
            drv_cpu_acc_in_t  in;
            sal_memset(&in, 0, sizeof(in));

            p_key_index = pe_del->fpae.offset_a;
            in.tbl_id = key_id;
            in.key_index  = p_key_index;
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, NULL));
        }

        count_size = TABLE_ENTRY_SIZE(key_id) / 12;
        p_gg_scl_master[lchip]->action_count[action_type] = p_gg_scl_master[lchip]->action_count[action_type] - count_size;
    }

    /* mem free*/
    mem_free(pe_del);
    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_tcam_entry_add(uint8 lchip, sys_scl_sw_group_t* pg,
                                   sys_scl_sw_entry_t* pe,
                                   sys_scl_sw_action_t* pa,
                                   sys_scl_sw_entry_t** pe_out,
                                   sys_scl_sw_action_t** pa_out)
{
    uint8              fpa_size;
    int32              ret;
    sys_scl_sw_entry_t * pe_new    = NULL;
    sys_scl_sw_action_t* pa_new    = NULL;
    sys_scl_sw_block_t * pb        = NULL;
    uint32             block_index = 0;


    /* malloc sys entry */
    MALLOC_ZERO(MEM_SCL_MODULE, pe_new, p_gg_scl_master[lchip]->entry_size[pe->key.type]);
    if (!pe_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pe_new, pe, p_gg_scl_master[lchip]->entry_size[pe->key.type]);
    pe_new->lchip = lchip;
    pe_new->fpae.lchip = lchip;

    /* malloc sys action */
    MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
    if (!pa_new)
    {
        mem_free(pe_new);
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(pa_new, pa, sizeof(sys_scl_sw_action_t));
    pa_new->lchip = lchip;

    /* get block index. */
    fpa_size = p_gg_scl_master[lchip]->alloc_id[pe->key.type];
    pb       = &p_gg_scl_master[lchip]->block[pg->group_info.block_id];

    CTC_ERROR_GOTO(fpa_goldengate_alloc_offset(p_gg_scl_master[lchip]->fpa, &pb->fpab, fpa_size,      /* may trigger fpa entry movement */
                                    pe->fpae.priority, &block_index), ret, cleanup);
    {   /* move to fpa */
        /* add to block */
        pb->fpab.entries[block_index] = &pe_new->fpae;

        /* set block index */
        pe_new->fpae.offset_a = block_index;
        pa_new->ad_index      = block_index;

        fpa_goldengate_reorder(p_gg_scl_master[lchip]->fpa, &pb->fpab, fpa_size); /* if fpab move_num at threshold will trigger scatter reorder */

    }

    *pe_out = pe_new;
    *pa_out = pa_new;

    return CTC_E_NONE;

 cleanup:
    mem_free(pe_new);
    mem_free(pa_new);
    return ret;
}




STATIC int32
_sys_goldengate_scl_tcam_entry_remove(uint8 lchip, sys_scl_sw_entry_t* pe_lkup,
                                      sys_scl_sw_block_t* pb_lkup)
{

    uint32 block_index = 0;

    /* remove from block */
    block_index = pe_lkup->fpae.offset_a;

    CTC_ERROR_RETURN(fpa_goldengate_free_offset(&pb_lkup->fpab, block_index));

    mem_free(pe_lkup->action);
    mem_free(pe_lkup);

    return CTC_E_NONE;
}




/*
 * pe, pg, pb are lookup result.
 */
int32
_sys_goldengate_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner )
{
    uint8 tunnel_type =0 ;
    uint8 action_type =0 ;
    sys_scl_sw_entry_t* pe_lkup = NULL;
    sys_scl_sw_group_t* pg_lkup = NULL;
    sys_scl_sw_block_t* pb_lkup = NULL;

    SYS_SCL_DBG_FUNC();

    _sys_goldengate_scl_get_nodes_by_eid(lchip, entry_id, &pe_lkup, &pg_lkup, &pb_lkup);
    if (!pg_lkup)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if(!pe_lkup)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (FPA_ENTRY_FLAG_INSTALLED == pe_lkup->fpae.flag) /* must uninstall hw table first*/
    {
        return CTC_E_SCL_ENTRY_INSTALLED;
    }

    if ((SYS_SCL_ACTION_INGRESS == pe_lkup->action->type) &&
        (pe_lkup->action->u0.ia.vlan_edit))
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_hash_vlan_edit_spool_remove
                             (lchip, pe_lkup, pe_lkup->action->u0.ia.vlan_edit));
    }

    /* remove from group */
    ctc_slist_delete_node(pg_lkup->entry_list, &(pe_lkup->head));

    /* remove from hash */
    if(NULL == ctc_hash_remove(p_gg_scl_master[lchip]->entry, pe_lkup))
    {
        return CTC_E_UNEXPECT;
    }

    if (SCL_INNER_ENTRY_ID(pe_lkup->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
    {
        /* remove hash by key */
        if(NULL == ctc_hash_remove(p_gg_scl_master[lchip]->entry_by_key, pe_lkup))
        {
            return CTC_E_UNEXPECT;
        }
    }

    tunnel_type = pe_lkup->key.tunnel_type;

    /* remove accessory property */
    CTC_ERROR_RETURN(_sys_goldengate_scl_remove_accessory_property(lchip, pe_lkup, pe_lkup->action));

    if (SCL_ENTRY_IS_TCAM(pe_lkup->key.type))
    {
        CTC_ERROR_RETURN(_sys_goldengate_scl_tcam_entry_remove(lchip, pe_lkup, pb_lkup));
    }
    else
    {
        action_type = pe_lkup->action->type;
        if (SYS_SCL_ACTION_EGRESS != pe_lkup->action->type)
        {
            CTC_ERROR_RETURN(_sys_goldengate_scl_hash_ad_spool_remove(lchip, pe_lkup, pe_lkup->action));
        }
        else
        {
            mem_free(pe_lkup->action);
        }

        CTC_ERROR_RETURN(_sys_goldengate_scl_hash_key_hash_remove(lchip, pe_lkup, action_type));
    }

    (pg_lkup->entry_count)--;

    if(tunnel_type)
    {
        (p_gg_scl_master[lchip]->tunnel_count[tunnel_type])--;
    }

    if (inner)
    {
        sys_goldengate_opf_t opf;
        /* sdk assigned, recycle */
        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_index = 0; /* care not chip */
        opf.pool_type  = OPF_SCL_ENTRY_ID;

        sys_goldengate_opf_free_offset(lchip, &opf, 1, (entry_id - SYS_SCL_MIN_INNER_ENTRY_ID));
    }

    return CTC_E_NONE;
}



/*
 * add scl entry to software table
 * pe pa is all ready
 *
 */
STATIC int32
_sys_goldengate_scl_add_entry(uint8 lchip, uint32 group_id,
                              sys_scl_sw_entry_t* pe,
                              sys_scl_sw_action_t* pa,
                              sys_scl_sw_vlan_edit_t* pv)
{
    int32                 ret      = 0;
    uint32               pu32[MAX_ENTRY_WORD]       = { 0 };
    uint32               key_id;
    uint32               act_id;
    uint8                count_size  = 0;
    /*uint32               cmd_key = 0;*/
    void                 * p_key_void = pu32;
    sys_scl_sw_entry_t    * pe_get = NULL;
    sys_scl_sw_action_t   * pa_get = NULL; /* get from share pool*/
    sys_scl_sw_vlan_edit_t* pv_get = NULL; /* get from share pool*/
    sys_scl_sw_group_t* pg = NULL;

    SYS_SCL_DBG_FUNC();

    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if (SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* 1. vlan edit. error roll back key.*/
        if ((SYS_SCL_ACTION_INGRESS == pa->type)
            && (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            ret = (_sys_goldengate_scl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get));
            if FAIL(ret)
            {
                return ret;
            }

            /* 3. action. error roll back vlan edit and key*/
            if (pv_get) /* complete action with vlan edit*/
            {

                pa->u0.ia.vlan_edit                 = pv_get;
                pa->u0.ia.vlan_action_profile_valid = 1;

                pa->u0.ia.chip.profile_id = pv_get->chip.profile_id;

            }
        }


        ret = (_sys_goldengate_scl_tcam_entry_add(lchip, pg, pe, pa, &pe_get, &pa_get));
        if (FAIL(ret))
        {
            if (pv_get)
            {
                _sys_goldengate_scl_hash_vlan_edit_spool_remove(lchip, pe, pv_get);
            }
            return ret;
        }
    }
    else
    {
        /* scl_spec index by action type,but egress action scl_spec not define! */
        ctc_ftm_spec_t scl_spec[] = {CTC_FTM_SPEC_SCL, CTC_FTM_SPEC_SCL, CTC_FTM_SPEC_TUNNEL, CTC_FTM_SPEC_SCL_FLOW};
        CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, pg->group_info.block_id, pe->key.type, pa->type, &key_id, &act_id));
        count_size = TABLE_ENTRY_SIZE(key_id) / 12;
        if(((pa->type != SYS_SCL_ACTION_EGRESS) && (p_gg_scl_master[lchip]->action_count[pa->type] + count_size > SYS_FTM_SPEC(lchip, scl_spec[pa->type])))
            || ((pa->type == SYS_SCL_ACTION_EGRESS) && (0 == p_gg_scl_master[lchip]->egs_entry_num)))
        {
            return CTC_E_NO_RESOURCE;
        }

        /* 1. key . error just return */
        CTC_ERROR_RETURN(_sys_goldengate_scl_hash_key_hash_add(lchip, pe, &pe_get, pa->type));

        /* 2. vlan edit. error roll back key.*/
        if ((SYS_SCL_ACTION_INGRESS == pa->type)
            && (CTC_FLAG_ISSET(pa->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            ret = (_sys_goldengate_scl_hash_vlan_edit_spool_add(lchip, pe, pv, &pv_get));
            if FAIL(ret)
            {
                _sys_goldengate_scl_hash_key_hash_remove(lchip, pe_get, pa->type);
                return ret;
            }

            /* 3. action. error roll back vlan edit and key*/
            if (pv_get) /* complete action with vlan edit*/
            {

                pa->u0.ia.vlan_edit                 = pv_get;
                pa->u0.ia.vlan_action_profile_valid = 1;

                pa->u0.ia.chip.profile_id = pv_get->chip.profile_id;
            }
        }

        if (SYS_SCL_ACTION_EGRESS != pa->type)
        {
            ret = (_sys_goldengate_scl_hash_ad_spool_add(lchip, pe_get, pa, &pa_get));
            if FAIL(ret)
            {
                _sys_goldengate_scl_hash_key_hash_remove(lchip, pe_get, pa->type);
                if (pv_get)
                {
                    _sys_goldengate_scl_hash_vlan_edit_spool_remove(lchip, pe_get, pv_get);
                }
                return ret;
            }
        }
        else
        {
            ret = (_sys_goldengate_scl_egs_hash_ad_add(lchip, pe, pa, &pa_get));
            if FAIL(ret)
            {
                _sys_goldengate_scl_hash_key_hash_remove(lchip, pe_get, pa->type);
                return ret;
            }
        }

        /* -- set hw valid bit  -- */

        switch (pe_get->key.type)
        {
            case SYS_SCL_KEY_HASH_PORT_2VLAN:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdDoubleVlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamDoubleVlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.port.dir)
                {
                    SetDsUserIdPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.vlan.dir)
                {
                    SetDsUserIdCvlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamCvlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.vlan.dir)
                {
                    SetDsUserIdSvlanPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamSvlanPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdCvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamCvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
                if(CTC_INGRESS == pe_get->key.u0.hash.u0.vtag.dir)
                {
                    SetDsUserIdSvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                else
                {
                    SetDsEgressXcOamSvlanCosPortHashKey(V, valid_f, pu32, 1);
                }
                break;

            case SYS_SCL_KEY_HASH_MAC:
                SetDsUserIdMacHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_MAC:
                SetDsUserIdMacPortHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdMacPortHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_IPV4:
                SetDsUserIdIpv4SaHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV4:
                SetDsUserIdIpv4PortHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_IPV6:
                SetDsUserIdIpv6SaHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdIpv6SaHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV6:
                SetDsUserIdIpv6PortHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid1_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid2_f, pu32, 1);
                SetDsUserIdIpv6PortHashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_L2:
                SetDsUserIdSclFlowL2HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdSclFlowL2HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
                SetDsUserIdTunnelIpv4HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
                SetDsUserIdTunnelIpv4DaHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
                SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4GreKeyHashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
                SetDsUserIdTunnelIpv4RpfHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
                SetDsUserIdTunnelIpv4UcNvgreMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
                SetDsUserIdTunnelIpv4UcNvgreMode1HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
                SetDsUserIdTunnelIpv4McNvgreMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
                SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4NvgreMode1HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcNvgreMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McNvgreMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
                SetDsUserIdTunnelIpv4UcVxlanMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
                SetDsUserIdTunnelIpv4UcVxlanMode1HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
                SetDsUserIdTunnelIpv4McVxlanMode0HashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
                SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv4VxlanMode1HashKey(V, valid1_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6UcVxlanMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode0HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid0_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid1_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid2_f, pu32, 1);
                SetDsUserIdTunnelIpv6McVxlanMode1HashKey(V, valid3_f, pu32, 1);
                break;

            case SYS_SCL_KEY_HASH_TRILL_UC:
                SetDsUserIdTunnelTrillUcDecapHashKey(V, valid_f, pu32, 1);
                break;
           case SYS_SCL_KEY_HASH_TRILL_MC:
                SetDsUserIdTunnelTrillMcDecapHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
                SetDsUserIdTunnelTrillUcRpfHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
                SetDsUserIdTunnelTrillMcRpfHashKey(V, valid_f, pu32, 1);
                break;
            case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
                SetDsUserIdTunnelTrillMcAdjHashKey(V, valid_f, pu32, 1);
                break;

            case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
            case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }

        if ((SYS_SCL_KEY_PORT_DEFAULT_EGRESS != pe_get->key.type) &&
            (SYS_SCL_KEY_PORT_DEFAULT_INGRESS != pe_get->key.type))
        {
            /*cmd_key = DRV_IOW(key_id, DRV_ENTRY_FLAG);*/
            uint8 acc_type = (pa->type == SYS_SCL_ACTION_EGRESS) ?
                DRV_CPU_ADD_ACC_XC_OAM_BY_IDX : DRV_CPU_ADD_ACC_USER_ID_BY_IDX;
            drv_cpu_acc_in_t  in;

            sal_memset(&in, 0, sizeof(in));

            in.tbl_id = key_id;
            in.data   = p_key_void;
            in.key_index  = pe_get->fpae.offset_a;
            CTC_ERROR_RETURN(drv_goldengate_cpu_acc(lchip, acc_type, &in, NULL));
        }
    }

    /* -- set action ptr -- */
    pe_get->action = pa_get;

    /* add to hash, error just return */
    if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry, pe_get))
    {
        return CTC_E_SCL_HASH_INSERT_FAILED;
    }

    if (SCL_INNER_ENTRY_ID(pe->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        /* add to hash by key */
        if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry_by_key, pe_get))
        {
            return CTC_E_SCL_HASH_INSERT_FAILED;
        }

    }

    /* add to group entry list's head, error just return */
    if(NULL == ctc_slist_add_head(pg->entry_list, &(pe_get->head)))
    {
        return CTC_E_INVALID_PARAM;
    }

    /* mark flag */
    pe_get->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;

    (pg->entry_count)++;

    if(pe_get->key.tunnel_type)
    {
        (p_gg_scl_master[lchip]->tunnel_count[pe_get->key.tunnel_type])++;
    }

    return CTC_E_NONE;
}

#define __SCL_DUMP_INFO__
#define SCL_DUMP_VLAN_INFO(flag, pk, TYPE)                                          \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CVLAN))             \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-vlan 0x%x  mask 0x%0x\n", pk->cvlan, pk->cvlan_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CTAG_COS))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-cos %u  mask %u\n", pk->ctag_cos, pk->ctag_cos_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_CTAG_CFI))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  c-cfi %u\n", pk->ctag_cfi);                             \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_SVLAN))             \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-vlan 0x%x  mask 0x%0x\n", pk->svlan, pk->svlan_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_STAG_COS))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-cos %u  mask %u\n", pk->stag_cos, pk->stag_cos_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_STAG_CFI))          \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  s-cfi %u\n", pk->stag_cfi);                             \
    }

#define SCL_DUMP_MAC_INFO(flag, pk, TYPE)                                                 \
    SYS_SCL_DBG_DUMP("  mac-sa");                                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_MAC_SA))                  \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_sa_mask[2]))                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x\n",                        \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_sa_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_sa_mask[2]))                                  \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" any\n");                                                   \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x\n",  \
                             pk->mac_sa[0], pk->mac_sa[1], pk->mac_sa[2],                 \
                             pk->mac_sa[3], pk->mac_sa[4], pk->mac_sa[5],                 \
                             pk->mac_sa_mask[0], pk->mac_sa_mask[1], pk->mac_sa_mask[2],  \
                             pk->mac_sa_mask[3], pk->mac_sa_mask[4], pk->mac_sa_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_SCL_DBG_DUMP(" any\n");                                                       \
    }                                                                                     \
                                                                                          \
    SYS_SCL_DBG_DUMP("  mac-da");                                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_MAC_DA))                  \
    {                                                                                     \
        if ((0xFFFF == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
            (0xFFFFFFFF == *(uint32 *) &pk->mac_da_mask[2]))                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" host %02x%02x.%02x%02x.%02x%02x\n",                        \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5]);                \
        }                                                                                 \
        else if ((0 == *(uint16 *) &pk->mac_da_mask[0]) &&                                \
                 (0 == *(uint32 *) &pk->mac_da_mask[2]))                                  \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" any\n");                                                   \
        }                                                                                 \
        else                                                                              \
        {                                                                                 \
            SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x %02x%02x.%02x%02x.%02x%02x\n",  \
                             pk->mac_da[0], pk->mac_da[1], pk->mac_da[2],                 \
                             pk->mac_da[3], pk->mac_da[4], pk->mac_da[5],                 \
                             pk->mac_da_mask[0], pk->mac_da_mask[1], pk->mac_da_mask[2],  \
                             pk->mac_da_mask[3], pk->mac_da_mask[4], pk->mac_da_mask[5]); \
        }                                                                                 \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
        SYS_SCL_DBG_DUMP(" any\n");                                                       \
    }

#define SCL_DUMP_L2_TYPE(flag, pk, TYPE)                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L2_TYPE))           \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  l2_type %u  mask %u\n", pk->l2_type, pk->l2_type_mask); \
    }

#define SCL_DUMP_L2_L3_TYPE(flag, pk, TYPE)                                         \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L3_TYPE))           \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  l3_type %u  mask %u\n", pk->l3_type, pk->l3_type_mask); \
    }                                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L2_TYPE))           \
    {                                                                               \
        SYS_SCL_DBG_DUMP("  l2_type %u  mask %u\n", pk->l2_type, pk->l2_type_mask); \
    }

#define SCL_DUMP_L3_INFO(flag, pk, TYPE)                                                                     \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_L4_TYPE))                                    \
    {                                                                                                        \
        SYS_SCL_DBG_DUMP("  l4_type %u  mask %u\n", pk->u0.ip.l4_type, pk->u0.ip.l4_type_mask);              \
    }                                                                                                        \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_ETH_TYPE))                                   \
    {                                                                                                        \
        SYS_SCL_DBG_DUMP("  eth-type 0x%x  mask 0x%x\n", pk->u0.other.eth_type, pk->u0.other.eth_type_mask); \
    }                                                                                                        \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_DSCP) ||                                     \
        CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_PRECEDENCE))                                 \
    {                                                                                                        \
        SYS_SCL_DBG_DUMP("  dscp %u  mask %u\n", pk->u0.ip.dscp, pk->u0.ip.dscp_mask);                       \
    }                                                                                                        \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_IP_FRAG))                                    \
    {                                                                                                        \
        SYS_SCL_DBG_DUMP("  frag-info %u  mask %u\n", pk->u0.ip.frag_info,                                   \
                         pk->u0.ip.frag_info_mask);                                                          \
    }                                                                                                        \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_ ## TYPE ## _KEY_FLAG_IP_OPTION))                                  \
    {                                                                                                        \
        SYS_SCL_DBG_DUMP("  ip-option %u\n", pk->u0.ip.ip_option);                                           \
    }



#define SCL_DUMP_L4_PROTOCOL(flag, pk, TYPE)                                                               \
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_L4_PROTOCOL))                                      \
    {                                                                                                      \
        if (pk->u0.ip.flag_l4info_mapped)                                                                  \
        {                                                                                                  \
            if ((0xFF == pk->u0.ip.l4info_mapped_mask)                                                     \
                && (SYS_L4_PROTOCOL_IPV6_ICMP == pk->u0.ip.l4info_mapped))        /* ICMP */               \
            {                                                                                              \
                SYS_SCL_DBG_DUMP("  L4 Protocol = Icmp\n");                                                \
                /* icmp type & code*/                                                                      \
                if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))                  \
                {                                                                                          \
                    SYS_SCL_DBG_DUMP("  icmp type:0x%x  mask:0x%x\n",                                      \
                                     (pk->u0.ip.l4_src_port >> 8), (pk->u0.ip.l4_src_port_mask >> 8));     \
                    SYS_SCL_DBG_DUMP("  icmp code:0x%x  mask:0x%x\n",                                      \
                                     (pk->u0.ip.l4_src_port & 0xFF), (pk->u0.ip.l4_src_port_mask & 0xFF)); \
                }                                                                                          \
            }                                                                                              \
            else    /* other layer 4 protocol type */                                                      \
            {                                                                                              \
                SYS_SCL_DBG_DUMP("  L4 Protocol = 0x%x  mask:0x%x\n",                                      \
                                 pk->u0.ip.l4info_mapped, pk->u0.ip.l4info_mapped_mask);                   \
            }                                                                                              \
        }                                                                                                  \
        else                                                                                               \
        {                                                                                                  \
                                                                                                           \
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_SRC_PORT))                      \
            {                                                                                              \
                SYS_SCL_DBG_DUMP("  l4 src port:0x%x  mask:0x%x\n",                                        \
                                 (pk->u0.ip.l4_src_port), (pk->u0.ip.l4_src_port_mask));                   \
            }                                                                                              \
            if (CTC_FLAG_ISSET(sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_L4_DST_PORT))                      \
            {                                                                                              \
                SYS_SCL_DBG_DUMP("  l4 dst port:0x%x  mask:0x%x\n",                                        \
                                 (pk->u0.ip.l4_dst_port), (pk->u0.ip.l4_dst_port_mask));                   \
            }                                                                                              \
                                                                                                           \
        }                                                                                                  \
    }



#if 0

/*
 * show mac entry
 */
STATIC int32
_sys_goldengate_scl_show_tcam_mac(uint8 lchip, sys_scl_sw_tcam_key_mac_t* pk)
{
    uint32 flag = pk->flag;

    SCL_DUMP_VLAN_INFO(flag, pk, MAC);
    SCL_DUMP_MAC_INFO(flag, pk, MAC);
    SCL_DUMP_L2_TYPE(flag, pk, MAC);

    return CTC_E_NONE;
}

/*
 * show ipv4 entry
 */
STATIC int32
_sys_goldengate_scl_show_tcam_ipv4(uint8 lchip, sys_scl_sw_tcam_key_ipv4_t* pk)
{
    uint32 flag      = pk->flag;
    uint32 sub_flag  = pk->sub_flag;

    {
        uint32 addr;
        char   ip_addr[16];
        SYS_SCL_DBG_DUMP("  ip-sa  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
        {
            if (0xFFFFFFFF == pk->u0.ip.ip_sa_mask)
            {
                SYS_SCL_DBG_DUMP("host");
                addr = sal_ntohl(pk->u0.ip.ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
            }
            else if (0 == pk->u0.ip.ip_sa_mask)
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                addr = sal_ntohl(pk->u0.ip.ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("%s", ip_addr);
                addr = sal_ntohl(pk->u0.ip.ip_sa_mask);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("  %s\n", ip_addr);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }

        SYS_SCL_DBG_DUMP("  ip-da  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_DA))
        {
            if (0xFFFFFFFF == pk->u0.ip.ip_da_mask)
            {
                SYS_SCL_DBG_DUMP("host");
                addr = sal_ntohl(pk->u0.ip.ip_da);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
            }
            else if (0 == pk->u0.ip.ip_da_mask)
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                addr = sal_ntohl(pk->u0.ip.ip_da);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("%s", ip_addr);
                addr = sal_ntohl(pk->u0.ip.ip_da_mask);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP("  %s\n", ip_addr);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }
    }

    SCL_DUMP_VLAN_INFO(flag, pk, IPV4);
    SCL_DUMP_MAC_INFO(flag, pk, IPV4);
    SCL_DUMP_L2_L3_TYPE(flag, pk, IPV4);
    SCL_DUMP_L3_INFO(flag, pk, IPV4);
    SCL_DUMP_L4_PROTOCOL(flag, pk, IPV4);
    return CTC_E_NONE;
}

/*
 * show ipv6 entry
 */
STATIC int32
_sys_goldengate_scl_show_tcam_ipv6(uint8 lchip, sys_scl_sw_tcam_key_ipv6_t* pk)
{
    uint32 flag     = pk->flag;
    uint32 sub_flag = pk->sub_flag;

    {
        char        buf[CTC_IPV6_ADDR_STR_LEN];
        ipv6_addr_t ipv6_addr;
        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);
        sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));
        SYS_SCL_DBG_DUMP("  ip-sa  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
        {
            if ((0xFFFFFFFF == pk->u0.ip.ip_sa_mask[0]) && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[1])
                && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[2]) && (0xFFFFFFFF == pk->u0.ip.ip_sa_mask[3]))
            {
                SYS_SCL_DBG_DUMP("host ");

                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
            else if ((0 == pk->u0.ip.ip_sa_mask[0]) && (0 == pk->u0.ip.ip_sa_mask[1])
                     && (0 == pk->u0.ip.ip_sa_mask[2]) && (0 == pk->u0.ip.ip_sa_mask[3]))
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa[3]);
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s ", buf);

                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_sa_mask[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_sa_mask[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_sa_mask[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_sa_mask[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }

        sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);

        SYS_SCL_DBG_DUMP("  ip-da  ");
        if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_DA))
        {
            if ((0xFFFFFFFF == pk->u0.ip.ip_da_mask[0]) && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[1])
                && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[2]) && (0xFFFFFFFF == pk->u0.ip.ip_da_mask[3]))
            {
                SYS_SCL_DBG_DUMP("host ");

                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
            else if ((0 == pk->u0.ip.ip_da_mask[0]) && (0 == pk->u0.ip.ip_da_mask[1])
                     && (0 == pk->u0.ip.ip_da_mask[2]) && (0 == pk->u0.ip.ip_da_mask[3]))
            {
                SYS_SCL_DBG_DUMP("any\n");
            }
            else
            {
                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s ", buf);

                ipv6_addr[0] = sal_htonl(pk->u0.ip.ip_da_mask[0]);
                ipv6_addr[1] = sal_htonl(pk->u0.ip.ip_da_mask[1]);
                ipv6_addr[2] = sal_htonl(pk->u0.ip.ip_da_mask[2]);
                ipv6_addr[3] = sal_htonl(pk->u0.ip.ip_da_mask[3]);

                sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%20s\n", buf);
            }
        }
        else
        {
            SYS_SCL_DBG_DUMP("any\n");
        }
    }
    SCL_DUMP_VLAN_INFO(flag, pk, IPV6);
    SCL_DUMP_MAC_INFO(flag, pk, IPV6);
    SCL_DUMP_L2_L3_TYPE(flag, pk, IPV6);
    SCL_DUMP_L3_INFO(flag, pk, IPV6);
    SCL_DUMP_L4_PROTOCOL(flag, pk, IPV6);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_FLOW_LABEL))
    {
        SYS_SCL_DBG_DUMP("  flow-label 0x%x flow-label 0x%x\n",
                         pk->flow_label, pk->flow_label_mask);
    }

    return CTC_E_NONE;
}


/*
 * show mpls entry
 */
STATIC int32
_sys_goldengate_scl_show_tcam_vlan(uint8 lchip, sys_scl_sw_tcam_key_vlan_t* pk)
{
    uint32 flag = pk->flag;


    SCL_DUMP_VLAN_INFO(flag, pk, VLAN);

    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_CTAG_VALID))
    {
        SYS_SCL_DBG_DUMP("  Ctag valid: 0x%x\n", pk->ctag_valid);
    }
    if (CTC_FLAG_ISSET(flag, CTC_SCL_TCAM_VLAN_KEY_FLAG_STAG_VALID))
    {
        SYS_SCL_DBG_DUMP("  Stag valid: 0x%x\n", pk->stag_valid);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_cvlan(uint8 lchip, sys_scl_sw_hash_key_vlan_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Cvlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->vid);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_svlan(uint8 lchip, sys_scl_sw_hash_key_vlan_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->vid);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_2vlan(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d Cvlan:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->svid, pk->cvid);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_cvlan_cos(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Cvlan:%d Ccos:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->cvid, pk->ccos);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_svlan_cos(uint8 lchip, sys_scl_sw_hash_key_vtag_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  dir:%d  Svlan:%d Scos:%d\n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->dir, pk->svid, pk->scos);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_mac(uint8 lchip, sys_scl_sw_hash_key_mac_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  %-5s:", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport, pk->use_macda ? "MacDa" : "MacSa");

    SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x\n", pk->mac[0], pk->mac[1], pk->mac[2],
                     pk->mac[3], pk->mac[4], pk->mac[5]);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_mac(uint8 lchip, sys_scl_sw_hash_key_mac_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:", pk->use_macda ? "MacDa" : "MacSa");

    SYS_SCL_DBG_DUMP(" %02x%02x.%02x%02x.%02x%02x\n", pk->mac[0], pk->mac[1], pk->mac[2],
                     pk->mac[3], pk->mac[4], pk->mac[5]);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_ipv4(uint8 lchip, sys_scl_sw_hash_key_ipv4_t* pk)
{
    uint32 addr;
    char   ip_addr[16];

    SYS_SCL_DBG_DUMP("  %-5s:0x%x  Ipv4-sa:", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_SCL_DBG_DUMP(" %s\n", ip_addr);

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_hash_ipv4(uint8 lchip, sys_scl_sw_hash_key_ipv4_t* pk)
{
    uint32 addr;
    char   ip_addr[16];

    SYS_SCL_DBG_DUMP("  Ipv4-sa:");
    addr = sal_ntohl(pk->ip_sa);
    sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
    SYS_SCL_DBG_DUMP(" %s\n", ip_addr);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_ipv6(uint8 lchip, sys_scl_sw_hash_key_ipv6_t* pk)
{
    char        buf[CTC_IPV6_ADDR_STR_LEN];
    ipv6_addr_t ipv6_addr;
    sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));

    ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);
    ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);
    ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);
    ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);

    sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_SCL_DBG_DUMP("  Ipv6-sa:%20s\n", buf);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_port_ipv6(uint8 lchip, sys_scl_sw_hash_key_ipv6_t* pk)
{
    char        buf[CTC_IPV6_ADDR_STR_LEN];
    ipv6_addr_t ipv6_addr;
    sal_memset(buf, 0, CTC_IPV6_ADDR_STR_LEN);
    sal_memset(ipv6_addr, 0, sizeof(ipv6_addr_t));

    SYS_SCL_DBG_DUMP("  %-5s:0x%x  Ipv6-sa:", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);

    ipv6_addr[0] = sal_htonl(pk->ip_sa[0]);
    ipv6_addr[1] = sal_htonl(pk->ip_sa[1]);
    ipv6_addr[2] = sal_htonl(pk->ip_sa[2]);
    ipv6_addr[3] = sal_htonl(pk->ip_sa[3]);

    sal_inet_ntop(AF_INET6, ipv6_addr, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_SCL_DBG_DUMP(" %s\n", buf);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_l2(uint8 lchip, sys_scl_sw_hash_key_l2_t* pk)
{
    SYS_SCL_DBG_DUMP("  mac-da %02x%02x.%02x%02x.%02x%02x \n",
                     pk->macda[0], pk->macda[1], pk->macda[2],
                     pk->macda[3], pk->macda[4], pk->macda[5]);
    SYS_SCL_DBG_DUMP("  mac-sa %02x%02x.%02x%02x.%02x%02x \n",
                     pk->macsa[0], pk->macsa[1], pk->macsa[2],
                     pk->macsa[3], pk->macsa[4], pk->macsa[5]);
    SYS_SCL_DBG_DUMP("  eth-type 0x%x,  class-id 0x%x, field_sel_id %d\n", pk->ether_type, pk->class_id, pk->field_sel);
    SYS_SCL_DBG_DUMP("  stag-exist %d, vlan-id %d, cos %d, cfi %d\n", pk->stag_exist, pk->svid, pk->scos, pk->scfi);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_port_default_ingress(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  [Ingress] \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_port_default_egress(uint8 lchip, sys_scl_sw_hash_key_port_t* pk)
{
    SYS_SCL_DBG_DUMP("  %-5s:0x%x  [Egress] \n", pk->gport_is_classid ? ("Class") : ("Gport"), pk->gport);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_tunnel_ipv4(uint8 lchip, sys_scl_hash_tunnel_ipv4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_tunnel_ipv4_gre(uint8 lchip, sys_scl_hash_tunnel_ipv4_gre_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 gre key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_tunnel_ipv4_rpf(uint8 lchip, sys_scl_hash_tunnel_ipv4_rpf_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash tunnel ipv4 rpf key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_uc_ipv4_mode0(sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre uc ipv4 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_uc_ipv4_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre uc ipv4 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_mc_ipv4_mode0(sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre mc ipv4 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_ipv4_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre ipv4 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_uc_ipv6_mode0(sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre uc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_uc_ipv6_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre uc ipv6 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_mc_ipv6_mode0(sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre mc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_nvgre_mc_ipv6_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash nvgre mc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_uc_ipv4_mode0(sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan uc ipv4 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_uc_ipv4_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan uc ipv4 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_mc_ipv4_mode0(sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan mc ipv4 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_ipv4_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v4_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan ipv4 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_uc_ipv6_mode0(sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan uc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_uc_ipv6_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan uc ipv6 mode1 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_mc_ipv6_mode0(sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan mc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_hash_vxlan_mc_ipv6_mode1(uint8 lchip, sys_scl_hash_overlay_tunnel_v6_key_t* pk)
{
    CTC_PTR_VALID_CHECK(pk);

    SYS_SCL_DBG_DUMP("SCL hash vxlan mc ipv6 mode0 key:\n");
    SYS_SCL_DBG_DUMP("---------------------------------:\n");

    return CTC_E_NONE;
}
#endif

/*
 * show scl action
 */
STATIC int32
_sys_goldengate_scl_show_egs_action(uint8 lchip, sys_scl_sw_egs_action_t* pea)
{
    char* tag_op[CTC_VLAN_TAG_OP_MAX];
    char* tag_sl[CTC_VLAN_TAG_SL_MAX];
    tag_op[CTC_VLAN_TAG_OP_NONE]       = "None";
    tag_op[CTC_VLAN_TAG_OP_REP_OR_ADD] = "Replace or Add";
    tag_op[CTC_VLAN_TAG_OP_ADD]        = "Add";
    tag_op[CTC_VLAN_TAG_OP_DEL]        = "Delete";
    tag_op[CTC_VLAN_TAG_OP_REP]        = "Replace";
    tag_op[CTC_VLAN_TAG_OP_VALID]      = "Valid";

    tag_sl[CTC_VLAN_TAG_SL_AS_PARSE]    = "As parser   ";
    tag_sl[CTC_VLAN_TAG_SL_ALTERNATIVE] = "Alternative";
    tag_sl[CTC_VLAN_TAG_SL_NEW]         = "New        ";
    tag_sl[CTC_VLAN_TAG_SL_DEFAULT]     = "Default    ";

    CTC_PTR_VALID_CHECK(pea);

    SYS_SCL_DBG_DUMP("SCL Egress action\n");
    SYS_SCL_DBG_DUMP("-----------------------------------------------\n");


    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_DISCARD))
    {
        SYS_SCL_DBG_DUMP("  --discard\n");
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_STATS))
    {
        SYS_SCL_DBG_DUMP("  --stats\n");
    }

    if (CTC_FLAG_ISSET(pea->flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        if (pea->stag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  --stag op:  %s\n", tag_op[pea->stag_op]);
            if (pea->stag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  --svid sl:  %s, new svid:  %d\n", tag_sl[pea->svid_sl], pea->svid);
                SYS_SCL_DBG_DUMP("  --scos sl:  %s, new scos:  %d\n", tag_sl[pea->scos_sl], pea->scos);
                SYS_SCL_DBG_DUMP("  --scfi sl:  %s, new scfi:  %d\n", tag_sl[pea->scfi_sl], pea->scfi);
            }
        }
        if (pea->ctag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  --ctag op:  %s\n", tag_op[pea->ctag_op]);
            if (pea->ctag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  --cvid sl:  %s, new cvid:  %d\n", tag_sl[pea->cvid_sl], pea->cvid);
                SYS_SCL_DBG_DUMP("  --ccos sl:  %s, new ccos:  %d\n", tag_sl[pea->ccos_sl], pea->ccos);
                SYS_SCL_DBG_DUMP("  --ccfi sl:  %s, new ccfi:  %d\n", tag_sl[pea->ccfi_sl], pea->ccfi);
            }
        }
    }
    return CTC_E_NONE;
}

/*
 * show scl action
 */
STATIC int32
_sys_goldengate_scl_show_igs_action(uint8 lchip, sys_scl_sw_igs_action_t* pia)
{

    char  * tag_op[CTC_VLAN_TAG_OP_MAX];
    char  * tag_sl[CTC_VLAN_TAG_SL_MAX];
    char  * domain_sl[CTC_SCL_VLAN_DOMAIN_MAX];

    tag_op[CTC_VLAN_TAG_OP_NONE]       = "None";
    tag_op[CTC_VLAN_TAG_OP_REP_OR_ADD] = "Replace or Add";
    tag_op[CTC_VLAN_TAG_OP_ADD]        = "Add";
    tag_op[CTC_VLAN_TAG_OP_DEL]        = "Delete";
    tag_op[CTC_VLAN_TAG_OP_REP]        = "Replace";
    tag_op[CTC_VLAN_TAG_OP_VALID]      = "Valid";

    tag_sl[CTC_VLAN_TAG_SL_AS_PARSE]    = "As parser   ";
    tag_sl[CTC_VLAN_TAG_SL_ALTERNATIVE] = "Alternative";
    tag_sl[CTC_VLAN_TAG_SL_NEW]         = "New        ";
    tag_sl[CTC_VLAN_TAG_SL_DEFAULT]     = "Default    ";

    domain_sl[CTC_SCL_VLAN_DOMAIN_SVLAN]    = "Svlan domain ";
    domain_sl[CTC_SCL_VLAN_DOMAIN_CVLAN]  = "Cvlan domain ";
    domain_sl[CTC_SCL_VLAN_DOMAIN_UNCHANGE]  = "Unchange ";

    CTC_PTR_VALID_CHECK(pia);

    SYS_SCL_DBG_DUMP("SCL ingress action\n");
    SYS_SCL_DBG_DUMP("-----------------------------------------------\n");

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_DISCARD))
    {
        SYS_SCL_DBG_DUMP("  --discard\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        SYS_SCL_DBG_DUMP("  --stats\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_STP_ID))
    {
        SYS_SCL_DBG_DUMP("  --stp id: %u\n", pia->stp_id);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_DSCP))
    {
        SYS_SCL_DBG_DUMP("  --dscp: %u\n", pia->dscp);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY))
    {
        SYS_SCL_DBG_DUMP("  --priority: %u\n", pia->priority);
    }
    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_COLOR))
    {
        SYS_SCL_DBG_DUMP("  --color: %u\n", pia->color);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        SYS_SCL_DBG_DUMP("  --priority: %u color: %u,", pia->priority, pia->color);
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_POLICER_EN))
    {
        SYS_SCL_DBG_DUMP("  --service policer id: %u\n", pia->service_policer_id);
    }

    if (CTC_FLAG_ISSET(pia->sub_flag, CTC_SCL_IGS_ACTION_SUB_FLAG_SERVICE_ACL_EN))
    {
        SYS_SCL_DBG_DUMP("  --service acl enable\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_SCL_DBG_DUMP("  --copy to cpu\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
    {
        SYS_SCL_DBG_DUMP("  --redirect nhid: %u\n", pia->nh_id);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_AGING))
    {
        SYS_SCL_DBG_DUMP("  --aging\n");
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        SYS_SCL_DBG_DUMP("  --fid %u\n", pia->fid);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        SYS_SCL_DBG_DUMP("  --vrf id %u\n", pia->fid);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT) && (pia->vlan_edit))
    {
        if (pia->vlan_edit->stag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  --stag op:  %s\n", tag_op[pia->vlan_edit->stag_op]);
            if (pia->vlan_edit->stag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  --svid sl:  %s, new svid:  %d\n", tag_sl[pia->vlan_edit->svid_sl], pia->svid);
                SYS_SCL_DBG_DUMP("  --scos sl:  %s, new scos:  %d\n", tag_sl[pia->vlan_edit->scos_sl], pia->scos);
                SYS_SCL_DBG_DUMP("  --scfi sl:  %s, new scfi:  %d\n", tag_sl[pia->vlan_edit->scfi_sl], pia->scfi);
            }
        }
        if (pia->vlan_edit->ctag_op != CTC_VLAN_TAG_OP_NONE)
        {
            SYS_SCL_DBG_DUMP("  --ctag op:  %s\n", tag_op[pia->vlan_edit->ctag_op]);
            if (pia->vlan_edit->ctag_op != CTC_VLAN_TAG_OP_DEL)
            {
                SYS_SCL_DBG_DUMP("  --cvid sl:  %s, new cvid:  %d\n", tag_sl[pia->vlan_edit->cvid_sl], pia->cvid);
                SYS_SCL_DBG_DUMP("  --ccos sl:  %s, new ccos:  %d\n", tag_sl[pia->vlan_edit->ccos_sl], pia->ccos);
                SYS_SCL_DBG_DUMP("  --ccfi sl:  %s, new ccfi:  %d\n", tag_sl[pia->vlan_edit->ccfi_sl], pia->ccfi);
            }
        }

        SYS_SCL_DBG_DUMP("  --vlan domain:  %s\n", domain_sl[pia->vlan_edit->vlan_domain]);

        SYS_SCL_DBG_DUMP("  --vlan action profile id: %d \n", pia->vlan_edit->chip.profile_id);

    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
    {
        SYS_SCL_DBG_DUMP("  --logic port: %d  , logic port type: %d\n", pia->bind.bds.logic_src_port, pia->bind.bds.logic_port_type);
    }


    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        SYS_SCL_DBG_DUMP("  --binding:");
        if (pia->binding_mac_sa)
        {
            SYS_SCL_DBG_DUMP("  mac-sa:%.4x.%.4x.%.4x%s ", sal_ntohs(*(unsigned short*)&pia->bind.mac_sa[0]),
                            sal_ntohs(*(unsigned short*)&pia->bind.mac_sa[2]),
                            sal_ntohs(*(unsigned short*)&pia->bind.mac_sa[4])," ");
        }
        else
        {
            uint32 ip_sa = 0;
            uint32 addr;
            char   ip_addr[16];

            switch (pia->bind_type)
            {
            case 0:
                SYS_SCL_DBG_DUMP("  Port:0x%X\n", (pia->bind.gport));
                break;
            case 1:
                SYS_SCL_DBG_DUMP("  Vlan:%u\n", (pia->bind.vlan_id) & 0x3FF);
                break;
            case 2:
                SYS_SCL_DBG_DUMP("  IPv4sa:");
                ip_sa = pia->bind.ipv4_sa;
                addr  = sal_ntohl(ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
                break;
            case 3:
                SYS_SCL_DBG_DUMP("  Vlan:%u, IPv4sa:", (pia->bind.ip_vlan.vlan_id) & 0x3FF);

                ip_sa = pia->bind.ip_vlan.ipv4_sa;
                addr  = sal_ntohl(ip_sa);
                sal_inet_ntop(AF_INET, &addr, ip_addr, sizeof(ip_addr));
                SYS_SCL_DBG_DUMP(" %s\n", ip_addr);
                break;
            }
        }
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        if(SYS_SCL_BY_PASS_VLAN_PTR == pia->user_vlan_ptr)
        {
            SYS_SCL_DBG_DUMP("  --Bypass vlan ptr , interface id: 0 \n");
        }
        else if(0x1000 == (pia->user_vlan_ptr & 0x1C00))
        {
            SYS_SCL_DBG_DUMP("  --Interface id: %u\n", pia->user_vlan_ptr & 0x3FF);
        }
        else
        {
            SYS_SCL_DBG_DUMP("  --User vlan ptr: %u\n", pia->user_vlan_ptr);
        }
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_APS))
    {
        SYS_SCL_DBG_DUMP("  --aps protected-vlan:%d  ,  aps select group_id:%d  ,  is working path:%d\n",
                         pia->user_vlan_ptr, pia->bind.bds.aps_select_group_id, !pia->bind.bds.aps_select_protecting_path);
    }

    if (CTC_FLAG_ISSET(pia->flag, CTC_SCL_IGS_ACTION_FLAG_ETREE_LEAF))
    {
        SYS_SCL_DBG_DUMP("  --Etree Leaf\n");
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_tunnel_action(uint8 lchip, sys_scl_tunnel_action_t* pta)
{
    CTC_PTR_VALID_CHECK(pta);

    SYS_SCL_DBG_DUMP("SCL tunnel action:\n");
    SYS_SCL_DBG_DUMP("-----------------------------------------------\n");

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_show_flow_action(uint8 lchip, sys_scl_sw_flow_action_t* pfa)
{
    CTC_PTR_VALID_CHECK(pfa);

    SYS_SCL_DBG_DUMP("SCL Flow action:\n");
    SYS_SCL_DBG_DUMP("-----------------------------------------------\n");

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_DISCARD))
    {
        SYS_SCL_DBG_DUMP("  --discard\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_STATS))
    {
        SYS_SCL_DBG_DUMP("  --stats\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_PRIORITY))
    {
        SYS_SCL_DBG_DUMP("  --priority: %u\n", pfa->priority);
    }
    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_COLOR))
    {
        SYS_SCL_DBG_DUMP("  --color: %u\n", pfa->color);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_COPY_TO_CPU))
    {
        SYS_SCL_DBG_DUMP("  --copy to cpu\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_REDIRECT))
    {
        SYS_SCL_DBG_DUMP("  --redirect nhid: %u\n", pfa->nh_id);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_BRIDGE))
    {
        SYS_SCL_DBG_DUMP("  --deny bridge\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_LEARNING))
    {
        SYS_SCL_DBG_DUMP("  --deny learning\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_DENY_ROUTE))
    {
        SYS_SCL_DBG_DUMP("  --deny route\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_BRIDGE))
    {
        SYS_SCL_DBG_DUMP("  --force bridge\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_LEARNING))
    {
        SYS_SCL_DBG_DUMP("  --force learning\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_FORCE_ROUTE))
    {
        SYS_SCL_DBG_DUMP("  --force route\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_TRUST))
    {
        uint32 trust;
        sys_goldengate_common_unmap_qos_policy(lchip, pfa->qos_policy, &trust);
        SYS_SCL_DBG_DUMP("  --trust: %u\n", trust);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SYS_SCL_DBG_DUMP("  --macro policer id: %u\n", pfa->macro_policer_id);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_MICRO_FLOW_POLICER))
    {
        SYS_SCL_DBG_DUMP("  --macro policer id: %u\n", pfa->micro_policer_id);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_MICRO_FLOW_POLICER) +
        CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_MACRO_FLOW_POLICER))
    {
        SYS_SCL_DBG_DUMP("\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_METADATA))
    {
        SYS_SCL_DBG_DUMP("  --metadata: %u,", pfa->metadata);
    }
    else if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_FID))
    {
        SYS_SCL_DBG_DUMP("  --fid: %u,", pfa->fid);
    }
    else if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_VRFID))
    {
        SYS_SCL_DBG_DUMP(" --vrfid: %u,", pfa->vrfid);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_HASH_EN))
    {
        SYS_SCL_DBG_DUMP("  --acl flow hash type: %u,", pfa->acl_flow_hash_type);
        SYS_SCL_DBG_DUMP("  acl flow hash field sel: %u\n", pfa->acl_flow_hash_field_sel);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_ACL_FLOW_TCAM_EN))
    {
        SYS_SCL_DBG_DUMP("  --acl tcam,");
        SYS_SCL_DBG_DUMP("  acl flow tcam type: %u,", pfa->acl_flow_tcam_lookup_type);
        SYS_SCL_DBG_DUMP("  acl flow tcam label: %u\n", pfa->acl_flow_tcam_label);
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_POSTCARD_EN))
    {
        SYS_SCL_DBG_DUMP("  --postcard\n");
    }

    if (CTC_FLAG_ISSET(pfa->flag, CTC_SCL_FLOW_ACTION_FLAG_OVERWRITE_PORT_IPFIX))
    {
        SYS_SCL_DBG_DUMP("  --ipfix hash type: %u,", pfa->ipfix_hash_type);
        SYS_SCL_DBG_DUMP("  ipfix hash field sel: %u\n", pfa->ipfix_hash_field_sel);
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_entry_detail(uint8 lchip, sys_scl_sw_entry_t* pe)
{
    uint8 block_id;
    uint8 step = 0;
    uint32 key_id = 0;
    uint32 act_id = 0;
    char key_name[50] = {0};
    char ad_name[50] = {0};
    uint32 tcam_ad_index = 0;

    CTC_PTR_VALID_CHECK(pe);
    block_id = pe->group->group_info.block_id;

    _sys_goldengate_scl_get_table_id(lchip, block_id, pe->key.type, pe->action->type, &key_id, &act_id);

    drv_goldengate_get_tbl_string_by_id(key_id, key_name);

    if (0xFFFFFFFF != act_id) /* normal egress ad DsVlanXlate is in key */
    {
        drv_goldengate_get_tbl_string_by_id(act_id, ad_name);
    }
    else
    {
        sal_memcpy(ad_name, key_name, sizeof(key_name));
    }

    if((SYS_SCL_ACTION_TUNNEL== pe->action->type) && (pe->action->u0.ta.rpf_check_en))
    {
        sal_strcpy(ad_name, "DsTunnelIdRpf");
    }

    if(SCL_ENTRY_IS_TCAM(pe->key.type))
    {
        if(SYS_SCL_ACTION_INGRESS == pe->action->type)
        {
            if(1 == block_id)
            {
                sal_strcpy(ad_name, "DsUserId1Tcam");
            }
            else
            {
                sal_strcpy(ad_name, "DsUserId0Tcam");
            }
        }
        else if((SYS_SCL_ACTION_TUNNEL == pe->action->type) && (pe->action->u0.ta.rpf_check_en))
        {
            if(1 == block_id)
            {
                sal_strcpy(ad_name, "DsTunnelIdRpfTcam1");
            }
            else
            {
                sal_strcpy(ad_name, "DsTunnelIdRpfTcam0");
            }
        }
        else if(SYS_SCL_ACTION_TUNNEL == pe->action->type)
        {
            if(1 == block_id)
            {
                sal_strcpy(ad_name, "DsTunnelId1Tcam");
            }
            else
            {
                sal_strcpy(ad_name, "DsTunnelId0Tcam");
            }
        }
    }

    SYS_SCL_DBG_DUMP("\n");
    SYS_SCL_DBG_DUMP("Detail Entry Info\n");
    SYS_SCL_DBG_DUMP("-----------------------------------------------\n");

    if((SYS_SCL_KEY_PORT_DEFAULT_INGRESS != pe->key.type) && (SYS_SCL_KEY_PORT_DEFAULT_EGRESS != pe->key.type))
    {
        if(SCL_ENTRY_IS_TCAM(pe->key.type))
        {
            step = SYS_SCL_MAP_FPA_SIZE_TO_STEP(p_gg_scl_master[lchip]->alloc_id[pe->key.type]);
            /*SYS_SCL_DBG_DUMP("  --%-32s :0x%-8X\n", key_name, pe->fpae.offset_a / step);*/
            SYS_SCL_DBG_DUMP("  --%-32s :%-8u\n", key_name, pe->fpae.offset_a / step);
        }
        else
        {
            SYS_SCL_DBG_DUMP("  --%-32s :0x%-8X\n", key_name, pe->fpae.offset_a);
        }
    }

    if ((SCL_ENTRY_IS_TCAM(pe->key.type)) || (SYS_SCL_IS_DEFAULT(pe->key.type)))  /* default index also get from key_index */
    {
        tcam_ad_index = pe->fpae.offset_a;

        if (0xFFFFFFFF != act_id) /* normal egress ad DsVlanXlate is in key */
        {
            /*SYS_SCL_DBG_DUMP("  --%-32s :0x%-8X\n", ad_name, tcam_ad_index);*/
            SYS_SCL_DBG_DUMP("  --%-32s :%-8u\n", ad_name, tcam_ad_index);
        }

    }
    else
    {
        if (0xFFFFFFFF != act_id) /* normal egress ad DsVlanXlate is in key */
        {
            SYS_SCL_DBG_DUMP("  --%-32s :0x%-8X\n", ad_name, pe->action->ad_index);
        }
    }

    SYS_SCL_DBG_DUMP("\n");

    switch (pe->action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        _sys_goldengate_scl_show_igs_action(lchip, &pe->action->u0.ia);
        break;
    case SYS_SCL_ACTION_EGRESS:
        _sys_goldengate_scl_show_egs_action(lchip, &pe->action->u0.ea);
        break;
    case SYS_SCL_ACTION_TUNNEL:
        _sys_goldengate_scl_show_tunnel_action(lchip, &pe->action->u0.ta);
        break;
    case SYS_SCL_ACTION_FLOW:
        _sys_goldengate_scl_show_flow_action(lchip, &pe->action->u0.fa);
        break;
    default:
        break;
    }



    return CTC_E_NONE;
}




#define SCL_PRINT_CHIP(lchip)    SYS_SCL_DBG_DUMP("++++++++++++++++ lchip %d ++++++++++++++++\n", lchip);


STATIC char*
_sys_goldengate_scl_get_type(uint8 lchip, uint8 key_type)
{
    switch (key_type)
    {
    case SYS_SCL_KEY_TCAM_VLAN:
        return "tcam_vlan";
    case SYS_SCL_KEY_TCAM_MAC:
        return "tcam_mac";
    case SYS_SCL_KEY_TCAM_IPV4:
        return "tcam_ipv4";
    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
        return "tcam_ipv4_single";
    case SYS_SCL_KEY_TCAM_IPV6:
        return "tcam_ipv6";
    case SYS_SCL_KEY_HASH_PORT:
        return "hash_port";
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
        return "hash_port_cvlan";
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        return "hash_port_svlan";
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
        return "hash_port_2vlan";
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
        return "hash_port_cvlan_cos";
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        return "hash_port_svlan_cos";
    case SYS_SCL_KEY_HASH_MAC:
        return "hash_mac";
    case SYS_SCL_KEY_HASH_PORT_MAC:
        return "hash_port_mac";
    case SYS_SCL_KEY_HASH_IPV4:
        return "hash_ipv4";
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        return "hash_port_ipv4";
    case SYS_SCL_KEY_HASH_IPV6:
        return "hash_ipv6";
     case SYS_SCL_KEY_HASH_PORT_IPV6:
        return "hash_port_ipv6";
    case SYS_SCL_KEY_HASH_L2:
        return "hash_l2";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
        return "hash_tunnel_ipv4";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
        return "hash_tunnel_ipv4_da";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
        return "hash_tunnel_ipv4_gre";
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
        return "hash_tunnel_ipv4_rpf";
    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
        return "hash_nvgre_uc_v4_mode0";
    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
        return "hash_nvgre_uc_v4_mode1";
    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
        return "hash_nvgre_mc_v4_mode0";
    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
        return "hash_nvgre_v4_mode";
    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
        return "hash_nvgre_uc_v6_mode0";
    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
        return "hash_nvgre_uc_v6_mode1";
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
        return "hash_nvgre_mc_v6_mode0";
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
        return "hash_nvgre_mc_v6_mode1";
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
        return "hash_vxlan_uc_v4_mode0";
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
        return "hash_vxlan_uc_v4_mode1";
    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
        return "hash_vxlan_mc_v4_mode0";
    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
        return "hash_vxlan_v4_mode1";
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
        return "hash_vxlan_uc_v6_mode0";
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
        return "hash_vxlan_uc_v6_mode1";
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
        return "hash_vxlan_mc_v6_mode0";
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
        return "hash_vxlan_mc_v6_mode1";
    case SYS_SCL_KEY_HASH_TRILL_UC:
        return "hash_trill_uc";
    case SYS_SCL_KEY_HASH_TRILL_MC:
        return "hash_trill_mc";
    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
        return "hash_trill_uc_rpf";
    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
        return "hash_trill_mc_rpf";
    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        return "hash_trill_mc_adj";
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        return "port_default_ingress";
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        return "port_default_egress";
    default:
        return "XXXXXX";
    }
}


STATIC int32
_sys_goldengate_scl_show_vlan_mapping_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char str[35];
    char format[10];
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
    uint32 tempip = 0;
    uint32 ipv6_address[4] = {0, 0, 0, 0};

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GET_NODES_FAILED;
    }

    if ((SYS_SCL_ALL_KEY != key_type)
        && (pe->key.type != key_type))
    {
        return CTC_E_NONE;
    }

    if (detail)
    {

    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-5s%-8s%-6s%-6s", "No.", "ENTRY_ID", "DIR", "PORT", "SVLAN", "CVLAN");
            SYS_SCL_DBG_DUMP("%-5s%-5s%-16s%-40s\n", "SCOS", "CCOS", "MACSA", "IPSA");
            SYS_SCL_DBG_DUMP("--------------------------------------------------------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }


    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }


        SYS_SCL_DBG_DUMP("%-5s", (pe->action->type == SYS_SCL_ACTION_INGRESS) ? "IGS" : "EGS");

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_TCAM_VLAN:
                SYS_SCL_DBG_DUMP("0x%-6.4x", pg->group_info.u0.gport);

                if(pe->key.u0.vlan_key.svlan_mask)
                {
                    SYS_SCL_DBG_DUMP("%-6u", pe->key.u0.vlan_key.svlan);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-6s", "-");
                }

                if(pe->key.u0.vlan_key.cvlan_mask)
                {
                    SYS_SCL_DBG_DUMP("%-6u", pe->key.u0.vlan_key.cvlan);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-6s", "-");
                }

                if(pe->key.u0.vlan_key.stag_cos_mask)
                {
                    SYS_SCL_DBG_DUMP("%-5u", pe->key.u0.vlan_key.stag_cos);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-5s", "-");
                }

                if(pe->key.u0.vlan_key.ctag_cos_mask)
                {
                    SYS_SCL_DBG_DUMP("%-5u", pe->key.u0.vlan_key.ctag_cos);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-5s", "-");
                }

                SYS_SCL_DBG_DUMP("%-16s%-40s\n", "-", "-");
                break;

            case SYS_SCL_KEY_HASH_PORT:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6s%-5s%-5s%-16s%-40s\n",pe->key.u0.hash.u0.port.gport,"-","-", "-","-", "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6u%-5s%-5s%-16s%-40s\n",pe->key.u0.hash.u0.vlan.gport, "-",pe->key.u0.hash.u0.vlan.vid, "-","-", "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6u%-6s%-5s%-5s%-16s%-40s\n",pe->key.u0.hash.u0.vlan.gport, pe->key.u0.hash.u0.vlan.vid,"-", "-", "-", "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_2VLAN:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6u%-6u%-5s%-5s%-16s%-40s\n",pe->key.u0.hash.u0.vtag.gport, pe->key.u0.hash.u0.vtag.svid,
                    pe->key.u0.hash.u0.vtag.cvid, "-", "-", "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6u%-5s%-5u%-16s%-40s\n",pe->key.u0.hash.u0.vtag.gport, "-",pe->key.u0.hash.u0.vtag.cvid,
                    "-", pe->key.u0.hash.u0.vtag.ccos, "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
                 SYS_SCL_DBG_DUMP("0x%-6.4x%-6u%-6s%-5u%-5s%-16s%-40s\n",pe->key.u0.hash.u0.vtag.gport, pe->key.u0.hash.u0.vtag.svid, "-",
                    pe->key.u0.hash.u0.vtag.scos, "-", "-","-");
                break;

            case SYS_SCL_KEY_HASH_PORT_MAC:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6s%-5s%-5s",pe->key.u0.hash.u0.mac.gport,"-","-", "-","-");
                SYS_SCL_DBG_DUMP("%.4x.%.4x.%.4x%s ", sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[0]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[2]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[4])," ");
                SYS_SCL_DBG_DUMP("%-40s\n", "-");
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV4:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6s%-5s%-5s%-16s",pe->key.u0.hash.u0.ipv4.gport,"-","-", "-","-","-");
                tempip = sal_ntohl(pe->key.u0.hash.u0.ipv4.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-40s\n", buf);
                break;

            case SYS_SCL_KEY_HASH_PORT_IPV6:
                SYS_SCL_DBG_DUMP("0x%-6.4x%-6s%-6s%-5s%-5s%-16s",pe->key.u0.hash.u0.ipv6.gport,"-","-", "-","-","-");
                ipv6_address[0] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[0]);
                ipv6_address[1] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[1]);
                ipv6_address[2] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[2]);
                ipv6_address[3] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[3]);

                sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-40s\n", buf);
                break;

            default:    /* never */
                return CTC_E_SCL_INVALID_KEY_TYPE;
        }
    }

    if (detail)
    {

    }

    (*p_cnt)++;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_vlan_class_mac_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char str[35];
    char format[10];


    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GET_NODES_FAILED;
    }

    if (detail)
    {

    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-16s%-16s\n", "No.", "ENTRY_ID", "MACSA", "MACDA");
            SYS_SCL_DBG_DUMP("------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }

    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_TCAM_MAC:
                if (CTC_FLAG_ISSET(pe->key.u0.mac_key.flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_SA))
                {
                    SYS_SCL_DBG_DUMP("%.4x.%.4x.%.4x%s ", sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_sa[0]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_sa[2]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_sa[4])," ");
                    SYS_SCL_DBG_DUMP("%-16s\n", "-");
                }
                else if (CTC_FLAG_ISSET(pe->key.u0.mac_key.flag, CTC_SCL_TCAM_MAC_KEY_FLAG_MAC_DA))
                {
                    SYS_SCL_DBG_DUMP("%-16s", "-");
                    SYS_SCL_DBG_DUMP("%.4x.%.4x.%.4x%s \n", sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_da[0]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_da[2]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.mac_key.mac_da[4])," ");
                }

                break;

            case SYS_SCL_KEY_HASH_MAC:
                if (!pe->key.u0.hash.u0.mac.use_macda)
                {
                    SYS_SCL_DBG_DUMP("%.4x.%.4x.%.4x%s ", sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[0]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[2]),
                            sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[4])," ");
                    SYS_SCL_DBG_DUMP("%-16s\n", "-");
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-16s", "-");
                    SYS_SCL_DBG_DUMP("%.4x.%.4x.%.4x%s \n", sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[0]),
                                sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[2]),
                                sal_ntohs(*(unsigned short*)&pe->key.u0.hash.u0.mac.mac[4])," ");
                }
                break;

            default:    /* never */
                return CTC_E_SCL_INVALID_KEY_TYPE;
        }
    }

    if (detail)
    {

    }

    (*p_cnt)++;

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_show_vlan_class_ipv4_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char str[35];
    char format[10];
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
    char buf1[CTC_IPV6_ADDR_STR_LEN] = {0};
    uint32 tempip = 0;
    uint32 tempip1 = 0;


    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GET_NODES_FAILED;
    }


    if (detail)
    {

    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-18s%-18s", "No.", "ENTRY_ID", "IPSA", "IPDA");
            SYS_SCL_DBG_DUMP("%-8s%-12s%-12s\n", "L4TYPE", "L4SRC_PORT", "L4DST_PORT");
            SYS_SCL_DBG_DUMP("--------------------------------------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }

    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
                tempip = sal_ntohl(pe->key.u0.ipv4_key.u0.ip.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                tempip1 = sal_ntohl(pe->key.u0.ipv4_key.u0.ip.ip_da);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-18s%-18s", buf, buf1);
                SYS_SCL_DBG_DUMP("%-8s", pe->key.u0.ipv4_key.u0.ip.l4_type == CTC_PARSER_L4_TYPE_TCP ? "TCP" :"UDP");
                SYS_SCL_DBG_DUMP("%-12u%-12u\n", pe->key.u0.ipv4_key.u0.ip.l4_src_port, pe->key.u0.ipv4_key.u0.ip.l4_dst_port);
                break;

            case SYS_SCL_KEY_HASH_IPV4:
                tempip = sal_ntohl(pe->key.u0.hash.u0.ipv4.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-18s", buf);
                SYS_SCL_DBG_DUMP("%-18s%-8s%-12s%-12s\n", "-", "-", "-", "-");
                break;

            default:    /* never */
                return CTC_E_SCL_INVALID_KEY_TYPE;
        }
    }

    if (detail)
    {

    }

    (*p_cnt)++;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_vlan_class_ipv6_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char str[35];
    char format[10];
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
    char buf1[CTC_IPV6_ADDR_STR_LEN] = {0};
    uint32 ipv6_address[4] = {0, 0, 0, 0};
    uint32 ipv6_address1[4] = {0, 0, 0, 0};

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GET_NODES_FAILED;
    }

    if (detail)
    {

    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-44s", "No.", "ENTRY_ID", "IPSA (IPDA)");
            SYS_SCL_DBG_DUMP("%-8s%-12s%-12s\n", "L4TYPE", "L4SRC_PORT", "L4DST_PORT");
            SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }

    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_TCAM_IPV6:
                ipv6_address[0] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[0]);
                ipv6_address[1] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[1]);
                ipv6_address[2] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[2]);
                ipv6_address[3] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[3]);
                sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-44s", buf);

                ipv6_address1[0] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[0]);
                ipv6_address1[1] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[1]);
                ipv6_address1[2] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[2]);
                ipv6_address1[3] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_address1, buf1, CTC_IPV6_ADDR_STR_LEN);

                if (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_L4_TYPE))
                {
                    switch (pe->key.u0.ipv6_key.u0.ip.l4_type)
                    {
                        case CTC_PARSER_L4_TYPE_TCP:
                            SYS_SCL_DBG_DUMP("%-8s", "TCP");
                            break;

                        case CTC_PARSER_L4_TYPE_UDP:
                            SYS_SCL_DBG_DUMP("%-8s", "UDP");
                            break;

                        case CTC_PARSER_L4_TYPE_GRE:
                            SYS_SCL_DBG_DUMP("%-8s", "GRE");
                            break;

                        case CTC_PARSER_L4_TYPE_ICMP:
                            SYS_SCL_DBG_DUMP("%-8s", "ICMP");
                            break;

                        case CTC_PARSER_L4_TYPE_IGMP:
                            SYS_SCL_DBG_DUMP("%-8s", "IGMP");
                            break;

                        default:
                            SYS_SCL_DBG_DUMP("%-8s", "OTHER");
                            break;
                    }
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-8s", "-");
                }

                if (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_SRC_PORT))
                {
                    SYS_SCL_DBG_DUMP("%-12u", pe->key.u0.ipv6_key.u0.ip.l4_src_port);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-12s", "-");
                }

                if (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_L4_DST_PORT))
                {
                    SYS_SCL_DBG_DUMP("%-12u\n", pe->key.u0.ipv6_key.u0.ip.l4_dst_port);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-12s\n", "-");
                }

                SYS_SCL_DBG_DUMP("%-19s%-44s\n", " ", buf1);
                break;

            case SYS_SCL_KEY_HASH_IPV6:
                ipv6_address[0] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[0]);
                ipv6_address[1] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[1]);
                ipv6_address[2] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[2]);
                ipv6_address[3] = sal_ntohl(pe->key.u0.hash.u0.ipv6.ip_sa[3]);

                sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-44s", buf);
                SYS_SCL_DBG_DUMP("%-8s%-12s%-12s\n", "-", "-", "-");
                break;

            default:    /* never */
                return CTC_E_SCL_INVALID_KEY_TYPE;
        }
    }

    if (detail)
    {

    }

    (*p_cnt)++;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_tunnel_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char str[35];
    char format[10];
    char buf[CTC_IPV6_ADDR_STR_LEN] = {0};
    char buf1[CTC_IPV6_ADDR_STR_LEN] = {0};
    uint32 ipv6_address[4] = {0, 0, 0, 0};
    uint32 ipv6_address1[4] = {0, 0, 0, 0};
    uint32 tempip = 0;
    uint32 tempip1 = 0;
    uint32 gre_key = 0;
    uint32 vnid = 0;
    uint32 cmd    = 0;
    ds_t ipe_vxlan_nvgre_ipda_ctl;
    sys_scl_hash_overlay_tunnel_v4_key_t* pinfo = NULL;
    sys_scl_hash_overlay_tunnel_v6_key_t* pinfo1 = NULL;

    static char *tunnel_type[SYS_SCL_TUNNEL_TYPE_NUM] =
    {
        "NONE",
        "4in4",
        "6in4",
        "GREin4",
        "NVGREin4",
        "VXLANin4",
        "GENEVEin4",
        "IPV4in6",
        "IPV6in6",
        "GREin6",
        "NVGREin6",
        "VXLANin6",
        "GENEVEin6",
        "RPF",
        "TRILL",
    };
    CTC_PTR_VALID_CHECK(pe);

    sal_memset(&ipe_vxlan_nvgre_ipda_ctl, 0, sizeof(ipe_vxlan_nvgre_ipda_ctl));
    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GET_NODES_FAILED;
    }


    if (detail)
    {

    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-13s%-21s%-21s%-12s%-10s\n", "No.", "ENTRY_ID", "TYPE", "IPDA/","IPSA/", "GRE_KEY", "VNID");
            SYS_SCL_DBG_DUMP("%-6s%-13s%-13s%-21s%-21s%-12s%-10s\n", " ", " ", "TRILL", "EGS_NICK","IGS_NICK", "SRC_PORT", " ");
            SYS_SCL_DBG_DUMP("---------------------------------------------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }

    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }

        SYS_SCL_DBG_DUMP("%-13s", tunnel_type[pe->key.tunnel_type]);

        switch (pe->key.type)
        {
            case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
                tempip = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                tempip1 = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4.ip_da);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s%-12s%-10s\n", buf1, buf, "-", "-");
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
                tempip1 = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4.ip_da);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s%-12s%-10s\n", buf1,"-",  "-", "-");
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
                tempip = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4_gre.ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                tempip1 = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4_gre.ip_da);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s", buf1, buf);
                SYS_SCL_DBG_DUMP("%-12s%-10s\n", CTC_DEBUG_HEX_FORMAT(str, format, pe->key.u0.hash.u0.tnnl_ipv4_gre.gre_key, 8, U), "-");
                break;

            case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
                tempip1 = sal_ntohl(pe->key.u0.hash.u0.tnnl_ipv4_rpf.ip_sa);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s%-12s%-10s\n", "-", buf1, "-", "-");
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
            case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
            case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
                pinfo = &(pe->key.u0.hash.u0.ol_tnnl_v4);
                tempip1 = sal_ntohl(pinfo->ip_da);
                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s%-12s%-10s\n", buf1,"-",  "-", CTC_DEBUG_HEX_FORMAT(str, format, pinfo->vn_id, 6, U));
                break;

            case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
            case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
            case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
            case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
                pinfo = &(pe->key.u0.hash.u0.ol_tnnl_v4);

                if ((SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1 == pe->key.type) || (SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1 == pe->key.type))
                {
                    cmd = DRV_IOR(IpeVxlanNvgreIpdaCtl_t, DRV_ENTRY_FLAG);
                    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_vxlan_nvgre_ipda_ctl));
                    if (0 == pinfo->ip_da)
                    {
                        GetIpeVxlanNvgreIpdaCtl(A, array_0_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &tempip1);
                    }
                    else if (1 == pinfo->ip_da)
                    {
                        GetIpeVxlanNvgreIpdaCtl(A, array_1_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &tempip1);
                    }
                    else if (2 == pinfo->ip_da)
                    {
                        GetIpeVxlanNvgreIpdaCtl(A, array_2_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &tempip1);
                    }
                    else if (3 == pinfo->ip_da)
                    {
                        GetIpeVxlanNvgreIpdaCtl(A, array_3_ipDa_f, ipe_vxlan_nvgre_ipda_ctl, &tempip1);
                    }
                }
                else
                {
                    tempip1 = sal_ntohl(pinfo->ip_da);
                }

                sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                tempip = sal_ntohl(pinfo->ip_sa);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s%-21s%-12s%-10s\n", buf1, buf,  "-", CTC_DEBUG_HEX_FORMAT(str, format, pinfo->vn_id, 6, U));
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
                pinfo1 = &(pe->key.u0.hash.u0.ol_tnnl_v6);
                ipv6_address1[0] = sal_ntohl(pinfo1->ip_da[0]);
                ipv6_address1[1] = sal_ntohl(pinfo1->ip_da[1]);
                ipv6_address1[2] = sal_ntohl(pinfo1->ip_da[2]);
                ipv6_address1[3] = sal_ntohl(pinfo1->ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_address1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-42s%-12s%-10s\n", buf1, "-", CTC_DEBUG_HEX_FORMAT(str, format, pinfo1->vn_id, 6, U));
                break;

            case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
            case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
            case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
            case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
                pinfo1 = &(pe->key.u0.hash.u0.ol_tnnl_v6);
                ipv6_address1[0] = sal_ntohl(pinfo1->ip_da[0]);
                ipv6_address1[1] = sal_ntohl(pinfo1->ip_da[1]);
                ipv6_address1[2] = sal_ntohl(pinfo1->ip_da[2]);
                ipv6_address1[3] = sal_ntohl(pinfo1->ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_address1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-42s%-12s%-10s\n", buf1, "-", CTC_DEBUG_HEX_FORMAT(str, format, pinfo1->vn_id, 6, U));

                ipv6_address[0] = sal_ntohl(pinfo1->ip_sa[0]);
                ipv6_address[1] = sal_ntohl(pinfo1->ip_sa[1]);
                ipv6_address[2] = sal_ntohl(pinfo1->ip_sa[2]);
                ipv6_address[3] = sal_ntohl(pinfo1->ip_sa[3]);
                sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-30s%-42s\n", " ", buf);
                break;

            case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
                tempip = sal_ntohl(pe->key.u0.ipv4_key.u0.ip.ip_da);
                sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-21s", buf);

                if (CTC_FLAG_ISSET(pe->key.u0.ipv4_key.flag, CTC_SCL_TCAM_IPV4_KEY_FLAG_IP_SA))
                {
                    tempip1 = sal_ntohl(pe->key.u0.ipv4_key.u0.ip.ip_sa);
                    sal_inet_ntop(AF_INET, &tempip1, buf1, CTC_IPV6_ADDR_STR_LEN);
                    SYS_SCL_DBG_DUMP("%-21s", buf1);
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-21s", "-");
                }

                if (CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_GRE_KEY))
                {
                    gre_key =  (pe->key.u0.ipv4_key.u0.ip.l4_src_port << 16);
                    gre_key |= ((pe->key.u0.ipv6_key.u0.ip.l4_dst_port) & 0xFFFF);
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", CTC_DEBUG_HEX_FORMAT(str, format, gre_key, 8, U), "-");
                }
                else if ((CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_NVGRE_KEY)) ||
                            (CTC_FLAG_ISSET(pe->key.u0.ipv4_key.sub_flag, CTC_SCL_TCAM_IPV4_KEY_SUB_FLAG_VNI)))
                {
                    vnid =  (pe->key.u0.ipv4_key.u0.ip.l4_src_port << 16);
                    vnid |= ((pe->key.u0.ipv4_key.u0.ip.l4_dst_port) & 0xFFFF);
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", "-", CTC_DEBUG_HEX_FORMAT(str, format, vnid, 6, U));
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", "-", "-");
                }
                break;

            case SYS_SCL_KEY_TCAM_IPV6:
                ipv6_address1[0] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[0]);
                ipv6_address1[1] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[1]);
                ipv6_address1[2] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[2]);
                ipv6_address1[3] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_da[3]);
                sal_inet_ntop(AF_INET6, ipv6_address1, buf1, CTC_IPV6_ADDR_STR_LEN);
                SYS_SCL_DBG_DUMP("%-42s", buf1);

                if (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_GRE_KEY))
                {
                    gre_key =  (pe->key.u0.ipv6_key.u0.ip.l4_src_port << 16);
                    gre_key |= ((pe->key.u0.ipv6_key.u0.ip.l4_dst_port) & 0xFFFF);
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", CTC_DEBUG_HEX_FORMAT(str, format, gre_key, 8, U), "-");
                }
                else if ((CTC_FLAG_ISSET(pe->key.u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_NVGRE_KEY)) ||
                            (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.sub_flag, CTC_SCL_TCAM_IPV6_KEY_SUB_FLAG_VNI)))
                {
                    vnid =  (pe->key.u0.ipv6_key.u0.ip.l4_src_port << 16);
                    vnid |= ((pe->key.u0.ipv6_key.u0.ip.l4_dst_port) & 0xFFFF);
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", "-", CTC_DEBUG_HEX_FORMAT(str, format, vnid, 6, U));
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", "-", "-");
                }


                if (CTC_FLAG_ISSET(pe->key.u0.ipv6_key.flag, CTC_SCL_TCAM_IPV6_KEY_FLAG_IP_SA))
                {
                    ipv6_address[0] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[0]);
                    ipv6_address[1] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[1]);
                    ipv6_address[2] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[2]);
                    ipv6_address[3] = sal_ntohl(pe->key.u0.ipv6_key.u0.ip.ip_sa[3]);
                    sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
                    SYS_SCL_DBG_DUMP("%-30s%-42s\n", " ", buf);
                }

                break;

            case SYS_SCL_KEY_HASH_TRILL_UC:
            case SYS_SCL_KEY_HASH_TRILL_MC:
            case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
            case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
            case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
                SYS_SCL_DBG_DUMP("%-21s",CTC_DEBUG_HEX_FORMAT(str, format, pe->key.u0.hash.u0.trill.egress_nickname, 4, U));
                SYS_SCL_DBG_DUMP("%-21s",CTC_DEBUG_HEX_FORMAT(str, format, pe->key.u0.hash.u0.trill.ingress_nickname, 4, U));
                if ((SYS_SCL_KEY_HASH_TRILL_MC_RPF == pe->key.type) ||(SYS_SCL_KEY_HASH_TRILL_MC_ADJ == pe->key.type))
                {
                    SYS_SCL_DBG_DUMP("0x%-10.4x%-10s\n",pe->key.u0.hash.u0.trill.gport,"-");
                }
                else
                {
                    SYS_SCL_DBG_DUMP("%-12s%-10s\n", "-", "-");
                }
                break;

            default:    /* never */
                return CTC_E_SCL_INVALID_KEY_TYPE;
        }
    }

    if (detail)
    {

    }

    (*p_cnt)++;

    return CTC_E_NONE;
}



/*
 * show scl entry to a specific entry with specific key type
 */
STATIC int32
_sys_goldengate_scl_show_entry(uint8 lchip, sys_scl_sw_entry_t* pe, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_group_t* pg;
    char*               type_name;
    char str[35];
    char format[10];
    uint8 step = 0;

    CTC_PTR_VALID_CHECK(pe);

    pg = pe->group;
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if ((SYS_SCL_ALL_KEY != key_type)
        && (pe->key.type != key_type))
    {
        return CTC_E_NONE;
    }

    if (detail)
    {
        SYS_SCL_DBG_DUMP("%-6s%-13s%-13s%-7s%-7s%-12s%s\n", "No.", "ENTRY_ID", "GROUP_ID", "HW", "PRI", "KEY_INDEX", "  KEY_TYPE");
        SYS_SCL_DBG_DUMP("-------------------------------------------------------------------------------\n");
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }
    else
    {
        if(1 == *p_cnt)
        {
            SYS_SCL_DBG_DUMP("%-6s%-13s%-13s%-7s%-7s%-12s%s\n", "No.", "ENTRY_ID", "GROUP_ID", "HW", "PRI", "KEY_INDEX", "  KEY_TYPE");
            SYS_SCL_DBG_DUMP("-------------------------------------------------------------------------------\n");
        }
        SYS_SCL_DBG_DUMP("%-6u", *p_cnt);
    }

    type_name = _sys_goldengate_scl_get_type(lchip, pe->key.type);

     /* on 1 chip, use that lchip's prio */
    {
        if (SYS_SCL_SHOW_IN_HEX <= pe->fpae.entry_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pe->fpae.entry_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pe->fpae.entry_id);
        }

        if (SYS_SCL_SHOW_IN_HEX <= pg->group_id)
        {
            SYS_SCL_DBG_DUMP("%-13s", CTC_DEBUG_HEX_FORMAT(str, format, pg->group_id, 8, U));
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-13u", pg->group_id);
        }


        if((SCL_ENTRY_IS_TCAM(pe->key.type)) || (SYS_SCL_IS_DEFAULT(pe->key.type)))
        {
            step = SYS_SCL_MAP_FPA_SIZE_TO_STEP(p_gg_scl_master[lchip]->alloc_id[pe->key.type]);
            SYS_SCL_DBG_DUMP("%-7s%-7u%-12u%s\n",
                            (pe->fpae.flag) ? "yes" : "no ", pe->fpae.priority, pe->fpae.offset_a/step, type_name);
        }
        else
        {
            SYS_SCL_DBG_DUMP("%-7s%-7u0x%-12X%s\n",
                        (pe->fpae.flag) ? "yes" : "no ", pe->fpae.priority, pe->fpae.offset_a, type_name);
        }
    }

    if (detail)
    {
        _sys_goldengate_scl_show_entry_detail(lchip, pe);
    }

    (*p_cnt)++;

    return CTC_E_NONE;
}

/*
 * show scl entriy by entry id
 */
STATIC int32
_sys_goldengate_scl_show_entry_by_entry_id(uint8 lchip, uint32 eid, sys_scl_key_type_t key_type, uint8 detail)
{
    sys_scl_sw_entry_t* pe = NULL;

    uint16            count = 1;

    SYS_SCL_LOCK(lchip);
    _sys_goldengate_scl_get_nodes_by_eid(lchip, eid, &pe, NULL, NULL);
    if (!pe)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    /* SCL_PRINT_ENTRY_ROW(eid); */

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_entry(lchip, pe, &count, key_type, detail));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show scl entries in a group with specific key type
 */
STATIC int32
_sys_goldengate_scl_show_entry_in_group(uint8 lchip, sys_scl_sw_group_t* pg, uint16* p_cnt, sys_scl_key_type_t key_type, uint8 detail)
{
    struct ctc_slistnode_s* pe;

    CTC_PTR_VALID_CHECK(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        _sys_goldengate_scl_show_entry(lchip, (sys_scl_sw_entry_t *) pe, p_cnt, key_type, detail);
    }

    return CTC_E_NONE;
}

/*
 * show scl entries by group id with specific key type
 */
STATIC int32
_sys_goldengate_scl_show_entry_by_group_id(uint8 lchip, uint32 gid, sys_scl_key_type_t key_type, uint8 detail)
{
    uint16            count = 1;
    sys_scl_sw_group_t* pg  = NULL;

    SYS_SCL_LOCK(lchip);
    _sys_goldengate_scl_get_group_by_gid(lchip, gid, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    SCL_PRINT_GROUP_ROW(gid);

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_entry_in_group(lchip, pg, &count, key_type, detail));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * show scl entries in tcam sort by index
 */
STATIC int32
_sys_goldengate_scl_show_tcam_entries(uint8 lchip, uint8 prio, sys_scl_key_type_t key_type, uint8 detail)
{
    uint16            count = 1;
    uint8             step = 0;
    uint16         block_idx;
    sys_scl_sw_block_t* pb;
    ctc_fpa_entry_t* pe;

    SYS_SCL_CHECK_GROUP_PRIO(prio);

    count = 1;
    SYS_SCL_LOCK(lchip);
    pb = &p_gg_scl_master[lchip]->block[prio];
    if (pb->fpab.free_count != pb->fpab.entry_count) /*not empty*/
    {
        step = 1;
        for (block_idx = 0; block_idx < pb->fpab.entry_count; block_idx = block_idx + step)
        {
            pe = pb->fpab.entries[block_idx];

            if(block_idx >= pb->fpab.start_offset[2] && (pb->fpab.sub_entry_count[2]))   /*640 bit key*/
            {
                step = 4;
            }
            else if(block_idx >= pb->fpab.start_offset[1] && (pb->fpab.sub_entry_count[1]))   /*320 bit key*/
            {
                step = 2;
            }
            else                                            /*160 bit key*/
            {
                step = 1;
            }
            pe = pb->fpab.entries[block_idx];

            if (pe)
            {
                CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_entry(lchip, SCL_OUTER_ENTRY(pe), &count, key_type, detail));
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}



STATIC int32
_sys_goldengate_scl_show_entry_all(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail)
{
    uint8              idx;
    _scl_cb_para_t     para;
    struct ctc_listnode* node;
    sys_scl_sw_group_t * pg;

    sal_memset(&para, 0, sizeof(_scl_cb_para_t));
    para.detail   = detail;
    para.key_type = key_type;
    para.count = 1;

    SYS_SCL_LOCK(lchip);
    for (idx = 0; idx < 3; idx++)
    {
        CTC_LIST_LOOP(p_gg_scl_master[lchip]->group_list[idx], pg, node)
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_entry_in_group
                                 (lchip, pg, &(para.count), para.key_type, para.detail));
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_goldengate_scl_show_entry(uint8 lchip, uint8 type, uint32 param, sys_scl_key_type_t key_type, uint8 detail)
{
    SYS_SCL_INIT_CHECK(lchip);
    /* SYS_SCL_KEY_NUM represents all type*/
    CTC_MAX_VALUE_CHECK(type, SYS_SCL_KEY_NUM);

    switch (type)
    {
    case 0:
        CTC_ERROR_RETURN(_sys_goldengate_scl_show_entry_all(lchip, key_type, 0));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 1:
        CTC_ERROR_RETURN(_sys_goldengate_scl_show_entry_by_entry_id(lchip, param, key_type, detail));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 2:
        CTC_ERROR_RETURN(_sys_goldengate_scl_show_entry_by_group_id(lchip, param, key_type, 0));
        SYS_SCL_DBG_DUMP("\n");
        break;

    case 3:
        CTC_ERROR_RETURN(_sys_goldengate_scl_show_tcam_entries(lchip, param, key_type, 0));
        SYS_SCL_DBG_DUMP("\n");
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_scl_show_vlan_mapping_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail)
{
    _scl_cb_para_t     para;
    struct ctc_listnode* node;
    struct ctc_slistnode_s* pe;
    sys_scl_sw_group_t * pg  = NULL;

    SYS_SCL_INIT_CHECK(lchip);

    sal_memset(&para, 0, sizeof(_scl_cb_para_t));
    para.detail   = detail;
    para.key_type = key_type;
    para.count = 1;

    SYS_SCL_LOCK(lchip);
    /* find in inner group */
    CTC_LIST_LOOP(p_gg_scl_master[lchip]->group_list[2], pg, node)
    {
        if ((SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS == pg->group_id) ||
            (SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS == pg->group_id) ||
            (SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE <= pg->group_id))
        {
            CTC_SLIST_LOOP(pg->entry_list, pe)
            {
                CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_vlan_mapping_entry(lchip, (sys_scl_sw_entry_t *) pe, &(para.count), para.key_type, para.detail));
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    if (para.count == 1)
    {
        SYS_SCL_DBG_DUMP("  Total entry number : 0\n");
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_scl_show_vlan_class_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail)
{
    struct ctc_slistnode_s* pe;
    sys_scl_sw_group_t* pg  = NULL;
    uint32  gid[3] = {0};
    uint8    index = 0;
    uint16  count = 1;

    SYS_SCL_INIT_CHECK(lchip);

    if ((SYS_SCL_KEY_TCAM_MAC == key_type) || (SYS_SCL_KEY_HASH_MAC == key_type))
    {
        gid[0] = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC;
        gid[1] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC;
        gid[2] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC + 1;
    }
    else if ((SYS_SCL_KEY_HASH_IPV4 == key_type) || (SYS_SCL_KEY_TCAM_IPV4_SINGLE == key_type))
    {
        gid[0] = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4;
        gid[1] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4;
        gid[2] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4 + 1;
    }
    else if ((SYS_SCL_KEY_HASH_IPV6 == key_type) || (SYS_SCL_KEY_TCAM_IPV6 == key_type))
    {
        gid[0] = SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6;
        gid[1] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6;
        gid[2] = SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6 + 1;
    }
    else
    {
         return CTC_E_SCL_INVALID_KEY_TYPE;
    }

    SYS_SCL_LOCK(lchip);
    for (index = 0; index < 3; index++)
    {
        _sys_goldengate_scl_get_group_by_gid(lchip, gid[index], &pg);
        SYS_SCL_CHECK_GROUP_UNEXIST(pg);

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            if ((SYS_SCL_KEY_TCAM_MAC == key_type) || (SYS_SCL_KEY_HASH_MAC == key_type))
            {
                CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_vlan_class_mac_entry(lchip, (sys_scl_sw_entry_t *) pe, &count, key_type, detail));
            }
            else if ((SYS_SCL_KEY_HASH_IPV4 == key_type) || (SYS_SCL_KEY_TCAM_IPV4_SINGLE == key_type))
            {
                CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_vlan_class_ipv4_entry(lchip, (sys_scl_sw_entry_t *) pe, &count, key_type, detail));
            }
            else if ((SYS_SCL_KEY_HASH_IPV6 == key_type) || (SYS_SCL_KEY_TCAM_IPV6 == key_type))
            {
                CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_vlan_class_ipv6_entry(lchip, (sys_scl_sw_entry_t *) pe, &count, key_type, detail));
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    if (count == 1)
    {
        SYS_SCL_DBG_DUMP("  Total entry number : 0\n");
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_scl_show_tunnel_entry(uint8 lchip, sys_scl_key_type_t key_type, uint8 detail)
{
    struct ctc_slistnode_s* pe;
    sys_scl_sw_group_t* pg  = NULL;
    uint32  gid[7] = {0};
    uint8    index = 0;
    uint16  count = 1;
    SYS_SCL_INIT_CHECK(lchip);

    gid[0] = SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL;
    gid[1] = SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL;
    gid[2] = SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL + 1;
    gid[3] = SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL;
    gid[4] = SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL;
    gid[5] = SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + 1;
    gid[6] = SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL;

    SYS_SCL_LOCK(lchip);
    for (index = 0; index < 7; index++)
    {
        _sys_goldengate_scl_get_group_by_gid(lchip, gid[index], &pg);
        SYS_SCL_CHECK_GROUP_UNEXIST(pg);

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_show_tunnel_entry(lchip, (sys_scl_sw_entry_t *) pe, &count, key_type, detail));
        }
    }
    SYS_SCL_UNLOCK(lchip);

    if (count == 1)
    {
        SYS_SCL_DBG_DUMP("  Total entry number : 0\n");
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_show_priority_status(uint8 lchip)
{
    uint8 vlan_mac;
    uint8 ipv4;
    uint8 ipv6;

    uint8 idx;

    vlan_mac = p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_VLAN];
    ipv4     = p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV4];
    ipv6     = p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV6];

    SYS_SCL_DBG_DUMP("Tcam Status (lchip %d):\n", lchip);
    if (p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en)
    {
        SYS_SCL_DBG_DUMP("SCL_ID      Vlan|Ipv4-S   Mac|Ipv4   Ipv6 \n");
    }
    else
    {
        SYS_SCL_DBG_DUMP("SCL_ID      Vlan|Mac|Ipv4-S   Ipv4   Ipv6 \n");
    }

    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");

    SYS_SCL_LOCK(lchip);
    for (idx = 0; idx < SCL_BLOCK_ID_NUM; idx++)
    {
        SYS_SCL_DBG_DUMP("SCL%d         %d/%d            %d/%d   %d/%d  \n",
                         idx,
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[vlan_mac] - p_gg_scl_master[lchip]->block[idx].fpab.sub_free_count[vlan_mac],
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[vlan_mac],
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[ipv4] - p_gg_scl_master[lchip]->block[idx].fpab.sub_free_count[ipv4],
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[ipv4],
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[ipv6] - p_gg_scl_master[lchip]->block[idx].fpab.sub_free_count[ipv6],
                         p_gg_scl_master[lchip]->block[idx].fpab.sub_entry_count[ipv6]);
    }
    SYS_SCL_UNLOCK(lchip);

    SYS_SCL_DBG_DUMP("\n\n");
    return CTC_E_NONE;
}


typedef struct
{
    uint32            group_id;
    char              * name;
    sys_scl_sw_group_t* pg;
    uint32            count;
} sys_scl_rsv_group_info_t;

STATIC int32
_sys_goldengate_scl_show_entry_status(uint8 lchip)
{
    uint8                    group_num1 = 0;
    uint8                    group_num2 = 0;
    uint8                    idx        = 0;
    uint32                   count      = 0;
    char                     str[35] = {0};
    char                     format[10] = {0};

    sys_scl_sw_group_t       * pg;
    struct ctc_listnode      * node;

    sys_scl_rsv_group_info_t rsv_group1[] =
    {
        { CTC_SCL_GROUP_ID_HASH_PORT,           "outer_hash_port          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_CVLAN,     "outer_hash_port_cvlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_SVLAN,     "outer_hash_port_svlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_2VLAN,     "outer_hash_port_2vlan    ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_CVLAN_COS, "outer_hash_port_cvlan_cos", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_SVLAN_COS, "outer_hash_port_svlan_cos", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_MAC,            "outer_hash_mac           ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_MAC,       "outer_hash_port_mac      ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_IPV4,           "outer_hash_ipv4          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_IPV4,      "outer_hash_port_ipv4     ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_IPV6,           "outer_hash_ipv6          ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_PORT_IPV6,      "outer_hash_port_ipv6     ", NULL, 0 },
        { CTC_SCL_GROUP_ID_HASH_L2,             "outer_hash_l2            ", NULL, 0 },
    };

    sys_scl_rsv_group_info_t rsv_group2[] =
    {
        { SYS_SCL_GROUP_ID_INNER_HASH_IP_TUNNEL,           "inner_hash_ip_tunnel        ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_IP_SRC_GUARD,        "inner_hash_ip_src_guard     ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS,    "inner_hash_vlan_mapping_igs ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS,    "inner_hash_vlan_mapping_egs ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS,    "inner_deft_vlan_mapping_igs ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS,    "inner_deft_vlan_mapping_egs ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_MAC,      "inner_hash_vlan_class_mac   ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV4,     "inner_hash_vlan_class_ipv4  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_VLAN_CLASS_IPV6,     "inner_hash_vlan_class_ipv6  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_OVERLAY_TUNNEL,     "inner_hash_overlay_tunnel  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_HASH_TRILL_TUNNEL,     "inner_trill_tunnel  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS,          "inner_deft_vlan_class       ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS + 1,      "inner_deft_vlan_class_1     ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD,        "inner_deft_ip_src_guard     ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + 1,    "inner_deft_ip_src_guard_1   ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL,           "inner_tcam_ip_tunnel        ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL + 1,       "inner_tcam_ip_tunnel_1      ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC,      "inner_tcam_vlan_class_mac   ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC + 1,  "inner_tcam_vlan_class_mac_1 ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4,     "inner_tcam_vlan_class_ipv4  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4 + 1, "inner_tcam_vlan_class_ipv4_1", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6,     "inner_tcam_vlan_class_ipv6  ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6 + 1, "inner_tcam_vlan_class_ipv6_1", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL,           "inner_tcam_overlay_tunnel     ", NULL, 0 },
        { SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + 1,       "inner_tcam_overlay_tunnel_1   ", NULL, 0 },
    };

    group_num1 = sizeof(rsv_group1) / sizeof(sys_scl_rsv_group_info_t);
    group_num2 = sizeof(rsv_group2) / sizeof(sys_scl_rsv_group_info_t);


    SYS_SCL_DBG_DUMP("Entry Status :\n");
    SYS_SCL_DBG_DUMP("%-7s%-17s%s\n", "No.", "GROUP_ID", "ENTRY_COUNT");
    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    count = 0;
    SYS_SCL_LOCK(lchip);
    CTC_LIST_LOOP(p_gg_scl_master[lchip]->group_list[0], pg, node)
    {
        count++;
        SYS_SCL_DBG_DUMP("%-7u%-17s%u\n", count, CTC_DEBUG_HEX_FORMAT(str, format, pg->group_id, 8, U),
                         pg->entry_count);
    }
    SYS_SCL_DBG_DUMP("Outer tcam group count :%-2u\n\n\n", count);

    SYS_SCL_DBG_DUMP("%-7s%-17s%-30s%s\n", "No.", "GROUP_ID", "NAME", "ENTRY_COUNT");
    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    count = 0;

    for (idx = 0; idx < group_num1; idx++)
    {
        count++;
        _sys_goldengate_scl_get_group_by_gid(lchip, rsv_group1[idx].group_id, &rsv_group1[idx].pg);
        SYS_SCL_DBG_DUMP("%-7u%-17s%-30s%u\n",
                         count,
                         CTC_DEBUG_HEX_FORMAT(str, format, rsv_group1[idx].group_id, 8, U),
                         rsv_group1[idx].name, rsv_group1[idx].pg->entry_count);
    }
    SYS_SCL_DBG_DUMP("Outer hash group count :%u \n\n\n", count);

    SYS_SCL_DBG_DUMP("%-7s%-17s%-30s%s\n", "No.", "GROUP_ID", "NAME", "ENTRY_COUNT");
    SYS_SCL_DBG_DUMP("----------------------------------------------------------------------------\n");
    count = 0;
    for (idx = 0; idx < group_num2; idx++)
    {
        count++;
        _sys_goldengate_scl_get_group_by_gid(lchip, rsv_group2[idx].group_id, &rsv_group2[idx].pg);
        SYS_SCL_DBG_DUMP("%-7u%-17s%-30s%u\n",
                         count,
                         CTC_DEBUG_HEX_FORMAT(str, format, rsv_group2[idx].group_id, 8, U),
                         rsv_group2[idx].name, rsv_group2[idx].pg->entry_count);
    }
    SYS_SCL_UNLOCK(lchip);
    SYS_SCL_DBG_DUMP("Inner group count :%u \n", count);

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_show_status(uint8 lchip)
{

    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_DUMP("================= SCL Overall Status ==================\n");


    SYS_SCL_DBG_DUMP("\n");

    _sys_goldengate_scl_show_priority_status(lchip);

    _sys_goldengate_scl_show_entry_status(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_show_tcam_alloc_status(uint8 lchip, uint8 fpa_block_id)
{
    char* key_str[3] = {"160 bit", "320 bit", "640 bit"};
    sys_scl_sw_block_t *pb = NULL;
    uint8 idx = 0;
    uint8 step = 1;

    SYS_SCL_INIT_CHECK(lchip);

    CTC_MAX_VALUE_CHECK(fpa_block_id, SCL_BLOCK_ID_NUM - 1);

    SYS_SCL_LOCK(lchip);
    pb = &(p_gg_scl_master[lchip]->block[fpa_block_id]);

    SYS_SCL_DBG_DUMP("\nblock id: %d, dir: %s\n", fpa_block_id, "Ingress");
    SYS_SCL_DBG_DUMP("(unit 160bit)  entry count: %d, used count: %d", pb->fpab.entry_count, pb->fpab.entry_count - pb->fpab.free_count);
    SYS_SCL_DBG_DUMP("\n------------------------------------------------------------------\n");
    SYS_SCL_DBG_DUMP("%-12s%-15s%-15s%-15s%-15s\n", "key size", "range", "entry count", "used count", "rsv count");

    for(idx = 0; idx < CTC_FPA_KEY_SIZE_NUM; idx++)
    {
        if(pb->fpab.sub_entry_count[idx] > 0)
        {
            SYS_SCL_DBG_DUMP("%-12s[%4d,%4d]    %-15u%-15u%-15u\n", key_str[idx], pb->fpab.start_offset[idx],
                                            pb->fpab.start_offset[idx] + pb->fpab.sub_entry_count[idx] * step - 1,
                                            pb->fpab.sub_entry_count[idx], pb->fpab.sub_entry_count[idx] - pb->fpab.sub_free_count[idx],
                                            pb->fpab.sub_rsv_count[idx]);
        }
        step = step * 2;
    }
    SYS_SCL_UNLOCK(lchip);

    SYS_SCL_DBG_DUMP("------------------------------------------------------------------\n");

    return CTC_E_NONE;
}


#define ___________SCL_OUTER_FUNCTION________________________

#define _1_SCL_GROUP_

/*
 * create an scl group.
 * For init api, group_info can be NULL.
 * For out using, group_info must not be NULL.
 *
 */
int32
sys_goldengate_scl_create_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_t* pg_new = NULL;
    sys_scl_sw_group_t* pg     = NULL;
    int32             ret;

    SYS_SCL_INIT_CHECK(lchip);
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(group_id);
    }
    /* inner group self assured. */

    CTC_PTR_VALID_CHECK(group_info);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     *  group_id is uint32.
     *  #1 check block_num from p_gg_scl_master. precedence cannot bigger than block_num.
     *  #2 malloc a sys_scl_sw_group_t, add to hash based on group_id.
     */

    SYS_SCL_LOCK(lchip);
    /* check if group exist */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    if (pg)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_GROUP_EXIST;
    }
    SYS_SCL_UNLOCK(lchip);

    /* malloc an empty group */

    MALLOC_ZERO(MEM_SCL_MODULE, pg_new, sizeof(sys_scl_sw_group_t));
    if (!pg_new)
    {
        return CTC_E_NO_MEMORY;
    }


    if (SYS_SCL_IS_TCAM_GROUP(group_id)) /* without inner hash group */
    {
        if (!inner)
        {
            CTC_ERROR_GOTO(_sys_goldengate_scl_check_group_info(lchip, group_info, group_info->type, 1), ret, cleanup);
        }
    }

    CTC_ERROR_GOTO(_sys_goldengate_scl_map_group_info(lchip, &pg_new->group_info, group_info, 1), ret, cleanup);

    pg_new->group_id   = group_id;
    pg_new->entry_list = ctc_slist_new();
    if (!pg_new->entry_list)
    {
        mem_free(pg_new);
        return CTC_E_INVALID_PTR;
    }

    SYS_SCL_LOCK(lchip);
    /* add to hash */
    if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->group, pg_new))
    {
        ret = CTC_E_SCL_HASH_INSERT_FAILED;
        SYS_SCL_UNLOCK(lchip);
        goto cleanup;
    }

    if (!inner) /* outer tcam */
    {
        ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[0], pg_new);
    }
    else if (group_id <= CTC_SCL_GROUP_ID_HASH_MAX) /* outer hash */
    {
        ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[1], pg_new);
    }
    else /* inner */
    {
        ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[2], pg_new);
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
 cleanup:
    mem_free(pg_new);
    return ret;
}

/*
 * destroy an empty group which all entry removed.
 */
int32
sys_goldengate_scl_destroy_group(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t* pg = NULL;

    SYS_SCL_INIT_CHECK(lchip);
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_NORMAL_GROUP_ID(group_id);
    }
    /* inner user won't destroy group */

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: group_id: %u \n", group_id);

    /*
     * #1 check if entry all removed.
     * #2 remove from hash. free sys_scl_sw_group_t.
     */

    SYS_SCL_LOCK(lchip);
    /* check if group exist */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* check if all entry removed */
    if (0 != pg->entry_list->count)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_GROUP_NOT_EMPTY;
    }

    if(NULL == ctc_hash_remove(p_gg_scl_master[lchip]->group, pg))
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_UNEXPECT;
    }

    if (!inner) /* outer tcam */
    {
        ctc_listnode_delete(p_gg_scl_master[lchip]->group_list[0], pg);
    }
    else if (group_id <= CTC_SCL_GROUP_ID_HASH_MAX) /* outer hash */
    {
        ctc_listnode_delete(p_gg_scl_master[lchip]->group_list[1], pg);
    }
    else /* inner */
    {
        ctc_listnode_delete(p_gg_scl_master[lchip]->group_list[2], pg);
    }
    SYS_SCL_UNLOCK(lchip);

    /* free slist */
    ctc_slist_free(pg->entry_list);

    /* free pg */
    mem_free(pg);

    return CTC_E_NONE;
}

/*
 * install a group (empty or NOT) to hardware table
 */
int32
sys_goldengate_scl_install_group(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_t    * pg        = NULL;
    uint8                 install_all = 0;
    struct ctc_slistnode_s* pe;

    uint32                eid = 0;

    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    if (group_info) /* group_info != NULL*/
    {
        bool is_new;

        /* if group_info is new, rewrite all entries ,may change group class id*/
        is_new = _sys_goldengate_scl_is_group_info_new(lchip, &pg->group_info, group_info);
        if (is_new)
        {
            install_all = 1;
        }
        else
        {
            install_all = 0;
        }
    }
    else    /*group info is NULL */
    {
        install_all = 0;
    }


    if (install_all) /* if install_all, group_info is not NULL */
    {                /* traverse all install */
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_check_group_info(lchip, group_info, pg->group_info.type, 0));

        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_map_group_info(lchip, &pg->group_info, group_info, 0));

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
            _sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 0);
        }
    }
    else
    {   /* traverse, stop at first installed entry.*/
        if (pg->entry_list)
        {
            for (pe = pg->entry_list->head;
                 pe && (((sys_scl_sw_entry_t *) pe)->fpae.flag != FPA_ENTRY_FLAG_INSTALLED);
                 pe = pe->next)
            {
                eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
                _sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 0);
            }
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * uninstall a group (empty or NOT) from hardware table
 */
int32
sys_goldengate_scl_uninstall_group(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t    * pg = NULL;
    struct ctc_slistnode_s* pe = NULL;

    uint32                eid = 0;

    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_DELETE, 0));
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get group info by group id. NOT For hash group.
 */
int32
sys_goldengate_scl_get_group_info(uint8 lchip, uint32 group_id, ctc_scl_group_info_t* group_info, uint8 inner)
{
    sys_scl_sw_group_info_t* pinfo = NULL;  /* sys group info */
    sys_scl_sw_group_t     * pg    = NULL;

    SYS_SCL_INIT_CHECK(lchip);

    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }

    CTC_PTR_VALID_CHECK(group_info);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid: %u \n", group_id);

    SYS_SCL_LOCK(lchip);
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    pinfo = &(pg->group_info);

    /* get ctc group info based on pinfo (sys group info) */
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_unmap_group_info(lchip, group_info, pinfo));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define _2_SCL_ENTRY_

/*
 * install entry to hardware table
 */
int32
sys_goldengate_scl_install_entry(uint8 lchip, uint32 eid, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(eid);
    }

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_ADD, 1));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


/*
 * uninstall entry from hardware table
 */
int32
sys_goldengate_scl_uninstall_entry(uint8 lchip, uint32 eid, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", eid);

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(eid);
    }

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_DELETE, 1));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


/* get entry id by key */
int32
sys_goldengate_scl_get_entry_id(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid)
{
    sys_scl_sw_entry_t e_tmp;
    sys_scl_sw_entry_t * pe_lkup;
    sys_scl_sw_group_t * pg = NULL;

    CTC_PTR_VALID_CHECK(scl_entry);

    sal_memset(&e_tmp, 0, sizeof(e_tmp));

    CTC_ERROR_RETURN(_sys_goldengate_scl_map_key(lchip, &scl_entry->key, &e_tmp.key));
    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_get_group_by_gid(lchip, gid, &pg));
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* must set group */
    e_tmp.group = pg;
    e_tmp.lchip = lchip;
    _sys_goldengate_scl_get_nodes_by_key(lchip, &e_tmp, &pe_lkup, NULL);
    if (!pe_lkup) /* hash's key must be unique */
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }
    scl_entry->entry_id = pe_lkup->fpae.entry_id;
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_get_entry(uint8 lchip, sys_scl_entry_t* scl_entry, uint32 gid)
{
    CTC_PTR_VALID_CHECK(scl_entry);

    CTC_ERROR_RETURN(sys_goldengate_scl_get_entry_id(lchip, scl_entry, gid));
    CTC_ERROR_RETURN(sys_goldengate_scl_get_action(lchip, scl_entry->entry_id, &(scl_entry->action), 1));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_check_action_type(uint8 lchip, sys_scl_sw_key_t* key, uint32 action_type)
{
    uint8 match     = 0;
    uint8 dir_valid = 0;
    uint8 dir       = 0;
    switch (key->type)
    {
    case SYS_SCL_KEY_TCAM_IPV4_SINGLE:
    case SYS_SCL_KEY_TCAM_IPV6:
        match = (SYS_SCL_ACTION_INGRESS == action_type) || (SYS_SCL_ACTION_TUNNEL == action_type) || (action_type == SYS_SCL_ACTION_FLOW);
        break;
    case SYS_SCL_KEY_TCAM_IPV4:
    case SYS_SCL_KEY_TCAM_VLAN:
    case SYS_SCL_KEY_TCAM_MAC:
    case SYS_SCL_KEY_HASH_MAC:
    case SYS_SCL_KEY_HASH_PORT_MAC:
    case SYS_SCL_KEY_HASH_IPV4:
    case SYS_SCL_KEY_HASH_PORT_IPV4:
    case SYS_SCL_KEY_HASH_IPV6:
    case SYS_SCL_KEY_HASH_PORT_IPV6:
    case SYS_SCL_KEY_PORT_DEFAULT_INGRESS:
        match = (SYS_SCL_ACTION_INGRESS == action_type) || (action_type == SYS_SCL_ACTION_FLOW);
        break;
    case SYS_SCL_KEY_HASH_PORT:
        dir       = key->u0.hash.u0.port.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        dir       = key->u0.hash.u0.vlan.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        dir       = key->u0.hash.u0.vtag.dir;
        dir_valid = 1;
        break;
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
    case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
    case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
    case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
    case SYS_SCL_KEY_HASH_TRILL_UC:
    case SYS_SCL_KEY_HASH_TRILL_MC:
    case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
    case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
    case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
        match = (SYS_SCL_ACTION_TUNNEL == action_type);
        break;
    case SYS_SCL_KEY_PORT_DEFAULT_EGRESS:
        match = (SYS_SCL_ACTION_EGRESS == action_type);
        break;
    case SYS_SCL_KEY_HASH_L2:
        match = (SYS_SCL_ACTION_FLOW == action_type);
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (dir_valid)
    {
        CTC_MAX_VALUE_CHECK(dir, CTC_BOTH_DIRECTION - 1);
        if (dir == CTC_INGRESS)
        {
            match = ((action_type == SYS_SCL_ACTION_INGRESS) ||
                     (action_type == SYS_SCL_ACTION_FLOW));
        }
        else if (dir == CTC_EGRESS)
        {
            match = (action_type == SYS_SCL_ACTION_EGRESS);
        }
    }

    if (!match)
    {
        return CTC_E_SCL_KEY_ACTION_TYPE_NOT_MATCH;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_scl_add_entry(uint8 lchip, uint32 group_id, sys_scl_entry_t* scl_entry, uint8 inner)
{
    sys_scl_sw_group_t     * pg      = NULL;
    sys_scl_sw_entry_t     * pe_lkup = NULL;

    sys_scl_sw_entry_t     e_tmp;
    sys_scl_sw_action_t    a_tmp;
    sys_scl_sw_vlan_edit_t v_tmp;
    int32                  ret = 0;


    SYS_SCL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(scl_entry);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u -- key_type %u\n", scl_entry->entry_id, scl_entry->key.type);

    SYS_SCL_LOCK(lchip);
    if (inner) /* get inner entry id from opf */
    {
        sys_goldengate_opf_t opf;
        uint32               entry_id = 0;

        sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
        opf.pool_index = 0; /* care not chip */
        opf.pool_type  = OPF_SCL_ENTRY_ID;

        CTC_ERROR_RETURN_SCL_UNLOCK(sys_goldengate_opf_alloc_offset(lchip, &opf, 1, &entry_id));
        scl_entry->entry_id = entry_id + SYS_SCL_MIN_INNER_ENTRY_ID;
    }
    else
    {
        if (group_id > CTC_SCL_GROUP_ID_HASH_MAX)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_GROUP_ID;
        }
        if (scl_entry->entry_id > CTC_SCL_MAX_USER_ENTRY_ID)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_INVALID_PARAM;
        }
        _sys_goldengate_scl_get_nodes_by_eid(lchip, scl_entry->entry_id, &pe_lkup, NULL, NULL);
        if (pe_lkup)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_ENTRY_EXIST;
        }
    }

    /* check sys group unexist */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* check hash entry only in hash group, tcam entry only in tcam group */
    if (!SCL_ENTRY_IS_TCAM(scl_entry->key.type)) /* hash entry */
    {
        if (SYS_SCL_IS_TCAM_GROUP(pg->group_id))
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ADD_HASH_ENTRY_TO_WRONG_GROUP;
        }
    }
    else /* tcam entry */
    {
        if (!SYS_SCL_IS_TCAM_GROUP(pg->group_id))
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ADD_TCAM_ENTRY_TO_WRONG_GROUP;
        }
    }
    SYS_SCL_UNLOCK(lchip);

    sal_memset(&e_tmp, 0, sizeof(e_tmp));
    sal_memset(&a_tmp, 0, sizeof(a_tmp));
    sal_memset(&v_tmp, 0, sizeof(v_tmp));
    e_tmp.lchip = lchip;
    a_tmp.lchip = lchip;
    v_tmp.lchip = lchip;
    /* check out hash key type <--> group id match*/
    if (inner == 0)
    {
        uint8 match = 1;
        switch (scl_entry->key.type)
        {
        case SYS_SCL_KEY_HASH_IPV4:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_IPV4);
            break;
        case SYS_SCL_KEY_HASH_IPV6:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_IPV6);
            break;
        case SYS_SCL_KEY_HASH_MAC:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_MAC);
            break;
        case SYS_SCL_KEY_HASH_PORT:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT);
            break;
        case SYS_SCL_KEY_HASH_PORT_2VLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_2VLAN);
            break;
        case SYS_SCL_KEY_HASH_PORT_CVLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_CVLAN);
            break;
        case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_CVLAN_COS);
            break;
        case SYS_SCL_KEY_HASH_PORT_IPV4:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_IPV4);
            break;
        case SYS_SCL_KEY_HASH_PORT_IPV6:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_IPV6);
            break;
        case SYS_SCL_KEY_HASH_PORT_MAC:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_MAC);
            break;
        case SYS_SCL_KEY_HASH_PORT_SVLAN:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_SVLAN);
            break;
        case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_PORT_SVLAN_COS);
            break;
        case SYS_SCL_KEY_HASH_L2:
            match = (group_id == CTC_SCL_GROUP_ID_HASH_L2);
            break;
        default:
            break;
        }

        if (!match)
        {
            return CTC_E_INVALID_PARAM;
        }
    }


    /* build sys sw entry key and action*/
    CTC_ERROR_RETURN(_sys_goldengate_scl_map_key(lchip, &scl_entry->key, &e_tmp.key));

    /* get lchip lchip_num */
    e_tmp.group = pg; /* get chip need this */

    a_tmp.type = scl_entry->action.type;
    a_tmp.action_flag = scl_entry->action.action_flag;

    /* check key type and action type match */
    CTC_ERROR_RETURN(_sys_goldengate_scl_check_action_type(lchip, &e_tmp.key, scl_entry->action.type));

    {
        if (SYS_SCL_ACTION_INGRESS == a_tmp.type)
        {
            a_tmp.u0.ia.stats_id = scl_entry->action.u.igs_action.stats_id;
        }
        else if(SYS_SCL_ACTION_EGRESS == a_tmp.type)
        {
            a_tmp.u0.ea.stats_id = scl_entry->action.u.egs_action.stats_id;
        }
        else if(SYS_SCL_ACTION_FLOW == a_tmp.type)
        {
            a_tmp.u0.fa.stats_id = scl_entry->action.u.flow_action.stats_id;
        }
    }

    CTC_ERROR_RETURN(_sys_goldengate_scl_map_action(lchip, &scl_entry->action, &a_tmp, &v_tmp));

    /* build sys sw entry inner field */
    e_tmp.fpae.entry_id = scl_entry->entry_id;
    e_tmp.head.next     = NULL;
    if (scl_entry->priority_valid)
    {
        e_tmp.fpae.priority = scl_entry->priority; /* meaningless for HASH */
    }
    else
    {
        e_tmp.fpae.priority = FPA_PRIORITY_DEFAULT; /* meaningless for HASH */
    }

    SYS_SCL_LOCK(lchip);
    /* assure inner entry (but not inner tcam defaul group which key is null), and outer hash entry is unique. out tcam have priority can be same*/
    if ((SCL_INNER_ENTRY_ID(scl_entry->entry_id) && !SCL_TCAM_DEFAUT_GROUP_ID(group_id))
        || !SCL_ENTRY_IS_TCAM(scl_entry->key.type))
    {

        _sys_goldengate_scl_get_nodes_by_key(lchip, &e_tmp, &pe_lkup, NULL);
        if (pe_lkup) /* must be unique */
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_ENTRY_EXIST;
        }
    }

    CTC_ERROR_GOTO(_sys_goldengate_scl_add_entry(lchip, group_id, &e_tmp, &a_tmp, &v_tmp), ret, cleanup);
    SYS_SCL_UNLOCK(lchip);
    return CTC_E_NONE;

 cleanup:
    _sys_goldengate_scl_remove_accessory_property(lchip, &e_tmp, &a_tmp);  /* clear statsptr */
    SYS_SCL_UNLOCK(lchip);
    return ret;
}


/*
 * remove entry from software table
 */
int32
sys_goldengate_scl_remove_entry(uint8 lchip, uint32 entry_id, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip);

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u \n", entry_id);

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_remove_entry(lchip, entry_id, inner));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * remove all entries from a group
 */
int32
sys_goldengate_scl_remove_all_entry(uint8 lchip, uint32 group_id, uint8 inner)
{
    sys_scl_sw_group_t    * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    uint32                eid       = 0;

    SYS_SCL_INIT_CHECK(lchip);
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(group_id);
    }


    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u \n", group_id);

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_goldengate_scl_get_group_by_gid(lchip, group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    /* check if all uninstalled */
    CTC_SLIST_LOOP(pg->entry_list, pe)
    {
        if (FPA_ENTRY_FLAG_INSTALLED ==
            ((sys_scl_sw_entry_t *) pe)->fpae.flag)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_SCL_ENTRY_INSTALLED;
        }
    }

    CTC_SLIST_LOOP_DEL(pg->entry_list, pe, pe_next)
    {
        eid = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
        /* no stop to keep consitent */
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_remove_entry(lchip, eid, inner));
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
_sys_goldengate_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner)
{
    uint8 fpa_size = 0;
    sys_scl_sw_entry_t* pe = NULL;
    sys_scl_sw_group_t* pg = NULL;
    sys_scl_sw_block_t* pb = NULL;

    /* get sys entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, entry_id, &pe, &pg, &pb);
    if (!pg)
    {
        return CTC_E_SCL_GROUP_UNEXIST;
    }

    if(!pe)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (!SCL_ENTRY_IS_TCAM(pe->key.type)) /* hash entry */
    {
        return CTC_E_SCL_HASH_ENTRY_NO_PRIORITY;
    }

    /* tcam entry check pb */
    if (pb == NULL)
    {
        return CTC_E_INVALID_PTR;
    }

    fpa_size = p_gg_scl_master[lchip]->alloc_id[pe->key.type];
    CTC_ERROR_RETURN(fpa_goldengate_set_entry_prio(p_gg_scl_master[lchip]->fpa, &pe->fpae, &pb->fpab, fpa_size, priority)); /* may move fpa entries */

    return CTC_E_NONE;
}

/*
 * set priority of tcam entry
 */
int32
sys_goldengate_scl_set_entry_priority(uint8 lchip, uint32 entry_id, uint32 priority, uint8 inner)
{
    SYS_SCL_INIT_CHECK(lchip);

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_CHECK_ENTRY_PRIO(priority);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: eid %u -- prio %u \n", entry_id, priority);

    SYS_SCL_LOCK(lchip);
    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_set_entry_priority(lchip, entry_id, priority, inner));
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * get multiple entries id
 */
int32
sys_goldengate_scl_get_multi_entry(uint8 lchip, ctc_scl_query_t* query, uint8 inner)
{
    uint32                entry_index = 0;
    sys_scl_sw_group_t    * pg        = NULL;
    struct ctc_slistnode_s* pe        = NULL;

    SYS_SCL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(query);
    if (!inner)
    {
        SYS_SCL_CHECK_OUTER_GROUP_ID(query->group_id);
    }

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: gid %u -- entry_sz %u\n", query->group_id, query->entry_size);

    SYS_SCL_LOCK(lchip);
    /* get group node */
    _sys_goldengate_scl_get_group_by_gid(lchip, query->group_id, &pg);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    if (query->entry_size == 0)
    {
        query->entry_count = pg->entry_count;
    }
    else
    {
        uint32* p_array = query->entry_array;
        if (NULL == p_array)
        {
            SYS_SCL_UNLOCK(lchip);
            return CTC_E_INVALID_PTR;
        }

        if (query->entry_size > pg->entry_count)
        {
            query->entry_size = pg->entry_count;
        }

        CTC_SLIST_LOOP(pg->entry_list, pe)
        {
            *p_array = ((sys_scl_sw_entry_t *) pe)->fpae.entry_id;
            p_array++;
            entry_index++;
            if (entry_index == query->entry_size)
            {
                break;
            }
        }

        query->entry_count = query->entry_size;
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}

/*
 * copy tcam entry to tcam group in sw table
 */
int32
sys_goldengate_scl_copy_entry(uint8 lchip, ctc_scl_copy_entry_t* copy_entry, uint8 inner)
{
    sys_scl_sw_entry_t * pe_src = NULL;
    sys_scl_sw_entry_t * pe_dst    = NULL;
    sys_scl_sw_action_t* pa_dst    = NULL;
    sys_scl_sw_block_t * pb_dst    = NULL;
    sys_scl_sw_group_t * pg_dst    = NULL;
    uint32             block_index = 0;
    uint8              fpa_size    = 0;
    int32              ret         = 0;
    uint8              is_hash;

    SYS_SCL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(copy_entry);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(copy_entry->dst_entry_id);
        SYS_SCL_CHECK_ENTRY_ID(copy_entry->src_entry_id);
        SYS_SCL_CHECK_OUTER_GROUP_ID(copy_entry->dst_group_id);
    }
    SYS_SCL_DBG_FUNC();

    SYS_SCL_LOCK(lchip);
    /* check src entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, copy_entry->src_entry_id, &pe_src, NULL, NULL);
    if (!pe_src)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    is_hash = !(SCL_ENTRY_IS_TCAM(pe_src->key.type));
    if (is_hash) /* hash entry not support copy */
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_SCL_HASH_ENTRY_UNSUPPORT_COPY;
    }

    /* check dst entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, copy_entry->dst_entry_id, &pe_dst, NULL, NULL);
    if (pe_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }

    _sys_goldengate_scl_get_group_by_gid(lchip, copy_entry->dst_group_id, &pg_dst);
    SYS_SCL_CHECK_GROUP_UNEXIST(pg_dst);

    if (pg_dst->group_info.type == CTC_SCL_GROUP_TYPE_HASH)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    MALLOC_ZERO(MEM_SCL_MODULE, pe_dst, p_gg_scl_master[lchip]->entry_size[pe_src->key.type]);
    if (!pe_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    MALLOC_ZERO(MEM_SCL_MODULE, pa_dst, sizeof(sys_scl_sw_action_t));
    if (!pa_dst)
    {
        SYS_SCL_UNLOCK(lchip);
        mem_free(pe_dst);
        return CTC_E_NO_MEMORY;
    }

    /* copy entry. if old entry has vlan edit. use it, no need to malloc.
                   hash not support copy, tcam ad is unique, so must malloc.*/

    sal_memcpy(pe_dst, pe_src, p_gg_scl_master[lchip]->entry_size[pe_src->key.type]);
    sal_memcpy(pa_dst, pe_src->action, sizeof(sys_scl_sw_action_t));
    pe_dst->action = pa_dst;

    if ((SYS_SCL_ACTION_INGRESS == pe_src->action->type)
        && (CTC_FLAG_ISSET(pe_src->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
    {   /* vlan edit share pool ++ */
        /* pv_get be NULL, */
        CTC_ERROR_GOTO(_sys_goldengate_scl_hash_vlan_edit_spool_add(lchip, pe_dst, pe_dst->action->u0.ia.vlan_edit, NULL), ret, cleanup);
    }


    pe_dst->fpae.entry_id    = copy_entry->dst_entry_id;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->fpae.priority = pe_src->fpae.priority;
    pe_dst->group            = pg_dst;
    pe_dst->head.next        = NULL;

    fpa_size = p_gg_scl_master[lchip]->alloc_id[pe_dst->key.type];
    pb_dst    = &p_gg_scl_master[lchip]->block[pg_dst->group_info.block_id];
    if (NULL == pb_dst)
    {
        ret = CTC_E_INVALID_PTR;
        goto cleanup;
    }

    /* get block index, based on priority */

    CTC_ERROR_GOTO(fpa_goldengate_alloc_offset(p_gg_scl_master[lchip]->fpa, &(pb_dst->fpab), fpa_size,
                                    pe_src->fpae.priority, &block_index), ret, cleanup);
    pe_dst->fpae.offset_a = block_index;

    /* add to hash */
    if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry, pe_dst))
    {
        ret = CTC_E_SCL_HASH_INSERT_FAILED;
        goto cleanup;
    }

    if (SCL_INNER_ENTRY_ID(pe_dst->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(pe_dst->key.type))
    {
        if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry_by_key, pe_dst))
        {
            ret = CTC_E_SCL_HASH_INSERT_FAILED;
            goto error_3;
        }
    }

    /* add to group */
    if(NULL == ctc_slist_add_head(pg_dst->entry_list, &(pe_dst->head)))
    {
        ret = CTC_E_INVALID_PARAM;
        goto error_2;
    }

    /* mark flag */
    pe_dst->fpae.flag = FPA_ENTRY_FLAG_UNINSTALLED;


    /* add to block */
    pb_dst->fpab.entries[block_index] = &pe_dst->fpae;
    /* set new priority */
    CTC_ERROR_GOTO(_sys_goldengate_scl_set_entry_priority(lchip, copy_entry->dst_entry_id, pe_src->fpae.priority, inner), ret, error_1);
    (pg_dst->entry_count)++;
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
error_1:
    if (pg_dst->entry_list)
    {
        ctc_slist_delete(pg_dst->entry_list);
    }
error_2:
    if (p_gg_scl_master[lchip]->entry_by_key)
    {
        ctc_hash_remove(p_gg_scl_master[lchip]->entry_by_key, pe_dst);
    }
error_3:
    if (p_gg_scl_master[lchip]->entry)
    {
        ctc_hash_remove(p_gg_scl_master[lchip]->entry, pe_dst);
    }
cleanup:
    SYS_SCL_UNLOCK(lchip);
    mem_free(pa_dst);
    mem_free(pe_dst);
    return ret;
}


/*
 * update entry action in sw table, update hw if entry installed
 */
int32
sys_goldengate_scl_update_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner)
{
    sys_scl_sw_entry_t     * pe_old    = NULL;
    sys_scl_sw_action_t    new_action;
    sys_scl_sw_vlan_edit_t new_vlan_edit;

    int32                  ret = 0;
    sys_scl_sw_action_t    * pa_get = NULL;
    sys_scl_sw_vlan_edit_t * pv_get = NULL;

    SYS_SCL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(action);
    SYS_SCL_DBG_FUNC();

    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    sal_memset(&new_action, 0, sizeof(new_action));
    sal_memset(&new_vlan_edit, 0, sizeof(new_vlan_edit));

    new_action.type = action->type;
    new_action.action_flag = action->action_flag;
    {
        if (SYS_SCL_ACTION_INGRESS == new_action.type)
        {
            new_action.u0.ia.stats_id = action->u.igs_action.stats_id;
        }
        else if(SYS_SCL_ACTION_EGRESS == new_action.type)
        {
            new_action.u0.ea.stats_id = action->u.egs_action.stats_id;
        }
        else if(SYS_SCL_ACTION_FLOW == new_action.type)
        {
            new_action.u0.fa.stats_id = action->u.flow_action.stats_id;
        }
    }

    SYS_SCL_LOCK(lchip);
    /* lookup */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, entry_id, &pe_old, NULL, NULL);
    if (!pe_old)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_check_action_type(lchip, &pe_old->key, action->type));

    CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_map_action(lchip, action, &new_action, &new_vlan_edit));

    if (SCL_ENTRY_IS_TCAM(pe_old->key.type))
    {
        /* 1. add vlan edit*/
        if ((SYS_SCL_ACTION_INGRESS == action->type)
            && (CTC_FLAG_ISSET(new_action.u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_hash_vlan_edit_spool_add(lchip, pe_old, &new_vlan_edit, &pv_get));

            if (pv_get)
            {
                new_action.u0.ia.vlan_edit                 = pv_get;
                new_action.u0.ia.vlan_action_profile_valid = 1;

                new_action.u0.ia.chip.profile_id = pv_get->chip.profile_id;

            }
        }

        /* if old action has vlan edit. delete it. */
        if ((SYS_SCL_ACTION_INGRESS == pe_old->action->type)
            && (CTC_FLAG_ISSET(pe_old->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pe_old->action->u0.ia.vlan_edit));
        }

        sal_memcpy(pe_old->action, &new_action, sizeof(sys_scl_sw_action_t));
    }
    else
    {
        /* 1. add vlan edit*/
        if ((SYS_SCL_ACTION_INGRESS == action->type)
            && (CTC_FLAG_ISSET(new_action.u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_hash_vlan_edit_spool_add(lchip, pe_old, &new_vlan_edit, &pv_get));

            if (pv_get)
            {
                new_action.u0.ia.vlan_edit                 = pv_get;
                new_action.u0.ia.vlan_action_profile_valid = 1;


                new_action.u0.ia.chip.profile_id = pv_get->chip.profile_id;

            }
        }

        /* 1. add ad , only ingress hash ad use spool*/
        if (SYS_SCL_ACTION_EGRESS != action->type)
        {
            ret = (_sys_goldengate_scl_hash_ad_spool_add(lchip, pe_old, &new_action, &pa_get));
            if FAIL(ret)
            {
                if (pv_get)
                {
                    _sys_goldengate_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pv_get);
                }
                SYS_SCL_UNLOCK(lchip);
                return ret;
            }
        }
        else
        {
            ret = (_sys_goldengate_scl_egs_hash_ad_add(lchip, pe_old, &new_action, &pa_get));
            if FAIL(ret)
            {
                SYS_SCL_UNLOCK(lchip);
                return ret;
            }
        }


        /* if old action has vlan edit. delete it. */
        if ((SYS_SCL_ACTION_INGRESS == pe_old->action->type)
            && (CTC_FLAG_ISSET(pe_old->action->u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {
            CTC_ERROR_GOTO(_sys_goldengate_scl_hash_vlan_edit_spool_remove(lchip, pe_old, pe_old->action->u0.ia.vlan_edit), ret, error);
        }

        if (SYS_SCL_ACTION_EGRESS == action->type)
        {
            mem_free(pe_old->action);
            pe_old->action = NULL;
        }
        else
        {
            /* old action must be delete */
            CTC_ERROR_GOTO(_sys_goldengate_scl_hash_ad_spool_remove(lchip, pe_old, pe_old->action), ret, error);
        }

        pe_old->action = pa_get;  /* update entry action */
    }

    if (pe_old->fpae.flag == FPA_ENTRY_FLAG_INSTALLED)
    {

        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_add_hw(lchip, entry_id));

    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
error:
    SYS_SCL_UNLOCK(lchip);
    if (pa_get)
    {
        mem_free(pa_get);
    }
    return ret;
}

int32
sys_goldengate_scl_set_hash_field_sel(uint8 lchip, ctc_scl_hash_field_sel_t* field_sel)
{
    SclFlowHashFieldSelect_m   ds_sel;
    uint32 cmd = 0;
    SYS_SCL_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(field_sel);
    CTC_MAX_VALUE_CHECK(field_sel->field_sel_id, 3);

    sal_memset(&ds_sel, 0, sizeof(SclFlowHashFieldSelect_m));
    if (CTC_SCL_KEY_HASH_L2 == field_sel->key_type)
    {
        SetSclFlowHashFieldSelect(V,etherTypeEn_f,   &ds_sel, !!field_sel->u.l2.eth_type);
        SetSclFlowHashFieldSelect(V,svlanIdValidEn_f,&ds_sel, !!field_sel->u.l2.tag_valid);
        SetSclFlowHashFieldSelect(V,stagCosEn_f,     &ds_sel, !!field_sel->u.l2.cos);
        SetSclFlowHashFieldSelect(V,svlanIdEn_f,     &ds_sel, !!field_sel->u.l2.vlan_id);
        SetSclFlowHashFieldSelect(V,stagCfiEn_f,     &ds_sel, !!field_sel->u.l2.cfi);
        SetSclFlowHashFieldSelect(V,macSaEn_f,       &ds_sel, !!field_sel->u.l2.mac_sa);
        SetSclFlowHashFieldSelect(V,macDaEn_f,       &ds_sel, !!field_sel->u.l2.mac_da);
        SetSclFlowHashFieldSelect(V,userIdLabelEn_f,    &ds_sel, !!field_sel->u.l2.class_id);

        cmd = DRV_IOW(SclFlowHashFieldSelect_t, DRV_ENTRY_FLAG);
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &ds_sel));

    return CTC_E_NONE;
}


/*
 * init scl register
 */
STATIC int32
_sys_goldengate_scl_init_register(uint8 lchip, void* scl_global_cfg)
{

    uint32 cmd   = 0;
    uint32 value = 0;

    cmd = DRV_IOW(UserIdHashLookupPortBaseCtl_t, UserIdHashLookupPortBaseCtl_localPhyPortBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    value = SYS_SCL_DEFAULT_ENTRY_PORT_BASE;
    cmd   = DRV_IOW(UserIdHashLookupPortBaseCtl_t, UserIdHashLookupPortBaseCtl_localPhyPortBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &value));

    cmd   = DRV_IOW(UserIdHashLookupCtl_t, UserIdHashLookupCtl_userIdDefaultEntryBase_f);
    value = p_gg_scl_master[lchip]->igs_default_base * 2;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    cmd   = DRV_IOW(UserIdHashLookupCtl_t, UserIdHashLookupCtl_tunnelIdDefaultEntryBase_f);
    value = p_gg_scl_master[lchip]->tunnel_default_base * 2;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    /* for DsSclFlow force decap use, init to 1 */
    cmd = DRV_IOW(IpeSclFlowCtl_t, IpeSclFlowCtl_removeByteShift_f);
    value = 1;
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_create_rsv_group(uint8 lchip)
{
    uint32               index = 0;
    ctc_scl_group_info_t global_group;
    ctc_scl_group_info_t hash_group;
    ctc_scl_group_info_t class_group;
    sal_memset(&global_group, 0, sizeof(global_group));
    sal_memset(&hash_group, 0, sizeof(hash_group));
    sal_memset(&class_group, 0, sizeof(class_group));

    global_group.type = CTC_SCL_GROUP_TYPE_GLOBAL;
    hash_group.type   = CTC_SCL_GROUP_TYPE_HASH;
    class_group.type  = CTC_SCL_GROUP_TYPE_PORT_CLASS;


    for (index = SYS_SCL_GROUP_ID_INNER_HASH_BEGIN; index <= SYS_SCL_GROUP_ID_INNER_HASH_END; index++)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, index, &hash_group, 1));
    }

    for (index = 0; index < 2; index++)
    {
        class_group.priority = index;  /*block id 0,1*/

        class_group.un.port_class_id = SYS_SCL_LABEL_RESERVE_FOR_IPSG;
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_IP_SRC_GUARD + index, &class_group, 1));

        class_group.un.port_class_id = SYS_SCL_LABEL_RESERVE_FOR_VLAN_CLASS;
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_CLASS + index, &class_group, 1));

        global_group.priority = index;
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_IP_TUNNEL + index, &global_group, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_MAC + index, &global_group, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV4 + index, &global_group, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_CLASS_IPV6 + index, &global_group, 1));
        CTC_ERROR_RETURN(sys_goldengate_scl_create_group(lchip, SYS_SCL_GROUP_ID_INNER_TCAM_OVERLAY_TUNNEL + index, &global_group, 1));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_opf_init(uint8 lchip, uint8 opf_type)
{
    uint32               entry_num    = 0;
    uint32               start_offset = 0;
    sys_goldengate_opf_t opf;

    sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));

    if (OPF_SCL_VLAN_ACTION == opf_type)
    {
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsVlanActionProfile_t, &entry_num));
        start_offset = SYS_SCL_VLAN_ACTION_RESERVE_NUM;
        entry_num   -= SYS_SCL_VLAN_ACTION_RESERVE_NUM;
    }
    else if (OPF_SCL_ENTRY_ID == opf_type)
    {
        start_offset = 0;
        entry_num    = SYS_SCL_MAX_INNER_ENTRY_ID - SYS_SCL_MIN_INNER_ENTRY_ID + 1;
    }

    if (entry_num)
    {
        CTC_ERROR_RETURN(sys_goldengate_opf_init(lchip, opf_type, 1));

        opf.pool_index = 0;
        opf.pool_type  = opf_type;
        CTC_ERROR_RETURN(sys_goldengate_opf_init_offset(lchip, &opf, start_offset, entry_num));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_vlan_edit_static_add(uint8 lchip, sys_scl_sw_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    sys_scl_sw_vlan_edit_t   * p_vlan_edit_new = NULL;
    uint32                   cmd               = 0;
    int32                    ret               = 0;
    DsVlanActionProfile_m ds_edit;

    SYS_SCL_DBG_FUNC();

    sal_memset(&ds_edit, 0, sizeof(ds_edit));

    MALLOC_ZERO(MEM_SCL_MODULE, p_vlan_edit_new, sizeof(sys_scl_sw_vlan_edit_t));
    if (NULL == p_vlan_edit_new)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memcpy(p_vlan_edit_new, p_vlan_edit, sizeof(sys_scl_sw_vlan_edit_t));
    p_vlan_edit_new->lchip = lchip;

    _sys_goldengate_scl_build_vlan_edit(lchip, &ds_edit, p_vlan_edit);


    p_vlan_edit_new->chip.profile_id = *p_prof_idx;
    if (ctc_spool_static_add(p_gg_scl_master[lchip]->vlan_edit_spool, p_vlan_edit_new) < 0)
    {
        ret = CTC_E_NO_MEMORY;
        goto cleanup;
    }

    cmd = DRV_IOW(DsVlanActionProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_vlan_edit_new->chip.profile_id, cmd, &ds_edit));


    *p_prof_idx = *p_prof_idx + 1;

    return CTC_E_NONE;

 cleanup:
    mem_free(p_vlan_edit_new);
    return ret;
}

STATIC int32
_sys_goldengate_scl_stag_edit_template_build(uint8 lchip, sys_scl_sw_vlan_edit_t* p_vlan_edit, uint16* p_prof_idx)
{
    uint8 stag_op = 0;

    SYS_SCL_DBG_FUNC();

    for (stag_op = CTC_VLAN_TAG_OP_NONE; stag_op < CTC_VLAN_TAG_OP_MAX; stag_op++)
    {
        p_vlan_edit->stag_op = stag_op;

        switch (stag_op)
        {
        case CTC_VLAN_TAG_OP_REP:         /* template has no replace */
        case CTC_VLAN_TAG_OP_VALID:       /* template has no valid */
            continue;
        case CTC_VLAN_TAG_OP_REP_OR_ADD:
        case CTC_VLAN_TAG_OP_ADD:
        {
            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_NEW;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));

            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));
        }
        break;
        case CTC_VLAN_TAG_OP_NONE:
        case CTC_VLAN_TAG_OP_DEL:
            p_vlan_edit->svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            p_vlan_edit->scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, p_vlan_edit, p_prof_idx));
            break;
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_rsv_vlan_edit(uint8 lchip)
{
    uint16                 prof_idx = 0;
    uint8                  ctag_op  = 0;

    sys_scl_sw_vlan_edit_t vlan_edit;
    sal_memset(&vlan_edit, 0, sizeof(vlan_edit));

    vlan_edit.vlan_domain = CTC_SCL_VLAN_DOMAIN_SVLAN;
    for (ctag_op = CTC_VLAN_TAG_OP_NONE; ctag_op < CTC_VLAN_TAG_OP_MAX; ctag_op++)
    {
        vlan_edit.ctag_op = ctag_op;

        switch (ctag_op)
        {
        case CTC_VLAN_TAG_OP_ADD:           /* template has no append ctag */
        case CTC_VLAN_TAG_OP_REP:           /* template has no replace */
        case CTC_VLAN_TAG_OP_VALID:         /* template has no valid */
            continue;
        case CTC_VLAN_TAG_OP_REP_OR_ADD:
        {
            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_NEW;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));

            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_NEW;
            CTC_ERROR_RETURN(_sys_goldengate_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));
        }
        break;
        case CTC_VLAN_TAG_OP_NONE:
        case CTC_VLAN_TAG_OP_DEL:
            vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
            CTC_ERROR_RETURN(_sys_goldengate_scl_stag_edit_template_build(lchip, &vlan_edit, &prof_idx));
            break;
        }
    }

    /*for swap*/
    vlan_edit.ctag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
    vlan_edit.stag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    vlan_edit.cvid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.ccos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    vlan_edit.svid_sl = CTC_VLAN_TAG_SL_AS_PARSE;
    vlan_edit.scos_sl = CTC_VLAN_TAG_SL_ALTERNATIVE;
    CTC_ERROR_RETURN(_sys_goldengate_scl_vlan_edit_static_add(lchip, &vlan_edit, &prof_idx));

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_group_compare(sys_scl_sw_group_t* p0, sys_scl_sw_group_t* p1)
{
    return((p0->group_id) - (p1->group_id));
}


STATIC bool
_sys_goldengate_scl_cmp_port_dir(uint8 lchip, sys_scl_sw_key_t* pk, uint16 user_port, uint8 user_dir)
{
    uint16 gport = 0;
    uint8  dir   = 0;
    switch (pk->type)
    {
    case SYS_SCL_KEY_HASH_PORT:
        gport = pk->u0.hash.u0.port.gport;
        dir   = pk->u0.hash.u0.port.dir;
        break;
    case SYS_SCL_KEY_HASH_PORT_CVLAN:
    case SYS_SCL_KEY_HASH_PORT_SVLAN:
        gport = pk->u0.hash.u0.vlan.gport;
        dir   = pk->u0.hash.u0.vlan.dir;
        break;
    case SYS_SCL_KEY_HASH_PORT_2VLAN:
    case SYS_SCL_KEY_HASH_PORT_CVLAN_COS:
    case SYS_SCL_KEY_HASH_PORT_SVLAN_COS:
        gport = pk->u0.hash.u0.vtag.gport;
        dir   = pk->u0.hash.u0.vtag.dir;
        break;
    case SYS_SCL_KEY_HASH_PORT_MAC:
        gport = pk->u0.hash.u0.mac.gport;
        dir   = CTC_INGRESS;
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV4:
        gport = pk->u0.hash.u0.ipv4.gport;
        dir   = CTC_INGRESS;
        break;
    case SYS_SCL_KEY_HASH_PORT_IPV6:
        gport = pk->u0.hash.u0.ipv6.gport;
        dir   = CTC_INGRESS;
        break;
    default:
        return FALSE;
    }

    if ((user_port == gport) && (dir == user_dir))
    {
        return TRUE;
    }

    return FALSE;
}

/*
 * remove all inner vlan mapping hash entry on given port
 */
int32
sys_goldengate_scl_remove_all_inner_hash_vlan(uint8 lchip, uint16 gport, uint8 dir)
{
    sys_scl_sw_group_t    * pg      = NULL;
    struct ctc_slistnode_s* pe      = NULL;
    struct ctc_slistnode_s* pe_next = NULL;
    sys_scl_sw_entry_t    * pe2     = NULL;
    uint32                eid       = 0;

    SYS_SCL_LOCK(lchip);
    if (CTC_INGRESS == dir)
    {
        _sys_goldengate_scl_get_group_by_gid(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_IGS, &pg);
    }
    else
    {
        _sys_goldengate_scl_get_group_by_gid(lchip, SYS_SCL_GROUP_ID_INNER_HASH_VLAN_MAPPING_EGS, &pg);
    }

    SYS_SCL_CHECK_GROUP_UNEXIST(pg);

    CTC_SLIST_LOOP_DEL(pg->entry_list, pe, pe_next)
    {
        pe2 = (sys_scl_sw_entry_t *) pe;

        eid = pe2->fpae.entry_id;
        if (_sys_goldengate_scl_cmp_port_dir(lchip, &pe2->key, gport, dir))
        {
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_install_entry(lchip, eid, SYS_SCL_ENTRY_OP_FLAG_DELETE, 1));
            CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_remove_entry(lchip, eid, 1));
        }
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


#define DEF_EID(lchip, dir, lport)    p_gg_scl_master[lchip]->def_eid[dir][lport]


int32
sys_goldengate_scl_set_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action)
{
    uint8  gchip = 0;

    uint16 lport = 0;
    uint8  dir   = 0;

    SYS_SCL_INIT_CHECK(lchip);
    SYS_SCL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(action);

    gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);

    if (!sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if (lport  >= SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_SCL_INVALID_DEFAULT_PORT;
    }

    if (SYS_SCL_ACTION_INGRESS == action->type)
    {
        dir = CTC_INGRESS;
    }
    else if (SYS_SCL_ACTION_EGRESS == action->type)
    {
        dir = CTC_EGRESS;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }


    CTC_ERROR_RETURN(sys_goldengate_scl_update_action(lchip, DEF_EID(lchip, dir, lport), action, 1));

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_get_default_action(uint8 lchip, uint16 gport, sys_scl_action_t* action)
{
    uint8  gchip = 0;

    uint16 lport = 0;
    uint8  dir   = 0;

    SYS_SCL_INIT_CHECK(lchip);
    SYS_SCL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(action);

    gchip = CTC_MAP_GPORT_TO_GCHIP(gport);
    lport = CTC_MAP_GPORT_TO_LPORT(gport);
    if (!sys_goldengate_chip_is_local(lchip, gchip))
    {
        return CTC_E_NONE;
    }
    if (lport >= SYS_GG_MAX_PORT_NUM_PER_CHIP)
    {
        return CTC_E_SCL_INVALID_DEFAULT_PORT;
    }

    if (SYS_SCL_ACTION_INGRESS == action->type)
    {
        dir = CTC_INGRESS;
    }
    else if (SYS_SCL_ACTION_EGRESS == action->type)
    {
        dir = CTC_EGRESS;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_get_action(lchip, DEF_EID(lchip, dir, lport), action, 1));

    return CTC_E_NONE;
}

int32
_sys_goldengate_scl_map_drv_tunnel_type(uint8 lchip, uint8 key_type, uint8* drv_key_type)
{
    uint8 drv_tmp_key_type = 0;

    switch(key_type)
    {
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4;
            break;
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4GREKEY;
            break;
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4RPF;
            break;
        case SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4DA;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE0;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4UCNVGREMODE1;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4MCNVGREMODE0;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_V4_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4NVGREMODE1;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE0;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6UCNVGREMODE1;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE0;
            break;
        case SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6MCNVGREMODE1;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE0;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4UCVXLANMODE1;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4MCVXLANMODE0;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_V4_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4VXLANMODE1;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE0;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6UCVXLANMODE1;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV4MCVXLANMODE0;
            break;
        case SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELIPV6MCVXLANMODE1;
            break;
        case SYS_SCL_KEY_HASH_TRILL_UC:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELTRILLUCDECAP;
            break;
        case SYS_SCL_KEY_HASH_TRILL_UC_RPF:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELTRILLUCRPF;
            break;
        case SYS_SCL_KEY_HASH_TRILL_MC:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELTRILLMCDECAP;
            break;
        case SYS_SCL_KEY_HASH_TRILL_MC_RPF:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELTRILLMCRPF;
            break;
        case SYS_SCL_KEY_HASH_TRILL_MC_ADJ:
            drv_tmp_key_type = USERIDHASHTYPE_TUNNELTRILLMCADJ;
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;
    }

    *drv_key_type = drv_tmp_key_type-USERIDHASHTYPE_TUNNELIPV4;

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_set_default_tunnel_action(uint8 lchip, uint8 key_type, sys_scl_action_t* action)
{
    DsTunnelId_m tunnel_id;
    uint8 drv_key_type = 0;
    uint32 ad_index = 0;
    uint32 cmd  = 0;

    SYS_SCL_INIT_CHECK(lchip);
    SYS_SCL_DBG_FUNC();
    CTC_PTR_VALID_CHECK(action);

    sal_memset(&tunnel_id, 0, sizeof(DsTunnelId_m));

    if ((key_type < SYS_SCL_KEY_HASH_TUNNEL_IPV4) || (key_type > SYS_SCL_KEY_HASH_TRILL_MC_ADJ)
        || (SYS_SCL_ACTION_TUNNEL != action->type))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_scl_build_tunnel_action
                             (lchip, &tunnel_id, &action->u.tunnel_action));

    CTC_ERROR_RETURN(_sys_goldengate_scl_map_drv_tunnel_type(lchip, key_type, &drv_key_type));

    ad_index = p_gg_scl_master[lchip]->tunnel_default_base + drv_key_type;

    SYS_SCL_DBG_INFO("  %% INFO: key_type %d ad_index %d \n", key_type, ad_index);

    cmd = DRV_IOW(DsTunnelId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, ad_index, cmd, &tunnel_id));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_init_default_entry(uint8 lchip)
{
    sys_scl_entry_t scl;
    uint16          lport;
    uint8           gchip    = 0;
    uint8           dir      = 0;
    uint8           key_type = 0;
    uint8           act_type = 0;
    uint32          gid      = 0;
    uint32          cmd      = 0;
    uint8           index    = 0;
    DsTunnelId_m tunnelid;

    SYS_SCL_INIT_CHECK(lchip);

    sal_memset(&scl, 0, sizeof(scl));
    sal_memset(&tunnelid, 0, sizeof(DsTunnelId_m));

    sys_goldengate_get_gchip_id(lchip, &gchip);
    for (dir = CTC_INGRESS; dir < CTC_BOTH_DIRECTION; dir++)
    {
        if ((CTC_EGRESS == dir)&&(0 == p_gg_scl_master[lchip]->egs_entry_num))
        {
            continue;
        }
        key_type = (CTC_INGRESS == dir) ? SYS_SCL_KEY_PORT_DEFAULT_INGRESS : SYS_SCL_KEY_PORT_DEFAULT_EGRESS;
        act_type = (CTC_INGRESS == dir) ? CTC_SCL_ACTION_INGRESS : CTC_SCL_ACTION_EGRESS;
        gid      = (CTC_INGRESS == dir) ? SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS : SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS;
        for (lport = 0; lport < SYS_GG_MAX_PORT_NUM_PER_CHIP; lport++)
        {

            scl.key.type                     = key_type;
            scl.key.u.port_default_key.gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

            scl.action.type = act_type;

            /* do nothing */
            CTC_ERROR_RETURN(sys_goldengate_scl_add_entry(lchip, gid, &scl, 1));
            p_gg_scl_master[lchip]->def_eid[dir][lport] = scl.entry_id;
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_IGS, NULL, 1));
    if (p_gg_scl_master[lchip]->egs_entry_num)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_install_group(lchip, SYS_SCL_GROUP_ID_INNER_DEFT_VLAN_MAPPING_EGS, NULL, 1));
    }

    /* init ip-tunnel default entry */
    for (index = 0; index <= (USERIDHASHTYPE_TUNNELMPLS - USERIDHASHTYPE_TUNNELIPV4); index++)
    {
        cmd = DRV_IOW(DsTunnelId_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_gg_scl_master[lchip]->tunnel_default_base + index, cmd, &tunnelid));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_ftm_scl_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_SCL_INIT_CHECK(lchip);

    specs_info->used_size = p_gg_scl_master[lchip]->action_count[SYS_SCL_ACTION_INGRESS];

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_ftm_tunnel_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_SCL_INIT_CHECK(lchip);

    specs_info->used_size = p_gg_scl_master[lchip]->action_count[SYS_SCL_ACTION_TUNNEL];

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_ftm_flow_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_SCL_INIT_CHECK(lchip);

    specs_info->used_size = p_gg_scl_master[lchip]->action_count[SYS_SCL_ACTION_FLOW];

    return CTC_E_NONE;
}


/*
 * init scl module
 */
int32
sys_goldengate_scl_init(uint8 lchip, void* scl_global_cfg)
{
    uint8       s0    = 0;
    uint8       s1    = 0;
    uint8       idx1  = 0;
    uint8       block_id = 0;

    uint32      size                    = 0;
    uint32      pb_sz[SCL_BLOCK_ID_NUM] = {0};
    uint32      entry_num[SCL_BLOCK_ID_NUM][CTC_FPA_KEY_SIZE_NUM] = {{0}};
    uint32      hash_num                = 0;
    uint32      ad_num                  = 0;
    uint32      vedit_entry_size        = 0;
    hash_cmp_fn cmp_action              = 0 ;
    uint8       array_idx               = 0;
    uint32      offset                  = 0;
    ctc_spool_t spool;

    LCHIP_CHECK(lchip);
    /* check init */
    if (NULL != p_gg_scl_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_memset(&spool, 0, sizeof(ctc_spool_t));
     /*CTC_PTR_VALID_CHECK(scl_global_cfg);*/

    /* init block_sz */
    for (block_id = 0; block_id < SCL_BLOCK_ID_NUM; block_id++)
    {
        size = 0;
        if(0 == block_id)
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId0MacKey160_t, &pb_sz[block_id]));
        }
        else
        {
            CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId1MacKey160_t, &pb_sz[block_id]));
        }
        for(idx1 = 0; idx1 < CTC_FPA_KEY_SIZE_NUM; idx1++)
        {
            /* block id refer to drv_ftm_tcam_key_type_t
               idx1 refer to DRV_FTM_TCAM_SIZE_XXX */
            CTC_ERROR_RETURN(sys_goldengate_ftm_get_entry_num_by_size(lchip, 5+block_id, idx1, &(entry_num[block_id][idx1])));
            size += entry_num[block_id][idx1];
            if(size > pb_sz[block_id])
            {
                entry_num[block_id][idx1] -= (size-pb_sz[block_id]);
                break;
            }
        }
        if(size < pb_sz[block_id])
        {
            entry_num[block_id][CTC_FPA_KEY_SIZE_160] += (pb_sz[block_id] - size);
        }
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserIdPortHashKey_t, &hash_num));

    {
        uint32 sum = 0;
        sum = pb_sz[0] + pb_sz[1] + hash_num;
        if (!sum)
        {   /* no resources */
            return CTC_E_NONE;
        }
    }

    /* malloc master */
    MALLOC_ZERO(MEM_SCL_MODULE, p_gg_scl_master[lchip], sizeof(sys_scl_master_t));
    if (NULL == p_gg_scl_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

#define SYS_SCL_GROUP_HASH_BLOCK_SIZE           32
#define SYS_SCL_ENTRY_HASH_BLOCK_SIZE           32
#define SYS_SCL_ENTRY_HASH_BLOCK_SIZE_BY_KEY    1024
    p_gg_scl_master[lchip]->group = ctc_hash_create(1,
                                        SYS_SCL_GROUP_HASH_BLOCK_SIZE,
                                        (hash_key_fn) _sys_goldengate_scl_hash_make_group,
                                        (hash_cmp_fn) _sys_goldengate_scl_hash_compare_group);

    p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_VLAN]        = CTC_FPA_KEY_SIZE_160;

    /* default mac tcam key use 320 bit  */
    p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en = 1;

    if (p_gg_scl_master[lchip]->mac_tcam_320bit_mode_en)
    {
        p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_MAC]     = CTC_FPA_KEY_SIZE_320;
    }
    else
    {
        p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_MAC]     = CTC_FPA_KEY_SIZE_160;
    }
    p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV4_SINGLE] = CTC_FPA_KEY_SIZE_160;
    p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV4]        = CTC_FPA_KEY_SIZE_320;
    p_gg_scl_master[lchip]->alloc_id[SYS_SCL_KEY_TCAM_IPV6]        = CTC_FPA_KEY_SIZE_640;

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsEgressXcOamPortHashKey_t, &(p_gg_scl_master[lchip]->egs_entry_num)));

    /* init max priority */
    p_gg_scl_master[lchip]->max_entry_priority = 0xFFFFFFFF;

    /* init count */
    sal_memset(p_gg_scl_master[lchip]->tunnel_count, 0, sizeof(p_gg_scl_master[lchip]->tunnel_count));
    sal_memset(p_gg_scl_master[lchip]->action_count, 0, sizeof(p_gg_scl_master[lchip]->action_count));

    /* init lchip num */


    /* init sort algor / hash table /share pool */
    {
        p_gg_scl_master[lchip]->fpa = fpa_goldengate_create(_sys_goldengate_scl_get_block_by_pe_fpa,
                                     _sys_goldengate_scl_entry_move_hw_fpa,
                                     sizeof(ctc_slistnode_t));
        /* init hash table :
         *    instead of using one linklist, scl use 32 linklist.
         *    hash caculatition is pretty fast, just label_id % hash_size
         */


        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsUserId_t, &ad_num));
        CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsVlanActionProfile_t, &vedit_entry_size));

        p_gg_scl_master[lchip]->entry = ctc_hash_create(1,
                                            SYS_SCL_ENTRY_HASH_BLOCK_SIZE,
                                            (hash_key_fn) _sys_goldengate_scl_hash_make_entry,
                                            (hash_cmp_fn) _sys_goldengate_scl_hash_compare_entry);

        p_gg_scl_master[lchip]->entry_by_key = ctc_hash_create(1,
                                                   SYS_SCL_ENTRY_HASH_BLOCK_SIZE,
                                                   (hash_key_fn) _sys_goldengate_scl_hash_make_entry_by_key,
                                                   (hash_cmp_fn) _sys_goldengate_scl_hash_compare_entry_by_key);

        spool.lchip = lchip;
        spool.block_num = 1;
        spool.block_size = vedit_entry_size / VLAN_ACTION_SIZE_PER_BUCKET;
        spool.max_count = vedit_entry_size;
        spool.user_data_size = sizeof(sys_scl_sw_vlan_edit_t);
        spool.spool_key = (hash_key_fn) _sys_goldengate_scl_hash_make_vlan_edit;
        spool.spool_cmp = (hash_cmp_fn) _sys_goldengate_scl_hash_compare_vlan_edit;
        p_gg_scl_master[lchip]->vlan_edit_spool = ctc_spool_create(&spool);

        cmp_action = (hash_cmp_fn) _sys_goldengate_scl_hash_compare_action0;

        for (array_idx = NORMAL_AD_SHARE_POOL; array_idx <= DEFAULT_AD_SHARE_POOL; array_idx++)
        {
            sal_memset(&spool, 0, sizeof(ctc_spool_t));
            spool.lchip = lchip;
            spool.block_num = 1;
            spool.block_size = SYS_SCL_ENTRY_HASH_BLOCK_SIZE_BY_KEY;
            spool.max_count = ad_num;
            spool.user_data_size = sizeof(sys_scl_sw_action_t);
            spool.spool_key = (hash_key_fn) _sys_goldengate_scl_hash_make_action;
            spool.spool_cmp = (hash_cmp_fn) cmp_action;
            p_gg_scl_master[lchip]->ad_spool[array_idx] = ctc_spool_create(&spool);

            if (!p_gg_scl_master[lchip]->ad_spool[array_idx])
            {
                goto ERROR_FREE_MEM0;
            }
        }
        /* init each tcam block */
        for (idx1 = 0; idx1 < SCL_BLOCK_ID_NUM; idx1++)
        {
            p_gg_scl_master[lchip]->block[idx1].fpab.entry_count = pb_sz[idx1];
            p_gg_scl_master[lchip]->block[idx1].fpab.free_count  = pb_sz[idx1];
            offset = 0;
            p_gg_scl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_160]    = offset;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_160] = entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_160]  = entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_160]  = 0;
            offset += entry_num[idx1][CTC_FPA_KEY_SIZE_160];
            p_gg_scl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_320]    = offset;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_320] = entry_num[idx1][CTC_FPA_KEY_SIZE_320]/2;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_320]  = entry_num[idx1][CTC_FPA_KEY_SIZE_320]/2;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_320]  = 0;
            offset += entry_num[idx1][CTC_FPA_KEY_SIZE_320];
            p_gg_scl_master[lchip]->block[idx1].fpab.start_offset[CTC_FPA_KEY_SIZE_640]    = offset;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_entry_count[CTC_FPA_KEY_SIZE_640] = entry_num[idx1][CTC_FPA_KEY_SIZE_640]/4;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_free_count[CTC_FPA_KEY_SIZE_640]  = entry_num[idx1][CTC_FPA_KEY_SIZE_640]/4;
            p_gg_scl_master[lchip]->block[idx1].fpab.sub_rsv_count[CTC_FPA_KEY_SIZE_640]  = 0;
            size = sizeof(sys_scl_sw_entry_t*) * pb_sz[idx1];
            MALLOC_ZERO(MEM_SCL_MODULE, p_gg_scl_master[lchip]->block[idx1].fpab.entries, size);
            if (NULL == p_gg_scl_master[lchip]->block[idx1].fpab.entries && size)
            {
                goto ERROR_FREE_MEM0;
            }
        }

        p_gg_scl_master[lchip]->group_list[0] = ctc_list_create((ctc_list_cmp_cb_t) _sys_goldengate_scl_group_compare, NULL);
        p_gg_scl_master[lchip]->group_list[1] = ctc_list_create((ctc_list_cmp_cb_t) _sys_goldengate_scl_group_compare, NULL);
        p_gg_scl_master[lchip]->group_list[2] = ctc_list_create((ctc_list_cmp_cb_t) _sys_goldengate_scl_group_compare, NULL);

        if (!(p_gg_scl_master[lchip]->fpa &&
              p_gg_scl_master[lchip]->entry &&
              p_gg_scl_master[lchip]->entry_by_key &&
              p_gg_scl_master[lchip]->vlan_edit_spool))
        {
            goto ERROR_FREE_MEM0;
        }
    }


    /* init entry size */
    {
        s0 = sizeof(sys_scl_sw_entry_t) - sizeof(sys_scl_sw_key_union_t);
        s1 = sizeof(sys_scl_sw_hash_key_t) - sizeof(sys_scl_sw_hash_key_union_t);


        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_VLAN             ] = s0 + sizeof(sys_scl_sw_tcam_key_vlan_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_MAC              ] = s0 + sizeof(sys_scl_sw_tcam_key_mac_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_IPV4             ] = s0 + sizeof(sys_scl_sw_tcam_key_ipv4_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_IPV4_SINGLE      ] = s0 + sizeof(sys_scl_sw_tcam_key_ipv4_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_TCAM_IPV6             ] = s0 + sizeof(sys_scl_sw_tcam_key_ipv6_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT             ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_CVLAN       ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vlan_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_SVLAN       ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vlan_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_2VLAN       ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_CVLAN_COS   ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_SVLAN_COS   ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_MAC              ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_mac_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_MAC         ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_mac_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_IPV4             ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv4_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_IPV4        ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv4_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_IPV6             ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv6_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_PORT_IPV6       ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_ipv6_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_L2              ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_l2_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4      ] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA   ] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE  ] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF  ] = s0 + s1 + sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_V4_MODE1   ] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_V4_MODE1   ] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1] = s0 + s1 + sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TRILL_UC] = s0 + s1 + sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TRILL_MC] = s0 + s1 + sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TRILL_UC_RPF] = s0 + s1 + sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TRILL_MC_RPF] = s0 + s1 + sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_HASH_TRILL_MC_ADJ] = s0 + s1 + sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_PORT_DEFAULT_INGRESS  ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);
        p_gg_scl_master[lchip]->entry_size[SYS_SCL_KEY_PORT_DEFAULT_EGRESS   ] = s0 + s1 + sizeof(sys_scl_sw_hash_key_port_t);

        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_VLAN             ] = sizeof(sys_scl_sw_tcam_key_vlan_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_MAC              ] = sizeof(sys_scl_sw_tcam_key_mac_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_IPV4             ] = sizeof(sys_scl_sw_tcam_key_ipv4_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_IPV4_SINGLE      ] = sizeof(sys_scl_sw_tcam_key_ipv4_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_TCAM_IPV6             ] = sizeof(sys_scl_sw_tcam_key_ipv6_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT             ] = sizeof(sys_scl_sw_hash_key_port_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_CVLAN       ] = sizeof(sys_scl_sw_hash_key_vlan_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_SVLAN       ] = sizeof(sys_scl_sw_hash_key_vlan_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_2VLAN       ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_CVLAN_COS   ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_SVLAN_COS   ] = sizeof(sys_scl_sw_hash_key_vtag_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_MAC              ] = sizeof(sys_scl_sw_hash_key_mac_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_MAC         ] = sizeof(sys_scl_sw_hash_key_mac_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_IPV4             ] = sizeof(sys_scl_sw_hash_key_ipv4_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_IPV4        ] = sizeof(sys_scl_sw_hash_key_ipv4_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_IPV6             ] = sizeof(sys_scl_sw_hash_key_ipv6_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_PORT_IPV6             ] = sizeof(sys_scl_sw_hash_key_ipv6_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_L2                 ] = sizeof(sys_scl_sw_hash_key_l2_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4      ] = sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA   ] = sizeof(sys_scl_hash_tunnel_ipv4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE  ] = sizeof(sys_scl_hash_tunnel_ipv4_gre_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF  ] = sizeof(sys_scl_hash_tunnel_ipv4_rpf_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_V4_MODE1   ] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_V4_MODE1   ] = sizeof(sys_scl_hash_overlay_tunnel_v4_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1] = sizeof(sys_scl_hash_overlay_tunnel_v6_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TRILL_UC] = sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TRILL_MC] = sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TRILL_UC_RPF] = sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TRILL_MC_RPF] = sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_HASH_TRILL_MC_ADJ] = sizeof(sys_scl_hash_trill_key_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_PORT_DEFAULT_INGRESS  ] = sizeof(sys_scl_sw_hash_key_port_t);
        p_gg_scl_master[lchip]->key_size[SYS_SCL_KEY_PORT_DEFAULT_EGRESS   ] = sizeof(sys_scl_sw_hash_key_port_t);

        p_gg_scl_master[lchip]->hash_igs_action_size =
            sizeof(sys_scl_sw_igs_action_t) - sizeof(sys_scl_sw_igs_action_chip_t) - sizeof(sys_scl_sw_vlan_edit_t*);

        p_gg_scl_master[lchip]->hash_egs_action_size =
            sizeof(sys_scl_sw_egs_action_t) - sizeof(sys_scl_sw_egs_action_chip_t);

        p_gg_scl_master[lchip]->hash_tunnel_action_size = sizeof(sys_scl_tunnel_action_t);

        p_gg_scl_master[lchip]->hash_flow_action_size =
            sizeof(sys_scl_sw_flow_action_t) - sizeof(sys_scl_sw_flow_action_chip_t) - sizeof(sys_scl_sw_vlan_edit_t*);

        p_gg_scl_master[lchip]->vlan_edit_size =
            sizeof(sys_scl_sw_vlan_edit_t) -
            ((sizeof(sys_scl_sw_vlan_edit_chip_t) < 4) ? 4 : sizeof(sys_scl_sw_vlan_edit_chip_t));
    }

    /* Ad type used as sclKeyType in tcam key */
    p_gg_scl_master[lchip]->ad_type[SYS_SCL_ACTION_INGRESS] = 0;   /* Ad is DsUerId */
    p_gg_scl_master[lchip]->ad_type[SYS_SCL_ACTION_EGRESS]  = 0;    /* egress hash not care */
    p_gg_scl_master[lchip]->ad_type[SYS_SCL_ACTION_TUNNEL]  = 1;    /* Ad is DsTunnel */
    p_gg_scl_master[lchip]->ad_type[SYS_SCL_ACTION_FLOW]    = 2;     /* Ad is DsSclFlow */
    SYS_SCL_CREATE_LOCK(lchip);

    CTC_ERROR_RETURN(_sys_goldengate_scl_create_rsv_group(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_scl_opf_init(lchip, OPF_SCL_VLAN_ACTION));
    CTC_ERROR_RETURN(_sys_goldengate_scl_opf_init(lchip, OPF_SCL_ENTRY_ID)); /* for alloc inner entry id */

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsUserId_t, 1, SYS_SCL_DEFAULT_ENTRY_BASE, &offset));
    p_gg_scl_master[lchip]->igs_default_base = offset;

    CTC_ERROR_RETURN(sys_goldengate_ftm_alloc_table_offset(lchip, DsTunnelId_t, 1, (USERIDHASHTYPE_TUNNELMPLS - USERIDHASHTYPE_TUNNELIPV4 + 1), &offset));
    p_gg_scl_master[lchip]->tunnel_default_base = offset;

    CTC_ERROR_RETURN(_sys_goldengate_scl_rsv_vlan_edit(lchip));

    CTC_ERROR_RETURN(_sys_goldengate_scl_init_default_entry(lchip));
    CTC_ERROR_RETURN(_sys_goldengate_scl_init_register(lchip, NULL)); /* after init default entry */

    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_SCL, sys_goldengate_scl_ftm_scl_cb);
    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_TUNNEL, sys_goldengate_scl_ftm_tunnel_cb);
    sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_SCL_FLOW, sys_goldengate_scl_ftm_flow_cb);

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_SCL, sys_goldengate_scl_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_scl_wb_restore(lchip));
    }

    return CTC_E_NONE;


 ERROR_FREE_MEM0:

    return CTC_E_SCL_INIT;
}

STATIC int32
_sys_goldengate_scl_free_node_data(void* node_data, void* user_data)
{
    uint32 free_entry_hash = 0;
    sys_scl_sw_entry_t* pe = NULL;

    if (user_data)
    {
        free_entry_hash = *(uint32*)user_data;
        if (1 == free_entry_hash)
        {
            pe = (sys_scl_sw_entry_t*)node_data;
            if (SCL_ENTRY_IS_TCAM(pe->key.type) || (pe->action->type == SYS_SCL_ACTION_EGRESS))
            {
                mem_free(pe->action);
            }
        }
    }
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_deinit(uint8 lchip)
{
    uint8 array_idx = 0;
    uint8 idx = 0;
    ctc_listnode_t     * next_node            = NULL;
    ctc_listnode_t     * node                 = NULL;
    sys_scl_sw_group_t * pg                   = NULL;
    uint32 free_entry_hash = 1;

    LCHIP_CHECK(lchip);
    if (NULL == p_gg_scl_master[lchip])
    {
        return CTC_E_NONE;
    }

    sys_goldengate_opf_deinit(lchip, OPF_SCL_VLAN_ACTION);
    sys_goldengate_opf_deinit(lchip, OPF_SCL_ENTRY_ID);

    for (idx = 0; idx < SCL_BLOCK_ID_NUM; idx++)
    {
        mem_free(p_gg_scl_master[lchip]->block[idx].fpab.entries);
    }

    /*free scl entry*/
    ctc_hash_traverse(p_gg_scl_master[lchip]->entry, (hash_traversal_fn)_sys_goldengate_scl_free_node_data, &free_entry_hash);
    ctc_hash_free(p_gg_scl_master[lchip]->entry);
    ctc_hash_free(p_gg_scl_master[lchip]->entry_by_key);

    /*free ad*/
    for (array_idx = NORMAL_AD_SHARE_POOL; array_idx <= DEFAULT_AD_SHARE_POOL; array_idx++)
    {
        ctc_spool_free(p_gg_scl_master[lchip]->ad_spool[array_idx]);
    }

    /*free vlan edit spool*/
    ctc_spool_free(p_gg_scl_master[lchip]->vlan_edit_spool);

    /*free scl sw group*/
    for (idx=0; idx< SYS_SCL_GROUP_LIST_NUM; idx++)
    {
        CTC_LIST_LOOP_DEL(p_gg_scl_master[lchip]->group_list[idx], pg, node, next_node)
        {
            ctc_slist_free(pg->entry_list);
            ctc_listnode_delete(p_gg_scl_master[lchip]->group_list[idx], pg);
        }
        ctc_list_free(p_gg_scl_master[lchip]->group_list[idx]);
    }

    /*free scl group*/
    ctc_hash_traverse(p_gg_scl_master[lchip]->group, (hash_traversal_fn)_sys_goldengate_scl_free_node_data, NULL);
    ctc_hash_free(p_gg_scl_master[lchip]->group);

    fpa_goldengate_free(p_gg_scl_master[lchip]->fpa);

    sal_mutex_destroy(p_gg_scl_master[lchip]->mutex);
    mem_free(p_gg_scl_master[lchip]);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_unmap_egs_action(uint8 lchip, ctc_scl_egs_action_t*       pc,
                                     sys_scl_sw_egs_action_t*    pea)
{
    uint32 flag;

    flag = pea->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_EGS_ACTION_FLAG_VLAN_EDIT))
    {
        pc->vlan_edit.svid_new = pea->svid;
        pc->vlan_edit.scos_new = pea->scos;
        pc->vlan_edit.scfi_new = pea->scfi;
        pc->vlan_edit.cvid_new = pea->cvid;
        pc->vlan_edit.ccos_new = pea->ccos;
        pc->vlan_edit.ccfi_new = pea->ccfi;

        pc->vlan_edit.stag_op = pea->stag_op;
        pc->vlan_edit.ctag_op = pea->ctag_op;
        pc->vlan_edit.svid_sl = pea->svid_sl;
        pc->vlan_edit.scos_sl = pea->scos_sl;
        pc->vlan_edit.scfi_sl = pea->scfi_sl;
        pc->vlan_edit.cvid_sl = pea->cvid_sl;
        pc->vlan_edit.ccos_sl = pea->ccos_sl;
        pc->vlan_edit.ccfi_sl = pea->ccfi_sl;
        pc->vlan_edit.tpid_index = pea->tpid_index;
    }

    pc->flag = flag;

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_scl_unmap_tunnel_action(uint8 lchip, sys_scl_tunnel_action_t*       pc,
                                     sys_scl_tunnel_action_t*    pta)
{
    pc->router_mac_profile_en = pta->router_mac_profile_en;
    pc->router_mac_profile = pta->router_mac_profile;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_scl_unmap_igs_action(uint8 lchip, ctc_scl_igs_action_t* pc,
                                     sys_scl_sw_igs_action_t* pia)
{
    uint32 flag;

    flag     = pia->flag;

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_FID))
    {
        pc->fid = pia->fid;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VRFID))
    {
        pc->vrf_id = pia->fid;
    }

    if (pia->service_policer_id)
    {
        pc->policer_id = pia->service_policer_id;
    }
    /* other 2 sub flag only need unmap flag. */

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STATS))
    {
        pc->stats_id = pia->stats_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_L2VPN_OAM))
    {
        pc->l2vpn_oam_id = pia->l2vpn_oam_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_LOGIC_PORT))
    {
        pc->logic_port.logic_port      = pia->bind.bds.logic_src_port;
        pc->logic_port.logic_port_type = pia->bind.bds.logic_port_type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_REDIRECT))
    {
        pc->nh_id = pia->nh_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_STP_ID))
    {
        pc->stp_id = pia->stp_id;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_DSCP))
    {
        pc->dscp = pia->dscp;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY))
    {
        pc->priority = pia->priority;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_COLOR))
    {
        pc->color = pia->color;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_PRIORITY_AND_COLOR))
    {
        pc->priority = pia->priority;
        pc->color = pia->color;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_USER_VLANPTR))
    {
        pc->user_vlanptr = pia->user_vlan_ptr;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_BINDING_EN))
    {
        uint8 type = 0;
        if (pia->binding_mac_sa)
        {
            type = CTC_SCL_BIND_TYPE_MACSA;
        }
        else
        {
            switch (pia->bind_type)
            {
            case 0:
                type           = CTC_SCL_BIND_TYPE_PORT;
                pc->bind.gport = pia->bind.gport;
                break;
            case 1:
                type             = CTC_SCL_BIND_TYPE_VLAN;
                pc->bind.vlan_id = pia->bind.vlan_id;
                break;
            case 2:
                type             = CTC_SCL_BIND_TYPE_IPV4SA;
                pc->bind.ipv4_sa = pia->bind.ipv4_sa;
                break;
            case 3:
                pc->bind.vlan_id = pia->bind.ip_vlan.vlan_id;
                pc->bind.ipv4_sa = pia->bind.ip_vlan.ipv4_sa;
                type             = CTC_SCL_BIND_TYPE_IPV4SA_VLAN;
                break;
            }
        }

        pc->bind.type = type;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_APS))
    {
        pc->aps.aps_select_group_id  = pia->bind.bds.aps_select_group_id;
        pc->aps.is_working_path      = !pia->bind.bds.aps_select_protecting_path;
        pc->aps.protected_vlan       = pia->user_vlan_ptr;
        pc->aps.protected_vlan_valid = (pia->user_vlan_ptr) ? 1 : 0;
    }

    if (CTC_FLAG_ISSET(flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT))
    {
        pc->vlan_edit.svid_new = pia->svid;
        pc->vlan_edit.scos_new = pia->scos;
        pc->vlan_edit.scfi_new = pia->scfi;
        pc->vlan_edit.cvid_new = pia->cvid;
        pc->vlan_edit.ccos_new = pia->ccos;
        pc->vlan_edit.ccfi_new = pia->ccfi;

        if (pia->vlan_edit)
        {
            pc->vlan_edit.vlan_domain = pia->vlan_edit->vlan_domain;
            pc->vlan_edit.tpid_index = pia->vlan_edit->tpid_index;
            pc->vlan_edit.stag_op = pia->vlan_edit->stag_op;
            pc->vlan_edit.ctag_op = pia->vlan_edit->ctag_op;
            pc->vlan_edit.svid_sl = pia->vlan_edit->svid_sl;
            pc->vlan_edit.scos_sl = pia->vlan_edit->scos_sl;
            pc->vlan_edit.scfi_sl = pia->vlan_edit->scfi_sl;
            pc->vlan_edit.cvid_sl = pia->vlan_edit->cvid_sl;
            pc->vlan_edit.ccos_sl = pia->vlan_edit->ccos_sl;
            pc->vlan_edit.ccfi_sl = pia->vlan_edit->ccfi_sl;
        }
    }

    pc->flag = flag;
    pc->sub_flag = pia->sub_flag;

    return CTC_E_NONE;
}

/*
 * get ctc action from entry in sw table
 */
int32
sys_goldengate_scl_get_action(uint8 lchip, uint32 entry_id, sys_scl_action_t* action, uint8 inner)
{
    sys_scl_sw_entry_t* pe = NULL;

    SYS_SCL_INIT_CHECK(lchip);

    SYS_SCL_DBG_FUNC();
    SYS_SCL_DBG_PARAM("  %% PARAM: entry_id %u \n", entry_id);
    if (!inner)
    {
        SYS_SCL_CHECK_ENTRY_ID(entry_id);
    }

    SYS_SCL_LOCK(lchip);
    /* get sys entry */
    _sys_goldengate_scl_get_nodes_by_eid(lchip, entry_id, &pe, NULL, NULL);
    if (!pe)
    {
        SYS_SCL_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    action->type = pe->action->type;
    action->action_flag = pe->action->action_flag;
    switch (pe->action->type)
    {
    case SYS_SCL_ACTION_INGRESS:
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_unmap_igs_action(lchip, &action->u.igs_action, &pe->action->u0.ia));
        break;

    case SYS_SCL_ACTION_EGRESS:
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_unmap_egs_action(lchip, &action->u.egs_action, &pe->action->u0.ea));
        break;

    case SYS_SCL_ACTION_TUNNEL: /* no requirement to support for now. */
        CTC_ERROR_RETURN_SCL_UNLOCK(_sys_goldengate_scl_unmap_tunnel_action(lchip, &action->u.tunnel_action, &pe->action->u0.ta));
        break;
    case SYS_SCL_ACTION_FLOW:   /* no requirement to support for now. */
        break;

    default:
        break;
    }
    SYS_SCL_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
_sys_goldengate_scl_wb_mapping_group(sys_wb_scl_sw_group_t *p_wb_scl_group, sys_scl_sw_group_t *p_scl_group, uint8 sync)
{
    if (sync)
    {
        p_wb_scl_group->group_id = p_scl_group->group_id;
        p_wb_scl_group->flag = p_scl_group->flag;
        sal_memcpy(&p_wb_scl_group->group_info, &p_scl_group->group_info, sizeof(sys_scl_sw_group_info_t));
    }
    else
    {
        p_scl_group->group_id = p_wb_scl_group->group_id;
        p_scl_group->flag = p_wb_scl_group->flag;
        p_scl_group->entry_list = ctc_slist_new();
        CTC_PTR_VALID_CHECK(p_scl_group->entry_list);
        sal_memcpy(&p_scl_group->group_info, &p_wb_scl_group->group_info, sizeof(sys_scl_sw_group_info_t));
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_scl_wb_mapping_entry(uint8 lchip, sys_wb_scl_sw_entry_t *p_wb_scl_entry, sys_scl_sw_entry_t *p_scl_entry, uint8 sync)
{
    uint8 step = 0;
    int32 ret = CTC_E_NONE;
    uint8 array_idx = NORMAL_AD_SHARE_POOL;
    uint32 cmd = 0;
    uint32 value = 0;
    sys_scl_sw_block_t * pb = NULL;
    sys_scl_sw_group_t *p_scl_group = NULL;
    sys_scl_sw_action_t* pa_new = NULL;
    sys_scl_sw_action_t* pa_get = NULL; /* get from share pool*/
    sys_scl_sw_vlan_edit_t *p_vlan_edit_new = NULL;
    sys_scl_sw_vlan_edit_t *p_vlan_edit_get = NULL;
    sys_goldengate_opf_t opf;
    DsVlanActionProfile_m ds_vlan_profile;

    if (sync)
    {
        p_wb_scl_entry->group_id = p_scl_entry->group->group_id;
        sal_memcpy(&p_wb_scl_entry->fpae, &p_scl_entry->fpae, sizeof(ctc_fpa_entry_t));
        sal_memcpy(&p_wb_scl_entry->action, p_scl_entry->action, sizeof(sys_scl_sw_action_t));
        sal_memcpy(&p_wb_scl_entry->key, &p_scl_entry->key, sizeof(sys_scl_sw_key_t));
    }
    else
    {

        p_scl_entry->lchip = lchip;
        sal_memcpy(&p_scl_entry->fpae, &p_wb_scl_entry->fpae, sizeof(ctc_fpa_entry_t));
        sal_memcpy(&p_scl_entry->key, &p_wb_scl_entry->key, sizeof(sys_scl_sw_key_t));

        if ((SYS_SCL_ACTION_INGRESS == p_wb_scl_entry->action.type)
            && (CTC_FLAG_ISSET(p_wb_scl_entry->action.u0.ia.flag, CTC_SCL_IGS_ACTION_FLAG_VLAN_EDIT)))
        {

            p_vlan_edit_new = mem_malloc(MEM_SCL_MODULE, sizeof(sys_scl_sw_vlan_edit_t));
            if (NULL == p_vlan_edit_new)
            {
                ret = CTC_E_NO_MEMORY;

                goto done;
            }
            sal_memset(p_vlan_edit_new, 0, sizeof(sys_scl_sw_vlan_edit_t));

            sal_memset(&ds_vlan_profile, 0, sizeof(DsVlanActionProfile_m));
            cmd = DRV_IOR(DsVlanActionProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_wb_scl_entry->action.u0.ia.chip.profile_id, cmd, &ds_vlan_profile), ret, vlan_done);

            p_vlan_edit_new->svid_sl = GetDsVlanActionProfile(V, sVlanIdAction_f, &ds_vlan_profile);
            p_vlan_edit_new->scos_sl = GetDsVlanActionProfile(V, sCosAction_f, &ds_vlan_profile);
            p_vlan_edit_new->scfi_sl = GetDsVlanActionProfile(V, sCfiAction_f, &ds_vlan_profile);
            p_vlan_edit_new->cvid_sl = GetDsVlanActionProfile(V, cVlanIdAction_f, &ds_vlan_profile);
            p_vlan_edit_new->ccos_sl = GetDsVlanActionProfile(V, cCosAction_f, &ds_vlan_profile);
            p_vlan_edit_new->ccfi_sl = GetDsVlanActionProfile(V, cCfiAction_f, &ds_vlan_profile);
            value = GetDsVlanActionProfile(V, sTagAction_f, &ds_vlan_profile);

            if (GetDsVlanActionProfile(V, stagModifyMode_f, &ds_vlan_profile))
            {
                p_vlan_edit_new->stag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
            }
            else if (0 == value)
            {
                p_vlan_edit_new->stag_op = CTC_VLAN_TAG_OP_NONE;
            }
            else if (1 == value)
            {
                p_vlan_edit_new->stag_op = CTC_VLAN_TAG_OP_REP;
            }
            else if (2 == value)
            {
                p_vlan_edit_new->stag_op = CTC_VLAN_TAG_OP_ADD;
            }
            else if (3 == value)
            {
                p_vlan_edit_new->stag_op = CTC_VLAN_TAG_OP_DEL;
            }

             value = GetDsVlanActionProfile(V, cTagAction_f, &ds_vlan_profile);

            if (GetDsVlanActionProfile(V, ctagModifyMode_f, &ds_vlan_profile))
            {
                p_vlan_edit_new->ctag_op = CTC_VLAN_TAG_OP_REP_OR_ADD;
            }
            else if (0 == value)
            {
                p_vlan_edit_new->ctag_op = CTC_VLAN_TAG_OP_NONE;
            }
            else if (1 == value)
            {
                p_vlan_edit_new->ctag_op = CTC_VLAN_TAG_OP_REP;
            }
            else if (2 == value)
            {
                p_vlan_edit_new->ctag_op = CTC_VLAN_TAG_OP_ADD;
            }
            else if (3 == value)
            {
                p_vlan_edit_new->ctag_op = CTC_VLAN_TAG_OP_DEL;
            }

            value = GetDsVlanActionProfile(V, outerVlanStatus_f, &ds_vlan_profile);
            switch (value)
            {
            case 2:
                p_vlan_edit_new->vlan_domain = CTC_SCL_VLAN_DOMAIN_SVLAN;
                break;
            case 1:
                p_vlan_edit_new->vlan_domain = CTC_SCL_VLAN_DOMAIN_CVLAN;
                break;
            case 0:
                p_vlan_edit_new->vlan_domain = CTC_SCL_VLAN_DOMAIN_UNCHANGE;
                break;
            default:
                break;
            }

            p_vlan_edit_new->chip.profile_id = p_wb_scl_entry->action.u0.ia.chip.profile_id;
            p_vlan_edit_new->lchip = lchip;

            ret = ctc_spool_add(p_gg_scl_master[lchip]->vlan_edit_spool, p_vlan_edit_new, NULL, &p_vlan_edit_get);

            if (ret < 0)
            {
                ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                goto vlan_done;
            }
            else if (CTC_SPOOL_E_OPERATE_MEMORY == ret)
            {
                sal_memset(&opf, 0, sizeof(sys_goldengate_opf_t));
                opf.pool_index = 0;
                opf.pool_type  = OPF_SCL_VLAN_ACTION;

                ret = sys_goldengate_opf_alloc_offset_from_position(lchip, &opf, 1, p_vlan_edit_new->chip.profile_id);
                if (ret)
                {
                    ctc_spool_remove(p_gg_scl_master[lchip]->vlan_edit_spool, p_vlan_edit_new, NULL);
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc DsVlanActionProfile ftm table offset from position %u error! ret: %d.\n", p_vlan_edit_new->chip.profile_id, ret);
                    goto vlan_done;
                }

                p_vlan_edit_get->chip.profile_id = p_vlan_edit_new->chip.profile_id;
            }
            else
            {
                mem_free(p_vlan_edit_new);
            }
        }


        if (!SCL_ENTRY_IS_TCAM(p_wb_scl_entry->key.type) && (SYS_SCL_ACTION_EGRESS != p_wb_scl_entry->action.type))
        {
            /* malloc sys action */
            MALLOC_ZERO(MEM_SCL_MODULE, pa_new, sizeof(sys_scl_sw_action_t));
            if (!pa_new)
            {
                ret = CTC_E_NO_MEMORY;
                goto vlan_done;
            }
            sal_memcpy(pa_new, &p_wb_scl_entry->action, sizeof(sys_scl_sw_action_t));

            if (SYS_SCL_IS_DEFAULT(p_scl_entry->key.type))
            {
                array_idx = DEFAULT_AD_SHARE_POOL;
            }

            /* -- set action ptr -- */
            ret = ctc_spool_add(p_gg_scl_master[lchip]->ad_spool[array_idx], pa_new, NULL, &pa_get);
            if (ret < 0)
            {
                ret = CTC_E_SPOOL_ADD_UPDATE_FAILED;
                goto vlan_done;
            }
            else if ((CTC_SPOOL_E_OPERATE_MEMORY == ret) && (NORMAL_AD_SHARE_POOL == array_idx))
            {
                ret = sys_goldengate_ftm_alloc_table_offset_from_position(lchip, DsUserId_t, 0, 1, pa_get->ad_index);
                if (ret < 0)
                {
                    ctc_spool_remove(p_gg_scl_master[lchip]->ad_spool[array_idx], pa_new, NULL);
                    CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "alloc DsUserId ftm table offset from position %u error! ret: %d.\n", pa_get->ad_index, ret);
                    goto vlan_done;
                }
            }
            else if (CTC_SPOOL_E_OPERATE_REFCNT == ret)
            {
                mem_free(pa_new);
            }

            p_scl_entry->action = pa_get;
        }
        else
        {
            MALLOC_ZERO(MEM_SCL_MODULE, p_scl_entry->action, sizeof(sys_scl_sw_action_t));
            if (!p_scl_entry->action)
            {
                ret = CTC_E_NO_MEMORY;
                goto vlan_done;
            }
            sal_memcpy(p_scl_entry->action, &p_wb_scl_entry->action, sizeof(sys_scl_sw_action_t));
        }

        p_scl_entry->action->u0.ia.vlan_edit = p_vlan_edit_get;

        _sys_goldengate_scl_get_group_by_gid(lchip, p_wb_scl_entry->group_id, &p_scl_group);
        if (!p_scl_group)
        {
            ret = CTC_E_SCL_GROUP_UNEXIST;
            goto vlan_done;
        }
        p_scl_entry->group = p_scl_group;

        /* add to group entry list's head, error just return */
        if(NULL == ctc_slist_add_head(p_scl_group->entry_list, &(p_scl_entry->head)))
        {
            ret = CTC_E_INVALID_PARAM;
            goto vlan_done;
        }
        p_scl_group->entry_count++; /*because of reserve group don't restore, so entry_count don't store*/

        if (SCL_ENTRY_IS_TCAM(p_scl_entry->key.type))
        {
            pb       = &p_gg_scl_master[lchip]->block[p_scl_group->group_info.block_id];
            pb->fpab.entries[p_scl_entry->fpae.offset_a] = &p_scl_entry->fpae;

            step = SYS_SCL_MAP_FPA_SIZE_TO_STEP(p_gg_scl_master[lchip]->alloc_id[p_scl_entry->key.type]);
            pb->fpab.free_count -= step;
        }
    }

    return CTC_E_NONE;

vlan_done:
    mem_free(p_vlan_edit_new);

 done:
    mem_free(pa_new);

    return ret;
}

int32
_sys_goldengate_scl_wb_sync_group_func(sys_scl_sw_group_t *p_scl_group, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_scl_sw_group_t  *p_wb_scl_group;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_scl_group = (sys_wb_scl_sw_group_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_scl_group, 0, sizeof(sys_wb_scl_sw_group_t));

    if ((p_scl_group->group_id >= SYS_SCL_GROUP_ID_INNER_HASH_BEGIN && p_scl_group->group_id <= SYS_SCL_GROUP_ID_INNER_HASH_END) ||
        (p_scl_group->group_id >= SYS_SCL_GROUP_ID_INNER_TCAM_BEGIN && p_scl_group->group_id < SYS_SCL_GROUP_ID_INNER_TCAM_VLAN_MAPPING_BASE))
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_scl_wb_mapping_group(p_wb_scl_group, p_scl_group, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
_sys_goldengate_scl_wb_sync_entry_func(sys_scl_sw_entry_t *p_scl_entry, void *user_data)
{
    uint16 max_entry_cnt = 0;
    sys_wb_scl_sw_entry_t  *p_wb_scl_entry;
    sys_traverse_t *data = (sys_traverse_t *)user_data;
    ctc_wb_data_t *wb_data = (ctc_wb_data_t *)(data->data);
    uint8 lchip = (uint8)(data->value1);

    max_entry_cnt = CTC_WB_DATA_BUFFER_LENGTH / (wb_data->key_len + wb_data->data_len);

    p_wb_scl_entry = (sys_wb_scl_sw_entry_t *)wb_data->buffer + wb_data->valid_cnt;
    sal_memset(p_wb_scl_entry, 0, sizeof(sys_wb_scl_sw_entry_t));

    if (p_scl_entry->key.type == SYS_SCL_KEY_PORT_DEFAULT_INGRESS || p_scl_entry->key.type == SYS_SCL_KEY_PORT_DEFAULT_EGRESS)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(_sys_goldengate_scl_wb_mapping_entry(lchip, p_wb_scl_entry, p_scl_entry, 1));
    if (++wb_data->valid_cnt == max_entry_cnt)
    {
        CTC_ERROR_RETURN(ctc_wb_add_entry(wb_data));
        wb_data->valid_cnt = 0;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_scl_wb_sync(uint8 lchip)
{
    uint16 loop = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_traverse_t user_data;
    sys_wb_scl_master_t  *p_wb_scl_master;

    /*syncup  scl_matser*/
    wb_data.buffer = mem_malloc(MEM_SCL_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_scl_master_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_MASTER);

    p_wb_scl_master = (sys_wb_scl_master_t *)wb_data.buffer;

    p_wb_scl_master->lchip = lchip;

    for (loop = 0; loop < SCL_BLOCK_ID_NUM; loop++)
    {
        sal_memcpy(p_wb_scl_master->block[loop].start_offset, p_gg_scl_master[lchip]->block[loop].fpab.start_offset, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
        sal_memcpy(p_wb_scl_master->block[loop].sub_entry_count, p_gg_scl_master[lchip]->block[loop].fpab.sub_entry_count, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
        sal_memcpy(p_wb_scl_master->block[loop].sub_free_count, p_gg_scl_master[lchip]->block[loop].fpab.sub_free_count, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
    }

    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

    /*syncup scl group*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_scl_sw_group_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_GROUP);
    user_data.data = &wb_data;

    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_scl_master[lchip]->group, (hash_traversal_fn) _sys_goldengate_scl_wb_sync_group_func, (void *)&user_data), ret, done);
    if (wb_data.valid_cnt > 0)
    {
        CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);
        wb_data.valid_cnt = 0;
    }

    /*syncup scl entry*/
    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_scl_sw_entry_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_ENTRY);
    user_data.data = &wb_data;
    user_data.value1 = lchip;

    CTC_ERROR_GOTO(ctc_hash_traverse(p_gg_scl_master[lchip]->entry, (hash_traversal_fn) _sys_goldengate_scl_wb_sync_entry_func, (void *)&user_data), ret, done);
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
sys_goldengate_scl_wb_restore(uint8 lchip)
{
    uint16 loop = 0;
    uint16 entry_cnt = 0;
    uint32 key_id = 0;
    uint32 act_id = 0;
    uint8 count_size  = 0;
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_scl_master_t  *p_wb_scl_master;
    sys_scl_sw_group_t *p_scl_group;
    sys_wb_scl_sw_group_t  *p_wb_scl_group;
    sys_scl_sw_entry_t *p_entry;
    sys_wb_scl_sw_entry_t *p_wb_entry;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_SCL_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_scl_master_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore scl_master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query scl master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }

    p_wb_scl_master = (sys_wb_scl_master_t *)wb_query.buffer;

    for (loop = 0; loop < SCL_BLOCK_ID_NUM; loop++)
    {
        sal_memcpy(p_gg_scl_master[lchip]->block[loop].fpab.start_offset, p_wb_scl_master->block[loop].start_offset, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
        sal_memcpy(p_gg_scl_master[lchip]->block[loop].fpab.sub_entry_count, p_wb_scl_master->block[loop].sub_entry_count, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
        sal_memcpy(p_gg_scl_master[lchip]->block[loop].fpab.sub_free_count, p_wb_scl_master->block[loop].sub_free_count, sizeof(uint16) * CTC_FPA_KEY_SIZE_NUM);
    }

    /*restore scl group*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_scl_sw_group_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_GROUP);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_scl_group = (sys_wb_scl_sw_group_t *)wb_query.buffer + entry_cnt++;

        p_scl_group = mem_malloc(MEM_SCL_MODULE, sizeof(sys_scl_sw_group_t));
        if (NULL == p_scl_group)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_scl_group, 0, sizeof(sys_scl_sw_group_t));

        ret = _sys_goldengate_scl_wb_mapping_group(p_wb_scl_group, p_scl_group, 0);
        if (ret)
        {
            continue;
        }

        /*add to soft table*/
        ctc_hash_insert(p_gg_scl_master[lchip]->group, p_scl_group);

        if (p_scl_group->group_id < 0xFFFF0001) /* outer tcam */
        {
            ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[0], p_scl_group);
        }
        else if (p_scl_group->group_id <= CTC_SCL_GROUP_ID_HASH_MAX) /* outer hash */
        {
            ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[1], p_scl_group);
        }
        else /* inner */
        {
            ctc_listnode_add_sort(p_gg_scl_master[lchip]->group_list[2], p_scl_group);
        }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

    /*restore scl entry*/
    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_scl_sw_entry_t, CTC_FEATURE_SCL, SYS_WB_APPID_SCL_SUBID_ENTRY);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
        p_wb_entry = (sys_wb_scl_sw_entry_t *)wb_query.buffer + entry_cnt++;

        p_entry = mem_malloc(MEM_SCL_MODULE, sizeof(sys_scl_sw_entry_t));
        if (NULL == p_entry)
        {
            ret = CTC_E_NO_MEMORY;

            goto done;
        }
        sal_memset(p_entry, 0, sizeof(sys_scl_sw_entry_t));

        ret = _sys_goldengate_scl_wb_mapping_entry(lchip, p_wb_entry, p_entry, 0);
        if (ret)
        {
            continue;
        }

        /* add to hash, error just return */
        if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry, p_entry))
        {
            return CTC_E_SCL_HASH_INSERT_FAILED;
        }

        if (SCL_INNER_ENTRY_ID(p_entry->fpae.entry_id) || !SCL_ENTRY_IS_TCAM(p_entry->key.type))
        {
            /* add to hash by key */
            if(NULL == ctc_hash_insert(p_gg_scl_master[lchip]->entry_by_key, p_entry))
            {
                return CTC_E_SCL_HASH_INSERT_FAILED;
            }
        }

        if(!SCL_ENTRY_IS_TCAM(p_entry->key.type) && !SYS_SCL_IS_DEFAULT(p_entry->key.type))
        {
            CTC_ERROR_RETURN(_sys_goldengate_scl_get_table_id(lchip, 0, p_entry->key.type, p_entry->action->type, &key_id, &act_id));
            count_size = TABLE_ENTRY_SIZE(key_id) / 12;
            p_gg_scl_master[lchip]->action_count[p_entry->action->type] = p_gg_scl_master[lchip]->action_count[p_entry->action->type] + count_size;
        }

        if(p_entry->key.tunnel_type)
        {
            (p_gg_scl_master[lchip]->tunnel_count[p_entry->key.tunnel_type])++;
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


